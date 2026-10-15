// MacNeutron's MetalFX presenter, loaded by DXMT's winemetal.so (wine-arm64/patches/dxmt/0002) when the launcher sets
// MACNEUTRON_PRESENT=1.
// When a game's drawable is smaller than the pixels its layer covers (a lower in-game resolution, or Wine's
// half-density rendering on a Retina screen), it upscales the frame with MetalFX into an overlay layer on top,
// instead of Core Animation's nearest-neighbour stretch. MACNEUTRON_POST_AA=cmaa2 also runs CMAA2 anti-aliasing
// (cmaa2.metal) in place on every SDR frame, before any upscale; MACNEUTRON_NO_METALFX=1 then skips only the upscale.
// Any failure passes the frame through untouched.
// Spec: docs/superpowers/specs/2026-09-28-macneutron-metalfx-upscaler-design.md
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#include <dlfcn.h>

@interface MNLayerState : NSObject
@property CAMetalLayer *overlay;       // over the game's layer while upscaling
@property CGSize overlaySize;          // drawable size the overlay was last given
@property BOOL updating;               // an overlay change is queued on the main thread
@property CGFloat scale;               // backing scale of the layer's window; 0 until known
@property id<MTLFXSpatialScaler> scaler;
@property id<MTLTexture> output;
@property NSUInteger inWidth, inHeight, outWidth, outHeight;
@property MTLPixelFormat format;
@property NSUInteger frames;
@property BOOL hdr, dumped;            // hdr: permanent for this layer
@property CGSize refusedIn, refusedOut; // the sizes MetalFX last refused: the linear filter while they last
@property BOOL refusedAll;             // MetalFX refused whatever the size (GPU, pixel format): for this layer
@property NSString *lastNote;
// CMAA2's working set, for one frame size; aaFailed: off for this layer
@property id<MTLTexture> aaEdges;
@property id<MTLBuffer> aaCtrl, aaCands, aaLocs, aaItems, aaHeads, aaArgs;
@property NSUInteger aaWidth, aaHeight;
@property BOOL aaFailed;
@property NSString *lastAANote;
@end
@implementation MNLayerState
@end

static const void *kState = &kState, *kIsOverlay = &kIsOverlay;
static id<CAMetalDrawable> (*origNextDrawable)(CAMetalLayer *, SEL);
static void (*origPresent)(id<MTLCommandBuffer>, SEL, id<MTLDrawable>);
static void (*origPresentAfter)(id<MTLCommandBuffer>, SEL, id<MTLDrawable>, CFTimeInterval);  // D3DMetal with vsync on
static void (*origPresentAt)(id<MTLCommandBuffer>, SEL, id<MTLDrawable>, CFTimeInterval);
static const char *dumpPath;      // test-only: write the 120th upscaled frame as a PPM
static double scaleOverride;      // test-only: pretend the window has this backing scale
static const char *refuseOutput;  // test-only: "<w>x<h>", an output size MetalFX is made to refuse; "all", every size
static BOOL postAA, noMetalFX;    // MACNEUTRON_POST_AA=cmaa2, MACNEUTRON_NO_METALFX=1
static float sharpenStrength;    // MACNEUTRON_SHARPEN, 0 (off: the presenter copies) to 1 (AMD's CAS maximum)

static void note(MNLayerState *st, NSString *message)
{
    if ([message isEqualToString:st.lastNote]) return;
    st.lastNote = message;
    fprintf(stderr, "macneutron-present: %s\n", message.UTF8String);
}

/* CMAA2's notes, kept apart from the upscaler's so neither repeats every frame. */
static void aaNote(MNLayerState *st, NSString *message)
{
    if ([message isEqualToString:st.lastAANote]) return;
    st.lastAANote = message;
    fprintf(stderr, "macneutron-present: %s\n", message.UTF8String);
}

/* The window's backing scale, read on the main thread where AppKit belongs. */
static void refreshScale(CAMetalLayer *layer, MNLayerState *st)
{
    if (scaleOverride > 0) { st.scale = scaleOverride; return; }
    __weak CAMetalLayer *weak = layer;
    dispatch_async(dispatch_get_main_queue(), ^{
        CAMetalLayer *l = weak;
        if (!l) return;
        id delegate = l.delegate;
        NSWindow *window = [delegate isKindOfClass:NSView.class] ? ((NSView *)delegate).window : nil;
        st.scale = window ? window.backingScaleFactor : NSScreen.mainScreen.backingScaleFactor;
    });
}

