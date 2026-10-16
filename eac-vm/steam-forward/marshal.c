/* Marshalling of lsteamclient's Unix-call params (see marshal.h and wire.h). */
#include <stdlib.h>
#include <string.h>
#include "marshal.h"

/* ---- growable buffer, arena ---- */

void mn_buf_free(struct mn_buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

static int buf_put(struct mn_buf *b, const void *data, size_t n) {
    if (b->len + n > MN_MAX_MESSAGE + sizeof(struct mn_rep_hdr)) return MN_E_TOO_BIG;
    if (b->len + n > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n) cap *= 2;
        uint8_t *d = realloc(b->data, cap);
        if (!d) return MN_E_NOMEM;
        b->data = d; b->cap = cap;
    }
    if (n) memcpy(b->data + b->len, data, n);
    b->len += n;
    return 0;
}
static int buf_u32(struct mn_buf *b, uint32_t v) { return buf_put(b, &v, 4); }

void mn_arena_free(struct mn_arena *a) {
    for (size_t i = 0; i < a->n; i++) free(a->ptrs[i]);
    free(a->ptrs); a->ptrs = NULL; a->n = a->cap = 0;
}
static void *arena_alloc(struct mn_arena *a, size_t n) {
    if (a->n == a->cap) {
        size_t cap = a->cap ? a->cap * 2 : 16;
        void **p = realloc(a->ptrs, cap * sizeof(*p));
        if (!p) return NULL;
        a->ptrs = p; a->cap = cap;
    }
    void *m = calloc(1, n ? n : 1);
    if (m) a->ptrs[a->n++] = m;
    return m;
}

/* ---- reading the message ---- */

struct cur { const uint8_t *p; size_t left; };
static const uint8_t *take(struct cur *c, size_t n) {
    if (n > c->left) return NULL;
    const uint8_t *r = c->p; c->p += n; c->left -= n;
    return r;
}
static int take_u32(struct cur *c, uint32_t *v) {
    const uint8_t *p = take(c, 4);
    if (!p) return MN_E_FORMAT;
    memcpy(v, p, 4);
    return 0;
}

/* ---- fields ---- */

static void *get_ptr(const void *base, uint16_t off) { void *p; memcpy(&p, (const uint8_t *)base + off, sizeof p); return p; }
static void set_ptr(void *base, uint16_t off, void *v) { memcpy((uint8_t *)base + off, &v, sizeof v); }

/* A count field: unsigned, a negative signed value counts as 0. */
static uint64_t get_count(const void *base, uint16_t off, uint8_t width) {
    const uint8_t *p = (const uint8_t *)base + off;
    switch (width) {
    case 1: { int8_t v; memcpy(&v, p, 1); return v < 0 ? 0 : (uint64_t)v; }
    case 2: { int16_t v; memcpy(&v, p, 2); return v < 0 ? 0 : (uint64_t)v; }
    case 4: { int32_t v; memcpy(&v, p, 4); return v < 0 ? 0 : (uint64_t)v; }
    case 8: { uint64_t v; memcpy(&v, p, 8); return v; }
    }
    return 0;
}
static int32_t inner_count(const void *pointee, const struct mn_field *f) {
    int32_t n; memcpy(&n, (const uint8_t *)pointee + f->inner_size, 4);
    return n < 0 ? 0 : n;
}

/* Bytes a non-NULL field's pointee carries, from the params it belongs to (and for MN_INNER the pointee itself is
 * fixed-size). Negative on an impossible size. */
static int64_t field_bytes(const struct mn_field *f, const void *params, const void *pointee) {
    switch (f->kind) {
    case MN_STR_IN: { size_t n = strnlen(pointee, MN_MAX_MESSAGE); return n == MN_MAX_MESSAGE ? MN_E_TOO_BIG : (int64_t)n + 1; }
    case MN_FIXED: return f->size;
    case MN_BUF: {
        uint64_t count = get_count(params, f->size_offset, f->size_width);
        if (f->size && count > MN_MAX_MESSAGE / f->size) return MN_E_TOO_BIG;
        return (int64_t)(count * f->size);
    }
    }
    return MN_E_UNSUPPORTED;
}

