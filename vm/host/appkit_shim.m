// AppKit is the one thing the VM host needs from Cocoa: the main thread must run its run loop (Metal and the game
// window's stand-in winemac.so share that loop), while the VM itself runs off the main thread. Isolating it here keeps
// the Rust host free of objc2 and any Objective-C dependency.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <mach/mach.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Held in a static so the compiler never inserts an early release of the activity (it must last until the process ends;
// [app run] never returns, and a balanced endActivity is never reached).
static id g_activity_token;
typedef void (*mn_wake_cb)(void *ctx);
// Guarded by g_wake_lock so callback/context pair registration and wake dispatch cannot tear across threads.
static pthread_mutex_t g_wake_lock = PTHREAD_MUTEX_INITIALIZER;
static mn_wake_cb g_wake_cb = NULL;
static void *g_wake_ctx = NULL;
static id g_wake_observer = nil;

void mn_register_wake_callback(mn_wake_cb cb, void *ctx) {
    pthread_mutex_lock(&g_wake_lock);
    g_wake_cb = cb;
    g_wake_ctx = ctx;
    pthread_mutex_unlock(&g_wake_lock);
}

void mn_begin_activity(void) {
    @autoreleasepool {
        if (g_activity_token != nil) {
            return;
        }
        // App Nap would slow the guest once the window is covered, and an idle Mac would sleep under a running game;
        // the activity lasts as long as the process (never balanced, it ends with the VM).
        g_activity_token = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical |
                                      NSActivityIdleDisplaySleepDisabled)
                              reason:@"MacNeutron game VM"];
        if (g_activity_token != nil) {
            CFRetain((__bridge CFTypeRef)g_activity_token);
        }
    }
}

void mn_run_appkit_main_loop(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        // No Dock icon until the game window shows (accessory, not regular).
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        mn_begin_activity();
        if (g_wake_observer == nil) {
            g_wake_observer = [[[NSWorkspace sharedWorkspace] notificationCenter]
                addObserverForName:NSWorkspaceDidWakeNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification *note) {
                            (void)note;
                            pthread_mutex_lock(&g_wake_lock);
                            mn_wake_cb cb = g_wake_cb;
                            void *ctx = g_wake_ctx;
                            pthread_mutex_unlock(&g_wake_lock);
                            if (cb != NULL) {
                                cb(ctx);
                            }
                        }];
            if (g_wake_observer != nil) {
                CFRetain((__bridge CFTypeRef)g_wake_observer);
            }
        }
        [app run];
    }
}

bool mn_gpu_is_paravirtual(void) {
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            return false;
        }
        NSString *name = [dev name];
        bool is_pv = (name != nil) &&
            ([name rangeOfString:@"paravirtual" options:NSCaseInsensitiveSearch].location != NSNotFound);
#if !__has_feature(objc_arc)
        [dev release];
#endif
        return is_pv;
    }
}

bool mn_metal_memory_info(char *name_buf, size_t name_len, uint64_t *allocated_out, uint64_t *recommended_out) {
    @autoreleasepool {
        if (name_buf && name_len > 0) {
            name_buf[0] = '\0';
        }
        if (allocated_out) {
            *allocated_out = 0;
        }
        if (recommended_out) {
            *recommended_out = 0;
        }
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) {
            return false;
        }
        if (name_buf && name_len > 0) {
            const char *utf8 = [[dev name] UTF8String];
            if (utf8) {
                strlcpy(name_buf, utf8, name_len);
            }
        }
        if (allocated_out) {
            *allocated_out = (uint64_t)[dev currentAllocatedSize];
        }
        if (recommended_out) {
            *recommended_out = (uint64_t)[dev recommendedMaxWorkingSetSize];
        }
#if !__has_feature(objc_arc)
        [dev release];
#endif
        return true;
    }
}

bool mn_task_memory_info(uint64_t *footprint_out, uint64_t *rss_out, uint64_t *compressed_out) {
    if (footprint_out) {
        *footprint_out = 0;
    }
    if (rss_out) {
        *rss_out = 0;
    }
    if (compressed_out) {
        *compressed_out = 0;
    }
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) {
        return false;
    }
    if (footprint_out) {
        *footprint_out = (uint64_t)info.phys_footprint;
    }
    if (rss_out) {
        *rss_out = (uint64_t)info.resident_size;
    }
    if (compressed_out) {
        *compressed_out = (uint64_t)info.compressed;
    }
    return true;
}
