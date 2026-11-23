/* wm_probe's g5 cases (slots.txt's g5; probe.h), recorded from DXMT's C: MetalFX's scalers and frame interpolator.
 * The descriptors a handler fills have no getters the probe could read back after: the swizzles (wm_swizzles) dump
 * each one as MetalFX gets it. The encode slots run on recorders that forward to real MetalFX objects, made here with
 * MetalFX directly (never through another slot). */
#include "probe.h"
#import <MetalFX/MetalFX.h>

/* A texture for a MetalFX input or output. */
static id<MTLTexture> tex(MTLPixelFormat f, NSUInteger w, NSUInteger h, MTLTextureUsage usage) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO];
    d.usage = usage;
    d.storageMode = MTLStorageModePrivate;
    return [[wm_device newTextureWithDescriptor:d] autorelease];
}

/* The C's to_metal_pixel_format drops the swizzle bits: RGBA16Float with AlphaIsOne reads RGBA16Float. */
#define RGBA16F_SWIZZLED ((enum WMTPixelFormat)(WMTPixelFormatAlphaIsOne | WMTPixelFormatRGBA16Float))

/* A real temporal scaler, 64x64 to 128x128, with a reactive mask (MetalFX directly). */
static id<MTLFXTemporalScaler> real_temporal(NSUInteger in, NSUInteger out) {
    MTLFXTemporalScalerDescriptor *d = [[[MTLFXTemporalScalerDescriptor alloc] init] autorelease];
    d.colorTextureFormat = MTLPixelFormatRGBA16Float;
    d.outputTextureFormat = MTLPixelFormatRGBA16Float;
    d.depthTextureFormat = MTLPixelFormatDepth32Float;
    d.motionTextureFormat = MTLPixelFormatRG16Float;
    d.inputWidth = in, d.inputHeight = in, d.outputWidth = out, d.outputHeight = out;
    d.reactiveMaskTextureEnabled = YES;
    d.reactiveMaskTextureFormat = MTLPixelFormatR8Unorm;
    return [[d newTemporalScalerWithDevice:wm_device] autorelease];
}

static id<MTLFXSpatialScaler> real_spatial(void) {
    MTLFXSpatialScalerDescriptor *d = [[[MTLFXSpatialScalerDescriptor alloc] init] autorelease];
    d.colorTextureFormat = MTLPixelFormatRGBA16Float;
    d.outputTextureFormat = MTLPixelFormatRGBA16Float;
    d.inputWidth = 64, d.inputHeight = 64, d.outputWidth = 128, d.outputHeight = 128;
    return [[d newSpatialScalerWithDevice:wm_device] autorelease];
}

static void supports(void) {
    struct unixcall_generic_obj_uint64_ret p;
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_call(68, &p);
    wm_eq("ret", p.ret, [MTLFXSpatialScalerDescriptor supportsDevice:wm_device], "supportsDevice");
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_call(69, &p);
    wm_eq("ret", p.ret, [MTLFXTemporalScalerDescriptor supportsDevice:wm_device], "supportsDevice");
    wm_fill(&p, sizeof p);
    p.handle = (obj_handle_t)wm_device;
    wm_call(165, &p);
    Class dc = NSClassFromString(@"MTLFXTemporalDenoisedScalerDescriptor");
    BOOL denoised_sup = dc ? [MTLFXTemporalDenoisedScalerDescriptor supportsDevice:wm_device] : NO;
    wm_eq("ret", p.ret, denoised_sup, "supportsDevice");
}

static void scale_range(void) {
    struct unixcall_mtldevice_fxtemporalscaler_scale p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device;
    wm_call(150, &p);
    wm_bytes("written", &p.min_scale, 2 * sizeof(float));
    float lo = [MTLFXTemporalScalerDescriptor supportedInputContentMinScaleForDevice:wm_device];
    float hi = [MTLFXTemporalScalerDescriptor supportedInputContentMaxScaleForDevice:wm_device];
    wm_rel("min", p.min_scale == lo, "supportedInputContentMinScale", "%a", p.min_scale);
    wm_rel("max", p.max_scale == hi, "supportedInputContentMaxScale", "%a", p.max_scale);
}

