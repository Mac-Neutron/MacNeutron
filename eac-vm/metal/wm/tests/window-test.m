/* Milestone 4c, Task 1: the host's game window (the stand-in winemac.so, host/winemac.m), through MacNeutron's real
 * winemetal.so, its slots called directly. Usage: window-test <metal-host dir>. The main thread runs NSApp, as in
 * macneutron-vm; the cases run on a worker thread started from its run loop.
 *   view       slot 72 gives a view and a layer in the window "MacNeutron VM" (1280x720 points); a 320x180 BGRA8
 *              drawable (slots 70, 67, 66) cleared to (0, 0.3, 0), blitted into a Shared buffer and presented (47)
 *              reads (0, 77, 0)
 *   two-views  a second HWND's view goes on top; slot 73 on it leaves the first on top, its layer intact
 *   release    slot 73 on the last view leaves no metal view in the window */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "../host/loader.h"
#include "winemetal_thunks.h"

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __func__, __LINE__, #c);                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static NTSTATUS_T W(unsigned slot, void *p) { return mn_wm_funcs[slot](p); }
static void on_main(dispatch_block_t b) { dispatch_sync(dispatch_get_main_queue(), b); }

static id<MTLDevice> device;
static NSView *content; /* the window's content view, kept past the views' release */
static obj_handle_t view1, layer1;

/* The content view's metal views, back to front. */
static NSArray *metal_views(void) {
    __block NSArray *a;
    on_main(^{
        a = [[content.subviews filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSView *v, id b) {
            (void)b;
            return [v.layer isKindOfClass:[CAMetalLayer class]];
        }]] retain];
    });
    return [a autorelease];
}

static void view(void) {
    CHECK(dlsym(RTLD_DEFAULT, "macdrv_functions") != NULL); /* else winemetal never finds the window code */
    struct unixcall_create_metal_view_from_hwnd c = {0x1234, (obj_handle_t)device, 0, 0};
    CHECK(W(72, &c) == 0 && c.ret_view && c.ret_layer);
    if (!c.ret_view || !c.ret_layer) return;
    view1 = c.ret_view, layer1 = c.ret_layer;
    __block BOOL delegate_ok = NO, title_ok = NO;
    __block NSSize size = {0, 0};
    on_main(^{
        NSView *v = (NSView *)((CAMetalLayer *)layer1).delegate;
        delegate_ok = v == (NSView *)view1;
        title_ok = [v.window.title isEqualToString:@"MacNeutron VM"];
        content = [v.window.contentView retain];
        size = content.frame.size;
    });
    CHECK(delegate_ok && title_ok && size.width == 1280 && size.height == 720);

    struct WMTLayerProps props = {.device = (obj_handle_t)device, .contents_scale = 1, .drawable_width = 320,
                                  .drawable_height = 180, .opaque = 1, .framebuffer_only = 0,
                                  .pixel_format = WMTPixelFormatBGRA8Unorm};
    struct unixcall_generic_obj_constptr_noret sp = {layer1, {&props}};
    CHECK(W(70, &sp) == 0);
    struct unixcall_generic_obj_obj_ret nd = {layer1, 0};
    CHECK(W(67, &nd) == 0 && nd.ret);
    if (!nd.ret) return;
    struct unixcall_generic_obj_obj_ret tx = {nd.ret, 0};
    CHECK(W(66, &tx) == 0 && tx.ret);
    id<MTLTexture> tex = (id<MTLTexture>)tx.ret;
    CHECK(tex.width == 320 && tex.height == 180 && tex.pixelFormat == MTLPixelFormatBGRA8Unorm);

    id<MTLCommandQueue> q = [device newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = tex;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0.3, 0, 1);
    [[cb renderCommandEncoderWithDescriptor:rp] endEncoding];
    id<MTLBuffer> buf = [device newBufferWithLength:320 * 180 * 4 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(320, 180, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:320 * 4
          destinationBytesPerImage:320 * 180 * 4];
    [blit endEncoding];
    struct unixcall_generic_obj_obj_noret pd = {(obj_handle_t)cb, nd.ret};
    CHECK(W(47, &pd) == 0);
    [cb commit];
    [cb waitUntilCompleted];
    CHECK(cb.status == MTLCommandBufferStatusCompleted);
    const uint8_t *px = buf.contents;
    int bad = 0;
    for (int i = 0; i < 320 * 180; i++) bad += px[i * 4] != 0 || px[i * 4 + 1] != 77 || px[i * 4 + 2] != 0;
    CHECK(bad == 0);
    [buf release];
    [q release];
}

static void two_views(void) {
    if (!view1) {
        CHECK(view1);
        return;
    }
    struct unixcall_create_metal_view_from_hwnd c = {0x5678, (obj_handle_t)device, 0, 0};
    CHECK(W(72, &c) == 0 && c.ret_view && c.ret_layer && c.ret_view != view1 && c.ret_layer != layer1);
    if (!c.ret_view) return;
    NSArray *v = metal_views();
    CHECK(v.count == 2 && v.lastObject == (NSView *)c.ret_view);
    struct unixcall_generic_obj_noret r = {c.ret_view};
    CHECK(W(73, &r) == 0);
    v = metal_views();
    CHECK(v.count == 1 && v.lastObject == (NSView *)view1);
    __block BOOL layer_ok = NO;
    on_main(^{
        layer_ok = ((NSView *)view1).layer == (CALayer *)layer1 && ((CAMetalLayer *)layer1).delegate == (id)view1;
    });
    CHECK(layer_ok);
}

static void release(void) {
    if (!view1) {
        CHECK(view1);
        return;
    }
    struct unixcall_generic_obj_noret r = {view1};
    CHECK(W(73, &r) == 0);
    CHECK(metal_views().count == 0);
}

static void *run(void *arg) {
    (void)arg;
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"view", view}, {"two-views", two_views}, {"release", release},
    };
    for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
        @autoreleasepool {
            int before = failures;
            tests[i].fn();
            printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
        }
    }
    printf("window-test: %s\n", failures ? "FAIL" : "all passed");
    fflush(stdout);
    exit(failures != 0);
}

int main(int argc, char **argv) {
    if (argc != 2 || mn_wm_load(argv[1])) {
        fprintf(stderr, "window-test: FAIL loading winemetal.so\n");
        return 1;
    }
    alarm(120); /* a main-thread deadlock fails the test instead of hanging it */
    device = MTLCreateSystemDefaultDevice();
    [NSApplication sharedApplication];
    dispatch_async(dispatch_get_main_queue(), ^{ /* runs once the main thread runs the application */
        pthread_t t;
        pthread_create(&t, NULL, run, NULL);
    });
    [NSApp run];
    return 1;
}
