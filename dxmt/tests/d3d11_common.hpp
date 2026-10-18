// Helpers shared by the D3D11 and D3D10 test programs (Direct3D forwarding M4): a device, shaders from the shaders
// folder (argv[1], the invocations' @S: precompiled DXBC that dxbc.cpp writes, so no program compiles HLSL when it
// runs: native builds have no d3dcompiler), and staging readback. Each program prints "<program> <check> <value>"
// lines whose values don't depend on timing.
#pragma once
#include <windows.h>
#include <d3d11_4.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(expr) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    printf("%s failed 0x%08lx\n", #expr, (unsigned long)(unsigned)hr_); exit(1); } } while (0)

inline std::string g_shaders; // the shaders folder, from argv[1]

// Unbuffered output: a run that hangs or is killed still shows its lines.
inline const int g_unbuffered = setvbuf(stdout, nullptr, _IONBF, 0);

inline std::vector<char> Load(const std::string &path) {
    std::vector<char> data;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    return data;
}

// <name>.dxbc from the shaders folder (dxbc.cpp's names: "quad.vs", "sample.ps", ...).
inline std::vector<char> Shader(const char *name) {
    std::vector<char> code = Load(g_shaders + "/" + name + ".dxbc");
    if (code.empty()) { printf("fail no shader %s\n", name); exit(1); }
    return code;
}

struct Dev {
    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL level = {};

    explicit Dev(D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0) {
        CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &device,
                                &level, &ctx));
    }

    ID3D11VertexShader *VS(const char *name, std::vector<char> *code = nullptr) {
        std::vector<char> c = Shader(name);
        ID3D11VertexShader *s;
        CHECK(device->CreateVertexShader(c.data(), c.size(), nullptr, &s));
        if (code) *code = c;
        return s;
    }
    ID3D11PixelShader *PS(const char *name) {
        std::vector<char> c = Shader(name);
        ID3D11PixelShader *s;
        CHECK(device->CreatePixelShader(c.data(), c.size(), nullptr, &s));
        return s;
    }
    ID3D11ComputeShader *CS(const char *name) {
        std::vector<char> c = Shader(name);
        ID3D11ComputeShader *s;
        CHECK(device->CreateComputeShader(c.data(), c.size(), nullptr, &s));
        return s;
    }

    ID3D11Texture2D *Tex2D(UINT w, UINT h, DXGI_FORMAT format, UINT bind, D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
                           UINT cpu = 0, UINT mips = 1, UINT array = 1, const D3D11_SUBRESOURCE_DATA *init = nullptr,
                           UINT misc = 0, UINT samples = 1) {
        D3D11_TEXTURE2D_DESC d = {w, h, mips, array, format, {samples, 0}, usage, bind, cpu, misc};
        ID3D11Texture2D *t;
        CHECK(device->CreateTexture2D(&d, init, &t));
        return t;
    }
    ID3D11Buffer *Buffer(UINT size, UINT bind, D3D11_USAGE usage = D3D11_USAGE_DEFAULT, UINT cpu = 0,
                         const void *init = nullptr, UINT misc = 0, UINT stride = 0) {
        D3D11_BUFFER_DESC d = {size, usage, bind, cpu, misc, stride};
        D3D11_SUBRESOURCE_DATA data = {init, 0, 0};
        ID3D11Buffer *b;
        CHECK(device->CreateBuffer(&d, init ? &data : nullptr, &b));
        return b;
    }
    ID3D11RenderTargetView *RTV(ID3D11Resource *r) {
        ID3D11RenderTargetView *v;
        CHECK(device->CreateRenderTargetView(r, nullptr, &v));
        return v;
    }
    ID3D11ShaderResourceView *SRV(ID3D11Resource *r) {
        ID3D11ShaderResourceView *v;
        CHECK(device->CreateShaderResourceView(r, nullptr, &v));
        return v;
    }

    // Draws n vertices with vs and ps into rtv (w x h), no input layout.
    void Draw(ID3D11VertexShader *vs, ID3D11PixelShader *ps, ID3D11RenderTargetView *rtv, UINT w, UINT h, UINT n = 3) {
        D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->Draw(n, 0);
    }

    // Subresource sub of a texture, through a staging twin, its rows packed (row_bytes each).
    std::vector<uint8_t> Read(ID3D11Texture2D *t, UINT sub, UINT row_bytes, UINT rows) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        UINT mip = sub % d.MipLevels;
        d.Width = d.Width >> mip ? d.Width >> mip : 1, d.Height = d.Height >> mip ? d.Height >> mip : 1;
        d.MipLevels = 1, d.ArraySize = 1, d.SampleDesc = {1, 0};
        d.Usage = D3D11_USAGE_STAGING, d.BindFlags = 0, d.CPUAccessFlags = D3D11_CPU_ACCESS_READ, d.MiscFlags = 0;
        ID3D11Texture2D *s;
        CHECK(device->CreateTexture2D(&d, nullptr, &s));
        ctx->CopySubresourceRegion(s, 0, 0, 0, 0, t, sub, nullptr);
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(s, 0, D3D11_MAP_READ, 0, &m));
        std::vector<uint8_t> out(row_bytes * rows);
        for (UINT y = 0; y < rows; y++) memcpy(&out[y * row_bytes], (const uint8_t *)m.pData + y * m.RowPitch, row_bytes);
        ctx->Unmap(s, 0);
        s->Release();
        return out;
    }
    std::vector<uint8_t> Read(ID3D11Buffer *b, UINT size) {
        ID3D11Buffer *s = Buffer(size, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
        D3D11_BOX box = {0, 0, 0, size, 1, 1};
        ctx->CopySubresourceRegion(s, 0, 0, 0, 0, b, 0, &box);
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(s, 0, D3D11_MAP_READ, 0, &m));
        std::vector<uint8_t> out((const uint8_t *)m.pData, (const uint8_t *)m.pData + size);
        ctx->Unmap(s, 0);
        s->Release();
        return out;
    }
};

// Elements of a and b that differ (the shorter's length; a length difference counts each missing one).
template <class T> inline size_t Wrong(const std::vector<T> &a, const std::vector<T> &b) {
    size_t n = a.size() < b.size() ? a.size() : b.size(), bad = (a.size() > b.size() ? a.size() : b.size()) - n;
    for (size_t i = 0; i < n; i++) bad += !(a[i] == b[i]);
    return bad;
}

template <class T> inline std::vector<T> As(const std::vector<uint8_t> &bytes) {
    std::vector<T> out(bytes.size() / sizeof(T));
    memcpy(out.data(), bytes.data(), out.size() * sizeof(T));
    return out;
}
