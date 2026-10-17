// Depth and stencil as Unreal uses them (SMITE 2's lobby), drawn offscreen through D3D12:
//   d3d12_depth.exe <vs.dxil> <ps.dxil> <psdepth.dxil>
// A R32G8X24_TYPELESS depth-stencil cleared to 0 (reversed Z), pipeline states from streams with DEPTH_STENCIL1:
//   pass 1: a near quad (z 0.75, stencil 1) then a far quad (z 0.25) with GREATER_EQUAL, writing depth and stencil;
//   pass 2: through a read-only DSV, yellow where depth EQUALs 0.25, magenta at the left where stencil is 1;
//   pass 3: the depth through a R32_FLOAT_X8X24_TYPELESS SRV, into a second target;
//   pass 4 (third target): a depth-read-only view (stencil writable) under a pipeline that writes both, and a
//   stencil-read-only view under one that writes depth only; then the depth sampled while bound read-only, and where
//   stencil is 2. (Writing a read-only plane is invalid D3D12; D3DMetal keeps depth but writes stencil.)
// Prints "depth ok <FNV-1a 64 of pass 2's pixels> <8 pixels of pass 2> <4 of pass 3>" or "depth fail ...". check.sh
// compares the pixels between our DXMT and D3DMetal; a second line, "depth2 ok ...", has pass 4's 12 pixels.
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

static ID3D12Device2 *device;

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

static D3D12_RESOURCE_DESC TextureDesc(UINT w, UINT h, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = format; d.SampleDesc.Count = 1; d.Flags = flags;
    return d;
}

