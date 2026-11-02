// D3D12 golden behavioral probe for DXMTRS step 5c (Task 5).
//   d3d12_probe[.exe] [fixed|full] [--ported] [families...]
// Families: d12x, d12d, d12o, d12h, d12p, d12l, d12s, d12c, d12n (native only), d12r (only when named).
#include "d3d12_common.hpp"
#include <dxgi1_6.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#ifndef _WIN32
#include <dlfcn.h>
#endif

static bool g_full = false;
static bool g_ported = false;
static std::vector<std::string> g_families;

static bool WantFamily(const char *f, bool explicit_only = false) {
  if (g_families.empty())
    return !explicit_only;
  for (const auto &s : g_families)
    if (s == f)
      return true;
  return false;
}

static ID3D12Resource *MakeBuffer(Gpu &gpu, UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                                  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
  return gpu.Buffer(type, size, state, flags);
}

static D3D12_RESOURCE_BARRIER Transition(ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER b = {};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = r;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = before;
  b.Transition.StateAfter = after;
  return b;
}

static void RunAndWait(Gpu &gpu) {
  gpu.Submit();
}

// Private DXMT D3D12 extension interfaces.
struct MTL_TEMPORAL_SCALER_D3D12_DESC {
  UINT InputWidth, InputHeight, OutputWidth, OutputHeight;
  DXGI_FORMAT ColorFormat, DepthFormat, MotionFormat, OutputFormat, ReactiveMaskFormat;
  BOOL AutoExposure, OutputResolutionMotionVectors, JitteredMotionVectors;
  FLOAT InputContentMinScale, InputContentMaxScale;
};
struct MTL_TEMPORAL_UPSCALE_D3D12_DESC {
  ID3D12Resource *Color, *Depth, *MotionVector, *Exposure, *ReactiveMask, *Output;
  UINT InputContentWidth, InputContentHeight;
  UINT ColorOffsetX, ColorOffsetY, DepthOffsetX, DepthOffsetY, MotionOffsetX, MotionOffsetY,
      ReactiveOffsetX, ReactiveOffsetY, OutputOffsetX, OutputOffsetY;
  FLOAT JitterOffsetX, JitterOffsetY, MotionVectorScaleX, MotionVectorScaleY, PreExposure;
  BOOL Reset, DepthReversed;
};
static const GUID IID_IMTLD3D12TemporalScaler = {0x5b27acde, 0xa0d8, 0x4677, {0x93, 0xc4, 0xee, 0xdd, 0x17, 0xa0, 0xab, 0x14}};
static const GUID IID_IMTLD3D12DeviceExt = {0xa8c64ba7, 0x6d54, 0x4833, {0x8c, 0xc6, 0x26, 0x05, 0xc5, 0xb4, 0x54, 0xbe}};
static const GUID IID_IMTLD3D12CommandListExt = {0x5690e8a7, 0x51a1, 0x4b37, {0x8b, 0xbd, 0x73, 0x0d, 0x0f, 0xdb, 0x2c, 0xec}};
static const GUID IID_IMTLSwapChainFactory = {0x6c030003, 0x460a, 0x4b19, {0x90, 0x9b, 0x38, 0xf9, 0x7d, 0x20, 0x3e, 0x45}};
static const GUID IID_IMTLD3D12SharedFence = {0x7e4d9a3b, 0x1c82, 0x4f06, {0xa5, 0x61, 0xd8, 0x30, 0x2b, 0x9c, 0x47, 0xe2}};
static const GUID IID_D3D12DeviceDownlevel = {0x74eaee3f, 0x2f4b, 0x476d, {0x82, 0xba, 0x2b, 0x85, 0xcb, 0x49, 0xe3, 0x10}};
static const GUID WKPDID_DebugNameW = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};
static const GUID kCustomPrivGuid = {0x11223344, 0x5566, 0x7788, {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01}};

struct IMTLD3D12TemporalScaler : public IUnknown {};
struct IMTLD3D12DeviceExt : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetTemporalScalerScaleRange(FLOAT *pMin, FLOAT *pMax) = 0;
  virtual HRESULT STDMETHODCALLTYPE CreateTemporalScaler(const MTL_TEMPORAL_SCALER_D3D12_DESC *pDesc,
                                                         IMTLD3D12TemporalScaler **ppScaler) = 0;
};
struct IMTLD3D12CommandListExt : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE TemporalUpscale(IMTLD3D12TemporalScaler *pScaler,
                                                    const MTL_TEMPORAL_UPSCALE_D3D12_DESC *pDesc) = 0;
};
struct IMTLSwapChainFactory : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(IDXGIFactory1 *pFactory, HWND hWnd,
                                                    const DXGI_SWAP_CHAIN_DESC1 *pDesc,
                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
                                                    IDXGISwapChain1 **ppSwapChain) = 0;
};
struct IMTLD3D12SharedFence : public IUnknown {
  virtual uint64_t STDMETHODCALLTYPE GetSharedEvent() = 0;
  virtual uint64_t STDMETHODCALLTYPE GetSignaledValue() = 0;
};

// Embedded SM 4.0/5.0 DXBC shaders from dxmt/tests/shaders/d3d11/{quad.vs,solid.ps,compute.cs}.dxbc.
static const uint8_t kQuadVs[820] = {
  0x44,0x58,0x42,0x43,0x16,0xc4,0x08,0x05,0x6f,0x4e,0x4e,0x5d,0x2f,0x60,0x52,0xfd,0x4e,0x8a,0x90,0x6d,0x01,0x00,0x00,0x00,
  0x34,0x03,0x00,0x00,0x05,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x68,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0x04,0x01,0x00,0x00,
  0xb8,0x02,0x00,0x00,0x49,0x53,0x47,0x4e,0x2c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x20,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x00,0x00,0x53,0x56,0x5f,0x56,
  0x65,0x72,0x74,0x65,0x78,0x49,0x44,0x00,0x4f,0x53,0x47,0x4e,0x50,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x08,0x00,0x00,0x00,
  0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,
  0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x03,0x0c,0x00,0x00,
  0x53,0x56,0x5f,0x50,0x6f,0x73,0x69,0x74,0x69,0x6f,0x6e,0x00,0x54,0x45,0x58,0x43,0x4f,0x4f,0x52,0x44,0x00,0xab,0xab,0xab,
  0x52,0x44,0x45,0x46,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,
  0x00,0x04,0xfe,0xff,0x00,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,0x76,0x6b,0x64,0x33,0x64,0x2d,0x73,0x68,0x61,0x64,0x65,0x72,
  0x20,0x32,0x2e,0x31,0x20,0x28,0x57,0x69,0x6e,0x65,0x20,0x62,0x75,0x6e,0x64,0x6c,0x65,0x64,0x29,0x00,0x53,0x48,0x44,0x52,
  0xac,0x01,0x00,0x00,0x40,0x00,0x01,0x00,0x6b,0x00,0x00,0x00,0x67,0x00,0x00,0x04,0xf2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,
  0x01,0x00,0x00,0x00,0x65,0x00,0x00,0x03,0x32,0x20,0x10,0x00,0x01,0x00,0x00,0x00,0x60,0x00,0x00,0x04,0x12,0x10,0x10,0x00,
  0x00,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x68,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x29,0x00,0x00,0x07,0x12,0x00,0x10,0x00,
  0x00,0x00,0x00,0x00,0x06,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x07,
  0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,0x02,0x00,0x00,0x00,
  0x01,0x00,0x00,0x07,0x22,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x01,0x40,0x00,0x00,
  0x02,0x00,0x00,0x00,0x56,0x00,0x00,0x05,0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,
  0x56,0x00,0x00,0x05,0x22,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x56,0x05,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x05,
  0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x05,0x42,0x00,0x10,0x00,
  0x00,0x00,0x00,0x00,0x56,0x05,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x05,0xa2,0x00,0x10,0x00,0x00,0x00,0x00,0x00,
  0x06,0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x38,0x00,0x00,0x0a,0xa2,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x46,0x0c,0x10,0x00,
  0x00,0x00,0x00,0x00,0x02,0x40,0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,
  0x00,0x00,0x00,0x0a,0xa2,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x46,0x0c,0x10,0x00,0x00,0x00,0x00,0x00,0x02,0x40,0x00,0x00,
  0x00,0x00,0x80,0xbf,0x00,0x00,0x80,0xbf,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x3f,0x36,0x00,0x00,0x05,0x32,0x00,0x10,0x00,
  0x01,0x00,0x00,0x00,0xd6,0x05,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x05,0x42,0x00,0x10,0x00,0x01,0x00,0x00,0x00,
  0x01,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x05,0x82,0x00,0x10,0x00,0x01,0x00,0x00,0x00,0x01,0x40,0x00,0x00,
  0x00,0x00,0x80,0x3f,0x36,0x00,0x00,0x05,0xf2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x46,0x0e,0x10,0x00,0x01,0x00,0x00,0x00,
  0x36,0x00,0x00,0x05,0x32,0x20,0x10,0x00,0x01,0x00,0x00,0x00,0x86,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x00,0x01,
  0x53,0x54,0x41,0x54,0x74,0x00,0x00,0x00,0x14,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x02,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
  0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,
};
static const uint8_t kSolidPs[496] = {
  0x44,0x58,0x42,0x43,0x00,0xbc,0xa1,0x74,0xbd,0xec,0xbe,0x53,0x4e,0x54,0x90,0x0d,0x88,0x77,0x46,0xd0,0x01,0x00,0x00,0x00,
  0xf0,0x01,0x00,0x00,0x05,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x44,0x00,0x00,0x00,0x78,0x00,0x00,0x00,0x2c,0x01,0x00,0x00,
  0x74,0x01,0x00,0x00,0x49,0x53,0x47,0x4e,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x4f,0x53,0x47,0x4e,
  0x2c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0x53,0x56,0x5f,0x54,0x61,0x72,0x67,0x65,0x74,0x00,0xab,0xab,
  0x52,0x44,0x45,0x46,0xac,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x40,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x1c,0x00,0x00,0x00,
  0x00,0x04,0xff,0xff,0x00,0x00,0x00,0x00,0x8c,0x00,0x00,0x00,0x3c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x63,0x00,0xab,0xab,
  0x58,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x5c,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x63,0x00,0xab,0xab,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x7c,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x63,0x6f,0x6c,0x6f,0x72,0x00,0xab,0xab,0x01,0x00,0x03,0x00,0x01,0x00,0x04,0x00,0x00,0x00,0x00,0x00,
  0x01,0x00,0x00,0x00,0x76,0x6b,0x64,0x33,0x64,0x2d,0x73,0x68,0x61,0x64,0x65,0x72,0x20,0x32,0x2e,0x31,0x20,0x28,0x57,0x69,
  0x6e,0x65,0x20,0x62,0x75,0x6e,0x64,0x6c,0x65,0x64,0x29,0x00,0x53,0x48,0x44,0x52,0x40,0x00,0x00,0x00,0x40,0x00,0x00,0x00,
  0x10,0x00,0x00,0x00,0x59,0x00,0x00,0x04,0x46,0x8e,0x20,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x65,0x00,0x00,0x03,
  0xf2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x06,0xf2,0x20,0x10,0x00,0x00,0x00,0x00,0x00,0x46,0x8e,0x20,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x00,0x01,0x53,0x54,0x41,0x54,0x74,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
static const uint8_t kComputeCs[788] = {
  0x44,0x58,0x42,0x43,0x1d,0xf7,0xd2,0x48,0x6f,0x5c,0xf3,0x46,0xc9,0xb7,0x9c,0xce,0xa0,0xba,0x28,0x06,0x01,0x00,0x00,0x00,
  0x14,0x03,0x00,0x00,0x05,0x00,0x00,0x00,0x34,0x00,0x00,0x00,0x44,0x00,0x00,0x00,0x54,0x00,0x00,0x00,0x7c,0x01,0x00,0x00,
  0x78,0x02,0x00,0x00,0x49,0x53,0x47,0x4e,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x4f,0x53,0x47,0x4e,
  0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x52,0x44,0x45,0x46,0x20,0x01,0x00,0x00,0x01,0x00,0x00,0x00,
  0x84,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x3c,0x00,0x00,0x00,0x00,0x05,0x53,0x43,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,
  0x52,0x44,0x31,0x31,0x3c,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x20,0x00,0x00,0x00,0x28,0x00,0x00,0x00,0x24,0x00,0x00,0x00,
  0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x06,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
  0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
  0x04,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
  0x73,0x00,0xab,0xab,0x72,0x00,0xab,0xab,0x9c,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0xa0,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x73,0x00,0xab,0xab,0xc8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,
  0x02,0x00,0x00,0x00,0xdc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
  0x00,0x00,0x00,0x00,0x24,0x45,0x6c,0x65,0x6d,0x65,0x6e,0x74,0x00,0xab,0xab,0xab,0x75,0x69,0x6e,0x74,0x00,0xab,0xab,0xab,
  0x00,0x00,0x13,0x00,0x01,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd4,0x00,0x00,0x00,0x76,0x6b,0x64,0x33,0x64,0x2d,0x73,0x68,0x61,0x64,0x65,0x72,
  0x20,0x32,0x2e,0x31,0x20,0x28,0x57,0x69,0x6e,0x65,0x20,0x62,0x75,0x6e,0x64,0x6c,0x65,0x64,0x29,0x00,0x53,0x48,0x44,0x52,
  0xf4,0x00,0x00,0x00,0x50,0x00,0x05,0x00,0x3d,0x00,0x00,0x00,0x9b,0x00,0x00,0x04,0x40,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
  0x01,0x00,0x00,0x00,0x68,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x9e,0x00,0x00,0x04,0x01,0xe0,0x11,0x00,0x00,0x00,0x00,0x00,
  0x04,0x00,0x00,0x00,0x9d,0x00,0x00,0x03,0x01,0xe0,0x11,0x00,0x01,0x00,0x00,0x00,0x5f,0x00,0x00,0x02,0x72,0x00,0x02,0x00,
  0x26,0x00,0x00,0x07,0x00,0xd0,0x00,0x00,0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x02,0x00,0x01,0x40,0x00,0x00,
  0x03,0x00,0x00,0x00,0x1e,0x00,0x00,0x07,0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,
  0x01,0x40,0x00,0x00,0x01,0x00,0x00,0x00,0xa8,0x00,0x00,0x08,0x12,0xe0,0x11,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x02,0x00,
  0x01,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x26,0x00,0x00,0x07,0x00,0xd0,0x00,0x00,
  0x12,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x02,0x00,0x01,0x40,0x00,0x00,0x04,0x00,0x00,0x00,0x26,0x00,0x00,0x07,
  0x00,0xd0,0x00,0x00,0x12,0x00,0x10,0x00,0x01,0x00,0x00,0x00,0x06,0x00,0x02,0x00,0x01,0x40,0x00,0x00,0x07,0x00,0x00,0x00,
  0xa6,0x00,0x00,0x07,0x12,0xe0,0x11,0x00,0x01,0x00,0x00,0x00,0x06,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x10,0x00,
  0x01,0x00,0x00,0x00,0x3e,0x00,0x00,0x01,0x53,0x54,0x41,0x54,0x94,0x00,0x00,0x00,0x0c,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
};

// Minimal SHA-1 (RFC 3174) for d12x blob digest checks.
static std::string Sha1Hex(const void *data, size_t len) {
  auto rol = [](uint32_t v, int b) { return (v << b) | (v >> (32 - b)); };
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::vector<uint8_t> msg((const uint8_t *)data, (const uint8_t *)data + len);
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56)
    msg.push_back(0);
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 7; i >= 0; i--)
    msg.push_back((uint8_t)(bits >> (i * 8)));
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = ((uint32_t)msg[off + i * 4] << 24) | ((uint32_t)msg[off + i * 4 + 1] << 16) |
             ((uint32_t)msg[off + i * 4 + 2] << 8) | msg[off + i * 4 + 3];
    for (int i = 16; i < 80; i++)
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f = i < 20 ? ((b & c) | (~b & d)) : i < 40 ? (b ^ c ^ d) : i < 60 ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d);
      uint32_t k = i < 20 ? 0x5A827999u : i < 40 ? 0x6ED9EBA1u : i < 60 ? 0x8F1BBCDC : 0xCA62C1D6u;
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  char out[41];
  snprintf(out, sizeof out, "%08x%08x%08x%08x%08x", h[0], h[1], h[2], h[3], h[4]);
  return out;
}

