/* Test program for presenter/check.sh: presents test frames through the presenter's own present hook, natively, on
 * CAMetalLayers outside any window, the way DXMT does (framebufferOnly off), and reports what came out.
 *   cmaa2_check <libmacneutron-present.dylib> frames <w> <h> [fp16|rgb10a2|bgr10a2]   one frame of each test image,
 *                                                                     compared with its input (default BGRA8), alpha too
 *   cmaa2_check <libmacneutron-present.dylib> upscale                 130 frames of 640x360 on a 640x360-point layer
 *   cmaa2_check <libmacneutron-present.dylib> cost <w> <h> [rgb10a2]  GPU time of the presenter's work per frame
 * The presenter reads MACNEUTRON_POST_AA, MACNEUTRON_PRESENT_SCALE and MACNEUTRON_PRESENT_DUMP when it loads: the caller
 * sets them. Output: "<what>: <value>" lines. Exit 1 on a Metal error (Metal's validation layers report through it).
 * The test images are the CMAA2 study's (no anti-aliasing anywhere, 128x128 tiles on a low-contrast gradient): flat
 * (the gradient), sparse (silhouette tiles only) and dense (silhouettes, 1-px lines and 5x7 glyphs with 1-px strokes). */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>

static NSString *const kGen = @"#include <metal_stdlib>\n"
"using namespace metal;\n"
"static inline uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }\n"
"kernel void gen(texture2d<float, access::write> o [[texture(0)]], constant uint &kind0 [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  float2 p = float2(g) + 0.5;\n"
"  float3 c = float3(0.18, 0.2, 0.24) + 0.06 * float3(p.x / 2560.0, p.y / 1440.0, 0.5);\n"
"  if (kind0 == 1u) { o.write(float4(c, 1), g); return; }\n"
"  uint2 t = g / 128u; uint2 q = g % 128u; float2 lp = float2(q) + 0.5;\n"
"  uint h = hash(t.x * 73856093u ^ t.y * 19349663u);\n"
"  uint kind = h % 3u;\n"
"  if (kind0 == 2u && kind != 0u) { o.write(float4(c, 1), g); return; }\n"
"  if (kind == 0u) {\n"
"    if (length(lp - float2(46, 50)) < 30.0) c = float3(0.95, 0.85, 0.2);\n"
"    float a = 0.3 + float(h >> 8 & 255u) / 255.0;\n"
"    float2 d = lp - float2(84, 82); float2 r = float2(cos(a) * d.x + sin(a) * d.y, -sin(a) * d.x + cos(a) * d.y);\n"
"    if (abs(r.x) < 34.0 && abs(r.y) < 12.0) c = float3(0.05, 0.05, 0.08);\n"
"  } else if (kind == 1u) {\n"
"    for (int i = 0; i < 8; i++) {\n"
"      float a = 0.04 + 0.19 * float(i) + float(h >> 4 & 15u) * 0.01;\n"
"      float2 n = float2(-sin(a), cos(a));\n"
"      if (abs(dot(lp - float2(4, 8 + 14 * i), n)) < 0.5) c = (i & 1) ? float3(1, 1, 1) : float3(0.9, 0.1, 0.1);\n"
"    }\n"
"  } else {\n"
"    c = float3(0.06, 0.07, 0.09);\n"
"    uint2 cell = q / uint2(7u, 10u), in = q % uint2(7u, 10u);\n"
"    if (in.x >= 1u && in.x <= 5u && in.y >= 1u && in.y <= 7u && cell.x < 18u) {\n"
"      uint gh = hash(cell.x * 31u + cell.y * 977u + h);\n"
"      if ((gh >> ((in.y - 1u) * 5u + (in.x - 1u))) & 1u) c = float3(0.92, 0.92, 0.88);\n"
"    }\n"
"  }\n"
"  o.write(float4(c, 1), g);\n"
"}\n";

enum { DENSE, FLAT, SPARSE };
static id<MTLDevice> D;
static id<MTLCommandQueue> Q;
static id<MTLComputePipelineState> gen;

