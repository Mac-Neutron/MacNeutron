// Encoder ordering (GPU overlap spec §5): each mode's first pass is heavy, so work after it that doesn't wait for it
// reads or overwrites its results too early. Shaders: shaders/hazards.hlsl, one .dxil per entry point.
//   d3d12_hazards.exe <shader folder> [mode...]   (no mode: every mode)
// Prints "hazard <mode> <values>" per mode. check.sh compares them with fixed values, on our DXMT (with overlap, in
// strict order, and while dumping passes) and on D3DMetal.
#include "d3d12_common.hpp"
#include <cmath>
#include <string>
#include <utility>
#include <vector>

static const UINT kSize = 1024;
static const DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; // blendable on every Mac; exact integers to 2048
static const auto RT = D3D12_RESOURCE_STATE_RENDER_TARGET, PSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                  UA = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, COPY_DEST = D3D12_RESOURCE_STATE_COPY_DEST,
                  COPY_SOURCE = D3D12_RESOURCE_STATE_COPY_SOURCE;

struct Target {
    ID3D12Resource *texture;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    D3D12_GPU_DESCRIPTOR_HANDLE srv;
};

static Gpu *g;
static ID3D12RootSignature *root, *croot;
static ID3D12PipelineState *add, *set, *sample, *add_sample, *fill, *count, *args;
static ID3D12CommandSignature *draw_signature;
static ID3D12DescriptorHeap *rtv_heap, *srv_heap;
static UINT targets_made;
static ID3D12Resource *readback; // 64 slots of 512 bytes
static Target T[3];

static float Half(uint16_t h) { // the positive halfs these tests write
    int e = h >> 10 & 31, m = h & 1023;
    return e ? std::ldexp(1024.0f + m, e - 25) : std::ldexp((float)m, -24);
}

static Target MakeTarget(ID3D12Resource *texture = nullptr) {
    if (!texture)
        texture = g->Texture(Tex2D(kSize, kSize, kFormat, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), RT);
    UINT rs = g->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT ss = g->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Target t = {texture, rtv_heap->GetCPUDescriptorHandleForHeapStart(), srv_heap->GetGPUDescriptorHandleForHeapStart()};
    D3D12_CPU_DESCRIPTOR_HANDLE srv = srv_heap->GetCPUDescriptorHandleForHeapStart();
    t.rtv.ptr += targets_made * rs;
    t.srv.ptr += targets_made * ss;
    srv.ptr += targets_made * ss;
    targets_made++;
    g->device->CreateRenderTargetView(texture, nullptr, t.rtv);
    g->device->CreateShaderResourceView(texture, nullptr, srv);
    return t;
}

static ID3D12PipelineState *Graphics(const std::vector<char> &vs, const std::vector<char> &ps, bool additive) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {};
    gd.pRootSignature = root;
    gd.VS = {vs.data(), vs.size()};
    gd.PS = {ps.data(), ps.size()};
    auto &blend = gd.BlendState.RenderTarget[0];
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (additive) {
        blend.BlendEnable = TRUE;
        blend.SrcBlend = blend.DestBlend = blend.SrcBlendAlpha = blend.DestBlendAlpha = D3D12_BLEND_ONE;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    gd.SampleMask = UINT_MAX;
    gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    gd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    gd.RasterizerState.DepthClipEnable = TRUE;
    gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    gd.NumRenderTargets = 1;
    gd.RTVFormats[0] = kFormat;
    gd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso;
    CHECK(g->device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));
    return pso;
}

static ID3D12PipelineState *Compute(const std::vector<char> &cs) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {croot, {cs.data(), cs.size()}};
    ID3D12PipelineState *pso;
    CHECK(g->device->CreateComputePipelineState(&cd, __uuidof(ID3D12PipelineState), (void **)&pso));
    return pso;
}

static void Clear(Target &t) {
    const float zero[4] = {0, 0, 0, 0};
    g->list->ClearRenderTargetView(t.rtv, zero, 0, nullptr);
}

// Sets up a full-screen draw into `t` with `pso` and its constant `value` (sampling `from`, for the sample pipelines).
static void Bind(Target &t, ID3D12PipelineState *pso, float value, Target *from = nullptr) {
    auto *l = g->list;
    D3D12_VIEWPORT vp = {0, 0, (float)kSize, (float)kSize, 0, 1};
    D3D12_RECT sc = {0, 0, (LONG)kSize, (LONG)kSize};
    l->OMSetRenderTargets(1, &t.rtv, FALSE, nullptr);
    l->RSSetViewports(1, &vp);
    l->RSSetScissorRects(1, &sc);
    l->SetDescriptorHeaps(1, &srv_heap);
    l->SetGraphicsRootSignature(root);
    l->SetPipelineState(pso);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT constants[2] = {0, 0};
    memcpy(&constants[0], &value, 4);
    l->SetGraphicsRoot32BitConstants(0, 2, constants, 0);
    if (from)
        l->SetGraphicsRootDescriptorTable(1, from->srv);
}

// `instances` full-screen triangles into `t` (with additive blending, each adds `value`).
static void Pass(Target &t, ID3D12PipelineState *pso, float value, UINT instances, Target *from = nullptr) {
    Bind(t, pso, value, from);
    g->list->DrawInstanced(3, instances, 0, 0);
}

// `groups` groups of `pso` on buffer `b` (u0), spinning `loops` times where the shader does.
static void Dispatch(ID3D12PipelineState *pso, ID3D12Resource *b, UINT groups, UINT loops = 0) {
    auto *l = g->list;
    UINT constants[2] = {0, loops};
    l->SetComputeRootSignature(croot);
    l->SetPipelineState(pso);
    l->SetComputeRoot32BitConstants(0, 2, constants, 0);
    l->SetComputeRootUnorderedAccessView(1, b->GetGPUVirtualAddress());
    l->Dispatch(groups, 1, 1);
}

