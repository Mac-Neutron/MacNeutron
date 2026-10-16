/* Milestone 4b-1, Task 3: winemetal's call table and the marshaller, guest and host in one process. The guest packs a
 * call, the host runs it against a fake winemetal table (functions that check what they were given and write
 * outputs), and the guest unpacks the reply. Fake objects are slots of objs[]; their retains and releases are
 * counted. Mac, ASan; Objective-C for the fake that raises. */
#import <Foundation/Foundation.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "../host/loader.h"
#include "../cmdlist.h"
#include "../marshal.h"
#include "../../guest/transport.h"
#include "airconv_thunks.h"
#include "loopback.h"
#include "winemetal_thunks.h"

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __func__, __LINE__, #c);                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

enum { ARRAY, DEVICE, QUEUE, STRING, SAMPLER, FUNCTION, ARCHIVE, PSO, POOL, CMDBUF, ENCODER, SHADER, BITCODE, LOG0, LOG1,
       CONSTFN, NOBJS };
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
static NTSTATUS_T f_blit_samples(void *p) {
    struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers *a = p;
    const struct WMTSampleBufferAttachmentInfo *v = a->attachments.ptr;
    checked[129] = a->cmdbuf == OBJ(DEVICE) && a->num_attachments == 2 && v[0].sample_buffer == OBJ(QUEUE) &&
                   v[1].sample_buffer == OBJ(STRING);
    a->ret = OBJ(ENCODER);
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

/* Command lists: the guest's nodes, kept to compare with what the host's list holds. */
#define MAX_TEST_NODES 64
static uint8_t sent[3][MAX_TEST_NODES][128];
static unsigned nsent[3];
static uint8_t payload_bytes[3][MAX_TEST_NODES][256];
static int list_ok[3];
static NTSTATUS_T f_encode(unsigned which, void *p) {
    struct unixcall_generic_obj_cmd_noret *a = p;
    unsigned ntypes, i = 0;
    const struct mn_wm_node *tab = mn_wm_nodes(36 + which, &ntypes);
    int ok = a->encoder == OBJ(DEVICE); /* any live object stands in for the encoder */
    for (const struct wmtcmd_base *n = a->cmd_head.ptr; n && ok; n = n->next.ptr, i++) {
        const uint8_t *g = sent[which][i], *h = (const uint8_t *)n;
        const struct mn_wm_node *t = &tab[n->type];
        ok = i < nsent[which] && n->type == ((const struct wmtcmd_base *)g)->type && t->size;
        for (unsigned b = 16; ok && b < t->size; b++) {
            int special = 0;
            for (unsigned k = 0; k < t->nhandles; k++) special |= b >= t->handles[k] && b < t->handles[k] + 8u;
            if (t->payload) special |= b >= t->payload && b < t->payload + 8u;
            if (!special) ok = g[b] == h[b];
        }
        for (unsigned k = 0; ok && k < t->nhandles; k++) {
            obj_handle_t v;
            memcpy(&v, h + t->handles[k], 8);
            ok = v == OBJ(STRING); /* every handle in the guest's list is the string's */
        }
        if (ok && t->payload) {
            const uint8_t *pp;
            memcpy(&pp, h + t->payload, 8);
            uint64_t len = mn_wm_node_payload(t, h);
            ok = len && pp && !memcmp(pp, payload_bytes[which][i], len);
        }
    }
    list_ok[which] = ok && i == nsent[which];
    return 0;
}
static NTSTATUS_T f_blit_encode(void *p) { return f_encode(0, p); }
static NTSTATUS_T f_compute_encode(void *p) { return f_encode(1, p); }
static NTSTATUS_T f_render_encode(void *p) { return f_encode(2, p); }

/* SM50: a shader whose reflection says 2 constant buffers and 3 arguments, bitcode of 100 bytes. */
static uint8_t bytecode[300], bitcode_bytes[100];
static int sm50_released[3];
static NTSTATUS_T f_sm50_init(void *p) {
    struct sm50_initialize_params *a = p;
    checked[74] = a->bytecode_size == sizeof bytecode && !memcmp(a->bytecode, bytecode, sizeof bytecode) && a->shader;
    *a->shader = (sm50_shader_t)OBJ(SHADER);
    struct MTL_SHADER_REFLECTION *r = a->reflection;
    if (r) r->NumConstantBuffers = 2, r->NumArguments = 3, r->ThreadgroupSize[0] = 64;
    if (a->error) *a->error = NULL;
    a->ret = 0;
    return 0;
}
static NTSTATUS_T f_sm50_destroy(void *p) { sm50_released[0] += ((struct sm50_destroy_params *)p)->shader == (void *)OBJ(SHADER); return 0; }
static NTSTATUS_T f_sm50_compile(void *p) {
    struct sm50_compile_params *a = p;
    const struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *n = a->args;
    const struct SM50_SHADER_COMMON_DATA *common = (const void *)n;
    const struct SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia = n ? n->next : NULL;
    checked[76] = a->shader == (void *)OBJ(SHADER) && !strcmp(a->func_name, "main") && n &&
                  n->type == SM50_SHADER_COMMON && common->metal_version == SM50_SHADER_METAL_320 && ia &&
                  ia->type == SM50_SHADER_IA_INPUT_LAYOUT && ia->num_elements == 2 && ia->elements[1].reg == 7 &&
                  !((const struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *)ia)->next;
    *a->bitcode = (sm50_bitcode_t)OBJ(BITCODE);
    if (a->error) *a->error = NULL;
    a->ret = 0;
    return 0;
}
static NTSTATUS_T f_sm50_bitcode(void *p) {
    struct sm50_get_compiled_bitcode_params *a = p;
    checked[77] = a->bitcode == (void *)OBJ(BITCODE);
    a->data_out->Data = bitcode_bytes;
    a->data_out->Size = sizeof bitcode_bytes;
    return 0;
}
static NTSTATUS_T f_sm50_destroy_bitcode(void *p) { sm50_released[1] += ((struct sm50_destroy_bitcode_params *)p)->bitcode == (void *)OBJ(BITCODE); return 0; }
static NTSTATUS_T f_sm50_message(void *p) {
    struct sm50_get_error_message_params *a = p;
    snprintf(a->buffer, a->buffer_size, "bad shader");
    a->ret_size = strlen(a->buffer);
    return 0;
}
static NTSTATUS_T f_sm50_args_info(void *p) {
    struct sm50_get_arguments_info_params *a = p;
    checked[88] = a->shader == (void *)OBJ(SHADER);
    if (a->constant_buffers) memset(a->constant_buffers, 0xc1, 2 * sizeof *a->constant_buffers);
    if (a->arguments) memset(a->arguments, 0xa3, 3 * sizeof *a->arguments);
    return 0;
}
/* Paths: what the host was given. */
static char host_path[1024];
static NTSTATUS_T f_cache_open(void *p) {
    struct unixcall_cache_alloc_init *a = p;
    snprintf(host_path, sizeof host_path, "%s", (const char *)a->path.ptr);
    a->ret_cache = OBJ(ARCHIVE);
    return 0;
}
static NTSTATUS_T f_cache_path(void *p) {
    struct unixcall_setmetalcachepath *a = p;
    snprintf(host_path, sizeof host_path, "%s", (const char *)a->path.ptr);
    a->ret_success = 1;
    return 0;
}
static NTSTATUS_T f_new_archive(void *p) {
    struct unixcall_mtldevice_newbinaryarchive *a = p;
    snprintf(host_path, sizeof host_path, "%s", a->url.ptr ? (const char *)a->url.ptr : "(null)");
    a->ret_archive = OBJ(ARCHIVE);
    return 0;
}
/* Strings. */
static NTSTATUS_T f_string(void *p) {
    struct unixcall_nsstring_string *a = p;
    checked[60] = !strcmp(a->buffer_ptr.ptr, "héllo") && a->encoding == WMTUTF8StringEncoding;
    a->ret = OBJ(STRING);
    return 0;
}
static NTSTATUS_T f_length(void *p) {
    struct unixcall_generic_obj_uint64_uint64_ret *a = p;
    a->ret = a->handle == OBJ(STRING) ? 6 : 0;
    return 0;
}
/* Logs: two of them, or a failure. */
static int fail_enumerate;
static NTSTATUS_T f_enumerate(void *p) {
    struct unixcall_enumerate *a = p;
    if (fail_enumerate) return MN_STATUS_UNSUCCESSFUL;
    obj_handle_t *v = a->buffer.ptr;
    checked[91] = a->enumerable == OBJ(DEVICE) && a->buffer_size == 4;
    v[0] = OBJ(LOG0), v[1] = OBJ(LOG1);
    a->ret_read = 2;
    return 0;
}
/* Function constants: a bool and an int. */
static NTSTATUS_T f_constants(void *p) {
    struct unixcall_mtllibrary_newfunction_with_constants *a = p;
    const struct WMTFunctionConstant *c = a->constants.ptr;
    checked[98] = !strcmp(a->name.ptr, "frag") && a->num_constants == 2 && c[0].type == WMTDataTypeBool &&
                  *(const uint8_t *)c[0].data.ptr == 1 && c[1].type == WMTDataTypeInt && c[1].index == 5 &&
                  *(const int32_t *)c[1].data.ptr == 42;
    a->ret = OBJ(CONSTFN);
    return 0;
}

static wm_unix_fn fakes[MN_WM_SLOTS] = {
    [74] = f_sm50_init, [75] = f_sm50_destroy, [76] = f_sm50_compile, [77] = f_sm50_bitcode,
    [78] = f_sm50_destroy_bitcode, [79] = f_sm50_message, [88] = f_sm50_args_info, [115] = f_cache_open,
    [117] = f_cache_open, [119] = f_cache_path, [112] = f_new_archive, [60] = f_string, [61] = f_string,
    [27] = f_length, [91] = f_enumerate, [98] = f_constants,
    [36] = f_blit_encode, [37] = f_compute_encode, [38] = f_render_encode,
    [0] = f_retain, [1] = f_release, [2] = f_object, [4] = f_devices, [7] = f_name, [8] = f_getcstring,
    [9] = f_queue, [10] = f_pool, [11] = f_cmdbuf, [12] = f_commit, [19] = f_sampler, [26] = f_function,
    [29] = f_pso, [33] = f_end, [92] = f_colorspace, [116] = f_cache_get, [129] = f_blit_samples,
};

/* The loopback's context and this thread's ring (no other rings: releases pass at once), which the guest code's own
 * transport calls (slot 77's second step) reach too. */
#define ctx loop_ctx
#define ring_state ((uint64_t *)loop_ring())
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
        mn_wm_unwind(&out, 1);
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
    struct WMTSampleBufferAttachmentInfo at[2] = {{.sample_buffer = queue}, {.sample_buffer = string}};
    struct unixcall_mtlcommandbuffer_blitcommandencoderwithsamplebuffers bs = {device, {at}, 2, 0};
    CHECK(call(129, &bs) == 0 && host_status == 0 && bs.ret && checked[129]); /* an array of structs, handles in */
    CHECK(at[0].sample_buffer == queue); /* untouched in the guest */
}

