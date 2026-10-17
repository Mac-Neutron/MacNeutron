// XeSS answered by MetalFX (spec §4.2, §7): Wine's builtin libxess.dll driven through XeSS's own API, as Unreal does.
// The library is loaded by full path, the game's way: check.sh passes a copy of this program named libxess.dll (a PE
// that isn't XeSS), so the run fails unless the launcher's libxess=b makes Wine load its builtin instead.
//   d3d12_xess.exe <path to libxess.dll> <mode>...
//   aa | quality | balanced | performance | ultraperf | aaodd
//       "xess <m> input <w>x<h>" (xessGetOptimalInputResolution for 2560x1440; aaodd: aa for 1728x1117, the built-in
//       display's odd height, Task FR), then 64 jittered frames of the spike's scene (d3d12_upscale.cpp's) through
//       xessD3D12Execute: "xess <m> ok psnr <xess> bilinear <bilinear>" (ok: the upscale beats a bilinear upscale of
//       the last input against the scene: unjittered, or for aa, whose "bilinear" is the last input itself, each
//       pixel's average)
//   cycles
//       Init twice on one context (1920x1080, then 2560x1440), 64 frames, the context destroyed after the last Execute
//       and before its list is submitted: the output is still right. 20 contexts made, upscaling a frame and destroyed.
//       A history reset on a converged context's last frame: closer to bilinear than to the converged upscale.
//       GPU memory (macOS 27.0.1's MetalFX never frees a temporal upscaler, ~230 MB each, so DXMT keeps released ones
//       for the next of the same kind; the bridge makes one at a context's first Execute, Ruling 17): the two Inits
//       grow it by less than 10 MB ("init"); another context's first Execute, while the destroyed one's upscaler is
//       still in the unsubmitted list, gets a new one (early: more than 150 MB); the 20 contexts grow it by less than
//       300 MB (one upscaler at most); balanced and performance alternating 10 times on one context, by less than
//       300 MB (the performance one made once: the two held). With one context live, 5 other settings each upscaling a
//       frame and released in turn ("five": their upscalers), then the first 4 again: less than 100 MB (DXMT keeps 4
//       released besides the live one); the live context's output still beats bilinear. "xess cycles ok psnr <s>
//       bilinear <b> reset <r> init <MB> early <MB> growth <MB> ab <MB> five <MB> rerun <MB> live <s>/<b>"
//   reuse
//       Balanced (1280x720) on 1440x810 textures: Init makes no upscaler, the first Execute one; content 1280x720,
//       1152x648, then 1024x576 (dynamic resolution) keeps it; then RG32F motion vectors instead of RG16F make one new
//       one. Both outputs beat bilinear. "xess reuse ok psnr <s> bilinear <b> psnr <s> bilinear <b> upscalers 0/1/2"
//       (made by Init, by the end of the dynamic frames, in all; counted by a device wrapper the bridge reaches DXMT
//       through)
//   flags
//       Init with bits 5 and 30 (external descriptor heap, profiling): SUCCESS; with bit 9: INVALID_ARGUMENT;
//       xessGetIntelXeFXVersion 0.0.0, xessGetVersion 2.0.1; a destroyed context: INVALID_CONTEXT. Then 64 balanced
//       frames each with bit 1 (inverted depth, the depth 1 - d), bit 4 (NDC velocity: pixels / (0.5 w, -0.5 h)),
//       bit 7 (jittered motion vectors: the jitter's change added) and bit 0 (motion vectors at 2560x1440, in output
//       pixels), each beating bilinear; then Unreal's call in SMITE 2 (Task 3): bits 0 and 8 (auto exposure) and no
//       depth texture (Intel's header: optional with bit 0), beating bilinear.
//       "xess flags ok inverted <s>/<b> ndc <s>/<b> jittered <s>/<b> highres <s>/<b> nodepth <s>/<b>"
//   nodepth
//       Unreal's call (bits 0 and 8, no depth texture) recorded on a COMPUTE list and queue (d3d12_upscale's compute),
//       then with inverted depth too (bit 1: the bridge's depth at 0.0), 64 balanced frames each, beating bilinear;
//       the first run's depth read back after, every texel 1.0 (-1: no depth was seen). The inverted one isn't read
//       back: fresh memory already reads 0.0, so an uncleared depth would pass (Task FR, measured with a build that
//       skips that clear, even after a far plane at 1.0 was just released).
//       "xess nodepth ok compute <s>/<b> depth 1.0 <texels off> inverted <s>/<b>"
//   allocator
//       (Task FR) Balanced's first Execute recorded into a list of another allocator, run, and that list and allocator
//       released without a Reset, the context destroyed; then a new context's first Execute: DXMT reuses the released
//       upscaler, so GPU memory grows by less than 150 MB (a new one is ~230 MB). "xess allocator ok growth <MB>"
//   api
//       (Task FR) XeSS's API contract beside the frames: xessGetPipelineBuildStatus before xessD3D12BuildPipelines,
//       after it, after Init and after another BuildPipelines; Execute without the exposure texture its Init flag 2
//       asks for, and without the responsive mask flag 3 asks for; the level a logging callback registered at ERROR
//       gets an Init refusal at, and how many calls; the calls the bridge doesn't implement on a live context, then
//       one on a destroyed context. "xess api status <a>/<b>/<c>/<d> exposure <r> mask <r> log <calls>:<level>
//       unimplemented <r>,... stale <r>" (check.sh pins the values)
//   unsupported
//       (Wine's wined3d) xessD3D12CreateContext: UNSUPPORTED_DEVICE and a NULL context. "xess unsupported ok"
// Anything else prints "xess <mode> FAIL ..." or a failed call.
//
// The inputs follow Intel's XeSS-SR Developer Guide 2.0: jitterOffset is the projection matrix's (Jx, Jy), which
// moves the samples by -(Jx, Jy) input pixels; motion vectors are the motion from the current frame to the previous
// one, in pixels of the motion texture (low-res, so input pixels).
#include "d3d12_common.hpp"
#include <dxgi1_4.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

