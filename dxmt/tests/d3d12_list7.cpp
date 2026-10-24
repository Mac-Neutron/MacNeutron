// ID3D12GraphicsCommandList3 to 7 (DXMT command list 7 spec §7):
//   d3d12_list7.exe
// Prints one line a row, "<row> ok" or "<row> FAIL <what>"; a failed row doesn't end the program.
//   list4  CreateCommandList answers ID3D12GraphicsCommandList4, 5, 6 and 7; the List7 one answers List3 to 6, and the
//          methods behind caps DXMT reports off (protected sessions, VRS, mesh shaders, ray tracing) can be called
//   resolveregion  ResolveSubresourceRegion of a 2x2 rectangle of a red 4x MSAA target to (2,2) of a blue one: those
//          four texels red, the rest still blue; a MAX resolve (which DXMT leaves out) changes nothing
#include "d3d12_common.hpp"

static char fail_text[256];

#define TRY(expr, what) do { HRESULT hr_ = (expr); if (FAILED(hr_)) { \
    snprintf(fail_text, sizeof fail_text, "FAIL %s %08x", what, (unsigned)hr_); return fail_text; } } while (0)

static const uint32_t kRed = 0xff0000ff, kGreen = 0xff00ff00, kBlue = 0xffff0000, kBlack = 0xff000000; // RGBA8 texels
static const FLOAT kRedF[4] = {1, 0, 0, 1}, kGreenF[4] = {0, 1, 0, 1}, kBlueF[4] = {0, 0, 1, 1}, kBlackF[4] = {0, 0, 0, 1};

// A 4x4 R8G8B8A8_UNORM render target, 4x MSAA when samples is 4, in RENDER_TARGET.
static ID3D12Resource *Target(Gpu &gpu, UINT samples = 1, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                              D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET) {
    D3D12_RESOURCE_DESC d = Tex2D(4, 4, format, 1, flags);
    d.SampleDesc.Count = samples;
    return gpu.Texture(d, state);
}

// Descriptors in a CPU heap of their own, one a call (the heaps live as long as the program).
static D3D12_CPU_DESCRIPTOR_HANDLE View(Gpu &gpu, ID3D12Resource *r, D3D12_DESCRIPTOR_HEAP_TYPE type) {
    D3D12_DESCRIPTOR_HEAP_DESC hd = {type, 1};
    ID3D12DescriptorHeap *heap;
    CHECK(gpu.device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&heap));
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) gpu.device->CreateRenderTargetView(r, nullptr, h);
    else gpu.device->CreateDepthStencilView(r, nullptr, h);
    return h;
}

