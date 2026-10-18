// D3D11's state getters (Direct3D forwarding M4): each hands back the objects set, as arrays of interface pointers
// out (the forwarding marshals each as an object, M4 Task 3), and the values beside them.
//   d3d11_getters.exe <shaders>   prints "getters <what> <same...>": 1 for each object that comes back as the one
//   set (pointer equality), then the values set (strides, offsets, blend factor and mask)
#include "d3d11_common.hpp"

template <class T> static int Same(T *got, T *set) {
    int same = got == set;
    if (got) got->Release();
    return same;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_getters.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;

    ID3D11Texture2D *ta = dev.Tex2D(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    ID3D11Texture2D *tb = dev.Tex2D(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    ID3D11ShaderResourceView *srv[2] = {dev.SRV(ta), dev.SRV(tb)}, *srv_out[2] = {};
    ctx->PSSetShaderResources(0, 2, srv);
    ctx->PSGetShaderResources(0, 2, srv_out);
    printf("getters srv %d %d\n", Same(srv_out[0], srv[0]), Same(srv_out[1], srv[1]));

    ID3D11Buffer *cbs[2] = {dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER), dev.Buffer(32, D3D11_BIND_CONSTANT_BUFFER)};
    ID3D11Buffer *cb_out[2] = {};
    ctx->VSSetConstantBuffers(1, 2, cbs);
    ctx->VSGetConstantBuffers(1, 2, cb_out);
    printf("getters cb %d %d\n", Same(cb_out[0], cbs[0]), Same(cb_out[1], cbs[1]));

    D3D11_SAMPLER_DESC sd = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                             D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
    ID3D11SamplerState *sampler, *sampler_out = nullptr;
    CHECK(d->CreateSamplerState(&sd, &sampler));
    ctx->PSSetSamplers(3, 1, &sampler);
    ctx->PSGetSamplers(3, 1, &sampler_out);
    printf("getters sampler %d\n", Same(sampler_out, sampler));

    ID3D11Buffer *vbs[2] = {dev.Buffer(64, D3D11_BIND_VERTEX_BUFFER), dev.Buffer(64, D3D11_BIND_VERTEX_BUFFER)};
    ID3D11Buffer *vb_out[2] = {};
    UINT strides[2] = {12, 20}, offsets[2] = {4, 8}, s_out[2] = {}, o_out[2] = {};
    ctx->IASetVertexBuffers(2, 2, vbs, strides, offsets);
    ctx->IAGetVertexBuffers(2, 2, vb_out, s_out, o_out);
    printf("getters vb %d %d %u %u %u %u\n", Same(vb_out[0], vbs[0]), Same(vb_out[1], vbs[1]), s_out[0], s_out[1],
           o_out[0], o_out[1]);

    ID3D11RenderTargetView *rtv[2] = {dev.RTV(ta), dev.RTV(tb)}, *rtv_out[2] = {};
    ID3D11Texture2D *ds = dev.Tex2D(8, 8, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL);
    ID3D11DepthStencilView *dsv, *dsv_out = nullptr;
    CHECK(d->CreateDepthStencilView(ds, nullptr, &dsv));
    ID3D11ShaderResourceView *unbind[2] = {};
    ctx->PSSetShaderResources(0, 2, unbind);  // the textures unbound as resources, so they can be targets
    ctx->OMSetRenderTargets(2, rtv, dsv);
    ctx->OMGetRenderTargets(2, rtv_out, &dsv_out);
    printf("getters rtv %d %d %d\n", Same(rtv_out[0], rtv[0]), Same(rtv_out[1], rtv[1]), Same(dsv_out, dsv));

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE,
                          D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    ID3D11BlendState *blend, *blend_out = nullptr;
    CHECK(d->CreateBlendState(&bd, &blend));
    const float factor[4] = {0.25f, 0.5f, 0.75f, 1};
    float f_out[4] = {};
    UINT mask_out = 0;
    ctx->OMSetBlendState(blend, factor, 0x00ff00ffu);
    ctx->OMGetBlendState(&blend_out, f_out, &mask_out);
    printf("getters blend %d %g %g %g %g %08x\n", Same(blend_out, blend), f_out[0], f_out[1], f_out[2], f_out[3], mask_out);

    ID3D11Buffer *so = dev.Buffer(256, D3D11_BIND_STREAM_OUTPUT), *so_out[2] = {};
    UINT so_offset = 0;
    ctx->SOSetTargets(1, &so, &so_offset);
    ctx->SOGetTargets(2, so_out);
    printf("getters so %d %d\n", Same(so_out[0], so), so_out[1] == nullptr);
    return 0;
}