static MNLayerState *stateFor(CAMetalLayer *layer)
{
    MNLayerState *st = objc_getAssociatedObject(layer, kState);
    if (st) return st;
    @synchronized (layer) {
        st = objc_getAssociatedObject(layer, kState);
        if (!st) {
            st = [MNLayerState new];
            objc_setAssociatedObject(layer, kState, st, OBJC_ASSOCIATION_RETAIN);
            refreshScale(layer, st);
        }
    }
    return st;
}

static BOOL isHDR(CAMetalLayer *layer)
{
    switch (layer.pixelFormat) {
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatBGRA10_XR: case MTLPixelFormatBGRA10_XR_sRGB:
    case MTLPixelFormatBGR10_XR: case MTLPixelFormatBGR10_XR_sRGB:
        return YES;
    default:
        return layer.wantsExtendedDynamicRangeContent;
    }
}

static CGSize targetSize(CAMetalLayer *layer, MNLayerState *st)
{
    CGSize b = layer.bounds.size;
    return CGSizeMake(round(b.width * st.scale), round(b.height * st.scale));
}

static BOOL refused(MNLayerState *st, CGSize drawable, CGSize target)
{
    return st.refusedAll || (CGSizeEqualToSize(drawable, st.refusedIn) && CGSizeEqualToSize(target, st.refusedOut));
}

static BOOL wantsUpscale(CAMetalLayer *layer, MNLayerState *st, CGSize drawable)
{
    if (noMetalFX || st.scale <= 0 || st.hdr) return NO;
    CGSize t = targetSize(layer, st);
    return drawable.width >= 16 && drawable.width < t.width && drawable.height < t.height && !refused(st, drawable, t);
}

static void removeOverlay(MNLayerState *st)
{
    CAMetalLayer *o = st.overlay;
    st.overlay = nil; st.overlaySize = CGSizeZero; st.scaler = nil; st.output = nil;
    if (!o) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [CATransaction begin]; [CATransaction setDisableActions:YES];
        [o removeFromSuperlayer];
        [CATransaction commit];
    });
}

/* Creates or resizes the overlay on the main thread; frames pass through until it's in place. */
static void placeOverlay(CAMetalLayer *layer, MNLayerState *st, CGSize target)
{
    if (st.updating) return;
    st.updating = YES;
    CGFloat scale = st.scale;
    __weak CAMetalLayer *weak = layer;
    dispatch_async(dispatch_get_main_queue(), ^{
        CAMetalLayer *l = weak;
        if (l) {
            [CATransaction begin]; [CATransaction setDisableActions:YES];
            CAMetalLayer *o = st.overlay;
            if (!o) {
                o = [CAMetalLayer layer];
                objc_setAssociatedObject(o, kIsOverlay, @YES, OBJC_ASSOCIATION_RETAIN);
                o.device = l.device; o.pixelFormat = l.pixelFormat; o.framebufferOnly = NO; o.opaque = YES;
                o.maximumDrawableCount = l.maximumDrawableCount;
                [l addSublayer:o];
            }
            o.frame = l.bounds; o.contentsScale = scale; o.drawableSize = target;
            [CATransaction commit];
            st.overlaySize = target;
            st.overlay = o;
        }
        st.updating = NO;
    });
}

/* MetalFX refused this drawable size for this target: Core Animation's linear filter until either changes, then MetalFX
 * again; or for this layer when the refusal doesn't depend on the size (`all`). The filter stays: an upscaled frame
 * shows only the overlay, and a full-size one isn't magnified. */
static void useLinear(CAMetalLayer *layer, MNLayerState *st, NSString *reason, BOOL all, CGSize drawable, CGSize target)
{
    st.refusedIn = drawable; st.refusedOut = target; st.refusedAll = all;
    removeOverlay(st);
    __weak CAMetalLayer *weak = layer;
    dispatch_async(dispatch_get_main_queue(), ^{ weak.magnificationFilter = kCAFilterLinear; });
    note(st, [NSString stringWithFormat:@"linear filter (%@)", reason]);
}

/* A MetalFX spatial scaler and output texture for this input and output, cached per layer. A refusal sets *all when it
 * holds whatever the size. */
