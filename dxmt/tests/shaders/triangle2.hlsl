// Render test (DXIL translator plan, Task 3): constant buffer, texture sample, discard, derivatives, and an extra
// varying the pixel shader ignores, so its TEXCOORD0 is packed at a different row than the vertex shader's; and the
// render target array index (always 0: the target isn't an array), written by the vertex shader; and a corner read
// from a vertex buffer through the input layout (Task 6: every game vertex shader reads one).
cbuffer Constants : register(b0) { float2 scale; float2 offset; };
Texture2D<float4> Tex : register(t0);
SamplerState Point : register(s0);
struct VSOut {
    float4 pos : SV_Position; float2 extra : TEXCOORD1; float2 uv : TEXCOORD0; uint layer : SV_RenderTargetArrayIndex;
};
VSOut vsmain(uint id : SV_VertexID, float2 uv : POSITION) {
    VSOut o;
    o.pos = float4((uv * float2(2, -2) + float2(-1, 1)) * scale + offset, 0, 1);
    o.extra = uv * 3;
    o.uv = uv;
    o.layer = id / 8; // 0
    // 0. Msad (50) in a vertex shader, whose helper the geometry pipeline must link too (DXIL SM 6.8 plan, Task 1).
    o.uv += msad4(id, uint2(id, 0), uint4(0, 0, 0, 0)).x;
    return o;
}
float4 psmain(float4 pos : SV_Position, float2 uv : TEXCOORD0, uint layer : SV_RenderTargetArrayIndex) : SV_Target {
    if (uv.x > 0.9) discard;
    float4 t = Tex.Sample(Point, uv * 0.5);
    return float4(t.rgb * (1 - uv.y) + ddx(uv.x) * 8, 1 - layer * 0.25);
}
// DXIL SM 6.8 plan, Task 10: psmain with Tex and its sampler through the descriptor heaps (d3d12_triangle.exe --heap: Tex
// at view-heap slot 0, a linear clamp sampler at sampler-heap slot 0, so not psmain's pixels: a point sampler, or one
// read from zeros, would draw those).
float4 psheap(float4 pos : SV_Position, float2 uv : TEXCOORD0, uint layer : SV_RenderTargetArrayIndex) : SV_Target {
    Texture2D<float4> tex = ResourceDescriptorHeap[0]; SamplerState lin = SamplerDescriptorHeap[0];
    if (uv.x > 0.9) discard;
    float4 t = tex.Sample(lin, uv * 0.5);
    return float4(t.rgb * (1 - uv.y) + ddx(uv.x) * 8, 1 - layer * 0.25);
}
// Geometry shader (Task 7: SMITE 2's volume passes need one): relinks the vertex outputs by semantic, reads the
// primitive ID, and emits two strips: the triangle shrunk to 3/4, then a small copy in the top right corner.
struct GSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; uint layer : SV_RenderTargetArrayIndex; };
[maxvertexcount(6)]
void gsmain(triangle VSOut input[3], uint prim : SV_PrimitiveID, inout TriangleStream<GSOut> stream) {
    for (uint i = 0; i < 3; i++) {
        GSOut o; o.pos = float4(input[i].pos.xy * 0.75, input[i].pos.zw); o.uv = input[i].uv; o.layer = prim;
        stream.Append(o);
    }
    stream.RestartStrip();
    for (uint j = 0; j < 3; j++) {
        GSOut o; o.pos = float4(input[j].pos.xy * 0.2 + 0.7, input[j].pos.zw); o.uv = input[j].uv.yx; o.layer = prim;
        stream.Append(o);
    }
}
