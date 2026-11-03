// D3D11 resource-hazard stress cases on one immediate context without mid-sequence Flush (DXMTRS step 5b):
//   d3d11_hazards.exe <shaders> [mode...]   prints "hazard11 <mode> <values>":
//     1) cs-to-all: long compute pass writing a 1024x1024 R32_UINT UAV, followed immediately by a PS sampling/loading
//        it as SRV, a CopyResource blit, and a CS reading it as SRV.
//     2) rt-read:   1024x1024 R32_UINT render target written by a 256-loop PS and immediately read as a PS SRV.
//     3) uav:       two ordered UAV-writing compute dispatches on the same buffer without an intervening SRV rebind.
#include "d3d11_common.hpp"

static bool want_mode(int argc, char **argv, const char *name, const char *num) {
    if (argc <= 2 || !strcmp(argv[2], "all")) return true;
    for (int i = 2; i < argc; i++)
        if (!strcmp(argv[i], name) || !strcmp(argv[i], num)) return true;
    return false;
}

static uint32_t expected_texel(uint32_t x, uint32_t y, uint32_t seed_add) {
    uint32_t v = y * 1024u + x + seed_add;
    for (uint32_t i = 0; i < 256u; i++)
        v = v * 1664525u + 1013904223u + i;
    return (v & 255u) + 1u;
}