// A default-heap buffer of `size` zero bytes (UAV-capable), left in `state`: committed, or placed at the start of `heap`.
static ID3D12Resource *Zeroed(UINT64 size, D3D12_RESOURCE_STATES state, ID3D12Heap *heap = nullptr) {
    ID3D12Resource *upload = g->Buffer(D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p;
    CHECK(upload->Map(0, nullptr, &p));
    memset(p, 0, size);
    upload->Unmap(0, nullptr);
    ID3D12Resource *b;
    if (heap) {
        D3D12_RESOURCE_DESC d = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                                 D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
        CHECK(g->device->CreatePlacedResource(heap, 0, &d, COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void **)&b));
    } else {
        b = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    g->list->CopyBufferRegion(b, 0, upload, 0, size);
    if (state != COPY_DEST)
        g->Barrier(b, COPY_DEST, state);
    g->Submit();
    upload->Release();
    return b;
}

// Copies texel (x, y) of `t` (in `state`) into readback slot `slot`.
static void Read(Target &t, D3D12_RESOURCE_STATES state, UINT x, UINT y, UINT slot) {
    g->Barrier(t.texture, state, COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {t.texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = {(UINT64)slot * 512, {kFormat, 1, 1, 1, 256}};
    D3D12_BOX box = {x, y, 0, x + 1, y + 1, 1};
    g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    g->Barrier(t.texture, COPY_SOURCE, state);
}

// Copies `bytes` at `offset` of buffer `b` (in `state`) into readback slot `slot`.
static void ReadBuffer(ID3D12Resource *b, D3D12_RESOURCE_STATES state, UINT64 offset, UINT bytes, UINT slot) {
    g->Barrier(b, state, COPY_SOURCE);
    g->list->CopyBufferRegion(readback, slot * 512, b, offset, bytes);
    g->Barrier(b, COPY_SOURCE, state);
}

// After the list ran: slot `slot`'s first channel, or its first `bytes` as an integer.
static float Texel(UINT slot) {
    uint8_t *r;
    D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
    CHECK(readback->Map(0, &whole, (void **)&r));
    uint16_t h;
    memcpy(&h, r + slot * 512, 2);
    readback->Unmap(0, &none);
    return Half(h);
}
static unsigned long long Word(UINT slot, UINT bytes) {
    uint8_t *r;
    D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
    CHECK(readback->Map(0, &whole, (void **)&r));
    unsigned long long v = 0;
    memcpy(&v, r + slot * 512, bytes);
    readback->Unmap(0, &none);
    return v;
}

// T0 rendered (heavy); a barrier RENDER_TARGET -> PIXEL_SHADER_RESOURCE; T1 = T0 sampled + 1.
static void RtRead() {
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    g->Barrier(T[0].texture, RT, PSR);
    Pass(T[1], sample, 1, 1, &T[0]);
    g->Barrier(T[0].texture, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    g->Submit();
    printf("hazard rt-read %g\n", Texel(0));
}

// No barrier: T0 (heavy), T1, then T0 again, replacing it. The last pass into T0 wins.
static void SameTarget() {
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    Pass(T[1], set, 5, 1);
    Pass(T[0], set, 7, 1);
    Read(T[0], RT, 512, 512, 0);
    Read(T[1], RT, 512, 512, 1);
    g->Submit();
    printf("hazard same-target %g %g\n", Texel(0), Texel(1));
}

// A dispatch adds 1 per thread to word 0 of `b` (heavy); a copy elsewhere; a UAV barrier; a dispatch copies word 0 to
// word 1.
static void UavOn(const char *name, ID3D12Resource *b) {
    ID3D12Resource *elsewhere = Zeroed(256, COPY_DEST), *zeros = Zeroed(256, COPY_SOURCE);
    Dispatch(fill, b, 16384);
    g->list->CopyBufferRegion(elsewhere, 0, zeros, 0, 256);
    D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
    barrier.UAV.pResource = b;
    g->list->ResourceBarrier(1, &barrier);
    Dispatch(count, b, 1);
    ReadBuffer(b, UA, 4, 4, 0);
    g->Submit();
    printf("hazard %s %llu\n", name, Word(0, 4));
}
static void Uav() { UavOn("uav", Zeroed(256, UA)); }
// The same in a buffer placed in a DEFAULT heap (on our DXMT, Private like its heap), read back through READBACK.
static void PlacedUav() {
    D3D12_HEAP_DESC hd = {65536, {D3D12_HEAP_TYPE_DEFAULT}, 0, D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS};
    ID3D12Heap *heap;
    CHECK(g->device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
    UavOn("placed-uav", Zeroed(256, UA, heap));
}

// Two textures placed in DEFAULT heaps the app releases at once: as in D3D12, each texture keeps its heap (and DXMT the
// heap's memory resident). The first is rendered (heavy), then sampled through a descriptor into T1 + 1, as rt-read.
// Prints T1's texel. The first texture is released after the readback, and its heap with it; the second lives on, and
// its heap with it (DXMT_STATS, this mode alone: "heaps destroyed 1"; 2 if placed textures didn't hold their heaps).
// Heap->Release() answers 0 either way (Ruling 24: the texture's hold is private), so it isn't printed (Task FR).
static void HeapReleased() {
    D3D12_HEAP_DESC hd = {32 << 20, {D3D12_HEAP_TYPE_DEFAULT}, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                          D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES};
    D3D12_RESOURCE_DESC desc = Tex2D(kSize, kSize, kFormat, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    ID3D12Resource *placed[2];
    for (auto &r : placed) {
        ID3D12Heap *heap;
        CHECK(g->device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
        CHECK(g->device->CreatePlacedResource(heap, 0, &desc, RT, nullptr, __uuidof(ID3D12Resource), (void **)&r));
        heap->Release();
    }
    ID3D12Resource *p = placed[0];
    Target P = MakeTarget(p);
    Clear(P);
    Pass(P, add, 1, 256);
    g->Barrier(p, RT, PSR);
    Pass(T[1], sample, 1, 1, &P);
    g->Barrier(p, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    g->Submit();
    p->Release(); // placed[1] lives on
    printf("hazard heap-released %g\n", Texel(0));
}

// A timestamp sampled and never resolved, its query heap released once the fence passed (a profiler turned off): our
// DXMT writes the query's value on the CPU after the command buffer completes, so the fence waits for that write
// (DXMT_STATS: "fence signals deferred to the CPU 1", this mode alone).
static void Sampled() {
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1};
    ID3D12QueryHeap *heap;
    CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    Clear(T[0]);
    Pass(T[0], add, 1, 1);
    g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->Submit();
    heap->Release();
    printf("hazard sampled 1\n");
}

// 16 copies of 5s into T2; a barrier COPY_DEST -> PIXEL_SHADER_RESOURCE; T1 = T2 sampled + 1.
static void CopyRead() {
    const UINT pitch = kSize * 8;
    ID3D12Resource *upload = g->Buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)pitch * kSize, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint16_t *p;
    CHECK(upload->Map(0, nullptr, (void **)&p));
    for (UINT i = 0; i < kSize * kSize * 4; i++)
        p[i] = 0x4500; // 5.0
    upload->Unmap(0, nullptr);
    g->Barrier(T[2].texture, RT, COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src = {upload, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint = {0, {kFormat, kSize, kSize, 1, pitch}};
    D3D12_TEXTURE_COPY_LOCATION dst = {T[2].texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dst.SubresourceIndex = 0;
    for (int i = 0; i < 16; i++)
        g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    g->Barrier(T[2].texture, COPY_DEST, PSR);
    Pass(T[1], sample, 1, 1, &T[2]);
    g->Barrier(T[2].texture, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    g->Submit();
    printf("hazard copy-read %g\n", Texel(0));
}

// A dispatch spins, then writes one draw's arguments; a barrier UNORDERED_ACCESS -> INDIRECT_ARGUMENT; ExecuteIndirect
// draws 9 into T0 with them (arguments read too early are zeros: no draw).
static void Indirect() {
    ID3D12Resource *a = Zeroed(16, UA);
    Clear(T[0]);
    Dispatch(args, a, 1, 1u << 22);
    g->Barrier(a, UA, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Bind(T[0], set, 9);
    g->list->ExecuteIndirect(draw_signature, 1, a, 0, nullptr, 0);
    g->Barrier(a, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, UA);
    Read(T[0], RT, 512, 512, 0);
    g->Submit();
    printf("hazard indirect %g\n", Texel(0));
}

// P and Q share one heap's memory: P rendered (heavy); an aliasing barrier P -> Q; Q cleared, then 2 drawn: 2.
static void Aliasing() {
    D3D12_RESOURCE_DESC desc = Tex2D(kSize, kSize, kFormat, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_HEAP_DESC hd = {};
    hd.SizeInBytes = 32 << 20;
    hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    ID3D12Heap *heap;
    CHECK(g->device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
    ID3D12Resource *p, *q;
    CHECK(g->device->CreatePlacedResource(heap, 0, &desc, RT, nullptr, __uuidof(ID3D12Resource), (void **)&p));
    CHECK(g->device->CreatePlacedResource(heap, 0, &desc, RT, nullptr, __uuidof(ID3D12Resource), (void **)&q));
    Target P = MakeTarget(p), Q = MakeTarget(q);
    Clear(P);
    Pass(P, add, 1, 256);
    D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_ALIASING};
    barrier.Aliasing.pResourceBefore = p;
    barrier.Aliasing.pResourceAfter = q;
    g->list->ResourceBarrier(1, &barrier);
    Clear(Q);
    Pass(Q, add, 2, 1);
    Read(Q, RT, 512, 512, 0);
    g->Submit();
    printf("hazard aliasing %g\n", Texel(0));
}

// A heavy pass counts its samples (256 x 1024 x 1024); ResolveQueryData on the GPU, which no barrier orders.
static void Occlusion() {
    ID3D12QueryHeap *heap;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 1};
    CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    ID3D12Resource *result = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, 8, COPY_DEST);
    Clear(T[0]);
    Bind(T[0], add, 1);
    g->list->BeginQuery(heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
    g->list->DrawInstanced(3, 256, 0, 0);
    g->list->EndQuery(heap, D3D12_QUERY_TYPE_OCCLUSION, 0);
    g->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, result, 0);
    ReadBuffer(result, COPY_DEST, 0, 8, 0);
    g->Submit();
    printf("hazard occlusion %llu\n", Word(0, 8));
}

// Two heavy passes into different targets with no barrier between them (free to overlap): both complete.
static void Independent() {
    Clear(T[0]);
    Clear(T[1]);
    Pass(T[0], add, 1, 256);
    Pass(T[1], add, 1, 256);
    Read(T[0], RT, 512, 512, 0);
    Read(T[1], RT, 512, 512, 1);
    g->Submit();
    printf("hazard independent %g %g\n", Texel(0), Texel(1));
}

// T0 rendered (heavy), T1 rendered, a barrier for T0 alone, T2 = T0 sampled + 1, T2 read. With precise transitions
// (M2) the sampling pass waits on T0's pass and not T1's, and the read on T0's and T2's: DXMT_STATS counts 2 encoders
// with a dependency list and 3 dependency waits.
static void Precise() {
    Clear(T[0]);
    Clear(T[1]);
    Clear(T[2]);
    g->Submit();
    Pass(T[0], add, 1, 256);
    Pass(T[1], set, 5, 1);
    g->Barrier(T[0].texture, RT, PSR);
    Pass(T[2], sample, 1, 1, &T[0]);
    Read(T[2], RT, 512, 512, 0);
    g->Barrier(T[0].texture, PSR, RT);
    g->Submit();
    printf("hazard precise %g\n", Texel(0));
}

// Large copies into X (word 0 = 77, the rest 0); a dispatch elsewhere opens a compute encoder; inside it, a barrier X
// COPY_DEST -> UNORDERED_ACCESS and a dispatch copying X's word 0 to word 1: 77 (0 if it ran before the copy).
static void MidPass() {
    const UINT64 size = 128 << 20;
    ID3D12Resource *upload = g->Buffer(D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint32_t *p;
    CHECK(upload->Map(0, nullptr, (void **)&p));
    memset(p, 0, size);
    p[0] = 77;
    upload->Unmap(0, nullptr);
    ID3D12Resource *x = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *elsewhere = Zeroed(256, UA);
    for (int i = 0; i < 8; i++) // heavy: 1 GB copied, in order (one blit encoder)
        g->list->CopyBufferRegion(x, 0, upload, 0, size);
    Dispatch(fill, elsewhere, 1);
    g->Barrier(x, COPY_DEST, UA);
    Dispatch(count, x, 1);
    ReadBuffer(x, UA, 4, 4, 0);
    g->Submit();
    printf("hazard mid-pass %llu\n", Word(0, 4));
}

// One list run twice back to back: T0 cleared and rendered, then sampled into T1, adding (T1 += 257), then back to
// RENDER_TARGET.
static void Twice() {
    Clear(T[1]);
    g->Submit();
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    g->Barrier(T[0].texture, RT, PSR);
    Pass(T[1], add_sample, 1, 1, &T[0]);
    g->Barrier(T[0].texture, PSR, RT);
    g->Submit(2);
    Read(T[1], RT, 512, 512, 0);
    g->Submit();
    printf("hazard twice %g\n", Texel(0));
}

// 300 passes, into T0 and T1 in turn, 8 instances of 1 each, no barrier: more encoders in one list than the queue's
// fences (256).
static void Many() {
    Clear(T[0]);
    Clear(T[1]);
    for (int i = 0; i < 300; i++)
        Pass(T[i % 2], add, 1, 8);
    Read(T[0], RT, 512, 512, 0);
    Read(T[1], RT, 512, 512, 1);
    g->Submit();
    printf("hazard many %g %g\n", Texel(0), Texel(1));
}

// T0 rendered (heavy), then a rectangle of it cleared to 3 (DXMT clears rectangles with a pass of its own).
static void ClearRects() {
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    const float three[4] = {3, 3, 3, 3};
    D3D12_RECT rect = {500, 500, 600, 600};
    g->list->ClearRenderTargetView(T[0].rtv, three, 1, &rect);
    Read(T[0], RT, 512, 512, 0);
    Read(T[0], RT, 0, 0, 1);
    g->Submit();
    printf("hazard clear-rects %g %g\n", Texel(0), Texel(1));
}

// T0 rendered (heavy); a barrier RENDER_TARGET -> COPY_SOURCE; 16 copies of all of T0 into a readback buffer; then a
// light clear of T1 that waits on none of them and may finish first. The D3D12 fence (Submit) must still wait for
// every copy: prints how many copied texel channels aren't 256 (0).
static void Signal() {
    const UINT pitch = kSize * 8, copies = 16;
    const UINT64 size = (UINT64)pitch * kSize;
    ID3D12Resource *big = g->Buffer(D3D12_HEAP_TYPE_READBACK, size * copies, COPY_DEST);
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    g->Barrier(T[0].texture, RT, COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {T[0].texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    for (UINT i = 0; i < copies; i++) {
        D3D12_TEXTURE_COPY_LOCATION dst = {big, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = {size * i, {kFormat, kSize, kSize, 1, pitch}};
        g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    Clear(T[1]);
    g->Barrier(T[0].texture, COPY_SOURCE, RT);
    g->Submit();
    uint16_t *p;
    D3D12_RANGE whole = {0, (SIZE_T)(size * copies)}, none = {0, 0};
    CHECK(big->Map(0, &whole, (void **)&p));
    unsigned long long wrong = 0;
    for (UINT64 i = 0; i < size * copies / 2; i++)
        wrong += p[i] != 0x5C00; // 256.0
    big->Unmap(0, &none);
    big->Release();
    printf("hazard signal %llu\n", wrong);
}

// A dispatch adds 1 per thread to word 0 (heavy), then 300 timestamps in empty encoders of their own, then a UAV
// barrier and a dispatch copying word 0 to word 1. The queue's 256 fences wrap while the heavy dispatch may still run:
// its fence must not be handed on while the barrier's join still has to wait on it.
static void Wrap() {
    ID3D12Resource *b = Zeroed(256, UA);
    ID3D12QueryHeap *heap;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 300};
    CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    Dispatch(fill, b, 65536);
    for (UINT i = 0; i < 300; i++)
        g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, i);
    D3D12_RESOURCE_BARRIER barrier = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
    barrier.UAV.pResource = b;
    g->list->ResourceBarrier(1, &barrier);
    Dispatch(count, b, 1);
    ReadBuffer(b, UA, 4, 4, 0);
    g->Submit();
    printf("hazard wrap %llu\n", Word(0, 4));
}

// T0 rendered (heavy), then T1 and T2 rendered in the same group (no barrier): with one wait (spec §3.9) the two later
// passes each wait on the first pass's early fence alone. DXMT_STATS, this mode alone: 10 fence waits (the clears 0,
// 1, 1; the passes 3, 1, 1; the read 3).
static void OneWait() {
    Clear(T[0]);
    Clear(T[1]);
    Clear(T[2]);
    g->Submit();
    Pass(T[0], add, 1, 256);
    Pass(T[1], set, 5, 1);
    Pass(T[2], set, 6, 1);
    g->Submit();
    Read(T[0], RT, 512, 512, 0);
    Read(T[1], RT, 512, 512, 1);
    Read(T[2], RT, 512, 512, 2);
    g->Submit();
    printf("hazard onewait %g %g %g\n", Texel(0), Texel(1), Texel(2));
}

// Five passes, into T0 and T1 in turn, no barrier (M3 doesn't merge passes with another target between them): with
// newest-writer dependencies (spec §3.9) each waits on the previous pass into its target alone. DXMT_STATS, this
// mode alone, with overlap: 3 dependency waits (every earlier writer: 4).
static void Newest() {
    Clear(T[0]);
    Clear(T[1]);
    g->Submit();
    for (int i = 0; i < 5; i++)
        Pass(T[i % 2], add, 1, 1);
    g->Submit();
    Read(T[0], RT, 512, 512, 0);
    Read(T[1], RT, 512, 512, 1);
    g->Submit();
    printf("hazard newest %g %g\n", Texel(0), Texel(1));
}

// T0 rendered (heavy); a barrier to PIXEL_SHADER_RESOURCE and a UAV barrier (a join); a pass into T2 whose only draw
// has no instances; then T1 = T0 sampled + 1, in that pass's group. The sampling pass must still see T0 done: a
// join's early fence (spec §3.9) is reached only after its waits, draws or not.
static void NoDraw() {
    Clear(T[0]);
    g->Submit();
    Pass(T[0], add, 1, 256);
    g->Barrier(T[0].texture, RT, PSR);
    D3D12_RESOURCE_BARRIER all = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
    g->list->ResourceBarrier(1, &all);
    Pass(T[2], set, 1, 0);
    Pass(T[1], sample, 1, 1, &T[0]);
    g->Barrier(T[0].texture, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    g->Submit();
    printf("hazard nodraw %g\n", Texel(0));
}


// Two queues. Queue 1 renders T0 (heavy) and signals f = 1; queue 2 waits for it, samples T0 into T1 (+1), reads T1
// and signals done = 1; queue 1 waits for that, reads T1 again and signals f = 2, which the CPU waits for. With one
// open command buffer per queue (spec §3.10) nothing may wait on uncommitted work: no deadlock, 257 257. DXMT_STATS,
// this mode alone: 3 command buffers committed (Execute+Signal, Wait+Execute+Signal, Wait+Execute+Signal).
static void Queues() {
    ID3D12CommandQueue *q2;
    ID3D12CommandAllocator *a2, *a3;
    ID3D12GraphicsCommandList *l2, *l3;
    ID3D12Fence *f, *done;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(g->device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&q2));
    CHECK(g->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&a2));
    CHECK(g->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&a3));
    CHECK(g->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a2, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&l2));
    CHECK(g->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a3, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&l3));
    CHECK(g->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&f));
    CHECK(g->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&done));
    auto *l1 = g->list;
    ID3D12CommandList *one[1];
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    CHECK(l1->Close());
    one[0] = l1;
    g->queue->ExecuteCommandLists(1, one);
    CHECK(g->queue->Signal(f, 1));
    g->list = l2;
    g->Barrier(T[0].texture, RT, PSR);
    Pass(T[1], sample, 1, 1, &T[0]);
    g->Barrier(T[0].texture, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    CHECK(l2->Close());
    one[0] = l2;
    CHECK(q2->Wait(f, 1));
    q2->ExecuteCommandLists(1, one);
    CHECK(q2->Signal(done, 1));
    g->list = l3;
    Read(T[1], RT, 512, 512, 1);
    CHECK(l3->Close());
    one[0] = l3;
    CHECK(g->queue->Wait(done, 1));
    g->queue->ExecuteCommandLists(1, one);
    CHECK(g->queue->Signal(f, 2));
    g->list = l1;
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(f->SetEventOnCompletion(2, ev));
    if (WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) { printf("hazard queues timeout\n"); exit(1); }
    CloseHandle(ev);
    CHECK(g->allocator->Reset());
    CHECK(l1->Reset(g->allocator, nullptr));
    printf("hazard queues %g %g\n", Texel(0), Texel(1));
}


// Two command lists in one ExecuteCommandLists call: the first renders T0 (heavy); the second starts with a
// timestamp, then renders into T0 again, loading it. M3 (spec §3.5) encodes both as one Metal render pass with the
// timestamp at its end (DXMT_STATS: 1 render pass merged, 1 timestamp blit folded). No merge in the variants:
// `barrier`: a barrier on T1 ends the first list and starts the second; `same_buffer`: the first pass already takes a
// timestamp from the counter buffer the second list's timestamp uses (Metal samples a buffer once per pass);
// `mid_barrier`: a UAV barrier between the second pass's two draws (T0 258); `query`: the first pass counts into
// occlusion query 0, the second draws once before its own query 1 on the same heap and once inside it: query 0 counts
// the first pass's 256 instances alone (prints T0 then the two counts).
enum UnsplitKind { kPlain, kBarrier, kSameBuffer, kMidBarrier, kQuery, kSegments };
static void Unsplit(const char *name, UnsplitKind kind) {
    static ID3D12CommandAllocator *a2;
    static ID3D12GraphicsCommandList *l2;
    static ID3D12QueryHeap *heap, *occlusion;
    if (!l2) {
        CHECK(g->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&a2));
        CHECK(g->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a2, nullptr, __uuidof(ID3D12GraphicsCommandList), (void **)&l2));
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
        D3D12_QUERY_HEAP_DESC od = {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 2};
        CHECK(g->device->CreateQueryHeap(&od, __uuidof(ID3D12QueryHeap), (void **)&occlusion));
    }
    Clear(T[0]);
    g->Submit();
    auto *l1 = g->list;
    if (kind == kSegments) // the list's first encoder, so its pass isn't the list's join (SMITE 2's base pass)
        l1->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 2);
    if (kind == kQuery) {
        Bind(T[0], add, 1);
        l1->BeginQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
        l1->DrawInstanced(3, 256, 0, 0);
        l1->EndQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
    } else {
        Pass(T[0], add, 1, 256);
    }
    if (kind == kSameBuffer || kind == kSegments)
        l1->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 1); // at the open pass's end
    if (kind == kBarrier)
        g->Barrier(T[1].texture, RT, PSR);
    g->list = l2;
    if (kind == kBarrier)
        g->Barrier(T[1].texture, PSR, RT);
    l2->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0); // a timestamp-only blit
    Pass(T[0], add, 1, 1);
    if (kind == kSegments)
        l2->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 3);
    if (kind == kQuery) {
        l2->BeginQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 1);
        l2->DrawInstanced(3, 1, 0, 0);
        l2->EndQuery(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 1);
    }
    if (kind == kMidBarrier) {
        D3D12_RESOURCE_BARRIER all = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
        l2->ResourceBarrier(1, &all);
        l2->DrawInstanced(3, 1, 0, 0);
    }
    g->list = l1;
    CHECK(l1->Close());
    CHECK(l2->Close());
    ID3D12CommandList *lists[] = {l1, l2};
    g->queue->ExecuteCommandLists(2, lists);
    CHECK(g->queue->Signal(g->fence, ++g->value));
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(g->fence->SetEventOnCompletion(g->value, ev));
    if (WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) { printf("hazard %s timeout\n", name); exit(1); }
    CloseHandle(ev);
    CHECK(g->allocator->Reset());
    CHECK(l1->Reset(g->allocator, nullptr));
    CHECK(a2->Reset());
    CHECK(l2->Reset(a2, nullptr));
    Read(T[0], RT, 512, 512, 0);
    if (kind == kQuery) {
        static ID3D12Resource *counts;
        if (!counts)
            counts = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, 16, COPY_DEST);
        g->list->ResolveQueryData(occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0, 2, counts, 0);
        ReadBuffer(counts, COPY_DEST, 0, 8, 1);
        ReadBuffer(counts, COPY_DEST, 8, 8, 2);
    }
    if (kind == kSegments)
        g->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 4, readback, 60 * 512);
    g->Submit();
    if (kind == kQuery)
        printf("hazard %s %g %llu %llu\n", name, Texel(0), Word(1, 8), Word(2, 8));
    else if (kind == kSegments) { // in time: 2 (l1's start), 1 (its pass's end), 0 (l2's start), 3 (its pass's end)
        UINT64 t[4];
        uint8_t *p;
        D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
        CHECK(readback->Map(0, &whole, (void **)&p));
        memcpy(t, p + 60 * 512, sizeof(t));
        readback->Unmap(0, &none);
        printf("hazard %s %g %d\n", name, Texel(0), t[2] <= t[1] && t[1] <= t[0] && t[0] <= t[3]);
    } else
        printf("hazard %s %g\n", name, Texel(0));
}
static void UnsplitPlain() { Unsplit("unsplit", kPlain); }
static void UnsplitBarrier() { Unsplit("unsplit-barrier", kBarrier); }
static void UnsplitSameBuffer() { Unsplit("unsplit-samebuffer", kSameBuffer); }
static void UnsplitMidBarrier() { Unsplit("unsplit-midbarrier", kMidBarrier); }
static void UnsplitQuery() { Unsplit("unsplit-query", kQuery); }
// SMITE 2's base-pass segments: each list a start timestamp, a pass and its end timestamp, one heap (B6).
static void UnsplitSegments() { Unsplit("unsplit-segments", kSegments); }

