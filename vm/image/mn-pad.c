/*
 * mn-pad: MacNeutron guest virtual gamepad and raw HID daemon (MIT License).
 *
 * Bridges a virtio-console line protocol (MN_PAD_PORT, default /dev/virtio-ports/mn.pad)
 * to Linux /dev/uinput (with a 16-slot CLOCK_MONOTONIC ff-memless rumble state machine)
 * and /dev/uhid (raw HID passthrough).
 *
 * Build & self-test on macOS or Linux:
 *   cc -Wall -Wextra -Werror -std=c11 mn-pad.c -o mn-pad && ./mn-pad --selftest
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/input.h>
#include <linux/uhid.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#endif

#define MN_PAD_DEFAULT_PORT "/dev/virtio-ports/mn.pad"
#define MN_PAD_LINE_MAX     16384u
#define MN_PAD_TX_CAP       65536u
#define MN_PAD_FF_SLOTS     16
#define MN_PAD_MAX_KEYS     256
#define MN_PAD_MAX_AXES     64
#define MN_PAD_MAX_EV       64
#define MN_PAD_UHID_DATA    4096u
#define MN_PAD_POLL_TX_MS   200

#ifndef EV_SYN
#define EV_SYN     0x00
#define SYN_REPORT 0
#endif
#ifndef EV_KEY
#define EV_KEY 0x01
#endif
#ifndef EV_ABS
#define EV_ABS 0x03
#endif
#ifndef EV_FF
#define EV_FF 0x15
#endif
#ifndef FF_RUMBLE
#define FF_RUMBLE 0x50
#endif
#ifndef KEY_MAX
#define KEY_MAX 0x2ff
#endif
#ifndef ABS_MAX
#define ABS_MAX 0x3f
#endif
#ifndef UINPUT_MAX_NAME_SIZE
#define UINPUT_MAX_NAME_SIZE 80
#endif

/* Clean-room packed Linux UAPI uhid_event layout (4380 bytes) for portable ABI verification. */
enum mn_uhid_event_type {
    MN_UHID_CREATE = 0,
    MN_UHID_DESTROY = 1,
    MN_UHID_START = 2,
    MN_UHID_STOP = 3,
    MN_UHID_OPEN = 4,
    MN_UHID_CLOSE = 5,
    MN_UHID_OUTPUT = 6,
    MN_UHID_OUTPUT_EV = 7,
    MN_UHID_INPUT = 8,
    MN_UHID_GET_REPORT = 9,
    MN_UHID_GET_REPORT_REPLY = 10,
    MN_UHID_CREATE2 = 11,
    MN_UHID_INPUT2 = 12,
    MN_UHID_SET_REPORT = 13,
    MN_UHID_SET_REPORT_REPLY = 14,
};

enum mn_uhid_report_type {
    MN_UHID_FEATURE_REPORT = 0,
    MN_UHID_OUTPUT_REPORT = 1,
    MN_UHID_INPUT_REPORT = 2,
};

struct __attribute__((packed)) mn_uhid_create_req {
    uint8_t name[128];
    uint8_t phys[64];
    uint8_t uniq[64];
    uint8_t *rd_data;
    uint16_t rd_size;
    uint16_t bus;
    uint32_t vendor;
    uint32_t product;
    uint32_t version;
    uint32_t country;
};

struct __attribute__((packed)) mn_uhid_create2_req {
    uint8_t name[128];
    uint8_t phys[64];
    uint8_t uniq[64];
    uint16_t rd_size;
    uint16_t bus;
    uint32_t vendor;
    uint32_t product;
    uint32_t version;
    uint32_t country;
    uint8_t rd_data[MN_PAD_UHID_DATA];
};

struct __attribute__((packed)) mn_uhid_input_req {
    uint8_t data[MN_PAD_UHID_DATA];
    uint16_t size;
};

struct __attribute__((packed)) mn_uhid_input2_req {
    uint16_t size;
    uint8_t data[MN_PAD_UHID_DATA];
};

struct __attribute__((packed)) mn_uhid_output_req {
    uint8_t data[MN_PAD_UHID_DATA];
    uint16_t size;
    uint8_t rtype;
};

struct __attribute__((packed)) mn_uhid_output_ev_req {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct __attribute__((packed)) mn_uhid_get_report_req {
    uint32_t id;
    uint8_t rnum;
    uint8_t rtype;
};

struct __attribute__((packed)) mn_uhid_get_report_reply_req {
    uint32_t id;
    uint16_t err;
    uint16_t size;
    uint8_t data[MN_PAD_UHID_DATA];
};

struct __attribute__((packed)) mn_uhid_set_report_req {
    uint32_t id;
    uint8_t rnum;
    uint8_t rtype;
    uint16_t size;
    uint8_t data[MN_PAD_UHID_DATA];
};

struct __attribute__((packed)) mn_uhid_set_report_reply_req {
    uint32_t id;
    uint16_t err;
};

/* Unpacked in Linux <linux/uhid.h>, giving union u 8-byte alignment (4376B) and uhid_event 4380B. */
struct mn_uhid_start_req {
    uint64_t dev_flags;
};

struct __attribute__((packed)) mn_uhid_event {
    uint32_t type;
    union {
        struct mn_uhid_create_req create;
        struct mn_uhid_input_req input;
        struct mn_uhid_output_req output;
        struct mn_uhid_output_ev_req output_ev;
        struct mn_uhid_create2_req create2;
        struct mn_uhid_input2_req input2;
        struct mn_uhid_get_report_req get_report;
        struct mn_uhid_get_report_reply_req get_report_reply;
        struct mn_uhid_set_report_req set_report;
        struct mn_uhid_set_report_reply_req set_report_reply;
        struct mn_uhid_start_req start;
    } u;
};

_Static_assert(sizeof(struct mn_uhid_event) == 4380, "mn_uhid_event must be 4380 bytes");
#ifdef __linux__
_Static_assert(sizeof(struct uhid_event) == 4380, "kernel uhid_event must be 4380 bytes");
_Static_assert(sizeof(struct uhid_event) == sizeof(struct mn_uhid_event), "uhid_event size mismatch");
_Static_assert(offsetof(struct uhid_event, u.create2.rd_data) ==
                   offsetof(struct mn_uhid_event, u.create2.rd_data),
               "uhid_create2 rd_data offset mismatch");
_Static_assert(offsetof(struct uhid_event, u.get_report_reply.data) ==
                   offsetof(struct mn_uhid_event, u.get_report_reply.data),
               "uhid_get_report_reply data offset mismatch");
_Static_assert(offsetof(struct uhid_event, u.set_report.data) ==
                   offsetof(struct mn_uhid_event, u.set_report.data),
               "uhid_set_report data offset mismatch");
#endif

struct tx_buf {
    char buf[MN_PAD_TX_CAP];
    size_t off;
    size_t len;
    bool overflow_logged;
};

struct rumble_slot {
    bool uploaded;
    bool playing;
    uint16_t strong;
    uint16_t weak;
    uint16_t length_ms;
    uint16_t delay_ms;
    uint32_t plays_left;
    uint64_t start_ms;
};

struct rumble_state {
    struct rumble_slot slots[MN_PAD_FF_SLOTS];
    uint16_t last_strong;
    uint16_t last_weak;
};

enum pad_cmd_kind {
    CMD_NONE = 0,
    CMD_CREATE,
    CMD_HID_CREATE,
    CMD_REMOVE,
    CMD_EV,
    CMD_HID_INPUT,
    CMD_HID_GET_REPLY,
    CMD_HID_SET_REPLY,
};

struct pad_axis_spec {
    uint16_t code;
    int32_t min;
    int32_t max;
    int32_t fuzz;
    int32_t flat;
};

struct pad_ev_item {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct pad_cmd {
    enum pad_cmd_kind kind;
    union {
        struct {
            uint16_t bus;
            uint16_t vid;
            uint16_t pid;
            uint16_t ver;
            uint16_t keys[MN_PAD_MAX_KEYS];
            size_t nkeys;
            struct pad_axis_spec axes[MN_PAD_MAX_AXES];
            size_t naxes;
            char name[UINPUT_MAX_NAME_SIZE];
        } create;
        struct {
            uint16_t bus;
            uint16_t vid;
            uint16_t pid;
            uint16_t ver;
            uint32_t country;
            uint8_t desc[MN_PAD_UHID_DATA];
            size_t desc_len;
            char name[128];
        } hid_create;
        struct {
            struct pad_ev_item items[MN_PAD_MAX_EV];
            size_t count;
        } ev;
        struct {
            uint8_t data[MN_PAD_UHID_DATA];
            size_t len;
        } hid_input;
        struct {
            uint32_t id;
            uint16_t err;
            uint8_t data[MN_PAD_UHID_DATA];
            size_t len;
        } hid_get_reply;
        struct {
            uint32_t id;
            uint16_t err;
        } hid_set_reply;
    } u;
};

#ifdef __linux__
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}
#endif

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return 0;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static bool hex_decode(const char *hex, uint8_t *out, size_t max_out, size_t *out_len) {
    size_t len = strlen(hex);
    if ((len & 1u) != 0)
        return false;
    size_t n = len / 2u;
    if (n > max_out)
        return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_val(hex[2 * i]);
        int lo = hex_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *out_len = n;
    return true;
}

static bool hex_encode(const uint8_t *in, size_t len, char *out, size_t out_cap) {
    static const char lut[] = "0123456789abcdef";
    if (out_cap < 2 * len + 1)
        return false;
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = lut[(in[i] >> 4) & 0xf];
        out[2 * i + 1] = lut[in[i] & 0xf];
    }
    out[2 * len] = '\0';
    return true;
}

