/* The guest's winemetal.so (milestone 4b-1): winemetal's Unix-call table, each slot forwarded over the Metal transport
 * (one ring per thread) to the VM host, which runs MacNeutron's own winemetal.so. x86-64 Linux, under FEX; linked
 * against Wine's ntdll.so. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../guest/transport.h"
#include "../marshal.h"
#include "../notify.h"
#include "galloc.h"
#include "winemetal_thunks.h"

struct buffers { uint8_t rec[MN_REC_MAX]; uint8_t reply[MN_REPLY_BYTES]; };
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static _Atomic uint64_t refused_logged[(MN_WM_SLOTS + 63) / 64];

static void make_key(void) { pthread_key_create(&key, free); }

extern unsigned int NtSetEvent(void *handle, void *prev_state); /* Wine's ntdll.so */

/* Slot 109 natively runs the listener until slot 110: here the host runs it, and this thread sets the Win32 events
 * it posts, sleeping on the queue between them. */
static void listener_loop(mn_handle h) {
    uint64_t blob;
    struct mn_notify *q = mn_wm_listener_queue(h, &blob);
    if (!q) return;
    for (;;) {
        struct mn_notify_rec r;
        while (mn_notify_next(q, &r)) {
            if (r.type == MN_NOTIFY_STOP) return;
            if (r.type == MN_NOTIFY_EVENT) NtSetEvent((void *)(uintptr_t)r.value, NULL);
        }
        mn_notify_wait(blob, atomic_load_explicit(&q->tail, memory_order_relaxed), 1000);
    }
}

static struct buffers *buffers(void) {
    pthread_once(&once, make_key);
    struct buffers *b = pthread_getspecific(key);
    if (!b && (b = malloc(sizeof *b))) pthread_setspecific(key, b);
    return b;
}

static NTSTATUS_T forward(unsigned slot, void *params) {
    struct buffers *b = buffers();
    if (!b || mn_open()) return MN_STATUS_UNSUCCESSFUL;
    struct mn_wm_out out = {.buf = b->rec, .cap = sizeof b->rec};
    NTSTATUS_T s = mn_wm_pack(slot, params, &out);
    if (s) {
        if (mn_wm_calls[slot].mode == MN_WM_REFUSED &&
            !(atomic_fetch_or(&refused_logged[slot / 64], 1ull << (slot % 64)) & (1ull << (slot % 64))))
            fprintf(stderr, "mn-winemetal: %s (slot %u) is refused: 0x%x\n", mn_wm_calls[slot].name, slot, s);
        return s;
    }
    if (!out.sync) {
        if (!mn_send(MN_OP_WM + slot, out.buf, out.len)) {
            if (slot == 109) listener_loop(((struct unixcall_generic_obj_noret *)params)->handle);
            return 0;
        }
        mn_wm_unwind(&out, 0);
        return MN_STATUS_UNSUCCESSFUL;
    }
    uint32_t len = 0;
    int r = mn_call(MN_OP_WM + slot, out.buf, out.len, b->reply, sizeof b->reply, &len);
    if (r) {
        int transport = r > -4096 && r < 0; /* else the host's status: it saw the record */
        mn_wm_unwind(&out, !transport);
        return transport ? MN_STATUS_UNSUCCESSFUL : (NTSTATUS_T)r;
    }
    return mn_wm_unpack(slot, params, &out, b->reply, len);
}

/* A texture's pixel format, for the extent of its data in slots 45 and 145: asked of the host once (slot 39), then
 * kept with the handle. */
static uint32_t texture_format(mn_handle t) {
    uint64_t att = mn_gh_attached(t);
    if (MN_GH_TAG(att) == MN_GH_FORMAT) return (uint32_t)MN_GH_VALUE(att);
    struct unixcall_generic_obj_uint64_ret q = {t, 0}, r = {0, 0};
    uint32_t n = 0;
    if (mn_call(MN_OP_WM + 39, &q, sizeof q, &r, sizeof r, &n) || n < sizeof r || !r.ret) return 0;
    mn_gh_attach(t, MN_GH_FORMAT | r.ret);
    return (uint32_t)r.ret;
}

