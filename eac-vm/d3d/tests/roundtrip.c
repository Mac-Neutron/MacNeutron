/* Every forwarded Direct3D call through the marshaller with the generator's tables (M2 Task 3): guest and host in one
 * process, no transport (as eac-vm/metal/wm/tests/kinds-test.c). For each call roundtrip-cases.c names, a walker
 * fills its params from its fields table (handles bound to stand-in objects, two elements an array, strings, the
 * first variant with pointers of a union), mn_wm_pack_call packs it, mn_wm_host_call_table unpacks it, and the
 * invoke walks the host's params against the guest's: handles translated, data equal outside the slots that hold
 * pointers, strings converted. Out regions come back as the host wrote them. Prints "roundtrip: ok <n> calls" or each
 * call that failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "marshal.h"
#include "../../guest/transport.h"
#include "loopback.h"
#include "mnd3d_params.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];
extern const unsigned mn_d3d_roundtrip_count, mn_d3d_roundtrip_ids[];
extern struct mn_wm_ctx loop_ctx;

unsigned int NtSetEvent(void *handle, void *prev) { (void)handle, (void)prev; return 0; } /* winemetal.c's listener */

#define NOBJ 4
static int objs[NOBJ], made; /* the host's stand-in objects, and one a call creates */
static mn_handle hs[NOBJ];
static const char *why; /* the first mismatch of the current call */
#define EXPECT(c, w) do { if (!(c) && !why) why = (w); } while (0)

/* "caché 😀" as the guest's UTF-16, and as the host's wchar_t. */
static const uint16_t wname[] = {'c', 'a', 'c', 'h', 0xe9, ' ', 0xd83d, 0xde00, 0};
static const wchar_t *const wnative = L"caché \U0001F600";

static uint8_t pool[1 << 20];
static size_t used;
static void *take(size_t n) {
    used = (used + 15) & ~(size_t)15;
    if (used + n > sizeof pool) abort();
    void *p = pool + used;
    used += n;
    return p;
}

static uint64_t get(const uint8_t *at, unsigned width) {
    uint64_t v = 0;
    memcpy(&v, at, width); /* little-endian, both sides */
    return v;
}
static void put(uint8_t *at, unsigned width, uint64_t v) { memcpy(at, &v, width); }

static const struct mn_wm_nested *nest_of(const struct mn_wm_tables *t, const struct mn_wm_field *f) {
    return f->nested && f->kind != WMF_UNION ? &t->nested[f->nested - 1] : NULL;
}
/* A union's variant for the discriminant's value in base, as the marshaller picks it. */
static const struct mn_wm_nested *variant(const struct mn_wm_tables *t, const struct mn_wm_field *f,
                                          const uint8_t *base) {
    const struct mn_wm_union *u = &t->unions[f->nested - 1];
    uint64_t d = get(base + f->count_offset, f->count_width);
    for (unsigned i = 0; i < u->nvalues; i++)
        if (u->values[i] == d) return u->nested[i] ? &t->nested[u->nested[i] - 1] : NULL;
    return NULL;
}
static uint64_t count_of(const struct mn_wm_field *f, const uint8_t *base) {
    const uint8_t *at = base + f->count_offset;
    if (f->flags & WMF_DEREF) {
        memcpy(&at, at, sizeof(const uint8_t *));
        if (!at) return 0;
    }
    return get(at, f->count_width);
}
static int is_data(uint8_t k) { return k == WMF_IN || k == WMF_OUT || k == WMF_INOUT; }
static uint64_t data_len(const struct mn_wm_field *f, const uint8_t *base) {
    return f->flags & WMF_ARRAY ? count_of(f, base) * f->size : f->size;
}