static bool parse_hex_u32(const char *s, size_t min_digits, size_t max_digits, uint32_t *out) {
    size_t n = strlen(s);
    if (n < min_digits || n > max_digits)
        return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int d = hex_val(s[i]);
        if (d < 0)
            return false;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return true;
}

static bool parse_hex_u16(const char *s, uint16_t *out) {
    uint32_t v = 0;
    if (!parse_hex_u32(s, 1, 4, &v) || v > UINT16_MAX)
        return false;
    *out = (uint16_t)v;
    return true;
}

static bool parse_dec_u32(const char *s, uint32_t *out) {
    if (*s < '0' || *s > '9')
        return false;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v > UINT32_MAX)
        return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_dec_u16(const char *s, uint16_t *out) {
    uint32_t v = 0;
    if (!parse_dec_u32(s, &v) || v > UINT16_MAX)
        return false;
    *out = (uint16_t)v;
    return true;
}

static bool parse_dec_i32(const char *s, int32_t *out) {
    const char *p = s;
    if (*p == '-')
        p++;
    if (*p < '0' || *p > '9')
        return false;
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return false;
    }
    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v < INT32_MIN || v > INT32_MAX)
        return false;
    *out = (int32_t)v;
    return true;
}

/* Splits out the next single-space-delimited token from *cursor. */
static char *next_tok(char **cursor) {
    char *s = *cursor;
    if (!s || *s == '\0')
        return NULL;
    char *sp = strchr(s, ' ');
    if (sp) {
        *sp = '\0';
        *cursor = sp + 1;
    } else {
        *cursor = s + strlen(s);
    }
    return (*s != '\0') ? s : NULL;
}

static bool parse_keys_field(char *field, uint16_t *keys, size_t *nkeys) {
    *nkeys = 0;
    if (strcmp(field, "-") == 0)
        return true;
    char *p = field;
    for (;;) {
        if (*p == '\0')
            return false;
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';
        uint16_t code = 0;
        if (!parse_dec_u16(p, &code) || code > KEY_MAX)
            return false;
        if (*nkeys >= MN_PAD_MAX_KEYS)
            return false;
        keys[(*nkeys)++] = code;
        if (!comma)
            break;
        p = comma + 1;
    }
    return true;
}

static bool parse_axes_field(char *field, struct pad_axis_spec *axes, size_t *naxes) {
    *naxes = 0;
    if (strcmp(field, "-") == 0)
        return true;
    char *p = field;
    for (;;) {
        if (*p == '\0')
            return false;
        char *comma = strchr(p, ',');
        if (comma)
            *comma = '\0';

        char *parts[5];
        char *cur = p;
        for (int i = 0; i < 4; i++) {
            char *colon = strchr(cur, ':');
            if (!colon)
                return false;
            *colon = '\0';
            parts[i] = cur;
            cur = colon + 1;
        }
        if (strchr(cur, ':') != NULL)
            return false;
        parts[4] = cur;

        struct pad_axis_spec ax;
        if (!parse_dec_u16(parts[0], &ax.code) || ax.code > ABS_MAX)
            return false;
        if (!parse_dec_i32(parts[1], &ax.min) || !parse_dec_i32(parts[2], &ax.max) ||
            !parse_dec_i32(parts[3], &ax.fuzz) || !parse_dec_i32(parts[4], &ax.flat))
            return false;
        if (ax.min > ax.max || ax.fuzz < 0 || ax.flat < 0)
            return false;
        if (*naxes >= MN_PAD_MAX_AXES)
            return false;
        axes[(*naxes)++] = ax;

        if (!comma)
            break;
        p = comma + 1;
    }
    return true;
}

