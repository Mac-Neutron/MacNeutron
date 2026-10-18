// The guest's Direct3D side (M2 Task 6): proxies for the host's objects, in mnd3d.dll (and the loopback library).
// The generated proxies (proxies.cpp) fill each method's params and call mn_d3d_call; this is what they share.
#pragma once
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include "d3d12_interfaces.hpp"
extern "C" {
#include "mnd3d_params.h"
#include "mn_d3d.h"
}

struct mn_obj;
// One a host object and an interface it answers: what the program holds. Starts as struct mn_d3d_proxy_hdr.
struct mn_proxy {
    const void *const *vtbl;
    uint32_t magic; // MN_D3D_PROXY_MAGIC
    int32_t iface;  // its mn_d3d_ifaces index
    uint64_t handle;
    mn_obj *obj;
};
extern const void *mn_d3d_vtables[MN_D3D_IFACES][MN_D3D_MAX_SLOTS];

// The unix side's call (unix.c: mn_d3d_unix_call; in mnd3d.dll through winemetal's WMTForward): 0, or a status.
extern "C" int32_t mn_d3d_call(uint32_t id, void *params);
// A command list's method, through mn_d3d_call: on a list another thread called last, MN_D3D_ARG_HANDOFF.
int32_t mn_d3d_record(mn_proxy *This, uint32_t id, void *params);
// A call's status as the method's HRESULT (mn_d3d.h's contract).
HRESULT mn_d3d_hr(int32_t status);
// A created object: its proxy for iface (an index; -1: riid's), answering what ifaces says (the reply's masks); one
// the guest already has (ifaces[4], the host's first handle for it): that object's proxy, the new handle let go.
void *mn_proxy_out(uint64_t handle, int iface, const IID *riid, const uint64_t ifaces[5]);
// A special's guest code (proxy.cpp) for a vtable slot, or fallback (which says it has none).
const void *mn_d3d_special(const char *name, const void *fallback);
// A refused method or a special without guest code: said once each.
void mn_d3d_unhandled(uint32_t id, const char *name, int refused);
HRESULT STDMETHODCALLTYPE mn_px_QueryInterface(mn_proxy *This, REFIID riid, void **out);
ULONG STDMETHODCALLTYPE mn_px_AddRef(mn_proxy *This);
ULONG STDMETHODCALLTYPE mn_px_Release(mn_proxy *This);
