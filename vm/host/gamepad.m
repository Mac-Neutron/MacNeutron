// GameController + IOHIDManager + CoreHaptics virtio-console gamepad host bridge (clean-room MIT).
#import "gamepad.h"
#import "gamepad_hid.h"
#import "gamepad_rumble.h"
#import <Dispatch/Dispatch.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !__has_feature(objc_arc)
#define MN_RETAIN(obj) [(obj) retain]
#define MN_RELEASE(obj) do { if ((obj) != nil) { [(obj) release]; (obj) = nil; } } while (0)
#define MN_DISPATCH_RELEASE(obj) do { if ((obj) != nil) { dispatch_release((obj)); (obj) = nil; } } while (0)
#else
#define MN_RETAIN(obj) (obj)
#define MN_RELEASE(obj) do { (obj) = nil; } while (0)
#define MN_DISPATCH_RELEASE(obj) do { (obj) = nil; } while (0)
#endif

static const uint16_t k_key_codes[13] = {
    304, /* BTN_SOUTH / BTN_A */
    305, /* BTN_EAST  / BTN_B */
    307, /* BTN_NORTH / BTN_X */
    308, /* BTN_WEST  / BTN_Y */
    310, /* BTN_TL */
    311, /* BTN_TR */
    312, /* BTN_TL2 */
    313, /* BTN_TR2 */
    314, /* BTN_SELECT */
    315, /* BTN_START */
    316, /* BTN_MODE */
    317, /* BTN_THUMBL */
    318, /* BTN_THUMBR */
};

static const uint16_t k_abs_codes[6] = {
    0, /* ABS_X */
    1, /* ABS_Y */
    2, /* ABS_Z (LT) */
    3, /* ABS_RX */
    4, /* ABS_RY */
    5, /* ABS_RZ (RT) */
};

static const char *k_std_axes =
    "0:-32768:32767:16:128,1:-32768:32767:16:128,2:0:255:0:0,"
    "3:-32768:32767:16:128,4:-32768:32767:16:128,5:0:255:0:0,"
    "16:-1:1:0:0,17:-1:1:0:0";

/* Minimal valid HID gamepad report descriptor (Report ID 1: 8 buttons, X/Y axes, 2B output, 4B feature). */
static const uint8_t k_selftest_hid_desc[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x05, 0x09,       /*   Usage Page (Button) */
    0x19, 0x01,       /*   Usage Minimum (1) */
    0x29, 0x08,       /*   Usage Maximum (8) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x25, 0x01,       /*   Logical Maximum (1) */
    0x75, 0x01,       /*   Report Size (1) */
    0x95, 0x08,       /*   Report Count (8) */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x05, 0x01,       /*   Usage Page (Generic Desktop) */
    0x09, 0x30,       /*   Usage (X) */
    0x09, 0x31,       /*   Usage (Y) */
    0x15, 0x81,       /*   Logical Minimum (-127) */
    0x25, 0x7F,       /*   Logical Maximum (127) */
    0x75, 0x08,       /*   Report Size (8) */
    0x95, 0x02,       /*   Report Count (2) */
    0x81, 0x02,       /*   Input (Data,Var,Abs) */
    0x06, 0x00, 0xFF, /*   Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,       /*   Usage (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
    0x75, 0x08,       /*   Report Size (8) */
    0x95, 0x02,       /*   Report Count (2) */
    0x91, 0x02,       /*   Output (Data,Var,Abs) */
    0x09, 0x02,       /*   Usage (2) */
    0x75, 0x08,       /*   Report Size (8) */
    0x95, 0x04,       /*   Report Count (4) */
    0xB1, 0x02,       /*   Feature (Data,Var,Abs) */
    0xC0              /* End Collection */
};

typedef enum {
    ST_PHASE_IDLE = 0,
    ST_PHASE_UINPUT_WAIT_RUMBLE = 1,
    ST_PHASE_UINPUT_RUMBLING = 2,
    ST_PHASE_UHID_ACTIVE = 3,
} mn_selftest_phase_t;

typedef struct {
    int wr_fd;
    int rd_fd;
    dispatch_source_t rd_src;
    dispatch_source_t wr_src;
    bool write_resumed;
    mn_pad_txq_t txq;
    char rx_buf[16384];
    size_t rx_len;
    bool rx_discard;

    bool hello_seen;
    bool caps_hid;
    bool uinput_active;

    GCController *controller;
    mn_pad_profile_t profile;
    mn_pad_ev_state_t prev_ev;

    struct mn_hid_device *hid_dev;

    mn_selftest_phase_t st_phase;
    bool st_seen_nonzero_rumble;
    bool st_pulse_high;
    bool st_got_hid_output;
    bool st_got_hid_get;
    bool st_got_hid_set;
    dispatch_source_t st_timer;
} mn_pad_slot_t;

typedef struct {
    struct mn_hid_device *dev;
    mn_hid_dev_info_t info;
    int assigned_slot;
} mn_hid_avail_t;

#define MN_MAX_AVAIL_HID 16

static struct {
    bool initialized;
    bool paused;
    bool selftest;
    uint64_t gen;
    int nslots;
    mn_pad_slot_t slots[MN_GAMEPAD_MAX_SLOTS];
    mn_hid_avail_t avail_hid[MN_MAX_AVAIL_HID];
    id connect_obs;
    id disconnect_obs;
} g_pad;

/* --- Pure math, mapping & encoding helpers --- */

static float clampf_range(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t scale_axis_s16(float v) {
    v = clampf_range(v, -1.0f, 1.0f);
    long r = lrintf(v * 32767.0f);
    if (r < -32768L) r = -32768L;
    if (r > 32767L) r = 32767L;
    return (int32_t)r;
}

static int32_t scale_trigger_u8(float v) {
    v = clampf_range(v, 0.0f, 1.0f);
    long r = lrintf(v * 255.0f);
    if (r < 0L) r = 0L;
    if (r > 255L) r = 255L;
    return (int32_t)r;
}

void mn_pad_apply_radial_deadzone(float x, float y, float deadzone, float *out_x, float *out_y) {
    if (!out_x || !out_y) {
        return;
    }
    if (deadzone < 0.0f) {
        deadzone = 0.0f;
    }
    float r = sqrtf(x * x + y * y);
    if (r <= deadzone || deadzone >= 1.0f || r == 0.0f) {
        *out_x = 0.0f;
        *out_y = 0.0f;
        return;
    }
    float norm_r = (r - deadzone) / (1.0f - deadzone);
    if (norm_r > 1.0f) {
        norm_r = 1.0f;
    }
    float scale = norm_r / r;
    *out_x = clampf_range(x * scale, -1.0f, 1.0f);
    *out_y = clampf_range(y * scale, -1.0f, 1.0f);
}

void mn_pad_map_input(mn_pad_profile_t profile, const mn_pad_input_t *in, float deadzone, mn_pad_ev_state_t *out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!in) {
        return;
    }

    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    mn_pad_apply_radial_deadzone(in->lx, in->ly, deadzone, &lx, &ly);
    mn_pad_apply_radial_deadzone(in->rx, in->ry, deadzone, &rx, &ry);

    /* GCController +Y is up; Linux evdev ABS_Y / ABS_RY +Y is down. */
    out->abs[0] = scale_axis_s16(lx);
    out->abs[1] = scale_axis_s16(-ly);
    out->abs[2] = scale_trigger_u8(in->lt);
    out->abs[3] = scale_axis_s16(rx);
    out->abs[4] = scale_axis_s16(-ry);
    out->abs[5] = scale_trigger_u8(in->rt);

    out->hat_x = (in->dpad_x > 0.5f) ? 1 : (in->dpad_x < -0.5f) ? -1 : 0;
    /* GCController dpad +Y is up; Linux ABS_HAT0Y -1 is up, +1 is down. */
    out->hat_y = (in->dpad_y > 0.5f) ? -1 : (in->dpad_y < -0.5f) ? 1 : 0;

    out->keys[0] = in->a ? 1 : 0;
    out->keys[1] = in->b ? 1 : 0;
    if (profile == MN_PAD_PROFILE_XBOX360) {
        /* xpad.c maps left face button X -> BTN_X (307) and top face button Y -> BTN_Y (308). */
        out->keys[2] = in->x ? 1 : 0;
        out->keys[3] = in->y ? 1 : 0;
    } else {
        /* hid-sony / hid-playstation maps top Triangle -> BTN_NORTH (307) and left Square -> BTN_WEST (308). */
        out->keys[2] = in->y ? 1 : 0;
        out->keys[3] = in->x ? 1 : 0;
    }
    out->keys[4] = in->lb ? 1 : 0;
    out->keys[5] = in->rb ? 1 : 0;
    if (profile != MN_PAD_PROFILE_XBOX360) {
        out->keys[6] = (in->lt_btn || in->lt > 0.1f) ? 1 : 0;
        out->keys[7] = (in->rt_btn || in->rt > 0.1f) ? 1 : 0;
    }
    out->keys[8]  = in->options ? 1 : 0;
    out->keys[9]  = in->menu ? 1 : 0;
    out->keys[10] = in->home ? 1 : 0;
    out->keys[11] = in->l3 ? 1 : 0;
    out->keys[12] = in->r3 ? 1 : 0;
}

