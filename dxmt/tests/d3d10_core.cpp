// D3D10 on d3d10core (Direct3D forwarding M4): its device made as d3d10.dll makes it (D3D10CoreCreateDevice on DXGI's
// first adapter), so it runs natively too, where there is no d3d10.dll.
//   d3d10_core.exe <shaders>   prints "d3d10 <check> <value>":
//     init    a buffer's and a 16x16 texture's initial data (the texture's pitch wider than its rows), read back
//             through staging maps: bytes wrong
//     map     a dynamic buffer mapped WRITE_DISCARD, copied and read back: bytes wrong
//     query   an occlusion query over a 16x16 draw: samples passed
//     draw    a draw of one colour into a 16x16 target: the target's byte sum
#include "d3d11_common.hpp"
#include <d3d10_1.h>
#include <dxgi.h>

typedef HRESULT(WINAPI *create_fn)(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL, ID3D10Device **);
#ifndef _WIN32
extern "C" HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL, ID3D10Device **);
#endif

static std::vector<uint8_t> Pattern(size_t n, unsigned seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 5 + seed * 17);
    return v;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d10_core.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
#ifdef _WIN32
    create_fn create = (create_fn)(void *)GetProcAddress(LoadLibraryA("d3d10core.dll"), "D3D10CoreCreateDevice");
    if (!create) { printf("d3d10 fail no D3D10CoreCreateDevice\n"); return 1; }
#else
    create_fn create = D3D10CoreCreateDevice;
#endif
    IDXGIFactory *factory;
    IDXGIAdapter *adapter;
    CHECK(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&factory));
    CHECK(factory->EnumAdapters(0, &adapter));
    ID3D10Device *d;
    CHECK(create(factory, adapter, 0, D3D_FEATURE_LEVEL_10_0, &d));

    std::vector<uint8_t> bytes = Pattern(256, 1);
    D3D10_BUFFER_DESC bd = {256, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0};
    D3D10_SUBRESOURCE_DATA bi = {bytes.data(), 0, 0};
    ID3D10Buffer *b, *staging;
    CHECK(d->CreateBuffer(&bd, &bi, &b));
    D3D10_BUFFER_DESC sd = {256, D3D10_USAGE_STAGING, 0, D3D10_CPU_ACCESS_READ, 0};
    CHECK(d->CreateBuffer(&sd, nullptr, &staging));
    d->CopyResource(staging, b);
    void *p;
    CHECK(staging->Map(D3D10_MAP_READ, 0, &p));
    std::vector<uint8_t> got((uint8_t *)p, (uint8_t *)p + 256);
    staging->Unmap();
    const UINT pitch = 16 * 4 + 8;
    std::vector<uint8_t> tex = Pattern(pitch * 16, 2), twant(16 * 16 * 4);
    for (UINT y = 0; y < 16; y++) memcpy(&twant[y * 64], &tex[y * pitch], 64);
    D3D10_TEXTURE2D_DESC td = {16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D10_USAGE_DEFAULT,
                               D3D10_BIND_SHADER_RESOURCE, 0, 0};
    D3D10_SUBRESOURCE_DATA ti = {tex.data(), pitch, 0};
    ID3D10Texture2D *t, *st;
    CHECK(d->CreateTexture2D(&td, &ti, &t));
    D3D10_TEXTURE2D_DESC std_ = td;
    std_.Usage = D3D10_USAGE_STAGING, std_.BindFlags = 0, std_.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    CHECK(d->CreateTexture2D(&std_, nullptr, &st));
    d->CopyResource(st, t);
    D3D10_MAPPED_TEXTURE2D m;
    CHECK(st->Map(0, D3D10_MAP_READ, 0, &m));
    std::vector<uint8_t> tgot(16 * 16 * 4);
    for (UINT y = 0; y < 16; y++) memcpy(&tgot[y * 64], (uint8_t *)m.pData + y * m.RowPitch, 64);
    st->Unmap(0);
    printf("d3d10 init %zu %zu\n", Wrong(got, bytes), Wrong(tgot, twant));

    D3D10_BUFFER_DESC dd = {256, D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER, D3D10_CPU_ACCESS_WRITE, 0};
    ID3D10Buffer *dyn;
    CHECK(d->CreateBuffer(&dd, nullptr, &dyn));
    std::vector<uint8_t> dpat = Pattern(256, 3);
    CHECK(dyn->Map(D3D10_MAP_WRITE_DISCARD, 0, &p));
    memcpy(p, dpat.data(), 256);
    dyn->Unmap();
    d->CopyResource(staging, dyn);
    CHECK(staging->Map(D3D10_MAP_READ, 0, &p));
    std::vector<uint8_t> dgot((uint8_t *)p, (uint8_t *)p + 256);
    staging->Unmap();
    printf("d3d10 map %zu\n", Wrong(dgot, dpat));

    std::vector<char> vs_code = Shader("quad.vs"), ps_code = Shader("solid.ps");
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    CHECK(d->CreateVertexShader(vs_code.data(), vs_code.size(), &vs));
    CHECK(d->CreatePixelShader(ps_code.data(), ps_code.size(), &ps));
    const float color[4] = {0.5f, 0.25f, 1, 1};
    D3D10_BUFFER_DESC cd = {16, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, 0};
    D3D10_SUBRESOURCE_DATA ci = {color, 0, 0};
    ID3D10Buffer *cb;
    CHECK(d->CreateBuffer(&cd, &ci, &cb));
    D3D10_TEXTURE2D_DESC rd = td;
    rd.BindFlags = D3D10_BIND_RENDER_TARGET;
    ID3D10Texture2D *rt;
    CHECK(d->CreateTexture2D(&rd, nullptr, &rt));
    ID3D10RenderTargetView *rtv;
    CHECK(d->CreateRenderTargetView(rt, nullptr, &rtv));
    D3D10_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    d->OMSetRenderTargets(1, &rtv, nullptr);
    d->RSSetViewports(1, &vp);
    d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d->VSSetShader(vs);
    d->PSSetShader(ps);
    d->PSSetConstantBuffers(0, 1, &cb);
    D3D10_QUERY_DESC qd = {D3D10_QUERY_OCCLUSION, 0};
    ID3D10Query *q;
    CHECK(d->CreateQuery(&qd, &q));
    q->Begin();
    d->Draw(3, 0);
    q->End();
    UINT64 samples = 0;
    HRESULT hr;
    for (int i = 0; (hr = q->GetData(&samples, sizeof samples, 0)) == S_FALSE && i < 1000000; i++) Sleep(0);
    printf("d3d10 query %08lx %llu\n", (unsigned long)(unsigned)hr, (unsigned long long)samples);
    d->CopyResource(st, rt);
    CHECK(st->Map(0, D3D10_MAP_READ, 0, &m));
    unsigned long sum = 0;
    for (UINT y = 0; y < 16; y++)
        for (UINT x = 0; x < 64; x++) sum += ((uint8_t *)m.pData)[y * m.RowPitch + x];
    st->Unmap(0);
    printf("d3d10 draw %lu\n", sum);
    return 0;
}