#ifndef _WIN32
// Native macOS headless WSI + macdrv stand-in for d12s swapchains.
struct WsiWindowP { uint64_t monitor; uint32_t width, height, flags; };
struct WsiDisplayP { uint64_t monitor; int32_t left, top, right, bottom; uint16_t name[32]; };
struct WsiModeP { uint32_t width, height, num, den; };
struct WsiHooksP {
  int (*window)(uint64_t, WsiWindowP *);
  int (*display)(uint32_t, WsiDisplayP *);
  int (*mode)(uint64_t, uint32_t, WsiModeP *);
  void (*action)(const void *);
};
static int NativeGetWindow(uint64_t h, WsiWindowP *w) {
  if (!h) return 0;
  *w = {0x10001, 64, 64, 4};
  return 1;
}
static int NativeGetDisplay(uint32_t i, WsiDisplayP *d) {
  if (i >= 1) return 0;
  *d = {0x10001, 0, 0, 1920, 1080, {}};
  const char *n = "\\\\.\\DISPLAY1";
  for (int k = 0; n[k]; k++) d->name[k] = (uint16_t)n[k];
  return 1;
}
static int NativeGetMode(uint64_t monitor, uint32_t i, WsiModeP *m) {
  if (monitor != 0x10001 || i >= 1) return 0;
  *m = {1920, 1080, 60, 1};
  return 1;
}
static void NativeWsiAction(const void *) {}
static const WsiHooksP kNativeWsiHooks = {NativeGetWindow, NativeGetDisplay, NativeGetMode, NativeWsiAction};

struct MacdrvWinData { void *a, *b, *c, *client; };
static MacdrvWinData *NativeGetWinData(void *hwnd) {
  if (!hwnd) return nullptr;
  auto *d = (MacdrvWinData *)calloc(1, sizeof(MacdrvWinData));
  d->client = (void *)1;
  return d;
}
static void NativeReleaseWinData(MacdrvWinData *d) { free(d); }
static void *NativeCreateMetalView(void *, void *device) {
  using GetCls = void *(*)(const char *);
  using SelReg = void *(*)(const char *);
  using Msg0 = void *(*)(void *, void *);
  using Msg1 = void (*)(void *, void *, void *);
  auto cls = (GetCls)dlsym(RTLD_DEFAULT, "objc_getClass");
  auto sel = (SelReg)dlsym(RTLD_DEFAULT, "sel_registerName");
  auto msg0 = (Msg0)dlsym(RTLD_DEFAULT, "objc_msgSend");
  auto msg1 = (Msg1)dlsym(RTLD_DEFAULT, "objc_msgSend");
  void *layer = msg0(cls("CAMetalLayer"), sel("new"));
  msg1(layer, sel("setDevice:"), device);
  return layer;
}
static void *NativeGetMetalLayer(void *v) { return v; }
static void NativeReleaseMetalView(void *v) {
  using SelReg = void *(*)(const char *);
  using Msg0 = void (*)(void *, void *);
  auto sel = (SelReg)dlsym(RTLD_DEFAULT, "sel_registerName");
  auto msg0 = (Msg0)dlsym(RTLD_DEFAULT, "objc_msgSend");
  if (v) msg0(v, sel("release"));
}
extern "C" __attribute__((visibility("default"))) const struct {
  void *init;
  MacdrvWinData *(*get_win_data)(void *);
  void (*release_win_data)(MacdrvWinData *);
  void *w0, *w1, *w2;
  void *(*view_create_metal_view)(void *, void *);
  void *(*view_get_metal_layer)(void *);
  void (*view_release_metal_view)(void *);
} macdrv_functions = {nullptr, NativeGetWinData, NativeReleaseWinData, nullptr, nullptr, nullptr,
                      NativeCreateMetalView, NativeGetMetalLayer, NativeReleaseMetalView};
static void InitNativeWsi() {
  using SetFn = void (*)(const WsiHooksP *);
  if (auto fn = (SetFn)dlsym(RTLD_DEFAULT, "WMTSetWsiHooks"))
    fn(&kNativeWsiHooks);
}
#endif

// ---- Family d12x: D3D12 exports and root-signature serializer/deserializer ----
static void RunD12x() {
  // 1. D3D12CreateDevice
  ID3D12Device *dev = (ID3D12Device *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_fl_bad = D3D12CreateDevice(nullptr, (D3D_FEATURE_LEVEL)0x9100, __uuidof(ID3D12Device), (void **)&dev);
  printf("d12x create-device-bad-fl %08lx\n", (unsigned long)(unsigned)hr_fl_bad);
  HRESULT hr_null_out = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
  printf("d12x create-device-null-out %08lx\n", (unsigned long)(unsigned)hr_null_out);
  dev = nullptr;
  HRESULT hr_ok = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&dev);
  printf("d12x create-device-ok %08lx nonnull=%d\n", (unsigned long)(unsigned)hr_ok, dev != nullptr);
  if (dev) dev->Release();
  IDXGIFactory1 *fac = nullptr;
  IDXGIAdapter *adp = nullptr;
  if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac))) {
    if (SUCCEEDED(fac->EnumAdapters(0, &adp))) {
      ID3D12Device *adev = nullptr;
      HRESULT hr_adp = D3D12CreateDevice(adp, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void **)&adev);
      printf("d12x create-device-adapter %08lx nonnull=%d\n", (unsigned long)(unsigned)hr_adp, adev != nullptr);
      if (adev) adev->Release();
      adp->Release();
    }
    fac->Release();
  }
  struct FakeAdapter : IUnknown {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
      if (!ppv) return E_POINTER;
      if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIAdapter)) { *ppv = this; return S_OK; }
      *ppv = nullptr;
      return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
  } fake_adp;
  ID3D12Device *fdev = nullptr;
  HRESULT hr_bad_unk = D3D12CreateDevice(&fake_adp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&fdev);
  printf("d12x create-device-non-dxmt-adapter %08lx\n", (unsigned long)(unsigned)hr_bad_unk);

  // 2. D3D12GetDebugInterface & D3D12GetInterface
  void *dbg = nullptr;
  HRESULT hr_dbg = D3D12GetDebugInterface(__uuidof(IUnknown), &dbg);
  printf("d12x get-debug-interface %08lx\n", (unsigned long)(unsigned)hr_dbg);
  HRESULT hr_dbg_null = D3D12GetDebugInterface(__uuidof(IUnknown), nullptr);
  printf("d12x get-debug-interface-null %08lx\n", (unsigned long)(unsigned)hr_dbg_null);
  void *ifc = nullptr;
#ifdef _WIN32
  using PFN_GetInterface = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
  auto fn_gi = (PFN_GetInterface)GetProcAddress(LoadLibraryA("d3d12.dll"), "D3D12GetInterface");
  HRESULT hr_ifc = fn_gi(__uuidof(IUnknown), __uuidof(IUnknown), &ifc);
  HRESULT hr_ifc_null = fn_gi(__uuidof(IUnknown), __uuidof(IUnknown), nullptr);
#else
  HRESULT hr_ifc = D3D12GetInterface(__uuidof(IUnknown), __uuidof(IUnknown), &ifc);
  HRESULT hr_ifc_null = D3D12GetInterface(__uuidof(IUnknown), __uuidof(IUnknown), nullptr);
