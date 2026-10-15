// DLSS answered by MetalFX: DXMT's nvngx.dll driven through NGX's Direct3D 12 API, as Unreal's DLSS plugin does.
// The library is loaded by full path, the NGX SDK's way, from the system32 the launcher installs it in; check.sh runs
// this with DXMT_ENABLE_NVEXT=1 (MACNEUTRON_DLSS's), except for `off`.
//   d3d12_dlss.exe <path to nvngx.dll> <mode>...
//   vendor
//       The adapter DXGI reports and what DXMT's nvapi64.dll says: "dlss vendor <hex> nvapi <status>" (NvAPI_Initialize
//       through nvapi_QueryInterface; 0 is NVAPI_OK).
//   quality | balanced | performance | ultraperf | dlaa
//       The render size the capability map's DLSSOptimalSettingsCallback picks for a 2560x1440 output, then 64
//       jittered frames of d3d12_xess's scene (low-res motion vectors, MVLowRes) through CreateFeature and
//       EvaluateFeature: "dlss <m> render <w>x<h> ok psnr <dlss> bilinear <bilinear>" (ok: the upscale beats a
//       bilinear upscale of the last input against the scene; for dlaa, against the last input itself).
//   inverted
//       Balanced with DepthInverted (the depth 1 - d) and the inputs at an offset in larger textures (subrect bases):
//       "dlss inverted ok psnr <s> bilinear <b>".
//   refuse
//       A feature that isn't super resolution (frame generation, 11), a CreateFeature whose sizes are past MetalFX's 3x,
//       and an EvaluateFeature without motion vectors: each refused, nothing recorded. "dlss refuse <r>,<r>,<r>"
//   off
//       (without DXMT_ENABLE_NVEXT) LoadLibrary refuses nvngx.dll: "dlss off ok".
//   framegen
//       120 frames presented through a 1280x720 swap chain, each a DLSS upscale (640x360 to 1280x720) copied into the
//       back buffer: with MACNEUTRON_FRAMEGEN=1 DXMT interpolates a frame between each two (DXMT_STATS counts them).
//       "dlss framegen presented 120". framegen-soak: 1500 frames, then "dlss framegen growth <MB>" (GPU memory from frame
//       60 to the end: what a leak per frame would show).
//   cost
//       The GPU time of one EvaluateFeature with Palworld's inputs (an R11G11B10 colour, a typeless D32S8 depth, an
//       exposure texture, HDR and inverted depth flags, an RGBA16F UAV output at 2560x1440), from timestamps around 20
//       back-to-back evaluations, the fastest of 5 rounds: "dlss cost <render> <mv> <ms>" for performance (1280x720)
//       and quality (1707x960) renders, with output-resolution motion vectors (Unreal's dilated ones, Palworld's) and
//       render-resolution ones (MVLowRes). Informational: check.sh prints it.
// Anything else prints "dlss <mode> FAIL ..." or a failed call.
//
// The inputs follow NGX's conventions, which are MetalFX's (and XeSS's): the jitter is the projection's, so the samples
// move by -jitter input pixels; motion vectors point from the current frame to the previous one, in render pixels.
#include "d3d12_common.hpp"
#include <dxgi1_4.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

// NGX's parameter map as DXMT's nvngx.hpp lays it out (the slot order games compiled against NVIDIA's SDK call).
class NgxParameter {
public:
    virtual void Set(const char *name, void *value) = 0;
    virtual void Set(const char *name, ID3D12Resource *value) = 0;
    virtual void Set(const char *name, struct ID3D11Resource *value) = 0;
    virtual void Set(const char *name, int value) = 0;
    virtual void Set(const char *name, unsigned int value) = 0;
    virtual void Set(const char *name, double value) = 0;
    virtual void Set(const char *name, float value) = 0;
    virtual void Set(const char *name, unsigned long long value) = 0;
    virtual unsigned Get(const char *name, void **out) const = 0;
    virtual unsigned Get(const char *name, ID3D12Resource **out) const = 0;
    virtual unsigned Get(const char *name, double *out) const = 0;
    virtual unsigned Get(const char *name, unsigned int *out) const = 0;
    virtual unsigned Get(const char *name, int *out) const = 0;
    virtual unsigned Get(const char *name, struct ID3D11Resource **out) const = 0;
    virtual unsigned Get(const char *name, float *out) const = 0;
    virtual unsigned Get(const char *name, unsigned long long *out) const = 0;
    virtual void Reset() = 0;
};
enum { NGX_OK = 1, NGX_FAIL = 0xbad00000 };
enum { kSuperSampling = 1, kFrameGeneration = 11 };
enum { kMaxPerf = 0, kBalanced = 1, kMaxQuality = 2, kUltraPerformance = 3, kDLAA = 5 };
enum { kFlagMVLowRes = 1 << 1, kFlagDepthInverted = 1 << 3 };
static bool Failed(unsigned r) { return (r & NGX_FAIL) == NGX_FAIL; }

