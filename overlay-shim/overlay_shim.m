// Clean-room MIT overlay compatibility shim for MacNeutron.
// Bridges late-loaded gameoverlayrenderer.dylib Mach-O interpose hooks, adapts
// BGRA8Unorm overlay render pipelines to HDR/10-bit drawable formats, bridges
// Metal 4 command queue presents onto Metal 3 command buffers, and gates
// mouse/keyboard/scroll input from Wine while the overlay is active.
//
// Copyright (c) 2026 Chad Cormier Roussel (MIT License)

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#if __has_include(<ptrauth.h>)
#include <ptrauth.h>
#endif

static atomic_int g_overlay_active = 0;

static const void *const kPSODescKey = &kPSODescKey;
static const void *const kPSOCacheKey = &kPSOCacheKey;
static const void *const kEncoderColorFormatKey = &kEncoderColorFormatKey;
static const void *const kMTL4BridgeKey = &kMTL4BridgeKey;
static const void *const kDrawableBridgeKey = &kDrawableBridgeKey;

static os_unfair_lock g_swizzle_lock = OS_UNFAIR_LOCK_INIT;
static NSMutableSet<NSString *> *g_swizzled_methods = nil;
static atomic_bool g_wine_send_event_is_gameoverlay = false;
static atomic_bool g_nsapp_send_event_is_gameoverlay = false;
static atomic_bool g_wine_overrides_send_event = false;
static atomic_bool g_shim_ready = false;
static atomic_bool g_wine_input_hooked = false;

static void mn_hook_metal_device(id<MTLDevice> device);
static void mn_hook_drawable_class(Class drawableCls);
static void mn_install_input_gating(void);

#pragma mark - Pointer Authentication Helpers

static inline uintptr_t mn_strip_func_ptr(uintptr_t ptr)
{
#if __has_feature(ptrauth_calls)
    return (uintptr_t)ptrauth_strip((void *)ptr, ptrauth_key_function_pointer);
#else
    return ptr;
#endif
}

static inline uintptr_t mn_encode_got_ptr(uintptr_t replacement, bool auth_const)
{
    uintptr_t raw = mn_strip_func_ptr(replacement);
#if __has_feature(ptrauth_calls)
    if (auth_const) {
        return (uintptr_t)ptrauth_sign_unauthenticated((void *)raw, ptrauth_key_function_pointer, 0);
    }
#else
    (void)auth_const;
#endif
    return raw;
}

#pragma mark - Objective-C Swizzle Helpers

static BOOL mn_swizzle(Class cls, SEL origSel, SEL altSel, IMP replacement)
{
    if (!cls || !origSel || !altSel || !replacement) return NO;

    os_unfair_lock_lock(&g_swizzle_lock);
    if (!g_swizzled_methods) {
        g_swizzled_methods = [NSMutableSet new];
    }
    NSString *key = [NSString stringWithFormat:@"%s:%s", class_getName(cls), sel_getName(origSel)];
    if ([g_swizzled_methods containsObject:key]) {
        os_unfair_lock_unlock(&g_swizzle_lock);
        return NO;
    }

    Method origMethod = class_getInstanceMethod(cls, origSel);
    if (!origMethod) {
        os_unfair_lock_unlock(&g_swizzle_lock);
        return NO;
    }

    const char *types = method_getTypeEncoding(origMethod);
    class_addMethod(cls, origSel, method_getImplementation(origMethod), types);
    origMethod = class_getInstanceMethod(cls, origSel);

    class_addMethod(cls, altSel, replacement, types);
    Method altMethod = class_getInstanceMethod(cls, altSel);
    if (!origMethod || !altMethod) {
        os_unfair_lock_unlock(&g_swizzle_lock);
        return NO;
    }

    method_exchangeImplementations(origMethod, altMethod);
    [g_swizzled_methods addObject:key];
    os_unfair_lock_unlock(&g_swizzle_lock);
    return YES;
}

static BOOL mn_is_gameoverlay_imp(IMP imp)
{
    if (!imp) return NO;
    Dl_info info;
    if (!dladdr((const void *)imp, &info) || !info.dli_fname) return NO;
    const char *base = strrchr(info.dli_fname, '/');
    base = base ? base + 1 : info.dli_fname;
    return strcasecmp(base, "gameoverlayrenderer.dylib") == 0;
}

#pragma mark - Mach-O Late Interpose Rebinding

struct mn_interpose_pair {
    uintptr_t replacement;
    uintptr_t replacee;
};