static NSString *prepareScaler(id<MTLDevice> device, MNLayerState *st, id<MTLTexture> src, CGSize target, BOOL *all)
{
    NSUInteger ow = (NSUInteger)target.width, oh = (NSUInteger)target.height;
    if (st.scaler && st.inWidth == src.width && st.inHeight == src.height && st.outWidth == ow && st.outHeight == oh
        && st.format == src.pixelFormat)
        return nil;
    *all = YES;
    if (![MTLFXSpatialScalerDescriptor supportsDevice:device]) return @"MetalFX isn't available on this GPU";
    NSString *size = [NSString stringWithFormat:@"%lux%lu", (unsigned long)ow, (unsigned long)oh];
    if (refuseOutput && (!strcmp(refuseOutput, "all") || [@(refuseOutput) isEqualToString:size])) {
        *all = !strcmp(refuseOutput, "all");
        fprintf(stderr, "macneutron-present: test refusal at %s\n", size.UTF8String);  // every one: the check counts them
        return @"refused for the test";
    }
    MTLFXSpatialScalerDescriptor *desc = [MTLFXSpatialScalerDescriptor new];
    desc.inputWidth = src.width; desc.inputHeight = src.height; desc.outputWidth = ow; desc.outputHeight = oh;
    desc.colorTextureFormat = src.pixelFormat; desc.outputTextureFormat = src.pixelFormat;
    desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
    id<MTLFXSpatialScaler> scaler = [desc newSpatialScalerWithDevice:device];
    if (!scaler)
        return [NSString stringWithFormat:@"MetalFX can't scale pixel format %lu", (unsigned long)src.pixelFormat];
    *all = NO;  // ponytail: a texture allocation failure is taken as size-dependent
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat
                                                                                    width:ow height:oh mipmapped:NO];
    td.usage = scaler.outputTextureUsage; td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> output = [device newTextureWithDescriptor:td];
    if (!output) return @"no memory for the MetalFX output";
    st.scaler = scaler; st.output = output; st.format = src.pixelFormat;
    st.inWidth = src.width; st.inHeight = src.height; st.outWidth = ow; st.outHeight = oh;
    return nil;
}

/* The metallib beside this library (wine-arm64/build.sh): CMAA2's four kernels and the sharpening kernel. */
static NSURL *metallibURL(void)
{
    Dl_info info;
    if (!dladdr((const void *)note, &info) || !info.dli_fname) return nil;
    return [[NSURL fileURLWithPath:@(info.dli_fname)].URLByDeletingLastPathComponent
            URLByAppendingPathComponent:@"libmacneutron-present.metallib"];
}

/* CMAA2's four kernels, for 8-bit frames (read through an sRGB view) or 10-bit ones (the shader's kTenBit: sRGB by
 * hand). nil: ready.
 * ponytail: one set of pipelines per depth for the first device that asks; Wine's games draw on one GPU. */
static id<MTLComputePipelineState> aaPS[2][4];  // [tenBit][edges, args, process, apply]
static NSString *loadCMAA2(id<MTLDevice> device, BOOL tenBit)
{
    static NSString *failure[2];
    static dispatch_once_t once[2];
    dispatch_once(&once[tenBit], ^{
        NSURL *url = metallibURL();
        if (!url) { failure[tenBit] = @"CMAA2 off (no library path)"; return; }
        NSError *error;
        id<MTLLibrary> lib = [device newLibraryWithURL:url error:&error];
        MTLFunctionConstantValues *constants = [MTLFunctionConstantValues new];
        bool ten = tenBit;
        [constants setConstantValue:&ten type:MTLDataTypeBool atIndex:0];
        id<MTLComputePipelineState> ps[4];
        NSArray *names = @[ @"cmaa2_edges", @"cmaa2_args", @"cmaa2_process", @"cmaa2_apply" ];
        for (NSUInteger i = 0; i < 4; i++) {
            id<MTLFunction> f = [lib newFunctionWithName:names[i] constantValues:constants error:&error];
            ps[i] = f ? [device newComputePipelineStateWithFunction:f error:&error] : nil;
            if (!ps[i]) {
                failure[tenBit] = [NSString stringWithFormat:@"CMAA2 off (%@: %@)", lib ? names[i] : url.lastPathComponent,
                                   error.localizedDescription ?: @"missing"];
                return;
            }
        }
        for (NSUInteger i = 0; i < 4; i++) aaPS[tenBit][i] = ps[i];
    });
    return failure[tenBit];
}

