/* wm_probe's g6 cases (slots.txt's g6; probe.h), recorded from DXMT's C: the HUD, layers and drawables, the views of
 * winemac (the stand-in) or the loose macdrv symbols (native: this file's), colour spaces, displays, EDR and the
 * display settings. Screen values are relations in fixed (the screen's own answers, read on main) and raw in full. */
#include "probe.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#include <objc/runtime.h>

/* ---- helpers ---- */

/* The 4-byte words of a struct of floats no longer the fill pattern, as byte ranges (a value's byte may be 0xA5). */
static void written32(const char *name, const void *p, size_t size) {
    const uint32_t *w = p;
    size_t n = size / 4;
    char s[160] = "";
    for (size_t i = 0; i < n;) {
        if (w[i] == 0xA5A5A5A5u) {
            i++;
            continue;
        }
        size_t j = i;
        while (j + 1 < n && w[j + 1] != 0xA5A5A5A5u) j++;
        snprintf(s + strlen(s), sizeof s - strlen(s), "%s%zu-%zu", *s ? "," : "", 4 * i, 4 * j + 3);
        i = j + 1;
    }
    wm_val(name, "%s", *s ? s : "none");
}

static NSScreen *screen_of(CGDirectDisplayID id) {
    __block NSScreen *found = nil;
    wm_on_main(^{
      for (NSScreen *s in NSScreen.screens)
          if ([s.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == id) found = s;
    });
    return found;
}

/* A screen's EDR values, read on main. */
struct edr {
    float max, potential, reference;
};
static struct edr screen_edr(NSScreen *s) {
    __block struct edr e = {0};
    wm_on_main(^{
      e.max = s.maximumExtendedDynamicRangeColorComponentValue;
      e.potential = s.maximumPotentialExtendedDynamicRangeColorComponentValue;
      e.reference = s.maximumReferenceExtendedDynamicRangeColorComponentValue;
    });
    return e;
}

static void set_wants_edr(CAMetalLayer *l, BOOL on) {
    wm_on_main(^{
      l.wantsExtendedDynamicRangeContent = on;
    });
}

static NSScreen *window_screen(struct wm_window w) {
    __block NSScreen *s;
    wm_on_main(^{
      s = w.window.screen;
    });
    return s;
}

/* The table's own entry, outside wm_call (no line, no stub): 99 as the observer of 100, on 100's line. */
static unsigned raw(unsigned slot, void *params) {
    unsigned (*const *t)(void *) = wm_sym("__wine_unix_call_funcs");
    return t ? t[slot](params) : 0;
}
static void setting_after(uint64_t display) {
    struct WMTHDRMetadata m;
    struct unixcall_query_display_setting q = {.display_id = display, .hdr_metadata = {.ptr = &m}};
    raw(99, &q);
    wm_val("after", "%d", q.ret);
}

static void update(uint64_t display, enum WMTColorSpace cs, const struct WMTHDRMetadata *m) {
    struct unixcall_update_display_setting u = {.display_id = display, .colorspace = cs, .hdr_metadata = {.ptr = m}};
    wm_call(100, &u);
    setting_after(display);
}

static void query(uint64_t display, const struct WMTHDRMetadata *expect) {
    struct WMTHDRMetadata m;
    wm_fill(&m, sizeof m);
    struct unixcall_query_display_setting q;
    wm_fill(&q, sizeof q);
    q.display_id = display, q.hdr_metadata.ptr = &m;
    wm_call(99, &q);
    wm_val("ret", "%u", (unsigned)*(uint8_t *)&q.ret);
    wm_val("colorspace", "%s", q.colorspace == (enum WMTColorSpace)WM_FILL64 ? "fill" : "set");
    if (q.colorspace != (enum WMTColorSpace)WM_FILL64) wm_val("cs", "%llu", (unsigned long long)q.colorspace);
    if (expect && !memcmp(&m, expect, sizeof m)) wm_val("metadata", "same");
    else wm_bytes("metadata", &m, sizeof m);
}

/* 101 on a layer: what it wrote; the EDR values against the screen's. */
static void query_layer(CAMetalLayer *layer, NSScreen *screen, bool wants, const struct WMTHDRMetadata *expect,
                        uint64_t expect_cs) {
    struct WMTHDRMetadata m;
    wm_fill(&m, sizeof m);
    struct unixcall_query_display_setting_for_layer q;
    wm_fill(&q, sizeof q);
    q.layer = (obj_handle_t)layer, q.hdr_metadata.ptr = &m;
    wm_call(101, &q);
    wm_val("version", "%s", q.version == WM_FILL64 ? "fill" : [NSString stringWithFormat:@"%llu", q.version].UTF8String);
    wm_val("colorspace", "%s", q.colorspace == (enum WMTColorSpace)WM_FILL64 ? "fill" : "set");
    if (q.colorspace != (enum WMTColorSpace)WM_FILL64)
        wm_rel("cs", q.colorspace == expect_cs, "window-screen's", "%llu", (unsigned long long)q.colorspace);
    if (expect && !memcmp(&m, expect, sizeof m)) wm_val("metadata", "window-screen's");
    else wm_bytes("metadata", &m, sizeof m);
    if (!screen) {
        wm_bytes("edr", &q.edr_value, sizeof q.edr_value);
        return;
    }
    struct edr e = screen_edr(screen);
    if (wants) wm_rel("max_edr", q.edr_value.maximum_edr_color_component_value == e.max, "screen", "%a",
                      q.edr_value.maximum_edr_color_component_value);
    else wm_val("max_edr", "%a", q.edr_value.maximum_edr_color_component_value);
    wm_rel("potential_edr", q.edr_value.maximum_potential_edr_color_component_value == e.potential, "screen", "%a",
           q.edr_value.maximum_potential_edr_color_component_value);
}

/* 95's answer as the C finds it: the first online display neither main nor a mirror (0: none); *others: how many. */
static uint64_t secondary_display(unsigned *others) {
    uint32_t n = 0;
    CGGetOnlineDisplayList(0, NULL, &n);
    CGDirectDisplayID ids[32];
    if (n > 32) n = 32;
    CGGetOnlineDisplayList(n, ids, &n);
    uint64_t first = 0;
    unsigned k = 0;
    for (uint32_t i = 0; i < n; i++)
        if (ids[i] != CGMainDisplayID() && CGDisplayMirrorsDisplay(ids[i]) == kCGNullDirectDisplay)
            if (!k++) first = ids[i];
    if (others) *others = k;
    return first;
}

static const struct WMTHDRMetadata M1 = {{34000, 16000}, {13250, 34500}, {7500, 3000}, {15635, 16450}, 10000000, 1, 1000, 400};
static const struct WMTHDRMetadata M2 = {{1, 2}, {3, 4}, {5, 6}, {7, 8}, 9, 10, 11, 12};
#define INVALID_DISPLAY 0xDEADBEEFu

/* ---- cases ---- */

/* 62 before anything loaded libMTLHud (Metal loads it only with MTL_HUD_ENABLED=1): no class, no instance (0). */
static void hud_absent(void) {
    struct unixcall_generic_obj_ret p;
    wm_fill(&p, sizeof p);
    wm_call(62, &p);
    wm_val("class", "%s", objc_lookUpClass("MTLHUDService") ? "present" : "absent");
    wm_h0("ret", p.ret);
}

/* libMTLHud loaded (as MTL_HUD_ENABLED=1 would): 62 is MTLHUDService's instance; 63-65 on a recorder with that class's
 * signatures: selectors and arguments. A child, so the rest of the run keeps no HUD. */
static void hud(void) {
    if (!dlopen("/usr/lib/libMTLHud.dylib", RTLD_NOW)) wm_line("no-libMTLHud %s", dlerror());
    struct unixcall_generic_obj_ret p;
    wm_fill(&p, sizeof p);
    wm_own("ret", &p.ret);
    wm_call(62, &p);
    Class c = objc_lookUpClass("MTLHUDService");
    if (p.ret && p.ret != WM_FILL64) wm_val("class", "%s", [(id)p.ret isKindOfClass:c] ? "MTLHUDService" : "other");
    id rec = wm_rec(c, nil);
    NSString *label = [NSMutableString stringWithString:@"wm-probe-label"];
    NSString *after = [NSMutableString stringWithString:@"wm-probe-after"];
    NSString *value = [NSMutableString stringWithString:@"wm-probe-value"];
    wm_in(label), wm_in(after), wm_in(value);
    struct unixcall_generic_obj_obj_obj_uint64_ret a;
    wm_fill(&a, sizeof a);
    a.handle = (obj_handle_t)rec, a.arg0 = (obj_handle_t)label, a.arg1 = (obj_handle_t)after;
    wm_call(63, &a);
    wm_val("ret", "%llu", (unsigned long long)a.ret);
    struct unixcall_generic_obj_obj_obj_noret u = {
        .handle = (obj_handle_t)rec, .arg0 = (obj_handle_t)label, .arg1 = (obj_handle_t)value};
    wm_call(64, &u);
    struct unixcall_generic_obj_obj_noret r = {.handle = (obj_handle_t)rec, .arg = (obj_handle_t)label};
    wm_call(65, &r);
    /* Each object argument as 0: sent on as nil. */
    wm_fill(&a, sizeof a);
    a.handle = (obj_handle_t)rec, a.arg0 = 0, a.arg1 = 0;
    wm_call(63, &a);
    u.arg0 = 0, u.arg1 = 0;
    wm_call(64, &u);
    r.arg = 0;
    wm_call(65, &r);
}

static struct WMTLayerProps layer_props(obj_handle_t device) {
    struct WMTLayerProps props;
    wm_fill(&props, sizeof props);
    props.device = device;
    props.contents_scale = 2.0, props.drawable_width = 640, props.drawable_height = 360;
    props.opaque = true, props.display_sync_enabled = false, props.framebuffer_only = false;
    props.pixel_format = WMTPixelFormatBGRX8Unorm_sRGB; /* the swizzle bits go: 81 */
    return props;
}

/* The bytes written past the device field (a pointer's byte can be 0xA5: the device is compared instead). */
static void print_props(const struct WMTLayerProps *o) {
    wm_bytes("written-past-device", (const char *)o + 8, sizeof *o - 8);
    wm_eq("device", o->device, (uint64_t)wm_device, "wm_device");
    wm_val("scale", "%a", o->contents_scale);
    wm_val("size", "%ax%a", o->drawable_width, o->drawable_height);
    wm_val("opaque", "%u", (unsigned)*(const uint8_t *)&o->opaque);
    wm_val("sync", "%u", (unsigned)*(const uint8_t *)&o->display_sync_enabled);
    wm_val("fb_only", "%u", (unsigned)*(const uint8_t *)&o->framebuffer_only);
    wm_val("pixel_format", "%u", (unsigned)o->pixel_format);
}

/* 70 from the worker on a recorder over a real layer (the sets run on main); 71 into a 0xA5 struct. */
static void layer_props_case(void) {
    CAMetalLayer *layer = [CAMetalLayer layer];
    id rec = wm_rec(layer, nil); /* no wm_in: the main thread's implicit CATransaction holds it a while */
    struct WMTLayerProps props = layer_props((obj_handle_t)wm_device);
    struct unixcall_generic_obj_constptr_noret s = {.handle = (obj_handle_t)rec, .arg = {.ptr = &props}};
    wm_call(70, &s);
    struct WMTLayerProps out;
    wm_fill(&out, sizeof out);
    struct unixcall_generic_obj_ptr_noret g = {.handle = (obj_handle_t)rec, .arg = {.ptr = &out}};
    wm_call(71, &g);
    print_props(&out);
}

/* 67 on a recorder over a real layer (exactly nextDrawable), then 66 twice: +0 each, and whether autoreleased. */
static void drawable(void) {
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = wm_device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake(64, 64);
    id rec = wm_rec(layer, nil);
    struct unixcall_generic_obj_obj_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)rec;
    wm_own("ret", &p.ret);
    wm_call(67, &p);
    if (!p.ret || p.ret == WM_FILL64) return;
    obj_handle_t d = p.ret;
    for (int i = 0; i < 2; i++) {
        wm_fill(&p, sizeof p);
        p.handle = d;
        wm_own("ret", &p.ret);
        wm_call(66, &p);
        if (p.ret && p.ret != WM_FILL64)
            wm_val("texture", "%s", p.ret == (obj_handle_t)[(id<CAMetalDrawable>)d texture] ? "drawable's" : "other");
    }
}

