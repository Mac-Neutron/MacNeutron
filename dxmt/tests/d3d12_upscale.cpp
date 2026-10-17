// MetalFX temporal upscaling through DXMT's private D3D12 interface (XeSS answered by MetalFX, spec §4.1). Frames are
// made on the CPU (no shaders): the MetalFX spike's scene (a scrolling checker and rings, three moving discs, one HDR),
// 64 of them point-sampled with Halton(2,3) jitter at the input size, upscaled to 2560x1440. The last output is
// compared with the unjittered full-size scene (PSNR after x/(1+x) per channel, as the spike did) and so is a bilinear
// upscale of the last input. Conventions the spike validated: JitterOffset = -(sample offset), motion vectors =
// -velocity in pixels of the motion texture (+ the jitter's change with jittered motion vectors).
//   d3d12_upscale.exe <mode>...
//   ratio <r>      r = 1.5, 2.0 or 3.0  "upscale ratio <r> ok psnr <scaler> bilinear <bilinear>" (ok: scaler wins)
//   depthstencil   2.0x, a D32_FLOAT_S8X24_UINT depth, jittered motion vectors     "upscale depthstencil ok psnr ..."
//   rtoutput       2.0x, an output with ALLOW_RENDER_TARGET only, R16G16_TYPELESS motion vectors   "upscale rtoutput ok ..."
//   direct         2.0x, an output with ALLOW_UNORDERED_ACCESS only (XeSS's norm)                "upscale direct ok ..."
//   placed         as direct, the output placed in a DEFAULT heap                                "upscale placed ok ..."
//   direct-offset  as direct, at (64, 32) of a 2624x1472 output (outputColorBase)        "upscale direct-offset ok ..."
//   timestamps     as direct, with two timestamps before each upscale and one after (Task F4: MetalFX's own passes
//                  take none; the blits around them do), then "upscale timestamps order <1: the last frame's three
//                  nonzero, in order, the upscale between the last two> gap <ticks>"
//   compute        2.0x, recorded on a COMPUTE command list and queue, with a reactive mask      "upscale compute ok ..."
//   typeless       2.0x, colour and output R16G16B16A16_TYPELESS (read as FLOAT)                "upscale typeless ok ..."
//   typeless32     2.0x, colour and output R32G32B32A32_TYPELESS (DXMT keeps them as RGBA32Uint: read through
//                  RGBA32Float views)                                                           "upscale typeless32 ok ..."
//   typeless10     2.0x, colour and output R10G10B10A2_TYPELESS (RGB10A2Uint, read through RGB10A2Unorm views; the
//                  scene clamped to 1)                                                          "upscale typeless10 ok ..."
//   bad            an unlisted TYPELESS colour (B8G8R8X8, R16G16) at creation, then a null output and a 4.0x input:
//                  all E_INVALIDARG; then an upscale on the same list works                     "upscale bad ok"
//   range          "range <min> <max>" (GetTemporalScalerScaleRange)
// Anything else prints "upscale <mode> FAIL ..." or a failed call. The interface is this program's own copy of DXMT's
// src/d3d12/d3d12_interfaces.hpp (the Wine bridge keeps another): a drift shows here as a failure.
#include "d3d12_common.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <thread>

