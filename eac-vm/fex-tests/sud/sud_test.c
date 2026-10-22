// SPDX-License-Identifier: MIT
//
// Regression test for syscall user dispatch (prctl PR_SET_SYSCALL_USER_DISPATCH), which Wine's ntdll enables on x86_64
// (dlls/ntdll/unix/signal_x86_64.c, init_syscall_frame / sigsys_handler) so that a `syscall` a Windows program makes from
// its own code reaches Wine's dispatcher as a SIGSYS: range = libc's mapping, selector = a byte in the TEB that is 0
// (ALLOW) while Wine's Unix side runs and 1 (BLOCK) in PE code. Anti-tamper code (Age of Empires II DE's) does exactly
// that from a page it writes itself.
//
// The expected results are Linux 6.11's kernel/entry/syscall_user_dispatch.c and prctl:
//   1-6   prctl arguments: OFF takes no arguments, unknown modes fail, a non-empty range must not wrap, the selector
//         must be a user address (EINVAL / EFAULT), offset 0 and length 0 are accepted
//   7-8   a syscall whose ip (the address after the instruction) is inside [offset, offset + len) runs, even when the
//         selector says BLOCK
//   9-11  outside it: SIGSYS with si_code SYS_USER_DISPATCH (2), si_errno 0, si_call_addr = ip, si_syscall, si_arch,
//         the ucontext at ip with RAX = the number; the syscall does not run; the handler's RAX is the result; 1000
//         and then 100000 dispatches
//   12-14 the selector: ALLOW runs, BLOCK dispatches; prctl OFF ends dispatch; a handler that turns dispatch off and
//         moves RIP back 2 (Wine's fallback) runs the syscall again
//   15    the process is killed: by SIGSYS for a selector value other than 0/1, by SIGSEGV for an unreadable selector,
//         by SIGSYS when SIGSYS is blocked or ignored (force_sig_info)
//   16-17 a fork child and a new thread start with dispatch off
//   18    a thread dispatching in a loop while another keeps sending it SIGUSR1 (Wine's NtSuspendThread and
//         NtGetContextThread) loses no dispatched syscall to the other signal
// The handler runs on a sigaltstack (SA_ONSTACK, as Wine's). x86-64 only; native run pending.
// Build: eac-tests/sud/build.sh [clang]   Run: ./sud_test   (exit status = number of failed checks)

#if !defined(__x86_64__)
#error "x86-64 only: the test executes x86-64 syscall gadgets"
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

enum { SYS_read = 0, SYS_write = 1, SYS_mmap = 9, SYS_munmap = 11, SYS_rt_sigaction = 13, SYS_rt_sigprocmask = 14,
       SYS_nanosleep = 35, SYS_getpid = 39, SYS_clone = 56, SYS_fork = 57, SYS_exit = 60, SYS_wait4 = 61,
       SYS_sched_yield = 24, SYS_gettid = 186, SYS_tgkill = 234, SYS_sigaltstack = 131, SYS_prctl = 157,
       SYS_exit_group = 231, SYS_pipe2 = 293 };
enum { SIGUSR1 = 10, SIGSEGV = 11, SIGSYS = 31 };
#define SA_SIGINFO 0x4
#define SA_ONSTACK 0x08000000
#define SA_RESTORER 0x04000000
enum { PR_SET_SYSCALL_USER_DISPATCH = 59, PR_SYS_DISPATCH_OFF = 0, PR_SYS_DISPATCH_ON = 1 };
enum { SYS_USER_DISPATCH = 2 };
#define AUDIT_ARCH_X86_64 0xC000003Eu
enum { EINVAL = 22, EFAULT = 14, EAGAIN = 11 };
enum { GREGS_OFF = 40, RAX = 13, RIP = 16 };
#define MAGIC 0x5A5A

struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
struct kstack { void* sp; int flags; u64 size; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }

static u64 slen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static void out(const char* s) { sc3(SYS_write, 1, s, slen(s)); }
static void outhex(u64 v) {
  char b[19]; b[0] = '0'; b[1] = 'x'; int i;
  for (i = 0; i < 16; ++i) { int d = (v >> (60 - 4 * i)) & 15; b[2 + i] = d < 10 ? '0' + d : 'a' + d - 10; }
  b[18] = 0; out(b);
}
static int failures;
static void check(int ok, const char* what) {
  out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures;
}

// ------------------------------------------------------------------------------------------------------
// The gadget: `syscall; ret` on a page of its own, far above the program (a static non-PIE binary, so a range
// [0, gadget) holds every syscall but the gadget's). ip = gadget + 2.
// ------------------------------------------------------------------------------------------------------
static unsigned char* gadget;
static u64 ip;
static i64 call_gadget(i64 nr, i64 a, i64 b, i64 c) {
  i64 ret;
  // The call pushes a return address: step over the red zone first.
  __asm__ volatile("sub $128, %%rsp\n call *%[g]\n add $128, %%rsp"
                   : "=a"(ret)
                   : [g] "r"(gadget), "a"(nr), "D"(a), "S"(b), "d"(c)
                   : "rcx", "r11", "memory");
  return ret;
}

static volatile char sel;   // the selector byte
static i64 sud(u64 mode, u64 offset, u64 len, const volatile char* selector) {
  return sc5(SYS_prctl, PR_SET_SYSCALL_USER_DISPATCH, mode, offset, len, selector);
}
static i64 sud_off(void) { return sud(PR_SYS_DISPATCH_OFF, 0, 0, 0); }

// ------------------------------------------------------------------------------------------------------
// The SIGSYS handler records what Linux hands it, then either answers MAGIC (Wine's dispatcher answers instead of the
// kernel) or, in rewind mode, turns dispatch off and backs RIP up onto the syscall (Wine's !syscall_dispatch_enabled).
// ------------------------------------------------------------------------------------------------------
static volatile u64 count, rewind;
static volatile int h_signo, h_errno, h_code, h_syscall;
static volatile unsigned h_arch;
static volatile u64 h_call_addr, h_rip, h_rax;
static void handler(int sig, void* si, void* uc) {
  (void)sig;
  sel = 0;
  const char* s = si;
  h_signo = *(const int*)(s + 0);
  h_errno = *(const int*)(s + 4);
  h_code = *(const int*)(s + 8);
  h_call_addr = *(const u64*)(s + 16);
  h_syscall = *(const int*)(s + 24);
  h_arch = *(const unsigned*)(s + 28);
  u64* gregs = (u64*)((char*)uc + GREGS_OFF);
  h_rip = gregs[RIP];
  h_rax = gregs[RAX];
  ++count;
  if (rewind) {
    sud_off();
    gregs[RIP] -= 2;
  } else {
    gregs[RAX] = MAGIC;
  }
}

static int wait_child(i64 pid) {
  int status = -1;
  sc4(SYS_wait4, pid, &status, 0, 0);
  return status;
}

// The thread of row 17, raw clone as in signal-race's spawn_thread.
static u64 thread_stack[16384] __attribute__((aligned(16)));
static volatile i64 thread_result;
static volatile int thread_done;
static void thread_fn(void) {
  thread_result = call_gadget(SYS_getpid, 0, 0, 0);
  thread_done = 1;
}
__asm__(".text\n.balign 16\n.globl spawn_thread\nspawn_thread:\n"   // i64(stacktop, fn)
        "  mov %rsi, %r9\n  mov %rdi, %rsi\n  mov $0x50F00, %edi\n"
        "  xor %edx, %edx\n  xor %r10d, %r10d\n  xor %r8d, %r8d\n"
        "  mov $56, %eax\n  syscall\n"
        "  test %rax, %rax\n  jnz 3f\n"
        "  call *%r9\n"
        "  mov $60, %eax\n  xor %edi, %edi\n  syscall\n"
        "3:\n  ret\n");
