// Answers the guest keeps (M5 revisited, 2026-10-18: SMITE 2 asked these 30 to 66 times a frame, each a trap), a
// native program run natively and through the loopback (answers.sh): 100 each of the device's removed reason, a
// queue's timestamp frequency, the adapter's video memory and a resource's description, then a memory reservation and
// the video memory again. Of those 502 calls only the reservation and what it invalidates need the host: answers.sh
// checks the guest trace's exits. Prints "answers ..." lines.
#include "d3d12_common.hpp"
#include <dxgi1_4.h>

int main() {
    Gpu gpu;
    HRESULT removed = S_OK;
    for (int i = 0; i < 100; i++) removed |= gpu.device->GetDeviceRemovedReason();
    printf("answers removed %08lx\n", (unsigned long)(unsigned)removed);

    UINT64 first = 0, f = 0;
    CHECK(gpu.queue->GetTimestampFrequency(&first));
    int same = 1;
    for (int i = 0; i < 99; i++) same &= SUCCEEDED(gpu.queue->GetTimestampFrequency(&f)) && f == first;
    printf("answers frequency %d nonzero %d\n", same, first != 0);

    IDXGIFactory4 *factory;
    IDXGIAdapter3 *adapter;
    CHECK(CreateDXGIFactory2(0, __uuidof(IDXGIFactory4), (void **)&factory));
    CHECK(factory->EnumAdapters(0, (IDXGIAdapter **)&adapter));
    DXGI_QUERY_VIDEO_MEMORY_INFO info = {}, again = {};
    int ok = 1;
    for (int i = 0; i < 100; i++) ok &= SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info));
    printf("answers memory %d budget %d\n", ok, info.Budget > 0);
    CHECK(adapter->SetVideoMemoryReservation(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, 4096));
    CHECK(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &again));
    printf("answers reservation %llu\n", (unsigned long long)again.CurrentReservation);
    printf("answers bad group %08lx\n",
           (unsigned long)(unsigned)adapter->QueryVideoMemoryInfo(0, (DXGI_MEMORY_SEGMENT_GROUP)7, &again));

    ID3D12Resource *buf = gpu.Buffer(D3D12_HEAP_TYPE_DEFAULT, 65536, D3D12_RESOURCE_STATE_COMMON);
    UINT64 width = 0;
    for (int i = 0; i < 100; i++) width |= buf->GetDesc().Width;
    printf("answers desc %llu\n", (unsigned long long)width);
    return 0;
}
