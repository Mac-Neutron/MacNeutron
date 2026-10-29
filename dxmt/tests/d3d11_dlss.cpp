// DLSS answered by MetalFX through NGX's Direct3D 11 API: DXMT's nvngx.dll loaded by full path, as d3d12_dlss.cpp
// loads it for Direct3D 12. check.sh runs it with MACNEUTRON_DLSS=1 (DXMT_ENABLE_NVEXT=1).
//   d3d11_dlss.exe <path to nvngx.dll> <mode>...
//   quality
//       The render size the capability map's DLSSOptimalSettingsCallback picks for a 1280x720 output, then one frame of
//       a left-to-right gradient (zero motion, depth 0.9, Reset) through CreateFeature and EvaluateFeature into an
//       RGBA16F UAV output: "dlss11 quality render <w>x<h> ok" when the output is the gradient again (its right edge
//       brighter than its left, and not black).
//   refuse
//       A feature that isn't super resolution (frame generation, 11), and an EvaluateFeature without motion vectors:
//       "dlss11 refuse <hex>,<hex>" (each NGX result).
//   cycle
//       CreateFeature, ReleaseFeature, CreateFeature again, ReleaseFeature, then Shutdown1: "dlss11 cycle ok".
// Anything else prints "dlss11 <mode> FAIL ..." or a failed call.
#include "d3d11_common.hpp"
#include <cmath>
#include <string>
#include <vector>

// NGX's parameter map as DXMT's nvngx.hpp lays it out (d3d12_dlss.cpp's mirror).
class NgxParameter {
public:
    virtual void Set(const char *name, void *value) = 0;
    virtual void Set(const char *name, struct ID3D12Resource *value) = 0;
    virtual void Set(const char *name, ID3D11Resource *value) = 0;
    virtual void Set(const char *name, int value) = 0;
    virtual void Set(const char *name, unsigned int value) = 0;
    virtual void Set(const char *name, double value) = 0;
    virtual void Set(const char *name, float value) = 0;
    virtual void Set(const char *name, unsigned long long value) = 0;
    virtual unsigned Get(const char *name, void **out) const = 0;
    virtual unsigned Get(const char *name, struct ID3D12Resource **out) const = 0;
    virtual unsigned Get(const char *name, double *out) const = 0;
    virtual unsigned Get(const char *name, unsigned int *out) const = 0;
    virtual unsigned Get(const char *name, int *out) const = 0;
    virtual unsigned Get(const char *name, ID3D11Resource **out) const = 0;
    virtual unsigned Get(const char *name, float *out) const = 0;
    virtual unsigned Get(const char *name, unsigned long long *out) const = 0;
    virtual void Reset() = 0;
};
enum { NGX_FAIL = 0xbad00000 };
enum { kSuperSampling = 1, kFrameGeneration = 11 };
enum { kMaxQuality = 2 };
enum { kFlagMVLowRes = 1 << 1 };
static bool Failed(unsigned r) { return (r & NGX_FAIL) == NGX_FAIL; }

static struct {
    unsigned (*Init)(unsigned long long, const wchar_t *, ID3D11Device *, const void *, unsigned);
    unsigned (*GetCapabilityParameters)(NgxParameter **);
    unsigned (*AllocateParameters)(NgxParameter **);
    unsigned (*DestroyParameters)(NgxParameter *);
    unsigned (*CreateFeature)(ID3D11DeviceContext *, unsigned, NgxParameter *, unsigned **);
    unsigned (*EvaluateFeature)(ID3D11DeviceContext *, const unsigned *, NgxParameter *, void *);
    unsigned (*ReleaseFeature)(unsigned *);
    unsigned (*Shutdown1)(ID3D11Device *);
} N;

static const UINT OW = 1280, OH = 720;
static std::string mode;
static ID3D11Device *device;
static ID3D11DeviceContext *ctx;

