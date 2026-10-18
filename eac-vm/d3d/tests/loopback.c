/* Direct3D forwarding's loopback transport (M2 Task 6): the guest's transport API (eac-vm/metal/guest/transport.h)
 * in one Mac process with the VM host's own executor, drained rings and dispatcher, and libkrun's part played here:
 * a blob is host memory both sides use, a ring is attached drained, and a flush is mn_executor_trap on the calling
 * thread, as a trap runs it on the storing vCPU's thread. So a queue operation runs every thread's published records
 * first, as in the VM. Native DXMT is MN_D3D_NATIVE's (default: the build's). */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../../metal/guest/transport.h"
#include "../../metal/host/executor.h"
#include "../../metal/ring.h"
#include "host/dispatch.h"
#include "../host/dispatch.h"

#define CTX 1

/* libkrun's dxmt context, played by us (executor.c registers its callbacks here). */
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
void mn_dxmt_fence_done(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id) { (void)ctx_id, (void)ring_idx, (void)fence_id; }

struct thread_ring { struct mn_blob blob; struct mn_ring_view v; uint64_t head, seq; };
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t key;
static _Thread_local struct thread_ring *tr;
static atomic_uint_fast64_t next_blob = 1;

static void ctrl(uint32_t type, uint64_t blob, uint64_t arg) {
    struct mn_ctrl c = {type, 0, blob, arg};
    ops->submit(ops->ctx, CTX, &c, sizeof c); /* the gpu worker's one at a time: ours under a lock */
}
static pthread_mutex_t worker = PTHREAD_MUTEX_INITIALIZER;

static uint64_t put(struct thread_ring *t, uint32_t op, const void *p, uint32_t len);
static void flush(struct thread_ring *t) { mn_executor_trap(CTX, t->blob.blob_id, NULL); }

static void thread_exit(void *p) {
    struct thread_ring *t = p;
    put(t, MN_OP_NOP | MN_REC_SYNC, NULL, 0);
    flush(t);
    pthread_mutex_lock(&worker);
    ctrl(MN_CTRL_RING_DETACH, t->blob.blob_id, 0);
    pthread_mutex_unlock(&worker);
    mn_blob_free(&t->blob);
    free(t);
    tr = NULL;
}

static void init(void) {
    pthread_key_create(&key, thread_exit);
    const char *dir = getenv("MN_D3D_NATIVE");
    mn_d3d_set_dir(dir && *dir ? dir : MN_D3D_NATIVE_DIR);
    mn_executor_start(mn_vm_dispatch, &mn_vm_hooks);
    ops->context_create(ops->ctx, CTX);
}

int mn_open(void) {
    pthread_once(&once, init);
    return 0;
}
int mn_drained(void) { return mn_open(); }
void mn_forget(void) {}

int mn_blob_alloc(uint64_t size, struct mn_blob *b) {
    mn_open();
    size = (size + MN_PAGE - 1) / MN_PAGE * MN_PAGE;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    if (p == MAP_FAILED) return -errno;
    *b = (struct mn_blob){.gem = 0, .blob_id = atomic_fetch_add(&next_blob, 1), .size = size, .ptr = p};
    pthread_mutex_lock(&worker);
    ops->blob_created(ops->ctx, CTX, b->blob_id, p, size);
    pthread_mutex_unlock(&worker);
    return 0;
}

int mn_blob_free(struct mn_blob *b) {
    pthread_mutex_lock(&worker);
    ops->blob_destroyed(ops->ctx, CTX, b->blob_id, b->ptr, b->size); /* the executor unmaps it once unused */
    pthread_mutex_unlock(&worker);
    return 0;
}

static struct thread_ring *ring(void) {
    if (tr) return tr;
    mn_open();
    struct thread_ring *t = calloc(1, sizeof *t);
    if (!t || mn_blob_alloc(MN_RING_BLOB_BYTES, &t->blob)) return free(t), NULL;
    mn_ring_view_init(&t->v, t->blob.ptr);
    pthread_mutex_lock(&worker);
    ctrl(MN_CTRL_RING_ATTACH, t->blob.blob_id, 1);
    pthread_mutex_unlock(&worker);
    if (t->v.hdr->reply_len != MN_ATTACHED) return mn_blob_free(&t->blob), free(t), NULL;
    pthread_setspecific(key, t);
    return tr = t;
}

static uint64_t put(struct thread_ring *t, uint32_t op, const void *p, uint32_t len) {
    struct mn_rec *r;
    while (!(r = mn_ring_reserve(&t->v, &t->head, len))) flush(t);
    r->op = op;
    r->seq = ++t->seq;
    if (len) memcpy(r + 1, p, len);
    mn_ring_publish(&t->v, t->head, NULL, NULL);
    return t->seq;
}

static int bad_op(uint32_t op) { return op == MN_OP_WRAP || (op & (MN_REC_SYNC | MN_REC_SUBMIT)); }

int mn_queue(uint32_t op, const void *p, uint32_t len) {
    struct thread_ring *t = ring();
    if (bad_op(op)) return -EINVAL;
    if (!t) return -ENODEV;
    if (len > MN_REC_MAX) return -E2BIG;
    put(t, op, p, len);
    return 0;
}

int mn_send(uint32_t op, const void *p, uint32_t len) {
    int r = mn_queue(op, p, len);
    if (!r) flush(tr);
    return r;
}

void mn_flush(void) {
    if (tr) flush(tr);
}

int mn_call_flush(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len) {
    struct thread_ring *t = ring();
    if (bad_op(op & ~MN_REC_SUBMIT)) return -EINVAL;
    if (!t) return -ENODEV;
    if (len > MN_REC_MAX) return -E2BIG;
    uint64_t seq = put(t, op | MN_REC_SYNC, p, len);
    flush(t);
    if (atomic_load_explicit(&t->v.hdr->done_seq, memory_order_acquire) < seq) return -EIO;
    uint32_t n = t->v.hdr->reply_len;
    if (reply && n) memcpy(reply, t->v.reply, n < cap ? n : cap);
    if (reply_len) *reply_len = n;
    return t->v.hdr->reply_status;
}

int mn_call(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len) {
    return mn_call_flush(op, p, len, reply, cap, reply_len);
}

int mn_notify_wait(uint64_t blob_id, uint64_t seen, int timeout_ms) {
    (void)blob_id, (void)seen, (void)timeout_ms;
    usleep(100); /* the caller rechecks its queue */
    return 0;
}

void mn_ring_stats(uint64_t *sent, uint64_t *done) {
    *sent = tr ? tr->seq : 0;
    *done = tr ? atomic_load(&tr->v.hdr->done_seq) : 0;
}
int mn_ring_fenced(void) { return tr ? 0 : -1; }
struct mn_ring *mn_ring_header(void) { return tr ? tr->v.hdr : NULL; }
void mn_set_spin_us(uint32_t us) { (void)us; }
