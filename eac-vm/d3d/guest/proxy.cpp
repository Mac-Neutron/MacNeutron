// Proxy objects (see proxy.h). An object counts its references in the guest; its last Release lets the host's go (the
// family's op 0). QueryInterface is answered from what the host said at the creation: an interface at the same pointer
// is another proxy of the same object, one at another pointer (a companion) a new handle from the host (op 4).
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "proxy.h"
#ifndef MN_D3D_LOOPBACK
#include "wsi_window.hpp" // DXMT's own Win32 window code, built in (build-guest.sh): the window's changes, made here
#endif

struct mn_obj {
    std::atomic<long> refs{1};
    uint64_t handle = 0;
    int created = 0; // the interface it was made as: its IUnknown
    uint64_t answers[2] = {}, moved[2] = {};
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
};

// The objects by handle, for identity (mn_proxy_out); an object leaves at its last Release, under the same lock.
static std::mutex objs_lock;
static std::unordered_map<uint64_t, mn_obj *> objs;

static const IID unknown_iid = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static bool has(const uint64_t mask[2], int k) { return k >= 0 && (mask[k / 64] >> (k % 64) & 1); }

static mn_proxy *proxy_of(mn_obj *o, int k) {
    std::lock_guard<std::mutex> l(o->m);
    if (!o->proxies[k]) o->proxies[k] = new mn_proxy{mn_d3d_vtables[k], MN_D3D_PROXY_MAGIC, k, o->handle, o};
    return o->proxies[k];
}

