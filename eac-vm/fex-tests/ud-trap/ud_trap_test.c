// SPDX-License-Identifier: MIT
//
// Regression test for an invalid opcode (#UD). Linux's do_error_trap reports it as SIGILL with si_code ILL_ILLOPN (2),
// si_addr = the instruction, trapno 6 (X86_TRAP_UD) and RIP = the instruction in the ucontext; Wine maps trapno 6 to
// EXCEPTION_ILLEGAL_INSTRUCTION (and logs "unexpected trap" for anything it doesn't know). Rows: the byte 60 (PUSHA,
// invalid in 64-bit code; Age of Empires II DE's protector plants it) and 8e c8 (mov cs, ax: CS can't be loaded by MOV).
// The handler steps over the instruction, so each row also checks that execution continues. The last rows run them
// again on a code page the program made writable and executable and then rewrote (FEX treats such a page as "hot" and
// verifies its code instead of write-protecting it: patch 0027), as Age of Empires II DE's protector does between two
// nanomites; FEX looped there forever. An alarm turns a hang into a FAIL. x86-64 only; native run pending.
// Build: eac-tests/ud-trap/build.sh [clang]   Run: ./ud_trap_test   (exit status = number of failed checks)

#if !defined(__x86_64__)
#error "x86-64 only"
#endif

typedef unsigned long u64;
typedef long i64;

static inline i64 sc6(i64 nr, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
  i64 ret;
  register i64 r10 __asm__("r10") = d;
  register i64 r8 __asm__("r8") = e;
  register i64 r9 __asm__("r9") = f;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
  return ret;
}
#define sc1(n, a) sc6(n, (i64)(a), 0, 0, 0, 0, 0)
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
#define sc4(n, a, b, c, d) sc6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)

enum { SYS_write = 1, SYS_mmap = 9, SYS_mprotect = 10, SYS_rt_sigaction = 13, SYS_alarm = 37, SYS_exit_group = 231 };
enum { SIGILL = 4, SIGALRM = 14, ILL_ILLOPN = 2, X86_TRAP_UD = 6 };
#define SA_SIGINFO 0x4
#define SA_RESTORER 0x04000000
enum { GREGS_OFF = 40, RIP = 16, TRAPNO = 20 };

struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }

static u64 slen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static void out(const char* s) { sc3(SYS_write, 1, s, slen(s)); }
static int failures;
static void check(int ok, const char* what) {
  out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures;
}

static volatile int h_count, h_code;
static volatile u64 h_addr, h_rip, h_trapno, skip;
static void handler(int sig, void* si, void* uc) {
  (void)sig;
  const char* s = si;
  h_code = *(const int*)(s + 8);
  h_addr = *(const u64*)(s + 16);
  u64* gregs = (u64*)((char*)uc + GREGS_OFF);
  h_rip = gregs[RIP];
  h_trapno = gregs[TRAPNO];
  gregs[RIP] += skip;  // step over it
  ++h_count;
}

// Runs the instruction at `at` (followed by a ret) and checks what the SIGILL said.
static void row(unsigned char* at, u64 len, const char* name) {
  h_count = 0;
  skip = len;
  __asm__ volatile("sub $128, %%rsp\n call *%0\n add $128, %%rsp" : : "r"(at) : "rax", "rcx", "rdx", "rsi", "rdi", "r8",
                   "r9", "r10", "r11", "memory");
  out(name); out("\n");
  check(h_count == 1, "  one SIGILL, and execution continues past the instruction");
  check(h_code == ILL_ILLOPN, "  si_code ILL_ILLOPN");
  check(h_addr == (u64)at && h_rip == (u64)at, "  si_addr and the ucontext RIP are the instruction");
  check(h_trapno == X86_TRAP_UD, "  trapno 6 (#UD)");
}

static void on_alarm(int sig) {
  (void)sig;
  out("FAIL: hung (SIGALRM after 20 s)\nRESULT: FAIL\n");
  sc1(SYS_exit_group, 99);
}

__attribute__((used)) void entry_c(void) {
  struct ksigaction al = {(void*)on_alarm, SA_RESTORER, (void*)restore_rt, 0};
  sc4(SYS_rt_sigaction, SIGALRM, &al, 0, 8);
  sc1(SYS_alarm, 20);
  struct ksigaction sa = {(void*)handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  check(sc4(SYS_rt_sigaction, SIGILL, &sa, 0, 8) == 0, "SIGILL handler installed");
  unsigned char* page = (unsigned char*)sc6(SYS_mmap, 0, 4096, 7, 0x22, -1, 0);
  page[0] = 0x60; page[1] = 0xc3;                        // pusha (invalid in 64-bit code); ret
  page[16] = 0x8e; page[17] = 0xc8; page[18] = 0xc3;     // mov cs, ax; ret
  row(page, 1, "60 (PUSHA in 64-bit code)");
  row(page + 16, 2, "8e c8 (mov cs, ax)");

  // A code page made RWX (hot under FEX), its code then rewritten: a ret at 0 that runs first, then the two rows.
  unsigned char* hot = (unsigned char*)sc6(SYS_mmap, 0, 4096, 7, 0x22, -1, 0);
  hot[0] = 0xc3;
  sc3(SYS_mprotect, hot, 4096, 5);  // r-x, as an image's code
  __asm__ volatile("sub $128, %%rsp\n call *%0\n add $128, %%rsp" : : "r"(hot) : "memory");
  sc3(SYS_mprotect, hot, 4096, 7);  // rwx, as the protector's NtProtectVirtualMemory(PAGE_EXECUTE_READWRITE)
  hot[32] = 0x16; hot[33] = 0xc3;                         // push ss (invalid in 64-bit code); ret
  hot[48] = 0x60; hot[49] = 0xc3;
  row(hot + 32, 1, "16 (PUSH SS in 64-bit code) on a page made RWX and rewritten");
  row(hot + 48, 1, "60 on a page made RWX and rewritten");
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
