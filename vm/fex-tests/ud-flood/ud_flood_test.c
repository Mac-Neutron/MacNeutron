// SPDX-License-Identifier: MIT
//
// Regression test for an asynchronous signal that lands while FEX raises a guest SIGILL. A thread executes ud2 in a
// loop with known values in every general register; its SIGILL handler checks them, clears them and moves the stack
// down 512 bytes to a trampoline that moves it back, the way Wine's exception dispatch and NtContinue do for Age of
// Empires II DE's nanomites. Another thread keeps sending it SIGUSR1 (Wine's NtSuspendThread). On Linux every SIGILL
// sees the loop's registers. FEX branches from the ud2 block to a dispatcher stub that saves the guest registers and
// then traps; a SIGUSR1 arriving while that stub saved them was given the registers of the previous SIGILL's return
// (cleared, stack 512 bytes down), and its rt_sigreturn made them the thread's. An alarm turns a hang into a FAIL.
// x86-64 only; native run pending.
// Build: eac-tests/ud-flood/build.sh [clang]   Run: ./ud_flood_test   (exit status = number of failed checks)

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

enum { SYS_write = 1, SYS_rt_sigaction = 13, SYS_sched_yield = 24, SYS_nanosleep = 35, SYS_getpid = 39, SYS_alarm = 37,
       SYS_gettid = 186, SYS_tgkill = 234, SYS_exit_group = 231 };
enum { SIGILL = 4, SIGUSR1 = 10, SIGALRM = 14 };
#define SA_SIGINFO 0x4
#define SA_RESTORER 0x04000000
// ucontext gregs: r8..r15, rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip.
enum { GREGS_OFF = 40, R8 = 0, R15 = 7, RDI = 8, RSI = 9, RBP = 10, RBX = 11, RSP = 15, RIP = 16 };
#define ROUNDS 20000

struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }
static u64 slen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static void out(const char* s) { sc3(SYS_write, 1, s, slen(s)); }
static void hex(u64 v) {
  char b[19] = "0x";
  for (int i = 0; i < 16; ++i) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
  b[18] = 0;
  out(b);
}
static int failures;
static void check(int ok, const char* what) { out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures; }

// What the loop puts in each checked register before its ud2 (index = gregs slot).
static const u64 magic[16] = {
  [R8] = 0x0808080808080808, 0x0909090909090909, 0x1010101010101010, 0x1111111111111111, 0x1212121212121212,
  0x1313131313131313, 0x1414141414141414, [R15] = 0x1515151515151515, [RDI] = 0x0707070707070707,
  [RSI] = 0x0606060606060606, [RBP] = 0x0505050505050505, [RBX] = 0x0303030303030303,
};
static const int checked[] = {R8, R8 + 1, R8 + 2, R8 + 3, R8 + 4, R8 + 5, R8 + 6, R15, RDI, RSI, RBP, RBX};
__attribute__((used)) volatile u64 remaining = ROUNDS, loop_rsp;
static volatile u64 ill_count, usr1_count, wrong;
static volatile int worker_tid;
__attribute__((used)) volatile int worker_done;

// The worker: every register but rax/rcx/rdx/rsp gets its magic value, then ud2. The SIGILL handler resumes it at
// ud_trampoline with the stack 512 bytes down, which the trampoline undoes in guest code.
__asm__(".text\n.balign 16\n.globl ud_loop\nud_loop:\n"
        "  mov %rsp, loop_rsp(%rip)\n"
        "1:\n"
        "  movabs $0x0808080808080808, %r8\n  movabs $0x0909090909090909, %r9\n"
        "  movabs $0x1010101010101010, %r10\n  movabs $0x1111111111111111, %r11\n"
        "  movabs $0x1212121212121212, %r12\n  movabs $0x1313131313131313, %r13\n"
        "  movabs $0x1414141414141414, %r14\n  movabs $0x1515151515151515, %r15\n"
        "  movabs $0x0707070707070707, %rdi\n  movabs $0x0606060606060606, %rsi\n"
        "  movabs $0x0505050505050505, %rbp\n  movabs $0x0303030303030303, %rbx\n"
        "  ud2\n"
        ".globl ud_trampoline\nud_trampoline:\n"
        "  add $512, %rsp\n"
        "  decq remaining(%rip)\n"
        "  jnz 1b\n"
        "  movl $1, worker_done(%rip)\n"
        "  mov $60, %eax\n  xor %edi, %edi\n  syscall\n");
