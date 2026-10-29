// Every answer DXMT's nvapi64.dll gives, through nvapi_QueryInterface as games reach it, with NVIDIA's own nvapi.h
// for the structs (the Makefile adds DXMT's external/nvapi to the include path). Written against the C++ nvapi64 to
// pin it before the Rust port (DXMTRS step 1b): check.sh runs it with MACNEUTRON_DLSS=1 (DXMT_ENABLE_NVEXT=1).
//   d3d11_nvapi.exe <path to nvapi64.dll> fixed|all
//   fixed
//       Lines that don't depend on the machine, one a call: "<family> <function>(<case>) <status> [<fields>]", family
//       nvsys, nvgpu, nvdisp, nvd3d or nvquery. Values that do (GPU handles, display ids, HDR capabilities) are
//       printed as relations: "yes" when the LUID nvapi gives is DXGI's adapter's, and so on. A struct the call
//       writes is a 0xA5-filled buffer of its largest version; "tail=untouched" says the bytes past the version
//       passed kept their 0xA5.
//   all
//       The same, plus "nvdump <function>(<case>) <status> <hex>" after each call that writes a buffer: the whole
//       buffer, for a diff between two nvapi64 builds on the same machine.
// A function nvapi_QueryInterface doesn't answer prints "<family> <function> missing".
#include "d3d11_common.hpp"
#include <type_traits>
#include <utility>
#include <initializer_list>
#include "nvapi_lite_common.h"  // first, then nvapi.h with empty SAL, as DXMT's nvapi.cpp includes them
#define __NVAPI_EMPTY_SAL
#include "nvapi.h"
#undef __NVAPI_EMPTY_SAL
#include "nvShaderExtnEnums.h"

static bool g_all;
static void *(*g_query)(NvU32);
alignas(16) static unsigned char g_buf[16384];

#define FN(name, id) auto name = (decltype(&NvAPI_##name))g_query(id)
#define HAVE(family, name) if (!name) { printf(family " " #name " missing\n"); } else

// A 0xA5-filled g_buf as T with `version` set.
template <class T> static T *Fill(NvU32 version) {
    static_assert(sizeof(T) <= sizeof(g_buf));
    memset(g_buf, 0xA5, sizeof(g_buf));
    auto p = (T *)g_buf;
    p->version = version;
    return p;
}
template <class T> static NvU32 Wrong() { return (NvU32)(sizeof(T) | (9 << 16)); }
static bool Untouched(size_t from, size_t to) {
    for (size_t i = from; i < to; i++)
        if (g_buf[i] != 0xA5) return false;
    return true;
}
static const char *Tail(size_t from, size_t to) { return Untouched(from, to) ? "untouched" : "written"; }
static void Dump(const char *call, int s, size_t n) {
    if (!g_all) return;
    printf("nvdump %s %d ", call, s);
    for (size_t i = 0; i < n; i++) printf("%02x", g_buf[i]);
    printf("\n");
}
static const char *Yes(bool b) { return b ? "yes" : "no"; }

// DXMT's private D3D11 extensions as d3d11_interfaces.hpp declares them: the C++ run validates this copy.
struct ContextExt : IUnknown {
    virtual void STDMETHODCALLTYPE TemporalUpscale(const void *desc) = 0;
    virtual void STDMETHODCALLTYPE BeginUAVOverlap() = 0;
    virtual void STDMETHODCALLTYPE EndUAVOverlap() = 0;
};
struct DeviceExt : IUnknown {
    virtual void STDMETHODCALLTYPE SetShaderExtensionSlot(UINT slot) = 0;
};
static const GUID IID_ContextExt = {0x43ace3ce, 0x1956, 0x448b, {0xa4, 0xeb, 0xae, 0xe6, 0x8b, 0xde, 0xb2, 0x83}};
static const GUID IID_DeviceExt = {0xefc77ae6, 0x2179, 0x4c0a, {0xb8, 0x44, 0x76, 0x61, 0xca, 0x0d, 0xcd, 0xe7}};

