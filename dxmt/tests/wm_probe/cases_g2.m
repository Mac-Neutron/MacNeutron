/* wm_probe's g2 cases (slots.txt's g2; probe.h), recorded from DXMT's C: resources, heaps, residency, indirect
 * command buffers (Task 8). */
#include "probe.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers (g2 only) ---- */

/* A handle the case goes on with: 0 or the fill pattern stops it ("bad-handle"). */
static id need(const char *name, uint64_t h) {
    if (!h || h == WM_FILL64) wm_h(name, h);
    return (id)h;
}

/* A recording device forwarding to the probe's. */
static id rec_device(void) {
    id d = wm_rec(wm_device, @protocol(MTLDevice));
    wm_in(d);
    return d;
}

static id<MTLBuffer> make_buffer(NSUInteger length, MTLResourceOptions options) {
    return [[wm_device newBufferWithLength:length options:options] autorelease];
}

static id<MTLTexture> make_texture(MTLTextureType type, MTLPixelFormat format, NSUInteger w, NSUInteger h,
                                   NSUInteger levels, NSUInteger slices, MTLTextureUsage usage,
                                   MTLStorageMode storage) {
    MTLTextureDescriptor *d = [[[MTLTextureDescriptor alloc] init] autorelease];
    d.textureType = type;
    d.pixelFormat = format;
    d.width = w;
    d.height = h;
    d.mipmapLevelCount = levels;
    d.arrayLength = slices;
    d.usage = usage;
    d.storageMode = storage;
    return [[wm_device newTextureWithDescriptor:d] autorelease];
}

static void hex(const char *name, const void *p, size_t n) {
    char *s = malloc(2 * n + 1);
    for (size_t i = 0; i < n; i++) snprintf(s + 2 * i, 3, "%02x", ((const uint8_t *)p)[i]);
    wm_val(name, "%s", n ? s : "-");
    free(s);
}

/* 39-44 on one handle: each getter's value on its own line. */
static void getters(uint64_t h) {
    static const char *const names[] = {"pixelFormat", "width", "height", "depth", "arrayLength", "mipmapLevelCount"};
    for (unsigned slot = 39; slot <= 44; slot++) {
        struct unixcall_generic_obj_uint64_ret q;
        wm_fill(&q, sizeof q);
        q.handle = h;
        wm_call(slot, &q);
        wm_val(names[slot - 39], "0x%llx", (unsigned long long)q.ret);
    }
}

/* ---- 18 ---- */

/* 18 through a recording device (the probe's own for NoCopy: the recorder would print the whole memory): the message,
 * ret's ownership, the info fields it writes. */
static void new_buffer(uint64_t length, uint64_t options, void *memory) {
    id dev = memory ? wm_device : rec_device();
    struct WMTBufferInfo info;
    wm_fill(&info, sizeof info);
    info.length = length;
    info.options = options;
    info.memory.ptr = memory;
    struct unixcall_mtldevice_newbuffer p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(18, &p);
    id<MTLBuffer> b = need("ret", p.ret);
    wm_val("length", "%llu", (unsigned long long)info.length);
    wm_val("options", "%llu", (unsigned long long)info.options);
    if (memory) wm_eq("memory", (uint64_t)info.memory.ptr, (uint64_t)memory, "page");
    else if (!info.memory.ptr) wm_val("memory", "0");
    else wm_eq("memory", (uint64_t)info.memory.ptr, (uint64_t)b.contents, "contents");
    wm_eq("gpu_address", info.gpu_address, b.gpuAddress, "gpuAddress");
    wm_val("storageMode", "%lu", (unsigned long)b.storageMode);
    wm_val("buffer.length", "%lu", (unsigned long)b.length);
    if (memory) wm_eq("contents", (uint64_t)b.contents, (uint64_t)memory, "page");
}

static void buffer_shared(void) { new_buffer(4096, WMTResourceStorageModeShared | WMTResourceCPUCacheModeWriteCombined, NULL); }
static void buffer_private(void) { new_buffer(65536, WMTResourceStorageModePrivate, NULL); }

/* NoCopy over memory that outlives the buffer (the probe keeps it to the case's end; no deallocator). */
static uint8_t nocopy_page[2 * 16384] __attribute__((aligned(16384)));
static void buffer_nocopy(void) {
    memset(nocopy_page, 0x3c, sizeof nocopy_page);
    new_buffer(sizeof nocopy_page, WMTResourceStorageModeShared, nocopy_page);
}

/* ---- 19 ---- */

/* M9: gpu_resource_id is always written: 0 without argument buffers, the sampler's id with them. */
static void sampler(void) {
    id dev = rec_device();
    for (int ab = 0; ab < 2; ab++) {
        struct WMTSamplerInfo info;
        wm_fill(&info, sizeof info);
        info.min_filter = ab ? WMTSamplerMinMagFilterNearest : WMTSamplerMinMagFilterLinear;
        info.mag_filter = ab ? WMTSamplerMinMagFilterLinear : WMTSamplerMinMagFilterNearest;
        info.mip_filter = ab ? WMTSamplerMipFilterNearest : WMTSamplerMipFilterLinear;
        info.r_address_mode = ab ? WMTSamplerAddressModeClampToBorderColor : WMTSamplerAddressModeMirrorClampToEdge;
        info.s_address_mode = ab ? WMTSamplerAddressModeClampToZero : WMTSamplerAddressModeRepeat;
        info.t_address_mode = ab ? WMTSamplerAddressModeClampToEdge : WMTSamplerAddressModeMirrorRepeat;
        info.border_color = ab ? WMTSamplerBorderColorOpaqueBlack : WMTSamplerBorderColorOpaqueWhite;
        info.compare_function = ab ? WMTCompareFunctionLessEqual : WMTCompareFunctionNotEqual;
        info.lod_min_clamp = ab ? 1.0f : 0.5f;
        info.lod_max_clamp = ab ? 2.0f : 7.25f;
        info.max_anisotroy = ab ? 1 : 3;
        info.normalized_coords = 1;
        info.lod_average = (bool)ab;
        info.support_argument_buffers = (bool)ab;
        struct unixcall_mtldevice_newsamplerstate p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        wm_own("ret", &p.ret);
        wm_call(19, &p);
        id<MTLSamplerState> s = need("ret", p.ret);
        if (ab) wm_eq("gpu_resource_id", info.gpu_resource_id, s.gpuResourceID._impl, "gpuResourceID");
        else wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
        wm_bytes("info", &info, offsetof(struct WMTSamplerInfo, gpu_resource_id)); /* byte 23 is padding */
    }
}

