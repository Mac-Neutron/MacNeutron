/* The marshaller (marshal.h): one walk over a slot's fields, depth first, on each side. The host reads every guest
 * value from its own copy (params and regions), so nothing the guest changes in the ring afterwards reaches a check. */
#include "marshal.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PAD8(n) (((n) + 7u) & ~7u)
#define MAX_DEPTH 4

struct rhdr { uint32_t len, present; };

static int is_ptr(uint8_t kind) {
    return kind == WMF_IN || kind == WMF_OUT || kind == WMF_INOUT || kind == WMF_STR_IN;
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

/* Specials the generic path runs until their task adds what they need beyond it. */
static int generic(uint8_t special) {
    return special == MN_SP_NONE || special == MN_SP_RESIDENCY; /* emulated heaps: Task 5 */
}

static const struct mn_wm_nested *nested_of(const struct mn_wm_field *f) {
    return f->nested ? &mn_wm_nested[f->nested - 1] : NULL;
}

/* ---- guest ---- */
#ifndef MN_WM_HOST /* macneutron-vm has no guest table */

static NTSTATUS_T emit(struct mn_wm_out *o, const void *p, uint32_t n) {
    if (n > o->cap - o->len) return MN_STATUS_BUFFER_TOO_SMALL;
    memcpy(o->buf + o->len, p, n);
    memset(o->buf + o->len + n, 0, PAD8(n) - n);
    o->len += PAD8(n);
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
        if (!ptr) {
            struct rhdr h = {0, 0};
            NTSTATUS_T s = emit(o, &h, sizeof h);
            if (s) return s;
            continue;
        }
        uint64_t len = region_len(base, f, ptr);
        if (len > MN_REC_MAX) return MN_STATUS_BUFFER_TOO_SMALL; /* ponytail: bulk data through blobs is Task 5's */
        struct rhdr h = {(uint32_t)len, 1};
        NTSTATUS_T s = emit(o, &h, sizeof h);
        if (!s && f->kind != WMF_OUT) s = emit(o, ptr, (uint32_t)len);
        if (s) return s;
        if (f->kind == WMF_OUT || f->kind == WMF_INOUT) {
            if (o->nregions == MN_WM_MAX_REGIONS) return MN_STATUS_BUFFER_TOO_SMALL;
            o->regions[o->nregions++] = (struct mn_wm_region){(void *)ptr, (uint32_t)len, f->kind};
        }
        const struct mn_wm_nested *nest = nested_of(f);
        uint32_t elem = f->flags & WMF_ARRAY ? f->size : (uint32_t)len;
        for (uint64_t e = 0; nest && elem && e < len / elem; e++)
            if ((s = pack_fields(o, nest->fields, nest->nfields, ptr + e * elem, depth + 1))) return s;
    }
    return 0;
}

void mn_wm_unwind(const struct mn_wm_out *out) {
    for (unsigned i = 0; i < out->nouts; i++) mn_gh_drop(out->outs[i]);
}

NTSTATUS_T mn_wm_pack(unsigned slot, void *params, struct mn_wm_out *out) {
    if (slot >= MN_WM_SLOTS) return MN_STATUS_INVALID_PARAMETER;
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    out->len = 0, out->nouts = 0, out->nregions = 0;
    out->sync = c->mode == MN_WM_SYNC;
    if (c->mode == MN_WM_REFUSED) return c->refuse_status;
    if (c->special == MN_SP_RETAIN || c->special == MN_SP_RELEASE) {
        mn_handle h;
        memcpy(&h, params, sizeof h);
        if ((c->special == MN_SP_RETAIN ? mn_gh_retain(h) : mn_gh_release(h)) < 0) return MN_STATUS_INVALID_HANDLE;
        return emit(out, &h, sizeof h);
    }
    if (!generic(c->special)) return MN_STATUS_NOT_IMPLEMENTED; /* Tasks 5-8 */
    for (unsigned i = 0; i < c->nfields; i++) {
        const struct mn_wm_field *f = &c->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h = mn_gh_alloc((enum mn_own)f->own);
        if (!h || out->nouts == MN_WM_MAX_OUTS) {
            mn_wm_unwind(out);
            return MN_STATUS_UNSUCCESSFUL;
        }
        if (f->hkind == MN_HK_POOL) mn_gh_pool_push(h);
        out->outs[out->nouts++] = h;
        memcpy((uint8_t *)params + f->offset, &h, sizeof h);
    }
    NTSTATUS_T s = emit(out, params, c->params_size);
    if (!s) s = pack_fields(out, c->fields, c->nfields, params, 0);
    if (s) mn_wm_unwind(out);
    return s;
}

