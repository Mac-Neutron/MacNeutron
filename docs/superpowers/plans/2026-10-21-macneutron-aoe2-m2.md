# AoE2 DE to its main menu in the VM (M2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Age of Empires II DE (813780) reaches its main menu in the VM, by fixing our stack (FEX, Wine, the guest), one
difference from Windows at a time.

**Architecture:** First a crashing run must end instead of waiting behind winedbg's crash window (Proton's Wine sets
`ShowCrashDialog = 0`; our guest prefix doesn't). Then FEX reports an invalid opcode as Linux does (trap 6,
`ILL_ILLOPN`), a fidelity fix that is **not** expected to change AoE2's outcome. Then a diagnostic run with Wine traces
finds the first difference behind AoE2's current stop, and a loop fixes differences one by one, each with a test.

**Tech Stack:** POSIX sh (guest scripts), C with llvm-mingw (Windows test programs), C++ (FEX), freestanding C + x86-64
asm (fex-tests), the builder VM and the microVM.

**Spec:** `docs/superpowers/specs/2026-10-21-macneutron-fex-syscall-user-dispatch-design.md` §6 (M2). M1's results and
the AoE2 run it ends with: `.superpowers/sdd/aoe2-m2-inputs/` (`aoe2-m1-run.log`, `m1-ledger.md`).

## Where AoE2 stops now (M1's run)

44 direct syscalls dispatched (anti-tamper: gadget page set up, `NtQuerySystemInformation` ×2, `NtCreateSection` and 14
map/unmap pairs, `NtQueryInformationProcess`, `NtReadVirtualMemory` ×3, `NtQueryVirtualMemory` ×3, **`NtCreateFile` ×3
last**), then the game's first exception: `#UD` at 0x142B9A04C. On disk those bytes are `60 f0 39 65 dc` where a
prologue belongs (`60` is invalid in 64-bit code): most likely a protector nanomite or stolen bytes. The vectored
handler at 0x142BF8308 returns CONTINUE_SEARCH, the SEH filter declines, and winedbg starts and never ends. FEX raised
the `#UD` as SIGILL with trap 0 (`InvalidOp`), which Wine logs as `unexpected trap 0` and still maps to
`EXCEPTION_ILLEGAL_INSTRUCTION`, as Windows reports a `#UD`.

## Global Constraints

- Fix our stack, never the game or its protector. Don't statically reverse the protector's handler (it starts with an
  obfuscated `jmp`); the evidence comes from traced runs.
- Proton parity: where Proton's Wine (ValveSoftware/wine) differs from upstream Wine for a reason that matters here,
  follow it. Porting a Proton-only Wine patch is the maintainer's decision: stop and ask.
- FEX changes: our fork only (`eac-vm/fex-patches`, next number 0037), never upstream; a fex-tests row each, expected
  results from the Linux kernel's behaviour. Don't amend 0036.
- Every AoE2 run is launched by the maintainer (standing rule: the executor never starts Steam or MacNeutron.app, and
  never launches a game through Steam unless the maintainer allows it in the review). Copy
  `build/eac-vm-work/runs/813780/run.log` right after each run: `run-guest.sh` deletes it at the next launch.
- Not in this plan: M1's deferred minors and the trap-frame crash under signal floods (reported separately).
- Testing: each task's own test; the guest gates (`wine.sh`, `sud.sh`, the new `crash.sh`) and the full fex-tests once
  before the report. No push, tag or release.

## Review Focus

