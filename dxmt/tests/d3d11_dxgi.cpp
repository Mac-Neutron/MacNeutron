// Every answer DXMT's dxgi.dll and d3d10core.dll give through their exports and the factory, adapter and output
// vtables, written against the C++ to pin it before the Rust port (DXMTRS step 3, plan
// docs/superpowers/plans/2026-10-30-dxmtrs-step3-d3d10core-dxgi.md, Task 3).
//   d3d11_dxgi.exe fixed|full [<family>...] [--ported]
//       Each case runs in a child process with an environment of its own (the parent's minus every DXMT_ variable,
//       plus its child's: DXMT_LOG_LEVEL=debug, DXMT_LOG_PATH, DXMT_SHADER_CACHE_PATH and the working folder its own
//       folder under %TEMP%\d3d11_dxgi, natively $TMPDIR), one line a call or relation:
//       "<family> <Interface>::<Method> <case>[:<sub>][@<child>] <hr hex8>|void|crashed <code>|hang [k=v ...]".
//       Families: dxgif (the factory and dxgi's exports), dxgia (the adapter), dxgio (the output), d3d10c
//       (d3d10core's exports) and dxgil ("dxgil <child> <file> <line>": each child's *_dxgi.log and *_d3d10core.log,
//       CR shown as <CR>). fixed prints relations where machines or lanes differ (the adapter's LUID, memory, the
//       displays and their modes) and the same number of lines on any machine; full adds the raw values. Out structs
//       are filled 0xA5 (and again 0x5A) and printed as the byte ranges a call wrote and left. --ported also runs the
//       cases the C++ crashes on or answers with garbage (NOT_ON_C), each in a child of its own.
//   d3d11_dxgi.exe rate          "rate <Method> <ns per call>" for GetMTLDevice, GetDesc1 and QueryVideoMemoryInfo
//   d3d11_dxgi.exe --list        the case table: "<family> <slot> <Interface>::<Method> <case> <flags>"
//   d3d11_dxgi.exe --cover       exit 1 unless every slot of the three vtables and every export has a case
//   d3d11_dxgi.exe --bites       each vtable slot stubbed alone (E_NOTIMPL, no write) must change a line of its
//                                family: "bites <Interface>::<Method> yes|excluded: <reason>|NO", exit 1 on a NO
//   d3d11_dxgi.exe --child <child> <first> <last> <fixed|full> [--ported] [--stub <kind> <slot>] [<family>...]
//                                (internal) the cases first..last that run in that child
// Built as PE (both Mac lanes, the VM guest's x86_64 and i386) and natively (build-tests.sh's line: two fake displays
// and a window through WMTSetWsiHooks; winemetal's MTLDevice_* through dlsym).
#include "d3d11_common.hpp"
#include <d3d10_1.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <thread>
#ifndef _WIN32
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
extern "C" HRESULT WINAPI D3D10CoreCreateDevice(IDXGIFactory *, IDXGIAdapter *, UINT, D3D_FEATURE_LEVEL, ID3D10Device **);
extern "C" HRESULT WINAPI D3D10CoreRegisterLayers();
#endif
extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport();

// ---- Modes, the case table ----

static bool g_full, g_ported;
static std::string g_child = "default";
static std::vector<std::string> g_families;
static int g_stub_kind = -1, g_stub_slot = -1;
#ifdef _WIN32
static const bool g_native = false;
#else
static const bool g_native = true;
#endif

enum Kind { K_FACTORY, K_ADAPTER, K_OUTPUT, K_DXGI, K_D3D10CORE };
struct SlotName { const char *iface, *method; };
static const SlotName kFactory[] = {
    {"IUnknown", "QueryInterface"}, {"IUnknown", "AddRef"}, {"IUnknown", "Release"},
    {"IDXGIObject", "SetPrivateData"}, {"IDXGIObject", "SetPrivateDataInterface"}, {"IDXGIObject", "GetPrivateData"},
    {"IDXGIObject", "GetParent"}, {"IDXGIFactory", "EnumAdapters"}, {"IDXGIFactory", "MakeWindowAssociation"},
    {"IDXGIFactory", "GetWindowAssociation"}, {"IDXGIFactory", "CreateSwapChain"},
    {"IDXGIFactory", "CreateSoftwareAdapter"}, {"IDXGIFactory1", "EnumAdapters1"}, {"IDXGIFactory1", "IsCurrent"},
    {"IDXGIFactory2", "IsWindowedStereoEnabled"}, {"IDXGIFactory2", "CreateSwapChainForHwnd"},
    {"IDXGIFactory2", "CreateSwapChainForCoreWindow"}, {"IDXGIFactory2", "GetSharedResourceAdapterLuid"},
    {"IDXGIFactory2", "RegisterStereoStatusWindow"}, {"IDXGIFactory2", "RegisterStereoStatusEvent"},
    {"IDXGIFactory2", "UnregisterStereoStatus"}, {"IDXGIFactory2", "RegisterOcclusionStatusWindow"},
    {"IDXGIFactory2", "RegisterOcclusionStatusEvent"}, {"IDXGIFactory2", "UnregisterOcclusionStatus"},
    {"IDXGIFactory2", "CreateSwapChainForComposition"}, {"IDXGIFactory3", "GetCreationFlags"},
    {"IDXGIFactory4", "EnumAdapterByLuid"}, {"IDXGIFactory4", "EnumWarpAdapter"},
    {"IDXGIFactory5", "CheckFeatureSupport"}, {"IDXGIFactory6", "EnumAdapterByGpuPreference"}};
static const SlotName kAdapter[] = {
    {"IUnknown", "QueryInterface"}, {"IUnknown", "AddRef"}, {"IUnknown", "Release"},
    {"IDXGIObject", "SetPrivateData"}, {"IDXGIObject", "SetPrivateDataInterface"}, {"IDXGIObject", "GetPrivateData"},
    {"IDXGIObject", "GetParent"}, {"IDXGIAdapter", "EnumOutputs"}, {"IDXGIAdapter", "GetDesc"},
    {"IDXGIAdapter", "CheckInterfaceSupport"}, {"IDXGIAdapter1", "GetDesc1"}, {"IDXGIAdapter2", "GetDesc2"},
    {"IDXGIAdapter3", "RegisterHardwareContentProtectionTeardownStatusEvent"},
    {"IDXGIAdapter3", "UnregisterHardwareContentProtectionTeardownStatus"}, {"IDXGIAdapter3", "QueryVideoMemoryInfo"},
    {"IDXGIAdapter3", "SetVideoMemoryReservation"},
    {"IDXGIAdapter3", "RegisterVideoMemoryBudgetChangeNotificationEvent"},
    {"IDXGIAdapter3", "UnregisterVideoMemoryBudgetChangeNotification"}, {"IDXGIAdapter4", "GetDesc3"},
    {"IMTLDXGIAdapter", "GetMTLDevice"}, {"IMTLDXGIAdapter", "GetLocalD3DKMT"}};
static const SlotName kOutput[] = {
    {"IUnknown", "QueryInterface"}, {"IUnknown", "AddRef"}, {"IUnknown", "Release"},
    {"IDXGIObject", "SetPrivateData"}, {"IDXGIObject", "SetPrivateDataInterface"}, {"IDXGIObject", "GetPrivateData"},
    {"IDXGIObject", "GetParent"}, {"IDXGIOutput", "GetDesc"}, {"IDXGIOutput", "GetDisplayModeList"},
    {"IDXGIOutput", "FindClosestMatchingMode"}, {"IDXGIOutput", "WaitForVBlank"}, {"IDXGIOutput", "TakeOwnership"},
    {"IDXGIOutput", "ReleaseOwnership"}, {"IDXGIOutput", "GetGammaControlCapabilities"},
    {"IDXGIOutput", "SetGammaControl"}, {"IDXGIOutput", "GetGammaControl"}, {"IDXGIOutput", "SetDisplaySurface"},
    {"IDXGIOutput", "GetDisplaySurfaceData"}, {"IDXGIOutput", "GetFrameStatistics"},
    {"IDXGIOutput1", "GetDisplayModeList1"}, {"IDXGIOutput1", "FindClosestMatchingMode1"},
    {"IDXGIOutput1", "GetDisplaySurfaceData1"}, {"IDXGIOutput1", "DuplicateOutput"},
    {"IDXGIOutput2", "SupportsOverlays"}, {"IDXGIOutput3", "CheckOverlaySupport"},
    {"IDXGIOutput4", "CheckOverlayColorSpaceSupport"}, {"IDXGIOutput5", "DuplicateOutput1"},
    {"IDXGIOutput6", "GetDesc1"}, {"IDXGIOutput6", "CheckHardwareCompositionSupport"},
    {"IMTLDXGIOutput", "GetGammaRamp"}};
