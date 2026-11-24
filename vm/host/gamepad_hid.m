// IOHIDManager raw HID passthrough and IOHIDLib report-buffer lifetime management (clean-room MIT).
#import "gamepad_hid.h"
#import <CoreFoundation/CoreFoundation.h>
#import <Dispatch/Dispatch.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/hid/IOHIDKeys.h>
#import <IOKit/hid/IOHIDLib.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDUsageTables.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !__has_feature(objc_arc)
#define MN_DISPATCH_RELEASE(obj) do { if ((obj) != nil) { dispatch_release((obj)); (obj) = nil; } } while (0)
#else
#define MN_DISPATCH_RELEASE(obj) do { (obj) = nil; } while (0)
#endif

#define MN_HID_MAX_DEVICES 32
#define MN_HID_GRACE_NS    (2ULL * NSEC_PER_SEC)
#define MN_HID_TCC_POLL_NS (3ULL * NSEC_PER_SEC)

struct mn_hid_device {
    void *raw_ref;
    const mn_hid_backend_ops_t *ops;
    mn_hid_dev_info_t info;
    uint8_t *input_buf;
    size_t input_cap;
    int owner_slot;
    mn_hid_input_cb on_input;
    void *on_input_ctx;
    bool dualsense_suppressed;
};

typedef struct {
    bool in_use;
    IOHIDDeviceRef ref;
    mn_hid_dev_info_t info;
    struct mn_hid_device *dev;
    bool waiting_grace;
    uint64_t grace_gen;
} mn_hid_slot_entry_t;

static struct {
    bool running;
    IOHIDManagerRef mgr;
    dispatch_queue_t report_queue;
    mn_hid_callbacks_t cbs;
    void *cb_ctx;
    mn_hid_slot_entry_t entries[MN_HID_MAX_DEVICES];
    uint64_t next_grace_gen;
    uint64_t session_gen;
    bool tcc_logged;
    bool tcc_requested;
    bool tcc_recreated;
    dispatch_source_t tcc_timer;
} g_hid;

static void real_input_report_trampoline(void *context, IOReturn result, void *sender,
                                         IOHIDReportType type, uint32_t reportID,
                                         uint8_t *report, CFIndex reportLength) {
    (void)sender;
    (void)type;
    (void)reportID;
    if (result != kIOReturnSuccess || !context || !report || reportLength <= 0) {
        return;
    }
    struct mn_hid_device *dev = (struct mn_hid_device *)context;
    mn_hid_device_deliver_input(dev, report, (size_t)reportLength);
}

static int iohid_open_dev(void *raw_ref) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    return (int)IOHIDDeviceOpen(ref, kIOHIDOptionsTypeNone);
}

static void iohid_schedule(void *raw_ref) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    IOHIDDeviceScheduleWithRunLoop(ref, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
}

static void iohid_register_input_cb(void *raw_ref, uint8_t *buf, size_t len, void *dev_ctx) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    IOHIDDeviceRegisterInputReportCallback(ref, buf, (CFIndex)len,
                                           real_input_report_trampoline, dev_ctx);
}

static void iohid_unschedule(void *raw_ref) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    IOHIDDeviceUnscheduleFromRunLoop(ref, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
}

static void iohid_close_dev(void *raw_ref) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    IOHIDDeviceClose(ref, kIOHIDOptionsTypeNone);
}

static void iohid_release_dev(void *raw_ref) {
    IOHIDDeviceRef ref = (IOHIDDeviceRef)raw_ref;
    if (ref) {
        CFRelease(ref);
    }
}

static const mn_hid_backend_ops_t g_real_hid_ops = {
    .open_dev = iohid_open_dev,
    .schedule = iohid_schedule,
    .register_input_cb = iohid_register_input_cb,
    .unschedule = iohid_unschedule,
    .close_dev = iohid_close_dev,
    .release_dev = iohid_release_dev,
};

