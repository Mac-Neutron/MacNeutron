// D3D10 through Wine's d3d10.dll (Direct3D forwarding M4: its effects framework and state blocks stay Wine's, in the
// guest, on top of d3d10core: spec §11), so a guest and macOS row only. A state block captures every state, the state
// changes, and Apply brings it back: Wine's state block reads it through the getters and sets it again.
//   d3d10_wine.exe <shaders>   prints "wine <check> <value>":
//     device       D3D10CreateDevice's HRESULT
//     stateblock   after Apply: the viewport as set, and 1 for each of the render target view, the pixel shader's
//                  constant buffer, the blend state and the vertex shader being the ones captured
//     draw         a draw after Apply into the captured target: its byte sum
#include "d3d11_common.hpp"
#include <d3d10.h>

template <class T> static int Same(T *got, T *set) {
    int same = got == set;
    if (got) got->Release();
    return same;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d10_wine.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    ID3D10Device *d = nullptr;
    HRESULT hr = D3D10CreateDevice(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0, D3D10_SDK_VERSION, &d);
    printf("wine device %08lx\n", (unsigned long)(unsigned)hr);
    if (!d) return 1;

    std::vector<char> vs_code = Shader("quad.vs"), ps_code = Shader("solid.ps");
    ID3D10VertexShader *vs;
    ID3D10PixelShader *ps;
    CHECK(d->CreateVertexShader(vs_code.data(), vs_code.size(), &vs));
    CHECK(d->CreatePixelShader(ps_code.data(), ps_code.size(), &ps));
    const float color[4] = {1, 0.5f, 0.25f, 1}, other[4] = {0, 0, 0, 0};
    D3D10_BUFFER_DESC cd = {16, D3D10_USAGE_DEFAULT, D3D10_BIND_CONSTANT_BUFFER, 0, 0};
    D3D10_SUBRESOURCE_DATA ci = {color, 0, 0}, oi = {other, 0, 0};
    ID3D10Buffer *cb, *cb2;
    CHECK(d->CreateBuffer(&cd, &ci, &cb));
    CHECK(d->CreateBuffer(&cd, &oi, &cb2));
    D3D10_TEXTURE2D_DESC td = {16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D10_USAGE_DEFAULT,
                               D3D10_BIND_RENDER_TARGET, 0, 0};
    ID3D10Texture2D *ta, *tb;
    CHECK(d->CreateTexture2D(&td, nullptr, &ta));
    CHECK(d->CreateTexture2D(&td, nullptr, &tb));
    ID3D10RenderTargetView *ra, *rb;
    CHECK(d->CreateRenderTargetView(ta, nullptr, &ra));
    CHECK(d->CreateRenderTargetView(tb, nullptr, &rb));
    D3D10_BLEND_DESC bd = {};
    bd.SrcBlend = bd.DestBlend = bd.SrcBlendAlpha = bd.DestBlendAlpha = D3D10_BLEND_ONE;
    bd.BlendOp = bd.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    bd.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    ID3D10BlendState *blend;
    CHECK(d->CreateBlendState(&bd, &blend));

    // State A, captured.
    D3D10_VIEWPORT va = {0, 0, 16, 16, 0, 1};
    const float factor[4] = {1, 1, 1, 1};
    d->OMSetRenderTargets(1, &ra, nullptr);
    d->RSSetViewports(1, &va);
    d->PSSetConstantBuffers(0, 1, &cb);
    d->OMSetBlendState(blend, factor, 0xffffffff);
    d->VSSetShader(vs);
    d->PSSetShader(ps);
    d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D10_STATE_BLOCK_MASK mask;
    CHECK(D3D10StateBlockMaskEnableAll(&mask));
    ID3D10StateBlock *sb;
    CHECK(D3D10CreateStateBlock(d, &mask, &sb));
    CHECK(sb->Capture());

    // State B, then A again.
    D3D10_VIEWPORT vb = {2, 3, 5, 7, 0, 1};
    d->OMSetRenderTargets(1, &rb, nullptr);
    d->RSSetViewports(1, &vb);
    d->PSSetConstantBuffers(0, 1, &cb2);
    d->OMSetBlendState(nullptr, factor, 0xffffffff);
    d->VSSetShader(nullptr);
    CHECK(sb->Apply());

    D3D10_VIEWPORT got[2] = {};
    UINT n = 2;
    d->RSGetViewports(&n, got);
    ID3D10RenderTargetView *rtv_out = nullptr;
    d->OMGetRenderTargets(1, &rtv_out, nullptr);
    ID3D10Buffer *cb_out = nullptr;
    d->PSGetConstantBuffers(0, 1, &cb_out);
    ID3D10BlendState *blend_out = nullptr;
    float f_out[4];
    UINT m_out;
    d->OMGetBlendState(&blend_out, f_out, &m_out);
    ID3D10VertexShader *vs_out = nullptr;
    d->VSGetShader(&vs_out);
    printf("wine stateblock %u %d %d %u %u %d %d %d %d\n", n, got[0].TopLeftX, got[0].TopLeftY, got[0].Width,
           got[0].Height, Same(rtv_out, ra), Same(cb_out, cb), Same(blend_out, blend), Same(vs_out, vs));

    d->Draw(3, 0);
    D3D10_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D10_USAGE_STAGING, sd.BindFlags = 0, sd.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    ID3D10Texture2D *st;
    CHECK(d->CreateTexture2D(&sd, nullptr, &st));
    d->CopyResource(st, ta);
    D3D10_MAPPED_TEXTURE2D m;
    CHECK(st->Map(0, D3D10_MAP_READ, 0, &m));
    unsigned long sum = 0;
    for (UINT y = 0; y < 16; y++)
        for (UINT x = 0; x < 64; x++) sum += ((uint8_t *)m.pData)[y * m.RowPitch + x];
    st->Unmap(0);
    printf("wine draw %lu\n", sum);
    return 0;
}