static os_unfair_lock g_interpose_lock = OS_UNFAIR_LOCK_INIT;
static const struct mach_header_64 *g_overlay_hdr = NULL;
static struct mn_interpose_pair *g_interpose_pairs = NULL;
static size_t g_interpose_count = 0;

static const char *mn_basename(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static BOOL mn_should_skip_image(const char *path, const struct mach_header_64 *hdr, const struct mach_header_64 *overlay_hdr)
{
    if (!path || !hdr || hdr->magic != MH_MAGIC_64) return YES;
    if (hdr == overlay_hdr) return YES;
#ifdef MH_DYLIB_IN_CACHE
    if ((hdr->flags & MH_DYLIB_IN_CACHE) != 0) return YES;
#endif
    if (strncmp(path, "/System/", 8) == 0 || strncmp(path, "/usr/lib/", 9) == 0) return YES;
    const char *base = mn_basename(path);
    if (strcasecmp(base, "gameoverlayrenderer.dylib") == 0) return YES;
    if (strcasecmp(base, "libmacneutron-overlay-shim.dylib") == 0) return YES;
    return NO;
}

static BOOL mn_query_protection(mach_vm_address_t addr, vm_prot_t *out_prot)
{
    mach_vm_address_t region = addr;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t obj = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&info, &count, &obj);
    if (obj != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), obj);
    }
    if (kr != KERN_SUCCESS) return NO;
    *out_prot = info.protection;
    return YES;
}

static void mn_rebind_section(const struct mach_header_64 *hdr,
                              const char *segname,
                              const char *sectname,
                              bool auth_const,
                              const struct mn_interpose_pair *pairs,
                              size_t pair_count)
{
    unsigned long sect_bytes = 0;
    uint8_t *sect_ptr = getsectiondata(hdr, segname, sectname, &sect_bytes);
    if (!sect_ptr || sect_bytes < sizeof(uintptr_t)) return;

    uintptr_t *slots = (uintptr_t *)sect_ptr;
    size_t slot_count = sect_bytes / sizeof(uintptr_t);

    BOOL any_match = NO;
    for (size_t i = 0; i < slot_count && !any_match; i++) {
        uintptr_t cur = mn_strip_func_ptr(slots[i]);
        if (!cur) continue;
        for (size_t k = 0; k < pair_count; k++) {
            if (cur == pairs[k].replacee) {
                any_match = YES;
                break;
            }
        }
    }
    if (!any_match) return;

    const mach_vm_size_t page_sz = (mach_vm_size_t)vm_page_size;
    for (size_t i = 0; i < slot_count; i++) {
        uintptr_t cur = mn_strip_func_ptr(slots[i]);
        if (!cur) continue;

        uintptr_t replacement = 0;
        for (size_t k = 0; k < pair_count; k++) {
            if (cur == pairs[k].replacee) {
                replacement = pairs[k].replacement;
                break;
            }
        }
        if (!replacement) continue;

        mach_vm_address_t slot_addr = (mach_vm_address_t)&slots[i];
        mach_vm_address_t page_start = slot_addr & ~(page_sz - 1);
        mach_vm_address_t page_end = (slot_addr + sizeof(uintptr_t) + page_sz - 1) & ~(page_sz - 1);
        mach_vm_size_t protect_len = page_end - page_start;

        vm_prot_t orig_prot = VM_PROT_READ;
        BOOL has_orig = mn_query_protection(page_start, &orig_prot);
        BOOL changed_prot = NO;

        if (!has_orig || (orig_prot & VM_PROT_WRITE) == 0) {
            kern_return_t kr = mach_vm_protect(mach_task_self(), page_start, protect_len, FALSE,
                                               VM_PROT_READ | VM_PROT_WRITE);
            if (kr != KERN_SUCCESS) {
                kr = mach_vm_protect(mach_task_self(), page_start, protect_len, FALSE,
                                     VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
            }
            if (kr != KERN_SUCCESS) continue;
            changed_prot = YES;
        }

        slots[i] = mn_encode_got_ptr(replacement, auth_const);

        if (changed_prot && has_orig) {
            (void)mach_vm_protect(mach_task_self(), page_start, protect_len, FALSE, orig_prot);
        }
    }
}

static void mn_rebind_image(const struct mach_header_64 *hdr,
                            const struct mn_interpose_pair *pairs,
                            size_t count)
{
    mn_rebind_section(hdr, "__DATA", "__la_symbol_ptr", false, pairs, count);
    mn_rebind_section(hdr, "__DATA", "__nl_symbol_ptr", false, pairs, count);
    mn_rebind_section(hdr, "__DATA", "__got", false, pairs, count);
    mn_rebind_section(hdr, "__DATA_CONST", "__got", false, pairs, count);
    mn_rebind_section(hdr, "__AUTH_CONST", "__got", true, pairs, count);
}

static void mn_on_image_added(const struct mach_header *mh, intptr_t vmaddr_slide)
{
    (void)vmaddr_slide;
    if (!mh || mh->magic != MH_MAGIC_64) return;
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)mh;

    Dl_info info;
    const char *path = (dladdr((const void *)hdr, &info) && info.dli_fname) ? info.dli_fname : NULL;

    os_unfair_lock_lock(&g_interpose_lock);
    const struct mach_header_64 *overlay_hdr = g_overlay_hdr;
    const struct mn_interpose_pair *pairs = g_interpose_pairs;
    size_t count = g_interpose_count;
    os_unfair_lock_unlock(&g_interpose_lock);

    if (pairs && count > 0 && path && !mn_should_skip_image(path, hdr, overlay_hdr)) {
        mn_rebind_image(hdr, pairs, count);
    }

    if (atomic_load_explicit(&g_shim_ready, memory_order_acquire)
        && !atomic_load_explicit(&g_wine_input_hooked, memory_order_acquire)
        && path && !mn_should_skip_image(path, hdr, overlay_hdr)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            mn_install_input_gating();
        });
    }
}