// Fakes that answer IUnknown and their one extension, count the extension's calls and balance their references.
struct FakeContext : ContextExt {
    LONG refs = 1;
    int begins = 0, ends = 0, upscales = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid != __uuidof(IUnknown) && iid != IID_ContextExt) { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        *out = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    void STDMETHODCALLTYPE TemporalUpscale(const void *) override { upscales++; }
    void STDMETHODCALLTYPE BeginUAVOverlap() override { begins++; }
    void STDMETHODCALLTYPE EndUAVOverlap() override { ends++; }
};
struct FakeDevice : DeviceExt {
    LONG refs = 1;
    UINT slot = 0xa5a5a5a5;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (iid != __uuidof(IUnknown) && iid != IID_DeviceExt) { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        *out = this;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { return --refs; }
    void STDMETHODCALLTYPE SetShaderExtensionSlot(UINT s) override { slot = s; }
};

static ID3D11Device *device;
static ID3D11DeviceContext *ctx;
static LUID g_luid;           // the D3D11 device's adapter's, from DXGI
static NvU32 g_primary;       // GPU_GetConnectedDisplayIds' first id
static NvPhysicalGpuHandle g_gpu;

static void Sys() {
    FN(Unload, 0xd22bdd7e);
    FN(Initialize, 0x0150e828);
    HAVE("nvsys", Unload) printf("nvsys Unload(before) %d\n", Unload());
    HAVE("nvsys", Initialize) {
        printf("nvsys Initialize(1) %d\n", Initialize());
        printf("nvsys Initialize(2) %d\n", Initialize());
    }
    HAVE("nvsys", Unload) {
        printf("nvsys Unload(1) %d\n", Unload());
        printf("nvsys Unload(2) %d\n", Unload());
        printf("nvsys Unload(past) %d\n", Unload());
    }
    HAVE("nvsys", Initialize) printf("nvsys Initialize(3) %d\n", Initialize());

    FN(SYS_GetDriverAndBranchVersion, 0x2926aaad);
    HAVE("nvsys", SYS_GetDriverAndBranchVersion) {
        NvU32 v = 0xa5a5a5a5;
        memset(g_buf, 0xA5, sizeof(g_buf));
        int s = SYS_GetDriverAndBranchVersion(&v, (char *)g_buf);
        printf("nvsys SYS_GetDriverAndBranchVersion %d %u %s\n", s, (unsigned)v, (char *)g_buf);
        Dump("SYS_GetDriverAndBranchVersion", s, sizeof(NvAPI_ShortString));
        printf("nvsys SYS_GetDriverAndBranchVersion(null version) %d\n",
               SYS_GetDriverAndBranchVersion(nullptr, (char *)g_buf));
        printf("nvsys SYS_GetDriverAndBranchVersion(null string) %d\n", SYS_GetDriverAndBranchVersion(&v, nullptr));
    }
    FN(GetDisplayDriverVersion, 0xf951a4d1);
    HAVE("nvsys", GetDisplayDriverVersion) {
        printf("nvsys GetDisplayDriverVersion(null) %d\n", GetDisplayDriverVersion(nullptr, nullptr));
        auto p = Fill<NV_DISPLAY_DRIVER_VERSION>(Wrong<NV_DISPLAY_DRIVER_VERSION>());
        int s = GetDisplayDriverVersion(nullptr, p);
        printf("nvsys GetDisplayDriverVersion(wrong) %d tail=%s\n", s, Tail(4, sizeof(*p)));
        p = Fill<NV_DISPLAY_DRIVER_VERSION>(NV_DISPLAY_DRIVER_VERSION_VER);
        s = GetDisplayDriverVersion(nullptr, p);
        printf("nvsys GetDisplayDriverVersion %d %u %u %s %s\n", s, (unsigned)p->drvVersion, (unsigned)p->bldChangeListNum,
               p->szBuildBranchString, p->szAdapterString);
        Dump("GetDisplayDriverVersion", s, sizeof(*p));
    }
    FN(GetInterfaceVersionString, 0x01053fa5);
    HAVE("nvsys", GetInterfaceVersionString) {
        memset(g_buf, 0xA5, sizeof(g_buf));
        int s = GetInterfaceVersionString((char *)g_buf);
        printf("nvsys GetInterfaceVersionString %d %s\n", s, (char *)g_buf);
        Dump("GetInterfaceVersionString", s, sizeof(NvAPI_ShortString));
        printf("nvsys GetInterfaceVersionString(null) %d\n", GetInterfaceVersionString(nullptr));
    }
    FN(GetErrorMessage, 0x6c2d048c);
    HAVE("nvsys", GetErrorMessage) {
        memset(g_buf, 0xA5, sizeof(g_buf));
        int s = GetErrorMessage(NVAPI_INVALID_ARGUMENT, (char *)g_buf);
        printf("nvsys GetErrorMessage %d \"%s\" tail=%s\n", s, (char *)g_buf, Tail(1, sizeof(NvAPI_ShortString)));
        Dump("GetErrorMessage", s, sizeof(NvAPI_ShortString));
    }
    FN(Mosaic_GetDisplayViewportsByResolution, 0xdc6dc8d3);
    HAVE("nvsys", Mosaic_GetDisplayViewportsByResolution) {
        NV_RECT viewports[NV_MOSAIC_MAX_DISPLAYS];
        NvU8 bezel = 0xa5;
        printf("nvsys Mosaic_GetDisplayViewportsByResolution %d\n",
               Mosaic_GetDisplayViewportsByResolution(0, 1920, 1080, viewports, &bezel));
    }
    FN(Stereo_IsEnabled, 0x348ff8e1);
    HAVE("nvsys", Stereo_IsEnabled) {
        NvU8 on = 0xa5;
        printf("nvsys Stereo_IsEnabled(null) %d\n", Stereo_IsEnabled(nullptr));
        printf("nvsys Stereo_IsEnabled %d %u\n", Stereo_IsEnabled(&on), on);
    }
    FN(Stereo_SetDriverMode, 0x5e8f0bec);
    HAVE("nvsys", Stereo_SetDriverMode)
        printf("nvsys Stereo_SetDriverMode %d\n", Stereo_SetDriverMode(NVAPI_STEREO_DRIVER_MODE_AUTOMATIC));
    FN(DRS_CreateSession, 0x0694d52e);
    HAVE("nvsys", DRS_CreateSession) {
        NvDRSSessionHandle h = (NvDRSSessionHandle)0xa5a5a5a5;
        printf("nvsys DRS_CreateSession(null) %d\n", DRS_CreateSession(nullptr));
        int s = DRS_CreateSession(&h);
        printf("nvsys DRS_CreateSession %d handle=%s\n", s, h == (NvDRSSessionHandle)0xa5a5a5a5 ? "untouched" : "written");
    }
    FN(DRS_LoadSettings, 0x375dbd6b);
    HAVE("nvsys", DRS_LoadSettings) printf("nvsys DRS_LoadSettings %d\n", DRS_LoadSettings(nullptr));
}

// The GPU queries a game makes with a handle: null handle, null output, then a handle and a buffer.
template <class F> static void HandleQuery(const char *name, F call) {
    printf("nvgpu %s(null handle) %d\n", name, call(nullptr, g_buf));
    printf("nvgpu %s(null) %d\n", name, call(g_gpu, nullptr));
    memset(g_buf, 0xA5, sizeof(g_buf));
    int s = call(g_gpu, g_buf);
    printf("nvgpu %s %d buffer=%s\n", name, s, Tail(0, sizeof(g_buf)));
}

static void Gpu() {
    NvPhysicalGpuHandle physical[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvLogicalGpuHandle logical[NVAPI_MAX_LOGICAL_GPUS] = {};
    NvU32 np = 0xa5a5a5a5, nl = 0xa5a5a5a5;
    FN(EnumPhysicalGPUs, 0xe5ac921f);
    HAVE("nvgpu", EnumPhysicalGPUs) {
        printf("nvgpu EnumPhysicalGPUs(null array) %d\n", EnumPhysicalGPUs(nullptr, &np));
        printf("nvgpu EnumPhysicalGPUs(null count) %d\n", EnumPhysicalGPUs(physical, nullptr));
        int s = EnumPhysicalGPUs(physical, &np);
        printf("nvgpu EnumPhysicalGPUs %d count>=1 %s\n", s, Yes(s == 0 && np >= 1 && np <= NVAPI_MAX_PHYSICAL_GPUS));
        if (g_all) printf("nvdump EnumPhysicalGPUs %d count %u first %p\n", s, (unsigned)np, (void *)physical[0]);
        g_gpu = physical[0];
    }
    FN(EnumLogicalGPUs, 0x48b3ea59);
    HAVE("nvgpu", EnumLogicalGPUs) {
        printf("nvgpu EnumLogicalGPUs(null array) %d\n", EnumLogicalGPUs(nullptr, &nl));
        printf("nvgpu EnumLogicalGPUs(null count) %d\n", EnumLogicalGPUs(logical, nullptr));
        int s = EnumLogicalGPUs(logical, &nl);
        bool same = nl == np;
        for (NvU32 i = 0; same && i < nl && i < NVAPI_MAX_LOGICAL_GPUS; i++) same = (void *)logical[i] == (void *)physical[i];
        printf("nvgpu EnumLogicalGPUs %d same-as-physical %s\n", s, Yes(s == 0 && same));
    }
    FN(GetPhysicalGPUsFromDisplay, 0x34ef9506);
    HAVE("nvgpu", GetPhysicalGPUsFromDisplay) {
        NvPhysicalGpuHandle from[NVAPI_MAX_PHYSICAL_GPUS] = {};
        NvU32 n = 0xa5a5a5a5;
        auto display = (NvDisplayHandle)1;  // any non-null handle: the C++ uses none
        printf("nvgpu GetPhysicalGPUsFromDisplay(null display) %d\n", GetPhysicalGPUsFromDisplay(nullptr, from, &n));
        printf("nvgpu GetPhysicalGPUsFromDisplay(null array) %d\n", GetPhysicalGPUsFromDisplay(display, nullptr, &n));
        printf("nvgpu GetPhysicalGPUsFromDisplay(null count) %d\n", GetPhysicalGPUsFromDisplay(display, from, nullptr));
        int s = GetPhysicalGPUsFromDisplay(display, from, &n);
        printf("nvgpu GetPhysicalGPUsFromDisplay %d same-as-physical %s\n", s,
               Yes(s == 0 && n == np && (void *)from[0] == (void *)physical[0]));
    }
    FN(GPU_GetArchInfo, 0xd8265d24);
    HAVE("nvgpu", GPU_GetArchInfo) {
        printf("nvgpu GPU_GetArchInfo(null handle) %d\n", GPU_GetArchInfo(nullptr, Fill<NV_GPU_ARCH_INFO>(NV_GPU_ARCH_INFO_VER_2)));
        printf("nvgpu GPU_GetArchInfo(null) %d\n", GPU_GetArchInfo(g_gpu, nullptr));
        auto p = Fill<NV_GPU_ARCH_INFO>(Wrong<NV_GPU_ARCH_INFO_V2>());
        int s = GPU_GetArchInfo(g_gpu, p);
        printf("nvgpu GPU_GetArchInfo(wrong) %d tail=%s\n", s, Tail(4, sizeof(*p)));
        for (NvU32 v : {(NvU32)NV_GPU_ARCH_INFO_VER_1, (NvU32)NV_GPU_ARCH_INFO_VER_2}) {
            p = Fill<NV_GPU_ARCH_INFO>(v);
            s = GPU_GetArchInfo(g_gpu, p);
            char call[64];
            snprintf(call, sizeof(call), "GPU_GetArchInfo(v%u)", (unsigned)v >> 16);
            printf("nvgpu %s %d %#x %#x %#x tail=%s\n", call, s, (unsigned)p->architecture, (unsigned)p->implementation, (unsigned)p->revision,
                   Tail(16, sizeof(*p)));
            Dump(call, s, sizeof(*p));
        }
    }
    FN(GPU_GetAllClockFrequencies, 0xdcb616c3);
    HAVE("nvgpu", GPU_GetAllClockFrequencies)
        HandleQuery("GPU_GetAllClockFrequencies", [&](auto h, void *p) { return GPU_GetAllClockFrequencies(h, (NV_GPU_CLOCK_FREQUENCIES *)p); });
    FN(GPU_GetGpuCoreCount, 0xc7026a87);
    HAVE("nvgpu", GPU_GetGpuCoreCount)
        HandleQuery("GPU_GetGpuCoreCount", [&](auto h, void *p) { return GPU_GetGpuCoreCount(h, (NvU32 *)p); });
    FN(GPU_GetBusId, 0x1be0b8e5);
    HAVE("nvgpu", GPU_GetBusId) HandleQuery("GPU_GetBusId", [&](auto h, void *p) { return GPU_GetBusId(h, (NvU32 *)p); });
    FN(GPU_GetBusType, 0x1bb18724);
    HAVE("nvgpu", GPU_GetBusType)
        HandleQuery("GPU_GetBusType", [&](auto h, void *p) { return GPU_GetBusType(h, (NV_GPU_BUS_TYPE *)p); });
    FN(GPU_GetMemoryInfo, 0x07f9b368);
    HAVE("nvgpu", GPU_GetMemoryInfo)
        HandleQuery("GPU_GetMemoryInfo", [&](auto h, void *p) { return GPU_GetMemoryInfo(h, (NV_DISPLAY_DRIVER_MEMORY_INFO *)p); });
    FN(GPU_GetMemoryInfoEx, 0xc0599498);
    HAVE("nvgpu", GPU_GetMemoryInfoEx)
        HandleQuery("GPU_GetMemoryInfoEx", [&](auto h, void *p) { return GPU_GetMemoryInfoEx(h, (NV_GPU_MEMORY_INFO_EX *)p); });
    FN(GPU_GetPstates20, 0x6ff81213);
    HAVE("nvgpu", GPU_GetPstates20)
        HandleQuery("GPU_GetPstates20", [&](auto h, void *p) { return GPU_GetPstates20(h, (NV_GPU_PERF_PSTATES20_INFO *)p); });
    FN(GPU_GetFullName, 0xceee8e9f);
    HAVE("nvgpu", GPU_GetFullName) {
        memset(g_buf, 0xA5, sizeof(g_buf));
        int s = GPU_GetFullName(g_gpu, (char *)g_buf);
        printf("nvgpu GPU_GetFullName %d %s\n", s, (char *)g_buf);
        Dump("GPU_GetFullName", s, sizeof(NvAPI_ShortString));
        printf("nvgpu GPU_GetFullName(null) %d\n", GPU_GetFullName(g_gpu, nullptr));
    }
    uint64_t swapped = __builtin_bswap64((uint64_t)(uintptr_t)g_gpu);
    FN(GPU_GetAdapterIdFromPhysicalGpu, 0x0ff07fde);
    HAVE("nvgpu", GPU_GetAdapterIdFromPhysicalGpu) {
        LUID luid;
        memset(&luid, 0xA5, sizeof(luid));
        printf("nvgpu GPU_GetAdapterIdFromPhysicalGpu(null) %d\n", GPU_GetAdapterIdFromPhysicalGpu(g_gpu, nullptr));
        printf("nvgpu GPU_GetAdapterIdFromPhysicalGpu(unknown handle) %d\n",
               GPU_GetAdapterIdFromPhysicalGpu((NvPhysicalGpuHandle)0x1234, &luid));
        int s = GPU_GetAdapterIdFromPhysicalGpu(g_gpu, &luid);
        printf("nvgpu GPU_GetAdapterIdFromPhysicalGpu %d dxgi %s bswap %s\n", s,
               Yes(!memcmp(&luid, &g_luid, sizeof(luid))), Yes(!memcmp(&luid, &swapped, sizeof(luid))));
    }
    FN(GPU_GetLogicalGpuInfo, 0x842b066e);
    HAVE("nvgpu", GPU_GetLogicalGpuInfo) {
        static LUID luid;
        printf("nvgpu GPU_GetLogicalGpuInfo(null) %d\n", GPU_GetLogicalGpuInfo((NvLogicalGpuHandle)g_gpu, nullptr));
        auto p = Fill<NV_LOGICAL_GPU_DATA>(NV_LOGICAL_GPU_DATA_VER1);
        printf("nvgpu GPU_GetLogicalGpuInfo(unknown handle) %d\n", GPU_GetLogicalGpuInfo((NvLogicalGpuHandle)0x1234, p));
        p = Fill<NV_LOGICAL_GPU_DATA>(Wrong<NV_LOGICAL_GPU_DATA>());
        int s = GPU_GetLogicalGpuInfo((NvLogicalGpuHandle)g_gpu, p);
        printf("nvgpu GPU_GetLogicalGpuInfo(wrong) %d tail=%s\n", s, Tail(4, sizeof(*p)));
        p = Fill<NV_LOGICAL_GPU_DATA>(NV_LOGICAL_GPU_DATA_VER1);
        memset(&luid, 0xA5, sizeof(luid));
        p->pOSAdapterId = &luid;
        s = GPU_GetLogicalGpuInfo((NvLogicalGpuHandle)g_gpu, p);
        bool rest = true;
        for (int i = 1; i < NVAPI_MAX_PHYSICAL_GPUS; i++) rest = rest && p->physicalGpuHandles[i] == nullptr;
        printf("nvgpu GPU_GetLogicalGpuInfo %d count %u handle %s others-null %s dxgi %s reserved=%s\n", s,
               (unsigned)p->physicalGpuCount, Yes((void *)p->physicalGpuHandles[0] == (void *)g_gpu), Yes(rest),
               Yes(!memcmp(&luid, &g_luid, sizeof(luid))),
               Tail(offsetof(NV_LOGICAL_GPU_DATA, reserved), sizeof(*p)));
        Dump("GPU_GetLogicalGpuInfo", s, sizeof(*p));
    }
}

// The primary monitor's GDI name, as Windows reports it ("\\.\DISPLAY1").
static std::string PrimaryName() {
    std::string name;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM out) -> BOOL {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(m, (MONITORINFO *)&info) || !(info.dwFlags & MONITORINFOF_PRIMARY)) return TRUE;
        for (WCHAR *c = info.szDevice; *c; c++) ((std::string *)out)->push_back((char)*c);
        return FALSE;
    }, (LPARAM)&name);
    return name;
}