int mn_pad_encode_create(mn_pad_profile_t profile, char *buf, size_t cap) {
    if (!buf || cap == 0) {
        return -1;
    }
    uint16_t bus = 0x0003u, vid = 0, pid = 0, ver = 0;
    const char *keys = NULL;
    const char *name = NULL;

    switch (profile) {
    case MN_PAD_PROFILE_XBOX360:
        vid = 0x045eu;
        pid = 0x028eu;
        ver = 0x0114u;
        keys = "304,305,307,308,310,311,314,315,316,317,318";
        name = "Microsoft X-Box 360 pad";
        break;
    case MN_PAD_PROFILE_DUALSENSE:
        vid = 0x054cu;
        pid = 0x0ce6u;
        ver = 0x8111u;
        keys = "304,305,307,308,310,311,312,313,314,315,316,317,318";
        name = "Sony Interactive Entertainment DualSense Wireless Controller";
        break;
    case MN_PAD_PROFILE_DUALSHOCK4:
        vid = 0x054cu;
        pid = 0x09ccu;
        ver = 0x8111u;
        keys = "304,305,307,308,310,311,312,313,314,315,316,317,318";
        name = "Sony Interactive Entertainment Wireless Controller";
        break;
    default:
        return -1;
    }

    int n = snprintf(buf, cap, "create %04x %04x %04x %04x %s %s %s\n",
                     bus, vid, pid, ver, keys, k_std_axes, name);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

static int append_ev_tok(char *buf, size_t cap, size_t *pos, bool *any,
                         uint16_t type, uint16_t code, int32_t val) {
    int n = snprintf(buf + *pos, cap - *pos, "%s%u:%u:%d",
                     *any ? " " : "ev ", (unsigned)type, (unsigned)code, val);
    if (n < 0 || (size_t)n >= cap - *pos) {
        return -1;
    }
    *pos += (size_t)n;
    *any = true;
    return 0;
}

int mn_pad_encode_ev_diff(mn_pad_profile_t profile,
                          const mn_pad_ev_state_t *prev,
                          const mn_pad_ev_state_t *next,
                          char *buf,
                          size_t cap) {
    if (!next || !buf || cap < 2) {
        return -1;
    }
    mn_pad_ev_state_t zero;
    if (!prev) {
        memset(&zero, 0, sizeof(zero));
        prev = &zero;
    }

    size_t pos = 0;
    bool any = false;
    buf[0] = '\0';

    for (size_t i = 0; i < 13; i++) {
        if (profile == MN_PAD_PROFILE_XBOX360 && (i == 6 || i == 7)) {
            continue;
        }
        if (prev->keys[i] != next->keys[i]) {
            if (append_ev_tok(buf, cap, &pos, &any, 1u, k_key_codes[i], (int32_t)next->keys[i]) < 0) {
                return -1;
            }
        }
    }
    for (size_t i = 0; i < 6; i++) {
        if (prev->abs[i] != next->abs[i]) {
            if (append_ev_tok(buf, cap, &pos, &any, 3u, k_abs_codes[i], next->abs[i]) < 0) {
                return -1;
            }
        }
    }
    if (prev->hat_x != next->hat_x) {
        if (append_ev_tok(buf, cap, &pos, &any, 3u, 16u, next->hat_x) < 0) {
            return -1;
        }
    }
    if (prev->hat_y != next->hat_y) {
        if (append_ev_tok(buf, cap, &pos, &any, 3u, 17u, next->hat_y) < 0) {
            return -1;
        }
    }

    if (!any) {
        return 0;
    }
    if (pos + 2 > cap) {
        return -1;
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return (int)pos;
}

int mn_pad_encode_remove(char *buf, size_t cap) {
    if (!buf || cap < 8) {
        return -1;
    }
    memcpy(buf, "remove\n", 8);
    return 7;
}

static size_t write_hex(const uint8_t *src, size_t len, char *dst, size_t cap) {
    static const char hx[] = "0123456789abcdef";
    if (cap < len * 2u + 1u) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        dst[i * 2u] = hx[(src[i] >> 4) & 0x0fu];
        dst[i * 2u + 1u] = hx[src[i] & 0x0fu];
    }
    dst[len * 2u] = '\0';
    return len * 2u;
}

int mn_pad_encode_hid_create(uint16_t bus, uint16_t vid, uint16_t pid, uint16_t ver,
                             uint8_t country, const uint8_t *desc, size_t desc_len,
                             const char *name, char *buf, size_t cap) {
    if (!desc || desc_len == 0 || desc_len > MN_PAD_HID_MAX_DESC || !buf || cap == 0) {
        return -1;
    }
    char safe_name[128];
    const char *src_name = (name && name[0] != '\0') ? name : "HID Gamepad";
    size_t ni = 0;
    for (; src_name[ni] != '\0' && ni + 1 < sizeof(safe_name); ni++) {
        char c = src_name[ni];
        safe_name[ni] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    safe_name[ni] = '\0';

    int hdr = snprintf(buf, cap, "hid-create %04x %04x %04x %04x %02x ",
                       bus, vid, pid, ver, (unsigned)country);
    if (hdr < 0 || (size_t)hdr >= cap) {
        return -1;
    }
    size_t pos = (size_t)hdr;
    size_t hex_len = write_hex(desc, desc_len, buf + pos, cap - pos);
    if (hex_len == 0) {
        return -1;
    }
    pos += hex_len;
    int tail = snprintf(buf + pos, cap - pos, " %s\n", safe_name);
    if (tail < 0 || (size_t)tail >= cap - pos) {
        return -1;
    }
    return (int)(pos + (size_t)tail);
}

int mn_pad_encode_hid_input(const uint8_t *report, size_t len, char *buf, size_t cap) {
    if (!report || len == 0 || len > MN_PAD_HID_MAX_REP || !buf || cap < 12u + len * 2u) {
        return -1;
    }
    memcpy(buf, "hid-input ", 10);
    size_t pos = 10;
    size_t hex_len = write_hex(report, len, buf + pos, cap - pos);
    if (hex_len == 0 || pos + hex_len + 2u > cap) {
        return -1;
    }
    pos += hex_len;
    buf[pos++] = '\n';
    buf[pos] = '\0';
    return (int)pos;
}

int mn_pad_encode_hid_get_reply(uint32_t req_id, uint16_t err,
                                const uint8_t *data, size_t len,
                                char *buf, size_t cap) {
    if (!buf || cap == 0) {
        return -1;
    }
    if (err == 0 && data && len > 0) {
        if (len > MN_PAD_HID_MAX_REP) {
            return -1;
        }
        int hdr = snprintf(buf, cap, "hid-get-reply %u 0 ", req_id);
        if (hdr < 0 || (size_t)hdr >= cap) {
            return -1;
        }
        size_t pos = (size_t)hdr;
        size_t hex_len = write_hex(data, len, buf + pos, cap - pos);
        if (hex_len == 0 || pos + hex_len + 2u > cap) {
            return -1;
        }
        pos += hex_len;
        buf[pos++] = '\n';
        buf[pos] = '\0';
        return (int)pos;
    }
    int n = snprintf(buf, cap, "hid-get-reply %u %u\n", req_id, (unsigned)err);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

int mn_pad_encode_hid_set_reply(uint32_t req_id, uint16_t err, char *buf, size_t cap) {
    if (!buf || cap == 0) {
        return -1;
    }
    int n = snprintf(buf, cap, "hid-set-reply %u %u\n", req_id, (unsigned)err);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    return n;
}

/* --- Per-slot TX queue --- */

void mn_pad_txq_init(mn_pad_txq_t *q) {
    memset(q, 0, sizeof(*q));
}

void mn_pad_txq_clear(mn_pad_txq_t *q) {
    if (!q) {
        return;
    }
    q->pending_len = 0;
    q->pending_off = 0;
    for (size_t i = 0; i < q->fifo_len; i++) {
        uint8_t id = q->fifo[i];
        free(q->latest[id].line);
        q->latest[id].line = NULL;
        q->latest[id].len = 0;
        q->latest[id].occupied = false;
    }
    q->fifo_len = 0;
}

void mn_pad_txq_destroy(mn_pad_txq_t *q) {
    if (!q) {
        return;
    }
    mn_pad_txq_clear(q);
    free(q->pending);
    q->pending = NULL;
    q->pending_cap = 0;
}

size_t mn_pad_txq_bytes(const mn_pad_txq_t *q) {
    if (!q) {
        return 0;
    }
    size_t total = q->pending_len - q->pending_off;
    for (size_t i = 0; i < q->fifo_len; i++) {
        uint8_t id = q->fifo[i];
        if (q->latest[id].occupied) {
            total += q->latest[id].len;
        }
    }
    return total;
}

static int txq_append_pending(mn_pad_txq_t *q, const char *data, size_t len) {
    if (len == 0) {
        return 0;
    }
    size_t avail = q->pending_len - q->pending_off;
    if (avail + len > MN_PAD_TX_CAP) {
        return -ENOSPC;
    }
    if (q->pending_off > 0 && q->pending_len + len > q->pending_cap) {
        if (avail > 0) {
            memmove(q->pending, q->pending + q->pending_off, avail);
        }
        q->pending_off = 0;
        q->pending_len = avail;
    }
    if (q->pending_len + len > q->pending_cap) {
        size_t new_cap = q->pending_cap ? q->pending_cap : 4096u;
        while (new_cap < q->pending_len + len) {
            new_cap *= 2u;
        }
        if (new_cap > MN_PAD_TX_CAP) {
            new_cap = MN_PAD_TX_CAP;
        }
        uint8_t *nb = (uint8_t *)realloc(q->pending, new_cap);
        if (!nb) {
            return -ENOMEM;
        }
        q->pending = nb;
        q->pending_cap = new_cap;
    }
    memcpy(q->pending + q->pending_len, data, len);
    q->pending_len += len;
    return 0;
}

static void txq_evict_oldest_latest(mn_pad_txq_t *q) {
    if (q->fifo_len == 0) {
        return;
    }
    uint8_t id = q->fifo[0];
    free(q->latest[id].line);
    q->latest[id].line = NULL;
    q->latest[id].len = 0;
    q->latest[id].occupied = false;
    if (q->fifo_len > 1) {
        memmove(q->fifo, q->fifo + 1, q->fifo_len - 1);
    }
    q->fifo_len--;
}

void mn_pad_txq_promote_all(mn_pad_txq_t *q) {
    if (!q || q->fifo_len == 0) {
        return;
    }
    for (size_t i = 0; i < q->fifo_len; i++) {
        uint8_t id = q->fifo[i];
        if (q->latest[id].occupied) {
            (void)txq_append_pending(q, q->latest[id].line, q->latest[id].len);
            free(q->latest[id].line);
            q->latest[id].line = NULL;
            q->latest[id].len = 0;
            q->latest[id].occupied = false;
        }
    }
    q->fifo_len = 0;
}

int mn_pad_txq_push_control(mn_pad_txq_t *q, const char *line, size_t len) {
    if (!q || !line || len == 0 || len > MN_PAD_TX_CAP) {
        return -EINVAL;
    }
    while (q->fifo_len > 0 && mn_pad_txq_bytes(q) + len > MN_PAD_TX_CAP) {
        txq_evict_oldest_latest(q);
    }
    if (mn_pad_txq_bytes(q) + len > MN_PAD_TX_CAP) {
        return -ENOSPC;
    }
    mn_pad_txq_promote_all(q);
    return txq_append_pending(q, line, len);
}

int mn_pad_txq_push_hid_input(mn_pad_txq_t *q, uint8_t report_id, const char *line, size_t len) {
    if (!q || !line || len == 0 || len > MN_PAD_TX_CAP) {
        return -EINVAL;
    }
    if ((q->pending_len - q->pending_off) == 0 && q->fifo_len == 0) {
        return txq_append_pending(q, line, len);
    }
    mn_pad_hid_latest_t *slot = &q->latest[report_id];
    size_t cur_total = mn_pad_txq_bytes(q);
    if (slot->occupied) {
        if (cur_total - slot->len + len > MN_PAD_TX_CAP) {
            return -ENOSPC;
        }
        char *copy = (char *)malloc(len + 1u);
        if (!copy) {
            return -ENOMEM;
        }
        memcpy(copy, line, len);
        copy[len] = '\0';
        free(slot->line);
        slot->line = copy;
        slot->len = len;
        return 0;
    }
    if (cur_total + len > MN_PAD_TX_CAP) {
        return -ENOSPC;
    }
    char *copy = (char *)malloc(len + 1u);
    if (!copy) {
        return -ENOMEM;
    }
    memcpy(copy, line, len);
    copy[len] = '\0';
    slot->line = copy;
    slot->len = len;
    slot->occupied = true;
    q->fifo[q->fifo_len++] = report_id;
    return 0;
}

bool mn_pad_txq_has_pending(const mn_pad_txq_t *q) {
    if (!q) {
        return false;
    }
    return (q->pending_len > q->pending_off) || (q->fifo_len > 0);
}

ssize_t mn_pad_txq_flush_fd(mn_pad_txq_t *q, int fd, int *out_errno) {
    if (out_errno) {
        *out_errno = 0;
    }
    if (!q || fd < 0) {
        if (out_errno) *out_errno = EINVAL;
        return -1;
    }
    ssize_t total_written = 0;
    for (;;) {
        if (q->pending_off == q->pending_len) {
            q->pending_off = 0;
            q->pending_len = 0;
            if (q->fifo_len == 0) {
                break;
            }
            mn_pad_txq_promote_all(q);
        }
        size_t rem = q->pending_len - q->pending_off;
        ssize_t n = write(fd, q->pending + q->pending_off, rem);
        if (n > 0) {
            q->pending_off += (size_t)n;
            total_written += n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (out_errno) *out_errno = EAGAIN;
            break;
        }
        if (out_errno) *out_errno = (n == 0) ? EPIPE : errno;
        return -1;
    }
    return total_written;
}

/* --- Runtime slot TX / dispatch helpers --- */

static void slot_flush_or_arm(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= g_pad.nslots) {
        return;
    }
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (s->wr_fd < 0 || s->wr_src == nil) {
        return;
    }
    int err = 0;
    ssize_t r = mn_pad_txq_flush_fd(&s->txq, s->wr_fd, &err);
    if (r < 0 && err != EAGAIN) {
        mn_pad_txq_clear(&s->txq);
        if (s->write_resumed) {
            s->write_resumed = false;
            dispatch_suspend(s->wr_src);
        }
        return;
    }
    bool need_write = mn_pad_txq_has_pending(&s->txq);
    if (need_write && !s->write_resumed) {
        s->write_resumed = true;
        dispatch_resume(s->wr_src);
    } else if (!need_write && s->write_resumed) {
        s->write_resumed = false;
        dispatch_suspend(s->wr_src);
    }
}

static void slot_send_control(int slot_idx, const char *line, size_t len) {
    if (slot_idx < 0 || slot_idx >= g_pad.nslots || !line || len == 0) {
        return;
    }
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (mn_pad_txq_push_control(&s->txq, line, len) == 0) {
        slot_flush_or_arm(slot_idx);
    }
}

static void slot_send_remove(int slot_idx) {
    char buf[16];
    int n = mn_pad_encode_remove(buf, sizeof(buf));
    if (n > 0) {
        slot_send_control(slot_idx, buf, (size_t)n);
    }
}

/* --- GCController profile & input helpers --- */

static mn_pad_profile_t detect_profile(GCController *c) {
    NSString *pc = [c.productCategory lowercaseString];
    NSString *vn = [c.vendorName lowercaseString];
    if ((pc && [pc containsString:@"dualsense"]) || (vn && [vn containsString:@"dualsense"])) {
        return MN_PAD_PROFILE_DUALSENSE;
    }
    if ((pc && [pc containsString:@"dualshock"]) || (vn && [vn containsString:@"dualshock"])) {
        return MN_PAD_PROFILE_DUALSHOCK4;
    }
    return MN_PAD_PROFILE_XBOX360;
}

static void read_gc_snapshot(GCExtendedGamepad *gp, mn_pad_input_t *out) {
    memset(out, 0, sizeof(*out));
    out->lx = gp.leftThumbstick.xAxis.value;
    out->ly = gp.leftThumbstick.yAxis.value;
    out->rx = gp.rightThumbstick.xAxis.value;
    out->ry = gp.rightThumbstick.yAxis.value;
    out->lt = gp.leftTrigger.value;
    out->rt = gp.rightTrigger.value;
    out->dpad_x = gp.dpad.xAxis.value;
    out->dpad_y = gp.dpad.yAxis.value;
    out->a = gp.buttonA.pressed ? 1 : 0;
    out->b = gp.buttonB.pressed ? 1 : 0;
    out->x = gp.buttonX.pressed ? 1 : 0;
    out->y = gp.buttonY.pressed ? 1 : 0;
    out->lb = gp.leftShoulder.pressed ? 1 : 0;
    out->rb = gp.rightShoulder.pressed ? 1 : 0;
    out->lt_btn = gp.leftTrigger.pressed ? 1 : 0;
    out->rt_btn = gp.rightTrigger.pressed ? 1 : 0;
    out->options = (gp.buttonOptions != nil && gp.buttonOptions.pressed) ? 1 : 0;
    out->menu = gp.buttonMenu.pressed ? 1 : 0;
    out->home = (gp.buttonHome != nil && gp.buttonHome.pressed) ? 1 : 0;
    out->l3 = (gp.leftThumbstickButton != nil && gp.leftThumbstickButton.pressed) ? 1 : 0;
    out->r3 = (gp.rightThumbstickButton != nil && gp.rightThumbstickButton.pressed) ? 1 : 0;
}

static void slot_emit_gc_update(int slot_idx) {
    if (!g_pad.initialized || slot_idx < 0 || slot_idx >= g_pad.nslots) {
        return;
    }
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (!s->hello_seen || s->hid_dev != NULL || !s->uinput_active || s->controller == nil) {
        return;
    }
    GCExtendedGamepad *gp = s->controller.extendedGamepad;
    if (gp == nil) {
        return;
    }
    mn_pad_input_t in;
    read_gc_snapshot(gp, &in);
    mn_pad_ev_state_t next;
    mn_pad_map_input(s->profile, &in, MN_PAD_DEADZONE, &next);

    char line[512];
    int n = mn_pad_encode_ev_diff(s->profile, &s->prev_ev, &next, line, sizeof(line));
    if (n > 0) {
        s->prev_ev = next;
        slot_send_control(slot_idx, line, (size_t)n);
    }
}

static void slot_activate_uinput(int slot_idx) {
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (!s->hello_seen || s->controller == nil || s->hid_dev != NULL) {
        return;
    }
    char line[512];
    int n = mn_pad_encode_create(s->profile, line, sizeof(line));
    if (n > 0) {
        memset(&s->prev_ev, 0, sizeof(s->prev_ev));
        s->uinput_active = true;
        slot_send_control(slot_idx, line, (size_t)n);
        slot_emit_gc_update(slot_idx);
    }
}

static void on_hid_input_for_slot(int slot_idx, const uint8_t *report, size_t len, void *ctx) {
    (void)ctx;
    if (!g_pad.initialized || slot_idx < 0 || slot_idx >= g_pad.nslots) {
        return;
    }
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (!s->hello_seen || !s->caps_hid || s->hid_dev == NULL) {
        return;
    }
    if (!report || len == 0 || len > MN_PAD_HID_MAX_REP) {
        return;
    }
    char line[16 + MN_PAD_HID_MAX_REP * 2u + 4u];
    int n = mn_pad_encode_hid_input(report, len, line, sizeof(line));
    if (n <= 0) {
        return;
    }
    const mn_hid_dev_info_t *info = mn_hid_device_info(s->hid_dev);
    uint8_t report_id = (info && info->uses_report_ids) ? report[0] : 0;
    if (mn_pad_txq_push_hid_input(&s->txq, report_id, line, (size_t)n) == 0) {
        slot_flush_or_arm(slot_idx);
    }
}

static void slot_activate_hid(int slot_idx, mn_hid_avail_t *avail) {
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (!s->hello_seen || !s->caps_hid) {
        return;
    }
    char line[128 + MN_PAD_HID_MAX_DESC * 2u + 256u];
    int n = mn_pad_encode_hid_create(avail->info.bus, avail->info.vid, avail->info.pid,
                                     avail->info.ver, avail->info.country,
                                     avail->info.desc, avail->info.desc_len,
                                     avail->info.name, line, sizeof(line));
    if (n <= 0) {
        return;
    }
    if (s->hid_dev != NULL && s->hid_dev != avail->dev) {
        mn_hid_device_set_active(s->hid_dev, -1, NULL, NULL);
    }
    mn_rumble_stop(slot_idx);
    s->uinput_active = false;
    s->hid_dev = avail->dev;
    avail->assigned_slot = slot_idx;
    mn_hid_device_set_active(avail->dev, slot_idx, on_hid_input_for_slot, NULL);
    slot_send_control(slot_idx, line, (size_t)n);
}

static void assign_available_hid_to_slots(void) {
    if (g_pad.selftest) {
        return;
    }
    for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
        mn_hid_avail_t *a = &g_pad.avail_hid[i];
        if (!a->dev || a->assigned_slot >= 0) {
            continue;
        }
        int target = -1;
        /* Prefer pairing DualSense HID with a slot holding a DualSense GCController. */
        if (a->info.kind == MN_HID_KIND_DUALSENSE) {
            for (int s = 0; s < g_pad.nslots; s++) {
                if (g_pad.slots[s].hello_seen && g_pad.slots[s].caps_hid &&
                    g_pad.slots[s].hid_dev == NULL &&
                    g_pad.slots[s].controller != nil &&
                    g_pad.slots[s].profile == MN_PAD_PROFILE_DUALSENSE) {
                    target = s;
                    break;
                }
            }
        }
        if (target < 0) {
            for (int s = 0; s < g_pad.nslots; s++) {
                if (g_pad.slots[s].hello_seen && g_pad.slots[s].caps_hid &&
                    g_pad.slots[s].hid_dev == NULL && g_pad.slots[s].controller == nil) {
                    target = s;
                    break;
                }
            }
        }
        if (target < 0 && a->info.kind == MN_HID_KIND_DUALSENSE) {
            for (int s = 0; s < g_pad.nslots; s++) {
                if (g_pad.slots[s].hello_seen && g_pad.slots[s].caps_hid &&
                    g_pad.slots[s].hid_dev == NULL) {
                    target = s;
                    break;
                }
            }
        }
        if (target >= 0) {
            slot_activate_hid(target, a);
        }
    }
}

static void on_hid_ready_cb(struct mn_hid_device *dev, const mn_hid_dev_info_t *info, void *ctx) {
    (void)ctx;
    if (!g_pad.initialized || !dev || !info) {
        return;
    }
    for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
        if (g_pad.avail_hid[i].dev == dev) {
            assign_available_hid_to_slots();
            return;
        }
    }
    for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
        if (!g_pad.avail_hid[i].dev) {
            g_pad.avail_hid[i].dev = dev;
            g_pad.avail_hid[i].info = *info;
            g_pad.avail_hid[i].assigned_slot = -1;
            break;
        }
    }
    assign_available_hid_to_slots();
}