/* 56 with every option the info carries; reactive: the conditional reactive-mask format. */
static void temporal_with(bool on) {
    struct WMTFXTemporalScalerInfo info;
    wm_fill(&info, sizeof info);
    info.color_format = RGBA16F_SWIZZLED;
    info.output_format = WMTPixelFormatRGBA16Float;
    info.depth_format = WMTPixelFormatDepth32Float;
    info.motion_format = WMTPixelFormatRG16Float;
    info.input_width = 640, info.input_height = 360, info.output_width = 1280, info.output_height = 720;
    info.input_content_min_scale = 1.0f, info.input_content_max_scale = 2.0f;
    info.auto_exposure = on;
    info.input_content_properties_enabled = on;
    info.requires_synchronous_initialization = on;
    info.reactive_mask_enabled = on;
    info.reactive_mask_format = WMTPixelFormatR8Unorm;
    info.output_resolution_motion_vectors = !on;
    info.jittered_motion_vectors = on;
    struct unixcall_mtldevice_newfxtemporalscaler p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device;
    p.info.ptr = &info;
    wm_swizzles(true);
    wm_own("ret", &p.ret);
    wm_call(56, &p);
    wm_swizzles(false);
    wm_bytes("info-written", &info, sizeof info); /* none: the info is the caller's */
    if (p.ret && p.ret != WM_FILL64) wm_dump_as((id)p.ret, "scaler.");
}
static void temporal(void) { temporal_with(true); }
static void temporal_plain(void) { temporal_with(false); }

static void spatial(void) {
    struct WMTFXSpatialScalerInfo info;
    wm_fill(&info, sizeof info);
    info.color_format = RGBA16F_SWIZZLED;
    info.output_format = WMTPixelFormatRGBA16Float;
    info.input_width = 640, info.input_height = 360, info.output_width = 1280, info.output_height = 720;
    struct unixcall_mtldevice_newfxspatialscaler p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device;
    p.info.ptr = &info;
    wm_swizzles(true);
    wm_own("ret", &p.ret);
    wm_call(57, &p);
    wm_swizzles(false);
    if (p.ret && p.ret != WM_FILL64) wm_dump_as((id)p.ret, "scaler.");
}