// One list, one pass adding 1 to T0, executed twice in one ExecuteCommandLists call: both runs draw (T0 2); M3 must
// not merge a list into itself.
static void UnsplitTwice() {
    Clear(T[0]);
    g->Submit();
    Pass(T[0], add, 1, 1);
    CHECK(g->list->Close());
    ID3D12CommandList *lists[] = {g->list, g->list};
    g->queue->ExecuteCommandLists(2, lists);
    CHECK(g->queue->Signal(g->fence, ++g->value));
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(g->fence->SetEventOnCompletion(g->value, ev));
    if (WaitForSingleObject(ev, 10000) != WAIT_OBJECT_0) { printf("hazard unsplit-twice timeout\n"); exit(1); }
    CloseHandle(ev);
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    Read(T[0], RT, 512, 512, 0);
    g->Submit();
    printf("hazard unsplit-twice %g\n", Texel(0));
}

// A timestamp resolved on the CPU, then a Signal: that signal waits for the timestamps (on the CPU). A later Signal
// with no timestamps pending is the GPU's again (DXMT_STATS, this mode alone: 1 signal deferred to the CPU).
static void Deferred() {
    static ID3D12QueryHeap *heap;
    if (!heap) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    }
    g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, readback, 63 * 512);
    g->Submit();
    Clear(T[0]);
    g->Submit();
    printf("hazard deferred 1\n");
}