static void on_hid_gone_cb(struct mn_hid_device *dev, const mn_hid_dev_info_t *info, void *ctx) {
    (void)info;
    (void)ctx;
    if (!dev) {
        return;
    }
    mn_hid_device_set_active(dev, -1, NULL, NULL);
    int was_slot = -1;
    for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
        if (g_pad.avail_hid[i].dev == dev) {
            was_slot = g_pad.avail_hid[i].assigned_slot;
            memset(&g_pad.avail_hid[i], 0, sizeof(g_pad.avail_hid[i]));
            g_pad.avail_hid[i].assigned_slot = -1;
            break;
        }
    }
    if (was_slot >= 0 && was_slot < g_pad.nslots) {
        mn_pad_slot_t *s = &g_pad.slots[was_slot];
        if (s->hid_dev == dev) {
            s->hid_dev = NULL;
            if (s->controller != nil && s->hello_seen) {
                slot_activate_uinput(was_slot);
            } else if (s->hello_seen) {
                slot_send_remove(was_slot);
            }
        }
    }
}

static void on_hid_get_reply_cb(int slot_idx, uint32_t req_id, uint16_t err,
                                const uint8_t *data, size_t len, void *ctx) {
    (void)ctx;
    if (!g_pad.initialized || slot_idx < 0 || slot_idx >= g_pad.nslots) {
        return;
    }
    char line[64 + MN_PAD_HID_MAX_REP * 2u];
    int n = mn_pad_encode_hid_get_reply(req_id, err, data, len, line, sizeof(line));
    if (n > 0) {
        slot_send_control(slot_idx, line, (size_t)n);
    }
}