// A 4x4 texture's 16 four-byte texels, row by row: copied out from `state` through a readback buffer (the list is
// submitted and reopened).
static std::vector<uint32_t> Read(Gpu &gpu, ID3D12Resource *r, D3D12_RESOURCE_STATES state) {
    ID3D12Resource *buffer = gpu.Buffer(D3D12_HEAP_TYPE_READBACK, 4 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RESOURCE_DESC d = r->GetDesc();
    D3D12_TEXTURE_COPY_LOCATION dst = {buffer, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}, src = {r};
    dst.PlacedFootprint.Footprint = {d.Format, 4, 4, 1, 256};
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) gpu.Barrier(r, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    gpu.Submit();
    uint8_t *p;
    D3D12_RANGE all = {0, 4 * 256};
    CHECK(buffer->Map(0, &all, (void **)&p));
    std::vector<uint32_t> texels(16);
    for (int y = 0; y < 4; y++) memcpy(&texels[y * 4], p + y * 256, 16);
    buffer->Unmap(0, nullptr);
    buffer->Release();
    return texels;
}

// "ok", or the first texel that isn't what want(x, y) says.
template <class Want> static const char *Expect(const std::vector<uint32_t> &texels, const char *what, Want want) {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (texels[y * 4 + x] != want(x, y)) {
                snprintf(fail_text, sizeof fail_text, "FAIL %s (%d,%d) %08x want %08x", what, x, y,
                         (unsigned)texels[y * 4 + x], (unsigned)want(x, y));
                return fail_text;
            }
    return "ok";
}

static const char *list4(Gpu &gpu) {
    ID3D12CommandAllocator *allocator;
    TRY(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                           (void **)&allocator), "CreateCommandAllocator");
    static const struct { REFIID iid; const char *name; } lists[] = {
        {__uuidof(ID3D12GraphicsCommandList4), "CreateCommandList(ID3D12GraphicsCommandList4)"},
        {__uuidof(ID3D12GraphicsCommandList5), "CreateCommandList(ID3D12GraphicsCommandList5)"},
        {__uuidof(ID3D12GraphicsCommandList6), "CreateCommandList(ID3D12GraphicsCommandList6)"},
        {__uuidof(ID3D12GraphicsCommandList7), "CreateCommandList(ID3D12GraphicsCommandList7)"},
    };
    ID3D12GraphicsCommandList7 *list7 = nullptr;
    for (auto &l : lists) {
        ID3D12GraphicsCommandList *list;
        TRY(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, l.iid, (void **)&list),
            l.name);
        if (&l != &lists[3]) {
            TRY(list->Close(), "Close");
            list->Release();
        } else {
            list7 = (ID3D12GraphicsCommandList7 *)list;
        }
    }
    static const struct { REFIID iid; const char *name; } older[] = {
        {__uuidof(ID3D12GraphicsCommandList3), "QueryInterface(ID3D12GraphicsCommandList3)"},
        {__uuidof(ID3D12GraphicsCommandList4), "QueryInterface(ID3D12GraphicsCommandList4)"},
        {__uuidof(ID3D12GraphicsCommandList5), "QueryInterface(ID3D12GraphicsCommandList5)"},
        {__uuidof(ID3D12GraphicsCommandList6), "QueryInterface(ID3D12GraphicsCommandList6)"},
    };
    for (auto &o : older) {
        IUnknown *u;
        TRY(list7->QueryInterface(o.iid, (void **)&u), o.name);
        u->Release();
    }
    list7->SetProtectedResourceSession(nullptr);
    list7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
    list7->RSSetShadingRateImage(nullptr);
    list7->DispatchMesh(1, 1, 1);
    list7->SetPipelineState1(nullptr);
    TRY(list7->Close(), "Close(ID3D12GraphicsCommandList7)");
    list7->Release();
    allocator->Release();
    return "ok";
}

static const char *resolveregion(Gpu &gpu, ID3D12GraphicsCommandList7 *list) {
    ID3D12Resource *src = Target(gpu, 4), *dst = Target(gpu);
    list->ClearRenderTargetView(View(gpu, src, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), kRedF, 0, nullptr);
    list->ClearRenderTargetView(View(gpu, dst, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), kBlueF, 0, nullptr);
    gpu.Barrier(src, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    gpu.Barrier(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    D3D12_RECT rect = {0, 0, 2, 2}, corner = {0, 0, 1, 1};
    list->ResolveSubresourceRegion(dst, 0, 2, 2, src, 0, &rect, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    list->ResolveSubresourceRegion(dst, 0, 0, 0, src, 0, &corner, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_MAX);
    auto texels = Read(gpu, dst, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    src->Release();
    dst->Release();
    return Expect(texels, "texel", [](int x, int y) { return x >= 2 && y >= 2 ? kRed : kBlue; });
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Gpu gpu;
    ID3D12GraphicsCommandList7 *list;
    printf("list4 %s\n", list4(gpu));
    if (FAILED(gpu.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), (void **)&list)))
        return printf("list7 FAIL QueryInterface(ID3D12GraphicsCommandList7)\n"), 0;
    printf("resolveregion %s\n", resolveregion(gpu, list));
    list->Release();
    return 0;
}
