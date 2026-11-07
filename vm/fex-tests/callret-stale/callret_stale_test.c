// SPDX-License-Identifier: MIT
//
// Regression test for a return into code that was rewritten while the call was in progress, by a thread that entered
// the caller through another thread's block link. x86 runs the new code; FEX's call-return shadow stack kept the host
// address of the old translation for any thread whose own lookup cache had no entry for the rewritten page (it reached
// the block through a link another thread made), so that thread returned into stale code (patch 0043). Age of Empires
// II DE's protector rewrites code that way; once FEX's code buffer was large enough that it was no longer refilled (and
// every shadow stack emptied) every few seconds, the game's locks broke.
//   X (page 0): call A; ret        A (page 1): call G; mov eax, imm32; ret        G: wait for a flag, cpuid, ret
// Rows 1 and 2, 64 rounds each on fresh pages: thread U runs X once (FEX translates X and A and links X's call to A, all
// from U's lookups); thread T then runs X, follows U's link into A and parks in G; the main thread rewrites A's imm32
// and releases T, which must return the new value. Row 1: A calls G with call rel32; row 2: with call rax.
// Row 3, 128 rounds: four threads T enter A through U's link and park in G at once; A is rewritten and all four
// return through the same return point, each with the new value (call rel32 and call rax in turn).
// Rows 4 and 5, 16 rounds each: a later translation takes over lookup entries of the one a thread waits in, so the
// discarded translation must be found through the pages it covers, not through its entries (row 4: stale returns),
// and a translation that took over an entry of a discarded one must stay usable (row 5: the thread hung, its return
// point patched while the return block's entry still led to it).
// The binary re-execs itself with FEX_SMCHOTPAGEFAULTS=0: a hot page's per-instruction check would hide the stale
// returns. An alarm turns a hang into a FAIL. x86-64 only; native run pending.
// Build: eac-tests/callret-stale/build.sh [clang]   Run: ./callret_stale_test   (exit status = number of failed checks)

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
#define sc4(n, a, b, c, d) sc6(n, (i64)(a), (i64)(b), (i64)(c), (i64)(d), 0, 0)

enum { SYS_write = 1, SYS_mmap = 9, SYS_rt_sigaction = 13, SYS_sched_yield = 24, SYS_alarm = 37, SYS_execve = 59, SYS_gettid = 186,
       SYS_clock_gettime = 228, SYS_exit_group = 231 };
enum { SIGALRM = 14 };
#define SA_RESTORER 0x04000000
#define MAP_RWX_NEAR(addr, size) ((u8*)sc6(SYS_mmap, (i64)(addr), (i64)(size), 7, 0x22 | 0x100000, -1, 0))  // FIXED_NOREPLACE
struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }

static void out(const char* s) { u64 n = 0; while (s[n]) ++n; sc3(SYS_write, 1, s, n); }
static void num(u64 v) { char b[24]; int i = 23; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } out(b + i); }
static int failures;
static void check(int ok, const char* what) { out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures; }
static void on_alarm(int sig) { (void)sig; out("FAIL: hung (SIGALRM after 60 s)\nRESULT: FAIL\n"); sc1(SYS_exit_group, 99); }

__asm__(".text\n.balign 16\n.globl spawn_thread\nspawn_thread:\n"
        "  mov %rsi, %r9\n  mov %rdi, %rsi\n  mov $0x50F00, %edi\n  xor %edx, %edx\n  xor %r10d, %r10d\n  xor %r8d, %r8d\n"
        "  mov $56, %eax\n  syscall\n  test %rax, %rax\n  jnz 3f\n  call *%r9\n  mov $60, %eax\n  xor %edi, %edi\n  syscall\n"
        "3:\n  ret\n");
extern i64 spawn_thread(u64 stacktop, void (*fn)(void));
static u64 new_stack(void) { return (u64)sc6(SYS_mmap, 0, 65536, 3, 0x22, -1, 0) + 65536; }

// --- rows 1 and 2: a thread parked in G while A is rewritten ----------------------------------------------------
enum { OLD = 1111, NEW = 2222, ROUNDS = 64 };
static u8* code;
static u32 imm_offset;
enum { MAX_T = 4 };
static volatile int release_g = 1, parked, u_done, go_t, t_done, t_slot;
static volatile u32 t_result[MAX_T];

// G: called by A; parks until released, then serializes (cpuid, as cross-modifying code needs) and returns into A.
__attribute__((noinline, used)) static void g(void) {
  __atomic_add_fetch(&parked, 1, __ATOMIC_SEQ_CST);
  while (!release_g) sc0(SYS_sched_yield);
  u32 a = 0, b, c = 0, d;
  __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
}
static u32 call_x(void) { return ((u32(*)(void))code)(); }
static void thread_u(void) { call_x(); u_done = 1; }
static void thread_t(void) {
  const int slot = __atomic_fetch_add(&t_slot, 1, __ATOMIC_SEQ_CST);
  while (!go_t) sc0(SYS_sched_yield);
  t_result[slot] = call_x();
  __atomic_add_fetch(&t_done, 1, __ATOMIC_SEQ_CST);
}
static void write_imm(u32 v) { *(volatile u32*)(code + 4096 + imm_offset) = v; }

