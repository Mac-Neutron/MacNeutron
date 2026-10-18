/* The host dispatcher (M2 Task 5) on native DXMT, through mn_vm_dispatch directly (no transport): the guest's
 * marshaller packs each record, as the guest would send it. In one process with Wine's winemetal.so (metal-host),
 * as macneutron-vm runs them.
 *   device     D3D12CreateDevice with a guest-picked handle makes a device that answers ID3D12Device
 *   fence      ID3D12Device::CreateFence on it; ID3D12Fence::GetCompletedValue answers the initial value
 *   winemetal  a winemetal op still reaches mn_wm_dispatch
 *   invalid    an unknown id, and a record shorter than its params, answer MN_EINVAL
 *   removed    once a stub says DXGI_ERROR_DEVICE_REMOVED, the next CreateFence answers it too
 *   release    the family's op 0 lets a handle go: a later call on it is refused
 *   dispatch-test <metal-host> <native DXMT lib>; prints "dispatch-test: ok" or each failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "marshal.h"
#include "../../guest/transport.h"
#include "host/dispatch.h"
#include "host/loader.h"
#include "mnd3d_params.h"
#include "d3d.h"
#include "../host/dispatch.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];
extern NTSTATUS_T (*mn_d3d_stubs[MN_D3D_CALLS])(void *);
unsigned int NtSetEvent(void *handle, void *prev) { (void)handle, (void)prev; return 0; } /* winemetal.c's listener */

static int failed;
#define CHECK(c) do { if (!(c)) { printf("dispatch-test: FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failed = 1; } } while (0)

static const uint8_t *iid(const char *name) {
    for (unsigned i = 0; i < MN_D3D_IFACES; i++)
        if (!strcmp(mn_d3d_ifaces[i].name, name)) return mn_d3d_ifaces[i].iid;
    printf("dispatch-test: no interface %s\n", name);
    exit(1);
}
static int answers(const uint64_t mask[4], const char *name) {
    for (unsigned i = 0; i < MN_D3D_IFACES; i++)
        if (!strcmp(mn_d3d_ifaces[i].name, name)) return (int)(mask[i / 64] >> (i % 64) & 1);
    return 0;
}

static unsigned id_of(const char *name) {
    for (unsigned i = MN_D3D_FIRST; i < MN_D3D_CALLS; i++)
        if (mn_d3d_calls[i].name && !strcmp(mn_d3d_calls[i].name, name)) return i;
    printf("dispatch-test: no %s\n", name);
    exit(1);
}

static uint8_t rec[MN_REC_MAX], reply[MN_REPLY_BYTES];
static uint64_t ring_state[8];

/* Packs params for id and runs the record: the dispatcher's status; the reply unpacked into params. */
static int32_t call(unsigned id, void *params) {
    const struct mn_wm_call *c = &mn_d3d_calls[id];
    struct mn_wm_out o = {.buf = rec, .cap = sizeof rec};
    if (mn_wm_pack_call(c, params, &o)) return 99;
    uint32_t n = 0;
    int32_t s = mn_vm_dispatch(1, ring_state, MN_OP_D3D + id, rec, o.len, o.sync ? reply : NULL,
                               o.sync ? sizeof reply : 0, &n);
    if (!s && o.sync && mn_wm_unpack_call(c, params, &o, reply, n)) return 98;
    return s;
}

struct private_data { uint64_t self, ret, guid, data_size, data; };
static NTSTATUS_T removed_stub(void *params) {
    ((struct private_data *)params)->ret = (uint32_t)0x887A0005; /* DXGI_ERROR_DEVICE_REMOVED */
    mn_d3d_result((int32_t)0x887A0005);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) return fprintf(stderr, "usage: %s <metal-host> <native DXMT lib>\n", argv[0]), 2;
    mn_open();
    CHECK(mn_wm_load(argv[1]) == 0); /* Wine's winemetal.so first, then native DXMT beside it */
    mn_d3d_set_dir(argv[2]);

    unsigned create = id_of("::D3D12CreateDevice"), fence = id_of("ID3D12Device::CreateFence"),
             value = id_of("ID3D12Fence::GetCompletedValue");

    /* device: the export's params, as mnd3d_params.h has them (their sizes checked against the table's) */
    struct { uint64_t self, ret, adapter; uint32_t level; uint64_t riid, device, device_ifaces[4]; } dev = {0};
    dev.level = 0xb000; /* D3D_FEATURE_LEVEL_11_0 */
    dev.riid = (uintptr_t)iid("ID3D12Device");
    CHECK(mn_d3d_calls[create].params_size == sizeof dev);
    CHECK(call(create, &dev) == 0);
    CHECK((uint32_t)dev.ret == 0);
    uint64_t device = dev.device;
    CHECK(device && answers(dev.device_ifaces, "ID3D12Device") && answers(dev.device_ifaces, "ID3D12Device8"));
    CHECK(answers(dev.device_ifaces, "IMTLD3D12DeviceExt") && answers(dev.device_ifaces + 2, "IMTLD3D12DeviceExt"));
    CHECK(!answers(dev.device_ifaces + 2, "ID3D12Device5")); /* the same pointer */

    /* fence */
    struct fence_params { uint64_t self, ret, initial_value; uint32_t flags; uint64_t riid, fence, ifaces[4]; } fp = {0};
    CHECK(mn_d3d_calls[fence].params_size == sizeof fp);
    fp.self = device, fp.initial_value = 7, fp.riid = (uintptr_t)iid("ID3D12Fence");
    CHECK(call(fence, &fp) == 0 && (uint32_t)fp.ret == 0 && fp.fence);
    struct { uint64_t self, ret; } gv = {fp.fence, 0};
    CHECK(mn_d3d_calls[value].params_size == sizeof gv);
    CHECK(call(value, &gv) == 0 && gv.ret == 7);

    /* winemetal */
    struct mn_wm_stats st;
    uint32_t n = 0;
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_WM + MN_WM_STATS, NULL, 0, &st, sizeof st, &n) == 0 &&
          n == sizeof st && st.live >= 2);

    /* invalid */
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_CALLS + 5, rec, 16, reply, sizeof reply, &n) == MN_EINVAL);
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + value, rec, 8, reply, sizeof reply, &n) == MN_EINVAL);

    /* release */
    struct fence_params f2 = fp;
    f2.fence = 0, f2.initial_value = 1;
    CHECK(call(fence, &f2) == 0 && f2.fence);
    uint64_t h = f2.fence;
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_RELEASE, &h, sizeof h, reply, sizeof reply, &n) == 0);
    mn_wm_hooks.progress(1, ring_state); /* the deferred release runs once the rings that used it pass: all here */
    struct { uint64_t self, ret; } gone = {h, 0};
    CHECK(call(value, &gone) != 0);

    /* removed */
    unsigned victim = id_of("ID3D12Object::GetPrivateData");
    NTSTATUS_T (*keep)(void *) = mn_d3d_stubs[victim];
    mn_d3d_stubs[victim] = removed_stub;
    CHECK(mn_d3d_calls[victim].params_size == sizeof(struct private_data));
    struct private_data gp = {0};
    uint8_t guid[16] = {1};
    uint32_t size = 0;
    gp.self = device, gp.guid = (uintptr_t)guid, gp.data_size = (uintptr_t)&size;
    call(victim, &gp);
    mn_d3d_stubs[victim] = keep;
    f2.fence = 0;
    CHECK(call(fence, &f2) == (int32_t)0x887A0005);

    if (!failed) puts("dispatch-test: ok");
    return failed;
}