/* The XeSS declarations below (types and values) are from Intel's public inc/xess/xess.h and xess_d3d12.h
 * (github.com/intel/xess, branch main):
 *
 * Copyright (c) 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of
 * the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#pragma pack(push, 8)
typedef struct _xess_context_handle_t *xess_context_handle_t;
typedef struct _xess_version_t { uint16_t major; uint16_t minor; uint16_t patch; uint16_t reserved; } xess_version_t;
typedef struct _xess_2d_t { uint32_t x; uint32_t y; } xess_2d_t;
typedef xess_2d_t xess_coord_t;
typedef struct _xess_d3d12_execute_params_t {
    ID3D12Resource *pColorTexture;
    ID3D12Resource *pVelocityTexture;
    ID3D12Resource *pDepthTexture;
    ID3D12Resource *pExposureScaleTexture;
    ID3D12Resource *pResponsivePixelMaskTexture;
    ID3D12Resource *pOutputTexture;
    float jitterOffsetX;
    float jitterOffsetY;
    float exposureScale;
    uint32_t resetHistory;
    uint32_t inputWidth;
    uint32_t inputHeight;
    xess_coord_t inputColorBase;
    xess_coord_t inputMotionVectorBase;
    xess_coord_t inputDepthBase;
    xess_coord_t inputResponsiveMaskBase;
    xess_coord_t reserved0;
    xess_coord_t outputColorBase;
    ID3D12DescriptorHeap *pDescriptorHeap;
    uint32_t descriptorHeapOffset;
} xess_d3d12_execute_params_t;
typedef struct _xess_d3d12_init_params_t {
    xess_2d_t outputResolution;
    int qualitySetting; /* xess_quality_settings_t */
    uint32_t initFlags;
    uint32_t creationNodeMask;
    uint32_t visibleNodeMask;
    ID3D12Heap *pTempBufferHeap;
    uint64_t bufferHeapOffset;
    ID3D12Heap *pTempTextureHeap;
    uint64_t textureHeapOffset;
    ID3D12PipelineLibrary *pPipelineLibrary;
} xess_d3d12_init_params_t;
#pragma pack(pop)
enum { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE = 100, XESS_QUALITY_SETTING_PERFORMANCE = 101,
       XESS_QUALITY_SETTING_BALANCED = 102, XESS_QUALITY_SETTING_QUALITY = 103, XESS_QUALITY_SETTING_AA = 106 };
enum { XESS_RESULT_SUCCESS = 0, XESS_RESULT_ERROR_UNSUPPORTED_DEVICE = -1, XESS_RESULT_ERROR_INVALID_ARGUMENT = -4,
       XESS_RESULT_ERROR_INVALID_CONTEXT = -8 };
enum { XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE = 1 << 2, XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK = 1 << 3 };
enum { XESS_LOGGING_LEVEL_ERROR = 3 };
typedef void (*xess_app_log_callback_t)(const char *message, int loggingLevel);
/* End of Intel's declarations. */
static_assert(sizeof(xess_d3d12_execute_params_t) == 136 && sizeof(xess_d3d12_init_params_t) == 64, "XeSS layout");

static struct {
    int (*CreateContext)(ID3D12Device *, xess_context_handle_t *);
    int (*BuildPipelines)(xess_context_handle_t, ID3D12PipelineLibrary *, bool, uint32_t);
    int (*Init)(xess_context_handle_t, const xess_d3d12_init_params_t *);
    int (*Execute)(xess_context_handle_t, ID3D12GraphicsCommandList *, const xess_d3d12_execute_params_t *);
    int (*DestroyContext)(xess_context_handle_t);
    int (*GetOptimalInputResolution)(xess_context_handle_t, const xess_2d_t *, int, xess_2d_t *, xess_2d_t *, xess_2d_t *);
    int (*GetIntelXeFXVersion)(xess_context_handle_t, xess_version_t *);
    int (*GetVersion)(xess_version_t *);
    int (*GetPipelineBuildStatus)(xess_context_handle_t);
    int (*SetLoggingCallback)(xess_context_handle_t, int, xess_app_log_callback_t);
} X;
static HMODULE xess_dll;

static UINT OW = 2560, OH = 1440; // the output: aaodd sets 1728x1117
static const UINT kFrames = 64;
static const auto COPY_DEST = D3D12_RESOURCE_STATE_COPY_DEST, READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  COPY_SOURCE = D3D12_RESOURCE_STATE_COPY_SOURCE, UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

// From here to Psnr: d3d12_upscale.cpp's scene and measure, the scale per axis.
static uint16_t ToHalf(float f) { // finite values well inside half's range; tiny ones flush to 0
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = x >> 16 & 0x8000;
    int e = (int)(x >> 23 & 255) - 112;
    if (e <= 0)
        return sign;
    return sign | ((e << 10) + (((x & 0x7fffff) + 0x1000) >> 13));
}
static float FromHalf(uint16_t h) {
    int e = h >> 10 & 31, m = h & 1023;
    float v = e ? std::ldexp(1024.0f + m, e - 25) : std::ldexp((float)m, -24);
    return e == 31 ? NAN : (h & 0x8000 ? -v : v);
}

// The spike's scene, in output pixels, at frame t: colour, depth, velocity (output px per frame).
struct Sample { float c[3], d, vx, vy; };
static Sample Scene(float qx, float qy, float t) {
    Sample s;
    float bx = qx - 3.0f * t, by = qy - 1.25f * t;
    int cx = (int)std::floor(bx / 5.0f), cy = (int)std::floor(by / 5.0f);
    float chk = ((cx + cy) & 1) ? 1.0f : 0.15f;
    float rings = 0.5f + 0.5f * std::sin(std::hypot(bx - 1280.0f, by - 720.0f) * 0.37f);
    s.c[0] = chk * 0.8f + 0.2f * rings; s.c[1] = 0.3f + 0.5f * rings * chk; s.c[2] = chk * 0.4f + 0.1f;
    s.d = 0.9f; s.vx = 3.0f; s.vy = 1.25f;
    static const float V[3][2] = {{-6.0f, 2.5f}, {4.5f, -3.5f}, {9.0f, 0.0f}}, C[3][3] = {{4, 3, 1}, {0.1f, 0.8f, 0.3f}, {0.9f, 0.2f, 0.9f}};
    for (int i = 0; i < 3; i++) {
        float px = 640.0f + 600.0f * i + V[i][0] * t, py = 400.0f + 250.0f * i + V[i][1] * t;
        px -= std::floor(px / OW) * OW; // wrap (motion is wrong only at the wrap frame)
        py -= std::floor(py / OH) * OH;
        float dx = qx - px, dy = qy - py, depth = 0.2f + 0.15f * i;
        if (std::hypot(dx, dy) < 150.0f + 40.0f * i && depth < s.d) {
            float st = 0.6f + 0.4f * std::sin(dx * 0.9f + dy * 0.5f);
            for (int k = 0; k < 3; k++) s.c[k] = C[i][k] * st;
            s.d = depth; s.vx = V[i][0]; s.vy = V[i][1];
        }
    }
    return s;
}

