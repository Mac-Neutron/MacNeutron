/* rawsyscall.exe: a Windows program for eac-vm/tests/sud.sh. Makes Windows system calls the way anti-tamper code does
 * (Age of Empires II DE's): `mov r10,rcx; mov eax,N; syscall; ret` from a page of its own, N read from ntdll's stub.
 * Under Wine on Linux such a syscall reaches Wine's dispatcher only through syscall user dispatch, which FEX emulates
 * since patch 0036. Prints ok/FAIL rows, then RESULT: PASS|FAIL; the exit code is the number of failures. */
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef LONG(WINAPI *close_fn)(HANDLE);
typedef LONG(WINAPI *query_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);
/* PROCESS_BASIC_INFORMATION */
struct basic_info { LONG_PTR exit_status; void *peb; ULONG_PTR affinity; LONG_PTR priority; ULONG_PTR pid, parent; };

static int failures;
static close_fn raw_close;
static query_fn raw_query;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if (!ok) ++failures;
}

/* Wine's x86-64 stubs are Windows' (include/wine/asm.h, __ASM_SYSCALL_FUNC): mov r10,rcx; mov eax,N; ... */
static BOOL syscall_number(const char *name, DWORD *id) {
    const unsigned char *p = (const unsigned char *)GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
    if (p && p[0] == 0x4c && p[1] == 0x8b && p[2] == 0xd1 && p[3] == 0xb8) {
        memcpy(id, p + 4, sizeof *id);
        return TRUE;
    }
    printf("FAIL stub %s:", name);
    for (int i = 0; p && i < 8; ++i) printf(" %02x", p[i]);
    printf("\n");
    ++failures;
    return FALSE;
}

static void *write_stub(unsigned char *at, DWORD id) {
    static const unsigned char head[] = {0x4c, 0x8b, 0xd1, 0xb8}, tail[] = {0x0f, 0x05, 0xc3};
    memcpy(at, head, sizeof head);
    memcpy(at + 4, &id, sizeof id);
    memcpy(at + 8, tail, sizeof tail);
    return at;
}

static BOOL close_ok(void) { return raw_close((HANDLE)(ULONG_PTR)0xdead) == (LONG)0xC0000008; /* STATUS_INVALID_HANDLE */ }
static BOOL query_ok(void) {
    struct basic_info info = {0};
    ULONG len = 0;
    return raw_query(GetCurrentProcess(), 0 /* ProcessBasicInformation */, &info, sizeof info, &len) == 0 &&
           info.pid == GetCurrentProcessId();
}
static BOOL rounds_ok(void) {
    BOOL ok = TRUE;
    for (int i = 0; i < 1000; ++i) ok = close_ok() && query_ok() && ok;
    return ok;
}
static DWORD WINAPI second_thread(void *arg) {
    (void)arg;
    return close_ok() && query_ok() && rounds_ok();
}

int main(void) {
    DWORD close_id, query_id;
    if (!syscall_number("NtClose", &close_id) || !syscall_number("NtQueryInformationProcess", &query_id)) {
        puts("RESULT: FAIL");
        return failures;
    }
    unsigned char *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    raw_close = (close_fn)write_stub(page, close_id);
    raw_query = (query_fn)write_stub(page + 16, query_id);

    printf("     NtClose is %#lx, NtQueryInformationProcess %#lx; NtClose(0xdead) = %#lx\n", close_id, query_id,
           (unsigned long)raw_close((HANDLE)(ULONG_PTR)0xdead));
    check(close_ok(), "NtClose(0xdead) by a raw syscall: STATUS_INVALID_HANDLE");
    check(query_ok(), "NtQueryInformationProcess by a raw syscall: our process id");
    check(rounds_ok(), "1000 rounds of both");
    HANDLE thread = CreateThread(NULL, 0, second_thread, NULL, 0, NULL);
    DWORD code = 0;
    WaitForSingleObject(thread, INFINITE);
    GetExitCodeThread(thread, &code);
    check(code == 1, "the same from a second thread");
    puts(failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures;
}
