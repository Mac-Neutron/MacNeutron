// Runs DXIL behaviour groups through D3D12 and prints each group's output (DXIL translator plan, Task 1):
//   d3d12_dxil_exec.exe <shader folder> [group...]   one line per group: "group <name> ok <1024 hex words>" or "group <name> fail 0x<hr>"
//   d3d12_dxil_exec.exe <shader folder> threads      compiles buffers.dxil on 8 threads at once: "threads ok 8/8"
// heap, heap-swap and any group named heap-<x> run heap.dxil (heap-noflag, heap-noflag-sampler: under a root signature
// missing a heap flag).
// Resources follow dxmt/tests/dxil/common.hlsli. check.sh runs it on our DXMT and on D3DMetal and compares.
#define WIDL_EXPLICIT_AGGREGATE_RETURNS  // D3D12 methods that return structs: the MSVC ABI under mingw
#include <windows.h>
#include <d3d12.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#define CHECK(expr) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    printf("%s failed 0x%08lx\n", #expr, (unsigned long)hr_); exit(1); } } while (0)

static std::vector<char> load(const std::string &path) {
    std::vector<char> data;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        char buffer[4096]; size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    return data;
}

static ID3D12Device *device;
static ID3D12CommandQueue *queue;
static ID3D12CommandAllocator *allocator;
static ID3D12GraphicsCommandList *list;
static ID3D12Fence *fence;
static UINT64 fence_value;
static HANDLE fence_event;

static ID3D12Resource *Buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES heap = {type};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = flags;
    ID3D12Resource *r;
    CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, __uuidof(ID3D12Resource), (void **)&r));
    return r;
}

static ID3D12Resource *Texture(UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags,
                              UINT16 mips = 1) {
    D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = w; desc.Height = h; desc.DepthOrArraySize = 1;
    desc.MipLevels = mips; desc.Format = format; desc.SampleDesc.Count = 1; desc.Flags = flags;
    ID3D12Resource *r;
    CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, __uuidof(ID3D12Resource), (void **)&r));
    return r;
}

static void Fill(ID3D12Resource *upload, const void *data, size_t size) {
    void *p; D3D12_RANGE none = {0, 0};
    CHECK(upload->Map(0, &none, &p)); memcpy(p, data, size); upload->Unmap(0, nullptr);
}

static void Barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

