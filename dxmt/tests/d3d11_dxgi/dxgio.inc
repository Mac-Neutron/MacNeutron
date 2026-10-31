// d3d11_dxgi's dxgio family: the output (d3d11_dxgi.cpp has the harness). Output 0 is the first monitor
// EnumDisplayMonitors gives (natively the first fake display); its values are printed as relations to what the probe
// reads itself (GetMonitorInfoW, EnumDisplaySettingsW; natively the fakes).

static const struct { const char *name; GUID iid; } kOutputIids[] = {
    {"IUnknown", __uuidof(IUnknown)},         {"IDXGIObject", __uuidof(IDXGIObject)},
    {"IDXGIOutput", __uuidof(IDXGIOutput)},   {"IDXGIOutput1", __uuidof(IDXGIOutput1)},
    {"IDXGIOutput2", __uuidof(IDXGIOutput2)}, {"IDXGIOutput3", __uuidof(IDXGIOutput3)},
    {"IDXGIOutput4", __uuidof(IDXGIOutput4)}, {"IDXGIOutput5", __uuidof(IDXGIOutput5)},
    {"IDXGIOutput6", __uuidof(IDXGIOutput6)}, {"IMTLDXGIOutput", IID_MTLDXGIOutput},
};
// A case's output (index 0 unless given), with its adapter, factory and the monitor the probe sees for it.
struct Op {
    P<IDXGIFactory6> f;
    P<IMTLDXGIAdapterP> a;
    P<IDXGIOutput6> o;
    Monitor mon;
    explicit Op(UINT index = 0) : f(NewFactory()), a(NewAdapter(f)), o(NewOutput(a, index)) {
        std::vector<Monitor> all = Monitors();
        if (index < all.size()) mon = all[index];
    }
};

// ---- IUnknown, IDXGIObject ----

CASE("dxgio", K_OUTPUT, 0, "qi", 0) {
    Op x;
    if (!x.o) return;
    for (auto &k : kOutputIids) {
        void *p = (void *)kA5Ptr;
        HRESULT hr = x.o->QueryInterface(k.iid, &p);
        std::string kv = F("out=%s", OutPtr(p));
        if (SUCCEEDED(hr) && p && p != kA5Ptr) {
            kv += F(" same=%d ref=%lu", p == (void *)x.o.p, (unsigned long)Refs(x.o));
            ((IUnknown *)p)->Release();
        }
        OutS(k.name, hr, kv);
    }
}
CASE("dxgio", K_OUTPUT, 0, "qi-unknown", 0) {
    Op x;
    if (!x.o) return;
    void *p = (void *)kA5Ptr, *q = (void *)kA5Ptr;
    HRESULT hr = x.o->QueryInterface(UnknownIid(20), &p);
    HRESULT again = x.o->QueryInterface(UnknownIid(20), &q);
    Out(hr, F("out=%s again=%08x out2=%s", OutPtr(p), (unsigned)again, OutPtr(q)));
}
CASE("dxgio", K_OUTPUT, 0, "qi-null-out", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->QueryInterface(__uuidof(IDXGIOutput), nullptr));
}
CASE("dxgio", K_OUTPUT, 0, "qi-null-riid", ISOLATE | NOT_ON_C) {
    Op x;
    if (!x.o) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(x.o.p, 0, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgio", K_OUTPUT, 1, "refs", 0) {
    Op x;
    if (!x.o) return;
    IDXGIOutput6 *o = x.o.p;
    x.o.p = nullptr;
    ULONG aref = Refs(x.a);
    ULONG add = o->AddRef(), rel = o->Release(), last = o->Release();
    Out(S_OK, F("addref=%lu release=%lu last=%lu adapterref=%lu back=%lu", (unsigned long)add, (unsigned long)rel,
                (unsigned long)last, (unsigned long)aref, (unsigned long)Refs(x.a)));
}
CASE("dxgio", K_OUTPUT, 2, "release", 0) {
    Op x;
    if (!x.o) return;
    IDXGIOutput6 *o = x.o.p;
    x.o.p = nullptr;
    o->AddRef();
    ULONG r1 = o->Release(), r2 = o->Release();
    Out(S_OK, F("release=%lu,%lu", (unsigned long)r1, (unsigned long)r2));
}
static const bool kOutputPdata = RegisterPdata("dxgio", K_OUTPUT, MakeOutput);
CASE("dxgio", K_OUTPUT, 6, "getparent", 0) {
    Op x;
    if (!x.o) return;
    void *p = (void *)kA5Ptr;
    ULONG before = Refs(x.a);
    HRESULT hr = x.o->GetParent(IID_MTLDXGIAdapter, &p);
    std::string kv = F("out=%s", OutPtr(p));
    if (SUCCEEDED(hr) && p && p != kA5Ptr) {
        kv += F(" same=%d adapterref=%+ld", p == (void *)x.a.p, (long)Refs(x.a) - (long)before);
        ((IUnknown *)p)->Release();
    }
    Out(hr, kv);
}
CASE("dxgio", K_OUTPUT, 6, "getparent-unknown", 0) {
    Op x;
    if (!x.o) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = x.o->GetParent(UnknownIid(21), &p);
    Out(hr, F("out=%s", OutPtr(p)));
}
CASE("dxgio", K_OUTPUT, 6, "getparent-null-out", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetParent(__uuidof(IDXGIAdapter), nullptr));
}
CASE("dxgio", K_OUTPUT, 6, "getparent-null-riid", ISOLATE | NOT_ON_C) {
    Op x;
    if (!x.o) return;
    void *p = (void *)kA5Ptr;
    HRESULT hr = Call<HRESULT>(x.o.p, 6, (const IID *)nullptr, &p);
    Out(hr, F("out=%s", OutPtr(p)));
}