#endif
  printf("d12x get-interface %08lx\n", (unsigned long)(unsigned)hr_ifc);
  printf("d12x get-interface-null %08lx\n", (unsigned long)(unsigned)hr_ifc_null);

  // 3. Serialize & Deserialize argument checks
  D3D12_ROOT_SIGNATURE_DESC empty10 = {};
  ID3DBlob *blob = nullptr, *err = (ID3DBlob *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_s_null_blob = D3D12SerializeRootSignature(&empty10, D3D_ROOT_SIGNATURE_VERSION_1_0, nullptr, &err);
  printf("d12x serialize-null-blob %08lx err_zero=%d\n", (unsigned long)(unsigned)hr_s_null_blob, err == nullptr);
  err = (ID3DBlob *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_s_bad_ver = D3D12SerializeRootSignature(&empty10, (D3D_ROOT_SIGNATURE_VERSION)99, &blob, &err);
  printf("d12x serialize-bad-ver %08lx err_zero=%d\n", (unsigned long)(unsigned)hr_s_bad_ver, err == nullptr);
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC vbad = {(D3D_ROOT_SIGNATURE_VERSION)99};
  err = (ID3DBlob *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_sv_bad = D3D12SerializeVersionedRootSignature(&vbad, &blob, &err);
  printf("d12x serialize-v-bad-ver %08lx err_zero=%d\n", (unsigned long)(unsigned)hr_sv_bad, err == nullptr);

  uint32_t bad_bytes[16] = {1, 2, 3, 4};
  void *des = (void *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_d_bad = D3D12CreateRootSignatureDeserializer(bad_bytes, sizeof bad_bytes,
                                                          __uuidof(ID3D12RootSignatureDeserializer), &des);
  printf("d12x deser-bad-bytes %08lx zeroed=%d\n", (unsigned long)(unsigned)hr_d_bad, des == nullptr);
  des = (void *)(uintptr_t)0xdeadbeefu;
  HRESULT hr_dv_bad = D3D12CreateVersionedRootSignatureDeserializer(
      bad_bytes, sizeof bad_bytes, __uuidof(ID3D12VersionedRootSignatureDeserializer), &des);
  printf("d12x deser-v-bad-bytes %08lx zeroed=%d\n", (unsigned long)(unsigned)hr_dv_bad, des == nullptr);

  // 4. 5a's 12 root-signature descriptions: serialize, check SHA-1, and round-trip both deserializers.
  auto check_v = [](const char *name, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC &vd) {
    ID3DBlob *b = nullptr;
    CHECK(D3D12SerializeVersionedRootSignature(&vd, &b, nullptr));
    std::string sha = Sha1Hex(b->GetBufferPointer(), b->GetBufferSize());
    ID3D12RootSignatureDeserializer *d10 = nullptr;
    HRESULT hr10 = D3D12CreateRootSignatureDeserializer(
        b->GetBufferPointer(), b->GetBufferSize(), __uuidof(ID3D12RootSignatureDeserializer), (void **)&d10);
    const D3D12_ROOT_SIGNATURE_DESC *p10 = d10 ? d10->GetRootSignatureDesc() : nullptr;
    ID3D12VersionedRootSignatureDeserializer *dv = nullptr;
    HRESULT hrv = D3D12CreateVersionedRootSignatureDeserializer(
        b->GetBufferPointer(), b->GetBufferSize(), __uuidof(ID3D12VersionedRootSignatureDeserializer), (void **)&dv);
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *unconv = dv ? dv->GetUnconvertedRootSignatureDesc() : nullptr;
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *at10 = nullptr, *at11 = nullptr, *atbad = nullptr;
    HRESULT hr_at10 = dv ? dv->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &at10) : E_FAIL;
    HRESULT hr_at11 = dv ? dv->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &at11) : E_FAIL;
    HRESULT hr_atbad = dv ? dv->GetRootSignatureDescAtVersion((D3D_ROOT_SIGNATURE_VERSION)99, &atbad) : E_FAIL;
    printf("d12x rootsig-%s size=%zu sha1=%s d10=%08lx/%u dv=%08lx/%u at10=%08lx/%u at11=%08lx/%u atbad=%08lx\n",
           name, (size_t)b->GetBufferSize(), sha.c_str(),
           (unsigned long)(unsigned)hr10, p10 ? p10->NumParameters : 99u,
           (unsigned long)(unsigned)hrv, unconv ? (unsigned)unconv->Version : 0u,
           (unsigned long)(unsigned)hr_at10, at10 ? at10->Desc_1_0.NumParameters : 99u,
           (unsigned long)(unsigned)hr_at11, at11 ? at11->Desc_1_1.NumParameters : 99u,
           (unsigned long)(unsigned)hr_atbad);
    if (d10) d10->Release();
    if (dv) dv->Release();
    b->Release();
  };

  auto v10 = [](UINT np, const D3D12_ROOT_PARAMETER *p, UINT ns, const D3D12_STATIC_SAMPLER_DESC *s,
                D3D12_ROOT_SIGNATURE_FLAGS f) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC d = {D3D_ROOT_SIGNATURE_VERSION_1_0};
    d.Desc_1_0 = {np, p, ns, s, f};
    return d;
  };
  auto v11 = [](UINT np, const D3D12_ROOT_PARAMETER1 *p, UINT ns, const D3D12_STATIC_SAMPLER_DESC *s,
                D3D12_ROOT_SIGNATURE_FLAGS f) {
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC d = {D3D_ROOT_SIGNATURE_VERSION_1_1};
    d.Desc_1_1 = {np, p, ns, s, f};
    return d;
  };

  check_v("01-empty-v10", v10(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  check_v("02-empty-v11-ia", v11(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT));
  {
    D3D12_ROOT_PARAMETER p = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_VERTEX};
    p.Constants = {2, 1, 4};
    check_v("03-constants-v10", v10(1, &p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  }
  {
    D3D12_ROOT_PARAMETER1 p = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_PIXEL};
    p.Constants = {0, 0, 16};
    check_v("04-constants-v11", v11(1, &p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT));
  }
  {
    D3D12_ROOT_PARAMETER p[3] = {
      {D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_ALL},
      {D3D12_ROOT_PARAMETER_TYPE_SRV, {}, D3D12_SHADER_VISIBILITY_VERTEX},
      {D3D12_ROOT_PARAMETER_TYPE_UAV, {}, D3D12_SHADER_VISIBILITY_PIXEL},
    };
    p[0].Descriptor = {0, 0}; p[1].Descriptor = {3, 1}; p[2].Descriptor = {1, 2};
    check_v("05-descriptors-v10", v10(3, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  }
  {
    D3D12_ROOT_PARAMETER1 p[3] = {
      {D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_ALL},
      {D3D12_ROOT_PARAMETER_TYPE_SRV, {}, D3D12_SHADER_VISIBILITY_GEOMETRY},
      {D3D12_ROOT_PARAMETER_TYPE_UAV, {}, D3D12_SHADER_VISIBILITY_ALL},
    };
    p[0].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    p[1].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    p[2].Descriptor = {2, 3, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    check_v("06-descriptors-v11", v11(3, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  }
  {
    D3D12_DESCRIPTOR_RANGE r[3] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, 1, 8},
      {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
    };
    D3D12_ROOT_PARAMETER p = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_ALL};
    p.DescriptorTable = {3, r};
    check_v("07-table-v10", v10(1, &p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  }
  {
    D3D12_DESCRIPTOR_RANGE1 r[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 2, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 16},
    };
    D3D12_ROOT_PARAMETER1 p[2] = {
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_PIXEL},
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_VERTEX},
    };
    p[0].DescriptorTable = {1, &r[0]}; p[1].DescriptorTable = {1, &r[1]};
    check_v("08-table-v11", v11(2, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED));
  }
  auto samp = [](D3D12_FILTER f, D3D12_TEXTURE_ADDRESS_MODE u, D3D12_TEXTURE_ADDRESS_MODE v,
                 D3D12_TEXTURE_ADDRESS_MODE w, FLOAT bias, UINT aniso, D3D12_COMPARISON_FUNC cmp,
                 D3D12_STATIC_BORDER_COLOR bc, FLOAT minl, FLOAT maxl, UINT reg, UINT sp, D3D12_SHADER_VISIBILITY vis) {
    return D3D12_STATIC_SAMPLER_DESC{f, u, v, w, bias, aniso, cmp, bc, minl, maxl, reg, sp, vis};
  };
  {
    D3D12_STATIC_SAMPLER_DESC s = samp(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
                                       -0.5f, 8, D3D12_COMPARISON_FUNC_LESS_EQUAL,
                                       D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE, 0.25f, 12.0f, 3, 2,
                                       D3D12_SHADER_VISIBILITY_PIXEL);
    check_v("09-sampler-v10", v10(0, nullptr, 1, &s, D3D12_ROOT_SIGNATURE_FLAG_NONE));
  }
  {
    D3D12_STATIC_SAMPLER_DESC s[2] = {
      samp(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
           D3D12_TEXTURE_ADDRESS_MODE_BORDER, 0.0f, 1, D3D12_COMPARISON_FUNC_NEVER,
           D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, 1000.0f, 0, 0, D3D12_SHADER_VISIBILITY_ALL),
      samp(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
           D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1.25f, 16, D3D12_COMPARISON_FUNC_GREATER,
           D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 1.0f, 8.0f, 5, 1, D3D12_SHADER_VISIBILITY_HULL),
    };
    check_v("10-samplers-v11", v11(0, nullptr, 2, s, D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED));
  }
  {
    D3D12_DESCRIPTOR_RANGE r[2] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 1, 0, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 2, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
    };
    D3D12_ROOT_PARAMETER p[4] = {
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_ALL},
      {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_VERTEX},
      {D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_PIXEL},
      {D3D12_ROOT_PARAMETER_TYPE_UAV, {}, D3D12_SHADER_VISIBILITY_ALL},
    };
    p[0].DescriptorTable = {2, r}; p[1].Constants = {0, 0, 3}; p[2].Descriptor = {4, 1}; p[3].Descriptor = {0, 0};
    D3D12_STATIC_SAMPLER_DESC s = samp(D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                                       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                       D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 0.0f, 4, D3D12_COMPARISON_FUNC_LESS,
                                       D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.0f, 16.0f, 1, 0,
                                       D3D12_SHADER_VISIBILITY_PIXEL);
    check_v("11-mixed-v10", v10(4, p, 1, &s, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS));
  }
  {
    D3D12_DESCRIPTOR_RANGE1 r[3] = {
      {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE, 0},
      {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE |
       D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
      {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4, 0, 3, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC, 12},
    };
    D3D12_ROOT_PARAMETER1 p[5] = {
      {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_ALL},
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_ALL},
      {D3D12_ROOT_PARAMETER_TYPE_SRV, {}, D3D12_SHADER_VISIBILITY_VERTEX},
      {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_DOMAIN},
      {D3D12_ROOT_PARAMETER_TYPE_CBV, {}, D3D12_SHADER_VISIBILITY_GEOMETRY},
    };
    p[0].Constants = {7, 2, 8}; p[1].DescriptorTable = {2, r};
    p[2].Descriptor = {9, 4, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    p[3].DescriptorTable = {1, &r[2]};
    p[4].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    D3D12_STATIC_SAMPLER_DESC s[2] = {
      samp(D3D12_FILTER_MINIMUM_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
           D3D12_TEXTURE_ADDRESS_MODE_WRAP, -1.0f, 2, D3D12_COMPARISON_FUNC_ALWAYS,
           D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE, 0.5f, 4.0f, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX),
      samp(D3D12_FILTER_MAXIMUM_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_MIRROR,
           D3D12_TEXTURE_ADDRESS_MODE_BORDER, 2.0f, 16, D3D12_COMPARISON_FUNC_NOT_EQUAL,
           D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, 0.0f, 32.0f, 2, 5, D3D12_SHADER_VISIBILITY_PIXEL),
    };
    check_v("12-mixed-v11", v11(5, p, 2, s, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                                D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED));
  }
}

// --- d12d: Device COM, CheckFeatureSupport, Create* validation, private data, residency, scaler ---
static void RunD12d(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;

  // 1. QI matrix for ID3D12Device..ID3D12Device10, IMTLD3D12DeviceExt, IUnknown, ID3D12Object, and negative IIDs
  struct QiEntry { const char *name; REFIID iid; };
  const QiEntry qis[] = {
    {"IUnknown", __uuidof(IUnknown)},
    {"ID3D12Object", __uuidof(ID3D12Object)},
    {"ID3D12Device", __uuidof(ID3D12Device)},
    {"ID3D12Device1", __uuidof(ID3D12Device1)},
    {"ID3D12Device2", __uuidof(ID3D12Device2)},
    {"ID3D12Device3", __uuidof(ID3D12Device3)},
    {"ID3D12Device4", __uuidof(ID3D12Device4)},
    {"ID3D12Device5", __uuidof(ID3D12Device5)},
    {"ID3D12Device6", __uuidof(ID3D12Device6)},
    {"ID3D12Device7", __uuidof(ID3D12Device7)},
    {"ID3D12Device8", __uuidof(ID3D12Device8)},
    {"ID3D12Device9", __uuidof(ID3D12Device9)},
    {"ID3D12Device10", __uuidof(ID3D12Device10)},
    {"IMTLD3D12DeviceExt", IID_IMTLD3D12DeviceExt},
    {"IDXGIDevice", __uuidof(IDXGIDevice)},
    {"ID3D12Fence", __uuidof(ID3D12Fence)},
  };
  for (const auto &q : qis) {
    void *out = (void *)1;
    HRESULT hr = dev->QueryInterface(q.iid, &out);
    bool same_unk = false;
    if (SUCCEEDED(hr) && out) {
      IUnknown *u1 = nullptr, *u2 = nullptr;
      dev->QueryInterface(__uuidof(IUnknown), (void **)&u1);
      ((IUnknown *)out)->QueryInterface(__uuidof(IUnknown), (void **)&u2);
      same_unk = (u1 == u2);
      if (u1) u1->Release();
      if (u2) u2->Release();
      ((IUnknown *)out)->Release();
    }
    printf("d12d qi-%s %08lx nonnull=%d same_unk=%d\n",
           q.name, (unsigned long)(unsigned)hr, out != nullptr, same_unk ? 1 : 0);
  }
  printf("d12d qi-null-ppv %08lx\n", (unsigned long)(unsigned)dev->QueryInterface(__uuidof(ID3D12Device), nullptr));

  // Refcount delta check
  ULONG rc0 = dev->AddRef();
  ULONG rc1 = dev->AddRef();
  ULONG rc2 = dev->Release();
  ULONG rc3 = dev->Release();
  printf("d12d refcount-delta d1=%ld d2=%ld d3=%ld\n", (long)(rc1 - rc0), (long)(rc2 - rc0), (long)(rc3 - (rc0 - 1)));

  // 2. GetNodeCount, GetDeviceRemovedReason, GetAdapterLuid vs IDXGIAdapter::GetDesc
  printf("d12d basic nodes=%u removed=%08lx\n",
         dev->GetNodeCount(), (unsigned long)(unsigned)dev->GetDeviceRemovedReason());
  {
    LUID luid_ret = {}, *luid_ptr = dev->GetAdapterLuid(&luid_ret);
    LUID luid_val = dev->GetAdapterLuid();
    IDXGIFactory1 *fac = nullptr;
    IDXGIAdapter *adap = nullptr;
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac));
    CHECK(fac->EnumAdapters(0, &adap));
    DXGI_ADAPTER_DESC adesc = {};
    CHECK(adap->GetDesc(&adesc));
    adap->Release();
    fac->Release();
    bool match = (luid_ret.LowPart == adesc.AdapterLuid.LowPart && luid_ret.HighPart == adesc.AdapterLuid.HighPart &&
                  luid_val.LowPart == luid_ret.LowPart && luid_val.HighPart == luid_ret.HighPart);
    printf("d12d adapter-luid ptr_eq=%d match_dxgi=%d", luid_ptr == &luid_ret, match ? 1 : 0);
    if (g_full)
      printf(" luid=%08lx:%08lx", (unsigned long)luid_ret.HighPart, (unsigned long)luid_ret.LowPart);
    printf("\n");
  }

  // 3. GetDescriptorHandleIncrementSize
  printf("d12d desc-inc cbv=%u samp=%u rtv=%u dsv=%u bad=%u\n",
         dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
         dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER),
         dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
         dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
         dev->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)99));

  // 4. CheckFeatureSupport per D3D12_FEATURE
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS o0 = {};
    HRESULT hr_bad = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof(o0) - 1);
    HRESULT hr = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof(o0));
    printf("d12d feat-options0 hr=%08lx bad_sz=%08lx bind=%d heap_tier=%d",
           (unsigned long)(unsigned)hr, (unsigned long)(unsigned)hr_bad,
           (int)o0.ResourceBindingTier, (int)o0.ResourceHeapTier);
    if (g_full)
      printf(" rov=%d tiled=%d conservative=%d", (int)o0.ROVsSupported, (int)o0.TiledResourcesTier,
             (int)o0.ConservativeRasterizationTier);
    printf("\n");
  }
  {
    D3D12_FEATURE_DATA_ARCHITECTURE a0 = {0}, a1 = {1};
    HRESULT hr0 = dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &a0, sizeof(a0));
    HRESULT hr1 = dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &a1, sizeof(a1));
    D3D12_FEATURE_DATA_ARCHITECTURE1 a1_0 = {0}, a1_1 = {1};
    HRESULT hr10 = dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &a1_0, sizeof(a1_0));
    HRESULT hr11 = dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &a1_1, sizeof(a1_1));
    printf("d12d feat-arch hr0=%08lx hr_node1=%08lx arch1_hr0=%08lx arch1_node1=%08lx",
           (unsigned long)(unsigned)hr0, (unsigned long)(unsigned)hr1,
           (unsigned long)(unsigned)hr10, (unsigned long)(unsigned)hr11);
    if (g_full)
      printf(" tile=%d uma=%d cache_uma=%d isolated_mmu=%d",
             a1_0.TileBasedRenderer, a1_0.UMA, a1_0.CacheCoherentUMA, a1_0.IsolatedMMU);
    printf("\n");
  }
  {
    D3D_FEATURE_LEVEL fls[] = {(D3D_FEATURE_LEVEL)0xc200, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS fld = {4, fls, (D3D_FEATURE_LEVEL)0};
    HRESULT hr = dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fld, sizeof(fld));
    D3D12_FEATURE_DATA_FEATURE_LEVELS fld_empty = {0, nullptr, (D3D_FEATURE_LEVEL)0};
    HRESULT hr_empty = dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fld_empty, sizeof(fld_empty));
    printf("d12d feat-levels hr=%08lx max=%04x empty_hr=%08lx\n",
           (unsigned long)(unsigned)hr, (unsigned)fld.MaxSupportedFeatureLevel, (unsigned long)(unsigned)hr_empty);
  }
  {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = {DXGI_FORMAT_R8G8B8A8_UNORM};
    HRESULT hr = dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
    D3D12_FEATURE_DATA_FORMAT_INFO fi = {DXGI_FORMAT_R8G8B8A8_UNORM, 0};
    HRESULT hr_fi = dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &fi, sizeof(fi));
    D3D12_FEATURE_DATA_FORMAT_INFO fi_nv12 = {DXGI_FORMAT_NV12, 0};
    HRESULT hr_nv12 = dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &fi_nv12, sizeof(fi_nv12));
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms4 = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0};
    HRESULT hr_ms4 = dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms4, sizeof(ms4));
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS ms3 = {DXGI_FORMAT_R8G8B8A8_UNORM, 3, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 99};
    HRESULT hr_ms3 = dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &ms3, sizeof(ms3));
    printf("d12d feat-formats fs=%08lx/%08x/%08x fi_rgba=%08lx/%u fi_nv12=%08lx/%u ms4=%08lx/%u ms3=%08lx/%u\n",
           (unsigned long)(unsigned)hr, (unsigned)fs.Support1, (unsigned)fs.Support2,
           (unsigned long)(unsigned)hr_fi, (unsigned)fi.PlaneCount,
           (unsigned long)(unsigned)hr_nv12, (unsigned)fi_nv12.PlaneCount,
           (unsigned long)(unsigned)hr_ms4, ms4.NumQualityLevels,
           (unsigned long)(unsigned)hr_ms3, ms3.NumQualityLevels);
  }
  {
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gva = {};
    HRESULT hr_gva = dev->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gva, sizeof(gva));
    D3D12_FEATURE_DATA_SHADER_MODEL sm60 = {D3D_SHADER_MODEL_6_0};
    HRESULT hr_sm60 = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm60, sizeof(sm60));
    D3D12_FEATURE_DATA_SHADER_MODEL sm_bad = {(D3D_SHADER_MODEL)0x99};
    HRESULT hr_smbad = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm_bad, sizeof(sm_bad));
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rs11 = {D3D_ROOT_SIGNATURE_VERSION_1_1};
    HRESULT hr_rs11 = dev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rs11, sizeof(rs11));
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rsbad = {(D3D_ROOT_SIGNATURE_VERSION)99};
    HRESULT hr_rsbad = dev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rsbad, sizeof(rsbad));
    D3D12_FEATURE_DATA_SHADER_CACHE sc = {};
    HRESULT hr_sc = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &sc, sizeof(sc));
    D3D12_FEATURE_DATA_EXISTING_HEAPS eh = {};
    HRESULT hr_eh = dev->CheckFeatureSupport(D3D12_FEATURE_EXISTING_HEAPS, &eh, sizeof(eh));
    D3D12_FEATURE_DATA_SERIALIZATION ser = {0};
    HRESULT hr_ser = dev->CheckFeatureSupport(D3D12_FEATURE_SERIALIZATION, &ser, sizeof(ser));
    D3D12_FEATURE_DATA_CROSS_NODE cn = {};
    HRESULT hr_cn = dev->CheckFeatureSupport(D3D12_FEATURE_CROSS_NODE, &cn, sizeof(cn));
    printf("d12d feat-misc gva=%08lx/%u/%u sm60=%08lx/%02x smbad=%08lx rs11=%08lx/%u rsbad=%08lx sc=%08lx/%x eh=%08lx/%d ser=%08lx/%d cn=%08lx\n",
           (unsigned long)(unsigned)hr_gva, gva.MaxGPUVirtualAddressBitsPerResource, gva.MaxGPUVirtualAddressBitsPerProcess,
           (unsigned long)(unsigned)hr_sm60, (unsigned)sm60.HighestShaderModel,
           (unsigned long)(unsigned)hr_smbad,
           (unsigned long)(unsigned)hr_rs11, (unsigned)rs11.HighestVersion,
           (unsigned long)(unsigned)hr_rsbad,
           (unsigned long)(unsigned)hr_sc, (unsigned)sc.SupportFlags,
           (unsigned long)(unsigned)hr_eh, eh.Supported,
           (unsigned long)(unsigned)hr_ser, (int)ser.HeapSerializationTier,
           (unsigned long)(unsigned)hr_cn);
  }
  {
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 o2 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS8 o8 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 o9 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS10 o10 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS11 o11 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 o13 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 o14 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS15 o15 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 o16 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 o17 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS18 o18 = {};
    struct { uint32_t w[10]; } o19 = {};
    struct { uint32_t w[4]; } o21 = {};
    struct { uint32_t w[4]; } o22 = {};
    HRESULT h1 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1));
    HRESULT h2 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &o2, sizeof(o2));
    HRESULT h3 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &o3, sizeof(o3));
    HRESULT h4 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof(o4));
    HRESULT h5 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5));
    HRESULT h6 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof(o6));
    HRESULT h7 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7));
    HRESULT h8 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8, &o8, sizeof(o8));
    HRESULT h9 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &o9, sizeof(o9));
    HRESULT h10 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS10, &o10, sizeof(o10));
    HRESULT h11 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS11, &o11, sizeof(o11));
    HRESULT h12 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof(o12));
    HRESULT h13 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &o13, sizeof(o13));
    HRESULT h14 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS14, &o14, sizeof(o14));
    HRESULT h15 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS15, &o15, sizeof(o15));
    HRESULT h16 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &o16, sizeof(o16));
    HRESULT h17 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &o17, sizeof(o17));
    HRESULT h18 = dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS18, &o18, sizeof(o18));
    HRESULT h19 = dev->CheckFeatureSupport((D3D12_FEATURE)48, &o19, sizeof(o19));
    HRESULT h21 = dev->CheckFeatureSupport((D3D12_FEATURE)53, &o21, sizeof(o21));
    HRESULT h22 = dev->CheckFeatureSupport((D3D12_FEATURE)65, &o22, sizeof(o22));
    HRESULT h_unimp = dev->CheckFeatureSupport((D3D12_FEATURE)999, &o22, sizeof(o22));
    printf("d12d feat-options1-22 %08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx/%08lx unimp=%08lx align16=%d enh_bar=%d max_samp=%u ei_tier=%u max1d=%u",
           (unsigned long)(unsigned)h1, (unsigned long)(unsigned)h2, (unsigned long)(unsigned)h3,
           (unsigned long)(unsigned)h4, (unsigned long)(unsigned)h5, (unsigned long)(unsigned)h6,
           (unsigned long)(unsigned)h7, (unsigned long)(unsigned)h8, (unsigned long)(unsigned)h9,
           (unsigned long)(unsigned)h10, (unsigned long)(unsigned)h11, (unsigned long)(unsigned)h12,
           (unsigned long)(unsigned)h13, (unsigned long)(unsigned)h14, (unsigned long)(unsigned)h15,
           (unsigned long)(unsigned)h16, (unsigned long)(unsigned)h17, (unsigned long)(unsigned)h18,
           (unsigned long)(unsigned)h19, (unsigned long)(unsigned)h21, (unsigned long)(unsigned)h22,
           (unsigned long)(unsigned)h_unimp,
           o4.MSAA64KBAlignedTextureSupported, o12.EnhancedBarriersSupported,
           o19.w[6], o21.w[1], o22.w[2]);
    if (g_full)
      printf(" wave=%d/%u/%u rt_tier=%d mesh_tier=%d",
             o1.WaveOps, o1.WaveLaneCountMin, o1.WaveLaneCountMax,
             (int)o5.RaytracingTier, (int)o7.MeshShaderTier);
    printf("\n");
  }

  // 5. Create* argument errors
  {
    ID3D12GraphicsCommandList *cl = nullptr;
    HRESULT hr_null_alloc = dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, nullptr, nullptr,
                                                   __uuidof(ID3D12GraphicsCommandList), (void **)&cl);
    ID3D12Device4 *dev4 = nullptr;
    CHECK(dev->QueryInterface(__uuidof(ID3D12Device4), (void **)&dev4));
    HRESULT hr_cl1_bad = dev4->CreateCommandList1(0, (D3D12_COMMAND_LIST_TYPE)99, D3D12_COMMAND_LIST_FLAG_NONE,
                                                  __uuidof(ID3D12GraphicsCommandList), (void **)&cl);
    D3D12_HEAP_PROPERTIES hp_def = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_HEAP_PROPERTIES hp_custom_bad = {D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN};
    D3D12_RESOURCE_DESC rdesc_buf = {};
    rdesc_buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rdesc_buf.Width = 256;
    rdesc_buf.Height = 1;
    rdesc_buf.DepthOrArraySize = 1;
    rdesc_buf.MipLevels = 1;
    rdesc_buf.SampleDesc.Count = 1;
    rdesc_buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *res = nullptr;
    D3D12_HEAP_PROPERTIES hp_bad_type = {(D3D12_HEAP_TYPE)99};
    D3D12_RESOURCE_DESC rdesc_bad_dim = rdesc_buf;
    rdesc_bad_dim.Dimension = D3D12_RESOURCE_DIMENSION_UNKNOWN;
    HRESULT hr_bad_hp = dev->CreateCommittedResource(&hp_bad_type, D3D12_HEAP_FLAG_NONE, &rdesc_buf,
                                                     D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void **)&res);
    HRESULT hr_custom_bad = dev->CreateCommittedResource(&hp_custom_bad, D3D12_HEAP_FLAG_NONE, &rdesc_buf,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void **)&res);
    HRESULT hr_bad_desc = dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &rdesc_bad_dim,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void **)&res);
    HRESULT hr_placed_null = dev->CreatePlacedResource(nullptr, 0, &rdesc_buf, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       __uuidof(ID3D12Resource), (void **)&res);
    HRESULT hr_prot = dev4->CreateCommittedResource1(&hp_def, D3D12_HEAP_FLAG_NONE, &rdesc_buf,
                                                     D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                     (ID3D12ProtectedResourceSession *)1,
                                                     __uuidof(ID3D12Resource), (void **)&res);
    dev4->Release();
    printf("d12d create-errors cl_null_alloc=%08lx cl1_bad=%08lx comm_bad_hp=%08lx comm_custom_bad=%08lx comm_bad_desc=%08lx placed_null_heap=%08lx comm1_prot=%08lx\n",
           (unsigned long)(unsigned)hr_null_alloc, (unsigned long)(unsigned)hr_cl1_bad,
           (unsigned long)(unsigned)hr_bad_hp, (unsigned long)(unsigned)hr_custom_bad,
           (unsigned long)(unsigned)hr_bad_desc, (unsigned long)(unsigned)hr_placed_null,
           (unsigned long)(unsigned)hr_prot);
  }

  // 6. Private data (SetPrivateData, GetPrivateData, SetPrivateDataInterface, SetName)
  {
    static const GUID kTestGuid = {0x11223344, 0x5566, 0x7788, {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01}};
    static const GUID kNameGuid = {0x429b8c22, 0x9188, 0x4b0c, {0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00}};
    uint32_t payload = 0xdeadbeefu, out = 0;
    UINT sz = 0;
    HRESULT hr_nf = dev->GetPrivateData(kTestGuid, &sz, nullptr);
    HRESULT hr_set = dev->SetPrivateData(kTestGuid, sizeof(payload), &payload);
    sz = 2;
    HRESULT hr_more = dev->GetPrivateData(kTestGuid, &sz, &out);
    UINT sz_after_more = sz;
    HRESULT hr_get = dev->GetPrivateData(kTestGuid, &sz, &out);
    HRESULT hr_del = dev->SetPrivateData(kTestGuid, 0, nullptr);
    HRESULT hr_nf2 = dev->GetPrivateData(kTestGuid, &sz, nullptr);
    HRESULT hr_name = dev->SetName(L"ProbeDev");
    UINT name_sz = 0;
    HRESULT hr_name_sz = dev->GetPrivateData(WKPDID_DebugNameW, &name_sz, nullptr);
    printf("d12d privdata nf=%08lx set=%08lx more=%08lx/%u get=%08lx/%08x del=%08lx nf2=%08lx name=%08lx/%08lx/chars=%u\n",
           (unsigned long)(unsigned)hr_nf, (unsigned long)(unsigned)hr_set,
           (unsigned long)(unsigned)hr_more, sz_after_more,
           (unsigned long)(unsigned)hr_get, out,
           (unsigned long)(unsigned)hr_del, (unsigned long)(unsigned)hr_nf2,
           (unsigned long)(unsigned)hr_name, (unsigned long)(unsigned)hr_name_sz,
           (unsigned)(name_sz / sizeof(WCHAR)));
  }

  // 7. Residency, reserved resources, shared handles, IMTLD3D12DeviceExt
  {
    ID3D12Resource *buf = MakeBuffer(gpu, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
    ID3D12Pageable *pg = buf;
    HRESULT hr_mr = dev->MakeResident(1, &pg);
    HRESULT hr_ev = dev->Evict(1, &pg);
    ID3D12Device3 *dev3 = nullptr;
    CHECK(dev->QueryInterface(__uuidof(ID3D12Device3), (void **)&dev3));
    D3D12_RESIDENCY_PRIORITY prio = D3D12_RESIDENCY_PRIORITY_HIGH;
    HRESULT hr_prio = dev3->SetResidencyPriority(1, &pg, &prio);
    HRESULT hr_enq_null = dev3->EnqueueMakeResident(D3D12_RESIDENCY_FLAG_NONE, 1, &pg, nullptr, 0);
    HRESULT hr_enq_ok = dev3->EnqueueMakeResident(D3D12_RESIDENCY_FLAG_NONE, 1, &pg, gpu.fence, 77);
    UINT64 fval = gpu.fence->GetCompletedValue();
    D3D12_RESOURCE_DESC rdesc = buf->GetDesc();
    ID3D12Resource *rsvd = nullptr;
    HRESULT hr_rsvd = dev->CreateReservedResource(&rdesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                  __uuidof(ID3D12Resource), (void **)&rsvd);
    HANDLE sh = nullptr;
    HRESULT hr_sh_create = dev->CreateSharedHandle(gpu.fence, nullptr, GENERIC_ALL, nullptr, &sh);
    ID3D12Fence *sh_fence = nullptr;
    HRESULT hr_sh_open = dev->OpenSharedHandle(sh, __uuidof(ID3D12Fence), (void **)&sh_fence);
    UINT64 sh_val = sh_fence ? sh_fence->GetCompletedValue() : 0;
    if (sh_fence) sh_fence->Release();
#ifdef _WIN32
    if (sh) CloseHandle(sh);
#endif
    HRESULT hr_sh_name = dev->OpenSharedHandleByName(L"foo", GENERIC_ALL, &sh);
    IMTLD3D12DeviceExt *ext = nullptr;
    CHECK(dev->QueryInterface(IID_IMTLD3D12DeviceExt, (void **)&ext));
    float min_s = 0.0f, max_s = 0.0f;
    ext->GetTemporalScalerScaleRange(&min_s, &max_s);
    MTL_TEMPORAL_SCALER_D3D12_DESC sdesc_bad = {0, 0, 0, 0, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_FLOAT,
                                                DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN,
                                                TRUE, FALSE, FALSE, 0.0f, 0.0f};
    IMTLD3D12TemporalScaler *scaler = nullptr;
    HRESULT hr_sc_bad = ext->CreateTemporalScaler(&sdesc_bad, &scaler);
    MTL_TEMPORAL_SCALER_D3D12_DESC sdesc_ok = {64, 64, 128, 128, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT,
                                               DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_UNKNOWN,
                                               TRUE, FALSE, FALSE, 0.0f, 0.0f};
    HRESULT hr_sc_ok = ext->CreateTemporalScaler(&sdesc_ok, &scaler);
    if (scaler) scaler->Release();
    ext->Release();
    dev3->Release();
    buf->Release();
    printf("d12d residency-shared-ext mr=%08lx ev=%08lx prio=%08lx enq_null=%08lx enq_ok=%08lx/fval=%llu rsvd=%08lx sh=%08lx/%08lx/val=%llu sh_name=%08lx scaler_range=%d sc_bad=%08lx sc_ok=%08lx\n",
           (unsigned long)(unsigned)hr_mr, (unsigned long)(unsigned)hr_ev, (unsigned long)(unsigned)hr_prio,
           (unsigned long)(unsigned)hr_enq_null, (unsigned long)(unsigned)hr_enq_ok, (unsigned long long)fval,
           (unsigned long)(unsigned)hr_rsvd,
           (unsigned long)(unsigned)hr_sh_create, (unsigned long)(unsigned)hr_sh_open, (unsigned long long)sh_val,
           (unsigned long)(unsigned)hr_sh_name,
           (min_s > 0.0f && max_s >= min_s) ? 1 : 0,
           (unsigned long)(unsigned)hr_sc_bad, (unsigned long)(unsigned)hr_sc_ok);
  }
}