/* ---- 20 ---- */

static void depth_stencil(void) {
    id dev = rec_device();
    for (int k = 0; k < 3; k++) { /* neither stencil, front only, back only */
        struct WMTDepthStencilInfo info;
        wm_fill(&info, sizeof info);
        info.depth_compare_function = k == 1 ? WMTCompareFunctionGreater : WMTCompareFunctionLessEqual;
        info.depth_write_enabled = k != 1;
        info.front_stencil = (struct WMTStencilInfo){k == 1, WMTStencilOperationReplace, WMTStencilOperationInvert,
                                                    WMTStencilOperationIncrementWrap, WMTCompareFunctionEqual, 0x5a, 0x3c};
        info.back_stencil = (struct WMTStencilInfo){k == 2, WMTStencilOperationDecrementClamp, WMTStencilOperationZero,
                                                   WMTStencilOperationDecrementWrap, WMTCompareFunctionGreaterEqual, 0x0f,
                                                   0xf0};
        struct unixcall_mtldevice_newdepthstencilstate p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        wm_own("ret", &p.ret);
        wm_call(20, &p);
        need("ret", p.ret);
    }
}

/* ---- 21, 39-44 ---- */

static struct WMTTextureInfo texture_info(enum WMTPixelFormat format, enum WMTTextureType type, uint32_t w, uint32_t h,
                                          uint32_t d, uint32_t slices, uint32_t levels, uint32_t samples,
                                          enum WMTTextureUsage usage, uint64_t options, uint32_t flags) {
    struct WMTTextureInfo info;
    wm_fill(&info, sizeof info);
    info.pixel_format = format;
    info.width = w;
    info.height = h;
    info.depth = d;
    info.array_length = slices;
    info.type = type;
    info.mipmap_level_count = levels;
    info.sample_count = samples;
    info.usage = usage;
    info.options = (enum WMTResourceOptions)options;
    info.flags = flags;
    return info;
}

/* After 21, 22 or 142: gpu_resource_id, mach_port (written 0), the inputs left alone. */
static void texture_out(struct WMTTextureInfo *info, const struct WMTTextureInfo *before, id<MTLTexture> t) {
    wm_eq("gpu_resource_id", info->gpu_resource_id, t.gpuResourceID._impl, "gpuResourceID");
    wm_val("mach_port", "0x%x", info->mach_port);
    wm_val("inputs", "%s", memcmp(info, before, offsetof(struct WMTTextureInfo, mach_port)) ? "changed" : "unchanged");
}

static void new_texture(struct WMTTextureInfo info) {
    id dev = rec_device();
    struct WMTTextureInfo before = info;
    struct unixcall_mtldevice_newtexture p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(21, &p);
    id<MTLTexture> t = need("ret", p.ret);
    texture_out(&info, &before, t);
    wm_val("compressionType", "%ld", (long)t.compressionType);
    wm_val("textureType", "%lu", (unsigned long)t.textureType);
    wm_val("sampleCount", "%lu", (unsigned long)t.sampleCount);
    wm_val("usage", "%lu", (unsigned long)t.usage);
    wm_val("storageMode", "%lu", (unsigned long)t.storageMode);
    wm_val("hazardTrackingMode", "%lu", (unsigned long)t.hazardTrackingMode);
    getters(p.ret);
}

static void texture_2d(void) {
    new_texture(texture_info(WMTPixelFormatBGRA8Unorm, WMTTextureType2D, 64, 32, 1, 1, 3, 1,
                             WMTTextureUsageShaderRead | WMTTextureUsageRenderTarget, WMTResourceStorageModePrivate, 0));
}
static void texture_3d(void) {
    new_texture(texture_info(WMTPixelFormatRGBA16Float, WMTTextureType3D, 16, 8, 4, 1, 2, 1,
                             WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite, WMTResourceStorageModeShared, 0));
}
static void texture_array(void) {
    new_texture(texture_info(WMTPixelFormatR32Float, WMTTextureType2DArray, 32, 32, 1, 6, 1, 1, WMTTextureUsageShaderRead,
                             WMTResourceStorageModePrivate | WMTResourceHazardTrackingModeUntracked, 0));
}
static void texture_msaa(void) {
    new_texture(texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2DMultisample, 32, 32, 1, 1, 1, 4,
                             WMTTextureUsageRenderTarget, WMTResourceStorageModePrivate, 0));
}
static void texture_lossless(void) {
    new_texture(texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 64, 64, 1, 1, 1, 1,
                             WMTTextureUsageShaderRead | WMTTextureUsageRenderTarget | WMTTextureUsagePixelFormatView,
                             WMTResourceStorageModePrivate, WMTTextureFlagLossless));
}

/* ---- 22 ---- */

