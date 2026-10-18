// D3D11 copies, updates, initial data and staging reads (Direct3D forwarding M4), in the layouts whose sizes the
// forwarding computes from a resource's description: buffers, 2D and 3D textures, mipped arrays, BC1 and BC7 (a box at
// a 2x2 mip tail included), with pitches wider than the rows. Block-compressed textures are read back by drawing them
// into an RGBA8 target (DXMT copies none to staging).
//   d3d11_transfer.exe <shaders>   prints "transfer <check> <value>": bytes wrong for a check whose bytes are known,
//   a target's byte sum for a block-compressed one. The DO_NOT_WAIT read flushes before it polls: DXMT submits nothing
//   for a map that answers WAS_STILL_DRAWING, so a poll without a flush never ends (on macOS too).
#include "d3d11_common.hpp"

static std::vector<uint8_t> Pattern(size_t n, unsigned seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(i * 7 + seed * 31 + (i >> 8));
    return v;
}

// A 3D texture's subresource 0 through a staging twin, rows packed.
static std::vector<uint8_t> Read3D(Dev &dev, ID3D11Texture3D *t, UINT row_bytes, UINT rows, UINT depth) {
    D3D11_TEXTURE3D_DESC d;
    t->GetDesc(&d);
    d.MipLevels = 1, d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0;
    ID3D11Texture3D *s;
    CHECK(dev.device->CreateTexture3D(&d, nullptr, &s));
    dev.ctx->CopySubresourceRegion(s, 0, 0, 0, 0, t, 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE m;
    CHECK(dev.ctx->Map(s, 0, D3D11_MAP_READ, 0, &m));
    std::vector<uint8_t> out(row_bytes * rows * depth);
    for (UINT z = 0; z < depth; z++)
        for (UINT y = 0; y < rows; y++)
            memcpy(&out[(z * rows + y) * row_bytes], (const uint8_t *)m.pData + z * m.DepthPitch + y * m.RowPitch, row_bytes);
    dev.ctx->Unmap(s, 0);
    s->Release();
    return out;
}

// A block-compressed texture's mip drawn into an RGBA8 target of its size: the target's byte sum.
static unsigned long Drawn(Dev &dev, ID3D11Texture2D *t, UINT mip, UINT w, UINT h) {
    ID3D11Texture2D *target = dev.Tex2D(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(target);
    ID3D11ShaderResourceView *srv = dev.SRV(t);
    const UINT cbv[4] = {mip, 0, 0, 0};
    ID3D11Buffer *cb = dev.Buffer(16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, cbv);
    dev.ctx->PSSetShaderResources(0, 1, &srv);
    dev.ctx->PSSetConstantBuffers(0, 1, &cb);
    dev.Draw(dev.VS("quad.vs"), dev.PS("sample.ps"), rtv, w, h);
    ID3D11ShaderResourceView *none = nullptr;
    dev.ctx->PSSetShaderResources(0, 1, &none);
    unsigned long sum = 0;
    for (uint8_t b : dev.Read(target, 0, w * 4, h)) sum += b;
    srv->Release(), rtv->Release(), target->Release(), cb->Release();
    return sum;
}

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_transfer.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;

    // CopyResource and CopySubresourceRegion.
    std::vector<uint8_t> a = Pattern(32 * 32 * 4, 1);
    D3D11_SUBRESOURCE_DATA ia = {a.data(), 32 * 4, 0};
    ID3D11Texture2D *src = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                     D3D11_USAGE_DEFAULT, 0, 1, 1, &ia);
    ID3D11Texture2D *dst = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    ctx->CopyResource(dst, src);
    printf("transfer copyresource %zu\n", Wrong(dev.Read(dst, 0, 32 * 4, 32), a));
    std::vector<uint8_t> bytes = Pattern(512, 2), bwant(512, 0);
    ID3D11Buffer *bsrc = dev.Buffer(512, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, bytes.data());
    std::vector<uint8_t> zeros(512, 0);
    ID3D11Buffer *bdst = dev.Buffer(512, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, zeros.data());
    D3D11_BOX bbox = {100, 0, 0, 300, 1, 1};
    ctx->CopySubresourceRegion(bdst, 0, 52, 0, 0, bsrc, 0, &bbox);
    memcpy(&bwant[52], &bytes[100], 200);
    printf("transfer copybuffer %zu\n", Wrong(dev.Read(bdst, 512), bwant));
    ID3D11Texture2D *dst2 = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    D3D11_BOX tbox = {8, 8, 0, 24, 16, 1};
    ctx->CopySubresourceRegion(dst2, 0, 0, 4, 0, src, 0, &tbox);
    std::vector<uint8_t> twant(32 * 32 * 4, 0);
    for (UINT y = 0; y < 8; y++) memcpy(&twant[((y + 4) * 32) * 4], &a[((y + 8) * 32 + 8) * 4], 16 * 4);
    printf("transfer copyregion %zu\n", Wrong(dev.Read(dst2, 0, 32 * 4, 32), twant));

    // UpdateSubresource: a buffer box, a 2D box, a 3D box, a mipped array's subresource, BC1 and BC7.
    ID3D11Buffer *ub = dev.Buffer(256, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0, zeros.data());
    std::vector<uint8_t> up = Pattern(128, 3), uwant(256, 0);
    D3D11_BOX ubox = {64, 0, 0, 192, 1, 1};
    ctx->UpdateSubresource(ub, 0, &ubox, up.data(), 0, 0);
    memcpy(&uwant[64], up.data(), 128);
    printf("transfer updatebuffer %zu\n", Wrong(dev.Read(ub, 256), uwant));

    ID3D11Texture2D *u2 = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    const UINT pitch2 = 16 * 4 + 12;  // wider than the box's rows
    std::vector<uint8_t> p2 = Pattern(pitch2 * 8, 4), w2(32 * 32 * 4, 0);
    D3D11_BOX box2 = {4, 4, 0, 20, 12, 1};
    ctx->UpdateSubresource(u2, 0, &box2, p2.data(), pitch2, 0);
    for (UINT y = 0; y < 8; y++) memcpy(&w2[((y + 4) * 32 + 4) * 4], &p2[y * pitch2], 16 * 4);
    printf("transfer update2d %zu\n", Wrong(dev.Read(u2, 0, 32 * 4, 32), w2));

    D3D11_TEXTURE3D_DESC d3 = {16, 16, 8, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
    ID3D11Texture3D *u3;
    CHECK(d->CreateTexture3D(&d3, nullptr, &u3));
    const UINT rp3 = 8 + 4, sp3 = rp3 * 8 + 20;
    std::vector<uint8_t> p3 = Pattern(sp3 * 4, 5), w3(16 * 16 * 8, 0);
    D3D11_BOX box3 = {2, 2, 1, 10, 10, 5};
    ctx->UpdateSubresource(u3, 0, &box3, p3.data(), rp3, sp3);
    for (UINT z = 0; z < 4; z++)
        for (UINT y = 0; y < 8; y++) memcpy(&w3[((z + 1) * 16 + y + 2) * 16 + 2], &p3[z * sp3 + y * rp3], 8);
    printf("transfer update3d %zu\n", Wrong(Read3D(dev, u3, 16, 16, 8), w3));

    ID3D11Texture2D *arr = dev.Tex2D(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                     D3D11_USAGE_DEFAULT, 0, 6, 3);
    UINT sub = D3D11CalcSubresource(2, 1, 6);
    std::vector<uint8_t> pa = Pattern(8 * 8 * 4, 6);
    ctx->UpdateSubresource(arr, sub, nullptr, pa.data(), 8 * 4, 0);
    printf("transfer updatearray %zu\n", Wrong(dev.Read(arr, sub, 8 * 4, 8), pa));

    ID3D11Texture2D *bc1 = dev.Tex2D(32, 32, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE);
    std::vector<uint8_t> pb1 = Pattern(4 * 8 * 4, 7);  // 4x4 blocks of 8 bytes: a 16x16 box
    D3D11_BOX boxb1 = {8, 8, 0, 24, 24, 1};
    ctx->UpdateSubresource(bc1, 0, &boxb1, pb1.data(), 4 * 8, 0);
    printf("transfer updatebc1 %lu\n", Drawn(dev, bc1, 0, 32, 32));

    ID3D11Texture2D *bc7 = dev.Tex2D(8, 8, DXGI_FORMAT_BC7_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
                                     0, 4);
    std::vector<uint8_t> pb7 = Pattern(16, 8);  // mip 2 is 2x2: one block
    D3D11_BOX boxb7 = {0, 0, 0, 2, 2, 1};
    ctx->UpdateSubresource(bc7, 2, &boxb7, pb7.data(), 16, 0);
    printf("transfer updatebc7tail %lu\n", Drawn(dev, bc7, 2, 2, 2));

    ID3D11DeviceContext1 *ctx1 = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void **)&ctx1))) {
        std::vector<uint8_t> p1 = Pattern(64, 9);
        D3D11_BOX box1 = {0, 0, 0, 64, 1, 1};
        ctx1->UpdateSubresource1(ub, 0, &box1, p1.data(), 0, 0, D3D11_COPY_DISCARD);
        std::vector<uint8_t> got = dev.Read(ub, 256);  // COPY_DISCARD: the bytes past the box are undefined now
        got.resize(64);
        printf("transfer update1 %zu\n", Wrong(got, p1));
    } else {
        printf("transfer update1 skip no ID3D11DeviceContext1\n");
    }

    // Initial data with pitches wider than the rows: 3D, a mipped array, BC1 with two mips.
    D3D11_TEXTURE3D_DESC i3 = {8, 8, 4, 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE};
    const UINT irp = 12, isp = irp * 8 + 4;
    std::vector<uint8_t> pi3 = Pattern(isp * 4, 10), wi3(8 * 8 * 4);
    for (UINT z = 0; z < 4; z++)
        for (UINT y = 0; y < 8; y++) memcpy(&wi3[(z * 8 + y) * 8], &pi3[z * isp + y * irp], 8);
    D3D11_SUBRESOURCE_DATA s3 = {pi3.data(), irp, isp};
    ID3D11Texture3D *t3;
    CHECK(d->CreateTexture3D(&i3, &s3, &t3));
    printf("transfer init3d %zu\n", Wrong(Read3D(dev, t3, 8, 8, 4), wi3));

    std::vector<std::vector<uint8_t>> subs;
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (UINT slice = 0; slice < 3; slice++)
        for (UINT mip = 0; mip < 4; mip++) {
            UINT w = 16 >> mip;
            subs.push_back(Pattern((w * 4 + 8) * w, 11 + slice * 4 + mip));
        }
    for (UINT i = 0; i < subs.size(); i++) data.push_back({subs[i].data(), (16u >> (i % 4)) * 4 + 8, 0});
    ID3D11Texture2D *ta = dev.Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE,
                                    D3D11_USAGE_DEFAULT, 0, 4, 3, data.data());
    size_t wrong = 0;
    for (UINT i = 0; i < subs.size(); i++) {
        UINT w = 16 >> (i % 4), p = w * 4 + 8;
        std::vector<uint8_t> want(w * 4 * w);
        for (UINT y = 0; y < w; y++) memcpy(&want[y * w * 4], &subs[i][y * p], w * 4);
        wrong += Wrong(dev.Read(ta, i, w * 4, w), want);
    }
    printf("transfer initarray %zu\n", wrong);

    std::vector<uint8_t> m0 = Pattern(4 * 8 * 4, 30), m1 = Pattern(2 * 8 * 2, 31);  // mip 0: 4x4 blocks, mip 1: 2x2
    D3D11_SUBRESOURCE_DATA sb1[2] = {{m0.data(), 4 * 8, 0}, {m1.data(), 2 * 8, 0}};
    ID3D11Texture2D *tb1 = dev.Tex2D(16, 16, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT,
                                     0, 2, 1, sb1);
    printf("transfer initbc1 %lu %lu\n", Drawn(dev, tb1, 0, 16, 16), Drawn(dev, tb1, 1, 8, 8));

    // Staging reads right after the GPU wrote: waited, and polled with DO_NOT_WAIT.
    ID3D11Texture2D *rt = dev.Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ID3D11RenderTargetView *rtv = dev.RTV(rt);
    const float c[4] = {0.5f, 0.25f, 1, 1};
    std::vector<uint32_t> want(256, 0xffff4080u);
    for (int nowait = 0; nowait < 2; nowait++) {
        ctx->ClearRenderTargetView(rtv, c);
        D3D11_TEXTURE2D_DESC sd = {16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0,
                                   D3D11_CPU_ACCESS_READ};
        ID3D11Texture2D *st;
        CHECK(d->CreateTexture2D(&sd, nullptr, &st));
        ctx->CopyResource(st, rt);
        D3D11_MAPPED_SUBRESOURCE m;
        HRESULT hr;
        if (nowait) {
            ctx->Flush();  // DXMT submits nothing for a map that answers WAS_STILL_DRAWING: a poll needs a flush first
            while ((hr = ctx->Map(st, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m)) == DXGI_ERROR_WAS_STILL_DRAWING)
                Sleep(0);
        }
        else
            hr = ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
        std::vector<uint32_t> got(256);
        if (SUCCEEDED(hr)) {
            for (UINT y = 0; y < 16; y++) memcpy(&got[y * 16], (const uint8_t *)m.pData + y * m.RowPitch, 64);
            ctx->Unmap(st, 0);
        }
        printf("transfer %s %08lx %zu\n", nowait ? "nowait" : "readback", (unsigned long)(unsigned)hr, Wrong(got, want));
        st->Release();
    }
    return 0;
}