/* 72/73 through the winemac stand-in (its own window): a view (+1, 73 releases it) and its layer (+0). */
static void view(void) {
    struct unixcall_create_metal_view_from_hwnd p;
    wm_fill(&p, sizeof p);
    p.hwnd = 0x20002, p.device = (obj_handle_t)wm_device;
    wm_own("ret_view", &p.ret_view);
    wm_own("ret_layer", &p.ret_layer);
    wm_call(72, &p);
    if (!p.ret_view || p.ret_view == WM_FILL64 || !p.ret_layer || p.ret_layer == WM_FILL64) return;
    NSView *v = (id)p.ret_view;
    __block CALayer *vl;
    wm_on_main(^{
      vl = v.layer;
    });
    wm_val("layer", "%s", (obj_handle_t)vl == p.ret_layer ? "view's" : "other");
    wm_val("metal", "%d", [(id)p.ret_layer isKindOfClass:[CAMetalLayer class]]);
    wm_eq("device", (uint64_t)((CAMetalLayer *)p.ret_layer).device, (uint64_t)wm_device, "wm_device");
    wm_in(v);
    struct unixcall_generic_obj_noret r = {.handle = p.ret_view};
    wm_call(73, &r);
}

/* Native only: no macdrv_functions there, so 72/73 take the loose symbols, which this file defines (global, for
 * dlsym(RTLD_DEFAULT)). An unknown HWND (no window data) and one without a client view: no view, ret_layer untouched. */
