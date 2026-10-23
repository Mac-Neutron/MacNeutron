# FEX stale returns (0043) and a 512 MiB code buffer (0044) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A guest `ret` never runs a translation FEX has discarded (patch 0043); then FEX's code buffer grows to 512 MiB (0044) so AoE2 DE stops retranslating its code every few seconds.

**Architecture:** The JIT records each call's return point (`l_CallReturn`) per translation and emits a 16-byte stub per return point that exits to the dispatcher with the return block's guest RIP. When `GuestToHostMap::Erase` discards a translation, every return point in it is overwritten with `b <its stub>`, once per translation. 0044 then raises `MAX_CODE_SIZE`.

**Tech Stack:** FEX fork (`build/eac-vm-work/fex-linux`, branch eac-fidelity, C++/ARMEmitter), freestanding x86-64 tests (`eac-vm/fex-tests`, clang + `tools/static-link.py`), the Fedora builder (`eac-vm/builder.sh`), the guest image, AoE2 launches through the running Steam.

**Spec:** `docs/superpowers/specs/2026-10-23-macneutron-fex-stale-returns-design.md`

## Global Constraints

- Fork-only FEX patches (FEX's CLAUDE.md forbids AI-made contributions upstream); fork commits carry no trailer; subjects `<Area>: <what>` as 0036-0042.
- Each patch: commit in fex-linux, `git format-patch -1 --stdout HEAD > eac-vm/fex-patches/004N-<slug cut at 52>.patch`.
- Repo commits end with the session's `Co-Authored-By` and `Claude-Session` lines; privacy scan (`/Users/`, 17-digit SteamIDs) before each; nothing pushed.
- fex-tests rule (README): freestanding x86-64, prints `RESULT: PASS`/`FAIL`, passes on a real x86-64 kernel, fails without its patch, listed in `run-all.sh` and the README tables.
- Builder runs: `sh eac-vm/builder.sh sync eac-vm/fex-tests fex-tests`; FEX built by `sh eac-vm/image/guest-stages.sh fex`; installed for tests with `sh eac-vm/builder.sh ssh 'sudo install -m 755 fex-linux/build/Bin/FEX fex-linux/build/Bin/FEXServer /usr/local/bin/ && sudo systemctl restart systemd-binfmt'`.
- Images from committed sources only: `sh eac-vm/builder.sh sync eac-vm/image mn-image && sh eac-vm/builder.sh ssh 'sh mn-image/build.sh'`; `build/eac-vm-work/run-game.sh` equals `eac-vm/guest/run-game.sh` after any diagnostic.
- AoE2 only via `open steam://rungameid/813780` with Steam already running; one VM at a time; menu = `is_menu.py` (scratchpad), never PNG size. Scratchpad tools: `launches3.sh <n> <label> [launcher copy]`, `is_menu.py`, `timeshots.sh`, `stats_dec.py`, `run-game.probeB.sh` (block naming), all in `/private/tmp/claude-501/-Users-chad-Documents-MacProton/7478e930-d11c-4271-8746-ea69c5da55ac/scratchpad`.
- Values: stub 16 bytes (`ldr TMP1, literal` / `b common` / 8-byte RIP), 8-aligned; `DiskCache::FormatVersion` 30 -> 31; `MAX_CODE_SIZE` 512 MiB.

## Review Focus

1. A return point whose return block is not emitted right after the call (the `b PendingCallReturnTargetLabel` layout): the patched instruction is that `b`; Task 2's indirect-call row and the stress row exercise call layouts the direct row doesn't.
2. 32-bit guest code under new or old WoW64 (32-bit return RIPs in a 64-bit literal): Task 2's run-all includes `wow64-mode`; Task 3 Step 2 runs the guest's `eac-vm/tests/old-wow64.sh`.
3. A return point patched while another thread is executing it or returning to it: Task 2's stress row (4 callers, 1 rewriter, 8 s).
4. A translation in an older code buffer still referenced by a thread that hasn't switched: `InvalidateCodeBuffersCodeRange` walks every live buffer's map, so Erase patches it too; Task 4's AoE2 launches (buffer growth 128 -> 256 -> 512) exercise it.
5. A translation loaded from the disk cache (off by default): the stub literal goes through `InsertGuestRIPLiteral` and the format version bumps; no test (not enabled in MacNeutron), noted in the patch message.

---

### Task 1: callret-stale, three rows, RED on 0042

**Files:**
- Modify: `eac-vm/fex-tests/callret-stale/callret_stale_test.c` (exists, uncommitted: the direct row)
- Exists: `eac-vm/fex-tests/callret-stale/build.sh`

**Interfaces:**
- Produces: binary `callret_stale_test`, rows printed `PASS:`/`FAIL:` then `RESULT:`; re-execs itself with `FEX_SMCHOTPAGEFAULTS=0` (as cpuid-fault re-execs) so the stress row's pages stay write-protected (a hot page's per-instruction check would hide the bug).

