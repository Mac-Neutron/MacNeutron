// SPDX-License-Identifier: MIT
//
// Regression test: a load whose result nothing uses still faults under FEX.
//
// x86 contract: an instruction with a memory operand reads it, and an unmapped address faults, whether or not anything
// uses what it read. FEX's IR treated LoadMem as free of side effects, so once the flags of `cmp (%rcx),%rax` went
// unused its dead-code pass dropped the load and the fault with it. HotSpot's interpreter null-checks a receiver with
// exactly that probe (MacroAssembler::null_check: "cmpptr(rax, Address(reg, 0))", flags unused) and turns the fault
// into a NullPointerException; without the fault a null receiver reached the method, whose own field load then faulted
// where the JVM expects none (3DMark's Java backend: EXCEPTION_ACCESS_VIOLATION in String.trim()).
//
// Two checks, each in a child whose SIGSEGV handler reports the fault and exits:
//   1. probe -- `cmp (%rcx),%rax` with rcx = 0, its flags overwritten by the next instruction
//   2. dead  -- `mov (%rcx),%rdx` with rcx = 0, rdx overwritten by the next instruction
// The code pages are hand-written so no compiler can remove the loads first:
//   48 31 c9        xor %rcx,%rcx
//   48 3b 01        cmp (%rcx),%rax       (probe)   or   48 8b 11   mov (%rcx),%rdx   (dead)
//   31 c0 / 31 d2   xor %eax,%eax (the flags) / xor %edx,%edx (rdx)
//   c3              ret
//
// Build: eac-tests/dead-load/build.sh [clang]   (x86-64 binary, run it under FEX)
// Run:   ./dead_load_test   (exit status = number of failed checks; prints RESULT: PASS/FAIL)

typedef unsigned long u64;
typedef long i64;

static inline i64 sc4(i64 nr, i64 a, i64 b, i64 c, i64 d) {
  i64 ret;
  register i64 r10 __asm__("r10") = d;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
  return ret;
}
static inline i64 sc6(i64 nr, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
  i64 ret;
  register i64 r10 __asm__("r10") = d;
  register i64 r8 __asm__("r8") = e;
  register i64 r9 __asm__("r9") = f;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
  return ret;
}

#define SYS_write 1
#define SYS_mmap 9
#define SYS_rt_sigaction 13
#define SYS_rt_sigreturn 15
#define SYS_fork 57
#define SYS_wait4 61
#define SYS_exit_group 231
#define SIGSEGV 11
#define SA_SIGINFO 4
#define SA_RESTORER 0x04000000

static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void out(const char *s) { sc4(SYS_write, 1, (i64)s, slen(s), 0); }

struct ksigaction { void *handler; u64 flags; void *restorer; u64 mask; };

static void on_segv(int sig, void *info, void *uc) {
  (void)sig, (void)info, (void)uc;
  sc4(SYS_exit_group, 0, 0, 0, 0); // faulted, as on x86
}
static void restorer(void) { __asm__ volatile("mov %0, %%eax; syscall" : : "i"(SYS_rt_sigreturn)); }

// A child runs code: 0 when it faulted, 1 when the code returned.
static int faults(const unsigned char *code, unsigned len) {
  i64 pid = sc4(SYS_fork, 0, 0, 0, 0);
  if (pid == 0) {
    struct ksigaction sa = {(void *)on_segv, SA_SIGINFO | SA_RESTORER, (void *)restorer, 0};
    sc4(SYS_rt_sigaction, SIGSEGV, (i64)&sa, 0, 8);
    unsigned char *page = (unsigned char *)sc6(SYS_mmap, 0, 4096, 7, 0x22, -1, 0);
    for (unsigned i = 0; i < len; i++) page[i] = code[i];
    ((void (*)(void))page)();
    sc4(SYS_exit_group, 1, 0, 0, 0);
  }
  int status = 0;
  sc4(SYS_wait4, pid, (i64)&status, 0, 0);
  return (status >> 8) & 0xff;
}

static int failures;
static void check(const char *name, const unsigned char *code, unsigned len) {
  int r = faults(code, len);
  out(r ? "FAIL: " : "PASS: ");
  out(name);
  out(r ? " returned without faulting\n" : " faulted\n");
  failures += r != 0;
}

void _start(void) {
  static const unsigned char probe[] = {0x48, 0x31, 0xc9, 0x48, 0x3b, 0x01, 0x31, 0xc0, 0xc3};
  static const unsigned char dead[] = {0x48, 0x31, 0xc9, 0x48, 0x8b, 0x11, 0x31, 0xd2, 0xc3};
  check("probe (cmp (%rcx),%rax, flags unused)", probe, sizeof probe);
  check("dead (mov (%rcx),%rdx, rdx overwritten)", dead, sizeof dead);
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc4(SYS_exit_group, failures, 0, 0, 0);
}
