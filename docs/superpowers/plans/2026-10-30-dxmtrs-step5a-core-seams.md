# DXMTRS step 5a: the core's crates and the shared seams — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the core gets its Rust crates and is linked into `d3d11.dll` and `d3d12.dll` (every target), four shared
seams go live in Rust behind the unchanged C++ classes (statistics with the dump folder, the presenter with the
scalers, formats, the shader cache), and the core pieces 5b and 5c switch on (resources, D3D11's encoder and queue,
D3D12's pure helpers) are written ahead, host-tested and not linked live.

**Architecture:** Phase P makes two small commits and records the C++ goldens. S0 lays down every crate and
module of step 5 with empty bodies, links each DLL's one staticlib, generates `winemetal-sys` (winemetal's C API for
Rust callers) and moves what both front ends share into `dxmt-util`. S1-S4 then run in parallel lanes: each replaces
one seam's C++ body by Rust behind a small `extern "C"` ABI declared in the C++ header that seam already owns, so no
caller changes and the lanes' files are disjoint. W lanes write `dxmt-core`'s resources, queue and encoder and
`d3d12`'s pure helpers on a side branch that 5b and 5c start from. Every gate runs once after S1-S4, then push and pin.

**Tech Stack:** Rust 1.99.0 (staticlibs for `aarch64`, `arm64ec`, `x86_64`, `i686-pc-windows-msvc` and
`aarch64-apple-darwin`), meson, llvm-mingw, C++20 (the cuts), winemetal's C API, POSIX sh, `python3 -I` (scratch).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (approved, with the 2026-10-30 revisions: §3 layout,
§5 step 5 and its pace paragraph, §6, §7, §8). Inputs: `build/step5/map/0-porting-map.md` (main input, cited `M0`),
`1-core.md` .. `5-d3d9-needs.md` (`M1`..`M5`), `6-critic.md` (accepted), `7-decisions.md` (5-D1..5-D14, binding);
`build/step4/map/7-decisions.md` (4-D4, 4-D6: the key and the DXBCParser edge); `build/pipeline.md` (the lock).
Format and gate commands: step R's plan (`2026-10-30-dxmtrs-stepR-layout.md`, Task 9's gate block), step 3's
(`2026-10-30-dxmtrs-step3-d3d10core-dxgi.md`), `build/step2/reports/gates-{mac,native,vm}.md` and, once they exist,
step R's and step 3's gate reports.