struct loose_win_data {
    void *hwnd, *cocoa_window, *cocoa_view, *client_cocoa_view;
};
static struct loose_win_data loose_data;
static id loose_view, loose_layer;
#define LOOSE_CLIENT_VIEW ((void *)0x7777)

struct loose_win_data *get_win_data(void *hwnd);
void release_win_data(struct loose_win_data *d);
void *macdrv_view_create_metal_view(void *v, void *device);
void *macdrv_view_get_metal_layer(void *v);
void macdrv_view_release_metal_view(void *v);

struct loose_win_data *get_win_data(void *hwnd) {
    wm_line("loose get_win_data hwnd=%#llx", (unsigned long long)(uintptr_t)hwnd);
    if (hwnd == (void *)0x1) return NULL;
    loose_data = (struct loose_win_data){hwnd, NULL, NULL, hwnd == (void *)0x2 ? NULL : LOOSE_CLIENT_VIEW};
    return &loose_data;
}
void release_win_data(struct loose_win_data *d) {
    wm_line("loose release_win_data data=%s", d == &loose_data ? "given" : "other");
}
void *macdrv_view_create_metal_view(void *v, void *device) {
    wm_line("loose create_metal_view view=%s device=%s", v == LOOSE_CLIENT_VIEW ? "client_cocoa_view" : "other",
            device == (void *)wm_device ? "wm_device" : "other");
    loose_view = [[NSObject alloc] init]; /* +1: release_metal_view's */
    return loose_view;
}
void *macdrv_view_get_metal_layer(void *v) {
    wm_line("loose get_metal_layer view=%s", v == (void *)loose_view ? "created" : "other");
    if (!loose_layer) loose_layer = [[CAMetalLayer alloc] init];
    return loose_layer;
}
void macdrv_view_release_metal_view(void *v) {
    wm_line("loose release_metal_view view=%s", v == (void *)loose_view ? "created" : v ? "other" : "0");
    if (v == (void *)loose_view) [loose_view release];
}

