// Proxy objects (see proxy.h). An object counts its references in the guest; its last Release lets the host's go (the
// family's op 0). QueryInterface is answered from what the host said at the creation: an interface at the same pointer
// is another proxy of the same object, one at another pointer (a companion) a new handle from the host (op 4).
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "proxy.h"
#include "formats.h"
#ifndef MN_D3D_LOOPBACK
#include "wsi_window.hpp" // DXMT's own Win32 window code, built in (build-guest.sh): the window's changes, made here
#endif

struct pacing;

struct mn_obj {
    std::atomic<long> refs{1};
    uint64_t handle = 0;
    int created = 0; // the interface it was made as: its IUnknown
    uint64_t answers[MN_D3D_MASK_WORDS] = {}, moved[MN_D3D_MASK_WORDS] = {};
    std::mutex m;
    mn_proxy *proxies[MN_D3D_IFACES] = {};
    std::vector<uint8_t> blob; // an ID3D10Blob's bytes, read once
    bool blob_read = false;
    std::atomic<uint64_t> *mirror = nullptr; // a fence's completed value, which the host raises (Task 8)
    HWND window = nullptr;                   // a swapchain's (M3 Task 2), and its state as last pushed
    mn_d3d_window pushed = {};
#ifndef MN_D3D_LOOPBACK
    dxmt::wsi::DXMTWindowState saved;        // as guest DXMT's swapchain keeps it (Task 3)
#endif
    DXGI_RGBA background = {0, 0, 0, 1};     // the swapchain's own answers (Task 3)
    UINT source_width = 0, source_height = 0;
    pacing *pace = nullptr;                  // a swapchain's frames (Task 4)
    std::vector<std::pair<int, mn_obj *>> companions; // one an interface at another pointer, held for our life (M3)
    uint64_t root = 0; // a companion's: the object it came from, whose IUnknown is its IUnknown too (COM's identity, M4)
    std::atomic<const void *> recorder{nullptr};      // a command list's: the thread that called it last (Task 10)
    mn_res res;                                       // a D3D11 or D3D10 resource's description (M4), under m
    std::recursive_mutex mt;                          // an ID3D11Multithread's Enter and Leave (M4)
    std::atomic<UINT> latency{0};                     // an IDXGIDevice1's SetMaximumFrameLatency (M4; 0: DXGI's 3)
};
static void end_pacing(pacing *p);

// The objects by handle, for identity (mn_proxy_out); an object leaves at its last Release, under the same lock.
static std::mutex objs_lock;
static std::unordered_map<uint64_t, mn_obj *> objs;

static const IID unknown_iid = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static bool has(const uint64_t mask[2], int k) { return k >= 0 && (mask[k / 64] >> (k % 64) & 1); }

// The vtable a proxy of k has (M3 Task 10, SMITE 2's): the most derived interface whose bases include k that the object
// answers at its own pointer. DXGI's and D3D12's objects have one vtable for all their versions, and programs count on
// it: Unreal calls IDXGIAdapter3::QueryVideoMemoryInfo through the IDXGIAdapter that EnumAdapters gives it.
static int widest(const mn_obj *o, int k) {
    int best = k, depth = 0;
    for (int j = 0; j < MN_D3D_IFACES; j++) {
        if (!has(o->answers, j) || has(o->moved, j)) continue;
        int d = 0, b = j;
        for (; b >= 0 && b != k; d++) b = mn_d3d_ifaces[b].base;
        if (b == k && d > depth) best = j, depth = d;
    }
    return best;
}

static mn_proxy *proxy_of(mn_obj *o, int k) {
    std::lock_guard<std::mutex> l(o->m);
    if (!o->proxies[k]) o->proxies[k] = new mn_proxy{mn_d3d_vtables[widest(o, k)], MN_D3D_PROXY_MAGIC, k, o->handle, o};
    return o->proxies[k];
}

// A new object for handle, made as k (-1: unknown, its first answer), answering answers (moved: at another pointer).
static mn_obj *object(uint64_t handle, int k, const uint64_t *answers, const uint64_t *moved) {
    auto *o = new mn_obj;
    o->handle = handle;
    if (answers) std::memcpy(o->answers, answers, sizeof o->answers);
    if (moved) std::memcpy(o->moved, moved, sizeof o->moved);
    for (int i = 0; i < MN_D3D_IFACES && k < 0; i++)
        if (has(o->answers, i)) k = i;
    if (k < 0) k = 0;
    o->created = k;
    o->answers[k / 64] |= 1ull << (k % 64); // it is what it was made as
    o->moved[k / 64] &= ~(1ull << (k % 64));
    std::lock_guard<std::mutex> l(objs_lock);
    objs[handle] = o;
    return o;
}

static_assert(2 * MN_D3D_MASK_WORDS + 1 == 9, "the params' <out>_ifaces[9]");

void *mn_proxy_out(uint64_t handle, int iface, const IID *riid, const uint64_t ifaces[9]) {
    int k = iface >= 0 ? iface : riid ? mn_d3d_iface_of(riid) : -1;
    mn_obj *have = nullptr;
    // The object we have, if it answers k at its own pointer (a companion QueryInterface made knows only that one).
    const uint64_t first = ifaces[2 * MN_D3D_MASK_WORDS];
    if (first && first != handle) {
        std::lock_guard<std::mutex> l(objs_lock);
        auto it = objs.find(first);
        mn_obj *o = it != objs.end() ? it->second : nullptr;
        if (o && o->refs > 0 && (k < 0 || (has(o->answers, k) && !has(o->moved, k)))) (have = o)->refs++;
    }
    if (have) { // its proxy, and the host lets the new handle go
        uint64_t h = handle;
        mn_d3d_call(MN_D3D_OP_RELEASE, &h);
        return proxy_of(have, k < 0 ? have->created : k);
    }
    mn_obj *o = object(handle, k, ifaces, ifaces + MN_D3D_MASK_WORDS);
    return proxy_of(o, o->created);
}

void *mn_proxy_known(uint64_t handle, int iface) {
    mn_obj *have = nullptr;
    if (handle) {
        std::lock_guard<std::mutex> l(objs_lock);
        auto it = objs.find(handle);
        if (it != objs.end() && it->second->refs > 0) (have = it->second)->refs++;
    }
    return have ? proxy_of(have, iface >= 0 && has(have->answers, iface) ? iface : have->created) : nullptr;
}

HRESULT mn_d3d_hr(int32_t status) {
    uint32_t u = (uint32_t)status;
    if (u >> 28 == 0x8) return (HRESULT)u;       // the host's refusal, an HRESULT already
    if (u >> 28 == 0xC) return E_INVALIDARG;     // the marshaller's: a handle or a record that didn't fit
    return E_FAIL;                               // MN_E*, or the transport's -errno
}

static std::atomic<uint64_t> said[(MN_D3D_CALLS + 63) / 64];

void mn_d3d_unhandled(uint32_t id, const char *name, int refused) {
    uint64_t bit = 1ull << (id % 64);
    if (said[id / 64].fetch_or(bit) & bit) return;
    fprintf(stderr, refused ? "mn-d3d: %s is refused\n" : "mn-d3d: %s has no guest code\n", name);
    uint32_t m[2] = {id, (uint32_t)refused};
    mn_d3d_call(MN_D3D_OP_TRACE, m); // for the guest's trace (coverage.py), if on
}

HRESULT STDMETHODCALLTYPE mn_px_QueryInterface(mn_proxy *This, REFIID riid, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    mn_obj *o = This->obj;
    bool unknown = !std::memcmp(&riid, &unknown_iid, sizeof riid);
    if (unknown && o->root && (*out = mn_proxy_known(o->root, -1))) return S_OK;
    int k = unknown ? o->created : mn_d3d_iface_of(&riid);
    if (!has(o->answers, k)) {
        static std::atomic<int> logged;
        if (logged++ < 32) {
            const uint8_t *b = (const uint8_t *)&riid;
            fprintf(stderr, "mn-d3d: %s: no interface %02x%02x%02x%02x-...\n", mn_d3d_ifaces[This->iface].name, b[3],
                    b[2], b[1], b[0]);
        }
        return E_NOINTERFACE;
    }
    if (!has(o->moved, k)) {
        o->refs++;
        *out = proxy_of(o, k);
        return S_OK;
    }
    // A companion: an object of its own, answering its whole family (M4: D3D11's device its DXGI device, which answers
    // IDXGIDevice1 too), the same each time (COM's identity: a caller may patch it, as a test of DXMT's own does),
    // which the object holds until its last release.
    mn_obj *have = nullptr;
    {
        std::lock_guard<std::mutex> l(o->m);
        for (auto &c : o->companions)
            if (has(c.second->answers, k) && !has(c.second->moved, k)) {
                (have = c.second)->refs++;
                break;
            }
    }
    if (have) {
        *out = proxy_of(have, k);
        return S_OK;
    }
    mn_d3d_query_args q = {};
    q.handle = o->handle;
    std::memcpy(q.iid, &riid, sizeof q.iid);
    int32_t s = mn_d3d_call(MN_D3D_OP_QUERY, &q);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)q.hr;
    if (FAILED(hr) || !q.out) return FAILED(hr) ? hr : E_NOINTERFACE;
    auto *p = (mn_proxy *)mn_proxy_out(q.out, k, nullptr, q.ifaces);
    *out = p;
    if (p->obj->handle != q.out) return S_OK; // one the guest has (a companion's device): not held, or neither goes
    // ponytail: a companion the guest got first from a getter (a new object) has no root: its IUnknown is its own
    p->obj->root = o->root ? o->root : o->handle;
    std::lock_guard<std::mutex> l(o->m);
    p->obj->refs++; // the object's hold
    o->companions.push_back({k, p->obj});
    return S_OK;
}

