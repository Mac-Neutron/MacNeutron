// d3d11_dxgi's dxgia family: the adapter (d3d11_dxgi.cpp has the harness).

static const struct { const char *name; GUID iid; } kAdapterIids[] = {
    {"IUnknown", __uuidof(IUnknown)},           {"IDXGIObject", __uuidof(IDXGIObject)},
    {"IDXGIAdapter", __uuidof(IDXGIAdapter)},   {"IDXGIAdapter1", __uuidof(IDXGIAdapter1)},
    {"IDXGIAdapter2", __uuidof(IDXGIAdapter2)}, {"IDXGIAdapter3", __uuidof(IDXGIAdapter3)},
    {"IDXGIAdapter4", __uuidof(IDXGIAdapter4)}, {"IMTLDXGIAdapter", IID_MTLDXGIAdapter},
    {"wine", IID_WineAdapter},
};
// A case's adapter, with the factory that made it.
struct Ad {
    P<IDXGIFactory6> f;
    P<IMTLDXGIAdapterP> a;
    Ad() : f(NewFactory()), a(NewAdapter(f)) {}
};
static uint64_t Budget(IMTLDXGIAdapterP *a) {
    DXGI_QUERY_VIDEO_MEMORY_INFO q = {};
    a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &q);
    return q.Budget;
}

// A GetDesc* answer: the Description (the device's name, or exactly), the IDs, memory and LUID by relation (P5).
template <class T> static std::string DescKv(IMTLDXGIAdapterP *a, const Two &t) {
    T d = t.Get<T>();
    WCHAR name[128];
    bool haveName = DeviceName(a->GetMTLDevice(), name);
    std::string desc = haveName && !memcmp(name, d.Description, sizeof name) ? std::string("device-name") : W(d.Description, 128);
    bool nul = false;
    for (int i = 0; i < 128; i++) nul |= d.Description[i] == 0;
    uint64_t budget = Budget(a), vram = (uint64_t)d.DedicatedVideoMemory;
    const char *vr = vram == budget / 2 ? "half-budget"
                     : sizeof(SIZE_T) == 4 && budget / 2 > 0xC0000000ull && vram == 0xC0000000ull ? "clamp"
                                                                                                  : "other";
    uint64_t luid = LuidBits(d.AdapterLuid), want = RegistryLuid(a->GetMTLDevice());
    std::string kv = F("desc=%s nul<128=%d vendor=%04x device=%04x subsys=%x revision=%x vram=%s sysmem=%llu shared=%llu "
                       "luid=%s",
                       desc.c_str(), nul, d.VendorId, d.DeviceId, d.SubSysId, d.Revision, vr,
                       (unsigned long long)d.DedicatedSystemMemory, (unsigned long long)d.SharedSystemMemory,
                       !Winemetal().registryID ? "no-winemetal" : luid == want ? "bswap-registry" : "other");
    if constexpr (!std::is_same_v<T, DXGI_ADAPTER_DESC>) kv += F(" flags=%x", (unsigned)d.Flags);
    if constexpr (std::is_same_v<T, DXGI_ADAPTER_DESC2> || std::is_same_v<T, DXGI_ADAPTER_DESC3>)
        kv += F(" preemption=%u/%u", (unsigned)d.GraphicsPreemptionGranularity, (unsigned)d.ComputePreemptionGranularity);
    return kv + " " + t.WU() + Raw(F("vram=%llu budget=%llu luid=%016llx", (unsigned long long)vram,
                                      (unsigned long long)budget, (unsigned long long)luid));
}
template <class T, class Fn> static void Desc(Fn call, size_t off = 0) {
    Ad x;
    if (!x.a) return;
    Two t(sizeof(T), off);
    HRESULT hr = call(x.a.p, (T *)t.a);
    call(x.a.p, (T *)t.b);
    Out(hr, DescKv<T>(x.a, t));
}

// ---- IUnknown, IDXGIObject ----