static void bad_handle(void) {
    obj_handle_t q = queue;
    CHECK(call(1, &q) == 0); /* the queue's last reference */
    mn_ht_sweep(ctx.ht, ring_state);
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
    mn_ht_sweep(ctx.ht, ring_state);
    CHECK(releases[POOL] == 1 && releases[CMDBUF] == 1);
    int ok;
    mn_ht_get(ctx.ht, cb.ret, MN_HK_OBJC, &ok);
    CHECK(!ok && mn_gh_retain(cb.ret) == -1); /* gone on both sides */
}

/* Every node type of each encoder, each node's bytes a pattern, its handles the string's and its payload bytes. */
static uint8_t lists[3][MAX_TEST_NODES][128];
static void build_list(unsigned which) {
    unsigned ntypes, n = 0;
    const struct mn_wm_node *tab = mn_wm_nodes(36 + which, &ntypes);
    for (unsigned type = 0; type < ntypes; type++) {
        const struct mn_wm_node *t = &tab[type];
        if (!t->size) continue;
        uint8_t *node = lists[which][n];
        for (unsigned b = 0; b < t->size; b++) node[b] = (uint8_t)(type * 7 + b);
        uint16_t ty = (uint16_t)type;
        memcpy(node, &ty, 2);
        for (unsigned k = 0; k < t->nhandles; k++) memcpy(node + t->handles[k], &string, 8);
        if (t->payload) {
            uint64_t count = t->elem == 1 ? 40 : 3;
            memset(node + t->count, 0, t->count_width);
            memcpy(node + t->count, &count, t->count_width);
            for (unsigned b = 0; b < 256; b++) payload_bytes[which][n][b] = (uint8_t)(b * 3 + type);
            const void *pp = payload_bytes[which][n];
            memcpy(node + t->payload, &pp, 8);
        }
        memcpy(sent[which][n], node, t->size);
        n++;
    }
    for (unsigned i = 0; i < n; i++) {
        void *next = i + 1 < n ? lists[which][i + 1] : NULL;
        memcpy(lists[which][i] + 8, &next, 8);
    }
    nsent[which] = n;
}

