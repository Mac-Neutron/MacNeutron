/* The host's side of presentation (milestone 4c): slots 47 and 48 count the drawables a context presents (dispatch.m
 * logs the count at its end); 71's reply carries no host device pointer; 73 is a view's release (the table's, which
 * calls winemetal's slot 73: invoke.m).
 * With MN_PRESENT_PROBE=1, every 60th present (the 30th, 90th, ...) first blits the drawable's texture into a Shared
 * buffer in the same command buffer; when it completes, the host logs the frame's pixels as winshot measures a window:
 * green (the green channel 60-95), white (every channel above 200), and colours at 4 bits a channel. The pixels are
 * read as stored: 8-bit BGRA or RGBA, or 10-bit RGB10A2 or BGR10A2 (SMITE 2's), of which the top 8 bits a channel. */
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <stdatomic.h>
#include <stdio.h>
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
