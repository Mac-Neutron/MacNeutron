/* lookups.exe: a Windows program for eac-vm/tests/share-lookups.sh. In a folder (its DOS path, the argument) of
 * d0..d19 with f0..f99 each and d0\long file name.txt: a name in another case and Wine's short name for a long one
 * still open, a missing name is ERROR_FILE_NOT_FOUND, then 2,000 names that aren't there, looked up as a game probes
 * for optional files (8.3 names and long ones, in folders of 100). Prints ok/FAIL rows, then
 * "RESULT: PASS|FAIL <microseconds per missing name>"; the exit code is the number of failures. */
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if (!ok) ++failures;
}

int wmain(int argc, WCHAR **argv) {
    WCHAR p[MAX_PATH], s[MAX_PATH];
    LARGE_INTEGER f, a, b;
    int i, n = 0;

    if (argc < 2) return 2;
    swprintf(p, MAX_PATH, L"%ls\\D0\\F1", argv[1]);
    check(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES, "a name in another case");
    swprintf(p, MAX_PATH, L"%ls\\d0\\long file name.txt", argv[1]);
    check(GetShortPathNameW(p, s, MAX_PATH) && wcschr(s, L'~') && GetFileAttributesW(s) != INVALID_FILE_ATTRIBUTES,
          "Wine's short name for a long one");
    swprintf(p, MAX_PATH, L"%ls\\d0\\missing.dll", argv[1]);
    check(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND, "a missing name");
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for (i = 0; i < 2000; i++) {
        swprintf(p, MAX_PATH, i & 1 ? L"%ls\\d%d\\no%d.dll" : L"%ls\\d%d\\missing file %d.dat", argv[1], i % 20, i);
        n += GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES;
    }
    QueryPerformanceCounter(&b);
    check(n == 2000, "2,000 missing names");
    printf("RESULT: %s %.0f\n", failures ? "FAIL" : "PASS", (double)(b.QuadPart - a.QuadPart) * 1e6 / f.QuadPart / 2000);
    return failures;
}
