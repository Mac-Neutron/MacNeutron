/* The special slots that are neither memory, events nor command lists (plan Task 8):
 *   SM50 (74-88)  shaders, bitcode and errors are handles of their own kinds, returned through the caller's
 *                 out-pointers; a shader's reflection counts stay with it on the host, and slot 88 sizes its outputs
 *                 from them; argument lists travel flat; bitcode comes back in two steps (its size, then its bytes)
 *   91            the guest picks up to 64 handles for the logs, the host binds as many as it reads
 *   98            function constants' values travel by type (scalars only)
 *   paths         54, 112, 113, 115, 117, 119 name a file in the host's cache directory instead of the guest's path
 * In a reply, a field that held an out-pointer carries the guest handle it got (0: none). */
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "airconv_thunks.h"
#include "marshal.h"
#include "winemetal_thunks.h"
#ifndef MN_WM_HOST
#include "../guest/transport.h"
#include "guest/galloc.h"
#endif

#define PAD8(n) (((n) + 7u) & ~(uint64_t)7)
#define MAX_ARGS 64
#define MAX_LOGS 64
#define MAX_MESSAGE (1u << 20)

/* The compile slots' layouts: 76 has one shader, the pipeline compiles two. */
struct compile_layout { uint8_t shaders, args, name, bitcode, error, ret; };
static const struct compile_layout *compile_layout(unsigned slot) {
    static const struct compile_layout one = {1, offsetof(struct sm50_compile_params, args),
                                              offsetof(struct sm50_compile_params, func_name),
                                              offsetof(struct sm50_compile_params, bitcode),
                                              offsetof(struct sm50_compile_params, error),
                                              offsetof(struct sm50_compile_params, ret)};
#define TWO(st, a) {2, offsetof(struct st, a), offsetof(struct st, func_name), offsetof(struct st, bitcode),          \
                    offsetof(struct st, error), offsetof(struct st, ret)}
    static const struct compile_layout vertex = TWO(sm50_compile_geometry_pipeline_vertex_params, vertex_args);
    static const struct compile_layout geometry = TWO(sm50_compile_geometry_pipeline_geometry_params, geometry_args);
    static const struct compile_layout hull = TWO(sm50_compile_tessellation_pipeline_hull_params, hull_args);
    static const struct compile_layout domain = TWO(sm50_compile_tessellation_pipeline_domain_params, domain_args);
    switch (slot) {
    case 76: return &one;
    case 81: return &vertex;
    case 82: return &geometry;
    case 84: return &hull;
    case 85: return &domain;
    default: return NULL;
    }
}

/* SM50 compile arguments: a list through next (offset 0), its type at offset 8. */
struct arg_type { uint8_t size, payload, count, count_width, elem; };
static const struct arg_type arg_types[] = {
    [SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT] = {sizeof(struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA),
        offsetof(struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, elements),
        offsetof(struct SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA, num_elements), 4,
        sizeof(struct SM50_STREAM_OUTPUT_ELEMENT)},
    [SM50_SHADER_COMMON] = {.size = sizeof(struct SM50_SHADER_COMMON_DATA)},
    [SM50_SHADER_PSO_PIXEL_SHADER] = {.size = sizeof(struct SM50_SHADER_PSO_PIXEL_SHADER_DATA)},
    [SM50_SHADER_IA_INPUT_LAYOUT] = {sizeof(struct SM50_SHADER_IA_INPUT_LAYOUT_DATA),
        offsetof(struct SM50_SHADER_IA_INPUT_LAYOUT_DATA, elements),
        offsetof(struct SM50_SHADER_IA_INPUT_LAYOUT_DATA, num_elements), 4, sizeof(struct SM50_IA_INPUT_ELEMENT)},
    [SM50_SHADER_GS_PASS_THROUGH] = {.size = sizeof(struct SM50_SHADER_GS_PASS_THROUGH_DATA)},
    [SM50_SHADER_PSO_GEOMETRY_SHADER] = {.size = sizeof(struct SM50_SHADER_PSO_GEOMETRY_SHADER_DATA)},
    [SM50_SHADER_PSO_TESSELLATOR] = {.size = sizeof(struct SM50_SHADER_PSO_TESSELLATOR_DATA)},
    [SM50_SHADER_ROOT_SIGNATURE] = {sizeof(struct SM50_SHADER_ROOT_SIGNATURE_DATA),
        offsetof(struct SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode),
        offsetof(struct SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode_length), 8, 1},
    [SM50_SHADER_ROOT_SIGNATURE2] = {sizeof(struct SM50_SHADER_ROOT_SIGNATURE_DATA),
        offsetof(struct SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode),
        offsetof(struct SM50_SHADER_ROOT_SIGNATURE_DATA, bytecode_length), 8, 1},
};
#define NARG_TYPES (sizeof arg_types / sizeof *arg_types)

