// Direct3D forwarding's VM smoke program (d3d-smoke.sh), x86-64, run in each mode. Prints one line each:
//   eventthread  (the final review of M2) a fence's event set when the process's first Direct3D call came from a
//                thread that has since ended: forwarded, the thread that sets the events the host posts must be a Wine
//                thread of its own, not one sharing the first caller's TEB ("ok", or "timeout": never set)
//   eventreuse   (after M2) an event closed after SetEventOnCompletion, whose handle value Wine gives the next event at
//                once: the fence's set reaches the first event, not the new one ("ok", "stray", or "untested": the
//                value wasn't reused)
//   name         (after M2) SetName read back by GetPrivateData(WKPDID_D3DDebugObjectNameW): the bytes given, 2-byte
//                WCHARs ("ok", or the size read back)
//   wmtforward   (after M2) winemetal's WMTForward (fork 0079) refuses a call past the guest's table, 156
//                ("ok": STATUS_INVALID_PARAMETER, or what it answered); last, as a wrong one may end the process
// A failed step prints "<line> FAIL <step>".
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstring>

static DWORD WINAPI first(void *) {
    IDXGIFactory1 *f = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&f))) f->Release();
    return 0;
}

static ID3D12Fence *new_fence(ID3D12Device *device) {
    ID3D12Fence *fence = nullptr;
    return SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence)) ? fence
                                                                                                           : nullptr;
}

static const char *event_thread(ID3D12Device *device, ID3D12CommandQueue *queue) {
    ID3D12Fence *fence = new_fence(device);
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    // The event first, then the signal: it must come from the host, not be set at once by the guest.
    if (!fence || FAILED(fence->SetEventOnCompletion(1, e)) || FAILED(queue->Signal(fence, 1))) return "FAIL signal";
    // No wait until the host has set it: a wait first would give the process ntsync's handle for the event, and the
    // set would need no server call (none of the listener's thread data).
    Sleep(1000);
    const char *r = WaitForSingleObject(e, 10000) == WAIT_OBJECT_0 ? "ok" : "timeout";
    CloseHandle(e);
    fence->Release();
    return r;
}

static const char *event_reuse(ID3D12Device *device, ID3D12CommandQueue *queue) {
    ID3D12Fence *fence = new_fence(device);
    HANDLE closed = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!fence || FAILED(fence->SetEventOnCompletion(1, closed))) return "FAIL set";
    CloseHandle(closed);
    HANDLE reused = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (reused != closed) return "untested";
    if (FAILED(queue->Signal(fence, 1))) return "FAIL signal";
    HANDLE done = CreateEventA(nullptr, FALSE, FALSE, nullptr); // after the first: its set comes after the first's
    if (FAILED(fence->SetEventOnCompletion(1, done)) || WaitForSingleObject(done, 10000) != WAIT_OBJECT_0)
        return "FAIL wait";
    Sleep(200);
    const char *r = WaitForSingleObject(reused, 0) == WAIT_TIMEOUT ? "ok" : "stray";
    CloseHandle(reused);
    CloseHandle(done);
    fence->Release();
    return r;
}

static void name(ID3D12CommandQueue *queue) {
    static const GUID debug_name = {0x4cca5fd8, 0x921f, 0x42c8, {0x85, 0x66, 0x70, 0xca, 0xf2, 0xa9, 0xb7, 0x41}};
    const WCHAR text[] = L"caché";
    WCHAR back[32] = {};
    UINT size = sizeof back;
    if (FAILED(queue->SetName(text)) || FAILED(queue->GetPrivateData(debug_name, &size, back)))
        return (void)printf("name FAIL calls\n");
    if (size == sizeof text && !memcmp(back, text, sizeof text)) printf("name ok\n");
    else printf("name %u\n", size);
}

static void wmt_forward() {
    auto forward = (UINT32(WINAPI *)(UINT32, void *))GetProcAddress(LoadLibraryA("winemetal.dll"), "WMTForward");
    if (!forward) return (void)printf("wmtforward FAIL no WMTForward\n");
    UINT64 params[4] = {};
    UINT32 s = forward(156, params);
    if (s == 0xC000000Du) printf("wmtforward ok\n");
    else printf("wmtforward %08x\n", s);
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    HANDLE t = CreateThread(nullptr, 0, first, nullptr, 0, nullptr);
    if (!t) return printf("eventthread FAIL thread\n"), 1;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void **)&device)) ||
        FAILED(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void **)&queue)))
        return printf("eventthread FAIL device\n"), 1;
    printf("eventthread %s\n", event_thread(device, queue));
    printf("eventreuse %s\n", event_reuse(device, queue));
    name(queue);
    queue->Release();
    device->Release();
    wmt_forward();
    return 0;
}
