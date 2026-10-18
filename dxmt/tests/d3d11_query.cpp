// D3D11 queries and fences (Direct3D forwarding M4): what a game polls each frame, and the fence a forwarded
// swapchain paces on.
//   d3d11_query.exe <shaders>   prints "query <check> <value>":
//     occlusion   samples passed for a 16x16 draw (GetData polled until it answers; the poll count isn't printed)
//     event       an event query's answer
//     timestamp   a disjoint query's Disjoint, whether its frequency is set, and whether the second timestamp is
//                 at or after the first
//     fence       ID3D11Fence: Signal(5) on ID3D11DeviceContext4, SetEventOnCompletion(5) waited 5 s: the wait's
//                 result and GetCompletedValue
#include "d3d11_common.hpp"

template <class T> static HRESULT Poll(ID3D11DeviceContext *ctx, ID3D11Asynchronous *q, T *out) {
    HRESULT hr;
    for (int i = 0; (hr = ctx->GetData(q, out, sizeof *out, 0)) == S_FALSE && i < 1000000; i++) Sleep(0);
    return hr;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_query.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev(D3D_FEATURE_LEVEL_11_1);
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;
    ID3D11Texture2D *t = dev.Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(t);
    const float white[4] = {1, 1, 1, 1};
    ID3D11Buffer *cb = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, white);
    ID3D11VertexShader *quad = dev.VS("quad.vs");
    ID3D11PixelShader *solid = dev.PS("solid.ps");
    ctx->PSSetConstantBuffers(0, 1, &cb);

    D3D11_QUERY_DESC od = {D3D11_QUERY_OCCLUSION, 0};
    ID3D11Query *occlusion;
    CHECK(d->CreateQuery(&od, &occlusion));
    ctx->Begin(occlusion);
    dev.Draw(quad, solid, rtv, 16, 16);
    ctx->End(occlusion);
    UINT64 samples = 0;
    HRESULT hr = Poll(ctx, occlusion, &samples);
    printf("query occlusion %08lx %llu\n", (unsigned long)(unsigned)hr, (unsigned long long)samples);

    D3D11_QUERY_DESC ed = {D3D11_QUERY_EVENT, 0};
    ID3D11Query *event;
    CHECK(d->CreateQuery(&ed, &event));
    dev.Draw(quad, solid, rtv, 16, 16);
    ctx->End(event);
    BOOL done = FALSE;
    hr = Poll(ctx, event, &done);
    printf("query event %08lx %d\n", (unsigned long)(unsigned)hr, done);

    D3D11_QUERY_DESC jd = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, sd = {D3D11_QUERY_TIMESTAMP, 0};
    ID3D11Query *disjoint, *t0, *t1;
    CHECK(d->CreateQuery(&jd, &disjoint));
    CHECK(d->CreateQuery(&sd, &t0));
    CHECK(d->CreateQuery(&sd, &t1));
    ctx->Begin(disjoint);
    ctx->End(t0);
    dev.Draw(quad, solid, rtv, 16, 16);
    ctx->End(t1);
    ctx->End(disjoint);
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
    UINT64 a = 0, b = 0;
    HRESULT h1 = Poll(ctx, disjoint, &dj), h2 = Poll(ctx, t0, &a), h3 = Poll(ctx, t1, &b);
    printf("query timestamp %08lx %08lx %08lx %d %d %d\n", (unsigned long)(unsigned)h1, (unsigned long)(unsigned)h2,
           (unsigned long)(unsigned)h3, dj.Disjoint, dj.Frequency > 0, b >= a);

    ID3D11Device5 *d5 = nullptr;
    ID3D11DeviceContext4 *c4 = nullptr;
    if (FAILED(d->QueryInterface(__uuidof(ID3D11Device5), (void **)&d5)) ||
        FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&c4))) {
        printf("query fence skip no ID3D11Device5\n");
        return 0;
    }
    ID3D11Fence *fence;
    CHECK(d5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&fence));
    dev.Draw(quad, solid, rtv, 16, 16);
    CHECK(c4->Signal(fence, 5));
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(fence->SetEventOnCompletion(5, e));
    ctx->Flush();
    DWORD w = WaitForSingleObject(e, 5000);
    printf("query fence %lu %llu\n", (unsigned long)w, (unsigned long long)fence->GetCompletedValue());
    CloseHandle(e);
    return 0;
}