// Counting IUnknown for SetPrivateDataInterface and object-lifetime checks.
struct CountingUnknown : IUnknown {
  ULONG refs = 1;
  int *destroyed = nullptr;
  CountingUnknown(int *d) : destroyed(d) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown)) { *ppv = this; AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG r = --refs;
    if (r == 0 && destroyed) (*destroyed)++;
    return r;
  }
};

// --- d12o: Child object COM (14 object kinds), GetDevice, GetDesc, private data, lifetimes ---
static void RunD12o(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;
  static const GUID kLifeGuid = {0xaabbccdd, 0x1122, 0x3344, {0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc}};

  auto check_child = [&](const char *kind, ID3D12DeviceChild *obj, REFIID primary_iid,
                         const IID *extra_iids, size_t n_extra, bool is_pageable) {
    IUnknown *u_base = nullptr, *u_prim = nullptr;
    HRESULT hr_unk = obj->QueryInterface(__uuidof(IUnknown), (void **)&u_base);
    HRESULT hr_obj = E_FAIL, hr_dc = E_FAIL, hr_pg = E_FAIL, hr_prim = E_FAIL, hr_bad = E_FAIL;
    void *tmp = nullptr;
    hr_obj = obj->QueryInterface(__uuidof(ID3D12Object), &tmp);
    if (SUCCEEDED(hr_obj) && tmp) ((IUnknown *)tmp)->Release();
    hr_dc = obj->QueryInterface(__uuidof(ID3D12DeviceChild), &tmp);
    if (SUCCEEDED(hr_dc) && tmp) ((IUnknown *)tmp)->Release();
    hr_pg = obj->QueryInterface(__uuidof(ID3D12Pageable), &tmp);
    if (SUCCEEDED(hr_pg) && tmp) ((IUnknown *)tmp)->Release();
    hr_prim = obj->QueryInterface(primary_iid, (void **)&u_prim);
    bool id_eq = (u_base != nullptr && u_base == u_prim);
    if (u_base) u_base->Release();
    if (u_prim) u_prim->Release();
    hr_bad = obj->QueryInterface(__uuidof(IDXGIDevice), &tmp);

    unsigned extra_mask = 0;
    for (size_t i = 0; i < n_extra; i++) {
      void *ex = nullptr;
      if (SUCCEEDED(obj->QueryInterface(extra_iids[i], &ex)) && ex) {
        extra_mask |= (1u << i);
        ((IUnknown *)ex)->Release();
      }
    }

    ID3D12Device *got_dev = nullptr;
    HRESULT hr_gd = obj->GetDevice(__uuidof(ID3D12Device), (void **)&got_dev);
    bool dev_eq = (got_dev == dev);
    if (got_dev) got_dev->Release();

    int dcount = 0;
    CountingUnknown cu(&dcount);
    HRESULT hr_pdi = obj->SetPrivateDataInterface(kLifeGuid, &cu);
    ULONG refs_after_set = cu.refs;
    HRESULT hr_pdi_clear = obj->SetPrivateData(kLifeGuid, 0, nullptr);
    ULONG refs_after_clear = cu.refs;

    printf("d12o %s unk=%08lx obj=%08lx dc=%08lx pg=%08lx(exp=%d) prim=%08lx id=%d extra=%x bad=%08lx dev=%08lx/%d pdi=%08lx/%lu/%08lx/%lu\n",
           kind, (unsigned long)(unsigned)hr_unk, (unsigned long)(unsigned)hr_obj,
           (unsigned long)(unsigned)hr_dc, (unsigned long)(unsigned)hr_pg, is_pageable ? 1 : 0,
           (unsigned long)(unsigned)hr_prim, id_eq ? 1 : 0, extra_mask,
           (unsigned long)(unsigned)hr_bad,
           (unsigned long)(unsigned)hr_gd, dev_eq ? 1 : 0,
           (unsigned long)(unsigned)hr_pdi, (unsigned long)refs_after_set,
           (unsigned long)(unsigned)hr_pdi_clear, (unsigned long)refs_after_clear);
  };

  // 1. Heap
  D3D12_HEAP_DESC hdesc = {};
  hdesc.SizeInBytes = 65536;
  hdesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  hdesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  ID3D12Heap *heap = nullptr;
  CHECK(dev->CreateHeap(&hdesc, __uuidof(ID3D12Heap), (void **)&heap));
  check_child("heap", heap, __uuidof(ID3D12Heap), nullptr, 0, true);
  {
    D3D12_HEAP_DESC hd_out = {}, *hd_ret = heap->GetDesc(&hd_out);
    printf("d12o heap-desc ptr_eq=%d sz=%llu\n", hd_ret == &hd_out, (unsigned long long)hd_out.SizeInBytes);
  }

  // 2. Buffer & 3. Texture
  ID3D12Resource *buf = MakeBuffer(gpu, 1024, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
  const IID res_extra[] = {__uuidof(ID3D12Resource1), __uuidof(ID3D12Resource2)};
  check_child("buffer", buf, __uuidof(ID3D12Resource), res_extra, 2, true);
  {
    D3D12_RESOURCE_DESC rd_out = {}, *rd_ret = buf->GetDesc(&rd_out);
    D3D12_HEAP_PROPERTIES hp_out = {};
    D3D12_HEAP_FLAGS hf_out = D3D12_HEAP_FLAG_NONE;
    HRESULT hr_hp = buf->GetHeapProperties(&hp_out, &hf_out);
    printf("d12o buffer-desc ptr_eq=%d w=%llu hp=%08lx/%d\n",
           rd_ret == &rd_out, (unsigned long long)rd_out.Width, (unsigned long)(unsigned)hr_hp, (int)hp_out.Type);
  }
  D3D12_RESOURCE_DESC tdesc = {};
  tdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tdesc.Width = 16;
  tdesc.Height = 16;
  tdesc.DepthOrArraySize = 1;
  tdesc.MipLevels = 1;
  tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  tdesc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES hp_def = {D3D12_HEAP_TYPE_DEFAULT};
  ID3D12Resource *tex = nullptr;
  CHECK(dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &tdesc,
                                     D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void **)&tex));
  check_child("texture", tex, __uuidof(ID3D12Resource), res_extra, 2, true);
  {
    D3D12_RESOURCE_DESC rd_out = {}, *rd_ret = tex->GetDesc(&rd_out);
    printf("d12o texture-desc ptr_eq=%d w=%llu h=%u fmt=%u\n",
           rd_ret == &rd_out, (unsigned long long)rd_out.Width, rd_out.Height, (unsigned)rd_out.Format);
  }

  // 4. RootSignature, 5. GraphicsPSO, 6. ComputePSO
  D3D12_ROOT_PARAMETER rp = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_ALL};
  rp.Constants = {0, 0, 4};
  D3D12_ROOT_SIGNATURE_DESC rs_desc = {1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ID3DBlob *rs_blob = nullptr;
  CHECK(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rs_blob, nullptr));
  ID3D12RootSignature *rs = nullptr;
  CHECK(dev->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                 __uuidof(ID3D12RootSignature), (void **)&rs));
  check_child("rootsig", rs, __uuidof(ID3D12RootSignature), nullptr, 0, false);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC gpso_desc = {};
  gpso_desc.pRootSignature = rs;
  gpso_desc.VS = {kQuadVs, sizeof(kQuadVs)};
  gpso_desc.PS = {kSolidPs, sizeof(kSolidPs)};
  gpso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  gpso_desc.SampleMask = UINT_MAX;
  gpso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  gpso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  gpso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  gpso_desc.NumRenderTargets = 1;
  gpso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  gpso_desc.SampleDesc.Count = 1;
  ID3D12PipelineState *gpso = nullptr;
  CHECK(dev->CreateGraphicsPipelineState(&gpso_desc, __uuidof(ID3D12PipelineState), (void **)&gpso));
  check_child("gpso", gpso, __uuidof(ID3D12PipelineState), nullptr, 0, true);

  D3D12_ROOT_PARAMETER crp[2] = {};
  D3D12_DESCRIPTOR_RANGE uav_range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0};
  crp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  crp[0].DescriptorTable = {1, &uav_range};
  crp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_ROOT_SIGNATURE_DESC crs_desc = {1, crp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ID3DBlob *crs_blob = nullptr;
  CHECK(D3D12SerializeRootSignature(&crs_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &crs_blob, nullptr));
  ID3D12RootSignature *crs = nullptr;
  CHECK(dev->CreateRootSignature(0, crs_blob->GetBufferPointer(), crs_blob->GetBufferSize(),
                                 __uuidof(ID3D12RootSignature), (void **)&crs));
  D3D12_COMPUTE_PIPELINE_STATE_DESC cpso_desc = {};
  cpso_desc.pRootSignature = crs;
  cpso_desc.CS = {kComputeCs, sizeof(kComputeCs)};
  ID3D12PipelineState *cpso = nullptr;
  CHECK(dev->CreateComputePipelineState(&cpso_desc, __uuidof(ID3D12PipelineState), (void **)&cpso));
  check_child("cpso", cpso, __uuidof(ID3D12PipelineState), nullptr, 0, true);

  // 7. PipelineLibrary
  ID3D12Device1 *dev1 = nullptr;
  CHECK(dev->QueryInterface(__uuidof(ID3D12Device1), (void **)&dev1));
  ID3D12PipelineLibrary *plib = nullptr;
  CHECK(dev1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), (void **)&plib));
  const IID plib_extra[] = {__uuidof(ID3D12PipelineLibrary1)};
  check_child("pipelinelib", plib, __uuidof(ID3D12PipelineLibrary), plib_extra, 1, false);
  plib->Release();
  dev1->Release();

  // 8. DescriptorHeap