static void view_loose(void) {
    const uint64_t hwnds[] = {0x10, 0x1, 0x2};
    obj_handle_t made = 0;
    for (unsigned i = 0; i < 3; i++) {
        struct unixcall_create_metal_view_from_hwnd p;
        wm_fill(&p, sizeof p);
        p.hwnd = hwnds[i], p.device = (obj_handle_t)wm_device;
        wm_own("ret_view", &p.ret_view);
        wm_own("ret_layer", &p.ret_layer);
        wm_call(72, &p);
        if (!i) made = p.ret_view;
    }
    struct unixcall_generic_obj_noret r = {.handle = made};
    wm_call(73, &r);
    r.handle = 0;
    wm_call(73, &r);
}

/* 92 for every colour space value; 93's sets (on main) on a recorder over a real layer, then the layer's colour
 * space name and EDR wish; 93 with an unknown colour space sends nothing and answers false. */
static void colorspace(void) {
    static const uint64_t all[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFFFFFFFFFFFFFFFFull};
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) {
        struct unixcall_generic_obj_uint64_ret p;
        wm_fill(&p, sizeof p);
        p.handle = all[i];
        wm_call(92, &p);
        wm_val("cs", "%#llx", (unsigned long long)all[i]);
        wm_val("ret", "%llu", (unsigned long long)p.ret);
    }
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) {
        if (all[i] == 6 || all[i] == 7 || all[i] == 8) continue;
        CAMetalLayer *layer = [CAMetalLayer layer];
        id rec = wm_rec(layer, nil);
        struct unixcall_generic_obj_uint64_uint64_ret p;
        wm_fill(&p, sizeof p);
        p.handle = (obj_handle_t)rec, p.arg = all[i];
        wm_call(93, &p);
        wm_val("cs", "%#llx", (unsigned long long)all[i]);
        wm_val("ret", "%llu", (unsigned long long)p.ret);
        __block NSString *name;
        __block BOOL wants;
        wm_on_main(^{
          name = layer.colorspace ? (__bridge NSString *)CGColorSpaceGetName(layer.colorspace) : @"none";
          wants = layer.wantsExtendedDynamicRangeContent;
        });
        wm_val("name", "%s", name.UTF8String);
        wm_val("wants_edr", "%d", wants);
    }
}