CASE("dxgia", K_ADAPTER, 0, "qi", 0) {
    Ad x;
    if (!x.a) return;
    for (auto &k : kAdapterIids) {
        void *p = (void *)kA5Ptr;
        HRESULT hr = x.a->QueryInterface(k.iid, &p);
        std::string kv = F("out=%s", OutPtr(p));
        if (SUCCEEDED(hr) && p && p != kA5Ptr) {
            kv += F(" same=%d ref=%lu", p == (void *)x.a.p, (unsigned long)Refs(x.a));
            ((IUnknown *)p)->Release();
        }
        OutS(k.name, hr, kv);
    }
}
CASE("dxgia", K_ADAPTER, 0, "qi-unknown", 0) {
    Ad x;
    if (!x.a) return;
    void *p = (void *)kA5Ptr, *q = (void *)kA5Ptr;
    HRESULT hr = x.a->QueryInterface(UnknownIid(10), &p);
    HRESULT again = x.a->QueryInterface(UnknownIid(10), &q);
    Out(hr, F("out=%s again=%08x out2=%s", OutPtr(p), (unsigned)again, OutPtr(q)));
}
CASE("dxgia", K_ADAPTER, 0, "qi-null-out", 0) {
    Ad x;
    if (!x.a) return;
    Out(x.a->QueryInterface(__uuidof(IDXGIAdapter), nullptr));
}
CASE("dxgia", K_ADAPTER, 0, "qi-null-riid", ISOLATE | NOT_ON_C) {
    Ad x;
    if (!x.a) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(x.a.p, 0, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgia", K_ADAPTER, 1, "refs", 0) {
    P<IDXGIFactory6> f(NewFactory());
    IMTLDXGIAdapterP *a = NewAdapter(f);
    if (!a) return;
    ULONG fref = Refs(f);
    ULONG add = a->AddRef(), rel = a->Release(), last = a->Release();
    Out(S_OK, F("addref=%lu release=%lu last=%lu factoryref=%lu back=%lu", (unsigned long)add, (unsigned long)rel,
                (unsigned long)last, (unsigned long)fref, (unsigned long)Refs(f)));
}
CASE("dxgia", K_ADAPTER, 2, "release", 0) {
    P<IDXGIFactory6> f(NewFactory());
    IMTLDXGIAdapterP *a = NewAdapter(f);
    if (!a) return;
    a->AddRef();
    ULONG r1 = a->Release(), r2 = a->Release();
    Out(S_OK, F("release=%lu,%lu", (unsigned long)r1, (unsigned long)r2));
}
static const bool kAdapterPdata = RegisterPdata("dxgia", K_ADAPTER, MakeAdapter);
CASE("dxgia", K_ADAPTER, 6, "getparent", 0) {
    Ad x;
    if (!x.a) return;
    void *p = (void *)kA5Ptr;
    ULONG before = Refs(x.f);
    HRESULT hr = x.a->GetParent(__uuidof(IDXGIFactory6), &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        kv += F(" same=%d factoryref=%+ld", p == (void *)x.f.p, (long)Refs(x.f) - (long)before);
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgia", K_ADAPTER, 6, "getparent-unknown", 0) {
    Ad x;
    if (!x.a) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = x.a->GetParent(UnknownIid(11), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgia", K_ADAPTER, 6, "getparent-null-out", 0) {
    Ad x;
    if (!x.a) return;
    Out(x.a->GetParent(__uuidof(IDXGIFactory), nullptr));
}
CASE("dxgia", K_ADAPTER, 6, "getparent-null-riid", ISOLATE | NOT_ON_C) {
    Ad x;
    if (!x.a) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(x.a.p, 6, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}

// ---- The descriptions, in every child (the config keys, NVEXT and the profiles change them) ----

CASE("dxgia", K_ADAPTER, 8, "desc", E_ALL) {
    Desc<DXGI_ADAPTER_DESC>([](IMTLDXGIAdapterP *a, DXGI_ADAPTER_DESC *d) { return a->GetDesc(d); });
}
CASE("dxgia", K_ADAPTER, 10, "desc", E_ALL) {
    Desc<DXGI_ADAPTER_DESC1>([](IMTLDXGIAdapterP *a, DXGI_ADAPTER_DESC1 *d) { return a->GetDesc1(d); });
}
CASE("dxgia", K_ADAPTER, 11, "desc", E_ALL) {
    Desc<DXGI_ADAPTER_DESC2>([](IMTLDXGIAdapterP *a, DXGI_ADAPTER_DESC2 *d) { return a->GetDesc2(d); });
}
CASE("dxgia", K_ADAPTER, 18, "desc", E_ALL) {
    Desc<DXGI_ADAPTER_DESC3>([](IMTLDXGIAdapterP *a, DXGI_ADAPTER_DESC3 *d) { return a->GetDesc3(d); });
}
CASE("dxgia", K_ADAPTER, 10, "misaligned", 0) {
    Desc<DXGI_ADAPTER_DESC1>([](IMTLDXGIAdapterP *a, DXGI_ADAPTER_DESC1 *d) { return a->GetDesc1(d); }, 4);
}
// The four descriptions agree, and two adapters' LUIDs.
CASE("dxgia", K_ADAPTER, 18, "same-all", 0) {
    Ad x;
    if (!x.a) return;
    P<IMTLDXGIAdapterP> b(NewAdapter(x.f));
    if (!b) return;
    DXGI_ADAPTER_DESC d0 = {};
    DXGI_ADAPTER_DESC1 d1 = {};
    DXGI_ADAPTER_DESC2 d2 = {};
    DXGI_ADAPTER_DESC3 d3 = {}, e3 = {};
    x.a->GetDesc(&d0);
    x.a->GetDesc1(&d1);
    x.a->GetDesc2(&d2);
    HRESULT hr = x.a->GetDesc3(&d3);
    b->GetDesc3(&e3);
    bool desc = !memcmp(d0.Description, d3.Description, sizeof d0.Description) &&
                !memcmp(d1.Description, d3.Description, sizeof d0.Description) &&
                !memcmp(d2.Description, d3.Description, sizeof d0.Description);
    uint64_t l = LuidBits(d3.AdapterLuid);
    bool luid = LuidBits(d0.AdapterLuid) == l && LuidBits(d1.AdapterLuid) == l && LuidBits(d2.AdapterLuid) == l;
    Out(hr, F("desc=%s luid=%s adapters=%s", desc ? "same-all" : "differ", luid ? "same-all" : "differ",
              LuidBits(e3.AdapterLuid) == l ? "same-luid" : "differ"));
}
CASE("dxgia", K_ADAPTER, 8, "null", 0) {
    Ad x;
    if (!x.a) return;
    Out(x.a->GetDesc(nullptr), F("desc1=%08x desc2=%08x desc3=%08x", (unsigned)x.a->GetDesc1(nullptr),
                                 (unsigned)x.a->GetDesc2(nullptr), (unsigned)x.a->GetDesc3(nullptr)));
}

// ---- CheckInterfaceSupport ----

CASE("dxgia", K_ADAPTER, 9, "check", 0) {
    Ad x;
    if (!x.a) return;
    static const struct { const char *name; GUID iid; } kIids[] = {
        {"IDXGIDevice", __uuidof(IDXGIDevice)},
        {"ID3D10Device", __uuidof(ID3D10Device)},
        {"ID3D10Device1", __uuidof(ID3D10Device1)},
        {"ID3D11Device", __uuidof(ID3D11Device)},
    };
    for (auto &k : kIids) {
        One b(sizeof(LARGE_INTEGER));
        HRESULT hr = x.a->CheckInterfaceSupport(k.iid, (LARGE_INTEGER *)b.p);
        uint64_t v;
        memcpy(&v, b.p, 8);
        OutS(k.name, hr, b.Untouched(0, 8) ? std::string("umd=untouched") : F("umd=%016llx", (unsigned long long)v));
    }
}
// d3d10core's own call: a null version pointer.
CASE("dxgia", K_ADAPTER, 9, "null-umd", 0) {
    Ad x;
    if (!x.a) return;
    Out(x.a->CheckInterfaceSupport(__uuidof(ID3D10Device), nullptr));
}
CASE("dxgia", K_ADAPTER, 9, "null-iid", ISOLATE | NOT_ON_C) {
    Ad x;
    if (!x.a) return;
    One b(sizeof(LARGE_INTEGER));
    HRESULT hr = Call<HRESULT>(x.a.p, 9, (const GUID *)nullptr, (LARGE_INTEGER *)b.p);
    Out(hr, F("umd=%s", b.Untouched(0, 8) ? "untouched" : "written"));
}

// ---- EnumOutputs ----

CASE("dxgia", K_ADAPTER, 7, "enum", 0) {
    Ad x;
    if (!x.a) return;
    UINT n = 0;
    HRESULT hr = S_OK;
    void *last = nullptr;
    for (;; n++) {
        IDXGIOutput *o = (IDXGIOutput *)kA5Ptr;
        hr = x.a->EnumOutputs(n, &o);
        last = o;
        if (hr != S_OK) break;
        o->Release();
    }
    Out(hr, F("count=%s out=%s", n == Monitors().size() ? "monitors" : "other", OutPtr(last)) + Raw(F("count=%u", n)));
}
CASE("dxgia", K_ADAPTER, 7, "objects", 0) {
    Ad x;
    if (!x.a) return;
    IDXGIOutput *o = nullptr, *p = nullptr;
    ULONG before = Refs(x.a);
    HRESULT hr = x.a->EnumOutputs(0, &o);
    HRESULT hr2 = x.a->EnumOutputs(0, &p);
    if (FAILED(hr) || FAILED(hr2)) return Out(hr, F("second=%08x", (unsigned)hr2));
    std::string kv = F("new=%d ref=%lu adapterref=%+ld", o != p, (unsigned long)Refs(o), (long)Refs(x.a) - (long)before);
    o->Release();
    p->Release();
    Out(hr, kv + F(" back=%+ld", (long)Refs(x.a) - (long)before));
}
CASE("dxgia", K_ADAPTER, 7, "null", 0) {
    Ad x;
    if (!x.a) return;
    Out(x.a->EnumOutputs(0, nullptr));
}

// ---- IDXGIAdapter3 ----

CASE("dxgia", K_ADAPTER, 12, "teardown-event", ISOLATE | NOT_ON_C) {
    Ad x;
    if (!x.a) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = x.a->RegisterHardwareContentProtectionTeardownStatusEvent((HANDLE)(uintptr_t)0x1234, &cookie);
    Out(hr, F("cookie=%s", cookie == 0xa5a5a5a5 ? "untouched" : "written"));
}
CASE("dxgia", K_ADAPTER, 13, "teardown-unregister", 0) {
    Ad x;
    if (!x.a) return;
    x.a->UnregisterHardwareContentProtectionTeardownStatus(7);
    OutV();
}
static std::string MemKv(const Two &t) {
    DXGI_QUERY_VIDEO_MEMORY_INFO q = t.Get<DXGI_QUERY_VIDEO_MEMORY_INFO>();
    return F("budget>0=%d avail=%llu reservation=%llx ", q.Budget > 0, (unsigned long long)q.AvailableForReservation,
             (unsigned long long)q.CurrentReservation) +
           t.WU() + Raw(F("budget=%llu usage=%llu", (unsigned long long)q.Budget, (unsigned long long)q.CurrentUsage));
}
CASE("dxgia", K_ADAPTER, 14, "query", 0) {
    Ad x;
    if (!x.a) return;
    for (int g : {0, 1}) {
        Two t(sizeof(DXGI_QUERY_VIDEO_MEMORY_INFO));
        HRESULT hr = x.a->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)g, (DXGI_QUERY_VIDEO_MEMORY_INFO *)t.a);
        x.a->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)g, (DXGI_QUERY_VIDEO_MEMORY_INFO *)t.b);
        OutS(g ? "non-local" : "local", hr, MemKv(t));
    }
}
CASE("dxgia", K_ADAPTER, 14, "bad", 0) {
    Ad x;
    if (!x.a) return;
    One b(sizeof(DXGI_QUERY_VIDEO_MEMORY_INFO));
    HRESULT node = x.a->QueryVideoMemoryInfo(1, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, (DXGI_QUERY_VIDEO_MEMORY_INFO *)b.p);
    HRESULT group = x.a->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)7, (DXGI_QUERY_VIDEO_MEMORY_INFO *)b.p);
    HRESULT null = x.a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, nullptr);
    Out(node, F("group7=%08x null=%08x buffer=%s", (unsigned)group, (unsigned)null, b.Untouched(0, b.n) ? "untouched" : "written"));
}
CASE("dxgia", K_ADAPTER, 14, "misaligned", 0) {
    Ad x;
    if (!x.a) return;
    Two t(sizeof(DXGI_QUERY_VIDEO_MEMORY_INFO), 4);
    HRESULT hr = x.a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, (DXGI_QUERY_VIDEO_MEMORY_INFO *)t.a);
    x.a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, (DXGI_QUERY_VIDEO_MEMORY_INFO *)t.b);
    Out(hr, MemKv(t));
}
// Reservations above 2^32 (a UINT64 argument: a 32-bit slot can't pass them), each read back exactly.
CASE("dxgia", K_ADAPTER, 15, "reserve", 0) {
    Ad x;
    if (!x.a) return;
    static const uint64_t kRes[2] = {0x0000000180001000ull, 0x0000000200002000ull};
    for (int g : {0, 1}) {
        HRESULT hr = x.a->SetVideoMemoryReservation(0, (DXGI_MEMORY_SEGMENT_GROUP)g, kRes[g]);
        DXGI_QUERY_VIDEO_MEMORY_INFO q = {}, other = {};
        x.a->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)g, &q);
        x.a->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)(1 - g), &other);
        OutS(g ? "non-local" : "local", hr,
             F("read=%016llx other=%016llx", (unsigned long long)q.CurrentReservation,
               (unsigned long long)other.CurrentReservation));
    }
}
CASE("dxgia", K_ADAPTER, 15, "bad", 0) {
    Ad x;
    if (!x.a) return;
    HRESULT node = x.a->SetVideoMemoryReservation(1, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 4096);
    HRESULT group = x.a->SetVideoMemoryReservation(0, (DXGI_MEMORY_SEGMENT_GROUP)7, 4096);
    DXGI_QUERY_VIDEO_MEMORY_INFO q = {};
    x.a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &q);
    Out(node, F("group7=%08x local=%llx", (unsigned)group, (unsigned long long)q.CurrentReservation));
}
CASE("dxgia", K_ADAPTER, 16, "cookie", 0) {
    Ad x;
    if (!x.a) return;
    DWORD cookie = 0xa5a5a5a5;
    HRESULT hr = x.a->RegisterVideoMemoryBudgetChangeNotificationEvent((HANDLE)(uintptr_t)0x1234, &cookie);
    Out(hr, F("cookie=%lx", (unsigned long)cookie));
}
CASE("dxgia", K_ADAPTER, 16, "null-cookie", ISOLATE | NOT_ON_C) {
    Ad x;
    if (!x.a) return;
    Out(x.a->RegisterVideoMemoryBudgetChangeNotificationEvent((HANDLE)(uintptr_t)0x1234, nullptr));
}
CASE("dxgia", K_ADAPTER, 17, "unregister", 0) {
    Ad x;
    if (!x.a) return;
    x.a->UnregisterVideoMemoryBudgetChangeNotification(0);
    OutV();
}

