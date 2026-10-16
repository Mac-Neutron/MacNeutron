/* The VM host's side of the Metal transport (spec §5.6): the executor behind libkrun's dxmt context (patch 0005). Each
 * attached ring gets a consumer thread that runs its records through a handler, spinning for 2 ms after the last one
 * and then parking until the guest's MN_CTRL_WAKE. */
#ifndef MN_EXECUTOR_H
#define MN_EXECUTOR_H
#include <stdint.h>

/* Runs one record (an op the transport doesn't handle itself). ring_state is 64 zeroed bytes per ring, the handler's;
 * reply is NULL with cap 0 for an asynchronous record. Returns the status a synchronous caller gets. */
typedef int32_t (*mn_op_fn)(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len,
                            void *reply, uint32_t cap, uint32_t *reply_len);

/* Registers with libkrun's dxmt context; call before the VM starts. */
void mn_executor_start(mn_op_fn handler);
/* The host address of [offset, offset + len) in one of a context's blobs, or NULL if it isn't one. */
void *mn_executor_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len);

/* 4a's test ops (testops.c). */
int32_t mn_testops(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                   uint32_t cap, uint32_t *reply_len);

#endif
