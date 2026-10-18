// The dispatcher's calls on native DXMT's objects (dispatch.c is C): releases, QueryInterface, and a creation's
// answers.
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "stubs.h"
#include "../mn_d3d.h"

extern "C" void mn_d3d_native_release(void *obj) { ((IUnknown *)obj)->Release(); }

// COM identity: the first handle each native object was bound to, and how many handles it has, so a method that hands
// back an object the guest already has (GetDevice, a pipeline library's stored pipeline) answers that handle and the
// guest its proxy. Keyed by the object alone: native DXMT shares no object between contexts (it has no singletons),
// and a handle's release doesn't know its context; one shared anyway would answer only its first context.
struct identity { uint32_t ctx_id; uint64_t handle; unsigned bindings; };
static std::mutex identity_lock;
static std::unordered_map<void *, identity> identities;

extern "C" uint64_t mn_d3d_identity_of(uint32_t ctx_id, void *obj) {
    std::lock_guard<std::mutex> l(identity_lock);
    auto it = identities.find(obj);
    return it != identities.end() && it->second.ctx_id == ctx_id ? it->second.handle : 0;
}

extern "C" void mn_d3d_identity_add(uint32_t ctx_id, void *obj, uint64_t handle) {
    std::lock_guard<std::mutex> l(identity_lock);
    identities.try_emplace(obj, identity{ctx_id, handle, 0}).first->second.bindings++;
}

// A binding's release (each holds its own reference).
extern "C" void mn_d3d_identity_drop(void *obj) {
    std::lock_guard<std::mutex> l(identity_lock);
    auto it = identities.find(obj);
    if (it != identities.end() && !--it->second.bindings) identities.erase(it);
}

extern "C" int32_t mn_d3d_native_query(void *obj, const uint8_t iid[16], void **out) {
    return ((IUnknown *)obj)->QueryInterface(*(const IID *)iid, out);
}

// An ID3D10Blob's bytes and size, or NULL: obj isn't one.
extern "C" const void *mn_d3d_native_blob(void *obj, uint64_t *size) {
    ID3D10Blob *blob = nullptr;
    if (FAILED(((IUnknown *)obj)->QueryInterface(__uuidof(ID3D10Blob), (void **)&blob))) return nullptr;
    *size = blob->GetBufferSize();
    const void *bytes = blob->GetBufferPointer();
    blob->Release(); // the handle holds it
    return bytes;
}

// Map (rules.txt's own): the arena's (blob, offset) where the host's pointer is, for the guest's own address of it.
extern "C" NTSTATUS_T mn_d3d_map_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Resource_Map *)params;
    auto *self = (ID3D12Resource *)(uintptr_t)a->self;
    auto *at = (uint64_t *)(uintptr_t)a->data;
    void *ptr = nullptr;
    HRESULT r = self->Map(a->sub_resource, (const D3D12_RANGE *)(uintptr_t)a->read_range, at ? &ptr : nullptr);
    if (SUCCEEDED(r) && at && mn_d3d_arena_of(ptr, &at[0], &at[1])) {
        static std::atomic<int> said;
        if (!said++) fprintf(stderr, "mn-d3d: error: ID3D12Resource::Map: memory outside the arenas\n");
        self->Unmap(a->sub_resource, nullptr);
        r = E_FAIL;
    }
    std::memcpy(&a->ret, &r, sizeof r);
    mn_d3d_result(r);
    return 0;
}

// ---- fences (rules.txt's own, Task 8) ----

#define RESULT(a, r)                                                                                                   \
    do {                                                                                                               \
        HRESULT r_ = (r);                                                                                              \
        std::memcpy(&(a)->ret, &r_, sizeof r_);                                                                        \
        mn_d3d_result(r_);                                                                                             \
    } while (0)

