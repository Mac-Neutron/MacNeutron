/* The VM host's stand-in for Wine's winemac.so (milestone 4c), which MacNeutron's winemetal.so links: winemetal finds
 * the window code through `macdrv_functions` (dlsym, DXMT's struct macdrv_functions_t). Every guest HWND maps to one
 * window, "MacNeutron VM", made on the main thread at first use, MN_VM_WINDOW=WxH points (default 1280x720), not
 * closable: milestone 5 owns its life. A metal view is a CAMetalLayer-backed view filling the content view, the newest
 * on top, as Wine's (dlls/winemac.drv/cocoa_window.m). */
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <stdio.h>
#include <stdlib.h>

/* winemetal's struct macdrv_win_data: it reads client_cocoa_view only. */
struct macdrv_win_data { void *hwnd, *cocoa_window, *cocoa_view, *client_cocoa_view; };

static void on_main(dispatch_block_t b) {
    if ([NSThread isMainThread]) b();
    else dispatch_sync(dispatch_get_main_queue(), b);
}

@interface MNMetalView : NSView {
    id<MTLDevice> device;
}
@end

@implementation MNMetalView
- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)d {
    if ((self = [super initWithFrame:frame])) {
        device = [d retain];
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
        self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }
    return self;
}
- (void)dealloc {
    [device release];
    [super dealloc];
}
- (CALayer *)makeBackingLayer {
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.framebufferOnly = YES; /* DXMT's setProps decides */
    layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
    return layer;
}
- (BOOL)isOpaque {
    return YES;
}
@end

/* Main thread only. */
static NSWindow *the_window(void) {
    static NSWindow *window;
    if (window) return window;
    unsigned w = 1280, h = 720;
    const char *size = getenv("MN_VM_WINDOW");
    if (size && (sscanf(size, "%ux%u", &w, &h) != 2 || !w || !h || w > 16384 || h > 16384)) {
        fprintf(stderr, "mn-window: MN_VM_WINDOW=%s isn't WxH, 1280x720 instead\n", size);
        w = 1280, h = 720;
    }
    [NSApplication sharedApplication];
    window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, w, h)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.releasedWhenClosed = NO;
    window.title = @"MacNeutron VM";
    window.contentView.wantsLayer = YES;
    [window center];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [window makeKeyAndOrderFront:nil];
    [NSApp activate];
    return window;
}

static struct macdrv_win_data *get_win_data(void *hwnd) {
    struct macdrv_win_data *d = calloc(1, sizeof *d);
    if (!d) abort(); /* winemetal reads it unchecked */
    d->hwnd = hwnd;
    on_main(^{
        NSWindow *w = the_window();
        d->cocoa_window = w;
        d->client_cocoa_view = w.contentView;
    });
    return d;
}

static void release_win_data(struct macdrv_win_data *d) { free(d); }

static void *create_metal_view(void *v, void *device) {
    __block MNMetalView *view = nil;
    on_main(^{
        NSView *content = v;
        view = [[MNMetalView alloc] initWithFrame:content.bounds device:device];
        [content addSubview:view positioned:NSWindowAbove relativeTo:nil];
    });
    return view; /* +1: release_metal_view's */
}

static void *get_metal_layer(void *v) {
    __block CALayer *layer;
    on_main(^{ layer = ((NSView *)v).layer; });
    return layer;
}

static void release_metal_view(void *v) {
    on_main(^{
        [(NSView *)v removeFromSuperview];
        [(NSView *)v release];
    });
}

__attribute__((visibility("default"))) const struct {
    void *init_display_devices;
    struct macdrv_win_data *(*get_win_data)(void *hwnd);
    void (*release_win_data)(struct macdrv_win_data *d);
    void *get_cocoa_window, *create_metal_device, *release_metal_device;
    void *(*view_create_metal_view)(void *view, void *device);
    void *(*view_get_metal_layer)(void *view);
    void (*view_release_metal_view)(void *view);
    void (*on_main_thread)(dispatch_block_t b);
} macdrv_functions = {
    NULL, get_win_data, release_win_data, NULL, NULL, NULL, create_metal_view, get_metal_layer, release_metal_view,
    on_main,
};
