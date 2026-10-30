# DXMTRS step R: D3D9 at src/d3d9, one workspace, built by meson — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** mtld3d stops being a subproject: every file moves by `git mv` into DXMTRS's layout (`src/d3d9`, `tests/d3d9`,
`docs/d3d9`, one root Cargo workspace), DXMTRS's meson builds every D3D9 binary MacNeutron ships, mtld3d's Makefile, CI,
bundle and release machinery are gone, one Rust gate (`tools/rust-check.sh`) and one audit cover the tree, and
MacNeutron's scripts change once, in the pin bump. Nothing a player or the launcher sees changes; NDEBUG for the
guest's C/C++ is the one deliberate change, its own commit.

**Architecture:** Save every "before" artifact first (R0). Move and merge the workspaces in one commit (R1), then one
mechanical format commit and the clippy fixes (R1f). Build the Mac's ARM64X pair through a ported `lld-link` recipe
(`tools/arm64x-link.sh`) and the Mac `mtld3d.so` through `tools/cargo-staticlib.sh`'s new cdylib mode (R2); the guest's
PE pairs as `nvngx.dll` is linked and its Linux `mtld3d.so` through the moved `tools/linux-cargo.sh` (R3). Each build is
compared table by table with the saved binaries. Then the gate script, the audit and the documents (R4a, R4b),
MacNeutron's edits (RM), NDEBUG (R5), every gate once, one fix pass, push and pin.

**Tech Stack:** Rust 1.99.0 (cargo `--offline`, clippy, rustdoc), the installed nightly's rustfmt, meson 1.12, llvm-mingw
(clang, `lld-link`, `llvm-rc`, `llvm-objcopy`, `llvm-readobj`), Wine's `winebuild`, POSIX sh, `python3 -I` (scratch only).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (approved revision cec2527: §3 layout and build
ownership, §5 step R and the D3D9 phases, §6, §7, §8). Inputs: `build/step-r/map/0-design.md` (design: tree, binaries
table §2.2, commits §2.3, MacNeutron edit list §2.4, identity checks §2.6), `1-mtld3d-tree.md` .. `5-consistency.md`
(readers), `6-critic.md` (A-G, all accepted), `7-decisions.md` (R-D1..R-D22, binding). Format: step 2's plan
(`2026-10-29-dxmtrs-step2-winemetal.md`) and step 3's (`2026-10-30-dxmtrs-step3-d3d10core-dxgi.md`); exact gate
commands from `build/step2/reports/gates-{mac,native,vm}.md`.

Paths: `<M>` = MacNeutron's main checkout (absolute), `$B` = `<M>/build`, `$D` = `$B/wine-arm64-src/dxmt` (DXMTRS),
`$SRC` = `$B/wine-arm64-src`, `$W` = `$B/eac-vm-work`, `$A` = `$B/wine-arm64/wine.app/Contents/Resources`, `$S` =
`$B/step-r`, `$R` = `~/.rustup/toolchains`. Before R1 the old paths are `$D/mtld3d/{windows,unix,...}`. Commands run from
`<M>` unless a step says otherwise. Ledger: `.superpowers/sdd/2026-10-30-dxmtrs-stepR-layout/progress.md`.

## Decisions

Binding (`7-decisions.md`), short form:

- **R-D1** Layout of `0-design.md` §1.1-§1.3 and spec §3; 16 crates (`vendor/snmalloc-sys` is a patch, not a member).
  D3D9's test crates: `tests/d3d9/programs` (package `mtld3d-tests`), `tests/d3d9/runner` (`mtld3d-e2e`),
  `tests/d3d9/conformance`, `tests/d3d9/probes`. Package names unchanged.
