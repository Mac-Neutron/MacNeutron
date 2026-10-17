/* The VM host's stand-in for Wine's winemac.so (milestone 4c), which MacNeutron's winemetal.so links: winemetal finds
 * the window code through `macdrv_functions` (dlsym, DXMT's struct macdrv_functions_t). Every guest HWND maps to one
 * window, "MacNeutron VM", made on the main thread at first use, MN_VM_WINDOW=WxH points (default 1280x720), not
 * closable: milestone 5 owns its life. A metal view is a CAMetalLayer-backed view filling the content view, the newest
 * on top, as Wine's (dlls/winemac.drv/cocoa_window.m).
 * Input: the content view takes the window's keyboard and mouse and posts them as evdev events to the sink
 * macneutron-vm sets (mn_window_set_input_sink, eac-vm/host/input.c, the guest's virtio-input device). Keys by macOS
 * key code; Command combinations stay the Mac's. The mouse is absolute (the Mac cursor is the game's) until Control and
 * Option are pressed together, which captures it: the cursor hides and stays put, and motion goes as relative deltas
 * (a game's mouse look). The same chord, or the window losing focus, lets it go. */
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

/* evdev (linux/input-event-codes.h). */
enum { EV_SYN = 0, EV_KEY = 1, EV_REL = 2, EV_ABS = 3, SYN_REPORT = 0, REL_X = 0, REL_Y = 1, REL_HWHEEL = 6,
       REL_WHEEL = 8, ABS_X = 0, ABS_Y = 1, BTN_LEFT = 0x110, BTN_RIGHT = 0x111, BTN_MIDDLE = 0x112 };
#define ABS_MAX 32767 /* input.c's range */

/* macOS virtual key codes (Carbon's kVK_*) to evdev KEY_* codes; 0: none. */
static const uint16_t keymap[128] = {
    [0x00] = 30,  [0x01] = 31,  [0x02] = 32,  [0x03] = 33,  [0x04] = 35,  [0x05] = 34,  [0x06] = 44,  [0x07] = 45,
    [0x08] = 46,  [0x09] = 47,  [0x0A] = 86,  [0x0B] = 48,  [0x0C] = 16,  [0x0D] = 17,  [0x0E] = 18,  [0x0F] = 19,
    [0x10] = 21,  [0x11] = 20,  [0x12] = 2,   [0x13] = 3,   [0x14] = 4,   [0x15] = 5,   [0x16] = 7,   [0x17] = 6,
    [0x18] = 13,  [0x19] = 10,  [0x1A] = 8,   [0x1B] = 12,  [0x1C] = 9,   [0x1D] = 11,  [0x1E] = 27,  [0x1F] = 24,
    [0x20] = 22,  [0x21] = 26,  [0x22] = 23,  [0x23] = 25,  [0x24] = 28,  [0x25] = 38,  [0x26] = 36,  [0x27] = 40,
    [0x28] = 37,  [0x29] = 39,  [0x2A] = 43,  [0x2B] = 51,  [0x2C] = 53,  [0x2D] = 49,  [0x2E] = 50,  [0x2F] = 52,
    [0x30] = 15,  [0x31] = 57,  [0x32] = 41,  [0x33] = 14,  [0x35] = 1,   [0x36] = 126, [0x37] = 125, [0x38] = 42,
    [0x39] = 58,  [0x3A] = 56,  [0x3B] = 29,  [0x3C] = 54,  [0x3D] = 100, [0x3E] = 97,  [0x40] = 187, [0x41] = 83,
    [0x43] = 55,  [0x45] = 78,  [0x47] = 69,  [0x4B] = 98,  [0x4C] = 96,  [0x4E] = 74,  [0x4F] = 188, [0x50] = 189,
    [0x51] = 117, [0x52] = 82,  [0x53] = 79,  [0x54] = 80,  [0x55] = 81,  [0x56] = 75,  [0x57] = 76,  [0x58] = 77,
    [0x59] = 71,  [0x5A] = 190, [0x5B] = 72,  [0x5C] = 73,  [0x5D] = 124, [0x5E] = 89,  [0x5F] = 95,  [0x60] = 63,
    [0x61] = 64,  [0x62] = 65,  [0x63] = 61,  [0x64] = 66,  [0x65] = 67,  [0x66] = 94,  [0x67] = 87,  [0x68] = 92,
    [0x69] = 183, [0x6A] = 186, [0x6B] = 184, [0x6D] = 68,  [0x6E] = 127, [0x6F] = 88,  [0x71] = 185, [0x72] = 110,
    [0x73] = 102, [0x74] = 104, [0x75] = 111, [0x76] = 62,  [0x77] = 107, [0x78] = 60,  [0x79] = 109, [0x7A] = 59,
    [0x7B] = 105, [0x7C] = 106, [0x7D] = 108, [0x7E] = 103,
};

