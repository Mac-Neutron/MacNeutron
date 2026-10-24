// Batch 1 of the D3D12 stubs spec: calls that aborted, hung or failed where D3DMetal succeeds.
//   d3d12_api.exe <vs.dxil> <ps.dxil> [section]   (shaders/depth.hlsl's vsmain and psmain)
// Each line starts with its section; check.sh compares them with D3DMetal's. "caps" is checked on our DXMT only.
#include "d3d12_common.hpp"
#include <atomic>
#include <string>
#include <thread>

static Gpu *gpu;
static std::vector<char> vs, ps;
static const char *only;
static bool Section(const char *name) { return !only || !strcmp(only, name); }

static void Markers() {  // PIX-style events on a command list, then the list runs
    const char text[] = "event";
    gpu->list->BeginEvent(1, text, sizeof text);
    gpu->list->SetMarker(1, text, sizeof text);
    gpu->list->EndEvent();
    gpu->Submit();
    printf("markers ok\n");
}

static void CachedBlob(ID3D12RootSignature *root) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd;
    ID3D12PipelineState *pso = QuadPipeline(*gpu, root, vs, ps, &gd);
    ID3DBlob *blob = nullptr;
    HRESULT hr = pso->GetCachedBlob(&blob);
    HRESULT again = E_FAIL;
    if (blob) {
        gd.CachedPSO = {blob->GetBufferPointer(), blob->GetBufferSize()};
        ID3D12PipelineState *second = nullptr;
        again = gpu->device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&second);
    }
    printf("cachedblob %08lx %zu %08lx\n", (unsigned long)(unsigned)hr, blob ? (size_t)blob->GetBufferSize() : (size_t)0,
           (unsigned long)(unsigned)again);
}

static void NullDsv(ID3D12RootSignature *root) {  // a depth view with no resource: the draw must finish
    ID3D12PipelineState *pso = QuadPipeline(*gpu, root, vs, ps);
    ID3D12DescriptorHeap *rtv_heap, *dsv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC rd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1}, dd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
    CHECK(gpu->device->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    CHECK(gpu->device->CreateDescriptorHeap(&dd, __uuidof(ID3D12DescriptorHeap), (void **)&dsv_heap));
    ID3D12Resource *target = gpu->Texture(Tex2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                          D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart(), dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    gpu->device->CreateRenderTargetView(target, nullptr, rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE2D};
    gpu->device->CreateDepthStencilView(nullptr, &dv, dsv);
    float draw[64] = {-1, -1, 1, 1, 1, 0, 0, 1, 0};
    ID3D12Resource *cb = gpu->Buffer(D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p; CHECK(cb->Map(0, nullptr, &p)); memcpy(p, draw, sizeof draw); cb->Unmap(0, nullptr);
    D3D12_VIEWPORT vp = {0, 0, 16, 16, 0, 1}; D3D12_RECT sc = {0, 0, 16, 16};
    gpu->list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    gpu->list->RSSetViewports(1, &vp); gpu->list->RSSetScissorRects(1, &sc);
    gpu->list->SetGraphicsRootSignature(root); gpu->list->SetPipelineState(pso);
    gpu->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu->list->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());
    gpu->list->DrawInstanced(6, 1, 0, 0);
    gpu->Submit();
    printf("nulldsv ok\n");
}

static void List1() {
    ID3D12Device4 *d4;
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device4), (void **)&d4));
    ID3D12GraphicsCommandList *list = nullptr;
    HRESULT hr = d4->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE,
                                        __uuidof(ID3D12GraphicsCommandList), (void **)&list);
    ID3D12CommandAllocator *allocator;
    CHECK(gpu->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
    HRESULT reset = list ? list->Reset(allocator, nullptr) : E_FAIL;
    HRESULT close = list ? list->Close() : E_FAIL;
    printf("list1 %08lx reset %08lx close %08lx\n", (unsigned long)(unsigned)hr, (unsigned long)(unsigned)reset,
           (unsigned long)(unsigned)close);
}

static void Heap1() {
    ID3D12Device4 *d4;
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device4), (void **)&d4));
    D3D12_HEAP_DESC hd = {65536, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    ID3D12Heap *heap = nullptr;
    printf("heap1 %08lx\n", (unsigned long)(unsigned)d4->CreateHeap1(&hd, nullptr, __uuidof(ID3D12Heap), (void **)&heap));
}

