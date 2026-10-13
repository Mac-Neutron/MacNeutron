// GPU efficiency spec E1: textures created with Apple's lossless compression. Prints
//   "compress clear <us>"            GPU microseconds per clear-only pass of a 3840x2160 RGBA16F target, from timestamps:
//                                    check.sh wants it 3x faster than with DXMT_D3D12_COMPRESSION=0 (a pass also costs
//                                    about 20 us that compression doesn't touch: the larger target keeps the ratio clear)
//   "compress views <a> <b> <c>"     a typeless RGBA8 target cleared to 0.5 through its sRGB view (a) and its UNORM view
//                                    (b), then copied into an R8G8B8A8_UINT texture (c): red bytes 188 128 128
//   "compress placed <a> <b> <c> <d>" two such targets placed back to back in one heap at GetResourceAllocationInfo's
//                                    offsets, cleared to 1 and 2, read at two corners each: 1 1 2 2
//   "compress uav-clear <us>"        as "compress clear", the target a UAV too: check.sh wants it 3x faster than with
//                                    DXMT_D3D12_COMPRESSION=rt (UAVs uncompressed)
//   "compress uav <f> <u> <p> <r>"   raw texels of render targets that are UAVs too, after UAV clears: RGBA16F cleared
//                                    as floats (f), RGBA16 UNORM as uints (u), R11G11B10F as uints (p: DXMT writes its
//                                    packed bits through an R32 view), then the RGBA16F target cleared as a render
//                                    target (r). check.sh wants them as with DXMT_D3D12_COMPRESSION=0.
// check.sh compares the views and placed lines with D3DMetal's.
#include "d3d12_common.hpp"
#include <algorithm>
#include <cmath>

static const UINT kW = 2560, kH = 1440;

static Gpu *g;
static ID3D12Resource *readback; // 16 slots of 256 bytes
static ID3D12DescriptorHeap *rtv_heap, *uav_gpu, *uav_cpu;
static UINT rtvs, uavs;

static D3D12_CPU_DESCRIPTOR_HANDLE Rtv(ID3D12Resource *r, DXGI_FORMAT format) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += rtvs++ * g->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_RENDER_TARGET_VIEW_DESC vd = {format, D3D12_RTV_DIMENSION_TEXTURE2D};
    g->device->CreateRenderTargetView(r, &vd, h);
    return h;
}

// One texel of `r` (in `state`) into readback slot `slot`.
static void Read(ID3D12Resource *r, D3D12_RESOURCE_STATES state, DXGI_FORMAT format, UINT x, UINT y, UINT slot) {
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        g->Barrier(r, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {r, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = {(UINT64)slot * 256, {format, 1, 1, 1, 256}};
    D3D12_BOX box = {x, y, 0, x + 1, y + 1, 1};
    g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE)
        g->Barrier(r, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
}

static const uint8_t *Slots() {
    static uint8_t copy[16 * 256];
    void *p;
    D3D12_RANGE whole = {0, sizeof copy}, none = {0, 0};
    CHECK(readback->Map(0, &whole, &p));
    memcpy(copy, p, sizeof copy);
    readback->Unmap(0, &none);
    return copy;
}

static float Half(uint16_t h) {
    int e = h >> 10 & 31, m = h & 1023;
    return e ? ldexpf(1024.0f + m, e - 25) : ldexpf((float)m, -24);
}

// A UAV of `r` (in UNORDERED_ACCESS) cleared to `u` (uint) or else `f` (float).
static void ClearUav(ID3D12Resource *r, DXGI_FORMAT format, const UINT *u, const float *f) {
    UINT inc = g->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = uav_cpu->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE shown = uav_gpu->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = uav_gpu->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += uavs * inc;
    shown.ptr += uavs * inc;
    gpu.ptr += uavs++ * inc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC vd = {format, D3D12_UAV_DIMENSION_TEXTURE2D};
    g->device->CreateUnorderedAccessView(r, nullptr, &vd, cpu);
    g->device->CreateUnorderedAccessView(r, nullptr, &vd, shown);
    g->list->SetDescriptorHeaps(1, &uav_gpu);
    if (u)
        g->list->ClearUnorderedAccessViewUint(gpu, cpu, r, u, 0, nullptr);
    else
        g->list->ClearUnorderedAccessViewFloat(gpu, cpu, r, f, 0, nullptr);
}

static void Clear(const char *name, D3D12_RESOURCE_FLAGS flags) {
    auto rt = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12Resource *t = g->Texture(Tex2D(3840, 2160, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, flags), rt);
    auto h = Rtv(t, DXGI_FORMAT_R16G16B16A16_FLOAT);
    ID3D12QueryHeap *heap;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2};
    CHECK(g->device->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void **)&heap));
    const UINT n = 40;
    double best = 1e30;
    for (int round = 0; round < 5; round++) { // the fastest of 5: other GPU work only slows a round down
        g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0);
        for (UINT i = 0; i < n; i++) {
            const float v[4] = {i % 2 ? 0.25f : 0.75f, 0.5f, 0.125f, 1.0f};
            g->list->ClearRenderTargetView(h, v, 0, nullptr);
        }
        g->list->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, 1);
        g->list->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback, 15 * 256);
        g->Submit();
        UINT64 ticks[2], freq;
        memcpy(ticks, Slots() + 15 * 256, sizeof ticks);
        CHECK(g->queue->GetTimestampFrequency(&freq));
        best = std::min(best, (double)(ticks[1] - ticks[0]) / freq * 1e6 / n);
    }
    printf("compress %s %.1f\n", name, best);
}