// nt threads T per round; indirect: 0 call rel32, 1 call rax, 2 alternating. Returns the stale returns.
static u32 parked_rounds(u64 base, int indirect_mode, int nt, int rounds) {
  u32 stale = 0;
  for (int r = 0; r < rounds; ++r) {
    const int indirect = indirect_mode == 2 ? (r & 1) : indirect_mode;
    code = MAP_RWX_NEAR(base + (u64)r * 0x10000, 2 * 4096);   // fresh pages: neither is hot, and A's call rel32 reaches G
    u8* x = code; u8* a = code + 4096;
    x[0] = 0xe8; *(u32*)(x + 1) = (u32)(i64)(a - (x + 5)); x[5] = 0xc3;                    // X: call A; ret
    if (indirect) {                                                                         // A: mov rax, G; call rax
      a[0] = 0x48; a[1] = 0xb8; *(u64*)(a + 2) = (u64)g; a[10] = 0xff; a[11] = 0xd0; imm_offset = 13;
    } else {                                                                                // A: call G
      a[0] = 0xe8; *(u32*)(a + 1) = (u32)(i64)((u8*)g - (a + 5)); imm_offset = 6;
    }
    a[imm_offset - 1] = 0xb8; a[imm_offset + 4] = 0xc3;                                   //    mov eax, imm32; ret
    write_imm(OLD);
    release_g = 1; parked = 0; u_done = 0; t_done = 0; t_slot = 0; go_t = 0;
    spawn_thread(new_stack(), thread_u);
    while (!u_done) sc0(SYS_sched_yield);
    release_g = 0; parked = 0;
    for (int k = 0; k < nt; ++k) spawn_thread(new_stack(), thread_t);
    while (t_slot < nt) sc0(SYS_sched_yield);
    go_t = 1;
    while (parked < nt) sc0(SYS_sched_yield);
    write_imm(NEW);                       // every T is inside G, its return address points into A
    release_g = 1;
    while (t_done < nt) sc0(SYS_sched_yield);
    for (int k = 0; k < nt; ++k) stale += t_result[k] != NEW;
  }
  return stale;
}

// --- rows 4 and 5: lookup entries a later translation took over -------------------------------------------------
static volatile int w_tid;
static u8* w_entry;
static volatile u32 w_result;
// G2: parks only thread W, so other threads run the same code through it without waiting.
__attribute__((noinline, used)) static void g2(void) { if (sc0(SYS_gettid) == w_tid) g(); }
static void thread_w(void) {
  while (!go_t) sc0(SYS_sched_yield);
  w_result = ((u32(*)(void))w_entry)();
  t_done = 1;
}
static u64 now_ms(void) { u64 ts[2]; sc3(SYS_clock_gettime, 1, ts, 0); return ts[0] * 1000 + ts[1] / 1000000; }
static int wait_w(void) {   // 0 if W has not returned within 5 s
  const u64 end = now_ms() + 5000;
  while (!t_done) { if (now_ms() > end) return 0; sc0(SYS_sched_yield); }
  return 1;
}
static void write_call(u8* at, void (*to)(void)) { at[0] = 0xe8; *(u32*)(at + 1) = (u32)(i64)((u8*)to - (at + 5)); }
static void write_mov_ret(u8* at, u32 v) { at[0] = 0xb8; *(volatile u32*)(at + 1) = v; at[5] = 0xc3; }

// Row 4: X (page 0): call R; ret   page 1: A0: call G2; R: call G2; mov eax, imm32; ret
// U runs X (X's call is linked to TB, R's translation); W follows the link into TB and parks in G2; the main thread
// runs A0, whose translation TA has entries A0, R and R+5 and so takes over both of TB's; then R+5's imm32 is
// rewritten. W must return the new value: TB, reached through no entry of its own any more, is still discarded.
static u32 taken_over_rounds(u64 base, int rounds) {
  u32 stale = 0;
  for (int r = 0; r < rounds; ++r) {
    code = MAP_RWX_NEAR(base + (u64)r * 0x10000, 2 * 4096);
    u8* a0 = code + 4096 + 16; u8* rr = a0 + 5;
    write_call(code, (void (*)(void))rr); code[5] = 0xc3;
    write_call(a0, g2); write_call(rr, g2); write_mov_ret(rr + 5, OLD);
    w_tid = 0; release_g = 1; u_done = 0; parked = 0; t_done = 0; go_t = 0; w_entry = code;
    spawn_thread(new_stack(), thread_u);
    while (!u_done) sc0(SYS_sched_yield);
    release_g = 0;
    w_tid = spawn_thread(new_stack(), thread_w);
    go_t = 1;
    while (parked < 1) sc0(SYS_sched_yield);
    ((u32(*)(void))a0)();
    *(volatile u32*)(rr + 6) = NEW;
    release_g = 1;
    if (!wait_w()) return ~0u;
    stale += w_result != NEW;
  }
  return stale;
}

