// Task F2: D3D11 vertex inputs from unbound slots, as d3d12_vsread's ia mode (D3D11 pulls vertices through the same
// translator code): slot 0 bound to (1, 2, 3, 4), slot 1 never bound (R32G32_FLOAT and R32G32B32A32_FLOAT), slot 3
// unbound by a null buffer (R32_UINT). Its shaders are dxbc.cpp's vsia.vs and vsia.ps from the shaders folder (M4:
// natively there is no d3dcompiler to compile them when it runs).
//   d3d11_vsia.exe <shaders>   prints "d3d11 ia <a> <b> <c> <d>", four floats each, from texel (2,2) of four RGBA32F
//                    targets.
//   d3d11_vsia.exe compile <in.hlsl> <entry> <target> <out>   compiles a file with D3DCompile and writes the bytecode
//                    (Task FR: dxbc/sync.dxbc, a DXBC shader for dxil-translate's offline rows).
// D3D: unbound slots read zeros, widened by the format: (0,0,0,1) for b and d, (0,0,0,0) for c.
#include <windows.h>
#include <d3d11.h>
#ifdef _WIN32
#include <d3dcompiler.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(expr) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    printf("%s failed 0x%08lx\n", #expr, (unsigned long)(unsigned)hr_); exit(1); } } while (0)

static std::vector<char> Load(const std::string &path) {
    std::vector<char> data;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    if (data.empty()) { printf("fail no shader %s\n", path.c_str()); exit(1); }
    return data;
}

#ifdef _WIN32
static int CompileFile(char **argv) {
    FILE *f = fopen(argv[2], "rb");
    if (!f) { printf("can't read %s\n", argv[2]); return 1; }
    static char source[65536];
    size_t n = fread(source, 1, sizeof source, f);
    fclose(f);
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(source, n, argv[2], nullptr, nullptr, argv[3], argv[4], 0, 0, &code, &errors);
    if (FAILED(hr)) {
        printf("D3DCompile failed 0x%08lx %s\n", (unsigned long)(unsigned)hr, errors ? (const char *)errors->GetBufferPointer() : "");
        return 1;
    }
    f = fopen(argv[5], "wb");
    if (!f || fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), f) != code->GetBufferSize() || fclose(f)) {
        printf("can't write %s\n", argv[5]);
        return 1;
    }
    printf("compiled %s: %zu bytes\n", argv[5], (size_t)code->GetBufferSize());
    return 0;
}

#endif

int main(int argc, char **argv) {
#ifdef _WIN32
    if (argc == 6 && !strcmp(argv[1], "compile")) return CompileFile(argv);
#endif
    if (argc != 2) { printf("usage: d3d11_vsia.exe <shaders>\n"); return 2; }
    ID3D11Device *device; ID3D11DeviceContext *ctx;
    CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device,
                            nullptr, &ctx));
    std::vector<char> vs_code = Load(std::string(argv[1]) + "/vsia.vs.dxbc"), ps_code = Load(std::string(argv[1]) + "/vsia.ps.dxbc");
    ID3D11VertexShader *vs; ID3D11PixelShader *ps;
    CHECK(device->CreateVertexShader(vs_code.data(), vs_code.size(), nullptr, &vs));
    CHECK(device->CreatePixelShader(ps_code.data(), ps_code.size(), nullptr, &ps));
    D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
        {"A", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"B", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"C", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"D", 0, DXGI_FORMAT_R32_UINT, 3, 4, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ID3D11InputLayout *layout;
    CHECK(device->CreateInputLayout(layout_desc, 4, vs_code.data(), vs_code.size(), &layout));

    float a[4] = {1, 2, 3, 4};
    D3D11_BUFFER_DESC bd = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER};
    D3D11_SUBRESOURCE_DATA init = {a};
    ID3D11Buffer *vb;
    CHECK(device->CreateBuffer(&bd, &init, &vb));

    ID3D11Texture2D *targets[4], *staging[4];
    ID3D11RenderTargetView *rtvs[4];
    D3D11_TEXTURE2D_DESC td = {4, 4, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET};
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < 4; i++) {
        CHECK(device->CreateTexture2D(&td, nullptr, &targets[i]));
        CHECK(device->CreateTexture2D(&sd, nullptr, &staging[i]));
        CHECK(device->CreateRenderTargetView(targets[i], nullptr, &rtvs[i]));
    }
    D3D11_VIEWPORT viewport = {0, 0, 4, 4, 0, 1};
    ctx->OMSetRenderTargets(4, rtvs, nullptr);
    ctx->RSSetViewports(1, &viewport);
    ctx->IASetInputLayout(layout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = 0, offset = 0, null_stride = 64;
    ID3D11Buffer *none = nullptr;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);   // (1, 2, 3, 4) for every vertex
    ctx->IASetVertexBuffers(3, 1, &none, &null_stride, &offset);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->Draw(3, 0);
    for (int i = 0; i < 4; i++)
        ctx->CopyResource(staging[i], targets[i]);
    printf("d3d11 ia");
    for (int i = 0; i < 4; i++) {
        D3D11_MAPPED_SUBRESOURCE m;
        CHECK(ctx->Map(staging[i], 0, D3D11_MAP_READ, 0, &m));
        const float *t = (const float *)((const char *)m.pData + 2 * m.RowPitch) + 2 * 4;  // texel (2,2)
        printf(" %g,%g,%g,%g", t[0], t[1], t[2], t[3]);
        ctx->Unmap(staging[i], 0);
    }
    printf("\n");
    return 0;
}
