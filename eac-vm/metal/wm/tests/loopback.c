/* The guest and the host in one Mac process (milestone 4b-1 tests): the guest transport's calls go straight to the
 * host's winemetal dispatcher, and the executor's blobs are this file's mmaps, freed by the "guest" and held by the
 * host as the executor does. Each thread is its own ring. The guest's addresses are the host's here. */
#include "loopback.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../../guest/transport.h"
#include "../../host/executor.h"
#include "../marshal.h"

struct fblob { uint64_t id; void *ptr; uint64_t size; atomic_int refs; int freed; };
static struct fblob *blobs[1024];
static int nblobs;
static uint64_t next_id = 1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static _Thread_local uint64_t ring_state[8];

struct mn_wm_ctx loop_ctx = {.ctx_id = 1};
static size_t no_rings(void *c, void *e, struct mn_mark *m, size_t cap) { (void)c, (void)e, (void)m, (void)cap; return 0; }
static int all_passed(void *c, const struct mn_mark *m, size_t n) { (void)c, (void)m, (void)n; return 1; }
static struct mn_ht_rings rings = {NULL, no_rings, all_passed};

static void init(void) { loop_ctx.ht = mn_ht_new(&rings, mn_wm_release_object); }

void *loop_ring(void) { return ring_state; }

int mn_open(void) {
    pthread_once(&once, init);
    return 0;
}

int mn_blob_alloc(uint64_t size, struct mn_blob *b) {
    size = (size + MN_PAGE - 1) / MN_PAGE * MN_PAGE;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    struct fblob *f = calloc(1, sizeof *f);
    if (p == MAP_FAILED || !f) return -12;
    pthread_mutex_lock(&lock);
    *f = (struct fblob){.id = next_id++, .ptr = p, .size = size, .refs = 1};
    blobs[nblobs++] = f;
    pthread_mutex_unlock(&lock);
    *b = (struct mn_blob){.blob_id = f->id, .size = size, .ptr = p};
    return 0;
}

/* The 32-bit (WoW64) placements guest/transport.c makes below 4 GiB: the loopback has no 32-bit callers, so plain
 * memory. */
int mn_blob_alloc_low(uint64_t size, struct mn_blob *b) { return mn_blob_alloc(size, b); }
void *mn_low_alloc(uint64_t size) { return calloc(1, size ? size : 1); }
void mn_low_free(void *p) { free(p); }

static void drop(struct fblob *f) {
    if (atomic_fetch_sub(&f->refs, 1) == 1) munmap(f->ptr, f->size);
}

int mn_blob_free(struct mn_blob *b) {
    struct fblob *f = NULL;
    pthread_mutex_lock(&lock);
    for (int i = 0; i < nblobs; i++)
        if (blobs[i]->id == b->blob_id && !blobs[i]->freed) (f = blobs[i])->freed = 1;
    pthread_mutex_unlock(&lock);
    if (f) drop(f);
    return f ? 0 : -22;
}

int mn_send(uint32_t op, const void *p, uint32_t len) {
    uint32_t n;
    mn_open();
    mn_wm_host_call(&loop_ctx, ring_state, op - MN_OP_WM, p, len, NULL, 0, &n);
    return 0; /* the guest never hears of a queued call's fate */
}

int mn_call(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len) {
    mn_open();
    static uint8_t none[8];
    return mn_wm_host_call(&loop_ctx, ring_state, op - MN_OP_WM, p, len, reply ? reply : none, reply ? cap : 0,
                           reply_len);
}

void mn_forget(void) {}

int mn_notify_wait(uint64_t blob_id, uint64_t seen, int timeout_ms) {
    (void)blob_id, (void)seen, (void)timeout_ms;
    usleep(1000); /* no fences here: the caller polls */
    return 0;
}

static struct fblob *find(uint64_t id) {
    for (int i = 0; i < nblobs; i++)
        if (blobs[i]->id == id && !blobs[i]->freed) return blobs[i];
    return NULL;
}

void *mn_executor_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len) {
    (void)ctx_id;
    pthread_mutex_lock(&lock);
    struct fblob *f = find(blob_id);
    void *p = f && offset <= f->size && len <= f->size - offset ? (char *)f->ptr + offset : NULL;
    pthread_mutex_unlock(&lock);
    return p;
}

void *mn_executor_blob_hold(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len, void **hold) {
    (void)ctx_id;
    pthread_mutex_lock(&lock);
    struct fblob *f = find(blob_id);
    void *p = f && offset <= f->size && len <= f->size - offset ? (char *)f->ptr + offset : NULL;
    if (p) {
        atomic_fetch_add(&f->refs, 1);
        *hold = f;
    }
    pthread_mutex_unlock(&lock);
    return p;
}

void mn_executor_blob_release(void *hold) { drop(hold); }

size_t mn_executor_snapshot(uint32_t ctx_id, void *except, struct mn_mark *marks, size_t cap) {
    (void)ctx_id, (void)except, (void)marks, (void)cap;
    return 0;
}
int mn_executor_passed(uint32_t ctx_id, const struct mn_mark *marks, size_t n) {
    (void)ctx_id, (void)marks, (void)n;
    return 1;
}
uint64_t mn_executor_async_failures(void) { return 0; }
void mn_executor_notified(uint32_t ctx_id, uint64_t blob_id) { (void)ctx_id, (void)blob_id; } /* the guest polls */

int loop_mapped(const void *p) {
    long page = 16384;
    return msync((void *)((uintptr_t)p & ~(uintptr_t)(page - 1)), (size_t)page, MS_ASYNC) == 0;
}

int loop_blob_free_of(const void *p) {
    pthread_mutex_lock(&lock);
    struct mn_blob b = {0};
    for (int i = 0; i < nblobs; i++)
        if (!blobs[i]->freed && (const char *)p >= (char *)blobs[i]->ptr &&
            (const char *)p < (char *)blobs[i]->ptr + blobs[i]->size)
            b.blob_id = blobs[i]->id;
    pthread_mutex_unlock(&lock);
    return b.blob_id ? mn_blob_free(&b) : -22;
}