static struct {
    unsigned (*Init)(unsigned long long, const wchar_t *, ID3D12Device *, const void *, unsigned);
    unsigned (*GetCapabilityParameters)(NgxParameter **);
    unsigned (*AllocateParameters)(NgxParameter **);
    unsigned (*DestroyParameters)(NgxParameter *);
    unsigned (*CreateFeature)(ID3D12GraphicsCommandList *, unsigned, NgxParameter *, unsigned **);
    unsigned (*EvaluateFeature)(ID3D12GraphicsCommandList *, const unsigned *, NgxParameter *, void *);
    unsigned (*ReleaseFeature)(unsigned *);
    unsigned (*Shutdown1)(ID3D12Device *);
} N;

static const UINT OW = 2560, OH = 1440;
static const UINT kFrames = 64;
static const auto COPY_DEST = D3D12_RESOURCE_STATE_COPY_DEST, READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  COPY_SOURCE = D3D12_RESOURCE_STATE_COPY_SOURCE, UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

// From here to Psnr: d3d12_xess.cpp's (d3d12_upscale.cpp's) scene and measure.
static uint16_t ToHalf(float f) {
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
        px -= std::floor(px / OW) * OW;
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
template <typename F> static void Rows(UINT h, F f) {
    unsigned n = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    for (unsigned k = 0; k < n; k++)
        threads.emplace_back([=] { for (UINT y = k; y < h; y += n) f(y); });
    for (auto &t : threads) t.join();
}
static UINT Pitch(UINT bytes) { return (bytes + 255) & ~255u; }
struct Staging { ID3D12Resource *buffer; uint8_t *p; UINT pitch; };
static Staging MakeStaging(Gpu &g, UINT row, UINT rows) {
    Staging s = {g.Buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)Pitch(row) * rows, D3D12_RESOURCE_STATE_GENERIC_READ), nullptr, Pitch(row)};
    CHECK(s.buffer->Map(0, nullptr, (void **)&s.p));
    return s;
}
static void Copy(Gpu &g, ID3D12Resource *tex, const Staging &s, DXGI_FORMAT format, UINT x, UINT y, UINT w, UINT h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {s.buffer, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Footprint = {format, w, h, 1, s.pitch};
    g.list->CopyTextureRegion(&dst, x, y, 0, &src, nullptr);
}
static double Psnr(const std::vector<float> &a, const std::vector<float> &b) {
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

static std::string mode;
[[noreturn]] static void Fail(const char *what, unsigned r) {
    printf("dlss %s FAIL %s 0x%08x\n", mode.c_str(), what, r);
    exit(1);
}
#define NGX(call) do { unsigned r_ = (call); if (Failed(r_)) Fail(#call, r_); } while (0)

// Colour RGBA16F, D32 depth and RG16F low-res motion vectors holding the render size at (base, base) of textures that
// much larger, and a 2560x1440 RGBA16F UAV output.
struct Frames {
    Gpu &g;
    UINT iw, ih, base;
    bool inverted;
    float sx, sy;
    int phases;
    ID3D12Resource *color, *depth, *motion, *output;
    Staging sc, sd, sm;
    std::vector<float> last;
    bool uploaded = false;

    Frames(Gpu &g, UINT w, UINT h, UINT base = 0, bool inverted = false) : g(g), iw(w), ih(h), base(base), inverted(inverted) {
        color = g.Texture(Tex2D(w + base, h + base, DXGI_FORMAT_R16G16B16A16_FLOAT), COPY_DEST);
        depth = g.Texture(Tex2D(w + base, h + base, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), COPY_DEST);
        motion = g.Texture(Tex2D(w + base, h + base, DXGI_FORMAT_R16G16_FLOAT), COPY_DEST);
        output = g.Texture(Tex2D(OW, OH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), UAV);
        sc = MakeStaging(g, w * 8, h);
        sd = MakeStaging(g, w * 4, h);
        sm = MakeStaging(g, w * 4, h);
        sx = (float)OW / iw; sy = (float)OH / ih;
        phases = (int)std::ceil(8 * sx * sy);
        last.assign((size_t)iw * ih * 4, 0.0f);
    }

    // Frame f's inputs uploaded on the list (not submitted), and its parameters set in `p`.
    void Record(NgxParameter *p, UINT f, bool reset) {
        int i = f % phases + 1;
        float jx = Halton(i, 2) - 0.5, jy = Halton(i, 3) - 0.5;
        Rows(ih, [&](UINT y) {
            auto *c = (uint16_t *)(sc.p + (size_t)y * sc.pitch);
            auto *d = (float *)(sd.p + (size_t)y * sd.pitch);
            auto *m = (uint16_t *)(sm.p + (size_t)y * sm.pitch);
            for (UINT x = 0; x < iw; x++) {
                Sample s = Scene((x + 0.5f - jx) * sx, (y + 0.5f - jy) * sy, (float)f);
                for (int ch = 0; ch < 4; ch++) {
                    c[x * 4 + ch] = ToHalf(ch < 3 ? s.c[ch] : 1.0f);
                    last[((size_t)y * iw + x) * 4 + ch] = FromHalf(c[x * 4 + ch]);
                }
                d[x] = inverted ? 1.0f - s.d : s.d;
                m[x * 2] = ToHalf(-s.vx / sx);
                m[x * 2 + 1] = ToHalf(-s.vy / sy);
            }
        });
        if (uploaded)
            for (auto *r : {color, depth, motion})
                g.Barrier(r, READ, COPY_DEST);
        Copy(g, color, sc, DXGI_FORMAT_R16G16B16A16_FLOAT, base, base, iw, ih);
        Copy(g, depth, sd, DXGI_FORMAT_R32_TYPELESS, base, base, iw, ih);
        Copy(g, motion, sm, DXGI_FORMAT_R16G16_FLOAT, base, base, iw, ih);
        for (auto *r : {color, depth, motion})
            g.Barrier(r, COPY_DEST, READ);
        uploaded = true;
        p->Set("Color", color);
        p->Set("Output", output);
        p->Set("Depth", depth);
        p->Set("MotionVectors", motion);
        p->Set("Jitter.Offset.X", jx);
        p->Set("Jitter.Offset.Y", jy);
        p->Set("MV.Scale.X", 1.0f);
        p->Set("MV.Scale.Y", 1.0f);
        p->Set("Reset", reset ? 1 : 0);
        p->Set("DLSS.Pre.Exposure", 1.0f);
        p->Set("DLSS.Render.Subrect.Dimensions.Width", iw);
        p->Set("DLSS.Render.Subrect.Dimensions.Height", ih);
        for (const char *name : {"DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
                                 "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
                                 "DLSS.Input.MV.SubrectBase.X", "DLSS.Input.MV.SubrectBase.Y"})
            p->Set(name, base);
    }

    // The output (submitted) against the scene at frame t, and a bilinear upscale of the last input: {dlss, bilinear}.
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
            if (!std::isfinite(out[k])) { printf("dlss %s FAIL non-finite output\n", mode.c_str()); exit(1); }
        return {Psnr(out, truth), Psnr(bilinear, truth)};
    }
};

static Gpu *gpu;

// The render size the SDK's NGX_DLSS_GET_OPTIMAL_SETTINGS gets for `quality` at 2560x1440: the capability map's
// callback, called on a map holding the output size and the quality.
static std::pair<UINT, UINT> Optimal(int quality) {
    NgxParameter *caps = nullptr;
    NGX(N.GetCapabilityParameters(&caps));
    unsigned available = 0;
    NGX(caps->Get("SuperSampling.Available", &available));
    if (!available) Fail("SuperSampling.Available", 0);
    void *callback = nullptr;
    NGX(caps->Get("DLSSOptimalSettingsCallback", &callback));
    caps->Set("Width", OW);
    caps->Set("Height", OH);
    caps->Set("PerfQualityValue", quality);
    NGX(((unsigned (*)(NgxParameter *))callback)(caps));
    unsigned w = 0, h = 0;
    NGX(caps->Get("OutWidth", &w));
    NGX(caps->Get("OutHeight", &h));
    NGX(N.DestroyParameters(caps));
    return {w, h};
}

static unsigned *Create(NgxParameter *p, UINT w, UINT h, int quality, int flags) {
    p->Set("Width", w);
    p->Set("Height", h);
    p->Set("OutWidth", OW);
    p->Set("OutHeight", OH);
    p->Set("PerfQualityValue", quality);
    p->Set("DLSS.Feature.Create.Flags", flags);
    unsigned *handle = nullptr;
    NGX(N.CreateFeature(gpu->list, kSuperSampling, p, &handle));
    return handle;
}

static void Upscale(int quality, UINT base = 0, bool inverted = false) {
    auto [w, h] = Optimal(quality);
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    unsigned *handle = Create(p, w, h, quality, kFlagMVLowRes | (inverted ? kFlagDepthInverted : 0));
    Frames fr(*gpu, w, h, base, inverted);
    for (UINT f = 0; f < kFrames; f++) {
        fr.Record(p, f, f == 0);
        NGX(N.EvaluateFeature(gpu->list, handle, p, nullptr));
        gpu->Submit();
    }
    auto [s, b] = fr.Read(kFrames - 1);
    NGX(N.ReleaseFeature(handle));
    NGX(N.DestroyParameters(p));
    bool ok = s > b;
    if (mode == "inverted")
        printf("dlss inverted %s psnr %.2f bilinear %.2f\n", ok ? "ok" : "FAIL", s, b);
    else
        printf("dlss %s render %ux%u %s psnr %.2f bilinear %.2f\n", mode.c_str(), w, h, ok ? "ok" : "FAIL", s, b);
}

static void Refuse() {
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    p->Set("Width", 1280u);
    p->Set("Height", 720u);
    p->Set("OutWidth", OW);
    p->Set("OutHeight", OH);
    unsigned *handle = nullptr;
    unsigned fg = N.CreateFeature(gpu->list, kFrameGeneration, p, &handle);
    p->Set("Width", 640u);
    p->Set("Height", 360u);
    unsigned big = N.CreateFeature(gpu->list, kSuperSampling, p, &handle);
    handle = Create(p, 1280, 720, kBalanced, kFlagMVLowRes);
    Frames fr(*gpu, 1280, 720);
    fr.Record(p, 0, true);
    ID3D12Resource *none = nullptr;
    p->Set("MotionVectors", none);
    unsigned nomv = N.EvaluateFeature(gpu->list, handle, p, nullptr);
    gpu->Submit();
    NGX(N.ReleaseFeature(handle));
    NGX(N.DestroyParameters(p));
    printf("dlss refuse %s,%s,%s\n", Failed(fg) ? "refused" : "taken", Failed(big) ? "refused" : "taken",
           Failed(nomv) ? "refused" : "taken");
}

static void Cost() {
    enum { kFlagIsHDR = 1 << 0 };
    ID3D12QueryHeap *heap;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
    CHECK(gpu->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    ID3D12Resource *readback = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 256, COPY_DEST);
    UINT64 freq = 1;
    CHECK(gpu->queue->GetTimestampFrequency(&freq));
    ID3D12Resource *output = gpu->Texture(Tex2D(OW, OH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), UAV);
    ID3D12Resource *exposure = gpu->Texture(Tex2D(1, 1, DXGI_FORMAT_R32_FLOAT), READ);
    ID3D12Resource *hires = gpu->Texture(Tex2D(OW, OH, DXGI_FORMAT_R16G16_FLOAT), READ);
    for (auto [w, h] : {std::pair<UINT, UINT>{1280, 720}, {1707, 960}})
        for (bool lowres : {false, true}) {
            ID3D12Resource *color = gpu->Texture(Tex2D(w, h, DXGI_FORMAT_R11G11B10_FLOAT), READ);
            ID3D12Resource *depth = gpu->Texture(Tex2D(w, h, DXGI_FORMAT_R32G8X24_TYPELESS, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), READ);
            ID3D12Resource *motion = lowres ? gpu->Texture(Tex2D(w, h, DXGI_FORMAT_R16G16_FLOAT), READ) : hires;
            NgxParameter *p = nullptr;
            NGX(N.AllocateParameters(&p));
            unsigned *handle = Create(p, w, h, kMaxPerf, kFlagIsHDR | kFlagDepthInverted | (lowres ? kFlagMVLowRes : 0));
            p->Set("Color", color);
            p->Set("Output", output);
            p->Set("Depth", depth);
            p->Set("MotionVectors", motion);
            p->Set("ExposureTexture", exposure);
            p->Set("DLSS.Render.Subrect.Dimensions.Width", w);
            p->Set("DLSS.Render.Subrect.Dimensions.Height", h);
            p->Set("Reset", 0);
            const UINT n = 20; // more in one command buffer trips the GPU's timeout (each waits on the Neural Engine)
            double best = 1e30;
            for (int round = 0; round < 6; round++) {
                gpu->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
                for (UINT i = 0; i < n; i++) NGX(N.EvaluateFeature(gpu->list, handle, p, nullptr));
                gpu->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
                gpu->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback, 0);
                gpu->Submit();
                UINT64 *ticks;
                CHECK(readback->Map(0, nullptr, (void **)&ticks));
                double ms = (double)(ticks[1] - ticks[0]) / freq * 1e3 / n;
                readback->Unmap(0, nullptr);
                if (round) best = std::min(best, ms); // round 0 makes the upscaler and warms it
            }
            printf("dlss cost %ux%u %s %.3f\n", w, h, lowres ? "render-res-mv" : "output-res-mv", best);
            NGX(N.ReleaseFeature(handle));
            NGX(N.DestroyParameters(p));
        }
}

