# DXMTRS: the C, C++ and Objective-C that remain, to Rust

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every source file of DXMTRS that the maintainer agrees to port is Rust, with its gates green and its
behaviour unchanged; what stays in C, C++ or Objective-C (if anything) is listed with its reason. The maintainer's
words (2026-11-16): port the remaining C/C++/Objective-C files "like the rest of DXMTRS". Wine stays C; MacNeutron's
own test programs (`dxmt/tests/`) and host tools (`dxmt/tools/`) are not DXMTRS and are not part of this.

**Architecture:** the 21 files fall in five groups, each ported differently: files that are already dead (deleted),
runtime shims and thunks that Rust expresses directly (ported in place), a second implementation of a C handle
protocol MacNeutron's host keeps using (ported, header kept), glue over LLVM's C++ API (three ways out, the
maintainer's call), and one Objective-C `@try` frame (structural under `panic = "abort"`, the maintainer's call).
One DXMTRS commit a file or pair, each behind `tools/rust-check.sh`; one MacNeutron pin move at the end.

**Tech Stack:** Rust 1.99.0, meson, llvm-mingw, LLVM 23 (fork `Mac-Neutron/llvm-project`, branch `air-23`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5, §7. Ledger:
`~/Documents/MacProton/.superpowers/sdd/2026-11-16-dxmtrs-c-to-rust/progress.md` (the main checkout's ignored
folder). Worktrees: `build/crs/dx` (DXMTRS, branch `c-rust`), `build/crs/mn` (MacNeutron, branch `c-rust-mn`);
scratch and logs in `build/crs/s`.

## Inventory (DXMTRS `5f9e5caa`, `git ls-files`, outside `external/` and `vendor/`)

Counted again on the branch's own base: the same 21 files as at `82a588ba`.

### 1. Translator: glue over LLVM's C++ API (3 files, 507 lines)

`src/airconv/shim/shim.cpp` (331), `src/airconv/transforms/lower_16bit_texread.cpp` (98),
`src/airconv/transforms/simdgroup_implicit_membarrier.cpp` (78), with their two `.hpp`.

- **What:** 20 `extern "C"` functions `airconv_shim_*` that `src/airconv/src/llvm/sys.rs:1153-` declares, and two
  LLVM function passes that run inside the shim's hand-built pass pipeline.
- **Who builds it:** `src/airconv/build.rs` (cc-rs, C++17, against `AIRCONV_LLVM`'s headers) into the `airconv`
  crate's archive, which `winemetal`'s static library carries into `winemetal.so` (Mac) and `winemetal.dylib`
  (native). Meson compiles none of it.
- **Why it is not Rust:** Rust reaches LLVM through hand-declared bindings of LLVM's **C API**
  (`src/airconv/src/llvm/sys.rs`); the shim is exactly what that API lacks. Per function, against the fork's
  installed headers (`build/wine-arm64-src/llvm23-arm64-nolto/include/llvm-c`, searched recursively):

  | Shim function | LLVM 23 C API | Bucket |
  | --- | --- | --- |
  | `get_fast_math_flags`, `copy_fast_math_flags`, `is_fp_math_operator` | `LLVMGetFastMathFlags`, `LLVMSetFastMathFlags`, `LLVMCanValueUseFastMathFlags` (`Core.h`; same seven bits) | A: the C API has it |
  | `memory_attribute`, `captures_none` | `LLVMCreateEnumAttribute` with the kind of `memory` / `captures` and the integer LLVM encodes | A (Rust computes the integer; pinned against the shim before it goes) |
  | `set_sdk_version` | `LLVMAddModuleFlag` + `LLVMConstDataArray` + `LLVMValueAsMetadata` | A |
  | `declare_simdgroup_barrier` | `LLVMGetNamedFunction`, `LLVMAddFunction`, `LLVMAddAttributeAtIndex` | A |
  | `run_passes`, `pipeline_text` (the standard passes) | `LLVMRunPasses` (`Transforms/PassBuilder.h`) takes the pipeline as text | A for the standard passes only; the two own passes have no name LLVM's parser knows |
  | `set_fast_math_flags` (builder) | none: the C builder has no fast-math setter (`LLVMBuilderSetDefaultFPMathTag` is the metadata tag) | B: rebuildable (set the flags on each instruction the wrapper creates), output at risk |
  | `build_atomic_rmw_value` | none; the arithmetic is `LLVMBuild*` calls | B |
  | `local_slot` | none (`ModuleSlotTracker`); the numbering can be recomputed | B |
  | `parse_bitcode` (DXIL's `i8:32` data-layout fix) | `LLVMParseBitcodeInContext2` has no data-layout callback | C: needs C++ |
  | `link_only_needed` | `LLVMLinkModules2` has no flags | C |
  | `erase_named_metadata` | `LLVMGetNamedMetadata` only, no erase | C |
  | `destroy_constant` | none | C |
  | `get_as_instruction` | none; `LLVMBuild*` would fold the constant operands back | C |
  | `split_block`, `split_block_and_insert_if_then` | none (`LLVMSplitBasicBlock` does not exist) | C |
  | `rename_struct` | no enumeration of identified struct types, no struct rename | C |
  | `Lower16BitTexReadPass` | a pass class; the body is C-API-expressible, its place in the pipeline is not | C as placed; B if run after the pipeline |
  | `SimdgroupImplicitMemBarrierPass` | `MemorySSA` has no C API at all | C |
  | `probe` | (a link probe; goes with the shim) | - |

- **Can it be Rust with what the tree uses:** bucket A, yes, today. Bucket C, no: there is no C entry point, and
  the tree's rule is hand-declared C bindings. The house answer for "LLVM lacks a C entry point" already exists:
  `LLVMWriteAIRBitcode` and `LLVMAIRTypedPointerType` live in **our LLVM fork** (`llvm-c/AIRWriter.h`,
  component `LLVMAIRBitWriter`), in C++, where LLVM's own C++ lives.
- **Three ways out (the maintainer's decision, Q1):**
  - **T-fork (recommended):** bucket A becomes Rust on the existing C API; buckets B and C move, bodies unchanged,
    into the fork as C entry points beside `AIRWriter.h` (`llvm-c/AIR.h`: `LLVMAIRParseBitcode`,
    `LLVMAIRLinkOnlyNeeded`, ... and the two passes registered under names so `LLVMRunPasses`' text can place them
    exactly where they run today). DXMTRS then holds no C++ and airconv's `build.rs` loses cc-rs. Cost: about 2
    agent-days; `LLVM_AIR_COMMIT` moves, both LLVM installs rebuild (ThinLTO and plain), a push to
    `Mac-Neutron/llvm-project` `air-23`, and the translator key moves. Risk: low for output (same C++ bodies, same
    pipeline order, proved by the 15 `air-oracle` corpora byte for byte and by the pipeline-text test), medium for
    the build (a third LLVM component in every link list).
  - **T-keep:** the shim and passes stay as DXMTRS's one documented C++ exception. Cost: none. The goal "all of
    DXMTRS in Rust" is then missed by 507 lines, with the reason written in `docs/CONVENTIONS.md`.
  - **T-max:** as T-fork, and bucket B plus `Lower16BitTexReadPass` are rewritten in Rust (the pass run after
    the pipeline, which is equivalent because it and the barrier pass are the last two function passes). Cost:
    about 4 agent-days. Risk: medium for output (the builder's fast-math emulation and the slot numbering are
    re-derivations, not moves); the corpora are the only proof. `MemorySSA` still needs the fork.
- **A fact that binds every option but T-keep:** the translator key is a hash of source text (see Global
  Constraints), so it moves on any edit under `src/airconv`, identical output or not.

### 2. Windows-side glue (5 files, 497 lines)

- `src/util/wsi_monitor_win32.cpp` (125), `src/util/wsi_window_win32.cpp` (282), with `wsi_monitor.hpp`,
  `wsi_window.hpp`, `wsi_platform_win32.hpp`. **Dead.** No `meson.build`, `build.rs` or script names them (only
  `tools/audit.sh`'s allow-list and two doc comments). Their 21 functions are `src/util/src/wsi/win32.rs` and
  `wsi.rs` already (`enumMonitors` is `enum_monitors`, ... one for one). **Port: delete.** `src/util/dxvk.LICENSE`
  stays: the Rust is still derived from DXVK. Cost: minutes. Risk: none.
- `src/util/rust_pe_rt.c` (18). **What:** `_fltused` (x86, x86-64), `__chkstk` as a jump to mingw's probe (x86-64:
  `___chkstk_ms`; i686: `___chkstk`), `__tls_array = 0x2c` (i686): what Rust's `-msvc` code references and
  llvm-mingw's CRT does not define. Empty on ARM64, ARM64EC and macOS. **Who links it:** `meson.build:241`
  (`rust_runtime_dep`'s `sources`, so every Rust PE image: nvngx, nvapi64, winemetal.dll, dxgi, d3d10core, d3d11,
  d3d12, the guest's d3d9 and mtld3d, dxmt-replay), `src/winemetal/unix/meson.build:42` (as `winemetal.so`'s one
  source, where it compiles to nothing), `tools/e2e-link.sh:26`. **Why not Rust yet:** it predates
  `dxmt_util::dxmt_dll!` and `src/d3d9/src/arm64_crt.rs`, which now supply the same kind of symbol from Rust.
  **Can it be Rust:** yes: a static with an export name and two `global_asm!` blocks. **The real cost is meson:**
  the file is the only *source* of several targets, and a target with no source is deprecated in meson (Task 0).
  Cost: half a day. Risk: low for the symbols (the link fails if one is missing; the probe's body is one jump),
  medium for the build plumbing.
- `src/nativemetal/win32_event.c` (26), `src/nativemetal/win32_native.cpp` (46). **What:** `winemetal.dylib`'s 13
  exported `wmt_native_*` calls and `MTLD3DNotImplemented`, each one line over the `static inline` functions of
  `src/util/win32_native.h` and `win32_native_event.h`. **Who links it:** `src/nativemetal/meson.build` (the
  native build only, `vm/dxmt-native/build.sh`). **Callers:** the Rust front ends through `winemetal-sys`
  (`src/winemetal/sys/src/lib.rs:28-51`), `src/winemetal/unix/src/g7.rs:74-80`, MacNeutron's host by `dlsym`
  (`vm/d3d/host/dispatch.c:166`). **Why not Rust yet:** the implementation is the header, and the header is a
  protocol MacNeutron's native host and guest glue compile themselves (`vm/d3d/host/events.c`,
  `vm/d3d/guest/unix.c`, `proxy.cpp`, `wm_probe/cases_g7.m`, `vm/dxmt-native/tests`). **Can it be Rust:** yes,
  as a **second implementation** of seven calls on the same `struct mn_win32_handle` (the `libc` crate's pthread
  types; `winemetal`'s unix crate already depends on it). The two headers stay: MacNeutron compiles them. Cost:
  one day. Risk: medium (layout or semantics drift between the header and the Rust; pinned by cross tests,
  Task 4).

### 3. winemetal (1 file, 169 lines)

`src/winemetal/airconv_thunks.c`. **What:** `winemetal.dll`'s twelve `SM50*` exports: each fills a params struct
and makes a Wine unix call. **Who links it:** `src/winemetal/meson.build:5` (every PE build, the guest's too).
**Callers:** d3d11, d3d12 and d3d9's core runtime through the import library. **Why not Rust yet:** step 2 ported
the 158 winemetal thunks and left the translator's for step 4, which ported the unix side only. **Can it be
Rust:** yes, in `winemetal-pe` beside the 158 thunks; `winemetal-wire` already mirrors every params struct and
`layouts.rs` asserts them per target. Cost: half a day. Risk: low (twelve thunks of a shape the crate has 158 of;
every D3D10-12 draw of every gate goes through them). **Binding fact:** MacNeutron's `translator_key` hashes this
file and dies without it, so `wine-arm64/lib.sh` learns the new shape in the same series and the key moves.

### 4. D3D9's macOS side (1 file, 32 lines)

`src/d3d9/unix/src/metal/macdrv/delegate_forward.m`. **What:** sends
`applicationDidChangeScreenParameters:` to Wine's application delegate inside `@try`/`@catch`, and asks
`respondsToSelector:`. **Who builds it:** `src/d3d9/unix/build.rs:77` (cc-rs) into `mtld3d.so`. **Why it is not
Rust:** `mtld3d.so` is built with `panic = "abort"` (root `Cargo.toml`), under which a foreign exception that
unwinds into a Rust frame ends the process; the catch must sit in a frame with no Rust between it and the send.
**Can it be Rust:**
  - objc2's `exception::catch`: no. It needs `panic = "unwind"` for the whole of `mtld3d.so`, and its helper
    crate (`objc2-exception-helper`, not in the registry: a download) compiles an Objective-C file itself, so the
    Objective-C only moves into a dependency.
  - a hand-written landing pad in `global_asm!` (personality `__objc_personality_v0`, a hand-encoded LSDA): it
    removes the `.m` and puts about 60 lines of unwind tables nobody can review in its place.
  - `respondsToSelector:` alone is Rust today (`objc2`); it sits in the `.m` only because the audit bans raw
    selectors in Rust and the typed binding was not enabled.
- **Recommendation (Q2):** leave it. It is 32 lines that retire with `mtld3d.so` at the end of C4 (spec §5: D3D9
  on the shared core). Cost of leaving: none.

### 5. Upstream sample programs (11 files, 5,366 lines)

`tests/dx11/*.cpp` with `3DMaths.h`, `ObjLoading.h`, `stb_image.h` (7,525 lines, third-party). **What:** DXMT
upstream's D3D11 sample windows. **Who builds them:** `tests/dx11/meson.build` builds five of the eleven, only
under `enable_tests`, which no MacNeutron script sets; `dx11_model.cpp` is empty. No gate runs any. MacNeutron's
shader corpus reads the folder's `.hlsl` files (`dxmt/tests/dxbc-corpus/sources.txt`), nothing else. **Can they
be Rust:** yes, as five or eleven Windows programs (an application-side D3D11 binding does not exist in the
tree; about 5 agent-days for programs no gate would run). **Recommendation (Q3):** delete the `.cpp`, the three
headers, `cube.obj`, `test.png` and the meson files (and the `enable_tests` option with them); keep the `.hlsl`.
MacNeutron's own D3D11 rows cover what the samples drew.

### Headers

| Header | Consumer today | After this plan |
| --- | --- | --- |
| `include/native/**` | meson's native include path; `tools/winemetal-layouts.sh` (record layouts); MacNeutron (`vm/dxmt-native/build.sh:71` copies it; `release/source-archive.sh`) | stays |
| `src/winemetal/winemetal.h`, `winemetal_thunks.h`, `airconv_thunks.h`, `src/airconv/airconv_public.h`, `dxil/dxil_public.h` | `tools/winemetal-layouts.sh` (layouts, protos, decls, `--airconv`), the `gen` leg of rust-check | stay: the generated bindings' source of truth |
| `src/dxmt/dxmt_format.hpp`, `dxmt_presenter.hpp`, `dxmt_scaler.hpp`, `dxmt_shader_cache.hpp`, `src/winemetal/sys/sm50-abi.h` | `--seam-abi` (each seam's `abi.rs` names its header), rust-check | stay |
| `src/dxmt/dxmt_command_constants.hpp` | `#include`d by `dxmt_command.metal` | stays |
| `src/d3d11/d3d11_interfaces.hpp`, `src/d3d12/d3d12_interfaces.hpp`, `src/dxgi/dxgi_interfaces.h`, `src/util/com/com_guid.hpp`, `src/util/util_d3dkmt.h` | `include_str!` in `src/util/src/ext/tests.rs` (IIDs), and what those include | stay |
| `src/util/win32_native.h`, `win32_native_event.h` | MacNeutron's native host, guest glue and tests | stay |
| `src/airconv/dxbc_root_signature.hpp`, `src/d3d12/d3d12_barrier_legacy.hpp`, `d3d12_shared_fence.hpp` | named in Rust doc comments only | **Q4:** delete and reword the comments, or keep as reference; default: keep (not C sources, out of the stated count) |
| `src/util/wsi_monitor.hpp`, `wsi_window.hpp`, `wsi_platform_win32.hpp` | the dead `.cpp` and two doc comments | deleted with Task 1 |
| `src/winemetal/wineunixlib.h` | `airconv_thunks.c` only | deleted with Task 3 |
| `src/nativemetal/wineunixlib.h` | nothing (its user `winemetal_thunks.c` is gone) | deleted with Task 3 |
| `src/airconv/transforms/*.hpp` | the shim | go with Q1's answer |

## Global Constraints

- **Behaviour unchanged.** No export, import, wire field, config key or log line changes. Per image the export
  table is compared before and after (`llvm-readobj --coff-exports`, `nm -gU` for Mach-O), and the plain
  `d3d9.dll`'s imports stay as they are.
- **The translator key is a hash of source text, not of output.** `wine-arm64/lib.sh`'s `translator_key` hashes
  every file under `src/airconv` (tests aside), `src/winemetal/airconv_thunks.[ch]` and `tools/llvm.sh`. So the
  key (`3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` today) moves when Task 3 lands and when
  any translator file is touched, with identical output. The task's "moves only if output changes" cannot hold as
  written (Ruling 1). Unchanged output is proved by the `air-oracle` corpora byte for byte, never by the key. All
  key-moving commits land under **one** pin move, so shader caches turn over once. The key's new value and the
  reason are called out in the pin commit, the spec and the final report.
- `tools/rust-check.sh` passes after every DXMTRS commit and the commit is chained on its exit code
  (`sh tools/rust-check.sh > log 2>&1 && git commit ...`). Commit bodies follow CONTRIBUTING §Changes.
- No new dependency (Rulings 6, 9), no new lint suppression, no `pub(crate)`, no new static holding mutable state
  beyond the one thread-local this plan names. No em dashes. `~` for the home folder in anything committed.
- Edits only in `build/crs/dx` and `build/crs/mn`; the other session's `build/c4b3c/*` worktrees and branches are
  not touched. Builds, installs, gates and any checkout in `build/wine-arm64-src/dxmt` run under
  `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`, one job each, the checkout restored by the job's
  trap. No `make` in a worktree. A job tests committed tips; a job script that has run is never edited.
- No push before the other session's slice C4b-3d5 is on both mains; then fetch, rebase, re-run rust-check and
  the count, privacy scan, push.
- Per task one lane; full gates once (Task 8). No gate while the maintainer is gaming; the Mac idle while
  `dxmt/check.sh` runs. Stop at 95% weekly usage.
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/crs/cargo`.

## Order

Task 0 (spike) → 1 (wsi, delete) → 2 (`rust_pe_rt.c`) → 3 (`airconv_thunks.c`) → 4 (native Win32) → **maintainer's
answers** → 5 (translator, per Q1) → 6 (`delegate_forward.m`, per Q2) → 7 (`tests/dx11`, per Q3) → 8 (full gates) →
9 (docs, spec, pin, push). Tasks 1-4 do not depend on any answer and start once the reviews are folded. Tasks 3
and 5 move the key; they share Task 9's one pin.

### Task 0: a PE and a Mach-O image with no C source link as today (spike, no commit)

**Files:** scratch only (`build/crs/s/spike`).

Meson warns that a target without sources "was never supposed to be allowed" and will stop working. After Tasks
2-4, `nvngx`, `nvapi64`, `d3d12`, `winemetal.dll`, the guest's `mtld3d`, `dxmt-replay`, the Mac's `winemetal.so`
and the native `winemetal.dylib` have none.

- [ ] **Step 1:** in a scratch meson project with the tree's meson version and cross file (`build-win64.txt`),
  build a DLL from a custom-target archive three ways: `link_with` and no source (expect the warning),
  `objects : <the custom target>`, `link_whole`. Record for each: configure output, whether the archive stays an
  archive on the link line (lazy member extraction) and the link line's order.
- [ ] **Step 2:** repeat natively (`native : true`, the `winemetal.so` shape: the archive's path in `link_args`,
  `-u` symbols).
- [ ] **Step 3:** rule on the form (Ruling 3) and ledger it. Expected: `objects :` (no warning, same link
  semantics, same order). If no form is clean, the fallback is one generated empty object from a custom target
  (still no C in the tree), ledgered.

### Task 1: the dead window-system C++ goes

**Files:** delete `src/util/wsi_monitor_win32.cpp`, `wsi_window_win32.cpp`, `wsi_monitor.hpp`, `wsi_window.hpp`,
`wsi_platform_win32.hpp`; modify `tools/audit.sh` (the two entries of `CPP_SOURCES`), `src/util/src/wsi.rs:47,57`
and `src/util/src/wsi/win32.rs:1` (the comments state the contract without naming deleted files).

- [ ] **Step 1 (test first):** prove nothing reads them:
  `git grep -n -E 'wsi_(monitor|window|platform)' -- . ':!src/util/wsi_*'` lists only `tools/audit.sh` and the
  three doc comments; `/usr/bin/grep -rn wsi_ ~/Documents/MacProton/{Makefile,dxmt,vm,wine-arm64,release}` is
  empty. Map the 21 C++ functions to their Rust names in the ledger (one line each).
- [ ] **Step 2:** delete, reword, `sh tools/rust-check.sh > log 2>&1 && git commit`
  (`util: the window-system C++ that wsi/win32.rs replaced leaves the tree`).

### Task 2: `rust_pe_rt.c` becomes Rust

**Files:** create `src/util/src/pe_rt.rs` (declared in `src/util/src/lib.rs` under
`cfg(all(windows, target_env = "msvc", any(target_arch = "x86", target_arch = "x86_64")))`); modify `meson.build`
(`rust_runtime_dep` loses its `sources`), `src/winemetal/unix/meson.build:42`, each meson target Task 0 named,
`tools/e2e-link.sh` (no C compile; the symbols come from the test crate's `dxmt-util`), `Cargo.toml` of the three
static-library crates that do not depend on `dxmt-util` (`src/d3d9/shim`, `src/replay`, `src/winemetal`: a
workspace path dependency and `use dxmt_util as _;`, Ruling 6), `README.md:43`, `src/d3d9/src/arm64_crt.rs:6`
(its comment); delete `src/util/rust_pe_rt.c`.

```rust
//! What Rust's `-msvc` code needs from MSVC's runtime that llvm-mingw's lacks, on x86.

/// MSVC's CRT defines `_fltused` for code that uses floating point; only its presence matters.
#[unsafe(export_name = "_fltused")]
static FLTUSED: i32 = 0;

// The stack probe. MSVC's `__chkstk` and mingw's `___chkstk_ms` both touch the pages below `rsp` and leave it
// alone, so a jump keeps the caller's frame as `__chkstk` would.
#[cfg(target_arch = "x86_64")]
core::arch::global_asm!(".globl __chkstk", "__chkstk:", "jmp ___chkstk_ms");

// i686: MSVC's `_chkstk` and compiler-rt's `___chkstk` both probe and lower `esp` by `eax`. The `-msvc` target
// reads the TEB's TLS array at the absolute `__tls_array`, the TEB's `ThreadLocalStoragePointer`.
#[cfg(target_arch = "x86")]
core::arch::global_asm!(
    ".globl __chkstk", "__chkstk:", "jmp ___chkstk",
    ".globl __tls_array", ".set __tls_array, 0x2c",
);
```

- [ ] **Step 1 (test first, the reference):** under the lock, from the base commit, record for every PE image of
  the Mac build and of the guest build (x86_64 and i686): `llvm-nm` lines of `_fltused`, `__chkstk`,
  `__tls_array`; the disassembly of `__chkstk`; `llvm-readobj --coff-exports --coff-imports`; the section table.
  Saved as `build/crs/s/ref-pe/<image>.txt`. (One job; the builds are the gates' own.)
- [ ] **Step 2:** write `pe_rt.rs`, the meson changes in Task 0's form, the three `Cargo.toml` lines; host
  `cargo test -p dxmt-util`; rust-check; commit (`util: the MSVC runtime shims of the x86 PE images are Rust`).
- [ ] **Step 3 (lane):** under the lock, build the branch tip (Mac + guest x86_64 + guest i686); the same
  records; expect: every symbol present exactly once per image with the same type and the same `__chkstk` body,
  exports and imports identical, no meson warning about sources. Then the guest's 32-bit D3D9 smoke row
  (`vm/dxmt/d3d9-gate.sh`'s shortest lane), since i686 is where `__tls_array` and `__chkstk` are live.
- [ ] **Step 4:** if `static FLTUSED` is dropped by LTO in any image (the record shows it missing), mark it
  `#[used]`; if a crate cannot take `dxmt-util` (a `no_std` shim), fall back to a three-line crate `dxmt-pe-rt`
  and ledger it.

### Task 3: `airconv_thunks.c` becomes Rust

**Files:** create `src/winemetal/src/sm50.rs`; modify `src/winemetal/src/lib.rs` (the module, the comment at
line 9), `src/winemetal/meson.build` (no C source, Task 0's form; the comment), `tools/audit.sh` if it lists the
file; delete `src/winemetal/airconv_thunks.c`, `src/winemetal/wineunixlib.h`, `src/nativemetal/wineunixlib.h`.
MacNeutron: `wine-arm64/lib.sh` (`translator_key` takes `src/winemetal/src/sm50.rs` where `airconv_thunks.c` is
absent, as it does for `d3d11_shader.cpp` / `args.rs`; the header is still hashed), `wine-arm64/tests/
translator_key_test.sh` (both shapes), the comment block above `translator_key`.

Each export has the C's shape: fill the `winemetal-wire` mirror, call the crate's existing unix-call helper with
the slot from `airconv_unixcalls` (74-88; `slot.rs` already numbers them), answer `-1` on a non-zero status
where the C does, `ret` or `ret_size` otherwise. `SM50GetErrorMessage` initialises `ret_size` to 0 before the
call, as the C does.

- [ ] **Step 1 (test first):** `translator_key_test.sh` gains the cases "a tree with `sm50.rs` and no
  `airconv_thunks.c` has a key", "editing `sm50.rs` changes it", "a tree with neither dies"; they fail; then
  `lib.sh`; they pass (`make` target `translator-key-test` or the script directly; host only, no lock).
- [ ] **Step 2 (test first, DXMTRS):** a host unit test per thunk shape in `src/winemetal/src/sm50/tests.rs`
  against the crate's existing fake of the unix call (the 158 thunks' tests show the pattern): the params struct
  the unix side receives, the slot number, the value answered for status 0 and for a failing status. Red: the
  module does not exist.
- [ ] **Step 3:** write `sm50.rs`; `tools/winemetal-layouts.sh --protos` must still print the committed
  `protos.rs` (the `gen` leg): if `SM50*` are `AIRCONV_API` and not `WINEMETAL_API`, add the twelve coercion
  asserts by hand in `sm50.rs` (`const _: unsafe extern "C" fn(..) -> .. = SM50Initialize;`) with the C
  prototypes of `airconv_public.h` as their source. rust-check; commit
  (`winemetal: the SM50 thunks of winemetal.dll are Rust`).
- [ ] **Step 4 (lane):** under the lock, Mac build of the tip: `winemetal.dll`'s export table equals the
  reference's; `dxmt/check.sh`'s shortest D3D11 lane (every draw compiles through `SM50Initialize` and
  `SM50Compile`).

### Task 4: the native build's Win32 calls become Rust

**Files:** create `src/winemetal/unix/src/native_win32.rs` (under the crate's `native` feature) and its
`tests.rs`; modify `src/winemetal/unix/src/lib.rs`, `src/winemetal/unix/src/g7.rs:74-80` (calls the module, no
`extern` block), `src/nativemetal/meson.build` (no source, Task 0's form), `tools/winemetal-layouts.sh` (the
`--decls` native block reads its prototypes from a `dxmt-abi` block moved into `src/util/win32_native_event.h`,
so `winemetal-sys`'s declarations stay pinned to a C header as every seam's are), `tools/audit.sh`; delete
`src/nativemetal/win32_event.c`, `src/nativemetal/win32_native.cpp`. MacNeutron: `vm/dxmt-native/tests/
win32-test.cpp` (cross cases).

The module mirrors `struct mn_win32_handle` (`#[repr(C)]`, `libc::pthread_mutex_t`, `libc::pthread_cond_t`) with
`const` asserts of its size and each field's offset (the numbers from Apple clang's `-fdump-record-layouts` of
the header, recorded in the ledger), and implements, with the header's exact semantics: `wmt_native_set_event`,
`_ref`, `_unref`, `_handle_count`, `_create_event`, `_create_semaphore`, `_release_semaphore`,
`_wait_for_single_object`, `_duplicate_handle`, `_open_thread`, `_close_handle`, `_not_implemented`,
`MTLD3DNotImplemented` (one `thread_local!` `Cell<bool>`, the C's `_Thread_local`, argued in its doc block).
Objects are allocated with `libc::calloc` and freed with `libc::free`: the header's `mn_win32_unref` frees what
this module creates and the reverse.

- [ ] **Step 1 (test first, MacNeutron):** `win32-test.cpp` gains cross cases against the **exported** symbols
  of `winemetal.dylib` (declared `extern "C"`, linked): an event the dylib created is set, waited on and closed
  by the header's inline functions and the reverse; a semaphore's count and `ERROR_TOO_MANY_POSTS` path through
  both; a duplicate made by one side closed by the other; a relay event set through `wmt_native_set_event`;
  `wmt_native_handle_count` against the header's `refs`; a timed wait that times out; the pseudo-handles -1
  and -2 refused by every call. Run under the test's existing `-fsanitize=address,undefined` against the **C**
  dylib first: green is the reference.
- [ ] **Step 2 (test first, DXMTRS):** host unit tests of the module for the same cases (the module is plain
  pthread code; it runs on the Mac host).
- [ ] **Step 3:** write the module from the header, function by function, the header's lines cited in the
  ledger map; rust-check (`--decls` regenerates equal); commit
  (`winemetal: the native build's Win32 calls are Rust`).
- [ ] **Step 4 (lane):** under the lock, `vm/dxmt-native/build.sh` at the tip; `nm -gU winemetal.dylib` equals
  the reference's; `vm/dxmt-native/tests/win32.sh`, `hook.sh`, `wmt-exports.sh` green.
- **Stated difference (Ruling 5):** the C++ calls set the header's thread-local last-error on failure; the Rust
  calls do not. No caller reads it: the callers are Rust front ends that have no `GetLastError` for it.

### Task 5: the translator, per Q1

Written here for **T-fork** (the recommendation); T-keep makes this task a paragraph in `docs/CONVENTIONS.md`,
T-max adds Steps 6-7.

**Files (llvm-project, worktree `build/crs/llvm` of `build/forks/llvm-project`, branch from `air-23`):**
`llvm/include/llvm-c/AIR.h`, `llvm/lib/Transforms/AIR/` (the two passes, `PassRegistry.def` entries
`air-lower-16bit-texread`, `air-simdgroup-membarrier`), the C entry points beside the AIR writer's.
**Files (DXMTRS):** `src/airconv/src/llvm/sys.rs` (the `airconv_shim_*` block becomes `LLVMAIR*` and C API
declarations), their callers, `src/airconv/build.rs` (no cc-rs), `src/airconv/Cargo.toml`, `src/airconv/
meson.build` (`llvm_deps`), `tools/llvm.sh` (`LLVM_AIR_COMMIT`), `tools/audit.sh`; delete `src/airconv/shim/`,
`src/airconv/transforms/`.

- [ ] **Step 1 (test first, the reference):** under the lock, run the 15 `air-oracle` corpora at the base and
  keep every output (`build/crs/s/ref-air`); keep `pipeline_text(0)` and `pipeline_text(1)`.
- [ ] **Step 2 (bucket A, DXMTRS only, LLVM unchanged):** one commit a function group, each with a unit test
  written while the shim still exists that asserts the Rust result equals the shim's (the attribute's printed
  form, the module flag's printed IR, the fast-math bits on an instruction), then the shim function goes. Key
  moves; output must not: corpora equal after the last.
- [ ] **Step 3 (fork):** move buckets B and C and the passes, bodies unchanged; `LLVMRunPasses` with the text
  that `pipeline_text(1)` prints must build the same pipeline: a fork-side test prints it back and compares.
  Check that `LLVMRunPasses` adds nothing the hand-built `PassBuilder` lacks (it installs
  `StandardInstrumentations` and, with `VerifyEach`, verifier passes: options left at their defaults, the printed
  pipeline compared).
- [ ] **Step 4:** rebuild both LLVM installs (the lock; `tools/llvm.sh`'s recipe), DXMTRS on the new entry
  points, rust-check, commit; corpora byte-identical to Step 1; `winemetal.so`'s link list gains the component
  in `llvm_deps`' order (`llvm-config --libs`).
- [ ] **Step 5:** push the fork branch only with Task 9 (the maintainer's yes for Q1 covers it).
- T-max only: [ ] **Step 6** `Lower16BitTexReadPass` in Rust after the pipeline; [ ] **Step 7** bucket B in
  Rust; corpora after each.

### Task 6: `delegate_forward.m`, per Q2

Default (leave): one sentence in `docs/CONVENTIONS.md`'s list of accepted non-Rust sources and `tools/audit.sh`'s
allow-list entry; no code change. Moving `respondsToSelector:` to Rust alone is not done (it would leave the
file in place and add a selector to the Rust).

### Task 7: `tests/dx11`, per Q3

Default (delete): remove the eleven `.cpp`, the three headers, `cube.obj`, `test.png`, `tests/dx11/meson.build`,
`tests/meson.build`, the `enable_tests` option and `meson.build`'s `subdir('tests')` block (D3D9's `tests/d3d9`
is cargo's, not meson's); keep the `.hlsl`. Test first: MacNeutron's corpus build (`dxmt/tests/dxbc-corpus`)
reads every path of `sources.txt` from the branch tip. One commit (`tests: upstream's D3D11 sample programs
leave; their shaders stay for the corpus`).

### Task 8: full gates (once, under the lock, committed tips)

- [ ] `tools/rust-check.sh`; `wine-arm64/tests/translator_key_test.sh`.
- [ ] Mac: dev build of the tip; the x64 `dxmt/check.sh` (all lanes, Mac idle); `dxmt/d3d9-check.sh` default
  lane; the plain `d3d9.dll` import check; `dxmt/wm-probe-check.sh`.
- [ ] VM: `vm/dxmt/gate.sh`, `vm/dxmt/d3d9-gate.sh` (the guest loads `winemetal.dll`, every Rust PE image and,
  through the host, `winemetal.dylib`).
- [ ] Native: `vm/dxmt-native/build.sh` and its `tests/`.
- [ ] If Task 5 ran: the 15 corpora byte-identical; the new key recorded.
- [ ] `git ls-files | /usr/bin/grep -E '\.(c|cc|cpp|cxx|m|mm)$'` outside `external/` and `vendor/` lists exactly
  what the maintainer chose to keep.

### Task 9: review, docs, spec, pin, push

- [ ] One Opus review of the whole diff (correctness; the meson forms; the handle protocol).
- [ ] DXMTRS docs: `README.md` (licence paragraph of `rust_pe_rt.c`), `docs/DEVELOPMENT.md` (what cc-rs still
  compiles), `docs/CONVENTIONS.md` and `tools/audit.sh`: the C++ allow-list becomes "no C, C++ or Objective-C
  source under `src/` except: <the kept list, each with its reason>", extended to `.c` and `.m`.
- [ ] MacNeutron: spec §5 (a paragraph: what was ported, what stays and why, the key's move), `dxmt/pins`,
  licences text if it names a removed file.
- [ ] Wait for C4b-3d5 on both mains; fetch; rebase both branches; rust-check and the count again; privacy
  scan; push DXMTRS main (and `air-23` if Task 5 ran), verify with `git ls-remote`, pin, push MacNeutron main.

## Rules check

- **New files:** `src/util/src/pe_rt.rs`, `src/winemetal/src/sm50.rs` (+ `sm50/tests.rs`),
  `src/winemetal/unix/src/native_win32.rs` (+ tests). No `mod.rs`. No new crate (Ruling 6).
- **Statics:** `FLTUSED` (immutable, a symbol the linker needs; its doc block says so). One `thread_local!`
  `Cell<bool>` in `native_win32.rs`: mutable per-thread state that replaces the C's `_Thread_local bool` of the
  same meaning; CONVENTIONS "state lives on an object" allows it with the argument written in its doc block
  (the flag is read by `dlsym` from a C host with no object to hold it).
- **Duplicate:** `native_win32.rs` is a second implementation of seven calls of `win32_native.h`, which stays
  for MacNeutron's C. Allowed because the header cannot leave (its users are not DXMTRS) and the C file cannot
  stay (the goal); pinned by Task 4's cross cases and the layout asserts.
- **Dependencies:** none added. `dxmt-util` becomes a dependency of three more workspace crates (a path crate).
  T-fork removes `cc` from airconv's build dependencies.
- **Wire fields, config keys, exports, log lines:** none added, removed or changed.
- **Lint suppressions:** none. `export_name = "_fltused"` on an upper-case static avoids
  `non_upper_case_globals`.
- **Raw selectors, `pub(crate)`, type aliases, glob imports:** none.
- **Derives:** `MnWin32Handle` derives nothing.
- **Generated files:** `decls.rs` regenerates equal after Task 4 (the block moved, its text did not);
  `protos.rs` and `layouts.rs` unchanged.
- **Audit:** `CPP_SOURCES` shrinks with each task and ends as the kept list; the rule grows to `.c` and `.m`.

## Rulings (planning)

1. **The key moves and that is called out, not avoided.** The brief asks that the key move only if output
   changes; `translator_key` hashes source text. Changing the key's composition so that a port keeps it would
   mean hashing outputs, a larger change with its own risk. Cost if wrong: one turnover of every user's shader
   cache at the next release (seconds to minutes of recompilation per game, once), as steps 4a and 4b cost.
2. **One pin for every key-moving commit.** Cost if wrong: none; a failed late task delays the earlier ones'
   pin, and the DXMTRS commits can still be pinned in two steps if the maintainer prefers.
3. **Source-less meson targets take the Rust archive through `objects :`** (to be confirmed by Task 0). Cost if
   wrong: a meson upgrade breaks the build loudly at configure time; the fallback is a generated empty object.
4. **`wsi_*.cpp` are deleted without a new test.** Their Rust has been the only implementation built since step
   3 and every windowed gate row runs it. Cost if wrong: none (a file nothing compiles).
5. **The Rust `wmt_native_*` calls do not write the header's last-error cell.** Cost if wrong: a C caller that
   reads `GetLastError()` after a failed `wmt_native_*` call sees a stale value; no such caller exists in either
   repository (grep in the ledger).
6. **The shims live in `dxmt-util`, not a new crate.** Three crates gain the dependency. Cost if wrong: a few
   kilobytes if LTO keeps something it should not (the export tables and section sizes of Task 2 Step 3 show
   it), or a `no_std` conflict, whose fallback is the three-line crate.
7. **`delegate_forward.m` stays (default for Q2).** Cost if wrong: the count ends at 1 instead of 0 until C4
   retires `mtld3d.so`.
8. **`tests/dx11` is deleted, not ported (default for Q3).** Cost if wrong: upstream's sample programs are gone
   from this fork; they remain in 3Shain/dxmt and in git history.
9. **T-fork is the default for Q1, and nothing of Task 5 starts before the maintainer answers.** Cost if wrong
   (had it started): an LLVM pin and a key move the maintainer did not want.
10. **Headers that are generator inputs stay as the ABI's source of truth.** Retiring the generator (hand-kept
    Rust layouts) is a different decision with its own risk (silent 32/64-bit layout drift) and is not taken
    here. Cost if wrong: about 3,900 lines of first-party C headers remain in a tree called "all Rust".
11. **Per-task lanes are the shortest row that exercises the port; the full gates run once.** Cost if wrong: a
    regression is found at Task 8 instead of at its task, and bisected over at most six commits.

## Questions for the maintainer (sent as one message after the reviews)

- **Q1** the translator's C++: T-fork (recommended), T-keep or T-max.
- **Q2** `delegate_forward.m`: leave until C4 retires `mtld3d.so` (recommended), or a hand-written landing pad.
- **Q3** `tests/dx11`: delete the programs and keep the shaders (recommended), or port five to Rust.
- **Q4** the three comment-only headers: keep (default) or delete.
- The key will move once (Ruling 1): stated, not asked.

## Stated gaps

- No bench: no hot path changes (the thunks keep their shape; the shims are link-time symbols).
- The i686 `__chkstk` and `__tls_array` are exercised only by the guest's 32-bit lane; a 32-bit game is not run
  (no go-ahead asked).
- The native Win32 port is exercised by the native tests and the VM's D3D forwarding rows, not by a game.