**Rebase note.** Written ahead on 2026-10-30 against DXMTRS da6ed2d (C++) and step R's layout as `stepR-layout`
had it while this plan was written (c0e74b8, then 7573a25, dcf027f at the review: `src/d3d9`, `src/util` = `dxmt-util`,
`src/winemetal/{wire,unix}`, root `Cargo.toml`; `tools/rust-check.sh` and the root-workspace `tools/audit.sh` land with
step R's Tasks 6-7), with step 3's plan (`src/dxgi`, `src/d3d10`, dxgi's `config.rs` and `abi.rs`) assumed landed. It
runs after both are on DXMTRS `main` and MacNeutron `dxmtrs` carries step R's pin bump; Task 0 Step 1 records the head
actually rebased onto, re-reads every path and line number named here (DXMTRS and MacNeutron) and ledgers each that
moved.

Paths: `<M>` = `~/Documents/MacProton`, `$B` = `<M>/build`, `$D` = `$B/wine-arm64-src/dxmt` (the build's
DXMTRS checkout), `$S` = `$B/step5a`, `$W` = `$B/eac-vm-work`, `$A` = `$B/wine-arm64/wine.app/Contents/Resources`,
`$R` = `~/.rustup/toolchains`. Commands run from `<M>` unless a step says otherwise. Ledger:
`.superpowers/sdd/2026-10-30-dxmtrs-step5a-core-seams/progress.md`.

## Decisions

Binding (`build/step5/map/7-decisions.md`), short form:

- **5-D1** Three plans: 5a (this: phase P, S0, S1-S4, the W lanes), 5b (D3D11 track, one switch of `d3d11.dll`), 5c
  (D3D12 track, one switch of `d3d12.dll`; frame generation moves into the core there). 5b and 5c run in parallel
  after 5a's seams.
- **5-D2** S5 dropped: the queue's sequencing goes live with D3D11's switch (written here in lane E).
- **5-D3** P2 dropped: natively the dummy cbuffer keeps getting null from `aligned_alloc(65536, 4096)`.
- **5-D4** The D3D12 heaps' asserts become `debug_assert!` (shipped behaviour since `b_ndebug=if-release`); a real
  check is the maintainer's own decision.
- **5-D5** The drawable prefetch (map P1) stays off; turning it on is its own decision with its own measurement.
- **5-D6** D3D11's switch rides a pin that moves the key anyway (step 4's). After step 4's switch the key hashes
  airconv's resolved dependency set and the toolchain, not the root `Cargo.lock` (4-D4), so step 5's crates never
  move players' caches. 5b/5c take the DXBC parser and root-signature deserializer from step 4's `airconv-ir`.
- **5-D7** Forwarder headers stay at their paths; spec §6's list corrected (add `d3d12_shared_fence.hpp` and the util
  WSI files; `d3d12_common.hpp` is MacNeutron's).
- **5-D8** Natively, `win32_event.c` exports the Win32 calls the Rust front ends need (CreateSemaphore,
  ReleaseSemaphore, DuplicateHandle, WaitForSingleObject, OpenThread, as today's inline C++ does).
- **5-D9** The i386 `d3d12.dll` is built, staged and gated; every check covers it.
- **5-D10** Per task only its own checks; full gates three times in step 5: after 5a's shared seams (here), at each
  switch.
- **5-D11** Spec amendments (step 5's order, §6's list, 5-D6's key rule) are written into the spec when 5a lands.
- **5-D12** The milestone D3D11 bench is asked before 5b's switch gates; 5a runs no game.
- **5-D13** The core's Rust API is shaped for D3D11, D3D12 and D3D9 per `5-d3d9-needs.md`; D3D9's binding fork is
  decided before C3.
- **5-D14** Three adversarial Opus plan reviewers.

Plan rulings (each: decision — why — cost if wrong):

- **P1 Branches.** DXMTRS `step5a` from `main` (R and 3 landed); S lanes `step5a-<lane>`. W lanes branch from S0's
  head into the integration branch `step5w`, which never reaches `main` in 5a: 5b and 5c start from it and land its
  code with the switch that uses it. — The gate covers only live code; no dead Rust ships. — `step5w` is rebased onto
  5a's pushed `main` once (files disjoint from S's but `Cargo.lock`).
- **P2 No new C header.** Each seam's `extern "C"` declarations go into the C++ header that seam already owns
  (`d3d12_stats.hpp`, `d3d12_dxil_dump.hpp`, `dxmt_presenter.hpp`, `dxmt_scaler.hpp`, `dxmt_format.hpp`,
  `dxmt_shader_cache.hpp`); names `dxmt_<seam>_<verb>`; the C++ functions and classes stay as thin forwards, so no
  caller changes. Each header's block sits between `/* dxmt-abi begin */` and `/* dxmt-abi end */` lines and uses C
  types only (`winemetal.h`'s, `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, and pointers to `struct X` for the
  header's own structs, `DXMTPresentMetadata`, `DXMTGammaRamp`, `MTL_DXGI_FORMAT_DESC`, which the block
  forward-declares or, for `dxmt_format_capability`, defines); every Rust export is pinned to it on all
  five targets by the lane's generated `src/dxmt/src/<seam>/abi.rs` (`const _: unsafe extern "C" fn(..) =
  crate::<seam>::dxmt_..;`, Task 3's `--seam-abi` mode, P12): the two sides are compiled apart and bind by name only,
  so a width mismatch (`WMTColorSpace` is `enum : uint64_t`, `winemetal.h:1906`) links cleanly and corrupts only i686's
  stack arguments. — Lanes stay file-disjoint without a shared header (critic 1.9). — Declarations live in six
  headers until each switch deletes them.
- **P3 Where the exports live.** The `#[unsafe(no_mangle)] extern "C"` functions (`"C-unwind"` natively, P19) are in
  `dxmt-core`; `d3d11` and `d3d12` (the DLLs' staticlibs) hold `use dxmt_core as _;`, which is what keeps them in the
  archive (verified 2026-10-30 on darwin, i686 and arm64ec: without it `nm` finds none). Each DLL carries its own copy,
  as today's `libdxmt.a`. — One staticlib per binary (spec §3). — none.
- **P4 Handles across the ABI.** A returned `obj_handle_t` named "+1" is retained and adopted by `WMT::Reference`'s
  `obj_handle_t` constructor (`Metal.hpp:80`); inputs are borrowed; Rust objects behind `void *` are owned by the C++
  wrapper and freed by its destructor (`*_destroy`); no reference count crosses (the C++ `RcObject`/`incRef` counts
  stay C++). Callbacks are a function pointer plus `void *ctx`, called only during the call that received them; the
  C++ trampolines over `std::function`s are `noexcept`, so a C++ throw terminates in C++ and never unwinds into Rust.
  No Rust-owned data symbol is referenced from C++ (ARM64EC C++ binds functions by their EC-mangled names, which only
  the arm64ec archive defines, but data by the plain name, which the aarch64 archive on the same link line defines
  too): flags cross by value. — Smallest contract; callers unchanged. — none.
- **P5 Logging.** Core Rust logs through `dxmt_core::log::{err,warn,info,debug,trace}`, which call the C symbol
  `dxmt_log(level: u32, msg: *const u8, len: usize)` resolved at link time; each front-end crate defines it at its
  switch over its own `dxmt_util` `Logger` (one writer per file). No S seam logs (grep of `dxmt_presenter.cpp`,
  `dxmt_scaler.cpp`, `dxmt_format.cpp`, `dxmt_shader_cache.cpp`, `d3d12_stats.cpp`, `d3d12_dxil_dump.cpp`: none), so
  5a defines `dxmt_log` nowhere; host tests define a capturing one under `cfg(test)`. With `perf-build`, `info`,
  `debug` and `trace` compile to nothing. — No runtime registration, no static. — none.
- **P6 Configuration.** `config.cpp` stays the C++ callers' reader until each switch. S0c promotes step 3's dxgi
  reader into `dxmt_util::config` with the typed parsers the front ends need and the drift test against
  `config.cpp`; no C ABI cut, since 5a has no Rust caller. — C2 may cut earlier; nobody needs it now. — two readers
  until the switches (drift-tested).
- **P7 Shader-cache key.** `ShaderCacheVersion()` stays C++ (it reads three compile-time inputs: the generated
  `DXMT_TRANSLATOR_KEY`, `version.h`'s `DXMT_VERSION`, `AIRCONV_VERSION`); the Rust store takes the `u64`. Its Rust
  form and the meson-to-cargo plumbing come with the first switch (5c). — Critic 1.9's plumbing for a function with
  one caller. — one C++ function left behind.
- **P8 Presenter prefetch.** Per 5-D5 the Rust presenter acquires the drawable where every build does today (inside
  `encode`); the never-compiled `#ifdef DXMT_NATIVE` block (`dxmt_presenter.hpp:78-94`, `.cpp:35,48,60,145,157,224,
  259`) goes with the C++ body it sits in. A later turn-on (5-D5) or C4 adds an acquisition field to `PresenterDesc`.
  The memory note `framegen-adaptive.md:31` (believes the VM host prefetches) is corrected by the controller. — It has
  never run. — re-implementing it from d89729b if wanted.
- **P9 Goldens.** Recorded from the C++ before the code they pin changes, each recorded twice and equal, each
  comparator with one mutation RED: the `stats.txt` counters and timed-line shapes (S1), `wmtcmd` record histograms
  (S2's encode-equality row; 5b/5c reuse them; they count record types, not values), the presenter's calls with their
  arguments over a scenario matrix (S2: HDR metadata, function constants, PSO choice, viewport, layer props), the
  spatial scaler's run (S2: no gate sets its variable), the format tables per GPU family (S3), the shader cache's
  open order and paths over an environment matrix (S4), the `LegacyBarriers` truth table (D1), and root-signature
  blobs, footprints, allocation info and feature answers from the native C++ `d3d12.dylib` (D2, D3); the Mac, guest,
  native and VM-host references of Task 1 are themselves gated (check.sh on `$S/ref-wine.app`). Map P5's D3D11 API
  probe and cold-cache AIR go to 5b, recorded then from the C++ they replace. — Each golden is needed where it is
  recorded. — 5b's references include 5a's Rust seams, gated equal by then.
- **P10 Histogram knob.** `DXMT_WMTCMD_HISTOGRAM=<file>` (winemetal's Rust Unix side, Mac and native): per
  `MTLCommandBuffer_commit`, one appended line per (encoder kind, record type) counted since the last commit;
  `$S/hist-compare.sh` sums per run. Unset: one latched flag test per `*_encodeCommands` call. — Map P5 asked for it;
  command-buffer splits vary with timing, run totals do not. — a dev knob in the shipped image.
- **P11 Lane builds are host work.** `$S/lane-build.sh` configures and compiles a worktree into its own `$S/lb/…`
  folder with MacNeutron's flags (never installs, never reads installed outputs or `$D`): no lock (pipeline rule 4's
  "own dirs"). During the gate's timed rows (Task 20) no lane builds or tests. — Four S lanes otherwise queue on the
  lock for compiles. — CPU contention outside timed rows.
- **P12 Bindings.** `tools/winemetal-layouts.sh --decls` prints `src/winemetal/sys/src/decls.rs`: exactly `--protos`'s
  function set (the PE AST, which `--protos` reads with `-DDXMT_GUEST_VM`, so the guest block's `WMTMemoryAlloc`,
  `WMTMemoryFree` and `WMTForward` are in it; an unused declaration links nothing and callers gate them with
  `cfg(feature = "guest-vm")`), types resolved to `winemetal-wire`'s mirrors as `--protos` does, printed as one `unsafe
  extern "C"` block under `cfg(windows)` and the same body as `unsafe extern "C-unwind"` under `cfg(not(windows))`
  (P19); every type in that set is fixed-width or a mirror, so it is valid on darwin too. The native-only functions
  (`#if defined(DXMT_NATIVE) || !defined(_WIN32)`, which Apple clang's AST has no `DLLImportAttr` for, and whose
  `WMTWsiHooks` has no mirror) are hand-declared in `lib.rs` under `cfg(not(windows))`: `WMTMemoryHooked`,
  `WMTGetWsiHooks` returning `*const c_void`, and 5-D8's `wmt_native_*` (P14), each with a prototype check against Apple
  clang's AST of `winemetal.h` and `win32_event.c` (`-DDXMT_NATIVE`), printed by the `--seam-abi` printer below for
  those names and compiled under `cfg(not(windows))`. `decls.rs` joins `rustfmt.toml`'s `ignore` (Task 2). A second
  mode, `--seam-abi <header> <module path>`, reads one header's `dxmt-abi` block (P2) into a scratch C file that
  includes `winemetal.h`, takes llvm-mingw's AST on the four PE targets and Apple clang's, and prints `const _: unsafe
  extern "C" fn(..) = <module path>::<name>;` per target `cfg`; a pointer to a struct `winemetal.h` doesn't define maps
  to `<module path>::<RustName>` (the seam's mirror, whose layout the lane's layout test asserts against the C++); it
  fails on a type it cannot map. Not `AIRCONV_API`'s `SM50*` (step 4's surface; 5b adds them if its Rust calls them). No
  `#[link]`: meson links `winemetal_dep`. The safe layer is hand-written beside it: `Obj` (owns +1, `Drop` releases) and
  `Pool` (autorelease pool guard). nvapi's and dxgi's hand declarations retire to it, but for dxgi's `WMTGetWsiHooks`,
  which keeps returning its own `abi::WsiHooks` (listed as a duplicate). — Critic 1.8, 1.9; the `DLLImportAttr` filter,
  the 64-bit-Windows `long` map and the missing `WMTWsi*` mirrors make a darwin pass of the generator wrong. — two
  generator modes and a few hand declarations.
- **P13 Native platform calls in S1.** Thread names, thread ids and local time come from kernel32 on PE (declared in
  `stats/platform.rs`) and from `libc` 0.2.190 on darwin (`proc_pidinfo`, `proc_threadinfo`, `pthread_threadid_np`,
  `localtime_r`: the calls `win32_native.h:228-233, 421-449` makes; `PROC_PIDTHREADID64INFO` = 15, which `libc` lacks,
  declared in `stats/platform.rs` from `<sys/proc_info.h>`); the thread id truncated to `DWORD` as the C++ does. —
  5-D8's list has `OpenThread` (the C++'s way to a thread's name) but not the thread-name, thread-id and local-time
  calls themselves; the darwin calls are libc's and `libc` is in the lock already. — a second thread-name
  implementation beside `win32_native.h`'s, listed for §7.
- **P14 Ported in 5a, deferred, or not ported.** Live: stats and the dump folder's file writing, presenter and
  scalers, formats, shader cache. Written ahead (W): resources, ring/staging/dynamic pools, residency mask, samplers,
  internal library and clear helpers, capture state, HUD, version text (lane R); queue, chunk arena, encoder,
  deptrack, flush and optimizer, queries, initializer, UAV counters, emulated context (lane E); `LegacyBarriers`,
  root-signature serialization and qword offsets, footprints, resource math, feature tables, samplers, pipeline-stream
  parsing (lane D). To 5b: `Device` and Metal-version choice, keyed mutex and D3DKMT shared-resource layouts,
  `BindingSet`, subresource sets, task scheduler, `dxmt_names`, `FrameStatistics`. To 5c: `Fence`/`EventListener`,
  frame generation, the recording writer (replay's C++ reader stays to step 6), the release-probe string rule, the
  native not-implemented flag (in `winemetal.dylib`, one symbol). In S0 (Task 3, critic 1.5), not at a switch:
  5-D8's native exports, because 5b's D3D11 switch (`d3d11_swapchain.cpp`'s frame-latency semaphore) and 5c's need the
  same calls and run in parallel. DXMT_STATS goes live in the core
  here (map 0's S1 and the controller's task text); 5-D1's "DXMT_STATS moving into the core at [5c's] switch" is read
  as 5c's Rust calling `dxmt_core::stats` directly and deleting S1's C ABI (finding). — Each where its first user goes
  live. — if 5-D1 meant stats to wait, Task 5 is held back and nothing else moves.
- **P15 C++ asserts** map to `debug_assert!` (critic 1.7, 5-D4); a real check only where this plan names one.
- **P16 Binary identity delta, pre-stated.** `d3d11.dll`, `d3d12.dll` (Mac ARM64EC, guest x86_64 and i686): name,
  machine, exports and ordinals, builtin marker, VERSIONINFO (`d3d11` has one, `d3d12` none) equal; imported DLL set ⊆
  reference ∪ {`ntdll.dll`, `api-ms-win-core-synch-l1-2-0.dll`, `api-ms-win-crt-private-l1-1-0.dll`}; symbols only
  added, each from Rust std's runtime (step 2's list, `gates-mac.md` §3) or a seam's own Win32 import (P13); nothing
  removed; `check-build.sh`'s requirements held. Native `d3d11.dylib`, `d3d12.dylib`: `nm -gU` equals exactly the
  `.def` names they define today (+ `MTLD3D11WaitIdle` for d3d11) through `-Wl,-exported_symbol` (step 3 D12/P9's
  form; today they export every global); `otool -L` equal; OSO stabs 0 and `$HOME` 0 (`-Wl,-S` in non-debug builds,
  as `src/nativemetal/meson.build:13`: the production profile's `debug = 1` puts DWARF in the Rust members, and ld64's
  debug map names the archive by its absolute path). `ident-diff.sh`'s ED10 (Task 0) is exactly this delta. — Step 2
  left this ruling open; not again. — a native consumer of an unlisted symbol fails the native gate, fixed in the fix
  pass.
- **P19 Unwinding natively** (spec §5 step 2, D4/P11: "a foreign Objective-C unwind passes"). On darwin the seams' Rust
  sits between the VM host's `catch (...)` and winemetal's `extern "C-unwind"` handlers, so: `winemetal-sys`'s
  declarations are `extern "C-unwind"` under `cfg(not(windows))`; the native `d3d11`/`d3d12` staticlibs build with
  `production-unwind` (as winemetal's do); the seam exports are `extern "C-unwind"` natively, each holding an
  abort-on-panic guard (winemetal's `AbortOnPanic` form) so a Rust panic still aborts and `MutexGuard`s drop on a
  foreign unwind. winemetal builds `production-unwind` for the same reason (the root `Cargo.toml`'s comment on that
  profile: its `"C-unwind"` handlers pass an Objective-C exception to the caller's `@catch`). PE is unchanged (`extern
  "C"`, `production`): an Objective-C exception there stays on Wine's Unix side and never crosses a PE frame. — Keeps
  the approved D4 behaviour instead of making a Metal exception abort the VM host. — the native staticlibs carry landing
  pads; a second `AbortOnPanic` (listed as a duplicate).
- **P17 Features.** `dxmt-core`, `d3d11`, `d3d12`: `perf-build` (forwarding to `dxmt-util/perf-build`), set when
  `-DDXMT_PERF_BUILD` is in `cpp_args` (step 3 D9's test), and `guest-vm`, set from `get_option('guest_vm')`, both in
  the component `meson.build`s (never a root option: key input). No `native` feature: the darwin target is the native
  build. With `perf-build`, `stats` and `dump` compile to nothing (no `dxmt_stats_*`, no report strings).
- **P18 Stats writes stay single-writer.** Per-thread counters are `AtomicU64` written by their thread with Relaxed
  load/store (no read-modify-write) and read by the reporter: the C++'s racy reads without a data race, at the C++'s
  cost.

## Global Constraints

- Agents never push, merge to `main`, fetch or download. Task 22's push, pin and pinned re-fetch run under the
  maintainer's standing authorization, by the controller only. No MacNeutron.app, Steam or game; nothing under
  `~/Library/Application Support/MacNeutron` (7-decisions allows none). Kill by exact PID. No bench (5-D12). Home
  written `~`; privacy scan before every push. Commits end with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`; DXMTRS subjects `<area>: sentence`.
- **The lock (`build/pipeline.md`).** Every build or install of MacNeutron's runtime or a DXMT build into the shared
  folders, every gate or check that reads installed outputs, and every checkout in `$D` runs as ONE job:
  `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c 'git -C <M>/build/wine-arm64-src/dxmt checkout -q --detach <sha> &&
  <build> && <tests>'`, logging the commit and wine.app's cdhash; no branch, stash or worktree left in `$D`; the job
  never waits on a review. Editing only in worktrees (`git -C $D worktree add $S/wt-<lane> …`; MacNeutron: `git
  worktree add $S/wt-mn -b step5a-mn dxmtrs` with `BUILD_DIR=<M>/build`).
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE`. The builder starts with `VM_SHARES` per the builder-out-share note. VM commands
  export `MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img`.
- **Rust.** `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH`, `--offline`, `CARGO_TARGET_DIR=$S/cargo-<lane>`.
  - **UT(<crates>)** (host tests): `cargo test --offline --target aarch64-apple-darwin -p <crate>… --config
    'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'` from the worktree.
  - **RL** (the lane form of the Rust gate, step R Task 6): `sh -c 'ROOT=$PWD; . dxmt/pins; LM=$(dirname "$(sh
    dxmt/toolchain.sh)"); git -C <wt> add -A; PATH="$(sh dxmt/mtld3d-tools.sh):$LM/bin:$PATH" MTLD3D_RUST=$MTLD3D_RUST
    MTLD3D_RUST_NIGHTLY=$MTLD3D_RUST_NIGHTLY MTLD3D_LLVM_MINGW=$LM WINE_BUILD=$PWD/build/wine-arm64-src/wine-build
    GUEST_WINE_SDK=$PWD/build/eac-vm-work/guest-wine-sdk CARGO_TARGET_DIR=$PWD/build/step5a/cargo-<lane>/check sh
    <wt>/tools/rust-check.sh'` → `rust-check: ok` (fmt, clippy pedantic+nursery `-D warnings` per shipped target,
    doc, tests, audit). **RC** is the same on `$D` at the gate.
  - **LB(<kind>)**: `sh $S/lane-build.sh <wt> <mac|native|guest64|guest32> [p]` → `lane-build: <kind> ok` (P11).
- Translator key: `sh -c 'ROOT=$PWD; . dxmt/pins; . wine-arm64/lib.sh; PATH="$(sh dxmt/toolchain.sh):$PATH";
  translator_key build/wine-arm64-src/dxmt release'` (on a worktree: its path) equals Task 0's value before and
  after every commit. No edit of `include/`, the root `meson.build`/`meson.options`, `src/d3d11/d3d11_shader.cpp`,
  `src/airconv`, `libs/DXBCParser`, `src/winemetal/airconv_thunks.{c,h}`.
- Conventions (`docs/CONVENTIONS.md`, tree-wide since R): no `#[allow]`/`#[expect]`; `unsafe` one operation a block
  with `SAFETY:`; no `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; no lowercase "dxmt" in comments;
  `FxHashMap`, never std's `HashMap`; derives only with a callsite, through `tools/audit.sh --update-derives`.
- Behaviour is the C++'s wherever anything outside the DLL sees it: files and their bytes, log and stats text,
  cache paths and keys, HRESULTs, thread names (`dxmt-encode-thread`, `dxmt-finish-thread`).
- zsh: quote `"====="`; gating scans use `/usr/bin/grep`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes (rebase, fast-forward, `git cherry` empty
before a branch is deleted) and keeps the ledger. One **pipeline owner** runs every lock job (Tasks 1 Step 3, 2 Step 6,
5 Step 4, 6 Step 4, 20, 22's restore of `$D` and pinned build); while step 4 runs too, the lock serializes the two
steps' jobs. **Lanes** do host work only (UT, RL, LB, editors). Reviews on Opus.

**Lane procedure.** `git -C $D worktree add $S/wt-<lane> -b step5a-<lane> <base>`; fill `include/native/directx` and
`external/nvapi` read-only from the main checkout's submodules (`git -C $D/<path> archive HEAD | tar -x -C
$S/wt-<lane>/<path>`, `chmod -R a-w`; `chmod -R u+w` before removal). RED (the task's tests), GREEN, the task's checks,
commit. Lanes never touch another lane's files, `Cargo.lock`'s other entries, `docs/CONVENTIONS.md`, `tools/audit.sh`,
`tools/rust-check.sh` or `rustfmt.toml` (S0's), a golden (Task 0/1's); a needed change goes to the controller.

| Task | Runs | Depends on | Owns (disjoint) | Lock |
|---|---|---|---|---|
| 0 base, kit, host goldens | owner (Sonnet) | R, 3 on `main` | `$S/*`; golden files | no |
| 1 phase P + pipeline goldens | owner (Sonnet) | 0 | `src/d3d12/d3d12_command_encoder.hpp` (+users), `src/winemetal/unix/src/` histogram | Step 3 |
| 2 S0a crates, skeletons, links | owner (Opus) | 1 | root `Cargo.toml`/`.lock`, `src/dxmt/{Cargo.toml,build.rs,src/**}` (stubs), `src/d3d1{1,2}/{Cargo.toml,src/lib.rs,meson.build}`, `src/d3d12/src/**` (stubs), `src/winemetal/sys/` (stub), `src/util/src/{lib.rs,sync.rs}` (stub line), `tools/{rust-check,audit}.sh`, `rustfmt.toml`, MN notices | Step 6 |
| 3 S0b winemetal-sys | lane `sys` (Opus) | 2 | `src/winemetal/sys/**`, `tools/winemetal-layouts.sh`, `src/nativemetal/win32_event.c`, `src/nvapi/{Cargo.toml,src/ffi.rs}`, `src/dxgi/Cargo.toml`, dxgi's winemetal declarations, their `Cargo.lock` entries | no |
| 4 S0c dxmt-util moves | lane `util` (Sonnet) | 2 | `src/util/{Cargo.toml,src/**}`, `src/dxgi/src/{config.rs,abi.rs}`, `src/nvngx/src/com.rs`, `src/nvapi/src/d3d.rs`, `docs/CONVENTIONS.md` (ABI homes) | no |
| 5 S1 stats + dump | lane `s1` (Opus) | 1, 2, 3 | `src/dxmt/src/{stats,dump}{.rs,/}`, `src/d3d12/d3d12_stats.{hpp,cpp}`, `d3d12_dxil_dump.{hpp,cpp}` | Step 4 |
| 6 S2 presenter + scalers | lane `s2` (Opus) | 3, 4; merges after 7 | `src/dxmt/src/{presenter,scaler}{.rs,/}`, `src/dxmt/dxmt_{presenter,scaler}.{hpp,cpp}` | Step 4 |
| 7 S3 formats | lane `s3` (Sonnet) | 0, 3 | `src/dxmt/src/format{.rs,/}`, `src/dxmt/dxmt_format.{hpp,cpp}` | no |
| 8 S4 shader cache | lane `s4` (Sonnet) | 3 | `src/dxmt/src/shader_cache{.rs,/}`, `src/dxmt/dxmt_shader_cache.{hpp,cpp}` | no |
| 9-11 R1-R3 | lanes `r1`-`r3` on `step5w` (Opus, Opus, Sonnet) | 3; R2, R3 after R1's types | `src/dxmt/src/{resource,ring,staging,dynamic,residency,sampler,library,capture,hud,info}…` | no |
| 12-15 E1-E4 | lanes `e1`-`e4` on `step5w` (Opus) | E1: 3; E2: R1; E3, E4: E2 and Task 6 merged | `src/util/src/sync{.rs,/}`, `src/dxmt/src/{queue,arena,encoder,deptrack,query,initializer,counter,emulated}…`, wire's reflection mirror | no |
| 16-19 D1-D4 | lanes `d1`-`d4` on `step5w` (Sonnet; D2 Opus) | 2 (D3: 7 merged) | `src/d3d12/src/{barrier,root_signature,resource_math,feature,sampler,pipeline_stream}…`, `src/d3d12/src/abi/<own>.rs` | no |
| 20 gates, once | owner (Sonnet) | 5-8 merged | — | yes (each job checks `<M>` out detached at `step5a-mn` first, `dxmtrs` last) |
| 21 one fix pass | owner, lanes (Opus) | 20 | as needed | as needed |
| 22 spec, push, pin, W hand-off | controller | 21 | spec, pins, ledgers | Steps 2, 4 |

**What starts before its dependencies land.** Task 0 starts as soon as R and 3 are on `main` (its harnesses read C++ R
and 3 don't touch). Lanes `s1`-`s4` branch after Task 3's merge (its `--seam-abi` mode, P2); `s3`, `s4` and D1-D4 need
only S0a's stubs and S0b's declarations, not each other. `s2` codes against S0a's stub signature of
`format::forget_srgb` and rebases onto S3's merge before its checks. E1 codes against S0a's empty `Encoder`; E2 against
R1's types once R1's first commit lands. `step5w` is created at Task 2's merge (D1-D4 may branch then) and
fast-forwarded past Tasks 3 and 4 before any R or E lane branches. Nothing in 5a waits for step 4 (P7; no key input
touched). Merge order on `step5a`: 1, 2, 3, 4, then 7, 5, 8, 6. `step5w` merges W lanes as they finish; it is rebased
past S3's merge before D3 starts, past S2's (Task 6) before E3 and E4 branch (E3 presents and upscales through S2's
`Presenter` and scalers, which are Task 2's stubs until then), and onto `main` after Task 22. Step 4's lanes also edit
the root `Cargo.toml`/`Cargo.lock`, `tools/rust-check.sh` and `tools/audit.sh`: whichever lands on `main` second rebases
with a mechanical merge of those lists (no version moves: `cargo metadata --locked` and `cargo tree` of the shipped
packages checked after it).

## Rules check

- **Statics** (each a process-wide fact, as the C++'s):
  - Stats `ON: AtomicBool`, private (no exported data symbol, P4): set once by `dxmt_stats_init(on)` from the C++
    load-time object, which keeps `g_stats_on` (`d3d12_stats.cpp:23`; the C++ macros are its only reader).
  - Stats registry `LazyLock<Mutex<Registry>>` (names, thread list, clocks, report text) and its `thread_local!`
    per-thread counters (leaked, as `new ThreadStats` is); `PASS: AtomicU32` (`g_pass`): the C++'s globals,
    `d3d12_stats.cpp:52-58, 218`.
  - Dump folder `LazyLock<Option<PathBuf>>` and the pipelines-file `Mutex<()>`: the function-local statics of
    `CaptureFolder` and `LogPipeline` (`d3d12_dxil_dump.cpp`).
  - Shader-cache stores: none in Rust. Each `Store` is a `Box` created and destroyed through the C ABI by the
    unchanged C++ `getInstance` map (`dxmt_shader_cache.cpp:23-24`), so its SQLite connections close at
    DLL_PROCESS_DETACH or exit as today (the last close checkpoints the WAL and removes `-wal`/`-shm`).
  - `DXMT_WMTCMD_HISTOGRAM`'s latched flag and file in winemetal (P10).
  - Test-only: capturing `dxmt_log`, `NSObject_retain`/`_release` counters, the `CacheReader`/`CacheWriter` call-order
    fakes (S4) and the presenter's winemetal fakes (S2) in `cfg(test)`, each a snake_case function under
    `#[unsafe(export_name = "…")]`. No `OnceLock`.
- **Config keys:** none new. Read by Rust in 5a: `DXMT_DXIL_DUMP`, `DXMT_SHADER_CACHE`, `DXMT_SHADER_CACHE_PATH`,
  `DXMT_CACHE_EXE` (all existing; `DXMT_STATS` stays the C++'s, passed by value). New dev knob: `DXMT_WMTCMD_HISTOGRAM`
  (P10). New build-time variable: `DXMT_COMMAND_METALLIB` (R3, set by meson at a switch).
- **Wire fields:** none changed. New mirrors: `DXMTPresentMetadata`, `MTL_DXGI_FORMAT_DESC` (asserted against the C++
  by the lane's layout test), the reflection pair `MTL_SHADER_REFLECTION`/`MTL_SM50_SHADER_ARGUMENT` in
  `winemetal-wire` through the generator (E2), D3D12 structs in `src/d3d12/src/abi/` (D lanes, asserted per target
  as step 3 D8). `GammaRamp` moves as step 3 defines it (`[AtomicU32; 1024]` ×3 + `AtomicU64`, 12296 bytes,
  `version` at 12288, unchanged).
- **Dependencies:** new workspace packages `dxmt-core` (`src/dxmt`, rlib), `d3d11`, `d3d12` (staticlibs),
  `winemetal-sys` (`src/winemetal/sys`); all LGPL-2.1-or-later. Edges: `dxmt-core` → `winemetal-sys`,
  `winemetal-wire`, `dxmt-util`, `rustc-hash`, `bitflags`, and `libc` on darwin only (P13); `d3d11`, `d3d12` →
  `dxmt-core`, `dxmt-util`; `winemetal-sys` → `winemetal-wire`; `nvapi`, `dxgi` → `winemetal-sys` (Task 3). No crate
  new to `Cargo.lock`. Native `d3d11`/`d3d12` staticlibs on `production-unwind` (P19). New C exports in the native
  `winemetal.dylib`: 5-D8's `wmt_native_*` (Task 3). Notices: the new shipped packages join `mtld3d-notices.py`'s
  legs (Task 2).
- **Derives:** small `Copy` newtypes only (`StatId(u32)`, view keys `u64`, `Family`, capability and flag bitflags,
  `MapResult`), each with its callsite through `--update-derives`; no derive on a type over 16 bytes.
- **Lint suppressions:** none. Float conversions in the stats report go through `dxmt_util::float_cast`. Generated
  files (`src/winemetal/sys/src/decls.rs`, `src/dxmt/src/{stats,dump,presenter,scaler,format,shader_cache}/abi.rs`)
  join `rustfmt.toml`'s `ignore` beside `layouts.rs` and `protos.rs` (Task 2).
- **Duplicates (for §7):** `config.cpp` vs `dxmt_util::config` (P6, drift-tested); C++ `ShaderCacheVersion` (P7); `libc`
  thread names vs `win32_native.h` (P13); kernel32 declarations in `stats/platform.rs` vs dxgi's `abi.rs`; the
  reflection mirror vs step 4's airconv crate (whichever lands second adopts the first); `winemetal-sys`'s `Obj` vs
  D3D9's `MetalHandle` (C4/C5); `mtld3d-mtl`'s `wmt.rs` vs `winemetal-wire` (D16); the C++ forwards of P2 (deleted at
  each switch); dxgi's own `WMTGetWsiHooks` returning `abi::WsiHooks` vs `winemetal-sys`'s `*const c_void` one (P12);
  `dxmt-core`'s abort-on-panic guard vs winemetal's `AbortOnPanic` (P19). Retired: nvapi's and dxgi's hand-declared
  winemetal functions (P12), nvngx's and nvapi's private interface copies (S0c).

## Review Focus

check.sh rows are named by their `expect` labels (Task 0 Step 1 ledgers the line each sits on at the rebase): the
**cache rows** (`cache run 1 (cold) draws as D3DMetal` .. `a probed run stores no translated function`), the **clear
rows** (`d3d12_clear presents every frame` .. `a swapchain's last Release frees it (DXMT 0131)`), the **capture rows**
(`DXMT_RENDER_SCALE=0.5 halves the swapchain` .. `an unwritable capture folder changes nothing for the game`), the
**version rows** (`D3D11 opens the translation cache`, `its table is named by the build, not AIRCONV_VERSION alone`,
`another build's table is dropped when the cache opens`), the **DLSS rows** (`MACNEUTRON_DLSS=1 shows an NVIDIA adapter
and initialises NVAPI` .. `without MACNEUTRON_DLSS, nvngx.dll doesn't load and the adapter is Apple's`) and the
**fullscreen rows** (`the fullscreen test refuses to run where its own AppDefaults turn emulation off` .. `and the
restored window presents again`, with `and count as frames in DXMT_STATS (31 and the restored one)`).

1. **A 32-bit game in the VM**: the i686 `d3d11.dll` and `d3d12.dll` carry Rust for the first time (std's TLS,
   `rust_pe_rt.c`'s `__tls_array`/`_aulldiv`; the `dxmt_*` ABI's stack arguments). Expected: they load, export and
   draw as before. Tests: Task 2's guest32 tables (P16); every seam's generated `abi.rs` compiled for i686 (P2);
   Task 20's d3d-smoke `32:` blocks in both modes and under `MN_WOW64=new`, `dxgi-guest.sh i386`, and the VM stats
   row (`list7_32.exe` with `DXMT_STATS=1`: the first run of the i686 `thread_local!` path, which stats-off never
   touches).
2. **The VM host's native dylibs**: export lists replace "every global" (P16); `dispatch.c` and the loopback `dlsym`
   every listed name, `d3d10core.dylib` links `d3d11.dylib`. Expected: every program forwards as before. Tests: Task
   2's `lipo`/`nm -gU` loop on the lane's dylibs, OSO and `$HOME` 0; Task 20's `eac-vm/dxmt-native/tests/build.sh`,
   native gate (31), loopback (31), `dispatch-test`, and the forwarder's own checks (`d3d/gate.sh`, `answers.sh`,
   `identity.sh`, `wrapped.sh`, `both.sh wire`, `pieces.sh`, `threads.sh`).
3. **Fullscreen, HDR, gamma and resizing presents through the Rust presenter.** Expected: a PSO rebuild waits for
   every requested frame without blocking the encode thread, a gamma ramp's version gates its upload, HDR metadata
   and colour space reach the layer. Tests: Task 6's `presenter::tests::{sync_waits_for_every_requested_frame,
   sync_wait_does_not_block_encode_state, frame_done_never_moves_back, gamma_lut_is_the_cpps,
   unchanged_properties_keep_the_pso, present_metadata_as_cpp}` and the presenter golden's replay (Task 0 Step 4),
   its `present_loop` and spatial-scaler smokes; Task 20's presenter/check.sh, the clear and fullscreen rows, step 3's
   fullscreen-gamma row, present-gate (guest and forward) and the histograms.
4. **A player's shader cache**: relative or slashless `DXMT_SHADER_CACHE_PATH`, `DXMT_SHADER_CACHE=0`, a D3D11 and a
   D3D12 device in one process, a cold cache. Expected: the same files and table as today, no cache turnover, no
   `-wal`/`-shm` left at exit. Tests: Task 8's `shader_cache::tests::{path_strings_are_the_cpps, cache_golden_replays,
   writer_opens_before_reader, drop_releases_reader_and_writer, zero_turns_it_off}` (exact strings, host-independent:
   every gate sets an absolute path, so the relative default players use is pinned only here); Task 20's cache and
   version rows, d3d-smoke's `shaders_*.db`, the key.
5. **MacNeutron's perf mode and p builds**: `DXMT_STATS=1` reports, a non-ASCII or unwritable dump folder, a game
   killed mid-write, F9 into a folder that cannot be made. Expected: `perf.py`'s regex and `fps()` match, no `.tmp`
   takes a final name, existing captures stay, F9 never loops, a p build has no stats code or text. Tests: Task 5's
   `stats::tests::{header_matches_perf_py, counters_sum_by_name_in_order, timed_lines_sorted_desc_ties_by_id_top_30,
   frame_reports_after_5s, ids_past_the_last_share_it}`, `dump::tests::{save_replaces, save_once_keeps_existing,
   failed_write_leaves_no_tmp, make_folder_true_on_other_errors, mac_path_gets_the_z_drive}`; Task 20's stats golden
   (with the 6 s `every 5 s` run), the capture and fullscreen rows, the VM stats row, the p-build strings check.

---

### Task 0: base, worktrees, the lane kit, the host goldens (owner)

**Files:** create (scratch, uncommitted) `$S/lane-build.sh`, `$S/ident.sh`, `$S/ident.py`, `$S/ident-diff.sh`,
`$S/ident-diff.py` (all four copied from `$B/step-r/`, step R P3: the `.sh` files exec the `.py` beside them),
`$S/golden/{format.cpp,presenter.cpp,shader_cache.cpp,barrier.cpp,d3d12_native.cpp}`, the ledger. Committed on
`step5a` (Task 2 carries them, the files' homes existing from S0a): golden data under
`src/dxmt/src/{format,presenter,shader_cache}/golden/`, `src/d3d12/src/{barrier,root_signature,resource_math,
feature}/golden/`.

**Interfaces:**
- Produces: `lane-build.sh <tree> <mac|native|guest64|guest32> [p]` (each kind configured with the options its
  MacNeutron script passes: `wine-arm64/build.sh:494-560`, `eac-vm/dxmt-native/build.sh:20-36`,
  `eac-vm/dxmt/build.sh:38-48, 73-80`, re-read at Step 1; `p` adds `-DDXMT_PERF_BUILD`), output
  `$S/lb/<basename tree>-<kind>[-p]` (overwritten when LB re-runs on the same worktree: a reference is copied out);
  `ident-diff.sh`'s **ED10** (P16's delta: on `d3d11.dll`/`d3d12.dll` imports may gain only P16's three DLLs and the
  symbols of step 2's Rust-std list and P13's kernel32 calls, nothing removed; section sizes; a TLS data directory;
  relative Rust source-path strings (`$HOME` stays 0); on the native `d3d11.dylib`/`d3d12.dylib` the export table may
  shrink to exactly P16's list, OSO and `$HOME` counts 0); the golden files below.

- [ ] **Step 1: Base.** `main` has step R's and step 3's commits (ledger both shas and the `stepR-layout` head
  actually rebased onto); MacNeutron `dxmtrs` carries step R's pin bump (`MTLD3D_RUST_NIGHTLY` in `dxmt/pins`,
  `mtld3d-notices.py`'s LEGS on the root workspace), else stop: RL has no nightly to pass. Every `src/`, `tools/` path
  this plan names as existing passes `test -e` in a worktree of `main`, and each cited line still says what this plan
  says, DXMTRS and MacNeutron alike (`wine-arm64/build.sh`, `eac-vm/dxmt/build.sh`, `eac-vm/dxmt-native/build.sh`,
  `eac-vm/dxmt-native/build-tests.sh`, `release/release.sh`'s `check_no_traces` lines, check.sh's labelled rows of
  Review Focus, `eac-vm/dxmt/gate.sh`'s program count, which step 3 makes 45); moved lines ledgered. No `dxmt_dep`
  user is left but `d3d11` and `d3d12` (step 3's dxgi meson dropped it). Key → ledger its value (`71a7e5f2…3311`
  unless step 4's switch has landed). Name the files holding dxgi's and nvapi's hand-declared winemetal functions
  (Task 3) and dxgi's config and `GammaRamp`, nvapi's and nvngx's private interfaces (Task 4): if one file is in
  both lists, lane `util` branches from lane `sys`'s commit instead of Task 2's.
- [ ] **Step 2: Branch and kit.** `git -C $D branch step5a main`; worktree `$S/wt-base` on it. RL and `sh
  tools/audit.sh` once on `wt-base` → `rust-check: ok`, `audit: clean (<n>)` (ledger `<n>`). Write `lane-build.sh`;
  run `LB(native)` and `LB(mac)` on `wt-base` → ok (the C++ reference dylibs for Step 6 come from
  `$S/lb/wt-base-native`). Add ED10 to `$S/ident-diff.py`; mutation RED: a copy of an LB `d3d12.dll` table with one
  export removed fails under ED10.
- [ ] **Step 3: Format golden** (host). `golden/format.cpp` compiles `src/dxmt/dxmt_format.cpp` with host clang++
  against `include/native`, defining `MTLDevice_supportsFamily` and `MTLDevice_supportsBCTextureCompression` from a
  global mask (the inspector's only device calls: `dxmt_format.cpp:37, 116, 162, 173, 181`). Dumps: `dxgi.txt` (every
  `DXGI_FORMAT` 0..=192, 0x7FFFFFFF, 0xFFFFFFFE and 0xFFFFFFFF: return value and all five desc fields), `caps.txt`
  (the inspector's table for each of {none, Apple7, Apple7+8, Apple7+8+9, Mac2, Apple7+Mac2} × BC {0,1}: every key,
  zero-capability ones included, since `textureCapabilities.contains()` is observable), `helpers.txt` (every
  `WMTPixelFormat` value, and each base format again with `WMTPixelFormatCustomSwizzle` bits set by one non-identity
  `ENCODE_FORMAT_SWIZZLE`, through the nine helpers: they switch on the raw value but `MTLGetTexelSize` masks
  `ORIGINAL_FORMAT`; `SanitizeRTVClearColor` per format on ±0, ±0.1, 255.5, 65504.5, 16777217, 4294967000, ±1e9,
  ±inf, NaN and a denormal, outputs as f64 bit patterns: the clamps take `float`, and the float path passes ±inf).
  Expected: two runs byte-equal.
- [ ] **Step 4: Presenter golden** (host). `golden/presenter.cpp` compiles `src/dxmt/dxmt_presenter.cpp` and
  `dxmt_format.cpp` with host clang++ against `include/native`, defining every winemetal C function they reach as a
  stub that prints its name and arguments (layer props and colour space, `nextDrawable`, device `newTexture`, library
  `newFunction`/`newFunctionWithConstants` with the six constants, `newRenderPipelineState` with its descriptor,
  `replaceRegion` with a hash of its bytes, the render encoder's records decoded from the `wmtcmd` stream with every
  argument, viewport and bytes included) and `WMTQueryDisplaySettingForLayer` answering from the scenario. It reads
  `presenter/golden/matrix.txt` (one scenario a line: drawable and backbuffer sizes, formats, sample counts, colour
  spaces incl. scRGB and PQ, HDR metadata none/some, gamma ramp none/two versions, display-setting version, EDR
  values) and prints `calls.txt`: each scenario's `changeLayerProperties`/`changeColorSpace`/`changeHDRMetadata`/
  `changeGammaRamp`, `synchronizeLayerProperties` (its `DXMTPresentMetadata` printed exactly) and `encodeCommands`
  calls with the stubs' lines. Expected: two runs byte-equal.
- [ ] **Step 5: Shader-cache golden** (host). `golden/shader_cache.cpp` compiles `src/dxmt/dxmt_shader_cache.cpp` and
  `src/util/util_env.cpp` with host clang++ (`DXMT_TRANSLATOR_KEY`, `version.h` and `AIRCONV_VERSION` defined on the
  command line), stubbing `CacheWriter_alloc_init`/`CacheReader_alloc_init` to print call order, path and version.
  It runs `shader_cache/golden/matrix.txt` (`DXMT_SHADER_CACHE_PATH` unset, `/abs`, `/abs/`, `rel`, empty ×
  `DXMT_CACHE_EXE` unset, `Game.exe` × `DXMT_SHADER_CACHE` unset, `0`, `1` × Metal versions 310, 320) and prints
  `calls.txt`. Expected: two runs byte-equal; the writer's line before the reader's in every opened row.
- [ ] **Step 6: D3D12 goldens** (host). `golden/barrier.cpp` includes `d3d12_barrier_legacy.hpp` and runs the input
  classes of `<M>/eac-vm/d3d/tests/barrier-test.cpp` plus one case per branch of `LegacyBarriers`, `LegacyState` and
  `InitialState` (sync NONE, NO_ACCESS, unknown access, UAV→UAV, the four global cases, texture and buffer, layouts
  and DISCARD ignored) → `barrier/golden.txt`. The five native dylibs of `$S/lb/wt-base-native` are copied into
  `$S/golden/native-lib/` with `eac-vm/dxmt-native/build.sh:55-67`'s `install_name_tool` lines applied; then
  `golden/d3d12_native.cpp` `dlopen`s that `d3d12.dylib`, creates a device and records: `D3D12SerializeVersionedRootSignature` of a 12-desc table (every
  parameter type, ranges, static samplers, flags, versions 1.0/1.1) as `root_signature/golden/*.bin`;
  `GetCopyableFootprints` and `GetResourceAllocationInfo` over a desc table (buffer, 1D/2D/3D, arrays, mips, BC,
  planar depth) → `resource_math/golden.txt`; `CheckFeatureSupport` for every feature it answers and every format →
  `feature/golden.txt`. Expected: two runs byte-equal; a desc the C++ crashes on is dropped and ledgered.

### Task 1: phase P and the pipeline goldens (owner)

**Files:** `src/d3d12/d3d12_command_encoder.hpp` and its users under `src/d3d12`; `src/winemetal/unix/src/` (the
handler of `MTLCommandBuffer_commit` and the three `*_encodeCommands` handlers; a `histogram.rs` +
`histogram/tests.rs`). Scratch: `$S/stats-golden.sh`, `$S/stats-compare.sh`, `$S/hist.sh`, `$S/hist-compare.sh`,
`$S/ref-wine.app`, `$S/ref-{lb,guest,native,vm,present,perf}/`. Map P4 (the statistics flag as a C symbol) is
dropped: the flag stays C++'s `g_stats_on` and reaches Rust by value (P4's data-symbol rule), so phase P is two
commits.

- [ ] **Step 1 (P3): `namespace dxmt::d3d12`** for D3D12's `EncoderType`, `EncoderData` and the six `*EncoderData`
  (`d3d12_command_encoder.hpp:25-183`). Check: `LB(native)`, `LB(mac)`, `LB(guest32)` ok; `nm -C` of the native
  `d3d12.dylib` and of the mac build's `d3d12.dll.p/*.obj` lists D3D12's destructors as `dxmt::d3d12::…` and the
  core's `dxmt::*EncoderData` unchanged; the only symbols that differ are those whose demangled text names a moved
  type. Commit `d3d12: its encoder types in their own namespace`.
- [ ] **Step 2 (P10): the histogram knob.** Test `histogram::tests::lines_count_since_the_last_commit` (two record
  lists, a commit, one more list: the appended lines carry the counts since the last commit). Implement; `UT(winemetal)`
  green; mutation RED: count one record twice → the test fails. Commit `winemetal: DXMT_WMTCMD_HISTOGRAM counts
  encoded records per command buffer`.
- [ ] **Step 3: Pipeline references.** Host first: `LB(mac)`, `LB(native)`, `LB(guest64)`, `LB(guest32)` on
  `wt-base` at `step5a`'s head, each copied to `$S/ref-lb/<kind>` and tabulated (`ident.sh --tabulate`). Then one
  lock job:
  - Build `step5a`'s head (`make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot`),
    `cp -cR` wine.app to `$S/ref-wine.app` and tabulate its DXMT files, record its cdhash; `sh eac-vm/dxmt/build.sh`
    (both widths, builder with `VM_SHARES`) and `sh eac-vm/dxmt-native/build.sh`, their outputs copied to `$S/ref-guest`
    and `$S/ref-native` and tabulated.
  - Gate the reference itself, right after the build (wine.app is the reference then): `sh dxmt/check.sh` and the
    ARM64EC form (Task 20's) → `$S/ref-perf/check-{x64,arm64ec}.txt`, 0 FAIL (Task 20's row set and count come from
    these).
  - VM and present references: `sh eac-vm/dxmt/share.sh`, then `MN_REF_RUNS=2 MN_REF_OUT=$S/ref-vm
    MN_REF_WINE_APP=$S/ref-wine.app sh eac-vm/dxmt/ref.sh` → 45 programs; present-gate's two reference invocations
    (`d3d12_clear 600`, `present_loop 1280 720 0 0 600 0`, written to a file as present-gate.sh writes them) through
    `MN_REF_INVOCATIONS=<that file> MN_REF_OUT=$S/ref-present MN_REF_WINE_APP=$S/ref-wine.app sh eac-vm/dxmt/ref.sh`,
    as step R recorded `$B/step-r/ref-present`.
  - Spatial scaler (no gate sets `DXMT_METALFX_SPATIAL_SWAPCHAIN`, the scaler's only caller): `present_loop 1280 720
    640 360 100 0` on the x64 lane with `DXMT_METALFX_SPATIAL_SWAPCHAIN=1 MTL_DEBUG_LAYER=1`, twice → its `frames`
    line, no validation error, and its histogram, in `$S/ref-perf/spatial.txt`.
  - On `$S/ref-wine.app`, twice each: `stats-golden.sh` (the D3D12 programs check.sh runs with `DXMT_STATS=1`, its
    arguments, `DXMT_DXIL_DUMP` per run, plus one run of 6 s or more of presents, `d3d12_clear <n>` with `<n>` = 7 s
    × the frame rate the first stats run's header reports (it presents without vsync; ledgered), whose `every 5 s` header
    `perf.py`'s `fps()` parses; keeps each `stats.txt`; a run with no `stats.txt`, no header or no `counters:` block
    fails) and `hist.sh` (`present_loop`, `d3d12_clear`, `d3d12_triangle`, `d3d12_upscale`, `d3d12_fullscreen` and the
    D3D11 programs of check.sh's `run ours` rows, check.sh's arguments; an empty histogram fails).
  `stats-compare.sh a b` compares the `counters:` blocks (names, counts), the header count and each header's frames,
  and the timed lines by shape per field class (`[0-9]+` for `%llu` and `%.0f`, `[0-9]+\.[0-9]` for `%.1f`, every other
  byte kept: `thread <name> (<id>): <n> us in D3D12[, <x> us/frame]`, `  <name> calls <n> us <t> max-us <m>[ | per
  frame: …]`) with names and their order, and prints `stats-compare: equal`; `hist-compare.sh` compares per-run
  totals and prints `hist-compare: equal`. Expected: each pair equal (a label or program that differs between the
  two runs is ledgered as compared by presence only); mutation RED: one edited count, and one `%.1f` field printed
  as an integer, in a copy each fail their comparator.

### Task 2 (S0a): crates, skeletons, links, lists (owner)

**Files (DXMTRS):** root `Cargo.toml` (members `src/dxmt`, `src/d3d11`, `src/d3d12`, `src/winemetal/sys`;
`[workspace.dependencies]` `dxmt-core`, `winemetal-sys`), `Cargo.lock`; create
`src/dxmt/{Cargo.toml,build.rs,src/lib.rs}` and one stub file per module of P14 (S and W: `stats`, `dump`, `presenter`,
`scaler`, `format`, `shader_cache`, `log`, `resource`, `ring`, `staging`, `dynamic`, `residency`, `sampler`, `library`,
`capture`, `hud`, `info`, `queue`, `arena`, `encoder`, `deptrack`, `query`, `initializer`, `counter`, `emulated`),
`src/d3d11/{Cargo.toml,src/lib.rs}`, `src/d3d12/{Cargo.toml,src/lib.rs,src/abi.rs}` with stubs `barrier`,
`root_signature`, `resource_math`, `feature`, `sampler`, `pipeline_stream`,
`abi/{barrier,root_signature,resource,feature,sampler,pipeline}`; `src/winemetal/sys/{Cargo.toml,src/lib.rs}` (`lib.rs`
empty, `Cargo.toml` final); `src/util/src/sync.rs` (stub) and its `pub mod` line in `src/util/src/lib.rs` (so E1 never
edits Task 4's file); modify `src/d3d11/meson.build`, `src/d3d12/meson.build`, `tools/rust-check.sh` (CM/UT/doc lists),
`tools/audit.sh` (PE-side list), `rustfmt.toml` (`ignore` gains `src/winemetal/sys/src/decls.rs` and the six seam
`abi.rs` of the Rules check, so no lane edits it); the Task 0 goldens. MacNeutron (`step5a-mn`):
`dxmt/mtld3d-notices.py` LEGS, `wine-arm64/tests/licences_test.sh` if it lists packages.

**Interfaces:**
- Every new manifest in its final form, so no lane edits one: `dxmt-core`'s full `[dependencies]` (the Rules check's
  edges, `libc` under `[target.'cfg(target_os = "macos")'.dependencies]`), `winemetal-sys`'s `winemetal-wire`
  dependency and `[features] guest-vm = []`, and the feature forwards below (`cargo metadata` resolves them although
  the crates are stubs).
- Produces: package `dxmt-core` (lib name `dxmt_core`), features `perf-build = ["dxmt-util/perf-build"]`, `guest-vm =
  ["winemetal-sys/guest-vm"]`; every module declared `pub mod` with a doc line and no items, except
  `format::forget_srgb(format: u32) -> u32` and `encoder::Encoder` (empty struct), whose final signatures S2 and E1 code
  against; `build.rs` is `fn main() {}` until R3. Packages `d3d11`, `d3d12`: `crate-type = ["staticlib"]`, deps
  `dxmt-core`, `dxmt-util`, features forwarding both; `lib.rs` = crate doc + `use dxmt_core as _;` (P3).
- meson (each component): `foreach t : rust_pe_targets` a `custom_target('d3d11-rust-' + t, …)` running
  `cargo_staticlib` with profile `production` (natively `production-unwind`, P19) and the feature list (P17),
  `link_with` it and `rust_runtime_dep` added to the DLL — nvapi's recipe (`src/nvapi/meson.build`); natively
  `link_args` gain `-Wl,-exported_symbol,_<name>` per `.def` name the dylib defines today (`nm -gU` of Task 0's
  native build ∩ the `.def`; `D3D11On12CreateDevice`, which `rules.txt` refuses, only if defined) plus
  `MTLD3D11WaitIdle` for d3d11, and `+ (get_option('debug') ? [] : ['-Wl,-S'])` (the expression of
  `src/nativemetal/meson.build:13`, P16).

- [ ] **Step 1: RED.** `UT(dxmt-core)` fails to resolve the package. (The tables' RED comes in Step 4: `ident-diff.sh`
  with no ED named fails on exactly the import rows P16 lists.)
- [ ] **Step 2: Crates and stubs**; `git add -A`; `cargo metadata --offline --locked` rc 0 after one `cargo metadata
  --offline` that only adds the four path packages (no version change: `cargo tree` of every existing shipped package
  equal before and after).
- [ ] **Step 3: meson** for `d3d11` and `d3d12` on every build; `LB(mac)`, `LB(native)`, `LB(guest64)`, `LB(guest32)`,
  `LB(mac) p` ok.
- [ ] **Step 4: Tables** (LB to LB, host). `ident-diff.sh $S/ref-lb <these LB outputs, copied as Task 1 copied its own>
  ED1 ED3 ED10` → nothing (ED1, ED3: other folders, another commit): exports and ordinals equal; imports within the P16
  set (each added symbol listed in the ledger); i686 `d3d11.dll`/`d3d12.dll` resolve `__tls_array` and `_aulldiv` (no
  new undefined); `otool -L` equal. Natively, on `$S/lb/<wt>-native/src/<dir>/<lib>.dylib` directly (the loop of
  `eac-vm/dxmt-native/tests/build.sh`, which itself builds `$D` into the shared folders and runs only in Task 20): `lipo
  -archs` = `arm64` and `nm -gU` has each entry point it names; `nm -gU` of `d3d11`/`d3d12` = P16's lists exactly; `nm
  -ap | /usr/bin/grep -c ' OSO '` and `LC_ALL=C /usr/bin/grep -ac "$HOME"` both 0.
- [ ] **Step 5: Lists.** `tools/rust-check.sh` names the four packages on every target each ships on (`dxmt-core`,
  `d3d11`, `d3d12`, `winemetal-sys`: aarch64, arm64ec, x86_64, i686 msvc and darwin; UT with the dynamic-lookup
  form; `perf-build` legs for `d3d11 d3d12`); `tools/audit.sh`'s PE side adds `src/dxmt`, `src/d3d11`, `src/d3d12`,
  `src/winemetal/sys`; MacNeutron's notices legs add `d3d11`, `d3d12` on their shipped targets. RL → `rust-check: ok`;
  audit file count grown by the new `.rs` files.
- [ ] **Step 6: Loads** (one lock job). Build this commit (`make wine-arm64 dxmt-tests`), run `d3d12_triangle` and
  `d3d11_render` on the x64 lane as check.sh runs them → their `ok` lines; ledger the cdhash.
- [ ] **Step 7: Commit** `dxmt: the core's crates, linked into d3d11 and d3d12` (goldens in the same commit);
  MacNeutron `step5a-mn`: `licences: d3d11 and d3d12 carry Rust`.

### Task 3 (S0b): `winemetal-sys` (lane `sys`)

**Files:** `tools/winemetal-layouts.sh` (`--decls`, `--seam-abi`); `src/winemetal/sys/src/{lib.rs,decls.rs
(generated),obj.rs,obj/tests.rs}` (its `Cargo.toml` is Task 2's, final); `src/nativemetal/win32_event.c` (5-D8's
exports, P14); `src/nvapi/{Cargo.toml,src/ffi.rs}` (`ffi.rs` re-exports the retired declarations from `winemetal_sys`,
so `disp.rs`/`gpu.rs` stay untouched), `src/dxgi/Cargo.toml`, dxgi's winemetal declarations (step 3's
`src/dxgi/src/…`) switched to `winemetal_sys` but for `WMTGetWsiHooks`; nvapi's and dxgi's `Cargo.lock` entries.

**Interfaces:**
- Produces: `winemetal_sys::*` = `decls.rs` (P12) plus, under `cfg(not(windows))`, the hand-declared `WMTMemoryHooked`,
  `WMTGetWsiHooks() -> *const c_void` and 5-D8's natives; `pub struct Obj(u64)` (non-null +1 handle;
  `Obj::adopt(h: u64) -> Option<Obj>`, `Obj::retain(h: u64) -> Option<Obj>`, `fn handle(&self) -> u64`, `Drop` →
  `NSObject_release`; `Send + Sync`: Metal objects' retain/release are thread-safe); `pub struct Pool` (`Pool::new()`
  = `NSAutoreleasePool_alloc_init`, `Drop` releases; `!Send`: a pool belongs to its thread).
- Produces (native C, 5-D8, critic 1.5): `win32_event.c` exports, beside its `wmt_native_set_event`/`_ref`/`_unref`,
  `wmt_native_*` wrappers over `win32_native.h`'s `CreateSemaphoreW`, `ReleaseSemaphore`, `DuplicateHandle`,
  `WaitForSingleObject`, `OpenThread`, `CloseHandle` and `CreateEventW` (one C implementation, no Rust copy of
  `mn_win32_handle`), in the native `winemetal.dylib` every native dylib already links.
- Produces (tool): `sh tools/winemetal-layouts.sh --seam-abi <header> <module path> "$LM/bin"` (P2, P12): the
  header's `dxmt-abi` block → `abi.rs` of per-target `const _: unsafe extern "C" fn(..) = <module path>::<name>;`
  (`"C-unwind"` under `cfg(not(windows))`, P19) from llvm-mingw's AST on the four PE targets and Apple clang's on
  darwin (64-bit `long` there); S1-S4 run it on their own headers.
- Consumes: Task 2's crate and `winemetal-wire`'s mirrors.

- [ ] **Step 1: RED.** `obj::tests::{adopt_releases_once, retain_retains_then_releases, null_is_none, pool_drains}`
  (test definitions of `NSObject_retain`/`_release`/`NSAutoreleasePool_alloc_init` count calls); a `decls` check:
  `sh tools/winemetal-layouts.sh --decls "$LM/bin" | cmp - src/winemetal/sys/src/decls.rs` fails (no mode); a scratch
  header whose `dxmt-abi` block declares `void dxmt_t(void *p, WMTColorSpace cs, double w)` against a Rust
  `dxmt_t(p: *mut c_void, cs: u32, w: f64)`: `--seam-abi` fails (no mode).
- [ ] **Step 2: GREEN.** `--decls` as P12 (the `--protos` set, both ABI blocks); `--seam-abi` as above, and the
  scratch pair then fails to compile on every target (`cs` is `u64`), passing once `cs: u64`; the native hand
  declarations each with their Apple-clang prototype check (P12); `win32_event.c`'s exports. Regenerate twice →
  equal; `--protos` and the layouts still regenerate equal (R-D4); `LB(native)` → `nm -gU` of its `winemetal.dylib`
  lists each new `_wmt_native_*`.
- [ ] **Step 3: Adopt** in nvapi and dxgi; msvc builds of both on their targets; `UT(winemetal-sys nvapi dxgi)`; RL.
- [ ] **Step 4: Commit** `winemetal: winemetal-sys, its C API for Rust callers (generated)`.

### Task 4 (S0c): what both front ends share, into `dxmt-util` (lane `util`)

**Files:** `src/util/src/{config.rs,config/tests.rs,gamma.rs,ext.rs,ext/tests.rs}`, `src/util/src/lib.rs`; remove the
moved items from `src/dxgi/src/{config.rs,abi.rs}`, `src/nvngx/src/com.rs:14-17, 60-112`, `src/nvapi/src/d3d.rs:13-29`
(their users import from `dxmt_util`); `docs/CONVENTIONS.md` (ABI homes: `GammaRamp`, the private interfaces).

**Interfaces:**
- Produces: `dxmt_util::config::{Config, Tristate}` with step 3's API plus `get_bool`, `get_i32`, `get_f32`, `get_str`,
  `get_tristate(key, fallback)`, each parsing as `config.hpp:127-145` does (`True`/`False`/`Auto` for `Tristate`, case
  as the C++); `dxmt_util::GammaRamp` moved verbatim from step 3's `abi.rs` (`#[repr(C)] { red, green, blue: [AtomicU32;
  1024], version: AtomicU64 }`, size 12296, align 8, `version` at 12288, asserted: dxgi's `&self` COM methods store the
  values Relaxed, then `version` Release, while the presenter reads them on the Present thread,
  `dxmt_presenter.cpp:178-198`); `dxmt_util::ext` = the three D3D12 and four D3D11 private interfaces' IIDs, vtables and
  `MTL_TEMPORAL_*` structs (`d3d12_interfaces.hpp:5-44`, `d3d11_interfaces.hpp:5-65`), size and offset asserted.

- [ ] **Step 1: RED.** Moved tests plus `config::tests::{i32_parses_as_cpp, f32_parses_as_cpp, tristate_reads_auto,
  bad_value_takes_fallback}`, `gamma::tests::layout_is_12296`, `ext::tests::upscale_desc_is_88_with_color_at_24`.
- [ ] **Step 2: GREEN**; the drift test (step 3's, `include_str!` of `src/util/config/config.cpp`) passes from its new
  home; nvngx, nvapi and dxgi build on their targets; `UT(dxmt-util nvngx nvapi dxgi)`; RL.
- [ ] **Step 3: Commit** `dxmt-util: configuration, the gamma ramp and the private interfaces, for every front end`.

### Task 5 (S1): statistics and the dump folder (lane `s1`)

**Files:** `src/dxmt/src/{stats.rs,stats/{platform.rs,tests.rs,abi.rs},dump.rs,dump/{tests.rs,abi.rs}}` (the `abi.rs`
generated, P2); `src/d3d12/d3d12_stats.hpp` (declarations, inline forwards), `d3d12_stats.cpp` (only `g_stats_on`,
unchanged, and the load-time object: ctor `dxmt_stats_init(g_stats_on)`, dtor `dxmt_stats_exit()`),
`d3d12_dxil_dump.hpp`, `d3d12_dxil_dump.cpp` (keeps `DXILStage`, `DumpDXIL`, `DumpRootSignature`, `SM6Caps`,
`CaptureHash`, `NamePipeline`/`PipelineName`; the file writing forwards).

**Interfaces:**
- Consumes: Task 1's stats golden; Task 2's `stats`/`dump` stubs and `perf-build` feature; Task 3's `--seam-abi`;
  `dxmt_util::float_cast`.
- Produces (C, P2):
  ```c
  void dxmt_stats_init(bool on);  void dxmt_stats_exit(void);   /* on: the C++'s g_stats_on, by value (P4) */
  unsigned dxmt_stat_id(const char *name);            /* copies; ids past 1023 share 1023 */
  void dxmt_stat_count(unsigned id, uint64_t n);
  uint64_t dxmt_stat_begin(void);  void dxmt_stat_end(unsigned id, uint64_t t0);
  void dxmt_stats_frame(void);  unsigned dxmt_stats_next_pass(void);
  bool dxmt_dump_on(void);
  void dxmt_dump_save(const char *name, const void *data, size_t len);       /* replaces */
  void dxmt_dump_save_once(const char *name, const void *data, size_t len);  /* keeps an existing file */
  bool dxmt_dump_make_folder(const char *name);
  void dxmt_dump_append_pipeline(const char *line, size_t len);              /* adds "\n" */
  ```
  Under `DXMT_PERF_BUILD` every forward is inline and answers as the C++ does with no folder (`MakeCaptureFolder`
  true, the rest nothing or false).
- Produces (Rust, for the switches and C2): `stats::{on() -> bool, id(name: &str) -> StatId, count(StatId, u64),
  begin() -> u64, end(StatId, u64), frame(), next_pass() -> u32, Scope}`; `dump::{folder() -> Option<&'static Path>,
  save(&str, &[u8]), save_once(&str, &[u8]), make_folder(&str) -> bool, append_pipeline(&str)}`. Names are any API's;
  the report text stays `… us in D3D12` until C2.
- Behaviour: `d3d12_stats.cpp:21-237` and `d3d12_dxil_dump.cpp`'s `CaptureFolder`, `SaveOnce`, `MakeCaptureFolder`,
  `SaveCapture` and `LogPipeline`, byte for byte in the files; the
  counter is the C++'s per target (`rdtsc`, `cntvct_el0`), seconds from `std::time::Instant` (QPC on PE,
  `CLOCK_UPTIME_RAW` natively: the C++'s clocks); P13's platform calls; P18's counters. The report: timed lines
  sorted by (ticks, id) descending, ties to the higher id, cut at 30 (`d3d12_stats.cpp:122-123`); a report every 5 s
  of frames from `frame()`; `next_pass` restarts at 0 each frame (`:228`); each line built as the C++'s `snprintf`
  formats print it. PE paths: `Z:` + a value starting with `/`, `\` + name; natively as `win32_native.h`'s file
  shims map them, `\` included (F9's `f9-<n>\<name>`). The folder is kept when its creation fails (capture mode and
  `SM6Caps` stay on, `d3d12_dxil_dump.cpp:50-55`); `make_folder` is false only when the folder already exists: true
  when off, created, or on any other error, since F9's `while (!MakeCaptureFolder(dir))` runs under the dumps mutex
  (`d3d12_command_queue.cpp:1166-1170`) and a `create_dir().is_ok()` would loop forever on an unwritable folder.
  `save_once` checks for the file, then writes `<name>.tmp` and renames (names are content hashes, so a racing
  replace writes equal bytes; ledgered).

- [ ] **Step 1: RED.** `stats::tests::{header_matches_perf_py (the line against perf.py's regex),
  thread_and_call_lines_as_printf, counters_sum_by_name_in_order, boundaries_line_always_last,
  ids_past_the_last_share_it, max_resets_per_report, timed_lines_sorted_desc_ties_by_id_top_30,
  scope_of_10ms_reports_10000us, frame_reports_after_5s, next_pass_restarts_each_frame}` over a pure `report(snapshot,
  seconds, frames, why, hh_mm_ss) -> String` and an injectable clock, the expected strings built in the test with
  `libc::snprintf` over the C++'s format literals (copied from `d3d12_stats.cpp`) on a grid that includes `%.1f`
  rounding ties; `dump::tests::{save_replaces, save_once_keeps_existing, failed_write_leaves_no_tmp,
  make_folder_true_when_new_or_off, make_folder_true_on_other_errors, folder_kept_when_mkdir_fails,
  save_into_subfolder_with_backslash (darwin), append_pipeline_adds_newline, mac_path_gets_the_z_drive}` (temp
  folders; the path rule pure). Expected: FAIL. Mutation RED after GREEN: `make_folder` as `create_dir().is_ok()` →
  `make_folder_true_on_other_errors` fails.
- [ ] **Step 2: GREEN**, then the C++ cut (the C++ keeps `g_stats_on` and passes it to `dxmt_stats_init`);
  `--seam-abi` on `d3d12_stats.hpp` and `d3d12_dxil_dump.hpp` → `stats/abi.rs`, `dump/abi.rs`, compiled by the msvc
  builds of `dxmt-core` on the four PE targets and on darwin; `UT(dxmt-core)`; `LB(mac)`, `LB(native)`,
  `LB(guest32)`, `LB(mac) p` ok; on the p build `nm` has no `dxmt_stats_`/`dxmt_dump_` and `strings | /usr/bin/grep
  -c 'us in D3D12\|stats.txt\|counters:'` = 0; RL.
- [ ] **Step 3: Commit** `dxmt: statistics and the dump folder in Rust`.
- [ ] **Step 4: Smoke** (one lock job, owner; S1 is the first Rust `thread_local!` live in a mingw-linked DLL): this
  commit detached in `$D`, `make wine-arm64 dxmt-tests`, `stats-golden.sh` for `d3d12_hazards` alone on x64 →
  `stats-compare.sh` against its Task 1 entry equal. A failure goes back to the lane before the merge.

### Task 6 (S2): the presenter and the scalers (lane `s2`)

**Files:** `src/dxmt/src/{presenter.rs,presenter/{tests.rs,abi.rs},scaler.rs,scaler/abi.rs}` (the `abi.rs`
generated, P2); `src/dxmt/dxmt_presenter.{hpp,cpp}`, `dxmt_scaler.{hpp,cpp}` (wrappers: the classes keep their public
members; P8's block goes).

**Interfaces:**
- Consumes: `winemetal_sys::{Obj, Pool, …}` and `--seam-abi` (Task 3), `dxmt_util::GammaRamp` (Task 4),
  `format::forget_srgb` (Task 7), Task 0's presenter golden, Task 1's spatial-scaler reference.
- Produces (C, P2, P4):
  ```c
  typedef void (*dxmt_encoder_fn)(void *ctx, obj_handle_t encoder);
  void *dxmt_presenter_create(obj_handle_t device, obj_handle_t layer, obj_handle_t library,
                              float scale_factor, uint8_t sample_count);
  void dxmt_presenter_destroy(void *p);
  bool dxmt_presenter_change_layer_properties(void *p, WMTPixelFormat format, WMTColorSpace cs, double width,
                                              double height, uint8_t sample_count);
  bool dxmt_presenter_change_layer_color_space(void *p, WMTColorSpace cs);
  void dxmt_presenter_change_hdr_metadata(void *p, const WMTHDRMetadata *metadata);
  void dxmt_presenter_change_gamma_ramp(void *p, const DXMTGammaRamp *ramp);
  uint64_t dxmt_presenter_synchronize(void *p, DXMTPresentMetadata *out);   /* the frame id */
  void dxmt_presenter_frame_done(void *p, uint64_t frame_id);               /* PresentState's destructor */
  obj_handle_t dxmt_presenter_encode(void *p, obj_handle_t cmdbuf, obj_handle_t backbuffer,
                                     const DXMTPresentMetadata *metadata, dxmt_encoder_fn wait,
                                     dxmt_encoder_fn update, void *ctx);    /* +1 drawable */
  void *dxmt_spatial_scaler_create(obj_handle_t device, const WMTFXSpatialScalerInfo *info);
  void *dxmt_temporal_scaler_create(obj_handle_t device, const WMTFXTemporalScalerInfo *info);
  void dxmt_scaler_destroy(void *s);
  obj_handle_t dxmt_scaler_object(void *s);  obj_handle_t dxmt_scaler_fence(void *s);  /* +0 */
  ```
  `cs` is `uint64_t` on both sides (`WMTColorSpace` is `enum : uint64_t`; Rust `u64`, `winemetal-wire`'s type), pinned
  by `presenter/abi.rs`. `library` is `InternalCommandLibrary::getLibrary()`'s handle (`dxmt_command.hpp:45-48`); the
  C++ `Presenter::encodeCommands` passes `noexcept` trampolines over its two `std::function`s (P4); `PresentState`'s
  destructor calls `frame_done`. `change_gamma_ramp`'s ramp is the dxgi output's live `GammaRamp`, read through its
  atomics (Task 4). Scaler refcounts, `refCount()` and `info()` stay C++.
- Produces (Rust): `Presenter::new(&PresenterDesc) -> Option<Presenter>` (`PresenterDesc { device, layer, library,
  scale_factor, sample_count }`: the struct D3D9 extends, 5-D13), the `change_*` methods, `synchronize() -> (u64,
  PresentMetadata)`, `frame_done(u64)`, `encode(cmdbuf, backbuffer, &PresentMetadata, wait: impl FnOnce(u64), update:
  impl FnOnce(u64)) -> Option<Obj>`; `SpatialScaler`, `TemporalScaler` (`new`, `scaler()`, `fence()`).
- Behaviour: `dxmt_presenter.cpp:11-337` without P8's block: the 1024×1 RGBA32F gamma LUT, the two PSOs with the six
  function constants, `WMTQueryDisplaySettingForLayer` and EDR, the rebuild that waits for every requested frame, the
  blit-or-scaled choice by size, the HDR metadata (`edr_scale`, scRGB's `*= 0.8`, `max_content_luminance` as
  `max<uint32_t>`, `:229-250`). The gamma ramp: `version` loaded Acquire and compared, then the values Relaxed (the
  C++'s order). State the encoding thread reads sits behind one `Mutex` (a lock per present), never `&mut` from two
  threads; the frame wait is a `Mutex<u64>` + `Condvar`. Lock discipline (D3D11's `PresentState` dies on the finish
  thread after the encode thread encoded its frame, `d3d11_swapchain.cpp:812, 827`; D3D12's `encode` runs the C++
  callbacks under `StartCommitting`, `d3d12_command_queue.cpp:2486-2504`; frame generation's pacing thread encodes
  while the game thread synchronizes, `:2272-2278`): `synchronize` reads the invalid flag and `frame_requested` under
  the state lock, drops it, waits on the frame `Condvar`, then relocks to rebuild; `encode` clones (retains) the PSOs,
  the LUT texture and the drawable size under the lock and releases it before any Metal call or callback;
  `frame_done` stores the max (as `CpuFence::signal`, `util_cpu_fence.hpp:17-24`), never moving back.

- [ ] **Step 1: RED.** `presenter::tests::{sync_waits_for_every_requested_frame (a second thread's frame_done
  releases it), sync_wait_does_not_block_encode_state (thread A waits in sync; thread B takes the encode-side
  snapshot, then calls frame_done), frame_done_never_moves_back, gamma_lut_is_the_cpps (the C++'s LUT formula over
  two ramps, built through the atomic stores), unchanged_properties_keep_the_pso, changed_colorspace_marks_it,
  hdr_metadata_none_and_some, present_metadata_as_cpp, golden_replays}` (`golden_replays`: Task 0's
  `presenter/golden/matrix.txt` through the Rust against `cfg(test)` definitions of the same winemetal functions,
  printing as the harness does → equal to `calls.txt`). Expected: FAIL. Mutation RED after GREEN: delete the scRGB
  `0.8` → `golden_replays` and `present_metadata_as_cpp` fail.
- [ ] **Step 2: GREEN** and the C++ cut; `--seam-abi` on `dxmt_presenter.hpp` and `dxmt_scaler.hpp` → the two `abi.rs`,
  compiled on all five targets; rebase onto S3's merge; `UT(dxmt-core)`; the four LBs; RL. Then the harness again:
  `golden/presenter.cpp` built against the cut `dxmt_presenter.cpp` wrapper linked to the lane's native `d3d12` Rust
  archive (`$S/lb/wt-s2-native`) and the same stubs → byte-equal to Task 0's `calls.txt`.
- [ ] **Step 3: Commit** `dxmt: the presenter and the scalers in Rust`.
- [ ] **Step 4: Smoke** (one lock job, owner): this commit detached in `$D`, `make wine-arm64 dxmt-tests presenter`,
  `present_loop` once on the x64 lane as check.sh runs it (presents all frames), Task 1's spatial-scaler run
  (`DXMT_METALFX_SPATIAL_SWAPCHAIN=1 MTL_DEBUG_LAYER=1 present_loop 1280 720 640 360 100 0`) → equal to
  `$S/ref-perf/spatial.txt` (frames line, no validation error, histogram), and `hist.sh` for `present_loop`,
  `d3d12_clear` → `hist-compare.sh` against Task 1's equal. A failure goes back to the lane before the merge. The
  i686 ABI is checked by `presenter/abi.rs` on i686 here and live by d3d-smoke's guest-mode `32:` block at Task 20
  (`wmsize`: a D3D11 swap chain resized through full screen).

### Task 7 (S3): formats (lane `s3`)

**Files:** `src/dxmt/src/{format.rs,format/{tests.rs,abi.rs}}` (`abi.rs` generated, P2);
`src/dxmt/dxmt_format.{hpp,cpp}` (forwards; the table goes).

**Interfaces:**
- Consumes: Task 0's format goldens, Task 2's `format` stub, `winemetal_sys::{MTLDevice_supportsFamily,
  MTLDevice_supportsBCTextureCompression}` and `--seam-abi` (Task 3), `winemetal-wire`'s `WMTPixelFormat` constants.
- Produces (C):
  ```c
  int32_t dxmt_format_query_dxgi(uint32_t dxgi_format, MTL_DXGI_FORMAT_DESC *out);
  WMTPixelFormat dxmt_format_forget_srgb(WMTPixelFormat f);  WMTPixelFormat dxmt_format_recall_srgb(WMTPixelFormat f);
  bool dxmt_format_is_bc(WMTPixelFormat f);  uint32_t dxmt_format_depth_stencil_planar_flags(WMTPixelFormat f);
  uint32_t dxmt_format_texel_size(WMTPixelFormat f);  WMTPixelFormat dxmt_format_unsigned_integer(WMTPixelFormat f);
  bool dxmt_format_is_unorm8_rt(WMTPixelFormat f);  bool dxmt_format_is_integer(WMTPixelFormat f);
  void dxmt_format_sanitize_clear_color(WMTPixelFormat f, WMTClearColor *color);
  size_t dxmt_format_capabilities(obj_handle_t device, struct dxmt_format_capability *out, size_t capacity);
  /* struct dxmt_format_capability { uint32_t format, caps; }: returns the count, writes min(count, capacity) */
  ```
  `MTLQueryDXGIFormat` keeps its device parameter (unused, as today); `FormatCapabilityInspector::Inspect` fills its
  map from `dxmt_format_capabilities`.
- Produces (Rust): `format::{query_dxgi(u32) -> (i32, DxgiFormatDesc), forget_srgb, recall_srgb, is_bc,
  depth_stencil_planar_flags, texel_size, unsigned_integer, is_unorm8_rt, is_integer, sanitize_clear_color,
  capabilities(supports_family: impl Fn(u32) -> bool, bc: bool) -> Vec<(u32, u32)>}`, all keyed by `WMTPixelFormat`
  as a raw `u32` (no enum conversion: D3D12 passes swizzled formats, `ENCODE_FORMAT_SWIZZLE`, `d3d12_texture.cpp:197`;
  the helpers switch on the raw value, `texel_size` alone masks `ORIGINAL_FORMAT`); table lookups through `.get()`, so
  a DXGI id past the table answers the C++'s `E_FAIL` instead of panicking (an abort under `panic = "abort"`); the
  clamps take `f32`, as the C++'s `float` parameters; the DXGI table is one map over them (D3D9's `D3DFORMAT` a
  second at C4).

- [ ] **Step 1: RED.** `format::tests::{dxgi_table_is_the_golden, caps_per_family_are_the_golden,
  helpers_are_the_golden, desc_layout_is_the_cpps}` (`include_str!` of Task 0's files). Mutation RED: one golden byte
  flipped → fails. Expected: FAIL.
- [ ] **Step 2: GREEN** and the C++ cut; `--seam-abi` on `dxmt_format.hpp` → `format/abi.rs`, compiled on all five
  targets; `UT(dxmt-core)`; the four LBs; RL. Then `golden/format.cpp` again, built against the cut `dxmt_format.cpp`
  linked to the lane's native `d3d12` Rust archive (`$S/lb/wt-s3-native`) → its three files byte-equal to Task 0's
  (the C++ forwards: `Inspect`'s capacity, the desc copy).
- [ ] **Step 3: Commit** `dxmt: the format table in Rust`.

### Task 8 (S4): the shader cache (lane `s4`)

**Files:** `src/dxmt/src/{shader_cache.rs,shader_cache/{tests.rs,abi.rs}}` (`abi.rs` generated, P2);
`src/dxmt/dxmt_shader_cache.{hpp,cpp}` (keeps `ShaderCacheVersion()`, P7, and `getInstance`'s map unchanged;
`ShaderCache` becomes a wrapper whose constructor calls `dxmt_shader_cache_create`, whose destructor calls
`dxmt_shader_cache_destroy`, and whose `getReader()`/`getWriter()` return proxies with `operator bool` and
`->get(key)`/`->set(key, …)`, so `d3d11_pipeline_cache.cpp` and `d3d12_shader_cache.cpp` are untouched).

**Interfaces:**
- Consumes: `winemetal_sys::{Obj, CacheReader_alloc_init, CacheReader_get, CacheWriter_alloc_init, CacheWriter_set}`
  and `--seam-abi` (Task 3), `dxmt_util::env::cache_exe_name` (step 3), Task 0's shader-cache golden.
- Produces (C):
  ```c
  void *dxmt_shader_cache_create(uint32_t metal_version, uint64_t version);  /* writer opened, then reader */
  void dxmt_shader_cache_destroy(void *store);  /* releases both: SQLite closes, as at today's exit */
  bool dxmt_shader_cache_has_reader(void *store);  bool dxmt_shader_cache_has_writer(void *store);
  obj_handle_t dxmt_shader_cache_read(void *store, const void *key, uint64_t len);  /* +1 DispatchData or 0 */
  void dxmt_shader_cache_write(void *store, const void *key, uint64_t len, obj_handle_t data);  /* data +0 */
  ```
- Produces (Rust): `shader_cache::Store::new(metal_version: u32, version: u64) -> Store` (boxed by the C ABI; `Drop`
  releases reader and writer), `read(&self, key: &[u8]) -> Option<Obj>`, `write(&self, key: &[u8], data: &Obj)`,
  `fn path(cache: Option<&str>, path: Option<&str>, cache_exe: &str, metal_version: u32) -> Option<String>` (pure:
  `dxmt_shader_cache.cpp:36-45`): the C++'s exact UTF-8 string built with `/` (`"dxmt/" + exe + "/"`, or the
  absolute value plus `/` when it lacks one, then `shaders_<v>.db`), never `Path::join`, which inserts `\` on
  `*-pc-windows-msvc` and so names another file on macOS. Writer, then reader, made once at `new` (the writer creates
  the database and its `cache_<v>` table; a `READONLY` reader opened first on a cold cache is nil for the process),
  each used under its own `Mutex`.

- [ ] **Step 1: RED.** `shader_cache::tests::{path_strings_are_the_cpps (exact strings: "dxmt//shaders_310.db" with
  the native empty exe name, "dxmt/Game.exe/shaders_310.db", "/x/cache/shaders_310.db" from "/x/cache" and from
  "/x/cache/", an empty or relative value falling back), cache_golden_replays (Task 0's `shader_cache/golden/
  matrix.txt` through `path` and the call-order fakes → `calls.txt`), writer_opens_before_reader,
  drop_releases_reader_and_writer, zero_turns_it_off, file_is_named_by_metal_version}`. Expected: FAIL. Mutation RED
  after GREEN: the reader opened first → `writer_opens_before_reader` and `cache_golden_replays` fail.
- [ ] **Step 2: GREEN** and the C++ cut; `--seam-abi` on `dxmt_shader_cache.hpp` → `shader_cache/abi.rs`, compiled
  on all five targets; `UT(dxmt-core)`; the four LBs; RL.
- [ ] **Step 3: Commit** `dxmt: the shader cache store in Rust`.

### Tasks 9-11 (lane R): the core's resources, written ahead (lanes `r1`-`r3`, on `step5w`)

Each lane: RED tests first, GREEN, `UT(dxmt-core)`, msvc builds of `dxmt-core` on the four targets, RL, commit
`dxmt: <what> (written ahead for the switches)`. Behaviour from the named C++; `debug_assert!` for its asserts (P15).
Consumes: `winemetal_sys::{Obj, Pool, …}` (Task 3), `winemetal-wire`'s mirrors, Task 2's stubs; R2 and R3 also R1's
types; logging through P5's `dxmt_core::log`.

- [ ] **Task 9 (R1): allocations, buffers, textures, views.** Files `src/dxmt/src/resource{.rs,/…}` from
  `dxmt_allocation.*`, `dxmt_buffer.*`, `dxmt_texture.*`. Produces: `BufferAllocation`, `TextureAllocation` (in
  `Arc`), `Buffer::{new, allocate(flags), allocate_over(flags, External { ptr, len }), current(), rename(),
  view(BufferViewKey)}`, `Texture::{allocate, import(mach_port), view(&TextureViewDescriptor), rename}`,
  `TextureViewDescriptor::key() -> TextureViewKey(u64)` (an explicit encoding: keys are internal, not the C++'s
  bitfield), `fn last_retained_seq(&self) -> u64` on both allocations, flags as bitflags, `guest-vm` memory through
  `WMTMemoryAlloc`. D3D9 hook: `External` is a struct (C4 adds its release notice); pages a front end owns stay
  distinct from `CpuPlaced`. Tests: `suballocation_stays_in_one_page`, `allocate_over_rounds_to_16k`,
  `view_keys_round_trip`, `distinct_descriptors_distinct_keys`, `retained_seq_is_the_latest`.
- [ ] **Task 10 (R2): rings, staging, dynamic pools, residency.** Files `src/dxmt/src/{ring,staging,dynamic,
  residency}{.rs,/tests.rs}` from `dxmt_ring_bump_allocator.hpp`, `dxmt_staging.*`, `dxmt_dynamic.*`,
  `dxmt_residency.hpp`. Produces: `RingBump::new(RingConfig { block: 32 MiB, lifetime: 300 })`, `allocate(seq,
  coherent, size, align) -> (BlockRef, u64)`, `free_blocks(coherent)`; `Staging::try_map(coherent, read, write) ->
  MapResult {Mappable, Renamable, Mapped}`; `DynamicBuffer`/`DynamicLinearTexture` with the `will_free_at` FIFO and
  the forwarder's lend/take-back pieces (B7); `ResidencyMask`. D3D9 hook: sizes are config values (32-bit memory
  profile, M5 G2). Tests: `ring_reuses_after_coherent`, `ring_grows_a_block_when_full`, `staging_state_machine`,
  `dynamic_fifo_frees_in_order`, `residency_dedups_per_encoder`.
- [ ] **Task 11 (R3): samplers, the internal library, capture, HUD, version text.** Files `src/dxmt/src/{sampler,
  library,capture,hud,info}{.rs,/tests.rs}`, `src/dxmt/build.rs` (copies `$DXMT_COMMAND_METALLIB`, or an empty file
  when unset, to `OUT_DIR`; `rerun-if-env-changed`) from `dxmt_sampler.*`, `dxmt_command*.{hpp,cpp}` (the library,
  `ClearRTV`/`ClearUAV`, depth-stencil blit, MV scale, tile barrier), `dxmt_capture.*`, `dxmt_hud_state.*`,
  `dxmt_info.*`. Produces: `trait SimpleCommandContext` (the 20 methods of `dxmt_command_context.hpp:30-58`),
  `ClearRtv<C>`, `ClearUav<C>`, `Library::new(device)`, `CaptureState::{next_action(frame), schedule(next),
  should_capture_next}` (its "DXMT capture enabled" WARN through `log`, P5), `Hud`, `version_text(api, feature_level)
  -> String`. Tests: `version_text_is_the_cpps` (strings read from `dxmt_info.cpp`), `capture_states_as_cpp`,
  `clear_dispatch_sizes`.

### Tasks 12-15 (lane E): D3D11's (and D3D9's) queue and encoder, written ahead (lanes `e1`-`e4`, on `step5w`)

Each lane as lane R. Map M1 §3.5 is the reference; no foreign call per command (map 0 §4). Consumes: lane R's
resources (R1) and pools (R2), S2's `Presenter` and scalers (E3, E4: on `step5w` only once it is rebased past Task
6's merge), Task 2's `Encoder` stub and `dxmt-util`'s `sync` stub.

- [ ] **Task 12 (E1): sync, the queue, the chunk arena.** Files `src/util/src/sync{.rs,/tests.rs}`,
  `src/dxmt/src/{queue,arena}{.rs,/…}` from `dxmt_command_queue.*`, `dxmt_command_list.hpp`, `util_cpu_fence.hpp`.
  Produces: `dxmt_util::sync::{wait(&AtomicU64, current: u64), wake_all(&AtomicU64), set_time_critical()}` (PE
  `WaitOnAddress`/`WakeByAddressAll`, `SetThreadPriority`; darwin `os_sync_wait_on_address`/
  `os_sync_wake_by_address_all`, priority untouched); `CpuFence`; `Queue::new(device: &Obj, QueueConfig { chunks: 32,
  cpu_heap: 4 MiB, gpu_heap: 4 MiB, encoder_heap: 1 MiB, max_latency: 3 })`, `current_seq`, `coherent_seq`,
  `chunk() -> &mut Chunk` (one recording thread, `dxmt_command_queue.hpp:245-250`), `commit`, `wait_cpu(seq)`,
  `present_boundary(wait)`, `set_max_latency`, `shared_event`, threads `dxmt-encode-thread`/`dxmt-finish-thread` at
  time-critical priority plus the event listener; `Chunk::{emit<F: FnOnce(&mut Encoder) + Send>(f), retain(alloc),
  allocate_cpu(size, align)}` placed in a bump arena with a monomorphized trampoline (no `Box` per command);
  `CommandList::emit_fn<F: Fn(&mut Encoder) + Send>` for deferred lists. D3D9 hooks: the config's values are D3D9's
  memory profile; `wait_committed(seq)` is addable beside `wait_cpu`. Tests: `arena_drops_each_command_once`,
  `arena_runs_in_order`, `list_replays_twice`, `ring_blocks_at_31_in_flight` (the counters as a pure `Ring`),
  `wait_wakes_on_another_thread`.
- [ ] **Task 13 (E2): binding, access tracking, argument buffers.** Files `src/dxmt/src/{encoder,deptrack}{.rs,/…}`
  (binding and `access`/`track`), `src/winemetal/wire` (the reflection mirror pair via the generator) from
  `dxmt_context.hpp:334-597, 863-888`, `dxmt_context.cpp:88-465`, `dxmt_deptrack.*` (D3D11's `BindingSet` stays 5b's).
  Produces: `Encoder::bind_*` by `Stage` (const generic) and index taking handles and core resources (never a D3D11
  type; a bytes bind addable for D3D9, 5-D13), `access::<S>`/`track::<S>`, `FenceSet` (4 parities × 64 lanes) over a
  pool of 256 fences, `GenericAccessTracker`, `encode_{vertex_buffers,constant_buffers,shader_resources,
  stream_output}` from a reflection slice. Tests: `fenceset_ops_as_cpp`, `tracker_read_after_write_waits`,
  `locality_check_drops_local_waits`, `argument_slots_from_reflection`.
- [ ] **Task 14 (E3): passes, flush, the optimizer** (after E2 and Task 6's merge). Files
  `src/dxmt/src/encoder/{pass,flush,optimize}.rs` (+ tests) from `dxmt_context.cpp:639-1420`. Produces:
  `start_render_pass/compute/blit`, `end_pass`, `encode_render_command<T: Record>` and the compute/blit forms (records =
  `winemetal-wire`'s `Wmtcmd*`), `flush(cmdbuf)` (backward reordering, clear and resolve merging, encode through
  `*_encodeCommands`, present through S2's `Presenter`, upscale through S2's scalers, events, timestamps, the queue's
  event signal). D3D9 hook: pass policies (Rule B) addable as an option. Tests: `optimizer_merges_clear_into_render`,
  `optimizer_refuses_across_dependency`, `records_keep_their_order`.
- [ ] **Task 15 (E4): queries, initializer, counters, emulated commands.** Files `src/dxmt/src/{query,initializer,
  counter,emulated}{.rs,/tests.rs}` from `dxmt_occlusion_query.hpp`, `dxmt_resource_initializer.*`,
  `dxmt_counter.*`, `EmulatedCommandContext` (`dxmt_command.hpp:41-258`). Produces: visibility and timestamp queries
  with per-chunk readbacks, `ResourceInitializer` (own queue, waited by the next chunk), `CounterPool`,
  `EmulatedContext` (compute clears, GS/TS marshal). D3D9 hook: query result slots by offset, polled by sequence.
  Tests: `visibility_offsets_bump`, `readbacks_sum_per_query`, `counter_pool_reuses`.

### Tasks 16-19 (lane D): D3D12's pure helpers, written ahead (lanes `d1`-`d4`, on `step5w`)

Each lane as lane R, on the `d3d12` crate (`UT(d3d12)`, its msvc builds on all four targets: 5-D9), its D3D12 structs
in its own `src/d3d12/src/abi/<x>.rs` with size/offset asserts per target against a throwaway C program on
llvm-mingw's sysroot and on `include/native` (step 3 D8). Root-signature parsing is not here (5-D6: `airconv-ir`). Consumes:
Task 0's D3D12 goldens, Task 2's `d3d12` stubs; D3 also `dxmt_core::format` (Task 7); D4 also `winemetal-wire`'s
sampler mirror.

- [ ] **Task 16 (D1): `LegacyBarriers`.** `src/d3d12/src/barrier{.rs,/tests.rs}` from `d3d12_barrier_legacy.hpp`
  (the forwarder keeps the header, 5-D7). Test `barrier_table_is_the_golden` over Task 0's table; mutation RED.
- [ ] **Task 17 (D2): root-signature serialization and qword offsets.** `src/d3d12/src/root_signature{.rs,/tests.rs}`
  from `d3d12_root_signature.cpp:42-245, 351-` (`SerializeVersionedRootSignatureImpl`,
  `CalculateRootSignatureQwordOffsets`). Tests `serialized_blobs_are_the_golden` (Task 0's 12 descs, built in the test),
  `qword_offsets_as_cpp`, `bad_desc_errors_as_cpp`.
- [ ] **Task 18 (D3): resource math and feature answers** (after S3's merge). `src/d3d12/src/{resource_math,feature}
  {.rs,/tests.rs}` from `d3d12_resource_helper.cpp`, `d3d12_device.cpp:1169-1173, 1335-1395, 1677-` and its
  `CheckFeatureSupport`, format support from `dxmt_core::format`. Tests `footprints_are_the_golden`,
  `allocation_info_is_the_golden`, `features_are_the_golden`, `validate_descs_as_cpp`.
- [ ] **Task 19 (D4): samplers and the pipeline stream.** `src/d3d12/src/{sampler,pipeline_stream}{.rs,/tests.rs}`
  from `d3d12_sampler.cpp:70-213` and `ParsePipelineStream` (`d3d12_device.cpp:47-`). Tests
  `static_and_dynamic_sampler_infos_as_cpp`, `stream_subobjects_parse_with_alignment`, `unknown_subobject_fails_as_cpp`.

### Task 20: the end-of-5a gates, once (owner)

Every row once on `step5a`'s head with S1-S4 merged, as one or more lock jobs in this order. Each job's `sh -c` first
checks `$D` out detached at `step5a`'s head and `<M>` out detached at `step5a-mn` (`git -C <M> checkout -q --detach
step5a-mn`: the branch itself is checked out in `$S/wt-mn`), and its last command (a `trap … EXIT`) checks `<M>` back
out to `dxmtrs`, so step 4's jobs between ours run `dxmtrs`'s scripts (pipeline rule 1; rule 6 keeps VM jobs on the main
checkout's `eac-vm/builder.sh`, so a worktree run is not enough). A failing row is recorded, not fixed (Task 21). Lanes
idle during the timed rows (P11). Environment: `MN_STATE`, `MN_REF_WINE_APP=$S/ref-wine.app`, the signing variables;
`MN_REF_FROZEN` per row as named. Commands as step R's Task 9 and step 3's Task 13 run them (their reports name the
forms that worked); Task 0 Step 1 ledgered any that moved. Reports `$S/reports/gates-{mac,native,vm,rust}.md` in step
2's shape. Tree clean after each build and gate; cdhash recorded.

- [ ] **Mac.**
  - `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot` → "development build",
    "staged … (dev)"; the key = Task 0's. Identity: `codesign -dv` names wine.app's cdhash (runtime=dev); wine.app's
    `d3d11.dll` and `d3d12.dll` import `api-ms-win-core-synch-l1-2-0` (Rust's std; the C++'s didn't), so the Rust is
    what runs.
  - `sh dxmt/check.sh` → the ok/FAIL row set of `$S/ref-perf/check-x64.txt`, 0 FAIL; ARM64EC:
    `MACNEUTRON_ARM64_TESTS=build/dxmt-tests-arm64ec
    MACNEUTRON_ARM64_LOOP=$PWD/build/dxmt-tests-arm64ec/present_loop.exe DXMT_CHECK_WORK="$TMPDIR/macneutron dxmt
    arm64ec" sh dxmt/check.sh` → `check-arm64ec.txt`'s set. Flaky rows (d3d11/d3d12 `bg-leave`, `ts-many`, the gamma
    rows while someone uses the Mac): one re-run each, ledgered.
  - `sh wine-arm64/check.sh dxmt dxmt-present dxmt-release` → PASS lines (incl. `present_loop cycles=20 ok`).
  - `sh presenter/check.sh` → 66 ok, 0 FAIL.
  - `sh dxmt/mtld3d-facade-test.sh` → 8 passed, 0 failed (facade); `sh dxmt/wm-probe-check.sh` and `--full` → 11 ok.
  - Step 2's probe rows as step R ran them: `cover.sh table <host> <build-tree winemetal.so>` → "158 entries, wow64
    153"; `cover.sh slots` ok; `cover.sh cases` (all groups) ok.
  - Step 3's `full` A/B (its `run.sh` and `full-diff.sh`, both lanes, and natively against `$S/ref-native`) → no
    unmasked difference outside `NOT_ON_C` cases.
  - D3D9: `sh dxmt/mtld3d-build.sh --loopback`; `MTLD3D_SO=<it> sh dxmt/d3d9-check.sh` and `MTLD3D_PE=$SRC/dxmt-install/
    aarch64-windows sh dxmt/d3d9-check.sh` → `d3d9-check: ok`; `sh dxmt/mtld3d-conformance.sh` and `MTLD3D_SO=<it> sh
    dxmt/mtld3d-conformance.sh` → step R's report's figures ("only triaged sites … passed").
  - `wine-arm64/tests/{mode,licences,translator_key}_test.sh` ok; `sh wine-arm64/bundle.sh --release --version 9.9.9
    --out $S/release-check` rc 0; `rm -rf $S/release-check`.
- [ ] **5a's own rows.** `stats-golden.sh` on the gate's wine.app → `stats-compare.sh` against Task 1's →
  `stats-compare: equal` (the 6 s `d3d12_clear <n>` run's `every 5 s` header parsed by `perf.py`'s `fps()`); `hist.sh`
  → `hist-compare: equal`; the spatial-scaler run → equal to `$S/ref-perf/spatial.txt`; check.sh's cache, clear,
  capture, version, DLSS and fullscreen rows (Review Focus) ok in the logs above; the format, presenter,
  shader-cache, barrier and D3D12 goldens' tests in RC.
- [ ] **Native and VM host.**
  - `sh eac-vm/dxmt-native/build.sh` rc 0; `nm -gU` of `install/lib/d3d11.dylib`, `d3d12.dylib` = P16's lists;
    `nm -ap | /usr/bin/grep -c ' OSO '` and `LC_ALL=C /usr/bin/grep -ac "$HOME"` 0 on both; `winemetal.dylib` exports
    the `_wmt_native_*` set (Task 3).
  - `sh eac-vm/dxmt-native/tests/build.sh` → `build: ok`; `tests/hook.sh` → `hook: ok`; `tests/win32.sh` → `win32: ok`.
  - `sh eac-vm/dxmt-native/gate.sh` → `dxmt-native gate: all 31 programs passed` (its `build-tests.sh`: `tests: ok`).
    Native thread names: the `d3d12_list7 order` run `build-tests.sh` makes, with `DXMT_STATS=1` and a temporary
    `DXMT_DXIL_DUMP`, against `install/lib` and again with `DYLD_LIBRARY_PATH=$S/ref-native` → the same set of
    `^thread <name> (<id>): N us in D3D12` names, at least one not `thread`.
  - `sh dxmt/wm-probe-check.sh --native build/dxmt-native/install/lib/winemetal.dylib` → 11 ok.
  - `sh eac-vm/host/build.sh`; identity: the VM host's `build/eac-vm-work/dxmt/{d3d11,d3d12}.dylib` sha256 equal
    `install/lib`'s; `make -C eac-vm/d3d test stubs dispatch-test` ok; `sh eac-vm/metal/gate.sh` → all passed.
  - `sh eac-vm/d3d/loopback-gate.sh` → all 31 passed, its lines equal step 3's reference except times.
  - The forwarder's own checks (the native export lists changed): `sh eac-vm/d3d/tests/answers.sh` → `answers: ok`;
    `sh eac-vm/d3d/both.sh wire` → `wire: ok`; `sh eac-vm/d3d/tests/identity.sh` → `identity: ok`; `sh
    eac-vm/d3d/tests/wrapped.sh` → `wrapped: ok`; `sh eac-vm/d3d/tests/pieces.sh` → `pieces: ok`; `sh
    eac-vm/d3d/threads.sh` → `threads: ok`.
- [ ] **VM.**
  - Builder up with `VM_SHARES`; `sh eac-vm/dxmt/build.sh` (both widths); `sh eac-vm/dxmt/check-build.sh` → ok;
    `sh eac-vm/d3d/build-guest.sh`; `sh eac-vm/image/guest-stages.sh dxmt d3d`; `sh eac-vm/builder.sh sync
    eac-vm/image mn-image`; `sh eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`. Identity: `dxmt-guest/.mn-source`
    names `step5a`'s head without `+dirty`; the staged guest `d3d11.dll` and `d3d12.dll` (both widths) equal
    `dxmt-guest-build{,32}`'s by sha256.
  - `MN_REF_FROZEN=$S/ref-vm sh eac-vm/dxmt/gate.sh` → `dxmt gate: all 45 programs passed`, its lines equal the
    reference except times.
  - `sh eac-vm/tests/d3d-smoke.sh` → `d3d-smoke: ok` (both `32:` blocks of 14 rows: `rawexport`, `warp`, `wmsize`,
    `conv`); `MN_WOW64=new sh eac-vm/tests/d3d-smoke.sh` → ok (i386 Rust DLLs under new WoW64).
  - VM stats row (the only run of the guest's stats path: i686 `thread_local!` through `__tls_array`, `rdtsc`, the
    kernel32 declarations of `stats/platform.rs`): d3d-smoke's guest-mode script (`$W/d3d-smoke-guest.sh`) cut to its
    `list7.exe` and `list7_32.exe` lines, each run with `MACNEUTRON_VM_D3D=guest DXMT_STATS=1
    DXMT_DXIL_DUMP=Z:<share>/stats5a-<width>`, through `sh eac-vm/run-guest.sh` as d3d-smoke runs its own → both
    exit 0; each `stats.txt` holds an exit header, a `counters:` block, the `encoder boundaries` line and at least one
    `thread <name> (<id>):` line.
  - `sh eac-vm/tests/dxgi-guest.sh i386` → `dxgi-guest i386: ok`; `sh eac-vm/tests/dxgi-guest.sh x86_64` →
    `dxgi-guest x86_64: ok`.
  - `sh eac-vm/d3d/gate.sh` → `d3d gate: all <n> programs passed` (`d3d10_core`, `d3d10_wine`, `d3d11_api` rows; `<n>`
    as step 3's report).
  - `sh eac-vm/dxmt/d3d9-gate.sh` → `d3d9 gate: ok`.
  - `MN_REF_FROZEN=$S/ref-present sh eac-vm/dxmt/present-gate.sh` → `present gate: all passed`;
    `MN_REF_FROZEN=$S/ref-present sh eac-vm/dxmt/present-gate.sh forward` → `present gate: all passed` (the only
    pixel check of the darwin Rust presenter's drawable).
- [ ] **Identity (P16), install to install.** `ident-diff.sh <Task 1's reference> <installed> ED1 ED3 ED5 ED10` (as
  step R's identity rows; ED5 run once without first, a resource byte difference ledgered) → nothing, for
  wine.app's `d3d11.dll`/`d3d12.dll` against `$S/ref-wine.app`, the guest's (both widths) against `$S/ref-guest`, the
  native dylibs against `$S/ref-native`; every other DXMT binary equal under ED1 ED4 (remapped paths, section sizes);
  `LC_ALL=C /usr/bin/grep -ac "$HOME"` 0 on the Mac, guest and native `d3d11`/`d3d12` (panic locations embed source
  paths); key equal Task 0's; `DXMT/version` `<sha>+dev`.
- [ ] **Performance** (no game, step 2 D11's set, each once): `present_loop` frame time, the DLSS cost row, check.sh's
  timing lines, against step 3's recorded figures; a row over 5% gets one control on `$S/ref-wine.app` in the same
  session before it counts (step 3 P16).
- [ ] **p profile** (last): `MACNEUTRON_PROFILE=p` Mac (`make wine-arm64`), guest (`sh eac-vm/dxmt/build.sh`) and
  native (`sh eac-vm/dxmt-native/build.sh`, then `sh eac-vm/host/build.sh`) builds rc 0; on their
  `d3d12.dll`/`.dylib`: no `dxmt_stats_`/`dxmt_dump_` symbol, the stats strings count 0 (Task 5's form);
  `release/release.sh`'s `check_no_traces` (its function and `DXMT_DEBUG_PROBE`, the line Task 0 ledgered, `:899` when
  written) on the p VM host's `d3d12.dylib` passes. Then rebuild std (Mac, guest, native, host).
- [ ] **Rust and licences.** RC → `rust-check: ok`, `audit: clean (<n>)`; layouts, protos, `decls.rs` and the six seam
  `abi.rs` regenerate equal; `cargo metadata` licences of every shipped package per target (no Apache-2.0-only);
  notices legs cover `d3d11`, `d3d12`; `diff -r` of wine.app's `licenses/` against `$S/ref-wine.app`'s → only the
  new legs' lines.

### Task 21: one fix pass (owner, lanes as needed)

- [ ] **Step 1:** per failing row: root cause, a failing test or table row first, the fix in the owning file (DXMTRS
  on `step5a` or a lane, MacNeutron on `step5a-mn`), the task's own checks, the row re-run once, and every gate whose
  inputs the fix touched, named per file in the ledger. A defect needing a second round goes to the maintainer.
- [ ] **Step 2:** commit each fix (`<area>: …`); key and RC again after the last.

### Task 22: spec, push, pin, the W hand-off (controller)

**Files (MacNeutron):** the spec, `dxmt/pins`, the ledgers, `.superpowers/sdd/dxmtrs-program.md`; the memory note
`framegen-adaptive.md` (P8).

- [ ] **Step 1: Spec** (5-D11), on `dxmtrs`: §5 step 5 gains its order as decided (S5 dropped: the queue and the
  encoding context go live with D3D11's switch; allocators, staging and residency have no live seam and go live
  inside each DLL at its switch; frame generation and pacing stay D3D12's until its switch; 5a done, with the DXMTRS
  sha); §6's header list and paths (5-D7); the key rule after step 4's switch (5-D6, 4-D4). Privacy-scan spec, plan,
  ledger. Commit `Spec: DXMTRS step 5a, the core's crates and shared seams`.
- [ ] **Step 2: DXMTRS main.** `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c 'git -C $D checkout -q --detach
  $(cat $B/wine-arm64-src/dxmt.applied)'` (pipeline rule 5's restore, a checkout in `$D`, so inside the lock: rule 1);
  privacy-scan `git -C $D log -p main..step5a`; `git -C $D fetch . step5a:main` (refuses anything but a
  fast-forward, touches no checkout), `push --dry-run origin main`, push. Remove S worktrees and lane branches (`git
  cherry` empty).
- [ ] **Step 3: Pin, one commit.** `git merge --squash step5a-mn` on `dxmtrs`, `DXMT_COMMIT=<main's sha>`; commit
  `DXMT: the core's crates and shared seams in Rust (DXMTRS <sha>)`.
- [ ] **Step 4: Pinned build** (one lock job): `make wine-arm64` re-fetches the pin; `DXMT/version` = `<sha>+<series>`;
  `git -C $D status --porcelain` empty; the key; `ident-diff.sh` of `d3d11.dll`/`d3d12.dll` against Task 20's → equal
  but the version strings; x64 check.sh 0 FAIL. Push URL reset to `git@github.com:Mac-Neutron/DXMTRS.git`.
- [ ] **Step 5: MacNeutron main.** Fast-forward `main` to `dxmtrs`, `push --dry-run`, push; a failed fast-forward here
  or in Step 2 stops for the maintainer.
- [ ] **Step 6: W hand-off.** Rebase `step5w` onto the pushed `main`; when its last W lane has merged, RL on it →
  `rust-check: ok`; ledger its head as 5b's and 5c's base. Correct the memory note (P8); program ledger: 5a done, the
  pinned sha, `step5w`'s head.

## Review record

Three adversarial Opus reviews (5-D14: `build/step5/map/plan-review-5a-{seams,tests,exec}.md`), 2026-10-30. Each
finding was checked against DXMTRS da6ed2d, `stepR-layout` dcf027f and MacNeutron `dxmtrs` before it was applied;
none was rejected.

1. Critical, `dxmt_*` C ABI unchecked per target (i686 cdecl, `WMTColorSpace` is `enum : uint64_t`,
   `winemetal.h:1906`): applied. P2's `dxmt-abi` block, Task 3's `--seam-abi` (generator form, run by each S lane
   into its own `abi.rs`, all five targets), `cs` stated `u64`, S2's i686 check named (d3d-smoke's guest `32:`).
2. Critical, shader-cache path as `PathBuf` (`\` on msvc; `dxmt_shader_cache.cpp:39-45` builds with `/`): applied.
   `path()` returns the C++'s exact `String`; exact-string tests.
3. Important, writer must open before reader (`g8.rs` reader `READONLY`, writer creates the table): applied. Order
   stated, `writer_opens_before_reader`, golden order lines.
4. Important, leaked stores leave the WAL open at exit: applied. `dxmt_shader_cache_create`/`_destroy` owned by the
   unchanged `getInstance` map; the Rust static dropped.
5. Important, presenter lock discipline (wait under the state lock deadlocks; `frame_done` max): applied. Task 6
   Behaviour, `sync_wait_does_not_block_encode_state`, `frame_done_never_moves_back`.
6. Important, exceptions through Rust frames: applied. (a) `noexcept` trampolines in P4; (b) new P19 keeps spec
   D4/P11 natively (`C-unwind`, `production-unwind` for the native staticlibs, abort-on-panic guards; PE unchanged).
7. Important, `GammaRamp` as plain fields races dxgi's atomic stores (step 3 plan :874): applied. Moved verbatim;
   Acquire/Relaxed read order; the LUT test builds ramps through the atomics.
8. Important, `dxmt_stats_on` as a Rust data symbol under ARM64EC's two archives: applied. Flag stays C++'s
   `g_stats_on`, `dxmt_stats_init(bool on)`; map P4's commit dropped (phase P is two commits); P4 states the rule.
9. Important, format golden misses swizzled formats, DXGI ids past 191 and zero-capability keys: applied (Task 0
   Step 3, Task 7: raw `u32`, `.get()`).
10. Important, S1's TLS path never runs in the VM: applied (Task 20's VM stats row, `list7.exe`/`list7_32.exe`).
11. Critical, spatial scaler ungated (`DXMT_METALFX_SPATIAL_SWAPCHAIN`, set by nothing): applied as a recorded run
    (Task 1 Step 3, Task 6 Step 4, Task 20). Not added as a `step5a-mn` check.sh row: the recorded pair gates it in
    5a; a permanent row is 5b's to add if wanted.
12. Critical, presenter values unchecked (histograms count types): applied. Task 0 Step 4's host harness and matrix,
    Task 6's `golden_replays`, `present_metadata_as_cpp`, mutation "delete the 0.8", the cut re-run.
13. Important, VM gates re-record their reference from the Rust build: applied. `$S/ref-vm` (45) and `$S/ref-present`
    in Task 1; `MN_REF_FROZEN`/`MN_REF_WINE_APP` in Task 20; `present-gate.sh forward` added.
14. Important, `MakeCaptureFolder` semantics (F9's loop): applied. Behaviour stated; three tests; mutation
    `create_dir().is_ok()`.
15. Important, stats comparator too loose and report rules untested: applied. Per-field-class shapes, `snprintf`-built
    expectations, four tests, the 6 s `d3d12_clear <n>` run.
16. Important, guest stats paths and native thread names unchecked: applied (merged with 10; native thread-name set
    against `$S/ref-native` in Task 20).
17. Important, clamp inputs f32-exact, four of five desc fields, C++ forwards unrecorded: applied (Task 0 Step 3's
    inputs as f64 bits, five fields; Task 7 Step 2 re-runs the harness on the cut).
18. Important, default relative cache path pinned only by a lane test: applied. Task 0 Step 5's env-matrix golden,
    Task 8's `cache_golden_replays`; the optional user-cache gate row not added (standing rules keep gates out of the
    user cache folder).
19. Important, references only for the Mac and never gated: applied. Task 1 Step 3 builds guest and native refs and
    runs both check.sh lanes on the reference; stats-golden and hist fail on empty output.
20. Important, Task 20 drops rows R and 3 run: applied (new-WoW64 d3d-smoke, the forwarder's seven checks, `$HOME`/OSO
    scans, the native p build for `check_no_traces`, `dxmt-release`).
21. Important, stale 44, line-number row names, missing Expected lines: applied (45; rows by `expect` label in Review
    Focus; Expected lines copied from step R's and step 3's gate blocks; comparators print success lines).
22. Critical, `--decls` wrong on darwin (`DLLImportAttr` filter, `long:i32`, no `WMTWsi*` mirrors) and `decls.rs`
    missing from rustfmt's ignore: applied. `--decls` = the `--protos` set, native-only functions hand-declared with
    Apple-clang checks, dxgi keeps its `WMTGetWsiHooks`, Task 2 owns `rustfmt.toml`.
23. Critical, `tests/build.sh` builds `$D` into the shared folders: applied. Task 2 Step 4 runs its loop on the lane's
    dylibs; the script stays in Task 20.
24. Important, Rust DWARF puts OSO stabs and `$HOME` in the native dylibs: applied (`-Wl,-S` expression in Task 2's
    meson, P16's "OSO 0, `$HOME` 0", checks in Tasks 2 and 20).
25. Important, `ident-diff` has no ED for P16 and refs per kind are missing: applied (all four `ident*` files, ED10
    with a mutation RED, `$S/ref-lb` LB-to-LB in Task 2, install-to-install in Task 20).
26. Important, `<M>` on `step5a-mn` and Task 22's restore outside the lock: applied. Each Task 20 job checks `<M>` out
    and back (rule 6 rules out the worktree form for VM jobs); Task 22 Step 2 inside `lockf`; Lock column marked.
27. Important, E3 needs S2 on `step5w`: applied (E3/E4 depend on Task 6's merge; `step5w` rebased past it first).
28. Important, 5-D8's native exports needed by 5b and 5c in parallel; P13 misstated 5-D8: applied (Task 3 exports
    the `wmt_native_*` set from `win32_event.c`; P13 and P14 reworded).
29. Important, manifests and nvapi/dxgi files without an owner: applied (Task 2 writes final manifests and the
    rustfmt ignore; Task 3 lists nvapi's and dxgi's `Cargo.toml` and lock entries; `ffi.rs` re-exports so
    `disp.rs`/`gpu.rs` stay untouched; Task 0's overlap test covers nvapi and nvngx).
30. Important, rebase check misses MacNeutron's pin bump and lines: applied (Task 0 Steps 1-2: R's pin bump required,
    RL and audit once on `wt-base`, MacNeutron lines re-read, `dxmt_dep` users, the rebased head recorded).
