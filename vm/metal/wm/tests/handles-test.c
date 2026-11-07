/* Milestone 4b-1, Task 2: the guest's handle allocator and the host's table, with fake objects (slots of objs[], whose
 * releases are counted) and fake rings (positions the test moves). Mac, ASan. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../handles.h"

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __func__, __LINE__, #c);                                             \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static int objs[4096];
static _Atomic int released[4096];
static void *obj(int i) { return &objs[i]; }
static void on_release(enum mn_hkind kind, void *o) {
    (void)kind;
    released[(int *)o - objs]++;
}
static void reset(void) {
    for (int i = 0; i < 4096; i++) released[i] = 0;
}

/* Fake rings: what each has published, and what it has finished. */
struct fake { uint64_t published[4], finished[4]; int n; };
static struct fake rings;
static size_t snapshot(void *ctx, void *except, struct mn_mark *m, size_t cap) {
    struct fake *f = ctx;
    size_t n = 0;
    for (int i = 0; i < f->n && n < cap; i++)
        if ((void *)&f->published[i] != except) m[n++] = (struct mn_mark){&f->published[i], f->published[i], 0};
    return n;
}
static int passed(void *ctx, const struct mn_mark *m, size_t n) {
    struct fake *f = ctx;
    for (size_t k = 0; k < n; k++) {
        int i = (int)((uint64_t *)m[k].ring - f->published);
        if (f->finished[i] < m[k].pos) return 0;
    }
    return 1;
}
static void *ring(int i) { return &rings.published[i]; }
static struct mn_ht_rings fake_rings = {&rings, snapshot, passed};

static struct mn_ht *table(void) {
    memset(&rings, 0, sizeof rings);
    rings.n = 2;
    reset();
    return mn_ht_new(&fake_rings, on_release);
}
static mn_handle H(uint32_t gen, uint32_t index) { return (uint64_t)gen << 32 | index; }

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void bind_get(void) {
    struct mn_ht *t = table();
    int ok;
    CHECK(mn_ht_bind(t, ring(0), H(1, 1), MN_HK_OBJC, obj(1), MN_OWN_PLUS1) == 0);
    CHECK(mn_ht_get(t, H(1, 1), MN_HK_OBJC, &ok) == obj(1) && ok);
    CHECK(mn_ht_get(t, H(1, 1), MN_HK_ANY, &ok) == obj(1) && ok);
    CHECK(mn_ht_live(t) == 1);
    mn_ht_free(t);
    CHECK(released[1] == 1);
}

static void stale(void) {
    struct mn_ht *t = table();
    int ok;
    mn_ht_bind(t, ring(0), H(1, 5), MN_HK_OBJC, obj(5), MN_OWN_PLUS1);
    CHECK(mn_ht_release(t, ring(0), H(1, 5)) == 0);
    mn_ht_sweep(t, NULL); /* the rings are idle: both phases pass */
    CHECK(released[5] == 1);
    mn_ht_get(t, H(1, 5), MN_HK_OBJC, &ok);
    CHECK(!ok);
    CHECK(mn_ht_release(t, ring(0), H(1, 5)) == -1);
    CHECK(mn_ht_bind(t, ring(0), H(1, 5), MN_HK_OBJC, obj(6), MN_OWN_PLUS1) == -1); /* the old generation again */
    CHECK(mn_ht_bind(t, ring(0), H(2, 5), MN_HK_OBJC, obj(6), MN_OWN_PLUS1) == 0);
    CHECK(mn_ht_get(t, H(2, 5), MN_HK_OBJC, &ok) == obj(6) && ok);
    mn_ht_free(t);
}

static void unbound(void) {
    struct mn_ht *t = table();
    int ok = 1;
    mn_ht_get(t, 0, MN_HK_OBJC, &ok);
    CHECK(!ok);
    mn_ht_bind(t, ring(0), H(3, 7), MN_HK_OBJC, obj(7), MN_OWN_PLUS1);
    double t0 = now_ms();
    mn_ht_get(t, H(2, 7), MN_HK_OBJC, &ok); /* below the binding's generation: stale at once */
    CHECK(!ok && now_ms() - t0 < 100);
    CHECK(mn_ht_bind(t, ring(0), H(3, 7), MN_HK_OBJC, obj(8), MN_OWN_PLUS1) == -1); /* bound at that generation */
    mn_ht_free(t);
}