/* ---- guest: request ---- */

int mn_pack_request(const struct mn_call_desc *d, uint32_t index, const void *params, struct mn_buf *out) {
    for (uint16_t i = 0; i < d->nfields; i++)
        if (d->fields[i].kind == MN_UNSUPPORTED) return MN_E_UNSUPPORTED;
    struct mn_req_hdr h = { MN_MAGIC, index, 0 };
    int r = buf_put(out, &h, sizeof h);
    if (!r) r = buf_put(out, params, d->params_size);
    for (uint16_t i = 0; !r && i < d->nfields; i++) {
        const struct mn_field *f = &d->fields[i];
        const void *p = get_ptr(params, f->offset);
        if (!p) { r = buf_u32(out, MN_NULL); continue; }
        int64_t n = field_bytes(f, params, p);
        if (n < 0) { r = (int)n; break; }
        if (!(r = buf_u32(out, (uint32_t)n))) r = buf_put(out, p, (size_t)n);
        if (!r && (f->flags & MN_INNER)) {
            const void *inner = get_ptr(p, f->inner_ptr);
            int32_t m = inner_count(p, f);
            if (!inner) r = buf_u32(out, MN_NULL);
            else if (!(r = buf_u32(out, (uint32_t)m))) r = buf_put(out, inner, (size_t)m);
        }
    }
    if (r) { out->len = 0; return r; }
    ((struct mn_req_hdr *)out->data)->len = (uint32_t)(out->len - sizeof h);
    return 0;
}

/* ---- host: request ---- */

int mn_unpack_request(const struct mn_call_desc *descs, uint32_t ndescs, const void *msg, size_t len,
                      uint32_t *index, void **host_params, struct mn_arena *arena) {
    struct cur c = { msg, len };
    const uint8_t *hp = take(&c, sizeof(struct mn_req_hdr));
    if (!hp) return MN_E_FORMAT;
    struct mn_req_hdr h; memcpy(&h, hp, sizeof h);
    if (h.magic != MN_MAGIC || h.index >= ndescs || h.len != c.left) return MN_E_FORMAT;
    const struct mn_call_desc *d = &descs[h.index];
    for (uint16_t i = 0; i < d->nfields; i++)
        if (d->fields[i].kind == MN_UNSUPPORTED) return MN_E_UNSUPPORTED;
    const uint8_t *pb = take(&c, d->params_size);
    if (!pb) return MN_E_FORMAT;
    uint8_t *params = arena_alloc(arena, d->params_size);
    if (!params) return MN_E_NOMEM;
    memcpy(params, pb, d->params_size);
    for (uint16_t i = 0; i < d->nfields; i++) {
        const struct mn_field *f = &d->fields[i];
        uint32_t n;
        if (take_u32(&c, &n)) return MN_E_FORMAT;
        if (n == MN_NULL) { set_ptr(params, f->offset, NULL); continue; }
        const uint8_t *src = take(&c, n);
        if (!src) return MN_E_FORMAT;
        if (f->kind == MN_STR_IN) {
            if (!n || src[n - 1]) return MN_E_FORMAT;
        } else if ((int64_t)n != field_bytes(f, params, NULL)) {
            return MN_E_FORMAT;
        }
        void *copy = arena_alloc(arena, n);
        if (!copy) return MN_E_NOMEM;
        memcpy(copy, src, n);
        set_ptr(params, f->offset, copy);
        if (f->flags & MN_INNER) {
            uint32_t m;
            if (take_u32(&c, &m)) return MN_E_FORMAT;
            if (m == MN_NULL) { set_ptr(copy, f->inner_ptr, NULL); continue; }
            const uint8_t *isrc = take(&c, m);
            if (!isrc || (int64_t)m != inner_count(copy, f)) return MN_E_FORMAT;
            void *icopy = arena_alloc(arena, m);
            if (!icopy) return MN_E_NOMEM;
            memcpy(icopy, isrc, m);
            set_ptr(copy, f->inner_ptr, icopy);
        }
    }
    if (c.left) return MN_E_FORMAT;
    *index = h.index;
    *host_params = params;
    return 0;
}