static void HdrGet(decltype(&NvAPI_Disp_HdrColorControl) control, NvU32 version, const char *what, bool fields) {
    auto p = Fill<NV_HDR_COLOR_DATA_V2>(version);
    p->cmd = NV_HDR_CMD_GET;
    int s = control(g_primary, (NV_HDR_COLOR_DATA *)p);
    auto &m = p->mastering_display_data;
    char call[64];
    snprintf(call, sizeof(call), "Disp_HdrColorControl(get v%u %s)", (unsigned)version >> 16, what);
    printf("nvdisp %s %d mode %d", call, s, p->hdrMode);
    if (fields)
        printf(" %u %u %u %u %u %u %u %u %u %u %u %u", m.displayPrimary_x0, m.displayPrimary_y0, m.displayPrimary_x1,
               m.displayPrimary_y1, m.displayPrimary_x2, m.displayPrimary_y2, m.displayWhitePoint_x,
               m.displayWhitePoint_y, m.max_display_mastering_luminance, m.min_display_mastering_luminance,
               m.max_content_light_level, m.max_frame_average_light_level);
    if (version == NV_HDR_COLOR_DATA_VER2)
        printf(" format %d range %d bpc %d", p->hdrColorFormat, p->hdrDynamicRange, p->hdrBpc);
    else
        printf(" tail=%s", Tail(sizeof(NV_HDR_COLOR_DATA_V1), sizeof(*p)));
    printf("\n");
    Dump(call, s, sizeof(*p));
}

