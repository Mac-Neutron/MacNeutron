// Draws dxmt/tests/shaders/triangle2.hlsl offscreen through D3D12 and prints the result (DXIL translator plan, Task 3):
//   d3d12_triangle.exe <vs.dxil> <ps.dxil> [gs.dxil] [--heap] [--start <vertex> <instance>] [--atomic64]
// --heap (DXIL SM 6.8 plan, Task 10): root signature flags 0xC00 (both heaps directly indexed), and a shader-visible
// sampler heap of one linear clamp sampler bound beside the view heap.
// --start (Task 12): the corners sit at vertex <vertex> of the vertex buffer, drawn with DrawInstanced(3, 1, <vertex>,
// <instance>).
// --atomic64 (Task 14a): a root UAV at u1 (visibility all; no heap flags, so the lock qword follows it: qword 3) bound
// to a zeroed 16-byte buffer, and after the draw "atomic64 pixels <n> sum <s> max <16 hex>": n the pixels that aren't
// the clear colour, s the buffer's first qword, max its second.
// Prints "triangle ok <FNV-1a 64 of all pixels> <8 sampled pixels>" or "triangle fail 0x<hr>". check.sh compares the
// pixels between our DXMT and D3DMetal.
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

static D3D12_RESOURCE_DESC TextureDesc(UINT w, UINT h, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Flags = flags;
    return d;
}

static void Barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