/* ---- host: reply ---- */

int mn_reg_log_add(struct mn_reg_log *log, uint32_t type, const char *key, size_t key_len, const char *name,
                   size_t name_len, const void *data, size_t data_len) {
    struct mn_reg_hdr h = { MN_REG_SET_VALUE, 0, type, (uint32_t)key_len, (uint32_t)name_len, (uint32_t)data_len };
    int r = buf_put(&log->entries, &h, sizeof h);
    if (!r) r = buf_put(&log->entries, key, key_len);
    if (!r) r = buf_put(&log->entries, name, name_len);
    if (!r) r = buf_put(&log->entries, data, data_len);
    if (!r) log->count++;
    return r;
}

int mn_pack_reply(const struct mn_call_desc *d, uint32_t status, const void *host_params,
                  const struct mn_reg_log *log, struct mn_buf *out) {
    struct mn_rep_hdr h = { MN_MAGIC, status, 0 };
    out->len = 0;
    int r = buf_put(out, &h, sizeof h);
    if (!r) r = buf_put(out, host_params, d->params_size);
    for (uint16_t i = 0; !r && i < d->nfields; i++) {
        const struct mn_field *f = &d->fields[i];
        const void *p = get_ptr(host_params, f->offset);
        if (!p || (f->flags & MN_IN_ONLY)) continue;
        int64_t n = field_bytes(f, host_params, p);
        if (n < 0) { r = (int)n; break; }
        if (!(r = buf_u32(out, (uint32_t)n))) r = buf_put(out, p, (size_t)n);
        if (!r && (f->flags & MN_INNER)) {
            const void *inner = get_ptr(p, f->inner_ptr);
            int32_t m = inner_count(p, f);
            if (!inner) r = buf_u32(out, MN_NULL);
            else if (!(r = buf_u32(out, (uint32_t)m))) r = buf_put(out, inner, (size_t)m);
        }
    }
    if (!r) r = buf_u32(out, log ? log->count : 0);
    if (!r && log) r = buf_put(out, log->entries.data, log->entries.len);
    if (r) { out->len = 0; return r; }
    ((struct mn_rep_hdr *)out->data)->len = (uint32_t)(out->len - sizeof h);
    return 0;
}

/* ---- guest: reply ---- */

/* Walks the reply's fields against the caller's params. With apply == 0 it only checks every length; with apply == 1
 * it copies into the caller's memory (after a successful check, so a bad reply changes nothing). */
static int reply_fields(const struct mn_call_desc *d, struct cur *c, void *params, int apply) {
    for (uint16_t i = 0; i < d->nfields; i++) {
        const struct mn_field *f = &d->fields[i];
        void *p = get_ptr(params, f->offset);
        if (!p || (f->flags & MN_IN_ONLY)) continue;
        uint32_t n;
        if (take_u32(c, &n)) return MN_E_FORMAT;
        const uint8_t *src = take(c, n);
        if (!src) return MN_E_FORMAT;
        if ((int64_t)n != field_bytes(f, params, p)) return MN_E_MISMATCH;
        void *inner = (f->flags & MN_INNER) ? get_ptr(p, f->inner_ptr) : NULL;
        int32_t want_inner = (f->flags & MN_INNER) ? inner_count(p, f) : 0;
        if (apply) {
            memcpy(p, src, n);
            if (f->flags & MN_INNER) set_ptr(p, f->inner_ptr, inner); /* the caller's buffer, not the host's */
        }
        if (f->flags & MN_INNER) {
            uint32_t m;
            if (take_u32(c, &m)) return MN_E_FORMAT;
            if (m == MN_NULL) { if (inner) return MN_E_MISMATCH; continue; }
            const uint8_t *isrc = take(c, m);
            if (!isrc) return MN_E_FORMAT;
            if (!inner || (int64_t)m != want_inner) return MN_E_MISMATCH;
            if (apply) memcpy(inner, isrc, m);
        }
    }
    return 0;
}

