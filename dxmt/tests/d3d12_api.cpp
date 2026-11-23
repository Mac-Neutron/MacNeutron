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

static const GUID kIidDevice11 = {0x5405c344, 0xd457, 0x444e, {0xb4, 0xdd, 0x23, 0x66, 0xe4, 0x5a, 0xee, 0x39}};
static const GUID kIidDevice12 = {0x5af5c532, 0x4c91, 0x4cd0, {0xb5, 0x41, 0x15, 0xa4, 0x05, 0x39, 0x5f, 0xc5}};
static const GUID kIidDevice13 = {0x14eecffc, 0x4df8, 0x40f7, {0xa1, 0x18, 0x5c, 0x81, 0x6f, 0x45, 0x69, 0x5e}};
static const GUID kIidDevice14 = {0x5f6e592d, 0xd895, 0x44c2, {0x8e, 0x4a, 0x88, 0xad, 0x49, 0x26, 0xd3, 0x23}};
static const GUID kIidList8 = {0xee936ef9, 0x599d, 0x4d28, {0x93, 0x8e, 0x23, 0xc4, 0xad, 0x05, 0xce, 0x51}};
static const GUID kIidList9 = {0x34ed2808, 0xffe6, 0x4c2b, {0xb1, 0x1a, 0xca, 0xbd, 0x2b, 0x0c, 0x59, 0xe1}};
static const GUID kIidList10 = {0x7013c015, 0xd161, 0x4b63, {0xa0, 0x8c, 0x23, 0x85, 0x52, 0xdd, 0x8a, 0xcc}};

struct AgSamplerDesc2 {
    D3D12_FILTER Filter;
    D3D12_TEXTURE_ADDRESS_MODE AddressU;
    D3D12_TEXTURE_ADDRESS_MODE AddressV;
    D3D12_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D12_COMPARISON_FUNC ComparisonFunc;
    union {
        FLOAT FloatBorderColor[4];
        UINT UintBorderColor[4];
    };
    FLOAT MinLOD;
    FLOAT MaxLOD;
    UINT Flags;
};

struct AgDevice11 : public ID3D12Device10 {
    virtual void STDMETHODCALLTYPE CreateSampler2(const AgSamplerDesc2 *pDesc,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) = 0;
};

struct AgDevice12 : public AgDevice11 {
    virtual D3D12_RESOURCE_ALLOCATION_INFO *STDMETHODCALLTYPE GetResourceAllocationInfo3(
        D3D12_RESOURCE_ALLOCATION_INFO *ret, UINT visibleMask, UINT numResourceDescs,
        const D3D12_RESOURCE_DESC1 *pResourceDescs, const UINT *pNumCastableFormats,
        const DXGI_FORMAT *const *ppCastableFormats,
        D3D12_RESOURCE_ALLOCATION_INFO1 *pResourceAllocationInfo1) = 0;
};

struct AgDevice13 : public AgDevice12 {
    virtual HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress1(const void *pAddress, SIZE_T size,
                                                                   REFIID riid, void **ppvHeap) = 0;
};

struct AgDevice14 : public AgDevice13 {
    virtual HRESULT STDMETHODCALLTYPE CreateRootSignatureFromSubobjectInLibrary(
        UINT nodeMask, const void *pLibraryBlob, SIZE_T blobLengthInBytes, LPCWSTR subobjectName,
        REFIID riid, void **ppvRootSignature) = 0;
};

struct AgList8 : public ID3D12GraphicsCommandList7 {
    virtual void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT FrontStencilRef,
                                                               UINT BackStencilRef) = 0;
};

struct AgList9 : public AgList8 {
    virtual void STDMETHODCALLTYPE RSSetDepthBias(FLOAT DepthBias, FLOAT DepthBiasClamp,
                                                  FLOAT SlopeScaledDepthBias) = 0;
    virtual void STDMETHODCALLTYPE IASetIndexBufferStripCutValue(
        D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue) = 0;
};

