// Proxy objects (see proxy.h). An object counts its references in the guest; its last Release lets the host's go (the
// family's op 0). QueryInterface is answered from what the host said at the creation: an interface at the same pointer
// is another proxy of the same object, one at another pointer (a companion) a new handle from the host (op 4).
#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>
#include "proxy.h"

struct mn_obj {
    std::atomic<long> refs{1};
    uint64_t handle = 0;
    int created = 0; // the interface it was made as: its IUnknown
    uint64_t answers[2] = {}, moved[2] = {};
    std::mutex m;
    mn_proxy *proxies[MN_D3D_IFACES] = {};
    std::vector<uint8_t> blob; // an ID3D10Blob's bytes, read once
    bool blob_read = false;
};

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
    return o;
}

void *mn_proxy_out(uint64_t handle, int iface, const IID *riid, const uint64_t ifaces[4]) {
    int k = iface >= 0 ? iface : riid ? mn_d3d_iface_of(riid) : -1;
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
    long n = --o->refs;
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

const void *mn_d3d_special(const char *name, const void *fallback) {
    static const struct { const char *name; const void *fn; } code[] = {
        {"ID3D12Resource::Map", (const void *)map},
        {"ID3D10Blob::GetBufferPointer", (const void *)blob_pointer},
        {"ID3D10Blob::GetBufferSize", (const void *)blob_size},
    };
    for (const auto &c : code)
        if (!std::strcmp(c.name, name)) return c.fn;
    return fallback;
}

#ifdef MN_D3D_LOOPBACK
// The loopback library: the unix side is in the same library (in mnd3d.dll it is winemetal's WMTForward).
extern "C" int32_t mn_d3d_unix_call(void *args);
extern "C" int32_t mn_d3d_call(uint32_t id, void *params) {
    mn_d3d_args a = {id, 0, (uintptr_t)params};
    return mn_d3d_unix_call(&a);
}
#endif
