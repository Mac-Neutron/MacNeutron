# 32-bit games in the VM, M1: FEX mixed mode — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** FEX runs 32-bit code inside a 64-bit x86-64 Linux process the way Wine's new WoW64 drives it.

**Architecture:** Four commits on the guest FEX tree (`build/eac-vm-work/fex-linux`, branch `eac-fidelity`, after 0029's
`9006133`), exported with `git format-patch` as `eac-vm/fex-patches/0030`-`0033`, plus one freestanding test in
`eac-vm/fex-tests/wow64-mode/`. FEX already decodes and builds IR per block from the code segment's L bit; the commits add
the GDT entries, per-block x87 sizing, `%fs`/`%gs` selector loads in 64-bit code and `int 0x80`'s thread-area calls.

**Tech Stack:** C++ (FEXCore, LinuxEmulation), freestanding C + inline x86 assembly (test), the Fedora builder VM.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-vm-32bit-games-design.md` §4.

## Global Constraints

- The changes live only in our fork's series `eac-vm/fex-patches`; never offered upstream ("AI must not be used to
  generate code for contributions to this project", FEX's CLAUDE.md). The macOS FEX tree (`build/wine-arm64-src/fex`) is
  not touched.
- No gate per milestone (maintainer, 2026-10-20): each task's runs are development runs; the full fex-tests suite and the
  VM gates run once after M4 (spec §8).
- Known ceilings carry a `ponytail:` comment where they live: RIP-only block lookup, code cache single-mode, seccomp
  filters skip `int 0x80` from 64-bit code, `int 0x80` answers only 243 and 244.
- FEX's own code style (2-space indent, `Ref`, `_StoreContextGPR`, `LOGMAN_*`); the test follows
  `eac-vm/fex-tests/tf-iret/tf_iret_test.c` (raw syscalls, `check()` rows, `RESULT: PASS|FAIL`, exit = failures).
- Never check out `eac-fidelity` in `build/wine-arm64-src/fex` (that tree feeds the macOS build).

## Review Focus

- A 64-bit program that loads `%fs`/`%gs` with a selector now gets that descriptor's base (null: base 0, Intel's way).
  Glibc and Wine's 64-bit code never do; covered by test row 3 for the selector case.
- `int 0x80` from 64-bit code with any number but 243/244 answers `-ENOSYS` where Linux would run the compat call; a
  documented ceiling, not tested (a native run would disagree).
- A signal landing in 32-bit code: frame `cs` 0x23, `rip` the faulting instruction, `sigreturn` back into 32-bit mode;
  test row 6.
- An x87 memory operand in 32-bit code whose address wraps at 4 GiB; test row 7.
- Threads: the 32-bit segments exist in every 64-bit thread (new threads copy the parent's GDT, the first gets
  0030's defaults); Wine re-runs `set_thread_area` per thread, so no row.

---

### Task 1: The wow64-mode test

**Files:**
- Create: `eac-vm/fex-tests/wow64-mode/wow64_mode_test.c`
- Create: `eac-vm/fex-tests/wow64-mode/build.sh` (copy of `open-efault/build.sh` with the file and binary names changed)
- Modify: `eac-vm/fex-tests/run-all.sh` (TESTS table: `wow64-mode          wow64_mode_test             PASS -`)

**Interfaces:**
- Produces: binary `wow64_mode_test`, exit status = failed rows, last line `RESULT: PASS|FAIL`.

The program is static non-PIE (text and bss below 4 GiB, as 32-bit code needs). Shapes it uses:
- `int 0x80` from 64-bit code: `eax` = 243/244, `ebx` = pointer to a bss `struct user_desc { u32 entry_number, base_addr,
  limit; u32 flags; }` with `entry_number` -1, `base_addr` = address of `tls32` (a bss `u32[4]`, `tls32[0] =
  0x5EC0DE32`), `limit` 0xffffffff, `flags` 0x41 (seg_32bit | useable: Wine's `alloc_fs_sel`). Selector = entry * 8 + 3.
- `run32()` (asm, 64-bit): saves `rbx`, `rbp`, `r12`-`r15` and `rsp` in bss, switches `rsp` to the top of a 64 KiB
  16-aligned bss stack, pushes an `iretq` frame (ss 0x2b, the low rsp, rflags, cs 0x23, the 32-bit entry) and `iretq`s.
  The `.code32` part stores its results in bss and leaves with `ljmp *back64` (a bss `{u32 offset; u16 sel 0x33}`); the
  `.code64` landing restores the saved registers and `rsp` and returns.
- The SIGILL handler (`SA_SIGINFO | SA_RESTORER`, restorer `rt_sigreturn` as in tf-iret): records
  `gregs[18] & 0xffff` (REG_CSGSFS's cs) and `gregs[16]` (REG_RIP), adds 2 to `gregs[16]`.

- [ ] **Step 1: Write `wow64_mode_test.c` with these rows** (each `check(cond, text)`):

```c
check(r_set == 0 && desc.entry_number >= 12 && desc.entry_number <= 14,
      "1. int 0x80 set_thread_area from 64-bit code gives a TLS entry (12-14)");
