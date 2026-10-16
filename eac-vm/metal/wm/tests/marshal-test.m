/* Milestone 4b-1, Task 3: winemetal's call table and the marshaller, guest and host in one process. The guest packs a
 * call, the host runs it against a fake winemetal table (functions that check what they were given and write
 * outputs), and the guest unpacks the reply. Fake objects are slots of objs[]; their retains and releases are
 * counted. Mac, ASan; Objective-C for the fake that raises. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include "../host/loader.h"
#include "../marshal.h"
#include "winemetal_thunks.h"

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __func__, __LINE__, #c);                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

enum { ARRAY, DEVICE, QUEUE, STRING, SAMPLER, FUNCTION, ARCHIVE, PSO, POOL, CMDBUF, NOBJS };
static int objs[NOBJS];
static int retains[NOBJS], releases[NOBJS], calls[MN_WM_SLOTS], checked[MN_WM_SLOTS];
static int fail_next, raise_next;
#define OBJ(i) ((obj_handle_t)(uintptr_t)&objs[i])
static int index_of(obj_handle_t o) { return (int)((int *)(uintptr_t)o - objs); }

static NTSTATUS_T f_retain(void *p) { retains[index_of(*(obj_handle_t *)p)]++; return 0; }
static NTSTATUS_T f_release(void *p) { releases[index_of(*(obj_handle_t *)p)]++; return 0; }
static NTSTATUS_T f_object(void *p) {
    struct unixcall_generic_obj_uint64_obj_ret *a = p;
    checked[2] = a->handle == OBJ(ARRAY) && a->arg == 0;
    a->ret = OBJ(DEVICE);
    return 0;
}
static NTSTATUS_T f_devices(void *p) { ((struct unixcall_generic_obj_ret *)p)->ret = OBJ(ARRAY); return 0; }
static NTSTATUS_T f_name(void *p) {
    struct unixcall_generic_obj_obj_ret *a = p;
    checked[7] = a->handle == OBJ(DEVICE);
    a->ret = OBJ(STRING);
    return 0;
}
static NTSTATUS_T f_getcstring(void *p) {
    struct unixcall_nsstring_getcstring *a = p;
    checked[8] = a->str == OBJ(STRING) && a->max_length == 64;
    strcpy((char *)(uintptr_t)a->buffer_ptr, "hello");
    a->ret = 1;
    return 0;
}
static NTSTATUS_T f_queue(void *p) {
    struct unixcall_generic_obj_uint64_obj_ret *a = p;
    checked[9] = a->handle == OBJ(DEVICE) && a->arg == 64 && a->ret == 0;
    a->ret = fail_next ? 0 : OBJ(QUEUE);
    fail_next = 0;
    return 0;
}
static NTSTATUS_T f_pool(void *p) { ((struct unixcall_generic_obj_ret *)p)->ret = OBJ(POOL); return 0; }
static NTSTATUS_T f_cmdbuf(void *p) {
    struct unixcall_generic_obj_obj_ret *a = p;
    a->ret = OBJ(CMDBUF);
    return 0;
}
static NTSTATUS_T f_commit(void *p) { (void)p; calls[12]++; return 0; }
static NTSTATUS_T f_sampler(void *p) {
    struct unixcall_mtldevice_newsamplerstate *a = p;
    struct WMTSamplerInfo *info = a->info.ptr;
    checked[19] = a->device == OBJ(DEVICE) && info->max_anisotroy == 8;
    info->gpu_resource_id = 0x1234;
    a->ret = OBJ(SAMPLER);
    return 0;
}
static NTSTATUS_T f_function(void *p) {
    struct unixcall_generic_obj_uint64_obj_ret *a = p;
    checked[26] = !strcmp((const char *)(uintptr_t)a->arg, "dbl");
    a->ret = OBJ(FUNCTION);
    return 0;
}
static NTSTATUS_T f_pso(void *p) {
    struct unixcall_mtldevice_newcomputepso *a = p;
    const struct WMTComputePipelineInfo *info = a->info.ptr;
    const obj_handle_t *archives = info->binary_archives_for_lookup.ptr;
    checked[29] = info->compute_function == OBJ(FUNCTION) && info->num_binary_archives_for_lookup == 1 &&
                  archives[0] == OBJ(ARCHIVE) && info->binary_archive_for_serialization == 0 &&
                  info->immutable_buffers == 5;
    a->ret_pso = OBJ(PSO);
    return 0;
}
static NTSTATUS_T f_colorspace(void *p) {
    struct unixcall_generic_obj_uint64_ret *a = p;
    checked[92] = a->handle == 7;
    a->ret = 1;
    return 0;
}
static NTSTATUS_T f_add_allocations(void *p) {
    struct unixcall_mtlresidencyset_addallocations *a = p;
    const obj_handle_t *v = a->allocations.ptr;
    checked[133] = a->set == OBJ(DEVICE) && a->count == 2 && v[0] == OBJ(QUEUE) && v[1] == OBJ(STRING);
    return 0;
}
static NTSTATUS_T f_cache_get(void *p) {
    struct unixcall_cache_get *a = p;
    checked[116] = a->key_length == 3 && !memcmp(a->key.ptr, "key", 3);
    a->ret_data = 0; /* a miss */
    return 0;
}
static NTSTATUS_T f_end(void *p) {
    (void)p;
    if (raise_next) {
        raise_next = 0;
        @throw [NSException exceptionWithName:NSInvalidArgumentException reason:@"not an encoder" userInfo:nil];
    }
    calls[33]++;
    return 0;
}