/* The guest's params: every field filled. seed varies the handles and the bytes. */
static void fill(const struct mn_wm_tables *t, const struct mn_wm_field *fs, unsigned n, uint8_t *base, int depth,
                 unsigned seed) {
    for (unsigned i = 0; i < n && depth < 8; i++) {
        const struct mn_wm_field *f = &fs[i];
        uint8_t *at = base + f->offset;
        switch (f->kind) {
        case WMF_HANDLE_IN: put(at, 8, hs[(seed + i) % NOBJ]); break;
        case WMF_STR_IN: { const char *s = "ab"; memcpy(at, &s, 8); break; }
        case WMF_WSTR_IN: { const uint16_t *s = wname; memcpy(at, &s, 8); break; }
        case WMF_UNION: {
            const struct mn_wm_union *u = &t->unions[f->nested - 1];
            put(base + f->count_offset, f->count_width, u->values[0]);
            const struct mn_wm_nested *v = variant(t, f, base);
            if (v) fill(t, v->fields, v->nfields, at, depth + 1, seed + 1);
            break;
        }
        case WMF_IN:
        case WMF_OUT:
        case WMF_INOUT: {
            if (f->flags & WMF_ARRAY) {
                uint8_t *c = base + f->count_offset;
                if (f->flags & WMF_DEREF) memcpy(&c, c, sizeof(uint8_t *)); /* an earlier inout field's memory */
                put(c, f->count_width, 2);
            }
            uint64_t len = data_len(f, base);
            uint8_t *p = take(len ? len : 1);
            for (uint64_t b = 0; b < len; b++) p[b] = (uint8_t)(seed * 31 + b);
            memcpy(at, &p, 8);
            const struct mn_wm_nested *nest = nest_of(t, f);
            uint64_t elem = f->flags & WMF_ARRAY ? f->size : len;
            for (uint64_t e = 0; nest && elem && e < len / elem; e++)
                fill(t, nest->fields, nest->nfields, p + e * elem, depth + 1, seed + 2 + (unsigned)e);
            break;
        }
        default: break; /* WMF_HANDLE_OUT: the marshaller picks it */
        }
    }
}

/* Zeroes the slots a field list translates (handles, pointers), so the rest of two copies compares byte for byte. */
static void mask(const struct mn_wm_tables *t, const struct mn_wm_nested *nest, uint8_t *p, const uint8_t *guest) {
    for (unsigned i = 0; nest && i < nest->nfields; i++) {
        const struct mn_wm_field *g = &nest->fields[i];
        if (g->kind == WMF_UNION) mask(t, variant(t, g, guest), p + g->offset, guest + g->offset);
        else memset(p + g->offset, 0, 8);
    }
}
static int same_outside(const struct mn_wm_tables *t, const struct mn_wm_nested *nest, const uint8_t *a,
                        const uint8_t *guest, uint64_t elem, uint64_t len) {
    uint8_t *x = malloc(len ? len : 1), *y = malloc(len ? len : 1);
    memcpy(x, a, len);
    memcpy(y, guest, len);
    for (uint64_t e = 0; nest && elem && e < len / elem; e++) {
        mask(t, nest, x + e * elem, guest + e * elem);
        mask(t, nest, y + e * elem, guest + e * elem);
    }
    int same = !memcmp(x, y, len);
    free(x), free(y);
    return same;
}

static void *host_obj(mn_handle h) {
    for (unsigned i = 0; i < NOBJ; i++)
        if (hs[i] == h) return &objs[i];
    return NULL;
}

/* The host's params against the guest's. */
static void check(const struct mn_wm_tables *t, const struct mn_wm_field *fs, unsigned n, uint8_t *host,
                  const uint8_t *guest, int depth) {
    for (unsigned i = 0; i < n && depth < 8; i++) {
        const struct mn_wm_field *f = &fs[i];
        uint64_t hv, gv;
        memcpy(&hv, host + f->offset, 8);
        memcpy(&gv, guest + f->offset, 8);
        switch (f->kind) {
        case WMF_HANDLE_IN: EXPECT(hv == (uintptr_t)host_obj(gv), "a handle didn't translate"); break;
        case WMF_HANDLE_OUT: memcpy(host + f->offset, &(void *){&made}, 8); break;
        case WMF_STR_IN: EXPECT(hv && !strcmp((const char *)(uintptr_t)hv, "ab"), "a string"); break;
        case WMF_WSTR_IN: EXPECT(hv && !wcscmp((const wchar_t *)(uintptr_t)hv, wnative), "a wide string"); break;
        case WMF_UNION: {
            const struct mn_wm_nested *v = variant(t, f, guest);
            EXPECT(get(host + f->count_offset, f->count_width) == get(guest + f->count_offset, f->count_width),
                   "a union's discriminant");
            if (v) check(t, v->fields, v->nfields, host + f->offset, guest + f->offset, depth + 1);
            break;
        }
        case WMF_IN:
        case WMF_OUT:
        case WMF_INOUT: {
            uint64_t len = data_len(f, guest);
            EXPECT(len == data_len(f, host), "a length");
            EXPECT(hv && hv != gv, "data wasn't the host's copy");
            if (!hv || hv == gv) break;
            uint8_t *h = (uint8_t *)(uintptr_t)hv;
            const uint8_t *g = (const uint8_t *)(uintptr_t)gv;
            const struct mn_wm_nested *nest = nest_of(t, f);
            uint64_t elem = f->flags & WMF_ARRAY ? f->size : len;
            if (f->kind != WMF_OUT) {
                EXPECT(same_outside(t, nest, h, g, elem, len), "data in differs");
                for (uint64_t e = 0; nest && elem && e < len / elem; e++)
                    check(t, nest->fields, nest->nfields, h + e * elem, g + e * elem, depth + 1);
            }
            break;
        }
        default: break;
        }
    }
}