static void cmdlist_roundtrip(void) {
    for (unsigned which = 0; which < 3; which++) {
        build_list(which);
        struct unixcall_generic_obj_cmd_noret e = {device, {lists[which][0]}};
        CHECK(call(36 + which, &e) == 0 && host_status == 0 && list_ok[which]);
    }
    CHECK(nsent[0] == 14 && nsent[1] == 14 && nsent[2] == 43);
}

/* A host record by hand: params, then the flat list's region (inline). */
static int32_t forged_list(unsigned slot, uint32_t count, uint16_t type, unsigned nodes) {
    static uint8_t r[1 << 21];
    struct unixcall_generic_obj_cmd_noret e = {device, {NULL}};
    uint32_t len = 16 + 8, body = 8 + nodes * 16;
    memcpy(r, &e, 16);
    struct mn_wm_rhdr h = {body, MN_RH_INLINE};
    memcpy(r + 16, &h, 8);
    memset(r + len, 0, body);
    memcpy(r + len, &count, 4);
    for (unsigned i = 0; i < nodes; i++) memcpy(r + len + 8 + i * 16, &type, 2);
    return mn_wm_host_call(&ctx, ring_state, slot, r, len + body, NULL, 0, &(uint32_t){0});
}

static void cmdlist_loop(void) {
    static uint8_t loop[16];
    struct wmtcmd_compute_nop *n = (void *)loop;
    n->type = WMTComputeCommandNop;
    n->next.ptr = n; /* a cycle */
    struct unixcall_generic_obj_cmd_noret e = {device, {n}};
    list_ok[1] = 0;
    CHECK(call(37, &e) == MN_STATUS_INVALID_PARAMETER);
    static struct wmtcmd_compute_nop many[MN_WM_MAX_NODES + 1];
    for (unsigned i = 0; i <= MN_WM_MAX_NODES; i++) many[i] = (struct wmtcmd_compute_nop){.next = {i < MN_WM_MAX_NODES ? &many[i + 1] : NULL}};
    e.cmd_head.ptr = many;
    CHECK(call(37, &e) == MN_STATUS_INVALID_PARAMETER);
    uint64_t before = ctx.failures;
    CHECK(forged_list(37, MN_WM_MAX_NODES + 1, 0, MN_WM_MAX_NODES + 1) == (int32_t)MN_STATUS_INVALID_PARAMETER);
    CHECK(forged_list(37, 4, 0, 2) == (int32_t)MN_STATUS_INVALID_PARAMETER); /* more nodes than the region holds */
    CHECK(ctx.failures == before + 2 && !list_ok[1]);
}

