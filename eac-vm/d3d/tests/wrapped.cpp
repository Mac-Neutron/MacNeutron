// Another layer's wrappers of forwarded objects (2026-10-19: Palworld's NVIDIA Streamline handed its own command queue
// to CreateSwapChainForHwnd, which the guest refused as not forwarded, and Unreal took the E_INVALIDARG for a GPU
// crash). The wrapper answers IUnknown and passes every other query to the object it wraps, as Streamline's do; given
// to D3D12CreateDevice as the adapter, natively DXMT queries it and through the loopback the guest finds the forwarded
// adapter in it (wrapped.sh). With the argument "foreign" it wraps nothing: the loopback only (natively DXMT would use
// the null its query gives), refused. Prints "wrapped ..." lines.
#include "d3d12_common.hpp"
#include <dxgi1_4.h>

struct Wrapper : IUnknown {
    IUnknown *base;
    explicit Wrapper(IUnknown *b) : base(b) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override {
        if (riid == __uuidof(IUnknown)) return *out = this, S_OK;
        if (!base) return *out = nullptr, E_NOINTERFACE;
        return base->QueryInterface(riid, out);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; } // on the stack: never freed
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
};

int main(int argc, char **argv) {
    bool foreign = argc > 1 && !strcmp(argv[1], "foreign");
    IDXGIFactory4 *factory;
    IDXGIAdapter *adapter;
    CHECK(CreateDXGIFactory2(0, __uuidof(IDXGIFactory4), (void **)&factory));
    CHECK(factory->EnumAdapters(0, &adapter));
    Wrapper wrapped(foreign ? nullptr : adapter);
    ID3D12Device *device = nullptr;
    HRESULT hr = D3D12CreateDevice(&wrapped, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device);
    printf("wrapped device %08lx %d\n", (unsigned long)(unsigned)hr, device != nullptr);
    if (device) device->Release();
    return 0;
}
