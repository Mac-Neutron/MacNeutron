/* The guest's unix side of Direct3D forwarding (M2 Task 6): slot MN_WM_D3D of the guest winemetal.so, which mnd3d.dll
 * reaches through winemetal's WMTForward (and the loopback library calls directly). A method's params are packed with
 * the generated tables (the proxies' addresses become their handles in the record), queued on the thread's drained
 * ring if asynchronous, else sent with a flush whose reply is there when it returns; a queue operation's flush first
 * runs every other ring of the context (MN_REC_SUBMIT). */
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "marshal.h"
#include "../../metal/guest/transport.h"
#include "mnd3d_params.h"
#include "../mn_d3d.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];

static pthread_once_t once = PTHREAD_ONCE_INIT;
static int ready; /* 0, or the transport's -errno */
static _Thread_local uint8_t *rec, *reply;

static int proxy_handle(uint64_t value, mn_handle *h) {
    const struct mn_d3d_proxy_hdr *p = (const void *)(uintptr_t)value;
    if (!p) return *h = 0, 0;
    if (p->magic != MN_D3D_PROXY_MAGIC) return -1;
    *h = p->handle;
    return 0;
}

static void init(void) {
    mn_wm_guest_handle = proxy_handle;
    ready = mn_open();
    if (!ready) ready = mn_drained();
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
    const struct mn_wm_call *c = &mn_d3d_calls[a->id];
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
    int32_t r = mn_call_flush((MN_OP_D3D + a->id) | (submit ? MN_REC_SUBMIT : 0), rec, o.len, reply, MN_REPLY_BYTES,
                              &n);
    if (r) {
        mn_wm_unwind(&o, r != -EIO && r != -ENODEV); /* a status from the host: it had the record */
        return r;
    }
    if (!o.sync) return 0;
    return (int32_t)mn_wm_unpack_call(c, params, &o, reply, n);
}
