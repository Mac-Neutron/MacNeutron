#include "common.hlsli"
#include "atomic64.hlsli"
// 64-bit atomics on raw and structured buffers (DXIL SM 6.8 plan, Task 14a: ops 78 and 79, i64 overload), run with
// root_a64 (flags 0xC00: the lock qword follows the heap qwords). Slots 0-7: threads 0-31 own Raw64's qword i, threads
// 32-63 AtomicS64[i] (AtomicS64S[i] for the signed kinds), every op kind. Then all 64 threads (two simdgroups) on shared
// qwords of a64: slots 8-9 an add of V to byte 512 through four views (raw, structured, root UAV, the heap), slots
// 10-11 a UMax at 520 whose high halves tie in eights, slot 12 a CAS-counter ticket at 528, slot 13 of thread 0 an add
// out of bounds (skipped: 0). After a barrier, slots 14-15 of threads 0-2 the three finals by plain loads.
RWByteAddressBuffer Root64 : register(u20);   // root_a64's root UAV: a64
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x, off = i * 8;
    OPERANDS(i)
    if (i < 32) { OWNRAW } else { OWN(AtomicS64[i], AtomicS64S[i]) }
    Put64(id, 0, o1); Put64(id, 2, o2); Put64(id, 4, o3); Put64(id, 6, o4);
    uint64_t s;
    if ((i & 3) == 0) Raw64.InterlockedAdd64(512, V, s);
    else if ((i & 3) == 1) InterlockedAdd(AtomicS64[64], V, s);
    else if ((i & 3) == 2) Root64.InterlockedAdd64(512, V, s);
    else { RWByteAddressBuffer h = ResourceDescriptorHeap[16]; h.InterlockedAdd64(512, V, s); }
    Put64(id, 8, s);
    Raw64.InterlockedMax64(520, (uint64_t)(i >> 3) << 32 | (InWord(i) & 0xffff), s); Put64(id, 10, s);
    uint64_t t = 0;
    [loop] for (;;) { InterlockedCompareExchange(AtomicS64[66], t, t + 1, s); if (s == t) break; t = s; }
    Put(id, 12, (uint)t);
    if (i == 0) { Raw64.InterlockedAdd64(16384, V, s); Put(id, 13, (uint)(s | s >> 32)); }
    DeviceMemoryBarrierWithGroupSync();
    if (i < 3) { uint2 f = Raw64.Load2(512 + i * 8); Put(id, 14, f.x); Put(id, 15, f.y); }
}
