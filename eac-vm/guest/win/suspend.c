/* suspend.exe: a Windows program for eac-vm/tests/suspend.sh. Age of Empires II DE's protector suspends every thread of
 * its process (SuspendThread, GetThreadContext); under Wine each suspension is a SIGUSR1 to the thread, and under FEX
 * some threads died of Wine's "nested exception on signal stack". Workers do what the game's threads do: take ud2
 * exceptions a vectored handler skips (the protector's nanomites), make raw syscalls (its direct syscalls), wait in
 * syscalls, compute. The main thread suspends, reads and resumes each of them for 30 s, then stops them; every worker
 * must stay alive and return 0. An unhandled exception prints the thread, its kind and registers, and ends the process
 * with 99 (no winedbg). argv[1], a hex mask of kinds (1 ud2, 2 raw syscall, 4 wait, 8 compute), picks the workers; all by
 * default. Prints ok/FAIL rows, then RESULT: PASS|FAIL; the exit code is the number of failures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define KINDS 4
#define PER_KIND 2
#define WORKERS (KINDS * PER_KIND)
#define SECONDS 30

typedef LONG(WINAPI *query_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);
static const char *kind_name[KINDS] = {"ud2", "raw syscall", "wait", "compute"};
static volatile LONG stop;
static volatile LONG64 counts[WORKERS];
static DWORD worker_tid[WORKERS];
static query_fn raw_query;
static HANDLE never;
static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    if (!ok) ++failures;
}

/* The protector's nanomites: an invalid instruction its handler steps over. */
static LONG CALLBACK skip_ud2(EXCEPTION_POINTERS *e) {
    const unsigned char *ip = (const unsigned char *)e->ContextRecord->Rip;
    const CONTEXT *c = e->ContextRecord;
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION || ip[0] != 0x0f || ip[1] != 0x0b)
        return EXCEPTION_CONTINUE_SEARCH;
    /* The worker's loop at its ud2 (see worker()): r15 = counts, rsi = rsp + 0x30. A context that breaks them was
     * corrupted before the handler ran. */
    if (c->R15 != (DWORD64)counts || c->Rsi != c->Rsp + 0x30) {
        printf("FAIL the ud2 context is corrupt in thread %04lx: rip %016llx rsp %016llx rsi %016llx r15 %016llx (counts %p)\n",
               GetCurrentThreadId(), c->Rip, c->Rsp, c->Rsi, c->R15, (void *)counts);
        printf("     rax %016llx rbx %016llx rcx %016llx rdx %016llx rdi %016llx rbp %016llx\n", c->Rax, c->Rbx, c->Rcx,
               c->Rdx, c->Rdi, c->Rbp);
        printf("     r12 %016llx r13 %016llx r14 %016llx eflags %08lx\nRESULT: FAIL\n", c->R12, c->R13, c->R14, c->EFlags);
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 98);
    }
    e->ContextRecord->Rip += 2;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *e) {
    const CONTEXT *c = e->ContextRecord;
    DWORD tid = GetCurrentThreadId();
    int w = -1;
    for (int i = 0; i < WORKERS; ++i) if (worker_tid[i] == tid) w = i;
    printf("FAIL unhandled exception %#lx at %p in thread %04lx (worker %d, %s)\n", e->ExceptionRecord->ExceptionCode,
           e->ExceptionRecord->ExceptionAddress, tid, w, w < 0 ? "main" : kind_name[w / PER_KIND]);
    printf("     rip %016llx rsp %016llx rbp %016llx eflags %08lx\n", c->Rip, c->Rsp, c->Rbp, c->EFlags);
    printf("     rax %016llx rbx %016llx rcx %016llx rdx %016llx\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
    printf("     rsi %016llx rdi %016llx r8  %016llx r9  %016llx\n", c->Rsi, c->Rdi, c->R8, c->R9);
    printf("     r10 %016llx r11 %016llx r12 %016llx r13 %016llx\n", c->R10, c->R11, c->R12, c->R13);
    printf("     r14 %016llx r15 %016llx\nRESULT: FAIL\n", c->R14, c->R15);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 99);
    return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI worker(void *arg) {
    int i = (int)(ULONG_PTR)arg;
    ULONG len;
    ULONG_PTR info[6];
    volatile unsigned x = 1;
    while (!stop) {
        switch (i / PER_KIND) {
        case 0: __asm__ volatile("ud2"); break;
        case 1: if (raw_query(GetCurrentProcess(), 0, info, sizeof info, &len)) return 2; break;
        case 2: WaitForSingleObject(never, 1); break;
        case 3: for (int k = 0; k < 1000; ++k) x = x * 1103515245u + 12345u; break;
        }
        ++counts[i];
    }
    return 0;
}

/* rawsyscall.c's stub: mov r10,rcx; mov eax,N; syscall; ret, N read from ntdll's own stub. */
static BOOL make_raw_query(void) {
    const unsigned char *p = (const unsigned char *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
    unsigned char *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    static const unsigned char head[] = {0x4c, 0x8b, 0xd1, 0xb8}, tail[] = {0x0f, 0x05, 0xc3};
    if (!p || !page || memcmp(p, head, sizeof head)) return FALSE;
    memcpy(page, head, sizeof head);
    memcpy(page + 4, p + 4, 4);
    memcpy(page + 8, tail, sizeof tail);
    raw_query = (query_fn)page;
    return TRUE;
}

int main(int argc, char **argv) {
    HANDLE threads[WORKERS] = {0};
    unsigned mask = argc > 1 ? strtoul(argv[1], NULL, 16) : 0xf;
    LONG64 rounds = 0, bad_context = 0;
    char what[160];

    if (!make_raw_query()) { puts("FAIL no syscall stub\nRESULT: FAIL"); return 1; }
    never = CreateEventA(NULL, TRUE, FALSE, NULL);
    AddVectoredExceptionHandler(1, skip_ud2);
    SetUnhandledExceptionFilter(unhandled);
    for (int i = 0; i < WORKERS; ++i)
        if (mask & (1u << (i / PER_KIND)))
            threads[i] = CreateThread(NULL, 0, worker, (void *)(ULONG_PTR)i, 0, &worker_tid[i]);
    printf("     pid %lu, kinds %#x, %d s\n", GetCurrentProcessId(), mask, SECONDS);
    fflush(stdout);

    ULONGLONG end = GetTickCount64() + SECONDS * 1000;
    while (GetTickCount64() < end) {
        for (int i = 0; i < WORKERS; ++i) {
            if (!threads[i]) continue;
            CONTEXT c = {.ContextFlags = CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS};
            DWORD code = STILL_ACTIVE;
            if (GetExitCodeThread(threads[i], &code) && code != STILL_ACTIVE) continue;
            if (SuspendThread(threads[i]) == (DWORD)-1) continue;
            if (!GetThreadContext(threads[i], &c) || !c.Rip || !c.Rsp) ++bad_context;
            ResumeThread(threads[i]);
        }
        ++rounds;
    }
    stop = 1;
    for (int i = 0; i < WORKERS; ++i) if (threads[i]) WaitForSingleObject(threads[i], 30000);

    printf("     %lld rounds of suspend, get context, resume\n", (long long)rounds);
    check(rounds > 100, "the main thread kept suspending");
    check(!bad_context, "every suspended thread's context had rip and rsp");
    for (int i = 0; i < WORKERS; ++i) {
        DWORD code = 12345;
        if (!threads[i]) continue;
        GetExitCodeThread(threads[i], &code);
        snprintf(what, sizeof what, "worker %d (%s): %lld iterations, exit code %lu (0 = alive to the end)", i,
                 kind_name[i / PER_KIND], (long long)counts[i], code);
        check(code == 0 && counts[i] > 0, what);
    }
    puts(failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures;
}