static inline const uint8_t *next_arg(const uint8_t *node) {
    const uint8_t *next;
    memcpy(&next, node, sizeof next);
    return next;
}

static const struct arg_type *arg_type(const uint8_t *node) {
    uint32_t type;
    memcpy(&type, node + 8, 4);
    return type < NARG_TYPES && arg_types[type].size ? &arg_types[type] : NULL;
}

/* An argument's payload bytes, or UINT64_MAX if it overflows. */
static uint64_t arg_payload(const struct arg_type *t, const uint8_t *node) {
    if (!t->payload) return 0;
    uint64_t n = 0;
    memcpy(&n, node + t->count, t->count_width);
    return n > UINT32_MAX / t->elem ? UINT64_MAX : n * t->elem;
}

/* A WMTDataType's bytes, for slot 98: scalars only (DXMT sets bools; WMTDataType has no 64-bit ones). */
static uint32_t data_type_size(uint16_t type) {
    switch (type) {
    case WMTDataTypeBool: case WMTDataTypeChar: case WMTDataTypeUChar: return 1;
    case WMTDataTypeShort: case WMTDataTypeUShort: case WMTDataTypeHalf: return 2;
    case WMTDataTypeInt: case WMTDataTypeUInt: case WMTDataTypeFloat: return 4;
    default: return 0;
    }
}

/* ---- guest ---- */
#ifndef MN_WM_HOST

/* Picks a handle the call returns, through out-pointer at (NULL: the caller doesn't want it). */
static mn_handle pick(struct mn_wm_out *o, void *at) {
    if (!at || o->nouts == MN_WM_MAX_OUTS) return 0;
    mn_handle h = mn_gh_alloc(MN_OWN_PLUS1);
    if (h) {
        o->out_at[o->nouts] = at;
        o->outs[o->nouts++] = h;
    }
    return h;
}

/* The argument list, flat: {u32 count, u32 0}, the nodes (next zeroed, a payload pointer its offset or ~0), then
 * the payloads. A longer or looping list is refused. */
static NTSTATUS_T emit_args(struct mn_wm_out *o, const uint8_t *head) {
    uint64_t count = 0, total = 8;
    for (const uint8_t *n = head; n; n = next_arg(n)) {
        const struct arg_type *t = arg_type(n);
        uint64_t pay = t ? arg_payload(t, n) : UINT64_MAX;
        if (++count > MAX_ARGS || !t || pay == UINT64_MAX) return MN_STATUS_INVALID_PARAMETER;
        total += PAD8(t->size) + PAD8(pay);
    }
    if (!count) return mn_wm_emit_none(o);
    uint8_t *w = calloc(1, total);
    if (!w) return MN_STATUS_UNSUCCESSFUL;
    uint32_t c32 = (uint32_t)count;
    memcpy(w, &c32, 4);
    uint64_t at = 8, nodes = 0;
    for (const uint8_t *n = head; n; n = next_arg(n)) nodes += PAD8(arg_type(n)->size);
    uint64_t pat = 8 + nodes;
    for (const uint8_t *n = head; n; n = next_arg(n)) {
        const struct arg_type *t = arg_type(n);
        memcpy(w + at, n, t->size);
        memset(w + at, 0, 8); /* next */
        if (t->payload) {
            uint64_t len = arg_payload(t, n), off = ~0ull;
            const void *src;
            memcpy(&src, n + t->payload, sizeof src);
            if (len) {
                if (!src || pat + len > total) {
                    free(w);
                    return MN_STATUS_INVALID_PARAMETER;
                }
                memcpy(w + pat, src, len);
                off = pat;
                pat += PAD8(len);
            }
            memcpy(w + at + t->payload, &off, 8);
        }
        at += PAD8(t->size);
    }
    NTSTATUS_T s = mn_wm_emit_data(o, w, total, WMF_IN);
    free(w);
    return s;
}

static NTSTATUS_T emit_string(struct mn_wm_out *o, const char *str) {
    return str ? mn_wm_emit_data(o, str, strlen(str) + 1, WMF_IN) : mn_wm_emit_none(o);
}

static void *field_ptr(const void *params, unsigned off) {
    void *p;
    memcpy(&p, (const uint8_t *)params + off, sizeof p);
    return p;
}
static void set_field(void *params, unsigned off, uint64_t v) { memcpy((uint8_t *)params + off, &v, sizeof v); }