/* 58 on a recorder over a real scaler: 26 setters, then encodeToCommandBuffer:, committed. */
static void temporal_encode(void) {
    id<MTLFXTemporalScaler> s = real_temporal(64, 128);
    if (!s) {
        wm_line("no-scaler");
        return;
    }
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLTexture> color = tex(MTLPixelFormatRGBA16Float, 64, 64, s.colorTextureUsage);
    id<MTLTexture> output = tex(MTLPixelFormatRGBA16Float, 128, 128, s.outputTextureUsage);
    id<MTLTexture> depth = tex(MTLPixelFormatDepth32Float, 64, 64, s.depthTextureUsage);
    id<MTLTexture> motion = tex(MTLPixelFormatRG16Float, 64, 64, s.motionTextureUsage);
    id<MTLTexture> exposure = tex(MTLPixelFormatR16Float, 1, 1, MTLTextureUsageShaderRead);
    id<MTLTexture> reactive = tex(MTLPixelFormatR8Unorm, 64, 64, s.reactiveMaskTextureUsage);
    id<MTLFence> fence = [[wm_device newFence] autorelease];
    id rec = wm_rec(s, @protocol(MTLFXTemporalScaler));
    struct WMTFXTemporalScalerProps props;
    wm_fill(&props, sizeof props);
    props.input_content_width = 48, props.input_content_height = 40;
    props.reset = true, props.depth_reversed = true;
    props.motion_vector_scale_x = 48.0f, props.motion_vector_scale_y = -40.0f;
    props.jitter_offset_x = 0.25f, props.jitter_offset_y = -0.375f, props.pre_exposure = 1.5f;
    props.color_content_offset_x = 1, props.color_content_offset_y = 2;
    props.depth_content_offset_x = 3, props.depth_content_offset_y = 4;
    props.motion_content_offset_x = 5, props.motion_content_offset_y = 6;
    props.reactive_mask_content_offset_x = 7, props.reactive_mask_content_offset_y = 8;
    props.output_offset_x = 9, props.output_offset_y = 10;
    struct unixcall_mtlcommandbuffer_temporal_scale p = {
        .cmdbuf = (obj_handle_t)cb, .scaler = (obj_handle_t)rec, .color = (obj_handle_t)color,
        .output = (obj_handle_t)output, .depth = (obj_handle_t)depth, .motion = (obj_handle_t)motion,
        .exposure = (obj_handle_t)exposure, .fence = (obj_handle_t)fence, .props = {.ptr = &props},
        .reactive_mask = (obj_handle_t)reactive};
    /* The inputs' numbers, on a line of their own (wm_hs keeps 8 strings at a time). */
    wm_val("cmdbuf", "%s", wm_hs(p.cmdbuf)), wm_val("scaler", "%s", wm_hs(p.scaler));
    wm_val("color", "%s", wm_hs(p.color)), wm_val("output", "%s", wm_hs(p.output));
    wm_val("depth", "%s", wm_hs(p.depth)), wm_val("motion", "%s", wm_hs(p.motion));
    wm_val("exposure", "%s", wm_hs(p.exposure)), wm_val("fence", "%s", wm_hs(p.fence));
    wm_val("reactive", "%s", wm_hs(p.reactive_mask));
    wm_in(cb), wm_in(color), wm_in(fence);
    wm_call(58, &p);
    [cb commit];
    [cb waitUntilCompleted];
    wm_val("cb-status", "%ld", (long)cb.status);
}

/* 59 on a recorder over a real spatial scaler: 3 setters, then encodeToCommandBuffer:. */
static void spatial_encode(void) {
    id<MTLFXSpatialScaler> s = real_spatial();
    if (!s) {
        wm_line("no-scaler");
        return;
    }
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLTexture> color = tex(MTLPixelFormatRGBA16Float, 64, 64, s.colorTextureUsage);
    id<MTLTexture> output = tex(MTLPixelFormatRGBA16Float, 128, 128, s.outputTextureUsage);
    id<MTLFence> fence = [[wm_device newFence] autorelease];
    id rec = wm_rec(s, @protocol(MTLFXSpatialScaler));
    struct unixcall_mtlcommandbuffer_spatial_scale p = {
        .cmdbuf = (obj_handle_t)cb, .scaler = (obj_handle_t)rec, .color = (obj_handle_t)color,
        .output = (obj_handle_t)output, .fence = (obj_handle_t)fence};
    wm_line("inputs cmdbuf=%s scaler=%s color=%s output=%s fence=%s", wm_hs(p.cmdbuf), wm_hs(p.scaler),
            wm_hs(p.color), wm_hs(p.output), wm_hs(p.fence));
    wm_in(cb), wm_in(color), wm_in(fence);
    wm_call(59, &p);
    [cb commit];
    [cb waitUntilCompleted];
    wm_val("cb-status", "%ld", (long)cb.status);
}