NTSTATUS_T mn_wm_unpack(unsigned slot, void *params, const struct mn_wm_out *out, const void *reply, uint32_t len) {
    const struct mn_wm_call *c = &mn_wm_calls[slot];
    const uint8_t *r = reply;
    uint32_t pos = PAD8(c->params_size);
    if (len < pos) {
        mn_wm_unwind(out);
        return MN_STATUS_UNSUCCESSFUL;
    }
    memcpy(params, r, c->params_size);
    unsigned k = 0;
    for (unsigned i = 0; i < c->nfields; i++) {
        const struct mn_wm_field *f = &c->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h;
        memcpy(&h, (uint8_t *)params + f->offset, sizeof h);
        if (!h) mn_gh_drop(out->outs[k]);
        k++;
    }
    for (unsigned i = 0; i < out->nregions; i++) {
        struct rhdr h;
        if (len - pos < sizeof h) return MN_STATUS_UNSUCCESSFUL;
        memcpy(&h, r + pos, sizeof h);
        pos += sizeof h;
        if (h.len > len - pos) return MN_STATUS_UNSUCCESSFUL;
        memcpy(out->regions[i].ptr, r + pos, h.len < out->regions[i].len ? h.len : out->regions[i].len);
        pos += PAD8(h.len);
    }
    return 0;
}

#endif

/* ---- host ---- */
#ifndef MN_WM_GUEST /* the guest's winemetal.so has no host table and no winemetal to call */

/* The marshaller's memory per ring (ring_state word 1): reserved once per call, so pointers into it stay valid. */
struct arena { uint8_t *buf; size_t cap, used; };

static struct arena *arena_of(void *ring) {
    struct arena **slot = (struct arena **)ring + 1;
    if (!*slot) *slot = calloc(1, sizeof **slot);
    return *slot;
}

static int arena_reserve(struct arena *a, size_t n) {
    a->used = 0;
    if (n <= a->cap) return 0;
    uint8_t *b = realloc(a->buf, n);
    if (!b) return -1;
    a->buf = b, a->cap = n;
    return 0;
}

static void *arena_alloc(struct arena *a, size_t n) {
    size_t at = (a->used + 15) & ~(size_t)15;
    if (at > a->cap || n > a->cap - at) return NULL;
    a->used = at + n;
    return a->buf + at;
}

void mn_wm_ring_end(void *ring) {
    struct arena **slot = (struct arena **)ring + 1;
    if (*slot) free((*slot)->buf);
    free(*slot);
    *slot = NULL;
}

struct hregion { uint8_t *host; const uint8_t *guest; uint32_t len; uint8_t kind; const struct mn_wm_field *f; };
struct unpacked {
    const uint8_t *rec;
    uint32_t len, pos;
    unsigned nregions;
    struct hregion regions[MN_WM_MAX_REGIONS];
    uint32_t out_bytes; /* what the reply's regions need */
};

