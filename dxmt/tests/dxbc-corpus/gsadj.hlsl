// DXMTRS step 4's DXBC corpus (sources.txt): a lineadj geometry shader with RestartStrip and a data-dependent second
// strip (the adjacency input kind, line-stream output, cut). gsadj.vs.dxbc and gsadj.gs.dxbc.
struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };

VSOut vs_main(float2 p : POSITION, float4 color : COLOR) {
    VSOut o;
    o.pos = float4(p, 0, 1);
    o.color = color;
    return o;
}

[maxvertexcount(6)]
void gs_main(lineadj VSOut input[4], inout LineStream<VSOut> stream) {
    stream.Append(input[1]);
    stream.Append(input[2]);
    stream.RestartStrip();
    if (input[0].pos.x < input[3].pos.x) {
        stream.Append(input[0]);
        stream.Append(input[1]);
        stream.RestartStrip();
    }
    stream.Append(input[2]);
    stream.Append(input[3]);
    stream.RestartStrip();
}