/* 94 is CGMainDisplayID; 95 is the C's answer for this Mac's displays: the first online display that is neither the
 * main one nor a mirror, else 0 (others= says how many such displays there are). */
static void displays(void) {
    struct unixcall_generic_obj_ret p;
    wm_fill(&p, sizeof p);
    wm_call(94, &p);
    wm_eq("ret", p.ret, CGMainDisplayID(), "CGMainDisplayID");
    wm_fill(&p, sizeof p);
    wm_call(95, &p);
    unsigned others;
    uint64_t oracle = secondary_display(&others);
    wm_eq("ret", p.ret, oracle, "first-other-online");
    wm_raw("others", "%u", others);
}

/* 96 for the main display and 95's (relations: the gamut written, each point inside the unit square; the EDR values
 * the screen's), an invalid ID and 0 (no profile: the generic RGB profile's gamut; no screen: 1, 0, 1). */
static void display_description(void) {
    const uint64_t ids[] = {INVALID_DISPLAY, 0, CGMainDisplayID(), secondary_display(NULL)};
    for (unsigned i = 0; i < 4; i++) {
        if (i == 3 && !ids[i]) {
            wm_line("secondary=none");
            break;
        }
        struct WMTDisplayDescription d;
        wm_fill(&d, sizeof d);
        struct unixcall_generic_obj_ptr_noret p = {.handle = ids[i], .arg = {.ptr = &d}};
        wm_call(96, &p);
        written32("written", &d, sizeof d);
        const float *pts[] = {d.red_primaries, d.green_primaries, d.blue_primaries, d.white_points};
        const char *names[] = {"red", "green", "blue", "white"};
        bool real = i >= 2;
        for (unsigned k = 0; k < 4; k++) {
            if (!real) wm_val(names[k], "%a,%a", pts[k][0], pts[k][1]);
            else
                wm_rel(names[k], pts[k][0] > 0 && pts[k][0] < 1 && pts[k][1] > 0 && pts[k][1] < 1, "in-unit", "%a,%a",
                       pts[k][0], pts[k][1]);
        }
        NSScreen *screen = real ? screen_of((CGDirectDisplayID)ids[i]) : nil;
        if (!screen) {
            wm_val("edr", "%a,%a,%a", d.maximum_edr_color_component_value,
                   d.maximum_potential_edr_color_component_value, d.maximum_reference_edr_color_component_value);
            continue;
        }
        struct edr e = screen_edr(screen);
        wm_rel("max_edr", d.maximum_edr_color_component_value == e.max, "screen", "%a",
               d.maximum_edr_color_component_value);
        wm_rel("potential_edr", d.maximum_potential_edr_color_component_value == e.potential, "screen", "%a",
               d.maximum_potential_edr_color_component_value);
        wm_rel("reference_edr", d.maximum_reference_edr_color_component_value == e.reference, "screen", "%a",
               d.maximum_reference_edr_color_component_value);
    }
}

