// SPDX-License-Identifier: MIT
//
// 32-bit code in a 64-bit process, the way Wine's new WoW64 runs it on x86-64 Linux (dlls/ntdll/unix/signal_x86_64.c,
// dlls/wow64cpu/cpu.c): the 32-bit TEB's %fs selector comes from set_thread_area called through `int $0x80` from 64-bit
// code (alloc_fs_sel); 64-bit code loads that selector with `mov %fs`; 32-bit code runs in the kernel's 32-bit user
// code segment 0x23, entered with IRETQ and left with a far jump to 0x33; a signal that lands in 32-bit code reaches the
// 64-bit handler and rt_sigreturn resumes the 32-bit code.
//
// Checks:
//   1. int 0x80 set_thread_area (entry -1) from 64-bit code gives a TLS entry (12-14);
//   2. int 0x80 get_thread_area returns its base;
//   3. mov %fs with that selector in 64-bit code (after arch_prctl gave %fs a 64-bit base) loads the entry's base;
//   4. IRETQ to 0x23 runs 32-bit code (mov %cs, inc/dec at 0x40-0x4f, pusha/popa) and ljmp returns to 0x33;
//   5. 32-bit code reads the TLS base through %fs:0;
//   6. ud2 in 32-bit code: the SIGILL frame has cs 0x23 and the ud2's rip, sigreturn resumes the 32-bit code;
//   7. an x87 load in 32-bit code whose address wraps at 4 GiB reads the wrapped address.
// The expected values are Linux's documented behaviour, which Wine's x86-64 WoW64 relies on; no native x86-64 reference
// run exists here yet. A fault the test doesn't expect prints the signal, rip and cs, then RESULT: FAIL.
//
// Build: wow64-mode/build.sh [clang]   (static non-PIE: text, data and bss sit below 4 GiB, as 32-bit code needs)
// Run:   ./wow64_mode_test   (exit status = number of failed checks)

typedef unsigned int u32;
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
#define sc2(n, a, b) sc6(n, (i64)(a), (i64)(b), 0, 0, 0, 0)
#define sc3(n, a, b, c) sc6(n, (i64)(a), (i64)(b), (i64)(c), 0, 0, 0)

// int 0x80 from 64-bit code: the i386 ABI, number in eax and first argument in ebx. Linux zeroes r8-r11 on the way out.
static inline i64 int80(i64 nr, u64 a) {
  i64 ret;
  __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a) : "r8", "r9", "r10", "r11", "memory");
  return ret;
}

enum { SYS_write = 1, SYS_rt_sigaction = 13, SYS_alarm = 37, SYS_arch_prctl = 158, SYS_exit_group = 231 };
enum { NR32_set_thread_area = 243, NR32_get_thread_area = 244, ARCH_SET_FS = 0x1002 };
enum { SIGILL = 4, SIGBUS = 7, SIGSEGV = 11, SA_SIGINFO = 4, SA_RESTORER = 0x04000000, REG_RIP = 16, REG_CSGSFS = 18 };

struct user_desc { u32 entry_number, base_addr, limit, flags; };
struct ksigaction { void* handler; u64 flags; void* restorer; u64 mask; };
__attribute__((naked, used)) static void restore_rt(void) { __asm__ volatile("mov $15, %eax\n syscall\n"); }

// Shared with the assembly below; all below 4 GiB, so 32-bit code and a 32-bit ebx can name them.
static struct user_desc desc, getdesc;
u32 tls32[4];
float x87_val = 2.5f;
volatile u32 have_tls, res_cs, res_incdec, res_popa, res_fs32, res_after_sig, res_x87;
u64 saved_rsp;
char stack32[65536] __attribute__((aligned(16)));

// run32: IRETQ from 64-bit code to the 32-bit code below, on stack32, and back through `ljmp *back64` (0x33). The
// callee-saved registers and rsp survive the trip. DS and ES get __USER_DS first: 32-bit data accesses through a null DS
// fault on a real CPU (wow64cpu loads them the same way).
void run32(void);
extern char ud2_32[];
__asm__(".text\n.globl run32\nrun32:\n"
        " push %rbx\n push %rbp\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, saved_rsp(%rip)\n"
        " mov $0x2b, %eax\n mov %eax, %ds\n mov %eax, %es\n"
        " lea stack32+65536(%rip), %rax\n mov %rax, %rsp\n"
        " pushq $0x2b\n push %rax\n pushfq\n pushq $0x23\n"   // ss, rsp, rflags, cs
        " lea code32(%rip), %rax\n push %rax\n"                 // rip
        " iretq\n"
        ".code32\n"
        "code32:\n"
        " mov %cs, %eax\n mov %eax, res_cs\n"
        " mov $5, %eax\n .byte 0x40, 0x40, 0x48\n mov %eax, res_incdec\n"   // inc %eax; inc %eax; dec %eax
        " mov $0x1234, %ebx\n pusha\n xor %ebx, %ebx\n popa\n mov %ebx, res_popa\n"
        " cmpl $0, have_tls\n je 1f\n"
        " movl %fs:0, %edx\n mov %edx, res_fs32\n"
        "1:\n"
        ".globl ud2_32\nud2_32:\n ud2\n"
        " movl $1, res_after_sig\n"
        " mov $0xF0000000, %eax\n flds x87_val+0x10000000(%eax)\n fstps res_x87\n"   // 0xF0000000 + 0x10000000 wraps to x87_val
        " ljmp *back64\n"
        ".code64\n"
        "land64:\n"
        " mov saved_rsp(%rip), %rsp\n"
        " pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbp\n pop %rbx\n"
        " ret\n"
        ".data\n.balign 8\nback64: .long land64\n .word 0x33\n"
        ".text\n");

