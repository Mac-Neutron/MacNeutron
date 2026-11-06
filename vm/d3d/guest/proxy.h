// The guest's Direct3D side (M2 Task 6): proxies for the host's objects, in mnd3d.dll (and the loopback library).
// The generated proxies (proxies.cpp) fill each method's params and call mn_d3d_call; this is what they share.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
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
// guest/unix.c's proxy_handle reads a proxy's magic and handle here, in a 32-bit process's (i686 build) too.
static_assert(offsetof(mn_proxy, magic) == sizeof(void *) && offsetof(mn_proxy, handle) == 16, "mn_proxy's header");
extern const void *mn_d3d_vtables[MN_D3D_IFACES][MN_D3D_MAX_SLOTS];

// The unix side's call (unix.c: mn_d3d_unix_call; in mnd3d.dll through winemetal's WMTForward): 0, or a status.
extern "C" int32_t mn_d3d_call(uint32_t id, void *params);
// A command list's method, through mn_d3d_call: on a list another thread called last, MN_D3D_ARG_HANDOFF.
int32_t mn_d3d_record(mn_proxy *This, uint32_t id, void *params);
// mn_d3d_call with mn_d3d.h's MN_D3D_ARG_* flags: a D3D10 resource's map after every thread's published records (it has
// no context), a descriptor write's chosen way.
int32_t mn_d3d_call_with(uint32_t id, uint32_t flags, void *params);
// A call's status as the method's HRESULT (mn_d3d.h's contract).
HRESULT mn_d3d_hr(int32_t status);
// An object argument as the marshaller takes it: our proxy, or the proxy another layer's wrapper of it gives for our
// own interface (2026-10-19: NVIDIA Streamline's wrappers pass the queries they don't know on to what they wrap);
// anything else as it is, which the marshaller refuses.
uintptr_t mn_in(const void *object);
// A created object: its proxy for iface (an index; -1: riid's), answering what ifaces says (the reply's masks); one
// the guest already has (ifaces[4], the host's first handle for it): that object's proxy, the new handle let go.
void *mn_proxy_out(uint64_t handle, int iface, const IID *riid, const uint64_t ifaces[9]);
// A getter's object (M4): the proxy of the object the guest has by that first handle, as iface when it answers it,
// referenced; nullptr for 0 or an object the guest doesn't have.
void *mn_proxy_known(uint64_t handle, int iface);
// A special's guest code (proxy.cpp) for a vtable slot, or fallback (which says it has none).
const void *mn_d3d_special(const char *name, const void *fallback);
// A refused method or a special without guest code: said once each.
void mn_d3d_unhandled(uint32_t id, const char *name, int refused);
HRESULT STDMETHODCALLTYPE mn_px_QueryInterface(mn_proxy *This, REFIID riid, void **out);
ULONG STDMETHODCALLTYPE mn_px_AddRef(mn_proxy *This);
ULONG STDMETHODCALLTYPE mn_px_Release(mn_proxy *This);
