/* modes.exe: a Windows program for eac-vm/tests/modes.sh. The display modes a game is offered (EnumDisplaySettingsW,
 * where DXMT's DXGI GetDisplayModeList reads them), then a full-screen mode change to 1280x720 as a game's resolution
 * setting makes it, and the mode after it. Prints "modes <count of distinct sizes>", each size, "change <result>" and
 * "current WxH". */
#include <stdio.h>
#include <windows.h>

int main(void) {
    DEVMODEW m = {.dmSize = sizeof m};
    DWORD sizes[256][2], n = 0;
    for (DWORD i = 0; EnumDisplaySettingsW(NULL, i, &m); i++) {
        DWORD j = 0;
        while (j < n && (sizes[j][0] != m.dmPelsWidth || sizes[j][1] != m.dmPelsHeight)) j++;
        if (j == n && n < 256) sizes[n][0] = m.dmPelsWidth, sizes[n++][1] = m.dmPelsHeight;
    }
    printf("modes %lu\n", n);
    for (DWORD j = 0; j < n; j++) printf("size %lux%lu\n", sizes[j][0], sizes[j][1]);
    DEVMODEW want = {.dmSize = sizeof want, .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT, .dmPelsWidth = 1280,
                     .dmPelsHeight = 720};
    printf("change %ld\n", ChangeDisplaySettingsExW(NULL, &want, NULL, CDS_FULLSCREEN, NULL));
    EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &m);
    printf("current %lux%lu\n", m.dmPelsWidth, m.dmPelsHeight);
    return 0;
}
