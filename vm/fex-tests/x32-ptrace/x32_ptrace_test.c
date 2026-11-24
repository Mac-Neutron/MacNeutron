// SPDX-License-Identifier: MIT
//
// Regression test for FEX 32-bit (x32) ptrace/wait4/waitpid compat ABI and
// /proc/<pid>/mem write JIT cache invalidation.
//
// Build: vm/fex-tests/x32-ptrace/build.sh [clang]   (i686 static binary, run under FEX)
// Run:   ./x32_ptrace_test                          (exit status = number of failed checks)

typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef unsigned char u8;

static inline i32 sc5(i32 nr, i32 a, i32 b, i32 c, i32 d, i32 e) {
  i32 ret;
  __asm__ volatile("int $0x80"
                   : "=a"(ret)
                   : "a"(nr), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                   : "memory");
  return ret;
}

#define sc1(n, a) sc5(n, (i32)(a), 0, 0, 0, 0)
#define sc2(n, a, b) sc5(n, (i32)(a), (i32)(b), 0, 0, 0)
#define sc3(n, a, b, c) sc5(n, (i32)(a), (i32)(b), (i32)(c), 0, 0)
#define sc4(n, a, b, c, d) sc5(n, (i32)(a), (i32)(b), (i32)(c), (i32)(d), 0)

enum {
  SYS_fork = 2,
  SYS_write = 4,
  SYS_close = 6,
  SYS_waitpid = 7,
  SYS_getpid = 20,
  SYS_ptrace = 26,
  SYS_kill = 37,
  SYS_wait4 = 114,
  SYS_pwrite64 = 181,
  SYS_exit_group = 252,
  SYS_openat = 295,
};

enum {
  PT_TRACEME = 0,
  PT_PEEKDATA = 2,
  PT_POKEDATA = 5,
  PT_CONT = 7,
  PT_DETACH = 17,
  PT_SYSCALL = 24,
  PT_SETOPTIONS = 0x4200,
  PT_GETREGSET = 0x4204,
};
enum { PT_O_TRACESYSGOOD = 1 };
enum { NT_PRSTATUS_ = 1 };
enum { SIGSTOP_ = 19 };
enum { AT_FDCWD_ = -100, O_RDWR_ = 2 };
enum { WALL = 0x40000000 };

#define WIFSTOPPED_(st) (((st) & 0xff) == 0x7f)
#define WSTOPSIG_(st) (((st) >> 8) & 0xff)
#define WIFEXITED_(st) (((st) & 0x7f) == 0)
#define WEXITSTATUS_(st) (((st) >> 8) & 0xff)

struct iov32 {
  u32 iov_base;
  u32 iov_len;
};

struct rusage32 {
  i32 raw[18];
};

static u32 slen(const char* s) {
  u32 n = 0;
  while (s[n]) ++n;
  return n;
}
static void out(const char* s) { sc3(SYS_write, 1, (i32)s, (i32)slen(s)); }
static void outdec(i32 v) {
  char b[16];
  int n = 0, neg = v < 0;
  u32 u = neg ? (u32)(-v) : (u32)v;
  if (!u) b[n++] = '0';
  while (u) {
    b[n++] = '0' + (u % 10);
    u /= 10;
  }
  if (neg) b[n++] = '-';
  char r[16];
  int k = 0;
  while (n) r[k++] = b[--n];
  r[k] = 0;
  out(r);
}

static int failures;
static void check(int ok, const char* what) {
  out(ok ? "PASS: " : "FAIL: ");
  out(what);
  out("\n");
  if (!ok) ++failures;
}

static volatile u32 peek_word = 0x12345678u;

__attribute__((noinline, aligned(16))) static u32 patchable_fn(void) {
  __asm__ volatile("" ::: "memory");
  return 0x11111111u;
}

static void fmt_proc_mem(char* buf, i32 pid) {
  const char* pfx = "/proc/";
  int k = 0;
  while (pfx[k]) {
    buf[k] = pfx[k];
    ++k;
  }
  char d[12];
  int n = 0;
  u32 u = (u32)pid;
  if (!u) d[n++] = '0';
  while (u) {
    d[n++] = '0' + (u % 10);
    u /= 10;
  }
  while (n) buf[k++] = d[--n];
  const char* sfx = "/mem";
  int j = 0;
  while (sfx[j]) buf[k++] = sfx[j++];
  buf[k] = 0;
}

