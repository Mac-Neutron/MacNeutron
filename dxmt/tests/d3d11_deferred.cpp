// D3D11 deferred contexts and a shared immediate context (Direct3D forwarding M4).
//   d3d11_deferred.exe <shaders>   prints "deferred <check> <value>":
//     lists    two command lists recorded on a second thread (state, a WRITE_DISCARD map and a draw each texel),
//              executed on the immediate context, the first with RestoreContextState and the second without: texels
//              wrong, and the immediate context's viewport count after each (1 restored, 0 cleared)
//     shared   two threads taking 32 turns on the immediate context under ID3D11Multithread (Enter, a WRITE_DISCARD
//              map, a draw into the turn's texel, Leave): texels wrong
//     mixed    one dynamic constant buffer mapped WRITE_DISCARD on the immediate context, in a command list (executed
//              there), then on the immediate context again, 3 rounds, a draw after each: texels wrong (the list's
//              discard is the buffer's memory after it, and the next one the immediate context's own)
#include "d3d11_common.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

static D3D11_VIEWPORT Texel(UINT x) { return {(float)x, 0, 1, 1, 0, 1}; }

static void MapDraw(ID3D11DeviceContext *c, ID3D11Buffer *cb, UINT value, UINT texel) {
    D3D11_MAPPED_SUBRESOURCE m;
    CHECK(c->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
    ((UINT *)m.pData)[0] = value;
    c->Unmap(cb, 0);
    D3D11_VIEWPORT vp = Texel(texel);
    c->RSSetViewports(1, &vp);
    c->Draw(3, 0);
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_deferred.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;
    ID3D11VertexShader *quad = dev.VS("quad.vs");
    ID3D11PixelShader *cbps = dev.PS("cb.ps");

    ID3D11Texture2D *t = dev.Tex2D(4, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(t);
    ID3D11Buffer *cb = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ID3D11DeviceContext *def;
    CHECK(d->CreateDeferredContext(0, &def));
    ID3D11CommandList *lists[2] = {};
    std::thread recorder([&] {
        for (UINT n = 0; n < 2; n++) {
            def->OMSetRenderTargets(1, &rtv, nullptr);
            def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            def->VSSetShader(quad, nullptr, 0);
            def->PSSetShader(cbps, nullptr, 0);
            def->PSSetConstantBuffers(0, 1, &cb);
            MapDraw(def, cb, 7 + n * 2, n * 2);
            MapDraw(def, cb, 8 + n * 2, n * 2 + 1);
            CHECK(def->FinishCommandList(FALSE, &lists[n]));
        }
    });
    recorder.join();
    D3D11_VIEWPORT full = {0, 0, 4, 1, 0, 1}, back[4];
    ctx->RSSetViewports(1, &full);
    ctx->ExecuteCommandList(lists[0], TRUE);
    UINT restored = 4;
    ctx->RSGetViewports(&restored, back);
    ctx->ExecuteCommandList(lists[1], FALSE);
    UINT cleared = 4;
    ctx->RSGetViewports(&cleared, back);
    std::vector<UINT> want = {7, 8, 9, 10};
    printf("deferred lists %zu %u %u\n", Wrong(As<UINT>(dev.Read(t, 0, 16, 1)), want), restored, cleared);

    // The same buffer discarded on both contexts.
    ID3D11Texture2D *mt3 = dev.Tex2D(9, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *mv3 = dev.RTV(mt3);
    ID3D11Buffer *both = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    std::vector<UINT> mwant(9);
    for (UINT round = 0; round < 3; round++) {
        UINT base = round * 3;
        for (ID3D11DeviceContext *c : {ctx, def}) {
            c->OMSetRenderTargets(1, &mv3, nullptr);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c->VSSetShader(quad, nullptr, 0);
            c->PSSetShader(cbps, nullptr, 0);
            c->PSSetConstantBuffers(0, 1, &both);
        }
        MapDraw(ctx, both, 100 + base, base);
        MapDraw(def, both, 101 + base, base + 1);
        ID3D11CommandList *list = nullptr;
        CHECK(def->FinishCommandList(FALSE, &list));
        ctx->ExecuteCommandList(list, TRUE);
        list->Release();
        MapDraw(ctx, both, 102 + base, base + 2);
        for (UINT k = 0; k < 3; k++) mwant[base + k] = 100 + base + k;
    }
    printf("deferred mixed %zu\n", Wrong(As<UINT>(dev.Read(mt3, 0, 9 * 4, 1)), mwant));

    const UINT TURNS = 32;
    ID3D11Multithread *mt;
    CHECK(d->QueryInterface(__uuidof(ID3D11Multithread), (void **)&mt));
    mt->SetMultithreadProtected(TRUE);
    ID3D11Texture2D *line = dev.Tex2D(TURNS, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *lv = dev.RTV(line);
    ID3D11Buffer *cb2 = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ctx->OMSetRenderTargets(1, &lv, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(quad, nullptr, 0);
    ctx->PSSetShader(cbps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb2);
    std::mutex m;
    std::condition_variable cv;
    UINT turn = 0;
    auto worker = [&](UINT me) {
        for (;;) {
            std::unique_lock<std::mutex> l(m);
            cv.wait(l, [&] { return turn >= TURNS || turn % 2 == me; });
            if (turn >= TURNS) return;
            mt->Enter();
            MapDraw(ctx, cb2, turn + 1, turn);
            mt->Leave();
            turn++;
            cv.notify_all();
        }
    };
    std::thread a(worker, 0), b(worker, 1);
    a.join(), b.join();
    std::vector<UINT> swant(TURNS);
    for (UINT i = 0; i < TURNS; i++) swant[i] = i + 1;
    printf("deferred shared %zu\n", Wrong(As<UINT>(dev.Read(line, 0, TURNS * 4, 1)), swant));

    // twice: one list executed twice into two texels (using an R8G8B8A8_UNORM RT and additive blend so executing the
    // same command list twice accumulates into both texels).
    {
        ID3D11PixelShader *solid_ps = dev.PS("solid.ps");
        ID3D11Texture2D *t_tw = dev.Tex2D(2, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *rtv_tw = dev.RTV(t_tw);
        float c_tw[4] = {64.0f / 255.0f, 100.0f / 255.0f, 32.0f / 255.0f, 128.0f / 255.0f};
        ID3D11Buffer *cb_tw = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, c_tw);
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ID3D11BlendState *bs_add = nullptr;
        CHECK(d->CreateBlendState(&bd, &bs_add));

        def->OMSetRenderTargets(1, &rtv_tw, nullptr);
        def->OMSetBlendState(bs_add, nullptr, 0xffffffffu);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(quad, nullptr, 0);
        def->PSSetShader(solid_ps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb_tw);
        D3D11_VIEWPORT vp0 = Texel(0), vp1 = Texel(1);
        def->RSSetViewports(1, &vp0);
        def->Draw(3, 0);
        def->RSSetViewports(1, &vp1);
        def->Draw(3, 0);
        ID3D11CommandList *cl_tw = nullptr;
        CHECK(def->FinishCommandList(FALSE, &cl_tw));

        const float zero[4] = {0, 0, 0, 0};
        ctx->ClearRenderTargetView(rtv_tw, zero);
        ctx->ExecuteCommandList(cl_tw, FALSE);
        ctx->ExecuteCommandList(cl_tw, FALSE);
        std::vector<uint8_t> got_tw = dev.Read(t_tw, 0, 8, 1);
        std::vector<uint8_t> want_tw = {128, 200, 64, 255, 128, 200, 64, 255};
        printf("deferred twice %zu\n", Wrong(got_tw, want_tw));

        cl_tw->Release();
        bs_add->Release();
        cb_tw->Release();
        rtv_tw->Release();
        t_tw->Release();
        solid_ps->Release();
    }

    // restore: FinishCommandList(TRUE, &list) keeping deferred state across two lists.
    {
        ID3D11Texture2D *t_rs = dev.Tex2D(2, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *rtv_rs = dev.RTV(t_rs);
        def->OMSetRenderTargets(1, &rtv_rs, nullptr);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(quad, nullptr, 0);
        def->PSSetShader(cbps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb);
        MapDraw(def, cb, 41, 0);
        ID3D11CommandList *cl_r1 = nullptr, *cl_r2 = nullptr;
        CHECK(def->FinishCommandList(TRUE, &cl_r1));
        // Without rebinding rtv_rs, quad, cbps, cb, or topology on `def`:
        MapDraw(def, cb, 42, 1);
        CHECK(def->FinishCommandList(FALSE, &cl_r2));
        ctx->ExecuteCommandList(cl_r1, FALSE);
        ctx->ExecuteCommandList(cl_r2, FALSE);
        std::vector<UINT> want_rs = {41, 42};
        printf("deferred restore %zu\n", Wrong(As<UINT>(dev.Read(t_rs, 0, 8, 1)), want_rs));
        cl_r2->Release();
        cl_r1->Release();
        rtv_rs->Release();
        t_rs->Release();
    }

    // query: an occlusion query begun and ended in a list, and one left open at FinishCommandList, then GetData on imm.
    {
        D3D11_QUERY_DESC qd = {D3D11_QUERY_OCCLUSION, 0};
        ID3D11Query *q_closed = nullptr, *q_open = nullptr;
        CHECK(d->CreateQuery(&qd, &q_closed));
        CHECK(d->CreateQuery(&qd, &q_open));
        def->OMSetRenderTargets(1, &rtv, nullptr);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(quad, nullptr, 0);
        def->PSSetShader(cbps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb);
        def->Begin(q_closed);
        MapDraw(def, cb, 51, 0);
        def->End(q_closed);
        def->Begin(q_open);
        MapDraw(def, cb, 52, 1);
        ID3D11CommandList *cl_q = nullptr;
        CHECK(def->FinishCommandList(FALSE, &cl_q));
        ctx->ExecuteCommandList(cl_q, FALSE);
        ctx->Flush();
        UINT64 sc = 0, so = 0;
        while (ctx->GetData(q_closed, &sc, sizeof(sc), 0) == S_FALSE) Sleep(1);
        while (ctx->GetData(q_open, &so, sizeof(so), 0) == S_FALSE) Sleep(1);
        size_t q_bad = (sc == 1 ? 0 : 1) + (so == 1 ? 0 : 1);
        printf("deferred query %zu\n", q_bad);
        cl_q->Release();
        q_open->Release();
        q_closed->Release();
    }

    // staging: a CopyResource to a staging texture in a list, then Map(READ) on the immediate context.
    {
        ID3D11Texture2D *t_src = dev.Tex2D(2, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *rtv_src = dev.RTV(t_src);
        ID3D11Texture2D *t_stg = dev.Tex2D(2, 1, DXGI_FORMAT_R32_UINT, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
        def->OMSetRenderTargets(1, &rtv_src, nullptr);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(quad, nullptr, 0);
        def->PSSetShader(cbps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb);
        MapDraw(def, cb, 61, 0);
        MapDraw(def, cb, 62, 1);
        def->CopyResource(t_stg, t_src);
        ID3D11CommandList *cl_stg = nullptr;
        CHECK(def->FinishCommandList(FALSE, &cl_stg));
        ctx->ExecuteCommandList(cl_stg, FALSE);
        D3D11_MAPPED_SUBRESOURCE ms = {};
        CHECK(ctx->Map(t_stg, 0, D3D11_MAP_READ, 0, &ms));
        std::vector<UINT> got_stg((const UINT *)ms.pData, (const UINT *)ms.pData + 2);
        ctx->Unmap(t_stg, 0);
        std::vector<UINT> want_stg = {61, 62};
        printf("deferred staging %zu\n", Wrong(got_stg, want_stg));
        cl_stg->Release();
        t_stg->Release();
        rtv_src->Release();
        t_src->Release();
    }

    // update: UpdateSubresource in a list.
    {
        ID3D11Texture2D *t_up = dev.Tex2D(2, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *rtv_up = dev.RTV(t_up);
        ID3D11Buffer *cb_def = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT);
        def->OMSetRenderTargets(1, &rtv_up, nullptr);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(quad, nullptr, 0);
        def->PSSetShader(cbps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb_def);
        UINT v0[64] = {71}, v1[64] = {72};
        def->UpdateSubresource(cb_def, 0, nullptr, v0, 0, 0);
        D3D11_VIEWPORT vp0 = Texel(0), vp1 = Texel(1);
        def->RSSetViewports(1, &vp0);
        def->Draw(3, 0);
        def->UpdateSubresource(cb_def, 0, nullptr, v1, 0, 0);
        def->RSSetViewports(1, &vp1);
        def->Draw(3, 0);
        ID3D11CommandList *cl_up = nullptr;
        CHECK(def->FinishCommandList(FALSE, &cl_up));
        ctx->ExecuteCommandList(cl_up, FALSE);
        std::vector<UINT> want_up = {71, 72};
        printf("deferred update %zu\n", Wrong(As<UINT>(dev.Read(t_up, 0, 8, 1)), want_up));
        cl_up->Release();
        cb_def->Release();
        rtv_up->Release();
        t_up->Release();
    }

    // nooverwrite: a WRITE_DISCARD then WRITE_NO_OVERWRITE map on a dynamic buffer in a list.
    {
        std::vector<char> pt_bc;
        ID3D11VertexShader *pt_vs = dev.VS("point.vs", &pt_bc);
        D3D11_INPUT_ELEMENT_DESC elem = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
        ID3D11InputLayout *il = nullptr;
        CHECK(d->CreateInputLayout(&elem, 1, pt_bc.data(), pt_bc.size(), &il));
        ID3D11Buffer *dvb = dev.Buffer(6 * 2 * sizeof(float), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC,
                                       D3D11_CPU_ACCESS_WRITE);
        ID3D11Texture2D *t_no = dev.Tex2D(2, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *rtv_no = dev.RTV(t_no);

        float tri_verts[6] = {-1.0f, -1.0f, -1.0f, 3.0f, 3.0f, -1.0f};
        D3D11_MAPPED_SUBRESOURCE dm = {};
        CHECK(def->Map(dvb, 0, D3D11_MAP_WRITE_DISCARD, 0, &dm));
        memcpy(dm.pData, tri_verts, sizeof(tri_verts));
        def->Unmap(dvb, 0);

        UINT stride = 2 * sizeof(float), offset = 0;
        def->OMSetRenderTargets(1, &rtv_no, nullptr);
        def->IASetInputLayout(il);
        def->IASetVertexBuffers(0, 1, &dvb, &stride, &offset);
        def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        def->VSSetShader(pt_vs, nullptr, 0);
        def->PSSetShader(cbps, nullptr, 0);
        def->PSSetConstantBuffers(0, 1, &cb);
        MapDraw(def, cb, 81, 0);

        CHECK(def->Map(dvb, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &dm));
        memcpy((float *)dm.pData + 6, tri_verts, sizeof(tri_verts));
        def->Unmap(dvb, 0);
        D3D11_MAPPED_SUBRESOURCE cm = {};
        CHECK(def->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &cm));
        ((UINT *)cm.pData)[0] = 82;
        def->Unmap(cb, 0);
        D3D11_VIEWPORT vp1 = Texel(1);
        def->RSSetViewports(1, &vp1);
        def->Draw(3, 3);

        ID3D11CommandList *cl_no = nullptr;
        CHECK(def->FinishCommandList(FALSE, &cl_no));
        ctx->ExecuteCommandList(cl_no, FALSE);
        std::vector<UINT> want_no = {81, 82};
        printf("deferred nooverwrite %zu\n", Wrong(As<UINT>(dev.Read(t_no, 0, 8, 1)), want_no));
        cl_no->Release();
        rtv_no->Release();
        t_no->Release();
        dvb->Release();
        il->Release();
        pt_vs->Release();
    }
    return 0;
}