/* A release of an SM50 handle (75, 78, 80): the table's, on both sides. */
static NTSTATUS_T release_pack(void *params, struct mn_wm_out *o, int bitcode) {
    mn_handle h;
    memcpy(&h, params, sizeof h);
    uint64_t att = mn_gh_attached(h);
    if (bitcode && MN_GH_TAG(att) == MN_GH_PTR) free((void *)(uintptr_t)MN_GH_VALUE(att)); /* slot 77's copy */
    if (mn_gh_release(h) < 0) return MN_STATUS_INVALID_HANDLE;
    return mn_wm_emit(o, &h, sizeof h);
}

NTSTATUS_T mn_wm_sm50_pack(unsigned slot, void *params, struct mn_wm_out *o) {
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    uint8_t rec[64];
    memcpy(rec, params, c->params_size);
    NTSTATUS_T s;
    switch (slot) {
    case 74: {
        struct sm50_initialize_params *a = params, *r = (void *)rec;
        mn_handle hs = pick(o, a->shader), he = pick(o, a->error);
        if (!hs) return MN_STATUS_INVALID_PARAMETER;
        r->shader = (void *)(uintptr_t)hs, r->error = (void *)(uintptr_t)he, r->bytecode = NULL;
        r->reflection = a->reflection ? (void *)1 : NULL;
        if ((s = mn_wm_emit(o, rec, c->params_size)) ||
            (s = mn_wm_emit_data(o, a->bytecode, a->bytecode_size, WMF_IN)) ||
            (s = mn_wm_emit_data(o, a->reflection, sizeof(struct MTL_SHADER_REFLECTION), WMF_OUT)))
            return s;
        return 0;
    }
    case 75:
    case 80: return release_pack(params, o, 0);
    case 78: return release_pack(params, o, 1);
    case 77: { /* the size first; the bytes come in the second step (sm50_unpack) */
        struct sm50_get_compiled_bitcode_params *a = params;
        if (a->data_out) memset(a->data_out, 0, sizeof *a->data_out); /* a failure: DXMT sees no bitcode */
        set_field(rec, offsetof(struct sm50_get_compiled_bitcode_params, data_out), 0);
        return mn_wm_emit(o, rec, c->params_size);
    }
    case 79: {
        struct sm50_get_error_message_params *a = params;
        if (!a->buffer_size || a->buffer_size > MAX_MESSAGE || !a->buffer) return MN_STATUS_INVALID_PARAMETER;
        if ((s = mn_wm_emit(o, rec, c->params_size))) return s;
        return mn_wm_emit_data(o, a->buffer, a->buffer_size, WMF_OUT);
    }
    case 88: {
        struct sm50_get_arguments_info_params *a = params;
        uint64_t att = mn_gh_attached((mn_handle)(uintptr_t)a->shader);
        if (MN_GH_TAG(att) != MN_GH_FORMAT) return MN_STATUS_INVALID_PARAMETER; /* its counts, from slot 74 */
        uint64_t ncb = MN_GH_VALUE(att) >> 31 & 0x7fffffff, nargs = MN_GH_VALUE(att) & 0x7fffffff;
        if ((s = mn_wm_emit(o, rec, c->params_size)) ||
            (s = mn_wm_emit_data(o, a->constant_buffers, ncb * sizeof(struct MTL_SM50_SHADER_ARGUMENT), WMF_OUT)))
            return s;
        return mn_wm_emit_data(o, a->arguments, nargs * sizeof(struct MTL_SM50_SHADER_ARGUMENT), WMF_OUT);
    }
    default: {
        const struct compile_layout *l = compile_layout(slot);
        if (!l) return MN_STATUS_NOT_IMPLEMENTED;
        mn_handle hb = pick(o, field_ptr(params, l->bitcode)), he = pick(o, field_ptr(params, l->error));
        if (!hb) return MN_STATUS_INVALID_PARAMETER;
        set_field(rec, l->bitcode, hb), set_field(rec, l->error, he), set_field(rec, l->args, 0),
            set_field(rec, l->name, 0);
        if ((s = mn_wm_emit(o, rec, c->params_size)) || (s = emit_args(o, field_ptr(params, l->args))) ||
            (s = emit_string(o, field_ptr(params, l->name))))
            return s;
        return 0;
    }
    }
}

/* The handles a reply's out-pointer fields carry: through the caller's out-pointers, a 0 freeing its index. */
static void give_outs(const struct mn_wm_out *o, const uint8_t *reply, const unsigned *offs) {
    for (unsigned i = 0; i < o->nouts; i++) {
        mn_handle h;
        memcpy(&h, reply + offs[i], sizeof h);
        if (!h) mn_gh_drop(o->outs[i]);
        uint64_t v = h;
        memcpy(o->out_at[i], &v, sizeof v);
    }
}

