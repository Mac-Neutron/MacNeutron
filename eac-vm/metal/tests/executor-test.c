/* The executor's blob lifetime on the Mac, without libkrun: the test stands in for mn_dxmt_set_ops and
 * mn_dxmt_fence_done and calls the executor's callbacks itself.
 *   freed      a blob the guest frees, with nothing holding it, is unmapped at once
 *   held       a held blob stays mapped (and out of the table) until its last hold is released
 *   marks      a snapshot of two rings passes only once the second has finished past its mark
 *   unrelated  a blob destroyed while another ring's op is blocked is unmapped at once, not after that op
 *   hooks      progress runs before a consumer parks; ring_end for each ring that stops, then context_end
 *   notify     an MN_CTRL_NOTIFY_WAIT fence completes once the queue's head passes arg: at once if it already has,
 *              else when mn_executor_notified says the queue moved
 *   spin       MN_EXECUTOR_SPIN_US (0 here, set before the start): a consumer parks after each record when they come
 *              0.5 ms apart; the parse keeps the default for a value outside 0-2000
 *   drain      a ring attached drained (arg 1) has no consumer: its records run only in mn_executor_trap, in order,
 *              on the caller's thread; an MN_REC_SUBMIT record on ring B first runs ring A's 100 published records;
 *              two threads trapping two rings at once both finish, and so do two that submit at once, many times;
 *              a trap for no ring returns */
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../host/executor.h"
#include "../ring.h"

struct mn_dxmt_ops {
    void *ctx;
    void (*context_create)(void *ctx, uint32_t ctx_id);
    void (*context_destroy)(void *ctx, uint32_t ctx_id);
    void (*blob_created)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*blob_destroyed)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*submit)(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len);
    void (*fence)(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);
};
static const struct mn_dxmt_ops *ops;
void mn_dxmt_set_ops(const struct mn_dxmt_ops *o) { ops = o; }
static atomic_int fences_done;
static uint64_t last_fence;
void mn_dxmt_fence_done(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id) {
    (void)ctx_id, (void)ring_idx;
    last_fence = fence_id;
    fences_done++;
}

static void *new_blob_sized(uint32_t ctx_id, uint64_t blob_id, uint64_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    assert(p != MAP_FAILED);
    ops->blob_created(ops->ctx, ctx_id, blob_id, p, size);
    return p;
}
static void *new_blob(uint32_t ctx_id, uint64_t blob_id) { return new_blob_sized(ctx_id, blob_id, 16384); }

static void ctrl(uint32_t ctx_id, uint32_t type, uint64_t blob) {
    struct mn_ctrl c = {type, 0, blob, 0};
    ops->submit(ops->ctx, ctx_id, &c, sizeof c);
}

/* A guest thread's ring: attached, with a producer view over it. */
struct guest_ring { uint64_t blob, head, seq; struct mn_ring_view v; };
static void wake_ring(void *p) { ctrl(1, MN_CTRL_WAKE, ((struct guest_ring *)p)->blob); }
static void guest_ring(struct guest_ring *g, uint64_t blob) {
    memset(g, 0, sizeof *g);
    g->blob = blob;
    mn_ring_view_init(&g->v, new_blob_sized(1, blob, MN_RING_BLOB_BYTES));
    ctrl(1, MN_CTRL_RING_ATTACH, blob);
    assert(g->v.hdr->reply_len == MN_ATTACHED);
}
static void send_op(struct guest_ring *g, uint32_t op) {
    struct mn_rec *r = mn_ring_reserve(&g->v, &g->head, 0);
    assert(r);
    r->op = op, r->len = 0, r->seq = ++g->seq;
    mn_ring_publish(&g->v, g->head, wake_ring, g);
}

#define OP_BLOCK 0x300u
#define OP_LOG 0x301u /* u64 payload: appended to the log, with the thread that ran it */
static atomic_int gate, progressed, ended, rings_ended;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t log_v[4096];
static pthread_t log_t[4096];
static unsigned nlog;
static atomic_int submits_seen;
static int32_t blocker(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len) {
    (void)ctx_id, (void)ring_state, (void)reply, (void)cap, (void)reply_len;
    while (op == OP_BLOCK && !gate) usleep(100);
    if (op & MN_REC_SUBMIT) submits_seen++; /* the executor strips it */
    if (op == OP_LOG && len == 8) {
        pthread_mutex_lock(&log_lock);
        if (nlog < 4096) memcpy(&log_v[nlog], p, 8), log_t[nlog++] = pthread_self();
        pthread_mutex_unlock(&log_lock);
    }
    return 0;
}
static void on_progress(uint32_t ctx_id, void *ring_state) { (void)ring_state, (void)ctx_id, progressed++; }
static void on_ring_end(uint32_t ctx_id, void *ring_state) { (void)ctx_id, (void)ring_state, rings_ended++; }
static void on_end(uint32_t ctx_id) {
    struct mn_mark m[4];
    assert(mn_executor_snapshot(ctx_id, NULL, m, 4) == 0); /* its rings have stopped */
    ended = (int)ctx_id;
}

