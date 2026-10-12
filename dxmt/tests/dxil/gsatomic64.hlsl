#include "common.hlsli"
#include "atomic64.hlsli"
// 64-bit groupshared atomics (DXIL SM 6.8 plan, Task 14c: LLVM atomicrmw/cmpxchg on i64 addrspace(3), no dx.op),
// through the translator's threadgroup lock array. Slots 0-7: each thread owns G[i] (GS[i] for the signed kinds),
// every op kind. Then all 64 threads: slots 8-9 Task 9's carry-crossing sum of In's words into Total, slots 10-11 a UMax
// whose high halves tie in eights, slot 12 a CAS-counter ticket; after a barrier, slots 14-15 of threads 0-2 the three
// finals by plain loads.
groupshared uint64_t G[64], Total, M, C;
groupshared int64_t GS[64];
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    OPERANDS(gi)
    G[gi] = 0; GS[gi] = 0;
    if (gi == 0) { Total = 0; M = 0; C = 0; }
    GroupMemoryBarrierWithGroupSync();
    OWN(G[gi], GS[gi])
    Put64(id, 0, o1); Put64(id, 2, o2); Put64(id, 4, o3); Put64(id, 6, o4);
    uint64_t s;
    InterlockedAdd(Total, (uint64_t)InWord(gi), s); Put64(id, 8, s);
    InterlockedMax(M, (uint64_t)(gi >> 3) << 32 | (InWord(gi) & 0xffff), s); Put64(id, 10, s);
    uint64_t n = 0;
    [loop] for (;;) { InterlockedCompareExchange(C, n, n + 1, s); if (s == n) break; n = s; }
    Put(id, 12, (uint)n);
    GroupMemoryBarrierWithGroupSync();
    if (gi < 3) Put64(id, 14, gi == 0 ? Total : gi == 1 ? M : C);
}
