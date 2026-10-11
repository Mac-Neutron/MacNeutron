// Indirect draws (ExecuteIndirect), as Unreal draws grass and GPU particles: each draw's StartInstanceLocation picks,
// through a per-instance vertex stream, a 2x2-pixel cell of a 64x64 target (32x32 cells, six vertices) to paint white.
float4 vsmain(uint id : SV_VertexID, uint cell : CELL) : SV_Position {
    float2 uv = float2(id == 1 || id == 4 || id == 5, id == 2 || id == 3 || id == 5);
    float2 pixel = float2(cell % 32, cell / 32) * 2 + uv * 2;
    return float4(pixel.x / 32 - 1, 1 - pixel.y / 32, 0, 1);
}
float4 psmain() : SV_Target0 { return 1; }
// Indirect dispatches, as Niagara simulates GPU particles: every thread counts itself.
RWByteAddressBuffer Counter : register(u0);
[numthreads(4, 1, 1)] void csmain() { Counter.InterlockedAdd(0, 1); }
// Indirect draws read straight from the argument buffer (GPU efficiency E10): vertex v (VERT, from a per-vertex stream
// of 0, 1, 2..., so fetched at StartVertexLocation + i, or index + BaseVertexLocation) of instance `inst` is a corner of
// a one-pixel triangle at cell (v / 3, inst) of a 32x8 target, painted with the instance's TAG (from a per-instance
// stream, so StartInstanceLocation picks it).
struct Tagged { float4 pos : SV_Position; nointerpolation uint tag : TAG; };
Tagged vsid(uint v : VERT, uint inst : SV_InstanceID, uint tag : TAG) {
    float2 pixel = float2(v / 3, inst) + float2(v % 3 == 1, v % 3 == 2) * 1.5;
    Tagged o = {float4(pixel.x / 16 - 1, 1 - pixel.y / 4, 0, 1), tag};
    return o;
}
uint psid(Tagged i) : SV_Target0 { return i.tag; }
// SV_VertexID under start and base vertex locations (DXIL SM 6.8 plan, Task 12): vsid's cells, tagged
// 600 + SV_VertexID - VERT. VERT is the fetched vertex (start + i, or index + base), so the tag is the same on all three
// corners whatever the provoking vertex: 600 when SV_VertexID counts from the start vertex, 600 - start when it counts
// from 0, 600 - BaseVertexLocation for indexed draws.
Tagged vsvid(uint v : VERT, uint inst : SV_InstanceID, uint vid : SV_VertexID) {
    float2 pixel = float2(v / 3, inst) + float2(v % 3 == 1, v % 3 == 2) * 1.5;
    Tagged o = {float4(pixel.x / 16 - 1, 1 - pixel.y / 4, 0, 1), (uint)(600 + (int)vid - (int)v)};
    return o;
}
// SV_StartVertexLocation and SV_StartInstanceLocation (SM 6.8): tagged 1000 + 100 * (start vertex + 10) + start instance.
Tagged vsstart(uint v : VERT, uint inst : SV_InstanceID, int sv : SV_StartVertexLocation, uint si : SV_StartInstanceLocation) {
    float2 pixel = float2(v / 3, inst) + float2(v % 3 == 1, v % 3 == 2) * 1.5;
    Tagged o = {float4(pixel.x / 16 - 1, 1 - pixel.y / 4, 0, 1), (uint)(1000 + 100 * (sv + 10)) + si};
    return o;
}
