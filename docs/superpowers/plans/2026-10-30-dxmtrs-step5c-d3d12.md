# DXMTRS step 5c: the D3D12 track — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `d3d12.dll` (Mac ARM64EC, guest x86_64 and i686) and the native `d3d12.dylib` the VM host loads become whole
Rust binaries in one switch: D3D12's front end (devices, command lists, allocators and queues with D3D12's own encoder
and queue, descriptor heaps, root signatures and the translation cache, resources, heaps and residency, enhanced
barriers, the swap chain) on 5a's core, with frame generation and pacing moved into the core and DXMT_STATS called
directly, every outside contract kept and pinned first by goldens recorded from the C++.

**Architecture:** Four groundwork tasks land on `step5w` what D3D11's track needs too (the `SM50*` declarations, the
native not-implemented flag, generated COM vtables, the two-count COM object, private data, the core `Fence`, the
translation cache's instances and version, WSI). The C++ is then characterized: a COM-level probe (`d3d12_probe`,
goldens on both Mac lanes, natively and on i386) and a reference corpus (stats, `wmtcmd` histograms, recordings, dump
folders, logs, caches, a real game's pipeline recording). On the integration branch `step5c`, after one `airconv-ir`
scaffold commit (P3), two skeleton tasks (7a: the build, the meson flip and the complete shared files; 7b: every pub
signature final with stub bodies and the device's dispatch) lay down the `d3d12` crate; twelve lanes then own disjoint
files, device creation (16a) merged first so every later lane runs programs on its own DLL; integration runs the whole
D3D12 surface against the goldens on private copies of the reference runtime and once in the VM and natively; one
commit deletes the C++, and the gates run once.

**Tech Stack:** Rust 1.99.0 (staticlibs for `aarch64`, `arm64ec`, `x86_64`, `i686-pc-windows-msvc` and
`aarch64-apple-darwin`), meson, llvm-mingw, `jq` over clang's AST (the vtable generator), C++17 (the probe, MacNeutron),
winemetal's C API, POSIX sh, `python3 -I` (scratch comparators).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (approved, with the 2026-10-30 revisions: §3 layout after
step R, §5 step 5 and the Pace paragraph, §6, §7, §8). Inputs: `build/step5/map/0-porting-map.md` (main input, `M0`),
`1-core.md` .. `5-d3d9-needs.md` (`M1`..`M5`; `3-d3d12.md` is `M3`), `6-critic.md` (accepted), `7-decisions.md`
(5-D1..5-D14, binding); plan 5a (`2026-10-30-dxmtrs-step5a-core-seams.md`: the seams, the W lanes on `step5w`, the kit
and Task 20's gate block this plan reuses); plan 4a (`2026-10-30-dxmtrs-step4a-translator.md`: `airconv-ir`'s API in
Task P1's table, the key at Z, P7); `build/step4/map/7-decisions.md` (4-D7: the game captures); step 3's plan (the
probe, the kit `run.sh`/`lane-app.sh`/`lane-dylib.sh`, `dxgi-guest.sh`); step R's plan; `build/pipeline.md` (the lock);
`build/step2/reports/gates-{mac,native,vm}.md` and, once they exist, step R's, step 3's and 5a's gate reports.

**Rebase note.** Written ahead on 2026-10-30 against DXMTRS da6ed2d (the C++, cited as `src/d3d12/<file>:<line>`), step
R's layout as `stepR-layout` dcf027f has it, step 3's and 5a's plans assumed landed (`src/dxgi`, `src/dxmt/src/**`,
`src/d3d12/src/{barrier,root_signature,resource_math,feature,sampler,pipeline_stream,abi/*}` from 5a's lane D on
`step5w`) and 4a's `airconv-ir` API as its Task P1 states it. Task 0 Step 1 records every head actually rebased onto
and re-reads every path and line cited here, ledgering each that moved.

Paths: `<M>` = `~/Documents/MacProton`, `$B` = `<M>/build`, `$D` = `$B/wine-arm64-src/dxmt`, `$S` =
`$B/step5c`, `$W` = `$B/eac-vm-work`, `$A` = `$B/wine-arm64/wine.app/Contents/Resources`, `$R` = `~/.rustup/toolchains`,
`C/` = `src/d3d12/` in a DXMTRS tree. Ledger: `.superpowers/sdd/2026-10-30-dxmtrs-step5c-d3d12/progress.md`.

## Decisions

Binding (`build/step5/map/7-decisions.md`), short form:

- **5-D1** Three plans: 5a (seams, W lanes), 5b (D3D11, one switch), 5c (this: D3D12, one switch of `d3d12.dll`; frame
  generation moves into the core at that switch; DXMT_STATS's counters were ported by 5a's S1 and this plan's Rust
  calls them directly, dropping S1's C ABI). 5b and 5c run in parallel after 5a's seams.
- **5-D2** S5 dropped: D3D11's queue goes live with D3D11's switch (not this plan's).
- **5-D3** P2 dropped (native dummy cbuffer: D3D11's, not touched here).
- **5-D4** The D3D12 heaps get a real overflow check that logs and aborts (maintainer 2026-10-30, P15).
- **5-D5** The drawable prefetch stays off.
- **5-D6** D3D11's switch rides a key-moving pin; after step 4's Z the key hashes `airconv`'s resolved dependencies, not
  the root `Cargo.lock`; 5b/5c take the DXBC parser and root-signature deserializer from `airconv-ir`, never the C++.
- **5-D7** The forwarder's headers stay at their paths (`d3d12_interfaces.hpp`, `d3d12_barrier_legacy.hpp`,
  `d3d12_shared_fence.hpp`, the util WSI files).
- **5-D8** Natively `win32_event.c` exports the Win32 calls the Rust front ends need (5a Task 3 did).
- **5-D9** The i386 `d3d12.dll` is built, staged and gated (d3d-smoke `list4`, `layout3`); every check covers it.
- **5-D10** Per task only its own checks; full gates at D3D12's switch, then its own pin and push.
- **5-D11** Spec amendments with each landing.
- **5-D12** The D3D11 milestone bench is 5b's question.
- **5-D13** The core's API stays shaped for D3D9 too (`5-d3d9-needs.md`).
- **5-D14** Three adversarial Opus plan reviewers.

Plan rulings (each: decision — why — cost if wrong):

- **P1 Branches.** Groundwork (Tasks 1-4) commits on `step5w`, 5a's integration branch, so 5b starts from it too.
  `step5c` branches from `step5w` after Task 4; lanes `step5c-<lane>`; MacNeutron `step5c-mn` from `dxmtrs`. `step5c`
  reaches `main` only at Task 21, fast-forward, history kept: commits between the skeleton (Tasks 7a, 7b) and the switch
  (Task 18) build but do not pass every D3D12 program; MacNeutron never pins them. — 5a P1's integration model; one
  gated landing. — a bisect across those commits on `main` meets a partial D3D12 (ledgered).
- **P2 The 5b handshake.** Groundwork items both tracks need are this plan's Tasks 1-4. If 5b's plan exists when Task 0
  runs and already owns an item, this plan consumes 5b's and drops that step (ledgered); if 5b has branched before
  Tasks 1-4 land, the controller tells 5b's controller to rebase past them (new files, plus one `pub mod`/`[dependencies]`
  line each in `src/dxmt/src/lib.rs`, `src/util/src/lib.rs`, `src/winemetal/sys/src/lib.rs`). — Both switches need
  them; neither may wait on the other's lanes. — one cross-plan coordination point.
- **P3 `airconv-ir` and step 4's Z.** `airconv-ir` stays off `main` until 4a's Z (4-D6), and before Z the key hashes
  `src/airconv` wholesale, so no early landing of it is key-neutral. Tasks 9 and 10 (the only users: container parse,
  input signatures, root-signature deserialization) code against `step4a-translator`'s `airconv-ir` through ONE
  scaffold commit on `step5c` before Task 7a (`scaffold: airconv-ir from step4a-translator <sha>`), the owner's: `git
  checkout <4a P1's sha> -- src/airconv/ir`; one `members`, one `default-members` and one `[workspace.dependencies]`
  entry in the root `Cargo.toml` (no `[profile.*]` edit); `Cargo.lock` from one `cargo metadata --offline`; and, only if
  `airconv-ir`'s `float_cast.rs` carries a suppression, 4a T1's one `ALLOW_SITES` line for it in `tools/audit.sh` (RL's
  audit leg fails without it). Nothing else: not 4a's T1 commit (it adds the LLVM-bound `airconv` crate, whose
  `build.rs` and `rust-check.sh` then require `AIRCONV_LLVM`/`AIRCONV_XCRUN`, and edits `Cargo.toml`, `Cargo.lock`,
  `audit.sh`, `winemetal-layouts.sh`, CONVENTIONS and README beside 5a's and this plan's edits), no other tool or docs
  edit. Its own check: `cargo metadata --locked --offline` passes (`airconv-ir`'s dependencies are in the offline cache)
  and RL → `rust-check: ok`. Every lane branches past it, so Task 7a's manifest is final and no lane edits
  `Cargo.toml`/`Cargo.lock`. Task 17 Step 1 drops exactly this commit when `step5c` rebases onto a `main` that has Z.
  Key: before Z the key hashes `src/airconv` wholesale, so every commit from the scaffold on reads the scaffold's key,
  ledgered as the baseline for all of them; after Task 17 Step 1 the baseline is `main`'s post-Z key (composition 2,
  4a P7). The switch lands after Z. — 5-D6 forbids the C++ copies; Z is the only key-neutral way in. — Task 7a waits
  for 4a's P1 on `step4a-translator`; 5c's switch waits for Z (finding 1).
- **P4 What `step5w` brings to `main`.** 5c is expected to switch first (M0 §3.4), so its landing takes all of
  `step5w`: lanes R and D (used) and lane E (D3D11's queue and encoder, no D3D12 caller). E's code is host-tested by
  RC and removed from the shipped images by the linker (`/OPT:REF` on PE; Task 7a adds `-Wl,-dead_strip` natively). If
  5b lands first, nothing here changes. — Splitting `step5w`'s linear history is surgery on 5a's integration branch. —
  lane E sits on `main` without a live caller until 5b's switch.
- **P5 The meson flip comes early, on `step5c`.** Task 7a makes `C/meson.build` build `d3d12.dll`/`d3d12.dylib` from the
  crate's staticlib and `d3d12.def` (nvngx's and step 3's dxgi recipe; natively 5a P16's `-exported_symbol` list of
  the seven names), so every lane builds and runs the Rust DLL; the C++ files stay in the tree, unbuilt, as the
  reference text, and Task 18 deletes them. — Lanes run programs against their own DLL without a second build path.
  — none.
- **P6 COM objects.** One `#[repr(C)]` struct per object: its primary vtable pointer first, companion vtable pointers at
  fixed offsets where the C++ answers a distinct pointer (`IMTLD3D12DeviceExt` on the device, `IMTLD3D12CommandListExt`
  on the list, `IMTLSwapChainFactory` on the queue: B9, `identity.sh`), then `dxmt_util::com::ComObject`'s two counts
  (public; private, which lists hold on PSOs and root signatures and placed resources on heaps:
  `src/util/com/com_object.hpp:45-74`), freed when the private count reaches 0. Vtables are generated (Task 2) from
  `include/native/d3d12/d3d12.h` and `dxgi1_6.h`'s `CINTERFACE` structs as the C++ compiles them (explicit aggregate
  returns take the out pointer and return it), every slot `unsafe extern "system-unwind" fn` (P20). The companion
  vtables are Task 7a's own `system-unwind` structs (`C/src/abi/ext.rs`), layout-asserted equal to `dxmt_util::ext`'s,
  whose slots are nvngx's `extern "system"` copy (5a S0c; nvngx's and nvapi's callers keep it): a `"system"` slot
  would turn MetalFX's Objective-C exception in `CreateTemporalScaler` or `TemporalUpscale` into an abort of the VM host.
  `MTL_TEMPORAL_SCALER_D3D12_DESC` and `MTL_TEMPORAL_UPSCALE_D3D12_DESC` are asserted per pointer width against the C
  header (upscale: 128 bytes, `InputContentWidth` at 48 on 64-bit; 100 bytes, at 24 on i686). A pointer the C++
  `static_cast`s to its own class (`M3 §2.1`) is checked by vtable identity: a foreign pointer gets
  `E_INVALIDARG` from a method that returns `HRESULT` and is ignored, logged once, by a `void` one (the C++ is
  undefined there: step 3 D7's rule). — Hand-typing ~400 slots per target is where slot-order bugs come from; a
  `static_cast` of a foreign pointer is undefined behaviour in Rust too. — one compare per cast.
- **P7 Not implemented.** `not_implemented!("<Interface>::<Method>")` (the name `unimpl-parity.sh` reads, Task 6) logs
  the C++'s line (`<file>:<function>(<line>) is not implemented.`, Rust's file and function) at error and flushes the
  log file; on PE it then calls the C runtime's `abort()` (declared, as the C++'s
  `IMPLEMENT_ME_END`, `d3d12_device.hpp:36-50`; not `std::process::abort`, whose `__fastfail` ends a game differently);
  natively it calls `wmt_native_not_implemented()` (Task 1) and returns `E_NOTIMPL` (nothing for `void`). MacNeutron's
  stubs check `MTLD3DNotImplemented()` after every call besides their `catch (...)` (D3D11 stays C++ until 5b). On
  i686 the 32-bit descriptor-handle helpers and shader-visible heaps keep aborting (`d3d12_device.hpp:428-490`,
  `d3d12_descriptor_heap.cpp:139-141, 549-551`; d3d-smoke expects only `list4`, `layout3` there). — B1 (M4 §1.4); one
  symbol for both DLLs (critic 1.9). — none.
- **P8 The translation cache is identical.** D3D12's keys and values are the C++'s byte for byte: `CompileFunction`'s key
  (SHA-1 of the shaders; SHA-1 of `"d3d12-function"`, the entry name, the argument variant and `DXMT_DXIL_BOUNDS`),
  `HashCompileArgs`' field order and its refusal of unknown kinds (`d3d12_shader_cache.cpp:122-243`), the
  `"d3d12-reflection 2"` tag and its record bytes (`:95-119`), and the table name from `ShaderCacheVersion()` (5a P7
  deferred its Rust form here): FNV-1a over the generated `dxmt_translator_key.h`'s key or `version.h`'s `DXMT_VERSION`,
  then `AIRCONV_VERSION`'s four bytes (`src/dxmt/dxmt_shader_cache.cpp:11-19`). `C/build.rs` reads those two generated
  headers from paths meson passes (`DXMT_TRANSLATOR_KEY_H`, `DXMT_VERSION_H`; `rerun-if-env-changed` for both,
  `rerun-if-changed` for a named file) and emits them as `rustc-env`. Unset (cargo without meson: UT, MB, RL), it
  falls back to the C++'s no-key form (an empty key and a placeholder version), as 5a R3's `build.rs` takes an empty
  file when unset; it fails only when a variable is set and names a missing file. Table-name tests feed explicit inputs
  to `shader_cache::version`; LB and the gates pin the meson path. `AIRCONV_VERSION` is `winemetal_wire::AIRCONV_VERSION`
  (`src/winemetal/wire/src/lib.rs:50`, asserted against the header at `layouts.rs:6769`), not parsed from
  `airconv_public.h`. Stores are `dxmt_core::shader_cache::instance(metal_version, version)` (Task 3), closed by
  `close_all()` after the counter line at `DLL_PROCESS_DETACH` (PE) and from an `atexit` registered at the first open
  (natively), as the C++'s static map's destructor did: the map owns its stores and destroys them whatever still uses
  them, so `close_all` takes each store's reader and writer out under their locks (reader released first, then writer:
  the C++ members' reverse order) and later calls see the cache as off, however many `Arc<Store>` a leaked device, a
  `CachedShader` or a pipeline still holds. — A changed key turns over every player's D3D12 cache; a store left open
  leaves `-wal`/`-shm`. — none.