static void HdrSet(decltype(&NvAPI_Disp_HdrColorControl) control, NvU32 version, NV_HDR_MODE mode) {
    auto p = Fill<NV_HDR_COLOR_DATA_V2>(version);
    p->cmd = NV_HDR_CMD_SET;
    p->hdrMode = mode;
    p->static_metadata_descriptor_id = NV_STATIC_METADATA_TYPE_1;
    p->mastering_display_data = {35400, 14600, 8500, 39850, 6550, 2300, 15635, 16450, 1000, 1, 1000, 400};
    int s = control(g_primary, (NV_HDR_COLOR_DATA *)p);
    char call[64];
    snprintf(call, sizeof(call), "Disp_HdrColorControl(set v%u mode %d)", (unsigned)version >> 16, mode);
    printf("nvdisp %s %d tail=%s\n", call, s, Tail(sizeof(NV_HDR_COLOR_DATA_V1), sizeof(*p)));
    Dump(call, s, sizeof(*p));
}

static void Disp() {
    FN(GPU_GetConnectedDisplayIds, 0x0078dba2);
    HAVE("nvdisp", GPU_GetConnectedDisplayIds) {
        NvU32 n = 0xa5a5a5a5;
        auto ids = (NV_GPU_DISPLAYIDS *)g_buf;
        printf("nvdisp GPU_GetConnectedDisplayIds(null count) %d\n", GPU_GetConnectedDisplayIds(g_gpu, ids, nullptr, 0));
        int s = GPU_GetConnectedDisplayIds(g_gpu, nullptr, &n, 0);
        printf("nvdisp GPU_GetConnectedDisplayIds(null array) %d count=%s\n", s, n == 0xa5a5a5a5 ? "untouched" : "written");
        n = 0;
        printf("nvdisp GPU_GetConnectedDisplayIds(no room) %d\n", GPU_GetConnectedDisplayIds(g_gpu, Fill<NV_GPU_DISPLAYIDS>(NV_GPU_DISPLAYIDS_VER2), &n, 0));
        n = 2;
        ids = Fill<NV_GPU_DISPLAYIDS>(Wrong<NV_GPU_DISPLAYIDS>());
        ids[1].version = NV_GPU_DISPLAYIDS_VER2;
        printf("nvdisp GPU_GetConnectedDisplayIds(wrong) %d\n", GPU_GetConnectedDisplayIds(g_gpu, ids, &n, 0));
        unsigned at = 0;  // labels by position: NVIDIA's VER2 carries version number 3
        for (NvU32 v : {(NvU32)NV_GPU_DISPLAYIDS_VER1, (NvU32)NV_GPU_DISPLAYIDS_VER2}) {
            ids = Fill<NV_GPU_DISPLAYIDS>(v);
            ids[1].version = v;
            n = 2;
            s = GPU_GetConnectedDisplayIds(g_gpu, ids, &n, 0);
            char call[64];
            snprintf(call, sizeof(call), "GPU_GetConnectedDisplayIds(v%u)", ++at);
            printf("nvdisp %s %d count>=1 %s first active %u connected %u physical %u type %d\n", call, s, Yes(n >= 1),
                   ids[0].isActive, ids[0].isConnected, ids[0].isPhysicallyConnected, ids[0].connectorType);
            Dump(call, s, 2 * sizeof(NV_GPU_DISPLAYIDS));
            g_primary = ids[0].displayId;
        }
    }
    FN(DISP_GetDisplayIdByDisplayName, 0xae457190);
    HAVE("nvdisp", DISP_GetDisplayIdByDisplayName) {
        NvU32 id = 0xa5a5a5a5;
        std::string name = PrimaryName();
        int s = DISP_GetDisplayIdByDisplayName(name.c_str(), &id);
        printf("nvdisp DISP_GetDisplayIdByDisplayName(primary) %d same-as-connected %s\n", s, Yes(id == g_primary));
        id = 0xa5a5a5a5;
        s = DISP_GetDisplayIdByDisplayName("\\\\.\\DISPLAY99", &id);
        printf("nvdisp DISP_GetDisplayIdByDisplayName(unknown) %d id=%s\n", s, id == 0xa5a5a5a5 ? "untouched" : "written");
    }
    FN(DISP_GetGDIPrimaryDisplayId, 0x1e9d8a31);
    HAVE("nvdisp", DISP_GetGDIPrimaryDisplayId) {
        NvU32 id = 0xa5a5a5a5;
        printf("nvdisp DISP_GetGDIPrimaryDisplayId(null) %d\n", DISP_GetGDIPrimaryDisplayId(nullptr));
        int s = DISP_GetGDIPrimaryDisplayId(&id);
        printf("nvdisp DISP_GetGDIPrimaryDisplayId %d id=%s\n", s, id == 0xa5a5a5a5 ? "untouched" : "written");
    }
    FN(EnumNvidiaDisplayHandle, 0x9abdd40d);
    HAVE("nvdisp", EnumNvidiaDisplayHandle) {
        auto h = (NvDisplayHandle)0xa5a5a5a5;
        printf("nvdisp EnumNvidiaDisplayHandle(null) %d\n", EnumNvidiaDisplayHandle(0, nullptr));
        int s = EnumNvidiaDisplayHandle(0, &h);
        printf("nvdisp EnumNvidiaDisplayHandle(0) %d handle=%s\n", s, h == (NvDisplayHandle)0xa5a5a5a5 ? "untouched" : "written");
        printf("nvdisp EnumNvidiaDisplayHandle(99) %d\n", EnumNvidiaDisplayHandle(99, &h));
    }
    FN(DISP_GetMonitorCapabilities, 0x3b05c7e1);
    HAVE("nvdisp", DISP_GetMonitorCapabilities) {
        printf("nvdisp DISP_GetMonitorCapabilities(null) %d\n", DISP_GetMonitorCapabilities(g_primary, nullptr));
        auto p = Fill<NV_MONITOR_CAPABILITIES>(NV_MONITOR_CAPABILITIES_VER1);
        int s = DISP_GetMonitorCapabilities(g_primary, p);
        printf("nvdisp DISP_GetMonitorCapabilities %d tail=%s\n", s, Tail(4, sizeof(*p)));
    }
    FN(Disp_GetHdrCapabilities, 0x84f2a8df);
    HAVE("nvdisp", Disp_GetHdrCapabilities) {
        auto p = Fill<NV_HDR_CAPABILITIES_V3>(Wrong<NV_HDR_CAPABILITIES_V3>());
        int s = Disp_GetHdrCapabilities(g_primary, (NV_HDR_CAPABILITIES *)p);
        printf("nvdisp Disp_GetHdrCapabilities(wrong) %d tail=%s\n", s, Tail(4, sizeof(*p)));
        const std::pair<NvU32, size_t> versions[] = {{NV_HDR_CAPABILITIES_VER1, sizeof(NV_HDR_CAPABILITIES_V1)},
                                                     {NV_HDR_CAPABILITIES_VER2, sizeof(NV_HDR_CAPABILITIES_V2)},
                                                     {NV_HDR_CAPABILITIES_VER3, sizeof(NV_HDR_CAPABILITIES_V3)}};
        for (auto [v, size] : versions) {
            p = Fill<NV_HDR_CAPABILITIES_V3>(v);
            s = Disp_GetHdrCapabilities(g_primary, (NV_HDR_CAPABILITIES *)p);
            char call[64];
            snprintf(call, sizeof(call), "Disp_GetHdrCapabilities(v%u)", (unsigned)v >> 16);
            printf("nvdisp %s %d version-kept %s min %u tail=%s\n", call, s, Yes(p->version == v),
                   p->display_data.desired_content_min_luminance, Tail(size, sizeof(*p)));
            Dump(call, s, sizeof(*p));
        }
    }
    FN(Disp_HdrColorControl, 0x351da224);
    HAVE("nvdisp", Disp_HdrColorControl) {
        auto p = Fill<NV_HDR_COLOR_DATA_V2>(Wrong<NV_HDR_COLOR_DATA_V2>());
        p->cmd = NV_HDR_CMD_GET;
        printf("nvdisp Disp_HdrColorControl(wrong) %d tail=%s\n", Disp_HdrColorControl(g_primary, (NV_HDR_COLOR_DATA *)p),
               Tail(8, sizeof(*p)));  // past version and cmd
        HdrGet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, "before", false);
        for (NvU32 v : {(NvU32)NV_HDR_COLOR_DATA_VER1, (NvU32)NV_HDR_COLOR_DATA_VER2}) {
            HdrSet(Disp_HdrColorControl, v, NV_HDR_MODE_UHDA);
            HdrGet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER1, "uhda", true);
            HdrGet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, "uhda", true);
        }
        HdrSet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, NV_HDR_MODE_UHDA_PASSTHROUGH);
        HdrGet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, "passthrough", true);
        HdrSet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, NV_HDR_MODE_OFF);
        HdrGet(Disp_HdrColorControl, NV_HDR_COLOR_DATA_VER2, "off", false);
    }
}