check(r_get == 0 && getdesc.base_addr == (u32)(u64)tls32,
      "2. int 0x80 get_thread_area returns the entry's base");
check(have_tls && fs64_read == 0x5EC0DE32,
      "3. mov %fs with that selector in 64-bit code loads its base (%fs:0)");
check(res_cs == 0x23 && res_incdec == 6 && res_popa == 0x1234,
      "4. iretq to 0x23 runs 32-bit code (mov %cs, inc/dec at 0x40-0x4f, pusha/popa) and ljmp returns to 0x33");
check(have_tls && res_fs32 == 0x5EC0DE32,
      "5. 32-bit code reads the TLS base through %fs:0");
check(sig_cs == 0x23 && sig_rip == (u64)ud2_32 && res_after_sig == 1,
      "6. a signal in 32-bit code: frame cs 0x23, rip at the ud2, sigreturn resumes the 32-bit code");
check(res_x87 == 0x40200000,  /* 2.5f */
      "7. an x87 load in 32-bit code whose address wraps at 4 GiB reads the wrapped address");
```

  32-bit sequence (in this order): `mov %cs,%eax` → `res_cs`; `mov $5,%eax; .byte 0x40,0x40,0x48` → `res_incdec`;
  `mov $0x1234,%ebx; pusha; xor %ebx,%ebx; popa` → `res_popa`; if `have_tls`: `movl %fs:0,%edx` → `res_fs32`;
  `ud2_32: ud2`; `movl $1,res_after_sig`; `mov $0xF0000000,%eax; flds (0x10000000 + x87_val)(%eax); fstps res_x87`
  (`x87_val` a bss float = 2.5); `ljmp *back64`. Rows 3 and 5 run only when row 1 gave a selector (`have_tls`), so a
  missing `set_thread_area` fails them instead of faulting on `%fs:0`. The header comment says the expected values are
  Linux's documented behaviour, which Wine's x86-64 WoW64 relies on; no native x86-64 reference run exists here yet.

- [ ] **Step 2: Run it under today's FEX (0029) in the builder to see it fail**

Run: `eac-vm/builder.sh sync eac-vm/fex-tests fex-tests && eac-vm/builder.sh ssh 'cd fex-tests && mkdir -p build && cd build && sh ../wow64-mode/build.sh clang wow64_mode_test >/dev/null && ./wow64_mode_test; echo "exit $?"'`
Expected: `FAIL: 1.` (set_thread_area answers -38), rows 2, 3 and 5 FAIL, and either row 4/6 FAIL or the process dies at
the `iretq` (no `RESULT:` line). Note what it printed in the commit message.

- [ ] **Step 3: Commit**

```bash
git add eac-vm/fex-tests/wow64-mode eac-vm/fex-tests/run-all.sh
git commit -m "eac-vm fex-tests: wow64-mode, 32-bit code in a 64-bit process as Wine's new WoW64 runs it"
```

### Task 2: 0030, Linux's user segments in a 64-bit process's GDT

**Files:**
- Modify: `build/eac-vm-work/fex-linux/FEXCore/include/FEXCore/Core/CoreState.h:246-251` (beside `DEFAULT_USER_CS`)
- Modify: `build/eac-vm-work/fex-linux/Source/Tools/LinuxEmulation/LinuxSyscalls/ThreadManager.cpp:249-266` (default GDT)

- [ ] **Step 1: Add `constexpr static uint32_t USER32_CS = 4;` (0x23) and `USER_DS = 5;` (0x2b) to `CPUState`**
- [ ] **Step 2: In the default-GDT branch, when `Is64BitMode()`, also set entry `USER32_CS` (base 0, limit 0xF'FFFF,
  L 0, D 1) and entry `USER_DS` (base 0, limit 0xF'FFFF)**, with a comment that these are the kernel's `__USER32_CS` and
  `__USER_DS`, which Wine's WoW64 enters with `iretq`/`ljmp` and loads into `%ds`/`%es`/`%ss`.
- [ ] **Step 3: Build** — `sh eac-vm/image/guest-stages.sh fex` (from the worktree root; syncs `fex-linux` to the
  builder and runs `build-fex.sh`). Expected: exits 0.
- [ ] **Step 4: Commit in fex-linux**

```bash
git -C build/eac-vm-work/fex-linux commit -am "LinuxEmulation: a 64-bit process's GDT has the kernel's 32-bit code (0x23) and data (0x2b) segments"
```

### Task 3: 0031, the operating mode per block

**Files:**
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/Core/Frontend.cpp:1767` (drop the assert)
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/Core/OpcodeDispatcher.cpp:4042` (drop the assert; in
  `BeginFunction` set the emitter's `GuestGPRSize` to `GetGPROpSize()`)
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/IR/IREmitter.h` (public field)
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/IR/Passes/x87StackOptimizationPass.cpp:154-164,725`

**Interfaces:**
- Produces: `IR::OpSize IREmitter::GuestGPRSize {IR::OpSize::i64Bit};` — the GPR size of the block being built; set by
  `OpDispatchBuilder::BeginFunction`.

- [ ] **Step 1: Remove both "Expected operating mode to not change at runtime!" asserts.** Where the Frontend one stood,
  a `ponytail:` comment: blocks are looked up by RIP alone, so a RIP run in both modes would reuse the first mode's code
  (Wine's 32- and 64-bit code never share an address); the code cache (`EnableCodeCachingWIP`, off) assumes one mode.
- [ ] **Step 2: Add `GuestGPRSize` to `IREmitter`, set it in `BeginFunction`, and make `X87StackOptimization::GPROpSize`
  non-const, assigned from `Emit->GuestGPRSize` at the top of `X87StackOptimization::Run`** (the constructor argument
  stays as the initial value, so `PassManager.cpp` doesn't change).
- [ ] **Step 3: Build** — `sh eac-vm/image/guest-stages.sh fex`. Expected: exits 0.
- [ ] **Step 4: Commit in fex-linux**

```bash
git -C build/eac-vm-work/fex-linux commit -am "FEXCore: the operating mode is each block's, and the x87 pass sizes addresses by it"
```

### Task 4: 0032, `%fs` and `%gs` selectors in 64-bit code

**Files:**
- Modify: `build/eac-vm-work/fex-linux/FEXCore/Source/Interface/Core/OpcodeDispatcher.cpp:1228` (`MOVSegOp`)

- [ ] **Step 1: In `MOVSegOp`'s write side, FS and GS take the 32-bit path in both modes** (store `fs_idx`/`gs_idx`,
  `UpdatePrefixFromSegment`), replacing the 64-bit `DecodeFailure`. Rewrite the function's leading comment: in 64-bit
  mode a non-null selector loads the descriptor's 32-bit base (Wine's WoW64 loads its 32-bit TEB this way); a null
  selector loads base 0 as Intel CPUs do (AMD keeps the base); 64-bit code then sets a full base with `wrfsbase` or
  `arch_prctl`.
- [ ] **Step 2: On the read side, FS and GS return `fs_idx`/`gs_idx` in both modes** (64-bit returned constant 0).
  `POPSegmentOp` already does both in 64-bit mode; no change there.
- [ ] **Step 3: Build** — `sh eac-vm/image/guest-stages.sh fex`. Expected: exits 0.
- [ ] **Step 4: Commit in fex-linux**

```bash
git -C build/eac-vm-work/fex-linux commit -am "FEXCore: mov to %fs/%gs in 64-bit code loads the selector and its base, mov from them reads the selector"
```

### Task 5: 0033, `int 0x80` from a 64-bit process

**Files:**
- Modify: `build/eac-vm-work/fex-linux/Source/Tools/LinuxEmulation/LinuxSyscalls/Syscalls.cpp:945` (`HandleSyscall`)
- Modify: `build/eac-vm-work/fex-linux/Source/Tools/LinuxEmulation/LinuxSyscalls/x32/Thread.cpp:154-181`
- Modify: `build/eac-vm-work/fex-linux/Source/Tools/LinuxEmulation/LinuxSyscalls/x32/Thread.h:29`

**Interfaces:**
- Produces: `uint64_t FEX::HLE::x32::GetThreadArea(FEXCore::Core::CpuStateFrame* Frame, void* tls);` (the body of
  today's `get_thread_area` lambda, which now calls it), declared beside `SetThreadArea`.

- [ ] **Step 1: Extract `GetThreadArea` from the x32 `get_thread_area` lambda.**
- [ ] **Step 2: In `HandleSyscall`, before the 64-bit `HandleSyscallImpl`: when `Is64BitMode()` and the two bytes at
  `Frame->State.rip` are `0xCD 0x80`, answer with the i386 ABI** — number in `eax`, first argument `ebx` (zero-extended
  to a pointer): 243 → `x32::SetThreadArea`, 244 → `x32::GetThreadArea`, anything else `-ENOSYS`; the 64-bit result goes
  to `rax` as Linux returns it (sign-extended). The existing `rip += 2` stays after both paths. `ponytail:` comment: only
  the two calls Wine's x86-64 WoW64 makes (`alloc_fs_sel`), answered before any seccomp filter; the x32 table and the
  filter when a program needs them. This also answers the TODO above the dispatch ("runtime selectable based on
  `syscall` versus `int 0x80`") for this one case.
- [ ] **Step 3: Build** — `sh eac-vm/image/guest-stages.sh fex`. Expected: exits 0.
- [ ] **Step 4: Commit in fex-linux**

```bash
git -C build/eac-vm-work/fex-linux commit -am "LinuxSyscalls: int 0x80 from a 64-bit process answers set_thread_area and get_thread_area with the i386 ABI"
```

### Task 6: Export the series and run the test on it

**Files:**
- Create: `eac-vm/fex-patches/0030-*.patch` … `0033-*.patch`

- [ ] **Step 1: Export** — `git -C build/eac-vm-work/fex-linux format-patch --start-number 30 -o "$PWD/eac-vm/fex-patches" 9006133..HEAD`.
  Expected: four files, 0030 to 0033, each `Subject: [PATCH …]` as 0029's.
- [ ] **Step 2: Install the built FEX as the builder's binfmt interpreter** — `eac-vm/builder.sh ssh 'sudo install -m 755
  fex-linux/build/Bin/FEX fex-linux/build/Bin/FEXServer /usr/local/bin/ && sudo systemctl restart systemd-binfmt'`
  (binfmt uses the F flag: re-register after reinstalling).
- [ ] **Step 3: Run the test** — the Task 1 Step 2 command. Expected: rows 1-7 `PASS`, `RESULT: PASS`, `exit 0`.
  If a row fails, fix it in the owning task's commit (`git commit --fixup` + `rebase --autosquash` in fex-linux) and
  re-export.
- [ ] **Step 4: Commit**

```bash
git add eac-vm/fex-patches
git commit -m "eac-vm: FEX patches 0030-0033, 32-bit code in a 64-bit process for Wine's new WoW64"
```
