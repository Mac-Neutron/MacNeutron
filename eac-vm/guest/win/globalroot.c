/* globalroot.exe: a Windows program for eac-vm/tests/globalroot.sh. Opens its own exe through the object manager's root,
 * as Age of Empires II DE's protector opens its data file: the image's name from NtQueryVirtualMemory
 * (MemoryMappedFilenameInformation), then CreateFileW("\\.\GLOBALROOT" + that name). Windows resolves \??\GLOBALROOT
 * to the root; Wine 11.20 fails with STATUS_BAD_DEVICE_TYPE until Proton's get_dos_prefix_len() change. Prints ok/FAIL
 * rows, then RESULT: PASS|FAIL; the exit code is the number of failures. */
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winternl.h>

NTSTATUS NTAPI NtQueryVirtualMemory(HANDLE, PVOID, ULONG, PVOID, SIZE_T, SIZE_T *);

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if (!ok) ++failures;
}

/* Opens `name` (Win32) for reading and checks that it starts with "MZ". */
static BOOL win32_opens(const WCHAR *name) {
    HANDLE h = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char mz[2] = {0};
    DWORD got = 0;
    BOOL ok = h != INVALID_HANDLE_VALUE && ReadFile(h, mz, 2, &got, NULL) && got == 2 && !memcmp(mz, "MZ", 2);
    if (h == INVALID_HANDLE_VALUE) printf("     %ls: error %lu\n", name, GetLastError());
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    return ok;
}

/* NtCreateFile on an NT name; returns its status. */
static NTSTATUS nt_open(const WCHAR *name) {
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    HANDLE h;
    RtlInitUnicodeString(&us, name);
    InitializeObjectAttributes(&attr, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    NTSTATUS st = NtCreateFile(&h, GENERIC_READ | SYNCHRONIZE, &attr, &io, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ,
                               FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!st) NtClose(h);
    return st;
}

int main(void) {
    WCHAR path[MAX_PATH], name[MAX_PATH + 64];
    GetModuleFileNameW(NULL, path, MAX_PATH);

    /* The protector's sequence: the image's mapped file name, then \\.\GLOBALROOT in front of it. */
    struct { UNICODE_STRING s; WCHAR buf[MAX_PATH + 64]; } mapped;
    SIZE_T len = 0;
    NTSTATUS st = NtQueryVirtualMemory(GetCurrentProcess(), GetModuleHandleW(NULL), 2 /* MemoryMappedFilenameInformation */,
                                       &mapped, sizeof mapped, &len);
    check(!st, "NtQueryVirtualMemory(MemoryMappedFilenameInformation) of the exe");
    if (!st) {
        swprintf(name, ARRAYSIZE(name), L"\\\\.\\GLOBALROOT%.*ls", (int)(mapped.s.Length / sizeof(WCHAR)), mapped.s.Buffer);
        printf("     %ls\n", name);
        check(win32_opens(name), "CreateFileW(\\\\.\\GLOBALROOT + the mapped name) opens the exe");
    }

    swprintf(name, ARRAYSIZE(name), L"\\\\.\\GLOBALROOT\\??\\%ls", path);
    check(win32_opens(name), "CreateFileW(\\\\.\\GLOBALROOT\\??\\<exe>)");
    swprintf(name, ARRAYSIZE(name), L"\\\\.\\GlobalRoot\\??\\%ls", path);
    check(win32_opens(name), "CreateFileW(\\\\.\\GlobalRoot\\??\\<exe>), mixed case");
    swprintf(name, ARRAYSIZE(name), L"\\??\\GLOBALROOT\\??\\%ls", path);
    st = nt_open(name);
    printf("     NtCreateFile(%ls) = %#lx\n", name, (unsigned long)st);
    check(!st, "NtCreateFile(\\??\\GLOBALROOT\\??\\<exe>)");
    swprintf(name, ARRAYSIZE(name), L"\\??\\GLOBALROOT\\??\\%ls.missing", path);
    st = nt_open(name);
    check(st == (NTSTATUS)0xC0000034, "a missing file through GLOBALROOT: STATUS_OBJECT_NAME_NOT_FOUND");
    puts(failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures;
}
