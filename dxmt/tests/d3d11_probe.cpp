// d3d11_probe.cpp — Step 5b behavioral oracle probe for D3D11 + D3D10 + DXMT extensions.
//
// Builds as a single standalone executable (x86_64 and i686 via llvm-mingw, plus
// macOS native via build-tests.sh) and emits deterministic, line-oriented key=value
// output suitable for `diff -u` between reference C++ DXMT and Rust dxmtrs.
//
// Usage:
//   d3d11_probe.exe <shaders> <fixed|full> [<family>...] [--ported]
//   d3d11_probe.exe --child-fl <fl_str>
//   d3d11_probe.exe --child-share <shaders> <h_shared_hex> <h_km_hex>
//
// Families:
//   d11dev, d11res, d11view, d11state, d11shader, d11ctx, d11defer,
//   d11query, d11ext, d11sc, d11share, d10, notimpl (native #ifdef __APPLE__)
//
// ============================================================================
// P10 Inventory: All 19 UNIMPLEMENTED sites in wine-arm64-src/dxmt/src/d3d11/
// ============================================================================
//  1. d3d11_device.cpp:803        ID3D11Device2::GetResourceTiling              ("tiled resource: get resource tiling", void)
//  2. d3d11_device.cpp:1000       ID3D11Device3::WriteToSubresource             ("map-on-default: write resource", void)
//  3. d3d11_device.cpp:1008       ID3D11Device3::ReadFromSubresource            ("map-on-default: read resource", void)
//  4. d3d11_device.cpp:1233       IDXGIDevice::CreateSurface                    ("CreateSurface", HRESULT -> E_NOTIMPL)
//  5. d3d11_context_imm.cpp:292   ID3D11DeviceContext::Map (imm default, null)  ("map-on-default: map", HRESULT -> E_NOTIMPL)
//  6. d3d11_context_def.cpp:268   ID3D11DeviceContext::Map (def default, null)  ("map-on-default: map", HRESULT -> E_NOTIMPL)
//  7. d3d11_context_def.cpp:400   ID3D11DeviceContext::ExecuteCommandList (def) ("ExecuteCommandList on deferred context", void)
//  8. d3d11_context_impl.cpp:1250 ID3D11DeviceContext::UpdateSubresource (buf)  ("update buffer: staging", void)
//  9. d3d11_context_impl.cpp:3209 ID3D11DeviceContext2::UpdateTileMappings      ("tiled resource: update mapping", HRESULT -> E_NOTIMPL)
// 10. d3d11_context_impl.cpp:3217 ID3D11DeviceContext2::CopyTileMappings        ("tiled resource: copy mapping", HRESULT -> E_NOTIMPL)
// 11. d3d11_context_impl.cpp:3224 ID3D11DeviceContext2::CopyTiles               ("tiled resource: copy tiles", void)
// 12. d3d11_context_impl.cpp:3231 ID3D11DeviceContext2::UpdateTiles             ("tiled resource: update tiles", void)
// 13. d3d11_context_impl.cpp:3235 ID3D11DeviceContext2::ResizeTilePool          ("tiled resource: resize", HRESULT -> E_NOTIMPL)
// 14. d3d11_context_impl.cpp:3240 ID3D11DeviceContext2::TiledResourceBarrier    ("tiled resource: barrier", void)
// 15. d3d11_context_impl.cpp:4109 CopyTextureFromCompressed (BC1 def -> R32G32 stg) ("copy texture: from compressed to staging", void)
// 16. d3d11_context_impl.cpp:4113 CopyTextureFromCompressed (BC1 stg -> R32G32 def) ("copy texture: from compressed staging to default", void)
// 17. d3d11_context_impl.cpp:4160 CopyTextureToCompressed (R32G32 def -> BC1 stg)   ("copy texture: copy to compressed staging", void)
// 18. d3d11_context_impl.cpp:4164 CopyTextureToCompressed (R32G32 stg -> BC1 def)   ("copy texture: from staging to compressed default", void)
// 19. d3d11_context_impl.cpp:4257 UpdateTexture (UpdateSubresource on stg tex2d)    ("update texture: staging", void)
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

#ifdef __APPLE__
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using PFN_D3D10CoreCreateDevice = HRESULT(WINAPI *)(
    IDXGIFactory *pFactory, IDXGIAdapter *pAdapter, UINT Flags,
    D3D_FEATURE_LEVEL FeatureLevel, ID3D10Device **ppDevice);

extern "C" HRESULT WINAPI D3D11On12CreateDevice(
    IUnknown *pDevice, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels, IUnknown *const *ppCommandQueues, UINT NumQueues,
    UINT NodeMask, ID3D11Device **ppDevice,
    ID3D11DeviceContext **ppImmediateContext,
    D3D_FEATURE_LEVEL *pChosenFeatureLevel);

// Private DXMT and extra D3D11/DXGI GUIDs
static const GUID kIID_IMTLDXGIDevice =
    {0x6bfa1657, 0x9cb1, 0x471a, {0xa4, 0xfb, 0x7c, 0xac, 0xf8, 0xa8, 0x12, 0x07}};
static const GUID kIID_IMTLD3D11DeviceExt =
    {0xefc77ae6, 0x2179, 0x4c0a, {0xb8, 0x44, 0x76, 0x61, 0xca, 0x0d, 0xcd, 0xe7}};
static const GUID kIID_IMTLD3D11ContextExt =
    {0x43ace3ce, 0x1956, 0x448b, {0xa4, 0xeb, 0xae, 0xe6, 0x8b, 0xde, 0xb2, 0x83}};
static const GUID kIID_IMTLD3D11ContextExt1 =
    {0x19a8e35a, 0x38be, 0x418f, {0x94, 0xe3, 0x9f, 0x73, 0x23, 0x93, 0x68, 0x70}};
static const GUID kIID_IMTLD3D11ContextExt2 =
    {0xa5253c74, 0x3ff9, 0x497f, {0xb1, 0x84, 0xf1, 0x37, 0xc5, 0xa7, 0xb9, 0x28}};
static const GUID kIID_IMTLD3D11DeviceContext =
    {0x3a3f085a, 0xd0fe, 0x4324, {0xb0, 0xae, 0xfe, 0x04, 0xde, 0x18, 0x57, 0x1c}};
static const GUID kIID_IMTLMinLODClampable =
    {0x9a6f6549, 0xd4b1, 0x45ea, {0x87, 0x94, 0x85, 0x03, 0xd1, 0x90, 0xd3, 0xd1}};
static const GUID kIID_IMTLD3D11BlendState =
    {0x03629ed8, 0xbcdd, 0x4582, {0x89, 0x97, 0x38, 0x17, 0x20, 0x9a, 0x34, 0xf4}};
static const GUID kIID_IMTLD3D11RasterizerState =
    {0xb01aaffa, 0xb4d3, 0x478a, {0x91, 0xbe, 0x61, 0x95, 0xf2, 0x15, 0xaa, 0xba}};
static const GUID kIID_IMTLD3D11DepthStencilState =
    {0x279a1d66, 0x2fc1, 0x460c, {0xa0, 0xa7, 0xa7, 0xa5, 0xf2, 0xb7, 0xa4, 0x8f}};
static const GUID kIID_IMTLD3D11InputLayout =
    {0xb56c6a99, 0x80cf, 0x4c7f, {0xa7, 0x56, 0x9e, 0x9c, 0xeb, 0x38, 0x73, 0x0f}};
static const GUID kIID_IMTLD3D11StreamOutputLayout =
    {0xfd58f76b, 0x7c22, 0x4605, {0xb4, 0x3c, 0x28, 0x04, 0x8c, 0x8b, 0x4a, 0x64}};
static const GUID kIID_IMTLD3D11Shader =
    {0xe95ba1c7, 0xe43f, 0x49c3, {0xa9, 0x07, 0x4a, 0xc6, 0x69, 0xc9, 0xfb, 0x42}};
static const GUID kIID_ID3D11On12Device =
    {0x85611e73, 0x70a9, 0x490e, {0x96, 0x14, 0xa9, 0xe3, 0x02, 0x77, 0x79, 0x04}};
static const GUID kIID_ID3D11Debug =
    {0x79cf2233, 0x7536, 0x4948, {0x9d, 0x36, 0x1e, 0x46, 0x92, 0xdc, 0x57, 0x60}};
static const GUID kIID_ID3D11InfoQueue =
    {0x6543dbb6, 0x1b48, 0x42f5, {0xab, 0x82, 0xe9, 0x7e, 0xc7, 0x43, 0x26, 0xf6}};
static const GUID kIID_ID3D11VideoDevice =
    {0x10ec4d5b, 0x975a, 0x4689, {0xb9, 0xe4, 0xd0, 0xaa, 0xc3, 0x0f, 0xe3, 0x33}};
static const GUID kIID_ID3D11VideoContext =
    {0x61f21c45, 0x3c0e, 0x4a74, {0x9c, 0xea, 0x67, 0x10, 0x0d, 0x9a, 0xd5, 0xe4}};
static const GUID kIID_ID3D11TracingDevice =
    {0x1911c771, 0x1587, 0x413e, {0xa7, 0xe0, 0xfb, 0x26, 0xc3, 0xde, 0x02, 0x68}};
static const GUID kIID_ID3D11SwitchToRef =
    {0x1ef337e3, 0x58e7, 0x4f83, {0xa6, 0x92, 0xdb, 0x22, 0x1f, 0x5e, 0xd4, 0x7e}};
static const GUID kIID_Bogus =
    {0xdeadbeef, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};
static const GUID kGuidPdataVal =
    {0x11223344, 0x5566, 0x7788, {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01}};
static const GUID kGuidPdataUnk =
    {0x11223344, 0x5566, 0x7788, {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02}};

// Minimal vtable wrappers for DXMT private interfaces
struct IMTLD3D11DeviceExtVtbl : IUnknown {
  virtual void STDMETHODCALLTYPE SetShaderExtensionSlot(UINT Slot) = 0;
};
struct IMTLD3D11ContextExtVtbl : IUnknown {
  virtual void STDMETHODCALLTYPE TemporalUpscale(const void *pDesc) = 0;
  virtual void STDMETHODCALLTYPE BeginUAVOverlap() = 0;
  virtual void STDMETHODCALLTYPE EndUAVOverlap() = 0;
};
struct IMTLD3D11ContextExt1Vtbl : IMTLD3D11ContextExtVtbl {
  virtual HRESULT STDMETHODCALLTYPE CheckFeatureSupport(int Feature, void *pData, UINT Size) = 0;
};
struct MTL_PIECE {
  void *command_buffer;
  void *fence;
  UINT64 signal_value;
};
struct IMTLD3D11ContextExt2Vtbl : IMTLD3D11ContextExt1Vtbl {
  virtual HRESULT STDMETHODCALLTYPE GetPieces(MTL_PIECE *pPieces, UINT *pCount) = 0;
  virtual void STDMETHODCALLTYPE UsePiece(const MTL_PIECE *pPiece) = 0;
};

static bool g_full = false;
static bool g_ported = false;
static std::string g_shader_dir;

static void Out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
}

static void Raw(const char *fmt, ...) {
  if (!g_full) return;
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
}

template <typename T>
struct P {
  T *p = nullptr;
  P() = default;
  explicit P(T *v) : p(v) {}
  ~P() { reset(); }
  P(const P &) = delete;
  P &operator=(const P &) = delete;
  void reset(T *v = nullptr) {
    if (p) p->Release();
    p = v;
  }
  T **operator&() {
    reset();
    return &p;
  }
  T *operator->() const { return p; }
  operator T *() const { return p; }
};

struct DummyUnk : IUnknown {
  ULONG refs = 1;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown)) {
      *ppv = static_cast<IUnknown *>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override { return --refs; }
};

static std::vector<uint8_t> LoadShader(const char *name) {
  std::string path = g_shader_dir + "/" + name;
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return {};
  auto sz = f.tellg();
  if (sz <= 0) return {};
  std::vector<uint8_t> buf(static_cast<size_t>(sz));
  f.seekg(0);
  f.read(reinterpret_cast<char *>(buf.data()), sz);
  return buf;
}

struct iid_entry {
  const char *name;
  GUID iid;
};

static void ProbeQiList(const char *fam, const char *obj_name, IUnknown *obj,
                        const iid_entry *entries, size_t count) {
  if (!obj) {
    Out("%s %s_qi obj=null\n", fam, obj_name);
    return;
  }
  HRESULT hr_null = obj->QueryInterface(__uuidof(IUnknown), nullptr);
  Out("%s %s_qi_null hr=%08x\n", fam, obj_name, (unsigned)hr_null);

  P<IUnknown> unk_root;
  obj->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&unk_root));

  for (size_t i = 0; i < count; ++i) {
    void *pv = reinterpret_cast<void *>(0x1);
    HRESULT hr = obj->QueryInterface(entries[i].iid, &pv);
    bool nonnull = (pv != nullptr && pv != reinterpret_cast<void *>(0x1));
    int id_match = 0;
    if (SUCCEEDED(hr) && nonnull) {
      IUnknown *q = static_cast<IUnknown *>(pv);
      P<IUnknown> q_unk;
      if (SUCCEEDED(q->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&q_unk)))) {
        id_match = (q_unk.p == unk_root.p) ? 1 : 0;
      }
      q->Release();
    }
    Out("%s %s_qi iid=%s hr=%08x out=%s id=%d\n",
        fam, obj_name, entries[i].name, (unsigned)hr,
        nonnull ? "ptr" : (pv == nullptr ? "null" : "untouched"), id_match);
  }
}

template <typename T>
static void ProbePrivateData(const char *fam, const char *tag, T *obj) {
  if (!obj) return;
  uint32_t val = 0xabcdef01u;
  uint32_t out = 0;
  UINT sz = 0;
  HRESULT hr_get_unset = obj->GetPrivateData(kGuidPdataVal, &sz, nullptr);
  HRESULT hr_set = obj->SetPrivateData(kGuidPdataVal, sizeof(val), &val);
  sz = 0;
  HRESULT hr_size = obj->GetPrivateData(kGuidPdataVal, &sz, nullptr);
  UINT small_sz = 2;
  HRESULT hr_small = obj->GetPrivateData(kGuidPdataVal, &small_sz, &out);
  UINT full_sz = sizeof(out);
  HRESULT hr_get = obj->GetPrivateData(kGuidPdataVal, &full_sz, &out);
  HRESULT hr_del = obj->SetPrivateData(kGuidPdataVal, 0, nullptr);
  UINT after_sz = sizeof(out);
  HRESULT hr_after = obj->GetPrivateData(kGuidPdataVal, &after_sz, &out);

  DummyUnk d1, d2;
  ULONG r0 = d1.refs;
  HRESULT hr_if_set = obj->SetPrivateDataInterface(kGuidPdataUnk, &d1);
  ULONG r_after_set = d1.refs;
  IUnknown *got_unk = nullptr;
  UINT if_sz = sizeof(got_unk);
  HRESULT hr_if_get = obj->GetPrivateData(kGuidPdataUnk, &if_sz, &got_unk);
  ULONG r_after_get = d1.refs;
  if (got_unk) got_unk->Release();
  HRESULT hr_if_repl = obj->SetPrivateDataInterface(kGuidPdataUnk, &d2);
  ULONG r_after_repl = d1.refs;
  HRESULT hr_if_clear = obj->SetPrivateDataInterface(kGuidPdataUnk, nullptr);
  ULONG r_d2_clear = d2.refs;

  Out("%s %s_pdata unset=%08x set=%08x qsz=%08x/%u small=%08x/%u get=%08x/%08x del=%08x after=%08x "
      "if_set=%08x(%lu->%lu) if_get=%08x(%lu) if_repl=%08x(%lu) if_clr=%08x(%lu)\n",
      fam, tag, (unsigned)hr_get_unset, (unsigned)hr_set,
      (unsigned)hr_size, sz, (unsigned)hr_small, small_sz,
      (unsigned)hr_get, out, (unsigned)hr_del, (unsigned)hr_after,
      (unsigned)hr_if_set, (unsigned long)r0, (unsigned long)r_after_set,
      (unsigned)hr_if_get, (unsigned long)r_after_get,
      (unsigned)hr_if_repl, (unsigned long)r_after_repl,
      (unsigned)hr_if_clear, (unsigned long)r_d2_clear);
}

static void ProbeChildGetDevice(const char *fam, const char *tag,
                                ID3D11DeviceChild *child, ID3D11Device *dev) {
  if (!child || !dev) return;
  ULONG before = dev->AddRef();
  dev->Release();
  ID3D11Device *got = nullptr;
  child->GetDevice(&got);
  ULONG during = dev->AddRef();
  dev->Release();
  int match = (got == dev) ? 1 : 0;
  if (got) got->Release();
  ULONG after = dev->AddRef();
  dev->Release();
  Out("%s %s_getdev match=%d ref_delta=%ld,%ld\n",
      fam, tag, match, (long)during - (long)before, (long)after - (long)before);
}

static bool CreateDefaultDevice(ID3D11Device **ppDev, ID3D11DeviceContext **ppCtx,
                                UINT flags = 0, D3D_FEATURE_LEVEL *pFl = nullptr) {
  D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
      nullptr, 0, D3D11_SDK_VERSION, ppDev, &fl, ppCtx);
  if (pFl) *pFl = fl;
  return SUCCEEDED(hr);
}

#ifdef _WIN32
static std::string SpawnCaptureWin32(const char *exe, const std::string &args,
                                     const char *env_key, const char *env_val) {
  if (env_key) {
    _putenv_s(env_key, env_val ? env_val : "");
  }
  SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  std::string out;
  if (CreatePipe(&rd, &wr, &sa, 0)) {
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::string cmdline = std::string("\"") + exe + "\" " + args;
    std::vector<char> mut_cmd(cmdline.begin(), cmdline.end());
    mut_cmd.push_back('\0');
    if (CreateProcessA(exe, mut_cmd.data(), nullptr, nullptr, TRUE, 0,
                       nullptr, nullptr, &si, &pi)) {
      CloseHandle(wr);
      wr = nullptr;
      CloseHandle(pi.hThread);
      char buf[1024];
      DWORD got = 0;
      while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
      }
      WaitForSingleObject(pi.hProcess, 30000);
      CloseHandle(pi.hProcess);
    }
    if (wr) CloseHandle(wr);
    CloseHandle(rd);
  }
  if (env_key) {
    _putenv_s(env_key, "");
  }
  return out;
}
#endif

static void RunChildCmd(const std::string &args, const char *env_key, const char *env_val) {
  char self[1024] = {};
#ifdef _WIN32
  GetModuleFileNameA(nullptr, self, MAX_PATH);
  std::string out = SpawnCaptureWin32(self, args, env_key, env_val);
  std::fwrite(out.data(), 1, out.size(), stdout);
#else
  uint32_t sz = sizeof(self);
  _NSGetExecutablePath(self, &sz);
  if (env_key) {
    if (env_val && *env_val) setenv(env_key, env_val, 1);
    else unsetenv(env_key);
  }
  std::string cmd = std::string("\"") + self + "\" " + args;
  FILE *fp = popen(cmd.c_str(), "r");
  if (fp) {
    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
      std::fputs(line, stdout);
    }
    pclose(fp);
  }
  if (env_key) {
    unsetenv(env_key);
  }
#endif
}

