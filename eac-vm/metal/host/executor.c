/* The executor (see executor.h). libkrun's gpu worker thread delivers control messages, fences and blob events, one at
 * a time; the ring list is that thread's alone. A fence follows the message it belongs to at once, so an MN_CTRL_WAIT
 * is paired with the next fence; any other fence completes immediately. */
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

#define SPIN_NS 2000000ull /* spec §5.6: a consumer spins 2 ms after its last record, then parks */
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
    uint64_t blob_id, tail;
    struct mn_ring_view v;
    pthread_t thread;
    pthread_mutex_t m;
    pthread_cond_t c;
    int wake_pending;
    atomic_int stop;
    atomic_uint nwaiters;
    atomic_int in_op; /* the handler may be using a blob: blob_destroyed waits it out */
    struct waiter waiters[MAX_WAITERS];
    uint64_t state[8];
    struct ring *next;
};
static struct ring *rings;
static mn_op_fn handler;
static struct ring *wait_ring; /* the MN_CTRL_WAIT the next fence belongs to */
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
    uint32_t op = rec->op & ~MN_REC_SYNC, n = 0;
    int sync = !!(rec->op & MN_REC_SYNC);
    int32_t status = 0;
    if (op != MN_OP_NOP && op != MN_OP_BLOB_RELEASE) {
        atomic_store(&r->in_op, 1); /* before any blob lookup: pairs with blob_destroyed */
        status = handler ? handler(r->ctx_id, r->state, op, r->v.payload, rec->len, sync ? r->v.reply : NULL,
                                   sync ? MN_REPLY_BYTES : 0, &n)
                         : MN_ENOSYS;
        atomic_store_explicit(&r->in_op, 0, memory_order_release);
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
        if (now_ns() - idle < SPIN_NS) {
            __asm__ volatile("yield");
            continue;
        }
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

/* Attaches a ring and tells the guest through its header (reply_len = MN_ATTACHED), which it reads once the attach's
 * fence completes; a refused attach leaves the header alone. */
static void attach(uint32_t ctx_id, uint64_t blob_id) {
    void *p = mn_executor_blob(ctx_id, blob_id, 0, MN_RING_BLOB_BYTES);
    if (!p || find(ctx_id, blob_id)) { error("attach: not a free ring blob", ctx_id, blob_id); return; }
    unsigned count = 0;
    for (struct ring *r = rings; r; r = r->next) count += r->ctx_id == ctx_id;
    if (count >= MN_MAX_RINGS) { error("attach: the context has its maximum of rings", ctx_id, MN_MAX_RINGS); return; }
    struct ring *r = calloc(1, sizeof *r);
    if (!r) { error("attach: no memory", ctx_id, blob_id); return; }
    r->ctx_id = ctx_id;
    r->blob_id = blob_id;
    mn_ring_view_init(&r->v, p);
    pthread_mutex_init(&r->m, NULL);
    pthread_cond_init(&r->c, NULL);
    if (pthread_create(&r->thread, NULL, consume, r)) {
        pthread_mutex_destroy(&r->m);
        pthread_cond_destroy(&r->c);
        free(r);
        error("attach: no consumer thread", ctx_id, blob_id);
        return;
    }
    r->next = rings;
    rings = r;
    r->v.hdr->reply_len = MN_ATTACHED;
}

static void stop(struct ring *r) {
    struct ring **pp = &rings;
    while (*pp != r) pp = &(*pp)->next;
    *pp = r->next;
    pthread_mutex_lock(&r->m);
    atomic_store(&r->stop, 1);
    pthread_cond_signal(&r->c);
    pthread_mutex_unlock(&r->m);
    pthread_join(r->thread, NULL);
    pthread_mutex_lock(&r->m);
    complete_waiters(r, 1);
    pthread_mutex_unlock(&r->m);
    pthread_mutex_destroy(&r->m);
    pthread_cond_destroy(&r->c);
    if (wait_ring == r) wait_ring = NULL;
    free(r);
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
    /* Lookups fail from here on; an op that found the blob before set in_op before its lookup (under blob_lock), so
     * waiting for every ring's in_op to clear means no handler still uses the memory libkrun unmaps next.
     * ponytail: waits out every ring's op, not just this blob's; per-blob pins when 4b's long ops make it show */
    for (struct ring *rr = rings; rr; rr = rr->next)
        while (atomic_load_explicit(&rr->in_op, memory_order_acquire)) sched_yield();
    if (b) mn_executor_blob_release(b); /* the table's reference: unmapped now unless something holds it */
    else munmap(ptr, size);             /* never tracked: still ours to unmap */
}

static void submit(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len) {
    (void)ctx;
    struct mn_ctrl c;
    wait_ring = NULL;
    if (len < sizeof c) { error("short control message", ctx_id, len); return; }
    memcpy(&c, cmd, sizeof c);
    struct ring *r = c.type == MN_CTRL_RING_ATTACH || c.type == MN_CTRL_PEEK ? NULL : find(ctx_id, c.ring_blob);
    switch (c.type) {
    case MN_CTRL_RING_ATTACH:
        attach(ctx_id, c.ring_blob);
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
    if (r && r->ctx_id == ctx_id) add_waiter(r, wait_seq, ring_idx, fence_id);
    else mn_dxmt_fence_done(ctx_id, ring_idx, fence_id);
}

void mn_executor_start(mn_op_fn h) {
    static const struct mn_dxmt_ops ops = {
        .context_create = context_create, .context_destroy = context_destroy, .blob_created = blob_created,
        .blob_destroyed = blob_destroyed, .submit = submit, .fence = fence,
    };
    handler = h;
    mn_dxmt_set_ops(&ops);
}
