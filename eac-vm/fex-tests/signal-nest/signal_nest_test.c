// SPDX-License-Identifier: MIT
//
// Regression test for a signal arriving while FEX itself handles another one. A thread makes syscalls that syscall user
// dispatch turns into SIGSYS (its handler masks nothing; the program has no SIGILL handler) while another thread keeps
// sending it SIGUSR1 (Wine's NtSuspendThread / NtGetContextThread do this). On Linux the SIGUSR1s simply interleave with
// the SIGSYSs: rt_sigreturn can't be interrupted. FEX implements a guest rt_sigreturn in a host SIGILL handler; a SIGUSR1
// landing inside it was treated as interrupted C++ code and its frame written over the host signal stack in use, and
// the process died by SIGSEGV within seconds. The thread also exits while still being signalled. An alarm turns a hang
// into a FAIL. x86-64 only; native run pending.
// Build: eac-tests/signal-nest/build.sh [clang]   Run: ./signal_nest_test   (exit status = number of failed checks)

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
#define sc0(n) sc6(n, 0, 0, 0, 0, 0, 0)
#define sc1(n, a) sc6(n, (i64)(a), 0, 0, 0, 0, 0)
#define sc2(n, a, b) sc6(n, (i64)(a), (i64)(b), 0, 0, 0, 0)
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
#define sc4(n, a, b, c, d) sc6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)
#define sc5(n, a, b, c, d, e) sc6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), (i64)(e), 0)

enum { SYS_write = 1, SYS_mmap = 9, SYS_rt_sigaction = 13, SYS_sched_yield = 24, SYS_nanosleep = 35, SYS_getpid = 39,
       SYS_alarm = 37, SYS_exit = 60, SYS_gettid = 186, SYS_tgkill = 234, SYS_prctl = 157, SYS_exit_group = 231 };
enum { SIGUSR1 = 10, SIGALRM = 14, SIGSYS = 31 };
#define SA_SIGINFO 0x4
#define SA_RESTORER 0x04000000
#define MAGIC 0x5A5A
enum { GREGS_OFF = 40, RAX = 13 };

struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }
static u64 slen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static void out(const char* s) { sc3(SYS_write, 1, s, slen(s)); }
static int failures;
static void check(int ok, const char* what) { out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures; }

static unsigned char* gadget;
static i64 call_gadget(i64 nr) {
  i64 ret;
  __asm__ volatile("sub $128, %%rsp\n call *%[g]\n add $128, %%rsp" : "=a"(ret) : [g] "r"(gadget), "a"(nr) : "rcx", "r11", "memory");
  return ret;
}
static volatile u64 sys_count, usr1_count, calls, wrong;
static volatile int worker_tid, worker_done;
static void sys_handler(int sig, void* si, void* uc) { (void)sig; (void)si; ((u64*)((char*)uc + GREGS_OFF))[RAX] = MAGIC; ++sys_count; }
static void usr1_handler(int sig, void* si, void* uc) { (void)sig; (void)si; (void)uc; ++usr1_count; }
static void on_alarm(int sig) { (void)sig; out("FAIL: hung (SIGALRM after 120 s)\nRESULT: FAIL\n"); sc1(SYS_exit_group, 99); }

static u64 worker_stack[16384] __attribute__((aligned(16)));
static void worker(void) {
  worker_tid = (int)sc0(SYS_gettid);
  sc5(SYS_prctl, 59, 1, 0, (u64)gadget, 0);  // dispatch on: everything below the gadget runs, the gadget traps
  for (int i = 0; i < 20000; ++i) {
    if (call_gadget(SYS_getpid) != MAGIC) ++wrong;
    ++calls;
  }
  sc5(SYS_prctl, 59, 0, 0, 0, 0);
  worker_done = 1;  // and exits while SIGUSR1s keep coming
}
__asm__(".text\n.balign 16\n.globl spawn_thread\nspawn_thread:\n"
        "  mov %rsi, %r9\n  mov %rdi, %rsi\n  mov $0x50F00, %edi\n  xor %edx, %edx\n  xor %r10d, %r10d\n  xor %r8d, %r8d\n"
        "  mov $56, %eax\n  syscall\n  test %rax, %rax\n  jnz 3f\n  call *%r9\n  mov $60, %eax\n  xor %edi, %edi\n  syscall\n"
        "3:\n  ret\n");
extern i64 spawn_thread(u64 stacktop, void (*fn)(void));

__attribute__((used)) void entry_c(void) {
  const i64 pid = sc0(SYS_getpid);
  struct ksigaction al = {(void*)on_alarm, SA_RESTORER, (void*)restore_rt, 0};
  struct ksigaction sys = {(void*)sys_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  struct ksigaction usr1 = {(void*)usr1_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  sc4(SYS_rt_sigaction, SIGALRM, &al, 0, 8);
  sc4(SYS_rt_sigaction, SIGSYS, &sys, 0, 8);
  sc4(SYS_rt_sigaction, SIGUSR1, &usr1, 0, 8);
  sc1(SYS_alarm, 120);
  gadget = (unsigned char*)sc6(SYS_mmap, 0x600000000000L, 4096, 7, 0x22, -1, 0);
  gadget[0] = 0x0f; gadget[1] = 0x05; gadget[2] = 0xc3;
  if (spawn_thread((u64)(worker_stack + 16384), worker) > 0) {
    while (!worker_tid) sc0(SYS_sched_yield);
    while (!worker_done) sc3(SYS_tgkill, pid, worker_tid, SIGUSR1);
    for (int i = 0; i < 2000; ++i) sc3(SYS_tgkill, pid, worker_tid, SIGUSR1);  // into the exit (ESRCH once it's gone)
  }
  struct { i64 s, ns; } settle = {0, 100000000};
  sc2(SYS_nanosleep, &settle, 0);
  check(calls == 20000 && wrong == 0 && sys_count == 20000,
        "20000 dispatched syscalls under a SIGUSR1 flood: each one SIGSYS, none lost or doubled");
  check(usr1_count > 0, "the SIGUSR1s were handled");
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
