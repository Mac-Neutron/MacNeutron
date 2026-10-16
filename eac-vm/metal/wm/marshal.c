/* The marshaller (marshal.h): one walk over a slot's fields, depth first, on each side. The host reads every guest
 * value it checks from its own copy (params and regions), so nothing the guest changes in the ring afterwards reaches
 * a check; bulk bytes in the scratch blob are used in place. A special slot's own regions come right after the
 * params, before the generic ones. Every blob region a record names (how 3) gets exactly one MN_NOTIFY_REGION_FREE
 * from the host: when the Metal object made over it goes, or at once if the call fails. */
#include "marshal.h"
#include <stdatomic.h>
#include "cmdlist.h"
#include <stdlib.h>
#include <string.h>
#include "notify.h"
#include "winemetal_thunks.h"
#ifndef MN_WM_HOST
#include "../guest/transport.h"
#include "guest/galloc.h"
#endif
#ifndef MN_WM_GUEST
#include "../host/executor.h"
#endif

#define PAD8(n) (((n) + 7u) & ~(uint64_t)7)
#define MAX_DEPTH 4
#define STORAGE(options) ((uint64_t)(options) & 0xf0) /* MTLStorageMode << 4 */
#define STORAGE_PRIVATE 32u
#define STORAGE_MEMORYLESS 48u

static int is_ptr(uint8_t kind) {
    return kind == WMF_IN || kind == WMF_OUT || kind == WMF_INOUT || kind == WMF_STR_IN || kind == WMF_BLOB_PTR;
}

static uint64_t read_count(const uint8_t *base, const struct mn_wm_field *f) {
    switch (f->count_width) {
    case 1: return base[f->count_offset];
    case 4: { uint32_t v; memcpy(&v, base + f->count_offset, 4); return v; }
    default: { uint64_t v; memcpy(&v, base + f->count_offset, 8); return v; }
    }
}

/* The bytes a pointer field covers in base (a struct of this field's), or UINT64_MAX if it overflows. */
static uint64_t region_len(const uint8_t *base, const struct mn_wm_field *f, const void *ptr) {
    if (f->kind == WMF_STR_IN) return strlen(ptr) + 1;
    if (!(f->flags & WMF_ARRAY)) return f->size;
    uint64_t n = read_count(base, f);
    return f->size && n > UINT32_MAX / f->size ? UINT64_MAX : n * f->size;
}

static const struct mn_wm_nested *nested_of(const struct mn_wm_field *f) {
    return f->nested ? &mn_wm_nested[f->nested - 1] : NULL;
}

/* The specials each side has code for so far (Tasks 6-8 add the rest). */
static inline int ready(uint8_t sp) { return sp != MN_SP_RETAIN && sp != MN_SP_RELEASE; } /* every special has its code */

/* ---- guest ---- */
#ifndef MN_WM_HOST /* macneutron-vm has no guest table */

uint32_t (*mn_wm_texture_format)(mn_handle texture);

/* A listener's notify queue, which the host's listener thread fills.
 * ponytail: it lives until the process ends (DXMT makes a couple per device). */
struct listener_queue { struct mn_blob blob; };

struct mn_notify *mn_wm_listener_queue(mn_handle h, uint64_t *blob_id) {
    uint64_t att = mn_gh_attached(h);
    if (MN_GH_TAG(att) != MN_GH_PTR) return NULL;
    struct listener_queue *l = (void *)(uintptr_t)MN_GH_VALUE(att);
    *blob_id = l->blob.blob_id;
    return l->blob.ptr;
}

/* The thread's scratch blob: a call's bulk data, reused once the call returns. A call that outgrows it keeps the old
 * one (its earlier regions are there) until it returns.
 * ponytail: a thread's scratch blob lives until the process ends. */
static _Thread_local struct { struct mn_blob cur, old; uint64_t used; } scr;

static void *scratch_alloc(uint64_t len, uint64_t *blob, uint64_t *off) {
    uint64_t at = (scr.used + 63) & ~63ull;
    if (!scr.cur.ptr || at > scr.cur.size || len > scr.cur.size - at) {
        uint64_t size = scr.cur.size * 2;
        if (size < len + 64) size = len + 64;
        if (size < (4u << 20)) size = 4u << 20;
        struct mn_blob b;
        if (mn_blob_alloc(size, &b)) return NULL;
        if (scr.used) {
            if (scr.old.ptr) mn_blob_free(&scr.old);
            scr.old = scr.cur;
        } else if (scr.cur.ptr) {
            mn_blob_free(&scr.cur);
        }
        scr.cur = b;
        at = 0;
    }
    scr.used = at + len;
    *blob = scr.cur.blob_id, *off = at;
    return (uint8_t *)scr.cur.ptr + at;
}

void mn_wm_forget(void) { memset(&scr, 0, sizeof scr); }

void mn_wm_scratch_done(void) {
    scr.used = 0;
    if (scr.old.ptr) {
        mn_blob_free(&scr.old);
        memset(&scr.old, 0, sizeof scr.old);
    }
}

