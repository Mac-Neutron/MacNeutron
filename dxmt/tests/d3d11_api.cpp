// D3D11's device and its answers (Direct3D forwarding M4): what a game asks before it draws.
//   d3d11_api.exe <shaders>   prints "api <check> <value>" lines:
//     level <requested>          D3D11CreateDevice asked for that level alone: its HRESULT and the level made
//     threading, doubles, options, arch   CheckFeatureSupport's answers
//     format <format>            CheckFormatSupport's bits for eight formats
//     msaa <count>               CheckMultisampleQualityLevels for RGBA8 at 1, 2, 4 and 8 samples
//     privatedata                SetPrivateData then GetPrivateData: the size and value read back
//     dxgi                       IDXGIDevice -> GetAdapter -> GetParent(IDXGIFactory1): each HRESULT, the vendor id
//     identity <interface>       QueryInterface from the device to it: its HRESULT, and whether its IUnknown is the
//                                device's and querying ID3D11Device back gives the device
//     usage                      IDXGIResource::GetUsage of a render target texture
#include "d3d11_common.hpp"
#include <d3d10_1.h>
#include <dxgi1_2.h>

static const GUID kData = {0x6d2f8c51, 0x1e2b, 0x4c3a, {0x9f, 0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}};

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_api.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    for (D3D_FEATURE_LEVEL want : {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0}) {
        ID3D11Device *d = nullptr;
        D3D_FEATURE_LEVEL got = {};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &d,
                                       &got, nullptr);
        printf("api level %x %08lx %x\n", (unsigned)want, (unsigned long)(unsigned)hr, (unsigned)got);
        if (d) d->Release();
    }
    Dev dev;
    ID3D11Device *device = dev.device;

    D3D11_FEATURE_DATA_THREADING threading = {};
    HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof threading);
    printf("api threading %08lx %d %d\n", (unsigned long)(unsigned)hr, threading.DriverConcurrentCreates,
           threading.DriverCommandLists);
    D3D11_FEATURE_DATA_DOUBLES doubles = {};
    hr = device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof doubles);
    printf("api doubles %08lx %d\n", (unsigned long)(unsigned)hr, doubles.DoublePrecisionFloatShaderOps);
    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
    hr = device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof options);
    printf("api options %08lx %d %d %d %d\n", (unsigned long)(unsigned)hr, options.OutputMergerLogicOp,
           options.ConstantBufferOffsetting, options.MapNoOverwriteOnDynamicConstantBuffer,
           options.MapNoOverwriteOnDynamicBufferSRV);
    D3D11_FEATURE_DATA_ARCHITECTURE_INFO arch = {};
    hr = device->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &arch, sizeof arch);
    printf("api arch %08lx %d\n", (unsigned long)(unsigned)hr, arch.TileBasedDeferredRenderer);

    for (DXGI_FORMAT f : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R16G16B16A16_FLOAT,
                          DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT,
                          DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC7_UNORM}) {
        UINT bits = 0;
        hr = device->CheckFormatSupport(f, &bits);
        printf("api format %u %08lx %08x\n", (unsigned)f, (unsigned long)(unsigned)hr, bits);
    }
    for (UINT samples : {1u, 2u, 4u, 8u}) {
        UINT levels = 0;
        hr = device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &levels);
        printf("api msaa %u %08lx %u\n", samples, (unsigned long)(unsigned)hr, levels);
    }

    UINT value = 0x1234abcd, back = 0, size = sizeof back;
    CHECK(device->SetPrivateData(kData, sizeof value, &value));
    hr = device->GetPrivateData(kData, &size, &back);
    printf("api privatedata %08lx %u %08x\n", (unsigned long)(unsigned)hr, size, back);

    IDXGIDevice *dxgi = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIFactory1 *factory = nullptr;
    HRESULT h1 = device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    HRESULT h2 = dxgi ? dxgi->GetAdapter(&adapter) : E_FAIL;
    HRESULT h3 = adapter ? adapter->GetParent(__uuidof(IDXGIFactory1), (void **)&factory) : E_FAIL;
    DXGI_ADAPTER_DESC ad = {};
    if (adapter) adapter->GetDesc(&ad);
    printf("api dxgi %08lx %08lx %08lx %04x\n", (unsigned long)(unsigned)h1, (unsigned long)(unsigned)h2,
           (unsigned long)(unsigned)h3, ad.VendorId);
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();

    IUnknown *root = nullptr;
    CHECK(device->QueryInterface(__uuidof(IUnknown), (void **)&root));
    struct { const char *name; IID iid; } ifaces[] = {
        {"IDXGIDevice1", __uuidof(IDXGIDevice1)},     {"ID3D10Device1", __uuidof(ID3D10Device1)},
        {"ID3D11Multithread", __uuidof(ID3D11Multithread)}, {"ID3D11Device1", __uuidof(ID3D11Device1)},
        {"ID3D11Device5", __uuidof(ID3D11Device5)}};
    for (auto &i : ifaces) {
        IUnknown *p = nullptr, *u = nullptr;
        ID3D11Device *again = nullptr;
        hr = device->QueryInterface(i.iid, (void **)&p);
        int same = 0, back_same = 0;
        if (p) {
            p->QueryInterface(__uuidof(IUnknown), (void **)&u);
            same = u == root;
            p->QueryInterface(__uuidof(ID3D11Device), (void **)&again);
            back_same = again == device;
        }
        printf("api identity %s %08lx %d %d\n", i.name, (unsigned long)(unsigned)hr, same, back_same);
        if (again) again->Release();
        if (u) u->Release();
        if (p) p->Release();
    }
    root->Release();

    ID3D11Texture2D *t = dev.Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    IDXGIResource *res = nullptr;
    DXGI_USAGE usage = 0;
    hr = t->QueryInterface(__uuidof(IDXGIResource), (void **)&res);
    if (res) res->GetUsage(&usage), res->Release();
    printf("api usage %08lx %08x\n", (unsigned long)(unsigned)hr, (unsigned)usage);
    t->Release();
    return 0;
}
