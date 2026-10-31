// d3d11_dxgi's dxgif family: dxgi's exports, DllMain and the factory (d3d11_dxgi.cpp has the harness).

static const struct { const char *name; GUID iid; } kFactoryIids[] = {
    {"IUnknown", __uuidof(IUnknown)},           {"IDXGIObject", __uuidof(IDXGIObject)},
    {"IDXGIFactory", __uuidof(IDXGIFactory)},   {"IDXGIFactory1", __uuidof(IDXGIFactory1)},
    {"IDXGIFactory2", __uuidof(IDXGIFactory2)}, {"IDXGIFactory3", __uuidof(IDXGIFactory3)},
    {"IDXGIFactory4", __uuidof(IDXGIFactory4)}, {"IDXGIFactory5", __uuidof(IDXGIFactory5)},
    {"IDXGIFactory6", __uuidof(IDXGIFactory6)}, {"IDXGIFactory7", IID_Factory7},
};
// An object answered for an IID: the same pointer as its IUnknown, its count once that is Released, its last Release.
static std::string Answered(void *p) {
    IUnknown *u = (IUnknown *)p, *base = nullptr;
    u->QueryInterface(__uuidof(IUnknown), (void **)&base);
    bool same = base == u;
    if (base) base->Release();
    ULONG refs = Refs(u);
    return F(" same=%d ref=%lu last=%lu", same, (unsigned long)refs, (unsigned long)u->Release());
}
static void ExportIids(HRESULT (*create)(const IID &, void **)) {
    for (auto &k : kFactoryIids) {
        void *p = (void *)kA5Ptr;
        HRESULT hr = create(k.iid, &p);
        std::string kv = F("out=%s", OutPtr(p));
        if (SUCCEEDED(hr) && p && p != kA5Ptr) kv += Answered(p);
        OutS(k.name, hr, kv);
    }
}
static HRESULT Create0(const IID &iid, void **p) { return CreateDXGIFactory(iid, p); }
static HRESULT Create1(const IID &iid, void **p) { return CreateDXGIFactory1(iid, p); }
static HRESULT Create2(const IID &iid, void **p) { return CreateDXGIFactory2(0, iid, p); }

// ---- Exports ----

