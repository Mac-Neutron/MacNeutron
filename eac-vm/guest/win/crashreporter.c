/* CrashReportClient.exe (built from crashreporter.c) for eac-vm/tests/crash-reporter.sh: a game's crash reporter as the
 * VM sees one, parked on the invisible Xvfb until run-game.sh ends the prefix. It prints once, then sleeps. */
#include <stdio.h>
#include <windows.h>

int main(void) {
    puts("crashreporter: waiting on the invisible screen");
    fflush(stdout);
    Sleep(300000);
    return 0;
}