static void buffer_texture(void) {
    id<MTLBuffer> b = make_buffer(65536, MTLResourceStorageModeShared);
    id rb = wm_rec(b, @protocol(MTLBuffer));
    wm_in(rb);
    struct WMTTextureInfo info = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 32, 16, 1, 1, 1, 1,
                                              WMTTextureUsageShaderRead, WMTResourceStorageModeShared, 0);
    struct WMTTextureInfo before = info;
    struct unixcall_mtlbuffer_newtexture p;
    wm_fill(&p, sizeof p);
    p.buffer = (obj_handle_t)rb;
    p.info.ptr = &info;
    p.offset = 1024;
    p.bytes_per_row = 256;
    wm_own("ret", &p.ret);
    wm_call(22, &p);
    id<MTLTexture> t = need("ret", p.ret);
    texture_out(&info, &before, t);
    wm_val("bufferOffset", "%lu", (unsigned long)t.bufferOffset);
    wm_val("bufferBytesPerRow", "%lu", (unsigned long)t.bufferBytesPerRow);
    wm_val("offset", "%llu", (unsigned long long)p.offset);
    wm_val("bytes_per_row", "%llu", (unsigned long long)p.bytes_per_row);
}

/* ---- 23 ---- */

/* One view through a recording texture: the message (format, type, ranges, the decoded swizzle), then the view's own
 * format, swizzle and ranges. */
static void view(id<MTLTexture> base, enum WMTPixelFormat format, enum WMTTextureType type, uint16_t level_start,
                 uint16_t level_count, uint16_t slice_start, uint16_t slice_count, struct WMTTextureSwizzleChannels sw) {
    id rt = wm_rec(base, @protocol(MTLTexture));
    wm_in(rt);
    struct unixcall_mtltexture_newtextureview p;
    wm_fill(&p, sizeof p);
    p.texture = (obj_handle_t)rt;
    p.format = format;
    p.texture_type = type;
    p.level_start = level_start;
    p.level_count = level_count;
    p.slice_start = slice_start;
    p.slice_count = slice_count;
    p.swizzle = sw;
    wm_own("ret", &p.ret);
    wm_call(23, &p);
    id<MTLTexture> v = need("ret", p.ret);
    wm_eq("gpu_resource_id", p.gpu_resource_id, v.gpuResourceID._impl, "gpuResourceID");
    wm_val("pixelFormat", "%lu", (unsigned long)v.pixelFormat);
    MTLTextureSwizzleChannels s = v.swizzle;
    wm_val("swizzle", "%u,%u,%u,%u", s.red, s.green, s.blue, s.alpha);
    wm_val("textureType", "%lu", (unsigned long)v.textureType);
    wm_val("levels", "%lu+%lu", (unsigned long)v.parentRelativeLevel, (unsigned long)v.mipmapLevelCount);
    wm_val("slices", "%lu+%lu", (unsigned long)v.parentRelativeSlice, (unsigned long)v.arrayLength);
}

/* Each custom-swizzle format (an offset per component, (base+off)%6) with a non-identity request whose sums wrap. */
static void view_swizzle(void) {
    MTLTextureUsage u = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    id<MTLTexture> bgra = make_texture(MTLTextureType2DArray, MTLPixelFormatBGRA8Unorm, 8, 8, 3, 4, u, MTLStorageModePrivate);
    id<MTLTexture> ds = make_texture(MTLTextureType2D, MTLPixelFormatDepth32Float_Stencil8, 8, 8, 1, 1, u,
                                     MTLStorageModePrivate);
    id<MTLTexture> abgr4 = make_texture(MTLTextureType2D, MTLPixelFormatABGR4Unorm, 8, 8, 1, 1, u, MTLStorageModePrivate);
    if (!bgra || !ds || !abgr4) {
        wm_line("no-base-texture");
        return;
    }
    /* AlphaIsOne: a+2 */
    view(bgra, WMTPixelFormatBGRX8Unorm, WMTTextureType2DArray, 1, 2, 1, 3,
         (struct WMTTextureSwizzleChannels){WMTTextureSwizzleBlue, WMTTextureSwizzleGreen, WMTTextureSwizzleRed,
                                            WMTTextureSwizzleAlpha});
    /* no custom swizzle: the request as it is */
    view(bgra, WMTPixelFormatBGRA8Unorm, WMTTextureType2D, 2, 1, 3, 1,
         (struct WMTTextureSwizzleChannels){WMTTextureSwizzleAlpha, WMTTextureSwizzleBlue, WMTTextureSwizzleGreen,
                                            WMTTextureSwizzleRed});
    /* R001: g+3, b+2, a+2 */
    view(ds, WMTPixelFormatR32X8X32, WMTTextureType2D, 0, 1, 0, 1,
         (struct WMTTextureSwizzleChannels){WMTTextureSwizzleGreen, WMTTextureSwizzleAlpha, WMTTextureSwizzleBlue,
                                            WMTTextureSwizzleRed});
    /* 0R01: r+4, g+5, b+2, a+2 */
    view(ds, WMTPixelFormatX32G8X32, WMTTextureType2D, 0, 1, 0, 1,
         (struct WMTTextureSwizzleChannels){WMTTextureSwizzleRed, WMTTextureSwizzleBlue, WMTTextureSwizzleAlpha,
                                            WMTTextureSwizzleZero});
    /* GBAR: r+1, g+1, b+1, a+3 */
    view(abgr4, WMTPixelFormatBGRA4Unorm, WMTTextureType2D, 0, 1, 0, 1,
         (struct WMTTextureSwizzleChannels){WMTTextureSwizzleAlpha, WMTTextureSwizzleOne, WMTTextureSwizzleBlue,
                                            WMTTextureSwizzleGreen});
}

/* ---- 39-44 ---- */

static void getters_nil(void) { getters(0); }

/* ---- 45, 145 ---- */

/* A 2D array texture (8x8, 2 levels, 2 slices): 45 writes a region of level 1 slice 1 (read back directly), then 145
 * reads part of it into rows wider than the region (the padding stays 0xA5). */