/* Translates and copies one struct's fields in place (base is the host's copy); regions come from the record. */
static NTSTATUS_T unpack_fields(struct mn_wm_ctx *c, struct arena *a, struct unpacked *u,
                                const struct mn_wm_field *fields, unsigned n, uint8_t *base, int depth) {
    if (depth > MAX_DEPTH) return MN_STATUS_INVALID_PARAMETER;
    for (unsigned i = 0; i < n; i++) {
        const struct mn_wm_field *f = &fields[i];
        if (f->kind == WMF_HANDLE_IN) {
            mn_handle h;
            memcpy(&h, base + f->offset, sizeof h);
            if (!h) continue;
            int ok;
            void *obj = mn_ht_get(c->ht, h, (enum mn_hkind)f->hkind, &ok);
            if (!ok) return MN_STATUS_INVALID_HANDLE;
            memcpy(base + f->offset, &obj, sizeof obj);
            continue;
        }
        if (!is_ptr(f->kind)) continue;
        struct rhdr h;
        if (u->len - u->pos < sizeof h) return MN_STATUS_INVALID_PARAMETER;
        memcpy(&h, u->rec + u->pos, sizeof h);
        u->pos += sizeof h;
        void *null = NULL;
        if (!h.present) {
            memcpy(base + f->offset, &null, sizeof null);
            continue;
        }
        /* The length the guest claims must be the one this struct's own fields give. */
        uint64_t want = f->kind == WMF_STR_IN ? h.len : region_len(base, f, NULL);
        if (h.len != want || (f->kind == WMF_STR_IN && !h.len)) return MN_STATUS_INVALID_PARAMETER;
        const uint8_t *guest = NULL;
        if (f->kind != WMF_OUT) {
            if (h.len > u->len - u->pos) return MN_STATUS_INVALID_PARAMETER;
            guest = u->rec + u->pos;
            u->pos += PAD8(h.len);
        } else if (h.len > MN_REPLY_BYTES - u->out_bytes) {
            return MN_STATUS_BUFFER_TOO_SMALL; /* ponytail: large outputs through blobs are Task 5's */
        }
        uint8_t *host = arena_alloc(a, h.len ? h.len : 1);
        if (!host) return MN_STATUS_BUFFER_TOO_SMALL;
        if (guest) memcpy(host, guest, h.len);
        else memset(host, 0, h.len);
        if (f->kind == WMF_STR_IN && host[h.len - 1]) return MN_STATUS_INVALID_PARAMETER;
        if (f->kind == WMF_OUT || f->kind == WMF_INOUT) {
            if (u->nregions == MN_WM_MAX_REGIONS) return MN_STATUS_INVALID_PARAMETER;
            u->regions[u->nregions++] = (struct hregion){host, guest, h.len, f->kind, f};
            u->out_bytes += sizeof h + PAD8(h.len);
        }
        memcpy(base + f->offset, &host, sizeof host);
        const struct mn_wm_nested *nest = nested_of(f);
        uint32_t elem = f->flags & WMF_ARRAY ? f->size : h.len;
        for (uint64_t e = 0; nest && elem && e < h.len / elem; e++) {
            NTSTATUS_T s = unpack_fields(c, a, u, nest->fields, nest->nfields, host + e * elem, depth + 1);
            if (s) return s;
        }
    }
    return 0;
}