static double Halton(int i, int b) { double f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }

// Rows [0, h) split over the CPU's cores (FEX runs x64 threads in parallel).
template <typename F> static void Rows(UINT h, F f) {
    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned k = 0; k < n; k++)
        threads.emplace_back([=] { for (UINT y = k; y < h; y += n) f(y); });
    for (auto &t : threads) t.join();
}

static UINT Pitch(UINT bytes) { return (bytes + 255) & ~255u; }

struct Staging {
    ID3D12Resource *buffer;
    uint8_t *p;
    UINT pitch;
};
static Staging MakeStaging(Gpu &g, UINT row, UINT rows) {
    Staging s = {g.Buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)Pitch(row) * rows, D3D12_RESOURCE_STATE_GENERIC_READ), nullptr, Pitch(row)};
    CHECK(s.buffer->Map(0, nullptr, (void **)&s.p));
    return s;
}
static void Copy(Gpu &g, ID3D12Resource *tex, const Staging &s, DXGI_FORMAT format, UINT w, UINT h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {s.buffer, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Footprint = {format, w, h, 1, s.pitch};
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

static double Psnr(const std::vector<float> &a, const std::vector<float> &b) { // RGB of RGBA, after x/(1+x)
    auto tm = [](float x) { x = std::max(x, 0.0f); return x / (1.0f + x); };
    double se = 0;
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        for (int c = 0; c < 3; c++) {
            double d = tm(a[i + c]) - tm(b[i + c]);
            se += d * d;
            n++;
        }
    return 10 * std::log10(1.0 / (se / n));
}

static std::string mode; // for messages
[[noreturn]] static void Fail(const char *what, int r) {
    printf("xess %s FAIL %s %d\n", mode.c_str(), what, r);
    exit(1);
}
#define XESS(call) do { int r_ = (call); if (r_ != XESS_RESULT_SUCCESS) Fail(#call, r_); } while (0)

// How a run's inputs are made, with the Init flags that say so.
struct Inputs {
    bool inverted = false; // bit 1: depth 1 - d
    bool ndc = false;      // bit 4: velocity in NDC of the content: pixels / (0.5 w, -0.5 h)
    bool jittered = false; // bit 7: velocity includes the jitter's change
    bool highres = false;  // bit 0: motion vectors at the output size, in output pixels
    bool nodepth = false;  // no depth texture (with bit 0, as Unreal's XeSS plugin)
    DXGI_FORMAT motion = DXGI_FORMAT_R16G16_FLOAT; // or R32G32_FLOAT
    UINT tw = 0, th = 0;   // the input textures' size (0: the content's)
};

// Input textures (colour RGBA16F, D32 depth, motion vectors) holding a content size (Content changes it) and a
// 2560x1440 RGBA16F UAV output.
struct Frames {
    Gpu &g;
    Inputs in;
    UINT iw, ih, mw, mh; // the content; the motion texture
    float sx, sy;
    int phases;
    ID3D12Resource *color, *depth, *motion, *output;
    Staging sc, sd, sm;
    std::vector<float> last; // the last frame's colour as uploaded
    bool uploaded = false;

    Frames(Gpu &g, xess_2d_t content, Inputs inputs = {}) : g(g), in(inputs) {
        UINT tw = in.tw ? in.tw : content.x, th = in.th ? in.th : content.y;
        mw = in.highres ? OW : tw;
        mh = in.highres ? OH : th;
        UINT mbytes = in.motion == DXGI_FORMAT_R32G32_FLOAT ? 8 : 4;
        color = g.Texture(Tex2D(tw, th, DXGI_FORMAT_R16G16B16A16_FLOAT), COPY_DEST);
        depth = g.Texture(Tex2D(tw, th, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), COPY_DEST);
        motion = g.Texture(Tex2D(mw, mh, in.motion), COPY_DEST);
        output = g.Texture(Tex2D(OW, OH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), UAV);
        sc = MakeStaging(g, tw * 8, th);
        sd = MakeStaging(g, tw * 4, th);
        sm = MakeStaging(g, mw * mbytes, mh);
        Content(content.x, content.y);
    }

    void Content(UINT w, UINT h) {
        iw = w; ih = h;
        sx = (float)OW / iw; sy = (float)OH / ih;
        phases = (int)std::ceil(8 * sx * sy);
        last.assign((size_t)iw * ih * 4, 0.0f);
    }

    // Motion vector (current to previous) at row y of the motion texture into m: in input pixels, or as `in` says.
    void Motion(uint8_t *m, UINT y, UINT f, float jx, float jy, float jpx, float jpy) {
        bool f32 = in.motion == DXGI_FORMAT_R32G32_FLOAT;
        for (UINT x = 0; x < (in.highres ? OW : iw); x++) {
            float mx, my;
            if (in.highres) { // the output pixel's own motion, in output pixels
                Sample s = Scene(x + 0.5f, y + 0.5f, (float)f);
                mx = -s.vx; my = -s.vy;
            } else {
                Sample s = Scene((x + 0.5f - jx) * sx, (y + 0.5f - jy) * sy, (float)f);
                mx = -s.vx / sx; my = -s.vy / sy;
                if (in.jittered) { mx += jpx - jx; my += jpy - jy; } // the samples' offset is -jitter
                if (in.ndc) { mx /= 0.5f * iw; my /= -0.5f * ih; }
            }
            if (f32) { ((float *)m)[x * 2] = mx; ((float *)m)[x * 2 + 1] = my; }
            else { ((uint16_t *)m)[x * 2] = ToHalf(mx); ((uint16_t *)m)[x * 2 + 1] = ToHalf(my); }
        }
    }

    // Frame f's inputs uploaded on the list (not submitted), and the execute parameters for them.
    xess_d3d12_execute_params_t Record(UINT f, bool reset) {
        int i = f % phases + 1, ip = (f + phases - 1) % phases + 1;
        float jx = Halton(i, 2) - 0.5, jy = Halton(i, 3) - 0.5; // the projection's jitter: samples move by -(jx, jy)
        float jpx = Halton(ip, 2) - 0.5, jpy = Halton(ip, 3) - 0.5;
        Rows(ih, [&](UINT y) {
            auto *c = (uint16_t *)(sc.p + (size_t)y * sc.pitch);
            auto *d = (float *)(sd.p + (size_t)y * sd.pitch);
            for (UINT x = 0; x < iw; x++) {
                Sample s = Scene((x + 0.5f - jx) * sx, (y + 0.5f - jy) * sy, (float)f);
                for (int ch = 0; ch < 4; ch++) {
                    c[x * 4 + ch] = ToHalf(ch < 3 ? s.c[ch] : 1.0f);
                    last[((size_t)y * iw + x) * 4 + ch] = FromHalf(c[x * 4 + ch]);
                }
                d[x] = in.inverted ? 1.0f - s.d : s.d;
            }
            if (!in.highres) Motion(sm.p + (size_t)y * sm.pitch, y, f, jx, jy, jpx, jpy);
        });
        if (in.highres)
            Rows(OH, [&](UINT y) { Motion(sm.p + (size_t)y * sm.pitch, y, f, jx, jy, jpx, jpy); });
        if (uploaded)
            for (auto *r : {color, depth, motion})
                g.Barrier(r, READ, COPY_DEST);
        Copy(g, color, sc, DXGI_FORMAT_R16G16B16A16_FLOAT, iw, ih);
        Copy(g, depth, sd, DXGI_FORMAT_R32_TYPELESS, iw, ih);
        Copy(g, motion, sm, in.motion, in.highres ? OW : iw, in.highres ? OH : ih);
        for (auto *r : {color, depth, motion})
            g.Barrier(r, COPY_DEST, READ);
        uploaded = true;
        xess_d3d12_execute_params_t p = {};
        p.pColorTexture = color; p.pVelocityTexture = motion; p.pDepthTexture = in.nodepth ? nullptr : depth;
        p.pOutputTexture = output;
        p.jitterOffsetX = jx; p.jitterOffsetY = jy;
        p.exposureScale = 1.0f;
        p.resetHistory = reset;
        p.inputWidth = iw; p.inputHeight = ih;
        return p;
    }

    // The output (submitted), against the scene at frame t and a bilinear upscale of the last input: {xess, bilinear}.
    std::pair<double, double> Read(UINT t) {
        UINT pitch = OW * 8;
        ID3D12Resource *rb = g.Buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)pitch * OH, COPY_DEST);
        g.Barrier(output, UAV, COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {output, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, OW, OH, 1, pitch};
        g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        g.Barrier(output, COPY_SOURCE, UAV);
        g.Submit();
        uint16_t *p;
        CHECK(rb->Map(0, nullptr, (void **)&p));
        std::vector<float> out((size_t)OW * OH * 4), truth(out.size()), bilinear(out.size());
        for (size_t k = 0; k < out.size(); k++) out[k] = FromHalf(p[k]);
        rb->Unmap(0, nullptr);
        rb->Release();
        Rows(OH, [&](UINT y) {
            for (UINT x = 0; x < OW; x++) {
                // Native AA's target is the pixel's average (4x4 samples), an upscale's its centre (the spike's).
                Sample s = Scene(x + 0.5f, y + 0.5f, (float)t);
                if (sx == 1.0f && sy == 1.0f) {
                    s.c[0] = s.c[1] = s.c[2] = 0;
                    for (int k = 0; k < 16; k++) {
                        Sample q = Scene(x + (k % 4 + 0.5f) / 4, y + (k / 4 + 0.5f) / 4, (float)t);
                        for (int ch = 0; ch < 3; ch++) s.c[ch] += q.c[ch] / 16;
                    }
                }
                float fx = (x + 0.5f) / sx - 0.5f, fy = (y + 0.5f) / sy - 0.5f;
                int x0 = std::clamp((int)std::floor(fx), 0, (int)iw - 1), y0 = std::clamp((int)std::floor(fy), 0, (int)ih - 1);
                int x1 = std::min(x0 + 1, (int)iw - 1), y1 = std::min(y0 + 1, (int)ih - 1);
                float ax = std::clamp(fx - x0, 0.0f, 1.0f), ay = std::clamp(fy - y0, 0.0f, 1.0f);
                size_t o = ((size_t)y * OW + x) * 4;
                for (int ch = 0; ch < 4; ch++) {
                    auto at = [&](int xx, int yy) { return last[((size_t)yy * iw + xx) * 4 + ch]; };
                    truth[o + ch] = ch < 3 ? s.c[ch] : 1.0f;
                    bilinear[o + ch] = (at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay;
                }
            }
        });
        for (size_t k = 0; k < out.size(); k++)
            if (!std::isfinite(out[k])) { printf("xess %s FAIL non-finite output\n", mode.c_str()); exit(1); }
        return {Psnr(out, truth), Psnr(bilinear, truth)};
    }
};

static xess_context_handle_t Create(Gpu &g) {
    xess_context_handle_t ctx = nullptr;
    XESS(X.CreateContext(g.device, &ctx));
    return ctx;
}
static void Init(xess_context_handle_t ctx, int quality, UINT w = OW, UINT h = OH, uint32_t flags = 0) {
    xess_d3d12_init_params_t p = {{w, h}, quality, flags};
    XESS(X.Init(ctx, &p));
}
static xess_2d_t Optimal(xess_context_handle_t ctx, int quality, UINT w = OW, UINT h = OH) {
    xess_2d_t out = {w, h}, opt = {}, lo = {}, hi = {};
    XESS(X.GetOptimalInputResolution(ctx, &out, quality, &opt, &lo, &hi));
    return opt;
}
// Frames [from, to) executed and submitted one by one; the last one's list is submitted after `before_last` runs.
template <typename F> static void Run(Frames &fr, xess_context_handle_t ctx, UINT from, UINT to, F before_last) {
    for (UINT f = from; f < to; f++) {
        auto p = fr.Record(f, f == 0);
        XESS(X.Execute(ctx, fr.g.list, &p));
        if (f + 1 == to) before_last();
        fr.g.Submit();
    }
}

// The GPU memory in use (DXMT: the Metal device's currentAllocatedSize, which MetalFX's upscalers count in).
static double GpuMB() {
    IDXGIFactory1 *factory;
    IDXGIAdapter *adapter;
    IDXGIAdapter3 *adapter3;
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory));
    CHECK(factory->EnumAdapters(0, &adapter));
    CHECK(adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void **)&adapter3));
    CHECK(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info));
    adapter3->Release(); adapter->Release(); factory->Release();
    return info.CurrentUsage / 1048576.0;
}