// The GPU memory in use (DXMT: the Metal device's currentAllocatedSize, MetalFX's allocations included).
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

static LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

static void FrameGen(UINT frames) {
    const UINT w = 1280, h = 720, rw = 640, rh = 360;
    double settled = 0;
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "d3d12_dlss";
    RegisterClassA(&wc);
    RECT r = {0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("d3d12_dlss", "d3d12_dlss", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, r.right - r.left,
                              r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    IDXGIFactory4 *factory;
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void **)&factory));
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w; sd.Height = h; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *swap1; IDXGISwapChain3 *swap;
    CHECK(factory->CreateSwapChainForHwnd(gpu->queue, hwnd, &sd, nullptr, nullptr, &swap1));
    CHECK(swap1->QueryInterface(__uuidof(IDXGISwapChain3), (void **)&swap));
    ID3D12Resource *buffers[2];
    for (UINT i = 0; i < 2; i++) CHECK(swap->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&buffers[i]));
    ID3D12Resource *color = gpu->Texture(Tex2D(rw, rh, DXGI_FORMAT_R16G16B16A16_FLOAT), READ);
    ID3D12Resource *depth = gpu->Texture(Tex2D(rw, rh, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), READ);
    ID3D12Resource *motion = gpu->Texture(Tex2D(rw, rh, DXGI_FORMAT_R16G16_FLOAT), READ);
    ID3D12Resource *output = gpu->Texture(Tex2D(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), UAV);
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    p->Set("Width", rw); p->Set("Height", rh); p->Set("OutWidth", w); p->Set("OutHeight", h);
    p->Set("PerfQualityValue", (int)kMaxPerf); p->Set("DLSS.Feature.Create.Flags", (int)kFlagMVLowRes);
    unsigned *handle = nullptr;
    NGX(N.CreateFeature(gpu->list, kSuperSampling, p, &handle));
    p->Set("Color", color); p->Set("Output", output); p->Set("Depth", depth); p->Set("MotionVectors", motion);
    p->Set("DLSS.Render.Subrect.Dimensions.Width", rw); p->Set("DLSS.Render.Subrect.Dimensions.Height", rh);
    UINT presented = 0;
    for (UINT f = 0; f < frames; f++) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        p->Set("Reset", f == 0 ? 1 : 0);
        p->Set("Jitter.Offset.X", (float)(Halton(f % 8 + 1, 2) - 0.5));
        p->Set("Jitter.Offset.Y", (float)(Halton(f % 8 + 1, 3) - 0.5));
        NGX(N.EvaluateFeature(gpu->list, handle, p, nullptr));
        ID3D12Resource *back = buffers[swap->GetCurrentBackBufferIndex()];
        gpu->Barrier(output, UAV, COPY_SOURCE);
        gpu->Barrier(back, D3D12_RESOURCE_STATE_PRESENT, COPY_DEST);
        gpu->list->CopyResource(back, output);
        gpu->Barrier(back, COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        gpu->Barrier(output, COPY_SOURCE, UAV);
        gpu->Submit();
        if (SUCCEEDED(swap->Present(0, 0))) presented++;
        if (f == 60) settled = GpuMB();
    }
    NGX(N.ReleaseFeature(handle));
    NGX(N.DestroyParameters(p));
    printf("dlss framegen presented %u\n", presented);
    if (frames > 120) // the soak: GPU memory between frame 60 and the last
        printf("dlss framegen growth %.0f MB\n", GpuMB() - settled);
}

