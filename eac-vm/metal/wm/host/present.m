/* The host's side of presentation (milestone 4c): slots 47 and 48 count the drawables a context presents (dispatch.m
 * logs the count at its end), as mn_wm_present_native does native DXMT's (Direct3D forwarding); 71's reply carries no
 * host device pointer; 73 is a view's release (the table's, which calls winemetal's slot 73: invoke.m).
 * With MN_PRESENT_PROBE=1, every 60th present (the 30th, 90th, ...) first blits the drawable's texture into a Shared
 * buffer in the same command buffer; when it completes, the host logs the frame's pixels as winshot measures a window:
 * green (the green channel 60-95), white (every channel above 200), and colours at 4 bits a channel. The pixels are
 * read as stored: 8-bit BGRA or RGBA, or 10-bit RGB10A2 or BGR10A2 (SMITE 2's), of which the top 8 bits a channel.
 * With MN_PRESENT_STATS=<file> (the launcher's MACNEUTRON_PERF, for eac-vm/tests/perf-compare.sh), the frames presented
 * every 5 s (MN_PRESENT_STATS_PERIOD seconds) go to the file as DXMT_STATS writes them, "# HH:MM:SS 5.0 s, N frames
 * (F fps), every 5 s": counted here, natively, where DXMT_STATS's own timing would cost the guest a clock read under
 * FEX for every D3D12 call. */
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "../marshal.h"
#include "winemetal_thunks.h"

static int probing(void) {
    static dispatch_once_t once;
    static int on;
    dispatch_once(&once, ^{
        const char *v = getenv("MN_PRESENT_PROBE");
        on = v && !strcmp(v, "1");
    });
    return on;
}

static _Atomic uint64_t window_frames;

static void count_frame(void) {
    static dispatch_once_t once;
    static dispatch_source_t timer; /* the process's life */
    dispatch_once(&once, ^{
        const char *path = getenv("MN_PRESENT_STATS"), *p = getenv("MN_PRESENT_STATS_PERIOD");
        double period = p && atof(p) > 0 ? atof(p) : 5;
        FILE *out = path && *path ? fopen(path, "a") : NULL;
        if (!out) return;
        __block uint64_t last = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
        dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(period * 1e9)),
                                  (uint64_t)(period * 1e9), (uint64_t)1e8);
        dispatch_source_set_event_handler(timer, ^{
            uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), n = atomic_exchange(&window_frames, 0);
            double s = (now - last) / 1e9;
            last = now;
            time_t wall = time(NULL);
            struct tm tm;
            localtime_r(&wall, &tm);
            fprintf(out, "# %02d:%02d:%02d %.1f s, %llu frames (%.1f fps), every 5 s\n", tm.tm_hour, tm.tm_min,
                    tm.tm_sec, s, (unsigned long long)n, s > 0 ? n / s : 0.0);
            fflush(out);
        });
        dispatch_resume(timer);
    });
    atomic_fetch_add(&window_frames, 1);
}