/* A region the host wrote, with the guest's own values back in its handle and pointer fields. */
static void restore(uint8_t *dst, const uint8_t *guest, const struct mn_wm_field *f, uint32_t len) {
    const struct mn_wm_nested *nest = nested_of(f);
    uint32_t elem = f->flags & WMF_ARRAY ? f->size : len;
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
    if (slot >= MN_WM_SLOTS) return failed(c, sync, MN_STATUS_INVALID_PARAMETER);
    const struct mn_wm_call *call = &mn_wm_calls[slot];
    if (call->mode == MN_WM_REFUSED) return failed(c, sync, call->refuse_status);
    uint32_t psz = PAD8(call->params_size);
    if (len < psz) return failed(c, sync, MN_STATUS_INVALID_PARAMETER);
    if (call->special == MN_SP_RETAIN || call->special == MN_SP_RELEASE) {
        mn_handle h;
        memcpy(&h, rec, sizeof h);
        int r = call->special == MN_SP_RETAIN ? mn_ht_retain(c->ht, h) : mn_ht_release(c->ht, ring, h);
        return r ? failed(c, sync, MN_STATUS_INVALID_HANDLE) : 0;
    }
    if (!generic(call->special)) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, MN_STATUS_NOT_IMPLEMENTED); /* Tasks 5-8 */
    }
    struct arena *a = arena_of(ring);
    if (!a || arena_reserve(a, (size_t)len + MN_REPLY_BYTES + 16 * (MN_WM_MAX_REGIONS + 2))) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, MN_STATUS_UNSUCCESSFUL);
    }
    uint8_t *p = arena_alloc(a, psz);
    memcpy(p, rec, call->params_size);
    mn_handle outs[MN_WM_MAX_OUTS];
    unsigned nouts = 0;
    for (unsigned i = 0; i < call->nfields && nouts < MN_WM_MAX_OUTS; i++)
        if (call->fields[i].kind == WMF_HANDLE_OUT) {
            memcpy(&outs[nouts++], p + call->fields[i].offset, sizeof(mn_handle));
            memset(p + call->fields[i].offset, 0, sizeof(mn_handle));
        }
    struct unpacked *u = arena_alloc(a, sizeof *u);
    *u = (struct unpacked){.rec = rec, .len = len, .pos = psz};
    NTSTATUS_T s = unpack_fields(c, a, u, call->fields, call->nfields, p, 0);
    if (!s && sync && psz + u->out_bytes > cap) s = MN_STATUS_BUFFER_TOO_SMALL;
    if (s) {
        if (!sync) bind_nil(c, ring, call, rec);
        return failed(c, sync, s);
    }
    s = mn_wm_invoke(slot, p);

    /* Bind what came back. A nil handle is no binding in a reply (the guest frees it), and a nil binding when queued. */
    int lost = 0;
    unsigned k = 0;
    for (unsigned i = 0; i < call->nfields; i++) {
        const struct mn_wm_field *f = &call->fields[i];
        if (f->kind != WMF_HANDLE_OUT) continue;
        mn_handle h = outs[k++];
        void *obj;
        memcpy(&obj, p + f->offset, sizeof obj);
        if (s) obj = NULL;
        if (!obj && sync) {
            memset(p + f->offset, 0, sizeof(mn_handle));
            continue;
        }
        if (obj && (f->own == MN_OWN_AUTORELEASED || f->own == MN_OWN_BORROWED)) mn_wm_invoke(0, &obj);
        if (mn_ht_bind(c->ht, ring, h, (enum mn_hkind)f->hkind, obj, (enum mn_own)f->own)) {
            if (obj) mn_wm_release_object((enum mn_hkind)f->hkind, obj);
            if (sync) memset(p + f->offset, 0, sizeof(mn_handle));
            lost = 1;
            continue;
        }
        if (f->hkind == MN_HK_POOL) mn_ht_pool_push(c->ht, ring, h);
        if (!obj) lost = 1; /* a queued creation that failed */
        memcpy(p + f->offset, &h, sizeof h);
    }
    if (!s && lost) s = sync ? MN_STATUS_INVALID_HANDLE : MN_STATUS_UNSUCCESSFUL;
    if (!sync) return s ? failed(c, 0, s) : 0;

    /* The reply: params with the guest's pointers and handles in, then the regions the host wrote. */
    uint8_t *r = reply;
    memcpy(r, p, call->params_size);
    for (unsigned i = 0; i < call->nfields; i++) {
        const struct mn_wm_field *f = &call->fields[i];
        if (f->kind == WMF_HANDLE_IN || is_ptr(f->kind))
            memcpy(r + f->offset, (const uint8_t *)rec + f->offset, 8);
    }
    uint32_t pos = psz;
    for (unsigned i = 0; i < u->nregions; i++) {
        struct hregion *g = &u->regions[i];
        struct rhdr h = {g->len, 1};
        memcpy(r + pos, &h, sizeof h);
        pos += sizeof h;
        memcpy(r + pos, g->host, g->len);
        restore(r + pos, g->guest, g->f, g->len);
        memset(r + pos + g->len, 0, PAD8(g->len) - g->len);
        pos += PAD8(g->len);
    }
    *reply_len = pos;
    return (int32_t)s;
}
#endif