/* 151 without a scaler (input = output) and with one (the scaler's sizes). */
static void interpolator(void) {
    for (int with = 0; with < 2; with++) {
        id<MTLFXTemporalScaler> s = with ? real_temporal(640, 1280) : nil;
        struct WMTFXFrameInterpolatorInfo info;
        wm_fill(&info, sizeof info);
        info.color_format = RGBA16F_SWIZZLED;
        info.output_format = WMTPixelFormatRGBA16Float;
        info.depth_format = WMTPixelFormatDepth32Float;
        info.motion_format = WMTPixelFormatRG16Float;
        info.input_width = with ? 640 : 1280, info.input_height = with ? 640 : 720;
        info.output_width = 1280, info.output_height = with ? 1280 : 720;
        info.scaler = (obj_handle_t)s;
        info.ui_format = with ? WMTPixelFormatRGBA16Float : WMTPixelFormatInvalid;
        info.pad = 0;
        if (with) wm_line("scaler=%s", wm_hs(info.scaler));
        struct unixcall_mtldevice_newfxframeinterpolator p;
        wm_fill(&p, sizeof p);
        p.device = (obj_handle_t)wm_device;
        p.info.ptr = &info;
        wm_swizzles(true);
        wm_own("ret", &p.ret);
        wm_call(151, &p);
        wm_swizzles(false);
    }
}

/* 152 on a recorder over a real interpolator (no scaler, 128x128): UI texture + properties, then encode. */
static void interpolate_encode(void) {
    MTLFXFrameInterpolatorDescriptor *d = [[[MTLFXFrameInterpolatorDescriptor alloc] init] autorelease];
    d.colorTextureFormat = MTLPixelFormatRGBA16Float;
    d.outputTextureFormat = MTLPixelFormatRGBA16Float;
    d.depthTextureFormat = MTLPixelFormatDepth32Float;
    d.motionTextureFormat = MTLPixelFormatRG16Float;
    d.uiTextureFormat = MTLPixelFormatRGBA16Float;
    d.inputWidth = 128, d.inputHeight = 128, d.outputWidth = 128, d.outputHeight = 128;
    id<MTLFXFrameInterpolator> fi = [[d newFrameInterpolatorWithDevice:wm_device] autorelease];
    if (!fi) {
        wm_line("no-interpolator");
        return;
    }
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLTexture> color = tex(MTLPixelFormatRGBA16Float, 128, 128, fi.colorTextureUsage);
    id<MTLTexture> prev = tex(MTLPixelFormatRGBA16Float, 128, 128, fi.colorTextureUsage);
    id<MTLTexture> depth = tex(MTLPixelFormatDepth32Float, 128, 128, fi.depthTextureUsage);
    id<MTLTexture> motion = tex(MTLPixelFormatRG16Float, 128, 128, fi.motionTextureUsage);
    id<MTLTexture> output = tex(MTLPixelFormatRGBA16Float, 128, 128, fi.outputTextureUsage);
    id<MTLTexture> ui = tex(MTLPixelFormatRGBA16Float, 128, 128, fi.uiTextureUsage);
    id<MTLFence> fence = [[wm_device newFence] autorelease];
    id rec = wm_rec(fi, @protocol(MTLFXFrameInterpolator));
    struct WMTFXFrameInterpolatorProps props;
    wm_fill(&props, sizeof props);
    props.motion_vector_scale_x = 128.0f, props.motion_vector_scale_y = -128.0f;
    props.delta_time = 0.016f, props.near_plane = 0.1f, props.far_plane = 1000.0f;
    props.field_of_view = 60.0f, props.aspect_ratio = 1.0f;
    props.jitter_offset_x = 0.25f, props.jitter_offset_y = -0.375f;
    props.reset = true, props.depth_reversed = true, props.ui_composited = true;
    props.pad = 0;
    props.content_width = 0, props.content_height = 0;
    props.depth_content_offset_x = 0, props.depth_content_offset_y = 0;
    props.motion_content_offset_x = 0, props.motion_content_offset_y = 0;
    props.output_offset_x = 0, props.output_offset_y = 0;
    props.has_matrices = 0;
    struct unixcall_mtlcommandbuffer_frame_interpolate p = {
        .cmdbuf = (obj_handle_t)cb, .interpolator = (obj_handle_t)rec, .color = (obj_handle_t)color,
        .prev_color = (obj_handle_t)prev, .depth = (obj_handle_t)depth, .motion = (obj_handle_t)motion,
        .output = (obj_handle_t)output, .fence = (obj_handle_t)fence, .props = {.ptr = &props},
        .ui_texture = (obj_handle_t)ui};
    wm_line("inputs cmdbuf=%s interpolator=%s color=%s prev=%s depth=%s motion=%s output=%s ui=%s fence=%s",
            wm_hs(p.cmdbuf), wm_hs(p.interpolator), wm_hs(p.color), wm_hs(p.prev_color), wm_hs(p.depth),
            wm_hs(p.motion), wm_hs(p.output), wm_hs(p.ui_texture), wm_hs(p.fence));
    wm_in(cb), wm_in(color), wm_in(fence);
    wm_call(152, &p);
    [cb commit];
    [cb waitUntilCompleted];
    wm_val("cb-status", "%ld", (long)cb.status);
}

