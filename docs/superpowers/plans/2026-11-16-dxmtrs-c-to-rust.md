# DXMTRS: the C, C++ and Objective-C that remain, to Rust

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every source file of DXMTRS that the maintainer agrees to port is Rust, with its gates green and its
behaviour unchanged; what stays in C, C++ or Objective-C (if anything) is listed with its reason. The maintainer's
words (2026-11-16): port the remaining C/C++/Objective-C files "like the rest of DXMTRS". Wine stays C; MacNeutron's
own programs (`dxmt/tests/`, `dxmt/tools/`, `vm/`) are not DXMTRS and are not part of this.

**Architecture:** the 21 files fall in six groups, each with its own answer: runtime shims and thunks that Rust
expresses directly (ported in place: Tasks 2, 3), a second implementation of a C handle protocol MacNeutron's host
keeps using (ported, headers kept: Task 4), sources that MacNeutron's guest proxy compiles out of the DXMTRS tree
(a move between repositories, the maintainer's call), glue over LLVM's C++ API (four ways out, the maintainer's
call), one Objective-C `@try` frame (structural under `panic = "abort"`, the maintainer's call), and upstream sample
programs nothing builds (the maintainer's call). One DXMTRS commit a file or pair, each behind
`tools/rust-check.sh`; one MacNeutron pin move at the end.

**Tech Stack:** Rust 1.99.0, meson 1.12.1, llvm-mingw, LLVM 23 (fork `Mac-Neutron/llvm-project`, branch `air-23`),
sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5, §7. Ledger:
`~/Documents/MacProton/.superpowers/sdd/2026-11-16-dxmtrs-c-to-rust/progress.md` (the main checkout's ignored
folder). Worktrees: `build/crs/dx` (DXMTRS, branch `c-rust`), `build/crs/mn` (MacNeutron, branch `c-rust-mn`);
scratch, logs and reviews in `build/crs/s` (`review-{1-design,2-build,3-translator}.md`, folded; dispositions at the
end, cited as R1-R3).

## Inventory (DXMTRS `5f9e5caa`, `git ls-files`, outside `external/` and `vendor/`)

Counted again on the branch's own base: the same 21 files as at `82a588ba` (R1 recounted: the line counts and the
group totals hold).

### 1. Translator: glue over LLVM's C++ API (3 files, 507 lines)

`src/airconv/shim/shim.cpp` (331), `src/airconv/transforms/lower_16bit_texread.cpp` (98),
`src/airconv/transforms/simdgroup_implicit_membarrier.cpp` (78), with their two `.hpp`.

- **What:** 21 `extern "C"` functions `airconv_shim_*` that `src/airconv/src/llvm/sys.rs:1153-` declares, and two
  LLVM function passes that run inside the shim's hand-built pass pipeline.
- **Who builds it:** `src/airconv/build.rs` (cc-rs, C++17, against `AIRCONV_LLVM`'s headers) into the `airconv`
  crate's archive, which `winemetal`'s static library carries into `winemetal.so` (Mac) and `winemetal.dylib`
  (native). Meson compiles none of it.
- **Why it is not Rust:** Rust reaches LLVM through hand-declared bindings of LLVM's **C API**; the shim is what
  that API lacks. Per function, against the pin's sources (`edc723f40d`) and installed headers
  (`build/wine-arm64-src/llvm23-arm64-nolto/include`, searched recursively; R3 read each body):

  | Shim function | Used by | LLVM 23 C API | Verdict |
  | --- | --- | --- | --- |
  | `get_fast_math_flags`, `is_fp_math_operator` | production | `LLVMGetFastMathFlags` (behind `LLVMCanValueUseFastMathFlags`: unguarded it is undefined on a non-FP value), `LLVMCanValueUseFastMathFlags` | **A**: the C API has it |
  | `memory_attribute`, `captures_none` | production | `LLVMCreateEnumAttribute`: both are integer attributes whose value is this LLVM's encoding (six memory locations, two bits each; `captures(none)` is 0) | **A**, with a lasting test on the printed form (`memory(argmem: read)`, ...): the integer changes when LLVM adds a location |
  | `set_sdk_version` | production | `LLVMAddModuleFlag` (`Warning`, `"SDK Version"`) + `LLVMConstDataArray` (`[2 x i32] [15, 0]`) | **A** |
  | `declare_simdgroup_barrier` | tests only | `LLVMGetNamedFunction`, `LLVMAddFunction`, `LLVMAddAttributeAtIndex` | **A** |
  | `copy_fast_math_flags` | production (`converter.rs:931, 949`, both **clear** flags) | `LLVMSetFastMathFlags` only ORs flags in (`Operator.h:241`); nothing clears one | **B**: rebuild the instruction without the flags (the tree's `rebuilt`, `converter.rs:791-822`), or a fork entry |
  | `set_fast_math_flags` (builder) | production | none: the C builder has no fast-math setter | **B**: set the flags on each instruction the wrapper creates; output at risk |
  | `build_atomic_rmw_value` | production | none; the arithmetic is `LLVMBuild*` calls | **B** |
  | `get_as_instruction` | production | none, but the tree already builds unfolded instructions (`rebuilt`) | **B** |
  | `split_block`, `split_block_and_insert_if_then` | production | the block and instruction movers exist; a PHI's incoming block cannot be set, so successor PHIs are rebuilt | **B**, output at risk |
  | `link_only_needed` | production | `LLVMLinkModules2` has no flags; pruning the source first re-derives it | **B**, link order at risk |
  | `local_slot` | `DXMT_PROBE` and one test | none (`ModuleSlotTracker`); the numbering can be recomputed | **B** |
  | `parse_bitcode` (DXIL's `i8:32` data-layout fix) | production | `LLVMParseBitcodeInContext2` has no data-layout callback | **C**: needs C++ |
  | `erase_named_metadata` | production | no erase in `Core.h` | **C** |
  | `destroy_constant` | production | none | **C** |
  | `rename_struct` | production | no enumeration of identified struct types, no struct rename | **C** |
  | `run_passes` | production | `LLVMRunPasses` runs a text pipeline, but it installs `StandardInstrumentations`, which skips every pass on an `optnone` function (the shim does not), and its parser knows no pass of ours | **C** as it runs today |
  | `pipeline_text` | tests only (`module/tests.rs:313-358`, the two pins of the pipeline) | nothing prints a pipeline | **C** |
  | `Lower16BitTexReadPass` | production | the body is C-API-expressible; a place inside the pipeline is not | **C** for its place |
  | `SimdgroupImplicitMemBarrierPass` | production | `MemorySSA` has no C API | **C** |
  | `probe` | tests only | a link probe; goes with the shim | - |

  **What truly needs C++:** parsing bitcode with a data-layout override, erasing a named metadata node, destroying
  a constant, renaming and listing identified structs, `MemorySSA`, printing a pipeline, and running a pass of our
  own at a chosen place in a pipeline without `StandardInstrumentations`.
- **The house answer already exists** for "LLVM lacks a C entry point": `LLVMWriteAIRBitcode` and
  `LLVMAIRTypedPointerType` live in our LLVM fork (`llvm-c/AIRWriter.h`, component `LLVMAIRBitWriter`), in C++,
  where LLVM's own C++ lives.
- **Four ways out (the maintainer's decision, Q1):**
  - **T-min (recommended): the logic becomes Rust; the fork gains only generic C entry points.** About a dozen
    functions of a few lines each (clear or copy fast-math flags, the builder's fast-math flags, erase named
    metadata, destroy constant, struct enumeration and rename, parse bitcode with a data-layout callback, link
    with flags, constant-expression-as-instruction, split block, local slots, atomic-RMW value), one `MemorySSA`
    query (the barrier pass reads only a load's defining access and its store), and a pipeline runner that takes
    the text plus named Rust callbacks as function passes, registers no `StandardInstrumentations`, and can print
    the pipeline back. Both passes and every translator policy (the `i8:32` filter, the `dx.types.` rename, the
    probe's slot rule) are then Rust, the passes at their exact place. DXMTRS holds no C++ and airconv's `build.rs`
    loses cc-rs. Cost: about 4 agent-days; `LLVM_AIR_COMMIT` moves, three LLVM installs rebuild (dev ThinLTO,
    no-LTO, the `p` profile's), a push to the fork, the key moves. Risk: medium-low for output (two passes of 98
    and 78 lines are re-derived; each gets a unit test pinned on printed IR while the shim still exists, a
    mutation RED, and the 13 corpora byte for byte), medium for the build (a new LLVM component in every link
    list).
  - **T-fork: the C++ moves, unchanged, into the fork.** Bucket A becomes Rust; the shim's other bodies and the
    two passes become fork code behind C names. Cost: about 2 agent-days and the same LLVM consequences. Risk:
    lowest for output. What it hides: translator-specific C++ is relocated, not ported, and every later edit of it
    costs an LLVM commit, a push, three install rebuilds and a key move.
  - **T-A: bucket A only.** Five production functions and one test helper leave the shim; no LLVM change. Cost:
    half a day. The shim and both passes stay.
  - **T-keep:** nothing changes; the shim and passes are DXMTRS's documented C++ exception. The goal is missed by
    507 lines, with the reason written in `docs/CONVENTIONS.md`.
- **A fact that binds every option but T-keep:** the translator key is a hash of source text (Global
  Constraints), so it moves on any edit under `src/airconv`, identical output or not. Task 3 moves it in any case.

### 2. Runtime shims the x86 PE images need (1 file, 18 lines)

`src/util/rust_pe_rt.c`. **What:** `_fltused` (x86, x86-64), `__chkstk` as a jump to mingw's probe (x86-64:
`___chkstk_ms`; i686: `___chkstk`), `__tls_array = 0x2c` (i686): what Rust's `-msvc` code references and
llvm-mingw's CRT does not define. Empty on ARM64, ARM64EC and macOS. **Who links it:** `meson.build:241`
(`rust_runtime_dep`'s `sources`: every Rust image, PE and native), `src/winemetal/unix/meson.build:42` (as
`winemetal.so`'s one source, where it compiles to nothing), `tools/e2e-link.sh:26`. **Why not Rust yet:** it
predates `dxmt_util::dxmt_dll!` and `src/d3d9/src/arm64_crt.rs`, which supply the same kind of symbol from Rust.
**Can it be Rust:** yes: a static with an export name and two `global_asm!` blocks (R2 assembled both: the same
`e9` jump and relocations as the C; the static survives fat LTO without `#[used]`). **The real cost is meson:**
the file is the only *source* of eight targets (nvngx, nvapi64, d3d12, winemetal.dll after Task 3, the guest's
mtld3d, dxmt-replay, the Mac's `winemetal.so`, the native `winemetal.dylib` after Task 4), and a target with no
source is deprecated in meson (Task 0, done). Cost: half a day. Risk: low for the symbols, medium for the build
plumbing.

### 3. winemetal's SM50 thunks (1 file, 169 lines)

`src/winemetal/airconv_thunks.c`. **What:** `winemetal.dll`'s twelve `SM50*` exports: each fills a params struct
and makes a Wine unix call. **Who links it:** `src/winemetal/meson.build:5` (every PE build: ARM64EC on the Mac,
x86_64 and i686 in the guest). **Callers:** d3d11, d3d12 and d3d9's core runtime through the import library.
**Why not Rust yet:** step 2 ported the 158 winemetal thunks and left the translator's for step 4, which ported
the unix side only. **Can it be Rust:** yes, in `winemetal-pe`; `winemetal-wire` mirrors all twelve params
structs with per-target layouts, i686 included (R2 checked `wire/src/lib.rs:2217-2448`), and `slot.rs` numbers
74-88. Cost: half a day. Risk: low. **Binding fact:** MacNeutron's `translator_key` hashes this file and dies
without it, so `wine-arm64/lib.sh` learns the new shape in the same series and the key moves.

### 4. The native build's Win32 calls (2 files, 72 lines)

`src/nativemetal/win32_event.c` (26), `src/nativemetal/win32_native.cpp` (46). **What:** `winemetal.dylib`'s
twelve exported `wmt_native_*` calls and `MTLD3DNotImplemented`, each one line over the `static inline` functions
of `src/util/win32_native.h` and `win32_native_event.h`. **Who links it:** `src/nativemetal/meson.build` (the
native build only, `vm/dxmt-native/build.sh`). **Callers:** the Rust front ends through `winemetal-sys`
(`src/winemetal/sys/src/lib.rs:28-51`), `src/winemetal/unix/src/g7.rs:74-80`, MacNeutron's host by `dlsym`
(`vm/d3d/host/dispatch.c:166`). **Why not Rust yet:** the implementation is the header, and the header is a
protocol MacNeutron's native host and guest glue compile themselves (`vm/d3d/host/events.c`,
`vm/d3d/guest/unix.c`, `proxy.cpp`, `wm_probe/cases_g7.m`, `vm/dxmt-native/tests`). **Can it be Rust:** yes, as a
**second implementation** on the same `struct mn_win32_handle` (the `libc` crate, already a dependency of
`winemetal`'s unix crate). The two headers stay. Cost: one day and a half. Risk: medium (layout or semantics
drift between the header and the Rust; pinned by generated asserts and cross tests, Task 4).

### 5. Window-system C++ that MacNeutron's guest proxy compiles (2 files, 407 lines)

`src/util/wsi_monitor_win32.cpp` (125), `src/util/wsi_window_win32.cpp` (282), with `wsi_monitor.hpp`,
`wsi_window.hpp`, `wsi_platform_win32.hpp`. **What:** DXMT's Win32 window and monitor calls. **Who builds them:**
nothing in DXMTRS (no `meson.build` or `build.rs` names them; their 21 functions are `src/util/src/wsi/win32.rs`
and `wsi.rs` already). **But MacNeutron does** (R1): `vm/d3d/build-guest.sh:32,40` copies both `.cpp` out of the
DXMTRS tree and compiles them into the guest's `mnd3d.dll` (x86_64 and i686), and `vm/d3d/guest/proxy.cpp:15`
includes `wsi_window.hpp`. So for DXMTRS they are already ported; they stay in the tree as a source MacNeutron's
C++ proxy borrows. **Options (Q3):** move the five files into MacNeutron (`vm/d3d/guest/wsi/`, with the DXVK
notice; `build-guest.sh` reads them there; the same pin as the DXMTRS deletion), which is the recommendation: the
proxy is MacNeutron's C++ and stays C++; or keep them in DXMTRS as an accepted non-Rust source carried for
MacNeutron. Cost of the move: an hour and one run of `vm/d3d/build-guest.sh`. `src/util/dxvk.LICENSE` stays
either way (the Rust is still derived from DXVK).

### 6. D3D9's macOS side (1 file, 32 lines)

`src/d3d9/unix/src/metal/macdrv/delegate_forward.m`. **What:** sends
`applicationDidChangeScreenParameters:` to Wine's application delegate inside `@try`/`@catch`, and asks
`respondsToSelector:`. **Who builds it:** `src/d3d9/unix/build.rs:77` (cc-rs) into `mtld3d.so`. **Why it is not
Rust:** `mtld3d.so` is built with `panic = "abort"` (`Cargo.toml:153-157,195`; `src/d3d9/unix/meson.build:9`
passes `production`), under which a foreign exception that unwinds into a Rust frame ends the process; objc2's
own documentation says `catch` cannot work there (`objc2-0.6.5/src/exception.rs:299-301`). The catch must sit in
a frame with no Rust between it and the send.
- **What happens to it without this plan (R1):** the spec retires `mtld3d.so` in **C5**, and in **C4** the EDR
  observer this forward belongs to becomes a winemetal slot (spec lines 1073-1077, 1134). `winemetal.so` is built
  `production-unwind`, where objc2's `exception::catch` works, through a crate that is not locked today
  (`objc2-exception-helper`: a download; whether it compiles Objective-C itself is to be read when fetched).
- **Options (Q2):** leave the file now and settle it when C4 re-homes the observer (objc2's exception feature
  there, or the `.m` moves into winemetal), which is the recommendation; or replace it now with a hand-written
  landing pad in `global_asm!` (personality `__objc_personality_v0`, a hand-encoded LSDA: about 60 lines of unwind
  tables nobody can review). Cost of leaving: the count ends at 1 until C4, and reaches 0 there only if the
  maintainer then accepts the new crate.

### 7. Upstream sample programs (11 files, 5,366 lines)

`tests/dx11/*.cpp` with `3DMaths.h`, `ObjLoading.h`, `stb_image.h` (7,525 lines, third-party). **What:** DXMT
upstream's D3D11 sample windows. **Who builds them:** `tests/dx11/meson.build` builds five of the eleven, only
under `enable_tests`, which nothing in either repository sets (R1: `git grep`); `dx11_model.cpp` is empty. No gate
runs any. MacNeutron's shader corpus reads the folder's nine `.hlsl` files (`dxmt/tests/dxbc-corpus/sources.txt`),
nothing else. **Can they be Rust:** yes, as five or eleven Windows programs (no application-side D3D11 binding
exists in the tree; about 5 agent-days for programs no gate would run). **Recommendation (Q4):** delete the
`.cpp`, the three headers, `cube.obj`, `test.png`, `testTexture.png` and the meson files (and the `enable_tests`
option with them); keep the `.hlsl`.

### Headers

| Header | Consumer today | After this plan |
| --- | --- | --- |
| `include/native/**` | meson's native include path; `tools/winemetal-layouts.sh`; MacNeutron (`vm/dxmt-native/build.sh:71` copies it; `release/source-archive.sh`) | stays |
| `src/winemetal/winemetal.h`, `winemetal_thunks.h`, `airconv_thunks.h`, `src/airconv/airconv_public.h`, `dxil/dxil_public.h` | `tools/winemetal-layouts.sh` (layouts, protos, decls, `--airconv`) and rust-check's `gen` leg; MacNeutron's `vm/metal` (`build-guest.sh:17`, `wm/*.c`) and `wm_probe` | stay: the generated bindings' source of truth |
| `src/dxmt/dxmt_format.hpp`, `dxmt_presenter.hpp`, `dxmt_scaler.hpp`, `dxmt_shader_cache.hpp`, `src/winemetal/sys/sm50-abi.h` | `--seam-abi` (each seam's `abi.rs` names its header), rust-check | stay |
| `src/dxmt/dxmt_command_constants.hpp` | `#include`d by `dxmt_command.metal` | stays |
| `src/d3d11/d3d11_interfaces.hpp`, `src/d3d12/d3d12_interfaces.hpp`, `src/dxgi/dxgi_interfaces.h`, `src/util/com/com_guid.hpp` | `include_str!` in `src/util/src/ext/tests.rs` (IIDs); MacNeutron compiles them (`vm/d3d/host/stubs.h`, `guest/proxy.h`, `rules.txt`) | stay |
| `src/d3d12/d3d12_barrier_legacy.hpp`, `d3d12_shared_fence.hpp` | MacNeutron's guest proxy and barrier test (`vm/d3d/guest/proxy.cpp:12-13`, `vm/d3d/tests/barrier-test.cpp:8`) | stay |
| `src/util/win32_native.h`, `win32_native_event.h`, `util_d3dkmt.h` | MacNeutron's native host, guest glue and tests; the wsi C++ | stay |
| `src/util/wsi_monitor.hpp`, `wsi_window.hpp`, `wsi_platform_win32.hpp` | MacNeutron's guest proxy | go where Q3 sends the `.cpp` |
| `src/airconv/dxbc_root_signature.hpp` | named in Rust doc comments only | kept (default): deleting it is tidy-up, not porting, and it sits under the hashed `src/airconv` |
| `src/winemetal/wineunixlib.h`, `src/nativemetal/wineunixlib.h` | `airconv_thunks.c:2` only (R1: no MacNeutron file includes either) | deleted with Task 3 |
| `src/airconv/transforms/*.hpp` | the shim | go with Q1's answer |

## Global Constraints

- **Behaviour unchanged.** No export, import, wire field, config key or log line changes. Per image the export
  table is compared before and after (`llvm-readobj --coff-exports`, `nm -gU` for Mach-O), and the plain
  `d3d9.dll`'s imports stay as they are. Stated differences are listed in each task and nowhere else.
- **The translator key is a hash of source text, not of output.** `wine-arm64/lib.sh`'s `translator_key` hashes
  every file under `src/airconv` (tests aside), `src/winemetal/airconv_thunks.[ch]`, `tools/llvm.sh` and airconv's
  resolved crates. So the key (`3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` today) moves when
  Task 3 lands and when any translator file is touched, with identical output. The task's "moves only if output
  changes" cannot hold as written (Ruling 1). Unchanged translator output is proved by C3's 13 in-repo corpora
  (`build/c3/xlate.sh`, a copy under `build/crs/s` that takes the LLVM prefix), `cmp -r` of their output folders
  and the `dxso_identity` digest, never by the key. All key-moving commits land under **one** pin move. The key's
  new value and the reason are called out in the pin commit, the spec and the final report.
- `tools/rust-check.sh` passes after every DXMTRS commit and the commit is chained on its exit code
  (`sh tools/rust-check.sh > log 2>&1 && git commit ...`). Commit bodies follow CONTRIBUTING §Changes.
- No new dependency, no new lint suppression, no `pub(crate)`, no new static holding mutable state beyond the one
  thread-local this plan names. No em dashes. `~` for the home folder in anything committed.
- Edits only in `build/crs/dx` and `build/crs/mn`; the other session's `build/c4b3c/*` worktrees and branches are
  not touched. Builds, installs, gates and any checkout in `build/wine-arm64-src/dxmt` run under
  `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`, one job each, the checkout restored by the job's
  trap. No `make` in a worktree. A job tests committed tips; a job script that has run is never edited.
- **Every MacNeutron script runs with `BUILD_DIR=$HOME/Documents/MacProton/build` in the environment**, from this
  session and from any subagent it briefs: without it a worktree's script builds llvm-mingw into the worktree
  (it happened during review 1; ledger).
- **The shared LLVM installs are not rebuilt by this plan before its pin.** `build_llvm` deletes an install or a
  source folder whose commit differs from `tools/llvm.sh`'s. Translator work builds LLVM into its own folders
  (`build/crs/llvm23-arm64{,-nolto}`) from a dedicated detached checkout at exactly the pinned commit, and points
  `AIRCONV_LLVM` and the corpus script at them.
- No push before the other session's slice C4b-3d5 is on both mains; then fetch, rebase, re-run rust-check and
  the count, privacy scan, push.
- Per task one lane; full gates once (Task 8). No gate while the maintainer is gaming; the Mac idle while
  `dxmt/check.sh` runs. Stop at 95% weekly usage.
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0 BUILD_DIR=$HOME/Documents/MacProton/build
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/crs/cargo`.

## Order

Task 0 (spike, **done**) → 2 (`rust_pe_rt.c`) → 3 (`airconv_thunks.c`) → 4 (native Win32) → **the maintainer's
answers** → 1 (wsi, per Q3) → 5 (translator, per Q1) → 6 (`delegate_forward.m`, per Q2) → 7 (`tests/dx11`, per Q4)
→ 8 (full gates) → 9 (docs, spec, pin, push). Tasks 2-4 depend on no answer and start once the reviews are folded.
Tasks 3 and 5 move the key; they share Task 9's one pin.

### Task 0: an image with no C source in the tree links as today (spike, done 2026-11-16)

Meson 1.12.1, `build-win64.txt`, llvm-mingw, a DLL from a custom-target archive (`build/crs/s/spike`; R2's toy
configures in `build/crs/s/r2-scratch` agree):

| Form | Result |
| --- | --- |
| `link_with`, no source | links with today's line minus the object; meson warns "has no sources ... support will be removed" |
| `objects : <the archive>` | configure error: "are not objects" (only `.o`, `.obj`, `.res`) |
| `link_whole` | links with `--whole-archive`: every member in, not today's link |
| the archive in `sources` only | no warning, but the archive moves past `-lntdll` and `--end-group` (R2) |
| the archive in `sources` and `link_with` | no warning, today's place (R2); not available to `winemetal.so` (`native : true` cannot `link_with` a host custom target) |
| **an empty source meson writes itself** (`configure_file(output : 'no_source.c', capture : true, command : ['true'])`) | no warning, today's line with that object where `rust_pe_rt.c`'s was, the same section table as today's shape; one form for all eight targets |

**Ruling 3:** `rust_runtime_dep`'s `sources` becomes that generated empty file; `winemetal.so` and the native
`winemetal.dylib` name it where they named the C. No C file is in the tree; one empty translation unit is
compiled in the build folder, because meson picks a target's link driver from a compiled input. The link driver
stays C for the PE images; the native dylibs go from the C++ driver to the C one when `win32_native.cpp` leaves
(R2: none of them loads libc++ today, and `airconv_dep_darwin` and d3d11 name `-lc++` themselves), recorded by
`otool -L` in Task 4.

### Task 2: `rust_pe_rt.c` becomes Rust

**Files:** create `src/util/src/pe_rt.rs` (declared in `src/util/src/lib.rs` under
`cfg(all(windows, target_env = "msvc", any(target_arch = "x86", target_arch = "x86_64")))`; `arm64ec` is its own
`target_arch`, so ARM64EC stays out as in the C); modify `meson.build` (`rust_runtime_dep`'s `sources` is the
generated empty file), `src/winemetal/unix/meson.build:42` (the same file; its `c_args` stay, so the image keeps
its `__objc_imageinfo` section), `tools/e2e-link.sh` (no C compile; the symbols come from the test crate's
`dxmt-util`, a `use dxmt_util as _;` if the crate does not already reach it), `Cargo.toml` and the root file of
the two static-library crates that do not reach `dxmt-util` (`src/replay`, `src/winemetal`: a workspace path
dependency and `use dxmt_util as _;`; `src/d3d9/shim` reaches it through `mtld3d-shared`), `README.md:43`,
`src/d3d9/src/arm64_crt.rs:6` (its comment); delete `src/util/rust_pe_rt.c`.

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

A module, not a `dxmt_dll!` arm: an arm needs a call at each of eleven crate roots and a forgotten one fails only
at that image's link; a module needs none (R2: a private static of a dependency survives fat LTO in a staticlib).

- [ ] **Step 1 (test first, the reference):** under the lock, from the base commit, the Mac build, the guest
  build (x86_64 and i686) and the native build. Record from the **build folders** (the guest installs are
  stripped) for every PE image: `llvm-nm` lines of `_fltused`, `__chkstk`, `__tls_array`; the disassembly of
  `__chkstk`; `llvm-readobj --coff-exports --coff-imports`; the section table. For `winemetal.so`: `nm -m` of
  `___wine_unix_call_funcs` and `___wine_unix_call_wow64_funcs` (both `(__DATA_CONST,__const) external` today),
  `otool -L`, the section table. For the five native dylibs: `nm -gU`, `otool -L`. Saved under
  `build/crs/s/ref/`. Step 1 is also Tasks 3 and 4's reference.
- [ ] **Step 2:** write `pe_rt.rs`, the meson changes, the `Cargo.toml` lines; rust-check; commit
  (`util: the MSVC runtime shims of the x86 PE images are Rust`).
- [ ] **Step 3 (lane):** under the lock, the same three builds at the tip; the same records. Expected: every
  symbol present once per image, `__chkstk`'s body equal, exports and imports identical, no meson warning about
  sources, and **two stated differences**: `_fltused` is `R` in `.rdata` where the C's was `B` in `.bss` (4 bytes
  move sections), and nothing else in the section tables. `winemetal.so`'s two tables still its own. Then
  `dxmt/wm-probe-check.sh --so <the tip's winemetal.so>`, `vm/dxmt/check-build.sh` (the guest build's formats,
  exports and imports), `vm/tests/dxgi-guest.sh i386` (the i686 D3D10-12 images load: `__chkstk` and
  `__tls_array` live) and `vm/dxmt/d3d9-gate.sh`'s default lane (the i686 d3d9.dll and mtld3d.dll).
- [ ] **Step 4:** if a crate cannot take `dxmt-util`, fall back to a three-line crate `dxmt-pe-rt` and ledger it.

### Task 3: `airconv_thunks.c` becomes Rust

**Files:** create `src/winemetal/src/sm50.rs` (`#[cfg(any(windows, test))] mod sm50;`: Windows-only in a build,
because `winemetal-pe` is also linked into the native `winemetal.dylib` beside airconv, which exports the real
`SM50Initialize`), `src/winemetal/src/sm50/abi.rs` (generated) and `sm50/tests.rs`; modify
`src/winemetal/src/lib.rs` (the module, the comment at line 9), `src/winemetal/src/transport.rs` (`params!` gains
the twelve `Sm50*Params`; its SAFETY line covers raw pointers), `src/winemetal/meson.build` (no C source; the
comment), `tools/winemetal-layouts.sh` (its slot scan of `src/winemetal/src/*.rs` leaves `sm50.rs` out:
`airconv_unixcalls` already supplies slots 74-88, and two export names do not spell their slot's name),
`tools/rust-check.sh` (one `gen_cmp` line for `sm50/abi.rs`; `winemetal-pe` in `HOST_P`),
`src/winemetal/airconv_thunks.h:233` (the `UNIX_CALL` macro goes with `wineunixlib.h`), `tools/audit.sh` if it
lists the file; delete `src/winemetal/airconv_thunks.c`, `src/winemetal/wineunixlib.h`,
`src/nativemetal/wineunixlib.h`. MacNeutron: `wine-arm64/lib.sh` (`translator_key` takes
`src/winemetal/src/sm50.rs` where `airconv_thunks.c` is absent, as it does for `d3d11_shader.cpp` / `args.rs`;
the header is still hashed), `wine-arm64/tests/translator_key_test.sh` (both shapes), the comment block above
`translator_key`, `wine-arm64/README.md:204`.

Each export is `#[unsafe(no_mangle)] pub unsafe extern "C" fn` of the C's name and signature (not through
`exports!`, which also emits a native twin). It fills the `winemetal-wire` mirror zeroed, then each field the C
wrote, calls the crate's unix call with the slot, answers `-1` on a non-zero status where the C does, `ret` or
`ret_size` otherwise. Zeroing is the one difference from the C, which left `ret` uninitialised before the call:
the unix side writes it on success and the C ignored it on failure.

- [ ] **Step 1 (test first, MacNeutron):** `translator_key_test.sh` gains "a tree with `sm50.rs` and no
  `airconv_thunks.c` has a key", "editing `sm50.rs` changes it", "a tree with neither dies"; red; then `lib.sh`;
  green (`sh wine-arm64/tests/translator_key_test.sh`, host only, no lock).
- [ ] **Step 2 (test first, DXMTRS):** `sm50/tests.rs`: the test binary defines `__wine_unix_call_funcs` as a
  table of recording handlers and calls each of the twelve exports: the slot reached, every params field as the
  unix side receives it (the tessellation and geometry pairs each with distinct shader values, so a swapped
  field fails), the value answered for status 0 and for a failing status, `ret_size` 0 when the handler writes
  nothing. Red: no module. If the host test binary cannot define the table (the crate's host `unix_call` and the
  `dynamic_lookup` link flags decide), the step is replaced by the lanes and ledgered.
- [ ] **Step 3:** write `sm50.rs`; generate `sm50/abi.rs`
  (`sh tools/winemetal-layouts.sh --seam-abi src/winemetal/sys/sm50-abi.h crate::sm50 ...`); the `gen` leg
  regenerates `layouts.rs`, `protos.rs`, `decls.rs` and the new `abi.rs` equal; rust-check; commit
  (`winemetal: the SM50 thunks of winemetal.dll are Rust`).
- [ ] **Step 4 (lane):** under the lock, the Mac and guest builds of the tip: `winemetal.dll`'s export table
  equals the reference's on ARM64EC, x86_64 and i686; `dxmt/check.sh` with `MACNEUTRON_ARM64_TESTS` naming the
  D3D11 and D3D12 probe programs (every draw compiles through `SM50Initialize` and `SM50Compile`; the
  tessellation and geometry rows through the pipeline calls); `vm/tests/d3d11-guest.sh i386` (the i686 thunks'
  32-bit params).

### Task 4: the native build's Win32 calls become Rust

**Files:** create `src/winemetal/unix/src/native_win32.rs` (under the crate's `native` feature) with
`native_win32/abi.rs` (generated) and `native_win32/tests.rs`; modify `src/winemetal/unix/src/lib.rs`,
`src/winemetal/unix/src/g7.rs:74-80` (calls the module, no `extern` block), `src/nativemetal/meson.build` (the
generated empty source; its `c_args` stay), `src/util/win32_native_event.h` (gains the `dxmt-abi` block of the
twelve prototypes and `MTLD3DNotImplemented`, inside `#ifdef __cplusplus extern "C" {` guards placed outside the
markers, as the generator expects), `tools/winemetal-layouts.sh` (`--decls` reads that block; one more record,
`struct mn_win32_handle`, in the darwin record-layout dump, printed as size and offset asserts),
`tools/rust-check.sh` (the `gen_cmp` lines), `tools/audit.sh`; delete `src/nativemetal/win32_event.c`,
`src/nativemetal/win32_native.cpp`. MacNeutron: `vm/dxmt-native/tests/win32-test.cpp` and `win32.sh` (the test
links `winemetal.dylib` with an rpath and runs after `build.sh`).

The definitions are `extern "C-unwind"` (what `winemetal-sys` declares and `--seam-abi` asserts) and each opens
with the crate's `AbortOnPanic` guard (CONVENTIONS: winemetal's exports under C names), since the library is built
`production-unwind` and a C host calls them. `MnWin32Handle` is `#[repr(C)]` over `libc::pthread_mutex_t` and
`libc::pthread_cond_t`; its asserts are generated from the header, not typed. Objects come from `libc::calloc`
and go to `libc::free`: the header frees what this module creates and the reverse.

**The semantics to copy** (`win32_native.h:101-251, 421-428`, `win32_native_event.h:40-95`; Step 3's checklist):
- `is_handle` reads `kind` from any pointer that is not null, -1 or -2.
- **create_event:** `max` 1, `count` is `initial ? 1 : 0`, `manual` stored as given. **create_semaphore:** refuses
  `max <= 0`, `initial < 0`, `initial > max`.
- **release_semaphore:** refuses `n <= 0` and a non-semaphore, checks `count <= max - n`, writes `previous` only
  on success, broadcasts.
- **wait:** events and semaphores only (a thread, a file and a relay answer `WAIT_FAILED`); `ms == 0` polls;
  `INFINITE` is `pthread_cond_wait`; a timed wait runs to a `CLOCK_UPTIME_RAW` deadline through
  `pthread_cond_timedwait_relative_np` and loops on spurious wakeups; a manual event is not consumed, an
  auto-reset event and a semaphore are.
- **duplicate:** refuses a null target, a null, -1 or -2 source and a relay; a file gets a new handle with plain
  `dup()` (no close-on-exec), freed on failure; anything else gains a relaxed reference and answers the same
  pointer; `DUPLICATE_CLOSE_SOURCE` (1) then closes the source.
- **open_thread:** refuses id 0 and a short `proc_pidinfo` read. The query is `PROC_PIDTHREADID64INFO` (15),
  which the `libc` crate lacks (its `PROC_PIDTHREADINFO`, 5, is another query): a named constant, asserted
  against the SDK header by the generator as it asserts winemetal.h's macros. The time source is
  `libc::clock_gettime(CLOCK_UPTIME_RAW, ..)` (the crate has no `clock_gettime_nsec_np`).
- **unref / close:** an acquire-release decrement; the last one closes a file's descriptor, calls a relay's
  `relay_end(ctx)` when not null, destroys an event's or semaphore's mutex and condition (never a thread's), sets
  `kind = 0`, frees. `close_handle` answers 1 for -1 and -2.
- **set_event:** calls a relay's `relay(ctx)` without a null check; else sets and broadcasts. **ref:** relaxed.
  **handle_count:** a relaxed load, 0 for a non-handle.
- **the not-implemented flag:** one `const`-initialised `thread_local!` `Cell<bool>`, false per thread, read and
  cleared by `MTLD3DNotImplemented`.

- [ ] **Step 1 (test first, MacNeutron):** `win32-test.cpp` gains cross cases against the dylib's exports (the
  header's own declarations): an event the dylib created set, waited on and closed by the header's inline
  functions and the reverse; a semaphore's count and its too-many-posts refusal through both; a duplicate made by
  one side closed by the other; a header-made **file** handle duplicated and closed by the dylib; a relay set
  through `wmt_native_set_event` and a relay whose last reference the dylib drops (`end(ctx)` called once); a
  thread handle from `wmt_native_open_thread` closed by the header; `wmt_native_handle_count` against the
  header's `refs`; a timed wait that times out; the pseudo-handles -1 and -2 with each call's own result (close
  1; set_event, unref, handle_count 0; ref a no-op; wait `WAIT_FAILED`; duplicate and release 0). Under the
  test's `-fsanitize=address,undefined`, against the **C** dylib of Task 2's reference build first: green is the
  reference.
- [ ] **Step 2 (test first, DXMTRS):** host unit tests of the module for the same cases (plain pthread code; it
  runs on the Mac host under the `native` feature).
- [ ] **Step 3:** write the module from the header, function by function, against the checklist; generate
  `native_win32/abi.rs` and the layout asserts; rust-check (`decls.rs` regenerates equal: the block moved, its
  text did not); commit (`winemetal: the native build's Win32 calls are Rust`).
- [ ] **Step 4 (lane):** under the lock, `vm/dxmt-native/build.sh` at the tip; `nm -gU winemetal.dylib` equals
  the reference's (the same 13 names); `otool -L` of the five dylibs against the reference (the driver change of
  Ruling 3); `vm/dxmt-native/tests/win32.sh`, `hook.sh`, `wmt-exports.sh`;
  `dxmt/wm-probe-check.sh --native <the tip's winemetal.dylib>`.
- **Stated difference (Ruling 5):** the C++ calls set the header's last-error cell on failure; the Rust calls do
  not. That cell has internal linkage (a `static inline` function's local `thread_local`, one per translation
  unit) and `winemetal.dylib` exports no `mn_win32*` symbol, so nothing outside `win32_native.cpp` could ever
  read what it wrote.

### Task 1: the window-system C++, per Q3

Default (move): MacNeutron gains `vm/d3d/guest/wsi/` with the two `.cpp`, the three `.hpp` and DXVK's notice;
`vm/d3d/build-guest.sh:32` copies from there and its `-I$DXMT/util` stays for `util_d3dkmt.h`; DXMTRS deletes the
five files, `tools/audit.sh`'s two entries and rewords `src/util/src/wsi.rs:47,57` and `wsi/win32.rs:1`. Test
first: `vm/d3d/build-guest.sh` at the base under the lock (the reference `mnd3d.dll`s' export tables), then at
the tips: equal. One DXMTRS commit (`util: the window-system C++ leaves for the guest proxy that compiles it`),
one MacNeutron commit, the same pin.

### Task 5: the translator, per Q1

Not started before the answer. T-keep makes it a paragraph in `docs/CONVENTIONS.md`; T-A is Step 2 alone. For
T-min or T-fork the task gets an addendum to this plan with its file list and one Opus review before code (the
fork's entry points are an interface of their own). What binds any form of it (R3):

- [ ] **Step 1 (the reference):** a copy of `build/c3/xlate.sh` under `build/crs/s` that takes the LLVM prefix;
  the 13 corpora at the base, their output folders kept; the `dxso_identity` digest with its program count;
  `pipeline_text(0)` and `(1)`. **A mutation RED:** the corpora translated with the two passes off must differ
  for each pass separately; a pass that changes no corpus gets a unit test on a minimal module, its printed IR
  pinned while the shim still exists, kept after.
- [ ] **Step 2 (bucket A, no LLVM change):** one commit a function group; each has a test written while the shim
  exists that asserts the Rust result equals the shim's and then pins the printed form (`memory(...)`,
  `captures(none)`, `!{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 0]}`, the fast-math bits behind the
  `LLVMCanValueUseFastMathFlags` guard). `copy_fast_math_flags` is **not** in this step.
- [ ] **Step 3 (the fork):** a worktree of `build/forks/llvm-project` on a new branch from `air-23`; entry
  points in a new component beside `LLVMAIRBitWriter`, no upstream file edited beyond one `add_subdirectory`
  line; the runner is our own (`registerPipelineParsingCallback` for the two names, no
  `StandardInstrumentations`, a printer for the two pins of `module/tests.rs`), given the long printed pipeline
  text (a bare `instcombine` would turn fixpoint verification on).
- [ ] **Step 4:** LLVM built into `build/crs/llvm23-arm64{,-nolto}` by `tools/llvm.sh`'s recipe from a dedicated
  detached checkout at the new commit; `llvm_deps` regenerated (`ninja llvm-config` in that build folder, since
  no install carries it) and `build.rs`'s `rerun-if-changed` loses `shim` and `transforms`; rust-check with
  `AIRCONV_LLVM` on the new no-LTO install; the corpora `cmp -r`-equal to Step 1 and the digest equal.
- [ ] **Step 5:** the Mac build fetches the pinned commit, so the fork branch is pushed (a new branch, the
  maintainer's yes in Q1) before Task 8; `air-23` is fast-forwarded with Task 9. The shared installs rebuild
  once, in Task 8's job, after C4b-3d5 has landed; `vm/dxmt-native/build.sh` runs after the Mac build (it checks
  only that an install exists). The `p` profile's install rebuilds at its next build.

### Task 6: `delegate_forward.m`, per Q2

Default (leave): one sentence in `docs/CONVENTIONS.md`'s list of accepted non-Rust sources, `tools/audit.sh`'s
allow-list entry, and a line in spec §5 under C4 (the observer's re-homing decides the file). No code change.

### Task 7: `tests/dx11`, per Q4

Default (delete): remove the eleven `.cpp`, the three headers, `cube.obj`, `test.png`, `testTexture.png`,
`tests/dx11/meson.build`, `tests/meson.build`, the `enable_tests` option and `meson.build`'s `subdir('tests')`
block (D3D9's `tests/d3d9` is cargo's, not meson's); keep the nine `.hlsl`. Test first: every path of
`dxmt/tests/dxbc-corpus/sources.txt` exists at the branch tip. One commit (`tests: upstream's D3D11 sample
programs leave; their shaders stay for the corpus`). Does not move the key.

### Task 8: full gates (once, under the lock, committed tips, the Mac build first)

- [ ] `tools/rust-check.sh`; `sh wine-arm64/tests/translator_key_test.sh`.
- [ ] Mac: dev build of the tip; the x64 `dxmt/check.sh` (Mac idle); `dxmt/d3d9-check.sh` default lane; the
  plain `d3d9.dll` import check; `dxmt/wm-probe-check.sh` (`--so`).
- [ ] VM guest: `vm/dxmt/build.sh`, `vm/dxmt/check-build.sh`, `vm/dxmt/gate.sh`, `vm/dxmt/d3d9-gate.sh`,
  `vm/tests/d3d11-guest.sh` and `vm/tests/dxgi-guest.sh` for `i386` and `x86_64`, `vm/d3d/build-guest.sh`.
- [ ] Native and forwarding (what loads `winemetal.dylib` in a real host): `vm/dxmt-native/build.sh` and its
  `tests/`, `dxmt/wm-probe-check.sh --native`, `vm/tests/d3d-smoke.sh`, `vm/dxmt/present-gate.sh forward`.
- [ ] If Task 5 ran: the 13 corpora `cmp -r`-equal on the shared installs at the new commit; the new key
  recorded.
- [ ] `git ls-files | /usr/bin/grep -E '\.(c|cc|cpp|cxx|m|mm)$'` outside `external/` and `vendor/` lists exactly
  what the maintainer chose to keep.

### Task 9: review, docs, spec, pin, push

- [ ] One Opus review of the whole diff (correctness; the meson form; the handle protocol; the generator edits).
- [ ] DXMTRS: `README.md:43`, `CLAUDE.md:8-9`, `docs/DEVELOPMENT.md` (what cc-rs still compiles),
  `docs/CONVENTIONS.md` and `tools/audit.sh` (the C++ allow-list becomes "no C, C++ or Objective-C source under
  `src/` or `tests/` except: <the kept list, each with its reason>", extended to `.c` and `.m`).
- [ ] MacNeutron: spec §5 (what was ported, what stays and why, the key's move), `dxmt/pins`,
  `vm/setup/parts.sh:42` and `wine-arm64/licenses/README:46` (they name `rust_pe_rt.c`),
  `dxmt/tests/dxbc-corpus/barrier.hlsl:3` if the passes moved.
- [ ] Wait for C4b-3d5 on both mains; fetch; rebase both branches; rust-check and the count again; privacy
  scan; push DXMTRS main (and fast-forward `air-23` if Task 5 ran), verify with `git ls-remote`, pin, push
  MacNeutron main. Then delete `build/crs/cargo` and the task's LLVM folders (disk hygiene).

## Rules check

- **New files:** `src/util/src/pe_rt.rs`; `src/winemetal/src/sm50.rs`, `sm50/abi.rs` (generated), `sm50/tests.rs`;
  `src/winemetal/unix/src/native_win32.rs`, `native_win32/abi.rs` (generated), `native_win32/tests.rs`. No
  `mod.rs`. No new crate (Ruling 6).
- **Statics:** `FLTUSED` (immutable; a symbol the linker needs; its doc block says so). One `thread_local!`
  `Cell<bool>` in `native_win32.rs`: per-thread state that replaces the C's `_Thread_local bool` of the same
  meaning; CONVENTIONS "state lives on an object" allows it with the argument in its doc block (a C host reads
  the flag by `dlsym`, with no object to hold it). `sm50/tests.rs` defines the unix-call table for the test
  binary only.
- **Duplicate:** `native_win32.rs` is a second implementation of `win32_native.h`'s event, semaphore, duplicate,
  thread and reference calls (and of `mn_win32_is_handle`, `_set_event`, `_ref`, `_unref`), which stay for
  MacNeutron's C and C++. Allowed because the header cannot leave (its users are not DXMTRS) and the C files
  cannot stay (the goal); pinned by the generated layout and prototype asserts and Task 4's cross cases.
- **Exports under C names (winemetal):** the thirteen native ones are `extern "C-unwind"` and open with
  `AbortOnPanic`; the twelve `SM50*` are `extern "C"` on Windows, as the 158 thunks are.
- **Dependencies:** none added. `dxmt-util` becomes a path dependency of two more workspace crates
  (`winemetal-pe`, `dxmt-replay`); it has no `#[no_mangle]` outside `dxmt_dll!` and depends on `log` alone, so
  it adds no import (R2). T-min or T-fork removes `cc` from airconv's build dependencies (which moves the key
  too).
- **Wire fields, exports, log lines, runtime config keys:** none added, removed or changed. The meson option
  `enable_tests` goes with Task 7.
- **Lint suppressions:** none. `export_name = "_fltused"` on an upper-case static avoids
  `non_upper_case_globals`.
- **Raw selectors, `pub(crate)`, type aliases, glob imports, derives:** none.
- **Generated files:** two new seam files (`sm50/abi.rs`, `native_win32/abi.rs`) and one more record in the
  layout dump, each with its `gen_cmp`; `layouts.rs`, `protos.rs` and `decls.rs` regenerate equal.
- **Audit:** `CPP_SOURCES` shrinks with each task and ends as the kept list; the rule grows to `.c` and `.m`.

## Rulings (planning)

1. **The key moves and that is called out, not avoided.** The brief asks that the key move only if output
   changes; `translator_key` hashes source text, and Task 3 alone moves it. Cost if wrong: nothing extra for
   players if this lands before the next release, since the key has moved three times since `v1.0.0` and that
   release turns every shader cache over anyway (seconds to minutes a game, once); developers' caches turn over
   at the pin.
2. **One pin for every key-moving commit.** Cost if wrong: a late task that fails holds the earlier ports out of
   a pin until it is fixed or dropped; the DXMTRS commits are separable, so the pin can then take the finished
   ones.
3. **Targets with no C source take an empty source meson writes itself** (Task 0). Cost if wrong: one empty
   translation unit is compiled in the build folder, which a strict reading of "no C" could object to; the
   reserve form (`sources` + `link_with` of the archive, R2) compiles none but does not fit `winemetal.so`.
4. **The wsi C++ is MacNeutron's to hold (default for Q3).** Cost if wrong: five files move repositories and
   their history stays behind in DXMTRS; reversible by one commit each way.
5. **The Rust `wmt_native_*` calls do not write a last-error cell.** Cost if wrong: none found; the C's cell was
   unreadable from outside its translation unit.
6. **The shims live in a module of `dxmt-util`, not a new crate and not a `dxmt_dll!` arm.** Cost if wrong: a
   crate that cannot take the dependency gets the three-line crate instead.
7. **`delegate_forward.m` stays until C4 re-homes its observer (default for Q2).** Cost if wrong: the count ends
   at 1, and C4 inherits a decision (a new crate, or an Objective-C file in winemetal).
8. **`tests/dx11` is deleted, not ported (default for Q4).** Cost if wrong: upstream's sample programs are gone
   from this fork; they remain in 3Shain/dxmt and in git history.
9. **Nothing of Task 5 starts before the maintainer answers; T-min is the recommendation.** Cost if wrong (had
   it started): an LLVM pin, three install rebuilds and translator edits the maintainer did not want.
10. **Headers that are generator inputs or that MacNeutron compiles stay.** Retiring the generator (hand-kept
    Rust layouts) is a different decision with its own risk (silent 32/64-bit layout drift). Cost if wrong:
    several thousand lines of first-party C headers remain in a tree called "all Rust".
11. **Per-task lanes are the rows that exercise the port; the full gates run once.** Cost if wrong: a
    regression is found at Task 8 instead of at its task, and bisected over at most seven commits.
12. **Task 5's fork work gets its own addendum and review after Q1, not a task list now.** Cost if wrong: a day
    between the answer and the first translator commit; the alternative is a detailed plan for an option the
    maintainer may not choose.

## Questions for the maintainer (one message, after this fold)

- **Q1** the translator's C++: T-min (recommended; includes a push of a new branch to `Mac-Neutron/llvm-project`
  and three LLVM rebuilds), T-fork, T-A or T-keep.
- **Q2** `delegate_forward.m`: leave until C4 re-homes the observer (recommended), or a hand-written landing pad
  now.
- **Q3** the wsi C++ that MacNeutron's guest proxy compiles: move it into MacNeutron (recommended), or keep it in
  DXMTRS.
- **Q4** `tests/dx11`: delete the programs and keep the shaders (recommended), or port five to Rust.
- Stated, not asked: the key moves once; `dxbc_root_signature.hpp` and every header a generator or MacNeutron
  reads stay.

## Stated gaps

- No bench: no hot path changes (the thunks keep their shape; the shims are link-time symbols).
- No 32-bit game is run; the i686 images are exercised by the guest's i386 scripts.
- The native Win32 port is exercised by the native tests, the probe and the VM's forwarding rows, not by a game.
- `objc2-exception-helper`'s contents were not read (not in the registry); Section 6 says so.

## Review dispositions

R1 design (1C/5I/9M), R2 build (0C/8I/11M), R3 translator (1C/8I/6M); every finding folded unless noted.

- **R1-1 (C)** the wsi C++ is compiled by MacNeutron's guest proxy: Section 5, Task 1, Q3, Ruling 4; verified.
- **R1-2** two d3d12 headers are MacNeutron's: Headers table; the header question is dropped (a default).
- **R1-3** C5, not C4; C4 re-homes the observer: Section 6, Ruling 7, Q2.
- **R1-4, R2-1, R2-2** `objects :` is refused: Task 0's table and Ruling 3. R2's `sources` + `link_with` form is
  the reserve; the generated empty source is taken because it is one form for all eight targets,
  `winemetal.so` included, and keeps the archive's place before `winemac.so`.
- **R1-5** `BUILD_DIR`: Global Constraints and Commands.
- **R1-6, R2-11** generated layout and prototype asserts for Task 4.
- **R1-7, R2-7, R2-8** the cross cases and the semantics checklist of Task 4.
- **R1-8** `C-unwind` and `AbortOnPanic`: Task 4, Rules check.
- **R1-9, R2-16** the SM50 asserts come from the existing `sm50-abi.h` seam.
- **R1-10, R2-13** two crates, not three; why a module and not a macro arm (Task 2).
- **R1-11, R2-5, R2-6** lanes: the native build in Task 2, the i386 guest scripts, forwarding and the probe in
  Tasks 2-4 and 8.
- **R1-12..15, R2-18, R2-19** counts, `testTexture.png`, the files Task 9 names, Ruling 2's cost, the removed
  meson option, the push named in Q1.
- **R2-3, R2-4** the generator's slot scan and the Windows-only module with its own test seam: Task 3.
- **R2-9, R2-10** `extern "C"` guards; `win32.sh` links the dylib: Task 4.
- **R2-12** `_fltused` moves from `.bss` to `.rdata`; records from the build folders: Task 2.
- **R2-14, R2-15** the native link driver and `__objc_imageinfo`: Ruling 3 and Task 2 (the `c_args` stay, so the
  section stays).
- **R2-17** Ruling 5's reason.
- **R3-1 (C)** `copy_fast_math_flags` cannot go through the C API (set ORs): the table, Task 5 Step 2; verified.
- **R3-2, R3-3, R3-4** the buckets, a "used by" column, the pipeline printer and the `optnone` gate of
  `LLVMRunPasses`: Section 1's table and Task 5 Step 3.
- **R3-5, R3-6** the proof is the 13 in-repo corpora with `cmp -r` and the digest, plus a mutation RED; the
  air-oracle corpora (LLVM 15, game captures) are not used: Global Constraints, Task 5 Step 1.
- **R3-7, R3-15** own LLVM folders, three installs, the fork branch pushed before the Mac build, the Mac build
  before the native one: Global Constraints, Task 5 Steps 4-5.
- **R3-8** an additive component and a parsing callback instead of `PassRegistry.def`; `llvm_deps` and
  `build.rs`: Task 5 Steps 3-4.
- **R3-9, R3-10** T-min and T-A added, T-fork's relocation said plainly, T-max dropped: Section 1, Q1, Ruling 9.
- **R3-11..14** 21 functions; the lasting attribute test; bucket A's conditions; Ruling 1's cost.

## Execution notes (2026-11-16)

Tasks 0, 2, 3, 4 and, for them, 8 and 9 are done; Tasks 1, 5, 6 and 7 wait for the answers to Q1-Q4.

- DXMTRS main 40c76ab1: 26a2709b (Task 2), 2c079856 (Task 3), ae870981 (Task 4), 920ec972 (the audit's list of the
  C-family sources left, with reasons; docs), 40c76ab1 (the gate's test leg runs `winemetal-pe`). MacNeutron: the key
  function and its test, the cross test, this pin.
- Task 2 as built: not `src/util/src/pe_rt.rs` but a `@pe_rt` arm of `dxmt_dll!`, named by each image's root crate (a
  module in the library would not reach the five end-to-end test binaries; `tools/e2e-link.sh` assembles the two
  symbols for them). Not predicted: `_fltused` is absent from the i686 `d3d9.dll` and both guest `mtld3d.dll`
  (`--gc-sections` drops a static nothing references); accepted, nothing reads it.
- Task 3 as built: the prototypes are asserted from the `sm50-abi.h` seam (`--seam-abi ... crate::sm50`), not by
  hand-written coercions; the generator's slot scan leaves the SM50 slots to the unix-call table.
- Task 4 as built: one module, `native_win32.rs`, with the prototypes moved into `win32_native_event.h` and generated
  asserts; the native feature has a test command of its own in the gate.
- The key: `3a0b1ab5...` to `cce99ae0...` (Task 3's source text), no output change.
- Gates (Task 8) ran once on the rebased tips: all green but the smoke's `present-held-a-vcpu`, the open row of
  C3c's ledger; an A/B of the native dylib (C base, Rust tip, three alternate rounds) failed 2 of 3 on the base and 1
  of 3 on the tip.
- Final review (Opus, whole diff): no Critical; one Important, fixed in 40c76ab1 (the thunks' six tests were named in
  the gate's clippy and doc lists only, so its test leg never ran them); one Minor, the licence notices, in this pin.
- Count at the pin, outside `external/` and `vendor/`: 6 files under `src/` (Q1: 3, Q2: 1, Q3: 2) and the 11 of
  `tests/dx11` (Q4).

## Addendum (2026-11-16, evening): the maintainer's answer, Tasks 5 and 6 in detail

The maintainer answered Q1-Q4 in one sentence: "I still want it all on rust". Read per question (Ruling 13):

- **Q1 → T-min.** The only option in which the translator's logic becomes Rust. It adds C++ entry points to the LLVM
  fork (LLVM's own language, where `LLVMWriteAIRBitcode` already lives) and moves the key a second time.
- **Q2 → the landing pad now**, the only other option offered. Task 6 below replaces the plan's "60 hand-encoded
  lines": the pad is clang's own assembly for today's `.m`, kept as assembler directives.
- **Q3 → the move** (Task 1 as planned; done first, MacNeutron `23881d10`). The five files stay C++, in MacNeutron,
  beside the C++ proxy that compiles them; they carry DXMT's LGPL 2.1 and DXVK's zlib notice into an MIT tree, said
  in the folder's README and in the report.
- **Q4 → delete** (Task 7 as planned). The sentence fits "port five programs" too; delete is taken as the default
  because no gate builds them, and it is done **last** so an objection costs nothing.
- **The push of a new branch to `Mac-Neutron/llvm-project`** is not covered by a goal statement: it is asked once,
  before Task 8's Mac build (the first step that fetches the pinned LLVM commit). Nothing earlier needs it.

Order now: Task 1 → this addendum's review → Task 6 → Task 5 (A1-A6) → Task 7 → Task 8 → Task 9. One pin for
everything (Ruling 2 holds: the key moves once more, at that pin).

### Task 6 (replaces the default): `delegate_forward.m` becomes a landing pad in `delegate_forward.rs`

**Why assembly and not Rust statements:** unchanged from Section 6. A catch needs a landing pad whose frame holds
the send; rustc emits no landing pad under `panic = "abort"` and its personality never stops a foreign exception.
`core::arch::global_asm!` is the tree's existing form for code rustc cannot emit (`src/util/src/boot.rs`,
`src/d3d9/src/hookable.rs`).

**Files:** `src/d3d9/unix/src/metal/macdrv/delegate_forward.rs` (the pad, the Rust `respondsToSelector:`),
`delegate_forward/tests.rs` (one more test), `src/d3d9/unix/build.rs` (the cc-rs block and `DELEGATE_FORWARD` go),
`src/d3d9/unix/Cargo.toml` (`cc` leaves the build dependencies if nothing else there uses it; `Cargo.lock`
follows), `delegate_forward.m` deleted, `tools/audit.sh` (its entry and reason), `docs/CONVENTIONS.md:65` ("The
Unix dylib's other C-family object" sentence) and `docs/DEVELOPMENT.md:143-145`.

- [ ] **Step 1 (the reference, before any edit):** `clang -target arm64-apple-macos<the deployment target cc-rs
  passes> -O2 -fobjc-exceptions -fno-objc-arc -S` and `-c` of today's `.m`, kept under `build/crs/s/df/`: the
  assembly, `llvm-objdump -d` of `_mtld3d_forward_screen_parameters`, `llvm-objdump --unwind-info`, the bytes of
  `__TEXT,__gcc_except_tab`, the relocations. The same three records from the object cargo's build script produced
  for the base (`libmtld3d_delegate_forward.a` in the base's `OUT_DIR`), to learn the flags cc-rs really used.
- [ ] **Step 2 (the pad):** `global_asm!` under `#[cfg(all(target_vendor = "apple", target_arch = "aarch64",
  not(mtl_facade)))]` defining `_mtld3d_forward_screen_parameters` as clang emitted it: the prologue with its
  `.cfi_*` lines, `.cfi_personality 155, ___objc_personality_v0`, `.cfi_lsda 16, <table>`, the send through
  `"_objc_msgSend$applicationDidChangeScreenParameters:"` (the linker's selector stub, as clang's object uses),
  `_objc_begin_catch`, `_objc_retain`, `_objc_end_catch`, the cleanup pad into `__Unwind_Resume`, and the
  exception table in `__TEXT,__gcc_except_tab` written with the assembler's `.uleb128` label differences and the
  `_OBJC_EHTYPE_id@GOT` type entry. Nothing is encoded by hand: every number in the table is a label difference
  the assembler computes. A doc block above it says what each part is and that the reference is
  `clang -S` of the deleted `.m` (its text quoted in the block, so the pad can be re-derived). The only apple
  target any script builds is aarch64 (`tools/rust-check.sh:72`, `tools/cargo-staticlib.sh`); another apple
  architecture gets a `compile_error!` naming this block, not a silent missing symbol.
- [ ] **Step 3 (`respondsToSelector:` in Rust):** `delegate.respondsToSelector(Sel::register(c"applicationDid
  ChangeScreenParameters:"))`, the form `src/d3d9/unix/src/hud_state.rs:387` already uses (typed method, no
  `sel!`). What is lost: the `.m` checked the selector against `NSApplicationDelegate` at compile time. What
  replaces it: a test that a class implementing the method through objc2's typed `define_class!` answers true and
  a plain `NSObject` answers false (the first exists; the second is added).
- [ ] **Step 4 (tests, the three that exist are the gate):** a delegate that raises comes back with its name and
  reason; one that returns answers `Ok`; a receiver without the method comes back as the runtime's
  unrecognized-selector exception. They run in `tools/rust-check.sh`'s test leg (`tst -p mtld3d-unix`, line 330;
  the log is checked for the three names, each once). **Mutation RED:** with the first call-site entry's landing
  pad label replaced by 0 the throwing test must abort the test binary (run by hand, not committed).
- [ ] **Step 5 (the records):** from the object rustc emits for the module (`--emit obj` of the crate, or the
  member of `libmtld3d_unix.rlib`): the function's disassembly, unwind info and `__gcc_except_tab` bytes equal
  Step 1's. From the dev build's `mtld3d.so` at the tip (Task 8): `nm` shows the symbol's code, `llvm-objdump
  --unwind-info` shows its entry with an LSDA and personality `___objc_personality_v0`, and the base's shows the
  same. **Stated differences:** none in behaviour; the object is assembled by rustc's LLVM, not Apple clang.
- [ ] One commit (`d3d9: the delegate forward's Objective-C is a landing pad in Rust`), chained on rust-check.
  Does not move the key.

### Task 5 as T-min

**The split:** the fork gains **generic** entry points, each a named LLVM C++ call with no translator policy in
it; every decision the shim or a pass makes becomes Rust.

**A1. References, before any edit (plan Step 1).** A copy of `build/c3/xlate.sh` as `build/crs/s/xlate2.sh`
(`ROOT` the `c-rust-mn` worktree, the LLVM prefix an argument, outputs under `build/crs/x`). Against the shared
**no-LTO** install (`build/wine-arm64-src/llvm23-arm64-nolto`, read only): the 13 corpora at the DXMTRS base, their
output folders kept; the `dxso_identity` digest and program count; `pipeline_text(0)` and `(1)`. **Mutation RED**
in a scratch worktree (never committed): the corpora with `Lower16BitTexReadPass` left out of `BuildPipeline`, and
again with `SimdgroupImplicitMemBarrierPass` left out; each must change at least one corpus output, else that
pass's proof is its unit tests alone (A2) and the ledger says so.

**A2. Unit tests pinned while the shim exists** (one commit, tests only; they call today's `module::optimize`):
- 16-bit read: a function with `bitcast <4 x i32> %v to <8 x i16>` and `extractelement`s at constant indices 0, 1
  and 7, one with a variable index (the cast must stay), one cast with no extract; the printed function pinned
  (the `.lshr16` and `.trunc_to_i16` names, the erased casts).
- Barrier: with `!air.kernel`, an address-space-3 store followed by two loads (one barrier, after the store); a
  load whose definition is a call (none); the same module without `!air.kernel` (none); a store that ends its
  block... cannot (a terminator follows), so the `getNextNode` branch is pinned by the store-then-load case only.
- Pipeline: the two existing pins (`module/tests.rs:313-358`) stay as they are and must still pass after A5 with
  **no edit of their expected text**.
- Bucket A: for `memory(...)` in its four location masks and four `ModRef`s, `captures(none)`, the SDK version
  flag, fast-math get on an FP and a non-FP value: the shim's answer equals the C-API form's, and the printed
  form is pinned.

**A3. Bucket A (plan Step 2, no LLVM change), one commit:** `get_fast_math_flags`, `is_fp_math_operator`
(`LLVMCanValueUseFastMathFlags`, then `LLVMGetFastMathFlags`), `memory_attribute`, `captures_none`
(`LLVMCreateEnumAttribute` with this LLVM's encoding, the printed-form test guarding it), `set_sdk_version`
(`LLVMAddModuleFlag` + `LLVMConstDataArray`), `declare_simdgroup_barrier` (`LLVMGetNamedFunction`, else
`LLVMAddFunction` and three enum attributes at the function index). The six leave `shim.cpp` and `sys.rs`.

**A4. The fork (plan Step 3).** A worktree of `build/forks/llvm-project` at `build/crs/llvm-dev`, branch
`air-23-extras` from `air-23` (`edc723f40d`). New, additive: `llvm/include/llvm-c/AIRExtras.h`,
`llvm/lib/AIRExtras/{CMakeLists.txt,AIRExtrasC.cpp}` (component `LLVMAIRExtras`; `LINK_COMPONENTS` Analysis,
BitReader, Core, Linker, Passes, Support, TransformUtils), and one `add_subdirectory(AIRExtras)` line in
`llvm/lib/CMakeLists.txt` with the fork's "Modified by Mac-Neutron" note. The LLVM licence header, as
`AIRWriterC.cpp` carries it. Entry points (C, `extern "C"`, no exceptions, none aborts on a bad argument it can
test for):

| Entry point | The C++ it is | Replaces |
| --- | --- | --- |
| `LLVMAIRBuilderSetFastMathFlags(B, LLVMFastMathFlags)` | `IRBuilder::setFastMathFlags` | `set_fast_math_flags` |
| `LLVMAIRCopyFastMathFlags(Inst, LLVMFastMathFlags)` | `Instruction::copyFastMathFlags` (replaces; a non-FP value is left alone) | `copy_fast_math_flags` |
| `LLVMAIRParseBitcode(C, Data, Size, Name, NameLen, LayoutCB, Ctx, *Out, *Error, *ErrorLen)` | `parseBitcodeFile` with `ParserCallbacks::DataLayout`; `LayoutCB(Ctx, Triple, TripleLen, Layout, LayoutLen, *OutLen)` answers the layout to use (bytes the callee owns until its next call; copied at once) or null to keep | `parse_bitcode`; the `i8:32` filter is Rust |
| `LLVMAIRLinkModules(Dest, Src, Flags)` | `Linker::linkModules`; `Flags` are `Linker::Flags`, static-asserted | `link_only_needed` |
| `LLVMAIREraseNamedMetadata(M, Name, NameLen)` | `getNamedMetadata` + `eraseNamedMetadata` | same |
| `LLVMAIRDestroyConstant(C)` | `Constant::destroyConstant` | same |
| `LLVMAIRGetIdentifiedStructTypes(M, *Out, Capacity)` → count | `Module::getIdentifiedStructTypes` | `rename_struct`'s listing |
| `LLVMAIRStructSetName(Ty, Name, NameLen)` | `StructType::setName` | `rename_struct`'s rename; the prefix rule is Rust |
| `LLVMAIRConstantExprAsInstruction(Expr, InsertBefore)` | `ConstantExpr::getAsInstruction` + `insertBefore` | `get_as_instruction` |
| `LLVMAIRSplitBasicBlock(BB, Before)` | `BasicBlock::splitBasicBlock` | `split_block` |
| `LLVMAIRSplitBlockAndInsertIfThen(Cond, Before, Unreachable)` | the `BasicBlockUtils` function | same |
| `LLVMAIRBuildAtomicRMWValue(B, Op, Loaded, Val)` | `buildAtomicRMWValue`, the C enum mapped as `Core.cpp` maps it; null for an operation the mapping lacks | same; which operations the translator accepts is Rust |
| `LLVMAIRCreateSlotTracker(Fn)`, `LLVMAIRGetLocalSlot(T, V)`, `LLVMAIRDisposeSlotTracker(T)` | `ModuleSlotTracker` + `incorporateFunction`, `getLocalSlot` | `local_slot`; the last-match scan is Rust |
| `LLVMAIRRunPasses(M, Pipeline, Passes, NumPasses, *Error)` | the shim's managers and `PassBuilder` (no `StandardInstrumentations`), `registerPipelineParsingCallback` for each `{Name, Run, Ctx}` as a function pass, `parsePassPipeline`, `MPM.run` | `run_passes` |
| `LLVMAIRPrintPipeline(Pipeline, Passes, NumPasses, *Out, *OutLen)` | the same parse, then `printPipeline` (a callback pass prints its name) | `pipeline_text` |
| `LLVMAIRGetMemoryDefInstruction(AM, Fn, Inst)` | `AM.getResult<MemorySSAAnalysis>(Fn)`, `getMemoryAccess(Inst)->getDefiningAccess()`; when that is a `MemoryDef`, its instruction; else null | the barrier pass's one analysis query |

A pass callback is `LLVMBool Run(void *Ctx, LLVMValueRef Fn, LLVMAIRFunctionAnalysesRef AM)`: non-zero is
`PreservedAnalyses::none()`, zero `all()`. The callback pass's class prints the name it was registered under.
Fork tests: none added to LLVM's lit suite (the install builds without tests); the entry points are exercised by
airconv's unit tests and the corpora. The fork branch is one commit.

**A5. The port (one DXMTRS commit a group, each chained on rust-check with `AIRCONV_LLVM` on the new install):**
1. `tools/llvm.sh`'s `LLVM_AIR_COMMIT` and `meson.build`'s `llvm_deps` (`-lLLVMAIRExtras`), `sys.rs` declarations
   for bucket B (the 1:1 renames), `shim.cpp` loses them.
2. Policy to Rust: the `i8:32` layout filter (a callback), the `dx.types.` rename (list, prefix test, set name),
   the probe's last-matching-slot scan, the accepted atomic operations.
3. The pipeline: its text is a constant in `module.rs`, equal to A1's `pipeline_text(1)` with the two passes
   named `air::Lower16BitTexReadPass` and `air::SimdgroupImplicitMemBarrierPass` (so both pins pass unedited);
   `optimize` runs it through `LLVMAIRRunPasses`. A parse error cannot happen for the pinned constant; it is
   returned as `LLVM error: ...` where `optimize`'s callers already return `Result`, never a panic.
4. The two passes in Rust over the C API, each a function taking the function value (and, for the barrier, the
   analyses handle): the same walks in the same order (blocks, instructions, `LLVMGetFirstUse` order for the
   cast's users), the builder positioned as `IRBuilder<>(Instruction*)` positions it (`LLVMPositionBuilderBefore`,
   which takes the instruction's debug location as the C++ constructor does), the same value names, erasure
   after the walk and only of instructions left without uses.
   **Stated difference (one):** a load whose defining access is the function's live-on-entry definition. The C++
   passes that definition's null instruction to `dyn_cast`, which dereferences it (a crash); the Rust skips the
   load. A test pins the skip; the corpora never reach it (they would have crashed).
5. `shim.cpp`, `transforms/`, the cc-rs block of `build.rs`, `cc` in airconv's build dependencies,
   `airconv_shim_probe` and its test go; `build.rs` keeps the Metal helpers and the link lines; its
   `rerun-if-changed` loses `shim` and `transforms`.

**A6. Proof (plan Step 4):** rust-check on the new no-LTO install; A2's tests pass unedited; the 13 corpora
`cmp -r`-equal to A1 and the digest equal, the tip built against `build/crs/llvm23-arm64-nolto`. LLVM is built
there by `tools/llvm.sh`'s recipe from a **second**, detached worktree at exactly the fork commit
(`build/crs/llvm-pin`): `build_llvm` deletes a source folder whose HEAD differs, so it never sees the worktree
being edited. The key's new value is recorded (`translator_key` hashes every non-test file under `src/airconv`,
so the new Rust files are in it and the deleted C++ is out).

**The shared installs (changes Step 5):** the plan's condition for rebuilding them (C4b-3d5 landed) is stale: the
session that started this one gates C4c on `build/wine-arm64-src/llvm23-arm64{,-nolto}` now, and `build_llvm`
deletes an install whose commit differs, so two sessions on two LLVM commits would rebuild against each other at
every job. Before Task 8 that session gets one message naming the moment; Task 8's job rebuilds both shared
installs once, the push follows the gates directly, and that session rebases onto the new pin before its next
build.

**Rules check (addendum):** new files `src/airconv/src/module/passes.rs` (or two files, the implementer's choice
by size; no `mod.rs`), their tests; no new crate, no new dependency (`cc` leaves two crates); no new static; the
two pass callbacks are `extern "C"` (a Rust panic in one aborts, as in every airconv export); `global_asm!` is
used once more (Task 6), under the argument above; no `sel!`, `class!` or `msg_send!`; no lint suppression.
`tools/audit.sh`'s `C_SOURCES` ends empty and the rule reads "no C, C++ or Objective-C source under `src/` or
`tests/`".

**Rulings (addendum):**

13. **"All on rust" is read per question as above.** Cost if wrong: Q4 (a deletion, last, reversible from
    history and upstream); Q3 (five files in the other repository, one commit each way); Q2 (an assembly block
    where the maintainer might have preferred to wait for C4: one revert); Q1 (the largest: an LLVM pin, three
    install rebuilds, a second key move, about four agent-days).
14. **The fork's entry points are generic LLVM calls; nothing translator-specific is C++.** Cost if wrong: an
    entry point that turns out to need policy grows a parameter, at the price of an LLVM commit and rebuilds.
15. **The pipeline is the printed text parsed back, not a hand-built pass list.** Cost if wrong: a pass whose
    printed options do not round-trip would run with different options; guarded by the print-equals-text pin
    and the corpora.
16. **The landing pad is clang's assembly, aarch64 only.** Cost if wrong: an x86_64 Mac build of `mtld3d.so`
    (none exists) stops at a `compile_error!` and needs its own block.
17. **The live-on-entry crash is not reproduced.** Cost if wrong: none found; a shader that reached it never
    produced output.
18. **The fork push is asked for, once, before Task 8.** Cost if wrong: an hour's wait at the gates; the
    alternative is a push to a public repository on an inferred yes.