ULONG STDMETHODCALLTYPE mn_px_AddRef(mn_proxy *This) { return (ULONG)++This->obj->refs; }

ULONG STDMETHODCALLTYPE mn_px_Release(mn_proxy *This) {
    mn_obj *o = This->obj;
    long n;
    {
        std::lock_guard<std::mutex> l(objs_lock); // the last one leaves objs before mn_proxy_out can find it
        n = --o->refs;
        if (!n) objs.erase(o->handle);
    }
    if (n) return (ULONG)n;
    uint64_t h = o->handle;
    mn_d3d_call(MN_D3D_OP_RELEASE, &h);
    if (o->pace) end_pacing(o->pace);
    for (auto &c : o->companions) mn_px_Release(c.second->proxies[c.first]);
    for (mn_proxy *p : o->proxies) delete p;
    delete o;
    return 0;
}

// ---- specials ----

// An ID3D10Blob is read once (op 5), then answered in the guest.
static void read_blob(mn_obj *o) {
    std::lock_guard<std::mutex> l(o->m);
    if (o->blob_read) return;
    o->blob_read = true;
    mn_d3d_blob_args b = {o->handle, 0, 0, 0};
    if (mn_d3d_call(MN_D3D_OP_BLOB, &b)) return; // its size first
    o->blob.resize(b.size);
    b.dst = (uintptr_t)o->blob.data(), b.cap = b.size;
    if (b.size && mn_d3d_call(MN_D3D_OP_BLOB, &b)) o->blob.clear();
}
static LPVOID STDMETHODCALLTYPE blob_pointer(mn_proxy *This) {
    read_blob(This->obj);
    return This->obj->blob.empty() ? nullptr : This->obj->blob.data();
}
static SIZE_T STDMETHODCALLTYPE blob_size(mn_proxy *This) {
    read_blob(This->obj);
    return This->obj->blob.size();
}

// Map (rules.txt's own): the unix side answers our address of the host's memory (in an arena) in the first word.
static HRESULT STDMETHODCALLTYPE map(mn_proxy *This, UINT sub, const D3D12_RANGE *read_range, void **data) {
    mn_d3d_p_ID3D12Resource_Map p = {};
    uint64_t at[2] = {};
    p.self = (uintptr_t)This, p.sub_resource = sub, p.read_range = (uintptr_t)read_range;
    p.data = data ? (uintptr_t)at : 0;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Resource_Map, &p);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
    if (SUCCEEDED(hr) && data && !at[0]) hr = E_FAIL; /* not in an arena of ours: the host said so too */
    if (data) *data = SUCCEEDED(hr) ? (void *)(uintptr_t)at[0] : nullptr;
    return hr;
}

// SetName (a special): SetPrivateData(WKPDID_D3DDebugObjectNameW) of the name, as D3D12's is, in the guest's WCHARs.
static const GUID debug_name = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};
static HRESULT STDMETHODCALLTYPE set_name(mn_proxy *This, LPCWSTR name) {
    UINT size = name ? (UINT)(sizeof(WCHAR) * (wcslen(name) + 1)) : 0;
    return ((ID3D12Object *)This)->SetPrivateData(debug_name, size, name);
}

// ---- fences (rules.txt's own, Task 8) ----

static HRESULT STDMETHODCALLTYPE create_fence(mn_proxy *This, UINT64 initial, D3D12_FENCE_FLAGS flags, REFIID riid,
                                              void **fence) {
    mn_d3d_p_ID3D12Device_CreateFence p = {};
    p.self = (uintptr_t)This, p.initial_value = initial, p.flags = (uint32_t)flags, p.riid = (uintptr_t)&riid;
    p.fence = (uintptr_t)fence;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Device_CreateFence, &p);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
    if (!fence) return hr;
    *fence = SUCCEEDED(hr) && p.fence ? mn_proxy_out(p.fence, -1, &riid, p.fence_ifaces) : nullptr;
    if (*fence && p.aux[1]) ((mn_proxy *)*fence)->obj->mirror = (std::atomic<uint64_t> *)(uintptr_t)p.aux[1];
    return hr;
}

static UINT64 STDMETHODCALLTYPE completed_value(mn_proxy *This) {
    if (This->obj->mirror) return This->obj->mirror->load();
    mn_d3d_p_ID3D12Fence_GetCompletedValue p = {};
    p.self = (uintptr_t)This;
    return mn_d3d_call(MN_D3D_ID_ID3D12Fence_GetCompletedValue, &p) ? 0 : p.ret;
}

// A CPU signal: complete at once, here too, before the host hears of it; it sets the value, lower too (M3 Task 5).
static HRESULT STDMETHODCALLTYPE fence_signal(mn_proxy *This, UINT64 value) {
    if (This->obj->mirror) This->obj->mirror->store(value);
    mn_d3d_p_ID3D12Fence_Signal p = {};
    p.self = (uintptr_t)This, p.value = value;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Fence_Signal, &p);
    return s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
}

#ifndef MN_D3D_LOOPBACK
// The thread that sets the events the host posts (op 6): a Wine thread of its own, started with the first event a fence
// is given.
static DWORD WINAPI listener_main(void *) {
    mn_d3d_call(MN_D3D_OP_LISTEN, nullptr);
    return 0;
}
static BOOL CALLBACK start_listener(INIT_ONCE *, void *, void **) {
    if (HANDLE t = CreateThread(nullptr, 0, listener_main, nullptr, 0, nullptr)) CloseHandle(t);
    return TRUE;
}
#endif

// The listener, started once (the loopback's unix side starts its own).
static void listen() {
#ifndef MN_D3D_LOOPBACK
    static INIT_ONCE listening = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&listening, start_listener, nullptr, nullptr);
#endif
}

// The host's relay posts the event, which the unix side's listener sets. NULL: we wait here, never the host.
static HRESULT on_event(HANDLE event, HRESULT (*send)(void *, HANDLE), void *ctx) {
    listen();
    HANDLE own = event ? nullptr : CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE e = event ? event : own;
    if (!e) return E_OUTOFMEMORY;
    // The listener's own, which it lets go once set: the program may close its handle first, and Wine reuses the value.
#ifdef MN_D3D_LOOPBACK
    mn_win32_ref(e);
    HANDLE theirs = e;
#else
    HANDLE theirs = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), e, GetCurrentProcess(), &theirs, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        if (own) CloseHandle(own);
        return E_INVALIDARG;
    }
#endif
    HRESULT hr = send(ctx, theirs);
#ifdef MN_D3D_LOOPBACK
    if (FAILED(hr)) mn_win32_unref(theirs);
#else
    if (FAILED(hr)) CloseHandle(theirs);
#endif
    if (own) {
        if (SUCCEEDED(hr)) WaitForSingleObject(own, INFINITE);
        CloseHandle(own);
    }
    return hr;
}

struct on_completion { mn_proxy *fence; UINT64 value; };
static HRESULT send_completion(void *ctx, HANDLE e) {
    auto *c = (on_completion *)ctx;
    mn_d3d_p_ID3D12Fence_SetEventOnCompletion p = {};
    p.self = (uintptr_t)c->fence, p.value = c->value, p.event = (uintptr_t)e;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Fence_SetEventOnCompletion, &p);
    return s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
}
static HRESULT STDMETHODCALLTYPE set_event(mn_proxy *This, UINT64 value, HANDLE event) {
    if (This->obj->mirror && This->obj->mirror->load() >= value) { // already there: no round trip
        if (event) SetEvent(event);
        return S_OK;
    }
    on_completion c = {This, value};
    return on_event(event, send_completion, &c);
}

struct on_multiple {
    mn_proxy *device;
    ID3D12Fence *const *fences;
    const UINT64 *values;
    UINT count;
    D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags;
};
static HRESULT send_multiple(void *ctx, HANDLE e) {
    auto *c = (on_multiple *)ctx;
    mn_d3d_p_ID3D12Device1_SetEventOnMultipleFenceCompletion p = {};
    p.self = (uintptr_t)c->device, p.fences = (uintptr_t)c->fences, p.values = (uintptr_t)c->values;
    p.fence_count = c->count, p.flags = (uint32_t)c->flags, p.event = (uintptr_t)e;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Device1_SetEventOnMultipleFenceCompletion, &p);
    return s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
}
static HRESULT STDMETHODCALLTYPE set_event_multiple(mn_proxy *This, ID3D12Fence *const *fences, const UINT64 *values,
                                                    UINT count, D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE event) {
    on_multiple c = {This, fences, values, count, flags};
    return on_event(event, send_multiple, &c);
}

// D3D11's (M4 Task 6), as D3D12's: the fence's slot, its value read there, its events relayed; Flush1's event too.
static HRESULT STDMETHODCALLTYPE create_fence11(mn_proxy *This, UINT64 initial, D3D11_FENCE_FLAG flags, REFIID riid,
                                                void **fence) {
    mn_d3d_p_ID3D11Device5_CreateFence p = {};
    p.self = (uintptr_t)This, p.initial_value = initial, p.flags = (uint32_t)flags, p.iid = (uintptr_t)&riid;
    p.fence = (uintptr_t)fence;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D11Device5_CreateFence, &p);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
    if (!fence) return hr;
    *fence = SUCCEEDED(hr) && p.fence ? mn_proxy_out(p.fence, -1, &riid, p.fence_ifaces) : nullptr;
    if (*fence && p.aux[1]) ((mn_proxy *)*fence)->obj->mirror = (std::atomic<uint64_t> *)(uintptr_t)p.aux[1];
    return hr;
}