CASE("dxgif", K_DXGI, 0, "iids", 0) { ExportIids(Create0); }
CASE("dxgif", K_DXGI, 0, "null-out", 0) { Out(CreateDXGIFactory(__uuidof(IDXGIFactory), nullptr)); }
CASE("dxgif", K_DXGI, 0, "null-riid", ISOLATE | NOT_ON_C) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = ((HRESULT(WINAPI *)(const IID *, void **))(void *)&CreateDXGIFactory)(nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_DXGI, 1, "iids", 0) { ExportIids(Create1); }
CASE("dxgif", K_DXGI, 1, "null-out", 0) { Out(CreateDXGIFactory1(__uuidof(IDXGIFactory1), nullptr)); }
CASE("dxgif", K_DXGI, 1, "null-riid", ISOLATE | NOT_ON_C) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = ((HRESULT(WINAPI *)(const IID *, void **))(void *)&CreateDXGIFactory1)(nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_DXGI, 2, "iids", 0) { ExportIids(Create2); }
CASE("dxgif", K_DXGI, 2, "null-out", 0) { Out(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), nullptr)); }
CASE("dxgif", K_DXGI, 2, "count", 0) {
    IDXGIFactory2 *f = nullptr;
    HRESULT hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void **)&f);
    if (FAILED(hr) || !f) return Out(hr);
    ULONG add = f->AddRef(), rel = f->Release();
    Out(hr, F("addref=%lu release=%lu last=%lu", (unsigned long)add, (unsigned long)rel, (unsigned long)f->Release()));
}
CASE("dxgif", K_DXGI, 2, "flags", 0) {
    for (UINT flags : {0u, 1u}) {
        IDXGIFactory3 *f = nullptr;
        HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory3), (void **)&f);
        UINT got = f ? f->GetCreationFlags() : 0xdeadu;
        if (f) f->Release();
        OutS(F("%u", flags).c_str(), hr, F("creation-flags=%u", got));
    }
}
CASE("dxgif", K_DXGI, 2, "null-riid", ISOLATE | NOT_ON_C) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = ((HRESULT(WINAPI *)(UINT, const IID *, void **))(void *)&CreateDXGIFactory2)(0, nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_DXGI, 3, "twice", 0) {
    HRESULT first = DXGIDeclareAdapterRemovalSupport();
    HRESULT second = DXGIDeclareAdapterRemovalSupport();
    Out(first, F("second=%08x", (unsigned)second));
}
// The NVEXT GUID: S_OK in the nvext child, E_NOINTERFACE otherwise; the out pointer is never written.
CASE("dxgif", K_DXGI, 4, "nvext", E_DEFAULT | E_NVEXT) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = DXGIGetDebugInterface1(0, IID_NvExt, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_DXGI, 4, "foreign", 0) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = DXGIGetDebugInterface1(0, __uuidof(IDXGIFactory), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_DXGI, 4, "null-riid", ISOLATE | NOT_ON_C) {
    void *p = (void *)kA5Ptr;
    HRESULT hr = ((HRESULT(WINAPI *)(UINT, const IID *, void **))(void *)&DXGIGetDebugInterface1)(0, nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
// What DllMain wrote or deleted in the registry (dxgi.cpp:19-51): with NVEXT two REG_DWORD 1 and NGXCore's FullPath.
CASE("dxgif", K_DXGI, 5, "registry", PE_ONLY | E_DEFAULT | E_NVEXT) {
#ifdef _WIN32
    auto value = [](const wchar_t *key, const wchar_t *name) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &k)) return std::string("absent");
        BYTE buf[256];
        DWORD type = 0, size = sizeof buf;
        LONG r = RegQueryValueExW(k, name, nullptr, &type, buf, &size);
        RegCloseKey(k);
        if (r) return std::string("absent");
        std::string s = F("%lu/%lu/", (unsigned long)type, (unsigned long)size);
        for (DWORD i = 0; i < size; i++) s += F("%02x", buf[i]);
        return s;
    };
    const wchar_t *nv = L"{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}";
    Out(S_OK, "global=" + value(L"SOFTWARE\\NVIDIA Corporation\\Global", nv) +
                  " nvlddmkm=" + value(L"SYSTEM\\ControlSet001\\Services\\nvlddmkm", nv) +
                  " ngxcore=" + value(L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath"));
#endif
}
// Threads made and joined after the load: DllMain may run per thread (Rust's TLS); nothing may block or deadlock.
CASE("dxgif", K_DXGI, 5, "threads", 0) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) threads.emplace_back([] {});
    for (auto &t : threads) t.join();
    Out(S_OK, "threads=8 ok");
}

// ---- The factory: IUnknown ----

