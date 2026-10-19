/* The guest's unix side of Direct3D forwarding (M2 Task 6): slot MN_WM_D3D of the guest winemetal.so, which mnd3d.dll
 * reaches through winemetal's WMTForward (and the loopback library calls directly). A method's params are packed with
 * the generated tables (the proxies' addresses become their handles in the record), queued on the thread's drained
 * ring if asynchronous, else sent with a flush whose reply is there when it returns; a queue operation's flush first
 * runs every other ring of the context (MN_REC_SUBMIT). */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "marshal.h"
#include "notify.h"
#include "guest/transport.h"
#include "guest/galloc.h"
#ifdef MN_D3D_LOOPBACK
#include "win32_native_event.h" /* the guest's events are the native program's */
#else
extern unsigned int NtSetEvent(void *handle, void *prev_state), NtClose(void *handle); /* Wine's ntdll.so */
#endif
#include "mnd3d_params.h"
#include "../mn_d3d.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int ready; /* 0, or the transport's -errno */
static _Thread_local uint8_t *rec, *reply; /* one allocation, freed as its thread ends (thread_end) */
static pthread_key_t thread_end;

/* The arenas this process gave the host (galloc regions, for the process's life): Map's (blob, offset) is ours here. */
struct arena { uint64_t blob, offset, len; uint8_t *ptr; };
static struct arena arenas[256];
static unsigned narenas;
static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t arena_bytes; /* their total, for the trace (M4 Task 8), under arena_lock */
#define E_OUTOFMEMORY_ ((int32_t)0x8007000E)

/* A new arena of at least want bytes: 0, or -1 (the test's MN_D3D_NO_ARENAS, no memory, or the host refused). */
static int add_arena(uint64_t want) {
    const char *no = getenv("MN_D3D_NO_ARENAS");
    if (no && *no == '1') return -1;
    struct mn_region r;
    uint64_t len = want > MN_D3D_ARENA_BYTES ? want : MN_D3D_ARENA_BYTES, id;
    struct mn_d3d_arena a;
    pthread_mutex_lock(&arena_lock);
    int ok = narenas < sizeof arenas / sizeof *arenas && !mn_galloc(len, &r) &&
             !mn_glookup(r.ptr, r.len, &id, &a.blob_id, &a.offset);
    a.len = r.len;
    uint32_t n = 0;
    if (ok && (ok = !mn_call_flush(MN_OP_D3D + MN_D3D_OP_ARENA, &a, sizeof a, NULL, 0, &n)))
        arenas[narenas++] = (struct arena){a.blob_id, a.offset, a.len, r.ptr}, arena_bytes += a.len;
    pthread_mutex_unlock(&arena_lock);
    return ok ? 0 : -1;
}

static void *arena_ptr(uint64_t blob, uint64_t offset) {
    void *p = NULL;
    pthread_mutex_lock(&arena_lock);
    for (unsigned i = 0; i < narenas && !p; i++)
        if (arenas[i].blob == blob && offset >= arenas[i].offset && offset < arenas[i].offset + arenas[i].len)
            p = arenas[i].ptr + (offset - arenas[i].offset);
    pthread_mutex_unlock(&arena_lock);
    return p;
}

static int proxy_handle(uint64_t value, mn_handle *h) {
    const struct mn_d3d_proxy_hdr *p = (const void *)(uintptr_t)value;
    if (!p) return *h = 0, 0;
    if (p->magic != MN_D3D_PROXY_MAGIC) return -1;
    *h = p->handle;
    return 0;
}

/* The context's notify queue and its fences' mirror (op 2), and the thread that sets the events the host posts: in the
 * VM mnd3d.dll's own Wine thread (op 6), as NtSetEvent needs its caller's TEB and server pipes (a pthread of ours would
 * run on its creator's, which may be gone); in the loopback a thread of ours. */
#define MIRROR_BYTES (1u << 20)
static struct mn_blob queue_blob, mirror_blob;
static int events_ready;

static void trace_out(void);