#ifdef __i386__
  D3D12_DESCRIPTOR_HEAP_DESC dh_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
#else
  D3D12_DESCRIPTOR_HEAP_DESC dh_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
#endif
  ID3D12DescriptorHeap *dheap = nullptr;
  CHECK(dev->CreateDescriptorHeap(&dh_desc, __uuidof(ID3D12DescriptorHeap), (void **)&dheap));
  check_child("descheap", dheap, __uuidof(ID3D12DescriptorHeap), nullptr, 0, true);
  {
    D3D12_DESCRIPTOR_HEAP_DESC dhd_out = {}, *dhd_ret = dheap->GetDesc(&dhd_out);
    printf("d12o descheap-desc ptr_eq=%d num=%u flags=%x\n", dhd_ret == &dhd_out, dhd_out.NumDescriptors, (unsigned)dhd_out.Flags);
  }
  dheap->Release();

  // 9. QueryHeap
  D3D12_QUERY_HEAP_DESC qh_desc = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 4, 0};
  ID3D12QueryHeap *qheap = nullptr;
  CHECK(dev->CreateQueryHeap(&qh_desc, __uuidof(ID3D12QueryHeap), (void **)&qheap));
  check_child("queryheap", qheap, __uuidof(ID3D12QueryHeap), nullptr, 0, true);
  qheap->Release();

  // 10. CommandSignature
  D3D12_INDIRECT_ARGUMENT_DESC iarg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
  D3D12_COMMAND_SIGNATURE_DESC cs_desc = {sizeof(D3D12_DRAW_ARGUMENTS), 1, &iarg, 0};
  ID3D12CommandSignature *cmdsig = nullptr;
  CHECK(dev->CreateCommandSignature(&cs_desc, nullptr, __uuidof(ID3D12CommandSignature), (void **)&cmdsig));
  check_child("cmdsig", cmdsig, __uuidof(ID3D12CommandSignature), nullptr, 0, true);
  cmdsig->Release();

  // 11. CommandAllocator, 12. CommandList, 13. CommandQueue, 14. Fence
  check_child("cmdalloc", gpu.allocator, __uuidof(ID3D12CommandAllocator), nullptr, 0, true);
  const IID cl_extra[] = {
    __uuidof(ID3D12GraphicsCommandList),
    __uuidof(ID3D12GraphicsCommandList1),
    __uuidof(ID3D12GraphicsCommandList2),
    __uuidof(ID3D12GraphicsCommandList3),
    __uuidof(ID3D12GraphicsCommandList4),
    __uuidof(ID3D12GraphicsCommandList5),
    __uuidof(ID3D12GraphicsCommandList6),
    __uuidof(ID3D12GraphicsCommandList7),
    IID_IMTLD3D12CommandListExt,
  };
  check_child("cmdlist", gpu.list, __uuidof(ID3D12CommandList), cl_extra, 9, false);
  const IID cq_extra[] = {IID_IMTLSwapChainFactory};
  check_child("cmdqueue", gpu.queue, __uuidof(ID3D12CommandQueue), cq_extra, 1, true);
  {
    D3D12_COMMAND_QUEUE_DESC qd_out = {}, *qd_ret = gpu.queue->GetDesc(&qd_out);
    printf("d12o cmdqueue-desc ptr_eq=%d type=%d\n", qd_ret == &qd_out, (int)qd_out.Type);
  }
  const IID fence_extra[] = {__uuidof(ID3D12Fence1)};
  check_child("fence", gpu.fence, __uuidof(ID3D12Fence), fence_extra, 1, true);

  // 15. Lifetimes: PSO & RootSignature held by command list until Reset; Heap held by placed buffer until buffer release
  {
    int pso_dead = 0, rs_dead = 0, heap_dead = 0;
    CountingUnknown cu_pso(&pso_dead), cu_rs(&rs_dead), cu_heap(&heap_dead);
    CHECK(gpso->SetPrivateDataInterface(kLifeGuid, &cu_pso));
    CHECK(rs->SetPrivateDataInterface(kLifeGuid, &cu_rs));
    CHECK(heap->SetPrivateDataInterface(kLifeGuid, &cu_heap));
    cu_pso.Release();
    cu_rs.Release();
    cu_heap.Release();

    gpu.list->SetPipelineState(gpso);
    gpu.list->SetGraphicsRootSignature(rs);
    gpso->Release();
    rs->Release();
    int pso_before_reset = pso_dead, rs_before_reset = rs_dead;
    CHECK(gpu.list->Close());
    CHECK(gpu.allocator->Reset());
    CHECK(gpu.list->Reset(gpu.allocator, nullptr));
    int pso_after_reset = pso_dead, rs_after_reset = rs_dead;

    D3D12_RESOURCE_DESC pbuf_desc = buf->GetDesc();
    pbuf_desc.Width = 4096;
    ID3D12Resource *placed = nullptr;
    CHECK(dev->CreatePlacedResource(heap, 0, &pbuf_desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                    __uuidof(ID3D12Resource), (void **)&placed));
    heap->Release();
    int heap_while_placed = heap_dead;
    placed->Release();
    int heap_after_placed = heap_dead;

    printf("d12o lifetimes pso=%d->%d rs=%d->%d heap=%d->%d\n",
           pso_before_reset, pso_after_reset, rs_before_reset, rs_after_reset,
           heap_while_placed, heap_after_placed);
  }

  cpso->Release();
  crs->Release();
  crs_blob->Release();
  rs_blob->Release();
  tex->Release();
  buf->Release();
}