bool mn_hid_desc_uses_report_ids(const uint8_t *desc, size_t len) {
    if (!desc || len == 0) {
        return false;
    }
    size_t i = 0;
    while (i < len) {
        uint8_t b = desc[i];
        if (b == 0xfe) {
            /* Long item: size in byte i+1, total 3 + data_len. */
            if (i + 2 >= len) {
                break;
            }
            i += 3u + (size_t)desc[i + 1];
            continue;
        }
        uint8_t size_code = b & 0x03u;
        size_t data_len = (size_code == 3u) ? 4u : (size_t)size_code;
        if ((b & 0xfcu) == 0x84u) {
            return true;
        }
        i += 1u + data_len;
    }
    return false;
}

size_t mn_hid_get_report_buf_size(const mn_hid_dev_info_t *info, mn_hid_report_type_t type) {
    if (!info) {
        return 64u;
    }
    size_t base = 0;
    switch (type) {
    case MN_HID_REPORT_INPUT:   base = info->max_input; break;
    case MN_HID_REPORT_OUTPUT:  base = info->max_output; break;
    case MN_HID_REPORT_FEATURE: base = info->max_feature; break;
    }
    if (base == 0) {
        base = 64u;
    }
    if (info->is_ble_steam && type == MN_HID_REPORT_FEATURE) {
        base += 64u;
    }
    return base;
}

void mn_hid_normalize_name(const char *src, char *dst, size_t cap) {
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c)) {
            dst[j++] = (char)tolower(c);
        }
    }
    dst[j] = '\0';
}

struct mn_hid_device *mn_hid_device_create_with_ops(
    void *raw_ref,
    const mn_hid_dev_info_t *info,
    const mn_hid_backend_ops_t *ops,
    int *out_err) {
    if (out_err) {
        *out_err = 0;
    }
    if (!raw_ref || !info || !ops) {
        if (out_err) *out_err = -EINVAL;
        return NULL;
    }
    size_t cap = info->max_input > 0 ? info->max_input : 64u;
    uint8_t *buf = (uint8_t *)calloc(1, cap);
    if (!buf) {
        if (out_err) *out_err = -ENOMEM;
        return NULL;
    }
    struct mn_hid_device *dev = (struct mn_hid_device *)calloc(1, sizeof(*dev));
    if (!dev) {
        free(buf);
        if (out_err) *out_err = -ENOMEM;
        return NULL;
    }
    dev->raw_ref = raw_ref;
    dev->ops = ops;
    dev->info = *info;
    dev->input_buf = buf;
    dev->input_cap = cap;
    dev->owner_slot = -1;

    int err = ops->open_dev ? ops->open_dev(raw_ref) : 0;
    if (err != 0) {
        if (out_err) *out_err = err;
        free(dev->input_buf);
        free(dev);
        return NULL;
    }
    if (ops->schedule) {
        ops->schedule(raw_ref);
    }
    /* IOHIDLib report-buffer lifetime invariant: register input_buf ONCE while device is open; never unregister/free until teardown. */
    if (ops->register_input_cb) {
        ops->register_input_cb(raw_ref, dev->input_buf, dev->input_cap, dev);
    }
    return dev;
}

void mn_hid_device_set_active(struct mn_hid_device *dev, int owner_slot,
                              mn_hid_input_cb on_input, void *ctx) {
    if (!dev) {
        return;
    }
    dev->owner_slot = (on_input && owner_slot >= 0) ? owner_slot : -1;
    dev->on_input = (dev->owner_slot >= 0) ? on_input : NULL;
    dev->on_input_ctx = (dev->owner_slot >= 0) ? ctx : NULL;
}

void mn_hid_device_deliver_input(struct mn_hid_device *dev, const uint8_t *report, size_t len) {
    if (!dev || !report || len == 0) {
        return;
    }
    if (dev->on_input && dev->owner_slot >= 0) {
        dev->on_input(dev->owner_slot, report, len, dev->on_input_ctx);
    }
}

void mn_hid_device_destroy(struct mn_hid_device *dev) {
    if (!dev) {
        return;
    }
    dev->on_input = NULL;
    dev->on_input_ctx = NULL;
    dev->owner_slot = -1;

    if (dev->ops) {
        if (dev->ops->unschedule) {
            dev->ops->unschedule(dev->raw_ref);
        }
        if (dev->ops->close_dev) {
            dev->ops->close_dev(dev->raw_ref);
        }
        if (dev->ops->release_dev) {
            dev->ops->release_dev(dev->raw_ref);
        }
    }
    /* IOHIDLib report-buffer lifetime invariant: input_buf is freed ONLY after unschedule + close + release. */
    free(dev->input_buf);
    dev->input_buf = NULL;
    dev->input_cap = 0;
    free(dev);
}

