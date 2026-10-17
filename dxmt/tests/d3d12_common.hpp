// Helpers shared by the D3D12 stub tests (d3d12_api, d3d12_copy, d3d12_null, d3d12_timestamp).
#pragma once
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

inline std::vector<char> Load(const char *path) {
    std::vector<char> data;
    if (FILE *f = fopen(path, "rb")) {
        char buffer[4096]; size_t n;
        while ((n = fread(buffer, 1, sizeof buffer, f)) > 0) data.insert(data.end(), buffer, buffer + n);
        fclose(f);
    }
    return data;
}

inline D3D12_RESOURCE_DESC Tex2D(UINT w, UINT h, DXGI_FORMAT format, UINT16 mips = 1,
                                 D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE, UINT16 array = 1) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = array;
    d.MipLevels = mips; d.Format = format; d.SampleDesc.Count = 1; d.Flags = flags;
    return d;
}

struct Gpu {
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    ID3D12Fence *fence = nullptr;
    UINT64 value = 0;

    Gpu() {
        CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device));
        D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT};
        CHECK(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue));
        CHECK(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&allocator));
        CHECK(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                        __uuidof(ID3D12GraphicsCommandList), (void **)&list));
        CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence));
    }

    // Closes the list, runs it `times` times back to back, waits (10 s at most) and reopens it.
    void Submit(int times = 1) {
        CHECK(list->Close());
        ID3D12CommandList *lists[] = {list};
        for (int i = 0; i < times; i++)
            queue->ExecuteCommandLists(1, lists);
        CHECK(queue->Signal(fence, ++value));
        HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        CHECK(fence->SetEventOnCompletion(value, done));
        if (WaitForSingleObject(done, 10000) != WAIT_OBJECT_0) { printf("fail timeout\n"); exit(1); }
        CloseHandle(done);
        CHECK(allocator->Reset());
        CHECK(list->Reset(allocator, nullptr));
    }

    ID3D12Resource *Buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state,
                           D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1;
        d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags = flags;
        D3D12_HEAP_PROPERTIES heap = {type};
        ID3D12Resource *r;
        CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, __uuidof(ID3D12Resource), (void **)&r));
        return r;
    }

    ID3D12Resource *Texture(const D3D12_RESOURCE_DESC &desc, D3D12_RESOURCE_STATES state,
                            const D3D12_CLEAR_VALUE *clear = nullptr) {
        D3D12_HEAP_PROPERTIES heap = {D3D12_HEAP_TYPE_DEFAULT};
        ID3D12Resource *r;
        CHECK(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, clear, __uuidof(ID3D12Resource), (void **)&r));
        return r;
    }

    ID3D12RootSignature *RootSignature(const D3D12_ROOT_SIGNATURE_DESC &desc) {
        ID3DBlob *blob = nullptr, *error = nullptr;
        CHECK(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
        ID3D12RootSignature *root;
        CHECK(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void **)&root));
        return root;
    }

    void Barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before; b.Transition.StateAfter = after;
        list->ResourceBarrier(1, &b);
    }
};

// A root CBV b0 and a quad pipeline from shaders/depth.hlsl (vsmain, psmain) drawing to one RGBA8 target.
inline ID3D12PipelineState *QuadPipeline(Gpu &gpu, ID3D12RootSignature *root, const std::vector<char> &vs,
                                         const std::vector<char> &ps, D3D12_GRAPHICS_PIPELINE_STATE_DESC *out = nullptr) {
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
    if (out)
        *out = gd;
    ID3D12PipelineState *pso;
    CHECK(gpu.device->CreateGraphicsPipelineState(&gd, __uuidof(ID3D12PipelineState), (void **)&pso));
    return pso;
}

inline ID3D12RootSignature *CbvRootSignature(Gpu &gpu) {
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    D3D12_ROOT_SIGNATURE_DESC rd = {1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    return gpu.RootSignature(rd);
}
