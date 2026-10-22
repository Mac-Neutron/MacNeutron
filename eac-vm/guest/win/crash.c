/* crash.exe: a Windows program for eac-vm/tests/crash.sh. Faults on a null write after one line, so the run shows what a
 * crashing game does: winedbg's report, then the run ends. */
#include <stdio.h>

int main(void) {
    puts("crash: about to fault");
    fflush(stdout);
    *(volatile int *)0 = 1;
    return 0;
}