const uint8_t *mn_hid_device_input_buf(const struct mn_hid_device *dev, size_t *out_cap) {
    if (out_cap) {
        *out_cap = dev ? dev->input_cap : 0;
    }
    return dev ? dev->input_buf : NULL;
}

bool mn_hid_device_is_active(const struct mn_hid_device *dev, int *out_slot) {
    bool active = dev && dev->on_input != NULL && dev->owner_slot >= 0;
    if (out_slot) {
        *out_slot = active ? dev->owner_slot : -1;
    }
    return active;
}

const mn_hid_dev_info_t *mn_hid_device_info(const struct mn_hid_device *dev) {
    return dev ? &dev->info : NULL;
}

/* --- Property inspection & classification --- */

static bool read_int_prop(IOHIDDeviceRef ref, CFStringRef key, int *out) {
    CFTypeRef val = IOHIDDeviceGetProperty(ref, key);
    if (!val || CFGetTypeID(val) != CFNumberGetTypeID()) {
        return false;
    }
    return CFNumberGetValue((CFNumberRef)val, kCFNumberIntType, out);
}

static bool read_usb_iface(IOHIDDeviceRef ref, int *out_iface) {
    if (read_int_prop(ref, CFSTR("bInterfaceNumber"), out_iface)) {
        return true;
    }
    io_service_t svc = IOHIDDeviceGetService(ref);
    if (svc == IO_OBJECT_NULL) {
        return false;
    }
    CFTypeRef val = IORegistryEntrySearchCFProperty(
        svc, kIOServicePlane, CFSTR("bInterfaceNumber"),
        kCFAllocatorDefault, kIORegistryIterateParents | kIORegistryIterateRecursively);
    if (!val) {
        return false;
    }
    bool ok = (CFGetTypeID(val) == CFNumberGetTypeID()) &&
              CFNumberGetValue((CFNumberRef)val, kCFNumberIntType, out_iface);
    CFRelease(val);
    return ok;
}

static bool read_str_prop(IOHIDDeviceRef ref, CFStringRef key, char *buf, size_t cap) {
    if (!buf || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    CFTypeRef val = IOHIDDeviceGetProperty(ref, key);
    if (!val || CFGetTypeID(val) != CFStringGetTypeID()) {
        return false;
    }
    return CFStringGetCString((CFStringRef)val, buf, (CFIndex)cap, kCFStringEncodingUTF8);
}

static bool is_usage_gamepad_or_joystick(IOHIDDeviceRef ref) {
    int page = 0, usage = 0;
    bool has_primary = read_int_prop(ref, CFSTR(kIOHIDPrimaryUsagePageKey), &page) &&
                       read_int_prop(ref, CFSTR(kIOHIDPrimaryUsageKey), &usage);
    if (has_primary && page == kHIDPage_GenericDesktop && usage == kHIDUsage_GD_MultiAxisController) {
        return false; /* Exclude Usage=8 (SpaceMouse / multi-axis). */
    }
    bool matched = has_primary && page == kHIDPage_GenericDesktop &&
                   (usage == kHIDUsage_GD_Joystick || usage == kHIDUsage_GD_GamePad);

    CFTypeRef pairs = IOHIDDeviceGetProperty(ref, CFSTR(kIOHIDDeviceUsagePairsKey));
    if (pairs && CFGetTypeID(pairs) == CFArrayGetTypeID()) {
        CFArrayRef arr = (CFArrayRef)pairs;
        CFIndex n = CFArrayGetCount(arr);
        for (CFIndex i = 0; i < n; i++) {
            CFTypeRef elem = CFArrayGetValueAtIndex(arr, i);
            if (!elem || CFGetTypeID(elem) != CFDictionaryGetTypeID()) {
                continue;
            }
            CFDictionaryRef dict = (CFDictionaryRef)elem;
            CFTypeRef p = CFDictionaryGetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey));
            CFTypeRef u = CFDictionaryGetValue(dict, CFSTR(kIOHIDDeviceUsageKey));
            int dp = 0, du = 0;
            if (p && u && CFGetTypeID(p) == CFNumberGetTypeID() && CFGetTypeID(u) == CFNumberGetTypeID() &&
                CFNumberGetValue((CFNumberRef)p, kCFNumberIntType, &dp) &&
                CFNumberGetValue((CFNumberRef)u, kCFNumberIntType, &du) &&
                dp == kHIDPage_GenericDesktop) {
                if (du == kHIDUsage_GD_MultiAxisController) {
                    return false;
                }
                if (du == kHIDUsage_GD_Joystick || du == kHIDUsage_GD_GamePad) {
                    matched = true;
                }
            }
        }
    }
    return matched;
}

