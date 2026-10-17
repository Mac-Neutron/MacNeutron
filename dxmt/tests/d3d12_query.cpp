// Occlusion queries as Unreal uses them (SMITE 2's lobby culls whatever reads 0), drawn offscreen through D3D12:
//   d3d12_query.exe <vs.dxil> <ps.dxil>   (shaders/depth.hlsl: a quad from a rect in a root CBV)
// Queries into one OCCLUSION heap, resolved into a readback buffer:
//   q0 a 16x16 quad (256), q1 an 8x8 quad (64), q2 no draw (0), q3 BINARY_OCCLUSION around the 16x16 quad (1),
//   q4 the 16x16 quad, a render target change, the 8x8 quad (320), q5 the 8x8 quad twice, instanced (128);
//   then, in a second submission, q0 again around the 8x8 quad (64): a resolved query starts over.
// Prints "query ok <q0> <q1> <q2> <q3> <q4> <q5> <q0 again>" or "query fail ...". check.sh compares with D3DMetal.
#define WIDL_EXPLICIT_AGGREGATE_RETURNS  // D3D12 methods that return structs: the MSVC ABI under mingw
#include <windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(expr) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    printf("%s failed 0x%08lx\n", #expr, (unsigned long)(unsigned)hr_); exit(1); } } while (0)

static std::vector<char> load(const char *path) {
    std::vector<char> data;
    if (FILE *f = fopen(path, "rb")) {
        char buffer[4096]; size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    return data;
}

static ID3D12Device *device;

static ID3D12Resource *Resource(D3D12_HEAP_TYPE type, const D3D12_RESOURCE_DESC &desc, D3D12_RESOURCE_STATES state,
                                const D3D12_CLEAR_VALUE *clear = nullptr) {
    D3D12_HEAP_PROPERTIES heap = {type};
    ID3D12Resource *r;
    CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear, __uuidof(ID3D12Resource), (void **)&r));
    return r;
}

static D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