// Row 5: A at P0+4000: 91 nops; call G2 (at 4091, so it returns to P1); P1: mov eax, OLD; ret
// The main thread runs A (T1: entries A and P1, on P0 and P1), then rewrites A within P0: call G2; mov eax, NEW; ret.
// W runs A (T2: entries A and A+5, on P0 only, as FEX counts a page touched by any instruction starting within 15
// bytes of it; T2 takes over A) and parks in G2; a byte on P1 is written, which discards T1 through the entries
// listed under P1, A among them. W must return NEW: T2 is current and stays usable.
static u32 overlap_rounds(u64 base, int rounds) {
  u32 stale = 0;
  for (int r = 0; r < rounds; ++r) {
    u8* p0 = MAP_RWX_NEAR(base + (u64)r * 0x10000, 2 * 4096);
    u8* a = p0 + 4000;
    for (int i = 0; i < 91; ++i) a[i] = 0x90;
    write_call(a + 91, g2); write_mov_ret(a + 96, OLD);
    w_tid = 0; release_g = 1;
    ((u32(*)(void))a)();
    write_call(a, g2); write_mov_ret(a + 5, NEW);
    release_g = 0; parked = 0; t_done = 0; go_t = 0; w_entry = a;
    w_tid = spawn_thread(new_stack(), thread_w);
    go_t = 1;
    while (parked < 1) sc0(SYS_sched_yield);
    *(volatile u8*)(p0 + 4096 + 64) = 0xcc;
    release_g = 1;
    if (!wait_w()) return ~0u;
    stale += w_result != NEW;
  }
  return stale;
}

static void run(void) {
  struct ksigaction al = {(void*)on_alarm, SA_RESTORER, (void*)restore_rt, 0};
  sc4(SYS_rt_sigaction, SIGALRM, &al, 0, 8);
  sc1(SYS_alarm, 60);
  u32 stale = parked_rounds(0x20000000, 0, 1, ROUNDS);
  out("  call rel32: "); num(stale); out(" of 64 rounds returned into the old code\n");
  check(stale == 0, "a return after the caller was rewritten ran the new code (call rel32)");
  stale = parked_rounds(0x28000000, 1, 1, ROUNDS);
  out("  call rax: "); num(stale); out(" of 64 rounds returned into the old code\n");
  check(stale == 0, "a return after the caller was rewritten ran the new code (call rax)");
  stale = parked_rounds(0x30000000, 2, MAX_T, 2 * ROUNDS);
  out("  4 threads: "); num(stale); out(" of 512 returns ran the old code\n");
  check(stale == 0, "four threads parked in the same call return the new code after it was rewritten");
  stale = taken_over_rounds(0x38000000, 16);
  if (stale == ~0u) { check(0, "a thread parked in a translation whose entries were taken over returned (hung)"); goto done; }
  out("  taken over: "); num(stale); out(" of 16 rounds returned into the old code\n");
  check(stale == 0, "a translation whose lookup entries a later one took over is still discarded");
  stale = overlap_rounds(0x40000000, 16);
  if (stale == ~0u) { check(0, "a thread parked in a current translation returned after an older one was discarded (hung)"); goto done; }
  out("  overlap: "); num(stale); out(" of 16 rounds returned the wrong value\n");
  check(stale == 0, "discarding a translation through an entry a later one took over leaves the later one usable");
done:
  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}

// entry: re-exec with FEX_SMCHOTPAGEFAULTS=0 (no hot pages), as cpuid-fault adds its option
static int seq(const char* a, const char* b) { while (*a && *a == *b) ++a, ++b; return *a == *b; }
static char* newenv[256];
__attribute__((used)) static void entry_c(u64* sp) {
  u64 argc = sp[0]; char** argv = (char**)(sp + 1); char** envp = argv + argc + 1;
  int have = 0, n = 0;
  for (char** e = envp; *e && n < 254; ++e) {
    if (seq(*e, "FEX_SMCHOTPAGEFAULTS=0")) have = 1;
    newenv[n++] = *e;
  }
  if (!have) {
    newenv[n++] = "FEX_SMCHOTPAGEFAULTS=0";
    newenv[n] = 0;
    sc3(SYS_execve, "/proc/self/exe", argv, newenv);
  }
  run();
}
__attribute__((naked, used)) void _start(void) { __asm__ volatile("mov %rsp, %rdi\n and $-16, %rsp\n call entry_c\n"); }
