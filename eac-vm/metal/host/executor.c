/* The executor (see executor.h). libkrun's gpu worker thread delivers control messages, fences and blob events, one at
 * a time; only it changes the ring list, under rings_lock for the consumers' snapshots. A fence follows the message it belongs to at once, so an MN_CTRL_WAIT
 * is paired with the next fence; any other fence completes immediately.
 *
 * A drained ring (Direct3D forwarding) has no consumer: the vCPU that traps runs it (mn_executor_trap), under the
 * ring's drain lock. A queue operation (MN_REC_SUBMIT) also runs the context's other drained rings, under submit_lock,
 * taken before any drain lock: no thread waits for submit_lock while it holds a drain lock (it lets its own go and
 * takes both again), so the submit holder can always take the others'. */
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include "executor.h"
#include "../ring.h"

/* libkrun's dxmt context (patch 0005, rutabaga_dxmt's MnDxmtOps): not in libkrun.h, which ffier generates. */
struct mn_dxmt_ops {
    void *ctx;
    void (*context_create)(void *ctx, uint32_t ctx_id);
    void (*context_destroy)(void *ctx, uint32_t ctx_id);
    void (*blob_created)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*blob_destroyed)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*submit)(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len);
    void (*fence)(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);
};
void mn_dxmt_set_ops(const struct mn_dxmt_ops *ops);
void mn_dxmt_fence_done(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);

/* Spec §5.6: a consumer spins after its last record (MN_EXECUTOR_SPIN_DEFAULT_NS; MN_EXECUTOR_SPIN_US), then parks. */
static uint64_t spin_ns = MN_EXECUTOR_SPIN_DEFAULT_NS;

uint64_t mn_executor_spin_ns(const char *value) {
    if (!value) return MN_EXECUTOR_SPIN_DEFAULT_NS;
    char *end;
    long us = strtol(value, &end, 10);
    if (*value && !*end && us >= 0 && us <= 2000) return (uint64_t)us * 1000;
    fprintf(stderr, "mn-executor: MN_EXECUTOR_SPIN_US=%s isn't 0 to 2000, %llu us instead\n", value,
            MN_EXECUTOR_SPIN_DEFAULT_NS / 1000);
    return MN_EXECUTOR_SPIN_DEFAULT_NS;
}
#define MAX_WAITERS 64

/* A blob's memory is ours once the guest frees it (patch 0005): the table holds one reference, each hold one more,
 * and the last release unmaps it. */
struct blob { uint32_t ctx_id; uint64_t blob_id; void *ptr; uint64_t size; atomic_int refs; };
/* ponytail: a linear scan; a hash map when milestone 4b's thousands of buffers make lookups show up */
static struct blob **blobs;
static size_t nblobs, cap_blobs;
static pthread_mutex_t blob_lock = PTHREAD_MUTEX_INITIALIZER;

struct waiter { uint64_t seq, fence_id; uint32_t ring_idx; };
struct ring {
    uint32_t ctx_id;
    int drained, stopped;    /* stopped: under drain, once the ring is unlinked (its blob may go next) */
    pthread_mutex_t drain;   /* a drained ring's: whoever runs its records holds it */
    _Atomic(void *) holder;  /* the thread holding drain */
    atomic_int refs;         /* the ring list's, and each drainer's that found it there */
    int broken_said;
    uint64_t blob_id, tail;
    struct mn_ring_view v;
    pthread_t thread;
    pthread_mutex_t m;
    pthread_cond_t c;
    int wake_pending;
    atomic_int stop;
    atomic_uint nwaiters;
    _Atomic uint64_t finished; /* tail after the last record the handler finished */
    uint64_t serial;
    unsigned since_progress;
    struct waiter waiters[MAX_WAITERS];
    uint64_t state[8];
    struct ring *next;
};
static struct ring *rings;
static pthread_mutex_t rings_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t next_serial;
static mn_op_fn handler;
static struct mn_executor_hooks hooks;
static struct ring *wait_ring; /* the MN_CTRL_WAIT the next fence belongs to */
static struct { int set; uint64_t blob, arg; } notify_wait; /* the MN_CTRL_NOTIFY_WAIT the next fence belongs to */
/* Fences waiting on notify queues; completed by mn_executor_notified (any thread) or the context's end. */
struct nwaiter { uint32_t ctx_id, ring_idx; uint64_t blob, arg, fence_id; };
#define MAX_NWAITERS 256
static struct nwaiter nwaiters[MAX_NWAITERS];
static unsigned nnwaiters;
static pthread_mutex_t nw_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint_fast64_t async_failures; /* since the last context began (MN_OP_TEST_ERRORS) */
static uint64_t wait_seq;

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void error(const char *what, uint64_t a, uint64_t b) {
    fprintf(stderr, "mn-executor: error: %s (%" PRIu64 ", %" PRIu64 ")\n", what, a, b);
}