static void on_hid_set_reply_cb(int slot_idx, uint32_t req_id, uint16_t err, void *ctx) {
    (void)ctx;
    if (!g_pad.initialized || slot_idx < 0 || slot_idx >= g_pad.nslots) {
        return;
    }
    char line[64];
    int n = mn_pad_encode_hid_set_reply(req_id, err, line, sizeof(line));
    if (n > 0) {
        slot_send_control(slot_idx, line, (size_t)n);
    }
}

static void attach_gccontroller(GCController *c) {
    if (!g_pad.initialized || g_pad.selftest || c == nil || c.extendedGamepad == nil) {
        return;
    }
    for (int i = 0; i < g_pad.nslots; i++) {
        if (g_pad.slots[i].controller == c) {
            return;
        }
    }
    int slot_idx = -1;
    for (int i = 0; i < g_pad.nslots; i++) {
        if (g_pad.slots[i].controller == nil && g_pad.slots[i].hid_dev == NULL) {
            slot_idx = i;
            break;
        }
    }
    if (slot_idx < 0) {
        for (int i = 0; i < g_pad.nslots; i++) {
            if (g_pad.slots[i].controller == nil) {
                slot_idx = i;
                break;
            }
        }
    }
    if (slot_idx < 0) {
        return;
    }

    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    s->controller = MN_RETAIN(c);
    s->profile = detect_profile(c);
    c.handlerQueue = dispatch_get_main_queue();
    c.playerIndex = (GCControllerPlayerIndex)slot_idx;
    mn_rumble_attach(slot_idx, c);

    c.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *gamepad, GCControllerElement *element) {
        (void)gamepad;
        (void)element;
        slot_emit_gc_update(slot_idx);
    };

    if (s->hello_seen && s->hid_dev == NULL) {
        slot_activate_uinput(slot_idx);
    }
    if (s->hello_seen && s->caps_hid) {
        assign_available_hid_to_slots();
    }
    mn_hid_on_gccontroller_changed();
}

