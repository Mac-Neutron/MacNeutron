/* The Metal transport's guest side (spec §5.6): one dxmt context per process on /dev/dri/renderD128, and one command
 * ring per thread (attached on its first call, detached when it exits). x86-64 Linux, under FEX. */
#ifndef MN_TRANSPORT_H
#define MN_TRANSPORT_H
#include <stdint.h>
#include "../proto.h"

struct mn_blob { uint32_t gem; uint64_t blob_id, size; void *ptr; uint8_t low; };

int mn_open(void);                                   /* once per process: 0 or -errno */
int mn_blob_alloc(uint64_t size, struct mn_blob *b); /* rounded up to MN_PAGE; guest and host share ptr's memory */
/* The same below 2 GiB, for a 32-bit (WoW64) process's DXMT: mapped over a range Wine's allocator reserved and
 * committed, so Wine's view of the 32-bit address space stays right (as winevulkan places a WoW64 mapping). */
int mn_blob_alloc_low(uint64_t size, struct mn_blob *b);
/* Memory below 2 GiB from Wine's allocator (committed, read-write), or NULL; and its release. */
void *mn_low_alloc(uint64_t size);
void mn_low_free(void *p);
int mn_blob_free(struct mn_blob *b);                 /* a synchronous MN_OP_BLOB_RELEASE, then GEM_CLOSE */
int mn_send(uint32_t op, const void *p, uint32_t len); /* asynchronous, on this thread's ring: 0 or -errno */
/* Synchronous: returns the host's status (negative: an error), with up to cap reply bytes in reply. */
int mn_call(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len);

/* Drained rings (Direct3D forwarding M2): from now on this process's rings have no host consumer; a flush, one 8-byte
 * store of the ring's blob id to the context's trap blob, runs the ring on the storing vCPU's host thread before the
 * store returns. Call before the first ring exists (rings attached earlier keep their consumers). 0 or -errno. */
int mn_drained(void);
/* Asynchronous, without a flush (drained: the ring is flushed when it fills, or by the next flush). Without drained
 * rings, mn_send. In drained mode mn_send flushes each record: winemetal's records have no later flush to wait for. */
int mn_queue(uint32_t op, const void *p, uint32_t len);
void mn_flush(void); /* drained: runs this thread's ring now */
/* Synchronous by a flush: the reply is there when the store returns, no wait. op may carry MN_REC_SUBMIT (a queue
 * operation: every other drained ring of the context first runs what it has published). -EIO if the host didn't run
 * it (the ring stopped or broke). Without drained rings, mn_call. */
int mn_call_flush(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len);

/* Sleeps until the notify queue in blob blob_id has a head past seen (MN_CTRL_NOTIFY_WAIT on this thread's fence
 * index), or timeout_ms passes; without a fence index, a short sleep. The caller rechecks the queue. */
int mn_notify_wait(uint64_t blob_id, uint64_t seen, int timeout_ms);

/* In a child after fork: the parent's DRM file, context and rings are the parent's; the next call opens our own. */
void mn_forget(void);

/* This thread's ring: records sent, and the last one the host finished. */
void mn_ring_stats(uint64_t *sent, uint64_t *done);
/* 1 if this thread's ring waits on fences, 0 if it polls (all 63 fence ring indexes leased), -1 without a ring. */
int mn_ring_fenced(void);
/* This thread's ring header (diagnostics), or NULL without a ring. */
struct mn_ring *mn_ring_header(void);
/* How long mn_call spins on its reply before waiting on a fence (default 50 µs): the calibration knob. */
void mn_set_spin_us(uint32_t us);

#endif