// Calls taking a device and a versioned struct: null device, null struct, a wrong version, each version.
template <class Big, class F>
static void Versioned(const char *name, F call, std::initializer_list<std::pair<NvU32, size_t>> versions) {
    printf("nvd3d %s(null device) %d\n", name, call(nullptr, Fill<Big>(versions.begin()->first)));
    printf("nvd3d %s(null) %d\n", name, call(device, (Big *)nullptr));
    int s = call(device, Fill<Big>(Wrong<Big>()));
    printf("nvd3d %s(wrong) %d tail=%s\n", name, s, Tail(4, sizeof(Big)));
    unsigned at = 0;  // labels by position: NVIDIA's SLI VER2 carries version number 1
    for (auto [v, size] : versions) {
        auto p = Fill<Big>(v);
        s = call(device, p);
        char label[96];
        snprintf(label, sizeof(label), "%s(v%u)", name, ++at);
        printf("nvd3d %s %d", label, s);
        if (size < sizeof(Big)) printf(" tail=%s", Tail(size, sizeof(Big)));
        if constexpr (std::is_same_v<Big, NV_GET_CURRENT_SLI_STATE_V2>) {
            printf(" %u %u %u %u %u %u", (unsigned)p->maxNumAFRGroups, (unsigned)p->numAFRGroups, (unsigned)p->currentAFRIndex,
                   (unsigned)p->nextFrameAFRIndex, (unsigned)p->previousFrameAFRIndex, (unsigned)p->bIsCurAFRGroupNew);
            if (size == sizeof(Big)) printf(" %u", (unsigned)p->numVRSLIGpus);
        }
        if constexpr (std::is_same_v<Big, NV_GET_SLEEP_STATUS_PARAMS_V1>)
            printf(" %u %u %u %u %u rsvd=%s", p->bLowLatencyMode, p->bFsVrr, p->bCplVsyncOn, (unsigned)p->sleepIntervalUs,
                   p->bUseGameSleep, Tail(offsetof(Big, rsvd), sizeof(Big)));
        if constexpr (std::is_same_v<Big, NV_LATENCY_RESULT_PARAMS_V1>) {
            bool zero = true;
            for (size_t i = offsetof(Big, frameReport); i < offsetof(Big, frameReport) + sizeof(p->frameReport); i++)
                zero = zero && g_buf[i] == 0;
            printf(" frames-zeroed %s rsvd=%s", Yes(zero), Tail(offsetof(Big, frameReport) + sizeof(p->frameReport), sizeof(Big)));
        }
        if constexpr (!std::is_same_v<Big, NV_GET_CURRENT_SLI_STATE_V2> && !std::is_same_v<Big, NV_GET_SLEEP_STATUS_PARAMS_V1> &&
                      !std::is_same_v<Big, NV_LATENCY_RESULT_PARAMS_V1>)
            printf(" body=%s", Tail(4, sizeof(Big)));
        printf("\n");
        Dump(label, s, sizeof(Big));
    }
}

