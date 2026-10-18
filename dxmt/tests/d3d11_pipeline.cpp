// D3D11's stream output and tessellation stages (Direct3D forwarding M4), as far as DXMT has them: their objects made
// (a stream-output layout from vertex shader bytecode and its declaration's semantic names, as games make one; a hull
// and a domain shader) and a draw streaming out. DXMT has neither a geometry shader's stream output nor DrawAuto, its
// emulated stream output wrote nothing in this draw on macOS, and its translator refuses Wine's compiled hull shader
// for a pipeline ("Invalid vertex-hull shader"), so what they produce isn't compared, only what forwarding carries.
//   d3d11_pipeline.exe <shaders>   prints "pipeline <check> <value>":
//     streamout    CreateGeometryShaderWithStreamOutput's HRESULT, and 1 once its draw is recorded and flushed
//     tessellation CreateHullShader's and CreateDomainShader's HRESULTs
#include "d3d11_common.hpp"

int main(int argc, char **argv) {
    if (argc != 2) { printf("usage: d3d11_pipeline.exe <shaders>\n"); return 2; }
    g_shaders = argv[1];
    Dev dev;
    ID3D11Device *d = dev.device;
    ID3D11DeviceContext *ctx = dev.ctx;

    float points[8][2];
    for (UINT i = 0; i < 8; i++) points[i][0] = (i * 4 + 0.5f) / 32 - 1, points[i][1] = 1 - (i * 2 + 0.5f) / 32;
    ID3D11Buffer *in = dev.Buffer(sizeof points, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, 0, points);
    std::vector<char> vs_code;
    ID3D11VertexShader *sovs = dev.VS("so.vs", &vs_code);
    D3D11_INPUT_ELEMENT_DESC le = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    ID3D11InputLayout *layout;
    CHECK(d->CreateInputLayout(&le, 1, vs_code.data(), vs_code.size(), &layout));
    D3D11_SO_DECLARATION_ENTRY decl[] = {{0, "SV_Position", 0, 0, 4, 0}, {0, "VALUE", 0, 0, 1, 0}};
    UINT stride = 20, zero = 0, vstride = 8;
    ID3D11GeometryShader *gs = nullptr;
    HRESULT hr = d->CreateGeometryShaderWithStreamOutput(vs_code.data(), vs_code.size(), decl, 2, &stride, 1,
                                                         D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs);
    ID3D11Buffer *so = dev.Buffer(64 * stride, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER);
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, &in, &vstride, &zero);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(sovs, nullptr, 0);
    ctx->GSSetShader(gs, nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
    ctx->SOSetTargets(1, &so, &zero);
    ctx->Draw(8, 0);
    ID3D11Buffer *none = nullptr;
    ctx->SOSetTargets(1, &none, &zero);
    ctx->GSSetShader(nullptr, nullptr, 0);
    dev.Read(so, 8 * stride);  // flushed and done
    printf("pipeline streamout %08lx 1\n", (unsigned long)(unsigned)hr);

    std::vector<char> hs_code = Shader("patch.hs"), ds_code = Shader("patch.ds");
    ID3D11HullShader *hs = nullptr;
    ID3D11DomainShader *ds = nullptr;
    HRESULT h1 = d->CreateHullShader(hs_code.data(), hs_code.size(), nullptr, &hs);
    HRESULT h2 = d->CreateDomainShader(ds_code.data(), ds_code.size(), nullptr, &ds);
    printf("pipeline tessellation %08lx %08lx\n", (unsigned long)(unsigned)h1, (unsigned long)(unsigned)h2);
    return 0;
}
