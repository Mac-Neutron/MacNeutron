#include "common.hlsli"
// Shader Model 6.6 float atomics (DXIL SM 6.8 plan, Task 14): float InterlockedExchange and the two FloatBitwise
// compares on a raw, a structured, a groupshared, a typed-buffer (R32_FLOAT) and a typed-texture (R32_FLOAT) location
// that each thread owns, all starting at 0.0, so every returned value is fixed. DXC emits each as an i32 atomic on the
// float's bits. Per location: compare-store against -0.0 (bitwise no match: stays +0.0); exchange 0 -> a (returns 0);
// compare-store against b (no match: stays a); compare-exchange a -> b (returns a); compare-store b -> c (b is a NaN
// with a payload: bitwise, it matches itself); exchange c -> 0 (returns c). A float == compare would store at the
// -0.0 step and miss at the NaN one.
groupshared float G[64];
#define STEPS(R, s) \
    InterlockedCompareStoreFloatBitwise(R, -0.0f, c); \
    InterlockedExchange(R, a, o0); InterlockedCompareStoreFloatBitwise(R, b, c); \
    InterlockedCompareExchangeFloatBitwise(R, a, b, o1); InterlockedCompareStoreFloatBitwise(R, b, c); \
    InterlockedExchange(R, 0.0f, o2); PutF(id, s, o0); PutF(id, s + 1, o1); PutF(id, s + 2, o2);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    uint i = id.x, raw = (i * 16 + 15) * 4;  // Out's last word of this thread's row: zeroed, no Put writes it
    uint2 t = uint2(i & 7, i >> 3);
    float a = i * 0.5 + 0.25, b = asfloat(0x7fc00000u | (i + 1)), c = i * 4.0 + 1.0, o0, o1, o2;
    G[gi] = 0; GroupMemoryBarrierWithGroupSync();
    Out.InterlockedCompareStoreFloatBitwise(raw, -0.0f, c);
    Out.InterlockedExchangeFloat(raw, a, o0); Out.InterlockedCompareStoreFloatBitwise(raw, b, c);
    Out.InterlockedCompareExchangeFloatBitwise(raw, a, b, o1); Out.InterlockedCompareStoreFloatBitwise(raw, b, c);
    Out.InterlockedExchangeFloat(raw, 0.0f, o2); PutF(id, 0, o0); PutF(id, 1, o1); PutF(id, 2, o2);
    STEPS(AtomicSF[i], 3) STEPS(G[gi], 6) STEPS(RWTypedF[i], 9) STEPS(AtomicTexF[t], 12)
}