NTSTATUS_T mn_wm_emit(struct mn_wm_out *o, const void *p, uint64_t n) {
    if (n > o->cap - o->len) return MN_STATUS_BUFFER_TOO_SMALL;
    memcpy(o->buf + o->len, p, n);
    memset(o->buf + o->len + n, 0, PAD8(n) - n);
    o->len += (uint32_t)PAD8(n);
    return 0;
}

#define emit mn_wm_emit
static NTSTATUS_T emit_hdr(struct mn_wm_out *o, uint64_t len, uint32_t how) {
    struct mn_wm_rhdr h = {(uint32_t)len, how};
    return len > UINT32_MAX ? MN_STATUS_BUFFER_TOO_SMALL : emit(o, &h, sizeof h);
}

NTSTATUS_T mn_wm_emit_none(struct mn_wm_out *o) { return emit_hdr(o, 0, MN_RH_NONE); }

/* len bytes at ptr that the host reads (IN, INOUT, strings) or writes (OUT): inline, or through the scratch blob. */
NTSTATUS_T mn_wm_emit_data(struct mn_wm_out *o, const void *ptr, uint64_t len, uint8_t kind) {
    if (!ptr) return emit_hdr(o, 0, MN_RH_NONE);
    int back = kind == WMF_OUT || kind == WMF_INOUT;
    const void *src = NULL;
    NTSTATUS_T s;
    if (back ? len > MN_WM_SCRATCH_OUT : len > MN_WM_SCRATCH_IN) {
        uint64_t v[2];
        uint8_t *sp = scratch_alloc(len, &v[0], &v[1]);
        if (!sp) return MN_STATUS_UNSUCCESSFUL;
        if (kind != WMF_OUT) memcpy(sp, ptr, len);
        o->sync = 1; /* the scratch blob is free again once the call returns */
        src = sp;
        if ((s = emit_hdr(o, len, MN_RH_SCRATCH)) || (s = emit(o, v, sizeof v))) return s;
    } else {
        if ((s = emit_hdr(o, len, MN_RH_INLINE))) return s;
        if (kind != WMF_OUT && (s = emit(o, ptr, len))) return s;
    }
    if (back) {
        if (o->nregions == MN_WM_MAX_REGIONS) return MN_STATUS_BUFFER_TOO_SMALL;
        o->regions[o->nregions++] = (struct mn_wm_region){(void *)ptr, src, (uint32_t)len, kind};
    }
    return 0;
}

/* Room for len bytes the host reads, written in place: in the record, or in the scratch blob. */
static NTSTATUS_T emit_reserve(struct mn_wm_out *o, uint64_t len, uint8_t **dst) {
    NTSTATUS_T s;
    if (len > MN_WM_SCRATCH_IN) {
        uint64_t v[2];
        if (!(*dst = scratch_alloc(len, &v[0], &v[1]))) return MN_STATUS_UNSUCCESSFUL;
        o->sync = 1;
        if ((s = emit_hdr(o, len, MN_RH_SCRATCH))) return s;
        return emit(o, v, sizeof v);
    }
    if ((s = emit_hdr(o, len, MN_RH_INLINE))) return s;
    if (PAD8(len) > o->cap - o->len) return MN_STATUS_BUFFER_TOO_SMALL;
    *dst = o->buf + o->len;
    memset(*dst + len, 0, PAD8(len) - len);
    o->len += (uint32_t)PAD8(len);
    return 0;
}

/* 36-38: the encoder's list, flat (cmdlist.h). At most MN_WM_MAX_NODES nodes of known types: a longer list, or one
 * that loops, is refused. */
static NTSTATUS_T emit_list(unsigned slot, const struct wmtcmd_base *head, struct mn_wm_out *o) {
    unsigned ntypes;
    const struct mn_wm_node *tab = mn_wm_nodes(slot, &ntypes);
    uint64_t count = 0, nodes = 0, payloads = 0;
    for (const struct wmtcmd_base *n = head; n; n = n->next.ptr) {
        if (++count > MN_WM_MAX_NODES || n->type >= ntypes || !tab[n->type].size) return MN_STATUS_INVALID_PARAMETER;
        nodes += PAD8(tab[n->type].size);
        payloads += PAD8(mn_wm_node_payload(&tab[n->type], (const uint8_t *)n));
    }
    if (!count) return emit_hdr(o, 0, MN_RH_NONE);
    uint64_t total = 8 + nodes + payloads, at = 8, pat = 8 + nodes;
    uint8_t *w;
    NTSTATUS_T s = emit_reserve(o, total, &w);
    if (s) return s;
    uint32_t c32 = (uint32_t)count, zero = 0;
    memcpy(w, &c32, 4);
    memcpy(w + 4, &zero, 4);
    for (const struct wmtcmd_base *n = head; n && count--; n = n->next.ptr) {
        const struct mn_wm_node *t = &tab[n->type];
        if (PAD8(t->size) > 8 + nodes - at) return MN_STATUS_INVALID_PARAMETER; /* the list changed under us */
        memcpy(w + at, n, t->size);
        memset(w + at + t->size, 0, PAD8(t->size) - t->size);
        memset(w + at + 8, 0, 8); /* next */
        if (t->payload) {
            uint64_t len = mn_wm_node_payload(t, (const uint8_t *)n), off = ~0ull;
            const void *src;
            memcpy(&src, (const uint8_t *)n + t->payload, sizeof src);
            if (len) {
                if (!src || PAD8(len) > total - pat) return MN_STATUS_INVALID_PARAMETER;
                memcpy(w + pat, src, len);
                memset(w + pat + len, 0, PAD8(len) - len);
                off = pat;
                pat += PAD8(len);
            }
            memcpy(w + at + t->payload, &off, 8);
        }
        at += PAD8(t->size);
    }
    return 0;
}