int main(int argc, char **argv) {
    int n = 1;  // the shaders: the arguments before the first -- option
    while (n < argc && strncmp(argv[n], "--", 2)) n++;
    bool heaps = false, atomic64 = false;
    UINT start_vertex = 0, start_instance = 0;
    for (int i = n; i < argc; i++)
        if (!strcmp(argv[i], "--heap")) heaps = true;
        else if (!strcmp(argv[i], "--start") && i + 2 < argc) start_vertex = atoi(argv[++i]), start_instance = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--atomic64")) atomic64 = true;
        else n = 0;  // an unknown option: the usage line
    if (n != 3 && n != 4) {
        printf("usage: d3d12_triangle.exe <vs.dxil> <ps.dxil> [gs.dxil] [--heap] [--start <vertex> <instance>] [--atomic64]\n");
        return 2;
    }
    std::vector<char> vs = load(argv[1]), ps = load(argv[2]), gs = n == 4 ? load(argv[3]) : std::vector<char>();
    if (vs.empty() || ps.empty() || (n == 4 && gs.empty())) { printf("can't read the shaders\n"); return 1; }
    const UINT size = 64;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device));
    ID3D12CommandQueue *queue; ID3D12CommandAllocator *allocator; ID3D12GraphicsCommandList *list; ID3D12Fence *fence;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue));
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));

    // Root signature: b0 root CBV, a table with t0, static point sampler s0; --atomic64: a root UAV at u1.
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &range;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; params[2].Descriptor.ShaderRegister = 1;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC rd = {atomic64 ? 3u : 2u, params, 1, &sampler, (D3D12_ROOT_SIGNATURE_FLAGS)(
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | (heaps ? 0xC00 : 0))};  // llvm-mingw lacks their names
    ID3DBlob *blob = nullptr, *error = nullptr;
    CHECK(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ID3D12RootSignature *root;
    CHECK(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
    gd.pRootSignature = root;
    gd.VS = {vs.data(), vs.size()};
    D3D12_INPUT_ELEMENT_DESC corner = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    gd.InputLayout = {&corner, 1};
    gd.PS = {ps.data(), ps.size()};
    gd.GS = {gs.data(), gs.size()};
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
    HRESULT hr = device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso);
    if (FAILED(hr)) { printf("triangle fail 0x%08lx\n", (unsigned long)(unsigned)hr); return 0; }

    // Constants: scale (0.9, 0.9), offset (0.05, -0.05).
    float cb[4] = {0.9f, 0.9f, 0.05f, -0.05f};
    ID3D12Resource *cbuf = Resource(D3D12_HEAP_TYPE_UPLOAD, BufferDesc(256), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p; D3D12_RANGE none = {0, 0};
    CHECK(cbuf->Map(0, &none, &p)); memcpy(p, cb, sizeof cb); cbuf->Unmap(0, nullptr);

    // Vertex buffer: the three corners, in texture coordinates, from vertex start_vertex (zeros before).
    const float corners[6] = {0, 0, 2, 0, 0, 2};
    const UINT vb_size = sizeof corners + start_vertex * 8;
    ID3D12Resource *vbuf = Resource(D3D12_HEAP_TYPE_UPLOAD, BufferDesc(256 + start_vertex * 8), D3D12_RESOURCE_STATE_GENERIC_READ);
    CHECK(vbuf->Map(0, &none, &p)); memset(p, 0, vb_size); memcpy((char *)p + start_vertex * 8, corners, sizeof corners);
    vbuf->Unmap(0, nullptr);
    D3D12_VERTEX_BUFFER_VIEW vbv = {vbuf->GetGPUVirtualAddress(), vb_size, 8};

    // Texture: 2x2 red, green, blue, white, uploaded through a 256-byte-pitched buffer.
    ID3D12Resource *tex = Resource(D3D12_HEAP_TYPE_DEFAULT, TextureDesc(2, 2, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COPY_DEST);
    uint8_t texels[512] = {};
    const uint32_t colors[4] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};
    memcpy(&texels[0], &colors[0], 8); memcpy(&texels[256], &colors[2], 8);
    ID3D12Resource *up = Resource(D3D12_HEAP_TYPE_UPLOAD, BufferDesc(512), D3D12_RESOURCE_STATE_GENERIC_READ);
    CHECK(up->Map(0, &none, &p)); memcpy(p, texels, 512); up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {up, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 2, 2, 1, 256};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Barrier(list, tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // --atomic64: Count, 16 bytes zeroed by a copy from `up`'s zeros (bytes 16-31).
    D3D12_RESOURCE_DESC count_desc = BufferDesc(16);
    count_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *count = nullptr, *count_readback = nullptr;
    if (atomic64) {
        count = Resource(D3D12_HEAP_TYPE_DEFAULT, count_desc, D3D12_RESOURCE_STATE_COPY_DEST);
        count_readback = Resource(D3D12_HEAP_TYPE_READBACK, BufferDesc(16), D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(count, 0, up, 16, 16);
        Barrier(list, count, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    ID3D12DescriptorHeap *srv_heap, *rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECK(device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&srv_heap));
    device->CreateShaderResourceView(tex, nullptr, srv_heap->GetCPUDescriptorHandleForHeapStart());
    ID3D12DescriptorHeap *bound[2] = {srv_heap, nullptr};
    if (heaps) {
        D3D12_DESCRIPTOR_HEAP_DESC shd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
        CHECK(device->CreateDescriptorHeap(&shd, __uuidof(ID3D12DescriptorHeap), (void **)&bound[1]));
        D3D12_SAMPLER_DESC sd = {D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                 D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP};
        sd.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&sd, bound[1]->GetCPUDescriptorHandleForHeapStart());
    }
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
    CHECK(device->CreateDescriptorHeap(&rhd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 1}};
    ID3D12Resource *target = Resource(D3D12_HEAP_TYPE_DEFAULT, TextureDesc(size, size, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                      D3D12_RESOURCE_STATE_RENDER_TARGET, &clear);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target, nullptr, rtv);
    ID3D12Resource *readback = Resource(D3D12_HEAP_TYPE_READBACK, BufferDesc(256 * size), D3D12_RESOURCE_STATE_COPY_DEST);

    const float black[4] = {0, 0, 0, 1};
    list->ClearRenderTargetView(rtv, black, 0, nullptr);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT viewport = {0, 0, (float)size, (float)size, 0, 1};
    D3D12_RECT scissor = {0, 0, (LONG)size, (LONG)size};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(root);
    list->SetDescriptorHeaps(heaps ? 2 : 1, bound);
    list->SetGraphicsRootConstantBufferView(0, cbuf->GetGPUVirtualAddress());
    list->SetGraphicsRootDescriptorTable(1, srv_heap->GetGPUDescriptorHandleForHeapStart());
    if (atomic64)
        list->SetGraphicsRootUnorderedAccessView(2, count->GetGPUVirtualAddress());
    list->SetPipelineState(pso);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv);
    list->DrawInstanced(3, 1, start_vertex, start_instance);
    Barrier(list, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from = {target, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; from.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION to = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 256};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    if (atomic64) {
        Barrier(list, count, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(count_readback, 0, count, 0, 16);
    }
    CHECK(list->Close());
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    CHECK(queue->Signal(fence, 1));
    HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(fence->SetEventOnCompletion(1, done));
    if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) { printf("triangle fail timeout\n"); return 1; }

    uint8_t *pixels; D3D12_RANGE whole = {0, 256 * size};
    CHECK(readback->Map(0, &whole, (void **)&pixels));
    uint64_t hash = 0xcbf29ce484222325ull;
    for (UINT y = 0; y < size; y++)
        for (UINT x = 0; x < size * 4; x++) hash = (hash ^ pixels[y * 256 + x]) * 0x100000001b3ull;
    // The last four are inside what triangle2's geometry shader draws: its small copy (top right), its shrunk triangle.
    const int at[12][2] = {{8, 8}, {20, 8}, {40, 8}, {8, 20}, {20, 20}, {8, 40}, {30, 30}, {50, 50}, {52, 8}, {56, 12},
                           {60, 6}, {14, 14}};
    printf("triangle ok %016llx", (unsigned long long)hash);
    for (auto &xy : at) { uint32_t px; memcpy(&px, &pixels[xy[1] * 256 + xy[0] * 4], 4); printf(" %08x", px); }
    printf("\n");
    if (atomic64) {
        unsigned n = 0;
        for (UINT y = 0; y < size; y++)
            for (UINT x = 0; x < size; x++) { uint32_t px; memcpy(&px, &pixels[y * 256 + x * 4], 4); n += px != 0xff000000; }
        uint64_t q[2]; D3D12_RANGE sixteen = {0, 16}; void *c;
        CHECK(count_readback->Map(0, &sixteen, &c)); memcpy(q, c, 16); count_readback->Unmap(0, &none);
        printf("atomic64 pixels %u sum %llu max %016llx\n", n, (unsigned long long)q[0], (unsigned long long)q[1]);
    }
    readback->Unmap(0, &none);
    return 0;
}
