// D3D11 dynamic resources (Direct3D forwarding M4, spec §4.10): each map's data is the one the next draw reads.
//   d3d11_dynamic.exe <shaders> [frames]   prints "dynamic <check> <value>":
//     discard <frame>   1,000 Map(WRITE_DISCARD) of a 256-byte constant buffer in a frame, a draw after each writing
//                       its value into its own texel of a 1000x1 target: texels wrong (frames of them, 4 by default:
//                       forwarding's piece test runs 1 and 30)
//     nooverwrite       Map(WRITE_DISCARD) then 100 Map(WRITE_NO_OVERWRITE) appending 3 points each to a dynamic
//                       vertex buffer, a draw of each 3: lit texels of a 64x64 target (300 when right)
//     offsets           VSSetConstantBuffers1/PSSetConstantBuffers1 into 16 slots of one buffer, a draw each: texels
//                       wrong (or skip without ConstantBufferOffsetting)
//     texture <round>   a 64x64 dynamic texture mapped WRITE_DISCARD with its row pitch, drawn: texels wrong
//     stress            10,000 Map(WRITE_DISCARD) of a 64 KiB constant buffer in one frame (both ends written), a draw
//                       after each into its own texel of a 100x100 target: texels wrong
#include "d3d11_common.hpp"

static D3D11_VIEWPORT Texel(UINT x, UINT y) { return {(float)x, (float)y, 1, 1, 0, 1}; }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) { printf("usage: d3d11_dynamic.exe <shaders> [frames]\n"); return 2; }
    g_shaders = argv[1];
    const UINT frames = argc == 3 ? (UINT)atoi(argv[2]) : 4;
    Dev dev(D3D_FEATURE_LEVEL_11_1);
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;
    ID3D11VertexShader *quad = dev.VS("quad.vs");
    ID3D11PixelShader *cbps = dev.PS("cb.ps"), *big = dev.PS("cbbig.ps");

    // 1,000 discards of a small constant buffer a frame.
    const UINT N = 1000;
    ID3D11Texture2D *line = dev.Tex2D(N, 1, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *lv = dev.RTV(line);
    ID3D11Buffer *cb = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ctx->OMSetRenderTargets(1, &lv, nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(quad, nullptr, 0);
    ctx->PSSetShader(cbps, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cb);
    for (UINT frame = 0; frame < frames; frame++) {
        for (UINT i = 0; i < N; i++) {
            D3D11_MAPPED_SUBRESOURCE m;
            CHECK(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
            ((UINT *)m.pData)[0] = frame * N + i + 1;
            ctx->Unmap(cb, 0);
            D3D11_VIEWPORT vp = Texel(i, 0);
            ctx->RSSetViewports(1, &vp);
            ctx->Draw(3, 0);
        }
        std::vector<UINT> got = As<UINT>(dev.Read(line, 0, N * 4, 1)), want(N);
        for (UINT i = 0; i < N; i++) want[i] = frame * N + i + 1;
        printf("dynamic discard %u %zu\n", frame, Wrong(got, want));
    }

    // Points appended with WRITE_NO_OVERWRITE after one WRITE_DISCARD.
    std::vector<char> vs_code;
    ID3D11VertexShader *pvs = dev.VS("point.vs", &vs_code);
    D3D11_INPUT_ELEMENT_DESC le = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ID3D11InputLayout *layout;
    CHECK(d->CreateInputLayout(&le, 1, vs_code.data(), vs_code.size(), &layout));
    ID3D11Buffer *vb = dev.Buffer(65536, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ID3D11Texture2D *pt = dev.Tex2D(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *pv = dev.RTV(pt);
    const float black[4] = {0, 0, 0, 0}, white[4] = {1, 1, 1, 1};
    ctx->ClearRenderTargetView(pv, black);
    ID3D11Buffer *color = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, white);
    D3D11_VIEWPORT full = {0, 0, 64, 64, 0, 1};
    UINT stride = 8, offset = 0;
    ctx->OMSetRenderTargets(1, &pv, nullptr);
    ctx->RSSetViewports(1, &full);
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(pvs, nullptr, 0);
    ctx->PSSetShader(dev.PS("solid.ps"), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &color);
    for (UINT k = 0; k < 100; k++) {
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(vb, 0, k ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD, 0, &m));
        float *p = (float *)m.pData + k * 6;
        for (UINT j = 0; j < 3; j++) {
            UINT n = k * 3 + j, x = n % 64, y = n / 64;  // texel centres, in clip space
            p[j * 2] = (x + 0.5f) / 32 - 1, p[j * 2 + 1] = 1 - (y + 0.5f) / 32;
        }
        ctx->Unmap(vb, 0);
        ctx->Draw(3, k * 3);
    }
    size_t lit = 0;
    for (uint32_t px : As<uint32_t>(dev.Read(pt, 0, 64 * 4, 64))) lit += px != 0;
    printf("dynamic nooverwrite %zu\n", lit);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(quad, nullptr, 0);

    // Constant buffer offsets: 16 slots of 256 bytes (16 constants) in one buffer.
    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
    d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof options);
    ID3D11DeviceContext1 *ctx1 = nullptr;
    if (!options.ConstantBufferOffsetting || FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&ctx1))) {
        printf("dynamic offsets skip no ConstantBufferOffsetting\n");
    } else {
        std::vector<UINT> slots(16 * 64, 0);
        for (UINT k = 0; k < 16; k++) slots[k * 64] = 100 + k;
        ID3D11Buffer *ob = dev.Buffer(16 * 256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, slots.data());
        ctx->OMSetRenderTargets(1, &lv, nullptr);
        ctx->PSSetShader(cbps, nullptr, 0);
        for (UINT k = 0; k < 16; k++) {
            UINT first = k * 16, count = 16;
            ctx1->PSSetConstantBuffers1(0, 1, &ob, &first, &count);
            D3D11_VIEWPORT vp = Texel(k, 0);
            ctx->RSSetViewports(1, &vp);
            ctx->Draw(3, 0);
        }
        std::vector<UINT> got = As<UINT>(dev.Read(line, 0, 16 * 4, 1)), want(16);
        for (UINT k = 0; k < 16; k++) want[k] = 100 + k;
        printf("dynamic offsets %zu\n", Wrong(got, want));
        ctx->PSSetConstantBuffers(0, 1, &cb);
    }

    // A dynamic texture mapped WRITE_DISCARD, its row pitch honoured, drawn after each map.
    ID3D11Texture2D *dt = dev.Tex2D(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                    D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ID3D11ShaderResourceView *dv = dev.SRV(dt);
    ID3D11Texture2D *out = dev.Tex2D(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *ov = dev.RTV(out);
    const UINT mip0[4] = {};
    ID3D11Buffer *mipcb = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, mip0);
    ID3D11PixelShader *sample = dev.PS("sample.ps");
    for (UINT round = 0; round < 4; round++) {
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(dt, 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
        std::vector<uint32_t> want(64 * 64);
        for (UINT y = 0; y < 64; y++)
            for (UINT x = 0; x < 64; x++) {
                want[y * 64 + x] = 0xff000000u | (round << 16) | (y << 8) | x;
                ((uint32_t *)((uint8_t *)m.pData + y * m.RowPitch))[x] = want[y * 64 + x];
            }
        ctx->Unmap(dt, 0);
        ctx->PSSetShaderResources(0, 1, &dv);
        ctx->PSSetConstantBuffers(0, 1, &mipcb);
        dev.Draw(quad, sample, ov, 64, 64);
        printf("dynamic texture %u %zu\n", round, Wrong(As<uint32_t>(dev.Read(out, 0, 64 * 4, 64)), want));
    }
    ID3D11ShaderResourceView *none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);

    // 10,000 discards of a 64 KiB constant buffer in one frame.
    const UINT S = 10000;
    ID3D11Texture2D *grid = dev.Tex2D(100, 100, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *gv = dev.RTV(grid);
    ID3D11Buffer *bigcb = dev.Buffer(65536, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    ctx->OMSetRenderTargets(1, &gv, nullptr);
    ctx->PSSetShader(big, nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &bigcb);
    std::vector<UINT> want(S);
    for (UINT i = 0; i < S; i++) {
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(bigcb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m));
        ((UINT *)m.pData)[0] = i + 1;
        ((UINT *)m.pData)[4095 * 4] = (i * 7) & 0xffff;
        ctx->Unmap(bigcb, 0);
        want[i] = (i + 1) ^ (((i * 7) & 0xffff) << 16);
        D3D11_VIEWPORT vp = Texel(i % 100, i / 100);
        ctx->RSSetViewports(1, &vp);
        ctx->Draw(3, 0);
    }
    printf("dynamic stress %zu\n", Wrong(As<UINT>(dev.Read(grid, 0, 100 * 4, 100)), want));
    return 0;
}