// A fence and its mirror slot (aux[0]; UINT64_MAX: none, the guest asks the host for its value).
extern "C" NTSTATUS_T mn_d3d_create_fence_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Device_CreateFence *)params;
    auto *riid = (const IID *)(uintptr_t)a->riid;
    HRESULT r = ((ID3D12Device *)(uintptr_t)a->self)
                    ->CreateFence(a->initial_value, (D3D12_FENCE_FLAGS)a->flags, *riid, (void **)&a->fence);
    a->aux[0] = UINT64_MAX;
    if (SUCCEEDED(r) && a->fence) {
        mn_d3d_answers((void *)(uintptr_t)a->fence, mn_d3d_iface_of(riid), a->fence_ifaces);
        a->aux[0] = mn_d3d_fence_new(mn_d3d_current_ctx(), (void *)(uintptr_t)a->fence, a->initial_value);
    }
    RESULT(a, r);
    return 0;
}

// A CPU signal completes at once: the slot too.
extern "C" NTSTATUS_T mn_d3d_fence_signal_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Fence_Signal *)params;
    auto *fence = (ID3D12Fence *)(uintptr_t)a->self;
    HRESULT r = fence->Signal(a->value);
    if (SUCCEEDED(r)) mn_d3d_fence_set(fence, fence->GetCompletedValue()); // lower too, as the CPU's signal can
    RESULT(a, r);
    return 0;
}

// A relay raises the slot when fence gets to value, by whatever DXMT does to it (D3D12's fence or D3D11's).
template <class F> static void raise_later(F *fence, uint64_t value) {
    if (void *relay = fence ? mn_d3d_fence_relay(fence, value) : nullptr) {
        fence->SetEventOnCompletion(value, relay);
        mn_d3d_relay_done(relay);
    }
}

// The GPU's: a relay raises the slot when the fence gets there.
extern "C" NTSTATUS_T mn_d3d_queue_signal_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12CommandQueue_Signal *)params;
    auto *fence = (ID3D12Fence *)(uintptr_t)a->fence;
    HRESULT r = ((ID3D12CommandQueue *)(uintptr_t)a->self)->Signal(fence, a->value);
    if (SUCCEEDED(r)) raise_later(fence, a->value);
    RESULT(a, r);
    return 0;
}

extern "C" NTSTATUS_T mn_d3d_enqueue_resident_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Device3_EnqueueMakeResident *)params;
    auto *fence = (ID3D12Fence *)(uintptr_t)a->fence;
    HRESULT r = ((ID3D12Device3 *)(uintptr_t)a->self)
                    ->EnqueueMakeResident((D3D12_RESIDENCY_FLAGS)a->flags, a->num_objects,
                                          (ID3D12Pageable *const *)(uintptr_t)a->objects, fence, a->fence_value);
    if (SUCCEEDED(r)) raise_later(fence, a->fence_value);
    RESULT(a, r);
    return 0;
}

// The guest's event, set by the guest when a relay posts it (a NULL event waits in the guest: never here).
extern "C" NTSTATUS_T mn_d3d_set_event_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Fence_SetEventOnCompletion *)params;
    void *relay = a->event ? mn_d3d_event_relay(mn_d3d_current_ctx(), a->event) : nullptr;
    HRESULT r = relay ? ((ID3D12Fence *)(uintptr_t)a->self)->SetEventOnCompletion(a->value, relay) : E_INVALIDARG;
    if (relay) mn_d3d_relay_done(relay);
    RESULT(a, r);
    return 0;
}

extern "C" NTSTATUS_T mn_d3d_set_event_multi_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Device1_SetEventOnMultipleFenceCompletion *)params;
    void *relay = a->event ? mn_d3d_event_relay(mn_d3d_current_ctx(), a->event) : nullptr;
    HRESULT r = relay ? ((ID3D12Device1 *)(uintptr_t)a->self)
                            ->SetEventOnMultipleFenceCompletion((ID3D12Fence *const *)(uintptr_t)a->fences,
                                                                (const UINT64 *)(uintptr_t)a->values, a->fence_count,
                                                                (D3D12_MULTIPLE_FENCE_WAIT_FLAGS)a->flags, relay)
                      : E_INVALIDARG;
    if (relay) mn_d3d_relay_done(relay);
    RESULT(a, r);
    return 0;
}

