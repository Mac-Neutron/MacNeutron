/* Winemetal calls as records (milestone 4b-1). A record is the params block (padded to 8), then one region per
 * pointer field, depth first in field order. A region is a header {u32 len, u32 how} and then:
 *   how 0  nothing (a NULL pointer)
 *   how 1  for data the host reads, len bytes padded to 8
 *   how 2  u64 blob, u64 offset: the data is in the thread's scratch blob (bulk data; the guest then waits)
 *   how 3  u64 blob, u64 offset, u64 region: blob memory the host maps in place (WMF_BLOB_PTR)
 * The guest picks every handle a call returns and writes it into the params it sends; the host translates handles
 * in, binds the ones out, and for a synchronous call replies with the params block (guest pointers and handles
 * restored, nil outputs 0) and, per region the host wrote, a header and its bytes (how 1) or none (how 2). */
#ifndef MN_WM_MARSHAL_H
#define MN_WM_MARSHAL_H
#include "calls.h"

typedef unsigned int NTSTATUS_T;

enum { MN_RH_NONE, MN_RH_INLINE, MN_RH_SCRATCH, MN_RH_BLOB };
struct mn_wm_rhdr { uint32_t len, how; };
#define MN_WM_SCRATCH_IN (MN_REC_MAX / 2)      /* larger data for the host goes through the scratch blob */
#define MN_WM_SCRATCH_OUT (MN_REPLY_BYTES / 4) /* and so does larger data back */

/* Guest side. */
#define MN_WM_MAX_OUTS 4
#define MN_WM_MAX_REGIONS 64
#define MN_WM_MAX_HOLDS 4
struct mn_wm_region { void *ptr; const void *scratch; uint32_t len; uint8_t kind; };
struct mn_wm_out {
    uint8_t *buf; /* the record: the caller's buffer of cap bytes */
    uint32_t cap, len;
    int sync;
    unsigned nouts;
    mn_handle outs[MN_WM_MAX_OUTS];
    unsigned nregions;
    struct mn_wm_region regions[MN_WM_MAX_REGIONS]; /* the memory a reply's regions go back to */
    unsigned nholds;
    uint64_t holds[MN_WM_MAX_HOLDS]; /* regions this call holds for the host (mn_ghold), undone if it never gets there */
    void **fix;                      /* after the reply, if its first handle came back (71: always): *fix = fix_value */
    void *fix_value;
    void *out_at[MN_WM_MAX_OUTS];    /* SM50: where each picked handle goes, through the caller's out-pointers */
};
/* Builds slot's record from params, picking the handles it returns and writing them into params at once. 0, or an
 * NTSTATUS to return without sending: a refused slot, a dead handle, or a record past cap. */
NTSTATUS_T mn_wm_pack(unsigned slot, void *params, struct mn_wm_out *out);
/* The same for a call another table describes (Direct3D forwarding's): no winemetal specials. */
NTSTATUS_T mn_wm_pack_call(const struct mn_wm_call *c, void *params, struct mn_wm_out *out);
/* Direct3D forwarding's: a handle field of a table's call holds a guest proxy's address, which this turns into its
 * handle in the record's copy (the caller's memory is left alone): 0, or -1 for something not a proxy. NULL: none. */
extern int (*mn_wm_guest_handle)(uint64_t value, mn_handle *h);
NTSTATUS_T mn_wm_unpack_call(const struct mn_wm_call *c, void *params, const struct mn_wm_out *out, const void *reply,
                             uint32_t len);
/* A synchronous call's reply: its outputs into params and the memory it points to; nil handles' indexes freed. */
NTSTATUS_T mn_wm_unpack(unsigned slot, void *params, const struct mn_wm_out *out, const void *reply, uint32_t len);
/* The call failed: frees the handles mn_wm_pack picked, and, if it never reached the host, its holds (else the host
 * frees each region the record named). */
