// Clean-room MIT GameController + CoreHaptics + raw HID gamepad bridge for MacNeutron VM.
#ifndef MN_GAMEPAD_H
#define MN_GAMEPAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MN_GAMEPAD_MAX_SLOTS 4
#define MN_PAD_TX_CAP        (256u * 1024u)
#define MN_PAD_HID_MAX_DESC  4096u
#define MN_PAD_HID_MAX_REP   4096u
#define MN_PAD_DEADZONE      0.08f

/* Public VM lifecycle API (called from vm/host/src/main.rs).
 * On valid arguments (non-NULL arrays, 1 <= nslots <= MN_GAMEPAD_MAX_SLOTS), mn_gamepad_init
 * takes ownership of all wr_fds[0..nslots-1] and rd_fds[0..nslots-1], closing them on failure
 * or on mn_gamepad_shutdown(). */
int  mn_gamepad_init(const int *wr_fds, const int *rd_fds, int nslots);
void mn_gamepad_shutdown(void);
void mn_gamepad_set_paused(int paused);

/* Controller uinput profiles. */
typedef enum {
    MN_PAD_PROFILE_XBOX360 = 0,
    MN_PAD_PROFILE_DUALSENSE = 1,
    MN_PAD_PROFILE_DUALSHOCK4 = 2,
} mn_pad_profile_t;

/* Canonical logical controller input snapshot (GCController space: +Y is up, triggers 0..1). */
typedef struct {
    float lx, ly;
    float rx, ry;
    float lt, rt;
    float dpad_x, dpad_y;
    uint8_t a, b, x, y;
    uint8_t lb, rb;
    uint8_t lt_btn, rt_btn;
    uint8_t options, menu, home;
    uint8_t l3, r3;
} mn_pad_input_t;

/* Encoded Linux evdev state snapshot used for diffing. */
typedef struct {
    int32_t abs[6];   /* 0:X, 1:Y, 2:Z(LT), 3:RX, 4:RY, 5:RZ(RT) */
    int32_t hat_x;    /* 16:HAT0X (-1, 0, 1) */
    int32_t hat_y;    /* 17:HAT0Y (-1, 0, 1) */
    uint8_t keys[13]; /* 304,305,307,308,310,311,312,313,314,315,316,317,318 */
} mn_pad_ev_state_t;

/* Pure math & mapping helpers. */
void mn_pad_apply_radial_deadzone(float x, float y, float deadzone, float *out_x, float *out_y);
void mn_pad_map_input(mn_pad_profile_t profile, const mn_pad_input_t *in, float deadzone, mn_pad_ev_state_t *out);

/* Line protocol encoders (return bytes written excluding NUL, or -1 if invalid/truncated). */
int mn_pad_encode_create(mn_pad_profile_t profile, char *buf, size_t cap);
int mn_pad_encode_ev_diff(mn_pad_profile_t profile,
                          const mn_pad_ev_state_t *prev,
                          const mn_pad_ev_state_t *next,
                          char *buf,
                          size_t cap);
int mn_pad_encode_remove(char *buf, size_t cap);
int mn_pad_encode_hid_create(uint16_t bus, uint16_t vid, uint16_t pid, uint16_t ver,
                             uint8_t country, const uint8_t *desc, size_t desc_len,
                             const char *name, char *buf, size_t cap);
int mn_pad_encode_hid_input(const uint8_t *report, size_t len, char *buf, size_t cap);
int mn_pad_encode_hid_get_reply(uint32_t req_id, uint16_t err,
                                const uint8_t *data, size_t len,
                                char *buf, size_t cap);
int mn_pad_encode_hid_set_reply(uint32_t req_id, uint16_t err, char *buf, size_t cap);

/* Per-slot TX queue with 256 KiB cap, latest[256] hid-input coalescing, and control barriers. */
typedef struct {
    char  *line;
    size_t len;
    bool   occupied;
} mn_pad_hid_latest_t;

typedef struct {
    uint8_t            *pending;
    size_t              pending_len;
    size_t              pending_off;
    size_t              pending_cap;
    mn_pad_hid_latest_t latest[256];
    uint8_t             fifo[256];
    size_t              fifo_len;
} mn_pad_txq_t;

void    mn_pad_txq_init(mn_pad_txq_t *q);
void    mn_pad_txq_destroy(mn_pad_txq_t *q);
void    mn_pad_txq_clear(mn_pad_txq_t *q);
size_t  mn_pad_txq_bytes(const mn_pad_txq_t *q);
int     mn_pad_txq_push_control(mn_pad_txq_t *q, const char *line, size_t len);
int     mn_pad_txq_push_hid_input(mn_pad_txq_t *q, uint8_t report_id, const char *line, size_t len);
void    mn_pad_txq_promote_all(mn_pad_txq_t *q);
ssize_t mn_pad_txq_flush_fd(mn_pad_txq_t *q, int fd, int *out_errno);
bool    mn_pad_txq_has_pending(const mn_pad_txq_t *q);

#ifdef __cplusplus
}
#endif

#endif // MN_GAMEPAD_H
