# MacNeutron: no return into a discarded translation in our FEX, then a code buffer that holds AoE2

Status: asked for by the maintainer on 2026-10-23 ("start working on FEX to optimize X86 calls", then "measure, then
pick", then "Yes, then start working on figuring out the real issue", then "Go with Option 1"). Two FEX fork
patches, 0043 (this fix) and 0044 (the code buffer ceiling it unblocks), each with its own test; one plan.

## 1. Goal

A guest `ret` never runs a translation that FEX has discarded. With that fixed, FEX's code buffer can grow past
128 MiB, so Age of Empires II DE (app 813780) stops retranslating its code every few seconds and reaches its menu
sooner, in every launch.

## 2. Findings (2026-10-23)

- FEX keeps a per-thread call-return shadow stack of `{guest return RIP, host return address}` pairs. A call with a
  known return block (`BranchOps.cpp`, the `ExitFunction` call paths) pushes the host address of its return point,
  `l_CallReturn`, the instruction right after the call's branch; a `ret` pops a pair and, if the guest RIP matches,
  branches straight to the host address. All other pushes store zeros, which never match.
- When guest code is rewritten, `InvalidateGuestCodeRange` discards the translations of the range and, per thread,
  `InvalidateThreadCachedCodeRange` empties the thread's shadow stack only if that thread's own lookup cache had an
  entry on the range (`LookupCache::InvalidateCacheRange`, its `CachedCodePages`). A thread records a page there when
  it looks a block up itself. A thread that entered a block only through a link another thread made (and pushed return
  points into it) keeps them: its next `ret` runs the old translation of the rewritten code.
- Reproduced deterministically: `eac-vm/fex-tests/callret-stale` (uncommitted until this fix) returns into the old
  code in 64 of 64 rounds on the committed FEX (fex-linux 7740725, patch 0042). The control, where the parked thread
  has looked the caller up itself, passes.
- Hidden at 128 MiB: AoE2's translations need ~240 MiB, so the shared code buffer fills every 4-8 s and is replaced,
  which empties every shadow stack. With a 512 MiB ceiling AoE2 failed in 7 of 10 launches: the game's fail-fast
  abort (`c0000409`), `RtlReleaseSRWLockExclusive ... is not owned exclusive`, a mutex locked again by its owner
  (`_Throw_Cpp_error(5)` at 0x141091944), hangs. The launches that worked reached the menu in 33-46 s (~49 s now).
- The protector behind it: 2.2M of AoE2's 2.4M compiles in its first 60 s are code it keeps writing at 0x146xxxxxx,
  just past the game image (~496k distinct entries on ~2,700 pages).

## 3. Decisions

- **Patch the return points of a discarded translation** (the maintainer's option 1). When a page a translation
  covers is invalidated, each return point in it is rewritten to branch to a small stub that sends the thread to the
  dispatcher with that return point's guest RIP. Nothing changes on the call/return fast path.
- Rejected: clearing every thread's shadow stack on every invalidation. Measured as a throwaway: AoE2's menu 49 -> 54-58
  s, invalidation time 8.7 -> 17.1 s, shadow-stack flushes 139k -> 12M in 60 s. Rejected: a generation tag in each
  shadow entry (a cost on every call and return, and every invalidation then empties every stack logically).
- Rejected: a host trap (`udf`) at the return point resolved in FEX's SIGILL handler. Smaller code, but another path
  from a host signal into guest state; FEX's signal paths needed 0039-0041 already.
- Fork-only, as all our FEX patches (FEX's CLAUDE.md forbids AI-made contributions upstream).

## 4. Patch 0043: return points of a discarded translation go to the dispatcher

### 4.1 Recording return points (JIT)

- In both `ExitFunction` call paths, when a host return address is pushed, record `{offset of l_CallReturn from the
  translation's start, the return block's guest RIP}`. The guest RIP is the return block's entry RIP (its
  `GuestEntryOffset` from the translation's `Entry`, as `JIT.cpp` computes it for `CodeData.EntryPoints`); record the
  block's IR node at the call and resolve its RIP when the block is bound.
- After the jump thunks, emit one stub per return point, 16 bytes, 8-aligned: `ldr TMP1, <literal>`, `b <common>`,
  the 8-byte guest RIP literal (`PlaceNamedSymbolLiteral(InsertGuestRIPLiteral(RIP))`, as the jump thunks place theirs,
  so a cached translation's literal is relocated). Then one common tail per translation that has return points: `str
  TMP1, [STATE, State.rip]`, `ldr TMP2, [STATE, Pointers.DispatcherLoopTop]`, `br TMP2` (the exit `ExitFunction` emits
  with block debugging). At a return point the guest registers are where a block exit leaves them and TMP1/TMP2 are
  free, so this is an ordinary exit to the dispatcher.
- `JITCodeTail` gains `NumberOfReturnPoints`, `OffsetToReturnPoints` (to an array of u32 return-point offsets from the
  translation's start) and `OffsetToReturnStubs` (stub k is there + 16 k), and a `ReturnPointsPatched` byte taken from
  `_Pad`. A translation without return points emits no stubs and records 0.
  `DiskCache::FormatVersion` goes from 30 to 31, since cached translations carry the tail.

### 4.2 Patching at invalidation

- `GuestToHostMap` gains `TranslationsByPage`: for each guest page, the translations covering it (their
  `JITCodeHeader`), filled by `AddBlockExecutableRange` from the compile and disk-cache paths (the offline code cache
  passes none). `InvalidateRange` patches every translation listed under the invalidated pages and drops those lists;
  `ClearCache` empties it. Patching: if the translation has return points and `ReturnPointsPatched` is not set, write
  `b <stub k>` over each return point's first instruction (a 32-bit atomic store through
  `ARMEmitter::DualMap::WriteAddr`, then `ClearICache` of the 4 bytes, as `DirectBlockDelinker` does) and set the byte.
- Not through the lookup entries (the first version, changed after the final review): `insert_or_assign` lets a later
  translation take over an address, so a discarded translation whose entries were all taken over was never patched,
  and a current one that took over an entry of a discarded one was patched while its return block's entry still led
  to the patched return point (a thread looped between the stub and the dispatcher). All of a translation's entries
  are listed under every page it covers, so after `InvalidateRange` no entry leads into a patched translation.
- Locking: `InvalidateRange` runs with the shared lookup map's write lock and `CodeInvalidationMutex` held, so no thread compiles,
  links or unlinks meanwhile. A thread already past a return point's first instruction finishes that block on the old
  code, as one already inside a discarded translation does today.
- Translations dropped by a code buffer replacement are not patched: each thread empties its own shadow stack when it
  switches buffers, as now.

### 4.3 Tests (fex-tests, the README's rule: passes natively, fails without the patch)

- `callret-stale`: the test above, two rows: A calls G with `call rel32`, and with `call rax` (the indirect call path).
  RED on 7740725 (64/64 stale on the direct row), GREEN with 0043; added to `run-all.sh` and the README.
- `run-all.sh` passes as before (pop-fault's expected FAIL included).
- A stress row in the same test, the same shape under concurrency: functions X_i (one page) call F_i (another page):
  `call L; mov eax, imm32; ret`, where L reads the generation last published for F_i into rdx, runs `cpuid` and
  returns. 4 threads call X_i (so most reach F_i through another thread's link) while a fifth rewrites each F_i's imm32
  to a rising generation and then publishes it. Every return must give imm32 >= the rdx it carries (the code had been
  rewritten to at least that generation before L returned); 8 s, no violation, no crash.

### 4.4 Acceptance

AoE2 at the 128 MiB ceiling with 0043: 10 launches reach the menu (`is_menu.py`, the content-area logo check), median
no more than 2 s slower than 0042's ~49 s; FEX stats show no new cost in invalidation time.

## 5. Patch 0044: the code buffer ceiling at 512 MiB

- `SharedCodeBufferManager.cpp`: `MAX_CODE_SIZE` 512 MiB, the comment saying why (AoE2's ~240 MiB working set; links
  beyond a direct branch's reach already go through the jump thunk's literal, `ExitFunctionLink`, stress-tested with
  every link forced indirect: 0 wrong results in ~25M calls).
- Test: `jit-capacity` gets back its code-buffer row (250,000 generated functions, ~250 MiB of host code, passes until
  one translates < 1%; RED retranslated all of them every pass; GREEN settled by pass 4).
- Acceptance: AoE2 10 launches, all reach the menu, median faster than 0043's.
- Memory: the buffer grows only when the previous one fills, so a program that fits in 128 MiB never maps more.
- Status (2026-10-23): held. On the fixed 0043, 7 of 10 launches reached the menu (some at 32-33 s); 3 hung on a grey
  window: a second 512 MiB bug remains (fex-linux branch exp/0044-on-0043b).

## 6. Shipping

Each patch: commit in fex-linux, export to `eac-vm/fex-patches/004N-*.patch`, the guest fex stage and the image
rebuilt, the builder's binfmt FEX reinstalled, run-all, then the AoE2 launches. Repo commits per patch with the
patch, tests and README rows. Nothing pushed without the maintainer's yes.

## 7. Risks

- A return point whose first instruction other code also branches to while the translation is live: none in the
  current layout (internal branches target the block label bound after `EmitEntryPoint`); the plan asserts the layout
  in a debug build.
- Code size: 16 bytes of stub and 4 bytes of table per call with a known return block (+5-10% translated code at
  AoE2's call density); measured in the plan.
- The 512 MiB failures having a second cause: 0044's acceptance (10 of 10) decides; a failure stops 0044, not 0043.