static void replace_getbytes(void) {
    id<MTLTexture> t = make_texture(MTLTextureType2DArray, MTLPixelFormatRGBA8Unorm, 8, 8, 2, 2,
                                    MTLTextureUsageShaderRead, MTLStorageModeShared);
    uint8_t zero[64] = {0}, level1[64];
    [t replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:1 slice:1 withBytes:zero bytesPerRow:16 bytesPerImage:0];
    id rt = wm_rec(t, @protocol(MTLTexture));
    wm_in(rt);
    uint8_t src[3 * 2 * 4];
    for (unsigned i = 0; i < sizeof src; i++) src[i] = (uint8_t)(0x11 + i);
    struct unixcall_mtltexture_replaceregion p;
    wm_fill(&p, sizeof p);
    p.texture = (obj_handle_t)rt;
    p.origin = (struct WMTOrigin){1, 2, 0};
    p.size = (struct WMTSize){3, 2, 1};
    p.level = 1;
    p.slice = 1;
    p.data.ptr = src;
    p.bytes_per_row = 12;
    p.bytes_per_image = 24;
    wm_call(45, &p);
    [t getBytes:level1 bytesPerRow:16 bytesPerImage:0 fromRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:1 slice:1];
    hex("level1", level1, sizeof level1);
    uint8_t out[2 * 20];
    wm_fill(out, sizeof out);
    wm_fill(&p, sizeof p);
    p.texture = (obj_handle_t)rt;
    p.origin = (struct WMTOrigin){0, 2, 0};
    p.size = (struct WMTSize){4, 2, 1};
    p.level = 1;
    p.slice = 1;
    p.data.ptr = out;
    p.bytes_per_row = 20;
    p.bytes_per_image = 40;
    wm_call(145, &p);
    wm_bytes("out", out, sizeof out);
    hex("bytes", out, sizeof out);
}

/* ---- 46 ---- */

/* The length comes from ret (the PE thunk passes it there); ret stays. */
static void did_modify_range(void) {
    id rb = wm_rec(nil, @protocol(MTLBuffer));
    wm_in(rb);
    struct unixcall_generic_obj_uint64_uint64_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)rb;
    p.arg = 16;
    p.ret = 48;
    wm_call(46, &p);
    wm_val("arg", "%llu", (unsigned long long)p.arg);
    wm_val("ret", "%llu", (unsigned long long)p.ret);
}

/* ---- 107 ---- */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" /* Managed: the C's didModifyRange branch */
static void update_contents(void) {
    for (int managed = 0; managed < 2; managed++) {
        id<MTLBuffer> b = make_buffer(64, managed ? MTLResourceStorageModeManaged : MTLResourceStorageModeShared);
        memset(b.contents, WM_FILL, 64);
        if (managed) [b didModifyRange:NSMakeRange(0, 64)];
        id rb = wm_rec(b, @protocol(MTLBuffer));
        wm_in(rb);
        uint8_t src[16];
        for (unsigned i = 0; i < sizeof src; i++) src[i] = (uint8_t)(0x41 + i);
        struct unixcall_mtlbuffer_updatecontents p;
        wm_fill(&p, sizeof p);
        p.buffer = (obj_handle_t)rb;
        p.offset = 8;
        p.data.ptr = src;
        p.length = sizeof src;
        wm_call(107, &p);
        NSData *d = wm_readback(b);
        wm_bytes("after", d.bytes, d.length);
        hex("bytes", d.bytes, d.length);
    }
}
#pragma clang diagnostic pop

/* ---- 132-137 ---- */

static void residency(void) {
    id dev = rec_device();
    struct unixcall_mtldevice_newresidencyset p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.init_capacity = 3;
    wm_own("ret_set", &p.ret_set);
    wm_own("ret_error", &p.ret_error);
    wm_call(132, &p);
    id<MTLResidencySet> set = need("ret_set", p.ret_set);
    wm_val("allocationCount", "%lu", (unsigned long)set.allocationCount);
    id rs = wm_rec(set, @protocol(MTLResidencySet));
    wm_in(rs);
    id<MTLBuffer> b1 = make_buffer(256, MTLResourceStorageModePrivate), b2 = make_buffer(512, MTLResourceStorageModeShared);
    wm_in(b1);
    wm_in(b2);
    obj_handle_t allocs[2] = {(obj_handle_t)b1, (obj_handle_t)b2};
    struct unixcall_mtlresidencyset_addallocations a;
    wm_fill(&a, sizeof a);
    a.set = (obj_handle_t)rs;
    a.allocations.ptr = allocs;
    a.count = 2;
    wm_call(133, &a);
    wm_val("allocationCount", "%lu", (unsigned long)set.allocationCount);
    wm_fill(&a, sizeof a);
    a.set = (obj_handle_t)rs;
    a.allocations.ptr = allocs + 1;
    a.count = 1;
    wm_call(134, &a);
    wm_val("allocationCount", "%lu", (unsigned long)set.allocationCount);
    wm_val("contains", "%d,%d", [set containsAllocation:b1], [set containsAllocation:b2]);
    struct unixcall_generic_obj_noret n;
    n.handle = (obj_handle_t)rs;
    wm_call(136, &n);
    wm_val("allocatedSize", "%s", set.allocatedSize ? "nonzero" : "0");
    n.handle = (obj_handle_t)rs;
    wm_call(135, &n);
    wm_val("allocationCount", "%lu", (unsigned long)set.allocationCount);
    n.handle = (obj_handle_t)rs;
    wm_call(136, &n);
    wm_val("allocatedSize", "%s", set.allocatedSize ? "nonzero" : "0");
    id<MTLCommandQueue> q = [[wm_device newCommandQueue] autorelease];
    id rq = wm_rec(q, @protocol(MTLCommandQueue));
    wm_in(rq);
    wm_in(set);
    struct unixcall_generic_obj_obj_noret o;
    wm_fill(&o, sizeof o);
    o.handle = (obj_handle_t)rq;
    o.arg = (obj_handle_t)set;
    wm_call(137, &o);
    [q removeResidencySet:set];
}

