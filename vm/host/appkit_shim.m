// AppKit is the one thing the VM host needs from Cocoa: the main thread must run its run loop (Metal and the game
// window's stand-in winemac.so share that loop), while the VM itself runs off the main thread. Isolating it here keeps
// the Rust host free of objc2 and any Objective-C dependency.
#import <AppKit/AppKit.h>

// Held in a static so the compiler never inserts an early release of the activity (it must last until the process ends;
// [app run] never returns, and a balanced endActivity is never reached).
static id g_activity_token;

void mn_run_appkit_main_loop(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        // No Dock icon until the game window shows (accessory, not regular).
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        // App Nap would slow the guest once the window is covered, and an idle Mac would sleep under a running game;
        // the activity lasts as long as the process (never balanced, it ends with the VM).
        g_activity_token = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                              reason:@"MacNeutron game VM"];
        CFRetain((__bridge CFTypeRef)g_activity_token);
        [app run];
    }
}