static void D3d() {
    FN(D3D_GetCurrentSLIState, 0x4b708b54);
    HAVE("nvd3d", D3D_GetCurrentSLIState)
        Versioned<NV_GET_CURRENT_SLI_STATE_V2>("D3D_GetCurrentSLIState",
            [&](IUnknown *d, auto *p) { return D3D_GetCurrentSLIState(d, (NV_GET_CURRENT_SLI_STATE *)p); },
            {{NV_GET_CURRENT_SLI_STATE_VER1, sizeof(NV_GET_CURRENT_SLI_STATE_V1)},
             {NV_GET_CURRENT_SLI_STATE_VER2, sizeof(NV_GET_CURRENT_SLI_STATE_V2)}});
    FN(D3D_GetSleepStatus, 0xaef96ca1);
    HAVE("nvd3d", D3D_GetSleepStatus)
        Versioned<NV_GET_SLEEP_STATUS_PARAMS_V1>("D3D_GetSleepStatus",
            [&](IUnknown *d, auto *p) { return D3D_GetSleepStatus(d, p); },
            {{NV_GET_SLEEP_STATUS_PARAMS_VER1, sizeof(NV_GET_SLEEP_STATUS_PARAMS_V1)}});
    FN(D3D_SetSleepMode, 0xac1ca9e0);
    HAVE("nvd3d", D3D_SetSleepMode)
        Versioned<NV_SET_SLEEP_MODE_PARAMS_V1>("D3D_SetSleepMode",
            [&](IUnknown *d, auto *p) { return D3D_SetSleepMode(d, p); },
            {{NV_SET_SLEEP_MODE_PARAMS_VER1, sizeof(NV_SET_SLEEP_MODE_PARAMS_V1)}});
    FN(D3D_SetLatencyMarker, 0xd9984c05);
    HAVE("nvd3d", D3D_SetLatencyMarker)
        Versioned<NV_LATENCY_MARKER_PARAMS_V1>("D3D_SetLatencyMarker",
            [&](IUnknown *d, auto *p) { return D3D_SetLatencyMarker(d, p); },
            {{NV_LATENCY_MARKER_PARAMS_VER1, sizeof(NV_LATENCY_MARKER_PARAMS_V1)}});
    FN(D3D_GetLatency, 0x1a587f9c);
    HAVE("nvd3d", D3D_GetLatency)
        Versioned<NV_LATENCY_RESULT_PARAMS_V1>("D3D_GetLatency",
            [&](IUnknown *d, auto *p) { return D3D_GetLatency(d, p); },
            {{NV_LATENCY_RESULT_PARAMS_VER1, sizeof(NV_LATENCY_RESULT_PARAMS_V1)}});
    FN(D3D_Sleep, 0x852cd1d2);
    HAVE("nvd3d", D3D_Sleep) {
        printf("nvd3d D3D_Sleep(null) %d\n", D3D_Sleep(nullptr));
        printf("nvd3d D3D_Sleep %d\n", D3D_Sleep(device));
    }
    FN(D3D_SetVerticalSyncMode, 0x5526cfd1);
    HAVE("nvd3d", D3D_SetVerticalSyncMode) {
        printf("nvd3d D3D_SetVerticalSyncMode(null) %d\n", D3D_SetVerticalSyncMode(nullptr, NVAPI_VSYNC_OFF));
        printf("nvd3d D3D_SetVerticalSyncMode %d\n", D3D_SetVerticalSyncMode(device, NVAPI_VSYNC_OFF));
    }

    FakeContext fctx;
    FakeDevice fdev;
    FN(D3D11_BeginUAVOverlap, 0x65b93ca8);
    FN(D3D11_EndUAVOverlap, 0x2216a357);
    HAVE("nvd3d", D3D11_BeginUAVOverlap) HAVE("nvd3d", D3D11_EndUAVOverlap) {
        printf("nvd3d D3D11_BeginUAVOverlap(null) %d\n", D3D11_BeginUAVOverlap(nullptr));
        printf("nvd3d D3D11_EndUAVOverlap(null) %d\n", D3D11_EndUAVOverlap(nullptr));
        int b = D3D11_BeginUAVOverlap(&fctx), e = D3D11_EndUAVOverlap(&fctx);
        printf("nvd3d D3D11_UAVOverlap(fake context) %d %d begins %d ends %d upscales %d refs-balanced %s\n", b, e,
               fctx.begins, fctx.ends, fctx.upscales, Yes(fctx.refs == 1));
        printf("nvd3d D3D11_UAVOverlap(fake device) %d %d\n", D3D11_BeginUAVOverlap(&fdev), D3D11_EndUAVOverlap(&fdev));
        printf("nvd3d D3D11_UAVOverlap(device) %d %d\n", D3D11_BeginUAVOverlap(device), D3D11_EndUAVOverlap(device));
        printf("nvd3d D3D11_UAVOverlap(context) %d %d\n", D3D11_BeginUAVOverlap(ctx), D3D11_EndUAVOverlap(ctx));
    }
    FN(D3D11_SetNvShaderExtnSlot, 0x8e90bb9f);
    HAVE("nvd3d", D3D11_SetNvShaderExtnSlot) {
        int s = D3D11_SetNvShaderExtnSlot(&fdev, 7);
        printf("nvd3d D3D11_SetNvShaderExtnSlot(fake device) %d slot %u refs-balanced %s\n", s, fdev.slot,
               Yes(fdev.refs == 1));
        printf("nvd3d D3D11_SetNvShaderExtnSlot(fake context) %d\n", D3D11_SetNvShaderExtnSlot(&fctx, 7));
        printf("nvd3d D3D11_SetNvShaderExtnSlot(device) %d\n", D3D11_SetNvShaderExtnSlot(device, 7));
    }
    FN(D3D11_SetDepthBoundsTest, 0x7aaf7a04);
    HAVE("nvd3d", D3D11_SetDepthBoundsTest)
        printf("nvd3d D3D11_SetDepthBoundsTest %d\n", D3D11_SetDepthBoundsTest(device, 1, 0.0f, 1.0f));
    FN(D3D11_IsNvShaderExtnOpCodeSupported, 0x5f68da40);
    HAVE("nvd3d", D3D11_IsNvShaderExtnOpCodeSupported) {
        for (NvU32 op : {(NvU32)NV_EXTN_OP_SHFL, (NvU32)NV_EXTN_OP_FP16_ATOMIC, (NvU32)NV_EXTN_OP_UINT64_ATOMIC, (NvU32)99}) {
            unsigned char supported = 0xa5;
            int s = D3D11_IsNvShaderExtnOpCodeSupported(device, op, (bool *)&supported);
            printf("nvd3d D3D11_IsNvShaderExtnOpCodeSupported(%u) %d %u\n", (unsigned)op, s, supported);
        }
    }
    FN(D3D_GetObjectHandleForResource, 0xfceac864);
    HAVE("nvd3d", D3D_GetObjectHandleForResource) {
        NVDX_ObjectHandle h = (NVDX_ObjectHandle)0xa5a5a5a5;
        int s = D3D_GetObjectHandleForResource(device, ctx, &h);
        printf("nvd3d D3D_GetObjectHandleForResource %d handle-is-resource %s\n", s, Yes((void *)h == (void *)ctx));
    }
    FN(D3D_SetResourceHint, 0x6c0ed98c);
    HAVE("nvd3d", D3D_SetResourceHint) {
        NvU32 value = 0xa5a5a5a5;
        int s = D3D_SetResourceHint(device, nullptr, NVAPI_D3D_SRH_CATEGORY_SLI, 1, &value);
        printf("nvd3d D3D_SetResourceHint %d value=%s\n", s, value == 0xa5a5a5a5 ? "untouched" : "written");
    }
}