// ---- GetDesc, GetDesc1 ----

template <class T> static std::string OutputDescKv(const Op &x, const Two &t) {
    T d = t.Get<T>();
    bool name = W(d.DeviceName, 32) == W(x.mon.name, 32);  // up to the NUL: Win32 leaves stack bytes after it
    bool rect = !memcmp(&d.DesktopCoordinates, &x.mon.rect, sizeof(RECT));
    std::string kv = F("name=%s rect=%s monitor=%s attached=%d rotation=%u ", name ? "monitorinfo" : "other",
                       rect ? "monitorinfo" : "other", d.Monitor == x.mon.handle ? "enum" : "other",
                       (int)d.AttachedToDesktop, (unsigned)d.Rotation);
    std::string raw = F("name=%s rect=%ld,%ld,%ld,%ld", W(d.DeviceName, 32).c_str(), (long)d.DesktopCoordinates.left,
                        (long)d.DesktopCoordinates.top, (long)d.DesktopCoordinates.right, (long)d.DesktopCoordinates.bottom);
    if constexpr (std::is_same_v<T, DXGI_OUTPUT_DESC1>) {
        bool hdr = (d.BitsPerColor == 10) == (d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) &&
                   (d.BitsPerColor == 10) == (d.MaxLuminance > 100.0f) && (d.BitsPerColor == 8 || d.BitsPerColor == 10);
        bool unit = true;
        const float *prim[] = {d.RedPrimary, d.GreenPrimary, d.BluePrimary, d.WhitePoint};
        for (const float *p : prim) unit &= p[0] >= 0 && p[0] <= 1 && p[1] >= 0 && p[1] <= 1;
        kv += F("hdr=%s primaries=%s min=%g fullframe=%s ", hdr ? "consistent" : "inconsistent", unit ? "unit" : "other",
                (double)d.MinLuminance, d.MaxFullFrameLuminance == d.MaxLuminance ? "max" : "other");
        if (InChild("config") || InChild("omori")) kv += F("bpc=%u colorspace=%u ", d.BitsPerColor, (unsigned)d.ColorSpace);
        raw += F(" bpc=%u colorspace=%u max=%g r=%g,%g g=%g,%g b=%g,%g w=%g,%g", d.BitsPerColor, (unsigned)d.ColorSpace,
                 (double)d.MaxLuminance, (double)d.RedPrimary[0], (double)d.RedPrimary[1], (double)d.GreenPrimary[0],
                 (double)d.GreenPrimary[1], (double)d.BluePrimary[0], (double)d.BluePrimary[1], (double)d.WhitePoint[0],
                 (double)d.WhitePoint[1]);
    }
    return kv + t.WU() + Raw(raw);
}
CASE("dxgio", K_OUTPUT, 7, "desc", 0) {
    Op x;
    if (!x.o) return;
    Two t(sizeof(DXGI_OUTPUT_DESC));
    HRESULT hr = x.o->GetDesc((DXGI_OUTPUT_DESC *)t.a);
    x.o->GetDesc((DXGI_OUTPUT_DESC *)t.b);
    Out(hr, OutputDescKv<DXGI_OUTPUT_DESC>(x, t));
}
CASE("dxgio", K_OUTPUT, 7, "null", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetDesc(nullptr));
}
// forceSDR (the config child's key, OMORI's profile): 8 bits, G22_P709.
CASE("dxgio", K_OUTPUT, 27, "desc1", E_DEFAULT | E_CONFIG | E_OMORI) {
    Op x;
    if (!x.o) return;
    Two t(sizeof(DXGI_OUTPUT_DESC1));
    HRESULT hr = x.o->GetDesc1((DXGI_OUTPUT_DESC1 *)t.a);
    x.o->GetDesc1((DXGI_OUTPUT_DESC1 *)t.b);
    Out(hr, OutputDescKv<DXGI_OUTPUT_DESC1>(x, t));
}
CASE("dxgio", K_OUTPUT, 27, "null", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetDesc1(nullptr));
}