// Map on a buffer placed in a heap the CPU can't see (CUSTOM with no CPU pages, DEFAULT) fails; in an UPLOAD one it
// maps. Each: the creation's or Map's result, and whether Map gave a pointer.
static void MapGuard() {
    auto placed = [](D3D12_HEAP_PROPERTIES props, D3D12_RESOURCE_STATES state, const char *name) {
        D3D12_HEAP_DESC hd = {65536, props, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
        D3D12_RESOURCE_DESC bd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                  D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
        ID3D12Heap *heap = nullptr;
        ID3D12Resource *b = nullptr;
        void *p = nullptr;
        HRESULT hr = gpu->device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap);
        if (SUCCEEDED(hr))
            hr = gpu->device->CreatePlacedResource(heap, 0, &bd, state, nullptr, __uuidof(ID3D12Resource), (void **)&b);
        if (SUCCEEDED(hr))
            hr = b->Map(0, nullptr, &p);
        printf(" %s %08lx %d", name, (unsigned long)(unsigned)hr, p != nullptr);
    };
    D3D12_HEAP_PROPERTIES custom = {D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE, D3D12_MEMORY_POOL_L0};
    printf("map");
    placed(custom, D3D12_RESOURCE_STATE_COMMON, "custom-na");
    placed({D3D12_HEAP_TYPE_DEFAULT}, D3D12_RESOURCE_STATE_COMMON, "default");
    placed({D3D12_HEAP_TYPE_UPLOAD}, D3D12_RESOURCE_STATE_GENERIC_READ, "upload");
    printf("\n");
}

static void Residency() {
    ID3D12Device1 *d1; ID3D12Device3 *d3;
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device1), (void **)&d1));
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device3), (void **)&d3));
    ID3D12Fence *fence;
    CHECK(gpu->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));
    ID3D12Pageable *objects[] = {fence};
    D3D12_RESIDENCY_PRIORITY priority = D3D12_RESIDENCY_PRIORITY_NORMAL;
    HRESULT evict = gpu->device->Evict(1, objects), resident = gpu->device->MakeResident(1, objects);
    HRESULT prio = d1->SetResidencyPriority(1, objects, &priority);
    HRESULT enqueue = d3->EnqueueMakeResident(D3D12_RESIDENCY_FLAG_NONE, 1, objects, fence, 5);
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(5, e);
    WaitForSingleObject(e, 1000);
    printf("residency evict %08lx resident %08lx priority %08lx enqueue %08lx fence %llu\n", (unsigned long)(unsigned)evict,
           (unsigned long)(unsigned)resident, (unsigned long)(unsigned)prio, (unsigned long)(unsigned)enqueue, (unsigned long long)fence->GetCompletedValue());
}

static void MultiFence() {
    ID3D12Device1 *d1;
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device1), (void **)&d1));
    ID3D12Fence *a, *b;
    CHECK(gpu->device->CreateFence(1, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&a));  // done
    CHECK(gpu->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&b));  // not yet
    ID3D12Fence *fences[] = {a, b};
    UINT64 values[] = {1, 1};
    HANDLE any = CreateEventA(nullptr, FALSE, FALSE, nullptr), all = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr_any = d1->SetEventOnMultipleFenceCompletion(fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY, any);
    int any_set = WaitForSingleObject(any, 1000) == WAIT_OBJECT_0;
    HRESULT hr_all = d1->SetEventOnMultipleFenceCompletion(fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, all);
    int all_early = WaitForSingleObject(all, 200) == WAIT_OBJECT_0;
    CHECK(b->Signal(1));
    int all_late = WaitForSingleObject(all, 1000) == WAIT_OBJECT_0;
    HRESULT wait = d1->SetEventOnMultipleFenceCompletion(fences, values, 2, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, nullptr);
    HANDLE none = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HRESULT hr_none = d1->SetEventOnMultipleFenceCompletion(fences, values, 0, D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, none);
    int none_set = WaitForSingleObject(none, 200) == WAIT_OBJECT_0;
    printf("multifence any %08lx %d all %08lx %d %d wait %08lx none %08lx %d\n", (unsigned long)(unsigned)hr_any, any_set,
           (unsigned long)(unsigned)hr_all, all_early, all_late, (unsigned long)wait, (unsigned long)(unsigned)hr_none, none_set);
}