static void mn_apply_overlay_interpose(const struct mach_header_64 *overlay_hdr)
{
    if (!overlay_hdr || overlay_hdr->magic != MH_MAGIC_64) return;

    unsigned long interpose_bytes = 0;
    const uint8_t *raw = getsectiondata(overlay_hdr, "__DATA", "__interpose", &interpose_bytes);
    if (!raw || interpose_bytes == 0) {
        raw = getsectiondata(overlay_hdr, "__DATA_CONST", "__interpose", &interpose_bytes);
    }
    if (!raw || interpose_bytes < sizeof(struct mn_interpose_pair)) return;

    size_t raw_count = interpose_bytes / sizeof(struct mn_interpose_pair);
    const struct mn_interpose_pair *src_pairs = (const struct mn_interpose_pair *)raw;
    struct mn_interpose_pair *pairs = calloc(raw_count, sizeof(struct mn_interpose_pair));
    if (!pairs) return;

    size_t count = 0;
    for (size_t i = 0; i < raw_count; i++) {
        uintptr_t rep = src_pairs[i].replacement;
        uintptr_t orig = mn_strip_func_ptr(src_pairs[i].replacee);
        if (rep && orig) {
            pairs[count].replacement = rep;
            pairs[count].replacee = orig;
            count++;
        }
    }
    if (count == 0) {
        free(pairs);
        return;
    }

    os_unfair_lock_lock(&g_interpose_lock);
    if (g_interpose_pairs) {
        os_unfair_lock_unlock(&g_interpose_lock);
        free(pairs);
        return;
    }
    g_overlay_hdr = overlay_hdr;
    g_interpose_pairs = pairs;
    g_interpose_count = count;
    os_unfair_lock_unlock(&g_interpose_lock);

    static dispatch_once_t add_image_once;
    dispatch_once(&add_image_once, ^{
        _dyld_register_func_for_add_image(mn_on_image_added);
    });
}

#pragma mark - HDR / 10-bit Pipeline Format Adapter

static BOOL mn_is_bgra8_format(MTLPixelFormat fmt)
{
    return fmt == MTLPixelFormatBGRA8Unorm || fmt == MTLPixelFormatBGRA8Unorm_sRGB;
}

static BOOL mn_needs_format_adaptation(MTLPixelFormat targetFormat)
{
    switch (targetFormat) {
    case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRGB10A2Unorm:
    case MTLPixelFormatBGR10A2Unorm:
    case MTLPixelFormatRGBA16Unorm:
    case MTLPixelFormatBGRA10_XR:
    case MTLPixelFormatBGRA10_XR_sRGB:
    case MTLPixelFormatBGR10_XR:
    case MTLPixelFormatBGR10_XR_sRGB:
    case MTLPixelFormatRGBA8Unorm:
    case MTLPixelFormatRGBA8Unorm_sRGB:
        return YES;
    default:
        return NO;
    }
}

