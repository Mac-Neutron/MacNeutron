// The D3D11 and D3D10 test programs' shaders (Direct3D forwarding M4), compiled with D3DCompile where one exists (the
// macOS runtime's Wine, through eac-vm/dxmt/ref.sh): `dxbc.exe <folder>` writes <name>.dxbc for each into the shaders
// folder (dxmt/tests/shaders/d3d11, the invocations' @S/d3d11; apart from the DXIL, whose count dxmt/check.sh checks),
// committed, so the programs load bytecode and compile nothing when they run (native builds have no d3dcompiler).
// Prints "dxbc <name> <bytes>" each, or the compiler's errors.
//
// `dxbc.exe --file <hlsl> <entry> <profile> <out> [--file ...]` compiles one entry point of an HLSL file per group to
// <out> (DXMTRS step 4's DXBC corpus: dxmt/tests/dxbc-corpus/sources.txt has one such group per row), printing the same
// lines, named by <out>'s file name. Wine's D3DCompile (vkd3d-shader) is not fxc: the bytecode is what Wine writes.
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <string>

struct Source { const char *name, *profile, *hlsl; };

#define FULLSCREEN "float2 uv = float2((id << 1) & 2, id & 2); o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);"

static const Source kShaders[] = {
    // d3d11_vsia: vertex inputs from unbound slots.
    {"vsia.vs", "vs_5_0", R"(
struct VSOut { float4 pos : SV_Position; float4 a : TEXCOORD0; float4 b : TEXCOORD1; float4 c : TEXCOORD2; float4 d : TEXCOORD3; };
struct IAIn { float4 a : A; float4 b : B; float4 c : C; uint4 d : D; };
VSOut main(IAIn i, uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.a = i.a; o.b = i.b; o.c = i.c; o.d = float4(i.d);
    return o;
})"},
    {"vsia.ps", "ps_5_0", R"(
struct VSOut { float4 pos : SV_Position; float4 a : TEXCOORD0; float4 b : TEXCOORD1; float4 c : TEXCOORD2; float4 d : TEXCOORD3; };
struct PSOut { float4 a : SV_Target0; float4 b : SV_Target1; float4 c : SV_Target2; float4 d : SV_Target3; };
PSOut main(VSOut i) { PSOut o; o.a = i.a; o.b = i.b; o.c = i.c; o.d = i.d; return o; })"},
    // A triangle covering the viewport, and its texture coordinates.
    {"quad.vs", "vs_4_0", "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
                          "V main(uint id : SV_VertexID) { V o; " FULLSCREEN " o.uv = uv; return o; }"},
    // The texel under the pixel, from mip `mip` (cb0).
    {"sample.ps", "ps_4_0", "Texture2D t : register(t0); cbuffer c : register(b0) { uint mip; };\n"
                            "float4 main(float4 pos : SV_Position) : SV_Target { return t.Load(int3(pos.xy, mip)); }"},
    // One colour from cb0.
    {"solid.ps", "ps_4_0", "cbuffer c : register(b0) { float4 color; };\n"
                           "float4 main() : SV_Target { return color; }"},
    // d3d11_render: instanced quads, each its offset, depth and colour (slot 0, per instance), into two targets.
    {"render.vs", "vs_4_0", R"(
struct I { float2 offset : OFFSET; float depth : DEPTH; float4 color : COLOR; };
struct V { float4 pos : SV_Position; float4 color : COLOR; };
V main(uint id : SV_VertexID, I i) {
    float2 uv = float2(id & 1, id >> 1);
    V o;
    o.pos = float4((uv * 0.5 + i.offset) * float2(2, -2) + float2(-1, 1), i.depth, 1);
    o.color = i.color;
    return o;
})"},
    {"render.ps", "ps_4_0", R"(
struct V { float4 pos : SV_Position; float4 color : COLOR; };
struct O { float4 a : SV_Target0; float4 b : SV_Target1; };
O main(V v) { O o; o.a = v.color; o.b = float4(v.color.bgr, 0.5); return o; })"},
    // A triangle over the viewport's lower left half (its edges cross pixels: MSAA's partial coverage).
    {"tri.vs", "vs_4_0", R"(
float4 main(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = {float2(-1, -1), float2(-1, 0.7), float2(1, -1)};  // clockwise: D3D's front face
    return float4(p[id], 0, 1);
})"},
    // d3d11_compute: a structured and a raw UAV.
    {"compute.cs", "cs_5_0", R"(
RWStructuredBuffer<uint> s : register(u0);
RWByteAddressBuffer r : register(u1);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { s[id.x] = id.x * 3 + 1; r.Store(id.x * 4, id.x * 7); })"},
    // A pixel shader with a UAV beside its render target: a pixel counter (u1). (Wine's HLSL compiler has no
    // AppendStructuredBuffer: the programs set and read append views' hidden counters instead.)
    {"omuav.ps", "ps_5_0", R"(
RWStructuredBuffer<uint> u : register(u1);
float4 main(float4 pos : SV_Position) : SV_Target { InterlockedAdd(u[0], 1); return float4(1, 0, 0, 1); })"},
    // d3d11_dynamic: a uint from a small constant buffer, one from both ends of a 64 KiB one, and points.
    {"cb.ps", "ps_4_0", "cbuffer c : register(b0) { uint4 v[16]; };\n"
                        "uint main() : SV_Target { return v[0].x; }"},
    {"cbbig.ps", "ps_4_0", "cbuffer c : register(b0) { uint4 v[4096]; };\n"
                           "uint main() : SV_Target { return v[0].x ^ (v[4095].x << 16); }"},
    {"point.vs", "vs_4_0", "float4 main(float2 p : POSITION) : SV_Position { return float4(p, 0, 1); }"},
    // d3d11_pipeline: a vertex shader whose outputs stream out (as DXMT does it), and hull and domain shaders for a
    // triangle patch.
    {"so.vs", "vs_4_0", R"(
struct V { float4 pos : SV_Position; float value : VALUE; };
V main(float2 p : POSITION, uint id : SV_VertexID) { V o; o.pos = float4(p, 0, 1); o.value = id; return o; })"},
    {"patch.vs", "vs_5_0", "struct V { float4 pos : POSITION; }; V main(float2 p : POSITION) { V o; o.pos = float4(p, 0, 1); return o; }"},
    {"patch.hs", "hs_5_0", R"(
struct V { float4 pos : POSITION; };
struct C { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
C constants(InputPatch<V, 3> p) { C c; c.edges[0] = c.edges[1] = c.edges[2] = 4; c.inside = 4; return c; }
[domain("tri")] [partitioning("integer")] [outputtopology("triangle_cw")] [outputcontrolpoints(3)]
[patchconstantfunc("constants")] V main(InputPatch<V, 3> p, uint i : SV_OutputControlPointID) { return p[i]; })"},
    {"patch.ds", "ds_5_0", R"(
struct V { float4 pos : POSITION; };
struct C { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
[domain("tri")] float4 main(C c, float3 uvw : SV_DomainLocation, const OutputPatch<V, 3> p) : SV_Position {
    return p[0].pos * uvw.x + p[1].pos * uvw.y + p[2].pos * uvw.z;
})"},
};

// Compiles HLSL to `path`: prints "dxbc <name> <bytes>", or the compiler's errors; 0 when it wrote the file.
static int compile(const char *name, const char *source, size_t size, const char *entry, const char *profile,
                   const std::string &path) {
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(source, size, name, nullptr, nullptr, entry, profile, 0, 0, &code, &errors);
    if (FAILED(hr)) {
        printf("dxbc %s failed 0x%08lx %s\n", name, (unsigned long)(unsigned)hr,
               errors ? (const char *)errors->GetBufferPointer() : "");
        return 1;
    }
    FILE *f = fopen(path.c_str(), "wb");
    if (!f || fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), f) != code->GetBufferSize() || fclose(f)) {
        printf("dxbc %s can't write %s\n", name, path.c_str());
        return 1;
    }
    printf("dxbc %s %zu\n", name, (size_t)code->GetBufferSize());
    return 0;
}