/* Parses a NUL-terminated line (modified in-place). Returns false on malformed input. */
static bool parse_line(char *line, struct pad_cmd *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    if (line[0] == '\0')
        return false;

    char *cur = line;
    char *op = next_tok(&cur);
    if (!op)
        return false;

    if (strcmp(op, "remove") == 0) {
        if (*cur != '\0')
            return false;
        cmd->kind = CMD_REMOVE;
        return true;
    }

    if (strcmp(op, "create") == 0) {
        char *f_bus = next_tok(&cur);
        char *f_vid = next_tok(&cur);
        char *f_pid = next_tok(&cur);
        char *f_ver = next_tok(&cur);
        char *f_keys = next_tok(&cur);
        char *f_axes = next_tok(&cur);
        char *name = cur;
        if (!f_bus || !f_vid || !f_pid || !f_ver || !f_keys || !f_axes || !name || *name == '\0')
            return false;
        if (!parse_hex_u16(f_bus, &cmd->u.create.bus) ||
            !parse_hex_u16(f_vid, &cmd->u.create.vid) ||
            !parse_hex_u16(f_pid, &cmd->u.create.pid) ||
            !parse_hex_u16(f_ver, &cmd->u.create.ver))
            return false;
        if (!parse_keys_field(f_keys, cmd->u.create.keys, &cmd->u.create.nkeys))
            return false;
        if (!parse_axes_field(f_axes, cmd->u.create.axes, &cmd->u.create.naxes))
            return false;
        snprintf(cmd->u.create.name, sizeof(cmd->u.create.name), "%s", name);
        cmd->kind = CMD_CREATE;
        return true;
    }

    if (strcmp(op, "hid-create") == 0) {
        char *f_bus = next_tok(&cur);
        char *f_vid = next_tok(&cur);
        char *f_pid = next_tok(&cur);
        char *f_ver = next_tok(&cur);
        char *f_country = next_tok(&cur);
        char *f_desc = next_tok(&cur);
        char *name = cur;
        if (!f_bus || !f_vid || !f_pid || !f_ver || !f_country || !f_desc || !name || *name == '\0')
            return false;
        if (!parse_hex_u16(f_bus, &cmd->u.hid_create.bus) ||
            !parse_hex_u16(f_vid, &cmd->u.hid_create.vid) ||
            !parse_hex_u16(f_pid, &cmd->u.hid_create.pid) ||
            !parse_hex_u16(f_ver, &cmd->u.hid_create.ver) ||
            !parse_hex_u32(f_country, 1, 8, &cmd->u.hid_create.country))
            return false;
        if (!hex_decode(f_desc, cmd->u.hid_create.desc, MN_PAD_UHID_DATA,
                        &cmd->u.hid_create.desc_len) ||
            cmd->u.hid_create.desc_len == 0)
            return false;
        snprintf(cmd->u.hid_create.name, sizeof(cmd->u.hid_create.name), "%s", name);
        cmd->kind = CMD_HID_CREATE;
        return true;
    }

    if (strcmp(op, "ev") == 0) {
        if (*cur == '\0')
            return false;
        while (*cur != '\0') {
            char *tok = next_tok(&cur);
            if (!tok)
                return false;
            char *c1 = strchr(tok, ':');
            if (!c1)
                return false;
            *c1 = '\0';
            char *c2 = strchr(c1 + 1, ':');
            if (!c2 || strchr(c2 + 1, ':') != NULL)
                return false;
            *c2 = '\0';

            uint16_t type = 0, code = 0;
            int32_t value = 0;
            if (!parse_dec_u16(tok, &type) || (type != EV_KEY && type != EV_ABS))
                return false;
            if (!parse_dec_u16(c1 + 1, &code))
                return false;
            if (!parse_dec_i32(c2 + 1, &value))
                return false;
            if (cmd->u.ev.count >= MN_PAD_MAX_EV)
                return false;
            cmd->u.ev.items[cmd->u.ev.count++] = (struct pad_ev_item){
                .type = type,
                .code = code,
                .value = value,
            };
        }
        if (cmd->u.ev.count == 0)
            return false;
        cmd->kind = CMD_EV;
        return true;
    }

    if (strcmp(op, "hid-input") == 0) {
        char *f_hex = next_tok(&cur);
        if (!f_hex || *cur != '\0')
            return false;
        if (!hex_decode(f_hex, cmd->u.hid_input.data, MN_PAD_UHID_DATA, &cmd->u.hid_input.len) ||
            cmd->u.hid_input.len == 0)
            return false;
        cmd->kind = CMD_HID_INPUT;
        return true;
    }

    if (strcmp(op, "hid-get-reply") == 0) {
        char *f_id = next_tok(&cur);
        char *f_err = next_tok(&cur);
        if (!f_id || !f_err)
            return false;
        if (!parse_dec_u32(f_id, &cmd->u.hid_get_reply.id) ||
            !parse_dec_u16(f_err, &cmd->u.hid_get_reply.err))
            return false;
        if (*cur != '\0') {
            char *f_hex = next_tok(&cur);
            if (!f_hex || *cur != '\0')
                return false;
            if (!hex_decode(f_hex, cmd->u.hid_get_reply.data, MN_PAD_UHID_DATA,
                            &cmd->u.hid_get_reply.len))
                return false;
        }
        cmd->kind = CMD_HID_GET_REPLY;
        return true;
    }

    if (strcmp(op, "hid-set-reply") == 0) {
        char *f_id = next_tok(&cur);
        char *f_err = next_tok(&cur);
        if (!f_id || !f_err || *cur != '\0')
            return false;
        if (!parse_dec_u32(f_id, &cmd->u.hid_set_reply.id) ||
            !parse_dec_u16(f_err, &cmd->u.hid_set_reply.err))
            return false;
        cmd->kind = CMD_HID_SET_REPLY;
        return true;
    }

    return false;
}

static void tx_init(struct tx_buf *tx) {
    tx->off = 0;
    tx->len = 0;
    tx->overflow_logged = false;
}

static bool tx_queue(struct tx_buf *tx, const char *data, size_t n) {
    if (n > sizeof(tx->buf))
        return false;
    if (tx->off > 0 && tx->off + tx->len + n > sizeof(tx->buf)) {
        memmove(tx->buf, tx->buf + tx->off, tx->len);
        tx->off = 0;
    }
    if (tx->len + n > sizeof(tx->buf)) {
        if (!tx->overflow_logged) {
            fprintf(stderr, "mn-pad: tx buffer full, dropping message\n");
            tx->overflow_logged = true;
        }
        return false;
    }
    memcpy(tx->buf + tx->off + tx->len, data, n);
    tx->len += n;
    return true;
}

static bool tx_queue_str(struct tx_buf *tx, const char *s) {
    return tx_queue(tx, s, strlen(s));
}

static int tx_flush(struct tx_buf *tx, int fd) {
    while (tx->len > 0) {
        ssize_t w = write(fd, tx->buf + tx->off, tx->len);
        if (w > 0) {
            tx->off += (size_t)w;
            tx->len -= (size_t)w;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            return -1;
        }
    }
    if (tx->len == 0) {
        tx->off = 0;
        tx->overflow_logged = false;
    } else if (tx->off > 0) {
        memmove(tx->buf, tx->buf + tx->off, tx->len);
        tx->off = 0;
    }
    return 0;
}

static void rumble_init(struct rumble_state *rs) {
    memset(rs, 0, sizeof(*rs));
}

static void rumble_emit_if_changed(struct rumble_state *rs, uint16_t strong, uint16_t weak,
                                   struct tx_buf *tx) {
    if (strong == rs->last_strong && weak == rs->last_weak)
        return;
    rs->last_strong = strong;
    rs->last_weak = weak;
    char line[64];
    int n = snprintf(line, sizeof(line), "rumble %u %u\n", (unsigned)strong, (unsigned)weak);
    if (n > 0 && (size_t)n < sizeof(line))
        tx_queue(tx, line, (size_t)n);
}

static uint64_t rumble_tick(struct rumble_state *rs, uint64_t now_ms, struct tx_buf *tx) {
    uint32_t sum_strong = 0;
    uint32_t sum_weak = 0;
    uint64_t next_deadline = UINT64_MAX;

    for (int i = 0; i < MN_PAD_FF_SLOTS; i++) {
        struct rumble_slot *s = &rs->slots[i];
        if (!s->uploaded || !s->playing || s->plays_left == 0) {
            s->playing = false;
            continue;
        }
        if (now_ms < s->start_ms) {
            if (s->start_ms < next_deadline)
                next_deadline = s->start_ms;
            continue;
        }
        if (s->length_ms == 0) {
            /* Zero length plays indefinitely until EV_FF value=0 or erase. */
            sum_strong += s->strong;
            sum_weak += s->weak;
            continue;
        }
        uint64_t period = s->length_ms;
        uint64_t elapsed_periods = (now_ms - s->start_ms) / period;
        if (elapsed_periods >= s->plays_left) {
            s->plays_left = 0;
            s->playing = false;
        } else if (elapsed_periods > 0) {
            s->plays_left -= (uint32_t)elapsed_periods;
            s->start_ms += elapsed_periods * period;
        }
        if (s->playing && now_ms >= s->start_ms) {
            sum_strong += s->strong;
            sum_weak += s->weak;
            uint64_t end_ms = (s->start_ms <= UINT64_MAX - period) ? (s->start_ms + period) : UINT64_MAX;
            if (end_ms < next_deadline)
                next_deadline = end_ms;
        }
    }

    uint16_t out_strong = (sum_strong > UINT16_MAX) ? UINT16_MAX : (uint16_t)sum_strong;
    uint16_t out_weak = (sum_weak > UINT16_MAX) ? UINT16_MAX : (uint16_t)sum_weak;
    rumble_emit_if_changed(rs, out_strong, out_weak, tx);
    return next_deadline;
}