static BOOL mn_is_overlay_pso_descriptor(MTLRenderPipelineDescriptor *descriptor)
{
    if (!descriptor) return NO;
    if (!mn_is_bgra8_format(descriptor.colorAttachments[0].pixelFormat)) return NO;
    for (NSUInteger i = 1; i < 8; i++) {
        if (descriptor.colorAttachments[i].pixelFormat != MTLPixelFormatInvalid) return NO;
    }
    if (descriptor.depthAttachmentPixelFormat != MTLPixelFormatInvalid) return NO;
    if (descriptor.stencilAttachmentPixelFormat != MTLPixelFormatInvalid) return NO;
    if (descriptor.rasterSampleCount != 1) return NO;
    return YES;
}

static id<MTLRenderPipelineState> mn_newRenderPipelineState(id self,
                                                            SEL _cmd,
                                                            MTLRenderPipelineDescriptor *descriptor,
                                                            NSError * __autoreleasing *error)
{
    (void)_cmd;
    id<MTLRenderPipelineState> pso = ((id<MTLRenderPipelineState> (*)(id, SEL, MTLRenderPipelineDescriptor *, NSError * __autoreleasing *))objc_msgSend)(
        self, @selector(mn_orig_newRenderPipelineStateWithDescriptor:error:), descriptor, error);
    if (pso && mn_is_overlay_pso_descriptor(descriptor)) {
        MTLRenderPipelineDescriptor *copy = [descriptor copy];
        objc_setAssociatedObject(pso, kPSODescKey, copy, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return pso;
}

static void mn_setRenderPipelineState(id self, SEL _cmd, id<MTLRenderPipelineState> pipelineState)
{
    (void)_cmd;
    id<MTLRenderPipelineState> selected = pipelineState;
    NSNumber *fmtNum = objc_getAssociatedObject(self, kEncoderColorFormatKey);
    if (fmtNum && pipelineState) {
        MTLPixelFormat targetFormat = (MTLPixelFormat)[fmtNum unsignedIntegerValue];
        if (mn_needs_format_adaptation(targetFormat)) {
            MTLRenderPipelineDescriptor *origDesc = objc_getAssociatedObject(pipelineState, kPSODescKey);
            if (origDesc && origDesc.colorAttachments[0].pixelFormat != targetFormat) {
                id cached = nil;
                @synchronized(pipelineState) {
                    NSMutableDictionary<NSNumber *, id> *cache =
                        objc_getAssociatedObject(pipelineState, kPSOCacheKey);
                    if (!cache) {
                        cache = [NSMutableDictionary new];
                        objc_setAssociatedObject(pipelineState, kPSOCacheKey, cache, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    }
                    cached = cache[@(targetFormat)];
                    if (!cached) {
                        MTLRenderPipelineDescriptor *patched = [origDesc copy];
                        patched.colorAttachments[0].pixelFormat = targetFormat;
                        NSError *err = nil;
                        id<MTLRenderPipelineState> variant =
                            [pipelineState.device newRenderPipelineStateWithDescriptor:patched error:&err];
                        cached = variant ? (id)variant : (id)[NSNull null];
                        cache[@(targetFormat)] = cached;
                    }
                }
                if (cached && cached != (id)[NSNull null]) {
                    selected = (id<MTLRenderPipelineState>)cached;
                }
            }
        }
    }
    ((void (*)(id, SEL, id<MTLRenderPipelineState>))objc_msgSend)(
        self, @selector(mn_orig_setRenderPipelineState:), selected);
}

static void mn_endEncoding(id self, SEL _cmd)
{
    (void)_cmd;
    objc_setAssociatedObject(self, kEncoderColorFormatKey, nil, OBJC_ASSOCIATION_ASSIGN);
    ((void (*)(id, SEL))objc_msgSend)(self, @selector(mn_orig_endEncoding));
}

static void mn_hook_render_encoder_class(Class encoderCls)
{
    if (!encoderCls) return;
    mn_swizzle(encoderCls,
               @selector(setRenderPipelineState:),
               @selector(mn_orig_setRenderPipelineState:),
               (IMP)mn_setRenderPipelineState);
    mn_swizzle(encoderCls,
               @selector(endEncoding),
               @selector(mn_orig_endEncoding),
               (IMP)mn_endEncoding);
}

static id<MTLRenderCommandEncoder> mn_renderCommandEncoderWithDescriptor(id self,
                                                                        SEL _cmd,
                                                                        MTLRenderPassDescriptor *descriptor)
{
    (void)_cmd;
    id<MTLRenderCommandEncoder> encoder = ((id<MTLRenderCommandEncoder> (*)(id, SEL, MTLRenderPassDescriptor *))objc_msgSend)(
        self, @selector(mn_orig_renderCommandEncoderWithDescriptor:), descriptor);
    if (encoder) {
        mn_hook_render_encoder_class(object_getClass(encoder));
        id<MTLTexture> color0 = descriptor.colorAttachments[0].texture;
        if (color0 && color0.pixelFormat != MTLPixelFormatInvalid) {
            objc_setAssociatedObject(encoder, kEncoderColorFormatKey, @(color0.pixelFormat), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
    }
    return encoder;
}

static void mn_hook_command_buffer_class(Class cbCls)
{
    if (!cbCls) return;
    mn_swizzle(cbCls,
               @selector(renderCommandEncoderWithDescriptor:),
               @selector(mn_orig_renderCommandEncoderWithDescriptor:),
               (IMP)mn_renderCommandEncoderWithDescriptor);
}

#pragma mark - Metal 4 Present Bridge

@interface MNMTL4QueueBridge : NSObject
@property (nonatomic, strong) id<MTLCommandQueue> m3Queue;
@property (nonatomic, strong) id<MTLSharedEvent> event;
@property (nonatomic, assign) uint64_t counter;
@end

@implementation MNMTL4QueueBridge
@end

@interface MNDrawableBridge : NSObject
@property (nonatomic, strong) id<MTLCommandQueue> m3Queue;
@property (nonatomic, strong) id<MTLSharedEvent> event;
@property (nonatomic, assign) uint64_t waitValue;
@end

@implementation MNDrawableBridge
@end

static MNMTL4QueueBridge *mn_bridge_for_mtl4_queue(id<MTL4CommandQueue> queue4)
{
    if (!queue4) return nil;
    MNMTL4QueueBridge *bridge = objc_getAssociatedObject(queue4, kMTL4BridgeKey);
    if (bridge) return bridge;
    @synchronized(queue4) {
        bridge = objc_getAssociatedObject(queue4, kMTL4BridgeKey);
        if (!bridge) {
            id<MTLDevice> device = queue4.device;
            id<MTLCommandQueue> m3Queue = [device newCommandQueue];
            id<MTLSharedEvent> event = [device newSharedEvent];
            if (m3Queue && event) {
                bridge = [MNMTL4QueueBridge new];
                bridge.m3Queue = m3Queue;
                bridge.event = event;
                bridge.counter = 0;
                objc_setAssociatedObject(queue4, kMTL4BridgeKey, bridge, OBJC_ASSOCIATION_RETAIN);
            }
        }
    }
    return bridge;
}

static void mn_drawable_present_common(id<MTLDrawable> drawable,
                                       MNDrawableBridge *bridge,
                                       void (^presentCall)(id<MTLCommandBuffer> cb),
                                       void (^fallbackCall)(void))
{
    if (!bridge) {
        fallbackCall();
        return;
    }
    objc_setAssociatedObject(drawable, kDrawableBridgeKey, nil, OBJC_ASSOCIATION_RETAIN);
    id<MTLCommandBuffer> cb = [bridge.m3Queue commandBuffer];
    if (cb) {
        [cb encodeWaitForEvent:bridge.event value:bridge.waitValue];
        presentCall(cb);
        [cb commit];
    } else {
        fallbackCall();
    }
}

static void mn_drawable_present(id self, SEL _cmd)
{
    (void)_cmd;
    MNDrawableBridge *bridge = objc_getAssociatedObject(self, kDrawableBridgeKey);
    if (bridge) {
        mn_drawable_present_common(self, bridge, ^(id<MTLCommandBuffer> cb) {
            [cb presentDrawable:self];
        }, ^{
            ((void (*)(id, SEL))objc_msgSend)(self, @selector(mn_orig_drawable_present));
        });
        return;
    }
    ((void (*)(id, SEL))objc_msgSend)(self, @selector(mn_orig_drawable_present));
}

static void mn_drawable_presentAtTime(id self, SEL _cmd, CFTimeInterval presentationTime)
{
    (void)_cmd;
    MNDrawableBridge *bridge = objc_getAssociatedObject(self, kDrawableBridgeKey);
    if (bridge) {
        mn_drawable_present_common(self, bridge, ^(id<MTLCommandBuffer> cb) {
            [cb presentDrawable:self atTime:presentationTime];
        }, ^{
            ((void (*)(id, SEL, CFTimeInterval))objc_msgSend)(
                self, @selector(mn_orig_drawable_presentAtTime:), presentationTime);
        });
        return;
    }
    ((void (*)(id, SEL, CFTimeInterval))objc_msgSend)(self, @selector(mn_orig_drawable_presentAtTime:), presentationTime);
}

static void mn_drawable_presentAfterMinimumDuration(id self, SEL _cmd, CFTimeInterval duration)
{
    (void)_cmd;
    MNDrawableBridge *bridge = objc_getAssociatedObject(self, kDrawableBridgeKey);
    if (bridge) {
        mn_drawable_present_common(self, bridge, ^(id<MTLCommandBuffer> cb) {
            [cb presentDrawable:self afterMinimumDuration:duration];
        }, ^{
            ((void (*)(id, SEL, CFTimeInterval))objc_msgSend)(
                self, @selector(mn_orig_drawable_presentAfterMinimumDuration:), duration);
        });
        return;
    }
    ((void (*)(id, SEL, CFTimeInterval))objc_msgSend)(
        self, @selector(mn_orig_drawable_presentAfterMinimumDuration:), duration);
}

static void mn_hook_drawable_class(Class drawableCls)
{
    if (!drawableCls) return;
    mn_swizzle(drawableCls,
               @selector(present),
               @selector(mn_orig_drawable_present),
               (IMP)mn_drawable_present);
    mn_swizzle(drawableCls,
               @selector(presentAtTime:),
               @selector(mn_orig_drawable_presentAtTime:),
               (IMP)mn_drawable_presentAtTime);
    mn_swizzle(drawableCls,
               @selector(presentAfterMinimumDuration:),
               @selector(mn_orig_drawable_presentAfterMinimumDuration:),
               (IMP)mn_drawable_presentAfterMinimumDuration);
}

static void mn_mtl4_signalDrawable(id self, SEL _cmd, id<MTLDrawable> drawable)
{
    (void)_cmd;
    MNMTL4QueueBridge *bridge = mn_bridge_for_mtl4_queue(self);
    if (bridge && drawable) {
        mn_hook_drawable_class(object_getClass(drawable));
        uint64_t val = 0;
        @synchronized(bridge) {
            val = ++bridge.counter;
        }
        [self signalEvent:bridge.event value:val];
        MNDrawableBridge *db = [MNDrawableBridge new];
        db.m3Queue = bridge.m3Queue;
        db.event = bridge.event;
        db.waitValue = val;
        objc_setAssociatedObject(drawable, kDrawableBridgeKey, db, OBJC_ASSOCIATION_RETAIN);
        return;
    }
    ((void (*)(id, SEL, id<MTLDrawable>))objc_msgSend)(self, @selector(mn_orig_mtl4_signalDrawable:), drawable);
}

static void mn_hook_mtl4_queue_class(Class queue4Cls)
{
    if (!queue4Cls) return;
    mn_swizzle(queue4Cls,
               @selector(signalDrawable:),
               @selector(mn_orig_mtl4_signalDrawable:),
               (IMP)mn_mtl4_signalDrawable);
}

static id<MTL4CommandQueue> mn_newMTL4CommandQueue(id self, SEL _cmd)
{
    (void)_cmd;
    id<MTL4CommandQueue> q = ((id<MTL4CommandQueue> (*)(id, SEL))objc_msgSend)(
        self, @selector(mn_orig_newMTL4CommandQueue));
    if (q) {
        mn_hook_mtl4_queue_class(object_getClass(q));
    }
    return q;
}

static id<MTL4CommandQueue> mn_newMTL4CommandQueueWithDescriptor(id self,
                                                                 SEL _cmd,
                                                                 MTL4CommandQueueDescriptor *descriptor,
                                                                 NSError * __autoreleasing *error)
{
    (void)_cmd;
    id<MTL4CommandQueue> q = ((id<MTL4CommandQueue> (*)(id, SEL, MTL4CommandQueueDescriptor *, NSError * __autoreleasing *))objc_msgSend)(
        self, @selector(mn_orig_newMTL4CommandQueueWithDescriptor:error:), descriptor, error);
    if (q) {
        mn_hook_mtl4_queue_class(object_getClass(q));
    }
    return q;
}

#pragma mark - Device & CAMetalLayer Hooks

static void mn_hook_metal_device(id<MTLDevice> device)
{
    if (!device) return;
    Class devCls = object_getClass(device);
    if (!mn_swizzle(devCls,
                    @selector(newRenderPipelineStateWithDescriptor:error:),
                    @selector(mn_orig_newRenderPipelineStateWithDescriptor:error:),
                    (IMP)mn_newRenderPipelineState)) {
        return;
    }

    @autoreleasepool {
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        if (cb) {
            mn_hook_command_buffer_class(object_getClass(cb));
        }
    }

    if ([device respondsToSelector:@selector(newMTL4CommandQueue)]) {
        mn_swizzle(devCls,
                   @selector(newMTL4CommandQueue),
                   @selector(mn_orig_newMTL4CommandQueue),
                   (IMP)mn_newMTL4CommandQueue);
        mn_swizzle(devCls,
                   @selector(newMTL4CommandQueueWithDescriptor:error:),
                   @selector(mn_orig_newMTL4CommandQueueWithDescriptor:error:),
                   (IMP)mn_newMTL4CommandQueueWithDescriptor);
        @autoreleasepool {
            id<MTL4CommandQueue> q4 = [device newMTL4CommandQueue];
            if (q4) {
                mn_hook_mtl4_queue_class(object_getClass(q4));
            }
        }
    }
}

static void mn_layer_setPixelFormat(CAMetalLayer *self, SEL _cmd, MTLPixelFormat pixelFormat)
{
    (void)_cmd;
    ((void (*)(id, SEL, MTLPixelFormat))objc_msgSend)(self, @selector(mn_orig_setPixelFormat:), pixelFormat);
    if (self.device) {
        mn_hook_metal_device(self.device);
    }
}

static void mn_layer_setDevice(CAMetalLayer *self, SEL _cmd, id<MTLDevice> device)
{
    (void)_cmd;
    ((void (*)(id, SEL, id<MTLDevice>))objc_msgSend)(self, @selector(mn_orig_setDevice:), device);
    if (device) {
        mn_hook_metal_device(device);
    }
}

static void mn_install_metal_hooks(void)
{
    mn_swizzle([CAMetalLayer class],
               @selector(setPixelFormat:),
               @selector(mn_orig_setPixelFormat:),
               (IMP)mn_layer_setPixelFormat);
    mn_swizzle([CAMetalLayer class],
               @selector(setDevice:),
               @selector(mn_orig_setDevice:),
               (IMP)mn_layer_setDevice);

    @autoreleasepool {
        id<MTLDevice> defaultDev = MTLCreateSystemDefaultDevice();
        if (defaultDev) {
            mn_hook_metal_device(defaultDev);
        }
    }
}

#pragma mark - Input Gating (WineApplication / WineWindow / NSApplication)

static BOOL mn_is_gated_input_event(NSEvent *event)
{
    if (!event) return NO;
    switch (event.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
    case NSEventTypeMouseEntered:
    case NSEventTypeMouseExited:
    case NSEventTypeScrollWheel:
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
    case NSEventTypeFlagsChanged:
        return YES;
    default:
        return NO;
    }
}

static void mn_wine_sendEvent(id self, SEL _cmd, NSEvent *event)
{
    if (atomic_load_explicit(&g_overlay_active, memory_order_acquire) != 0 && mn_is_gated_input_event(event)) {
        if (atomic_load_explicit(&g_wine_send_event_is_gameoverlay, memory_order_acquire)) {
            // gameoverlayrenderer.dylib hooked WineApplication directly: let it handle and consume the event.
            ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_wine_sendEvent:), event);
            return;
        }
        BOOL nsAppIsOverlay = atomic_load_explicit(&g_nsapp_send_event_is_gameoverlay, memory_order_acquire);
        if (atomic_load_explicit(&g_wine_overrides_send_event, memory_order_acquire)) {
            if (nsAppIsOverlay) {
                // Bypass WineApplication's event translation while forwarding to NSApplication (where Steam Overlay lives).
                struct objc_super sup = { self, [NSApplication class] };
                ((void (*)(struct objc_super *, SEL, NSEvent *))objc_msgSendSuper)(&sup, _cmd, event);
            }
            return;
        }
        if (nsAppIsOverlay) {
            ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_wine_sendEvent:), event);
        }
        return;
    }
    ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_wine_sendEvent:), event);
}