NTSTATUS_T mn_wm_sm50_unpack(unsigned slot, void *params, const struct mn_wm_out *o, const void *reply, uint32_t len) {
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    const uint8_t *r = reply;
    uint64_t pos = PAD8(c->params_size);
    if (len < pos) {
        mn_wm_unwind(o, 1);
        return MN_STATUS_UNSUCCESSFUL;
    }
    NTSTATUS_T s = 0;
    switch (slot) {
    case 74: {
        const struct sm50_initialize_params *rp = reply;
        struct sm50_initialize_params *a = params;
        unsigned offs[2] = {offsetof(struct sm50_initialize_params, shader), offsetof(struct sm50_initialize_params, error)};
        mn_handle hs = (mn_handle)(uintptr_t)rp->shader;
        give_outs(o, r, offs);
        if (hs) mn_gh_attach(hs, MN_GH_FORMAT | rp->bytecode_size); /* the reflection's counts */
        a->ret = rp->ret;
        s = mn_wm_copy_back(o, r, len, pos);
        break;
    }
    case 77: { /* now the bytes, into memory of ours the caller reads until slot 78 (a 32-bit caller too: DXMT passes
              * Data on as a 64-bit value, never dereferencing it) */
        struct sm50_get_compiled_bitcode_params *a = params;
        uint64_t size;
        memcpy(&size, r + offsetof(struct sm50_get_compiled_bitcode_params, data_out), sizeof size);
        uint8_t *buf = size <= (64ull << 20) ? malloc(size ? size : 1) : NULL;
        if (!buf) {
            s = MN_STATUS_UNSUCCESSFUL;
            break;
        }
        mn_wm_scratch_done();
        static _Thread_local uint8_t rec2[64], reply2[MN_REPLY_BYTES];
        struct mn_wm_out o2 = {.buf = rec2, .cap = sizeof rec2};
        struct sm50_get_compiled_bitcode_params q = {a->bitcode, (void *)(uintptr_t)size};
        uint32_t n = 0;
        if ((s = mn_wm_emit(&o2, &q, sizeof q)) || (s = mn_wm_emit_data(&o2, buf, size, WMF_OUT)) ||
            (s = (NTSTATUS_T)mn_call(MN_OP_WM + 77, o2.buf, o2.len, reply2, sizeof reply2, &n)) ||
            (s = mn_wm_copy_back(&o2, reply2, n, PAD8(sizeof q)))) {
            free(buf);
            break;
        }
        mn_handle h = (mn_handle)(uintptr_t)a->bitcode;
        uint64_t old = mn_gh_attached(h);
        if (MN_GH_TAG(old) == MN_GH_PTR) free((void *)(uintptr_t)MN_GH_VALUE(old));
        mn_gh_attach(h, MN_GH_PTR | (uintptr_t)buf);
        a->data_out->Data = buf;
        a->data_out->Size = size;
        break;
    }
    case 79: {
        struct sm50_get_error_message_params *a = params;
        a->ret_size = ((const struct sm50_get_error_message_params *)reply)->ret_size;
        s = mn_wm_copy_back(o, r, len, pos);
        if (a->buffer_size) a->buffer[a->buffer_size - 1] = 0;
        break;
    }
    case 88: s = mn_wm_copy_back(o, r, len, pos); break;
    default: {
        const struct compile_layout *l = compile_layout(slot);
        unsigned offs[2] = {l->bitcode, l->error};
        give_outs(o, r, offs);
        memcpy((uint8_t *)params + l->ret, r + l->ret, sizeof(int));
        break;
    }
    }
    mn_wm_scratch_done();
    return s;
}

/* 91: up to MAX_LOGS handles picked now, sent for the host to bind; the reply says how many it read. */
static _Thread_local mn_handle logs[MAX_LOGS];
static _Thread_local unsigned nlogs;

NTSTATUS_T mn_wm_enumerate_pack(void *params, struct mn_wm_out *o) {
    struct unixcall_enumerate *a = params;
    struct unixcall_enumerate rec = *a;
    nlogs = a->buffer_size < MAX_LOGS ? (unsigned)a->buffer_size : MAX_LOGS; /* the caller asks again from start */
    if (nlogs && !a->buffer.ptr) return MN_STATUS_INVALID_PARAMETER;
    for (unsigned i = 0; i < nlogs; i++)
        if (!(logs[i] = mn_gh_alloc(MN_OWN_AUTORELEASED))) {
            while (i--) mn_gh_drop(logs[i]);
            return MN_STATUS_UNSUCCESSFUL;
        }
    rec.buffer_size = nlogs;
    NTSTATUS_T s = mn_wm_emit(o, &rec, sizeof rec);
    if (!s) s = mn_wm_emit_data(o, logs, nlogs * sizeof *logs, WMF_IN);
    if (s) mn_wm_enumerate_unwind();
    return s;
}