static int mapped(void *p) { return msync(p, 16384, MS_ASYNC) == 0; }

static atomic_int destroyed;
static void *destroy_40(void *p) {
    ops->blob_destroyed(ops->ctx, 1, 40, p, 16384);
    destroyed = 1;
    return NULL;
}

/* A drained ring of context 2: no consumer, no doorbell. */
static void drained_ring(struct guest_ring *g, uint64_t blob) {
    memset(g, 0, sizeof *g);
    g->blob = blob;
    mn_ring_view_init(&g->v, new_blob_sized(2, blob, MN_RING_BLOB_BYTES));
    struct mn_ctrl c = {MN_CTRL_RING_ATTACH, 0, blob, 1};
    ops->submit(ops->ctx, 2, &c, sizeof c);
    assert(g->v.hdr->reply_len == MN_ATTACHED);
}
static void send_log(struct guest_ring *g, uint32_t flags, uint64_t v) {
    struct mn_rec *r = mn_ring_reserve(&g->v, &g->head, 8);
    assert(r);
    r->op = OP_LOG | flags, r->seq = ++g->seq;
    memcpy(r + 1, &v, 8);
    mn_ring_publish(&g->v, g->head, NULL, NULL);
}
static uint64_t done_of(struct guest_ring *g) { return atomic_load(&g->v.hdr->done_seq); }

struct trapper { struct guest_ring *g; int rounds, submit; };
static void *trap_thread(void *p) {
    struct trapper *t = p;
    for (int i = 0; i < t->rounds; i++) {
        send_log(t->g, 0, 1);
        send_log(t->g, t->submit ? MN_REC_SUBMIT | MN_REC_SYNC : MN_REC_SYNC, 2);
        mn_executor_trap(2, t->g->blob, NULL);
        if (done_of(t->g) != t->g->seq) return (void *)1; /* the reply is there when the trap returns */
    }
    return NULL;
}
static void *watchdog(void *p) {
    (void)p;
    sleep(20);
    puts("FAIL drain: deadlocked");
    _exit(1);
}

static void drain_cases(void) {
    pthread_t dog;
    pthread_create(&dog, NULL, watchdog, NULL);
    pthread_detach(dog);
    ops->context_create(ops->ctx, 2);
    struct guest_ring a, b;
    drained_ring(&a, 50);
    drained_ring(&b, 51);
    for (uint64_t i = 1; i <= 100; i++) send_log(&a, 0, i);
    usleep(20000);
    assert(nlog == 0 && done_of(&a) == 0); /* nothing runs them */
    mn_executor_trap(2, a.blob, NULL);
    assert(nlog == 100 && done_of(&a) == 100 && atomic_load(&a.v.hdr->tail) == a.head);
    for (unsigned i = 0; i < 100; i++) assert(log_v[i] == i + 1 && pthread_equal(log_t[i], pthread_self()));
    nlog = 0;
    for (uint64_t i = 1; i <= 100; i++) send_log(&a, 0, i);
    send_log(&b, MN_REC_SUBMIT | MN_REC_SYNC, 1000);
    mn_executor_trap(2, b.blob, NULL); /* b's queue operation: a's records first */
    assert(nlog == 101 && log_v[100] == 1000 && done_of(&a) == a.seq && done_of(&b) == b.seq);
    for (unsigned i = 0; i < 100; i++) assert(log_v[i] == i + 1);
    assert(submits_seen == 0);
    mn_executor_trap(2, 999, NULL); /* no such ring: an error on stderr */
    mn_executor_trap(2, a.blob, NULL); /* nothing new */
    for (int submit = 0; submit < 2; submit++) {
        struct trapper t1 = {&a, 2000, submit}, t2 = {&b, 2000, submit};
        pthread_t x, y;
        void *rx, *ry;
        pthread_create(&x, NULL, trap_thread, &t1);
        pthread_create(&y, NULL, trap_thread, &t2);
        pthread_join(x, &rx);
        pthread_join(y, &ry);
        assert(!rx && !ry);
    }
    ops->context_destroy(ops->ctx, 2);
    puts("drain ok");
}