static void cmdlist_unknown_type(void) {
    static uint8_t node[16];
    uint16_t unused = 26; /* Unused0 */
    memcpy(node, &unused, 2);
    struct unixcall_generic_obj_cmd_noret e = {device, {node}};
    list_ok[2] = 0;
    CHECK(call(38, &e) == MN_STATUS_INVALID_PARAMETER);
    CHECK(forged_list(38, 1, 27, 1) == (int32_t)MN_STATUS_INVALID_PARAMETER);
    CHECK(forged_list(36, 1, 14, 1) == (int32_t)MN_STATUS_INVALID_PARAMETER); /* past blit's last type */
    CHECK(!list_ok[2]);
}

static sm50_shader_t shader;

static void sm50_sizes(void) {
    for (size_t i = 0; i < sizeof bytecode; i++) bytecode[i] = (uint8_t)i;
    struct MTL_SHADER_REFLECTION refl = {0};
    sm50_error_t err = (sm50_error_t)1;
    struct sm50_initialize_params in = {bytecode, sizeof bytecode, &shader, &refl, &err, -1};
    CHECK(call(74, &in) == 0 && in.ret == 0 && shader && !err && checked[74]);
    CHECK(refl.NumConstantBuffers == 2 && refl.NumArguments == 3 && refl.ThreadgroupSize[0] == 64);
    struct MTL_SM50_SHADER_ARGUMENT cb[2], args[3];
    struct sm50_get_arguments_info_params ai = {shader, cb, args};
    CHECK(call(88, &ai) == 0 && checked[88] && ((uint8_t *)cb)[31] == 0xc1 && ((uint8_t *)args)[47] == 0xa3);
    CHECK(ai.constant_buffers == cb && ai.arguments == args);
    /* A guest claiming more than the reflection said: refused on the host. */
    static uint8_t r[256];
    struct sm50_get_arguments_info_params forged = {shader, (void *)8, (void *)8};
    memcpy(r, &forged, sizeof forged);
    struct mn_wm_rhdr h1 = {5 * 16, MN_RH_INLINE}, h2 = {3 * 16, MN_RH_INLINE};
    memcpy(r + 24, &h1, 8);
    memcpy(r + 32, &h2, 8);
    uint32_t n;
    CHECK(mn_wm_host_call(&ctx, ring_state, 88, r, 40, reply, sizeof reply, &n) == (int32_t)MN_STATUS_INVALID_PARAMETER);
    /* 79 with no room for the NUL is refused, on both sides. */
    char msg[64];
    struct sm50_get_error_message_params em = {(sm50_error_t)shader, msg, 0, 0};
    CHECK(call(79, &em) == MN_STATUS_INVALID_PARAMETER);
    memcpy(r, &em, sizeof em);
    CHECK(mn_wm_host_call(&ctx, ring_state, 79, r, sizeof em, reply, sizeof reply, &n) ==
          (int32_t)MN_STATUS_INVALID_PARAMETER);
}

