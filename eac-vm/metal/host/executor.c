/* The executor (see executor.h). For now: it tracks contexts and blobs, answers the probe's MN_CTRL_PEEK and completes
 * every fence at once. */
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "executor.h"
#include "../proto.h"

/* libkrun's dxmt context (patch 0005, rutabaga_dxmt's MnDxmtOps): not in libkrun.h, which ffier generates. */
struct mn_dxmt_ops {
    void *ctx;
    void (*context_create)(void *ctx, uint32_t ctx_id);
    void (*context_destroy)(void *ctx, uint32_t ctx_id);
    void (*blob_created)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*blob_destroyed)(void *ctx, uint32_t ctx_id, uint64_t blob_id);
    void (*submit)(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len);
    void (*fence)(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);
};
void mn_dxmt_set_ops(const struct mn_dxmt_ops *ops);
void mn_dxmt_fence_done(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);

#define MAX_BLOBS 4096
struct blob { uint32_t ctx_id; uint64_t blob_id; void *ptr; uint64_t size; };
static struct blob blobs[MAX_BLOBS];
static unsigned nblobs;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void context_create(void *ctx, uint32_t ctx_id) { (void)ctx; fprintf(stderr, "mn-executor: context %u\n", ctx_id); }
static void context_destroy(void *ctx, uint32_t ctx_id) { (void)ctx; fprintf(stderr, "mn-executor: context %u gone\n", ctx_id); }

static void blob_created(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *ptr, uint64_t size) {
    (void)ctx;
    pthread_mutex_lock(&lock);
    if (nblobs < MAX_BLOBS) blobs[nblobs++] = (struct blob){ ctx_id, blob_id, ptr, size };
    else fprintf(stderr, "mn-executor: error: more than %d blobs\n", MAX_BLOBS);
    pthread_mutex_unlock(&lock);
}

static void blob_destroyed(void *ctx, uint32_t ctx_id, uint64_t blob_id) {
    (void)ctx;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < nblobs; i++)
        if (blobs[i].ctx_id == ctx_id && blobs[i].blob_id == blob_id) { blobs[i] = blobs[--nblobs]; break; }
    pthread_mutex_unlock(&lock);
}

/* The host address of [offset, offset + len) in a blob, or NULL. */
static void *blob_at(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len) {
    void *p = NULL;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < nblobs; i++)
        if (blobs[i].ctx_id == ctx_id && blobs[i].blob_id == blob_id) {
            if (offset <= blobs[i].size && len <= blobs[i].size - offset) p = (char *)blobs[i].ptr + offset;
            break;
        }
    pthread_mutex_unlock(&lock);
    return p;
}

static void submit(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len) {
    (void)ctx;
    struct mn_ctrl c;
    if (len < sizeof c) { fprintf(stderr, "mn-executor: error: %u-byte command\n", len); return; }
    memcpy(&c, cmd, sizeof c);
    if (c.type != MN_CTRL_PEEK) { fprintf(stderr, "mn-executor: error: command %u\n", c.type); return; }
    uint64_t *p = blob_at(ctx_id, c.ring_blob, c.arg, sizeof *p);
    if (!p) { fprintf(stderr, "mn-executor: error: peek outside blob %" PRIu64 "\n", c.ring_blob); return; }
    if (*p == MN_PROBE_MAGIC) *p = MN_PROBE_MAGIC + 1;
}

static void fence(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id) {
    (void)ctx;
    mn_dxmt_fence_done(ctx_id, ring_idx, fence_id);
}

void mn_executor_start(void) {
    static const struct mn_dxmt_ops ops = {
        .context_create = context_create, .context_destroy = context_destroy, .blob_created = blob_created,
        .blob_destroyed = blob_destroyed, .submit = submit, .fence = fence,
    };
    mn_dxmt_set_ops(&ops);
}
