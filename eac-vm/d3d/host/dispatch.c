/* Direct3D forwarding's dispatcher (see dispatch.h). A record runs on the thread that drains its ring (a vCPU's): the
 * call running is that thread's, for the stubs' results. */
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "marshal.h"
#include "host/dispatch.h"
#include "mnd3d_params.h"
#include "../mn_d3d.h"
#include "dispatch.h"

extern const struct mn_wm_call mn_d3d_calls[MN_D3D_CALLS];
extern NTSTATUS_T (*mn_d3d_stubs[MN_D3D_CALLS])(void *);
struct mn_d3d_export { const char *dll, *name; };
extern const struct mn_d3d_export mn_d3d_export_names[MN_D3D_EXPORTS];
extern void *mn_d3d_exports[MN_D3D_EXPORTS];
/* native.cpp */
void mn_d3d_native_release(void *obj);
int32_t mn_d3d_native_query(void *obj, const uint8_t iid[16], void **out);
const void *mn_d3d_native_blob(void *obj, uint64_t *size);

static char *dir;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int loaded;
static _Thread_local struct { struct mn_wm_ctx *c; const struct mn_wm_call *call; int sync; } cur;
static _Atomic uint64_t said[(MN_D3D_CALLS + 63) / 64]; /* the methods without host code said so once */

void mn_d3d_set_dir(const char *d) {
    free(dir);
    dir = d ? strdup(d) : NULL;
}

int mn_d3d_load(const char *d) {
    static const char *const names[2] = {"d3d12", "dxgi"};
    void *libs[2];
    char path[4096];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s.dylib", d, names[i]);
        if (!(libs[i] = dlopen(path, RTLD_NOW | RTLD_LOCAL))) {
            fprintf(stderr, "mn-d3d: error: native DXMT: %s\n", dlerror());
            return -1;
        }
    }
    for (int k = 0; k < MN_D3D_EXPORTS; k++) {
        void *lib = libs[strcmp(mn_d3d_export_names[k].dll, "d3d12") ? 1 : 0];
        if (!(mn_d3d_exports[k] = dlsym(lib, mn_d3d_export_names[k].name))) {
            fprintf(stderr, "mn-d3d: error: native DXMT: %s\n", dlerror());
            return -1;
        }
    }
    mn_wm_d3d_release = mn_d3d_native_release;
    return 0;
}

/* ponytail: the first Direct3D record loads native DXMT on its vCPU (dlopen and Metal's device: tens of ms, once) */
static void load_once(void) {
    if (!dir) fprintf(stderr, "mn-d3d: error: native DXMT: no directory set\n");
    loaded = dir && !mn_d3d_load(dir);
}

void mn_d3d_result(int32_t hr) {
    uint32_t u = (uint32_t)hr, none = 0;
    if (!cur.c) return;
    if (u == 0x887A0005u || u == 0x887A0006u || u == 0x887A0007u) { /* DXGI_ERROR_DEVICE_REMOVED, _HUNG, _RESET */
        if (atomic_compare_exchange_strong(&cur.c->d3d_removed, &none, u))
            fprintf(stderr, "mn-d3d: error: the device was removed (0x%08x), first by %s\n", u, cur.call->name);
    } else if (hr < 0 && !cur.sync) {
        fprintf(stderr, "mn-d3d: error: %s 0x%08x\n", cur.call->name, u);
    }
}