[[noreturn]] static void Fail(const char *what, unsigned r) {
    printf("dlss11 %s FAIL %s 0x%08x\n", mode.c_str(), what, r);
    exit(1);
}
#define NGX(call) do { unsigned r_ = (call); if (Failed(r_)) Fail(#call, r_); } while (0)

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

static ID3D11Texture2D *Texture(UINT w, UINT h, DXGI_FORMAT format, UINT bind, const void *data, UINT pitch) {
    D3D11_TEXTURE2D_DESC d = {w, h, 1, 1, format, {1, 0}, D3D11_USAGE_DEFAULT, bind, 0, 0};
    D3D11_SUBRESOURCE_DATA init = {data, pitch, 0};
    ID3D11Texture2D *t = nullptr;
    CHECK(device->CreateTexture2D(&d, data ? &init : nullptr, &t));
    return t;
}

static std::pair<UINT, UINT> Optimal(int quality) {
    NgxParameter *caps = nullptr;
    NGX(N.GetCapabilityParameters(&caps));
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

static unsigned *Create(NgxParameter *p, UINT w, UINT h) {
    p->Set("Width", w);
    p->Set("Height", h);
    p->Set("OutWidth", OW);
    p->Set("OutHeight", OH);
    p->Set("PerfQualityValue", (int)kMaxQuality);
    p->Set("DLSS.Feature.Create.Flags", (int)kFlagMVLowRes);
    unsigned *handle = nullptr;
    NGX(N.CreateFeature(ctx, kSuperSampling, p, &handle));
    return handle;
}

// The frame's inputs: a gradient colour, depth 0.9 (cleared), zero motion, and the output.
struct Frame { ID3D11Texture2D *color, *depth, *motion, *output; };
static Frame Inputs(UINT w, UINT h) {
    std::vector<uint16_t> color(w * h * 4), motion(w * h * 2, 0);
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++) {
            float v = (x + 0.5f) / w;
            uint16_t *px = &color[(y * w + x) * 4];
            px[0] = px[1] = px[2] = ToHalf(v);
            px[3] = ToHalf(1.0f);
        }
    Frame f;
    f.color = Texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, color.data(), w * 8);
    f.depth = Texture(w, h, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv = {DXGI_FORMAT_D32_FLOAT, D3D11_DSV_DIMENSION_TEXTURE2D};
    ID3D11DepthStencilView *dsv = nullptr;
    CHECK(device->CreateDepthStencilView(f.depth, &dv, &dsv));
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.9f, 0);
    dsv->Release();
    f.motion = Texture(w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, motion.data(), w * 4);
    f.output = Texture(OW, OH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, 0);
    return f;
}

static void SetInputs(NgxParameter *p, const Frame &f, UINT w, UINT h, bool motion) {
    p->Set("Color", (ID3D11Resource *)f.color);
    p->Set("Output", (ID3D11Resource *)f.output);
    p->Set("Depth", (ID3D11Resource *)f.depth);
    if (motion)
        p->Set("MotionVectors", (ID3D11Resource *)f.motion);
    p->Set("Jitter.Offset.X", 0.0f);
    p->Set("Jitter.Offset.Y", 0.0f);
    p->Set("MV.Scale.X", 1.0f);
    p->Set("MV.Scale.Y", 1.0f);
    p->Set("Reset", 1);
    p->Set("DLSS.Pre.Exposure", 1.0f);
    p->Set("DLSS.Render.Subrect.Dimensions.Width", w);
    p->Set("DLSS.Render.Subrect.Dimensions.Height", h);
}

// The output's mean brightness in its left and right tenths.
static std::pair<double, double> Edges(ID3D11Texture2D *output) {
    D3D11_TEXTURE2D_DESC d = {OW, OH, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_STAGING, 0,
                              D3D11_CPU_ACCESS_READ, 0};
    ID3D11Texture2D *staging = nullptr;
    CHECK(device->CreateTexture2D(&d, nullptr, &staging));
    ctx->CopyResource(staging, output);
    D3D11_MAPPED_SUBRESOURCE m;
    CHECK(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m));
    double left = 0, right = 0;
    for (UINT y = 0; y < OH; y++) {
        auto *row = (const uint16_t *)((const uint8_t *)m.pData + y * m.RowPitch);
        for (UINT x = 0; x < OW / 10; x++) {
            left += FromHalf(row[x * 4]);
            right += FromHalf(row[(OW - 1 - x) * 4]);
        }
    }
    ctx->Unmap(staging, 0);
    staging->Release();
    double n = (double)OH * (OW / 10);
    return {left / n, right / n};
}