/* The sharpening kernel, from the same metallib. nil: ready. */
static id<MTLComputePipelineState> sharpenPS;
static NSString *loadSharpen(id<MTLDevice> device)
{
    static NSString *failure;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSURL *url = metallibURL();
        if (!url) { failure = @"sharpening off (no library path)"; return; }
        NSError *error;
        id<MTLLibrary> lib = [device newLibraryWithURL:url error:&error];
        id<MTLFunction> f = [lib newFunctionWithName:@"sharpen"];
        sharpenPS = f ? [device newComputePipelineStateWithFunction:f error:&error] : nil;
        if (!sharpenPS)
            failure = [NSString stringWithFormat:@"sharpening off (%@: %@)", lib ? @"sharpen" : url.lastPathComponent,
                       error.localizedDescription ?: @"missing"];
    });
    return failure;
}

/* CMAA2's working set for this frame size (upstream's default sizes, vaCMAA2DX12.cpp), made once per size. */
static NSString *prepareCMAA2(id<MTLDevice> device, MNLayerState *st, id<MTLTexture> src)
{
    if (st.aaCtrl && st.aaWidth == src.width && st.aaHeight == src.height) return nil;
    st.aaCtrl = nil;
    NSUInteger w = src.width, h = src.height, hw = (w + 1) / 2, hh = (h + 1) / 2;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint width:hw
                                                                                  height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; td.storageMode = MTLStorageModePrivate;
    MTLResourceOptions gpu = MTLResourceStorageModePrivate, cpu = MTLResourceStorageModeShared;
    id<MTLTexture> edges = [device newTextureWithDescriptor:td];
    id<MTLBuffer> ctrl = [device newBufferWithLength:64 options:cpu];
    id<MTLBuffer> cands = [device newBufferWithLength:4 * (w * h / 4 + 1) options:gpu];
    id<MTLBuffer> locs = [device newBufferWithLength:4 * ((w * h + 3) / 6 + 1) options:gpu];
    id<MTLBuffer> items = [device newBufferWithLength:8 * (w * h / 2 + 1) options:gpu];
    id<MTLBuffer> heads = [device newBufferWithLength:4 * hw * hh options:cpu];
    id<MTLBuffer> args = [device newBufferWithLength:16 options:gpu];
    if (!edges || !ctrl || !cands || !locs || !items || !heads || !args) return @"CMAA2 off (no memory for its buffers)";
    memset(ctrl.contents, 0, ctrl.length);       // the counters; each frame's last pass resets them
    memset(heads.contents, 0xFF, heads.length);  // empty lists
    st.aaEdges = edges; st.aaCands = cands; st.aaLocs = locs; st.aaItems = items; st.aaHeads = heads; st.aaArgs = args;
    st.aaWidth = w; st.aaHeight = h; st.aaCtrl = ctrl;
    aaNote(st, [NSString stringWithFormat:@"CMAA2 %lux%lu", (unsigned long)w, (unsigned long)h]);
    return nil;
}

/* The view CMAA2 works through for a pixel format: its sRGB twin (8-bit), the format itself (10-bit SDR, *tenBit), or
 * MTLPixelFormatInvalid for a format it doesn't handle. */
static MTLPixelFormat aaView(MTLPixelFormat format, BOOL *tenBit)
{
    *tenBit = NO;
    switch (format) {
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
    case MTLPixelFormatRGB10A2Unorm: case MTLPixelFormatBGR10A2Unorm: *tenBit = YES; return format;
    default: return MTLPixelFormatInvalid;
    }
}

/* CMAA2 in place on the game's frame. Upstream works on linear colour: an 8-bit frame is read and written through an
 * sRGB view (DXMT's layer is non-sRGB, and framebufferOnly off); a 10-bit SDR frame has no sRGB view, so the shader
 * decodes and encodes it by hand and keeps its blends at 10 bits. Five dispatches in one serial encoder, two of them
 * sized by the GPU. HDR layers pass through. */