static bool classify_device(IOHIDDeviceRef ref, mn_hid_dev_info_t *out) {
    memset(out, 0, sizeof(*out));
    int vid = 0, pid = 0, ver = 0, country = 0;
    if (!read_int_prop(ref, CFSTR(kIOHIDVendorIDKey), &vid) ||
        !read_int_prop(ref, CFSTR(kIOHIDProductIDKey), &pid)) {
        return false;
    }
    (void)read_int_prop(ref, CFSTR(kIOHIDVersionNumberKey), &ver);
    (void)read_int_prop(ref, CFSTR(kIOHIDCountryCodeKey), &country);

    uint16_t uvid = (uint16_t)vid;
    uint16_t upid = (uint16_t)pid;
    uint16_t mapped_pid = upid;
    mn_hid_kind_t kind = MN_HID_KIND_NONE;
    bool is_ble_steam = false;

    if (uvid == 0x054cu && (upid == 0x0ce6u || upid == 0x0df2u)) {
        kind = MN_HID_KIND_DUALSENSE;
    } else if (uvid == 0x28deu && upid == 0x1302u) {
        kind = MN_HID_KIND_STEAM;
    } else if (uvid == 0x28deu && upid == 0x1303u) {
        kind = MN_HID_KIND_STEAM;
        is_ble_steam = true;
    } else if (uvid == 0x28deu && upid == 0x1304u) {
        int iface = -1;
        if (!read_usb_iface(ref, &iface) || iface < 2 || iface > 5) {
            return false;
        }
        kind = MN_HID_KIND_STEAM;
        mapped_pid = 0x1302u;
    } else if (is_usage_gamepad_or_joystick(ref)) {
        kind = MN_HID_KIND_GENERIC;
    } else {
        return false;
    }

    CFTypeRef desc_val = IOHIDDeviceGetProperty(ref, CFSTR(kIOHIDReportDescriptorKey));
    if (!desc_val || CFGetTypeID(desc_val) != CFDataGetTypeID()) {
        return false;
    }
    CFDataRef desc_data = (CFDataRef)desc_val;
    CFIndex desc_len = CFDataGetLength(desc_data);
    if (desc_len <= 0 || (size_t)desc_len > MN_HID_MAX_DESC_BYTES) {
        return false;
    }
    CFDataGetBytes(desc_data, CFRangeMake(0, desc_len), out->desc);
    out->desc_len = (size_t)desc_len;
    out->uses_report_ids = mn_hid_desc_uses_report_ids(out->desc, out->desc_len);

    int max_in = 64, max_out = 64, max_feat = 64;
    (void)read_int_prop(ref, CFSTR(kIOHIDMaxInputReportSizeKey), &max_in);
    (void)read_int_prop(ref, CFSTR(kIOHIDMaxOutputReportSizeKey), &max_out);
    (void)read_int_prop(ref, CFSTR(kIOHIDMaxFeatureReportSizeKey), &max_feat);

    char transport[64] = {0};
    (void)read_str_prop(ref, CFSTR(kIOHIDTransportKey), transport, sizeof(transport));
    bool is_bt = (strcasecmp(transport, "Bluetooth") == 0 ||
                  strcasecmp(transport, "BluetoothLowEnergy") == 0);

    out->kind = kind;
    out->bus = is_bt ? 0x0005u : 0x0003u;
    out->vid = uvid;
    out->pid = mapped_pid;
    out->ver = (uint16_t)ver;
    out->country = (uint8_t)country;
    out->is_ble_steam = is_ble_steam;
    out->max_input = (max_in > 0) ? (size_t)max_in : 64u;
    out->max_output = (max_out > 0) ? (size_t)max_out : 64u;
    out->max_feature = (max_feat > 0) ? (size_t)max_feat : 64u;

    if (!read_str_prop(ref, CFSTR(kIOHIDProductKey), out->name, sizeof(out->name)) ||
        out->name[0] == '\0') {
        snprintf(out->name, sizeof(out->name), "HID Gamepad %04x:%04x", out->vid, out->pid);
    }
    return true;
}