static void detach_gccontroller(GCController *c) {
    if (c == nil) {
        return;
    }
    for (int i = 0; i < g_pad.nslots; i++) {
        mn_pad_slot_t *s = &g_pad.slots[i];
        if (s->controller != c) {
            continue;
        }
        c.extendedGamepad.valueChangedHandler = nil;
        mn_rumble_detach(i);
        MN_RELEASE(s->controller);
        if (s->uinput_active) {
            s->uinput_active = false;
            if (s->hello_seen && s->hid_dev == NULL) {
                slot_send_remove(i);
            }
        }
        break;
    }
    mn_hid_on_gccontroller_changed();
}

/* --- Selftest mode (MN_PAD_SELFTEST=1) --- */

static void stop_selftest_timer(mn_pad_slot_t *s) {
    if (s->st_timer != nil) {
        dispatch_source_cancel(s->st_timer);
        MN_DISPATCH_RELEASE(s->st_timer);
    }
}

static void start_selftest_timer(int slot_idx) {
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    stop_selftest_timer(s);
    dispatch_source_t t = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (t == nil) {
        return;
    }
    dispatch_source_set_timer(t,
                              dispatch_time(DISPATCH_TIME_NOW, 150u * NSEC_PER_MSEC),
                              200u * NSEC_PER_MSEC,
                              20u * NSEC_PER_MSEC);
    uint64_t gen = g_pad.gen;
    dispatch_source_set_event_handler(t, ^{
        if (!g_pad.initialized || !g_pad.selftest || g_pad.gen != gen) {
            return;
        }
        if (s->st_phase == ST_PHASE_UINPUT_WAIT_RUMBLE) {
            s->st_pulse_high = !s->st_pulse_high;
            const char *ev = s->st_pulse_high ? "ev 1:304:1 3:0:16384\n" : "ev 1:304:0 3:0:0\n";
            slot_send_control(slot_idx, ev, strlen(ev));
        } else if (s->st_phase == ST_PHASE_UHID_ACTIVE) {
            if (!s->st_got_hid_output || !s->st_got_hid_get || !s->st_got_hid_set) {
                const uint8_t rep[4] = {0x01, 0x05, 0x40, 0x80};
                char line[64];
                int n = mn_pad_encode_hid_input(rep, sizeof(rep), line, sizeof(line));
                if (n > 0) {
                    slot_send_control(slot_idx, line, (size_t)n);
                }
            } else {
                stop_selftest_timer(s);
            }
        } else {
            stop_selftest_timer(s);
        }
    });
    s->st_timer = t;
    dispatch_resume(t);
}