- **P9 Statistics and the dump folder.** The Rust calls `dxmt_core::stats` and `dxmt_core::dump` (5a S1). `scope!(name)`
  (Task 7a, in `C/src/stats.rs`) is the C++'s `DXMT_STAT_SCOPE`: the id registered on the call site's first run in a
  per-site `LazyLock<StatId>` (the C++'s function-local static) and a timed scope when stats are on; it carries no
  abort-on-panic guard (P20 puts that in every slot's prologue, kept under `perf-build`). `count!(name, n)` is
  `DXMT_STAT_COUNT`. Every vtable slot whose C++ has a `DXMT_STAT_SCOPE` starts with `scope!` under the C++'s name (the
  C++'s 269 scope literals and 29 counter literals at da6ed2d, Task 6's `statnames.sh`). Stats start at the first entry
  into any export (`DXMT_STATS == "1"`, as `d3d12_stats.cpp:23` reads it) and write their exit report at
  `DLL_PROCESS_DETACH` (PE) or from `atexit` (natively). With `perf-build` both macros and every report string are
  removed by `#[cfg]` (not `cfg!`). At the switch S1's C ABI (`dxmt_stats_*`,
  `dxmt_dump_*`, their `abi.rs`, `C/d3d12_stats.*`, `C/d3d12_dxil_dump.*`) is deleted: its only callers were D3D12's
  C++. — 5-D1. — none.