void mn_wm_enumerate_unwind(void) {
    for (unsigned i = 0; i < nlogs; i++) mn_gh_drop(logs[i]);
    nlogs = 0;
}

NTSTATUS_T mn_wm_enumerate_unpack(void *params, const struct mn_wm_out *o, const void *reply, uint32_t len) {
    (void)o;
    struct unixcall_enumerate *a = params;
    uint64_t read = len >= sizeof *a ? ((const struct unixcall_enumerate *)reply)->ret_read : 0;
    if (read > nlogs) read = nlogs;
    obj_handle_t *out = a->buffer.ptr;
    for (unsigned i = 0; i < nlogs; i++)
        if (i < read) out[i] = logs[i];
        else mn_gh_drop(logs[i]);
    nlogs = 0;
    a->ret_read = read;
    mn_wm_scratch_done();
    return 0;
}

/* 98: the constants, flat: the array (data pointers as offsets into it), then each value. */
NTSTATUS_T mn_wm_constants_emit(void *params, struct mn_wm_out *o) {
    struct unixcall_mtllibrary_newfunction_with_constants *a = params;
    const struct WMTFunctionConstant *c = a->constants.ptr;
    if (a->num_constants > 1024 || (a->num_constants && !c)) return MN_STATUS_INVALID_PARAMETER;
    NTSTATUS_T s = emit_string(o, a->name.ptr);
    if (s) return s;
    if (!a->num_constants) return mn_wm_emit_none(o);
    uint64_t total = a->num_constants * sizeof *c;
    for (uint64_t i = 0; i < a->num_constants; i++) {
        uint32_t n = data_type_size(c[i].type);
        if (!n || !c[i].data.ptr) return MN_STATUS_INVALID_PARAMETER;
        total += 8; /* each value padded to 8 */
    }
    uint8_t *w = calloc(1, total);
    if (!w) return MN_STATUS_UNSUCCESSFUL;
    uint64_t at = a->num_constants * sizeof *c;
    for (uint64_t i = 0; i < a->num_constants; i++) {
        struct WMTFunctionConstant k = c[i];
        memcpy(w + at, c[i].data.ptr, data_type_size(c[i].type));
        k.data.ptr = (const void *)(uintptr_t)at;
        memcpy(w + i * sizeof k, &k, sizeof k);
        at += 8;
    }
    s = mn_wm_emit_data(o, w, total, WMF_IN);
    free(w);
    return s;
}

#endif

/* ---- host ---- */
#ifndef MN_WM_GUEST

struct shader { void *real; uint32_t ncb, nargs; };

void mn_wm_sm50_release(enum mn_hkind kind, void *obj) {
    switch (kind) {
    case MN_HK_SM50_SHADER: {
        struct shader *w = obj;
        struct sm50_destroy_params d = {w->real};
        mn_wm_invoke(75, &d);
        free(w);
        return;
    }
    case MN_HK_SM50_BITCODE: {
        struct sm50_destroy_bitcode_params d = {obj};
        mn_wm_invoke(78, &d);
        return;
    }
    default: {
        struct sm50_free_error_params d = {obj};
        mn_wm_invoke(80, &d);
        return;
    }
    }
}

static void *get(struct mn_wm_state *k, uint64_t h, enum mn_hkind kind) {
    int ok;
    void *o = h ? mn_ht_get(k->c->ht, h, kind, &ok) : NULL;
    return h && ok ? o : NULL;
}

/* The guest's flat argument list, rebuilt with host pointers. */
static NTSTATUS_T take_args(struct mn_wm_state *k, void **head) {
    uint8_t *r;
    uint32_t len;
    NTSTATUS_T s = mn_wm_take(k, WMF_IN, UINT64_MAX, 1, &r, &len);
    *head = NULL;
    if (s || !r) return s;
    uint32_t count;
    if (len < 8) return MN_STATUS_INVALID_PARAMETER;
    memcpy(&count, r, 4);
    if (!count || count > MAX_ARGS) return MN_STATUS_INVALID_PARAMETER;
    uint64_t at = 8;
    uint8_t *prev = NULL;
    for (uint32_t i = 0; i < count; i++) {
        if (len - at < 16) return MN_STATUS_INVALID_PARAMETER;
        const struct arg_type *t = arg_type(r + at);
        if (!t || t->size > len - at) return MN_STATUS_INVALID_PARAMETER;
        uint8_t *n = mn_wm_alloc(k, t->size);
        if (!n) return MN_STATUS_UNSUCCESSFUL;
        memcpy(n, r + at, t->size);
        memset(n, 0, 8);
        if (t->payload) {
            uint64_t plen = arg_payload(t, n), off;
            void *p = NULL;
            memcpy(&off, n + t->payload, 8);
            if (plen == UINT64_MAX) return MN_STATUS_INVALID_PARAMETER;
            if (plen) {
                if (off < 8 || off > len || plen > len - off) return MN_STATUS_INVALID_PARAMETER;
                p = r + off;
            }
            memcpy(n + t->payload, &p, 8);
        }
        if (prev) memcpy(prev, &n, sizeof n);
        else *head = n;
        prev = n;
        at += PAD8(t->size);
        if (at > len) return MN_STATUS_INVALID_PARAMETER;
    }
    return 0;
}

