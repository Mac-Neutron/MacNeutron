/* Direct3D forwarding's records (M2), shared by the guest and the host. A method's record is MN_OP_D3D + its id
 * (manifest.txt, from MN_D3D_FIRST); ids below that are the family's own ops.
 *
 * Status: the method's own result is in its params' ret (an HRESULT as (uint64_t)(uint32_t)); the dispatcher's
 * return is 0; a negative MN_E* for a malformed record (an unknown id, a record shorter than its params); an NTSTATUS
 * of the marshaller's (MN_STATUS_*: a handle that didn't translate, a reply that didn't fit); or an HRESULT when the
 * host refused the call before running it: DXGI_ERROR_DEVICE_REMOVED once the context's device is removed, E_FAIL
 * without native DXMT, E_NOTIMPL for a method the host has no code for. */
#ifndef MN_D3D_H
#define MN_D3D_H
#include <stdint.h>

#define MN_OP_D3D 0x10000u
enum {
    MN_D3D_OP_RELEASE = 0, /* u64 handle: the guest's last Release (deferred until the rings that used it pass) */
    MN_D3D_OP_ARENA = 1,   /* Task 7 */
    MN_D3D_OP_NOTIFY = 2,  /* Task 8 */
    MN_D3D_OP_TRACE = 3,   /* Task 11 */
    MN_D3D_OP_QUERY = 4,   /* struct mn_d3d_query: QueryInterface for an interface at another pointer, a new handle */
};
struct mn_d3d_query { uint64_t handle; uint8_t iid[16]; uint64_t out; }; /* reply: int32 HRESULT */

#define MN_D3D_E_FAIL ((int32_t)0x80004005)
#define MN_D3D_E_NOTIMPL ((int32_t)0x80004001)
#define MN_D3D_E_NOINTERFACE ((int32_t)0x80004002)
#define MN_D3D_DEVICE_REMOVED ((int32_t)0x887A0005)

#endif