- An existing prefix (AoE2's) that lacks `ShowCrashDialog` must get it at its next launch, not only new prefixes: Task 1's
  GREEN runs on the state disk the RED run made with today's script.
- A crashing run whose winedbg itself hangs under FEX would still hang with the dialog off: Task 1's test asserts
  winedbg's report lines, so "ended because killed" can't pass for "ended".
- The real bytes at the fault may differ from the file (a protector restores or encrypts code at runtime): Task 3 reads
  them from winedbg's report before any conclusion.
- `mov cs, r` and `InvalidOp` both raise `#UD` on hardware: Task 2 tests both.

---

### Task 1: A crashing run ends (Proton's `ShowCrashDialog = 0`)

**Files:**
- Modify: `eac-vm/guest/run-game.sh` (the `EmulateModeset` block, ~line 48)
- Create: `eac-vm/guest/win/crash.c`
- Create: `eac-vm/tests/crash.sh` (host-side, shaped like `eac-vm/tests/modes.sh`)
- Rename: `eac-vm/guest/build-sud.sh` → `eac-vm/guest/build-win-tests.sh` (builds `rawsyscall.exe` and `crash.exe`; fix
  the name in `eac-vm/tests/sud.sh`'s header)

**Interfaces:**
- Produces: `crash.exe` in the share's `bin/`; `tests/crash.sh` (prints `crash: ok` or `crash: FAIL <what>`, exit 1 on
  FAIL).

- [ ] **Step 1: Write `crash.c`**: prints `crash: about to fault`, flushes, writes through a null pointer. Rename the
  build script: `git mv eac-vm/guest/build-sud.sh eac-vm/guest/build-win-tests.sh`, add `crash.exe` to it.
- [ ] **Step 2: Write `tests/crash.sh`** as `modes.sh`: build both exes (`build-win-tests.sh`), write a guest script
  `VERB=run APPID=0 GAME_DIR=/mnt/share/bin EXE=crash.exe`, `set --`, then `run-game.sh`; run it with
  `MN_STATE=$W/state/crash.img MN_AUDIO_OUTPUT=none MN_RUN_TIMEOUT=300`. Checks: no `the guest still runs` (run-guest's
  timeout message); the output has `crash: about to fault`, `Unhandled exception: page fault` and `Backtrace:` (winedbg's `--auto`
  report).
- [ ] **Step 3: RED with today's `run-game.sh`**: `rm -f build/eac-vm-work/state/crash.img; sh eac-vm/tests/crash.sh`.
  Expected: `crash: FAIL` with the timeout (the dialog waits on Xvfb). If instead winedbg's report is there and the run
  ended, the hang has another cause: stop, ledger it, and debug that before Step 4.
- [ ] **Step 4: Fix**: in `run-game.sh`'s registry block, also `wine reg add 'HKCU\Software\Wine\WineDbg' /v
  ShowCrashDialog /t REG_DWORD /d 0 /f`, guarded like `EmulateModeset` (grep `user.reg` for
  `"ShowCrashDialog"=dword:00000000`; one `wineserver -k` after either add). Comment: Proton's Wine sets it in
  `wine.inf`'s `[Debugger]`; the macOS prefix gets it from `PrefixManager.swift`. Copy the script into the VM folder as
  `eac-vm/host/build.sh` does: `cp eac-vm/guest/run-game.sh build/eac-vm-work/run-game.sh`.
- [ ] **Step 5: GREEN on the same state disk** (the RED run's prefix, which lacks the value): `sh eac-vm/tests/crash.sh`.
  Expected: `crash: ok`. Then once more (value already there): `crash: ok`.
- [ ] **Step 6: Commit**

```bash
git add eac-vm/guest/run-game.sh eac-vm/guest/win/crash.c eac-vm/tests/crash.sh eac-vm/guest/build-win-tests.sh eac-vm/tests/sud.sh
git commit -m "eac-vm guest: a crashing game ends instead of waiting behind winedbg's dialog (Proton's ShowCrashDialog=0)"
```

### Task 2: Patch 0037, an invalid opcode is SIGILL/`ILL_ILLOPN` with trap 6

States plainly: this removes Wine's `unexpected trap 0` and matches Linux; it does **not** change what AoE2 gets (Wine
maps both trap numbers to `EXCEPTION_ILLEGAL_INSTRUCTION`). Do it while waiting for Task 3's launch.

**Files:**
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/Core/OpcodeDispatcher.cpp` (`InvalidOp`, ~5186; the
  CS case of the segment-register move, ~1253)
- Create: `eac-vm/fex-tests/ud-trap/ud_trap_test.c`, `eac-vm/fex-tests/ud-trap/build.sh` (copy of `sud/build.sh`, names
  changed); Modify: `eac-vm/fex-tests/run-all.sh` (row `ud-trap  ud_trap_test  PASS -` before `pop-fault`),
  `eac-vm/fex-tests/README.md` (row, patch 0037)
- Create: `eac-vm/fex-patches/0037-*.patch`

- [ ] **Step 1: Test** (scaffolding from `sud_test.c`): a SIGILL handler (`SA_SIGINFO`) records `si_code`, `si_addr`,
  the ucontext trapno (gregs slot 20, `REG_TRAPNO`) and RIP, then moves RIP past the instruction. Rows: the byte `60`
  and the bytes `8e c8` (`mov cs, ax`), each on an mmap'd RWX page → SIGILL, `si_code` 2 (`ILL_ILLOPN`), `si_addr` ==
  the instruction, trapno 6, RIP == the instruction; then execution continues.
- [ ] **Step 2: RED** on the installed FEX (0036): build and run in the builder as `sud`'s command. Expected: FAIL
  (trapno 0, `si_code` 0).
- [ ] **Step 3: Fix**: both Breaks get `.TrapNumber = X86State::X86_TRAPNO_UD, .si_code = 2 /* ILL_ILLOPN */`, as
  `UnimplementedOp` already has (Linux `do_error_trap` for `#UD`).
- [ ] **Step 4: Commit in fex-linux, build, install, GREEN**: `git -C build/eac-vm-work/fex-linux commit -am
  "FEXCore: an invalid opcode raises SIGILL ILL_ILLOPN with trap 6, as Linux"`, `sh eac-vm/image/guest-stages.sh fex`,
  install as M1's Task 2 Step 5, run the test: PASS; `sud` still PASS.
- [ ] **Step 5: Export and commit**: `git -C build/eac-vm-work/fex-linux format-patch --start-number 37 -o
  "$PWD/eac-vm/fex-patches" HEAD~1..HEAD`, then commit the tests and the patch.

### Task 3: The diagnostic run

**Interfaces:**
- Consumes: Task 1 (winedbg now prints its report into `run.log` and the run ends). Task 2 isn't needed.

- [ ] **Step 1: Images for the run**: if Task 2 is done, rebuild the images (M1's command, builder with both shares) so
  the run has 0037; otherwise the 0036 images serve.
- [ ] **Step 2: Ask the maintainer to launch AoE2** with the Steam launch option
  `/usr/bin/env WINEDEBUG=+seh,+loaddll,+file,+virtual,+debugstr %command%` (`LaunchEnvironment.wineDebug` keeps a
  user's WINEDEBUG; `MicroVM.swift` passes it to the guest), and to remove the option afterwards. Then copy
  `runs/813780/run.log` to `.superpowers/sdd/aoe2-m2-inputs/aoe2-m2-run1.log`.
- [ ] **Step 3: Read it** and write down, in the M2 ledger: the three `NtCreateFile` names and statuses; the
  `NtQueryVirtualMemory` classes and results before them; the `NtQuerySystemInformation` classes; the in-memory bytes
  and disassembly winedbg shows at the fault (the file's `60 f0 39 65 dc` is a hypothesis until this confirms it); any
  syscall or memory query inside the vectored handler's call; any failed call (non-zero status) before the fault.
- [ ] **Step 4: Name the first difference from Windows** (with evidence lines), or the next trace to take if none shows.
  Report it to the maintainer with the proposed fix's class (Task 4).

### Task 4: The loop

Per difference, one iteration:

1. **Classify** it: (a) FEX fidelity → a fex-patch (next number) with a fex-tests row; (b) Wine 11.20 differs from Windows
   where Proton's Wine has a fix → **stop and ask the maintainer** (porting it is their call), naming the Proton commit;
   (c) upstream Wine and Proton's Wine both differ from Windows → a guest Wine patch (`eac-vm/wine-patches`) with a
   guest test, after the maintainer's OK; (d) guest image or configuration → `run-game.sh` or the image, with a guest
   test.
2. **RED→GREEN** with that test, as in Tasks 1 and 2; images rebuilt when the guest changed.
3. **Ask for a launch** (Task 3 Step 2's option, channels chosen for the new stop), copy the log, read it, ledger the
   new stop.
4. Repeat until the main menu shows; then the gates (`run-all.sh`, `wine.sh`, `sud.sh`, `crash.sh`) and the report.

Stop for: downloads, a Proton-only patch to port, anything that touches the maintainer's data or Steam, or a
difference that can only be the protector's own policy (e.g. it refuses to run under any emulator).