/* Blob memory the host maps in place (len 0: a holder of region with no memory of its own); held for the host. */
static NTSTATUS_T emit_hold(struct mn_wm_out *o, uint64_t len, uint64_t blob, uint64_t off, uint64_t region) {
    if (o->nholds == MN_WM_MAX_HOLDS) return MN_STATUS_INVALID_PARAMETER;
    uint64_t v[3] = {blob, off, region};
    NTSTATUS_T s = emit_hdr(o, len, MN_RH_BLOB);
    if (!s) s = emit(o, v, sizeof v);
    if (s) return s;
    mn_ghold(region);
    o->holds[o->nholds++] = region;
    return 0;
}

static NTSTATUS_T pack_fields(struct mn_wm_out *o, const struct mn_wm_field *fields, unsigned n, const uint8_t *base,
                              int depth) {
    if (depth > MAX_DEPTH) return MN_STATUS_INVALID_PARAMETER;
    for (unsigned i = 0; i < n; i++) {
        const struct mn_wm_field *f = &fields[i];
        if (!is_ptr(f->kind)) continue;
        const uint8_t *ptr;
        memcpy(&ptr, base + f->offset, sizeof ptr);
        NTSTATUS_T s;
        uint64_t len = ptr ? region_len(base, f, ptr) : 0;
        if (f->kind == WMF_BLOB_PTR) {
            uint64_t id, blob, off;
            if (!ptr) s = emit_hdr(o, 0, MN_RH_NONE);
            else if (mn_glookup(ptr, len ? len : 1, &id, &blob, &off) || off % MN_PAGE) s = MN_STATUS_INVALID_PARAMETER;
            else s = emit_hold(o, len, blob, off, id);
            if (s) return s;
            continue;
        }
        if ((s = mn_wm_emit_data(o, ptr, len, f->kind))) return s;
        const struct mn_wm_nested *nest = ptr ? nested_of(f) : NULL;
        uint64_t elem = f->flags & WMF_ARRAY ? f->size : len;
        for (uint64_t e = 0; nest && elem && e < len / elem; e++)
            if ((s = pack_fields(o, nest->fields, nest->nfields, ptr + e * elem, depth + 1))) return s;
    }
    return 0;
}

/* A special's own regions, right after the params; and for 18, the memory a CPU-visible buffer gets. */
static NTSTATUS_T guest_special(unsigned slot, uint8_t *params, struct mn_wm_out *o) {
    switch (slot) {
    case 18: {
        struct unixcall_mtldevice_newbuffer *a = (void *)params;
        struct WMTBufferInfo *info = a->info.ptr;
        if (!info) return MN_STATUS_INVALID_PARAMETER;
        uint64_t st = STORAGE(info->options);
        if (info->memory.ptr || st == STORAGE_PRIVATE || st == STORAGE_MEMORYLESS) return 0;
        struct mn_region r; /* the buffer handle owns it */
        if (mn_galloc(info->length, &r)) return MN_STATUS_UNSUCCESSFUL;
        mn_gh_attach(o->outs[0], MN_GH_REGION | r.id);
        info->memory.ptr = r.ptr;
        return 0;
    }
    case 138: {
        struct unixcall_mtldevice_newheap *a = (void *)params;
        const struct WMTHeapInfo *info = a->info.ptr;
        if (!info) return MN_STATUS_INVALID_PARAMETER;
        uint64_t st = STORAGE(info->options);
        if (st == STORAGE_PRIVATE || st == STORAGE_MEMORYLESS) return emit_hdr(o, 0, MN_RH_NONE); /* a real heap */
        struct mn_region r; /* emulated over our memory: the heap handle owns it */
        uint64_t id, blob, off;
        if (mn_galloc(info->size, &r) || mn_glookup(r.ptr, 1, &id, &blob, &off)) return MN_STATUS_UNSUCCESSFUL;
        mn_gh_attach(o->outs[0], MN_GH_REGION | r.id);
        return emit_hold(o, r.len, blob, off, r.id);
    }
    case 141:
    case 142: {
        mn_handle heap;
        memcpy(&heap, params, sizeof heap); /* both structs start with it */
        uint64_t att = mn_gh_attached(heap);
        if (MN_GH_TAG(att) != MN_GH_REGION) return slot == 141 ? emit_hdr(o, 0, MN_RH_NONE) : 0;
        if (slot == 142) return MN_STATUS_NOT_SUPPORTED; /* D3D12 puts textures in default (Private) heaps */
        struct unixcall_mtlheap_newbuffer *a = (void *)params;
        struct WMTBufferInfo *info = a->info.ptr;
        uint8_t *base = mn_gptr(MN_GH_VALUE(att));
        if (!info || !base) return MN_STATUS_INVALID_PARAMETER;
        o->fix = &info->memory.ptr; /* the buffer's memory, once the host made it */
        o->fix_value = base + a->offset;
        return emit_hold(o, 0, 0, 0, MN_GH_VALUE(att)); /* the buffer holds the heap's region */
    }
    case 36:
    case 37:
    case 38: return emit_list(slot, ((struct unixcall_generic_obj_cmd_noret *)params)->cmd_head.ptr, o);
    case 108: { /* its notify queue, whose blob id comes first */
        struct listener_queue *l = calloc(1, sizeof *l);
        if (!l || mn_blob_alloc(MN_NOTIFY_BYTES, &l->blob)) {
            free(l);
            return MN_STATUS_UNSUCCESSFUL;
        }
        memset(l->blob.ptr, 0, MN_NOTIFY_BYTES);
        mn_gh_attach(o->outs[0], MN_GH_PTR | (uintptr_t)l);
        uint64_t id = l->blob.blob_id;
        return mn_wm_emit_data(o, &id, sizeof id, WMF_IN);
    }
    case 98: return mn_wm_constants_emit(params, o);
    case 110: { /* destroying it is its release: the host unbinds it too */
        mn_handle h;
        memcpy(&h, params, sizeof h);
        return mn_gh_release(h) < 0 ? MN_STATUS_INVALID_HANDLE : 0;
    }
    case 45:
    case 145: {
        struct unixcall_mtltexture_replaceregion *a = (void *)params;
        uint32_t fmt = mn_wm_texture_format ? mn_wm_texture_format(a->texture) : 0;
        uint64_t ext = fmt ? mn_wm_region_extent(fmt, a->size.width, a->size.height, a->size.depth, a->bytes_per_row,
                                                 a->bytes_per_image)
                           : UINT64_MAX;
        if (ext == UINT64_MAX) return MN_STATUS_INVALID_PARAMETER;
        return mn_wm_emit_data(o, ext ? a->data.ptr : NULL, ext, slot == 45 ? WMF_IN : WMF_OUT);
    }
    default:
        return 0;
    }
}

