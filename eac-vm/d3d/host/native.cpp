// The dispatcher's calls on native DXMT's objects (dispatch.c is C): releases, QueryInterface, and a creation's
// answers.
#include <atomic>
#include <cstdio>
#include "stubs.h"

extern "C" void mn_d3d_native_release(void *obj) { ((IUnknown *)obj)->Release(); }

extern "C" int32_t mn_d3d_native_query(void *obj, const uint8_t iid[16], void **out) {
    return ((IUnknown *)obj)->QueryInterface(*(const IID *)iid, out);
}

extern "C" const void *mn_d3d_native_blob(void *obj, uint64_t *size) {
    *size = ((ID3D10Blob *)obj)->GetBufferSize();
    return ((ID3D10Blob *)obj)->GetBufferPointer();
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
    if (SUCCEEDED(r)) mn_d3d_fence_raise(fence, fence->GetCompletedValue());
    RESULT(a, r);
    return 0;
}

// A relay raises the slot when fence gets to value, by whatever DXMT does to it.
static void raise_later(ID3D12Fence *fence, uint64_t value) {
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

extern "C" void mn_d3d_answers(void *obj, int iface, uint64_t out[4]) {
    std::memset(out, 0, 4 * sizeof *out);
    if (iface < 0) return;
    const mn_d3d_iface &f = mn_d3d_ifaces[iface];
    for (unsigned i = 0; i < f.nfamily; i++) {
        unsigned k = f.family[i];
        void *q = nullptr;
        if (FAILED(((IUnknown *)obj)->QueryInterface(*(const IID *)mn_d3d_ifaces[k].iid, &q)) || !q) continue;
        out[k / 64] |= 1ull << (k % 64);
        if (q != obj) out[2 + k / 64] |= 1ull << (k % 64);
        ((IUnknown *)q)->Release();
    }
}
