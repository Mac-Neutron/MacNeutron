/* Calls into MacNeutron's winemetal.so (marshal.h): an Objective-C exception, which a guest can cause by passing an
 * object of the wrong class, refuses the call instead of taking the VM down. */
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "../marshal.h"
#include "loader.h"
#include "winemetal_thunks.h"

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
    if (slot == 30 || slot == 31 || slot == 32 || slot == 129 || slot == 147) {
        obj_handle_t cb_raw = 0;
        memcpy(&cb_raw, params, sizeof cb_raw);
        if (!cb_raw) return MN_STATUS_INVALID_PARAMETER;
        if (mn_wm_is_real && mn_wm_funcs[14] != NULL) {
            @try {
                struct unixcall_generic_obj_uint64_ret st = {cb_raw, 0};
                if (mn_wm_funcs[14](&st) == 0 && st.ret >= WMTCommandBufferStatusCommitted) {
                    fprintf(stderr, "mn-winemetal: slot %u refused on committed cmdbuf (status %llu)\n",
                            slot, (unsigned long long)st.ret);
                    return MN_STATUS_INVALID_PARAMETER;
                }
            } @catch (id ex) {
                (void)ex;
                return MN_STATUS_INVALID_PARAMETER;
            }
        }
    }
    if ((slot == 34 || slot == 156) && mn_wm_is_real) {
        const struct WMTRenderPipelineInfo *info =
            slot == 34 ? ((const struct unixcall_mtldevice_newrenderpso *)params)->info.ptr
                       : ((const struct unixcall_mtldevice_newrenderpso_vertex *)params)->info.ptr;
        if (!info || !info->vertex_function) {
            fprintf(stderr, "mn-winemetal: slot %u refused null vertex_function\n", slot);
            return MN_STATUS_INVALID_PARAMETER;
        }
    }
    @try {
        NTSTATUS_T s = mn_wm_funcs[slot](params);
        if (!s && slot == 159 && mn_wm_is_real) {
            struct unixcall_wmt_attach_metal_view *a = params;
            if (a->ret_layer) {
                id layer = (id)(uintptr_t)a->ret_layer;
                Class mlc = objc_getClass("CAMetalLayer");
                if (mlc && [layer isKindOfClass:mlc]) {
                    NSUInteger max_d = mn_desired_max_drawables();
                    void (^apply)(void) = ^{
                        [CATransaction begin];
                        [CATransaction setDisableActions:YES];
                        [(CAMetalLayer *)layer setMaximumDrawableCount:max_d];
                        [CATransaction commit];
                        [CATransaction flush];
                    };
                    if ([NSThread isMainThread]) apply();
                    else dispatch_sync(dispatch_get_main_queue(), apply);
                }
            }
        }
        if (!s && slot == 67) {
            const struct unixcall_generic_obj_obj_ret *nd = params;
            static _Atomic uint32_t nil_nd_logs;
            if (!nd->ret && atomic_fetch_add(&nil_nd_logs, 1) < 4)
                fprintf(stderr, "mn-winemetal: slot 67 returned nil drawable\n");
        }
        return s;
    } @catch (id e) {
        fprintf(stderr, "mn-winemetal: slot %u raised %s\n", slot, [[e description] UTF8String]);
        return MN_STATUS_INVALID_PARAMETER;
    }
}

void mn_wm_end_encoder(void *obj) {
    if (!obj || !mn_wm_funcs || !mn_wm_funcs[33]) return;
    @autoreleasepool {
        @try {
            struct unixcall_generic_obj_noret a = {(obj_handle_t)(uintptr_t)obj};
            mn_wm_funcs[33](&a);
        } @catch (id e) {
            (void)e;
        }
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
    case MN_HK_ATTACHED_VIEW: { /* WMTRetireMetalView: detach, off window, release, park = 0 */
        struct unixcall_generic_obj_uint64_noret a = {(obj_handle_t)(uintptr_t)obj, 0};
        mn_wm_invoke(160, &a);
        return;
    }
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
