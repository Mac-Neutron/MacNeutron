# MacNeutron: syscall user dispatch in our FEX, and AoE2 DE to its main menu in the VM

Status: asked for by the maintainer on 2026-10-21 after Age of Empires II: Definitive Edition (app 813780) crashed in
the VM. Their choices: write a spec; done means **AoE2 reaches its main menu in the VM**; approach 1 below; the M2
shape in §6. Design sections 1-4 approved in chat on 2026-10-21. Milestones M1 (syscall user dispatch in FEX) and M2
(the AoE2 bring-up loop), each with its own plan; M2's is written when M1 is done.

## 1. Goal

Wine's syscall user dispatch (SUD) works under our FEX in the VM, so a Windows program that makes direct `syscall`
instructions from its own code (AoE2's anti-tamper does) reaches Wine's syscall dispatcher as it does on x86-64 Linux.
Then AoE2 is brought up, one difference from Windows at a time, until its main menu.

## 2. Findings (2026-10-21)

- `AoE2DE_s.exe` crashes at 0x142D54F1A (`movq (%r10),%r10`, null pointer) right after start; winedbg then hangs and
  holds the VM open. Same after a Steam file repair (7 files re-acquired, which explained the Mac build's
  "corrupted" message).
- AoE2's anti-tamper builds an executable page with a `syscall` gadget and makes Windows system calls through it.
  Wine catches those with SUD: `init_syscall_frame` (`dlls/ntdll/unix/signal_x86_64.c`, wine-linux at Wine 11.20)
  calls `prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, libc_addr, libc_size,
  &thread_data->syscall_dispatch)` per thread, the selector at TEB+0x340. The selector is 0 (ALLOW) in Wine's Unix
  code and signal handlers, 1 (BLOCK) on return to PE code. `sigsys_handler` reads the Windows syscall number from
  RAX, sets `frame->rip = RIP + 0xb` (the dispatcher subtracts 0xb again, so the caller resumes right after its
  `syscall`) and redirects to `__wine_syscall_dispatcher_prolog_end`. i386 Wine doesn't use SUD.
- Under FEX that prctl fails: FEX passes unknown prctl options to the arm64 host (`Syscalls/Thread.cpp`, prctl
  `default:` → `::prctl`), and every Wine process logs `could not enable syscall user dispatch` (9 lines in
  `runs/813780/run.log`). A direct syscall then runs as a Linux syscall with a Windows number.
- FEX has what delivery needs. `HandleSyscallImpl<Is64Bit>` (`LinuxSyscalls/Syscalls.cpp`) runs every guest syscall,
  with `State.rip` at the `syscall` instruction (upstream 66cad97 removed inline syscalls, so there is no JIT path
  around it). Patch 0010's `SECCOMP_RET_TRAP` path (`Seccomp/SeccompEmulator.cpp`) queues a SIGSYS whose guest frame
  is entered at the post-syscall RIP (`ThreadStateObject::SeccompTrapRIP`); because that differs from `OriginalRIP`,
  `rt_sigreturn` (`RestoreFrame_x64`) restores the handler's registers and resumes there, and the trapped syscall never
  runs. `FaultSafeUserMemAccess::CopyFromUser` reads guest memory and reports a fault as EFAULT.
- FEX reports a kernel between 5.15 and 6.11 (`CalculateGuestKernelVersion`), so the SUD modes are the 6.11 ones:
  OFF and ON (no `PR_SYS_DISPATCH_INCLUSIVE_ON`).
- Seccomp emulation is gated by `FEX_NEEDSSECCOMP=1`; Wine doesn't set it, so SUD can't be gated by it.
- FEX's own CLAUDE.md forbids AI-written contributions upstream.

## 3. Decisions

| Question | Decision | Why |
|---|---|---|
| Where SUD lives | Emulated in FEX's Linux layer: FEX handles the prctl and checks each guest syscall itself; delivery reuses patch 0010's trap path. Patch 0036, our fork only | FEX already sees every guest syscall and has a SIGSYS path that skips the syscall |
| Rejected | Passing the prctl to the host kernel (it would trap FEX's own arm64 syscalls); Wine on seccomp instead of SUD (Proton's old way, a Wine change for one emulator) | — |
| Kernel model | Linux 6.11's `kernel/entry/syscall_user_dispatch.c`, `prctl` and `syscall_trace_enter` order (SUD, then ptrace, then seccomp) | The oracle rule of `eac-vm/fex-tests/README.md` |
| Blocked or ignored SIGSYS | Matched: the process is killed by SIGSYS, as `force_sig_info` does | Two lines with the kill helper; testable |
| FEX's 32-bit sigreturn trampolines | Exempt from dispatch, as the kernel's `arch_syscall_is_vdso_sigreturn` exempts its 32-bit vDSO sigreturn landing pads | They are FEX's equivalent (`VDSO_kernel_sigreturn`, `VDSO_kernel_rt_sigreturn`), outside any range a program sets |
| Seccomp's `kill(0, …)` | Left alone | Outside this work |
| M2's rule | Fix our stack (FEX, Wine, the guest), never the game or its anti-tamper | The maintainer's |

Out of scope: upstreaming to FEX, SUD on the macOS path (Wine on macOS doesn't use it), 32-bit SUD in Wine (Wine has
none; FEX's 32-bit path gets the same check for free), `PR_SYS_DISPATCH_INCLUSIVE_ON` (newer than 6.11).

## 4. M1: syscall user dispatch in FEX (patch 0036)

### 4.1 The prctl and per-thread state

`PR_SET_SYSCALL_USER_DISPATCH` (59) gets a case in the prctl switch of `Syscalls/Thread.cpp` and is never passed to
`::prctl`. As the kernel:

- `PR_SYS_DISPATCH_OFF` (0): offset, len and selector all 0, else `-EINVAL`. Turns dispatch off.
- `PR_SYS_DISPATCH_ON` (1): a nonzero offset with `offset + len <= offset` (a zero length or an overflow) → `-EINVAL`; a non-null selector outside the user address space
  (`access_ok`, not "mapped") → `-EFAULT`. Stores the range and the selector address.
- Any other mode → `-EINVAL`. No sysctl, no `-EPERM`.

The state lives in `ThreadStateObject` (`LinuxSyscalls/ThreadManager.h`, beside the seccomp fields): on/off, offset,
len, selector address. Every new thread starts off (`ThreadManager::CreateThread` copies fields explicitly, so the new
fields are simply not copied); a fork child clears it (`ForkGuest`'s child path, `Syscalls/Thread.cpp`); exec re-execs
FEX, so it ends there. The kernel clears it in `dup_task_struct` for every clone, threads included, and on exec.

### 4.2 The check and SIGSYS delivery

First thing in `HandleSyscallImpl<Is64Bit>`, before `SeccompEmulator.HasFilter` and the PtraceEmu entry stop. Off: one
flag test. On:

1. `ip = State.rip + 2` (both `0f 05` and `cd 80` are two bytes).
2. A 32-bit syscall whose `ip` is the end of a sigreturn trampoline's `int 0x80` (`VDSO_kernel_sigreturn` + 8,
   `VDSO_kernel_rt_sigreturn` + 7, `VDSO_Emulation.cpp`'s layouts; the plan checks FEX's vDSO image has the same) →
   run the syscall. 64-bit signal returns go through the program's `SA_RESTORER` (libc's, inside Wine's range).
3. `(ip - offset) < len`, unsigned → run the syscall.
4. A selector is set: read one byte with `CopyFromUser`. A fault → the process is killed by SIGSEGV; 0 (ALLOW) → run
   the syscall; 1 (BLOCK) → dispatch; any other value → the process is killed by SIGSYS.
5. Dispatch: SIGSYS blocked or ignored in the guest → the process is killed by SIGSYS. Otherwise a siginfo with
   `si_code = 2` (`SYS_USER_DISPATCH`), `si_errno = 0`, `si_call_addr = ip`, `si_syscall` = the number,
   `si_arch` = `AUDIT_ARCH_X86_64` or `AUDIT_ARCH_I386`; `Thread->SeccompTrapRIP = ip` (the field's comment widened to
   cover SUD, no second field); `QueueSignal(..., IgnoreMask=true)`; return without running the syscall. RAX keeps the
   syscall number, which the handler reads.

"Killed by" is one small helper, `KillProcessWith(sig)`: unblock the signal, uninstall FEX's host handler for it,
`kill(getpid(), sig)` (not seccomp's `kill(0, …)`, which signals the whole process group).

### 4.3 Tests

**`eac-vm/fex-tests/sud`** (`sud_test.c` + `build.sh`, a `run-all.sh` row, a README row): one freestanding x86-64
program in `seccomp-trap`'s style, expectations from the kernel source, native column "native run pending". Its RED is
the same binary failing on the current guest FEX (3bb9e30).

1. prctl arguments: OFF with a nonzero argument, an unknown mode, and ON with a nonzero offset and a zero or
   overflowing length → `-EINVAL`; a selector outside user space → `-EFAULT`; ON with offset 0 and length 0 is
   accepted.
2. Range: a syscall inside it runs; at the edges `ip == offset` runs and `ip == offset + len` is dispatched.
3. Dispatch: a `0f 05 c3` gadget on an mmap'd page → SIGSYS with `si_code` 2, `si_errno` 0, `si_call_addr` = gadget + 2,
   `si_syscall` = the number, `si_arch` X86_64; ucontext RIP = gadget + 2 and RAX = the number; a trapped `write` to a
   pipe leaves the pipe empty; the RAX the handler writes is what the caller gets; 1000 repetitions.
4. Selector: none → dispatched; ALLOW runs, BLOCK dispatches, toggled; prctl OFF while on → the gadget runs.
5. Kills, each in a forked child, checked through its wait status: selector value 2 → SIGSYS; selector page unmapped →
   SIGSEGV; SIGSYS blocked → SIGSYS; SIGSYS ignored → SIGSYS.
6. Inheritance: a raw `clone` thread and a fork child start off (their gadgets run).

**`eac-vm/guest/win/rawsyscall.c`** (built with llvm-mingw as `rawinput.exe` is, into the share's `bin/`) and the guest
script **`eac-vm/tests/sud.sh`**: the anti-tamper's pattern. The program reads the syscall numbers from ntdll's
`NtClose` and `NtQueryInformationProcess` stubs (checking the `4c 8b d1 b8` prefix, failing loudly otherwise) and runs
`mov r10,rcx; mov eax,N; syscall; ret` from its own `VirtualAlloc` page: `NtClose((HANDLE)0xdead)` →
`STATUS_INVALID_HANDLE`; `NtQueryInformationProcess(ProcessBasicInformation)`'s `UniqueProcessId` ==
`GetCurrentProcessId()`; the same from a second thread; 1000 repetitions. `sud.sh` also checks that Wine's stderr
(with `WINEDEBUG=warn+seh`) has no `could not enable syscall user dispatch`.

Lanes (the maintainer's lighter-testing rule: one lane per task, full gates at the milestone): during tasks, `fex-tests/sud` alone under the builder's FEX. At the M1 gate: the
full `run-all.sh` (seccomp-trap and the signal tests share the trap path), `tests/sud.sh` and `tests/wine.sh` in the VM.

### 4.4 Shipping

- One commit in `build/eac-vm-work/fex-linux` on 3bb9e30, exported with `git format-patch` as
  `eac-vm/fex-patches/0036-LinuxSyscalls-emulate-syscall-user-dispatch.patch`; the fex-tests README lists 0036.
- `guest-stages.sh fex` (builder started with both shares), which writes the new `.mn-source` stamp. For the tests the
  builder's `/usr/local/bin/FEX` is replaced and `systemctl restart systemd-binfmt` run, else binfmt keeps the old one.
- An image rebuild, so the VM runs it. Wine doesn't change. The p profile's FEX is rebuilt at release time
  (`MACNEUTRON_PROFILE=p`, as every guest stage before a release).
- Nothing is released; 0036 waits for the next release, on the maintainer's word.

### 4.5 M1 is done when

1. `fex-tests/sud` passes, its RED on 3bb9e30 recorded.
2. The M1 gate passes: full `run-all.sh`; `tests/sud.sh` and `tests/wine.sh` in the VM.
3. The maintainer launches AoE2 in the VM (the executor never opens MacNeutron.app or Steam) and its `run.log` has
   zero `could not enable syscall user dispatch` lines (9 before).

AoE2 may still crash after M1, somewhere new; that is M2's.

## 5. Risks

- FEX's sigreturn trampolines might not be the only x86 code FEX runs a `syscall` from outside Wine's range (its x86
  vDSO fallbacks). Wine's selector is ALLOW whenever its Unix side runs, so those would trap only from PE code, as on
  the kernel; `rawsyscall.exe` and `wine.sh` exercise the common paths.
- A SIGSYS delivered to a thread inside a FEX-internal critical section. The trap path is the one Wine's seccomp mode
  and `signal-race` already exercise.

## 6. M2: AoE2 to its main menu

A loop, planned after M1:

1. **Crash runs end.** A crashing game must end the run instead of hanging in winedbg and holding the VM open. First
   check what Proton does about winedbg and the crash dialog and do the same (the Proton-parity rule for anything Steam and Wine).
2. **Find the first difference.** Run AoE2 in the VM with `WINEDEBUG` channels chosen for the failure (`+seh`,
   `+loaddll`, `+module`, `+virtual`, …) to the first place where Wine under FEX behaves unlike Windows.
3. **Fix it in our stack, with a test.** FEX (a fex-tests program), Wine (a guest test) or the guest image; never the
   game or its anti-tamper.
4. Repeat until the main menu shows. Stop and ask for anything only the maintainer decides (downloads, release
   changes, anything that touches their data or Steam).