static void out_of_range(void) {
    struct mn_ht *t = table();
    int ok = 1;
    double t0 = now_ms();
    mn_ht_get(t, H(1, MN_H_MAX), MN_HK_OBJC, &ok);
    CHECK(!ok && now_ms() - t0 < 100);
    CHECK(mn_ht_bind(t, ring(0), H(1, MN_H_MAX), MN_HK_OBJC, obj(1), MN_OWN_PLUS1) == -1);
    CHECK(mn_ht_bind(t, ring(0), H(1, 0), MN_HK_OBJC, obj(1), MN_OWN_PLUS1) == -1);
    mn_ht_free(t);
}

static void kind_mismatch(void) {
    struct mn_ht *t = table();
    int ok = 1;
    mn_ht_bind(t, ring(0), H(1, 2), MN_HK_SM50_SHADER, obj(2), MN_OWN_PLUS1);
    CHECK(mn_ht_get(t, H(1, 2), MN_HK_OBJC, &ok) == NULL && !ok);
    CHECK(mn_ht_get(t, H(1, 2), MN_HK_SM50_SHADER, &ok) == obj(2) && ok);
    mn_ht_free(t);
    CHECK(released[2] == 1);
}

static void refs(void) {
    struct mn_ht *t = table();
    mn_ht_bind(t, ring(0), H(1, 3), MN_HK_OBJC, obj(3), MN_OWN_PLUS1);
    CHECK(mn_ht_retain(t, H(1, 3)) == 0);
    mn_ht_release(t, ring(0), H(1, 3));
    mn_ht_sweep(t, NULL);
    CHECK(released[3] == 0);
    mn_ht_release(t, ring(0), H(1, 3));
    mn_ht_sweep(t, NULL);
    CHECK(released[3] == 1);
    CHECK(mn_ht_live(t) == 0);
    mn_ht_free(t);
    CHECK(released[3] == 1);
}

static void pool(void) {
    struct mn_ht *t = table();
    int ok;
    mn_ht_bind(t, ring(0), H(1, 10), MN_HK_POOL, obj(10), MN_OWN_PLUS1);
    mn_ht_pool_push(t, ring(0), H(1, 10));
    mn_ht_bind(t, ring(0), H(1, 11), MN_HK_OBJC, obj(11), MN_OWN_AUTORELEASED);
    mn_ht_bind(t, ring(0), H(1, 12), MN_HK_OBJC, obj(12), MN_OWN_AUTORELEASED);
    mn_ht_retain(t, H(1, 11));
    CHECK(mn_ht_get(t, H(1, 10), MN_HK_OBJC, &ok) == NULL && !ok); /* a pool is no object to pass */
    CHECK(mn_ht_retain(t, H(1, 10)) == -1);
    CHECK(mn_ht_release(t, ring(1), H(1, 10)) == -1); /* another ring's pool */
    CHECK(mn_ht_release(t, ring(0), H(1, 10)) == 0);  /* pops it */
    CHECK(released[10] == 1);                         /* at once, on its own ring */
    mn_ht_sweep(t, NULL);
    CHECK(released[12] == 1 && released[11] == 0);
    CHECK(mn_ht_get(t, H(1, 11), MN_HK_OBJC, &ok) == obj(11) && ok);
    mn_ht_get(t, H(1, 12), MN_HK_OBJC, &ok);
    CHECK(!ok);
    CHECK(mn_ht_live(t) == 1);
    mn_ht_free(t);
    CHECK(released[11] == 1);

    /* The guest's mirror. */
    mn_handle p = mn_gh_alloc(MN_OWN_PLUS1);
    mn_gh_pool_push(p);
    mn_handle a = mn_gh_alloc(MN_OWN_AUTORELEASED), b = mn_gh_alloc(MN_OWN_AUTORELEASED);
    CHECK(a && b && a != b);
    mn_gh_retain(a);
    CHECK(mn_gh_release(p) == 1);
    CHECK(mn_gh_retain(b) == -1); /* freed with the pool */
    CHECK(mn_gh_release(a) == 1);
}

