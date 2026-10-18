// D3D11 drawing (Direct3D forwarding M4): instanced quads into two targets with depth, stencil, an independent blend
// and a scissor; a 4x MSAA target resolved; GenerateMips.
//   d3d11_render.exe <shaders>   prints "render <check> <r> <g> <b> <a>", each a target's per-channel byte sums (or
//   one texel's bytes):
//     mrt0, mrt1   the instanced pass's two targets (target 1 blended over its clear, alpha 0.5)
//     stencil      target 0 after a pass drawing yellow where the instances wrote stencil 1
//     msaa         a triangle over a 4x target, resolved
//     mips4, mips8 GenerateMips over a 256x256 checker: mip 4's sums, mip 8's texel
#include "d3d11_common.hpp"

static void Sums(const char *name, const std::vector<uint8_t> &px) {
    unsigned long s[4] = {};
    for (size_t i = 0; i < px.size(); i++) s[i % 4] += px[i];
    printf("render %s %lu %lu %lu %lu\n", name, s[0], s[1], s[2], s[3]);
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_render.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;
    const UINT W = 64, H = 64;

    std::vector<char> vs_code;
    ID3D11VertexShader *vs = dev.VS("render.vs", &vs_code), *quad = dev.VS("quad.vs");
    ID3D11PixelShader *ps = dev.PS("render.ps"), *solid = dev.PS("solid.ps");
    D3D11_INPUT_ELEMENT_DESC le[] = {
        {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"DEPTH", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1}};
    ID3D11InputLayout *layout;
    CHECK(d->CreateInputLayout(le, 3, vs_code.data(), vs_code.size(), &layout));
    const float inst[4][7] = {{0.00f, 0.00f, 0.5f, 1, 0, 0, 1}, {0.25f, 0.25f, 0.3f, 0, 1, 0, 1},
                              {0.50f, 0.00f, 0.7f, 0, 0, 1, 1}, {0.25f, 0.50f, 0.1f, 1, 1, 1, 1}};
    ID3D11Buffer *ib = dev.Buffer(sizeof inst, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, 0, inst);

    ID3D11Texture2D *t0 = dev.Tex2D(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11Texture2D *t1 = dev.Tex2D(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11Texture2D *ds = dev.Tex2D(W, H, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
    ID3D11RenderTargetView *rtv[2] = {dev.RTV(t0), dev.RTV(t1)};
    ID3D11DepthStencilView *dsv;
    CHECK(d->CreateDepthStencilView(ds, nullptr, &dsv));
    const float clear0[4] = {0, 0, 0, 1}, clear1[4] = {0.2f, 0.4f, 0.6f, 1};
    ctx->ClearRenderTargetView(rtv[0], clear0);
    ctx->ClearRenderTargetView(rtv[1], clear1);
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE, dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL, dsd.DepthFunc = D3D11_COMPARISON_LESS;
    dsd.StencilEnable = TRUE, dsd.StencilReadMask = dsd.StencilWriteMask = 0xff;
    dsd.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
    dsd.BackFace = dsd.FrontFace;
    ID3D11DepthStencilState *write_stencil;
    CHECK(d->CreateDepthStencilState(&dsd, &write_stencil));
    D3D11_BLEND_DESC bd = {};
    bd.IndependentBlendEnable = TRUE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bd.RenderTarget[1] = {TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE,
                          D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    ID3D11BlendState *blend;
    CHECK(d->CreateBlendState(&bd, &blend));
    D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0, 0, TRUE, TRUE, FALSE, FALSE};
    ID3D11RasterizerState *scissor;
    CHECK(d->CreateRasterizerState(&rd, &scissor));
    D3D11_RECT rect = {0, 0, 48, 64};
    D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};

    UINT stride = sizeof inst[0], offset = 0;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &ib, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->OMSetRenderTargets(2, rtv, dsv);
    ctx->OMSetDepthStencilState(write_stencil, 1);
    ctx->OMSetBlendState(blend, nullptr, 0xffffffff);
    ctx->RSSetState(scissor);
    ctx->RSSetScissorRects(1, &rect);
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->DrawInstanced(4, 4, 0, 0);
    Sums("mrt0", dev.Read(t0, 0, W * 4, H));
    Sums("mrt1", dev.Read(t1, 0, W * 4, H));

    // Yellow where the instances wrote stencil 1, depth off, no blend, no scissor.
    D3D11_DEPTH_STENCIL_DESC eq = {};
    eq.StencilEnable = TRUE, eq.StencilReadMask = eq.StencilWriteMask = 0xff;
    eq.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_EQUAL};
    eq.BackFace = eq.FrontFace;
    ID3D11DepthStencilState *test_stencil;
    CHECK(d->CreateDepthStencilState(&eq, &test_stencil));
    const float yellow[4] = {1, 1, 0, 1};
    ID3D11Buffer *cb = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, yellow);
    ctx->OMSetRenderTargets(1, rtv, dsv);
    ctx->OMSetDepthStencilState(test_stencil, 1);
    ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    ctx->RSSetState(nullptr);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(quad, nullptr, 0);
    ctx->PSSetShader(solid, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    ctx->Draw(3, 0);
    Sums("stencil", dev.Read(t0, 0, W * 4, H));
    ctx->OMSetDepthStencilState(nullptr, 0);

    // A triangle over a 4x MSAA target, resolved.
    ID3D11Texture2D *ms = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0,
                                    1, 1, nullptr, 0, 4);
    ID3D11Texture2D *resolved = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ID3D11RenderTargetView *msv = dev.RTV(ms);
    ctx->ClearRenderTargetView(msv, clear0);
    const float white[4] = {1, 1, 1, 1};
    ctx->UpdateSubresource(cb, 0, nullptr, white, 0, 0);
    dev.Draw(dev.VS("tri.vs"), solid, msv, 32, 32);
    ctx->ResolveSubresource(resolved, 0, ms, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    Sums("msaa", dev.Read(resolved, 0, 32 * 4, 32));

    // GenerateMips over a 256x256 checker of 8-texel squares.
    ID3D11Texture2D *mips = dev.Tex2D(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM,
                                      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 0, 1,
                                      nullptr, D3D11_RESOURCE_MISC_GENERATE_MIPS);
    std::vector<uint32_t> checker(256 * 256);
    for (UINT y = 0; y < 256; y++)
        for (UINT x = 0; x < 256; x++) checker[y * 256 + x] = ((x / 8 + y / 8) & 1) ? 0xff2040c0u : 0xff000000u + y;
    ctx->UpdateSubresource(mips, 0, nullptr, checker.data(), 256 * 4, 0);
    ID3D11ShaderResourceView *mv = dev.SRV(mips);
    ctx->GenerateMips(mv);
    Sums("mips4", dev.Read(mips, 4, 16 * 4, 16));
    Sums("mips8", dev.Read(mips, 8, 4, 1));
    return 0;
}
