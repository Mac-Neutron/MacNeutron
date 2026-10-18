/* Direct3D forwarding's host side (M2 Task 5): the executor's handler for macneutron-vm. Records from MN_OP_D3D go to
 * native DXMT through the generated stubs (stubs.cpp), loaded on the first one; the rest to winemetal's dispatcher.
 * Statuses: eac-vm/d3d/mn_d3d.h. */
#ifndef MN_D3D_DISPATCH_H
#define MN_D3D_DISPATCH_H
#include <stdint.h>
#include "../../metal/host/executor.h"

int32_t mn_vm_dispatch(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len);
/* The same with 4a's test ops (--metal-test-ops). */
int32_t mn_vm_dispatch_test(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                            uint32_t cap, uint32_t *reply_len);
/* Where native DXMT is (d3d12.dylib, dxgi.dylib): loaded from there by the first Direct3D record. */
void mn_d3d_set_dir(const char *dir);
/* dlopens native DXMT and resolves its exports, once: 0, or -1 logged as "mn-d3d: error: native DXMT: <dlerror>"
 * (every Direct3D record then answers E_FAIL). */
int mn_d3d_load(const char *dir);
/* A stub's HRESULT (stubs.cpp): removes the context's device on DXGI_ERROR_DEVICE_REMOVED, _HUNG or _RESET, and logs
 * an asynchronous call's failure. */
void mn_d3d_result(int32_t hr);

#endif
