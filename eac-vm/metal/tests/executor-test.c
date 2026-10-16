/* The executor's blob lifetime on the Mac, without libkrun: the test stands in for mn_dxmt_set_ops and
 * mn_dxmt_fence_done and calls the executor's callbacks itself.
 *   freed      a blob the guest frees, with nothing holding it, is unmapped at once
 *   held       a held blob stays mapped (and out of the table) until its last hold is released */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../host/executor.h"

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

static void *new_blob(uint32_t ctx_id, uint64_t blob_id) {
    void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
    assert(p != MAP_FAILED);
    ops->blob_created(ops->ctx, ctx_id, blob_id, p, 16384);
    return p;
}

static int mapped(void *p) { return msync(p, 16384, MS_ASYNC) == 0; }

int main(void) {
    mn_executor_start(NULL);
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
    return 0;
}