/* A device whose residency set fails with an autoreleased error: the C retains ret_error (P7: calls.c says AR). */
@interface WMG2FailingDevice : NSObject
@end
@implementation WMG2FailingDevice
- (id)newResidencySetWithDescriptor:(MTLResidencySetDescriptor *)d error:(NSError **)error {
    *error = [NSError errorWithDomain:@"wm_probe" code:132 userInfo:nil];
    return nil;
}
@end

static void residency_error(void) {
    id dev = wm_rec([[[WMG2FailingDevice alloc] init] autorelease], @protocol(MTLDevice));
    wm_in(dev);
    struct unixcall_mtldevice_newresidencyset p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.init_capacity = 0;
    wm_own("ret_set", &p.ret_set);
    wm_own("ret_error", &p.ret_error);
    wm_call(132, &p);
    NSError *e = need("ret_error", p.ret_error);
    wm_val("domain", "%s", e.domain.UTF8String);
    wm_val("code", "%ld", (long)e.code);
}

static void residency_empty(void) {
    MTLResidencySetDescriptor *d = [[[MTLResidencySetDescriptor alloc] init] autorelease];
    id<MTLResidencySet> set = [[wm_device newResidencySetWithDescriptor:d error:NULL] autorelease];
    id rs = wm_rec(set, @protocol(MTLResidencySet));
    wm_in(rs);
    for (unsigned slot = 133; slot <= 134; slot++) {
        struct unixcall_mtlresidencyset_addallocations a;
        wm_fill(&a, sizeof a);
        a.set = (obj_handle_t)rs;
        a.allocations.ptr = NULL;
        a.count = 0;
        wm_call(slot, &a);
        wm_val("allocationCount", "%lu", (unsigned long)set.allocationCount);
    }
}

/* ---- 138-142 ---- */

static id new_heap(id dev, uint64_t size, uint64_t options, enum WMTHeapType type, enum WMTSparsePageSize page) {
    struct WMTHeapInfo info = {size, (enum WMTResourceOptions)options, type, page};
    struct unixcall_mtldevice_newheap p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(138, &p);
    id<MTLHeap> h = need("ret", p.ret);
    wm_val("size", "%lu", (unsigned long)h.size);
    wm_val("type", "%ld", (long)h.type);
    wm_val("storageMode", "%lu", (unsigned long)h.storageMode);
    return h;
}

static void heap_buffer(id<MTLHeap> heap, uint64_t length, uint64_t options, uint64_t offset) {
    id rh = wm_rec(heap, @protocol(MTLHeap));
    wm_in(rh);
    struct WMTBufferInfo info;
    wm_fill(&info, sizeof info);
    info.length = length;
    info.options = (enum WMTResourceOptions)options;
    struct unixcall_mtlheap_newbuffer p;
    wm_fill(&p, sizeof p);
    p.heap = (obj_handle_t)rh;
    p.info.ptr = &info;
    p.offset = offset;
    wm_own("ret", &p.ret);
    wm_call(141, &p);
    id<MTLBuffer> b = need("ret", p.ret);
    if (!info.memory.ptr) wm_val("memory", "0");
    else wm_eq("memory", (uint64_t)info.memory.ptr, (uint64_t)b.contents, "contents");
    wm_eq("gpu_address", info.gpu_address, b.gpuAddress, "gpuAddress");
    wm_val("length", "%llu", (unsigned long long)info.length);
    if (~offset) wm_val("heapOffset", "%lu", (unsigned long)b.heapOffset);
    wm_eq("heap", (uint64_t)b.heap, (uint64_t)heap, "heap");
}

static void heap_texture(id<MTLHeap> heap, uint64_t options, uint64_t offset) {
    id rh = wm_rec(heap, @protocol(MTLHeap));
    wm_in(rh);
    struct WMTTextureInfo info = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 64, 64, 1, 1, 1, 1,
                                              WMTTextureUsageShaderRead, options, 0);
    struct WMTTextureInfo before = info;
    struct unixcall_mtlheap_newtexture p;
    wm_fill(&p, sizeof p);
    p.heap = (obj_handle_t)rh;
    p.info.ptr = &info;
    p.offset = offset;
    wm_own("ret", &p.ret);
    wm_call(142, &p);
    id<MTLTexture> t = need("ret", p.ret);
    texture_out(&info, &before, t);
    if (~offset) wm_val("heapOffset", "%lu", (unsigned long)t.heapOffset);
    wm_eq("heap", (uint64_t)t.heap, (uint64_t)heap, "heap");
}

static void heap(void) {
    id dev = rec_device();
    id<MTLHeap> automatic = new_heap(dev, 1 << 20, WMTResourceStorageModeShared, WMTHeapTypeAutomatic, WMTSparsePageSize16);
    id<MTLHeap> placement = new_heap(dev, 1 << 20, WMTResourceStorageModePrivate, WMTHeapTypePlacement, WMTSparsePageSize64);

    struct unixcall_mtldevice_heapbuffersizeandalign s;
    wm_fill(&s, sizeof s);
    s.device = (obj_handle_t)dev;
    s.length = 1000;
    s.options = WMTResourceStorageModePrivate;
    wm_call(139, &s);
    MTLSizeAndAlign want = [wm_device heapBufferSizeAndAlignWithLength:1000 options:MTLResourceStorageModePrivate];
    wm_eq("ret_size", s.ret_size, want.size, "size");
    wm_eq("ret_align", s.ret_align, want.align, "align");

    struct WMTTextureInfo ti = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 64, 64, 1, 1, 1, 1,
                                            WMTTextureUsageShaderRead, WMTResourceStorageModePrivate, 0);
    struct unixcall_mtldevice_heaptexturesizeandalign t;
    wm_fill(&t, sizeof t);
    t.device = (obj_handle_t)dev;
    t.info.ptr = &ti;
    wm_call(140, &t);
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:64
                                                                                 height:64
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    want = [wm_device heapTextureSizeAndAlignWithDescriptor:td];
    wm_eq("ret_size", t.ret_size, want.size, "size");
    wm_eq("ret_align", t.ret_align, want.align, "align");

    heap_buffer(automatic, 4096, WMTResourceStorageModeShared, ~0ull);
    heap_buffer(placement, 4096, WMTResourceStorageModePrivate, 65536);
    heap_texture(automatic, WMTResourceStorageModeShared, ~0ull);
    heap_texture(placement, WMTResourceStorageModePrivate, 131072);
}