static void Vendor() {
    IDXGIFactory1 *factory;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC desc = {};
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory));
    CHECK(factory->EnumAdapters(0, &adapter));
    CHECK(adapter->GetDesc(&desc));
    adapter->Release();
    factory->Release();
    int status = -999;
    if (HMODULE nvapi = LoadLibraryA("nvapi64.dll")) {
        auto query = (void *(*)(unsigned))GetProcAddress(nvapi, "nvapi_QueryInterface");
        auto initialize = query ? (int (*)())query(0x0150E828) : nullptr;  // NvAPI_Initialize's interface id
        status = initialize ? initialize() : -998;
    }
    printf("dlss vendor %04x nvapi %d\n", desc.VendorId, status);
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 3) { printf("usage: d3d12_dlss.exe <nvngx.dll> <mode>...\n"); return 2; }
    for (int a = 2; a < argc; a++) {
        mode = argv[a];
        if (mode == "off") {
            HMODULE dll = LoadLibraryA(argv[1]);
            printf("dlss off %s\n", dll ? "FAIL loaded" : "ok");
            continue;
        }
        if (mode == "vendor") { Vendor(); continue; }
        static HMODULE dll;
        if (!dll) {
            if (!(dll = LoadLibraryA(argv[1]))) { printf("dlss FAIL LoadLibrary %s: %lu\n", argv[1], GetLastError()); return 1; }
            auto get = [&](auto &f, const char *name) {
                if (!(f = (std::remove_reference_t<decltype(f)>)GetProcAddress(dll, name))) {
                    printf("dlss FAIL no export %s\n", name);
                    exit(1);
                }
            };
            get(N.Init, "NVSDK_NGX_D3D12_Init");
            get(N.GetCapabilityParameters, "NVSDK_NGX_D3D12_GetCapabilityParameters");
            get(N.AllocateParameters, "NVSDK_NGX_D3D12_AllocateParameters");
            get(N.DestroyParameters, "NVSDK_NGX_D3D12_DestroyParameters");
            get(N.CreateFeature, "NVSDK_NGX_D3D12_CreateFeature");
            get(N.EvaluateFeature, "NVSDK_NGX_D3D12_EvaluateFeature");
            get(N.ReleaseFeature, "NVSDK_NGX_D3D12_ReleaseFeature");
            get(N.Shutdown1, "NVSDK_NGX_D3D12_Shutdown1");
            gpu = new Gpu();
            NGX(N.Init(0x1234, L"C:\\", gpu->device, nullptr, 0x15));
        }
        if (mode == "quality") Upscale(kMaxQuality);
        else if (mode == "balanced") Upscale(kBalanced);
        else if (mode == "performance") Upscale(kMaxPerf);
        else if (mode == "ultraperf") Upscale(kUltraPerformance);
        else if (mode == "dlaa") Upscale(kDLAA);
        else if (mode == "inverted") Upscale(kBalanced, 16, true);
        else if (mode == "refuse") Refuse();
        else if (mode == "cost") Cost();
        else if (mode == "framegen") FrameGen(120);
        else if (mode == "framegen-soak") FrameGen(1500);
        else { printf("unknown mode %s\n", mode.c_str()); return 2; }
    }
    if (gpu) N.Shutdown1(gpu->device);
    return 0;
}
