#include "common.hlsli"
// Shader Model 6.8's Barrier() (ops 244, 245): barriers.hlsl with each legacy barrier as its Barrier() form, modes 8, 9,
// 2, 3 (through the resource: 245), 10, 11, so the results and the fences must be barriers.hlsl's.
groupshared uint Shared[64];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    Shared[gi] = InWord(gi);
    Barrier(GROUP_SHARED_MEMORY, GROUP_SCOPE); Barrier(GROUP_SHARED_MEMORY, GROUP_SYNC | GROUP_SCOPE);
    Put(id, 0, Shared[63 - gi]);
    RWTyped[gi] = gi * 3 + 1;
    Barrier(UAV_MEMORY, DEVICE_SCOPE); Barrier(RWTyped, GROUP_SYNC | DEVICE_SCOPE);
    Put(id, 1, RWTyped[(gi + 1) & 63]);
    Shared[gi] = gi + 7; RWTyped[gi] = gi * 5;
    Barrier(UAV_MEMORY | GROUP_SHARED_MEMORY, DEVICE_SCOPE);
    Barrier(UAV_MEMORY | GROUP_SHARED_MEMORY, GROUP_SYNC | DEVICE_SCOPE);
    Put(id, 2, Shared[(gi + 2) & 63] + RWTyped[(gi + 3) & 63]);
}