/* The blob and the address of [offset, offset + len) in it, or NULL; under blob_lock. */
static struct blob *find_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len, void **p) {
    for (size_t i = 0; i < nblobs; i++)
        if (blobs[i]->ctx_id == ctx_id && blobs[i]->blob_id == blob_id) {
            if (offset > blobs[i]->size || len > blobs[i]->size - offset) return NULL;
            *p = (char *)blobs[i]->ptr + offset;
            return blobs[i];
        }
    return NULL;
}

void *mn_executor_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len) {
    void *p = NULL;
    pthread_mutex_lock(&blob_lock);
    find_blob(ctx_id, blob_id, offset, len, &p);
    pthread_mutex_unlock(&blob_lock);
    return p;
}

void *mn_executor_blob_hold(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len, void **hold) {
    void *p = NULL;
    pthread_mutex_lock(&blob_lock);
    struct blob *b = find_blob(ctx_id, blob_id, offset, len, &p);
    if (b) {
        atomic_fetch_add(&b->refs, 1);
        *hold = b;
    }
    pthread_mutex_unlock(&blob_lock);
    return b ? p : NULL;
}

void mn_executor_blob_release(void *hold) {
    struct blob *b = hold;
    if (atomic_fetch_sub(&b->refs, 1) == 1) {
        munmap(b->ptr, b->size);
        free(b);
    }
}

/* Completes the waits the ring's progress satisfies (all of them when it stops). Under r->m. */
static void complete_waiters(struct ring *r, int all) {
    uint64_t done = atomic_load_explicit(&r->v.hdr->done_seq, memory_order_acquire);
    unsigned n = atomic_load(&r->nwaiters);
    for (unsigned i = 0; i < n;) {
        if (all || r->waiters[i].seq <= done) {
            mn_dxmt_fence_done(r->ctx_id, r->waiters[i].ring_idx, r->waiters[i].fence_id);
            r->waiters[i] = r->waiters[--n];
        } else {
            i++;
        }
    }
    atomic_store(&r->nwaiters, n);
}

static void run(struct ring *r, const struct mn_rec *rec) {
    uint32_t op = rec->op & ~(MN_REC_SYNC | MN_REC_SUBMIT), n = 0;
    int sync = !!(rec->op & MN_REC_SYNC);
    int32_t status = 0;
    if (op != MN_OP_NOP && op != MN_OP_BLOB_RELEASE) {
        status = handler ? handler(r->ctx_id, r->state, op, r->v.payload, rec->len, sync ? r->v.reply : NULL,
                                   sync ? MN_REPLY_BYTES : 0, &n)
                         : MN_ENOSYS;
    }
    if (n > MN_REPLY_BYTES) n = MN_REPLY_BYTES;
    if (sync) {
        r->v.hdr->reply_len = n;
        r->v.hdr->reply_status = status;
    } else if (status < 0) {
        atomic_fetch_add(&async_failures, 1);
        error("asynchronous op failed", op, (uint64_t)-status);
    }
    mn_ring_done(&r->v, &r->tail);
    atomic_store_explicit(&r->finished, r->tail, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst); /* done_seq, then nwaiters: pairs with add_waiter */
    if (atomic_load_explicit(&r->nwaiters, memory_order_relaxed)) {
        pthread_mutex_lock(&r->m);
        complete_waiters(r, 0);
        pthread_mutex_unlock(&r->m);
    }
}

