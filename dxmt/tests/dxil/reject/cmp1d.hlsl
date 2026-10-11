// Comparison samples of a Texture1D (DXIL SM 6.8 plan, Task 13 fix round): airconv binds a 1D texture as a 2D colour
// texture, never a depth one, so Metal has no comparison sample for it. SampleCmpBias (255), SampleCmpGrad (254) and
// SampleCmp (64) are refused naming their op (they crashed the translator before).
Texture1D<float> D : register(t0);
SamplerComparisonState C : register(s0);
float4 psbias(float4 pos : SV_Position, float u : TEXCOORD0) : SV_Target { return D.SampleCmpBias(C, u, 0.5, 1); }
float4 psgrad(float4 pos : SV_Position, float u : TEXCOORD0) : SV_Target { return D.SampleCmpGrad(C, u, 0.5, ddx(u), ddy(u)); }
float4 pscmp(float4 pos : SV_Position, float u : TEXCOORD0) : SV_Target { return D.SampleCmp(C, u, 0.5); }