static int failures;
static void out(const char* s) { u64 n = 0; while (s[n]) ++n; sc3(SYS_write, 1, s, n); }
static void outhex(u64 v) { char b[19]; int i = 18; b[i] = 0; do { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v); b[--i] = 'x'; b[--i] = '0'; out(b + i); }
static void check(int ok, const char* what) { out(ok ? "PASS: " : "FAIL: "); out(what); out("\n"); if (!ok) ++failures; }

static void fatal_handler(int sig, void* info, void* uc) {
  u64* gregs = (u64*)((char*)uc + 40);
  (void)info;
  out(sig == SIGILL ? "  SIGILL" : sig == SIGBUS ? "  SIGBUS" : "  SIGSEGV");
  out(" at rip "); outhex(gregs[REG_RIP]); out(", cs "); outhex(gregs[REG_CSGSFS] & 0xffff); out("\n");
  out("RESULT: FAIL\n");
  sc1(SYS_exit_group, 100);
}

// Row 6's ud2. An int 0x80 a FEX without 32-bit calls in 64-bit code refuses is skipped the same way (both are 2
// bytes), so rows 1 and 2 report instead of the run dying there.
static volatile u64 sig_cs, sig_rip;
static void ill_handler(int sig, void* info, void* uc) {
  u64* gregs = (u64*)((char*)uc + 40);
  const unsigned char* p = (const unsigned char*)gregs[REG_RIP];
  if (!((p[0] == 0x0f && p[1] == 0x0b) || (p[0] == 0xcd && p[1] == 0x80))) fatal_handler(sig, info, uc);
  sig_cs = gregs[REG_CSGSFS] & 0xffff;
  sig_rip = gregs[REG_RIP];
  gregs[REG_RIP] += 2;
}

__attribute__((used)) void entry_c(void) {
  struct ksigaction ill = {(void*)ill_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  struct ksigaction fatal = {(void*)fatal_handler, SA_SIGINFO | SA_RESTORER, (void*)restore_rt, 0};
  sc6(SYS_rt_sigaction, SIGILL, (i64)&ill, 0, 8, 0, 0);
  sc6(SYS_rt_sigaction, SIGSEGV, (i64)&fatal, 0, 8, 0, 0);
  sc6(SYS_rt_sigaction, SIGBUS, (i64)&fatal, 0, 8, 0, 0);
  sc1(SYS_alarm, 60);

  // Wine's alloc_fs_sel: entry -1, the 32-bit TEB as base, limit ~0, seg_32bit | useable.
  tls32[0] = 0x5EC0DE32;
  desc.entry_number = (u32)-1;
  desc.base_addr = (u32)(u64)tls32;
  desc.limit = 0xffffffff;
  desc.flags = 0x41;
  i64 r_set = int80(NR32_set_thread_area, (u64)&desc);
  have_tls = r_set == 0 && desc.entry_number >= 12 && desc.entry_number <= 14;
  check(have_tls, "1. int 0x80 set_thread_area from 64-bit code gives a TLS entry (12-14)");

  getdesc.entry_number = desc.entry_number;
  i64 r_get = int80(NR32_get_thread_area, (u64)&getdesc);
  check(r_get == 0 && getdesc.base_addr == (u32)(u64)tls32, "2. int 0x80 get_thread_area returns the entry's base");

  // 64-bit code has its own base in %fs first (Wine's 64-bit TEB): the selector load must replace all 64 bits of it.
  u32 fs64_read = 0;
  if (have_tls) {
    sc2(SYS_arch_prctl, ARCH_SET_FS, 0x123400000000UL);
    u32 sel = desc.entry_number * 8 + 3;
    __asm__ volatile("mov %k1, %%fs\n movl %%fs:0, %0" : "=r"(fs64_read) : "r"(sel) : "memory");
  }
  check(have_tls && fs64_read == 0x5EC0DE32, "3. mov %fs with that selector in 64-bit code loads its base (%fs:0)");

  sig_cs = sig_rip = 0;
  run32();
  check(res_cs == 0x23 && res_incdec == 6 && res_popa == 0x1234,
        "4. iretq to 0x23 runs 32-bit code (mov %cs, inc/dec at 0x40-0x4f, pusha/popa) and ljmp returns to 0x33");
  check(have_tls && res_fs32 == 0x5EC0DE32, "5. 32-bit code reads the TLS base through %fs:0");
  check(sig_cs == 0x23 && sig_rip == (u64)ud2_32 && res_after_sig == 1,
        "6. a signal in 32-bit code: frame cs 0x23, rip at the ud2, sigreturn resumes the 32-bit code");
  check(res_x87 == 0x40200000, // 2.5f
        "7. an x87 load in 32-bit code whose address wraps at 4 GiB reads the wrapped address");

  out(failures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
  sc1(SYS_exit_group, failures);
}

__attribute__((naked, used)) void _start(void) { __asm__ volatile("and $-16, %rsp\n call entry_c\n"); }