NTSTATUS_T mn_wm_sm50(struct mn_wm_state *k) {
    NTSTATUS_T s;
    switch (k->slot) {
    case 74: {
        struct sm50_initialize_params *a = (void *)k->p;
        mn_handle hs = (mn_handle)(uintptr_t)a->shader, he = (mn_handle)(uintptr_t)a->error;
        uint8_t *code, *refl_out;
        uint32_t code_len, refl_len;
        if ((s = mn_wm_take(k, WMF_IN, UINT64_MAX, 0, &code, &code_len)) ||
            (s = mn_wm_take(k, WMF_OUT, sizeof(struct MTL_SHADER_REFLECTION), 0, &refl_out, &refl_len)))
            return s;
        if (code_len != a->bytecode_size) return MN_STATUS_INVALID_PARAMETER;
        struct MTL_SHADER_REFLECTION refl = {0};
        sm50_shader_t shader = NULL;
        sm50_error_t err = NULL;
        struct sm50_initialize_params r = {code, code_len, &shader, &refl, he ? &err : NULL, -1};
        if ((s = mn_wm_invoke(74, &r))) return s;
        if (refl_out) memcpy(refl_out, &refl, sizeof refl);
        struct shader *w = shader ? malloc(sizeof *w) : NULL;
        if (w) *w = (struct shader){shader, refl.NumConstantBuffers & 0x7fffffff, refl.NumArguments & 0x7fffffff};
        else if (shader) {
            struct sm50_destroy_params d = {shader};
            mn_wm_invoke(75, &d);
        }
        a->shader = (void *)(uintptr_t)(w ? mn_wm_bind_out(k, hs, MN_HK_SM50_SHADER, w, MN_OWN_PLUS1) : 0);
        a->error = (void *)(uintptr_t)(err ? mn_wm_bind_out(k, he, MN_HK_SM50_ERROR, err, MN_OWN_PLUS1) : 0);
        a->ret = r.ret;
        a->bytecode = NULL, a->reflection = NULL;
        a->bytecode_size = w ? (uint64_t)w->ncb << 31 | w->nargs : 0; /* for the guest's slot 88 */
        return 0;
    }
    case 75:
    case 78:
    case 80: {
        enum mn_hkind kind = k->slot == 75 ? MN_HK_SM50_SHADER : k->slot == 78 ? MN_HK_SM50_BITCODE : MN_HK_SM50_ERROR;
        mn_handle h;
        memcpy(&h, k->p, sizeof h);
        if (!get(k, h, kind)) return MN_STATUS_INVALID_HANDLE;
        return mn_ht_release(k->c->ht, k->ring, h) ? MN_STATUS_INVALID_HANDLE : 0;
    }
    case 77: {
        struct sm50_get_compiled_bitcode_params *a = (void *)k->p;
        void *bc = get(k, (mn_handle)(uintptr_t)a->bitcode, MN_HK_SM50_BITCODE);
        uint64_t want = (uint64_t)(uintptr_t)a->data_out;
        if (!bc) return MN_STATUS_INVALID_HANDLE;
        struct SM50_COMPILED_BITCODE out = {0};
        struct sm50_get_compiled_bitcode_params r = {bc, &out};
        if ((s = mn_wm_invoke(77, &r))) return s;
        if (want) { /* the second step: the bytes */
            uint8_t *dst;
            uint32_t n;
            if ((s = mn_wm_take(k, WMF_OUT, UINT64_MAX, 0, &dst, &n))) return s;
            if (!dst || n != want || want != out.Size) return MN_STATUS_INVALID_PARAMETER;
            memcpy(dst, out.Data, n);
        }
        a->data_out = (void *)(uintptr_t)out.Size;
        return 0;
    }
    case 79: {
        struct sm50_get_error_message_params *a = (void *)k->p;
        if (!a->buffer_size) return MN_STATUS_INVALID_PARAMETER; /* winemetal would write before the buffer */
        void *err = get(k, (mn_handle)(uintptr_t)a->error, MN_HK_SM50_ERROR);
        uint8_t *buf;
        uint32_t n;
        if (!err) return MN_STATUS_INVALID_HANDLE;
        if ((s = mn_wm_take(k, WMF_OUT, a->buffer_size, 0, &buf, &n))) return s;
        if (!buf) return MN_STATUS_INVALID_PARAMETER;
        struct sm50_get_error_message_params r = {err, (char *)buf, n, 0};
        if ((s = mn_wm_invoke(79, &r))) return s;
        a->ret_size = r.ret_size;
        a->buffer = NULL;
        return 0;
    }
    case 88: {
        struct sm50_get_arguments_info_params *a = (void *)k->p;
        struct shader *w = get(k, (mn_handle)(uintptr_t)a->shader, MN_HK_SM50_SHADER);
        uint8_t *cb, *args;
        uint32_t n1, n2;
        if (!w) return MN_STATUS_INVALID_HANDLE;
        /* the sizes the shader's own reflection gave, never the guest's */
        if ((s = mn_wm_take(k, WMF_OUT, (uint64_t)w->ncb * sizeof(struct MTL_SM50_SHADER_ARGUMENT), 0, &cb, &n1)) ||
            (s = mn_wm_take(k, WMF_OUT, (uint64_t)w->nargs * sizeof(struct MTL_SM50_SHADER_ARGUMENT), 0, &args, &n2)))
            return s;
        struct sm50_get_arguments_info_params r = {w->real, (void *)cb, (void *)args};
        return mn_wm_invoke(88, &r);
    }
    default: {
        const struct compile_layout *l = compile_layout(k->slot);
        if (!l) return MN_STATUS_NOT_IMPLEMENTED;
        uint8_t r[64];
        memset(r, 0, sizeof r);
        for (unsigned i = 0; i < l->shaders; i++) {
            mn_handle h;
            memcpy(&h, k->p + 8 * i, sizeof h);
            struct shader *w = get(k, h, MN_HK_SM50_SHADER);
            if (h && !w) return MN_STATUS_INVALID_HANDLE;
            void *real = w ? w->real : NULL;
            memcpy(r + 8 * i, &real, sizeof real);
        }
        void *args;
        uint8_t *name;
        uint32_t name_len;
        if ((s = take_args(k, &args)) || (s = mn_wm_take(k, WMF_STR_IN, UINT64_MAX, 1, &name, &name_len))) return s;
        mn_handle hb, he;
        memcpy(&hb, k->p + l->bitcode, 8);
        memcpy(&he, k->p + l->error, 8);
        sm50_bitcode_t bc = NULL;
        sm50_error_t err = NULL;
        void *pbc = &bc, *perr = he ? &err : NULL;
        memcpy(r + l->args, &args, 8);
        memcpy(r + l->name, &name, 8);
        memcpy(r + l->bitcode, &pbc, 8);
        memcpy(r + l->error, &perr, 8);
        int ret = -1;
        memcpy(r + l->ret, &ret, sizeof ret);
        if ((s = mn_wm_invoke(k->slot, r))) return s;
        uint64_t gb = bc ? mn_wm_bind_out(k, hb, MN_HK_SM50_BITCODE, bc, MN_OWN_PLUS1) : 0;
        uint64_t ge = err ? mn_wm_bind_out(k, he, MN_HK_SM50_ERROR, err, MN_OWN_PLUS1) : 0;
        memcpy(k->p + l->bitcode, &gb, 8);
        memcpy(k->p + l->error, &ge, 8);
        memcpy(k->p + l->ret, r + l->ret, sizeof ret);
        return 0;
    }
    }
}

