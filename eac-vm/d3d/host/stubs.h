// What the generated stubs (stubs.cpp) need: native DXMT's headers, its private interfaces, the params structs and the
// dispatcher's side (dispatch.c).
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include "d3d12_interfaces.hpp"
extern "C" {
#include "mnd3d_params.h"
}

typedef unsigned int NTSTATUS_T;

// A params member holding a pointer, converted to whatever pointer type the native method's parameter is.
struct any_ptr {
    uint64_t v;
    template <class T> operator T *() const { return (T *)(uintptr_t)v; }
};
// A struct passed by value, from its guest bytes (the same layout: layout.cpp).
template <class T> static inline std::remove_const_t<T> val(const uint8_t *bytes) {
    std::remove_const_t<T> v;
    std::memcpy(&v, bytes, sizeof v);
    return v;
}

extern "C" {
struct mn_d3d_export { const char *dll, *name; };
// After a creation: which interfaces of iface's family (an mn_d3d_ifaces index; -1: none known) the object answers
// (out[0..3]), which of them at another pointer (out[4..7]), and the handle it already has (out[8]: 0, none).
void mn_d3d_answers(void *obj, int iface, uint64_t out[9]);
// A getter's objects (M4), n native pointers in p, each turned into the handle the guest knows it by (0 for one it
// doesn't, or NULL), DXMT's reference let go.
void mn_d3d_objects_out(uint64_t *p, uint32_t n);
uint64_t mn_d3d_identity_of(uint32_t ctx_id, void *obj); // native.cpp
// A method's HRESULT: device removal, and an asynchronous call's failure, for the call running (dispatch.c).
void mn_d3d_result(HRESULT hr);
// A call whose DXMT path threw (natively, IMPLEMENT_ME: fork 0084): logged once a method, E_NOTIMPL into ret if it has
// one (dispatch.c): the method's own result, 0.
NTSTATUS_T mn_d3d_not_in_dxmt(uint32_t id, uint64_t *ret);
int mn_d3d_arena_of(const void *ptr, uint64_t *blob, uint64_t *offset); // memory.c
uint64_t mn_d3d_memory_largest(void);
void mn_d3d_memory_want(uint64_t len);
int mn_d3d_arena_fits(uint32_t ctx_id, uint64_t len);
// events.c and dispatch.c (Task 8): the call's context, fences' mirror slots, and relays DXMT sets in place of events
uint32_t mn_d3d_current_ctx(void);
uint64_t mn_d3d_fence_new(uint32_t ctx_id, void *obj, uint64_t initial); // UINT64_MAX: none
void mn_d3d_fence_raise(void *obj, uint64_t v);
void mn_d3d_fence_set(void *obj, uint64_t v); // the slot to v, lower or higher (a CPU signal's)
void *mn_d3d_event_relay(uint32_t ctx_id, uint64_t guest_event);
void *mn_d3d_fence_relay(void *obj, uint64_t value);
void mn_d3d_relay_done(void *h);
}