void mn_wm_unwind(const struct mn_wm_out *out, int reached_host) {
    for (unsigned i = 0; i < out->nouts; i++) mn_gh_drop(out->outs[i]);
    mn_wm_enumerate_unwind(); /* slot 91's handles aren't outs: the host binds as many as it reads */
    if (!reached_host) /* else the host frees each region the record named */
        for (unsigned i = 0; i < out->nholds; i++) mn_gunhold(out->holds[i]);
    mn_wm_scratch_done();
}

NTSTATUS_T mn_wm_pack(unsigned slot, void *params, struct mn_wm_out *out) {
    if (slot >= MN_WM_SLOTS) return MN_STATUS_INVALID_PARAMETER;
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    out->len = 0, out->nouts = 0, out->nregions = 0, out->nholds = 0, out->fix = NULL;
    out->sync = c->mode == MN_WM_SYNC;
    if (c->mode == MN_WM_REFUSED || c->mode == MN_WM_LOCAL) return c->refuse_status;
    if (c->special == MN_SP_RETAIN || c->special == MN_SP_RELEASE) {
        mn_handle h;
        memcpy(&h, params, sizeof h);
        if ((c->special == MN_SP_RETAIN ? mn_gh_retain(h) : mn_gh_release(h)) < 0) return MN_STATUS_INVALID_HANDLE;
        return emit(out, &h, sizeof h);
    }
    if (c->special == MN_SP_SM50) return mn_wm_sm50_pack(slot, params, out);
    if (c->special == MN_SP_ENUMERATE) return mn_wm_enumerate_pack(params, out);
    for (unsigned i = 0; i < c->nfields; i++) {
        const struct mn_wm_field *f = &c->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h = out->nouts < MN_WM_MAX_OUTS ? mn_gh_alloc((enum mn_own)f->own) : 0;
        if (!h) {
            mn_wm_unwind(out, 0);
            return MN_STATUS_UNSUCCESSFUL;
        }
        if (f->hkind == MN_HK_POOL) mn_gh_pool_push(h);
        out->outs[out->nouts++] = h;
        memcpy((uint8_t *)params + f->offset, &h, sizeof h);
    }
    NTSTATUS_T s = emit(out, params, c->params_size);
    if (!s) s = guest_special(slot, params, out);
    if (!s) s = pack_fields(out, c->fields, c->nfields, params, 0);
    if (s) mn_wm_unwind(out, 0);
    return s;
}

NTSTATUS_T mn_wm_unpack(unsigned slot, void *params, const struct mn_wm_out *out, const void *reply, uint32_t len) {
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    if (c->special == MN_SP_SM50) return mn_wm_sm50_unpack(slot, params, out, reply, len);
    if (c->special == MN_SP_ENUMERATE) return mn_wm_enumerate_unpack(params, out, reply, len);
    const uint8_t *r = reply;
    uint64_t pos = PAD8(c->params_size);
    if (len < pos) {
        mn_wm_unwind(out, 1);
        return MN_STATUS_UNSUCCESSFUL;
    }
    memcpy(params, r, c->params_size);
    mn_handle first = 0;
    for (unsigned i = 0, k = 0; i < c->nfields; i++) {
        const struct mn_wm_field *f = &c->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h;
        memcpy(&h, (uint8_t *)params + f->offset, sizeof h);
        if (!h) mn_gh_drop(out->outs[k]);
        if (!k++) first = h;
    }
    NTSTATUS_T s = mn_wm_copy_back(out, r, len, pos);
    if (out->fix && first) *out->fix = out->fix_value;
    mn_wm_scratch_done();
    return s;
}