static void sm50_args(void) {
    struct SM50_IA_INPUT_ELEMENT elements[2] = {{.reg = 1}, {.reg = 7}};
    struct SM50_SHADER_IA_INPUT_LAYOUT_DATA ia = {.type = SM50_SHADER_IA_INPUT_LAYOUT, .num_elements = 2,
                                                  .elements = elements};
    struct SM50_SHADER_COMMON_DATA common = {.next = &ia, .type = SM50_SHADER_COMMON,
                                             .metal_version = SM50_SHADER_METAL_320};
    sm50_bitcode_t bc = NULL;
    sm50_error_t err = NULL;
    struct sm50_compile_params c = {shader, &common, "main", &bc, &err, -1};
    CHECK(call(76, &c) == 0 && c.ret == 0 && bc && !err && checked[76]);
    struct SM50_COMPILED_BITCODE out = {0};
    struct sm50_get_compiled_bitcode_params gb = {bc, &out};
    for (size_t i = 0; i < sizeof bitcode_bytes; i++) bitcode_bytes[i] = (uint8_t)(i ^ 0x5a);
    CHECK(call(77, &gb) == 0 && checked[77] && out.Size == sizeof bitcode_bytes && out.Data &&
          out.Data != bitcode_bytes && !memcmp(out.Data, bitcode_bytes, sizeof bitcode_bytes));
    struct sm50_destroy_bitcode_params db = {bc};
    CHECK(call(78, &db) == 0);
    mn_ht_sweep(ctx.ht, ring_state);
    CHECK(sm50_released[1] == 1);
    /* A list that loops is refused before it leaves the guest. */
    ia.next = &common;
    CHECK(call(76, &c) == MN_STATUS_INVALID_PARAMETER);
    ia.next = NULL;
    struct sm50_destroy_params ds = {shader};
    CHECK(call(75, &ds) == 0);
    mn_ht_sweep(ctx.ht, ring_state);
    CHECK(sm50_released[0] == 1);
}

