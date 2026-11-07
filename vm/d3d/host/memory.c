/* Arenas (M2 Task 7): native DXMT's CPU-visible buffers in memory the guest shares, so a mapped resource is the
 * guest's own memory. The guest hands over regions (op 1); native winemetal's memory hooks (WMTSetMemoryHooks, fork
 * 0076) carve 16 KiB pieces from them, first fit, during a creation (a call with a handle out) of the context running
 * on this thread: every CPU-visible buffer DXMT makes in it, the object's and DXMT's own (a shader-visible descriptor
 * heap's argument buffer), as guest DXMT's all come from guest memory. A creation that finds no room is made in host
 * memory (DXMT doesn't survive a failed buffer), and the dispatcher lets it go and answers MN_D3D_NEED_ARENA with the
 * bytes it wanted. Buffers made inside other calls get host memory the guest never sees. A piece is the arena's again
 * once Metal lets its buffer go, on whichever thread; an arena lives as long as its context, or after it until its
 * last piece comes back. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../../metal/host/executor.h"
#include "dispatch.h"

#define PIECE 16384ull

struct span { uint64_t off, len; };
struct spans { struct span *v; unsigned n, cap; };
struct arena {
    uint32_t ctx_id;
    int dead;
    uint64_t blob, offset, len, used;
    uint8_t *ptr;
    void *hold;
    struct span *free;
    unsigned nfree, cap;
    struct spans taken; /* ponytail: a linear list of the pieces out; a map when buffers get many */
    struct arena *next;
};
struct own { uint8_t *ptr; uint64_t len; struct own *next; }; /* host memory handed out instead */

/* ponytail: one lock and linear lists for every context; per-context trees if arenas or pieces get many */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct arena *arenas;
static struct own *owns;
static _Thread_local struct { uint32_t ctx_id; int creation; uint64_t need, largest; } call;

/* A published, address-sorted snapshot of the arenas for the hot read (mn_d3d_arena_of, on every mapped-buffer Map).
 * The arena list is the source of truth under lock; a structural change rebuilds this snapshot in place. Value-only
 * entries read under a seqlock: a reader that overlaps a rebuild sees a changed sequence and retries, and nothing is
 * ever freed (a fixed array), so no preempted reader can touch freed memory. Beyond the array's room the snapshot is
 * marked full and readers fall back to the list under lock. */
#define ARENA_SNAP_MAX 2048
struct arena_span { uintptr_t base, end; uint64_t blob, offset; };
static _Atomic uint64_t arena_seq; /* even: stable; odd: a rebuild is in flight */
static _Atomic size_t arena_n;    /* snapshot entries, or SIZE_MAX: too many, take the lock */
static struct arena_span arena_snap[ARENA_SNAP_MAX];

static int span_by_base(const void *x, const void *y) {
    uintptr_t a = ((const struct arena_span *)x)->base, b = ((const struct arena_span *)y)->base;
    return a < b ? -1 : a > b;
}

/* Under lock: rebuild the snapshot, then bump the sequence even again (release). */
static void snap_rebuild(void) {
    uint64_t s = atomic_load_explicit(&arena_seq, memory_order_relaxed);
    atomic_store_explicit(&arena_seq, s + 1, memory_order_release); /* odd: a reader retries */
    atomic_thread_fence(memory_order_acq_rel);
    size_t n = 0;
    int over = 0;
    for (struct arena *a = arenas; a; a = a->next) {
        if (a->dead) continue;
        if (n == ARENA_SNAP_MAX) { over = 1; break; }
        arena_snap[n++] = (struct arena_span){(uintptr_t)a->ptr, (uintptr_t)(a->ptr + a->len), a->blob, a->offset};
    }
    if (n > 1) qsort(arena_snap, n, sizeof *arena_snap, span_by_base);
    atomic_store_explicit(&arena_n, over ? SIZE_MAX : n, memory_order_relaxed);
    atomic_store_explicit(&arena_seq, s + 2, memory_order_release);
}

int mn_d3d_arena_add(uint32_t ctx_id, uint64_t blob, uint64_t offset, uint64_t len) {
    if (!len || offset % PIECE || len % PIECE) return -1;
    struct arena *a = calloc(1, sizeof *a);
    struct span *s = malloc(sizeof *s);
    void *hold;
    uint8_t *p = a && s ? mn_executor_blob_hold(ctx_id, blob, offset, len, &hold) : NULL;
    if (!p) {
        free(a), free(s);
        return -1;
    }
    *s = (struct span){0, len};
    *a = (struct arena){.ctx_id = ctx_id, .blob = blob, .offset = offset, .len = len, .ptr = p, .hold = hold,
                        .free = s, .nfree = 1, .cap = 1};
    pthread_mutex_lock(&lock);
    a->next = arenas;
    arenas = a;
    snap_rebuild();
    pthread_mutex_unlock(&lock);
    return 0;
}

