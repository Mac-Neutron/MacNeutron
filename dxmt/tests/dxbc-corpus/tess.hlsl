// DXMTRS step 4's DXBC corpus (sources.txt): hull and domain pipelines C5's one triangle/integer pair does not reach
// (airconv's A/dxbc_converter_ts.cpp): quad with fractional_odd partitioning and triangle output, isoline with pow2
// partitioning and line output, each with its vertex shader. tessq.{vs,hs,ds}.dxbc and tessl.{vs,hs,ds}.dxbc.
struct HSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct HSCtl { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct DSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

HSIn vs_main(float2 p : POSITION, float2 uv : TEXCOORD0) {
    HSIn o;
    o.pos = float4(p, 0, 1);
    o.uv = uv;
    return o;
}

struct QuadC { float edges[4] : SV_TessFactor; float inside[2] : SV_InsideTessFactor; };

QuadC quad_constants(InputPatch<HSIn, 4> p) {
    QuadC c;
    c.edges[0] = 2.5; c.edges[1] = 3.5; c.edges[2] = 4.5; c.edges[3] = 5.5;
    c.inside[0] = p[0].uv.x + 3; c.inside[1] = p[2].uv.y + 3;
    return c;
}

[domain("quad")] [partitioning("fractional_odd")] [outputtopology("triangle_cw")] [outputcontrolpoints(4)]
[patchconstantfunc("quad_constants")]
HSCtl hs_quad(InputPatch<HSIn, 4> p, uint i : SV_OutputControlPointID) {
    HSCtl o;
    o.pos = p[i].pos;
    o.uv = p[i].uv;
    return o;
}

[domain("quad")]
DSOut ds_quad(QuadC c, float2 uv : SV_DomainLocation, const OutputPatch<HSCtl, 4> p) {
    DSOut o;
    o.pos = lerp(lerp(p[0].pos, p[1].pos, uv.x), lerp(p[3].pos, p[2].pos, uv.x), uv.y);
    o.uv = lerp(lerp(p[0].uv, p[1].uv, uv.x), lerp(p[3].uv, p[2].uv, uv.x), uv.y);
    return o;
}

struct LineC { float edges[2] : SV_TessFactor; };

LineC line_constants(InputPatch<HSIn, 2> p) {
    LineC c;
    c.edges[0] = 4;  // lines in the patch
    c.edges[1] = 8;  // segments per line
    return c;
}

[domain("isoline")] [partitioning("pow2")] [outputtopology("line")] [outputcontrolpoints(2)]
[patchconstantfunc("line_constants")]
HSCtl hs_line(InputPatch<HSIn, 2> p, uint i : SV_OutputControlPointID) {
    HSCtl o;
    o.pos = p[i].pos;
    o.uv = p[i].uv;
    return o;
}

[domain("isoline")]
DSOut ds_line(LineC c, float2 uv : SV_DomainLocation, const OutputPatch<HSCtl, 2> p) {
    DSOut o;
    o.pos = lerp(p[0].pos, p[1].pos, uv.x) + float4(0, uv.y * 0.1, 0, 0);
    o.uv = lerp(p[0].uv, p[1].uv, uv.x);
    return o;
}
