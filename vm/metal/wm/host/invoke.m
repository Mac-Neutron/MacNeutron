/* Calls into MacNeutron's winemetal.so (marshal.h): an Objective-C exception, which a guest can cause by passing an
 * object of the wrong class, refuses the call instead of taking the VM down. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../marshal.h"
#include "loader.h"

/* A guest's NSAutoreleasePool (slot 10) is a stand-in object here, not a host pool: a drained ring runs on whichever
 * vCPU traps, so one guest thread's calls move between host threads and several guest threads' share one, while host
 * pools must be popped in order on the thread that pushed them. The handle table (htable.c) keeps the guest's pools and
 * releases what each holds; every call drains its own host pool (mn_wm_host_call). */
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *pool);
void *mn_wm_autorelease_push(void) { return objc_autoreleasePoolPush(); }
void mn_wm_autorelease_pop(void *pool) { objc_autoreleasePoolPop(pool); }

NTSTATUS_T mn_wm_invoke(unsigned slot, void *params) {
    if (slot == 10) { /* NSAutoreleasePool_alloc_init */
        *(uint64_t *)params = (uint64_t)(uintptr_t)[[NSObject alloc] init];
        return 0;
    }
    @try {
        return mn_wm_funcs[slot](params);
    } @catch (id e) {
        fprintf(stderr, "mn-winemetal: slot %u raised %s\n", slot, [[e description] UTF8String]);
        return MN_STATUS_INVALID_PARAMETER;
    }
}

void (*mn_wm_d3d_release)(void *obj);

void mn_wm_release_object(enum mn_hkind kind, void *obj) {
    switch (kind) {
    case MN_HK_OBJC:
        mn_wm_will_release(obj);
        mn_wm_invoke(1, &obj);
        return;
    case MN_HK_POOL: /* the stand-in (mn_wm_invoke): the table released what the guest's pool held */
        [(id)obj release];
        return;
    case MN_HK_HEAP_EMU: /* an MNHeap (memory.m) */
        mn_wm_invoke(1, &obj);
        return;
    case MN_HK_VIEW: /* winemetal's ReleaseMetalView: off the window, then released */
        mn_wm_invoke(73, &obj);
        return;
    case MN_HK_LISTENER: /* our wrapper (events.m) */
        mn_wm_listener_release(obj);
        return;
    case MN_HK_SM50_SHADER:
    case MN_HK_SM50_BITCODE:
    case MN_HK_SM50_ERROR: /* slots.c */
        mn_wm_sm50_release(kind, obj);
        return;
    case MN_HK_D3D: /* a native DXMT object: the Direct3D dispatcher's Release */
        if (mn_wm_d3d_release) mn_wm_d3d_release(obj);
        return;
    default:
        fprintf(stderr, "mn-winemetal: no release for a kind-%d object\n", kind);
        return;
    }
}
