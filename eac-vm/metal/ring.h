/* The command ring (spec §5.6): one producer (a guest thread) and one consumer (its host thread) over a ring blob
 * (proto.h). Positions are free-running byte counts; records are 8-byte aligned and never straddle the end of the
 * record area. */
#ifndef MN_RING_H
#define MN_RING_H
#include "proto.h"

struct mn_ring_view { struct mn_ring *hdr; uint8_t *data, *reply; };
void mn_ring_view_init(struct mn_ring_view *v, void *blob);

/* Producer: a record for len payload bytes at *head, which moves past it; NULL while the ring lacks the space or
 * len > MN_REC_MAX. The caller fills op, seq and the payload, then publishes. */
struct mn_rec *mn_ring_reserve(struct mn_ring_view *v, uint64_t *head, uint32_t len);
/* Producer: makes everything up to head visible, and calls wake(ctx) if the consumer is parked. */
void mn_ring_publish(struct mn_ring_view *v, uint64_t head, void (*wake)(void *), void *ctx);

/* Consumer: the record at *tail (moved past padding), or NULL if there is none. */
const struct mn_rec *mn_ring_next(struct mn_ring_view *v, uint64_t *tail);
/* Consumer: the record is finished (its reply written): publishes done_seq, then frees its space. */
void mn_ring_done(struct mn_ring_view *v, uint64_t *tail, const struct mn_rec *r);
/* Consumer: 1 if it may sleep (parked, and no record arrived meanwhile); 0 if work arrived (not parked). */
int mn_ring_try_park(struct mn_ring_view *v);
void mn_ring_unpark(struct mn_ring_view *v);

#endif
