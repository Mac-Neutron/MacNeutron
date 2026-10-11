// Shader Model 6.0-6.6 queries with fixed answers on a one-sample target without view instancing (DXIL SM 6.8 plan,
// Task 1): triangle2's pixel shader plus terms that are 0 when ViewID (138), the sample count (77), the sample position
// (76) and IsHelperLane (221: a covered pixel is no helper) are right. The compute shader (75) is translated offline.
Texture2D<float4> Tex : register(t0);
SamplerState Point : register(s0);
float4 psmain(float4 pos : SV_Position, float2 uv : TEXCOORD0, uint layer : SV_RenderTargetArrayIndex,
              uint view : SV_ViewID) : SV_Target {
    if (uv.x > 0.9) discard;
    float4 t = Tex.Sample(Point, uv * 0.5);
    float2 p = GetRenderTargetSamplePosition(0);
    float zero = view + (GetRenderTargetSampleCount() - 1) + abs(p.x) + abs(p.y) + (IsHelperLane() ? 1 : 0);
    return float4(t.rgb * (1 - uv.y) + ddx(uv.x) * 8 + zero, 1 - layer * 0.25);
}
Texture2DMS<float4> MS : register(t1);
RWByteAddressBuffer Positions : register(u0);
[numthreads(1, 1, 1)] void csmain() { Positions.Store2(0, asuint(MS.GetSamplePosition(1))); }