/* 166 with denoised G-buffer/mask formats and enable booleans. */
static void denoised_scaler(void) {
    struct WMTFXTemporalDenoisedScalerInfo info;
    wm_fill(&info, sizeof info);
    info.color_format = RGBA16F_SWIZZLED;
    info.depth_format = WMTPixelFormatDepth32Float;
    info.motion_format = WMTPixelFormatRG16Float;
    info.diffuse_albedo_format = WMTPixelFormatRGBA16Float;
    info.specular_albedo_format = WMTPixelFormatRGBA16Float;
    info.normal_format = WMTPixelFormatRGBA16Float;
    info.roughness_format = WMTPixelFormatR16Float;
    info.specular_hit_distance_format = WMTPixelFormatR16Float;
    info.denoise_strength_mask_format = WMTPixelFormatR8Unorm;
    info.transparency_overlay_format = WMTPixelFormatRGBA8Unorm;
    info.reactive_mask_format = WMTPixelFormatR8Unorm;
    info.output_format = WMTPixelFormatRGBA16Float;
    info.input_width = 64, info.input_height = 64, info.output_width = 128, info.output_height = 128;
    info.synchronous_init = true;
    info.auto_exposure = false;
    info.reactive_mask_enabled = true;
    info.specular_hit_distance_enabled = true;
    info.denoise_strength_mask_enabled = true;
    info.transparency_overlay_enabled = true;
    info.pad[0] = info.pad[1] = 0;
    struct unixcall_mtldevice_newfxtemporaldenoisedscaler p;
    wm_fill(&p, sizeof p);
    p.device = (obj_handle_t)wm_device;
    p.info.ptr = &info;
    wm_swizzles(true);
    wm_own("ret", &p.ret);
    wm_call(166, &p);
    wm_swizzles(false);
    wm_bytes("info-written", &info, sizeof info);
    if (p.ret && p.ret != WM_FILL64) wm_dump_as((id)p.ret, "scaler.");
}