static void *consume(void *arg) {
    struct ring *r = arg;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint64_t idle = now_ns();
    for (;;) {
        const struct mn_rec *rec = mn_ring_next(&r->v, &r->tail);
        if (rec) {
            run(r, rec);
            if (hooks.progress && ++r->since_progress == 64) {
                r->since_progress = 0;
                hooks.progress(r->ctx_id, r->state);
            }
            idle = now_ns();
            continue;
        }
        if (atomic_load(&r->stop)) return NULL;
        if (r->v.broken) {
            /* A header or padding that didn't fit what the guest published: stop reading this ring. */
            error("ring broken: a record didn't fit what was published", r->ctx_id, r->blob_id);
            pthread_mutex_lock(&r->m);
            while (!atomic_load(&r->stop)) pthread_cond_wait(&r->c, &r->m);
            pthread_mutex_unlock(&r->m);
            return NULL;
        }
        if (now_ns() - idle < spin_ns) {
            __asm__ volatile("yield");
            continue;
        }
        if (hooks.progress) hooks.progress(r->ctx_id, r->state);
        if (!mn_ring_try_park(&r->v, r->tail)) continue;
        pthread_mutex_lock(&r->m);
        while (!r->wake_pending && !atomic_load(&r->stop)) pthread_cond_wait(&r->c, &r->m);
        r->wake_pending = 0;
        pthread_mutex_unlock(&r->m);
        mn_ring_unpark(&r->v);
        idle = now_ns();
    }
}

static void wake(struct ring *r) {
    pthread_mutex_lock(&r->m);
    r->wake_pending = 1;
    pthread_cond_signal(&r->c);
    pthread_mutex_unlock(&r->m);
}

static struct ring *find(uint32_t ctx_id, uint64_t blob_id) {
    for (struct ring *r = rings; r; r = r->next)
        if (r->ctx_id == ctx_id && r->blob_id == blob_id) return r;
    return NULL;
}

static void unref(struct ring *r) {
    if (atomic_fetch_sub(&r->refs, 1) != 1) return;
    pthread_mutex_destroy(&r->m);
    pthread_cond_destroy(&r->c);
    pthread_mutex_destroy(&r->drain);
    free(r);
}

/* Attaches a ring and tells the guest through its header (reply_len = MN_ATTACHED), which it reads once the attach's
 * fence completes; a refused attach leaves the header alone. */
static void attach(uint32_t ctx_id, uint64_t blob_id, int drained) {
    void *p = mn_executor_blob(ctx_id, blob_id, 0, MN_RING_BLOB_BYTES);
    if (!p || find(ctx_id, blob_id)) { error("attach: not a free ring blob", ctx_id, blob_id); return; }
    unsigned count = 0;
    for (struct ring *r = rings; r; r = r->next) count += r->ctx_id == ctx_id;
    if (count >= MN_MAX_RINGS) { error("attach: the context has its maximum of rings", ctx_id, MN_MAX_RINGS); return; }
    struct ring *r = calloc(1, sizeof *r);
    if (!r) { error("attach: no memory", ctx_id, blob_id); return; }
    r->ctx_id = ctx_id;
    r->blob_id = blob_id;
    r->drained = drained;
    atomic_init(&r->refs, 1);
    mn_ring_view_init(&r->v, p);
    pthread_mutex_init(&r->m, NULL);
    pthread_cond_init(&r->c, NULL);
    pthread_mutex_init(&r->drain, NULL);
    if (!drained && pthread_create(&r->thread, NULL, consume, r)) {
        unref(r);
        error("attach: no consumer thread", ctx_id, blob_id);
        return;
    }
    pthread_mutex_lock(&rings_lock);
    r->serial = ++next_serial;
    r->next = rings;
    rings = r;
    pthread_mutex_unlock(&rings_lock);
    r->v.hdr->reply_len = MN_ATTACHED;
}

static void stop(struct ring *r) {
    pthread_mutex_lock(&rings_lock); /* unlinked before the join: its consumer may be in a snapshot */
    struct ring **pp = &rings;
    while (*pp != r) pp = &(*pp)->next;
    *pp = r->next;
    pthread_mutex_unlock(&rings_lock);
    if (r->drained) { /* a drain in progress finishes; a later one finds it stopped */
        pthread_mutex_lock(&r->drain);
        r->stopped = 1;
        pthread_mutex_unlock(&r->drain);
    } else {
        pthread_mutex_lock(&r->m);
        atomic_store(&r->stop, 1);
        pthread_cond_signal(&r->c);
        pthread_mutex_unlock(&r->m);
        pthread_join(r->thread, NULL);
    }
    pthread_mutex_lock(&r->m);
    complete_waiters(r, 1);
    pthread_mutex_unlock(&r->m);
    if (wait_ring == r) wait_ring = NULL;
    if (hooks.ring_end) hooks.ring_end(r->ctx_id, r->state);
    unref(r);
}

/* ---- drained rings ---- */

/* ponytail: one submit lock for every context; a context's own if two processes' queue operations ever contend */
static pthread_mutex_t submit_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int holds_submit;

