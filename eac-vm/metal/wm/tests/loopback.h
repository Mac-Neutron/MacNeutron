/* loopback.c: the guest and the host in one Mac process, for tests. */
#ifndef MN_WM_LOOPBACK_H
#define MN_WM_LOOPBACK_H
#include "../marshal.h"

extern struct mn_wm_ctx loop_ctx; /* the one context (made by the first transport call) */
void *loop_ring(void);            /* this thread's ring_state */
int loop_mapped(const void *p);   /* 1 while p's page is still mapped */
int loop_blob_free_of(const void *p); /* the guest frees the blob holding p (the host may still hold it) */

#endif