/* The regions a reply carries, from pos, back to the memory they came from. */
NTSTATUS_T mn_wm_copy_back(const struct mn_wm_out *out, const uint8_t *r, uint32_t len, uint64_t pos) {
    for (unsigned i = 0; i < out->nregions; i++) {
        struct mn_wm_rhdr h;
        if (len < pos || len - pos < sizeof h) return MN_STATUS_UNSUCCESSFUL;
        memcpy(&h, r + pos, sizeof h);
        pos += sizeof h;
        uint32_t n = h.len < out->regions[i].len ? h.len : out->regions[i].len;
        if (h.how == MN_RH_INLINE) {
            if (h.len > len - pos) return MN_STATUS_UNSUCCESSFUL;
            memcpy(out->regions[i].ptr, r + pos, n);
            pos += PAD8(h.len);
        } else if (h.how == MN_RH_SCRATCH && out->regions[i].scratch) {
            memcpy(out->regions[i].ptr, out->regions[i].scratch, n);
        }
    }
    return 0;
}

#endif

/* ---- host ---- */
#ifndef MN_WM_GUEST /* the guest's winemetal.so has no host table and no winemetal to call */

/* The marshaller's memory per ring (ring_state word 1): chunks, so that pointers into it stay valid for a call, and
 * the scratch blobs the call uses, held until the next call (a blob the guest frees meanwhile stays mapped). */
struct chunk { struct chunk *next; size_t cap, used; _Alignas(16) uint8_t data[]; };
#define ARENA_HOLDS 4
struct arena { struct chunk *head; unsigned nholds; uint64_t held[ARENA_HOLDS]; void *holds[ARENA_HOLDS]; };
#define CHUNK_BYTES (1u << 20)

static struct arena *arena_of(void *ring) {
    struct arena **slot = (struct arena **)ring + 1;
    if (!*slot) *slot = calloc(1, sizeof **slot);
    return *slot;
}

static void arena_reset(struct arena *a) {
    while (a->nholds) mn_executor_blob_release(a->holds[--a->nholds]);
    while (a->head && a->head->next) { /* the oldest chunk stays */
        struct chunk *c = a->head;
        a->head = c->next;
        free(c);
    }
    if (a->head) a->head->used = 0;
}

static void *arena_alloc(struct arena *a, size_t n) {
    struct chunk *c = a->head;
    size_t at = c ? (c->used + 15) & ~(size_t)15 : 0;
    if (!c || at > c->cap || n > c->cap - at) {
        size_t cap = n + 16 > CHUNK_BYTES ? n + 16 : CHUNK_BYTES;
        if (!(c = malloc(sizeof *c + cap))) return NULL;
        *c = (struct chunk){a->head, cap, 0};
        a->head = c;
        at = 0;
    }
    c->used = at + n;
    return c->data + at;
}

void *mn_wm_alloc(struct mn_wm_state *k, size_t n) { return arena_alloc(k->arena, n); }

/* [offset, offset + len) of a scratch blob, which the call's arena holds; a call names a few blobs at most. */
static uint8_t *scratch_of(struct mn_wm_state *k, uint64_t blob, uint64_t offset, uint64_t len) {
    struct arena *a = k->arena;
    unsigned i = 0;
    while (i < a->nholds && a->held[i] != blob) i++;
    if (i == ARENA_HOLDS) return NULL;
    void *hold, *p = mn_executor_blob_hold(k->c->ctx_id, blob, offset, len, &hold);
    if (!p) return NULL;
    if (i < a->nholds) {
        mn_executor_blob_release(hold); /* already held for this call */
    } else {
        a->held[i] = blob, a->holds[i] = hold;
        a->nholds++;
    }
    return p;
}

void mn_wm_ring_end(void *ring) {
    struct arena **slot = (struct arena **)ring + 1;
    if (*slot) {
        arena_reset(*slot);
        free((*slot)->head);
        free(*slot);
    }
    *slot = NULL;
}

/* The record's next region as host memory (*host NULL for a NULL pointer), and its length. want: the length the
 * struct's own fields give (UINT64_MAX: any). copy: the host reads values out of it, so it gets its own copy; bulk
 * bytes in the scratch blob are otherwise used in place. */
