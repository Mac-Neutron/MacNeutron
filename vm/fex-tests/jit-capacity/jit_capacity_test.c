// SPDX-License-Identifier: MIT
//
// Regression test for a capacity limit of FEX's JIT that a large self-modifying program hits (Age of Empires II DE writes
// to ~2,900 of its code pages over and over). It measures FEX through its own statistics (FEX_PROFILESTATS=1,
// /dev/shm/fex-<pid>-stats: the binary re-execs itself with it) and checks results that hold on any x86-64 Linux:
//   hot pages: 2,048 code pages, each a small function and a data word; 128 rounds call every function and write every
//     data word. Under write protection (SMCChecks=mtrack) each write faults until the page has taken SMCHotPageFaults
//     (default 32) faults and is hot, no longer write-protected (patch 0002). A hot set of at most 1,536 pages left the
//     other pages faulting in every round: at most 40 faults per page are allowed. Every 32 rounds the functions are
//     rewritten in place and must return their new values.
// Natively, or without the statistics file, the measurement reports SKIP. x86-64 only; native run pending.
// Build: eac-tests/jit-capacity/build.sh [clang]   Run: ./jit_capacity_test   (exit status = number of failed checks)

#if !defined(__x86_64__)
#error "x86-64 only"
#endif

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef unsigned char u8;

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
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)

enum { SYS_read = 0, SYS_write = 1, SYS_open = 2, SYS_close = 3, SYS_mmap = 9, SYS_getpid = 39, SYS_execve = 59, SYS_gettid = 186,
       SYS_clock_gettime = 228, SYS_exit_group = 231 };

static void out(const char* s) { u64 n = 0; while (s[n]) ++n; sc3(SYS_write, 1, s, n); }
static void num(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } out(b + i); }
static int failures;
static void check(int ok, const char* what) { out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures; }
static int seq(const char* a, const char* b) { while (*a && *a == *b) ++a, ++b; return *a == *b; }
static void ucopy(char* d, const char* s) { while ((*d++ = *s++)); }
static void* map_rwx(u64 size) { return (void*)sc6(SYS_mmap, 0, (i64)size, 7, 0x22, -1, 0); } // RWX, private anonymous

// FEX's per-thread statistics (FEXCore/include/FEXCore/Utils/SHMStats.h): a 64-byte header whose u16 at 2 is the slot
// size, then slots {u32 Next, u32 TID, u64 counters...}. Counter 3 is SMC write faults.
enum { STAT_SMC = 3 };
static u8 statbuf[1 << 20];
static int stat_read(u64 index, u64* value) {
  char path[48] = "/dev/shm/fex-"; char* p = path + 13;
  char d[24]; int i = 23; d[i] = 0; u64 pid = sc0(SYS_getpid); do d[--i] = '0' + pid % 10; while (pid /= 10);
  ucopy(p, d + i); while (*p) ++p; ucopy(p, "-stats");
  i64 fd = sc3(SYS_open, path, 0, 0);
  if (fd < 0) return 0;
  i64 n = sc3(SYS_read, fd, statbuf, sizeof statbuf);
  sc1(SYS_close, fd);
  if (n < 64) return 0;
  u64 slot = *(unsigned short*)(statbuf + 2), tid = sc0(SYS_gettid), sum = 0;
  if (slot < 8 + 8 * (index + 1)) return 0;
  for (u64 o = 64; o + slot <= (u64)n; o += slot)
    if (*(u32*)(statbuf + o + 4) == tid) sum += *(u64*)(statbuf + o + 8 + 8 * index);
  *value = sum;
  return 1;
}

// --- hot pages ------------------------------------------------------------------------------------------------------
enum { HOT_PAGES = 2048, HOT_ROUNDS = 128, HOT_REWRITE = 32, HOT_MAX_FAULTS_PER_PAGE = 40 };
static void hot_pages(void) {
  u8* m = map_rwx((u64)HOT_PAGES * 4096);
  u64 before = 0, after = 0;
  int stats = stat_read(STAT_SMC, &before), right = 1;
  for (int r = 0; r < HOT_ROUNDS; ++r) {
    u32 gen = (u32)(r / HOT_REWRITE);
    for (u32 i = 0; i < HOT_PAGES; ++i) {
      u8* f = m + (u64)i * 4096;
      if (r % HOT_REWRITE == 0) { f[0] = 0xb8; *(u32*)(f + 1) = i + 100000 * gen; f[5] = 0xc3; } // mov eax, imm32; ret
      u32 got = ((u32(*)(void))f)();
      if (got != i + 100000 * gen) right = 0;
      *(volatile u64*)(f + 2048) += 1;   // a data write on the code page
    }
  }
  check(right, "hot pages: every call returns its function's current value, rewritten every 32 rounds");
  if (stats && stat_read(STAT_SMC, &after)) {
    u64 faults = after - before;
    out("  SMC write faults: "); num(faults); out(" for "); num(HOT_PAGES); out(" pages, "); num(HOT_ROUNDS); out(" rounds\n");
    check(faults <= (u64)HOT_PAGES * HOT_MAX_FAULTS_PER_PAGE, "hot pages: every page stops faulting once it is hot (<= 40 faults a page)");
  } else {
    out("SKIP: hot pages' fault count (no FEX statistics)\n");
  }
}

static void run(void) {
  hot_pages();
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}

// entry: re-exec with FEX_PROFILESTATS=1 so that FEX publishes the statistics the rows read
static char* newenv[256];
__attribute__((used)) static void entry_c(u64* sp) {
  u64 argc = sp[0]; char** argv = (char**)(sp + 1); char** envp = argv + argc + 1;
  int have = 0, n = 0;
  for (char** e = envp; *e && n < 254; ++e) {
    if (seq(*e, "FEX_PROFILESTATS=1")) have = 1;
    newenv[n++] = *e;
  }
  if (!have) {
    newenv[n++] = "FEX_PROFILESTATS=1";
    newenv[n] = 0;
    sc3(SYS_execve, "/proc/self/exe", argv, newenv);
    // execve returned: continue; the measurements report SKIP without the statistics
  }
  run();
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("mov %rsp, %rdi\n and $-16, %rsp\n call entry_c\n"); }