static void paths(void) {
    char a[1024], b[1024];
    struct unixcall_cache_alloc_init open = {{"dxmt/shaders.db"}, 3, 0};
    CHECK(call(115, &open) == 0 && open.ret_cache);
    snprintf(a, sizeof a, "%s", host_path);
    CHECK(call(117, &open) == 0 && !strcmp(a, host_path)); /* the same guest path: the same file */
    open.path.ptr = "dxmt/other.db";
    CHECK(call(115, &open) == 0 && strcmp(a, host_path));
    snprintf(b, sizeof b, "%s", host_path);
    const char *root = getenv("MN_WM_CACHE_DIR");
    CHECK(root && !strncmp(a, root, strlen(root)) && !strncmp(b, root, strlen(root)));
    CHECK(strstr(a, "shaders.db") && !strstr(a, "dxmt/")); /* named by the guest's file, never its directories */
    struct unixcall_setmetalcachepath cp = {{"/home/mn/.cache/dxmt"}, 0};
    CHECK(call(119, &cp) == 0 && cp.ret_success == 1 && !strncmp(host_path, root, strlen(root)));
    struct unixcall_mtldevice_newbinaryarchive na = {device, {NULL}, 0, 0};
    CHECK(call(112, &na) == 0 && na.ret_archive && !strcmp(host_path, "(null)")); /* in memory: no path */
}

static void strings(void) {
    struct unixcall_nsstring_string str = {{"héllo"}, WMTUTF8StringEncoding, 0};
    CHECK(call(60, &str) == 0 && str.ret && checked[60]);
    struct unixcall_generic_obj_uint64_uint64_ret len = {str.ret, WMTUTF8StringEncoding, 0};
    CHECK(call(27, &len) == 0 && len.ret == 6);
    checked[60] = 0;
    struct unixcall_nsstring_string str2 = {{"héllo"}, WMTUTF8StringEncoding, 0};
    CHECK(call(61, &str2) == 0 && str2.ret && checked[60]);
}

static void enumerate(void) {
    obj_handle_t logs[4] = {0};
    struct unixcall_enumerate e = {device, 0, 4, {logs}, 0};
    CHECK(call(91, &e) == 0 && checked[91] && e.ret_read == 2 && logs[0] && logs[1] && !logs[2]);
    int ok;
    CHECK(mn_ht_get(ctx.ht, logs[1], MN_HK_OBJC, &ok) == (void *)OBJ(LOG1) && ok && retains[LOG1] == 1);
    CHECK(e.buffer.ptr == logs && e.buffer_size == 4);
}

/* Slots 153 and 154 are the guest's own: the host refuses them, queued or not, and never calls past the real
 * winemetal.so's 153 entries. */
static void local_refused(void) {
    uint8_t r[16] = {0}, out[64];
    uint32_t n = 0;
    for (unsigned slot = 153; slot <= 154; slot++) {
        uint64_t before = atomic_load(&ctx.failures);
        CHECK(mn_wm_host_call(&ctx, ring_state, slot, r, sizeof r, NULL, 0, &n) == (int32_t)MN_STATUS_INVALID_PARAMETER);
        CHECK(atomic_load(&ctx.failures) == before + 1);
        CHECK(mn_wm_host_call(&ctx, ring_state, slot, r, sizeof r, out, sizeof out, &n) ==
              (int32_t)MN_STATUS_INVALID_PARAMETER);
        CHECK(atomic_load(&ctx.failures) == before + 1 && n == 0);
    }
}

/* A slot 91 the host fails: the log handles the guest picked for it (the next fresh indexes) are freed. */
static void enumerate_fails(void) {
    mn_handle probe = mn_gh_alloc(MN_OWN_PLUS1);
    obj_handle_t logs[4] = {0};
    struct unixcall_enumerate e = {device, 0, 4, {logs}, 0};
    fail_enumerate = 1;
    CHECK(call(91, &e) == MN_STATUS_UNSUCCESSFUL);
    fail_enumerate = 0;
    for (uint32_t i = 1; i <= 4; i++) CHECK(mn_gh_retain(1ull << 32 | (MN_H_INDEX(probe) + i)) == -1);
    mn_gh_drop(probe);
}

static void constants(void) {
    uint8_t yes = 1;
    int32_t answer = 42;
    struct WMTFunctionConstant c[2] = {{{&yes}, WMTDataTypeBool, 0, 0}, {{&answer}, WMTDataTypeInt, 5, 0}};
    struct unixcall_mtllibrary_newfunction_with_constants nf = {queue, {"frag"}, {c}, 2, 0, 0};
    nf.library = device; /* any live object stands in for the library */
    CHECK(call(98, &nf) == 0 && nf.ret && !nf.ret_error && checked[98]);
    CHECK(c[0].data.ptr == &yes);
    struct WMTFunctionConstant bad = {{&yes}, (enum WMTDataType)2 /* Array */, 0, 0};
    nf.constants.ptr = &bad, nf.num_constants = 1;
    CHECK(call(98, &nf) == MN_STATUS_INVALID_PARAMETER);
}

