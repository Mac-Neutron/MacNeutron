/* Winemetal calls as records (milestone 4b-1). A record is the params block (padded to 8), then one region per
 * pointer field, depth first in field order: a header {u32 len, u32 present} and, for data the host reads, len bytes
 * padded to 8. The guest picks every handle a call returns and writes it into the params it sends; the host
 * translates handles in, binds the ones out, and for a synchronous call replies with the params block (guest pointers
 * and handles restored, nil outputs 0) and the regions the host wrote. */
#ifndef MN_WM_MARSHAL_H
#define MN_WM_MARSHAL_H
#include "calls.h"

typedef unsigned int NTSTATUS_T;

/* Guest side. */
#define MN_WM_MAX_OUTS 4
#define MN_WM_MAX_REGIONS 64
struct mn_wm_region { void *ptr; uint32_t len; uint8_t kind; };
struct mn_wm_out {
    uint8_t *buf; /* the record: the caller's buffer of cap bytes */
    uint32_t cap, len;
    int sync;
    unsigned nouts;
    mn_handle outs[MN_WM_MAX_OUTS];
    unsigned nregions;
    struct mn_wm_region regions[MN_WM_MAX_REGIONS]; /* the memory a reply's regions go back to */
};
/* Builds slot's record from params, picking the handles it returns and writing them into params at once. 0, or an
 * NTSTATUS to return without sending: a refused slot, a dead handle, or a record past cap. */
NTSTATUS_T mn_wm_pack(unsigned slot, void *params, struct mn_wm_out *out);
/* A synchronous call's reply: its outputs into params and the memory it points to; nil handles' indexes freed. */
NTSTATUS_T mn_wm_unpack(unsigned slot, void *params, const struct mn_wm_out *out, const void *reply, uint32_t len);
/* The call failed or never reached the host: frees the handles mn_wm_pack picked. */
void mn_wm_unwind(const struct mn_wm_out *out);

/* Host side. ring is the executor's ring_state: words 0 and 1 are the dispatcher's and the marshaller's. */
struct mn_wm_ctx {
    uint32_t ctx_id;
    struct mn_ht *ht;
    _Atomic uint64_t failures; /* queued calls that failed: they have no caller to tell */
};
int32_t mn_wm_host_call(struct mn_wm_ctx *c, void *ring, unsigned slot, const void *rec, uint32_t len, void *reply,
                        uint32_t cap, uint32_t *reply_len);
/* Frees the marshaller's memory in a ring_state (the ring is gone). */
void mn_wm_ring_end(void *ring);

/* invoke.m: winemetal's own slot (mn_wm_funcs), an Objective-C exception becoming STATUS_INVALID_PARAMETER; and the
 * table's release of an object, by kind. */
NTSTATUS_T mn_wm_invoke(unsigned slot, void *params);
void mn_wm_release_object(enum mn_hkind kind, void *obj);

#endif
