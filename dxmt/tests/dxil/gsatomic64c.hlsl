#include "common.hlsli"
#include "atomic64.hlsli"
// 64-bit groupshared atomics under contention (DXIL SM 6.8 plan, Task 14c): numthreads(1024), 32 simdgroups, every
// lane on one address twice: A gets V (each old k * V), B gets 1 from 0xfffffe00 (the olds cross into the high half at
// k = 512). Bit k of each bitmap in Out, set with 32-bit atomics: words 0-31 (A), 32-63 (B); word 64 counts malformed
// olds; words 65-68 the finals of A and B.
groupshared uint64_t A, B;
void Mark(uint k, bool ok, uint base) {
    if (!ok || k >= 1024) { Out.InterlockedAdd(256, 1); return; }
    Out.InterlockedOr(base + (k >> 5) * 4, 1u << (k & 31));
}
[numthreads(1024, 1, 1)]
void main(uint gi : SV_GroupIndex) {
    if (gi == 0) { A = 0; B = 0xfffffe00; }
    GroupMemoryBarrierWithGroupSync();
    uint64_t o, d;
    InterlockedAdd(A, V, o); Mark((uint)o, (uint)(o >> 32) == (uint)o, 0);
    InterlockedAdd(B, (uint64_t)1, o); d = o - 0xfffffe00; Mark((uint)d, d < 1024, 128);
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0) Out.Store4(260, uint4((uint)A, (uint)(A >> 32), (uint)B, (uint)(B >> 32)));
}