static void take(struct ring *r) {
    pthread_mutex_lock(&r->drain);
    atomic_store(&r->holder, (void *)pthread_self());
}
static void let_go(struct ring *r) {
    atomic_store(&r->holder, NULL);
    pthread_mutex_unlock(&r->drain);
}

static void drain(struct ring *r);

/* Every record q's guest published has run (finished only moves once a record's handler is done; read before the head,
 * so a record published meanwhile is after the caller's): nothing of q's to run before a queue operation. */
static int idle(struct ring *q) {
    uint64_t done = atomic_load_explicit(&q->finished, memory_order_acquire);
    return done == atomic_load_explicit(&q->v.hdr->head, memory_order_acquire);
}

/* r's queue operation: every other drained ring of the context runs what it has published (one this thread already
 * runs is past it: it is where this thread came from); an idle one isn't taken (the frame rate, 2026-10-18: a game's
 * dozens of rings, most of them idle). Under submit_lock. */
static void drain_others(struct ring *r) {
    size_t n = 0;
    struct ring *list[MN_MAX_RINGS]; /* a context's at most (attach refuses more) */
    pthread_mutex_lock(&rings_lock);
    for (struct ring *q = rings; q && n < sizeof list / sizeof *list; q = q->next)
        if (q->ctx_id == r->ctx_id && q->drained && q != r && !idle(q)) atomic_fetch_add(&q->refs, 1), list[n++] = q;
    pthread_mutex_unlock(&rings_lock);
    for (size_t i = 0; i < n; i++) {
        struct ring *q = list[i];
        if (atomic_load(&q->holder) != (void *)pthread_self()) {
            take(q);
            if (!q->stopped) drain(q);
            let_go(q);
        }
        unref(q);
    }
}

/* Runs r's published records on this thread; r->drain held (let go and taken again around waiting for submit_lock). */
static void drain(struct ring *r) {
    int took = 0;
    while (!r->stopped) {
        const struct mn_rec *rec = mn_ring_next(&r->v, &r->tail);
        if (!rec) break;
        if ((rec->op & MN_REC_SUBMIT) && !holds_submit) {
            if (pthread_mutex_trylock(&submit_lock)) {
                let_go(r); /* the holder may run this record meanwhile: read again at tail */
                pthread_mutex_lock(&submit_lock);
                take(r);
            }
            holds_submit = took = 1;
            continue;
        }
        if (rec->op & MN_REC_SUBMIT) drain_others(r);
        run(r, rec);
    }
    if (took) {
        holds_submit = 0;
        pthread_mutex_unlock(&submit_lock);
    }
    if (r->v.broken && !r->broken_said) {
        r->broken_said = 1;
        error("ring broken: a record didn't fit what was published", r->ctx_id, r->blob_id);
    }
    if (hooks.progress && !r->stopped) hooks.progress(r->ctx_id, r->state);
}

void mn_executor_trap(uint32_t ctx_id, uint64_t ring_blob, void *opaque) {
    (void)opaque;
    pthread_mutex_lock(&rings_lock);
    struct ring *r = find(ctx_id, ring_blob);
    if (r && r->drained) atomic_fetch_add(&r->refs, 1);
    else r = NULL;
    pthread_mutex_unlock(&rings_lock);
    if (!r) {
        error("trap for no ring", ctx_id, ring_blob);
        return;
    }
    take(r);
    drain(r);
    let_go(r);
    unref(r);
}

static void add_waiter(struct ring *r, uint64_t seq, uint32_t ring_idx, uint64_t fence_id) {
    pthread_mutex_lock(&r->m);
    unsigned n = atomic_load(&r->nwaiters);
    if (n == MAX_WAITERS) {
        pthread_mutex_unlock(&r->m);
        mn_dxmt_fence_done(r->ctx_id, ring_idx, fence_id); /* the guest rechecks and waits again */
        return;
    }
    r->waiters[n] = (struct waiter){ seq, fence_id, ring_idx };
    atomic_store(&r->nwaiters, n + 1);
    atomic_thread_fence(memory_order_seq_cst); /* nwaiters, then done_seq: pairs with run()'s fence */
    complete_waiters(r, 0);
    pthread_mutex_unlock(&r->m);
}

static void context_create(void *ctx, uint32_t ctx_id) {
    (void)ctx;
    atomic_store(&async_failures, 0);
    fprintf(stderr, "mn-executor: context %u\n", ctx_id);
}

uint64_t mn_executor_async_failures(void) { return atomic_load(&async_failures); }