static UINT64 STDMETHODCALLTYPE completed_value11(mn_proxy *This) {
    if (This->obj->mirror) return This->obj->mirror->load();
    mn_d3d_p_ID3D11Fence_GetCompletedValue p = {};
    p.self = (uintptr_t)This;
    return mn_d3d_call(MN_D3D_ID_ID3D11Fence_GetCompletedValue, &p) ? 0 : p.ret;
}

static HRESULT send_completion11(void *ctx, HANDLE e) {
    auto *c = (on_completion *)ctx;
    mn_d3d_p_ID3D11Fence_SetEventOnCompletion p = {};
    p.self = (uintptr_t)c->fence, p.value = c->value, p.event = (uintptr_t)e;
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D11Fence_SetEventOnCompletion, &p);
    return s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
}
// (NULL, which D3D11 doesn't allow, is waited out here as D3D12's is: never DXMT's to take)
static HRESULT STDMETHODCALLTYPE set_event11(mn_proxy *This, UINT64 value, HANDLE event) {
    if (This->obj->mirror && This->obj->mirror->load() >= value) {
        if (event) SetEvent(event);
        return S_OK;
    }
    on_completion c = {This, value};
    return on_event(event, send_completion11, &c);
}

struct on_flush { mn_proxy *context; D3D11_CONTEXT_TYPE type; };
static HRESULT send_flush(void *ctx, HANDLE e) {
    auto *c = (on_flush *)ctx;
    mn_d3d_p_ID3D11DeviceContext3_Flush1 p = {};
    p.self = (uintptr_t)c->context, p.type = (uint32_t)c->type, p.event = (uintptr_t)e;
    int32_t s = mn_d3d_record(c->context, MN_D3D_ID_ID3D11DeviceContext3_Flush1, &p);
    return s ? mn_d3d_hr(s) : S_OK;
}
static void STDMETHODCALLTYPE flush1(mn_proxy *This, D3D11_CONTEXT_TYPE type, HANDLE event) {
    on_flush c = {This, type};
    if (event) on_event(event, send_flush, &c);
    else send_flush(&c, nullptr);
}

// ---- frame pacing (M3 Task 4): in the guest, so no present waits on the host's vCPU for the GPU or a drawable ----
// A swapchain's queue (M4: or a D3D11 device's immediate context) signals a fence of ours after each present. A swapchain without the frame latency object waits
// on it before presenting, to keep at most kInFlight frames in flight (DXGI's default latency; a D3D11 device's own,
// SetMaximumFrameLatency's); a waitable one's object
// is our semaphore, which a thread of ours releases once for each frame the fence passes. Every present also waits
// until the display has shown all but one of the drawables presented (the mirror's first slots, mn_d3d.h), so the
// host's nextDrawable finds one free.
static const UINT kInFlight = 3;

struct pacing {
    ID3D12CommandQueue *queue = nullptr; // the swapchain's, held
    ID3D12Fence *fence = nullptr;        // ours
    ID3D11DeviceContext4 *context = nullptr; // or a D3D11 device's immediate one, held (M4)
    ID3D11Fence *fence11 = nullptr;          // and ours on it
    mn_obj *device = nullptr;                // its IDXGIDevice1's object: SetMaximumFrameLatency's (held, by dxgi)
    IDXGIDevice1 *dxgi = nullptr;
    std::atomic<uint64_t> presented{0};
    UINT latency = kInFlight;            // a waitable swapchain's: DXMT's own, as GetMaximumFrameLatency answers
    HANDLE waitable = nullptr;           // the frame latency object (its first count: latency)
    HANDLE thread = nullptr, wake = nullptr;
    std::atomic<bool> stop{false};
    std::mutex m;
    std::atomic<uint64_t> *drawables = nullptr; // the context's presented and shown, raised by the host
};

static void listen();

// Set by the host's listener each time a drawable is shown (op 11): the process's one, for its life.
static HANDLE shown_event() {
    static HANDLE e = [] {
        listen();
        HANDLE h = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        uint64_t v = (uintptr_t)h;
        if (h && mn_d3d_call(MN_D3D_OP_SHOWN, &v)) CloseHandle(h), h = nullptr;
        return h;
    }();
    return e;
}

// Until fence passes v (a guest wait: the host is never asked to wait). D3D12's fence or D3D11's.
template <class F> static void wait_fence(F *fence, uint64_t v, HANDLE e = nullptr) {
    if (!v || fence->GetCompletedValue() >= v) return;
    static thread_local HANDLE own = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE w = e ? e : own;
    if (SUCCEEDED(fence->SetEventOnCompletion(v, w))) WaitForSingleObject(w, INFINITE);
}

// A wait for the pacing fence, and a frame's signal after its present: D3D12's queue or D3D11's context.
static void wait_frame(pacing *p, uint64_t v, HANDLE e = nullptr) {
    if (p->fence) wait_fence(p->fence, v, e);
    else wait_fence(p->fence11, v, e);
}
static void signal(pacing *p, uint64_t v) {
    if (p->queue) p->queue->Signal(p->fence, v);
    else p->context->Signal(p->fence11, v);
}

#ifndef MN_D3D_LOOPBACK
static uint64_t completed(pacing *p) { return p->fence ? p->fence->GetCompletedValue() : p->fence11->GetCompletedValue(); }

static DWORD WINAPI pacer(void *arg) {
    auto *p = (pacing *)arg;
    for (uint64_t released = 0; !p->stop;) {
        if (released + 1 > p->presented) {
            WaitForSingleObject(p->wake, INFINITE); // a present, or the end
            continue;
        }
        if (completed(p) < released + 1) {
            wait_frame(p, released + 1, p->wake);
            continue;
        }
        ReleaseSemaphore(p->waitable, 1, nullptr);
        released++;
    }
    return 0;
}
#endif

// D3D11's: the device's immediate context, a fence on it, and its IDXGIDevice1 (the device's latency); false when
// device isn't a D3D11 device that has fences (ID3D11Device5).
static bool d3d11_pacing(pacing *p, IUnknown *device) {
    ID3D11Device5 *d5 = nullptr;
    ID3D11DeviceContext *imm = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(ID3D11Device5), (void **)&d5))) return false;
    d5->GetImmediateContext(&imm);
    if (imm) imm->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&p->context), imm->Release();
    d5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&p->fence11);
    if (SUCCEEDED(d5->QueryInterface(__uuidof(IDXGIDevice1), (void **)&p->dxgi))) p->device = ((mn_proxy *)p->dxgi)->obj;
    d5->Release();
    return p->context && p->fence11;
}

// After a swapchain's creation on device (a D3D12 queue, or M4's D3D11 device): nullptr when it's neither.
static pacing *start_pacing(IUnknown *device, IUnknown *swapchain, UINT flags) {
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Device *dev = nullptr;
    if (!device) return nullptr;
    auto *p = new pacing;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), (void **)&queue))) {
        p->queue = queue;
        if (SUCCEEDED(queue->GetDevice(__uuidof(ID3D12Device), (void **)&dev))) {
            dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&p->fence);
            dev->Release();
        }
        if (!p->fence) return end_pacing(p), nullptr;
    } else if (!d3d11_pacing(p, device)) {
        return end_pacing(p), nullptr;
    }
    uint64_t at = 0;
    if (!mn_d3d_call(MN_D3D_OP_DRAWABLES, &at)) p->drawables = (std::atomic<uint64_t> *)(uintptr_t)at;
#ifndef MN_D3D_LOOPBACK // the loopback's programs make no swapchains
    if (flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
        IDXGISwapChain2 *sc2 = nullptr;
        if (SUCCEEDED(swapchain->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) {
            sc2->GetMaximumFrameLatency(&p->latency);
            sc2->Release();
        }
        p->waitable = CreateSemaphoreA(nullptr, (LONG)p->latency, DXGI_MAX_SWAP_CHAIN_BUFFERS, nullptr);
        p->wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        p->thread = p->waitable && p->wake ? CreateThread(nullptr, 0, pacer, p, 0, nullptr) : nullptr;
    }
#else
    (void)swapchain, (void)flags;
#endif
    return p;
}

static void end_pacing(pacing *p) {
    if (p->thread) {
        p->stop = true;
        SetEvent(p->wake);
        WaitForSingleObject(p->thread, INFINITE);
        CloseHandle(p->thread);
    }
    if (p->wake) CloseHandle(p->wake);
    if (p->waitable) CloseHandle(p->waitable);
    if (p->fence) p->fence->Release();
    if (p->queue) p->queue->Release();
    if (p->fence11) p->fence11->Release();
    if (p->context) p->context->Release();
    if (p->dxgi) p->dxgi->Release();
    delete p;
}

// Before a present: without the frame latency object, at most kInFlight frames in flight; and a drawable free, waited
// for on the shown event, for 100 ms at most (a window never shown mustn't hang the game).
static void pace_before(pacing *p, UINT flags) {
    if (!p || (flags & DXGI_PRESENT_TEST)) return;
    uint64_t n = p->presented;
    UINT latency = p->device && p->device->latency ? p->device->latency.load() : p->latency;
    if (!p->waitable) wait_frame(p, n + 1 > latency ? n + 1 - latency : 0);
    HANDLE shown = p->drawables ? shown_event() : nullptr;
    for (int i = 0; shown && i < 10 && p->drawables[0].load() > p->drawables[1].load() + 1; i++)
        WaitForSingleObject(shown, 10);
}

// After it: the frame's fence value, on the swapchain's queue after the present.
static void pace_after(pacing *p, HRESULT hr, UINT flags) {
    if (!p || FAILED(hr) || (flags & DXGI_PRESENT_TEST)) return;
    std::lock_guard<std::mutex> l(p->m);
    signal(p, p->presented + 1);
    p->presented++;
    if (p->wake) SetEvent(p->wake);
}