// A committed resource starts zeroed (D3D12 promises it for buffers; render targets read zeros on D3DMetal too): buffers
// and render targets filled with junk and released, then created again at the same sizes, read back all zeros (prints
// the nonzero bytes found in the new buffers, then textures).
static void Zeroed() {
    const UINT64 size = 4 << 20;
    const UINT n = 8;
    ID3D12Resource *junk = g->Buffer(D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p;
    CHECK(junk->Map(0, nullptr, &p));
    memset(p, 0xAB, size);
    junk->Unmap(0, nullptr);
    for (UINT i = 0; i < n; i++) {
        ID3D12Resource *b = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        g->list->CopyBufferRegion(b, 0, junk, 0, size);
        Target t = MakeTarget();
        const float pink[4] = {1, 0, 0.5f, 1};
        g->list->ClearRenderTargetView(t.rtv, pink, 0, nullptr);
        g->Submit();
        b->Release();
        t.texture->Release();
        targets_made--;
    }
    ID3D12Resource *rb = g->Buffer(D3D12_HEAP_TYPE_READBACK, size, COPY_DEST);
    unsigned long long buffer_bytes = 0, texture_bytes = 0;
    for (UINT i = 0; i < n; i++) {
        ID3D12Resource *b = g->Buffer(D3D12_HEAP_TYPE_DEFAULT, size, COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        g->Barrier(b, COPY_DEST, COPY_SOURCE);
        g->list->CopyBufferRegion(rb, 0, b, 0, size);
        g->Submit();
        uint8_t *r;
        D3D12_RANGE whole = {0, (SIZE_T)size}, none = {0, 0};
        CHECK(rb->Map(0, &whole, (void **)&r));
        for (UINT64 k = 0; k < size; k++)
            buffer_bytes += r[k] != 0;
        rb->Unmap(0, &none);
        b->Release();
        Target t = MakeTarget();
        g->Barrier(t.texture, RT, COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src = {t.texture, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst = {rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = {0, {kFormat, kSize, kSize / 2, 1, kSize * 8}};
        D3D12_BOX box = {0, 0, 0, kSize, kSize / 2, 1};
        g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        g->Submit();
        CHECK(rb->Map(0, &whole, (void **)&r));
        for (UINT64 k = 0; k < (UINT64)kSize * kSize / 2 * 8; k++)
            texture_bytes += r[k] != 0;
        rb->Unmap(0, &none);
        t.texture->Release();
        targets_made--;
    }
    printf("hazard zeroed %llu %llu\n", buffer_bytes, texture_bytes);
}

// M4 (GPU overlap spec §3.6): T0 cleared to 2, then 4 additive draws into its left half (scissor): one Metal render
// pass, its clear the load action. T1 cleared to 5, a barrier (on T2), then the same draws: the clear stays a pass.
// Prints T0 left, T0 right, T1 left, T1 right: 6 2 9 5.
static void Fold() {
    const float two[4] = {2, 2, 2, 2}, five[4] = {5, 5, 5, 5};
    D3D12_RECT left = {0, 0, (LONG)kSize / 2, (LONG)kSize};
    g->list->ClearRenderTargetView(T[0].rtv, two, 0, nullptr);
    Bind(T[0], add, 1);
    g->list->RSSetScissorRects(1, &left);
    g->list->DrawInstanced(3, 4, 0, 0);
    g->list->ClearRenderTargetView(T[1].rtv, five, 0, nullptr);
    g->Barrier(T[2].texture, RT, PSR);
    Bind(T[1], add, 1);
    g->list->RSSetScissorRects(1, &left);
    g->list->DrawInstanced(3, 4, 0, 0);
    g->Barrier(T[2].texture, PSR, RT);
    Read(T[0], RT, 100, 512, 0);
    Read(T[0], RT, 900, 512, 1);
    Read(T[1], RT, 100, 512, 2);
    Read(T[1], RT, 900, 512, 3);
    g->Submit();
    printf("hazard fold %g %g %g %g\n", Texel(0), Texel(1), Texel(2), Texel(3));
}

// M4: a two-slice texture cleared to 2 through a view of both slices, then slice 1 to 7 through its own view, then
// drawn into through the first view (slice 0, adding 1 four times). The later clear must stay after the earlier one,
// which folds into the pass. Prints slice 0, slice 1: 6 7.
static void FoldOrder() {
    ID3D12Resource *tex = g->Texture(Tex2D(kSize, kSize, kFormat, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 2), RT);
    UINT rs = g->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    Target both = {tex, rtv_heap->GetCPUDescriptorHandleForHeapStart(), {}};
    both.rtv.ptr += targets_made++ * rs;
    D3D12_CPU_DESCRIPTOR_HANDLE one = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    one.ptr += targets_made++ * rs;
    D3D12_RENDER_TARGET_VIEW_DESC vd = {kFormat, D3D12_RTV_DIMENSION_TEXTURE2DARRAY};
    vd.Texture2DArray.ArraySize = 2;
    g->device->CreateRenderTargetView(tex, &vd, both.rtv);
    vd.Texture2DArray.FirstArraySlice = 1;
    vd.Texture2DArray.ArraySize = 1;
    g->device->CreateRenderTargetView(tex, &vd, one);
    const float two[4] = {2, 2, 2, 2}, seven[4] = {7, 7, 7, 7};
    g->list->ClearRenderTargetView(both.rtv, two, 0, nullptr);
    g->list->ClearRenderTargetView(one, seven, 0, nullptr);
    Pass(both, add, 1, 4);
    g->Barrier(tex, RT, COPY_SOURCE);
    for (UINT slice = 0; slice < 2; slice++) {
        D3D12_TEXTURE_COPY_LOCATION src = {tex, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        src.SubresourceIndex = slice;
        D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = {(UINT64)slice * 512, {kFormat, 1, 1, 1, 256}};
        D3D12_BOX box = {512, 512, 0, 513, 513, 1};
        g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }
    g->Submit();
    printf("hazard fold-order %g %g\n", Texel(0), Texel(1));
}

// M5 (GPU overlap spec §3.11): fences between queues. A second queue with its own list (open), and helpers.
struct Queue2 {
    ID3D12CommandQueue *q;
    ID3D12CommandAllocator *a;
    ID3D12GraphicsCommandList *l;
};
static Queue2 MakeQueue() {
    Queue2 b;
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
    CHECK(g->device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&b.q));
    CHECK(g->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&b.a));
    CHECK(g->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, b.a, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                       (void **)&b.l));
    return b;
}
static ID3D12Fence *MakeFence() {
    ID3D12Fence *f;
    CHECK(g->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&f));
    return f;
}
static void Run(ID3D12CommandQueue *q, ID3D12GraphicsCommandList *l) {
    CHECK(l->Close());
    ID3D12CommandList *one[] = {l};
    q->ExecuteCommandLists(1, one);
}
// Waits on the CPU for `f` to reach `v`, `ms` at most.
static bool CpuWait(ID3D12Fence *f, UINT64 v, DWORD ms = 10000) {
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CHECK(f->SetEventOnCompletion(v, ev));
    bool ok = WaitForSingleObject(ev, ms) == WAIT_OBJECT_0;
    CloseHandle(ev);
    return ok;
}
// After the main queue's earlier work: its list and allocator reopened.
static void Drain() {
    CHECK(g->queue->Signal(g->fence, ++g->value));
    if (!CpuWait(g->fence, g->value)) { printf("hazard drain timeout\n"); exit(1); }
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
}
static ID3D12QueryHeap *TimestampHeap() {
    static ID3D12QueryHeap *heap;
    if (!heap) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    }
    return heap;
}

// The main queue owes a timestamp (resolved on the CPU), so its Signal(F, 5) reaches F's CPU-visible value only once
// its heavy pass completes; meanwhile queue B waits for F = 5, then signals F = 6. D3D12: B runs after the main
// queue's signal, and F reaches 6. (Before M5 the late signal of 5 replaced F's event under B's wait: a deadlock.)
// Prints whether F reached 6 within 10 s: 1.
static void FenceReset() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence();
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    g->list->EndQuery(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list->ResolveQueryData(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, readback, 63 * 512);
    Run(g->queue, g->list);
    CHECK(g->queue->Signal(f, 5));
    CHECK(b.q->Wait(f, 5));
    CHECK(b.q->Signal(f, 6));
    bool ok = CpuWait(f, 6);
    printf("hazard fence-reset %d\n", ok ? 1 : 0);
    fflush(stdout);
    if (!ok)
        exit(1);
    Drain();
}

// The main queue waits for G (which the CPU signals last) before signaling F; queue B waits for F, adds 1 to T1 and
// signals H. The CPU signals F itself, waits for H, then signals G. D3D12 lets B through on the CPU's signal of F.
// Prints T1 (-1 on a timeout): 1.
static void FenceCpuLate() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *gate = MakeFence(), *h = MakeFence();
    CHECK(g->queue->Wait(gate, 1));
    CHECK(g->queue->Signal(f, 1));
    auto *main_list = g->list;
    g->list = b.l;
    Clear(T[1]);
    Pass(T[1], add, 1, 1);
    Read(T[1], RT, 512, 512, 0);
    g->list = main_list;
    CHECK(b.q->Wait(f, 1));
    Run(b.q, b.l);
    CHECK(b.q->Signal(h, 1));
    CHECK(f->Signal(1));
    bool ok = CpuWait(h, 1);
    CHECK(gate->Signal(1));
    printf("hazard fence-cpu-late %g\n", ok ? Texel(0) : -1.0f);
    fflush(stdout);
    if (!ok)
        exit(1);
    CHECK(g->queue->Signal(g->fence, ++g->value)); // the main queue's list stays open: no Drain
    if (!CpuWait(g->fence, g->value)) { printf("hazard drain timeout\n"); exit(1); }
}