// A new object for handle, made as k (-1: unknown, its first answer), answering answers (moved: at another pointer).
static mn_obj *object(uint64_t handle, int k, const uint64_t answers[2], const uint64_t moved[2]) {
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

void *mn_proxy_out(uint64_t handle, int iface, const IID *riid, const uint64_t ifaces[5]) {
    int k = iface >= 0 ? iface : riid ? mn_d3d_iface_of(riid) : -1;
    mn_obj *have = nullptr;
    // The object we have, if it answers k at its own pointer (a companion QueryInterface made knows only that one).
    if (ifaces[4] && ifaces[4] != handle) {
        std::lock_guard<std::mutex> l(objs_lock);
        auto it = objs.find(ifaces[4]);
        mn_obj *o = it != objs.end() ? it->second : nullptr;
        if (o && o->refs > 0 && (k < 0 || (has(o->answers, k) && !has(o->moved, k)))) (have = o)->refs++;
    }
    if (have) { // its proxy, and the host lets the new handle go
        uint64_t h = handle;
        mn_d3d_call(MN_D3D_OP_RELEASE, &h);
        return proxy_of(have, k < 0 ? have->created : k);
    }
    mn_obj *o = object(handle, k, ifaces, ifaces + 2);
    return proxy_of(o, o->created);
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
    int k = std::memcmp(&riid, &unknown_iid, sizeof riid) ? mn_d3d_iface_of(&riid) : o->created;
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
    mn_d3d_query_args q = {};
    q.handle = o->handle;
    std::memcpy(q.iid, &riid, sizeof q.iid);
    int32_t s = mn_d3d_call(MN_D3D_OP_QUERY, &q);
    HRESULT hr = s ? mn_d3d_hr(s) : (HRESULT)q.hr;
    if (FAILED(hr) || !q.out) return FAILED(hr) ? hr : E_NOINTERFACE;
    uint64_t one[2] = {};
    one[k / 64] = 1ull << (k % 64);
    mn_obj *c = object(q.out, k, one, nullptr); // a companion: an object of its own
    *out = proxy_of(c, k);
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

static void raise(std::atomic<uint64_t> *slot, uint64_t v) {
    uint64_t cur = slot->load();
    while (cur < v && !slot->compare_exchange_weak(cur, v)) {}
}

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

// A CPU signal: complete at once, here too, before the host hears of it.
static HRESULT STDMETHODCALLTYPE fence_signal(mn_proxy *This, UINT64 value) {
    if (This->obj->mirror) raise(This->obj->mirror, value);
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

// The host's relay posts the event, which the unix side's listener sets. NULL: we wait here, never the host.
static HRESULT on_event(HANDLE event, HRESULT (*send)(void *, HANDLE), void *ctx) {
#ifndef MN_D3D_LOOPBACK
    static INIT_ONCE listening = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&listening, start_listener, nullptr, nullptr);
#endif
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
    for (uint32_t i = 0; i < a.len / sizeof *v; i++) {
        const mn_d3d_action &x = v[i];
        HWND w = (HWND)(uintptr_t)x.hwnd;
        HMONITOR m = (HMONITOR)(uintptr_t)x.monitor;
        wsi::WsiMode mode = {x.width, x.height, {x.refresh_num, x.refresh_den}, x.bits_per_pixel, x.interlaced != 0};
        std::lock_guard<std::mutex> l(o->m);
        switch (x.kind) {
        case MN_D3D_ACTION_SAVE: wsi::saveWindowState(w, &o->saved); break;
        case MN_D3D_ACTION_SET_MODE: wsi::setWindowMode(m, w, mode); break;
        case MN_D3D_ACTION_ENTER: wsi::enterFullscreenMode(m, w, &o->saved, x.arg != 0); break;
        case MN_D3D_ACTION_LEAVE: wsi::leaveFullscreenMode(w, &o->saved, x.arg != 0); break;
        case MN_D3D_ACTION_RESTORE_MODE: wsi::restoreDisplayMode(m); break;
        case MN_D3D_ACTION_RESIZE: wsi::resizeWindow(w, &o->saved, x.width, x.height); break;
        case MN_D3D_ACTION_UPDATE_FULLSCREEN: wsi::updateFullscreenWindow(m, w, x.arg != 0); break;
        case MN_D3D_ACTION_MINIMIZE: wsi::minimizeWindow(w); break;
        }
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
    if (SUCCEEDED(hr) && out && *out) replay(((mn_proxy *)*out)->obj);
    return hr;
}

// Resizing the buffers resets the source size (DXGI's).
static void resized(mn_obj *o) {
    std::lock_guard<std::mutex> l(o->m);
    o->source_width = o->source_height = 0;
}

static HRESULT STDMETHODCALLTYPE resize(mn_proxy *This, UINT count, UINT width, UINT height, DXGI_FORMAT format,
                                        UINT flags) {
    push_window(This->obj->window, This->obj);
    HRESULT hr = PX(resize, px_resize)(This, count, width, height, format, flags);
    if (SUCCEEDED(hr)) resized(This->obj);
    return hr;
}

static HRESULT STDMETHODCALLTYPE resize1(mn_proxy *This, UINT count, UINT width, UINT height, DXGI_FORMAT format,
                                         UINT flags, const UINT *node_mask, IUnknown *const *queues) {
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
    HRESULT hr = PX(present, px_present)(This, sync, flags);
    if (minimised && !(flags & DXGI_PRESENT_TEST)) Sleep(16);
    return hr;
}

static HRESULT STDMETHODCALLTYPE present1(mn_proxy *This, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *p) {
    bool minimised = push_window(This->obj->window, This->obj, true);
    HRESULT hr = PX(present1, px_present1)(This, sync, flags, p);
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

#ifdef MN_D3D_LOOPBACK
// The loopback library: the unix side is in the same library.
extern "C" int32_t mn_d3d_unix_call(void *args);
extern "C" int32_t mn_d3d_call(uint32_t id, void *params) {
    mn_d3d_args a = {id, 0, (uintptr_t)params};
    return mn_d3d_unix_call(&a);
}
#else
// mnd3d.dll: the guest winemetal.so's slot MN_WM_D3D, through winemetal.dll (DXMT 0079), a plain DLL having no unix
// lib of its own.
extern "C" __declspec(dllimport) uint32_t WMTForward(uint32_t call, void *params);
extern "C" int32_t mn_d3d_call(uint32_t id, void *params) {
    mn_d3d_args a = {id, 0, (uintptr_t)params};
    return (int32_t)WMTForward(155 /* MN_WM_D3D */, &a);
}
#endif
