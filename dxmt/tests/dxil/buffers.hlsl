#include "common.hlsli"
// Dynamically indexed static const arrays stay globals in DXIL (SMITE 2: Unreal's sample tables).
static const float kFloats[8] = {0.5, -1, 2, 3.25, -4, 5, 6.5, -7};
static const uint kWords[4] = {0x11, 0x2200, 0x330000, 0x44000000};
// A local array of constants read in a loop: DXC hoists it to a global (.hca) and, from SM 6.6, keeps the array's lifetime
// markers on it (Palworld's NeighborOffset).
uint Neighbors(uint i) {
    int2 kNeighbor[4] = {int2(0, -1), int2(-1, 0), int2(1, 0), int2(0, 1)};
    uint sum[4];
    [loop] for (uint n = 0; n < 4; n++) {
        int2 o = kNeighbor[n];
        sum[n] = (uint)(o.x * 3 + o.y + 4) + i;
    }
    return sum[0] + sum[1] * 16 + sum[2] * 256 + sum[3] * 4096;
}
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    Put(id, 0, InWord(i));
    uint2 two = In.Load2(i * 8); Put(id, 1, two.x ^ two.y);
    uint4 four = In.Load4(i * 16); Put(id, 2, four.x + four.y + four.z + four.w);
    float4 t = Typed[i]; PutF(id, 3, t.x + t.y * 2 + t.z * 3 + t.w * 4);
    S s = Structured[i]; PutF(id, 4, s.a.y + s.a.w); Put(id, 5, s.b);
    PutF(id, 6, k[i & 3].x * k[(i + 1) & 3].y); Put(id, 7, u[i & 3]);
    float4 a = Arr[NonUniformResourceIndex(i & 1)][i]; PutF(id, 8, a.x + a.y);
    RWTyped[i] = i * 3; AllMemoryBarrier(); Put(id, 9, RWTyped[i]);
    Out.Store((i * 16 + 10) * 4, InWord(i) >> 3);
    Out.Store2((i * 16 + 11) * 4, uint2(i, i + 1));
    PutF(id, 13, kFloats[i & 7]); Put(id, 14, kWords[(i * 3) & 3]); Put(id, 15, Neighbors(i));
}