static void *listener(void *arg) {
    (void)arg;
    struct mn_notify *q = queue_blob.ptr;
#ifndef MN_D3D_LOOPBACK
    struct timespec last = {0};
#endif
    for (;;) {
#ifndef MN_D3D_LOOPBACK
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - last.tv_sec >= 5) last = now, trace_out(); /* a killed process's trace, at most 5 s old */
#endif
        struct mn_notify_rec r;
        while (mn_notify_next(q, &r)) {
            if (r.type == MN_NOTIFY_SET) { /* the proxies' own, which they keep */
#ifdef MN_D3D_LOOPBACK
                mn_win32_set_event((void *)(uintptr_t)r.value);
#else
                NtSetEvent((void *)(uintptr_t)r.value, NULL);
#endif
                continue;
            }
            if (r.type != MN_NOTIFY_EVENT) continue;
            /* ours (proxy.cpp's on_event), let go once set: the program may have closed its own */
#ifdef MN_D3D_LOOPBACK
            mn_win32_set_event((void *)(uintptr_t)r.value);
            mn_win32_unref((void *)(uintptr_t)r.value);
#else
            NtSetEvent((void *)(uintptr_t)r.value, NULL);
            NtClose((void *)(uintptr_t)r.value);
#endif
        }
        mn_notify_wait(queue_blob.blob_id, atomic_load_explicit(&q->tail, memory_order_relaxed), 1000);
    }
    return NULL;
}

static void events(void) {
    uint32_t n = 0;
    if (mn_blob_alloc(MN_NOTIFY_BYTES, &queue_blob) || mn_blob_alloc(MIRROR_BYTES, &mirror_blob)) return;
    memset(queue_blob.ptr, 0, queue_blob.size);
    memset(mirror_blob.ptr, 0, mirror_blob.size);
    struct mn_d3d_notify a = {queue_blob.blob_id, mirror_blob.blob_id, mirror_blob.size};
    if (mn_call_flush(MN_OP_D3D + MN_D3D_OP_NOTIFY, &a, sizeof a, NULL, 0, &n)) return;
#ifdef MN_D3D_LOOPBACK
    pthread_t t;
    if (pthread_create(&t, NULL, listener, NULL)) return;
    pthread_detach(t);
#endif
    events_ready = 1;
}

/* MACNEUTRON_VM_D3D_TRACE=1: each method's calls, written to the share's traces/d3d-guest.<pid>.txt as the process ends
 * and every 5 s from the listener (M3 Task 10: a game killed at its run's end never ends; the loopback's, appended at
 * its end: MN_D3D_LOOPBACK_TRACE); a method the guest answered itself (op 3, once) is marked by why. */
static _Atomic uint64_t *counts;
static _Atomic uint64_t exits, submits; /* the calls that trapped: the others, and those that ran every ring first */
static uint8_t *why; /* 1 refused, 2 a special without guest code, 3 answered in the guest by design (M4 Task 8) */

static pthread_mutex_t trace_lock = PTHREAD_MUTEX_INITIALIZER; /* the listener's every 5 s and the exit's (M3's minors) */
static void trace_out_locked(void);
static void trace_out(void) {
    pthread_mutex_lock(&trace_lock);
    trace_out_locked();
    pthread_mutex_unlock(&trace_lock);
}
static void trace_out_locked(void) {
#ifdef MN_D3D_LOOPBACK
    const char *path = getenv("MN_D3D_LOOPBACK_TRACE"), *mode = "a";
#else
    char path[64];
    const char *mode = "w"; /* the process's own, rewritten */
    snprintf(path, sizeof path, "/mnt/share/traces/d3d-guest.%d.txt", (int)getpid());
#endif
    FILE *f = counts && path ? fopen(path, mode) : NULL;
    if (!f) return;
    fprintf(f, "# process %d\n", (int)getpid());
    fprintf(f, "# arenas %llu\n", (unsigned long long)arena_bytes); /* the guest memory it gave the host (M4) */
    fprintf(f, "# exits %llu\n# submits %llu\n", (unsigned long long)exits, (unsigned long long)submits);
    for (unsigned i = 0; i < MN_D3D_CALLS; i++)
        if (counts[i])
            fprintf(f, "%u %s %llu%s\n", i, mn_d3d_calls[i].name, (unsigned long long)counts[i],
                    why[i] == 1 ? " refused" : why[i] == 2 ? " unhandled" : why[i] == 3 ? " answered" : "");
    fclose(f);
}
__attribute__((destructor)) static void trace_end(void) { trace_out(); }

static void init(void) {
    const char *t = getenv("MACNEUTRON_VM_D3D_TRACE");
    if (t && *t == '1' && (why = calloc(MN_D3D_CALLS, 1))) counts = calloc(MN_D3D_CALLS, sizeof *counts);
    pthread_key_create(&thread_end, free);
    mn_wm_guest_handle = proxy_handle;
    ready = mn_open();
    if (!ready) ready = mn_drained();
    if (!ready) events();
}

static int buffers(void) {
    if (rec) return 0;
    if (!(rec = malloc(MN_REC_MAX + MN_REPLY_BYTES))) return -ENOMEM;
    reply = rec + MN_REC_MAX;
    pthread_setspecific(thread_end, rec);
    return 0;
}