struct MTL_TEMPORAL_SCALER_D3D12_DESC {
    UINT InputWidth, InputHeight;
    UINT OutputWidth, OutputHeight;
    DXGI_FORMAT ColorFormat, DepthFormat, MotionFormat, OutputFormat;
    DXGI_FORMAT ReactiveMaskFormat;
    BOOL AutoExposure, OutputResolutionMotionVectors, JitteredMotionVectors;
    FLOAT InputContentMinScale, InputContentMaxScale;
};
struct MTL_TEMPORAL_UPSCALE_D3D12_DESC {
    ID3D12Resource *Color, *Depth, *MotionVector, *Exposure, *ReactiveMask, *Output;
    UINT InputContentWidth, InputContentHeight;
    UINT ColorOffsetX, ColorOffsetY, DepthOffsetX, DepthOffsetY, MotionOffsetX, MotionOffsetY,
         ReactiveOffsetX, ReactiveOffsetY, OutputOffsetX, OutputOffsetY;
    FLOAT JitterOffsetX, JitterOffsetY, MotionVectorScaleX, MotionVectorScaleY, PreExposure;
    BOOL Reset, DepthReversed;
};
struct IMTLD3D12TemporalScaler : public IUnknown {};
struct IMTLD3D12DeviceExt : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetTemporalScalerScaleRange(FLOAT *pMin, FLOAT *pMax) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTemporalScaler(const MTL_TEMPORAL_SCALER_D3D12_DESC *pDesc,
                                                           IMTLD3D12TemporalScaler **ppScaler) = 0;
};
struct IMTLD3D12CommandListExt : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE TemporalUpscale(IMTLD3D12TemporalScaler *pScaler,
                                                      const MTL_TEMPORAL_UPSCALE_D3D12_DESC *pDesc) = 0;
};
// a8c64ba7-6d54-4833-8cc6-2605c5b454be, 5690e8a7-51a1-4b37-8bbd-730d0fdb2cec
static const GUID kDeviceExt = {0xa8c64ba7, 0x6d54, 0x4833, {0x8c, 0xc6, 0x26, 0x05, 0xc5, 0xb4, 0x54, 0xbe}};
static const GUID kListExt = {0x5690e8a7, 0x51a1, 0x4b37, {0x8b, 0xbd, 0x73, 0x0d, 0x0f, 0xdb, 0x2c, 0xec}};

static const UINT OW = 2560, OH = 1440, kFrames = 64;
static const auto COPY_DEST = D3D12_RESOURCE_STATE_COPY_DEST, READ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                  COPY_SOURCE = D3D12_RESOURCE_STATE_COPY_SOURCE;

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