static uint32_t expected_uav_elem(uint32_t idx) {
    uint32_t v = idx + 3u;
    for (uint32_t i = 0; i < 256u; i++)
        v = v * 1664525u + 1013904223u + i;
    uint32_t step1 = (v & 65535u) + 1u;
    return step1 * 3u + 7u;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: d3d11_hazards.exe <shaders> [mode...]\n");
        return 2;
    }
    g_shaders = argv[1];

    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;

    ID3D11VertexShader *quad_vs = dev.VS("quad.vs");
    ID3D11ComputeShader *cs_write = dev.CS("haz_cs_write.cs");
    ID3D11PixelShader *ps_read = dev.PS("haz_ps_read.ps");
    ID3D11ComputeShader *cs_read = dev.CS("haz_cs_read.cs");
    ID3D11PixelShader *ps_write = dev.PS("haz_ps_write.ps");
    ID3D11ComputeShader *uav1_cs = dev.CS("haz_uav1.cs");
    ID3D11ComputeShader *uav2_cs = dev.CS("haz_uav2.cs");

    constexpr UINT BIG = 1024;
    constexpr UINT SMALL = 64;

    if (want_mode(argc, argv, "cs-to-all", "1")) {
        uint32_t exp_sum = 0;
        for (uint32_t y = 0; y < BIG; y++)
            for (uint32_t x = 0; x < BIG; x++)
                exp_sum += expected_texel(x, y, 1u);

        ID3D11Texture2D *uav_tex =
            dev.Tex2D(BIG, BIG, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE);
        ID3D11Texture2D *copy_tex = dev.Tex2D(BIG, BIG, DXGI_FORMAT_R32_UINT, D3D11_BIND_SHADER_RESOURCE);

        ID3D11UnorderedAccessView *tex_uav = nullptr;
        ID3D11ShaderResourceView *tex_srv = dev.SRV(uav_tex);
        CHECK(d->CreateUnorderedAccessView(uav_tex, nullptr, &tex_uav));

        ID3D11Texture2D *ps_rt = dev.Tex2D(SMALL, SMALL, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *ps_rtv = dev.RTV(ps_rt);

        uint32_t zero = 0;
        ID3D11Buffer *cs_out = dev.Buffer(4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, &zero,
                                          D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
        D3D11_UNORDERED_ACCESS_VIEW_DESC buavd = {};
        buavd.Format = DXGI_FORMAT_UNKNOWN;
        buavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        buavd.Buffer.NumElements = 1;
        ID3D11UnorderedAccessView *cs_out_uav = nullptr;
        CHECK(d->CreateUnorderedAccessView(cs_out, &buavd, &cs_out_uav));

        // Issue all 4 GPU operations back-to-back without any mid-sequence Flush:
        ctx->CSSetShader(cs_write, nullptr, 0);
        ctx->CSSetUnorderedAccessViews(0, 1, &tex_uav, nullptr);
        ctx->Dispatch(64, 64, 1);
        ID3D11UnorderedAccessView *null_uav = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

        ctx->PSSetShaderResources(0, 1, &tex_srv);
        dev.Draw(quad_vs, ps_read, ps_rtv, SMALL, SMALL);
        ID3D11ShaderResourceView *null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);

        ctx->CopyResource(copy_tex, uav_tex);

        ctx->CSSetShader(cs_read, nullptr, 0);
        ctx->CSSetShaderResources(0, 1, &tex_srv);
        ctx->CSSetUnorderedAccessViews(0, 1, &cs_out_uav, nullptr);
        ctx->Dispatch(64, 64, 1);
        ctx->CSSetShaderResources(0, 1, &null_srv);
        ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

        uint32_t ps_sum = 0;
        for (uint32_t v : As<uint32_t>(dev.Read(ps_rt, 0, SMALL * 4, SMALL))) ps_sum += v;

        uint32_t copy_sum = 0;
        for (uint32_t v : As<uint32_t>(dev.Read(copy_tex, 0, BIG * 4, BIG))) copy_sum += v;

        uint32_t cs_sum = As<uint32_t>(dev.Read(cs_out, 4))[0];

        int ok = (ps_sum == exp_sum && copy_sum == exp_sum && cs_sum == exp_sum) ? 1 : 0;
        printf("hazard11 cs-to-all %u %u %u %d\n", ps_sum, copy_sum, cs_sum, ok);

        cs_out_uav->Release();
        cs_out->Release();
        ps_rtv->Release();
        ps_rt->Release();
        tex_srv->Release();
        tex_uav->Release();
        copy_tex->Release();
        uav_tex->Release();
    }

    if (want_mode(argc, argv, "rt-read", "2")) {
        uint32_t exp_sum = 0;
        for (uint32_t y = 0; y < BIG; y++)
            for (uint32_t x = 0; x < BIG; x++)
                exp_sum += expected_texel(x, y, 7u);

        ID3D11Texture2D *big_rt =
            dev.Tex2D(BIG, BIG, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
        ID3D11RenderTargetView *big_rtv = dev.RTV(big_rt);
        ID3D11ShaderResourceView *big_srv = dev.SRV(big_rt);

        ID3D11Texture2D *out_rt = dev.Tex2D(SMALL, SMALL, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET);
        ID3D11RenderTargetView *out_rtv = dev.RTV(out_rt);

        dev.Draw(quad_vs, ps_write, big_rtv, BIG, BIG);

        ctx->OMSetRenderTargets(1, &out_rtv, nullptr);
        ctx->PSSetShaderResources(0, 1, &big_srv);
        dev.Draw(quad_vs, ps_read, out_rtv, SMALL, SMALL);
        ID3D11ShaderResourceView *null_srv = nullptr;
        ctx->PSSetShaderResources(0, 1, &null_srv);

        uint32_t rt_sum = 0;
        for (uint32_t v : As<uint32_t>(dev.Read(out_rt, 0, SMALL * 4, SMALL))) rt_sum += v;

        int ok = (rt_sum == exp_sum) ? 1 : 0;
        printf("hazard11 rt-read %u %d\n", rt_sum, ok);

        out_rtv->Release();
        out_rt->Release();
        big_srv->Release();
        big_rtv->Release();
        big_rt->Release();
    }

    if (want_mode(argc, argv, "uav", "3")) {
        constexpr UINT COUNT = 65536;
        ID3D11Buffer *ubuf = dev.Buffer(COUNT * 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, nullptr,
                                        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4);
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
        uavd.Format = DXGI_FORMAT_UNKNOWN;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavd.Buffer.NumElements = COUNT;
        ID3D11UnorderedAccessView *uav = nullptr;
        CHECK(d->CreateUnorderedAccessView(ubuf, &uavd, &uav));

        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        ctx->CSSetShader(uav1_cs, nullptr, 0);
        ctx->Dispatch(COUNT / 64, 1, 1);
        ctx->CSSetShader(uav2_cs, nullptr, 0);
        ctx->Dispatch(COUNT / 64, 1, 1);
        ID3D11UnorderedAccessView *null_uav = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

        std::vector<uint32_t> got = As<uint32_t>(dev.Read(ubuf, COUNT * 4));
        uint32_t sum = 0, bad = 0;
        for (UINT i = 0; i < COUNT; i++) {
            sum += got[i];
            if (got[i] != expected_uav_elem(i)) bad++;
        }
        printf("hazard11 uav %u %u %d\n", sum, bad, bad == 0 ? 1 : 0);

        uav->Release();
        ubuf->Release();
    }

    uav2_cs->Release();
    uav1_cs->Release();
    ps_write->Release();
    cs_read->Release();
    ps_read->Release();
    cs_write->Release();
    quad_vs->Release();
    return 0;
}