static int32_t rumble_upload(struct rumble_state *rs, int32_t id, uint16_t type, uint16_t strong,
                             uint16_t weak, uint16_t length_ms, uint16_t delay_ms,
                             uint64_t now_ms, struct tx_buf *tx) {
    if (id < 0 || id >= MN_PAD_FF_SLOTS || type != FF_RUMBLE)
        return -EINVAL;
    struct rumble_slot *s = &rs->slots[id];
    s->uploaded = true;
    s->strong = strong;
    s->weak = weak;
    s->length_ms = length_ms;
    s->delay_ms = delay_ms;
    if (s->playing)
        s->start_ms = now_ms + delay_ms;
    rumble_tick(rs, now_ms, tx);
    return 0;
}

static int32_t rumble_erase(struct rumble_state *rs, uint32_t id, uint64_t now_ms,
                            struct tx_buf *tx) {
    if (id >= MN_PAD_FF_SLOTS)
        return -EINVAL;
    memset(&rs->slots[id], 0, sizeof(rs->slots[id]));
    rumble_tick(rs, now_ms, tx);
    return 0;
}

static void rumble_play(struct rumble_state *rs, uint16_t code, int32_t value, uint64_t now_ms,
                        struct tx_buf *tx) {
    if (code >= MN_PAD_FF_SLOTS)
        return;
    struct rumble_slot *s = &rs->slots[code];
    if (!s->uploaded)
        return;
    if (value > 0) {
        s->playing = true;
        s->plays_left = (uint32_t)value;
        s->start_ms = now_ms + s->delay_ms;
    } else {
        s->playing = false;
        s->plays_left = 0;
    }
    rumble_tick(rs, now_ms, tx);
}

static void rumble_reset(struct rumble_state *rs, struct tx_buf *tx) {
    memset(rs->slots, 0, sizeof(rs->slots));
    rumble_emit_if_changed(rs, 0, 0, tx);
}

static const char *port_basename(const char *port_path) {
    const char *slash = strrchr(port_path, '/');
    const char *base = slash ? (slash + 1) : port_path;
    return (*base != '\0') ? base : "mn.pad";
}

#ifdef __linux__

enum active_dev_kind {
    DEV_NONE = 0,
    DEV_UINPUT,
    DEV_UHID,
};

struct pad_daemon {
    int port_fd;
    int dev_fd;
    enum active_dev_kind dev_kind;
    char phys[64];
    struct tx_buf tx;
    struct rumble_state rumble;
};

static bool write_full(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w > 0) {
            off += (size_t)w;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static void destroy_active_dev(struct pad_daemon *d) {
    if (d->dev_fd >= 0) {
        if (d->dev_kind == DEV_UINPUT) {
            (void)ioctl(d->dev_fd, UI_DEV_DESTROY);
        } else if (d->dev_kind == DEV_UHID) {
            struct uhid_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = UHID_DESTROY;
            (void)write_full(d->dev_fd, &ev, sizeof(ev));
        }
        close(d->dev_fd);
        d->dev_fd = -1;
    }
    d->dev_kind = DEV_NONE;
    rumble_reset(&d->rumble, &d->tx);
}

static void handle_create(struct pad_daemon *d, const struct pad_cmd *cmd) {
    destroy_active_dev(d);

    int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "mn-pad: open /dev/uinput failed: %s\n", strerror(errno));
        return;
    }

    if (ioctl(fd, UI_SET_PHYS, d->phys) < 0) {
        fprintf(stderr, "mn-pad: UI_SET_PHYS failed: %s\n", strerror(errno));
        close(fd);
        return;
    }

    if (cmd->u.create.nkeys > 0) {
        if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
            fprintf(stderr, "mn-pad: UI_SET_EVBIT EV_KEY failed: %s\n", strerror(errno));
            close(fd);
            return;
        }
        for (size_t i = 0; i < cmd->u.create.nkeys; i++) {
            if (ioctl(fd, UI_SET_KEYBIT, (int)cmd->u.create.keys[i]) < 0) {
                fprintf(stderr, "mn-pad: UI_SET_KEYBIT %u failed: %s\n",
                        (unsigned)cmd->u.create.keys[i], strerror(errno));
                close(fd);
                return;
            }
        }
    }

    if (cmd->u.create.naxes > 0) {
        if (ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) {
            fprintf(stderr, "mn-pad: UI_SET_EVBIT EV_ABS failed: %s\n", strerror(errno));
            close(fd);
            return;
        }
        for (size_t i = 0; i < cmd->u.create.naxes; i++) {
            const struct pad_axis_spec *ax = &cmd->u.create.axes[i];
            if (ioctl(fd, UI_SET_ABSBIT, (int)ax->code) < 0) {
                fprintf(stderr, "mn-pad: UI_SET_ABSBIT %u failed: %s\n", (unsigned)ax->code,
                        strerror(errno));
                close(fd);
                return;
            }
            struct uinput_abs_setup abs_setup;
            memset(&abs_setup, 0, sizeof(abs_setup));
            abs_setup.code = ax->code;
            abs_setup.absinfo.minimum = ax->min;
            abs_setup.absinfo.maximum = ax->max;
            abs_setup.absinfo.fuzz = ax->fuzz;
            abs_setup.absinfo.flat = ax->flat;
            if (ioctl(fd, UI_ABS_SETUP, &abs_setup) < 0) {
                fprintf(stderr, "mn-pad: UI_ABS_SETUP %u failed: %s\n", (unsigned)ax->code,
                        strerror(errno));
                close(fd);
                return;
            }
        }
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_FF) < 0 || ioctl(fd, UI_SET_FFBIT, FF_RUMBLE) < 0) {
        fprintf(stderr, "mn-pad: UI_SET_FFBIT FF_RUMBLE failed: %s\n", strerror(errno));
        close(fd);
        return;
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = cmd->u.create.bus;
    usetup.id.vendor = cmd->u.create.vid;
    usetup.id.product = cmd->u.create.pid;
    usetup.id.version = cmd->u.create.ver;
    usetup.ff_effects_max = MN_PAD_FF_SLOTS;
    snprintf(usetup.name, sizeof(usetup.name), "%s", cmd->u.create.name);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "mn-pad: UI_DEV_SETUP/CREATE failed: %s\n", strerror(errno));
        close(fd);
        return;
    }

    d->dev_fd = fd;
    d->dev_kind = DEV_UINPUT;
}

