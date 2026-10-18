/* Waiting (M2 Task 8): a context's Direct3D notify queue and its fences' mirror (op 2), the slots, and the relay
 * events native DXMT sets instead of the guest's (win32_native_event.h, fork 0076). A relay for a guest's event posts
 * MN_NOTIFY_EVENT with its handle, which the guest's listener sets; a relay for a fence's value raises its slot. A
 * relay fires once, on whichever thread DXMT sets it; what it uses holds a reference on the context's state, which
 * outlives the context until the last relay is gone. Slots are never reused: a late relay could raise a new fence's. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "marshal.h"
#include "notify.h"
#include "../../metal/host/executor.h"
#include "win32_native_event.h"
#include "dispatch.h"

struct mn_notifyq;
struct mn_notifyq *mn_notifyq_new(struct mn_wm_ctx *c, uint64_t blob_id); /* memory.m */
void mn_notifyq_post(struct mn_notifyq *q, uint32_t type, uint64_t value);
void mn_notifyq_put(struct mn_notifyq *q);

struct events {
    atomic_int refs;
    uint32_t ctx_id;
    struct mn_notifyq *q;
    _Atomic uint64_t *mirror;
    void *hold;
    uint64_t slots, next;
    struct events *next_ctx;
};
struct fence { void *obj; struct events *e; uint64_t slot; struct fence *next; };

/* ponytail: linear lists under one lock; a map if fences get many */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct events *all;
static struct fence *fences;

static void put(struct events *e) {
    if (atomic_fetch_sub(&e->refs, 1) != 1) return;
    mn_notifyq_put(e->q);
    mn_executor_blob_release(e->hold);
    free(e);
}

/* Under lock. */
static struct events *of(uint32_t ctx_id) {
    struct events *e = all;
    while (e && e->ctx_id != ctx_id) e = e->next_ctx;
    return e;
}

int mn_d3d_events_attach(struct mn_wm_ctx *c, uint64_t queue_blob, uint64_t mirror_blob, uint64_t mirror_len) {
    struct events *e = calloc(1, sizeof *e);
    if (!e || mirror_len < 8 || !(e->q = mn_notifyq_new(c, queue_blob))) return free(e), -1;
    if (!(e->mirror = mn_executor_blob_hold(c->ctx_id, mirror_blob, 0, mirror_len, &e->hold))) {
        mn_notifyq_put(e->q);
        free(e);
        return -1;
    }
    atomic_init(&e->refs, 1);
    e->ctx_id = c->ctx_id, e->slots = mirror_len / 8;
    pthread_mutex_lock(&lock);
    struct events *old = of(c->ctx_id);
    if (old) { /* again (a fork's child has its own context): the new one */
        struct events **pp = &all;
        while (*pp != old) pp = &(*pp)->next_ctx;
        *pp = old->next_ctx;
    }
    e->next_ctx = all;
    all = e;
    pthread_mutex_unlock(&lock);
    if (old) put(old);
    return 0;
}

void mn_d3d_events_end(uint32_t ctx_id) {
    pthread_mutex_lock(&lock);
    struct events **pp = &all;
    while (*pp && (*pp)->ctx_id != ctx_id) pp = &(*pp)->next_ctx;
    struct events *e = *pp;
    if (e) *pp = e->next_ctx;
    for (struct fence **fp = &fences; *fp;) {
        struct fence *f = *fp;
        if (f->e != e || !e) {
            fp = &f->next;
            continue;
        }
        *fp = f->next;
        put(f->e);
        free(f);
    }
    pthread_mutex_unlock(&lock);
    if (e) put(e);
}

void mn_d3d_mirror_raise(uint64_t *slot, uint64_t v) {
    _Atomic uint64_t *s = (_Atomic uint64_t *)slot;
    uint64_t cur = atomic_load(s);
    while (cur < v && !atomic_compare_exchange_weak(s, &cur, v)) {}
}

uint64_t mn_d3d_fence_new(uint32_t ctx_id, void *obj, uint64_t initial) {
    struct fence *f = malloc(sizeof *f);
    uint64_t slot = UINT64_MAX;
    pthread_mutex_lock(&lock);
    struct events *e = of(ctx_id);
    if (f && e && e->next < e->slots) {
        slot = e->next++;
        atomic_store(&e->mirror[slot], initial);
        atomic_fetch_add(&e->refs, 1);
        *f = (struct fence){obj, e, slot, fences};
        fences = f;
        f = NULL;
    } else if (e && e->next >= e->slots) {
        fprintf(stderr, "mn-d3d: error: the fences' mirror is full (%llu): GetCompletedValue goes to the host\n",
                (unsigned long long)e->slots);
    }
    pthread_mutex_unlock(&lock);
    free(f);
    return slot;
}

/* The fence's slot (and a reference on its state), or NULL. Under lock. */
static _Atomic uint64_t *slot_of(void *obj, struct events **held) {
    for (struct fence *f = fences; f; f = f->next)
        if (f->obj == obj) {
            atomic_fetch_add(&f->e->refs, 1);
            *held = f->e;
            return &f->e->mirror[f->slot];
        }
    return NULL;
}

void mn_d3d_fence_gone(void *obj) {
    pthread_mutex_lock(&lock);
    for (struct fence **fp = &fences; *fp; fp = &(*fp)->next)
        if ((*fp)->obj == obj) {
            struct fence *f = *fp;
            *fp = f->next;
            put(f->e);
            free(f);
            break;
        }
    pthread_mutex_unlock(&lock);
}

void mn_d3d_fence_raise(void *obj, uint64_t v) {
    struct events *e = NULL;
    pthread_mutex_lock(&lock);
    _Atomic uint64_t *s = slot_of(obj, &e);
    pthread_mutex_unlock(&lock);
    if (!s) return;
    mn_d3d_mirror_raise((uint64_t *)s, v);
    put(e);
}

struct relay { struct events *e; _Atomic uint64_t *slot; uint64_t value; };

static void fire(void *ctx) {
    struct relay *r = ctx;
    if (r->slot) mn_d3d_mirror_raise((uint64_t *)r->slot, r->value);
    else mn_notifyq_post(r->e->q, MN_NOTIFY_EVENT, r->value);
    put(r->e);
    free(r);
}

static void *relay(struct events *e, _Atomic uint64_t *slot, uint64_t value) {
    struct relay *r = malloc(sizeof *r);
    void *h = r ? mn_win32_relay_event(fire, r) : NULL;
    if (!h) {
        free(r);
        put(e);
        return NULL;
    }
    *r = (struct relay){e, slot, value};
    return h;
}

void *mn_d3d_event_relay(uint32_t ctx_id, uint64_t guest_event) {
    pthread_mutex_lock(&lock);
    struct events *e = of(ctx_id);
    if (e) atomic_fetch_add(&e->refs, 1);
    pthread_mutex_unlock(&lock);
    if (!e) {
        fprintf(stderr, "mn-d3d: error: an event to set but no notify queue attached\n");
        return NULL;
    }
    return relay(e, NULL, guest_event);
}

void *mn_d3d_fence_relay(void *obj, uint64_t value) {
    struct events *e = NULL;
    pthread_mutex_lock(&lock);
    _Atomic uint64_t *s = slot_of(obj, &e);
    pthread_mutex_unlock(&lock);
    return s ? relay(e, s, value) : NULL;
}

void mn_d3d_relay_done(void *h) { mn_win32_unref(h); } /* DXMT holds its own until it sets it */
