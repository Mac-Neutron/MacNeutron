# DXMTRS step 4a: the translator in Rust on LLVM 15 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Everything behind `airconv_public.h` (airconv, the DXBC parser it uses, our DXIL translator) is Rust on the
LLVM 15 MacNeutron ships, and its typed `llvm-dis` text is identical to the C++'s on every corpus module; one switch (Z)
wires it into `winemetal.so` and the native `winemetal.dylib`, moves the translator key once, and runs every gate once.

**Architecture:** Two crates under `src/airconv`: `airconv-ir` (pure: container, token parser, decode, CFG, the
signature handlers as data, binding recorders, reflection, MTLB packing, sha256; builds for every target, for step 5's
PE users too) and `airconv` (LLVM: hand-declared C API bindings, one `Ty` for every type the translator makes, a C++ shim
for what LLVM 15's C API lacks, the AIR layer, DXBC and DXIL lowering, the 14 C exports; an rlib of `winemetal`). Lanes
grow the crate behind exports that answer "not ported"; each lane is checked host-only, through MacNeutron's
`dxil-translate` linked against the Rust staticlib, against a frozen C++ baseline (`dis=`, `refl=`, `tags=`, failure
reasons). Z replaces `libairconv.a` in both images in one commit and one pin bump.

**Tech Stack:** Rust 1.99.0 (edition 2024, cargo `--offline`), the pinned nightly's rustfmt, LLVM 15.0.7 (MacNeutron's
recipe, C API + one C++17 shim built by `cc`), Apple's `metal` through `wine-arm64/tools/xcrun-metal.sh`, meson 1.12,
Objective-C++ (`dxil-translate`), `python3 -I` (corpus-diff), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§3 layout after step R, §4 Decided, §5 step 4 and the
Pace paragraph, §6, §7, §8). Inputs: `build/step4/map/0-porting-map.md` (synthesis; K1-K20 taken as written unless
changed below), `1-airconv.md` .. `5-rust-shape.md` (readers), `6-critic.md` (rows 1-27, all accepted),
`7-decisions.md` (4-D1..4-D10, binding); step 5's `build/step5/map/7-decisions.md` (5-D6: the key rule and the
DXBCParser/root-signature edge). Format and command forms: `2026-10-30-dxmtrs-stepR-layout.md` and
`2026-10-29-dxmtrs-step2-winemetal.md`; gate commands from `build/step2/reports/gates-{mac,native,vm}.md`.

**Written ahead (spec §5 Pace).** Paths are step R's layout (`src/winemetal/unix` = crate `winemetal`,
`src/winemetal/wire`, `tools/{audit,rust-check,winemetal-layouts,cargo-staticlib}.sh`, `docs/CONVENTIONS.md`, one root
workspace). Step R landed (DXMTRS `main` 1ccec40, which MacNeutron `dxmtrs` c36dab4 pins); Task 0 re-checked every
path and line cited here against those two trees and ledgered each correction. DXMTRS line numbers are 1ccec40's (in
`src/airconv`, `libs`, `src/d3d11` and `src/d3d12` equal to da6ed2d's: R did not touch them); G1's commits on
`step4a-translator` shift `A/dxbc_converter.cpp` (+1 after `:801`, +2 after `:1024`, +3 after `:1328`),
`A/dxbc_converter.hpp` (+2 after `:463`) and `A/dxil/dxil_types.hpp` (+3 after `:146`). MacNeutron line numbers are
`dxmtrs`'s (c36dab4): `dxil-translate.mm`'s are the tool before G2 and G3 changed it on `step4-mn`.

Paths: `<M>` = `~/Documents/MacProton`, `$B` = `<M>/build`, `$D` = `$B/wine-arm64-src/dxmt` (the pipeline's
DXMTRS checkout), `$SRC` = `$B/wine-arm64-src`, `$W` = `$B/eac-vm-work`, `$S` = `$B/step4`, `$R` =
`~/.rustup/toolchains`, `A/` = `src/airconv/` in a DXMTRS tree, `L15/` = `$B/dxmt-src/llvm-project/llvm`. Ledger:
`.superpowers/sdd/2026-10-30-dxmtrs-step4a-translator/progress.md`.

## Decisions

Binding (`7-decisions.md`), short form:

- **4-D1** Two plans: this one (4a: lanes G, P, L, A, D, X, switch Z) and 4W (our typed-pointer AIR writer for LLVM 23,
  its own plan and lane in Mac-Neutron/llvm-project). 4b (the LLVM 23 move) is planned after Z, from W's results.
- **4-D2** LLVM bindings are hand-declared in `airconv` with the C API's own signatures; no `llvm-sys` (it may need
  `llvm-config`, which the LLVM 15 install lacks). Cost: one declaration file per LLVM version (4b redoes it).
- **4-D3** C++ that stays: the pass pipeline and airconv's two passes (`A/transforms/*`) in the crate's build-script shim
  through 4b (MemorySSA has no C API); the barrier pass freed from AIRBuilder by a local declaration committed before
  the baselines; `src/winemetal/unix/sm50_wow64.c` until step 6. Spec §1's "no C++ left" is reached at step 6; the spec
  is amended at Z5.
- **4-D4** The key after Z hashes `cargo tree -p airconv` (resolved dependencies) and the Rust toolchain, not the root
  `Cargo.lock`, plus the shim's inputs (P7).
- **4-D5** No process exit or abort on bad input: every `LLVMContext` gets a diagnostic handler; bad DXIL returns the
  C++'s `DXIL:` text; no `println!` on any path; D3D11's uninitialised reflection is written through raw pointers only.
- **4-D6** One non-LTO LLVM 15 for all lanes, outside the pipeline's build dirs (`$S/llvm15`); the shipped build keeps
  ThinLTO. Step 4's commits stay off DXMTRS `main` until Z.
- **4-D7** Corpus: the repo's shaders, `tests/dx11`'s HLSL compiled to DXBC, and the game captures the maintainer
  allowed on 2026-10-30 ("Yes, both"): SMITE 2's DXMT capture and 3DMark's D3D12 pipeline recording, read in place,
  read-only, outputs only under `build/step4`, never committed or copied into a repository. No D3D11 capture ("Skip it
  for now"; revisited at step 4's milestone). P1 says where each lands.
- **4-D8** The gate tool is MacNeutron's `dxil-translate`: its reach-ins move behind a test-only getter; it gains `dis=`,
  `--no-metal`, every DXBC stage, `--pipelines`; `corpus-diff.py` compares `dis=` and failure reasons.
- **4-D9** Milestone benches (spec §8, end of step 4) need the maintainer's go-ahead, asked when Z and 4b are ready.
- **4-D10** Three adversarial Opus plan reviewers.

Critic rows (`6-critic.md`, all accepted; 7-decisions cites some by an older numbering, see Review record), where each
lands: 1 → P5, X1; 2 → G1b, A1; 3 → 4-D2, L2; 4 → P3, Review Focus 3; 5-8 → L2 (8 also P15); 9 → T1; 10 → T1,
P3, P15; 11 → P15; 12 → P4; 13 → P4m, T1 (README); 14 → Global Constraints; 15 → P7, Z2; 16 → T1; 17 → Z1; 18 → P10;
19 → Z3; 20 → P8; 21 → G2; 22 → P2, G5a; 23 → 4b handoff (Z5); 24 → Global Constraints (branches); 25 → P7; 26 → G4;
27 → P1.

Plan rulings (new here; each: decision — why — cost if wrong):

- **P1 The game corpora are allowed (4-D7) and planned.** SMITE 2's capture (`DXMT_DXIL_DUMP` folder,
  `~/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt`: 10,271 `.dxil`, 150 `rs-*.bin`,
  `pipelines.txt`) is corpus C8: G6 (`--pipelines`) follows G2, G6 records C8's baseline, X1, X2 and Z3 run it
  (hashes only, no `--dis`). 3DMark's D3D12 pipeline recording (a DXMTPRC1 `.pipelines` file, path ledgered in Task 0
  from the maintainer's answer) becomes corpora C10 (its DXBC) and C11 (its DXIL) through G7's extractor
  (`dxmt/tools/prc-extract.py`, ~40 lines of `python3 -I`): its shaders and compute pipelines written under
  `$S/corpus`, translated by P3, D1, D2, X1, X2 and Z3. No D3D11 capture (P12 is out). Both are read in place,
  read-only; nothing from them is committed. — C8 is the only DXIL under real root
  signatures (1.1 flags, static samplers, root constants and descriptors, heap flags) and real RTV formats; C10 is the
  only SM5.1 DXBC under real root signatures (critic 27). — none.
- **P2 One LLVM for baseline and lanes.** `$S/llvm15` = the shipped recipe without LTO: `[ -f $S/llvm15/.complete ] || [
  -d $S/llvm-project/llvm ] || cp -cR $B/dxmt-src/llvm-project $S/llvm-project` inside the pipeline lock (rule 1: `make
  wine-arm64` owns that clone; `fix_llvm` already applied), then, outside it but inside `/usr/bin/lockf -k
  $S/llvm15.lock` with the stamp check (4W's contract: whichever plan runs first copies and builds once; `build_llvm`
  shares one `$S/llvm15-build` and deletes it on a stamp mismatch), `build_llvm arm64 $S/llvm15 $S/llvm-project` with
  `UNIX_CPU=-mcpu=apple-m5` and `UNIX_LTO` empty (`dxmt/llvm.sh:4-42`: Release, assertions off, so InstCombine's NDEBUG
  default holds) → `.complete` = `llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto`. The C++ baseline is recorded on it and,
  once, on the shipped ThinLTO install; the two manifests must be equal (G5b). At Z the same recipe becomes a MacNeutron
  product, `$SRC/llvm-arm64-nolto` (`make llvm-nolto`, Z2), which `tools/rust-check.sh`'s `AIRCONV_LLVM` names from then
  on, so no later step depends on scratch. — Lanes link LLVM in every `cargo test`; ThinLTO archives redo LLVM's codegen
  per link (critic 22). — If LTO changed output, G5b says so before any port.
- **P3 Lane tool builds use cargo's `release` profile** (`debug-assertions`, `overflow-checks` on) with feature
  `oracle`; Z's images use `production-unwind` through meson. — An overflow where the C++ wraps aborts a lane run
  instead of hiding; the port writes `wrapping_*` where the C++ wraps. — none.
- **P4 Gate thread 256 KiB** (critic 12): every translation in `dxil-translate` runs on a 256 KiB pthread. If the C++
  baseline overflows on a corpus shader, G5b stops and the controller raises it to 512 KiB (the native and VM-host
  limit), ledgered. P4m (critic 13): `A/sha256.hpp` and `A/metallib_writer.cpp` are hand-ported (no `sha2`); the
  README's licence list names their origins (Brad Conte / Florian Ziesche, "as is"; metallib writer: no notice), and
  the DXBCParser port's MIT notice stays in the ported files. — none.
- **P5 Bitcode is parsed through the shim's `parseBitcodeFile`** (`airconv_shim_parse_bitcode`, the buffer named
  `"dxil"` as `LoadModule` names it, `A/dxil/dxil_metadata.cpp:131`), which returns `toString(err)`, so `"DXIL: can't
  read the bitcode: " + text` is the C++'s byte for byte; every context still gets the diagnostic handler (`extern "C"`,
  never `C-unwind`), which covers `Linker` and anything else that emits through the context. **The handler's rule**
  (with a handler installed LLVM hands it every diagnostic and prints none, `L15/lib/IR/LLVMContext.cpp:243-263`):
  it records only `LLVMDSError` (`LLVMGetDiagInfoSeverity`); a warning or note is written to stderr as the C++ printed
  it (`warning: <description>`, `let _ = writeln!(io::stderr().lock(), …)`) and the call goes on; remarks are dropped
  (LLVM filters them by default). After each parse, link and verify, a recorded error fails the call with P15's
  `LLVM error: <description>`, no `DXIL:` prefix, so D3D12 answers E_FAIL (`d3d12_pipeline.hpp:107-112`). — 4-D5's
  handler alone sees `LLVMGetDiagInfoDescription`'s wording, not `toString`'s; the C++ continues past warnings (the IR
  mover's data-layout and triple warnings from `linkShader`, `L15/lib/Linker/IRMover.cpp:1564,1573`; the reader's
  invalid-debug-metadata one, `L15/lib/IR/AutoUpgrade.cpp:4215`) and exits on errors. — none.
- **P6 Exports and calls.** `airconv` exports 14 functions (`airconv_public.h:325-366`'s 12,
  `DXILCompilePassThroughVertex`, `DXILGetEntry`) as `#[unsafe(export_name = "SM50Initialize")] pub unsafe extern "C"`
  (edition 2024; never `C-unwind`: a panic aborts at the boundary). At Z `g10.rs`'s `unsafe extern "C"` block goes and
  its handlers call `airconv::sm50_initialize` etc. directly (which also makes cargo link the rlib). LLVM's libraries are
  linked `static:-bundle` (meson supplies them to the images, `build.rs` to tests and the tool); the shim's archive is
  bundled. — A bundled LLVM inside `libwinemetal-rust.a` would duplicate every LLVM symbol meson links. — none.
- **P7 The key at Z.** `translator_key` (`wine-arm64/lib.sh:163-189`) keeps today's composition (`macneutron
  translator 1`, the same stream byte for byte) for a tree without `src/airconv/Cargo.toml` (older pins: `inert.sh`'s
  base side, development builds of an old pin); a tree with it gets `macneutron translator 2`:
  - files: `src/airconv` (Rust, shim, passes, helpers), `src/d3d11/d3d11_shader.cpp` (until 5b),
    `src/winemetal/airconv_thunks.[ch]`. Gone: `libs/DXBCParser` (no longer a translator input), `include`,
    `meson.build`, `meson.options` (after Z cargo builds the translator: `build.rs` sees only `-I$AIRCONV_LLVM/include`
    and P10 keeps meson's flags away, while step 5 edits these files and step 6 removes meson; 5-D6, 5-D7, spec §6);
  - the cargo settings that do reach it: the root `Cargo.toml`'s `[profile.release]`, `[profile.production]` and
    `[profile.production-unwind]` sections (the inherits chain the images build with: `lto`, `panic`,
    `overflow-checks`) and `.cargo/config.toml`'s `[build]`, `[env]` and `[target.aarch64-apple-darwin]` sections
    (`[build]` and the last absent at 1ccec40: only `x86_64-apple-darwin` has rustflags, and they never reach this
    build), printed by one `awk`;
  - `rustc +$MTLD3D_RUST -vV`, and `cargo +$MTLD3D_RUST tree --locked --offline --no-dedupe -p airconv -e normal,build
    --target aarch64-apple-darwin --prefix none --format '{p}'` with each ` (/abs/path)` and each path crate's version
    removed, `LC_ALL=C sort -u` (path crates are hashed by content through `src/airconv`; a workspace version bump must
    not move the key);
  - unchanged: `buildtype`, `llvm $LLVM_TAG`, `dxmt/llvm.sh` (which loses the tool builders, P8), `xcrun-metal.sh`,
    `c++ --version`'s and `metal --version`'s first lines.

  **No line is computed inside the hashed pipeline.** POSIX sh has no `pipefail`: under `set -eu` a failing command in
  `{ …; } | shasum -a 256 | cut` stops the group and the pipeline answers `cut`'s 0, a well-formed key without the
  lines after it (`sh -c 'set -eu; k=$({ echo a; nonexistent-cmd -vV; echo b; } | shasum -a 256 | cut -d " " -f 1);
  echo "key=$k rc=$?"'` prints a key and `rc=0`). So each command's output is taken first, `_tk_x=$(…) || die`, and
  only `echo`ed inside the group: the file hashes, `rustc -vV`, `cargo tree` (also `[ -n "$_tk_dep" ] || die`),
  `c++ --version`, `xcrun -sdk macosx metal --version` (for both compositions: the stream, so v1's key, is unchanged
  when they pass). `translator_key` also dies when a `(<path>)` in `cargo tree`'s output is not under
  `$1/src/airconv` (a path crate elsewhere, map 0 K2's logger say, would be in neither the file list nor the dependency
  lines), and when `meson.options`' `cargo_toolchain` default is not `$MTLD3D_RUST` (`wine-arm64/build.sh:528-530` and
  `eac-vm/dxmt-native/build.sh` pass no `-Dcargo_toolchain`, so the images build with that default while the key
  hashes `rustc +$MTLD3D_RUST`). D3D11's switch (step 5b) deletes `d3d11_shader.cpp`: if 5b's
  switch is gated when Z pins, both ride one bump and 5b's compile-arguments file replaces that entry; otherwise Z pins
  with the C++ entry and 5b rides 4b's bump (5-D6: "the next pin that moves the key anyway"). No player release is cut
  between Z and 4b unless the maintainer asks (K1): turnover is per release, not per pin. — Critic 15 and 25; a key
  that moves on step 5's meson edits, or silently drops lines, turns caches over or reuses translations from another
  translator. — 5b's switch may wait for 4b's pin.
