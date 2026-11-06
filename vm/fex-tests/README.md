# eac-tests

Regression tests for the fidelity patches on this branch (see [EAC-README.md](../EAC-README.md)). Each one is a small
freestanding x86-64 program (raw system calls, no libc) that checks one behaviour a genuine Easy Anti-Cheat title,
Wine/Proton or a debugger relies on, and prints `RESULT: PASS` or `RESULT: FAIL`.

**The expected result is what a real x86-64 Linux kernel does.** Every test runs unchanged on bare-metal x86-64 Linux;
that native run is the oracle, not FEX's own current behaviour. A test that cannot pass natively is the test's bug. A
remaining difference from native that no real program observes is a documented limit, not a defect.

## Building and running

Needs clang with the x86-64 target and python3. `ld.lld` is used when it works; otherwise
[tools/static-link.py](tools/static-link.py), a minimal linker for one freestanding object, links the binary, so the
tests also build on an Arm64 machine without an x86-64 linker.

```
eac-tests/run-all.sh                    # build everything into eac-tests/build/, run each test, one line per run
eac-tests/run-all.sh FEXInterpreter     # the same with an explicit runner instead of binfmt_misc
NATIVE=1 eac-tests/run-all.sh           # on a real x86-64 Linux: the reference run, every test expects PASS
```

`run-all.sh` exits 0 when every run gives its expected result. Per test:

```
eac-tests/<name>/build.sh [clang] [output]   # x86-64 binary: run it under FEX, and unchanged on x86-64 Linux
eac-tests/<name>/build.sh native [output]    # where supported: built with the host's cc for the host's own arch
./<binary>                                   # exit status = number of failed checks
```

## The tests

