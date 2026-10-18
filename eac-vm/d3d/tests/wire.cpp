// Direct3D forwarding's wire cases the 19 programs don't reach (M2 Task 10), a native program run natively and
// through the loopback (both.sh wire <shaders>):
//   bytecode  1 MiB of bytecode through CreateComputePipelineState (the scratch path): its HRESULT
//   input     a graphics pipeline with 32 input elements, each a semantic name: its HRESULT
//   name      SetName with non-ASCII text, read back by GetPrivateData(WKPDID_D3DDebugObjectNameW)
//   adapter   IDXGIAdapter1::GetDesc1's description and ids
// Prints "wire ..." lines.
#include "d3d12_common.hpp"
#include <dxgi1_6.h>
#include <string>

static const GUID name_guid = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};

static std::string utf8(const WCHAR *w, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && w[i]; i++) {
        uint32_t c = (uint32_t)w[i];
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) s += (char)(0xc0 | c >> 6), s += (char)(0x80 | (c & 0x3f));
        else if (c < 0x10000) s += (char)(0xe0 | c >> 12), s += (char)(0x80 | (c >> 6 & 0x3f)), s += (char)(0x80 | (c & 0x3f));
        else s += (char)(0xf0 | c >> 18), s += (char)(0x80 | (c >> 12 & 0x3f)), s += (char)(0x80 | (c >> 6 & 0x3f)),
             s += (char)(0x80 | (c & 0x3f));
    }
    return s;
}

int main(int argc, char **argv) {
    if (argc != 2) return printf("usage: wire <shaders>\n"), 2;
    std::string dir = argv[1];
    Gpu gpu;
    ID3D12RootSignature *root = CbvRootSignature(gpu);

    std::vector<char> big(1 << 20);
    for (size_t i = 0; i < big.size(); i++) big[i] = (char)(i * 31 + 7);
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {root, {big.data(), big.size()}};
    ID3D12PipelineState *pso = nullptr;
    HRESULT hr = gpu.device->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("wire bytecode %08lx\n", (unsigned long)(unsigned)hr);

    auto vs = Load((dir + "/depth.vs.dxil").c_str()), ps = Load((dir + "/depth.ps.dxil").c_str());
    D3D12_INPUT_ELEMENT_DESC elements[32];
    static char names[32][16];
    for (int i = 0; i < 32; i++) {
        snprintf(names[i], sizeof names[i], "ATTR%d", i);
        elements[i] = {names[i], 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
                       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd;
    QuadPipeline(gpu, root, vs, ps, &gd);
    gd.InputLayout = {elements, 32};
    hr = gpu.device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso);
    printf("wire input %08lx\n", (unsigned long)(unsigned)hr);

    const WCHAR text[] = L"caché \U0001F600";
    hr = gpu.queue->SetName(text);
    WCHAR back[64] = {};
    UINT size = sizeof back;
    HRESULT got = gpu.queue->GetPrivateData(name_guid, &size, back);
    printf("wire name %08lx %08lx %u %s\n", (unsigned long)(unsigned)hr, (unsigned long)(unsigned)got, size,
           utf8(back, size / sizeof *back).c_str());

    IDXGIFactory1 *factory = nullptr;
    IDXGIAdapter1 *adapter = nullptr;
    DXGI_ADAPTER_DESC1 desc = {};
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory));
    CHECK(factory->EnumAdapters1(0, &adapter));
    hr = adapter->GetDesc1(&desc);
    printf("wire adapter %08lx %s %04x %04x %llu\n", (unsigned long)(unsigned)hr, utf8(desc.Description, 128).c_str(),
           desc.VendorId, desc.DeviceId, (unsigned long long)desc.DedicatedSystemMemory);
    return 0;
}