/* --- Guest -> Host protocol parser --- */

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *hex, uint8_t *out, size_t max_out, size_t *out_len) {
    size_t slen = strlen(hex);
    if (slen == 0 || (slen % 2u) != 0 || slen / 2u > max_out) {
        return -1;
    }
    for (size_t i = 0; i < slen / 2u; i++) {
        int hi = hex_nibble(hex[i * 2u]);
        int lo = hex_nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = slen / 2u;
    return 0;
}

static bool parse_report_type(const char *s, mn_hid_report_type_t *out) {
    if (strcmp(s, "feature") == 0) {
        *out = MN_HID_REPORT_FEATURE;
        return true;
    }
    if (strcmp(s, "output") == 0) {
        *out = MN_HID_REPORT_OUTPUT;
        return true;
    }
    if (strcmp(s, "input") == 0) {
        *out = MN_HID_REPORT_INPUT;
        return true;
    }
    return false;
}

static bool parse_u32_strict(const char *s, uint32_t *out) {
    if (!s || s[0] < '0' || s[0] > '9') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static void slot_handle_line(int slot_idx, char *line) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') {
        line[--len] = '\0';
    }
    if (len == 0) {
        return;
    }

    mn_pad_slot_t *s = &g_pad.slots[slot_idx];

    if (strcmp(line, "hello") == 0) {
        s->hello_seen = true;
        s->caps_hid = false;
        s->uinput_active = false;
        if (s->hid_dev) {
            mn_hid_device_set_active(s->hid_dev, -1, NULL, NULL);
            for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
                if (g_pad.avail_hid[i].dev == s->hid_dev) {
                    g_pad.avail_hid[i].assigned_slot = -1;
                }
            }
            s->hid_dev = NULL;
        }
        memset(&s->prev_ev, 0, sizeof(s->prev_ev));

        if (g_pad.selftest && slot_idx == 0) {
            s->st_phase = ST_PHASE_UINPUT_WAIT_RUMBLE;
            s->st_seen_nonzero_rumble = false;
            s->st_pulse_high = false;
            s->st_got_hid_output = false;
            s->st_got_hid_get = false;
            s->st_got_hid_set = false;
            char cbuf[512];
            int n = mn_pad_encode_create(MN_PAD_PROFILE_XBOX360, cbuf, sizeof(cbuf));
            if (n > 0) {
                slot_send_control(slot_idx, cbuf, (size_t)n);
            }
            const char *ev0 = "ev 1:304:1 3:0:16384\n";
            s->st_pulse_high = true;
            slot_send_control(slot_idx, ev0, strlen(ev0));
            start_selftest_timer(slot_idx);
            return;
        }
        if (s->controller != nil) {
            slot_activate_uinput(slot_idx);
        }
        return;
    }

    if (strcmp(line, "caps hid") == 0) {
        s->caps_hid = true;
        if (!g_pad.selftest) {
            assign_available_hid_to_slots();
        }
        return;
    }

    if (strncmp(line, "rumble ", 7) == 0) {
        unsigned long strong = 0, weak = 0;
        char *end1 = NULL, *end2 = NULL;
        strong = strtoul(line + 7, &end1, 10);
        if (end1 == line + 7 || *end1 != ' ') {
            return;
        }
        weak = strtoul(end1 + 1, &end2, 10);
        if (end2 == end1 + 1 || *end2 != '\0') {
            return;
        }
        uint16_t s16 = strong > 65535ul ? 65535u : (uint16_t)strong;
        uint16_t w16 = weak > 65535ul ? 65535u : (uint16_t)weak;

        if (g_pad.selftest && slot_idx == 0) {
            fprintf(stderr, "mn-pad-selftest: rumble %u %u\n", (unsigned)s16, (unsigned)w16);
            fflush(stderr);
            if (s16 > 0 || w16 > 0) {
                s->st_seen_nonzero_rumble = true;
                s->st_phase = ST_PHASE_UINPUT_RUMBLING;
            } else if (s->st_seen_nonzero_rumble && s->st_phase == ST_PHASE_UINPUT_RUMBLING) {
                s->st_phase = ST_PHASE_UHID_ACTIVE;
                char hbuf[1024];
                int hn = mn_pad_encode_hid_create(0x0003u, 0x045eu, 0x028eu, 0x0114u, 0x00u,
                                                  k_selftest_hid_desc, sizeof(k_selftest_hid_desc),
                                                  "MacNeutron Selftest HID", hbuf, sizeof(hbuf));
                if (hn > 0) {
                    slot_send_control(slot_idx, hbuf, (size_t)hn);
                }
                const uint8_t rep[4] = {0x01, 0x05, 0x40, 0x80};
                char ibuf[64];
                int in = mn_pad_encode_hid_input(rep, sizeof(rep), ibuf, sizeof(ibuf));
                if (in > 0) {
                    slot_send_control(slot_idx, ibuf, (size_t)in);
                }
                start_selftest_timer(slot_idx);
            }
            return;
        }
        if (!g_pad.paused) {
            mn_rumble_set(slot_idx, s16, w16);
        }
        return;
    }

    if (strncmp(line, "hid-output ", 11) == 0) {
        char *sp = strchr(line + 11, ' ');
        if (!sp) return;
        *sp = '\0';
        const char *type_str = line + 11;
        const char *hex_str = sp + 1;
        mn_hid_report_type_t rtype;
        if (!parse_report_type(type_str, &rtype)) return;
        uint8_t bytes[MN_PAD_HID_MAX_REP];
        size_t blen = 0;
        if (parse_hex(hex_str, bytes, sizeof(bytes), &blen) != 0) return;

        if (g_pad.selftest && slot_idx == 0) {
            fprintf(stderr, "mn-pad-selftest: hid-output %s %s\n", type_str, hex_str);
            fflush(stderr);
            s->st_got_hid_output = true;
            return;
        }
        if (s->hid_dev) {
            mn_hid_output_report(s->hid_dev, rtype, bytes, blen);
        }
        return;
    }

    if (strncmp(line, "hid-get ", 8) == 0) {
        char *p = line + 8;
        char *sp1 = strchr(p, ' ');
        if (!sp1) return;
        *sp1 = '\0';
        char *type_str = sp1 + 1;
        char *sp2 = strchr(type_str, ' ');
        if (!sp2) return;
        *sp2 = '\0';
        const char *rnum_str = sp2 + 1;

        uint32_t req_id = 0;
        if (!parse_u32_strict(p, &req_id)) {
            return;
        }
        mn_hid_report_type_t rtype;
        if (!parse_report_type(type_str, &rtype)) {
            on_hid_get_reply_cb(slot_idx, req_id, EINVAL, NULL, 0, NULL);
            return;
        }
        uint32_t rnum_u32 = 0;
        if (!parse_u32_strict(rnum_str, &rnum_u32) || rnum_u32 > 255u) {
            on_hid_get_reply_cb(slot_idx, req_id, EINVAL, NULL, 0, NULL);
            return;
        }
        uint8_t rnum = (uint8_t)rnum_u32;

        if (g_pad.selftest && slot_idx == 0) {
            fprintf(stderr, "mn-pad-selftest: hid-get %u %s %u\n", req_id, type_str, (unsigned)rnum);
            fflush(stderr);
            s->st_got_hid_get = true;
            const uint8_t reply_data[5] = {rnum, 0xca, 0xfe, 0x12, 0x34};
            on_hid_get_reply_cb(slot_idx, req_id, 0, reply_data, sizeof(reply_data), NULL);
            return;
        }
        if (s->hid_dev) {
            mn_hid_get_report(s->hid_dev, slot_idx, req_id, rtype, rnum);
        } else {
            on_hid_get_reply_cb(slot_idx, req_id, ENODEV, NULL, 0, NULL);
        }
        return;
    }

    if (strncmp(line, "hid-set ", 8) == 0) {
        char *p = line + 8;
        char *sp1 = strchr(p, ' ');
        if (!sp1) return;
        *sp1 = '\0';
        char *type_str = sp1 + 1;
        char *sp2 = strchr(type_str, ' ');
        if (!sp2) return;
        *sp2 = '\0';
        const char *hex_str = sp2 + 1;

        uint32_t req_id = 0;
        if (!parse_u32_strict(p, &req_id)) {
            return;
        }
        mn_hid_report_type_t rtype;
        if (!parse_report_type(type_str, &rtype)) {
            on_hid_set_reply_cb(slot_idx, req_id, EINVAL, NULL);
            return;
        }
        uint8_t bytes[MN_PAD_HID_MAX_REP];
        size_t blen = 0;
        if (parse_hex(hex_str, bytes, sizeof(bytes), &blen) != 0) {
            on_hid_set_reply_cb(slot_idx, req_id, EINVAL, NULL);
            return;
        }

        if (g_pad.selftest && slot_idx == 0) {
            fprintf(stderr, "mn-pad-selftest: hid-set %u %s %s\n", req_id, type_str, hex_str);
            fflush(stderr);
            s->st_got_hid_set = true;
            on_hid_set_reply_cb(slot_idx, req_id, 0, NULL);
            return;
        }
        if (s->hid_dev) {
            mn_hid_set_report(s->hid_dev, slot_idx, req_id, rtype, bytes, blen);
        } else {
            on_hid_set_reply_cb(slot_idx, req_id, ENODEV, NULL);
        }
        return;
    }
}