// The main queue resolves a timestamp (on the CPU) and signals F; queue B waits for F and signals G. Once the CPU sees
// G, it sees F, and the timestamp as it ends up (D3D12: G's signal comes after F's). 50 rounds; prints the rounds where
// F wasn't reached, then those where the timestamp changed after: 0 0.
static void FenceTransitive() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *gg = MakeFence();
    unsigned early_fence = 0, early_stamp = 0;
    D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
    auto stamp = [&]() {
        uint8_t *p;
        UINT64 v;
        CHECK(readback->Map(0, &whole, (void **)&p));
        memcpy(&v, p + 62 * 512, 8);
        readback->Unmap(0, &none);
        return v;
    };
    for (UINT64 r = 1; r <= 50; r++) {
        uint8_t *p;
        CHECK(readback->Map(0, &whole, (void **)&p));
        memset(p + 62 * 512, 0xFF, 8);
        readback->Unmap(0, &whole);
        g->list->EndQuery(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        g->list->ResolveQueryData(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, readback, 62 * 512);
        Run(g->queue, g->list);
        CHECK(g->queue->Signal(f, r));
        CHECK(b.q->Wait(f, r));
        CHECK(b.q->Signal(gg, r));
        if (!CpuWait(gg, r)) { printf("hazard fence-transitive timeout\n"); exit(1); }
        UINT64 seen = f->GetCompletedValue(), at_g = stamp();
        Drain();
        early_fence += seen < r;
        early_stamp += at_g != stamp();
    }
    printf("hazard fence-transitive %u %u\n", early_fence, early_stamp);
}