static const SlotName kDxgi[] = {{"dxgi", "CreateDXGIFactory"}, {"dxgi", "CreateDXGIFactory1"},
                                 {"dxgi", "CreateDXGIFactory2"}, {"dxgi", "DXGIDeclareAdapterRemovalSupport"},
                                 {"dxgi", "DXGIGetDebugInterface1"}, {"dxgi", "DllMain"}};
static const SlotName kD3d10core[] = {{"d3d10core", "D3D10CoreCreateDevice"}, {"d3d10core", "D3D10CoreRegisterLayers"}};
static const SlotName *const kSlots[] = {kFactory, kAdapter, kOutput, kDxgi, kD3d10core};
static const int kSlotCount[] = {30, 21, 30, 5, 2};  // what --cover asks for (DllMain is dxgi's sixth, not an export)
static const char *const kKindName[] = {"IDXGIFactory", "IDXGIAdapter", "IDXGIOutput", "dxgi", "d3d10core"};

enum : unsigned {
    ISOLATE = 1, NOT_ON_C = 2, FULL_ONLY = 4, PE_ONLY = 8, NATIVE_ONLY = 16,
    // The children a case runs in (none: default only).
    E_DEFAULT = 1u << 8, E_CONFIG = 1u << 9, E_CONFIGU = 1u << 10, E_CONFBAD = 1u << 11, E_NVEXT = 1u << 12,
    E_CONFFILE = 1u << 13, E_CONFFILE2 = 1u << 14, E_OMORI = 1u << 15, E_GENSHIN = 1u << 16,
    E_EMPTYSECTION = 1u << 17, E_ALL = 0x3ffu << 8,
};

struct Case;
typedef void (*CaseFn)(const Case &);
struct Case {
    const char *family;
    int kind, slot;
    const char *name;
    unsigned flags;
    CaseFn fn;
    void *arg;
};
static std::vector<Case> &Cases() {
    static std::vector<Case> cases;
    return cases;
}
static bool Register(const Case &c) {
    Cases().push_back(c);
    return true;
}
#define CASE_CAT(a, b) a##b
#define CASE_J(n, fam, kind, slot, name, flags)                                                                       \
    static void CASE_CAT(case_, n)(const Case &);                                                                     \
    static const bool CASE_CAT(reg_, n) = Register({fam, kind, slot, name, flags, CASE_CAT(case_, n), nullptr});      \
    static void CASE_CAT(case_, n)([[maybe_unused]] const Case &self)
#define CASE_I(n, ...) CASE_J(n, __VA_ARGS__)
// CASE(family, kind, slot, name, flags) { body }: one test, registered in file order.
#define CASE(...) CASE_I(__COUNTER__, __VA_ARGS__)

// The children, in the order they run. env: added to the child's environment (UTF-8); file: written into its
// folder (name, contents); exe: the name of the probe's copy it runs (a built-in profile matches it).
struct Child {
    const char *name;
    unsigned bit, only;
    std::vector<std::pair<std::string, std::string>> env;
    const char *file, *contents, *exe;
};
static std::string Repeat(size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += char('A' + i % 26);
    return s;
}
static const std::vector<Child> &Children() {
    static const std::vector<Child> children = {
        {"default", E_DEFAULT, 0, {}, nullptr, nullptr, nullptr},
        {"config", E_CONFIG, 0,
         {{"DXMT_CONFIG", "dxgi.customVendorId=beef;dxgi.customDeviceId=1234;dxgi.customDeviceDesc=" + Repeat(200) +
                              ";dxgi.forceSDR=True"}},
         nullptr, nullptr, nullptr},
        // 125 ASCII, then U+00E9, U+1D11E and Z: the pair would need units 126-127 of 127, so it is dropped and Z
        // fills unit 126 (UTF-16); natively (UTF-32) the clef fits and Z is dropped.
        {"configu", E_CONFIGU, 0,
         {{"DXMT_CONFIG", "dxgi.customDeviceDesc=" + Repeat(125) + "\xc3\xa9\xf0\x9d\x84\x9eZ"}}, nullptr, nullptr,
         nullptr},
        // A stray continuation byte, then an invalid lead byte.
        {"confbad", E_CONFBAD, 0, {}, "dxmt.conf", "dxgi.customDeviceDesc = ab\x80" "cd\xff\n", nullptr},
        {"nvext", E_NVEXT, 0, {{"DXMT_ENABLE_NVEXT", "1"}}, nullptr, nullptr, nullptr},
        // A section for this exe, CRLF line ends, ending in an inactive section that makes DXMT_CONFIG inactive.
        {"conffile", E_CONFFILE, 0, {{"DXMT_CONFIG", "dxgi.customDeviceId=5678"}}, "dxmt.conf",
         "[d3d11_dxgi.exe]\r\ndxgi.customVendorId = 4321\r\n[other.exe]\r\n", nullptr},
        {"conffile2", E_CONFFILE2, 0, {{"DXMT_CONFIG_FILE", "other.conf"}}, "other.conf",
         "dxgi.customVendorId = 2468\n", nullptr},
        {"omori", E_OMORI, PE_ONLY, {}, nullptr, nullptr, "OMORI.exe"},
        {"genshin", E_GENSHIN, PE_ONLY, {}, nullptr, nullptr, "GenshinImpact.exe"},
        // An empty section: the native build's exe name is empty, so it is active there.
        {"emptysection", E_EMPTYSECTION, NATIVE_ONLY, {}, "dxmt.conf", "[]\ndxgi.customVendorId = 1357\n", nullptr},
    };
    return children;
}
static const Child *FindChild(const std::string &name) {
    for (const Child &c : Children())
        if (name == c.name) return &c;
    return nullptr;
}
static bool InChild(const char *name) { return g_child == name; }

static bool PlatformOk(unsigned flags) {
    return !((flags & PE_ONLY) && g_native) && !((flags & NATIVE_ONLY) && !g_native);
}
static bool FamilyOk(const char *family) {
    return g_families.empty() || std::find(g_families.begin(), g_families.end(), family) != g_families.end();
}
static bool RunsIn(const Case &c, unsigned bit) {
    if (!FamilyOk(c.family) || !PlatformOk(c.flags) || (c.flags & ISOLATE)) return false;
    if ((c.flags & FULL_ONLY) && !g_full) return false;
    return (c.flags & E_ALL) ? (c.flags & bit) != 0 : bit == E_DEFAULT;
}
static bool IsolateRuns(const Case &c) {
    return (c.flags & ISOLATE) && FamilyOk(c.family) && PlatformOk(c.flags) && (!(c.flags & NOT_ON_C) || g_ported) &&
           (!(c.flags & FULL_ONLY) || g_full);
}

// ---- Output ----