static bool gccontroller_claims_hid(IOHIDDeviceRef ref, const mn_hid_dev_info_t *info) {
    if (![GCController supportsHIDDevice:ref]) {
        return false;
    }
    char hid_norm[128];
    mn_hid_normalize_name(info->name, hid_norm, sizeof(hid_norm));

    for (GCController *c in [GCController controllers]) {
        char vn[128] = {0}, pc[128] = {0};
        if (c.vendorName != nil) {
            mn_hid_normalize_name([c.vendorName UTF8String], vn, sizeof(vn));
        }
        if (c.productCategory != nil) {
            mn_hid_normalize_name([c.productCategory UTF8String], pc, sizeof(pc));
        }
        if (hid_norm[0] != '\0' && vn[0] != '\0' &&
            (strstr(vn, hid_norm) != NULL || strstr(hid_norm, vn) != NULL)) {
            return true;
        }
        if (hid_norm[0] != '\0' && pc[0] != '\0' &&
            (strstr(pc, hid_norm) != NULL || strstr(hid_norm, pc) != NULL)) {
            return true;
        }
    }
    return false;
}

static int dualsense_count(void) {
    int count = 0;
    for (int i = 0; i < MN_HID_MAX_DEVICES; i++) {
        if (g_hid.entries[i].in_use && g_hid.entries[i].info.kind == MN_HID_KIND_DUALSENSE) {
            count++;
        }
    }
    return count;
}

static bool dualsense_hid_enabled(void) {
    const char *env = getenv("MN_GAMEPAD_HID_DUALSENSE");
    if (env && strcmp(env, "0") == 0) {
        return false;
    }
    return dualsense_count() == 1;
}

static void start_tcc_poll(void);
static int setup_hid_manager(void);
static void teardown_hid_manager(void);

static void handle_tcc_not_permitted(void) {
    if (!g_hid.tcc_logged) {
        g_hid.tcc_logged = true;
        fprintf(stderr, "macneutron-vm: Input Monitoring permission required for HID gamepad\n");
    }
    if (!g_hid.tcc_requested) {
        g_hid.tcc_requested = true;
        (void)IOHIDRequestAccess(kIOHIDRequestTypeListenEvent);
        start_tcc_poll();
    }
}

