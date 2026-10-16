/* Shared events signalling the guest's Win32 events (milestone 4b-1, plan Task 7). A listener handle is bound to our
 * wrapper around winemetal's listener: the real one runs on a host thread of its own (slot 109), never on a ring's
 * consumer, and winemetal's callbacks there call the stand-in NtSetEvent, whose hook posts MN_NOTIFY_EVENT to that
 * listener's notify queue. The guest's slot-109 stub reads the queue and sets the Win32 events. */
#import <Foundation/Foundation.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../marshal.h"
#include "../notify.h"
#include "loader.h"
#include "notifyq.h"
#include "winemetal_thunks.h"

struct listener {
    void *real; /* winemetal's: its first field is the run loop, once slot 109 published it */
    struct mn_notifyq *q;
    pthread_t thread;
    atomic_int started, stopped;
};

static _Thread_local struct mn_notifyq *thread_q; /* the listener whose run loop this thread runs */

static void on_nt_event(uint64_t nt_event) {
    if (thread_q) mn_notifyq_post(thread_q, MN_NOTIFY_EVENT, nt_event);
}

static void *listener_main(void *p) {
    struct listener *w = p;
    thread_q = w->q;
    struct unixcall_generic_obj_noret a = {(obj_handle_t)w->real};
    mn_wm_invoke(109, &a); /* until slot 110 stops the run loop */
    thread_q = NULL;
    return NULL;
}

/* Stops the real listener and its thread, once; the guest's loop then reads MN_NOTIFY_STOP. */
static void stop(struct listener *w) {
    if (atomic_exchange(&w->stopped, 1)) return;
    if (w->started) {
        /* winemetal's destroy does nothing until the run loop is published, and the thread would never return */
        _Atomic(void *) *runloop = w->real;
        int i = 0;
        while (!atomic_load(runloop) && i++ < 5000) usleep(1000);
        if (atomic_load(runloop)) {
            struct unixcall_generic_obj_noret a = {(obj_handle_t)w->real};
            mn_wm_invoke(110, &a);
            pthread_join(w->thread, NULL);
        } else {
            fprintf(stderr, "mn-winemetal: a listener never started its run loop: left running\n");
            pthread_detach(w->thread);
        }
    } /* ponytail: a listener never started is winemetal's to leak, as natively */
    mn_notifyq_post(w->q, MN_NOTIFY_STOP, 0);
}

void mn_wm_listener_release(void *obj) {
    struct listener *w = obj;
    stop(w);
    mn_notifyq_put(w->q);
    free(w);
}

NTSTATUS_T mn_wm_listener_special(struct mn_wm_state *k) {
    switch (k->slot) {
    case 108: { /* the guest's notify queue for it came first, as its blob id */
        static atomic_int hooked;
        if (!atomic_exchange(&hooked, 1)) mn_wm_set_event_hook(on_nt_event);
        uint64_t blob;
        if (!k->data || k->data_len != sizeof blob) return MN_STATUS_INVALID_PARAMETER;
        memcpy(&blob, k->data, sizeof blob);
        struct unixcall_generic_obj_ret *a = (void *)k->p;
        struct listener *w = calloc(1, sizeof *w);
        if (!w || !(w->q = mn_notifyq_new(k->c, blob))) {
            free(w);
            return MN_STATUS_INVALID_PARAMETER;
        }
        NTSTATUS_T s = mn_wm_invoke(108, a);
        if (s || !a->ret) {
            mn_notifyq_put(w->q);
            free(w);
            return s;
        }
        w->real = (void *)a->ret;
        a->ret = (obj_handle_t)w;
        return 0;
    }
    case 109: {
        struct listener *w = (void *)((struct unixcall_generic_obj_noret *)k->p)->handle;
        if (!w) return MN_STATUS_INVALID_PARAMETER;
        if (atomic_exchange(&w->started, 1)) return 0;
        if (pthread_create(&w->thread, NULL, listener_main, w)) {
            w->started = 0;
            return MN_STATUS_UNSUCCESSFUL;
        }
        return 0;
    }
    case 110: { /* stopped now; the wrapper goes with the table's last release of the handle */
        struct listener *w = (void *)((struct unixcall_generic_obj_noret *)k->p)->handle;
        if (!w) return MN_STATUS_INVALID_PARAMETER;
        stop(w);
        mn_handle h;
        memcpy(&h, k->rec, sizeof h);
        mn_ht_release(k->c->ht, k->ring, h);
        return 0;
    }
    case 104: {
        struct unixcall_mtlsharedevent_setevent *a = (void *)k->p;
        struct listener *w = (void *)a->shared_event_listener;
        if (!w || !a->shared_event) return MN_STATUS_INVALID_PARAMETER;
        a->shared_event_listener = (obj_handle_t)w->real;
        return mn_wm_invoke(104, a);
    }
    case 146: {
        struct unixcall_mtlsharedevent_seteventatvalues *a = (void *)k->p;
        struct listener *w = (void *)a->shared_event_listener;
        const obj_handle_t *events = a->shared_events.ptr;
        if (!w || (a->count && (!events || !a->values.ptr))) return MN_STATUS_INVALID_PARAMETER;
        for (uint32_t i = 0; i < a->count; i++)
            if (!events[i]) return MN_STATUS_INVALID_PARAMETER; /* winemetal messages each one */
        a->shared_event_listener = (obj_handle_t)w->real;
        return mn_wm_invoke(146, a);
    }
    default:
        return MN_STATUS_NOT_IMPLEMENTED;
    }
}
