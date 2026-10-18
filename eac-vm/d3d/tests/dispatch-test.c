/* The host dispatcher (M2 Task 5) on native DXMT, through mn_vm_dispatch directly (no transport): the guest's
 * marshaller packs each record, as the guest would send it. In one process with Wine's winemetal.so (metal-host),
 * as macneutron-vm runs them.
 *   device     D3D12CreateDevice with a guest-picked handle makes a device that answers ID3D12Device
 *   fence      ID3D12Device::CreateFence on it; ID3D12Fence::GetCompletedValue answers the initial value
 *   winemetal  a winemetal op still reaches mn_wm_dispatch
 *   invalid    an unknown id, and a record shorter than its params, answer MN_EINVAL
 *   removed    once a stub says DXGI_ERROR_DEVICE_REMOVED, the next CreateFence answers it too
 *   release    the family's op 0 lets a handle go: a later call on it is refused
 *   arena      (M2 Task 7) an upload buffer larger than the arenas answers MN_D3D_NEED_ARENA with the bytes it wants;
 *              after op 1 adds one it is made; Map answers its arena's blob and an offset inside it; released, its
 *              piece is the arena's again
 *   dispatch-test <metal-host> <native DXMT lib>; prints "dispatch-test: ok" or each failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "marshal.h"
#include "../../guest/transport.h"
#include "host/dispatch.h"
#include "host/loader.h"
#include "mnd3d_params.h"
#include "mn_d3d.h"
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

    /* arena */
    unsigned committed = id_of("ID3D12Device::CreateCommittedResource"), map = id_of("ID3D12Resource::Map");
    struct { uint32_t type, cpu_page, pool, creation_node, visible_node; } upload = {2}; /* D3D12_HEAP_TYPE_UPLOAD */
    struct { uint32_t dimension; uint64_t alignment, width; uint32_t height; uint16_t depth, mips; uint32_t format;
             uint32_t count, quality, layout, flags; } buf = {1, 0, 1 << 20, 1, 1, 1, 0, 1, 0, 1, 0};
    struct res_params { uint64_t self, ret, heap_properties; uint32_t heap_flags; uint64_t desc; uint32_t state;
                        uint64_t clear, riid, resource, ifaces[4]; } rp = {0};
    CHECK(mn_d3d_calls[committed].params_size == sizeof rp && sizeof buf == 56 && sizeof upload == 20);
    rp.self = device, rp.heap_properties = (uintptr_t)&upload, rp.desc = (uintptr_t)&buf;
    rp.state = 0xac3; /* D3D12_RESOURCE_STATE_GENERIC_READ */
    rp.riid = (uintptr_t)iid("ID3D12Resource");
    struct res_params try1 = rp;
    CHECK(call(committed, &try1) == MN_D3D_NEED_ARENA);
    uint64_t wanted;
    memcpy(&wanted, reply, sizeof wanted);
    CHECK(wanted >= (1 << 20));
    struct mn_blob small, big;
    CHECK(!mn_blob_alloc(512 << 10, &small) && !mn_blob_alloc(4 << 20, &big));
    struct mn_d3d_arena add = {small.blob_id, 0, small.size};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_ARENA, &add, sizeof add, reply, sizeof reply, &n) == 0);
    struct res_params try2 = rp;
    CHECK(call(committed, &try2) == MN_D3D_NEED_ARENA); /* still too small */
    add = (struct mn_d3d_arena){big.blob_id, 0, big.size};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_ARENA, &add, sizeof add, reply, sizeof reply, &n) == 0);
    uint64_t total = small.size + big.size;
    CHECK(mn_d3d_arena_free(1) == total);
    struct res_params try3 = rp;
    CHECK(call(committed, &try3) == 0 && (uint32_t)try3.ret == 0 && try3.resource);
    CHECK(mn_d3d_arena_free(1) <= total - (1 << 20));
    struct { uint64_t self, ret; uint32_t sub; uint64_t range, data; } mp = {try3.resource, 0, 0, 0, 0};
    uint64_t at[2] = {0, ~0ull};
    mp.data = (uintptr_t)at;
    CHECK(mn_d3d_calls[map].params_size == sizeof mp);
    CHECK(call(map, &mp) == 0 && (uint32_t)mp.ret == 0);
    CHECK(at[0] == big.blob_id && at[1] + (1 << 20) <= big.size);
    uint64_t rh = try3.resource;
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_RELEASE, &rh, sizeof rh, reply, sizeof reply, &n) == 0);
    mn_wm_hooks.progress(1, ring_state);
    for (int i = 0; i < 200 && mn_d3d_arena_free(1) != total; i++) usleep(10000); /* Metal lets it go */
    CHECK(mn_d3d_arena_free(1) == total);

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