/* 97 on a layer without a view (1, 1), then on the probe window's layer, its EDR wish on and off. */
static void edr(void) {
    struct WMTEDRValue v;
    wm_fill(&v, sizeof v);
    struct unixcall_generic_obj_ptr_noret p = {.handle = (obj_handle_t)[CAMetalLayer layer], .arg = {.ptr = &v}};
    wm_call(97, &p);
    wm_val("value", "%a,%a", v.maximum_edr_color_component_value, v.maximum_potential_edr_color_component_value);
    struct wm_window w = wm_window();
    NSScreen *s = window_screen(w);
    for (int on = 1; on >= 0; on--) {
        set_wants_edr(w.layer, on);
        wm_fill(&v, sizeof v);
        p.handle = (obj_handle_t)w.layer;
        wm_call(97, &p);
        struct edr e = screen_edr(s);
        wm_val("wants", "%d", on);
        if (on) wm_rel("max_edr", v.maximum_edr_color_component_value == e.max, "screen", "%a",
                       v.maximum_edr_color_component_value);
        else wm_val("max_edr", "%a", v.maximum_edr_color_component_value);
        wm_rel("potential_edr", v.maximum_potential_edr_color_component_value == e.potential, "screen", "%a",
               v.maximum_potential_edr_color_component_value);
    }
}

/* 100 then 99 (main: entry 1; any other ID, invalid or 0: entry 0; no metadata clears); 101 on a windowless layer,
 * then on the probe window's layer after 100 set the main display's (its EDR wish on and off). 99 after each 100, on
 * 100's line, is the observer (outside wm_call). */
static void display_settings(void) {
    uint64_t main_id = CGMainDisplayID();
    update(main_id, WMTColorSpaceSRGB, NULL);
    update(INVALID_DISPLAY, WMTColorSpaceSRGB, NULL);
    query(main_id, NULL);
    update(main_id, WMTColorSpaceHDR_PQ, &M1);
    query(main_id, &M1);
    query(INVALID_DISPLAY, NULL);
    update(INVALID_DISPLAY, WMTColorSpaceHDR_scRGB, &M2);
    query(0, &M2);
    uint64_t secondary = secondary_display(NULL);
    if (secondary) query(secondary, &M2);
    query(main_id, &M1);
    update(main_id, WMTColorSpaceSRGB, NULL);
    query(main_id, NULL);
    query_layer([CAMetalLayer layer], nil, false, NULL, 0);
    update(main_id, WMTColorSpaceHDR_PQ, &M1);
    /* The window is on NSScreen.mainScreen, the focused screen, not always the main display's: entry 1 or 0. */
    struct wm_window w = wm_window();
    NSScreen *s = window_screen(w);
    bool on_main = [s.deviceDescription[@"NSScreenNumber"] unsignedIntValue] == main_id;
    for (int on = 1; on >= 0; on--) {
        set_wants_edr(w.layer, on);
        query_layer(w.layer, s, on, on_main ? &M1 : &M2, on_main ? WMTColorSpaceHDR_PQ : WMTColorSpaceHDR_scRGB);
    }
    update(main_id, WMTColorSpaceSRGB, NULL);
    update(INVALID_DISPLAY, WMTColorSpaceSRGB, NULL);
}

/* With main blocked: 96, 97 and 101 on the window's layer read AppKit on the calling thread and return. */
static void main_blocked(void) {
    struct wm_window w = wm_window();
    wm_main_blocked(^{
      struct WMTDisplayDescription d;
      wm_fill(&d, sizeof d);
      struct unixcall_generic_obj_ptr_noret p = {.handle = CGMainDisplayID(), .arg = {.ptr = &d}};
      wm_call(96, &p);
      written32("written", &d, sizeof d);
      struct WMTEDRValue v;
      wm_fill(&v, sizeof v);
      struct unixcall_generic_obj_ptr_noret e = {.handle = (obj_handle_t)w.layer, .arg = {.ptr = &v}};
      wm_call(97, &e);
      written32("written", &v, sizeof v);
      struct WMTHDRMetadata m;
      wm_fill(&m, sizeof m);
      struct unixcall_query_display_setting_for_layer q;
      wm_fill(&q, sizeof q);
      q.layer = (obj_handle_t)w.layer, q.hdr_metadata.ptr = &m;
      wm_call(101, &q);
      wm_val("version", "%s", q.version == WM_FILL64 ? "fill" : "written");
    });
}

/* With main blocked: 70's sets wait for main (hang), then run. */
static void main_blocked_70(void) {
    CAMetalLayer *layer = [CAMetalLayer layer];
    struct WMTLayerProps props = layer_props((obj_handle_t)wm_device);
    wm_main_blocked(^{
      struct unixcall_generic_obj_constptr_noret s = {.handle = (obj_handle_t)layer, .arg = {.ptr = &props}};
      wm_call(70, &s);
    });
    __block CGSize size;
    wm_on_main(^{
      size = layer.drawableSize;
    });
    wm_val("size", "%ax%a", size.width, size.height);
}

