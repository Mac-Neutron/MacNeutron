// A 64-bit groupshared atomic (DXIL SM 6.8 plan, Task 9): an LLVM atomicrmw on addrspace(3), no dx.op. Metal has no
// threadgroup ulong atomics and its compiler service crashes on this one (i64 spike), so the translator refuses it by
// name until Task 14c's lock path.
#include "../common.hlsli"
groupshared uint64_t Total;
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    if (gi == 0) Total = 0;
    GroupMemoryBarrierWithGroupSync();
    InterlockedAdd(Total, (uint64_t)InWord(gi));
    GroupMemoryBarrierWithGroupSync();
    Put(id, 0, (uint)Total); Put(id, 1, (uint)(Total >> 32));
}