/* The family's own ops (mn_d3d.h). */
static int32_t family(struct mn_wm_ctx *c, void *ring, uint32_t id, const void *p, uint32_t len, void *reply,
                      uint32_t cap, uint32_t *n) {
    switch (id) {
    case MN_D3D_OP_RELEASE: {
        mn_handle h;
        if (len < sizeof h) return MN_EINVAL;
        memcpy(&h, p, sizeof h);
        if (!mn_ht_release(c->ht, ring, h)) return 0;
        fprintf(stderr, "mn-d3d: error: release of a handle not bound (0x%llx)\n", (unsigned long long)h);
        return MN_EINVAL;
    }
    case MN_D3D_OP_QUERY: {
        struct mn_d3d_query q;
        if (len < sizeof q || !reply || cap < sizeof(int32_t)) return MN_EINVAL;
        memcpy(&q, p, sizeof q);
        int ok;
        void *obj = mn_ht_get(c->ht, q.handle, MN_HK_D3D, &ok), *out = NULL;
        if (!ok || !obj) return (int32_t)MN_STATUS_INVALID_HANDLE;
        int32_t hr = mn_d3d_native_query(obj, q.iid, &out);
        if (hr >= 0 && mn_ht_bind(c->ht, ring, q.out, MN_HK_D3D, out, MN_OWN_PLUS1)) {
            mn_d3d_native_release(out);
            hr = MN_D3D_E_FAIL;
        }
        memcpy(reply, &hr, sizeof hr);
        *n = sizeof hr;
        return 0;
    }
    case MN_D3D_OP_BLOB: {
        struct mn_d3d_blob b;
        if (len < sizeof b || !reply || cap < 8) return MN_EINVAL;
        memcpy(&b, p, sizeof b);
        int ok;
        void *obj = mn_ht_get(c->ht, b.handle, MN_HK_D3D, &ok);
        if (!ok || !obj) return (int32_t)MN_STATUS_INVALID_HANDLE;
        uint64_t size;
        const uint8_t *bytes = mn_d3d_native_blob(obj, &size);
        uint64_t take = b.offset < size ? size - b.offset : 0;
        if (take > b.len) take = b.len;
        if (take > cap - 8) take = cap - 8;
        memcpy(reply, &size, 8);
        if (take) memcpy((uint8_t *)reply + 8, bytes + b.offset, take);
        *n = (uint32_t)(8 + take);
        return 0;
    }
    default:
        return MN_ENOSYS; /* ops 1-3: Tasks 7, 8, 11 */
    }
}

static int32_t d3d(uint32_t ctx_id, void *ring, uint32_t id, const void *p, uint32_t len, void *reply, uint32_t cap,
                   uint32_t *n) {
    struct mn_wm_ctx *c = mn_wm_context(ctx_id, ring);
    if (!c) return MN_D3D_E_FAIL;
    if (id < MN_D3D_FIRST) return family(c, ring, id, p, len, reply, cap, n);
    if (id >= MN_D3D_CALLS || !mn_d3d_calls[id].name) {
        fprintf(stderr, "mn-d3d: error: no method %u\n", id);
        return MN_EINVAL;
    }
    const struct mn_wm_call *call = &mn_d3d_calls[id];
    if (len < call->params_size) {
        fprintf(stderr, "mn-d3d: error: %s: a record of %u bytes, its params %u\n", call->name, len,
                call->params_size);
        return MN_EINVAL;
    }
    pthread_once(&once, load_once);
    if (!loaded) return MN_D3D_E_FAIL;
    uint32_t removed = atomic_load(&c->d3d_removed);
    if (removed) return MN_D3D_DEVICE_REMOVED;
    NTSTATUS_T (*stub)(void *) = mn_d3d_stubs[id];
    if (!stub) {
        if (!(atomic_fetch_or(&said[id / 64], 1ull << (id % 64)) & (1ull << (id % 64))))
            fprintf(stderr, "mn-d3d: error: %s has no host code\n", call->name);
        return MN_D3D_E_NOTIMPL;
    }
    __typeof__(cur) outer = cur; /* a stub doesn't call back into the dispatcher, but keep it right if one did */
    cur.c = c, cur.call = call, cur.sync = reply != NULL;
    int32_t s = mn_wm_host_call_table(c, ring, call, stub, p, len, reply, cap, n);
    cur = outer;
    return s;
}

int32_t mn_vm_dispatch(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len) {
    if (op < MN_OP_D3D) return mn_wm_dispatch(ctx_id, ring_state, op, p, len, reply, cap, reply_len);
    return d3d(ctx_id, ring_state, op - MN_OP_D3D, p, len, reply, cap, reply_len);
}

int32_t mn_vm_dispatch_test(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                            uint32_t cap, uint32_t *reply_len) {
    if (op < MN_OP_D3D) return mn_wm_dispatch_test(ctx_id, ring_state, op, p, len, reply, cap, reply_len);
    return d3d(ctx_id, ring_state, op - MN_OP_D3D, p, len, reply, cap, reply_len);
}