NTSTATUS_T mn_wm_enumerate(struct mn_wm_state *k) {
    struct unixcall_enumerate *a = (void *)k->p;
    uint8_t *hv;
    uint32_t n;
    if (a->buffer_size > MAX_LOGS) return MN_STATUS_INVALID_PARAMETER;
    NTSTATUS_T s = mn_wm_take(k, WMF_IN, a->buffer_size * sizeof(mn_handle), 1, &hv, &n);
    if (s) return s;
    void **objs = mn_wm_alloc(k, (a->buffer_size ? a->buffer_size : 1) * sizeof *objs);
    if (!objs || (a->buffer_size && !hv)) return MN_STATUS_INVALID_PARAMETER;
    struct unixcall_enumerate r = *a;
    r.buffer.ptr = objs;
    if ((s = mn_wm_invoke(91, &r))) return s;
    uint64_t read = r.ret_read < a->buffer_size ? r.ret_read : a->buffer_size;
    for (uint64_t i = 0; i < read; i++) {
        mn_handle h;
        memcpy(&h, hv + i * sizeof h, sizeof h);
        mn_wm_bind_out(k, h, MN_HK_OBJC, objs[i], MN_OWN_AUTORELEASED);
    }
    a->ret_read = read;
    a->buffer.ptr = NULL;
    return 0;
}

