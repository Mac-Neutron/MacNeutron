// Direct3D forwarding's VM smoke program (d3d-smoke.sh), x86-64, run in each mode. Prints one line each:
//   eventthread  (the final review of M2) a fence's event set when the process's first Direct3D call came from a
//                thread that has since ended: forwarded, the thread that sets the events the host posts must be a Wine
//                thread of its own, not one sharing the first caller's TEB ("ok", or "timeout": never set)
//   eventreuse   (after M2) an event closed after SetEventOnCompletion, whose handle value Wine gives the next event at
//                once: the fence's set reaches the first event, not the new one ("ok", "stray", or "untested": the
//                value wasn't reused)
//   name         (after M2) SetName read back by GetPrivateData(WKPDID_D3DDebugObjectNameW): the bytes given, 2-byte
//                WCHARs ("ok", or the size read back)
//   minimised    (M3 Task 2) a swapchain on a Wine window, the window minimised: Present answers S_OK (flip model:
//                DXMT skips the frame and sleeps a 60 Hz frame), and it and a call right after take under 28 ms: one
//                sleep, the guest's, not a second on the host's vCPU ("ok", "untested": not minimised, or what it
//                answered and how long)
//   swapchain    (M3 Task 3) forwarded only (guest DXMT aborts on them): the seven methods DXMT has no code for, as
//                DXGI answers them: no output restricted to, the background colour kept, identity rotation only, a
//                source size no larger than the buffers ("ok", or the first wrong one)
//   latency      (M3 Task 4) a waitable swapchain's frame latency object starts at its maximum latency and is then
//                signalled once a presented frame; a swapchain without one presents 60 frames the GPU takes longer
//                over than the CPU (four 256 MiB copies each), which d3d-smoke.sh checks held no vCPU on the host
//                ("ok", or what broke)
//   d3d11latency (M4 Task 6) a D3D11CreateDeviceAndSwapChain swapchain (no frame latency object) presents 60 frames
//                the GPU takes longer over than the CPU (four 256 MiB copies each), a fence signalled before each:
//                after each present the fence trails it by no more than the device's latency, 3, and d3d-smoke.sh
//                checks no present held a vCPU on the host ("ok", or what broke)
//   smoke.exe die (M4's final review) a D3D11 device, its shaders and draws that compile pipelines, and the process's
//                end at once, releasing nothing: d3d-smoke.sh runs it three times before the rest, which the host must
//                still be up for (its "die status" lines)
//   wmsize       (M3's final review) a window that answers WM_SIZE with ResizeBuffers(0, 0), as DXGI's samples do:
//                ResizeTarget(640x480) and SetFullscreenState(TRUE, then FALSE) on its own thread each return, and
//                the buffers are then the window's size ("ok", "hang <step>": 10 s, the process ends, or the size)
//   wmtforward   (after M2) winemetal's WMTForward (fork 0079) refuses a call past the guest's table, 156
//                ("ok": STATUS_INVALID_PARAMETER, or what it answered); last, as a wrong one may end the process
// A failed step prints "<line> FAIL <step>".
#include <windows.h>
#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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

static LRESULT CALLBACK window_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(w, m, wp, lp); }

static void pump() {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
}

static double ms_since(LARGE_INTEGER a) {
    LARGE_INTEGER b, f;
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&f);
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

// A new 320x240 Wine window.
static HWND new_window() {
    static WNDCLASSA wc = {};
    if (!wc.lpszClassName) {
        wc.lpfnWndProc = window_proc, wc.hInstance = GetModuleHandleA(nullptr), wc.lpszClassName = "mn-smoke";
        RegisterClassA(&wc);
    }
    return CreateWindowA("mn-smoke", "smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240, nullptr, nullptr,
                         wc.hInstance, nullptr);
}

// A 320x240 flip-model swapchain on a new Wine window: the swapchain, or nullptr.
static IDXGISwapChain1 *swapchain(ID3D12CommandQueue *queue, HWND *window, UINT flags = 0) {
    HWND w = new_window();
    IDXGIFactory2 *f = nullptr;
    IDXGISwapChain1 *sc = nullptr;
    DXGI_SWAP_CHAIN_DESC1 d = {};
    d.Width = 320, d.Height = 240, d.Format = DXGI_FORMAT_R8G8B8A8_UNORM, d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, d.BufferCount = 2, d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.Flags = flags;
    pump();
    if (w && SUCCEEDED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void **)&f)))
        f->CreateSwapChainForHwnd(queue, w, &d, nullptr, nullptr, &sc);
    if (f) f->Release();
    *window = w;
    return sc;
}