static void antialias(id<MTLCommandBuffer> cb, id<MTLTexture> src, MNLayerState *st)
{
    if (!postAA || st.hdr || st.aaFailed || src.framebufferOnly) return;
    BOOL tenBit;
    MTLPixelFormat view = aaView(src.pixelFormat, &tenBit);
    if (view == MTLPixelFormatInvalid) {
        aaNote(st, [NSString stringWithFormat:@"CMAA2 skipped (pixel format %lu)", (unsigned long)src.pixelFormat]);
        return;
    }
    NSString *failure = loadCMAA2(cb.device, tenBit) ?: prepareCMAA2(cb.device, st, src);
    id<MTLTexture> color = failure || view == src.pixelFormat ? src : [src newTextureViewWithPixelFormat:view];
    if (!color) failure = @"CMAA2 off (no sRGB view of the drawable)";
    if (failure) { st.aaFailed = YES; aaNote(st, failure); return; }
    struct { uint32_t candCap, itemCap, locCap, headsW, w, h; } caps = {
        (uint32_t)(st.aaCands.length / 4), (uint32_t)(st.aaItems.length / 8), (uint32_t)(st.aaLocs.length / 4),
        (uint32_t)st.aaEdges.width, (uint32_t)src.width, (uint32_t)src.height};
    MTLSize one = MTLSizeMake(1, 1, 1);
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setTexture:color atIndex:0]; [ce setTexture:st.aaEdges atIndex:1];
    [ce setBuffer:st.aaCtrl offset:0 atIndex:0]; [ce setBuffer:st.aaCands offset:0 atIndex:1];
    [ce setBuffer:st.aaLocs offset:0 atIndex:2]; [ce setBuffer:st.aaItems offset:0 atIndex:3];
    [ce setBuffer:st.aaHeads offset:0 atIndex:4]; [ce setBuffer:st.aaArgs offset:0 atIndex:5];
    [ce setBytes:&caps length:sizeof caps atIndex:6];
    [ce setComputePipelineState:aaPS[tenBit][0]];
    [ce dispatchThreadgroups:MTLSizeMake((caps.w + 27) / 28, (caps.h + 27) / 28, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [ce setComputePipelineState:aaPS[tenBit][1]];
    [ce dispatchThreadgroups:MTLSizeMake(2, 1, 1) threadsPerThreadgroup:one];
    [ce setComputePipelineState:aaPS[tenBit][2]];
    [ce dispatchThreadgroupsWithIndirectBuffer:st.aaArgs indirectBufferOffset:0 threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];
    [ce setComputePipelineState:aaPS[tenBit][1]];
    [ce dispatchThreadgroups:MTLSizeMake(1, 2, 1) threadsPerThreadgroup:one];
    [ce setComputePipelineState:aaPS[tenBit][3]];
    [ce dispatchThreadgroupsWithIndirectBuffer:st.aaArgs indirectBufferOffset:0 threadsPerThreadgroup:MTLSizeMake(4, 32, 1)];
    [ce endEncoding];
}

/* Test-only: the 120th upscaled frame of a layer as a PPM (BGRA8 only). */
static void dumpFrame(id<MTLCommandBuffer> cb, id<MTLTexture> t, MNLayerState *st)
{
    if (!dumpPath || st.dumped || st.frames < 120 || t.pixelFormat != MTLPixelFormatBGRA8Unorm) return;
    st.dumped = YES;
    NSUInteger w = t.width, h = t.height;
    NSString *path = @(dumpPath);
    id<MTLBuffer> buf = [cb.device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, h, 1)
                 toBuffer:buf destinationOffset:0 destinationBytesPerRow:w * 4 destinationBytesPerImage:w * h * 4];
    [blit endEncoding];
    [cb addCompletedHandler:^(id<MTLCommandBuffer> done) {
        FILE *f = fopen(path.fileSystemRepresentation, "wb");
        if (!f) return;
        fprintf(f, "P6 %lu %lu 255\n", (unsigned long)w, (unsigned long)h);
        const uint8_t *px = buf.contents;
        for (NSUInteger i = 0; i < w * h; i++) { uint8_t rgb[3] = {px[i * 4 + 2], px[i * 4 + 1], px[i * 4]}; fwrite(rgb, 1, 3, f); }
        fclose(f);
    }];
}

/* MetalFX's output into the layer's drawable: the copy the presenter makes anyway, sharpened in place when
 * MACNEUTRON_SHARPEN asks for it, so the sharpening costs its arithmetic and neighbour reads, not a pass of its own.
 * CAS works on gamma-encoded values and the presenter's layers are non-sRGB (DXMT's are), so no view is taken; a
 * 10-bit or sRGB frame is copied as before. Returns the note's suffix, or nil for a plain copy. */