static int ChildFeatureLevelMain(const char *fl_str) {
  D3D_FEATURE_LEVEL fl_out = (D3D_FEATURE_LEVEL)0;
  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  HRESULT hr_def = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
      nullptr, 0, D3D11_SDK_VERSION, &dev, &fl_out, &ctx);
  Out("d11dev fl_cfg=%s default hr=%08x fl=%04x dev_fl=%04x\n",
      fl_str, (unsigned)hr_def, (unsigned)fl_out,
      dev ? (unsigned)dev->GetFeatureLevel() : 0u);

  const D3D_FEATURE_LEVEL reqs[] = {
      D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,
      D3D_FEATURE_LEVEL_9_1,
  };
  for (D3D_FEATURE_LEVEL req : reqs) {
    P<ID3D11Device> d2;
    D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        &req, 1, D3D11_SDK_VERSION, &d2, &got, nullptr);
    Out("d11dev fl_cfg=%s req=%04x hr=%08x got=%04x\n",
        fl_str, (unsigned)req, (unsigned)hr, (unsigned)got);
  }
  return 0;
}
static void RunD11Dev() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  // 1. D3D11CreateDevice argument validation
  {
    const D3D_DRIVER_TYPE dtypes[] = {
        D3D_DRIVER_TYPE_UNKNOWN,   D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_REFERENCE, D3D_DRIVER_TYPE_NULL,
        D3D_DRIVER_TYPE_SOFTWARE,  D3D_DRIVER_TYPE_WARP,
    };
    for (D3D_DRIVER_TYPE dt : dtypes) {
      P<ID3D11Device> d;
      P<ID3D11DeviceContext> c;
      D3D_FEATURE_LEVEL fl = (D3D_FEATURE_LEVEL)0;
      HRESULT hr = D3D11CreateDevice(
          nullptr, dt, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &fl, &c);
      Out("d11dev create_dt dt=%d hr=%08x dev=%d ctx=%d fl=%04x\n",
          (int)dt, (unsigned)hr, d ? 1 : 0, c ? 1 : 0, (unsigned)fl);
      ops++;
    }

    // Adapter non-null + HARDWARE (invalid per D3D11 spec) vs UNKNOWN (valid)
    P<IDXGIFactory1> fac;
    CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&fac));
    P<IDXGIAdapter1> adp;
    if (fac) fac->EnumAdapters1(0, &adp);
    if (adp) {
      P<ID3D11Device> d1, d2;
      HRESULT hr_hw = D3D11CreateDevice(
          adp, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
          D3D11_SDK_VERSION, &d1, nullptr, nullptr);
      HRESULT hr_unk = D3D11CreateDevice(
          adp, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
          D3D11_SDK_VERSION, &d2, nullptr, nullptr);
      Out("d11dev create_with_adapter hw=%08x unk=%08x\n",
          (unsigned)hr_hw, (unsigned)hr_unk);
      ops += 2;
    }

    // Null pFeatureLevels with non-zero count, or non-null with 0 count
    D3D_FEATURE_LEVEL fl_11_0 = D3D_FEATURE_LEVEL_11_0;
    P<ID3D11Device> d_bad1, d_bad2, d_null_both;
    HRESULT hr_null_fl = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 1,
        D3D11_SDK_VERSION, &d_bad1, nullptr, nullptr);
    HRESULT hr_zero_fl = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl_11_0, 0,
        D3D11_SDK_VERSION, &d_bad2, nullptr, nullptr);
    D3D_FEATURE_LEVEL fl_only = (D3D_FEATURE_LEVEL)0;
    HRESULT hr_probe_fl = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, nullptr, &fl_only, nullptr);
    Out("d11dev create_edge null_fl_cnt1=%08x nonnull_fl_cnt0=%08x null_dev_ctx=%08x(fl=%04x)\n",
        (unsigned)hr_null_fl, (unsigned)hr_zero_fl, (unsigned)hr_probe_fl, (unsigned)fl_only);

    // D3D11CreateDeviceAndSwapChain with nullptr SwapChainDesc
    P<ID3D11Device> d_sc;
    P<ID3D11DeviceContext> c_sc;
    P<IDXGISwapChain> sc;
    HRESULT hr_sc_null = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, nullptr, &sc, &d_sc, nullptr, &c_sc);
    Out("d11dev create_and_sc_null_desc hr=%08x dev=%d ctx=%d sc=%d\n",
        (unsigned)hr_sc_null, d_sc ? 1 : 0, c_sc ? 1 : 0, sc ? 1 : 0);
  }

  // 2. Feature level matrix via child processes with DXMT_CONFIG=d3d11.maxFeatureLevel
  {
    const char *fl_configs[] = {"12_1", "12_0", "11_1", "11_0", "10_1", "10_0", "9_3", "9_2", "9_1"};
    for (const char *cfg : fl_configs) {
      std::string env_val = std::string("d3d11.maxFeatureLevel=") + cfg;
      RunChildCmd(std::string("--child-fl ") + cfg, "DXMT_CONFIG", env_val.c_str());
      ops++;
    }
  }

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx, D3D11_CREATE_DEVICE_BGRA_SUPPORT)) return;

  // 3. Device QI matrix
  static const iid_entry kDevIids[] = {
      {"IUnknown", __uuidof(IUnknown)},
      {"ID3D11Device", __uuidof(ID3D11Device)},
      {"ID3D11Device1", __uuidof(ID3D11Device1)},
      {"ID3D11Device2", __uuidof(ID3D11Device2)},
      {"ID3D11Device3", __uuidof(ID3D11Device3)},
      {"ID3D11Device4", __uuidof(ID3D11Device4)},
      {"ID3D11Device5", __uuidof(ID3D11Device5)},
      {"IDXGIDevice", __uuidof(IDXGIDevice)},
      {"IDXGIDevice1", __uuidof(IDXGIDevice1)},
      {"IDXGIDevice2", __uuidof(IDXGIDevice2)},
      {"IDXGIDevice3", __uuidof(IDXGIDevice3)},
      {"IDXGIDevice4", __uuidof(IDXGIDevice4)},
      {"IMTLDXGIDevice", kIID_IMTLDXGIDevice},
      {"IMTLD3D11DeviceExt", kIID_IMTLD3D11DeviceExt},
      {"ID3D11Multithread", __uuidof(ID3D11Multithread)},
      {"ID3D10Multithread", __uuidof(ID3D10Multithread)},
      {"ID3D10Device", __uuidof(ID3D10Device)},
      {"ID3D10Device1", __uuidof(ID3D10Device1)},
      {"ID3D11Debug", kIID_ID3D11Debug},
      {"ID3D11InfoQueue", kIID_ID3D11InfoQueue},
      {"ID3D11On12Device", kIID_ID3D11On12Device},
      {"ID3D11VideoDevice", kIID_ID3D11VideoDevice},
      {"ID3D11TracingDevice", kIID_ID3D11TracingDevice},
      {"ID3D11SwitchToRef", kIID_ID3D11SwitchToRef},
      {"Bogus", kIID_Bogus},
  };
  ProbeQiList("d11dev", "device", dev, kDevIids, sizeof(kDevIids) / sizeof(kDevIids[0]));
  ProbePrivateData("d11dev", "device", dev.p);

  // 4. CheckFeatureSupport for every D3D11_FEATURE (0..=21)
  {
    struct feat_spec {
      UINT feat;
      const char *name;
      UINT right_size;
    };
    static const feat_spec kSpecs[] = {
        {0, "THREADING", sizeof(D3D11_FEATURE_DATA_THREADING)},
        {1, "DOUBLES", sizeof(D3D11_FEATURE_DATA_DOUBLES)},
        {2, "FORMAT_SUPPORT", sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT)},
        {3, "FORMAT_SUPPORT2", sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2)},
        {4, "D3D10_X_HARDWARE_OPTIONS", sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS)},
        {5, "D3D11_OPTIONS", sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)},
        {6, "ARCHITECTURE_INFO", sizeof(D3D11_FEATURE_DATA_ARCHITECTURE_INFO)},
        {7, "D3D9_OPTIONS", sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS)},
        {8, "SHADER_MIN_PRECISION_SUPPORT", sizeof(D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT)},
        {9, "D3D9_SHADOW_SUPPORT", sizeof(D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT)},
        {10, "D3D11_OPTIONS1", sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS1)},
        {11, "D3D9_SIMPLE_INSTANCING_SUPPORT", sizeof(D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT)},
        {12, "MARKER_SUPPORT", sizeof(D3D11_FEATURE_DATA_MARKER_SUPPORT)},
        {13, "D3D9_OPTIONS1", sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS1)},
        {14, "D3D11_OPTIONS2", sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS2)},
        {15, "D3D11_OPTIONS3", sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS3)},
        {16, "GPU_VIRTUAL_ADDRESS_SUPPORT", sizeof(D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT)},
        {17, "D3D11_OPTIONS4", 4 /* D3D11_FEATURE_DATA_D3D11_OPTIONS4 */},
        {18, "SHADER_CACHE", sizeof(D3D11_FEATURE_DATA_SHADER_CACHE)},
        {19, "D3D11_OPTIONS5", sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS5)},
        {20, "DISPLAYABLE", 8 /* D3D11_FEATURE_DATA_DISPLAYABLE */},
        {21, "OUT_OF_RANGE_21", 16},
    };
    for (const auto &sp : kSpecs) {
      uint32_t words[32] = {};
      if (sp.feat == 2 || sp.feat == 3) {
        words[0] = (uint32_t)DXGI_FORMAT_R8G8B8A8_UNORM;
      }
      HRESULT hr_null = dev->CheckFeatureSupport((D3D11_FEATURE)sp.feat, nullptr, g_ported ? sp.right_size : 0);
      HRESULT hr_bad_sz = dev->CheckFeatureSupport((D3D11_FEATURE)sp.feat, words, 1);
      std::memset(words, 0, sizeof(words));
      if (sp.feat == 2 || sp.feat == 3) {
        words[0] = (uint32_t)DXGI_FORMAT_R8G8B8A8_UNORM;
      }
      HRESULT hr_ok = dev->CheckFeatureSupport((D3D11_FEATURE)sp.feat, words, sp.right_size);
      Out("d11dev feat=%u(%s) null=%08x badsz=%08x ok=%08x",
          sp.feat, sp.name, (unsigned)hr_null, (unsigned)hr_bad_sz, (unsigned)hr_ok);
      UINT nwords = sp.right_size / 4;
      for (UINT w = 0; w < nwords; ++w) {
        Out(" w%u=%08x", w, words[w]);
      }
      Out("\n");
      ops += 3;
    }
  }

  // 5. CheckFormatSupport & FORMAT_SUPPORT2 & CheckMultisampleQualityLevels(1) for 0..=191
  {
    P<ID3D11Device2> dev2;
    dev->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void **>(&dev2));
    for (UINT fmt = 0; fmt <= 191; ++fmt) {
      UINT sup1 = 0xdeadbeef;
      HRESULT hr1 = dev->CheckFormatSupport((DXGI_FORMAT)fmt, &sup1);
      D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fs2 = {(DXGI_FORMAT)fmt, 0};
      HRESULT hr2 = dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fs2, sizeof(fs2));
      UINT q1 = 99, q2 = 99, q4 = 99, q8 = 99;
      HRESULT hq1 = dev->CheckMultisampleQualityLevels((DXGI_FORMAT)fmt, 1, &q1);
      HRESULT hq2 = dev->CheckMultisampleQualityLevels((DXGI_FORMAT)fmt, 2, &q2);
      HRESULT hq4 = dev->CheckMultisampleQualityLevels((DXGI_FORMAT)fmt, 4, &q4);
      HRESULT hq8 = dev->CheckMultisampleQualityLevels((DXGI_FORMAT)fmt, 8, &q8);
      UINT q1_4_f0 = 99, q1_4_f1 = 99;
      HRESULT hq1_f0 = dev2 ? dev2->CheckMultisampleQualityLevels1((DXGI_FORMAT)fmt, 4, 0, &q1_4_f0) : E_FAIL;
      HRESULT hq1_f1 = dev2 ? dev2->CheckMultisampleQualityLevels1((DXGI_FORMAT)fmt, 4, 1, &q1_4_f1) : E_FAIL;
      Out("d11dev fmt=%03u sup1=%08x:%08x sup2=%08x:%08x msq=%08x/%u,%08x/%u,%08x/%u,%08x/%u msq1=%08x/%u,%08x/%u\n",
          fmt, (unsigned)hr1, sup1, (unsigned)hr2, fs2.OutFormatSupport2,
          (unsigned)hq1, q1, (unsigned)hq2, q2, (unsigned)hq4, q4, (unsigned)hq8, q8,
          (unsigned)hq1_f0, q1_4_f0, (unsigned)hq1_f1, q1_4_f1);
      ops += 8;
    }
    UINT q_unk = 99, q_s3 = 99, q_s16 = 99;
    HRESULT hr_fmt_null = dev->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, nullptr);
    HRESULT hr_msq_null = g_ported
                              ? dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 1, nullptr)
                              : dev->CheckMultisampleQualityLevels(DXGI_FORMAT_UNKNOWN, 1, &q_unk);
    HRESULT hr_msq_s3 = dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 3, &q_s3);
    HRESULT hr_msq_s16 = dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 16, &q_s16);
    Out("d11dev fmt_edge fmt_null=%08x msq_null=%08x s3=%08x/%u s16=%08x/%u\n",
        (unsigned)hr_fmt_null, (unsigned)hr_msq_null,
        (unsigned)hr_msq_s3, q_s3, (unsigned)hr_msq_s16, q_s16);
  }

  // 6. CheckCounterInfo, CheckCounter, flags, exception mode, removed reason
  {
    D3D11_COUNTER_INFO ci = {static_cast<D3D11_COUNTER>(0x1234), 99, 99};
    dev->CheckCounterInfo(&ci);
    D3D11_COUNTER_DESC cd = {D3D11_COUNTER_DEVICE_DEPENDENT_0, 0};
    D3D11_COUNTER_TYPE ct = (D3D11_COUNTER_TYPE)0;
    UINT active = 0;
    HRESULT hr_cnt = dev->CheckCounter(&cd, &ct, &active, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    HRESULT hr_cnt_null = dev->CheckCounter(nullptr, &ct, &active, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    UINT cflags = dev->GetCreationFlags();
    HRESULT hr_rem = dev->GetDeviceRemovedReason();
    UINT exc0 = dev->GetExceptionMode();
    HRESULT hr_set_exc = dev->SetExceptionMode(D3D11_RAISE_FLAG_DRIVER_INTERNAL_ERROR);
    UINT exc1 = dev->GetExceptionMode();
    Out("d11dev misc counter_info=%d,%u,%u check_cnt=%08x check_cnt_null=%08x flags=%08x rem=%08x exc=%u,%08x,%u\n",
        (int)ci.LastDeviceDependentCounter, ci.NumSimultaneousCounters,
        (unsigned)ci.NumDetectableParallelUnits,
        (unsigned)hr_cnt, (unsigned)hr_cnt_null, cflags, (unsigned)hr_rem,
        exc0, (unsigned)hr_set_exc, exc1);
  }

  // 7. GetImmediateContext / 1 / 2 / 3 & refcount interaction
  {
    ULONG dev_r0 = dev->AddRef();
    dev->Release();
    ID3D11DeviceContext *ic0 = nullptr;
    dev->GetImmediateContext(&ic0);
    ULONG dev_r1 = dev->AddRef();
    dev->Release();
    int same_ctx = (ic0 == ctx.p) ? 1 : 0;
    if (ic0) ic0->Release();

    P<ID3D11Device1> d1;
    P<ID3D11Device2> d2;
    P<ID3D11Device3> d3;
    dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&d1));
    dev->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void **>(&d2));
    dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&d3));
    P<ID3D11DeviceContext1> ic1;
    P<ID3D11DeviceContext2> ic2;
    P<ID3D11DeviceContext3> ic3;
    if (d1) d1->GetImmediateContext1(&ic1);
    if (d2) d2->GetImmediateContext2(&ic2);
    if (d3) d3->GetImmediateContext3(&ic3);
    Out("d11dev imm_ctx same=%d dev_ref_delta=%ld ic1=%d ic2=%d ic3=%d\n",
        same_ctx, (long)dev_r1 - (long)dev_r0, ic1 ? 1 : 0, ic2 ? 1 : 0, ic3 ? 1 : 0);
  }

  // 8. ID3D11Multithread
  {
    P<ID3D11Multithread> mt;
    dev->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void **>(&mt));
    if (mt) {
      BOOL b0 = mt->GetMultithreadProtected();
      BOOL prev1 = mt->SetMultithreadProtected(TRUE);
      BOOL b1 = mt->GetMultithreadProtected();
      mt->Enter();
      mt->Leave();
      BOOL prev2 = mt->SetMultithreadProtected(FALSE);
      BOOL b2 = mt->GetMultithreadProtected();
      Out("d11dev multithread b0=%d prev1=%d b1=%d prev2=%d b2=%d\n",
          (int)b0, (int)prev1, (int)b1, (int)prev2, (int)b2);
    }
  }

  // 9. CreateDeviceContextState
  {
    P<ID3D11Device1> d1;
    dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&d1));
    if (d1) {
      D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
      P<ID3DDeviceContextState> dcs;
      HRESULT hr_null_out = d1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11DeviceContext), nullptr, nullptr);
      HRESULT hr_null_fl = d1->CreateDeviceContextState(0, nullptr, 1, D3D11_SDK_VERSION, __uuidof(ID3D11DeviceContext), nullptr, &dcs);
      HRESULT hr_zero_fl = d1->CreateDeviceContextState(0, &fl, 0, D3D11_SDK_VERSION, __uuidof(ID3D11DeviceContext), nullptr, &dcs);
      HRESULT hr_bad_iid = d1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, kIID_Bogus, nullptr, &dcs);
      D3D_FEATURE_LEVEL chosen = (D3D_FEATURE_LEVEL)0;
      HRESULT hr_ok11 = d1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11DeviceContext), &chosen, &dcs);
      P<ID3DDeviceContextState> dcs10;
      HRESULT hr_ok10 = d1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D10Device), nullptr, &dcs10);
      Out("d11dev dcs null_out=%08x null_fl=%08x zero_fl=%08x bad_iid=%08x ok11=%08x(fl=%04x) ok10=%08x\n",
          (unsigned)hr_null_out, (unsigned)hr_null_fl, (unsigned)hr_zero_fl,
          (unsigned)hr_bad_iid, (unsigned)hr_ok11, (unsigned)chosen, (unsigned)hr_ok10);
      if (dcs) {
        static const iid_entry kDcsIids[] = {
            {"IUnknown", __uuidof(IUnknown)},
            {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
            {"ID3DDeviceContextState", __uuidof(ID3DDeviceContextState)},
            {"Bogus", kIID_Bogus},
        };
        ProbeQiList("d11dev", "dcs", dcs, kDcsIids, 4);
        ProbeChildGetDevice("d11dev", "dcs", dcs, dev);
        ProbePrivateData("d11dev", "dcs", dcs.p);
      }
    }
  }

  // 10. ID3D11Device5::CreateFence & ID3D11DeviceContext4::Signal/Wait
  {
    P<ID3D11Device5> d5;
    dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void **>(&d5));
    P<ID3D11DeviceContext4> c4;
    ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void **>(&c4));
    if (d5 && c4) {
      P<ID3D11Fence> fence;
      HRESULT hr_null = d5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), nullptr);
      HRESULT hr_create = d5->CreateFence(7, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), reinterpret_cast<void **>(&fence));
      UINT64 v0 = fence ? fence->GetCompletedValue() : 0;
      HANDLE h_shared = nullptr;
      HRESULT hr_sh = fence ? fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &h_shared) : E_FAIL;
      HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      HRESULT hr_ev = fence ? fence->SetEventOnCompletion(9, ev) : E_FAIL;
      HRESULT hr_sig = fence ? c4->Signal(fence, 9) : E_FAIL;
      ctx->Flush();
      DWORD wait_res = ev ? WaitForSingleObject(ev, 2000) : WAIT_FAILED;
      if (ev) CloseHandle(ev);
      UINT64 v1 = fence ? fence->GetCompletedValue() : 0;
      HRESULT hr_wait = fence ? c4->Wait(fence, 9) : E_FAIL;
      P<ID3D11Fence> opened;
      HRESULT hr_open = d5->OpenSharedFence(nullptr, __uuidof(ID3D11Fence), reinterpret_cast<void **>(&opened));
      Out("d11dev fence null=%08x create=%08x v0=%llu sh=%08x ev=%08x sig=%08x w=%u v1=%llu cwait=%08x open=%08x\n",
          (unsigned)hr_null, (unsigned)hr_create, (unsigned long long)v0,
          (unsigned)hr_sh, (unsigned)hr_ev, (unsigned)hr_sig,
          (unsigned)(wait_res == WAIT_OBJECT_0 ? 1 : 0),
          (unsigned long long)v1, (unsigned)hr_wait, (unsigned)hr_open);
      if (fence) {
        static const iid_entry kFenceIids[] = {
            {"IUnknown", __uuidof(IUnknown)},
            {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
            {"ID3D11Fence", __uuidof(ID3D11Fence)},
            {"Bogus", kIID_Bogus},
        };
        ProbeQiList("d11dev", "fence", fence, kFenceIids, 4);
        ProbeChildGetDevice("d11dev", "fence", fence, dev);
        ProbePrivateData("d11dev", "fence", fence.p);
      }
    }
  }

  // 11. D3D11On12CreateDevice stub
  {
    P<ID3D11Device> d12;
    HRESULT hr_11on12 = D3D11On12CreateDevice(
        dev, 0, nullptr, 0, nullptr, 0, 0, &d12, nullptr, nullptr);
    Out("d11dev d3d11on12 hr=%08x out=%s\n", (unsigned)hr_11on12, d12 ? "ptr" : "null");
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11dev %.1f\n", dt > 0 ? ops / dt : 0.0);
}
static void RunD11Res() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;
  P<ID3D11Device3> dev3;
  dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&dev3));

  // 1. CreateBuffer valid + invalid table
  {
    uint32_t init_data[16] = {1, 2, 3, 4, 5, 6, 7, 8};
    D3D11_SUBRESOURCE_DATA srd = {init_data, 64, 64};

    struct buf_case {
      const char *name;
      D3D11_BUFFER_DESC desc;
      bool use_init;
      bool null_desc;
      bool null_out;
    };
    const buf_case cases[] = {
        {"zero_width", {0, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0}, false, false, false},
        {"imm_no_init", {64, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0}, false, false, false},
        {"imm_with_init", {64, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0}, true, false, false},
        {"vb_default", {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0}, false, false, false},
        {"cb_valid", {64, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0}, false, false, false},
        {"staging", {64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0}, false, false, false},
        {"raw_uav_srv", {64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0}, false, false, false},
        {"structured", {64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16}, false, false, false},
        {"draw_indirect", {64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0}, false, false, false},
        {"stream_output", {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT, 0, 0, 0}, false, false, false},
    };
    for (const auto &c : cases) {
      P<ID3D11Buffer> buf;
      HRESULT hr = dev->CreateBuffer(
          c.null_desc ? nullptr : &c.desc,
          c.use_init ? &srd : nullptr,
          c.null_out ? nullptr : &buf);
      D3D11_BUFFER_DESC got = {};
      D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
      if (buf) {
        buf->GetDesc(&got);
        buf->GetType(&dim);
      }
      Out("d11res buf case=%s hr=%08x out=%s dim=%d bw=%u usage=%d bind=%x cpu=%x misc=%x stride=%u\n",
          c.name, (unsigned)hr, buf ? "ptr" : "null", (int)dim,
          got.ByteWidth, (int)got.Usage, got.BindFlags, got.CPUAccessFlags,
          got.MiscFlags, got.StructureByteStride);
      ops++;
    }

    // NOT_ON_C: null pDesc, null ppBuffer, and invalid Usage enum deref/abort in C++ d3d11_device.cpp:85,97 & d3d11_buffer.cpp:317
    if (g_ported) {
      P<ID3D11Buffer> b_null;
      HRESULT hr_null = dev->CreateBuffer(nullptr, nullptr, &b_null);
      D3D11_BUFFER_DESC vb_desc = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
      HRESULT hr_null_out = dev->CreateBuffer(&vb_desc, nullptr, nullptr);
      D3D11_BUFFER_DESC bad_usage = {64, static_cast<D3D11_USAGE>(99), D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
      P<ID3D11Buffer> b_bad;
      HRESULT hr_bad = dev->CreateBuffer(&bad_usage, nullptr, &b_bad);
      Out("d11res create_buffer_invalid null=%08x null_out=%08x usage=%08x out=%s\n",
          (unsigned)hr_null, (unsigned)hr_null_out, (unsigned)hr_bad, b_bad ? "ptr" : "null");
    }
  }

  // 2. Buffer QI, EvictionPriority, IDXGIResource, PrivateData
  {
    D3D11_BUFFER_DESC bd = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    P<ID3D11Buffer> buf;
    dev->CreateBuffer(&bd, nullptr, &buf);
    if (buf) {
      static const iid_entry kBufIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11Resource", __uuidof(ID3D11Resource)},
          {"ID3D11Buffer", __uuidof(ID3D11Buffer)},
          {"ID3D10Resource", __uuidof(ID3D10Resource)},
          {"ID3D10Buffer", __uuidof(ID3D10Buffer)},
          {"IDXGIObject", __uuidof(IDXGIObject)},
          {"IDXGIDeviceSubObject", __uuidof(IDXGIDeviceSubObject)},
          {"IDXGIResource", __uuidof(IDXGIResource)},
          {"IDXGIResource1", __uuidof(IDXGIResource1)},
          {"IDXGISurface", __uuidof(IDXGISurface)},
          {"IMTLMinLODClampable", kIID_IMTLMinLODClampable},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11res", "buffer", buf, kBufIids, sizeof(kBufIids) / sizeof(kBufIids[0]));
      ProbeChildGetDevice("d11res", "buffer", buf, dev);
      ProbePrivateData("d11res", "buffer", buf.p);
      UINT ep0 = buf->GetEvictionPriority();
      buf->SetEvictionPriority(DXGI_RESOURCE_PRIORITY_HIGH);
      UINT ep1 = buf->GetEvictionPriority();
      Out("d11res buffer_evict ep0=%08x ep1=%08x\n", ep0, ep1);
    }
  }

  // 3. CreateTexture1D valid + invalid
  {
    D3D11_TEXTURE1D_DESC td = {64, 0, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    P<ID3D11Texture1D> t1d;
    HRESULT hr_ok = dev->CreateTexture1D(&td, nullptr, &t1d);
    HRESULT hr_null = dev->CreateTexture1D(nullptr, nullptr, &t1d);
    D3D11_TEXTURE1D_DESC td_zero = td;
    td_zero.Width = 0;
    P<ID3D11Texture1D> t1d_bad;
    HRESULT hr_zero = dev->CreateTexture1D(&td_zero, nullptr, &t1d_bad);
    D3D11_TEXTURE1D_DESC got = {};
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    if (t1d) {
      t1d->GetDesc(&got);
      t1d->GetType(&dim);
    }
    Out("d11res tex1d ok=%08x null=%08x zero_w=%08x dim=%d w=%u mips=%u arr=%u fmt=%u\n",
        (unsigned)hr_ok, (unsigned)hr_null, (unsigned)hr_zero, (int)dim,
        got.Width, got.MipLevels, got.ArraySize, (unsigned)got.Format);
    if (t1d) {
      static const iid_entry kT1dIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11Resource", __uuidof(ID3D11Resource)},
          {"ID3D11Texture1D", __uuidof(ID3D11Texture1D)},
          {"ID3D10Texture1D", __uuidof(ID3D10Texture1D)},
          {"IDXGIResource1", __uuidof(IDXGIResource1)},
          {"IDXGISurface", __uuidof(IDXGISurface)},
          {"IMTLMinLODClampable", kIID_IMTLMinLODClampable},
      };
      ProbeQiList("d11res", "tex1d", t1d, kT1dIids, sizeof(kT1dIids) / sizeof(kT1dIids[0]));
    }
  }

  // 4. CreateTexture2D + CreateTexture2D1 + IDXGISurface + IDXGIResource1
  {
    D3D11_TEXTURE2D_DESC td = {
        32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
    P<ID3D11Texture2D> t2d_single;
    HRESULT hr_single = dev->CreateTexture2D(&td, nullptr, &t2d_single);

    D3D11_TEXTURE2D_DESC td_mips = td;
    td_mips.MipLevels = 0;
    td_mips.ArraySize = 2;
    P<ID3D11Texture2D> t2d_mips;
    HRESULT hr_mips = dev->CreateTexture2D(&td_mips, nullptr, &t2d_mips);
    D3D11_TEXTURE2D_DESC got_mips = {};
    if (t2d_mips) t2d_mips->GetDesc(&got_mips);

    D3D11_TEXTURE2D_DESC1 td1 = {
        16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
        D3D11_TEXTURE_LAYOUT_UNDEFINED};
    P<ID3D11Texture2D1> t2d1;
    HRESULT hr_t2d1 = dev3 ? dev3->CreateTexture2D1(&td1, nullptr, &t2d1) : E_FAIL;
    D3D11_TEXTURE2D_DESC1 got1 = {};
    if (t2d1) t2d1->GetDesc1(&got1);

    D3D11_TEXTURE2D_DESC1 td1_swiz = td1;
    td1_swiz.TextureLayout = D3D11_TEXTURE_LAYOUT_64K_STANDARD_SWIZZLE;
    P<ID3D11Texture2D1> t2d1_swiz;
    HRESULT hr_swiz = dev3 ? dev3->CreateTexture2D1(&td1_swiz, nullptr, &t2d1_swiz) : E_FAIL;

    Out("d11res tex2d single=%08x mips=%08x(computed_mips=%u) t2d1=%08x(layout=%d) swiz=%08x\n",
        (unsigned)hr_single, (unsigned)hr_mips, got_mips.MipLevels,
        (unsigned)hr_t2d1, (int)got1.TextureLayout, (unsigned)hr_swiz);

    if (t2d_single) {
      static const iid_entry kT2dIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11Resource", __uuidof(ID3D11Resource)},
          {"ID3D11Texture2D", __uuidof(ID3D11Texture2D)},
          {"ID3D11Texture2D1", __uuidof(ID3D11Texture2D1)},
          {"ID3D10Texture2D", __uuidof(ID3D10Texture2D)},
          {"IDXGIResource", __uuidof(IDXGIResource)},
          {"IDXGIResource1", __uuidof(IDXGIResource1)},
          {"IDXGISurface", __uuidof(IDXGISurface)},
          {"IDXGISurface1", __uuidof(IDXGISurface1)},
          {"IDXGISurface2", __uuidof(IDXGISurface2)},
          {"IMTLMinLODClampable", kIID_IMTLMinLODClampable},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11res", "tex2d_single", t2d_single, kT2dIids, sizeof(kT2dIids) / sizeof(kT2dIids[0]));
      ProbeChildGetDevice("d11res", "tex2d", t2d_single, dev);
      ProbePrivateData("d11res", "tex2d", t2d_single.p);

      P<IDXGIResource1> dxgi_res;
      t2d_single->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void **>(&dxgi_res));
      if (dxgi_res) {
        DXGI_USAGE usage = 0;
        HRESULT hr_u = dxgi_res->GetUsage(&usage);
        P<IDXGISurface2> sub0, sub1;
        HRESULT hr_sub0 = dxgi_res->CreateSubresourceSurface(0, &sub0);
        HRESULT hr_sub1 = dxgi_res->CreateSubresourceSurface(1, &sub1);
        Out("d11res tex2d_dxgi usage=%08x/%08x sub0=%08x sub1=%08x\n",
            (unsigned)hr_u, usage, (unsigned)hr_sub0, (unsigned)hr_sub1);
      }
    }
    if (t2d_mips) {
      P<IDXGISurface> surf_multi;
      HRESULT hr_surf_multi = t2d_mips->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void **>(&surf_multi));
      Out("d11res tex2d_multi_surf hr=%08x out=%s\n", (unsigned)hr_surf_multi, surf_multi ? "ptr" : "null");
    }
  }

  // 5. CreateTexture3D + CreateTexture3D1
  {
    D3D11_TEXTURE3D_DESC td = {
        16, 16, 8, 0, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    P<ID3D11Texture3D> t3d;
    HRESULT hr_ok = dev->CreateTexture3D(&td, nullptr, &t3d);
    D3D11_TEXTURE3D_DESC got = {};
    if (t3d) t3d->GetDesc(&got);
    D3D11_TEXTURE3D_DESC1 td1 = {
        8, 8, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0,
        D3D11_TEXTURE_LAYOUT_UNDEFINED};
    P<ID3D11Texture3D1> t3d1;
    HRESULT hr_t3d1 = dev3 ? dev3->CreateTexture3D1(&td1, nullptr, &t3d1) : E_FAIL;
    Out("d11res tex3d ok=%08x(mips=%u) t3d1=%08x\n",
        (unsigned)hr_ok, got.MipLevels, (unsigned)hr_t3d1);
    if (t3d) {
      static const iid_entry kT3dIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11Resource", __uuidof(ID3D11Resource)},
          {"ID3D11Texture3D", __uuidof(ID3D11Texture3D)},
          {"ID3D11Texture3D1", __uuidof(ID3D11Texture3D1)},
          {"ID3D10Texture3D", __uuidof(ID3D10Texture3D)},
          {"IDXGISurface", __uuidof(IDXGISurface)},
          {"IMTLMinLODClampable", kIID_IMTLMinLODClampable},
      };
      ProbeQiList("d11res", "tex3d", t3d, kT3dIids, sizeof(kT3dIids) / sizeof(kT3dIids[0]));
    }
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11res %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11View() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;
  P<ID3D11Device3> dev3;
  dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&dev3));

  D3D11_TEXTURE2D_DESC td = {
      16, 16, 2, 2, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
      D3D11_USAGE_DEFAULT,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS,
      0, 0};
  P<ID3D11Texture2D> tex;
  dev->CreateTexture2D(&td, nullptr, &tex);

  D3D11_TEXTURE2D_DESC dsd = {
      16, 16, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0},
      D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  P<ID3D11Texture2D> dstex;
  dev->CreateTexture2D(&dsd, nullptr, &dstex);

  D3D11_BUFFER_DESC bd = {
      64, D3D11_USAGE_DEFAULT,
      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
      0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
  P<ID3D11Buffer> rawbuf;
  dev->CreateBuffer(&bd, nullptr, &rawbuf);

  D3D11_BUFFER_DESC vbd = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  P<ID3D11Buffer> vb_only;
  dev->CreateBuffer(&vbd, nullptr, &vb_only);

  // 1. SRV & SRV1
  {
    P<ID3D11ShaderResourceView> srv_def, srv_bad1, srv_bad2, srv_raw;
    HRESULT hr_null_res = dev->CreateShaderResourceView(nullptr, nullptr, &srv_bad1);
    HRESULT hr_no_bind = dev->CreateShaderResourceView(vb_only, nullptr, &srv_bad2);
    HRESULT hr_def = dev->CreateShaderResourceView(tex, nullptr, &srv_def);
    D3D11_SHADER_RESOURCE_VIEW_DESC got_def = {};
    if (srv_def) srv_def->GetDesc(&got_def);

    D3D11_SHADER_RESOURCE_VIEW_DESC raw_desc = {};
    raw_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    raw_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    raw_desc.BufferEx.FirstElement = 0;
    raw_desc.BufferEx.NumElements = 16;
    raw_desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    HRESULT hr_raw = dev->CreateShaderResourceView(rawbuf, &raw_desc, &srv_raw);

    D3D11_SHADER_RESOURCE_VIEW_DESC1 srv1_desc = {};
    srv1_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv1_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv1_desc.Texture2D.MostDetailedMip = 0;
    srv1_desc.Texture2D.MipLevels = 1;
    srv1_desc.Texture2D.PlaneSlice = 0;
    P<ID3D11ShaderResourceView1> srv1_ok, srv1_plane1;
    HRESULT hr_srv1 = dev3 ? dev3->CreateShaderResourceView1(tex, &srv1_desc, &srv1_ok) : E_FAIL;
    srv1_desc.Texture2D.PlaneSlice = 1;
    HRESULT hr_srv1_p1 = dev3 ? dev3->CreateShaderResourceView1(tex, &srv1_desc, &srv1_plane1) : E_FAIL;

    Out("d11view srv null_res=%08x no_bind=%08x def=%08x(dim=%d,fmt=%u,mips=%u,arr=%u) raw=%08x srv1=%08x srv1_p1=%08x\n",
        (unsigned)hr_null_res, (unsigned)hr_no_bind, (unsigned)hr_def,
        (int)got_def.ViewDimension, (unsigned)got_def.Format,
        got_def.Texture2DArray.MipLevels, got_def.Texture2DArray.ArraySize,
        (unsigned)hr_raw, (unsigned)hr_srv1, (unsigned)hr_srv1_p1);

    if (srv_def) {
      static const iid_entry kSrvIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11View", __uuidof(ID3D11View)},
          {"ID3D11ShaderResourceView", __uuidof(ID3D11ShaderResourceView)},
          {"ID3D11ShaderResourceView1", __uuidof(ID3D11ShaderResourceView1)},
          {"ID3D10View", __uuidof(ID3D10View)},
          {"ID3D10ShaderResourceView", __uuidof(ID3D10ShaderResourceView)},
          {"ID3D10ShaderResourceView1", __uuidof(ID3D10ShaderResourceView1)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11view", "srv", srv_def, kSrvIids, sizeof(kSrvIids) / sizeof(kSrvIids[0]));
      ProbeChildGetDevice("d11view", "srv", srv_def, dev);
      ProbePrivateData("d11view", "srv", srv_def.p);
      P<ID3D11Resource> res;
      srv_def->GetResource(&res);
      Out("d11view srv_getres match=%d\n", (res.p == static_cast<ID3D11Resource *>(tex.p)) ? 1 : 0);
    }
    ops += 6;
  }

  // 2. RTV & RTV1
  {
    P<ID3D11RenderTargetView> rtv_def, rtv_bad1, rtv_bad2;
    HRESULT hr_null_res = dev->CreateRenderTargetView(nullptr, nullptr, &rtv_bad1);
    HRESULT hr_no_bind = dev->CreateRenderTargetView(vb_only, nullptr, &rtv_bad2);
    HRESULT hr_def = dev->CreateRenderTargetView(tex, nullptr, &rtv_def);
    D3D11_RENDER_TARGET_VIEW_DESC got_def = {};
    if (rtv_def) rtv_def->GetDesc(&got_def);

    D3D11_RENDER_TARGET_VIEW_DESC1 rtv1_desc = {};
    rtv1_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtv1_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtv1_desc.Texture2D.MipSlice = 0;
    rtv1_desc.Texture2D.PlaneSlice = 0;
    P<ID3D11RenderTargetView1> rtv1_ok, rtv1_p1;
    HRESULT hr_rtv1 = dev3 ? dev3->CreateRenderTargetView1(tex, &rtv1_desc, &rtv1_ok) : E_FAIL;
    rtv1_desc.Texture2D.PlaneSlice = 1;
    HRESULT hr_rtv1_p1 = dev3 ? dev3->CreateRenderTargetView1(tex, &rtv1_desc, &rtv1_p1) : E_FAIL;

    Out("d11view rtv null_res=%08x no_bind=%08x def=%08x(dim=%d,fmt=%u,arr=%u) rtv1=%08x rtv1_p1=%08x\n",
        (unsigned)hr_null_res, (unsigned)hr_no_bind, (unsigned)hr_def,
        (int)got_def.ViewDimension, (unsigned)got_def.Format,
        got_def.Texture2DArray.ArraySize, (unsigned)hr_rtv1, (unsigned)hr_rtv1_p1);

    if (rtv_def) {
      static const iid_entry kRtvIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11View", __uuidof(ID3D11View)},
          {"ID3D11RenderTargetView", __uuidof(ID3D11RenderTargetView)},
          {"ID3D11RenderTargetView1", __uuidof(ID3D11RenderTargetView1)},
          {"ID3D10RenderTargetView", __uuidof(ID3D10RenderTargetView)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11view", "rtv", rtv_def, kRtvIids, sizeof(kRtvIids) / sizeof(kRtvIids[0]));
      ProbeChildGetDevice("d11view", "rtv", rtv_def, dev);
      ProbePrivateData("d11view", "rtv", rtv_def.p);
    }
    ops += 5;
  }

  // 3. DSV
  {
    P<ID3D11DepthStencilView> dsv_ok, dsv_ro, dsv_bad1, dsv_bad_flags;
    HRESULT hr_null_res = dev->CreateDepthStencilView(nullptr, nullptr, &dsv_bad1);
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvd = {};
    dsvd.Format = DXGI_FORMAT_D32_FLOAT;
    dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvd.Flags = 0;
    dsvd.Texture2D.MipSlice = 0;
    HRESULT hr_ok = dev->CreateDepthStencilView(dstex, &dsvd, &dsv_ok);
    dsvd.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    HRESULT hr_ro = dev->CreateDepthStencilView(dstex, &dsvd, &dsv_ro);
    dsvd.Flags = 0x80;
    HRESULT hr_bad_f = dev->CreateDepthStencilView(dstex, &dsvd, &dsv_bad_flags);
    Out("d11view dsv null_res=%08x ok=%08x ro=%08x bad_flags=%08x\n",
        (unsigned)hr_null_res, (unsigned)hr_ok, (unsigned)hr_ro, (unsigned)hr_bad_f);
    if (dsv_ok) {
      static const iid_entry kDsvIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11View", __uuidof(ID3D11View)},
          {"ID3D11DepthStencilView", __uuidof(ID3D11DepthStencilView)},
          {"ID3D10DepthStencilView", __uuidof(ID3D10DepthStencilView)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11view", "dsv", dsv_ok, kDsvIids, sizeof(kDsvIids) / sizeof(kDsvIids[0]));
      ProbeChildGetDevice("d11view", "dsv", dsv_ok, dev);
      ProbePrivateData("d11view", "dsv", dsv_ok.p);
    }
    ops += 4;
  }

  // 4. UAV & UAV1
  {
    P<ID3D11UnorderedAccessView> uav_def, uav_raw, uav_bad1;
    HRESULT hr_null_res = dev->CreateUnorderedAccessView(nullptr, nullptr, &uav_bad1);
    HRESULT hr_def = dev->CreateUnorderedAccessView(tex, nullptr, &uav_def);
    D3D11_UNORDERED_ACCESS_VIEW_DESC got_def = {};
    if (uav_def) uav_def->GetDesc(&got_def);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_raw_desc = {};
    uav_raw_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    uav_raw_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uav_raw_desc.Buffer.FirstElement = 0;
    uav_raw_desc.Buffer.NumElements = 16;
    uav_raw_desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    HRESULT hr_raw = dev->CreateUnorderedAccessView(rawbuf, &uav_raw_desc, &uav_raw);

    D3D11_UNORDERED_ACCESS_VIEW_DESC1 uav1_desc = {};
    uav1_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uav1_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uav1_desc.Texture2D.MipSlice = 0;
    uav1_desc.Texture2D.PlaneSlice = 0;
    P<ID3D11UnorderedAccessView1> uav1_ok, uav1_p1;
    HRESULT hr_uav1 = dev3 ? dev3->CreateUnorderedAccessView1(tex, &uav1_desc, &uav1_ok) : E_FAIL;
    uav1_desc.Texture2D.PlaneSlice = 1;
    HRESULT hr_uav1_p1 = dev3 ? dev3->CreateUnorderedAccessView1(tex, &uav1_desc, &uav1_p1) : E_FAIL;

    Out("d11view uav null_res=%08x def=%08x(dim=%d,fmt=%u) raw=%08x uav1=%08x uav1_p1=%08x\n",
        (unsigned)hr_null_res, (unsigned)hr_def, (int)got_def.ViewDimension,
        (unsigned)got_def.Format, (unsigned)hr_raw, (unsigned)hr_uav1, (unsigned)hr_uav1_p1);

    if (uav_def) {
      static const iid_entry kUavIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11View", __uuidof(ID3D11View)},
          {"ID3D11UnorderedAccessView", __uuidof(ID3D11UnorderedAccessView)},
          {"ID3D11UnorderedAccessView1", __uuidof(ID3D11UnorderedAccessView1)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11view", "uav", uav_def, kUavIids, sizeof(kUavIids) / sizeof(kUavIids[0]));
      ProbeChildGetDevice("d11view", "uav", uav_def, dev);
      ProbePrivateData("d11view", "uav", uav_def.p);
    }
    ops += 5;
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11view %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11State() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;
  P<ID3D11Device1> dev1;
  P<ID3D11Device3> dev3;
  dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&dev1));
  dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&dev3));

  // 1. BlendState & BlendState1 (dedup + ManagedDeviceChild refcount + validation)
  {
    D3D11_BLEND_DESC bd = {};
    bd.AlphaToCoverageEnable = FALSE;
    bd.IndependentBlendEnable = FALSE;
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    P<ID3D11BlendState> bs1, bs2, bs_bad;
    P<ID3D11BlendState1> bs1_null;
    HRESULT hr_null = dev1 ? dev1->CreateBlendState1(nullptr, &bs1_null) : E_FAIL;
    HRESULT hr_null_out = dev->CreateBlendState(&bd, nullptr);
    HRESULT hr1 = dev->CreateBlendState(&bd, &bs1);
    HRESULT hr2 = dev->CreateBlendState(&bd, &bs2);
    int same = (bs1.p == bs2.p && bs1.p != nullptr) ? 1 : 0;

    D3D11_BLEND_DESC bd_bad = bd;
    bd_bad.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA_SAT;
    bd_bad.RenderTarget[0].SrcBlendAlpha = static_cast<D3D11_BLEND>(99);
    HRESULT hr_bad = dev->CreateBlendState(&bd_bad, &bs_bad);

    D3D11_BLEND_DESC1 bd1 = {};
    bd1.RenderTarget[0].LogicOpEnable = TRUE;
    bd1.RenderTarget[0].BlendEnable = TRUE; // invalid: both LogicOpEnable and BlendEnable
    bd1.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_NOOP;
    bd1.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    P<ID3D11BlendState1> bs1_both;
    HRESULT hr_both = dev1 ? dev1->CreateBlendState1(&bd1, &bs1_both) : E_FAIL;

    Out("d11state blend null=%08x null_out=%08x hr1=%08x hr2=%08x same=%d bad=%08x logic_and_blend=%08x\n",
        (unsigned)hr_null, (unsigned)hr_null_out, (unsigned)hr1, (unsigned)hr2, same, (unsigned)hr_bad, (unsigned)hr_both);

    if (bs1) {
      static const iid_entry kBsIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11BlendState", __uuidof(ID3D11BlendState)},
          {"ID3D11BlendState1", __uuidof(ID3D11BlendState1)},
          {"ID3D10BlendState", __uuidof(ID3D10BlendState)},
          {"ID3D10BlendState1", __uuidof(ID3D10BlendState1)},
          {"IMTLD3D11BlendState", kIID_IMTLD3D11BlendState},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11state", "blend", bs1, kBsIids, sizeof(kBsIids) / sizeof(kBsIids[0]));
      ProbeChildGetDevice("d11state", "blend", bs1, dev);
      ProbePrivateData("d11state", "blend", bs1.p);
    }
    // Release both and recreate to verify ManagedDeviceChild lifecycle
    bs2.reset();
    bs1.reset();
    P<ID3D11BlendState> bs3;
    HRESULT hr3 = dev->CreateBlendState(&bd, &bs3);
    Out("d11state blend_recreate hr=%08x out=%s\n", (unsigned)hr3, bs3 ? "ptr" : "null");
    ops += 6;
  }

  // 2. DepthStencilState
  {
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    dsd.StencilEnable = FALSE;
    dsd.StencilReadMask = 0xff;
    dsd.StencilWriteMask = 0xff;
    dsd.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    dsd.BackFace = dsd.FrontFace;

    P<ID3D11DepthStencilState> ds1, ds2, ds_bad;
    HRESULT hr_null = dev->CreateDepthStencilState(nullptr, &ds_bad);
    HRESULT hr1 = dev->CreateDepthStencilState(&dsd, &ds1);
    HRESULT hr2 = dev->CreateDepthStencilState(&dsd, &ds2);
    int same = (ds1.p == ds2.p && ds1.p != nullptr) ? 1 : 0;
    Out("d11state ds null=%08x hr1=%08x hr2=%08x same=%d\n",
        (unsigned)hr_null, (unsigned)hr1, (unsigned)hr2, same);
    if (ds1) {
      static const iid_entry kDsIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DepthStencilState", __uuidof(ID3D11DepthStencilState)},
          {"ID3D10DepthStencilState", __uuidof(ID3D10DepthStencilState)},
          {"IMTLD3D11DepthStencilState", kIID_IMTLD3D11DepthStencilState},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11state", "ds", ds1, kDsIids, sizeof(kDsIids) / sizeof(kDsIids[0]));
      ProbeChildGetDevice("d11state", "ds", ds1, dev);
      ProbePrivateData("d11state", "ds", ds1.p);
    }
    ops += 4;
  }

  // 3. RasterizerState / 1 / 2
  {
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_BACK;
    rd.DepthClipEnable = TRUE;

    P<ID3D11RasterizerState> rs1, rs2;
    P<ID3D11RasterizerState2> rs2_bad;
    HRESULT hr_null = dev3 ? dev3->CreateRasterizerState2(nullptr, &rs2_bad) : E_FAIL;
    HRESULT hr_null_out = dev->CreateRasterizerState(&rd, nullptr);
    HRESULT hr1 = dev->CreateRasterizerState(&rd, &rs1);
    HRESULT hr2 = dev->CreateRasterizerState(&rd, &rs2);
    int same = (rs1.p == rs2.p && rs1.p != nullptr) ? 1 : 0;

    D3D11_RASTERIZER_DESC2 rd2 = {};
    rd2.FillMode = D3D11_FILL_SOLID;
    rd2.CullMode = D3D11_CULL_BACK;
    rd2.DepthClipEnable = TRUE;
    rd2.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_ON;
    P<ID3D11RasterizerState2> rs2_cons;
    HRESULT hr_cons = dev3 ? dev3->CreateRasterizerState2(&rd2, &rs2_cons) : E_FAIL;
    rd2.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    rd2.ForcedSampleCount = 3; // invalid sample count
    P<ID3D11RasterizerState2> rs2_fsc3;
    HRESULT hr_fsc3 = dev3 ? dev3->CreateRasterizerState2(&rd2, &rs2_fsc3) : E_FAIL;

    Out("d11state rast null=%08x null_out=%08x hr1=%08x hr2=%08x same=%d cons_on=%08x fsc3=%08x\n",
        (unsigned)hr_null, (unsigned)hr_null_out, (unsigned)hr1, (unsigned)hr2, same,
        (unsigned)hr_cons, (unsigned)hr_fsc3);
    if (rs1) {
      static const iid_entry kRsIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11RasterizerState", __uuidof(ID3D11RasterizerState)},
          {"ID3D11RasterizerState1", __uuidof(ID3D11RasterizerState1)},
          {"ID3D11RasterizerState2", __uuidof(ID3D11RasterizerState2)},
          {"ID3D10RasterizerState", __uuidof(ID3D10RasterizerState)},
          {"IMTLD3D11RasterizerState", kIID_IMTLD3D11RasterizerState},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11state", "rast", rs1, kRsIids, sizeof(kRsIids) / sizeof(kRsIids[0]));
      ProbeChildGetDevice("d11state", "rast", rs1, dev);
      ProbePrivateData("d11state", "rast", rs1.p);
    }
    ops += 6;
  }

  // 4. SamplerState
  {
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;

    P<ID3D11SamplerState> ss1, ss2, ss_bad;
    HRESULT hr_null = dev->CreateSamplerState(nullptr, &ss_bad);
    HRESULT hr1 = dev->CreateSamplerState(&sd, &ss1);
    HRESULT hr2 = dev->CreateSamplerState(&sd, &ss2);
    D3D11_SAMPLER_DESC sd_bad = sd;
    sd_bad.Filter = static_cast<D3D11_FILTER>(0xfff);
    HRESULT hr_bad_flt = dev->CreateSamplerState(&sd_bad, &ss_bad);
    Out("d11state samp null=%08x hr1=%08x hr2=%08x bad_flt=%08x\n",
        (unsigned)hr_null, (unsigned)hr1, (unsigned)hr2, (unsigned)hr_bad_flt);
    if (ss1) {
      static const iid_entry kSsIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11SamplerState", __uuidof(ID3D11SamplerState)},
          {"ID3D10SamplerState", __uuidof(ID3D10SamplerState)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11state", "samp", ss1, kSsIids, sizeof(kSsIids) / sizeof(kSsIids[0]));
      ProbeChildGetDevice("d11state", "samp", ss1, dev);
      ProbePrivateData("d11state", "samp", ss1.p);
    }
    ops += 4;
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11state %.1f\n", dt > 0 ? ops / dt : 0.0);
}
static void RunD11Shader() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;

  auto vs_quad = LoadShader("quad.vs.dxbc");
  auto vs_pt = LoadShader("point.vs.dxbc");
  auto vs_so = LoadShader("so.vs.dxbc");
  auto ps_solid = LoadShader("solid.ps.dxbc");
  auto hs_patch = LoadShader("patch.hs.dxbc");
  auto ds_patch = LoadShader("patch.ds.dxbc");
  auto cs_comp = LoadShader("compute.cs.dxbc");
  const uint32_t bogus_bc = 0xdeadbeef;

  // 1. InputLayout
  {
    D3D11_INPUT_ELEMENT_DESC elems[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    D3D11_INPUT_ELEMENT_DESC bad_elems[] = {
        {"NONEXISTENT_SEM", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    P<ID3D11InputLayout> il, il_bad1, il_bad2;
    HRESULT hr_null_out = dev->CreateInputLayout(
        elems, 1, vs_pt.data(), vs_pt.size(), nullptr);
    HRESULT hr_ok = dev->CreateInputLayout(
        elems, 1, vs_pt.data(), vs_pt.size(), &il);
    HRESULT hr_bogus_bc = dev->CreateInputLayout(
        elems, 1, &bogus_bc, sizeof(bogus_bc), &il_bad1);
    HRESULT hr_bad_sem = dev->CreateInputLayout(
        bad_elems, 1, vs_pt.data(), vs_pt.size(), &il_bad2);
    Out("d11shader il null_out=%08x ok=%08x bogus_bc=%08x bad_sem=%08x\n",
        (unsigned)hr_null_out, (unsigned)hr_ok, (unsigned)hr_bogus_bc, (unsigned)hr_bad_sem);
    if (il) {
      static const iid_entry kIlIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11InputLayout", __uuidof(ID3D11InputLayout)},
          {"ID3D10InputLayout", __uuidof(ID3D10InputLayout)},
          {"IMTLD3D11InputLayout", kIID_IMTLD3D11InputLayout},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11shader", "il", il, kIlIids, sizeof(kIlIids) / sizeof(kIlIids[0]));
      ProbeChildGetDevice("d11shader", "il", il, dev);
      ProbePrivateData("d11shader", "il", il.p);
    }
    ops += 4;
  }

  // 2. VS / PS / GS / GSWithSO / HS / DS / CS
  {
    P<ID3D11VertexShader> vs, vs_bad;
    HRESULT hr_gs_null = dev->CreateGeometryShader(vs_quad.data(), vs_quad.size(), nullptr, nullptr);
    HRESULT hr_vs_ok = dev->CreateVertexShader(vs_quad.data(), vs_quad.size(), nullptr, &vs);
    HRESULT hr_vs_bad = dev->CreateVertexShader(&bogus_bc, sizeof(bogus_bc), nullptr, &vs_bad);

    P<ID3D11PixelShader> ps;
    HRESULT hr_ps_ok = dev->CreatePixelShader(ps_solid.data(), ps_solid.size(), nullptr, &ps);

    P<ID3D11GeometryShader> gs, gs_so, gs_so_rast;
    HRESULT hr_gs_ok = dev->CreateGeometryShader(vs_quad.data(), vs_quad.size(), nullptr, &gs);
    D3D11_SO_DECLARATION_ENTRY so_decl[] = {
        {0, "SV_Position", 0, 0, 4, 0},
    };
    UINT stride = 16;
    HRESULT hr_gs_so = dev->CreateGeometryShaderWithStreamOutput(
        vs_so.data(), vs_so.size(), so_decl, 1, &stride, 1,
        D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs_so);
    HRESULT hr_gs_so_rast0 = dev->CreateGeometryShaderWithStreamOutput(
        vs_so.data(), vs_so.size(), so_decl, 1, &stride, 1,
        0, nullptr, &gs_so_rast);

    P<ID3D11HullShader> hs;
    HRESULT hr_hs_ok = dev->CreateHullShader(hs_patch.data(), hs_patch.size(), nullptr, &hs);
    P<ID3D11DomainShader> ds;
    HRESULT hr_ds_ok = dev->CreateDomainShader(ds_patch.data(), ds_patch.size(), nullptr, &ds);
    P<ID3D11ComputeShader> cs;
    HRESULT hr_cs_ok = dev->CreateComputeShader(cs_comp.data(), cs_comp.size(), nullptr, &cs);

    Out("d11shader create gs_null=%08x vs=%08x vs_bad=%08x ps=%08x gs=%08x gs_so=%08x gs_so_r0=%08x hs=%08x ds=%08x cs=%08x\n",
        (unsigned)hr_gs_null, (unsigned)hr_vs_ok, (unsigned)hr_vs_bad,
        (unsigned)hr_ps_ok, (unsigned)hr_gs_ok, (unsigned)hr_gs_so,
        (unsigned)hr_gs_so_rast0, (unsigned)hr_hs_ok, (unsigned)hr_ds_ok, (unsigned)hr_cs_ok);

    if (vs) {
      static const iid_entry kVsIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11VertexShader", __uuidof(ID3D11VertexShader)},
          {"ID3D10VertexShader", __uuidof(ID3D10VertexShader)},
          {"IMTLD3D11Shader", kIID_IMTLD3D11Shader},
          {"IMTLD3D11StreamOutputLayout", kIID_IMTLD3D11StreamOutputLayout},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11shader", "vs", vs, kVsIids, sizeof(kVsIids) / sizeof(kVsIids[0]));
      ProbeChildGetDevice("d11shader", "vs", vs, dev);
      ProbePrivateData("d11shader", "vs", vs.p);
    }
    if (gs_so) {
      static const iid_entry kGsSoIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11GeometryShader", __uuidof(ID3D11GeometryShader)},
          {"ID3D10GeometryShader", __uuidof(ID3D10GeometryShader)},
          {"IMTLD3D11Shader", kIID_IMTLD3D11Shader},
          {"IMTLD3D11StreamOutputLayout", kIID_IMTLD3D11StreamOutputLayout},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11shader", "gs_so", gs_so, kGsSoIids, sizeof(kGsSoIids) / sizeof(kGsSoIids[0]));
    }
    ops += 10;
  }

  // 3. ClassLinkage
  {
    P<ID3D11ClassLinkage> cl;
    HRESULT hr_cl = dev->CreateClassLinkage(&cl);
    P<ID3D11ClassInstance> ci1, ci2;
    HRESULT hr_create_ci = cl ? cl->CreateClassInstance("MyClass", 0, 0, 0, 0, &ci1) : E_FAIL;
    HRESULT hr_get_ci = cl ? cl->GetClassInstance("MyInst", 0, &ci2) : E_FAIL;
    Out("d11shader class_linkage create=%08x create_ci=%08x get_ci=%08x\n",
        (unsigned)hr_cl, (unsigned)hr_create_ci, (unsigned)hr_get_ci);
    if (cl) {
      static const iid_entry kClIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11ClassLinkage", __uuidof(ID3D11ClassLinkage)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11shader", "class_linkage", cl, kClIids, 4);
      ProbeChildGetDevice("d11shader", "class_linkage", cl, dev);
      ProbePrivateData("d11shader", "class_linkage", cl.p);
    }
    ops += 3;
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11shader %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11Ctx() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;
  P<ID3D11DeviceContext1> ctx1;
  ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void **>(&ctx1));

  // 1. Immediate Context QI, GetType, GetContextFlags, GetDevice, PrivateData
  {
    static const iid_entry kCtxIids[] = {
        {"IUnknown", __uuidof(IUnknown)},
        {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
        {"ID3D11DeviceContext", __uuidof(ID3D11DeviceContext)},
        {"ID3D11DeviceContext1", __uuidof(ID3D11DeviceContext1)},
        {"ID3D11DeviceContext2", __uuidof(ID3D11DeviceContext2)},
        {"ID3D11DeviceContext3", __uuidof(ID3D11DeviceContext3)},
        {"ID3D11DeviceContext4", __uuidof(ID3D11DeviceContext4)},
        {"ID3DUserDefinedAnnotation", __uuidof(ID3DUserDefinedAnnotation)},
        {"IMTLD3D11DeviceContext", kIID_IMTLD3D11DeviceContext},
        {"IMTLD3D11ContextExt", kIID_IMTLD3D11ContextExt},
        {"IMTLD3D11ContextExt1", kIID_IMTLD3D11ContextExt1},
        {"IMTLD3D11ContextExt2", kIID_IMTLD3D11ContextExt2},
        {"ID3D11VideoContext", kIID_ID3D11VideoContext},
        {"Bogus", kIID_Bogus},
    };
    ProbeQiList("d11ctx", "imm", ctx, kCtxIids, sizeof(kCtxIids) / sizeof(kCtxIids[0]));
    ProbeChildGetDevice("d11ctx", "imm", ctx, dev);
    ProbePrivateData("d11ctx", "imm", ctx.p);
    Out("d11ctx imm_meta type=%d flags=%08x\n", (int)ctx->GetType(), ctx->GetContextFlags());
  }

  // 2. Setter/Getter roundtrip + ClearState defaults
  {
    D3D11_BUFFER_DESC vbd = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT, 0, 0, 0};
    P<ID3D11Buffer> vb;
    dev->CreateBuffer(&vbd, nullptr, &vb);

    D3D11_BUFFER_DESC ibd = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    P<ID3D11Buffer> ib;
    dev->CreateBuffer(&ibd, nullptr, &ib);

    D3D11_BUFFER_DESC cbd = {256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    P<ID3D11Buffer> cb;
    dev->CreateBuffer(&cbd, nullptr, &cb);

    D3D11_TEXTURE2D_DESC td = {
        16, 16, 2, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS,
        0, 0};
    P<ID3D11Texture2D> tex;
    dev->CreateTexture2D(&td, nullptr, &tex);
    P<ID3D11ShaderResourceView> srv;
    dev->CreateShaderResourceView(tex, nullptr, &srv);
    P<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(tex, nullptr, &rtv);
    P<ID3D11UnorderedAccessView> uav;
    dev->CreateUnorderedAccessView(tex, nullptr, &uav);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxAnisotropy = 1;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    P<ID3D11SamplerState> samp;
    dev->CreateSamplerState(&sd, &samp);

    UINT stride = 16, offset = 4;
    ID3D11Buffer *vbs[1] = {vb.p};
    ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
    P<ID3D11Buffer> got_vb;
    UINT got_stride = 0, got_offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &got_vb, &got_stride, &got_offset);

    ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 8);
    P<ID3D11Buffer> got_ib;
    DXGI_FORMAT got_ib_fmt = DXGI_FORMAT_UNKNOWN;
    UINT got_ib_off = 0;
    ctx->IAGetIndexBuffer(&got_ib, &got_ib_fmt, &got_ib_off);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_PRIMITIVE_TOPOLOGY got_topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ctx->IAGetPrimitiveTopology(&got_topo);

    // ConstantBuffers1 with range
    ID3D11Buffer *cbs[1] = {cb.p};
    UINT first_c = 4, num_c = 8;
    if (ctx1) ctx1->VSSetConstantBuffers1(0, 1, cbs, &first_c, &num_c);
    P<ID3D11Buffer> got_cb;
    UINT got_fc = 0, got_nc = 0;
    if (ctx1) ctx1->VSGetConstantBuffers1(0, 1, &got_cb, &got_fc, &got_nc);

    ID3D11ShaderResourceView *srvs[1] = {srv.p};
    ctx->PSSetShaderResources(0, 1, srvs);
    P<ID3D11ShaderResourceView> got_srv;
    ctx->PSGetShaderResources(0, 1, &got_srv);

    ID3D11SamplerState *samps[1] = {samp.p};
    ctx->PSSetSamplers(0, 1, samps);
    P<ID3D11SamplerState> got_samp;
    ctx->PSGetSamplers(0, 1, &got_samp);

    ID3D11RenderTargetView *rtvs[1] = {rtv.p};
    ctx->OMSetRenderTargets(1, rtvs, nullptr);
    P<ID3D11RenderTargetView> got_rtv;
    ctx->OMGetRenderTargets(1, &got_rtv, nullptr);

    D3D11_VIEWPORT vp = {1.0f, 2.0f, 16.0f, 16.0f, 0.0f, 1.0f};
    ctx->RSSetViewports(1, &vp);
    UINT num_vp = 1;
    D3D11_VIEWPORT got_vp = {};
    ctx->RSGetViewports(&num_vp, &got_vp);

    D3D11_RECT sc = {1, 2, 15, 16};
    ctx->RSSetScissorRects(1, &sc);
    UINT num_sc = 1;
    D3D11_RECT got_sc = {};
    ctx->RSGetScissorRects(&num_sc, &got_sc);

    UINT so_off = 0;
    ctx->SOSetTargets(1, vbs, &so_off);
    P<ID3D11Buffer> got_so;
    ctx->SOGetTargets(1, &got_so);

    // MinLOD on texture vs buffer
    ctx->SetResourceMinLOD(tex, 1.0f);
    float lod_tex = ctx->GetResourceMinLOD(tex);
    ctx->SetResourceMinLOD(vb, 1.0f);
    float lod_buf = ctx->GetResourceMinLOD(vb);

    Out("d11ctx bind_roundtrip vb=%d/%u/%u ib=%d/%u/%u topo=%d cb1=%d/%u/%u srv=%d samp=%d rtv=%d vp=%.0f,%.0f sc=%ld,%ld so=%d lod=%.1f,%.1f\n",
        (got_vb.p == vb.p) ? 1 : 0, got_stride, got_offset,
        (got_ib.p == ib.p) ? 1 : 0, (unsigned)got_ib_fmt, got_ib_off,
        (int)got_topo, (got_cb.p == cb.p) ? 1 : 0, got_fc, got_nc,
        (got_srv.p == srv.p) ? 1 : 0, (got_samp.p == samp.p) ? 1 : 0,
        (got_rtv.p == rtv.p) ? 1 : 0, got_vp.Width, got_vp.Height,
        (long)got_sc.right, (long)got_sc.bottom, (got_so.p == vb.p) ? 1 : 0,
        lod_tex, lod_buf);

    // ClearState verification
    ctx->ClearState();
    D3D11_PRIMITIVE_TOPOLOGY clr_topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ctx->IAGetPrimitiveTopology(&clr_topo);
    P<ID3D11BlendState> clr_bs;
    FLOAT clr_bf[4] = {};
    UINT clr_sm = 0;
    ctx->OMGetBlendState(&clr_bs, clr_bf, &clr_sm);
    P<ID3D11DepthStencilState> clr_ds;
    UINT clr_sref = 99;
    ctx->OMGetDepthStencilState(&clr_ds, &clr_sref);
    UINT clr_nvp = 0;
    ctx->RSGetViewports(&clr_nvp, nullptr);
    Out("d11ctx clear_state topo=%d bs=%d bf=%.0f,%.0f,%.0f,%.0f sm=%08x ds=%d sref=%u nvp=%u\n",
        (int)clr_topo, clr_bs ? 1 : 0, clr_bf[0], clr_bf[1], clr_bf[2], clr_bf[3],
        clr_sm, clr_ds ? 1 : 0, clr_sref, clr_nvp);
    ops += 10;
  }

  // 3. SwapDeviceContextState
  {
    P<ID3D11Device1> d1;
    dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&d1));
    if (d1 && ctx1) {
      D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
      P<ID3DDeviceContextState> dcs, prev;
      d1->CreateDeviceContextState(0, &fl, 1, D3D11_SDK_VERSION, __uuidof(ID3D11DeviceContext), nullptr, &dcs);
      ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
      ctx1->SwapDeviceContextState(dcs, &prev);
      D3D11_PRIMITIVE_TOPOLOGY topo_after_swap = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
      ctx->IAGetPrimitiveTopology(&topo_after_swap);
      ctx1->SwapDeviceContextState(prev, nullptr);
      D3D11_PRIMITIVE_TOPOLOGY topo_restored = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
      ctx->IAGetPrimitiveTopology(&topo_restored);
      Out("d11ctx swap_dcs prev=%d topo_swapped=%d topo_restored=%d\n",
          prev ? 1 : 0, (int)topo_after_swap, (int)topo_restored);
    }
  }

  // 4. Map flags x usages + DO_NOT_WAIT on busy staging buffer
  {
    D3D11_BUFFER_DESC dyn_bd = {64, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    P<ID3D11Buffer> dyn_buf;
    dev->CreateBuffer(&dyn_bd, nullptr, &dyn_buf);

    D3D11_BUFFER_DESC def_bd = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    uint32_t init_words[16] = {0x11223344u, 0x55667788u};
    D3D11_SUBRESOURCE_DATA srd = {init_words, 64, 64};
    P<ID3D11Buffer> def_buf;
    dev->CreateBuffer(&def_bd, &srd, &def_buf);

    D3D11_BUFFER_DESC stg_bd = {64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0};
    P<ID3D11Buffer> stg_buf;
    dev->CreateBuffer(&stg_bd, nullptr, &stg_buf);

    D3D11_MAPPED_SUBRESOURCE msr = {};
    HRESULT hr_dyn_read = ctx->Map(dyn_buf, 0, D3D11_MAP_READ, 0, &msr);
    HRESULT hr_dyn_write = ctx->Map(dyn_buf, 0, D3D11_MAP_WRITE, 0, &msr);
    HRESULT hr_dyn_disc = ctx->Map(dyn_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr_dyn_disc)) ctx->Unmap(dyn_buf, 0);
    HRESULT hr_dyn_noov = ctx->Map(dyn_buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &msr);
    if (SUCCEEDED(hr_dyn_noov)) ctx->Unmap(dyn_buf, 0);

    HRESULT hr_stg_disc = ctx->Map(stg_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    HRESULT hr_stg_idle = ctx->Map(stg_buf, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &msr);
    if (SUCCEEDED(hr_stg_idle)) ctx->Unmap(stg_buf, 0);

    // Queue CopyResource to staging buffer; before Flush/Wait, staging is in-flight at CurrentSeqId()
    ctx->CopyResource(stg_buf, def_buf);
    HRESULT hr_stg_busy = ctx->Map(stg_buf, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &msr);
    HRESULT hr_stg_wait = ctx->Map(stg_buf, 0, D3D11_MAP_READ, 0, &msr);
    uint32_t read_w0 = 0;
    if (SUCCEEDED(hr_stg_wait) && msr.pData) {
      read_w0 = static_cast<const uint32_t *>(msr.pData)[0];
      ctx->Unmap(stg_buf, 0);
    }

    Out("d11ctx map dyn_r=%08x dyn_w=%08x dyn_disc=%08x dyn_noov=%08x stg_disc=%08x stg_idle=%08x stg_busy=%08x stg_wait=%08x(%08x)\n",
        (unsigned)hr_dyn_read, (unsigned)hr_dyn_write, (unsigned)hr_dyn_disc,
        (unsigned)hr_dyn_noov, (unsigned)hr_stg_disc, (unsigned)hr_stg_idle,
        (unsigned)hr_stg_busy, (unsigned)hr_stg_wait, read_w0);
    ops += 8;
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11ctx %.1f\n", dt > 0 ? ops / dt : 0.0);
}
static void RunD11Defer() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> imm;
  if (!CreateDefaultDevice(&dev, &imm)) return;
  P<ID3D11Device1> d1;
  P<ID3D11Device2> d2;
  P<ID3D11Device3> d3;
  dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&d1));
  dev->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void **>(&d2));
  dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&d3));

  P<ID3D11DeviceContext> def, def_bad;
  HRESULT hr_bad_flags = dev->CreateDeferredContext(1, &def_bad);
  HRESULT hr_def0 = dev->CreateDeferredContext(0, &def);
  P<ID3D11DeviceContext1> def1;
  P<ID3D11DeviceContext2> def2;
  P<ID3D11DeviceContext3> def3;
  HRESULT hr_def1 = d1 ? d1->CreateDeferredContext1(0, &def1) : E_FAIL;
  HRESULT hr_def2 = d2 ? d2->CreateDeferredContext2(0, &def2) : E_FAIL;
  HRESULT hr_def3 = d3 ? d3->CreateDeferredContext3(0, &def3) : E_FAIL;
  Out("d11defer create bad_flags=%08x def0=%08x def1=%08x def2=%08x def3=%08x type=%d flags=%08x\n",
      (unsigned)hr_bad_flags, (unsigned)hr_def0, (unsigned)hr_def1,
      (unsigned)hr_def2, (unsigned)hr_def3,
      def ? (int)def->GetType() : -1, def ? def->GetContextFlags() : 0u);

  if (!def) return;

  static const iid_entry kDefIids[] = {
      {"IUnknown", __uuidof(IUnknown)},
      {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
      {"ID3D11DeviceContext", __uuidof(ID3D11DeviceContext)},
      {"ID3D11DeviceContext1", __uuidof(ID3D11DeviceContext1)},
      {"ID3D11DeviceContext2", __uuidof(ID3D11DeviceContext2)},
      {"ID3D11DeviceContext3", __uuidof(ID3D11DeviceContext3)},
      {"ID3D11DeviceContext4", __uuidof(ID3D11DeviceContext4)},
      {"ID3DUserDefinedAnnotation", __uuidof(ID3DUserDefinedAnnotation)},
      {"IMTLD3D11DeviceContext", kIID_IMTLD3D11DeviceContext},
      {"IMTLD3D11ContextExt", kIID_IMTLD3D11ContextExt},
      {"IMTLD3D11ContextExt1", kIID_IMTLD3D11ContextExt1},
      {"IMTLD3D11ContextExt2", kIID_IMTLD3D11ContextExt2},
      {"Bogus", kIID_Bogus},
  };
  ProbeQiList("d11defer", "def", def, kDefIids, sizeof(kDefIids) / sizeof(kDefIids[0]));
  ProbeChildGetDevice("d11defer", "def", def, dev);
  ProbePrivateData("d11defer", "def", def.p);

  // FinishCommandList on immediate context -> DXGI_ERROR_INVALID_CALL
  P<ID3D11CommandList> cl_imm;
  HRESULT hr_fin_imm = imm->FinishCommandList(FALSE, &cl_imm);

  // Deferred state preservation on FinishCommandList(FALSE) vs FinishCommandList(TRUE)
  def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  P<ID3D11CommandList> cl1, cl2;
  HRESULT hr_fin_false = def->FinishCommandList(FALSE, &cl1);
  D3D11_PRIMITIVE_TOPOLOGY topo_after_false = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
  def->IAGetPrimitiveTopology(&topo_after_false);

  def->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  HRESULT hr_fin_true = def->FinishCommandList(TRUE, &cl2);
  D3D11_PRIMITIVE_TOPOLOGY topo_after_true = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  def->IAGetPrimitiveTopology(&topo_after_true);

  // FinishCommandList twice in a row (empty second list)
  P<ID3D11CommandList> cl3;
  HRESULT hr_fin_twice = def->FinishCommandList(FALSE, &cl3);

  Out("d11defer finish imm=%08x fin_false=%08x(topo=%d) fin_true=%08x(topo=%d) fin_twice=%08x\n",
      (unsigned)hr_fin_imm, (unsigned)hr_fin_false, (int)topo_after_false,
      (unsigned)hr_fin_true, (unsigned)topo_after_true, (unsigned)hr_fin_twice);

  if (cl1) {
    static const iid_entry kClIids[] = {
        {"IUnknown", __uuidof(IUnknown)},
        {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
        {"ID3D11CommandList", __uuidof(ID3D11CommandList)},
        {"Bogus", kIID_Bogus},
    };
    ProbeQiList("d11defer", "cmdlist", cl1, kClIids, 4);
    ProbeChildGetDevice("d11defer", "cmdlist", cl1, dev);
    ProbePrivateData("d11defer", "cmdlist", cl1.p);
    Out("d11defer cmdlist_flags flags=%08x\n", cl1->GetContextFlags());
  }

  // ExecuteCommandList with RestoreContextState FALSE vs TRUE + recorded RTV clear
  {
    D3D11_TEXTURE2D_DESC td = {
        4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    P<ID3D11Texture2D> rt;
    dev->CreateTexture2D(&td, nullptr, &rt);
    P<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(rt, nullptr, &rtv);
    D3D11_TEXTURE2D_DESC stgd = {
        4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
    P<ID3D11Texture2D> stg;
    dev->CreateTexture2D(&stgd, nullptr, &stg);

    const FLOAT clear_col[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    def->ClearRenderTargetView(rtv, clear_col);
    P<ID3D11CommandList> cl_clear;
    def->FinishCommandList(FALSE, &cl_clear);

    imm->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    imm->ExecuteCommandList(cl_clear, TRUE);
    D3D11_PRIMITIVE_TOPOLOGY topo_rest1 = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    imm->IAGetPrimitiveTopology(&topo_rest1);

    imm->ExecuteCommandList(cl_clear, FALSE);
    D3D11_PRIMITIVE_TOPOLOGY topo_rest0 = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    imm->IAGetPrimitiveTopology(&topo_rest0);

    imm->CopyResource(stg, rt);
    D3D11_MAPPED_SUBRESOURCE msr = {};
    uint32_t px = 0;
    if (SUCCEEDED(imm->Map(stg, 0, D3D11_MAP_READ, 0, &msr))) {
      px = *static_cast<const uint32_t *>(msr.pData);
      imm->Unmap(stg, 0);
    }
    Out("d11defer exec topo_restore1=%d topo_restore0=%d clear_px=%08x\n",
        (int)topo_rest1, (int)topo_rest0, px);
  }

  // Deferred Map rules
  {
    D3D11_BUFFER_DESC dyn_bd = {64, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    P<ID3D11Buffer> dyn_buf;
    dev->CreateBuffer(&dyn_bd, nullptr, &dyn_buf);
    D3D11_BUFFER_DESC stg_bd = {64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0};
    P<ID3D11Buffer> stg_buf;
    dev->CreateBuffer(&stg_bd, nullptr, &stg_buf);

    D3D11_MAPPED_SUBRESOURCE msr = {};
    HRESULT hr_def_disc = def->Map(dyn_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
    if (SUCCEEDED(hr_def_disc)) def->Unmap(dyn_buf, 0);
    HRESULT hr_def_noov = def->Map(dyn_buf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &msr);
    if (SUCCEEDED(hr_def_noov)) def->Unmap(dyn_buf, 0);
    HRESULT hr_def_stg = def->Map(stg_buf, 0, D3D11_MAP_READ, 0, &msr);
    P<ID3D11CommandList> cl_discard;
    def->FinishCommandList(FALSE, &cl_discard);
    Out("d11defer map dyn_disc=%08x dyn_noov=%08x stg_read=%08x\n",
        (unsigned)hr_def_disc, (unsigned)hr_def_noov, (unsigned)hr_def_stg);
  }
  ops += 12;

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11defer %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11Query() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;

  const D3D11_QUERY qtypes[] = {
      D3D11_QUERY_EVENT,
      D3D11_QUERY_OCCLUSION,
      D3D11_QUERY_TIMESTAMP,
      D3D11_QUERY_TIMESTAMP_DISJOINT,
      D3D11_QUERY_PIPELINE_STATISTICS,
      D3D11_QUERY_OCCLUSION_PREDICATE,
      D3D11_QUERY_SO_STATISTICS,
      D3D11_QUERY_SO_OVERFLOW_PREDICATE,
      D3D11_QUERY_SO_STATISTICS_STREAM0,
      D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0,
      D3D11_QUERY_SO_STATISTICS_STREAM1,
      D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1,
      D3D11_QUERY_SO_STATISTICS_STREAM2,
      D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2,
      D3D11_QUERY_SO_STATISTICS_STREAM3,
      D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3,
      static_cast<D3D11_QUERY>(99),
  };

  for (D3D11_QUERY qt : qtypes) {
    D3D11_QUERY_DESC qd = {qt, 0};
    P<ID3D11Query> q;
    HRESULT hr = dev->CreateQuery(&qd, &q);
    UINT dsz = q ? q->GetDataSize() : 0;
    D3D11_QUERY_DESC got = {static_cast<D3D11_QUERY>(0xff), 0xff};
    if (q) q->GetDesc(&got);

    // Begin/End/GetData roundtrip
    HRESULT hr_gd_null = E_FAIL;
    HRESULT hr_gd_ok = E_FAIL;
    if (q) {
      if (qt != D3D11_QUERY_EVENT && qt != D3D11_QUERY_TIMESTAMP) {
        ctx->Begin(q);
      }
      ctx->End(q);
      ctx->Flush();
      for (int spin = 0; spin < 200; ++spin) {
        hr_gd_null = ctx->GetData(q, nullptr, 0, 0);
        if (hr_gd_null == S_OK) break;
        Sleep(2);
      }
      uint8_t buf[256] = {};
      for (int spin = 0; spin < 200; ++spin) {
        hr_gd_ok = ctx->GetData(q, buf, dsz, 0);
        if (hr_gd_ok == S_OK) break;
        Sleep(2);
      }
    }
    Out("d11query qt=%d hr=%08x dsz=%u got_qt=%d gd_null=%08x gd_ok=%08x\n",
        (int)qt, (unsigned)hr, dsz, (int)got.Query,
        (unsigned)hr_gd_null, (unsigned)hr_gd_ok);
    ops++;
  }

  // CreatePredicate & CreateCounter
  {
    D3D11_QUERY_DESC pd_occ = {D3D11_QUERY_OCCLUSION_PREDICATE, 0};
    D3D11_QUERY_DESC pd_so = {D3D11_QUERY_SO_OVERFLOW_PREDICATE, 0};
    D3D11_QUERY_DESC pd_bad = {D3D11_QUERY_EVENT, 0};
    P<ID3D11Predicate> p_occ, p_so, p_bad;
    HRESULT hr_p_occ = dev->CreatePredicate(&pd_occ, &p_occ);
    HRESULT hr_p_so = dev->CreatePredicate(&pd_so, &p_so);
    HRESULT hr_p_bad = dev->CreatePredicate(&pd_bad, &p_bad);

    D3D11_COUNTER_DESC cd = {D3D11_COUNTER_DEVICE_DEPENDENT_0, 0};
    P<ID3D11Counter> cnt;
    HRESULT hr_cnt = dev->CreateCounter(&cd, &cnt);
    Out("d11query pred_cnt occ=%08x so=%08x bad=%08x cnt=%08x\n",
        (unsigned)hr_p_occ, (unsigned)hr_p_so, (unsigned)hr_p_bad, (unsigned)hr_cnt);

    if (p_occ) {
      static const iid_entry kPredIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"ID3D11DeviceChild", __uuidof(ID3D11DeviceChild)},
          {"ID3D11Asynchronous", __uuidof(ID3D11Asynchronous)},
          {"ID3D11Query", __uuidof(ID3D11Query)},
          {"ID3D11Predicate", __uuidof(ID3D11Predicate)},
          {"ID3D10Asynchronous", __uuidof(ID3D10Asynchronous)},
          {"ID3D10Query", __uuidof(ID3D10Query)},
          {"ID3D10Predicate", __uuidof(ID3D10Predicate)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11query", "pred", p_occ, kPredIids, sizeof(kPredIids) / sizeof(kPredIids[0]));
      ProbeChildGetDevice("d11query", "pred", p_occ, dev);
      ProbePrivateData("d11query", "pred", p_occ.p);
    }
  }

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11query %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11Ext() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> imm;
  if (!CreateDefaultDevice(&dev, &imm)) return;
  P<ID3D11DeviceContext> def;
  dev->CreateDeferredContext(0, &def);

  P<IMTLD3D11DeviceExtVtbl> dev_ext;
  HRESULT hr_dext = dev->QueryInterface(kIID_IMTLD3D11DeviceExt, reinterpret_cast<void **>(&dev_ext));
  if (dev_ext) {
    dev_ext->SetShaderExtensionSlot(7);
    dev_ext->SetShaderExtensionSlot(0xffffffffu);
  }

  P<IMTLD3D11ContextExt1Vtbl> imm_ext1;
  HRESULT hr_cext1 = imm->QueryInterface(kIID_IMTLD3D11ContextExt1, reinterpret_cast<void **>(&imm_ext1));
  HRESULT hr_mfx_ok = E_FAIL, hr_mfx_badsz = E_FAIL, hr_mfx_badfeat = E_FAIL;
  if (imm_ext1) {
    imm_ext1->BeginUAVOverlap();
    imm_ext1->EndUAVOverlap();
    BOOL sup = FALSE;
    hr_mfx_ok = imm_ext1->CheckFeatureSupport(0, &sup, sizeof(sup));
    hr_mfx_badsz = imm_ext1->CheckFeatureSupport(0, &sup, 1);
    hr_mfx_badfeat = imm_ext1->CheckFeatureSupport(99, &sup, sizeof(sup));
  }

  P<IMTLD3D11ContextExt2Vtbl> imm_ext2, def_ext2;
  HRESULT hr_imm_ext2 = imm->QueryInterface(kIID_IMTLD3D11ContextExt2, reinterpret_cast<void **>(&imm_ext2));
  HRESULT hr_def_ext2 = def ? def->QueryInterface(kIID_IMTLD3D11ContextExt2, reinterpret_cast<void **>(&def_ext2)) : E_FAIL;
  HRESULT hr_def_pieces = E_NOINTERFACE;
  if (def_ext2) {
    UINT cnt = 0;
    hr_def_pieces = def_ext2->GetPieces(nullptr, &cnt);
  }
  Out("d11ext dext=%08x cext1=%08x mfx_ok=%08x mfx_badsz=%08x mfx_badfeat=%08x imm_ext2=%08x def_ext2=%08x def_pieces=%08x\n",
      (unsigned)hr_dext, (unsigned)hr_cext1, (unsigned)hr_mfx_ok,
      (unsigned)hr_mfx_badsz, (unsigned)hr_mfx_badfeat,
      (unsigned)hr_imm_ext2, (unsigned)hr_def_ext2, (unsigned)hr_def_pieces);
  ops += 6;

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11ext %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD11Sc() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;

  P<IDXGIDevice> dxgi_dev;
  dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_dev));
  P<IDXGIAdapter> adp;
  if (dxgi_dev) dxgi_dev->GetAdapter(&adp);
  P<IDXGIFactory2> fac2;
  if (adp) adp->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&fac2));
  if (!fac2) return;

  {
    DXGI_SWAP_CHAIN_DESC1 scd_val = {};
    scd_val.Width = 32;
    scd_val.Height = 32;
    scd_val.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd_val.SampleDesc = {1, 0};
    scd_val.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd_val.BufferCount = 2;
    scd_val.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    P<IDXGISwapChain1> sc_val;
    HRESULT hr_null_desc = fac2->CreateSwapChainForHwnd(dev, nullptr, nullptr, nullptr, nullptr, &sc_val);
    HRESULT hr_null_out = fac2->CreateSwapChainForHwnd(dev, nullptr, &scd_val, nullptr, nullptr, nullptr);
    Out("d11sc validate null_desc=%08x null_out=%08x\n",
        (unsigned)hr_null_desc, (unsigned)hr_null_out);
    ops += 2;
  }

#ifdef _WIN32
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = "D3D11ProbeWnd";
  RegisterClassA(&wc);

  // 1. Hidden window swapchain lifecycle
  HWND hwnd_hidden = CreateWindowExA(
      0, "D3D11ProbeWnd", "Hidden", WS_OVERLAPPEDWINDOW,
      0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

  {
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = 32;
    scd.Height = 32;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc = {1, 0};
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    P<IDXGISwapChain1> sc1;
    HRESULT hr_create = fac2->CreateSwapChainForHwnd(dev, hwnd_hidden, &scd, nullptr, nullptr, &sc1);
    if (sc1) {
      static const iid_entry kScIids[] = {
          {"IUnknown", __uuidof(IUnknown)},
          {"IDXGIObject", __uuidof(IDXGIObject)},
          {"IDXGIDeviceSubObject", __uuidof(IDXGIDeviceSubObject)},
          {"IDXGISwapChain", __uuidof(IDXGISwapChain)},
          {"IDXGISwapChain1", __uuidof(IDXGISwapChain1)},
          {"IDXGISwapChain2", __uuidof(IDXGISwapChain2)},
          {"IDXGISwapChain3", __uuidof(IDXGISwapChain3)},
          {"IDXGISwapChain4", __uuidof(IDXGISwapChain4)},
          {"Bogus", kIID_Bogus},
      };
      ProbeQiList("d11sc", "sc", sc1, kScIids, sizeof(kScIids) / sizeof(kScIids[0]));

      P<ID3D11Texture2D> bb_tex;
      P<IDXGISurface> bb_surf;
      HRESULT hr_bb_tex = sc1->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&bb_tex));
      HRESULT hr_bb_surf = sc1->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void **>(&bb_surf));
      P<IUnknown> u1, u2;
      if (bb_tex) bb_tex->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u1));
      if (bb_surf) bb_surf->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u2));
      int same_bb = (u1.p == u2.p && u1.p != nullptr) ? 1 : 0;

      P<IDXGISwapChain2> sc2;
      sc1->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void **>(&sc2));
      HANDLE h_wait = sc2 ? sc2->GetFrameLatencyWaitableObject() : nullptr;
      DWORD w_init = h_wait ? WaitForSingleObject(h_wait, 500) : WAIT_FAILED;
      if (h_wait) CloseHandle(h_wait);

      bb_tex.reset();
      bb_surf.reset();
      u1.reset();
      u2.reset();

      HRESULT hr_resize = sc1->ResizeBuffers(
          2, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM,
          DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
      DXGI_SWAP_CHAIN_DESC1 got_d1 = {};
      sc1->GetDesc1(&got_d1);

      HRESULT hr_ptest = sc1->Present(0, DXGI_PRESENT_TEST);
      HRESULT hr_p0 = sc1->Present(0, 0);
      HRESULT hr_p_bad_sync = sc1->Present(5, 0);

      Out("d11sc hidden create=%08x bb_tex=%08x bb_surf=%08x same_bb=%d wait=%d resize=%08x(%ux%u) ptest=%08x p0=%08x p5=%08x\n",
          (unsigned)hr_create, (unsigned)hr_bb_tex, (unsigned)hr_bb_surf, same_bb,
          (w_init == WAIT_OBJECT_0 ? 1 : 0), (unsigned)hr_resize,
          got_d1.Width, got_d1.Height, (unsigned)hr_ptest, (unsigned)hr_p0,
          (unsigned)hr_p_bad_sync);
    }
    ops += 6;
  }

  // 2. Iconic (minimized) window occlusion probes (Step 3 D7 regression coverage)
  HWND hwnd_min = CreateWindowExA(
      0, "D3D11ProbeWnd", "Min", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(hwnd_min, SW_MINIMIZE);
  BOOL iconic = IsIconic(hwnd_min);
  {
    DXGI_SWAP_CHAIN_DESC1 scd_disc = {};
    scd_disc.Width = 32;
    scd_disc.Height = 32;
    scd_disc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd_disc.SampleDesc = {1, 0};
    scd_disc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd_disc.BufferCount = 1;
    scd_disc.Scaling = DXGI_SCALING_STRETCH;
    scd_disc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scd_disc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    P<IDXGISwapChain1> sc_disc;
    fac2->CreateSwapChainForHwnd(dev, hwnd_min, &scd_disc, nullptr, nullptr, &sc_disc);
    auto tp0 = std::chrono::steady_clock::now();
    HRESULT hr_disc_p0 = sc_disc ? sc_disc->Present(0, 0) : E_FAIL;
    auto tp1 = std::chrono::steady_clock::now();
    int fast = (std::chrono::duration_cast<std::chrono::milliseconds>(tp1 - tp0).count() < 100) ? 1 : 0;
    HRESULT hr_disc_test = sc_disc ? sc_disc->Present(0, DXGI_PRESENT_TEST) : E_FAIL;
    HRESULT hr_disc_p5 = sc_disc ? sc_disc->Present(5, 0) : E_FAIL;

    DXGI_SWAP_CHAIN_DESC1 scd_flip = scd_disc;
    scd_flip.BufferCount = 2;
    scd_flip.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    P<IDXGISwapChain1> sc_flip;
    fac2->CreateSwapChainForHwnd(dev, hwnd_min, &scd_flip, nullptr, nullptr, &sc_flip);
    HRESULT hr_flip_p0 = sc_flip ? sc_flip->Present(0, 0) : E_FAIL;
    HRESULT hr_flip_p5 = sc_flip ? sc_flip->Present(5, 0) : E_FAIL;

    // 0x0 ResizeBuffers then Present on a bitblt chain
    HWND hwnd_zero = CreateWindowExA(
        0, "D3D11ProbeWnd", "Zero", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    P<IDXGISwapChain1> sc_zero;
    fac2->CreateSwapChainForHwnd(dev, hwnd_zero, &scd_disc, nullptr, nullptr, &sc_zero);
    HRESULT hr_zero_rsz = sc_zero ? sc_zero->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0) : E_FAIL;
    DXGI_SWAP_CHAIN_DESC1 zero_d1 = {};
    if (sc_zero) sc_zero->GetDesc1(&zero_d1);
    HRESULT hr_zero_p0 = sc_zero ? sc_zero->Present(0, 0) : E_FAIL;
    HRESULT hr_zero_test = sc_zero ? sc_zero->Present(0, DXGI_PRESENT_TEST) : E_FAIL;
    sc_zero.reset();
    DestroyWindow(hwnd_zero);

    Out("d11sc iconic=%d disc_p0=%08x(fast=%d) disc_test=%08x disc_p5=%08x flip_p0=%08x flip_p5=%08x zero_rsz=%08x(%ux%u) zero_p0=%08x zero_test=%08x\n",
        (int)iconic, (unsigned)hr_disc_p0, fast, (unsigned)hr_disc_test, (unsigned)hr_disc_p5,
        (unsigned)hr_flip_p0, (unsigned)hr_flip_p5,
        (unsigned)hr_zero_rsz, zero_d1.Width, zero_d1.Height, (unsigned)hr_zero_p0, (unsigned)hr_zero_test);
    ops += 8;
  }

  DestroyWindow(hwnd_min);
  DestroyWindow(hwnd_hidden);
#endif

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11sc %.1f\n", dt > 0 ? ops / dt : 0.0);
}
static void FlushAndWaitIdle(ID3D11Device *dev, ID3D11DeviceContext *ctx) {
  D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0};
  P<ID3D11Query> q;
  if (SUCCEEDED(dev->CreateQuery(&qd, &q))) {
    ctx->End(q);
    ctx->Flush();
    BOOL done = FALSE;
    for (int i = 0; i < 500; ++i) {
      if (ctx->GetData(q, &done, sizeof(done), 0) == S_OK && done) break;
      Sleep(2);
    }
  } else {
    ctx->Flush();
  }
}