/* 167 on a recorder over a real temporal denoised scaler, verifying worldToViewMatrix / viewToClipMatrix round-trip. */
static void denoised_encode(void) {
    Class dc = NSClassFromString(@"MTLFXTemporalDenoisedScalerDescriptor");
    if (!dc || ![MTLFXTemporalDenoisedScalerDescriptor supportsDevice:wm_device]) {
        wm_line("no-denoised");
        return;
    }
    MTLFXTemporalDenoisedScalerDescriptor *d = [[[dc alloc] init] autorelease];
    d.colorTextureFormat = MTLPixelFormatRGBA16Float;
    d.depthTextureFormat = MTLPixelFormatDepth32Float;
    d.motionTextureFormat = MTLPixelFormatRG16Float;
    d.diffuseAlbedoTextureFormat = MTLPixelFormatRGBA16Float;
    d.specularAlbedoTextureFormat = MTLPixelFormatRGBA16Float;
    d.normalTextureFormat = MTLPixelFormatRGBA16Float;
    d.roughnessTextureFormat = MTLPixelFormatR16Float;
    d.outputTextureFormat = MTLPixelFormatRGBA16Float;
    d.inputWidth = 64, d.inputHeight = 64, d.outputWidth = 128, d.outputHeight = 128;
    d.requiresSynchronousInitialization = YES;
    id<MTLFXTemporalDenoisedScaler> s = [[d newTemporalDenoisedScalerWithDevice:wm_device] autorelease];
    if (!s) {
        wm_line("no-denoised");
        return;
    }
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLTexture> color = tex(MTLPixelFormatRGBA16Float, 64, 64, s.colorTextureUsage);
    id<MTLTexture> output = tex(MTLPixelFormatRGBA16Float, 128, 128, s.outputTextureUsage);
    id<MTLTexture> depth = tex(MTLPixelFormatDepth32Float, 64, 64, s.depthTextureUsage);
    id<MTLTexture> motion = tex(MTLPixelFormatRG16Float, 64, 64, s.motionTextureUsage);
    id<MTLTexture> diffuse = tex(MTLPixelFormatRGBA16Float, 64, 64, s.diffuseAlbedoTextureUsage);
    id<MTLTexture> specular = tex(MTLPixelFormatRGBA16Float, 64, 64, s.specularAlbedoTextureUsage);
    id<MTLTexture> normal = tex(MTLPixelFormatRGBA16Float, 64, 64, s.normalTextureUsage);
    id<MTLTexture> rough = tex(MTLPixelFormatR16Float, 64, 64, s.roughnessTextureUsage);
    id<MTLFence> fence = [[wm_device newFence] autorelease];
    id rec = wm_rec(s, @protocol(MTLFXTemporalDenoisedScaler));
    struct WMTFXTemporalDenoisedScalerProps props;
    memset(&props, 0, sizeof props);
    props.pre_exposure = 1.5f;
    props.jitter_offset_x = 0.25f, props.jitter_offset_y = -0.5f;
    props.motion_vector_scale_x = 64.0f, props.motion_vector_scale_y = -64.0f;
    props.reset = true, props.depth_reversed = true, props.has_matrices = 1;
    for (int i = 0; i < 16; i++) {
        props.world_to_view_matrix[i] = (float)(i + 1);
        props.view_to_clip_matrix[i] = (float)(i + 17);
    }
    struct unixcall_mtlcommandbuffer_temporal_denoised_scale p = {
        .cmdbuf = (obj_handle_t)cb, .scaler = (obj_handle_t)rec, .color = (obj_handle_t)color,
        .output = (obj_handle_t)output, .depth = (obj_handle_t)depth, .motion = (obj_handle_t)motion,
        .diffuse_albedo = (obj_handle_t)diffuse, .specular_albedo = (obj_handle_t)specular,
        .normal = (obj_handle_t)normal, .roughness = (obj_handle_t)rough, .fence = (obj_handle_t)fence,
        .props = {.ptr = &props}};
    wm_line("inputs cmdbuf=%s scaler=%s color=%s output=%s fence=%s",
            wm_hs(p.cmdbuf), wm_hs(p.scaler), wm_hs(p.color), wm_hs(p.output), wm_hs(p.fence));
    wm_in(cb), wm_in(color), wm_in(fence);
    wm_call(167, &p);
    [cb commit];
    [cb waitUntilCompleted];
    wm_val("cb-status", "%ld", (long)cb.status);
    simd_float4x4 w2v = s.worldToViewMatrix;
    simd_float4x4 v2c = s.viewToClipMatrix;
    int m_ok = 1;
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            m_ok &= w2v.columns[c][r] == props.world_to_view_matrix[c * 4 + r];
            m_ok &= v2c.columns[c][r] == props.view_to_clip_matrix[c * 4 + r];
        }
    }
    wm_val("matrices-ok", "%d", m_ok);
}

/* Each object argument as 0, sent on as nil: the encode slots on target-less recorders (nothing encoded), the
 * device-taking slots with no device. */
