/* The host's winemetal handle table (handles.h), one per context. Lookups are lock-free on the hot path (a state word
 * of generation, kind and bound bit, checked again after reading the object); everything else holds the context's
 * lock. A last release goes through two phases, each waiting until the other rings have finished what they had
 * published: the first so that uses queued before the release still find the object, the second so that no lookup
 * still in flight when it was unbound uses it after the object is released.
 * ponytail: one lock per context; per-index locks if contention shows. */
#include "../handles.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define CHUNK 4096u
#define BOUND 1ull
#define STATE(gen, kind, bound) ((uint64_t)(gen) << 32 | (uint64_t)(kind) << 8 | (bound))
#define S_GEN(s) ((uint32_t)((s) >> 32))
#define S_KIND(s) ((enum mn_hkind)(((s) >> 8) & 0xff))

struct hentry {
    _Atomic uint64_t state;
    void *_Atomic obj;
    uint32_t refs;          /* the guest's references */
    uint8_t own, busy;      /* busy: bound, or its release is in its second phase */
    void *ring;             /* a pool's ring */
};

struct deferred {
    struct deferred *next;
    uint32_t index, gen;
    int phase;
    size_t n;
    struct mn_mark marks[];
};

struct hpool { mn_handle h; mn_handle *items; size_t n, cap; };
struct ringpools { void *ring; struct hpool *stack; size_t n, cap; };

struct mn_ht {
    struct mn_ht_rings rings;
    void (*release)(enum mn_hkind, void *);
    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct hentry *_Atomic chunks[MN_H_MAX / CHUNK];
    struct deferred *head, **tail;
    _Atomic size_t ndeferred;
    size_t live;
    struct ringpools *rp;
    size_t nrp, caprp;
};

/* Releases collected under the lock, run after it (a dealloc may do anything). */
struct pending { enum mn_hkind kind; void *obj; };
struct pendings { struct pending *v; size_t n, cap; };

static void pend(struct pendings *p, enum mn_hkind kind, void *obj) {
    if (!obj) return; /* a queued creation that failed */
    if (p->n == p->cap) {
        struct pending *v = realloc(p->v, (p->cap ? p->cap * 2 : 16) * sizeof *v);
        if (!v) return; /* ponytail: leaks the object rather than failing a release */
        p->v = v, p->cap = p->cap ? p->cap * 2 : 16;
    }
    p->v[p->n++] = (struct pending){kind, obj};
}

static void run_pending(struct mn_ht *t, struct pendings *p) {
    for (size_t i = 0; i < p->n; i++) t->release(p->v[i].kind, p->v[i].obj);
    free(p->v);
}

struct mn_ht *mn_ht_new(const struct mn_ht_rings *rings, void (*release)(enum mn_hkind kind, void *obj)) {
    struct mn_ht *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->rings = *rings;
    t->release = release;
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);
    t->tail = &t->head;
    return t;
}

static struct hentry *entry(struct mn_ht *t, uint32_t i, int create) {
    if (!i || i >= MN_H_MAX) return NULL;
    struct hentry *c = t->chunks[i / CHUNK];
    if (!c && create) { /* under the lock */
        c = calloc(CHUNK, sizeof *c);
        if (!c) return NULL;
        t->chunks[i / CHUNK] = c;
    }
    return c ? &c[i % CHUNK] : NULL;
}

static int kind_ok(enum mn_hkind have, enum mn_hkind want) {
    return want == MN_HK_ANY ? have != MN_HK_POOL : have == want;
}

static struct timespec deadline(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec d = {now.tv_sec + MN_H_WAIT_MS / 1000, (long)now.tv_usec * 1000 + (MN_H_WAIT_MS % 1000) * 1000000L};
    if (d.tv_nsec >= 1000000000L) d.tv_sec++, d.tv_nsec -= 1000000000L;
    return d;
}

/* With the lock: the entry h is bound to, waiting while its binding may still be queued on another ring (a generation
 * above the index's last); NULL for a null, out-of-range, stale or other-kind handle, or once the wait runs out. */
static struct hentry *resolve(struct mn_ht *t, mn_handle h, enum mn_hkind kind) {
    uint32_t i = MN_H_INDEX(h), g = MN_H_GEN(h);
    if (!i || i >= MN_H_MAX || !g) return NULL;
    struct timespec end = deadline();
    for (;;) {
        struct hentry *e = entry(t, i, 0);
        uint64_t s = e ? e->state : 0;
        if (S_GEN(s) == g && (s & BOUND)) return kind_ok(S_KIND(s), kind) ? e : NULL;
        if (S_GEN(s) >= g) return NULL; /* released, or older than the index's binding */
        if (pthread_cond_timedwait(&t->cond, &t->lock, &end) == ETIMEDOUT) return NULL;
    }
}

