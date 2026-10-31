// d3d11_dxgi's d3d10c family: d3d10core's two exports (d3d11_dxgi.cpp has the harness). D3D10CoreCreateDevice asks
// the adapter about ID3D10Device, makes a D3D11 device with DXMT's private D3D10 flag and answers its ID3D10Device.

typedef HRESULT(WINAPI *CreateD3D10Fn)(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL, ID3D10Device **);
typedef HRESULT(WINAPI *RegisterLayersFn)();
static CreateD3D10Fn D3D10Create() {
#ifdef _WIN32
    return (CreateD3D10Fn)(void *)GetProcAddress(LoadLibraryA("d3d10core.dll"), "D3D10CoreCreateDevice");
#else
    return D3D10CoreCreateDevice;
#endif
}
static RegisterLayersFn D3D10Layers() {
#ifdef _WIN32
    return (RegisterLayersFn)(void *)GetProcAddress(LoadLibraryA("d3d10core.dll"), "D3D10CoreRegisterLayers");
#else
    return D3D10CoreRegisterLayers;
#endif
}

// A device made on adapter 0: the identities, its flags, multithread protection and feature level.
static void D3D10Device(UINT flags, D3D_FEATURE_LEVEL fl) {
    CreateD3D10Fn create = D3D10Create();
    if (!create) return Fatal("D3D10CoreCreateDevice missing", E_FAIL);
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a(NewAdapter(f));
    if (!a) return;
    ID3D10Device *d = (ID3D10Device *)kA5Ptr;
    HRESULT hr = create(f, a, flags, fl, &d);
    if (FAILED(hr) || !d || d == kA5Ptr) return Out(hr, F("out=%s", OutPtr(d)));
    P<IUnknown> u, u1, u11;
    P<ID3D10Device1> d1;
    P<ID3D11Device> d11;
    P<ID3D10Multithread> mt;
    d->QueryInterface(__uuidof(IUnknown), u.pv());
    HRESULT h1 = d->QueryInterface(__uuidof(ID3D10Device1), d1.pv());
    HRESULT h11 = d->QueryInterface(__uuidof(ID3D11Device), d11.pv());
    if (d1) d1->QueryInterface(__uuidof(IUnknown), u1.pv());
    if (d11) d11->QueryInterface(__uuidof(IUnknown), u11.pv());
    d->QueryInterface(__uuidof(ID3D10Multithread), mt.pv());
    std::string kv = F("d3d10.1=%08x d3d11=%08x iunknown=%s flags=%x mt=%d", (unsigned)h1, (unsigned)h11,
                       u && u.p == u1.p && u.p == u11.p ? "same" : "differ", d->GetCreationFlags(),
                       mt ? (int)mt->GetMultithreadProtected() : -1);
    kv += F(" fl=%s", !d1 ? "none" : d1->GetFeatureLevel() == (D3D10_FEATURE_LEVEL1)fl ? "asked" : F("%x", (unsigned)d1->GetFeatureLevel()).c_str());
    u.reset(), u1.reset(), u11.reset(), d1.reset(), d11.reset(), mt.reset();
    kv += F(" ref=%lu", (unsigned long)Refs(d));
    d->Release();
    Out(hr, kv);
}
CASE("d3d10c", K_D3D10CORE, 0, "flags0", 0) { D3D10Device(0, D3D_FEATURE_LEVEL_10_0); }
CASE("d3d10c", K_D3D10CORE, 0, "singlethreaded", 0) { D3D10Device(D3D10_CREATE_DEVICE_SINGLETHREADED, D3D_FEATURE_LEVEL_10_0); }
// d2d1's flags (B2): D3D10_CREATE_DEVICE_BGRA_SUPPORT passes through.
CASE("d3d10c", K_D3D10CORE, 0, "bgra", 0) { D3D10Device(D3D10_CREATE_DEVICE_BGRA_SUPPORT, D3D_FEATURE_LEVEL_10_0); }
CASE("d3d10c", K_D3D10CORE, 0, "fl10_1", 0) { D3D10Device(0, D3D_FEATURE_LEVEL_10_1); }
// An adapter that refuses ID3D10Device: what d3d10core asked (the IID, a null version pointer) and its answer.
CASE("d3d10c", K_D3D10CORE, 0, "fake-adapter", 0) {
    CreateD3D10Fn create = D3D10Create();
    if (!create) return Fatal("D3D10CoreCreateDevice missing", E_FAIL);
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeAdapter fake;
    ID3D10Device *d = (ID3D10Device *)kA5Ptr;
    HRESULT hr = create(f, &fake, 0, D3D_FEATURE_LEVEL_10_0, &d);
    Out(hr, F("asked=%s umd=%s out=%s adapterref=%+ld", fake.asked.c_str(), fake.umd ? "set" : "null", OutPtr(d),
              fake.refs.load() - 1));
}
// A null ppDevice (B12): the device is made and released, then the QI answers E_POINTER.
CASE("d3d10c", K_D3D10CORE, 0, "null-out", 0) {
    CreateD3D10Fn create = D3D10Create();
    if (!create) return Fatal("D3D10CoreCreateDevice missing", E_FAIL);
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a(NewAdapter(f));
    if (!a) return;
    ULONG before = Refs(a);
    HRESULT hr = create(f, a, 0, D3D_FEATURE_LEVEL_10_0, nullptr);
    Out(hr, F("adapterref=%+ld", (long)Refs(a) - (long)before));
}
CASE("d3d10c", K_D3D10CORE, 0, "null-adapter", ISOLATE | NOT_ON_C) {
    CreateD3D10Fn create = D3D10Create();
    if (!create) return Fatal("D3D10CoreCreateDevice missing", E_FAIL);
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    ID3D10Device *d = (ID3D10Device *)kA5Ptr;
    HRESULT hr = create(f, nullptr, 0, D3D_FEATURE_LEVEL_10_0, &d);
    Out(hr, F("out=%s", OutPtr(d)));
}
CASE("d3d10c", K_D3D10CORE, 1, "stub", 0) {
    RegisterLayersFn layers = D3D10Layers();
    if (!layers) return Fatal("D3D10CoreRegisterLayers missing", E_FAIL);
    Out(layers());
}