CASE("dxgif", K_FACTORY, 0, "qi", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    for (auto &k : kFactoryIids) {
        void *p = (void *)kA5Ptr;
        HRESULT hr = f->QueryInterface(k.iid, &p);
        std::string kv = F("out=%s", OutPtr(p));
        if (SUCCEEDED(hr) && p && p != kA5Ptr) {
            kv += F(" same=%d ref=%lu", p == (void *)f.p, (unsigned long)Refs(f));
            ((IUnknown *)p)->Release();
        }
        OutS(k.name, hr, kv);
    }
}
CASE("dxgif", K_FACTORY, 0, "qi-unknown", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr, *q = (void *)kA5Ptr;
    HRESULT hr = f->QueryInterface(UnknownIid(1), &p);
    HRESULT again = f->QueryInterface(UnknownIid(1), &q);
    Out(hr, F("out=%s again=%08x out2=%s ref=%lu", OutPtr(p), (unsigned)again, OutPtr(q), (unsigned long)Refs(f)));
}
CASE("dxgif", K_FACTORY, 0, "qi-null-out", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->QueryInterface(__uuidof(IDXGIFactory), nullptr), F("ref=%lu", (unsigned long)Refs(f)));
}
CASE("dxgif", K_FACTORY, 0, "qi-misaligned", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    std::vector<uint8_t> raw;
    GUID iid = __uuidof(IDXGIFactory1);
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 0, (const IID *)Aligned4(raw, &iid, sizeof iid), &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        kv += F(" same=%d", p == (void *)f.p);
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgif", K_FACTORY, 0, "qi-null-riid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 0, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 1, "addref", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    ULONG a = f->AddRef(), b = f->AddRef();
    f->Release();
    f->Release();
    Out(S_OK, F("addref=%lu,%lu", (unsigned long)a, (unsigned long)b));
}
CASE("dxgif", K_FACTORY, 2, "release", 0) {
    IDXGIFactory6 *f = NewFactory();
    if (!f) return;
    f->AddRef();
    ULONG a = f->Release(), b = f->Release();
    Out(S_OK, F("release=%lu,%lu", (unsigned long)a, (unsigned long)b));
}

// ---- IDXGIObject ----