/* ---- 143 ---- */

static struct WMTIndirectCommandBufferInfo icb_info(void) {
    struct WMTIndirectCommandBufferInfo info;
    wm_fill(&info, sizeof info);
    info.type = WMTIndirectCommandTypeDraw | WMTIndirectCommandTypeDrawIndexed;
    info.inherit_buffers = 0;
    info.inherit_pso = 0;
    info.inherit_cull_mode = 0;
    info.inherit_depth_bias = 0;
    info.inherit_depth_clip_mode = 0;
    info.inherit_depth_stencil_state = 0;
    info.inherit_front_facing = 0;
    info.inherit_fill_mode = 0;
    info.support_ray_tracing = 0;
    info.support_dynamic_attribute_stride = 0;
    info.support_color_attachment_mapping = 0;
    info.max_vertex_buffer_binding = 3;
    info.max_fragment_buffer_binding = 4;
    info.max_kernel_buffer_binding = 5;
    info.max_mesh_buffer_binding = 6;
    info.max_object_buffer_binding = 7;
    info.max_object_threadgroup_memory_binding = 1;
    info.max_kernel_threadgroup_memory_binding = 2;
    return info;
}

static void icb(void) {
    id dev = rec_device();
    struct WMTIndirectCommandBufferInfo info = icb_info();
    info.inherit_buffers = 1;
    info.inherit_pso = 1;
    struct unixcall_mtldevice_newicb p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    p.max_count = 16;
    p.options = WMTResourceStorageModeShared;
    wm_own("ret", &p.ret);
    wm_call(143, &p);
    id<MTLIndirectCommandBuffer> b = need("ret", p.ret);
    wm_val("size", "%lu", (unsigned long)b.size);
    wm_eq("gpu_resource_id", info.gpu_resource_id, b.gpuResourceID._impl, "gpuResourceID");
    wm_val("storageMode", "%lu", (unsigned long)b.storageMode);
}

/* Each flag alone through a target-less device (no ICB is made: the descriptor is what's pinned): the four the C reads,
 * then inherit_cull_mode (#if 0 in the C: unread). */
static void icb_flags(void) {
    id dev = wm_rec(nil, @protocol(MTLDevice));
    wm_in(dev);
    for (int f = 0; f < 5; f++) {
        struct WMTIndirectCommandBufferInfo info = icb_info();
        info.type = WMTIndirectCommandTypeConcurrentDispatch;
        switch (f) {
        case 0: info.inherit_buffers = 1; break;
        case 1: info.inherit_pso = 1; break;
        case 2: info.support_dynamic_attribute_stride = 1; break;
        case 3: info.support_ray_tracing = 1; break;
        case 4: info.inherit_cull_mode = 1; break;
        }
        struct unixcall_mtldevice_newicb p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)dev;
        p.info.ptr = &info;
        p.max_count = 9;
        p.options = WMTResourceStorageModePrivate;
        wm_own("ret", &p.ret);
        wm_call(143, &p);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
    }
}

/* ---- nil ---- */

/* Each receiver and object argument of the group's sends as 0, where the C sends it on as nil: the bytes each call
 * writes. 107 is left out: a nil buffer's contents is NULL and the C's memcpy faults. */