static wm_unix_fn fakes[MN_WM_SLOTS] = {
    [0] = f_retain, [1] = f_release, [2] = f_object, [4] = f_devices, [7] = f_name, [8] = f_getcstring,
    [9] = f_queue, [10] = f_pool, [11] = f_cmdbuf, [12] = f_commit, [19] = f_sampler, [26] = f_function,
    [29] = f_pso, [33] = f_end, [92] = f_colorspace, [116] = f_cache_get, [133] = f_add_allocations,
};

static size_t no_rings(void *ctx, void *except, struct mn_mark *m, size_t cap) { (void)ctx, (void)except, (void)m, (void)cap; return 0; }
static int all_passed(void *ctx, const struct mn_mark *m, size_t n) { (void)ctx, (void)m, (void)n; return 1; }
static struct mn_ht_rings rings = {NULL, no_rings, all_passed};
static struct mn_wm_ctx ctx;
static uint64_t ring_state[8];
static uint8_t rec[1 << 20], reply[MN_REPLY_BYTES];
static int32_t host_status;

/* One call, guest to host and back, as the guest winemetal.so makes it. */
static NTSTATUS_T call(unsigned slot, void *params) {
    struct mn_wm_out out = {.buf = rec, .cap = sizeof rec};
    NTSTATUS_T s = mn_wm_pack(slot, params, &out);
    if (s) return s;
    uint32_t rl = 0;
    host_status = mn_wm_host_call(&ctx, ring_state, slot, rec, out.len, out.sync ? reply : NULL,
                                  out.sync ? sizeof reply : 0, &rl);
    if (!out.sync) return 0;
    if (host_status) {
        mn_wm_unwind(&out);
        return (NTSTATUS_T)host_status;
    }
    return mn_wm_unpack(slot, params, &out, reply, rl);
}

static obj_handle_t device, queue, string;

static void roundtrip(void) {
    struct unixcall_generic_obj_ret all = {0};
    CHECK(call(4, &all) == 0 && all.ret);
    struct unixcall_generic_obj_uint64_obj_ret first = {all.ret, 0, 0};
    CHECK(call(2, &first) == 0 && first.ret && checked[2]);
    device = first.ret;
    CHECK(retains[DEVICE] == 1); /* borrowed: the table holds its own reference */
    struct unixcall_generic_obj_uint64_obj_ret q = {device, 64, 0};
    CHECK(call(9, &q) == 0 && q.ret && checked[9]);
    queue = q.ret;
    struct unixcall_generic_obj_obj_ret name = {device, 0};
    CHECK(call(7, &name) == 0 && name.ret && checked[7]);
    string = name.ret;
    CHECK(retains[STRING] == 1); /* autoreleased: retained when bound */
    char buf[64] = "";
    struct unixcall_nsstring_getcstring gs = {string, (uint64_t)(uintptr_t)buf, sizeof buf, WMTUTF8StringEncoding, 0};
    CHECK(call(8, &gs) == 0 && gs.ret == 1 && !strcmp(buf, "hello") && checked[8]);
    CHECK(gs.buffer_ptr == (uint64_t)(uintptr_t)buf && gs.str == string); /* the guest's own values come back */
    struct WMTSamplerInfo si = {.max_anisotroy = 8};
    struct unixcall_mtldevice_newsamplerstate ns = {device, {&si}, 0};
    CHECK(call(19, &ns) == 0 && ns.ret && si.gpu_resource_id == 0x1234 && checked[19] && ns.info.ptr == &si);
    struct unixcall_generic_obj_uint64_obj_ret fn = {0, (uint64_t)(uintptr_t) "dbl", 0};
    fn.handle = queue; /* any live object stands in for the library */
    CHECK(call(26, &fn) == 0 && fn.ret && checked[26]);
    /* An archive handle the guest made (bound here directly), in an array inside the pipeline info. */
    obj_handle_t archive = mn_gh_alloc(MN_OWN_PLUS1);
    CHECK(mn_ht_bind(ctx.ht, ring_state, archive, MN_HK_OBJC, (void *)OBJ(ARCHIVE), MN_OWN_PLUS1) == 0);
    struct WMTComputePipelineInfo pi = {.compute_function = fn.ret, .binary_archives_for_lookup = {&archive},
                                        .num_binary_archives_for_lookup = 1, .immutable_buffers = 5};
    struct unixcall_mtldevice_newcomputepso cp = {device, {&pi}, 0, 0};
    CHECK(call(29, &cp) == 0 && cp.ret_pso && cp.ret_error == 0 && checked[29]);
    CHECK(pi.compute_function == fn.ret); /* untouched in the guest */
    struct unixcall_generic_obj_uint64_ret cs = {7, 0};
    CHECK(call(92, &cs) == 0 && cs.ret == 1 && cs.handle == 7 && checked[92]);
    obj_handle_t allocs[2] = {queue, string};
    struct unixcall_mtlresidencyset_addallocations aa = {device, {allocs}, 2};
    CHECK(call(133, &aa) == 0 && host_status == 0 && checked[133]);
}

