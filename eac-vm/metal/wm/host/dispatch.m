/* The executor's handler for winemetal ops (milestone 4b-1): one handle table per guest context, made on its first
 * call; a ring's state caches its context (word 0; the marshaller has word 1). */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "../marshal.h"
#include "../notify.h"
#include "dispatch.h"

struct ctx {
    struct mn_wm_ctx wm;
    struct mn_ht_rings rings;
    struct ctx *next;
};
static struct ctx *ctxs;
static pthread_mutex_t ctx_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t snapshot(void *p, void *except, struct mn_mark *marks, size_t cap) {
    return mn_executor_snapshot(((struct ctx *)p)->wm.ctx_id, except, marks, cap);
}
static int passed(void *p, const struct mn_mark *marks, size_t n) {
    return mn_executor_passed(((struct ctx *)p)->wm.ctx_id, marks, n);
}

static struct ctx *ctx_of(uint32_t ctx_id, void *ring_state) {
    struct ctx **cached = ring_state;
    if (*cached) return *cached;
    pthread_mutex_lock(&ctx_lock);
    struct ctx *c = ctxs;
    while (c && c->wm.ctx_id != ctx_id) c = c->next;
    if (!c && (c = calloc(1, sizeof *c))) {
        c->wm.ctx_id = ctx_id;
        c->rings = (struct mn_ht_rings){c, snapshot, passed};
        if ((c->wm.ht = mn_ht_new(&c->rings, mn_wm_release_object))) {
            c->next = ctxs;
            ctxs = c;
        } else {
            free(c);
            c = NULL;
        }
    }
    pthread_mutex_unlock(&ctx_lock);
    return *cached = c;
}

struct mn_wm_ctx *mn_wm_context(uint32_t ctx_id, void *ring_state) {
    struct ctx *c = ctx_of(ctx_id, ring_state);
    return c ? &c->wm : NULL;
}

int32_t mn_wm_dispatch(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len) {
    if (op < MN_OP_WM || op > MN_OP_WM + MN_WM_NOTIFY_ATTACH) return MN_ENOSYS; /* the slots, stats, notify */
    struct ctx *c = ctx_of(ctx_id, ring_state);
    if (!c) return (int32_t)MN_STATUS_UNSUCCESSFUL;
    return mn_wm_host_call(&c->wm, ring_state, op - MN_OP_WM, p, len, reply, cap, reply_len);
}

int32_t mn_wm_dispatch_test(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                            uint32_t cap, uint32_t *reply_len) {
    return op >= MN_OP_WM ? mn_wm_dispatch(ctx_id, ring_state, op, p, len, reply, cap, reply_len)
                          : mn_testops(ctx_id, ring_state, op, p, len, reply, cap, reply_len);
}

static void progress(uint32_t ctx_id, void *ring_state) {
    (void)ctx_id;
    struct ctx *c = *(struct ctx **)ring_state;
    if (c) mn_ht_sweep(c->wm.ht, ring_state);
}

static void ring_end(uint32_t ctx_id, void *ring_state) {
    (void)ctx_id;
    struct ctx *c = *(struct ctx **)ring_state;
    if (c) mn_ht_ring_end(c->wm.ht, ring_state);
    mn_wm_ring_end(ring_state);
}

static void context_end(uint32_t ctx_id) {
    pthread_mutex_lock(&ctx_lock);
    struct ctx **pp = &ctxs;
    while (*pp && (*pp)->wm.ctx_id != ctx_id) pp = &(*pp)->next;
    struct ctx *c = *pp;
    if (c) *pp = c->next;
    pthread_mutex_unlock(&ctx_lock);
    if (!c) return;
    uint64_t presented = atomic_load(&c->wm.presented);
    if (presented)
        fprintf(stderr, "mn-present: context %u: %llu drawables presented\n", ctx_id, (unsigned long long)presented);
    mn_ht_free(c->wm.ht);
    mn_wm_ctx_end(&c->wm);
    free(c);
}

const struct mn_executor_hooks mn_wm_hooks = {progress, context_end, ring_end};
