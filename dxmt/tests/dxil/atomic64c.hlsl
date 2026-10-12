#include "common.hlsli"
#include "atomic64.hlsli"
// 64-bit adds under contention (DXIL SM 6.8 plan, Task 14a), Dispatch(64, 1, 1): 4,096 threads, 128 simdgroups. Storm:
// every thread adds V to Out's qword 0. Spread: 1,024 qwords of a64 from byte 8192, four threads each from four groups
// (the probe never ran this shape without fences; the Lane E fence row is the fence proof). Each old must be k * V with k distinct per address:
// Mark sets bit k of a bitmap in Out with 32-bit atomics on other words. Out words 0-1: the storm's final; 2: malformed
// olds; 128-255: the storm's k; 256-383: the spread's 4 * address + k; 384-511: the typed storm's k;
// 512-639: the texel storm's k.
void Mark(uint64_t old, uint limit, uint base, uint bit0) {
    uint k = (uint)old;
    if ((uint)(old >> 32) != k || k >= limit) { Out.InterlockedAdd(8, 1); return; }
    uint b = bit0 + k; Out.InterlockedOr(base + (b >> 5) * 4, 1u << (b & 31));
}
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint64_t o; uint a = id.x & 1023;
    Out.InterlockedAdd64(0, V, o); Mark(o, 4096, 512, 0);
    Raw64.InterlockedAdd64(8192 + a * 8, V, o); Mark(o, 4, 1024, a * 4);
    InterlockedAdd(RWTyped64[0], V, o); Mark(o, 4096, 1536, 0);                  // a typed element: a64's qword 128
    InterlockedAdd(AtomicTex64[uint2(0, 0)], V, o); Mark(o, 4096, 2048, 0);      // a texel
}