static void handle_hid_create(struct pad_daemon *d, const struct pad_cmd *cmd) {
    destroy_active_dev(d);

    int fd = open("/dev/uhid", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "mn-pad: open /dev/uhid failed: %s\n", strerror(errno));
        return;
    }

    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    snprintf((char *)ev.u.create2.name, sizeof(ev.u.create2.name), "%s", cmd->u.hid_create.name);
    snprintf((char *)ev.u.create2.phys, sizeof(ev.u.create2.phys), "%s", d->phys);
    ev.u.create2.rd_size = (uint16_t)cmd->u.hid_create.desc_len;
    ev.u.create2.bus = cmd->u.hid_create.bus;
    ev.u.create2.vendor = cmd->u.hid_create.vid;
    ev.u.create2.product = cmd->u.hid_create.pid;
    ev.u.create2.version = cmd->u.hid_create.ver;
    ev.u.create2.country = cmd->u.hid_create.country;
    memcpy(ev.u.create2.rd_data, cmd->u.hid_create.desc, cmd->u.hid_create.desc_len);

    if (!write_full(fd, &ev, sizeof(ev))) {
        fprintf(stderr, "mn-pad: UHID_CREATE2 write failed: %s\n", strerror(errno));
        close(fd);
        return;
    }

    d->dev_fd = fd;
    d->dev_kind = DEV_UHID;
}

static void handle_ev(struct pad_daemon *d, const struct pad_cmd *cmd) {
    if (d->dev_kind != DEV_UINPUT || d->dev_fd < 0)
        return;
    struct input_event buf[MN_PAD_MAX_EV + 1];
    memset(buf, 0, sizeof(buf[0]) * (cmd->u.ev.count + 1));
    for (size_t i = 0; i < cmd->u.ev.count; i++) {
        buf[i].type = cmd->u.ev.items[i].type;
        buf[i].code = cmd->u.ev.items[i].code;
        buf[i].value = cmd->u.ev.items[i].value;
    }
    buf[cmd->u.ev.count].type = EV_SYN;
    buf[cmd->u.ev.count].code = SYN_REPORT;
    buf[cmd->u.ev.count].value = 0;

    size_t bytes = (cmd->u.ev.count + 1) * sizeof(struct input_event);
    ssize_t w;
    do {
        w = write(d->dev_fd, buf, bytes);
    } while (w < 0 && errno == EINTR);
    if (w < 0 || (size_t)w != bytes)
        fprintf(stderr, "mn-pad: uinput write failed: %s\n", strerror(errno));
}

static void handle_hid_input(struct pad_daemon *d, const struct pad_cmd *cmd) {
    if (d->dev_kind != DEV_UHID || d->dev_fd < 0)
        return;
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_INPUT2;
    ev.u.input2.size = (uint16_t)cmd->u.hid_input.len;
    memcpy(ev.u.input2.data, cmd->u.hid_input.data, cmd->u.hid_input.len);
    if (!write_full(d->dev_fd, &ev, sizeof(ev)))
        fprintf(stderr, "mn-pad: UHID_INPUT2 write failed: %s\n", strerror(errno));
}

static void handle_hid_get_reply(struct pad_daemon *d, const struct pad_cmd *cmd) {
    if (d->dev_kind != DEV_UHID || d->dev_fd < 0)
        return;
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_GET_REPORT_REPLY;
    ev.u.get_report_reply.id = cmd->u.hid_get_reply.id;
    ev.u.get_report_reply.err = cmd->u.hid_get_reply.err;
    ev.u.get_report_reply.size = (uint16_t)cmd->u.hid_get_reply.len;
    if (cmd->u.hid_get_reply.len > 0)
        memcpy(ev.u.get_report_reply.data, cmd->u.hid_get_reply.data, cmd->u.hid_get_reply.len);
    if (!write_full(d->dev_fd, &ev, sizeof(ev)))
        fprintf(stderr, "mn-pad: UHID_GET_REPORT_REPLY write failed: %s\n", strerror(errno));
}

static void handle_hid_set_reply(struct pad_daemon *d, const struct pad_cmd *cmd) {
    if (d->dev_kind != DEV_UHID || d->dev_fd < 0)
        return;
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_SET_REPORT_REPLY;
    ev.u.set_report_reply.id = cmd->u.hid_set_reply.id;
    ev.u.set_report_reply.err = cmd->u.hid_set_reply.err;
    if (!write_full(d->dev_fd, &ev, sizeof(ev)))
        fprintf(stderr, "mn-pad: UHID_SET_REPORT_REPLY write failed: %s\n", strerror(errno));
}

static void dispatch_cmd(struct pad_daemon *d, const struct pad_cmd *cmd) {
    switch (cmd->kind) {
    case CMD_CREATE:
        handle_create(d, cmd);
        break;
    case CMD_HID_CREATE:
        handle_hid_create(d, cmd);
        break;
    case CMD_REMOVE:
        destroy_active_dev(d);
        break;
    case CMD_EV:
        handle_ev(d, cmd);
        break;
    case CMD_HID_INPUT:
        handle_hid_input(d, cmd);
        break;
    case CMD_HID_GET_REPLY:
        handle_hid_get_reply(d, cmd);
        break;
    case CMD_HID_SET_REPLY:
        handle_hid_set_reply(d, cmd);
        break;
    case CMD_NONE:
        break;
    }
}

static const char *uhid_rtype_str(uint8_t rtype) {
    switch (rtype) {
    case UHID_FEATURE_REPORT:
        return "feature";
    case UHID_OUTPUT_REPORT:
        return "output";
    case UHID_INPUT_REPORT:
        return "input";
    default:
        return NULL;
    }
}

static void handle_uinput_readable(struct pad_daemon *d) {
    struct input_event evs[64];
    for (;;) {
        ssize_t n = read(d->dev_fd, evs, sizeof(evs));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            fprintf(stderr, "mn-pad: uinput read failed: %s\n", strerror(errno));
            break;
        }
        if (n == 0)
            break;
        size_t cnt = (size_t)n / sizeof(struct input_event);
        for (size_t i = 0; i < cnt; i++) {
            const struct input_event *ev = &evs[i];
            if (ev->type == EV_UINPUT) {
                if (ev->code == UI_FF_UPLOAD) {
                    struct uinput_ff_upload up;
                    memset(&up, 0, sizeof(up));
                    up.request_id = (uint32_t)ev->value;
                    if (ioctl(d->dev_fd, UI_BEGIN_FF_UPLOAD, &up) == 0) {
                        uint64_t now = monotonic_ms();
                        up.retval = rumble_upload(&d->rumble, up.effect.id, up.effect.type,
                                                  up.effect.u.rumble.strong_magnitude,
                                                  up.effect.u.rumble.weak_magnitude,
                                                  up.effect.replay.length, up.effect.replay.delay,
                                                  now, &d->tx);
                        if (ioctl(d->dev_fd, UI_END_FF_UPLOAD, &up) < 0)
                            fprintf(stderr, "mn-pad: UI_END_FF_UPLOAD failed: %s\n",
                                    strerror(errno));
                    }
                } else if (ev->code == UI_FF_ERASE) {
                    struct uinput_ff_erase er;
                    memset(&er, 0, sizeof(er));
                    er.request_id = (uint32_t)ev->value;
                    if (ioctl(d->dev_fd, UI_BEGIN_FF_ERASE, &er) == 0) {
                        uint64_t now = monotonic_ms();
                        er.retval = rumble_erase(&d->rumble, er.effect_id, now, &d->tx);
                        if (ioctl(d->dev_fd, UI_END_FF_ERASE, &er) < 0)
                            fprintf(stderr, "mn-pad: UI_END_FF_ERASE failed: %s\n",
                                    strerror(errno));
                    }
                }
            } else if (ev->type == EV_FF) {
                rumble_play(&d->rumble, ev->code, ev->value, monotonic_ms(), &d->tx);
            }
        }
    }
}

