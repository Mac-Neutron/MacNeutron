/* The executor's handler and hooks for winemetal ops (dispatch.m), for macneutron-vm without --metal-test-ops. */
#ifndef MN_WM_DISPATCH_H
#define MN_WM_DISPATCH_H
#include "../../host/executor.h"

int32_t mn_wm_dispatch(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len);
extern const struct mn_executor_hooks mn_wm_hooks;

#endif