static void nested_pools(void) {
    struct mn_ht *t = table();
    mn_ht_bind(t, ring(0), H(1, 20), MN_HK_POOL, obj(20), MN_OWN_PLUS1);
    mn_ht_pool_push(t, ring(0), H(1, 20));
    mn_ht_bind(t, ring(0), H(1, 21), MN_HK_OBJC, obj(21), MN_OWN_AUTORELEASED);
    mn_ht_bind(t, ring(0), H(1, 22), MN_HK_POOL, obj(22), MN_OWN_PLUS1);
    mn_ht_pool_push(t, ring(0), H(1, 22));
    mn_ht_bind(t, ring(0), H(1, 23), MN_HK_OBJC, obj(23), MN_OWN_AUTORELEASED);
    /* The outer pool takes the inner one with it: the host's pools are stand-ins (invoke.m), each released once. */
    mn_ht_release(t, ring(0), H(1, 20));
    mn_ht_sweep(t, NULL);
    CHECK(released[20] == 1 && released[21] == 1 && released[22] == 1 && released[23] == 1);
    CHECK(mn_ht_live(t) == 0);
    mn_ht_free(t);
    CHECK(released[22] == 1);
    mn_handle p1 = mn_gh_alloc(MN_OWN_PLUS1);
    mn_gh_pool_push(p1);
    mn_handle p2 = mn_gh_alloc(MN_OWN_PLUS1);
    mn_gh_pool_push(p2);
    mn_handle a = mn_gh_alloc(MN_OWN_AUTORELEASED);
    CHECK(mn_gh_release(p1) == 1);
    CHECK(mn_gh_retain(p2) == -1 && mn_gh_retain(a) == -1);
}

static void borrowed(void) {
    struct mn_ht *t = table();
    int ok;
    mn_ht_bind(t, ring(0), H(1, 30), MN_HK_POOL, obj(30), MN_OWN_PLUS1);
    mn_ht_pool_push(t, ring(0), H(1, 30));
    mn_ht_bind(t, ring(0), H(1, 31), MN_HK_OBJC, obj(31), MN_OWN_BORROWED);
    mn_ht_release(t, ring(0), H(1, 30));
    mn_ht_sweep(t, NULL);
    CHECK(released[31] == 0);
    CHECK(mn_ht_get(t, H(1, 31), MN_HK_OBJC, &ok) == obj(31) && ok);
    mn_ht_free(t);
    CHECK(released[31] == 1);
}

struct late { struct mn_ht *t; mn_handle h; int i; };
static void *bind_late(void *p) {
    struct late *l = p;
    usleep(50000);
    mn_ht_bind(l->t, ring(0), l->h, MN_HK_OBJC, obj(l->i), MN_OWN_PLUS1);
    return NULL;
}

static void cross_ring_use(void) {
    struct mn_ht *t = table();
    int ok;
    struct late l = {t, H(1, 40), 40};
    pthread_t th;
    pthread_create(&th, NULL, bind_late, &l);
    double t0 = now_ms();
    CHECK(mn_ht_get(t, H(1, 40), MN_HK_OBJC, &ok) == obj(40) && ok); /* ring 1 waits for ring 0's creation */
    CHECK(now_ms() - t0 >= 40);
    pthread_join(th, NULL);
    /* The same index rebound later: a use of the next generation waits for it too. */
    mn_ht_release(t, ring(0), H(1, 40));
    mn_ht_sweep(t, NULL);
    l = (struct late){t, H(2, 40), 41};
    pthread_create(&th, NULL, bind_late, &l);
    CHECK(mn_ht_get(t, H(2, 40), MN_HK_OBJC, &ok) == obj(41) && ok);
    pthread_join(th, NULL);
    /* Never bound: refused once the wait runs out. */
    t0 = now_ms();
    mn_ht_get(t, H(1, 42), MN_HK_OBJC, &ok);
    CHECK(!ok && now_ms() - t0 >= MN_H_WAIT_MS - 100);
    mn_ht_free(t);
}

