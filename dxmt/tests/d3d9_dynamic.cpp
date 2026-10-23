// Direct3D 9 loaded at run time (LoadLibrary, as a game built on another API loads it for dxdiag or a video renderer):
// mtld3d must leave the program's own imports alone. It redirects the main module's EnumDisplaySettings* and exit
// imports only when that module imports d3d9.dll itself; Age of Empires II DE's protector, finding its import table
// changed after an EVR or dxdiag load, ended the game. This program imports no d3d9 function.
// Prints, each "ok" or "FAIL <what>": "d3d9 module" (Direct3DCreate9 through GetProcAddress, then mtld3d's mtld3d.dll
// is loaded: the d3d9.dll is mtld3d's) and "d3d9 imports" (this program's EnumDisplaySettingsW and ExitProcess import
// slots still hold user32's and kernel32's own entry points). Exits with the number of failures.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>

static int failures;
static void row(const char *what, bool ok, const char *why) {
    if (ok) printf("d3d9 %s ok\n", what);
    else printf("d3d9 %s FAIL %s\n", what, why), ++failures;
}

int main() {
    // Taking a dllimport function's address reads its import slot, so these follow a redirection.
    DEVMODEW dm = {};
    dm.dmSize = sizeof dm;
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);  // the import is used, so it stays in the table
    HMODULE d3d9 = LoadLibraryA("d3d9.dll");
    auto create = d3d9 ? (IDirect3D9 *(WINAPI *)(UINT))GetProcAddress(d3d9, "Direct3DCreate9") : nullptr;
    IDirect3D9 *d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    row("module", d3d && GetModuleHandleW(L"mtld3d.dll"), d3d ? "no mtld3d.dll in the process" : "Direct3DCreate9");
    void *enum_slot = (void *)&EnumDisplaySettingsW, *exit_slot = (void *)&ExitProcess;
    void *enum_own = (void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "EnumDisplaySettingsW");
    void *exit_own = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess");
    char why[160];
    snprintf(why, sizeof why, "EnumDisplaySettingsW %p (user32's %p), ExitProcess %p (kernel32's %p)", enum_slot,
             enum_own, exit_slot, exit_own);
    row("imports", enum_slot == enum_own && exit_slot == exit_own, why);
    if (d3d) d3d->Release();
    return failures;
}