// ---- Mode lists ----

static const struct { const char *name; DXGI_FORMAT format; } kModeFormats[] = {
    {"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM},       {"B8G8R8A8_UNORM", DXGI_FORMAT_B8G8R8A8_UNORM},
    {"B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB}, {"R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM},
    {"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT}, {"UNKNOWN", DXGI_FORMAT_UNKNOWN},
    {"BC1_UNORM", DXGI_FORMAT_BC1_UNORM},
};
struct ModeOf {
    UINT w, h, num, den, format, scan, scaling, stereo;
};
template <class T> static ModeOf ToMode(const T &m) {
    ModeOf r = {m.Width, m.Height, m.RefreshRate.Numerator, m.RefreshRate.Denominator, (UINT)m.Format,
                (UINT)m.ScanlineOrdering, (UINT)m.Scaling, 0};
    if constexpr (std::is_same_v<T, DXGI_MODE_DESC1>) r.stereo = (UINT)m.Stereo;
    return r;
}
static bool SameMode(const ModeOf &m, const Mode &e) { return m.w == e.w && m.h == e.h && m.num == e.num && m.den == e.den; }
static uint64_t WholeRate(const ModeOf &m) { return m.den ? m.num / m.den : 0; }
// A list DXGI gave against the probe's own: count, order, format, the current mode, the refresh representation.
static std::string ModeListKv(const std::vector<ModeOf> &list, const Monitor &mon, DXGI_FORMAT format, UINT flags) {
    if (list.empty()) return "count=0";
    bool fp16 = format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    size_t want = fp16 ? mon.n64 : mon.modes.size() + ((flags & DXGI_ENUM_MODES_INTERLACED) ? mon.interlaced32 : 0);
    bool sorted = true, asked = true, current = false, den1000 = true, hook = true, progressive = true, plain = true;
    for (size_t i = 0; i < list.size(); i++) {
        const ModeOf &m = list[i];
        if (i) {
            const ModeOf &p = list[i - 1];
            sorted &= p.w < m.w || (p.w == m.w && (p.h < m.h || (p.h == m.h && WholeRate(p) <= WholeRate(m))));
        }
        asked &= m.format == (UINT)format;
        current |= SameMode(m, mon.current);
        den1000 &= m.den == 1000;
        bool inList = false;
        for (const Mode &e : mon.modes) inList |= SameMode(m, e);
        hook &= inList;
        progressive &= m.scan == DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        plain &= m.scaling == 0 && m.stereo == 0;
    }
    std::string kv = F("count=%s sorted=%d format=%s has-current=%d den=%s scan=%s scaling-stereo=%s",
                       list.size() == want ? "enum" : "other", sorted, asked ? "asked" : "other", current,
                       g_native ? (hook ? "hook" : "other") : (den1000 ? "1000" : "other"),
                       progressive ? "progressive" : "other", plain ? "0" : "other");
    if (g_full) {
        kv += F(" count=%zu modes=", list.size());
        for (size_t i = 0; i < list.size(); i++)
            kv += F("%s%ux%u@%u/%u", i ? "," : "", list[i].w, list[i].h, list[i].num, list[i].den);
    }
    return kv;
}
// GetDisplayModeList or GetDisplayModeList1 into a buffer of the size the count asked for.
template <class T, class Fn> static void ModeLists(Fn call) {
    Op x;
    if (!x.o) return;
    for (auto &k : kModeFormats)
        for (UINT flags : {0u, (UINT)DXGI_ENUM_MODES_INTERLACED, (UINT)DXGI_ENUM_MODES_SCALING}) {
            UINT n = 0xa5a5a5a5;
            HRESULT first = call(x.o.p, k.format, flags, &n, (T *)nullptr);
            std::vector<T> buf(n == 0xa5a5a5a5 ? 1 : n + 1);
            UINT got = n;
            HRESULT hr = n == 0xa5a5a5a5 ? first : call(x.o.p, k.format, flags, &got, buf.data());
            std::vector<ModeOf> list;
            for (UINT i = 0; i < got && i < n; i++) list.push_back(ToMode(buf[i]));
            OutS(F("%s.%u", k.name, flags).c_str(), hr,
                 F("count-call=%08x same-count=%d ", (unsigned)first, got == n) + ModeListKv(list, x.mon, k.format, flags));
        }
}
static HRESULT List0(IDXGIOutput6 *o, DXGI_FORMAT f, UINT flags, UINT *n, DXGI_MODE_DESC *d) {
    return o->GetDisplayModeList(f, flags, n, d);
}
static HRESULT List1(IDXGIOutput6 *o, DXGI_FORMAT f, UINT flags, UINT *n, DXGI_MODE_DESC1 *d) {
    return o->GetDisplayModeList1(f, flags, n, d);
}
CASE("dxgio", K_OUTPUT, 8, "list", 0) { ModeLists<DXGI_MODE_DESC>(List0); }
CASE("dxgio", K_OUTPUT, 19, "list", 0) { ModeLists<DXGI_MODE_DESC1>(List1); }

// A buffer of `room` entries filled 0xA5, *pNumModes = asked: the answer, the count, and which entries were written.
template <class T, class Fn> static std::string Sized(Fn call, IDXGIOutput6 *o, UINT asked, UINT room, HRESULT *hr,
                                                      size_t off = 0) {
    One b(sizeof(T) * room + 1, off);
    UINT n = asked;
    *hr = call(o, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, (T *)b.p);
    UINT written = 0, last = 0;
    for (UINT i = 0; i < room; i++)
        if (!b.Untouched(i * sizeof(T), (i + 1) * sizeof(T))) written++, last = i + 1;
    return F("count=%u written=%u through=%u", n, written, last);
}
template <class T, class Fn> static void SizedCases(Fn call, bool big) {
    Op x;
    if (!x.o) return;
    UINT n = 0;
    call(x.o.p, DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, (T *)nullptr);
    if (n == 0) return Fatal("no modes", S_OK);
    HRESULT hr;
    // Each count is printed relative to n, the list's length: n-1, 0, n.
    auto rel = [n](const std::string &s) {
        std::string r = s;
        for (auto &key : {"count=", "written=", "through="}) {
            size_t at = r.find(key);
            if (at == std::string::npos) continue;
            size_t end = r.find(' ', at);
            UINT v = (UINT)strtoul(r.c_str() + at + strlen(key), nullptr, 10);
            std::string word = v == n ? "n" : v + 1 == n ? "n-1" : v == 0 ? "0" : F("n%+d", (int)v - (int)n);
            r = r.substr(0, at + strlen(key)) + word + (end == std::string::npos ? "" : r.substr(end));
        }
        return r;
    };
    std::string s = Sized<T>(call, x.o.p, n - 1, n, &hr);
    OutS("short", hr, rel(s));
    s = Sized<T>(call, x.o.p, 0, n, &hr);
    OutS("zero", hr, rel(s));
    if (big) {
        s = Sized<T>(call, x.o.p, n + 3, n + 3, &hr);
        OutS("big", hr, rel(s) + " room=n+3");
    }
    OutS("null-count", call(x.o.p, DXGI_FORMAT_R8G8B8A8_UNORM, 0, nullptr, (T *)nullptr));
}
CASE("dxgio", K_OUTPUT, 8, "sized", 0) { SizedCases<DXGI_MODE_DESC>(List0, true); }
CASE("dxgio", K_OUTPUT, 19, "sized", 0) { SizedCases<DXGI_MODE_DESC1>(List1, true); }
CASE("dxgio", K_OUTPUT, 19, "misaligned", 0) {
    Op x;
    if (!x.o) return;
    UINT n = 0;
    x.o->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr);
    HRESULT hr;
    std::string s = Sized<DXGI_MODE_DESC1>(List1, x.o.p, n, n, &hr, 4);
    Out(hr, F("all=%d", s == F("count=%u written=%u through=%u", n, n, n)));
}

// ---- FindClosestMatchingMode1, FindClosestMatchingMode ----

static DXGI_MODE_DESC1 Target(UINT w, UINT h, UINT num, UINT den, DXGI_FORMAT f,
                              DXGI_MODE_SCANLINE_ORDER scan = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, BOOL stereo = FALSE) {
    DXGI_MODE_DESC1 m = {};
    m.Width = w;
    m.Height = h;
    m.RefreshRate = {num, den};
    m.Format = f;
    m.ScanlineOrdering = scan;
    m.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    m.Stereo = stereo;
    return m;
}
// The result named by relation: the target itself, the current mode, a member of the list, else none; its size;
// whether its rate is the nearest of its size's to `rate` (the C++'s integer rule); its exact format and orders.
// With a rate asked, current and member are one word, `listed`: whether the nearest rate is the display's current one
// depends on the display (144 Hz here, 60 in the guest and natively), and rate=nearest size=current pin the choice.
static std::string ClosestKv(const ModeOf &r, const Monitor &mon, const DXGI_MODE_DESC1 *target, UINT rate) {
    bool member = false;
    for (const Mode &e : mon.modes) member |= SameMode(r, e);
    const char *what = target && r.w == target->Width && r.h == target->Height &&
                               r.num == target->RefreshRate.Numerator && r.den == target->RefreshRate.Denominator
                           ? "target"
                       : rate && (member || SameMode(r, mon.current)) ? "listed"
                       : SameMode(r, mon.current)                     ? "current"
                       : member                                       ? "member"
                                                                      : "none";
    std::string kv = F("mode=%s size=%s", what, r.w == mon.current.w && r.h == mon.current.h ? "current" : "other");
    if (rate) {
        uint64_t best = UINT64_MAX;
        for (const Mode &e : mon.modes)
            if (e.w == r.w && e.h == r.h && e.den) {
                int64_t d = (int64_t)((uint64_t)e.num / e.den) - (int64_t)rate;
                best = std::min<uint64_t>(best, (uint64_t)(d < 0 ? -d : d));
            }
        int64_t d = r.den ? (int64_t)((uint64_t)r.num / r.den) - (int64_t)rate : INT64_MAX;
        kv += F(" rate=%s", (uint64_t)(d < 0 ? -d : d) == best ? "nearest" : "other");
    }
    return kv + F(" format=%u scan=%u scaling=%u stereo=%u", r.format, r.scan, r.scaling, r.stereo) +
           Raw(F("mode=%ux%u@%u/%u", r.w, r.h, r.num, r.den));
}
static void Closest1(const char *sub, const Op &x, const DXGI_MODE_DESC1 *target, bool device, UINT rate = 0) {
    FakeUnk dev('d');
    One b(sizeof(DXGI_MODE_DESC1));
    HRESULT hr = x.o->FindClosestMatchingMode1(target, (DXGI_MODE_DESC1 *)b.p, device ? &dev : nullptr);
    std::string kv = b.Untouched(0, b.n) ? std::string("out=untouched")
                                         : ClosestKv(ToMode(*(DXGI_MODE_DESC1 *)b.p), x.mon, target, rate);
    OutS(sub, hr, kv + F(" device=%s", Events().c_str()));
}
static DXGI_MODE_DESC1 CurrentTarget(const Op &x, DXGI_FORMAT f = DXGI_FORMAT_R8G8B8A8_UNORM) {
    return Target(x.mon.current.w, x.mon.current.h, x.mon.current.num, x.mon.current.den, f,
                  DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE);
}
CASE("dxgio", K_OUTPUT, 20, "closest", 0) {
    Op x;
    if (!x.o) return;
    DXGI_MODE_DESC1 member = CurrentTarget(x);
    Closest1("member", x, &member, true);
    DXGI_MODE_DESC1 unknown = Target(0, 0, 0, 0, DXGI_FORMAT_UNKNOWN);
    Closest1("format-unknown", x, &unknown, true);
    Closest1("format-unknown-no-device", x, &unknown, false);
    DXGI_MODE_DESC1 width = Target(640, 0, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    Closest1("width-only", x, &width, true);
    Closest1("null-mode", x, nullptr, true);
    One b(sizeof(DXGI_MODE_DESC1));
    OutS("null-out", x.o->FindClosestMatchingMode1(&member, nullptr, nullptr));
    DXGI_MODE_DESC1 stereo = CurrentTarget(x);
    stereo.Stereo = TRUE;
    Closest1("stereo", x, &stereo, true);
    DXGI_MODE_DESC1 fp16 = Target(0, 0, 0, 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Closest1("no-modes", x, &fp16, true);
    DXGI_MODE_DESC1 zero = Target(0, 0, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    Closest1("zero", x, &zero, true);
    DXGI_MODE_DESC1 zero61 = Target(0, 0, 61, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    Closest1("zero-61", x, &zero61, true, 61);
    DXGI_MODE_DESC1 rate61 = Target(x.mon.current.w, x.mon.current.h, 61, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    Closest1("rate-61", x, &rate61, true, 61);
    DXGI_MODE_DESC1 prog = Target(x.mon.current.w, x.mon.current.h, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM,
                                  DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE);
    Closest1("progressive", x, &prog, true);
}
CASE("dxgio", K_OUTPUT, 9, "closest", 0) {
    Op x;
    if (!x.o) return;
    DXGI_MODE_DESC1 m1 = CurrentTarget(x);
    DXGI_MODE_DESC m = {m1.Width, m1.Height, m1.RefreshRate, m1.Format, m1.ScanlineOrdering, m1.Scaling};
    One b(sizeof(DXGI_MODE_DESC));
    HRESULT hr = x.o->FindClosestMatchingMode(&m, (DXGI_MODE_DESC *)b.p, nullptr);
    OutS("member", hr, b.Untouched(0, b.n) ? std::string("out=untouched")
                                           : ClosestKv(ToMode(*(DXGI_MODE_DESC *)b.p), x.mon, &m1, 0));
    One c(sizeof(DXGI_MODE_DESC));
    OutS("null-mode", x.o->FindClosestMatchingMode(nullptr, (DXGI_MODE_DESC *)c.p, nullptr),
         F("out=%s", c.Untouched(0, c.n) ? "untouched" : "written"));
    OutS("null-out", x.o->FindClosestMatchingMode(&m, nullptr, nullptr));
}

// ---- The rest of IDXGIOutput ----

CASE("dxgio", K_OUTPUT, 10, "vblank", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->WaitForVBlank());
}
CASE("dxgio", K_OUTPUT, 11, "take", 0) {
    Op x;
    if (!x.o) return;
    FakeUnk dev('d');
    HRESULT hr = x.o->TakeOwnership(&dev, TRUE);
    Out(hr, F("device=%s", Events().c_str()));
}
CASE("dxgio", K_OUTPUT, 12, "release", 0) {
    Op x;
    if (!x.o) return;
    x.o->ReleaseOwnership();
    OutV();
}
CASE("dxgio", K_OUTPUT, 16, "set-surface", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->SetDisplaySurface(nullptr));
}
CASE("dxgio", K_OUTPUT, 17, "surface-data", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetDisplaySurfaceData(nullptr));
}
CASE("dxgio", K_OUTPUT, 18, "statistics", 0) {
    Op x;
    if (!x.o) return;
    One b(sizeof(DXGI_FRAME_STATISTICS));
    HRESULT hr = x.o->GetFrameStatistics((DXGI_FRAME_STATISTICS *)b.p);
    Out(hr, F("stats=%s", b.Untouched(0, b.n) ? "untouched" : "written"));
}
CASE("dxgio", K_OUTPUT, 21, "surface-data1", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetDisplaySurfaceData1(nullptr));
}
CASE("dxgio", K_OUTPUT, 22, "duplicate", 0) {
    Op x;
    if (!x.o) return;
    FakeUnk dev('d');
    IDXGIOutputDuplication *d = (IDXGIOutputDuplication *)kA5Ptr, *e = (IDXGIOutputDuplication *)kA5Ptr;
    HRESULT none = x.o->DuplicateOutput(nullptr, &d);
    HRESULT hr = x.o->DuplicateOutput(&dev, &e);
    OutS("null-device", none, F("out=%s", OutPtr(d)));
    OutS("device", hr, F("out=%s device=%s", OutPtr(e), Events().c_str()));
}
CASE("dxgio", K_OUTPUT, 26, "duplicate1", 0) {
    Op x;
    if (!x.o) return;
    FakeUnk dev('d');
    DXGI_FORMAT formats[1] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    IDXGIOutputDuplication *d = (IDXGIOutputDuplication *)kA5Ptr, *e = (IDXGIOutputDuplication *)kA5Ptr;
    HRESULT none = x.o->DuplicateOutput1(nullptr, 0, 1, formats, &d);
    HRESULT hr = x.o->DuplicateOutput1(&dev, 0, 1, formats, &e);
    OutS("null-device", none, F("out=%s", OutPtr(d)));
    OutS("device", hr, F("out=%s device=%s", OutPtr(e), Events().c_str()));
}
CASE("dxgio", K_OUTPUT, 23, "overlays", 0) {
    Op x;
    if (!x.o) return;
    Out((HRESULT)x.o->SupportsOverlays());
}
// Each flags query: 0 written, a null flags pointer S_OK.
CASE("dxgio", K_OUTPUT, 24, "flags", 0) {
    Op x;
    if (!x.o) return;
    FakeUnk dev('d');
    UINT flags = 0xa5a5a5a5;
    HRESULT hr = x.o->CheckOverlaySupport(DXGI_FORMAT_B8G8R8A8_UNORM, &dev, &flags);
    HRESULT none = x.o->CheckOverlaySupport(DXGI_FORMAT_B8G8R8A8_UNORM, &dev, nullptr);
    Out(hr, F("flags=%x null=%08x device=%s", flags, (unsigned)none, Events().c_str()));
}
CASE("dxgio", K_OUTPUT, 25, "flags", 0) {
    Op x;
    if (!x.o) return;
    FakeUnk dev('d');
    UINT flags = 0xa5a5a5a5;
    HRESULT hr = x.o->CheckOverlayColorSpaceSupport(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &dev, &flags);
    HRESULT none = x.o->CheckOverlayColorSpaceSupport(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                      DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &dev, nullptr);
    Out(hr, F("flags=%x null=%08x device=%s", flags, (unsigned)none, Events().c_str()));
}
CASE("dxgio", K_OUTPUT, 28, "flags", 0) {
    Op x;
    if (!x.o) return;
    UINT flags = 0xa5a5a5a5;
    HRESULT hr = x.o->CheckHardwareCompositionSupport(&flags);
    HRESULT none = x.o->CheckHardwareCompositionSupport(nullptr);
    Out(hr, F("flags=%x null=%08x", flags, (unsigned)none));
}

// ---- Gamma ----

static bool Identity(const DXGI_GAMMA_CONTROL &g) {
    for (int i = 0; i < 1024; i++) {
        float v = float(i) / float(1023);
        if (g.GammaCurve[i].Red != v || g.GammaCurve[i].Green != v || g.GammaCurve[i].Blue != v) return false;
    }
    return true;
}
// A ramp no output has: red i/2047, green 1 - i/1023, blue 0.25 (+ k); point 1024, past DXMT's 1024, is 9.
static void Ramp(DXGI_GAMMA_CONTROL &g, float k = 0) {
    memset(&g, 0, sizeof g);
    g.Scale = {2, 2, 2};
    g.Offset = {0.5f, 0.5f, 0.5f};
    for (int i = 0; i < 1025; i++)
        g.GammaCurve[i] = i == 1024 ? DXGI_RGB{9, 9, 9} : DXGI_RGB{float(i) / 2047.0f, 1.0f - float(i) / 1023.0f, 0.25f + k};
}
static std::string ControlKv(const DXGI_GAMMA_CONTROL &got, const DXGI_GAMMA_CONTROL &set, const One &b) {
    bool curve = true;
    for (int i = 0; i < 1024; i++)
        curve &= !memcmp(&got.GammaCurve[i], &set.GammaCurve[i], sizeof(DXGI_RGB));
    size_t p1024 = offsetof(DXGI_GAMMA_CONTROL, GammaCurve) + 1024 * sizeof(DXGI_RGB);
    return F("curve=%s p1024=%s scale=%g,%g,%g offset=%g,%g,%g", curve ? "set" : Identity(got) ? "identity" : "other",
             b.Untouched(p1024, p1024 + sizeof(DXGI_RGB)) ? "untouched" : "written", (double)got.Scale.Red,
             (double)got.Scale.Green, (double)got.Scale.Blue, (double)got.Offset.Red, (double)got.Offset.Green,
             (double)got.Offset.Blue);
}
CASE("dxgio", K_OUTPUT, 13, "caps", 0) {
    Op x;
    if (!x.o) return;
    One b(sizeof(DXGI_GAMMA_CONTROL_CAPABILITIES));
    HRESULT hr = x.o->GetGammaControlCapabilities((DXGI_GAMMA_CONTROL_CAPABILITIES *)b.p);
    DXGI_GAMMA_CONTROL_CAPABILITIES c;
    memcpy(&c, b.p, sizeof c);
    bool pos = true;
    for (UINT i = 0; i < 1024; i++) pos &= c.ControlPointPositions[i] == float(i) / float(1023);
    size_t last = offsetof(DXGI_GAMMA_CONTROL_CAPABILITIES, ControlPointPositions) + 1024 * sizeof(float);
    Out(hr, F("scale-offset=%d max=%g min=%g points=%u positions=%s p1024=%s", (int)c.ScaleAndOffsetSupported,
              (double)c.MaxConvertedValue, (double)c.MinConvertedValue, c.NumGammaControlPoints,
              pos ? "i/1023" : "other", b.Untouched(last, last + 4) ? "untouched" : "written"));
}
CASE("dxgio", K_OUTPUT, 13, "null", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->GetGammaControlCapabilities(nullptr));
}
CASE("dxgio", K_OUTPUT, 15, "identity", 0) {
    Op x;
    if (!x.o) return;
    One b(sizeof(DXGI_GAMMA_CONTROL));
    HRESULT hr = x.o->GetGammaControl((DXGI_GAMMA_CONTROL *)b.p);
    DXGI_GAMMA_CONTROL set;
    Ramp(set);
    Out(hr, ControlKv(*(DXGI_GAMMA_CONTROL *)b.p, set, b));
}
CASE("dxgio", K_OUTPUT, 14, "round-trip", 0) {
    Op x;
    if (!x.o) return;
    DXGI_GAMMA_CONTROL set;
    Ramp(set);
    HRESULT hr = x.o->SetGammaControl(&set);
    One b(sizeof(DXGI_GAMMA_CONTROL));
    HRESULT get = x.o->GetGammaControl((DXGI_GAMMA_CONTROL *)b.p);
    Out(hr, F("get=%08x ", (unsigned)get) + ControlKv(*(DXGI_GAMMA_CONTROL *)b.p, set, b));
}
// Gamma is per output object (B9): a second EnumOutputs(0) object keeps the identity ramp.
CASE("dxgio", K_OUTPUT, 15, "per-object", 0) {
    Op x;
    if (!x.o) return;
    P<IDXGIOutput6> other(NewOutput(x.a));
    if (!other) return;
    DXGI_GAMMA_CONTROL set;
    Ramp(set);
    x.o->SetGammaControl(&set);
    One b(sizeof(DXGI_GAMMA_CONTROL));
    HRESULT hr = other->GetGammaControl((DXGI_GAMMA_CONTROL *)b.p);
    Out(hr, ControlKv(*(DXGI_GAMMA_CONTROL *)b.p, set, b));
}
CASE("dxgio", K_OUTPUT, 14, "null", 0) {
    Op x;
    if (!x.o) return;
    Out(x.o->SetGammaControl(nullptr), F("get-null=%08x", (unsigned)x.o->GetGammaControl(nullptr)));
}
CASE("dxgio", K_OUTPUT, 14, "misaligned", 0) {
    Op x;
    if (!x.o) return;
    DXGI_GAMMA_CONTROL set;
    Ramp(set);
    std::vector<uint8_t> raw;
    void *ms = Aligned4(raw, &set, sizeof set);
    HRESULT hr = x.o->SetGammaControl((const DXGI_GAMMA_CONTROL *)ms);
    One b(sizeof(DXGI_GAMMA_CONTROL), 4);
    HRESULT get = x.o->GetGammaControl((DXGI_GAMMA_CONTROL *)b.p);
    DXGI_GAMMA_CONTROL got;
    memcpy(&got, b.p, sizeof got);
    Out(hr, F("get=%08x ", (unsigned)get) + ControlKv(got, set, b));
}
// IMTLDXGIOutput's slot 29, d3d11's swap chain's view of the ramp: points 0, 511, 1023 and the version per set.
CASE("dxgio", K_OUTPUT, 29, "ramp", 0) {
    Op x;
    if (!x.o) return;
    IMTLDXGIOutputP *m = nullptr;
    HRESULT hr = x.o->QueryInterface(IID_MTLDXGIOutput, (void **)&m);
    if (FAILED(hr) || !m) return Out(hr, F("out=%s", OutPtr(m)));
    auto points = [](const GammaRampP *r) {
        std::string s;
        for (int i : {0, 511, 1023})
            s += F("%s%.9g/%.9g/%.9g", s.empty() ? "" : ",", (double)r->red[i], (double)r->green[i], (double)r->blue[i]);
        return s + F(" version=%llu", (unsigned long long)r->version);
    };
    std::string before = points(m->GetGammaRamp());
    DXGI_GAMMA_CONTROL set;
    Ramp(set);
    x.o->SetGammaControl(&set);
    std::string once = points(m->GetGammaRamp());
    Ramp(set, 0.5f);
    x.o->SetGammaControl(&set);
    const GammaRampP *r = m->GetGammaRamp();
    Out(hr, "before=" + before + " set=" + once + F(" again=%llu same-pointer=%d", (unsigned long long)r->version,
                                                     r == m->GetGammaRamp()));
    m->Release();
}

// ---- The native lane's second display, and a 0/0 mode ----

CASE("dxgio", K_OUTPUT, 19, "display2", NATIVE_ONLY) {
    Op x(1);
    if (!x.o) return;
    Two t(sizeof(DXGI_OUTPUT_DESC));
    HRESULT hr = x.o->GetDesc((DXGI_OUTPUT_DESC *)t.a);
    x.o->GetDesc((DXGI_OUTPUT_DESC *)t.b);
    OutS("desc", hr, OutputDescKv<DXGI_OUTPUT_DESC>(x, t));
    UINT n = 0;
    x.o->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr);
    std::vector<DXGI_MODE_DESC1> buf(n + 1);
    hr = x.o->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, buf.data());
    std::vector<ModeOf> list;
    for (UINT i = 0; i < n; i++) list.push_back(ToMode(buf[i]));
    OutS("list", hr, ModeListKv(list, x.mon, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
}
// Defined result (P20): a 0/0 mode sorts last and the filter skips it.
CASE("dxgio", K_OUTPUT, 19, "zero-den", ISOLATE | NOT_ON_C | NATIVE_ONLY) {
    g_zero_den = true;
    Op x(1);
    if (!x.o) return;
    UINT n = 0;
    x.o->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr);
    std::vector<DXGI_MODE_DESC1> buf(n + 1);
    HRESULT hr = x.o->GetDisplayModeList1(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, buf.data());
    int at = -1;
    for (UINT i = 0; i < n; i++)
        if (buf[i].RefreshRate.Denominator == 0) at = (int)i;
    DXGI_MODE_DESC1 target = Target(1280, 720, 60, 1, DXGI_FORMAT_R8G8B8A8_UNORM), r = {};
    HRESULT closest = x.o->FindClosestMatchingMode1(&target, &r, nullptr);
    Out(hr, F("count=%u zero-den=%s closest=%08x closest-den=%u", n, at < 0 ? "absent" : at == (int)n - 1 ? "last" : "inside",
              (unsigned)closest, r.RefreshRate.Denominator));
}
