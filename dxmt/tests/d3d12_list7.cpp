// ID3D12GraphicsCommandList3 to 7 (DXMT command list 7 spec §7):
//   d3d12_list7.exe
// Prints one line a row, "<row> ok" or "<row> FAIL <what>"; a failed row doesn't end the program.
//   list4  CreateCommandList answers ID3D12GraphicsCommandList4, 5, 6 and 7; the List7 one answers List3 to 6, and the
//          methods behind caps DXMT reports off (protected sessions, VRS) can be called as those tiers allow
//   resolveregion  ResolveSubresourceRegion of a 2x2 rectangle of a red 4x MSAA target to (2,2) of a blue one: those
//          four texels red, the rest still blue; a MAX resolve of (0,0) (which DXMT leaves out) doesn't abort
//   renderpass  BeginRenderPass clears a 4x MSAA target green (and its 4x MSAA depth to 0.5) and EndRenderPass
//          resolves it in two halves into a black texture: all green
//   mrt    two targets, the first CLEAR green and the second PRESERVE: the first green, the second still blue
//   depthpass  no render targets, a depth target cleared 1.0 then CLEAR 0.25 by the pass: 0.25 everywhere
//   suspend  a SUSPENDING pass in one list (CLEAR red, ending in a resolve to dst) and a RESUMING one in the next
//          (CLEAR green, then a resolve to dst2 after it): dst red, dst2 green; both passes' accesses happen
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

// "ok", or the first texel that isn't what want(x, y) says (0: anything).
template <class Want> static const char *Expect(const std::vector<uint32_t> &texels, const char *what, Want want) {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (want(x, y) && texels[y * 4 + x] != want(x, y)) { // 0: any
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
    // The calls valid at those tiers: a null session, 1x1 shading, no shading rate image (a null state object or a
    // mesh dispatch with no mesh pipeline is invalid: D3DMetal faults on SetPipelineState1(nullptr)).
    list7->SetProtectedResourceSession(nullptr);
    list7->RSSetShadingRate(D3D12_SHADING_RATE_1X1, nullptr);
    list7->RSSetShadingRateImage(nullptr);
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
    // (0,0) unchecked: D3D12 resolves MAX (D3DMetal does), DXMT warns and skips it (spec §4.1)
    return Expect(texels, "texel", [](int x, int y) { return !x && !y ? 0 : x >= 2 && y >= 2 ? kRed : kBlue; });
}

// The Gpu's list, then `other`, in one ExecuteCommandLists; waits (10 s at most) and reopens the Gpu's list.
static void Submit(Gpu &gpu, ID3D12CommandList *other) {
    CHECK(gpu.list->Close());
    ID3D12CommandList *lists[] = {gpu.list, other};
    gpu.queue->ExecuteCommandLists(2, lists);
    CHECK(gpu.queue->Signal(gpu.fence, ++gpu.value));
    HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(gpu.fence->SetEventOnCompletion(gpu.value, done));
    if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) { printf("fail timeout\n"); exit(1); }
    CloseHandle(done);
    CHECK(gpu.allocator->Reset());
    CHECK(gpu.list->Reset(gpu.allocator, nullptr));
}

static D3D12_RENDER_PASS_RENDER_TARGET_DESC PassTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                                       D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE begin,
                                                       const FLOAT color[4]) {
    D3D12_RENDER_PASS_RENDER_TARGET_DESC d = {};
    d.cpuDescriptor = rtv;
    d.BeginningAccess.Type = begin;
    d.BeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    memcpy(d.BeginningAccess.Clear.ClearValue.Color, color, 4 * sizeof(FLOAT));
    d.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE;
    return d;
}

static void ResolveInto(D3D12_RENDER_PASS_RENDER_TARGET_DESC &d, ID3D12Resource *src, ID3D12Resource *dst, UINT count,
                        const D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS *params) {
    d.EndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_RESOLVE;
    d.EndingAccess.Resolve = {src, dst, count, params, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE, FALSE};
}

static D3D12_RENDER_PASS_DEPTH_STENCIL_DESC PassDepth(D3D12_CPU_DESCRIPTOR_HANDLE dsv, float depth,
                                                      D3D12_RENDER_PASS_ENDING_ACCESS_TYPE end) {
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC d = {};
    d.cpuDescriptor = dsv;
    d.DepthBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR;
    d.DepthBeginningAccess.Clear.ClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    d.DepthBeginningAccess.Clear.ClearValue.DepthStencil = {depth, 0};
    d.StencilBeginningAccess.Type = D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_NO_ACCESS;
    d.DepthEndingAccess.Type = end;
    d.StencilEndingAccess.Type = D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_NO_ACCESS;
    return d;
}