static void nil_args(void) {
    struct WMTFXTemporalScalerProps tp;
    wm_fill(&tp, sizeof tp);
    tp.input_content_width = 64, tp.input_content_height = 64, tp.reset = false, tp.depth_reversed = false;
    struct unixcall_mtlcommandbuffer_temporal_scale t = {.scaler = (obj_handle_t)wm_rec(nil, @protocol(MTLFXTemporalScaler)),
                                                         .props = {.ptr = &tp}};
    wm_call(58, &t);
    struct unixcall_mtlcommandbuffer_spatial_scale s = {.scaler = (obj_handle_t)wm_rec(nil, @protocol(MTLFXSpatialScaler))};
    wm_call(59, &s);
    struct WMTFXFrameInterpolatorProps ip;
    wm_fill(&ip, sizeof ip);
    ip.reset = false, ip.depth_reversed = false, ip.ui_composited = false;
    ip.content_width = 0, ip.content_height = 0;
    ip.depth_content_offset_x = 0, ip.depth_content_offset_y = 0;
    ip.motion_content_offset_x = 0, ip.motion_content_offset_y = 0;
    ip.output_offset_x = 0, ip.output_offset_y = 0;
    ip.has_matrices = 0;
    struct unixcall_mtlcommandbuffer_frame_interpolate f = {
        .interpolator = (obj_handle_t)wm_rec(nil, @protocol(MTLFXFrameInterpolator)), .props = {.ptr = &ip}};
    wm_call(152, &f);
    struct WMTFXTemporalDenoisedScalerProps dp;
    wm_fill(&dp, sizeof dp);
    dp.reset = false, dp.depth_reversed = false, dp.has_matrices = 0;
    dp.input_content_width = 0, dp.input_content_height = 0;
    struct unixcall_mtlcommandbuffer_temporal_denoised_scale d = {
        .scaler = (obj_handle_t)wm_rec(nil, @protocol(MTLFXTemporalDenoisedScaler)), .props = {.ptr = &dp}};
    wm_call(167, &d);
    struct unixcall_generic_obj_uint64_ret u;
    static const unsigned sup_slots[] = {68, 69, 165};
    for (unsigned i = 0; i < 3; i++) {
        wm_fill(&u, sizeof u);
        u.handle = 0;
        wm_call(sup_slots[i], &u);
        wm_val("ret", "%llu", (unsigned long long)u.ret);
    }
    struct unixcall_mtldevice_fxtemporalscaler_scale r;
    wm_fill(&r, sizeof r);
    r.device = 0;
    wm_call(150, &r);
    wm_val("min", "%a", r.min_scale);
    wm_val("max", "%a", r.max_scale);
    struct WMTFXSpatialScalerInfo si;
    wm_fill(&si, sizeof si);
    si.color_format = si.output_format = WMTPixelFormatRGBA16Float;
    si.input_width = si.input_height = 64, si.output_width = si.output_height = 128;
    struct unixcall_mtldevice_newfxspatialscaler sp = {.device = 0, .info = {.ptr = &si}};
    wm_fill(&sp.ret, sizeof sp.ret);
    wm_own("ret", &sp.ret);
    wm_call(57, &sp);
    struct WMTFXFrameInterpolatorInfo fi;
    wm_fill(&fi, sizeof fi);
    fi.color_format = fi.output_format = WMTPixelFormatRGBA16Float;
    fi.depth_format = WMTPixelFormatDepth32Float, fi.motion_format = WMTPixelFormatRG16Float;
    fi.input_width = fi.input_height = fi.output_width = fi.output_height = 128, fi.scaler = 0;
    fi.ui_format = WMTPixelFormatInvalid, fi.pad = 0;
    struct unixcall_mtldevice_newfxframeinterpolator np = {.device = 0, .info = {.ptr = &fi}};
    wm_fill(&np.ret, sizeof np.ret);
    wm_own("ret", &np.ret);
    /* supportsDevice:nil is NO: the C sends no newFrameInterpolatorWithDevice: (no swz@ line). */
    wm_swizzles(true);
    wm_call(151, &np);
    wm_swizzles(false);
    struct WMTFXTemporalDenoisedScalerInfo di;
    wm_fill(&di, sizeof di);
    di.color_format = di.output_format = WMTPixelFormatRGBA16Float;
    di.depth_format = WMTPixelFormatDepth32Float, di.motion_format = WMTPixelFormatRG16Float;
    di.diffuse_albedo_format = di.specular_albedo_format = di.normal_format = WMTPixelFormatRGBA16Float;
    di.roughness_format = WMTPixelFormatR16Float;
    di.input_width = di.input_height = 64, di.output_width = di.output_height = 128;
    di.synchronous_init = di.auto_exposure = di.reactive_mask_enabled = false;
    di.specular_hit_distance_enabled = di.denoise_strength_mask_enabled = di.transparency_overlay_enabled = false;
    di.pad[0] = di.pad[1] = 0;
    struct unixcall_mtldevice_newfxtemporaldenoisedscaler ndp = {.device = 0, .info = {.ptr = &di}};
    wm_fill(&ndp.ret, sizeof ndp.ret);
    wm_own("ret", &ndp.ret);
    wm_swizzles(true);
    wm_call(166, &ndp);
    wm_swizzles(false);
}