/* Under lock: an arena gone with its context, its pieces all back, goes. */
static void reap(struct arena **pp) {
    struct arena *a = *pp;
    if (!a->dead || a->used) return;
    *pp = a->next;
    mn_executor_blob_release(a->hold);
    free(a->free);
    free(a->taken.v);
    free(a);
    snap_rebuild();
}

static void *take(struct arena *a, uint64_t len) {
    if (a->taken.n == a->taken.cap) {
        struct span *g = realloc(a->taken.v, (a->taken.cap * 2 + 16) * sizeof *g);
        if (!g) return NULL;
        a->taken.v = g, a->taken.cap = a->taken.cap * 2 + 16;
    }
    for (unsigned i = 0; i < a->nfree; i++) {
        struct span *s = &a->free[i];
        if (s->len < len) continue;
        uint64_t off = s->off;
        a->taken.v[a->taken.n++] = (struct span){off, len};
        s->off += len, s->len -= len;
        if (!s->len) memmove(s, s + 1, (a->nfree - i - 1) * sizeof *s), a->nfree--;
        a->used += len;
        return a->ptr + off;
    }
    return NULL;
}

/* A piece back, its neighbours joined. Under lock. */
static void give(struct arena *a, uint64_t off, uint64_t len) {
    unsigned i = 0;
    while (i < a->nfree && a->free[i].off < off) i++;
    int before = i && a->free[i - 1].off + a->free[i - 1].len == off;
    int after = i < a->nfree && off + len == a->free[i].off;
    a->used -= len;
    if (before && after) {
        a->free[i - 1].len += len + a->free[i].len;
        memmove(&a->free[i], &a->free[i + 1], (a->nfree - i - 1) * sizeof *a->free);
        a->nfree--;
    } else if (before) {
        a->free[i - 1].len += len;
    } else if (after) {
        a->free[i].off = off, a->free[i].len += len;
    } else {
        if (a->nfree == a->cap) {
            struct span *g = realloc(a->free, (a->cap * 2 + 4) * sizeof *g);
            if (!g) return; /* the piece is lost to the arena, nothing worse */
            a->free = g, a->cap = a->cap * 2 + 4;
        }
        memmove(&a->free[i + 1], &a->free[i], (a->nfree - i) * sizeof *a->free);
        a->free[i] = (struct span){off, len};
        a->nfree++;
    }
}

static void *hook_alloc(uint64_t length, void *ctx) {
    (void)ctx;
    uint64_t len = (length + PIECE - 1) / PIECE * PIECE;
    if (!len) len = PIECE;
    if (len > call.largest) call.largest = len;
    uint8_t *p = NULL;
    if (call.creation) {
        pthread_mutex_lock(&lock);
        for (struct arena *a = arenas; a && !p; a = a->next)
            if (a->ctx_id == call.ctx_id && !a->dead) p = take(a, len);
        pthread_mutex_unlock(&lock);
        if (p) memset(p, 0, len); /* a new buffer's memory is zero, as Metal's own is; the piece may have been used */
        else call.need += len;    /* a creation may make several */
    }
    if (!p) {
        struct own *o = malloc(sizeof *o);
        p = o ? mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0) : NULL;
        if (!o || p == MAP_FAILED) return free(o), NULL;
        *o = (struct own){p, len, NULL};
        pthread_mutex_lock(&lock);
        o->next = owns;
        owns = o;
        pthread_mutex_unlock(&lock);
    }
    return p;
}

static void hook_free(void *ptr, void *ctx) {
    (void)ctx;
    uint8_t *p = ptr;
    pthread_mutex_lock(&lock);
    for (struct arena **pp = &arenas; *pp; pp = &(*pp)->next) {
        struct arena *a = *pp;
        if (p < a->ptr || p >= a->ptr + a->len) continue;
        uint64_t off = (uint64_t)(p - a->ptr);
        for (unsigned i = 0; i < a->taken.n; i++)
            if (a->taken.v[i].off == off) {
                give(a, off, a->taken.v[i].len);
                a->taken.v[i] = a->taken.v[--a->taken.n];
                break;
            }
        reap(pp);
        pthread_mutex_unlock(&lock);
        return;
    }
    for (struct own **pp = &owns; *pp; pp = &(*pp)->next)
        if ((*pp)->ptr == p) {
            struct own *o = *pp;
            *pp = o->next;
            pthread_mutex_unlock(&lock);
            munmap(o->ptr, o->len);
            free(o);
            return;
        }
    pthread_mutex_unlock(&lock);
    fprintf(stderr, "mn-d3d: error: a buffer's memory the hooks never gave (%p)\n", ptr);
}

