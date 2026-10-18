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
void mn_d3d_identity_add(uint32_t ctx_id, void *obj, uint64_t handle), mn_d3d_identity_drop(void *obj);
NTSTATUS_T mn_d3d_map_stub(void *params);
NTSTATUS_T mn_d3d_create_fence_stub(void *params), mn_d3d_fence_signal_stub(void *params),
    mn_d3d_queue_signal_stub(void *params), mn_d3d_set_event_stub(void *params),
    mn_d3d_set_event_multi_stub(void *params), mn_d3d_enqueue_resident_stub(void *params),
    mn_d3d_check_feature_stub(void *params), mn_d3d_adapter_desc_stub(void *params),
    mn_d3d_adapter_desc1_stub(void *params), mn_d3d_adapter_desc2_stub(void *params),
    mn_d3d_adapter_desc3_stub(void *params), mn_d3d_output_desc_stub(void *params),
    mn_d3d_output_desc1_stub(void *params);
int mn_d3d_events_attach(struct mn_wm_ctx *c, uint64_t queue_blob, uint64_t mirror_blob, uint64_t mirror_len);
void mn_d3d_events_end(uint32_t ctx_id);
void mn_d3d_fence_gone(void *obj);
static void release(void *obj), present(void *cmdbuf, void *drawable);

static char *dir, *trace;
static _Atomic uint64_t counts[MN_D3D_CALLS]; /* with a trace: each method's records, all contexts' */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int loaded;
static _Thread_local struct { struct mn_wm_ctx *c; const struct mn_wm_call *call; int sync; } cur;
static _Atomic uint64_t said[(MN_D3D_CALLS + 63) / 64]; /* the methods without host code said so once */

void mn_d3d_set_dir(const char *d) {
    free(dir);
    dir = d ? strdup(d) : NULL;
}

void mn_d3d_set_trace(const char *path) {
    free(trace);
    trace = path ? strdup(path) : NULL;
}

static void trace_out(uint32_t ctx_id) {
    FILE *f = trace ? fopen(trace, "a") : NULL;
    if (!f) return;
    fprintf(f, "# context %u\n", ctx_id);
    for (unsigned i = 0; i < MN_D3D_CALLS; i++) {
        uint64_t n = atomic_exchange(&counts[i], 0);
        if (n) fprintf(f, "%u %s %llu\n", i, mn_d3d_calls[i].name, (unsigned long long)n);
    }
    fclose(f);
}

int mn_d3d_load(const char *d) {
    static const char *const names[3] = {"d3d12", "dxgi", "winemetal"};
    void *libs[3];
    char path[4096];
    for (int i = 0; i < 3; i++) {
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
    void (*hooks)(void *(*)(uint64_t, void *), void (*)(void *, void *), void *) =
        (void (*)(void *(*)(uint64_t, void *), void (*)(void *, void *), void *))dlsym(libs[2], "WMTSetMemoryHooks");
    if (!hooks) {
        fprintf(stderr, "mn-d3d: error: native DXMT: %s\n", dlerror());
        return -1;
    }
    mn_d3d_memory_hooks(hooks);
    void (*present_hook)(void (*)(void *, void *)) =
        (void (*)(void (*)(void *, void *)))dlsym(libs[2], "WMTSetPresentHook");
    if (!present_hook) {
        fprintf(stderr, "mn-d3d: error: native DXMT: %s\n", dlerror());
        return -1;
    }
    present_hook(present);
    void (*wsi)(const void *) = (void (*)(const void *))dlsym(libs[2], "WMTSetWsiHooks");
    if (!wsi) {
        fprintf(stderr, "mn-d3d: error: native DXMT: %s\n", dlerror());
        return -1;
    }
    mn_d3d_wsi_hooks(wsi); /* window.c: native DXMT's window system, from the guest's ops 7 and 8 */
    mn_d3d_stubs[MN_D3D_ID_ID3D12Resource_Map] = mn_d3d_map_stub; /* rules.txt's own */
    mn_d3d_stubs[MN_D3D_ID_ID3D12Device_CreateFence] = mn_d3d_create_fence_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12Fence_Signal] = mn_d3d_fence_signal_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12CommandQueue_Signal] = mn_d3d_queue_signal_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12Fence_SetEventOnCompletion] = mn_d3d_set_event_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12Device1_SetEventOnMultipleFenceCompletion] = mn_d3d_set_event_multi_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12Device3_EnqueueMakeResident] = mn_d3d_enqueue_resident_stub;
    mn_d3d_stubs[MN_D3D_ID_ID3D12Device_CheckFeatureSupport] = mn_d3d_check_feature_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIAdapter_GetDesc] = mn_d3d_adapter_desc_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIAdapter1_GetDesc1] = mn_d3d_adapter_desc1_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIAdapter2_GetDesc2] = mn_d3d_adapter_desc2_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIAdapter4_GetDesc3] = mn_d3d_adapter_desc3_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIOutput_GetDesc] = mn_d3d_output_desc_stub;
    mn_d3d_stubs[MN_D3D_ID_IDXGIOutput6_GetDesc1] = mn_d3d_output_desc1_stub;
    mn_wm_d3d_release = release;
    return 0;
}

/* A handle's object goes: a fence forgets its mirror slot first. */
static void release(void *obj) {
    mn_d3d_identity_drop(obj);
    mn_d3d_fence_gone(obj);
    mn_d3d_native_release(obj);
}

uint32_t mn_d3d_current_ctx(void) { return cur.c ? cur.c->ctx_id : 0; }

