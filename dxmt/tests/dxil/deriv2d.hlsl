#include "common.hlsli"
// 2D compute quads (SM 6.6): (x, y), (x+1, y), (x, y+1), (x+1, y+1) for even x and y of SV_GroupThreadID, whatever the
// group's width: 8 (rows fit a 32-lane simdgroup), 32 (they don't) and 6 (not a power of two). Rows of Out are
// indexed by SV_GroupIndex.
void Body(uint3 gt, uint gi) {
    uint3 o = uint3(gi, 0, 0);
    float v = gt.x * gt.x + 3 * gt.y * gt.y + gt.x * gt.y;
    PutF(o, 0, ddx_coarse(v)); PutF(o, 1, ddy_coarse(v)); PutF(o, 2, ddx_fine(v)); PutF(o, 3, ddy_fine(v));
    Put(o, 4, QuadReadAcrossX(gi)); Put(o, 5, QuadReadAcrossY(gi)); Put(o, 6, QuadReadAcrossDiagonal(gi));
    Put(o, 7, QuadReadLaneAt(gi, 3)); Put(o, 8, gt.x | gt.y << 8 | gt.z << 16);
    float s = exp2((gt.x / 2 + gt.y / 2) % 4) / 8;   // one k per quad
    Put(o, 9, Lod64(Tex.CalculateLevelOfDetailUnclamped(Linear, float2(gt.x & 1, gt.y & 1) * s)));
}
[numthreads(8, 8, 1)] void main8x8(uint3 gt : SV_GroupThreadID, uint gi : SV_GroupIndex) { Body(gt, gi); }
[numthreads(32, 2, 1)] void main32x2(uint3 gt : SV_GroupThreadID, uint gi : SV_GroupIndex) { Body(gt, gi); }
[numthreads(6, 6, 1)] void main6x6(uint3 gt : SV_GroupThreadID, uint gi : SV_GroupIndex) { Body(gt, gi); }
