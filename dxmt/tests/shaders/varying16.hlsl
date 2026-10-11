// 16-bit signature elements (DXIL SM 6.8 plan, Task 5): one triangle as float, min precision and native 16-bit. Signature
// registers are 32-bit, so the IA input, the interpolated and flat varyings and the render-target output must all draw
// the float form's pixels. tag has bit 15 set and bias is negative: a 32-bit pixel shader reading the 16-bit vertex
// shader's registers (check.sh's tri-v16x) sees tag zero-extended and bias sign-extended, else blue or alpha saturates.
#if defined(NATIVE16)
typedef float16_t2 H2; typedef float16_t4 H4; typedef uint16_t U1; typedef int16_t S1;
#elif defined(MIN16)
typedef min16float2 H2; typedef min16float4 H4; typedef min16uint U1; typedef min16int S1;
#else
typedef float2 H2; typedef float4 H4; typedef uint U1; typedef int S1;
#endif
cbuffer Constants : register(b0) { float2 scale; float2 offset; };
struct V { float4 pos : SV_Position; H2 uv : TEXCOORD0; nointerpolation U1 tag : TAG; nointerpolation S1 bias : BIAS; };
V vsmain(H2 uv : POSITION) {
    V o; o.pos = float4(((float2)uv * float2(2, -2) + float2(-1, 1)) * scale + offset, 0, 1); o.uv = uv;
    o.tag = 0x80C8; o.bias = -56;
    return o;
}
// Blue 72 (-56 + 128), alpha 128 (0x80C8 >> 8).
H4 psmain(V i) : SV_Target { return H4(i.uv.x, i.uv.y, (float)(i.bias + 128) / 255, (float)(i.tag >> 8) / 255); }