- **P8 Tool builders leave `dxmt/llvm.sh`** for a new `dxmt/tools.sh` at Z (critic 20), so later tool edits never move
  the key; `build_translate` links `cargo rustc -p airconv --crate-type staticlib` (P6). — none.
- **P9 DXBC at both Metal versions.** D3D11 passes the device's version (`d3d11_shader.cpp:33`, 3.2 on this Mac) and
  `check.sh:1051` notes no row runs the 3.2 path. Every DXBC corpus runs `--metal 310` and `--metal 320`, and C4-C6
  once more at 320 with `--shader-flags 3` (D3D11's shipped app profiles turn on `sampleNaNToZero` and `defuseFma`,
  which select nine NaN-to-zero branches and one defuse-FMA branch of `A/nt/air_builder.cpp`; D3D12 passes 0, so
  DXIL runs keep flags 0); `dxil-translate`'s default stays 310 (check.sh rows unchanged). — none.
- **P10 No rustc `target-cpu` at Z** (critic 18): the C++ was `-mcpu=apple-m5` and ThinLTO'd with LLVM; Z records the
  translate-only time against the baseline as information, with the gap named. — Adding `target-cpu` changes winemetal's
  codegen too, its own measured commit. — translation may be slower until then.
- **P11 Per-task Rust checks are crate-scoped** (CK below); the full `tools/rust-check.sh` (RC) runs once, at Z3. —
  Maintainer: a task runs only its own checks. — a D3D9 or winemetal finding surfaces at Z3.
- **P12 No D3D11 capture in 4a** (4-D7: "Skip it for now"). C9 is dropped; the question returns at step 4's milestone,
  with a D3D11 game bench's go-ahead. — The maintainer's answer. — D3D11's real argument chains are gated by G3's
  variant matrix and the runtime rows only.
- **P13 MacNeutron's step 4 commits** live on `step4-mn` (worktree `$S/wt-mn`, from `dxmtrs`) until Z5 squashes them
  with the pin into one `dxmtrs` commit: `dxil-translate` needs `DXILGetEntry`, which no pinned DXMTRS has before Z. —
  none.
- **P14 Generated and shared files.** Only T1 edits the root `Cargo.toml`/`Cargo.lock` and `docs/CONVENTIONS.md`;
  only Z1 edits `src/winemetal/unix/Cargo.toml`. A lane adding a derive runs `sh tools/audit.sh --update-derives` in
  its commit; after every rebase the controller re-runs it (the file is generated) and resolves `Cargo.lock` as R's
  P15 does (`cargo metadata --offline` once without `--locked`, then `--locked` holds; never `cargo update`). Conflicts
  with steps 3 and 5 are expected only in root `members` and the lock. — none.
- **P15 Errors added on guest bytes** (K11, listed as step 1b listed its exceptions): a token walk or container read
  past its blob → `Invalid DXBC bytecode: <what ran past>` (the C++ trusted the length token and read out of bounds);
  where the C++ checks (each `ReadDXBC` branch, `libs/DXBCParser/BlobContainer.cpp:27-85`; `FindDXIL`'s three
  refusals) the text stays the C++'s byte for byte (C7 compares them); a name with an interior NUL → `Invalid DXBC
  bytecode: a name holds a NUL` (critic 8); an LLVM error diagnostic (a failed helper link, anything the C++ let
  `exit(1)`) → `LLVM error: <description>` (P5); `SM50GetErrorMessage` with size 0 writes nothing and returns 0; an
  AIRBuilder null → `UnsupportedFeature`'s text naming the op; `get_max_potential_workload_count` on an unknown domain
  → 0 (critic 10). Error texts are bytes (`Vec<u8>`), never `String`: guest names (`"DXIL: static (non-const) global
  " + name`, `A/dxil/dxil_converter.cpp:103`), the verifier's text and `toString(err)` need not be UTF-8, and a lossy
  conversion would change the bytes and the length `SM50GetErrorMessage` returns. Kept as the C++: the unhandled-opcode
  `InstNop` and its stdout line, written `let _ = writeln!(io::stdout().lock(), …)` (critic 11), a null `pRefl`
  (skips the hull write-back, `dxbc_converter.cpp:1250-1291`). — Valid shaders are unaffected (0 new failures on the
  corpus). — a shader that worked by luck now refuses with a message.

## Global Constraints

- Agents never push, merge to `main`, fetch or download. Z5's push, pin and pinned re-fetch run under the maintainer's
  standing authorization, by the controller only. Never open MacNeutron.app, start Steam, run a game, or read
  `~/Library/Application Support/MacNeutron` (4-D7's two captures excepted, read-only, in place: P1). Kill by exact
  PID. Benches once, only with 4-D9's go-ahead. Home written `~`. Commits end with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`; DXMTRS subjects `<area>: sentence`.
- **Branches.** DXMTRS: `step4a-translator` from `main` after step R's fast-forward (R Task 11 Step 2; before that,
  cut from `stepR-layout`'s head and rebase); lanes `step4a-<lane>` in worktrees `$S/wt-<lane>`
  (`git -C $D worktree add …`, submodules filled read-only as R's lane procedure does). Nothing from these branches
  reaches `main` before Z5 (critic 24). MacNeutron: `step4-mn` (P13). Never edit in `$D` or MacNeutron's main checkout.
- **The lock.** Every command that builds or installs MacNeutron's runtime or a DXMT build, runs a gate reading
  installed outputs, or checks out anything in `$D` runs as one job: `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c
  '<checkout + build + tests>'`, checking out (detached) what it needs first, recording commit and cdhash in its log,
  leaving no branch, stash or worktree in `$D`. Lock jobs here: G4 (dxbc.exe), G5a (clone copy), G5b (C++ baseline
  build, the pinned tool's copy), Z3 (with `make llvm-nolto`) and Z5. Everything else is host-only
  (`build/pipeline.md` rule 4).
- **`<M>` is switched only inside a lock job.** A Z3 or Z5 job that needs MacNeutron at `step4-mn` starts with
  `git -C <M> switch -q --detach step4-mn` (detached: `$S/wt-mn` keeps the branch) and ends with `git -C <M> switch
  -q dxmtrs`, both in its log; the controller announces each such window in the program ledger, and no step commits in
  `<M>` during it (steps 3 and 5 commit on `dxmtrs` there). Running the gates from `$S/wt-mn` instead does not work
  as-is: `dxmt/check.sh:13` takes `$ROOT/build`, not `BUILD_DIR`, and `eac-vm/dxmt/ref.sh:20` `$ROOT/.build`.
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE`; the builder starts with `VM_SHARES` per the builder-out-share note; any `make` or
  `<M>/dxmt/*.sh` from a MacNeutron worktree exports `BUILD_DIR=$B`.
- **Rust** (host): `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH cargo … --offline`, `CARGO_TARGET_DIR=$S/cargo-<lane>`,
  and for anything building `airconv`: `AIRCONV_LLVM=$S/llvm15 AIRCONV_XCRUN=<M>/wine-arm64/tools/xcrun-metal.sh`
  (a `sh` script, mode 0644: `build.rs` runs it through `/bin/sh`, T1). Tests that read the frozen baseline find it
  through `AIRCONV_STEP4_BASE=$S/base` (test-only, Rules check): unset, such a test prints why and passes (RC at Z3
  and every later step); set, a missing folder or file fails it. CK sets it for every task from G5b on.
- **CK, the crate checks** (each task, from its worktree root; from G5b on with `AIRCONV_STEP4_BASE=$S/base`
  exported):
  - fmt: `PATH=$R/nightly-aarch64-apple-darwin/bin:$PATH cargo fmt --all --check`;
  - clippy: `cargo clippy --offline --target aarch64-apple-darwin -p airconv-ir -p airconv --all-targets --features
    airconv/oracle -- -D warnings`, and `-p airconv-ir --all-targets` on `aarch64-`, `arm64ec-`, `x86_64-`,
    `i686-pc-windows-msvc` (no build script, no linking: clippy only);
  - doc: `RUSTDOCFLAGS="-D warnings" cargo doc --offline --no-deps -p airconv-ir -p airconv --target
    aarch64-apple-darwin`;
  - test: `cargo test --offline --target aarch64-apple-darwin -p airconv-ir` and `-p airconv --features oracle`;
  - audit: `git -C <wt> add -A && sh tools/audit.sh` → `audit: clean (<n> files)`, exit 0;
  - site counts (critic 5-7, lanes that port lowering: L3, A1, A2, D1-D4, X2): for each C++ file the task ports, the
    `grep -o` counts of `CreateExtractElement(`, `CreateInsertElement(`, `CreateConstGEP1_32(`, `CreateConstGEP1_64(`,
    `CreateConstInBoundsGEP1_32(`, `CreateStructGEP(`, `CreateVectorSplat(` and `getOrInsertFunction(` equal the Rust
    counts of their helpers (`extract_element`/`extract_element_u64` together, likewise insert; `const_gep1_32`,
    `const_gep1_64`, `const_inbounds_gep1_32`, `struct_gep`, `vector_splat`, `get_or_insert_function`); the
    value-index extract/insert sites (an explicit `getInt32`/`getInt64`/`ConstantInt`/`Value *` index) are listed in
    the lane's ledger by C++ line, and the Rust value-index count equals that list's length, every other site being a
    `_u64` call. Totals on da6ed2d by `grep -o` over `A/**/*.{cpp,hpp}` (re-counted at Task 0): 48 extract, 109 insert,
    8 + 1 const GEP1, 11 in-bounds GEP1_32, 3 struct GEP, 10 splat, 57 `getOrInsertFunction`.
- **The lane run (LR)** of a task's stages: `sh $S/tool.sh rust <wt> $S/bin/<lane>` (G5b), `(cd $S && shasum -q -c
  base/inputs.sha256)` (G5b: the corpus snapshots, the baseline tool, `$S/llvm15/.complete`; before a C8, C10 or C11
  run also `base/C8.sha256` or `base/C10.sha256`, G6/G7), then for each corpus of
  the task `$S/bin/<lane>/dxil-translate $S/corpus/<c> --no-metal --dis $S/runs/<lane>/<c>.dis [--metal 320]
  [--shader-flags 3] > $S/runs/<lane>/<c>.txt 2> $S/runs/<lane>/<c>.err` (`<c>` = the corpus id; `C5-320` for a 320
  run, `C5-320f` for 320 with flags 3; `$S/base` uses the same names), then `python3 -I
  $S/wt-mn/dxmt/tools/corpus-diff.py $S/base/<c>.txt $S/runs/<lane>/<c>.txt [--stages …]` → `0 new failures, 0
  fixed, 0 changed, 0 extra`, exit 0; the oracle's untyped list (D1 and later depend on L4 for it): `/usr/bin/grep
  '^oracle-untyped' $S/runs/<lane>/<c>.err | LC_ALL=C sort -u > $S/runs/<lane>/<c>.untyped` and `LC_ALL=C comm -13
  $S/untyped/<c>.txt $S/runs/<lane>/<c>.untyped | wc -l` → 0 (a new entry fails; one the run lacks is ledgered,
  `comm -23`); a changed
  module is shown with `diff $S/base/<c>.dis/<f>.ll $S/runs/<lane>/<c>.dis/<f>.ll`. `air=` differences with equal
  `dis=` are ledgered, not failed. **Initialize-only runs** (P3, X1, while compile still answers "not ported"):
  `dxil-translate $S/corpus/<c> --initialize-only > $S/runs/<lane>/<c>-init.txt`, compared with
  `$S/base/<c>-init.txt` by the same corpus-diff, same Expected line.
- **Corpora**, snapshotted by G5b into `$S/corpus/<c>` (read there only; C8 and the 3DMark recording are read in
  place and hashed, never copied):

  | Id | Source | Shaders | Runs |
  |---|---|---|---|
  | C1 | `<M>/dxmt/tests/shaders` | 64 DXIL (96 metallibs) | 310 |
  | C2 | `<M>/dxmt/tests/dxil` | 33 DXIL | 310 |
  | C3 | `<M>/dxmt/tests/dxil/reject` | 7 DXIL, reasons compared | 310 |
  | C4 | `<M>/dxmt/tests/dxbc` | 2 DXBC CS | 310, 320, 320f |
  | C5 | `<M>/dxmt/tests/shaders/d3d11` | 17 DXBC, every stage | 310, 320, 320f |
  | C6 | `$S/wt-mn/dxmt/tests/dxbc-corpus` (G4) | `tests/dx11`'s 9 HLSL, G4's GS, HS/DS, barrier CS and system-value shaders, DXBC | 310, 320, 320f |
  | C7 | `$S/corpus/C7` (G5b, scratch) | malformed containers from C1's `cache.*`, reasons compared | 310 |
  | C8 | SMITE 2's capture (P1), in place | 10,271 DXIL, 150 root signatures, `--pipelines` | 310 |
  | C10 | `$S/corpus/C10` (G7, 3DMark's DXBC) | SM5.1 DXBC; compute pipelines under real root signatures (`--pipelines`), every shader (`C10-shaders`) | 310 (shaders also 320) |
  | C11 | `$S/corpus/C11` (G7, 3DMark's DXIL) | DXIL; compute pipelines (`--pipelines`), every shader (`C11-shaders`) | 310 |

  C8, C10 and C11 runs print hashes only (no `--dis`, no `--out`); a changed shader is re-run alone with `--dis` to
  show it. Having no baseline modules, they have no `$S/untyped` list: their runs' untyped lines are ledgered, not
  compared.

  DXBC files run with G3's variant matrix (`<file>#<v>` lines, D3D11's chain values and the `#rs` root-signature
  variant).
- Seams that do not move: `airconv_public.h` and `dxil/dxil_public.h`'s existing declaration byte-identical (G1 appends
  `DXILGetEntry`, its three structs and the `DXIL_*` enumerators only), `AIRCONV_VERSION 26` (K20), `dxbc_root_signature.hpp` (until step 5c), `libs/DXBCParser` (until
  step 5b), `airconv_thunks.[ch]`, the VM wire (`slots.c`, `sm50-wire.h`, `sm50-args32`), slot numbers, both tables,
  `DXMT_PROBE` and `DXMT_DXIL_BOUNDS` names and meaning, the error contract (`*ppError = nullptr` on entry, 0/1,
  `DXIL:` prefixes and refusal texts as data), reflection bytes per stage, the binding model (CB table 29, argument
  table 30, `GetArgumentIndex`), concurrent compiles on one handle with no lock.
- Translator key: read with R's form (`sh -c 'ROOT=$PWD; . dxmt/pins; . wine-arm64/lib.sh; PATH="$(sh
  dxmt/toolchain.sh):$PATH"; translator_key build/wine-arm64-src/dxmt release'`); before Z it is R's value (R-D18), and
  no lane touches `$D`. Z2 adds the Rust proxy to that form.
- No profile cfg, feature or flag reaches `airconv`/`airconv-ir` from the p profile (critic 14): std and p share one
  key and one output.