static void run(id<MTLCommandBuffer> cb)
{
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
        printf("metal error: %s\n", cb.error.description.UTF8String);
        exit(1);
    }
}

static id<MTLTexture> image(MTLPixelFormat format, NSUInteger w, NSUInteger h, uint32_t kind)
{
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [D newTextureWithDescriptor:td];
    id<MTLCommandBuffer> cb = [Q commandBuffer];
    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:gen]; [ce setTexture:t atIndex:0]; [ce setBytes:&kind length:4 atIndex:0];
    [ce dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [ce endEncoding];
    run(cb);
    return t;
}

static void copyOut(id<MTLCommandBuffer> cb, id<MTLTexture> t, id<MTLBuffer> buf)
{
    NSUInteger bpp = buf.length / (t.width * t.height);
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(t.width, t.height, 1)
              toBuffer:buf destinationOffset:0 destinationBytesPerRow:t.width * bpp destinationBytesPerImage:buf.length];
    [b endEncoding];
}

static CAMetalLayer *layerFor(MTLPixelFormat format, NSUInteger w, NSUInteger h)
{
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = D; layer.pixelFormat = format; layer.framebufferOnly = NO;  // as DXMT's layer (dxmt_presenter.cpp)
    layer.bounds = CGRectMake(0, 0, w, h); layer.drawableSize = CGSizeMake(w, h);
    return layer;
}

/* One frame: the image into a drawable, presented through the hook. Returns the drawable's pixels after the hook's work
 * (in: the image's), or the GPU milliseconds of the present's own command buffer (cost). */
static double present(CAMetalLayer *layer, id<MTLTexture> img, id<MTLBuffer> in, id<MTLBuffer> out)
{
    id<CAMetalDrawable> d = [layer nextDrawable];
    if (!d) { printf("metal error: no drawable\n"); exit(1); }
    id<MTLCommandBuffer> cb = [Q commandBuffer];
    id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromTexture:img toTexture:d.texture];
    [b endEncoding];
    if (in) copyOut(cb, d.texture, in);
    run(cb);
    cb = [Q commandBuffer];
    [cb presentDrawable:d];
    if (out) copyOut(cb, d.texture, out);
    run(cb);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);  // the presenter places its overlay on the main queue;
                                                             // without it an upscale never runs and cost reads 0
    return (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
}

static NSUInteger changed(id<MTLBuffer> a, id<MTLBuffer> b, NSUInteger bpp)
{
    const uint8_t *x = a.contents, *y = b.contents;
    NSUInteger n = 0;
    for (NSUInteger i = 0; i < a.length; i += bpp) n += memcmp(x + i, y + i, bpp) != 0;
    return n;
}

static MTLPixelFormat F = MTLPixelFormatBGRA8Unorm;  // the layer's format: how a pixel's bytes read
static NSUInteger code(const uint8_t *px, int c)  // channel c (0 red, 1 green, 2 blue) of a 10-bit pixel, 0-1023
{
    uint32_t v; memcpy(&v, px, 4);
    int shift = F == MTLPixelFormatRGB10A2Unorm ? 10 * c : 10 * (2 - c);
    return v >> shift & 1023;
}
static double luma(const uint8_t *px)
{
    if (F == MTLPixelFormatBGRA8Unorm) return (0.299 * px[2] + 0.587 * px[1] + 0.114 * px[0]) / 255.0;
    return (0.299 * code(px, 0) + 0.587 * code(px, 1) + 0.114 * code(px, 2)) / 1023.0;
}
static MTLPixelFormat formatNamed(const char *name)
{
    if (!name) return MTLPixelFormatBGRA8Unorm;
    if (!strcmp(name, "fp16")) return MTLPixelFormatRGBA16Float;
    if (!strcmp(name, "rgb10a2")) return MTLPixelFormatRGB10A2Unorm;
    if (!strcmp(name, "bgr10a2")) return MTLPixelFormatBGR10A2Unorm;
    fprintf(stderr, "cmaa2_check: unknown format %s\n", name);
    exit(2);
}
static uint32_t hashu(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }

int main(int argc, char **argv)
{ @autoreleasepool {
    if (argc < 3) { fprintf(stderr, "usage: cmaa2_check <dylib> frames <w> <h> [fp16] | upscale | cost <w> <h>\n"); return 2; }
    if (!dlopen(argv[1], RTLD_NOW)) { printf("can't load the presenter: %s\n", dlerror()); return 1; }
    D = MTLCreateSystemDefaultDevice();
    Q = [D newCommandQueue];
    NSError *error;
    id<MTLLibrary> lib = [D newLibraryWithSource:kGen options:nil error:&error];
    gen = lib ? [D newComputePipelineStateWithFunction:[lib newFunctionWithName:@"gen"] error:&error] : nil;
    if (!gen) { printf("metal error: %s\n", error.description.UTF8String); return 1; }
    const char *mode = argv[2];

    if (!strcmp(mode, "frames") && argc >= 5) {
        NSUInteger w = strtoul(argv[3], NULL, 10), h = strtoul(argv[4], NULL, 10);
        MTLPixelFormat format = F = formatNamed(argc > 5 ? argv[5] : NULL);
        BOOL fp16 = format == MTLPixelFormatRGBA16Float, ten = format != MTLPixelFormatBGRA8Unorm && !fp16;
        NSUInteger bpp = fp16 ? 8 : 4;
        CAMetalLayer *layer = layerFor(format, w, h);
        id<MTLBuffer> in = [D newBufferWithLength:w * h * bpp options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [D newBufferWithLength:w * h * bpp options:MTLResourceStorageModeShared];
        const char *names[3] = {"dense", "flat", "sparse"};
        NSUInteger alpha = 0;  // pixels whose alpha changed, over the three images (upstream CMAA2 writes alpha 0)
        for (uint32_t kind = 0; kind < 3; kind++) {
            present(layer, image(format, w, h, kind), in, out);
            printf("%s changed: %lu\n", names[kind], (unsigned long)changed(in, out, bpp));
            if (fp16) continue;
            const uint8_t *a = in.contents, *b = out.contents;
            for (NSUInteger i = 0; i < w * h * 4; i += 4) {
                uint32_t x, y; memcpy(&x, a + i, 4); memcpy(&y, b + i, 4);
                alpha += ten ? (x >> 30) != (y >> 30) : a[i + 3] != b[i + 3];
            }
            if (kind == SPARSE) {  // changed pixels more than 1 px from an edge (a 4-neighbour step of luma > 0.05)
                NSUInteger far = 0;
                for (NSUInteger y = 0; y < h; y++) for (NSUInteger x = 0; x < w; x++) {
                    if (!memcmp(a + (y * w + x) * 4, b + (y * w + x) * 4, 4)) continue;
                    BOOL near = NO;
                    for (NSInteger dy = -1; dy <= 1 && !near; dy++) for (NSInteger dx = -1; dx <= 1 && !near; dx++) {
                        NSInteger px = (NSInteger)x + dx, py = (NSInteger)y + dy;
                        if (px < 0 || py < 0 || px >= (NSInteger)w || py >= (NSInteger)h) continue;
                        double l = luma(a + (py * w + px) * 4);
                        if ((px > 0 && fabs(l - luma(a + (py * w + px - 1) * 4)) > 0.05)
                            || (px + 1 < (NSInteger)w && fabs(l - luma(a + (py * w + px + 1) * 4)) > 0.05)
                            || (py > 0 && fabs(l - luma(a + ((py - 1) * w + px) * 4)) > 0.05)
                            || (py + 1 < (NSInteger)h && fabs(l - luma(a + ((py + 1) * w + px) * 4)) > 0.05))
                            near = YES;
                    }
                    far += !near;
                }
                printf("sparse changed far from edges: %lu\n", (unsigned long)far);
                printf("sparse changed per mille: %.2f\n", 1000.0 * changed(in, out, bpp) / (w * h));
                if (ten) {  // blended channel values a 10-bit code an 8-bit value can't give: none if 8 bits were in between
                    NSUInteger values = 0, off = 0;
                    for (NSUInteger i = 0; i < w * h * 4; i += 4) for (int c = 0; c < 3; c++) {
                        NSUInteger v = code(b + i, c);
                        if (v == code(a + i, c)) continue;
                        values++;
                        off += (NSUInteger)lround(lround(v * 255.0 / 1023) * 1023.0 / 255) != v;
                    }
                    printf("sparse blended values off the 8-bit grid percent: %.1f\n", values ? 100.0 * off / values : 0);
                }
            }
            if (kind == DENSE) {  // glyph tiles: stroke-to-background luma contrast kept, over every 4-neighbour pair
                double cin = 0, cout = 0;
                for (NSUInteger y = 1; y + 1 < h; y++) for (NSUInteger x = 1; x + 1 < w; x++) {
                    if (hashu((uint32_t)(x / 128) * 73856093u ^ (uint32_t)(y / 128) * 19349663u) % 3u != 2u) continue;
                    NSUInteger i = (y * w + x) * 4;
                    if (luma(a + i) <= 0.5) continue;
                    NSUInteger nb[4] = {i - 4, i + 4, i - w * 4, i + w * 4};
                    for (int k = 0; k < 4; k++) if (luma(a + nb[k]) < 0.5) {
                        cin += luma(a + i) - luma(a + nb[k]);
                        cout += luma(b + i) - luma(b + nb[k]);
                    }
                }
                printf("glyph contrast kept percent: %.1f\n", cin > 0 ? 100.0 * cout / cin : 0);
            }
        }
        if (!fp16) printf("alpha changed: %lu\n", (unsigned long)alpha);
        return 0;
    }

    if (!strcmp(mode, "upscale")) {  // with MACNEUTRON_PRESENT_SCALE=2: MetalFX to 1280x720
        CAMetalLayer *layer = layerFor(MTLPixelFormatBGRA8Unorm, 640, 360);
        id<MTLTexture> img = image(MTLPixelFormatBGRA8Unorm, 640, 360, DENSE);
        id<MTLBuffer> in = [D newBufferWithLength:640 * 360 * 4 options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [D newBufferWithLength:640 * 360 * 4 options:MTLResourceStorageModeShared];
        for (int i = 0; i < 130; i++) {
            present(layer, img, in, out);
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, false);  // the presenter places its overlay on the main queue
        }
        printf("drawable changed: %lu\n", (unsigned long)changed(in, out, 4));
        return 0;
    }

    if (!strcmp(mode, "cost") && argc >= 5) {
        NSUInteger w = strtoul(argv[3], NULL, 10), h = strtoul(argv[4], NULL, 10);
        MTLPixelFormat format = formatNamed(argc > 5 ? argv[5] : NULL);
        CAMetalLayer *layer = layerFor(format, w, h);
        const char *names[3] = {"dense", "flat", "sparse"};
        for (uint32_t kind = 0; kind < 3; kind++) {
            id<MTLTexture> img = image(format, w, h, kind);
            double ms[100];
            for (int i = 0; i < 20; i++) present(layer, img, nil, nil);
            for (int i = 0; i < 100; i++) ms[i] = present(layer, img, nil, nil);
            qsort_b(ms, 100, sizeof(double), ^int(const void *x, const void *y) {
                double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); });
            printf("cost %lux%lu%s%s %s: median %.3f ms, p90 %.3f ms\n", (unsigned long)w, (unsigned long)h,
                   argc > 5 ? " " : "", argc > 5 ? argv[5] : "", names[kind], ms[50], ms[90]);
        }
        return 0;
    }
    fprintf(stderr, "cmaa2_check: unknown mode %s\n", mode);
    return 2;
} }
