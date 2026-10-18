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