// ---- the window and displays (wraps, M3 Task 2): native DXMT's window system answers from what the guest pushes ----

#ifndef MN_D3D_LOOPBACK
static mn_d3d_window window_state(HWND hwnd) {
    mn_d3d_window w = {};
    w.hwnd = (uintptr_t)hwnd;
    RECT r;
    if (IsWindow(hwnd)) w.flags |= 4;
    if (GetClientRect(hwnd, &r)) w.width = (uint32_t)(r.right - r.left), w.height = (uint32_t)(r.bottom - r.top);
    if (IsIconic(hwnd)) w.flags |= 1;
    HWND front = GetForegroundWindow();
    DWORD pid = 0;
    if (front == hwnd) w.flags |= 2;
    if (front && GetWindowThreadProcessId(front, &pid) && pid == GetCurrentProcessId()) w.flags |= 8;
    w.monitor = (uintptr_t)MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    return w;
}

// Each display, then its modes: the current one, then EnumDisplaySettings' 32-bit ones (DXGI's formats are).
static BOOL CALLBACK one_display(HMONITOR m, HDC, LPRECT, LPARAM out) {
    auto *v = (std::vector<uint8_t> *)out;
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof mi;
    DEVMODEW dm = {};
    dm.dmSize = sizeof dm;
    if (!GetMonitorInfoW(m, &mi) || !EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return TRUE;
    std::vector<mn_d3d_mode> modes = {{dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, 1}};
    for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); i++)
        if (dm.dmBitsPerPel == 32) modes.push_back({dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, 1});
    mn_d3d_display d = {};
    d.monitor = (uintptr_t)m, d.left = mi.rcMonitor.left, d.top = mi.rcMonitor.top, d.right = mi.rcMonitor.right;
    d.bottom = mi.rcMonitor.bottom, d.modes = (uint32_t)modes.size();
    for (int i = 0; i < 31 && mi.szDevice[i]; i++) d.name[i] = (uint16_t)mi.szDevice[i];
    v->insert(v->end(), (uint8_t *)&d, (uint8_t *)(&d + 1));
    v->insert(v->end(), (uint8_t *)modes.data(), (uint8_t *)(modes.data() + modes.size()));
    ++*(uint32_t *)v->data();
    return TRUE;
}

static void push_displays() {
    std::vector<uint8_t> v(8, 0);
    EnumDisplayMonitors(nullptr, nullptr, one_display, (LPARAM)&v);
    mn_d3d_displays_args a = {(uintptr_t)v.data(), (uint32_t)v.size(), 0};
    mn_d3d_call(MN_D3D_OP_DISPLAYS, &a);
}

// The window's state to the host: always, or (changed) only when it differs from what this swapchain last pushed.
// True if the window is minimised.
static bool push_window(HWND hwnd, mn_obj *o = nullptr, bool changed = false) {
    if (!hwnd) return false;
    mn_d3d_window w = window_state(hwnd);
    if (o) {
        std::lock_guard<std::mutex> l(o->m);
        if (changed && !std::memcmp(&w, &o->pushed, sizeof w)) return w.flags & 1;
        o->pushed = w;
    }
    mn_d3d_call(MN_D3D_OP_WINDOW, &w);
    return w.flags & 1;
}
// The window changes native DXMT made during the call just forwarded (op 9), made here with DXMT's own Win32 window
// code as guest DXMT makes them, in order; then the window and displays as they are now.
static void replay(mn_obj *o) {
    mn_d3d_action v[32];
    mn_d3d_actions_args a = {(uintptr_t)v, sizeof v, 0};
    if (mn_d3d_call(MN_D3D_OP_ACTIONS, &a) || !a.len) return;
    namespace wsi = dxmt::wsi;
    // The window calls send WM_SIZE and the like to the game's window procedure at once, which may call back into this
    // swapchain (ResizeBuffers on WM_SIZE): made without its lock, as guest DXMT drops its own around them.
    dxmt::wsi::DXMTWindowState saved;
    {
        std::lock_guard<std::mutex> l(o->m);
        saved = o->saved;
    }
    for (uint32_t i = 0; i < a.len / sizeof *v; i++) {
        const mn_d3d_action &x = v[i];
        HWND w = (HWND)(uintptr_t)x.hwnd;
        HMONITOR m = (HMONITOR)(uintptr_t)x.monitor;
        wsi::WsiMode mode = {x.width, x.height, {x.refresh_num, x.refresh_den}, x.bits_per_pixel, x.interlaced != 0};
        switch (x.kind) {
        case MN_D3D_ACTION_SAVE: wsi::saveWindowState(w, &saved); break;
        case MN_D3D_ACTION_SET_MODE: wsi::setWindowMode(m, w, mode); break;
        case MN_D3D_ACTION_ENTER: wsi::enterFullscreenMode(m, w, &saved, x.arg != 0); break;
        case MN_D3D_ACTION_LEAVE: wsi::leaveFullscreenMode(w, &saved, x.arg != 0); break;
        case MN_D3D_ACTION_RESTORE_MODE: wsi::restoreDisplayMode(m); break;
        case MN_D3D_ACTION_RESIZE: wsi::resizeWindow(w, &saved, x.width, x.height); break;
        case MN_D3D_ACTION_UPDATE_FULLSCREEN: wsi::updateFullscreenWindow(m, w, x.arg != 0); break;
        case MN_D3D_ACTION_MINIMIZE: wsi::minimizeWindow(w); break;
        }
    }
    {
        std::lock_guard<std::mutex> l(o->m);
        o->saved = saved;
    }
    push_displays();
    push_window(o->window, o);
}
#else
static void push_displays() {}
static bool push_window(HWND, mn_obj * = nullptr, bool = false) { return false; } // the loopback's have no windows
static void replay(mn_obj *) {}
#endif

// Each wrap's generated proxy (mn_d3d_special keeps it).
static const void *px_create_swapchain, *px_create_for_hwnd, *px_resize, *px_resize1, *px_resize_target,
    *px_fullscreen, *px_present, *px_present1, *px_containing_output, *px_enum_outputs;
#define PX(fn, p) ((decltype(&fn))(p))

static void bind_window(void **out, HWND hwnd) {
    if (!out || !*out) return;
    mn_obj *o = ((mn_proxy *)*out)->obj;
    std::lock_guard<std::mutex> l(o->m);
    o->window = hwnd;
}

static HRESULT STDMETHODCALLTYPE create_swapchain(mn_proxy *This, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc,
                                                  IDXGISwapChain **out) {
    HWND hwnd = desc ? desc->OutputWindow : nullptr;
    push_window(hwnd);
    HRESULT hr = PX(create_swapchain, px_create_swapchain)(This, device, desc, out);
    if (SUCCEEDED(hr)) bind_window((void **)out, hwnd);
    if (SUCCEEDED(hr) && out && *out)
        ((mn_proxy *)*out)->obj->pace = start_pacing(device, *out, desc ? desc->Flags : 0);
    if (SUCCEEDED(hr) && out && *out) replay(((mn_proxy *)*out)->obj); // created full screen
    return hr;
}

static HRESULT STDMETHODCALLTYPE create_for_hwnd(mn_proxy *This, IUnknown *device, HWND hwnd,
                                                 const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                 const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                 IDXGIOutput *restrict_to, IDXGISwapChain1 **out) {
    push_window(hwnd);
    HRESULT hr = PX(create_for_hwnd, px_create_for_hwnd)(This, device, hwnd, desc, fullscreen, restrict_to, out);
    if (SUCCEEDED(hr)) bind_window((void **)out, hwnd);
    if (SUCCEEDED(hr) && out && *out)
        ((mn_proxy *)*out)->obj->pace = start_pacing(device, *out, desc ? desc->Flags : 0);
    if (SUCCEEDED(hr) && out && *out) replay(((mn_proxy *)*out)->obj);
    return hr;
}

// Resizing the buffers resets the source size (DXGI's).
static void resized(mn_obj *o) {
    std::lock_guard<std::mutex> l(o->m);
    o->source_width = o->source_height = 0;
}

// The buffers are free only once the frames that use them are done: waited for here, not on the host.
static void drain(pacing *p) {
    if (p) wait_frame(p, p->presented);
}

static HRESULT STDMETHODCALLTYPE resize(mn_proxy *This, UINT count, UINT width, UINT height, DXGI_FORMAT format,
                                        UINT flags) {
    drain(This->obj->pace);
    push_window(This->obj->window, This->obj);
    HRESULT hr = PX(resize, px_resize)(This, count, width, height, format, flags);
    if (SUCCEEDED(hr)) resized(This->obj);
    return hr;
}

static HRESULT STDMETHODCALLTYPE resize1(mn_proxy *This, UINT count, UINT width, UINT height, DXGI_FORMAT format,
                                         UINT flags, const UINT *node_mask, IUnknown *const *queues) {
    drain(This->obj->pace);
    push_window(This->obj->window, This->obj);
    HRESULT hr = PX(resize1, px_resize1)(This, count, width, height, format, flags, node_mask, queues);
    if (SUCCEEDED(hr)) resized(This->obj);
    return hr;
}

static HRESULT STDMETHODCALLTYPE resize_target(mn_proxy *This, const DXGI_MODE_DESC *mode) {
    push_displays();
    push_window(This->obj->window, This->obj);
    HRESULT hr = PX(resize_target, px_resize_target)(This, mode);
    replay(This->obj);
    return hr;
}

static HRESULT STDMETHODCALLTYPE fullscreen(mn_proxy *This, BOOL on, IDXGIOutput *target) {
    push_displays();
    push_window(This->obj->window, This->obj);
    HRESULT hr = PX(fullscreen, px_fullscreen)(This, on, target);
    replay(This->obj);
    return hr;
}