static void Quality(int quality) {
    Gpu g;
    xess_context_handle_t ctx = Create(g);
    xess_2d_t in = Optimal(ctx, quality);
    printf("xess %s input %ux%u\n", mode.c_str(), in.x, in.y);
    XESS(X.BuildPipelines(ctx, nullptr, true, 0));
    Init(ctx, quality);
    Frames fr(g, in);
    Run(fr, ctx, 0, kFrames, [] {});
    auto [s, b] = fr.Read(kFrames - 1);
    XESS(X.DestroyContext(ctx));
    printf("xess %s %s psnr %.2f bilinear %.2f\n", mode.c_str(), s > b ? "ok" : "FAIL", s, b);
}

static void Cycles() {
    Gpu g;
    // Re-initialised (another output size), then destroyed before the last list runs. Meanwhile another context of
    // the same kind can't have that upscaler: the list still uses it.
    xess_context_handle_t ctx = Create(g);
    double before = GpuMB();
    Init(ctx, XESS_QUALITY_SETTING_BALANCED, 1920, 1080);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    double init = GpuMB() - before; // Init makes no upscaler (Ruling 17)
    Frames fr(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED)), second(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED));
    double early = 0;
    Run(fr, ctx, 0, kFrames, [&] {
        XESS(X.DestroyContext(ctx));
        double before = GpuMB();
        auto other = Create(g);
        Init(other, XESS_QUALITY_SETTING_BALANCED);
        auto p = second.Record(0, true);
        XESS(X.Execute(other, g.list, &p));
        early = GpuMB() - before;
        XESS(X.DestroyContext(other));
    });
    auto [s, b] = fr.Read(kFrames - 1);
    // Contexts made, used and destroyed.
    before = GpuMB();
    for (int i = 0; i < 20; i++) {
        ctx = Create(g);
        Init(ctx, XESS_QUALITY_SETTING_BALANCED);
        Run(fr, ctx, 0, 1, [] {});
        XESS(X.DestroyContext(ctx));
    }
    double growth = GpuMB() - before;
    // A -> B -> A ...: balanced and performance in turn on one context.
    ctx = Create(g);
    Frames perf(g, Optimal(ctx, XESS_QUALITY_SETTING_PERFORMANCE));
    before = GpuMB();
    for (int i = 0; i < 10; i++) {
        bool a = i % 2 == 0;
        Init(ctx, a ? XESS_QUALITY_SETTING_BALANCED : XESS_QUALITY_SETTING_PERFORMANCE);
        Run(a ? fr : perf, ctx, 0, 1, [] {});
    }
    XESS(X.DestroyContext(ctx));
    double ab = GpuMB() - before;
    // A converged history reset on the last frame: what's left is the frame itself.
    ctx = Create(g);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    Run(fr, ctx, 0, kFrames - 1, [] {});
    auto p = fr.Record(kFrames - 1, true);
    XESS(X.Execute(ctx, g.list, &p));
    g.Submit();
    auto [r, rb] = fr.Read(kFrames - 1);
    XESS(X.DestroyContext(ctx));
    // One context live while 5 other settings are made and released in turn, then the first 4 again: DXMT keeps 4
    // released upscalers besides the live one, so the re-run makes none. The live one still upscales afterwards.
    ctx = Create(g);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    Run(fr, ctx, 0, kFrames / 2, [] {});
    struct { int quality; UINT w, h; } set[] = {{XESS_QUALITY_SETTING_AA, OW, OH},
                                                {XESS_QUALITY_SETTING_QUALITY, OW, OH},
                                                {XESS_QUALITY_SETTING_PERFORMANCE, OW, OH},
                                                {XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, OW, OH},
                                                {XESS_QUALITY_SETTING_BALANCED, 1920, 1080}};
    std::vector<Frames> frames; // each setting's inputs: its first Execute makes its upscaler
    frames.reserve(5);
    for (auto &e : set) frames.emplace_back(g, Optimal(ctx, e.quality, e.w, e.h));
    auto Each = [&](int n) {
        for (int i = 0; i < n; i++) {
            auto other = Create(g);
            Init(other, set[i].quality, set[i].w, set[i].h);
            auto p = frames[i].Record(0, true);
            XESS(X.Execute(other, g.list, &p));
            g.Submit();
            XESS(X.DestroyContext(other));
        }
    };
    before = GpuMB();
    Each(5);
    double five = GpuMB() - before;
    before = GpuMB();
    Each(4);
    double rerun = GpuMB() - before;
    Run(fr, ctx, kFrames / 2, kFrames, [] {});
    auto [l, lb] = fr.Read(kFrames - 1);
    XESS(X.DestroyContext(ctx));
    bool ok = s > b && r < (s + b) / 2 && init < 10 && early > 150 && growth < 300 && ab < 300 && rerun < 100 &&
              l > lb;
    printf("xess cycles %s psnr %.2f bilinear %.2f reset %.2f init %.0f early %.0f growth %.0f ab %.0f five %.0f "
           "rerun %.0f live %.2f/%.2f\n", ok ? "ok" : "FAIL", s, b, r, init, early, growth, ab, five, rerun, l, lb);
}

