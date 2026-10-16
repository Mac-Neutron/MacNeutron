/* Host-to-guest notifications for winemetal forwarding (milestone 4b-1): queues in blobs the guest allocates and
 * attaches (MN_WM_NOTIFY_ATTACH), the host the producer under a mutex, the guest the consumer. A context's own queue
 * carries MN_NOTIFY_REGION_FREE (a Metal object over a blob region is gone); each shared-event listener gets one for
 * its Win32 events (Task 7). */
#ifndef MN_WM_NOTIFY_H
#define MN_WM_NOTIFY_H
#include <stdatomic.h>
#include <stdint.h>
#include "../proto.h"

#define MN_NOTIFY_SLOTS 4096u
struct mn_notify_rec { uint32_t type, reserved; uint64_t value; };
struct mn_notify {
    _Alignas(64) _Atomic uint64_t head; /* records the host has written */
    _Alignas(64) _Atomic uint64_t tail; /* records the guest has read */
    _Alignas(64) struct mn_notify_rec recs[MN_NOTIFY_SLOTS];
};
#define MN_NOTIFY_BYTES ((sizeof(struct mn_notify) + MN_PAGE - 1) / MN_PAGE * MN_PAGE)
enum {
    MN_NOTIFY_REGION_FREE = 1, /* value: the region (blob_id << 32 | first page) one holder no longer uses */
    MN_NOTIFY_EVENT,           /* value: a Win32 event handle to set (Task 7) */
    MN_NOTIFY_STOP,            /* the listener is gone (Task 7) */
};
#define MN_REGION(blob_id, page) ((uint64_t)(blob_id) << 32 | (uint32_t)(page))

/* op MN_OP_WM + MN_WM_NOTIFY_ATTACH, synchronous: blob_id is a queue for the context (listener 0) or a listener. */
#define MN_WM_NOTIFY_ATTACH 0x801u
struct mn_wm_notify_attach { uint64_t blob_id, listener; };

_Static_assert(sizeof(struct mn_notify_rec) == 16, "mn_notify_rec");
_Static_assert(sizeof(struct mn_wm_notify_attach) == 16, "mn_wm_notify_attach");

/* Guest: the next record, 1; or 0 if there is none. One consumer per queue. */
static inline int mn_notify_next(struct mn_notify *n, struct mn_notify_rec *out) {
    uint64_t t = atomic_load_explicit(&n->tail, memory_order_relaxed);
    if (t == atomic_load_explicit(&n->head, memory_order_acquire)) return 0;
    *out = n->recs[t % MN_NOTIFY_SLOTS];
    atomic_store_explicit(&n->tail, t + 1, memory_order_release);
    return 1;
}

#endif