static NTSTATUS_T take_data(struct mn_wm_state *k, const struct mn_wm_field *f, uint8_t kind, uint64_t want, int copy,
                            uint8_t **host, uint32_t *len) {
    struct mn_wm_rhdr h;
    *host = NULL, *len = 0;
    if (k->len - k->pos < sizeof h) return MN_STATUS_INVALID_PARAMETER;
    memcpy(&h, k->rec + k->pos, sizeof h);
    k->pos += sizeof h;
    if (h.how == MN_RH_NONE) return 0;
    if ((want != UINT64_MAX && h.len != want) || (kind == WMF_STR_IN && !h.len)) return MN_STATUS_INVALID_PARAMETER;
    const uint8_t *src = NULL;
    uint8_t *scratch = NULL;
    if (h.how == MN_RH_INLINE) {
        if (kind != WMF_OUT) {
            if (h.len > k->len - k->pos) return MN_STATUS_INVALID_PARAMETER;
            src = k->rec + k->pos;
            k->pos += (uint32_t)PAD8(h.len);
        } else if (h.len > MN_REPLY_BYTES - 1024 - k->out_bytes) { /* the params come first */
            return MN_STATUS_BUFFER_TOO_SMALL;
        }
    } else if (h.how == MN_RH_SCRATCH) {
        uint64_t v[2];
        if (k->len - k->pos < sizeof v) return MN_STATUS_INVALID_PARAMETER;
        memcpy(v, k->rec + k->pos, sizeof v);
        k->pos += sizeof v;
        if (!(scratch = scratch_of(k, v[0], v[1], h.len))) return MN_STATUS_INVALID_PARAMETER;
        src = scratch;
    } else {
        return MN_STATUS_INVALID_PARAMETER;
    }
    uint8_t *dst = scratch && !copy ? scratch : arena_alloc(k->arena, h.len ? h.len : 1);
    if (!dst) return MN_STATUS_UNSUCCESSFUL;
    if (dst != scratch) {
        if (src && kind != WMF_OUT) memcpy(dst, src, h.len);
        else memset(dst, 0, h.len);
    }
    if (kind == WMF_STR_IN && dst[h.len - 1]) return MN_STATUS_INVALID_PARAMETER;
    if (kind == WMF_OUT || kind == WMF_INOUT) {
        if (k->nregions == MN_WM_MAX_REGIONS) return MN_STATUS_INVALID_PARAMETER;
        k->regions[k->nregions++] = (struct mn_wm_hregion){dst, src, scratch, h.len, kind, (uint8_t)h.how, f};
        k->out_bytes += sizeof h + (h.how == MN_RH_INLINE ? (uint32_t)PAD8(h.len) : 0);
    }
    *host = dst, *len = h.len;
    return 0;
}

NTSTATUS_T mn_wm_take(struct mn_wm_state *k, uint8_t kind, uint64_t want, int copy, uint8_t **host, uint32_t *len) {
    return take_data(k, NULL, kind, want, copy, host, len);
}

mn_handle mn_wm_bind_out(struct mn_wm_state *k, mn_handle h, enum mn_hkind kind, void *obj, enum mn_own own) {
    if (!obj) return 0;
    if (own == MN_OWN_AUTORELEASED || own == MN_OWN_BORROWED) mn_wm_invoke(0, &obj);
    if (!mn_ht_bind(k->c->ht, k->ring, h, kind, obj, own)) return h;
    mn_wm_release_object(kind, obj);
    return 0;
}

/* The record's next region as blob memory (how 3), held; *x NULL for none. want: its length, or UINT64_MAX. */
static NTSTATUS_T take_blob(struct mn_wm_state *k, uint64_t want, struct mn_wm_hold **x) {
    struct mn_wm_rhdr h;
    uint64_t v[3];
    *x = NULL;
    if (k->len - k->pos < sizeof h) return MN_STATUS_INVALID_PARAMETER;
    memcpy(&h, k->rec + k->pos, sizeof h);
    k->pos += sizeof h;
    if (h.how == MN_RH_NONE) return 0;
    if (h.how != MN_RH_BLOB || k->len - k->pos < sizeof v || k->nholds == MN_WM_MAX_HOLDS)
        return MN_STATUS_INVALID_PARAMETER;
    memcpy(v, k->rec + k->pos, sizeof v);
    k->pos += sizeof v;
    /* Recorded before anything can fail: its region gets its MN_NOTIFY_REGION_FREE whatever happens next. */
    struct mn_wm_hold *d = &k->holds[k->nholds++];
    *d = (struct mn_wm_hold){.blob = v[0], .offset = v[1], .len = h.len, .region = v[2]};
    *x = d;
    if (want != UINT64_MAX && h.len != want) return MN_STATUS_INVALID_PARAMETER;
    if (!h.len) return 0;
    if (v[1] % MN_PAGE) return MN_STATUS_INVALID_PARAMETER;
    uint64_t span = ((uint64_t)h.len + MN_PAGE - 1) / MN_PAGE * MN_PAGE;
    d->ptr = mn_executor_blob_hold(k->c->ctx_id, v[0], v[1], span, &d->hold);
    return d->ptr ? 0 : MN_STATUS_INVALID_PARAMETER;
}

/* Translates and copies one struct's fields in place (base is the host's copy); regions come from the record. A
 * handle that doesn't translate fails the call, but only after every region was read: so is each blob region's. */