static void mn_nsapp_sendEvent(id self, SEL _cmd, NSEvent *event)
{
    (void)_cmd;
    // Never drop/blackhole events before gameoverlayrenderer.dylib sees them; rely on Wine handler gating.
    ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_nsapp_sendEvent:), event);
}

static void mn_wine_window_keyDown(id self, SEL _cmd, NSEvent *event)
{
    (void)_cmd;
    if (atomic_load_explicit(&g_overlay_active, memory_order_acquire) != 0) {
        return;
    }
    ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_wine_window_keyDown:), event);
}

static void mn_wine_window_flagsChanged(id self, SEL _cmd, NSEvent *event)
{
    (void)_cmd;
    if (atomic_load_explicit(&g_overlay_active, memory_order_acquire) != 0) {
        return;
    }
    ((void (*)(id, SEL, NSEvent *))objc_msgSend)(self, @selector(mn_orig_wine_window_flagsChanged:), event);
}

static void mn_install_input_gating(void)
{
    Class nsAppCls = [NSApplication class];
    Class wineAppCls = NSClassFromString(@"WineApplication");
    Class wineWinCls = NSClassFromString(@"WineWindow");

    Method nsAppMethod = class_getInstanceMethod(nsAppCls, @selector(sendEvent:));
    Method origNsAppMethod = class_getInstanceMethod(nsAppCls, @selector(mn_orig_nsapp_sendEvent:));
    IMP nsAppImp = origNsAppMethod ? method_getImplementation(origNsAppMethod)
                                   : (nsAppMethod ? method_getImplementation(nsAppMethod) : NULL);
    atomic_store_explicit(&g_nsapp_send_event_is_gameoverlay, mn_is_gameoverlay_imp(nsAppImp), memory_order_release);

    if (wineAppCls && wineAppCls != nsAppCls) {
        Method wineMethod = class_getInstanceMethod(wineAppCls, @selector(sendEvent:));
        Method origWineMethod = class_getInstanceMethod(wineAppCls, @selector(mn_orig_wine_sendEvent:));
        BOOL overrides = (wineMethod && wineMethod != nsAppMethod);
        IMP wineImp = origWineMethod ? method_getImplementation(origWineMethod)
                                     : (overrides ? method_getImplementation(wineMethod) : NULL);
        atomic_store_explicit(&g_wine_overrides_send_event, overrides, memory_order_release);
        atomic_store_explicit(&g_wine_send_event_is_gameoverlay, mn_is_gameoverlay_imp(wineImp), memory_order_release);
        mn_swizzle(wineAppCls,
                   @selector(sendEvent:),
                   @selector(mn_orig_wine_sendEvent:),
                   (IMP)mn_wine_sendEvent);
    } else if (nsAppCls) {
        mn_swizzle(nsAppCls,
                   @selector(sendEvent:),
                   @selector(mn_orig_nsapp_sendEvent:),
                   (IMP)mn_nsapp_sendEvent);
    }

    if (wineWinCls) {
        mn_swizzle(wineWinCls,
                   @selector(keyDown:),
                   @selector(mn_orig_wine_window_keyDown:),
                   (IMP)mn_wine_window_keyDown);
        mn_swizzle(wineWinCls,
                   @selector(flagsChanged:),
                   @selector(mn_orig_wine_window_flagsChanged:),
                   (IMP)mn_wine_window_flagsChanged);
    }

    if (wineAppCls && wineWinCls) {
        atomic_store_explicit(&g_wine_input_hooked, true, memory_order_release);
    }
}

