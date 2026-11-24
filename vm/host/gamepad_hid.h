// Clean-room MIT IOHIDManager raw HID passthrough with IOHIDLib report-buffer lifetime safety.
#ifndef MN_GAMEPAD_HID_H
#define MN_GAMEPAD_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MN_HID_MAX_DESC_BYTES 4096u

typedef enum {
    MN_HID_KIND_NONE = 0,
    MN_HID_KIND_DUALSENSE = 1,
    MN_HID_KIND_STEAM = 2,
    MN_HID_KIND_GENERIC = 3,
} mn_hid_kind_t;

typedef enum {
    MN_HID_REPORT_INPUT = 0,
    MN_HID_REPORT_OUTPUT = 1,
    MN_HID_REPORT_FEATURE = 2,
} mn_hid_report_type_t;

typedef struct {
    mn_hid_kind_t kind;
    uint16_t bus;
    uint16_t vid;
    uint16_t pid;
    uint16_t ver;
    uint8_t country;
    bool is_ble_steam;
    bool uses_report_ids;
    size_t max_input;
    size_t max_output;
    size_t max_feature;
    uint8_t desc[MN_HID_MAX_DESC_BYTES];
    size_t desc_len;
    char name[128];
} mn_hid_dev_info_t;

struct mn_hid_device;

typedef void (*mn_hid_input_cb)(int slot, const uint8_t *report, size_t len, void *ctx);
typedef void (*mn_hid_get_reply_cb)(int slot, uint32_t req_id, uint16_t err,
                                    const uint8_t *data, size_t len, void *ctx);
typedef void (*mn_hid_set_reply_cb)(int slot, uint32_t req_id, uint16_t err, void *ctx);
typedef void (*mn_hid_device_cb)(struct mn_hid_device *dev, const mn_hid_dev_info_t *info, void *ctx);

typedef struct {
    mn_hid_device_cb    on_ready;
    mn_hid_device_cb    on_gone;
    mn_hid_get_reply_cb on_get_reply;
    mn_hid_set_reply_cb on_set_reply;
} mn_hid_callbacks_t;

/* Testable IOHIDLib report-buffer lifetime interface. */
typedef struct {
    int  (*open_dev)(void *raw_ref);
    void (*schedule)(void *raw_ref);
    void (*register_input_cb)(void *raw_ref, uint8_t *buf, size_t len, void *dev_ctx);
    void (*unschedule)(void *raw_ref);
    void (*close_dev)(void *raw_ref);
    void (*release_dev)(void *raw_ref);
} mn_hid_backend_ops_t;

struct mn_hid_device *mn_hid_device_create_with_ops(
    void *raw_ref,
    const mn_hid_dev_info_t *info,
    const mn_hid_backend_ops_t *ops,
    int *out_err);
void mn_hid_device_set_active(struct mn_hid_device *dev, int owner_slot,
                              mn_hid_input_cb on_input, void *ctx);
void mn_hid_device_deliver_input(struct mn_hid_device *dev, const uint8_t *report, size_t len);
void mn_hid_device_destroy(struct mn_hid_device *dev);
const uint8_t *mn_hid_device_input_buf(const struct mn_hid_device *dev, size_t *out_cap);
bool mn_hid_device_is_active(const struct mn_hid_device *dev, int *out_slot);
const mn_hid_dev_info_t *mn_hid_device_info(const struct mn_hid_device *dev);

/* Pure classification & descriptor helpers (exposed for unit tests). */
bool mn_hid_desc_uses_report_ids(const uint8_t *desc, size_t len);
size_t mn_hid_get_report_buf_size(const mn_hid_dev_info_t *info, mn_hid_report_type_t type);
void mn_hid_normalize_name(const char *src, char *dst, size_t cap);

/* Host IOHIDManager lifecycle and async report dispatch. */
int  mn_hid_init(const mn_hid_callbacks_t *cbs, void *ctx);
void mn_hid_shutdown(void);
void mn_hid_on_gccontroller_changed(void);

void mn_hid_get_report(struct mn_hid_device *dev, int slot, uint32_t req_id,
                       mn_hid_report_type_t rtype, uint8_t rnum);
void mn_hid_set_report(struct mn_hid_device *dev, int slot, uint32_t req_id,
                       mn_hid_report_type_t rtype, const uint8_t *data, size_t len);
void mn_hid_output_report(struct mn_hid_device *dev, mn_hid_report_type_t rtype,
                          const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // MN_GAMEPAD_HID_H