static void (*_Atomic input_sink)(uint16_t type, uint16_t code, int32_t value);

__attribute__((visibility("default"))) void mn_window_set_input_sink(void (*sink)(uint16_t, uint16_t, int32_t)) {
    input_sink = sink;
}

static void post(uint16_t type, uint16_t code, int32_t value) {
    void (*sink)(uint16_t, uint16_t, int32_t) = input_sink;
    if (sink) sink(type, code, value);
}

/* The window's content view: the game's input. Main thread only, as AppKit calls it. */
@interface MNInputView : NSView {
    uint8_t down[0x120]; /* evdev keys and buttons the guest saw pressed */
    BOOL captured, chord;
    double wheel, hwheel; /* a trackpad's scroll, until it makes a notch */
}
@end

@implementation MNInputView
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)isOpaque {
    return YES;
}
- (void)updateTrackingAreas {
    for (NSTrackingArea *a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[[NSTrackingArea alloc]
                              initWithRect:NSZeroRect
                                   options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                     owner:self
                                  userInfo:nil] autorelease]];
    [super updateTrackingAreas];
}
- (void)key:(uint16_t)code down:(BOOL)d {
    if (!code || code >= sizeof down || down[code] == d) return;
    down[code] = d;
    post(EV_KEY, code, d);
    post(EV_SYN, SYN_REPORT, 0);
}
- (void)releaseAll {
    for (uint16_t c = 1; c < sizeof down; c++) [self key:c down:NO];
    [self capture:NO];
}
- (void)capture:(BOOL)on {
    if (captured == on) return;
    captured = on;
    CGAssociateMouseAndMouseCursorPosition(!on);
    if (on) [NSCursor hide];
    else [NSCursor unhide];
}
- (void)keyDown:(NSEvent *)e {
    if (!e.isARepeat && !(e.modifierFlags & NSEventModifierFlagCommand) && e.keyCode < 128) [self key:keymap[e.keyCode] down:YES];
}
- (void)keyUp:(NSEvent *)e {
    if (e.keyCode < 128) [self key:keymap[e.keyCode] down:NO];
}
- (void)flagsChanged:(NSEvent *)e {
    /* Which side: the device-dependent bits (IOLLEvent.h's NX_DEVICE*KEYMASK). */
    static const struct { uint16_t vk; NSUInteger mask; } mods[] = {
        {0x38, 0x02}, {0x3C, 0x04}, {0x3B, 0x01}, {0x3E, 0x2000}, {0x3A, 0x20}, {0x3D, 0x40}, {0x37, 0x08}, {0x36, 0x10},
    };
    NSUInteger f = e.modifierFlags;
    if (e.keyCode == 0x39) { /* Caps Lock: one change, one press */
        [self key:58 down:YES];
        [self key:58 down:NO];
        return;
    }
    for (size_t i = 0; i < sizeof mods / sizeof *mods; i++)
        if (mods[i].vk == e.keyCode && mods[i].vk != 0x37 && mods[i].vk != 0x36) /* Command stays the Mac's */
            [self key:keymap[e.keyCode] down:(f & mods[i].mask) != 0];
    BOOL both = (f & NSEventModifierFlagControl) && (f & NSEventModifierFlagOption);
    if (both && !chord) [self capture:!captured];
    chord = both;
}
- (void)moved:(NSEvent *)e {
    if (captured) {
        if (e.deltaX) post(EV_REL, REL_X, (int32_t)lround(e.deltaX));
        if (e.deltaY) post(EV_REL, REL_Y, (int32_t)lround(e.deltaY));
    } else {
        NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
        NSSize s = self.bounds.size;
        double x = s.width > 0 ? p.x / s.width : 0, y = s.height > 0 ? 1 - p.y / s.height : 0;
        post(EV_ABS, ABS_X, (int32_t)lround(fmin(fmax(x, 0), 1) * ABS_MAX));
        post(EV_ABS, ABS_Y, (int32_t)lround(fmin(fmax(y, 0), 1) * ABS_MAX));
    }
    post(EV_SYN, SYN_REPORT, 0);
}
- (void)button:(uint16_t)b event:(NSEvent *)e down:(BOOL)d {
    if (d && !captured) [self moved:e]; /* where the click is, first */
    [self key:b down:d];
}
- (void)mouseMoved:(NSEvent *)e { [self moved:e]; }
- (void)mouseDragged:(NSEvent *)e { [self moved:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self moved:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self moved:e]; }
- (void)mouseDown:(NSEvent *)e { [self button:BTN_LEFT event:e down:YES]; }
- (void)mouseUp:(NSEvent *)e { [self button:BTN_LEFT event:e down:NO]; }
- (void)rightMouseDown:(NSEvent *)e { [self button:BTN_RIGHT event:e down:YES]; }
- (void)rightMouseUp:(NSEvent *)e { [self button:BTN_RIGHT event:e down:NO]; }
- (void)otherMouseDown:(NSEvent *)e { [self button:(uint16_t)(BTN_MIDDLE + MIN(e.buttonNumber - 2, 2)) event:e down:YES]; }
- (void)otherMouseUp:(NSEvent *)e { [self button:(uint16_t)(BTN_MIDDLE + MIN(e.buttonNumber - 2, 2)) event:e down:NO]; }
- (void)scrollWheel:(NSEvent *)e {
    /* A wheel's notch is one; a trackpad's scroll adds up, a notch each 10 points. */
    double k = e.hasPreciseScrollingDeltas ? 0.1 : 1;
    wheel += e.scrollingDeltaY * k, hwheel += e.scrollingDeltaX * k;
    int v = (int)trunc(wheel), h = (int)trunc(hwheel);
    wheel -= v, hwheel -= h;
    if (v) post(EV_REL, REL_WHEEL, v);
    if (h) post(EV_REL, REL_HWHEEL, -h);
    if (v || h) post(EV_SYN, SYN_REPORT, 0);
}
@end

