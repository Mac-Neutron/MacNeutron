// Descriptor writes by destination (the frame rate, 2026-10-18: guest/proxy.cpp's descriptor wraps), a native program
// run natively and through the loopback (descriptors.sh): 64 shader resource views into a CPU-only heap, 64 copied
// one by one into a shader-visible heap and once more as ranges, 64 constant buffer views into the shader-visible
// heap, 16 samplers into each sampler heap, and one copy whose source is shader-visible. Of the 226 writes, the 81
// into CPU-only heaps or from a shader-visible one are the only ones that need to run every thread's records first.
// Prints "descriptors ..." lines.
#include "d3d12_common.hpp"

static ID3D12DescriptorHeap *Heap(Gpu &gpu, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT n, bool visible) {
    D3D12_DESCRIPTOR_HEAP_DESC d = {type, n, visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                                     : D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ID3D12DescriptorHeap *h;
    CHECK(gpu.device->CreateDescriptorHeap(&d, __uuidof(ID3D12DescriptorHeap), (void **)&h));
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE At(ID3D12DescriptorHeap *h, UINT i, UINT inc) {
    return {h->GetCPUDescriptorHandleForHeapStart().ptr + (SIZE_T)i * inc};
}

int main() {
    Gpu gpu;
    const UINT n = 64, ns = 16;
    UINT inc = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT sinc = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    ID3D12DescriptorHeap *cpu = Heap(gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, n, false);
    ID3D12DescriptorHeap *vis = Heap(gpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2 * n, true);
    ID3D12DescriptorHeap *scpu = Heap(gpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, ns, false);
    ID3D12DescriptorHeap *svis = Heap(gpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, ns, true);
    ID3D12Resource *buf = gpu.Buffer(D3D12_HEAP_TYPE_UPLOAD, 65536, D3D12_RESOURCE_STATE_GENERIC_READ);

    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Buffer.NumElements = 16, sd.Buffer.StructureByteStride = 4;
    for (UINT i = 0; i < n; i++) {
        sd.Buffer.FirstElement = i;
        gpu.device->CreateShaderResourceView(buf, &sd, At(cpu, i, inc));
    }
    for (UINT i = 0; i < n; i++)
        gpu.device->CopyDescriptorsSimple(1, At(vis, i, inc), At(cpu, i, inc), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE dst = At(vis, n, inc), src = At(cpu, 0, inc);
    UINT dn = n, sn = n;
    gpu.device->CopyDescriptors(1, &dst, &dn, 1, &src, &sn, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (UINT i = 0; i < n; i++) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cd = {buf->GetGPUVirtualAddress() + i * 256, 256};
        gpu.device->CreateConstantBufferView(&cd, At(vis, i, inc));
    }
    D3D12_SAMPLER_DESC smp = {D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                              D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP};
    smp.MaxLOD = D3D12_FLOAT32_MAX;
    for (UINT i = 0; i < ns; i++) {
        gpu.device->CreateSampler(&smp, At(scpu, i, sinc));
        gpu.device->CreateSampler(&smp, At(svis, i, sinc));
    }
    gpu.device->CopyDescriptorsSimple(1, At(vis, 0, inc), At(vis, 1, inc), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // The writes went through: a list that binds the shader-visible heaps closes and runs.
    ID3D12DescriptorHeap *heaps[] = {vis, svis};
    gpu.list->SetDescriptorHeaps(2, heaps);
    gpu.Submit();
    printf("descriptors writes %u\n", n + n + 1 + n + 2 * ns + 1);
    return 0;
}
