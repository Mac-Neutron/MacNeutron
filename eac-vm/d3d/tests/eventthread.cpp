// A fence's event set when the process's first Direct3D call came from a thread that has since ended (the final
// review of M2): forwarded, the unix side's first call sets up the notify queue, and the thread that sets the events
// the host posts must be a Wine thread of its own, not one sharing the first caller's TEB. Prints "eventthread ok",
// or "eventthread timeout" when the event is never set (or "eventthread FAIL <step>"). x86-64, run by d3d-smoke.sh.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>

static DWORD WINAPI first(void *) {
    IDXGIFactory1 *f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f))) f->Release();
    return 0;
}

int main() {
    HANDLE t = CreateThread(nullptr, 0, first, nullptr, 0, nullptr);
    if (!t) return printf("eventthread FAIL thread\n"), 1;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device)))
        return printf("eventthread FAIL device\n"), 1;
    if (FAILED(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue)) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence)))
        return printf("eventthread FAIL objects\n"), 1;
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    // The event first, then the signal: it must come from the host, not be set at once by the guest.
    if (FAILED(fence->SetEventOnCompletion(1, e)) || FAILED(queue->Signal(fence, 1)))
        return printf("eventthread FAIL signal\n"), 1;
    // No wait until the host has set it: a wait first would give the process ntsync's handle for the event, and the
    // set would need no server call (none of the listener's thread data).
    Sleep(1000);
    printf("eventthread %s\n", WaitForSingleObject(e, 10000) == WAIT_OBJECT_0 ? "ok" : "timeout");
    fence->Release();
    queue->Release();
    device->Release();
    return 0;
}