static const Case *g_case;
static std::string F(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string F(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}
static std::string Hex(HRESULT hr) { return F("%08x", (unsigned)hr); }
static void Emit(const char *sub, const std::string &status, const std::string &kv) {
    const SlotName &s = kSlots[g_case->kind][g_case->slot];
    std::string name = g_case->name;
    if (sub) name += std::string(":") + sub;
    if (g_child != "default" && g_child != "isolate") name += "@" + g_child;
    printf("%s %s::%s %s %s%s%s\n", g_case->family, s.iface, s.method, name.c_str(), status.c_str(),
           kv.empty() ? "" : " ", kv.c_str());
}
static void Out(HRESULT hr, const std::string &kv = "") { Emit(nullptr, Hex(hr), kv); }
static void OutS(const char *sub, HRESULT hr, const std::string &kv = "") { Emit(sub, Hex(hr), kv); }
static void OutV(const std::string &kv = "") { Emit(nullptr, "void", kv); }
// A setup step failed: the case stops with this line.
static void Fatal(const char *what, HRESULT hr) { Emit(nullptr, "fatal", F("%s %08x", what, (unsigned)hr)); }
// Raw values, in full mode only, each key prefixed "raw-" (full-diff.sh masks by key).
static std::string Raw(const std::string &kv) {
    if (!g_full) return "";
    std::string r;
    size_t at = 0;
    while (at < kv.size()) {
        size_t sp = kv.find(' ', at);
        r += " raw-" + kv.substr(at, sp == std::string::npos ? std::string::npos : sp - at);
        at = sp == std::string::npos ? kv.size() : sp + 1;
    }
    return r;
}

static std::string G(const GUID &g) {
    return F("%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", (unsigned)g.Data1, g.Data2, g.Data3, g.Data4[0],
             g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}
// A WCHAR string (UTF-16 on PE, UTF-32 natively) up to its NUL: printable ASCII as is, anything else (space and
// backslash included) as \xNN, \uNNNN or \UNNNNNNNN.
static std::string W(const WCHAR *s, size_t n) {
    std::string r;
    for (size_t i = 0; i < n && s[i]; i++) {
        uint32_t c = (uint32_t)s[i];
        if (c > 0x20 && c < 0x7f && c != '\\') r += char(c);
        else if (c < 0x100) r += F("\\x%02x", c);
        else if (c < 0x10000) r += F("\\u%04x", c);
        else r += F("\\U%08x", c);
    }
    return r.empty() ? "\"\"" : r;
}
static std::string Ranges(const std::vector<std::pair<size_t, size_t>> &v) {
    if (v.empty()) return "none";
    std::string r;
    for (auto &p : v) r += F("%s%zu-%zu", r.empty() ? "" : ",", p.first, p.second);
    return r;
}

// An out buffer filled 0xA5 (a) and a twin filled 0x5A (b): a call made into both writes the bytes that differ from
// their fill in either; a value that happens to equal one fill can't read as untouched. off: 0 or 4 (8n+4).
struct Two {
    std::vector<uint8_t> ra, rb;
    uint8_t *a, *b;
    size_t n;
    explicit Two(size_t n_, size_t off = 0) : ra(n_ + 64, 0xA5), rb(n_ + 64, 0x5A), n(n_) {
        a = Align(ra.data()) + off;
        b = Align(rb.data()) + off;
    }
    static uint8_t *Align(uint8_t *p) { return (uint8_t *)(((uintptr_t)p + 15) & ~(uintptr_t)15); }
    bool Written(size_t i) const { return a[i] != 0xA5 || b[i] != 0x5A; }
    std::string WU() const {
        std::vector<std::pair<size_t, size_t>> w, u;
        for (size_t i = 0; i < n; i++) {
            auto &v = Written(i) ? w : u;
            if (!v.empty() && v.back().second == i - 1) v.back().second = i;
            else v.push_back({i, i});
        }
        return "written=" + Ranges(w) + " untouched=" + Ranges(u);
    }
    template <class T> T Get() const {
        T t;
        memcpy(&t, a, sizeof t);
        return t;
    }
};
// One buffer filled 0xA5, for calls that change state (one call only).
struct One {
    std::vector<uint8_t> raw;
    uint8_t *p;
    size_t n;
    explicit One(size_t n_, size_t off = 0) : raw(n_ + 64, 0xA5), n(n_) { p = Two::Align(raw.data()) + off; }
    bool Untouched(size_t from, size_t to) const {
        for (size_t i = from; i < to; i++)
            if (p[i] != 0xA5) return false;
        return true;
    }
};
static const void *const kA5Ptr = (const void *)(uintptr_t)(sizeof(void *) == 8 ? 0xA5A5A5A5A5A5A5A5ull : 0xA5A5A5A5u);
static const char *OutPtr(const void *p) { return p == nullptr ? "null" : p == kA5Ptr ? "untouched" : "set"; }

// A reference held for the case, Released at its end.
template <class T> struct P {
    T *p = nullptr;
    P() = default;
    explicit P(T *x) : p(x) {}
    P(const P &) = delete;
    P &operator=(const P &) = delete;
    ~P() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T *operator->() const { return p; }
    operator T *() const { return p; }
    T **put() {
        reset();
        return &p;
    }
    void **pv() { return (void **)put(); }
};
// The reference count, by an AddRef and a Release.
static ULONG Refs(IUnknown *p) {
    p->AddRef();
    return p->Release();
}
// A vtable slot called through a pointer type of the caller's choice (a null or misaligned riid as a pointer).
template <class R, class... A> static R Call(void *obj, int slot, A... a) {
    return ((R(STDMETHODCALLTYPE *)(void *, A...))(*(void ***)obj)[slot])(obj, a...);
}
static void *Aligned4(std::vector<uint8_t> &raw, const void *src, size_t n) {  // a copy of src at 8n+4
    raw.assign(n + 32, 0);
    uint8_t *p = Two::Align(raw.data()) + 4;
    memcpy(p, src, n);
    return p;
}

// ---- DXMT's private interfaces, as its C++ headers lay them out ----

struct IMTLDXGIAdapterP : IDXGIAdapter4 {
    virtual uint64_t STDMETHODCALLTYPE GetMTLDevice() = 0;  // slot 19 (WMT::Device, one handle)
    virtual UINT STDMETHODCALLTYPE GetLocalD3DKMT() = 0;    // slot 20
};
static const GUID IID_MTLDXGIAdapter = {0xacdf3ef1, 0xb33a, 0x4cb6, {0x97, 0xbd, 0x1c, 0x19, 0x74, 0x82, 0x7e, 0x6d}};
struct GammaRampP {
    float red[1024], green[1024], blue[1024];
    uint64_t version;
};
struct IMTLDXGIOutputP : IDXGIOutput6 {
    virtual const GammaRampP *STDMETHODCALLTYPE GetGammaRamp() = 0;  // slot 29
};
static const GUID IID_MTLDXGIOutput = {0x4c1128cd, 0xb437, 0x4d44, {0x99, 0x58, 0x5d, 0x76, 0x5b, 0xb8, 0x5a, 0x76}};
struct SwapChainFactoryP : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory1 *, HWND, const DXGI_SWAP_CHAIN_DESC1 *,
                                                      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGISwapChain1 **) = 0;
};
static const GUID IID_SwapChainFactory = {0x6c030003, 0x460a, 0x4b19, {0x90, 0x9b, 0x38, 0xf9, 0x7d, 0x20, 0x3e, 0x45}};
static const GUID IID_NvExt = {0xba0af616, 0x4a43, 0x4259, {0x81, 0x5c, 0xdb, 0x3b, 0x89, 0x82, 0x99, 0x05}};
static const GUID IID_WineAdapter = {0x17399d75, 0x964e, 0x4c03, {0x99, 0xf8, 0x9d, 0x4f, 0xd1, 0x96, 0xdd, 0x62}};
static const GUID IID_Factory7 = {0xa4966eed, 0x76db, 0x44da, {0x84, 0xc1, 0xee, 0x9a, 0x7a, 0xfb, 0x20, 0xa8}};
// IIDs nothing answers, one per use so each case's log-once line is its own.
static GUID UnknownIid(unsigned n) { return {0x0badc0de, 0x1234, (unsigned short)n, {1, 2, 3, 4, 5, 6, 7, 8}}; }

// ---- winemetal (the probe's own view of the device) and D3DKMT ----