// The main queue resolves a timestamp into a custom (CPU write-back, GPU-readable) heap and signals F; queue B waits
// for F and copies that buffer into the readback buffer. B copies the timestamp as written. Prints whether B's copy
// equals the custom buffer's final contents (-1 on a timeout): 1.
static void FenceCustom() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *d = MakeFence();
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_CUSTOM, D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0};
    D3D12_RESOURCE_DESC bd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, 256, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                              D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource *custom;
    CHECK(g->device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                             (void **)&custom));
    uint8_t *p;
    D3D12_RANGE whole = {0, 256}, none = {0, 0};
    CHECK(custom->Map(0, &none, (void **)&p));
    memset(p, 0, 256);
    custom->Unmap(0, &whole);
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    g->list->EndQuery(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list->ResolveQueryData(TimestampHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, custom, 0);
    Run(g->queue, g->list);
    CHECK(g->queue->Signal(f, 1));
    D3D12_RESOURCE_BARRIER rb = {D3D12_RESOURCE_BARRIER_TYPE_TRANSITION};
    rb.Transition = {custom, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, COPY_DEST, COPY_SOURCE};
    b.l->ResourceBarrier(1, &rb);
    b.l->CopyBufferRegion(readback, 61 * 512, custom, 0, 8);
    CHECK(b.q->Wait(f, 1));
    Run(b.q, b.l);
    CHECK(b.q->Signal(d, 1));
    bool ok = CpuWait(d, 1);
    UINT64 copied = 0, written = 0;
    D3D12_RANGE rr = {0, 64 * 512};
    CHECK(readback->Map(0, &rr, (void **)&p));
    memcpy(&copied, p + 61 * 512, 8);
    readback->Unmap(0, &none);
    Drain();
    CHECK(custom->Map(0, &whole, (void **)&p));
    memcpy(&written, p, 8);
    custom->Unmap(0, &none);
    printf("hazard fence-custom %d\n", ok ? (int)(copied == written) : -1);
}

// The CPU signals F = 10, then lowers it to 1; queue B waits for F = 2, then signals D. 50 ms later D is still 0;
// after the CPU signals F = 2, D reaches 1. Prints D before and after: 0 1.
static void FenceLower() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *d = MakeFence();
    CHECK(f->Signal(10));
    CHECK(f->Signal(1));
    CHECK(b.q->Wait(f, 2));
    CHECK(b.q->Signal(d, 1));
    Sleep(50);
    UINT64 before = d->GetCompletedValue();
    CHECK(f->Signal(2));
    bool ok = CpuWait(d, 1);
    printf("hazard fence-lower %llu %d\n", (unsigned long long)before, ok ? 1 : 0);
}

// Queue B's wait for F and its pass sampling T0 into T1 are submitted before the main queue renders T0 (heavy) and
// signals F: B samples the finished T0. Prints T1 (-1 on a timeout): 256.
static void FenceWaitFirst() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *d = MakeFence();
    auto *main_list = g->list;
    g->list = b.l;
    g->Barrier(T[0].texture, RT, PSR);
    Pass(T[1], sample, 1, 1, &T[0]);
    g->Barrier(T[0].texture, PSR, RT);
    Read(T[1], RT, 512, 512, 0);
    g->list = main_list;
    CHECK(b.q->Wait(f, 1));
    Run(b.q, b.l);
    CHECK(b.q->Signal(d, 1));
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    Run(g->queue, g->list);
    CHECK(g->queue->Signal(f, 1));
    bool ok = CpuWait(d, 1);
    printf("hazard fence-wait-first %g\n", ok ? Texel(0) : -1.0f);
    fflush(stdout);
    if (!ok)
        exit(1);
    Drain();
}

// Values asked out of the order they run in, rising all the same: queue B waits for F = 1 and then signals F = 2, and
// the CPU signals F = 1 after; then, on fence E, queue B waits for 5 and signals 6, and the main queue signals 5 after.
// D3D12 runs each to its higher value. Prints whether F reached 2, then E 6, within 10 s each: 1 1.
static void FenceOrder() {
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence(), *e = MakeFence();
    CHECK(b.q->Wait(f, 1));
    CHECK(b.q->Signal(f, 2));
    CHECK(f->Signal(1));
    bool cpu = CpuWait(f, 2);
    CHECK(b.q->Wait(e, 5));
    CHECK(b.q->Signal(e, 6));
    CHECK(g->queue->Signal(e, 5));
    bool queue = cpu && CpuWait(e, 6);
    printf("hazard fence-order %d %d\n", cpu ? 1 : 0, queue ? 1 : 0);
    fflush(stdout);
    if (!queue)
        exit(1);
}

// GPU efficiency spec E2 (prerequisite): a render pass sampled from two timestamp heaps at its end. Metal writes only
// one counter buffer per encoder on Apple GPUs, so each further heap's sample needs its own encoder. Two frames: each
// heap's timestamp nonzero, the second frame's later than the first. Prints heap A ok, heap B ok: 1 1.
static void TwoHeaps() {
    ID3D12QueryHeap *heaps[2];
    for (auto &h : heaps) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&h));
    }
    UINT64 first[2] = {}, second[2] = {};
    for (int frame = 0; frame < 2; frame++) {
        uint8_t *p;
        D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
        CHECK(readback->Map(0, &whole, (void **)&p));
        memset(p + 60 * 512, 0, 16);
        readback->Unmap(0, &whole);
        Clear(T[0]);
        Pass(T[0], add, 1, 16);
        g->list->EndQuery(heaps[0], D3D12_QUERY_TYPE_TIMESTAMP, 0);
        g->list->EndQuery(heaps[1], D3D12_QUERY_TYPE_TIMESTAMP, 0);
        g->list->DrawInstanced(3, 1, 0, 0);
        for (int h = 0; h < 2; h++)
            g->list->ResolveQueryData(heaps[h], D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, readback, 60 * 512 + h * 8);
        g->Submit();
        CHECK(readback->Map(0, &whole, (void **)&p));
        memcpy(frame ? second : first, p + 60 * 512, 16);
        readback->Unmap(0, &none);
    }
    printf("hazard two-heaps %d %d\n", first[0] && second[0] > first[0], first[1] && second[1] > first[1]);
}

// Task F2 item 0: timestamps sampled on one queue, resolved on another behind a fence (queue B samples nothing, so our
// DXMT gives it no counter ring): B's resolve reads the values A's queue wrote. Prints 1 when both are nonzero and in
// order (D3DMetal: 0, its timestamps are zero).
static void TimestampQueues() {
    static ID3D12QueryHeap *heap;
    if (!heap) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    }
    Queue2 b = MakeQueue();
    ID3D12Fence *f = MakeFence();
    uint8_t *p;
    D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
    CHECK(readback->Map(0, &whole, (void **)&p));
    memset(p + 60 * 512, 0, 16);
    readback->Unmap(0, &whole);
    Clear(T[0]);
    g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
    Pass(T[0], add, 1, 16);
    g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
    Run(g->queue, g->list);
    CHECK(g->queue->Signal(f, 1));
    CHECK(b.q->Wait(f, 1));
    b.l->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback, 60 * 512);
    Run(b.q, b.l);
    CHECK(b.q->Signal(f, 2));
    if (!CpuWait(f, 2)) { printf("hazard ts-queues timeout\n"); exit(1); }
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    UINT64 t[2];
    CHECK(readback->Map(0, &whole, (void **)&p));
    memcpy(t, p + 60 * 512, 16);
    readback->Unmap(0, &none);
    printf("hazard ts-queues %d\n", t[0] && t[1] >= t[0]);
    b.l->Release(); b.a->Release(); b.q->Release(); f->Release();
}

// GPU efficiency spec E4: a lone timestamp (a list's first; a list of timestamps alone) is taken at the start of the
// next encoder the queue encodes, not by a blit of its own (DXMT_STATS: 2 here). One call, three lists:
// [t0, pass on T0, t1], [t2, t3], [t4, pass on T1, t5]. Prints 1 when all six resolve nonzero and in order.
static void TimestampStart() {
    static ID3D12QueryHeap *heap;
    if (!heap) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 6};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    }
    Queue2 b = MakeQueue(), c = MakeQueue(); // their lists only: all three lists run on the main queue
    auto *main_list = g->list;
    auto ts = [&](UINT i) { g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, i); };
    ts(0);
    Clear(T[0]);
    Pass(T[0], add, 1, 4);
    ts(1);
    g->list = b.l;
    ts(2);
    ts(3);
    g->list = c.l;
    ts(4);
    Clear(T[1]);
    Pass(T[1], add, 1, 4);
    ts(5);
    g->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 6, readback, 59 * 512);
    g->list = main_list;
    ID3D12GraphicsCommandList *lists[] = {g->list, b.l, c.l};
    for (auto *l : lists)
        CHECK(l->Close());
    g->queue->ExecuteCommandLists(3, (ID3D12CommandList *const *)lists);
    CHECK(g->queue->Signal(g->fence, ++g->value));
    if (!CpuWait(g->fence, g->value)) { printf("hazard ts-start timeout\n"); exit(1); }
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    UINT64 t[6];
    uint8_t *p;
    D3D12_RANGE whole = {0, 64 * 512}, none = {0, 0};
    CHECK(readback->Map(0, &whole, (void **)&p));
    memcpy(t, p + 59 * 512, sizeof t);
    readback->Unmap(0, &none);
    bool ok = t[0] != 0;
    for (int i = 1; i < 6; i++)
        ok = ok && t[i] >= t[i - 1];
    printf("hazard ts-start %d\n", ok ? 1 : 0);
}