struct AgList10 : public AgList9 {
    virtual void STDMETHODCALLTYPE SetProgram(const void *pDesc) = 0;
    virtual void STDMETHODCALLTYPE DispatchGraph(const void *pDesc) = 0;
};

struct AgStaticSamplerDesc1 {
    D3D12_FILTER Filter;
    D3D12_TEXTURE_ADDRESS_MODE AddressU;
    D3D12_TEXTURE_ADDRESS_MODE AddressV;
    D3D12_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D12_COMPARISON_FUNC ComparisonFunc;
    D3D12_STATIC_BORDER_COLOR BorderColor;
    FLOAT MinLOD;
    FLOAT MaxLOD;
    UINT ShaderRegister;
    UINT RegisterSpace;
    D3D12_SHADER_VISIBILITY ShaderVisibility;
    UINT Flags;
};

struct AgRootSignatureDesc2 {
    UINT NumParameters;
    const D3D12_ROOT_PARAMETER1 *pParameters;
    UINT NumStaticSamplers;
    const AgStaticSamplerDesc1 *pStaticSamplers;
    D3D12_ROOT_SIGNATURE_FLAGS Flags;
};

struct AgVersionedRootSignatureDesc {
    UINT Version;
    union {
        D3D12_ROOT_SIGNATURE_DESC Desc_1_0;
        D3D12_ROOT_SIGNATURE_DESC1 Desc_1_1;
        AgRootSignatureDesc2 Desc_1_2;
    };
};

