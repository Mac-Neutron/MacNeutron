/* crash.exe: a Windows program for eac-vm/tests/crash.sh. Shaped like a big game before it faults: it loads two dozen
 * large builtin DLLs and parks eight threads, so winedbg's report has to cover many modules and threads (as for Age of
 * Empires II DE), then faults on a null write. The run shows what a crashing game does: winedbg's report, then the
 * run ends. */
#include <stdio.h>
#include <windows.h>

static DWORD WINAPI parked(void *arg) {
    (void)arg;
    Sleep(INFINITE);
    return 0;
}

int main(void) {
    static const char *const dlls[] = {
        "user32", "gdi32", "ole32", "oleaut32", "shell32", "shlwapi", "comctl32", "comdlg32", "advapi32", "ws2_32",
        "winhttp", "wininet", "crypt32", "bcrypt", "setupapi", "dbghelp", "d3d11", "dxgi", "d3d9", "d3dcompiler_47",
        "windowscodecs", "mmdevapi", "xaudio2_9", "dinput8", "msvcp140",
    };
    int loaded = 0;
    for (size_t i = 0; i < sizeof dlls / sizeof *dlls; ++i) loaded += LoadLibraryA(dlls[i]) != NULL;
    for (int i = 0; i < 8; ++i) CloseHandle(CreateThread(NULL, 0, parked, NULL, 0, NULL));
    printf("crash: %d DLLs, 8 threads\n", loaded);
    puts("crash: about to fault");
    fflush(stdout);
    *(volatile int *)0 = 1;
    return 0;
}
