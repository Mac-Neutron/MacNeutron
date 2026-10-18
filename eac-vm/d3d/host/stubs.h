// What the generated stubs (stubs.cpp) need: native DXMT's headers, its private interfaces, the params structs and the
// dispatcher's side (dispatch.c).
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
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
// (out[0..1]), and which of them at another pointer (out[2..3]).
void mn_d3d_answers(void *obj, int iface, uint64_t out[4]);
// A method's HRESULT: device removal, and an asynchronous call's failure, for the call running (dispatch.c).
void mn_d3d_result(HRESULT hr);
}
