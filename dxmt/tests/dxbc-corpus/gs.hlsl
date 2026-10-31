// DXMTRS step 4's DXBC corpus (sources.txt): a point-to-quad geometry shader, the general (not pass-through) path of
// airconv's GS lowering (A/dxbc_converter_gs.cpp), with SV_PrimitiveID read, and the vertex shader that feeds it.
// gs.vs.dxbc and gs.gs.dxbc.
struct VSOut { float4 pos : SV_Position; float4 color : COLOR; float size : SIZE; };

VSOut vs_main(float2 p : POSITION, float4 color : COLOR, float size : SIZE) {
    VSOut o;
    o.pos = float4(p, 0, 1);
    o.color = color;
    o.size = size;
    return o;
}

struct GSOut { float4 pos : SV_Position; float4 color : COLOR; float2 uv : TEXCOORD0; uint prim : PRIM; };

[maxvertexcount(4)]
void gs_main(point VSOut input[1], uint prim : SV_PrimitiveID, inout TriangleStream<GSOut> stream) {
    [unroll] for (uint i = 0; i < 4; i++) {
        float2 corner = float2(i & 1, i >> 1);
        GSOut o;
        o.pos = input[0].pos + float4((corner - 0.5) * input[0].size, 0, 0);
        o.color = input[0].color;
        o.uv = corner;
        o.prim = prim;
        stream.Append(o);
    }
}