static const bool kFactoryPdata = RegisterPdata("dxgif", K_FACTORY, MakeFactory);
CASE("dxgif", K_FACTORY, 5, "pdata-misaligned", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    f->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    One b(16, 4);
    UINT size = 16;
    HRESULT hr = f->GetPrivateData(kPd1, &size, b.p);
    Out(hr, F("size=%u same=%d", size, !memcmp(b.p, kPdData, 12)));
}
CASE("dxgif", K_FACTORY, 5, "pdata-null-guid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    uint8_t b[16];
    UINT size = 16;
    HRESULT hr = Call<HRESULT>(f.p, 5, (const GUID *)nullptr, &size, (void *)b);
    Out(hr, F("size=%u", size));
}
CASE("dxgif", K_FACTORY, 3, "pdata-null-guid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(Call<HRESULT>(f.p, 3, (const GUID *)nullptr, (UINT)sizeof kPdData, (const void *)kPdData));
}
CASE("dxgif", K_FACTORY, 4, "pdata-null-guid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeUnk x('x');
    HRESULT hr = Call<HRESULT>(f.p, 4, (const GUID *)nullptr, (const IUnknown *)&x);
    Out(hr, F("x=%ld", x.refs.load()));
}
CASE("dxgif", K_FACTORY, 6, "getparent", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->GetParent(__uuidof(IDXGIFactory), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 6, "getparent-null-out", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->GetParent(__uuidof(IUnknown), nullptr));
}
CASE("dxgif", K_FACTORY, 6, "getparent-null-riid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 6, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}

// ---- Adapters: EnumAdapters, EnumAdapters1, ByLuid, Warp, ByGpuPreference ----

// An adapter answered: a new object per call, its count, the factory's count while held and after.
static std::string Adapters(IDXGIFactory6 *f, IUnknown *a, IUnknown *b) {
    ULONG fref = Refs(f);
    std::string s = F("new=%d ref=%lu factoryref=%lu", a != b, (unsigned long)Refs(a), (unsigned long)fref);
    a->Release();
    b->Release();
    return s + F(" back=%lu", (unsigned long)Refs(f));
}
static UINT AdapterCount(IDXGIFactory6 *f) {
    UINT n = 0;
    for (;; n++) {
        IDXGIAdapter1 *a = nullptr;
        if (f->EnumAdapters1(n, &a) != S_OK) break;
        a->Release();
    }
    return n;
}
static LUID AdapterLuid(IUnknown *a) {
    DXGI_ADAPTER_DESC d = {};
    ((IDXGIAdapter *)a)->GetDesc(&d);
    return d.AdapterLuid;
}
CASE("dxgif", K_FACTORY, 7, "enum0", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    IDXGIAdapter *a = nullptr, *b = nullptr;
    HRESULT hr = f->EnumAdapters(0, &a);
    HRESULT hr2 = f->EnumAdapters(0, &b);
    if (FAILED(hr) || FAILED(hr2)) return Out(hr, F("second=%08x", (unsigned)hr2));
    Out(hr, Adapters(f, a, b));
}
CASE("dxgif", K_FACTORY, 7, "past-last", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    UINT n = 0;
    HRESULT hr = S_OK;
    void *p = nullptr;
    for (;; n++) {
        IDXGIAdapter *a = (IDXGIAdapter *)kA5Ptr;
        hr = f->EnumAdapters(n, &a);
        p = a;
        if (hr != S_OK) break;
        a->Release();
    }
    Out(hr, F("notfound-at=%u out=%s", n, OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 7, "null-out", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->EnumAdapters(0, nullptr));
}
CASE("dxgif", K_FACTORY, 12, "enum0", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    IDXGIAdapter1 *a = nullptr, *b = nullptr;
    HRESULT hr = f->EnumAdapters1(0, &a);
    HRESULT hr2 = f->EnumAdapters1(0, &b);
    if (FAILED(hr) || FAILED(hr2)) return Out(hr, F("second=%08x", (unsigned)hr2));
    Out(hr, Adapters(f, a, b));
}
CASE("dxgif", K_FACTORY, 12, "past-last", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    UINT n = AdapterCount(f);
    IDXGIAdapter1 *a = (IDXGIAdapter1 *)kA5Ptr;
    HRESULT hr = f->EnumAdapters1(n, &a);
    Out(hr, F("notfound-at=%u out=%s", n, OutPtr(a)));
}
CASE("dxgif", K_FACTORY, 12, "null-out-past", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->EnumAdapters1(AdapterCount(f), nullptr));
}
CASE("dxgif", K_FACTORY, 12, "null-out", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    HRESULT hr = f->EnumAdapters1(0, nullptr);
    Out(hr, F("factoryref=%lu", (unsigned long)Refs(f)));
}
CASE("dxgif", K_FACTORY, 26, "own", 0) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    LUID luid = AdapterLuid(a0);
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        kv += F(" new=%d luid=%s ref=%lu", p != (void *)a0.p,
                LuidBits(AdapterLuid((IUnknown *)p)) == LuidBits(luid) ? "same" : "other",
                (unsigned long)Refs((IUnknown *)p));
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgif", K_FACTORY, 26, "wrong", 0) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    LUID luid = AdapterLuid(a0);
    luid.LowPart ^= 0x5a5a5a5a;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter), &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        luid.LowPart ^= 0x5a5a5a5a;
        kv += F(" luid=%s", LuidBits(AdapterLuid((IUnknown *)p)) == LuidBits(luid) ? "adapter0" : "other");
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgif", K_FACTORY, 26, "unknown-iid", 0) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumAdapterByLuid(AdapterLuid(a0), UnknownIid(2), &p);
    Out(hr, F("out=%s factoryref=%lu", OutPtr(p), (unsigned long)Refs(f)));
}
CASE("dxgif", K_FACTORY, 26, "null-riid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 26, AdapterLuid(a0), (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 27, "warp", 0) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumWarpAdapter(__uuidof(IDXGIAdapter1), &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        kv += F(" luid=%s ref=%lu", LuidBits(AdapterLuid((IUnknown *)p)) == LuidBits(AdapterLuid(a0)) ? "adapter0" : "other",
                (unsigned long)Refs((IUnknown *)p));
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgif", K_FACTORY, 27, "unknown-iid", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumWarpAdapter(UnknownIid(3), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 27, "null-riid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 27, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgif", K_FACTORY, 29, "preference", 0) {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a0(NewAdapter(f));
    if (!a0) return;
    for (int pref : {0, 1, 2}) {
        void *p = (void *)kA5Ptr;
        HRESULT hr = f->EnumAdapterByGpuPreference(0, (DXGI_GPU_PREFERENCE)pref, __uuidof(IDXGIAdapter1), &p);
        std::string kv = F("out=%s", OutPtr(p));
        if (SUCCEEDED(hr) && p && p != kA5Ptr) {
            kv += F(" luid=%s", LuidBits(AdapterLuid((IUnknown *)p)) == LuidBits(AdapterLuid(a0)) ? "adapter0" : "other");
            ((IUnknown *)p)->Release();
        }
        OutS(F("%d", pref).c_str(), hr, kv);
    }
}
CASE("dxgif", K_FACTORY, 29, "unknown-iid", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_UNSPECIFIED, UnknownIid(4), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
// Adapter 0, so the C++ reaches the adapter's QueryInterface with the null riid; P20: *pp is never nulled.
CASE("dxgif", K_FACTORY, 29, "null-riid", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 29, (UINT)0, DXGI_GPU_PREFERENCE_UNSPECIFIED, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
// Past the last: NOT_FOUND with the out pointer left as it was (no InitReturnPtr, dxgi_factory.cpp:315-326).
CASE("dxgif", K_FACTORY, 29, "past-last", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = f->EnumAdapterByGpuPreference(AdapterCount(f), DXGI_GPU_PREFERENCE_UNSPECIFIED, __uuidof(IDXGIAdapter), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}

// ---- Window association, software adapters, the flags ----

CASE("dxgif", K_FACTORY, 8, "round-trip", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    HWND before = (HWND)kA5Ptr, after = (HWND)kA5Ptr;
    HRESULT get0 = f->GetWindowAssociation(&before);
    HRESULT hr = f->MakeWindowAssociation(Window(), 0);
    HRESULT get = f->GetWindowAssociation(&after);
    HRESULT clear = f->MakeWindowAssociation(nullptr, 0);
    HWND cleared = (HWND)kA5Ptr;
    f->GetWindowAssociation(&cleared);
    Out(hr, F("before=%08x/%s get=%08x same=%d clear=%08x after=%s", (unsigned)get0, OutPtr(before), (unsigned)get,
              after == Window(), (unsigned)clear, OutPtr(cleared)));
}
CASE("dxgif", K_FACTORY, 8, "flags", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    HRESULT hr = f->MakeWindowAssociation(Window(), DXGI_MWA_NO_WINDOW_CHANGES);
    HWND got = nullptr;
    f->GetWindowAssociation(&got);
    Out(hr, F("same=%d", got == Window()));
}
CASE("dxgif", K_FACTORY, 9, "null-out", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->GetWindowAssociation(nullptr));
}
CASE("dxgif", K_FACTORY, 11, "software", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    IDXGIAdapter *a = (IDXGIAdapter *)kA5Ptr;
    HRESULT hr = f->CreateSoftwareAdapter(SelfModule(), &a);
    Out(hr, F("out=%s", OutPtr(a)));
}
CASE("dxgif", K_FACTORY, 11, "null-out", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->CreateSoftwareAdapter(SelfModule(), nullptr));
}
CASE("dxgif", K_FACTORY, 13, "is-current", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out((HRESULT)f->IsCurrent());
}
CASE("dxgif", K_FACTORY, 14, "stereo", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out((HRESULT)f->IsWindowedStereoEnabled());
}
CASE("dxgif", K_FACTORY, 25, "creation-flags", 0) {
    for (UINT flags : {0u, 1u}) {
        P<IDXGIFactory6> f(NewFactory(flags));
        if (!f) return;
        OutS(F("%u", flags).c_str(), (HRESULT)f->GetCreationFlags());
    }
}

// ---- Swap chains through a fake device (IMTLSwapChainFactory) ----

static DXGI_SWAP_CHAIN_DESC1 Desc1(UINT w, UINT h) {
    DXGI_SWAP_CHAIN_DESC1 d = {};
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc = {1, 0};
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_NONE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    return d;
}
// The factory's part of a swap chain made through the fake: hwnd, the factory it passed, the device's count.
static std::string Made(FakeDevice &dev, IDXGIFactory6 *f, IDXGISwapChain1 *out, long devBefore, bool client) {
    return dev.Got(client) + F(" factory=%s hwnd=%s devref=%+ld out=%s", dev.factory == (IDXGIFactory1 *)f ? "self" : "other",
                               dev.hwnd == Window() ? "same" : "other", dev.refs.load() - devBefore,
                               out == (IDXGISwapChain1 *)(IUnknown *)&dev.chain ? "fake" : OutPtr(out));
}
CASE("dxgif", K_FACTORY, 15, "fake", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    FakeUnk restrict('r');
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(0, 0);
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = f->CreateSwapChainForHwnd(&dev, Window(), &d, nullptr, (IDXGIOutput *)(IUnknown *)&restrict, &sc);
    Out(hr, Made(dev, f, sc, 1, true) + F(" restrict=%s", Events().c_str()));
}
CASE("dxgif", K_FACTORY, 15, "fs-desc", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs = {{50, 1}, DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE, DXGI_MODE_SCALING_CENTERED, FALSE};
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = f->CreateSwapChainForHwnd(&dev, Window(), &d, &fs, nullptr, &sc);
    Out(hr, Made(dev, f, sc, 1, false));
}
CASE("dxgif", K_FACTORY, 15, "misaligned", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    std::vector<uint8_t> raw;
    void *md = Aligned4(raw, &d, sizeof d);
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(f.p, 15, (IUnknown *)&dev, Window(), (const DXGI_SWAP_CHAIN_DESC1 *)md,
                               (const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *)nullptr, (IDXGIOutput *)nullptr, &sc);
    Out(hr, Made(dev, f, sc, 1, false));
}
CASE("dxgif", K_FACTORY, 15, "refused", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev(true);
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = f->CreateSwapChainForHwnd(&dev, Window(), &d, nullptr, nullptr, &sc);
    Out(hr, F("calls=%d asked=%s devref=%+ld out=%s", dev.calls, dev.asked.c_str(), dev.refs.load() - 1, OutPtr(sc)));
}
CASE("dxgif", K_FACTORY, 15, "null-args", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    for (int which = 0; which < 4; which++) {
        IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
        HRESULT hr = f->CreateSwapChainForHwnd(which == 0 ? nullptr : &dev, which == 1 ? nullptr : Window(),
                                               which == 2 ? nullptr : &d, nullptr, nullptr, which == 3 ? nullptr : &sc);
        static const char *const names[] = {"device", "hwnd", "desc", "out"};
        OutS(names[which], hr, F("calls=%d out=%s", dev.calls, which == 3 ? "-" : OutPtr(sc)));
    }
}
CASE("dxgif", K_FACTORY, 10, "translate", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferDesc = {320, 200, {50, 1}, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE,
                    DXGI_MODE_SCALING_CENTERED};
    d.SampleDesc = {1, 0};
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    d.BufferCount = 3;
    d.OutputWindow = Window();
    d.Windowed = FALSE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    IDXGISwapChain *sc = (IDXGISwapChain *)kA5Ptr;
    HRESULT hr = f->CreateSwapChain(&dev, &d, &sc);
    Out(hr, Made(dev, f, (IDXGISwapChain1 *)sc, 1, false));
}
CASE("dxgif", K_FACTORY, 10, "refused", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev(true);
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferDesc = {320, 200, {60, 1}, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
                    DXGI_MODE_SCALING_UNSPECIFIED};
    d.SampleDesc = {1, 0};
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.OutputWindow = Window();
    d.Windowed = TRUE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain *sc = (IDXGISwapChain *)kA5Ptr;
    HRESULT hr = f->CreateSwapChain(&dev, &d, &sc);
    Out(hr, F("calls=%d asked=%s devref=%+ld out=%s", dev.calls, dev.asked.c_str(), dev.refs.load() - 1, OutPtr(sc)));
}
// Null arguments fail before anything is written.
CASE("dxgif", K_FACTORY, 10, "null-args", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferDesc = {320, 200, {60, 1}, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
                    DXGI_MODE_SCALING_UNSPECIFIED};
    d.SampleDesc = {1, 0};
    d.BufferCount = 2;
    d.OutputWindow = Window();
    d.Windowed = TRUE;
    for (int which = 0; which < 3; which++) {
        IDXGISwapChain *sc = (IDXGISwapChain *)kA5Ptr;
        HRESULT hr = f->CreateSwapChain(which == 0 ? nullptr : &dev, which == 1 ? nullptr : &d, which == 2 ? nullptr : &sc);
        static const char *const names[] = {"device", "desc", "out"};
        OutS(names[which], hr, F("calls=%d out=%s", dev.calls, which == 2 ? "-" : OutPtr(sc)));
    }
}
CASE("dxgif", K_FACTORY, 16, "core-window", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    FakeUnk window('w');
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = f->CreateSwapChainForCoreWindow(&dev, &window, &d, nullptr, &sc);
    Out(hr, F("calls=%d out=%s window=%s", dev.calls, OutPtr(sc), Events().c_str()));
}
CASE("dxgif", K_FACTORY, 24, "composition", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    FakeDevice dev;
    DXGI_SWAP_CHAIN_DESC1 d = Desc1(320, 200);
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)kA5Ptr;
    HRESULT hr = f->CreateSwapChainForComposition(&dev, &d, nullptr, &sc);
    Out(hr, F("calls=%d out=%s", dev.calls, OutPtr(sc)));
}