static NSString *copyOut(id<MTLCommandBuffer> cb, MNLayerState *st, id<MTLTexture> src, id<MTLTexture> dst)
{
    BOOL eightBit = src.pixelFormat == MTLPixelFormatBGRA8Unorm || src.pixelFormat == MTLPixelFormatRGBA8Unorm;
    if (sharpenStrength > 0 && eightBit && src.width == dst.width && src.height == dst.height) {
        NSString *failure = loadSharpen(cb.device);
        if (!failure) {
            MTLSize tg = MTLSizeMake(16, 16, 1);
            id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:sharpenPS];
            [ce setTexture:src atIndex:0];
            [ce setTexture:dst atIndex:1];
            [ce setBytes:&sharpenStrength length:sizeof sharpenStrength atIndex:0];
            [ce dispatchThreadgroups:MTLSizeMake((src.width + tg.width - 1) / tg.width,
                                                 (src.height + tg.height - 1) / tg.height, 1)
                threadsPerThreadgroup:tg];
            [ce endEncoding];
            return [NSString stringWithFormat:@", sharpen %.2f", sharpenStrength];
        }
        static BOOL noted;
        if (!noted) { noted = YES; fprintf(stderr, "macneutron-present: %s\n", failure.UTF8String); }
    }
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:src toTexture:dst];
    [blit endEncoding];
    return nil;
}

/* Upscales the game's drawable into the overlay and presents the overlay with `present`, the same kind of present the
 * game asked for. NO: present the game's drawable as usual. */
static BOOL upscaleInto(id<MTLCommandBuffer> cb, id<CAMetalDrawable> drawable, void (^present)(id<MTLDrawable>))
{
    CAMetalLayer *layer = drawable.layer;
    id<MTLTexture> src = drawable.texture;
    if (!layer || !src || objc_getAssociatedObject(layer, kIsOverlay)) return NO;
    MNLayerState *st = stateFor(layer);
    st.frames += 1;
    if (st.frames % 120 == 0) refreshScale(layer, st);  // the window may have moved to another screen
    antialias(cb, src, st);  // on the game's own frame: before the upscale, or the frame presented as it is
    if (!wantsUpscale(layer, st, CGSizeMake(src.width, src.height))) {
        if (st.overlay) removeOverlay(st);
        if (st.scale > 0 && !st.hdr && !refused(st, CGSizeMake(src.width, src.height), targetSize(layer, st)))
            note(st, noMetalFX ? @"pass-through (MetalFX off)" : @"pass-through (full size)");
        return NO;
    }
    if (src.framebufferOnly) return NO;  // switched this frame: the next drawable is readable
    if (st.overlay && st.overlay.pixelFormat != src.pixelFormat) {  // the game switched formats: rebuild the overlay
        removeOverlay(st);
        note(st, @"pass-through (pixel format changed)");
        return NO;
    }
    CGSize target = targetSize(layer, st);
    if (!st.overlay || !CGSizeEqualToSize(st.overlaySize, target)) {
        placeOverlay(layer, st, target);
        if (!st.overlay) return NO;
    }
    BOOL all = NO;
    NSString *failure = prepareScaler(cb.device, st, src, st.overlaySize, &all);
    if (failure) { useLinear(layer, st, failure, all, CGSizeMake(src.width, src.height), st.overlaySize); return NO; }
    // The overlay paces like the game's own layer: a game presenting without vsync turns display sync off there.
    if (st.overlay.displaySyncEnabled != layer.displaySyncEnabled) st.overlay.displaySyncEnabled = layer.displaySyncEnabled;
    id<CAMetalDrawable> out = [st.overlay nextDrawable];
    if (!out || out.texture.width != st.outWidth || out.texture.height != st.outHeight) {
        note(st, @"pass-through (no overlay drawable)");
        return NO;
    }
    st.scaler.colorTexture = src;
    st.scaler.outputTexture = st.output;
    [st.scaler encodeToCommandBuffer:cb];
    NSString *sharpened = copyOut(cb, st, st.output, out.texture);
    dumpFrame(cb, out.texture, st);
    note(st, [NSString stringWithFormat:@"MetalFX %lux%lu -> %lux%lu%@", (unsigned long)src.width,
              (unsigned long)src.height, (unsigned long)st.outWidth, (unsigned long)st.outHeight,
              sharpened ?: @""]);
    present(out);
    return YES;
}