// A fence's shared handle, as 3DMark's Steel Nomad makes one (SHARED | SHARED_CROSS_ADAPTER, GENERIC_ALL, no name) and
// opens it on its display device: an unshared fence has none; the handle opens on this device and on a second one, and
// the three fences are one (a CPU signal, a queue's signal, another device's queue waiting); the handle keeps the fence
// with every fence object let go; once closed it opens nothing, and nor does an event of the program's.
static const char *SharedFence() {
    static char text[160];
#define SHARED_TRY(expr, what) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    snprintf(text, sizeof text, "FAIL %s %08x", what, (unsigned)hr_); return text; } } while (0)
#define SHARED_EXPECT(cond, what) do { if (!(cond)) { snprintf(text, sizeof text, "FAIL %s", what); return text; } } while (0)
    ID3D12Fence *plain, *a, *b, *c, *d = nullptr;
    HANDLE h = nullptr;
    SHARED_TRY(gpu->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&plain), "CreateFence");
    HRESULT hr = gpu->device->CreateSharedHandle(plain, nullptr, GENERIC_ALL, nullptr, &h);
    if (hr != E_INVALIDARG || h) { snprintf(text, sizeof text, "FAIL unshared fence's handle %08x", (unsigned)hr); return text; }
    plain->Release();
    SHARED_TRY(gpu->device->CreateFence(1, (D3D12_FENCE_FLAGS)(D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER),
                                        __uuidof(ID3D12Fence), (void **)&a), "CreateFence(shared)");
    SHARED_TRY(gpu->device->CreateSharedHandle(a, nullptr, GENERIC_ALL, nullptr, &h), "CreateSharedHandle");
    SHARED_EXPECT(h, "a null handle");
    SHARED_TRY(gpu->device->OpenSharedHandle(h, __uuidof(ID3D12Fence), (void **)&b), "OpenSharedHandle");
    SHARED_EXPECT(b->GetCompletedValue() == 1, "opened fence's value");
    SHARED_TRY(a->Signal(5), "Signal");
    SHARED_EXPECT(b->GetCompletedValue() == 5, "a CPU signal on the opened fence");
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    SHARED_TRY(gpu->queue->Signal(b, 7), "queue Signal");
    SHARED_TRY(a->SetEventOnCompletion(7, e), "SetEventOnCompletion");
    SHARED_EXPECT(WaitForSingleObject(e, 1000) == WAIT_OBJECT_0, "a queue's signal of the opened fence");
    ID3D12Device *second;
    SHARED_TRY(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&second), "D3D12CreateDevice");
    SHARED_TRY(second->OpenSharedHandle(h, __uuidof(ID3D12Fence), (void **)&c), "OpenSharedHandle(second device)");
    ID3D12CommandQueue *queue;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    SHARED_TRY(second->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue), "CreateCommandQueue");
    SHARED_TRY(queue->Wait(c, 9), "Wait");
    SHARED_TRY(queue->Signal(c, 10), "Signal(second device)");
    SHARED_EXPECT(a->GetCompletedValue() == 7, "the second device's queue didn't wait");
    SHARED_TRY(b->Signal(9), "Signal(9)");
    SHARED_TRY(a->SetEventOnCompletion(10, e), "SetEventOnCompletion(10)");
    SHARED_EXPECT(WaitForSingleObject(e, 1000) == WAIT_OBJECT_0, "the second device's queue's wait and signal");
    a->Release(); b->Release(); c->Release();
    SHARED_TRY(gpu->device->OpenSharedHandle(h, __uuidof(ID3D12Fence), (void **)&d), "OpenSharedHandle(fences let go)");
    SHARED_EXPECT(d->GetCompletedValue() == 10, "the handle's fence");
    d->Release();
    SHARED_EXPECT(CloseHandle(h), "CloseHandle");
    d = nullptr;
    hr = gpu->device->OpenSharedHandle(h, __uuidof(ID3D12Fence), (void **)&d);
    SHARED_EXPECT(hr == E_INVALIDARG && !d, "a closed handle opens");
    hr = gpu->device->OpenSharedHandle(e, __uuidof(ID3D12Fence), (void **)&d);
    SHARED_EXPECT(hr == E_INVALIDARG && !d, "an event opens");
    CloseHandle(e);
    queue->Release(); second->Release();
    return "ok";
#undef SHARED_TRY
#undef SHARED_EXPECT
}

template <typename T> static void Feature(int id) {
    T data = {};
    printf("feature %d %08lx\n", id, (unsigned long)(unsigned)gpu->device->CheckFeatureSupport((D3D12_FEATURE)id, &data, sizeof data));
}
struct Raw40 { UINT v[10]; };  // OPTIONS19 (48)
struct Raw16 { UINT v[4]; };   // OPTIONS21 (53)

static void Features() {
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS>(0); Feature<D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT>(6);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS1>(8); Feature<D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT>(10);
    Feature<D3D12_FEATURE_DATA_ARCHITECTURE1>(16); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS2>(18);
    Feature<D3D12_FEATURE_DATA_SHADER_CACHE>(19); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS3>(21);
    Feature<D3D12_FEATURE_DATA_EXISTING_HEAPS>(22); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS4>(23);
    Feature<D3D12_FEATURE_DATA_CROSS_NODE>(25); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS5>(27);
    Feature<D3D12_FEATURE_DATA_DISPLAYABLE>(28); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS6>(30);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS7>(32); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS8>(36);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS9>(37); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS10>(39);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS11>(40); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS12>(41);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS13>(42); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS14>(43);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS15>(44); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS16>(45);
    Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS17>(46); Feature<D3D12_FEATURE_DATA_D3D12_OPTIONS18>(47);
    Feature<Raw40>(48); Feature<Raw16>(53);
    // Our DXMT only (check.sh): no claim of what the fork lacks.
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {}; D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {};
    gpu->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5);
    gpu->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof o6);
    gpu->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7);
    printf("caps rt=%d mesh=%d vrs=%d sfb=%d\n", (int)o5.RaytracingTier, (int)o7.MeshShaderTier,
           (int)o6.VariableShadingRateTier, (int)o7.SamplerFeedbackTier);
}