static void cross_ring_release(void) {
    struct mn_ht *t = table();
    int ok;
    mn_ht_bind(t, ring(0), H(1, 50), MN_HK_OBJC, obj(50), MN_OWN_PLUS1);
    rings.published[1] = 10, rings.finished[1] = 5; /* ring 1 still has a use of it queued */
    mn_ht_release(t, ring(0), H(1, 50));
    mn_ht_sweep(t, NULL);
    CHECK(released[50] == 0);
    CHECK(mn_ht_get(t, H(1, 50), MN_HK_OBJC, &ok) == obj(50) && ok);
    CHECK(mn_ht_deferred(t) == 1);
    rings.finished[1] = 10;
    mn_ht_sweep(t, NULL);
    CHECK(released[50] == 1);
    CHECK(mn_ht_deferred(t) == 0 && mn_ht_live(t) == 0);
    /* A retain from the other ring that ran after the release keeps it. */
    mn_ht_bind(t, ring(0), H(1, 51), MN_HK_OBJC, obj(51), MN_OWN_PLUS1);
    rings.published[1] = 20;
    mn_ht_release(t, ring(0), H(1, 51));
    CHECK(mn_ht_retain(t, H(1, 51)) == 0);
    rings.finished[1] = 20;
    mn_ht_sweep(t, NULL);
    CHECK(released[51] == 0);
    CHECK(mn_ht_get(t, H(1, 51), MN_HK_OBJC, &ok) == obj(51) && ok);
    mn_ht_free(t);
    CHECK(released[51] == 1);
}

static void reuse_fifo(void) {
    mn_handle first = mn_gh_alloc(MN_OWN_PLUS1);
    uint32_t index = MN_H_INDEX(first);
    CHECK(mn_gh_release(first) == 1);
    int reused = 0;
    for (uint32_t i = 0; i < MN_H_QUARANTINE; i++) { /* the next frees, one at a time */
        mn_handle h = mn_gh_alloc(MN_OWN_PLUS1);
        reused |= MN_H_INDEX(h) == index;
        mn_gh_release(h);
    }
    CHECK(!reused);
    mn_handle h = mn_gh_alloc(MN_OWN_PLUS1); /* MN_H_QUARANTINE frees later: its turn */
    CHECK(MN_H_INDEX(h) == index && MN_H_GEN(h) == MN_H_GEN(first) + 1);
    mn_gh_release(h);
    mn_handle d = mn_gh_alloc(MN_OWN_PLUS1);
    mn_gh_drop(d);
    CHECK(mn_gh_retain(d) == -1);
}

struct worker { struct mn_ht *t; int r; };
static void *work(void *p) {
    struct worker *w = p;
    for (int round = 0; round < 100; round++) {
        uint32_t base = 1000 + (uint32_t)w->r * 1000, gen = (uint32_t)round + 1;
        mn_ht_bind(w->t, ring(w->r), H(gen, base), MN_HK_POOL, obj(base), MN_OWN_PLUS1);
        mn_ht_pool_push(w->t, ring(w->r), H(gen, base));
        for (uint32_t i = 1; i < 200; i++)
            mn_ht_bind(w->t, ring(w->r), H(gen, base + i), MN_HK_OBJC, obj((int)(base + i)), MN_OWN_AUTORELEASED);
        mn_ht_release(w->t, ring(w->r), H(gen, base));
        mn_ht_sweep(w->t, ring(w->r));
        /* the guest's side too, one table per process */
        mn_handle p2 = mn_gh_alloc(MN_OWN_PLUS1);
        mn_gh_pool_push(p2);
        for (int i = 0; i < 50; i++) mn_gh_alloc(MN_OWN_AUTORELEASED);
        mn_gh_release(p2);
    }
    return NULL;
}

static void threads(void) {
    struct mn_ht *t = table();
    struct worker w[2] = {{t, 0}, {t, 1}};
    pthread_t th[2];
    for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, work, &w[i]);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    mn_ht_sweep(t, NULL);
    CHECK(mn_ht_live(t) == 0 && mn_ht_deferred(t) == 0);
    int all = 1;
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 200; i++) all &= released[1000 + r * 1000 + i] == 100;
    CHECK(all);
    mn_ht_free(t);
}

/* A ring whose thread ended with a pool open: the pool goes with it (its stand-in released), its handles lose that
 * reference, and a later ring at the same address starts with no pools. */
