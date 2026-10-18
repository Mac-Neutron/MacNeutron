/* The host dispatcher (M2 Task 5) on native DXMT, through mn_vm_dispatch directly (no transport): the guest's
 * marshaller packs each record, as the guest would send it. In one process with Wine's winemetal.so (metal-host),
 * as macneutron-vm runs them.
 *   device     D3D12CreateDevice with a guest-picked handle makes a device that answers ID3D12Device
 *   fence      ID3D12Device::CreateFence on it; ID3D12Fence::GetCompletedValue answers the initial value
 *   identity   (after M2) ID3D12DeviceChild::GetDevice answers the device's first handle, again after its own
 *              handle's release
 *   winemetal  a winemetal op still reaches mn_wm_dispatch
 *   invalid    an unknown id, and a record shorter than its params, answer MN_EINVAL
 *   removed    once a stub says DXGI_ERROR_DEVICE_REMOVED, the next CreateFence answers it too
 *   release    the family's op 0 lets a handle go: a later call on it is refused
 *   arena      (M2 Task 7) an upload buffer larger than the arenas answers MN_D3D_NEED_ARENA with the bytes it wants;
 *              after op 1 adds one it is made; Map answers its arena's blob and an offset inside it; released, its
 *              piece is the arena's again
 *   events     (M2 Task 8) after op 2 attaches a notify queue and a mirror, a fence's creation names its mirror slot;
 *              a queue's Signal(5) raises the slot to 5 with no record of the guest's; a CPU Signal(9) leaves 9; a raise
 *              to 3 after 5 leaves 5; SetEventOnCompletion(12, e) posts MN_NOTIFY_EVENT with e once the fence
 *              reaches 12 (whatever the guest did with e meanwhile); SetEventOnMultipleFenceCompletion's any and all;
 *              (after M2) the context's notify queue and mirror go with its end, though an event is never set (its
 *              fence released first, or a wait on no fences)
 *   window     (M3 Task 2) op 7's window and op 8's displays: the window by context; IDXGIAdapter::EnumOutputs, the
 *              output's DesktopCoordinates and its two modes as op 8 had them
 *   notimpl    (M3 Task 3) a call that reaches DXMT's IMPLEMENT_ME (a buffer's render target view) fails and the host
 *              goes on, logged
 *   wchar      (M2 Task 10) IDXGIAdapter1::GetDesc1 answers the guest's layout: its description UTF-16, its ids past
 *              it where the guest has them
 *   d3d11device   (M4 Task 5) D3D11CoreCreateDevice on a forwarded factory's adapter makes a device that answers
 *              ID3D11Device, and IDXGIDevice and ID3D10Device1 at other pointers
 *   d3d11identity (M4 Task 5) op 4's companion (the device's IDXGIDevice1) comes with its whole interfaces block: it
 *              answers IDXGIDevice too, and ID3D11Device at another pointer, whose query names the device's first handle
 *   d3d11getters  (M4 Task 5) two views PSSetShaderResources set come back from PSGetShaderResources as their handles
 *   d3d11maparena (M4 Task 7) a dynamic buffer's WRITE_DISCARD maps, renames included, in the guest's arenas; a rename
 *              the arenas can't hold (the creation's allocation, which it names, past their room) answers
 *              MN_D3D_NEED_ARENA before DXMT maps, then maps inside the arena op 1 adds
 *   d3d11events   (M4 Task 6) a D3D11 fence's mirror slot, raised by ID3D11DeviceContext4::Signal; the fence's
 *              SetEventOnCompletion(7, e) and Flush1(e) post MN_NOTIFY_EVENT with e (relays: DXMT never takes e)
 *   d3d11notimpl  (M4 Task 5) a D3D11 call that reaches DXMT's UNIMPLEMENTED (a STAGING buffer's UpdateSubresource)
 *              fails and the host goes on
 *   dispatch-test <metal-host> <native DXMT lib>; prints "dispatch-test: ok" or each failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "marshal.h"
#include "notify.h"
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
    struct { uint64_t self, ret, adapter; uint32_t level; uint64_t riid, device, device_ifaces[9]; } dev = {0};
    dev.level = 0xb000; /* D3D_FEATURE_LEVEL_11_0 */
    dev.riid = (uintptr_t)iid("ID3D12Device");
    CHECK(mn_d3d_calls[create].params_size == sizeof dev);
    CHECK(call(create, &dev) == 0);
    CHECK((uint32_t)dev.ret == 0);
    uint64_t device = dev.device;
    CHECK(device && answers(dev.device_ifaces, "ID3D12Device") && answers(dev.device_ifaces, "ID3D12Device8"));
    CHECK(answers(dev.device_ifaces, "IMTLD3D12DeviceExt") && answers(dev.device_ifaces + MN_D3D_MASK_WORDS, "IMTLD3D12DeviceExt"));
    CHECK(!answers(dev.device_ifaces + MN_D3D_MASK_WORDS, "ID3D12Device5")); /* the same pointer */

    /* fence */
    struct fence_params { uint64_t self, ret, initial_value; uint32_t flags; uint64_t riid, fence, ifaces[9], aux[2]; }
        fp = {0};
    CHECK(mn_d3d_calls[fence].params_size == sizeof fp);
    fp.self = device, fp.initial_value = 7, fp.riid = (uintptr_t)iid("ID3D12Fence");
    CHECK(call(fence, &fp) == 0 && (uint32_t)fp.ret == 0 && fp.fence);
    struct value_params { uint64_t self, ret, aux[2]; } gv = {fp.fence, 0, {0}};
    CHECK(mn_d3d_calls[value].params_size == sizeof gv);
    CHECK(call(value, &gv) == 0 && gv.ret == 7);

    /* identity: GetDevice answers the device's own first handle (the guest returns its proxy), twice, the handle it
     * bound let go between */
    unsigned getdev = id_of("ID3D12DeviceChild::GetDevice");
    struct gd_params { uint64_t self, ret, riid, device, ifaces[9]; };
    CHECK(mn_d3d_calls[getdev].params_size == sizeof(struct gd_params));
    for (int i = 0; i < 2; i++) {
        struct gd_params g = {fp.fence, 0, (uintptr_t)iid("ID3D12Device"), 0, {0}};
        CHECK(call(getdev, &g) == 0 && (uint32_t)g.ret == 0 && g.device && g.device != device && g.ifaces[8] == device);
        uint64_t gh = g.device;
        uint32_t gn = 0;
        CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_RELEASE, &gh, sizeof gh, reply, sizeof reply, &gn) == 0);
        mn_vm_hooks.progress(1, ring_state);
    }

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
    struct value_params gone = {h, 0, {0}};
    CHECK(call(value, &gone) != 0);

    /* arena */
    unsigned committed = id_of("ID3D12Device::CreateCommittedResource"), map = id_of("ID3D12Resource::Map");
    struct { uint32_t type, cpu_page, pool, creation_node, visible_node; } upload = {2}; /* D3D12_HEAP_TYPE_UPLOAD */
    struct { uint32_t dimension; uint64_t alignment, width; uint32_t height; uint16_t depth, mips; uint32_t format;
             uint32_t count, quality, layout, flags; } buf = {1, 0, 1 << 20, 1, 1, 1, 0, 1, 0, 1, 0};
    struct res_params { uint64_t self, ret, heap_properties; uint32_t heap_flags; uint64_t desc; uint32_t state;
                        uint64_t clear, riid, resource, ifaces[9]; } rp = {0};
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
    struct { uint64_t self, ret; uint32_t sub; uint64_t range, data, aux[2]; } mp = {try3.resource, 0, 0, 0, 0, {0}};
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

    /* events */
    struct mn_blob qb, mb;
    CHECK(!mn_blob_alloc(MN_NOTIFY_BYTES, &qb) && !mn_blob_alloc(MN_PAGE, &mb));
    memset(qb.ptr, 0, qb.size);
    struct mn_d3d_notify na = {qb.blob_id, mb.blob_id, mb.size};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_NOTIFY, &na, sizeof na, reply, sizeof reply, &n) == 0);
    volatile uint64_t *mirror = mb.ptr;
    struct mn_notify *q = qb.ptr;
    unsigned queue = id_of("ID3D12Device::CreateCommandQueue"), qsignal = id_of("ID3D12CommandQueue::Signal"),
             fsignal = id_of("ID3D12Fence::Signal"), onc = id_of("ID3D12Fence::SetEventOnCompletion"),
             multi = id_of("ID3D12Device1::SetEventOnMultipleFenceCompletion");
    struct { uint32_t type, priority, flags, node; } qd = {0};
    struct { uint64_t self, ret, desc, riid, queue, ifaces[9]; } cq = {device, 0, (uintptr_t)&qd, 0, 0, {0}};
    cq.riid = (uintptr_t)iid("ID3D12CommandQueue");
    CHECK(mn_d3d_calls[queue].params_size == sizeof cq && call(queue, &cq) == 0 && cq.queue);
    struct fence_params e1 = fp, e2 = fp;
    e1.fence = 0, e1.initial_value = 0, e2.fence = 0, e2.initial_value = 0;
    CHECK(call(fence, &e1) == 0 && e1.fence && call(fence, &e2) == 0 && e2.fence && e1.aux[0] != e2.aux[0]);
    volatile uint64_t *s1 = &mirror[e1.aux[0]], *s2 = &mirror[e2.aux[0]];
    struct qsig { uint64_t self, ret, fence, value, aux[2]; } qs = {cq.queue, 0, e1.fence, 5, {0}};
    CHECK(mn_d3d_calls[qsignal].params_size == sizeof qs && call(qsignal, &qs) == 0 && (uint32_t)qs.ret == 0);
    for (int i = 0; i < 500 && *s1 != 5; i++) usleep(1000); /* the GPU reaches it */
    CHECK(*s1 == 5);
    struct { uint64_t self, ret, value, aux[2]; } fs = {e1.fence, 0, 9, {0}};
    CHECK(mn_d3d_calls[fsignal].params_size == sizeof fs && call(fsignal, &fs) == 0 && *s1 == 9);
    mn_d3d_mirror_raise((uint64_t *)s1, 3); /* a relay for an earlier value firing late */
    CHECK(*s1 == 9);
    struct { uint64_t self, ret, value, event, aux[2]; } oc = {e1.fence, 0, 12, 0x1234, {0}};
    CHECK(mn_d3d_calls[onc].params_size == sizeof oc && call(onc, &oc) == 0 && (uint32_t)oc.ret == 0);
    usleep(20000);
    struct mn_notify_rec r;
    CHECK(!mn_notify_next(q, &r)); /* not there yet */
    struct qsig q12 = {cq.queue, 0, e1.fence, 12, {0}};
    CHECK(call(qsignal, &q12) == 0);
    int got = 0;
    for (int i = 0; i < 500 && !got; i++)
        if (!(got = mn_notify_next(q, &r))) usleep(1000);
    CHECK(got && r.type == MN_NOTIFY_EVENT && r.value == 0x1234);
    uint64_t fences[2] = {e1.fence, e2.fence}, values[2] = {20, 20};
    struct mf { uint64_t self, ret, fences, values; uint32_t count, flags; uint64_t event, aux[2]; }
        any = {device, 0, (uintptr_t)fences, (uintptr_t)values, 2, 1, 0x55, {0}}; /* D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY */
    CHECK(mn_d3d_calls[multi].params_size == sizeof any && call(multi, &any) == 0 && (uint32_t)any.ret == 0);
    struct qsig q20 = {cq.queue, 0, e2.fence, 20, {0}};
    CHECK(call(qsignal, &q20) == 0);
    got = 0;
    for (int i = 0; i < 500 && !got; i++)
        if (!(got = mn_notify_next(q, &r))) usleep(1000);
    CHECK(got && r.value == 0x55);
    values[0] = values[1] = 30;
    struct mf all = any;
    all.flags = 0, all.event = 0x66;
    CHECK(call(multi, &all) == 0);
    struct qsig q30a = {cq.queue, 0, e1.fence, 30, {0}}, q30b = {cq.queue, 0, e2.fence, 30, {0}};
    CHECK(call(qsignal, &q30a) == 0);
    usleep(50000);
    CHECK(!mn_notify_next(q, &r)); /* one of the two */
    CHECK(call(qsignal, &q30b) == 0);
    got = 0;
    for (int i = 0; i < 500 && !got; i++)
        if (!(got = mn_notify_next(q, &r))) usleep(1000);
    CHECK(got && r.value == 0x66);
    /* two events never set, checked at the end: one on a fence released first (Metal keeps what would set it), one
     * that a wait on no fences leaves alone */
    struct fence_params e3 = fp;
    e3.fence = 0, e3.initial_value = 0;
    CHECK(call(fence, &e3) == 0 && e3.fence);
    struct { uint64_t self, ret, value, event, aux[2]; } pending = {e3.fence, 0, 100, 0x77, {0}};
    CHECK(call(onc, &pending) == 0 && (uint32_t)pending.ret == 0);
    uint64_t h3 = e3.fence;
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_RELEASE, &h3, sizeof h3, reply, sizeof reply, &n) == 0);
    mn_vm_hooks.progress(1, ring_state);
    struct mf none = any;
    none.count = 0, none.event = 0x88;
    CHECK(call(multi, &none) == 0 && (uint32_t)none.ret == 0);
    CHECK(mn_d3d_events_live() == 1);

    /* wchar */
    unsigned factory1 = id_of("::CreateDXGIFactory1"), enum1 = id_of("IDXGIFactory1::EnumAdapters1"),
             desc1 = id_of("IDXGIAdapter1::GetDesc1");
    struct { uint64_t self, ret, riid, factory, ifaces[9]; } cf = {0, 0, (uintptr_t)iid("IDXGIFactory1"), 0, {0}};
    CHECK(mn_d3d_calls[factory1].params_size == sizeof cf && call(factory1, &cf) == 0 && cf.factory);
    struct { uint64_t self, ret; uint32_t index; uint64_t adapter, ifaces[9]; } ea = {cf.factory, 0, 0, 0, {0}};
    CHECK(mn_d3d_calls[enum1].params_size == sizeof ea && call(enum1, &ea) == 0 && ea.adapter);
    uint8_t guest[312]; /* DXGI_ADAPTER_DESC1 with a 2-byte WCHAR: Description[128], then VendorId at 256 */
    memset(guest, 0xee, sizeof guest);
    struct { uint64_t self, ret, desc, aux[2]; } gd = {ea.adapter, 0, (uintptr_t)guest, {0}};
    CHECK(mn_d3d_calls[desc1].params_size == sizeof gd && mn_d3d_calls[desc1].fields[1].size == sizeof guest);
    CHECK(call(desc1, &gd) == 0 && (uint32_t)gd.ret == 0);
    uint16_t u[128];
    uint32_t vendor;
    memcpy(u, guest, sizeof u);
    memcpy(&vendor, guest + 256, sizeof vendor);
    char ascii[129] = {0};
    for (int i = 0; i < 128 && u[i] && u[i] < 0x80; i++) ascii[i] = (char)u[i];
    CHECK(!strncmp(ascii, "Apple ", 6) && vendor == 0x106b);

    /* levels: CheckFeatureSupport(FEATURE_LEVELS) whose levels the guest didn't put after it (aux[0] 0) answers
     * E_INVALIDARG; its pointer, the guest's, is never read */
    unsigned check = id_of("ID3D12Device::CheckFeatureSupport");
    struct { uint32_t count, pad; uint64_t requested; uint32_t max, pad2; } lv = {1, 0, 0x414141414000ull, 0, 0};
    mn_d3d_p_ID3D12Device_CheckFeatureSupport cs = {0};
    cs.self = device, cs.feature = 2 /* D3D12_FEATURE_FEATURE_LEVELS */, cs.feature_data = (uintptr_t)&lv;
    cs.feature_data_size = sizeof lv;
    CHECK(sizeof lv == 24 && call(check, &cs) == 0 && (uint32_t)cs.ret == 0x80070057u);

    /* blob: op 5 on an object that isn't an ID3D10Blob is refused */
    struct mn_d3d_blob nb = {device, 0, 64};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_BLOB, &nb, sizeof nb, reply, sizeof reply, &n) ==
          (int32_t)MN_STATUS_INVALID_HANDLE);

    /* window: op 7 and op 8 are what native DXMT's window system answers; context 2's window is its own */
    struct mn_d3d_window win = {0x1234, 0x10001, 800, 600, 4, 0};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_WINDOW, &win, sizeof win, NULL, 0, &n) == 0);
    struct {
        uint32_t count, pad;
        struct mn_d3d_display d;
        struct mn_d3d_mode modes[3];
    } disp = {1, 0, {0x10001, 0, 0, 1920, 1080, {0}, 3, 0}, {{1920, 1080, 60, 1}, {1920, 1080, 60, 1}, {1280, 720, 60, 1}}};
    const char *dev_name = "\\.\DISPLAY1";
    for (int i = 0; dev_name[i]; i++) disp.d.name[i] = (uint16_t)dev_name[i];
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_DISPLAYS, &disp, sizeof disp, NULL, 0, &n) == 0);
    uint64_t ring2[8] = {0};
    struct mn_d3d_window win2 = {0x1234, 0x10001, 640, 480, 4, 0};
    CHECK(mn_vm_dispatch(2, ring2, MN_OP_D3D + MN_D3D_OP_WINDOW, &win2, sizeof win2, NULL, 0, &n) == 0);
    struct mn_d3d_window got1 = {0}, got2 = {0};
    CHECK(mn_d3d_window_of(1, 0x1234, &got1) && got1.width == 800 && got1.height == 600);
    CHECK(mn_d3d_window_of(2, 0x1234, &got2) && got2.width == 640 && got2.height == 480);
    unsigned outputs = id_of("IDXGIAdapter::EnumOutputs"), odesc = id_of("IDXGIOutput::GetDesc"),
             omodes = id_of("IDXGIOutput::GetDisplayModeList");
    struct { uint64_t self, ret; uint32_t index; uint64_t output, ifaces[9]; } eo = {ea.adapter, 0, 0, 0, {0}};
    CHECK(mn_d3d_calls[outputs].params_size == sizeof eo && call(outputs, &eo) == 0 && (uint32_t)eo.ret == 0 &&
          eo.output);
    uint8_t od[96]; /* DXGI_OUTPUT_DESC with a 2-byte WCHAR: DeviceName[32], DesktopCoordinates at 64 */
    memset(od, 0xee, sizeof od);
    struct { uint64_t self, ret, desc, aux[2]; } gdo = {eo.output, 0, (uintptr_t)od, {0}};
    CHECK(mn_d3d_calls[odesc].params_size == sizeof gdo && call(odesc, &gdo) == 0 && (uint32_t)gdo.ret == 0);
    int32_t rect[4];
    memcpy(rect, od + 64, sizeof rect);
    CHECK(rect[0] == 0 && rect[1] == 0 && rect[2] == 1920 && rect[3] == 1080);
    uint32_t count = 0;
    struct { uint64_t self, ret; uint32_t format, flags; uint64_t count, desc; } dm =
        {eo.output, 0, 28 /* DXGI_FORMAT_R8G8B8A8_UNORM */, 0, (uintptr_t)&count, 0};
    CHECK(mn_d3d_calls[omodes].params_size == sizeof dm && call(omodes, &dm) == 0 && (uint32_t)dm.ret == 0);
    CHECK(count == 2);
    /* then the list, as Unreal asks for it (M3 Task 10: SMITE 2 stopped at DXGI_ERROR_MORE_DATA): both modes, sorted */
    struct { uint32_t width, height, num, den, format, scanline, scaling; } modes[2];
    memset(modes, 0, sizeof modes);
    dm.desc = (uintptr_t)modes;
    CHECK(sizeof modes == 56 && call(omodes, &dm) == 0 && (uint32_t)dm.ret == 0 && count == 2);
    CHECK(modes[0].width == 1280 && modes[0].height == 720 && modes[1].width == 1920 && modes[1].height == 1080);
    /* a method on no object (self 0; M3's final review): refused, and the host goes on (its stub ran on NULL) */
    struct { uint64_t self, ret, desc, aux[2]; } nod = {0, 0, (uintptr_t)od, {0}};
    CHECK(call(odesc, &nod) == (int32_t)MN_STATUS_INVALID_HANDLE);

    /* notimpl: a call DXMT has no code for (a render target view of a buffer: its IMPLEMENT_ME) fails, logged; the
     * host goes on */
    unsigned heapc = id_of("ID3D12Device::CreateDescriptorHeap"), rtv = id_of("ID3D12Device::CreateRenderTargetView"),
             start = id_of("ID3D12DescriptorHeap::GetCPUDescriptorHandleForHeapStart");
    struct { uint32_t type, count, flags, node; } hd = {2 /* D3D12_DESCRIPTOR_HEAP_TYPE_RTV */, 1, 0, 0};
    struct { uint64_t self, ret, desc, riid, heap, ifaces[9]; } ch = {device, 0, (uintptr_t)&hd, 0, 0, {0}};
    ch.riid = (uintptr_t)iid("ID3D12DescriptorHeap");
    CHECK(mn_d3d_calls[heapc].params_size == sizeof ch && call(heapc, &ch) == 0 && (uint32_t)ch.ret == 0 && ch.heap);
    uint64_t cpu = 0; /* the struct the guest's caller returns, at __ret */
    struct { uint64_t self, ret, h; } hs = {ch.heap, 0, (uintptr_t)&cpu};
    CHECK(mn_d3d_calls[start].params_size == sizeof hs && call(start, &hs) == 0 && cpu);
    struct { uint32_t type, cpu_page, pool, creation_node, visible_node; } gpu_heap = {1}; /* D3D12_HEAP_TYPE_DEFAULT */
    struct res_params bp = rp;
    bp.heap_properties = (uintptr_t)&gpu_heap, bp.state = 0, bp.resource = 0;
    CHECK(call(committed, &bp) == 0 && (uint32_t)bp.ret == 0 && bp.resource);
    struct { uint64_t self, ret, resource, desc; uint64_t descriptor; } cr = {device, 0, bp.resource, 0, cpu};
    CHECK(mn_d3d_calls[rtv].params_size == sizeof cr && call(rtv, &cr) == 0);
    puts("dispatch-test: notimpl: went on");

    /* d3d11device */
    unsigned mkfactory = id_of("::CreateDXGIFactory1"), enumad = id_of("IDXGIFactory::EnumAdapters"),
             core11 = id_of("::D3D11CoreCreateDevice");
    struct { uint64_t self, ret, riid, factory, ifaces[9]; } f11 = {0, 0, (uintptr_t)iid("IDXGIFactory1"), 0, {0}};
    CHECK(mn_d3d_calls[mkfactory].params_size == sizeof f11 && call(mkfactory, &f11) == 0 && (uint32_t)f11.ret == 0);
    struct { uint64_t self, ret; uint32_t idx; uint64_t adapter, ifaces[9]; } ea1 = {f11.factory, 0, 0, 0, {0}};
    CHECK(mn_d3d_calls[enumad].params_size == sizeof ea1 && call(enumad, &ea1) == 0 && (uint32_t)ea1.ret == 0);
    uint32_t level11 = 0xb000; /* D3D_FEATURE_LEVEL_11_0 */
    struct { uint64_t self, ret, factory, adapter; uint32_t flags; uint64_t levels; uint32_t n; uint64_t device, ifaces[9]; }
        cd = {0, 0, f11.factory, ea1.adapter, 0, (uintptr_t)&level11, 1, 0, {0}};
    CHECK(mn_d3d_calls[core11].params_size == sizeof cd && call(core11, &cd) == 0 && (uint32_t)cd.ret == 0 && cd.device);
    uint64_t d11 = cd.device;
    CHECK(answers(cd.ifaces, "ID3D11Device") && answers(cd.ifaces, "ID3D11Device5"));
    CHECK(answers(cd.ifaces + MN_D3D_MASK_WORDS, "IDXGIDevice") && answers(cd.ifaces + MN_D3D_MASK_WORDS, "ID3D10Device1"));

    /* d3d11identity: op 4 at another pointer, its reply's block, and back */
    struct mn_d3d_query q11 = {d11, {0}, mn_gh_alloc(MN_OWN_PLUS1)};
    memcpy(q11.iid, iid("IDXGIDevice1"), 16);
    struct mn_d3d_query_reply qr = {0};
    uint32_t qn = 0;
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_QUERY, &q11, sizeof q11, &qr, sizeof qr, &qn) == 0);
    CHECK(qn == sizeof qr && qr.hr == 0 && answers(qr.ifaces, "IDXGIDevice1") && answers(qr.ifaces, "IDXGIDevice"));
    CHECK(answers(qr.ifaces, "ID3D11Device") && answers(qr.ifaces + MN_D3D_MASK_WORDS, "ID3D11Device"));
    struct mn_d3d_query back = {q11.out, {0}, mn_gh_alloc(MN_OWN_PLUS1)};
    memcpy(back.iid, iid("ID3D11Device"), 16);
    struct mn_d3d_query_reply br = {0};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_QUERY, &back, sizeof back, &br, sizeof br, &qn) == 0);
    CHECK(br.hr == 0 && br.ifaces[2 * MN_D3D_MASK_WORDS] == d11); /* the device the guest has */

    /* d3d11getters */
    unsigned imm = id_of("ID3D11Device::GetImmediateContext"), tex2d = id_of("ID3D11Device::CreateTexture2D"),
             srv11 = id_of("ID3D11Device::CreateShaderResourceView"),
             pset = id_of("ID3D11DeviceContext::PSSetShaderResources"),
             pget = id_of("ID3D11DeviceContext::PSGetShaderResources");
    struct { uint64_t self, ret, ctx, ifaces[9]; } gi = {d11, 0, 0, {0}};
    CHECK(mn_d3d_calls[imm].params_size == sizeof gi && call(imm, &gi) == 0 && gi.ctx);
    struct { uint32_t w, h, mips, array, format, count, quality, usage, bind, cpu, misc; } td =
        {8, 8, 1, 1, 28 /* R8G8B8A8_UNORM */, 1, 0, 0, 8 /* D3D11_BIND_SHADER_RESOURCE */, 0, 0};
    struct { uint64_t self, ret, desc, init, tex, ifaces[9], aux[2]; } ct = {d11, 0, (uintptr_t)&td, 0, 0, {0}, {0}};
    CHECK(mn_d3d_calls[tex2d].params_size == sizeof ct && call(tex2d, &ct) == 0 && (uint32_t)ct.ret == 0 && ct.tex);
    uint64_t views[2];
    for (int i = 0; i < 2; i++) {
        struct { uint64_t self, ret, res, desc, view, ifaces[9]; } cs = {d11, 0, ct.tex, 0, 0, {0}};
        CHECK(mn_d3d_calls[srv11].params_size == sizeof cs && call(srv11, &cs) == 0 && (uint32_t)cs.ret == 0);
        views[i] = cs.view;
    }
    struct { uint64_t self, ret; uint32_t start, n; uint64_t views; } ps = {gi.ctx, 0, 0, 2, (uintptr_t)views};
    CHECK(mn_d3d_calls[pset].params_size == sizeof ps && call(pset, &ps) == 0);
    uint64_t got11[2] = {0};
    struct { uint64_t self, ret; uint32_t start, n; uint64_t views; } pg = {gi.ctx, 0, 0, 2, (uintptr_t)got11};
    CHECK(mn_d3d_calls[pget].params_size == sizeof pg && call(pget, &pg) == 0);
    CHECK(got11[0] == views[0] && got11[1] == views[1]);

    /* d3d11notimpl: a STAGING buffer's UpdateSubresource (DXMT's UNIMPLEMENTED "update buffer: staging"): the host
     * goes on */
    unsigned buf11 = id_of("ID3D11Device::CreateBuffer"), upd11 = id_of("ID3D11DeviceContext::UpdateSubresource");
    struct { uint32_t width, usage, bind, cpu, misc, stride; } bd11 = {256, 3 /* STAGING */, 0, 0x30000 /* READ|WRITE */, 0, 0};
    struct { uint64_t self, ret, desc, init, buffer, ifaces[9], aux[2]; } cb11 = {d11, 0, (uintptr_t)&bd11, 0, 0, {0}, {0}};
    CHECK(call(buf11, &cb11) == 0 && (uint32_t)cb11.ret == 0 && cb11.buffer);
    uint8_t src[16] = {1};
    struct { uint64_t self, ret, res; uint32_t sub; uint64_t box, data; uint32_t pitch, slice; uint64_t aux[2]; } up11 =
        {gi.ctx, 0, cb11.buffer, 0, 0, (uintptr_t)src, 0, 0, {sizeof src, 0}};
    CHECK(mn_d3d_calls[upd11].params_size == sizeof up11 && call(upd11, &up11) == 0);
    CHECK(call(imm, &gi) == 0 && gi.ctx); /* still there */
    puts("dispatch-test: d3d11notimpl: went on");

    /* d3d11maparena: a 64 KiB dynamic buffer mapped WRITE_DISCARD twice, each map's memory in a guest arena (the
     * renames DXMT makes inside Map too); one whose creation names its allocation and whose rename the arenas can't
     * hold answers MN_D3D_NEED_ARENA before DXMT maps it, and maps inside the arena op 1 then adds */
    unsigned unmap11 = id_of("ID3D11DeviceContext::Unmap"), map11 = id_of("ID3D11DeviceContext::Map");
    struct { uint32_t width, usage, bind, cpu, misc, stride; } dd11 = {64 << 10, 2 /* DYNAMIC */, 1, 0x10000, 0, 0};
    struct { uint64_t self, ret, desc, init, buffer, ifaces[9], aux[2]; } cdy = {d11, 0, (uintptr_t)&dd11, 0, 0, {0}, {0}};
    CHECK(call(buf11, &cdy) == 0 && (uint32_t)cdy.ret == 0 && cdy.buffer && cdy.aux[1] >= (64 << 10));
    struct map11_params { uint64_t self, ret, res; uint32_t sub, type, flags; uint64_t mapped, aux[2]; };
    struct { uint64_t self, ret, res; uint32_t sub; } um = {gi.ctx, 0, cdy.buffer, 0};
    for (int i = 0; i < 2; i++) {
        uint8_t m11[16];
        struct map11_params md = {gi.ctx, 0, cdy.buffer, 0, 4 /* WRITE_DISCARD */, 0, (uintptr_t)m11, {cdy.aux[1], 0}};
        CHECK(call(map11, &md) == 0 && (uint32_t)md.ret == 0);
        CHECK((md.aux[0] == small.blob_id || md.aux[0] == big.blob_id) && md.aux[1] != UINT64_MAX);
        CHECK(call(unmap11, &um) == 0);
    }
    uint64_t room = mn_d3d_arena_free(1) / 5 * 3 / 16384 * 16384; /* its creation fits, a second one doesn't */
    struct { uint32_t width, usage, bind, cpu, misc, stride; } db11 = {(uint32_t)room, 2, 1, 0x10000, 0, 0};
    struct { uint64_t self, ret, desc, init, buffer, ifaces[9], aux[2]; } cbig = {d11, 0, (uintptr_t)&db11, 0, 0, {0}, {0}};
    CHECK(call(buf11, &cbig) == 0 && (uint32_t)cbig.ret == 0 && cbig.buffer && cbig.aux[1] >= room);
    uint64_t before = mn_d3d_arena_free(1);
    uint8_t mb11[16];
    struct map11_params mbig = {gi.ctx, 0, cbig.buffer, 0, 4, 0, (uintptr_t)mb11, {cbig.aux[1], 0}};
    struct map11_params mbig2 = mbig;
    CHECK(call(map11, &mbig) == MN_D3D_NEED_ARENA && mn_d3d_arena_free(1) == before);
    memcpy(&wanted, reply, sizeof wanted);
    CHECK(wanted >= room);
    struct mn_blob more;
    CHECK(!mn_blob_alloc(wanted, &more));
    add = (struct mn_d3d_arena){more.blob_id, 0, more.size};
    CHECK(mn_vm_dispatch(1, ring_state, MN_OP_D3D + MN_D3D_OP_ARENA, &add, sizeof add, reply, sizeof reply, &n) == 0);
    CHECK(call(map11, &mbig2) == 0 && (uint32_t)mbig2.ret == 0 && mbig2.aux[0] == more.blob_id);
    struct { uint64_t self, ret, res; uint32_t sub; } umb = {gi.ctx, 0, cbig.buffer, 0};
    CHECK(call(unmap11, &umb) == 0);

    /* d3d11events: a D3D11 fence's slot raised by the context's Signal, and the guest's events relayed (the fence's
     * SetEventOnCompletion, Flush1's), never DXMT's to take as its own */
    unsigned fence11 = id_of("ID3D11Device5::CreateFence"), sig11 = id_of("ID3D11DeviceContext4::Signal"),
             onc11 = id_of("ID3D11Fence::SetEventOnCompletion"), flush1 = id_of("ID3D11DeviceContext3::Flush1");
    struct { uint64_t self, ret, initial; uint32_t flags; uint64_t iid, fence, ifaces[9], aux[2]; } f11e =
        {d11, 0, 0, 0, (uintptr_t)iid("ID3D11Fence"), 0, {0}, {0}};
    CHECK(mn_d3d_calls[fence11].params_size == sizeof f11e && call(fence11, &f11e) == 0 && (uint32_t)f11e.ret == 0);
    CHECK(f11e.fence && f11e.aux[0] != UINT64_MAX);
    volatile uint64_t *s11 = &mirror[f11e.aux[0]];
    struct { uint64_t self, ret, value, event, aux[2]; } oc11 = {f11e.fence, 0, 7, 0x2345, {0}};
    CHECK(mn_d3d_calls[onc11].params_size == sizeof oc11 && call(onc11, &oc11) == 0 && (uint32_t)oc11.ret == 0);
    struct { uint64_t self, ret, fence, value, aux[2]; } sg11 = {gi.ctx, 0, f11e.fence, 7, {0}};
    CHECK(mn_d3d_calls[sig11].params_size == sizeof sg11 && call(sig11, &sg11) == 0 && (uint32_t)sg11.ret == 0);
    for (int i = 0; i < 500 && *s11 != 7; i++) usleep(1000);
    CHECK(*s11 == 7);
    got = 0;
    for (int i = 0; i < 500 && !got; i++)
        if (!(got = mn_notify_next(q, &r))) usleep(1000);
    CHECK(got && r.type == MN_NOTIFY_EVENT && r.value == 0x2345);
    struct { uint64_t self, ret; uint32_t type; uint64_t event, aux[2]; } fl1 = {gi.ctx, 0, 0 /* ALL */, 0x3456, {0}};
    CHECK(mn_d3d_calls[flush1].params_size == sizeof fl1 && call(flush1, &fl1) == 0);
    got = 0;
    for (int i = 0; i < 500 && !got; i++)
        if (!(got = mn_notify_next(q, &r))) usleep(1000);
    CHECK(got && r.type == MN_NOTIFY_EVENT && r.value == 0x3456);

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

    /* events' end: the context's notify queue and mirror go with it, the two events never set above too */
    mn_vm_hooks.context_end(1);
    CHECK(mn_d3d_events_live() == 0);

    if (!failed) puts("dispatch-test: ok");
    return failed;
}
