// Direct3D 9 (D3D9 spec §6): a 256x256 window, a HAL device, then frames of a clear to blue and a fixed-function
// pretransformed red triangle over the centre, each read back through GetRenderTargetData and presented, until one shows
// the triangle and 10 more have been presented (for up to 10 s): mtld3d leaves a draw out of its frame while its
// pipeline builds (shader.asyncCompile, on by default), so the first frames may show the clear alone, as they do in a
// game (in the VM's busy DXMT gate the build has taken over 300 frames).
// Prints, each "ok" or "FAIL <what>": "d3d9 module" (mtld3d's mtld3d.dll is loaded once Direct3DCreate9 ran: the
// d3d9.dll is mtld3d's, not Wine's wined3d one), "d3d9 clear" (a corner of the first frame is 0x0000ff), "d3d9
// triangle" (a frame's centre is 0xff0000) and "d3d9 present" (every present D3D_OK, at least 10 of them); and
// "frames: <n>". With the argument "vsync" the device presents at D3DPRESENT_INTERVAL_ONE, as most games do, and
// "d3d9 vsync" says 60 more presents took under 5 s (a display paces them at its rate; a present the layer never
// reports presented makes each one wait out mtld3d's 1 s cap). Exits with the number of failures.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

static int failures;
static void row(const char *what, bool ok, const char *why) {
    if (ok) printf("d3d9 %s ok\n", what);
    else printf("d3d9 %s FAIL %s\n", what, why), ++failures;
}

int main(int argc, char **argv) {
    const bool vsync = argc > 1 && !strcmp(argv[1], "vsync");
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"d3d9_triangle";
    RegisterClassW(&wc);
    RECT r = {0, 0, 256, 256};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(L"d3d9_triangle", L"d3d9_triangle", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    row("module", d3d && GetModuleHandleW(L"mtld3d.dll"), d3d ? "no mtld3d.dll in the process" : "Direct3DCreate9");
    if (!d3d || !hwnd) return printf("d3d9 clear FAIL no device\n"), failures + 3;

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 256, pp.BackBufferHeight = 256, pp.BackBufferFormat = D3DFMT_X8R8G8B8, pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD, pp.hDeviceWindow = hwnd, pp.Windowed = TRUE;
    pp.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                   &dev);
    if (FAILED(hr)) return printf("d3d9 clear FAIL CreateDevice %08lx\n", (unsigned long)hr), failures + 3;

    struct Vertex { float x, y, z, rhw; DWORD color; };
    const Vertex tri[3] = {{128, 16, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {240, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)},
                           {16, 240, 0.5f, 1, D3DCOLOR_XRGB(255, 0, 0)}};
    IDirect3DSurface9 *rt = nullptr, *sys = nullptr;
    bool read = SUCCEEDED(dev->GetRenderTarget(0, &rt)) &&
                SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 256, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr));
    DWORD corner = 0, centre = 0;
    int frames = 0, presented = 0, after = 0;
    const ULONGLONG until = GetTickCount64() + 10000;
    while (after < 10 && GetTickCount64() < until) {
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
        dev->BeginScene();
        dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof *tri);
        dev->EndScene();
        D3DLOCKED_RECT lr = {};
        read = read && SUCCEEDED(dev->GetRenderTargetData(rt, sys)) &&
               SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        if (!read) break;
        const BYTE *p = (const BYTE *)lr.pBits;
        if (!frames++) corner = *(const DWORD *)(p + 2 * lr.Pitch + 2 * 4) & 0xffffff;
        if (centre != 0xff0000) centre = *(const DWORD *)(p + 128 * lr.Pitch + 128 * 4) & 0xffffff;
        sys->UnlockRect();
        presented += dev->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK;
        after += centre == 0xff0000;
    }
    char why[64];
    snprintf(why, sizeof why, read ? "corner %06lx" : "no readback", (unsigned long)corner);
    row("clear", read && corner == 0x0000ff, why);
    snprintf(why, sizeof why, read ? "centre %06lx after %d frames" : "no readback", (unsigned long)centre, frames);
    row("triangle", read && centre == 0xff0000, why);
    snprintf(why, sizeof why, "%d of %d", presented, frames);
    row("present", frames >= 10 && presented == frames, why);
    printf("frames: %d\n", frames);
    if (vsync) {
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        int ok = 0;
        for (int i = 0; i < 60; i++) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.0f, 0);
            ok += dev->Present(nullptr, nullptr, nullptr, nullptr) == D3D_OK;
        }
        QueryPerformanceCounter(&t1);
        double s = (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart;
        printf("vsync: 60 presents in %.2f s\n", s);
        snprintf(why, sizeof why, "%d of 60 presents in %.1f s", ok, s);
        row("vsync", ok == 60 && s < 5.0, why);
    }

    if (sys) sys->Release();
    if (rt) rt->Release();
    dev->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    return failures;
}