static NTSTATUS_T unpack_fields(struct mn_wm_state *k, const struct mn_wm_field *fields, unsigned n, uint8_t *base,
                                int depth) {
    if (depth > MAX_DEPTH) return MN_STATUS_INVALID_PARAMETER;
    for (unsigned i = 0; i < n; i++) {
        const struct mn_wm_field *f = &fields[i];
        NTSTATUS_T s;
        if (f->kind == WMF_HANDLE_IN) {
            mn_handle h;
            memcpy(&h, base + f->offset, sizeof h);
            if (!h) continue;
            int ok;
            void *obj = mn_ht_get(k->c->ht, h, (enum mn_hkind)f->hkind, &ok);
            if (!ok && !k->status) k->status = MN_STATUS_INVALID_HANDLE;
            memcpy(base + f->offset, &obj, sizeof obj);
            continue;
        }
        if (f->kind == WMF_BLOB_PTR) {
            struct mn_wm_hold *x;
            if ((s = take_blob(k, region_len(base, f, NULL), &x))) return s;
            void *p = x ? x->ptr : NULL;
            memcpy(base + f->offset, &p, sizeof p);
            continue;
        }
        if (!is_ptr(f->kind)) continue;
        uint8_t *host;
        uint32_t len;
        uint64_t want = f->kind == WMF_STR_IN ? UINT64_MAX : region_len(base, f, NULL);
        int copy = f->nested || f->kind == WMF_STR_IN || !(f->flags & WMF_ARRAY);
        if ((s = take_data(k, f, f->kind, want, copy, &host, &len))) return s;
        memcpy(base + f->offset, &host, sizeof host);
        const struct mn_wm_nested *nest = host ? nested_of(f) : NULL;
        uint64_t elem = f->flags & WMF_ARRAY ? f->size : len;
        for (uint64_t e = 0; nest && elem && e < len / elem; e++)
            if ((s = unpack_fields(k, nest->fields, nest->nfields, host + e * elem, depth + 1))) return s;
    }
    return 0;
}

/* A region the host wrote, with the guest's own values back in its handle and pointer fields. */
static void restore(uint8_t *dst, const uint8_t *guest, const struct mn_wm_field *f, uint32_t len) {
    const struct mn_wm_nested *nest = f ? nested_of(f) : NULL;
    uint32_t elem = f && f->flags & WMF_ARRAY ? f->size : len;
    if (!guest || !nest || !elem) return;
    for (uint64_t e = 0; e < len / elem; e++)
        for (unsigned i = 0; i < nest->nfields; i++) {
            const struct mn_wm_field *g = &nest->fields[i];
            if (g->kind == WMF_HANDLE_IN || is_ptr(g->kind))
                memcpy(dst + e * elem + g->offset, guest + e * elem + g->offset, 8);
        }
}

static int32_t failed(struct mn_wm_ctx *c, int sync, uint32_t status) {
    if (!sync) atomic_fetch_add(&c->failures, 1);
    return (int32_t)status;
}

/* Every handle the call would have returned, bound to nil, so that later calls on them are no-ops, not waits. */
static void bind_nil(struct mn_wm_ctx *c, void *ring, const struct mn_wm_call *call, const uint8_t *rec) {
    for (unsigned i = 0; i < call->nfields; i++) {
        const struct mn_wm_field *f = &call->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h;
        memcpy(&h, rec + f->offset, sizeof h);
        mn_ht_bind(c->ht, ring, h, (enum mn_hkind)f->hkind, NULL, (enum mn_own)f->own);
    }
}

/* The regions a special reads first, right after the params. */
static NTSTATUS_T take_prefix(struct mn_wm_state *k) {
    switch (k->slot) {
    case 138:
    case 141: return take_blob(k, UINT64_MAX, &k->prefix_hold);
    case 36:
    case 37:
    case 38:
    case 45:
    case 108: return take_data(k, NULL, WMF_IN, UINT64_MAX, 0, &k->data, &k->data_len);
    case 145: return take_data(k, NULL, WMF_OUT, UINT64_MAX, 0, &k->data, &k->data_len);
    default: return 0;
    }
}