static void Uav() {
    auto ua = D3D12_RESOURCE_STATE_UNORDERED_ACCESS, rt = D3D12_RESOURCE_STATE_RENDER_TARGET;
    auto flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *f16 = g->Texture(Tex2D(256, 256, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, flags), ua);
    ID3D12Resource *u16 = g->Texture(Tex2D(256, 256, DXGI_FORMAT_R16G16B16A16_UNORM, 1, flags), ua);
    ID3D12Resource *p11 = g->Texture(Tex2D(256, 256, DXGI_FORMAT_R11G11B10_FLOAT, 1, flags), ua);
    const float quarter[4] = {0.25f, 0.5f, 0.75f, 1.0f}, half[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    const UINT words[4] = {1000, 20000, 40000, 65535}, bits[4] = {0x5a5, 0x2d3, 0x1b6, 0};
    ClearUav(f16, DXGI_FORMAT_R16G16B16A16_FLOAT, nullptr, quarter);
    ClearUav(u16, DXGI_FORMAT_R16G16B16A16_UNORM, words, nullptr);
    ClearUav(p11, DXGI_FORMAT_R11G11B10_FLOAT, bits, nullptr);
    Read(f16, ua, DXGI_FORMAT_R16G16B16A16_FLOAT, 100, 100, 7);
    Read(u16, ua, DXGI_FORMAT_R16G16B16A16_UNORM, 100, 100, 8);
    Read(p11, ua, DXGI_FORMAT_R11G11B10_FLOAT, 100, 100, 9);
    g->Barrier(f16, ua, rt);
    g->list->ClearRenderTargetView(Rtv(f16, DXGI_FORMAT_R16G16B16A16_FLOAT), half, 0, nullptr);
    Read(f16, rt, DXGI_FORMAT_R16G16B16A16_FLOAT, 100, 100, 10);
    g->Submit();
    auto s = Slots();
    UINT64 f, u, r;
    UINT p;
    memcpy(&f, s + 7 * 256, 8);
    memcpy(&u, s + 8 * 256, 8);
    memcpy(&p, s + 9 * 256, 4);
    memcpy(&r, s + 10 * 256, 8);
    printf("compress uav %016llx %016llx %08x %016llx\n", f, u, p, r);
}

static void Views() {
    auto rt = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12Resource *t = g->Texture(Tex2D(256, 256, DXGI_FORMAT_R8G8B8A8_TYPELESS, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET), rt);
    ID3D12Resource *u = g->Texture(Tex2D(256, 256, DXGI_FORMAT_R8G8B8A8_UINT), D3D12_RESOURCE_STATE_COPY_DEST);
    const float half[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    g->list->ClearRenderTargetView(Rtv(t, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), half, 0, nullptr);
    Read(t, rt, DXGI_FORMAT_R8G8B8A8_UNORM, 100, 100, 0);
    g->list->ClearRenderTargetView(Rtv(t, DXGI_FORMAT_R8G8B8A8_UNORM), half, 0, nullptr);
    Read(t, rt, DXGI_FORMAT_R8G8B8A8_UNORM, 100, 100, 1);
    g->Barrier(t, rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    g->list->CopyResource(u, t);
    g->Barrier(t, D3D12_RESOURCE_STATE_COPY_SOURCE, rt);
    Read(u, D3D12_RESOURCE_STATE_COPY_DEST, DXGI_FORMAT_R8G8B8A8_UINT, 100, 100, 2);
    g->Submit();
    auto s = Slots();
    printf("compress views %u %u %u\n", s[0], s[256], s[512]);
}

static void Placed() {
    auto rt = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_DESC d = Tex2D(kW, kH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_RESOURCE_ALLOCATION_INFO info = g->device->GetResourceAllocationInfo(0, 1, &d);
    UINT64 second = (info.SizeInBytes + info.Alignment - 1) / info.Alignment * info.Alignment;
    D3D12_HEAP_DESC hd = {second + info.SizeInBytes, {D3D12_HEAP_TYPE_DEFAULT}, info.Alignment,
                          D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES};
    ID3D12Heap *heap;
    CHECK(g->device->CreateHeap(&hd, __uuidof(ID3D12Heap), (void **)&heap));
    ID3D12Resource *a, *b;
    CHECK(g->device->CreatePlacedResource(heap, 0, &d, rt, nullptr, __uuidof(ID3D12Resource), (void **)&a));
    CHECK(g->device->CreatePlacedResource(heap, second, &d, rt, nullptr, __uuidof(ID3D12Resource), (void **)&b));
    const float one[4] = {1, 1, 1, 1}, two[4] = {2, 2, 2, 2};
    g->list->ClearRenderTargetView(Rtv(a, DXGI_FORMAT_R16G16B16A16_FLOAT), one, 0, nullptr);
    g->list->ClearRenderTargetView(Rtv(b, DXGI_FORMAT_R16G16B16A16_FLOAT), two, 0, nullptr);
    Read(a, rt, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, 3);
    Read(a, rt, DXGI_FORMAT_R16G16B16A16_FLOAT, kW - 1, kH - 1, 4);
    Read(b, rt, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, 5);
    Read(b, rt, DXGI_FORMAT_R16G16B16A16_FLOAT, kW - 1, kH - 1, 6);
    g->Submit();
    auto s = Slots();
    uint16_t h[4];
    for (int i = 0; i < 4; i++)
        memcpy(&h[i], s + (3 + i) * 256, 2);
    printf("compress placed %g %g %g %g\n", Half(h[0]), Half(h[1]), Half(h[2]), Half(h[3]));
}

int main() {
    g = new Gpu();
    readback = g->Buffer(D3D12_HEAP_TYPE_READBACK, 16 * 256, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DESCRIPTOR_HEAP_DESC dd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16};
    CHECK(g->device->CreateDescriptorHeap(&dd, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_heap));
    D3D12_DESCRIPTOR_HEAP_DESC ud = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    CHECK(g->device->CreateDescriptorHeap(&ud, __uuidof(ID3D12DescriptorHeap), (void **)&uav_gpu));
    ud.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CHECK(g->device->CreateDescriptorHeap(&ud, __uuidof(ID3D12DescriptorHeap), (void **)&uav_cpu));
    Clear("clear", D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Clear("uav-clear", D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    Views();
    Placed();
    Uav();
    return 0;
}