// The spike's scene, in output pixels, at frame t: colour, depth, velocity (output px per frame), reactive.
struct Sample { float c[3], d, vx, vy, react; };
static Sample Scene(float qx, float qy, float t) {
    Sample s;
    float bx = qx - 3.0f * t, by = qy - 1.25f * t;
    int cx = (int)std::floor(bx / 5.0f), cy = (int)std::floor(by / 5.0f);
    float chk = ((cx + cy) & 1) ? 1.0f : 0.15f;
    float rings = 0.5f + 0.5f * std::sin(std::hypot(bx - 1280.0f, by - 720.0f) * 0.37f);
    s.c[0] = chk * 0.8f + 0.2f * rings; s.c[1] = 0.3f + 0.5f * rings * chk; s.c[2] = chk * 0.4f + 0.1f;
    s.d = 0.9f; s.vx = 3.0f; s.vy = 1.25f; s.react = 0;
    static const float V[3][2] = {{-6.0f, 2.5f}, {4.5f, -3.5f}, {9.0f, 0.0f}}, C[3][3] = {{4, 3, 1}, {0.1f, 0.8f, 0.3f}, {0.9f, 0.2f, 0.9f}};
    for (int i = 0; i < 3; i++) {
        float px = 640.0f + 600.0f * i + V[i][0] * t, py = 400.0f + 250.0f * i + V[i][1] * t;
        px -= std::floor(px / OW) * OW; // wrap (motion is wrong only at the wrap frame)
        py -= std::floor(py / OH) * OH;
        float dx = qx - px, dy = qy - py, depth = 0.2f + 0.15f * i;
        if (std::hypot(dx, dy) < 150.0f + 40.0f * i && depth < s.d) {
            float st = 0.6f + 0.4f * std::sin(dx * 0.9f + dy * 0.5f);
            for (int k = 0; k < 3; k++) s.c[k] = C[i][k] * st;
            s.d = depth; s.vx = V[i][0]; s.vy = V[i][1]; s.react = i == 2 ? 0.8f : 0.0f;
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

struct Upscale {
    std::string name;
    float ratio = 2.0f;
    DXGI_FORMAT depth = DXGI_FORMAT_D32_FLOAT, motion = DXGI_FORMAT_R16G16_FLOAT, mask = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT color = DXGI_FORMAT_R16G16B16A16_FLOAT, out = DXGI_FORMAT_R16G16B16A16_FLOAT; // 8 or 16 bytes a texel
    D3D12_RESOURCE_FLAGS output = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    bool jittered = false, exposure = false, compute = false, bad = false, placed = false, timestamps = false;
    UINT ox = 0, oy = 0; // where in the output (that much larger) the upscale goes
};

// A buffer to upload `rows` rows of `row` bytes into, its pitch Pitch(row).
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
    dst.SubresourceIndex = 0; // a depth/stencil texture's depth plane
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

static void Run(const Upscale &u) {
    Gpu g;
    if (u.compute) { // Gpu's queue, allocator and list are DIRECT: COMPUTE ones instead
        D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_COMPUTE};
        g.list->Release(); g.allocator->Release(); g.queue->Release();
        CHECK(g.device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&g.queue));
        CHECK(g.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, __uuidof(ID3D12CommandAllocator), (void **)&g.allocator));
        CHECK(g.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, g.allocator, nullptr,
                                          __uuidof(ID3D12GraphicsCommandList), (void **)&g.list));
    }
    IMTLD3D12DeviceExt *ext = nullptr;
    HRESULT hr = g.device->QueryInterface(kDeviceExt, (void **)&ext);
    if (FAILED(hr)) { printf("upscale %s FAIL no IMTLD3D12DeviceExt 0x%08lx\n", u.name.c_str(), (unsigned long)(unsigned)hr); exit(1); }
    IMTLD3D12CommandListExt *list = nullptr;
    CHECK(g.list->QueryInterface(kListExt, (void **)&list));

    UINT iw = (UINT)std::ceil(OW / u.ratio), ih = (UINT)std::ceil(OH / u.ratio);
    float scale = (float)OW / iw;
    int phases = (int)std::ceil(8 * u.ratio * u.ratio);
    MTL_TEMPORAL_SCALER_D3D12_DESC sd = {iw, ih, OW, OH, u.color, u.depth, u.motion, u.out, u.mask, FALSE, FALSE,
                                         u.jittered, 1.0f, 3.0f};
    bool c32 = u.color == DXGI_FORMAT_R32G32B32A32_TYPELESS, o32 = u.out == DXGI_FORMAT_R32G32B32A32_TYPELESS;
    bool c10 = u.color == DXGI_FORMAT_R10G10B10A2_TYPELESS, o10 = u.out == DXGI_FORMAT_R10G10B10A2_TYPELESS;
    IMTLD3D12TemporalScaler *scaler = nullptr;
    if (u.bad) // TYPELESS colours DXMT doesn't read as a typed variant
        for (DXGI_FORMAT f : {DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_R16G16_TYPELESS}) {
            auto refused = sd;
            refused.ColorFormat = f;
            HRESULT hr = ext->CreateTemporalScaler(&refused, &scaler);
            if (hr != E_INVALIDARG || scaler) {
                printf("upscale bad FAIL colour %d 0x%08lx\n", (int)f, (unsigned long)(unsigned)hr);
                exit(1);
            }
        }
    CHECK(ext->CreateTemporalScaler(&sd, &scaler));

    auto DS = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ID3D12Resource *color = g.Texture(Tex2D(iw, ih, u.color), COPY_DEST);
    ID3D12Resource *depth = g.Texture(Tex2D(iw, ih, u.depth, 1, DS), COPY_DEST);
    ID3D12Resource *motion = g.Texture(Tex2D(iw, ih, u.motion), COPY_DEST);
    ID3D12Resource *mask = u.mask ? g.Texture(Tex2D(iw, ih, u.mask), COPY_DEST) : nullptr;
    auto out_state = u.output & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                                                                          : D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12Resource *output;
    D3D12_RESOURCE_DESC od = Tex2D(OW + u.ox, OH + u.oy, u.out, 1, u.output);
    if (u.placed) {
        D3D12_RESOURCE_ALLOCATION_INFO ai = g.device->GetResourceAllocationInfo(0, 1, &od);
        D3D12_HEAP_DESC hd = {ai.SizeInBytes, {D3D12_HEAP_TYPE_DEFAULT}, ai.Alignment, D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES};
        ID3D12Heap *heap;
        CHECK(g.device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
        CHECK(g.device->CreatePlacedResource(heap, 0, &od, out_state, nullptr, __uuidof(ID3D12Resource), (void **)&output));
        heap->Release(); // the output holds it
    } else {
        output = g.Texture(od, out_state);
    }
    ID3D12Resource *exposure = nullptr;
    if (u.exposure) { // 1x1 R16F holding 1.0
        exposure = g.Texture(Tex2D(1, 1, DXGI_FORMAT_R16_FLOAT), COPY_DEST);
        Staging e = MakeStaging(g, 2, 1);
        *(uint16_t *)e.p = ToHalf(1.0f);
        Copy(g, exposure, e, DXGI_FORMAT_R16_FLOAT, 1, 1);
        g.Barrier(exposure, COPY_DEST, READ);
        g.Submit();
    }
    ID3D12QueryHeap *ts_heap = nullptr;
    ID3D12Resource *ts_values = nullptr;
    if (u.timestamps) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 3};
        CHECK(g.device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&ts_heap));
        ts_values = g.Buffer(D3D12_HEAP_TYPE_READBACK, 3 * 8, COPY_DEST);
    }
    Staging sc = MakeStaging(g, iw * (c32 ? 16 : c10 ? 4 : 8), ih), sdp = MakeStaging(g, iw * 4, ih), sm = MakeStaging(g, iw * 4, ih),
            sk = MakeStaging(g, iw, ih);
    std::vector<float> last(iw * ih * 4); // the last frame's colour as uploaded

    for (UINT f = 0; f < kFrames; f++) {
        int i = f % phases + 1, ip = (f + phases - 1) % phases + 1;
        float jx = Halton(i, 2) - 0.5, jy = Halton(i, 3) - 0.5, jpx = Halton(ip, 2) - 0.5, jpy = Halton(ip, 3) - 0.5;
        Rows(ih, [&](UINT y) {
            auto *c = (uint16_t *)(sc.p + (size_t)y * sc.pitch);
            auto *d = (float *)(sdp.p + (size_t)y * sdp.pitch);
            auto *m = (uint16_t *)(sm.p + (size_t)y * sm.pitch);
            auto *k = sk.p + (size_t)y * sk.pitch;
            for (UINT x = 0; x < iw; x++) {
                Sample s = Scene((x + 0.5f + jx) * scale, (y + 0.5f + jy) * scale, (float)f);
                for (int ch = 0; ch < 4; ch++) {
                    float v = ch < 3 ? s.c[ch] : 1.0f;
                    if (c32)
                        ((float *)c)[x * 4 + ch] = v;
                    else if (c10) { // 10 bits a colour, 2 for alpha (3: 1.0)
                        int bits = ch < 3 ? 1023 : 3;
                        uint32_t q = (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * bits);
                        ((uint32_t *)c)[x] = (ch ? ((uint32_t *)c)[x] : 0) | q << (10 * ch);
                        v = (float)q / bits;
                    } else
                        v = FromHalf(c[x * 4 + ch] = ToHalf(v));
                    last[((size_t)y * iw + x) * 4 + ch] = v;
                }
                d[x] = s.d;
                float mx = -s.vx / scale, my = -s.vy / scale;
                if (u.jittered) { mx += jx - jpx; my += jy - jpy; }
                m[x * 2] = ToHalf(mx);
                m[x * 2 + 1] = ToHalf(my);
                k[x] = (uint8_t)std::lround(s.react * 255);
            }
        });
        if (f) {
            for (auto *r : {color, depth, motion, mask})
                if (r) g.Barrier(r, READ, COPY_DEST);
        }
        Copy(g, color, sc, u.color, iw, ih);
        Copy(g, depth, sdp, DXGI_FORMAT_R32_TYPELESS, iw, ih);
        Copy(g, motion, sm, DXGI_FORMAT_R16G16_TYPELESS, iw, ih);
        if (mask) Copy(g, mask, sk, DXGI_FORMAT_R8_UNORM, iw, ih);
        for (auto *r : {color, depth, motion, mask})
            if (r) g.Barrier(r, COPY_DEST, READ);
        MTL_TEMPORAL_UPSCALE_D3D12_DESC ud = {};
        ud.Color = color; ud.Depth = depth; ud.MotionVector = motion; ud.Exposure = exposure; ud.ReactiveMask = mask;
        ud.Output = output;
        ud.OutputOffsetX = u.ox; ud.OutputOffsetY = u.oy;
        ud.InputContentWidth = iw; ud.InputContentHeight = ih;
        ud.JitterOffsetX = -jx; ud.JitterOffsetY = -jy;
        ud.MotionVectorScaleX = ud.MotionVectorScaleY = 1.0f;
        ud.PreExposure = 1.0f;
        ud.Reset = f == 0;
        if (u.bad) { // refused before anything is recorded; the list still works
            auto refused = ud;
            refused.Output = nullptr;
            HRESULT a = list->TemporalUpscale(scaler, &refused);
            refused = ud;
            refused.InputContentWidth = OW / 4; refused.InputContentHeight = OH / 4;
            HRESULT b = list->TemporalUpscale(scaler, &refused);
            HRESULT c = list->TemporalUpscale(scaler, &ud);
            if (a != E_INVALIDARG || b != E_INVALIDARG || c != S_OK) {
                printf("upscale bad FAIL 0x%08lx 0x%08lx 0x%08lx\n", (unsigned long)(unsigned)a, (unsigned long)(unsigned)b, (unsigned long)(unsigned)c);
                exit(1);
            }
        } else {
            if (ts_heap) {
                g.list->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
                g.list->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            }
            CHECK(list->TemporalUpscale(scaler, &ud));
            if (ts_heap) {
                g.list->EndQuery(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 2);
                g.list->ResolveQueryData(ts_heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 3, ts_values, 0);
            }
        }
        g.Submit();
        if (u.bad)
            break;
    }

    // The output back, against the scene at the last frame's time and a bilinear upscale of the last input.
    UINT t = u.bad ? 0 : kFrames - 1, pitch = OW * (o32 ? 16 : o10 ? 4 : 8);
    ID3D12Resource *rb = g.Buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)pitch * OH, COPY_DEST);
    g.Barrier(output, out_state, COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {output, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint.Footprint = {u.out, OW, OH, 1, pitch};
    D3D12_BOX box = {u.ox, u.oy, 0, u.ox + OW, u.oy + OH, 1};
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    g.Submit();
    uint16_t *p;
    CHECK(rb->Map(0, nullptr, (void **)&p));
    std::vector<float> out((size_t)OW * OH * 4), truth(out.size()), bilinear(out.size());
    for (size_t k = 0; k < out.size(); k++)
        out[k] = o32   ? ((float *)p)[k]
                 : o10 ? (float)(((uint32_t *)p)[k / 4] >> (10 * (k % 4)) & (k % 4 < 3 ? 1023 : 3)) / (k % 4 < 3 ? 1023 : 3)
                       : FromHalf(p[k]);
    rb->Unmap(0, nullptr);
    Rows(OH, [&](UINT y) {
        for (UINT x = 0; x < OW; x++) {
            Sample s = Scene(x + 0.5f, y + 0.5f, (float)t);
            float fx = (x + 0.5f) * iw / OW - 0.5f, fy = (y + 0.5f) * ih / OH - 0.5f;
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
    size_t bad_pixels = 0;
    double sum = 0;
    for (size_t k = 0; k < out.size(); k += 4)
        for (int ch = 0; ch < 3; ch++) {
            if (!std::isfinite(out[k + ch])) bad_pixels++;
            else sum += out[k + ch];
        }
    double mean = sum / (out.size() / 4 * 3), scaler_db = Psnr(out, truth), bilinear_db = Psnr(bilinear, truth);
    if (u.bad) {
        bool ok = !bad_pixels && mean > 0.1;
        printf("upscale bad %s", ok ? "ok\n" : "FAIL");
        if (!ok) printf(" non-finite %zu mean %.3f\n", bad_pixels, mean);
    } else {
        bool ok = !bad_pixels && scaler_db > bilinear_db;
        printf("upscale %s %s psnr %.2f bilinear %.2f", u.name.c_str(), ok ? "ok" : "FAIL", scaler_db, bilinear_db);
        if (bad_pixels) printf(" non-finite %zu", bad_pixels);
        printf("\n");
    }
    if (ts_heap) {
        UINT64 *t;
        CHECK(ts_values->Map(0, nullptr, (void **)&t));
        bool order = t[0] && t[0] <= t[1] && t[1] < t[2];
        printf("upscale timestamps order %d gap %llu\n", order ? 1 : 0, (unsigned long long)(t[2] - t[1]));
        ts_values->Unmap(0, nullptr);
    }
    scaler->Release();
    list->Release();
    ext->Release();
}

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: d3d12_upscale.exe <mode>...\n"); return 2; }
    for (int a = 1; a < argc; a++) {
        std::string mode = argv[a];
        Upscale u;
        u.name = mode;
        if (mode == "range") {
            Gpu g;
            IMTLD3D12DeviceExt *ext = nullptr;
            HRESULT hr = g.device->QueryInterface(kDeviceExt, (void **)&ext);
            if (FAILED(hr)) { printf("upscale range FAIL no IMTLD3D12DeviceExt 0x%08lx\n", (unsigned long)(unsigned)hr); return 1; }
            float lo = 0, hi = 0;
            CHECK(ext->GetTemporalScalerScaleRange(&lo, &hi));
            printf("range %.3f %.3f\n", lo, hi);
            ext->Release();
            continue;
        }
        if (mode == "ratio" && a + 1 < argc) {
            u.name = mode + " " + argv[++a];
            u.ratio = (float)atof(argv[a]);
            u.exposure = true;
        } else if (mode == "depthstencil") {
            u.depth = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            u.jittered = true;
        } else if (mode == "rtoutput") {
            u.output = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            u.motion = DXGI_FORMAT_R16G16_TYPELESS;
        } else if (mode == "direct" || mode == "placed" || mode == "direct-offset" || mode == "timestamps") {
            u.output = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            u.placed = mode == "placed";
            u.timestamps = mode == "timestamps";
            if (mode == "direct-offset") { u.ox = 64; u.oy = 32; }
        } else if (mode == "compute") {
            u.compute = true;
            u.mask = DXGI_FORMAT_R8_UNORM;
        } else if (mode == "typeless") {
            u.color = u.out = DXGI_FORMAT_R16G16B16A16_TYPELESS;
        } else if (mode == "typeless32") {
            u.color = u.out = DXGI_FORMAT_R32G32B32A32_TYPELESS;
        } else if (mode == "typeless10") {
            u.color = u.out = DXGI_FORMAT_R10G10B10A2_TYPELESS;
        } else if (mode == "bad") {
            u.bad = true;
        } else {
            printf("unknown mode %s\n", mode.c_str());
            return 2;
        }
        Run(u);
    }
    return 0;
}