/* The Dock icon's Quit (or a logout) would end the process under a running VM, its state disk mid-write: refused.
 * ponytail: the VM ends with its game; a quit that powers the guest off waits for milestone 5's agent channel. */
@interface MNAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation MNAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;
    fprintf(stderr, "mn-window: quit refused while the VM runs: quit the game instead\n");
    return NSTerminateCancel;
}
@end

@interface MNWindowDelegate : NSObject <NSWindowDelegate>
@end
@implementation MNWindowDelegate
- (void)windowDidResignKey:(NSNotification *)n {
    [(MNInputView *)((NSWindow *)n.object).contentView releaseAll];
}
@end

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
- (NSView *)hitTest:(NSPoint)p {
    (void)p;
    return nil; /* every click is the content view's: the game's input */
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
    window.contentView = [[[MNInputView alloc] initWithFrame:NSMakeRect(0, 0, w, h)] autorelease];
    window.contentView.wantsLayer = YES;
    window.acceptsMouseMovedEvents = YES;
    window.delegate = [[MNWindowDelegate alloc] init]; /* the window's life: never released */
    [window makeFirstResponder:window.contentView];
    [window center];
    if (!NSApp.delegate) NSApp.delegate = [[MNAppDelegate alloc] init]; /* the process's life: never released */
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