static uint32_t ReadbackTex2DPixel0(ID3D11Device *dev, ID3D11DeviceContext *ctx, ID3D11Texture2D *src) {
  D3D11_TEXTURE2D_DESC stgd = {
      4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
  P<ID3D11Texture2D> stg;
  if (FAILED(dev->CreateTexture2D(&stgd, nullptr, &stg))) return 0;
  ctx->CopyResource(stg, src);
  D3D11_MAPPED_SUBRESOURCE msr = {};
  uint32_t px = 0;
  if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &msr))) {
    px = *static_cast<const uint32_t *>(msr.pData);
    ctx->Unmap(stg, 0);
  }
  return px;
}

static int ChildShareMain(const char *h_sh_str, const char *h_km_str) {
  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return 1;

  uintptr_t h_sh_val = std::strtoull(h_sh_str, nullptr, 16);
  uintptr_t h_km_val = std::strtoull(h_km_str, nullptr, 16);
  HANDLE h_sh = reinterpret_cast<HANDLE>(h_sh_val);
  HANDLE h_km = reinterpret_cast<HANDLE>(h_km_val);

  P<ID3D11Texture2D> tex_sh;
  HRESULT hr_open_sh = dev->OpenSharedResource(h_sh, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex_sh));
  if (tex_sh) {
    P<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(tex_sh, nullptr, &rtv);
    const FLOAT red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    if (rtv) ctx->ClearRenderTargetView(rtv, red);
    FlushAndWaitIdle(dev, ctx);
  }

  P<ID3D11Texture2D> tex_km;
  HRESULT hr_open_km = dev->OpenSharedResource(h_km, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex_km));
  HRESULT hr_acq = E_FAIL, hr_rel = E_FAIL;
  if (tex_km) {
    P<IDXGIKeyedMutex> km;
    tex_km->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&km));
    if (km) {
      hr_acq = km->AcquireSync(0, 5000);
      P<ID3D11RenderTargetView> rtv;
      dev->CreateRenderTargetView(tex_km, nullptr, &rtv);
      const FLOAT blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
      if (rtv) ctx->ClearRenderTargetView(rtv, blue);
      FlushAndWaitIdle(dev, ctx);
      hr_rel = km->ReleaseSync(1);
    }
  }
  Out("d11share child open_sh=%08x open_km=%08x acq0=%08x rel1=%08x\n",
      (unsigned)hr_open_sh, (unsigned)hr_open_km, (unsigned)hr_acq, (unsigned)hr_rel);
  return 0;
}