static BOOL upscaled(id<MTLCommandBuffer> cb, id<MTLDrawable> drawable, void (^present)(id<MTLDrawable>))
{
    return [drawable conformsToProtocol:@protocol(CAMetalDrawable)] && upscaleInto(cb, (id<CAMetalDrawable>)drawable, present);
}

/* An upscaled frame shows only the overlay; every other frame is presented as the game asked. */
static void mnPresent(id<MTLCommandBuffer> self, SEL _cmd, id<MTLDrawable> drawable)
{
    if (!upscaled(self, drawable, ^(id<MTLDrawable> out) { origPresent(self, _cmd, out); })) origPresent(self, _cmd, drawable);
}

static void mnPresentAfter(id<MTLCommandBuffer> self, SEL _cmd, id<MTLDrawable> drawable, CFTimeInterval duration)
{
    if (!upscaled(self, drawable, ^(id<MTLDrawable> out) { origPresentAfter(self, _cmd, out, duration); }))
        origPresentAfter(self, _cmd, drawable, duration);
}

static void mnPresentAt(id<MTLCommandBuffer> self, SEL _cmd, id<MTLDrawable> drawable, CFTimeInterval time)
{
    if (!upscaled(self, drawable, ^(id<MTLDrawable> out) { origPresentAt(self, _cmd, out, time); }))
        origPresentAt(self, _cmd, drawable, time);
}

/* Hooks presentation on this device's command buffers, once per process, the first time something draws. */
static void hookPresent(id<MTLDevice> device)
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        id<MTLCommandBuffer> cb = [[device newCommandQueue] commandBuffer];
        Class c = cb ? object_getClass(cb) : Nil;
        Method m = class_getInstanceMethod(c, @selector(presentDrawable:));
        Method after = class_getInstanceMethod(c, @selector(presentDrawable:afterMinimumDuration:));
        Method at = class_getInstanceMethod(c, @selector(presentDrawable:atTime:));
        if (m) origPresent = (void *)method_setImplementation(m, (IMP)mnPresent);
        if (after) origPresentAfter = (void *)method_setImplementation(after, (IMP)mnPresentAfter);
        if (at) origPresentAt = (void *)method_setImplementation(at, (IMP)mnPresentAt);
    });
}

static id<CAMetalDrawable> mnNextDrawable(CAMetalLayer *self, SEL _cmd)
{
    if (objc_getAssociatedObject(self, kIsOverlay)) return origNextDrawable(self, _cmd);
    MNLayerState *st = stateFor(self);
    if (!st.hdr && isHDR(self)) { st.hdr = YES; note(st, @"left alone (HDR/extended-range layer)"); }
    BOOL tenBit;
    BOOL aa = postAA && !st.hdr && aaView(self.pixelFormat, &tenBit) != MTLPixelFormatInvalid;  // a frame CMAA2 handles
    if ((wantsUpscale(self, st, self.drawableSize) || aa) && self.framebufferOnly) self.framebufferOnly = NO;
    id<CAMetalDrawable> drawable = origNextDrawable(self, _cmd);
    if (self.device) hookPresent(self.device);
    return drawable;
}

__attribute__((constructor)) static void mnInit(void)
{
    const char *scale = getenv("MACNEUTRON_PRESENT_SCALE");
    dumpPath = getenv("MACNEUTRON_PRESENT_DUMP");
    refuseOutput = getenv("MACNEUTRON_PRESENT_REFUSE");
    scaleOverride = scale ? atof(scale) : 0;
    const char *aa = getenv("MACNEUTRON_POST_AA"), *noFX = getenv("MACNEUTRON_NO_METALFX");
    postAA = aa && !strcmp(aa, "cmaa2");
    noMetalFX = noFX && !strcmp(noFX, "1");
    const char *sharpen = getenv("MACNEUTRON_SHARPEN");
    sharpenStrength = sharpen ? (float)fmin(fmax(atof(sharpen), 0.0), 1.0) : 0.0f;
    Method m = class_getInstanceMethod(CAMetalLayer.class, @selector(nextDrawable));
    if (m) origNextDrawable = (void *)method_setImplementation(m, (IMP)mnNextDrawable);
}