/* Then the host's answer: out regions written with the pattern 0xa5 ^ byte (lengths read before any is written). */
static void write_out(const struct mn_wm_field *fs, unsigned n, uint8_t *host) {
    uint64_t lens[64];
    for (unsigned i = 0; i < n && i < 64; i++) lens[i] = is_data(fs[i].kind) ? data_len(&fs[i], host) : 0;
    for (unsigned i = 0; i < n && i < 64; i++) {
        uint8_t *h;
        memcpy(&h, host + fs[i].offset, 8);
        if (!is_data(fs[i].kind) || fs[i].kind == WMF_IN || !h) continue;
        for (uint64_t b = 0; b < lens[i]; b++) h[b] = (uint8_t)(0xa5 ^ b);
    }
}

static const struct mn_wm_call *cur;
static uint8_t guest_copy[4096];
static int ran;
static NTSTATUS_T invoke(void *params) {
    ran = 1;
    check(cur->tables, cur->fields, cur->nfields, params, guest_copy, 0);
    write_out(cur->fields, cur->nfields, params);
    return 0;
}

/* After the reply: out regions as the host wrote them (pointer slots back to the guest's own). */
static void check_back(const struct mn_wm_tables *t, const struct mn_wm_field *fs, unsigned n, const uint8_t *base) {
    for (unsigned i = 0; i < n; i++) {
        const struct mn_wm_field *f = &fs[i];
        if (!is_data(f->kind) || f->kind == WMF_IN) continue;
        const uint8_t *g;
        memcpy(&g, base + f->offset, 8);
        uint64_t len = f->flags & WMF_DEREF ? 2 * f->size : data_len(f, base); /* the count came back rewritten */
        uint8_t *want = malloc(len ? len : 1);
        for (uint64_t b = 0; b < len; b++) want[b] = (uint8_t)(0xa5 ^ b);
        uint64_t elem = f->flags & WMF_ARRAY ? f->size : len;
        EXPECT(same_outside(t, nest_of(t, f), want, g, elem, len), "data out didn't come back");
        free(want);
    }
}

int main(void) {
    mn_open();
    for (unsigned i = 0; i < NOBJ; i++) {
        hs[i] = mn_gh_alloc(MN_OWN_PLUS1);
        if (mn_ht_bind(loop_ctx.ht, loop_ring(), hs[i], MN_HK_D3D, &objs[i], MN_OWN_PLUS1)) return 1;
    }
    static uint8_t rec[MN_REC_MAX], reply[MN_REPLY_BYTES];
    unsigned bad = 0;
    for (unsigned k = 0; k < mn_d3d_roundtrip_count; k++) {
        cur = &mn_d3d_calls[mn_d3d_roundtrip_ids[k]];
        why = NULL, ran = 0, used = 0;
        uint8_t *params = take(cur->params_size);
        fill(cur->tables, cur->fields, cur->nfields, params, 0, k);
        memcpy(guest_copy, params, cur->params_size);
        struct mn_wm_out o = {.buf = rec, .cap = sizeof rec};
        uint32_t n = 0;
        if (mn_wm_pack_call(cur, params, &o)) why = "pack";
        memcpy(guest_copy, params, cur->params_size); /* with the handles it picked */
        int32_t st = why ? 0 : mn_wm_host_call_table(&loop_ctx, loop_ring(), cur, invoke, rec, o.len,
                                                     o.sync ? reply : NULL, o.sync ? sizeof reply : 0, &n);
        static char msg[64];
        if (st) snprintf(msg, sizeof msg, "host call 0x%x", (unsigned)st), why = msg;
        if (!why && !ran) why = "never invoked";
        if (!why && o.sync && mn_wm_unpack_call(cur, params, &o, reply, n)) why = "unpack";
        if (!why && o.sync) check_back(cur->tables, cur->fields, cur->nfields, params);
        if (why) printf("roundtrip: FAIL %u %s: %s\n", mn_d3d_roundtrip_ids[k], cur->name, why), bad++;
    }
    if (!bad) printf("roundtrip: ok %u calls\n", mn_d3d_roundtrip_count);
    return bad != 0;
}
