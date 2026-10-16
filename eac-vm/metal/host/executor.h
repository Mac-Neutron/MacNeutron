/* The VM host's side of the Metal transport (spec §5.6): the executor behind libkrun's dxmt context (patch 0005). Each
 * attached ring gets a consumer thread that runs its records through a handler, spinning for 2 ms after the last one
 * and then parking until the guest's MN_CTRL_WAKE. */
#ifndef MN_EXECUTOR_H
#define MN_EXECUTOR_H
#include <stddef.h>
#include <stdint.h>
#include "../proto.h"

/* Runs one record (an op the transport doesn't handle itself). ring_state is 64 zeroed bytes per ring, the handler's;
 * reply is NULL with cap 0 for an asynchronous record. Returns the status a synchronous caller gets. */
typedef int32_t (*mn_op_fn)(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len,
                            void *reply, uint32_t cap, uint32_t *reply_len);

/* What a handler learns besides its records. */
struct mn_executor_hooks {
    void (*progress)(uint32_t ctx_id, void *ring_state); /* on a consumer, every 64 records and before it parks */
    void (*context_end)(uint32_t ctx_id);                 /* after the context's rings have stopped */
    void (*ring_end)(uint32_t ctx_id, void *ring_state);  /* a ring stopped: its state goes next */
};

/* Registers with libkrun's dxmt context; call before the VM starts. hooks may be NULL. */
void mn_executor_start(mn_op_fn handler, const struct mn_executor_hooks *hooks);
/* Every attached ring of the context but except (a ring_state), with its published position: up to cap marks. */
size_t mn_executor_snapshot(uint32_t ctx_id, void *except, struct mn_mark *marks, size_t cap);
/* 1 once every ring in marks has finished past its mark, or is gone. */
int mn_executor_passed(uint32_t ctx_id, const struct mn_mark *marks, size_t n);
/* The host address of [offset, offset + len) in one of a context's blobs, or NULL if it isn't one. Valid only on
 * libkrun's gpu worker thread, which is the one that frees blobs; anywhere else (an op on a ring) hold it instead. */
void *mn_executor_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len);
/* The same, valid until mn_executor_blob_release(*hold), even after the guest frees the blob (e.g. while a Metal buffer
 * wraps it). NULL, and *hold untouched, if it isn't one. */
void *mn_executor_blob_hold(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len, void **hold);
void mn_executor_blob_release(void *hold);

/* A notify queue in one of the context's blobs moved: completes the MN_CTRL_NOTIFY_WAITs it satisfies. */
void mn_executor_notified(uint32_t ctx_id, uint64_t blob_id);

/* Asynchronous records that failed since the last context began (they have no caller to tell). */
uint64_t mn_executor_async_failures(void);

/* 4a's test ops (testops.c). */
int32_t mn_testops(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                   uint32_t cap, uint32_t *reply_len);

#endif