static void Library(ID3D12RootSignature *root) {
    ID3D12Device1 *d1;
    CHECK(gpu->device->QueryInterface(__uuidof(ID3D12Device1), (void **)&d1));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd;
    ID3D12PipelineState *pso = QuadPipeline(*gpu, root, vs, ps, &gd), *got = nullptr;
    auto step = [](const char *name, HRESULT hr) { printf("library %s %08lx\n", name, (unsigned long)(unsigned)hr); };
    ID3D12PipelineLibrary *lib = nullptr;
    step("create", d1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), (void **)&lib));
    if (!lib)
        return;
    step("load-missing", lib->LoadGraphicsPipeline(L"a", &gd, __uuidof(ID3D12PipelineState), (void **)&got));
    step("store", lib->StorePipeline(L"a", pso));
    step("store-again", lib->StorePipeline(L"a", pso));
    HRESULT hr = lib->LoadGraphicsPipeline(L"a", &gd, __uuidof(ID3D12PipelineState), (void **)&got);
    printf("library load-stored %08lx same %d\n", (unsigned long)(unsigned)hr, got == pso);
    if (got) got->Release();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC other = gd;
    other.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    step("load-other", lib->LoadGraphicsPipeline(L"a", &other, __uuidof(ID3D12PipelineState), (void **)&got));
    pso->Release();  // the library still holds the stored pipeline
    got = nullptr;
    hr = lib->LoadGraphicsPipeline(L"a", &gd, __uuidof(ID3D12PipelineState), (void **)&got);
    printf("library load-after-release %08lx valid %d\n", (unsigned long)(unsigned)hr, got != nullptr);
    SIZE_T size = lib->GetSerializedSize();
    std::vector<char> blob(size);
    hr = lib->Serialize(blob.data(), size);
    printf("library serialize %08lx nonzero %d\n", (unsigned long)(unsigned)hr, size > 0);
    ID3D12PipelineLibrary *lib2 = nullptr;
    step("from-blob", d1->CreatePipelineLibrary(blob.data(), size, __uuidof(ID3D12PipelineLibrary), (void **)&lib2));
    if (lib2)
        step("load-from-blob", lib2->LoadGraphicsPipeline(L"a", &gd, __uuidof(ID3D12PipelineState), (void **)&got));
    char junk[64] = {1, 2, 3};
    step("junk", d1->CreatePipelineLibrary(junk, sizeof junk, __uuidof(ID3D12PipelineLibrary), (void **)&lib2));
    // Serialize while another thread stores pipelines (libraries are free-threaded): it never writes past the size
    // it was given (guard bytes after the buffer stay untouched).
    ID3D12PipelineState *another = QuadPipeline(*gpu, root, vs, ps);
    std::atomic<bool> stop{false};
    std::thread storer([&] {
        for (int k = 0; !stop; k++) lib->StorePipeline((L"s" + std::to_wstring(k)).c_str(), another);
    });
    int overflow = 0;
    for (int i = 0; i < 3000 && !overflow; i++) {
        SIZE_T n = lib->GetSerializedSize();
        std::vector<unsigned char> buffer(n + 64, 0x5a);
        lib->Serialize(buffer.data(), n);
        for (SIZE_T j = n; j < n + 64; j++) overflow |= buffer[j] != 0x5a;
    }
    stop = true;
    storer.join();
    printf("library serialize-race overflow %d\n", overflow);
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: d3d12_api.exe <vs.dxil> <ps.dxil> [section]\n"); return 2; }
    vs = Load(argv[1]); ps = Load(argv[2]); only = argc > 3 ? argv[3] : nullptr;
    if (vs.empty() || ps.empty()) { printf("can't read the shaders\n"); return 1; }
    gpu = new Gpu();
    ID3D12RootSignature *root = CbvRootSignature(*gpu);
    if (Section("features")) Features();
    if (Section("list1")) List1();
    if (Section("heap1")) Heap1();
    if (Section("residency")) Residency();
    if (Section("multifence")) MultiFence();
    if (Section("sharedfence")) printf("sharedfence %s\n", SharedFence());
    if (Section("library")) Library(root);
    if (Section("markers")) Markers();
    if (Section("cachedblob")) CachedBlob(root);
    if (Section("nulldsv")) NullDsv(root);
    if (Section("map")) MapGuard();
    return 0;
}