- **R-D2** One root workspace, `resolver = "2"`; `[workspace.dependencies]` takes the unix entries for `objc2`,
  `objc2-foundation`, `objc2-metal` (test-only delta for `mtld3d-core`'s dev-deps, expected); `.cargo/config.toml` keeps
  `ARFLAGS_arm64ec-pc-windows-msvc = "-machine:arm64ec"`, drops `[build] target` and every `/opt/xwin` entry;
  `[workspace.package]` is DXMTRS's (LGPL-2.1-or-later); every mtld3d crate states `license = "Zlib"`.
- **R-D3** D3D9's shader cache stays valid: `core/build.rs` hashes path-independent names reproducing today's bytes
  (`"../../unix/shared/src/mtl.rs"` …, `ponytail:` comment); `dxso/tests.rs`'s `MODULE_ROOTS` stays consistent;
  `SHADER_EMITTER_VERSION` (printed by `emit_corpus`) equal before and after.
- **R-D4** `layouts.rs`, `protos.rs` (`tools/winemetal-layouts.sh`) and `derive_inventory.txt` regenerated once after
  the move as a no-diff check.
- **R-D5** Binaries as `0-design.md` §2.2; the Mac `mtld3d.so` links with `-Wl,-S`; `tools/arm64x-link.sh` keeps the
  Makefile's guards (LLD 23+, an ARM64EC half in `dllcrt2.o`) and drops `--fake-module`; resource compilers: the Mac
  keeps llvm-rc (`arm64x-link.sh`), the guest's changes from llvm-rc (`<M>/dxmt/mtld3d-build.sh:83`) to meson's
  `wrc_generator` (`windres` = llvm-windres) — R-D5's "each target keeps its current resource compiler" is wrong for the
  guest (correction, Task 11 Step 1); the resource directory and VERSIONINFO values are compared (P3), a byte
  difference ledgered, not assumed padding; every non-guest x86/x86_64 cross build takes the guest's `shared_library`
  recipe.
- **R-D6** `tools/mingw-cc.sh` (personality from its link name; `cargo-staticlib.sh` makes the four links in the build
  dir) and `tools/linux-cargo.sh` move into DXMTRS.
- **R-D7** No build output in the DXMTRS tree: absolute `CARGO_TARGET_DIR` under `$B` for every MacNeutron cargo run;
  root `.gitignore` gets `/target` and `/.codex`.
- **R-D8** `cargo "+$MTLD3D_RUST" fetch --locked` at the DXMTRS root before `meson compile` in `wine-arm64/build.sh`,
  `eac-vm/dxmt/build.sh`, `eac-vm/dxmt-native/build.sh`.
- **R-D9** `eac-vm/dxmt/check-build.sh` allows exactly `x86_64-unix/mtld3d.so` and `i386-unix/mtld3d.so` in
  `$W/dxmt-guest`.
- **R-D10** NDEBUG in its own commit (R5); the production-assert check moves into `check-build.sh` and `bundle.sh`.
- **R-D11** CPU baselines kept (guest `x86-64-v2`/`pentium4` for D3D9, none for DXMT's C++).
- **R-D12** `tools/rust-check.sh` under MacNeutron's environment, inputs by documented variables; legs include
  `clippy-perf`/`test-unit-perf` (`MTLD3D_PERF=1`), `test-unit`'s `--features mtld3d-core/disk-cache`, `clippy -p
  mtld3d-unix --features mtld3d-unix/winemetal` on darwin; rustfmt from a pinned nightly (`MTLD3D_RUST_NIGHTLY` in
  `dxmt/pins`); `cargo test` (nextest's per-test isolation lost, noted); clippy 1.99.0 on darwin and every
  `-pc-windows-msvc` target a crate ships on.
- **R-D13** One mechanical `cargo fmt` commit right after the layout commit (generated files keep their generators'
  output), clippy findings fixed behaviour-neutral. Step 3's plan drops its Task 5a.
- **R-D14** `configure-test-prefix` inlined in `dxmt/mtld3d-conformance.sh` (lockf, the three keys, the wineserver
  restart after `RetinaMode`, the persistent server, `wineboot`); `--update-baseline` passed through, the two runtime
  strings name it.
- **R-D15** An aarch64 `d3d9_triangle` joins `d3d9-check`.
- **R-D16** Sweeps: `tools/audit.sh` (critic E1), every document path and `make` target named in code or strings (E2),
  MacNeutron comments R makes false (E3), the guest stage stamp gains MacNeutron's `dxmt/` inputs (E4).
- **R-D17** Per task only its own checks; every gate once at the end, one fix pass, push, pin, pinned checks,
  MacNeutron main.
- **R-D18** Translator key unchanged, read before and after.
- **R-D19** Licences: `vendor/snmalloc-sys/LICENSE` (MIT: a byte copy of the crate's own notice wine.app ships today,
  `$S/before/mac/licenses/mtld3d/crates/snmalloc-sys-0.7.6/LICENSE-MIT`, "Copyright (c) schrodingerzhu"; not
  `mtld3d-notices.py:42`, which is `EMBEDDED`, Microsoft's snmalloc notice, still written as `LICENSE-snmalloc`);
  `mtld3d-notices.py` LEGS add the guest legs and every
  shipped package; `eac-vm/setup/parts.sh` copies `licenses/mtld3d/{LICENSE,crates}`, its README names D3D9; the
  README licence map names every zlib file outside the three LICENSE folders and the LGPL `rust_pe_rt.c` in D3D9's
  guest DLLs.
- **R-D20** Root `CLAUDE.md` = mtld3d's agent manual made tree-wide (its "Rules check" rule included); `AGENTS.md` ->
  `CLAUDE.md`; `CONTRIBUTING.md` keeps DXMT's AI policy as the rule for contributions to 3Shain/dxmt.
- **R-D21** Step 3's plan gets a whole-plan pass onto the new layout after R lands.
- **R-D22** No new abstraction beyond the moved scripts; the Makefile's dormant legs are deleted, not ported.

Plan rulings (new here; each: decision — why — cost if wrong):

- **P1 Before-builds.** Mac: `build/wine-arm64/wine.app` is the pinned da6ed2d build (`DXMT/version` =
  `da6ed2d…+da6ed2d418e6`) and is saved as is; guest and native are rebuilt at da6ed2d first (the guest build dir's
  `.setup-inputs` names step 2's 9ffcce5). The ignored build dirs `$D/mtld3d/windows/target` (257 MB) and
  `$D/mtld3d/unix/target` (521 MB) are moved (not deleted) to `$S/before/mtld3d-target/` — they hold the Mac
  before-images' build tree, and once `mtld3d/.gitignore` goes they would be untracked output (development mode, VM
  stamp `+dirty`). — none.
- **P2 References.** `$S/ref-wine.app` (`cp -cR` of the da6ed2d wine.app) and `$S/ref-vm` (re-recorded on the Mac from
  it with `ref.sh`) are new; `$S/ref-present` is copied from `build/step2/ref-present` (vsync-bound macOS reference,
  gated equal to da6ed2d's code at step 2's end). No gate runs on the before: the end gates compare against step 2's
  row sets (`build/step2/ref-perf/*`) and fixed counts; a differing row gets one control run on `$S/ref-wine.app` or
  R0's binaries. — R's before is step 2's after. — a pre-existing flake costs one control run.
- **P3 Identity script.** `$S/ident.sh <file> > <table>` and `$S/ident-diff.sh <before-dir> <after-dir> <ED>…`
  (scratch, uncommitted, like step 2's `lane-so.sh`): one table per binary (PE, per view — native and, on ARM64X, the
  `HybridObject` block, never merged or `sort -u`ed: `llvm-readobj --file-headers` Machine, Characteristics,
  DllCharacteristics, Subsystem, Major OS/subsystem versions, ImageBase, File/SectionAlignment, stack and heap reserve,
  which data directories are present (export, TLS, load config, base reloc, debug), CHPE count, exports with ordinals
  and a count per view, imports by DLL and symbol with a count per view, `--coff-debug-directory` entry types,
  `--coff-resources` type/name/language, bytes 64-79, VERSIONINFO fixed and string values, section names and sizes,
  assert-string count, path-like strings; Mach-O: `nm -gU`, sorted `otool -L`, `otool -D`, LC_RPATH, minos, undefined
  symbols by library, OSO count, assert count, `$HOME` count; ELF: machine, NEEDED, defined dynamic symbols, assert
  count; every binary: IF8's string set, the sorted unique `(MTLD3D|DXMT|MACNEUTRON|RUST)_[A-Z0-9_]+` strings plus
  the presence of `MTLD3D_CONFIG`, `log.dir`, `mtld3d.conf`, `mtld3d_shaders.bin`, `mtld3d-logs`). The diff applies
  exactly the EDs named on its command line and prints everything else; every call in this plan names its EDs, ED4
  wherever ED1 is (a path-length change moves section sizes). — One definition of "identical" for R0-R5 and the
  gates. — none.
- **P4 The shader-cache fingerprint is byte-for-byte.** `src/d3d9/core/build.rs` keeps the two names literally
  (`"../../unix/shared/src/mtl.rs"`, `"../../unix/shared/src/shader_create.rs"`) and reads each from the name with
  `../../unix/shared/` replaced by `../shared/` (`ponytail:` comment: the names are the cache's identity, not paths);
  `cargo:rerun-if-changed` prints the read path (a missing path would rerun the script every build); `MODULE_ROOTS`
  unchanged. The 11 hashed files (`src/d3d9/core/src/dxso/**` minus `tests.rs`/`*_tests.rs`, `dxso.rs`,
  `vs_draw.rs`, `ps_draw.rs`, `src/d3d9/shared/src/{mtl,shader_create}.rs`) stay byte-identical through R: R1f's
  formatter has no hunk there (measured 2026-10-30: core's hunks are in `page_box.rs`, shared's in `lib.rs`), no clippy
  fix may touch them, and R4b's sweep leaves their stale comments (`dxso/ff.rs:1072`, `dxso/emit.rs:1561`,
  `shared/src/mtl.rs:18`) to C3, which retires the emitters. — A comment edit there changes `SHADER_EMITTER_VERSION`
  and every player's D3D9 cache regenerates. — three stale comments until C3.
- **P5 The nightly pin names the installed toolchain.** `MTLD3D_RUST_NIGHTLY=nightly-2026-10-30` (the installed
  nightly's channel manifest says `date = "2026-10-30"`; its rustc is `1.101.0-nightly (92b5b8c87 2026-10-29)`).
  rust-check.sh runs `$RUST_NIGHTLY_BIN/cargo fmt` (default `$R/nightly-aarch64-apple-darwin/bin`) and refuses unless
  `$RUST_NIGHTLY_BIN/../lib/rustlib/multirust-channel-manifest.toml`'s `date` equals the pin's date. — Rustup names a
  nightly by its build date, one day after rustc's commit date; Homebrew rustup can't `rustup run nightly`. — A newer
  nightly installed over it fails the fmt leg by name until the pin moves.
- **P6 One home for the per-target C environment.** `tools/mingw-cc.sh env <rust-target> <links-dir>` makes the four
  links (`mingw-cc`, `mingw-c++`, `linux-cc`, `linux-c++` → itself) in `<links-dir>`, each only through an atomic rename
  (`ln -s "$self" "$d/.$n.$$" && mv -f "$d/.$n.$$" "$d/$n"`: ninja runs the `d3d9-*`/`mtld3d-*` targets in parallel and
  `ln -sf` unlinks a link another job's cc-rs is exec'ing, step 2's race), and prints `export` lines for cc-rs in the
  underscore spelling (`CC_aarch64_pc_windows_msvc`, `CXXFLAGS_x86_64_unknown_linux_gnu`, …: sh's `export` refuses the
  hyphenated names and `set -e` kills the caller; cc 1.8.0 reads `{VAR}_{target with - and . → _}`,
  `cc-1.8.0/src/lib.rs:4908-4912`, and concatenates every variant, `:4937-4952`) for `CC_`, `CXX_`, `CFLAGS_`,
  `CXXFLAGS_<t>`, Linux also `AR_<t>`, `CARGO_TARGET_<T>_LINKER`, `MTLD3D_LINUX_TRIPLE`, plain `CFLAGS`/`CXXFLAGS`
  where today's build had them — exactly today's values (table in Task 3/4). `cargo-staticlib.sh` applies it only when
  `MTLD3D_LLVM_MINGW` is set, which only `src/d3d9`'s meson targets pass (custom_target `env:`), so nvngx, nvapi and
  winemetal build exactly as today. `MTLD3D_LLVM_MINGW` = the parent of the directory of meson's cross C compiler,
  resolved: `fs.parent(fs.parent(find_program(cc.cmd_array()[0]).full_path()))` (meson keeps the cross file's bare
  name, `intro-compilers.json`: `['arm64ec-w64-mingw32-gcc']`; llvm-mingw's `bin` is on `PATH` in all three builds),
  asserted absolute at configure. `tools/arm64x-link.sh` runs `<llvm-mingw-bin>/lld-link` only (never `PATH`'s, which
  is rust-lld LLD 23.1.1) and prints its `--version` into the build log. — No meson option (translator key), no `PATH`
  lookup for the linker. — none.
- **P7 The shim's Wine archive.** `src/d3d9/shim/build.rs` reads `MTLD3D_WINECRT0`, the absolute path of
  `libwinecrt0.a`, which `src/d3d9/meson.build` computes itself (`cc.find_library` returns no path): the first of
  `wine_build_path/libs/winecrt0/<dir>/libwinecrt0.a`, `wine_build_path/dlls/winecrt0/<dir>/libwinecrt0.a` (Mac; only
  `libs/` exists today) or `wine_install_path/lib/wine/<dir>/libwinecrt0.a` (guest) that `fs.is_file()` finds, `<dir>`
  = `windows_builtin_install_dir`, `error()` if none. It always bundles `unix_lib.o` (the cdylib branch goes with the
  cdylib crate type). `cfg(mingw_crt)` and `MTLD3D_MINGW_CRT` become `cfg(windows)`. — B6; the Mac's Wine install for
  mtld3d goes. — none.
- **P8 Guest link line.** The guest `shared_library` links name `-Wl,--gc-sections -Wl,--icf=all` (the hand link's
  `/opt:ref,icf`; lld's MinGW driver does no ICF by default) and `-lmingw32 -lmingwex -lucrt -lkernel32 -lmincore
  -luser32 -ladvapi32 -lgdi32 -lws2_32 -luserenv -lbcrypt -ldbghelp` in that order (the hand link's,
  `<M>/dxmt/mtld3d-build.sh:81`: the CRT first, so `strlen`/`wcslen`/`mem*` keep resolving to ucrt's `api-ms-win-crt-*`
  sets ahead of Wine's `libntdll.a`, which meson's line places after `link_args` and which defines all six — today's
  meson-linked `winemetal.dll` imports `memcpy`/`memset`/`memcmp`/`wcslen` from `ntdll.dll`; `kernel32` before
  `mincore`, so no import moves from `KERNEL32.dll` to an `api-ms-win-*` set). Expected import set: Task 0's minus
  `ntdll.dll!__chkstk` (ED2). Meson appends `-Wl,--subsystem,console` after `link_args` and lld takes the last
  `--subsystem`, so the subsystem flip is ED9, not a link flag. — Today's x86_64 `d3d9.dll` imports 13 DLLs, two of
  them through mincore's sets. — none; the import tables and section sizes are compared.
- **P9 cdylib mode.** An out file ending in `.so` selects it: darwin adds `-C link-arg=-Wl,-S` to the remap rustflags and
  copies `lib<crate>.dylib`; Linux runs through `tools/linux-cargo.sh` and `llvm-objcopy --strip-debug`
  (`$MTLD3D_LLVM_MINGW/bin`). `src/d3d9/unix/install.sh` copies without `strip -x` (the Mac `mtld3d.so` keeps its
  symbols as today; a release strips it, `bundle.sh:198`). — G1. — none.
- **P10 `tools/linux-cargo.sh` guards.** It requires `MTLD3D_RUST`, `MTLD3D_LLVM_MINGW`, `MTLD3D_SYSROOT` and an absolute
  `CARGO_TARGET_DIR`, one `${X:?…}` line each. — R-D7: no default lands in the tree. — none.
- **P11 rust-check.sh inputs.** `MTLD3D_RUST` (default: `meson.options`' `cargo_toolchain`), `MTLD3D_RUST_NIGHTLY`,
  `MTLD3D_LLVM_MINGW`, `WINE_BUILD` (the Mac Wine build tree), `GUEST_WINE_SDK` (`$W/guest-wine-sdk`), optional
  `RUST_NIGHTLY_BIN` and `CARGO_TARGET_DIR`; no sysroot and no Linux leg (no leg of R-D12 compiles a Linux target; the
  guest build and `d3d9-gate` do). Its audit leg refuses while `git ls-files --others --exclude-standard` lists a
  `.rs`, `.toml` or `.md` (the audit skips untracked files). — The critic's question 5 listed the sysroot; nothing reads
  it. — Linux-only code stays unlinted (pre-existing, D4).
- **P12 Lanes.** R2 then R3 on the owner's branch (both edit `src/d3d9/meson.build`, `src/d3d9/unix/meson.build`,
  `tools/cargo-staticlib.sh`, `tools/mingw-cc.sh`); R4a a lane from R3's commit (it needs `mingw-cc.sh env`'s x86 msvc
  rows, which R3 adds); R4b a lane
  from R1f; RM a MacNeutron lane from R1's commit. R4b never edits R2/R3's files, the 11 hashed files or
  `docs/CONVENTIONS.md` (R4a's). — Disjoint files only. — none.
- **P13 MacNeutron's one commit.** RM commits on `stepR-mn` (worktree `$S/wt-mn`); the end gates run with the main
  checkout switched to `stepR-mn`; the controller squashes it with the pin into one `dxmtrs` commit (`git merge
  --squash`). — No `dxmtrs` commit names the new layout with the old pin (spec §3 "in its pin bump"); no rebase of
  `dxmtrs`. — none.
- **P14 Package metadata.** mtld3d crates state `license = "Zlib"`, `authors`, `description`, `repository`/`homepage`
  (`https://github.com/athei/mtld3d`), `keywords`, `categories` literally; `version`, `edition`, `rust-version` stay
  workspace-inherited. Only `CARGO_PKG_VERSION` is read by code (`shared/src/identity.rs`). — L7. — none.
- **P15 The root `Cargo.lock`** merges both locks' `[[package]]` entries per `(name, version)` (a scratch `python3 -I`
  merge): one entry each, its `dependencies` the union of both entries' (the locks differ in contents: `dispatch2` and
  `objc2-core-foundation` gain `block2`/`libc` on the unix side, `mtld3d-core` its dev-dependencies `objc2`,
  `objc2-metal`, `objc2-foundation`, `snmalloc-rs` on the windows side), and `snmalloc-sys 0.7.6` only as the unsourced
  patch entry (the unix registry entry and its checksum go: the root `[patch]` replaces it, and two entries would make
  `snmalloc-rs`'s bare reference ambiguous). Checked by `cargo metadata --locked --offline`. If that still fails: the
  merge is a seed, `cargo metadata --offline --format-version 1 > /dev/null` runs once without `--locked` (it keeps
  locked versions, only prunes and adds edges), the lock's `name version` set must equal both locks' union minus the
  registry `snmalloc-sys`, and `--locked` holds from then on (ledgered). Never `cargo update` or `generate-lockfile`
  (they re-resolve). — Both locks agree on every shared name and version (critic, verified). — none.
- **P16 `default-members`** = the old unix default members (`mtld3d-unix`, `mtld3d-mtl`, `mtld3d-shared`,
  `mtld3d-conformance`, `mtld3d-e2e`, `winemetal-wire`) + `mtld3d-core`, `mtld3d-types`; `cargo metadata`'s
  `workspace_members` must list exactly the 16 (else `exclude = ["vendor/snmalloc-sys"]`). — none.
- **P17 Generated files** are skipped by `rustfmt.toml`'s `ignore = ["src/winemetal/wire/src/layouts.rs",
  "src/winemetal/src/protos.rs"]` (a nightly option, as the file's others). — One line; the generators stay as they
  are. — none.
- **P18 The audit is not a check of R1-R3**: R1 moves `scripts/audit.sh` unchanged to `tools/`, R4a repoints it. — It
  names `windows/`/`unix/` on 52 lines. — An R1-R3 audit finding surfaces at R4a.
- **P19 The licence map** also names `tools/arm64x-link.sh` (ported from mtld3d's Makefile, zlib). `tools/mingw-cc.sh`
  and `tools/linux-cargo.sh` are the maintainer's MacNeutron code, contributed under DXMTRS's licence. — zlib clause 2.
  — none.
- **P20 R-D15's program**: MacNeutron's `Makefile` builds `build/dxmt-tests-arm64/d3d9_triangle.exe`
  (`$(MINGW_BIN)/aarch64-w64-mingw32-clang++ -O2 -static -s -std=c++17 -o $@ $< -ld3d9 -luser32`) as part of
  `dxmt-tests`; `d3d9-check.sh` runs it once as rows `arm64: module clear triangle present`, through the launcher, or,
  if the launcher does not start an aarch64 PE natively, through the tool folder's `bin/wine` in the same prefix
  (ledgered which). — none.
- **P21 Conformance prefix**: the inlined recipe always takes the session path (three `reg add`, `wineserver -k`,
  `-w`, `-p`, `wineboot`, under `lockf` on the prefix); the Makefile's "already configured" fast paths are not ported. —
  R-D22. — a few seconds a run.

## Global Constraints

- Agents never push, merge to `main`, fetch or download, except locked crate fetches (`cargo fetch --locked`, RM's
  fresh-home check). The final task's push, pin and pinned re-fetch run under the maintainer's standing authorization,
  by the controller only. Kill by exact PID; no MacNeutron.app, Steam or game; never print SteamIDs or account names;
  privacy-scan before pushes; no bench (`0-design.md` §2.6 item 5). Home written `~`. Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. DXMTRS commit subjects are `<area>: sentence`.
- DXMTRS: branch `stepR-layout` from `main` da6ed2d in `$D`; lanes branch from it (Execution). MacNeutron: `dxmtrs`
  (cec2527 + this plan), lane branch `stepR-mn`. Do not touch `$D`'s git state before Task 0 Step 4.
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE` exported. The builder starts with `VM_SHARES` per the builder-out-share note. From
  Task 0 Step 4 the DXMT tree is "development" (two branches, worktrees), so builds take the working tree; the pinned
  path re-fetches and resets the push URL (re-set to `git@github.com:Mac-Neutron/DXMTRS.git`).
- Any `make` or `<M>/dxmt/*.sh` run from a MacNeutron worktree exports `BUILD_DIR=$B`.
- Meson builds outside MacNeutron's scripts (Tasks 1, 3, 4, 8): `PATH="$(sh dxmt/mtld3d-tools.sh):$(sh
  dxmt/toolchain.sh):$PATH"`, then `meson compile -C <build dir>` and `meson install -C <build dir> --destdir <dir>` (never
  into `$SRC/dxmt-install` or `$W/dxmt-guest`); guest compiles export `MTLD3D_SYSROOT="$(sh eac-vm/sysroot/fetch.sh
  x86_64)"` (`i686` for `dxmt-guest-build32`).
- Rust, `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH` with a bare `cargo` (a `+toolchain` needs rustup's proxy:
  only scripts that put `dxmt/mtld3d-tools.sh`'s folder first use it), always `--offline`, `CARGO_TARGET_DIR` outside `$D` (lanes:
  `$S/cargo-<lane>`); old-tree runs (Task 0, Task 2 Step 1) from `$D/mtld3d/{windows,unix}`, all others from `$D`.
- **The PE environment by hand (Tasks 0-2, before `mingw-cc.sh env` exists):** `T=$B/mtld3d-tools/bin LM=$(dirname
  "$(sh dxmt/toolchain.sh)")`; per target `t`: `env PATH="$R/1.99.0-aarch64-apple-darwin/bin:$T:$PATH"
  MTLD3D_LLVM_MINGW=$LM "CC_$t=$T/mingw-cc"
  "CXX_$t=$T/mingw-c++" "CXXFLAGS_$t=-fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP
  -include stdlib.h"` and `WINE_SDK=$SRC/wine-sdk/usr/local` (aarch64, arm64ec) or `WINE_SDK=$W/guest-wine-sdk` (x86_64,
  i686). The toolchain's own `bin` comes before `$T`: `$T/cargo` is rustup's proxy, which runs the default `stable`
  (rustc 1.99.1, no msvc standard library; step 2's trap); `$T` still supplies `lld-link` and `llvm-lib`. From Task 3
  on (x86 rows from Task 4 on): the same `PATH`, `eval "$(sh $D/tools/mingw-cc.sh env $t $S/cc-links)"` and
  `MTLD3D_WINECRT0=<archive>`.
- **The clippy matrix (CM)**, 1.99.0, `-- -D warnings`, `--keep-going`:
  - `i686-pc-windows-msvc`: `-p d3d9 -p mtld3d -p mtld3d-core -p mtld3d-types -p mtld3d-shared -p winemetal-pe`, and
    `-p mtld3d-tests --all-targets`;
  - `x86_64-pc-windows-msvc`, `aarch64-pc-windows-msvc`: the same plus `-p nvngx -p nvapi -p dxmt-util`, and `-p
    mtld3d-tests --all-targets`;
  - `arm64ec-pc-windows-msvc`: the x86_64 set without `mtld3d-tests`;
  - `aarch64-apple-darwin`: `--all-targets -p mtld3d-core -p mtld3d-types -p mtld3d-shared -p mtld3d-unix -p mtld3d-mtl
    -p mtld3d-conformance -p mtld3d-e2e -p winemetal-wire -p winemetal -p dxmt-util -p nvapi -p nvngx --features
    mtld3d-core/disk-cache`; then `-p mtld3d-unix --features mtld3d-unix/winemetal --all-targets`; then `-p winemetal
    --features native`;
  - perf (`MTLD3D_PERF=1`, its own target dir): the darwin `-p mtld3d-core -p mtld3d-unix` line and the i686 line.
- **The unit tests (UT)**, host, 1.99.0: `cargo test -p mtld3d-core -p mtld3d-types --features mtld3d-core/disk-cache`;
  `cargo test -p mtld3d-unix -p mtld3d-mtl -p mtld3d-shared -p mtld3d-conformance -p mtld3d-e2e -p winemetal-wire`; the
  same two with `MTLD3D_PERF=1` (own target dir); `cargo test -p dxmt-util -p nvapi -p nvngx -p winemetal --config
  'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'`; all `--target
  aarch64-apple-darwin`.
- **Doc**: `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps` for the i686 CM `-p` list (`--features
  mtld3d-core/disk-cache`) and the first darwin CM `-p` list.
- **fmt**: `PATH=$R/nightly-aarch64-apple-darwin/bin:$PATH cargo fmt --all --check` from `$D`.
- **Audit**: `git -C <tree> add -A` first; before R4a `sh scripts/audit.sh` from `<tree>/mtld3d`, from R4a on `sh
  tools/audit.sh` → `audit: clean (<n> files)`, exit 0.
- **RC** (the gate, from Task 6 on): `sh -c 'ROOT=$PWD; . dxmt/pins; LM=$(dirname "$(sh dxmt/toolchain.sh)");
  git -C build/wine-arm64-src/dxmt add -A; PATH="$(sh dxmt/mtld3d-tools.sh):$LM/bin:$PATH" MTLD3D_RUST=$MTLD3D_RUST
  MTLD3D_RUST_NIGHTLY=$MTLD3D_RUST_NIGHTLY MTLD3D_LLVM_MINGW=$LM WINE_BUILD=$PWD/build/wine-arm64-src/wine-build
  GUEST_WINE_SDK=$PWD/build/eac-vm-work/guest-wine-sdk CARGO_TARGET_DIR=$PWD/build/d3d9-cargo/check sh
  build/wine-arm64-src/dxmt/tools/rust-check.sh'` → `rust-check: ok`.
- Translator key: `sh -c 'ROOT=$PWD; . dxmt/pins; . wine-arm64/lib.sh; PATH="$(sh dxmt/toolchain.sh):$PATH";
  translator_key build/wine-arm64-src/dxmt release'` = `71a7e5f22c50f03e79c56344f7a11819f2f922e67fe9ebe4f84ed9761d763311`.
- Emitter version: `cargo run --offline -q --release -p mtld3d-core --features mtld3d-core/disk-cache --example
  emit_corpus --target aarch64-apple-darwin -- --iters 1 | sed -n 's/.* emitter \([0-9a-f]*\)$/\1/p'` = Task 0's value.
- Tree clean: `git -C $D status --porcelain` lists only the task's own edits, and nothing after a commit, a build or a
  gate (A3).
- zsh: quote `"====="`; unquoted `$var` is not split. Gating scans use `/usr/bin/grep`.

### Identity facts (IF) and expected differences (ED)

Every task preserves the IFs; `$S/ident-diff.sh` passes only the EDs.

- **IF1** Names and places: wine.app `lib/wine/aarch64-windows/{d3d9,mtld3d}.dll`, `lib/wine/aarch64-unix/mtld3d.so`;
  guest stage `{x86_64,i386}-windows/{d3d9,mtld3d}.dll`, `{x86_64,i386}-unix/mtld3d.so`; `licenses/mtld3d/`.
- **IF2** Mac pair: `IMAGE_FILE_MACHINE_ARM64X (0xA64E)` with a CHPEMetadata block, Wine builtin marker, `d3d9.dll`
  imports `mtld3d.dll`, exports equal `d3d9.def` (both views, counted per view) and `mtld3d.dll` exports only
  `mtld3d_unix_call`, VERSIONINFO values, linked by the ledgered LLD (Task 0: llvm-mingw's `lld-link`, LLD 24.0.0; the
  build log's `--version` line).
- **IF3** Guest pairs: machine, builtin marker, exports equal the `.def`, import DLLs and symbols equal (ED2).
- **IF4** `mtld3d.so`: Mac `nm -gU`, sorted `otool -L`, install name `@rpath/mtld3d.so`, minos 27.0; Linux ELF machine,
  both `__wine_unix_call*_funcs`, NEEDED set.
- **IF5** `SHADER_EMITTER_VERSION` = Task 0's; the 11 hashed files renamed only: `git -C $D diff -M --name-status
  da6ed2d <rev>` (before a commit: `--cached da6ed2d`) lists each of them as `R100 mtld3d/… src/d3d9/…` (no pathspec:
  a rename is detected only when both paths are in it).
- **IF6** Translator key `71a7e5f2…3311`.
- **IF7** Package names, version 0.14.1, every `Cargo.lock` version; `cargo tree` per shipped package and target equal
  Task 0's (ED6).
- **IF8** `version.rc` strings, `MTLD3D_CONFIG`, `mtld3d.conf`, log locations, `mtld3d_shaders.bin`: no code edit;
  checked by P3's string set per shipped binary, equal before and after.
- **IF9** Assert strings (`assert fail: {} in {} on {} `, `_assert`/`__assert_rtn` imports): Mac 0, guest PE Task 0's
  count, guest Linux Task 0's count (8 measured by map 2) until R5, then 0 everywhere.
- **IF10** `nvngx.dll`, `nvapi64.dll`, `winemetal.dll`/`.so`/`.dylib` (Mac, guest, native): equal but ED1/ED4.

- **ED1** Remapped path strings (`mtld3d/windows/<c>/` → `dxmtrs/src/…`, `mtld3d/unix/<c>/` → `dxmtrs/src/…`,
  `tests/d3d9/…`), and panic-location line numbers in files R1f or R4b reformatted or re-commented.
- **ED2** Guest DLLs: `ntdll.dll!__chkstk` gone (`rust_pe_rt.c` defines it, `0-design.md` §0 item 3).
- **ED3** `MTLD3D_BUILD` (`git describe` of the new commit, `shared/build.rs:20,72`).
- **ED4** Section sizes (meson's driver link vs the hand link; R1f/clippy code edits): ledgered; a section more than
  3% off is explained (symbols that moved, a link flag) before Task 11, not a pass/fail row.
- **ED5** Mac `mtld3d.so`: no OSO debug map and `$HOME` count 0 (`-Wl,-S`, G1). Guest `d3d9.dll`'s resource now comes
  from llvm-windres instead of llvm-rc (R-D5): the resource directory (type, ID 1, language 1033) and the VERSIONINFO
  fixed and string values stay equal; a byte difference is ledgered with what differs (C7).
- **ED6** `cargo tree`: `snmalloc-sys`'s source becomes `vendor/` on darwin and Linux (L5); `objc2`'s
  `disable-encoding-assertions` in `mtld3d-core`'s dev-dependencies (C2); local path strings.
- **ED7** After R5: the guest's assert code gone (sizes shrink, IF9 → 0).
- **ED8** Licence folder (`licenses/mtld3d/`, `diff -r` against `$S/before/mac/licenses/mtld3d`): `snmalloc-sys`'s
  INDEX line loses "(MIT's text written here: the crate ships none)" and its folder's `LICENSE-MIT` becomes `LICENSE`
  with the same bytes; folders and INDEX lines for crates only the new R-D19 legs link are added (exactly Task 0's
  `notices-legs.txt` minus `notices-INDEX`'s set); `rust/`, `LICENSE` and every other file byte-equal.
- **ED9** Guest PE DLLs (meson's driver link, R3): Subsystem `WINDOWS_CUI (0x3)` for `WINDOWS_GUI (0x2)` (meson appends
  `-Wl,--subsystem,console` after `link_args`; the loader ignores a DLL's subsystem; DXMT's other guest DLLs are CUI
  already; the maintainer rules on it before Task 11, ledgered), FileAlignment 4096 for 512 (root `meson.build:92`), a
  `.buildid` section and its CodeView debug-directory entry.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes and keeps the ledger. One **pipeline owner**
runs everything that uses a shared build dir, the VM or a quiet machine (`make wine-arm64`, meson in `$SRC/dxmt-build`,
`$W/dxmt-guest-build{,32}`, `build/dxmt-native`, `eac-vm/*`, `dxmt/check.sh`). **Lanes** use host tools only (cargo,
clippy, rustdoc, the audit, editors) with `CARGO_TARGET_DIR=$S/cargo-<lane>`.

**Lane procedure.** DXMTRS: `git -C $D worktree add $S/wt-<lane> -b stepR-<lane> <base>`; fill the worktree's
`include/native/directx` and `external/nvapi` read-only from the main checkout's submodules (`git -C $D/<path> archive
HEAD | tar -x -C $S/wt-<lane>/<path>`, `chmod -R a-w`; `chmod -R u+w` before `git worktree remove`). MacNeutron: `git
worktree add $S/wt-mn -b stepR-mn dxmtrs`. Commit in the lane; the controller rebases onto `stepR-layout`,
fast-forwards, ledgers; a lane branch is deleted only after `git cherry` shows nothing unmerged.

**Most of R is sequential**: `git mv` touches every crate, and R2 and R3 share their files (P12). Lanes only where
files are disjoint:

| Task | Runs | Depends on |
|---|---|---|
| 0 R0: before-artifacts, references | owner (Sonnet) | — |
| 1 R1: layout and one workspace | owner (Opus) | 0 |
| 2 R1f: format commit, clippy fixes | owner (Sonnet) | 1 |
| 3 R2: the Mac's D3D9 from meson | owner (Opus) | 2 |
| 4 R3: the guest's D3D9 from meson | owner (Opus) | 3 |
| 5 R4b: documents and sweeps | lane `r4d` from 2's commit (Sonnet) | 2 |
| 6 R4a: rust-check.sh, the audit, the inventory | lane `r4t` from 4's commit (Sonnet) | 4 |
| 7 RM: MacNeutron's edits | lane `mn` writes after 1; owner integrates and builds after 4 and 6 (Opus) | 1, 4, 6 |
| 8 R5: NDEBUG | owner (Sonnet) | 4-7 |
| 9 End-of-step gates, once | owner (Sonnet) | 8 |
| 10 One fix pass | owner and lanes (Opus) | 9 |
| 11 Spec, push, pin, step 3's plan | controller | 10 |

Merge order on `stepR-layout`: 1, 2, 3, 4, then 5 and 6 (rebased), the merged-tree check (Task 8 Step 0), then 8.
Reviews on Opus.

## Rules check (CLAUDE.md, tree-wide from R4b)

- **Statics:** none added. One removed: d3d9's x86 `_fltused` (`src/d3d9/src/arm64_crt.rs`; `rust_pe_rt.c` defines it,
  B5). No `OnceLock` outside `ONCELOCK_SITES` (paths repointed only).
- **Config keys:** none. Build-time variables: `MTLD3D_WINECRT0` (new, replaces the shim's `WINE_SDK`),
  `MTLD3D_LLVM_MINGW`, `MTLD3D_SYSROOT`, `MTLD3D_LINUX_TRIPLE` (moved with the scripts), `MTLD3D_RUST_NIGHTLY` (new
  `dxmt/pins` key), rust-check.sh's `WINE_BUILD`, `GUEST_WINE_SDK`, `RUST_NIGHTLY_BIN`. `MTLD3D_MINGW_CRT` retires (P7).
  Runtime names unchanged (IF8).
- **Wire fields:** none.
- **Dependencies:** none added or upgraded; one lockfile, the union (P15); the `snmalloc-sys` patch now reaches darwin
  and Linux (L5, identity-checked). Notices: `mtld3d-notices.py` covers every shipped package and target (R-D19).
- **Derives:** none; `tools/derive_inventory.txt` regenerated for paths only (R-D4).
- **Lint suppressions:** none, except `mtld3d-mtl` (controller ruling (a), 2026-10-30: it keeps no `[lints] workspace =
  true` until C5 retires it; Task 2 Step 0 is answered) as measured at Task 2 Step 0 on `mtld3d-mtl` (243 findings measured
  by the identity review with the workspace table on darwin: ~139 truncating/wrapping casts, 17
  `missing_const_for_fn`, 4 `significant_drop_tightening`). Ruling (a): `mtld3d-mtl` keeps no `[lints] workspace =
  true` until C1/C5 rewrite or retire it — one stated exception (C8 allows "an exception on purpose"), with the count.
  Ruling (b): it gains the line (C8) and its findings are fixed under Task 2 Step 3's binding rules, none allowed.
- **Duplicates (for §7):** two sample configs at the root (`dxmt.conf`, `mtld3d.conf`, until C2); two resource
  compilers (llvm-rc Mac, windres guest, C7); `mtld3d-mtl`'s `wmt.rs` vs `winemetal-wire` (D16, until C5). Removed: the
  C-environment copies in MacNeutron's `mtld3d-build.sh`/`mtld3d-linux-cargo.sh` (now `mingw-cc.sh env`); the
  duplicate `_fltused`.

## Review Focus

1. **A player's D3D9 shader cache regenerates** because a path rename, a format hunk or a sweep touched an emitter
   fingerprint input. Expected: `mtld3d_shaders.bin` stays valid. Tests: IF5 after Tasks 1, 2 and 5, in Task 9, after
   the fix pass (Task 10 Step 3, whose fixes may not touch the 11 files) and on the pinned tree (Task 11 Step 4)
   (emitter value and the 11-file rename-only diff); `dxso::tests::every_module_the_emitter_names_is_fingerprinted` in
   UT.
2. **A native arm64 D3D9 process** (an arm64 Windows program) loads `d3d9.dll`'s native half, which no x64 test
   reaches. Expected: it draws. Tests: Task 3's IF2 tables (`/defarm64native` exports, ARM64X machine); Task 7's
   `arm64` d3d9-check row (P20), run in Task 9.
3. **A 32-bit game in the VM**: the i386 pair now linked by meson's driver (`--enable-stdcall-fixup`, `--kill-at`,
   `__tls_array` from `rust_pe_rt.c`, the `_fltused` duplicate). Expected: i386 exports equal `d3d9.def`, imports as
   before but ED2, the game draws. Tests: Task 4's i386 tables; Task 9's `d3d9-gate` x86 lane (`tri32-*`, the
   `i686-arm64` conformance figures) and `d3d-smoke` `32:` rows.
4. **MacNeutron's build turns "development" or the VM stamp `+dirty` after R** (cargo output, `.codex`, a leftover
   `mtld3d/` dir in the tree). Expected: the pinned build says pinned, `DXMT/version` = `<sha>+<series>`,
   `.mn-source` has no `+dirty`. Tests: the tree-clean check after every build (Global Constraints); Task 11 Step 4's
   pinned `DXMT/version` and `git status --porcelain` empty.
5. **A fresh Mac or a cleared cargo registry**: meson's `--offline` cargo finds nothing (A4). Expected: the build
   fetches the locked crates and builds. Test: Task 7 Step 6's `eac-vm/dxmt-native/build.sh` on an empty
   `CARGO_HOME`, the fetch line's place in the three scripts, and the offline tree per shipped package and target.

---

### Task 0 (R0): the before-artifacts and references (owner)

**Files:** MacNeutron `dxmtrs`: this plan (commit). Create (uncommitted): `$S/ident.sh`, `$S/ident-diff.sh`,
`$S/before/**`, `$S/ref-wine.app`, `$S/ref-vm`, `$S/ref-present`, the ledger.

**Interfaces:**
- Produces: `$S/before/{mac,guest,native}/` (binaries + `tables/<name>.txt`), `$S/before/mtld3d-target/` (moved build
  dirs), `$S/before/cargo/<pkg>@<target>[+<features>].txt`, `$S/before/cargo/unified-darwin.txt`, `$S/before/emitter.txt`,
  `$S/before/key.txt`, `$S/before/audit.txt`, `$S/before/notices-INDEX` (wine.app's `licenses/mtld3d/crates/INDEX`),
  `$S/before/notices-legs.txt`, `$S/before/toolchain.txt`, `$S/before/flags/`, `$S/before/tests/`,
  `$S/before/packages.json`; `$S/ident.sh`, `$S/ident-diff.sh` (P3); `$S/ref-wine.app`, `$S/ref-vm` (264 files),
  `$S/ref-present`.

- [ ] **Step 1: Plan.** Commit this plan on `dxmtrs` (`Plan: DXMTRS step R, D3D9 at src/d3d9`). Ledger:
  `git -C $D rev-parse HEAD` = da6ed2d, `cat $A/DXMT/version` = `da6ed2d…+da6ed2d418e6`, the key (Global Constraints).
- [ ] **Step 2: Rebuild guest and native at da6ed2d** (P1): builder up, `sh eac-vm/dxmt/build.sh`, `sh
  eac-vm/dxmt-native/build.sh`. Expected: rc 0; `$W/dxmt-guest-build/.setup-inputs` and `-build32`'s end in da6ed2d.
- [ ] **Step 3: Save.** Write `$S/ident.sh`/`ident-diff.sh`; copy and tabulate: Mac — `$A/lib/wine/aarch64-windows/
  {d3d9,mtld3d,winemetal}.dll`, `$A/lib/wine/aarch64-unix/{mtld3d,winemetal}.so`, `$A/DXMT/aarch64-windows/
  {nvngx,nvapi64}.dll`, `$A/licenses/mtld3d/`; guest — `$W/dxmt-guest/**`, `$B/mtld3d-guest/**`; native —
  `$B/dxmt-native/install/lib/winemetal.dylib`. Cargo, from the old workspaces: for each (package, target) of IF7's
  matrix — `d3d9`, `mtld3d`, `winemetal-pe` × the four msvc targets; `nvngx`, `nvapi` × aarch64/arm64ec/x86_64 msvc;
  `mtld3d-unix` × `aarch64-apple-darwin` (also `+winemetal`), `x86_64-`/`i686-unknown-linux-gnu`; `winemetal` ×
  `aarch64-apple-darwin` (also `+native`) — `cargo tree --locked --offline -e normal,build,features --prefix none
  --format '{p} {f}' -p <pkg> --target <t> [--features …] | sed 's| (/[^)]*)||; s/ (\*)$//; s/ (command-line)//' |
  sort -u` (the markers alone differ between a `-p` and a whole-workspace listing); `unified-darwin.txt` = the
  same with `-p` for each old unix default member together on `aarch64-apple-darwin` (the whole-workspace build that
  made the Mac `mtld3d.so`). `notices-legs.txt` = the sorted unique `name version` of every non-workspace package of
  `cargo tree --locked --offline -e normal --prefix none --format '{p}'` (`mtld3d-notices.py`'s edge kind, so no
  `cc`/`cmake`) over exactly R-D19's legs in the old workspaces. `toolchain.txt`: `$LM/bin/lld-link --version` (LLD
  24.0.0), `rustc -vV` of 1.99.0. `flags/`: per shipped (crate, target), the newest `.fingerprint/<crate>-*/lib-*.json`'s
  `rustflags` and the `= Some(` lines of each C build script's `output` (snmalloc-sys, zstd-sys where linked) from the
  before target dirs (`$D/mtld3d/{windows,unix}/target*`, which Step 4 moves; the guest's from Step 2's rebuild),
  and a `python3 -I`
  `tomllib` dump of both old `[profile.*]` tables. `tests/`: UT's command sets in the old workspaces (also with
  `MTLD3D_PERF=1`) with `-- --list`, the test count per test binary. `packages.json`: `cargo metadata --no-deps
  --format-version 1` per old workspace, each package's name, version, license, authors, description, repository,
  homepage, keywords, categories. The emitter version (from `$D/mtld3d/windows`, `CARGO_TARGET_DIR=$S/cargo-r0`); the
  audit (`sh scripts/audit.sh` from `$D/mtld3d`; step 2 ended at 688 files).
  RED of the identity scripts: copy `$S/before/guest` to `$S/ident-red`; in its tables delete one export line, rename
  one import symbol, change one non-path string and one header field (Subsystem) → `ident-diff.sh $S/before/guest
  $S/ident-red` prints all four; a copy where only a path prefix changes (`mtld3d/windows/core/` →
  `dxmtrs/src/d3d9/core/`) → `ident-diff.sh … ED1 ED4` prints nothing, and without `ED1 ED4` prints it; `rm -rf
  $S/ident-red`.
  Expected: a table for every saved binary (18: Mac 7, guest 10, native 1); `ident-diff.sh $S/before $S/before` prints
  nothing; the RED above; Mac assert count 0, guest Linux 8 each (record the guest PE counts); `d3d9.dll` x86_64
  imports 13 DLLs; the ARM64X `d3d9.dll`'s two export views counted apart.
- [ ] **Step 4: Clear the tree** (P1): `mv $D/mtld3d/windows/target $S/before/mtld3d-target/windows` and
  `$D/mtld3d/unix/target` likewise (and any `target-wm`, `target-linux*`). Expected: `git -C $D status --porcelain
  --ignored` lists nothing under `mtld3d/`. Then `git -C $D switch -c stepR-layout`.
- [ ] **Step 5: References** (P2): `cp -cR $B/wine-arm64/wine.app $S/ref-wine.app`; `sh eac-vm/dxmt/share.sh`
  (`MN_DXMT_ONLY` unset), then `MN_REF_RUNS=2 MN_REF_OUT=$S/ref-vm MN_REF_WINE_APP=$S/ref-wine.app sh eac-vm/dxmt/ref.sh`;
  `cp -cR build/step2/ref-present $S/ref-present`. Expected: 264 files in `$S/ref-vm`, every `.status` 0 (a non-zero one
  is ledgered).

### Task 1 (R1): layout and one workspace (owner)

**Files (DXMTRS, all `git mv` unless marked):**
- `mtld3d/windows/d3d9/*` → `src/d3d9/` (`Cargo.toml build.rs src/ d3d9.def version.rc`); `windows/shim` →
  `src/d3d9/shim`; `unix/unix` → `src/d3d9/unix`; `windows/core` → `src/d3d9/core`; `windows/types` → `src/d3d9/types`;
  `unix/shared` → `src/d3d9/shared`; `unix/mtl` → `src/d3d9/mtl`; `mtld3d/LICENSE` → `src/d3d9/LICENSE`, plus unaltered
  copies `tests/d3d9/LICENSE`, `docs/d3d9/LICENSE` (new).
- `windows/nvngx/{Cargo.toml,src}` → `src/nvngx/`; `windows/nvapi/{Cargo.toml,src}` → `src/nvapi/`;
  `windows/winemetal-pe/{Cargo.toml,src}` → `src/winemetal/`; `unix/winemetal/{Cargo.toml,src}` → `src/winemetal/unix/`;
  `unix/winemetal-wire` → `src/winemetal/wire`; `windows/dxmt-util/{Cargo.toml,src}` → `src/util/`.
- `windows/tests` → `tests/d3d9/programs`; `unix/e2e` → `tests/d3d9/runner`; `unix/conformance` →
  `tests/d3d9/conformance`; `scripts/cursor_{startup,transaction}_probe.swift` → `tests/d3d9/probes/`.
- `windows/vendor/snmalloc-sys` → `vendor/snmalloc-sys`, plus `vendor/snmalloc-sys/LICENSE` (new, F1/R-D19: `cp
  $S/before/mac/licenses/mtld3d/crates/snmalloc-sys-0.7.6/LICENSE-MIT`, the crate's own notice, "Copyright (c)
  schrodingerzhu"; Microsoft's snmalloc notice keeps coming from `mtld3d-notices.py`'s `EMBEDDED`).
- `rustfmt.toml`, `mtld3d.conf` → root; `scripts/{audit.sh,derive_inventory.txt}` → `tools/` (P18);
  `docs/CONVENTIONS.md` → `docs/CONVENTIONS.md`; `docs/{ARCHITECTURE,STATUS,GAMES}.md`, `docs/architecture.svg` →
  `docs/d3d9/`; `README.md` → `docs/d3d9/README.md`; `CONTRIBUTING.md` → `docs/d3d9/TESTING.md`; `CLAUDE.md` →
  `CLAUDE.md`; `git rm AGENTS.md` (upstream's line) then `mtld3d/AGENTS.md` (symlink to `CLAUDE.md`) → `AGENTS.md`.
  Left in `mtld3d/` for Task 5: `Makefile`, `INSTALL.md`, `.github/`, `.gitignore`, `docs/BUILDING.md`,
  `scripts/test-{isolation,e2e-discovery}.py`.
- Create: `Cargo.toml`, `Cargo.lock` (P15), `.cargo/config.toml` (R-D2). Modify: `.gitignore` (`/target`, `/.codex`);
  each moved manifest (path deps → `workspace = true`; P14 metadata; `windows/Cargo.toml:19,21`,
  `unix/unix/Cargo.toml:19-20,24,43`, `unix/e2e/Cargo.toml:18`, nvngx/nvapi `../dxmt-util`); `src/d3d9/core/build.rs:22-23`
  (P4); `tests/d3d9/programs/tests/snmalloc_drift.rs:37` (`../../../../src/d3d9/core/tests/snmalloc_drift/shape.rs`),
  `…/tests/e2e/export_prologues.rs:31` (`../../../../../src/d3d9/d3d9.def`); `src/nvapi/src/lib.rs:20`
  (`../../../external/nvapi/nvapi_interface.h`); `tools/cargo-staticlib.sh:15,19-22` (`cd "$root"`, no routing; remap
  `$root=dxmtrs`); `tools/winemetal-layouts.sh:4,20,33`; comments naming `mtld3d/` in `src/{winemetal,winemetal/unix,
  nativemetal,nvngx,nvapi}/meson.build`, `src/winemetal/unix/sm50_wow64.c:3`, `src/winemetal/winemetal.h:1271`.

**Interfaces:**
- Consumes: Task 0's `$S/before/**`, `$S/ident*.sh`.
- Produces: the tree of `0-design.md` §1.2 with R-D1's test names; root `Cargo.toml`: `members` (16 paths),
  `default-members` (P16), `resolver = "2"`, `[workspace.package]` (edition 2024, rust-version 1.99, version 0.14.1,
  license LGPL-2.1-or-later, repository `https://github.com/Mac-Neutron/DXMTRS`), `[workspace.dependencies]` (union;
  objc2 family = the unix entries), profiles `dev`/`release`/`production`/`production-unwind` and the snmalloc-sys/zstd-sys
  `debug = 0` overrides, one `[workspace.lints.clippy]`, `[patch.crates-io] snmalloc-sys = { path =
  "vendor/snmalloc-sys" }`. `.cargo/config.toml`: the i686/x86_64/aarch64/arm64ec msvc rustflags (pentium4, x86-64-v2,
  `-crt-static`), `linker = "lld-link"`, the darwin x86_64 entries, `[env] MACOSX_DEPLOYMENT_TARGET = "15.0"`,
  `AR_<pe target> = "llvm-lib"`, `ARFLAGS_arm64ec-pc-windows-msvc = "-machine:arm64ec"`.

- [ ] **Step 1: Move.** Every `git mv` above, then the manifests, lock, config and path edits; `git -C $D add -A`.
  Expected: `ls $D/mtld3d` shows only Task 5's leftovers; every line of `git -C $D status --porcelain` starts with
  `R`, `M`, `A` or `D` (index column), none with `??`. Licences: `cmp` each of `src/d3d9/LICENSE`,
  `tests/d3d9/LICENSE`, `docs/d3d9/LICENSE` with `git -C $D show da6ed2d:mtld3d/LICENSE`, and `vendor/snmalloc-sys/LICENSE`
  with `$S/before/mac/licenses/mtld3d/crates/snmalloc-sys-0.7.6/LICENSE-MIT` → equal.
- [ ] **Step 2: Workspace checks.** From `$D`: `cargo metadata --locked --offline --format-version 1` rc 0 (P15) and
  `workspace_members` = 16; its packages: 10 `Zlib` (the mtld3d crates; a surviving `license.workspace = true` would
  inherit DXMTRS's LGPL), 6 `LGPL-2.1-or-later` (nvngx, nvapi, winemetal-pe, winemetal, winemetal-wire, dxmt-util),
  name, version and P14's fields equal `$S/before/packages.json`; the root `[profile.*]` dump equals both old tables'
  (`production-unwind` the only one-sided profile); each Task 0 matrix row's `cargo tree` (now from `$D`, Task 0's
  `sed`) against `$S/before/cargo/` → ED6 only.
- [ ] **Step 3: Fingerprint** (IF5): the emitter version = `$S/before/emitter.txt`; the 11 files `R100` in `git -C $D
  diff --cached -M --name-status da6ed2d`; `cargo test -p mtld3d-core --features mtld3d-core/disk-cache
  every_module_the_emitter_names_is_fingerprinted` passes.
- [ ] **Step 4: Own builds and tests.** UT green with the test count per binary equal `$S/before/tests/` (binary names
  mapped by R-D1; a moved `#[path]`, `include_str!` fixture or `tests/` target that stops compiling shows as a lower
  count); `cargo
  check -p mtld3d-tests --all-targets --target x86_64-pc-windows-msvc` in the hand PE environment; `sh
  tools/winemetal-layouts.sh "$LM/bin"` and `--protos` equal `src/winemetal/wire/src/layouts.rs` and
  `src/winemetal/src/protos.rs` (R-D4). Mac meson (`$SRC/dxmt-build`, destdir `$S/r1-mac`), guest meson (both dirs,
  `$S/r1-guest`) and `sh eac-vm/dxmt-native/build.sh`: `ident-diff.sh` of nvngx, nvapi64, winemetal (dll, so, dylib)
  against `$S/before` → `ED1 ED4`. Key unchanged. Tree clean.
- [ ] **Step 5: Commit** `d3d9: mtld3d moves to src/d3d9, one Cargo workspace at the root`.

### Task 2 (R1f): the format commit and the clippy fixes (owner)

**Files:** every `.rs` nightly rustfmt changes (not `vendor/`, not the 11 hashed files, not the P17 files);
`rustfmt.toml` (P17 `ignore`); `src/d3d9/mtl/Cargo.toml` (`[lints] workspace = true`, C8, only under ruling (b)); the
finding sites:
`src/nvngx/src/…` (`not_unsafe_ptr_arg_deref`), `src/winemetal/src/transport.rs:181` (i686 `usize`→`u32`), whatever
CM and Doc raise (R-D12's new legs: perf, `mtld3d-unix/winemetal`, `mtld3d-mtl`'s lints).

**Interfaces:** Consumes Task 1's tree. Produces a tree on which fmt, CM and Doc are clean (R4a's script finds nothing).

- [ ] **Step 0: The `mtld3d-mtl` ruling.** Before any edit, measure `mtld3d-mtl`'s findings with the workspace lints
  (the `[lints]` line added, `cargo clippy -p mtld3d-mtl --target aarch64-apple-darwin`, the line removed again; its
  Linux build is not linted, P11) and ledger the count by lint. `mtld3d-mtl` is the facade inside the
  guest's x86_64 and i686 `mtld3d.so` (`usize` is 32 bits on i686), and IF3/IF4/ED4 cannot see a behaviour change
  there. The controller rules (a) or (b) (Rules check, "Lint suppressions") and ledgers it; the task waits for it.
- [ ] **Step 1: RED.** fmt-check, CM and Doc in the hand PE environment. Ledger counts per leg (expected: the ~2000
  format hunks of D24, nvngx and winemetal-pe's two PE findings, plus the new legs').
- [ ] **Step 2: Format** with the nightly (`cargo fmt --all`). Expected: `git diff --stat` touches no `vendor/` path,
  none of the 11 hashed files, neither generated file. Commit `all: nightly rustfmt over the workspace (no behaviour
  change)`.
- [ ] **Step 3: Fixes**, test first where a behaviour is reachable (winemetal-pe: a pointer above `u32::MAX` is refused
  as the C would, `transport::tests::a_pointer_above_4g_is_refused`). Binding rules for every fix in a shipped crate
  (`mtld3d-mtl`'s included under ruling (b)): no cast changes semantics on i686 (no `try_from(..).unwrap()`/`expect` where `as`
  truncated or wrapped; keep the truncation explicit instead); no drop, lock-release or borrow reorder
  (`significant_drop_tightening` sites `mtl/src/meta.rs:64,81`, `metal/command.rs:965`, `metal/descriptors.rs:638`);
  every non-format hunk is ledgered with its lint and why it is behaviour-neutral, and reviewed on that list. Commit
  `nvngx, winemetal-pe, mtl: clippy findings (no behaviour change)`.
- [ ] **Step 4: Checks.** fmt-check, CM, Doc clean; UT green with Task 0's counts per binary plus the tests Step 3
  adds, by name; the msvc builds of every PE crate rc 0 — the staticlib crates by `cargo build -p … --target <t>
  --config "target.<t>.rustflags=[\"-Dwarnings\"]"`, `d3d9` and `mtld3d` (still `cdylib` until Task 3; a `cargo build`
  would link a DLL against `/opt/xwin`'s CRT, gone) by `cargo rustc -p <crate> --target <t> --profile production
  --crate-type staticlib -- -D warnings` (as `<M>/dxmt/mtld3d-build.sh:67`); IF5 (emitter value, 11 files untouched);
  layouts/protos regenerate equal. Mac meson rebuild of nvngx/nvapi/winemetal and `ident-diff.sh … ED1 ED4`. Guest
  `mtld3d.so`: `sh <M>/dxmt/mtld3d-build.sh --guest` is gone with the old tree, so the owner rebuilds both guest `.so`
  through the old recipe's cargo line from `$D` (`MTLD3D_LINUX=x86_64|i686`, `production`, `-p mtld3d-unix`, its
  environment as `<M>/dxmt/mtld3d-linux-cargo.sh:10-24` sets it, `CARGO_TARGET_DIR=$S/cargo-r1f-linux`, then `llvm-objcopy
  --strip-debug`) and runs `ident-diff.sh $S/before/guest <them> ED1 ED4` → nothing else.

### Task 3 (R2): the Mac's D3D9 from meson (owner)

**Files (DXMTRS):** `src/meson.build` (after `subdir('winemetal')`, inside the cross branch: `subdir('d3d9')`); create
`src/d3d9/meson.build`, `src/d3d9/unix/meson.build`, `src/d3d9/unix/install.sh`, `tools/arm64x-link.sh`,
`tools/mingw-cc.sh` (mode `100755`: `cp -p` of `<M>/dxmt/mtld3d-mingw-cc.sh` or `git update-index --chmod=+x`; cc-rs
and cargo exec its links directly, a 644 file gives EACCES; the other new scripts run through `sh` or `find_program`);
modify `tools/cargo-staticlib.sh`, `src/d3d9/Cargo.toml` and `src/d3d9/shim/Cargo.toml`
(`crate-type = ["staticlib"]`), `src/d3d9/build.rs` (lines 1-11 and the `MTLD3D_MINGW_CRT` block go),
`src/d3d9/src/lib.rs:1,71,171` (`cfg(windows)`), `src/d3d9/src/arm64_crt.rs` (x86 `_fltused` and its doc go),
`src/d3d9/shim/build.rs` (P7).

**Interfaces:**
- Consumes: Task 1's layout; `wine_build_path`, `wine_install_path`, `windows_builtin_install_dir`, `winebuild`,
  `rust_pe_targets`, `rust_target_dir`, `cargo_staticlib`, `native_compiler_arch` (meson globals).
- Produces:
  - `tools/cargo-staticlib.sh <toolchain> <rust-target> <crate> <target-dir> <out> [<profile> [<features>]]`
    (unchanged); with `MTLD3D_LLVM_MINGW` set it evals `mingw-cc.sh env <rust-target> <target-dir>/cc`; `<out>` ending in
    `.so` → cdylib mode (P9).
  - `tools/mingw-cc.sh`: personalities by link name as `<M>/dxmt/mtld3d-mingw-cc.sh`; `mingw-cc.sh env <t> <dir>`
    (P6). Values: `maps="-ffile-prefix-map=$HOME=~ -ffile-prefix-map=${CARGO_HOME:-$HOME/.cargo}/registry/src=crates
    -ffile-prefix-map=<dxmtrs root>=dxmtrs"`; aarch64/arm64ec msvc: `CC_`/`CXX_` = the links, `CFLAGS_=$maps`,
    `CXXFLAGS_=-fno-exceptions -fno-rtti -fno-threadsafe-statics -DSNMALLOC_USE_THREAD_CLEANUP -include stdlib.h $maps`,
    plain `CFLAGS=-DNDEBUG CXXFLAGS=-DNDEBUG`; `aarch64-apple-darwin`: `CFLAGS_=CXXFLAGS_=$maps` and the plain NDEBUG pair
    (`<M>/dxmt/mtld3d-build.sh:52-56,108-113`, `Makefile:308-320`). Task 4 adds the guest rows. Every name printed in
    P6's underscore spelling.
  - `tools/arm64x-link.sh <llvm-mingw-bin> <winebuild> <libntdll.a> <def> <out.dll> <arm64.lib> <arm64ec.lib>
    [<version.rc>]`: the guards of `Makefile:724-735` (exit 2 with their messages), the linker `<llvm-mingw-bin>/lld-link`
    only, its `--version` printed (P6); `ARM64X_CHECK_ARCHIVES` only when `<out.dll>`'s basename is `d3d9.dll` (the
    Makefile guards `d3d9.lib` only; the shim's halves link no snmalloc and would fail "defines no
    `_malloc_thread_cleanup`") on the two `d3d9` halves (mingw ABI guard
    `_ZGVZN8snmalloc10PALWindows24initialise_for_singletonEPyE4tidy`, `_malloc_thread_cleanup` defined, no
    `__tlregdtor`; `Makefile:752-765`), `llvm-rc` when an `.rc` is given, the
    `arm64x_link` line of `Makefile:767-774` verbatim (`/defarm64native`, `/debug /pdb … /pdbaltpath:%_PDB%`, the 12
    CRT libraries from `<bin>/../aarch64-w64-mingw32/lib`), then `winebuild --builtin` on the output.
  - `src/d3d9/meson.build`: per `rust_pe_targets`, `custom_target('d3d9-<t>')` and `('mtld3d-<t>')` with `env:
    {'MTLD3D_LLVM_MINGW': <P6's resolved root>, 'MTLD3D_WINECRT0': <P7's fs.is_file() path>}`; on `cpu_family ==
    'aarch64'` two `custom_target`s running
    `arm64x-link.sh` (`d3d9.dll` with `version.rc`, `mtld3d.dll` with `shim/mtld3d.def`), `install_dir:
    windows_builtin_install_dir`, the `.pdb` not installed; `libntdll.a` = `wine_build_path/dlls/ntdll/aarch64-windows`
    or, with `wine_install_path`, its `lib/wine/aarch64-windows` (`src/winemetal/meson.build`'s two branches).
  - `src/d3d9/unix/meson.build`: where `src/winemetal/meson.build:73` builds `winemetal.so`, `custom_target` `mtld3d.so`
    (`cargo_staticlib … -p mtld3d-unix`, darwin target from `native_compiler_arch`, `production`), installed by
    `install.sh` into `unix_install_dir` without `strip -x`.

- [ ] **Step 1: RED.** `tools/arm64x-link.sh` with a stand-in `lld-link` printing `LLD 22.0` → exit 2, "needs 23 or newer".
  `sh -ec 'eval "$(sh tools/mingw-cc.sh env arm64ec-pc-windows-msvc $S/cc-red)"'` → rc 0 (fails while a hyphenated
  name is printed).
- [ ] **Step 2: Implement**; the stand-in passes the version guard and fails the next (no `dllcrt2.o` ARM64EC half on a
  stand-in sysroot) with its message; the eval test passes; `git -C $D ls-files -s tools/mingw-cc.sh` → `100755`.
- [ ] **Step 3: Build.** Mac meson compile, install to `$S/r2-mac`. Expected: rc 0; `aarch64-windows/{d3d9,mtld3d}.dll`
  and `aarch64-unix/mtld3d.so` under the destdir; the build log's `lld-link --version` line = `$S/before/toolchain.txt`'s
  LLD 24.0.0.
- [ ] **Step 4: Identity.** `ident-diff.sh $S/before/mac <destdir tree> ED1 ED3 ED4 ED5` on the three (IF2, IF4, IF9).
  Feature set: `cargo tree … -p mtld3d-unix --target aarch64-apple-darwin` (Task 0's `sed`) restricted to its closure
  equals `unified-darwin.txt` restricted the same way — exactly; the old workspace resolves the same features for both
  (measured), so a difference is a root-manifest defect fixed in Task 1's manifest (stop for the controller), never
  ledgered or matched with `--features`. Flags: the fingerprint `rustflags`, the build scripts' `= Some(` lines and the
  profiles from meson's `CARGO_TARGET_DIR`s for `d3d9`, `mtld3d` (aarch64, arm64ec msvc) and `mtld3d-unix` (darwin)
  equal `$S/before/flags/` except the remap/prefix-map value (ED1) and the dropped `/opt/xwin` entries (R-D2).
  nvngx/nvapi/winemetal → `ED1 ED4`. Key unchanged; tree clean.
- [ ] **Step 5: Commit** `d3d9: the Mac's d3d9.dll, mtld3d.dll and mtld3d.so built by meson`.

### Task 4 (R3): the guest's D3D9 from meson (owner)

**Files (DXMTRS):** `src/d3d9/meson.build` (the non-aarch64 branch), `src/d3d9/unix/meson.build` (the `guest_vm`
branch), `tools/cargo-staticlib.sh` (Linux mode), `tools/mingw-cc.sh` (guest rows), create `tools/linux-cargo.sh`.

**Interfaces:**
- Consumes: Task 3's scripts and globals; `lib_ntdll`, `wrc_generator`, `rust_runtime_dep`.
- Produces:
  - `src/d3d9/meson.build` else-branch: `shared_library('d3d9', wrc_generator.process('version.rc'), name_prefix: '',
    link_with: <d3d9 libs>, vs_module_defs: 'd3d9.def', dependencies: [lib_ntdll, rust_runtime_dep], link_args: P8's list,
    install_dir: windows_builtin_install_dir)` (`wrc_generator` is llvm-windres: R-D5's correction, ED5), the same for
    `mtld3d` (`shim/mtld3d.def`, no resource), each followed by
    a `winebuild --builtin` postprocess target (`src/winemetal/meson.build:59-65`'s pattern). Every non-aarch64 cross
    build takes it (R-D5).
  - `src/d3d9/unix/meson.build` `guest_vm` branch: `mtld3d.so` for `x86_64-unknown-linux-gnu` (cpu x86_64) or
    `i686-unknown-linux-gnu` (cpu x86), installed into `x86_64-unix` or `i386-unix` (explicit names).
  - `tools/linux-cargo.sh <cargo args…>` (`MTLD3D_LINUX=x86_64|i686`, P10): at the root, `eval "$(mingw-cc.sh env
    <arch>-unknown-linux-gnu …)"`, `exec cargo "+$MTLD3D_RUST" "$@" --target <arch>-unknown-linux-gnu`;
    cargo-staticlib.sh's Linux mode calls it with `MTLD3D_RUST=$1 CARGO_TARGET_DIR=$4`.
  - `mingw-cc.sh env` guest rows: x86_64/i686 msvc `CFLAGS_=-march=x86-64-v2|pentium4 -fno-omit-frame-pointer $maps`,
    `CXXFLAGS_=` the same plus the snmalloc flags (`<M>/dxmt/mtld3d-build.sh:61-66`), no plain NDEBUG (R5);
    Linux `CC_`/`CXX_` = `linux-cc`/`linux-c++`, `AR_=$LM/bin/llvm-ar`, `CFLAGS_=CXXFLAGS_=$maps`, the linker =
    `linux-cc`, `MTLD3D_LINUX_TRIPLE=<arch>-redhat-linux` (`<M>/dxmt/mtld3d-linux-cargo.sh:19-24`).

- [ ] **Step 1: RED.** `MTLD3D_SYSROOT` unset: cargo-staticlib.sh's Linux mode exits non-zero naming `MTLD3D_SYSROOT`.
- [ ] **Step 2: Build** both guest dirs (sysroots per Global Constraints), destdir `$S/r3-guest`. Expected: rc 0; the four
  DLLs and two `.so` in `x86_64-windows`, `i386-windows`, `x86_64-unix`, `i386-unix`.
- [ ] **Step 3: Identity.** `ident-diff.sh $S/before/guest <destdir tree> ED1 ED2 ED3 ED4 ED5 ED9` (IF3, IF4, IF9:
  Linux counts still Task 0's). i386 exports equal `d3d9.def` undecorated; import sets = Task 0's minus
  `ntdll.dll!__chkstk` (P8; no `mem*`/`strlen`/`wcslen` moved to `ntdll.dll`). Flags: as Task 3 Step 4 for `d3d9`,
  `mtld3d` (x86_64, i686 msvc) and `mtld3d-unix` (x86_64, i686 linux-gnu) → equal but ED1 and R-D2's dropped
  entries (the guest's plain NDEBUG pair still absent until R5). Guest nvngx/nvapi64/winemetal → `ED1 ED4`. Key unchanged; tree
  clean.
- [ ] **Step 4: Commit** `d3d9: the VM guest's D3D9 built by meson`.

### Task 5 (R4b): documents and sweeps (lane `r4d`)

**Files (DXMTRS):** `CLAUDE.md` (tree-wide, its "Rules check" rule kept, R-D20), `CONTRIBUTING.md` (DXMT's commit format
and AI policy as upstream's rule for 3Shain/dxmt; mtld3d's tree-wide parts merged in; DXMTRS commits `<area>:
sentence`), `README.md` (one D3D9 paragraph; the licence map: R-D19's list + P19, `vendor/` MIT, `rust_pe_rt.c` LGPL in
D3D9's guest DLLs, "altered from athei/mtld3d 0.14.1"), `docs/DEVELOPMENT.md` ("Rust": baselines from BUILDING.md:112,
target paths from :244-295, `tools/rust-check.sh` and its variables), `docs/d3d9/README.md` (non-goal paragraph
:10-11), `docs/d3d9/TESTING.md` (D3D9 parts only: reading a run and the conformance suite told through MacNeutron's
`dxmt/mtld3d-conformance.sh` and `eac-vm/dxmt/d3d9-gate.sh`; the e2e and bench programs await C4's llvm-mingw build,
X5; no `make` command left; the release section :740-774 and `dxmt` in the reference list go); `git
rm` `mtld3d/Makefile`, `mtld3d/INSTALL.md`, `mtld3d/.github`, `mtld3d/.gitignore`, `mtld3d/docs/BUILDING.md`,
`mtld3d/scripts/test-{isolation,e2e-discovery}.py`; root `.gitignore` gains `mtld3d/.gitignore`'s `.idea/`, `*.swp`,
`*~` (`.vscode`, `.DS_Store` are there already; `/.wine-isolated`, `/target`, `/windows/target`, `/unix/target*` and
`/windows/target-native` named obsolete in the commit message: the Makefile and the old dirs are gone; Task 1 owns the
file's `/target`, `/.codex` lines); sweeps (R-D16/E2): comments and strings naming
`docs/{STATUS,ARCHITECTURE,BUILDING}.md`, the Makefile or a `make` target (the 16 runtime strings: the
`#[ignore = "benchmark, run by \`make bench\`"]` lines in `tests/d3d9/programs/tests/e2e/bench_*.rs`,
`bench_cold_start.rs:253`, `tests/d3d9/runner/src/main.rs:3`, `bench.rs:78`; `tests/d3d9/conformance/src/main.rs:240`
and `model.rs:397` name `dxmt/mtld3d-conformance.sh --update-baseline` (R-D14), with `baseline.txt:3` changed alike).
Never: the 11 hashed files (P4), Task 3/4's files, `docs/CONVENTIONS.md`.

**Interfaces:** Produces `mtld3d/` gone; documents only, plus comment and test-string edits.

- [ ] **Step 1: Write and delete** as above. `ls $S/wt-r4d/mtld3d` fails (gone).
- [ ] **Step 2: Checks.** `/usr/bin/grep -rnE 'make [a-z-]+|PROD=1|\bMakefile\b|INSTALL\.md|docs/(STATUS|ARCHITECTURE|BUILDING|GAMES)\.md|scripts/(audit\.sh|derive_inventory|cursor_)|mtld3d/(windows|unix)'`
  over the tree outside `vendor/`, `.git`, the 11 hashed files and R4a's `tools/audit.sh`, `tools/derive_inventory.txt`,
  `docs/CONVENTIONS.md` (Task 6 rewrites them; the full grep runs at Task 8 Step 0) → every remaining hit justified
  in the ledger (a MacNeutron `make` target, a document that now lives under `docs/d3d9/`); UT of
  `mtld3d-conformance` (triage and baseline tests) green; IF5; fmt-check clean; `git diff --stat` touches none of the
  excluded files.
- [ ] **Step 3: Commit** `docs: D3D9's documents under docs/d3d9, one manual; mtld3d's Makefile, CI and bundle retired`.

### Task 6 (R4a): rust-check.sh, the audit, the inventory (lane `r4t`)

**Files (DXMTRS):** create `tools/rust-check.sh`; modify `tools/audit.sh`, `tools/derive_inventory.txt` (regenerated),
`docs/CONVENTIONS.md` (`dxmt` leaves the competitor list :54; the tree-wide scope).

**Interfaces:**
- Consumes: Task 3's and Task 4's `mingw-cc.sh env` rows (the lane branches from Task 4's commit), P7's
  `MTLD3D_WINECRT0`.
- Produces: `tools/rust-check.sh` (no arguments; P5, P11 inputs, each `${X:?…}` naming where MacNeutron gets it):
  legs `fmt`, `clippy` (CM, perf included), `doc`, `test` (UT), `audit`; per PE target it sets
  `MTLD3D_WINECRT0` (aarch64/arm64ec: `$WINE_BUILD/{libs,dlls}/winecrt0/aarch64-windows/libwinecrt0.a`; x86_64/i686:
  `$GUEST_WINE_SDK/lib/wine/{x86_64,i386}-windows/libwinecrt0.a`) and evals `mingw-cc.sh env`. Prints each cargo
  command line it runs, `rust-check: <leg> ok|FAIL` per leg and `rust-check: ok` last; exit 1 if any leg failed (each
  target's rc kept, not only the last under `--keep-going`). `tools/audit.sh` (E1 and `0-design.md` R4): `cd`
  to the root; lists `:20-108` repointed; the PE side = each PE crate's `src/` and `build.rs` (`src/d3d9`,
  `src/d3d9/shim`, `src/d3d9/core`, `src/d3d9/types`, `src/util`, `src/nvapi`, `src/nvngx`, `src/winemetal` — never
  `src/winemetal/unix/**` or `wire/**`, never `src/d3d9/unix/**`) and `tests/d3d9/programs/**`; `PE_JOIN_EXEMPT`
  `^tests/d3d9/`; `VENDOR_TREE` and the `--file` skip `^vendor/`; the release set = the workspace crates' files,
  `docs/d3d9`, `docs/CONVENTIONS.md`, `mtld3d.conf`, `tools/audit.sh` (minus `^tests/d3d9/conformance/`); `dxmt` out of
  the competitor pattern `:562`; messages `:2-10,483,575` name `tools/audit.sh`.

The lane's RC form (RL), literally, from `<M>` (on `dxmtrs`, whose `dxmt/pins` has no `MTLD3D_RUST_NIGHTLY` yet):
`sh -c 'ROOT=$PWD; . dxmt/pins; LM=$(dirname "$(sh dxmt/toolchain.sh)"); git -C build/step-r/wt-r4t add -A;
PATH="$(sh dxmt/mtld3d-tools.sh):$LM/bin:$PATH" MTLD3D_RUST=$MTLD3D_RUST MTLD3D_RUST_NIGHTLY=nightly-2026-10-30
MTLD3D_LLVM_MINGW=$LM WINE_BUILD=$PWD/build/wine-arm64-src/wine-build GUEST_WINE_SDK=$PWD/build/eac-vm-work/guest-wine-sdk
CARGO_TARGET_DIR=$PWD/build/step-r/cargo-r4t/check sh build/step-r/wt-r4t/tools/rust-check.sh'`.

- [ ] **Step 1: RED, one per leg**, each a scratch mutation in a lane file, RL → `rust-check: <leg> FAIL` naming that
  leg, the other legs ok, exit 1, then reverted: fmt (a misformatted line); clippy (an unused binding in an i686-only
  `cfg` arm); doc (a broken intra-doc link); test (`assert!(false)` in a unit test); audit, one violation per changed
  selector (a `.join()` in `src/d3d9/shim/src/`, an aligned deref of an app pointer under `APP_POINTER_DIR`, an
  unlisted e2e spawn under `tests/d3d9/programs`, an `#[allow(` outside `ALLOW_SITES`, `/Users/` in a comment under
  `src/nvngx`: the audit's rules run only when their selector lists files, `audit.sh:510-530`, so an empty selector
  passes silently). With `MTLD3D_RUST_NIGHTLY=nightly-2000-01-01` it refuses naming the pin.
- [ ] **Step 2: Implement**; `sh tools/audit.sh --update-derives`, then the audit.
- [ ] **Step 3: Checks.** RL → every leg ok; `audit: clean (688 files)` (688 = step 2's count, every `.rs` in the
  tree; plus any `.rs` R adds, named in the ledger: the count is `git ls-files '*.rs'`, not the audited set, so it
  bounds nothing by itself); the printed cargo command lines diffed once against Global Constraints' CM/UT/Doc lists →
  equal; `git diff` of `derive_inventory.txt` changes paths only (same type and derive columns, `LC_ALL=C sort`ed).
  Commit `tools: one Rust gate (rust-check.sh), the audit tree-wide`.

### Task 7 (RM): MacNeutron's same-pin-bump edits (lane `mn`, then owner)

**Files (MacNeutron, `stepR-mn`; line numbers as of cec2527):**
- `dxmt/pins:23-24`: `MTLD3D_RUST` comment cites `meson.options:14`; add `MTLD3D_RUST_NIGHTLY=nightly-2026-10-30` (P5).
- `wine-arm64/build.sh`: `:39-55` add `i686-unknown-linux-gnu`; `:160-169` the stamp drops `mtld3d-build.sh`,
  `mtld3d-mingw-cc.sh`, `mtld3d-linux-cargo.sh`, `mtld3d-layout32-check.sh`; after the `PATH=…mtld3d-tools.sh` line
  (`:532`) and before `meson compile` (`:541`), `(cd "$D" && cargo "+$MTLD3D_RUST" fetch --locked)` (R-D8; the two
  `eac-vm` scripts source no pins and `eac-vm/dxmt/build.sh` has no `$D`: their forms are in their bullets);
  `:583-591` step 7b goes; `:593-594` notices take `"$D"`; comments
  `:4,584,592` (E3). `wine-arm64/tests/mode_test.sh:49-50`: the anchor moves to the new last stamp item; the file list
  drops `dxmt/mtld3d-build.sh`.
- `wine-arm64/bundle.sh`: `:83-88` from `$DXMT_IN/aarch64-windows/{d3d9,mtld3d}.dll`, `$DXMT_IN/aarch64-unix/mtld3d.so`,
  the `winebuild` line goes; `:107-111` `LICENSE` from `$DXMT_TREE/src/d3d9`; `:318-329` adds `llvm-readobj --file-headers`
  grepped exactly for the line `Machine: IMAGE_FILE_MACHINE_ARM64X (0xA64E)` for both DLLs (every ARM64X file also
  prints `IMAGE_FILE_MACHINE_ARM64EC (0xA641)` in its `HybridObject` block, so a looser pattern always passes).
- `dxmt/mtld3d-build.sh`: only `--loopback`, at `$T` = the tree root, `CARGO_TARGET_DIR=$B/d3d9-cargo/wm`, writes no
  cargo config; header rewritten. `git rm dxmt/mtld3d-linux-cargo.sh dxmt/mtld3d-mingw-cc.sh`. `dxmt/mtld3d-tools.sh`:
  the four links (`:24-27`) and their header text go.
- `dxmt/mtld3d-layout32-check.sh`, `dxmt/mtld3d-layouts.py`: `$T/tools/linux-cargo.sh` with what P10's guards need
  (today `mtld3d-linux-cargo.sh:10-21` supplied it): `. "$ROOT/dxmt/pins"`, `LM=$(dirname "$(sh
  "$ROOT/dxmt/toolchain.sh")")`, `PATH="$(sh "$ROOT/dxmt/mtld3d-tools.sh"):$PATH"` (the `+$MTLD3D_RUST` proxy), exported
  `MTLD3D_RUST`, `MTLD3D_LLVM_MINGW=$LM`, per arch `MTLD3D_SYSROOT=$(sh "$ROOT/eac-vm/sysroot/fetch.sh" $a)` and
  `CARGO_TARGET_DIR=$B/d3d9-cargo/layout32-<a>`; sources `$T/src/d3d9/{shared,mtl}`.
- `dxmt/mtld3d-notices.py:20-21,50-51` (R-D19): one workspace at the tree root; LEGS = `(("d3d9","mtld3d","winemetal-pe",
  "nvngx","nvapi"), (aarch64, arm64ec, x86_64 msvc))`, `(("d3d9","mtld3d","winemetal-pe"), (i686 msvc,))`,
  `(("mtld3d-unix","winemetal"), (aarch64-apple-darwin,))`, `(("mtld3d-unix",), (x86_64, i686 linux-gnu))`.
- `dxmt/mtld3d-conformance.sh:26-45`: `T` root; presence gate on `$T/tests/d3d9/conformance/baseline.txt`; the prefix
  recipe inline (R-D14, P21) instead of `:36`; `cd "$T"`, `CARGO_TARGET_DIR=$B/d3d9-cargo/conformance`, `--assets
  "$T/tests/d3d9/conformance"`; args still pass through (`--update-baseline`).
- `dxmt/mtld3d-facade-test.sh:16-18`: `cd "$T"`, `CARGO_TARGET_DIR=$B/d3d9-cargo/wm`.
- `dxmt/d3d9-check.sh`: header `:11-14`; `MTLD3D_PE` text names `dxmt-install/aarch64-windows`; the `arm64` row (P20),
  its exe `build/dxmt-tests-arm64/d3d9_triangle.exe` joining the presence loop at `:21` (exit 2 when missing; not the
  ARM64EC exe `build/dxmt-tests-arm64ec/d3d9_triangle.exe`, which loads the EC half and proves nothing). P20's
  launcher-or-`bin/wine` choice is settled in Step 1 by reading how the launcher starts a PE's architecture
  (`Sources/`), ledgered with the lines read.
  `Makefile`: the `build/dxmt-tests-arm64/%.exe` rule, `dxmt-tests` builds `d3d9_triangle` there; comments `:58,71`.
- `dxmt/tests/mtld3d_guest_test.sh:3,11-13`: default folder `$W/dxmt-guest` (header `:3` too), `.def`
  `$T/src/d3d9/d3d9.def`.
- `eac-vm/dxmt/build.sh`: `:87-91` goes; `MTLD3D_SYSROOT` exported before each `meson compile`; after the
  `mtld3d-tools.sh` `PATH` line (`:44`), before the first `meson compile`: `( . "$ROOT/dxmt/pins"; cd "$S/dxmt" &&
  cargo "+$MTLD3D_RUST" fetch --locked )` (the script's own `$S` = `wine-arm64-src`, so its tree is `$S/dxmt`,
  `:16,38`; no pins sourced otherwise); `sh
  "$ROOT/dxmt/mtld3d-layout32-check.sh"` after both installs; header `:2-5,87-89`.
- `eac-vm/dxmt/stage.sh:13-20`: `d3d9 mtld3d` join both DLL lists, `.so` from `$W/dxmt-guest/{x86_64,i386}-unix`,
  `build/mtld3d-guest` gone; header. `eac-vm/dxmt/check-build.sh`: `:13` loop adds `d3d9 mtld3d`; `:26` allows exactly
  the two `.so` (R-D9); `:29-30` the guest test on `$D`; header `:3-4`.
- `eac-vm/dxmt/d3d9-gate.sh:7,16,31-43`: `$T/tools/linux-cargo.sh` with layout32-check's environment lines (pins, `LM`,
  the tools `PATH`, `MTLD3D_RUST`, `MTLD3D_LLVM_MINGW`; `MTLD3D_SYSROOT` per arch in the `x86_64 i686` loop — it
  builds both arches' unit tests — and the x86_64 one for the runner), `CARGO_TARGET_DIR=$B/d3d9-cargo/gate-<a>`, the
  runner from there, `$T/tests/d3d9/conformance/{baseline.txt,CONFORMANCE.md}`; `WANT` unchanged.
- `eac-vm/dxmt-native/build.sh`: after its `mtld3d-tools.sh` `PATH` line (`:25`), before `meson compile`: `( .
  "$ROOT/dxmt/pins"; cd "$D" && cargo "+$MTLD3D_RUST" fetch --locked )`. `eac-vm/image/stamps.sh:17`: the `dxmt` row adds
  `scripts $(here dxmt) sysroot $(here eac-vm/sysroot)` (E4). `eac-vm/setup/parts.sh:19-32`: copies
  `licenses/mtld3d/{LICENSE,crates}`, README names D3D9 (F2).
- Text (E3, map 3 §7): `README.md:86,125-132` (and the stale "game VM keeps Wine's own Direct3D 9" sentence),
  `wine-arm64/licenses/README:29-42` (`DXMTRS's src/d3d9`; F4's LGPL clause), `release/release.sh:674`,
  `wine-arm64/winetests.sh:4`, `eac-vm/sysroot/fetch.sh:3` (names the deleted `dxmt/mtld3d-mingw-cc.sh`).
- Delete `$SRC/.cargo/config.toml` (the retired script's output; not tracked).

**Interfaces:** Consumes Tasks 3, 4, 6's names (`tools/linux-cargo.sh`, `tools/rust-check.sh`, install dirs,
`MTLD3D_SYSROOT`). Produces `dxmt/mtld3d-build.sh --loopback` → prints `$B/d3d9-cargo/wm/aarch64-apple-darwin/production/mtld3d.so`.

- [ ] **Step 1: Write** (lane, after Task 1); `sh -n` every edited script; the sweep over every tracked file, not only
  the edited ones (the old paths are mostly defined without a trailing slash or through a variable): `git -C $S/wt-mn
  ls-files | /usr/bin/grep -vE '^(docs/|\.superpowers/)' | (cd $S/wt-mn && xargs /usr/bin/grep -nE
  'wine-arm64-src/dxmt/mtld3d([/"]|$)|DXMT_TREE/mtld3d|\$M/(windows|unix)|mtld3d-(mingw-cc|linux-cargo)\.sh|mtld3d-guest|target-wm|target-linux')`
  → none (today it finds `mtld3d-conformance.sh:26`, `d3d9-gate.sh:15`, `mtld3d-build.sh:26,37`, `bundle.sh:85,108`,
  `sysroot/fetch.sh:3`, …). R-D8's three fetch lines: in each of `wine-arm64/build.sh`, `eac-vm/dxmt/build.sh`,
  `eac-vm/dxmt-native/build.sh` the `fetch --locked` line comes after the `mtld3d-tools.sh` `PATH` line and before the
  first `meson compile` (`awk` over line numbers). P20's launcher-or-`bin/wine` choice settled and ledgered.
- [ ] **Step 2: Integrate** (owner, after Tasks 4 and 6): the lane's work committed, `git worktree remove $S/wt-mn`
  (git refuses to switch to a branch another worktree holds), then `git switch stepR-mn` in `<M>`; later MacNeutron
  fixes (Tasks 8, 10) land there. `rm $SRC/.cargo/config.toml`.
- [ ] **Step 3: Mac build.** `make wine-arm64 dxmt-tests` → "development build", "staged … (dev)"; bundle.sh's checks
  pass; `ident-diff.sh $S/before/mac <wine.app's D3D9 files> ED1 ED3 ED4 ED5`; tree clean; key unchanged. Bundle.sh's
  machine check, RED: its exact grep on `$A/lib/wine/aarch64-windows/winemetal.dll` (ARM64EC, 0xA641) fails. The
  arm64 program: `build/dxmt-tests-arm64/d3d9_triangle.exe` exists and `llvm-readobj --file-headers` prints `Machine:
  IMAGE_FILE_MACHINE_ARM64 (0xAA64)`; RED: moved aside, `sh dxmt/d3d9-check.sh` exits 2 naming it at the presence loop
  (before any Wine run); moved back. The loopback build: `sh dxmt/mtld3d-build.sh --loopback` prints
  `$B/d3d9-cargo/wm/aarch64-apple-darwin/production/mtld3d.so`, the file exists, `nm -gU` lists both
  `___wine_unix_call_funcs` and `___wine_unix_call_wow64_funcs`.
- [ ] **Step 4: Guest build.** `sh eac-vm/dxmt/build.sh` rc 0; `sh eac-vm/dxmt/check-build.sh` → `check-build: ok`;
  `mtld3d-layout32: ok (132 structs)` in its log; tree clean. R-D9, RED: `cp -R $W/dxmt-guest $S/guest-red`, a stray
  `$S/guest-red/x86_64-unix/foo.so` → `sh eac-vm/dxmt/check-build.sh $S/guest-red` prints `check-build: FAIL`; `rm -rf
  $S/guest-red`.
- [ ] **Step 5: MacNeutron tests.** `sh wine-arm64/tests/mode_test.sh`, `licences_test.sh`, `translator_key_test.sh` ok;
  `diff -r $S/before/mac/licenses/mtld3d $A/licenses/mtld3d` → ED8 only (the snmalloc-sys notice still "schrodingerzhu",
  `LICENSE-snmalloc` still Microsoft's); INDEX's `name version` set (first two words per line) equals
  `$S/before/notices-legs.txt` (R-D19's legs exactly, so a missing guest or nvngx/nvapi leg shows), no Apache-2.0-only
  entry; `sh eac-vm/image/stamps.sh dxmt` names the new rows.
- [ ] **Step 6: Fresh registry** (A4) through the scripts' own fetch line: `CARGO_HOME=$S/cargo-home-fresh sh
  eac-vm/dxmt-native/build.sh` (46 s in step 2) with `$S/cargo-home-fresh` empty → rc 0 and
  `$S/cargo-home-fresh/registry/cache` populated; then, with `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH` and that
  `CARGO_HOME`, each Task 0 matrix row's `cargo tree --offline --locked` rc 0; `rm -rf $S/cargo-home-fresh`.
- [ ] **Step 7: Commit** on `stepR-mn`: `D3D9 at src/d3d9: MacNeutron's scripts (pin bump to follow)`.

### Task 8 (R5): NDEBUG (owner)

**Files:** DXMTRS `tools/mingw-cc.sh` (guest PE and Linux rows gain the plain `CFLAGS=-DNDEBUG CXXFLAGS=-DNDEBUG`);
MacNeutron (`stepR-mn`) `wine-arm64/lib.sh`: one function `production_asserts <file>…` — the production-assert check
of `Makefile:1027-1039` (no `_assert`/`_wassert` import in a PE, no `__assert_rtn` in Mach-O and no `__assert_fail`
in ELF, glibc's handler, by `llvm-nm -u`; no `assert fail: {} in {} on {} ` string), exit 1 naming the file;
`wine-arm64/bundle.sh` (sources `lib.sh` already) and `eac-vm/dxmt/check-build.sh` (gains `. "$ROOT/wine-arm64/lib.sh"`)
call it over the D3D9 binaries they hold.

- [ ] **Step 0: Merged-tree check** (after Tasks 5 and 6 are rebased onto `stepR-layout`; the main checkout on
  `stepR-mn` since Task 7 Step 2, so `dxmt/pins` has `MTLD3D_RUST_NIGHTLY`): Task 5's grep without its three
  exclusions → every hit justified in the ledger; RC → `rust-check: ok`, its audit line `audit: clean (688 files)`
  (plus R's added `.rs`, named).
- [ ] **Step 1: RED.** The new check-build.sh check on Task 7's guest build → FAIL naming `x86_64-unix/mtld3d.so`; and
  once through bundle.sh's call path (`. wine-arm64/lib.sh; production_asserts
  $S/before/guest/x86_64-unix/mtld3d.so`) → FAIL naming it (the Mac binaries have 0 asserts, so bundle.sh's own inputs
  can never show the check failing).
- [ ] **Step 2: GREEN.** The flag; `sh eac-vm/dxmt/build.sh`; check-build.sh ok; `make wine-arm64` (bundle.sh's check
  passes; the Mac had NDEBUG). `ident-diff.sh <Task 4's guest tables> $W/dxmt-guest ED3 ED7`; the guest flags
  (Task 4 Step 3's comparison) differ from Task 4's exactly by the plain `CFLAGS = Some(-DNDEBUG)` and `CXXFLAGS =
  Some(-DNDEBUG)` lines (ED7); the Mac's D3D9 files (the Mac rows share `mingw-cc.sh`): `ident-diff.sh $S/before/mac
  <wine.app's D3D9 files> ED1 ED3 ED4 ED5`.
- [ ] **Step 3: Commit** DXMTRS `d3d9: NDEBUG for the guest's C and C++ (behaviour change)`; MacNeutron on `stepR-mn`
  `D3D9: production assert checks in check-build.sh and bundle.sh`.

### Task 9: the end-of-step gates, once (owner)

Every gate runs once on `stepR-layout`'s head with `<M>` on `stepR-mn`, in this order; a failing row is recorded, not
fixed (Task 10). Exports for every VM and reference gate: `MN_STATE=$W/state/step2-gates.img`, `MN_REF_FROZEN=$S/ref-vm`
(present-gate `$S/ref-present`), `MN_REF_WINE_APP=$S/ref-wine.app`, the signing variables. Reports:
`$S/reports/gates-{mac,native,vm,rust}.md` in step 2's shape. After each build and gate: tree clean. Identity runs after
the builds, on the files the gates use (not on Task 8's pre-commit builds).

- [ ] **Mac.**
  - `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot` → "development build",
    "staged … (dev)"; `codesign -dv` cdhash of wine.app recorded (runtime=dev).
  - `sh dxmt/check.sh` → 412 ok, 0 FAIL, the row set of `build/step2/ref-perf/check-x64.txt`; ARM64EC:
    `MACNEUTRON_ARM64_TESTS=build/dxmt-tests-arm64ec MACNEUTRON_ARM64_LOOP=$PWD/build/dxmt-tests-arm64ec/present_loop.exe
    DXMT_CHECK_WORK="$TMPDIR/macneutron dxmt arm64ec" sh dxmt/check.sh` → 412 ok, 0 FAIL. Flaky rows (bg-leave,
    ts-many): one re-run, ledgered.
  - `sh wine-arm64/check.sh dxmt dxmt-present` → PASS lines; `sh presenter/check.sh` → 66 ok, 0 FAIL;
    `sh dxmt/mtld3d-facade-test.sh` → 8 passed, 0 failed (facade); `sh dxmt/wm-probe-check.sh` and `--full` → 11 ok.
  - Step 2's probe rows, as `build/step2/reports/gates-mac.md` §6 ran them (host from the build tree's `winemetal.so`
    by `make -C eac-vm/metal/wm metal-host`; `wm_probe` built as step 2 did, or step 2's `build/step2/probe/wm_probe`
    if it still exists — step 2's `$S` was its own scratch): `dxmt/tests/wm_probe/cover.sh table <host> <build-tree
    winemetal.so>` → "158 entries, wow64 153"; `cover.sh slots` ok; `cover.sh cases` (all groups) ok.
  - D3D9: `sh dxmt/mtld3d-build.sh --loopback` → its path; `MTLD3D_SO=<it> DXMT_CHECK_WORK=$S/gn/d3d9-lo sh
    dxmt/d3d9-check.sh` and `MTLD3D_PE=$SRC/dxmt-install/aarch64-windows DXMT_CHECK_WORK=$S/gn/d3d9-pe sh
    dxmt/d3d9-check.sh` → `d3d9-check: ok` (the `arm64` row included); `sh dxmt/mtld3d-conformance.sh` and
    `MTLD3D_SO=<it> sh dxmt/mtld3d-conformance.sh` → each exactly step 2's figures
    (`build/step2/logs/gn-mtld3d-conformance.txt`): device 739 failed / 159554 executed, visual 31 / 211219,
    stateblock 0 / 14738, d3d9ex 74 / 2430, crash 0, the three triaged new sites (`device.c:14544,14946,15104`), "only
    triaged sites … passed". After each run the prefix recipe (R-D14, P21) is read back: the three keys as
    `Makefile:1441-1443` reads them (`WineDbg` `ShowCrashDialog` 0x0, `X11 Driver` `EmulateModeset` Y, `Mac Driver`
    `RetinaMode` Y) and `wineserver -k0` finds the persistent server. A control run on `$S/ref-wine.app` is only for a
    site in Wine's own code; a figure off on the control too is a script defect, not "same as before".
  - `wine-arm64/tests/{mode,licences,translator_key}_test.sh` ok; `sh wine-arm64/bundle.sh --release --version 9.9.9
    --out $S/release-check` rc 0 (its `build_paths`); `rm -rf $S/release-check`.
- [ ] **Native and VM host.** `sh eac-vm/dxmt-native/build.sh`; `sh eac-vm/dxmt-native/tests/{build,hook,win32}.sh` →
  ok; `sh eac-vm/dxmt-native/gate.sh` → all 31; `sh dxmt/wm-probe-check.sh --native
  build/dxmt-native/install/lib/winemetal.dylib` → 11 ok; as `gates-native.md` §1-2: `cover.sh table` on the dylib
  (copied as `winemetal.so` into a stand-in host dir) → "158 entries, no wow64 table", and `wm_probe <host> pair
  build/dxmt-native/install/lib/winemetal.dylib` → `pair wine 117 0 ret=nonnull class=CacheWriter` and `pair native 117
  0 ret=nonnull class=DXMTNativeCacheWriter`; `sh eac-vm/host/build.sh`, `sh eac-vm/metal/build-guest.sh` (its log:
  `layout32: ok (241 structs, 222 on the wire)`), `sh eac-vm/metal/gate.sh` → all passed; `make -C eac-vm/metal/wm
  host-test` equals `build/step2/ref-metal/metal-wm-host.txt`; `make -C eac-vm/metal/wm memory-test` → all passed; `sh
  eac-vm/d3d/loopback-gate.sh` → all 31, its lines equal `build/step2/ref-perf/loopback-gate.txt` except times.
- [ ] **VM.** Builder with `VM_SHARES`; `sh eac-vm/dxmt/build.sh` (its log: `mtld3d-layout32: ok (132 structs)`; the
  script prints "ok" for any N>0), `sh eac-vm/dxmt/check-build.sh` → ok (R-D9's two `.so`); `sh
  eac-vm/d3d/build-guest.sh`; `sh eac-vm/image/guest-stages.sh dxmt d3d`; `sh eac-vm/builder.sh sync eac-vm/image
  mn-image`; `sh eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`. Identity: `.mn-source` names the head without
  `+dirty`; staged D3D9 files equal `$W/dxmt-guest`'s by sha256. Then `sh eac-vm/dxmt/gate.sh` → all 44, its lines
  equal `build/step2/ref-perf/dxmt-gate.txt` except times; `sh eac-vm/tests/d3d-smoke.sh` and `MN_WOW64=new sh
  eac-vm/tests/d3d-smoke.sh` → ok, each row set equal `build/step2/ref-perf/d3d-smoke-run4-freshstate.txt` with both
  `32:` blocks of 14 rows (Review Focus 3); `sh eac-vm/dxmt/d3d9-gate.sh` → `d3d9 gate: ok`, its ok/FAIL/info lines
  equal `build/step2/ref-perf/d3d9-gate-run2-freshstate.txt`; `MN_REF_FROZEN=$S/ref-present sh
  eac-vm/dxmt/present-gate.sh` → all passed. VM parts: `sh eac-vm/setup/parts.sh $S/parts`, then
  `release.sh:144-147`'s grep over it → nothing; `licenses/mtld3d/{LICENSE,crates/INDEX}` present.
- [ ] **Identity and Rust** (after the builds above, on the files the gates use).
  - `ident-diff.sh $S/before <after> ED1 ED2 ED3 ED4 ED5 ED7 ED9` for wine.app's D3D9 and DXMT files, `$W/dxmt-guest`,
    the staged guest, the native dylib (ED2/ED9 apply to guest PE files only, ED5 as defined); IF5, IF6 (key and
    `translator_key_test.sh`), IF7 (Task 0's matrix, `unified-darwin` relation: equal within the closure).
  - RC → `rust-check: ok`, `audit: clean (688 files)` (plus R's added `.rs`, named); layouts/protos regenerate equal;
    `$HOME` count 0 and `nm -ap | /usr/bin/grep -c ' OSO '` 0 on wine.app's `mtld3d.so`, `$HOME` 0 on the D3D9 DLLs
    (Mac, guest); Task 5's grep without its exclusions → only the ledgered hits.
  - Licences (spec §8): the three LICENSE copies `cmp`-equal `git -C $D show da6ed2d:mtld3d/LICENSE`;
    `vendor/snmalloc-sys/LICENSE` byte-equal the before `LICENSE-MIT`; `cargo metadata` → 10 packages `Zlib`, 6
    `LGPL-2.1-or-later`; no Apache-2.0-only crate: the `cargo tree -e normal` crate set of every shipped package per
    shipped target (aarch64, arm64ec, x86_64, i686 msvc; darwin; x86_64/i686 linux-gnu) joined with `cargo metadata
    --format-version 1`'s `license` field (the tree prints none); `diff -r $S/before/mac/licenses/mtld3d
    $A/licenses/mtld3d` → ED8 only.
- [ ] **The p profile** (last: it rebuilds the guest dirs the gates above used). `MACNEUTRON_PROFILE=p sh
  eac-vm/dxmt/build.sh` (the guest D3D9 link now takes `-Db_lto=true -Db_lto_mode=thin` and `-DDXMT_PERF_BUILD`, which
  the hand link ignored; releases ship p) → rc 0; `ident-diff.sh <the std after-tables> <its D3D9 files> ED4` → sizes
  only. Then `sh eac-vm/dxmt/build.sh` again (std), so Tasks 10 and 11 compare a std guest.
- [ ] **Ledger** every result; nothing committed.

### Task 10: one fix pass (owner, lanes as needed)

- [ ] **Step 1: Defects.** Per failing row: root cause, a failing test or table row first, the fix in the owning file
  (DXMTRS on `stepR-layout` or a lane; MacNeutron on `stepR-mn`), the task's own checks, the failed rows re-run once,
  and every gate whose inputs the fix touched, named per fixed file in the ledger (e.g. `tools/mingw-cc.sh` or
  `tools/cargo-staticlib.sh`: the Mac, guest and native builds and every D3D9 and winemetal row; `check-build.sh`: the
  VM build rows). A fix that touches one of the 11 hashed files (P4) stops and goes to the maintainer: it changes
  `SHADER_EMITTER_VERSION`, and every player's D3D9 shader cache regenerates once. A defect needing a second round goes
  to the maintainer.
- [ ] **Step 2: Commit** each fix (`d3d9: …`, `tools: …`; MacNeutron on `stepR-mn`).
- [ ] **Step 3: Identity again**, once, after the last fix: Task 9's identity block (ident-diff of wine.app,
  `$W/dxmt-guest`, the guest stage and the native dylib with the same ED list; IF5, IF6, IF7) and RC → as in Task 9.

### Task 11: spec, push, pin, step 3's plan (controller)

**Files (MacNeutron):** the spec, `dxmt/pins`, the ledgers, `.superpowers/sdd/dxmtrs-program.md`,
`docs/superpowers/plans/2026-10-30-dxmtrs-step3-d3d10core-dxgi.md` (untracked).

- [ ] **Step 1: Spec.** `git switch dxmtrs` in `<M>` (the gates are done; it has been on `stepR-mn` since Task 7 Step
  2). §5 step R marked done (date, DXMTRS sha) with what it did and P4, P5, P11 as corrections (the three stale emitter
  comments wait for C3; the nightly pin's name; no Linux leg), and R-D5's resource-compiler premise (the guest's
  changes from llvm-rc to llvm-windres) and R-D19's licence text (the crate's own notice, not `EMBEDDED`'s) as
  corrections; ED9's subsystem ruling; §5 R's "the guest's PE and Linux D3D9 binaries carried snmalloc's assertions"
  made to say what Task 0 measured. Privacy-scan spec, plan, ledger. Commit on `dxmtrs`: `Spec: DXMTRS step R, D3D9
  at src/d3d9`.
- [ ] **Step 2: DXMTRS main.** Privacy-scan `git -C $D log -p main..stepR-layout`; `git -C $D switch main && git -C $D
  merge --ff-only stepR-layout`; `git -C $D push --dry-run origin main` (a fast-forward of `main` only); push. Remove
  the worktrees (`chmod -R u+w` first) and lane branches (`git cherry` empty first). Expected: one worktree, `main`
  only, no stash.
- [ ] **Step 3: Pin, one commit** (P13). `git switch dxmtrs && git merge --squash stepR-mn`, `DXMT_COMMIT=<main's sha>`
  in `dxmt/pins`; commit `DXMT: D3D9 at src/d3d9, built by meson (DXMTRS <sha>)`; `git diff dxmtrs stepR-mn -- .
  ':!dxmt/pins' ':!docs/superpowers' ':!.superpowers'` empty (Step 1's spec and ledger paths are on `dxmtrs` only),
  then `git branch -D stepR-mn`.
- [ ] **Step 4: Pinned build.** `git -C $D checkout -q --detach $(cat $SRC/dxmt.applied)`; `make wine-arm64` re-fetches
  the pin (the log says so). On it: `$A/DXMT/version` = `<sha>+<series>` (not `+dev`); `git -C $D status --porcelain`
  empty; the key; IF5 (the emitter version from the pinned tree, `CARGO_TARGET_DIR=$S/cargo-pinned`); `ident-diff.sh
  $S/before/mac <wine.app's D3D9 files> ED1 ED3 ED4 ED5`; nvngx, nvapi64, winemetal (dll, so) against `$S/before/mac`
  → `ED1 ED4`; x64 and ARM64EC check.sh 0 FAIL (flaky rule); `sh eac-vm/dxmt/build.sh` and `check-build.sh` ok, then
  `ident-diff.sh $S/before/guest $W/dxmt-guest ED1 ED2 ED3 ED4 ED5 ED7 ED9`; `sh eac-vm/dxmt-native/build.sh` and its
  dylib against `$S/before/native` → `ED1 ED4`. `git -C $D remote set-url --push origin
  git@github.com:Mac-Neutron/DXMTRS.git`.
- [ ] **Step 5: MacNeutron main.** `git switch main && git merge --ff-only dxmtrs`, `git push --dry-run origin main`,
  push; back on `dxmtrs`. A failed fast-forward (here or Step 2) STOPs for the maintainer; never rebase `main` or
  `dxmtrs` unasked.
- [ ] **Step 6: Step 3's plan** (R-D21): a whole-plan pass onto the new layout — crates at `src/d3d10`, `src/dxgi`
  (rule 2), the root workspace and RC instead of `R/windows`/`R/unix` commands, `tools/audit.sh`, `docs/CONVENTIONS.md`,
  `APP_POINTER_DIR` `src/d3d9/src src/dxgi/src src/d3d10/src`, its Task 5a dropped (done here), the lane kit's
  `CARGO_TARGET_DIR` outside the tree. Expected: `/usr/bin/grep -nE 'R/windows|R/unix|mtld3d/(windows|unix)|scripts/audit\.sh|Task 5a|nextest'`
  over the rewritten plan → none, and every `src/`, `tools/`, `tests/d3d9/` and `docs/` path it names that should
  exist at R's head passes `test -e` in `$D` (the ones its own tasks create excepted, listed). Ledger and program
  ledger: step R done, the pinned sha.

## Review record

Plan review 2026-10-30 (lenses: identity, build, gates); each finding verified against the source before applying.
Numbered in the order received; "with #N" marks a merged duplicate.

1. Critical, no re-check after the fix pass — applied: Task 10 Steps 1 and 3 (identity block once, gates per fixed
   file, a hashed-file fix stops for the maintainer), Task 11 Step 4 (IF5, guest, nvngx/nvapi64/winemetal, native).
2. Critical, `vendor/snmalloc-sys/LICENSE` would drop schrodingerzhu's notice — applied (verified `notices.py:42` is
   `EMBEDDED`, wine.app's `LICENSE-MIT` reads schrodingerzhu): R-D19, Task 1 Files/Step 1, ED8, Task 7 Step 5, Task 9.
3. Critical, `mtld3d-mtl`'s 243 workspace-lint findings — applied: Task 2 Step 0 (controller ruling (a)/(b)), Step 3's
   binding rules, Step 4's guest `.so` ident-diff, Rules check. Count is the reviewer's measurement; verified here only
   that `mtl/Cargo.toml` has no `[lints]` and the table is pedantic + nursery.
4. Important, P8's order moves CRT imports to `ntdll.dll` — applied: P8 starts with `-lmingw32 -lmingwex -lucrt`
   (verified `mtld3d-build.sh:81`, meson's `LINK_ARGS`, `winemetal.dll`'s ntdll imports); Task 4 Step 3's import set.
5. Important, Subsystem/FileAlignment/`.buildid` unseen — applied: P3's PE header, debug-directory and resource rows,
   ED9 (verified GUI 0x2/512 vs CUI 0x3/4096).
6. Important, P6's bare compiler name; another LLD on `PATH` — applied: P6 `find_program(...).full_path()`,
   `arm64x-link.sh` uses `<bin>/lld-link` and prints its version, Task 0 `toolchain.txt`, IF2, Task 3 Step 3.
7. Important, IF8 checked nothing — applied: P3's string set, IF8.
8. Important, feature difference "justified in the ledger" — applied: Task 3 Step 4 requires equality (with #43).
9. Important, the p profile's guest D3D9 link never built — applied: Task 9 "The p profile", then a std rebuild.
10. Important, R-D5's guest resource-compiler premise wrong — applied: R-D5 short form, ED5, Task 4, Task 11 Step 1.
11. Critical, PE environment runs the proxy's `stable` — applied: Global Constraints (toolchain `bin` before `$T`).
12. Critical, `cc.cmd_array()[0]` bare — applied with #6.
13. Critical, `export` of hyphenated names — applied: P6 underscore spelling, Task 3 Interfaces and Step 1's eval test.
14. Critical, lock union fails `--locked` — applied: P15's per-(name, version) merge, seed fallback.
15. Critical, `cargo build` of the two cdylib crates — applied: Task 2 Step 4 `cargo rustc --crate-type staticlib`.
16. Critical, `MTLD3D_RUST`/`$D` unset in the `eac-vm` scripts — applied: Task 7's per-script fetch forms.
17. Critical, layout32-check and d3d9-gate lack `linux-cargo.sh`'s inputs — applied: Task 7 (pins, `LM`, tools `PATH`,
    sysroot per arch; d3d9-gate builds both arches).
18. Important, lane r4t lacks the x86 rows — applied: branches from Task 4 (Execution, P12, Task 6, Global Constraints).
19. Important, archive guard on the shim — applied: only when the output is `d3d9.dll`.
20. Important, no meson API for the archive path — applied: P7 `fs.is_file()` and `error()`.
21. Important, `ln -sf` race — applied: P6 atomic rename.
22. Important, `mingw-cc.sh` mode — applied: Task 3 Files and Step 2 (`100755`).
23. Important, Task 5's grep hits R4a's files — applied: excluded in Task 5; full grep at Task 8 Step 0 and Task 9
    (with #40).
24. Important, INDEX superset fails on snmalloc-sys's line — applied: `name version` set and ED8 (with #2, #34).
25. Important, Task 11 switch and diff — applied: Step 1 `git switch dxmtrs`, Step 3 excludes Step 1's paths.
26. Important, identity scripts never shown to fail — applied: Task 0 RED; `ident-diff.sh` takes its ED names, ED4
    wherever ED1.
27. Important, optional-header fields, merged export views — applied with #5, per view with counts. Its "P8 adds
    `-Wl,--subsystem,windows`" not taken: meson appends `-Wl,--subsystem,console` after `link_args` and lld takes the
    last one; ED9 (its stated alternative) instead.
28. Important, flags and profiles invisible — applied: Task 0 `flags/`, Tasks 1, 3, 4 comparisons, Task 8's exact ED7.
29. Important, UT counts — applied: Task 0 `tests/`, Tasks 1 and 2.
30. Important, licence inheritance and a Licences row — applied: Task 0 `packages.json`, Task 1 Steps 1-2, Task 9.
    Partly not applied: "check `vendor/snmalloc-sys/LICENSE` against `mtld3d-notices.py:42`" contradicts #2 (line 42 is
    Microsoft's `EMBEDDED`); checked against the before `LICENSE-MIT` instead.
31. Important, rust-check legs and audit selectors unproven — applied: Task 6 RED per leg, printed commands, 688.
32. Important, Task 7's sweep cannot find the old paths — applied: Step 1 over `git ls-files`; `fetch.sh:3` added.
33. Important, RM scripts unchecked — applied: (1) loopback, (2) arm64 exe and presence RED, choice settled in Step 1,
    (3) Task 9's key read-back and `wineserver -k0` (the option needing no ruling), (4) exact machine grep with RED,
    (5) stray `.so` RED.
34. Important, INDEX check cannot fail on missing legs — applied: equals `notices-legs.txt` (`-e normal`, R-D19's legs).
35. Important, fetch lines never exercised — applied: Task 7 Step 6 through `eac-vm/dxmt-native/build.sh`, Step 1's
    placement scan.
36. Important, assert check can never fail; ELF needs `__assert_fail` — applied: Task 8 `production_asserts` in
    `lib.sh`, RED through bundle.sh's path, Mac ident-diff.
37. Important, Task 9 omits step 2 rows — applied: cover.sh, pair, both layout32 counts, d3d-smoke, dxmt gate, loopback.
38. Important, conformance accepts regressions — applied: step 2's figures, control only for Wine's code.
39. Important, identity before the builds; Task 10 re-runs only failed rows — applied with #1.
40. Important, narrow grep, unaudited docs, `.gitignore` lines — applied: widened grep, Task 8 Step 0 audit, root
    `.gitignore` lines.
41. Important, RC's lane form undefined — applied: RL, literally.
42. Important, Task 11 Step 6 unchecked — applied: grep and `test -e`.
43. Important, feature-set escape — applied with #8 (Task 0's `sed` strips the markers).