static void handle_uhid_readable(struct pad_daemon *d) {
    struct uhid_event ev;
    char hex[MN_PAD_UHID_DATA * 2u + 1u];
    char line[MN_PAD_LINE_MAX];

    for (;;) {
        ssize_t n = read(d->dev_fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            fprintf(stderr, "mn-pad: uhid read failed: %s\n", strerror(errno));
            break;
        }
        if (n == 0)
            break;

        switch (ev.type) {
        case UHID_OUTPUT: {
            const char *rt = uhid_rtype_str(ev.u.output.rtype);
            if (!rt || ev.u.output.size > MN_PAD_UHID_DATA)
                break;
            if (!hex_encode(ev.u.output.data, ev.u.output.size, hex, sizeof(hex)))
                break;
            int ln = snprintf(line, sizeof(line), "hid-output %s %s\n", rt, hex);
            if (ln > 0 && (size_t)ln < sizeof(line))
                tx_queue(&d->tx, line, (size_t)ln);
            break;
        }
        case UHID_GET_REPORT: {
            const char *rt = uhid_rtype_str(ev.u.get_report.rtype);
            if (!rt)
                break;
            int ln = snprintf(line, sizeof(line), "hid-get %u %s %u\n", ev.u.get_report.id, rt,
                              (unsigned)ev.u.get_report.rnum);
            if (ln > 0 && (size_t)ln < sizeof(line))
                tx_queue(&d->tx, line, (size_t)ln);
            break;
        }
        case UHID_SET_REPORT: {
            const char *rt = uhid_rtype_str(ev.u.set_report.rtype);
            if (!rt || ev.u.set_report.size > MN_PAD_UHID_DATA)
                break;
            if (!hex_encode(ev.u.set_report.data, ev.u.set_report.size, hex, sizeof(hex)))
                break;
            int ln = snprintf(line, sizeof(line), "hid-set %u %s %s\n", ev.u.set_report.id, rt,
                              hex);
            if (ln > 0 && (size_t)ln < sizeof(line))
                tx_queue(&d->tx, line, (size_t)ln);
            break;
        }
        default:
            break;
        }
    }
}

static int run_daemon(const char *port_path) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    int port_fd = open(port_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (port_fd < 0) {
        if (errno == ENOENT)
            return 0;
        fprintf(stderr, "mn-pad: open %s failed: %s\n", port_path, strerror(errno));
        return 1;
    }

    struct pad_daemon d;
    memset(&d, 0, sizeof(d));
    d.port_fd = port_fd;
    d.dev_fd = -1;
    d.dev_kind = DEV_NONE;
    snprintf(d.phys, sizeof(d.phys), "%s", port_basename(port_path));
    tx_init(&d.tx);
    rumble_init(&d.rumble);

    tx_queue_str(&d.tx, "hello\n");
    int uhid_probe = open("/dev/uhid", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (uhid_probe >= 0) {
        close(uhid_probe);
        tx_queue_str(&d.tx, "caps hid\n");
    }

    char line[MN_PAD_LINE_MAX];
    size_t line_len = 0;
    bool discard = false;
    char chunk[4096];
    int exit_code = 0;

    while (!g_stop) {
        uint64_t now_ms = monotonic_ms();
        uint64_t next_deadline = rumble_tick(&d.rumble, now_ms, &d.tx);
        if (d.tx.len > 0 && tx_flush(&d.tx, d.port_fd) < 0) {
            fprintf(stderr, "mn-pad: port write failed: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        int timeout_ms = -1;
        if (next_deadline != UINT64_MAX) {
            uint64_t cur_ms = monotonic_ms();
            uint64_t rem = (next_deadline > cur_ms) ? (next_deadline - cur_ms) : 0;
            timeout_ms = (rem > (uint64_t)INT_MAX) ? INT_MAX : (int)rem;
        }
        if (d.tx.len > 0 && (timeout_ms < 0 || timeout_ms > MN_PAD_POLL_TX_MS))
            timeout_ms = MN_PAD_POLL_TX_MS;

        struct pollfd pfds[2];
        nfds_t nfds = 1;
        pfds[0].fd = d.port_fd;
        pfds[0].events = POLLIN | (d.tx.len > 0 ? POLLOUT : 0);
        pfds[0].revents = 0;
        if (d.dev_fd >= 0) {
            pfds[1].fd = d.dev_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        int pr = poll(pfds, nfds, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "mn-pad: poll failed: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        if (nfds == 2 && (pfds[1].revents & POLLIN)) {
            if (d.dev_kind == DEV_UINPUT)
                handle_uinput_readable(&d);
            else if (d.dev_kind == DEV_UHID)
                handle_uhid_readable(&d);
        }

        if (pfds[0].revents & POLLOUT) {
            if (tx_flush(&d.tx, d.port_fd) < 0) {
                fprintf(stderr, "mn-pad: port write failed: %s\n", strerror(errno));
                exit_code = 1;
                break;
            }
        }

        if (pfds[0].revents & POLLIN) {
            bool eof = false;
            for (;;) {
                ssize_t n = read(d.port_fd, chunk, sizeof(chunk));
                if (n < 0) {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    fprintf(stderr, "mn-pad: port read failed: %s\n", strerror(errno));
                    exit_code = 1;
                    eof = true;
                    break;
                }
                if (n == 0) {
                    eof = true;
                    break;
                }
                for (ssize_t i = 0; i < n; i++) {
                    char c = chunk[i];
                    if (c == '\n') {
                        if (!discard) {
                            if (line_len > 0 && line[line_len - 1] == '\r')
                                line_len--;
                            line[line_len] = '\0';
                            struct pad_cmd cmd;
                            if (parse_line(line, &cmd))
                                dispatch_cmd(&d, &cmd);
                        }
                        line_len = 0;
                        discard = false;
                    } else if (!discard) {
                        if (c == '\0' || line_len >= sizeof(line) - 1)
                            discard = true;
                        else
                            line[line_len++] = c;
                    }
                }
            }
            if (eof)
                break;
        } else if ((pfds[0].revents & (POLLHUP | POLLERR)) != 0) {
            break;
        }
    }

    destroy_active_dev(&d);
    if (d.tx.len > 0)
        (void)tx_flush(&d.tx, d.port_fd);
    close(d.port_fd);
    return exit_code;
}

#endif /* __linux__ */

/* -------------------- Portable Self-Test Suite -------------------- */

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "selftest FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static bool parse_line_copy(const char *s, struct pad_cmd *cmd) {
    char *buf = malloc(strlen(s) + 1);
    if (!buf)
        return false;
    memcpy(buf, s, strlen(s) + 1);
    bool ok = parse_line(buf, cmd);
    free(buf);
    return ok;
}

static int test_uhid_abi(void) {
    CHECK(sizeof(struct mn_uhid_event) == 4380);
    CHECK(offsetof(struct mn_uhid_event, type) == 0);
    CHECK(offsetof(struct mn_uhid_event, u) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.create2.name) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.create2.phys) == 132);
    CHECK(offsetof(struct mn_uhid_event, u.create2.uniq) == 196);
    CHECK(offsetof(struct mn_uhid_event, u.create2.rd_size) == 260);
    CHECK(offsetof(struct mn_uhid_event, u.create2.bus) == 262);
    CHECK(offsetof(struct mn_uhid_event, u.create2.vendor) == 264);
    CHECK(offsetof(struct mn_uhid_event, u.create2.product) == 268);
    CHECK(offsetof(struct mn_uhid_event, u.create2.version) == 272);
    CHECK(offsetof(struct mn_uhid_event, u.create2.country) == 276);
    CHECK(offsetof(struct mn_uhid_event, u.create2.rd_data) == 280);
    CHECK(offsetof(struct mn_uhid_event, u.input2.size) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.input2.data) == 6);
    CHECK(offsetof(struct mn_uhid_event, u.output.data) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.output.size) == 4100);
    CHECK(offsetof(struct mn_uhid_event, u.output.rtype) == 4102);
    CHECK(offsetof(struct mn_uhid_event, u.get_report.id) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.get_report.rnum) == 8);
    CHECK(offsetof(struct mn_uhid_event, u.get_report.rtype) == 9);
    CHECK(offsetof(struct mn_uhid_event, u.get_report_reply.id) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.get_report_reply.err) == 8);
    CHECK(offsetof(struct mn_uhid_event, u.get_report_reply.size) == 10);
    CHECK(offsetof(struct mn_uhid_event, u.get_report_reply.data) == 12);
    CHECK(offsetof(struct mn_uhid_event, u.set_report.id) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.set_report.rnum) == 8);
    CHECK(offsetof(struct mn_uhid_event, u.set_report.rtype) == 9);
    CHECK(offsetof(struct mn_uhid_event, u.set_report.size) == 10);
    CHECK(offsetof(struct mn_uhid_event, u.set_report.data) == 12);
    CHECK(offsetof(struct mn_uhid_event, u.set_report_reply.id) == 4);
    CHECK(offsetof(struct mn_uhid_event, u.set_report_reply.err) == 8);
    return 0;
}

