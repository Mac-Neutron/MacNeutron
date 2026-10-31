// DXMTRS step 4's DXBC corpus (sources.txt): a pass-through geometry shader, the true branch of airconv's detection
// (A/dxbc_converter.cpp CheckGSBBIsPassThrough and CheckGSSignatureIsPassThrough: only mov, emit and cut; every output
// in an input's register and mask; no resources). It copies a triangle and routes each vertex's LAYER to
// SV_RenderTargetArrayIndex, whose register is matched by position only. gspt.vs.dxbc and gspt.gs.dxbc.
struct VSOut { float4 pos : SV_Position; uint layer : LAYER; };

VSOut vs_main(float2 p : POSITION, uint layer : LAYER) {
    VSOut o;
    o.pos = float4(p, 0, 1);
    o.layer = layer;
    return o;
}

struct GSOut { float4 pos : SV_Position; uint layer : SV_RenderTargetArrayIndex; };

[maxvertexcount(3)]
void gs_main(triangle VSOut input[3], inout TriangleStream<GSOut> stream) {
    GSOut o;
    o.pos = input[0].pos; o.layer = input[0].layer; stream.Append(o);
    o.pos = input[1].pos; o.layer = input[1].layer; stream.Append(o);
    o.pos = input[2].pos; o.layer = input[2].layer; stream.Append(o);
}
