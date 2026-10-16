/* Calls into MacNeutron's winemetal.so (marshal.h): an Objective-C exception, which a guest can cause by passing an
 * object of the wrong class, refuses the call instead of taking the VM down. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include "../marshal.h"
#include "loader.h"

NTSTATUS_T mn_wm_invoke(unsigned slot, void *params) {
    @try {
        return mn_wm_funcs[slot](params);
    } @catch (id e) {
        fprintf(stderr, "mn-winemetal: slot %u raised %s\n", slot, [[e description] UTF8String]);
        return MN_STATUS_INVALID_PARAMETER;
    }
}

void mn_wm_release_object(enum mn_hkind kind, void *obj) {
    switch (kind) {
    case MN_HK_OBJC:
        mn_wm_will_release(obj);
        mn_wm_invoke(1, &obj);
        return;
    case MN_HK_POOL:     /* releasing a pool drains it: the table calls this on the pool's own ring */
    case MN_HK_HEAP_EMU: /* an MNHeap (memory.m) */
        mn_wm_invoke(1, &obj);
        return;
    case MN_HK_LISTENER: /* our wrapper (events.m) */
        mn_wm_listener_release(obj);
        return;
    case MN_HK_SM50_SHADER:
    case MN_HK_SM50_BITCODE:
    case MN_HK_SM50_ERROR: /* slots.c */
        mn_wm_sm50_release(kind, obj);
        return;
    default:
        fprintf(stderr, "mn-winemetal: no release for a kind-%d object\n", kind);
        return;
    }
}
