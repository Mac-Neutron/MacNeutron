/* Direct3D forwarding's records (M2), shared by the guest and the host. A method's record is MN_OP_D3D + its id
 * (manifest.txt, from MN_D3D_FIRST); ids below that are the family's own ops.
 *
 * Status: the method's own result is in its params' ret (an HRESULT as (uint64_t)(uint32_t)); the dispatcher's
 * return is 0; a negative MN_E* for a malformed record (an unknown id, a record shorter than its params); an NTSTATUS
 * of the marshaller's (MN_STATUS_*: a handle that didn't translate, a reply that didn't fit); or an HRESULT when the
 * host refused the call before running it: DXGI_ERROR_DEVICE_REMOVED once the context's device is removed, E_FAIL
 * without native DXMT, E_NOTIMPL for a method the host has no code for. */
#ifndef MN_D3D_MN_D3D_H
#define MN_D3D_MN_D3D_H
#include <stdint.h>

#define MN_OP_D3D 0x10000u
enum {
    MN_D3D_OP_RELEASE = 0, /* u64 handle: the guest's last Release (deferred until the rings that used it pass) */
    MN_D3D_OP_ARENA = 1,   /* Task 7 */
    MN_D3D_OP_NOTIFY = 2,  /* Task 8 */
    MN_D3D_OP_TRACE = 3,   /* u32 id, u32 refused: a method the guest answered itself, for its trace; never sent */
    MN_D3D_OP_QUERY = 4,   /* struct mn_d3d_query: QueryInterface for an interface at another pointer, a new handle */
    MN_D3D_OP_BLOB = 5,    /* struct mn_d3d_blob: an ID3D10Blob's bytes; reply: u64 its size, then the bytes asked */
    MN_D3D_OP_LISTEN = 6,  /* none: the calling thread sets the events the host posts, for the process's life; never sent */
};
struct mn_d3d_query { uint64_t handle; uint8_t iid[16]; uint64_t out; }; /* reply: int32 HRESULT */
struct mn_d3d_blob { uint64_t handle, offset, len; };
/* Op 2: the context's Direct3D notify queue (MN_NOTIFY_EVENT: a guest event to set) and its fences' mirror, one
 * uint64_t a fence (its completed value, raised by the host: max(slot, value)); a fence's creation names its slot in
 * aux[0], the guest's address of it in aux[1] (the unix side's). */
struct mn_d3d_notify { uint64_t queue_blob, mirror_blob, mirror_len; };

/* Op 1: guest memory (a galloc region, held for the host) the host carves CPU-visible resources from. */
struct mn_d3d_arena { uint64_t blob_id, offset, len; };
/* A creation's status when no arena has room: the reply is the u64 bytes wanted; the guest adds an arena of at least
 * MN_D3D_ARENA_BYTES that size and sends the creation again, once (then E_OUTOFMEMORY). */
#define MN_D3D_NEED_ARENA 0x20000001
#define MN_D3D_ARENA_BYTES (64ull << 20)

/* A guest proxy's start (guest/proxy.h): the marshaller's hook reads the handle there. */
struct mn_d3d_proxy_hdr { const void *vtbl; uint32_t magic; int32_t iface; uint64_t handle; };
#define MN_D3D_PROXY_MAGIC 0x4433444eu /* "ND3D" */
/* The unix side's call (guest/unix.c): id, and the params (a method's struct, or a family op's below). */
struct mn_d3d_args { uint32_t id, pad; uint64_t params; };
struct mn_d3d_query_args { uint64_t handle; uint8_t iid[16]; uint64_t out; int32_t hr, pad; };
struct mn_d3d_blob_args { uint64_t handle, dst, cap, size; }; /* the blob's bytes into dst (at most cap), its size */

#define MN_D3D_E_FAIL ((int32_t)0x80004005)
#define MN_D3D_E_NOTIMPL ((int32_t)0x80004001)
#define MN_D3D_E_NOINTERFACE ((int32_t)0x80004002)
#define MN_D3D_DEVICE_REMOVED ((int32_t)0x887A0005)

#endif