/* 56 with no device: MetalFX's answer to newTemporalScalerWithDevice:nil. */
static void nil_device_temporal(void) {
    struct WMTFXTemporalScalerInfo info;
    wm_fill(&info, sizeof info);
    info.color_format = info.output_format = WMTPixelFormatRGBA16Float;
    info.depth_format = WMTPixelFormatDepth32Float, info.motion_format = WMTPixelFormatRG16Float;
    info.input_width = info.input_height = 64, info.output_width = info.output_height = 128;
    info.input_content_min_scale = 1.0f, info.input_content_max_scale = 2.0f;
    info.auto_exposure = info.input_content_properties_enabled = info.requires_synchronous_initialization = false;
    info.reactive_mask_enabled = info.output_resolution_motion_vectors = info.jittered_motion_vectors = false;
    struct unixcall_mtldevice_newfxtemporalscaler p = {.device = 0, .info = {.ptr = &info}};
    wm_fill(&p.ret, sizeof p.ret);
    wm_own("ret", &p.ret);
    wm_call(56, &p);
}

/* 58 with the device as the scaler: its first setter raises. */
static void x_scaler_on_device(void) {
    struct WMTFXTemporalScalerProps props;
    wm_fill(&props, sizeof props);
    struct unixcall_mtlcommandbuffer_temporal_scale p = {.scaler = (obj_handle_t)wm_device, .props = {.ptr = &props}};
    wm_call(58, &p);
}

const struct wm_case wm_cases_g5[] = {
    {"g5", 68, "supports", 0, supports},
    {"g5", 150, "scale-range", 0, scale_range},
    {"g5", 56, "temporal", 0, temporal},
    {"g5", 56, "temporal-plain", 0, temporal_plain},
    {"g5", 57, "spatial", 0, spatial},
    {"g5", 58, "temporal-encode", 0, temporal_encode},
    {"g5", 59, "spatial-encode", 0, spatial_encode},
    {"g5", 151, "interpolator", 0, interpolator},
    {"g5", 152, "interpolate-encode", 0, interpolate_encode},
    {"g5", 166, "denoised-scaler", 0, denoised_scaler},
    {"g5", 167, "denoised-encode", 0, denoised_encode},
    {"g5", 58, "nil-args", 0, nil_args},
    {"g5", 56, "nil-device-temporal", WM_ISOLATE, nil_device_temporal},
    {"g5", 58, "x-scaler-on-device", 0, x_scaler_on_device},
    {0},
};