// An allocator released without a Reset still lets go of what its lists recorded (DXMT's allocator destructor runs
// the encoders' destructors): the upscaler goes back to DXMT's pool.
static void Allocator() {
    Gpu g;
    auto ctx = Create(g);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    Frames fr(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED));
    auto p = fr.Record(0, true);
    g.Submit(); // the uploads
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *list;
    CHECK(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                           (void **)&allocator));
    CHECK(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                      __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    XESS(X.Execute(ctx, list, &p));
    CHECK(list->Close());
    ID3D12CommandList *lists[] = {list};
    g.queue->ExecuteCommandLists(1, lists);
    CHECK(g.queue->Signal(g.fence, ++g.value));
    HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(g.fence->SetEventOnCompletion(g.value, done));
    if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) Fail("timeout", 0);
    CloseHandle(done);
    XESS(X.DestroyContext(ctx));
    list->Release();
    allocator->Release(); // never reset
    double before = GpuMB();
    ctx = Create(g);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    Run(fr, ctx, 1, 2, [] {});
    double growth = GpuMB() - before;
    XESS(X.DestroyContext(ctx));
    printf("xess allocator %s growth %.0f\n", growth < 150 ? "ok" : "FAIL", growth);
}

static int log_calls, log_level = -1;
static void LogCallback(const char *, int level) { log_calls++; log_level = level; }