static const char *renderpass(Gpu &gpu, ID3D12GraphicsCommandList7 *list) {
    ID3D12Resource *target = Target(gpu, 4), *dst = Target(gpu);
    ID3D12Resource *depth = Target(gpu, 4, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                                   D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto rtv = View(gpu, target, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    list->ClearRenderTargetView(rtv, kBlueF, 0, nullptr);
    list->ClearRenderTargetView(View(gpu, dst, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), kBlackF, 0, nullptr);
    gpu.Barrier(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    // Two halves: a resolve's subresource parameters travel as an array (forwarded: one in a union, counted)
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS halves[2] = {{0, 0, 0, 0, {0, 0, 2, 4}},
                                                                               {0, 0, 2, 0, {2, 0, 4, 4}}};
    auto rt = PassTarget(rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR, kGreenF);
    ResolveInto(rt, target, dst, 2, halves);
    auto ds = PassDepth(View(gpu, depth, D3D12_DESCRIPTOR_HEAP_TYPE_DSV), 0.5f, D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_DISCARD);
    list->BeginRenderPass(1, &rt, &ds, D3D12_RENDER_PASS_FLAG_NONE);
    list->EndRenderPass();
    auto texels = Read(gpu, dst, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    target->Release(), dst->Release(), depth->Release();
    return Expect(texels, "texel", [](int, int) { return kGreen; });
}

static const char *mrt(Gpu &gpu, ID3D12GraphicsCommandList7 *list) {
    ID3D12Resource *rt0 = Target(gpu), *rt1 = Target(gpu);
    D3D12_RENDER_PASS_RENDER_TARGET_DESC rts[2] = {
        PassTarget(View(gpu, rt0, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR, kGreenF),
        PassTarget(View(gpu, rt1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_PRESERVE, kGreenF)};
    list->ClearRenderTargetView(rts[0].cpuDescriptor, kBlueF, 0, nullptr);
    list->ClearRenderTargetView(rts[1].cpuDescriptor, kBlueF, 0, nullptr);
    list->BeginRenderPass(2, rts, nullptr, D3D12_RENDER_PASS_FLAG_NONE);
    list->EndRenderPass();
    auto first = Read(gpu, rt0, D3D12_RESOURCE_STATE_RENDER_TARGET), second = Read(gpu, rt1, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rt0->Release(), rt1->Release();
    const char *r = Expect(first, "first", [](int, int) { return kGreen; });
    return strcmp(r, "ok") ? r : Expect(second, "second", [](int, int) { return kBlue; });
}

static const char *depthpass(Gpu &gpu, ID3D12GraphicsCommandList7 *list) {
    ID3D12Resource *depth = Target(gpu, 1, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                                   D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto dsv = View(gpu, depth, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    auto ds = PassDepth(dsv, 0.25f, D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_PRESERVE);
    list->BeginRenderPass(0, nullptr, &ds, D3D12_RENDER_PASS_FLAG_NONE);
    list->EndRenderPass();
    auto texels = Read(gpu, depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    depth->Release();
    return Expect(texels, "depth", [](int, int) { return 0x3e800000u; }); // 0.25f
}

static const char *suspend(Gpu &gpu, ID3D12GraphicsCommandList7 *list) {
    ID3D12Resource *target = Target(gpu, 4), *dst = Target(gpu), *dst2 = Target(gpu);
    auto rtv = View(gpu, target, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    list->ClearRenderTargetView(rtv, kBlueF, 0, nullptr);
    list->ClearRenderTargetView(View(gpu, dst, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), kBlackF, 0, nullptr);
    list->ClearRenderTargetView(View(gpu, dst2, D3D12_DESCRIPTOR_HEAP_TYPE_RTV), kBlackF, 0, nullptr);
    gpu.Barrier(dst, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    gpu.Barrier(dst2, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS whole = {0, 0, 0, 0, {0, 0, 4, 4}};
    auto a = PassTarget(rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR, kRedF);
    ResolveInto(a, target, dst, 1, &whole);
    list->BeginRenderPass(1, &a, nullptr, D3D12_RENDER_PASS_FLAG_SUSPENDING_PASS);
    list->EndRenderPass();
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList7 *b;
    TRY(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                           (void **)&allocator), "CreateCommandAllocator");
    TRY(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                      __uuidof(ID3D12GraphicsCommandList7), (void **)&b), "CreateCommandList");
    auto resumed = PassTarget(rtv, D3D12_RENDER_PASS_BEGINNING_ACCESS_TYPE_CLEAR, kGreenF);
    b->BeginRenderPass(1, &resumed, nullptr, D3D12_RENDER_PASS_FLAG_RESUMING_PASS);
    b->EndRenderPass();
    D3D12_RESOURCE_BARRIER to_source = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    to_source.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_RESOLVE_SOURCE};
    b->ResourceBarrier(1, &to_source);
    b->ResolveSubresource(dst2, 0, target, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    TRY(b->Close(), "Close");
    Submit(gpu, b);
    auto first = Read(gpu, dst, D3D12_RESOURCE_STATE_RESOLVE_DEST), second = Read(gpu, dst2, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    b->Release(), allocator->Release(), target->Release(), dst->Release(), dst2->Release();
    const char *r = Expect(first, "dst", [](int, int) { return kRed; });
    return strcmp(r, "ok") ? r : Expect(second, "dst2", [](int, int) { return kGreen; });
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Gpu gpu;
    ID3D12GraphicsCommandList7 *list;
    printf("list4 %s\n", list4(gpu));
    if (FAILED(gpu.list->QueryInterface(__uuidof(ID3D12GraphicsCommandList7), (void **)&list)))
        return printf("list7 FAIL QueryInterface(ID3D12GraphicsCommandList7)\n"), 0;
    printf("resolveregion %s\n", resolveregion(gpu, list));
    printf("renderpass %s\n", renderpass(gpu, list));
    printf("mrt %s\n", mrt(gpu, list));
    printf("depthpass %s\n", depthpass(gpu, list));
    printf("suspend %s\n", suspend(gpu, list));
    list->Release();
    return 0;
}