static void RunD11Share() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> ctx;
  if (!CreateDefaultDevice(&dev, &ctx)) return;
  P<ID3D11Device1> dev1;
  dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&dev1));

  // 1. Null / invalid handle checks
  {
    P<ID3D11Texture2D> bad1, bad2, bad3;
    HRESULT hr_null0 = dev->OpenSharedResource(nullptr, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&bad1));
    HRESULT hr_null1 = dev1 ? dev1->OpenSharedResource1(nullptr, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&bad2)) : E_FAIL;
    HRESULT hr_name = dev1 ? dev1->OpenSharedResourceByName(L"NonExistent", GENERIC_ALL, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&bad3)) : E_FAIL;
    Out("d11share invalid open0_null=%08x open1_null=%08x open_name=%08x\n",
        (unsigned)hr_null0, (unsigned)hr_null1, (unsigned)hr_name);
    ops += 3;
  }

  // 2. Create MISC_SHARED and MISC_SHARED_KEYEDMUTEX textures
  D3D11_TEXTURE2D_DESC td_sh = {
      4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
      D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
      0, D3D11_RESOURCE_MISC_SHARED};
  P<ID3D11Texture2D> tex_sh;
  HRESULT hr_create_sh = dev->CreateTexture2D(&td_sh, nullptr, &tex_sh);

  D3D11_TEXTURE2D_DESC td_km = td_sh;
  td_km.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  P<ID3D11Texture2D> tex_km;
  HRESULT hr_create_km = dev->CreateTexture2D(&td_km, nullptr, &tex_km);

  HANDLE h_sh = nullptr, h_km = nullptr;
  if (tex_sh) {
    P<IDXGIResource> res;
    tex_sh->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&res));
    if (res) res->GetSharedHandle(&h_sh);
  }
  P<IDXGIKeyedMutex> km;
  if (tex_km) {
    P<IDXGIResource> res;
    tex_km->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&res));
    if (res) res->GetSharedHandle(&h_km);
    tex_km->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&km));
  }

  if (km) {
    static const iid_entry kKmIids[] = {
        {"IUnknown", __uuidof(IUnknown)},
        {"IDXGIObject", __uuidof(IDXGIObject)},
        {"IDXGIDeviceSubObject", __uuidof(IDXGIDeviceSubObject)},
        {"IDXGIKeyedMutex", __uuidof(IDXGIKeyedMutex)},
        {"ID3D11Texture2D", __uuidof(ID3D11Texture2D)},
        {"Bogus", kIID_Bogus},
    };
    ProbeQiList("d11share", "keyed_mutex", km, kKmIids, sizeof(kKmIids) / sizeof(kKmIids[0]));
  }

  // Initialize both textures before child opens them
  if (tex_sh) {
    P<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(tex_sh, nullptr, &rtv);
    const FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    if (rtv) ctx->ClearRenderTargetView(rtv, green);
    FlushAndWaitIdle(dev, ctx);
  }
  HRESULT hr_parent_acq0 = E_FAIL, hr_parent_rel0 = E_FAIL;
  if (km) {
    hr_parent_acq0 = km->AcquireSync(0, 5000);
    P<ID3D11RenderTargetView> rtv;
    dev->CreateRenderTargetView(tex_km, nullptr, &rtv);
    const FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    if (rtv) ctx->ClearRenderTargetView(rtv, green);
    FlushAndWaitIdle(dev, ctx);
    hr_parent_rel0 = km->ReleaseSync(0);
  }

  Out("d11share parent_init create_sh=%08x(h=%d) create_km=%08x(h=%d) acq0=%08x rel0=%08x\n",
      (unsigned)hr_create_sh, h_sh ? 1 : 0, (unsigned)hr_create_km, h_km ? 1 : 0,
      (unsigned)hr_parent_acq0, (unsigned)hr_parent_rel0);

  if (h_sh && h_km) {
    char args[512];
    std::snprintf(args, sizeof(args), "--child-share \"%s\" %llx %llx",
                  g_shader_dir.c_str(),
                  (unsigned long long)(uintptr_t)h_sh,
                  (unsigned long long)(uintptr_t)h_km);
    RunChildCmd(args, nullptr, nullptr);

    uint32_t px_sh = ReadbackTex2DPixel0(dev, ctx, tex_sh);
    HRESULT hr_acq1 = km ? km->AcquireSync(1, 5000) : E_FAIL;
    uint32_t px_km = ReadbackTex2DPixel0(dev, ctx, tex_km);
    HRESULT hr_rel0 = km ? km->ReleaseSync(0) : E_FAIL;
    Out("d11share parent_verify px_sh=%08x acq1=%08x px_km=%08x rel0=%08x\n",
        px_sh, (unsigned)hr_acq1, px_km, (unsigned)hr_rel0);

#if defined(_WIN32) && defined(__x86_64__)
    // Also test 64-bit parent <-> 32-bit child if d3d11_probe_i386.exe is alongside self
    char self[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string i386_exe(self);
    auto pos = i386_exe.rfind("d3d11_probe.exe");
    if (pos != std::string::npos) {
      i386_exe.replace(pos, std::strlen("d3d11_probe.exe"), "d3d11_probe_i386.exe");
      DWORD attr = GetFileAttributesA(i386_exe.c_str());
      if (attr != INVALID_FILE_ATTRIBUTES) {
        // Reset textures to green before 32-bit child runs
        P<ID3D11RenderTargetView> rtv_s, rtv_k;
        dev->CreateRenderTargetView(tex_sh, nullptr, &rtv_s);
        dev->CreateRenderTargetView(tex_km, nullptr, &rtv_k);
        const FLOAT green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        if (rtv_s) ctx->ClearRenderTargetView(rtv_s, green);
        if (km) km->AcquireSync(0, 5000);
        if (rtv_k) ctx->ClearRenderTargetView(rtv_k, green);
        FlushAndWaitIdle(dev, ctx);
        if (km) km->ReleaseSync(0);

        std::string out32 = SpawnCaptureWin32(i386_exe.c_str(), args, nullptr, nullptr);
        size_t pos32 = 0;
        while (pos32 < out32.size()) {
          size_t nl = out32.find('\n', pos32);
          std::string line = (nl == std::string::npos) ? out32.substr(pos32) : out32.substr(pos32, nl - pos32 + 1);
          if (line.compare(0, 9, "d11share ") == 0) {
            Out("d11share i386_%s", line.c_str() + 9);
          } else {
            Out("%s", line.c_str());
          }
          if (nl == std::string::npos) break;
          pos32 = nl + 1;
        }
        uint32_t px_sh32 = ReadbackTex2DPixel0(dev, ctx, tex_sh);
        HRESULT hr_acq32 = km ? km->AcquireSync(1, 5000) : E_FAIL;
        uint32_t px_km32 = ReadbackTex2DPixel0(dev, ctx, tex_km);
        HRESULT hr_rel32 = km ? km->ReleaseSync(0) : E_FAIL;
        Out("d11share parent_verify_i386 px_sh=%08x acq1=%08x px_km=%08x rel0=%08x\n",
            px_sh32, (unsigned)hr_acq32, px_km32, (unsigned)hr_rel32);
      }
    }
#endif
  }
  ops += 8;

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d11share %.1f\n", dt > 0 ? ops / dt : 0.0);
}