// --- d12h: Descriptor heaps, CPU/GPU handle stride, view/sampler creation, CopyDescriptors ---
static void RunD12h(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;

  // RTV and DSV heaps with SHADER_VISIBLE -> E_INVALIDARG
  for (auto ht : {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_TYPE_DSV}) {
    D3D12_DESCRIPTOR_HEAP_DESC bad_desc = {ht, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ID3D12DescriptorHeap *h = nullptr;
    HRESULT hr = dev->CreateDescriptorHeap(&bad_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h);
    printf("d12h shader-visible-err type=%d hr=%08lx\n", (int)ht, (unsigned long)(unsigned)hr);
  }

#ifdef __i386__
  printf("d12h status untested\n");
#else
  D3D12_DESCRIPTOR_HEAP_DESC cbv_sv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  D3D12_DESCRIPTOR_HEAP_DESC cbv_cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  D3D12_DESCRIPTOR_HEAP_DESC samp_sv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  D3D12_DESCRIPTOR_HEAP_DESC samp_cpu_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 4, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  D3D12_DESCRIPTOR_HEAP_DESC rtv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
  D3D12_DESCRIPTOR_HEAP_DESC dsv_desc = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};

  ID3D12DescriptorHeap *h_cbv_sv = nullptr, *h_cbv_cpu = nullptr, *h_samp_sv = nullptr, *h_samp_cpu = nullptr, *h_rtv = nullptr, *h_dsv = nullptr;
  CHECK(dev->CreateDescriptorHeap(&cbv_sv_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_cbv_sv));
  CHECK(dev->CreateDescriptorHeap(&cbv_cpu_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_cbv_cpu));
  CHECK(dev->CreateDescriptorHeap(&samp_sv_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_samp_sv));
  CHECK(dev->CreateDescriptorHeap(&samp_cpu_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_samp_cpu));
  CHECK(dev->CreateDescriptorHeap(&rtv_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_rtv));
  CHECK(dev->CreateDescriptorHeap(&dsv_desc, __uuidof(ID3D12DescriptorHeap), (void **)&h_dsv));

  D3D12_CPU_DESCRIPTOR_HANDLE cpu_out = {}, *cpu_ret = h_cbv_sv->GetCPUDescriptorHandleForHeapStart(&cpu_out);
  D3D12_GPU_DESCRIPTOR_HANDLE gpu_out = {}, *gpu_ret = h_cbv_sv->GetGPUDescriptorHandleForHeapStart(&gpu_out);
  UINT inc_cbv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  UINT inc_samp = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  UINT inc_rtv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  UINT inc_dsv = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
  printf("d12h handles cpu_ptr=%d gpu_ptr=%d cpu_nz=%d gpu_nz=%d inc=%u/%u/%u/%u\n",
         cpu_ret == &cpu_out, gpu_ret == &gpu_out, cpu_out.ptr != 0, gpu_out.ptr != 0,
         inc_cbv, inc_samp, inc_rtv, inc_dsv);

  // Create views & samplers in CPU heaps, then CopyDescriptorsSimple and CopyDescriptors to shader-visible heaps
  ID3D12Resource *buf = MakeBuffer(gpu, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  D3D12_RESOURCE_DESC tdesc = {};
  tdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  tdesc.Width = 16; tdesc.Height = 16; tdesc.DepthOrArraySize = 1; tdesc.MipLevels = 1;
  tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; tdesc.SampleDesc.Count = 1;
  tdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES hp_def = {D3D12_HEAP_TYPE_DEFAULT};
  ID3D12Resource *rt_tex = nullptr, *ds_tex = nullptr;
  CHECK(dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &tdesc, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     nullptr, __uuidof(ID3D12Resource), (void **)&rt_tex));
  tdesc.Format = DXGI_FORMAT_D32_FLOAT;
  tdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  CHECK(dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &tdesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                     nullptr, __uuidof(ID3D12Resource), (void **)&ds_tex));

  D3D12_CPU_DESCRIPTOR_HANDLE c0 = h_cbv_cpu->GetCPUDescriptorHandleForHeapStart();
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_d = {buf->GetGPUVirtualAddress(), 256};
  dev->CreateConstantBufferView(&cbv_d, c0);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_d = {};
  srv_d.Format = DXGI_FORMAT_R32_UINT;
  srv_d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srv_d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_d.Buffer.NumElements = 64;
  dev->CreateShaderResourceView(buf, &srv_d, {c0.ptr + inc_cbv});
  dev->CreateShaderResourceView(nullptr, &srv_d, {c0.ptr + 2 * inc_cbv});
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav_d = {};
  uav_d.Format = DXGI_FORMAT_R32_UINT;
  uav_d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uav_d.Buffer.NumElements = 64;
  dev->CreateUnorderedAccessView(buf, nullptr, &uav_d, {c0.ptr + 3 * inc_cbv});
  dev->CreateUnorderedAccessView(nullptr, nullptr, &uav_d, {c0.ptr + 4 * inc_cbv});

  dev->CopyDescriptorsSimple(3, h_cbv_sv->GetCPUDescriptorHandleForHeapStart(), c0, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE dst_r = {h_cbv_sv->GetCPUDescriptorHandleForHeapStart().ptr + 3 * inc_cbv};
  D3D12_CPU_DESCRIPTOR_HANDLE src_r = {c0.ptr + 3 * inc_cbv};
  UINT cnt_r = 2;
  dev->CopyDescriptors(1, &dst_r, &cnt_r, 1, &src_r, &cnt_r, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_SAMPLER_DESC samp_d = {};
  samp_d.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  samp_d.AddressU = samp_d.AddressV = samp_d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samp_d.MaxLOD = D3D12_FLOAT32_MAX;
  dev->CreateSampler(&samp_d, h_samp_cpu->GetCPUDescriptorHandleForHeapStart());
  dev->CopyDescriptorsSimple(1, h_samp_sv->GetCPUDescriptorHandleForHeapStart(),
                             h_samp_cpu->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

  dev->CreateRenderTargetView(rt_tex, nullptr, h_rtv->GetCPUDescriptorHandleForHeapStart());
  dev->CreateDepthStencilView(ds_tex, nullptr, h_dsv->GetCPUDescriptorHandleForHeapStart());
  printf("d12h views-and-copy status=ok\n");

  ds_tex->Release();
  rt_tex->Release();
  buf->Release();
  h_dsv->Release();
  h_rtv->Release();
  h_samp_cpu->Release();
  h_samp_sv->Release();
  h_cbv_cpu->Release();
  h_cbv_sv->Release();
#endif
}

template <typename T>
struct alignas(void *) StreamSubobj {
  D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
  T data;
};

// --- d12p: Pipeline state streams, cached blobs, and pipeline libraries ---
static void RunD12p(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;
  ID3D12Device2 *dev2 = nullptr;
  CHECK(dev->QueryInterface(__uuidof(ID3D12Device2), (void **)&dev2));

  // Root signatures for graphics and compute
  D3D12_ROOT_PARAMETER rp = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_ALL};
  rp.Constants = {0, 0, 4};
  D3D12_ROOT_SIGNATURE_DESC grs_d = {1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ID3DBlob *grs_b = nullptr;
  CHECK(D3D12SerializeRootSignature(&grs_d, D3D_ROOT_SIGNATURE_VERSION_1_0, &grs_b, nullptr));
  ID3D12RootSignature *grs = nullptr;
  CHECK(dev->CreateRootSignature(0, grs_b->GetBufferPointer(), grs_b->GetBufferSize(),
                                 __uuidof(ID3D12RootSignature), (void **)&grs));

  D3D12_DESCRIPTOR_RANGE uav_r = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 0};
  D3D12_ROOT_PARAMETER crp = {D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, {}, D3D12_SHADER_VISIBILITY_ALL};
  crp.DescriptorTable = {1, &uav_r};
  D3D12_ROOT_SIGNATURE_DESC crs_d = {1, &crp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
  ID3DBlob *crs_b = nullptr;
  CHECK(D3D12SerializeRootSignature(&crs_d, D3D_ROOT_SIGNATURE_VERSION_1_0, &crs_b, nullptr));
  ID3D12RootSignature *crs = nullptr;
  CHECK(dev->CreateRootSignature(0, crs_b->GetBufferPointer(), crs_b->GetBufferSize(),
                                 __uuidof(ID3D12RootSignature), (void **)&crs));

  // 1. Graphics stream and Compute stream via ID3D12Device2::CreatePipelineState
  struct GfxStream {
    StreamSubobj<ID3D12RootSignature *> rs;
    StreamSubobj<D3D12_SHADER_BYTECODE> vs;
    StreamSubobj<D3D12_SHADER_BYTECODE> ps;
    StreamSubobj<D3D12_PRIMITIVE_TOPOLOGY_TYPE> topo;
    StreamSubobj<D3D12_RT_FORMAT_ARRAY> rtv;
  } gs = {
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, grs},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, {kQuadVs, sizeof(kQuadVs)}},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, {kSolidPs, sizeof(kSolidPs)}},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, {{DXGI_FORMAT_R8G8B8A8_UNORM}, 1}},
  };
  D3D12_PIPELINE_STATE_STREAM_DESC gs_desc = {sizeof(gs), &gs};
  ID3D12PipelineState *gpso = nullptr;
  HRESULT hr_gs = dev2->CreatePipelineState(&gs_desc, __uuidof(ID3D12PipelineState), (void **)&gpso);

  struct CompStream {
    StreamSubobj<ID3D12RootSignature *> rs;
    StreamSubobj<D3D12_SHADER_BYTECODE> cs;
  } cs = {
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, crs},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, {kComputeCs, sizeof(kComputeCs)}},
  };
  D3D12_PIPELINE_STATE_STREAM_DESC cs_desc = {sizeof(cs), &cs};
  ID3D12PipelineState *cpso = nullptr;
  HRESULT hr_cs = dev2->CreatePipelineState(&cs_desc, __uuidof(ID3D12PipelineState), (void **)&cpso);

  // Error cases: duplicate subobject token, unknown token, truncated stream
  struct DupStream {
    StreamSubobj<ID3D12RootSignature *> rs1;
    StreamSubobj<ID3D12RootSignature *> rs2;
  } dup_s = {
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, crs},
    {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, crs},
  };
  D3D12_PIPELINE_STATE_STREAM_DESC dup_desc = {sizeof(dup_s), &dup_s};
  ID3D12PipelineState *bad_pso = nullptr;
  HRESULT hr_dup = dev2->CreatePipelineState(&dup_desc, __uuidof(ID3D12PipelineState), (void **)&bad_pso);

  StreamSubobj<UINT> unk_s = {(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)999, 0};
  D3D12_PIPELINE_STATE_STREAM_DESC unk_desc = {sizeof(unk_s), &unk_s};
  HRESULT hr_unk = dev2->CreatePipelineState(&unk_desc, __uuidof(ID3D12PipelineState), (void **)&bad_pso);

  D3D12_PIPELINE_STATE_STREAM_DESC trunc_desc = {sizeof(cs) - 4, &cs};
  HRESULT hr_trunc = dev2->CreatePipelineState(&trunc_desc, __uuidof(ID3D12PipelineState), (void **)&bad_pso);

  StreamSubobj<D3D12_SHADER_BYTECODE> as_s = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, {kQuadVs, sizeof(kQuadVs)}};
  D3D12_PIPELINE_STATE_STREAM_DESC as_desc = {sizeof(as_s), &as_s};
  HRESULT hr_as = dev2->CreatePipelineState(&as_desc, __uuidof(ID3D12PipelineState), (void **)&bad_pso);

  StreamSubobj<D3D12_SHADER_BYTECODE> ms_s = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, {kQuadVs, sizeof(kQuadVs)}};
  D3D12_PIPELINE_STATE_STREAM_DESC ms_desc = {sizeof(ms_s), &ms_s};
  HRESULT hr_ms = dev2->CreatePipelineState(&ms_desc, __uuidof(ID3D12PipelineState), (void **)&bad_pso);

  // GetCachedBlob on graphics and compute PSO
  ID3DBlob *gcache = nullptr, *ccache = nullptr;
  HRESULT hr_gcache = gpso->GetCachedBlob(&gcache);
  HRESULT hr_ccache = cpso->GetCachedBlob(&ccache);
  HRESULT hr_null_cache = gpso->GetCachedBlob(nullptr);
  printf("d12p stream-and-cache gs=%08lx cs=%08lx dup=%08lx unk=%08lx trunc=%08lx as=%08lx ms=%08lx gcache=%08lx/%zu ccache=%08lx/%zu null_cache=%08lx\n",
         (unsigned long)(unsigned)hr_gs, (unsigned long)(unsigned)hr_cs,
         (unsigned long)(unsigned)hr_dup, (unsigned long)(unsigned)hr_unk, (unsigned long)(unsigned)hr_trunc,
         (unsigned long)(unsigned)hr_as, (unsigned long)(unsigned)hr_ms,
         (unsigned long)(unsigned)hr_gcache, gcache ? (size_t)gcache->GetBufferSize() : 99u,
         (unsigned long)(unsigned)hr_ccache, ccache ? (size_t)ccache->GetBufferSize() : 99u,
         (unsigned long)(unsigned)hr_null_cache);
  if (gcache) gcache->Release();
  if (ccache) ccache->Release();

  // 2. Pipeline Library: empty create, StorePipeline, LoadGraphicsPipeline, LoadComputePipeline, LoadPipeline,
  // duplicate StorePipeline, LoadGraphicsPipeline mismatch, Serialize & round-trip into second library, bad magic & truncated
  ID3D12PipelineLibrary1 *lib1 = nullptr;
  HRESULT hr_lib1 = dev2->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary1), (void **)&lib1);
  SIZE_T sz_empty = lib1->GetSerializedSize();
  HRESULT hr_store_g = lib1->StorePipeline(L"gfx", gpso);
  HRESULT hr_store_c = lib1->StorePipeline(L"cmp", cpso);
  HRESULT hr_store_dup = lib1->StorePipeline(L"gfx", gpso);
  HRESULT hr_store_null = lib1->StorePipeline(nullptr, gpso);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC gdesc = {};
  gdesc.pRootSignature = grs;
  gdesc.VS = {kQuadVs, sizeof(kQuadVs)};
  gdesc.PS = {kSolidPs, sizeof(kSolidPs)};
  gdesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  gdesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
  gdesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  gdesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  gdesc.RasterizerState.DepthClipEnable = TRUE;
  gdesc.DepthStencilState.DepthEnable = TRUE;
  gdesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  gdesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  gdesc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
  gdesc.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
  gdesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  gdesc.NumRenderTargets = 1;
  gdesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  gdesc.SampleDesc.Count = 1;

  D3D12_COMPUTE_PIPELINE_STATE_DESC cdesc = {};
  cdesc.pRootSignature = crs;
  cdesc.CS = {kComputeCs, sizeof(kComputeCs)};

  ID3D12PipelineState *lg = nullptr, *lc = nullptr, *ls = nullptr, *lbad = nullptr;
  HRESULT hr_lg = lib1->LoadGraphicsPipeline(L"gfx", &gdesc, __uuidof(ID3D12PipelineState), (void **)&lg);
  HRESULT hr_lc = lib1->LoadComputePipeline(L"cmp", &cdesc, __uuidof(ID3D12PipelineState), (void **)&lc);
  HRESULT hr_ls = lib1->LoadPipeline(L"cmp", &cs_desc, __uuidof(ID3D12PipelineState), (void **)&ls);
  HRESULT hr_lmis = lib1->LoadGraphicsPipeline(L"cmp", &gdesc, __uuidof(ID3D12PipelineState), (void **)&lbad);
  HRESULT hr_lnf = lib1->LoadGraphicsPipeline(L"missing", &gdesc, __uuidof(ID3D12PipelineState), (void **)&lbad);
  if (lg) lg->Release();
  if (lc) lc->Release();
  if (ls) ls->Release();

  SIZE_T sz_full = lib1->GetSerializedSize();
  // Normalize name character count across 2-byte PE WCHAR and 4-byte native WCHAR: 16 header + 2 * (4 + 8) + 6 * sizeof(WCHAR)
  size_t norm_chars = (sz_full - 16 - 24) / sizeof(WCHAR);
  std::vector<uint8_t> sbuf(sz_full);
  HRESULT hr_ser_small = lib1->Serialize(sbuf.data(), sz_full - 1);
  HRESULT hr_ser_ok = lib1->Serialize(sbuf.data(), sz_full);

  ID3D12PipelineLibrary1 *lib2 = nullptr;
  HRESULT hr_lib2 = dev2->CreatePipelineLibrary(sbuf.data(), sbuf.size(), __uuidof(ID3D12PipelineLibrary1), (void **)&lib2);
  ID3D12PipelineState *lg2 = nullptr, *lc2 = nullptr;
  HRESULT hr_lg2 = lib2 ? lib2->LoadGraphicsPipeline(L"gfx", &gdesc, __uuidof(ID3D12PipelineState), (void **)&lg2) : E_FAIL;
  HRESULT hr_lc2 = lib2 ? lib2->LoadPipeline(L"cmp", &cs_desc, __uuidof(ID3D12PipelineState), (void **)&lc2) : E_FAIL;
  if (lg2) lg2->Release();
  if (lc2) lc2->Release();
  if (lib2) lib2->Release();

  HRESULT hr_trunc_lib = dev2->CreatePipelineLibrary(sbuf.data(), sbuf.size() - 2, __uuidof(ID3D12PipelineLibrary1), (void **)&lib2);
  sbuf[0] ^= 0xff;
  HRESULT hr_bad_magic = dev2->CreatePipelineLibrary(sbuf.data(), sbuf.size(), __uuidof(ID3D12PipelineLibrary1), (void **)&lib2);

  printf("d12p library create=%08lx empty_sz=%zu store=%08lx/%08lx/dup=%08lx/null=%08lx load=%08lx/%08lx/%08lx/mis=%08lx/nf=%08lx chars=%zu ser=%08lx/%08lx rt=%08lx/%08lx/%08lx trunc=%08lx magic=%08lx\n",
         (unsigned long)(unsigned)hr_lib1, (size_t)sz_empty,
         (unsigned long)(unsigned)hr_store_g, (unsigned long)(unsigned)hr_store_c,
         (unsigned long)(unsigned)hr_store_dup, (unsigned long)(unsigned)hr_store_null,
         (unsigned long)(unsigned)hr_lg, (unsigned long)(unsigned)hr_lc, (unsigned long)(unsigned)hr_ls,
         (unsigned long)(unsigned)hr_lmis, (unsigned long)(unsigned)hr_lnf,
         norm_chars,
         (unsigned long)(unsigned)hr_ser_small, (unsigned long)(unsigned)hr_ser_ok,
         (unsigned long)(unsigned)hr_lib2, (unsigned long)(unsigned)hr_lg2, (unsigned long)(unsigned)hr_lc2,
         (unsigned long)(unsigned)hr_trunc_lib, (unsigned long)(unsigned)hr_bad_magic);

  lib1->Release();
  cpso->Release();
  gpso->Release();
  crs->Release();
  crs_b->Release();
  grs->Release();
  grs_b->Release();
  dev2->Release();
}

