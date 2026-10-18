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
#include "marshal.h"
#include "notify.h"
#include "guest/transport.h"
#include "guest/galloc.h"
#ifdef MN_D3D_LOOPBACK
#include "win32_native_event.h" /* the guest's events are the native program's */
#else
extern unsigned int NtSetEvent(void *handle, void *prev_state); /* Wine's ntdll.so */
#endif
#include "mnd3d_params.h"
#include "../mn_d3d.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int ready; /* 0, or the transport's -errno */
static _Thread_local uint8_t *rec, *reply;

/* The arenas this process gave the host (galloc regions, for the process's life): Map's (blob, offset) is ours here. */
struct arena { uint64_t blob, offset, len; uint8_t *ptr; };
static struct arena arenas[256];
static unsigned narenas;
static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;
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
        arenas[narenas++] = (struct arena){a.blob_id, a.offset, a.len, r.ptr};
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

/* The context's notify queue and its fences' mirror (op 2), and the thread that sets the events the host posts. */
#define MIRROR_BYTES (1u << 20)
static struct mn_blob queue_blob, mirror_blob;
static int events_ready;

static void *listener(void *arg) {
    (void)arg;
    struct mn_notify *q = queue_blob.ptr;
    for (;;) {
        struct mn_notify_rec r;
        while (mn_notify_next(q, &r)) {
            if (r.type != MN_NOTIFY_EVENT) continue;
#ifdef MN_D3D_LOOPBACK
            mn_win32_set_event((void *)(uintptr_t)r.value);
            mn_win32_unref((void *)(uintptr_t)r.value); /* the proxy's reference: the program may have closed it */
#else
            NtSetEvent((void *)(uintptr_t)r.value, NULL); /* a closed handle: an error, nothing more */
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
    pthread_t t;
    if (mn_call_flush(MN_OP_D3D + MN_D3D_OP_NOTIFY, &a, sizeof a, NULL, 0, &n) || pthread_create(&t, NULL, listener, NULL))
        return;
    pthread_detach(t);
    events_ready = 1;
}

/* MACNEUTRON_VM_D3D_TRACE=1: each method's calls, written to the share's traces/d3d-guest.txt as the process ends
 * (the loopback's: MN_D3D_LOOPBACK_TRACE); a method the guest answered itself (op 3, once) is marked by why. */
static _Atomic uint64_t *counts;
static uint8_t *why; /* 1 refused, 2 a special without guest code */

static void trace_out(void) {
#ifdef MN_D3D_LOOPBACK
    const char *path = getenv("MN_D3D_LOOPBACK_TRACE");
#else
    const char *path = "/mnt/share/traces/d3d-guest.txt";
#endif
    FILE *f = counts && path ? fopen(path, "a") : NULL;
    if (!f) return;
    fprintf(f, "# process %d\n", (int)getpid());
    for (unsigned i = 0; i < MN_D3D_CALLS; i++)
        if (counts[i])
            fprintf(f, "%u %s %llu%s\n", i, mn_d3d_calls[i].name, (unsigned long long)counts[i],
                    why[i] == 1 ? " refused" : why[i] == 2 ? " unhandled" : "");
    fclose(f);
}
__attribute__((destructor)) static void trace_end(void) { trace_out(); }

static void init(void) {
    const char *t = getenv("MACNEUTRON_VM_D3D_TRACE");
    if (t && *t == '1' && (why = calloc(MN_D3D_CALLS, 1))) counts = calloc(MN_D3D_CALLS, sizeof *counts);
    mn_wm_guest_handle = proxy_handle;
    ready = mn_open();
    if (!ready) ready = mn_drained();
    if (!ready) events();
}

static int buffers(void) {
    if (!rec) rec = malloc(MN_REC_MAX);
    if (!reply) reply = malloc(MN_REPLY_BYTES);
    return rec && reply ? 0 : -ENOMEM;
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
        int32_t hr = 0, s = mn_call_flush(MN_OP_D3D + id, &q, sizeof q, &hr, sizeof hr, &n);
        if (s || hr < 0) {
            mn_gh_drop(q.out);
            q.out = 0;
        }
        a->out = q.out, a->hr = s ? 0 : hr;
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
    case MN_D3D_OP_TRACE: {
        const uint32_t *m = params;
        if (counts && m[0] < MN_D3D_CALLS) {
            why[m[0]] = m[1] ? 1 : 2;
            atomic_fetch_add_explicit(&counts[m[0]], 1, memory_order_relaxed);
        }
        return 0;
    }
    default:
        return MN_EINVAL;
    }
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
    for (int again = 0;; again = 1) {
        struct mn_wm_out o = {.buf = rec, .cap = MN_REC_MAX};
        NTSTATUS_T s = mn_wm_pack_call(c, params, &o);
        if (s) return (int32_t)s;
        int submit = c->mode == MN_WM_SUBMIT;
        if (!o.sync && !submit) {
            int r = mn_queue(MN_OP_D3D + a->id, rec, o.len);
            if (r) mn_wm_unwind(&o, 0);
            return r;
        }
        uint32_t n = 0;
        int32_t r = mn_call_flush((MN_OP_D3D + a->id) | (submit ? MN_REC_SUBMIT : 0), rec, o.len, reply,
                                  MN_REPLY_BYTES, &n);
        if (r) {
            mn_wm_unwind(&o, r != -EIO && r != -ENODEV); /* a status from the host: it had the record */
            if (r != MN_D3D_NEED_ARENA) return r;
            uint64_t want = 0;
            if (n >= sizeof want) memcpy(&want, reply, sizeof want);
            if (again || add_arena(want)) return E_OUTOFMEMORY_; /* the guest's memory, once */
            continue;
        }
        if (!o.sync) return 0;
        s = mn_wm_unpack_call(c, params, &o, reply, n);
        if (!s && a->id == MN_D3D_ID_ID3D12Device_CreateFence) { /* the fence's mirror slot: our address of it */
            mn_d3d_p_ID3D12Device_CreateFence *f = params;
            f->aux[1] = events_ready && f->aux[0] < mirror_blob.size / 8 ? (uintptr_t)((uint64_t *)mirror_blob.ptr + f->aux[0]) : 0;
        }
        if (!s && a->id == MN_D3D_ID_ID3D12Resource_Map) { /* the arena's (blob, offset): our address of it */
            uint64_t *at = (uint64_t *)(uintptr_t)((mn_d3d_p_ID3D12Resource_Map *)params)->data;
            if (at) at[0] = (uintptr_t)arena_ptr(at[0], at[1]);
        }
        return (int32_t)s;
    }
}
