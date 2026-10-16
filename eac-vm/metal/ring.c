/* The command ring (see ring.h). The parked flag and head form a Dekker pair: the producer stores head then loads
 * parked, the consumer stores parked then loads head, each with a full fence between, so a wake is never lost.
 * parked is 0 (running), 1 (parked) or 2 (parked, doorbell sent). */
#include <stdatomic.h>
#include "ring.h"

#define MASK (MN_RING_BYTES - 1)

static uint32_t rec_size(uint32_t len) { return (uint32_t)sizeof(struct mn_rec) + ((len + 7u) & ~7u); }

void mn_ring_view_init(struct mn_ring_view *v, void *blob) {
    *v = (struct mn_ring_view){ 0 };
    v->hdr = blob;
    v->data = (uint8_t *)blob + MN_RING_DATA;
    v->reply = (uint8_t *)blob + MN_RING_REPLY;
}

struct mn_rec *mn_ring_reserve(struct mn_ring_view *v, uint64_t *head, uint32_t len) {
    if (len > MN_REC_MAX) return NULL;
    uint32_t size = rec_size(len);
    uint64_t off = *head & MASK, pad = off + size > MN_RING_BYTES ? MN_RING_BYTES - off : 0;
    if (*head + pad + size - v->seen > MN_RING_BYTES) {
        v->seen = atomic_load_explicit(&v->hdr->tail, memory_order_acquire);
        if (*head + pad + size - v->seen > MN_RING_BYTES) return NULL;
    }
    if (pad) {
        if (pad >= sizeof(struct mn_rec)) ((struct mn_rec *)(v->data + off))->op = MN_OP_WRAP;
        *head += pad;
        off = 0;
    }
    struct mn_rec *r = (struct mn_rec *)(v->data + off);
    r->len = len;
    *head += size;
    return r;
}

void mn_ring_publish(struct mn_ring_view *v, uint64_t head, void (*wake)(void *), void *ctx) {
    atomic_store_explicit(&v->hdr->head, head, memory_order_release);
    atomic_thread_fence(memory_order_seq_cst);
    uint32_t parked = 1; /* one doorbell per park: the first producer to see it parked marks it woken (2) */
    if (atomic_load_explicit(&v->hdr->parked, memory_order_relaxed) == 1 &&
        atomic_compare_exchange_strong(&v->hdr->parked, &parked, 2) && wake)
        wake(ctx);
}

const struct mn_rec *mn_ring_next(struct mn_ring_view *v, uint64_t *tail) {
    if (*tail == v->seen) v->seen = atomic_load_explicit(&v->hdr->head, memory_order_acquire);
    while (*tail != v->seen) {
        uint64_t off = *tail & MASK;
        const struct mn_rec *r = (const struct mn_rec *)(v->data + off);
        if (MN_RING_BYTES - off < sizeof(struct mn_rec) || r->op == MN_OP_WRAP) {
            *tail += MN_RING_BYTES - off;
            continue;
        }
        return r;
    }
    return NULL;
}

void mn_ring_done(struct mn_ring_view *v, uint64_t *tail, const struct mn_rec *r) {
    uint64_t seq = r->seq; /* the producer may reuse the record's space once tail moves */
    int sync = !!(r->op & MN_REC_SYNC);
    *tail += rec_size(r->len);
    if (!sync && ++v->pending < 64 && *tail != v->seen) return;
    v->pending = 0;
    atomic_store_explicit(&v->hdr->done_seq, seq, memory_order_release);
    atomic_store_explicit(&v->hdr->tail, *tail, memory_order_release);
}

int mn_ring_try_park(struct mn_ring_view *v) {
    atomic_store_explicit(&v->hdr->parked, 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    if (atomic_load_explicit(&v->hdr->head, memory_order_relaxed) !=
        atomic_load_explicit(&v->hdr->tail, memory_order_relaxed)) {
        atomic_store_explicit(&v->hdr->parked, 0, memory_order_relaxed);
        return 0;
    }
    return 1;
}

void mn_ring_unpark(struct mn_ring_view *v) { atomic_store_explicit(&v->hdr->parked, 0, memory_order_relaxed); }
