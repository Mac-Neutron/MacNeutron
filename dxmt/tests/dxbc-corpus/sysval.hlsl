// DXMTRS step 4's DXBC corpus (sources.txt): system values C5 and tests/dx11 never write. A vertex shader writing
// SV_ClipDistance; a pixel shader writing SV_Coverage and SV_Depth beside its target. (SV_DepthGreaterEqual cannot
// be written here: Wine's HLSL compiler, vkd3d-shader, has no such semantic in its pixel output table, tpf.c's
// sm4_sysval_semantic_from_semantic_name, so airconv's greater-equal depth register has no DXBC in this corpus.)
// sysval.vs.dxbc and sysval.ps.dxbc.
struct VSOut { float4 pos : SV_Position; float clip : SV_ClipDistance0; float2 uv : TEXCOORD0; };

VSOut vs_main(float2 p : POSITION, float2 uv : TEXCOORD0) {
    VSOut o;
    o.pos = float4(p, 0.5, 1);
    o.clip = p.x + 0.75;
    o.uv = uv;
    return o;
}

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
struct PSOut { float4 color : SV_Target0; uint coverage : SV_Coverage; float depth : SV_Depth; };

PSOut ps_main(PSIn i) {
    PSOut o;
    o.color = float4(i.uv, 0, 1);
    o.coverage = 0x5;
    o.depth = saturate(i.pos.z + i.uv.x * 0.01);
    return o;
}