// Closes the list, runs it, waits, and reopens it.
static void Flush() {
    CHECK(list->Close());
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    CHECK(queue->Signal(fence, ++fence_value));
    if (fence->GetCompletedValue() < fence_value) {
        CHECK(fence->SetEventOnCompletion(fence_value, fence_event));
        if (WaitForSingleObject(fence_event, 10000) != WAIT_OBJECT_0) { printf("GPU work never finished\n"); exit(1); }
    }
    CHECK(allocator->Reset()); CHECK(list->Reset(allocator, nullptr));
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: d3d12_dxil_exec.exe <shader folder> [group...|threads]\n"); return 2; }
    std::string folder = argv[1];
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device));
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue));
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));
    fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    // Root signature: b0 root CBV; one table of t0-t6 then u0-u4; static samplers s0 (linear), s1 (point) and s2 (point
    // comparison, LESS_EQUAL).
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 7; ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 5; ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 7;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2; params[1].DescriptorTable.pDescriptorRanges = ranges;
    D3D12_STATIC_SAMPLER_DESC samplers[3] = {};
    for (int s = 0; s < 3; s++) {
        samplers[s].Filter = s == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                             : s == 1 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        samplers[s].AddressU = samplers[s].AddressV = samplers[s].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[s].MaxLOD = D3D12_FLOAT32_MAX; samplers[s].ShaderRegister = s;
    }
    samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 3, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob = nullptr, *error = nullptr;
    CHECK(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ID3D12RootSignature *root;
    CHECK(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root));

    // threads: 8 pipelines from one shader at once (Unreal creates PSOs from worker threads).
    if (argc > 2 && std::string(argv[2]) == "threads") {
        std::vector<char> cs = load(folder + "/buffers.dxil");
        std::vector<std::thread> threads;
        std::vector<HRESULT> results(8, E_FAIL);
        for (int t = 0; t < 8; t++) threads.emplace_back([&, t] {
            D3D12_COMPUTE_PIPELINE_STATE_DESC d = {}; d.pRootSignature = root; d.CS = {cs.data(), cs.size()};
            ID3D12PipelineState *pso = nullptr;
            results[t] = device->CreateComputePipelineState(&d, __uuidof(ID3D12PipelineState), (void **)&pso);
            if (pso) pso->Release();
        });
        int ok = 0;
        for (int t = 0; t < 8; t++) { threads[t].join(); ok += SUCCEEDED(results[t]); }
        printf("threads %s %d/8\n", ok == 8 ? "ok" : "fail", ok);
        return 0;
    }

    // Inputs, filled as common.hlsli describes.
    std::vector<uint32_t> in(2048);
    for (uint32_t i = 0; i < 1024; i++) {
        in[i] = i * 2654435761u;
        float f = ((float)i - 512.0f) / 37.0f; memcpy(&in[1024 + i], &f, 4);
    }
    float cb[20];
    for (int i = 0; i < 4; i++) { cb[i * 4] = i + 1; cb[i * 4 + 1] = -(i + 1) / 2.0f; cb[i * 4 + 2] = (i + 1) * 0.25f; cb[i * 4 + 3] = 7; }
    uint32_t cbu[4] = {3, 5, 7, 11}; memcpy(&cb[16], cbu, 16);
    std::vector<float> typed(256), arr0(256), arr1(256);
    std::vector<uint32_t> structured(64 * 5);
    for (int i = 0; i < 64; i++) {
        typed[i * 4] = i; typed[i * 4 + 1] = i * 0.5f; typed[i * 4 + 2] = -i; typed[i * 4 + 3] = 1;
        arr0[i * 4] = i; arr1[i * 4 + 1] = i;
        float a[4] = {(float)i, 2.0f * i, 3.0f * i, 4.0f * i}; memcpy(&structured[i * 5], a, 16); structured[i * 5 + 4] = i ^ 0x5a5a;
    }
    ID3D12Resource *cbuf = Buffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(cbuf, cb, sizeof cb);
    ID3D12Resource *inbuf = Buffer(8192, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(inbuf, in.data(), 8192);
    ID3D12Resource *typedbuf = Buffer(1024, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(typedbuf, typed.data(), 1024);
    ID3D12Resource *structbuf = Buffer(64 * 20, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(structbuf, structured.data(), 64 * 20);
    ID3D12Resource *arrbuf[2] = {Buffer(1024, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ),
                                 Buffer(1024, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ)};
    Fill(arrbuf[0], arr0.data(), 1024); Fill(arrbuf[1], arr1.data(), 1024);

    // Tex: 8x8 RGBA8, uploaded through a 256-byte-pitched buffer.
    ID3D12Resource *tex = Texture(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    std::vector<uint8_t> texels(256 * 8);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        uint8_t *t = &texels[y * 256 + x * 4]; t[0] = x * 32; t[1] = y * 32; t[2] = (x + y) * 16; t[3] = 255;
    }
    ID3D12Resource *texup = Buffer(texels.size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(texup, texels.data(), texels.size());
    D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {texup, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, 1, 256};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // Depth: 8x8 D32 with 4 mips, mip m all 0.125 + 0.25 m, each copied whole from its own 2048-byte block (pitch 256).
    ID3D12Resource *depth = Texture(8, 8, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_STATE_COPY_DEST,
                                    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 4);
    std::vector<float> depths(4 * 512);
    for (size_t i = 0; i < depths.size(); i++) depths[i] = 0.125f + 0.25f * (i / 512);
    ID3D12Resource *depthup = Buffer(depths.size() * 4, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    Fill(depthup, depths.data(), depths.size() * 4);
    for (UINT m = 0; m < 4; m++) {
        D3D12_TEXTURE_COPY_LOCATION to = {depth, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; to.SubresourceIndex = m;
        D3D12_TEXTURE_COPY_LOCATION from = {depthup, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        from.PlacedFootprint.Offset = m * 2048; from.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, 8u >> m, 8u >> m, 1, 256};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    Barrier(depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // UAVs, zeroed from an upload buffer of zeros.
    std::vector<uint8_t> zeros(4096, 0);
    ID3D12Resource *zerobuf = Buffer(4096, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); Fill(zerobuf, zeros.data(), 4096);
    ID3D12Resource *out = Buffer(4096, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *rwtyped = Buffer(256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    list->CopyBufferRegion(rwtyped, 0, zerobuf, 0, 256);
    Barrier(rwtyped, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource *rwtex = Texture(8, 8, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *atomics = Buffer(65 * 8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *atomictex = Texture(8, 8, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *readback = Buffer(4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Flush();

    // Descriptors: t0-t6 at 0-6, u0-u4 at 7-11 (the heap groups add b0's buffer at 12).
    ID3D12DescriptorHeap *heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 13, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECK(device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&heap));
    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    auto slot = [&](int i) { D3D12_CPU_DESCRIPTOR_HANDLE h = cpu; h.ptr += i * inc; return h; };
    auto buffer_srv = [&](ID3D12Resource *r, int i, DXGI_FORMAT f, UINT n, UINT stride, D3D12_BUFFER_SRV_FLAGS flags) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d = {}; d.Format = f; d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Buffer.NumElements = n; d.Buffer.StructureByteStride = stride; d.Buffer.Flags = flags;
        device->CreateShaderResourceView(r, &d, slot(i));
    };
    auto views = [&](ID3D12DescriptorHeap *h) {  // slot() writes to h from here on
        cpu = h->GetCPUDescriptorHandleForHeapStart();
        buffer_srv(inbuf, 0, DXGI_FORMAT_R32_TYPELESS, 2048, 0, D3D12_BUFFER_SRV_FLAG_RAW);
        device->CreateShaderResourceView(tex, nullptr, slot(1));
        buffer_srv(typedbuf, 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 0, D3D12_BUFFER_SRV_FLAG_NONE);
        buffer_srv(structbuf, 3, DXGI_FORMAT_UNKNOWN, 64, 20, D3D12_BUFFER_SRV_FLAG_NONE);
        buffer_srv(arrbuf[0], 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 0, D3D12_BUFFER_SRV_FLAG_NONE);
        buffer_srv(arrbuf[1], 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 64, 0, D3D12_BUFFER_SRV_FLAG_NONE);
        D3D12_SHADER_RESOURCE_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_R32_FLOAT; dv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        dv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; dv.Texture2D.MipLevels = 4;
        device->CreateShaderResourceView(depth, &dv, slot(6));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u = {}; u.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        u.Format = DXGI_FORMAT_R32_TYPELESS; u.Buffer.NumElements = 1024; u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        device->CreateUnorderedAccessView(out, nullptr, &u, slot(7));
        u.Format = DXGI_FORMAT_R32_UINT; u.Buffer.NumElements = 64; u.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(rwtyped, nullptr, &u, slot(8));
        device->CreateUnorderedAccessView(rwtex, nullptr, nullptr, slot(9));
        u.Format = DXGI_FORMAT_UNKNOWN; u.Buffer.NumElements = 65; u.Buffer.StructureByteStride = 8;
        device->CreateUnorderedAccessView(atomics, nullptr, &u, slot(10));
        device->CreateUnorderedAccessView(atomictex, nullptr, nullptr, slot(11));
    };
    views(heap);

    const char *all[] = {"buffers", "math", "transcendental", "textures", "groupshared", "wave", "half", "packed", "atomics", "quad", "specials", "mad", "barriers"};
    std::vector<std::string> groups;
    for (int i = 2; i < argc; i++) groups.push_back(argv[i]);
    if (groups.empty()) groups.assign(std::begin(all), std::end(all));

    // The heap groups (DXIL SM 6.8 plan, Task 10), made only when one runs (the D3DMetal run has none). root_heap: `root`
    // plus 3 root constants at b1 (heap.hlsl doesn't read them; they round up to two qwords, so the heap bases follow
    // at qword 4), flags 0xC00 (llvm-mingw lacks their names). A sampler heap of [0] linear clamp, [1] point clamp.
    // b0's buffer at slot 12. heap2: heap's views, except a raw view of zeros at slot 0.
    ID3D12RootSignature *root_heap = nullptr;
    ID3D12DescriptorHeap *sampler_heap = nullptr, *heap2 = nullptr;
    if (std::any_of(groups.begin(), groups.end(), [](const std::string &g) { return g.rfind("heap", 0) == 0; })) {
        D3D12_ROOT_PARAMETER hp[3] = {params[0], params[1], {}};
        hp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        hp[2].Constants.ShaderRegister = 1; hp[2].Constants.Num32BitValues = 3;
        D3D12_ROOT_SIGNATURE_DESC hrd = {3, hp, 3, samplers, (D3D12_ROOT_SIGNATURE_FLAGS)0xC00};
        ID3DBlob *b = nullptr;
        CHECK(D3D12SerializeRootSignature(&hrd, D3D_ROOT_SIGNATURE_VERSION_1, &b, &error));
        CHECK(device->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root_heap));
        D3D12_DESCRIPTOR_HEAP_DESC sd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        CHECK(device->CreateDescriptorHeap(&sd, __uuidof(ID3D12DescriptorHeap), (void **)&sampler_heap));
        D3D12_CPU_DESCRIPTOR_HANDLE s = sampler_heap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < 2; i++) {
            D3D12_SAMPLER_DESC d = {};
            d.Filter = i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            d.AddressU = d.AddressV = d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; d.MaxLOD = D3D12_FLOAT32_MAX;
            device->CreateSampler(&d, s); s.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        D3D12_CONSTANT_BUFFER_VIEW_DESC cv = {cbuf->GetGPUVirtualAddress(), 256};
        device->CreateConstantBufferView(&cv, slot(12));
        CHECK(device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&heap2));
        views(heap2);
        device->CreateConstantBufferView(&cv, slot(12));
        buffer_srv(zerobuf, 0, DXGI_FORMAT_R32_TYPELESS, 1024, 0, D3D12_BUFFER_SRV_FLAG_RAW);
    }

    for (auto &name : groups) {
        // heap-noflag and heap-noflag-sampler (DXIL SM 6.8 plan, Task 9): heap.dxil under `root`, which has neither heap
        // flag, and under its description with CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED (0x400) alone: no sampler heap flag.
        std::vector<char> cs = load(folder + "/" + (name.rfind("heap-", 0) == 0 ? std::string("heap") : name) + ".dxil");
        if (cs.empty()) { printf("group %s fail missing\n", name.c_str()); continue; }
        ID3D12RootSignature *rs = name == "heap" || name == "heap-swap" ? root_heap : root;
        if (name == "heap-noflag-sampler") {
            D3D12_ROOT_SIGNATURE_DESC views = rd; views.Flags = (D3D12_ROOT_SIGNATURE_FLAGS)0x400; // llvm-mingw lacks its name
            ID3DBlob *b = nullptr;
            CHECK(D3D12SerializeRootSignature(&views, D3D_ROOT_SIGNATURE_VERSION_1, &b, &error));
            CHECK(device->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&rs));
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC d = {}; d.pRootSignature = rs; d.CS = {cs.data(), cs.size()};
        ID3D12PipelineState *pso = nullptr;
        HRESULT hr = device->CreateComputePipelineState(&d, __uuidof(ID3D12PipelineState), (void **)&pso);
        if (FAILED(hr)) { printf("group %s fail 0x%08lx\n", name.c_str(), (unsigned long)hr); fflush(stdout); continue; }
        // Every group starts from zeroed UAVs (RWTyped, AtomicS and AtomicTex are zeroed here too; RWTex isn't read).
        list->CopyBufferRegion(out, 0, zerobuf, 0, 4096);
        Barrier(out, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier(rwtyped, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(rwtyped, 0, zerobuf, 0, 256);
        Barrier(rwtyped, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->CopyBufferRegion(atomics, 0, zerobuf, 0, 65 * 8);
        Barrier(atomics, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_TEXTURE_COPY_LOCATION to = {atomictex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; to.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION from = {zerobuf, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_UINT, 8, 8, 1, 256};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        Barrier(atomictex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap *bound[2] = {heap, sampler_heap};
        if (rs == root_heap) {  // the heaps first: setting the root signature brings their bases in
            list->SetDescriptorHeaps(2, bound);
            list->SetComputeRootSignature(rs);
            const UINT constants[3] = {1, 2, 3};
            list->SetComputeRoot32BitConstants(2, 3, constants, 0);
        } else {
            list->SetComputeRootSignature(rs);
            list->SetDescriptorHeaps(1, &heap);
        }
        list->SetComputeRootConstantBufferView(0, cbuf->GetGPUVirtualAddress());
        list->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(pso);
        list->Dispatch(1, 1, 1);
        if (name == "heap-swap") {  // heap2 bound between two dispatches in one list: the second reads it, and is printed
            D3D12_RESOURCE_BARRIER uav[2] = {};
            uav[0].Type = uav[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav[0].UAV.pResource = out; uav[1].UAV.pResource = rwtyped;
            list->ResourceBarrier(2, uav);
            bound[0] = heap2;
            list->SetDescriptorHeaps(2, bound);
            list->SetComputeRootDescriptorTable(1, heap2->GetGPUDescriptorHandleForHeapStart());
            list->Dispatch(1, 1, 1);
        }
        Barrier(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(readback, 0, out, 0, 4096);
        Barrier(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier(atomics, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier(atomictex, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        Flush();
        uint32_t *words; D3D12_RANGE whole = {0, 4096};
        CHECK(readback->Map(0, &whole, (void **)&words));
        printf("group %s ok", name.c_str());
        for (int i = 0; i < 1024; i++) printf(" %08x", words[i]);
        printf("\n"); fflush(stdout);
        D3D12_RANGE none = {0, 0}; readback->Unmap(0, &none);
        pso->Release();
    }
    return 0;
}