- **P10 Logging and texts others read.** `C/src/lib.rs` defines `dxmt_log` (5a P5) over a `dxmt_util::log::Logger` named
  `d3d12` (`<exe>_d3d12.log`, natively `_d3d12.log`, CRLF). Texts kept byte for byte where a gate or MacNeutron reads
  them (Task 0 ledgers the grep: at least `d3d12 shader cache:`, `d3d12 pipeline recording off: `, every
  `macneutron-framegen: ` line, written to stderr as the C++'s `fprintf(stderr, …)` (`d3d12_command_queue.cpp:2229,
  2407, 2415`), never to the log, the `hazard ` lines and the stats labels). Stderr is written as `fprintf` does,
  ignoring a failed write: `let _ = writeln!(std::io::stderr().lock(), …)`, never `eprintln!`/`println!`, which panic on
  a closed pipe (`EPIPE`, `ERROR_NO_DATA` under Wine) and so abort the game; `tools/audit.sh` rejects `eprintln!` and
  `println!` in `src/d3d12` and `src/dxmt` (Task 7a, owner). `release/lib.sh:9`'s probe text
  `GetResourceAllocationInfo: invalid texture descriptor` stays a debug line (`d3d12_device.cpp:1849`): present in
  every std image, absent from p images (`#[cfg]`). — The release check is a plain `grep -F`
  (`release/release.sh:174-179`). — none.
- **P11 Frame generation and pacing move into `dxmt_core::presenter::framegen`** (Task 15): the process-wide slot of the
  last temporal upscale, the interpolator, the three-frame ring, the pacing thread and the adaptive decision
  (`d3d12_command_queue.cpp:82-107, 2160-2476`). D3D12's queue is its only driver; D3D11 and D3D9 do not drive it
  (M0 §5). — 5-D1. — none.
- **P12 Pipeline recording.** The Rust writes `.pipelines` (`DXMTPRC1`, `d3d12_pipeline_record.hpp:22-35`): the same
  bytes as the C++ for a program that creates its pipelines on one thread, the same torn-tail cut, foreign-file restart
  and `recording off` line. Replay keeps the C++ reader (`src/replay/meson.build:3, 12` compiles
  `C/d3d12_pipeline_record.cpp`) until step 6. — 5a P14; the C++ reader reading the Rust's files is the format check. —
  writer and reader in two languages until step 6.
- **P13 What stays in `src/d3d12` after the switch:** `d3d12.def`, `meson.build`, the crate, `d3d12_interfaces.hpp`,
  `d3d12_barrier_legacy.hpp`, `d3d12_shared_fence.hpp` (5-D7), `d3d12_pipeline_record.{hpp,cpp}` (replay). Deleted: every
  other `C/*.cpp|hpp` and `src/d3d10/d3d10_blob.{cpp,hpp}` (its only user). Kept though D3D12 stops using it:
  `src/airconv/dxbc_root_signature.hpp` (a key input until a key-moving bump, critic 1.3; deleted then) and
  `libs/DXBCParser` (D3D11's until 5b). — none.
- **P14 Pipeline libraries** keep `DXMTPLB1`, version 1 and their layout (`d3d12_pipeline_library.cpp:87-150`); the
  description hash is the Rust's own (FxHasher over the C++'s field list), as "stable within one build only" allows
  (`:33-35`): a C++-built blob loads, and each `Load*` answers `E_INVALIDARG` on the hash, as after any update. — libc++'s
  `std::hash` is not worth reproducing. — none.
- **P15 A real overflow check (maintainer 2026-10-30, "Real check", superseding 5-D4's debug_assert).**
  `AllocateCPUHeap`'s and `AllocateGPUHeap`'s asserts (`d3d12_command_allocator.hpp:274, 461-469`) become one compare
  per allocation in release too: a CPU request of 4 MiB or more, or a GPU write past the block, logs one line
  (`d3d12: command allocator <cpu|gpu> heap overflow: <size> > <limit>`) and aborts as `IMPLEMENT_ME` does (PE: the CRT
  abort; natively the not-implemented flag and E_NOTIMPL per P7), so the `unsafe` write's SAFETY line states a real
  invariant. A probe case pins the message on the native build (where it does not end the process). — cost: a game that
  hits it crashes cleanly instead of hanging or corrupting memory; one compare per allocation.
- **P16 Threads and knobs.** `dxmt-cmdbuf-waiting-thread` (`d3d12_command_queue.cpp:1363`), the pacing thread and the
  fence listener keep their names, the C++'s 1 MiB stacks (`util/thread.hpp:55`) and priorities. Each `DXMT_D3D12_*`,
  `DXMT_*` and `MACNEUTRON_FRAMEGEN` read happens where the C++ reads it: a function-local `static const` becomes a
  `LazyLock`, a member initializer (`serial_`, `:132`) a read per object. — An app may set a variable between devices.
  — none.
- **P17 Binary identity delta, pre-stated.** `d3d12.dll` (ARM64EC, x86_64, i686): name, machine, the seven exports with
  `@101 @102`, the builtin marker (guest), no VERSIONINFO; imports may lose the C++ runtime's and gain Rust std's (5a
  P16's list), keep `winemetal.dll` and `dxgi.dll` (`check-build.sh:20-24`); i686 exports undecorated (`--kill-at`).
  Native `d3d12.dylib`: `nm -gU` = the seven names, `otool -L` equal, OSO and `$HOME` 0. — The C++ runtime leaves with
  the C++. — a missing import shows in `check-build.sh`, fixed in Task 20.
- **P18 Game data (4-D7).** Allowed and used read-only in place: 3DMark's D3D12 pipeline recording (a `DXMTPRC1`
  file; its path from 4a's ledger) and SMITE 2's capture's 150 `rs-*.bin`
  (`~/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt`); every output under `$S`, with
  `DXMT_SHADER_CACHE_PATH` and `DXMT_DXIL_DUMP` pointed there; nothing copied or committed. The maintainer extended
  4-D7 to step 5 with the same rules (2026-10-30, "Yes, same rules": outputs under build/step5c). SMITE 2's own
  `game.pipelines` is not used. — The only real pipelines and root signatures on this Mac. — none.
- **P19 Performance without a game.** At the switch, on both Mac lanes, each row Rust against the C++ reference, each
  with one control run on the reference before a row above +5% counts (5a's form), printed `<row>: <rust> vs <ref>
  (+x.x%)`, passing at ≤ +5.0%: check.sh's pipeline-creation `timing` cold and warm (`check.sh:204`), `dlss cost`
  (`:1289-1291`); a `d3d12_probe` rate family (`d12r`, step 2's probe call-rate form: draws with root constants,
  `ResourceBarrier`, `ExecuteCommandLists`, ns/call; std and p; also natively); the 3DMark replay's warm second run
  (the cold run is compile-bound). No row times DXMT_STATS-instrumented runs. Spec §8's step-5 benches (SMITE 2,
  Palworld, a D3D11 title) come after both switches with the maintainer's go-ahead; Task 21 asks before Step 2 whether
  to run the two D3D12 ones now (`sh eac-vm/tests/bench-smite2.sh <label> 120` and `BENCH_GAME=palworld sh
  eac-vm/tests/bench-smite2.sh <label>`, each once, plus a named C++ baseline). — Game runs need a go-ahead each time. —
  a loss only a game shows waits for the milestone.
- **P20 Unwinding** (5a P19). Natively the staticlib builds `production-unwind`; slots are `"system-unwind"` (`"C-unwind"`
  on darwin), so an Objective-C exception from winemetal still reaches the VM host's `catch (...)`. The abort-on-panic
  guard (5a P19's form) is every slot's prologue whatever the build: Task 2's generated slot types are filled through
  one `slot!` macro (Task 7a, `C/src/com.rs`) that always holds the guard and adds `scope!` only without `perf-build`, so
  `AddRef`/`Release`, the companion slots and every scope-less slot have it, and a p build keeps it. Every thread body
  `d3d12` and `dxmt-core` spawn (the completion thread, the pacing thread, the fence listener's) starts with the guard
  too: under `production-unwind` a panic there would end only that thread, and fences stop completing with the VM
  hanging silently. PE builds `production` (`panic = "abort"`). Test: `com::tests::slot_and_thread_panics_abort` (a
  child process; natively, std and `perf-build`); mutation: the guard dropped → FAIL. — Keeps approved D4. — landing
  pads in the native image.

## Global Constraints

- Agents never push, merge to `main`, fetch or download. Task 21's push, pin and pinned re-fetch run under the
  maintainer's standing authorization, by the controller only. No MacNeutron.app, Steam or game; under
  `~/Library/Application Support/MacNeutron` only P18's paths, read-only. Kill by exact PID. Home written `~`; privacy
  scan before every push. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; DXMTRS subjects
  `<area>: sentence`.
- **The lock (`build/pipeline.md`).** Every build or install of MacNeutron's runtime or a DXMT build into the shared
  folders, every gate or check that reads installed outputs, and every checkout in `$D` runs as ONE job:
  `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c 'git -C <M>/build/wine-arm64-src/dxmt checkout -q --detach <sha> &&
  <build> && <tests>'`, logging the commit and wine.app's cdhash; no branch, stash or worktree left in `$D`; no waiting on
  a review inside it. Editing only in worktrees (`git -C $D worktree add $S/wt-<lane> …`; MacNeutron `git worktree add
  $S/wt-mn -b step5c-mn dxmtrs`, `BUILD_DIR=<M>/build` in the environment, never as a make argument). Private copies
  under `$S` (the reference app, the kit worktree, test programs) are host work: no lock, but for `kc.sh` (Execution).
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE`; the builder starts with `VM_SHARES` (builder-out-share note); VM commands export
  `MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img`.
- **Rust**, 5a's forms with `$S`: `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH`, `--offline`,
  `CARGO_TARGET_DIR=$S/cargo-<lane>`. **UT(<crates>)** = 5a's host-test form (`-Wl,-undefined,dynamic_lookup`).
  **MB(<crate>)** = `cargo build --offline -p <crate> --target <t>` for each of the four `-pc-windows-msvc` targets.
  **RL** = 5a's lane form of `tools/rust-check.sh` → `rust-check: ok`; **RC** the same on `$D` at the gate; from Task 17
  Step 1 on (a tree with Z) both gain `AIRCONV_LLVM=<M>/build/wine-arm64-src/llvm-arm64-nolto
  AIRCONV_XCRUN=<M>/wine-arm64/tools/xcrun-metal.sh` (4a Z2). **LB(<kind>)** = 5a's `$S/lane-build.sh <wt>
  <mac|native|guest64|guest32> [p]` → `lane-build: <kind> ok`, configured with `-Dtranslator_key_file=$S/key` (Task 0
  Step 4), never `$SRC/dxmt-translator-key`, which every `make wine-arm64` rewrites.
- Translator key, read with 5a's form, equals the ledgered baseline before and after every commit: Task 0's value on
  `step5w`; the scaffold commit's value on `step5c` from the scaffold on (P3: before Z the key hashes `src/airconv`
  wholesale); `main`'s post-Z value after Task 17 Step 1. No edit of `include/`, the root `meson.build`/`meson.options`,
  `src/airconv/**`, `libs/DXBCParser`, `src/d3d11/d3d11_shader.cpp`, `src/winemetal/airconv_thunks.{c,h}`, or the root
  `Cargo.toml`'s `[profile.*]` sections and `.cargo/config.toml` (key inputs after Z, 4a P7).
- Conventions (`docs/CONVENTIONS.md`): no `#[allow]`/`#[expect]`; one `unsafe` operation a block with `SAFETY:`; no
  `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; `FxHashMap`; derives only with a callsite through
  `tools/audit.sh --update-derives`; no lowercase "dxmt" in comments.
- Behaviour is the C++'s wherever anything outside the DLL sees it: HRESULTs, out-parameter writes, QI answers and
  pointers, refcount returns, handles and GPU-visible layouts (32-byte descriptor entries, root-argument qwords and the
  lock table, static samplers' 4 qwords, the indirect-command structs the resolver MSL reads), files and their bytes,
  log, stderr and stats text, cache paths, keys and values, thread names, the `IMPLEMENT_ME` set (`rules.txt:142-175`,
  `coverage.py`'s `AS_DXMT`; pinned by Task 6's `unimpl-parity.sh`: the C++'s `IMPLEMENT_ME` methods, about 36 sites at
  da6ed2d, counted at Task 0, are the Rust's `not_implemented!` set, no more), the `E_NOTIMPL` answers. Every observable
  quirk stays (`GetType` answers `DIRECT`, `d3d12_command_list.cpp:344-347`). The CPU clock is `dxmt_util::qpc()` (Task
  4): kernel32's `QueryPerformanceCounter` on PE, `CLOCK_UPTIME_RAW` at 1 GHz natively (`win32_native.h:390-399`), never
  `Instant` or `mach_absolute_time`.
- No allocation on the recording hot paths beyond the C++'s (`PreDraw`, `ResourceBarrier`, `ExecuteCommandLists`'s
  per-encoder loop: Task 12b's and 13's counting-allocator tests); no foreign call per recorded command.
- zsh: quote `"====="`; gating scans use `/usr/bin/grep`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes (rebase, fast-forward, `git cherry` empty before
a branch is deleted) and keeps the ledger. One **pipeline owner** runs every lock job (Tasks 0 Step 4, 5 Step 4, 17
Steps 1 and 3 (its `kc.sh` runs and its one VM-and-native job), 19, 21 Steps 2 and 4); step 4's and 5b's jobs share the
lock. `kc.sh` reads only private copies but is
a long GPU load, so it runs as a lock job too, never beside another step's timed rows. **Lanes** do host work only
(UT, MB, RL, LB, single programs through `run.sh` on private copies). Reviews on Opus.

**Lane procedure.** `git -C $D worktree add $S/wt-<lane> -b step5c-<lane> <base>`; fill `include/native/directx` and
`external/nvapi` read-only as 5a's lane procedure does; RED, GREEN, the task's checks, commit. A lane never edits another
lane's files, a pub signature Tasks 7a/7b fixed, `Cargo.toml`/`Cargo.lock`'s entries, `tools/audit.sh`,
`tools/rust-check.sh`, `rustfmt.toml`, `docs/CONVENTIONS.md` or a golden; a needed change goes to the controller.

| Task | Runs | Depends on | Owns (disjoint) | Lock |
|---|---|---|---|---|
| 0 base, kit, references | owner (Sonnet) | 5a pushed, `step5w` handed off | `$S/*` | Step 4 |
| 1 `SM50*`, not-implemented, handle count | lane `sys` (Sonnet), `step5w` | 5a S0; Step 2 on 2 | `src/winemetal/sys/{sm50-abi.h,src/sm50.rs,src/sm50/}`, its `lib.rs` lines, `src/nativemetal/win32_event.c`; MacNeutron `eac-vm/dxmt-native/tests/{wmt-exports-test.c,wmt-exports.sh}` | no |
| 2 COM groundwork | lane `com` (Opus), `step5w` | 5a S0 | `tools/winemetal-layouts.sh` (`--com`; `-I src/airconv` for `--seam-abi`), `src/util/src/{com.rs,com/,ext.rs,dxgi_vtbl.rs,private_data*}`, dxgi's moved files | no |
| 3 core groundwork | lane `core` (Opus), `step5w` | 0 (`step5w` rebased past S4, 5a Task 22 Step 6) | `src/dxmt/src/fence{.rs,/}`, `shader_cache/{instance,version}.rs`, `shader_cache.rs`'s `mod` lines and `Store::close`, `src/dxmt/src/lib.rs` line | no |
| 4 WSI, swap-chain helpers, clock | lane `wsi` (Sonnet), `step5w` | 2 | `src/util/src/{wsi,qpc}{.rs,/}`, `src/dxmt/src/swapchain{.rs,/}`, dxgi's `wsi*` | no |
| 5 the probe | lane `probe` (Opus), MacNeutron | 0 | `dxmt/tests/d3d12_probe.{cpp,fixed,native,i386,guest64}`, `dxmt/tests/d3d12_dlss.cpp` (`framegen-still` mode), `eac-vm/tests/dxgi-guest.sh`, `eac-vm/dxmt-native/build-tests.sh` | Step 4 |
| 6 reference corpus | owner (Sonnet) | 0, 5 | `$S/ref-corpus/**`, `$S/golden/**`, scratch comparators | no |
| S `airconv-ir` scaffold | owner, `step5c` | 4; 4a's P1 committed | P3's list only | no |
| 7a skeleton: build and shared files | owner (Opus), `step5c` | 1-4, S, 6 Step 2b | `C/{Cargo.toml,build.rs,meson.build}`, `C/src/{lib,stats,unimpl,com,encoder}.rs`, `C/src/abi/{vtbl,ext}.rs`, `C/src/abi.rs`'s `mod` lines and the empty per-lane `C/src/abi/*.rs`, `src/dxmt/meson.build` (one line), `rustfmt.toml`, `tools/audit.sh` (P10's rule), the committed goldens (Task 6's) | no |
| 7b skeleton: signatures and dispatch | owner (Opus), `step5c` | 7a | every other `C/src/**` stub with its final pub items, `C/src/device{.rs,/vtbl.rs}`, `C/src/list.rs` (the `List` struct and the list's internal seams), `src/dxmt/src/presenter/framegen.rs` (stub) and its `pub mod` line | no |
| 8 objects | lane `obj` (Sonnet) | 7b | `C/src/{descriptor_heap,fence,shared_fence,heap,query_heap,command_signature,blob}{.rs,/}`, `C/src/abi/objects.rs` | no |
| 9 shaders | lane `shd` (Opus) | 7b | `C/src/root_signature.rs` (5a D2's, finished), `C/src/root_signature/{object,deserializer}.rs`, `C/src/{shader_cache,shader,capture}{.rs,/}` | no |
| 10 pipelines | lane `pso` (Opus) | 7b | `C/src/pipeline{.rs,/}` | no |
| 11 resources | lane `res` (Opus) | 7b | `C/src/resource{.rs,/}`, `C/src/abi/views.rs` | no |
| 12a allocator | lane `alloc` (Opus) | 7b | `C/src/allocator{.rs,/}` | no |
| 12b list state and draws | lane `draw` (Opus) | 7b | `C/src/list/{state,draw}{.rs,/}`, `C/src/abi/list_state.rs` | no |
| 12c list operations | lane `ops` (Opus) | 7b | `C/src/list/{copy,barrier,indirect,clear,upscale,submission}{.rs,/}`, `C/src/abi/list_ops.rs` | no |
| 13 queue | lane `queue` (Opus) | 7b | `C/src/queue{.rs,/}` but `queue/dump.rs`, `C/src/abi/queue.rs` | no |
| 14 dumps and capture | lane `dump` (Sonnet) | 7b | `C/src/queue/dump{.rs,/}` | no |
| 15 frame generation | lane `fg` (Opus) | 7b | `src/dxmt/src/presenter/framegen{.rs,/}` | no |
| 16a device creation | lane `dev` (Opus) | 7b | `C/src/device/{features,factory,temporal,residency,null}{.rs,/}` | no |
| 16b swap chain | lane `swap` (Opus) | 7b | `C/src/swapchain{.rs,/}`, `C/src/abi/swapchain.rs` | no |
| 17 integration | owner (Opus), lanes | 8-16b | merges; fixes in the owning lane | Step 1 (if `main` moved; `make llvm-nolto`), Step 3's `kc.sh` and its one VM-and-native job |
| 18 the switch commit | owner (Opus) | 17 | deletions, `C/meson.build`, `rustfmt.toml`, S1's ABI; MacNeutron `eac-vm/d3d/{gen.py,host/native.cpp,host/dispatch.c,host/stubs.h,tests/dispatch-test.c}`, `dxmt/check.sh` | no |
| 19 gates, once | owner (Sonnet) | 18, Z on `main` | — | yes |
| 20 one fix pass | owner, lanes (Opus) | 19 | as needed | as needed |
| 21 spec, push, pin | controller | 20 | spec, pins, ledgers | Steps 2, 4 |

**What starts before its dependencies land.** Tasks 1, 2 and 4 need only 5a's S0 on `step5w` (5a Tasks 2-4), not 5a's
push, and may start then; Task 3 waits for `step5w`'s rebase past S4 (5a Task 22 Step 6: before it, `shader_cache.rs`
is S0a's stub and `Store` is not on `step5w`). Task 0's reference build waits for 5a's push. Tasks 5 and 6 need Task 0's
references. The scaffold (S) needs 4a's P1 committed on `step4a-translator`; Task 7a needs it. Lanes start at Task 7b's
reviewed commit (a missed signature rebases every lane, so 7b is reviewed before any lane branches); 15 needs only 5a's
S2 (`Presenter`, `TemporalScaler`) and 7a's framegen stub. Integration (Task 17) runs on what has merged, as lanes
finish. The switch commit and its gates wait for Z on `main` (P3). Merge order on `step5c`, the order a draw needs:
16a, 8, 11, 9, 10, 12a, 12b, 12c, 13, 14, 15, 16b; each lane rebases onto `step5c` before its merge and runs its own
program check there (Lane procedure, Tasks 8-16b).

## Rules check

- **Statics** (each a process-wide fact of the C++):
  - `d3d12`: `LOG: LazyLock<Logger>` (`Logger::s_instance`, `d3d12.cpp:29`); `INIT: LazyLock<()>` (stats on, P9; natively
    the `atexit` registrations); `scope!`'s per-site `LazyLock<StatId>` (the C++'s function-local statics); the
    translation cache counters, `AtomicU64`s (`d3d12_shader_cache.cpp:31`); the shared-fence table,
    `LazyLock<Mutex<FxHashMap<…>>>`, never dropped (`d3d12_shared_fence.hpp:28-32`, leaked on purpose); the recorder,
    `LazyLock<Mutex<Option<Recorder>>>` (`d3d12_pipeline_record.cpp:218-232`); pass dumps, `LazyLock<Mutex<Dumps>>`
    (`d3d12_command_queue.cpp:821-823`); `LazyLock`s for function-local env reads (P16).
  - `dxmt-core`: `framegen::SLOT: Mutex<Slot>` and `framegen::MODE: LazyLock<Mode>` (`d3d12_command_queue.cpp:86-107`);
    `shader_cache`'s instance map `LazyLock<Mutex<FxHashMap<u32, Arc<Store>>>>` and its `atexit` flag `AtomicBool`
    (`dxmt_shader_cache.cpp:23-24`).
  - `winemetal.dylib` (C): the not-implemented flag, `_Thread_local bool`.
  - Not statics: `dxmt_core::fence::EventListener` (one per device, its winemetal listener and thread, P16's name and
    stack).
  - Test-only: capturing `dxmt_log`, winemetal fakes, under `cfg(test)`; no `OnceLock`.
- **Config keys:** none new; `d3d12.preferredMaxFrameRate` read through `dxmt_util::config`. Environment read as today:
  `DXMT_D3D12_{MERGE,COMPRESSION,TIMESTAMP_BLITS,SM6,SERIAL,OVERLAP,PRIVATE,INDIRECT,DEFER_RESOLVES,CMDBUF_SLOTS}`,
  `DXMT_{STATS,RENDER_SCALE,PROBE,PIPELINE_RECORD,DXIL_DUMP,DXIL_BOUNDS,DUMP_PIXEL,DUMP_FRAMES,DUMP_FRAME,CAPTURE_FRAME}`,
  `MACNEUTRON_FRAMEGEN`. Build-time (meson to cargo, `C/meson.build` only), optional: `DXMT_TRANSLATOR_KEY_H`,
  `DXMT_VERSION_H` (unset: the C++'s no-key form, P8; set to a missing file: the build fails).
- **Wire fields:** none changed. New Rust mirrors, each asserted per target against the C (step 3 D8's throwaway layout
  program): the generated vtables (Task 2's `--com`, whose asserts are the slot count, each slot's offset and each
  slot's summed parameter bytes, so a by-value `UINT64` mapped to `usize` fails on i686 stdcall); the companion
  vtables of `C/src/abi/ext.rs` (7a, equal to `dxmt_util::ext`'s layout) and `MTL_TEMPORAL_{SCALER,UPSCALE}_D3D12_DESC`
  per pointer width (P6); the `SM50_SHADER_*` argument and reflection structs (Task 1, unless `airconv-ir` exports them:
  Task 0 ledgers), with `SM50_IA_INPUT_ELEMENT`'s bitfields (`step_function:1`, `step_rate:31`) pinned by value, not
  only by layout (Task 1); the per-lane mirrors in `C/src/abi/{objects,views,list_state,list_ops,queue,swapchain}.rs`
  (descriptor-heap, query-heap, heap and command-signature descs and the indirect argument descs; the five view-desc
  unions; vertex, index and stream-output views; `D3D12_RESOURCE_BARRIER`, copy locations and the render-pass structs;
  `D3D12_COMMAND_QUEUE_DESC`; `DXGI_SWAP_CHAIN_DESC1` and the fullscreen desc), each lane adding its rows to the
  throwaway program on the five targets;
  `IndirectComputeCommandData`, `IndirectRenderCommandData` (`d3d12_command_allocator.hpp:44-72`), the descriptor
  entries (`d3d12_descriptor_heap.cpp:28-76`), `EMBEDDED_DESCRIPTOR_HANDLE` (`d3d12_descriptor_heap.hpp:32-77`: Tag 5,
  Descriptor 20, Heap 39 bits), `WMTFXTemporalScalerInfo` (52). The forwarder's C++ headers are unchanged (5-D7).
- **Dependencies:** no new external package; `Cargo.lock` gains only the path package `airconv-ir` (P3's scaffold
  commit, the owner's `cargo metadata --offline`; from `main` after Z). `d3d12` → `dxmt-core`, `dxmt-util`,
  `winemetal-sys`, `winemetal-wire`, `airconv-ir` (new edge, path, `LGPL-2.1-or-later AND MIT`: its MIT notice joins
  d3d12's notices legs), `rustc-hash`, `bitflags`, `libc` (darwin). `dxmt-core` gains no edge. New C exports in the
  native `winemetal.dylib`: `wmt_native_not_implemented`, `MTLD3DNotImplemented`, `wmt_native_handle_count` (Task 1).
- **Derives:** small `Copy` types only (handles, view keys, `StatId`, the descriptor and encoder kinds, `Mode`), each
  through `--update-derives`; no derive on a type over 16 bytes.
- **Lint suppressions:** none. Generated `C/src/abi/vtbl.rs`, `src/util/src/dxgi_vtbl.rs` and
  `src/winemetal/sys/src/sm50/abi.rs` join `rustfmt.toml`'s `ignore` in the task that creates them (Tasks 1, 2, 7a:
  the owner edits it; lanes never). The scaffold's `float_cast.rs` keeps 4a's accepted per-site exception (P3), not a
  new one.
- **Duplicates (for §7 and step 6):** `LegacyBarriers` (5a D1) and shared fences in Rust vs the forwarder's headers
  (5-D7); the Rust recording writer vs replay's C++ reader (P12); `C/build.rs`'s key inputs vs 5b's for D3D11 (if 5b
  copies it); `dxmt_util::wsi` vs `src/util/wsi_*.cpp` (the forwarder's guest); `dxmt_core::swapchain`'s helpers vs
  D3D11's C++ until 5b; the `airconv-ir` scaffold (until Task 17); 5a lane D2's own raw root-signature layouts vs
  `airconv-ir`'s `root_signature::raw` (D2's deleted at Task 17, not kept); `C/src/abi/ext.rs`'s `system-unwind`
  companion vtables vs `dxmt_util::ext`'s `system` ones (nvngx's and nvapi's callers; P6); `dxmt_util::qpc` vs
  `win32_native.h`'s `QueryPerformanceCounter` (the C++ and the forwarder's guest) and 5a's stats clock; the scratch
  golden harnesses' verbatim C++ excerpts (Task 6, under `$S` only). Retired: `src/d3d10/d3d10_blob.*`, dxgi's
  private-data store and WSI (moved, Tasks 2, 4).

## Review Focus

1. **A player's D3D12 translation cache after the update.** Expected: every shader cached by the C++ build is a hit
   for the Rust and back; same table name; no `-wal`/`-shm` left. Tests: Task 3's
   `shader_cache::tests::{version_is_the_cpps, close_all_closes_every_store (an extra `Arc` held)}`, Task 9's
   `shader_cache::tests::{hash_compile_args_is_the_golden, unknown_kind_gives_up, reflection_record_is_the_golden,
   function_key_is_the_golden}`, Task 17's cross-hit rows (a C++-filled cache read by the Rust: the `d3d12 shader
   cache:` line shows no miss; and the reverse; both apps keyed by `$S/key`), Task 19's 3DMark replay (same keys and
   values) and check.sh's cache and version rows.
2. **A game's real pipelines and root signatures** (SM5.1 DXBC, root constants, static samplers, 1.1 range flags, GS
   emulation). Expected: the same pipelines created and refused, the same qword offsets. Tests: Task 9's
   `root_signature::object::tests::{smite_root_signatures_as_cpp, mixed_sampler_table_refused}`; Task 6's and Task 19's
   3DMark replay (created/failed/bad counts equal); `pso-compare.sh` (Task 6: what reaches winemetal's pipeline,
   depth-stencil and sampler creators over the native replay and native tests, equal as sorted multisets);
   `unimpl-parity.sh` and the probe's `d12c` family (implemented slots no program calls, `OMSetBlendFactor` among
   them); check.sh's `d3d12_dxil`, `d3d12_dxil_exec`, `d3d12_vsread` rows.
3. **Frame generation and pacing** (`MACNEUTRON_FRAMEGEN=1` and `always`, Palworld's case: a game never waits on a
   drawable). Expected: the same on/off decisions, generated counts and stderr lines; no stall; ring slots not
   rewritten before read; the frame copy and the interpolation ordered behind the queue's fence ring. Tests: Task 15's
   `framegen::tests::{adapt_replays_cpp_trace, present_orders_as_cpp, turns_on_after_half_a_second_slow,
   turns_off_when_held_back, learned_cap_forgotten_after_60s, slot_fresh_once_per_upscale, vsync_present_not_generated}`
   (the first two against goldens from the C++, Task 6); check.sh's `dlss-fg`, `dlss-nofg`, `dlss-fgsoak`, `dlss-fgfast`,
   `dlss-fgslow`, `dlss-fgphases` rows; Task 17's and 19's `dlss framegen pixels same` row; present-gate.
4. **The VM host natively.** Expected: an unimplemented method fails its call and the VM host goes on; companions at
   their own pointers; forwarded programs as before; a Rust panic or an unscoped slot never unwinds into the host.
   Tests: Task 1's `wmt-exports.sh` (the flag and the handle count, linked against the lane's `winemetal.dylib`); Task
   5's `d12n` family natively; Task 18's dispatch-test `notimpl` cases (one per return kind through the generated
   stubs, RED with the old `gen.py`); P20's `slot_and_thread_panics_abort` in both profiles; Task 17's native gate run;
   Task 19's loopback (31), `identity.sh`, `answers.sh`, `threads.sh`, `coverage.py` unchanged, d3d gate, the p
   `d3d12_null` row.
5. **A 32-bit game and many recording threads.** Expected: i686 `d3d12.dll` loads and answers `list4`, `layout3`; lists
   recorded on many threads with one queue completion thread. Tests: Task 5's `.i386` golden (no descriptor-heap rows,
   as `d3d12_list7.cpp:31` documents); Task 2's per-slot parameter-byte asserts on i686; Task 12b's
   `list::draw::tests::recording_allocates_only_in_the_heap`; Task 17's VM-and-native job (the first i686 and x86_64
   Rust runs, before the gate); Task 19's d3d-smoke `32:` blocks (both modes, new WoW64), `threads.sh`, the VM stats
   row with `list7_32.exe`.

---

### Task 0: base, kit, references (owner)

**Files:** scratch only: `$S/{lane-build.sh,ident*.{sh,py},stats-golden.sh,stats-compare.sh,hist.sh,hist-compare.sh}`
(copied from `$B/step5a/`, 5a Tasks 0-1), `$S/{run.sh,lane-app.sh,lane-dylib.sh}` (step 3's kit, copied from
`$B/step3/` or, if absent, written to step 3 Task 4's interfaces and controls), `$S/kc.sh`, `$S/wt-kit/` (a MacNeutron
worktree), `$S/ref-*`, the ledger.

**Interfaces:**
- Produces `$S/kc.sh <app> <tag>`: the full check.sh on a private app, `MACNEUTRON_ARM64_APP=<app>
  MACNEUTRON_ARM64_TOOLS=$S/wt-kit/build/wine-arm64 DXMT_CHECK_WORK=$S/work/<tag> sh $S/wt-kit/dxmt/check.sh` and the
  ARM64EC form, from `$S/wt-kit` (detached at `dxmtrs`'s head, its `build/dxmt-tests{,-arm64ec}`, `build/presenter`,
  `build/wine-arm64/{dxil-probe,dxil-translate}` and `.build/release/macneutron` copies of Step 4's build), into
  `$S/work/<tag>/check-{x64,arm64ec}.txt`; reads private copies only, run as a lock job (Execution: GPU load).
  `$S/lane-app.sh <out.app> <dll>…`
  as step 3's. `$S/ref-wine.app`, `$S/ref-{lb,guest,native,vm,present,perf}/` as 5a Task 1 made its own.
- Produces the ledger's facts: the heads (DXMTRS `main`, `step5w`, `step4a-translator` and whether Z is on `main`,
  MacNeutron `dxmtrs`), the key, 5b's plan status (P2), where step 3 put dxgi's private data, WSI and private
  interfaces, whether `airconv-ir` exports the `SM50_SHADER_*` mirrors, 3DMark's recording path (4a's ledger), the
  texts others read (P10's grep of `dxmt/`, `eac-vm/`, `release/`, `presenter/`, `wine-arm64/`, the Swift sources);
  every `wsi::` call in `d3d12_swapchain.cpp` against Task 4's and step 3's sets (a missing one goes into Task 4 before
  Task 7b freezes signatures); the C++'s `IMPLEMENT_ME` methods and `E_NOTIMPL` sites (Task 6's `unimpl-parity.sh`
  inputs); the implemented slots no program in `dxmt/tests` or `eac-vm/d3d/tests` calls (`OMSetBlendFactor`,
  `ClearState`, `DiscardResource`, `OMSetDepthBounds`, `SetStablePowerState` at least: the probe's `d12c` family).

- [ ] **Step 1: Base.** `main` has step R, step 3 and 5a; `step5w` has 5a's W lanes merged (5a Task 22 Step 6). Every
  path this plan names as existing passes `test -e`, and each cited line still says what this plan says; moved lines
  ledgered. If Z is on `main`, `airconv-ir` is too and P3's scaffold is not needed.
- [ ] **Step 2: Kit.** Copy or write the scripts; each control as 5a and step 3 state them. `lane-app.sh` with `ref`'s
  own `d3d12.dll` swapped in, then `kc.sh` on it → the same ok set as Step 4's reference run (the kit's control).
- [ ] **Step 3: Branches.** `git -C $D branch step5c step5w` (moved to Task 4's head later); `$S/wt-base` on it; RL and
  `sh tools/audit.sh` → `rust-check: ok`, `audit: clean (<n>)`.
- [ ] **Step 4: References** (one lock job): `main`'s head detached in `$D`; `make wine-arm64 dxmt-tests
  dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot`, `sh eac-vm/dxmt/build.sh`, `sh
  eac-vm/dxmt-native/build.sh`, `sh eac-vm/dxmt-native/build-tests.sh`; copies as 5a Task 1 Step 3 (`cp -cR` wine.app,
  guest, native, the native test programs into `$S/native-tests`, the test programs into `$S/wt-kit/build`), each
  tabulated (`ident.sh --tabulate`), cdhash ledgered; the build's `$SRC/dxmt-translator-key` copied to `$S/key` and
  ledgered (every LB configures with it; a later `make wine-arm64` of another step rewrites `$SRC`'s copy);
  `kc.sh $S/ref-wine.app ref` → `$S/ref-perf/check-{x64,arm64ec}.txt`, 0 FAIL; the VM and present references
  (`MN_REF_*` forms of 5a Task 1 Step 3) → `$S/ref-vm` (45, a superset of the native gate's 31 rows),
  `$S/ref-present`.

### Task 1: `SM50*` declarations, the native not-implemented flag and handle count (lane `sys`, on `step5w`)

**Files:** create `src/winemetal/sys/sm50-abi.h`, `src/winemetal/sys/src/sm50.rs`, `src/winemetal/sys/src/sm50/{abi.rs
(generated),tests.rs}`; modify `src/winemetal/sys/src/lib.rs` (one `pub mod` line, three native declarations and the
`CURRENT_PROCESS` pseudo-handle), `src/nativemetal/win32_event.c`, `rustfmt.toml` (by the owner: `sm50/abi.rs`);
MacNeutron: create `eac-vm/dxmt-native/tests/{wmt-exports-test.c,wmt-exports.sh}` (`win32-test.cpp` and `win32.sh`
unchanged: `win32.sh` compiles header-only against the shared `$D/src/util` and links no `winemetal.dylib`).

**Interfaces:**
- Produces `winemetal_sys::sm50`: `SM50Initialize`, `SM50Destroy`, `SM50Compile`, `SM50GetCompiledBitcode`,
  `SM50DestroyBitcode`, `SM50GetErrorMessage`, `SM50FreeError`, `SM50CompileGeometryPipelineVertex`,
  `SM50CompileGeometryPipelineGeometry`, `SM50CompileTessellationPipelineHull`, `SM50CompileTessellationPipelineDomain`,
  `SM50GetArgumentsInfo` (winemetal's exports, `winemetal.def:131-142`), with the argument-chain and reflection mirrors
  they take (`SM50_SHADER_COMPILATION_ARGUMENT_DATA` and its kinds, `MTL_SHADER_REFLECTION`, `SM50_COMPILED_BITCODE`; or
  `airconv-ir`'s, re-exported, per Task 0). `sm50-abi.h` includes `airconv_public.h` and repeats the twelve prototypes
  in a `dxmt-abi` block, so clang rejects a copy that drifts; `--seam-abi sm50-abi.h winemetal_sys::sm50` pins the Rust
  to it on the five targets (5a Task 3's mode). `SM50GetErrorMessageString`'s inline C++ is ported as
  `sm50::error_message(err) -> String`.
- Produces (native C, `win32_event.c`): `void wmt_native_not_implemented(void)` sets the calling thread's flag; `bool
  MTLD3DNotImplemented(void)` returns it and clears it; `uint32_t wmt_native_handle_count(void *h)` = an atomic load
  of `mn_win32_handle::refs` (the C++'s `HandleCount`, `d3d12_shared_fence.hpp:35-46`; 0 for a non-handle, by
  `mn_win32_is_handle`), so the shared-fence sweep never copies the struct's layout; all three exported from
  `winemetal.dylib`; declared in `winemetal-sys` under `cfg(not(windows))` with 5a P12's prototype check, beside
  `CURRENT_PROCESS` (`GetCurrentProcess`'s `-1` pseudo-handle, named once next to 5a's `DuplicateHandle` wrapper).
- Produces (MacNeutron): `sh eac-vm/dxmt-native/tests/wmt-exports.sh <winemetal.dylib>` compiles `wmt-exports-test.c`
  linked against that dylib (`-Wl,-rpath` to its folder), into a folder under `$TMPDIR`, and prints `wmt-exports: ok`
  or `wmt-exports: FAIL <cases>`.

- [ ] **Step 1: RED.** `sm50::tests::{error_message_as_cpp, chain_layouts_as_cpp, ia_input_element_packs_as_c}` (the
  last by value: C writes `{step_function, step_rate}` = `{1, 0x5a5a5a5}`, `{0, 0x7fffffff}`, `{1, 0}` into
  `SM50_IA_INPUT_ELEMENT`s and the Rust packer's bytes equal them on the host and, through step 3 D8's throwaway
  program, on i686 and the other PE targets; bool fields written as 0/1 bytes; or on `airconv-ir`'s mirror if Task 0
  finds one: `HashCompileArgs` hashes the unpacked values, so a wrong packing caches wrong AIR under the C++'s key);
  in `wmt-exports-test.c` `not_implemented_flag_reads_and_clears` (set → true, then false; another thread's set leaves
  this thread false) and `handle_count_follows_refs` (a new event 1, after `DuplicateHandle` 2 on either handle, after
  one `CloseHandle` 1, a non-handle and `-1` 0); a scratch drifted prototype in `sm50-abi.h` (an `int` for a
  `size_t`) → the clang check fails.
- [ ] **Step 2: GREEN** (rebased onto Task 2, whose `--seam-abi` carries `-I src/airconv` for `sm50-abi.h`'s
  include). `UT(winemetal-sys)`, MB(winemetal-sys), `LB(native)` → `nm -gU` of its `winemetal.dylib` lists
  `_MTLD3DNotImplemented`, `_wmt_native_not_implemented` and `_wmt_native_handle_count`; `sh
  eac-vm/dxmt-native/tests/wmt-exports.sh $S/lb/wt-sys-native/<its winemetal.dylib>` → `wmt-exports: ok` (host work:
  the lane's own dylib, nothing shared); RL.
- [ ] **Step 3: Commit** `winemetal: the translator's C API, the native not-implemented flag and handle count, for the
  front ends`; MacNeutron (`step5c-mn`) `tests: wmt-exports, native winemetal's exports for the Rust front ends`.

### Task 2: COM groundwork (lane `com`, on `step5w`)

**Files:** `tools/winemetal-layouts.sh` (`--com`); `src/util/src/{com.rs,com/tests.rs,ext.rs,dxgi_vtbl.rs (generated)}`;
`src/util/src/private_data{.rs,/tests.rs}` moved from step 3's dxgi with `git mv`, dxgi importing it; dxgi's private
interfaces (`IMTLDXGIAdapter` to slot 20, `IMTLDXGIOutput` slot 29, `IMTLSwapChainFactory` slot 3, `GammaRamp`'s users)
moved into `dxmt_util::ext`.

**Interfaces:**
- Produces `sh tools/winemetal-layouts.sh --com <header> <module> <Interface>… "$LM/bin"`: with `-DCINTERFACE`, from
  the headers each build compiles against (`meson.build:24-37`): llvm-mingw's sysroot through its clang's AST on the
  four PE targets, `include/native/{d3d12,windows,directx}` with `-DWIDL_EXPLICIT_AGGREGATE_RETURNS` through Apple
  clang's on darwin; one `#[repr(C)] pub struct <I>Vtbl` per interface, its slots in order as `pub <snake_name>: unsafe
  extern "system-unwind" fn(...)`, every pointer to an interface or struct `*mut c_void`/`*const c_void` (const kept),
  integers at their width (`SIZE_T` as `usize`), enums as `u32` (checked non-negative), by-value structs as the mirror
  named in the mode's table (`D3D12_CPU_DESCRIPTOR_HANDLE`, `D3D12_GPU_DESCRIPTOR_HANDLE`, `LUID`, …; an unmapped one
  fails), and per target a `const _: () = assert!(size_of::<<I>Vtbl>() == <n> * size_of::<usize>())` and, per slot, the
  summed sizes of the Rust parameters (each rounded up to the stack slot, as stdcall pops them on i686) equal to the
  C's, printed by step 3 D8's throwaway program from the same AST, plus each by-value mirror's size (a by-value `UINT64`
  such as `D3D12_GPU_VIRTUAL_ADDRESS` or `D3D12_GPU_DESCRIPTOR_HANDLE` mapped to `usize` corrupts i686's stack and
  passes on x64 and ARM64EC); fails when the targets disagree beyond pointer width. `src/util/src/dxgi_vtbl.rs` = `--com
  dxgi1_6.h` over `IDXGISwapChain4` and its bases, `IDXGIFactory`, `IDXGIOutput`, `IDXGIAdapter` (what both swap chains
  call).
- Produces `dxmt_util::com::ComObject` (`pub_count: AtomicU32`, `priv_count: AtomicU32`; `add_ref() -> u32`,
  `release() -> u32` returning the C++'s values, `add_ref_private()`, `release_private() -> bool` (true: free now),
  `private_count()`), and `ComObjectWithInitialRef` if Task 0 finds a D3D12 or D3D11 class on it;
  `dxmt_util::private_data::PrivateData` with step 3 D6's semantics unchanged.

- [ ] **Step 1: RED.** `com::tests::{counts_as_cpp (public 1→0 drops to private, private 0 frees once),
  private_hold_outlives_public}`; `ext::tests::dxgi_slots_as_step3`; a scratch header with one slot's `UINT` argument
  changed to `UINT64` → `--com`'s parameter-byte assert fails on i686 (and the slot count and offsets still equal).
- [ ] **Step 2: GREEN**; regenerate twice → equal; dxgi builds and its tests pass from the new homes; `UT(dxmt-util dxgi)`,
  MB(dxmt-util), MB(dxgi), RL.
- [ ] **Step 3: Commit** `dxmt-util: generated COM vtables, the two-count COM object and private data, for every front
  end`.

### Task 3: core groundwork: fences and the translation cache's instances (lane `core`, on `step5w`)

**Files:** `src/dxmt/src/fence{.rs,/tests.rs}`, `src/dxmt/src/shader_cache/{instance,version}.rs` (+ tests);
`shader_cache.rs` (5a S4's: its `mod` lines and one method, `Store::close`); `src/dxmt/src/lib.rs` (`pub mod fence;`).

**Interfaces:**
- Produces `dxmt_core::fence::{Fence, Generation, EventListener}` from `src/dxmt/dxmt_fence.{hpp,cpp}`: `Fence::new(device:
  &Obj, helper_queue: &Obj) -> Arc<Fence>`, `current() -> Generation`, `ask(value) -> Generation` (a lower value starts a
  generation), `signal(value)` (the shared event, then the MTLEvent through the helper queue), `forward(&Generation,
  value)`, `completed_value()`, `wait(value, timeout)`, `shared_event() -> Obj`; `EventListener` wraps winemetal's
  listener exactly as `dxmt_fence.cpp:60-86` does: `new()` = `SharedEventListener_create` plus a thread (P16, P20's
  guard) running `SharedEventListener_start`; `Drop` = the C++'s 10 ms sleep, `SharedEventListener_destroy`, join;
  `set_event_on_value(&Fence, event: Handle, value)` through slot 104 (`MTLSharedEvent_setWin32EventAtValue`);
  `set_event_on_values(fences: &[&Fence], values: &[u64], all: bool, event: Handle)` through slot 146
  (`MTLSharedEvent_setWin32EventAtValues`, `needed = if all { count } else { 1 }`, each fence's current shared event
  held through the call; `SetEventOnMultipleFenceCompletion`, `d3d12_device.cpp:1490-1508`). winemetal sets the event
  on its listener thread and, natively, holds it from registration to the set (`wmt_native_ref`, `g7.rs:125-150,
  571-573`), so a guest that closes its relay event first never makes the host set a freed handle. The Rust never sets
  an event itself but on the caller's thread when the value is already reached (Task 8, `d3d12_fence.cpp:71-82`:
  kernel32's `SetEvent` on PE, `wmt_native_set_event` natively; never a Rust copy of `mn_win32_handle`). If 5a's lane
  E1 already exposes an event-listener type, `EventListener` is that type (Task 0 ledgers).
- Produces `dxmt_core::shader_cache::{version(key_or_version: &str, airconv_version: u32) -> u64,
  instance(metal_version: u32, version: u64) -> Arc<Store>, close_all()}` and `Store::close(&self)` (P8: reader then
  writer taken out under their locks and released; later `read`/`write` see the cache as off).

- [ ] **Step 1: RED.** `fence::tests::{lower_value_starts_a_generation, signal_forwards_nonzero_only,
  listener_registers_through_slot_104, multiple_fences_through_slot_146 (needed = count for all, 1 for any),
  listener_drop_destroys_then_joins}` (winemetal fakes recording the slot calls; mutation: a direct set instead of a
  slot call → FAIL); `shader_cache::tests::{version_is_the_cpps (FNV over "71a7e5f2…3311" and 26, and over a
  `DXMT_VERSION` text, against values printed by the C++ function compiled on the host), one_store_per_metal_version,
  close_all_closes_every_store (an extra `Arc<Store>` held: both releases still seen, in the C++'s order, and a later
  read misses)}`.
- [ ] **Step 2: GREEN**; `UT(dxmt-core)`, MB(dxmt-core), RL.
- [ ] **Step 3: Commit** `dxmt: the core's fence and the translation cache's instances`.

### Task 4: WSI, swap-chain helpers and the CPU clock (lane `wsi`, on `step5w`)

**Files:** `src/util/src/wsi{.rs,/{win32.rs,hooks.rs,tests.rs}}` (step 3's dxgi WSI moved with `git mv`, dxgi importing
it), `src/util/src/qpc{.rs,/tests.rs}`, `src/dxmt/src/swapchain{.rs,/tests.rs}`.

**Interfaces:**
- Produces `dxmt_util::wsi`: step 3's seven functions, plus the window functions the swap chains call, from
  `src/util/wsi_window_win32.cpp` (PE) and `wsi_window_headless.cpp` over the WSI hooks (natively): `window_size`,
  `window_monitor`, `is_window`, `is_minimized`, `save_window_state`, `set_window_mode`, `enter_fullscreen_mode`,
  `leave_fullscreen_mode`, `restore_display_mode`, `resize_window`, `update_fullscreen_window`,
  `is_process_foreground`, `minimize_window` (the swap chain minimises a fullscreen window the game leaves in the
  background, `d3d12_swapchain.cpp:414-423`: on PE from `wsi_window_win32.cpp`, natively the hook's
  `WMTWsiMinimizeWindow` action and flag 8, `wsi_window_headless.cpp:103-108`), plus any call Task 0's ledger adds,
  `WindowState` (the C++'s `DXMTWindowState` fields, Rust-only: the forwarder keeps its C++ copy, 5-D7), with the
  C++'s log lines.
- Produces `dxmt_util::qpc() -> (u64, u64)` (count, frequency): kernel32's `QueryPerformanceCounter` and
  `QueryPerformanceFrequency` on PE; `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` and 1 000 000 000 natively, as
  `win32_native.h:390-399` (`Instant` exposes no raw value; `mach_absolute_time` counts 24 MHz ticks). Tasks 13 and
  16a's `GetClockCalibration` and timestamp frequency use it.
- Produces `dxmt_core::swapchain::{convert_format, convert_color_space, monitor_format_bpp, render_scale}` from
  `d3d12_swapchain.cpp:42-100` (the copies D3D11's swap chain shares, `:60`).

- [ ] **Step 1: RED.** `wsi::tests::{fullscreen_round_trip_restores_state (hooks fake), minimized_window_reports,
  background_leave_minimises (hooks fake: flag 8 clear → the minimise action)}`; `qpc::tests::native_is_uptime_raw_ns`
  (frequency 1e9; the count between two `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` reads);
  `swapchain::tests::{formats_as_cpp, color_spaces_as_cpp, render_scale_parses_as_cpp}` (tables read from the C++ by
  the implementer, the reading noted).
- [ ] **Step 2: GREEN**; dxgi's tests from the new home; `UT(dxmt-util dxmt-core dxgi)`, MB, RL.
- [ ] **Step 3: Commit** `dxmt-util: window and display calls for the swap chains, and the CPU clock`.

### Task 5: the D3D12 probe (lane `probe`, MacNeutron; goldens from the C++)

**Files:** `dxmt/tests/d3d12_probe.cpp` (built by `make dxmt-tests`, `Makefile:60`'s wildcard, and
`build/dxmt-tests-i386`), `dxmt/tests/d3d12_probe.{fixed,native,i386,guest64}`; `eac-vm/dxmt-native/build-tests.sh` (the
probe and its `.native` check, as step 3 P3 added `d3d11_dxgi`); `eac-vm/tests/dxgi-guest.sh` (`MN_PROBE=<program>`,
default `d3d11_dxgi`, comparing with `<MN_PROBE_GOLDEN>/<program>.<arch>`, `MN_PROBE_GOLDEN` default `dxmt/tests`, so a
lock job records and compares under `$S` and never writes the main checkout); `dxmt/tests/d3d12_dlss.cpp` (a
`framegen-still` mode: one fixed frame through the upscaler, presented for 3 s, for the `dlss framegen pixels same`
row).

**Interfaces:**
- Produces `d3d12_probe.exe <fixed|full> [--ported] [families…]`, lines `<family> <case> <answer>`; `fixed` prints
  relations where machines or lanes differ (LUIDs, timestamps, GPU addresses, feature values that depend on the GPU),
  exact values elsewhere; `--ported` adds the `NOT_ON_C` cases (step 3 P6: foreign pointers, P6 here). Families:
  - `d12x` the seven exports: feature levels, null out-pointer (`S_FALSE`), a foreign adapter, the two `E_NOINTERFACE`s,
    serialization and deserializers over 5a's 12-desc table (sizes, SHA-1 of blobs, error-blob texts);
  - `d12d` the device: QI matrix (every D3D12 IID, the refused DXGI/D3D10/D3D11 and downlevel IIDs,
    `IMTLD3D12DeviceExt` as `same`/`other`/`none`), refcount returns, node count, LUID relation, descriptor increments,
    `CheckFeatureSupport` per feature, `Create*` argument errors, private data and `SetName`, residency calls, reserved
    resources (`E_NOTIMPL`), shared handles;
  - `d12o` every object kind: QI set, `GetDevice` identity, `GetDesc` through its out pointer, private data, refcount
    returns, and lifetime through a counting `SetPrivateDataInterface` (a PSO and root signature released while a list
    records with them die at the list's `Reset`; a heap outlives its placed resource's public release);
  - `d12h` descriptor handles: CPU/GPU starts and increments as relations, `CopyDescriptors*` counts, RTV heaps refusing
    `SHADER_VISIBLE`; skipped (`untested`) on i686;
  - `d12p` pipelines: stream answers (AS/MS `E_NOTIMPL`, unknown subobject), `GetCachedBlob` size, library
    store/load/serialize, a blob with another magic (`D3D12_ERROR_DRIVER_VERSION_MISMATCH`), a truncated one
    (`E_INVALIDARG`), a name/description mismatch (`E_INVALIDARG`);
  - `d12l` lists, allocators, queues, fences: `GetType` per allocator type, a type mismatch, `Reset` while recording,
    `Close` twice, queue `GetDesc`, timestamp frequency and calibration as relations (the calibration's CPU value
    between the probe's own `QueryPerformanceCounter` reads, before ≤ cpu ≤ after, on every lane, natively included),
    CPU and GPU `Signal`/`Wait`, `SetEventOnCompletion` (already reached, pending, no event),
    `SetEventOnMultipleFenceCompletion` any and all, a shared fence handle round trip;
  - `d12s` the swap chain on a hidden window through dxgi's factory and the queue: QI set, `GetDesc1`, `GetBuffer`,
    `GetCurrentBackBufferIndex` over presents, `ResizeBuffers`, the latency waitable and `SetMaximumFrameLatency`,
    `CheckColorSpaceSupport`, `SetColorSpace1`, `SetHDRMetaData`;
  - `d12c` each implemented slot Task 0 ledgered as called by no program (`OMSetBlendFactor`, `ClearState`,
    `DiscardResource`, `OMSetDepthBounds`, `SetStablePowerState`, …), once with benign arguments, then a draw or read
    that shows its effect where one exists → the answer (in `.fixed`, `.native` and `.guest64`; the i686 golden as
    `d12h`'s rule allows);
  - `d12n` natively only: one call into an `IMPLEMENT_ME` per return kind → `not implemented` (the C++'s
    `std::logic_error` caught, or `MTLD3DNotImplemented()` once it exists, `dlsym`ed);
  - `d12r` only when named, never in a golden (P19): ns/call of draws with root constants, `ResourceBarrier` and
    `ExecuteCommandLists` in step 2's call-rate form.

- [ ] **Step 1: The program**, then on `$S/ref-wine.app` through `run.sh`: `fixed` twice per Mac lane → identical,
  the four equal → `d3d12_probe.fixed` (a lane difference ledgered, its own file); `full` twice per lane into
  `$S/ref-probe/<lane>.full.{1,2}`, differing keys → `full.mask`.
- [ ] **Step 2: Native.** Built as step 3 built `$S/probe-native/d3d11_dxgi` (its P3 link, ledgered in Task 0), against
  `$S/ref-native`: `fixed` twice → `d3d12_probe.native`.
- [ ] **Step 3: Mutation RED.** One flipped answer in a copy of each golden fails its comparator (`diff`, the
  `build-tests.sh` check, `dxgi-guest.sh`). `d3d12_dlss framegen-still` runs on both Mac lanes of the reference (exit
  0). Commit (`step5c-mn`) `tests: d3d12_probe and its Mac and native goldens, d3d12_dlss framegen-still`.
- [ ] **Step 4: Guest goldens** (one lock job, 5a Task 20's form; Task 0 Step 4 staged nothing, and step 4's and 5b's
  jobs restage the image in between): `$D` detached at Task 0's `main` sha; `<M>` detached at `step5c-mn` (Steps 1-3
  committed) and restored to `dxmtrs` by `trap … EXIT` (pipeline rule 6: VM jobs run the main checkout's
  `builder.sh`); builder up with `VM_SHARES`; `sh eac-vm/dxmt/build.sh`, `sh eac-vm/image/guest-stages.sh dxmt d3d`,
  `sh eac-vm/builder.sh sync eac-vm/image mn-image`, `sh eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`;
  `dxmt-guest/.mn-source` equals that sha without `+dirty` (ledgered, with the staged DLLs' sha256);
  `MN_PROBE=d3d12_probe MN_PROBE_GOLDEN=$S/ref-probe/guest.<n> sh eac-vm/tests/dxgi-guest.sh i386 record` and
  `x86_64 record`, for n = 1, 2 → identical; then without `record` against `guest.1` → `dxgi-guest i386: ok`,
  `dxgi-guest x86_64: ok`. After the job, `guest.1`'s files copied into `$S/wt-mn/dxmt/tests` as `.i386`,
  `.guest64`; commit (`step5c-mn`) `tests: d3d12_probe's guest answers as DXMT's C++ gives them`.

### Task 6: the reference corpus (owner, host on Task 0's copies)

**Files:** scratch `$S/ref-corpus/**`, `$S/golden/**`, `$S/{rec-compare.sh,dump-compare.sh,log-compare.sh,crosshit.sh,
replay.sh,statnames.sh,unimpl-parity.sh,pso-compare.sh,fgpix.sh}`.

**Interfaces:** each comparator prints `<name>-compare: equal` or the first difference.
- `stats-golden.sh` and `hist.sh` (5a's) widened to every D3D12 program check.sh runs, with its arguments.
  `stats-compare.sh` gains 5c's form (`--names-as-set`): counters exactly, the header count and each header's frames,
  and the timed names per thread as a set, not their order (the C++ sorts timed lines by ticks and prints the top 30 a
  thread, `d3d12_stats.cpp:121-123`, so the Rust's other costs reorder them with no port error).
- `statnames.sh <tree>`: the C++'s `DXMT_STAT_SCOPE` and `DXMT_STAT_COUNT` literals (`/usr/bin/grep` of `src/d3d12` at
  Task 0's sha) equal the `scope!`/`count!` literals of `<tree>`'s `src/d3d12/src` and `src/dxmt/src/presenter/framegen*`
  → `statnames: equal (269, 29)` (da6ed2d's unique scope and counter literals; Task 0 re-counts): a label below a
  thread's 30th never prints, so only this catches a missing or renamed `scope!`.
- `unimpl-parity.sh <tree>`: the `Interface::Method` names of `<tree>`'s `not_implemented!` calls (P7) equal Task 0's
  list of the C++'s `IMPLEMENT_ME` methods (the i686-only ones, P7, under `cfg(target_arch = "x86")`, listed apart),
  and every method whose C++ body returns `E_NOTIMPL` has an `E_NOTIMPL` return in its Rust function (found by its
  `scope!` name) → `unimpl-parity: equal (<n> stubs, <m> E_NOTIMPL)`; otherwise each extra stub and missing answer.
- `golden/framegen-adapt.cpp`: `d3d12_command_queue.cpp:2212-2250` (`AdaptFrameGen`) verbatim over a stub `FrameGen`
  struct, driven by a scripted trace across each boundary (`1.1 × max(2/refresh, pair, learned_cap)`, the 500 ms
  holds, `wait > 0.1 × interval`, the 60 s expiry), printing every decision and stderr line →
  `golden/framegen-adapt.{trace,txt}`.
- `golden/framegen-present.txt`: written from `d3d12_command_queue.cpp:2425-2468` and reviewed against it: the call
  sequence `PresentFrameGen` makes on a recording fake (the pacing wait; the `wait` callback, `Order(nullptr, true)`'s
  join over the frontier with `waits_` and `ScratchFill`, into the begin blit encoder; `updateFence(fg.fence)`; the
  `read` event wait from the fourth frame; the interpolation with every prop: `motion_vector_scale_{x,y}`,
  `delta_time`, near 0.1, far 10000, fov 70, aspect, `jitter_offset_{x,y}`, `depth_reversed`; the copy encoder waiting
  `fg.fence`, then the `update` callback, `fences_[done]`; the `written` signal), for a generated and a not-generated
  frame.
- `golden/wmt-trace.c`: a `DYLD_INSERT_LIBRARIES` interposer on the native `winemetal.dylib`'s render-pipeline,
  compute-pipeline, depth-stencil and sampler creators, printing each info struct (blend ops and factors, per-target
  write masks, alpha-to-coverage, rasterizer state, the depth-stencil variants) with handles masked, a line a creation;
  `pso-compare.sh a b`: two traces as sorted multisets → `pso-compare: equal`.
- `fgpix.sh <app> <out>`: `d3d12_dlss.exe <ngx> framegen-still` (Task 5's private build of the program through the
  kit, as the probe's is: Task 0's `$S/wt-kit/build` copy predates the mode) with `MACNEUTRON_DLSS=1
  MACNEUTRON_FRAMEGEN=always` through `run.sh` on `<app>`, read during its 3 s by `build/wine-arm64-tests/winshot d3d12_dlss <out>.png` → its
  `pixels … green … white …` line; two outputs' lines equal → `dlss framegen pixels same` (the backstop of Task 15's
  `present_orders_as_cpp`: the ring textures are untracked, `:2371`, so Metal's validation stays silent on a missing
  join).
- `rec-compare.sh a b`: `cmp` of the `.pipelines` files of `d3d12_cache`, `d3d12_triangle` and the other single-threaded
  recording programs check.sh runs under `DXMT_PIPELINE_RECORD`.
- `dump-compare.sh a b`: `diff -r` of two `DXMT_DXIL_DUMP` folders, `stats.txt` left out.
- `log-compare.sh a b`: two `<exe>_d3d12.log` files equal after `0x[0-9a-f]+` → `0x?` (the mask ledgered).
- `crosshit.sh <app-a> <app-b>`: each D3D12 program check.sh runs, cold into `$S/cache/x` with `<app-a>`, then with
  `<app-b>` on that cache: the `d3d12 shader cache:` line's misses 0 and the table count 1.
- `replay.sh <native-lib-dir> <out>`: native `dxmt-replay` from `<native-lib-dir>` on 3DMark's recording (read in place)
  with `DXMT_SHADER_CACHE_PATH=<out>/cache/` → its final `replay:` line without the time, and the sorted
  `(key, sha256(value))` list of `shaders_310.db` by `sqlite3`.

- [ ] **Step 1: Record** on `$S/ref-wine.app` and `$S/ref-native`, twice each: stats, histograms, recordings, dump
  folders, logs, `replay.sh $S/ref-native`, the `wmt-trace` of that replay and of `$S/native-tests` (as
  `build-tests.sh` runs them), `fgpix.sh` on both Mac lanes; each pair equal (a run-dependent item ledgered as compared
  by presence). `crosshit.sh $S/ref-wine.app $S/ref-wine.app` → no misses (the comparator's control).
- [ ] **Step 2: SMITE 2's root signatures** (P18): a host harness (5a's `golden/d3d12_native.cpp` form) creating a
  root signature from each `rs-*.bin` read in place on `$S/ref-native`'s `d3d12.dylib` → per blob the HRESULT and the
  qword offsets → `$S/ref-corpus/rs-smite.txt`, twice equal.
- [ ] **Step 2b: Framegen goldens.** `golden/framegen-adapt.cpp` built and run twice → equal; `framegen-present.txt`
  written and reviewed. Task 7a commits both under `src/dxmt/src/presenter/framegen/golden/` (lanes never edit a
  golden).
- [ ] **Step 3: Mutation RED** per comparator (a flipped byte, a changed count, a deleted dump file, a missed key; a
  renamed label in a copy of the C++ list → `statnames` FAIL; one blend op swapped in a trace copy → `pso-compare`
  FAIL; `1.1` → `1.0` in a copy of `framegen-adapt.cpp` → its output differs; a changed `fgpix` line → FAIL).
  `unimpl-parity.sh`'s RED runs at Task 17 (a filled slot reverted to the stub → FAIL).

### Task 7a: the skeleton's build and shared files, the meson flip (owner, on `step5c`)

Runs after P3's scaffold commit on `step5c`.

**Files:** `src/dxmt/meson.build` (the `dxmt_translator_key.h` `configure_file` assigned to a variable that
`C/meson.build` reads; 5b reuses it), `C/Cargo.toml` (final dependency and feature lists: `airconv-ir` is on `step5c`
from the scaffold), `C/build.rs` (P8), `C/meson.build` (P5: the Rust DLL for every target, `env` for
`DXMT_TRANSLATOR_KEY_H`/`DXMT_VERSION_H` with `dxmt_version` as a dependency, natively `-Wl,-exported_symbol` × 7 and
`-Wl,-dead_strip`, P4); `C/src/lib.rs` (complete: the seven exports, `DllMain`, `dxmt_log`, `INIT`; `D3D12CreateDevice`
reaches a stub that 7b's `device` module replaces), `C/src/{stats,unimpl,com}.rs` (complete: `scope!`, `count!`,
`not_implemented!("<Interface>::<Method>")`, P20's `slot!` prologue with the guard, the object header helpers over
`ComObject`, the thread-spawn helper with the guard and P16's stack), `C/src/encoder.rs` (complete: the contract
types), `C/src/abi/vtbl.rs` (`--com` over `d3d12.h`'s implemented interfaces), `C/src/abi/ext.rs` (P6's companion
vtables and the per-width desc asserts), `C/src/abi.rs`'s `mod` lines and the empty per-lane
`C/src/abi/{objects,views,list_state,list_ops,queue,swapchain}.rs` (a `//!` line each; each lane owns its file),
`rustfmt.toml`, `tools/audit.sh` (P10's `eprintln!`/`println!` rule for `src/d3d12` and `src/dxmt`), Task 6's
framegen goldens committed as `src/dxmt/src/presenter/framegen/golden/{adapt.trace,adapt.txt,present.txt}`.

- [ ] **Step 1: RED.** `UT(d3d12)` fails (no module); `lib::tests::{exports_answer_as_cpp_without_a_device
  (D3D12GetInterface/GetDebugInterface E_NOINTERFACE and their WARN lines, feature level < 11_0 E_INVALIDARG),
  vtables_have_the_cpp_slot_counts}`, `com::tests::slot_and_thread_panics_abort` (P20), `build_rs` cases (`cargo test
  -p d3d12` with both variables unset builds; `DXMT_VERSION_H=/nonexistent` fails the build); a scratch `eprintln!` in
  `C/src/lib.rs` → `sh tools/audit.sh` FAIL.
- [ ] **Step 2: GREEN**: the generator run, the complete files; `UT(d3d12)` (and natively with `--features
  perf-build`: the slot and thread guard still abort), MB(d3d12) on all four (the i686 desc asserts compile), `LB(mac)`,
  `LB(native)`, `LB(guest64)`, `LB(guest32)`, `LB(mac) p` ok; the p build's `d3d12.dll` has no `us in D3D12`,
  `stats.txt` or `counters:` text (`LC_ALL=C /usr/bin/grep -c`; the probe text is Task 16a's to check: until its slot
  is filled, fat LTO drops it from both builds); natively `nm -gU` = the seven names; audit clean; RL.
- [ ] **Step 3:** `run.sh` on a `lane-app.sh` app with this `d3d12.dll`, `d3d12_null` on the x64 lane: it reaches the
  creation stub and the log names it (the kit runs the Rust). Commit `d3d12: the Rust DLL's build and shared files
  (the switch lands with the C++'s removal)`.

### Task 7b: every signature and the device's dispatch (owner, on `step5c`; reviewed before any lane branches)

**Files:** every module file of the Files columns of Tasks 8-16b with its final pub items and stub bodies (each stub
`not_implemented!("<Interface>::<Method>")`, or its function's path for an internal one, so a reached stub says
which); `C/src/device.rs` and `C/src/device/vtbl.rs` (complete: the `Device` struct, every slot a one-line dispatch
through `slot!` into the owning lane's function); `C/src/list.rs` (complete: the `List` struct with every field of the
C++ list and the seams between 12b and 12c: the recording state both read, the encoder open/close helpers, `PreDraw`'s
entry that `ExecuteIndirect` calls); `src/dxmt/src/presenter/framegen.rs` (stub with its final pub items) and its `pub
mod framegen;` line in 5a's `presenter.rs` (the only edit of that file in this plan).

**Interfaces (final; lanes code against them):**
- Consumes: `dxmt_util::ext`'s three D3D12 private interfaces' IIDs and structs (5a S0c; nvngx's and Wine's libxess's
  copies are of the same frozen definitions; the vtables are 7a's `abi/ext.rs`, P6), Task 2's vtables, `ComObject` and
  `PrivateData`, Task 1's `sm50`, Task 3's `Fence`, `EventListener` and cache instances, Task 4's `wsi`, `qpc` and
  `swapchain`, 5a's S1-S4 and lanes R and D.
- `encoder.rs` (7a's): D3D12's `EncoderType` (Null, Clear, Render, Blit, Compute, Resolve, TemporalUpscale),
  `EncoderData` and the six kinds as `d3d12_command_encoder.hpp:25-183` lays them out (5a Task 1's `dxmt::d3d12`
  names), `BarrierOpacity` (`d3d12_device.hpp:79-94`), and `Submission`: the fields the queue reads from a closed list
  (`entry`, `encoder_count`, `barrier_count`, `timestamp_resolves`, `timestamps`, `custom_resolves`, `barrier_names`,
  `barrier_opaque`, `d3d12_device.hpp:68-95`).
- `Device` (fields of `d3d12_device.cpp:341-370`): `mtl: Obj`, `adapter` (`IMTLDXGIAdapter`, slot 19 → `u64`),
  `residency: device::residency::Residency`, `null: device::null::NullResources`, `library: dxmt_core::library::Library`,
  `formats` (`dxmt_core::format::capabilities`), `lock_table`, `upscalers`, `fence_helper: Obj` (queue),
  `events: dxmt_core::fence::EventListener`, `temporal_supported: bool`.
- One cross-lane function per seam (each lane fills its own): `device::factory::create(...)`,
  `device::residency::{register, register_with_va, unregister, lookup_buffer_by_va}`, `device::null` (Task 16a);
  `descriptor_heap::{create, shader_visible(handle) -> (&Heap, u32), render_target(handle), sampler(handle),
  write_srv/uav/cbv/rtv/dsv/sampler(...)}` (Task 8; i686: abort, P7); `root_signature::object::RootSignature { blob,
  qword_offsets, static_samplers, flags, sha1 }` (Task 9); `shader_cache::{CachedShader::new(&[u8]) ->
  Result<Arc<CachedShader>, Hresult>, compile_function(&[&CachedShader], entry: &str, args: &ArgChain) -> Result<Obj,
  CompileError>, log_counters()}` (Task 9); `pipeline::{Graphics, Compute}` with `pso`, depth-stencil variants, GS
  variants, threadgroup size, `desc_hash` (Task 10); `resource::Resource` (`buffer()`, `texture()`, `gpu_va()`,
  `desc()`, `heap_props()`, `create_*_view` into `descriptor_heap`'s writers) (Task 11); `allocator::Allocator` (Task
  12a); `list::List::submission(&self) -> &Submission` (Task 12c), the draw entries (12b); `queue::Queue::{execute(&[&List]),
  signal, wait, present(presenter, backbuffer, latency_waitable, after: f64, refresh: f64) -> Hresult}` (Task 13);
  `queue::dump::{Dumps, on_present(…), pass_label(…)}` (Task 14); `dxmt_core::presenter::framegen::{mode() -> Mode,
  record_upscale(&TemporalScaler, depth: &Obj, motion: &Obj, props: &WmtFxTemporalScalerProps), Order (trait: `wait(&mut
  self, begin: &Obj)` encodes the queue's join into the begin blit encoder: `Order(nullptr, true)` over the frontier,
  `waits_`, `ScratchFill`, `d3d12_command_queue.cpp:2430-2436`; `update(&mut self, end: &Obj)` the copy encoder's
  `updateFence(fences_[done])`, `:2465`), FrameGen::{new, present(&mut self, cmdbuf: &Obj, presenter: &Arc<Presenter>,
  frame: &Obj, metadata: &PresentMetadata, refresh: f64, order: &mut dyn Order) -> bool, notify, drain, running}}`
  (Task 15, P11; the queue implements `Order`, a test records it); `swapchain::create(factory, &Device, &Queue, hwnd,
  desc, fullscreen_desc, out) -> Hresult` (Task 16b).

- [ ] **Step 1: RED.** `device::vtbl::tests::every_slot_dispatches_to_its_lane` (a stub reached names its
  `Interface::Method`); `unimpl-parity.sh` on this tree lists every slot as an extra stub (expected here).
- [ ] **Step 2: GREEN**: the stubs and dispatch; `UT(d3d12 dxmt-core)`, MB on all four, the four LBs, RL. An Opus review
  of every pub signature against the C++ before any lane branches (a missed one rebases every lane).
- [ ] **Step 3: Commit** `d3d12: every Rust signature, stubbed, and the device's dispatch`.

### Tasks 8-16b: the lanes

Each lane: branch from `step5c` at Task 7b's reviewed commit; RED tests first from the named C++ (a golden from Task 5
or 6 where one exists, else values the implementer reads from the C++ and notes); GREEN; `UT(d3d12)` (Task 15:
`UT(dxmt-core)`), MB on all four, `LB(mac)`, `LB(native)`, `LB(guest32)`, RL; its mirrors in its `C/src/abi/<lane>.rs`
with their rows added to step 3 D8's throwaway layout program on the five targets. Then, rebased onto `step5c` in
Execution's merge order, its own program check there: `run.sh d3d12_probe fixed <its families>` on its `lane-app.sh`
app against `.fixed`'s lines for them, and the same natively on `LB(native)`'s dylibs against `.native` (16a: `d12x`,
`d12d`; 8: `d12o`, `d12h`; 10: `d12p`, with 9's paths; 13: `d12l`, with 12a-12c's; 16b: `d12s`; each `d12c` case at its
owning lane), plus the check.sh programs its files serve, with check.sh's arguments → the reference's ok titles for
those rows. A family or program that reaches a later lane's stub runs at that lane's merge instead (ledgered); not a
gate. Commit `d3d12: <what> in Rust`. Behaviour from the named C++; P6-P20 apply to every lane.

- [ ] **Task 16a (device creation, merged first).** `device/{features,factory,temporal,residency,null}.rs` from
  `d3d12_device.cpp`: creation through `IMTLDXGIAdapter` slot 19, QI answers at `:424-468` with `IMTLD3D12DeviceExt`
  only when `supportsFXTemporalScaler`, `CheckFeatureSupport`/footprints/allocation info over 5a D3 (with
  `GetResourceAllocationInfo`'s debug probe text, `:1849`, P10), `ParsePipelineStream` glue,
  `MTLD3D12TemporalScalerImpl` `:267-321`, `CreateD3D12Device`, `SetStablePowerState` (`:1453`); the residency set, VA
  multimap and newest-at-or-below lookup (`:1888-2038`); the null resources (`:1904-1996`, the 4 KiB null vertex
  buffer, the 256 KiB lock table); `TimestampFrequency` (`:1934-1952`) over `dxmt_util::qpc`. Tests:
  `device::tests::{qi_matrix_as_probe (the probe's d12d answers as data), ext_only_with_temporal_scaler}`,
  `device::residency::tests::va_lookup_takes_newest_alias`, `device::null::tests::null_views_as_cpp`. Its own check also:
  `LB(mac)`, `LB(mac) p`, `LB(native)` and `LB(native) p` → the std images have the probe text once or more, the p
  images none.
- [ ] **Task 8 (objects).** From `d3d12_descriptor_heap.{hpp,cpp}`, `d3d12_fence.cpp`, `d3d12_shared_fence.hpp`,
  `d3d12_heap.cpp`, `d3d12_query_heap.cpp`, `d3d12_command_signature.cpp` (its MSL as a string, compiled at runtime
  through slot 144, `:328-335`), `src/d3d10/d3d10_blob.cpp` (`ID3D10Blob` answered by QI, B9). Handle packing and the
  three heap layouts; shader-visible heaps on a core `Buffer` registered for residency and VA; native CPU-visible heaps
  through the memory hooks (B2); fences on Task 3's `Fence`, CPU `Signal` through the device's helper queue;
  `SetEventOnCompletion` as `d3d12_fence.cpp:71-82` (reached: the event set here, on the caller's thread; no event: a
  wait; else Task 3's `set_event_on_value`); the shared-fence sweep's handle count through `NtQueryObject` on PE and
  `wmt_native_handle_count` natively (Task 1). Mirrors in `C/src/abi/objects.rs`. Tests:
  `descriptor_heap::tests::{handle_packs_as_cpp, increments_are_32, gpu_entries_as_cpp, rtv_heap_refuses_shader_visible,
  copy_descriptors_as_cpp}`, `fence::tests::set_event_on_completion_as_cpp`, `shared_fence::tests::
  table_sweeps_closed_handles` (over a fake count obeying `wmt-exports.sh`'s pinned semantics), `command_signature::tests::
  indirect_structs_as_cpp`, `blob::tests::qi_answers_id3d10blob`, `query_heap::tests::desc_as_cpp`,
  `heap::tests::native_cpu_heap_through_hooks` (B2, a hooks fake).
- [ ] **Task 9 (shaders).** From `d3d12_root_signature.cpp:248-527` (deserializer objects and the object; serialization
  and qword offsets are 5a D2's), `d3d12_shader_cache.{hpp,cpp}`, `d3d12_pipeline.hpp`, `d3d12_dxil_dump.cpp`'s
  non-file parts (`DXILStage`, `DumpDXIL`, `DumpRootSignature`, `SM6Caps`, `CaptureHash`, `NamePipeline`), parsing through
  `airconv-ir` (`Container::parse`, `root_signature::from_container`, `RootSignature::deserialize`), 5a D2 switched onto
  `airconv-ir`'s `root_signature::raw` (its own copies deleted). The `DXIL:` prefix maps to `E_NOTIMPL`
  (`d3d12_pipeline.hpp:106-113`). Tests: `root_signature::object::tests::{smite_root_signatures_as_cpp (Task 6's
  `rs-smite.txt`; skipped with a printed reason when the folder is absent), mixed_sampler_table_refused,
  static_samplers_as_cpp, versioned_deserializer_as_cpp}`, `shader_cache::tests::{hash_compile_args_is_the_golden,
  unknown_kind_gives_up, function_key_is_the_golden, reflection_record_is_the_golden, probe_bypasses_the_cache}`
  (goldens from a host harness over `d3d12_shader_cache.cpp`'s key functions, recorded once and noted; the
  `IA_INPUT_LAYOUT` case through Task 1's bitfield packer). Mutation RED: swap two hashed fields →
  `hash_compile_args_is_the_golden` fails.
- [ ] **Task 10 (pipelines).** From `d3d12_pipeline_graphics.cpp`, `d3d12_pipeline_compute.cpp`,
  `d3d12_pipeline_library.cpp` (P14), `d3d12_pipeline_record.cpp`'s writer (P12), over 5a D4's stream parser and Task 9's
  cache; Metal 3.1 for every compile, GS mesh emulation variants on first draw, six depth-stencil variants; synchronous
  on the caller's thread. Tests: `pipeline::tests::{record_bytes_as_cpp (a fixed pipeline set against bytes the C++
  wrote, recorded once), torn_tail_cut, foreign_file_restarted, library_blob_round_trip, library_cpp_blob_loads_then_refuses_by_hash,
  input_signature_from_airconv_ir}`; Task 6's `pso-compare.sh` over the lane's native dylibs' `wmt-trace` of the
  replay → equal.
- [ ] **Task 11 (resources).** From `d3d12_buffer.cpp`, `d3d12_texture.cpp`, `d3d12_resource_helper.cpp` (over 5a D3),
  over 16a's residency and null resources; placed resources on the heap's `MTLHeap` or the hooked CPU buffer;
  CPU-visible buffers created on the calling thread and `Map` returning the buffer's own pointer (B2);
  `DXMT_D3D12_COMPRESSION` and `IsPrivateHeap` storage. The five view-desc unions in `C/src/abi/views.rs`. Tests:
  `resource::tests::{placed_buffer_on_heap_offset, view_descs_as_cpp (ExtractEntireResourceViewDescription),
  map_returns_buffer_pointer}`.
- [ ] **Task 12a (allocator).** From `d3d12_command_allocator.{hpp,cpp}` (CPU heap with spills, GPU heap, P15;
  `Decide`, `FoldClears`, the ICB pool, the temp-buffer ring on 5a R2's `RingBump`, `SimpleCommandContext` over 5a R3's
  trait). Tests: `allocator::tests::{decide_joins_as_cpp, fold_clears_as_cpp, reset_refused_while_recording}`.
- [ ] **Task 12b (list state and draws).** From `d3d12_command_list.cpp`'s state, root arguments, `PreDraw`, the draw and
  dispatch calls and the state setters (`OMSetBlendFactor` `:1296`, `OMSetDepthBounds` `:2114`, `ClearState` `:367`
  among them), into 7b's `List`. Vertex, index and stream-output views in `C/src/abi/list_state.rs`. Tests:
  `list::draw::tests::{recording_allocates_only_in_the_heap (a counting global allocator over a draw loop),
  get_type_is_direct, blend_factor_reaches_the_encoder}`.
- [ ] **Task 12c (list operations).** From `d3d12_command_list.cpp`'s copies, `ResourceBarrier`'s classification,
  `Barrier` through 5a D1's `LegacyBarriers`, `ExecuteIndirect`, clears, `DiscardResource` (`:1842`), `TemporalUpscale`
  for `IMTLD3D12CommandListExt`, filling `Submission`. `D3D12_RESOURCE_BARRIER`, copy locations and the render-pass
  structs in `C/src/abi/list_ops.rs`. Tests: `list::barrier::tests::barrier_classification_as_cpp`,
  `list::submission::tests::fields_as_cpp`.
- [ ] **Task 13 (queue).** From `d3d12_command_queue.cpp:44-135, 141-728, 1227-1602, 1604-2111, 2113-2158, 2480-2518`:
  the fence ring, `Order`/`PlanMerge`, clear and timestamp folding, `EncodeOrdered`, the 64-slot in-flight pool
  (`DXMT_D3D12_CMDBUF_SLOTS`), the completion thread (P20's guard), `Signal`/`Wait`, calibration over
  `dxmt_util::qpc` (`:2090-2103`), `IMTLSwapChainFactory`'s slot, `Present`/`PresentFrame` through 5a S2's `Presenter`
  and Task 15's `FrameGen` (the queue implements `framegen::Order` with its fence ring), writing Task 15's slot after
  each temporal upscale (`:2001-2007`); one command buffer per `ExecuteCommandLists`, committed at once.
  `D3D12_COMMAND_QUEUE_DESC` in `C/src/abi/queue.rs`. Tests: `queue::tests::{order_as_cpp (pure over encoder lists),
  plan_merge_as_cpp, serial_unless_overlap, timestamp_folding_as_cpp, cmdbuf_slots_clamped, execute_allocates_as_cpp (a
  counting allocator over fake lists of 1 and 64 encoders: equal allocation counts; RED: a per-encoder `Vec` of
  waits), framegen_order_joins (the `Order` impl encodes the frontier's waits and updates `fences_[done]`)}`.
- [ ] **Task 14 (dumps and capture).** From `d3d12_command_queue.cpp:730-1226` and `:2128-2157`: pass dumps, F9,
  `passes.txt`, pixel history, Metal frame capture over 5a R3's `CaptureState`, files through 5a S1's `dump`
  (`make_folder`'s loop semantics). Tests:
  `dump::tests::{passes_txt_as_cpp, f9_folder_numbering, capture_schedule_as_cpp}`.
- [ ] **Task 15 (frame generation, in the core).** `src/dxmt/src/presenter/framegen{.rs,/tests.rs}` from
  `d3d12_command_queue.cpp:82-107, 2160-2476` (P11): the slot, the three-frame ring, the interpolator from the slot's
  upscaler, the pacing thread (P20's guard) on its own queue behind the written/read events, `present` ordering the
  copy and the interpolation through the caller's `Order` (7b's signature), `AdaptFrameGen` as a pure function of
  (interval, refresh, now, pair, wait, learned), the stderr lines (P10), the `frames generated` and `presents skipped`
  counters through `stats`. Tests: Review Focus 3's, among them `adapt_replays_cpp_trace` (7a's committed
  `golden/adapt.{trace,txt}`, Task 6: every decision and stderr line equal; RED: `1.1` → `1.0`) and
  `present_orders_as_cpp` (a recording fake of the command buffer, encoders and `Order` prints the call sequence and
  every interpolator prop against `golden/present.txt`; RED: the `wait` call dropped, or jitter x and y swapped), plus
  `pacing_waits_read_before_rewrite`.
- [ ] **Task 16b (swap chain).** `swapchain` from `d3d12_swapchain.cpp` over Task 4 (`Present1`'s minimised skip and its
  16 ms sleep unless WSI hooks are set, `:790-801`; leaving fullscreen in the background minimises, `:414-423`;
  `d3d12.preferredMaxFrameRate`; `CreateMetalViewFromHWND`; 5a R3's `Hud`; 5a S2's `Presenter`; `IMPLEMENT_ME`s kept,
  `:827-868`). `DXGI_SWAP_CHAIN_DESC1` and the fullscreen desc in `C/src/abi/swapchain.rs`. Tests:
  `swapchain::tests::{minimised_skip_sleeps_without_hooks, max_frame_rate_interval_as_cpp, background_leave_minimises
  (WSI fake: not foreground and not switching output → `Leaving fullscreen in the background: minimising` and the
  minimise)}`.

### Task 17: integration (owner, lanes for fixes)

- [ ] **Step 1: Base.** Rebase `step5c` onto `main` once Z is there, dropping exactly P3's scaffold commit
  (`airconv-ir` from `main`); the key baseline becomes `main`'s post-Z key (composition 2, 4a P7), ledgered. If
  `airconv-ir`'s API moved between the scaffold's commit and Z, lanes `shd` and `pso` adapt their calls here.
  MacNeutron: rebase `step5c-mn` onto the `dxmtrs` that carries 4a Z5's pin (composition-2 `translator_key`,
  `dxmt/tools.sh`, the reordered fetch and key lines of the build scripts), so the gates run Z's `lib.sh` and
  `llvm.sh` on a Z tree and Task 21's squash applies; ledgered. In one lock job, `make llvm-nolto` once (4a Z2; a no-op
  while its `.complete` matches); from here RL and RC carry `AIRCONV_LLVM`/`AIRCONV_XCRUN` (Global Constraints). If
  `main` moved since Task 0's references, re-run Task 0 Step 4 (`$S/key` re-copied and ledgered) and Task 6 Step 1 on
  the new head (lock job), so references and gates share a base.
- [ ] **Step 2: Merge** the lanes in Execution's order; after each, `UT(d3d12 dxmt-core)`, MB, the four LBs, RL.
- [ ] **Step 3: Run the surface** (host, private copies), after the last merge and again after each fix: `kc.sh` on the
  integrated app → every row of `$S/ref-perf/check-{x64,arm64ec}.txt`; `d3d12_probe fixed` on both lanes → `.fixed`;
  natively on `LB(native)`'s dylibs: the probe → `.native`, `$S/native-tests` (as `build-tests.sh` runs them) → ok;
  Task 6's comparators against `$S/ref-corpus` → `equal` (stats in `--names-as-set` form, histograms, recordings,
  dumps, logs); `statnames.sh` on the integrated tree → `statnames: equal (269, 29)`; `unimpl-parity.sh` → `unimpl-parity:
  equal (<n> stubs, <m> E_NOTIMPL)` (its RED once: `OMSetBlendFactor` reverted to the stub in a scratch copy → FAIL);
  `crosshit.sh $S/ref-wine.app <integrated>` and the reverse (both apps' `d3d12 shader cache:` lines name one table
  first: both keyed by `$S/key`) → no misses; `replay.sh` on the integrated native dylibs → the reference's line and
  key list; `pso-compare.sh` of their `wmt-trace` (replay and `$S/native-tests`) against the reference's → `pso-compare:
  equal`; `fgpix.sh` on both Mac lanes against the reference's → `dlss framegen pixels same`; `--ported` cases checked
  against P6 and appended to the goldens. Then one lock job at the integrated head (after the last merge, and again
  after a fix that touches the guest or native path; lanes idle): `$D` detached at the head, `<M>` detached at
  `step5c-mn` and restored to `dxmtrs` by `trap … EXIT`; builder up with `VM_SHARES`; `sh eac-vm/dxmt/build.sh`, `sh
  eac-vm/image/guest-stages.sh dxmt d3d`, `sh eac-vm/builder.sh sync eac-vm/image mn-image`, `sh eac-vm/builder.sh ssh
  'sh ~/mn-image/build.sh wine'`; `dxmt-guest/.mn-source` = the head without `+dirty`; `MN_PROBE=d3d12_probe sh
  eac-vm/tests/dxgi-guest.sh i386` and `x86_64` → `dxgi-guest i386: ok`, `dxgi-guest x86_64: ok`; d3d-smoke's
  guest-mode `list7.exe` and `list7_32.exe` lines (cut as 5a Task 20's VM stats row cuts them) → `list4 ok` …
  `layout3 ok`, and `list4 ok`, `layout3 ok` for i386; `sh eac-vm/dxmt-native/build.sh`, then `MN_REF_FROZEN=$S/ref-vm
  sh eac-vm/dxmt-native/gate.sh` → `dxmt-native gate: all 31 programs passed`. So the first i686 and x86_64 Rust
  runs, the first native comparison and the first forwarded run happen here, not at the once-only gate. A failure
  goes to the lane owning the file its root cause sits in, with a failing test first.
- [ ] **Step 4:** ledger the integrated head; nothing is pushed.

### Task 18: the switch commit (owner)

**Files (DXMTRS):** delete P13's list and `src/d3d10/d3d10_blob.*`; `C/meson.build` final (no C++ sources, no
`dxbc_parser_dep`, `airconv_forward_dep` or `dxmt_dep`); delete S1's C ABI (`dxmt_stats_*`, `dxmt_dump_*`,
`src/dxmt/src/{stats,dump}/abi.rs`, their `rustfmt.toml` lines). **MacNeutron (`step5c-mn`):** `eac-vm/d3d/gen.py`
(after each call in the stub template, `if (mn_not_impl && mn_not_impl()) return mn_d3d_not_in_dxmt(id, ret);` beside
the kept `catch (...)`), the six hand-written sites in `eac-vm/d3d/host/native.cpp`, `eac-vm/d3d/host/dispatch.c`
(`mn_not_impl = dlsym(libs[2], "MTLD3DNotImplemented")`, optional: null for an older DXMT), `eac-vm/d3d/host/stubs.h`
(declares `extern bool (*mn_not_impl)(void);` in its `extern "C"` block beside `mn_d3d_not_in_dxmt`, for the
generated `stubs.cpp` and `native.cpp`; `dispatch.c` defines it), `eac-vm/d3d/tests/dispatch-test.c` (the `notimpl`
case widened: one `IMPLEMENT_ME` call per return kind, `HRESULT`, `void` and a value, through the generated stubs, with
stderr captured around them (`dup2` onto a temporary file): each call's `mn-d3d: error: <Interface>::<Method>: not in
DXMT` line once, and `E_NOTIMPL` in the `HRESULT` call's `ret`; today's case only checks that the call returns 0, which
passes either way); `dxmt/check.sh` (the `d3d12_probe` row: `fixed` against `dxmt/tests/d3d12_probe.fixed`, placed as
step 3 placed `d3d11_dxgi`'s).

- [ ] **Step 1: RED** (host, no lock). `NL=$S/lb/step5c-native-lib`: the integrated head's `LB(native)` dylibs with 5a
  Task 0 Step 6's `install_name_tool` lines applied (never the shared `install/lib`, which the last lock job left and
  this would read without the lock). With the widened case and the old `gen.py`, `make -C eac-vm/d3d dispatch-test
  NATIVE_LIB=$NL` (`BUILD_DIR` in the environment) → the `notimpl` case FAILs (no line: the flag unread, the calls
  answered as success); against `$S/ref-native`'s C++ dylibs it passes through `catch (...)` (the case's control).
- [ ] **Step 2: GREEN**: the deletions and edits; `git grep -n 'd3d12_stats\|d3d12_dxil_dump\|dxmt_stats_\|dxmt_dump_\|
  d3d10_blob' src/` → none; with the new `gen.py`, `make -C eac-vm/d3d test stubs` ok and `make -C eac-vm/d3d
  dispatch-test NATIVE_LIB=$NL` → `dispatch-test: ok`; the key unchanged; RL; the four LBs.
- [ ] **Step 3: Commits** `d3d12: the DLL is Rust; its C++ goes` (DXMTRS) and `vm: native DXMT's Rust front ends report
  unimplemented calls through MTLD3DNotImplemented` (MacNeutron).

### Task 19: the gates, once (owner)

Every row once on `step5c`'s head, as 5a Task 20 runs them (its lock-job form: `$D` detached at the head, `<M>` at
`step5c-mn` and back to `dxmtrs` by `trap … EXIT`; `MN_REF_FROZEN`, `MN_REF_WINE_APP=$S/ref-wine.app`), with `$S` for
5a's and these changes. A failing row is recorded, not fixed (Task 20). Lanes idle during timed rows. Reports
`$S/reports/gates-{mac,native,vm,rust}.md`.

- [ ] **5a Task 20's rows**, all: Mac (both check.sh lanes against `$S/ref-perf`, wine-arm64 checks, presenter, facade,
  probe, step 3's A/B, D3D9), native and VM host (the export list now the seven Rust names; `tests/build.sh`, hook,
  win32, native gate 31, wm-probe, host build and identity, `make -C eac-vm/d3d test stubs dispatch-test` (Task 18's
  widened `notimpl` case against `install/lib`), metal gate,
  loopback 31, `answers`, `wire`, `identity`, `wrapped`, `pieces`, `threads`), VM (guest builds, `check-build.sh`, VM
  DXMT gate 45 against `$S/ref-vm`, d3d-smoke both modes with both `32:` blocks and under `MN_WOW64=new`, the VM stats
  row, `dxgi-guest.sh` both widths, d3d gate with `coverage.py` unchanged, `d3d9-gate`, present-gate guest and forward
  against `$S/ref-present`), identity (P17's delta: `ident-diff.sh` with ED10 and P17's import list), p profile, Rust
  (RC with `AIRCONV_LLVM`/`AIRCONV_XCRUN`; the regenerate-equal row names `C/src/abi/vtbl.rs`,
  `src/util/src/dxgi_vtbl.rs` and `src/winemetal/sys/src/sm50/abi.rs`) and licences (airconv-ir's MIT notice in
  d3d12's legs). `stats-compare.sh` runs in 5c's `--names-as-set` form.
- [ ] **5c's rows.** `d3d12_probe` on both Mac lanes, natively (`build-tests.sh`), `dxgi-guest.sh` with
  `MN_PROBE=d3d12_probe` both widths → equal to the goldens; Task 6's comparators against `$S/ref-corpus` on the gate's
  build; `statnames.sh` → `statnames: equal (269, 29)`; `unimpl-parity.sh` → `unimpl-parity: equal (<n> stubs, <m>
  E_NOTIMPL)`; `pso-compare.sh` → equal; `fgpix.sh` on both Mac lanes → `dlss framegen pixels same`; `sh
  eac-vm/dxmt-native/tests/wmt-exports.sh build/dxmt-native/install/lib/winemetal.dylib` → `wmt-exports: ok`;
  cross-hit both ways; 3DMark replay (P18) line and key list equal; the framegen rows of Review Focus 3 in check.sh's
  log; release probe text: absent from the p VM host's `d3d12.dylib` (`check_no_traces`) and present in the std one;
  `nm` of the p `d3d12.dll`/`.dylib` has no stats symbol and no stats text; the p VM host's `d3d12.dylib` executed:
  native `d3d12_null` on the p dylibs against the std run by `compare.py`'s rule → ok (P20's guard kept under
  `perf-build`).
- [ ] **Performance** (P19's rows, both Mac lanes): pipeline creation cold and warm, `dlss cost`, `d3d12_probe full
  d12r` (std and p; also natively), the 3DMark replay's warm second run → each `<row>: <rust> vs <ref> (+x.x%)`, ≤
  +5.0%; a row above gets one control run on the reference first.

### Task 20: one fix pass (owner, lanes)

- [ ] **Step 1:** per failing row: root cause, a failing test or row first, the fix in the owning file, the task's own
  checks, the row re-run once, and every gate whose inputs the fix touched (ledgered per file). A second round goes to
  the maintainer.
- [ ] **Step 2:** commit each fix; key and RC again after the last.

### Task 21: spec, push, pin (controller)

- [ ] **Step 1: Spec** (5-D11) on `dxmtrs`: §5 step 5 records D3D12's switch (DXMTRS sha), frame generation in the core,
  the native not-implemented contract (`MTLD3DNotImplemented`, §6's MacNeutron list), the switch's wait for Z. Commit
  `Spec: DXMTRS step 5c, D3D12 in Rust`. Then ask the maintainer, before Step 2's push, whether SMITE 2's and
  Palworld's benches (P19) run now or at step 5's milestone: `sh eac-vm/tests/bench-smite2.sh <label> 120` and
  `BENCH_GAME=palworld sh eac-vm/tests/bench-smite2.sh <label>`, each once, against a named C++ baseline (the reference
  build's, or the maintainer's choice); on a yes they run as lock jobs before Step 2.
- [ ] **Step 2: DXMTRS `main`.** Restore `$D` inside the lock (pipeline rule 5); privacy-scan `main..step5c`; `git -C $D
  fetch . step5c:main`; `push --dry-run`, push; worktrees and lane branches removed (`git cherry` empty). Tell 5b's
  controller `step5w` is on `main` (P4).
- [ ] **Step 3: Pin**, one commit on `dxmtrs`: `git merge --squash step5c-mn`, `DXMT_COMMIT=<main's sha>`; `DXMT: D3D12
  is Rust (DXMTRS <sha>)`.
- [ ] **Step 4: Pinned build** (one lock job): `make wine-arm64` re-fetches; `DXMT/version` `<sha>+<series>`; `$D` clean;
  the key; `ident-diff.sh` of `d3d12.dll` against Task 19's → equal but version strings; x64 check.sh 0 FAIL.
- [ ] **Step 5: MacNeutron `main`** fast-forwarded, dry-run, push; a refused fast-forward stops for the maintainer.

## Findings for the controller

1. **The switch depends on Z** (P3): 5-D6 reads as a plain consume, but `airconv-ir` is off `main` until 4a's Z and any
   earlier landing moves the key; 5c's switch is sequenced after Z.
2. **SMITE 2's `.pipelines` recording is not covered by 4-D7** (which names SMITE 2's `traces/dxmt` capture and 3DMark's
   recording, outputs under `build/step4`). This plan uses 3DMark's recording and SMITE 2's root-signature blobs, with
   outputs under `build/step5c`; replaying SMITE 2's 124 MB `game.pipelines` needs the maintainer's yes.
3. **5-D4 under Rust:** keeping the overflow as `debug_assert!` leaves an `unsafe` write whose SAFETY line cannot be an
   invariant (P15); a one-compare check that logs and aborts as `IMPLEMENT_ME` does would cost nothing measurable.
4. **P4 reverses 5a P1's "no dead Rust ships"**: if 5c switches first, lane E (D3D11's queue and encoder) reaches `main`
   with no live caller (dead-stripped, RC-tested). The controller may prefer 5b first or a split of `step5w`.
5. **Task 7a waits for 4a's P1** (P3): the scaffold now sits under every lane, so the skeleton cannot start before
   `airconv-ir`'s P1 is committed on `step4a-translator`; Tasks 0-6 do not wait.

## Review record

Plan review of 2026-10-30 (three Opus reviewers, 5-D14); each finding verified against da6ed2d (`fc774dd` for post-R
paths), MacNeutron at `dxmtrs`, and plans 5a and 4a before it was applied.

1. Critical, `EventListener` setting events itself: applied. Task 3 wraps winemetal's listener (create, start thread,
   10 ms sleep, destroy, join; slots 104 and 146 with `needed`); the direct set only on the caller's thread (Task 8).
2. Important, abort-on-panic guard inside `scope!`: applied (with 22). P20: the guard is every slot's prologue
   (`slot!`) and every spawned thread's, kept under `perf-build`; P9's `scope!` carries none; test in both profiles.
3. Important, `HandleCount` reading `mn_win32_handle::refs`: applied. Task 1 exports `wmt_native_handle_count` and names
   `CURRENT_PROCESS`; `wmt-exports.sh` pins it; Task 8's sweep uses it.
4. Important, `is_process_foreground`/`minimize_window` missing: applied. Task 4's list, Task 16b's test, Task 0's
   ledger of every `wsi::` call before 7b.
5. Important, the CPU clock unnamed: applied. `dxmt_util::qpc()` (Task 4), used by 13 and 16a; `d12l` brackets the
   calibration's CPU value.
6. Important, `eprintln!` panics on a failed write: applied. P10 (`let _ = writeln!(…)`), audit rule in 7a.
7. Important, `close_all` with outstanding `Arc<Store>`: applied. P8, Task 3 (`Store::close` in `shader_cache.rs`,
   added to its Files), test holds an extra `Arc`.
8. Important, `SM50_IA_INPUT_ELEMENT` bitfields: applied. Task 1's `ia_input_element_packs_as_c` by value per target;
   Rules check Wire fields.
9. Important, `dxmt_util::ext`'s `extern "system"` companion slots and 64-bit-only asserts: applied, one value
   corrected. 7a's `C/src/abi/ext.rs` (`system-unwind`); i686 `MTL_TEMPORAL_UPSCALE_D3D12_DESC` is 100 bytes with
   `InputContentWidth` at 24 (clang's record layout for `i686-pc-windows-msvc`; the finding said 104), 64-bit 128/48.
10. Critical, no parity between Rust stubs and the C++'s `IMPLEMENT_ME`/`E_NOTIMPL` set: applied. P7's named macro,
    Task 6's `unimpl-parity.sh` (Tasks 17, 19), Task 0's ledger of uncalled implemented slots, the probe's `d12c`.
11. Critical, `FrameGen::present` unable to order behind the queue's fence ring: applied. 7b's `framegen::Order`
    trait in the signature; Task 13 implements it; Task 15's `present_orders_as_cpp` against a golden written from
    `:2425-2468`; the `fgpix.sh` pixel row (winshot over a new `d3d12_dlss framegen-still` mode, Task 5) as backstop.
12. Important, device creation a stub until the last lane: applied (with 27). Task 16a (creation, residency, null)
    merged first; per-lane probe families on the rebased branch.
13. Important, first guest/native Rust runs at the once-only gate: applied. Task 17 Step 3's lock job (guest stages,
    `dxgi-guest.sh` both widths, d3d-smoke's list7 lines, native gate with `MN_REF_FROZEN=$S/ref-vm`, whose 45
    programs include the native gate's 31).
14. Important, `--com` not pinning argument widths: applied. Task 2's per-slot parameter-byte asserts, i686 RED; Task
    19's regenerate-equal row names the three generated files.
15. Important, stats lines compared in tick order, labels below 30th unchecked: applied. `statnames.sh` (269, 29),
    `stats-compare.sh --names-as-set` (Tasks 6, 17, 19).
16. Important, guest goldens on an unstaged image: applied (with 29). Task 5 Step 4 is one lock job in 5a Task 20's
    form, staging and ledgering `.mn-source`, recording twice under `$S`.
17. Important, dispatch-test against the shared install and an indistinguishable `notimpl` case: applied (with 35).
    Task 18: `NATIVE_LIB=` the lane's dylibs, widened case, RED with the old `gen.py`, control on the C++ dylibs.
18. Important, P19's rows timing instrumentation, bench question after the push: applied. P19's rows, Task 19's
    Performance, Task 21 Step 1 asks before Step 2 with the named commands.
19. Important, no test of `ExecuteCommandLists`' per-encoder allocations: applied. Task 13's `execute_allocates_as_cpp`.
20. Important, `AdaptFrameGen`'s thresholds unpinned: applied. Task 6's `golden/framegen-adapt.cpp`; Task 15's
    `adapt_replays_cpp_trace`, RED `1.1` → `1.0`.
21. Important, what reaches the pipeline and depth-stencil creators unchecked: applied. Task 6's `wmt-trace.c` and
    `pso-compare.sh` (Tasks 10, 17, 19).
22. Important, the guard removed with `scope!` under `perf-build`: applied (with 2); Task 19's p `d3d12_null` row.
23. Critical, `build.rs` failing without meson's variables: applied. P8: the C++'s no-key fallback when unset, a
    failure only for a set variable naming a missing file; `AIRCONV_VERSION` from `winemetal_wire`.
24. Critical, the scaffold as 4a's whole T1 and after Task 7: applied, one addition. P3: one commit before 7a with only
    `src/airconv/ir`, the workspace entries and `Cargo.lock`; added the `cargo metadata --locked --offline` check and
    4a's single `ALLOW_SITES` line when `float_cast.rs` carries a suppression (RL's audit leg fails otherwise).
25. Critical, `win32.sh` header-only and on the shared `$D`: applied. New `wmt-exports-test.c`/`wmt-exports.sh <dylib>`
    (named for both of Task 1's exports, not only the flag); `win32-test.cpp` unchanged.
26. Important, prose vs table on Tasks 1-4's start: applied. Tasks 1, 2, 4 at S0; Task 3 after 5a Task 22 Step 6.
27. Important, every program blocked on Task 16's creation: applied (with 12). Task 16 split into 16a and 16b; merge
    order 16a, 8, 11, 9, 10, 12a, 12b, 12c, 13, 14, 15, 16b.
28. Important, lanes' `#[repr(C)]` mirrors uncovered: applied. 7a creates `C/src/abi/{objects,views,list_state,list_ops,
    queue,swapchain}.rs`, each owned by its lane, rows on the five targets; Rules check Wire fields.
29. Important, Task 5 Step 4 neither checking out nor staging, and the main-checkout rule: applied (with 16).
30. Important, the probe-text check unpassable at the skeleton: applied. 7a keeps only the p side; std-and-p moved to
    16a's check and Task 19.
31. Important, the key moving on every commit after the scaffold: applied. Global Constraints and P3: the scaffold's
    key is the baseline on `step5c`; `main`'s post-Z key after Task 17 Step 1.
32. Important, RL/RC without `AIRCONV_LLVM`/`AIRCONV_XCRUN` after Z and `step5c-mn` not on Z5's pin: applied. Global
    Constraints' forms; Task 17 Step 1 rebases `step5c-mn`, runs `make llvm-nolto` in the lock; Task 19's RC.
33. Important, LB keyed by whatever `$SRC/dxmt-translator-key` holds: applied. Task 0 Step 4 copies `$S/key`; LB passes
    it; `crosshit.sh` checks one table first.
34. Important, oversized Tasks 12, 7, 16: applied. 7a/7b (7b reviewed before lanes branch), 12a/12b/12c with the
    list's seams in 7b, 16a/16b.
35. Important, `d12n` never through the generated stubs: applied (with 17). Task 18's widened dispatch-test case per
    return kind; `mn_not_impl` declared in `host/stubs.h`, defined in `dispatch.c`.

No finding rejected.