// A step that may hang: the watchdog ends the process after 10 s, saying where.
static const char *volatile step;
static HANDLE step_done;
static DWORD WINAPI watchdog(void *line) {
    for (const char *last = nullptr;;) {
        if (WaitForSingleObject(step_done, 10000) == WAIT_OBJECT_0) {
            if (!step) return 0;
            last = step;
            continue;
        }
        if (step && step == last) {
            printf("%s hang %s\n", (const char *)line, step);
            ExitProcess(3);
        }
        last = step;
    }
}
static void watch(const char *line) {
    step_done = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    CloseHandle(CreateThread(nullptr, 0, watchdog, (void *)line, 0, nullptr));
}
static void at(const char *s) { step = s, SetEvent(step_done); }

static IDXGISwapChain1 *sizing; // the WM_SIZE answer's swapchain
static LRESULT CALLBACK size_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SIZE && sizing) sizing->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    return DefWindowProcA(w, m, wp, lp);
}

static void wmsize(ID3D12CommandQueue *queue) {
    HWND w;
    IDXGISwapChain1 *sc = swapchain(queue, &w);
    if (!sc) return (void)printf("wmsize FAIL swapchain\n");
    SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)size_proc);
    sizing = sc;
    watch("wmsize");
    DXGI_MODE_DESC mode = {};
    mode.Width = 640, mode.Height = 480, mode.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    at("ResizeTarget");
    HRESULT hr = sc->ResizeTarget(&mode);
    pump();
    at("SetFullscreenState TRUE");
    sc->SetFullscreenState(TRUE, nullptr);
    pump();
    at("SetFullscreenState FALSE");
    sc->SetFullscreenState(FALSE, nullptr);
    pump();
    at("ResizeTarget again");
    sc->ResizeTarget(&mode);
    pump();
    at(nullptr);
    sizing = nullptr;
    DXGI_SWAP_CHAIN_DESC1 d = {};
    RECT r = {};
    sc->GetDesc1(&d);
    GetClientRect(w, &r);
    if (SUCCEEDED(hr) && d.Width == (UINT)r.right && d.Height == (UINT)r.bottom) printf("wmsize ok\n");
    else printf("wmsize %08lx buffers %ux%u, window %ldx%ld\n", (unsigned long)hr, d.Width, d.Height, r.right, r.bottom);
    sc->Release();
    DestroyWindow(w);
}

static const char *own_answers(ID3D12CommandQueue *queue) {
    HWND w;
    IDXGISwapChain1 *sc = swapchain(queue, &w);
    IDXGISwapChain2 *sc2 = nullptr;
    if (!sc || FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) return "FAIL swapchain";
    IDXGIOutput *o = (IDXGIOutput *)1;
    DXGI_RGBA c = {}, set = {1, 0.5f, 0.25f, 1};
    DXGI_MODE_ROTATION r = DXGI_MODE_ROTATION_UNSPECIFIED;
    UINT sw = 0, sh = 0;
    const char *bad = nullptr;
    if (sc->GetRestrictToOutput(&o) != S_OK || o) bad = "GetRestrictToOutput";
    else if (sc->GetBackgroundColor(&c) != S_OK || c.r || c.g || c.b || c.a != 1) bad = "GetBackgroundColor";
    else if (sc->SetBackgroundColor(&set) != S_OK || sc->GetBackgroundColor(&c) != S_OK || c.g != 0.5f)
        bad = "SetBackgroundColor";
    else if (sc->SetRotation(DXGI_MODE_ROTATION_IDENTITY) != S_OK ||
             sc->SetRotation(DXGI_MODE_ROTATION_ROTATE90) != DXGI_ERROR_INVALID_CALL)
        bad = "SetRotation";
    else if (sc->GetRotation(&r) != S_OK || r != DXGI_MODE_ROTATION_IDENTITY) bad = "GetRotation";
    else if (sc2->GetSourceSize(&sw, &sh) != S_OK || sw != 320 || sh != 240) bad = "GetSourceSize";
    else if (sc2->SetSourceSize(160, 120) != S_OK || sc2->GetSourceSize(&sw, &sh) != S_OK || sw != 160 || sh != 120)
        bad = "SetSourceSize";
    else if (sc2->SetSourceSize(640, 480) != E_INVALIDARG) bad = "SetSourceSize past the buffers";
    sc2->Release();
    sc->Release();
    DestroyWindow(w);
    return bad ? bad : "ok";
}