struct Wm {
    uint64_t (*registryID)(uint64_t);
    uint64_t (*name)(uint64_t);
    uint32_t (*getCString)(uint64_t, char *, uint64_t, uint64_t);
    void (*setWsiHooks)(const void *);
};
static const Wm &Winemetal() {
    static Wm wm = [] {
        Wm w = {};
#ifdef _WIN32
        HMODULE m = LoadLibraryA("winemetal.dll");
        auto get = [m](const char *n) { return m ? (void *)GetProcAddress(m, n) : nullptr; };
#else
        auto get = [](const char *n) { return dlsym(RTLD_DEFAULT, n); };
#endif
        w.registryID = (uint64_t(*)(uint64_t))get("MTLDevice_registryID");
        w.name = (uint64_t(*)(uint64_t))get("MTLDevice_name");
        w.getCString = (uint32_t(*)(uint64_t, char *, uint64_t, uint64_t))get("NSString_getCString");
        w.setWsiHooks = (void (*)(const void *))get("WMTSetWsiHooks");
        return w;
    }();
    return wm;
}
// The LUID DXGI should give: the registry ID, big-endian.
static uint64_t RegistryLuid(uint64_t device) {
    const Wm &w = Winemetal();
    return w.registryID ? __builtin_bswap64(w.registryID(device)) : 0;
}
static uint64_t LuidBits(const LUID &l) {
    uint64_t v;
    memcpy(&v, &l, 8);
    return v;
}
// The device's name in the target's WCHAR encoding, as GetDesc3 writes it (zeroed first).
static bool DeviceName(uint64_t device, WCHAR (&out)[128]) {
    const Wm &w = Winemetal();
    memset(out, 0, sizeof out);
    if (!w.name || !w.getCString) return false;
    w.getCString(w.name(device), (char *)out, sizeof out, sizeof(WCHAR) == 4 ? 0x9c000100 : 10);
    return true;
}
struct KmtOpen {
    LUID luid;
    UINT handle;
};
struct KmtCreateDevice {  // D3DKMT_CREATEDEVICE
    union {
        UINT hAdapter;
        void *pAdapter;
    };
    UINT flags, hDevice;
    void *commandBuffer;
    UINT commandBufferSize;
    void *allocationList;
    UINT allocationListSize;
    void *patchLocationList;
    UINT patchLocationListSize;
};
// Whether a D3DKMT adapter handle is one (a device made on it, then destroyed; natively there is no D3DKMT).
static bool KmtValid(UINT handle) {
#ifdef _WIN32
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    auto create = (LONG(WINAPI *)(KmtCreateDevice *))(void *)GetProcAddress(gdi, "D3DKMTCreateDevice");
    auto destroy = (LONG(WINAPI *)(UINT *))(void *)GetProcAddress(gdi, "D3DKMTDestroyDevice");
    KmtCreateDevice d = {};
    d.hAdapter = handle;
    if (!create || create(&d) != 0) return false;
    if (destroy) destroy(&d.hDevice);
    return true;
#else
    (void)handle;
    return false;
#endif
}
// Whether the probe's own D3DKMTOpenAdapterFromLuid of this LUID succeeds (natively: no D3DKMT, false).
static bool KmtOpens(LUID luid) {
#ifdef _WIN32
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    auto open = (LONG(WINAPI *)(KmtOpen *))(void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
    auto close = (LONG(WINAPI *)(UINT *))(void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    KmtOpen o = {luid, 0};
    if (!open || open(&o) != 0) return false;
    if (close) close(&o.handle);
    return true;
#else
    (void)luid;
    return false;
#endif
}

// ---- The native lane's window system: two fake displays and a window (WMTSetWsiHooks) ----

struct WsiWindowP { uint64_t monitor; uint32_t width, height, flags; };
struct WsiDisplayP { uint64_t monitor; int32_t left, top, right, bottom; uint16_t name[32]; };
struct WsiModeP { uint32_t width, height, num, den; };
struct WsiHooksP {
    int (*window)(uint64_t, WsiWindowP *);
    int (*display)(uint32_t, WsiDisplayP *);
    int (*mode)(uint64_t, uint32_t, WsiModeP *);
    void (*action)(const void *);
};
static const uint64_t kMonitor[2] = {0x10001, 0x10002};
static const WsiDisplayP kDisplay[2] = {{kMonitor[0], 0, 0, 1920, 1080, {}}, {kMonitor[1], 1920, 0, 4480, 1440, {}}};
// Index 0 the current mode, then EnumDisplaySettings' list: unsorted, no two of one size and whole rate.
static const WsiModeP kModes0[] = {{1920, 1080, 60, 1},       {1280, 720, 60, 1},   {1920, 1080, 60, 1},
                                   {1920, 1080, 120000, 1000}, {1024, 768, 75, 1},   {1600, 900, 59940, 1000},
                                   {1920, 1080, 50, 1}};
static const WsiModeP kModes1[] = {{2560, 1440, 144, 1}, {2560, 1440, 60, 1}, {1920, 1080, 60, 1},
                                   {2560, 1440, 144, 1}, {3840, 2160, 30, 1}};
static bool g_zero_den;  // the zero-den case's child: display 2 also lists a 0/0 mode
static const UINT kWindowW = 800, kWindowH = 600;
static void InstallFakeDisplays() {
    static const WsiHooksP hooks = {
        [](uint64_t, WsiWindowP *w) {
            *w = {kMonitor[0], kWindowW, kWindowH, 4};
            return 1;
        },
        [](uint32_t i, WsiDisplayP *d) {
            if (i >= 2) return 0;
            *d = kDisplay[i];
            const char *n = i ? "\\\\.\\DISPLAY2" : "\\\\.\\DISPLAY1";
            for (int k = 0; n[k]; k++) d->name[k] = (uint16_t)n[k];
            return 1;
        },
        [](uint64_t monitor, uint32_t i, WsiModeP *m) {
            const WsiModeP *list = monitor == kMonitor[0] ? kModes0 : kModes1;
            uint32_t n = monitor == kMonitor[0] ? 7 : 5;
            if (monitor != kMonitor[0] && monitor != kMonitor[1]) return 0;
            if (i < n) {
                *m = list[i];
                return 1;
            }
            if (g_zero_den && monitor == kMonitor[1] && i == n) {
                *m = {1280, 720, 0, 0};
                return 1;
            }
            return 0;
        },
        [](const void *) {},
    };
    if (Winemetal().setWsiHooks) Winemetal().setWsiHooks(&hooks);
}

// ---- The displays as the probe sees them: monitors, their info and modes ----

struct Mode { UINT w, h, num, den; };
struct Monitor {
    HMONITOR handle = nullptr;
    WCHAR name[32] = {};
    RECT rect = {};
    Mode current = {};
    std::vector<Mode> modes;  // 32 bits per pixel, progressive (the list DXGI filters for an 8-bit format)
    unsigned interlaced32 = 0, n64 = 0;  // what INTERLACED adds, and the 64-bit modes (FP16's)
};
#ifdef _WIN32
static BOOL CALLBACK CollectMonitor(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    ((std::vector<HMONITOR> *)lp)->push_back(m);
    return TRUE;
}
#endif
static std::vector<Monitor> Monitors() {
    std::vector<Monitor> r;
#ifdef _WIN32
    std::vector<HMONITOR> handles;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, (LPARAM)&handles);
    for (HMONITOR h : handles) {
        Monitor m;
        m.handle = h;
        MONITORINFOEXW info = {};
        info.cbSize = sizeof info;
        GetMonitorInfoW(h, (MONITORINFO *)&info);
        memcpy(m.name, info.szDevice, sizeof m.name);
        m.rect = info.rcMonitor;
        DEVMODEW dm = {};
        dm.dmSize = sizeof dm;
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &dm))
            m.current = {dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency * 1000, 1000};
        for (DWORD i = 0;; i++) {
            dm = {};
            dm.dmSize = sizeof dm;
            if (!EnumDisplaySettingsW(info.szDevice, i, &dm)) break;
            if (dm.dmBitsPerPel == 32 && !(dm.dmDisplayFlags & DM_INTERLACED))
                m.modes.push_back({dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency * 1000, 1000});
            else if (dm.dmBitsPerPel == 32)
                m.interlaced32++;
            else if (dm.dmBitsPerPel == 64 && !(dm.dmDisplayFlags & DM_INTERLACED))
                m.n64++;
        }
        r.push_back(m);
    }
#else
    for (int i = 0; i < 2; i++) {
        Monitor m;
        m.handle = (HMONITOR)(uintptr_t)kMonitor[i];
        const char *n = i ? "\\\\.\\DISPLAY2" : "\\\\.\\DISPLAY1";
        for (int k = 0; n[k]; k++) m.name[k] = (WCHAR)n[k];
        m.rect = {kDisplay[i].left, kDisplay[i].top, kDisplay[i].right, kDisplay[i].bottom};
        const WsiModeP *list = i ? kModes1 : kModes0;
        size_t count = i ? 5 : 7;
        m.current = {list[0].width, list[0].height, list[0].num, list[0].den};
        for (size_t k = 1; k < count; k++) m.modes.push_back({list[k].width, list[k].height, list[k].num, list[k].den});
        r.push_back(m);
    }