#pragma mark - Public C ABI

__attribute__((visibility("default")))
void mn_overlay_shim_install(void)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        const char *overlay_path = NULL;
        const struct mach_header_64 *overlay_hdr = NULL;
        uint32_t image_count = _dyld_image_count();
        for (uint32_t i = 0; i < image_count; i++) {
            const char *path = _dyld_get_image_name(i);
            if (!path) continue;
            if (strcasecmp(mn_basename(path), "gameoverlayrenderer.dylib") == 0) {
                const struct mach_header_64 *hdr = (const struct mach_header_64 *)_dyld_get_image_header(i);
                if (hdr && hdr->magic == MH_MAGIC_64) {
                    overlay_path = path;
                    overlay_hdr = hdr;
                    break;
                }
            }
        }
        if (!overlay_hdr || !overlay_path) {
            return;
        }

        mn_apply_overlay_interpose(overlay_hdr);

        void *handle = dlopen(overlay_path, RTLD_NOLOAD | RTLD_LAZY);
        void (*steammetalhook_init)(void) = handle ? (void (*)(void))dlsym(handle, "steammetalhook_init") : NULL;
        if (handle) {
            dlclose(handle);
        }

        void (^run_on_main)(void) = ^{
            if (steammetalhook_init) {
                steammetalhook_init();
            }
            mn_install_metal_hooks();
            mn_install_input_gating();
            atomic_store_explicit(&g_shim_ready, true, memory_order_release);
        };

        if ([NSThread isMainThread]) {
            run_on_main();
        } else {
            dispatch_async(dispatch_get_main_queue(), run_on_main);
        }
    });
}

__attribute__((visibility("default")))
void mn_overlay_shim_set_active(int active)
{
    atomic_store_explicit(&g_overlay_active, active != 0 ? 1 : 0, memory_order_release);
}
