/* Plan 3a Task 4, Review Focus 4: the path stand-ins map the shared library folder and refuse everything else. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/unixlib.h"
#include "../host_wine.h"

/* Declared by wine/unixlib.h only inside ntdll; host_wine.c defines them. */
NTSTATUS ntdll_get_dos_file_name(const char *unix_name, WCHAR **dos, UINT disposition);
NTSTATUS ntdll_get_unix_file_name(const WCHAR *dos, char **unix_name, UINT disposition);

static int failures;
static void dos(const char *unix_name, const char *want) {
    WCHAR *w = NULL; char got[512] = "(refused)";
    if (!ntdll_get_dos_file_name(unix_name, &w, 0) && w) {
        DWORD len = 0; while (w[len]) len++;
        int n = ntdll_wcstoumbs(w, len, got, sizeof got - 1, 0); got[n] = 0;
    }
    if (strcmp(got, want)) { printf("FAIL dos %s: got %s, want %s\n", unix_name, got, want); failures++; }
    free(w);
}
static void unx(const char *dos_name, const char *want) {
    WCHAR w[512]; DWORD n = ntdll_umbstowcs(dos_name, strlen(dos_name), w, 511); w[n] = 0;
    char *u = NULL;
    const char *got = !ntdll_get_unix_file_name(w, &u, 0) && u ? u : "(refused)";
    if (strcmp(got, want)) { printf("FAIL unix %s: got %s, want %s\n", dos_name, got, want); failures++; }
    free(u);
}
int main(void) {
    mn_stub_set_map("/Users/x/Steam/steamapps/common", "/mnt/games");
    dos("/Users/x/Steam/steamapps/common/SMITE 2/bin", "Z:\\mnt\\games\\SMITE 2\\bin");
    dos("/Users/x/Steam/steamapps/common", "Z:\\mnt\\games");
    dos("/Users/x/Steam/steamapps/commonX/a", "(refused)");
    dos("/etc/passwd", "(refused)");
    unx("Z:\\mnt\\games\\SMITE 2\\x.dll", "/Users/x/Steam/steamapps/common/SMITE 2/x.dll");
    unx("\\??\\Z:\\mnt\\games\\a", "/Users/x/Steam/steamapps/common/a");
    unx("C:\\windows\\system32", "(refused)");
    unx("Z:\\mnt\\gamesX\\a", "(refused)");
    unx("Z:\\etc\\passwd", "(refused)");
    /* lsteamclient's unescaped URLs: UTF-8 in, UTF-16 out, lengths in bytes (a NULL dst asks for the length). */
    WCHAR w[8] = {0}; DWORD bytes = 0, need = 0;
    RtlUTF8ToUnicodeN(w, sizeof w, &bytes, "a\xc3\xa9", 4);
    RtlUTF8ToUnicodeN(NULL, 0, &need, "a\xc3\xa9", 4);
    if (w[0] != 'a' || w[1] != 0xe9 || w[2] != 0 || bytes != 6 || need != 6) {
        printf("FAIL RtlUTF8ToUnicodeN: %x %x %x, %u and %u bytes\n", w[0], w[1], w[2], bytes, need); failures++;
    }
    printf(failures ? "path-test: %d failure(s)\n" : "path-test: all passed\n", failures);
    return failures != 0;
}