static int test_hex(void) {
    uint8_t out[8];
    size_t len = 0;
    CHECK(hex_decode("0aFf12", out, sizeof(out), &len));
    CHECK(len == 3 && out[0] == 0x0a && out[1] == 0xff && out[2] == 0x12);

    char encoded[16];
    CHECK(hex_encode(out, len, encoded, sizeof(encoded)));
    CHECK(strcmp(encoded, "0aff12") == 0);
    CHECK(!hex_encode(out, len, encoded, 6));

    CHECK(!hex_decode("0", out, sizeof(out), &len));
    CHECK(!hex_decode("0g", out, sizeof(out), &len));
    CHECK(!hex_decode("001122334455667788", out, sizeof(out), &len));
    CHECK(hex_decode("", out, sizeof(out), &len) && len == 0);
    return 0;
}

static void tx_take(struct tx_buf *tx, char *out, size_t cap) {
    size_t n = (tx->len < cap - 1) ? tx->len : (cap - 1);
    if (n > 0)
        memcpy(out, tx->buf + tx->off, n);
    out[n] = '\0';
    tx->off = 0;
    tx->len = 0;
}

static int test_rumble(void) {
    struct rumble_state rs;
    struct tx_buf tx;
    char got[256];
    rumble_init(&rs);
    tx_init(&tx);

    /* Reject non-FF_RUMBLE and out-of-range slots */
    CHECK(rumble_upload(&rs, 0, FF_RUMBLE + 1, 100, 100, 100, 0, 1000, &tx) == -EINVAL);
    CHECK(rumble_upload(&rs, -1, FF_RUMBLE, 100, 100, 100, 0, 1000, &tx) == -EINVAL);
    CHECK(rumble_upload(&rs, 16, FF_RUMBLE, 100, 100, 100, 0, 1000, &tx) == -EINVAL);
    CHECK(rumble_erase(&rs, 16, 1000, &tx) == -EINVAL);

    /* Basic play & expiration */
    CHECK(rumble_upload(&rs, 0, FF_RUMBLE, 30000, 10000, 100, 0, 1000, &tx) == 0);
    rumble_play(&rs, 0, 1, 1000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 30000 10000\n") == 0);
    CHECK(rumble_tick(&rs, 1099, &tx) == 1100);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);
    CHECK(rumble_tick(&rs, 1100, &tx) == UINT64_MAX);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);

    /* Delay + repetitions (including multi-period jump) */
    CHECK(rumble_upload(&rs, 1, FF_RUMBLE, 20000, 5000, 50, 40, 2000, &tx) == 0);
    rumble_play(&rs, 1, 3, 2000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);
    CHECK(rumble_tick(&rs, 2039, &tx) == 2040);
    CHECK(rumble_tick(&rs, 2040, &tx) == 2090);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 20000 5000\n") == 0);
    /* Jump across first two cycles into third (2140..2190) */
    CHECK(rumble_tick(&rs, 2150, &tx) == 2190);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);
    CHECK(rumble_tick(&rs, 2190, &tx) == UINT64_MAX);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);

    /* Zero-length indefinite + explicit stop */
    CHECK(rumble_upload(&rs, 2, FF_RUMBLE, 1234, 5678, 0, 0, 3000, &tx) == 0);
    rumble_play(&rs, 2, 1, 3000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 1234 5678\n") == 0);
    CHECK(rumble_tick(&rs, 9000000, &tx) == UINT64_MAX);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);
    rumble_play(&rs, 2, 0, 9000001, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);

    /* Re-upload while playing restarts start_ms = now + delay preserving plays_left */
    CHECK(rumble_upload(&rs, 3, FF_RUMBLE, 10000, 2000, 100, 0, 4000, &tx) == 0);
    rumble_play(&rs, 3, 2, 4000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 10000 2000\n") == 0);
    CHECK(rumble_upload(&rs, 3, FF_RUMBLE, 25000, 3000, 100, 25, 4050, &tx) == 0);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);
    CHECK(rs.slots[3].plays_left == 2);
    CHECK(rumble_tick(&rs, 4075, &tx) == 4175);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 25000 3000\n") == 0);

    /* Additive saturation + erase + reset */
    CHECK(rumble_upload(&rs, 4, FF_RUMBLE, 50000, 64000, 500, 0, 4100, &tx) == 0);
    rumble_play(&rs, 4, 1, 4100, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 65535 65535\n") == 0);
    CHECK(rumble_erase(&rs, 3, 4110, &tx) == 0);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 50000 64000\n") == 0);
    rumble_reset(&rs, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);
    /* Second reset does not emit duplicate 0 0 */
    rumble_reset(&rs, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);

    /* Huge clock jump (10 hours) with length_ms=1, value=1000000 completes in O(1) */
    CHECK(rumble_upload(&rs, 5, FF_RUMBLE, 15000, 5000, 1, 0, 5000, &tx) == 0);
    rumble_play(&rs, 5, 1000000, 5000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 15000 5000\n") == 0);
    CHECK(rumble_tick(&rs, 5000ULL + 10ULL * 3600ULL * 1000ULL, &tx) == UINT64_MAX);
    CHECK(!rs.slots[5].playing && rs.slots[5].plays_left == 0);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 0 0\n") == 0);

    /* Huge clock jump partial completion (10 hours = 36M periods out of 40M) */
    CHECK(rumble_upload(&rs, 6, FF_RUMBLE, 12000, 4000, 1, 0, 6000, &tx) == 0);
    rumble_play(&rs, 6, 40000000, 6000, &tx);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "rumble 12000 4000\n") == 0);
    uint64_t jump10h = 6000ULL + 10ULL * 3600ULL * 1000ULL;
    CHECK(rumble_tick(&rs, jump10h, &tx) == jump10h + 1ULL);
    CHECK(rs.slots[6].playing && rs.slots[6].plays_left == 4000000u);
    tx_take(&tx, got, sizeof(got));
    CHECK(strcmp(got, "") == 0);
    return 0;
}

