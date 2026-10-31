# DXMTRS step 3: d3d10core and dxgi in Rust Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `d3d10core.dll` and `dxgi.dll` (and their native dylibs) are whole Rust DLLs, every answer pinned first by a
COM-level probe whose goldens were recorded from the C++ after two small C++ cuts.

**Architecture:** Two C++ cuts come first (S0): the native `dxgi.dylib` exports only its five entry points, and the
d3d11 swap chain reads the output's gamma ramp through a new private COM interface, `IMTLDXGIOutput`, instead of a
`static_cast` to a C++ class. A probe (`d3d11_dxgi`, MacNeutron) then drives every slot of the factory, adapter and
output vtables, the five dxgi exports and the two d3d10core exports, in-process and in child processes with their own
environment, and prints machine-independent lines (relations where a machine or lane differs) plus the DLLs' log files.
Its goldens come from the S0 C++ on both Mac lanes, natively (fake displays through the WSI hooks) and on i386 in the
VM. Two new staticlib crates of the root Cargo workspace replace the C++: `d3d10core` at `src/d3d10` (S3), then `dxgi`
at `src/dxgi` (S4), whose lanes own disjoint module files. Every gate runs once at the end of the step.

**Tech Stack:** Rust 1.99.0 (staticlibs for `i686`, `x86_64`, `aarch64`, `arm64ec-pc-windows-msvc` and
`aarch64-apple-darwin`), meson, llvm-mingw, C++17 (the probe), winemetal's C API, Wine's user32/gdi32/advapi32.

**Spec:** `<M>/docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§3, §5 step 3, §6, §7, §8). Inputs:
`build/step3/map/0-porting-map.md` (synthesis), `1-dxgi.md` .. `5-tests.md` (readers), `6-critic.md` (A1-A10, B1-B13,
all accepted), `7-decisions.md` (D1-D24, binding). Format: step 2's plan
(`2026-10-29-dxmtrs-step2-winemetal.md`) and step 1b's (`2026-10-29-dxmtrs-step1b-nvapi.md`). Layout and gate: step R's
(`2026-10-30-dxmtrs-stepR-layout.md`, landed as DXMTRS `main` 1ccec40, MacNeutron pin ce1a56f): every component under
`src/<component>/`, the crate that becomes its PE binary with its `Cargo.toml` at the component root (spec §3, layout
rule 2), one Cargo workspace at the root (`Cargo.toml`, `Cargo.lock`, `.cargo/config.toml`, `rustfmt.toml`), the Rust
gate `tools/rust-check.sh`, the audit `tools/audit.sh`, the conventions `docs/CONVENTIONS.md`.

Paths: `<M>` = MacNeutron's main checkout (absolute), `$D` = `<M>/build/wine-arm64-src/dxmt` (DXMTRS: the pipeline's
checkout, checked out and built only inside lock jobs, never edited), `$S` = `<M>/build/step3`, `$R` =
`~/.rustup/toolchains`. DXMTRS paths (`src/…`, `tools/…`, `docs/…`) are relative to a DXMTRS tree: a lane's worktree
`$S/wt-<lane>`, or the integration worktree `$S/wt-s0` (branch `step3-dxgi` from Task 4R on). Commands run from `<M>`
unless a step says otherwise. Ledger: `.superpowers/sdd/2026-10-30-dxmtrs-step3-d3d10core-dxgi/progress.md`;
pipeline rules: `<M>/build/pipeline.md`.

**Status (2026-10-30).** Tasks 1-4 ran early, beside step R, on DXMTRS `step3-s0` (worktree `$S/wt-s0`, off da6ed2d)
and MacNeutron `step3-probe` (worktree `$S/wt-mn-probe`, off `dxmtrs` f1680f7); their results and rulings are in the
ledger. Waiting for R: the five lane-kit scripts tied to the build layout (Task 4 Step 2), the two full check.sh
reference runs, a present-gate run on S0 and a quiet-machine rate. R has landed; this plan was passed onto its layout by
step R's Task 11 Step 6. **The remaining work starts at Task 4R**: both branches rebased onto R, then the kit.

## Decisions

Binding (`7-decisions.md`), short form:

- **D1** Step 3 is `d3d10core.dll`, then `dxgi.dll`, whole Rust DLLs and dylibs. The 17 `MTLD3D10*` classes (inside
  `d3d11.dll`) move with d3d11 at step 5; `d3d10_blob.cpp` with d3d12. Spec §5 corrected at the end.
- **D2** S0a: `dxgi.dylib` exports its five names; `d3d10core` links `[ util_dep, d3d11_dep ]`. Per-dylib native logs
  and Config accepted; the `macneutron-vm.swift:207` comment and the research README updated.
- **D3** S0b: `IMTLDXGIOutput : IDXGIOutput6 { const dxmt::DXMTGammaRamp *GetGammaRamp(); }` (forward-declared ramp);
  `dxgi_output.hpp` goes; d3d11's swap chain QIs once where `target_` is set. Checked by a D3D11 fullscreen-present
  case in `d3d12_fullscreen`, run alone.
- **D4** No C++ cut for `GetMTLDevice`: slot 19 is `extern "system" fn(*mut c_void) -> u64`. Call-rate lines for
  `GetMTLDevice`, `GetDesc1`, `QueryVideoMemoryInfo`, C++ against Rust, within 5%.
- **D5** A private Rust config reader (map 0 D12 + critic B3) with a drift test against `config.cpp`; S0 sorts the
  matched profile's info lines by key in the C++, and the Rust sorts the same way.
- **D6** COM objects: `#[repr(C)]`, vtable first, one `AtomicU32` from 1, `Box` freed on 1→0, one vtable per class,
  same `this` for every IID, QI sets exactly the C++'s plus `IMTLDXGIOutput`. Private data in a `Mutex` with
  `ComPrivateData`'s codes; app AddRef/Release outside the lock; replace = AddRef new, then Release old; a null
  interface is stored; a null `pData` returns the size. As `com_private_data.cpp` (verified for this plan): a stored
  interface is Released on replace (:47, from :140-146), on erase by `SetPrivateData(guid, n, NULL)` (:102) and when
  the owning object dies (:33); `GetPrivateData` of an interface entry AddRefs it before handing it out (:80);
  `*pDataSize` = the entry's size on every path with a non-null `pData`, `DXGI_ERROR_MORE_DATA` included (:71, :87).
- **D7** Every observable C++ quirk stays (map 0 "Behaviour", critic B9, B10). Crashes and UB get step 1b's defined
  results; a zero mode denominator is skipped by the filter and sorts last. Each is a `NOT_ON_C` probe case. The full
  list of defined results is P20.
- **D8** Hand-written `abi.rs` (CamelCase types, snake_case fields, no `allow`), asserted per target against a
  throwaway C layout program built on llvm-mingw's sysroot (PE) and `include/native` (native). No bindgen.