static void RunD10() {
  auto t0 = std::chrono::steady_clock::now();
  unsigned ops = 0;

  P<IDXGIFactory1> fac;
  CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&fac));
  P<IDXGIAdapter1> adp;
  if (fac) fac->EnumAdapters1(0, &adp);
  if (!fac || !adp) return;

  P<ID3D10Device> d10;
#ifdef __APPLE__
  auto pfn_core = reinterpret_cast<PFN_D3D10CoreCreateDevice>(
      dlsym(RTLD_DEFAULT, "D3D10CoreCreateDevice"));
#else
  HMODULE h_d3d10c = GetModuleHandleA("d3d10core.dll");
  if (!h_d3d10c) h_d3d10c = LoadLibraryA("d3d10core.dll");
  auto pfn_core = h_d3d10c
      ? reinterpret_cast<PFN_D3D10CoreCreateDevice>(
            GetProcAddress(h_d3d10c, "D3D10CoreCreateDevice"))
      : nullptr;
#endif
  HRESULT hr_core = pfn_core
      ? pfn_core(fac, adp, 0, D3D_FEATURE_LEVEL_10_0, &d10)
      : E_NOTIMPL;
  Out("d10 core_create hr=%08x dev=%d\n", (unsigned)hr_core, d10 ? 1 : 0);
  if (!d10) return;

  static const iid_entry kD10Iids[] = {
      {"IUnknown", __uuidof(IUnknown)},
      {"ID3D10Device", __uuidof(ID3D10Device)},
      {"ID3D10Device1", __uuidof(ID3D10Device1)},
      {"ID3D10Multithread", __uuidof(ID3D10Multithread)},
      {"ID3D11Device", __uuidof(ID3D11Device)},
      {"IDXGIDevice", __uuidof(IDXGIDevice)},
      {"Bogus", kIID_Bogus},
  };
  ProbeQiList("d10", "device", d10, kD10Iids, sizeof(kD10Iids) / sizeof(kD10Iids[0]));

  P<ID3D10Device1> d10_1;
  d10->QueryInterface(__uuidof(ID3D10Device1), reinterpret_cast<void **>(&d10_1));
  D3D10_FEATURE_LEVEL1 fl1 = d10_1 ? d10_1->GetFeatureLevel() : (D3D10_FEATURE_LEVEL1)0;
  UINT flags = d10->GetCreationFlags();
  HRESULT hr_rem = d10->GetDeviceRemovedReason();
  Out("d10 meta fl1=%04x flags=%08x rem=%08x\n", (unsigned)fl1, flags, (unsigned)hr_rem);

  // Create Buffer, Texture1D/2D/3D, SRV/SRV1, RTV, DSV, BlendState/1, DepthStencilState, RasterizerState, SamplerState, Query
  D3D10_BUFFER_DESC bd = {64, D3D10_USAGE_DYNAMIC, D3D10_BIND_VERTEX_BUFFER | D3D10_BIND_SHADER_RESOURCE, D3D10_CPU_ACCESS_WRITE, D3D10_RESOURCE_MISC_GENERATE_MIPS};
  P<ID3D10Buffer> buf10;
  HRESULT hr_buf = d10->CreateBuffer(&bd, nullptr, &buf10);
  D3D10_BUFFER_DESC got_bd10 = {};
  if (buf10) buf10->GetDesc(&got_bd10);
  P<ID3D11Buffer> buf11;
  if (buf10) buf10->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void **>(&buf11));
  D3D11_BUFFER_DESC got_bd11 = {};
  if (buf11) buf11->GetDesc(&got_bd11);
  Out("d10 desc_buf hr=%08x d10=%u,%d,%x,%x,%x d11=%u,%d,%x,%x,%x,%u\n",
      (unsigned)hr_buf,
      got_bd10.ByteWidth, (int)got_bd10.Usage, got_bd10.BindFlags, got_bd10.CPUAccessFlags, got_bd10.MiscFlags,
      got_bd11.ByteWidth, (int)got_bd11.Usage, got_bd11.BindFlags, got_bd11.CPUAccessFlags, got_bd11.MiscFlags, got_bd11.StructureByteStride);

  D3D10_TEXTURE1D_DESC t1d = {32, 0, 2, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_USAGE_DEFAULT, D3D10_BIND_SHADER_RESOURCE, 0, 0};
  P<ID3D10Texture1D> tex1d10;
  HRESULT hr_t1d = d10->CreateTexture1D(&t1d, nullptr, &tex1d10);
  D3D10_TEXTURE1D_DESC got_t1d10 = {};
  if (tex1d10) tex1d10->GetDesc(&got_t1d10);
  P<ID3D11Texture1D> tex1d11;
  if (tex1d10) tex1d10->QueryInterface(__uuidof(ID3D11Texture1D), reinterpret_cast<void **>(&tex1d11));
  D3D11_TEXTURE1D_DESC got_t1d11 = {};
  if (tex1d11) tex1d11->GetDesc(&got_t1d11);
  Out("d10 desc_tex1d hr=%08x d10=%u,%u,%u,%u,%d,%x d11=%u,%u,%u,%u,%d,%x\n",
      (unsigned)hr_t1d,
      got_t1d10.Width, got_t1d10.MipLevels, got_t1d10.ArraySize, (unsigned)got_t1d10.Format, (int)got_t1d10.Usage, got_t1d10.BindFlags,
      got_t1d11.Width, got_t1d11.MipLevels, got_t1d11.ArraySize, (unsigned)got_t1d11.Format, (int)got_t1d11.Usage, got_t1d11.BindFlags);

  D3D10_TEXTURE2D_DESC td = {
      16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
      D3D10_USAGE_DEFAULT, D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET, 0, 0};
  P<ID3D10Texture2D> tex10;
  HRESULT hr_tex = d10->CreateTexture2D(&td, nullptr, &tex10);
  D3D10_TEXTURE2D_DESC got_t2d10 = {};
  if (tex10) tex10->GetDesc(&got_t2d10);
  P<ID3D11Texture2D> tex11;
  if (tex10) tex10->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex11));
  D3D11_TEXTURE2D_DESC got_t2d11 = {};
  if (tex11) tex11->GetDesc(&got_t2d11);
  Out("d10 desc_tex2d hr=%08x d10=%u,%u,%u,%u,%u,%d,%x d11=%u,%u,%u,%u,%u,%d,%x\n",
      (unsigned)hr_tex,
      got_t2d10.Width, got_t2d10.Height, got_t2d10.MipLevels, got_t2d10.ArraySize, (unsigned)got_t2d10.Format, (int)got_t2d10.Usage, got_t2d10.BindFlags,
      got_t2d11.Width, got_t2d11.Height, got_t2d11.MipLevels, got_t2d11.ArraySize, (unsigned)got_t2d11.Format, (int)got_t2d11.Usage, got_t2d11.BindFlags);

  D3D10_TEXTURE3D_DESC t3d = {8, 8, 4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_USAGE_DEFAULT, D3D10_BIND_SHADER_RESOURCE, 0, 0};
  P<ID3D10Texture3D> tex3d10;
  HRESULT hr_t3d = d10->CreateTexture3D(&t3d, nullptr, &tex3d10);
  D3D10_TEXTURE3D_DESC got_t3d10 = {};
  if (tex3d10) tex3d10->GetDesc(&got_t3d10);
  P<ID3D11Texture3D> tex3d11;
  if (tex3d10) tex3d10->QueryInterface(__uuidof(ID3D11Texture3D), reinterpret_cast<void **>(&tex3d11));
  D3D11_TEXTURE3D_DESC got_t3d11 = {};
  if (tex3d11) tex3d11->GetDesc(&got_t3d11);
  Out("d10 desc_tex3d hr=%08x d10=%u,%u,%u,%u,%u,%d,%x d11=%u,%u,%u,%u,%u,%d,%x\n",
      (unsigned)hr_t3d,
      got_t3d10.Width, got_t3d10.Height, got_t3d10.Depth, got_t3d10.MipLevels, (unsigned)got_t3d10.Format, (int)got_t3d10.Usage, got_t3d10.BindFlags,
      got_t3d11.Width, got_t3d11.Height, got_t3d11.Depth, got_t3d11.MipLevels, (unsigned)got_t3d11.Format, (int)got_t3d11.Usage, got_t3d11.BindFlags);

  P<ID3D10ShaderResourceView> srv10;
  HRESULT hr_srv = d10->CreateShaderResourceView(tex10, nullptr, &srv10);
  D3D10_SHADER_RESOURCE_VIEW_DESC got_srv10 = {};
  if (srv10) srv10->GetDesc(&got_srv10);
  P<ID3D11ShaderResourceView> srv11;
  if (srv10) srv10->QueryInterface(__uuidof(ID3D11ShaderResourceView), reinterpret_cast<void **>(&srv11));
  D3D11_SHADER_RESOURCE_VIEW_DESC got_srv11 = {};
  if (srv11) srv11->GetDesc(&got_srv11);
  Out("d10 desc_srv hr=%08x d10=%u,%d,%u,%u d11=%u,%d,%u,%u\n",
      (unsigned)hr_srv,
      (unsigned)got_srv10.Format, (int)got_srv10.ViewDimension, got_srv10.Texture2D.MostDetailedMip, got_srv10.Texture2D.MipLevels,
      (unsigned)got_srv11.Format, (int)got_srv11.ViewDimension, got_srv11.Texture2D.MostDetailedMip, got_srv11.Texture2D.MipLevels);

  P<ID3D10RenderTargetView> rtv10;
  HRESULT hr_rtv = d10->CreateRenderTargetView(tex10, nullptr, &rtv10);
  D3D10_RENDER_TARGET_VIEW_DESC got_rtv10 = {};
  if (rtv10) rtv10->GetDesc(&got_rtv10);
  P<ID3D11RenderTargetView> rtv11;
  if (rtv10) rtv10->QueryInterface(__uuidof(ID3D11RenderTargetView), reinterpret_cast<void **>(&rtv11));
  D3D11_RENDER_TARGET_VIEW_DESC got_rtv11 = {};
  if (rtv11) rtv11->GetDesc(&got_rtv11);
  Out("d10 desc_rtv hr=%08x d10=%u,%d,%u d11=%u,%d,%u\n",
      (unsigned)hr_rtv,
      (unsigned)got_rtv10.Format, (int)got_rtv10.ViewDimension, got_rtv10.Texture2D.MipSlice,
      (unsigned)got_rtv11.Format, (int)got_rtv11.ViewDimension, got_rtv11.Texture2D.MipSlice);

  D3D10_BLEND_DESC1 bs_desc = {};
  bs_desc.AlphaToCoverageEnable = TRUE;
  bs_desc.IndependentBlendEnable = FALSE;
  bs_desc.RenderTarget[0].BlendEnable = TRUE;
  bs_desc.RenderTarget[0].SrcBlend = D3D10_BLEND_SRC_ALPHA;
  bs_desc.RenderTarget[0].DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
  bs_desc.RenderTarget[0].BlendOp = D3D10_BLEND_OP_ADD;
  bs_desc.RenderTarget[0].SrcBlendAlpha = D3D10_BLEND_ONE;
  bs_desc.RenderTarget[0].DestBlendAlpha = D3D10_BLEND_ZERO;
  bs_desc.RenderTarget[0].BlendOpAlpha = D3D10_BLEND_OP_ADD;
  bs_desc.RenderTarget[0].RenderTargetWriteMask = D3D10_COLOR_WRITE_ENABLE_ALL;
  P<ID3D10BlendState1> bs10;
  HRESULT hr_bs = d10_1 ? d10_1->CreateBlendState1(&bs_desc, &bs10) : E_FAIL;
  D3D10_BLEND_DESC got_bsd10 = {};
  D3D10_BLEND_DESC1 got_bsd10_1 = {};
  if (bs10) {
    bs10->GetDesc(&got_bsd10);
    bs10->GetDesc1(&got_bsd10_1);
  }
  P<ID3D11BlendState1> bs11;
  if (bs10) bs10->QueryInterface(__uuidof(ID3D11BlendState1), reinterpret_cast<void **>(&bs11));
  D3D11_BLEND_DESC1 got_bsd11_1 = {};
  if (bs11) bs11->GetDesc1(&got_bsd11_1);
  Out("d10 desc_bs hr=%08x d10=%d,%d,%d,%d,%d,%x d10_1=%d,%d,%d,%d,%x d11_1=%d,%d,%d,%d,%x\n",
      (unsigned)hr_bs,
      (int)got_bsd10.AlphaToCoverageEnable, (int)got_bsd10.BlendEnable[0], (int)got_bsd10.SrcBlend, (int)got_bsd10.DestBlend, (int)got_bsd10.BlendOp, got_bsd10.RenderTargetWriteMask[0],
      (int)got_bsd10_1.AlphaToCoverageEnable, (int)got_bsd10_1.RenderTarget[0].BlendEnable, (int)got_bsd10_1.RenderTarget[0].SrcBlend, (int)got_bsd10_1.RenderTarget[0].DestBlend, got_bsd10_1.RenderTarget[0].RenderTargetWriteMask,
      (int)got_bsd11_1.AlphaToCoverageEnable, (int)got_bsd11_1.RenderTarget[0].BlendEnable, (int)got_bsd11_1.RenderTarget[0].SrcBlend, (int)got_bsd11_1.RenderTarget[0].DestBlend, got_bsd11_1.RenderTarget[0].RenderTargetWriteMask);

  D3D10_DEPTH_STENCIL_DESC dsd10 = {};
  dsd10.DepthEnable = TRUE;
  dsd10.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
  dsd10.DepthFunc = D3D10_COMPARISON_LESS_EQUAL;
  dsd10.StencilEnable = FALSE;
  dsd10.StencilReadMask = 0xff;
  dsd10.StencilWriteMask = 0xff;
  dsd10.FrontFace = {D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_STENCIL_OP_KEEP, D3D10_COMPARISON_ALWAYS};
  dsd10.BackFace = dsd10.FrontFace;
  P<ID3D10DepthStencilState> ds10;
  HRESULT hr_ds = d10->CreateDepthStencilState(&dsd10, &ds10);
  D3D10_DEPTH_STENCIL_DESC got_ds10 = {};
  if (ds10) ds10->GetDesc(&got_ds10);
  P<ID3D11DepthStencilState> ds11;
  if (ds10) ds10->QueryInterface(__uuidof(ID3D11DepthStencilState), reinterpret_cast<void **>(&ds11));
  D3D11_DEPTH_STENCIL_DESC got_ds11 = {};
  if (ds11) ds11->GetDesc(&got_ds11);
  Out("d10 desc_ds hr=%08x d10=%d,%d,%d d11=%d,%d,%d\n",
      (unsigned)hr_ds,
      (int)got_ds10.DepthEnable, (int)got_ds10.DepthWriteMask, (int)got_ds10.DepthFunc,
      (int)got_ds11.DepthEnable, (int)got_ds11.DepthWriteMask, (int)got_ds11.DepthFunc);

  D3D10_RASTERIZER_DESC rsd10 = {D3D10_FILL_SOLID, D3D10_CULL_BACK, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE};
  P<ID3D10RasterizerState> rs10;
  HRESULT hr_rs = d10->CreateRasterizerState(&rsd10, &rs10);
  D3D10_RASTERIZER_DESC got_rs10 = {};
  if (rs10) rs10->GetDesc(&got_rs10);
  P<ID3D11RasterizerState> rs11;
  if (rs10) rs10->QueryInterface(__uuidof(ID3D11RasterizerState), reinterpret_cast<void **>(&rs11));
  D3D11_RASTERIZER_DESC got_rs11 = {};
  if (rs11) rs11->GetDesc(&got_rs11);
  Out("d10 desc_rs hr=%08x d10=%d,%d,%d d11=%d,%d,%d\n",
      (unsigned)hr_rs,
      (int)got_rs10.FillMode, (int)got_rs10.CullMode, (int)got_rs10.DepthClipEnable,
      (int)got_rs11.FillMode, (int)got_rs11.CullMode, (int)got_rs11.DepthClipEnable);

  D3D10_SAMPLER_DESC ssd10 = {};
  ssd10.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
  ssd10.AddressU = D3D10_TEXTURE_ADDRESS_WRAP;
  ssd10.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
  ssd10.AddressW = D3D10_TEXTURE_ADDRESS_MIRROR;
  ssd10.MaxAnisotropy = 4;
  ssd10.ComparisonFunc = D3D10_COMPARISON_NEVER;
  ssd10.MaxLOD = D3D10_FLOAT32_MAX;
  P<ID3D10SamplerState> ss10;
  HRESULT hr_ss = d10->CreateSamplerState(&ssd10, &ss10);
  D3D10_SAMPLER_DESC got_ss10 = {};
  if (ss10) ss10->GetDesc(&got_ss10);
  P<ID3D11SamplerState> ss11;
  if (ss10) ss10->QueryInterface(__uuidof(ID3D11SamplerState), reinterpret_cast<void **>(&ss11));
  D3D11_SAMPLER_DESC got_ss11 = {};
  if (ss11) ss11->GetDesc(&got_ss11);
  Out("d10 desc_ss hr=%08x d10=%x,%d,%d,%d,%u d11=%x,%d,%d,%d,%u\n",
      (unsigned)hr_ss,
      (unsigned)got_ss10.Filter, (int)got_ss10.AddressU, (int)got_ss10.AddressV, (int)got_ss10.AddressW, got_ss10.MaxAnisotropy,
      (unsigned)got_ss11.Filter, (int)got_ss11.AddressU, (int)got_ss11.AddressV, (int)got_ss11.AddressW, got_ss11.MaxAnisotropy);

  D3D10_QUERY_DESC qd10 = {D3D10_QUERY_EVENT, 0};
  P<ID3D10Query> q10;
  HRESULT hr_q = d10->CreateQuery(&qd10, &q10);
  D3D10_QUERY_DESC got_qd10 = {};
  if (q10) q10->GetDesc(&got_qd10);
  P<ID3D11Query> q11;
  if (q10) q10->QueryInterface(__uuidof(ID3D11Query), reinterpret_cast<void **>(&q11));
  D3D11_QUERY_DESC got_qd11 = {};
  if (q11) q11->GetDesc(&got_qd11);
  Out("d10 desc_q hr=%08x d10=%d,%x d11=%d,%x\n",
      (unsigned)hr_q, (int)got_qd10.Query, got_qd10.MiscFlags, (int)got_qd11.Query, got_qd11.MiscFlags);

  // Bind and read back via D3D10 getters (must return D3D10 wrapper pointers)
  UINT stride = 16, offset = 0;
  ID3D10Buffer *vbs[1] = {buf10.p};
  d10->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
  P<ID3D10Buffer> got_vb;
  UINT got_s = 0, got_o = 0;
  d10->IAGetVertexBuffers(0, 1, &got_vb, &got_s, &got_o);

  ID3D10ShaderResourceView *srvs[1] = {srv10.p};
  d10->PSSetShaderResources(0, 1, srvs);
  P<ID3D10ShaderResourceView> got_srv;
  d10->PSGetShaderResources(0, 1, &got_srv);

  ID3D10RenderTargetView *rtvs[1] = {rtv10.p};
  d10->OMSetRenderTargets(1, rtvs, nullptr);
  P<ID3D10RenderTargetView> got_rtv;
  d10->OMGetRenderTargets(1, &got_rtv, nullptr);

  FLOAT bf[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  d10->OMSetBlendState(bs10, bf, 0xffffffffu);
  P<ID3D10BlendState> got_bs;
  FLOAT got_bf[4] = {};
  UINT got_sm = 0;
  d10->OMGetBlendState(&got_bs, got_bf, &got_sm);

  Out("d10 objects buf=%08x(bw=%u/%u) tex=%08x srv=%08x rtv=%08x bs=%08x "
      "getters vb_eq=%d srv_eq=%d rtv_eq=%d bs_eq=%d\n",
      (unsigned)hr_buf, got_bd10.ByteWidth, got_bd11.ByteWidth,
      (unsigned)hr_tex, (unsigned)hr_srv, (unsigned)hr_rtv, (unsigned)hr_bs,
      (got_vb.p == buf10.p) ? 1 : 0, (got_srv.p == srv10.p) ? 1 : 0,
      (got_rtv.p == rtv10.p) ? 1 : 0,
      (got_bs.p == static_cast<ID3D10BlendState *>(bs10.p)) ? 1 : 0);
  ops += 10;

  auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  Raw("perfinfo d10 %.1f\n", dt > 0 ? ops / dt : 0.0);
}

