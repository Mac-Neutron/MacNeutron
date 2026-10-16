/* The guest's memory that the host maps too (milestone 4b-1): regions of whole 16 KiB pages in 64 MiB blobs, a larger
 * request getting a blob of its own. A region comes back for reuse only once the guest freed it and every host holder
 * (a Metal object made over it) is gone: the host says so with MN_NOTIFY_REGION_FREE, one per holder, which the
 * allocator reads from the context's notify queue whenever it allocates. A region comes back zeroed, as a new Metal
 * buffer's memory does. */
#ifndef MN_WM_GALLOC_H
#define MN_WM_GALLOC_H
#include <stdint.h>

struct mn_region { uint64_t id; void *ptr; uint64_t len; }; /* id: MN_REGION(blob_id, first page) */

int mn_galloc(uint64_t len, struct mn_region *r); /* 0 or -errno */
void mn_gfree(uint64_t id);                       /* the guest is done with it */
void mn_ghold(uint64_t id);                       /* one more host holder: a call that makes a Metal object over it */
void mn_gunhold(uint64_t id);                     /* that call never reached the host */
/* The region holding [p, p + len): its id, blob and the offset of p in the blob; -1 if there is none. */
int mn_glookup(const void *p, uint64_t len, uint64_t *id, uint64_t *blob_id, uint64_t *offset);
void *mn_gptr(uint64_t id);        /* the region's guest address, or NULL */
unsigned mn_gholders(uint64_t id); /* for tests */
void mn_gdrain(void);              /* reads the context's notify queue now */
void mn_galloc_forget(void);       /* in a child after fork: the parent's blobs are the parent context's */

#endif
