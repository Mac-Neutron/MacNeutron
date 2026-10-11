#include "common.hlsli"
// Shader Model 6.8's comparison sampling (254, 255; 81 with a comparison sampler) and SampleCmp/SampleCmpLevelZero
// (64, 65) on Depth's 4 mips (0.125 + 0.25 m). LESS_EQUAL against 0.5 passes on mips 2 and 3. Quad q's gradient is
// 2^k / 8 per lane, k = q % 5: LOD k, clamped to 3.
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {
    float k = (gi >> 2) % 5, s = exp2(k) / 8;
    float2 uv = float2(gi & 1, (gi >> 1) & 1) * s, gx = float2(s, 0), gy = float2(0, s);
    PutF(id, 0, Depth.SampleCmpGrad(Cmp, uv, 0.5, gx, gy));               // k >= 2
    PutF(id, 1, Depth.SampleCmpBias(Cmp, uv, 0.5, -1));                   // k >= 3
    PutF(id, 2, Depth.SampleCmpBias(Cmp, uv, 0.5, 1));                    // k >= 1
    Put(id, 3, Lod64(Depth.CalculateLevelOfDetail(Cmp, uv)));             // 64 min(k, 3)
    Put(id, 4, Lod64(Depth.CalculateLevelOfDetailUnclamped(Cmp, uv)));    // 64 k
    PutF(id, 5, Depth.SampleCmp(Cmp, uv, 0.5));                           // k >= 2
    PutF(id, 6, Depth.SampleCmpLevelZero(Cmp, uv, 0.1));                  // 1
    PutF(id, 7, Depth.SampleCmpGrad(Cmp, uv, 0.7, gx * 2, gy * 2));       // mip min(k + 1, 3) >= 0.7: k >= 2
    PutF(id, 8, Depth.SampleCmpGrad(Cmp, uv, 0.5, gx, gy, int2(1, 0)));   // offset form: as slot 0
}
