/* wm_probe's g0 cases (slots.txt's g0; probe.h), recorded from DXMT's C: the null entry 83, the guest-only slots 153-155,
 * and the two constructors (DXMT_MAX_ANISOTROPY, whose floor slot 19 applies; MACNEUTRON_PRESENT, which loads the
 * presenter). The constructors run when the image loads, so each env case is a child with the variable set. */
#include "probe.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <string.h>

/* 83: the table entry is null and never called (wm_call answers "null" without calling). */
static void slot83_null(void) {
    struct unixcall_generic_obj_noret p = {.handle = WM_FILL64};
    wm_call(83, &p);
    wm_bytes("written", &p, sizeof p);
}

/* 153-155: STATUS_NOT_IMPLEMENTED, nothing read (a null params too) and nothing written. */
static void guest_only(void) {
    for (unsigned slot = 153; slot <= 155; slot++) {
        uint8_t block[64];
        wm_fill(block, sizeof block);
        wm_call(slot, block);
        wm_bytes("written", block, sizeof block);
        wm_call(slot, NULL);
    }
}

/* 19 through a recording device (the real device behind it makes the sampler): the descriptor's maxAnisotropy shows
 * the floor. The floor applies when the sampler is trilinear (min, mag and mip linear), not a comparison sampler
 * (compare never) and its own value is below the forced one; the child's DXMT_MAX_ANISOTROPY (wm_env()) is read
 * once at load: 8 and "8x" (atoi) give 8, "0" and "17" (out of 1..16) give none. */
static void aniso_env(void) {
    wm_line("env=%s", wm_env());
    id device = wm_rec(wm_device, @protocol(MTLDevice));
    static const struct {
        const char *what;
        unsigned min, mag, mip, compare, own;
    } samplers[] = {
        {"all-five-own-1", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 1},
        {"all-five-own-4", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 4},
        {"min-nearest", WMTSamplerMinMagFilterNearest, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 1},
        {"mag-nearest", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterNearest, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 1},
        {"mip-nearest", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterNearest,
         WMTCompareFunctionNever, 1},
        {"mip-none", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterNotMipmapped,
         WMTCompareFunctionNever, 1},
        {"compare-less", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionLess, 1},
        {"own-8", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 8},
        {"own-12", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 12},
        {"own-16", WMTSamplerMinMagFilterLinear, WMTSamplerMinMagFilterLinear, WMTSamplerMipFilterLinear,
         WMTCompareFunctionNever, 16},
    };
    for (unsigned i = 0; i < sizeof samplers / sizeof *samplers; i++) {
        struct WMTSamplerInfo info;
        wm_fill(&info, sizeof info);
        info.min_filter = samplers[i].min;
        info.mag_filter = samplers[i].mag;
        info.mip_filter = samplers[i].mip;
        info.r_address_mode = info.s_address_mode = info.t_address_mode = WMTSamplerAddressModeRepeat;
        info.border_color = WMTSamplerBorderColorTransparentBlack;
        info.compare_function = samplers[i].compare;
        info.lod_min_clamp = 0;
        info.lod_max_clamp = 1000;
        info.max_anisotroy = samplers[i].own;
        info.normalized_coords = true;
        info.lod_average = false;
        info.support_argument_buffers = false;
        struct unixcall_mtldevice_newsamplerstate p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)device;
        p.info.ptr = &info;
        wm_line("sampler=%s", samplers[i].what);
        wm_own("ret", &p.ret);
        wm_call(19, &p);
    }
}

static bool presenter_image_loaded(void) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *n = _dyld_get_image_name(i);
        const char *tail = "/libmacneutron-present.dylib";
        size_t len = strlen(n), t = strlen(tail);
        if (len >= t && !strcmp(n + len - t, tail)) return true;
    }
    return false;
}

/* The load_presenter constructor with MACNEUTRON_PRESENT=1: without the presenter beside the image ("absent") the C's
 * stderr line (it names the folder, so the line is a relation), with a copy of the folder holding it ("present") the
 * image is loaded and the line is not printed. */
static void presenter_env(void) {
    const char *variant = wm_env();
    wm_line("env=%s", variant);
    const char *err = wm_child_stderr();
    const char *head = "winemetal: can't load the MetalFX presenter: ";
    bool failed = err && !strncmp(err, head, strlen(head)) && strstr(err, "libmacneutron-present.dylib");
    const char *kind = !err ? "none" : !*err ? "empty" : failed ? "load-failed-line" : "other";
    wm_rel("stderr", strcmp(kind, "other") != 0, kind, "%s", err ? err : "(no stderr)");
    wm_val("presenter-loaded", "%s", presenter_image_loaded() ? "yes" : "no");
}

const struct wm_case wm_cases_g0[] = {
    {"g0", 83, "slot83-null", 0, slot83_null},
    {"g0", 153, "guest-only", 0, guest_only},
    {"g0", 19, "aniso-env", WM_ENV_ANISO, aniso_env},
    {"g0", 153, "presenter-env", WM_ENV_PRESENT, presenter_env},
    {0},
};