static void nil_args(void) {
    {
        struct WMTBufferInfo info;
        wm_fill(&info, sizeof info);
        info.length = 64;
        info.options = 0;
        info.memory.ptr = NULL;
        struct unixcall_mtldevice_newbuffer p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_call(18, &p);
        wm_bytes("params", &p, sizeof p);
        wm_bytes("info", &info, sizeof info);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_address", "0x%llx", (unsigned long long)info.gpu_address);
    }
    {
        struct WMTSamplerInfo info;
        memset(&info, 0, sizeof info);
        info.support_argument_buffers = 1;
        info.gpu_resource_id = WM_FILL64;
        struct unixcall_mtldevice_newsamplerstate p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_call(19, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
    }
    {
        struct WMTDepthStencilInfo info;
        memset(&info, 0, sizeof info);
        struct unixcall_mtldevice_newdepthstencilstate p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_call(20, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
    }
    {
        struct WMTTextureInfo info = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 4, 4, 1, 1, 1, 1,
                                                  WMTTextureUsageShaderRead, 0, 0);
        struct unixcall_mtldevice_newtexture p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_call(21, &p);
        wm_bytes("params", &p, sizeof p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
        wm_val("mach_port", "0x%x", info.mach_port);
    }
    {
        struct WMTTextureInfo info = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 4, 4, 1, 1, 1, 1,
                                                  WMTTextureUsageShaderRead, 0, 0);
        struct unixcall_mtlbuffer_newtexture p;
        wm_fill(&p, sizeof p);
        p.buffer = 0;
        p.info.ptr = &info;
        p.offset = 0;
        p.bytes_per_row = 64;
        wm_call(22, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
        wm_val("mach_port", "0x%x", info.mach_port);
    }
    {
        struct unixcall_mtltexture_newtextureview p;
        wm_fill(&p, sizeof p);
        p.texture = 0;
        p.format = WMTPixelFormatBGRX8Unorm;
        p.texture_type = WMTTextureType2D;
        p.level_start = 0, p.level_count = 1, p.slice_start = 0, p.slice_count = 1;
        p.swizzle = (struct WMTTextureSwizzleChannels){2, 3, 4, 5};
        wm_call(23, &p);
        wm_bytes("params", &p, sizeof p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)p.gpu_resource_id);
    }
    for (unsigned slot = 45; slot <= 145; slot += 100) {
        uint8_t data[16];
        wm_fill(data, sizeof data);
        struct unixcall_mtltexture_replaceregion p;
        wm_fill(&p, sizeof p);
        p.texture = 0;
        p.origin = (struct WMTOrigin){0, 0, 0};
        p.size = (struct WMTSize){2, 2, 1};
        p.level = 0, p.slice = 0;
        p.data.ptr = data;
        p.bytes_per_row = 8, p.bytes_per_image = 16;
        wm_call(slot, &p);
        wm_bytes("data", data, sizeof data);
    }
    {
        struct unixcall_generic_obj_uint64_uint64_ret p;
        wm_fill(&p, sizeof p);
        p.handle = 0;
        p.arg = 1, p.ret = 2;
        wm_call(46, &p);
        wm_val("ret", "%llu", (unsigned long long)p.ret);
    }
    {
        struct unixcall_mtldevice_newresidencyset p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.init_capacity = 1;
        wm_call(132, &p);
        wm_val("ret_set", "0x%llx", (unsigned long long)p.ret_set);
        wm_val("ret_error", "0x%llx", (unsigned long long)p.ret_error);
    }
    {
        id<MTLBuffer> b = make_buffer(64, MTLResourceStorageModeShared);
        obj_handle_t allocs[2] = {0, (obj_handle_t)b};
        id rs = wm_rec(nil, @protocol(MTLResidencySet));
        wm_in(rs);
        for (unsigned slot = 133; slot <= 134; slot++) {
            struct unixcall_mtlresidencyset_addallocations a;
            wm_fill(&a, sizeof a);
            a.set = (obj_handle_t)rs;
            a.allocations.ptr = allocs;
            a.count = 2;
            wm_call(slot, &a);
            wm_fill(&a, sizeof a);
            a.set = 0;
            a.allocations.ptr = allocs;
            a.count = 2;
            wm_call(slot, &a);
        }
        for (unsigned slot = 135; slot <= 136; slot++) {
            struct unixcall_generic_obj_noret n = {0};
            wm_call(slot, &n);
        }
        id rq = wm_rec(nil, @protocol(MTLCommandQueue));
        wm_in(rq);
        struct unixcall_generic_obj_obj_noret o = {(obj_handle_t)rq, 0};
        wm_call(137, &o);
        o = (struct unixcall_generic_obj_obj_noret){0, 0};
        wm_call(137, &o);
    }
    {
        struct WMTHeapInfo info = {1 << 16, 0, WMTHeapTypeAutomatic, WMTSparsePageSize16};
        struct unixcall_mtldevice_newheap p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        wm_call(138, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
    }
    {
        struct unixcall_mtldevice_heapbuffersizeandalign s;
        wm_fill(&s, sizeof s);
        s.device = 0;
        s.length = 100;
        s.options = 0;
        wm_call(139, &s);
        wm_val("ret_size", "%u", s.ret_size);
        wm_val("ret_align", "%u", s.ret_align);
        struct WMTTextureInfo ti = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 4, 4, 1, 1, 1, 1,
                                                WMTTextureUsageShaderRead, 0, 0);
        struct unixcall_mtldevice_heaptexturesizeandalign t;
        wm_fill(&t, sizeof t);
        t.device = 0;
        t.info.ptr = &ti;
        wm_call(140, &t);
        wm_val("ret_size", "%u", t.ret_size);
        wm_val("ret_align", "%u", t.ret_align);
    }
    {
        struct WMTBufferInfo info;
        wm_fill(&info, sizeof info);
        info.length = 64;
        info.options = 0;
        struct unixcall_mtlheap_newbuffer p;
        wm_fill(&p, sizeof p);
        p.heap = 0;
        p.info.ptr = &info;
        p.offset = ~0ull;
        wm_call(141, &p);
        wm_bytes("info", &info, sizeof info);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("memory", "0x%llx", (unsigned long long)info.memory.ptr);
        wm_val("gpu_address", "0x%llx", (unsigned long long)info.gpu_address);
    }
    {
        struct WMTTextureInfo info = texture_info(WMTPixelFormatRGBA8Unorm, WMTTextureType2D, 4, 4, 1, 1, 1, 1,
                                                  WMTTextureUsageShaderRead, 0, 0);
        struct unixcall_mtlheap_newtexture p;
        wm_fill(&p, sizeof p);
        p.heap = 0;
        p.info.ptr = &info;
        p.offset = 0;
        wm_call(142, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
        wm_val("mach_port", "0x%x", info.mach_port);
    }
    {
        struct WMTIndirectCommandBufferInfo info = icb_info();
        struct unixcall_mtldevice_newicb p;
        wm_fill(&p, sizeof p);
        p.device = 0;
        p.info.ptr = &info;
        p.max_count = 1;
        p.options = 0;
        wm_call(143, &p);
        wm_val("ret", "0x%llx", (unsigned long long)p.ret);
        wm_val("gpu_resource_id", "0x%llx", (unsigned long long)info.gpu_resource_id);
    }
}

/* ---- raises ---- */

static void x_pixelformat_on_buffer(void) {
    id<MTLBuffer> b = make_buffer(64, MTLResourceStorageModeShared);
    wm_in(b);
    struct unixcall_generic_obj_uint64_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)b;
    wm_call(39, &p);
    wm_bytes("params", &p, sizeof p);
}

/* ---- native hooks (WM_NATIVE; last: the hooks are the process's) ---- */

static struct {
    _Atomic unsigned allocs, frees;
    _Atomic(void *) last_alloc, last_free, ctx_seen;
    _Atomic uint64_t last_length;
} hooks;
static int hooks_ctx;

static void *hook_alloc(uint64_t length, void *ctx) {
    void *p = NULL;
    if (posix_memalign(&p, 16384, length)) return NULL;
    atomic_store(&hooks.last_length, length);
    atomic_store(&hooks.last_alloc, p);
    atomic_store(&hooks.ctx_seen, ctx);
    atomic_fetch_add(&hooks.allocs, 1);
    return p;
}

static void hook_free(void *p, void *ctx) {
    atomic_store(&hooks.last_free, p);
    atomic_store(&hooks.ctx_seen, ctx);
    atomic_fetch_add(&hooks.frees, 1);
    free(p);
}

static void native_hooks(void) {
    void (*set)(void *(*)(uint64_t, void *), void (*)(void *, void *), void *) = wm_sym("WMTSetMemoryHooks");
    int (*hooked)(void) = wm_sym("WMTMemoryHooked");
    if (!set || !hooked) {
        wm_line("no-hooks");
        return;
    }
    wm_line("hooked=%d", hooked());
    set(hook_alloc, hook_free, &hooks_ctx);
    wm_line("hooked=%d", hooked());
    /* The hooked sends straight to the device (a recorder would print the hook's whole memory). */
    id dev = wm_device;
    /* Shared, with the probe's retain: the buffer's memory is the hook's, whole 16 KiB pages. */
    struct WMTBufferInfo info;
    wm_fill(&info, sizeof info);
    info.length = 100;
    info.options = WMTResourceStorageModeShared;
    info.memory.ptr = NULL;
    struct unixcall_mtldevice_newbuffer p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(18, &p);
    id<MTLBuffer> b = need("ret", p.ret);
    wm_val("allocs", "%u", atomic_load(&hooks.allocs));
    wm_val("alloc_length", "%llu", (unsigned long long)atomic_load(&hooks.last_length));
    wm_val("ctx", "%s", atomic_load(&hooks.ctx_seen) == &hooks_ctx ? "same" : "other");
    wm_eq("memory", (uint64_t)info.memory.ptr, (uint64_t)atomic_load(&hooks.last_alloc), "hook");
    wm_eq("contents", (uint64_t)b.contents, (uint64_t)atomic_load(&hooks.last_alloc), "hook");
    wm_eq("gpu_address", info.gpu_address, b.gpuAddress, "gpuAddress");
    wm_val("buffer.length", "%lu", (unsigned long)b.length);
    /* Private: not the hooks'. */
    wm_fill(&info, sizeof info);
    info.length = 100;
    info.options = WMTResourceStorageModePrivate;
    info.memory.ptr = NULL;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)rec_device();
    p.info.ptr = &info;
    wm_own("ret", &p.ret);
    wm_call(18, &p);
    need("ret", p.ret);
    wm_val("allocs", "%u", atomic_load(&hooks.allocs));
    wm_val("memory", "0x%llx", (unsigned long long)info.memory.ptr);
    /* Shared, released here (no probe retain): the free hook gets the hook's memory back. */
    wm_fill(&info, sizeof info);
    info.length = 20000;
    info.options = WMTResourceStorageModeShared;
    info.memory.ptr = NULL;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)dev;
    p.info.ptr = &info;
    wm_call(18, &p);
    id<MTLBuffer> c = need("ret", p.ret);
    void *mem = atomic_load(&hooks.last_alloc);
    wm_val("alloc_length", "%llu", (unsigned long long)atomic_load(&hooks.last_length));
    unsigned frees = atomic_load(&hooks.frees);
    [c release];
    for (int i = 0; i < 200 && atomic_load(&hooks.frees) == frees; i++) usleep(10000);
    wm_val("freed", "%s", atomic_load(&hooks.frees) > frees ? "yes" : "no");
    wm_val("free_ptr", "%s", atomic_load(&hooks.last_free) == mem ? "same" : "other");
    set(NULL, NULL, NULL);
    wm_line("hooked=%d", hooked());
}