static const char *baseName(const char *path) {
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

// One --file group: the HLSL file's bytes (no #include handler: the corpus files hold none), then compile().
static int compileFile(const char *hlsl, const char *entry, const char *profile, const char *out) {
    std::string source;
    FILE *f = fopen(hlsl, "rb");
    char chunk[4096];
    size_t got;
    while (f && (got = fread(chunk, 1, sizeof chunk, f)) > 0) source.append(chunk, got);
    if (!f || ferror(f)) {
        printf("dxbc %s can't read %s\n", baseName(out), hlsl);
        if (f) fclose(f);
        return 1;
    }
    fclose(f);
    // <out>'s file name is the source name, as the folder mode's shader name is: D3DCompile keeps it for its messages.
    return compile(baseName(out), source.c_str(), source.size(), entry, profile, out);
}

int main(int argc, char **argv) {
    if (argc >= 6 && !strcmp(argv[1], "--file")) {
        int failed = 0;
        for (int i = 1; i < argc; i += 5) {
            if (i + 4 >= argc || strcmp(argv[i], "--file")) {
                printf("usage: dxbc.exe --file <hlsl> <entry> <profile> <out> [--file ...]\n");
                return 2;
            }
            failed |= compileFile(argv[i + 1], argv[i + 2], argv[i + 3], argv[i + 4]);
        }
        return failed;
    }
    if (argc != 2) {
        printf("usage: dxbc.exe <folder> | dxbc.exe --file <hlsl> <entry> <profile> <out> [--file ...]\n");
        return 2;
    }
    int failed = 0;
    for (const Source &s : kShaders)
        failed |= compile(s.name, s.hlsl, strlen(s.hlsl), "main", s.profile, std::string(argv[1]) + "/" + s.name + ".dxbc");
    return failed;
}