static void start_tcc_poll(void) {
    if (g_hid.tcc_timer != nil || g_hid.tcc_recreated) {
        return;
    }
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    if (timer == nil) {
        return;
    }
    dispatch_source_set_timer(timer,
                              dispatch_time(DISPATCH_TIME_NOW, MN_HID_TCC_POLL_NS),
                              MN_HID_TCC_POLL_NS,
                              500u * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(timer, ^{
        if (!g_hid.running || g_hid.tcc_recreated) {
            return;
        }
        if (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted) {
            g_hid.tcc_recreated = true;
            if (g_hid.tcc_timer != nil) {
                dispatch_source_cancel(g_hid.tcc_timer);
                MN_DISPATCH_RELEASE(g_hid.tcc_timer);
            }
            teardown_hid_manager();
            (void)setup_hid_manager();
        }
    });
    g_hid.tcc_timer = timer;
    dispatch_resume(timer);
}

static void open_entry_if_eligible(mn_hid_slot_entry_t *e) {
    if (!g_hid.running || !e->in_use || e->dev != NULL || e->waiting_grace) {
        return;
    }
    if (e->info.kind == MN_HID_KIND_DUALSENSE && !dualsense_hid_enabled()) {
        return;
    }
    CFRetain(e->ref);
    int err = 0;
    struct mn_hid_device *dev = mn_hid_device_create_with_ops(e->ref, &e->info, &g_real_hid_ops, &err);
    if (!dev) {
        CFRelease(e->ref);
        if (err == (int)kIOReturnNotPermitted) {
            handle_tcc_not_permitted();
        }
        return;
    }
    e->dev = dev;
    if (g_hid.cbs.on_ready) {
        g_hid.cbs.on_ready(dev, &dev->info, g_hid.cb_ctx);
    }
}

static void sync_dualsense_policy(void) {
    bool allow = dualsense_hid_enabled();
    for (int i = 0; i < MN_HID_MAX_DEVICES; i++) {
        mn_hid_slot_entry_t *e = &g_hid.entries[i];
        if (!e->in_use || e->info.kind != MN_HID_KIND_DUALSENSE) {
            continue;
        }
        if (!allow && e->dev != NULL) {
            if (!e->dev->dualsense_suppressed) {
                e->dev->dualsense_suppressed = true;
                if (g_hid.cbs.on_gone) {
                    g_hid.cbs.on_gone(e->dev, &e->dev->info, g_hid.cb_ctx);
                }
            }
        } else if (allow) {
            if (e->dev == NULL) {
                open_entry_if_eligible(e);
            } else if (e->dev->dualsense_suppressed) {
                e->dev->dualsense_suppressed = false;
                if (g_hid.cbs.on_ready) {
                    g_hid.cbs.on_ready(e->dev, &e->dev->info, g_hid.cb_ctx);
                }
            }
        }
    }
}

static int find_entry_by_ref(IOHIDDeviceRef ref) {
    for (int i = 0; i < MN_HID_MAX_DEVICES; i++) {
        if (g_hid.entries[i].in_use && g_hid.entries[i].ref == ref) {
            return i;
        }
    }
    return -1;
}

static void on_hid_matched(void *context, IOReturn result, void *sender, IOHIDDeviceRef ref) {
    (void)context;
    (void)sender;
    if (!g_hid.running || result != kIOReturnSuccess || !ref) {
        return;
    }
    if (find_entry_by_ref(ref) >= 0) {
        return;
    }
    mn_hid_dev_info_t info;
    if (!classify_device(ref, &info)) {
        return;
    }
    int free_idx = -1;
    for (int i = 0; i < MN_HID_MAX_DEVICES; i++) {
        if (!g_hid.entries[i].in_use) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        return;
    }

    mn_hid_slot_entry_t *e = &g_hid.entries[free_idx];
    memset(e, 0, sizeof(*e));
    e->in_use = true;
    e->ref = (IOHIDDeviceRef)CFRetain(ref);
    e->info = info;

    if (info.kind == MN_HID_KIND_DUALSENSE) {
        sync_dualsense_policy();
        return;
    }
    if (info.kind == MN_HID_KIND_GENERIC && [GCController supportsHIDDevice:ref]) {
        e->waiting_grace = true;
        uint64_t gen = ++g_hid.next_grace_gen;
        e->grace_gen = gen;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, MN_HID_GRACE_NS),
                       dispatch_get_main_queue(), ^{
            if (!g_hid.running || !e->in_use || e->grace_gen != gen) {
                return;
            }
            e->waiting_grace = false;
            if (gccontroller_claims_hid(e->ref, &e->info)) {
                return; /* Yield to GCController without ever opening the HID device. */
            }
            open_entry_if_eligible(e);
        });
        return;
    }
    open_entry_if_eligible(e);
}

static void remove_entry(int idx) {
    mn_hid_slot_entry_t *e = &g_hid.entries[idx];
    if (!e->in_use) {
        return;
    }
    mn_hid_kind_t kind = e->info.kind;
    e->waiting_grace = false;
    e->grace_gen++;
    if (e->dev) {
        struct mn_hid_device *dev = e->dev;
        e->dev = NULL;
        if (g_hid.cbs.on_gone && !dev->dualsense_suppressed) {
            g_hid.cbs.on_gone(dev, &dev->info, g_hid.cb_ctx);
        }
        mn_hid_device_destroy(dev);
    }
    if (e->ref) {
        CFRelease(e->ref);
        e->ref = NULL;
    }
    e->in_use = false;
    if (kind == MN_HID_KIND_DUALSENSE) {
        sync_dualsense_policy();
    }
}