#ifdef __APPLE__
static void RunNotImpl() {
  using FnNotImpl = bool (*)();
  auto fn = reinterpret_cast<FnNotImpl>(dlsym(RTLD_DEFAULT, "MTLD3DNotImplemented"));
  auto read_flag = [&]() -> int { return fn ? (fn() ? 1 : 0) : -1; };
  if (fn) (void)fn(); // clear any prior flag

  P<ID3D11Device> dev;
  P<ID3D11DeviceContext> imm;
  if (!CreateDefaultDevice(&dev, &imm)) return;
  P<ID3D11Device2> dev2;
  P<ID3D11Device3> dev3;
  P<IDXGIDevice> dxgi_dev;
  dev->QueryInterface(__uuidof(ID3D11Device2), reinterpret_cast<void **>(&dev2));
  dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void **>(&dev3));
  dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_dev));

  P<ID3D11DeviceContext2> imm2;
  imm->QueryInterface(__uuidof(ID3D11DeviceContext2), reinterpret_cast<void **>(&imm2));
  P<ID3D11DeviceContext> def;
  dev->CreateDeferredContext(0, &def);
  P<ID3D11CommandList> cmd_list;
  if (def) def->FinishCommandList(FALSE, &cmd_list);

  D3D11_BUFFER_DESC bd_def = {64, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_BUFFER_DESC bd_stg = {64, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0, 0};
  P<ID3D11Buffer> buf_def, buf_stg;
  dev->CreateBuffer(&bd_def, nullptr, &buf_def);
  dev->CreateBuffer(&bd_stg, nullptr, &buf_stg);

  D3D11_TEXTURE2D_DESC td_stg = {
      4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0};
  P<ID3D11Texture2D> tex_stg;
  dev->CreateTexture2D(&td_stg, nullptr, &tex_stg);

  D3D11_TEXTURE2D_DESC td_bc1_def = {
      4, 4, 1, 1, DXGI_FORMAT_BC1_UNORM, {1, 0},
      D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  D3D11_TEXTURE2D_DESC td_bc1_stg = {
      4, 4, 1, 1, DXGI_FORMAT_BC1_UNORM, {1, 0},
      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0};
  D3D11_TEXTURE2D_DESC td_u64_def = {
      1, 1, 1, 1, DXGI_FORMAT_R32G32_UINT, {1, 0},
      D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  D3D11_TEXTURE2D_DESC td_u64_stg = {
      1, 1, 1, 1, DXGI_FORMAT_R32G32_UINT, {1, 0},
      D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE, 0};
  P<ID3D11Texture2D> tex_bc1_def, tex_bc1_stg, tex_u64_def, tex_u64_stg;
  dev->CreateTexture2D(&td_bc1_def, nullptr, &tex_bc1_def);
  dev->CreateTexture2D(&td_bc1_stg, nullptr, &tex_bc1_stg);
  dev->CreateTexture2D(&td_u64_def, nullptr, &tex_u64_def);
  dev->CreateTexture2D(&td_u64_stg, nullptr, &tex_u64_stg);

  auto probe_void = [&](const char *name, auto &&callable) {
    if (fn) (void)fn();
    try {
      callable();
    } catch (...) {
    }
    int f1 = read_flag();
    int f2 = read_flag();
    Out("notimpl %s ret=void flag=%d,%d\n", name, f1, f2);
  };

  auto probe_hr = [&](const char *name, auto &&callable) {
    if (fn) (void)fn();
    HRESULT hr = E_NOTIMPL;
    try {
      hr = callable();
    } catch (...) {
      hr = E_NOTIMPL;
    }
    int f1 = read_flag();
    int f2 = read_flag();
    Out("notimpl %s hr=%08x flag=%d,%d\n", name, (unsigned)hr, f1, f2);
  };

  uint32_t dummy_data[16] = {};

  // 1. GetResourceTiling
  probe_void("dev2_get_resource_tiling", [&]() {
    dev2->GetResourceTiling(buf_def, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
  });
  // 2. WriteToSubresource
  probe_void("dev3_write_to_subresource", [&]() {
    dev3->WriteToSubresource(buf_def, 0, nullptr, dummy_data, 16, 64);
  });
  // 3. ReadFromSubresource
  probe_void("dev3_read_from_subresource", [&]() {
    dev3->ReadFromSubresource(dummy_data, 16, 64, buf_def, 0, nullptr);
  });
  // 4. IDXGIDevice::CreateSurface
  probe_hr("dxgidev_create_surface", [&]() {
    DXGI_SURFACE_DESC sd = {4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}};
    IDXGISurface *surf = nullptr;
    return dxgi_dev->CreateSurface(&sd, 1, DXGI_USAGE_RENDER_TARGET_OUTPUT, nullptr, &surf);
  });
  // 5. Immediate context Map on default resource with pMappedResource == nullptr
  probe_hr("imm_map_default_null", [&]() {
    return imm->Map(tex_u64_def, 0, D3D11_MAP_READ, 0, nullptr);
  });
  // 6. Deferred context Map on default resource with pMappedResource == nullptr
  probe_hr("def_map_default_null", [&]() {
    return def->Map(tex_u64_def, 0, D3D11_MAP_READ, 0, nullptr);
  });
  // 7. Deferred context ExecuteCommandList
  probe_void("def_execute_command_list", [&]() {
    def->ExecuteCommandList(cmd_list, FALSE);
  });
  // 8. UpdateSubresource on staging buffer
  probe_void("update_buffer_staging", [&]() {
    imm->UpdateSubresource(buf_stg, 0, nullptr, dummy_data, 64, 64);
  });
  // 9. UpdateTileMappings
  probe_hr("ctx2_update_tile_mappings", [&]() {
    return imm2->UpdateTileMappings(buf_def, 0, nullptr, nullptr, buf_def, 0, nullptr, nullptr, nullptr, 0);
  });
  // 10. CopyTileMappings
  probe_hr("ctx2_copy_tile_mappings", [&]() {
    return imm2->CopyTileMappings(buf_def, nullptr, buf_def, nullptr, nullptr, 0);
  });
  // 11. CopyTiles
  probe_void("ctx2_copy_tiles", [&]() {
    imm2->CopyTiles(buf_def, nullptr, nullptr, buf_def, 0, 0);
  });
  // 12. UpdateTiles
  probe_void("ctx2_update_tiles", [&]() {
    imm2->UpdateTiles(buf_def, nullptr, nullptr, dummy_data, 0);
  });
  // 13. ResizeTilePool
  probe_hr("ctx2_resize_tile_pool", [&]() {
    return imm2->ResizeTilePool(buf_def, 65536);
  });
  // 14. TiledResourceBarrier
  probe_void("ctx2_tiled_resource_barrier", [&]() {
    imm2->TiledResourceBarrier(nullptr, nullptr);
  });
  // 15. CopyTextureFromCompressed: BC1 default -> R32G32 staging
  probe_void("copy_from_compressed_def_to_stg", [&]() {
    imm->CopySubresourceRegion(tex_u64_stg, 0, 0, 0, 0, tex_bc1_def, 0, nullptr);
  });
  // 16. CopyTextureFromCompressed: BC1 staging -> R32G32 default
  probe_void("copy_from_compressed_stg_to_def", [&]() {
    imm->CopySubresourceRegion(tex_u64_def, 0, 0, 0, 0, tex_bc1_stg, 0, nullptr);
  });
  // 17. CopyTextureToCompressed: R32G32 default -> BC1 staging
  probe_void("copy_to_compressed_def_to_stg", [&]() {
    imm->CopySubresourceRegion(tex_bc1_stg, 0, 0, 0, 0, tex_u64_def, 0, nullptr);
  });
  // 18. CopyTextureToCompressed: R32G32 staging -> BC1 default
  probe_void("copy_to_compressed_stg_to_def", [&]() {
    imm->CopySubresourceRegion(tex_bc1_def, 0, 0, 0, 0, tex_u64_stg, 0, nullptr);
  });
  // 19. UpdateSubresource on staging texture
  probe_void("update_texture_staging", [&]() {
    imm->UpdateSubresource(tex_stg, 0, nullptr, dummy_data, 16, 64);
  });
}
#endif

int main(int argc, char **argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  if (argc >= 3 && std::strcmp(argv[1], "--child-fl") == 0) {
    return ChildFeatureLevelMain(argv[2]);
  }
  if (argc >= 5 && std::strcmp(argv[1], "--child-share") == 0) {
    g_shader_dir = argv[2];
    return ChildShareMain(argv[3], argv[4]);
  }
  if (argc < 3) {
    std::fprintf(stderr, "Usage: %s <shaders> <fixed|full> [<family>...] [--ported]\n", argv[0]);
    return 1;
  }

  g_shader_dir = argv[1];
  g_full = (std::strcmp(argv[2], "full") == 0);

  std::vector<std::string> families;
  for (int i = 3; i < argc; ++i) {
    if (std::strcmp(argv[i], "--ported") == 0) {
      g_ported = true;
    } else {
      families.emplace_back(argv[i]);
    }
  }

  auto want = [&](const char *fam) -> bool {
    if (families.empty()) return std::strcmp(fam, "notimpl") != 0 || g_ported;
    for (const auto &f : families) {
      if (f == fam) return true;
    }
    return false;
  };

  if (want("d11dev")) RunD11Dev();
  if (want("d11res")) RunD11Res();
  if (want("d11view")) RunD11View();
  if (want("d11state")) RunD11State();
  if (want("d11shader")) RunD11Shader();
  if (want("d11ctx")) RunD11Ctx();
  if (want("d11defer")) RunD11Defer();
  if (want("d11query")) RunD11Query();
  if (want("d11ext")) RunD11Ext();
  if (want("d11sc")) RunD11Sc();
  if (want("d11share")) RunD11Share();
  if (want("d10")) RunD10();
#ifdef __APPLE__
  if (want("notimpl")) RunNotImpl();
#endif

  return 0;
}