void mn_wm_unwind(const struct mn_wm_out *out, int reached_host);
/* A listener handle's notify queue, and its blob (for mn_notify_wait); NULL if h isn't one. */
struct mn_notify;
struct mn_notify *mn_wm_listener_queue(mn_handle h, uint64_t *blob_id);
/* For the special slots' own code (slots.c). Guest: */
NTSTATUS_T mn_wm_emit(struct mn_wm_out *o, const void *p, uint64_t n);
NTSTATUS_T mn_wm_emit_none(struct mn_wm_out *o);
NTSTATUS_T mn_wm_emit_data(struct mn_wm_out *o, const void *ptr, uint64_t len, uint8_t kind); /* IN, OUT, STR_IN */
NTSTATUS_T mn_wm_copy_back(const struct mn_wm_out *o, const uint8_t *reply, uint32_t len, uint64_t pos);
void mn_wm_scratch_done(void);
void mn_wm_forget(void); /* in a child after fork: the thread's scratch blob is the parent context's */
/* slots.c, guest: SM50 slots' records and replies; slot 91's handles; slot 98's constants (after the params). */
NTSTATUS_T mn_wm_sm50_pack(unsigned slot, void *params, struct mn_wm_out *o);
NTSTATUS_T mn_wm_sm50_unpack(unsigned slot, void *params, const struct mn_wm_out *o, const void *reply, uint32_t len);
NTSTATUS_T mn_wm_enumerate_pack(void *params, struct mn_wm_out *o);
NTSTATUS_T mn_wm_enumerate_unpack(void *params, const struct mn_wm_out *o, const void *reply, uint32_t len);
void mn_wm_enumerate_unwind(void); /* a slot 91 that never came back: frees the log handles it picked */
NTSTATUS_T mn_wm_constants_emit(void *params, struct mn_wm_out *o);
/* The guest's pixel format for a texture handle (slots 45 and 145), or 0 when unknown: winemetal.c's, which asks the
 * host once per texture. */
extern uint32_t (*mn_wm_texture_format)(mn_handle texture);

/* Host side. ring is the executor's ring_state: words 0 and 1 are the dispatcher's and the marshaller's. */
struct mn_notifyq;
struct mn_wm_ctx {
    uint32_t ctx_id;
    struct mn_ht *ht;
    _Atomic uint64_t failures;       /* queued calls that failed: they have no caller to tell */
    _Atomic uint64_t presented;      /* drawables presented (slots 47 and 48, present.m) */
    struct mn_notifyq *_Atomic notify; /* MN_NOTIFY_REGION_FREE goes here (memory.m) */
    _Atomic uint32_t d3d_removed;      /* Direct3D: the HRESULT that removed the device; every later call answers it */
};
int32_t mn_wm_host_call(struct mn_wm_ctx *c, void *ring, unsigned slot, const void *rec, uint32_t len, void *reply,
                        uint32_t cap, uint32_t *reply_len);
/* A call another table describes (Direct3D forwarding's), its params translated and handed to invoke. */
int32_t mn_wm_host_call_table(struct mn_wm_ctx *c, void *ring, const struct mn_wm_call *call,
                              NTSTATUS_T (*invoke)(void *params), const void *rec, uint32_t len, void *reply,
                              uint32_t cap, uint32_t *reply_len);
/* invoke.m: how an MN_HK_D3D object is released (the Direct3D dispatcher sets it). */
extern void (*mn_wm_d3d_release)(void *obj);
/* Frees the marshaller's memory in a ring_state (the ring is gone). */
void mn_wm_ring_end(void *ring);

/* Blob memory a call brought (how 3), held until something takes it: a Metal object made over it. len 0: a holder
 * of region without memory of its own (a buffer placed in an emulated heap). */
struct mn_wm_hold { void *hold; uint8_t *ptr; uint64_t blob, offset, len, region; int taken; };
/* A region the host writes back: its host memory, the guest's original (for restoring handles and pointers in it), and
 * the scratch blob when it came through one. */