static void Api() {
    Gpu g;
    // Pipeline build status: only between BuildPipelines and Init (xess.h).
    auto ctx = Create(g);
    int status[4];
    status[0] = X.GetPipelineBuildStatus(ctx);
    XESS(X.BuildPipelines(ctx, nullptr, true, 0));
    status[1] = X.GetPipelineBuildStatus(ctx);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED);
    status[2] = X.GetPipelineBuildStatus(ctx);
    XESS(X.BuildPipelines(ctx, nullptr, true, 0));
    status[3] = X.GetPipelineBuildStatus(ctx);
    // A texture an Init flag asks for, missing at Execute.
    Init(ctx, XESS_QUALITY_SETTING_BALANCED, OW, OH, XESS_INIT_FLAG_EXPOSURE_SCALE_TEXTURE);
    Frames fr(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED));
    auto p = fr.Record(0, true);
    int exposure = X.Execute(ctx, g.list, &p);
    Init(ctx, XESS_QUALITY_SETTING_BALANCED, OW, OH, XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK);
    int mask = X.Execute(ctx, g.list, &p);
    g.Submit();
    // An Init refusal (an unknown flag: this run's first) reaches a callback registered at ERROR.
    XESS(X.SetLoggingCallback(ctx, XESS_LOGGING_LEVEL_ERROR, LogCallback));
    xess_d3d12_init_params_t bad = {{OW, OH}, XESS_QUALITY_SETTING_BALANCED, 1u << 9};
    if (X.Init(ctx, &bad) != XESS_RESULT_ERROR_INVALID_ARGUMENT) Fail("unknown flag accepted", 0);
    // The exports the bridge doesn't implement, on a live context (their arguments as xess.h and xess_debug.h type
    // them; none is read), then one on a destroyed context.
    static const char *const unimplemented[] = {
        "xessForceLegacyScaleFactors", "xessGetExposureMultiplier", "xessSetExposureMultiplier",
        "xessGetMaxResponsiveMaskValue", "xessSetMaxResponsiveMaskValue", "xessSelectNetworkModel", "xessStartDump",
        "xessGetProfilingData", "xessD3D12GetProfilingData", "xessD3D12GetResourcesToDump"};
    using Fn = int (*)(xess_context_handle_t, void *);
    float value = 0;
    std::string codes;
    for (auto *name : unimplemented) {
        auto fn = (Fn)GetProcAddress(xess_dll, name);
        if (!fn) Fail(name, 0);
        codes += (codes.empty() ? "" : ",") + std::to_string(fn(ctx, &value));
    }
    XESS(X.DestroyContext(ctx));
    int stale = ((Fn)GetProcAddress(xess_dll, "xessGetExposureMultiplier"))(ctx, &value);
    printf("xess api status %d/%d/%d/%d exposure %d mask %d log %d:%d unimplemented %s stale %d\n", status[0], status[1],
           status[2], status[3], exposure, mask, log_calls, log_level, codes.c_str(), stale);
}

// The bridge reaches DXMT only through the device's IMTLD3D12DeviceExt (DXMT's src/d3d12/d3d12_interfaces.hpp): a
// device standing in for the real one answers it with a wrapper counting the upscalers made.
struct IMTLD3D12TemporalScaler : IUnknown {};
struct IMTLD3D12DeviceExt : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetTemporalScalerScaleRange(FLOAT *pMin, FLOAT *pMax) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTemporalScaler(const void *pDesc, IMTLD3D12TemporalScaler **ppScaler) = 0;
};
static const GUID kDeviceExt = {0xa8c64ba7, 0x6d54, 0x4833, {0x8c, 0xc6, 0x26, 0x05, 0xc5, 0xb4, 0x54, 0xbe}};
struct Counting : IMTLD3D12DeviceExt { // lives as long as the run
    IMTLD3D12DeviceExt *real = nullptr;
    int made = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        *out = riid == __uuidof(IUnknown) || riid == kDeviceExt ? this : nullptr;
        return *out ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTemporalScalerScaleRange(FLOAT *lo, FLOAT *hi) override {
        return real->GetTemporalScalerScaleRange(lo, hi);
    }
    HRESULT STDMETHODCALLTYPE CreateTemporalScaler(const void *desc, IMTLD3D12TemporalScaler **out) override {
        made++;
        return real->CreateTemporalScaler(desc, out);
    }
};
struct Device : IUnknown {
    Counting ext;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        *out = riid == kDeviceExt ? &ext : nullptr;
        return *out ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
};

