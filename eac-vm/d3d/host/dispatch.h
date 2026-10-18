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
/* memory.c: the arenas (op 1) native DXMT's CPU-visible buffers come from during a creation; begin and end bracket a
 * call (end: the bytes a creation wanted and found no room for, else 0). */
int mn_d3d_arena_add(uint32_t ctx_id, uint64_t blob, uint64_t offset, uint64_t len);
void mn_d3d_memory_hooks(void (*set)(void *(*alloc)(uint64_t, void *), void (*free)(void *, void *), void *ctx));
void mn_d3d_memory_begin(uint32_t ctx_id, int creation);
uint64_t mn_d3d_memory_end(void);
int mn_d3d_arena_of(const void *ptr, uint64_t *blob, uint64_t *offset); /* 0, or -1: not in an arena */
void mn_d3d_arenas_end(uint32_t ctx_id);
uint64_t mn_d3d_arena_free(uint32_t ctx_id); /* free bytes in the context's arenas (tests) */
/* events.c: a fence's mirror slot raised to v, never lowered (relays fire in any order). */
void mn_d3d_mirror_raise(uint64_t *slot, uint64_t v);
extern const struct mn_executor_hooks mn_vm_hooks; /* winemetal's, and the arenas' end with the context's */

#endif