static void on_hid_removed(void *context, IOReturn result, void *sender, IOHIDDeviceRef ref) {
    (void)context;
    (void)result;
    (void)sender;
    if (!ref) {
        return;
    }
    int idx = find_entry_by_ref(ref);
    if (idx >= 0) {
        remove_entry(idx);
    }
}

static int setup_hid_manager(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionIndependentDevices);
    if (!mgr) {
        return -1;
    }
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    IOHIDManagerRegisterDeviceMatchingCallback(mgr, on_hid_matched, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(mgr, on_hid_removed, NULL);
    IOHIDManagerScheduleWithRunLoop(mgr, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    IOReturn kr = IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);
    if (kr != kIOReturnSuccess) {
        IOHIDManagerUnscheduleFromRunLoop(mgr, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        CFRelease(mgr);
        if (kr == kIOReturnNotPermitted) {
            handle_tcc_not_permitted();
        }
        return -1;
    }
    g_hid.mgr = mgr;
    return 0;
}

static void teardown_hid_manager(void) {
    for (int i = 0; i < MN_HID_MAX_DEVICES; i++) {
        if (g_hid.entries[i].in_use) {
            remove_entry(i);
        }
    }
    if (g_hid.mgr) {
        IOHIDManagerUnscheduleFromRunLoop(g_hid.mgr, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        IOHIDManagerClose(g_hid.mgr, kIOHIDOptionsTypeNone);
        CFRelease(g_hid.mgr);
        g_hid.mgr = NULL;
    }
}

int mn_hid_init(const mn_hid_callbacks_t *cbs, void *ctx) {
    if (g_hid.running) {
        mn_hid_shutdown();
    }
    g_hid.running = false;
    g_hid.session_gen++;
    g_hid.mgr = NULL;
    g_hid.report_queue = nil;
    g_hid.tcc_timer = nil;
    memset(g_hid.entries, 0, sizeof(g_hid.entries));
    g_hid.tcc_logged = false;
    g_hid.tcc_requested = false;
    g_hid.tcc_recreated = false;
    if (cbs) {
        g_hid.cbs = *cbs;
    } else {
        memset(&g_hid.cbs, 0, sizeof(g_hid.cbs));
    }
    g_hid.cb_ctx = ctx;
    g_hid.report_queue = dispatch_queue_create("io.macneutron.gamepad.hid.report", DISPATCH_QUEUE_SERIAL);
    g_hid.running = true;
    return setup_hid_manager();
}

void mn_hid_shutdown(void) {
    g_hid.running = false;
    g_hid.session_gen++;
    if (g_hid.tcc_timer != nil) {
        dispatch_source_cancel(g_hid.tcc_timer);
        MN_DISPATCH_RELEASE(g_hid.tcc_timer);
    }
    teardown_hid_manager();
    MN_DISPATCH_RELEASE(g_hid.report_queue);
    memset(&g_hid.cbs, 0, sizeof(g_hid.cbs));
    g_hid.cb_ctx = NULL;
}

void mn_hid_on_gccontroller_changed(void) {
    /* Grace timers inspect [GCController controllers] on expiry; nothing extra required here. */
}

static IOHIDReportType to_iohid_report_type(mn_hid_report_type_t t) {
    switch (t) {
    case MN_HID_REPORT_INPUT:   return kIOHIDReportTypeInput;
    case MN_HID_REPORT_OUTPUT:  return kIOHIDReportTypeOutput;
    case MN_HID_REPORT_FEATURE: return kIOHIDReportTypeFeature;
    }
    return kIOHIDReportTypeFeature;
}

void mn_hid_get_report(struct mn_hid_device *dev, int slot, uint32_t req_id,
                       mn_hid_report_type_t rtype, uint8_t rnum) {
    if (!dev || !dev->raw_ref || !g_hid.running || !g_hid.report_queue) {
        if (g_hid.cbs.on_get_reply) {
            g_hid.cbs.on_get_reply(slot, req_id, ENODEV, NULL, 0, g_hid.cb_ctx);
        }
        return;
    }
    /* Capture CFRetain(ref) ONLY, never the main-thread mn_hid_device pointer. */
    IOHIDDeviceRef ref = (IOHIDDeviceRef)CFRetain((IOHIDDeviceRef)dev->raw_ref);
    size_t buf_sz = mn_hid_get_report_buf_size(&dev->info, rtype);
    IOHIDReportType io_type = to_iohid_report_type(rtype);
    mn_hid_get_reply_cb reply_cb = g_hid.cbs.on_get_reply;
    void *cb_ctx = g_hid.cb_ctx;
    uint64_t sess_gen = g_hid.session_gen;

    dispatch_async(g_hid.report_queue, ^{
        uint8_t *buf = (uint8_t *)calloc(1, buf_sz);
        uint16_t err = EIO;
        size_t out_len = 0;
        if (buf) {
            CFIndex len = (CFIndex)buf_sz;
            IOReturn kr = IOHIDDeviceGetReport(ref, io_type, (CFIndex)rnum, buf, &len);
            if (kr == kIOReturnSuccess && len >= 0) {
                err = 0;
                out_len = (size_t)len;
                if (out_len > buf_sz) {
                    out_len = buf_sz;
                }
                if (out_len > MN_HID_MAX_DESC_BYTES) {
                    out_len = MN_HID_MAX_DESC_BYTES;
                }
            }
        } else {
            err = ENOMEM;
        }
        CFRelease(ref);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (reply_cb && g_hid.running && g_hid.session_gen == sess_gen) {
                reply_cb(slot, req_id, err, buf, out_len, cb_ctx);
            }
            free(buf);
        });
    });
}