static void bad_handle(void) {
    obj_handle_t q = queue;
    CHECK(call(1, &q) == 0); /* the queue's last reference */
    mn_ht_sweep(ctx.ht);
    CHECK(releases[QUEUE] == 1);
    uint64_t before = ctx.failures;
    /* The guest refuses its own dead handle without sending; a forged record reaches the host and is refused there. */
    struct unixcall_generic_obj_noret c = {queue};
    CHECK(call(1, &c.handle) == MN_STATUS_INVALID_HANDLE);
    struct { obj_handle_t h; } forged = {queue};
    CHECK(mn_wm_host_call(&ctx, ring_state, 12, &forged, sizeof forged, NULL, 0, &(uint32_t){0}) ==
          (int32_t)MN_STATUS_INVALID_HANDLE);
    CHECK(calls[12] == 0 && ctx.failures == before + 1);
}

static void create_fails(void) {
    uint64_t before = ctx.failures;
    fail_next = 1;
    struct unixcall_generic_obj_uint64_obj_ret q = {device, 64, 0};
    CHECK(call(9, &q) == 0 && q.ret); /* queued: the guest has its handle at once */
    CHECK(host_status < 0 && ctx.failures == before + 1);
    int ok;
    CHECK(mn_ht_get(ctx.ht, q.ret, MN_HK_OBJC, &ok) == NULL && ok); /* bound to nil: later calls are no-ops */
    struct unixcall_generic_obj_noret c = {q.ret};
    CHECK(call(12, &c) == 0 && host_status == 0 && calls[12] == 1);
}

static void nil_answer(void) {
    struct unixcall_cache_get g = {string, {"key"}, 3, 0};
    struct mn_wm_out out = {.buf = rec, .cap = sizeof rec};
    CHECK(mn_wm_pack(116, &g, &out) == 0 && out.sync);
    obj_handle_t picked = g.ret_data;
    CHECK(picked != 0);
    uint32_t rl = 0;
    CHECK(mn_wm_host_call(&ctx, ring_state, 116, rec, out.len, reply, sizeof reply, &rl) == 0);
    CHECK(mn_wm_unpack(116, &g, &out, reply, rl) == 0);
    CHECK(g.ret_data == 0 && checked[116]);
    CHECK(mn_gh_retain(picked) == -1); /* its index is free again */
}

static void exception(void) {
    uint64_t before = ctx.failures;
    raise_next = 1;
    struct unixcall_generic_obj_noret e = {device};
    CHECK(call(33, &e) == 0 && host_status == (int32_t)MN_STATUS_INVALID_PARAMETER && ctx.failures == before + 1);
    CHECK(call(33, &e) == 0 && host_status == 0 && calls[33] == 1);
}

static void pool(void) {
    struct unixcall_generic_obj_ret p = {0};
    CHECK(call(10, &p) == 0 && p.ret);
    struct unixcall_generic_obj_obj_ret cb = {device, 0};
    CHECK(call(11, &cb) == 0 && cb.ret && retains[CMDBUF] == 1);
    obj_handle_t h = p.ret;
    CHECK(call(1, &h) == 0);
    mn_ht_sweep(ctx.ht);
    CHECK(releases[POOL] == 1 && releases[CMDBUF] == 1);
    int ok;
    mn_ht_get(ctx.ht, cb.ret, MN_HK_OBJC, &ok);
    CHECK(!ok && mn_gh_retain(cb.ret) == -1); /* gone on both sides */
}