// --- d12l: Command allocator, command list state machine, command queue clocks, fences ---
static void RunD12l(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;

  // 1. CommandList GetType, Close twice -> E_FAIL, Reset while recording, allocator->Reset
  D3D12_COMMAND_LIST_TYPE ltype = gpu.list->GetType();
  HRESULT hr_close1 = gpu.list->Close();
  HRESULT hr_close2 = gpu.list->Close();
  HRESULT hr_alloc_reset = gpu.allocator->Reset();
  HRESULT hr_reset1 = gpu.list->Reset(gpu.allocator, nullptr);
  HRESULT hr_reset_while_open = gpu.list->Reset(gpu.allocator, nullptr);
  printf("d12l list-state type=%d close1=%08lx close2=%08lx alloc_reset=%08lx reset1=%08lx reset_open=%08lx\n",
         (int)ltype, (unsigned long)(unsigned)hr_close1, (unsigned long)(unsigned)hr_close2,
         (unsigned long)(unsigned)hr_alloc_reset, (unsigned long)(unsigned)hr_reset1,
         (unsigned long)(unsigned)hr_reset_while_open);

  // 2. Queue timestamp frequency and clock calibration bracketed by QueryPerformanceCounter
  UINT64 freq = 0;
  HRESULT hr_freq_null = gpu.queue->GetTimestampFrequency(nullptr);
  HRESULT hr_freq = gpu.queue->GetTimestampFrequency(&freq);
  LARGE_INTEGER qpc0 = {}, qpc1 = {};
  QueryPerformanceCounter(&qpc0);
  UINT64 gpu_ts = 0, cpu_ts = 0;
  HRESULT hr_cal_null = gpu.queue->GetClockCalibration(nullptr, &cpu_ts);
  HRESULT hr_cal = gpu.queue->GetClockCalibration(&gpu_ts, &cpu_ts);
  QueryPerformanceCounter(&qpc1);
  bool bracket = (cpu_ts >= (UINT64)qpc0.QuadPart && cpu_ts <= (UINT64)qpc1.QuadPart && gpu_ts > 0);
  printf("d12l queue-clocks freq_null=%08lx freq=%08lx/gt0=%d cal_null=%08lx cal=%08lx/bracket=%d",
         (unsigned long)(unsigned)hr_freq_null, (unsigned long)(unsigned)hr_freq, freq > 0 ? 1 : 0,
         (unsigned long)(unsigned)hr_cal_null, (unsigned long)(unsigned)hr_cal, bracket ? 1 : 0);
  if (g_full)
    printf(" freq_val=%llu gpu_ts=%llu cpu_ts=%llu", (unsigned long long)freq,
           (unsigned long long)gpu_ts, (unsigned long long)cpu_ts);
  printf("\n");

  // 3. Fences: CPU Signal/GetCompletedValue, GPU queue->Signal/queue->Wait, SetEventOnCompletion,
  // SetEventOnMultipleFenceCompletion (ALL & ANY), IMTLD3D12SharedFence + CreateSharedHandle/OpenSharedHandle
  ID3D12Fence1 *f1 = nullptr, *f2 = nullptr;
  CHECK(dev->CreateFence(10, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), (void **)&f1));
  CHECK(dev->CreateFence(20, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence1), (void **)&f2));
  D3D12_FENCE_FLAGS ff1 = f1->GetCreationFlags(), ff2 = f2->GetCreationFlags();
  UINT64 v0 = f1->GetCompletedValue();
  HRESULT hr_cpu_sig = f1->Signal(15);
  UINT64 v1 = f1->GetCompletedValue();
  HRESULT hr_wait_null = f1->SetEventOnCompletion(15, nullptr);

  HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  CHECK(gpu.queue->Signal(f1, 25));
  CHECK(gpu.queue->Wait(f1, 25));
  CHECK(gpu.queue->Signal(f2, 30));
  HRESULT hr_ev = f2->SetEventOnCompletion(30, ev);
  DWORD wres = WaitForSingleObject(ev, 5000);
  UINT64 v1_gpu = f1->GetCompletedValue(), v2_gpu = f2->GetCompletedValue();

  ID3D12Device1 *dev1 = nullptr;
  CHECK(dev->QueryInterface(__uuidof(ID3D12Device1), (void **)&dev1));
  ID3D12Fence *fpair[2] = {f1, f2};
  UINT64 v_all[2] = {25, 30};
  HRESULT hr_m_zero = dev1->SetEventOnMultipleFenceCompletion(nullptr, nullptr, 0, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
  HRESULT hr_m_bad = dev1->SetEventOnMultipleFenceCompletion(nullptr, v_all, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
  HRESULT hr_m_all = dev1->SetEventOnMultipleFenceCompletion(fpair, v_all, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
  UINT64 v_any[2] = {25, 9999};
  HRESULT hr_m_any = dev1->SetEventOnMultipleFenceCompletion(fpair, v_any, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, ev);
  DWORD wres_any = WaitForSingleObject(ev, 5000);

  HANDLE sh_h = nullptr;
  HRESULT hr_sf = dev->CreateSharedHandle(f2, nullptr, GENERIC_ALL, nullptr, &sh_h);
  ID3D12Fence *sf = nullptr;
  HRESULT hr_sf_open = dev->OpenSharedHandle(sh_h, __uuidof(ID3D12Fence), (void **)&sf);
  UINT64 sf_val = sf ? sf->GetCompletedValue() : 0;
  if (sf) sf->Release();
  if (sh_h) CloseHandle(sh_h);

  printf("d12l fences flags=%x/%x v=%llu->%llu->%llu/%llu cpu_sig=%08lx wait_null=%08lx ev=%08lx/%lu m=%08lx/%08lx/%08lx/%08lx/%lu sf=%08lx/%08lx/%llu\n",
         (unsigned)ff1, (unsigned)ff2,
         (unsigned long long)v0, (unsigned long long)v1, (unsigned long long)v1_gpu, (unsigned long long)v2_gpu,
         (unsigned long)(unsigned)hr_cpu_sig, (unsigned long)(unsigned)hr_wait_null,
         (unsigned long)(unsigned)hr_ev, (unsigned long)wres,
         (unsigned long)(unsigned)hr_m_zero, (unsigned long)(unsigned)hr_m_bad,
         (unsigned long)(unsigned)hr_m_all, (unsigned long)(unsigned)hr_m_any, (unsigned long)wres_any,
         (unsigned long)(unsigned)hr_sf, (unsigned long)(unsigned)hr_sf_open, (unsigned long long)sf_val);

#ifdef _WIN32
  if (ev) CloseHandle(ev);
#endif
  dev1->Release();
  f2->Release();
  f1->Release();
}

// --- d12s: SwapChain (CreateSwapChainForHwnd, QI, BackBuffer rotation, ResizeBuffers, WaitableObject, HDR/ColorSpace) ---
static void RunD12s(Gpu &gpu) {
#ifdef _WIN32
  WNDCLASSA wc = {};
  wc.lpfnWndProc = DefWindowProcA;
  wc.lpszClassName = "D3D12ProbeWnd";
  RegisterClassA(&wc);
  HWND hwnd = CreateWindowExA(0, "D3D12ProbeWnd", "D3D12Probe", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
#else
  HWND hwnd = (HWND)(uintptr_t)0x4242;
#endif

  DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
  sc_desc.Width = 64;
  sc_desc.Height = 64;
  sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sc_desc.SampleDesc.Count = 1;
  sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sc_desc.BufferCount = 2;
  sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sc_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  sc_desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

  IDXGIFactory2 *fac = nullptr;
  CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void **)&fac));
  IDXGISwapChain1 *sc1 = nullptr;
  CHECK(fac->CreateSwapChainForHwnd(gpu.queue, hwnd, &sc_desc, nullptr, nullptr, &sc1));
  fac->Release();
  IDXGISwapChain4 *sc4 = nullptr;
  CHECK(sc1->QueryInterface(__uuidof(IDXGISwapChain4), (void **)&sc4));

  // QI checks
  const IID sc_iids[] = {
    __uuidof(IDXGIObject), __uuidof(IDXGIDeviceSubObject),
    __uuidof(IDXGISwapChain), __uuidof(IDXGISwapChain1),
    __uuidof(IDXGISwapChain2), __uuidof(IDXGISwapChain3), __uuidof(IDXGISwapChain4),
  };
  unsigned sc_qi_mask = 0;
  for (size_t i = 0; i < 7; i++) {
    void *p = nullptr;
    if (SUCCEEDED(sc4->QueryInterface(sc_iids[i], &p)) && p) {
      sc_qi_mask |= (1u << i);
      ((IUnknown *)p)->Release();
    }
  }

  ID3D12Device *got_dev = nullptr;
  HRESULT hr_gd = sc4->GetDevice(__uuidof(ID3D12Device), (void **)&got_dev);
  bool q_eq = (got_dev == gpu.device);
  if (got_dev) got_dev->Release();

  IDXGIFactory1 *got_f = nullptr;
  HRESULT hr_gp = sc4->GetParent(__uuidof(IDXGIFactory1), (void **)&got_f);
  if (got_f) got_f->Release();

  IDXGIOutput *got_out = nullptr;
  HRESULT hr_out = sc4->GetContainingOutput(&got_out);
  if (got_out) got_out->Release();

  DXGI_SWAP_CHAIN_DESC d0 = {};
  DXGI_SWAP_CHAIN_DESC1 d1 = {};
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC df = {};
  HWND got_hwnd = nullptr;
  void *core_win = nullptr;
  HRESULT hr_d0 = sc4->GetDesc(&d0);
  HRESULT hr_d1 = sc4->GetDesc1(&d1);
  HRESULT hr_df = sc4->GetFullscreenDesc(&df);
  HRESULT hr_hwnd = sc4->GetHwnd(&got_hwnd);
  HRESULT hr_core = sc4->GetCoreWindow(__uuidof(IUnknown), &core_win);
  BOOL mono = sc4->IsTemporaryMonoSupported();
  printf("d12s meta qi=%x dev=%08lx/%d parent=%08lx out=%08lx d0=%08lx/%ux%u d1=%08lx/%ux%u df=%08lx/%d hwnd=%08lx/%d core=%08lx mono=%d\n",
         sc_qi_mask, (unsigned long)(unsigned)hr_gd, q_eq ? 1 : 0,
         (unsigned long)(unsigned)hr_gp, (unsigned long)(unsigned)hr_out,
         (unsigned long)(unsigned)hr_d0, d0.BufferDesc.Width, d0.BufferDesc.Height,
         (unsigned long)(unsigned)hr_d1, d1.Width, d1.Height,
         (unsigned long)(unsigned)hr_df, df.Windowed ? 1 : 0,
         (unsigned long)(unsigned)hr_hwnd, got_hwnd == hwnd ? 1 : 0,
         (unsigned long)(unsigned)hr_core, (int)mono);

  // GetBuffer(0/1/2), GetCurrentBackBufferIndex rotating across Present(0, 0), GetFrameStatistics, GetLastPresentCount
  ID3D12Resource *bb0 = nullptr, *bb1 = nullptr, *bb2 = nullptr;
  HRESULT hr_b0 = sc4->GetBuffer(0, __uuidof(ID3D12Resource), (void **)&bb0);
  HRESULT hr_b1 = sc4->GetBuffer(1, __uuidof(ID3D12Resource), (void **)&bb1);
  HRESULT hr_b2 = sc4->GetBuffer(2, __uuidof(ID3D12Resource), (void **)&bb2);
  if (bb0) bb0->Release();
  if (bb1) bb1->Release();
  UINT idx0 = sc4->GetCurrentBackBufferIndex();
  HRESULT hr_p1 = sc4->Present(0, 0);
  UINT idx1 = sc4->GetCurrentBackBufferIndex();
  HRESULT hr_p2 = sc4->Present(0, 0);
  UINT idx2 = sc4->GetCurrentBackBufferIndex();
  DXGI_FRAME_STATISTICS fstats = {};
  HRESULT hr_fs = sc4->GetFrameStatistics(&fstats);
  UINT last_pc = 0;
  HRESULT hr_lpc = sc4->GetLastPresentCount(&last_pc);
  printf("d12s present b0=%08lx b1=%08lx b2=%08lx idx=%u->%u->%u p=%08lx/%08lx fs=%08lx lpc=%08lx/%u\n",
         (unsigned long)(unsigned)hr_b0, (unsigned long)(unsigned)hr_b1, (unsigned long)(unsigned)hr_b2,
         idx0, idx1, idx2, (unsigned long)(unsigned)hr_p1, (unsigned long)(unsigned)hr_p2,
         (unsigned long)(unsigned)hr_fs, (unsigned long)(unsigned)hr_lpc, last_pc);

  // ResizeBuffers, WaitableObject, MaximumFrameLatency, MatrixTransform, ColorSpace, HDRMetaData
  HRESULT hr_resize = sc4->ResizeBuffers(2, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
  DXGI_SWAP_CHAIN_DESC1 d1_after = {};
  CHECK(sc4->GetDesc1(&d1_after));
  HANDLE wait_obj = sc4->GetFrameLatencyWaitableObject();
#ifdef _WIN32
  if (wait_obj) CloseHandle(wait_obj);
#endif
  HRESULT hr_lat_set = sc4->SetMaximumFrameLatency(2);
  UINT max_lat = 0;
  HRESULT hr_lat_get = sc4->GetMaximumFrameLatency(&max_lat);
  DXGI_MATRIX_3X2_F mat = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
  HRESULT hr_mat_set = sc4->SetMatrixTransform(&mat);
  HRESULT hr_mat_get = sc4->GetMatrixTransform(&mat);
  UINT cs_sup = 0;
  HRESULT hr_cs_chk = sc4->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &cs_sup);
  HRESULT hr_cs_set = sc4->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
  DXGI_HDR_METADATA_HDR10 hdr10 = {};
  HRESULT hr_hdr = sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10), &hdr10);
  printf("d12s resize-and-props resize=%08lx/%ux%u wait_nz=%d lat=%08lx/%08lx/%u mat=%08lx/%08lx cs=%08lx/%x/%08lx hdr=%08lx\n",
         (unsigned long)(unsigned)hr_resize, d1_after.Width, d1_after.Height,
         wait_obj != nullptr ? 1 : 0,
         (unsigned long)(unsigned)hr_lat_set, (unsigned long)(unsigned)hr_lat_get, max_lat,
         (unsigned long)(unsigned)hr_mat_set, (unsigned long)(unsigned)hr_mat_get,
         (unsigned long)(unsigned)hr_cs_chk, cs_sup, (unsigned long)(unsigned)hr_cs_set,
         (unsigned long)(unsigned)hr_hdr);

  sc4->Release();
  sc1->Release();
#ifdef _WIN32
  DestroyWindow(hwnd);
#endif
}