/* The params struct sizes, from the call catalog's table (its own clang run on DXMT's headers). */
static const uint32_t catalog_sizes[MN_WM_SLOTS] = {
    8, 8, 24, 16, 8, 16, 16, 16, 40, 24, 8, 16, 8, 8, 16, 16, 16, 24, 24, 24, 24, 24, 40, 48, 24, 32, 24, 24, 16, 32,
    16, 24, 24, 8, 32, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 96, 24, 16, 24, 24, 16, 24, 16, 8, 24, 8, 24, 24, 80,
    40, 24, 24, 8, 32, 24, 16, 16, 16, 16, 16, 16, 16, 32, 8, 48, 8, 48, 16, 8, 32, 8, 56, 56, 0, 56, 56, 16, 16, 24,
    16, 16, 40, 16, 24, 8, 8, 16, 16, 48, 32, 24, 40, 24, 16, 32, 16, 16, 32, 8, 8, 8, 24, 32, 24, 24, 24, 32, 24, 32,
    16, 24, 136, 136, 16, 24, 16, 32, 24, 32, 32, 24, 32, 32, 24, 24, 8, 8, 16, 24, 32, 24, 32, 32, 40, 40, 96, 40,
    40, 24, 32, 16, 24, 72,
    16, 16, /* DXMT 0071's WMTMemoryAlloc and WMTMemoryFree */
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
    56, 62, 89, 115, 116, 127, 151,                             /* nil is an answer */
    74, 76, 81, 82, 84, 85,                                     /* SM50 with outputs */
    13, 117, 126,                                               /* blocking */
    118,                       /* the guest's lock on its cache writer must cover the host's insert (not thread-safe) */
    67, 71, 72, 93, 97, 101,                                    /* presentation (4c): nil, layer props, display */
};
static const unsigned refused_slots[] = {120, 121, 122, 123, 124, 83};
static const unsigned local_slots[] = {153, 154};

static void modes(void) {
    int want[MN_WM_SLOTS] = {0}, n[4] = {0};
    for (size_t i = 0; i < sizeof sync_slots / sizeof *sync_slots; i++) want[sync_slots[i]] = MN_WM_SYNC;
    for (size_t i = 0; i < sizeof refused_slots / sizeof *refused_slots; i++) want[refused_slots[i]] = MN_WM_REFUSED;
    for (size_t i = 0; i < sizeof local_slots / sizeof *local_slots; i++) want[local_slots[i]] = MN_WM_LOCAL;
    for (unsigned s = 0; s < MN_WM_SLOTS; s++) {
        n[mn_wm_calls[s].mode]++;
        if (mn_wm_calls[s].mode != want[s]) {
            fprintf(stderr, "  slot %u: mode %d, want %d\n", s, mn_wm_calls[s].mode, want[s]);
            failures++;
        }
    }
    CHECK(n[MN_WM_SYNC] == 82 && n[MN_WM_QUEUED] == 65 && n[MN_WM_REFUSED] == 6 && n[MN_WM_LOCAL] == 2);
}

int main(void) {
    @autoreleasepool {
        mn_wm_funcs = fakes;
        mn_open(); /* the loopback's context */
        struct { const char *name; void (*fn)(void); } tests[] = {
            {"roundtrip", roundtrip}, {"bad-handle", bad_handle}, {"create-fails", create_fails},
            {"nil-answer", nil_answer}, {"exception", exception}, {"pool", pool},
            {"cmdlist-roundtrip", cmdlist_roundtrip}, {"cmdlist-loop", cmdlist_loop},
            {"cmdlist-unknown-type", cmdlist_unknown_type}, {"sm50-sizes", sm50_sizes}, {"sm50-args", sm50_args},
            {"paths", paths}, {"strings", strings}, {"enumerate", enumerate}, {"enumerate-fails", enumerate_fails},
            {"local-refused", local_refused},
            {"constants", constants},
            {"coverage", coverage},
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
