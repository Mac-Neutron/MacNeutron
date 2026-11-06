// SPDX-License-Identifier: MIT
//
// Regression test: open, openat and openat2 with an unreadable path pointer return -EFAULT, as on Linux.
//
// The Easy Anti-Cheat Linux launcher injects its client by stopping the child at a syscall-entry stop, loading the
// registers for a dlopen() call (rdi the path, rsi the flags) and resuming. On Linux the interrupted syscall still runs
// with those registers: when it is an openat, the "path" is the small flags value, the kernel's copy_from_user fails,
// the call returns -EFAULT and the child continues at the new rip. FEX dereferenced the guest pointer on the host,
// faulted inside its openat handler and delivered SIGSEGV to the guest, killing the child (launcher result 210, #1).
//
// Expected: passes on a real Linux kernel and on the patched FEX. On an unpatched FEX the process dies at the first
// call and the RESULT line never prints.
//
// Build: eac-tests/open-efault/build.sh [clang]   (x86-64 binary, run it under FEX)   or   build.sh native (host arch)
// Run:   ./open_efault_test   (exit status = number of failed checks)

typedef unsigned long u64;
typedef long i64;

#if defined(__x86_64__)
static inline i64 sc6(i64 nr, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
  i64 ret;
  register i64 r10 __asm__("r10") = d;
  register i64 r8 __asm__("r8") = e;
  register i64 r9 __asm__("r9") = f;
  __asm__ volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
  return ret;
}
enum { SYS_write = 1, SYS_open = 2, SYS_getpid = 39, SYS_exit_group = 231, SYS_openat = 257, SYS_openat2 = 437 };
#elif defined(__aarch64__)
static inline i64 sc6(i64 nr, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
  register i64 x8 __asm__("x8") = nr;
  register i64 x0 __asm__("x0") = a;
  register i64 x1 __asm__("x1") = b;
  register i64 x2 __asm__("x2") = c;
  register i64 x3 __asm__("x3") = d;
  register i64 x4 __asm__("x4") = e;
  register i64 x5 __asm__("x5") = f;
  __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
  return x0;
}
// aarch64 has no open(2); the reference build checks openat and openat2 only.
enum { SYS_write = 64, SYS_open = -1, SYS_getpid = 172, SYS_exit_group = 94, SYS_openat = 56, SYS_openat2 = 437 };
#else
#error "x86-64 and aarch64 only"
#endif

#define sc1(n, a) sc6(n, (i64)(a), 0, 0, 0, 0, 0)
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)
#define sc4(n, a, b, c, d) sc6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)

static u64 slen(const char* s) { u64 n = 0; while (s[n]) ++n; return n; }
static void out(const char* s) { sc3(SYS_write, 1, s, slen(s)); }

static int failures;
static void check(int ok, const char* what) {
  out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures;
}

enum { AT_FDCWD = -100, EFAULT = 14, RTLD_NOW = 2, RTLD_GLOBAL_NOW = 0x102 };
struct open_how { u64 flags, mode, resolve; };

__attribute__((used)) static void entry_c(void) {
  i64 pid = sc1(SYS_getpid, 0);

  // The launcher's case: openat whose "path" is dlopen's flags (RTLD_NOW, RTLD_NOW|RTLD_GLOBAL).
  check(sc4(SYS_openat, AT_FDCWD, RTLD_NOW, 0, 0) == -EFAULT, "openat(AT_FDCWD, (char*)2) returns -EFAULT");
  check(sc4(SYS_openat, AT_FDCWD, RTLD_GLOBAL_NOW, 0, 0) == -EFAULT, "openat(AT_FDCWD, (char*)0x102) returns -EFAULT");
  // A dirfd that is no descriptor at all, as the launcher's string address truncated to an int.
  check(sc4(SYS_openat, (int)0xffffdd90, RTLD_NOW, 0, 0) == -EFAULT, "openat(bad dirfd, (char*)2) returns -EFAULT");
  if (SYS_open >= 0) check(sc3(SYS_open, 8, 0, 0) == -EFAULT, "open((char*)8) returns -EFAULT");
  struct open_how how = {0, 0, 0};
  check(sc4(SYS_openat2, AT_FDCWD, RTLD_NOW, &how, sizeof(how)) == -EFAULT, "openat2(AT_FDCWD, (char*)2) returns -EFAULT");

  check(sc1(SYS_getpid, 0) == pid, "the process survives (getpid is unchanged)");
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}
#if defined(__x86_64__)
__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
#else
__asm__(".text\n.globl _start\n_start:\n bl entry_c\n");
#endif