static void slot_on_readable(int slot_idx) {
    mn_pad_slot_t *s = &g_pad.slots[slot_idx];
    if (s->rd_fd < 0) {
        return;
    }
    for (;;) {
        if (s->rx_len >= sizeof(s->rx_buf) - 1u) {
            s->rx_len = 0;
            s->rx_discard = true;
        }
        ssize_t n = read(s->rd_fd, s->rx_buf + s->rx_len, sizeof(s->rx_buf) - 1u - s->rx_len);
        if (n > 0) {
            s->rx_len += (size_t)n;
            char *start = s->rx_buf;
            char *end = s->rx_buf + s->rx_len;
            if (s->rx_discard) {
                char *nl = memchr(start, '\n', (size_t)(end - start));
                if (!nl) {
                    s->rx_len = 0;
                    continue;
                }
                start = nl + 1;
                s->rx_discard = false;
            }
            char *nl = NULL;
            while ((nl = memchr(start, '\n', (size_t)(end - start))) != NULL) {
                *nl = '\0';
                slot_handle_line(slot_idx, start);
                start = nl + 1;
            }
            size_t rem = (size_t)(end - start);
            if (rem >= sizeof(s->rx_buf) - 1u) {
                s->rx_len = 0;
                s->rx_discard = true;
            } else {
                if (rem > 0 && start != s->rx_buf) {
                    memmove(s->rx_buf, start, rem);
                }
                s->rx_len = rem;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (s->rd_src != nil) {
            s->rd_fd = -1;
            dispatch_source_cancel(s->rd_src);
        }
        break;
    }
}

static int setup_fd_flags(int fd, bool set_nosigpipe) {
    int fd_flags = fcntl(fd, F_GETFD, 0);
    if (fd_flags >= 0) {
        (void)fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
        return -1;
    }
    if (set_nosigpipe) {
        (void)fcntl(fd, F_SETNOSIGPIPE, 1);
    }
    return 0;
}

int mn_gamepad_init(const int *wr_fds, const int *rd_fds, int nslots) {
    if (!wr_fds || !rd_fds || nslots < 1 || nslots > MN_GAMEPAD_MAX_SLOTS) {
        return -1;
    }
    if (g_pad.initialized) {
        mn_gamepad_shutdown();
    }
    uint64_t next_gen = g_pad.gen + 1u;
    g_pad.initialized = false;
    g_pad.paused = false;
    g_pad.selftest = false;
    g_pad.nslots = 0;
    g_pad.connect_obs = nil;
    g_pad.disconnect_obs = nil;
    memset(g_pad.avail_hid, 0, sizeof(g_pad.avail_hid));
    g_pad.gen = next_gen;
    for (int i = 0; i < MN_MAX_AVAIL_HID; i++) {
        g_pad.avail_hid[i].assigned_slot = -1;
    }
    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        g_pad.slots[i].wr_fd = -1;
        g_pad.slots[i].rd_fd = -1;
        g_pad.slots[i].rx_len = 0;
        g_pad.slots[i].rx_discard = false;
        mn_pad_txq_init(&g_pad.slots[i].txq);
    }

    const char *st_env = getenv("MN_PAD_SELFTEST");
    g_pad.selftest = (st_env && strcmp(st_env, "0") != 0 && st_env[0] != '\0');
    g_pad.nslots = nslots;

    for (int i = 0; i < nslots; i++) {
        int wfd = wr_fds[i];
        int rfd = rd_fds[i];
        if (wfd < 0 || rfd < 0 ||
            setup_fd_flags(wfd, true) != 0 ||
            setup_fd_flags(rfd, false) != 0) {
            for (int j = i; j < nslots; j++) {
                if (wr_fds[j] >= 0) close(wr_fds[j]);
                if (rd_fds[j] >= 0) close(rd_fds[j]);
            }
            mn_gamepad_shutdown();
            return -1;
        }

        dispatch_source_t rsrc = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_READ, (uintptr_t)rfd, 0, dispatch_get_main_queue());
        dispatch_source_t wsrc = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_WRITE, (uintptr_t)wfd, 0, dispatch_get_main_queue());
        if (rsrc == nil || wsrc == nil) {
            if (rsrc != nil) {
                dispatch_source_cancel(rsrc);
                dispatch_resume(rsrc);
                MN_DISPATCH_RELEASE(rsrc);
            }
            if (wsrc != nil) {
                dispatch_source_cancel(wsrc);
                dispatch_resume(wsrc);
                MN_DISPATCH_RELEASE(wsrc);
            }
            for (int j = i; j < nslots; j++) {
                if (wr_fds[j] >= 0) close(wr_fds[j]);
                if (rd_fds[j] >= 0) close(rd_fds[j]);
            }
            mn_gamepad_shutdown();
            return -1;
        }
        mn_pad_slot_t *s = &g_pad.slots[i];
        s->wr_fd = wfd;
        s->rd_fd = rfd;
        uint64_t gen = g_pad.gen;
        int captured_rfd = rfd;
        int captured_wfd = wfd;
        dispatch_source_set_event_handler(rsrc, ^{
            if (g_pad.initialized && g_pad.gen == gen) {
                slot_on_readable(i);
            }
        });
        dispatch_source_set_cancel_handler(rsrc, ^{
            close(captured_rfd);
        });
        dispatch_source_set_event_handler(wsrc, ^{
            if (g_pad.initialized && g_pad.gen == gen) {
                slot_flush_or_arm(i);
            }
        });
        dispatch_source_set_cancel_handler(wsrc, ^{
            close(captured_wfd);
        });
        s->rd_src = rsrc;
        s->wr_src = wsrc;
        s->write_resumed = false;
        dispatch_resume(rsrc);
    }

    g_pad.initialized = true;

    if (!g_pad.selftest) {
        @autoreleasepool {
            NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
            g_pad.connect_obs = MN_RETAIN([nc addObserverForName:GCControllerDidConnectNotification
                                                          object:nil
                                                           queue:[NSOperationQueue mainQueue]
                                                      usingBlock:^(NSNotification *note) {
                GCController *c = (GCController *)note.object;
                attach_gccontroller(c);
            }]);
            g_pad.disconnect_obs = MN_RETAIN([nc addObserverForName:GCControllerDidDisconnectNotification
                                                             object:nil
                                                              queue:[NSOperationQueue mainQueue]
                                                         usingBlock:^(NSNotification *note) {
                GCController *c = (GCController *)note.object;
                detach_gccontroller(c);
            }]);
            for (GCController *c in [GCController controllers]) {
                attach_gccontroller(c);
            }
        }
        const mn_hid_callbacks_t hid_cbs = {
            .on_ready = on_hid_ready_cb,
            .on_gone = on_hid_gone_cb,
            .on_get_reply = on_hid_get_reply_cb,
            .on_set_reply = on_hid_set_reply_cb,
        };
        (void)mn_hid_init(&hid_cbs, NULL);
    }

    return 0;
}