// D3D11's (M4 Task 6), as D3D12's: a fence and its slot, the context's GPU signal, the guest's events as relays.
extern "C" NTSTATUS_T mn_d3d_create_fence11_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D11Device5_CreateFence *)params;
    auto *riid = (const IID *)(uintptr_t)a->iid;
    HRESULT r = ((ID3D11Device5 *)(uintptr_t)a->self)
                    ->CreateFence(a->initial_value, (D3D11_FENCE_FLAG)a->flags, *riid, (void **)&a->fence);
    a->aux[0] = UINT64_MAX;
    if (SUCCEEDED(r) && a->fence) {
        mn_d3d_answers((void *)(uintptr_t)a->fence, mn_d3d_iface_of(riid), a->fence_ifaces);
        a->aux[0] = mn_d3d_fence_new(mn_d3d_current_ctx(), (void *)(uintptr_t)a->fence, a->initial_value);
    }
    RESULT(a, r);
    return 0;
}

extern "C" NTSTATUS_T mn_d3d_context_signal_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D11DeviceContext4_Signal *)params;
    auto *fence = (ID3D11Fence *)(uintptr_t)a->fence;
    HRESULT r = ((ID3D11DeviceContext4 *)(uintptr_t)a->self)->Signal(fence, a->value);
    if (SUCCEEDED(r)) raise_later(fence, a->value);
    RESULT(a, r);
    return 0;
}

extern "C" NTSTATUS_T mn_d3d_set_event11_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D11Fence_SetEventOnCompletion *)params;
    void *relay = a->event ? mn_d3d_event_relay(mn_d3d_current_ctx(), a->event) : nullptr;
    HRESULT r = relay ? ((ID3D11Fence *)(uintptr_t)a->self)->SetEventOnCompletion(a->value, relay) : E_INVALIDARG;
    if (relay) mn_d3d_relay_done(relay);
    RESULT(a, r);
    return 0;
}

// Flush1's event, set once the flushed work is done: a relay (none without one).
extern "C" NTSTATUS_T mn_d3d_flush1_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D11DeviceContext3_Flush1 *)params;
    void *relay = a->event ? mn_d3d_event_relay(mn_d3d_current_ctx(), a->event) : nullptr;
    try {
        ((ID3D11DeviceContext3 *)(uintptr_t)a->self)->Flush1((D3D11_CONTEXT_TYPE)a->type, relay);
    } catch (...) {
        if (relay) mn_d3d_relay_done(relay);
        return mn_d3d_not_in_dxmt(MN_D3D_ID_ID3D11DeviceContext3_Flush1, nullptr);
    }
    if (relay) mn_d3d_relay_done(relay);
    return 0;
}

// ---- CheckFeatureSupport (own, Task 10) ----

extern "C" NTSTATUS_T mn_d3d_check_feature_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D12Device_CheckFeatureSupport *)params;
    auto *data = (uint8_t *)(uintptr_t)a->feature_data;
    UINT size = a->feature_data_size;
    if (a->feature == D3D12_FEATURE_FEATURE_LEVELS) { // its levels follow it, or its pointer is the guest's: refused
        auto *levels = (D3D12_FEATURE_DATA_FEATURE_LEVELS *)data;
        if (!data || a->aux[0] != sizeof *levels || size < sizeof *levels + 4ull * levels->NumFeatureLevels) {
            RESULT(a, E_INVALIDARG);
            return 0;
        }
        levels->pFeatureLevelsRequested = (const D3D_FEATURE_LEVEL *)(data + sizeof *levels);
        size = sizeof *levels;
    }
    HRESULT r = ((ID3D12Device *)(uintptr_t)a->self)->CheckFeatureSupport((D3D12_FEATURE)a->feature, data, size);
    RESULT(a, r);
    return 0;
}

// ---- DXGI's descriptions (own, Task 10): the WCHAR array first, the rest laid out as the guest's past it ----