- [ ] **Step 1: Add the indirect row.** Same shape as the direct row with A = `mov rax, imm64(G)` (48 b8), `call rax` (ff d0), `mov eax, imm32` (b8), `ret`; `write_imm(offset, v)` takes the imm32's offset (direct: 4096+6, indirect: 4096+13). Check line: `"a return after the caller was rewritten ran the new code (call rax)"`, 64 rounds.
- [ ] **Step 2: Add the stress row.** N = 1024: X_i on page 2i (`mov edi, i`; `call F_i`; `ret`), F_i on page 2i+1 (`call L`; `mov eax, imm32`; `ret`), all mapped near the binary (MAP_FIXED_NOREPLACE at 0x30000000). L is a naked asm function: `push rbx; xor eax, eax; cpuid; pop rbx; mov eax, 24; syscall (sched_yield); mov rdx, published[rdi]; ret`. Four workers loop for 8 s picking i (xorshift) and calling X_i through inline asm that returns rax and rdx; each counts `(u32)rax < rdx` as a violation. A writer loops generations g = 1, 2, ...: for each i, store g into F_i's imm32, then `__atomic_store_n(&published[i], g, __ATOMIC_RELEASE)`. Check line: `"no return older than the code at the callee's return (4 callers, 1 rewriter)"`; print calls, generations, violations.
- [ ] **Step 3: Re-exec with `FEX_SMCHOTPAGEFAULTS=0`** at entry, as `cpuid-fault/cpuid_fault_test.c` adds its variable (pass the original envp pointers through, no fixed-size copies).
- [ ] **Step 4: RED on the installed FEX (7740725).** Run: `sh eac-vm/builder.sh sync eac-vm/fex-tests fex-tests && sh eac-vm/builder.sh ssh 'cd fex-tests/build && sh ../callret-stale/build.sh clang callret_stale_test >/dev/null && timeout 90 ./callret_stale_test'`. Expected: the direct row FAIL 64/64; the indirect row FAIL (any count > 0); the stress row FAIL (violations > 0; if 0, lengthen L's park with a second `sched_yield` and rerun); `RESULT: FAIL`. Record the counts in the ledger.

### Task 2: patch 0043, return points of a discarded translation exit to the dispatcher

**Files:**
- Modify: `FEXCore/Source/Interface/Core/JIT/JITClass.h` (pending return points, return-block RIPs)
- Modify: `FEXCore/Source/Interface/Core/JIT/BranchOps.cpp` (both `ExitFunction` call paths, after `Bind(&l_CallReturn)`)
- Modify: `FEXCore/Source/Interface/Core/JIT/JIT.cpp` (RIP of each return block, stubs, tail table, the patch function)
- Modify: `FEXCore/Source/Interface/Core/CPUBackend.h` (`JITCodeTail` fields, the patch function's declaration)
- Modify: `FEXCore/Source/Interface/Core/LookupCache.h` (`BlockEntry::TranslationBegin`, `AddBlockMapping`, `Erase`)
- Modify: `FEXCore/Source/Interface/Core/Core.cpp` (both `AddBlockMapping` callers)
- Modify: `FEXCore/Source/Interface/Core/DiskCache.h` (`FormatVersion` 31)
- Repo: `eac-vm/fex-tests/run-all.sh`, `eac-vm/fex-tests/README.md`, `eac-vm/fex-patches/0043-*.patch`

**Interfaces:**
- Consumes: Task 1's test.
- Produces: `void FEXCore::CPU::PatchDiscardedReturnPoints(uint8_t* TranslationBegin)` (declared in `CPUBackend.h` at namespace scope, defined in `JIT.cpp`); `GuestToHostMap::BlockEntry::TranslationBegin` (`uint8_t*`); `LookupCache::AddBlockMapping(Thread, Address, CodePages, HostCode, uint8_t* TranslationBegin)` and `GuestToHostMap::AddBlockMapping(Address, CodePages, HostCode, TranslationBegin, lk)`; `JITCodeTail::{NumberOfReturnPoints, OffsetToReturnPoints, OffsetToReturnStubs}` (u32, offsets from the translation's start) and `ReturnPointsPatched` (the first byte of `_Pad`, accessed with `std::atomic_ref`).

- [ ] **Step 1: Record return points.** `JITClass.h`: `struct PendingReturnPoint { uint64_t Location; IR::NodeID ReturnBlock; };`, `fextl::vector<PendingReturnPoint> PendingReturnPoints;`, `fextl::map<IR::NodeID, uint64_t> ReturnBlockRIPs;`, both cleared where `CallReturnTargets.clear()` is. `BranchOps.cpp`: in each call path that pushed `adr TMP1, &l_CallReturn`, after `Bind(&l_CallReturn)` push `{GetCursorAddress<uint64_t>(), Op->CallReturnBlock.ID()}`. `JIT.cpp`: in the `BlockIROp->EntryPoint` branch, `ReturnBlockRIPs[Node] = BlockStartRIP`.
- [ ] **Step 2: Emit stubs and the table.** After the exit-linker literal, if `PendingReturnPoints` is non-empty: `Align(8)`, then per point k a 16-byte stub (`ldr TMP1, <label_k>`, `b <common>`, bind label_k, `PlaceNamedSymbolLiteral(InsertGuestRIPLiteral(ReturnBlockRIPs.at(point.ReturnBlock)))`), then common: `str TMP1, STATE, offsetof(CpuStateFrame, State.rip)`; `ldr TMP2, STATE, offsetof(CpuStateFrame, Pointers.DispatcherLoopTop)`; `br TMP2`. Append the u32 offsets after the RIP entries (4-aligned, inside the block's `Size`); fill the three tail fields (all 0 without return points). A `LOGMAN_THROW_A_FMT` that each return point is inside `[CodeBegin, stubs)`.
- [ ] **Step 3: The patch function.** `PatchDiscardedReturnPoints`: header → tail (`OffsetToBlockTail`); return if `NumberOfReturnPoints == 0` or `std::atomic_ref(ReturnPointsPatched).exchange(1)` was 1 (the byte written through `ARMEmitter::DualMap::WriteAddr`); per point, encode `b (stub_k - point)` as `DirectBlockDelinker` encodes its branch, store it 32-bit atomically through `DualMap::WriteAddr`, `ClearICache(point, 4)`.
- [ ] **Step 4: Entries know their translation.** `BlockEntry` gains `uint8_t* TranslationBegin`; both `AddBlockMapping` levels take it; `Core.cpp` passes `CompiledCode.BlockBegin` (compile) and `LoadedCode.BlockBegin` (disk cache hit). `GuestToHostMap::Erase`: before `BlockList.erase(Address)`, if the entry exists and has a `TranslationBegin`, call `CPU::PatchDiscardedReturnPoints`. `DiskCache.h`: `FormatVersion = 31`.
- [ ] **Step 5: Build, install, GREEN.** Run: `sh eac-vm/image/guest-stages.sh fex` then the install command, then Task 1's run command. Expected: all three rows PASS, `RESULT: PASS`.
- [ ] **Step 6: Whole suite.** Add `callret-stale        callret_stale_test          PASS -` to `run-all.sh` after jit-capacity; README: the test row (patch 0043) and the expected-results row (`PASS (derived from the kernel source; native run pending) | FAIL | PASS`). Run: `sh eac-vm/builder.sh sync eac-vm/fex-tests fex-tests && sh eac-vm/builder.sh ssh 'cd fex-tests && sh run-all.sh; echo "run-all exit $?"'`. Expected: every line `ok`, `run-all exit 0`.
- [ ] **Step 7: Commit.** fex-linux: `git commit` (subject `FEXCore: a return into a discarded translation goes to the dispatcher`, body: the bug, the fix, the test's RED/GREEN counts, the disk-cache note), export `0043-*.patch`; repo commit `eac-vm: FEX patch 0043, no return into a discarded translation; fex-tests callret-stale` with the patch, the test folder, run-all.sh and README.

### Task 3: 0043 in AoE2

**Files:** none in the repo (images and ledger).

**Interfaces:**
- Consumes: Task 2's committed fex-linux.

- [ ] **Step 1: Images.** Run the image build (Global Constraints). Expected: `base.erofs` newer, builder `.mn-source` names Task 2's fex-linux commit.
- [ ] **Step 2: 32-bit guest check.** Run: `MN_STATE=$PWD/build/eac-vm-work/state/old-wow64.img MN_AUDIO_OUTPUT=none sh eac-vm/run-guest.sh eac-vm/tests/old-wow64.sh`. Expected: its checks pass as before.
- [ ] **Step 3: Ten launches.** Run: `$S/launches3.sh 10 T3`. Expected: 10 menus; median ≤ 51 s (0042: ~49 s).
- [ ] **Step 4: Code size.** One launch with `$S/run-game.probeB.sh` as the launcher copy (`launches3.sh 1 T3size $S/run-game.probeB.sh`), then the exe-range bytes-per-compile script from the spike over `perf.60`. Expected: within +10% of the spike's 421-474 B per compile; record it in the ledger.

### Task 4: patch 0044, a 512 MiB code buffer

**Files:**
- Modify: `FEXCore/Source/Interface/Core/SharedCodeBufferManager.cpp` (`MAX_CODE_SIZE` and its comment, the spec's §5 wording)
- Modify: `eac-vm/fex-tests/jit-capacity/jit_capacity_test.c` (the code-buffer row back, from scratchpad `jit_capacity_test.with-code-buffer.c`), `eac-vm/fex-tests/README.md` (its row)
- Repo: `eac-vm/fex-patches/0044-*.patch`

- [ ] **Step 1: RED.** Restore the code-buffer row (250,000 functions, 120 ops, 1 KiB stride, at most 5 passes, pass when a later pass translates < 1%); install Task 2's FEX; run `jit_capacity_test`. Expected: the code-buffer row FAIL (every pass ~254k blocks).
- [ ] **Step 2: GREEN.** `MAX_CODE_SIZE = 1024 * 1024 * 512` with the comment; build, install, run. Expected: settles by pass 4, `RESULT: PASS`; then run-all, `run-all exit 0`.
- [ ] **Step 3: Fork commit.** fex-linux (`FEXCore: a 512 MiB code buffer holds a large program's translations`), export `0044-*.patch`.
- [ ] **Step 4: AoE2.** Image build, then `$S/launches3.sh 10 T4`. Expected: 10 menus, median below Task 3's. Any launch without a menu: stop, keep `fail-T4-*`, ledger it, rebuild the images from Task 2's fex-linux commit; 0044 stays out of the repo until understood (0043 stands).
- [ ] **Step 5: Repo commit** (only after Step 4's 10 of 10): the patch, the test and README, `eac-vm: FEX patch 0044, a 512 MiB code buffer; fex-tests jit-capacity code-buffer row`.
