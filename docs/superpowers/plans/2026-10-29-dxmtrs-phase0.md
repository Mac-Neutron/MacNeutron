# DXMTRS Phase 0 and repository Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mac-Neutron/DXMTRS exists and MacNeutron builds from it, and Phase 0's three experiments have answered
whether Rust links into every DXMT binary (E1), whether Metal loads AIR written by the latest LLVM (E2), and whether
AIR builds faster than MSL (E3), with the translator decision recorded in the spec.

**Architecture:** DXMTRS is our dxmt fork's history under a new name; its working copy is MacNeutron's existing DXMT
tree, `build/wine-arm64-src/dxmt`, with `origin` pointed at DXMTRS. Experiment code lives on DXMTRS branch `phase0`
and is never merged as is; MacNeutron's side (pins, tool options, the build scripts' Rust toolchain, results) is on
MacNeutron branch `dxmtrs`. LLVM 23.1.4 is built from the local `build/forks/llvm-project` tag `llvmorg-23.1.4`.

**Tech Stack:** Rust 1.99.0 (`MTLD3D_RUST`), meson, llvm-mingw (MacNeutron's own), LLVM 15.0.7 and 23.1.4, Metal,
Objective-C++, DXC (Windows build, under the frozen Rosetta reference), SPIRV-Cross (fetched with the maintainer's yes).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§3, §4, §9).

## Global Constraints

- Never push, create a repository, or fetch from the network (git or otherwise) without the maintainer's yes in chat;
  each such step below says STOP.
- Never open MacNeutron.app, start or quit Steam, run a game, or touch `~/Library/Application Support/MacNeutron` or
  `steamapps/compatdata`. Game shader capture is the maintainer's to do (Task 4).
- Kill processes by exact PID only (no `pkill -f`). Each bench or timing run once (E3's two cold runs are one check).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; no hook bypasses; write home as `~`.
- DXMT_COMMIT stays `89558c1ad38f19121dddeb655b507dbe5c957ce7` in this plan; MacNeutron builds DXMT `phase0` in
  development mode (the tree's HEAD differs from the pin), never as a release.
- Rust: toolchain `1.99.0`; new crates follow mtld3d's CONVENTIONS.md; no crate dependencies in Phase 0 code.
- Results are written into the spec (§4), not into a new document; raw outputs stay in `build/phase0/` (never commit a
  game's shaders).

## Review Focus

1. The aarch64 (native ARM64) half of ARM64X DLLs: no check lane runs it (both lanes run the arm64ec half); the link
   must still resolve the probe in both halves — Task 2 Step 5 inspects both halves' symbol tables.
2. The x86_64 and i686 guest DLLs are built but not run in Phase 0; a Rust std import the guest Wine lacks would only
   show at load — Task 2 Step 6 lists each guest DLL's imports and compares them with the C++-only build's.
3. Shader-cache hits silently skipping E2's runtime round trip — Task 4 Step 4 uses a fresh
   `DXMT_SHADER_CACHE_PATH` and requires a nonzero count of round-tripped shaders.
4. Metal's own compiled-shader cache making E3's second path look fast — Task 5 Step 4's cold-cache check.
5. The `phase0` hook leaking into a normal run — Task 4 Step 5 reruns the check without `DXMT_PHASE0_ROUNDTRIP`.

## Rulings against the spec (for the maintainer's review)

- **E1's plumbing stays on `phase0`.** The spec keeps experiment code on a branch; the cargo-in-meson plumbing too is
  only cherry-picked to `main` by step 1's plan, when nvapi's port needs it (no scaffolding ahead of its first user).
- **The `pe/` and `unix/` rename** of mtld3d's workspaces (§3 Layout) waits for step 1's plan, with the build-script
  move: both rewrite the same MacNeutron paths. Phase 0 adds its crates to `mtld3d/windows` and `mtld3d/unix`.

---

### Task 1: DXMTRS created and pinned

**Files:**
- Modify (MacNeutron): `dxmt/pins:2-3` (the comment and `DXMT_REPO`), and every file `grep -rn 'Mac-Neutron/dxmt'`
  finds outside `build/` and `docs/` that names where shipped DXMT source lives.
- Modify (local only): `build/forks/LEDGER.md`.

**Interfaces:**
- Produces: `DXMT_REPO=https://github.com/Mac-Neutron/DXMTRS.git`; `build/wine-arm64-src/dxmt` with `origin` at
  DXMTRS; MacNeutron branch `dxmtrs` (exists, holds the spec).

- [ ] **Step 1: STOP — ask the maintainer** to push DXMT `89558c1` to `Mac-Neutron/dxmt` `main` (fast-forward from
  `15899428`, so MacNeutron `8f1513a`'s pin is public), to create the public repository `Mac-Neutron/DXMTRS`, and to
  push `main` to it. Continue only on a yes.
- [ ] **Step 2: Push and create.**
  `git -C build/wine-arm64-src/dxmt push origin 89558c1:refs/heads/main`;
  `gh repo create Mac-Neutron/DXMTRS --public --description "DXMT ported to Rust, with mtld3d: Direct3D 9-12 on Metal"`;
  `git -C build/wine-arm64-src/dxmt remote set-url origin https://github.com/Mac-Neutron/DXMTRS.git`;
  `git -C build/wine-arm64-src/dxmt push origin main`.
  Expected: both pushes report `main -> main`; `gh repo view Mac-Neutron/DXMTRS --json defaultBranchRef -q .defaultBranchRef.name` prints `main`.
- [ ] **Step 3: DXMTRS serves the pin by commit**, as `fetch_dxmt` fetches it. In a new scratchpad folder:
  `. dxmt/pins; git init -q t && git -C t fetch -q --depth 1 https://github.com/Mac-Neutron/DXMTRS.git "$DXMT_COMMIT" && echo fetched`.
  Expected: `fetched`.
- [ ] **Step 4: Point the pins at DXMTRS.** `DXMT_REPO=https://github.com/Mac-Neutron/DXMTRS.git`; the comment above
  it says DXMT_COMMIT is a commit of DXMTRS's `main`. Then `grep -rn 'Mac-Neutron/dxmt' --exclude-dir=build
  --exclude-dir=docs --exclude-dir=.git --exclude-dir=.claude .` and update each hit that names where shipped DXMT
  source lives (source offers, notices); history in `docs/` stays as written.
- [ ] **Step 5: Verify.** Step 3's command with `"$DXMT_REPO"` in place of the URL, after sourcing the edited pins.
  Expected: `fetched`. (No build here: the commit is unchanged; Task 2 builds.)
- [ ] **Step 6: Commit** on `dxmtrs`: `dxmt/pins` and Step 4's files, message
  `DXMT: build from Mac-Neutron/DXMTRS (same commit)`. Add a DXMTRS line to `build/forks/LEDGER.md` (not tracked).

### Task 2: E1 — a Rust static library in every DXMT binary

**Files (DXMTRS branch `phase0`, from `main`):**
- Create: `mtld3d/windows/dxmt-pe/Cargo.toml`, `mtld3d/windows/dxmt-pe/src/lib.rs` (crate `dxmt-pe`, `staticlib`)
- Create: `mtld3d/unix/dxmt-unix/Cargo.toml`, `mtld3d/unix/dxmt-unix/src/lib.rs` (crate `dxmt-unix`, `staticlib`)
- Modify: `mtld3d/windows/Cargo.toml`, `mtld3d/unix/Cargo.toml` (members), both `Cargo.lock`
- Create: `tools/cargo-staticlib.sh`; `src/util/rust_probe.c`
- Modify: `meson_options.txt` (`rust_toolchain`), `meson.build` and the `meson.build` of `src/d3d10`, `src/d3d11`,
  `src/d3d12`, `src/dxgi`, `src/nvapi`, `src/nvngx`, `src/winemetal`, `src/winemetal/unix`
**Files (MacNeutron branch `dxmtrs`):** `wine-arm64/build.sh:523-537`, `eac-vm/dxmt/build.sh:38-80`,
`eac-vm/dxmt-native/build.sh` (their meson setup and compile lines).

**Interfaces:**
- Produces: C ABI `uint32_t dxmtrs_probe(void)` in both crates: returns `0x44584d54` ("DXMT"), or `0` when the
  environment variable `DXMTRS_PROBE_FAIL` is set (read through `std::env::var_os`, so std is linked for real).
  `src/util/rust_probe.c`: a `__attribute__((constructor))` function calling `dxmtrs_probe()` and `abort()`ing when
  it isn't `0x44584d54`; compiled into every DLL, `winemetal.so` and every native dylib (not into `util`'s library,
  where nothing would reference it).
  `tools/cargo-staticlib.sh <toolchain> <rust-target> <crate> <cargo-target-dir> <out-file>`: runs
  `cargo +<toolchain> build --profile production --target <rust-target> -p <crate>` in the crate's workspace and
  copies the library (`<crate>.lib` for `*-windows-msvc`, `lib<crate>.a` otherwise; hyphens as cargo writes them) to
  `<out-file>`. Meson option `rust_toolchain` (string, default `1.99.0`).
- Rust targets per build: Mac (`build-arm64ec.txt`, `-marm64x`): `aarch64-pc-windows-msvc` and
  `arm64ec-pc-windows-msvc`, both linked into each DLL; `winemetal.so` and the native dylibs: `aarch64-apple-darwin`
  (`dxmt-unix` for `winemetal.so`, `dxmt-pe` for the native D3D dylibs); guest: `x86_64-pc-windows-msvc`,
  `i686-pc-windows-msvc`.

- [ ] **Step 1: The failing build.** `git -C build/wine-arm64-src/dxmt switch -c phase0`. Before any change, save
  the guest DLLs' import lists (Step 6's baseline): `llvm-objdump -p` on each DLL in
  `build/eac-vm-work/dxmt-guest/{x86_64,i386}-windows` into `build/phase0/e1-imports-before.txt` (build them first
  with `sh eac-vm/dxmt/build.sh` if absent). Then add `src/util/rust_probe.c` to every binary's sources, without any Rust.
  Run: `make wine-arm64 > build/phase0/e1-red.log 2>&1; echo $?`
  Expected: nonzero, and `build/wine-arm64-src/dxmt.log` shows `undefined symbol: dxmtrs_probe`.
- [ ] **Step 2: The crates and the plumbing.** Write both crates, `tools/cargo-staticlib.sh`, the meson option and one
  `custom_target` per Rust target the build needs, linked into each binary (`link_with` the custom target outputs).
  Link the Rust std's Windows imports with the same libraries mtld3d's PE link names (`dxmt/mtld3d-build.sh:81`:
  `ws2_32 userenv bcrypt ntdll` and friends) only where a link error asks for them. MacNeutron's three build scripts
  pass `-Drust_toolchain=$MTLD3D_RUST` and put `$(sh "$ROOT/dxmt/mtld3d-tools.sh")` on `PATH` for meson setup and
  compile.
- [ ] **Step 3: Mac build and both check lanes.** Run: `make wine-arm64 && make dxmt-check > build/phase0/e1-check.log 2>&1; echo $?`
  Expected: `0`; no `FAIL` lines in either lane's output.
- [ ] **Step 4: The probe really runs (mutation).** Run one x64 test program as `dxmt/check.sh` runs `d3d12_null`,
  with `DXMTRS_PROBE_FAIL=1` exported. Expected: the program fails to start (DLL initialization fails or aborts),
  where without the variable it prints its usual output.
- [ ] **Step 5: Both ARM64X halves.** For `d3d12.dll`: `llvm-objdump --syms` (llvm-mingw's) on the aarch64 and the
  arm64ec halves. Expected: `dxmtrs_probe` defined in each (`#dxmtrs_probe` / `dxmtrs_probe` per arm64ec mangling).
- [ ] **Step 6: Native and guest builds.** Run: `sh eac-vm/dxmt-native/build.sh && sh eac-vm/dxmt-native/build-tests.sh`
  Expected: `tests: ok`. Run: `sh eac-vm/dxmt/build.sh` (needs `MACNEUTRON_SIGN_IDENTITY`). Expected: exit 0. Then
  the same `llvm-objdump -p` listing into `build/phase0/e1-imports-after.txt`, diffed against Step 1's. Expected: no
  new DLL imported, or only ones the guest Wine has (`ws2_32`, `bcrypt`, `userenv`, `ntdll`); list them in the result.
- [ ] **Step 7: Rust lint.** `cargo +1.99.0 clippy --profile production -p dxmt-pe --target aarch64-pc-windows-msvc -- -D warnings`
  in `mtld3d/windows`, and the same for `dxmt-unix` with `aarch64-apple-darwin` in `mtld3d/unix`. Expected: no warnings.
- [ ] **Step 8: Commit** on DXMTRS `phase0`: `phase0 E1: a Rust static library in every DXMT binary`; on MacNeutron
  `dxmtrs`: `DXMT builds: Rust toolchain for DXMTRS's meson (phase 0)`. Record in the ledger: the libraries the link
  needed, link errors met, and the binary size change of `d3d12.dll` (`stat -f %z`, before/after).

### Task 3: E2 tools — metallibs out of dxil-translate, LLVM 23.1.4, air-roundtrip

**Files:**
- Modify (MacNeutron): `dxmt/tools/dxil-translate.mm` (`--out`, `--use`)
- Create (DXMTRS `phase0`): `phase0/air-roundtrip/air-roundtrip.cpp`, `phase0/air-roundtrip/build.sh`
- Build output: `build/phase0/llvm-23` (install), `build/phase0/air-roundtrip-23`, `build/phase0/air-roundtrip-15`

**Interfaces:**
- Produces: `dxil-translate --out <dir> <folder>` writes `<dir>/<input file name>.metallib` for every shader it
  translates; `dxil-translate --use <dir> <folder>` builds each shader's pipelines from `<dir>/<input file name>.metallib`
  when that file exists (reflection still from the translator), else from the translator's output.
  `air-roundtrip extract <in.metallib> <out.bc>`: writes the one module's bitcode.
  `air-roundtrip rewrite <in.metallib> <out.metallib>`: reads the module with the LLVM it is built against, verifies
  it, and writes it back through airconv's `MetallibWriter` (`src/airconv/metallib_writer.cpp`, compiled into the
  tool). Built against LLVM 23.1.4 it writes opaque pointers; built against LLVM 15 (`build/wine-arm64-src/llvm-arm64`)
  it calls `LLVMContext::setOpaquePointers(true)` first, so it writes opaque pointers with LLVM 15's records.
  `metallib_writer.cpp` is compiled unchanged where it builds; where LLVM 23's API differs, an adapted copy lives in
  `phase0/air-roundtrip/`.
  Exit 0 on success; on failure, the LLVM error on stderr and exit 1.

- [ ] **Step 1: The failing tool test.** Run: `build/wine-arm64/dxil-translate --out build/phase0/air-15 dxmt/tests/shaders`
  Expected: exits nonzero and writes no `.metallib` (the option doesn't exist yet).
- [ ] **Step 2: Implement `--out` and `--use`** in `dxil-translate.mm`; rebuild with `make wine-arm64`.
- [ ] **Step 3: Verify.** Run `--out build/phase0/air-15` on `dxmt/tests/shaders`. Expected: one `.metallib` per
  `ok` line. Run `--use build/phase0/air-15` on the same folder. Expected: the same summary and the same `air=` per
  shader as a plain run (`python3 dxmt/tools/corpus-diff.py plain.txt use.txt` → `0 new failures, 0 fixed, 0 changed`).
  Mutation: truncate one metallib to 100 bytes and rerun `--use`. Expected: that shader's line is `fail`. Restore it.
- [ ] **Step 4: LLVM 23.1.4.** `git -C build/forks/llvm-project worktree add build/phase0/llvm-23-src llvmorg-23.1.4`
  (absolute path), then cmake as `dxmt/llvm.sh` does (`-DLLVM_TARGETS_TO_BUILD=""`, Release, arm64) but with
  `-DLLVM_BUILD_TOOLS=On`, installing into `build/phase0/llvm-23`; run it in the background.
  Expected: `build/phase0/llvm-23/bin/llvm-dis --version` prints `LLVM version 23.1.4`.
- [ ] **Step 5: air-roundtrip, test first.** `phase0/air-roundtrip/build.sh <llvm-prefix> <out>` builds the tool.
  Test (a shell check in the same folder, `check.sh`): for `compute.cs.dxil.metallib` from Step 3,
  `air-roundtrip-23 rewrite` then `extract` then `llvm-dis` (23). Expected: the IR has `ptr addrspace(1)` and no
  `addrspace(1)*`; `air-roundtrip-15 rewrite` then `extract` then `llvm-dis` (23) likewise has `ptr addrspace(`; the
  original's `extract` + `llvm-dis` has `addrspace(1)*`. Run it before the tool exists (fails: no binary), then write
  the tool and run it again (passes).
- [ ] **Step 6: Commit** on MacNeutron `dxmtrs`: `dxil-translate: --out writes each metallib, --use builds from them`;
  on DXMTRS `phase0`: `phase0 E2: air-roundtrip`.

### Task 4: E2 — does Metal load latest-LLVM AIR?

**Files:**
- Modify (DXMTRS `phase0`): `src/winemetal/unix/winemetal_unix.c` (`thunk_SM50GetCompiledBitcode`, 64-bit only)
- Create (DXMTRS `phase0`): `phase0/llvm-c-api.md`
- Outputs: `build/phase0/e2/*.txt`

**Interfaces:**
- Consumes: Task 3's `dxil-translate --out/--use`, `air-roundtrip-23`, `air-roundtrip-15`.
- Produces: with `DXMT_PHASE0_ROUNDTRIP=<air-roundtrip path>` and `DXMT_PHASE0_LOG=<file>` set, each translated
  shader's metallib is written to a temporary file, rewritten by the tool, and the result returned in place of the
  original (a buffer the hook owns until `SM50DestroyBitcode` for that bitcode); one line per shader appended to the log:
  `roundtrip ok <in-bytes> <out-bytes>` or `roundtrip fail <status>` (the original is then returned). Without
  `DXMT_PHASE0_ROUNDTRIP` nothing changes.

- [ ] **Step 1: Offline, all test shaders.** `air-roundtrip-23 rewrite` every file of `build/phase0/air-15` into
  `build/phase0/air-23`, then `dxil-translate --use build/phase0/air-23 dxmt/tests/shaders > build/phase0/e2/use-23.txt`.
  Expected: a summary line; record `<ok>/<total>` against the plain run's, and every rewrite failure.
- [ ] **Step 2: Name the cause (only if Step 1 has failures).** Same with `air-roundtrip-15` into `air-15o`. If
  `air-15o` builds where `air-23` fails, the refusal is LLVM 23's newer records: run `llvm-bcanalyzer -dump` on one
  failing module and record which records are new. If `air-15o` fails too, Metal refuses opaque pointers: build
  `llvmorg-16.0.6` as Task 3 Step 4 did, build `air-roundtrip-16` with `setOpaquePointers(false)`, and confirm Metal
  loads its output (the spec's fallback row).
- [ ] **Step 3: The runtime hook, test first.** Run `d3d12_dxil_exec` as `dxmt/check.sh` does, with both variables set
  and a fresh `DXMT_SHADER_CACHE_PATH`. Expected before the hook: the log file is absent. Write the hook; rerun.
  Expected: the log has `roundtrip ok` lines and the program's output is unchanged.
- [ ] **Step 4: The check suite on round-tripped shaders.** `make wine-arm64`, then
  `DXMT_PHASE0_ROUNDTRIP=$PWD/build/phase0/air-roundtrip-23 DXMT_PHASE0_LOG=$PWD/build/phase0/e2/runtime.log DXMT_SHADER_CACHE_PATH=$(mktemp -d) sh dxmt/check.sh > build/phase0/e2/check-23.txt 2>&1`
  (the x64 lane). Expected: the same `ok`/`FAIL` rows as Task 2 Step 3's x64 lane; `grep -c 'roundtrip ok'` of the log
  greater than 0; `grep -c 'roundtrip fail'` recorded.
- [ ] **Step 5: The hook is inert.** Rerun `sh dxmt/check.sh` without the two variables. Expected: as Task 2 Step 3.
- [ ] **Step 6: The game corpus.** If `~/dxil-smite2` holds `.dxil` files, run Steps 1 and 2 on it (outputs under
  `build/phase0/e2/corpus-*`, never committed). If not, ask the maintainer whether to capture one: a SMITE 2 session of
  theirs with `DXMT_DXIL_DUMP=~/dxil-smite2` in its launch options (`/usr/bin/env` first). Record either way.
- [ ] **Step 7: LLVM C API inventory.** In `phase0/llvm-c-api.md`, every LLVM C++ API `src/airconv` uses (by class and
  method, from `grep -rhoE 'llvm::[A-Za-z_:]+|Builder\.[A-Za-z]+|IRB\.[A-Za-z]+'` and the includes), each marked
  `C API: <function>` or `shim`. Expected: every row marked; the `shim` rows counted.
- [ ] **Step 8: Commit** on DXMTRS `phase0`: `phase0 E2: round-trip hook and LLVM C API inventory`.

### Task 5: E3 — AIR against MSL

**Files:**
- Create (DXMTRS `phase0`): `phase0/air-vs-msl/air-vs-msl.mm`, `phase0/air-vs-msl/build.sh`,
  `phase0/air-vs-msl/shaders.sh`
- Outputs: `build/phase0/e3/`

**Interfaces:**
- Consumes: Task 3's `--out` metallibs; DXC as `dxmt/tests/shaders/compile.sh` runs it.
- Produces: `shaders.sh` writes, for each of the 19 `dxmt/tests/shaders/*.hlsl`, each entry point's SPIR-V
  (`dxc -spirv`, the profile and entry `compile.sh` uses) and MSL (`spirv-cross --msl --msl-version 30200`) into
  `build/phase0/e3/msl`. `air-vs-msl <air-dir> <msl-dir>` prints one line per pipeline,
  `<name> air <lib-ms> <pso-ms> msl <lib-ms> <pso-ms>`, then `total air <ms> msl <ms>`: compute shaders alone; vertex
  and pixel shaders of one `.hlsl` as one render pipeline (the pair a draw uses); skipped pipelines named with why.
  `air-vs-msl --gpu <air-dir> <msl-dir>` prints `<name> gpu air <ms> msl <ms>` for compute shaders whose only resources
  are buffers: the median of 20 timed dispatches (`GPUEndTime - GPUStartTime`) sized to at least 5 ms; AIR bound
  through the argument layout `SM50GetArgumentsInfo` reports, written as DXMT's D3D11 compute path writes it; MSL
  bound by `spirv-cross --reflect`'s indices.

- [ ] **Step 1: STOP — ask the maintainer** to fetch SPIRV-Cross (github.com/KhronosGroup/SPIRV-Cross, its latest
  release tag, Apache-2.0, development only, never shipped) into `build/phase0/spirv-cross` and build its CLI with
  cmake. Continue only on a yes. If DXC's Windows build refuses `-spirv`, stop and report: E3 then needs another
  HLSL-to-SPIR-V path, the maintainer's call.
- [ ] **Step 2: Shaders.** Run `shaders.sh`. Expected: an `.spv` and a `.metal` per entry point; record failures.
- [ ] **Step 3: The harness, test first.** Run `air-vs-msl` on a folder holding one deliberately broken `.metal`
  (a syntax error). Expected before the harness exists: no binary; after: that pipeline is listed as skipped with
  Metal's error, and the others time.
- [ ] **Step 4: Cold-cache check and the latency run.** Copy the built harness to two new names under
  `build/phase0/e3/` and run each once on the real folders. Expected: the two `total` lines within 20% of each other
  (else Metal's shader cache answered and the run is void: record it and stop). Keep the first run's output as the
  result.
- [ ] **Step 5: GPU time.** `air-vs-msl --gpu` once. Expected: a line per eligible compute shader; record which
  shaders were eligible.
- [ ] **Step 6: Commit** on DXMTRS `phase0`: `phase0 E3: AIR against MSL harness`.

### Task 6: Results and the translator decision

**Files:** Modify (MacNeutron): `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§4: a "Results" paragraph per
experiment and the decided row); memory `dxmtrs.md` (new) and `MEMORY.md`.

- [ ] **Step 1: Write the results** into spec §4: E1 (builds, lanes, imports added, size change), E2 (`ok/total` per
  writer, the named refusal if any, runtime rows, corpus or its absence, shim count), E3 (latency totals, GPU lines,
  shader counts and what was skipped).
- [ ] **Step 2: Decide by the spec's table.** Rows 1 and 2: write the translator's LLVM version and binding into §4 and
  §5 step 4. Row 3 (MSL faster or AIR's GPU time worse): write the numbers and stop for the maintainer.
- [ ] **Step 3: Memory.** `dxmtrs.md`: repository, branches (`phase0`, `dxmtrs`), Phase 0 outcome, the rulings above,
  what step 1's plan starts with. One index line in `MEMORY.md`.
- [ ] **Step 4: Commit** on `dxmtrs`: `Spec: DXMTRS Phase 0 results`.
