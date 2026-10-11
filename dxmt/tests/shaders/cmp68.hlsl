// Shader Model 6.8's SampleCmpBias (255) and SampleCmpGrad (254) in a pixel shader, on Metal's own LOD path (DXIL SM
// 6.8 plan, Task 13). Translated offline only; dxil/cmp68.hlsl runs both in a compute shader against a CPU reference.
Texture2D<float> D : register(t0);
SamplerComparisonState C : register(s0);
float4 psmain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return D.SampleCmpBias(C, uv, 0.5, 1) + D.SampleCmpGrad(C, uv, 0.5, ddx(uv), ddy(uv));
}