extern i64 spawn_thread(u64 stacktop, void (*fn)(void));

// Row 18's worker: dispatch is per thread, so it turns it on itself.
static u64 race_stack[16384] __attribute__((aligned(16)));
static volatile int race_tid, race_done;
static volatile u64 race_calls, race_lost, usr1_count;
static void usr1_handler(int sig, void* si, void* uc) { (void)sig; (void)si; (void)uc; ++usr1_count; }
static void race_fn(void) {
  race_tid = (int)sc0(SYS_gettid);
  sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
  for (int i = 0; i < 20000; ++i) {
    if (call_gadget(SYS_getpid, 0, 0, 0) != MAGIC) ++race_lost;
    ++race_calls;
  }
  sud_off();
  // ponytail: SIGUSR1 blocked before exiting. A thread exiting under a SIGUSR1 flood after trapped syscalls can crash FEX
  // in the frame machinery seccomp traps share (not this patch's); unblock it here once that is fixed.
  u64 usr1 = 1UL << (SIGUSR1 - 1);
  sc4(SYS_rt_sigprocmask, 0 /* SIG_BLOCK */, &usr1, 0, 8);
  race_done = 1;
}

static char altstack[65536] __attribute__((aligned(16)));

__attribute__((used)) void entry_c(void) {
  const i64 pid = sc0(SYS_getpid);
  gadget = (unsigned char*)sc6(SYS_mmap, 0x600000000000L, 4096, 7, 0x22, -1, 0);
  gadget[0] = 0x0f; gadget[1] = 0x05; gadget[2] = 0xc3;
  ip = (u64)gadget + 2;
  check((u64)gadget > (u64)&entry_c && (i64)gadget > 0, "the gadget page lies above the program");

  struct kstack ss = {altstack, 0, sizeof altstack};
  // SIGUSR1 blocked while it runs, as in Wine's handlers (sa_mask = server_block_set).
  struct ksigaction sa = {(void*)handler, SA_SIGINFO | SA_RESTORER | SA_ONSTACK, (void*)restore_rt, 1UL << (SIGUSR1 - 1)};
  check(sc2(SYS_sigaltstack, &ss, 0) == 0 && sc4(SYS_rt_sigaction, SIGSYS, &sa, 0, 8) == 0,
        "SIGSYS handler installed on a sigaltstack");

  // 1-6: prctl arguments
  check(sud(PR_SYS_DISPATCH_OFF, 1, 0, 0) == -EINVAL, "1 OFF with an argument: -EINVAL");
  check(sud(7, 0, 0, 0) == -EINVAL, "2 unknown mode 7: -EINVAL");
  check(sud(PR_SYS_DISPATCH_ON, 0x1000, 0, 0) == -EINVAL, "3 ON, nonzero offset and length 0: -EINVAL");
  check(sud(PR_SYS_DISPATCH_ON, 0xFFFFFFFFFFFFF000UL, 0x2000, 0) == -EINVAL, "4 ON, a range that wraps: -EINVAL");
  check(sud(PR_SYS_DISPATCH_ON, 0, 0x1000, (const volatile char*)0xFFFF800000000000UL) == -EFAULT,
        "5 ON, a kernel selector address: -EFAULT");
  {
    sel = 0;
    const i64 on = sud(PR_SYS_DISPATCH_ON, 0, 0, &sel);
    const i64 p = sc0(SYS_getpid);
    const i64 off = sud_off();
    check(on == 0 && p == pid && off == 0, "6 ON with offset 0, length 0 and an ALLOW selector; syscalls run; OFF");
  }

  // 7-8: inside the range
  {
    const u64 c0 = count;
    sud(PR_SYS_DISPATCH_ON, 0, ip + 1, 0);
    const i64 r = call_gadget(SYS_getpid, 0, 0, 0);
    sud_off();
    check(r == pid && count == c0, "7 ip inside [0, ip + 1): the syscall runs");
  }
  {
    const u64 c0 = count;
    sel = 0;
    sud(PR_SYS_DISPATCH_ON, ip, 1, &sel);
    sel = 1;
    const i64 r = call_gadget(SYS_getpid, 0, 0, 0);
    sel = 0;
    sud_off();
    check(r == pid && count == c0, "8 ip == offset with the selector on BLOCK: the syscall runs (range first)");
  }

  // 9-11: outside the range, no selector
  {
    const u64 c0 = count;
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    const i64 r = call_gadget(SYS_getpid, 0, 0, 0);
    sud_off();
    check(r == MAGIC && count == c0 + 1, "9 ip == offset + len: dispatched, the handler's RAX is the result");
    check(h_signo == SIGSYS && h_errno == 0 && h_code == SYS_USER_DISPATCH, "9 si_signo SIGSYS, si_errno 0, si_code 2");
    check(h_call_addr == ip && h_syscall == SYS_getpid && h_arch == AUDIT_ARCH_X86_64,
          "9 si_call_addr = ip, si_syscall = 39, si_arch = AUDIT_ARCH_X86_64");
    check(h_rip == ip && h_rax == SYS_getpid, "9 the ucontext: RIP = ip, RAX = the syscall number");
    if (h_rip != ip) { out("   rip "); outhex(h_rip); out(" ip "); outhex(ip); out("\n"); }
  }
  {
    int fds[2] = {-1, -1};
    sc2(SYS_pipe2, fds, 0x800 /* O_NONBLOCK */);
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    const i64 r = call_gadget(SYS_write, fds[1], (i64)"x", 1);
    sud_off();
    char b;
    check(r == MAGIC && sc3(SYS_read, fds[0], &b, 1) == -EAGAIN, "10 a dispatched write() never runs");
  }
  {
    const u64 c0 = count;
    int ok = 1;
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    for (int i = 0; i < 1000; ++i)
      if (call_gadget(SYS_getpid, 0, 0, 0) != MAGIC) ok = 0;
    check(ok && count == c0 + 1000, "11 1000 dispatches");
    for (int i = 0; i < 100000; ++i) call_gadget(SYS_getpid, 0, 0, 0);
    sud_off();
    check(count == c0 + 101000, "11 100000 more dispatches complete (no host stack leaked per dispatch)");
  }

  // 12-14: the selector, OFF, the rewind
  {
    int ok = 1;
    sel = 0;
    sud(PR_SYS_DISPATCH_ON, 0, (u64)gadget, &sel);
    for (int i = 0; i < 10; ++i) {
      if (call_gadget(SYS_getpid, 0, 0, 0) != pid) ok = 0;
      sel = 1;
      const i64 r = call_gadget(SYS_getpid, 0, 0, 0);
      sel = 0;
      if (r != MAGIC) ok = 0;
    }
    sud_off();
    check(ok, "12 selector ALLOW runs the syscall, BLOCK dispatches it, 10 times each");
  }
  {
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    sud_off();
    check(call_gadget(SYS_getpid, 0, 0, 0) == pid, "13 after prctl OFF the gadget's syscall runs");
  }
  {
    const u64 c0 = count;
    rewind = 1;
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    const i64 r = call_gadget(SYS_getpid, 0, 0, 0);
    rewind = 0;
    sud_off();
    check(r == pid && count == c0 + 1, "14 a handler that turns dispatch off and backs RIP up 2 runs the syscall again, once");
  }

  // 15: kills
  {
    i64 child = sc0(SYS_fork);
    if (child == 0) {
      sel = 2;
      sud(PR_SYS_DISPATCH_ON, 0, ip, &sel);
      call_gadget(SYS_getpid, 0, 0, 0);
      sc1(SYS_exit_group, 100);
    }
    check((wait_child(child) & 0x7f) == SIGSYS, "15a a selector value of 2 kills the process with SIGSYS");
  }
  {
    char* page = (char*)sc6(SYS_mmap, 0, 4096, 3, 0x22, -1, 0);
    sc2(SYS_munmap, page, 4096);
    i64 child = sc0(SYS_fork);
    if (child == 0) {
      sud(PR_SYS_DISPATCH_ON, 0, ip, page);
      call_gadget(SYS_getpid, 0, 0, 0);
      sc1(SYS_exit_group, 100);
    }
    check((wait_child(child) & 0x7f) == SIGSEGV, "15b an unmapped selector kills the process with SIGSEGV");
  }
  {
    i64 child = sc0(SYS_fork);
    if (child == 0) {
      u64 mask = 1UL << (SIGSYS - 1);
      sc4(SYS_rt_sigprocmask, 0 /* SIG_BLOCK */, &mask, 0, 8);
      sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
      call_gadget(SYS_getpid, 0, 0, 0);
      sc1(SYS_exit_group, 100);
    }
    check((wait_child(child) & 0x7f) == SIGSYS, "15c with SIGSYS blocked, a dispatch kills the process with SIGSYS");
  }
  {
    i64 child = sc0(SYS_fork);
    if (child == 0) {
      struct ksigaction ign = {(void*)1 /* SIG_IGN */, SA_RESTORER, (void*)restore_rt, 0};
      sc4(SYS_rt_sigaction, SIGSYS, &ign, 0, 8);
      sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
      call_gadget(SYS_getpid, 0, 0, 0);
      sc1(SYS_exit_group, 100);
    }
    check((wait_child(child) & 0x7f) == SIGSYS, "15d with SIGSYS ignored, a dispatch kills the process with SIGSYS");
  }

  // 16-17: inheritance
  {
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    i64 child = sc0(SYS_fork);
    if (child == 0) sc1(SYS_exit_group, call_gadget(SYS_getpid, 0, 0, 0) == sc0(SYS_getpid) ? 0 : 1);
    const int status = wait_child(child);
    sud_off();
    check(status == 0, "16 a fork child starts with dispatch off");
  }
  {
    const u64 c0 = count;
    sud(PR_SYS_DISPATCH_ON, 0, ip, 0);
    const i64 tid = spawn_thread((u64)(thread_stack + 16384), thread_fn);
    struct { i64 s, ns; } one_ms = {0, 1000000};
    for (int waits = 0; tid > 0 && !thread_done && waits < 5000; ++waits) sc2(SYS_nanosleep, &one_ms, 0);
    sud_off();
    check(thread_done && thread_result == pid && count == c0, "17 a new thread starts with dispatch off");
  }

  // 18: no dispatched syscall lost to another signal
  {
    struct ksigaction usr1 = {(void*)usr1_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
    sc4(SYS_rt_sigaction, SIGUSR1, &usr1, 0, 8);
    const u64 c0 = count;
    if (spawn_thread((u64)(race_stack + 16384), race_fn) > 0) {
      while (!race_tid) sc0(SYS_sched_yield);
      // About one SIGUSR1 per dispatch: an unthrottled flood also crashes the shared frame machinery now and then.
      struct { i64 s, ns; } pause = {0, 20000};
      while (!race_done) {
        sc3(SYS_tgkill, pid, race_tid, SIGUSR1);
        sc2(SYS_nanosleep, &pause, 0);
      }
    }
    out("   18: "); outhex(race_lost); out(" lost of "); outhex(race_calls); out(", SIGUSR1 handled "); outhex(usr1_count);
    out(", SIGSYS handled "); outhex(count - c0); out("\n");
    check(race_calls == 20000 && race_lost == 0 && count - c0 == 20000 && usr1_count > 0,
          "18 20000 dispatches while SIGUSR1 keeps arriving: none lost");
  }

  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