#endif
    return r;
}

// The probe's window for the swap chain cases (never shown): its client size is the size a 0x0 swap chain gets.
static HWND Window(UINT *cw = nullptr, UINT *ch = nullptr) {
#ifdef _WIN32
    static HWND hwnd = [] {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "d3d11_dxgi";
        RegisterClassA(&wc);
        RECT r = {0, 0, 640, 360};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        return CreateWindowExA(0, "d3d11_dxgi", "d3d11_dxgi", WS_OVERLAPPEDWINDOW, 0, 0, r.right - r.left,
                               r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    }();
    RECT c = {};
    GetClientRect(hwnd, &c);
    if (cw) *cw = (UINT)(c.right - c.left);
    if (ch) *ch = (UINT)(c.bottom - c.top);
    return hwnd;
#else
    if (cw) *cw = kWindowW;
    if (ch) *ch = kWindowH;
    return (HWND)(uintptr_t)0x4242;
#endif
}

static HMODULE SelfModule() {
#ifdef _WIN32
    return GetModuleHandleA(nullptr);
#else
    return nullptr;
#endif
}

// ---- Objects: the factory, its first adapter and an output, each stubbed under --bites ----

static HRESULT STDMETHODCALLTYPE StubSlot(void *) { return E_NOTIMPL; }
// Under --bites, an object of the stubbed kind gets a copy of its vtable with that slot stubbed (the copy holds
// slots past the interface's: a C++ class's destructors).
static void Stub(void *obj, int kind) {
    if (!obj || kind != g_stub_kind) return;
    void **vtbl = *(void ***)obj;
    void **copy = new void *[48];
    memcpy(copy, vtbl, 48 * sizeof(void *));
    copy[g_stub_slot] = (void *)&StubSlot;
    *(void ***)obj = copy;
}
static IDXGIFactory6 *NewFactory(UINT flags = 0) {
    IDXGIFactory6 *f = nullptr;
    HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory6), (void **)&f);
    if (FAILED(hr) || !f) {
        Fatal("CreateDXGIFactory2", hr);
        return nullptr;
    }
    Stub(f, K_FACTORY);
    return f;
}
static IMTLDXGIAdapterP *NewAdapter(IDXGIFactory6 *f) {
    if (!f) return nullptr;
    IDXGIAdapter1 *a1 = nullptr;
    HRESULT hr = f->EnumAdapters1(0, &a1);
    if (FAILED(hr) || !a1) {
        Fatal("EnumAdapters1", hr);
        return nullptr;
    }
    IMTLDXGIAdapterP *a = nullptr;
    hr = a1->QueryInterface(IID_MTLDXGIAdapter, (void **)&a);
    a1->Release();
    if (FAILED(hr) || !a) {
        Fatal("QueryInterface(IMTLDXGIAdapter)", hr);
        return nullptr;
    }
    Stub(a, K_ADAPTER);
    return a;
}
static IDXGIOutput6 *NewOutput(IDXGIAdapter *a, UINT index = 0) {
    if (!a) return nullptr;
    IDXGIOutput *o = nullptr;
    HRESULT hr = a->EnumOutputs(index, &o);
    if (FAILED(hr) || !o) {
        Fatal("EnumOutputs", hr);
        return nullptr;
    }
    IDXGIOutput6 *o6 = nullptr;
    hr = o->QueryInterface(__uuidof(IDXGIOutput6), (void **)&o6);
    o->Release();
    if (FAILED(hr) || !o6) {
        Fatal("QueryInterface(IDXGIOutput6)", hr);
        return nullptr;
    }
    Stub(o6, K_OUTPUT);
    return o6;
}
// pdata's object makers: a fresh object, holding what it needs.
static IDXGIObject *MakeFactory() { return NewFactory(); }
static IDXGIObject *MakeAdapter() {
    P<IDXGIFactory6> f(NewFactory());
    return NewAdapter(f);
}
static IDXGIObject *MakeOutput() {
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a(NewAdapter(f));
    return NewOutput(a);
}

// ---- Fakes ----

// What the fakes saw: "<tag>+<count>" for an AddRef, "<tag>-<count>" for a Release, in order.
static std::string g_events;
static std::string Events() {
    std::string e = g_events.empty() ? "none" : g_events;
    g_events.clear();
    return e;
}
struct FakeUnk : IUnknown {
    char tag;
    std::atomic<long> refs{1};
    explicit FakeUnk(char t) : tag(t) {}
    virtual ~FakeUnk() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown)) {
            AddRef();
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        long r = ++refs;
        g_events += F("%s%c+%ld", g_events.empty() ? "" : ",", tag, r);
        return (ULONG)r;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        long r = --refs;
        g_events += F("%s%c-%ld", g_events.empty() ? "" : ",", tag, r);
        return (ULONG)r;
    }
};
// A device the factory makes swap chains with: it answers IUnknown and IMTLSwapChainFactory (unless it refuses),
// records the IIDs asked and what CreateSwapChain got, and returns its own swap chain.
struct FakeDevice : SwapChainFactoryP {
    bool refuse;
    std::atomic<long> refs{1};
    std::string asked;
    int calls = 0;
    IDXGIFactory1 *factory = nullptr;
    HWND hwnd = nullptr;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs = {};
    FakeUnk chain{'s'};
    explicit FakeDevice(bool r = false) : refuse(r) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        asked += (asked.empty() ? "" : ",") + (iid == __uuidof(IUnknown)     ? std::string("IUnknown")
                                               : iid == IID_SwapChainFactory ? std::string("IMTLSwapChainFactory")
                                                                              : G(iid));
        if (iid == __uuidof(IUnknown) || (iid == IID_SwapChainFactory && !refuse)) {
            AddRef();
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return (ULONG)--refs; }
    HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory1 *f, HWND h, const DXGI_SWAP_CHAIN_DESC1 *d,
                                              const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fsd,
                                              IDXGISwapChain1 **out) override {
        calls++;
        factory = f;
        hwnd = h;
        desc = *d;
        fs = fsd ? *fsd : DXGI_SWAP_CHAIN_FULLSCREEN_DESC{{9, 9}, (DXGI_MODE_SCANLINE_ORDER)9, (DXGI_MODE_SCALING)9, 9};
        *out = (IDXGISwapChain1 *)(IUnknown *)&chain;
        return S_OK;
    }
    // What the factory handed over: the desc, its fullscreen desc ("default" for the factory's defaults).
    std::string Got(bool clientSize) const {
        UINT cw = 0, ch = 0;
        Window(&cw, &ch);
        std::string size = clientSize && desc.Width == cw && desc.Height == ch ? std::string("client")
                                                                                 : F("%ux%u", desc.Width, desc.Height);
        std::string fsd = fs.RefreshRate.Numerator == 0 && fs.RefreshRate.Denominator == 0 && fs.ScanlineOrdering == 0 &&
                                  fs.Scaling == 0 && fs.Windowed == TRUE
                              ? std::string("default")
                              : F("%u/%u,%u,%u,%u", fs.RefreshRate.Numerator, fs.RefreshRate.Denominator,
                                  (unsigned)fs.ScanlineOrdering, (unsigned)fs.Scaling, (unsigned)fs.Windowed);
        return F("calls=%d asked=%s size=%s desc=%u,%u,%u/%u,%x,%u,%u,%u,%u,%x fs=%s", calls, asked.c_str(),
                 size.c_str(), (unsigned)desc.Format, (unsigned)desc.Stereo, desc.SampleDesc.Count,
                 desc.SampleDesc.Quality, (unsigned)desc.BufferUsage, desc.BufferCount, (unsigned)desc.Scaling,
                 (unsigned)desc.SwapEffect, (unsigned)desc.AlphaMode, desc.Flags, fsd.c_str());
    }
};
// An adapter for d3d10core: CheckInterfaceSupport records what it was asked and refuses only ID3D10Device.
struct FakeAdapter : IDXGIAdapter {
    std::atomic<long> refs{1};
    std::string asked = "none";
    bool umd = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDXGIObject) || iid == __uuidof(IDXGIAdapter)) {
            AddRef();
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return (ULONG)--refs; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *, void *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT, IDXGIOutput **) override { return DXGI_ERROR_NOT_FOUND; }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID guid, LARGE_INTEGER *umd_version) override {
        asked = guid == __uuidof(ID3D10Device) ? "ID3D10Device" : G(guid);
        umd = umd_version != nullptr;
        return guid == __uuidof(ID3D10Device) ? DXGI_ERROR_UNSUPPORTED : S_OK;
    }
};

