// D3D11 indexed, instanced, strip-cut, indirect and predicated draws, context-state swaps, resource MinLOD and GS
// (DXMTRS step 5b):
//   d3d11_draw.exe <shaders> [perf]   prints "draw <case> <texels or sums>" from texel readbacks (and "perfinfo <row>
//                                     <ns per call>" when `perf` is passed).
#include "d3d11_common.hpp"
#include <chrono>

struct DrawVert {
    float x, y;
    float r, g, b, a;
};

struct InstOff {
    float ox, oy;
};

static uint64_t qpc_ns() {
#ifndef DXMT_NATIVE
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
#endif
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "perf") != 0)) {
        printf("usage: d3d11_draw.exe <shaders> [perf]\n");
        return 2;
    }
    g_shaders = argv[1];
    bool want_perf = (argc == 3 && !strcmp(argv[2], "perf"));

    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;

    std::vector<char> draw_vs_bc;
    ID3D11VertexShader *draw_vs = dev.VS("draw.vs", &draw_vs_bc);
    ID3D11PixelShader *draw_ps = dev.PS("draw.ps");
    ID3D11VertexShader *quad_vs = dev.VS("quad.vs");
    ID3D11VertexShader *tri_vs = dev.VS("tri.vs");
    ID3D11PixelShader *solid_ps = dev.PS("solid.ps");
    ID3D11PixelShader *minlod_ps = dev.PS("minlod.ps");
    ID3D11ComputeShader *compute_cs = dev.CS("compute.cs");
    std::vector<char> pass_gs_bc = Shader("pass.gs");

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    ID3D11InputLayout *il = nullptr;
    CHECK(d->CreateInputLayout(elems, 3, draw_vs_bc.data(), draw_vs_bc.size(), &il));

    constexpr UINT W = 8, H = 4;
    ID3D11Texture2D *rt = dev.Tex2D(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(rt);

    D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
    const float zero_rgba[4] = {0, 0, 0, 0};

    // 1. R16 and R32 DrawIndexed at a non-zero index-buffer offset, with StartIndexLocation and negative BaseVertexLocation.
    DrawVert verts1[8] = {
        {-9, -9, 0, 0, 0, 0},
        {-9, -9, 0, 0, 0, 0},
        {-1, -1, 0.25f, 0.5f, 0.75f, 1.0f},
        {-1, +3, 0.25f, 0.5f, 0.75f, 1.0f},
        {+3, -1, 0.25f, 0.5f, 0.75f, 1.0f},
        {-9, -9, 0, 0, 0, 0},
        {-9, -9, 0, 0, 0, 0},
        {-9, -9, 0, 0, 0, 0},
    };
    InstOff zero_inst[1] = {{0.0f, 0.0f}};
    ID3D11Buffer *vb1 = dev.Buffer(sizeof(verts1), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, verts1);
    ID3D11Buffer *ib_inst0 = dev.Buffer(sizeof(zero_inst), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, zero_inst);

    uint16_t idx16[8] = {0, 0, 0, 0, 5, 6, 7, 0};
    uint32_t idx32[8] = {0, 0, 0, 0, 5, 6, 7, 0};
    ID3D11Buffer *ib16 = dev.Buffer(sizeof(idx16), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, 0, idx16);
    ID3D11Buffer *ib32 = dev.Buffer(sizeof(idx32), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, 0, idx32);

    ID3D11Buffer *vbs[2] = {vb1, ib_inst0};
    UINT strides[2] = {sizeof(DrawVert), sizeof(InstOff)};
    UINT offsets[2] = {0, 0};

    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(il);
    ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(draw_vs, nullptr, 0);
    ctx->PSSetShader(draw_ps, nullptr, 0);

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 4);
    ctx->DrawIndexed(3, 2, -3);
    std::vector<uint8_t> px = dev.Read(rt, 0, W * 4, H);
    printf("draw indexed_r16 %u %u %u %u\n", px[0], px[1], px[2], px[3]);

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetIndexBuffer(ib32, DXGI_FORMAT_R32_UINT, 8);
    ctx->DrawIndexed(3, 2, -3);
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw indexed_r32 %u %u %u %u\n", px[0], px[1], px[2], px[3]);

    // 2. DrawIndexedInstanced with StartInstanceLocation.
    DrawVert quad_v[4] = {
        {-0.5f, -1.0f, 0.5f, 0.75f, 0.25f, 1.0f},
        {-0.5f, +1.0f, 0.5f, 0.75f, 0.25f, 1.0f},
        {+0.5f, -1.0f, 0.5f, 0.75f, 0.25f, 1.0f},
        {+0.5f, +1.0f, 0.5f, 0.75f, 0.25f, 1.0f},
    };
    InstOff insts[5] = {{-9.0f, -9.0f}, {-9.0f, -9.0f}, {-9.0f, -9.0f}, {-0.5f, 0.0f}, {+0.5f, 0.0f}};
    uint16_t quad_idx[6] = {0, 1, 2, 2, 1, 3};
    ID3D11Buffer *vb_quad = dev.Buffer(sizeof(quad_v), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, quad_v);
    ID3D11Buffer *vb_inst = dev.Buffer(sizeof(insts), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, insts);
    ID3D11Buffer *ib_quad = dev.Buffer(sizeof(quad_idx), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, 0, quad_idx);

    ID3D11Buffer *vbs_inst[2] = {vb_quad, vb_inst};
    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetVertexBuffers(0, 2, vbs_inst, strides, offsets);
    ctx->IASetIndexBuffer(ib_quad, DXGI_FORMAT_R16_UINT, 0);
    ctx->DrawIndexedInstanced(6, 2, 0, 0, 3);
    px = dev.Read(rt, 0, W * 4, H);
    const uint8_t *left_p = &px[(2 * W + 1) * 4];
    const uint8_t *right_p = &px[(2 * W + 6) * 4];
    printf("draw instanced %u %u %u %u\n", left_p[0], left_p[1], right_p[0], right_p[1]);

    // 3. Strip cut indices (0xFFFF for R16, 0xFFFFFFFF for R32) in triangle strips.
    DrawVert strip_v[6] = {
        {-0.9f, -0.9f, 1, 0, 0, 1},
        {-0.9f, +0.9f, 1, 0, 0, 1},
        {-0.25f, 0.0f, 1, 0, 0, 1},
        {+0.25f, 0.0f, 0, 1, 0, 1},
        {+0.9f, +0.9f, 0, 1, 0, 1},
        {+0.9f, -0.9f, 0, 1, 0, 1},
    };
    uint16_t strip_i16[7] = {0, 1, 2, 0xFFFFu, 3, 4, 5};
    uint32_t strip_i32[7] = {0, 1, 2, 0xFFFFFFFFu, 3, 4, 5};
    ID3D11Buffer *vb_strip = dev.Buffer(sizeof(strip_v), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, strip_v);
    ID3D11Buffer *ib_strip16 = dev.Buffer(sizeof(strip_i16), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, 0, strip_i16);
    ID3D11Buffer *ib_strip32 = dev.Buffer(sizeof(strip_i32), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, 0, strip_i32);

    ID3D11Buffer *vbs_strip[2] = {vb_strip, ib_inst0};
    ctx->IASetVertexBuffers(0, 2, vbs_strip, strides, offsets);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetIndexBuffer(ib_strip16, DXGI_FORMAT_R16_UINT, 0);
    ctx->DrawIndexed(7, 0, 0);
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw strip_cut_r16 %u %u %u\n", px[(2 * W + 1) * 4 + 0], px[(2 * W + 4) * 4 + 0] + px[(2 * W + 4) * 4 + 1],
           px[(2 * W + 6) * 4 + 1]);

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetIndexBuffer(ib_strip32, DXGI_FORMAT_R32_UINT, 0);
    ctx->DrawIndexed(7, 0, 0);
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw strip_cut_r32 %u %u %u\n", px[(2 * W + 1) * 4 + 0], px[(2 * W + 4) * 4 + 0] + px[(2 * W + 4) * 4 + 1],
           px[(2 * W + 6) * 4 + 1]);

    // 4. DrawInstancedIndirect, DrawIndexedInstancedIndirect, and DispatchIndirect at a non-zero args offset.
    uint32_t indirect_words[16] = {
        0, 0, 0, 0,
        3, 1, 2, 0,
        3, 1, 4, (uint32_t)-3, 0,
        1, 1, 1,
    };
    ID3D11Buffer *args_buf = dev.Buffer(sizeof(indirect_words), 0, D3D11_USAGE_DEFAULT, 0, indirect_words,
                                        D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->DrawInstancedIndirect(args_buf, 16);
    px = dev.Read(rt, 0, W * 4, H);
    uint32_t ind_draw_r = px[0];

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 0);
    ctx->DrawIndexedInstancedIndirect(args_buf, 32);
    px = dev.Read(rt, 0, W * 4, H);
    uint32_t ind_idx_g = px[1];

    ID3D11Buffer *sbuf = dev.Buffer(64 * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                    D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
    ID3D11Buffer *rbuf = dev.Buffer(64 * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                    D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0);
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd_s = {};
    uavd_s.Format = DXGI_FORMAT_UNKNOWN;
    uavd_s.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavd_s.Buffer.NumElements = 64;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavd_r = {};
    uavd_r.Format = DXGI_FORMAT_R32_TYPELESS;
    uavd_r.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavd_r.Buffer.NumElements = 64;
    uavd_r.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ID3D11UnorderedAccessView *uavs[2] = {nullptr, nullptr};
    CHECK(d->CreateUnorderedAccessView(sbuf, &uavd_s, &uavs[0]));
    CHECK(d->CreateUnorderedAccessView(rbuf, &uavd_r, &uavs[1]));
    ctx->CSSetShader(compute_cs, nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    ctx->DispatchIndirect(args_buf, 52);
    ID3D11UnorderedAccessView *null_uavs[2] = {nullptr, nullptr};
    ctx->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
    std::vector<uint32_t> cs_words = As<uint32_t>(dev.Read(sbuf, 64 * 4));
    uint32_t ind_cs_sum = 0;
    for (uint32_t v : cs_words) ind_cs_sum += v;
    printf("draw indirect %u %u %u\n", ind_draw_r, ind_idx_g, ind_cs_sum);

    // 5. SetPredication both ways (an occlusion predicate that passes and one that fails).
    ID3D11Texture2D *ds_tex = dev.Tex2D(W, H, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL);
    ID3D11DepthStencilView *dsv = nullptr;
    CHECK(d->CreateDepthStencilView(ds_tex, nullptr, &dsv));
    D3D11_DEPTH_STENCIL_DESC dss_desc = {};
    dss_desc.DepthEnable = TRUE;
    dss_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dss_desc.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11DepthStencilState *dss_less = nullptr, *dss_always = nullptr;
    CHECK(d->CreateDepthStencilState(&dss_desc, &dss_less));
    dss_desc.DepthEnable = FALSE;
    CHECK(d->CreateDepthStencilState(&dss_desc, &dss_always));

    D3D11_QUERY_DESC pdesc = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    ID3D11Predicate *pred_pass = nullptr, *pred_fail = nullptr;
    CHECK(d->CreatePredicate(&pdesc, &pred_pass));
    CHECK(d->CreatePredicate(&pdesc, &pred_fail));

    float c_solid[4] = {0.75f, 0.25f, 0.5f, 1.0f};
    ID3D11Buffer *cb_solid = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, c_solid);

    ctx->OMSetRenderTargets(1, &rtv, dsv);
    ctx->VSSetShader(quad_vs, nullptr, 0);
    ctx->PSSetShader(solid_ps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb_solid);

    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetDepthStencilState(dss_less, 0);
    ctx->Begin(pred_pass);
    ctx->Draw(3, 0);
    ctx->End(pred_pass);

    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
    ctx->Begin(pred_fail);
    ctx->Draw(3, 0);
    ctx->End(pred_fail);

    ctx->OMSetDepthStencilState(dss_always, 0);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->SetPredication(pred_pass, FALSE);
    ID3D11Predicate *got_pred = nullptr;
    BOOL got_pval = TRUE;
    ctx->GetPredication(&got_pred, &got_pval);
    int pred_get_ok = (got_pred == pred_pass && got_pval == FALSE) ? 1 : 0;
    if (got_pred) got_pred->Release();
    ctx->Draw(3, 0);
    ctx->SetPredication(nullptr, FALSE);
    px = dev.Read(rt, 0, W * 4, H);
    uint32_t pred_pass_r = px[0];

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->SetPredication(pred_fail, FALSE);
    ctx->Draw(3, 0);
    ctx->SetPredication(nullptr, FALSE);
    px = dev.Read(rt, 0, W * 4, H);
    uint32_t pred_fail_r = px[0];

    ctx->Flush();
    BOOL q_pass_val = FALSE, q_fail_val = TRUE;
    while (ctx->GetData(pred_pass, &q_pass_val, sizeof(BOOL), 0) == S_FALSE) Sleep(1);
    while (ctx->GetData(pred_fail, &q_fail_val, sizeof(BOOL), 0) == S_FALSE) Sleep(1);
    printf("draw predication %d %d %d %u %u\n", q_pass_val ? 1 : 0, q_fail_val ? 1 : 0, pred_get_ok, pred_pass_r,
           pred_fail_r);

    // 6. SwapDeviceContextState round trip (state set, swapped out, swapped back, drawn).
    ID3D11Device1 *dev1 = nullptr;
    ID3D11DeviceContext1 *ctx1 = nullptr;
    CHECK(d->QueryInterface(__uuidof(ID3D11Device1), (void **)&dev1));
    CHECK(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&ctx1));
    D3D_FEATURE_LEVEL fl = d->GetFeatureLevel(), chosen_fl = D3D_FEATURE_LEVEL_11_0;
    ID3DDeviceContextState *fresh_state = nullptr, *prev_state = nullptr;
    CHECK(dev1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device1), &chosen_fl, &fresh_state));

    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(quad_vs, nullptr, 0);
    ctx->PSSetShader(solid_ps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb_solid);

    ctx1->SwapDeviceContextState(fresh_state, &prev_state);
    ID3D11RenderTargetView *mid_rtv = nullptr;
    ctx->OMGetRenderTargets(1, &mid_rtv, nullptr);
    int mid_has_rtv = (mid_rtv != nullptr) ? 1 : 0;
    if (mid_rtv) mid_rtv->Release();

    ctx1->SwapDeviceContextState(prev_state, nullptr);
    ctx->Draw(3, 0);
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw swap_state %d %u %u %u\n", mid_has_rtv, px[0], px[1], px[2]);
    prev_state->Release();
    fresh_state->Release();

    // 7. SetResourceMinLOD on a sampled mipped texture.
    uint32_t mip0[16], mip1[4];
    for (int i = 0; i < 16; i++) mip0[i] = 0xff1428c8u; // (200, 40, 20, 255)
    for (int i = 0; i < 4; i++) mip1[i] = 0xfff0b414u;  // (20, 180, 240, 255)
    D3D11_SUBRESOURCE_DATA mip_sd[2] = {{mip0, 16, 0}, {mip1, 8, 0}};
    ID3D11Texture2D *mtex = dev.Tex2D(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                      D3D11_USAGE_DEFAULT, 0, 2, 1, mip_sd);
    ID3D11ShaderResourceView *msrv = dev.SRV(mtex);
    D3D11_SAMPLER_DESC samp_d = {};
    samp_d.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samp_d.AddressU = samp_d.AddressV = samp_d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_d.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState *samp = nullptr;
    CHECK(d->CreateSamplerState(&samp_d, &samp));

    ctx->SetResourceMinLOD(mtex, 1.0f);
    int got_lod10 = (int)(ctx->GetResourceMinLOD(mtex) * 10.0f + 0.5f);
    ctx->ClearRenderTargetView(rtv, zero_rgba);
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp2 = {0, 0, 2, 2, 0, 1};
    ctx->RSSetViewports(1, &vp2);
    ctx->VSSetShader(quad_vs, nullptr, 0);
    ctx->PSSetShader(minlod_ps, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &msrv);
    ctx->PSSetSamplers(0, 1, &samp);
    ctx->Draw(3, 0);
    ctx->RSSetViewports(1, &vp);
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw minlod %d %u %u %u\n", got_lod10, px[0], px[1], px[2]);

    // 8. Pass-through geometry shader if the C++ accepts it (else its refusal's HRESULT is the golden).
    ID3D11GeometryShader *gs = nullptr;
    HRESULT gs_hr = d->CreateGeometryShader(pass_gs_bc.data(), pass_gs_bc.size(), nullptr, &gs);
    ctx->ClearRenderTargetView(rtv, zero_rgba);
    if (SUCCEEDED(gs_hr) && gs) {
        ctx->VSSetShader(tri_vs, nullptr, 0);
        ctx->GSSetShader(gs, nullptr, 0);
        ctx->PSSetShader(solid_ps, nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, &cb_solid);
        ctx->Draw(3, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
    }
    px = dev.Read(rt, 0, W * 4, H);
    printf("draw gs 0x%08lx %u %u %u\n", (unsigned long)(uint32_t)gs_hr, px[0], px[1], px[2]);
    if (gs) gs->Release();

    if (want_perf) {
        constexpr int N = 100000;
        constexpr int N_DEF = 10000;

        ID3D11Buffer *dyn_cb = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
        ID3D11Buffer *dyn_vb = dev.Buffer(64 * sizeof(DrawVert), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC,
                                          D3D11_CPU_ACCESS_WRITE);

        D3D11_QUERY_DESC eqd = {D3D11_QUERY_EVENT, 0};
        ID3D11Query *ev_q = nullptr;
        CHECK(d->CreateQuery(&eqd, &ev_q));

        auto wait_gpu = [&]() {
            ctx->End(ev_q);
            ctx->Flush();
            BOOL done = FALSE;
            while (ctx->GetData(ev_q, &done, sizeof(BOOL), 0) == S_FALSE) {
            }
        };

        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(il);
        ctx->IASetVertexBuffers(0, 2, vbs, strides, offsets);
        ctx->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 4);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(draw_vs, nullptr, 0);
        ctx->PSSetShader(draw_ps, nullptr, 0);

        // Warm-up pass
        for (int i = 0; i < 1000; i++) ctx->DrawIndexed(3, 2, -3);
        wait_gpu();

        // 1) DrawIndexed with no state change
        uint64_t t0 = qpc_ns();
        for (int i = 0; i < N; i++) ctx->DrawIndexed(3, 2, -3);
        uint64_t t1 = qpc_ns();
        printf("perfinfo draw_indexed %.1f\n", (double)(t1 - t0) / (double)N);

        // Drain Flush after 100k recorded draws until event query answers
        uint64_t td0 = qpc_ns();
        wait_gpu();
        uint64_t td1 = qpc_ns();
        printf("perfinfo drain_flush %.1f\n", (double)(td1 - td0) / (double)N);

        // 2) VSSetConstantBuffers + DrawIndexed
        ID3D11Buffer *cbs_alt[2] = {cb_solid, dyn_cb};
        t0 = qpc_ns();
        for (int i = 0; i < N; i++) {
            ctx->VSSetConstantBuffers(0, 1, &cbs_alt[i & 1]);
            ctx->DrawIndexed(3, 2, -3);
        }
        t1 = qpc_ns();
        printf("perfinfo bind_draw %.1f\n", (double)(t1 - t0) / (double)N);
        wait_gpu();

        // 3) Map(WRITE_DISCARD) + Unmap on a dynamic CB
        t0 = qpc_ns();
        for (int i = 0; i < N; i++) {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (SUCCEEDED(ctx->Map(dyn_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                ((uint32_t *)m.pData)[0] = (uint32_t)i;
                ctx->Unmap(dyn_cb, 0);
            }
        }
        t1 = qpc_ns();
        printf("perfinfo map_discard_cb %.1f\n", (double)(t1 - t0) / (double)N);
        wait_gpu();

        // 4) Map(WRITE_DISCARD) + Unmap + Draw
        ctx->PSSetConstantBuffers(0, 1, &dyn_cb);
        t0 = qpc_ns();
        for (int i = 0; i < N; i++) {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (SUCCEEDED(ctx->Map(dyn_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                ((float *)m.pData)[0] = 0.5f;
                ctx->Unmap(dyn_cb, 0);
            }
            ctx->DrawIndexed(3, 2, -3);
        }
        t1 = qpc_ns();
        printf("perfinfo map_discard_draw %.1f\n", (double)(t1 - t0) / (double)N);
        wait_gpu();

        // 5) Map(WRITE_NO_OVERWRITE) + Unmap on a dynamic VB
        {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (SUCCEEDED(ctx->Map(dyn_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) ctx->Unmap(dyn_vb, 0);
        }
        t0 = qpc_ns();
        for (int i = 0; i < N; i++) {
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (SUCCEEDED(ctx->Map(dyn_vb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m))) {
                ((uint32_t *)m.pData)[0] = (uint32_t)i;
                ctx->Unmap(dyn_vb, 0);
            }
        }
        t1 = qpc_ns();
        printf("perfinfo map_nooverwrite_vb %.1f\n", (double)(t1 - t0) / (double)N);

        // 6) UpdateSubresource on a dynamic CB
        uint32_t upd_words[4] = {1, 2, 3, 4};
        t0 = qpc_ns();
        for (int i = 0; i < N; i++) {
            upd_words[0] = (uint32_t)i;
            ctx->UpdateSubresource(dyn_cb, 0, nullptr, upd_words, 0, 0);
        }
        t1 = qpc_ns();
        printf("perfinfo update_dynamic_cb %.1f\n", (double)(t1 - t0) / (double)N);
        wait_gpu();

        // 7) FinishCommandList + ExecuteCommandList of a 10k-draw deferred list (10 iterations = 100k draws)
        ID3D11DeviceContext *def = nullptr;
        CHECK(d->CreateDeferredContext(0, &def));
        t0 = qpc_ns();
        for (int rep = 0; rep < 10; rep++) {
            def->OMSetRenderTargets(1, &rtv, nullptr);
            def->RSSetViewports(1, &vp);
            def->IASetInputLayout(il);
            def->IASetVertexBuffers(0, 2, vbs, strides, offsets);
            def->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 4);
            def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            def->VSSetShader(draw_vs, nullptr, 0);
            def->PSSetShader(draw_ps, nullptr, 0);
            for (int i = 0; i < N_DEF; i++) def->DrawIndexed(3, 2, -3);
            ID3D11CommandList *cl = nullptr;
            CHECK(def->FinishCommandList(FALSE, &cl));
            ctx->ExecuteCommandList(cl, FALSE);
            cl->Release();
        }
        t1 = qpc_ns();
        printf("perfinfo deferred_list %.1f\n", (double)(t1 - t0) / (double)(10 * N_DEF));
        wait_gpu();

        def->Release();
        ev_q->Release();
        dyn_vb->Release();
        dyn_cb->Release();
    }

    samp->Release();
    msrv->Release();
    mtex->Release();
    ctx1->Release();
    dev1->Release();
    cb_solid->Release();
    pred_fail->Release();
    pred_pass->Release();
    dss_always->Release();
    dss_less->Release();
    dsv->Release();
    ds_tex->Release();
    uavs[1]->Release();
    uavs[0]->Release();
    rbuf->Release();
    sbuf->Release();
    args_buf->Release();
    ib_strip32->Release();
    ib_strip16->Release();
    vb_strip->Release();
    ib_quad->Release();
    vb_inst->Release();
    vb_quad->Release();
    ib32->Release();
    ib16->Release();
    ib_inst0->Release();
    vb1->Release();
    rtv->Release();
    rt->Release();
    il->Release();
    compute_cs->Release();
    minlod_ps->Release();
    solid_ps->Release();
    tri_vs->Release();
    quad_vs->Release();
    draw_ps->Release();
    draw_vs->Release();
    return 0;
}