static void Quality() {
    auto [w, h] = Optimal(kMaxQuality);
    printf("dlss11 quality render %ux%u\n", w, h);
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    unsigned *handle = Create(p, w, h);
    Frame f = Inputs(w, h);
    SetInputs(p, f, w, h, true);
    NGX(N.EvaluateFeature(ctx, handle, p, nullptr));
    auto [left, right] = Edges(f.output);
    NGX(N.ReleaseFeature(handle));
    NGX(N.DestroyParameters(p));
    if (!(right > 0.5 && left < 0.3 && right > left + 0.4))
        printf("dlss11 quality FAIL edges %.3f %.3f\n", left, right);
    else
        printf("dlss11 quality render %ux%u ok\n", w, h);
}

static void Refuse() {
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    p->Set("Width", 640u);
    p->Set("Height", 360u);
    p->Set("OutWidth", OW);
    p->Set("OutHeight", OH);
    unsigned *fg = nullptr;
    unsigned framegen = N.CreateFeature(ctx, kFrameGeneration, p, &fg);
    unsigned *handle = Create(p, 640, 360);
    Frame f = Inputs(640, 360);
    SetInputs(p, f, 640, 360, false);
    unsigned no_motion = N.EvaluateFeature(ctx, handle, p, nullptr);
    NGX(N.ReleaseFeature(handle));
    NGX(N.DestroyParameters(p));
    printf("dlss11 refuse %08x,%08x\n", framegen, no_motion);
}

static void Cycle() {
    NgxParameter *p = nullptr;
    NGX(N.AllocateParameters(&p));
    NGX(N.ReleaseFeature(Create(p, 640, 360)));
    NGX(N.ReleaseFeature(Create(p, 640, 360)));
    NGX(N.DestroyParameters(p));
    NGX(N.Shutdown1(device));
    printf("dlss11 cycle ok\n");
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: d3d11_dlss.exe <nvngx.dll> <mode>...\n"); return 2; }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("dlss11 FAIL LoadLibrary %s: %lu\n", argv[1], GetLastError()); return 1; }
    auto get = [&](auto &f, const char *name) {
        if (!(f = (std::remove_reference_t<decltype(f)>)GetProcAddress(dll, name))) {
            printf("dlss11 FAIL no export %s\n", name);
            exit(1);
        }
    };
    get(N.Init, "NVSDK_NGX_D3D11_Init");
    get(N.GetCapabilityParameters, "NVSDK_NGX_D3D11_GetCapabilityParameters");
    get(N.AllocateParameters, "NVSDK_NGX_D3D11_AllocateParameters");
    get(N.DestroyParameters, "NVSDK_NGX_D3D11_DestroyParameters");
    get(N.CreateFeature, "NVSDK_NGX_D3D11_CreateFeature");
    get(N.EvaluateFeature, "NVSDK_NGX_D3D11_EvaluateFeature");
    get(N.ReleaseFeature, "NVSDK_NGX_D3D11_ReleaseFeature");
    get(N.Shutdown1, "NVSDK_NGX_D3D11_Shutdown1");
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_1;
    CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &ctx));
    for (int a = 2; a < argc; a++) {
        mode = argv[a];
        NGX(N.Init(0x1234, L"C:\\", device, nullptr, 0x15));
        if (mode == "quality") Quality();
        else if (mode == "refuse") Refuse();
        else if (mode == "cycle") Cycle();
        else printf("dlss11 %s FAIL unknown mode\n", mode.c_str());
    }
    return 0;
}