static void minimised(ID3D12CommandQueue *queue) {
    HWND w;
    IDXGISwapChain1 *sc = swapchain(queue, &w);
    if (!sc || FAILED(sc->Present(0, 0))) return (void)printf("minimised FAIL swapchain\n");
    ShowWindow(w, SW_MINIMIZE);
    pump();
    if (!IsIconic(w)) return (void)printf("minimised untested\n");
    LARGE_INTEGER a;
    QueryPerformanceCounter(&a);
    HRESULT hr = sc->Present(0, 0);
    DXGI_SWAP_CHAIN_DESC1 got = {};
    sc->GetDesc1(&got); // synchronous: whatever the host does for the present before it, it does first
    double ms = ms_since(a);
    if (hr == S_OK && ms < 28) printf("minimised ok\n");
    else printf("minimised %08lx %.1f ms\n", (unsigned long)hr, ms);
    sc->Release();
    DestroyWindow(w);
}

static ID3D12Resource *gpu_buffer(ID3D12Device *device, UINT64 size) {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER, d.Width = size, d.Height = 1, d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1, d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *r = nullptr;
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                    __uuidof(ID3D12Resource), (void **)&r);
    return r;
}

static const char *latency(ID3D12Device *device, ID3D12CommandQueue *queue) {
    HWND w;
    IDXGISwapChain1 *sc = swapchain(queue, &w, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    IDXGISwapChain2 *sc2 = nullptr;
    if (!sc || FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain2), (void **)&sc2))) return "FAIL swapchain";
    HANDLE object = sc2->GetFrameLatencyWaitableObject();
    UINT most = 0;
    sc2->GetMaximumFrameLatency(&most);
    const char *bad = nullptr;
    if (!object || !most) bad = "no object";
    for (UINT i = 0; !bad && i < most; i++)
        if (WaitForSingleObject(object, 0) != WAIT_OBJECT_0) bad = "the object's first count";
    if (!bad && WaitForSingleObject(object, 0) != WAIT_TIMEOUT) bad = "the object past its count";
    for (int k = 0; !bad && k < 10; k++)
        if (FAILED(sc->Present(0, 0)) || WaitForSingleObject(object, 2000) != WAIT_OBJECT_0) bad = "a frame's signal";
    if (object) CloseHandle(object);
    sc2->Release();
    sc->Release();
    DestroyWindow(w);
    if (bad) return bad;
    // Without the object: frames the GPU is behind on (its copies), presented as fast as the CPU can.
    sc = swapchain(queue, &w);
    ID3D12Resource *a = gpu_buffer(device, 256u << 20), *b = gpu_buffer(device, 256u << 20);
    ID3D12CommandAllocator *alloc = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    if (!sc || !a || !b ||
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                              (void **)&alloc)) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr,
                                         __uuidof(ID3D12GraphicsCommandList), (void **)&list)))
        return "FAIL the GPU's frames";
    list->Close();
    std::vector<ID3D12CommandAllocator *> allocs; // one a frame: a list's memory is the GPU's until its frame is done
    for (int k = 0; !bad && k < 60; k++) {
        ID3D12CommandAllocator *fa = nullptr;
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void **)&fa);
        allocs.push_back(fa);
        list->Reset(fa, nullptr);
        for (int i = 0; i < 4; i++) list->CopyBufferRegion(i % 2 ? a : b, 0, i % 2 ? b : a, 0, 256u << 20);
        list->Close();
        ID3D12CommandList *lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        if (FAILED(sc->Present(0, 0))) bad = "a GPU-bound frame's present";
    }
    ID3D12Fence *done = new_fence(device);
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (done && SUCCEEDED(queue->Signal(done, 1)) && SUCCEEDED(done->SetEventOnCompletion(1, e)))
        WaitForSingleObject(e, 30000);
    CloseHandle(e);
    if (done) done->Release();
    for (auto *x : allocs) x->Release();
    list->Release(), alloc->Release(), a->Release(), b->Release(), sc->Release();
    DestroyWindow(w);
    return bad ? bad : "ok";
}