// D3D11CreateDeviceAndSwapChain (a wrap): its swapchain's window as IDXGIFactory::CreateSwapChain's.
extern "C" HRESULT WINAPI mn_px_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                              const D3D_FEATURE_LEVEL *, UINT, UINT,
                                                              const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
                                                              ID3D11Device **, D3D_FEATURE_LEVEL *,
                                                              ID3D11DeviceContext **);
extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE swrast,
                                                        UINT flags, const D3D_FEATURE_LEVEL *levels, UINT nlevels,
                                                        UINT sdk, const DXGI_SWAP_CHAIN_DESC *desc,
                                                        IDXGISwapChain **swapchain, ID3D11Device **device,
                                                        D3D_FEATURE_LEVEL *level, ID3D11DeviceContext **context) {
    HWND hwnd = desc ? desc->OutputWindow : nullptr;
    if (swapchain) push_window(hwnd);
    HRESULT hr = mn_px_D3D11CreateDeviceAndSwapChain(adapter, type, swrast, flags, levels, nlevels, sdk, desc, swapchain,
                                                     device, level, context);
    if (SUCCEEDED(hr) && swapchain) bind_window((void **)swapchain, hwnd);
    if (SUCCEEDED(hr) && swapchain && *swapchain && device && *device)
        ((mn_proxy *)*swapchain)->obj->pace = start_pacing(*device, *swapchain, desc ? desc->Flags : 0);
    if (SUCCEEDED(hr) && swapchain && *swapchain) replay(((mn_proxy *)*swapchain)->obj); // created full screen
    return hr;
}

// ID3D11Multithread's (and ID3D10Multithread's) Enter and Leave (specials): one object per DXMT device (identity).
static void STDMETHODCALLTYPE mt_enter(mn_proxy *This) { This->obj->mt.lock(); }
static void STDMETHODCALLTYPE mt_leave(mn_proxy *This) { This->obj->mt.unlock(); }

