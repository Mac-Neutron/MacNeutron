/* Winemetal handles across the guest boundary (milestone 4b-1): the guest picks every handle, (generation << 32) |
 * index, and the host keeps a table per context that binds each to its object, translates and validates it. Both
 * sides count references in lockstep; the host holds one reference of its own on each bound object. Handles used or
 * released across rings follow the plan's ordering rules: a use waits for a binding still queued on another ring, and
 * a last release waits until the context's other rings have finished what they had published. */
#ifndef MN_WM_HANDLES_H
#define MN_WM_HANDLES_H
#include <stddef.h>
#include <stdint.h>
#include "../proto.h"

typedef uint64_t mn_handle; /* (generation << 32) | index; 0 = null */
#define MN_H_INDEX(h) ((uint32_t)(h))
#define MN_H_GEN(h) ((uint32_t)((h) >> 32))
#define MN_H_MAX (1u << 20)    /* indexes per context */
#define MN_H_QUARANTINE 65536u /* a freed index is reused only after this many later frees */
#define MN_H_WAIT_MS 5000      /* a use or bind that waits for another ring gives up after this */

enum mn_hkind {
    MN_HK_ANY = 0, /* lookups only: slots 0 and 1 take any kind */
    MN_HK_OBJC = 1, MN_HK_SM50_SHADER, MN_HK_SM50_BITCODE, MN_HK_SM50_ERROR, MN_HK_LISTENER, MN_HK_HEAP_EMU,
    MN_HK_POOL, /* an NSAutoreleasePool: only its own ring may release it, and nothing else takes it */
};
enum mn_own { MN_OWN_PLUS1, MN_OWN_AUTORELEASED, MN_OWN_BORROWED };

/* Guest side, one table per process. */
mn_handle mn_gh_alloc(enum mn_own own); /* refs 1; an autoreleased one joins this thread's innermost pool; 0 if full */
int mn_gh_retain(mn_handle h);          /* 0, or -1 for a handle that isn't live */
int mn_gh_release(mn_handle h);         /* 1 when the index was freed (a pool's release pops it first), 0, or -1 */
void mn_gh_drop(mn_handle h);           /* a synchronous reply said nil: free the index, no host release */
void mn_gh_pool_push(mn_handle pool);   /* slot 10 made pool on this thread */
void mn_gh_pool_pop(mn_handle pool);    /* pops it and the pools inside it: one reference less for each handle */

/* Host side, one table per context; rings are the executor's ring_state pointers. */
struct mn_ht_rings {
    void *ctx;
    size_t (*snapshot)(void *ctx, void *except, struct mn_mark *marks, size_t cap); /* published positions */
    int (*passed)(void *ctx, const struct mn_mark *marks, size_t n);               /* each finished past its mark */
};
struct mn_ht;
struct mn_ht *mn_ht_new(const struct mn_ht_rings *rings, void (*release)(enum mn_hkind kind, void *obj));
void mn_ht_free(struct mn_ht *t); /* context end: releases every bound and deferred object */
/* Binds h to obj, which carries one reference for the table (a creation's own, or one the caller took). 0, or -1:
 * refused (index out of range, already bound at this generation, a stale generation, or still bound after
 * MN_H_WAIT_MS). Autoreleased handles join ring's innermost pool. */
int mn_ht_bind(struct mn_ht *t, void *ring, mn_handle h, enum mn_hkind kind, void *obj, enum mn_own own);
/* The object, *ok 1; or *ok 0: refuse the call (null, out of range, stale, another kind, or not bound within
 * MN_H_WAIT_MS). A bound handle may carry a nil object (a queued creation that failed). */
void *mn_ht_get(struct mn_ht *t, mn_handle h, enum mn_hkind kind, int *ok);
int mn_ht_retain(struct mn_ht *t, mn_handle h);            /* 0, or -1 (and nothing changes) */
int mn_ht_release(struct mn_ht *t, void *ring, mn_handle h); /* the last one is deferred; 0 or -1 */
void mn_ht_sweep(struct mn_ht *t); /* runs the deferred releases whose rings have passed */
void mn_ht_pool_push(struct mn_ht *t, void *ring, mn_handle pool); /* pool is bound, kind MN_HK_POOL */
void mn_ht_pool_pop(struct mn_ht *t, void *ring, mn_handle pool);
/* ring's thread is gone, and its open pools with it: their handles lose that reference. */
void mn_ht_ring_end(struct mn_ht *t, void *ring);
size_t mn_ht_live(struct mn_ht *t);     /* bound handles */
size_t mn_ht_deferred(struct mn_ht *t); /* releases waiting for rings */

#endif