| test | checks | patch |
|---|---|---|
| `ptrace-inject` | The whole `ptrace` conversation of a launcher injecting into a child (35 checks). Under stock FEX it hangs after `PASS: fork`. `run.sh` runs it with a 60 s timeout. | 0001 |
| `signal-mask` | The Linux signal-mask rule around guest handlers. | 0007, 0008 |
| `signal-regs` | A handler's register changes survive across a syscall. | 0008 |
| `seccomp-trap` | Wine's `SECCOMP_RET_TRAP`/SIGSYS path: `si_code`/`si_syscall`/`si_arch`/`si_call_addr`, the six arguments in the ucontext, a RAX written by the handler resumes the caller; 1000 repetitions. Run with `FEX_NEEDSSECCOMP=1`. | 0010 |
| `seccomp-trap-noexec` | A trapped syscall is never executed (a trapped `uname()` leaves its buffer untouched). Run with `FEX_NEEDSSECCOMP=1`. | 0010 |
| `signal-race` | A thread looping trapped syscalls while another hammers it with SIGUSR1 can still be stopped (the shape of Wine's `NtGetContextThread`). Run with `FEX_NEEDSSECCOMP=1`. | 0010, 0022 |
| `cpuid-fault` | `arch_prctl(ARCH_SET_CPUID/ARCH_GET_CPUID)`: a CPUID faults as SIGSEGV/`SI_KERNEL` while disabled. Re-execs itself with `FEX_ENABLECPUIDFAULTING=1`. A host CPU without `cpuid_fault` reports SKIP natively. | 0011 |
| `ptrace-dregs` | Debug registers through `PTRACE_PEEKUSER`/`POKEUSER`, with the kernel's validation and error codes. | 0012 |
| `proc-status` | `Seccomp:` and `TracerPid:` in `/proc/self/status`. Re-execs itself with `FEX_NEEDSSECCOMP=1`. | 0013 |
| `ptrace-regsets` | `PTRACE_GETREGSET`/`SETREGSET`: `NT_PRSTATUS`, `NT_X86_XSTATE`, `-EINVAL` for an unknown type. | 0014 |
| `restart-syscall` | `restart_syscall` returns `-EINTR` and the process survives. | 0015 |
| `signal-frame` | The signal frame against what Linux writes: TF single-step, CS/SS `0x33`/`0x2b`, the XSAVE fpstate trailer (run a second time with `FEX_HOSTFEATURES=disableavx` for the FXSAVE frame), `ud2` → `ILL_ILLOPN`, `uc_sigmask`, a frame that does not fit the stack. | 0016–0020 |
| `sigreturn-eflags` | `rt_sigreturn` applies EFLAGS a handler wrote (DF; TF starts single-stepping with exactly one SIGTRAP). | 0016 |
| `maps-probe` | Probes every inaccessible mapping listed in `/proc/self/maps` under a SIGSEGV handler: every probe returns, `si_addr` is the probed address. | 0023 |
| `tf-iret` | Where the single-step trap lands after `POPFQ`, an `IRETQ` loading TF, Wine's `popfq; iretq`, and across cached call/ret code. | 0025 |
| `proc-maps` | `/proc/self/maps` lists only x86-64 process mappings: everything below `TASK_SIZE_MAX` except `[vsyscall]`, every file-backed ELF mapping x86-64. | 0033 |
| `smc-remote-write` | A code page the guest made RWX (and executed from again) can be written by another process with `process_vm_writev` (Wine's `NtWriteVirtualMemory`), and the new code runs. | 0035 |
| `sud` | Syscall user dispatch (`PR_SET_SYSCALL_USER_DISPATCH`) as Wine's ntdll uses it for a Windows program's own `syscall`: the prctl's argument checks, the range and the selector, the SIGSYS (`si_code` 2, `si_call_addr`, ucontext RIP/RAX), the dispatched syscall never runs, a handler that rewinds RIP re-runs it, the kills (bad selector value, unreadable selector, SIGSYS blocked or ignored), fork children and new threads start off, a SIGUSR1 arriving mid-dispatch loses nothing. x86-64 only. | 0036 |
| `ud-trap` | An invalid opcode (`60` in 64-bit code, `mov cs, ax`) is SIGILL `ILL_ILLOPN` with trap 6 and `si_addr`/RIP at the instruction, as Linux's `do_error_trap`; also on a code page made RWX and rewritten (an SMC hot page), where FEX spun forever. x86-64 only. | 0037, 0038 |
| `ud-flood` | A thread executing `ud2` in a loop, whose SIGILL handler checks every general register and the stack pointer, clears them and moves the stack 512 bytes down (as Wine's exception dispatch does for Age of Empires II DE's nanomites), while another thread floods it with SIGUSR1: every SIGILL sees the loop's registers. A SIGUSR1 while FEX spilled the registers before raising the SIGILL, or after an earlier `rt_sigreturn` left "all spilled" set, handed the thread the previous return's registers: 0 runs in 3 passed; 3 in 5 with only the first half of 0041, 0 in 6 with only the second, 8 in 8 with both. x86-64 only. | 0041 |
| `signal-nest` | A thread making 20000 dispatched syscalls while another floods it with SIGUSR1, then exiting under the flood. Before 0039/0040 it died by SIGSEGV in every run (0/11); with both it passed 7 runs in 8, the eighth resuming with a stale guest register file after a SIGUSR1 `rt_sigreturn`; with 0041, 28 in 28. | 0039, 0040, 0041 |
| `jit-capacity` | A large self-modifying program within the JIT's SMC hot set: 2,048 code pages each written next to its running code stop faulting once hot (at most 40 faults a page; a hot set of 1,536 pages left 116,224), and rewritten functions return their new values. Reads FEX's statistics (it re-execs itself with `FEX_PROFILESTATS=1`); natively the measurement reports SKIP. x86-64 only. | 0042 |
| `callret-stale` | A return into code rewritten during the call, by a thread that entered the caller through another thread's block link: a thread parked in the callee while the caller's `mov eax, imm32` is rewritten must return the new value (call rel32 and call rax, 64 rounds each; four threads parked in the same call, 128 rounds; 16 rounds where a later translation took over all lookup entries of the one the thread waits in; 16 where the thread's own translation took over an entry of an older one that is then discarded through another page). FEX's call-return stack kept the old translation's host address for such a thread: 64/64, 64/64 and 512/512 returns ran the old code; a first fix that patched translations through their lookup entries left 16/16 stale returns and a hang. Re-execs itself with `FEX_SMCHOTPAGEFAULTS=0`. x86-64 only. | 0043 |
| `dead-load` | A load whose result nothing uses still faults: HotSpot's interpreter null check (`cmp (%rcx),%rax`, flags unused) and a dead `mov`, with `rcx` = 0. Stock FEX drops both loads (IR `LoadMem` without side effects). | wine-arm64 fex 0008 |
| `pop-fault` | A faulting `pop` to memory leaves RSP unchanged. Its patch (0021) is withdrawn, so it is **expected to fail** on this branch. | — |
| `bench` | Not a pass/fail test: micro-benchmarks for code-churn invalidation (`EXTRA="-DINVAL_MODE=1 -DINVAL_THREADS=190"`) and the self-modifying-code modes; `tso_probe.c` is an ordinary Arm64 program printing the TSO `prctl` state. | 0002, 0004 |

Expected results (same binaries on all three):

| test | native x86-64 | stock FEX-2610 | eac-2610 |
|---|---|---|---|
| `ptrace-inject` | PASS | hang (timeout) | PASS |
| `signal-mask`, `signal-regs`, `signal-race`, `cpuid-fault`, `proc-status`, `ptrace-regsets`, `ptrace-dregs`, `signal-frame`, `sigreturn-eflags`, `tf-iret` | PASS | FAIL | PASS |
| `proc-maps` | PASS (derived from the kernel source; native run pending) | FAIL | PASS |
| `smc-remote-write` | PASS (derived from the kernel source; native run pending) | FAIL | PASS |
| `seccomp-trap`, `seccomp-trap-noexec` | PASS | SIGSEGV | PASS |
| `restart-syscall` | PASS | SIGILL | PASS |
| `maps-probe` | PASS | hang (timeout) | PASS |
| `sud` | PASS (derived from the kernel source; native run pending) | FAIL | PASS |
| `ud-trap` | PASS (derived from the kernel source; native run pending) | FAIL | PASS |
| `ud-flood`, `signal-nest` | PASS (derived from the kernel source; native run pending) | not run | PASS |
| `jit-capacity` | PASS (measurements SKIP; native run pending) | FAIL | PASS |
| `callret-stale` | PASS (derived from the kernel source; native run pending) | FAIL | PASS |
| `dead-load` | PASS (x86 faults on every memory operand; native run pending) | FAIL | PASS |
| `pop-fault` | PASS | FAIL | FAIL (withdrawn 0021) |

Each test fails without the patch it names: the series was built one patch at a time and each test flips from FAIL to
PASS at its own patch.

## Adding a test

New behaviour on this branch ships with a test here: a freestanding program that prints `RESULT: PASS`/`FAIL`, passes
on a real x86-64 Linux kernel, fails without the change and passes with it. Add it to the list in `run-all.sh`.
