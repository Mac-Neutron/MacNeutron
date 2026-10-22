# Syscall user dispatch in our FEX (M1) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wine's `PR_SET_SYSCALL_USER_DISPATCH` works under our guest FEX, so a Windows program's direct `syscall` from
its own code reaches Wine's dispatcher in the VM.

**Architecture:** One commit on the guest FEX tree (`build/eac-vm-work/fex-linux`, on 0035's `3bb9e30`), exported as
`eac-vm/fex-patches/0036`: the prctl stores per-thread state, `SyscallHandler::HandleSyscall` checks it before anything
else and dispatches through patch 0010's SIGSYS trap path. Two refinements of spec §4.2, both found while planning: the
check sits in `HandleSyscall` (the wrapper), not `HandleSyscallImpl`, so a 64-bit `int 0x80` (patch 0033's branch) is
covered and a dispatched syscall returns *before* the wrapper's `State.rip += 2` (a handler that turns dispatch off and
rewinds RIP by 2, Wine's fallback in `sigsys_handler`, then re-runs the syscall instead of skipping it); and the kill
helper is `SignalDelegator::ForceExitSignal(int)`, the tail of patch 0019's `ForceGuestSigSegv` made signal-generic
(SIG_DFL, flush the code map, `raise`), instead of a new `kill(getpid(), …)`. Tests: `eac-vm/fex-tests/sud` (FEX alone)
and `rawsyscall.exe` + `tests/sud.sh` (Wine in the VM).

**Tech Stack:** C++ (LinuxEmulation), freestanding C + inline x86-64 assembly (FEX test), C with llvm-mingw (Windows
test), the Fedora builder VM, the microVM (`run-guest.sh`).

**Spec:** `docs/superpowers/specs/2026-10-21-macneutron-fex-syscall-user-dispatch-design.md` (§4; §6 is M2, not here).

## Global Constraints

- Our fork only: never offered upstream ("AI must not be used to generate code for contributions to this project",
  FEX's CLAUDE.md). Never touch `build/wine-arm64-src/fex` (the macOS FEX).
- The kernel is the oracle: Linux 6.11 `kernel/entry/syscall_user_dispatch.c`. Values: `PR_SET_SYSCALL_USER_DISPATCH`
  59, `PR_SYS_DISPATCH_OFF` 0, `PR_SYS_DISPATCH_ON` 1, `SYSCALL_DISPATCH_FILTER_ALLOW` 0, `…_BLOCK` 1, `si_code`
  `SYS_USER_DISPATCH` 2, `AUDIT_ARCH_X86_64` 0xC000003E, `AUDIT_ARCH_I386` 0x40000003, user space ends at
  `TASK_SIZE_MAX` 0x7FFF'FFFF'F000.
- Testing (maintainer's lighter-testing rule): during tasks only the task's own lane; the full `run-all.sh` and the VM
  gates once, in Task 4.
- FEX's style (2-space indent, `LogMan`, comments that cite the Linux behaviour); known ceilings get a `ponytail:`
  comment. The FEX test follows `eac-vm/fex-tests/seccomp-trap/seccomp_trap_test.c` (raw `sc6`, `check(ok, what)` rows,
  `RESULT: PASS|FAIL`, exit status = failures).
- Standing rules: never open MacNeutron.app or start/quit Steam (the maintainer launches AoE2); no push, tag or
  release; no `pkill -f`; one VM run at a time; the builder starts with both shares:
  `VM_SHARES="out=$PWD/build/eac-vm-work/images,outp=$PWD/build/p/eac-vm-work/images" sh eac-vm/builder.sh start`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

- A handler that turns dispatch off and rewinds RIP by 2 (Wine's `!syscall_dispatch_enabled` path) must see the
  syscall run again, once: Task 1 row 14.
- A syscall inside the range while the selector says BLOCK must run (range before selector, as the kernel): row 8.
- A handler on an alternate signal stack (Wine installs its handlers with `SA_ONSTACK`): Task 1's handler is installed
  with `SA_ONSTACK` on a `sigaltstack`, so every dispatch row exercises it.
- Host stack per dispatch: 100000 dispatches must complete (a frame leaked per trap crashes after a few thousand): row 11.
- A Wine thread other than the first: Wine re-enables dispatch per thread on FEX's per-thread state: Task 3's
  second-thread row.

---

### Task 1: The `sud` test (RED on 3bb9e30)

**Files:**
- Create: `eac-vm/fex-tests/sud/sud_test.c`
- Create: `eac-vm/fex-tests/sud/build.sh` (copy of `seccomp-trap/build.sh`, file and binary names changed)
- Modify: `eac-vm/fex-tests/run-all.sh` (TESTS row before `pop-fault`: `sud                 sud_test                    PASS -`)
- Modify: `eac-vm/fex-tests/README.md` (test table row `sud` → patch 0036; expected-results: native "pending", stock
  FEX FAIL, ours PASS)

**Interfaces:**
- Produces: `sud_test` (x86-64 only: `#error` on other architectures; the native column stays "native run pending"
  until someone runs it on x86-64 Linux).

- [ ] **Step 1: Write the test.** Scaffolding from `seccomp_trap_test.c` (x86-64 half only); the raw thread from
  `signal-race/seccomp_signal_race_test.c`'s `spawn_thread`. A global `volatile char sel` (the selector). A gadget page
  mmap'd RWX with hint 0x600000000000 holding `0f 05 c3`, called as `call_gadget(nr, a, b, c)` (inline asm `call *%g`,
  rax/rdi/rsi/rdx in, rax out, rcx/r11/memory clobbered); `ip` below means gadget + 2. The test's own code is low
  (non-PIE static), so a range `[0, gadget)` holds every syscall but the gadget's. The SIGSYS handler is installed with
  `SA_SIGINFO|SA_RESTORER|SA_ONSTACK` on a `sigaltstack`; it records `si_signo`, `si_errno`, `si_code`,
  `si_call_addr`, `si_syscall`, `si_arch` (siginfo offsets 0, 4, 8, 16, 24, 28), the ucontext RIP and RAX (gregs
  slots 16 and 13 at offset 40), counts calls, sets `sel = 0`, writes `MAGIC` (0x5A5A) to RAX; in "rewind" mode it
  instead calls prctl OFF and subtracts 2 from RIP. Every row turns dispatch OFF at its end. Where a row sets
  `sel = 1`, it does so right before the gadget call and sets `sel = 0` right after it, before any other syscall (the
  test's own code is outside such a range). Rows:
  1. `prctl(59, 0, 1, 0, 0)` → -22.
  2. `prctl(59, 7, 0, 0, 0)` → -22.
  3. `prctl(59, 1, 0x1000, 0, 0)` → -22.
  4. `prctl(59, 1, 0xFFFFFFFFFFFFF000, 0x2000, 0)` → -22.
  5. `prctl(59, 1, 0, 0x1000, 0xFFFF800000000000)` → -14.
  6. `prctl(59, 1, 0, 0, &sel)` with `sel = 0` → 0; `getpid` then returns the pid; OFF → 0.
  7. ON `[0, ip + 1)`, no selector: `call_gadget(39)` == pid, handler count unchanged.
  8. ON `offset = ip, len = 1`, `&sel`, `sel = 1`: `call_gadget(39)` == pid, count unchanged.
  9. ON `[0, ip)`, no selector: `call_gadget(39)` == MAGIC, count +1; `si_signo` 31, `si_errno` 0, `si_code` 2,
     `si_call_addr` == ip, `si_syscall` 39, `si_arch` 0xC000003E, ucontext RIP == ip, ucontext RAX == 39.
  10. Same range: `pipe2(fds, O_NONBLOCK)`; `call_gadget(1, wfd, "x", 1)` == MAGIC; `read(rfd)` → -11 (the write
      never ran).
  11. Same range: 1000 calls each == MAGIC with count +1000; then 100000 calls complete.
  12. ON `[0, gadget)`, `&sel`: `sel = 0` → pid; `sel = 1` → MAGIC (handler resets to 0); alternated 10 times.
  13. ON `[0, ip)`, then OFF from the test's code: `call_gadget(39)` == pid.
  14. ON `[0, ip)`, rewind mode: `call_gadget(39)` == pid, count +1 exactly.
  15. Kills, each in a `fork` child (`wait4` status `& 0x7f` is the signal):
      a. ON `[0, ip)`, `&sel`, `sel = 2`, `call_gadget(39)` → 31.
      b. a page mmap'd and munmap'd, ON with the selector in it, `call_gadget(39)` → 11.
      c. SIGSYS blocked with `rt_sigprocmask`, ON `[0, ip)`, `call_gadget(39)` → 31.
      d. SIGSYS set to `SIG_IGN`, ON `[0, ip)`, `call_gadget(39)` → 31.
  16. ON `[0, ip)`, `fork`: the child's `call_gadget(39)` equals its own `getpid` → child exits 0; parent sees status 0.
  17. ON `[0, ip)`, a raw `clone` thread (flags 0x50f00): its `call_gadget(39)` == pid, count unchanged; it stores the
      result, sets a done flag, `SYS_exit`s; the main thread waits on the flag.
- [ ] **Step 2: Run it under today's FEX (0035) in the builder** — `sh eac-vm/builder.sh sync eac-vm/fex-tests
  fex-tests && sh eac-vm/builder.sh ssh 'cd fex-tests && mkdir -p build && cd build && sh ../sud/build.sh clang
  sud_test >/dev/null && ./sud_test; echo "exit $?"'`. Expected: RED (FEX passes prctl 59 to the arm64 host: rows fail
  or the process dies). Record the output's first failing row in the commit message.
- [ ] **Step 3: Commit**

```bash
git add eac-vm/fex-tests/sud eac-vm/fex-tests/run-all.sh eac-vm/fex-tests/README.md
git commit -m "eac-vm fex-tests: sud, syscall user dispatch as Wine uses it (RED on 0035: <first failing row>)"
```

### Task 2: Patch 0036, syscall user dispatch in FEX

**Files** (all under `build/eac-vm-work/fex-linux/Source/Tools/LinuxEmulation/`):
- Modify: `LinuxSyscalls/ThreadManager.h` (fields beside `SeccompTrapRIP`; widen its comment to "a SECCOMP_RET_TRAP or a
  syscall user dispatch")
- Modify: `LinuxSyscalls/SignalDelegator.h`, `LinuxSyscalls/SignalDelegator.cpp` (two methods; `ForceGuestSigSegv`
  keeps its log and calls `ForceExitSignal(SIGSEGV)`)
- Modify: `LinuxSyscalls/Syscalls.h`, `LinuxSyscalls/Syscalls.cpp` (two `SyscallHandler` methods; the call in
  `HandleSyscall`)
- Modify: `LinuxSyscalls/Syscalls/Thread.cpp` (prctl case; `ForkGuest`'s `IsChild` path clears the state)
- Create: `eac-vm/fex-patches/0036-*.patch`

**Interfaces:**
- Produces, in `ThreadStateObject`:
  `struct { bool On {}; uint64_t Offset {}; uint64_t Len {}; uint64_t Selector {}; } SyscallUserDispatch {};`
- Produces, in `SignalDelegator`:
  `[[noreturn]] void ForceExitSignal(int Signal);` (Linux `force_exit_sig`: the process dies by `Signal`, whatever the
  guest's handler or mask) and `void ForceGuestSignal(FEX::HLE::ThreadStateObject* Thread, siginfo_t* Info);` (Linux
  `force_sig_info`: guest-blocked — `Thread->SignalInfo.CurrentSignalMask.Val` — or guest `SIG_IGN` → `ForceExitSignal`;
  else `QueueSignal(::getpid(), FHU::Syscalls::gettid(), signo, Info, true)`)
- Produces, in `SyscallHandler`:
  `uint64_t SetSyscallUserDispatch(FEXCore::Core::CpuStateFrame* Frame, uint64_t Mode, uint64_t Offset, uint64_t Len, uint64_t Selector);`
  and `bool CheckSyscallUserDispatch(FEXCore::Core::CpuStateFrame* Frame, bool Is64Bit);` (true: dispatched or
  killed, the syscall must not run)

- [ ] **Step 1: State and prctl.** The `SyscallUserDispatch` field (not copied by `CreateThread`, which copies fields one
  by one, so threads start off; set to `{}` in `ForkGuest`'s child path). In the prctl switch:
  `case PR_SET_SYSCALL_USER_DISPATCH: return FEX::HLE::_SyscallHandler->SetSyscallUserDispatch(Frame, arg2, arg3, arg4, arg5);`
  (define 59 if the headers lack it). `SetSyscallUserDispatch` per spec §4.1: OFF with any nonzero argument → `-EINVAL`;
  ON with `Offset && Offset + Len <= Offset` → `-EINVAL`; ON with `Selector && Selector + 1 > 0x7FFF'FFFF'F000` →
  `-EFAULT`; other modes → `-EINVAL`; else store and return 0.
- [ ] **Step 2: The kill and delivery helpers** in `SignalDelegator` (Interfaces above).
- [ ] **Step 3: The check.** First statement after `Is64Bit` in `SyscallHandler::HandleSyscall`:
  `if (CheckSyscallUserDispatch(Frame, Is64Bit)) return;`, commented: before patch 0033's `int 0x80` branch and before
  the RIP advance, so a handler that rewinds RIP re-runs the syscall. `CheckSyscallUserDispatch`, per spec §4.2, with
  `State` the thread's `SyscallUserDispatch` (`ThreadManager::GetStateObjectFromCPUState(Frame)`):

```
if (!State.On) return false
ip = Frame->State.rip + 2
if (!Is64Bit && (ip == VDSO_kernel_sigreturn + 8 || ip == VDSO_kernel_rt_sigreturn + 7)) return false   // FEX::VDSO::GetVDSOSymbols()
if (ip - Offset < Len) return false
if (Selector):
  CopyFromUser(&b, Selector, 1) == EFAULT → ForceExitSignal(SIGSEGV)
  b == 0 → return false;  b != 1 → ForceExitSignal(SIGSYS)
Thread->SeccompTrapRIP = ip
ForceGuestSignal(Thread, {SIGSYS, si_errno 0, si_code 2, si_call_addr ip, si_syscall RAX (EAX when !Is64Bit), si_arch Is64Bit ? X86_64 : I386})
return true
```

  `ponytail:` comment on `si_arch`: a 64-bit process's `int 0x80` reports X86_64 here where Linux says I386.
- [ ] **Step 4: Commit in fex-linux, then build** (committed first so the stage's `.mn-source` stamp names it).
  Expected: exit 0. Later fixes: `git commit --amend` in fex-linux and rebuild.

```bash
git -C build/eac-vm-work/fex-linux commit -am "LinuxSyscalls: emulate syscall user dispatch (PR_SET_SYSCALL_USER_DISPATCH) as Linux 6.11 does"
sh eac-vm/image/guest-stages.sh fex
```

- [ ] **Step 5: Install it as the builder's binfmt interpreter** — `sh eac-vm/builder.sh ssh 'sudo install -m 755
  fex-linux/build/Bin/FEX fex-linux/build/Bin/FEXServer /usr/local/bin/ && sudo systemctl restart systemd-binfmt'`.
- [ ] **Step 6: Run the test** — Task 1 Step 2's command. Expected: every row `PASS`, `RESULT: PASS`, `exit 0`.
- [ ] **Step 7: Export and commit**

```bash
git -C build/eac-vm-work/fex-linux format-patch --start-number 36 -o "$PWD/eac-vm/fex-patches" 3bb9e30..HEAD
git add eac-vm/fex-patches
git commit -m "eac-vm: FEX patch 0036, syscall user dispatch for Wine's direct-syscall path"
```

### Task 3: Wine in the VM: `rawsyscall.exe` and `tests/sud.sh`

**Files:**
- Create: `eac-vm/guest/win/rawsyscall.c`
- Create: `eac-vm/guest/build-sud.sh` (as `build-input.sh`'s last command: llvm-mingw
  `x86_64-w64-mingw32-clang -O2 -o "$OUT/rawsyscall.exe" eac-vm/guest/win/rawsyscall.c`, `OUT` the share's `bin/`)
- Create: `eac-vm/tests/sud.sh`

**Interfaces:**
- Consumes: Task 2's FEX in the images.
- Produces: `bin/rawsyscall.exe` in the share; `tests/sud.sh` (exit 1 if any check misses), used by Task 4.

- [ ] **Step 1: Write `rawsyscall.c`.** Looks up `NtClose` and `NtQueryInformationProcess` in `ntdll.dll`; each stub
  must start `4c 8b d1 b8` (Wine's `__ASM_SYSCALL_FUNC`, `include/wine/asm.h`), else print `FAIL stub <name>` with the
  bytes; the number is the next 32-bit word. On a `VirtualAlloc` RWX page, one stub per call: `4c 8b d1 b8 <id> 0f 05
  c3`. Rows (`ok`/`FAIL` lines, then `RESULT: PASS|FAIL`, exit = failures): `NtClose((HANDLE)0xdead)` ==
  0xC0000008; `NtQueryInformationProcess(GetCurrentProcess(), 0, &pbi, sizeof pbi, &len)` == 0 and
  `pbi.UniqueProcessId == GetCurrentProcessId()`; both 1000 times; both again from a `CreateThread` thread.
- [ ] **Step 2: Write `tests/sud.sh`**, shaped like `tests/wine.sh`: Xvfb, prefix `/home/mn/sud` made with
  `WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i`, then `WINEDEBUG=warn+seh wine /mnt/share/bin/rawsyscall.exe` with
  output to a file that is printed. Checks: `RESULT` == `PASS`; `grep -c 'could not enable syscall user dispatch'` == 0.
- [ ] **Step 3: RED on today's images** — `sh eac-vm/guest/build-sud.sh && MN_STATE=$PWD/build/eac-vm-work/state/sud.img
  sh eac-vm/run-guest.sh eac-vm/tests/sud.sh`. Expected: exit 1, the warning count non-zero (the program may crash
  instead of printing FAIL: a Windows number runs as a Linux syscall).
- [ ] **Step 4: Build the images** with Task 2's FEX — builder started with both shares (Global Constraints), then
  `sh eac-vm/builder.sh sync eac-vm/image mn-image && sh eac-vm/builder.sh ssh 'sh mn-image/build.sh'`. Expected: exit
  0 and a new timestamp on `build/eac-vm-work/images/base.erofs`.
- [ ] **Step 5: GREEN** — Step 3's run command. Expected: every row `ok`, warning count 0, exit 0.
- [ ] **Step 6: Commit**

```bash
git add eac-vm/guest/win/rawsyscall.c eac-vm/guest/build-sud.sh eac-vm/tests/sud.sh
git commit -m "eac-vm tests: sud.sh, a Windows program's direct syscalls reach Wine under FEX"
```

### Task 4: M1 gate and the AoE2 check

**Files:** none new (fixes go to the owning task's commit; in fex-linux `git commit --fixup` + `rebase --autosquash`,
then re-export 0036).

- [ ] **Step 1: Full fex-tests** — `sh eac-vm/builder.sh sync eac-vm/fex-tests fex-tests && sh eac-vm/builder.sh ssh
  'cd fex-tests && rm -rf build && sh run-all.sh'`. Expected: exit 0, every row `ok` (`pop-fault` FAIL as expected).
- [ ] **Step 2: VM gates** — `MN_STATE=$PWD/build/eac-vm-work/state/wine.img sh eac-vm/run-guest.sh
  eac-vm/tests/wine.sh` (exit 0) and Task 3 Step 5's run (exit 0).
- [ ] **Step 3: Hand AoE2 to the maintainer.** Ask them to launch AoE2 DE from MacNeutron (dev app from this worktree)
  and close it or let it crash. Then: `/usr/bin/grep -c 'could not enable syscall user dispatch'
  build/eac-vm-work/runs/813780/run.log`. Expected: 0 (was 9). Note where AoE2 now stops: M2's starting point.
- [ ] **Step 4: Record** — update the `fex-syscall-user-dispatch` memory (M1 done, 0036 unreleased, AoE2's new stop)
  and report to the maintainer; M2's plan is written next.