NTSTATUS_T mn_wm_constants(struct mn_wm_state *k) {
    struct unixcall_mtllibrary_newfunction_with_constants *a = (void *)k->p;
    uint8_t *name, *w;
    uint32_t name_len, len;
    NTSTATUS_T s;
    if ((s = mn_wm_take(k, WMF_STR_IN, UINT64_MAX, 1, &name, &name_len)) ||
        (s = mn_wm_take(k, WMF_IN, UINT64_MAX, 1, &w, &len)))
        return s;
    uint64_t n = a->num_constants;
    if (!name || (n && !w) || n > 1024 || n * sizeof(struct WMTFunctionConstant) > len)
        return MN_STATUS_INVALID_PARAMETER;
    struct WMTFunctionConstant *c = (void *)w;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t off = (uint64_t)(uintptr_t)c[i].data.ptr;
        uint32_t size = data_type_size(c[i].type);
        if (!size || off < n * sizeof *c || off > len || size > len - off) return MN_STATUS_INVALID_PARAMETER;
        c[i].data.ptr = w + off;
    }
    a->name.ptr = name;
    a->constants.ptr = n ? c : NULL;
    return mn_wm_invoke(98, a);
}

/* ~/Library/Caches/MacNeutron/eac-vm/metal (MN_WM_CACHE_DIR for tests), made on first use. */
static const char *cache_root(void) {
    static char root[1024];
    static atomic_int made;
    if (atomic_load(&made)) return root;
    const char *env = getenv("MN_WM_CACHE_DIR"), *home = getenv("HOME");
    if (env) snprintf(root, sizeof root, "%s", env);
    else snprintf(root, sizeof root, "%s/Library/Caches/MacNeutron/eac-vm/metal", home ? home : "/tmp");
    for (char *p = root + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(root, 0755);
            *p = '/';
        }
    mkdir(root, 0755);
    atomic_store(&made, 1);
    return root;
}

/* The host file (or directory) a guest path names: in the cache root, a hash of the whole guest path, then the guest's
 * last component, so a cache stays the guest's from run to run. Never the guest's own path. */
static char *map_path(struct mn_wm_state *k, const char *guest) {
    uint64_t hash = 14695981039346656037ull;
    for (const char *p = guest; *p; p++) hash = (hash ^ (uint8_t)*p) * 1099511628211ull;
    const char *base = strrchr(guest, '/');
    base = base ? base + 1 : guest;
    char name[65];
    size_t n = 0;
    for (; base[n] && n < 64; n++) {
        char ch = base[n];
        name[n] = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                          ch == '_' || ch == '-'
                      ? ch
                      : '_';
    }
    name[n] = 0;
    char *out = mn_wm_alloc(k, 1200);
    if (out) snprintf(out, 1200, "%s/%016llx-%s", cache_root(), (unsigned long long)hash, n ? name : "x");
    return out;
}

static NTSTATUS_T map_field(struct mn_wm_state *k, const void **field) {
    if (!*field) return 0;
    char *m = map_path(k, *field);
    if (!m) return MN_STATUS_UNSUCCESSFUL;
    *field = m;
    return 0;
}

NTSTATUS_T mn_wm_paths(struct mn_wm_state *k) {
    NTSTATUS_T s = 0;
    switch (k->slot) {
    case 54: {
        struct unixcall_mtlcapturemanager_startcapture *a = (void *)k->p;
        struct WMTCaptureInfo *ci = a->info.ptr;
        if (ci) s = map_field(k, &ci->output_url.ptr);
        break;
    }
    case 112: s = map_field(k, &((struct unixcall_mtldevice_newbinaryarchive *)k->p)->url.ptr); break;
    case 113: s = map_field(k, &((struct unixcall_mtlbinaryarchive_serialize *)k->p)->url.ptr); break;
    case 115:
    case 117: s = map_field(k, &((struct unixcall_cache_alloc_init *)k->p)->path.ptr); break;
    case 119: s = map_field(k, &((struct unixcall_setmetalcachepath *)k->p)->path.ptr); break;
    default: return MN_STATUS_NOT_IMPLEMENTED;
    }
    return s ? s : mn_wm_invoke(k->slot, k->p);
}

#endif