static const char *d3d11_latency() {
    HWND w = new_window();
    pump();
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferDesc.Width = 320, d.BufferDesc.Height = 240, d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1, d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, d.BufferCount = 2, d.OutputWindow = w;
    d.Windowed = TRUE, d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    IDXGISwapChain *sc = nullptr;
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    if (!w || FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1,
                                                   D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx)))
        return "FAIL device";
    ID3D11Device5 *d5 = nullptr;
    ID3D11DeviceContext4 *c4 = nullptr;
    ID3D11Fence *fence = nullptr;
    ID3D11Buffer *a = nullptr, *b = nullptr;
    D3D11_BUFFER_DESC bd = {256u << 20, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
    const char *bad = nullptr;
    if (FAILED(dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&d5)) ||
        FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&c4)) ||
        FAILED(d5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void **)&fence)) ||
        FAILED(dev->CreateBuffer(&bd, nullptr, &a)) || FAILED(dev->CreateBuffer(&bd, nullptr, &b)))
        bad = "FAIL objects";
    static char trail[64];
    for (UINT64 k = 1; !bad && k <= 60; k++) {
        for (int i = 0; i < 4; i++) ctx->CopyResource(i % 2 ? a : b, i % 2 ? b : a);
        c4->Signal(fence, k);
        if (FAILED(sc->Present(0, 0))) bad = "a GPU-bound frame's present";
        UINT64 done = fence->GetCompletedValue();
        if (!bad && done + 3 < k)
            snprintf(trail, sizeof trail, "frame %llu: the fence at %llu", (unsigned long long)k,
                     (unsigned long long)done), bad = trail;
    }
    HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (fence && SUCCEEDED(fence->SetEventOnCompletion(60, e))) WaitForSingleObject(e, 30000);
    CloseHandle(e);
    for (IUnknown *x : {(IUnknown *)a, (IUnknown *)b, (IUnknown *)fence, (IUnknown *)c4, (IUnknown *)d5})
        if (x) x->Release();
    ctx->Release(), sc->Release(), dev->Release();
    DestroyWindow(w);
    return bad ? bad : "ok";
}

static std::vector<char> file(const char *path) {
    std::vector<char> v;
    if (FILE *f = fopen(path, "rb")) {
        char b[4096];
        for (size_t n; (n = fread(b, 1, sizeof b, f));) v.insert(v.end(), b, b + n);
        fclose(f);
    }
    return v;
}

// A child's life (smoke.exe die): a device, shaders, draws whose pipelines compile on the host, then the end at once.
static int die() {
    ID3D11Device *dev = nullptr;
    ID3D11DeviceContext *ctx = nullptr;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx)))
        return 2;
    std::vector<char> vs = file("shaders/d3d11/quad.vs.dxbc");
    ID3D11VertexShader *v = nullptr;
    if (vs.empty() || FAILED(dev->CreateVertexShader(vs.data(), vs.size(), nullptr, &v))) return 3;
    D3D11_TEXTURE2D_DESC td = {64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *t = nullptr;
    ID3D11RenderTargetView *rtv = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &t)) || FAILED(dev->CreateRenderTargetView(t, nullptr, &rtv))) return 4;
    D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(v, nullptr, 0);
    const D3D11_BLEND blends[] = {D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ZERO};
    for (const char *ps : {"shaders/d3d11/solid.ps.dxbc", "shaders/d3d11/sample.ps.dxbc", // float colours: RGBA8's
                           "shaders/d3d11/render.ps.dxbc", "shaders/d3d11/vsia.ps.dxbc"}) {
        std::vector<char> code = file(ps);
        ID3D11PixelShader *p = nullptr;
        if (code.empty() || FAILED(dev->CreatePixelShader(code.data(), code.size(), nullptr, &p))) continue;
        ctx->PSSetShader(p, nullptr, 0);
        for (D3D11_BLEND b : blends) { // a pipeline each: their compiles still running as the process ends
            D3D11_BLEND_DESC bd = {};
            bd.RenderTarget[0] = {TRUE, b, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,
                                  D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
            ID3D11BlendState *bs = nullptr;
            if (SUCCEEDED(dev->CreateBlendState(&bd, &bs))) ctx->OMSetBlendState(bs, nullptr, 0xffffffff);
            ctx->Draw(3, 0);
        }
    }
    ctx->Flush();
    return 0; // the process ends now, nothing released: its pipelines still compiling
}

static void wmt_forward() {
    auto forward = (UINT32(WINAPI *)(UINT32, void *))GetProcAddress(LoadLibraryA("winemetal.dll"), "WMTForward");
    if (!forward) return (void)printf("wmtforward FAIL no WMTForward\n");
    UINT64 params[4] = {};
    UINT32 s = forward(156, params);
    if (s == 0xC000000Du) printf("wmtforward ok\n");
    else printf("wmtforward %08x\n", s);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "die")) return die();
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
    minimised(queue);
    printf("latency %s\n", latency(device, queue));
    printf("d3d11latency %s\n", d3d11_latency());
    const char *mode = getenv("MACNEUTRON_VM_D3D");
    bool forward = mode && !strcmp(mode, "forward");
    if (forward) printf("swapchain %s\n", own_answers(queue));
    else printf("swapchain untested: guest DXMT aborts on these\n");
    wmsize(queue);
    queue->Release();
    device->Release();
    wmt_forward();
    return 0;
}