// The frame latency object (a special): our semaphore, a waitable swapchain's (DXMT's answers none without the flag).
static HANDLE STDMETHODCALLTYPE latency_object(mn_proxy *This) {
    pacing *p = This->obj->pace;
    HANDLE h = nullptr;
    if (p && p->waitable)
        DuplicateHandle(GetCurrentProcess(), p->waitable, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    return h;
}

static const void *px_set_latency;
// A waitable swapchain's raised latency frees as many more frames at once, as DXMT's semaphore does.
static HRESULT STDMETHODCALLTYPE set_latency(mn_proxy *This, UINT latency) {
    HRESULT hr = PX(set_latency, px_set_latency)(This, latency);
    pacing *p = This->obj->pace;
    if (SUCCEEDED(hr) && p && p->waitable) {
        std::lock_guard<std::mutex> l(p->m);
        if (latency > p->latency) ReleaseSemaphore(p->waitable, (LONG)(latency - p->latency), nullptr);
        p->latency = latency;
    }
    return hr;
}

static const void *px_device_latency;
// A D3D11 device's frame latency (a wrap, M4 Task 6): its swapchains' pacing keeps it (0: DXGI's default).
static HRESULT STDMETHODCALLTYPE device_latency(mn_proxy *This, UINT latency) {
    HRESULT hr = PX(device_latency, px_device_latency)(This, latency);
    if (SUCCEEDED(hr)) This->obj->latency = latency;
    return hr;
}

// ---- a swapchain's own answers (specials, M3 Task 3): DXMT has none (IMPLEMENT_ME); DXGI's are these ----

static HRESULT STDMETHODCALLTYPE restrict_to_output(mn_proxy *, IDXGIOutput **out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr; // a swapchain restricted to none
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_background(mn_proxy *This, const DXGI_RGBA *color) {
    if (!color) return E_INVALIDARG;
    std::lock_guard<std::mutex> l(This->obj->m);
    This->obj->background = *color;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_background(mn_proxy *This, DXGI_RGBA *color) {
    if (!color) return E_INVALIDARG;
    std::lock_guard<std::mutex> l(This->obj->m);
    *color = This->obj->background;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_rotation(mn_proxy *, DXGI_MODE_ROTATION rotation) {
    return rotation == DXGI_MODE_ROTATION_IDENTITY ? S_OK : DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE get_rotation(mn_proxy *, DXGI_MODE_ROTATION *rotation) {
    if (!rotation) return E_INVALIDARG;
    *rotation = DXGI_MODE_ROTATION_IDENTITY;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_source_size(mn_proxy *This, UINT width, UINT height) {
    DXGI_SWAP_CHAIN_DESC1 d = {};
    if (FAILED(((IDXGISwapChain1 *)This)->GetDesc1(&d))) return E_FAIL;
    if (!width || !height || width > d.Width || height > d.Height) return E_INVALIDARG;
    std::lock_guard<std::mutex> l(This->obj->m);
    This->obj->source_width = width, This->obj->source_height = height;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_source_size(mn_proxy *This, UINT *width, UINT *height) {
    if (!width || !height) return E_INVALIDARG;
    {
        std::lock_guard<std::mutex> l(This->obj->m);
        *width = This->obj->source_width, *height = This->obj->source_height;
    }
    DXGI_SWAP_CHAIN_DESC1 d = {};
    if (!*width && SUCCEEDED(((IDXGISwapChain1 *)This)->GetDesc1(&d))) *width = d.Width, *height = d.Height;
    return S_OK;
}

// A minimised window's present is skipped by DXMT, which then sleeps a 60 Hz frame (natively, with the guest's window
// system hooked, it doesn't): the sleep is here, never on the host's vCPU thread.
static HRESULT STDMETHODCALLTYPE present(mn_proxy *This, UINT sync, UINT flags) {
    bool minimised = push_window(This->obj->window, This->obj, true);
    pace_before(This->obj->pace, flags);
    HRESULT hr = PX(present, px_present)(This, sync, flags);
    pace_after(This->obj->pace, hr, flags);
    if (minimised && !(flags & DXGI_PRESENT_TEST)) Sleep(16);
    return hr;
}

static HRESULT STDMETHODCALLTYPE present1(mn_proxy *This, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *p) {
    bool minimised = push_window(This->obj->window, This->obj, true);
    pace_before(This->obj->pace, flags);
    HRESULT hr = PX(present1, px_present1)(This, sync, flags, p);
    pace_after(This->obj->pace, hr, flags);
    if (minimised && !(flags & DXGI_PRESENT_TEST)) Sleep(16);
    return hr;
}

static HRESULT STDMETHODCALLTYPE containing_output(mn_proxy *This, IDXGIOutput **out) {
    push_displays();
    push_window(This->obj->window, This->obj);
    return PX(containing_output, px_containing_output)(This, out);
}

static HRESULT STDMETHODCALLTYPE enum_outputs(mn_proxy *This, UINT index, IDXGIOutput **out) {
    push_displays();
    return PX(enum_outputs, px_enum_outputs)(This, index, out);
}

// ---- pipeline state streams (specials, Task 10) ----

// A stream's subobject: its type, then its data at the data's alignment, the whole padded to a pointer's size.
template <class T> struct subobject {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
    T data;
};
template <class T> static const T *take(const char *&at, const char *end) {
    auto *s = (const subobject<T> *)at;
    if ((size_t)(end - at) < sizeof *s) return nullptr;
    at += (sizeof *s + sizeof(void *) - 1) / sizeof(void *) * sizeof(void *);
    return &s->data;
}

// The stream as the description forms (the host's CreatePipelineState reads it so): D3D12's defaults for what it
// leaves out, each subobject once; amplification and mesh shaders aren't supported (E_NOTIMPL), and a compute shader
// with a vertex one is invalid.
static HRESULT read_stream(const D3D12_PIPELINE_STATE_STREAM_DESC *d, D3D12_GRAPHICS_PIPELINE_STATE_DESC &g,
                           D3D12_COMPUTE_PIPELINE_STATE_DESC &c, bool &compute) {
    g = {}, c = {};
    if (!d || (d->SizeInBytes && !d->pPipelineStateSubobjectStream)) return E_INVALIDARG;
    g.DepthStencilState.DepthEnable = TRUE;
    g.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    g.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    g.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                     D3D12_COMPARISON_FUNC_ALWAYS};
    g.DepthStencilState.BackFace = g.DepthStencilState.FrontFace;
    g.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    g.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    g.RasterizerState.DepthClipEnable = TRUE;
    g.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    g.SampleDesc = {1, 0};
    g.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    const char *at = (const char *)d->pPipelineStateSubobjectStream, *end = at + d->SizeInBytes;
    uint64_t seen = 0;
    using root = ID3D12RootSignature *;
#define TAKE(T, ...)                                                                                                   \
    do {                                                                                                               \
        const T *v_ = take<T>(at, end);                                                                                \
        if (!v_) return E_INVALIDARG;                                                                                  \
        __VA_ARGS__;                                                                                                   \
    } while (0)
    while (at < end) {
        if ((size_t)(end - at) < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE)) return E_INVALIDARG;
        auto type = *(const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE *)at;
        if ((unsigned)type >= 64 || (seen >> type & 1)) return E_INVALIDARG;
        seen |= 1ull << type;
        switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
            TAKE(root, g.pRootSignature = c.pRootSignature = *v_);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: TAKE(D3D12_SHADER_BYTECODE, g.VS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: TAKE(D3D12_SHADER_BYTECODE, g.PS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: TAKE(D3D12_SHADER_BYTECODE, g.DS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: TAKE(D3D12_SHADER_BYTECODE, g.HS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: TAKE(D3D12_SHADER_BYTECODE, g.GS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: TAKE(D3D12_SHADER_BYTECODE, c.CS = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: TAKE(D3D12_STREAM_OUTPUT_DESC, g.StreamOutput = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: TAKE(D3D12_BLEND_DESC, g.BlendState = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: TAKE(UINT, g.SampleMask = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: TAKE(D3D12_RASTERIZER_DESC, g.RasterizerState = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
            TAKE(D3D12_DEPTH_STENCIL_DESC, g.DepthStencilState = *v_);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: TAKE(D3D12_INPUT_LAYOUT_DESC, g.InputLayout = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
            TAKE(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE, g.IBStripCutValue = *v_);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
            TAKE(D3D12_PRIMITIVE_TOPOLOGY_TYPE, g.PrimitiveTopologyType = *v_);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
            TAKE(D3D12_RT_FORMAT_ARRAY, std::memcpy(g.RTVFormats, v_->RTFormats, sizeof g.RTVFormats);
                 g.NumRenderTargets = v_->NumRenderTargets);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: TAKE(DXGI_FORMAT, g.DSVFormat = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: TAKE(DXGI_SAMPLE_DESC, g.SampleDesc = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: TAKE(UINT, g.NodeMask = c.NodeMask = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
            TAKE(D3D12_CACHED_PIPELINE_STATE, g.CachedPSO = c.CachedPSO = *v_);
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: TAKE(D3D12_PIPELINE_STATE_FLAGS, g.Flags = c.Flags = *v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: // its depth bounds test has no place: as natively
            TAKE(D3D12_DEPTH_STENCIL_DESC1, {
                auto &ds = g.DepthStencilState;
                ds.DepthEnable = v_->DepthEnable, ds.DepthWriteMask = v_->DepthWriteMask, ds.DepthFunc = v_->DepthFunc;
                ds.StencilEnable = v_->StencilEnable, ds.StencilReadMask = v_->StencilReadMask;
                ds.StencilWriteMask = v_->StencilWriteMask, ds.FrontFace = v_->FrontFace, ds.BackFace = v_->BackFace;
            });
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: TAKE(D3D12_VIEW_INSTANCING_DESC, (void)v_); break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
            TAKE(D3D12_SHADER_BYTECODE, if (v_->pShaderBytecode) return E_NOTIMPL);
            break;
        default:
            return E_INVALIDARG;
        }
    }
#undef TAKE
    compute = c.CS.pShaderBytecode != nullptr;
    uint64_t drawing = 1ull << D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS | 1ull << D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS |
                       1ull << D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
    return compute && (seen & drawing) ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE create_pipeline_state(mn_proxy *This, const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                                                       REFIID riid, void **pipeline) {
    if (pipeline) *pipeline = nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC g;
    D3D12_COMPUTE_PIPELINE_STATE_DESC c;
    bool compute;
    HRESULT hr = read_stream(desc, g, c, compute);
    if (FAILED(hr)) return hr;
    auto *device = (ID3D12Device2 *)This; // the forwarded description forms, through its own vtable
    return compute ? device->CreateComputePipelineState(&c, riid, pipeline)
                   : device->CreateGraphicsPipelineState(&g, riid, pipeline);
}

static HRESULT STDMETHODCALLTYPE load_pipeline(mn_proxy *This, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                                               REFIID riid, void **pipeline) {
    if (pipeline) *pipeline = nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC g;
    D3D12_COMPUTE_PIPELINE_STATE_DESC c;
    bool compute;
    HRESULT hr = read_stream(desc, g, c, compute);
    if (FAILED(hr)) return hr;
    auto *library = (ID3D12PipelineLibrary1 *)This;
    return compute ? library->LoadComputePipeline(name, &c, riid, pipeline)
                   : library->LoadGraphicsPipeline(name, &g, riid, pipeline);
}

// ---- CheckFeatureSupport (own, Task 10) ----

static HRESULT STDMETHODCALLTYPE check_feature(mn_proxy *This, D3D12_FEATURE feature, void *data, UINT size) {
    mn_d3d_p_ID3D12Device_CheckFeatureSupport p = {};
    p.self = (uintptr_t)This, p.feature = (uint32_t)feature, p.feature_data = (uintptr_t)data, p.feature_data_size = size;
    std::vector<uint8_t> flat; // D3D12_FEATURE_FEATURE_LEVELS: the struct, then the levels its pointer names
    auto *levels = (D3D12_FEATURE_DATA_FEATURE_LEVELS *)data;
    bool flatten = feature == D3D12_FEATURE_FEATURE_LEVELS && data && size == sizeof *levels &&
                   levels->pFeatureLevelsRequested && levels->NumFeatureLevels <= 64;
    if (flatten) {
        flat.resize(sizeof *levels + 4ull * levels->NumFeatureLevels);
        std::memcpy(flat.data(), levels, sizeof *levels);
        std::memcpy(flat.data() + sizeof *levels, levels->pFeatureLevelsRequested, 4ull * levels->NumFeatureLevels);
        p.feature_data = (uintptr_t)flat.data(), p.feature_data_size = (UINT)flat.size(), p.aux[0] = sizeof *levels;
    }
    int32_t s = mn_d3d_call(MN_D3D_ID_ID3D12Device_CheckFeatureSupport, &p);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
    if (flatten && SUCCEEDED(hr))
        levels->MaxSupportedFeatureLevel = ((D3D12_FEATURE_DATA_FEATURE_LEVELS *)flat.data())->MaxSupportedFeatureLevel;
    return hr;
}

// ---- D3D11's and D3D10's data and maps (own, M4 Task 4) ----

// An interface's mn_d3d_ifaces index (natively __uuidof is a temporary).
template <class T> static int iface_of() {
    static const GUID iid = __uuidof(T);
    return mn_d3d_iface_of(&iid);
}

static mn_res described(const D3D11_BUFFER_DESC &d) { return mn_res_of(1, DXGI_FORMAT_UNKNOWN, d.ByteWidth, 1, 1, 1, 1); }
static mn_res described(const D3D11_TEXTURE1D_DESC &d) { return mn_res_of(2, d.Format, d.Width, 1, 1, d.MipLevels, d.ArraySize); }
static mn_res described(const D3D11_TEXTURE2D_DESC &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
static mn_res described(const D3D11_TEXTURE2D_DESC1 &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
static mn_res described(const D3D11_TEXTURE3D_DESC &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }
static mn_res described(const D3D11_TEXTURE3D_DESC1 &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }
static mn_res described(const D3D10_BUFFER_DESC &d) { return mn_res_of(1, DXGI_FORMAT_UNKNOWN, d.ByteWidth, 1, 1, 1, 1); }
static mn_res described(const D3D10_TEXTURE1D_DESC &d) { return mn_res_of(2, d.Format, d.Width, 1, 1, d.MipLevels, d.ArraySize); }
static mn_res described(const D3D10_TEXTURE2D_DESC &d) {
    return mn_res_of(3, d.Format, d.Width, d.Height, 1, d.MipLevels, d.ArraySize);
}
static mn_res described(const D3D10_TEXTURE3D_DESC &d) { return mn_res_of(4, d.Format, d.Width, d.Height, d.Depth, d.MipLevels, 1); }

template <class Res, class T, class Desc> static bool describe(Res *res, mn_res &r) {
    T *t = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(T), (void **)&t))) return false;
    Desc d = {};
    t->GetDesc(&d);
    t->Release();
    r = described(d);
    return true;
}

// The description a resource the guest didn't create (a back buffer, another API's view of one) has: asked once.
static mn_res resource_of(IUnknown *resource) {
    mn_obj *o = ((mn_proxy *)resource)->obj;
    {
        std::lock_guard<std::mutex> l(o->m);
        if (o->res.dim) return o->res;
    }
    mn_res r;
    ID3D11Resource *r11 = nullptr;
    ID3D10Resource *r10 = nullptr;
    if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Resource), (void **)&r11))) {
        describe<ID3D11Resource, ID3D11Buffer, D3D11_BUFFER_DESC>(r11, r) ||
            describe<ID3D11Resource, ID3D11Texture1D, D3D11_TEXTURE1D_DESC>(r11, r) ||
            describe<ID3D11Resource, ID3D11Texture2D, D3D11_TEXTURE2D_DESC>(r11, r) ||
            describe<ID3D11Resource, ID3D11Texture3D, D3D11_TEXTURE3D_DESC>(r11, r);
        r11->Release();
    } else if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D10Resource), (void **)&r10))) {
        describe<ID3D10Resource, ID3D10Buffer, D3D10_BUFFER_DESC>(r10, r) ||
            describe<ID3D10Resource, ID3D10Texture1D, D3D10_TEXTURE1D_DESC>(r10, r) ||
            describe<ID3D10Resource, ID3D10Texture2D, D3D10_TEXTURE2D_DESC>(r10, r) ||
            describe<ID3D10Resource, ID3D10Texture3D, D3D10_TEXTURE3D_DESC>(r10, r);
        r10->Release();
    }
    std::lock_guard<std::mutex> l(o->m);
    return o->res = r;
}

// A resource's initial data, each subresource sized from its description, packed (mn_d3d.h's struct mn_d3d_sub).
static std::vector<uint8_t> pack(const mn_res &r, const D3D11_SUBRESOURCE_DATA *init) {
    const uint32_t n = mn_subresources(r);
    std::vector<mn_d3d_sub> subs(n);
    uint64_t at = 8 + n * sizeof(mn_d3d_sub);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t size = init[i].pSysMem ? mn_subresource_bytes(r, i, nullptr, init[i].SysMemPitch, init[i].SysMemSlicePitch) : 0;
        subs[i] = {(uint32_t)at, init[i].SysMemPitch, init[i].SysMemSlicePitch, (uint32_t)size};
        at += (size + 15) & ~15ull;
    }
    std::vector<uint8_t> flat(at);
    std::memcpy(flat.data(), &n, 4);
    std::memcpy(flat.data() + 8, subs.data(), n * sizeof(mn_d3d_sub));
    for (uint32_t i = 0; i < n; i++)
        if (subs[i].size) std::memcpy(flat.data() + subs[i].offset, init[i].pSysMem, subs[i].size);
    return flat;
}

static void set_res(mn_obj *o, const mn_res &r) {
    std::lock_guard<std::mutex> l(o->m);
    o->res = r;
}

#define MN_CREATE(fn, NAME, Desc, Data, T, DESC, INIT, OUT)                                                            \
    static HRESULT STDMETHODCALLTYPE fn(mn_proxy *This, const Desc *desc, const Data *init, T **out) {                \
        mn_res r = desc ? described(*desc) : mn_res{};                                                                 \
        std::vector<uint8_t> flat;                                                                                     \
        if (desc && init) flat = pack(r, (const D3D11_SUBRESOURCE_DATA *)init);                                        \
        mn_d3d_p_##NAME p = {};                                                                                        \
        p.self = (uintptr_t)This, p.DESC = (uintptr_t)desc, p.OUT = (uintptr_t)out;                                   \
        p.INIT = flat.empty() ? 0 : (uintptr_t)flat.data(), p.aux[0] = flat.size();                                  \
        int32_t s = mn_d3d_call(MN_D3D_ID_##NAME, &p);                                                                 \
        HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;                                                      \
        if (out) {                                                                                                     \
            *out = SUCCEEDED(hr) && p.OUT                                                                              \
                       ? (T *)mn_proxy_out(p.OUT, iface_of<T>(), nullptr, p.OUT##_ifaces)              \
                       : nullptr;                                                                                      \
            if (*out) set_res(((mn_proxy *)*out)->obj, r);                                                             \
        }                                                                                                              \
        return hr;                                                                                                     \
    }
MN_CREATE(create_buffer11, ID3D11Device_CreateBuffer, D3D11_BUFFER_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Buffer, pDesc,
          pInitialData, ppBuffer)
MN_CREATE(create_tex1d11, ID3D11Device_CreateTexture1D, D3D11_TEXTURE1D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture1D,
          pDesc, pInitialData, ppTexture1D)
MN_CREATE(create_tex2d11, ID3D11Device_CreateTexture2D, D3D11_TEXTURE2D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture2D,
          pDesc, pInitialData, ppTexture2D)
MN_CREATE(create_tex3d11, ID3D11Device_CreateTexture3D, D3D11_TEXTURE3D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture3D,
          pDesc, pInitialData, ppTexture3D)
MN_CREATE(create_tex2d1_11, ID3D11Device3_CreateTexture2D1, D3D11_TEXTURE2D_DESC1, D3D11_SUBRESOURCE_DATA,
          ID3D11Texture2D1, desc, initial_data, texture)
MN_CREATE(create_tex3d1_11, ID3D11Device3_CreateTexture3D1, D3D11_TEXTURE3D_DESC1, D3D11_SUBRESOURCE_DATA,
          ID3D11Texture3D1, desc, initial_data, texture)
MN_CREATE(create_buffer10, ID3D10Device_CreateBuffer, D3D10_BUFFER_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Buffer, pDesc,
          pInitialData, ppBuffer)
MN_CREATE(create_tex1d10, ID3D10Device_CreateTexture1D, D3D10_TEXTURE1D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture1D,
          pDesc, pInitialData, ppTexture1D)
MN_CREATE(create_tex2d10, ID3D10Device_CreateTexture2D, D3D10_TEXTURE2D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture2D,
          pDesc, pInitialData, ppTexture2D)
MN_CREATE(create_tex3d10, ID3D10Device_CreateTexture3D, D3D10_TEXTURE3D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture3D,
          pDesc, pInitialData, ppTexture3D)

#define MN_COMMA ,
// UpdateSubresource: its data sized from the destination's description and the box.
#define MN_UPDATE(fn, NAME, Res, Box, EXTRA, SET)                                                                       \
    static void STDMETHODCALLTYPE fn(mn_proxy *This, Res *dst, UINT sub, const Box *box, const void *data, UINT pitch, \
                                     UINT slice EXTRA) {                                                               \
        mn_d3d_p_##NAME p = {};                                                                                        \
        p.self = (uintptr_t)This, p.pDstResource = (uintptr_t)dst, p.DstSubresource = sub, p.pDstBox = (uintptr_t)box; \
        p.pSrcData = (uintptr_t)data, p.SrcRowPitch = pitch, p.SrcDepthPitch = slice;                                 \
        SET;                                                                                                           \
        p.aux[0] = data && dst ? mn_subresource_bytes(resource_of(dst), sub, (const D3D11_BOX *)box, pitch, slice) : 0; \
        mn_d3d_record(This, MN_D3D_ID_##NAME, &p);                                                                     \
    }
MN_UPDATE(update11, ID3D11DeviceContext_UpdateSubresource, ID3D11Resource, D3D11_BOX, , (void)0)
MN_UPDATE(update1_11, ID3D11DeviceContext1_UpdateSubresource1, ID3D11Resource, D3D11_BOX, MN_COMMA UINT flags,
          p.CopyFlags = flags)
MN_UPDATE(update10, ID3D10Device_UpdateSubresource, ID3D10Resource, D3D10_BOX, , (void)0)

// Maps: the unix side answers our address of the host's memory in aux[0] (rules.txt's own, as ID3D12Resource::Map's).
static HRESULT STDMETHODCALLTYPE map11(mn_proxy *This, ID3D11Resource *res, UINT sub, D3D11_MAP type, UINT flags,
                                       D3D11_MAPPED_SUBRESOURCE *mapped) {
    mn_d3d_p_ID3D11DeviceContext_Map p = {};
    D3D11_MAPPED_SUBRESOURCE m = {};
    p.self = (uintptr_t)This, p.pResource = (uintptr_t)res, p.Subresource = sub, p.MapType = (uint32_t)type;
    p.MapFlags = flags, p.pMappedResource = mapped ? (uintptr_t)&m : 0;
    int32_t s = mn_d3d_record(This, MN_D3D_ID_ID3D11DeviceContext_Map, &p);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;
    if (SUCCEEDED(hr) && mapped && !p.aux[0]) hr = E_FAIL; // not in an arena of ours: the host said so too
    if (mapped) *mapped = SUCCEEDED(hr) ? D3D11_MAPPED_SUBRESOURCE{(void *)(uintptr_t)p.aux[0], m.RowPitch, m.DepthPitch}
                                        : D3D11_MAPPED_SUBRESOURCE{};
    return hr;
}

#define MN_MAP10(fn, NAME, SUB, Out, OUT, FILL)                                                                         \
    static HRESULT STDMETHODCALLTYPE fn(mn_proxy *This SUB, D3D10_MAP type, UINT flags, Out *out) {                    \
        using mn_out = Out;                                                                                            \
        mn_d3d_p_##NAME p = {};                                                                                        \
        mn_out m = {};                                                                                                 \
        p.self = (uintptr_t)This, p.MapType = (uint32_t)type, p.MapFlags = flags, p.OUT = out ? (uintptr_t)&m : 0;    \
        FILL;                                                                                                          \
        int32_t s = mn_d3d_call(MN_D3D_ID_##NAME, &p);                                                                 \
        HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)(uint32_t)p.ret;                                                      \
        if (SUCCEEDED(hr) && out && !p.aux[0]) hr = E_FAIL;                                                            \
        if (out) {                                                                                                     \
            *out = SUCCEEDED(hr) ? m : mn_out{};                                                                       \
            *(void **)out = SUCCEEDED(hr) ? (void *)(uintptr_t)p.aux[0] : nullptr; /* its pData, first */            \
        }                                                                                                              \
        return hr;                                                                                                     \
    }
MN_MAP10(map10_buffer, ID3D10Buffer_Map, , void *, ppData, (void)0)
MN_MAP10(map10_tex1d, ID3D10Texture1D_Map, MN_COMMA UINT sub, void *, ppData, p.Subresource = sub)
MN_MAP10(map10_tex2d, ID3D10Texture2D_Map, MN_COMMA UINT sub, D3D10_MAPPED_TEXTURE2D, pMappedTex2D, p.Subresource = sub)
MN_MAP10(map10_tex3d, ID3D10Texture3D_Map, MN_COMMA UINT sub, D3D10_MAPPED_TEXTURE3D, pMappedTex3D, p.Subresource = sub)

// D3D11_KEEP_* counts carry no array (a wrap): the generated proxy would read that many.
static const void *px_om_uav;
static void STDMETHODCALLTYPE om_uav(mn_proxy *This, UINT nrtv, ID3D11RenderTargetView *const *rtvs,
                                     ID3D11DepthStencilView *dsv, UINT start, UINT nuav,
                                     ID3D11UnorderedAccessView *const *uavs, const UINT *counts) {
    if (nrtv == D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL) rtvs = nullptr;
    if (nuav == D3D11_KEEP_UNORDERED_ACCESS_VIEWS) uavs = nullptr, counts = nullptr;
    PX(om_uav, px_om_uav)(This, nrtv, rtvs, dsv, start, nuav, uavs, counts);
}

// DXMT has no meta commands: it answers so, and so do we (their descriptions hold names the wire doesn't carry).
static HRESULT STDMETHODCALLTYPE meta_commands(mn_proxy *, UINT *count, D3D12_META_COMMAND_DESC *) {
    if (!count) return E_INVALIDARG;
    *count = 0;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE meta_command_parameters(mn_proxy *, REFGUID, D3D12_META_COMMAND_PARAMETER_STAGE, UINT *,
                                                         UINT *, D3D12_META_COMMAND_PARAMETER_DESC *) {
    return E_INVALIDARG;
}

const void *mn_d3d_special(const char *name, const void *fallback) {
    static const struct {
        const char *name;
        const void *fn;
        const void **next = nullptr; // a wrap's: its generated proxy
    } code[] = {
        {"IDXGIFactory::CreateSwapChain", (const void *)create_swapchain, &px_create_swapchain},
        {"IDXGIFactory2::CreateSwapChainForHwnd", (const void *)create_for_hwnd, &px_create_for_hwnd},
        {"IDXGISwapChain::ResizeBuffers", (const void *)resize, &px_resize},
        {"IDXGISwapChain3::ResizeBuffers1", (const void *)resize1, &px_resize1},
        {"IDXGISwapChain::ResizeTarget", (const void *)resize_target, &px_resize_target},
        {"IDXGISwapChain::SetFullscreenState", (const void *)fullscreen, &px_fullscreen},
        {"IDXGISwapChain::Present", (const void *)present, &px_present},
        {"IDXGISwapChain1::Present1", (const void *)present1, &px_present1},
        {"IDXGISwapChain::GetContainingOutput", (const void *)containing_output, &px_containing_output},
        {"IDXGIAdapter::EnumOutputs", (const void *)enum_outputs, &px_enum_outputs},
        {"IDXGISwapChain2::GetFrameLatencyWaitableObject", (const void *)latency_object},
        {"IDXGIDevice1::SetMaximumFrameLatency", (const void *)device_latency, &px_device_latency},
        {"ID3D11Device5::CreateFence", (const void *)create_fence11},
        {"ID3D11Fence::GetCompletedValue", (const void *)completed_value11},
        {"ID3D11Fence::SetEventOnCompletion", (const void *)set_event11},
        {"ID3D11DeviceContext3::Flush1", (const void *)flush1},
        {"ID3D11Multithread::Enter", (const void *)mt_enter},
        {"ID3D11Multithread::Leave", (const void *)mt_leave},
        {"IDXGISwapChain2::SetMaximumFrameLatency", (const void *)set_latency, &px_set_latency},
        {"IDXGISwapChain1::GetRestrictToOutput", (const void *)restrict_to_output},
        {"IDXGISwapChain1::SetBackgroundColor", (const void *)set_background},
        {"IDXGISwapChain1::GetBackgroundColor", (const void *)get_background},
        {"IDXGISwapChain1::SetRotation", (const void *)set_rotation},
        {"IDXGISwapChain1::GetRotation", (const void *)get_rotation},
        {"IDXGISwapChain2::SetSourceSize", (const void *)set_source_size},
        {"IDXGISwapChain2::GetSourceSize", (const void *)get_source_size},
        {"ID3D12Device5::EnumerateMetaCommands", (const void *)meta_commands},
        {"ID3D12Device5::EnumerateMetaCommandParameters", (const void *)meta_command_parameters},
        {"ID3D12Device::CheckFeatureSupport", (const void *)check_feature},
        {"ID3D11Device::CreateBuffer", (const void *)create_buffer11},
        {"ID3D11Device::CreateTexture1D", (const void *)create_tex1d11},
        {"ID3D11Device::CreateTexture2D", (const void *)create_tex2d11},
        {"ID3D11Device::CreateTexture3D", (const void *)create_tex3d11},
        {"ID3D11Device3::CreateTexture2D1", (const void *)create_tex2d1_11},
        {"ID3D11Device3::CreateTexture3D1", (const void *)create_tex3d1_11},
        {"ID3D10Device::CreateBuffer", (const void *)create_buffer10},
        {"ID3D10Device::CreateTexture1D", (const void *)create_tex1d10},
        {"ID3D10Device::CreateTexture2D", (const void *)create_tex2d10},
        {"ID3D10Device::CreateTexture3D", (const void *)create_tex3d10},
        {"ID3D11DeviceContext::UpdateSubresource", (const void *)update11},
        {"ID3D11DeviceContext1::UpdateSubresource1", (const void *)update1_11},
        {"ID3D10Device::UpdateSubresource", (const void *)update10},
        {"ID3D11DeviceContext::Map", (const void *)map11},
        {"ID3D10Buffer::Map", (const void *)map10_buffer},
        {"ID3D10Texture1D::Map", (const void *)map10_tex1d},
        {"ID3D10Texture2D::Map", (const void *)map10_tex2d},
        {"ID3D10Texture3D::Map", (const void *)map10_tex3d},
        {"ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews", (const void *)om_uav, &px_om_uav},
        {"ID3D12Device2::CreatePipelineState", (const void *)create_pipeline_state},
        {"ID3D12PipelineLibrary1::LoadPipeline", (const void *)load_pipeline},
        {"ID3D12Device::CreateFence", (const void *)create_fence},
        {"ID3D12Fence::GetCompletedValue", (const void *)completed_value},
        {"ID3D12Fence::Signal", (const void *)fence_signal},
        {"ID3D12Fence::SetEventOnCompletion", (const void *)set_event},
        {"ID3D12Device1::SetEventOnMultipleFenceCompletion", (const void *)set_event_multiple},
        {"ID3D12Resource::Map", (const void *)map},
        {"ID3D12Object::SetName", (const void *)set_name},
        {"ID3D10Blob::GetBufferPointer", (const void *)blob_pointer},
        {"ID3D10Blob::GetBufferSize", (const void *)blob_size},
    };
    for (const auto &c : code)
        if (!std::strcmp(c.name, name)) {
            if (c.next) *c.next = fallback;
            return c.fn;
        }
    return fallback;
}

// ---- DXMT's NVIDIA extension (a wrapped export, M3 Task 6) ----

// As DXMT's dxgi.cpp writes them in the guest's DXGI (InitializeVendorExtensionNV): the keys NVIDIA's DLSS loader and
// nvapi read, there with DXMT_ENABLE_NVEXT=1 and gone without; then native DXMT is asked (its answer is the host's).
extern "C" HRESULT WINAPI mn_px_DXGIGetDebugInterface1(UINT flags, REFIID iid, void **debug);
static const GUID nvext_guid = {0xba0af616, 0x4a43, 0x4259, {0x81, 0x5c, 0xdb, 0x3b, 0x89, 0x82, 0x99, 0x05}};

#ifndef MN_D3D_LOOPBACK
static void nvext_registry() {
    HKEY key1 = nullptr, key2 = nullptr, key3 = nullptr;
    const WCHAR *name1 = L"{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}", *name2 = L"FullPath";
    WCHAR value3[] = L"C:\\Windows\\System32";
    const char *on = getenv("DXMT_ENABLE_NVEXT");
    if (!RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global", 0, nullptr, 0, KEY_ALL_ACCESS,
                         nullptr, &key1, nullptr) &&
        !RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\ControlSet001\\Services\\nvlddmkm", 0, nullptr, 0,
                         KEY_ALL_ACCESS, nullptr, &key2, nullptr) &&
        !RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", 0, nullptr, 0,
                         KEY_ALL_ACCESS, nullptr, &key3, nullptr)) {
        if (!on || std::strcmp(on, "1")) {
            RegDeleteValueW(key1, name1);
            RegDeleteValueW(key2, name1);
            RegDeleteValueW(key3, name2);
        } else {
            DWORD one = 1;
            RegSetValueExW(key1, name1, 0, REG_DWORD, (const BYTE *)&one, sizeof one);
            RegSetValueExW(key2, name1, 0, REG_DWORD, (const BYTE *)&one, sizeof one);
            RegSetValueExW(key3, name2, 0, REG_SZ, (const BYTE *)value3, sizeof value3);
        }
    }
    if (key1) RegCloseKey(key1);
    if (key2) RegCloseKey(key2);
    if (key3) RegCloseKey(key3);
}
#else
static void nvext_registry() {}
#endif

static std::once_flag nvext_once;

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid, void **debug) {
    if (!std::memcmp(&iid, &nvext_guid, sizeof iid)) std::call_once(nvext_once, nvext_registry);
    return mn_px_DXGIGetDebugInterface1(flags, iid, debug);
}

#ifndef MN_D3D_LOOPBACK
// As DXMT's dxgi.dll does at load: the keys are there before the game looks for NVIDIA's loader.
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, void *) {
    if (reason == DLL_PROCESS_ATTACH) std::call_once(nvext_once, nvext_registry);
    return TRUE;
}
#endif

#ifdef MN_D3D_LOOPBACK
// The loopback library: the unix side is in the same library.
extern "C" int32_t mn_d3d_unix_call(void *args);
static int32_t call(uint32_t id, uint32_t flags, void *params) {
    mn_d3d_args a = {id, flags, (uintptr_t)params};
    return mn_d3d_unix_call(&a);
}
#else
// mnd3d.dll: the guest winemetal.so's slot MN_WM_D3D, through winemetal.dll (DXMT 0079), a plain DLL having no unix
// lib of its own.
extern "C" __declspec(dllimport) uint32_t WMTForward(uint32_t call, void *params);
static int32_t call(uint32_t id, uint32_t flags, void *params) {
    mn_d3d_args a = {id, flags, (uintptr_t)params};
    return (int32_t)WMTForward(155 /* MN_WM_D3D */, &a);
}
#endif
extern "C" int32_t mn_d3d_call(uint32_t id, void *params) { return call(id, 0, params); }

int32_t mn_d3d_record(mn_proxy *This, uint32_t id, void *params) {
    static thread_local char me; // the calling thread, by an address of its own
    bool handoff = This->obj->recorder.exchange(&me, std::memory_order_relaxed) != &me;
    return call(id, handoff ? MN_D3D_ARG_HANDOFF : 0, params);
}
