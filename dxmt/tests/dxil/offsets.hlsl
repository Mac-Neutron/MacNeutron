#include "common.hlsli"
// Programmable offsets (SM 6.7): runtime Load and Sample offsets reach the texture unit wrapped to their low 4 bits,
// sign-extended; runtime Gather offsets pass through (in [-8, 7] here). Base texel (4, 4); every wrapped offset stays
// inside the 8x8 texture.
static const int kOff[8] = {-4, -1, 0, 3, 12, 13, -13, -15};  // the last four wrap to -4, -3, 3, 1
uint Rgba(float4 c) { uint4 u = (uint4)round(saturate(c) * 255); return u.x | u.y << 8 | u.z << 16 | u.w << 24; }
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint w = InWord(id.x); int2 o = int2(kOff[w & 7], kOff[(w >> 3) & 7]), og = int2(kOff[w & 3], kOff[(w >> 3) & 3]);
    float2 uv = 4.5 / 8;
    Put(id, 0, Rgba(Tex.Load(int3(4, 4, 0), o)));
    Put(id, 1, Rgba(Tex.SampleLevel(Point, uv, 0, o)));
    Put(id, 2, Rgba(Tex.GatherRed(Point, uv, og)));  // footprint (4..5, 4..5) + og, clamped: x, y, z, w = (i0,j1) (i1,j1) (i1,j0) (i0,j0)
    Put(id, 3, Rgba(Tex.GatherGreen(Point, uv, og)));  // red holds only x: green shows og.y
}
