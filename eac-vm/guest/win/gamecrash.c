/* gamecrash.exe for eac-vm/tests/crash-reporter.sh: an Unreal-like crash, a reporter process it waits for. The game
 * starts CrashReportClient.exe (the test puts a copy in the share's bin/) and waits, as Unreal's crash handler does;
 * nothing visible ever happens on Xvfb, so run-game.sh has to end the VM. */
#include <stdio.h>
#include <windows.h>

int main(void) {
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof si;
    puts("gamecrash: starting its crash reporter");
    fflush(stdout);
    if (!CreateProcessA("CrashReportClient.exe", "CrashReportClient.exe", NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        printf("gamecrash: CreateProcess failed %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    puts("gamecrash: the reporter ended");
    return 0;
}