/* Native only: the WSI hooks set, read back, cleared. */
static void wsi_hooks(void) {
    void (*set)(const void *) = wm_sym("WMTSetWsiHooks");
    const void *(*get)(void) = wm_sym("WMTGetWsiHooks");
    if (!set || !get) {
        wm_line("no-wsi-hooks");
        return;
    }
    static const char hooks[32];
    wm_line("initial=%s", get() ? "set" : "none");
    set(hooks);
    wm_line("after-set=%s", get() == hooks ? "same" : "other");
    set(NULL);
    wm_line("after-clear=%s", get() ? "set" : "none");
}

/* Each object argument as 0, sent on as nil: the getters' zeros, the recorders' nil arguments. */
static void nil_args(void) {
    /* The HUD's arguments as 0 are the hud case's (its child has the HUD's class); here a nil HUD. */
    struct unixcall_generic_obj_obj_obj_uint64_ret a;
    wm_fill(&a, sizeof a);
    a.handle = 0, a.arg0 = 0, a.arg1 = 0;
    wm_call(63, &a);
    wm_val("ret", "%llu", (unsigned long long)a.ret);
    struct unixcall_generic_obj_obj_obj_noret u = {.handle = 0};
    wm_call(64, &u);
    struct unixcall_generic_obj_obj_noret r = {.handle = 0};
    wm_call(65, &r);
    struct unixcall_generic_obj_obj_ret o;
    for (unsigned slot = 66; slot <= 67; slot++) {
        wm_fill(&o, sizeof o);
        o.handle = 0;
        wm_own("ret", &o.ret);
        wm_call(slot, &o);
    }
    struct WMTLayerProps props = layer_props(0);
    struct unixcall_generic_obj_constptr_noret s = {.handle = 0, .arg = {.ptr = &props}};
    wm_call(70, &s);
    s.handle = (obj_handle_t)wm_rec([CAMetalLayer layer], nil);
    wm_call(70, &s);
    struct WMTLayerProps out;
    wm_fill(&out, sizeof out);
    struct unixcall_generic_obj_ptr_noret g = {.handle = 0, .arg = {.ptr = &out}};
    wm_call(71, &g);
    wm_bytes("written", &out, sizeof out); /* zeros: no 0xA5 by chance */
    wm_val("device", "%llu", (unsigned long long)out.device);
    wm_val("scale", "%a", out.contents_scale);
    wm_val("pixel_format", "%u", (unsigned)out.pixel_format);
    struct unixcall_generic_obj_uint64_uint64_ret c;
    wm_fill(&c, sizeof c);
    c.handle = 0, c.arg = WMTColorSpaceSRGB;
    wm_call(93, &c);
    wm_val("ret", "%llu", (unsigned long long)c.ret);
    struct WMTEDRValue v;
    wm_fill(&v, sizeof v);
    struct unixcall_generic_obj_ptr_noret e = {.handle = 0, .arg = {.ptr = &v}};
    wm_call(97, &e);
    wm_val("value", "%a,%a", v.maximum_edr_color_component_value, v.maximum_potential_edr_color_component_value);
    query_layer(nil, nil, false, NULL, 0);
}

const struct wm_case wm_cases_g6[] = {
    {"g6", 62, "hud-absent", 0, hud_absent},
    {"g6", 62, "hud", WM_ISOLATE, hud},
    {"g6", 70, "layer-props", 0, layer_props_case},
    {"g6", 67, "drawable", 0, drawable},
    {"g6", 72, "view", WM_WINE_ONLY, view},
    {"g6", 72, "view-loose", WM_NATIVE, view_loose},
    {"g6", 92, "colorspace", 0, colorspace},
    {"g6", 94, "displays", 0, displays},
    {"g6", 96, "display-description", 0, display_description},
    {"g6", 97, "edr", 0, edr},
    {"g6", 100, "display-settings", 0, display_settings},
    {"g6", 96, "main-blocked", WM_ISOLATE, main_blocked},
    {"g6", 70, "main-blocked-70", WM_ISOLATE, main_blocked_70},
    {"g6", 99, "wsi-hooks", WM_NATIVE, wsi_hooks},
    {"g6", 63, "nil-args", 0, nil_args},
    {0},
};