- zsh: quote `"====="`; gating scans use `/usr/bin/grep`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes, keeps the ledger and the program ledger's
notes (the `<M>` switch windows, Z's pin freeze, the `llvm-nolto` input for steps 3 and 5). One **pipeline owner**
runs every lock job (G4's run, G5a, G5b, Z1's builds, Z3). **Lanes** use host tools only (cargo, the tool built by
`$S/tool.sh`, corpus-diff, the audit) with their own `CARGO_TARGET_DIR` and `$S/bin/<lane>`.

**Lane procedure.** Branch from `step4a-translator` at the commit the task names; commit in the lane; hand to the
controller, who rebases onto `step4a-translator`, fast-forwards, re-runs `--update-derives` (P14) and ledgers. A
lane never edits another lane's files, the root manifest, CONVENTIONS, the audit script, `$S/base` or `$S/untyped`
(L4's Step 3 creates `$S/untyped` once; the controller freezes it). **Shared files are made once:** T1 creates every
module file the tasks below name (each an empty module with its `//!` line) and every `mod` line in `A/src/lib.rs`,
`A/src/{llvm,air,dxbc,dxil,entry}.rs` and `A/ir/src/lib.rs`, plus `A/src/entry/tests.rs`, so a later task's "Create"
fills T1's stub; L2 declares the whole C API list in `llvm/sys.rs` (L4's introspection set included), every
shim function with a stub body in `shim/shim.cpp`, and the builder's fast-math setter, so L3 fills only `shim.cpp`,
`module.rs` and `writer.rs`, and L4 alone edits `builder.rs` (its oracle hook); X2 touches `entry/geometry.rs` only
after D4 has landed. A declaration a later lane lacks goes through the controller, never into another lane's file.

| Task | Lane / branch | Model | Depends on | Lock |
|---|---|---|---|---|
| 0 kit, rebase, permissions | controller | — | step R landed | — |
| G1 C++ getter, barrier declaration | `g` (DXMTRS) | Sonnet | 0 | no |
| G2 tool identity outputs, corpus-diff | `mn` | Opus | G1's header (starts at 0) | no |
| G3 every DXBC stage in the tool, variants | `mn` | Sonnet | G2 | no |
| G4 DXBC corpus | `mn`, owner runs | Sonnet | 0 | run |
| G5a LLVM nolto | owner | Sonnet | 0 | clone copy |
| G5b C7, snapshots, baselines, coverage, lane kit | owner | Sonnet | G1-G4, G5a | yes |
| G6 `--pipelines`, C8's baseline | `mn` | Sonnet | G2, G5b | no |
| G7 3DMark extractor, C10's and C11's baselines | `mn` | Sonnet | G6 | no |
| T1 crates, build script, exports, mirrors | `l` | Opus | 0, G1b (branched from a head that holds it: `build.rs` compiles `transforms/`, which includes AIRBuilder before G1b); link proof after G5a | no |
| L2 bindings, newtypes, `Ty` | `l` | Opus | T1, G5a | no |
| L3 shim, module, helpers, passes, writer glue | `l` | Opus | L2, P4, G5b | no |
| L4 oracle | `o` | Opus | L2, G5b | no |
| P1 container, signatures, root signature | `p` | Sonnet | T1 (C8/C10 rows: G6, G7) | no |
| P2 decode, IR, CFG | `p` | Opus | P1 | no |
| P3 handlers as data, recorders, reflection, DXBC Initialize | `p` | Opus | P2, G5b | no |
| P4 MTLB, sha256 | `m` | Sonnet | T1 (check: G5b) | no |
| A1 AIR types, intrinsics | `a` | Opus | L2, G5b (barrier test: L3) | no |
| A2 signatures, argument buffers, metadata | `a` | Opus | A1, P3 | no |
| D1 Converter core, blocks, CS | `d` | Opus | A2, L3, L4 | no |
| D2 VS, PS, replay, bindings | `d` | Opus | D1 | no |
| D3 hull and domain | `ts` | Sonnet | D2 | no |
| D4 geometry | `gs` | Sonnet | D2 | no |
| X1 DXIL Initialize | `x` | Opus | L3, P3 | no |
| X2 DXIL lowering | `x` | Opus | X1, D2 (D4 for GS rows and `entry/geometry.rs`) | no |
| Z1 the switch (DXMTRS) | owner | Opus | all above | builds |
| Z2 MacNeutron's same-pin edits | `mn` | Sonnet | Z1 | no |
| Z3 gates, once | owner | Sonnet | Z1, Z2 | yes |
| Z4 one fix pass | owner, lanes | Opus | Z3 | as needed |
| Z5 spec, push, pin | controller | — | Z4 | yes |

**Before dependencies land.** G2-G4 start at once on `step4-mn` (MacNeutron files only, disjoint from R's RM files); G2
compiles against G1's header from G1's worktree. G5a starts at Task 0 (the clone copy is a short lock job; the build
is host-only), so T1's link proof and L2 never wait for the baselines. P1, P2 and P4 write code and host tests right
after T1; their tool checks wait for G5b. L4 runs on the C++ baseline's modules before any lowering exists (early
data for W). X1 needs no D task. Every C8 row waits for G6, every C10 or C11 row for G7 (expected long before D1).
Critical path: T1 → L2 → A1 → A2 → D1 → D2 → X2 → Z (A1's barrier test also waits for L3).

**Gate points.** One: Z3 (every gate once), then Z4 (one fix pass), then Z5. Lane runs (LR) and CK are each task's own
checks. Reviews on Opus.

## Rules check (`CLAUDE.md`, tree-wide since R)

- **Statics:** two, read once at load: `DXMT_PROBE` (`dxil_converter.cpp:137-140`) and `DXMT_DXIL_BOUNDS`
  (`dxil_lower.cpp:89-102`) as `LazyLock`s — process-wide environment, the C++'s function statics. Constant tables
  (`TextureInfo`, `QUERIES`, sha256's `k`) are `const`. Dropped: `llvm_overwrite` and its `cl::opt` poke (K7: a no-op
  under NDEBUG). LLVM's own globals are LLVM's.
- **Config keys:** none at run time (the two variables above exist). Build-time: `AIRCONV_LLVM` and `AIRCONV_XCRUN`
  (`build.rs`, meson `env:`, rust-check inputs; `AIRCONV_XCRUN` is xcrun or a `sh` script taking xcrun's arguments);
  cargo feature `oracle` (tests and lane tools only, never meson's). Test-only: `AIRCONV_STEP4_BASE` (the frozen
  baseline's folder for the baseline-reading tests of L3, P4, A1, A2; unset outside step 4's lanes, Global
  Constraints). `dxil-translate` options `--no-metal`, `--dis`, `--metal`, `--shader-flags`, `--initialize-only`,
  `--pipelines` (test tool, MacNeutron). Make target `llvm-nolto` (Z2; its install is RC's `AIRCONV_LLVM`).
- **Wire fields:** none. `#[repr(C)]` mirrors of `airconv_public.h`'s data structs (reflection with its unions,
  `MTL_SM50_SHADER_ARGUMENT`, the nine chain structs, `SM50_COMPILED_BITCODE`) and of `dxil_public.h`'s
  `DXIL_ENTRY_ELEMENT`, `DXIL_ENTRY_RESOURCE`, `DXIL_ENTRY_INFO` (G1), with asserts generated by
  `tools/winemetal-layouts.sh`; `SM50_IA_INPUT_ELEMENT`'s bitfields read as one LSB-first `u32` (critic 9).
- **Dependencies:** none new. `airconv`: `airconv-ir` (path), `rustc-hash` (in the workspace, for the four
  `unordered_map`s), build-dependency `cc` (in the workspace). `airconv-ir`: none. No `llvm-sys` (4-D2), no `sha2`
  (P4m). `winemetal` gains `airconv` at Z1. MacNeutron's new host tools (`prc-extract.py`, G7; the scratch
  root-signature dumper, G6) use only the standard library and the C++ header they read.
- **Derives:** none by default; each task adds only what a call site needs (`Clone` on the function-signature
  descriptors the emitters copy, `Copy` on `Ty`/`Val` handles and small operand enums, `PartialEq`/`Debug` where tests
  compare), listed by `tools/audit.sh --update-derives` (P14).
- **Lint suppressions:** none. One allow site with its CONVENTIONS exception: `src/airconv/ir/src/float_cast.rs` (the
  hull estimators' float/int conversions, critic 10), in `ALLOW_SITES`. FFI declarations use snake_case names with
  `#[link_name]`, so no `non_snake_case` allow. **No panic on guest values** (a panic in either crate aborts the game:
  the exports are `extern "C"`, and `production-unwind` stops at that boundary; CONVENTIONS' "no `unwrap`, `expect`,
  indexing or overflowing arithmetic on a guest's values", which clippy's pedantic and nursery groups do not enforce):
  each crate's `lib.rs` opens with `#![cfg_attr(not(test), deny(clippy::indexing_slicing, clippy::unwrap_used,
  clippy::expect_used, clippy::panic))]`. A deny is not a suppression, and `cfg_attr(not(test), …)` leaves unit tests
  free without an `allow` (clippy's `--all-targets` still checks the library itself); the crate-level attribute goes
  in `lib.rs` because cargo rejects a `[lints]` table that both inherits from the workspace and names lints.
  `std::map::operator[]` (it inserts a default; 71 `Map[` sites in `A/dxbc_instructions.cpp` alone, e.g. `:624`'s
  `shader_info.srvMap[…].sampled = true`) ports as `entry(k).or_default()`, never `map[&k]` (panics) or `.get()` (skips
  the insert, which reaches the binding table and the reflection masks).
- **Duplicates:** `airconv-ir`'s DXBC parser and root-signature deserializer against `libs/DXBCParser` and
  `dxbc_root_signature.hpp` (until step 5's PE users switch, 5-D6); the shim's C++ passes (until 4b or step 6);
  `sm50_wow64.c` against the Rust exports it calls (step 6); `airconv_public.h` hand-kept against the Rust mirrors
  (asserted); `winemetal-wire`'s `AIRCONV_VERSION` mirror (exists); G1's `DXIL_*` enumerators against
  `dxil_types.hpp` and `D3D1x_SB_*` (static_asserted, until Z1 deletes both C++ sides); the barrier declaration in G1b's
  pass against `AIRBuilder::CreateBarrier` (until Z1 deletes AIRBuilder); `prc-extract.py`'s reader against
  `src/d3d12/d3d12_pipeline_record.cpp` (a test tool, read-only); the four fixtures in `A/ir/tests/data/` against
  MacNeutron's test shaders they were copied from (sha256 in the ledger).

## Review Focus

1. **Malformed or hostile bytecode** (a DXBC length token past its blob, a container `ReadDXBC` refuses, a DXIL part
   `FindDXIL` refuses, damaged bitcode inside a well-formed part, a name with an interior NUL, a guest name that is not
   UTF-8). Expected: an error object and status 1, never an abort, a panic or `exit(1)`. Tests: P1's
   `container_rejects_*`, P2's `token_walk_stops_at_blob_end`, X1's C7 rows (reasons equal to the C++'s; the bitcode
   rows reach the reader and start `DXIL: can't read the bitcode:`), P3's `interior_nul_name_is_an_error`, X2's
   `static_global_name_keeps_its_bytes`, the crate-level `deny` (Rules check).
2. **Concurrent compiles of one shader handle** (D3D11's workers compile two variants of one shader at once). Expected:
   every output byte-equal to a single-threaded compile, no data race. Test: D2's
   `compile_on_eight_threads_matches_one` (and X2's for DXIL); the handle type is `Sync` by construction.
3. **Uninitialised caller memory** (D3D11's `MTL_SHADER_REFLECTION`, `sm50_error_t`). Expected: only the stage's fields
   written, nothing read. Tests: `dxil-translate` fills the reflection with 0xA5 before `SM50Initialize`, so `refl=`
   catches any extra write (every LR); g10's written-byte ranges at Z3.
4. **Deep shaders on small stacks** (native and VM-host threads have 512 KiB). Expected: no overflow. Test: every LR
   runs on the 256 KiB thread (P4); Z3's native gate.
5. **Emission order across processes** (an `FxHashMap` iterated into IR, value-name order). Expected: identical bytes in
   two processes. Tests: `dis=` (names pin creation order) on every LR; Z3's two-process `cmp -r` of `--out`.

---

### Task 0: kit, rebase, permissions (controller)

- [ ] **Step 1: Rebase this plan** onto step R's landed head: re-check every path and line cited (Global Constraints,
  Rules check, tasks' Files), ledger each correction; commit the plan on `dxmtrs` (`Plan: DXMTRS step 4a, the
  translator in Rust on LLVM 15`).
- [ ] **Step 2: Branches.** `git -C $D branch step4a-translator <R's main sha>` (a branch, no checkout: `$D` stays
  detached); `git -C <M> worktree add $S/wt-mn -b step4-mn dxmtrs`. Ledger the key (R's value). Start G5a.
- [ ] **Step 3: Permissions.** Ledger 4-D7 as answered (2026-10-30: SMITE 2's capture and 3DMark's recording "Yes,
  both", read in place, read-only, outputs only under `$S`; a D3D11 capture "Skip it for now") with C8's path from
  `7-decisions.md` and the 3DMark recording's path (`~/Library/Application Support/MacNeutron/…/*.pipelines`, located
  read-only by name); never asked again. 4-D9 is asked when Z and 4b are ready.

### Task G1: the getter and the barrier declaration (DXMTRS C++, lane `g`)

**Files** (as landed, `step4a-translator` 1d0e42e and 1b44a84): create `A/dxil/dxil_entry.cpp` (the getter, in
`A/meson.build`'s `airconv_src`); modify `A/dxil/dxil_public.h` (declarations only appended), `A/meson.build`,
`A/dxbc_converter.hpp` (the handle's `entry_view`), `A/dxbc_converter.cpp` (`SM50Initialize` builds it, DXIL and
DXBC), `A/dxil/dxil.hpp` (`BuildEntryView`), `A/dxil/dxil_types.hpp` (`EntryView` declared),
`A/transforms/simdgroup_implicit_membarrier.{cpp,hpp}` (`declareSimdgroupBarrier`). The controller's clip-distance fix
(0439fa9, `A/dxbc_converter.cpp`: no clip insert into a void vertex function) rides on this lane before G5b.

**Interfaces:**
- Produces (C, `dxil_public.h`), pointers owned by the shader handle until `SM50Destroy`:
  `typedef struct { uint32_t kind; uint32_t rows; int32_t start_row; const char *name; uint32_t
  num_semantic_indices; const uint32_t *semantic_indices; } DXIL_ENTRY_ELEMENT;` (`kind` = a `DXIL_SEMANTIC_*`
  value; `num_semantic_indices` because the tool's `UserName` reads the vector's size, `dxil_types.hpp:151-156`),
  `typedef struct { uint32_t cls, space, lower_bound, size, heap; } DXIL_ENTRY_RESOURCE;` (`cls` = a
  `DXIL_RESOURCE_CLASS_*` value), `typedef struct { uint32_t is_dxil; uint32_t stage; uint32_t num_inputs,
  num_outputs, num_resources; const DXIL_ENTRY_ELEMENT *inputs, *outputs; const DXIL_ENTRY_RESOURCE *resources; }
  DXIL_ENTRY_INFO;` and `AIRCONV_API void DXILGetEntry(sm50_shader_t shader, DXIL_ENTRY_INFO *out);` (`stage` = a
  `DXIL_SHADER_KIND_*` value: DXIL's `ShaderKind` and the DXBC program type `D3D1x_SB_*` share the values 0-5). For
  DXBC, `inputs` and `outputs` come from the container's ISGN and OSGN (semantic name, index, register as
  `start_row`, the system value mapped to the `DXIL_SEMANTIC_*` of the same meaning, `ARBITRARY` for a user varying;
  G3's stream-output variant needs a VS's output registers) and
  `num_resources` is 0. Plus C enumerators the tool tests, appended to `dxil_public.h`: `DXIL_SHADER_KIND_{PIXEL,
  VERTEX,GEOMETRY,HULL,DOMAIN,COMPUTE}` (0-5), `DXIL_SEMANTIC_{ARBITRARY,POSITION,DEPTH,DEPTH_LESS_EQUAL,
  DEPTH_GREATER_EQUAL,STENCIL_REF}` (0, 3, 17, 18, 19, 20), `DXIL_RESOURCE_CLASS_{SRV,UAV,CBUFFER,SAMPLER}` (0-3);
  the getter's `.cpp` `static_assert`s each against `dxil_types.hpp`'s enums and
  `libs/DXBCParser/d3d12tokenizedprogramformat.hpp:62-69`. These are exactly the fields and values
  `dxil-translate.mm:57-79,158-170,204-326,374-385` reads, so the tool needs no other airconv header (a tool that kept
  `dxil_types.hpp` would link today and fail at Z1, which deletes it, and again at 5b).

- [ ] **Step 1 (G1a): the getter.** Implement over `SM50ShaderInternal` (`dxil->entry`, or the DXBC signatures); the
  element arrays are built at Initialize into the handle (or lazily, never by a static). Check: the C++ compiles in
  G5b's job; G2's tool links it. Commit `airconv: DXILGetEntry, the test tools' view of a shader (test use)`.
- [ ] **Step 2 (G1b): the barrier pass** (critic 2): `SimdgroupImplicitMemBarrierPass` declares `air.simdgroup.barrier`
  through one function, `declareSimdgroupBarrier(Module &)` (L3's shim exports it as
  `airconv_shim_declare_simdgroup_barrier` for A1's test), with `getOrInsertFunction` and the same three attributes
  `AIRBuilder::CreateBarrier` gives it (`A/nt/air_builder.cpp:1191-1212`); its `#include "../nt/air_builder.hpp"`
  goes, so both passes compile alone in the shim (4-D3). Check: G5b's job runs the C++ tool at G1a and at G1b on C1-C7
  → manifests equal, and the pass fires on G4's barrier kernel (G5b Step 5). Commit `airconv: the barrier pass declares
  its intrinsic itself`.

### Task G2: the tool's identity outputs and corpus-diff (MacNeutron, lane `mn`)

**Files:** Modify `dxmt/tools/dxil-translate.mm`, `dxmt/tools/corpus-diff.py`.

**Interfaces:**
- Consumes: `DXILGetEntry` and the `DXIL_*` enumerators (G1).
- Produces: option `--no-metal` (translation only: no device, no pipelines; `ok` means every compile returned 0),
  under which the lines are `ok <file> <stage> <ms> air=<fnv64> dis=<sha256> refl=<sha256> tags=<sha256>` and `fail
  <file> <stage>|- refl=<sha256>|refl=- <reason>` (`-` when Initialize failed); option `--initialize-only` (every
  file through `SM50Initialize` alone, then `SM50Destroy`; one line per file, grouped DXBC files each on their own:
  `init <file> <stage> refl=<sha256>` or `fail <file> - refl=- <reason>`), for the tasks that port Initialize before
  any compile exists (P3, X1); option `--shader-flags <n>` (sets `SM50_SHADER_COMMON_DATA.flags`, default 0: D3D11's
  `sampleNaNToZero` is 1, `defuseFma` 2, `src/util/config/config.cpp:55-61` ships both); without `--no-metal` and
  `--initialize-only` every line is exactly today's (`check.sh:1036-1045` greps `^fail <file> <reason>$`); `summary …`
  unchanged; `--dis <dir>` (each metallib's text as `<file>[#<v>][.<n>].ll`), `--metal 310|320` (default 310).
  `corpus-diff.py <before> <after> [--stages vs,ps,cs,hs,ds,gs]` (no `--fields`: what it compares is fixed),
  printing `corpus-diff: <ok>/<total> ok, <n> new failures, <n> fixed, <n> changed, <n> extra, <n> unchanged`, exit 1
  on any of: a status change either way (`ok`/`init` → `fail` is a new failure, `fail` → `ok` is fixed: an identity
  port that accepts a shader the C++ refuses is wrong), a `dis=`, `refl=` or `tags=` difference, a reason difference
  between two `fail` lines, a shader missing from `<after>`, an extra one in it, zero shaders compared (after
  `--stages`), a missing summary, an `ok` line without `air=` (as today; `init` lines carry none); `air=` differences
  printed as `air-only <file>`, never failing. `--stages` keeps a
  shader by the stage on its `<before>` line; a `-` stage (Initialize failed) passes every filter.

- [ ] **Step 1: RED** for corpus-diff, as scratch files, each an exit code and a named file: two outputs differing
  only in `dis=` → 1; only in a fail reason → 1; `ok` → `fail` → 1 (new failure); `fail` → `ok` → 1 (fixed); a shader
  only in `<after>` → 1 (extra); `--stages cs` on a file with no CS → 1 (zero compared); `--stages vs` with a `ps`
  difference → 0; two `init` files differing in `refl=` → 1; only in `air=` → 0 with `air-only`.
- [ ] **Step 2: Implement** in the tool: the getter and the `DXIL_*` enumerators replace both casts, the
  `dxbc_converter.hpp` include and every `dxmt::dxil` type; the tool keeps its own `UserName` and needs only
  `airconv_public.h` and `dxil_public.h` (Step 3 checks it against a folder holding just those); `dis=` =
  sha256 over each translated metallib's `Module::print` text (bitcode from the wrapper magic, LLVM 15 typed reader as
  `CountFlags` does, empty module identifier so no `ModuleID`) in translation order; `refl=` = sha256 of the 72
  reflection bytes, pre-filled 0xA5, plus `SM50GetArgumentsInfo`'s two arrays sized by the counts; `tags=` = sha256 of
  the parsed MTLB function list (each function's NAME, TYPE, VERS, TESS, VATT, VATY tags in order; HASH, OFFT, MDSZ and
  sizes left out), a ~40-line reader (critic 21); every translation on one 256 KiB pthread (P4); `--metal` sets
  `SM50_SHADER_COMMON_DATA.metal_version`.
- [ ] **Step 3: Checks** (after G5b builds it): the RED cases pass; the tool compiles against a folder holding only
  `airconv_public.h` and `dxil/dxil_public.h` (`clang++ -fsyntax-only -std=c++17 -I$S/llvm15/include -I<that folder>`); `check.sh:1028-1102`'s commands run verbatim
  against the G1b-linked C++ tool (`TOOLS` = its folder) → every `expect` row passes; `--no-metal --dis` on C1 gives
  the same `.ll` texts as a `--dis` run with Metal.
  Commit `dxil-translate: dis=, refl=, tags=, translation only, Initialize only; corpus-diff compares them (DXMTRS
  step 4)`.

### Task G3: every DXBC stage in the tool (MacNeutron, lane `mn`)

**Files:** Modify `dxmt/tools/dxil-translate.mm`.

**Interfaces:** Produces: DXBC VS, PS, CS translated standalone with D3D11's chains (`src/d3d11/d3d11_shader.cpp`:
vertex `:119-163`, pixel `:165-201`, compute `:203-224`); `<name>.{vs,hs,ds}.dxbc` as hull (`:226-272`) and domain
(`:274-311`) pipelines and `<name>.{vs,gs}.dxbc` as geometry pipelines (`:363-443`), grouped by `<name>` and printed on
the HS or GS file's line. Pixel formats RGBA8Unorm, as the DXIL path does. Under `--no-metal` each DXBC file or group
also runs a **fixed variant matrix**, one line `ok|fail <file>#<v> …` per variant, the same names in base and lanes
(D3D11 varies these chain values; one fixed chain would let an inverted mask test or a dropped store pass every LR):
- VS: `#nolayout` (no IA layout: the `stage_in` path, the only one that emits VATT/VATY), `#f4` (every input
  Float4), `#fmt<k>` (input *i* takes format *(i + k·n) mod F* of the IA formats airconv converts, *k* until each
  format has appeared once), `#so` (stream output, `:313-361`: every output register's four components from
  `DXILGetEntry`'s DXBC outputs, buffer 0; today's `<file>.1.metallib`), `#gspt` (`SM50_SHADER_GS_PASS_THROUGH` with
  an RTAI output and `RasterizationDisabled` 1);
- PS: `#d` (`sample_mask` 0xffffffff, `dual_source_blending` 0, `disable_depth_output` 0, `unorm_output_reg_mask` 0)
  and `#v` (0x1, 1, 1, 0xff);
- HS pipelines: `#i<f>t<n>` for `index_buffer_format` 0, 1, 2 × `max_potential_tess_factor` 64 and 8;
- GS pipelines: `#s0`, `#s1` (`strip_topology`);
- every file and group: `#rs`, compiled as D3D12 compiles DXBC (`d3d12_shader_cache.cpp:62-90`, any DXBC with a
  `SM50_SHADER_ROOT_SIGNATURE` chain; `setup_binding_rootsig`'s eight DXBC call sites, `A/dxbc_converter.cpp:389,513,
  704`, `A/dxbc_converter_ts.cpp:249,257,778`, `A/dxbc_converter_gs.cpp`'s two): a synthetic RTS0 built as
  `RootSignature()` builds it (`dxil-translate.mm:57-79`), its ranges one per set bit of the reflection's slot masks
  (`ConstantBufferSlotMask`, `SamplerSlotMask`, `UAVSlotMask`, `SRVSlotMask{Lo,Hi}`), space 0; a hull or geometry
  pipeline's vertex part also gets it as `SM50_SHADER_ROOT_SIGNATURE2`.
A variant the C++ refuses is a line like any other: its reason is compared.

- [ ] **Step 1: Implement**; a DXBC file whose group is incomplete fails with `no <stage> for <name>`.
- [ ] **Step 2: Checks** (after G5b): the C++ tool on C5 at 310 and 320 → every file `ok` or a ledgered C++ refusal;
  `patch.hs.dxbc`'s line carries two metallibs; every variant name above appears on C5's output; without
  `--no-metal`, C4's lines are today's. Commit `dxil-translate: every DXBC stage, D3D11's argument chains varied, and
  D3D12's root-signature chain`.

### Task G4: the DXBC corpus (MacNeutron, lane `mn`; the owner runs dxbc.exe)

**Files:** Modify `dxmt/tests/dxbc.cpp` (a `--file <hlsl> <entry> <profile> <out>` mode); create
`dxmt/tests/dxbc-corpus/sources.txt` (rows `<hlsl> <entry> <profile> <out>`, the HLSL path relative to the DXMTRS
tree, which the run job resolves against `$D`: `tests/dx11/*.hlsl`'s `vs_main` and `ps_main`, `shader_hdr.hlsl`'s
`vs_main`, `ps_main_linear` and `ps_main_pq`; the rows of the files below relative to `dxmt/tests/dxbc-corpus`), the
compiled `dxmt/tests/dxbc-corpus/*.dxbc`, and HLSL in `dxmt/tests/dxbc-corpus/` for what C5 and `tests/dx11` leave
unexercised (C5 has one tri/integer HS/DS pair and no GS; 1,056 lines of `A/dxbc_converter_ts.cpp` and 892 of
`A/dxbc_converter_gs.cpp` would otherwise rest on it):
- `gs.hlsl`: a point-to-quad `gs_5_0` with `[maxvertexcount(4)]` and `SV_PrimitiveID`, and its `vs_5_0`
  (`gs.{vs,gs}.dxbc`); `gspt.hlsl`: a pass-through GS writing `SV_RenderTargetArrayIndex` (the pass-through
  detection's true branch); `gsadj.hlsl`: a `lineadj` GS with `RestartStrip`;
- `tess.hlsl`: `quad` + `fractional_odd` and `isoline` + `pow2` HS/DS pipelines with their VS (`tessq.{vs,hs,ds}`,
  `tessl.{vs,hs,ds}`);
- `sysval.hlsl`: a VS writing `SV_ClipDistance`, a PS writing `SV_Coverage` and `SV_DepthGreaterEqual`;
- `barrier.hlsl`: a `cs_5_0` with `groupshared uint Shared[64]; … Shared[gi] = x; y = Shared[gi ^ 1];` and no barrier
  between, so the barrier pass fires (no C1 metallib calls `air.simdgroup.barrier`: 0 of `build/phase0/air-15`'s 96;
  `sync.hlsl` and `groupshared.hlsl` put a barrier before their loads); it also gives C6 its CS.

- [ ] **Step 1: Implement** the mode; `make dxmt-tests` builds `dxbc.exe` as today.
- [ ] **Step 2: Run** (owner, one lock job): `dxbc.exe --file` per row under the macOS runtime's Wine as the header of
  `dxbc.cpp` says (as `eac-vm/dxmt/ref.sh` runs it). Expected: one `dxbc <name> <bytes>` per row, about 34 files; a
  row Wine's D3DCompile refuses is dropped and ledgered with its message. Ledger the caveat: Wine's D3DCompile output
  is not fxc's.
- [ ] **Step 3: Commit** `DXMT tests: a DXBC corpus from DXMT's samples and the stages they lack (step 4)`.

### Task G5a: LLVM without LTO (owner, from Task 0)

**Files (scratch, uncommitted):** `$S/llvm-project`, `$S/llvm15`.

- [ ] **Step 1: The clone** (pipeline lock job): `[ -f $S/llvm15/.complete ] || [ -d $S/llvm-project/llvm ] || cp
  -cR $B/dxmt-src/llvm-project $S/llvm-project`. Then, host-only, `/usr/bin/lockf -k $S/llvm15.lock sh -c 'ROOT=<M>;
  die(){ echo "$*" >&2; exit 1; }; . <M>/dxmt/pins; . <M>/dxmt/llvm.sh; [ "$(cat $S/llvm15/.complete 2>/dev/null)" =
  "llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto" ] || UNIX_CPU=-mcpu=apple-m5 UNIX_LTO= build_llvm arm64 $S/llvm15
  $S/llvm-project'` (P2; the same call 4W's Task 0 Step 3 runs under the same lock, which built it on 2026-10-30).
  Expected: `$S/llvm15/.complete` = `llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto`; `grep -c 'MacNeutron: FMFSource'
  $S/llvm-project/llvm/include/llvm/IR/IRBuilder.h` = 1. T1's link proof and L2 start then.

### Task G5b: C7, the snapshots, the baselines, coverage, the lane kit (owner)

**Files (scratch, uncommitted):** `$S/tool.sh`, `$S/c7.py`, `$S/corpus/**`, `$S/base-build/**`, `$S/base/**`,
`$S/bin/{base-<sha>,pin}/`, `$S/cov-build`, `$S/cov`.

**Interfaces:**
- Produces: `$S/tool.sh cxx <dxmtrs-tree> <archive-dir> <llvm> <out-dir> [<extra link flags>]` (links `$S/wt-mn`'s
  `dxil-translate.mm` as `build_translate` does, with the two archives from `<archive-dir>`) and `$S/tool.sh rust
  <dxmtrs-tree> <out-dir> [<llvm>]` (`cargo rustc -p airconv --release --features oracle --crate-type staticlib
  --target aarch64-apple-darwin` with `AIRCONV_LLVM`/`AIRCONV_XCRUN`, then the same clang++ line without
  `libairconv.a`/`libDXBCParserNative.a`); `$S/corpus/C1`-`C7` (snapshots: lanes and Z3 read only these); from the C++
  at G1b on `$S/llvm15`, for each corpus `$S/base/<c>.txt`, `<c>.dis/`, `<c>.out/` (metallibs) and `<c>-init.txt`
  (`--initialize-only`), with `-320` and `-320f` (`--shader-flags 3`) runs for C4, C5 and C6;
  `$S/base/inputs.sha256` (every `$S/corpus/C1`-`C7` file, `$S/bin/base-<G1b>/dxil-translate`,
  `$S/llvm15/.complete`); `$S/bin/base-<G1b>/dxil-translate` kept as the baseline tool.

- [ ] **Step 1: Snapshots** (host-only): `cp -cR` of each corpus source in the table into `$S/corpus/<c>` (MacNeutron's
  `dxmtrs` and `step4-mn` shas ledgered), so a later edit to a test shader in `<M>` never changes a lane's input
  against a frozen output.
- [ ] **Step 2: C7** (host-only): `python3 -I $S/c7.py` (~40 lines) writes `$S/corpus/C7/<src>-<row>.dxil`, each row
  a well-formed edit of C1's `cache.{vs,ps,cs}.dxil`:
  - **reader rows**, from all three files, the file length and every container, part and program-header size
    unchanged, so `ReadDXBC` and `FindDXIL` pass and `parseBitcodeFile` runs: `DxilBitcodeHeader.BitcodeSize` (DXIL
    part offset 20) lowered by 4, to half and to 8; the bitcode's last quarter zero-filled at full size. A copy cut
    short with its sizes kept would never get there: `ReadDXBC` refuses any container whose `ContainerSizeInBytes`
    is not the size passed (`BlobContainer.cpp:32-34`), and the C++ answers `Invalid DXBC bytecode`
    (`A/dxbc_converter.cpp:1000-1003`);
  - **`FindDXIL` rows**, from `cache.cs.dxil` (`A/dxil/dxil_container.cpp:37-44`): `BitcodeSize` past the part; a DXIL
    part of 16 bytes, the part and container sizes made to match; the `DXIL` magic changed;
  - **`ReadDXBC` rows**, from `cache.cs.dxil`, one per reachable refusal of `BlobContainer.cpp:27-85`: shorter than the
    header; `ContainerSizeInBytes` not the file length; a bad fourcc; a bad version; a blob count whose index runs past
    the end; a blob offset whose header runs past the end; a blob size past the end; two blobs not back to back.
- [ ] **Step 3: The baseline job** (one lock job): for G1a then G1b, `git -C $D checkout -q --detach <sha>` and
  `ninja -C $SRC/dxmt-build src/airconv/darwin/libairconv.a libs/DXBCParser/libDXBCParserNative.a` (the configured
  product build, so the shipped flags; PATH as R's "Meson builds outside MacNeutron's scripts"), the two archives
  copied to `$S/base-build/<sha>/`; then restore `$D` to `dxmt.applied`'s commit and ledger the shas; and `cp
  build/wine-arm64/dxil-translate $S/bin/pin/` (today's installed tool, R's pin: its cdhash and the pin's DXMTRS sha
  ledgered). Outside the lock: `$S/tool.sh cxx $S/wt-g $S/base-build/<sha> $S/llvm15 $S/bin/base-<sha>` per sha (G1a
  and G1b differ in a `.cpp` only, so `$S/wt-g`'s headers serve both), and at G1a and G1b also against
  `$SRC/llvm-arm64` (ThinLTO, read only). With the G1b nolto tool: C1, C2, C3, C7 at 310; C4, C5, C6 at 310, 320 and
  320 with `--shader-flags 3`; every corpus once more with `--initialize-only`; a second run of all of it in a new
  process. With the G1a nolto and G1b ThinLTO tools: the 310 and 320 runs. Write `inputs.sha256`.
- [ ] **Step 4: Coverage** (host-only, information): the C++ at G1b built again in `$S/cov-build` from `$S/wt-g` by R's
  out-of-tree meson form with the native C++ args `-fprofile-instr-generate -fcoverage-mapping` and
  `native_llvm_path=$S/llvm15` (the two archives only), linked by `$S/tool.sh cxx … -fprofile-instr-generate`; C1-C7
  run with `LLVM_PROFILE_FILE=$S/cov/%p.profraw` (DXBC at 310 and 320, with the variants); `xcrun llvm-profdata merge`
  and `xcrun llvm-cov report -show-functions` over `src/airconv`. Ledger each file's line coverage and the functions
  no corpus reaches, named transliteration-only: no LR can fail on them, so their review is by reading.
- [ ] **Step 5: Checks.**
  - G1a and G1b: manifests equal (`dis=` per file); `/usr/bin/grep -l 'call void @air.simdgroup.barrier'
    $S/base/C6*.dis/*.ll` names at least one shader (G4's barrier kernel; else G4 adds a kernel that fires the pass and
    this step re-runs), and that shader's `.ll` at G1a (`AIRBuilder::CreateBarrier`) equals G1b's (the local
    declaration), so the manifest check is not vacuous;
  - G1a against R's pin: `$S/bin/pin/dxil-translate` and the G1a ThinLTO tool, both with Metal, on C1, C2 and C4 →
    the `air=` of every file both print equal (information only if the pin's DXMTRS sha is not `step4a-translator`'s
    base);
  - nolto and ThinLTO equal; the two G1b processes equal and their `--out` folders `cmp -r` equal; no 256 KiB overflow
    (else P4's ruling);
  - C1 64/64, C2 33/33, C3 0/7 with check.sh's reasons; C7: every reader row's reason starts `DXIL: can't read the
    bitcode:`, the `FindDXIL` rows give `DXIL: bitcode lies outside the DXIL part`, `DXIL: program header truncated` and
    `DXIL: bad program header`, every `ReadDXBC` row `Invalid DXBC bytecode`; the expected reason per file goes into the
    ledger; a row on which the C++ crashes is dropped and ledgered;
  - each DXBC shader's 320 `dis=` differs from its 310 one (`air.version` 2.7 and the `macosx15` triple,
    `A/dxbc_converter.cpp:211-221`; `sync.dxbc`'s 3.2 barrier branch, `A/nt/dxbc_converter_base.cpp:2967-2975`);
  - the file list: `C1 C2 C3 C7`, `C4 C5 C6` × {310, `-320`, `-320f`} `.txt` (16) and seven `-init.txt`, each with its
    `.dis/` and `.out/` except the init runs.
  Ledger the per-corpus summaries and the translate-only mean of each. Freeze `$S/base` (`chmod -R a-w`); G6 and G7
  add their files under `chmod u+w $S/base` and freeze again, nothing else writes there.

### Task G6: `--pipelines` and C8's baseline (MacNeutron, lane `mn`)

**Files:** Modify `dxmt/tools/dxil-translate.mm`; scratch `$S/rsdump` (the root-signature dumper P1 compares with:
~60 lines of C++ over `A/dxbc_root_signature.hpp`, built with `clang++ -I$S/wt-g/include -I$S/wt-g/src/airconv`,
printing a canonical field list of `v1_0` and `v1_1` per blob), `$S/base/C8.txt`, `$S/base/C8-init.txt`,
`$S/base/C8.sha256`, `$S/base/rsdump/` (`list.txt`: `<blob path> <dump file>` per line, and the dumps).

**Interfaces:** Produces `--pipelines <pipelines.txt>`: each line as `src/d3d12/d3d12_dxil_dump.cpp`'s writers emit it
(`d3d12_pipeline_compute.cpp:46` `cs cs=<h> rs=<h>`; `d3d12_pipeline_graphics.cpp:617-633` `gfx vs= ps= gs= rs= rtv=
dsv= topology= samples= blend0= mask0=`), shaders read as `<stage>-<h>.dxil` and root signatures as `rs-<h>.bin` from
the file's folder; the root signature chain from the real blob; `pixel_formats` from `rtv=` through the DXGI→Metal
mapping D3D12 uses when it fills `SM50_SHADER_PSO_PIXEL_SHADER_DATA` (an unmapped format fails the line by name); IA
layout stays synthetic. One output line per pipeline line, named `<line number>`.

- [ ] **Step 1: Implement**; the grammar from the writers, never from the capture.
- [ ] **Step 2: C8's baseline** (host-only): `$S/tool.sh cxx` relinks the baseline tool with G6's
  `dxil-translate.mm` against G5b's G1b archives and `$S/llvm15` (`$S/bin/base-<G1b>-g6`); `shasum -a 256` of every C8
  file, read in place, into `$S/base/C8.sha256`; `--pipelines <C8>/pipelines.txt --no-metal` (hashes only: no `--dis`
  and no `--out` for 10,271 shaders) into `$S/base/C8.txt`, and `--initialize-only` over the folder into
  `C8-init.txt`; `$S/rsdump` over C8's 150 `rs-*.bin` into `$S/base/rsdump/`. Checks: a second run in a new process
  equal; C1's lines from the relinked tool equal G5b's (G6 added an option, nothing else); 150 dumps, every one of
  version 1.0 or 1.1; ledger the summary. `chmod a-w` what this step wrote. Commit `dxil-translate: --pipelines, a
  capture's root signatures and formats`.

### Task G7: 3DMark's recording as corpora C10 and C11 (MacNeutron, lane `mn`)

**Files:** Create `dxmt/tools/prc-extract.py`; scratch `$S/corpus/C10/**`, `$S/corpus/C11/**`, `$S/base/C10*`,
`$S/base/C11*`.

**Interfaces:** Produces `python3 -I dxmt/tools/prc-extract.py <recording.pipelines> <out-dir>` (~40 lines): reads
DXMTPRC1 as `src/d3d12/d3d12_pipeline_record.{hpp,cpp}` writes it (the 8-byte magic, then records of a 36-byte header:
`uint32` kind, `uint32` payload size, `uint64` FNV-1a of the payload, 20-byte SHA-1 id; kind 1 = a blob, 2 a graphics
pipeline whose payload starts with the root signature's, VS's, PS's and GS's ids, 3 a compute pipeline whose payload
starts with the root signature's and CS's ids, `d3d12_pipeline_record.cpp:120-123,180-182`); skips a record cut short,
of an unknown kind or with a wrong checksum, counting them; writes each blob a pipeline names once, into
`<out-dir>/dxbc/` or `<out-dir>/dxil/` by its shader's kind (a container with a `DXIL` part is DXIL; a root signature
goes beside each shader that uses it), as `<role>-<fnv1a64>.dxil` (shader) or `rs-<fnv1a64>.bin` (root signature),
`%016llx` as `d3d12_dxil_dump.cpp:115,123` names them by `CaptureHash`; writes each folder's `pipelines.txt` with one
`cs cs=<h> rs=<h>` line per compute record (graphics pipelines' shaders are translated one by one: their `gfx` lines
would need the descriptor's formats, which this reader does not parse); prints `prc-extract: <blobs> blobs, <compute>
compute, <graphics> graphics, <bad> skipped`. `<out-dir>/dxbc` is C10, `<out-dir>/dxil` is C11.

- [ ] **Step 1: RED**: `prc-extract.py --selftest` builds a scratch recording from C1's `cache.cs.dxil` and the
  synthetic RTS0 bytes (two blobs, one compute record, one record with a wrong checksum) and asserts the two files in
  `dxil/`, one `cs` line and `1 skipped`; it fails before Step 2.
- [ ] **Step 2: Implement; run** (host-only, read-only on the recording): into `$S/corpus/3dmark`, moved to
  `$S/corpus/C10` and `C11`; `shasum -a 256` of the recording and of both folders into `$S/base/C10.sha256`. With the
  G6 tool (`$S/bin/base-<G1b>-g6`), for C10 and C11 each: `--pipelines $S/corpus/<c>/pipelines.txt --no-metal`
  (`<c>`), the folder `--no-metal` (`<c>-shaders`; C10's also `--metal 320`, `<c>-shaders-320`) and
  `--initialize-only` (`<c>-init`), into `$S/base/` (hashes only, as C8); a second run in a new process equal;
  `$S/rsdump` over their `rs-*.bin`, appended to `$S/base/rsdump/`; ledger the counts of DXBC and DXIL shaders and of
  DXBC CS under a real root signature. `chmod a-w` what this step wrote.
  Commit `DXMT tools: prc-extract.py, a pipeline recording's shaders and compute pipelines (DXMTRS step 4)`.

### Task T1: the crates, the build script, the exports, the mirrors (lane `l`)

**Files:** Create `A/Cargo.toml` (package `airconv`, lib, feature `oracle`, `[lints] workspace = true`, licence
`LGPL-2.1-or-later`), `A/build.rs`, `A/src/lib.rs`, `A/src/api.rs`, `A/src/abi.rs`, `A/src/abi/layouts.rs`
(generated), `A/src/shader.rs`, `A/src/entry.rs` and `A/src/entry/{initialize,compile,tessellation,geometry}.rs` (lane
stubs) and `A/src/entry/tests.rs`, every other module file the tasks below create, as an empty stub with its `//!` line
and its `mod` line (Execution, "Shared files are made once"), `A/shim/shim.cpp` (one probe function), `A/ir/Cargo.toml`
(package `airconv-ir`, licence `LGPL-2.1-or-later AND MIT`), `A/ir/src/lib.rs`, `A/ir/src/float_cast.rs`,
`A/ir/tests/data/{vsia.vs.dxbc,sync.dxbc,render.ps.dxbc,cache.cs.dxil}` (copied from MacNeutron's
`dxmt/tests/{shaders/d3d11,dxbc,shaders}` at `dxmtrs`'s head, sha256 ledgered: committed tests read these, never a
MacNeutron path, so the tree-wide audit and Z5's privacy scan see no `/Users/` path and the tests run wherever the tree
is); modify root `Cargo.toml` (members `src/airconv`, `src/airconv/ir`; `airconv-ir` in `default-members`, `airconv`
not; both in `[workspace.dependencies]`), `Cargo.lock`, `tools/winemetal-layouts.sh` (emits `A/src/abi/layouts.rs`,
`dxil_public.h`'s `DXIL_ENTRY_*` structs included), `tools/rust-check.sh` (inputs `AIRCONV_LLVM`, `AIRCONV_XCRUN` as
`${X:?…}`; both crates in `HOST_P` (the host clippy and doc lines) and in `leg_test`'s host `tst` line; `airconv-ir`
in the four msvc clippy rows, `PE_P`'s i686 and `PE64_P`'s x86_64, aarch64 and arm64ec), `docs/CONVENTIONS.md`
and `tools/audit.sh` (`float_cast.rs` exception and `ALLOW_SITES` entry), `README.md` (licence list: P4m's files, the
DXBCParser port).

**Interfaces:**
- Produces: `build.rs` — requires `AIRCONV_LLVM` and `AIRCONV_XCRUN` (`cargo:rerun-if-env-changed` for both);
  compiles `A/shim/shim.cpp` and `A/transforms/*.cpp` with `cc` (`-std=c++17 -fno-rtti -fno-exceptions -DNDEBUG -O3`,
  `-I$AIRCONV_LLVM/include`; bundled); compiles `A/shaders/{air_msad,air_samplepos,air_tessellation}.metal` with
  `$AIRCONV_XCRUN -sdk macosx metal -o $OUT_DIR/<n>.air -c <in> -std=metal3.1 --target=air64-apple-macos14.0` (that
  argument order is what `xcrun-metal.sh` recognises), run as `/bin/sh $AIRCONV_XCRUN …` when the file is not
  executable (`wine-arm64/tools/xcrun-metal.sh` is mode 0644: the machine files run it as `['/bin/sh', path]`,
  `wine-arm64/build.sh:497`, and meson's `full_path()` gives the script, `mesonbuild/programs.py:115-123`, so a direct
  exec fails with EACCES in every lane, CK and image); prints `cargo:rerun-if-changed` for `build.rs`, `shim/`,
  `transforms/`, `shaders/` and `$AIRCONV_LLVM/.complete` (once a build script prints any `rerun-if`, cargo stops
  rerunning it on other file changes, and `cc` prints only `rerun-if-env-changed`, `cc-1.8.0/src/lib.rs:4858`: without
  these an edit to the shim, a pass or a helper never rebuilds, in lanes or in the images' cargo directory); emits
  `-L$AIRCONV_LLVM/lib`, `src/airconv/meson.build`'s `llvm_deps` in order as `static:-bundle=`, then `z`, `curses`,
  `xml2`, `c++`, `m` (P6). L3 fills `shim.cpp`; T1 commits it with one probe function.
- `lib.rs` (both crates): opens with the `cfg_attr(not(test), deny(…))` line (Rules check).
- `api.rs`: the 14 exports with `airconv_public.h`'s signatures (handles pointer-sized, as `g10.rs` passes them), the
  error object (`Error { text: Vec<u8> }`: bytes, P15; message builders take `&[u8]`, LLVM names and messages are read
  as bytes through `LLVMGetValueName2` and `CStr::to_bytes`; `SM50GetErrorMessage`/`SM50FreeError` final now, P15's
  size-0 rule), the
  bitcode object (`Vec<u8>`, pointer stable until `SM50DestroyBitcode`); the shader handle is `Box<Shader>` with
  `Shader` in `A/src/shader.rs` (`Sync`; T1 creates it empty, P3 adds the DXBC variant, X1 the DXIL one, so lanes
  never edit `api.rs`), and `SM50GetArgumentsInfo` copies the two arrays the variant holds. Each other body calls one
  function of `entry.rs`: `initialize(bytes: &[u8], refl: *mut MtlShaderReflection) ->
  Result<Shader, Error>`, `compile(…)`, `compile_hull(…)`, `compile_domain(…)`, `compile_gs_vertex(…)`,
  `compile_gs(…)`, `passthrough_vertex(…)`, `entry_info(…)`, each returning `Err(Error::not_ported())` (`"not ported"`)
  until its lane replaces it. T1 creates the stubs in one file per owner, so lanes never share a file at once:
  `A/src/entry/initialize.rs` (`initialize`, `entry_info`: P3's DXBC half, then X1's DXIL half),
  `A/src/entry/compile.rs` (`compile`: D1, D2, then X2; `passthrough_vertex`: X2), `A/src/entry/tessellation.rs`
  (D3), `A/src/entry/geometry.rs` (D4, then X2's DXIL branch).
- `abi.rs`: `#[repr(C)]` mirrors (Rules check), enums read as integers, never transmuted; `ArgChain::walk(ptr)` per
  `A/dxbc_converter.hpp:556-572` (first node of each type wins, unknown skipped, null allowed).

- [ ] **Step 1: RED**: `abi/tests.rs` `ia_input_element_bitfields_lsb_first` (a `u32` with known bits → the fields
  `airconv_public.h:277-278` declares) and `arg_chain_first_of_each_type_wins`; `api/tests.rs`
  `every_export_answers_not_ported` (status 1, text `not ported`, `*ppError` set),
  `get_error_message_size_zero_writes_nothing` and `error_text_keeps_non_utf8_bytes` (an error made from `b"x\xff"`
  comes back from `SM50GetErrorMessage` as those two bytes, length 2).
- [ ] **Step 2: Implement**; `sh tools/winemetal-layouts.sh` regenerates `layouts.rs` and the existing outputs unchanged.
- [ ] **Step 3: Build script and link proof** (after G5a): with `AIRCONV_XCRUN=<M>/wine-arm64/tools/xcrun-metal.sh`
  (mode 0644), `cargo build -p airconv` writes the three `.air` files to `OUT_DIR` (RED first: a `build.rs` that
  execs the script directly fails with EACCES); `touch A/shim/shim.cpp` then `cargo build -p airconv -v` shows the
  build script and the shim's compile running again, and a second build with no touch runs neither. `cargo test -p
  airconv` links `$S/llvm15`; ledger the link time, and once the same against `$SRC/llvm-arm64` (ThinLTO, read-only)
  to record what P2 saves. `nm` of `$S/tool.sh rust`'s staticlib: 14 exported `T` symbols, no `LLVM*` definitions
  (P6).
- [ ] **Step 4: CK**; commit `airconv: two crates, the exports answer "not ported", the header's mirrors`.

### Task L2: bindings, newtypes, `Ty` (lane `l`)

**Files:** Create `A/src/llvm.rs`, `A/src/llvm/sys.rs` (hand-declared C API, 4-D2: the whole list every later lane
uses, L4's introspection set included; every shim function's declaration), `A/src/llvm/ty.rs`, `A/src/llvm/builder.rs`,
`A/src/llvm/tests.rs`; modify `A/shim/shim.cpp` (every shim function L3 fills, as a stub body that does nothing or
returns null, so a declared symbol always links).

**Interfaces:**
- Produces: `Context::new() -> Context` (typed pointers: `LLVMContextSetOpaquePointers(false)`; the diagnostic
  handler installed with P5's rule: errors recorded, warnings and notes written to stderr, remarks dropped, and
  `Context::take_error()` that each parse, link and verify checks); `Module`, `Func`, `Block` handles; `TyCx` interning
  `Ty` (Copy, per compile): `Int(bits)`, `Half`,
  `Float`, `Double`, `Vector(Ty, n)`, `Array(Ty, n)`, `Struct(id)`, `Function(ret, params, vararg)`, `Ptr { pointee:
  Ty, addrspace }`, `Label`, `Metadata`, with `lower(&self, ty) -> LLVMTypeRef` the only place a pointer type is made
  (spec §5 step 4); `Val { raw, ty }` (Copy); `Builder` with every build method taking and returning `Val`, built on
  `LLVMBuildLoad2`/`GEP2`/`Call2`, holding the current fast-math flags as state and applying them through the shim's
  setter (L2 writes the Rust side; L3 the shim's body); the identity traps
  (critic 5-8): `extract_element_u64`/`insert_element_u64` (an i64 constant index, as `IRBuilder`'s integer
  overloads) beside the value-index forms, `const_gep1_32`/`const_gep1_64`, `const_inbounds_gep1_32` (11 C++ sites)
  and `struct_gep` (3), each with `IRBuilder`'s index width and in-bounds flag (`dis=` is taken after the pipeline, and
  InstCombine rewrites constant vector indices to i64 and GEP indices to the index type,
  `L15/lib/Transforms/InstCombine/InstCombineVectorOps.cpp:381-387,400-402,1507-1510`, so most width mistakes never
  reach an LR: these tests and CK's site counts catch them), `vector_splat(len, v, name)` (insert
  into `poison` at i32 0 named `<name>.splatinsert`, zero-mask shuffle named `<name>.splat`),
  `Module::get_or_insert_function(name, ty, attrs)` (`LLVMGetNamedFunction` first; attributes only on creation; another
  type → the bitcast `getOrInsertFunction` returns, or an error, never silently), names through length-taking forms
  (`LLVMSetValueName2`, `LLVMMDStringInContext2`), an interior NUL → P15's error. Each wrapper: one `unsafe` operation,
  its `// SAFETY:`.

- [ ] **Step 1: RED** in `llvm/tests.rs`, each printing a fresh module and comparing with the text LLVM 15's C++ makes
  (written as literals from `L15/lib/IR/IRBuilder.cpp:1149-1161` and `IRBuilder.h:2284-2309`):
  `vector_splat_names_and_poison`, `extract_element_u64_is_i64`, `insert_element_u64_is_i64`, `const_gep1_32_is_i32`,
  `const_gep1_64_is_i64`, `const_inbounds_gep1_32_is_inbounds_i32`, `struct_gep_is_inbounds_i32_i32`,
  `get_or_insert_keeps_first_attributes`, `get_or_insert_other_type_is_a_bitcast`, `name_with_nul_is_an_error`,
  `ty_lowers_to_llvm_type_identity` (for each `Ty` kind, `lower` == the C API's own constructor, pointer equality),
  `diagnostic_handler_catches_link_error` (the call fails with `LLVM error: …`, no exit),
  `diagnostic_warning_does_not_fail` (linking two modules with different target triples: the link succeeds, the
  warning reaches stderr, `take_error()` is empty).
- [ ] **Step 2: Implement**; **Step 3: CK**; commit `airconv: the LLVM 15 C API, its newtypes and Ty`.

### Task L3: shim, module setup, helpers, pass pipeline, writer glue (lane `l`)

**Files:** Modify `A/shim/shim.cpp` (L2's stubs filled), `A/src/module.rs`, `A/src/writer.rs` (T1's stubs); create
`A/src/module/tests.rs`.

**Interfaces:**
- Consumes: L2; `airconv_ir::metallib::pack(bitcode: &[u8], functions: &[FunctionEntry]) -> Vec<u8>` (P4).
- Produces: shim C functions (all `extern "C"`, prefix `airconv_shim_`): fast-math guard and per-instruction setters
  (`A/nt/dxbc_converter_base.cpp:2942-2965`, `A/dxil/dxil_converter.cpp:239-252`), `write_bitcode` (`WriteBitcodeToFile(M,
  OS, false, nullptr, true)`), `link_only_needed`, `parse_bitcode` (P5), `run_passes` (`A/airconv_context.cpp:69-158`'s
  pipeline, the two passes in their adaptor; no `cl::opt` poke, K7), `split_block`, `split_block_and_insert_if_then`,
  `get_as_instruction`, `rename_struct`, `build_atomic_rmw_value`, `local_slot`, `set_sdk_version`,
  `declare_simdgroup_barrier` (G1b's `declareSimdgroupBarrier`, for A1's test). Rust:
  `module::initialize(cx, name) -> Module` (`airconv_context.cpp:40-65`: source name, triple, data layout, SDK 15.0,
  the nine flags), `module::link_helpers(m, msad, samplepos, tessellation)` (`:167-188`: six named metadata erased,
  `LinkOnlyNeeded`), `module::optimize(m)`, `writer::function_entries(m) -> Vec<FunctionEntry>` (the function list
  from `air.{vertex,fragment,kernel,object,mesh}`, `A/metallib_writer.cpp:42-355`) and `writer::write(m) -> Vec<u8>`
  (the shim's bitcode and that list, packed by P4).

- [ ] **Step 1: RED** in `module/tests.rs` (baseline under `AIRCONV_STEP4_BASE`, Global Constraints):
  `initialize_prints_the_baseline_header` (the module header and flags equal the first lines of
  `C1.dis/compute.cs.dxil.ll`); `helpers_parse_and_link` (each of the three, into a module calling one function,
  verifier clean); `writer_packs_baseline` (each `C1.out` metallib: `metallib::pack(its own bitcode bytes,
  writer::function_entries(parsed module))` equals the metallib byte for byte: the function list and the packing,
  exact); `writer_round_trip_keeps_the_text` (`writer::write` of the parsed module parses back to the baseline's
  `Module::print` text and P4-parsed tags, and writing that parse again gives the same bytes: a fixed point). Bytes
  of a same-LLVM read-then-write are not compared with the baseline's: 34 of 96 C1 metallibs differ in value-symbol
  order and the HASH record with equal text (map 4 §5.2, `4-tests.md:140-147`); Z3's `air=`/g10 rows stay information.
- [ ] **Step 2: Implement**; **Step 3: CK** (with `AIRCONV_STEP4_BASE`: the baseline tests run, none skips); commit
  `airconv: module setup, helpers, the pass pipeline, the metallib`.

### Task L4: the oracle (lane `o`)

**Files:** Create `A/src/oracle.rs`, `A/src/oracle/tests.rs`; modify `A/src/llvm/builder.rs` (one `#[cfg(feature =
"oracle")]` call at value creation).

**Interfaces:** Produces (feature `oracle`): at each value's creation `ty.lower(cx) == LLVMTypeOf(raw)`, else the
compile fails with `oracle: <op> made <llvm type>, Ty says <ty>`; `oracle::derive(m)` after `module::optimize`, on the
module `writer::write` packs (so its input is the baseline metallibs' module), over every pointer-typed value, from
facts opaque IR keeps (alloca type, global value type, GEP source type and indices, called function type, load and
store types, select and phi operands to a fixpoint), compared with LLVM 15's typed truth; each value it cannot type
prints `oracle-untyped <function> <instruction text>` to stderr (`let _ = writeln!`); a wrong answer fails the
compile.

- [ ] **Step 1: RED**: `derive_types_a_gep_chain`, `derive_reports_an_untypable_value` (a `bitcast` of an argument
  with no use).
- [ ] **Step 2: Implement**; **Step 3: Checks**: CK; a test-only run of `derive` over every `$S/base/*.out` module
  (C++ output, parsed bodies and linked helpers included) writes `$S/untyped/<c>.txt` (its `oracle-untyped` lines,
  `LC_ALL=C sort -u`; `wc -l` per corpus ledgered: W's input, target 0); the controller freezes the folder (`chmod -R
  a-w`). LR compares each run's list with it (a new entry fails). Commit `airconv: the type oracle`.

### Task P1: container, signatures, root signature (lane `p`)

**Files:** Create `A/ir/src/container.rs`, `A/ir/src/tokens.rs` (the subset of `libs/DXBCParser/d3d12tokenizedprogramformat.hpp`
used), `A/ir/src/signature.rs`, `A/ir/src/root_signature.rs`, `A/ir/src/dxil_part.rs`, and their `tests.rs`.

**Interfaces:** Produces the public API step 5's PE users consume (5-D6: 5b and 5c use it, never the C++ copies, so
nothing a call site needs may be missing: each PE call site and what replaces it, with the C++'s semantics):

| PE call site (DXMTRS at da6ed2d) | C++ | `airconv-ir` |
|---|---|---|
| `src/d3d12/d3d12_shader_cache.cpp:65-66` | `CDXBCParser::ReadDXBC` | `Container::parse(&[u8]) -> Result<Container, Error>` (every `BlobContainer.cpp:27-85` refusal; `find(fourcc)`, `part(i)` in order) |
| `src/d3d11/d3d11_input_layout.cpp:23-25`, `src/d3d12/d3d12_pipeline_graphics.cpp:171-172` | `DXBCGetInputSignature` + `CSignatureParser` | `signature::input(&Container)` |
| `src/d3d11/d3d11_input_layout.cpp:186-187` | `DXBCGetOutputSignature` + plain `CSignatureParser` (`DXBCUtils.h:17-19`) | `signature::output_v4(&Container)` |
| the translator (`DXBCUtils.h:20-22`) | `DXBCGetOutputSignature` + `CSignatureParser5` | `signature::output_v5(&Container)` |
| the translator | `DXBCGetPatchConstantSignature` | `signature::patch_constant(&Container)` |
| `src/d3d12/d3d12_root_signature.cpp:319,336,411` | `DXBCGetRootSignature` | `root_signature::from_container(&Container) -> Result<&[u8], Error>` |
| `src/d3d12/d3d12_root_signature.cpp:248-305,415-418` | `RootSignatureDeserializer::Deserialize` (`A/dxbc_root_signature.hpp:122-249`) | `RootSignature::deserialize(&[u8]) -> Result<RootSignature, Error>`, `RootSignature { version, v1_0, v1_1 }`: the raw version's description read, the other converted from it with `SetFlag`'s defaults (1.0 → 1.1: descriptors `DATA_VOLATILE`, non-sampler ranges `DESCRIPTORS_VOLATILE \| DATA_VOLATILE`, sampler ranges `DESCRIPTORS_VOLATILE`; `:50-77`); another version → `E_INVALIDARG`'s error |
| `src/d3d12/d3d12_root_signature.cpp:80-196` (the serializer) | `RawRootSignatureDesc`, `RawRootParameter`, `RawRootDescriptorTable`, `DXBCHeader`, `DXBCBlobHeader` | the same layouts as pub types (`root_signature::raw`, `container::raw`), with their sizes asserted |
| the DXIL front end | `FindDXIL` (`A/dxil/dxil_container.cpp:25-46`) | `dxil_part::find(&Container) -> Result<Option<DxilPart>, Error>` (its three texts) |

Every read bounds-checked (P15); no `unsafe`.

- [ ] **Step 1: RED** (fixtures from `A/ir/tests/data/`, T1): `container_rejects_part_past_end`,
  `container_rejects_huge_part_count`, `signature_of_vsia_vs` (`vsia.vs.dxbc`: four elements A-D as `dxbc.cpp`
  declares), one test per row above against the C++'s output on the same bytes: `input_signature_*`,
  `output_v4_*` and `output_v5_*` (literals read once from the C++ by the implementer, the reading noted),
  `root_signature_matches_the_cxx_dump` (below), `raw_layout_sizes`, `dxil_part_*` (the three texts).
  The root signature: G6's dumper (`$S/rsdump`, ~60 lines of host C++ over `dxbc_root_signature.hpp`) prints a
  canonical field list of `v1_0` and `v1_1` per blob; the Rust test prints the same list and compares, over the
  synthetic RTS0 (`dxil-translate.mm:53-79`'s bytes for two ranges) and committed 1.1 fixtures in
  `A/ir/tests/data/rs/` (root constants, a root CBV, a static sampler, range flags; each with the dumper's output
  beside it), and over every real root signature listed in `$AIRCONV_STEP4_BASE/rsdump/list.txt` (C8's 150 `rs-*.bin`
  read in place, C10's and C11's; G6 and G7 write the list and the dumps).
- [ ] **Step 2: Implement**; **Step 3: CK** (msvc clippy rows included); commit `airconv-ir: DXBC container,
  signatures and root signatures`.

### Task P2: decode, IR, CFG (lane `p`)

**Files:** Create `A/ir/src/decode.rs` (`readInstruction`, `A/dxbc_instructions.cpp`), `A/ir/src/ir.rs` (the 50 op
structs, `A/dxbc_instructions.hpp:95-863`, as plain data; `BasicBlock` with typed terminators), `A/ir/src/cfg.rs`
(`read_control_flow`, `A/dxbc_converter_cfg.cpp`), `A/ir/src/shader_info.rs`, tests.

**Interfaces:** Produces `cfg::read(program: &Program, sink: &mut dyn DeclSink) -> Result<Cfg, Error>` and
`ShaderInfo` (the `dcl_*` maps as `BTreeMap`); the token walk bounded by the blob, never by the length token alone
(P15); an unhandled opcode → `Instruction::Nop` plus the stdout line (P15).

- [ ] **Step 1: RED**: `token_walk_stops_at_blob_end` (a program whose length token claims past the blob → error),
  `cfg_of_sync_dxbc` (block count and terminator kinds of `A/ir/tests/data/sync.dxbc`, read from the C++ by the
  implementer once and written as literals with the reading noted), `unhandled_opcode_is_a_nop`.
- [ ] **Step 2: Implement**; **Step 3: CK**; commit `airconv-ir: decode and control flow`.

### Task P3: the handlers as data, the recorders, reflection, DXBC Initialize (lane `p`)

**Files:** Create `A/ir/src/handlers.rs` (the 31 DXBC signature closures of `A/dxbc_signature.cpp` as one enum),
`A/ir/src/binding.rs` (`Define*` recorders, `GetArgumentIndex`), `A/ir/src/reflection.rs`, `A/ir/src/estimators.rs`
(`A/dxbc_converter_ts.cpp:37-176`, through `float_cast.rs`), `A/ir/src/initialize.rs`; `A/src/entry/initialize.rs`
(DXBC branch; writes reflection).

**Interfaces:**
- Produces: `airconv_ir::initialize::dxbc(bytes: &[u8]) -> Result<DxbcShader, Error>` with `DxbcShader { program_type,
  info: ShaderInfo, cfg: Cfg, signature_handlers: Vec<SignatureHandler>, mesh_output_handlers: Vec<SignatureHandler>,
  func_signature: FunctionSignatureDesc, args_cbuffer, args, reflection: Reflection, … }`; each `SignatureHandler`
  variant holds exactly what its C++ lambda captured, in recorded order (D and X replay them; P3's own check does not
  test replay, D2's `dis=` does). `entry::initialize` writes `Reflection` into the caller's struct field by field
  through raw pointers (`addr_of_mut!` + `write`, nothing read, only the stage's fields, `dxbc_converter.cpp:1250-1330`,
  the hull write-back only with a non-null `pRefl`) (critic 4). `entry_info`'s DXBC half: inputs and outputs from P1's
  `signature::input` and `output_v5` as G1 defines them (system values mapped to `DXIL_SEMANTIC_*`), no resources.
- [ ] **Step 1: RED**: `interior_nul_name_is_an_error`; `hull_estimators_match` (inputs and outputs of
  `estimate_payload_size` and `get_max_potential_workload_count` for `patch.hs`, literals read from the C++);
  `reflection_writes_only_stage_fields` (a 0xA5-filled struct; a PS leaves `ThreadgroupSize` 0xA5); the
  initialize-only LR on C5 → differences (Initialize answers "not ported").
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Checks**: CK; the initialize-only LR on C4, C5, C6 and C10's shaders (all DXBC; C7, C8 and C11 are X1's) →
  `0 new failures, 0 fixed, 0 changed, 0 extra` against `$S/base/<c>-init.txt` (Initialize takes no Metal version,
  so one run per corpus; corpus-diff keeps "a new failure fails" while compile still answers "not ported", because the
  init runs never compile). Commit `airconv-ir: DXBC Initialize, its handlers as data and its reflection`.

### Task P4: MTLB and sha256 (lane `m`)

**Files:** Create `A/ir/src/metallib.rs`, `A/ir/src/sha256.rs`, tests.

**Interfaces:** Produces `metallib::pack(bitcode: &[u8], functions: &[FunctionEntry]) -> Vec<u8>` (`FunctionEntry {
name, kind, vertex_attributes, patch }`; header MTLB 2.7, OS 14.4, VERS 2.6/3.1 for every function as
`A/metallib_writer.cpp:63-68,183-186,…` hard-code it), `metallib::parse(&[u8]) -> Result<(Vec<u8>, Vec<FunctionEntry>),
Error>` (test use and L3's check), `sha256::digest(&[u8]) -> [u8; 32]`.

- [ ] **Step 1: RED**: `sha256_known_vectors` (FIPS 180-2 "abc" and the empty string),
  `repack_every_baseline_metallib` (`parse` then `pack` of each `$AIRCONV_STEP4_BASE/*.out/*.metallib` → equal bytes).
- [ ] **Step 2: Implement**; **Step 3: CK** (with `AIRCONV_STEP4_BASE`: the repack test runs over every baseline
  metallib, all equal); commit `airconv-ir: the metallib container and sha256`.

### Task A1: AIR types and intrinsics (lane `a`)

**Files:** Create `A/src/air/ty.rs` (`AirType`, `A/air_type.*`), `A/src/air/builder.rs` (`AIRBuilder`,
`A/nt/air_builder.*`: names, mangling from pointee `Ty`s, attribute lists), tests.

**Interfaces:** Produces `AirBuilder<'a> { b: &'a mut Builder, ty: &'a AirTypes }` with one method per C++ method
(`create_barrier`, `create_texture_query`, `create_fp_un_op`, …), each declaring through `get_or_insert_function`
with the C++'s attribute list; a null from the C++ becomes `Err(Unsupported)` (P15).

- [ ] **Step 1: RED**: `declares_match_baseline` — every `declare … @air.*` line in
  `$AIRCONV_STEP4_BASE/*.dis/*.ll` is produced by the Rust declarer for the same operand `Ty`s, attributes included (a
  table built by the test from the baseline text; G4's barrier kernel puts `air.simdgroup.barrier`'s declaration from
  AIRBuilder's attribute list among them, G5b Step 5); `barrier_declaration_matches_the_shim` (critic 2, after L3:
  `airconv_shim_declare_simdgroup_barrier`'s declaration and the declarer's, each in a fresh module, print the same).
- [ ] **Step 2: Implement**; **Step 3: CK** (with `AIRCONV_STEP4_BASE`); commit `airconv: AIR types and intrinsics`.

### Task A2: function signatures, argument buffers, metadata (lane `a`)

**Files:** Create `A/src/air/signature.rs` (`FunctionSignatureBuilder::CreateFunction`, `ArgumentBufferBuilder::Build`,
`A/air_signature.cpp:361-1049`), `A/src/air/metadata.rs` (version metadata, `A/dxbc_converter.cpp:201-232`), tests.

**Interfaces:** Consumes P3's `FunctionSignatureDesc` and binding descriptors. Produces `create_function(m,
&FunctionSignatureDesc, name) -> (Func, FunctionMeta)` and `build_argument_buffer(m, &[ArgumentDesc]) -> Ty`.

- [ ] **Step 1: RED**: `signature_matches_baseline` for C4's two CS and C5's `render.vs`/`render.ps` (the function's
  `define` line and its `!air.kernel|vertex|fragment` metadata tuple equal the baseline `.ll`'s under
  `AIRCONV_STEP4_BASE`), and for `render.vs#nolayout` (the `stage_in` signature).
- [ ] **Step 2: Implement**; **Step 3: CK** (with `AIRCONV_STEP4_BASE`); commit `airconv: function signatures, argument
  buffers, AIR metadata`.

### Task D1: the Converter, basic blocks, compute (lane `d`)

**Files:** Create `A/src/dxbc/converter.rs` (`A/nt/dxbc_converter_base.*`), `A/src/dxbc/blocks.rs`
(`A/dxbc_converter_basicblock.cpp`, both lowering styles become one), `A/src/dxbc/binding_sm50.rs`,
`A/src/dxbc/binding_rootsig.rs` (`A/dxbc_binding_rootsig.cpp`: the `#rs` variants and C10's pipelines need it from the
first CS on), `A/src/dxbc/cs.rs` (`A/dxbc_converter.cpp:474-575`), `A/src/dxbc/resources.rs` (temps, ICB, TGSM,
`:95-199`); modify `A/src/entry/compile.rs` (CS).

**Interfaces:** Produces `dxbc::Converter` (one method per instruction, the C++ `operator()`s), `blocks::convert(cx,
&Cfg) -> Result<(), Error>`, `binding_sm50::setup(…) -> BindingMap`, `binding_rootsig::setup(…) -> BindingMap`; the
four `unordered_map`s as `FxHashMap`, never iterated into IR.

- [ ] **Step 1: RED**: LR with `--stages cs` on C4, C5 and C6 (G4's barrier kernel) at 310, 320 and 320f, the `#rs`
  variants included → differences (compile not ported).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Checks**: the same LR, and C10's compute pipelines (`--pipelines $S/corpus/C10/pipelines.txt
  --no-metal`: DXBC CS under real root signatures) → `0 new failures, 0 fixed, 0 changed, 0 extra`, the untyped list
  unchanged; the
  barrier kernel's `air.simdgroup.barrier` call is in the Rust `.ll`; check.sh's `fence=2:1,5:3,5:3,7:3,7:3
  barrier=3` for `sync.dxbc` and `grad=` for `grad.dxbc` from `$S/bin/d/dxil-translate --flags $S/corpus/C4`; CK with
  site counts. Commit `airconv: DXBC compute in Rust`.

### Task D2: vertex, pixel, handler replay, bindings (lane `d`)

**Files:** Create `A/src/dxbc/vs.rs`, `A/src/dxbc/ps.rs` (`A/dxbc_converter.cpp:315-473,576-860`), `A/src/dxbc/replay.rs`
(signature handlers into prologue/epilogue); modify `A/src/entry/compile.rs` (VS, PS, stream output),
`A/src/entry/tests.rs`.

- [ ] **Step 1: RED**: `compile_on_eight_threads_matches_one` (Review Focus 2: `A/ir/tests/data/render.ps.dxbc`
  initialized once, compiled on 8 threads, every bitcode equal to a single compile); LR `--stages vs,ps` →
  differences.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Checks**: LR on C5 and C6 at 310, 320 and 320f, `--stages vs,ps` (every VS and PS variant: `#nolayout`,
  `#f4`, `#fmt<k>`, `#so`, `#gspt`, `#d`, `#v`, `#rs`), and C10's shaders at 310 and 320 with `--stages vs,ps` (SM5.1
  DXBC, under the `#rs` root signature) → `0 new failures, 0 fixed, 0 changed, 0 extra`, the untyped list unchanged;
  CK with site counts. Commit `airconv: DXBC vertex and pixel shaders in Rust`.

### Task D3: hull and domain (lane `ts`)

**Files:** Create `A/src/dxbc/ts.rs` (`A/dxbc_converter_ts.cpp:178-1056`); modify `A/src/entry/tessellation.rs`.

- [ ] **Step 1: RED / Step 2: Implement / Step 3: Checks**: LR `--stages vs,hs,ds` on C5's `patch.{vs,hs,ds}` and C6's
  `tessq` and `tessl` pipelines (quad + `fractional_odd`, isoline + `pow2`) at 310, 320 and 320f, every `#i<f>t<n>`
  and `#rs` variant → differences, then `0 new failures, 0 fixed, 0 changed, 0 extra`; CK with site counts. Commit
  `airconv: DXBC tessellation in Rust`.

### Task D4: geometry (lane `gs`)

**Files:** Create `A/src/dxbc/gs.rs` (`A/dxbc_converter_gs.cpp`, the `ShaderBody` callback form X2 uses); modify
`A/src/entry/geometry.rs`.

- [ ] **Step 1: RED / Step 2: Implement / Step 3: Checks**: LR `--stages vs,gs` on C6's `gs`, `gspt` (pass-through
  with RTAI) and `gsadj` (`lineadj`, `RestartStrip`) pipelines at 310, 320 and 320f, `#s0`, `#s1` and `#rs` →
  differences, then `0 new failures, 0 fixed, 0 changed, 0 extra`; C1's `triangle2.{vs,gs}` waits for X2; CK with site
  counts. Commit `airconv: DXBC geometry pipelines in Rust`.

### Task X1: DXIL Initialize (lane `x`)

**Files:** Create `A/src/dxil/initialize.rs`, `A/src/dxil/metadata.rs`, `A/src/dxil/signature.rs` (its 18 handlers as
P3's enum variants), `A/src/dxil/types.rs`; modify `A/src/entry/initialize.rs` (DXIL branch, `DXILGetEntry`).

**Interfaces:** Produces `dxil::initialize(part: &DxilPart, capture_hash: u64) -> Result<DxilShader, Error>` (its own
typed context, bitcode through P5's wrapper, the bitcode kept for Compile), every reflection field written (DXIL
writes all, `~0u` binding indices, `A/dxil/dxil_initialize.cpp:326-328`), `entry_info` for G1's getter.

- [ ] **Step 1: RED**: the initialize-only LR on C1, C2, C3 and C7 → differences.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Checks**: the initialize-only LR on C1, C2, C3, C7, C8 (`--initialize-only` over the folder, in
  place), C10's and C11's shaders → `0 new failures, 0 fixed, 0 changed, 0 extra` against `$S/base/<c>-init.txt`
  (refl and reasons, whatever status the C++'s init baseline gives each C3 shader); C7's reasons byte-equal to
  the C++'s, every reader row's starting `DXIL: can't read the bitcode:` (so the Rust went through P5's
  `parseBitcodeFile` wrapper and the handler, not the C API's reader, which would `exit(1)`), no `exit`; CK. Commit
  `airconv: DXIL Initialize in Rust`.

### Task X2: DXIL lowering (lane `x`)

**Files:** Create `A/src/dxil/converter.rs`, `A/src/dxil/lower.rs`, `A/src/dxil/lower_resources.rs`,
`A/src/dxil/lower_math.rs`, `A/src/dxil/passthrough.rs`, `A/src/dxil/probe.rs` (`DXMT_PROBE`, `LazyLock`),
`A/src/dxil/bounds.rs` (`DXMT_DXIL_BOUNDS`, `LazyLock`); modify `A/src/entry/compile.rs` (DXIL branch).

**Interfaces:** Consumes D2's `Converter`, binding maps and replay; D4's GS emitters. Produces `dxil::convert(…)`
(`A/dxil/dxil_converter.cpp:388-556`: blocks moved with `LLVMMoveBasicBlockAfter`, never `AppendExistingBasicBlock` on a
parented block; `StripFastMath`; the verifier).

- [ ] **Step 1: RED**: `compile_on_eight_threads_matches_one` for `A/ir/tests/data/cache.cs.dxil`;
  `static_global_name_keeps_its_bytes` (a module made through the C API with a non-constant global named `g\xff`,
  through `move_constants_to_constant_space`: the error's bytes are `DXIL: static (non-const) global g\xff not
  supported`, P15); LR on C1, C2, C3 → differences.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Checks**: LR on C1, C2, C3, C8 (`--pipelines <C8>/pipelines.txt`, in place, hashes only), C11
  (`--pipelines` and its shaders) and the DXIL in C1's `triangle2.{vs,gs}` → `0 new failures, 0 fixed, 0 changed, 0
  extra`, the untyped list unchanged (an entry the run lacks is a W-plan item, ledgered); `DXMT_DXIL_BOUNDS=component`
  and `=off` runs of C1 equal the C++'s under the same variable (the baseline tool re-run with it, host-only); the
  probe: `DXMT_PROBE=<h>:<a>,<b>,<c>` with `<h>` the FNV-1a 64 of `cache.ps.dxil`'s bytes as `SM50Initialize`
  computes it (`A/dxbc_converter.cpp:1015-1017`, `%016llx`) and three scalar value numbers of its DXIL, and a second
  spec `<h>:<a>,-,<c>,a` (a kept channel and a kept alpha), each value in its own process, both tools: the probed C++
  `.ll` differs from the unprobed one (so the numbers exist and the probe applied; `check.sh:232`'s
  `0000000000000000:-,-,-` matches no shader) and the Rust `dis=` equals the probed C++'s; the hash and numbers
  ledgered; `$S/bin/x/dxil-translate --flags` on C1 and C2 → check.sh `:1079-1099`'s values; CK with site counts.
  Commit `airconv: DXIL lowering in Rust`.

### Task Z1: the switch (DXMTRS, owner)

**Files:** Modify `src/winemetal/unix/Cargo.toml` (dependency `airconv`), `src/winemetal/unix/src/g10.rs` (P6),
`src/winemetal/unix/meson.build` and `src/nativemetal/meson.build` (custom targets get `env: {'AIRCONV_LLVM':
native_llvm_path, 'AIRCONV_XCRUN': xcrun.full_path()}`; `airconv_dep_darwin` keeps only the LLVM link arguments),
`A/meson.build` and `A/darwin/meson.build` (no `libairconv`, no CLI, no `build_airconv_for_windows`), `meson.options`
(that option gone), `.github/workflows/ci.yml` (`:423` passes `-Dbuild_airconv_for_windows=true`), `Cargo.lock`,
`tools/rust-check.sh` (its `${AIRCONV_LLVM:?…}` message names MacNeutron's `make llvm-nolto` install,
`build/wine-arm64-src/llvm-arm64-nolto`, P2); delete `A/*.cpp`, `A/*.hpp` but `dxbc_root_signature.hpp`, `A/nt/`, `A/dxil/*.cpp|hpp`
but `dxil_public.h`, `A/airconv_cli.cpp`, `A/monad.hpp`, `A/packed_variant_list.hpp`, `A/sha256.hpp`,
`A/metallib_writer.*`; keep `A/airconv_public.h`, `A/dxil/dxil_public.h`, `A/dxbc_root_signature.hpp`,
`A/transforms/*.{cpp,hpp}`, `A/shaders/*.metal`, `A/shim/shim.cpp`, `A/darwin/meson.build`. (Checked 2026-10-30:
`dxbc_root_signature.hpp` includes only `d3d12.h` and standard headers, `airconv_public.h` only standard headers;
Step 1 re-runs that `grep '#include'` and keeps whatever they name.)

- [ ] **Step 0: Rebase** `step4a-translator` onto DXMTRS `main` (steps 3 and 5 may have pinned since 4a branched);
  ledger both shas; a conflict outside `src/airconv` and the root manifest/lock stops for the controller. From Z3 to
  Z5 the controller declares a DXMTRS-`main` pin freeze in the program ledger; if `main` moves anyway, Z5 rebases
  again and re-runs Z3's Mac, Translator and key rows before pinning.
- [ ] **Step 1: Implement**; `nm -gU` of a scratch `meson compile` (own build dir, host-only from the worktree): the
  14 exports in `winemetal.dylib`.
- [ ] **Step 2: Checks**: CK; `git grep -n 'libairconv\|airconv_dep_darwin\|build_airconv_for_windows'` → only the
  dependency's new definition. Commit `airconv: the translator is Rust; winemetal links it`.

### Task Z2: MacNeutron's same-pin edits (lane `mn`)

**Files:** Modify `wine-arm64/lib.sh` (P7, both compositions), `wine-arm64/tests/translator_key_test.sh` (a scratch
workspace with `src/airconv/Cargo.toml` and `src/airconv/ir`; rows below), every `translator_key` caller's `PATH`
gains `$(sh dxmt/mtld3d-tools.sh)` and exports `MTLD3D_RUST` (the two callers: `wine-arm64/build.sh:518` and
`eac-vm/dxmt/inert.sh:24`; `eac-vm/dxmt/build.sh` and `eac-vm/dxmt-native/build.sh` call none, they pass the file
`wine-arm64/build.sh` writes as `-Dtranslator_key_file`); create `dxmt/tools.sh` (P8:
`build_probe`, `build_translate` linking `cargo rustc -p airconv --profile production --crate-type staticlib`,
`CARGO_TARGET_DIR=$SRC/airconv-cargo`); modify `dxmt/llvm.sh` (tool builders gone), `wine-arm64/build.sh` (`:159-168`
stamp, `dxmt/llvm.sh` and the tools' sources at `:161-162`, and `:568-569` the tool builds; and an order change: the
`PATH=…mtld3d-tools.sh` line (`:531`) and R-D8's `cargo fetch --locked` (`:544`) move ahead of
`translator=$(translator_key …)` at `:518`, because P7's `cargo tree --locked --offline` needs the dependencies'
manifests in the cache on a fresh `CARGO_HOME`; `inert.sh` gains the `PATH` line; line numbers as of c36dab4),
`Makefile` (target `llvm-nolto`: `build_llvm arm64 $SRC/llvm-arm64-nolto $B/dxmt-src/llvm-project` with the CPU
`wine-arm64/build.sh` uses and `UNIX_LTO` unset, P2's recipe; a no-op while its `.complete` matches; run in the
lock, since it reads `make wine-arm64`'s clone), `tools/rust-check.sh`'s documented input list in the ledger's gate
commands: R's RC form gains `AIRCONV_LLVM=$PWD/build/wine-arm64-src/llvm-arm64-nolto
AIRCONV_XCRUN=$PWD/wine-arm64/tools/xcrun-metal.sh`, and the program ledger tells steps 3 and 5 (their RC needs
`make llvm-nolto` once; Z1 names it in DXMTRS's `tools/rust-check.sh`).

`translator_key_test.sh` rows (each a key or a die, against the scratch workspace):
- an edit to a crate outside airconv's dependency set keeps the key; a lock edit inside it changes it;
  `libs/DXBCParser` is no longer listed;
- an edit to the root `meson.build` or to `include/` keeps the key; an edit to `[profile.production]` changes it;
- another toolchain changes it: `MTLD3D_RUST=nightly` (installed for rustfmt, with its own `cargo` and `rustc`; not
  `stable`, which may be 1.99.0 itself) with the scratch `meson.options`' `cargo_toolchain` default set to match;
- an uninstalled `MTLD3D_RUST` dies and prints no key; a `Cargo.lock` out of step with a manifest dies; a path
  dependency outside `src/airconv` dies; a `cargo_toolchain` default other than `MTLD3D_RUST` dies; an empty `cargo
  tree` output dies;
- a tree without `src/airconv/Cargo.toml` gets exactly the key `dxmtrs`'s `lib.sh` (`git show dxmtrs:wine-arm64/lib.sh`,
  sourced in a subshell) gives it.

- [ ] **Step 1: RED**: `translator_key_test.sh` with only the new rows written → fails on the old `lib.sh` (the die
  rows print a key there).
- [ ] **Step 2: Implement**; `sh wine-arm64/tests/translator_key_test.sh` → ok. The order, checked statically (no build:
  a `wine-arm64/build.sh` run is a pipeline build, needs the signing variables and, on an empty `CARGO_HOME`, the
  network): `sh -n` on every script Z2 edits; in `wine-arm64/build.sh` (the one build script that calls
  `translator_key`) the line numbers of the `mtld3d-tools.sh` `PATH` line, of `cargo fetch --locked` and of
  `translator=` increase in that order (`awk`); Z3's `make wine-arm64`, in the lock, is the proof. Commit on `step4-mn`
  `DXMT: the translator key covers the Rust translator; tool builders in dxmt/tools.sh; make llvm-nolto`.

### Task Z3: the gates, once (owner)

Every row once on `step4a-translator`'s head (Z1's rebase) with `<M>` at `step4-mn`, switched detached at the start of
each lock job and back to `dxmtrs` at its end (Global Constraints); a failing row is recorded, not fixed (Z4).
References: the newest landed step's (R's `$B/step-r/ref-*`, or step 3's), named in the ledger; command forms as R's
Task 9. Reports `$S/reports/gates-{mac,native,vm,translator,rust}.md`.

- [ ] **Mac.** `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot` (one lock
  job per command group, cdhash recorded); the new key ≠ R's, ledgered; `sh dxmt/check.sh` and the ARM64EC form → 412
  ok, 0 FAIL each (flaky rule as R's); `sh wine-arm64/check.sh dxmt dxmt-present`; `sh presenter/check.sh`; `sh
  dxmt/wm-probe-check.sh` and `--full` → 11 ok (g10's five `bitcode=` lines equal, or re-recorded with the reason in
  one MacNeutron golden commit, K4); facade test, both `d3d9-check` forms, conformance (R's figures);
  `wine-arm64/tests/{mode,licences,translator_key}_test.sh`.
- [ ] **Native and VM host.** `sh eac-vm/dxmt-native/build.sh`; `nm -gU build/dxmt-native/install/lib/winemetal.dylib`
  → the 14 names; native tests and `gate.sh` (all 31); `wm-probe-check.sh --native`; `eac-vm/host/build.sh`, metal
  gate, `host-test`, `memory-test`; `sh eac-vm/d3d/loopback-gate.sh` (two translators in one host process).
- [ ] **VM.** `sh eac-vm/dxmt/build.sh` (the guest gets the new key file), `check-build.sh`, guest stages, `sh
  eac-vm/dxmt/gate.sh` (44), `d3d-smoke` both WoW64 modes, `d3d9-gate`, `present-gate`.
- [ ] **Translator** (critic 19, 21; P10). With `build/wine-arm64/dxil-translate` (Z2's build, ThinLTO, oracle off;
  copied out in the Mac lock job): `shasum -c` of `$S/base/inputs.sha256`, `C8.sha256`, `C10.sha256`; every run
  `$S/base` holds (C1-C7, the `-320`, `-320f` and `-init` runs, C8, C10, C11 and their shader runs) → corpus-diff
  `0 new failures, 0 fixed, 0 changed, 0 extra`; two processes with `--out` → `cmp -r` equal; translate-only mean per
  corpus against G5b's (information, P10's gap named); `$B/step-r/ident.sh` and
  `ident-diff.sh` on `winemetal.so` and `winemetal.dylib` against R's before-tables, every difference listed (exports:
  C++ symbols gone, 14 Rust exports; imports; sizes), install name, rpaths, minos, `___wine_unix_call_*`, the single
  `NtSetEvent` import unchanged.
- [ ] **Rust and licences.** `make llvm-nolto` in a lock job (its `.complete` = P2's recipe), then RC (with Z2's
  inputs) → `rust-check: ok`; `cargo metadata` → `airconv` and `airconv-ir` with their licences, no new third-party
  package; README lists P4m's files and the DXBCParser port.

### Task Z4: one fix pass (owner, lanes as needed)

- [ ] **Step 1:** per failing row: root cause, a failing test or LR row first, the fix in the owning file, its task's
  checks, the failed rows once, and every gate whose inputs the fix touched (named in the ledger). A translator fix
  re-runs the Translator block. A second round goes to the maintainer.
- [ ] **Step 2:** commit each fix (`airconv: …`, MacNeutron on `step4-mn`).

### Task Z5: spec, push, pin (controller)

- [ ] **Step 1: Spec** on `dxmtrs`: §5 step 4 records 4a done (date, DXMTRS sha), 4-D2 (no `llvm-sys`: amends §4's
  "llvm-sys with no-llvm-linking"), 4-D3 (the shim's C++ through 4b, `sm50_wow64.c` to step 6; §1's end state at step
  6), the key rule (P7, with 5-D6), P1's outcome, P15's list; 4b's handoff (critic 23: `dxil-probe.cpp` and
  `dxil-translate.mm` move to LLVM 23's API in 4b's bump; L4's untyped list; G5b's timing and coverage ledger; `make
  llvm-nolto` moves to LLVM 23 with the recipe). Privacy-scan spec, plan,
  ledger. Commit `Spec: DXMTRS step 4a, the translator in Rust on LLVM 15`.
- [ ] **Step 2: DXMTRS main.** Restore `$D` to `git checkout --detach $(cat $SRC/dxmt.applied)` (rule 5);
  privacy-scan `git -C $D log -p main..step4a-translator`; `merge --ff-only`; `push --dry-run`; push. Remove worktrees
  and lane branches (`git cherry` empty first).
- [ ] **Step 3: Pin, one commit** (P13, P7): `git merge --squash step4-mn` onto `dxmtrs`, `DXMT_COMMIT=<main's sha>`,
  commit `DXMT: the translator in Rust (DXMTRS <sha>)`; with 5b's switch in the same bump when P7 says so.
- [ ] **Step 4: Pinned build** (lock job): `make wine-arm64` re-fetches the pin; `DXMT/version` = `<sha>+<series>`;
  `git -C $D status --porcelain` empty; the key equals the one Z4's last head gave (Z3's when Z4 changed nothing in
  `src/airconv`); both check.sh lanes 0 FAIL; `wm-probe-check.sh` 11 ok.
- [ ] **Step 5: MacNeutron main** fast-forward and push; a failed fast-forward stops for the maintainer. Ledger and
  program ledger: 4a done; ask 4-D9's bench question; 4b's plan is written next, from W's results.

## Questions for the maintainer

1. 4-D9: the milestone benches (SMITE 2 lobby, Palworld title, a D3D11 title) after Z or after 4b? (Asked when Z and
   4b are ready. The corpus questions were answered on 2026-10-30, 4-D7.)

## Review record

(Three adversarial Opus reviewers, 4-D10; findings numbered as received, each verified against the source before
applying.) Noted while writing: `7-decisions.md` cites critic rows by an older numbering (its "critic 7/9/10/12/13" are
rows 11/15/22/24/26 of `6-critic.md`, and it says "rows 1-14 accepted" of a 27-row file); this plan maps every row by
content (Decisions, "Critic rows").

Round 1 (2026-10-30; reviewers `plan-review-4a-seams.md` = SR, `-gates.md` = GR, `-exec.md` = ER in
`build/step4/map/`; numbered as received; each checked against the cited source first):

- SR1 (Critical, no `pipefail` in the key) — applied: P7 "No line is computed inside the hashed pipeline" (both
  compositions), Z2 rows (uninstalled toolchain and stale lock die with no key; another toolchain through the
  installed `nightly`).
- SR2 (C7 never reaches the reader) — applied: G5b Step 2's reader rows keep every size; G5b Step 5 and X1 assert the
  `DXIL: can't read the bitcode:` prefix.
- SR3 (the handler's rule) — applied: P5 (errors recorded, warnings printed and continued, remarks dropped), P15's
  `LLVM error:` text (no `DXIL:`, so E_FAIL), L2's `diagnostic_warning_does_not_fail`.
- SR4 (corpus-diff and "not ported") — applied: G2's `--initialize-only`, G5b's `-init.txt` baselines, P3's and X1's
  init LRs; "a new failure fails" stays unconditional.
- SR5 (the barrier pass never fires) — applied: G4's `barrier.hlsl`; G5b Step 5 requires an `air.simdgroup.barrier` call
  and equal G1a/G1b text; A1's baseline declarations then include AIRBuilder's.
- SR6 (the tool's reach into `dxil_types.hpp`) — applied: G1's enumerators and `static_assert`s, `num_semantic_indices`,
  G2's two-header syntax check, the `DXIL_ENTRY_*` mirrors (T1, Rules check). `DXIL_STAGE_DXBC_COMPUTE` folded into
  `DXIL_SHADER_KIND_*`: `ShaderKind` and `D3D1x_SB_*` share the values 0-5, asserted.
- SR7 (4-D7 is answered) — applied: 4-D7, P1, Task 0 Step 3, P12 out, Questions; its "extractor stays its own ruling"
  superseded by GR3 below.
- SR8 (P1's call sites) — applied: P1's table (each PE site, its C++ semantics, its replacement) and one test per row;
  `d3d12_pipeline_graphics.cpp:171-172` and the serializer's header layouts added from a grep.
- SR9 (the key's file list) — applied: P7 drops `include`, `meson.build`, `meson.options`; hashes the cargo profiles and
  config sections; dies on a `cargo_toolchain` default other than `MTLD3D_RUST` (chosen over passing
  `-Dcargo_toolchain`, which older pins without the option would fail, `wine-arm64/build.sh:528-530`); Z2 rows. Two
  corrections: `[profile.release]` is hashed too (production inherits from it), and `.cargo/config.toml` has no
  aarch64-darwin rustflags today (its `[build]`, `[env]` and `[target.aarch64-apple-darwin]` sections are hashed).
- SR10 (a path crate outside `src/airconv`) — applied: P7 die, Z2 row.
- SR11 (panics on guest values) — applied with one change: `#![cfg_attr(not(test), deny(…))]` instead of a deny plus a
  test-module `allow` in `ALLOW_SITES` (no suppression at all; `--all-targets` still lints the library); `operator[]`
  → `entry(k).or_default()` (Rules check).
- SR12 (error bytes) — applied: P15, T1 (`Error { text: Vec<u8> }`, `error_text_keeps_non_utf8_bytes`), X2
  (`static_global_name_keeps_its_bytes`, a module built through the C API: the function is reached without DXIL
  metadata, which a hand-made container would need).
- GR1 (Critical, C7) — applied with SR2 and ER7: well-formed reader rows (its "part and container sizes adjusted" clause
  not adopted: lowering `BitcodeSize` alone keeps `ReadDXBC` and `FindDXIL` passing), `FindDXIL` rows, one row per
  reachable `ReadDXBC` refusal, reasons ledgered, crash rows dropped and ledgered, P5's `"dxil"` buffer name.
- GR2 (Critical, corpus-diff) — applied: status changes either way, extra shaders and zero compared exit 1; fail lines
  carry the stage; `--stages` by the before-line; LR's Expected line. `--allow-not-ported` not adopted: SR4's init mode
  covers P3 and X1 without loosening compile lines.
- GR3 (Critical, 4-D7) — applied: G6 unconditional after G2, C8 hash-only in X1, X2 and Z3; P12 skipped; G7 plans the
  ~40-line extractor (committed as `dxmt/tools/prc-extract.py`), split into C10 (DXBC) and C11 (DXIL) so DXBC lanes
  never meet DXIL lines.
- GR4 (Critical, DXBC under root signatures) — applied: G3's `#rs` variant (ranges from the reflection's slot masks,
  `ROOT_SIGNATURE2` for a pipeline's VS), `binding_rootsig.rs` moved to D1 (its CS `#rs` lines need it), D1-D4 run
  `#rs`, C10's real DXBC compute pairs at D1 and Z3, its shaders at D2.
- GR5 (L3's repack fails) — applied with ER4: `writer_packs_baseline` (bytes, from the baseline's own bitcode) and
  `writer_round_trip_keeps_the_text` (text, tags, byte fixed point).
- GR6 (untyped count) — applied: L4 writes `$S/untyped/<c>.txt` (not `$S/base`, which lanes never write), LR's `comm`
  check with `wc -l`, D1 depends on L4.
- GR7 (no 320 baselines) — applied: G5b Step 3's run list, Step 5's file list and its 320 ≠ 310 check.
- GR8 (chain values never varied) — applied: G3's variant matrix, D2-D4's LRs name the variants.
- GR9 (shader flags) — applied: `--shader-flags`, P9, G5b's `-320f`, D1-D4, Z3. C1 with flags 3 is not run (D3D12
  passes 0) rather than run as information.
- GR10 (coverage unknown) — applied: G4's `tess`, `gspt`, `gsadj`, `sysval` HLSL; G5b Step 4's coverage ledger;
  uncovered functions named transliteration-only.
- GR11 (corpora move) — applied: G5b Step 1's snapshots, `inputs.sha256`, LR's `shasum -q -c` first; C8 and the 3DMark
  recording hashed in place, not copied (4-D7).
- GR12 (G1a compared with nothing) — applied: the pinned tool copied in G5b's lock job (an installed output), `air=`
  equal on C1, C2, C4 against the G1a ThinLTO tool; the barrier half merged with SR5.
- GR13 (GEP overloads, site counts) — applied: L2's `const_inbounds_gep1_32`, `struct_gep` and a RED per overload; CK's
  site counts with a ledgered value-index list. Its totals not adopted: `grep -o` on da6ed2d gives 48 extract + 109
  insert (not 148); the plan states the grep and re-counts at Task 0.
- GR14 (`DXMT_PROBE` never applies) — applied: X2 names the hash, ledgers three value numbers and a second spec, one
  process each; the probed C++ `.ll` differing from the unprobed one proves the numbers exist, so `dxc -dumpbin` is
  not required.
- GR15 (L3's tests skip) — applied with ER8: `AIRCONV_STEP4_BASE` (unset: skip with a reason; set: a missing folder
  fails), CK sets it from G5b on, L3 depends on G5b.
- GR16 (root signatures untested) — applied: G6's dumper over `dxbc_root_signature.hpp`, dumps of C8's, C10's and C11's
  root signatures in `$S/base/rsdump/`, P1's comparison over those, the synthetic RTS0 and committed 1.1 fixtures.
- ER1 (Critical, `xcrun-metal.sh` is 0644) — applied: `build.rs` runs it through `/bin/sh` when not executable; T1
  Step 3's RED; Global Constraints; Rules check. The `chmod +x` alternative not taken (it fixes one caller only).
- ER2 (Critical, `rerun-if-changed`) — applied: T1's list and its touch check.
- ER3 (Critical, corpus-diff) — merged into SR4 and GR2. Its "status and reason compared only when `reason` is in
  `--fields`" rejected: that is the loosening SR4 warns of; `--fields` is dropped (nothing uses it after init mode).
- ER4 (Critical, L3's repack) — merged into GR5, the fixed point adopted.
- ER5 (G5 as one task) — applied: G5a and G5b, the dependency rows, A1's barrier test after L3, G1b's
  `declareSimdgroupBarrier` exported by L3's shim.
- ER6 (lanes share files) — applied: Execution's "Shared files are made once" (T1's stubs, L2's whole C API, shim
  stubs and fast-math setter, L3's three files, X2 after D4 for `geometry.rs`).
- ER7 (C7 by zero-fill) — applied in part: the zero-filled tail at full size is one of C7's reader rows. "Size fields
  that lie go to P1's unit tests" rejected: the size rows compare reasons with the C++ end to end, which a unit test
  cannot; P1's `container_rejects_*` stay as well.
- ER8 (tests read MacNeutron paths) — applied: four fixtures copied into `A/ir/tests/data/` (T1), `AIRCONV_STEP4_BASE`
  (Global Constraints, Rules check).
- ER9 (`cargo tree` in the pipe, old trees) — applied with SR1: v1 composition kept for trees without
  `src/airconv/Cargo.toml` (Z2 row), `--no-dedupe`.
- ER10 (Z2 runs `build.sh`) — applied: Z2 Step 2's static order check.
- ER11 (branches at Z) — applied: Z1 Step 0's rebase and pin freeze, `<M>` switched detached only inside lock jobs
  (`check.sh:13` and `ref.sh:20` rule out the worktree route as-is), Z5 Step 4's key from Z4's last head.
- ER12 (RC on a scratch LLVM) — applied: `make llvm-nolto` (Z2), P2, Z1's `rust-check.sh` message, RC form, Z3, the
  program-ledger note.
- ER13 (`DXILGetEntry` empty for DXBC) — applied, first option: G1 fills DXBC inputs and outputs from ISGN/OSGN and
  adds `num_semantic_indices`; P3 mirrors it. "The tool parses OSGN" not taken: it needs a container reader in the
  tool, against SR6.
- ER14 (4-D7) — merged into SR7 and GR3; "no extractor stays a ruling" superseded: 4-D7 puts 3DMark's recording in the
  corpus, so G7 plans the extractor.
- Noted while applying (no reviewer): T1 depends on G1b (its `build.rs` compiles `transforms/`, which includes
  AIRBuilder until G1b); L4 alone creates `$S/untyped`; a task's "Create" fills T1's stub; G2's syntax check names
  its include paths.