static void context_destroy(void *ctx, uint32_t ctx_id) {
    (void)ctx;
    for (struct ring *r = rings, *next; r; r = next) {
        next = r->next;
        if (r->ctx_id == ctx_id) stop(r);
    }
    mn_executor_notified(ctx_id, 0); /* its notify waits complete: the guest is gone */
    if (hooks.context_end) hooks.context_end(ctx_id);
    fprintf(stderr, "mn-executor: context %u gone\n", ctx_id);
}

static void blob_created(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *ptr, uint64_t size) {
    (void)ctx;
    struct blob *b = malloc(sizeof *b);
    pthread_mutex_lock(&blob_lock);
    if (b && nblobs == cap_blobs) {
        struct blob **grown = realloc(blobs, (cap_blobs ? cap_blobs * 2 : 256) * sizeof *blobs);
        if (grown) blobs = grown, cap_blobs = cap_blobs ? cap_blobs * 2 : 256;
    }
    if (b && nblobs < cap_blobs) {
        *b = (struct blob){ ctx_id, blob_id, ptr, size, 1 };
        blobs[nblobs++] = b;
    } else {
        free(b); /* out of memory: the blob stays unknown here, so the guest's ops on it fail */
        error("no memory to track a blob", ctx_id, blob_id);
    }
    pthread_mutex_unlock(&blob_lock);
}

static void blob_destroyed(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *ptr, uint64_t size) {
    (void)ctx;
    struct ring *r = find(ctx_id, blob_id);
    if (r) stop(r); /* a ring blob closed without a detach: its consumer goes before its memory */
    struct blob *b = NULL;
    pthread_mutex_lock(&blob_lock);
    for (size_t i = 0; i < nblobs; i++)
        if (blobs[i]->ctx_id == ctx_id && blobs[i]->blob_id == blob_id) {
            b = blobs[i];
            blobs[i] = blobs[--nblobs];
            break;
        }
    pthread_mutex_unlock(&blob_lock);
    /* Lookups fail from here on. A handler that uses blob memory holds it (mn_executor_blob_hold), so a blob
     * another ring's op is still using stays mapped until that op lets go, and no op waits for another's. */
    if (b) mn_executor_blob_release(b); /* the table's reference: unmapped now unless something holds it */
    else munmap(ptr, size);             /* never tracked: still ours to unmap */
}

/* A notify queue's head (UINT64_MAX for no such blob: its waiters then complete rather than hang). */
static uint64_t queue_head(uint32_t ctx_id, uint64_t blob) {
    _Atomic uint64_t *h = mn_executor_blob(ctx_id, blob, 0, sizeof *h);
    return h ? atomic_load_explicit(h, memory_order_acquire) : UINT64_MAX;
}

/* Under nw_lock: completes the waiters of (ctx_id, blob) that are satisfied, all of a context's when blob is 0. */
static void complete_nwaiters(uint32_t ctx_id, uint64_t blob) {
    for (unsigned i = 0; i < nnwaiters;) {
        struct nwaiter *w = &nwaiters[i];
        if (w->ctx_id == ctx_id && (!blob || (w->blob == blob && queue_head(ctx_id, blob) > w->arg))) {
            mn_dxmt_fence_done(w->ctx_id, w->ring_idx, w->fence_id);
            *w = nwaiters[--nnwaiters];
        } else {
            i++;
        }
    }
}

void mn_executor_notified(uint32_t ctx_id, uint64_t blob_id) {
    pthread_mutex_lock(&nw_lock);
    complete_nwaiters(ctx_id, blob_id);
    pthread_mutex_unlock(&nw_lock);
}

static void add_nwaiter(uint32_t ctx_id, uint64_t blob, uint64_t arg, uint32_t ring_idx, uint64_t fence_id) {
    pthread_mutex_lock(&nw_lock); /* the poster writes head, then takes the lock: one of us sees the other */
    /* A queue has one consumer (notify.h): its new wait supersedes one it gave up on (its poll timed out). Kept, an
     * idle queue's waits would pile up one a second, each holding virtio-gpu control-queue entries until the guest's
     * queue fills and every process's next control message blocks (3DMark's idle UI stalled its workload). */
    for (unsigned i = 0; i < nnwaiters;) {
        struct nwaiter *w = &nwaiters[i];
        if (w->ctx_id == ctx_id && w->blob == blob) {
            mn_dxmt_fence_done(w->ctx_id, w->ring_idx, w->fence_id);
            *w = nwaiters[--nnwaiters];
        } else {
            i++;
        }
    }
    if (queue_head(ctx_id, blob) > arg || nnwaiters == MAX_NWAITERS) {
        mn_dxmt_fence_done(ctx_id, ring_idx, fence_id); /* the guest rechecks and waits again */
    } else {
        nwaiters[nnwaiters++] = (struct nwaiter){ctx_id, ring_idx, blob, arg, fence_id};
    }
    pthread_mutex_unlock(&nw_lock);
}

