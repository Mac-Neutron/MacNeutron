/* rawinput.exe: a Windows program for eac-vm/tests/input.sh. A full-screen window that registers for raw mouse input
 * (what games read for mouse look) and clips the cursor to itself, as a game in a match does, then prints for 40 s the
 * raw deltas (WM_INPUT), clicks and keys it gets: what the host's MN_INPUT_SELFTEST sends, replayed by mn-input. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_INPUT: {
        RAWINPUT ri;
        UINT size = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && (ri.data.mouse.lLastX || ri.data.mouse.lLastY))
            printf("raw %s %ld %ld\n", ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ? "abs" : "rel", ri.data.mouse.lLastX,
                   ri.data.mouse.lLastY);
        break;
    }
    case WM_LBUTTONDOWN: printf("click %d %d\n", (short)LOWORD(lp), (short)HIWORD(lp)); break;
    case WM_KEYDOWN: printf("key %u\n", (unsigned)wp); break;
    default: return DefWindowProcA(w, m, wp, lp);
    }
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "mode")) { /* a game's full-screen mode below the screen's (Wine's emulated) */
        DEVMODEW m = {.dmSize = sizeof m, .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT, .dmPelsWidth = 1920,
                      .dmPelsHeight = 1080};
        printf("rawinput: mode %ld\n", ChangeDisplaySettingsExW(NULL, &m, NULL, CDS_FULLSCREEN, NULL));
    }
    WNDCLASSA c = {0};
    c.lpfnWndProc = proc;
    c.lpszClassName = "rawinput";
    RegisterClassA(&c);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND w = CreateWindowA("rawinput", "rawinput", WS_POPUP | WS_VISIBLE, 0, 0, sw, sh, NULL, NULL, NULL, NULL);
    SetForegroundWindow(w);
    SetFocus(w);
    RAWINPUTDEVICE d = {1, 2, RIDEV_INPUTSINK, w}; /* generic desktop, mouse */
    RECT r = {0, 0, sw, sh};
    ShowCursor(FALSE);
    printf("rawinput: register %d, clip %d, window %dx%d\n", RegisterRawInputDevices(&d, 1, sizeof d), ClipCursor(&r),
           sw, sh);
    fflush(stdout);
    DWORD end = GetTickCount() + 40000;
    while (GetTickCount() < end) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        Sleep(5);
    }
    return 0;
}
