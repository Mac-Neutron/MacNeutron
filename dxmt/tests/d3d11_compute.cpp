// D3D11 compute and UAVs (Direct3D forwarding M4).
//   d3d11_compute.exe <shaders>   prints "compute <check> <value>":
//     structured, raw   a compute shader over a structured and a raw UAV (256 threads): elements wrong
//     append            an append UAV bound beside them with initial count 10: CopyStructureCount's count (Wine's HLSL
//                       compiler has no AppendStructuredBuffer, so the counter is set and read, never appended to)
//     omuav             OMSetRenderTargetsAndUnorderedAccessViews: a 16x16 draw counting pixels into u1 beside an
//                       append view (initial count 7): the counter, the append view's count; then
//                       D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL with a new counter and the append view kept (-1):
//                       its count
//     clearuint, clearfloat, clearview   ClearUnorderedAccessViewUint on a buffer, ClearUnorderedAccessViewFloat on an
//                       R32_FLOAT texture, ClearView on a target with a rectangle: values wrong
#include "d3d11_common.hpp"

static ID3D11UnorderedAccessView *BufferUAV(ID3D11Device *d, ID3D11Buffer *b, DXGI_FORMAT format, UINT n, UINT flags) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
    u.Format = format, u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer = {0, n, flags};
    ID3D11UnorderedAccessView *v;
    CHECK(d->CreateUnorderedAccessView(b, &u, &v));
    return v;
}

static UINT Count(Dev &dev, ID3D11UnorderedAccessView *uav) {
    ID3D11Buffer *b = dev.Buffer(16, 0);
    dev.ctx->CopyStructureCount(b, 0, uav);
    UINT n = As<UINT>(dev.Read(b, 4))[0];
    b->Release();
    return n;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_compute.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;
    const UINT N = 256;

    ID3D11Buffer *sb = dev.Buffer(N * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11Buffer *rb = dev.Buffer(N * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
    ID3D11UnorderedAccessView *uavs[2] = {BufferUAV(d, sb, DXGI_FORMAT_UNKNOWN, N, 0),
                                          BufferUAV(d, rb, DXGI_FORMAT_R32_TYPELESS, N, D3D11_BUFFER_UAV_FLAG_RAW)};
    ctx->CSSetShader(dev.CS("compute.cs"), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    ctx->Dispatch(N / 64, 1, 1);
    std::vector<UINT> s = As<UINT>(dev.Read(sb, N * 4)), r = As<UINT>(dev.Read(rb, N * 4)), ws(N), wr(N);
    for (UINT i = 0; i < N; i++) ws[i] = i * 3 + 1, wr[i] = i * 7;
    printf("compute structured %zu\n", Wrong(s, ws));
    printf("compute raw %zu\n", Wrong(r, wr));

    ID3D11Buffer *ab = dev.Buffer(N * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11UnorderedAccessView *append = BufferUAV(d, ab, DXGI_FORMAT_UNKNOWN, N, D3D11_BUFFER_UAV_FLAG_APPEND);
    UINT ten = 10;
    ID3D11UnorderedAccessView *none[3] = {};
    ctx->CSSetUnorderedAccessViews(2, 1, &append, &ten);
    ctx->Dispatch(1, 1, 1);
    ctx->CSSetUnorderedAccessViews(0, 3, none, nullptr);
    printf("compute append %u\n", Count(dev, append));

    // A 16x16 target with a pixel counter (u1) and an append buffer (u2, from 7) beside it.
    ID3D11Texture2D *t = dev.Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(t);
    ID3D11Buffer *c1 = dev.Buffer(16, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11Buffer *c2 = dev.Buffer(16, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11Buffer *ap = dev.Buffer(N * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11UnorderedAccessView *counter1 = BufferUAV(d, c1, DXGI_FORMAT_UNKNOWN, 4, 0),
                              *counter2 = BufferUAV(d, c2, DXGI_FORMAT_UNKNOWN, 4, 0),
                              *appended = BufferUAV(d, ap, DXGI_FORMAT_UNKNOWN, N, D3D11_BUFFER_UAV_FLAG_APPEND);
    const UINT zero[4] = {};
    ctx->ClearUnorderedAccessViewUint(counter1, zero);
    ctx->ClearUnorderedAccessViewUint(counter2, zero);
    ID3D11UnorderedAccessView *ps_uavs[2] = {counter1, appended};
    UINT counts[2] = {0, 7};
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(dev.VS("quad.vs"), nullptr, 0);
    ctx->PSSetShader(dev.PS("omuav.ps"), nullptr, 0);
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(1, &rtv, nullptr, 1, 2, ps_uavs, counts);
    ctx->Draw(3, 0);
    ID3D11UnorderedAccessView *second[2] = {counter2, appended};
    UINT keep[2] = {(UINT)-1, (UINT)-1};
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 1, 2,
                                                   second, keep);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 1, 2, none, nullptr);
    printf("compute omuav %u %u %u\n", As<UINT>(dev.Read(c1, 4))[0], Count(dev, appended), As<UINT>(dev.Read(c2, 4))[0]);

    const UINT fives[4] = {5, 6, 7, 8};
    ID3D11Buffer *cb = dev.Buffer(N * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                  D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11UnorderedAccessView *cv = BufferUAV(d, cb, DXGI_FORMAT_UNKNOWN, N, 0);
    ctx->ClearUnorderedAccessViewUint(cv, fives);
    std::vector<UINT> got = As<UINT>(dev.Read(cb, N * 4)), want(N, 5);
    printf("compute clearuint %zu\n", Wrong(got, want));

    ID3D11Texture2D *ft = dev.Tex2D(16, 16, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    ID3D11UnorderedAccessView *fv;
    CHECK(d->CreateUnorderedAccessView(ft, nullptr, &fv));
    const float quarter[4] = {0.25f, 0, 0, 0};
    ctx->ClearUnorderedAccessViewFloat(fv, quarter);
    std::vector<float> f = As<float>(dev.Read(ft, 0, 16 * 4, 16)), fw(256, 0.25f);
    printf("compute clearfloat %zu\n", Wrong(f, fw));

    ID3D11DeviceContext1 *ctx1 = nullptr;
    if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&ctx1))) {
        printf("compute clearview skip no ID3D11DeviceContext1\n");
        return 0;
    }
    const float black[4] = {0, 0, 0, 0}, green[4] = {0, 1, 0, 1};
    ctx->ClearRenderTargetView(rtv, black);
    D3D11_RECT rect = {4, 4, 12, 8};
    ctx1->ClearView(rtv, green, &rect, 1);
    std::vector<uint32_t> px = As<uint32_t>(dev.Read(t, 0, 16 * 4, 16)), pw(256, 0);
    for (UINT y = 4; y < 8; y++)
        for (UINT x = 4; x < 12; x++) pw[y * 16 + x] = 0xff00ff00u;
    printf("compute clearview %zu\n", Wrong(px, pw));
    return 0;
}