// ---- The stubs ----

CASE("dxgif", K_FACTORY, 17, "luid", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    One b(sizeof(LUID));
    HRESULT hr = f->GetSharedResourceAdapterLuid((HANDLE)(uintptr_t)0x1234, (LUID *)b.p);
    Out(hr, F("luid=%s", b.Untouched(0, sizeof(LUID)) ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 18, "stereo-window", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = f->RegisterStereoStatusWindow(Window(), 0x400, &cookie);
    Out(hr, F("cookie=%s", cookie == 0xa5a5a5a5 ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 19, "stereo-event", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = f->RegisterStereoStatusEvent((HANDLE)(uintptr_t)0x1234, &cookie);
    Out(hr, F("cookie=%s", cookie == 0xa5a5a5a5 ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 20, "unregister-stereo", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    f->UnregisterStereoStatus(7);
    OutV();
}
CASE("dxgif", K_FACTORY, 21, "occlusion-window", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = f->RegisterOcclusionStatusWindow(Window(), 0x400, &cookie);
    Out(hr, F("cookie=%s", cookie == 0xa5a5a5a5 ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 22, "occlusion-event", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = f->RegisterOcclusionStatusEvent((HANDLE)(uintptr_t)0x1234, &cookie);
    Out(hr, F("cookie=%s", cookie == 0xa5a5a5a5 ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 23, "unregister-occlusion", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    f->UnregisterOcclusionStatus(7);
    OutV();
}

// ---- CheckFeatureSupport ----

CASE("dxgif", K_FACTORY, 28, "tearing", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    One b(8);
    HRESULT hr = f->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, b.p, 4);
    BOOL v;
    memcpy(&v, b.p, 4);
    Out(hr, F("value=%d tail=%s", (int)v, b.Untouched(4, 8) ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 28, "size8", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    One b(8);
    HRESULT hr = f->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, b.p, 8);
    Out(hr, F("buffer=%s", b.Untouched(0, 8) ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 28, "feature99", 0) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    One b(8);
    HRESULT hr = f->CheckFeatureSupport((DXGI_FEATURE)99, b.p, 4);
    Out(hr, F("buffer=%s", b.Untouched(0, 8) ? "untouched" : "written"));
}
CASE("dxgif", K_FACTORY, 28, "null-data", ISOLATE | NOT_ON_C) {
    P<IDXGIFactory6> f(NewFactory());
    if (!f) return;
    Out(f->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, nullptr, 4));
}