void mn_d3d_memory_hooks(void (*set)(void *(*alloc)(uint64_t, void *), void (*free)(void *, void *), void *ctx)) {
    set(hook_alloc, hook_free, NULL);
}

void mn_d3d_memory_begin(uint32_t ctx_id, int creation) {
    call.ctx_id = ctx_id, call.creation = creation, call.need = 0, call.largest = 0;
}

uint64_t mn_d3d_memory_largest(void) { return call.largest; }

void mn_d3d_memory_want(uint64_t len) { call.need += (len + PIECE - 1) / PIECE * PIECE; }

int mn_d3d_arena_fits(uint32_t ctx_id, uint64_t len) {
    len = (len + PIECE - 1) / PIECE * PIECE;
    int fits = 0;
    pthread_mutex_lock(&lock);
    for (struct arena *a = arenas; a && !fits; a = a->next)
        for (unsigned i = 0; a->ctx_id == ctx_id && !a->dead && i < a->nfree && !fits; i++) fits = a->free[i].len >= len;
    pthread_mutex_unlock(&lock);
    return fits;
}

uint64_t mn_d3d_memory_end(void) {
    uint64_t need = call.need;
    call.creation = 0, call.need = 0;
    return need;
}

int mn_d3d_arena_of(const void *ptr, uint64_t *blob, uint64_t *offset) {
    uintptr_t p = (uintptr_t)ptr;
    for (;;) {
        uint64_t s0 = atomic_load_explicit(&arena_seq, memory_order_acquire);
        if (s0 & 1) continue; /* a rebuild is in flight */
        size_t n = atomic_load_explicit(&arena_n, memory_order_relaxed);
        if (n == SIZE_MAX) break; /* too many to snapshot: the list under lock (never here in practice) */
        size_t lo = 0, hi = n;
        uintptr_t base = 0;
        uint64_t b = 0, off = 0;
        int hit = 0;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            uintptr_t bmid = atomic_load_explicit((_Atomic uintptr_t *)&arena_snap[mid].base, memory_order_relaxed);
            uintptr_t emid = atomic_load_explicit((_Atomic uintptr_t *)&arena_snap[mid].end, memory_order_relaxed);
            if (p < bmid) {
                hi = mid;
            } else if (p >= emid) {
                lo = mid + 1;
            } else {
                base = bmid;
                b = arena_snap[mid].blob, off = arena_snap[mid].offset;
                hit = 1;
                break;
            }
        }
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&arena_seq, memory_order_relaxed) == s0) {
            if (!hit) return -1;
            *blob = b, *offset = off + (uint64_t)(p - base);
            return 0;
        }
        /* a rebuild raced us: retry */
    }
    int found = -1;
    pthread_mutex_lock(&lock);
    for (struct arena *a = arenas; a && found; a = a->next)
        if (p >= (uintptr_t)a->ptr && p < (uintptr_t)(a->ptr + a->len))
            *blob = a->blob, *offset = a->offset + (uint64_t)(p - (uintptr_t)a->ptr), found = 0;
    pthread_mutex_unlock(&lock);
    return found;
}

void mn_d3d_arenas_end(uint32_t ctx_id) {
    pthread_mutex_lock(&lock);
    for (struct arena **pp = &arenas; *pp;) {
        struct arena *a = *pp;
        if (a->ctx_id == ctx_id) a->dead = 1;
        reap(pp);
        if (*pp == a) pp = &a->next;
    }
    snap_rebuild(); /* reap only rebuilds when it frees one; a context's arenas still in use lose their dead flag here */
    pthread_mutex_unlock(&lock);
}

uint64_t mn_d3d_arena_free(uint32_t ctx_id) {
    uint64_t n = 0;
    pthread_mutex_lock(&lock);
    for (struct arena *a = arenas; a; a = a->next)
        if (a->ctx_id == ctx_id && !a->dead) n += a->len - a->used;
    pthread_mutex_unlock(&lock);
    return n;
}
