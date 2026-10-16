/* The guest's winemetal handles (handles.h), one table per process: indexes in chunks that never move, references
 * counted with atomics, and autorelease pools mirrored per thread so that both sides free the same handles. */
#include "../handles.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

#define CHUNK 4096u

struct gentry {
    _Atomic uint32_t refs;
    _Atomic uint32_t gen; /* the handle's generation is gen + 1 */
    _Atomic uint8_t pool;
    _Atomic uint64_t attached;
};

void (*mn_gh_on_free)(uint64_t value);

static struct gentry *_Atomic chunks[MN_H_MAX / CHUNK];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
/* Freed indexes, oldest first: one is reused only once MN_H_QUARANTINE others were freed after it, so a release still
 * queued on the host has long run before its index comes back. */
static uint32_t fifo[MN_H_MAX], fifo_head, fifo_count;
static uint32_t fresh = 1;

struct gpool { mn_handle h; mn_handle *items; size_t n, cap; };
static _Thread_local struct gpool *pools;
static _Thread_local size_t npools, cap_pools;

static struct gentry *entry(uint32_t i) {
    if (!i || i >= MN_H_MAX) return NULL;
    struct gentry *c = chunks[i / CHUNK];
    return c ? &c[i % CHUNK] : NULL;
}

/* The entry of a live handle, else NULL. */
static struct gentry *live(mn_handle h) {
    struct gentry *e = entry(MN_H_INDEX(h));
    return e && e->gen + 1 == MN_H_GEN(h) && e->refs ? e : NULL;
}

mn_handle mn_gh_alloc(enum mn_own own) {
    uint32_t i;
    pthread_mutex_lock(&lock);
    if (fifo_count > MN_H_QUARANTINE || (fresh >= MN_H_MAX && fifo_count)) {
        i = fifo[fifo_head];
        fifo_head = (fifo_head + 1) % MN_H_MAX;
        fifo_count--;
    } else if (fresh < MN_H_MAX) {
        i = fresh++;
        if (!chunks[i / CHUNK]) {
            struct gentry *c = calloc(CHUNK, sizeof *c);
            if (!c) {
                fresh--;
                pthread_mutex_unlock(&lock);
                return 0;
            }
            chunks[i / CHUNK] = c;
        }
    } else {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    struct gentry *e = entry(i);
    e->pool = 0;
    e->attached = 0;
    e->refs = 1;
    mn_handle h = (uint64_t)(e->gen + 1) << 32 | i;
    pthread_mutex_unlock(&lock);
    if (own == MN_OWN_AUTORELEASED && npools) {
        struct gpool *p = &pools[npools - 1];
        if (p->n == p->cap) {
            mn_handle *items = realloc(p->items, (p->cap ? p->cap * 2 : 16) * sizeof *items);
            if (!items) return h; /* ponytail: it then lives until exit, as with no pool open */
            p->items = items, p->cap = p->cap ? p->cap * 2 : 16;
        }
        p->items[p->n++] = h;
    }
    return h;
}

static void free_index(struct gentry *e, uint32_t i) {
    uint64_t attached = atomic_exchange(&e->attached, 0);
    pthread_mutex_lock(&lock);
    e->pool = 0;
    e->gen++;
    fifo[(fifo_head + fifo_count) % MN_H_MAX] = i;
    fifo_count++;
    pthread_mutex_unlock(&lock);
    if (attached && mn_gh_on_free) mn_gh_on_free(attached);
}

void mn_gh_attach(mn_handle h, uint64_t value) {
    struct gentry *e = live(h);
    if (e) e->attached = value;
}

uint64_t mn_gh_attached(mn_handle h) {
    struct gentry *e = live(h);
    return e ? e->attached : 0;
}

int mn_gh_retain(mn_handle h) {
    struct gentry *e = live(h);
    if (!e || e->pool) return -1; /* a pool can't be retained */
    uint32_t r = e->refs;
    do
        if (!r) return -1;
    while (!atomic_compare_exchange_weak(&e->refs, &r, r + 1));
    return 0;
}

int mn_gh_release(mn_handle h) {
    struct gentry *e = live(h);
    if (!e) return -1;
    if (e->pool && e->refs == 1) mn_gh_pool_pop(h);
    uint32_t r = e->refs;
    do
        if (!r) return -1;
    while (!atomic_compare_exchange_weak(&e->refs, &r, r - 1));
    if (r > 1) return 0;
    free_index(e, MN_H_INDEX(h));
    return 1;
}

void mn_gh_drop(mn_handle h) {
    struct gentry *e = live(h);
    if (!e) return;
    for (size_t k = npools; k-- > 0;)
        for (size_t j = pools[k].n; j-- > 0;)
            if (pools[k].items[j] == h) {
                pools[k].items[j] = pools[k].items[--pools[k].n];
                goto dropped;
            }
dropped:
    e->refs = 0;
    free_index(e, MN_H_INDEX(h));
}

void mn_gh_pool_push(mn_handle pool) {
    struct gentry *e = live(pool);
    if (!e) return;
    if (npools == cap_pools) {
        struct gpool *p = realloc(pools, (cap_pools ? cap_pools * 2 : 8) * sizeof *p);
        if (!p) return;
        pools = p, cap_pools = cap_pools ? cap_pools * 2 : 8;
    }
    e->pool = 1;
    pools[npools++] = (struct gpool){.h = pool};
}

void mn_gh_pool_pop(mn_handle pool) {
    size_t k = npools;
    while (k > 0 && pools[k - 1].h != pool) k--;
    if (!k) return; /* not one of this thread's */
    while (npools >= k) {
        struct gpool p = pools[--npools];
        for (size_t j = 0; j < p.n; j++) mn_gh_release(p.items[j]);
        free(p.items);
        if (p.h != pool) { /* an inner pool goes with the outer one */
            struct gentry *e = live(p.h);
            if (e) {
                e->refs = 0;
                free_index(e, MN_H_INDEX(p.h));
            }
        }
    }
}