static int test_protocol_parser(void) {
    struct pad_cmd cmd;

    /* Valid create with spaces in name */
    CHECK(parse_line_copy(
        "create 0003 045e 028e 0114 304,305,318 "
        "0:-32768:32767:16:128,16:-1:1:0:0 Microsoft X-Box 360 pad",
        &cmd));
    CHECK(cmd.kind == CMD_CREATE);
    CHECK(cmd.u.create.bus == 0x0003 && cmd.u.create.vid == 0x045e &&
          cmd.u.create.pid == 0x028e && cmd.u.create.ver == 0x0114);
    CHECK(cmd.u.create.nkeys == 3 && cmd.u.create.keys[0] == 304 && cmd.u.create.keys[2] == 318);
    CHECK(cmd.u.create.naxes == 2 && cmd.u.create.axes[0].min == -32768 &&
          cmd.u.create.axes[1].code == 16 && cmd.u.create.axes[1].max == 1);
    CHECK(strcmp(cmd.u.create.name, "Microsoft X-Box 360 pad") == 0);

    /* Valid create with '-' for keys/axes */
    CHECK(parse_line_copy("create 0003 054c 0ce6 8111 - - DualSense", &cmd));
    CHECK(cmd.u.create.nkeys == 0 && cmd.u.create.naxes == 0);

    /* Valid hid-create with max 4096-byte descriptor (8192 hex chars) */
    char max_hid[9000];
    strcpy(max_hid, "hid-create 0003 054c 0ce6 8111 00 ");
    size_t pos = strlen(max_hid);
    memset(max_hid + pos, 'a', 8192);
    strcpy(max_hid + pos + 8192, " Sony DualSense");
    CHECK(strlen(max_hid) < MN_PAD_LINE_MAX);
    CHECK(parse_line_copy(max_hid, &cmd));
    CHECK(cmd.kind == CMD_HID_CREATE && cmd.u.hid_create.desc_len == 4096);
    CHECK(strcmp(cmd.u.hid_create.name, "Sony DualSense") == 0);

    /* Valid remove, ev, hid-input, hid-get-reply, hid-set-reply */
    CHECK(parse_line_copy("remove", &cmd) && cmd.kind == CMD_REMOVE);
    CHECK(parse_line_copy("ev 1:304:1 3:0:-32768", &cmd) && cmd.kind == CMD_EV &&
          cmd.u.ev.count == 2 && cmd.u.ev.items[1].value == -32768);
    CHECK(parse_line_copy("hid-input 0102ff", &cmd) && cmd.kind == CMD_HID_INPUT &&
          cmd.u.hid_input.len == 3);
    CHECK(parse_line_copy("hid-get-reply 42 0 aabb", &cmd) && cmd.kind == CMD_HID_GET_REPLY &&
          cmd.u.hid_get_reply.id == 42 && cmd.u.hid_get_reply.err == 0 &&
          cmd.u.hid_get_reply.len == 2);
    CHECK(parse_line_copy("hid-get-reply 43 5", &cmd) && cmd.kind == CMD_HID_GET_REPLY &&
          cmd.u.hid_get_reply.id == 43 && cmd.u.hid_get_reply.err == 5 &&
          cmd.u.hid_get_reply.len == 0);
    CHECK(parse_line_copy("hid-set-reply 99 19", &cmd) && cmd.kind == CMD_HID_SET_REPLY &&
          cmd.u.hid_set_reply.id == 99 && cmd.u.hid_set_reply.err == 19);

    /* Fuzz & malformed rejections */
    const char *bad[] = {
        "",
        " ",
        "unknown arg",
        "remove extra",
        "create 0003 045e 028e 0114 304 -",
        "create 0003 045e 028e 0114 304 - ",
        "create 000G 045e 028e 0114 - - Pad",
        "create 00003 045e 028e 0114 - - Pad",
        "create 0003 045e 028e 0114 304, - Pad",
        "create 0003 045e 028e 0114 304,,305 - Pad",
        "create 0003 045e 028e 0114 999 - Pad",
        "create 0003 045e 028e 0114 - 0:10:-10:0:0 Pad",
        "create 0003 045e 028e 0114 - 0:0:10:-1:0 Pad",
        "create 0003 045e 028e 0114 - 0:0:10:0 Pad",
        "create 0003 045e 028e 0114 - 64:0:10:0:0 Pad",
        "hid-create 0003 054c 0ce6 8111 00 abc Pad",
        "hid-create 0003 054c 0ce6 8111 00 01",
        "hid-create 0003 054c 0ce6 8111 zz 0102 Pad",
        "ev",
        "ev 0:0:0",
        "ev 2:0:1",
        "ev 21:0:1",
        "ev 1:304",
        "ev 1:304:1:2",
        "ev 1:65536:1",
        "ev 1:304:3000000000",
        "hid-input",
        "hid-input 0",
        "hid-input 0102 extra",
        "hid-get-reply 1",
        "hid-get-reply -1 0 aa",
        "hid-get-reply 1 0 zzz",
        "hid-get-reply 1 0 aa extra",
        "hid-set-reply 1",
        "hid-set-reply 1 0 extra",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (parse_line_copy(bad[i], &cmd)) {
            fprintf(stderr, "selftest FAIL: accepted malformed line: '%s'\n", bad[i]);
            return 1;
        }
    }

    /* >4096-byte descriptor rejected */
    char over_hid[9000];
    strcpy(over_hid, "hid-create 0003 054c 0ce6 8111 00 ");
    pos = strlen(over_hid);
    memset(over_hid + pos, 'a', 8194);
    strcpy(over_hid + pos + 8194, " Over");
    CHECK(!parse_line_copy(over_hid, &cmd));

    /* basename extraction */
    CHECK(strcmp(port_basename("/dev/virtio-ports/mn.pad"), "mn.pad") == 0);
    CHECK(strcmp(port_basename("/dev/virtio-ports/mn.pad2"), "mn.pad2") == 0);
    CHECK(strcmp(port_basename("mn.pad"), "mn.pad") == 0);

    return 0;
}

static int test_tx_and_clock(void) {
    uint64_t t0 = monotonic_ms();
    uint64_t t1 = monotonic_ms();
    CHECK(t1 >= t0);

    int fds[2];
    CHECK(pipe(fds) == 0);
    int fl = fcntl(fds[1], F_GETFL, 0);
    CHECK(fl >= 0 && fcntl(fds[1], F_SETFL, fl | O_NONBLOCK) == 0);

    struct tx_buf tx;
    tx_init(&tx);
    CHECK(tx_queue_str(&tx, "hello\n"));
    CHECK(tx_queue_str(&tx, "caps hid\n"));
    CHECK(tx_flush(&tx, fds[1]) == 0);
    CHECK(tx.len == 0 && tx.off == 0);

    char buf[64];
    ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
    CHECK(n == 15);
    buf[n] = '\0';
    CHECK(strcmp(buf, "hello\ncaps hid\n") == 0);
    close(fds[0]);
    close(fds[1]);
    return 0;
}

static int run_selftest(void) {
    if (test_uhid_abi() != 0)
        return 1;
    if (test_hex() != 0)
        return 1;
    if (test_rumble() != 0)
        return 1;
    if (test_protocol_parser() != 0)
        return 1;
    if (test_tx_and_clock() != 0)
        return 1;
    printf("mn-pad: selftest ok\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
        return run_selftest();
    if (argc > 1) {
        fprintf(stderr, "usage: %s [--selftest]\n", argv[0]);
        return 2;
    }

#ifdef __linux__
    const char *port = getenv("MN_PAD_PORT");
    if (!port || *port == '\0')
        port = MN_PAD_DEFAULT_PORT;
    return run_daemon(port);
#else
    fprintf(stderr, "mn-pad: daemon mode requires Linux; use --selftest on host\n");
    return 1;
#endif
}
