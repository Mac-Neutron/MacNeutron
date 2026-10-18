// The dispatcher's calls on native DXMT's objects (dispatch.c is C): releases, QueryInterface, and a creation's
// answers.
#include "stubs.h"

extern "C" void mn_d3d_native_release(void *obj) { ((IUnknown *)obj)->Release(); }

extern "C" int32_t mn_d3d_native_query(void *obj, const uint8_t iid[16], void **out) {
    return ((IUnknown *)obj)->QueryInterface(*(const IID *)iid, out);
}

extern "C" int mn_d3d_iface_of(const void *iid) {
    for (int i = 0; i < MN_D3D_IFACES; i++)
        if (!std::memcmp(mn_d3d_ifaces[i].iid, iid, 16)) return i;
    return -1;
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
