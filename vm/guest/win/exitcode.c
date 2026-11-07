/* exitcode.exe <code|hang> <file>: a Steam install script's step for tests/install-scripts.sh. Appends a line to <file>
 * (how many times the step ran), then exits with <code>, or with "hang" never ends: an installer waiting for a click on
 * a screen nobody sees. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int main(int argc, char **argv)
{
    FILE *f;
    if (argc < 3) return 2;
    if ((f = fopen(argv[2], "ab"))) {
        fputs("ran\r\n", f);
        fclose(f);
    }
    if (!strcmp(argv[1], "hang")) for (;;) Sleep(INFINITE);
    return atoi(argv[1]);
}
