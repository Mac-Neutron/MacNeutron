// Varyings at their signature's width (DXMT 0063): elements of one to three columns with a flat uint among them, two
// float2s that DXC packs into one register, and a pixel shader that reads three columns of a four-column element. Each
// pair draws as D3DMetal (check.sh), and dxil-translate --flags counts the vertex shaders' output components (vout).
cbuffer Constants : register(b0) { float2 scale; float2 offset; };
float4 Corner(float2 uv) { return float4((uv * float2(2, -2) + float2(-1, 1)) * scale + offset, 0, 1); }

struct Narrow { float4 pos : SV_Position; float3 col : TEXCOORD0; nointerpolation uint tag : TAG; float2 uv : TEXCOORD1; };
Narrow vsnarrow(float2 uv : POSITION) {
    Narrow o; o.pos = Corner(uv); o.col = float3(uv, 1 - uv.x); o.tag = 0xC8; o.uv = uv.yx;
    return o;
}
float4 psnarrow(Narrow i) : SV_Target { return float4(i.col.x * i.uv.y, i.col.y, i.col.z * i.uv.x, i.tag / 255.0); }

struct Wide { float4 pos : SV_Position; float4 col : TEXCOORD0; float2 uv : TEXCOORD1; };
Wide vswide(float2 uv : POSITION) {
    Wide o; o.pos = Corner(uv); o.col = float4(uv, 1 - uv.y, 0.75); o.uv = uv;
    return o;
}
struct Read3 { float4 pos : SV_Position; float3 col : TEXCOORD0; float2 uv : TEXCOORD1; };
float4 psread3(Read3 i) : SV_Target { return float4(i.col, i.uv.x); }

struct Packed { float4 pos : SV_Position; float2 a : TEXCOORD0; float2 b : TEXCOORD1; };
Packed vspacked(float2 uv : POSITION) {
    Packed o; o.pos = Corner(uv); o.a = uv; o.b = float2(1 - uv.x, 0.5);
    return o;
}
float4 pspacked(Packed i) : SV_Target { return float4(i.a, i.b); }
