// Creates a graphics and a compute pipeline from DXIL shaders, plus one from an out-of-scope shader (DXIL translator plan):
//   d3d12_dxil.exe <vs.dxil> <ps.dxil> <cs.dxil> <canary.dxil>
// Prints each HRESULT, each line flushed (a runtime that dies on the canary still leaves the lines before it). The
// canary uses an op DXMT doesn't translate (DXIL SM 6.8 plan, Task 9: TextureGatherRaw), which it refuses with E_NOTIMPL
// (0x80004001).
#define WIDL_EXPLICIT_AGGREGATE_RETURNS
#include <windows.h>
#include <d3d12.h>
#include <climits>
#include <cstdio>
#include <vector>

static std::vector<char> load(const char *path) {
    std::vector<char> data;
    if (FILE *f = fopen(path, "rb")) {
        char buffer[4096]; size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    return data;
}

int main(int argc, char **argv) {
    if (argc != 5) { printf("usage: d3d12_dxil.exe <vs.dxil> <ps.dxil> <cs.dxil> <canary.dxil>\n"); fflush(stdout); return 2; }
    std::vector<char> vs = load(argv[1]), ps = load(argv[2]), cs = load(argv[3]), canary = load(argv[4]);
    if (vs.empty() || ps.empty() || cs.empty() || canary.empty()) { printf("can't read the shaders\n"); fflush(stdout); return 1; }

    ID3D12Device *device;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device);
    if (FAILED(hr)) { printf("D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr); fflush(stdout); return 1; }
    // A root UAV at u0, which compute.hlsl writes; a table of t0 and a static point sampler at s0, so that the canary's
    // resources are all bound and only its op is refused. The triangle binds nothing.
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_DESCRIPTOR_RANGE srv = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srv;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_STATIC_SAMPLER_DESC point = {};
    point.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    point.AddressU = point.AddressV = point.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    point.MaxLOD = D3D12_FLOAT32_MAX;
    point.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd = {};
    rd.NumParameters = 2;
    rd.pParameters = params;
    rd.NumStaticSamplers = 1;
    rd.pStaticSamplers = &point;
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob *blob = nullptr, *error = nullptr;
    hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) { printf("D3D12SerializeRootSignature hr=0x%08lx\n", (unsigned long)hr); fflush(stdout); return 1; }
    ID3D12RootSignature *root;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                     __uuidof(ID3D12RootSignature), (void **)&root);
    if (FAILED(hr)) { printf("CreateRootSignature hr=0x%08lx\n", (unsigned long)hr); fflush(stdout); return 1; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
    gd.pRootSignature = root;
    gd.VS = {vs.data(), vs.size()};
    gd.PS = {ps.data(), ps.size()};
    gd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    gd.SampleMask = UINT_MAX;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.RasterizerState.DepthClipEnable = TRUE;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    gd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso = nullptr;
    hr = device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("graphics hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
    // A sample count of 0 (SMITE 2 creates one): D3DMetal is the reference for what it gives.
    gd.SampleDesc.Count = 0;
    hr = device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("graphics-samples0 hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
    // Newer device interfaces (AMD's FSR 3 swapchain needs ID3D12Device8): D3DMetal is the reference.
    const IID devices[] = {__uuidof(ID3D12Device5), __uuidof(ID3D12Device6), __uuidof(ID3D12Device7), __uuidof(ID3D12Device8)};
    for (int i = 0; i < 4; i++) {
        IUnknown *newer = nullptr;
        hr = device->QueryInterface(devices[i], (void **)&newer);
        printf("device%d hr=0x%08lx\n", i + 5, (unsigned long)hr); fflush(stdout);
        if (newer) newer->Release();
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {};
    cd.pRootSignature = root;
    cd.CS = {cs.data(), cs.size()};
    hr = device->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("compute hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);

    cd.CS = {canary.data(), canary.size()};
    hr = device->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("canary hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
    return 0;
}