// Overlap order (DXMT_D3D12_OVERLAP=1): a pass sampled from two timestamp heaps puts one of the queue's own blits inside
// the list (E2); later work in the list still waits on what it depends on: T0 rendered (heavy), work on T1, then T0
// set to 7. Prints 7.
static void AfterOwnBlit() {
    static ID3D12QueryHeap *heaps[2];
    if (!heaps[0])
        for (auto &h : heaps) {
            D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1};
            CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&h));
        }
    Clear(T[2]);
    Pass(T[2], add, 1, 1);
    g->list->EndQuery(heaps[0], D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list->EndQuery(heaps[1], D3D12_QUERY_TYPE_TIMESTAMP, 0);
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    Pass(T[1], set, 5, 1);
    Pass(T[0], set, 7, 1);
    Read(T[0], RT, 512, 512, 0);
    g->Submit();
    printf("hazard after-own-blit %g\n", Texel(0));
}

// Task F4 (Ruling 23): 203 timestamps from two query heaps in one list, as Unreal's GPU profiler takes them: one at the
// list's start, then 33 times a render pass, a dispatch and a copy, each followed by one timestamp per heap (and a
// UAV barrier, so the order holds with DXMT_D3D12_OVERLAP=1 too), and two at the list's end. The 17th dispatch is heavy
// (csfill, 65535 groups), and the same dispatch again right after its iteration, between two timestamps of its own,
// gives its time (the GPU's clock changes over a run, so it is measured next to it). Every value must be nonzero and
// in submission order, and the gap around the heavy dispatch at least half that time. Prints "hazard ts-many 1", then
// "ts-many values <count> heavy <gap> alone <time>" (ticks).
static void TimestampMany() {
    static ID3D12QueryHeap *heaps[2];
    static ID3D12Resource *b, *src, *dst, *values;
    if (!heaps[0]) {
        for (auto &h : heaps) {
            D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 128};
            CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&h));
        }
        b = Zeroed(256, UA);
        src = Zeroed(256, COPY_SOURCE);
        dst = Zeroed(256, COPY_DEST);
        values = g->Buffer(D3D12_HEAP_TYPE_READBACK, 2 * 128 * 8, COPY_DEST);
    }
    std::vector<std::pair<int, UINT>> order; // (heap, query) in submission order
    UINT next[2] = {};
    auto ts = [&](int h) {
        g->list->EndQuery(heaps[h], D3D12_QUERY_TYPE_TIMESTAMP, next[h]);
        order.push_back({h, next[h]++});
    };
    auto barrier = [] {
        D3D12_RESOURCE_BARRIER all = {D3D12_RESOURCE_BARRIER_TYPE_UAV};
        g->list->ResourceBarrier(1, &all);
    };
    auto resolve = [&] {
        for (int h = 0; h < 2; h++)
            if (next[h])
                g->list->ResolveQueryData(heaps[h], D3D12_QUERY_TYPE_TIMESTAMP, 0, next[h], values, h * 128 * 8);
        g->Submit();
    };
    auto value = [&](std::pair<int, UINT> q) {
        uint8_t *p;
        D3D12_RANGE whole = {0, 2 * 128 * 8}, none = {0, 0};
        CHECK(values->Map(0, &whole, (void **)&p));
        UINT64 v;
        memcpy(&v, p + (q.first * 128 + q.second) * 8, 8);
        values->Unmap(0, &none);
        return v;
    };
    size_t heavy_before = 0, alone_before = 0;
    ts(0);
    for (int i = 0; i < 33; i++) {
        Pass(T[0], add, 1, 1);
        ts(i % 2);
        ts(1 - i % 2);
        barrier();
        if (i == 16)
            heavy_before = order.size() - 1;
        Dispatch(i == 16 ? fill : count, b, i == 16 ? 65535 : 1);
        ts(0);
        ts(1);
        barrier();
        g->list->CopyBufferRegion(dst, 0, src, 0, 256);
        ts(1);
        ts(0);
        barrier();
        if (i == 16) {
            alone_before = order.size();
            ts(0);
            Dispatch(fill, b, 65535);
            ts(1);
            barrier();
        }
    }
    ts(0);
    ts(1);
    resolve();
    bool ok = true;
    UINT64 last = 0;
    for (auto q : order) {
        UINT64 v = value(q);
        ok = ok && v && v >= last;
        last = v;
    }
    UINT64 heavy = value(order[heavy_before + 1]) - value(order[heavy_before]);
    UINT64 alone = value(order[alone_before + 1]) - value(order[alone_before]);
    ok = ok && alone && heavy * 2 >= alone;
    printf("hazard ts-many %d\n", ok ? 1 : 0);
    printf("ts-many values %zu heavy %llu alone %llu\n", order.size(), (unsigned long long)heavy,
           (unsigned long long)alone);
}

