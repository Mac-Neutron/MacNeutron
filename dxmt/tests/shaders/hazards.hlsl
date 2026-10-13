// Encoder ordering tests (d3d12_hazards.cpp): heavy work, then work that reads, overwrites or reuses what it wrote.
cbuffer Constants : register(b0) { float value; uint loops; };
Texture2D<float4> Source : register(t0);
RWByteAddressBuffer Data : register(u0);

// A triangle covering the whole target.
float4 vsfull(uint id : SV_VertexID) : SV_Position {
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
// `value` everywhere (with additive blending, each instance adds it).
float4 psvalue() : SV_Target0 { return value; }
// The source texel under the pixel, plus `value`.
float4 pssample(float4 pos : SV_Position) : SV_Target0 { return Source.Load(int3(pos.xy, 0)) + value; }
// `value` everywhere, each pixel adding 1 to word 2 (a pixel shader that writes a UAV).
float4 psuav() : SV_Target0 { Data.InterlockedAdd(8, 1); return value; }
// Every thread adds 1 to word 0.
[numthreads(64, 1, 1)] void csfill() { Data.InterlockedAdd(0, 1); }
// Copies word 0 to word 1.
[numthreads(1, 1, 1)] void cscount() { Data.Store(4, Data.Load(0)); }
// Spins `loops` times, then writes one draw's arguments: 3 vertices, 1 instance.
[numthreads(1, 1, 1)] void csargs() {
    uint x = 1;
    for (uint i = 0; i < loops; i++)
        x = x * 1664525 + 1013904223;
    Data.Store4(0, uint4(3, x == 0 ? 2 : 1, 0, 0));
}