static void Reuse() {
    Gpu g;
    Device device;
    CHECK(g.device->QueryInterface(kDeviceExt, (void **)&device.ext.real));
    xess_context_handle_t ctx = nullptr;
    XESS(X.CreateContext((ID3D12Device *)&device, &ctx));
    Init(ctx, XESS_QUALITY_SETTING_BALANCED); // 1280x720
    int init = device.ext.made;
    // Dynamic resolution: the content shrinks in textures larger than it.
    Inputs in;
    in.tw = 1440; in.th = 810;
    Frames dyn(g, {1280, 720}, in);
    Run(dyn, ctx, 0, 16, [] {});
    dyn.Content(1152, 648);
    Run(dyn, ctx, 16, 32, [] {});
    dyn.Content(1024, 576);
    Run(dyn, ctx, 32, kFrames, [] {});
    auto [s1, b1] = dyn.Read(kFrames - 1);
    int kept = device.ext.made;
    // Another motion vector format.
    in.motion = DXGI_FORMAT_R32G32_FLOAT;
    Frames other(g, {1280, 720}, in);
    Run(other, ctx, 0, kFrames, [] {});
    auto [s2, b2] = other.Read(kFrames - 1);
    XESS(X.DestroyContext(ctx));
    bool ok = !init && kept == 1 && device.ext.made == 2 && s1 > b1 && s2 > b2;
    printf("xess reuse %s psnr %.2f bilinear %.2f psnr %.2f bilinear %.2f upscalers %d/%d/%d\n", ok ? "ok" : "FAIL",
           s1, b1, s2, b2, init, kept, device.ext.made);
    device.ext.real->Release();
}

static void Flags() {
    Gpu g;
    xess_context_handle_t ctx = Create(g);
    xess_d3d12_init_params_t p = {{OW, OH}, XESS_QUALITY_SETTING_BALANCED, 1u << 5 | 1u << 30};
    int known = X.Init(ctx, &p);
    p.initFlags = 1u << 9;
    int unknown = X.Init(ctx, &p);
    xess_version_t fx = {1, 1, 1, 1}, v = {};
    int fxr = X.GetIntelXeFXVersion(ctx, &fx), vr = X.GetVersion(&v);
    XESS(X.DestroyContext(ctx));
    int stale = X.Init(ctx, &p), stale_destroy = X.DestroyContext(ctx), null_destroy = X.DestroyContext(nullptr);
    bool ok = known == XESS_RESULT_SUCCESS && unknown == XESS_RESULT_ERROR_INVALID_ARGUMENT && !fxr && !fx.major &&
              !fx.minor && !fx.patch && !vr && v.major == 2 && !v.minor && v.patch == 1 &&
              stale == XESS_RESULT_ERROR_INVALID_CONTEXT && stale_destroy == XESS_RESULT_ERROR_INVALID_CONTEXT &&
              null_destroy == XESS_RESULT_SUCCESS;
    if (!ok)
        printf("xess flags FAIL init %d %d xefx %d %u.%u.%u version %d %u.%u.%u stale %d %d null %d\n", known, unknown,
               fxr, fx.major, fx.minor, fx.patch, vr, v.major, v.minor, v.patch, stale, stale_destroy, null_destroy);
    // Real frames with each flag that changes what the inputs mean.
    std::string psnrs;
    for (int k = 0; k < 5; k++) {
        static const char *const names[] = {"inverted", "ndc", "jittered", "highres", "nodepth"};
        static const uint32_t bits[] = {1u << 1, 1u << 4, 1u << 7, 1u << 0, 1u << 0 | 1u << 8};
        Inputs in;
        in.inverted = k == 0; in.ndc = k == 1; in.jittered = k == 2; in.highres = k >= 3; in.nodepth = k == 4;
        ctx = Create(g);
        Init(ctx, XESS_QUALITY_SETTING_BALANCED, OW, OH, bits[k]);
        Frames fr(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED), in);
        Run(fr, ctx, 0, kFrames, [] {});
        auto [s, b] = fr.Read(kFrames - 1);
        XESS(X.DestroyContext(ctx));
        char line[64];
        snprintf(line, sizeof line, " %s %.2f/%.2f", names[k], s, b);
        psnrs += line;
        ok = ok && s > b;
    }
    printf("xess flags %s%s\n", ok ? "ok" : "FAIL", psnrs.c_str());
}

// The bridge's own depth (Unreal passes none) reaches DXMT only in TemporalUpscale's desc: the list's
// IMTLD3D12CommandListExt (DXMT's object) gets a copy of its method table whose TemporalUpscale keeps the desc's Depth
// (MTL_TEMPORAL_UPSCALE_D3D12_DESC's second field) before calling DXMT's. Once per list.
static const GUID kListExt = {0x5690e8a7, 0x51a1, 0x4b37, {0x8b, 0xbd, 0x73, 0x0d, 0x0f, 0xdb, 0x2c, 0xec}};
using UpscaleFn = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *, const void *);
static UpscaleFn dxmt_upscale;
static ID3D12Resource *bridge_depth;
static HRESULT STDMETHODCALLTYPE KeepDepth(IUnknown *self, IUnknown *scaler, const void *desc) {
    bridge_depth = ((ID3D12Resource *const *)desc)[1];
    return dxmt_upscale(self, scaler, desc);
}
static void HookDepth(ID3D12GraphicsCommandList *list) {
    // IMTLD3D12CommandListExt's whole table: IUnknown's 3 methods and TemporalUpscale (DXMT's d3d12_interfaces.hpp,
    // libxess's dxmt_d3d12_ext.h). A method added to the interface must grow this copy, or the list loses it.
    static void *table[4];
    IUnknown *ext;
    CHECK(list->QueryInterface(kListExt, (void **)&ext));
    void **dxmt = *(void ***)ext;
    memcpy(table, dxmt, sizeof table);
    dxmt_upscale = (UpscaleFn)dxmt[3];
    table[3] = (void *)KeepDepth;
    *(void ***)ext = table;
    ext->Release();
    bridge_depth = nullptr;
}
// The depth the bridge passed (w x h, D32_FLOAT, in NON_PIXEL_SHADER_RESOURCE): its texels not equal to `want`.
static size_t DepthOff(Gpu &g, UINT w, UINT h, float want) {
    if (!bridge_depth) return (size_t)-1;
    UINT pitch = Pitch(w * 4);
    ID3D12Resource *rb = g.Buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)pitch * h, COPY_DEST);
    g.Barrier(bridge_depth, READ, COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {bridge_depth, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_TYPELESS, w, h, 1, pitch};
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    g.Barrier(bridge_depth, COPY_SOURCE, READ);
    g.Submit();
    uint8_t *p;
    CHECK(rb->Map(0, nullptr, (void **)&p));
    size_t off = 0;
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++)
            off += ((float *)(p + (size_t)y * pitch))[x] != want;
    rb->Unmap(0, nullptr);
    rb->Release();
    return off;
}