extern char ud_loop[], ud_trampoline[];

static void ill_handler(int sig, void* si, void* uc) {
  (void)sig; (void)si;
  u64* g = (u64*)((char*)uc + GREGS_OFF);
  int bad = g[RSP] != loop_rsp;
  for (unsigned i = 0; i < sizeof checked / sizeof checked[0]; ++i) bad |= g[checked[i]] != magic[checked[i]];
  if (bad && !wrong) {
    out("     first wrong SIGILL: rsp "); hex(g[RSP]); out(" (loop "); hex(loop_rsp); out("), rbx "); hex(g[RBX]);
    out(", r15 "); hex(g[R15]); out(", rsi "); hex(g[RSI]); out("\n");
  }
  wrong += bad;
  for (unsigned i = 0; i < sizeof checked / sizeof checked[0]; ++i) g[checked[i]] = 0;
  g[RSP] = loop_rsp - 512;  // also repairs a wrong one, so every SIGILL is checked on its own
  g[RIP] = (u64)ud_trampoline;
  ++ill_count;
}
static void usr1_handler(int sig, void* si, void* uc) { (void)sig; (void)si; (void)uc; ++usr1_count; }
static void on_alarm(int sig) { (void)sig; out("FAIL: hung (SIGALRM after 120 s)\nRESULT: FAIL\n"); sc1(SYS_exit_group, 99); }

static u64 worker_stack[16384] __attribute__((aligned(16)));
static void worker(void) {
  worker_tid = (int)sc0(SYS_gettid);
  __asm__ volatile("jmp ud_loop" ::: "memory");
}
__asm__(".text\n.balign 16\n.globl spawn_thread\nspawn_thread:\n"
        "  mov %rsi, %r9\n  mov %rdi, %rsi\n  mov $0x50F00, %edi\n  xor %edx, %edx\n  xor %r10d, %r10d\n  xor %r8d, %r8d\n"
        "  mov $56, %eax\n  syscall\n  test %rax, %rax\n  jnz 3f\n  call *%r9\n  mov $60, %eax\n  xor %edi, %edi\n  syscall\n"
        "3:\n  ret\n");
extern i64 spawn_thread(u64 stacktop, void (*fn)(void));

__attribute__((used)) void entry_c(void) {
  const i64 pid = sc0(SYS_getpid);
  struct ksigaction al = {(void*)on_alarm, SA_RESTORER, (void*)restore_rt, 0};
  struct ksigaction ill = {(void*)ill_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  struct ksigaction usr1 = {(void*)usr1_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  sc4(SYS_rt_sigaction, SIGALRM, &al, 0, 8);
  sc4(SYS_rt_sigaction, SIGILL, &ill, 0, 8);
  sc4(SYS_rt_sigaction, SIGUSR1, &usr1, 0, 8);
  sc1(SYS_alarm, 120);
  if (spawn_thread((u64)(worker_stack + 16384), worker) > 0) {
    while (!worker_tid) sc0(SYS_sched_yield);
    while (!worker_done) sc3(SYS_tgkill, pid, worker_tid, SIGUSR1);
  }
  struct { i64 s, ns; } settle = {0, 100000000};
  sc2(SYS_nanosleep, &settle, 0);
  check(ill_count == ROUNDS, "20000 ud2s under a SIGUSR1 flood: one SIGILL each");
  check(wrong == 0, "every SIGILL saw the loop's registers and stack pointer");
  check(usr1_count > 0, "the SIGUSR1s were handled");
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