void *mn_ht_get(struct mn_ht *t, mn_handle h, enum mn_hkind kind, int *ok) {
    uint32_t i = MN_H_INDEX(h);
    struct hentry *c = i && i < MN_H_MAX ? t->chunks[i / CHUNK] : NULL;
    if (c) {
        struct hentry *e = &c[i % CHUNK];
        uint64_t s = atomic_load_explicit(&e->state, memory_order_acquire);
        if ((s & BOUND) && S_GEN(s) == MN_H_GEN(h) && kind_ok(S_KIND(s), kind)) {
            void *o = atomic_load_explicit(&e->obj, memory_order_acquire);
            if (atomic_load_explicit(&e->state, memory_order_acquire) == s) {
                *ok = 1;
                return o;
            }
        }
    }
    pthread_mutex_lock(&t->lock);
    struct hentry *e = resolve(t, h, kind);
    void *o = e ? e->obj : NULL;
    pthread_mutex_unlock(&t->lock);
    *ok = e != NULL;
    return o;
}

static struct ringpools *ringpools(struct mn_ht *t, void *ring, int create) {
    for (size_t i = 0; i < t->nrp; i++)
        if (t->rp[i].ring == ring) return &t->rp[i];
    if (!create) return NULL;
    if (t->nrp == t->caprp) {
        struct ringpools *rp = realloc(t->rp, (t->caprp ? t->caprp * 2 : 8) * sizeof *rp);
        if (!rp) return NULL;
        t->rp = rp, t->caprp = t->caprp ? t->caprp * 2 : 8;
    }
    t->rp[t->nrp] = (struct ringpools){.ring = ring};
    return &t->rp[t->nrp++];
}

static void push_item(struct hpool *p, mn_handle h) {
    if (p->n == p->cap) {
        mn_handle *items = realloc(p->items, (p->cap ? p->cap * 2 : 16) * sizeof *items);
        if (!items) return; /* ponytail: it then lives until the context ends, as with no pool open */
        p->items = items, p->cap = p->cap ? p->cap * 2 : 16;
    }
    p->items[p->n++] = h;
}

int mn_ht_bind(struct mn_ht *t, void *ring, mn_handle h, enum mn_hkind kind, void *obj, enum mn_own own) {
    uint32_t i = MN_H_INDEX(h), g = MN_H_GEN(h);
    if (!i || i >= MN_H_MAX || !g || kind == MN_HK_ANY) return -1;
    pthread_mutex_lock(&t->lock);
    struct hentry *e = entry(t, i, 1);
    struct timespec end = deadline();
    /* The index's last release may still be on its way through the rings (the guest's FIFO makes it rare). */
    while (e && e->busy && S_GEN(e->state) < g)
        if (pthread_cond_timedwait(&t->cond, &t->lock, &end) == ETIMEDOUT) break;
    if (!e || e->busy || g <= S_GEN(e->state)) {
        pthread_mutex_unlock(&t->lock);
        return -1;
    }
    e->obj = obj;
    e->refs = 1;
    e->own = (uint8_t)own;
    e->busy = 1;
    e->ring = ring;
    atomic_store_explicit(&e->state, STATE(g, kind, BOUND), memory_order_release);
    t->live++;
    if (own == MN_OWN_AUTORELEASED) {
        struct ringpools *rp = ringpools(t, ring, 0);
        if (rp && rp->n) push_item(&rp->stack[rp->n - 1], h);
    }
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
    return 0;
}

int mn_ht_retain(struct mn_ht *t, mn_handle h) {
    pthread_mutex_lock(&t->lock);
    struct hentry *e = resolve(t, h, MN_HK_ANY); /* a pool can't be retained */
    if (e) e->refs++;
    pthread_mutex_unlock(&t->lock);
    return e ? 0 : -1;
}

static void defer(struct mn_ht *t, uint32_t index, uint32_t gen, int phase, void *except) {
    struct mn_mark marks[MN_MAX_RINGS];
    size_t n = t->rings.snapshot(t->rings.ctx, except, marks, MN_MAX_RINGS);
    struct deferred *d = malloc(sizeof *d + n * sizeof *marks);
    if (!d) return; /* ponytail: the object then lives until the context ends */
    *d = (struct deferred){.index = index, .gen = gen, .phase = phase, .n = n};
    memcpy(d->marks, marks, n * sizeof *marks);
    *t->tail = d;
    t->tail = &d->next;
    t->ndeferred++;
}

/* With the lock: one reference less; the last one starts the release. */
static void unref(struct mn_ht *t, struct hentry *e, mn_handle h, void *ring) {
    if (--e->refs) return;
    defer(t, MN_H_INDEX(h), MN_H_GEN(h), 1, ring);
}

static void unbind(struct mn_ht *t, struct hentry *e) {
    atomic_store_explicit(&e->state, e->state & ~BOUND, memory_order_release);
    t->live--;
}

/* With the lock: pops pool and the pools inside it, dropping a reference from each of their handles. Inner pools are
 * unbound only: draining the outer NSAutoreleasePool releases them. */