// Unreal's call without depth on a COMPUTE list (Gpu's queue, allocator and list are DIRECT: COMPUTE ones instead),
// then on a DIRECT one with inverted depth. The first one's bridge depth is read back after: 1.0 everywhere.
static void Nodepth() {
    std::string psnrs;
    bool ok = true;
    for (int k = 0; k < 2; k++) {
        Gpu g;
        if (k == 0) {
            D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_COMPUTE};
            g.list->Release(); g.allocator->Release(); g.queue->Release();
            CHECK(g.device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&g.queue));
            CHECK(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, __uuidof(ID3D12CommandAllocator),
                                                   (void **)&g.allocator));
            CHECK(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, g.allocator, nullptr,
                                              __uuidof(ID3D12GraphicsCommandList), (void **)&g.list));
        }
        HookDepth(g.list);
        Inputs in;
        in.highres = in.nodepth = true;
        auto ctx = Create(g);
        Init(ctx, XESS_QUALITY_SETTING_BALANCED, OW, OH, 1u << 0 | 1u << 8 | (k == 1 ? 1u << 1 : 0));
        Frames fr(g, Optimal(ctx, XESS_QUALITY_SETTING_BALANCED), in);
        Run(fr, ctx, 0, kFrames, [] {});
        auto [s, b] = fr.Read(kFrames - 1);
        size_t off = k ? 0 : DepthOff(g, fr.iw, fr.ih, 1.0f);
        XESS(X.DestroyContext(ctx));
        char line[96];
        if (k) snprintf(line, sizeof line, " inverted %.2f/%.2f", s, b);
        else snprintf(line, sizeof line, " compute %.2f/%.2f depth 1.0 %zd", s, b, (ptrdiff_t)off);
        psnrs += line;
        ok = ok && s > b && !off;
    }
    printf("xess nodepth %s%s\n", ok ? "ok" : "FAIL", psnrs.c_str());
}

// A device without DXMT's interface: Wine's own D3D12, or, where it makes none (no Vulkan), an object answering only
// IUnknown, which is all xessD3D12CreateContext asks of it.
struct Bare : IUnknown {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **out) override { *out = nullptr; return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
};
static void Unsupported() {
    ID3D12Device *device = nullptr;
    static Bare bare;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device);
    if (FAILED(hr)) {
        printf("info no D3D12 device (0x%08lx): a bare IUnknown instead\n", (unsigned long)(unsigned)hr);
        device = (ID3D12Device *)&bare;
    }
    xess_context_handle_t ctx = (xess_context_handle_t)1;
    int r = X.CreateContext(device, &ctx);
    bool ok = r == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE && !ctx;
    printf("xess unsupported %s", ok ? "ok\n" : "FAIL");
    if (!ok) printf(" %d %p\n", r, (void *)ctx);
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { printf("usage: d3d12_xess.exe <libxess.dll> <mode>...\n"); return 2; }
    HMODULE xess = xess_dll = LoadLibraryA(argv[1]);
    if (!xess) { printf("xess FAIL LoadLibrary %s: %lu\n", argv[1], GetLastError()); return 1; }
    static const char *const exports[] = {
        "xessD3D12CreateContext", "xessD3D12BuildPipelines", "xessD3D12Init", "xessD3D12GetInitParams",
        "xessD3D12Execute", "xessDestroyContext", "xessGetOptimalInputResolution", "xessGetInputResolution",
        "xessGetIntelXeFXVersion", "xessSetLoggingCallback", "xessSetJitterScale", "xessSetVelocityScale",
        "xessGetJitterScale", "xessGetVelocityScale", "xessGetProperties", "xessIsOptimalDriver",
        "xessGetPipelineBuildStatus", "xessGetVersion"};
    for (auto *name : exports)
        if (!GetProcAddress(xess, name)) { printf("xess FAIL no export %s\n", name); return 1; }
    auto get = [&](auto &f, const char *name) { f = (std::remove_reference_t<decltype(f)>)GetProcAddress(xess, name); };
    get(X.CreateContext, "xessD3D12CreateContext");
    get(X.BuildPipelines, "xessD3D12BuildPipelines");
    get(X.Init, "xessD3D12Init");
    get(X.Execute, "xessD3D12Execute");
    get(X.DestroyContext, "xessDestroyContext");
    get(X.GetOptimalInputResolution, "xessGetOptimalInputResolution");
    get(X.GetIntelXeFXVersion, "xessGetIntelXeFXVersion");
    get(X.GetVersion, "xessGetVersion");
    get(X.GetPipelineBuildStatus, "xessGetPipelineBuildStatus");
    get(X.SetLoggingCallback, "xessSetLoggingCallback");
    for (int a = 2; a < argc; a++) {
        mode = argv[a];
        if (mode == "aa") Quality(XESS_QUALITY_SETTING_AA);
        else if (mode == "quality") Quality(XESS_QUALITY_SETTING_QUALITY);
        else if (mode == "balanced") Quality(XESS_QUALITY_SETTING_BALANCED);
        else if (mode == "performance") Quality(XESS_QUALITY_SETTING_PERFORMANCE);
        else if (mode == "ultraperf") Quality(XESS_QUALITY_SETTING_ULTRA_PERFORMANCE);
        else if (mode == "aaodd") { OW = 1728; OH = 1117; Quality(XESS_QUALITY_SETTING_AA); OW = 2560; OH = 1440; }
        else if (mode == "cycles") Cycles();
        else if (mode == "reuse") Reuse();
        else if (mode == "flags") Flags();
        else if (mode == "nodepth") Nodepth();
        else if (mode == "allocator") Allocator();
        else if (mode == "api") Api();
        else if (mode == "unsupported") Unsupported();
        else { printf("unknown mode %s\n", mode.c_str()); return 2; }
    }
    return 0;
}