const struct wm_case wm_cases_g2[] = {
    {"g2", 18, "buffer-shared", 0, buffer_shared},
    {"g2", 18, "buffer-private", 0, buffer_private},
    {"g2", 18, "buffer-nocopy", 0, buffer_nocopy},
    {"g2", 19, "sampler", 0, sampler},
    {"g2", 20, "depth-stencil", 0, depth_stencil},
    {"g2", 21, "texture-2d", 0, texture_2d},
    {"g2", 21, "texture-3d", 0, texture_3d},
    {"g2", 21, "texture-array", 0, texture_array},
    {"g2", 21, "texture-msaa", 0, texture_msaa},
    {"g2", 21, "texture-lossless", 0, texture_lossless},
    {"g2", 22, "buffer-texture", 0, buffer_texture},
    {"g2", 23, "view-swizzle", 0, view_swizzle},
    {"g2", 39, "getters-nil", 0, getters_nil},
    {"g2", 45, "replace-getbytes", 0, replace_getbytes},
    {"g2", 46, "did-modify-range", 0, did_modify_range},
    {"g2", 107, "update-contents", 0, update_contents},
    {"g2", 132, "residency", 0, residency},
    {"g2", 132, "residency-error", 0, residency_error},
    {"g2", 133, "residency-empty", 0, residency_empty},
    {"g2", 138, "heap", 0, heap},
    {"g2", 143, "icb", 0, icb},
    {"g2", 143, "icb-flags", 0, icb_flags},
    {"g2", 18, "nil-args", 0, nil_args},
    {"g2", 39, "x-pixelformat-on-buffer", 0, x_pixelformat_on_buffer},
    {"g2", 18, "native-hooks", WM_NATIVE, native_hooks},
    {0},
};