void mn_hid_set_report(struct mn_hid_device *dev, int slot, uint32_t req_id,
                       mn_hid_report_type_t rtype, const uint8_t *data, size_t len) {
    if (!dev || !dev->raw_ref || !data || len == 0 || !g_hid.running || !g_hid.report_queue) {
        if (g_hid.cbs.on_set_reply) {
            g_hid.cbs.on_set_reply(slot, req_id, ENODEV, g_hid.cb_ctx);
        }
        return;
    }
    IOHIDDeviceRef ref = (IOHIDDeviceRef)CFRetain((IOHIDDeviceRef)dev->raw_ref);
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) {
        CFRelease(ref);
        if (g_hid.cbs.on_set_reply) {
            g_hid.cbs.on_set_reply(slot, req_id, ENOMEM, g_hid.cb_ctx);
        }
        return;
    }
    memcpy(copy, data, len);
    CFIndex rnum = dev->info.uses_report_ids ? (CFIndex)copy[0] : 0;
    IOHIDReportType io_type = to_iohid_report_type(rtype);
    mn_hid_set_reply_cb reply_cb = g_hid.cbs.on_set_reply;
    void *cb_ctx = g_hid.cb_ctx;
    uint64_t sess_gen = g_hid.session_gen;

    dispatch_async(g_hid.report_queue, ^{
        IOReturn kr = IOHIDDeviceSetReport(ref, io_type, rnum, copy, (CFIndex)len);
        uint16_t err = (kr == kIOReturnSuccess) ? 0 : EIO;
        CFRelease(ref);
        free(copy);
        dispatch_async(dispatch_get_main_queue(), ^{
            if (reply_cb && g_hid.running && g_hid.session_gen == sess_gen) {
                reply_cb(slot, req_id, err, cb_ctx);
            }
        });
    });
}

void mn_hid_output_report(struct mn_hid_device *dev, mn_hid_report_type_t rtype,
                          const uint8_t *data, size_t len) {
    if (!dev || !dev->raw_ref || !data || len == 0 || !g_hid.running || !g_hid.report_queue) {
        return;
    }
    IOHIDDeviceRef ref = (IOHIDDeviceRef)CFRetain((IOHIDDeviceRef)dev->raw_ref);
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) {
        CFRelease(ref);
        return;
    }
    memcpy(copy, data, len);
    CFIndex rnum = dev->info.uses_report_ids ? (CFIndex)copy[0] : 0;
    IOHIDReportType io_type = to_iohid_report_type(rtype);

    dispatch_async(g_hid.report_queue, ^{
        (void)IOHIDDeviceSetReport(ref, io_type, rnum, copy, (CFIndex)len);
        CFRelease(ref);
        free(copy);
    });
}