static int32_t family(uint32_t id, void *params) {
    uint32_t n = 0;
    switch (id) {
    case MN_D3D_OP_RELEASE: {
        mn_handle h;
        memcpy(&h, params, sizeof h);
        if (mn_gh_release(h) < 0) return (int32_t)MN_STATUS_INVALID_HANDLE;
        return mn_queue(MN_OP_D3D + id, &h, sizeof h);
    }
    case MN_D3D_OP_QUERY: {
        struct mn_d3d_query_args *a = params;
        struct mn_d3d_query q = {a->handle, {0}, mn_gh_alloc(MN_OWN_PLUS1)};
        memcpy(q.iid, a->iid, sizeof q.iid);
        if (!q.out) return (int32_t)MN_STATUS_UNSUCCESSFUL;
        struct mn_d3d_query_reply r = {0};
        int32_t s = mn_call_flush(MN_OP_D3D + id, &q, sizeof q, &r, sizeof r, &n);
        if (!s && n != sizeof r) s = (int32_t)MN_STATUS_UNSUCCESSFUL;
        if (s || r.hr < 0) {
            mn_gh_drop(q.out);
            q.out = 0;
        }
        a->out = q.out, a->hr = s ? 0 : r.hr;
        memcpy(a->ifaces, r.ifaces, sizeof a->ifaces);
        return s;
    }
    case MN_D3D_OP_BLOB: {
        struct mn_d3d_blob_args *a = params;
        for (uint64_t at = 0;;) {
            struct mn_d3d_blob b = {a->handle, at, a->cap - at};
            int32_t s = mn_call_flush(MN_OP_D3D + id, &b, sizeof b, reply, MN_REPLY_BYTES, &n);
            if (s) return s;
            if (n < 8) return MN_EINVAL;
            memcpy(&a->size, reply, 8);
            if (n > 8) memcpy((uint8_t *)(uintptr_t)a->dst + at, reply + 8, n - 8);
            at += n - 8;
            if (n == 8 || at >= a->cap) return 0;
        }
    }
    case MN_D3D_OP_WINDOW: /* queued: the call that reads it comes after it on this thread's ring */
        return mn_queue(MN_OP_D3D + id, params, sizeof(struct mn_d3d_window));
    case MN_D3D_OP_DISPLAYS: {
        const struct mn_d3d_displays_args *d = params;
        return mn_queue(MN_OP_D3D + id, (const void *)(uintptr_t)d->data, d->len);
    }
    case MN_D3D_OP_ACTIONS: { /* after the call that took them: this thread's ring ran it already */
        struct mn_d3d_actions_args *a = params;
        uint32_t got = 0;
        int32_t s = mn_call_flush(MN_OP_D3D + id, NULL, 0, (void *)(uintptr_t)a->data, a->cap, &got);
        a->len = s ? 0 : got < a->cap ? got : a->cap; /* what was copied: the host's reply may be longer (M3's minors) */
        return s;
    }
    case MN_D3D_OP_SHOWN:
        return mn_call_flush(MN_OP_D3D + id, params, sizeof(uint64_t), NULL, 0, &n);
    case MN_D3D_OP_DRAWABLES: /* the mirror's first slots: our address of them, or 0 without the mirror */
        *(uint64_t *)params = events_ready ? (uintptr_t)mirror_blob.ptr : 0;
        return 0;
    case MN_D3D_OP_LISTEN:
        if (!events_ready) return MN_EINVAL;
        listener(NULL);
        return 0;
    case MN_D3D_OP_TRACE: {
        const uint32_t *m = params;
        if (counts && m[0] < MN_D3D_CALLS) {
            why[m[0]] = m[1] == 2 ? 3 : m[1] ? 1 : 2;
            atomic_fetch_add_explicit(&counts[m[0]], 1, memory_order_relaxed);
        }
        return 0;
    }
    default:
        return MN_EINVAL;
    }
}

/* A fence's mirror slot (aux[0]), our address of it in aux[1] (0: none). */
static void fence_slot(uint64_t aux[2]) {
    aux[1] = events_ready && aux[0] < mirror_blob.size / 8 ? (uintptr_t)((uint64_t *)mirror_blob.ptr + aux[0]) : 0;
}

/* A D3D11 or D3D10 map's (blob, offset), our address (aux[1] UINT64_MAX: the loopback's pointer, already ours). */
static void map_address(uint64_t aux[2]) {
    if (aux[1] != UINT64_MAX && (aux[0] || aux[1])) aux[0] = (uintptr_t)arena_ptr(aux[0], aux[1]);
}

