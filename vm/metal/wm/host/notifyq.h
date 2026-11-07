/* Host-to-guest notify queues (memory.m), refcounted: a context's own, and one per shared-event listener (events.m). */
#ifndef MN_WM_NOTIFYQ_H
#define MN_WM_NOTIFYQ_H
#include <stdint.h>
#include "../marshal.h"

struct mn_notifyq *mn_notifyq_new(struct mn_wm_ctx *c, uint64_t blob_id); /* refs 1; NULL if blob_id isn't one */
void mn_notifyq_post(struct mn_notifyq *q, uint32_t type, uint64_t value); /* and wakes a guest waiting on it */
void mn_notifyq_put(struct mn_notifyq *q);

#endif