int mn_unpack_reply(const struct mn_call_desc *d, const void *msg, size_t len, void *params, uint32_t *status,
                    mn_reg_fn apply, void *ctx) {
    struct cur c = { msg, len };
    const uint8_t *hp = take(&c, sizeof(struct mn_rep_hdr));
    if (!hp) return MN_E_FORMAT;
    struct mn_rep_hdr h; memcpy(&h, hp, sizeof h);
    if (h.magic != MN_MAGIC || h.len != c.left) return MN_E_FORMAT;
    const uint8_t *pb = take(&c, d->params_size);
    if (!pb) return MN_E_FORMAT;

    /* Check everything first against the caller's own params. */
    struct cur check = c;
    int r = reply_fields(d, &check, params, 0);
    if (r) return r;
    uint32_t nreg;
    if (take_u32(&check, &nreg)) return MN_E_FORMAT;
    struct cur regs = check;
    for (uint32_t i = 0; i < nreg; i++) {
        const uint8_t *rh = take(&check, sizeof(struct mn_reg_hdr));
        if (!rh) return MN_E_FORMAT;
        struct mn_reg_hdr e; memcpy(&e, rh, sizeof e);
        if (!take(&check, e.key_len) || !take(&check, e.name_len) || !take(&check, e.data_len)) return MN_E_FORMAT;
    }
    if (check.left) return MN_E_FORMAT;

    /* Then apply: the params' bytes, with the caller's own pointers kept, then the pointees. */
    void *saved[64];
    if (d->nfields > 64) return MN_E_UNSUPPORTED;
    for (uint16_t i = 0; i < d->nfields; i++) saved[i] = get_ptr(params, d->fields[i].offset);
    uint8_t *before = malloc(d->params_size ? d->params_size : 1);
    if (!before) return MN_E_NOMEM;
    memcpy(before, params, d->params_size);
    memcpy(params, pb, d->params_size);
    for (uint16_t i = 0; i < d->nfields; i++) set_ptr(params, d->fields[i].offset, saved[i]);
    /* Counts are inputs: the lengths were checked against the caller's values, so keep those. */
    for (uint16_t i = 0; i < d->nfields; i++) {
        const struct mn_field *f = &d->fields[i];
        if (f->kind == MN_BUF) memcpy((uint8_t *)params + f->size_offset, before + f->size_offset, f->size_width);
    }
    free(before);
    reply_fields(d, &c, params, 1);
    for (uint32_t i = 0; i < nreg; i++) {
        struct mn_reg_hdr e; memcpy(&e, take(&regs, sizeof e), sizeof e);
        const char *key = (const char *)take(&regs, e.key_len);
        const char *name = (const char *)take(&regs, e.name_len);
        const void *data = take(&regs, e.data_len);
        if (apply) apply(ctx, e.type, key, e.key_len, name, e.name_len, data, e.data_len);
    }
    *status = h.status;
    return 0;
}

/* ---- table identity ---- */

static uint32_t fnv(uint32_t h, uint64_t v) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(v >> (8 * i)); h *= 16777619u; }
    return h;
}
uint32_t mn_table_checksum(const struct mn_call_desc *descs, unsigned n) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; i++) {
        const struct mn_call_desc *d = &descs[i];
        h = fnv(h, d->params_size); h = fnv(h, d->nfields);
        for (uint16_t j = 0; j < d->nfields; j++) {
            const struct mn_field *f = &d->fields[j];
            h = fnv(h, f->offset); h = fnv(h, f->kind); h = fnv(h, f->flags); h = fnv(h, f->size);
            h = fnv(h, f->size_offset); h = fnv(h, f->size_width); h = fnv(h, f->inner_ptr); h = fnv(h, f->inner_size);
        }
    }
    return h;
}