/* Native DXMT's presents (fork 0081's hook), counted and probed as guest DXMT's are (present.m). */
static void present(void *cmdbuf, void *drawable) { mn_wm_present_native(cur.c, cmdbuf, drawable); }

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
        } else if (hr >= 0) {
            mn_d3d_identity_add(c->ctx_id, out, q.out); /* its release drops a binding too */
        }
        memcpy(reply, &hr, sizeof hr);
        *n = sizeof hr;
        return 0;
    }
    case MN_D3D_OP_NOTIFY: {
        struct mn_d3d_notify a;
        if (len < sizeof a) return MN_EINVAL;
        memcpy(&a, p, sizeof a);
        if (!mn_d3d_events_attach(c, a.queue_blob, a.mirror_blob, a.mirror_len)) return 0;
        fprintf(stderr, "mn-d3d: error: a notify queue or mirror that isn't guest memory\n");
        return MN_EINVAL;
    }
    case MN_D3D_OP_ARENA: {
        struct mn_d3d_arena a;
        if (len < sizeof a) return MN_EINVAL;
        memcpy(&a, p, sizeof a);
        if (!mn_d3d_arena_add(c->ctx_id, a.blob_id, a.offset, a.len)) return 0;
        fprintf(stderr, "mn-d3d: error: an arena that isn't guest memory (%llu, %llu, %llu)\n",
                (unsigned long long)a.blob_id, (unsigned long long)a.offset, (unsigned long long)a.len);
        return MN_EINVAL;
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
        if (!bytes) {
            fprintf(stderr, "mn-d3d: error: a blob's bytes asked of an object that isn't one\n");
            return (int32_t)MN_STATUS_INVALID_HANDLE;
        }
        uint64_t take = b.offset < size ? size - b.offset : 0;
        if (take > b.len) take = b.len;
        if (take > cap - 8) take = cap - 8;
        memcpy(reply, &size, 8);
        if (take) memcpy((uint8_t *)reply + 8, bytes + b.offset, take);
        *n = (uint32_t)(8 + take);
        return 0;
    }
    case MN_D3D_OP_WINDOW: {
        struct mn_d3d_window w;
        if (len < sizeof w) return MN_EINVAL;
        memcpy(&w, p, sizeof w);
        return mn_d3d_window_set(c->ctx_id, &w) ? MN_D3D_E_FAIL : 0;
    }
    case MN_D3D_OP_DISPLAYS:
        if (!mn_d3d_displays_set(c->ctx_id, p, len)) return 0;
        fprintf(stderr, "mn-d3d: error: displays that don't fit their record (%u bytes)\n", len);
        return MN_EINVAL;
    default:
        return MN_ENOSYS; /* ops 3 and 6: the guest's own, never sent */
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
    if (trace) atomic_fetch_add_explicit(&counts[id], 1, memory_order_relaxed);
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
    int creation = 0;
    for (unsigned i = 0; i < call->nfields && !creation; i++) creation = call->fields[i].kind == WMF_HANDLE_OUT;
    __typeof__(cur) outer = cur; /* a stub doesn't call back into the dispatcher, but keep it right if one did */
    cur.c = c, cur.call = call, cur.sync = reply != NULL;
    mn_d3d_memory_begin(c->ctx_id, creation && reply);
    int32_t s = mn_wm_host_call_table(c, ring, call, stub, p, len, reply, cap, n);
    uint64_t need = mn_d3d_memory_end();
    cur = outer;
    if (!s && !need && reply && creation)
        for (unsigned i = 0; i < call->nfields; i++) { /* the handles it bound, for identity */
            mn_handle h;
            int ok;
            if (call->fields[i].kind != WMF_HANDLE_OUT) continue;
            memcpy(&h, (uint8_t *)reply + call->fields[i].offset, sizeof h);
            void *obj = h ? mn_ht_get(c->ht, h, MN_HK_D3D, &ok) : NULL;
            if (obj) mn_d3d_identity_add(c->ctx_id, obj, h);
        }
    if (need && !s && cap >= sizeof need) { /* made in host memory for want of an arena: let it go, the guest adds one
                                              * and asks again */
        for (unsigned i = 0; i < call->nfields; i++) {
            mn_handle h;
            if (call->fields[i].kind != WMF_HANDLE_OUT) continue;
            memcpy(&h, (uint8_t *)reply + call->fields[i].offset, sizeof h);
            if (h) mn_ht_release(c->ht, ring, h);
        }
        memcpy(reply, &need, sizeof need);
        *n = sizeof need;
        return MN_D3D_NEED_ARENA;
    }
    return s;
}

int32_t mn_vm_dispatch(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                       uint32_t cap, uint32_t *reply_len) {
    if (op < MN_OP_D3D) return mn_wm_dispatch(ctx_id, ring_state, op, p, len, reply, cap, reply_len);
    return d3d(ctx_id, ring_state, op - MN_OP_D3D, p, len, reply, cap, reply_len);
}

static void progress(uint32_t ctx_id, void *ring_state) { mn_wm_hooks.progress(ctx_id, ring_state); }
static void ring_end(uint32_t ctx_id, void *ring_state) { mn_wm_hooks.ring_end(ctx_id, ring_state); }
static void context_end(uint32_t ctx_id) {
    mn_wm_hooks.context_end(ctx_id); /* its objects released: their pieces come back as Metal lets them go */
    mn_d3d_arenas_end(ctx_id);
    mn_d3d_events_end(ctx_id);
    mn_d3d_windows_end(ctx_id);
    trace_out(ctx_id);
}
const struct mn_executor_hooks mn_vm_hooks = {progress, context_end, ring_end};

int32_t mn_vm_dispatch_test(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len, void *reply,
                            uint32_t cap, uint32_t *reply_len) {
    if (op < MN_OP_D3D) return mn_wm_dispatch_test(ctx_id, ring_state, op, p, len, reply, cap, reply_len);
    return d3d(ctx_id, ring_state, op - MN_OP_D3D, p, len, reply, cap, reply_len);
}