// ---- The private slots ----

CASE("dxgia", K_ADAPTER, 19, "device", 0) {
    Ad x;
    if (!x.a) return;
    P<IMTLDXGIAdapterP> b(NewAdapter(x.f));
    if (!b) return;
    uint64_t d = x.a->GetMTLDevice(), e = x.a->GetMTLDevice(), other = b->GetMTLDevice();
    Out(S_OK, F("nonzero=%d stable=%d adapters=%s", d != 0, d == e, d == other ? "same" : "differ"));
}
CASE("dxgia", K_ADAPTER, 20, "kmt", 0) {
    Ad x;
    if (!x.a) return;
    UINT h = x.a->GetLocalD3DKMT();
    DXGI_ADAPTER_DESC d = {};
    x.a->GetDesc(&d);
    // The handle is one D3DKMT knows, and it is there exactly when the probe's own open of the LUID succeeds.
    Out(S_OK, F("kmt=%s opens=%d", h == 0 ? "0" : KmtValid(h) ? "valid" : "invalid", KmtOpens(d.AdapterLuid)));
}

// ---- Threads: counts and queries from four threads at once ----

CASE("dxgia", K_ADAPTER, 14, "threads", 0) {
    Ad x;
    if (!x.a) return;
    ULONG before = Refs(x.a);
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++)
        threads.emplace_back([&x, &failed] {
            for (int k = 0; k < 10000; k++) {
                x.a->AddRef();
                DXGI_QUERY_VIDEO_MEMORY_INFO q;
                if (x.a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &q) != S_OK) failed++;
                x.a->Release();
            }
        });
    for (auto &t : threads) t.join();
    Out(S_OK, F("count=%s failed=%d", Refs(x.a) == before ? "unchanged" : "changed", failed.load()));
}