static void probe(id<MTLCommandBuffer> cb, id<CAMetalDrawable> drawable, uint64_t frame) {
    @autoreleasepool {
        id<MTLTexture> t = drawable.texture;
        MTLPixelFormat f = t.pixelFormat;
        /* Where red, green and blue are in a pixel's 32 bits, and how many bits each has. */
        unsigned rs, gs = 8, bs, bits = 8;
        if (f == MTLPixelFormatRGBA8Unorm || f == MTLPixelFormatRGBA8Unorm_sRGB) rs = 0, bs = 16;
        else if (f == MTLPixelFormatBGRA8Unorm || f == MTLPixelFormatBGRA8Unorm_sRGB) rs = 16, bs = 0;
        else if (f == MTLPixelFormatRGB10A2Unorm) rs = 0, gs = 10, bs = 20, bits = 10;
        else if (f == MTLPixelFormatBGR10A2Unorm) rs = 20, gs = 10, bs = 0, bits = 10;
        else rs = bs = 32; /* not read */
        if (rs == 32 || t.framebufferOnly) {
            fprintf(stderr, "mn-present: probe %llu format %lu%s not read\n", (unsigned long long)frame,
                    (unsigned long)f, t.framebufferOnly ? " framebuffer-only" : "");
            return;
        }
        NSUInteger w = t.width, h = t.height;
        id<MTLBuffer> buf = [cb.device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
        if (!buf) return;
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(w, h, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:w * 4
              destinationBytesPerImage:w * h * 4];
        [blit endEncoding];
        [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
            if (done.status != MTLCommandBufferStatusCompleted) {
                fprintf(stderr, "mn-present: probe %llu failed\n", (unsigned long long)frame);
            } else {
                const uint32_t *p = buf.contents;
                size_t n = w * h, green = 0, white = 0, colors = 0;
                uint64_t seen[4096 / 64] = {0};
                unsigned top = bits - 8; /* each channel's top 8 bits */
                for (size_t i = 0; i < n; i++) {
                    unsigned r = p[i] >> (rs + top) & 255, g = p[i] >> (gs + top) & 255, b = p[i] >> (bs + top) & 255;
                    green += g >= 60 && g <= 95;
                    white += r > 200 && g > 200 && b > 200;
                    unsigned c = (r >> 4) << 8 | (g >> 4) << 4 | b >> 4;
                    if (!(seen[c / 64] >> (c % 64) & 1)) seen[c / 64] |= 1ull << (c % 64), colors++;
                }
                fprintf(stderr, "mn-present: probe %llu pixels %zu green %zu white %zu colors %zu\n",
                        (unsigned long long)frame, n, n ? green * 100 / n : 0, n ? white * 100 / n : 0, colors);
            }
            [buf release];
        }];
    }
}

/* A native present (Direct3D forwarding: native DXMT's present hook, fork 0081), as slots 47 and 48 take one: counted
 * to c, the context whose call it ran in, or to none off a call (DXMT's own threads: probed and in the stats only). */
void mn_wm_present_native(struct mn_wm_ctx *c, void *cmdbuf, void *drawable) {
    static _Atomic uint64_t uncounted;
    uint64_t frame = atomic_fetch_add(c ? &c->presented : &uncounted, 1) + 1;
    if (probing() && frame % 60 == 30 && cmdbuf && drawable)
        probe((id<MTLCommandBuffer>)cmdbuf, (id<CAMetalDrawable>)drawable, frame);
    count_frame();
}

/* fn(ctx) once the display shows drawable (or drops it): Metal's presented handler. */
void mn_wm_on_shown(void *drawable, void (*fn)(void *ctx), void *ctx) {
    [(id<MTLDrawable>)drawable addPresentedHandler:^(id<MTLDrawable> d) {
        (void)d;
        fn(ctx);
    }];
}

NTSTATUS_T mn_wm_present_special(struct mn_wm_state *k) {
    switch (k->slot) {
    case 47:
    case 48: { /* both structs: the command buffer, then the drawable */
        id<MTLCommandBuffer> cb;
        id<CAMetalDrawable> drawable;
        memcpy(&cb, k->p, sizeof cb);
        memcpy(&drawable, k->p + 8, sizeof drawable);
        uint64_t frame = atomic_fetch_add(&k->c->presented, 1) + 1;
        if (probing() && frame % 60 == 30 && cb && drawable) probe(cb, drawable, frame);
        NTSTATUS_T s = mn_wm_invoke(k->slot, k->p);
        if (s) atomic_fetch_sub(&k->c->presented, 1);
        else count_frame();
        return s;
    }
    case 71: {
        struct unixcall_generic_obj_ptr_noret *a = (void *)k->p;
        struct WMTLayerProps *props = a->arg.ptr;
        if (!props) return MN_STATUS_INVALID_PARAMETER;
        NTSTATUS_T s = mn_wm_invoke(71, a);
        props->device = 0; /* the guest puts its own value back (marshal.c) */
        return s;
    }
    case 73: {
        mn_handle h;
        memcpy(&h, k->rec, sizeof h);
        return mn_ht_release(k->c->ht, k->ring, h) ? MN_STATUS_INVALID_HANDLE : 0;
    }
    default:
        return MN_STATUS_NOT_IMPLEMENTED;
    }
}