/* The params struct sizes, from the call catalog's table (its own clang run on DXMT's headers). */
static const uint32_t catalog_sizes[MN_WM_SLOTS] = {
    8, 8, 24, 16, 8, 16, 16, 16, 40, 24, 8, 16, 8, 8, 16, 16, 16, 24, 24, 24, 24, 24, 40, 48, 24, 32, 24, 24, 16, 32,
    16, 24, 24, 8, 32, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 96, 24, 16, 24, 24, 16, 24, 16, 8, 24, 8, 24, 24, 80,
    40, 24, 24, 8, 32, 24, 16, 16, 16, 16, 16, 16, 16, 32, 8, 48, 8, 48, 16, 8, 32, 8, 56, 56, 0, 56, 56, 16, 16, 24,
    16, 16, 40, 16, 24, 8, 8, 16, 16, 48, 32, 24, 40, 24, 16, 32, 16, 16, 32, 8, 8, 8, 24, 32, 24, 24, 24, 32, 24, 32,
    16, 24, 136, 136, 16, 24, 16, 32, 24, 32, 32, 24, 32, 32, 24, 24, 8, 8, 16, 24, 32, 24, 32, 32, 40, 40, 96, 40,
    40, 24, 32, 16, 24, 72,
};

static void coverage(void) {
    for (unsigned s = 0; s < MN_WM_SLOTS; s++) {
        const struct mn_wm_call *c = &mn_wm_calls[s];
        if (!c->name || c->params_size != catalog_sizes[s]) {
            fprintf(stderr, "  slot %u: %s, params %u (catalog %u)\n", s, c->name ? c->name : "(none)", c->params_size,
                    catalog_sizes[s]);
            failures++;
        }
    }
}

/* Global Constraints' lists. */
static const unsigned sync_slots[] = {
    3, 5, 6, 8, 14, 16, 24, 27, 39, 40, 41, 42, 43, 44, 49, 50, 51, 52, 63, 68, 69, 77, 79, 88, 91, 92, 94, 95, 96,
    99, 111, 119, 125, 128, 130, 139, 140, 145, 148, 149, 150,  /* data comes back */
    18, 19, 21, 22, 23, 141, 142, 143,                          /* GPU addresses or resource ids */
    25, 29, 34, 35, 98, 112, 113, 131, 132, 144,                /* error handles */
    56, 89, 115, 116, 127, 151,                                 /* nil is an answer */
    74, 76, 81, 82, 84, 85,                                     /* SM50 with outputs */
    13, 117, 126,                                               /* blocking */
};
static const unsigned refused_slots[] = {47, 48, 66, 67, 70, 71, 72, 73, 93, 97, 101, 120, 121, 122, 123, 124, 83};

static void modes(void) {
    int want[MN_WM_SLOTS] = {0}, n[3] = {0};
    for (size_t i = 0; i < sizeof sync_slots / sizeof *sync_slots; i++) want[sync_slots[i]] = MN_WM_SYNC;
    for (size_t i = 0; i < sizeof refused_slots / sizeof *refused_slots; i++) want[refused_slots[i]] = MN_WM_REFUSED;
    for (unsigned s = 0; s < MN_WM_SLOTS; s++) {
        n[mn_wm_calls[s].mode]++;
        if (mn_wm_calls[s].mode != want[s]) {
            fprintf(stderr, "  slot %u: mode %d, want %d\n", s, mn_wm_calls[s].mode, want[s]);
            failures++;
        }
    }
    CHECK(n[MN_WM_SYNC] == 74 && n[MN_WM_QUEUED] == 62 && n[MN_WM_REFUSED] == 17);
}

int main(void) {
    @autoreleasepool {
        mn_wm_funcs = fakes;
        ctx.ht = mn_ht_new(&rings, mn_wm_release_object);
        struct { const char *name; void (*fn)(void); } tests[] = {
            {"roundtrip", roundtrip}, {"bad-handle", bad_handle}, {"create-fails", create_fails},
            {"nil-answer", nil_answer}, {"exception", exception}, {"pool", pool}, {"coverage", coverage},
            {"modes", modes},
        };
        for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
            int before = failures;
            tests[i].fn();
            printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
        }
        mn_ht_free(ctx.ht);
        mn_wm_ring_end(ring_state);
        printf("marshal-test: %s\n", failures ? "FAIL" : "all passed");
        return failures != 0;
    }
}