// ---- Shared case bodies: the private-data trio on any of the three objects (pdata-*) ----

static const GUID kPd1 = {0x9a5f0001, 0x1111, 0x2222, {3, 3, 4, 4, 5, 5, 6, 6}};
static const GUID kPd2 = {0x9a5f0002, 0x1111, 0x2222, {3, 3, 4, 4, 5, 5, 6, 6}};
static IDXGIObject *PdObject(const Case &c) { return ((IDXGIObject * (*)()) c.arg)(); }
static const uint8_t kPdData[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

static void PdSetGet(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    HRESULT set = o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    One b(16);
    UINT size = 16;
    HRESULT hr = o->GetPrivateData(kPd1, &size, b.p);
    Out(hr, F("set=%08x size=%u same=%d tail=%s", (unsigned)set, size, !memcmp(b.p, kPdData, 12),
              b.Untouched(12, 16) ? "untouched" : "written"));
}
static void PdShort(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    One b(16);
    UINT size = 11;
    HRESULT hr = o->GetPrivateData(kPd1, &size, b.p);
    Out(hr, F("size=%u buffer=%s", size, b.Untouched(0, 16) ? "untouched" : "written"));
}
static void PdBig(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    One b(32);
    UINT size = 32;
    HRESULT hr = o->GetPrivateData(kPd1, &size, b.p);
    Out(hr, F("size=%u same=%d tail=%s", size, !memcmp(b.p, kPdData, 12), b.Untouched(12, 32) ? "untouched" : "written"));
}
static void PdNullSize(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    One b(16);
    HRESULT hr = o->GetPrivateData(kPd1, nullptr, b.p);
    Out(hr, F("buffer=%s", b.Untouched(0, 16) ? "untouched" : "written"));
}
static void PdMissing(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    One b(16);
    UINT size = 16;
    HRESULT hr = o->GetPrivateData(kPd2, &size, b.p);
    Out(hr, F("size=%u buffer=%s", size, b.Untouched(0, 16) ? "untouched" : "written"));
}
static void PdErase(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    HRESULT first = o->SetPrivateData(kPd1, sizeof kPdData, nullptr);
    HRESULT second = o->SetPrivateData(kPd1, 0, nullptr);
    UINT size = 16;
    uint8_t b[16];
    HRESULT get = o->GetPrivateData(kPd1, &size, b);
    Out(first, F("again=%08x get=%08x size=%u", (unsigned)second, (unsigned)get, size));
}
static void PdIface(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    FakeUnk x('x'), y('y');
    Events();
    HRESULT set = o->SetPrivateDataInterface(kPd1, &x);
    std::string afterSet = Events();
    void *got[2] = {(void *)kA5Ptr, (void *)kA5Ptr};
    UINT size = sizeof got;
    HRESULT get = o->GetPrivateData(kPd1, &size, got);
    std::string afterGet = Events();
    bool same = got[0] == (IUnknown *)&x;
    if (same) x.Release();
    Events();
    HRESULT replace = o->SetPrivateDataInterface(kPd1, &y);
    std::string afterReplace = Events();
    o.reset();
    std::string afterDie = Events();
    Out(set, F("set=%s get=%08x size=%u same=%d tail=%s getref=%s replace=%08x replaceref=%s dies=%s x=%ld y=%ld",
               afterSet.c_str(), (unsigned)get, size, same, got[1] == kA5Ptr ? "untouched" : "written",
               afterGet.c_str(), (unsigned)replace, afterReplace.c_str(), afterDie.c_str(), x.refs.load(),
               y.refs.load()));
}
static void PdIfaceErase(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    FakeUnk x('x');
    o->SetPrivateDataInterface(kPd1, &x);
    Events();
    HRESULT hr = o->SetPrivateData(kPd1, 0, nullptr);
    Out(hr, F("events=%s x=%ld", Events().c_str(), x.refs.load()));
}
static void PdIfaceDies(const Case &c) {
    FakeUnk x('x');
    {
        P<IDXGIObject> o(PdObject(c));
        if (!o) return;
        o->SetPrivateDataInterface(kPd1, &x);
        Events();
    }
    Out(S_OK, F("events=%s balance=%ld", Events().c_str(), x.refs.load() - 1));
}
static void PdRetype(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    FakeUnk x('x');
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    Events();
    HRESULT toIface = o->SetPrivateDataInterface(kPd1, &x);
    HRESULT toData = o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    uint8_t b[16];
    UINT size = 16;
    HRESULT get = o->GetPrivateData(kPd1, &size, b);
    Out(toIface, F("back=%08x events=%s get=%08x size=%u same=%d", (unsigned)toData, Events().c_str(), (unsigned)get,
                   size, !memcmp(b, kPdData, 12)));
}
static void PdNullIface(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    HRESULT set = o->SetPrivateDataInterface(kPd1, nullptr);
    void *got[2] = {(void *)kA5Ptr, (void *)kA5Ptr};
    UINT size = sizeof got;
    HRESULT get = o->GetPrivateData(kPd1, &size, got);
    Out(set, F("get=%08x size=%s value=%s tail=%s", (unsigned)get, size == sizeof(void *) ? "pointer" : F("%u", size).c_str(),
               OutPtr(got[0]), got[1] == kA5Ptr ? "untouched" : "written"));
}
static void PdNullData(const Case &c) {
    P<IDXGIObject> o(PdObject(c));
    if (!o) return;
    o->SetPrivateData(kPd1, sizeof kPdData, kPdData);
    UINT size = 3;
    HRESULT hr = o->GetPrivateData(kPd1, &size, nullptr);
    FakeUnk x('x');
    o->SetPrivateDataInterface(kPd2, &x);
    UINT isize = 3;
    HRESULT ihr = o->GetPrivateData(kPd2, &isize, nullptr);
    Out(hr, F("size=%u iface=%08x isize=%s x=%ld", size, (unsigned)ihr,
              isize == sizeof(void *) ? "pointer" : F("%u", isize).c_str(), x.refs.load()));
}
// The trio's cases for one object kind (slots 3, 4 and 5).
static bool RegisterPdata(const char *family, int kind, IDXGIObject *(*make)()) {
    struct { int slot; const char *name; CaseFn fn; } list[] = {
        {3, "pdata-set-get", PdSetGet},  {5, "pdata-short", PdShort},           {5, "pdata-big", PdBig},
        {5, "pdata-nullsize", PdNullSize}, {5, "pdata-missing", PdMissing},       {3, "pdata-erase", PdErase},
        {4, "pdata-iface", PdIface},      {3, "pdata-iface-erase", PdIfaceErase}, {4, "pdata-iface-dies", PdIfaceDies},
        {4, "pdata-retype", PdRetype},    {4, "pdata-null-iface", PdNullIface},  {5, "pdata-null-data", PdNullData},
    };
    for (auto &l : list) Register({family, kind, l.slot, l.name, 0, l.fn, (void *)make});
    return true;
}

// ---- The families ----

#include "d3d11_dxgi/dxgif.inc"
#include "d3d11_dxgi/dxgia.inc"
#include "d3d11_dxgi/dxgio.inc"
#include "d3d11_dxgi/d3d10c.inc"

// ---- Children ----

#ifdef _WIN32
static std::wstring Wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::filesystem::path SelfPath() {
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    return std::filesystem::path(std::wstring(buf, n));
}
static std::filesystem::path Root() {
    wchar_t buf[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    return std::filesystem::path(std::wstring(buf, n)) / L"d3d11_dxgi";
}
#else
static std::filesystem::path SelfPath() {
    char buf[4096];
    uint32_t size = sizeof buf;
    _NSGetExecutablePath(buf, &size);
    return std::filesystem::path(buf);
}
static std::filesystem::path Root() {
    const char *t = getenv("TMPDIR");
    return std::filesystem::path(t && *t ? t : "/tmp") / ("d3d11_dxgi." + std::to_string(getpid()));
}
#endif

struct Ran {
    std::string out;
    bool hang = false;
    unsigned long code = 0;  // the exit code, or natively a signal as 0x100 + its number
};
// The child's environment: ours minus every DXMT_ variable, plus its own.
static Ran Spawn(const std::filesystem::path &exe, const std::vector<std::string> &args, const std::filesystem::path &dir,
                 const std::vector<std::pair<std::string, std::string>> &env, int timeout_ms) {
    Ran r;
#ifdef _WIN32
    std::wstring block;
    if (wchar_t *e = GetEnvironmentStringsW()) {
        for (wchar_t *p = e; *p; p += wcslen(p) + 1)
            if (_wcsnicmp(p, L"DXMT_", 5) != 0) block += std::wstring(p) + L'\0';
        FreeEnvironmentStringsW(e);
    }
    for (auto &kv : env) block += Wide(kv.first + "=" + kv.second) + L'\0';
    block += L'\0';
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    for (auto &a : args) cmd += L" " + Wide(a);
    std::wstring app = exe.wstring();
    if (exe != SelfPath()) {
        // ponytail: another file of this exe runs through cmd.exe: an x64 parent under FEX that creates a process
        // from a copy of its own image crashes right after (execute faults in its own code); cmd.exe is native.
        wchar_t sys[MAX_PATH];
        GetSystemDirectoryW(sys, MAX_PATH);
        app = std::wstring(sys) + L"\\cmd.exe";
        cmd = L"cmd.exe /s /c \"" + cmd + L"\"";
    }
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd, wr;
    CreatePipe(&rd, &wr, &sa, 0);
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::wstring dirw = dir.wstring();
    if (!CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
                        &block[0], dirw.c_str(), &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        r.code = 0xdead0000 | (GetLastError() & 0xffff);
        return r;
    }
    CloseHandle(wr);
    CloseHandle(pi.hThread);
    DWORD start = GetTickCount();
    char buf[4096];
    for (;;) {
        DWORD avail = 0, got = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail) {
            if (ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr)) r.out.append(buf, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
            while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got) r.out.append(buf, got);
            break;
        }
        if (GetTickCount() - start > (DWORD)timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            r.hang = true;
            break;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    r.code = code;
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
#else
    std::vector<std::string> envs;
    for (char **e = environ; *e; e++)
        if (strncmp(*e, "DXMT_", 5) != 0) envs.push_back(*e);
    for (auto &kv : env) envs.push_back(kv.first + "=" + kv.second);
    std::vector<char *> envp, argv;
    for (auto &s : envs) envp.push_back((char *)s.c_str());
    envp.push_back(nullptr);
    std::string exes = exe.string();
    argv.push_back((char *)exes.c_str());
    for (auto &a : args) argv.push_back((char *)a.c_str());
    argv.push_back(nullptr);
    int fds[2];
    if (pipe(fds) != 0) {
        r.code = 0xdead0000;
        return r;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    posix_spawn_file_actions_addchdir(&fa, dir.c_str());
    pid_t pid;
    int err = posix_spawn(&pid, exes.c_str(), &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (err) {
        close(fds[0]);
        r.code = 0xdead0000 | (unsigned)err;
        return r;
    }
    auto start = std::chrono::steady_clock::now();
    char buf[4096];
    for (;;) {
        pollfd p = {fds[0], POLLIN, 0};
        int n = poll(&p, 1, 50);
        if (n > 0) {
            ssize_t got = read(fds[0], buf, sizeof buf);
            if (got > 0) {
                r.out.append(buf, (size_t)got);
                continue;
            }
            break;  // EOF
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeout_ms)) {
            kill(pid, SIGKILL);  // our own child, by its exact pid
            r.hang = true;
            break;
        }
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    r.code = WIFSIGNALED(status) ? 0x100u + (unsigned)WTERMSIG(status) : (unsigned)WEXITSTATUS(status);
#endif
    return r;
}

// A child's folder, emptied, with its files; the exe it runs (the probe, or a copy under a profile's name).
static std::filesystem::path Prepare(const std::string &folder, const Child *c, std::filesystem::path *exe) {
    std::filesystem::path dir = Root() / folder;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    *exe = SelfPath();
    if (c && c->file) {
        FILE *f = fopen((dir / c->file).string().c_str(), "wb");
        if (f) {
            fwrite(c->contents, 1, strlen(c->contents), f);
            fclose(f);
        }
    }
    if (c && c->exe) {
        std::filesystem::path copy = dir / c->exe;
        std::filesystem::copy_file(*exe, copy, ec);
        *exe = copy;
    }
    return dir;
}
static std::vector<std::pair<std::string, std::string>> ChildEnv(const std::filesystem::path &dir, const Child *c) {
    std::string d = dir.string();
    std::replace(d.begin(), d.end(), '\\', '/');
    std::vector<std::pair<std::string, std::string>> env = {
        {"DXMT_LOG_LEVEL", "debug"}, {"DXMT_LOG_PATH", d + "/"}, {"DXMT_SHADER_CACHE_PATH", d + "/"}};
    if (c) env.insert(env.end(), c->env.begin(), c->env.end());
    return env;
}
static std::vector<std::string> ChildArgs(const std::string &child, size_t first, size_t last) {
    std::vector<std::string> a = {"--child", child, std::to_string(first), std::to_string(last), g_full ? "full" : "fixed"};
    if (g_ported) a.push_back("--ported");
    if (g_stub_kind >= 0) {
        a.push_back("--stub");
        a.push_back(std::to_string(g_stub_kind));
        a.push_back(std::to_string(g_stub_slot));
    }
    for (auto &f : g_families) a.push_back(f);
    return a;
}

// dxgil: each line of the folder's *_dxgi.log and *_d3d10core.log; in fixed mode, FindClosestMatchingMode1's debug
// line names its modes by relation (P5).
static std::string LogLines(const std::string &child, const std::filesystem::path &dir) {
    if (!FamilyOk("dxgil")) return "";
    std::vector<std::string> files;
    std::error_code ec;
    for (auto &e : std::filesystem::directory_iterator(dir, ec)) {
        std::string n = e.path().filename().string();
        auto ends = [&n](const char *s) { return n.size() >= strlen(s) && n.compare(n.size() - strlen(s), strlen(s), s) == 0; };
        if (ends("_dxgi.log") || ends("_d3d10core.log")) files.push_back(n);
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) return "dxgil " + child + " none\n";
    std::string r;
    for (auto &n : files) {
        std::string text;
        if (FILE *f = fopen((dir / n).string().c_str(), "rb")) {
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, got);
            fclose(f);
        }
        if (text.empty()) r += "dxgil " + child + " " + n + " empty\n";
        size_t at = 0;
        while (at < text.size()) {
            size_t nl = text.find('\n', at);
            std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
            at = nl == std::string::npos ? text.size() : nl + 1;
            size_t m = line.find("DXGI: For mode ");
            if (!g_full && m != std::string::npos && line.find(" found closest mode ") != std::string::npos)
                line = line.substr(0, m) + "DXGI: For mode <target> found closest mode <result>" +
                       (!line.empty() && line.back() == '\r' ? "\r" : "");
            std::string shown;
            for (char ch : line) shown += ch == '\r' ? std::string("<CR>") : ch == 0 ? std::string("<NUL>") : std::string(1, ch);
            r += "dxgil " + child + " " + n + " " + shown + "\n";
        }
    }
    return r;
}

// Every case that runs in the child named, in one process; then its log lines.
static std::string RunChild(const Child &c) {
    size_t first = SIZE_MAX, last = 0;
    for (size_t i = 0; i < Cases().size(); i++)
        if (RunsIn(Cases()[i], c.bit)) first = std::min(first, i), last = std::max(last, i);
    if (first == SIZE_MAX || !PlatformOk(c.only)) return "";
    std::filesystem::path exe;
    std::filesystem::path dir = Prepare(c.name, &c, &exe);
    Ran r = Spawn(exe, ChildArgs(c.name, first, last), dir, ChildEnv(dir, &c), 120000);
    std::string out = r.out;
    if (r.hang) out += "dxgil " + std::string(c.name) + " hang\n";
    else if (r.code) out += F("dxgil %s exit %lx\n", c.name, r.code);
    return out + LogLines(c.name, dir);
}
// One ISOLATE case in a child of its own: its line, or "crashed <code>" / "hang" after 10 s.
static std::string RunIsolated(size_t i) {
    const Case &c = Cases()[i];
    std::string folder = std::string("isolate-") + c.family + "." + kSlots[c.kind][c.slot].method + "." + c.name;
    std::filesystem::path exe;
    std::filesystem::path dir = Prepare(folder, FindChild("default"), &exe);
    Ran r = Spawn(exe, ChildArgs("isolate", i, i), dir, ChildEnv(dir, nullptr), 10000);
    std::string out = r.out;
    if (r.hang || r.code) {
        const SlotName &s = kSlots[c.kind][c.slot];
        out += F("%s %s::%s %s %s\n", c.family, s.iface, s.method, c.name,
                 r.hang ? "hang" : F("crashed %lx", r.code).c_str());
    }
    return out + LogLines(folder, dir);
}

// ---- Modes ----

static void Print(const std::string &s) { fwrite(s.data(), 1, s.size(), stdout); }

static int Run() {
    std::error_code ec;
    std::filesystem::remove_all(Root(), ec);
    for (const Child &c : Children()) Print(RunChild(c));
    for (size_t i = 0; i < Cases().size(); i++)
        if (IsolateRuns(Cases()[i])) Print(RunIsolated(i));
    if (g_native) std::filesystem::remove_all(Root(), ec);
    return 0;
}

static int ChildMain(int argc, char **argv) {
    if (argc < 6) return 2;
    g_child = argv[2];
    size_t first = strtoul(argv[3], nullptr, 10), last = strtoul(argv[4], nullptr, 10);
    g_full = !strcmp(argv[5], "full");
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "--ported")) g_ported = true;
        else if (!strcmp(argv[i], "--stub") && i + 2 < argc) {
            g_stub_kind = atoi(argv[++i]);
            g_stub_slot = atoi(argv[++i]);
        } else g_families.push_back(argv[i]);
    }
    if (g_native) InstallFakeDisplays();
    const Child *c = FindChild(g_child);
    for (size_t i = first; i <= last && i < Cases().size(); i++) {
        const Case &k = Cases()[i];
        if (g_child == "isolate" ? !IsolateRuns(k) : !c || !RunsIn(k, c->bit)) continue;
        g_case = &k;
        g_events.clear();
        k.fn(k);
    }
    fflush(stdout);
    return 0;
}