int32_t mn_wm_host_call(struct mn_wm_ctx *c, void *ring, unsigned slot, const void *rec, uint32_t len, void *reply,
                        uint32_t cap, uint32_t *reply_len) {
    int sync = reply != NULL;
    *reply_len = 0;
    if (slot == MN_WM_STATS) {
        if (!sync || cap < sizeof(struct mn_wm_stats)) return MN_EINVAL;
        mn_ht_sweep(c->ht, ring);
        struct mn_wm_stats st = {mn_ht_live(c->ht), mn_ht_deferred(c->ht), atomic_load(&c->failures)};
        memcpy(reply, &st, sizeof st);
        *reply_len = sizeof st;
        return 0;
    }
    if (slot == MN_WM_NOTIFY_ATTACH) {
        struct mn_wm_notify_attach a;
        if (len < sizeof a) return MN_EINVAL;
        memcpy(&a, rec, sizeof a);
        return (int32_t)mn_wm_notify_attach(c, a.blob_id, a.listener);
    }
    if (slot >= MN_WM_SLOTS) return failed(c, sync, MN_STATUS_INVALID_PARAMETER);
    const struct mn_wm_call *call = &mn_wm_calls[slot];
    if (call->mode == MN_WM_REFUSED || call->mode == MN_WM_LOCAL) return failed(c, sync, call->refuse_status);
    uint32_t psz = (uint32_t)PAD8(call->params_size);
    if (len < psz) return failed(c, sync, MN_STATUS_INVALID_PARAMETER);
    if (call->special == MN_SP_RETAIN || call->special == MN_SP_RELEASE) {
        mn_handle h;
        memcpy(&h, rec, sizeof h);
        int r = call->special == MN_SP_RETAIN ? mn_ht_retain(c->ht, h) : mn_ht_release(c->ht, ring, h);
        return r ? failed(c, sync, MN_STATUS_INVALID_HANDLE) : 0;
    }
    if (!ready(call->special)) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, MN_STATUS_NOT_IMPLEMENTED); /* Tasks 6-8 */
    }
    struct arena *a = arena_of(ring);
    if (a) arena_reset(a);
    struct mn_wm_state *k = a ? arena_alloc(a, sizeof *k) : NULL;
    uint8_t *p = k ? arena_alloc(a, psz) : NULL;
    if (!p) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, MN_STATUS_UNSUCCESSFUL);
    }
    *k = (struct mn_wm_state){.c = c, .ring = ring, .slot = slot, .call = call, .sync = sync, .rec = rec, .len = len,
                              .pos = psz, .p = p, .arena = a};
    memcpy(p, rec, call->params_size);
    mn_handle outs[MN_WM_MAX_OUTS];
    unsigned nouts = 0;
    for (unsigned i = 0; i < call->nfields && nouts < MN_WM_MAX_OUTS; i++)
        if (call->fields[i].kind == WMF_HANDLE_OUT) {
            k->out_kind[nouts] = call->fields[i].hkind;
            memcpy(&outs[nouts++], p + call->fields[i].offset, sizeof(mn_handle));
            memset(p + call->fields[i].offset, 0, sizeof(mn_handle));
        }
    NTSTATUS_T s = take_prefix(k);
    if (!s) s = unpack_fields(k, call->fields, call->nfields, p, 0);
    if (!s) s = k->status;
    if (!s && sync && psz + k->out_bytes > cap) s = MN_STATUS_BUFFER_TOO_SMALL;
    if (!s) s = call->special == MN_SP_NONE ? mn_wm_invoke(slot, p) : mn_wm_special(k);
    for (unsigned i = 0; i < k->nholds; i++) /* blob memory nothing took: its region is the guest's again */
        if (!k->holds[i].taken) mn_wm_drop_hold(c, &k->holds[i]);
    if (s) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, s);
    }

    /* Bind what came back. A nil handle is no binding in a reply (the guest frees it), and a nil binding when queued. */
    int lost = 0;
    for (unsigned i = 0, n = 0; i < call->nfields; i++) {
        const struct mn_wm_field *f = &call->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h = outs[n];
        enum mn_hkind kind = (enum mn_hkind)k->out_kind[n++];
        void *obj;
        memcpy(&obj, p + f->offset, sizeof obj);
        if (!obj && sync) {
            memset(p + f->offset, 0, sizeof(mn_handle));
            continue;
        }
        if (obj && (f->own == MN_OWN_AUTORELEASED || f->own == MN_OWN_BORROWED)) mn_wm_invoke(0, &obj);
        if (mn_ht_bind(c->ht, ring, h, kind, obj, (enum mn_own)f->own)) {
            if (obj) mn_wm_release_object(kind, obj);
            if (sync) memset(p + f->offset, 0, sizeof(mn_handle));
            lost = 1;
            continue;
        }
        if (kind == MN_HK_POOL) mn_ht_pool_push(c->ht, ring, h);
        if (!obj) lost = 1; /* a queued creation that failed */
        memcpy(p + f->offset, &h, sizeof h);
    }
    if (lost) s = sync ? MN_STATUS_INVALID_HANDLE : MN_STATUS_UNSUCCESSFUL;
    if (!sync) return s ? failed(c, 0, s) : 0;

    /* The reply: params with the guest's pointers and handles in, then the regions the host wrote. */
    uint8_t *r = reply;
    memcpy(r, p, call->params_size);
    for (unsigned i = 0; i < call->nfields; i++) {
        const struct mn_wm_field *f = &call->fields[i];
        if (f->kind == WMF_HANDLE_IN || is_ptr(f->kind)) memcpy(r + f->offset, (const uint8_t *)rec + f->offset, 8);
    }
    uint32_t pos = psz;
    for (unsigned i = 0; i < k->nregions; i++) {
        struct mn_wm_hregion *g = &k->regions[i];
        restore(g->host, g->guest, g->f, g->len);
        struct mn_wm_rhdr h = {g->len, g->how};
        memcpy(r + pos, &h, sizeof h);
        pos += sizeof h;
        if (g->how == MN_RH_INLINE) {
            memcpy(r + pos, g->host, g->len);
            memset(r + pos + g->len, 0, PAD8(g->len) - g->len);
            pos += (uint32_t)PAD8(g->len);
        } else if (g->host != g->scratch) {
            memcpy(g->scratch, g->host, g->len); /* back through the scratch blob */
        }
    }
    *reply_len = pos;
    return (int32_t)s;
}

#endif
