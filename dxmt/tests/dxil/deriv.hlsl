#include "common.hlsli"
// Compute-shader derivatives on 1D quads (four consecutive group indices). Quad q's coordinates step 2^k / 8 per lane,
// k = q % 5, so its LOD is exactly k; Depth has 4 mips, so sampling clamps to mip 3.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    float k = (gi >> 2) % 5, s = exp2(k) / 8, v = gi * gi;  // gi * gi: fine and coarse differ
    PutF(id, 0, ddx_coarse(v)); PutF(id, 1, ddy_coarse(v)); PutF(id, 2, ddx_fine(v)); PutF(id, 3, ddy_fine(v));
    float2 uv = float2(gi & 1, (gi >> 1) & 1) * s;
    Put(id, 4, Lod64(Depth.CalculateLevelOfDetailUnclamped(Point, uv)));   // 64 k
    Put(id, 5, Lod64(Depth.CalculateLevelOfDetail(Point, uv)));            // 64 min(k, 3)
    PutF(id, 6, Depth.Sample(Point, uv));                                  // mip min(k, 3)
    PutF(id, 7, Depth.SampleBias(Point, uv, -1));                          // mip clamp(k - 1, 0, 3)
    PutF(id, 8, Depth.SampleCmp(Cmp, uv, 0.5));                            // 1 when the mip's value >= 0.5: k >= 2
    Put(id, 9, Lod64(Tex.CalculateLevelOfDetailUnclamped(Linear, uv * float2(1, 2))));  // the larger gradient: 64 (k + 1)
}