static int pop(struct mn_ht *t, void *ring, mn_handle pool) {
    struct ringpools *rp = ringpools(t, ring, 0);
    size_t k = rp ? rp->n : 0;
    while (k > 0 && rp->stack[k - 1].h != pool) k--;
    if (!k) return -1;
    while (rp->n >= k) {
        struct hpool p = rp->stack[--rp->n];
        for (size_t j = 0; j < p.n; j++) {
            struct hentry *e = entry(t, MN_H_INDEX(p.items[j]), 0);
            if (e && (e->state & BOUND) && S_GEN(e->state) == MN_H_GEN(p.items[j]) && e->refs)
                unref(t, e, p.items[j], ring);
        }
        free(p.items);
        if (p.h != pool) {
            struct hentry *e = entry(t, MN_H_INDEX(p.h), 0);
            unbind(t, e);
            e->busy = 0;
            e->obj = NULL;
        }
    }
    return 0;
}

void mn_ht_pool_push(struct mn_ht *t, void *ring, mn_handle pool) {
    pthread_mutex_lock(&t->lock);
    struct hentry *e = resolve(t, pool, MN_HK_POOL);
    struct ringpools *rp = e && e->ring == ring ? ringpools(t, ring, 1) : NULL;
    if (rp && rp->n == rp->cap) {
        struct hpool *s = realloc(rp->stack, (rp->cap ? rp->cap * 2 : 8) * sizeof *s);
        if (s) rp->stack = s, rp->cap = rp->cap ? rp->cap * 2 : 8;
    }
    if (rp && rp->n < rp->cap) rp->stack[rp->n++] = (struct hpool){.h = pool};
    pthread_mutex_unlock(&t->lock);
}

void mn_ht_pool_pop(struct mn_ht *t, void *ring, mn_handle pool) {
    pthread_mutex_lock(&t->lock);
    pop(t, ring, pool);
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
}

int mn_ht_release(struct mn_ht *t, void *ring, mn_handle h) {
    struct pendings run = {0};
    pthread_mutex_lock(&t->lock);
    struct hentry *e = entry(t, MN_H_INDEX(h), 0);
    int pool = e && S_KIND(e->state) == MN_HK_POOL;
    e = resolve(t, h, pool ? MN_HK_POOL : MN_HK_ANY);
    int r = -1;
    if (e && e->refs && (!pool || e->ring == ring)) {
        r = 0;
        if (pool) { /* at once, on its own ring's thread: an NSAutoreleasePool is drained where it was made */
            pop(t, ring, h);
            e->refs = 0;
            unbind(t, e);
            e->busy = 0;
            pend(&run, MN_HK_POOL, e->obj);
            e->obj = NULL;
            pthread_cond_broadcast(&t->cond);
        } else {
            unref(t, e, h, ring);
        }
    }
    pthread_mutex_unlock(&t->lock);
    run_pending(t, &run);
    return r;
}

void mn_ht_sweep(struct mn_ht *t) {
    if (!t->ndeferred) return;
    struct pendings run = {0};
    pthread_mutex_lock(&t->lock);
    struct deferred *d;
    while ((d = t->head) && t->rings.passed(t->rings.ctx, d->marks, d->n)) {
        t->head = d->next;
        if (!t->head) t->tail = &t->head;
        t->ndeferred--;
        struct hentry *e = entry(t, d->index, 0);
        if (d->phase == 1 && !e->refs && S_GEN(e->state) == d->gen) {
            unbind(t, e); /* no new use can find it now; one still in flight is waited out next */
            defer(t, d->index, d->gen, 2, NULL);
        } else if (d->phase == 2) {
            pend(&run, S_KIND(e->state), e->obj);
            e->obj = NULL;
            e->busy = 0;
            pthread_cond_broadcast(&t->cond);
        } /* else retained again by a ring that ran after the release: it lives on */
        free(d);
    }
    pthread_mutex_unlock(&t->lock);
    run_pending(t, &run);
}

void mn_ht_free(struct mn_ht *t) {
    struct pendings run = {0};
    for (struct deferred *d = t->head, *next; d; d = next) {
        next = d->next;
        free(d);
    }
    for (uint32_t c = 0; c < MN_H_MAX / CHUNK; c++) {
        struct hentry *chunk = t->chunks[c];
        if (!chunk) continue;
        for (uint32_t i = 0; i < CHUNK; i++)
            /* A pool died with its ring's thread, which drained it. */
            if (chunk[i].busy && S_KIND(chunk[i].state) != MN_HK_POOL) pend(&run, S_KIND(chunk[i].state), chunk[i].obj);
        free(chunk);
    }
    for (size_t i = 0; i < t->nrp; i++) {
        for (size_t k = 0; k < t->rp[i].n; k++) free(t->rp[i].stack[k].items);
        free(t->rp[i].stack);
    }
    free(t->rp);
    run_pending(t, &run);
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}

size_t mn_ht_live(struct mn_ht *t) {
    pthread_mutex_lock(&t->lock);
    size_t n = t->live;
    pthread_mutex_unlock(&t->lock);
    return n;
}

size_t mn_ht_deferred(struct mn_ht *t) { return t->ndeferred; }