/* A child after fork opens its own context on its next call (Wine forks to start processes, which then exec). */
static void forget(void) {
    mn_forget();
    mn_galloc_forget();
    mn_wm_forget();
}

__attribute__((constructor)) static void init(void) {
    mn_wm_texture_format = texture_format;
    pthread_atfork(NULL, NULL, forget);
}

/* The context's live handles, deferred releases and failed queued calls (the gates' check). */
int mn_wm_stats(struct mn_wm_stats *st) {
    if (mn_open()) return -1;
    uint32_t len = 0;
    int r = mn_call(MN_OP_WM + MN_WM_STATS, NULL, 0, st, sizeof *st, &len);
    return r || len != sizeof *st ? -1 : 0;
}

#define STUB(n) static NTSTATUS_T slot##n(void *p) { return forward(n, p); }
#define STUB10(t) STUB(t##0) STUB(t##1) STUB(t##2) STUB(t##3) STUB(t##4) STUB(t##5) STUB(t##6) STUB(t##7) STUB(t##8) STUB(t##9)
#define SLOT10(t) slot##t##0, slot##t##1, slot##t##2, slot##t##3, slot##t##4, slot##t##5, slot##t##6, slot##t##7, \
                  slot##t##8, slot##t##9,
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7) STUB(8) STUB(9)
STUB10(1) STUB10(2) STUB10(3) STUB10(4) STUB10(5) STUB10(6) STUB10(7) STUB10(8) STUB10(9) STUB10(10) STUB10(11)
STUB10(12) STUB10(13) STUB10(14) STUB(150) STUB(151) STUB(152) STUB(156) STUB(157)

/* DXMT 0071's WMTMemoryAlloc and WMTMemoryFree: memory DXMT makes Metal buffers over, from the blob allocator, so the
 * host maps it too. A freed allocation comes back once every buffer over it is gone (galloc.h). */
static NTSTATUS_T slot153(void *p) {
    struct unixcall_wmtmemory *m = p;
    struct mn_region r;
    m->ptr.ptr = NULL;
    if (mn_open() || mn_galloc(m->length, &r)) return MN_STATUS_NO_MEMORY;
    m->ptr.ptr = r.ptr;
    return 0;
}

static NTSTATUS_T slot154(void *p) {
    struct unixcall_wmtmemory *m = p;
    uint64_t id, blob, off;
    if (!m->ptr.ptr) return 0;
    if (mn_glookup(m->ptr.ptr, 1, &id, &blob, &off) || mn_gptr(id) != m->ptr.ptr) return MN_STATUS_INVALID_PARAMETER;
    mn_gfree(id);
    return 0;
}

/* Direct3D forwarding (M2): mnd3d.dll's calls, through winemetal.dll's WMTForward (DXMT 0079); eac-vm/d3d/guest/unix.c
 * has them (a build without it, the tests', refuses). */
__attribute__((weak)) int32_t mn_d3d_unix_call(void *args) {
    (void)args;
    return (int32_t)MN_STATUS_NOT_IMPLEMENTED;
}
static NTSTATUS_T d3d(void *args) { return (NTSTATUS_T)mn_d3d_unix_call(args); }

typedef NTSTATUS_T (*unixlib_entry_t)(void *params);
const unixlib_entry_t __wine_unix_call_funcs[MN_WM_SLOTS] = {
    slot0, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9,
    SLOT10(1) SLOT10(2) SLOT10(3) SLOT10(4) SLOT10(5) SLOT10(6) SLOT10(7) SLOT10(8) SLOT10(9) SLOT10(10) SLOT10(11)
    SLOT10(12) SLOT10(13) SLOT10(14) slot150, slot151, slot152, slot153, slot154, d3d, slot156, slot157,
};