struct mn_wm_hregion {
    uint8_t *host;
    const uint8_t *guest;
    uint8_t *scratch;
    uint32_t len;
    uint8_t kind, how;
    const struct mn_wm_field *f;
};
/* A call on the host, for the special slots (memory.m). */
struct mn_wm_state {
    struct mn_wm_ctx *c;
    void *ring;
    unsigned slot;
    const struct mn_wm_call *call;
    int sync;
    const uint8_t *rec; /* the record, and how far the walk has read */
    uint32_t len, pos;
    uint8_t *p;        /* the host's params, handles translated */
    NTSTATUS_T status; /* the first handle that didn't translate: the walk reads on, so every blob region is seen */
    unsigned nholds;
    struct mn_wm_hold holds[MN_WM_MAX_HOLDS];
    struct mn_wm_hold *prefix_hold; /* 138, 141: the special's own blob region, or NULL */
    uint8_t *data;                  /* 45, 145: the texture data */
    uint32_t data_len;
    unsigned nregions;
    struct mn_wm_hregion regions[MN_WM_MAX_REGIONS];
    uint32_t out_bytes;
    uint8_t out_kind[MN_WM_MAX_OUTS]; /* each handle out is bound with this kind (a special may change it) */
    void *arena;
};
/* Memory for the rest of a host call; the record's next region (take_data in marshal.c), and a nil-or-bound handle
 * out for a special (0 if it couldn't be bound: the object is then released). */
void *mn_wm_alloc(struct mn_wm_state *k, size_t n);
NTSTATUS_T mn_wm_take(struct mn_wm_state *k, uint8_t kind, uint64_t want, int copy, uint8_t **host, uint32_t *len);
mn_handle mn_wm_bind_out(struct mn_wm_state *k, mn_handle h, enum mn_hkind kind, void *obj, enum mn_own own);
/* slots.c, host: SM50 (74-88), slot 91, slot 98, and host paths (54, 112, 113, 115, 117, 119). */
NTSTATUS_T mn_wm_sm50(struct mn_wm_state *k);
NTSTATUS_T mn_wm_enumerate(struct mn_wm_state *k);
NTSTATUS_T mn_wm_constants(struct mn_wm_state *k);
NTSTATUS_T mn_wm_paths(struct mn_wm_state *k);
void mn_wm_sm50_release(enum mn_hkind kind, void *obj);
/* cmdlist.c: slots 36-38's host side, the guest's flat list rebuilt as winemetal's. */
NTSTATUS_T mn_wm_encode(struct mn_wm_state *k);
/* memory.m: a special slot's host side, in place of calling winemetal. */
NTSTATUS_T mn_wm_special(struct mn_wm_state *k);
/* memory.m: blob memory nothing took: released, and its region freed for the guest. */
void mn_wm_drop_hold(struct mn_wm_ctx *c, struct mn_wm_hold *h);
/* memory.m: attaches the context's notify queue (op MN_OP_WM + MN_WM_NOTIFY_ATTACH); its end with the context. */
NTSTATUS_T mn_wm_notify_attach(struct mn_wm_ctx *c, uint64_t blob_id, uint64_t listener);
void mn_wm_ctx_end(struct mn_wm_ctx *c);

/* present.m: slots 47 and 48 (the count, and the probe with MN_PRESENT_PROBE=1), 71 (the reply's device is 0) and 73
 * (a view's release is its handle's). */
NTSTATUS_T mn_wm_present_special(struct mn_wm_state *k);
void mn_wm_present_native(struct mn_wm_ctx *c, void *cmdbuf, void *drawable); /* native DXMT's presents */
/* D3D11's (M4): counted at its Present call, probed when DXMT's encoder presents it, for a context by its id, which may
 * have ended (host/dispatch.m: under the contexts' lock) */
void mn_wm_present_count(struct mn_wm_ctx *c);
void mn_wm_present_probe(struct mn_wm_ctx *c, void *cmdbuf, void *drawable);
void mn_wm_present_probe_of(uint32_t ctx_id, void *cmdbuf, void *drawable);
void mn_wm_on_shown(void *drawable, void (*fn)(void *ctx), void *ctx); /* present.m: once the display shows it */

/* events.m: slots 104, 108-110 and 146; and the table's release of a listener. */
NTSTATUS_T mn_wm_listener_special(struct mn_wm_state *k);
void mn_wm_listener_release(void *obj);

/* memory.m: before the table's last release of an object (an emulated heap's buffer leaves its residency sets). */
void mn_wm_will_release(void *obj);

/* invoke.m: winemetal's own slot (mn_wm_funcs), an Objective-C exception becoming STATUS_INVALID_PARAMETER; and the
 * table's release of an object, by kind. */
NTSTATUS_T mn_wm_invoke(unsigned slot, void *params);
void mn_wm_release_object(enum mn_hkind kind, void *obj);

#endif