static int List() {
    std::vector<std::string> names;
    for (const Case &c : Cases()) {
        const SlotName &s = kSlots[c.kind][c.slot];
        std::string flags;
        auto add = [&flags](const char *f) { flags += (flags.empty() ? "" : "|") + std::string(f); };
        if (c.flags & ISOLATE) add("ISOLATE");
        if (c.flags & NOT_ON_C) add("NOT_ON_C");
        if (c.flags & FULL_ONLY) add("FULL_ONLY");
        if (c.flags & PE_ONLY) add("PE_ONLY");
        if (c.flags & NATIVE_ONLY) add("NATIVE_ONLY");
        for (const Child &ch : Children())
            if (c.flags & ch.bit) add(("ENV_" + std::string(ch.name)).c_str());
        std::string slot = c.kind >= K_DXGI ? std::string("x") : std::to_string(c.slot);
        printf("%s %s %s::%s %s %s\n", c.family, slot.c_str(), s.iface, s.method, c.name, flags.empty() ? "-" : flags.c_str());
        std::string key = std::string(c.family) + " " + std::to_string(c.kind) + " " + std::to_string(c.slot) + " " + c.name;
        names.push_back(key);
    }
    std::sort(names.begin(), names.end());
    auto dup = std::adjacent_find(names.begin(), names.end());
    if (dup != names.end()) {
        printf("list duplicate %s\n", dup->c_str());
        return 1;
    }
    return 0;
}

