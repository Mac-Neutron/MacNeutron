// D3D11 deferred contexts and a shared immediate context (Direct3D forwarding M4).
//   d3d11_deferred.exe <shaders>   prints "deferred <check> <value>":
//     lists    two command lists recorded on a second thread (state, a WRITE_DISCARD map and a draw each texel),
//              executed on the immediate context, the first with RestoreContextState and the second without: texels
//              wrong, and the immediate context's viewport count after each (1 restored, 0 cleared)
//     shared   two threads taking 32 turns on the immediate context under ID3D11Multithread (Enter, a WRITE_DISCARD
//              map, a draw into the turn's texel, Leave): texels wrong
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
    return 0;
}