#ifdef MN_D3D_LOOPBACK
static const size_t guest_wchar = 4; // the loopback's guest is native code
#else
static const size_t guest_wchar = 2;
#endif

template <class D> static void to_guest(const D &n, const WCHAR *w, size_t wchars, void *out) {
    auto *g = (uint8_t *)out;
    if (guest_wchar == sizeof(WCHAR)) {
        std::memcpy(g, &n, sizeof n);
        return;
    }
    auto *u = (uint16_t *)g; // UTF-16, NUL-terminated within the array
    size_t k = 0;
    for (size_t i = 0; i < wchars && w[i]; i++) {
        uint32_t c = (uint32_t)w[i];
        if (c >= 0x10000 && k + 2 < wchars) {
            c -= 0x10000;
            u[k++] = (uint16_t)(0xd800 + (c >> 10)), u[k++] = (uint16_t)(0xdc00 + (c & 0x3ff));
        } else if (c < 0x10000 && k + 1 < wchars) {
            u[k++] = (uint16_t)c;
        }
    }
    std::memset(u + k, 0, (wchars - k) * 2);
    std::memcpy(g + 2 * wchars, (const uint8_t *)&n + sizeof(WCHAR) * wchars, sizeof n - sizeof(WCHAR) * wchars);
}

#define DESCRIPTION(stub, iface, method, type, field, param)                                                          \
    extern "C" NTSTATUS_T stub(void *params) {                                                                         \
        auto *a = (mn_d3d_p_##iface##_##method *)params;                                                              \
        static_assert(offsetof(type, field) == 0, #type);                                                              \
        type n = {};                                                                                                   \
        HRESULT r = ((iface *)(uintptr_t)a->self)->method(a->param ? &n : nullptr);                                     \
        if (SUCCEEDED(r) && a->param) to_guest(n, n.field, sizeof n.field / sizeof *n.field, (void *)(uintptr_t)a->param); \
        RESULT(a, r);                                                                                                  \
        return 0;                                                                                                      \
    }
DESCRIPTION(mn_d3d_adapter_desc_stub, IDXGIAdapter, GetDesc, DXGI_ADAPTER_DESC, Description, desc)
DESCRIPTION(mn_d3d_adapter_desc1_stub, IDXGIAdapter1, GetDesc1, DXGI_ADAPTER_DESC1, Description, pDesc)
DESCRIPTION(mn_d3d_adapter_desc2_stub, IDXGIAdapter2, GetDesc2, DXGI_ADAPTER_DESC2, Description, pDesc)
DESCRIPTION(mn_d3d_adapter_desc3_stub, IDXGIAdapter4, GetDesc3, DXGI_ADAPTER_DESC3, Description, desc)
DESCRIPTION(mn_d3d_output_desc_stub, IDXGIOutput, GetDesc, DXGI_OUTPUT_DESC, DeviceName, desc)
DESCRIPTION(mn_d3d_output_desc1_stub, IDXGIOutput6, GetDesc1, DXGI_OUTPUT_DESC1, DeviceName, desc)

extern "C" void mn_d3d_objects_out(uint64_t *p, uint32_t n) {
    for (uint32_t i = 0; p && i < n; i++) {
        auto *obj = (IUnknown *)(uintptr_t)p[i];
        p[i] = obj ? mn_d3d_identity_of(mn_d3d_current_ctx(), obj) : 0;
        if (obj) obj->Release(); // the getter's reference: the guest holds its own
    }
}

extern "C" void mn_d3d_answers(void *obj, int iface, uint64_t out[9]) {
    std::memset(out, 0, 9 * sizeof *out);
    out[2 * MN_D3D_MASK_WORDS] = mn_d3d_identity_of(mn_d3d_current_ctx(), obj); // the guest's: its proxy, not a new one
    if (iface < 0) return;
    const mn_d3d_iface &f = mn_d3d_ifaces[iface];
    for (unsigned i = 0; i < f.nfamily; i++) {
        unsigned k = f.family[i];
        void *q = nullptr;
        if (FAILED(((IUnknown *)obj)->QueryInterface(*(const IID *)mn_d3d_ifaces[k].iid, &q)) || !q) continue;
        out[k / 64] |= 1ull << (k % 64);
        if (q != obj) out[MN_D3D_MASK_WORDS + k / 64] |= 1ull << (k % 64);
        ((IUnknown *)q)->Release();
    }
}

// ---- D3D11's and D3D10's data and maps (own, M4 Task 4) ----

// An interface's mn_d3d_ifaces index (natively __uuidof is a temporary).
template <class T> static int iface_of() {
    static const GUID iid = __uuidof(T);
    return mn_d3d_iface_of(&iid);
}

// A packed region (guest/proxy.cpp's pack: count, each subresource's mn_d3d_sub, their bytes), as DXMT takes it.
static bool unpacked(const void *flat, uint64_t len, std::vector<D3D11_SUBRESOURCE_DATA> &out) {
    auto *b = (const uint8_t *)flat;
    uint32_t n;
    if (!b || len < 8) return false;
    std::memcpy(&n, b, 4);
    if ((len - 8) / sizeof(mn_d3d_sub) < n) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        mn_d3d_sub s;
        std::memcpy(&s, b + 8 + i * sizeof s, sizeof s);
        if (s.offset > len || s.size > len - s.offset) return false;
        out[i] = {b + s.offset, s.pitch, s.slice};
    }
    return true;
}

#define CREATE_STUB(stub, NAME, Iface, method, Desc, Data, T, DESC, INIT, OUT)                                         \
    extern "C" NTSTATUS_T stub(void *params) {                                                                         \
        auto *a = (mn_d3d_p_##NAME *)params;                                                                           \
        std::vector<D3D11_SUBRESOURCE_DATA> subs;                                                                      \
        if (a->INIT && !unpacked((const void *)(uintptr_t)a->INIT, a->aux[0], subs)) {                                \
            RESULT(a, E_INVALIDARG);                                                                                   \
            return 0;                                                                                                  \
        }                                                                                                              \
        try {                                                                                                          \
            HRESULT r = ((Iface *)(uintptr_t)a->self)                                                                  \
                            ->method((const Desc *)(uintptr_t)a->DESC, a->INIT ? (const Data *)subs.data() : nullptr, \
                                     (T **)&a->OUT);                                                                   \
            if (SUCCEEDED(r) && a->OUT) mn_d3d_answers((void *)(uintptr_t)a->OUT, iface_of<T>(), a->OUT##_ifaces); \
            a->aux[1] = mn_d3d_memory_largest(); /* its allocation: what a map's rename makes (Task 7) */             \
            RESULT(a, r);                                                                                              \
        } catch (...) {                                                                                                \
            return mn_d3d_not_in_dxmt(MN_D3D_ID_##NAME, &a->ret);                                                      \
        }                                                                                                              \
        return 0;                                                                                                      \
    }
CREATE_STUB(mn_d3d_create_buffer11_stub, ID3D11Device_CreateBuffer, ID3D11Device, CreateBuffer, D3D11_BUFFER_DESC,
            D3D11_SUBRESOURCE_DATA, ID3D11Buffer, pDesc, pInitialData, ppBuffer)
CREATE_STUB(mn_d3d_create_tex1d11_stub, ID3D11Device_CreateTexture1D, ID3D11Device, CreateTexture1D,
            D3D11_TEXTURE1D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture1D, pDesc, pInitialData, ppTexture1D)
CREATE_STUB(mn_d3d_create_tex2d11_stub, ID3D11Device_CreateTexture2D, ID3D11Device, CreateTexture2D,
            D3D11_TEXTURE2D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture2D, pDesc, pInitialData, ppTexture2D)
CREATE_STUB(mn_d3d_create_tex3d11_stub, ID3D11Device_CreateTexture3D, ID3D11Device, CreateTexture3D,
            D3D11_TEXTURE3D_DESC, D3D11_SUBRESOURCE_DATA, ID3D11Texture3D, pDesc, pInitialData, ppTexture3D)
CREATE_STUB(mn_d3d_create_tex2d1_11_stub, ID3D11Device3_CreateTexture2D1, ID3D11Device3, CreateTexture2D1,
            D3D11_TEXTURE2D_DESC1, D3D11_SUBRESOURCE_DATA, ID3D11Texture2D1, desc, initial_data, texture)
CREATE_STUB(mn_d3d_create_tex3d1_11_stub, ID3D11Device3_CreateTexture3D1, ID3D11Device3, CreateTexture3D1,
            D3D11_TEXTURE3D_DESC1, D3D11_SUBRESOURCE_DATA, ID3D11Texture3D1, desc, initial_data, texture)
CREATE_STUB(mn_d3d_create_buffer10_stub, ID3D10Device_CreateBuffer, ID3D10Device, CreateBuffer, D3D10_BUFFER_DESC,
            D3D10_SUBRESOURCE_DATA, ID3D10Buffer, pDesc, pInitialData, ppBuffer)
CREATE_STUB(mn_d3d_create_tex1d10_stub, ID3D10Device_CreateTexture1D, ID3D10Device, CreateTexture1D,
            D3D10_TEXTURE1D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture1D, pDesc, pInitialData, ppTexture1D)
CREATE_STUB(mn_d3d_create_tex2d10_stub, ID3D10Device_CreateTexture2D, ID3D10Device, CreateTexture2D,
            D3D10_TEXTURE2D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture2D, pDesc, pInitialData, ppTexture2D)
CREATE_STUB(mn_d3d_create_tex3d10_stub, ID3D10Device_CreateTexture3D, ID3D10Device, CreateTexture3D,
            D3D10_TEXTURE3D_DESC, D3D10_SUBRESOURCE_DATA, ID3D10Texture3D, pDesc, pInitialData, ppTexture3D)

#define UPDATE_STUB(stub, NAME, Iface, method, Res, Box, EXTRA)                                                        \
    extern "C" NTSTATUS_T stub(void *params) {                                                                         \
        auto *a = (mn_d3d_p_##NAME *)params;                                                                           \
        try {                                                                                                          \
            ((Iface *)(uintptr_t)a->self)                                                                              \
                ->method((Res *)(uintptr_t)a->pDstResource, a->DstSubresource, (const Box *)(uintptr_t)a->pDstBox,    \
                         (const void *)(uintptr_t)a->pSrcData, a->SrcRowPitch, a->SrcDepthPitch EXTRA);               \
        } catch (...) {                                                                                                \
            return mn_d3d_not_in_dxmt(MN_D3D_ID_##NAME, nullptr);                                                      \
        }                                                                                                              \
        return 0;                                                                                                      \
    }
#define MN_COMMA ,
UPDATE_STUB(mn_d3d_update11_stub, ID3D11DeviceContext_UpdateSubresource, ID3D11DeviceContext, UpdateSubresource,
            ID3D11Resource, D3D11_BOX, )
UPDATE_STUB(mn_d3d_update1_11_stub, ID3D11DeviceContext1_UpdateSubresource1, ID3D11DeviceContext1, UpdateSubresource1,
            ID3D11Resource, D3D11_BOX, MN_COMMA a->CopyFlags)
UPDATE_STUB(mn_d3d_update10_stub, ID3D10Device_UpdateSubresource, ID3D10Device, UpdateSubresource, ID3D10Resource,
            D3D10_BOX, )

// Before a map (M4 Task 7): room in the arenas for a rename of the resource's allocation (alloc: its creation's,
// the guest's word; 0: unknown), or false, more wanted of the guest (the map answers MN_D3D_NEED_ARENA, and DXMT
// hasn't renamed: a map that renamed into host memory can't be taken back).
static bool room_for(uint64_t alloc) {
    if (!alloc || mn_d3d_arena_fits(mn_d3d_current_ctx(), alloc)) return true;
    mn_d3d_memory_want(alloc);
    return false;
}

// A map's memory as the arena's (blob, offset) in aux (the loopback's guest shares the host's memory: the pointer, and
// UINT64_MAX); false outside every arena.
static bool located(void *ptr, uint64_t aux[2]) {
    if (!mn_d3d_arena_of(ptr, &aux[0], &aux[1])) return true;
#ifdef MN_D3D_LOOPBACK
    aux[0] = (uintptr_t)ptr, aux[1] = UINT64_MAX;
    return true;
#else
    static std::atomic<int> said;
    if (!said++) fprintf(stderr, "mn-d3d: error: a D3D11 or D3D10 map: memory outside the arenas\n");
    return false;
#endif
}

extern "C" NTSTATUS_T mn_d3d_map11_stub(void *params) {
    auto *a = (mn_d3d_p_ID3D11DeviceContext_Map *)params;
    auto *ctx = (ID3D11DeviceContext *)(uintptr_t)a->self;
    auto *res = (ID3D11Resource *)(uintptr_t)a->pResource;
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (!room_for(a->aux[0])) return 0;
    try {
        HRESULT r = ctx->Map(res, a->Subresource, (D3D11_MAP)a->MapType, a->MapFlags, a->pMappedResource ? &m : nullptr);
        if (SUCCEEDED(r) && a->pMappedResource) {
            if (!located(m.pData, a->aux)) {
                ctx->Unmap(res, a->Subresource);
                r = E_FAIL;
            } else {
                std::memcpy((void *)(uintptr_t)a->pMappedResource, &m, sizeof m); // the pitches (pData: aux's)
            }
        }
        RESULT(a, r);
    } catch (...) {
        return mn_d3d_not_in_dxmt(MN_D3D_ID_ID3D11DeviceContext_Map, &a->ret);
    }
    return 0;
}

#define MAP10_STUB(stub, NAME, Iface, Out, OUT, SUB, UNMAP)                                                            \
    extern "C" NTSTATUS_T stub(void *params) {                                                                         \
        auto *a = (mn_d3d_p_##NAME *)params;                                                                           \
        auto *self = (Iface *)(uintptr_t)a->self;                                                                      \
        Out m = {};                                                                                                    \
        if (!room_for(a->aux[0])) return 0;                                                                            \
        try {                                                                                                          \
            HRESULT r = self->Map(SUB(D3D10_MAP)a->MapType, a->MapFlags, a->OUT ? &m : nullptr);                      \
            if (SUCCEEDED(r) && a->OUT) {                                                                              \
                if (!located(*(void **)&m, a->aux)) {                                                                  \
                    self->Unmap(UNMAP);                                                                                \
                    r = E_FAIL;                                                                                        \
                } else {                                                                                               \
                    std::memcpy((void *)(uintptr_t)a->OUT, &m, sizeof m);                                              \
                }                                                                                                      \
            }                                                                                                          \
            RESULT(a, r);                                                                                              \
        } catch (...) {                                                                                                \
            return mn_d3d_not_in_dxmt(MN_D3D_ID_##NAME, &a->ret);                                                      \
        }                                                                                                              \
        return 0;                                                                                                      \
    }
#define NO_SUB
#define WITH_SUB a->Subresource,
MAP10_STUB(mn_d3d_map10_buffer_stub, ID3D10Buffer_Map, ID3D10Buffer, void *, ppData, NO_SUB, )
MAP10_STUB(mn_d3d_map10_tex1d_stub, ID3D10Texture1D_Map, ID3D10Texture1D, void *, ppData, WITH_SUB, a->Subresource)
MAP10_STUB(mn_d3d_map10_tex2d_stub, ID3D10Texture2D_Map, ID3D10Texture2D, D3D10_MAPPED_TEXTURE2D, pMappedTex2D, WITH_SUB,
           a->Subresource)
MAP10_STUB(mn_d3d_map10_tex3d_stub, ID3D10Texture3D_Map, ID3D10Texture3D, D3D10_MAPPED_TEXTURE3D, pMappedTex3D, WITH_SUB,
           a->Subresource)