// GPU efficiency spec E7: a clear-only pass folds into the first pass that binds its view later in the same
// ExecuteCommandLists call, across lists, timestamps and barriers that don't name its texture. Lists on the main queue.
struct List {
    ID3D12CommandAllocator *a;
    ID3D12GraphicsCommandList *l;
};
static List MakeList() {
    List x;
    CHECK(g->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&x.a));
    CHECK(g->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, x.a, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                       (void **)&x.l));
    return x;
}
// Runs `lists` (closed) in one call on the main queue and waits.
static void RunLists(std::initializer_list<ID3D12GraphicsCommandList *> lists) {
    std::vector<ID3D12CommandList *> v(lists.begin(), lists.end());
    g->queue->ExecuteCommandLists((UINT)v.size(), v.data());
    CHECK(g->queue->Signal(g->fence, ++g->value));
    if (!CpuWait(g->fence, g->value)) { printf("hazard fold timeout\n"); exit(1); }
}
static ID3D12QueryHeap *FoldHeap() {
    static ID3D12QueryHeap *heap;
    if (!heap) {
        D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4};
        CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    }
    return heap;
}
static void ClearTo(Target &t, float v) {
    const float c[4] = {v, v, v, v};
    g->list->ClearRenderTargetView(t.rtv, c, 0, nullptr);
}
// 4 additive draws of 1 into the left half of `t` (scissor).
static void LeftHalf(Target &t) {
    Bind(t, add, 1);
    D3D12_RECT left = {0, 0, (LONG)kSize / 2, (LONG)kSize};
    g->list->RSSetScissorRects(1, &left);
    g->list->DrawInstanced(3, 4, 0, 0);
}
// List 1: clear T0 to 2, a timestamp. List 2: a barrier round trip on `barrier_on` (no encoder). List 3: a timestamp,
// then 4 draws of +1 into T0's left half. Returns T0 left, T0 right.
static std::pair<float, float> FoldLists(Target &barrier_on) {
    auto *main_list = g->list;
    List b = MakeList(), c = MakeList();
    ClearTo(T[0], 2);
    g->list->EndQuery(FoldHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list = b.l;
    g->Barrier(barrier_on.texture, RT, PSR);
    g->Barrier(barrier_on.texture, PSR, RT);
    g->list = c.l;
    g->list->EndQuery(FoldHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    LeftHalf(T[0]);
    Read(T[0], RT, 100, 512, 0);
    Read(T[0], RT, 900, 512, 1);
    g->list = main_list;
    for (auto *l : {main_list, b.l, c.l})
        CHECK(l->Close());
    RunLists({main_list, b.l, c.l});
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    return {Texel(0), Texel(1)};
}
// The fold across lists: prints 6 2 (DXMT_STATS: 1 clear folded at execute).
static void FoldListsMode() {
    auto [l, r] = FoldLists(T[2]);
    printf("hazard fold-lists %g %g\n", l, r);
}
// A barrier naming the cleared texture between: no fold, same pixels (DXMT_STATS: 1 refused, barrier).
static void FoldListsBarrier() {
    auto [l, r] = FoldLists(T[0]);
    printf("hazard fold-lists-barrier %g %g\n", l, r);
}
// List 1: clear T0 to 2, a timestamp. List 2: clear T0 to 7 (M4 folds it into the pass), +1 x4 into the left half.
// The later clear wins: prints 11 7.
static void FoldM4() {
    auto *main_list = g->list;
    List b = MakeList();
    ClearTo(T[0], 2);
    g->list->EndQuery(FoldHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list = b.l;
    ClearTo(T[0], 7);
    LeftHalf(T[0]);
    Read(T[0], RT, 100, 512, 0);
    Read(T[0], RT, 900, 512, 1);
    g->list = main_list;
    for (auto *l : {main_list, b.l})
        CHECK(l->Close());
    RunLists({main_list, b.l});
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    printf("hazard fold-m4 %g %g\n", Texel(0), Texel(1));
}
// List A: clear T0 to 2, a timestamp. List B: +1 x4 over all of T0. One call runs {A, B, B}: 10; a second call runs
// {B}: 14. The fold is for that execution of A's clear only. Prints 10 14.
static void FoldTwice() {
    auto *main_list = g->list;
    List b = MakeList(), r = MakeList();
    ClearTo(T[0], 2);
    g->list->EndQuery(FoldHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list = b.l;
    Pass(T[0], add, 1, 4);
    g->list = r.l;
    Read(T[0], RT, 512, 512, 0);
    g->list = main_list;
    for (auto *l : {main_list, b.l, r.l})
        CHECK(l->Close());
    RunLists({main_list, b.l, b.l, r.l});
    float first = Texel(0);
    RunLists({b.l, r.l});
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    printf("hazard fold-twice %g %g\n", first, Texel(0));
}
// List 2 copies a texel of T0 out (barriers around it name T0) before list 3's draws: the copy reads 2, the clear
// stays a pass. Prints 2 6 2.
static void FoldCopy() {
    auto *main_list = g->list;
    List b = MakeList(), c = MakeList();
    ClearTo(T[0], 2);
    g->list->EndQuery(FoldHeap(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    g->list = b.l;
    Read(T[0], RT, 900, 512, 2);
    g->list = c.l;
    LeftHalf(T[0]);
    Read(T[0], RT, 100, 512, 0);
    Read(T[0], RT, 900, 512, 1);
    g->list = main_list;
    for (auto *l : {main_list, b.l, c.l})
        CHECK(l->Close());
    RunLists({main_list, b.l, c.l});
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    printf("hazard fold-copy %g %g %g\n", Texel(2), Texel(0), Texel(1));
}

// GPU efficiency E10: indirect draws read their arguments in the render pass. Its arguments {3, 1, 0, 0} (csargs).
// T0 gets 256 (heavy); a dispatch elsewhere ends that pass; a second pass into T0 (which waits on the first, as both
// write T0) adds 9 by the indirect draw; a barrier INDIRECT_ARGUMENT -> UNORDERED_ACCESS; cscount then makes the
// instance count 3. Only that barrier orders cscount after the second pass: 265 (283 if cscount ran first).
static void IndirectWar() {
    ID3D12Resource *a = Zeroed(16, UA), *elsewhere = Zeroed(256, UA);
    Dispatch(args, a, 1);
    g->Barrier(a, UA, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Clear(T[0]);
    Pass(T[0], add, 1, 256);
    Dispatch(fill, elsewhere, 1);
    Bind(T[0], add, 9);
    g->list->ExecuteIndirect(draw_signature, 1, a, 0, nullptr, 0);
    g->Barrier(a, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, UA);
    Dispatch(count, a, 1);
    Read(T[0], RT, 512, 512, 0);
    g->Submit();
    printf("hazard indirect-war %g\n", Texel(0));
}
// List 1: clear T0 to 2, an indirect draw of 4 instances adding 1. List 2: the same draw. With no resolver pass before
// them, the two passes are one Metal render pass (DXMT_STATS: 1 render pass merged). Prints 10.
static void MergeIndirect() {
    static const D3D12_DRAW_ARGUMENTS four = {3, 4, 0, 0};
    ID3D12Resource *a = g->Buffer(D3D12_HEAP_TYPE_UPLOAD, sizeof(four), D3D12_RESOURCE_STATE_GENERIC_READ);
    void *p;
    CHECK(a->Map(0, nullptr, &p));
    memcpy(p, &four, sizeof(four));
    a->Unmap(0, nullptr);
    auto *main_list = g->list;
    List b = MakeList();
    ClearTo(T[0], 2);
    Bind(T[0], add, 1);
    g->list->ExecuteIndirect(draw_signature, 1, a, 0, nullptr, 0);
    g->list = b.l;
    Bind(T[0], add, 1);
    g->list->ExecuteIndirect(draw_signature, 1, a, 0, nullptr, 0);
    Read(T[0], RT, 512, 512, 0);
    g->list = main_list;
    for (auto *l : {main_list, b.l})
        CHECK(l->Close());
    RunLists({main_list, b.l});
    CHECK(g->allocator->Reset());
    CHECK(g->list->Reset(g->allocator, nullptr));
    printf("hazard merge-indirect %g\n", Texel(0));
}

int main(int argc, char **argv) {
    if (argc < 2) { printf("usage: d3d12_hazards.exe <shader folder> [mode...]\n"); return 2; }
    std::string dir = argv[1];
    auto shader = [&](const char *entry) {
        auto code = Load((dir + "/hazards." + entry + ".dxil").c_str());
        if (code.empty()) { printf("can't read hazards.%s.dxil\n", entry); exit(1); }
        return code;
    };
    Gpu gpu;
    g = &gpu;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 2;
    D3D12_DESCRIPTOR_RANGE srvs = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {1, &srvs};
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    root = gpu.RootSignature(rd);
    params[1] = {};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    croot = gpu.RootSignature(rd);
    auto vs = shader("vsfull"), value = shader("psvalue"), sampled = shader("pssample");
    add = Graphics(vs, value, true);
    set = Graphics(vs, value, false);
    sample = Graphics(vs, sampled, false);
    add_sample = Graphics(vs, sampled, true);
    fill = Compute(shader("csfill"));
    count = Compute(shader("cscount"));
    args = Compute(shader("csargs"));
    D3D12_INDIRECT_ARGUMENT_DESC arg = {D3D12_INDIRECT_ARGUMENT_TYPE_DRAW};
    D3D12_COMMAND_SIGNATURE_DESC sd = {sizeof(D3D12_DRAW_ARGUMENTS), 1, &arg, 0};
    CHECK(gpu.device->CreateCommandSignature(&sd, nullptr, __uuidof(ID3D12CommandSignature), (void **)&draw_signature));
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16};
    CHECK(gpu.device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECK(gpu.device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void **)&srv_heap));
    readback = gpu.Buffer(D3D12_HEAP_TYPE_READBACK, 64 * 512, COPY_DEST);
    for (auto &t : T)
        t = MakeTarget();
    static const struct { const char *name; void (*run)(); } kModes[] = {
        {"rt-read", RtRead},         {"same-target", SameTarget}, {"uav", Uav},       {"copy-read", CopyRead},
        {"indirect", Indirect},      {"aliasing", Aliasing},      {"occlusion", Occlusion},
        {"independent", Independent}, {"precise", Precise},       {"mid-pass", MidPass}, {"twice", Twice},
        {"many", Many},              {"clear-rects", ClearRects}, {"signal", Signal},
        {"wrap", Wrap}, {"onewait", OneWait}, {"newest", Newest}, {"nodraw", NoDraw}, {"queues", Queues}, {"unsplit", UnsplitPlain}, {"unsplit-barrier", UnsplitBarrier},
        {"unsplit-samebuffer", UnsplitSameBuffer}, {"unsplit-midbarrier", UnsplitMidBarrier},
        {"unsplit-query", UnsplitQuery}, {"unsplit-segments", UnsplitSegments}, {"unsplit-twice", UnsplitTwice}, {"deferred", Deferred}, {"zeroed", Zeroed}, {"fold", Fold}, {"fold-order", FoldOrder}, {"fence-reset", FenceReset}, {"fence-cpu-late", FenceCpuLate},
        {"fence-transitive", FenceTransitive}, {"fence-custom", FenceCustom}, {"fence-lower", FenceLower},
        {"fence-wait-first", FenceWaitFirst}, {"fence-order", FenceOrder}, {"two-heaps", TwoHeaps}, {"ts-start", TimestampStart}, {"after-own-blit", AfterOwnBlit}, {"fold-lists", FoldListsMode},
        {"fold-lists-barrier", FoldListsBarrier}, {"fold-m4", FoldM4}, {"fold-twice", FoldTwice}, {"fold-copy", FoldCopy},
        {"indirect-war", IndirectWar}, {"merge-indirect", MergeIndirect}, {"placed-uav", PlacedUav},
        {"heap-released", HeapReleased}, {"ts-many", TimestampMany}, {"sampled", Sampled}, {"ts-queues", TimestampQueues}};
    std::vector<std::string> modes(argv + 2, argv + argc);
    if (modes.empty())
        for (auto &m : kModes)
            modes.push_back(m.name);
    for (auto &m : modes) {
        bool found = false;
        for (auto &k : kModes)
            if (m == k.name) {
                k.run();
                found = true;
            }
        if (!found) { printf("unknown mode %s\n", m.c_str()); return 2; }
    }
    return 0;
}