static void Agility(ID3D12RootSignature *root) {
    // 1. Device11..14 and GraphicsCommandList8..10 QueryInterface.
    AgDevice11 *d11 = nullptr;
    AgDevice12 *d12 = nullptr;
    AgDevice13 *d13 = nullptr;
    AgDevice14 *d14 = nullptr;
    CHECK(gpu->device->QueryInterface(kIidDevice11, (void **)&d11));
    CHECK(gpu->device->QueryInterface(kIidDevice12, (void **)&d12));
    CHECK(gpu->device->QueryInterface(kIidDevice13, (void **)&d13));
    CHECK(gpu->device->QueryInterface(kIidDevice14, (void **)&d14));

    AgList8 *l8 = nullptr;
    AgList9 *l9 = nullptr;
    AgList10 *l10 = nullptr;
    CHECK(gpu->list->QueryInterface(kIidList8, (void **)&l8));
    CHECK(gpu->list->QueryInterface(kIidList9, (void **)&l9));
    CHECK(gpu->list->QueryInterface(kIidList10, (void **)&l10));

    // 2. CheckFeatureSupport for ROOT_SIGNATURE 1.2, OPTIONS12, OPTIONS16.
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rsFeat = {(D3D_ROOT_SIGNATURE_VERSION)3};
    CHECK(gpu->device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rsFeat, sizeof rsFeat));
    if ((UINT)rsFeat.HighestVersion != 3) { printf("agility FAIL rsFeat=%u\n", (UINT)rsFeat.HighestVersion); exit(1); }
    rsFeat.HighestVersion = (D3D_ROOT_SIGNATURE_VERSION)5;
    CHECK(gpu->device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rsFeat, sizeof rsFeat));
    if ((UINT)rsFeat.HighestVersion != 3) { printf("agility FAIL rsFeatClamp=%u\n", (UINT)rsFeat.HighestVersion); exit(1); }

    D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12 = {};
    CHECK(gpu->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof o12));
    if (!o12.RelaxedFormatCastingSupported) { printf("agility FAIL RelaxedFormatCastingSupported=0\n"); exit(1); }

    D3D12_FEATURE_DATA_D3D12_OPTIONS16 o16 = {};
    CHECK(gpu->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &o16, sizeof o16));
    if (!o16.DynamicDepthBiasSupported || !o16.GPUUploadHeapSupported) {
        printf("agility FAIL o16 bias=%d upload=%d\n", (int)o16.DynamicDepthBiasSupported, (int)o16.GPUUploadHeapSupported);
        exit(1);
    }

    // 3. CreateSampler2, GetResourceAllocationInfo3, and Device13..14 unimplemented slots.
    D3D12_DESCRIPTOR_HEAP_DESC shd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1};
    ID3D12DescriptorHeap *sampHeap = nullptr;
    CHECK(gpu->device->CreateDescriptorHeap(&shd, __uuidof(ID3D12DescriptorHeap), (void **)&sampHeap));
    AgSamplerDesc2 sd2 = {};
    sd2.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sd2.AddressU = sd2.AddressV = sd2.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sd2.MaxAnisotropy = 1;
    sd2.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd2.UintBorderColor[0] = 1;
    sd2.MaxLOD = 1.0f;
    sd2.Flags = 0x1;  // D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR
    d11->CreateSampler2(&sd2, sampHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RESOURCE_DESC1 rd1 = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}};
    DXGI_FORMAT castFormats[2] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_UINT};
    const DXGI_FORMAT *castPtr = castFormats;
    UINT castCount = 2;
    D3D12_RESOURCE_ALLOCATION_INFO rai = {};
    D3D12_RESOURCE_ALLOCATION_INFO1 rai1 = {};
    d12->GetResourceAllocationInfo3(&rai, 0, 1, &rd1, &castCount, &castPtr, &rai1);
    if (rai.SizeInBytes == 0 || rai.SizeInBytes == UINT64_MAX || rai.Alignment == 0) {
        printf("agility FAIL allocInfo3 size=%llu align=%llu\n", (unsigned long long)rai.SizeInBytes,
               (unsigned long long)rai.Alignment);
        exit(1);
    }
    void *stale = (void *)1;
    if (d13->OpenExistingHeapFromAddress1(nullptr, 0, __uuidof(ID3D12Heap), &stale) != E_NOTIMPL || stale != nullptr) {
        printf("agility FAIL OpenExistingHeapFromAddress1\n"); exit(1);
    }
    stale = (void *)1;
    if (d14->CreateRootSignatureFromSubobjectInLibrary(0, nullptr, 0, L"rs", __uuidof(ID3D12RootSignature), &stale) != E_NOTIMPL ||
        stale != nullptr) {
        printf("agility FAIL CreateRootSignatureFromSubobjectInLibrary\n"); exit(1);
    }

    // 4. Root Signature 1.2 serialize + deserialize + CreateRootSignature.
    D3D12_ROOT_PARAMETER1 rp1 = {};
    rp1.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    AgStaticSamplerDesc1 ss1[3] = {};
    for (UINT i = 0; i < 3; i++) {
        ss1[i].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss1[i].AddressU = ss1[i].AddressV = ss1[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss1[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ss1[i].MaxLOD = 1.0f;
        ss1[i].ShaderRegister = i;
    }
    ss1[1].BorderColor = (D3D12_STATIC_BORDER_COLOR)3;  // D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT
    ss1[2].BorderColor = (D3D12_STATIC_BORDER_COLOR)4;  // D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE_UINT
    AgVersionedRootSignatureDesc vrs = {};
    vrs.Version = 3;
    vrs.Desc_1_2 = {1, &rp1, 3, ss1, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob *rsBlob = nullptr, *rsErr = nullptr;
    CHECK(D3D12SerializeVersionedRootSignature((const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *)&vrs, &rsBlob, &rsErr));
    ID3D12VersionedRootSignatureDeserializer *deser = nullptr;
    CHECK(D3D12CreateVersionedRootSignatureDeserializer(rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                                        __uuidof(ID3D12VersionedRootSignatureDeserializer), (void **)&deser));
    const D3D12_VERSIONED_ROOT_SIGNATURE_DESC *outDesc = nullptr;
    CHECK(deser->GetRootSignatureDescAtVersion((D3D_ROOT_SIGNATURE_VERSION)3, &outDesc));
    if ((UINT)outDesc->Version != 3) { printf("agility FAIL deser version=%u\n", (UINT)outDesc->Version); exit(1); }
    ID3D12RootSignature *rs12 = nullptr;
    CHECK(gpu->device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                           __uuidof(ID3D12RootSignature), (void **)&rs12));

    // 5. D3D12_HEAP_TYPE_GPU_UPLOAD committed/placed buffer and WriteToSubresource texture.
    const D3D12_HEAP_TYPE gpuUploadHeap = (D3D12_HEAP_TYPE)5;
    ID3D12Resource *gpuUpBuf = gpu->Buffer(gpuUploadHeap, 256, D3D12_RESOURCE_STATE_COMMON);
    uint32_t *upPtr = nullptr;
    CHECK(gpuUpBuf->Map(0, nullptr, (void **)&upPtr));
    upPtr[0] = 0x11223344u; upPtr[1] = 0x55667788u;
    gpuUpBuf->Unmap(0, nullptr);

    D3D12_HEAP_DESC gpuUpHd = {65536, {gpuUploadHeap}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    ID3D12Heap *gpuHeap = nullptr;
    CHECK(gpu->device->CreateHeap(&gpuUpHd, __uuidof(ID3D12Heap), (void **)&gpuHeap));
    D3D12_RESOURCE_DESC placedBd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                    D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ID3D12Resource *placedUpBuf = nullptr;
    CHECK(gpu->device->CreatePlacedResource(gpuHeap, 0, &placedBd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            __uuidof(ID3D12Resource), (void **)&placedUpBuf));
    CHECK(placedUpBuf->Map(0, nullptr, (void **)&upPtr));
    upPtr[0] = 0xcafebabeu; upPtr[1] = 0xdeadbeefu;
    placedUpBuf->Unmap(0, nullptr);

    D3D12_RESOURCE_DESC upTexDesc = Tex2D(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    D3D12_HEAP_PROPERTIES upTexProps = {gpuUploadHeap};
    ID3D12Resource *gpuUpTex = nullptr;
    CHECK(gpu->device->CreateCommittedResource(&upTexProps, D3D12_HEAP_FLAG_NONE, &upTexDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               __uuidof(ID3D12Resource), (void **)&gpuUpTex));
    uint32_t texSrc[16];
    for (int i = 0; i < 16; i++) texSrc[i] = 0xff000080u + (uint32_t)i;
    CHECK(gpuUpTex->WriteToSubresource(0, nullptr, texSrc, 16, 64));

    ID3D12Resource *rbBuf = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 512, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *rbTex = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    gpu->list->CopyBufferRegion(rbBuf, 0, gpuUpBuf, 0, 16);
    gpu->list->CopyBufferRegion(rbBuf, 256, placedUpBuf, 0, 16);
    D3D12_TEXTURE_COPY_LOCATION tdDst = {rbTex, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    tdDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    D3D12_TEXTURE_COPY_LOCATION tdSrc = {gpuUpTex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    gpu->list->CopyTextureRegion(&tdDst, 0, 0, 0, &tdSrc, nullptr);
    gpu->Submit();

    uint8_t *rbPtr = nullptr;
    CHECK(rbBuf->Map(0, nullptr, (void **)&rbPtr));
    uint32_t *rbWords = (uint32_t *)rbPtr;
    if (rbWords[0] != 0x11223344u || rbWords[1] != 0x55667788u ||
        rbWords[64] != 0xcafebabeu || rbWords[65] != 0xdeadbeefu) {
        printf("agility FAIL gpu_upload buffer readback\n"); exit(1);
    }
    rbBuf->Unmap(0, nullptr);
    CHECK(rbTex->Map(0, nullptr, (void **)&rbPtr));
    for (int y = 0; y < 4; y++) {
        if (memcmp(rbPtr + y * 256, &texSrc[y * 4], 16) != 0) {
            printf("agility FAIL gpu_upload WriteToSubresource row=%d\n", y); exit(1);
        }
    }
    rbTex->Unmap(0, nullptr);

    // 6. CreateCommittedResource3 & CreatePlacedResource2 with cross-layout castable formats.
    D3D12_RESOURCE_DESC1 castRd = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 4, 4, 1, 1,
                                   DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
                                   D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET};
    D3D12_RESOURCE_DESC1 castCommittedRd = castRd;
    castCommittedRd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES defProps = {D3D12_HEAP_TYPE_DEFAULT};
    ID3D12Resource *castCommitted = nullptr, *castPlaced = nullptr;
    CHECK(d14->CreateCommittedResource3(&defProps, D3D12_HEAP_FLAG_NONE, &castCommittedRd,
                                         D3D12_BARRIER_LAYOUT_RENDER_TARGET, nullptr, nullptr, 2,
                                         castFormats, __uuidof(ID3D12Resource), (void **)&castCommitted));
    D3D12_DESCRIPTOR_HEAP_DESC castViewHd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2};
    ID3D12DescriptorHeap *castViewHeap = nullptr;
    CHECK(gpu->device->CreateDescriptorHeap(&castViewHd, __uuidof(ID3D12DescriptorHeap), (void **)&castViewHeap));
    UINT cbvSrvUavInc = gpu->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE castSrvHandle = castViewHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE castUavHandle = castSrvHandle;
    castUavHandle.ptr += cbvSrvUavInc;
    D3D12_SHADER_RESOURCE_VIEW_DESC castSrvDesc = {};
    castSrvDesc.Format = DXGI_FORMAT_R32_UINT;
    castSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    castSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    castSrvDesc.Texture2D.MipLevels = 1;
    gpu->device->CreateShaderResourceView(castCommitted, &castSrvDesc, castSrvHandle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC castUavDesc = {};
    castUavDesc.Format = DXGI_FORMAT_R32_UINT;
    castUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    gpu->device->CreateUnorderedAccessView(castCommitted, nullptr, &castUavDesc, castUavHandle);
    D3D12_RESOURCE_ALLOCATION_INFO castAlloc = {};
    D3D12_RESOURCE_ALLOCATION_INFO1 castAlloc1 = {};
    d12->GetResourceAllocationInfo3(&castAlloc, 0, 1, &castRd, &castCount, &castPtr, &castAlloc1);
    D3D12_HEAP_DESC rtHeapDesc = {castAlloc.SizeInBytes > 65536 ? castAlloc.SizeInBytes : 65536,
                                  {D3D12_HEAP_TYPE_DEFAULT}, castAlloc.Alignment,
                                  D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES};
    ID3D12Heap *rtHeap = nullptr;
    CHECK(gpu->device->CreateHeap(&rtHeapDesc, __uuidof(ID3D12Heap), (void **)&rtHeap));
    CHECK(d14->CreatePlacedResource2(rtHeap, 0, &castRd, D3D12_BARRIER_LAYOUT_RENDER_TARGET,
                                      nullptr, 2, castFormats, __uuidof(ID3D12Resource), (void **)&castPlaced));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4};
    ID3D12DescriptorHeap *rtvHeap = nullptr;
    CHECK(gpu->device->CreateDescriptorHeap(&rtvHd, __uuidof(ID3D12DescriptorHeap), (void **)&rtvHeap));
    UINT rtvInc = gpu->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtvAt = [&](UINT i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)i * rtvInc;
        return h;
    };
    gpu->device->CreateRenderTargetView(castCommitted, nullptr, rtvAt(0));
    gpu->device->CreateRenderTargetView(castPlaced, nullptr, rtvAt(1));
    const FLOAT green[4] = {0, 1, 0, 1}, blue[4] = {0, 0, 1, 1}, red[4] = {1, 0, 0, 1}, black[4] = {0, 0, 0, 1};
    gpu->list->ClearRenderTargetView(rtvAt(0), green, 0, nullptr);
    gpu->list->ClearRenderTargetView(rtvAt(1), blue, 0, nullptr);
    gpu->Barrier(castCommitted, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu->Barrier(castPlaced, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12Resource *rbCast0 = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *rbCast1 = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION cDst0 = {rbCast0, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    cDst0.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    D3D12_TEXTURE_COPY_LOCATION cDst1 = {rbCast1, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    cDst1.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    D3D12_TEXTURE_COPY_LOCATION cSrc0 = {castCommitted, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION cSrc1 = {castPlaced, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    gpu->list->CopyTextureRegion(&cDst0, 0, 0, 0, &cSrc0, nullptr);
    gpu->list->CopyTextureRegion(&cDst1, 0, 0, 0, &cSrc1, nullptr);
    gpu->Submit();
    CHECK(rbCast0->Map(0, nullptr, (void **)&rbPtr));
    if (*(uint32_t *)rbPtr != 0xff00ff00u) { printf("agility FAIL castCommitted=%08x\n", *(uint32_t *)rbPtr); exit(1); }
    rbCast0->Unmap(0, nullptr);
    CHECK(rbCast1->Map(0, nullptr, (void **)&rbPtr));
    if (*(uint32_t *)rbPtr != 0xffff0000u) { printf("agility FAIL castPlaced=%08x\n", *(uint32_t *)rbPtr); exit(1); }
    rbCast1->Unmap(0, nullptr);

    // 7. ID3D12GraphicsCommandList8..10 methods & RSSetDepthBias rendering + SetPipelineState reset.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd;
    QuadPipeline(*gpu, root, vs, ps, &gd)->Release();
    gd.DepthStencilState.DepthEnable = TRUE;
    gd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    gd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    gd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    ID3D12PipelineState *depthPso = nullptr;
    CHECK(gpu->device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&depthPso));

    ID3D12Resource *rtBias = gpu->Texture(Tex2D(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                          D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource *rtReset = gpu->Texture(Tex2D(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET),
                                           D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource *dsvTex = gpu->Texture(Tex2D(4, 4, DXGI_FORMAT_D32_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
                                          D3D12_RESOURCE_STATE_DEPTH_WRITE);
    gpu->device->CreateRenderTargetView(rtBias, nullptr, rtvAt(2));
    gpu->device->CreateRenderTargetView(rtReset, nullptr, rtvAt(3));
    D3D12_DESCRIPTOR_HEAP_DESC dsvHd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1};
    ID3D12DescriptorHeap *dsvHeap = nullptr;
    CHECK(gpu->device->CreateDescriptorHeap(&dsvHd, __uuidof(ID3D12DescriptorHeap), (void **)&dsvHeap));
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    gpu->device->CreateDepthStencilView(dsvTex, nullptr, dsv);

    struct { float rect[4], color[4], z; } draws[3] = {
        {{-1, -1, 1, 1}, {1, 0, 0, 1}, 0.5f},
        {{-1, -1, 1, 1}, {0, 1, 0, 1}, 0.5f},
        {{-1, -1, 1, 1}, {0, 0, 1, 1}, 0.5f},
    };
    ID3D12Resource *cb = gpu->Buffer(D3D12_HEAP_TYPE_UPLOAD, 3 * 256, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t *cbPtr = nullptr;
    CHECK(cb->Map(0, nullptr, (void **)&cbPtr));
    for (int i = 0; i < 3; i++) memcpy(cbPtr + i * 256, &draws[i], sizeof draws[i]);
    cb->Unmap(0, nullptr);
    D3D12_GPU_VIRTUAL_ADDRESS cbVa = cb->GetGPUVirtualAddress();

    D3D12_VIEWPORT vp = {0, 0, 4, 4, 0, 1};
    D3D12_RECT sc = {0, 0, 4, 4};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv2 = rtvAt(2), rtv3 = rtvAt(3);

    l10->OMSetFrontAndBackStencilRef(1, 1);
    l10->IASetIndexBufferStripCutValue(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF);
    l10->SetProgram(nullptr);
    l10->DispatchGraph(nullptr);

    // Pass 1: negative dynamic depth bias allows second z=0.5 quad (blue) to beat initial z=0.5 (red).
    gpu->list->ClearRenderTargetView(rtv2, black, 0, nullptr);
    gpu->list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    gpu->list->OMSetRenderTargets(1, &rtv2, FALSE, &dsv);
    gpu->list->RSSetViewports(1, &vp);
    gpu->list->RSSetScissorRects(1, &sc);
    gpu->list->SetGraphicsRootSignature(root);
    gpu->list->SetPipelineState(depthPso);
    gpu->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu->list->SetGraphicsRootConstantBufferView(0, cbVa);
    gpu->list->DrawInstanced(6, 1, 0, 0);
    l9->RSSetDepthBias(10000.0f, 0.0f, 0.0f);
    gpu->list->SetGraphicsRootConstantBufferView(0, cbVa + 256);
    gpu->list->DrawInstanced(6, 1, 0, 0);
    l9->RSSetDepthBias(-10000.0f, 0.0f, 0.0f);
    gpu->list->SetGraphicsRootConstantBufferView(0, cbVa + 512);
    gpu->list->DrawInstanced(6, 1, 0, 0);

    // Pass 2: SetPipelineState resets prior RSSetDepthBias back to PSO's 0, so z=0.5 (green) fails LESS.
    gpu->list->ClearRenderTargetView(rtv3, black, 0, nullptr);
    gpu->list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    gpu->list->OMSetRenderTargets(1, &rtv3, FALSE, &dsv);
    gpu->list->SetPipelineState(depthPso);
    gpu->list->SetGraphicsRootConstantBufferView(0, cbVa);
    gpu->list->DrawInstanced(6, 1, 0, 0);
    l9->RSSetDepthBias(-10000.0f, 0.0f, 0.0f);
    gpu->list->SetPipelineState(depthPso);
    gpu->list->SetGraphicsRootConstantBufferView(0, cbVa + 256);
    gpu->list->DrawInstanced(6, 1, 0, 0);

    gpu->Barrier(rtBias, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu->Barrier(rtReset, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12Resource *rbBias = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *rbReset = gpu->Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION bDst = {rbBias, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    bDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    D3D12_TEXTURE_COPY_LOCATION rDst = {rbReset, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    rDst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, 256};
    D3D12_TEXTURE_COPY_LOCATION bSrc = {rtBias, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    D3D12_TEXTURE_COPY_LOCATION rSrc = {rtReset, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    gpu->list->CopyTextureRegion(&bDst, 0, 0, 0, &bSrc, nullptr);
    gpu->list->CopyTextureRegion(&rDst, 0, 0, 0, &rSrc, nullptr);
    gpu->Submit();

    CHECK(rbBias->Map(0, nullptr, (void **)&rbPtr));
    if (*(uint32_t *)rbPtr != 0xffff0000u) { printf("agility FAIL depthBias=%08x\n", *(uint32_t *)rbPtr); exit(1); }
    rbBias->Unmap(0, nullptr);
    CHECK(rbReset->Map(0, nullptr, (void **)&rbPtr));
    if (*(uint32_t *)rbPtr != 0xff0000ffu) { printf("agility FAIL depthBiasReset=%08x\n", *(uint32_t *)rbPtr); exit(1); }
    rbReset->Unmap(0, nullptr);

    printf("agility ok\n");
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: d3d12_api.exe <vs.dxil> <ps.dxil> [section]\n"); return 2; }
    vs = Load(argv[1]); ps = Load(argv[2]); only = argc > 3 ? argv[3] : nullptr;
    if (vs.empty() || ps.empty()) { printf("can't read the shaders\n"); return 1; }
    gpu = new Gpu();
    ID3D12RootSignature *root = CbvRootSignature(*gpu);
    if (only && !strcmp(only, "agility")) {
        Agility(root);
        return 0;
    }
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
