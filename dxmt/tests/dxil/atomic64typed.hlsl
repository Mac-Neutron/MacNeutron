#include "common.hlsli"
#include "atomic64.hlsli"
// 64-bit atomics on a typed buffer and a texture (DXIL SM 6.8 plan, Task 14b), both R32G32_UINT; the S views are the
// same resources as int64_t. Slots 0-7: threads 0-31 own RWTyped64[i], threads 32-63 texel (i & 7, i >> 3), every op
// kind. Then all 64 threads: slots 8-9 an add of V to one qword through a raw and a typed view (one lock: the address
// key), slots 10-11 a Nanite-shaped max of depth << 32 | id on texels (0..3, 1), 16 threads each, slot 12 a
// CAS-counter ticket on texel (0, 0) (every retry re-reads the texel: the texture fence), slot 13 of thread 0 an add on
// RWTyped64[64] (out of the view: skipped, 0). After a barrier, slots 14-15: thread 0 the raw/typed final, threads
// 1-4 the four maxima, thread 5 the counter, thread 6 Raw64's qword 192 (0: the out-of-view add didn't land).
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x; uint2 t = uint2(i & 7, i >> 3);
    OPERANDS(i)
    if (i < 32) { OWN(RWTyped64[i], RWTyped64S[i]) } else { OWN(AtomicTex64[t], AtomicTex64S[t]) }
    Put64(id, 0, o1); Put64(id, 2, o2); Put64(id, 4, o3); Put64(id, 6, o4);
    uint64_t s;
    if (i & 1) InterlockedAdd(RWTyped64[40], V, s); else Raw64.InterlockedAdd64(1344, V, s);
    Put64(id, 8, s);
    InterlockedMax(AtomicTex64[uint2(i & 3, 1)], (uint64_t)(InWord(i) >> 28) << 32 | i, s); Put64(id, 10, s);
    uint64_t n = 0;
    [loop] for (;;) { InterlockedCompareExchange(AtomicTex64[uint2(0, 0)], n, n + 1, s); if (s == n) break; n = s; }
    Put(id, 12, (uint)n);
    if (i == 0) { InterlockedAdd(RWTyped64[64], V, s); Put(id, 13, (uint)(s | s >> 32)); }
    DeviceMemoryBarrierWithGroupSync();
    if (i == 0) { uint2 f = Raw64.Load2(1344); s = (uint64_t)f.y << 32 | f.x; }
    else if (i <= 4) InterlockedOr(AtomicTex64[uint2(i - 1, 1)], (uint64_t)0, s);
    else if (i == 5) InterlockedOr(AtomicTex64[uint2(0, 0)], (uint64_t)0, s);
    else if (i == 6) { uint2 f = Raw64.Load2(1536); s = (uint64_t)f.y << 32 | f.x; }
    if (i <= 6) Put64(id, 14, s);
}