int32_t mn_d3d_unix_call(void *args) {
    const struct mn_d3d_args *a = args;
    pthread_once(&once, init);
    if (ready) return ready;
    if (buffers()) return -ENOMEM;
    void *params = (void *)(uintptr_t)a->params;
    if (a->id < MN_D3D_FIRST) return family(a->id, params);
    if (a->id >= MN_D3D_CALLS || !mn_d3d_calls[a->id].name) return MN_EINVAL;
    if (counts) atomic_fetch_add_explicit(&counts[a->id], 1, memory_order_relaxed);
    const struct mn_wm_call *c = &mn_d3d_calls[a->id];
    for (int tries = 0;;) {
        struct mn_wm_out o = {.buf = rec, .cap = MN_REC_MAX};
        NTSTATUS_T s = mn_wm_pack_call(c, params, &o);
        if (s) return (int32_t)s;
        int submit = c->mode == MN_WM_SUBMIT || (a->flags & MN_D3D_ARG_HANDOFF);
        if (!o.sync && !submit && !(a->flags & MN_D3D_ARG_SYNC)) {
            int r = mn_queue(MN_OP_D3D + a->id, rec, o.len);
            if (r) mn_wm_unwind(&o, 0);
            return r;
        }
        uint32_t n = 0;
        if (counts) atomic_fetch_add_explicit(submit ? &submits : &exits, 1, memory_order_relaxed);
        int32_t r = mn_call_flush((MN_OP_D3D + a->id) | (submit ? MN_REC_SUBMIT : 0), rec, o.len, reply,
                                  MN_REPLY_BYTES, &n);
        if (r) {
            mn_wm_unwind(&o, r != -EIO && r != -ENODEV); /* a status from the host: it had the record */
            if (r != MN_D3D_NEED_ARENA) return r;
            uint64_t want = 0;
            if (n >= sizeof want) memcpy(&want, reply, sizeof want);
            /* the guest's memory, an arena a try: another thread's creation may take the one added first.
             * ponytail: 8 tries; a reservation for the asker if creators ever starve each other */
            if (++tries == 8 || add_arena(want)) return E_OUTOFMEMORY_;
            continue;
        }
        if (!o.sync) return 0;
        s = mn_wm_unpack_call(c, params, &o, reply, n);
        if (!s && a->id == MN_D3D_ID_ID3D12Device_CreateFence) /* the fence's mirror slot: our address of it */
            fence_slot(((mn_d3d_p_ID3D12Device_CreateFence *)params)->aux);
        if (!s && a->id == MN_D3D_ID_ID3D11Device5_CreateFence) /* D3D11's (M4) */
            fence_slot(((mn_d3d_p_ID3D11Device5_CreateFence *)params)->aux);
        if (!s && a->id == MN_D3D_ID_IMTLD3D11ContextExt2_GetPieces) { /* each piece's memory: our address (M4) */
            mn_d3d_p_IMTLD3D11ContextExt2_GetPieces *g = params;
            struct mn_d3d_piece *p = (struct mn_d3d_piece *)(uintptr_t)g->pieces;
            for (uint32_t i = 0; p && i < (g->count ? g->count : 1); i++)
                if (p[i].blob || p[i].offset) map_address(&p[i].blob); /* (blob, offset): adjacent, as a map's aux */
        }
        if (!s) /* D3D11's and D3D10's maps (M4): the arena's (blob, offset) in aux, our address of it in aux[0] */
            switch (a->id) {
            case MN_D3D_ID_ID3D11DeviceContext_Map: map_address(((mn_d3d_p_ID3D11DeviceContext_Map *)params)->aux); break;
            case MN_D3D_ID_ID3D10Buffer_Map: map_address(((mn_d3d_p_ID3D10Buffer_Map *)params)->aux); break;
            case MN_D3D_ID_ID3D10Texture1D_Map: map_address(((mn_d3d_p_ID3D10Texture1D_Map *)params)->aux); break;
            case MN_D3D_ID_ID3D10Texture2D_Map: map_address(((mn_d3d_p_ID3D10Texture2D_Map *)params)->aux); break;
            case MN_D3D_ID_ID3D10Texture3D_Map: map_address(((mn_d3d_p_ID3D10Texture3D_Map *)params)->aux); break;
            }
        if (!s && a->id == MN_D3D_ID_ID3D12Resource_Map) { /* the arena's (blob, offset): our address of it */
            uint64_t *at = (uint64_t *)(uintptr_t)((mn_d3d_p_ID3D12Resource_Map *)params)->data;
            if (at) at[0] = (uintptr_t)arena_ptr(at[0], at[1]);
        }
        return (int32_t)s;
    }
}