- **D9** Crates at the component roots (spec §3, layout rule 2: Rust's `src/` beside the C++ while both exist):
  directory `src/d3d10`, package `d3d10core`; directory `src/dxgi`, package `dxgi` (meson's `cargo_staticlib` and every
  `-p` take the package name). Members of the root workspace, not `default-members` (as `nvngx`, `nvapi`); staticlib,
  `license.workspace = true` (LGPL-2.1-or-later), no third-party dependency; dxgi on `dxmt-util` (`src/util`) and
  `winemetal-wire` (`src/winemetal/wire`). Each has `perf-build` forwarding to `dxmt-util/perf-build`, set by meson when
  `-DDXMT_PERF_BUILD` is in `cpp_args`.
- **D10** dxmt-util: `Guid` `Clone, Copy, PartialEq, Eq`, an unaligned reader, `Display` in `com_guid.cpp`'s format;
  `Logger::{info, debug, trace}` and `perf-build`; natively an empty exe name; exe and cache-exe names with
  `getExePath`'s truncation; `luid_from_registry_id`, adopted by nvapi in the same commit. Host tests pass
  `--target aarch64-apple-darwin`.
- **D11** `.def`s verbatim, exports as code, both `version.rc`s, `winebuild --builtin` for guest DLLs. i686: project
  `--kill-at`; `llvm-readobj` checks names and the i386 imports (d3d10core's `D3D11.DLL: D3D11CoreCreateDevice`,
  dxgi's `ntdll.dll: _aulldiv`; spec :162 stays open until that row passes). Win libs from meson's defaults.
- **D12** Native: the same crates for `aarch64-apple-darwin`, `cfg(not(windows))`, an explicit export list per dylib
  (`nm -gU` = exactly the names, no Rust symbol bound across dylibs). If the native half stalls, `if not
  dxmt_crossbuild` keeps the C++ natively and the PE switch ships alone.
- **D13** `panic = "abort"`; `IMTLSwapChainFactory::CreateSwapChain` is `extern "system-unwind"`. `DllMain`:
  `DisableThreadLibraryCalls`, the cache path, then the NVEXT `Once` (all three HKLM keys or NVEXT off, no log;
  `RegSetValueExW` results ignored; i386 skips). Nothing blocking on that path; Config a `LazyLock` first touched at
  `EnumAdapters1`.
- **D14** Statics, each with its CONVENTIONS argument: `LOG`, the NVEXT `Once` and flag, the adapter-removal flag,
  the QI-log set, Config. `LazyLock` or `Once` only.
- **D15** ARM64EC: LLVM's entry thunks (x64 calls Rust slots) and exit thunks (Rust calls app objects); the x64 lane's
  probe calls every slot.
- **D16** Probe `dxmt/tests/d3d11_dxgi.cpp`, families `dxgif`, `dxgia`, `dxgio`, `d3d10c`, `dxgil`; `fixed`/`full`;
  map 0 §4.1's cases plus critic B2-B4, B9, B10, B12; the i386 probe calls every slot. Goldens from the S0 C++:
  `.fixed` (Mac lanes), `.native` (fake displays), `.i386` (VM, frozen); this plan adds `.guest64` (the guest's
  x86_64 code, P4). `d3d10_core`, `d3d10_wine` rows on both Mac lanes. One mutation RED per new comparator.
- **D17** A `d3d10c` case with `D3D10_CREATE_DEVICE_BGRA_SUPPORT` only; no d2d1 row.
- **D18** Per task only its own checks; every gate of map 0 §4.2 once at the end, one pipeline owner.
- **D19** `tools/audit.sh`'s `APP_POINTER_DIR` covers the two crates (`src/d3d9/src src/dxgi/src src/d3d10/src`) and its
  `PE_CRATES` names them; the release-hygiene rules read them once they are workspace members (`CRATES`, from the root
  `Cargo.toml`); `sh tools/audit.sh` from the tree root prints `audit: clean (N files)`.
- **D20** Tooling (maintainer 2026-10-30: "You can download the tools you need"). Installed: rustfmt and clippy for
  1.99.0 and the pinned nightly with rustfmt (`MTLD3D_RUST_NIGHTLY=nightly-2026-10-30` in MacNeutron's `dxmt/pins`;
  `rustfmt.toml` has nightly-only options). Since step R both are legs of `tools/rust-check.sh` (R-D12): fmt is the
  pinned nightly's `cargo fmt --all --check` over the one workspace; clippy is 1.99.0 `--keep-going … -- -D warnings`
  on every PE target a crate ships on (arm64ec, aarch64, x86_64 msvc; i686 for `dxmt-util`, `winemetal-pe`, `dxgi`,
  `d3d10core` and D3D9's; nvngx and nvapi are 64-bit only) and darwin. Task 5 adds `dxgi` and `d3d10core` to its
  package lists, and `dxmt-util` to its i686 list (it ships on i686 from then on). No bindgen (D8).
- **D24** S2f, done by step R: its format commit (R-D13, one mechanical nightly-rustfmt commit over the one workspace;
  `rustfmt.toml`'s `ignore` keeps the generated `src/winemetal/wire/src/layouts.rs` and `src/winemetal/src/protos.rs`
  as `tools/winemetal-layouts.sh` writes them) and its clippy fixes (nvngx's `not_unsafe_ptr_arg_deref` among them,
  step R ledger Task 2). This plan has no format task: fmt and clippy are RC's legs in every task's checks and Task 13.
- **D21** No edit outside `src/dxgi`, `src/d3d10`, `src/d3d11/d3d11_swapchain.cpp`, `src/util/config/config.cpp`,
  `src/util` (`dxmt-util`: `Cargo.toml`, `src/`), `src/nvapi/src/gpu.rs`, `tools/`, `docs/CONVENTIONS.md` and the root
  `Cargo.toml` (members) and `Cargo.lock` (step R's homes for what this list called the Rust tree); key
  `71a7e5f22c50…3311` read before and after.
- **D22** No game bench; D4's call-rate lines and `present_loop`'s frame time, C++ against Rust, within 5%, once.
- **D23** Licences in the pin-bump commit: the README's DXMT entry, `licences_test.sh`, VM parts, app README.

Rulings this plan adds (ledgered; Task 15 carries the spec-changing ones):

- **P1 Lanes check on the host; the PE probe once before the gates.** A lane runs unit tests, the msvc builds,
  clippy, doc, fmt, audit, PE DLLs linked by `$S/lane-dll.sh` (export/import tables, i686 `ret $N`), and the native
  probe on dylibs linked by `$S/lane-dylib.sh`. The PE probe first runs on the Rust DLLs at the S4 merge (Task 12
  Step 4, pipeline owner), on both Mac lanes, in a copy of `$S/ref-wine.app` with the Rust DLLs swapped in and the
  copy re-signed (`$S/lane-app.sh`) — why: `macneutron install`'s `codesign --verify --strict`
  (`RuntimeInstaller.swift:119`) checks validity only, so a copy re-signed with `MACNEUTRON_SIGN_IDENTITY` and the
  original's identifier, entitlements, requirements, flags and runtime passes, and `check.sh`/`$S/run.sh` take any
  `wine.app` (`MACNEUTRON_ARM64_APP`, `check.sh:17`, `:47`); so Win32 WSI, registry, D3DKMT, CRLF logs, profiles and
  ARM64EC thunks run before the gates — cost: one re-signed copy; i686 and the guest x86_64 still run first at Task
  13. If `lane-app.sh`'s control fails (Task 4R), the PE probe first runs at Task 13, ledgered (the fix pass
  absorbs it). `make wine-arm64` is a shared build the owner runs inside the pipeline lock (Tasks 2, 4, 4R, 13, 15),
  not a gate.
- **P2 One object chain, disjoint files.** Task 7 lays down every dxgi module with its final signatures and stub
  bodies; Tasks 8-11 each own whole files. The native probe of all dxgi families runs at the S4 merge (Task 12) —
  why: factory, adapter and output are one chain; no family can be probed without the other two — cost: per-lane
  feedback is unit tests, which reach the vtables where no winemetal or Win32 call is on the path (Task 8's factory
  tests, Task 10's `cfg(test)` output); `EnumAdapters1`, the adapter and the Win32 WSI first run at Task 12 (natively,
  then PE through `lane-app.sh`), ledgered as P1's cost.
- **P3 Native golden checked by `build-tests.sh`.** `d3d11_dxgi` joins `eac-vm/dxmt-native/build-tests.sh`'s
  programs (`WMTSetWsiHooks` and `MTLDevice_registryID` through `dlsym`: no new link) and its check compares with
  `d3d11_dxgi.native`;
  it is not a `gate.sh`/`loopback-gate.sh` row — why: fake displays, UTF-32, `_dxgi.log` and D3DKMT stub lines differ
  from macOS by design; `gate.sh` runs `build-tests.sh` first, so the native gate still runs it — cost: the loopback
  doesn't run the probe (the forwarder's support tests cover forwarding).
- **P4 Guest runner.** `eac-vm/tests/dxgi-guest.sh <i386|x86_64> [record]` compiles the probe for i686 (as
  `d3d-smoke.sh` compiles `smoke32.exe`) or x86_64, runs it in guest mode (i386: old WoW64, the image default) and
  compares with `dxmt/tests/d3d11_dxgi.i386` or `d3d11_dxgi.guest64`, printing `dxgi-guest <arch>: ok` — why: no macOS
  i386 lane; the Mac DLLs are ARM64X (`meson.build:224-225`: x64 programs run their ARM64EC code), so
  `x86_64-pc-windows-msvc` (`:226-227`) runs only in the guest, and families the guest invocation drops (Task 4 Step
  5) and every `NOT_ON_C` case would otherwise be unpinned there; d3d-smoke's two modes are too heavy to re-run for
  recording — cost: one script, one golden.
- **P5 `fixed` prints relations where machines or lanes differ** (map 5 §4's mask table: Description, LUID, VRAM,
  usage, outputs, rects, modes, HDR, D3DKMT, refresh), exact values elsewhere; raw values go to `full` (each mode's
  `RefreshRate` as raw `num/den`) — why: one `.fixed` for both Mac lanes, a `.fixed` that survives a monitor change,
  and an x86_64 guest row comparable with macOS — cost: the raw values are pinned only by same-machine C++/Rust `full`
  diffs. Two exceptions pin what a relation would hide: each mode list prints its refresh representation (`den=1000`
  on PE, where `wsi_monitor_win32.cpp:87` gives `{dmDisplayFrequency * 1000, 1000}`; `den=hook` natively), and in
  `fixed` mode `dxgil` replaces the numbers of `FindClosestMatchingMode1`'s debug line (`dxgi_output.cpp:515-526`)
  with relation words (`DXGI: For mode <target> found closest mode <result>`), `full` keeping the raw line.
- **P6 `NOT_ON_C` cases run only with `--ported`.** The C++ goldens never hold them (C++ results are crashes or
  garbage). Their Rust lines are first checked against P20 on the lanes: `lane-dylib.sh … --ported` (Task 6 Step 2,
  Task 12 Step 1) compares the other lines with `.native` and prints the `NOT_ON_C` lines (named by `--list`'s flags)
  for that check, and Task 12 Step 4 does the same on the Mac lanes. Task 14 records them from the gates' builds,
  appends them, and every comparator switches to `fixed --ported`.
- **P7 The gamma row's oracle is `winshot`** (`wine-arm64/tools/winshot.c`): two phases while fullscreen, an identity
  ramp (green share ≥ 56, presents reach the screen) then an all-1.0 ramp (white ≥ 56, green < 5) — why: nothing in
  the presenter counts or logs its gamma pipeline, and D21 forbids presenter edits — cost: Screen Recording
  permission (as `wine-arm64/check.sh dxmt-present`); a locked screen fails the row, as it fails present-gate's
  `winshot`. If `winshot` can't see the fullscreen window on the unmodified C++ (Task 2 Step 2), the row grades the
  Present results only and the presenter path is ledgered as untested (critic D1's alternative).
- **P8** `IMTLDXGIOutput`'s IID is `4c1128cd-b437-4d44-9958-5d765bb85a76`.
- **P9 Native link.** Each Rust dylib passes `-Wl,-u,_<name>` and `-Wl,-exported_symbol,_<name>` for every export,
  plus `-Wl,-S` unless debug (D12's fallback made the rule) — why: no object references the Rust exports — cost: none.
- **P10 `DllMain`** acts on `DLL_PROCESS_ATTACH` only (`dxgi.cpp:64`). `DisableThreadLibraryCalls` stays for parity;
  with Rust's TLS it is refused, so `DllMain` and std's TLS callback run per thread (winemetal.dll and nvapi64.dll
  already do; step 2 ledger) — cost: one branch per thread start.
- **P11** A `Release` past zero answers 0 and frees nothing (d3d9 factory's `checked_sub`); a unit test, not a probe
  case (the C++ use-after-frees).
- **P12** d3d10core states its own two IIDs and two partial vtables (nvngx's way) — why: a staticlib crate can't be
  a dependency without its exports — cost: a duplicate of `src/dxgi/src/abi.rs`, listed for §7.
- **P13 i686 callee-pop check.** `$S/i686-ret.sh` compares the `ret $N` of every Rust vtable function and export in the
  i686 staticlib with the C++'s objects as Task 4 Step 1 snapshotted them (`$S/ref-build/i686/`, from
  `dxmt-guest-build32/src/<dir>/<dll>.dll.p/*.obj`; `eac-vm/dxmt/build.sh:55-57, :84-86` delete both guest build dirs
  whenever DXMTRS's HEAD changes) — why: layout asserts can't see a wrong argument width (`UINT64`, by-value `LUID`,
  critic B6) — cost: Rust thunks named `<class>_<method_snake>`.
- **P14 Tooling (D20, D24).** The tools are installed under D20's yes; nothing further is downloaded. Step R left the
  tree formatted and clippy-clean, so from Task 5 on every task's Rust checks and Task 13 run RC (Global Constraints)
  over the whole workspace and every shipped target.
- **P15 Import tables.** `dxgi.dll` and `d3d10core.dll`'s own imports: DLL set ⊆ the S0 reference's ∪ {ntdll,
  api-ms-win-core-synch-l1-2-0, api-ms-win-crt-private-l1-1-0}, std's symbols added (step 2's ruling, pre-applied),
  fewer USER32/GDI32 symbols allowed, no DXMT DLL import added. The importers' tables (d3d11, d3d12, nvapi64, nvngx;
  guest both widths) equal the S0 reference symbol for symbol.
- **P16** Call-rate on the ARM64EC lane (d3d11/d3d12's callers are ARM64EC code) and natively; over 5% gets one C++
  control in the same session before it counts.
- **P17 References from S0**, not da6ed2d: `$S/ref-*` are saved after Tasks 1-2, before any step-3 Rust lands on
  `step3-dxgi` (done: Task 4 saved them from 4fb43ea; Task 5 is fast-forwarded only after Task 4R). They stay the
  references after step R: R changed none of DXMT's C++ (`git diff da6ed2d 1ccec40` is empty under `src/dxgi`,
  `src/d3d10`, `src/d3d11`, `src/d3d12`, `src/dxmt`; under `src/util` it only adds the moved `dxmt-util` crate's
  `Cargo.toml` and `src/`) and every shipped binary's identity table matched the one saved before it (step R ledger,
  Task 10 Step 3); Task 4R checks the rebased S0's key and C++ diff, and its
  `lane-dll.sh` control replays a post-R link line on `ref-build`'s inputs. Every VM command from Task 4 on
  (`ref.sh`, `gate.sh`, `dxgi-guest.sh`, `present-gate.sh`, the gates) runs with
  `MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img` (absolute; `run-guest.sh:55` otherwise uses
  `state/default.img`, 100% full per step 2's Task 1 ruling).
- **P18 Probe children.** Each child's environment is the parent's minus every `DXMT_*` variable, plus its case's;
  `DXMT_LOG_LEVEL=debug`; `DXMT_LOG_PATH`, `DXMT_SHADER_CACHE_PATH` and the working folder its own folder under
  `%TEMP%\d3d11_dxgi\<child>` (natively `$TMPDIR`; the `d3d10c` children's D3D11 devices never touch a shared cache), where profile children run copies of the probe named `OMORI.exe` and `GenshinImpact.exe`.
  The probe deletes each child's folder before spawning it (a log file exists only once a line passes the level,
  `log.cpp` `emitMsg`, so a stale one would read as current), and `dxgil` prints `dxgil <child> none` for an absent
  log file.
- **P19** The dxgi crate never calls `dxmt_util::nvext::enabled()` (it would import its own export); Task 12 checks
  `llvm-nm -u` of each dxgi staticlib names no `DXGIGetDebugInterface1`.
- **P20 Defined results (D7), each an `ISOLATE|NOT_ON_C` case:** `D3D10CoreCreateDevice` null adapter → `80070057`;
  adapter slot 12 → `80004001` + ERR `Not implemented`; slot 16 null cookie → `80070057`; `CheckFeatureSupport` null
  data → `80070057`; the native `zero-den` mode sorted last and skipped by the filter; `EnumAdapters1(i < count,
  NULL)` (the C++ writes through null at `dxgi_factory.cpp:199`) → `DXGI_ERROR_INVALID_CALL` as `EnumAdapters`
  answers (:165-166), no adapter created, factory count unchanged; a null `riid`/`REFGUID` (every C++ path
  dereferences it) → `E_POINTER` for QI-shaped calls (`QueryInterface`, `GetParent`, `EnumAdapterByLuid`,
  `EnumWarpAdapter`, `EnumAdapterByGpuPreference`, `CreateDXGIFactory*`, `DXGIGetDebugInterface1`), the out pointer
  nulled where that call nulls it on its other failures (never by `EnumAdapterByGpuPreference` or
  `DXGIGetDebugInterface1`), and `E_INVALIDARG` for `Get/SetPrivateData`, `SetPrivateDataInterface` and
  `CheckInterfaceSupport`. Cases: Task 3; checked: Tasks 6, 12 (`--ported`) and 14.
- **P21 `--bites`.** The probe's `--bites` calls each vtable slot through a copy of the object's vtable with that slot
  stubbed (`E_NOTIMPL`, no write, no log; void slots return) and requires a changed `fixed` line; an exclusion list
  states each reason (adapter slots 13 and 17 are void no-ops; any slot whose stubbed answer prints as its real one).
  Mac lanes only (a generic stub can't pop i386 stdcall arguments). Run once on the C++ at Task 4 Step 6 — why:
  `--cover` proves a case exists, not that it notices the slot; step 2 had `cover.sh bites` — cost: one mode.
- **P22 MacNeutron's step-3 commits stay on `step3-probe`** (worktree `$S/wt-mn-probe`, rebased onto `dxmtrs` at
  Task 4R) until Task 15 lands them with the pin as one `dxmtrs` commit (`git merge --squash`, step R's P13) — why:
  `dxmtrs` is MacNeutron's `main` line (step R pushed it), and its new check.sh, native and guest rows need step 3's
  DXMTRS (S0's `IMTLDXGIOutput`, then the Rust); owner jobs run MacNeutron's scripts from `$S/wt-mn-probe` with
  `BUILD_DIR` exported, VM steps through the main checkout's `eac-vm/builder.sh` (Global Constraints) — cost: the
  probe's eleven commits become one on `dxmtrs`.

## Global Constraints

- Agents never push, merge to `main`, fetch or download (D20's tools are installed; anything else needs the
  maintainer). Task 15's push, pin and pushed-pin build (with its `git ls-remote`) run under the
  maintainer's standing authorization, by the controller only. Kill by exact PID; no MacNeutron.app, Steam or game;
  never print SteamIDs or account names; privacy-scan before pushes. Benches once. Home written `~`. Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- DXMTRS: branch `step3-dxgi` = `step3-s0` rebased onto `main` 1ccec40 (Task 4R Step 1), checked out in the
  integration worktree `$S/wt-s0`; lanes branch from it. MacNeutron: branch `dxmtrs` (= `main` ce1a56f, step R's pin)
  and `step3-probe` rebased onto it (P22).
- **The pipeline lock** (`<M>/build/pipeline.md`, binding): every command that builds or installs MacNeutron's runtime
  or a DXMT build (`make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/dxmt-native/build.sh`, `eac-vm/host/build.sh`,
  guest stages, image builds), runs a gate or check that reads installed outputs (`dxmt/check.sh`, `presenter/check.sh`,
  `d3d9-check`, conformance, the VM and native gates, `build-tests.sh`, `tests/build.sh`, `check-build.sh`), or checks
  out anything in `$D`, runs as ONE job: `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c '<checkout + build +
  tests>'`. A job never assumes `$D`'s state: it first runs `git -C $D checkout -q --detach <the commit it needs>`
  (a detached checkout of `step3-dxgi`'s head is allowed while `$S/wt-s0` holds the branch), builds before it tests,
  records the commit and wine.app's cdhash in its log, and holds the lock no longer than its commands (no review inside
  it). Never edit files in `$D` or in `<M>` (rule 2; the controller's merge, pin, spec and ledger commits on
  `dxmtrs` in Task 15 excepted, as in step R's Task 11); never leave a branch, stash or worktree a job made in `$D`.
  Host-only work
  (cargo in a `CARGO_TARGET_DIR` of one's own, the audit in a worktree, reading files, the probe on a copy of a
  runtime such as `$S/ref-wine.app` or a `lane-app.sh` copy, Task 3's ruling) takes no lock. Rule 7: MacNeutron's
  build `rm -rf`s `$D` when it sees a clean one-branch checkout with a moved pin ("reapply"), so step branches and
  worktrees are never deleted from `$D` to make a build pinned, and a pushed pin is verified by a development build at
  the pushed commit plus `git ls-remote` (Task 15). Before any push or pin the controller restores `$D` with `git -C $D
  checkout --detach $(cat <M>/build/wine-arm64-src/dxmt.applied)` (rule 5).
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE` exported. The builder starts with `VM_SHARES` per the builder-out-share note. VM
  steps use the main checkout's `eac-vm/builder.sh` (its ssh key lives under `<M>/build`; a worktree's copy finds no key,
  Task 4's ruling), and `presenter/check.sh` runs from `<M>` (it reads `$ROOT/build/wine-arm64/wine.app`, not
  `BUILD_DIR`). The DXMT tree counts as development (more than one branch, a worktree, a stash, or HEAD ≠
  `dxmt.applied`), so in-step builds build the checked-out tree.
- Every `make` or `dxmt/*.sh` run in a MacNeutron worktree sets `BUILD_DIR=<M>/build` in the ENVIRONMENT
  (`BUILD_DIR=<M>/build make …`), never as a make argument (`make BUILD_DIR=…` does not reach `$(shell sh
  dxmt/toolchain.sh)`, pipeline.md rule 6; the ledger's Fix ruling): `Makefile:5` runs the worktree's
  `dxmt/toolchain.sh`, whose `llvm-mingw.sh` would otherwise look for `llvm-mingw-own` under the worktree, fetch
  llvm-mingw and build LLVM (`llvm-mingw.sh:17-22, :35-41`). Make's own targets stay relative to the worktree
  (`build/dxmt-tests/…` lands in it; `mkdir -p` first).
- Rust (one workspace, R-D2): from the DXMTRS worktree's root (`$S/wt-<lane>`, or `$S/wt-s0` for the controller and
  owner), `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH`, `CARGO_TARGET_DIR=$S/cargo-<lane>` (absolute, outside every
  tree, R-D7), always `--offline`, every command naming its package (`-p`) and target:
  - host tests: `cargo test --offline --target aarch64-apple-darwin -p <crate> --config
    'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'` (the exports keep
    winemetal and `D3D11CoreCreateDevice` references alive; never a committed config). Host tests reach pure functions
    and the COM plumbing, nothing that calls winemetal, Win32 or d3d11 (step 1b's rule);
  - msvc: `cargo build --offline -p <crate> --target <t> --config "target.<t>.rustflags=[\"-Dwarnings\"]"` for
    `<t>` in `aarch64-`, `arm64ec-`, `x86_64-`, `i686-pc-windows-msvc` (the staticlibs; RC's clippy compiles no code);
  - perf-build (D9): `cargo build --offline -p dxgi -p d3d10core --features dxgi/perf-build,d3d10core/perf-build
    --target <t> --config "target.<t>.rustflags=[\"-Dwarnings\"]"` for `i686-pc-windows-msvc`,
    `arm64ec-pc-windows-msvc` and `aarch64-apple-darwin`, by every task that edits either crate and at Task 13
    (`tools/rust-check.sh` has no leg for these features);
  - **RC**, the Rust gate (step R's `tools/rust-check.sh`: legs fmt with the pinned nightly, clippy on every shipped
    target and darwin, doc, test, audit; `tools/audit.sh` reads tracked files only, so the form stages the worktree
    first), in the lane form, from `<M>` (on `dxmtrs`, whose `dxmt/pins` names `MTLD3D_RUST_NIGHTLY`):
    `sh -c 'ROOT=$PWD; . dxmt/pins; LM=$(dirname "$(sh dxmt/toolchain.sh)"); git -C build/step3/wt-<lane> add -A;
    PATH="$(sh dxmt/mtld3d-tools.sh):$LM/bin:$PATH" MTLD3D_RUST=$MTLD3D_RUST MTLD3D_RUST_NIGHTLY=$MTLD3D_RUST_NIGHTLY
    MTLD3D_LLVM_MINGW=$LM WINE_BUILD=$PWD/build/wine-arm64-src/wine-build
    GUEST_WINE_SDK=$PWD/build/eac-vm-work/guest-wine-sdk CARGO_TARGET_DIR=$PWD/build/step3/cargo-<lane>/check sh
    build/step3/wt-<lane>/tools/rust-check.sh'` → every leg ok, `audit: clean (<n> files)` (`<n>` = `git ls-files
    '*.rs'`: 688 at 1ccec40, grown by the step's new `.rs` files), `rust-check: ok`, exit 0. Its package lists name
    `dxgi` and `d3d10core` from Task 5 on (D20);
  - anything running `tools/cargo-staticlib.sh` (the kit, meson): `PATH=<M>/build/mtld3d-tools/bin:$PATH` first (its
    `cargo "+<toolchain>"` needs rustup's proxy; `lld-link` and `llvm-lib` are there too), its fourth argument an
    absolute `$S/cargo-<lane>/…`.
- CONVENTIONS (`docs/CONVENTIONS.md`, tree-wide since step R): no `#[allow]`/`#[expect]`; `unsafe` one operation a
  block with `SAFETY:`; no `pub(crate)`; tests in `<stem>/tests.rs`; derive inventory current (`tools/audit.sh
  --update-derives`); `foo.rs` + `foo/`, no `mod.rs`.
- An application's pointer promises no alignment (D19): app structs, out pointers and `riid` through
  `read_unaligned`/`write_unaligned` (or `Guid::read`); a null check wherever the C++ has one, and P20's where it had
  none. Production builds check no alignment and arm64/x86 load misaligned scalars in hardware, so on PE the audit's
  grep is the only net; `lane-dylib.sh` builds with debug assertions so a misaligned access aborts natively.
- No `unwrap`, `expect`, indexing or overflowing arithmetic on application values; every vtable function a safe
  `extern "system" fn` named `<class>_<method_snake>` (P13), one `static` vtable per class.
- Behaviour is the C++'s: HRESULTs, bytes written and left, object identity, reference counts, log lines (text,
  level, CRLF on PE), except D7's defined results.
- Seams that do not move: `dxgi.def` (`LIBRARY DXGI.DLL`, ordinals 9, 10, 11, 16, 17), `d3d10core.def`; DLL and dylib
  names and install names; `IMTLDXGIAdapter` slots 19 (`-> u64`) and 20 (`-> u32`); `IMTLDXGIOutput` slot 29;
  `IMTLSwapChainFactory` slot 3; `DXMTGammaRamp`'s 12296 bytes with `version` at 12288; `dxgi_interfaces.h`,
  `dxgi_object.hpp`, `dxgi_resource.hpp` for d3d11/d3d12; `wsi_window_win32.cpp`, `wsi_monitor_win32.cpp` (the
  forwarder copies them); the `0x80000000` flag; `VERSIONINFO` 10.0.17763.1; exports as code.
- Translator key: read with `sh -c 'ROOT=$PWD; . dxmt/pins; . wine-arm64/lib.sh; PATH="$(sh dxmt/toolchain.sh):$PATH";
  translator_key build/wine-arm64-src/dxmt release'` = `71a7e5f22c50f03e79c56344f7a11819f2f922e67fe9ebe4f84ed9761d763311`.
- zsh: quote `"====="`; unquoted `$var` is not split. Gating scans use `/usr/bin/grep`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes and keeps the ledger (rulings, flaky re-runs,
deferred Minors, golden edits). One **pipeline owner** at a time runs anything that uses the shared build, the VM or
a quiet machine: `make wine-arm64`, `eac-vm/*` builds and gates, `dxmt/check.sh`, the probe on a runtime, call-rate
runs. **Lanes** use host tools only (P1).

**Lane procedure.**
1. `git -C $D worktree add $S/wt-<lane> -b step3-<lane> step3-dxgi`. Fill the worktree's `include/native/directx` and
   `external/nvapi` read-only from the main checkout's submodules (`git -C $D/<path> archive HEAD | tar -x -C
   $S/wt-<lane>/<path>`, then `chmod -R a-w`); never committed. `chmod -R u+w` them before `git worktree remove`.
2. RED: the task's failing tests (and, where the task says, the stubbed crate's native probe differing).
3. GREEN: the port; the task's checks.
4. Host checks in the Global Constraints' forms (the task's host tests, msvc and perf-build builds, then RC over the
   lane's worktree with `CARGO_TARGET_DIR=$S/cargo-<lane>/check`); the reviewer greps for `unwrap|expect|\[` on
   application values and for typed access to app pointers.
5. Commit; the controller rebases the lane on `step3-dxgi` (in `$S/wt-s0`), fast-forwards, and ledgers it. Rebased
   lane branches are deleted only after `git cherry step3-dxgi step3-<lane>` shows nothing unmerged, and only step 3's
   (rule 7: other steps' branches and worktrees live in `$D` too).

Lanes never touch the root `Cargo.toml`, `Cargo.lock`'s other entries, `docs/CONVENTIONS.md`, `tools/audit.sh`,
`tools/rust-check.sh`, `tools/derive_inventory.txt` (except through `sh tools/audit.sh --update-derives` for their own
types), a golden, the probe, or another lane's files; a missing case, exception, derive or golden change goes to the
controller. Task 5 is the one lane that edits the workspace, gate and audit files, as its Files list says.

**Shared state.** Lanes use the owner's probe builds (`$S/probe-native/d3d11_dxgi`) and the references `$S/ref-*`
(Task 4), never rebuild them.

**Order and parallelism.**

| Task | Runs | Depends on |
|---|---|---|
| 1 S0a + D5: native exports, d3d10core's util, sorted profile lines | DONE early (`step3-s0`) | — |
| 2 S0b: `IMTLDXGIOutput`, the D3D11 gamma row | DONE early (`step3-s0`, `step3-probe`) | 1 |
| 3 S1a: the probe, all families (3a-3c in one worktree, `$S/wt-mn-probe`) | DONE early (`step3-probe`) | 1 |
| 4 S1b: references, goldens, rows (the kit's layout-bound part deferred) | DONE early (`step3-probe`) | 1, 2, 3 |
| 4R onto step R: both rebases, the lane kit, the deferred references | owner and controller (Opus) | 4, step R |
| 5 S2: dxmt-util, nvapi's LUID, empty crates, gate and audit scope | lane `s2` (Sonnet) | 4R |
| 6 S3: d3d10core in Rust | lane `d3d10core` (Opus) | 4, 5 |
| 7 S4-T1: dxgi's ABI, COM plumbing, skeleton | lane `t1` (Opus) | 4, 5 |
| 8 S4-T2: exports, `DllMain`, NVEXT, factory | lane `t2` (Opus) | 7 |
| 9 S4-T3: adapter, D3DKMT, config | lane `t3` (Opus) | 7 |
| 10 S4-T4: output, modes, Win32 WSI | lane `t4` (Opus) | 7 |
| 11 S4-T5: native WSI hooks | lane `t5` (Sonnet) | 7 |
| 12 S4-T6: meson switch, C++ deletion, the S4 merge's native and PE probes | lane `t6` on the merged branch (Opus); Step 4 owner | 6, 8-11 |
| 13 end-of-step gates, once | owner (Sonnet) | 12 |
| 14 one fix pass, the ported goldens | owner and lanes (Opus) | 13 |
| 15 notices, spec, push, pin | controller | 14 |

Tasks 1-4 ran early beside step R (ledger). The remaining work starts with Task 4R (Step 1, the rebases, first);
Task 5 branches from the rebased `step3-dxgi`, and the controller fast-forwards it only after Task 4R (the references
hold no step-3 Rust, P17); Tasks 6 and 7 branch after that. Reviews on Opus.

## Rules check (CLAUDE.md, tree-wide since step R)

- **Statics:**
  - `d3d10core`: `LOG: Logger` — one log file, a process-wide resource (as `Logger::s_instance`).
  - `dxgi`: `LOG: Logger` (same argument); `NVEXT: Once` and `NVEXT_ON: AtomicBool` in `nvext.rs` — `DllMain` and an
    export with no object (the C++'s `nvext_init`, `g_extension_enabled`); `DECLARED: AtomicBool` inside
    `DXGIDeclareAdapterRemovalSupport` — declared by that one function; `QI_LOGGED: Mutex<Vec<(Guid, Guid)>>` in
    `com.rs` — a per-pair log-once latch (`com_guid.cpp:45-46`); `CONFIG: LazyLock<Config>` in `config.rs` — a process
    fact latched once at the first `EnumAdapters1` (`config.cpp:394-398`), never under the loader lock.
  - The three vtables (`FACTORY_VTBL`, `ADAPTER_VTBL`, `OUTPUT_VTBL`) and the IID constants: immutable, not state.
  - Test-only (`cfg(test)`, never in a shipped build): Task 11's hooks setter keeps its hooks in one `AtomicPtr` in
    `wsi/hooks.rs` (winemetal's `WMTGetWsiHooks` isn't linked in host tests); Task 10's `cfg(test)` output
    constructor is a function, not state.
  - No `OnceLock` (outside `ONCELOCK_SITES`).
- **Config keys:** none new (`dxgi.customVendorId`, `dxgi.customDeviceId`, `dxgi.customDeviceDesc`, `dxgi.forceSDR`,
  `DXMT_ENABLE_NVEXT`, `DXMT_USE_DEFAULT_METAL_CACHE`, `DXMT_CACHE_EXE`, `DXMT_LOG_*`, `DXMT_CONFIG*` exist).
- **Wire fields:** none; `WMTDisplayDescription` is `winemetal-wire`'s.
- **Dependencies:** `dxgi` → `dxmt-util`, `winemetal-wire`; `d3d10core` → `dxmt-util`. Path crates only;
  `mtld3d-notices.py`'s LEGS untouched.
- **Derives:** `dxmt_util::com::Guid` `Clone, Copy, PartialEq, Eq` (16 bytes: within the 16-byte rule; Task 5);
  `Clone, Copy` on the ≤16-byte `abi.rs` structs the code copies (`Luid`, `Rational`, `Rect`, `SampleDesc`; Task 7).
  No derive on a larger aggregate (`ModeDesc1`, 32 bytes, is moved, not copied). Each through `--update-derives`.
- **Lint suppressions:** none. Exports named as exported with `#[unsafe(no_mangle)]` (nvngx's form); `DllMain` by
  `#[unsafe(export_name)]`; vtable types CamelCase with snake_case fields.
- **CONVENTIONS, gate and audit edits (Task 5, before any lane):** `docs/CONVENTIONS.md`'s "ABI constants have one
  home" list names `src/dxgi/src/abi.rs` as the DXGI/D3D10 home (with the Win32, D3DKMT and WSI-hook mirrors dxgi uses)
  until §7's bindgen; the application-pointer rule's scope (its scope sentence, its gate-table row and its section)
  becomes `src/d3d9/src`, `src/dxgi/src`, `src/d3d10/src`, as `tools/audit.sh`'s `APP_POINTER_DIR`; `PE_CRATES` gains
  `src/dxgi src/d3d10`; `tools/rust-check.sh`'s package lists gain `dxgi`, `d3d10core` (and `dxmt-util` on i686). No
  float-cast exception: the gamma ramp uses `float_cast::u32_to_f32`; any other conversion goes to the controller.
- **Duplicates (for §7):** config table and parser (Rust dxgi vs `config.cpp` for d3d11/d3d12, drift-tested); private
  data (Rust vs `ComPrivateData` in d3d11/d3d12); dxgi's seven WSI functions (Rust vs `wsi_*_win32.cpp`/
  `wsi_*_headless.cpp`, until step 6); d3d10core's IIDs and partial vtables vs `abi.rs` (P12); dxgi's COM plumbing vs
  D3D9's `src/d3d9/src/com_ref.rs`; adapter and output facts vs D3D9's (LUID, adapter count, video memory, device id, mode
  policy); nvapi's display mirrors vs `winemetal-wire`; `dxmt_util::com::Guid` vs `mtld3d_types::Guid`; the LUID rule
  in `d3d12_device.cpp:1479` (step 5); `abi.rs`'s Win32 (`DEVMODEW`, `MONITORINFOEXW`, `POINT`), D3DKMT and
  `WMTWsi*` mirrors vs the SDK headers and `winemetal.h` (layout-asserted, Task 7); `abi::transcode_into` vs
  `str::transcodeString` for d3d11/d3d12.

## Review Focus

1. **A 32-bit game in the VM** (guest mode, the default for i386): a slot whose argument width the Rust gets wrong
   (`SetVideoMemoryReservation`'s `UINT64`, `EnumAdapterByLuid`'s by-value `LUID`) unbalances the stack only on i386;
   app structs arrive 4-aligned; `_aulldiv` has never reached a final link through `rust_pe_wine_ntdll` (critic A2).
   Expected: the C++'s callee-pop sizes, unaligned access, Wine ntdll's `_aulldiv`. Tests: Task 12's `i686-ret.sh`;
   Task 3's `misaligned` cases (only `lane-dylib.sh`'s debug-assertion build makes a typed misaligned access fail:
   production builds and the hardware don't, so on PE the audit's grep is the only net); Task 3's
   `SetVideoMemoryReservation` values above 2^32; Task 6's i386 export/import tables; Task 13's `dxgi-guest.sh i386`,
   d3d-smoke `32: wmsize` (reaches the u64 division) and `32: conv`.
2. **A foreign `IDXGIOutput` handed to `SetFullscreenState`.** Expected: the swap chain's `IMTLDXGIOutput` QI fails,
   no gamma read, presents go on. Test: Task 2's `fs-gamma-foreign` row (graded from Task 2 Step 3 on): its fake
   answers QI itself, so `target_` is the fake (`d3d11_swapchain.cpp:268-273`).
3. **`GetDisplayModeList` with a short, zero-count or larger buffer.** Expected: `DXGI_ERROR_MORE_DATA`, `*pNumModes`
   untouched, only `*pNumModes` entries written (the forwarder relies on it, `rules.txt:344-349`); a larger buffer:
   S_OK, the count set, the tail untouched. Test: Task 3's `short`, `zero` and `big` cases, pinned in every golden;
   Task 10 implements and unit-tests the copy-out.
4. **nvngx's `DllMain` calling `DXGIGetDebugInterface1` under the loader lock, and `DllMain` per thread** (P10).
   Expected: no deadlock, no thread, nothing but the `Once`. Tests: Task 3's `DllMain threads` case; check.sh's DLSS
   and nvapi rows at Task 13; Task 8's review grep (no `LazyLock`, `Mutex` or log call reachable from `DllMain` or
   `DXGIGetDebugInterface1` besides the `Once`'s own registry/log work, which the C++ also does there).
5. **Game threads on dxgi objects**: UE's per-frame `QueryVideoMemoryInfo`, a C++ swap chain releasing the factory on
   its own thread, private data from two threads. Expected: atomic counts, freed once, no data race. Tests: Task 7's
   threaded refcount unit test and `private_data::tests::two_threads_set_get_balance`; Task 3's `threads` case;
   `present_loop cycles=20` at Task 13.

---

### Task 1 (S0a + D5): native exports, d3d10core's own util, sorted profile lines (owner) — DONE early

**Done (2026-10-30, ledger):** DXMTRS `step3-s0` (worktree `$S/wt-s0`, off da6ed2d, local) 4330a7e `dxgi.dylib: export
only its five entry points; d3d10core: its own util` and 797a2be `config: a built-in profile's lines in key order`.
Measured as Step 2 expects (125 exports before, the five after; `(from dxgi)` 50/49/3 → 1/1/0); key unchanged;
`$S/pre-wine.app` and `$S/pre-native` saved. Deviations, ledgered: the branch was `step3-s0` in a worktree, not
`step3-dxgi` in `$D` (it becomes `step3-dxgi` at Task 4R Step 1); the plan was not committed on `dxmtrs` (the
controller's); the Logger and sort checks ran at Task 4.

**Files (DXMTRS):**
- Modify `src/dxgi/meson.build`: `if not dxmt_crossbuild`, `dxgi_ld_args += ['-Wl,-exported_symbol,_' + n]` for the
  five `.def` names (the C++ objects define them; no `-u`).
- Modify `src/d3d10/meson.build:12`: `dependencies : [ util_dep, d3d11_dep ]` (critic A1).
- Modify `src/util/config/config.cpp:340-341`: log the matched profile's entries in key order (a `std::map` copy of
  `m_options`).

**Interfaces:** Produces the S0 native layout every later native check assumes: `dxgi.dylib` exports five names;
d3d11/d3d12 bind only `_CreateDXGIFactory1` from it; d3d10core binds only `_D3D11CoreCreateDevice` from d3d11.

- [x] **Step 1: Branches and before-state.** `git -C $D switch -c step3-dxgi`; MacNeutron `git switch dxmtrs`.
  Commit this plan on `dxmtrs` (`Plan: DXMTRS step 3, d3d10core and dxgi in Rust`). Ledger: `git -C $D rev-parse HEAD`
  (da6ed2d), the key, and from `build/dxmt-native/install/lib`: `nm -gU dxgi.dylib | wc -l` (125), `nm -mu
  <d3d11|d3d12|d3d10core>.dylib | /usr/bin/grep -c '(from dxgi)'` (50, 49, 3). Save
  `cp -cR build/wine-arm64/wine.app $S/pre-wine.app` and `cp -cR build/dxmt-native/install $S/pre-native` (da6ed2d,
  Task 3's runtimes, so Task 3 never reads a build the owner is rewriting).
- [x] **Step 2: Edit, then `sh eac-vm/dxmt-native/tests/build.sh`.** Expected: `build: ok`; `nm -gU dxgi.dylib` lists
  exactly `_CreateDXGIFactory`, `_CreateDXGIFactory1`, `_CreateDXGIFactory2`, `_DXGIDeclareAdapterRemovalSupport`,
  `_DXGIGetDebugInterface1`; `(from dxgi)` counts 1, 1, 0 (the one being `_CreateDXGIFactory1`); `nm -mu
  d3d10core.dylib` names `_D3D11CoreCreateDevice (from d3d11)` and no Logger symbol; `nm -m d3d10core.dylib` shows
  `Logger::err` defined in it.
- [x] **Step 3: Commit** (two): `dxgi.dylib: export only its five entry points; d3d10core: its own util` and `config:
  a built-in profile's lines in key order`. (The sort is checked by Task 4's `genshin` child: three lines in the order
  `customDeviceDesc`, `customDeviceId`, `customVendorId`.)

### Task 2 (S0b): the `IMTLDXGIOutput` seam and the D3D11 gamma row (owner) — DONE early

**Done (2026-10-30, ledger):** DXMTRS `step3-s0` 4fb43ea `dxgi: IMTLDXGIOutput, the gamma ramp through COM (d3d11's
swap chain QIs once)`; MacNeutron `step3-probe` e2a4942 `dxmt tests: d3d11 fullscreen presents through the output's
gamma ramp`. Both rows read their expected lines on both lanes at the seam. Deviations, ledgered: P7's fallback applied
(the fullscreen window sits off layer 0, so `fs-gamma` grades the presents alone; its row is named "…presents with the
output's gamma ramp set all succeed", and the presenter path was checked once by a scratch any-layer `winshot`); Step
3's two DXMTRS mutations not built (Step 2's mutant shows the oracle's bite).

**Files:**
- DXMTRS: modify `src/dxgi/dxgi_interfaces.h` (`namespace dxmt { struct DXMTGammaRamp; }`; `DEFINE_COM_INTERFACE(
  "4c1128cd-b437-4d44-9958-5d765bb85a76", IMTLDXGIOutput) : public IDXGIOutput6 { virtual const
  dxmt::DXMTGammaRamp *STDMETHODCALLTYPE GetGammaRamp() = 0; };`), `src/dxgi/dxgi_output.cpp` (`MTLDXGIOutputImpl :
  MTLDXGIObject<IMTLDXGIOutput>`; QI answers `IMTLDXGIOutput` with the same pointer), `src/d3d11/d3d11_swapchain.cpp`
  (drop the `dxgi_output.hpp` include; a `Com<IMTLDXGIOutput> gamma_output_` QI'd from `output` where `target_` is set
  at :337 (failure leaves it null), cleared at :362; Present's :803-806 reads `gamma_output_->GetGammaRamp()` when
  non-null). Delete `src/dxgi/dxgi_output.hpp`.
- MacNeutron: modify `dxmt/tests/d3d12_fullscreen.cpp` (new `gamma` and `gamma-foreign` modes; the `d3d12`/`d3d11`
  modes, which `present-gate.sh` and the fs loop run, unchanged), `dxmt/check.sh` (an `fs-gamma` row after the fs
  loop, alone; an `fs-gamma-foreign` row after it from Step 3), `Makefile` (`dxmt-check` also depends on
  `build/wine-arm64-tests/winshot`). Create (uncommitted) `$S/fs-gamma.sh <wine.app> <x64|arm64ec> [foreign]`:
  check.sh's `run` and the row's grading alone.

**Interfaces:**
- Produces: `IMTLDXGIOutput` slot 29 (`GetGammaRamp`, after IDXGIOutput6's 0-28; no destructor slots), the seam
  Task 10 implements in Rust.
- `d3d12_fullscreen.exe gamma`: the d3d11 mode's device and swap chain (EmulateModeset guard as the others), window in
  front; `SetFullscreenState(TRUE)`;
  `GetContainingOutput` → `SetGammaControl` with points `i/1024` (identity); `SetWindowTextA("d3d12_fullscreen
  gamma1")`; 600 frames of `ClearRenderTargetView(0, 0.3, 0, 1)` + `Present(1, 0)`; `SetGammaControl` with every
  point `1.0`; title `d3d12_fullscreen gamma2`; 600 frames more; `SetFullscreenState(FALSE)`. Prints `gamma presents
  <ok>/<n> <first failing hr, hex8>`.
- `d3d12_fullscreen.exe gamma-foreign`: the same device and swap chain; `SetFullscreenState(TRUE, foreign)` with a fake
  `IDXGIOutput1` that answers QI itself (IUnknown, IDXGIObject, IDXGIOutput, IDXGIOutput1 → itself, AddRef'd;
  everything else, `IMTLDXGIOutput` included → `E_NOINTERFACE`) and forwards only the other methods to the real
  output — `SetFullscreenState` QIs `pTarget` for `IDXGIOutput1` and keeps the answer as `target_`
  (`d3d11_swapchain.cpp:268-273`, `:337`), so a fake forwarding QI would hand back the real output; 60 presents,
  leave. Prints `gamma foreign presents <ok>/<n> <hr>`. Before the seam, Present `static_cast`s `target_` and calls
  past the fake's 23-slot vtable (`:803-806`): undefined, so this mode is graded only from Step 3 on.
- The rows: two `winshot` runs started in the background before `run ours fs-gamma dxmt "$TESTS/d3d12_fullscreen.exe"
  gamma` (titles `d3d12_fullscreen gamma1`,
  `…gamma2`, PNGs in `$WORK`); `expect "d3d11: fullscreen presents go through the output's gamma ramp" "<gamma
  presents line>;<g1 green ≥ 56>;<g2 white ≥ 56, green < 5>" "gamma presents 1200/1200 00000000;green;white"`.
  Then `run ours fs-gamma-foreign dxmt "$TESTS/d3d12_fullscreen.exe" gamma-foreign`; `expect "d3d11: a foreign
  output's swap chain presents without its gamma ramp" "<gamma foreign presents line>" "gamma foreign presents 60/60
  00000000"`.

- [x] **Step 1: The program and the `fs-gamma` row** (MacNeutron), on the unmodified C++ (Task 1's tree): `make
  wine-arm64 dxmt-tests dxmt-tests-arm64ec build/wine-arm64-tests/winshot` (an owner build, P1), then `sh
  $S/fs-gamma.sh build/wine-arm64/wine.app x64` and `arm64ec`. Expected: the `fs-gamma` line equals its expected
  string in both lanes (the `static_cast` path works today). `gamma-foreign` runs once per lane and its result
  (crashed or not) is ledgered, not graded. If `winshot` finds no `gamma1` window (fullscreen windows off layer 0),
  apply P7's fallback and ledger it.
- [x] **Step 2: Mutation.** The gamma2 ramp made identity: the row FAILs on `white`. Restore.
- [x] **Step 3: The seam** (DXMTRS), `make wine-arm64`, `sh $S/fs-gamma.sh … x64` and `arm64ec`, each also with
  `foreign`. Expected: both rows pass in both lanes. Mutations, each restored (a wrong IID can't bite: the output has
  one vtable and the same `this` for every IID, so slot 29 stays `GetGammaRamp`): the output's QI refuses
  `IMTLDXGIOutput` → `fs-gamma` FAILs on `white`; Present skips the ramp read → `white` FAILs.
- [x] **Step 4: Commits:** DXMTRS `dxgi: IMTLDXGIOutput, the gamma ramp through COM (d3d11's swap chain QIs once)`;
  MacNeutron `dxmt tests: d3d11 fullscreen presents through the output's gamma ramp`.

### Task 3 (S1a): the probe `d3d11_dxgi`, in three parts (MacNeutron worktrees) — DONE early

**Done (2026-10-30, ledger):** all three parts by one agent in one worktree (`$S/wt-mn-probe`, branch `step3-probe`,
one commit per family; no `-a`/`-o` worktrees or branches): 62e212c `dxmt tests: d3d11_dxgi's harness`, 7f224dc
`…harness, from its first runs`, 89d9e04 `dxgif`, d9cc462 `d3d10c`, 6c89259 `dxgia`, 962c0d1 `dxgio` cases. `$S/run.sh`
written, its nvapi control empty. Later fixes on the same branch: b950449 (P20's null-riid cases for every QI-shaped
call: 202 cases, 23 `NOT_ON_C`) and 76434db (closest-mode lines with a rate asked say `mode=listed`). Rulings that
change later steps: profile children start through `cmd.exe` on PE (x64 FEX crash on CreateProcess of a copy of its
own image); `--bites` spawns one child per slot; slot 20 prints `kmt=valid|invalid|0 opens=…`; `run.sh`'s watchdog is
`RUN_TIMEOUT` (300 s). Step 3's merge onto `dxmtrs` did not happen: P22.

**Files (three parts, three worktrees, disjoint files; the controller cherry-picks the three onto `dxmtrs`):**
- 3a, worktree `$S/wt-mn-probe`, branch `step3-probe` from `dxmtrs`: create `dxmt/tests/d3d11_dxgi.cpp` (fits
  `Makefile:60, 79`'s glob: the harness, line format, case table, children, isolate, `--list`/`--cover`/`--bites`,
  `rate`, the fakes, shared helpers), `dxmt/tests/d3d11_dxgi/dxgif.inc`, `d3d11_dxgi/d3d10c.inc`, and empty
  `d3d11_dxgi/dxgia.inc`, `d3d11_dxgi/dxgio.inc` (Step 1's commit); modify `Makefile` (the two `d3d11_dxgi.exe`
  targets also depend on `$(wildcard dxmt/tests/d3d11_dxgi/*.inc)`: the pattern rules at `:65` and `:84` track only
  the two common headers). Uncommitted: `$S/run.sh` (Step 0).
- 3b, worktree `$S/wt-mn-probe-a`, branch `step3-probe-a` from 3a's Step 1 commit: `dxmt/tests/d3d11_dxgi/dxgia.inc`.
- 3c, worktree `$S/wt-mn-probe-o`, branch `step3-probe-o` from 3a's Step 1 commit: `dxmt/tests/d3d11_dxgi/dxgio.inc`.

**Interfaces:**
- `$S/run.sh <wine.app> <x64|arm64ec> <name> <exe> [args…]` → `$S/runs/<name>.txt` (3a Step 0; Task 4 reuses it):
  check.sh's `run` (a tool folder installed from `<wine.app>` by `macneutron install --wine-app`, its own compat
  prefix, `DXMT_MAX_ANISOTROPY=0`, CRs removed), step 1b's `probe.sh` shape; `<exe>` absolute; the caller's
  environment passed through (nothing sets `MACNEUTRON_DLSS` unless the caller does). Control: `MACNEUTRON_DLSS=1
  $S/run.sh $S/pre-wine.app x64 nvapi-ctl <M>/build/dxmt-tests/d3d11_nvapi.exe 'C:\windows\system32\nvapi64.dll'
  fixed`, its `^nv` lines diffed with `dxmt/tests/d3d11_nvapi.fixed` → empty (check.sh's nvapi row).
- `d3d11_dxgi.exe fixed|full [<family>…]|rate|--list|--cover|--bites [--ported]` (no family: all; 3b and 3c run only
  their own); internal `--child <env> <first-case> <last-case>`. Built
  as PE (both Mac lanes, the guest x86_64 and i686 by `dxgi-guest.sh`) and natively (`#ifndef _WIN32`: `native-prelude.h`,
  `posix_spawn` for children, `WMTSetWsiHooks` resolved from `winemetal.dylib`, `D3D10CoreCreateDevice` linked).
- `--bites` (P21): Mac lanes; `bites <Interface>::<Method> <yes|excluded: reason|NO>`, exit 1 on any `NO`.
- Line: `<family> <Interface>::<Method> <case> <hr hex8> [k=v …]`; families `dxgif` (factory, exports), `dxgia`,
  `dxgio`, `d3d10c`, `dxgil` (`dxgil <child> <file> <line>`: every line of the child's `*_dxgi.log` and
  `*_d3d10core.log`, `\r` printed as `<CR>`). A fixed number of lines whatever the machine (P5).
- Case table: `{family, interface, slot, name, flags, fn}`; flags `ISOLATE` (a child; `crashed <code>`, `hang` after
  10 s, or the line), `NOT_ON_C` (P6), `FULL_ONLY`, `PE_ONLY`, `NATIVE_ONLY`, `ENV_<child>`. `--list` prints
  `<family> <slot> <Interface>::<Method> <case> <flags>`; `--cover` exits 1 unless every slot of the factory (0-29),
  adapter (0-20) and output (0-29) vtables and every export has a case.
- Conventions: every out struct prefilled 0xA5 and printed as `written=<ranges> untouched=<ranges>`; AddRef/Release
  return values around every object-returning call (`ref=+n`); identity booleans (`same=1`); out pointers on failure
  (`out=null|untouched`); private interfaces through the C++ header layout (`IMTLDXGIAdapter` slots 19-20 declared
  `uint64_t`/`UINT`, `IMTLDXGIOutput` slot 29, `IMTLSwapChainFactory` slot 3). `full` prints every raw value,
  each mode's `RefreshRate` as `num/den` (P5). `misaligned` cases pass app buffers, structs and `riid`s at 8n+4.
- Fakes: a device answering IUnknown + `IMTLSwapChainFactory` (records the IIDs asked, the factory pointer, the HWND
  (`hwnd=same`), the descs; returns a fake swap chain; prints its own reference count after each call, `devref=+0`:
  the C++ Releases the `IMTLSwapChainFactory` it queried, `dxgi_factory.cpp:112`); one refusing it (also `devref=+0`);
  a counting IUnknown for private data (logs each AddRef/Release with the count); an adapter for `d3d10c` whose
  `CheckInterfaceSupport` prints `asked=<IID> umd=<null|set>`, refuses only `ID3D10Device` and answers S_OK to any
  other IID (so a wrong IID or a non-null umd changes the line, `d3d10core.cpp:23`).
- Children (P18): `default`; `config` (`DXMT_CONFIG="dxgi.customVendorId=beef;dxgi.customDeviceId=1234;
  dxgi.customDeviceDesc=<200 ASCII chars>;dxgi.forceSDR=True"`); `configu` (`DXMT_CONFIG=dxgi.customDeviceDesc=` 125
  ASCII characters, then `é`, `𝄞`, `Z`: the pair would need units 126-127 of 127, so it is dropped and `Z` fills unit
  126, `util_string.cpp:142-180`, `util_string.hpp:125-137`); `confbad` (a `dxmt.conf` whose `dxgi.customDeviceDesc`
  holds an invalid UTF-8 byte and a stray continuation byte: each becomes `?`, `util_string.cpp:17-58`); `nvext`
  (`DXMT_ENABLE_NVEXT=1`); `conffile` (a
  `dxmt.conf` with a `[d3d11_dxgi.exe]` section setting `dxgi.customVendorId = 4321`, CRLF line ends, ending in an
  inactive `[other.exe]` section, plus `DXMT_CONFIG=dxgi.customDeviceId=5678`, which that context makes inactive);
  `conffile2` (`DXMT_CONFIG_FILE=other.conf`, relative to its folder, setting `dxgi.customVendorId = 2468`: `Found
  config file: other.conf`, `config.cpp:353-358, 373`); `omori`, `genshin` (`PE_ONLY`; copies named `OMORI.exe`,
  `GenshinImpact.exe`); `emptysection` (`NATIVE_ONLY`: a `[]` section, active natively); `isolate:<case>`.
- `rate`: `rate <Method> <ns per call>` for `GetMTLDevice`, `GetDesc1`, `QueryVideoMemoryInfo` (1 s each, after 1000
  warm-up calls).

Cases (each a test; relations per P5; `full` adds the raw values):
- **dxgif**: `CreateDXGIFactory2` flags 0 → count 1 (`ref=+1/-1` around an AddRef/Release), flags 1 →
  `GetCreationFlags` 1; each export with each of IUnknown, IDXGIObject, IDXGIFactory..6 → S_OK `same=1`;
  IDXGIFactory7 → E_NOINTERFACE, out null; null `pp` → E_POINTER. Factory `QueryInterface`: same set; unknown IID
  twice (one warn line in `dxgil`); null ppv. `GetParent` → E_NOINTERFACE + WARN, out null; `GetParent(NULL)` →
  E_NOINTERFACE + WARN (:45-46, null-safe). Private data trio (`pdata-*`, run
  on all three objects): set/get, too small (`pdata-short`: MORE_DATA, buffer untouched, `size=<n>`), larger buffer
  (`pdata-big`: S_OK, size shrinks to n, the tail untouched), null size, missing (NOT_FOUND, size 0), erase
  (S_OK then S_FALSE), interface set/get/replace (AddRef new before Release old, by the fake's call log; get AddRefs,
  `com_private_data.cpp:80`), `pdata-iface-erase` (the fake logs a Release), `pdata-iface-dies` (a fresh factory,
  adapter and output each store the fake, the object is Released: the fake logs a Release and its balance prints 0),
  `pdata-retype` (data replaced by an interface and back: one AddRef, one Release), null
  interface stored (pointer-sized null), null `pData` (size, S_OK). `EnumAdapters`/`EnumAdapters1` 0 (new object a
  call, factory `ref=+1` per adapter, back after Release), past the last (`notfound-at=<n>`, n = adapter count);
  `EnumAdapters(0, NULL)` → `887a0001`; `EnumAdapters1(<count>, NULL)` → `887a0002` (returns before the write);
  `EnumAdapters1(0, NULL)` `ISOLATE|NOT_ON_C` (P20). `EnumAdapterByLuid` (own LUID; wrong LUID + WARN; unknown IID),
  `EnumWarpAdapter` (adapter 0 + WARN), `EnumAdapterByGpuPreference` adapter 0 with GpuPreference 0/1/2 and unknown
  IID, and `EnumAdapterByGpuPreference(<count>, UNSPECIFIED, IDXGIAdapter, 0xA5 out)` → `887a0002`,
  `out=untouched` (no `InitReturnPtr`, `dxgi_factory.cpp:315-326`; ByLuid and Warp null it, :266, :286).
  `MakeWindowAssociation` round trip with `GetWindowAssociation`;
  flags 1 → WARN; null out. `CreateSoftwareAdapter` null/non-null. `IsCurrent`, `IsWindowedStereoEnabled`.
  `CreateSwapChainForHwnd` through the fake (IIDs asked, `factory=self`, `hwnd=same`, `devref=+0`, 0×0 filled from
  the client rect, default fullscreen desc, restrict ignored, out = the fake's), refusing device (UNSUPPORTED + ERR,
  `devref=+0`), each null argument; `CreateSwapChain` (DESC→DESC1 and fullscreen desc as `dxgi_factory.cpp:70-100`;
  `hwnd=same` from `OutputWindow`, `devref=+0`; null arguments before any write; out written on failure).
  `CreateSwapChainForCoreWindow`, `…ForComposition`, `GetSharedResourceAdapterLuid`, the four
  `Register*Status*` → E_NOTIMPL + ERR; the two `Unregister*` → ERR. `CheckFeatureSupport` tearing (TRUE), size 8,
  feature 99 + ERR; null data `ISOLATE|NOT_ON_C` (→ E_INVALIDARG). `DXGIDeclareAdapterRemovalSupport` twice (S_OK +
  WARN, then ALREADY_EXISTS); `DXGIGetDebugInterface1` NVEXT GUID with a 0xA5 `pp` (S_OK in `nvext`, else
  E_NOINTERFACE; `pp` untouched either way, `dxgi.cpp:88-104`) and a foreign IID with a 0xA5 `pp` (E_NOINTERFACE,
  untouched). `DllMain registry` (`PE_ONLY`, in the `default` and `nvext` children): the three values `dxgi.cpp:39-44`
  writes, read back (type, size, bytes): with NVEXT two `REG_DWORD` 1 at `{41FCC608-8496-4DEF-B43E-7D9BD675A6FF}` and
  `REG_SZ` `FullPath` = `C:\Windows\System32` with its NUL (40 bytes); without, all three absent (:35-38 delete
  them); on i386 absent in both (:17-19 return first). `DllMain threads`: 8 threads created and joined after load →
  `ok`. Null `riid` (P20), each `ISOLATE|NOT_ON_C`: factory `QueryInterface`, factory `GetParent`,
  `CreateDXGIFactory2`, `EnumAdapterByLuid`, `DXGIGetDebugInterface1`, `GetPrivateData`; `misaligned`
  (`QueryInterface` with a `riid` at 8n+4, the `CreateSwapChainForHwnd` desc at 8n+4, `GetPrivateData`'s `pData`
  at 8n+4).
- **dxgia**: QI set (IUnknown, IDXGIObject, IDXGIAdapter..4, IMTLDXGIAdapter → `same=1`; Wine's
  `17399d75-964e-4c03-99f8-9d4fd196dd62` → E_NOINTERFACE with no log line; unknown → one WARN). `GetParent` → the
  creating factory (`same=1`); `GetParent` with an unknown IID (E_NOINTERFACE + the factory's WARN) and with a null
  out (E_POINTER, `dxgi_adapter.cpp:73`). `GetDesc`, `GetDesc1`, `GetDesc2`, `GetDesc3`: bytes written/untouched;
  `desc=same-all`, `desc-nul<128`, `desc=device-name` where no custom description applies (the probe's own
  `MTLDevice_name` through winemetal, in the target's WCHAR encoding: natively this pins the UTF-32 write);
  VendorId exact (106b; 10de in `nvext`; beef in `config`; 4321 in `conffile`; 2468 in `conffile2`; 1002
  in `genshin`); DeviceId (0; 1234; 7340); Description exact in `config` (127 of 200 characters), `configu`
  (125 ASCII, `é`, `Z` on PE; 125 ASCII, `é`, `𝄞` natively), `confbad` (`?` for each bad byte) and `genshin`; SubSys,
  Revision, system memories 0; `vram=half-budget` (unified), `vram=clamp` on i386 when the budget is above 6 GiB;
  `luid=bswap-registry` (the probe's own `MTLDevice_registryID(GetMTLDevice())` through winemetal), equal across the
  four descs and two adapters; Flags; preemption; null → E_INVALIDARG. `misaligned` (`GetDesc1`,
  `QueryVideoMemoryInfo` into a buffer at 8n+4). `CheckInterfaceSupport` IDXGIDevice, ID3D10Device, ID3D10Device1
  (umd all ones), null umd (d3d10core's shape), other IID (UNSUPPORTED + two err lines), null IID
  `ISOLATE|NOT_ON_C` (P20). `EnumOutputs` 0.. (new
  object a call, adapter `ref=+1`; `count=EnumDisplayMonitors`), past the last, null. Slot 12 `ISOLATE|NOT_ON_C` (→
  E_NOTIMPL + ERR `Not implemented`); slot 13 (returns). `QueryVideoMemoryInfo` LOCAL/NON_LOCAL (`budget>0`,
  `avail=0`, reservation), node 1, group 7, null. `SetVideoMemoryReservation` `0x0000000180001000` LOCAL and
  `0x0000000200002000` NON_LOCAL (`UINT64`, `dxgi_adapter.cpp:264`: values above 2^32, so a `u32` slot can't pass),
  each read back exactly; node 1, group 7. Slot 16 cookie (0, S_OK); null cookie `ISOLATE|NOT_ON_C` (→ E_INVALIDARG); slot 17. Slot 19
  (`nonzero`, `stable`), slot 20 (`kmt=consistent` with the probe's own `D3DKMTOpenAdapterFromLuid`; natively `0`).
  `threads` (4 threads × 10000 AddRef/Release and `QueryVideoMemoryInfo` → count unchanged).
- **dxgio**: QI set (IUnknown, IDXGIObject, IDXGIOutput..6, IMTLDXGIOutput → `same=1`); `GetParent` → the adapter;
  `GetParent` with an unknown IID and with a null out (`dxgi_output.cpp:189`).
  `GetDesc` (`name=monitorinfo`, `rect=monitorinfo`, `monitor=enum`, AttachedToDesktop 1, Rotation UNSPECIFIED;
  null). `GetDesc1` (`hdr=consistent`; `forceSDR` in `config` and `omori`: 8 bpc, G22_P709; MinLuminance 0;
  MaxFullFrame = Max; primaries in [0,1]; null → `887a0001`, :576). `GetDisplayModeList`/`…1` for R8G8B8A8_UNORM, B8G8R8A8_UNORM(_SRGB),
  R10G10B10A2_UNORM, R16G16B16A16_FLOAT, UNKNOWN (0, S_OK), BC1_UNORM (0, S_OK); flags 0, INTERLACED, SCALING: count,
  `sorted`, `format=asked`, `has-current`, `den=1000` on PE / `den=hook` natively (P5); `short` (n−1: MORE_DATA,
  count n−1, n−1 written, tail untouched); `zero` (count 0, buffer: MORE_DATA, nothing written); `big` (`…1` with
  n+3 entries: S_OK, count n, n written, the tail untouched, :429-437); null count; `misaligned` (the `…1` buffer at
  8n+4). `FindClosestMatchingMode1` (every case passes a device unless it says otherwise; a member → itself; Format
  UNKNOWN → R8G8B8A8_UNORM_SRGB; Format UNKNOWN with a null device → `887a0001` (:449); 640×0 → `887a0001` (:453);
  null mode or null out → `887a0001`; Stereo TRUE → `887a0002` with no ERR (filtered empty, :510-511); a format with
  no modes → ERR `No modes found`, `887a0002` (:498-501); 0×0 at 0/0 → `=current` (the second pass, :461-492,
  :507-508); 0×0 at 61/1 → `size=current`; a target with refresh 61/1 → the nearest member, the u64 path; an
  explicit PROGRESSIVE scanline; the debug line in `dxgil`, with relation words in `fixed`, P5);
  `FindClosestMatchingMode` (null mode, null out → `887a0001`, :248; a member). `WaitForVBlank`, `TakeOwnership` (WARN),
  `ReleaseOwnership` (WARN). Gamma: `GetGammaControlCapabilities` (1024 points `i/1023`, no scale/offset, 0..1;
  null → `887a0001`, :296); `GetGammaControl` and `SetGammaControl` null (`887a0001`, :311, :326); `misaligned`
  (`GetGammaControl`/`SetGammaControl` structs at 8n+4);
  `SetGammaControl` with a ramp whose point 1024 differs, then `GetGammaControl` (1024 points equal, point 1024
  untouched, scale 1, offset 0) and slot 29 (`GetGammaRamp`: points 0, 511, 1023 of each channel and `version` 1, then
  2 after a second set); a second `EnumOutputs(0)` object's `GetGammaControl` stays identity (B9). `SetDisplaySurface`,
  `GetDisplaySurfaceData(1)`, `GetFrameStatistics` (E_NOTIMPL + ERR); `DuplicateOutput(1)` null device
  (E_INVALIDARG), a device (UNSUPPORTED + ERR); `SupportsOverlays`, `CheckOverlaySupport` (flags 0; null),
  `CheckOverlayColorSpaceSupport`, `CheckHardwareCompositionSupport` (each: flags 0; null flags → S_OK, :569, :616).
  Natively (`NATIVE_ONLY`): two fake displays
  (1920×1080 at 0,0 `\\.\DISPLAY1`; 2560×1440 at 1920,0 `\\.\DISPLAY2`), their modes, every one with a non-zero
  denominator, and a fake window 800×600. Only the `isolate:zero-den` child's hooks add a 0/0 mode to the second
  display (`zero-den`, `ISOLATE|NOT_ON_C`: skipped by the filter, sorted last): arm64's divide by zero gives 0, so the
  native C++ would sort it first and keep it, and in the shared fixture that order would land in ordinary lines.
- **d3d10c**: `D3D10CoreCreateDevice` flags 0 / FL 10_0 (S_OK; `ID3D10Device`/`1` and the D3D11 device share an
  IUnknown; `GetCreationFlags` 0; `GetMultithreadProtected` TRUE; device `ref=1`); `SINGLETHREADED` (FALSE, flags 1);
  `BGRA_SUPPORT` 0x20 (flags 0x20, D17); FL 10_1 (`fl=asked`); the fake adapter (`asked=<ID3D10Device's IID>
  umd=null`, its hr, out null); null
  `ppDevice` (E_POINTER, B12); null adapter `ISOLATE|NOT_ON_C` (→ E_INVALIDARG). `D3D10CoreRegisterLayers` (E_NOTIMPL +
  ERR).

Builds, in each worktree (Global Constraints: `BUILD_DIR`): `mkdir -p build/dxmt-tests build/dxmt-tests-arm64ec &&
BUILD_DIR=<M>/build make build/dxmt-tests/d3d11_dxgi.exe build/dxmt-tests-arm64ec/d3d11_dxgi.exe` (the exes land in
the worktree's `build/`); natively `build-tests.sh`'s compile line with every path absolute, against `$S/pre-native`
(`WMTSetWsiHooks` and `MTLDevice_registryID` through `dlsym`), into `$S/pre-native/d3d11_dxgi-<part>`. Runs:
`$S/run.sh $S/pre-wine.app <lane> …` with the exe's absolute path (both saved at Task 1 Step 1).

- [x] **Step 0 (3a): `$S/run.sh`** and its control (Interfaces). Expected: the nvapi diff is empty.
- [x] **Step 1 (3a): The harness.** `d3d11_dxgi.cpp`, the fakes, children, isolate, `--list`/`--cover`/`--bites`,
  `rate`, the empty `dxgia.inc`/`dxgio.inc`, the Makefile edit; it builds and `--list` runs. Commit `dxmt tests:
  d3d11_dxgi's harness`; the controller branches 3b and 3c from it.
- [x] **Step 2 (3a, 3b, 3c in parallel): One family each** (3a `dxgif` and `d3d10c`, 3b `dxgia`, 3c `dxgio`), built
  and run on the pre-S0 C++ (`fixed <family>` and `full <family>`, each twice, both lanes and natively). Expected:
  exit 0; identical runs except `full`'s usage values; every case of the family prints; `IMTLDXGIOutput` cases print
  E_NOINTERFACE (pre-S0b, expected); no `ISOLATE|NOT_ON_C` case runs without `--ported`. Commits: `dxmt tests:
  d3d11_dxgi's <family> cases`.
- [x] **Step 3 (controller): Merge.** Cherry-pick `step3-probe`, `step3-probe-a`, `step3-probe-o` onto `dxmtrs`;
  build from the main checkout. Expected: `--cover` passes; `fixed` and `full` of all families, twice, both lanes and
  natively on the pre-S0 C++, as Step 2. (Done as checks on `step3-probe`: `--cover` ok, `fixed`/`full` twice per lane
  identical, x64 = arm64ec, native twice identical; the merge itself is Task 15's, P22.)

### Task 4 (S1b): references, goldens, rows and the lane kit (owner) — DONE early, its layout-bound part in Task 4R

**Done (2026-10-30, ledger):** MacNeutron `step3-probe` cecbcf1 `dxmt tests: dxgi and d3d10core goldens from the C++,
their rows` (then b950449 and 76434db, Task 3's fixes, which hand-edited the four goldens). References from the S0 C++
(4fb43ea, key `71a7e5f2…3311`): `$S/ref-wine.app` (cdhash dee5abb8…), `ref-mac`, `ref-guest`, `ref-native`, `ref-build`
(with meson's `.._util_rust_pe_rt.c.obj` names), `ref-vm`, `ref-present` (step 2's copy), `ref-probe`, `ref-PROVENANCE.txt`;
`probe-native/d3d11_dxgi`. Goldens: `.fixed` 403 lines, `.native` 700, `.i386` and `.guest64` 405 each. `--bites`: 81
slots, 78 yes, 3 excluded, no `NO`. Rows (`dxgi`, `d3d10-core`, `d3d10-wine`, `fs-gamma`, `fs-gamma-foreign`) ok on both
lanes inside check.sh's own code (`$S/rows.sh`); every RED of Step 6 bit. Kit written: `run.sh`, `full-diff.sh`
(control ok). Deviations, ledgered: the VM gate row compares `dxgif` and `d3d10c` only (130 lines; the guest's display
changes the other families, which `.i386`/`.guest64` pin); `dxgi-guest.sh` has a third mode, `compare`; rates measured
at load ~5.6. **Deferred to Task 4R:** Step 2's five layout-bound scripts (`cfg.sh`, `lane-dll.sh`, `lane-dylib.sh`,
`lane-app.sh`, `i686-ret.sh`) and their controls, Step 4's two full check.sh reference runs (`$S/ref-perf`), Step 5's
present-gate run on S0, and a quiet-machine rate.

**Files:**
- MacNeutron (commit): `dxmt/tests/d3d11_dxgi.{fixed,native,i386,guest64}`; `dxmt/check.sh` (rows below);
  `eac-vm/dxmt/invocations` (`d3d11_dxgi|d3d11_dxgi.exe|fixed|dxgif dxgia dxgio d3d10c dxgil`);
  `eac-vm/dxmt-native/build-tests.sh` (P3); `eac-vm/dxmt-native/tests/build.sh` (all seven names: `dxgi` five,
  `d3d10core` two); `eac-vm/dxmt/check-build.sh` (both widths: `dxgi.dll` exports 9 `CreateDXGIFactory`, 10
  `CreateDXGIFactory1`, 11 `CreateDXGIFactory2`, 16 `DXGIDeclareAdapterRemovalSupport`, 17 `DXGIGetDebugInterface1`;
  `d3d10core.dll` its two names; i386 `d3d10core.dll` imports `D3D11.DLL: D3D11CoreCreateDevice`); create
  `eac-vm/tests/dxgi-guest.sh <i386|x86_64> [record]` (P4).
- Uncommitted (`$S`): `ref-wine.app`, `ref-mac/`, `ref-guest/{x86_64,i386}-windows/`, `ref-build/{arm64ec,x86_64,i686}/`,
  `ref-native/`, `ref-vm/`, `ref-present/`, `ref-probe/`, `ref-perf/`, `probe-native/d3d11_dxgi`, and the kit
  (`run.sh` is Task 3's): `full-diff.sh` here; `cfg.sh`, `lane-dll.sh`, `lane-dylib.sh`, `lane-app.sh`, `i686-ret.sh`
  in Task 4R (written for step R's layout).

**Interfaces (the kit):**
- `$S/run.sh`: Task 3's (3a Step 0).
- `$S/cfg.sh`, `$S/lane-dll.sh`, `$S/lane-dylib.sh`, `$S/lane-app.sh`, `$S/i686-ret.sh`: Task 4R's Interfaces.
- `$S/full-diff.sh <a> <b>`: the two `full` outputs line by line with `$S/ref-probe/full.mask`'s keys removed.
  Control: a C++ `full` output with one unmasked value changed is reported.

- [x] **Step 1: Save the S0 C++.** After Tasks 1-2: `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter`, `sh
  eac-vm/dxmt/build.sh`, `sh eac-vm/dxmt-native/build.sh`. Copy: `$S/ref-wine.app` (`cp -cR`); `ref-mac/`
  (`exports.txt`, `imports.txt`: `llvm-readobj --coff-exports`/`--coff-imports` of every DLL in
  `DXMT/aarch64-windows`); `ref-guest/<arch>/` (the DLLs and the same two files); `ref-native/` (`install/lib`, `nm
  -gU` and `nm -mu` of each dylib); `ref-build/<arch>/` from `$D/../dxmt-build` (arm64ec),
  `build/eac-vm-work/dxmt-guest-build` (x86_64) and `-build32` (i686): `src/{dxgi,d3d10}/*.dll.p/*.obj` (i686),
  every `version.o`, `rust_pe_rt.c.obj` and `*.dll.a`, and the `ninja -t commands` link lines of `dxgi.dll`,
  `d3d10core.dll`, `nvngx.dll` and `winemetal.dll` in each build dir. Ledger the key. Only now may the controller
  fast-forward Task 5 (P17; now after Task 4R).
- [ ] **Step 2: Kit.** Write the six scripts; each control as stated. Build `$S/probe-native/d3d11_dxgi` (P3's link).
  (Done: `full-diff.sh` and its control, `$S/probe-native/d3d11_dxgi`. The other five: Task 4R Step 4.)
- [x] **Step 3: Goldens, from the S0 C++.** `run.sh` takes absolute exe paths; nothing here sets `MACNEUTRON_DLSS`.
  - Mac: `$S/run.sh $S/ref-wine.app x64 dxgi-x64-1 <M>/build/dxmt-tests/d3d11_dxgi.exe fixed`, twice, and the same for
    arm64ec. Expected: the four identical (families only). They become `d3d11_dxgi.fixed` (one file; a lane
    difference is ledgered and gets its own file). `full` twice per lane into `$S/ref-probe/<lane>.full.{1,2}`; keys
    differing between runs → `full.mask`. `rate` once on arm64ec on a quiet machine → `rate-arm64ec.txt` (with the
    1-min load average; P16).
  - Native: `$S/probe-native/d3d11_dxgi fixed` twice (identical) → `d3d11_dxgi.native`; `full` twice →
    `$S/ref-probe/native.full.{1,2}` (their differing keys join `full.mask`); `rate` → `rate-native.txt`.
  - Check by reading: the `genshin` child's three info lines in key order (Task 1); `dxgil` lines carry `<CR>` on PE,
    none natively; the native log file is `_dxgi.log`; no `dxgil` line in `.fixed` or `.native` holds a
    display-dependent number (P5); every mode list's `den=` relation is there.
- [x] **Step 4: Rows.** (The two full reference runs of its second bullet: Task 4R Step 5.)
  - check.sh, in the window after `:1294`'s `unset … MACNEUTRON_DLSS` (after the `dlss-val` expect, `:1295-1296`,
    before the frame-generation block at `:1297`; `MACNEUTRON_DLSS` is exported at `:1246-1283`, `:1285-1294`,
    `:1301-1306`, `:1313-1315`, `:1323-1326`, and the launcher turns it into `DXMT_ENABLE_NVEXT=1`,
    `Launcher.swift:340`, which the probe's statically imported `dxgi.dll` reads at attach): `run ours dxgi dxmt "$TESTS/d3d11_dxgi.exe" fixed`; `expect "dxgi and d3d10core
    answer as DXMT's C++ did" "${firstdiff:-same}" same` with `firstdiff=$(/usr/bin/grep -E '^(dxgif|dxgia|dxgio|
    d3d10c|dxgil) ' "$WORK/dxgi.txt" | diff - "$ROOT/dxmt/tests/d3d11_dxgi.fixed" | sed -n 2p)`. `run ours d3d10-core
    dxmt "$TESTS/d3d10_core.exe" "Z:$S/d3d11"` expecting `d3d10 init 0 0;d3d10 map 0;d3d10 query 00000000 256;d3d10
    draw 179456`; `run ours d3d10-wine dxmt "$TESTS/d3d10_wine.exe" "Z:$S/d3d11"` expecting `wine device
    00000000;wine stateblock 2 0 0 16 16 1 1 1 1;wine draw 179456`.
  - The C++'s full check.sh, once per lane (a reference run): `MACNEUTRON_ARM64_APP=$S/ref-wine.app sh dxmt/check.sh`
    → `$S/ref-perf/check-x64.txt`, and the ARM64EC form (Global Constraints) → `check-arm64ec.txt`. Expected: every
    row ok, the new ones included (flaky rows: one re-run each, ledgered). Ledger the measured ok count per lane; Task
    13 expects the same ok/FAIL row set.
- [x] **Step 5: Guest and native comparators.** (Its present-gate run on S0: Task 4R Step 5.) First `export
  MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img`
  (absolute, P17) for every VM command of this step.
  - `sh eac-vm/dxmt/share.sh`, then `MN_REF_RUNS=2 MN_REF_OUT=$S/ref-vm MN_REF_WINE_APP=$S/ref-wine.app sh
    eac-vm/dxmt/ref.sh`. Expected: 45 programs × 2 × {txt, status, time}; statuses ledgered.
  - Image with the S0 guest DLLs (`sh eac-vm/image/guest-stages.sh dxmt d3d`, `sh eac-vm/builder.sh sync eac-vm/image
    mn-image`, `sh eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`), then `MN_REF_FROZEN=$S/ref-vm
    MN_DXMT_ONLY=d3d11_dxgi sh eac-vm/dxmt/gate.sh`. Expected: ok; a family
    that differs leaves the invocation's prefixes, ledgered with its lines (step 1b's rule; `.guest64` pins it).
  - `sh eac-vm/tests/dxgi-guest.sh i386 record` twice → `d3d11_dxgi.i386` (identical runs), then `sh
    eac-vm/tests/dxgi-guest.sh i386` → `dxgi-guest i386: ok`; the same with `x86_64` → `d3d11_dxgi.guest64`.
  - `sh eac-vm/dxmt-native/build-tests.sh` → `tests: ok` (the probe vs `.native`); `sh
    eac-vm/dxmt-native/tests/build.sh` → `build: ok`; `sh eac-vm/dxmt/check-build.sh` → `check-build: ok`.
  - `sh eac-vm/dxmt/present-gate.sh` once; its `present-ref` → `$S/ref-present`.
- [x] **Step 6: Mutation REDs and bites**, each restored: a value in `.fixed` (the check.sh row FAILs naming
  the line, run by hand), in `.native` (`build-tests.sh` FAILs), in `.i386` and `.guest64` (`dxgi-guest.sh` FAILs); a
  wrong expected line in each d3d10 row; a nonexistent `_CreateDXGIFactory9` added to `tests/build.sh`'s list (FAIL);
  ordinal 17 expected as 18 in `check-build.sh` (FAIL). `--bites` (P21) once per Mac lane on `$S/ref-wine.app`:
  every slot `yes` or excluded with its reason; a `NO` gets a case through Task 3's owner before the goldens are
  committed (and the goldens re-recorded).
- [x] **Step 7: Commit** (MacNeutron): `dxmt tests: dxgi and d3d10core goldens from the C++, their rows`.

### Task 4R: onto step R's layout — both rebases, the lane kit, the deferred references (controller, then owner)

The first task of the remaining work. Step R's format task replaced this plan's (D24), so no format task follows.

**Files:** DXMTRS: `step3-s0`'s three commits rebased onto 1ccec40 as `step3-dxgi` (no content change). MacNeutron:
`step3-probe` rebased onto `dxmtrs` ce1a56f (conflict resolutions only). Uncommitted (`$S`): `cfg.sh`, `lane-dll.sh`,
`lane-dylib.sh`, `lane-app.sh`, `i686-ret.sh`; `ref-perf/check-{x64,arm64ec}.txt`; `ref-present` (re-recorded on S0);
`ref-probe/rate-{arm64ec,native}.txt` (quiet machine).

**Interfaces (the kit, for step R's layout).** Common rules: `<tree>` is a DXMTRS worktree (a lane's `$S/wt-<lane>`
or `$S/wt-s0`), never `$D` (pipeline rule 1: `$D` is read only inside lock jobs); the scripts that run cargo take
`CARGO_TARGET_DIR` from the environment and refuse unless it is absolute and outside `<tree>` (one `${…:?}` line and
one `case`, as `tools/linux-cargo.sh`'s guards; R-D7: no build output in a DXMTRS tree; lanes pass `$S/cargo-<lane>`);
cargo runs only through `<tree>/tools/cargo-staticlib.sh "$MTLD3D_RUST" …` (`MTLD3D_RUST` from `<M>/dxmt/pins`,
`PATH=<M>/build/mtld3d-tools/bin:$PATH` for rustup's proxy), the command meson runs, so the kit restates no cargo flag;
meson and ninja come from `PATH="$(sh <M>/dxmt/mtld3d-tools.sh):$(sh <M>/dxmt/toolchain.sh):$PATH"` (step R's form for
meson outside MacNeutron's scripts; the main checkout's `toolchain.sh`, never a worktree's, rule 6).
- `$S/cfg.sh <tree> <out> [--pe <x86_64|i686|arm64ec>] [-D…]`: `meson setup <out> <tree>` only (configure, no
  compile), every path absolute; then `ninja -C <out> -t commands <target>` shows the cargo and link lines. Natively
  (default) with `eac-vm/dxmt-native/build.sh:20-21`'s and `:40-41`'s options (`--buildtype release
  -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false -Dwine_builtin_dll=false
  -Dnative_llvm_path=<M>/build/wine-arm64-src/llvm-arm64 -Dtranslator_key_file=<M>/build/wine-arm64-src/dxmt-translator-key
  --native-file <M>/build/dxmt-native/xcrun.txt`); `--pe x86_64` and `--pe i686` with `eac-vm/dxmt/build.sh:40-42`'s
  and `:79-81`'s (`<tree>/build-win64.txt` or `build-win32.txt`, `--buildtype release --strip`, `-Dguest_vm=true
  -Dwine_builtin_dll=true -Denable_d3d12=true`, `-Denable_nvapi`/`-Denable_nvngx` true on x86_64 and false on i686,
  `-Dwine_install_path=<M>/build/eac-vm-work/guest-wine-sdk`, the key file); `--pe arm64ec` with
  `wine-arm64/build.sh:522-527`'s (`<tree>/build-arm64ec.txt`, `<M>/build/wine-arm64-src/dxmt-xcrun.txt` and
  `dxmt-cpu.txt`, `--buildtype release --strip`, `-Dwine_builtin_dll=false -Denable_d3d12=true -Denable_nvapi=true
  -Denable_nvngx=true -Dnative_llvm_path=… -Dwine_build_path=<M>/build/wine-arm64-src/wine-build`, the key file). Since
  R every PE configure also sets up `src/d3d9` (step R's P6/P7: llvm-mingw's root from the cross compiler, a
  `libwinecrt0.a` under the Wine build tree or the guest SDK, else `error()`), so the `--pe` forms need those inputs in
  place. Control: `cfg.sh $S/wt-s0 $S/cfg-check` and the three `--pe` forms on `$S/wt-s0` (into
  `$S/cfg-check-<arch>`) succeed, and `ninja -C $S/cfg-check-x86_64 -t commands src/nvngx/nvngx.dll` shows
  `cargo-staticlib.sh … nvngx …` and the link.
- `$S/lane-dll.sh <tree> <crate> <x86_64|i686|arm64ec> <out.dll>`: `cfg.sh <tree> $CARGO_TARGET_DIR/cfg-<arch> --pe
  <arch>`; the crate's staticlib for each Rust target of that build (root `meson.build:223-229`: x86_64 →
  `x86_64-pc-windows-msvc`, i686 → `i686-pc-windows-msvc`, arm64ec → `aarch64-` and `arm64ec-pc-windows-msvc`) by
  `<tree>/tools/cargo-staticlib.sh "$MTLD3D_RUST" <t> <crate> "$CARGO_TARGET_DIR" <the file meson's custom_target
  names, in the cfg folder>` (`production`); then the DLL's link line (the last line of `ninja -t commands
  src/<dir>/<dll>.dll`: `d3d10core` → `src/d3d10/d3d10core.dll`, `dxgi` → `src/dxgi/dxgi.dll`, `nvngx` →
  `src/nvngx/nvngx.dll`, `winemetal-pe` → `src/winemetal/winemetal.dll`) replayed in the cfg folder with `-o
  <out.dll>`, the other inputs meson would build (import libraries `*.dll.a`, `version.o`, `.._util_rust_pe_rt.c.obj`)
  copied into place from `$S/ref-build/<arch>/` first (never from the live build dirs, which
  `eac-vm/dxmt/build.sh:55-57, :84-86` delete on any HEAD change). It prints the replayed line. Control:
  `lane-dll.sh $S/wt-s0 nvngx x86_64 $S/kit-check/nvngx.dll`'s export table equals
  `$S/ref-guest/x86_64-windows/nvngx.dll`'s and its line holds `rust_pe_rt.c.obj`, `-lntdll` and `nvngx.def`;
  `lane-dll.sh $S/wt-s0 winemetal-pe i686 …`'s line holds the SDK's `libntdll.a`.
- `$S/lane-dylib.sh <tree> <out-dir> <crate>… [--ported]`: for each crate named (`dxgi`, `d3d10core`) its
  `aarch64-apple-darwin` staticlib by `<tree>/tools/cargo-staticlib.sh "$MTLD3D_RUST" aarch64-apple-darwin <crate>
  "$CARGO_TARGET_DIR/da" <out-dir>/<crate>.a production` with `CARGO_PROFILE_PRODUCTION_DEBUG_ASSERTIONS=true` in its
  environment (so a misaligned `ptr::read`/`write` or `*p` panics and aborts; a target folder of its own, so
  `lane-dll.sh`'s and meson's production builds stay as they are), then `clang -dynamiclib -install_name
  @rpath/<n>.dylib` with P9's flags against `$S/ref-native/lib`'s `winemetal.dylib`/`d3d11.dylib`, the other
  `$S/ref-native/lib` dylibs copied beside them; runs `DYLD_LIBRARY_PATH=<out-dir> $S/probe-native/d3d11_dxgi fixed
  [--ported]` and prints the first line differing from `$S/wt-mn-probe/dxmt/tests/d3d11_dxgi.native` (`NOT_ON_C`
  lines, named by `--list`, left out of the comparison), or `same`; with `--ported` it then prints the `NOT_ON_C`
  lines for the P20 check. Control: with no crate named, `same`.
- `$S/lane-app.sh <out.app> <dll>…` (P1): `cp -cR $S/ref-wine.app <out.app>`, the named DLLs replace their namesakes in
  `<out.app>`'s `Contents/Resources/DXMT/aarch64-windows`, then `codesign --force --sign "$MACNEUTRON_SIGN_IDENTITY"
  --preserve-metadata=identifier,entitlements,requirements,flags,runtime <out.app>`. Host-only (a copy, not the
  installed runtime). Control: an unmodified re-signed copy passes `codesign --verify --strict`, installs through
  `$S/run.sh` and passes check.sh's nvapi row (Task 3's run.sh control on it). If the control fails, P1's fallback.
- `$S/i686-ret.sh <rust .lib> <crate>`: `ret $N` of each `<class>_<method>` and export in the Rust i686 library
  (`lane-dll.sh … i686`'s, `$CARGO_TARGET_DIR/i686-pc-windows-msvc/production/<crate>.lib`) against the C++'s demangled
  `<Class>::<Method>` in `$S/ref-build/i686/src/<dir>/<dll>.dll.p/*.obj` (`src/dxgi/dxgi.dll.p`,
  `src/d3d10/d3d10core.dll.p`); prints each mismatch, then `compared <n>`, and FAILs on any Rust symbol with no C++
  counterpart, else `same`. Control: a scratch `extern "system" fn adapter_set_video_memory_reservation(this, node:
  u32, group: u32, reservation: u32) -> i32` in a throwaway crate is reported (`ret $16` against the C++'s `ret $20`).

- [ ] **Step 1: DXMTRS rebase** (controller, host-only: no build, `$D` untouched). `git -C $S/wt-s0 rebase 1ccec40`
  (no conflict expected: `git diff da6ed2d 1ccec40` touches none of S0's seven files), then `git -C $S/wt-s0 branch
  -m step3-s0 step3-dxgi`. Expected: `git -C $S/wt-s0 log --oneline 1ccec40..` lists the three S0 commits;
  `git -C $S/wt-s0 diff 4fb43ea HEAD -- src/dxgi src/d3d10 src/util/config src/d3d11/d3d11_swapchain.cpp` is empty
  (the references stay the S0 C++'s, P17); fill `include/native/directx` and `external/nvapi` as the lane procedure
  does if absent. Ledger the three new shas. The three commits' Sonnet trailers are reworded to Opus here or at Task
  15's merge (ledger Minor).
- [ ] **Step 2: MacNeutron rebase** (controller, in `$S/wt-mn-probe`). `git -C $S/wt-mn-probe rebase dxmtrs`.
  Conflicts expected in `Makefile`, `eac-vm/dxmt-native/build-tests.sh` and `eac-vm/dxmt/check-build.sh` (step R's pin
  bump edited lines next to the probe's: adjacent hunks); each resolved by keeping both sides; `sh -n` on the two
  scripts. Expected: `git -C $S/wt-mn-probe diff --stat dxmtrs` lists exactly the probe's 17 files (`Makefile`,
  `dxmt/check.sh`, `dxmt/tests/d3d11_dxgi{.cpp,.fixed,.native,.i386,.guest64}`, the four `.inc`,
  `dxmt/tests/d3d12_fullscreen.cpp`, `eac-vm/dxmt-native/{build-tests.sh,tests/build.sh}`,
  `eac-vm/dxmt/{check-build.sh,invocations}`, `eac-vm/tests/dxgi-guest.sh`), each hunk the probe's own.
- [ ] **Step 3: The rebased trees build and their rows hold** (owner, ONE lock job; the job's first command `git -C $D
  checkout -q --detach <step3-dxgi's head>`; MacNeutron commands from `$S/wt-mn-probe` with `BUILD_DIR=<M>/build`
  exported, the signing variables exported): `make wine-arm64 dxmt-tests dxmt-tests-arm64ec`, `sh
  eac-vm/dxmt-native/build.sh`, `sh eac-vm/dxmt/build.sh`. Expected: "development build"; the key =
  `71a7e5f2…3311`; `sh eac-vm/dxmt-native/tests/build.sh` → `build: ok` (the five dxgi names, d3d10core's two); `sh
  eac-vm/dxmt-native/build-tests.sh` → `tests: ok` (the probe vs `.native`); `sh eac-vm/dxmt/check-build.sh` →
  `check-build: ok (19 files in x86_64-windows, 15 in i386-windows)`; `$S/rows.sh` on the built wine.app, both lanes →
  the five step-3 rows ok. The job logs the commit and wine.app's cdhash; `$D`'s `git status --porcelain` empty at the
  end.
- [ ] **Step 4: The kit** (owner, host-only). Write the five scripts above; each control as stated (the
  `lane-app.sh` control's run is a copy, no lock). A failing control is fixed before any lane starts (Task 5 waits);
  `lane-app.sh`'s may instead take P1's fallback, ledgered.
- [ ] **Step 5: The deferred references** (owner, lock jobs, each checking out step3-dxgi's head first as Step 3):
  the C++'s full check.sh once per lane, `MACNEUTRON_ARM64_APP=$S/ref-wine.app sh dxmt/check.sh` (from
  `$S/wt-mn-probe`) → `$S/ref-perf/check-x64.txt` and the ARM64EC form (Task 13) → `check-arm64ec.txt`: expected
  417 ok, 0 FAIL per lane (step R's 412 and the five new rows; flaky rows one re-run each, ledgered), the measured
  sets being what Task 13 compares; the image with Step 3's S0 guest DLLs (`sh eac-vm/image/guest-stages.sh dxmt
  d3d`, then the main checkout's `sh eac-vm/builder.sh sync eac-vm/image mn-image` and `sh eac-vm/builder.sh ssh 'sh
  ~/mn-image/build.sh wine'`), then `MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img sh
  eac-vm/dxmt/present-gate.sh` once, its `present-ref` → `$S/ref-present` (replacing step 2's copy; ledger both frame
  times); `rate` once on arm64ec and natively at a 1-min load average below 2 →
  `$S/ref-probe/rate-{arm64ec,native}.txt` (else the load ledgered, P16).

### Task 5 (S2): dxmt-util, nvapi's LUID, the empty crates, the gate's and audit's scope (lane `s2`, from `step3-dxgi`)

**Files (DXMTRS, step R's layout):**
- Modify `src/util/Cargo.toml` (feature `perf-build`), `src/util/src/lib.rs`, `src/util/src/com.rs`,
  `src/util/src/log.rs`; create `src/util/src/env.rs`, `src/util/src/env/tests.rs`, `src/util/src/com/tests.rs`,
  `src/util/src/tests.rs` (the `lib` test); extend `src/util/src/log/tests.rs`.
- Modify `src/nvapi/src/gpu.rs` (use `dxmt_util::luid_from_registry_id`).
- Create `src/dxgi/Cargo.toml`, `src/dxgi/src/lib.rs` (package `dxgi`) and `src/d3d10/Cargo.toml`,
  `src/d3d10/src/lib.rs` (package `d3d10core`): `crate-type = ["staticlib"]`, `version`/`edition`/`rust-version`/
  `license` `.workspace = true` (LGPL-2.1-or-later), `publish = false`, `[lints] workspace = true`, `perf-build =
  ["dxmt-util/perf-build"]`; dependencies declared now through `[workspace.dependencies]` (`dxgi`: `dxmt-util`,
  `winemetal-wire`; `d3d10core`: `dxmt-util`), so the lock is settled before any lane; `lib.rs` a crate doc only. Modify
  the root `Cargo.toml` (`members` gains `"src/d3d10"`, `"src/dxgi"`; not `default-members`, as `nvngx`/`nvapi`) and
  `Cargo.lock` (the two path packages only, written by cargo offline; MacNeutron's `cargo fetch --locked` needs them).
- Modify `tools/audit.sh`: `APP_POINTER_DIR` becomes a list, `src/d3d9/src src/dxgi/src src/d3d10/src`, at both of its
  uses (the `--file` mode's `case "$APP_POINTER_DIR"/*.rs)` and the tree mode's `git ls-files "$APP_POINTER_DIR/*.rs"`:
  a loop over the list at each; `APP_POINTER_EXEMPT` stays `src/d3d9/src/crash.rs`); `PE_CRATES` gains `src/dxgi
  src/d3d10`. `tools/rust-check.sh`: `PE_P` gains `-p dxgi -p d3d10core -p dxmt-util` and `PE64_P` drops its own
  `-p dxmt-util` (so i686 lints `dxmt-util` too, D20), `HOST_P` gains `-p dxgi -p d3d10core`, the
  `-undefined dynamic_lookup` test line gains `-p dxgi -p d3d10core`. `docs/CONVENTIONS.md`: the ABI-home list
  ("ABI constants have one home", `src/dxgi/src/abi.rs`) and the pointer rule's scope in its three places (the scope
  sentence at the top, the gate table's row, the section "An application's pointer promises no alignment"), each
  naming `src/d3d9/src`, `src/dxgi/src`, `src/d3d10/src`. `tools/derive_inventory.txt` (`Guid`, through `sh
  tools/audit.sh --update-derives`).

**Interfaces (produced):**
- `dxmt_util::com::Guid`: `#[derive(Clone, Copy, PartialEq, Eq)]`; `pub fn read(p: *const Guid) -> Option<Guid>`
  (null → None, `read_unaligned`); `impl Display` = `{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}×6` lowercase.
- `dxmt_util::log::Logger::{info, debug, trace}(&self, &str)`; with `perf-build` the three compile to nothing.
- `dxmt_util::env::{exe_path, exe_name, exe_base_name, cache_exe_name}() -> String`: PE `GetModuleFileNameW(NULL, buf,
  260)` truncated at its length; name after the last `\`; base without a case-insensitive `.exe`; cache name
  `DXMT_CACHE_EXE` or the name. Natively all `""`. `Logger`'s file name uses `exe_path()`.
- `dxmt_util::luid_from_registry_id(registry_id: u64) -> [u8; 8]` (= `swap_bytes().to_ne_bytes()`).

- [ ] **Step 1: Failing tests** (`--target aarch64-apple-darwin`): `com::tests::guid_display_is_com_guids_form`
  (IMTLDXGIAdapter's IID → `"acdf3ef1-b33a-4cb6-97bd-1c1974827e6d"`), `guid_read_takes_an_odd_address`,
  `guid_read_of_null_is_none`; `log::tests::native_file_name_has_an_empty_exe` (`file_name(Some("/t"), false, "",
  "dxgi.log") == Some("/t/_dxgi.log")`), `info_passes_at_info_and_debug_does_not`; `env::tests::base_name_strips_exe`
  (`"C:\\g\\A.EXE"` → `"A"`), `cache_name_prefers_dxmt_cache_exe`; `lib` test `luid_swaps_bytes`
  (`luid_from_registry_id(0x0102_0304_0506_0708) == 0x0807_0605_0403_0201_u64.to_ne_bytes()`). Run. Expected:
  FAIL (missing items).
- [ ] **Step 2: Implement; nvapi adopts the LUID rule; the two empty crates; the workspace, gate, audit and
  CONVENTIONS edits.**
- [ ] **Step 3: Checks.** Host tests `-p dxmt-util -p nvapi -p nvngx` pass; `cargo test … -p dxmt-util --features
  perf-build` passes with `info_passes…` gated off by `cfg`; msvc builds of `dxmt-util`, `nvapi`, `nvngx`, `dxgi`,
  `d3d10core` on the four targets; the perf-build builds of `dxgi` and `d3d10core`; `cargo metadata --offline
  --format-version 1 --no-deps` lists 18 workspace members; RC → `rust-check: ok`, its printed clippy lines naming
  `-p dxgi -p d3d10core` on every PE target and darwin and `-p dxmt-util` on i686, `audit: clean (694 files)` (688 at
  1ccec40 and Step 1-2's six `.rs`). REDs, each staged in the lane worktree and then removed, one per changed
  selector: `p.write(` on an app pointer in an `extern "system" fn` in a scratch `src/d3d10/src/x.rs` (the last entry
  of `APP_POINTER_DIR`) → the audit names it; a `.join()` in a scratch `src/dxgi/src/x.rs` → the audit's PE-side join
  rule names it (`PE_CRATES`); an unused binding under `#[cfg(target_arch = "x86")]` in `src/dxgi/src/lib.rs` → RC's
  clippy leg FAILs on `i686-pc-windows-msvc`.
- [ ] **Step 4: Commit:** `dxmt-util: what dxgi and d3d10core share; nvapi takes the LUID rule; empty crates`.

### Task 6 (S3): `d3d10core.dll` and `d3d10core.dylib` in Rust (lane `d3d10core`)

**Files (DXMTRS):** `src/d3d10/src/{lib.rs, abi.rs}` (+ `src/d3d10/src/abi/tests.rs`); modify
`src/d3d10/meson.build`; delete `src/d3d10/d3d10core.cpp`. Lane kit: `CARGO_TARGET_DIR=$S/cargo-d3d10core`.

**Interfaces:**
- Exports (`#[unsafe(no_mangle)] pub extern "system"`): `D3D10CoreCreateDevice(factory: *mut c_void, adapter:
  *mut c_void, flags: u32, feature_level: u32, device: *mut *mut c_void) -> i32` (i686 `_…@20`),
  `D3D10CoreRegisterLayers() -> i32` (`@0`). No `DllMain`.
- Import: `unsafe extern "system" { fn D3D11CoreCreateDevice(*mut c_void, *mut c_void, u32, *const u32, u32,
  *mut *mut c_void) -> i32; }` (i686 `_…@24`; PE through `d3d11_dep`'s import library, natively `d3d11.dylib`).
- `abi.rs` (P12): `IID_ID3D10_DEVICE` (`9b7e4c0f-342c-4106-a19f-4f2704f689f0`), `IID_ID3D10_MULTITHREAD`
  (`9b7e4e00-342c-4106-a19f-4f2704f689f0`); partial vtables
  `AdapterCheckVtbl` (slot 9 `check_interface_support(*mut c_void, *const Guid, *mut i64) -> i32`),
  `MultithreadVtbl` (slot 5 `set_multithread_protected(*mut c_void, i32) -> i32`); `IUnknown` from `dxmt_util::com`.
  Slot offsets asserted.
- `static LOG: Logger = Logger::new("d3d10core.log")`.
- meson: nvngx's template (`custom_target('d3d10core-' + t)` per `rust_pe_targets`, `cargo_staticlib` with
  `production` and, when `'-DDXMT_PERF_BUILD' in get_option('cpp_args')`, `perf-build`), `shared_library('d3d10core',
  d3d10core_res, …, dependencies: [rust_runtime_dep, d3d11_dep], link_with: d3d10core_rust, vs_module_defs:
  'd3d10core.def')`, natively P9's flags for the two names, the builtin postprocess unchanged.

Behaviour (`d3d10core.cpp:15-44`, the six calls of critic A7): null `device` written null first when non-null;
`CheckInterfaceSupport(ID3D10Device, NULL)` on the adapter (its failure returned); `D3D11CoreCreateDevice(factory,
adapter, flags | 0x80000000, &feature_level, 1, &d3d11)`; QI `ID3D10Multithread` — a failure skips the next call (D7);
`SetMultithreadProtected(flags & 1 == 0)`; Release it; QI `ID3D10Device` into `device` (a null `device` reaches d3d11's
QI, which answers E_POINTER); Release the D3D11 device. Null `adapter` → E_INVALIDARG (D7). `D3D10CoreRegisterLayers`:
`LOG.err("D3D10CoreRegisterLayers: stub")`, E_NOTIMPL.

Hazards: the first Rust stdcall import through a meson import library (`--enable-stdcall-fixup`); the first Rust
stdcall exports undecorated by `--kill-at` (S4 relies on this proof); ARM64EC exit thunks for the app adapter's slot 9
and d3d11's QI/Release; app `adapter` and `device` pointers unaligned-safe (only `vtbl`, `write_unaligned`). Statics:
`LOG` only.

Pinned by: the `d3d10c` family (`.fixed`, `.native`, `.i386`, `.guest64`), the `d3d10-core`/`d3d10-wine` rows, the
forwarder gate's `d3d10_core`/`d3d10_wine`, d3d-smoke `32: conv`.

- [ ] **Step 1: RED.** Exports that return `E_NOTIMPL`, `abi.rs` with its slot asserts; `sh $S/lane-dylib.sh
  $S/wt-d3d10core $S/ld-d3d10core d3d10core`. Expected: the first differing line is a `d3d10c` line; no `dxgi*` line differs.
- [ ] **Step 2: GREEN.** The behaviour above. Expected: `lane-dylib.sh` prints `same`; `lane-dylib.sh … d3d10core
  --ported` prints `same` and the null-adapter line `80070057` (P20).
- [ ] **Step 3: Meson and deletion.** Edit `src/d3d10/meson.build`, delete `d3d10core.cpp`. Configure-only checks:
  `sh $S/cfg.sh $S/wt-d3d10core $S/cfg-d3d10core` succeeds and `ninja -C $S/cfg-d3d10core -t commands
  src/d3d10/d3d10core.dylib` shows `cargo-staticlib.sh … d3d10core … production` and
  `-Wl,-u,_D3D10CoreCreateDevice`; a second (`cfg.sh … -Dcpp_args=-DDXMT_PERF_BUILD`) shows `perf-build`; `cfg.sh
  … --pe <arch>` for `arm64ec`, `x86_64`, `i686` succeeds and `ninja -t commands src/d3d10/d3d10core.dll` shows the
  crate's staticlib(s), `rust_pe_rt.c.obj`, `-lntdll`, `d3d11`'s import library, the `.def` and the version
  resource (the perf-build form once, on `i686`).
- [ ] **Step 4: PE tables.** `sh $S/lane-dll.sh $S/wt-d3d10core d3d10core <arch> $S/ld-d3d10core/<arch>.dll` for
  `arm64ec`, `x86_64`, `i686` (replaying Step 3's meson lines). Expected, each: `llvm-readobj --coff-exports` =
  `D3D10CoreCreateDevice`, `D3D10CoreRegisterLayers` (no `@`, no `_`); `--coff-imports` has `D3D11.DLL:
  D3D11CoreCreateDevice` and nothing from `dxgi.dll`; the DLL set per P15. `sh $S/i686-ret.sh
  $S/cargo-d3d10core/i686-pc-windows-msvc/production/d3d10core.lib d3d10core` → `compared 2`, `same` (`ret $20`,
  `ret`).
- [ ] **Step 5: Host checks** (Global Constraints: the host tests, msvc and perf-build builds, RC → `rust-check: ok`
  with `audit: clean (<n> files)`, `<n>` grown by the task's new `.rs`).
- [ ] **Step 6: Commit:** `d3d10core: in Rust (d3d10core.dll and .dylib)`.

### Task 7 (S4-T1): dxgi's ABI, COM plumbing and module skeleton (lane `t1`)

**Files (DXMTRS, `src/dxgi/src/`):** `lib.rs` (the module list), `abi.rs` (+ `abi/tests.rs`), `com.rs` (+
`com/tests.rs`), `private_data.rs` (+ `private_data/tests.rs`), `ffi.rs`, `wsi.rs` (final: dispatch to
`#[cfg(windows)] mod win32;` / `#[cfg(not(windows))] mod hooks;`), and stubs with final signatures:
`exports.rs`, `nvext.rs`, `factory.rs`, `adapter.rs`, `kmt.rs`, `config.rs`, `output.rs`, `modes.rs`, `wsi/win32.rs`,
`wsi/hooks.rs`. Uncommitted: `$S/layout/dump.sh`, its header-only translation units and its five dumps.

**Interfaces (produced; Tasks 8-11 consume exactly these):**
- `abi.rs` (D8): `pub type Wchar = u16` (`cfg(windows)`) / `u32`; `pub type Hwnd = *mut c_void`, `pub type Hmonitor =
  *mut c_void`; HRESULT constants `S_OK`, `S_FALSE`, `E_FAIL`,
  `E_NOTIMPL`, `E_NOINTERFACE`, `E_POINTER`, `E_INVALIDARG`, `DXGI_ERROR_{INVALID_CALL, NOT_FOUND, MORE_DATA,
  UNSUPPORTED, ALREADY_EXISTS}`; the IIDs (IUnknown, IDXGIObject, IDXGIFactory..7, IDXGIAdapter..4, IDXGIOutput..6,
  IDXGIDevice, ID3D10Device, ID3D10Device1, IMTLDXGIAdapter, IMTLDXGIOutput, IMTLSwapChainFactory, IMTLDXGIDevice,
  Wine's adapter IID, `DXMT_NVEXT_GUID`); `FactoryVtbl` (30 slots), `AdapterVtbl` (21: `get_mtl_device: extern
  "system" fn(*mut c_void) -> u64` at 19, `get_local_d3dkmt -> u32` at 20), `OutputVtbl` (30: `get_gamma_ramp:
  extern "system" fn(*mut c_void) -> *const GammaRamp` at 29), `SwapChainFactoryVtbl` (slot 3 `create_swap_chain:
  unsafe extern "system-unwind" fn(*mut c_void, *mut c_void, Hwnd, *const SwapChainDesc1, *const
  SwapChainFullscreenDesc, *mut *mut c_void) -> i32`); structs `AdapterDesc`, `AdapterDesc1..3`, `OutputDesc`,
  `OutputDesc1`, `ModeDesc`, `ModeDesc1`, `Rational`, `SampleDesc`, `SwapChainDesc`, `SwapChainDesc1`,
  `SwapChainFullscreenDesc`, `GammaControl`, `GammaControlCapabilities`, `Rgb`, `QueryVideoMemoryInfo`, `Luid`,
  `Rect`, `GammaRamp` (`#[repr(C)] { red, green, blue: [AtomicU32; 1024], version: AtomicU64 }`: size 12296, align 8,
  `version` at 12288); the Win32 and D3DKMT mirrors the lanes call with (`cfg(windows)`): `DevModeW` (220 bytes, its
  unions as the SDK lays them, `dm_size`), `MonitorInfoExW`, `Point` (by value to `MonitorFromPoint`), `Hkey`,
  `D3dkmtOpenAdapterFromLuid`, `D3dkmtCloseAdapter`, `pub type MonitorEnumProc = extern "system" fn(Hmonitor, *mut
  c_void, *mut Rect, isize) -> i32`, and the constants `MONITOR_DEFAULTTOPRIMARY`, `DM_INTERLACED`,
  `HKEY_LOCAL_MACHINE`, `KEY_ALL_ACCESS`, `REG_DWORD`, `REG_SZ`, `DLL_PROCESS_ATTACH` (`ENUM_*_SETTINGS` stay
  `wsi`'s, below); natively (`cfg(not(windows))`) `WsiWindow`, `WsiDisplay`, `WsiHookMode` and `WsiHooks` (the
  four function pointers) mirroring `winemetal.h:2329-2352`; `transcode_into(src: &[u8], dst: &mut [Wchar]) ->
  usize` with `str::transcodeString`'s rules (`util_string.cpp:17-58` decode: a bad lead or stray continuation byte is
  `?`; `:142-190` encode: a character that doesn't fit is dropped and a later one may fill the unit; stops at a NUL;
  never writes past `dst`).
- `com.rs`: `#[repr(C)] pub struct Head<V: 'static> { vtbl: &'static V, refs: AtomicU32 }` (`Head::new` at 1);
  `pub fn add_ref<T>(this: *mut c_void) -> u32`; `pub fn release<T>(this: *mut c_void) -> u32` (`Box::<T>::from_raw` on
  1→0 after an Acquire fence; past zero → 0, P11); `pub fn query_interface(this: *mut c_void, riid: *const Guid, out:
  *mut *mut c_void, answered: &[Guid], silent: &[Guid], log_as: (&Guid, &str), add_ref: extern "system" fn(*mut
  c_void) -> u32) -> i32` (null `out` → E_POINTER; match → `this` + AddRef; else null + E_NOINTERFACE and, unless
  silent, `"<name>: Unknown interface query <guid>"` at warn once per pair through `qi_log`); `pub fn qi_log(object:
  &Guid, riid: &Guid) -> bool` (`QI_LOGGED`, level gate as `com_guid.cpp:48-54`); `pub fn write_out<P>(out: *mut P,
  value: P)` (unaligned; caller null-checks). Calls on app and C++ objects use `dxmt_util::com::{query, release,
  vtbl}` with partial vtables (nvngx's pattern).
- `private_data.rs`: `pub struct PrivateData` with `set_data(&self, guid: Guid, size: u32, data: *const c_void) ->
  i32`, `set_interface(&self, guid: Guid, unknown: *mut c_void) -> i32`, `get_data(&self, guid: Guid, size: *mut u32,
  data: *mut c_void) -> i32` — `com_private_data.cpp:98-148`'s codes with DXGI's (D6), app AddRef/Release after the
  lock is dropped: an interface entry is Released when replaced, when erased (`set_data(guid, _, null)`) and in `impl
  Drop for PrivateData` (entries drained under the lock, Released after it); `get_data` AddRefs an interface entry
  before writing it; `*size` = the entry's size whenever `data` is non-null, MORE_DATA included.
- `ffi.rs`: `unsafe extern "C"` for `NSObject_retain`, `NSObject_release`, `NSArray_object`, `NSArray_count`,
  `WMTCopyAllDevices`, `MTLDevice_recommendedMaxWorkingSetSize`, `MTLDevice_currentAllocatedSize`, `MTLDevice_name`,
  `NSString_getCString(u64, *mut u8, u64, encoding: u64) -> u32`, `MTLDevice_hasUnifiedMemory -> bool`,
  `WMTGetPrimaryDisplayId -> u32`, `WMTGetSecondaryDisplayId -> u32`, `WMTGetDisplayDescription(u32,
  *mut winemetal_wire::WMTDisplayDescription)`, `MTLDevice_registryID -> u64`, `WMTSetMetalShaderCachePath(*const
  c_char) -> bool` (`cfg(windows)`), `WMTGetWsiHooks() -> *const abi::WsiHooks` (`cfg(not(windows))`).
- Stub signatures (bodies `E_NOTIMPL`/null/`None`/empty):
  - `factory::create(flags: u32) -> *mut c_void`;
  - `adapter::create(device: u64, factory: *mut c_void) -> *mut c_void` (takes a device reference and a public one on
    the factory);
  - `output::create(adapter: *mut c_void, monitor: Hmonitor, force_sdr: bool) -> *mut c_void`;
  - `nvext::enabled() -> bool`; `kmt::open(luid: Luid) -> u32`; `kmt::close(handle: u32)`;
  - `config::CONFIG: LazyLock<Config>`; `config::Options { vendor_id: Option<u16>, device_id: Option<u16>,
    device_desc: Option<Vec<u8>>, force_sdr: bool }` (the description's bytes as the file or variable gave them, as
    the C++'s `std::string`), `Options::from_config(&Config) -> Options`;
  - `modes::{monitor_format_bpp(format: u32) -> u32, is_back_buffer_format(format: u32) -> bool, mode_list(raw:
    &[WsiMode], format: u32, flags: u32) -> Vec<ModeDesc1>, find_closest(modes: Vec<ModeDesc1>, target: &ModeDesc1,
    current: &ModeDesc1) -> Option<ModeDesc1>, refresh_from_hz(hz: u32) -> Rational}`;
  - `wsi::{enum_monitors(u32) -> Option<Hmonitor>, default_monitor() -> Option<Hmonitor>, desktop_coordinates(Hmonitor)
    -> Option<Rect>, display_name(Hmonitor) -> Option<[Wchar; 32]>, display_mode(Hmonitor, u32) -> Option<WsiMode>,
    current_display_mode(Hmonitor) -> Option<WsiMode>, window_size(Hwnd) -> (u32, u32)}` with `pub struct WsiMode {
    width: u32, height: u32, refresh: Rational, bits_per_pixel: u32, interlaced: bool }` and `pub const
    ENUM_CURRENT_SETTINGS: u32 = 0xFFFF_FFFF`, `ENUM_REGISTRY_SETTINGS: u32 = 0xFFFF_FFFE`.

- [ ] **Step 1: The layout dumps** (step 2's `tools/winemetal-layouts.sh` method: PE programs can't run on a lane
  host). `$S/layout/dump.sh` runs `clang -Xclang -fdump-record-layouts -fsyntax-only` over header-only translation
  units naming every `abi.rs` struct: llvm-mingw's clang with `-ffreestanding` and its sysroot for `i686-`,
  `x86_64-`, `aarch64-`, `arm64ec-w64-mingw32`; Apple clang against the lane worktree's
  `$S/wt-t1/include/native/{d3d12,windows,directx}` (`directx` filled by the lane procedure) and
  `$S/wt-t1/src/winemetal/winemetal.h`, never `$D`'s (pipeline rule 1), and `-DWIDL_EXPLICIT_AGGREGATE_RETURNS` for
  arm64 macOS (critic A6). IIDs are read from the headers' text (`MIDL_INTERFACE`/`DEFINE_GUID` strings; the private
  ones from `src/dxgi/dxgi_interfaces.h` and Task 2's IID). Expected: five dumps; DXGI layouts agree between the
  sysroot and `include/native` where the targets agree; `DEVMODEW` 220 bytes on every PE target.
- [ ] **Step 2: Failing tests.** `abi::tests` per target (`cfg(target_arch, target_os)`): every size and offset of
  Step 1's dumps (the Win32, D3DKMT and `WMTWsi*` mirrors included), IIDs equal to the headers', vtable slot offsets
  (`get_mtl_device` at 19 pointer widths, `get_gamma_ramp` 29, `create_swap_chain` 3);
  `abi::tests::{transcode_fits, transcode_stops_at_the_last_unit, transcode_drops_a_pair_that_does_not_fit,
  transcode_bad_utf8_is_a_question_mark}` (the `configu`/`confbad` inputs);
  `com::tests::{starts_at_one, release_frees_once_on_the_last, release_past_zero_answers_zero,
  eight_threads_balance}` (a counting `Drop`); `private_data::tests::{too_small_writes_nothing_but_the_size,
  big_buffer_shrinks_the_size, null_data_returns_size, null_interface_is_stored, replace_adds_before_it_releases,
  missing_is_not_found_size_zero, erase_then_s_false, erase_releases_interface, get_adds_a_reference,
  drop_releases_interfaces, two_threads_set_get_balance}` (a fake IUnknown with a call log and count). Expected: FAIL.
- [ ] **Step 3: GREEN** (`abi.rs`, `com.rs`, `private_data.rs`, `ffi.rs`, `wsi.rs`, stubs). The size/offset asserts
  are `const _: () = assert!(…)` per target cfg in `abi.rs`; the tests above check the rest.
- [ ] **Step 4: Native RED for the S4 merge.** `sh $S/lane-dylib.sh $S/wt-t1 $S/ld-t1 dxgi` with the stub crate. Expected:
  the first differing line is a `dxgif` line (stubs answer E_NOTIMPL); ledger it.
- [ ] **Step 5: Host checks**, the four msvc builds (the `abi.rs` asserts compile per target), the perf-build
  builds, `sh tools/audit.sh --update-derives` (the `abi.rs` derives), RC.
- [ ] **Step 6: Commit:** `dxgi: its ABI, COM plumbing and module skeleton (stubs)`.

### Task 8 (S4-T2): exports, `DllMain`, NVEXT and the factory (lane `t2`)

**Files:** `src/dxgi/src/{exports.rs, nvext.rs, factory.rs}` (+ `factory/tests.rs`, `nvext/tests.rs`). C++
replaced (deleted in Task 12): `src/dxgi/dxgi.cpp`, `src/dxgi/dxgi_factory.cpp`.

**Interfaces:**
- Exports (`#[unsafe(no_mangle)] pub extern "system"`, every target): `CreateDXGIFactory(riid: *const Guid, out: *mut
  *mut c_void) -> i32` (`@8`), `CreateDXGIFactory1` (`@8`), `CreateDXGIFactory2(flags: u32, riid, out)` (`@12`),
  `DXGIDeclareAdapterRemovalSupport() -> i32` (`@0`), `DXGIGetDebugInterface1(flags: u32, riid: *const Guid, out: *mut
  *mut c_void) -> i32` (`@12`); `cfg(windows)`: `#[unsafe(export_name = "DllMain")] pub extern "system" fn
  dll_main(instance: *mut c_void, reason: u32, reserved: *mut c_void) -> i32` (`@12`; winemetal-pe's form,
  `src/winemetal/src/transport.rs:25-26`, which V7 showed yields `_DllMain@12`).
- `nvext::enabled() -> bool`: `NVEXT.call_once(init)` then `NVEXT_ON`. PE `init` (`dxgi.cpp:16-51`, critic A9): i686
  returns at once; three `RegCreateKeyExW(HKLM, …, KEY_ALL_ACCESS)`, any failure → off, no log; then
  `DXMT_ENABLE_NVEXT == "1"` sets the three values (results ignored), logs `Vendor extension enabled: NVEXT` at info and
  turns it on, else deletes them. Native `init`: the variable alone.
- `factory::create(flags) -> *mut c_void` (count 1); `FACTORY_VTBL`; `fn adapter_order(unified: &[bool]) -> Vec<usize>`
  (`dxgi_factory.cpp:182-198`, pure).

Slots: factory 0-29 (map 1 §1.1): QI (answered IUnknown, IDXGIObject, IDXGIFactory..6; logged as IDXGIFactory2),
AddRef, Release, private data ×3 (`PrivateData`), `GetParent`, `EnumAdapters` (→ `EnumAdapters1`),
`MakeWindowAssociation`, `GetWindowAssociation` (an `AtomicUsize`), `CreateSwapChain`, `CreateSoftwareAdapter`,
`EnumAdapters1` (`WMTCopyAllDevices` each call, `adapter_order`, `adapter::create`, the copy released), `IsCurrent`,
`IsWindowedStereoEnabled`, `CreateSwapChainForHwnd` (`dxgi_factory.cpp:102-139`: null checks, QI
`IMTLSwapChainFactory`, `wsi::window_size` for a 0 dimension, the default fullscreen desc, slot 3 with `this`, the
QI'd pointer Released after), the stubs, `GetCreationFlags`, `EnumAdapterByLuid`, `EnumWarpAdapter`,
`CheckFeatureSupport` (null data → E_INVALIDARG, D7), `EnumAdapterByGpuPreference` (no out-pointer nulling: past the
last it returns NOT_FOUND with `*pp` untouched). P20's results: `EnumAdapters1(i < count, NULL)` →
`DXGI_ERROR_INVALID_CALL` before any adapter is created; a null `riid` → `E_POINTER`. `DllMain` (P10, D13): reason 1
only; `DisableThreadLibraryCalls`; unless `DXMT_USE_DEFAULT_METAL_CACHE` is `1`,
`WMTSetMetalShaderCachePath("dxmt/<cache exe name>/com.apple.metal")` and on false `Failed to set Metal cache path,
fallback to system default` at info; then `nvext::enabled()`. `CreateDXGIFactory2` = create, QI, Release; the others
call it with flags 0. VendorId's NVEXT branch reads `nvext::enabled()` (Task 9).

Hazards: `IMTLSwapChainFactory` lives at a non-primary base of d3d11's device (critic B13): never compare it with the
device or keep it past its Release; slot 3 is `system-unwind` (D13); the C++ swap chain AddRefs/Releases the factory
from other threads (atomic); ARM64EC exit thunks on the app device's QI and slot 3; i686 `@N` per export, `--kill-at`
maps them (Task 6's proof); `DXGIGetDebugInterface1` never writes `out` (`dxgi.cpp:88-104`); nothing on the `DllMain`
or `DXGIGetDebugInterface1` path takes a `Mutex` or `LazyLock` but the `Once`'s work (Review Focus 4); P19.
Statics: `NVEXT`, `NVEXT_ON`, `DECLARED`.

Pinned by: the `dxgif` family and its `dxgil` lines (every golden), check.sh's DLSS vendor and nvapi rows, the
replay cache row, d3d-smoke `rawexport`, `warp`, `eventthread`.

- [ ] **Step 1: Failing tests:** `factory::tests::{one_gpu_is_index_zero, discrete_last_without_unified_memory_first}`
  (`adapter_order`); `nvext::tests::native_reads_the_variable_once` (native cfg); host tests over `FACTORY_VTBL`
  (`factory::create` calls no winemetal; a fake device answering `IMTLSwapChainFactory` with a counting slot 3):
  `factory::tests::{qi_answers_factory_one_to_six, qi_refuses_factory_seven, qi_null_out_is_e_pointer,
  qi_null_riid_is_e_pointer, add_ref_release_count, get_parent_refuses, window_association_round_trip,
  tearing_is_supported, check_feature_null_data_is_invalid_arg, create_swap_chain_translates_the_desc,
  create_swap_chain_writes_out_on_failure, swap_chain_factory_released_after_the_call}` (descs with non-zero sizes,
  so no `wsi::window_size` call; `EnumAdapters1` and the 0×0 fill call winemetal or the window system and are pinned
  by Task 12's probes, P2's cost). Expected: FAIL.
- [ ] **Step 2: GREEN.** Host checks; msvc builds (the export names' `@N` show in `llvm-nm` of the i686 library:
  `_CreateDXGIFactory2@12`, `_DllMain@12`).
- [ ] **Step 3: Commit:** `dxgi: exports, DllMain, NVEXT and the factory in Rust`.

### Task 9 (S4-T3): the adapter, D3DKMT and config (lane `t3`)

**Files:** `src/dxgi/src/{adapter.rs, kmt.rs, config.rs}` (+ `adapter/tests.rs`, `config/tests.rs`). C++
replaced: `src/dxgi/dxgi_adapter.cpp`, `src/dxgi/dxgi_options.{hpp,cpp}`.

**Interfaces:**
- `adapter::create(device, factory) -> *mut c_void`: retains `device`, AddRefs the factory (public), opens D3DKMT
  with the LUID (`kmt::open`; failure → warn line, 0), `Options::from_config(&CONFIG)` (its `force_sdr` passed to each `output::create`);
  `ADAPTER_VTBL`. Drop: `kmt::close`, release the device, Release the factory.
- `fn desc3(facts: &DeviceFacts, options: &Options, nvext: bool) -> AdapterDesc3` (pure) with `DeviceFacts { name:
  Option<[Wchar; 128]>, recommended_max: u64, unified: bool, registry_id: u64 }` — Description: custom
  (`abi::transcode_into` of `device_desc`'s bytes into the first 127 units, the rest zero, as
  `dxgi_adapter.cpp:148-154`) else the device name (`NSString_getCString` into the 256-byte array with `WMTUTF16StringEncoding`
  (10) on PE, `WMTUTF32LittleEndianStringEncoding` (0x9c000100) natively; too long → zeroed); VendorId custom, else
  0x10DE with NVEXT, else 0x106B; DeviceId custom or 0; DedicatedVideoMemory `recommended_max`, halved when unified,
  i686 capped at `0xC0000000`; LUID `luid_from_registry_id`; Flags NONE; preemption DMA_BUFFER_BOUNDARY.
- `kmt.rs`: PE `D3DKMTOpenAdapterFromLuid`/`D3DKMTCloseAdapter` (gdi32, `extern "system"`); native: `util_d3dkmt.h:
  547-653`'s warn text and failure.
- `config.rs` (D5, critic B3): user file (`DXMT_CONFIG_FILE` or `dxmt.conf`; PE text mode: CRLF read as LF; native raw,
  `\r` whitespace outside quotes), then `DXMT_CONFIG` split on `;` with the file's `[exe]` context carried over;
  sections match `exe_name()` exactly; keys `[0-9A-Za-z._]`; quotes; the six built-in profiles matched
  case-insensitively as a suffix `\<name>.exe` of `exe_path()` (the first an alternation of three names); merge
  user-first; info lines `Found config file: <path>`, `Found config env: <value>`, `Found built-in config:` and
  `"  <key> = <value>"` in key order (a `BTreeMap`); hex4 parser (exactly 4 hex digits) and bool parser
  (`true`/`false`, case-insensitive). Lines and values are bytes, never UTF-8-checked (an invalid byte reaches
  `transcode_into` as the C++'s `std::string` does).

Slots: adapter 0-20 (map 1 §1.2): QI (IUnknown, IDXGIObject, IDXGIAdapter..4, IMTLDXGIAdapter; Wine's IID silent),
AddRef, Release, private data, `GetParent` (the factory's QI), `EnumOutputs` (`wsi::enum_monitors`, `output::create`),
`GetDesc`/`1`/`2`/`3` (from `desc3`; `Flags & 0b11` for `GetDesc1`), `CheckInterfaceSupport`, slot 12 (E_NOTIMPL + ERR
`Not implemented`, D7), 13 (no-op), `QueryVideoMemoryInfo`, `SetVideoMemoryReservation` (`[AtomicU64; 2]`), 16 (null →
E_INVALIDARG, D7), 17, `GetMTLDevice` (`device`), `GetLocalD3DKMT`. P20: a null `riid` → `E_POINTER` (QI,
`GetParent`), `E_INVALIDARG` (`CheckInterfaceSupport`, private data).

Hazards: slot 19's `u64` in x0/RAX/EDX:EAX with `retl $4` (V2); i686 `SetVideoMemoryReservation(this, u32, u32, u64)`
pops 20 and `QueryVideoMemoryInfo` 16; ARM64EC entry thunks (x64 games call `QueryVideoMemoryInfo` through
`IDXGIAdapter`, `identity.sh`); unaligned `QueryVideoMemoryInfo` out; `NSString_getCString`'s `u64` encoding argument;
`MTLDevice_hasUnifiedMemory`'s C `bool`. Static: `CONFIG`.

Pinned by: `dxgia` and its `dxgil` lines; the `config`, `configu`, `confbad`, `conffile`, `conffile2`, `nvext`,
`omori`, `genshin`, `emptysection` children; check.sh's nvapi LUID relation; d3d-smoke `32: conv` (`0xC0000000`);
`answers.cpp`, `wire.cpp`.

- [ ] **Step 1: Failing tests:** `adapter::tests::{vram_halves_on_unified, vram_caps_on_i686 (cfg pointer width 32),
  vendor_custom_beats_nvext, custom_desc_truncates_at_127, long_name_leaves_zeros}`; `config::tests::{
  section_matches_exactly, file_context_carries_into_env, crlf_is_lf_on_pe, cr_is_whitespace_natively,
  user_beats_profile, profile_suffix_ignores_case, genshin_alternation, hex4_exactly_four, bool_ignores_case,
  profile_lines_in_key_order, table_matches_config_cpp}` (the drift test, in `src/dxgi/src/config/tests.rs`:
  `include_str!` of `../../../../src/util/config/config.cpp`, i.e. the tree root's `src/util/config/config.cpp`, its
  six patterns and their keys equal the Rust table). Expected: FAIL.
- [ ] **Step 2: GREEN**; host checks; msvc builds.
- [ ] **Step 3: Commit:** `dxgi: the adapter, D3DKMT and config in Rust`.

### Task 10 (S4-T4): the output, modes and Win32 WSI (lane `t4`)

**Files:** `src/dxgi/src/{output.rs, modes.rs, wsi/win32.rs}` (+ `modes/tests.rs`, `output/tests.rs`). C++
replaced: `src/dxgi/dxgi_output.cpp` (the forwarder's `src/util/wsi_*_win32.cpp` copies stay).

**Interfaces:**
- `output::create(adapter, monitor, force_sdr) -> *mut c_void`: AddRefs the adapter (public); reads
  `WMTGetDisplayDescription(primary if monitor == default_monitor() else secondary)` once (`dxgi_output.cpp:140-151`);
  identity ramp `i/1023`, `version` 0; `OUTPUT_VTBL`. A `#[cfg(test)]` constructor takes a `WMTDisplayDescription`
  and a fake adapter (answering slot 19) instead of calling winemetal.
- `modes.rs` (pure; critic B10): bpp 32, or 64 for R16G16B16A16_FLOAT, unknown → warn `GetMonitorFormatBpp: Unknown
  format: <n>` and 32 (`dxgi_output.cpp:36-38`); the six back-buffer formats (V19), anything else 0 modes with S_OK; interlaced skipped unless
  asked; each mode's Format forced to the asked one; sort by width, height, integer refresh; the copy-out of
  `GetDisplayModeList1` (`dxgi_output.cpp:429-437`: at most `*pNumModes` entries written; MORE_DATA leaves the count,
  else the count is set and the tail untouched) as a pure helper; `find_closest(modes, target, current)`: DXVK's
  filter (`dxgi_output.cpp:57-136`) in `u64` arithmetic (on i686 the division is `_aulldiv`), `minDiffRefreshRate` a
  `u32` from a `u64` (empty list → `0xFFFFFFFF`), a zero denominator skipped and sorted last (D7), run twice
  (`:507-508`): first with `target`, then with a default mode built from `current` (`:461-492`: Stereo = target's;
  ScanlineOrdering and Scaling = current's where target's are UNSPECIFIED; Format = current's (R8G8B8A8_UNORM_SRGB)
  where target's is UNKNOWN; Width/Height = current's where target's Width is 0; RefreshRate = current's where
  target's numerator or denominator is 0; everything else zero/UNSPECIFIED); empty → `None`. `refresh_from_hz(hz)` =
  `{hz.wrapping_mul(1000), 1000}` (`wsi_monitor_win32.cpp:87`).
- `wsi/win32.rs`: `MonitorFromPoint({0,0}, PRIMARY)`, `EnumDisplayMonitors` with an `extern "system"` callback
  (critic B6) counting to the index, `GetMonitorInfoW` (`szDevice`, `rcMonitor`), `EnumDisplaySettingsW` (index,
  `ENUM_CURRENT_SETTINGS`, `ENUM_REGISTRY_SETTINGS`; refresh `modes::refresh_from_hz(dmDisplayFrequency)`, i.e.
  `{dmDisplayFrequency * 1000, 1000}`), `GetClientRect`, as `wsi_monitor_win32.cpp:24-124` and
  `wsi_window_win32.cpp:74-83`, with their error lines; every Win32 struct and constant from `abi.rs`.

Slots: output 0-29 (map 1 §1.3 plus 29): QI (IUnknown, IDXGIObject, IDXGIOutput..6, IMTLDXGIOutput), AddRef, Release,
private data, `GetParent`, `GetDesc`, `GetDisplayModeList` (via `…1` with a scratch list of `max(1, count)`),
`FindClosestMatchingMode`, `WaitForVBlank`, `Take/ReleaseOwnership`, gamma ×3 (per object, B9; points 0..1023 of
1025, point 1024 untouched; values stored Relaxed, then `version` incremented Release), the stubs, `GetDisplayModeList1`
(MORE_DATA without updating `*pNumModes`), `FindClosestMatchingMode1` (null mode or out, Format UNKNOWN with no
device, Width-xor-Height zero → `DXGI_ERROR_INVALID_CALL`, `:446-454`; no modes for the format → ERR `No modes found`
and NOT_FOUND, `:498-501`; filtered empty → NOT_FOUND silently; its debug line), `DuplicateOutput(1)`, the
overlay and composition no-ops, `GetDesc1` (forceSDR, EDR > 1 → 10 bpc G2084_P2020, primaries, MaxLuminance = potential
EDR × 100), `GetGammaRamp` (slot 29: the ramp's address). P20: a null `riid` → `E_POINTER` (QI, `GetParent`),
`E_INVALIDARG` (private data).

Hazards: the presenter reads the ramp on the Present thread without a lock (`dxmt_presenter.cpp:178-198`): the bytes
are `DXMTGammaRamp`'s, `version` last; slot 29 stays after `IDXGIOutput6`'s 29 slots (no destructor slots);
`MonitorEnumProc` reached through ARM64EC entry thunks from Wine's user32; unaligned mode buffers and gamma structs;
i686 `FindClosestMatchingMode1` pops 16; `DeviceName` is `[Wchar; 32]` (UTF-32 natively, Task 11's names). Statics:
none (the ramp lives on its output).

Pinned by: `dxgio` and its `dxgil` lines; Task 2's gamma rows; check.sh's `d3d12_fullscreen` rows; d3d-smoke `wmsize`
(both widths); `present_loop cycles=20`.

- [ ] **Step 1: Failing tests:** `modes::tests::{fp16_needs_64_bpp, unknown_format_has_no_modes,
  non_back_buffer_format_has_no_modes, interlaced_only_when_asked, sorted_by_width_height_rate,
  closest_prefers_resolution_then_rate, empty_rate_diff_is_u32_max, zero_denominator_sorts_last_and_is_skipped,
  unknown_format_takes_srgb, zero_size_takes_the_current_size, zero_size_at_61_keeps_the_current_size,
  stereo_finds_nothing, progressive_scanline_filters, win32_refresh_is_milli, short_buffer_is_more_data_count_kept,
  big_buffer_sets_the_count_tail_untouched}`; `output::tests::{identity_ramp_is_i_over_1023,
  set_keeps_point_1024_and_bumps_version, get_gamma_leaves_point_1024, qi_answers_output_one_to_six_and_mtl,
  qi_null_riid_is_e_pointer, find_closest_unknown_without_device_is_invalid_call,
  find_closest_width_without_height_is_invalid_call}` (the `cfg(test)` constructor; `wsi/win32.rs` is PE-only, so its
  calls first run at Task 12 Step 4, P1). Expected: FAIL.
- [ ] **Step 2: GREEN**; host checks; msvc builds.
- [ ] **Step 3: Commit:** `dxgi: the output, its modes and the Win32 window system in Rust`.

### Task 11 (S4-T5): the native WSI hooks (lane `t5`)

**Files:** `src/dxgi/src/wsi/hooks.rs` (+ `wsi/hooks/tests.rs`). C++ replaced natively:
`src/util/wsi_monitor_headless.cpp`, `wsi_window_headless.cpp`'s functions dxgi uses (the files stay for d3d11/d3d12).

**Interfaces:** the seven `wsi` functions natively over `WMTGetWsiHooks()` (`winemetal.h:2330-2354`): display `index`
→ `monitor`; name UTF-16 → `[u32; 32]`, last 0; rect; mode index 0 = current, list index n = hook index n+1
(`ENUM_CURRENT_SETTINGS`/`REGISTRY` → 0); bpp 32, never interlaced; window size from `window`, else 1024×1024
(`wsi_window_headless.cpp:40-48`); no hooks → none of anything.

Hazards: hooks are C function pointers set by the VM host before any device (`extern "C"`); a hook's 0/0 mode
reaches `modes.rs` (D7); names widen to the 4-byte native WCHAR. Statics: none (the hooks are winemetal's).

Pinned by: `d3d11_dxgi.native` (two fake displays), the native and loopback gates, `wire.cpp`, `hook.sh`.

- [ ] **Step 1: Failing tests:** `wsi::hooks::tests::{no_hooks_no_monitors, name_widens_to_u32, mode_zero_is_current,
  list_index_shifts_by_one, window_falls_back_to_1024}` with test hooks installed through a test-only setter
  (`cfg(test)`). Expected: FAIL.
- [ ] **Step 2: GREEN**; host checks.
- [ ] **Step 3: Commit:** `dxgi: the native window system over winemetal's hooks`.

### Task 12 (S4-T6): the meson switch, the C++'s deletion and the S4 merge's probe (lane `t6`)

The controller first rebases and fast-forwards Tasks 6, 8, 9, 10, 11 onto `step3-dxgi` (the S4 merge, ledgered); the
lane branches from the result.

**Files (DXMTRS):** modify `src/dxgi/meson.build` (Task 6's template: `dxgi_res`, `rust_runtime_dep`,
`winemetal_dep`, `perf-build`, P9's flags natively for the five names, `vs_module_defs : 'dxgi.def'`, the builtin
postprocess; `dxgi_dep` unchanged: `link_with` the DLL, the folder's headers); modify `src/dxgi/dxgi_interfaces.h`
(delete `VendorExtension` and `g_extension_enabled`, V10); delete `src/dxgi/{dxgi.cpp, dxgi_factory.cpp,
dxgi_adapter.cpp, dxgi_output.cpp, dxgi_options.cpp, dxgi_options.hpp}`. Kept: `dxgi_interfaces.h`, `dxgi_object.hpp`,
`dxgi_resource.hpp`, `dxgi.def`, `version.rc`.

- [ ] **Step 1: The native probe, every family.** `sh $S/lane-dylib.sh $S/wt-t6 $S/ld-t6 dxgi d3d10core`. Expected: `same` (Task 7
  Step 4's RED now green). Then `… dxgi d3d10core --ported`: `same`, and each `NOT_ON_C` line equals P20's result
  (slot 12 `80004001` + ERR, slot 16 null `80070057`, `CheckFeatureSupport` null `80070057`, d3d10 null adapter
  `80070057`, `zero-den` sorted last and skipped, `EnumAdapters1(0, NULL)` `887a0001` with the factory count
  unchanged, each null-`riid` shape). A difference goes to the owning lane's files, fixed test-first here, and ledgered.
- [ ] **Step 2: Meson and deletion**, then Task 6 Step 3's `cfg.sh` checks for `src/dxgi/dxgi.dylib`
  (`-Wl,-u,_CreateDXGIFactory` … five, `production`; `perf-build` with the define) and, with `--pe <arch>` for
  `arm64ec`, `x86_64`, `i686`, `src/dxgi/dxgi.dll` (the staticlib(s), `rust_pe_rt.c.obj`, `-lntdll`, `winemetal`'s
  import library, `dxgi.def`, the version resource).
- [ ] **Step 3: PE tables and ABI.** `lane-dll.sh … dxgi <arch>` for `arm64ec`, `x86_64`, `i686` (Step 2's meson
  lines; `d3d10core` the same for `arm64ec`, for Step 4). Expected:
  `--coff-exports` = `$S/ref-mac/exports.txt`'s dxgi names and ordinals (`ref-guest`'s for the two guest widths);
  imports per P15; `sh $S/i686-ret.sh $S/cargo-t6/i686-pc-windows-msvc/production/dxgi.lib dxgi` → `compared <n>`
  (every vtable function and export) and `same`; `llvm-nm -u` of each library names no `DXGIGetDebugInterface1` (P19).
- [ ] **Step 4: The PE probe on both Mac lanes (pipeline owner, P1; no lock: a copy of `$S/ref-wine.app`).** `sh
  $S/lane-app.sh $S/ld-t6/wine.app $S/ld-t6/arm64ec/dxgi.dll $S/ld-t6/arm64ec/d3d10core.dll`, then `$S/run.sh
  $S/ld-t6/wine.app <x64|arm64ec> t6-<lane> $S/wt-mn-probe/build/<dxmt-tests|dxmt-tests-arm64ec>/d3d11_dxgi.exe fixed`,
  its family lines diffed with `$S/wt-mn-probe/dxmt/tests/d3d11_dxgi.fixed` → empty, and `fixed --ported`'s `NOT_ON_C`
  lines checked against P20. Expected: both lanes `same`. A difference is fixed as Step 1's. If `lane-app.sh`'s control
  failed at Task 4R, skip and ledger (P1).
- [ ] **Step 5: Host checks** (Global Constraints, all of `dxgi`'s, the perf-build builds included; RC), `--cover` of
  the probe still passes.
- [ ] **Step 6: Commit:** `dxgi: in Rust (dxgi.dll and .dylib); the C++ goes, its headers stay for d3d11 and d3d12`.

### Task 13: the end-of-step gates, once (owner)

Every gate runs once on `step3-dxgi`'s head, in this order; a row that fails is recorded, not fixed (Task 14). Each
block below is one or more pipeline-lock jobs (Global Constraints): a job first checks out `step3-dxgi`'s head in `$D`
(detached) and builds what it tests, logs the commit and wine.app's cdhash, and leaves `$D`'s `git status
--porcelain` empty; MacNeutron's commands run in `$S/wt-mn-probe` (P22; `cd` there, so check.sh's ARM64EC form's
relative paths and `TESTS="$ROOT/build/dxmt-tests"` name the worktree's probe builds) with `BUILD_DIR=<M>/build`
exported (check.sh then takes `<M>/build/wine-arm64/wine.app`, `check.sh:17`, `:19`), as Task 4's lock jobs ran
`gate.sh` and `dxgi-guest.sh`; `eac-vm/builder.sh` and `presenter/check.sh` run from `<M>` (they read `$ROOT/build`,
not `BUILD_DIR`). Exports for every VM and reference gate:
`MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img`, `MN_REF_FROZEN=$S/ref-vm` (present-gate: `$S/ref-present`),
`MN_REF_WINE_APP=$S/ref-wine.app`, the signing variables. Report: `$S/reports/gates-{mac,native,vm,rust}.md` in step
2's shape (commands, results, what is not plain ok).

- [ ] **Mac.**
  - `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot` → "development build",
    "staged … (dev)"; the key (Global Constraints) = `71a7e5f2…3311`.
  - Tables (`PATH="$(sh dxmt/toolchain.sh):$PATH"`, wine.app's `DXMT/aarch64-windows`): exports of `dxgi.dll`,
    `d3d10core.dll` = `$S/ref-mac/exports.txt`'s; importers per P15; no `Wine builtin DLL` at bytes 64-79 (bundle.sh's
    assert, rc 0).
  - Identity: `codesign -dv` of `build/wine-arm64/wine.app` names its cdhash (runtime=dev); `presenter/check.sh`'s
    and `check.sh`'s installed tool folders carry that cdhash; wine.app's `dxgi.dll` imports
    `api-ms-win-core-synch-l1-2-0` (Rust's std; the C++'s didn't), so the Rust is what runs.
  - `sh dxmt/check.sh` → the ok/FAIL row set of `$S/ref-perf/check-x64.txt` (Task 4R Step 5's measured set, expected
    417 ok), 0 FAIL;
    ARM64EC: `MACNEUTRON_ARM64_TESTS=build/dxmt-tests-arm64ec
    MACNEUTRON_ARM64_LOOP=$PWD/build/dxmt-tests-arm64ec/present_loop.exe DXMT_CHECK_WORK="$TMPDIR/macneutron dxmt
    arm64ec" sh dxmt/check.sh` → `check-arm64ec.txt`'s set. Its `MACNEUTRON_FRAMEGEN` rows (`:1297-1326`) are spec
    §8's framegen modes. Flaky rows (d3d11/d3d12 `bg-leave`, `ts-many`, the gamma rows while someone uses the Mac):
    one re-run each, ledgered.
  - `sh wine-arm64/check.sh dxmt dxmt-present dxmt-release` → PASS lines (incl. `present_loop cycles=20 ok`).
  - `sh presenter/check.sh` → 66 ok, 0 FAIL.
  - `sh dxmt/mtld3d-facade-test.sh` → its suites as step 2's gates-mac (8 passed, 0 failed for the facade).
  - `full` A/B: `$S/run.sh <$S/ref-wine.app | <M>/build/wine-arm64/wine.app> <lane> <c|r>full-<lane>
    $S/wt-mn-probe/build/<dxmt-tests|dxmt-tests-arm64ec>/d3d11_dxgi.exe full --ported` for both lanes; `sh
    $S/full-diff.sh` → no unmasked difference outside `NOT_ON_C` cases (raw `num/den` refresh
    rates included). Natively: `DYLD_LIBRARY_PATH=$S/ref-native` against `DYLD_LIBRARY_PATH=build/dxmt-native/install/lib`
    `$S/probe-native/d3d11_dxgi full --ported`, through `full-diff.sh` → the same.
  - Performance (D22, P16), each once: `rate` on arm64ec and natively against `$S/ref-probe/rate-*.txt`; `present_loop
    1280 720 0 0 600 0` frame-time line through `$S/run.sh` on both runtimes (x64). Within 5%; over → one C++ control
    in the session; still over → STOP, report to the maintainer before Task 15.
- [ ] **Native and VM host.**
  - `sh eac-vm/dxmt-native/build.sh` rc 0; `nm -gU install/lib/dxgi.dylib` exactly the five names, `d3d10core.dylib`
    exactly its two; `nm -mu` of d3d11/d3d12: `(from dxgi)` only `_CreateDXGIFactory1`; d3d10core: `(from d3d11)` only
    `_D3D11CoreCreateDevice`; no `(from …)` names a Rust symbol (`rust_eh_personality`, `_RNv…`); `python3 -I -c` dlopen
    `RTLD_NOW` of the five from `install/lib` succeeds.
  - `sh eac-vm/dxmt-native/tests/build.sh` → `build: ok`; `tests/hook.sh` → `hook: ok`; `tests/win32.sh` → `win32: ok`.
  - `sh eac-vm/dxmt-native/gate.sh` → `dxmt-native gate: all 31 programs passed` (its `build-tests.sh`: `tests: ok`,
    the probe vs `.native` with `fixed`).
  - `sh eac-vm/host/build.sh`; identity: the VM host's `build/eac-vm-work/dxmt/{dxgi,d3d10core}.dylib` sha256 equal
    `build/dxmt-native/install/lib`'s; `sh eac-vm/d3d/loopback-gate.sh` → all 31 passed; `sh eac-vm/d3d/tests/answers.sh` →
    `answers: ok`; `sh eac-vm/d3d/both.sh wire` → `wire: ok`; `sh eac-vm/d3d/tests/identity.sh` → `identity: ok`;
    `sh eac-vm/d3d/tests/wrapped.sh` → ok.
  - D3D9, as step R's Task 9 ran it: `sh dxmt/mtld3d-build.sh --loopback` → its path
    (`<M>/build/d3d9-cargo/wm/aarch64-apple-darwin/production/mtld3d.so`); `MTLD3D_SO=<it>
    DXMT_CHECK_WORK=$S/gn/d3d9-lo sh dxmt/d3d9-check.sh` and `MTLD3D_PE=<M>/build/wine-arm64-src/dxmt-install/aarch64-windows
    DXMT_CHECK_WORK=$S/gn/d3d9-pe sh dxmt/d3d9-check.sh` → `d3d9-check: ok` (the `arm64` row included); `sh
    dxmt/mtld3d-conformance.sh` and `MTLD3D_SO=<it> sh dxmt/mtld3d-conformance.sh` → step R's figures, "only triaged
    sites … fail anew: passed" (a new site: one control run against `$S/ref-wine.app`, ledgered).
  - P9: `LC_ALL=C /usr/bin/grep -ac "$HOME"` = 0 and `nm -ap | /usr/bin/grep -c ' OSO '` = 0 on both new dylibs; the
    grep = 0 on wine.app's `dxgi.dll`, `d3d10core.dll`.
- [ ] **VM.**
  - Builder up with `VM_SHARES`; `sh eac-vm/dxmt/build.sh`; `sh eac-vm/dxmt/check-build.sh` → `check-build: ok (19
    files in x86_64-windows, 15 in i386-windows)`; exports and
    importers' imports vs `$S/ref-guest` (both widths, P15); i386 `dxgi.dll` imports `ntdll.dll: _aulldiv` (D11);
    P9 0 on the guest DLLs. Identity: `dxmt-guest-build` and `-build32`'s `.setup-inputs` end in `step3-dxgi`'s head.
  - `sh eac-vm/image/guest-stages.sh dxmt d3d`; `sh eac-vm/builder.sh sync eac-vm/image mn-image`; `sh
    eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`. Identity: `dxmt-guest/.mn-source` names `step3-dxgi`'s
    head; the staged guest `dxgi.dll` and `d3d10core.dll` (both widths) equal `dxmt-guest-build{,32}`'s by sha256.
  - `sh eac-vm/dxmt/gate.sh` → `dxmt gate: all 45 programs passed`.
  - `sh eac-vm/tests/d3d-smoke.sh` → `d3d-smoke: ok` (both `32:` blocks: `rawexport`, `warp`, `wmsize`, `conv`); `MN_WOW64=new
    sh eac-vm/tests/d3d-smoke.sh` → ok.
  - `sh eac-vm/tests/dxgi-guest.sh i386` → `dxgi-guest i386: ok` (the first i386 Rust dxgi); `sh
    eac-vm/tests/dxgi-guest.sh x86_64` → `dxgi-guest x86_64: ok`.
  - `sh eac-vm/d3d/gate.sh` → the forwarder gate passes (`d3d10_core`, `d3d10_wine`, `d3d11_api` rows).
  - `sh eac-vm/dxmt/d3d9-gate.sh` → `d3d9 gate: ok`; `sh eac-vm/dxmt/present-gate.sh` → `present gate: all passed`.
- [ ] **Rust** (host-only, no lock), on `$S/wt-s0` at `step3-dxgi`'s head with `CARGO_TARGET_DIR=$S/cargo-gates`:
  host tests of `dxmt-util`, `nvapi`, `nvngx`, `dxgi`, `d3d10core`; the msvc builds; the perf-build builds (Global
  Constraints); RC in the lane form over `build/step3/wt-s0` (`CARGO_TARGET_DIR=$PWD/build/step3/cargo-gates/check`)
  → fmt, clippy (every crate on each PE target it ships on and darwin, `dxgi`/`d3d10core` included), doc, test and
  audit legs ok, `audit: clean (<n> files)` (694 plus Tasks 6-11's new `.rs`, counted), `rust-check: ok`.
- [ ] **Ledger** every result; nothing committed.

### Task 14: one fix pass and the ported goldens (owner, lanes as needed)

- [ ] **Step 1: Defects.** For each failing gate row: the root cause, a failing test in the owning file's tests (or a
  probe case through the controller), the fix in a lane worktree, the lane's host checks; the failed rows and the
  probe re-run once. Defects that need more than one round go to the maintainer.
- [ ] **Step 2: `NOT_ON_C`.** `fixed --ported` on both Mac lanes (`$S/run.sh`), natively (`$S/probe-native`) and in
  the guest (`dxgi-guest.sh i386` and `x86_64`); each `NOT_ON_C` line equals P20's result (adapter slot 12
  `80004001` + ERR, slot 16 null `80070057`, `CheckFeatureSupport` null `80070057`, `D3D10CoreCreateDevice` null
  adapter `80070057`, the native `zero-den` mode last and skipped, `EnumAdapters1(0, NULL)` `887a0001` with no adapter
  created and the factory count unchanged, each null-`riid` shape's `80004003` or `80070057`). The controller appends
  them to `.fixed`, `.native`, `.i386`, `.guest64` and switches check.sh's row, `build-tests.sh` and `dxgi-guest.sh`
  to `--ported` (the guest invocation stays `fixed`: its frozen macOS reference has no such lines; the guest's x86_64
  code, which no Mac lane runs (the Mac DLLs are ARM64X), is pinned by `.guest64`), in one commit (MacNeutron
  `step3-probe`, P22: `dxmt tests: dxgi's defined results for what crashed the C++`); those comparators re-run once
  (lock jobs).
- [ ] **Step 3: Commit** each fix in DXMTRS (`dxgi: …` / `d3d10core: …`), ff onto `step3-dxgi`.

### Task 15: notices, the spec, push and pin (controller)

**Files (MacNeutron; Step 1's on `step3-probe` in `$S/wt-mn-probe`, the spec and ledgers on `dxmtrs`):**
`wine-arm64/licenses/README`, `wine-arm64/tests/licences_test.sh`, `eac-vm/setup/parts.sh:24-40`,
`release/release.sh:672-674`, `eac-vm/host/macneutron-vm.swift:207`,
`<M>/docs/research/2026-10-15-vm-route/README.md:1089, 1114`, `dxmt/pins`, the spec, the ledgers.

- [ ] **Step 1: Notices, test first (D23).** `licences_test.sh` greps the DXMT entry for `nvngx.dll, nvapi64.dll,
  dxgi.dll, d3d10core.dll, winemetal.dll and winemetal.so are Rust` → FAIL; the README entry → PASS. `parts.sh` (the
  guest's `dxgi.dll` and `d3d10core.dll` link Rust's standard library) and `release.sh` (the VM's `dxgi.dylib`,
  `d3d10core.dylib`). `cargo metadata` shows only path dependencies for `dxgi` and `d3d10core`. The swift comment and
  the research README name per-dylib logs (`_dxgi.log`, `_d3d11.log`, `_d3d12.log`, `_d3d10core.log`, D2).
- [ ] **Step 2: Spec.** §5 step 3: "`d3d10core`, then `dxgi`" (D1: the D3D10 layer with d3d11 at step 5,
  `d3d10_blob.cpp` with d3d12), marked done with what it did; corrections D2, D3 (+ P7, P8), D4, D5, D7, D11 and :162's
  i686 shims (closed if Task 13's `_aulldiv` import and `32: wmsize` passed, else left open with why), D12 (+ P9), D13
  (+ P10), D17, D20 and D24 (+ P14; step R's `tools/rust-check.sh` legs now cover the two crates), D22; step R's
  layout (P17, P22) as the plan's pass onto it. Privacy scan of the spec, plan and ledger. Commit on `dxmtrs` (the
  controller, as step R's Task 11 Step 1) `Spec: DXMTRS step 3, d3d10core and dxgi in Rust`.
- [ ] **Step 3: DXMTRS main.** Privacy-scan `git -C $S/wt-s0 log -p main..step3-dxgi`. One lock job: rule 5's restore
  (`git -C $D checkout -q --detach $(cat <M>/build/wine-arm64-src/dxmt.applied)`), `git -C $D switch main && git -C $D
  merge --ff-only step3-dxgi`, `git -C $D push --dry-run origin main` (a fast-forward of `main` only), push, then `git
  -C $D checkout -q --detach main`. Remove step 3's worktrees only (`$S/wt-s0`, `$S/wt-<lane>`; `chmod -R u+w` first)
  and its branches (`step3-dxgi`, `step3-<lane>`; `git cherry` empty first). Expected: no step-3 worktree or branch in
  `$D`, every other step's branches and worktrees as they were (rule 7), no stash.
- [ ] **Step 4: Pin, one commit** (P22, step R's P13). In `<M>` on `dxmtrs`: `git merge --squash step3-probe`,
  `DXMT_COMMIT=<main's sha>` in `dxmt/pins`; commit `DXMT: d3d10core and dxgi in Rust (DXMTRS <sha>)`; `git diff dxmtrs
  step3-probe -- . ':!dxmt/pins' ':!docs/superpowers' ':!.superpowers'` empty; then `git worktree remove
  $S/wt-mn-probe` and `git branch -D step3-probe`. **The pushed pin's build** (rule 7, step R's Task 11 ruling: never a
  forced pinned build while other steps' branches live in `$D`), one lock job: `git -C $D ls-remote origin main` names
  `<main's sha>`; `git -C $D checkout -q --detach <main's sha>`; `make wine-arm64` (a development build of the pushed
  commit; `DXMT/version` reads `+dev`). On it: the key; x64 and ARM64EC check.sh (the row sets of
  `$S/ref-perf/check-*.txt`, 0 FAIL; flaky rule); the probe row; P9 on what ships (wine.app's two DLLs; `sh
  eac-vm/dxmt-native/build.sh` → the two dylibs; `sh eac-vm/dxmt/build.sh` → the guest DLLs, both widths); `git -C $D
  remote get-url --push origin` still `git@github.com:Mac-Neutron/DXMTRS.git`.
- [ ] **Step 5: MacNeutron main.** `git switch main && git merge --ff-only dxmtrs`, `git push --dry-run origin main`,
  push; checkout back on `dxmtrs`. A failed fast-forward (here or in Step 3) STOPs for the maintainer; never rebase
  `main` or `dxmtrs` unasked. Ledger and program ledger (`.superpowers/sdd/dxmtrs-program.md`): step 3 done, the pinned
  sha.

## Review record

Plan review of 2026-10-30 (46 findings: 2 Critical, 44 Important), each checked against the source named; none
rejected. Duplicates are applied once and point at the same edit.

1. Critical, Win32 refresh `dmDisplayFrequency/1` (`wsi_monitor_win32.cpp:87` is `{f * 1000, 1000}`): applied — Task 10
   `refresh_from_hz` + `win32_refresh_is_milli`; P5's `den=1000`/`den=hook` relation; `full` prints `num/den`.
2. Important, private data destroy/AddRef unspecified: applied — D6 (verified :33, :47, :80, :102), Task 7's `Drop`,
   erase Release, get AddRef and their tests; Task 3 `pdata-iface-dies` on all three objects.
3. Important, the foreign fake forwards QI: applied — Task 2 `gamma-foreign`, a fake answering QI itself, graded from
   Step 3; Step 1 grades only `fs-gamma`, the pre-seam foreign result ledgered.
4. Important, `EnumAdapters1(0, NULL)` crashes the C++: applied — P20 (`INVALID_CALL`, no adapter, count unchanged),
   Task 3 `ISOLATE|NOT_ON_C`, Tasks 12 and 14.
5. Important, unasserted DEVMODEW/MONITORINFOEXW/POINT/D3DKMT/WMTWsi* layouts: applied — Task 7 `abi.rs` and dumps.
6. Important, split the `EnumAdapters*` null cases: applied with 4 (887a0001, 887a0002, the isolated one).
7. Important, `EnumAdapterByGpuPreference` leaves `*pp` untouched: applied — Task 3 case, Task 8 slot text.
8. Important, missing C++ null checks: applied — gamma ×3, output `GetDesc1`, `FindClosestMatchingMode(1)`, overlay and
   composition flags, adapter/output `GetParent`, factory `GetParent(NULL)`, NVEXT with a 0xA5 `pp`, `big`.
9. Important, null `riid` undefined: applied — P20 (E_POINTER / E_INVALIDARG), one `ISOLATE|NOT_ON_C` case per shape.
10. Important, `*pDataSize` and destroy paths unpinned: applied — `pdata-short` size, `pdata-big`, `pdata-iface-erase`,
    `pdata-iface-dies`, `pdata-retype`; Task 7 tests (`drop_releases_interfaces` is the asked-for object-drop test).
11. Important, reservation values fit in 32 bits: applied — `0x0000000180001000`, `0x0000000200002000`.
12. Important, `FindClosestMatchingMode1` returns and second pass: applied — Task 3 cases, Task 10 two passes with
    `current`, Task 7's `find_closest` signature.
13. Important, the closest-mode debug line holds display numbers: applied — P5's relation words, Task 4 Step 3 check.
14. Important, native 0/0 mode in the shared fixture: applied — only the `isolate:zero-den` child's hooks add it.
15. Important, refresh (duplicate of 1): applied with 1.
16. Important, NVEXT registry values unpinned: applied — `DllMain registry` (`PE_ONLY`; absent on i386).
17. Important, transcoding quirks and native Description: applied — `configu`, `confbad`, `abi::transcode_into`
    (replacing `encode_utf16_into`/`utf32`), byte-valued config, `desc=device-name`, Task 13's native `full` A/B.
18. Important, `DXMT_CONFIG_FILE` untested: applied — `conffile2`.
19. Important, d3d10c's refusing adapter can't see the IID or umd: applied — `asked=… umd=…`, S_OK to other IIDs.
20. Important, swap-chain fakes miss HWND and the QI'd reference: applied — `hwnd=same`, `devref=+0`.
21. Important, stale log files: applied — P18 (folder deleted first, `dxgil <child> none`).
22. Important, `--cover` doesn't bite: applied — P21 `--bites`, run at Task 4 Step 6.
23. Important, kit scripts without controls: applied — `i686-ret.sh` `compared <n>` + u32 control; `full-diff.sh` control.
24. Important, lanes 8-10 test no vtable code: applied — Task 8 factory host tests, Task 10 `cfg(test)` output tests;
    the factory tests use non-zero sizes instead of Task 11's hooks setter (it isn't on Task 8's branch; the 0×0 fill
    and `EnumAdapters1` are pinned at Task 12), ledgered as P2's cost.
25. Important, P20 results first compared at Task 14: applied — `lane-dylib.sh --ported` at Tasks 6 and 12, PE at 12.
26. Important, misaligned access passes in production: applied — debug-assertion `lane-dylib.sh`, wider cases, RF1.
27. Important, Task 2's (a) UB pre-seam, (b) QI forwarding, (c) a mutation that can't bite: applied with 3; Step 3's
    mutations are now the QI refusing `IMTLDXGIOutput` and Present skipping the read.
28. Important, the guest's x86_64 code unpinned: applied — P4 `dxgi-guest.sh <i386|x86_64>`, `.guest64`, Task 14 text.
29. Important, D24 omitted and D20 misquoted: applied — D20 restated, D24 added (with its own format task, which step
    R's format commit later replaced), P14, Global Constraints, Task 13.
30. Important, mtld3d gates missing: applied — facade test, `d3d9-check.sh`, conformance at Task 13; framegen via check.sh.
31. Important, nothing proves the Rust ran: applied — identity rows at Task 13 (cdhash, sha256, `.setup-inputs`,
    `.mn-source`, the synch API-set import, which the C++ `dxgi.dll` lacks today).
32. Important, check.sh row count never observed: applied — Task 4 Step 4's reference runs to `$S/ref-perf`.
33. Important, `perf-build` never compiled: applied — Global Constraints form, Tasks 5-7, 12, 13.
34. Important, private data from two threads untested: applied — `two_threads_set_get_balance`.
35. Critical, the worktree probe build would fetch and build LLVM: applied — `BUILD_DIR=<M>/build` constraint, Task 3's
    build line (exes land in the worktree; `run.sh` takes absolute paths).
36. Important, `run.sh` written after Task 3 needs it: applied — Task 3a Step 0 with the nvapi-row control.
37. Important, check.sh row inside `MACNEUTRON_DLSS=1`: applied — the window after `:1294` and before `:1297` named; the
    "clear the variable before dxgi loads" option dropped (the probe imports `dxgi.dll` statically, so `DllMain` reads
    it before `main`).
38. Important, Task 5 landing before the references: applied — order table, P17, Task 4 Step 1.
39. Important, guest build dirs deleted on HEAD change, wrong object path: applied — `$S/ref-build` snapshot, P13.
40. Important, no crossbuild meson check before Task 13: applied — `cfg.sh --pe`, `lane-dll.sh` replays its line;
    Tasks 6 and 12 put the meson step before the PE tables.
41. Important, Task 4 VM commands on the full default disk: applied — `MN_STATE` exported at Task 4 Step 5, P17.
42. Important, D24 (duplicate of 29): applied with 29.
43. Important, the layout program can't run PE executables on a lane: applied — `-fdump-record-layouts` dumps.
44. Important, Win32/D3DKMT/registry types missing from `abi.rs` (duplicate of 5): applied with 5, constants included.
45. Important, P1's reason is false: applied — `$S/lane-app.sh` (re-signed copy, control), Task 12 Step 4; P1 keeps a
    fallback if the control fails; `make wine-arm64` called an owner build, not a gate.
46. Important, Task 3 too large: applied — 3a/3b/3c, per-family `.inc` files, a Makefile prerequisite.

Noted, not a finding: Task 10 says `GetMonitorFormatBpp` answers 0 for an unknown format; the C++ answers 32 after its
warn (`dxgi_output.cpp:36-38`). Unreachable today (every back-buffer format is in its table). Controller: corrected in
Task 10 to 32.

Pass onto step R's layout (2026-10-30, step R's Task 11 Step 6, R-D21): the crates moved to `src/d3d10` (`d3d10core`)
and `src/dxgi` (`dxgi`) in the root workspace (D9, layout rule 2); every Rust command runs from a worktree's root with
`CARGO_TARGET_DIR` outside the tree, and RC (`tools/rust-check.sh` in its lane form) replaced the per-workspace fmt,
clippy, doc and audit commands (Global Constraints, D20); the format task went (D24: step R's R1f did it); Task 5 now
also edits `tools/rust-check.sh`'s package lists, `tools/audit.sh`'s `PE_CRATES` and both uses of `APP_POINTER_DIR`
(`src/d3d9/src src/dxgi/src src/d3d10/src`), the root `Cargo.toml` and `docs/CONVENTIONS.md`'s three scope sites
(D19, D21); the pipeline lock rules (`build/pipeline.md`, rule 7 included) joined the Global Constraints, Task 13 and
Task 15 (no forced pinned build; only step 3's branches removed); Tasks 1-4 marked done early with their commits, and
Task 4R added (both rebases first, then the five layout-bound kit scripts, the deferred references); P22 keeps
MacNeutron's commits on `step3-probe` until the pin. Checked: the plan has no old-layout path, and every `src/`,
`tools/`, `tests/d3d9/` and `docs/` path it names exists at DXMTRS 1ccec40 (`git cat-file -e 1ccec40:<path>`), except:
what its tasks create (`src/dxgi/Cargo.toml` and `src/dxgi/src/**`, `src/d3d10/Cargo.toml` and `src/d3d10/src/**`,
`src/util/src/{env.rs,env/tests.rs,com/tests.rs,tests.rs}`, Task 5's scratch `x.rs` REDs); meson build-folder names,
not tree paths (`src/<dir>/<dll>.dll`, `.dylib`, `.dll.p`, `*.obj`); MacNeutron's `docs/superpowers` (Task 15's
pathspec) and the `<M>/docs/…` files.
