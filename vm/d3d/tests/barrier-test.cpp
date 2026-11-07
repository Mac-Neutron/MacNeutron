// Enhanced barriers as legacy ones (DXMT's d3d12_barrier_legacy.hpp, command list 7 spec §5), natively: what DXMT's
// List7::Barrier and the guest's own Barrier hand to ResourceBarrier, and Device10's initial layouts as states.
// Prints "barrier-test: ok", or the failed case and exits 1.
#include <d3d12.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "d3d12_barrier_legacy.hpp"

using dxmt::InitialState;
using dxmt::LegacyBarriers;

static ID3D12Resource *const R = (ID3D12Resource *)0x1000;
static int failed = 0;

#define CHECK(c, expr) do { if (!(expr)) { printf("barrier-test: FAIL case %d: %s\n", c, #expr); failed = 1; } } while (0)

static std::vector<D3D12_RESOURCE_BARRIER> One(D3D12_BARRIER_GROUP g) {
    std::vector<D3D12_RESOURCE_BARRIER> out;
    LegacyBarriers(1, &g, out);
    return out;
}

static D3D12_BARRIER_GROUP Buffer(const D3D12_BUFFER_BARRIER &b) {
    D3D12_BARRIER_GROUP g = {D3D12_BARRIER_TYPE_BUFFER, 1};
    g.pBufferBarriers = &b;
    return g;
}

static D3D12_BARRIER_GROUP Texture(const D3D12_TEXTURE_BARRIER &b) {
    D3D12_BARRIER_GROUP g = {D3D12_BARRIER_TYPE_TEXTURE, 1};
    g.pTextureBarriers = &b;
    return g;
}

static D3D12_BARRIER_GROUP Global(const D3D12_GLOBAL_BARRIER &b) {
    D3D12_BARRIER_GROUP g = {D3D12_BARRIER_TYPE_GLOBAL, 1};
    g.pGlobalBarriers = &b;
    return g;
}

static bool Transition(const std::vector<D3D12_RESOURCE_BARRIER> &out, D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
    return out.size() == 1 && out[0].Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION && out[0].Transition.pResource == R &&
           out[0].Transition.Subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES &&
           out[0].Transition.StateBefore == before && out[0].Transition.StateAfter == after;
}

static bool Uav(const std::vector<D3D12_RESOURCE_BARRIER> &out, ID3D12Resource *r) {
    return out.size() == 1 && out[0].Type == D3D12_RESOURCE_BARRIER_TYPE_UAV && out[0].UAV.pResource == r;
}

static bool AnyAliasing(const std::vector<D3D12_RESOURCE_BARRIER> &out) {
    return out.size() == 1 && out[0].Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING && !out[0].Aliasing.pResourceBefore &&
           !out[0].Aliasing.pResourceAfter;
}

int main() {
    const auto SR = D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // 1-2: a write that ends, a texture's render target read as a shader resource
    CHECK(1, Transition(One(Buffer({D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_SYNC_COPY,
                                    D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_COPY_SOURCE, R, 0, 256})),
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
    D3D12_TEXTURE_BARRIER t = {D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_SYNC_PIXEL_SHADING,
                               D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                               D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE, R};
    CHECK(2, Transition(One(Texture(t)), D3D12_RESOURCE_STATE_RENDER_TARGET, SR));
    // 3: nothing earlier to wait on; 4: no access before, but a scope to wait for (an execution dependency): any
    CHECK(3, One(Buffer({D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_NO_ACCESS,
                         D3D12_BARRIER_ACCESS_COPY_DEST, R, 0, 256})).empty());
    CHECK(4, AnyAliasing(One(Buffer({D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_NO_ACCESS,
                                     D3D12_BARRIER_ACCESS_COPY_DEST, R, 0, 256}))));
    // 5-7: global barriers name no resource
    CHECK(5, Uav(One(Global({D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_SYNC_PIXEL_SHADING,
                             D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_SHADER_RESOURCE})), nullptr));
    CHECK(6, AnyAliasing(One(Global({D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_SYNC_PIXEL_SHADING,
                                     D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_ACCESS_SHADER_RESOURCE}))));
    CHECK(7, One(Global({D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                         D3D12_BARRIER_ACCESS_COPY_SOURCE})).empty());
    // 14-16: a global read then a write still orders (write after read): before UAV writes a UAV barrier, else any;
    // nothing after, nothing
    CHECK(14, Uav(One(Global({D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                              D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS})), nullptr));
    CHECK(15, AnyAliasing(One(Global({D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_RENDER_TARGET,
                                      D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_ACCESS_RENDER_TARGET}))));
    CHECK(16, One(Global({D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_SHADER_RESOURCE,
                          D3D12_BARRIER_ACCESS_NO_ACCESS})).empty());
    // 8: UAV to UAV is a UAV barrier on the resource
    CHECK(8, Uav(One(Buffer({D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_SYNC_COMPUTE_SHADING,
                             D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, R, 0, 256})), R));
    // 9: an access with no legacy state (acceleration structures): any resource
    t.AccessBefore = D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
    CHECK(9, AnyAliasing(One(Texture(t))));
    // 10-11: no access after (a write still ends, as COMMON), COMMON before
    t.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET, t.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    CHECK(10, Transition(One(Texture(t)), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON));
    CHECK(11, Transition(One(Buffer({D3D12_BARRIER_SYNC_ALL, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COMMON,
                                     D3D12_BARRIER_ACCESS_COPY_DEST, R, 0, 256})),
                         D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    // 12: no groups, and a group of no barriers: nothing, and an earlier call's barriers are gone
    std::vector<D3D12_RESOURCE_BARRIER> out(3);
    LegacyBarriers(0, nullptr, out);
    CHECK(12, out.empty());
    D3D12_BARRIER_GROUP empty = {D3D12_BARRIER_TYPE_BUFFER, 0};
    out.resize(2);
    LegacyBarriers(1, &empty, out);
    CHECK(12, out.empty());
    // 13: Device10's initial layouts as the states ValidateResourceStates wants
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_UNDEFINED, D3D12_HEAP_TYPE_UPLOAD) == D3D12_RESOURCE_STATE_GENERIC_READ);
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_UNDEFINED, D3D12_HEAP_TYPE_READBACK) == D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_UNDEFINED, D3D12_HEAP_TYPE_DEFAULT) == D3D12_RESOURCE_STATE_COMMON);
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_RENDER_TARGET, D3D12_HEAP_TYPE_DEFAULT) ==
                  D3D12_RESOURCE_STATE_RENDER_TARGET);
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_SHADER_RESOURCE, D3D12_HEAP_TYPE_DEFAULT) == SR);
    CHECK(13, InitialState(D3D12_BARRIER_LAYOUT_COMPUTE_QUEUE_UNORDERED_ACCESS, D3D12_HEAP_TYPE_DEFAULT) ==
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (failed) return 1;
    printf("barrier-test: ok\n");
    return 0;
}