static void Barrier(ID3D12GraphicsCommandList *list, ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before; b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T> struct alignas(void *) Sub {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
    T value;
};

// A graphics pipeline state through a stream, as Unreal creates them.
static ID3D12PipelineState *Pipeline(ID3D12RootSignature *root, const std::vector<char> &vs, const std::vector<char> &ps,
                                     const D3D12_DEPTH_STENCIL_DESC1 &depth, DXGI_FORMAT dsv) {
    struct {
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature *> root;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1> depth;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> dsv;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> rtvs;
        Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
    } stream = {};
    stream.root.value = root;
    stream.vs.value = {vs.data(), vs.size()};
    stream.ps.value = {ps.data(), ps.size()};
    stream.rasterizer.value.FillMode = D3D12_FILL_MODE_SOLID;
    stream.rasterizer.value.CullMode = D3D12_CULL_MODE_NONE;
    stream.rasterizer.value.DepthClipEnable = TRUE;
    stream.depth.value = depth;
    stream.dsv.value = dsv;
    stream.rtvs.value.NumRenderTargets = 1;
    stream.rtvs.value.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    stream.topology.value = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    D3D12_PIPELINE_STATE_STREAM_DESC sd = {sizeof stream, &stream};
    ID3D12PipelineState *pso = nullptr;
    HRESULT hr = device->CreatePipelineState(&sd, __uuidof(ID3D12PipelineState), (void **)&pso);
    if (FAILED(hr)) { printf("depth fail CreatePipelineState 0x%08lx\n", (unsigned long)(unsigned)hr); exit(0); }
    return pso;
}

static D3D12_DEPTH_STENCIL_DESC1 Depth(BOOL enable, D3D12_DEPTH_WRITE_MASK write, D3D12_COMPARISON_FUNC func) {
    D3D12_DEPTH_STENCIL_DESC1 d = {};
    d.DepthEnable = enable; d.DepthWriteMask = write; d.DepthFunc = func;
    d.StencilReadMask = d.StencilWriteMask = 0xff;
    d.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    d.BackFace = d.FrontFace;
    return d;
}

int main(int argc, char **argv) {
    if (argc != 4) { printf("usage: d3d12_depth.exe <vs.dxil> <ps.dxil> <psdepth.dxil>\n"); return 2; }
    std::vector<char> vs = load(argv[1]), ps = load(argv[2]), psdepth = load(argv[3]);
    if (vs.empty() || ps.empty() || psdepth.empty()) { printf("can't read the shaders\n"); return 1; }
    const UINT size = 64;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device2), (void **)&device));
    ID3D12CommandQueue *queue; ID3D12CommandAllocator *allocator; ID3D12GraphicsCommandList *list; ID3D12Fence *fence;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue));
    CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
    CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&list));
    CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));

    // Root signature: b0 root CBV, a table with t0.
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *blob = nullptr, *error = nullptr;
    CHECK(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    ID3D12RootSignature *root;
    CHECK(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root));

    const DXGI_FORMAT D32S8 = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    auto write = Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_GREATER_EQUAL);
    write.StencilEnable = TRUE;
    write.FrontFace.StencilPassOp = write.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    ID3D12PipelineState *pso_write = Pipeline(root, vs, ps, write, D32S8);
    ID3D12PipelineState *pso_equal = Pipeline(root, vs, ps, Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_EQUAL), D32S8);
    auto stencil = Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS);
    stencil.StencilEnable = TRUE;
    stencil.FrontFace.StencilFunc = stencil.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    ID3D12PipelineState *pso_stencil = Pipeline(root, vs, ps, stencil, D32S8);
    ID3D12PipelineState *pso_show = Pipeline(root, vs, psdepth, Depth(FALSE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS),
                                             DXGI_FORMAT_UNKNOWN);
    auto both = Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_ALWAYS);
    both.StencilEnable = TRUE;
    both.FrontFace.StencilPassOp = both.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    ID3D12PipelineState *pso_both = Pipeline(root, vs, ps, both, D32S8);
    ID3D12PipelineState *pso_depth_only = Pipeline(root, vs, ps, Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ALL, D3D12_COMPARISON_FUNC_ALWAYS), D32S8);
    ID3D12PipelineState *pso_sampled = Pipeline(root, vs, psdepth, Depth(TRUE, D3D12_DEPTH_WRITE_MASK_ZERO, D3D12_COMPARISON_FUNC_ALWAYS),
                                                D32S8); // reads the depth it's bound to

    // Draws: rect (left, bottom, right, top in NDC), color, z; each at a 256-byte-aligned offset.
    struct Draw { float rect[4], color[4], z, pad[55]; };
    const Draw draws[] = {
        {{-1, -1, 0.2f, 0.5f}, {1, 0, 0, 1}, 0.75f},  // near, red
        {{-0.2f, -0.5f, 1, 1}, {0, 1, 0, 1}, 0.25f},  // far, green
        {{-1, -1, 1, 1}, {1, 1, 0, 1}, 0.25f},        // yellow where depth is 0.25
        {{-1, -1, -0.5f, 1}, {1, 0, 1, 1}, 0},        // magenta at the left where stencil is 1
        {{-1, -1, 1, 1}, {0, 0, 0, 1}, 0},            // the depth
        {{-1, -1, 0, 1}, {0, 0, 1, 1}, 0.9f},         // 5: blue, left half, depth 0.9, stencil 2 (depth read-only)
        {{0, -1, 1, 1}, {0, 1, 1, 1}, 0.6f},          // 6: cyan, right half, depth 0.6 (stencil read-only)
        {{-1, -1, 1, 1}, {0, 0, 0, 1}, 0},            // 7: the depth, sampled while bound read-only
        {{-1, -1, -0.5f, 1}, {1, 1, 1, 1}, 0},        // 8: white at the left where stencil is 2
    };
    ID3D12Resource *cbuf = Resource(D3D12_HEAP_TYPE_UPLOAD, BufferDesc(sizeof draws), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p; D3D12_RANGE none = {0, 0};
    CHECK(cbuf->Map(0, &none, &p)); memcpy(p, draws, sizeof draws); cbuf->Unmap(0, nullptr);

    ID3D12DescriptorHeap *srv_heap, *rtv_heap, *dsv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECK(device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&srv_heap));
    D3D12_DESCRIPTOR_HEAP_DESC rhd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3};
    CHECK(device->CreateDescriptorHeap(&rhd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    D3D12_DESCRIPTOR_HEAP_DESC dhd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 4};
    CHECK(device->CreateDescriptorHeap(&dhd, __uuidof(ID3D12DescriptorHeap), (void **)&dsv_heap));
    UINT rtv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT dsv_step = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_CLEAR_VALUE clear = {DXGI_FORMAT_R8G8B8A8_UNORM, {0, 0, 0, 1}};
    ID3D12Resource *targets[3];
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    for (int i = 0; i < 3; i++) {
        targets[i] = Resource(D3D12_HEAP_TYPE_DEFAULT, TextureDesc(size, size, DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), D3D12_RESOURCE_STATE_RENDER_TARGET, &clear);
        rtvs[i] = rtv_heap->GetCPUDescriptorHandleForHeapStart(); rtvs[i].ptr += i * rtv_step;
        device->CreateRenderTargetView(targets[i], nullptr, rtvs[i]);
    }
    D3D12_CLEAR_VALUE depth_clear = {D32S8}; depth_clear.DepthStencil = {0, 0};
    ID3D12Resource *depth = Resource(D3D12_HEAP_TYPE_DEFAULT, TextureDesc(size, size, DXGI_FORMAT_R32G8X24_TYPELESS,
                                     D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL), D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart(), dsv_read = dsv;
    dsv_read.ptr += dsv_step;
    D3D12_DEPTH_STENCIL_VIEW_DESC dd = {D32S8, D3D12_DSV_DIMENSION_TEXTURE2D};
    device->CreateDepthStencilView(depth, &dd, dsv);
    dd.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    device->CreateDepthStencilView(depth, &dd, dsv_read);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_depth_ro = dsv, dsv_stencil_ro = dsv;
    dsv_depth_ro.ptr += 2 * dsv_step;
    dsv_stencil_ro.ptr += 3 * dsv_step;
    dd.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    device->CreateDepthStencilView(depth, &dd, dsv_depth_ro);
    dd.Flags = D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    device->CreateDepthStencilView(depth, &dd, dsv_stencil_ro);
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, D3D12_SRV_DIMENSION_TEXTURE2D,
                                          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING};
    sv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depth, &sv, srv_heap->GetCPUDescriptorHandleForHeapStart());
    ID3D12Resource *readback = Resource(D3D12_HEAP_TYPE_READBACK, BufferDesc(3 * 256 * size), D3D12_RESOURCE_STATE_COPY_DEST);

    const float black[4] = {0, 0, 0, 1};
    D3D12_VIEWPORT viewport = {0, 0, (float)size, (float)size, 0, 1};
    D3D12_RECT scissor = {0, 0, (LONG)size, (LONG)size};
    auto draw = [&](ID3D12PipelineState *pso, int i) {
        list->SetPipelineState(pso);
        list->SetGraphicsRootConstantBufferView(0, cbuf->GetGPUVirtualAddress() + i * sizeof(Draw));
        list->DrawInstanced(6, 1, 0, 0);
    };
    list->ClearRenderTargetView(rtvs[0], black, 0, nullptr);
    list->ClearRenderTargetView(rtvs[1], black, 0, nullptr);
    list->ClearRenderTargetView(rtvs[2], black, 0, nullptr);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0, 0, 0, nullptr);
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(root);
    list->SetDescriptorHeaps(1, &srv_heap);
    list->SetGraphicsRootDescriptorTable(1, srv_heap->GetGPUDescriptorHandleForHeapStart());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // Pass 1: depth and stencil written.
    list->OMSetRenderTargets(1, &rtvs[0], FALSE, &dsv);
    list->OMSetStencilRef(1);
    draw(pso_write, 0);
    list->OMSetStencilRef(0);
    draw(pso_write, 1);
    // Pass 2: read-only depth and stencil, also bound as a shader resource.
    const D3D12_RESOURCE_STATES read = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Barrier(list, depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, read);
    list->OMSetRenderTargets(1, &rtvs[0], FALSE, &dsv_read);
    draw(pso_equal, 2);
    list->OMSetStencilRef(1);
    draw(pso_stencil, 3);
    // Pass 3: the depth, read through its SRV.
    list->OMSetRenderTargets(1, &rtvs[1], FALSE, nullptr);
    draw(pso_show, 4);
    // Pass 4: read-only planes under a pipeline that writes both, then the depth sampled while bound read-only.
    Barrier(list, depth, read, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    list->OMSetRenderTargets(1, &rtvs[2], FALSE, &dsv_depth_ro);
    list->OMSetStencilRef(2);
    draw(pso_both, 5);
    list->OMSetRenderTargets(1, &rtvs[2], FALSE, &dsv_stencil_ro);
    draw(pso_depth_only, 6);
    Barrier(list, depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, read);
    list->OMSetRenderTargets(1, &rtvs[2], FALSE, &dsv_read);
    draw(pso_sampled, 7);
    list->OMSetStencilRef(2);
    draw(pso_stencil, 8);

    for (int i = 0; i < 3; i++) {
        Barrier(list, targets[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from = {targets[i], D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}; from.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION to = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        to.PlacedFootprint.Offset = i * 256 * size;
        to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, size, size, 1, 256};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    }
    CHECK(list->Close());
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    CHECK(queue->Signal(fence, 1));
    HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(fence->SetEventOnCompletion(1, done));
    if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) { printf("depth fail timeout\n"); return 1; }

    uint8_t *pixels; D3D12_RANGE whole = {0, 3 * 256 * size};
    CHECK(readback->Map(0, &whole, (void **)&pixels));
    uint64_t hash = 0xcbf29ce484222325ull;
    for (UINT y = 0; y < size; y++)
        for (UINT x = 0; x < size * 4; x++) hash = (hash ^ pixels[y * 256 + x]) * 0x100000001b3ull;
    // Pass 2: nothing, near only (magenta), near only (magenta), overlap (red), overlap (red), far only (yellow) twice,
    // nothing. Pass 3: depth 0, 0.75, 0.25, 0.
    const int at[12][3] = {{0, 10, 8}, {0, 10, 40}, {0, 10, 56}, {0, 32, 40}, {0, 32, 24}, {0, 54, 40}, {0, 54, 8},
                           {0, 54, 56}, {1, 10, 8}, {1, 32, 40}, {1, 54, 8}, {1, 54, 56}};
    printf("depth ok %016llx", (unsigned long long)hash);
    for (auto &t : at) { uint32_t px; memcpy(&px, &pixels[t[0] * 256 * size + t[2] * 256 + t[1] * 4], 4); printf(" %08x", px); }
    printf("\n");
    // Pass 4: white where the depth-read-only view let stencil become 2 (x < 16); elsewhere the depth sampled: the
    // left half as passes 1-3 left it (the depth-read-only view kept it), the right half 0.6 (the stencil-read-only
    // view let depth change).
    uint64_t hash2 = 0xcbf29ce484222325ull;
    for (UINT y = 0; y < size; y++)
        for (UINT x = 0; x < size * 4; x++) hash2 = (hash2 ^ pixels[2 * 256 * size + y * 256 + x]) * 0x100000001b3ull;
    const int at2[12][2] = {{8, 8}, {24, 8}, {8, 32}, {24, 32}, {8, 56}, {24, 56}, {40, 8}, {56, 8}, {40, 32}, {56, 32}, {40, 56}, {56, 56}};
    printf("depth2 ok %016llx", (unsigned long long)hash2);
    for (auto &t : at2) { uint32_t px; memcpy(&px, &pixels[2 * 256 * size + t[1] * 256 + t[0] * 4], 4); printf(" %08x", px); }
    printf("\n");
    readback->Unmap(0, &none);
    queue->Release(); // DXMT's capture mode saves a frame-0 pass dump (DXMT_DUMP_FRAME=0) as the queue goes
    return 0;
}