static void shutdown_on_main(void) {
    if (!g_pad.initialized && g_pad.nslots == 0) {
        return;
    }
    g_pad.initialized = false;
    mn_rumble_stop_all();
    mn_hid_shutdown();

    @autoreleasepool {
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        if (g_pad.connect_obs != nil) {
            [nc removeObserver:g_pad.connect_obs];
            MN_RELEASE(g_pad.connect_obs);
        }
        if (g_pad.disconnect_obs != nil) {
            [nc removeObserver:g_pad.disconnect_obs];
            MN_RELEASE(g_pad.disconnect_obs);
        }
    }

    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        mn_pad_slot_t *s = &g_pad.slots[i];
        stop_selftest_timer(s);
        if (s->controller != nil) {
            s->controller.extendedGamepad.valueChangedHandler = nil;
            mn_rumble_detach(i);
            MN_RELEASE(s->controller);
        }
        s->hid_dev = NULL;
        if (s->rd_src != nil) {
            s->rd_fd = -1;
            dispatch_source_cancel(s->rd_src);
            MN_DISPATCH_RELEASE(s->rd_src);
        } else if (s->rd_fd >= 0) {
            close(s->rd_fd);
            s->rd_fd = -1;
        }
        if (s->wr_src != nil) {
            s->wr_fd = -1;
            dispatch_source_cancel(s->wr_src);
            if (!s->write_resumed) {
                s->write_resumed = true;
                dispatch_resume(s->wr_src);
            }
            MN_DISPATCH_RELEASE(s->wr_src);
        } else if (s->wr_fd >= 0) {
            close(s->wr_fd);
            s->wr_fd = -1;
        }
        mn_pad_txq_destroy(&s->txq);
    }
    g_pad.paused = false;
    g_pad.selftest = false;
    g_pad.nslots = 0;
    g_pad.connect_obs = nil;
    g_pad.disconnect_obs = nil;
    memset(g_pad.avail_hid, 0, sizeof(g_pad.avail_hid));
    for (int i = 0; i < MN_GAMEPAD_MAX_SLOTS; i++) {
        mn_pad_slot_t *s = &g_pad.slots[i];
        s->wr_fd = -1;
        s->rd_fd = -1;
        s->write_resumed = false;
        s->rx_len = 0;
        s->rx_discard = false;
        s->hello_seen = false;
        s->caps_hid = false;
        s->uinput_active = false;
        s->profile = MN_PAD_PROFILE_XBOX360;
        memset(&s->prev_ev, 0, sizeof(s->prev_ev));
        s->st_phase = ST_PHASE_IDLE;
        s->st_seen_nonzero_rumble = false;
        s->st_pulse_high = false;
        s->st_got_hid_output = false;
        s->st_got_hid_get = false;
        s->st_got_hid_set = false;
    }
}

void mn_gamepad_shutdown(void) {
    if ([NSThread isMainThread]) {
        shutdown_on_main();
    } else {
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        if (done == nil) {
            return;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            shutdown_on_main();
            dispatch_semaphore_signal(done);
        });
        (void)dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC));
        MN_DISPATCH_RELEASE(done);
    }
}

void mn_gamepad_set_paused(int paused) {
    dispatch_block_t blk = ^{
        g_pad.paused = (paused != 0);
        if (g_pad.paused) {
            mn_rumble_stop_all();
        }
    };
    if (pthread_main_np() != 0) {
        blk();
    } else {
        dispatch_async(dispatch_get_main_queue(), blk);
    }
}