static void submit(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len) {
    (void)ctx;
    struct mn_ctrl c;
    wait_ring = NULL;
    notify_wait.set = 0;
    if (len < sizeof c) { error("short control message", ctx_id, len); return; }
    memcpy(&c, cmd, sizeof c);
    struct ring *r = c.type == MN_CTRL_RING_ATTACH || c.type == MN_CTRL_PEEK ? NULL : find(ctx_id, c.ring_blob);
    switch (c.type) {
    case MN_CTRL_RING_ATTACH:
        attach(ctx_id, c.ring_blob, c.arg == 1);
        return;
    case MN_CTRL_RING_DETACH:
        if (r) stop(r);
        return;
    case MN_CTRL_WAKE:
        if (!r) { error("wake: no such ring", ctx_id, c.ring_blob); return; }
        wake(r);
        return;
    case MN_CTRL_WAIT:
        if (!r) { error("wait: no such ring", ctx_id, c.ring_blob); return; }
        wait_ring = r;
        wait_seq = c.arg;
        wake(r); /* the guest waits for this ring: a lost doorbell must not leave its consumer asleep */
        return;
    case MN_CTRL_NOTIFY_WAIT:
        notify_wait.set = 1, notify_wait.blob = c.ring_blob, notify_wait.arg = c.arg;
        return;
    case MN_CTRL_PEEK: {
        uint64_t *p = mn_executor_blob(ctx_id, c.ring_blob, c.arg, sizeof *p);
        if (!p) { error("peek outside a blob", ctx_id, c.ring_blob); return; }
        if (*p == MN_PROBE_MAGIC) *p = MN_PROBE_MAGIC + 1;
        return;
    }
    default:
        { error("unknown control message", ctx_id, c.type); return; }
    }
}

static void fence(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id) {
    (void)ctx;
    struct ring *r = wait_ring;
    wait_ring = NULL;
    if (notify_wait.set) {
        notify_wait.set = 0;
        add_nwaiter(ctx_id, notify_wait.blob, notify_wait.arg, ring_idx, fence_id);
        return;
    }
    if (r && r->ctx_id == ctx_id) add_waiter(r, wait_seq, ring_idx, fence_id);
    else mn_dxmt_fence_done(ctx_id, ring_idx, fence_id);
}

size_t mn_executor_snapshot(uint32_t ctx_id, void *except, struct mn_mark *marks, size_t cap) {
    size_t n = 0;
    pthread_mutex_lock(&rings_lock);
    for (struct ring *r = rings; r && n < cap; r = r->next)
        if (r->ctx_id == ctx_id && (void *)r->state != except)
            marks[n++] = (struct mn_mark){r->state, atomic_load_explicit(&r->v.hdr->head, memory_order_acquire),
                                          r->serial};
    pthread_mutex_unlock(&rings_lock);
    return n;
}

int mn_executor_passed(uint32_t ctx_id, const struct mn_mark *marks, size_t n) {
    int passed = 1;
    pthread_mutex_lock(&rings_lock);
    for (size_t i = 0; i < n && passed; i++)
        for (struct ring *r = rings; r; r = r->next)
            if ((void *)r->state == marks[i].ring && r->serial == marks[i].serial && r->ctx_id == ctx_id) {
                passed = atomic_load_explicit(&r->finished, memory_order_acquire) >= marks[i].pos;
                break;
            }
    pthread_mutex_unlock(&rings_lock);
    return passed;
}

void mn_executor_start(mn_op_fn h, const struct mn_executor_hooks *k) {
    spin_ns = mn_executor_spin_ns(getenv("MN_EXECUTOR_SPIN_US"));
    static const struct mn_dxmt_ops ops = {
        .context_create = context_create, .context_destroy = context_destroy, .blob_created = blob_created,
        .blob_destroyed = blob_destroyed, .submit = submit, .fence = fence,
    };
    handler = h;
    if (k) hooks = *k;
    mn_dxmt_set_ops(&ops);
}