static int Cover() {
    int missing = 0;
    for (int k = 0; k <= K_D3D10CORE; k++)
        for (int s = 0; s < kSlotCount[k]; s++) {
            bool have = false;
            for (const Case &c : Cases()) have |= c.kind == k && c.slot == s;
            if (!have) printf("cover missing %s::%s (%s slot %d)\n", kSlots[k][s].iface, kSlots[k][s].method, kKindName[k], s), missing++;
        }
    printf("cover %s: %zu cases, %d slots missing\n", missing ? "FAIL" : "ok", Cases().size(), missing);
    return missing ? 1 : 0;
}

// --bites (P21): per vtable slot, the default child of its kind's family runs with that slot stubbed; any changed
// line (its dxgil lines included) or a crash bites. Slots no default case can tell apart are excluded with a reason.
static const char *Excluded(int kind, int slot) {
    if (kind == K_ADAPTER && slot == 12) return "only a NOT_ON_C case calls it (the C++ asserts, then returns garbage)";
    if (kind == K_ADAPTER && slot == 13) return "a void no-op (the C++'s assert is compiled out)";
    if (kind == K_ADAPTER && slot == 17) return "a void no-op";
    return nullptr;
}
static int Bites() {
    static const char *const kFamily[] = {"dxgif", "dxgia", "dxgio"};
    const Child &def = *FindChild("default");
    int no = 0;
    for (int k = K_FACTORY; k <= K_OUTPUT; k++) {
        g_families = {kFamily[k], "dxgil"};
        g_stub_kind = -1;
        std::string base = RunChild(def);
        for (int s = 0; s < kSlotCount[k]; s++) {
            const SlotName &n = kSlots[k][s];
            if (const char *why = Excluded(k, s)) {
                printf("bites %s::%s excluded: %s\n", n.iface, n.method, why);
                continue;
            }
            g_stub_kind = k, g_stub_slot = s;
            bool bit = RunChild(def) != base;
            printf("bites %s::%s %s\n", n.iface, n.method, bit ? "yes" : "NO");
            no += !bit;
        }
    }
    g_stub_kind = -1;
    std::error_code ec;
    if (g_native) std::filesystem::remove_all(Root(), ec);
    return no ? 1 : 0;
}

// rate (D4, P16): ns per call after 1000 warm-up calls, over 1 s.
template <class Fn> static void Rate(const char *name, Fn fn) {
    for (int i = 0; i < 1000; i++) fn();
    auto start = std::chrono::steady_clock::now();
    uint64_t calls = 0;
    std::chrono::nanoseconds took{};
    do {
        for (int i = 0; i < 1024; i++) fn();
        calls += 1024;
        took = std::chrono::steady_clock::now() - start;
    } while (took < std::chrono::seconds(1));
    printf("rate %s %.1f\n", name, (double)took.count() / (double)calls);
}
static int RateMain() {
    static const Case rateCase = {"rate", K_ADAPTER, 19, "rate", 0, nullptr, nullptr};
    g_case = &rateCase;
    if (g_native) InstallFakeDisplays();
    P<IDXGIFactory6> f(NewFactory());
    P<IMTLDXGIAdapterP> a(NewAdapter(f));
    if (!a) return 1;
    volatile uint64_t sink = 0;
    Rate("GetMTLDevice", [&] { sink = sink + a->GetMTLDevice(); });
    DXGI_ADAPTER_DESC1 d;
    Rate("GetDesc1", [&] { a->GetDesc1(&d); });
    DXGI_QUERY_VIDEO_MEMORY_INFO q;
    Rate("QueryVideoMemoryInfo", [&] { a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &q); });
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "--child")) return ChildMain(argc, argv);
    std::string mode = argc >= 2 ? argv[1] : "";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--ported")) g_ported = true;
        else g_families.push_back(argv[i]);
    }
    if (mode == "fixed" || mode == "full") {
        g_full = mode == "full";
        return Run();
    }
    if (mode == "rate") return RateMain();
    if (mode == "--list") return List();
    if (mode == "--cover") return Cover();
    if (mode == "--bites") return Bites();
    printf("usage: d3d11_dxgi.exe fixed|full [<family>...] [--ported] | rate | --list | --cover | --bites\n");
    return 2;
}
