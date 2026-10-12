// 64-bit groupshared atomics with no threadgroup memory left for a lock (DXIL SM 6.8 plan, Task 14c): Big fills all
// 32768 bytes, so the translator refuses the shader by name.
#include "../common.hlsli"
groupshared uint64_t Big[4096];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    Big[gi] = 0;
    GroupMemoryBarrierWithGroupSync();
    InterlockedAdd(Big[gi & 7], (uint64_t)1);
    GroupMemoryBarrierWithGroupSync();
    Put(id, 0, (uint)Big[gi]);
}