static void Query() {
    // nvapi.cpp's "unknown functions": null on purpose, no log line.
    for (NvU32 id : {0x33c7358cu, 0x593e8644u, 0xad298d3fu, 0xd7c61344u, 0xd44d3c4eu, 0x6d67533cu, 0xb7fbdbfau,
                     0xc99f4a67u})
        printf("nvquery %#010x %s\n", (unsigned)id, g_query(id) ? "answered" : "null");
    // One nvapi_interface.h names (its log line names it) and one it doesn't (its log line gives the number).
    printf("nvquery NvAPI_GPU_GetThermalSettings %s\n", g_query(0xe3640a56) ? "answered" : "null");
    printf("nvquery %#010x %s\n", 0x12345678u, g_query(0x12345678) ? "answered" : "null");
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: d3d11_nvapi.exe <nvapi64.dll> fixed|all\n"); return 2; }
    g_all = !strcmp(argv[2], "all");
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("nvsys FAIL LoadLibrary %s: %lu\n", argv[1], GetLastError()); return 1; }
    if (!(g_query = (void *(*)(NvU32))GetProcAddress(dll, "nvapi_QueryInterface"))) {
        printf("nvsys FAIL no export nvapi_QueryInterface\n");
        return 1;
    }
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_1;
    CHECK(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &device,
                            nullptr, &ctx));
    IDXGIDevice *dxgi;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC desc;
    CHECK(device->QueryInterface(IID_PPV_ARGS(&dxgi)));
    CHECK(dxgi->GetAdapter(&adapter));
    CHECK(adapter->GetDesc(&desc));
    g_luid = desc.AdapterLuid;
    adapter->Release();
    dxgi->Release();
    Sys();
    Gpu();
    Disp();
    D3d();
    Query();
    printf("nvsys done\n");
    return 0;
}