int main(int argc, char **argv) {
    if (argc != 3) { printf("usage: d3d12_query.exe <vs.dxil> <ps.dxil>\n"); return 2; }
    std::vector<char> vs = load(argv[1]), ps = load(argv[2]);
    if (vs.empty() || ps.empty()) { printf("can't read the shaders\n"); return 1; }
    const UINT size = 64;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device));
    ID3D12CommandQueue *queue; ID3D12CommandAllocator *allocator; ID3D12GraphicsCommandList *list; ID3D12Fence *fence;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue));
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    D3D12_ROOT_SIGNATURE_DESC rd = {1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob = nullptr, *error = nullptr;
    CHECK(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ID3D12RootSignature *root;
    CHECK(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root));

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
    ID3D12PipelineState *pso;
    CHECK(device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));

    // Quads on pixel edges of the 64x64 target (8 pixels = 0.25 in NDC): 16x16 at the bottom left, 8x8 at the centre.
    struct Draw { float rect[4], color[4], z, pad[55]; };
    const Draw draws[] = {
        {{-1, -1, -0.5f, -0.5f}, {1, 0, 0, 1}, 0},
        {{0, 0, 0.25f, 0.25f}, {0, 1, 0, 1}, 0},
    };
    ID3D12Resource *cbuf = Resource(D3D12_HEAP_TYPE_UPLOAD, BufferDesc(sizeof draws), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p; D3D12_RANGE none = {0, 0};
    CHECK(cbuf->Map(0, &none, &p)); memcpy(p, draws, sizeof draws); cbuf->Unmap(0, nullptr);

    ID3D12DescriptorHeap *rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2};
    CHECK(device->CreateDescriptorHeap(&rhd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    UINT rtv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
    for (int i = 0; i < 2; i++) {
        D3D12_RESOURCE_DESC d = BufferDesc(size);
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Height = size; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ID3D12Resource *target = Resource(D3D12_HEAP_TYPE_DEFAULT, d, D3D12_RESOURCE_STATE_RENDER_TARGET);
        rtvs[i] = rtv_heap->GetCPUDescriptorHandleForHeapStart(); rtvs[i].ptr += i * rtv_step;
        device->CreateRenderTargetView(target, nullptr, rtvs[i]);
    }

    D3D12_QUERY_HEAP_DESC qhd = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 8};
    ID3D12QueryHeap *queries;
    CHECK(device->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void **)&queries));
    // Filled with a marker first: a resolve that writes nothing shows as 0x5555555555555555.
    ID3D12Resource *results = Resource(D3D12_HEAP_TYPE_READBACK, BufferDesc(512), D3D12_RESOURCE_STATE_COPY_DEST);
    CHECK(results->Map(0, &none, &p)); memset(p, 0x55, 512); results->Unmap(0, nullptr);

    D3D12_VIEWPORT viewport = {0, 0, (float)size, (float)size, 0, 1};
    D3D12_RECT scissor = {0, 0, (LONG)size, (LONG)size};
    const float black[4] = {0, 0, 0, 1};
    auto setup = [&](int target) {
        list->OMSetRenderTargets(1, &rtvs[target], FALSE, nullptr);
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->SetGraphicsRootSignature(root);
        list->SetPipelineState(pso);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    };
    auto draw = [&](int i, UINT instances = 1) {
        list->SetGraphicsRootConstantBufferView(0, cbuf->GetGPUVirtualAddress() + i * sizeof(Draw));
        list->DrawInstanced(6, instances, 0, 0);
    };
    auto submit = [&](UINT64 value) {
        CHECK(list->Close());
        ID3D12CommandList *lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        CHECK(queue->Signal(fence, value));
        HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        CHECK(fence->SetEventOnCompletion(value, done));
        if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) { printf("query fail timeout\n"); exit(1); }
    };
    const D3D12_QUERY_TYPE OCC = D3D12_QUERY_TYPE_OCCLUSION;

    list->ClearRenderTargetView(rtvs[0], black, 0, nullptr);
    list->ClearRenderTargetView(rtvs[1], black, 0, nullptr);
    setup(0);
    list->BeginQuery(queries, OCC, 0); draw(0); list->EndQuery(queries, OCC, 0);
    list->BeginQuery(queries, OCC, 1); draw(1); list->EndQuery(queries, OCC, 1);
    list->BeginQuery(queries, OCC, 2); list->EndQuery(queries, OCC, 2);
    list->BeginQuery(queries, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3); draw(0);
    list->EndQuery(queries, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3);
    list->BeginQuery(queries, OCC, 4); draw(0); setup(1); draw(1); list->EndQuery(queries, OCC, 4);
    list->BeginQuery(queries, OCC, 5); draw(1, 2); list->EndQuery(queries, OCC, 5);
    list->ResolveQueryData(queries, OCC, 0, 3, results, 0);
    list->ResolveQueryData(queries, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 3, 1, results, 24);
    list->ResolveQueryData(queries, OCC, 4, 2, results, 32);
    submit(1);

    CHECK(allocator->Reset());
    CHECK(list->Reset(allocator, nullptr));
    setup(0);
    list->BeginQuery(queries, OCC, 0); draw(1); list->EndQuery(queries, OCC, 0);
    list->ResolveQueryData(queries, OCC, 0, 1, results, 256);
    submit(2);

    uint64_t *r; D3D12_RANGE whole = {0, 512};
    CHECK(results->Map(0, &whole, (void **)&r));
    printf("query ok %llu %llu %llu %llu %llu %llu %llu\n", (unsigned long long)r[0], (unsigned long long)r[1],
           (unsigned long long)r[2], (unsigned long long)r[3], (unsigned long long)r[4], (unsigned long long)r[5],
           (unsigned long long)r[32]);
    results->Unmap(0, &none);
    return 0;
}
