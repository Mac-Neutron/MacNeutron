/* Print ntdll's wine_get_version() string for guest EAC version-spoofing checks (64-bit and 32-bit). */
#include <stdio.h>
#include <windows.h>

int main(void)
{
    typedef const char *(__cdecl *wine_get_version_fn)(void);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    wine_get_version_fn fn;

    if (!ntdll) return 1;
    fn = (wine_get_version_fn)(void *)GetProcAddress(ntdll, "wine_get_version");
    if (!fn) return 2;
    printf("%s\n", fn());
    return 0;
}