static void child_role(void) {
  if (sc4(SYS_ptrace, PT_TRACEME, 0, 0, 0) != 0) sc1(SYS_exit_group, 90);
  sc2(SYS_kill, sc1(SYS_getpid, 0), SIGSTOP_);
  (void)sc1(SYS_getpid, 0);

  typedef u32 (*fn_t)(void);
  volatile fn_t fn = patchable_fn;
  u32 acc = 0;
  for (int i = 0; i < 10000; ++i) acc ^= fn();
  if (acc != 0) sc1(SYS_exit_group, 91);

  sc2(SYS_kill, sc1(SYS_getpid, 0), SIGSTOP_);

  u32 post = fn();
  sc1(SYS_exit_group, post == 0x22222222u ? 0 : 92);
}

__attribute__((used)) int entry_c(void) {
  i32 child = sc1(SYS_fork, 0);
  if (child == 0) {
    child_role();
    sc1(SYS_exit_group, 99);
  }
  check(child > 0, "fork");
  if (child <= 0) return failures;

  i32 st = 0;
  i32 wr = sc3(SYS_waitpid, child, (i32)&st, WALL);
  check(wr == child && WIFSTOPPED_(st) && WSTOPSIG_(st) == SIGSTOP_, "waitpid initial SIGSTOP");

  u32 regs[17];
  for (int i = 0; i < 17; ++i) regs[i] = 0;
  struct iov32 iov = { (u32)&regs[0], sizeof(regs) };
  i32 gr = sc4(SYS_ptrace, PT_GETREGSET, child, NT_PRSTATUS_, (i32)&iov);
  check(gr == 0 && iov.iov_len == sizeof(regs), "x32 PTRACE_GETREGSET(NT_PRSTATUS) size");

  u32 word = 0;
  i32 pk = sc4(SYS_ptrace, PT_PEEKDATA, child, (i32)&peek_word, (i32)&word);
  check(pk == 0 && word == 0x12345678u, "x32 PTRACE_PEEKDATA");

  sc4(SYS_ptrace, PT_SETOPTIONS, child, 0, PT_O_TRACESYSGOOD);
  check(sc4(SYS_ptrace, PT_SYSCALL, child, 0, 0) == 0, "x32 PTRACE_SYSCALL");
  st = 0;
  wr = sc3(SYS_waitpid, child, (i32)&st, WALL);
  check(wr == child && WIFSTOPPED_(st) && WSTOPSIG_(st) == (5 | 0x80), "waitpid syscall-enter SIGTRAP|0x80");
  for (int i = 0; i < 17; ++i) regs[i] = 0;
  iov.iov_len = sizeof(regs);
  gr = sc4(SYS_ptrace, PT_GETREGSET, child, NT_PRSTATUS_, (i32)&iov);
  check(gr == 0 && iov.iov_len == sizeof(regs) && regs[12] != 0 && regs[11] == SYS_getpid, "x32 PTRACE_GETREGSET(NT_PRSTATUS) syscall regs");

  i32 cr = sc4(SYS_ptrace, PT_CONT, child, 0, 0);
  check(cr == 0, "x32 PTRACE_CONT");

  struct rusage32 ru;
  for (int i = 0; i < 18; ++i) ru.raw[i] = 0;
  st = 0;
  i32 w4 = sc4(SYS_wait4, child, (i32)&st, WALL, (i32)&ru);
  check(w4 == child && WIFSTOPPED_(st) && WSTOPSIG_(st) == SIGSTOP_, "x32 wait4 second SIGSTOP");

  char mempath[32];
  fmt_proc_mem(mempath, child);
  i32 fd = sc4(SYS_openat, AT_FDCWD_, (i32)mempath, O_RDWR_, 0);
  check(fd >= 0, "open /proc/child/mem");
  if (fd >= 0) {
    // mov $0x22222222, %eax; ret
    u8 patch[6] = { 0xb8, 0x22, 0x22, 0x22, 0x22, 0xc3 };
    u32 target = (u32)&patchable_fn;
    i32 pw = sc5(SYS_pwrite64, fd, (i32)patch, sizeof(patch), (i32)target, 0);
    check(pw == (i32)sizeof(patch), "x32 pwrite64 /proc/child/mem");
    sc1(SYS_close, fd);
  }

  i32 dt = sc4(SYS_ptrace, PT_DETACH, child, 0, 0);
  check(dt == 0, "x32 PTRACE_DETACH");

  st = 0;
  wr = sc3(SYS_waitpid, child, (i32)&st, WALL);
  check(wr == child && WIFEXITED_(st) && WEXITSTATUS_(st) == 0, "child saw patched code after JIT warm");

  out("failures=");
  outdec(failures);
  out("\n");
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  return failures;
}

__asm__(
    ".text\n"
    ".globl _start\n"
    "_start:\n"
    "  xor %ebp, %ebp\n"
    "  and $-16, %esp\n"
    "  call entry_c\n"
    "  mov %eax, %ebx\n"
    "  mov $252, %eax\n"
    "  int $0x80\n");