// --- d12c: Uncalled implemented COM slots census + OMSetBlendFactor & RenderPass draw/readback ---
static void RunD12c(Gpu &gpu) {
  ID3D12Device *dev = gpu.device;

  // Group 1: Buffer/Texture WriteToSubresource & ReadFromSubresource, Texture::GetGPUVirtualAddress,
  // Resource::GetProtectedResourceSession, Resource::GetDesc1
  {
    ID3D12Resource *buf = MakeBuffer(gpu, 64, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint32_t dummy = 0;
    HRESULT hr_bw = buf->WriteToSubresource(0, nullptr, &dummy, 4, 4);
    HRESULT hr_br = buf->ReadFromSubresource(&dummy, 4, 4, 0, nullptr);
    ID3D12Resource2 *res2 = nullptr;
    CHECK(buf->QueryInterface(__uuidof(ID3D12Resource2), (void **)&res2));
    void *psess = nullptr;
    HRESULT hr_psess = res2->GetProtectedResourceSession(__uuidof(IUnknown), &psess);
    D3D12_RESOURCE_DESC1 rd1_out = {}, *rd1_ret = res2->GetDesc1(&rd1_out);

    D3D12_HEAP_PROPERTIES hp_cpu = dev->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC tdesc = {};
    tdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tdesc.Width = 4; tdesc.Height = 4; tdesc.DepthOrArraySize = 1; tdesc.MipLevels = 1;
    tdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; tdesc.SampleDesc.Count = 1;
    tdesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Resource *tex = nullptr;
    CHECK(dev->CreateCommittedResource(&hp_cpu, D3D12_HEAP_FLAG_NONE, &tdesc, D3D12_RESOURCE_STATE_COMMON,
                                       nullptr, __uuidof(ID3D12Resource), (void **)&tex));
    D3D12_GPU_VIRTUAL_ADDRESS t_va = tex->GetGPUVirtualAddress();
    uint32_t src_px[16], dst_px[16] = {};
    for (int i = 0; i < 16; i++) src_px[i] = 0x11223300u + (uint32_t)i;
    HRESULT hr_tw = tex->WriteToSubresource(0, nullptr, src_px, 16, 64);
    HRESULT hr_tr = tex->ReadFromSubresource(dst_px, 16, 64, 0, nullptr);
    bool px_ok = (memcmp(src_px, dst_px, sizeof(src_px)) == 0);
    printf("d12c group1-res bw=%08lx br=%08lx psess=%08lx d1_ptr=%d/w=%llu t_va=%llu tw=%08lx tr=%08lx px_ok=%d\n",
           (unsigned long)(unsigned)hr_bw, (unsigned long)(unsigned)hr_br,
           (unsigned long)(unsigned)hr_psess, rd1_ret == &rd1_out, (unsigned long long)rd1_out.Width,
           (unsigned long long)t_va, (unsigned long)(unsigned)hr_tw, (unsigned long)(unsigned)hr_tr, px_ok ? 1 : 0);
    tex->Release();
    res2->Release();
    buf->Release();
  }

  // Group 2: Device5-10 non-resource & resource creation slots
  {
    ID3D12Device10 *dev10 = nullptr;
    CHECK(dev->QueryInterface(__uuidof(ID3D12Device10), (void **)&dev10));
    HRESULT hr_stable = dev10->SetStablePowerState(FALSE);
    HRESULT hr_fmap = dev10->OpenExistingHeapFromFileMapping(nullptr, __uuidof(ID3D12Heap), nullptr);
    void *tmp = nullptr;
    HRESULT hr_prs = dev10->CreateProtectedResourceSession(nullptr, __uuidof(IUnknown), &tmp);
    HRESULT hr_lt = dev10->CreateLifetimeTracker(nullptr, __uuidof(IUnknown), &tmp);
    dev10->RemoveDevice();
    UINT mc_cnt = 99;
    HRESULT hr_emc = dev10->EnumerateMetaCommands(&mc_cnt, nullptr);
    GUID dummy_guid = {};
    HRESULT hr_emcp = dev10->EnumerateMetaCommandParameters(dummy_guid, D3D12_META_COMMAND_PARAMETER_STAGE_EXECUTION, nullptr, &mc_cnt, nullptr);
    HRESULT hr_cmc = dev10->CreateMetaCommand(dummy_guid, 0, nullptr, 0, __uuidof(IUnknown), &tmp);
    HRESULT hr_so = dev10->CreateStateObject(nullptr, __uuidof(IUnknown), &tmp);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO rt_info = {9, 9, 9};
    dev10->GetRaytracingAccelerationStructurePrebuildInfo(nullptr, &rt_info);
    BOOL further = TRUE;
    HRESULT hr_bg = dev10->SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE_ALLOWED,
                                                       D3D12_MEASUREMENTS_ACTION_KEEP_ALL, nullptr, &further);
    HRESULT hr_add_so = dev10->AddToStateObject(nullptr, nullptr, __uuidof(IUnknown), &tmp);
    HRESULT hr_prs1 = dev10->CreateProtectedResourceSession1(nullptr, __uuidof(IUnknown), &tmp);
    D3D12_RESOURCE_DESC1 rd1 = {};
    rd1.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd1.Width = 256; rd1.Height = 1; rd1.DepthOrArraySize = 1; rd1.MipLevels = 1;
    rd1.SampleDesc.Count = 1; rd1.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RESOURCE_ALLOCATION_INFO1 ai1 = {};
    D3D12_RESOURCE_ALLOCATION_INFO ai_out = {}, *ai_ret = dev10->GetResourceAllocationInfo2(&ai_out, 0, 1, &rd1, &ai1);
    D3D12_HEAP_PROPERTIES hp_def = {D3D12_HEAP_TYPE_DEFAULT};
    ID3D12Resource *cr3 = nullptr, *pr2 = nullptr;
    HRESULT hr_cr3 = dev10->CreateCommittedResource3(&hp_def, D3D12_HEAP_FLAG_NONE, &rd1, D3D12_BARRIER_LAYOUT_UNDEFINED,
                                                     nullptr, nullptr, 0, nullptr, __uuidof(ID3D12Resource), (void **)&cr3);
    D3D12_HEAP_DESC hd = {65536, hp_def, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, D3D12_HEAP_FLAG_NONE};
    ID3D12Heap *h1 = nullptr;
    HRESULT hr_h1 = dev10->CreateHeap1(&hd, nullptr, __uuidof(ID3D12Heap), (void **)&h1);
    HRESULT hr_pr2 = dev10->CreatePlacedResource2(h1, 0, &rd1, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr,
                                                  __uuidof(ID3D12Resource), (void **)&pr2);
    D3D12_RESOURCE_DESC rdesc_plain = cr3->GetDesc();
    HRESULT hr_rsvd1 = dev10->CreateReservedResource1(&rdesc_plain, D3D12_RESOURCE_STATE_COMMON, nullptr, nullptr, __uuidof(ID3D12Resource), &tmp);
    HRESULT hr_scs = dev10->CreateShaderCacheSession(nullptr, __uuidof(IUnknown), &tmp);
    HRESULT hr_scc = dev10->ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CACHE_FOR_DRIVER, D3D12_SHADER_CACHE_CONTROL_FLAG_ENABLE);
    HRESULT hr_rsvd2 = dev10->CreateReservedResource2(&rdesc_plain, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, nullptr, 0, nullptr,
                                                      __uuidof(ID3D12Resource), &tmp);
    if (pr2) pr2->Release();
    if (h1) h1->Release();
    if (cr3) cr3->Release();
    dev10->Release();
    printf("d12c group2-dev stable=%08lx fmap=%08lx prs=%08lx lt=%08lx emc=%08lx/%u emcp=%08lx cmc=%08lx so=%08lx rt=%llu bg=%08lx/%d add_so=%08lx prs1=%08lx ai2_ptr=%d rsvd1=%08lx scs=%08lx scc=%08lx cr3=%08lx h1=%08lx pr2=%08lx rsvd2=%08lx\n",
           (unsigned long)(unsigned)hr_stable, (unsigned long)(unsigned)hr_fmap,
           (unsigned long)(unsigned)hr_prs, (unsigned long)(unsigned)hr_lt,
           (unsigned long)(unsigned)hr_emc, mc_cnt, (unsigned long)(unsigned)hr_emcp,
           (unsigned long)(unsigned)hr_cmc, (unsigned long)(unsigned)hr_so,
           (unsigned long long)rt_info.ResultDataMaxSizeInBytes,
           (unsigned long)(unsigned)hr_bg, (int)further,
           (unsigned long)(unsigned)hr_add_so, (unsigned long)(unsigned)hr_prs1,
           ai_ret == &ai_out, (unsigned long)(unsigned)hr_rsvd1,
           (unsigned long)(unsigned)hr_scs, (unsigned long)(unsigned)hr_scc,
           (unsigned long)(unsigned)hr_cr3, (unsigned long)(unsigned)hr_h1,
           (unsigned long)(unsigned)hr_pr2, (unsigned long)(unsigned)hr_rsvd2);
  }

  // Group 3: CommandList slots + OMSetBlendFactor & BeginRenderPass/EndRenderPass draw/readback
  {
    ID3D12GraphicsCommandList7 *cl7 = nullptr;
    CHECK(gpu.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), (void **)&cl7));

    D3D12_HEAP_PROPERTIES hp_def = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rtd = {};
    rtd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtd.Width = 4; rtd.Height = 4; rtd.DepthOrArraySize = 1; rtd.MipLevels = 1;
    rtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rtd.SampleDesc.Count = 1;
    rtd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource *rt = nullptr, *msaa_rt = nullptr;
    CHECK(dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &rtd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                       nullptr, __uuidof(ID3D12Resource), (void **)&rt));
    rtd.SampleDesc.Count = 4;
    CHECK(dev->CreateCommittedResource(&hp_def, D3D12_HEAP_FLAG_NONE, &rtd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                       nullptr, __uuidof(ID3D12Resource), (void **)&msaa_rt));
    ID3D12Resource *rb = MakeBuffer(gpu, 2048, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

#ifndef __i386__
    D3D12_DESCRIPTOR_HEAP_DESC rtv_hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ID3D12DescriptorHeap *rtv_h = nullptr;
    CHECK(dev->CreateDescriptorHeap(&rtv_hd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_h));
    D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = rtv_h->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv1 = {rtv0.ptr + dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV)};
    dev->CreateRenderTargetView(rt, nullptr, rtv0);
    dev->CreateRenderTargetView(msaa_rt, nullptr, rtv1);
#endif

    D3D12_QUERY_HEAP_DESC qhd = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 2, 0};
    ID3D12QueryHeap *qh = nullptr;
    CHECK(dev->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void **)&qh));

    // RootSignature with 4 root constants at b0, PSO with BLEND_FACTOR
    D3D12_ROOT_PARAMETER rp = {D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, {}, D3D12_SHADER_VISIBILITY_ALL};
    rp.Constants = {0, 0, 4};
    D3D12_ROOT_SIGNATURE_DESC rsd = {1, &rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *rsb = nullptr;
    CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1_0, &rsb, nullptr));
    ID3D12RootSignature *rs = nullptr;
    CHECK(dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&rs));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
    gd.pRootSignature = rs;
    gd.VS = {kQuadVs, sizeof(kQuadVs)};
    gd.PS = {kSolidPs, sizeof(kSolidPs)};
    gd.BlendState.RenderTarget[0].BlendEnable = TRUE;
    gd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_BLEND_FACTOR;
    gd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    gd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    gd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    gd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    gd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.SampleMask = UINT_MAX;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gd.SampleDesc.Count = 4;
    ID3D12PipelineState *pso = nullptr;
    CHECK(dev->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));

    // Exercise command list methods, render pass with MSAA resolve + blend factor + occlusion query
    cl7->ClearState(nullptr);
    cl7->DiscardResource(rt, nullptr);
    cl7->SetMarker(0, "m", 2);
    cl7->BeginEvent(0, "e", 2);
    cl7->EndEvent();
    cl7->SetProtectedResourceSession(nullptr);
    cl7->InitializeMetaCommand(nullptr, nullptr, 0);
    cl7->ExecuteMetaCommand(nullptr, nullptr, 0);
    cl7->SetPipelineState1(nullptr);
    D3D12_DISPATCH_RAYS_DESC drd = {};
    cl7->DispatchRays(&drd);
    cl7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
    cl7->RSSetShadingRateImage(nullptr);
    cl7->OMSetDepthBounds(0.0f, 1.0f);
    float bf[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    cl7->OMSetBlendFactor(bf);

    D3D12_RESOURCE_BARRIER b_to_res = Transition(rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    cl7->ResourceBarrier(1, &b_to_res);
    cl7->BeginQuery(qh, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
#ifndef __i386__
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS rsub = {0, 0, 0, 0, {0, 0, 4, 4}};
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rprt = {};
    rprt.cpuDescriptor = rtv1;
    rprt.BeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    rprt.BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rprt.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
    rprt.EndingAccess.Resolve.pSrcResource = msaa_rt;
    rprt.EndingAccess.Resolve.pDstResource = rt;
    rprt.EndingAccess.Resolve.SubresourceCount = 1;
    rprt.EndingAccess.Resolve.pSubresourceParameters = &rsub;
    rprt.EndingAccess.Resolve.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rprt.EndingAccess.Resolve.ResolveMode = D3D12_RESOLVE_MODE_AVERAGE;

    cl7->BeginRenderPass(1, &rprt, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    cl7->SetGraphicsRootSignature(rs);
    cl7->SetPipelineState(pso);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    cl7->SetGraphicsRoot32BitConstants(0, 4, white, 0);
    cl7->OMSetBlendFactor(bf);
    D3D12_VIEWPORT vp = {0.0f, 0.0f, 4.0f, 4.0f, 0.0f, 1.0f};
    D3D12_RECT sc_r = {0, 0, 4, 4};
    cl7->RSSetViewports(1, &vp);
    cl7->RSSetScissorRects(1, &sc_r);
    cl7->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl7->DrawInstanced(3, 1, 0, 0);
    cl7->EndRenderPass();
#endif
    cl7->EndQuery(qh, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0);
    cl7->ResolveQueryData(qh, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0, 1, rb, 0);

    D3D12_RESOURCE_BARRIER b_msaa_src = Transition(msaa_rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    cl7->ResourceBarrier(1, &b_msaa_src);
    cl7->ResolveSubresourceRegion(rt, 0, 0, 0, msaa_rt, 0, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);

    D3D12_RESOURCE_BARRIER b_to_copy = Transition(rt, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl7->ResourceBarrier(1, &b_to_copy);
    D3D12_TEXTURE_COPY_LOCATION dst_loc = {}, src_loc = {};
    dst_loc.pResource = rb;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_loc.PlacedFootprint.Offset = 512;
    dst_loc.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    src_loc.pResource = rt;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cl7->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
    RunAndWait(gpu);

#ifdef __i386__
    printf("d12c group3-draw status=untested\n");
#else
    uint8_t *mapped = nullptr;
    CHECK(rb->Map(0, nullptr, (void **)&mapped));
    uint32_t px0 = *(const uint32_t *)(mapped + 512);
    uint64_t qval = *(const uint64_t *)mapped;
    rb->Unmap(0, nullptr);
    printf("d12c group3-draw px0=%08x qval=%llu\n", px0, (unsigned long long)qval);
    rtv_h->Release();
#endif

    pso->Release();
    rs->Release();
    rsb->Release();
    qh->Release();
    rb->Release();
    msaa_rt->Release();
    rt->Release();
    cl7->Release();
  }
}

#ifndef _WIN32
// --- d12n: Native-only IMPLEMENT_ME probe (1 per return kind: HRESULT and void) ---
static void RunD12n(Gpu &gpu) {
  auto p_not_impl = (uint32_t (*)())dlsym(RTLD_DEFAULT, "MTLD3DNotImplemented");

  // 1. HRESULT return kind: IDXGISwapChain1::GetRotation
  IDXGISwapChain1 *sc1 = nullptr;
  DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
  sc_desc.Width = 64; sc_desc.Height = 64;
  sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sc_desc.SampleDesc.Count = 1;
  sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sc_desc.BufferCount = 2;
  sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  IDXGIFactory2 *fac = nullptr;
  CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void **)&fac));
  CHECK(fac->CreateSwapChainForHwnd(gpu.queue, (HWND)(uintptr_t)0x4242, &sc_desc, nullptr, nullptr, &sc1));
  fac->Release();

  bool hr_caught = false;
  uint32_t before_hr = p_not_impl ? p_not_impl() : 0;
  try {
    DXGI_MODE_ROTATION rot = DXGI_MODE_ROTATION_UNSPECIFIED;
    HRESULT hr = sc1->GetRotation(&rot);
    if (hr == E_NOTIMPL && p_not_impl && p_not_impl() > before_hr)
      hr_caught = true;
  } catch (const std::exception &) {
    hr_caught = true;
  }
  printf("d12n hr %s\n", hr_caught ? "not implemented" : "unexpected");
  sc1->Release();

  // 2. void return kind: ID3D12Device::GetResourceTiling
  ID3D12Resource *buf = MakeBuffer(gpu, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
  bool void_caught = false;
  uint32_t before_void = p_not_impl ? p_not_impl() : 0;
  try {
    gpu.device->GetResourceTiling(buf, nullptr, nullptr, nullptr, nullptr, 0, nullptr);
    if (p_not_impl && p_not_impl() > before_void)
      void_caught = true;
  } catch (const std::exception &) {
    void_caught = true;
  }
  printf("d12n void %s\n", void_caught ? "not implemented" : "unexpected");
  buf->Release();
}
#endif

// --- d12r: Micro-bench (only when d12r is explicitly named on the command line) ---
static void RunD12r(Gpu &gpu) {
  ID3D12Resource *buf = MakeBuffer(gpu, 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
  auto t0 = std::chrono::steady_clock::now();
  const int kIters = 1000;
  for (int i = 0; i < kIters; i++) {
    D3D12_RESOURCE_BARRIER b1 = Transition(buf, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    D3D12_RESOURCE_BARRIER b2 = Transition(buf, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu.list->ResourceBarrier(1, &b1);
    gpu.list->ResourceBarrier(1, &b2);
  }
  RunAndWait(gpu);
  auto t1 = std::chrono::steady_clock::now();
  double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / (kIters * 2);
  printf("d12r barrier-ns %.1f\n", ns);
  buf->Release();
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "fixed")
      g_full = false;
    else if (a == "full")
      g_full = true;
    else if (a == "--ported")
      g_ported = true;
    else
      g_families.push_back(a);
  }

#ifndef _WIN32
  InitNativeWsi();
#endif

  if (WantFamily("d12x"))
    RunD12x();

  Gpu gpu;
  if (WantFamily("d12d"))
    RunD12d(gpu);
  if (WantFamily("d12o"))
    RunD12o(gpu);
  if (WantFamily("d12h"))
    RunD12h(gpu);
  if (WantFamily("d12p"))
    RunD12p(gpu);
  if (WantFamily("d12l"))
    RunD12l(gpu);
  if (WantFamily("d12s"))
    RunD12s(gpu);
  if (WantFamily("d12c"))
    RunD12c(gpu);
#ifndef _WIN32
  if (g_ported) {
    auto p_not_impl = (uint32_t (*)())dlsym(RTLD_DEFAULT, "MTLD3DNotImplemented");
    if (p_not_impl && p_not_impl() != 0) {
      fprintf(stderr, "--ported failed: MTLD3DNotImplemented() == %u\n", p_not_impl());
      return 1;
    }
  }
  if (WantFamily("d12n"))
    RunD12n(gpu);
#endif
  if (WantFamily("d12r", true))
    RunD12r(gpu);

  return 0;
}