static void ring_end(void) {
    struct mn_ht *t = table();
    int ok;
    mn_ht_bind(t, ring(0), H(1, 70), MN_HK_POOL, obj(70), MN_OWN_PLUS1);
    mn_ht_pool_push(t, ring(0), H(1, 70));
    mn_ht_bind(t, ring(0), H(1, 71), MN_HK_OBJC, obj(71), MN_OWN_AUTORELEASED);
    mn_ht_ring_end(t, ring(0));
    mn_ht_sweep(t, NULL);
    CHECK(released[71] == 1 && released[70] == 1);
    mn_ht_get(t, H(1, 70), MN_HK_POOL, &ok);
    CHECK(!ok);
    mn_ht_bind(t, ring(0), H(1, 72), MN_HK_OBJC, obj(72), MN_OWN_AUTORELEASED); /* the next ring there: no pool */
    CHECK(mn_ht_live(t) == 1);
    mn_ht_free(t);
    CHECK(released[72] == 1);
}

/* The second phase waits for lookups in flight on other rings; the ring that sweeps has none (it is sweeping), even
 * while it has records queued after the one it is running. */
static void sweep_phase2(void) {
    struct mn_ht *t = table();
    mn_ht_bind(t, ring(0), H(1, 80), MN_HK_OBJC, obj(80), MN_OWN_PLUS1);
    mn_ht_release(t, ring(0), H(1, 80)); /* ring 1 had published nothing */
    rings.published[1] = 10, rings.finished[1] = 5;
    mn_ht_sweep(t, ring(1));
    CHECK(released[80] == 1);
    mn_ht_free(t);
}

static void free_all(void) {
    struct mn_ht *t = table();
    mn_ht_bind(t, ring(0), H(1, 60), MN_HK_OBJC, obj(60), MN_OWN_PLUS1);
    mn_ht_bind(t, ring(0), H(1, 61), MN_HK_OBJC, obj(61), MN_OWN_BORROWED);
    mn_ht_bind(t, ring(0), H(1, 62), MN_HK_OBJC, obj(62), MN_OWN_PLUS1);
    rings.published[1] = 10;
    mn_ht_release(t, ring(0), H(1, 62)); /* deferred: ring 1 hasn't finished */
    mn_ht_free(t);
    CHECK(released[60] == 1 && released[61] == 1 && released[62] == 1);
}

/* M4 Task 9: a context's end releases what's left newest first, as a program tears down (a device made first goes last:
 * native DXMT's D3D11 children use their device's pools without holding it), by when each was bound, not its index
 * (a freed index is the guest's again). */
static int order[8], norder;
static void on_release_order(enum mn_hkind kind, void *o) {
    (void)kind;
    if (norder < 8) order[norder++] = (int)((int *)o - objs);
}
static void free_newest_first(void) {
    memset(&rings, 0, sizeof rings);
    rings.n = 2;
    norder = 0;
    struct mn_ht *t = mn_ht_new(&fake_rings, on_release_order);
    mn_ht_bind(t, ring(0), H(1, 7), MN_HK_OBJC, obj(3), MN_OWN_PLUS1); /* the device */
    mn_ht_bind(t, ring(0), H(1, 4), MN_HK_OBJC, obj(4), MN_OWN_PLUS1); /* its children: a lower index, after it */
    mn_ht_bind(t, ring(0), H(1, 5000), MN_HK_OBJC, obj(5), MN_OWN_PLUS1); /* in the second chunk */
    mn_ht_free(t);
    CHECK(norder == 3 && order[0] == 5 && order[1] == 4 && order[2] == 3);
}

int main(void) {
    struct { const char *name; void (*fn)(void); } tests[] = {
        {"bind-get", bind_get}, {"stale", stale}, {"unbound", unbound}, {"out-of-range", out_of_range},
        {"kind-mismatch", kind_mismatch}, {"refs", refs}, {"pool", pool}, {"nested-pools", nested_pools},
        {"borrowed", borrowed}, {"cross-ring-use", cross_ring_use}, {"cross-ring-release", cross_ring_release},
        {"reuse-fifo", reuse_fifo}, {"threads", threads}, {"ring-end", ring_end}, {"sweep-phase2", sweep_phase2},
        {"free-all", free_all}, {"free-newest-first", free_newest_first},
    };
    for (size_t i = 0; i < sizeof tests / sizeof *tests; i++) {
        int before = failures;
        tests[i].fn();
        printf("%s %s\n", failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("handles-test: %s\n", failures ? "FAIL" : "all passed");
    return failures != 0;
}