int main(void) {
    static const struct mn_executor_hooks hooks = {on_progress, on_end, on_ring_end};
    assert(mn_executor_spin_ns("200") == 200000 && mn_executor_spin_ns("0") == 0);
    assert(mn_executor_spin_ns(NULL) == MN_EXECUTOR_SPIN_DEFAULT_NS);
    assert(mn_executor_spin_ns("-5") == MN_EXECUTOR_SPIN_DEFAULT_NS && mn_executor_spin_ns("abc") == MN_EXECUTOR_SPIN_DEFAULT_NS);
    assert(mn_executor_spin_ns("2001") == MN_EXECUTOR_SPIN_DEFAULT_NS);
    setenv("MN_EXECUTOR_SPIN_US", "0", 1); /* read once, at the start */
    mn_executor_start(blocker, &hooks);
    ops->context_create(ops->ctx, 1);

    void *p = new_blob(1, 7);
    assert(mn_executor_blob(1, 7, 0, 16) == p);
    ops->blob_destroyed(ops->ctx, 1, 7, p, 16384);
    assert(!mapped(p) && errno == ENOMEM);
    assert(!mn_executor_blob(1, 7, 0, 16));
    puts("freed ok");

    p = new_blob(1, 8);
    void *hold1, *hold2;
    assert(mn_executor_blob_hold(1, 8, 16, 32, &hold1) == (char *)p + 16);
    assert(mn_executor_blob_hold(1, 8, 0, 16384, &hold2) == p);
    assert(!mn_executor_blob_hold(1, 8, 16380, 8, &hold2) || 0); /* out of range: refused, and hold2 untouched */
    ops->blob_destroyed(ops->ctx, 1, 8, p, 16384);
    assert(mapped(p));
    assert(!mn_executor_blob(1, 8, 0, 16)); /* gone from the guest's view */
    mn_executor_blob_release(hold1);
    assert(mapped(p));
    mn_executor_blob_release(hold2);
    assert(!mapped(p));
    puts("held ok");

    struct guest_ring a, b;
    guest_ring(&a, 20);
    guest_ring(&b, 21);
    send_op(&b, OP_BLOCK); /* ring b's consumer holds this record until gate opens */
    send_op(&a, MN_OP_NOP + 0x400);
    usleep(20000);
    void *u = new_blob(1, 40);
    pthread_t t;
    pthread_create(&t, NULL, destroy_40, u);
    for (int i = 0; i < 100 && !destroyed; i++) usleep(1000);
    int unrelated = destroyed && !mapped(u);
    if (!unrelated) gate = 1; /* let the blocked op go, so the destroy can finish */
    pthread_join(t, NULL);
    printf("%s unrelated\n", unrelated ? "ok  " : "FAIL");
    assert(unrelated);
    struct mn_mark m[4];
    size_t n = mn_executor_snapshot(1, NULL, m, 4);
    assert(n == 2);
    assert(!mn_executor_passed(1, m, n));
    gate = 1;
    for (int i = 0; i < 1000 && !mn_executor_passed(1, m, n); i++) usleep(1000);
    assert(mn_executor_passed(1, m, n));
    assert(mn_executor_snapshot(1, m[0].ring, m + 2, 2) == 1); /* except one ring */
    puts("marks ok");

    usleep(10000); /* past the spin: both consumers park */
    assert(progressed >= 2);
    int parked = progressed;
    for (int i = 0; i < 10; i++) {
        send_op(&a, MN_OP_NOP + 0x400);
        usleep(500);
    }
    usleep(10000);
    printf("%s spin (%d parks for 10 records)\n", progressed - parked >= 9 ? "ok  " : "FAIL", progressed - parked);
    assert(progressed - parked >= 9);
    /* A notify queue: head is its first word (notify.h). */
    _Atomic uint64_t *head = new_blob_sized(1, 30, 16384);
    struct mn_ctrl w = {MN_CTRL_NOTIFY_WAIT, 0, 30, 0};
    int before = fences_done;
    ops->submit(ops->ctx, 1, &w, sizeof w);
    ops->fence(ops->ctx, 1, 3, 77);
    assert(fences_done == before); /* nothing past 0 yet */
    *head = 1;
    mn_executor_notified(1, 30);
    assert(fences_done == before + 1 && last_fence == 77);
    ops->submit(ops->ctx, 1, &w, sizeof w); /* already past: at once */
    ops->fence(ops->ctx, 1, 3, 78);
    assert(fences_done == before + 2 && last_fence == 78);
    puts("notify ok");

    ops->context_destroy(ops->ctx, 1);
    assert(ended == 1 && rings_ended == 2);
    puts("hooks ok");
    nlog = 0;
    drain_cases();
    assert(ended == 2 && rings_ended == 4);
    return 0;
}
