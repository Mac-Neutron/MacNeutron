# Direct3D 9 through mtld3d, M1: the macOS path — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 64-bit Direct3D 9 program on MacNeutron's macOS path renders through mtld3d on Metal, mtld3d living as a
subtree of our DXMT fork and built and bundled with our arm64 Wine.

**Architecture:** mtld3d 0.14.1 comes into the DXMT fork tree as one squashed subtree commit (exported as DXMT patch
0095). `wine-arm64/build.sh` builds its ARM64X PE pair (`d3d9.dll`, `mtld3d.dll`) and its `aarch64-apple-darwin`
`mtld3d.so` with mtld3d's own Makefile against an installed copy of our Wine; `wine-arm64/bundle.sh` puts them into
`wine.app` as builtins, over Wine's `d3d9.dll`. Our Wine is `--enable-archs=arm64ec,aarch64` (no i386, no x86_64
Windows side), so M1 builds only what the macOS path loads: an x64 game loads the ARM64X pair; 32-bit stays refused.

**Tech Stack:** Rust 1.99.0 (rustup) with `aarch64-pc-windows-msvc` and `arm64ec-pc-windows-msvc`, mtld3d's Make,
llvm-mingw 20261006 (ours, LLD 23.1.3), POSIX sh, C++ (one test program).

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-d3d9-mtld3d-design.md` (§4 M1, §6 gates).

## Global Constraints

- mtld3d release 0.14.1, commit `0c17212`, github.com/athei/mtld3d, zlib licence (its `LICENSE` stays in `mtld3d/` and
  ships with `wine.app`'s licences).
- The DXMT fork tree is `build/wine-arm64-src/dxmt`, branch `macneutron`; its commits leave as patches in
  `wine-arm64/patches/dxmt` (`format-patch --zero-commit -N`, numbered after 0094).
- Rust: 1.99.0 (mtld3d's CI `RUST_STABLE`), `--profile minimal --component llvm-tools`; no nightly, no xwin, no
  nextest, nothing written to `~/.cargo/bin` (the `lld-link`/`llvm-lib` names go in a tool folder under `build/`).
- Downloads (rustup's toolchain and targets) wait for the maintainer's yes; `xwin` (Microsoft's CRT and SDK, 3 GB,
  `sudo`) only after a ruling that llvm-mingw's headers can't build it and another yes.
- No change to the launcher's overrides: the DXMT backend's `d3d9=b` already takes the builtin.
- Never push or publish; commit locally.

## Departures from the spec (rulings to ledger at the start)

- §1/§4 say the i386 pair ships in M1: our macOS Wine has no i386 Windows side (`--enable-archs=arm64ec,aarch64`) and
  macOS refuses 32-bit games, so M1 builds the ARM64X pair only; the i686 and x86_64 pairs come with M2, which needs
  them in the VM.
- §3 says the subtree keeps history: `--squash` instead, because the patch series exports every non-merge commit and
  upstream has 699; the squash message names `0c17212`, and `git subtree pull --squash` takes later releases.
- §6 names mtld3d's `make test` (its end-to-end suite): its test programs build for the `-msvc` targets against
  Microsoft's SDK (`xwin`), even on the arm64 leg. M1 runs its unit tests, its D3D9 conformance leg for the ARM64X build
  and our own D3D9 program instead; the end-to-end suite waits for a yes to `xwin`.

## Review Focus

- A prefix made before this `wine.app` (no `mtld3d.dll` marker in `system32`): after the launcher's prefix update its
  D3D9 must still load mtld3d, not fail to find `mtld3d.dll` — Task 4's check runs in such a prefix too.
- The release bundle (`bundle.sh --release`): mtld3d's files are copied before it strips import libraries and Wine's
  developer tools, `mtld3d.so` is signed with the rest, and `d3d9.dll`/`mtld3d.dll` keep the builtin marker — Task 3's
  assertions run in both modes.
- A 64-bit game's D3D9 runs mtld3d's ARM64EC half under FEX (the x64 test program exercises it, not an arm64 one).
- Wine's own `d3d9` (wined3d) is no longer reachable on the macOS path: a D3D9 program that worked there before and
  breaks now is a regression to note, not hide (the spec has no fallback in M1).
- mtld3d's fixed Makefile rules still hold for a wine-build Wine (`compatdb.so` present): Task 2's change only adds the
  stock layout.

---

### Task 1: Toolchain and the import

**Files:**
- Modify: the DXMT fork tree `build/wine-arm64-src/dxmt` (one squashed subtree commit and its merge)
- Create: `wine-arm64/patches/dxmt/0095-mtld3d-import-0.14.1.patch` (the export of that commit)
- Create: `dxmt/mtld3d-tools.sh` (prints the tool folder: `lld-link` and `llvm-lib` links onto the toolchain's
  `rust-lld` and `llvm-ar`, made once under `build/mtld3d-tools/bin`)

**Interfaces:**
- Produces: `$D/mtld3d/` (mtld3d 0.14.1's tree in the fork, `D=build/wine-arm64-src/dxmt`); `dxmt/mtld3d-tools.sh`
  printing an absolute folder for `PATH`.

- [ ] Step 1: with the maintainer's yes, `rustup toolchain install 1.99.0 --profile minimal --component llvm-tools` and
  `rustup target add --toolchain 1.99.0 aarch64-pc-windows-msvc arm64ec-pc-windows-msvc`.
  Expected: `rustup target list --installed --toolchain 1.99.0` lists both and `aarch64-apple-darwin`.
- [ ] Step 2: in `$D`, `git subtree add --squash --prefix=mtld3d https://github.com/athei/mtld3d.git 0c17212`
  (`--squash`: the series can't carry upstream's 699 commits; the squash message names `0c17212`).
  Expected: `$D/mtld3d/LICENSE` is the zlib text; `git -C $D log -2 --oneline` shows the squash and its merge.
- [ ] Step 3: export the squash commit as `0095-…` with the series' flags (`format-patch --zero-commit -N
  --binary --start-number 95`; merges aren't exported). Prove the series reproduces the tree: a scratch worktree of
  the fork at `dxmt/pins`' `DXMT_COMMIT`, `git am` 0001-0095, its `HEAD^{tree}` equal to `$D`'s.
  Expected: equal trees.
- [ ] Step 4: `dxmt/mtld3d-tools.sh`. Expected: `$(sh dxmt/mtld3d-tools.sh)/lld-link --version` prints LLD and
  `$(sh dxmt/mtld3d-tools.sh)/llvm-lib` exists.
- [ ] Step 5: commit (worktree: the patch and the script) —
  `git commit -m "dxmt 0095: mtld3d 0.14.1 (Direct3D 9 on Metal, zlib) as a subtree of the DXMT fork"`

### Task 2: mtld3d builds against our Wine

**Files:**
- Modify: `$D/mtld3d/Makefile` (a fork commit, exported as 0096): the arm64 mode takes a stock arm64 Wine as well
  as wine-build's
- Create: `dxmt/mtld3d-build.sh` (`mtld3d-build.sh <WINE_SDK>`: builds the ARM64X pair and `mtld3d.so`; prints the
  two output folders)

**Interfaces:**
- Consumes: Task 1's `$D/mtld3d`, `dxmt/mtld3d-tools.sh`, `sh dxmt/toolchain.sh` (our llvm-mingw `bin`).
- Produces: `dxmt/mtld3d-build.sh <sdk>`: runs `make -C $D/mtld3d windows-arm64x unix-arm64 WINE_SDK=<sdk>
  LLVM_MINGW=<llvm-mingw root> ARM64X_LLD=<llvm-mingw>/bin/lld-link RUST_STABLE=1.99.0` with the tool folder first in
  `PATH`; outputs `$D/mtld3d/windows/target/arm64x/production/{d3d9,mtld3d}.dll` and
  `$D/mtld3d/unix/target/aarch64-apple-darwin/production/mtld3d.so`.

- [ ] Step 1: the RED: `make -C $D/mtld3d windows-arm64x WINE_SDK=build/wine-arm64/wine.app/Contents/Resources …`.
  Expected: exit 2, "holds no lib/wine/aarch64-unix/compatdb.so" (our Wine has neither `compatdb.so`, the
  `lib/wine/d3d9/mtld3d` tree, nor the i386/x86_64 link archives `WINE_SDK_FILES_arm64` lists).
- [ ] Step 2: the Makefile change: in the arm64 mode, a Wine without `lib/wine/aarch64-unix/compatdb.so` is a stock arm64
  Wine whose own `lib/wine` takes mtld3d: `WINE_SDK_REQUIRE` asks it only for the three tools and
  `lib/wine/aarch64-windows/{libwinecrt0.a,libntdll.a}`, and not for the tree (`MTLD3D_TREE` already falls back to
  `lib/wine`). A wine-build Wine (with `compatdb.so`) keeps today's checks. One commentary line per rule, in the file's
  own style.
- [ ] Step 3: `dxmt/mtld3d-build.sh`. The C and C++ of mtld3d's build scripts (snmalloc-sys) take llvm-mingw's headers
  instead of `/opt/xwin`'s: `CFLAGS_<target>`/`CXXFLAGS_<target>` in the environment for both targets (cargo's `[env]`
  doesn't override them), the C++ flags keeping the config's own (`-fno-exceptions -fno-rtti -fno-threadsafe-statics
  -DSNMALLOC_USE_THREAD_CLEANUP` and the `ARM64_SYSREG` define) with `-idirafter <llvm-mingw>/aarch64-w64-mingw32/include`
  and `-isystem <llvm-mingw>/aarch64-w64-mingw32/include/c++/v1` (C: `-idirafter
  <llvm-mingw>/aarch64-w64-mingw32/include` only), `<llvm-mingw>` being `$(dirname $(sh dxmt/toolchain.sh))`. If they
  can't compile it, that is a ruling (xwin, after the maintainer's yes, into
  `build/xwin` with the paths overridden the same way), not a silent fallback.
- [ ] Step 4: run it against the dev `wine.app`'s `Contents/Resources`. Expected: exit 0; `file` says both DLLs are
  PE32+ "Aarch64" with an ARM64EC half (`llvm-readobj --coff-load-config` or the Makefile's own
  `HYBRID_PLACEHOLDER_CHECK` passing), `mtld3d.dll` carries Wine's builtin marker, `mtld3d.so` is Mach-O arm64.
- [ ] Step 5: unit tests on the host: `cargo +1.99.0 test -p mtld3d-core -p mtld3d-types --features
  mtld3d-core/disk-cache --target aarch64-apple-darwin` in `windows/` and `cargo +1.99.0 test` in `unix/` (nextest's
  runner isn't installed; a test that needs its process isolation is a ruling). Expected: pass.
- [ ] Step 6: commit the fork change (export 0096) and the script —
  `git commit -m "dxmt 0096, mtld3d-build.sh: mtld3d's ARM64X pair and mtld3d.so against our arm64 Wine"`

### Task 3: Built and bundled with wine.app

**Files:**
- Modify: `wine-arm64/build.sh` (after the DXMT step: an installed copy of our Wine as the SDK, `make -C wine-build
  install DESTDIR=$B/wine-arm64-src/wine-sdk`, then `dxmt/mtld3d-build.sh $B/wine-arm64-src/wine-sdk/usr/local`)
- Modify: `wine-arm64/bundle.sh` (step 1, beside DXMT, before signing and before the release strip: the pair into
  `$R/lib/wine/aarch64-windows/` over Wine's `d3d9.dll`, `winebuild --builtin` on `d3d9.dll`; `mtld3d.so` into
  `$R/lib/wine/aarch64-unix/`; `mtld3d/LICENSE` into the licences folder as `mtld3d/LICENSE`; assertions beside DXMT's)

**Interfaces:**
- Consumes: Task 2's outputs and script.
- Produces: `wine.app` with `lib/wine/aarch64-windows/{d3d9,mtld3d}.dll` (mtld3d's, builtin-marked, ARM64X) and
  `lib/wine/aarch64-unix/mtld3d.so` (signed).

- [ ] Step 1: the assertions first, in `bundle.sh`'s check block: `d3d9.dll` and `mtld3d.dll` in
  `lib/wine/aarch64-windows` are mtld3d's (`llvm-readobj --coff-imports d3d9.dll` lists `mtld3d.dll`, which Wine's
  own `d3d9.dll` never imports), carry the builtin marker and are ARM64X; `mtld3d.so` exists and
  `codesign --verify` passes; `licenses/mtld3d/LICENSE` exists. Run `make wine-arm64` before the copy exists.
  Expected: the bundle dies on the first assertion.
- [ ] Step 2: the `build.sh` and `bundle.sh` changes. Expected: `make wine-arm64` exits 0 and the assertions hold;
  `bundle.sh --release --version 0.0.0-test --out <scratch>` (the release path) holds them too.
- [ ] Step 3: commit — `git commit -m "wine-arm64: mtld3d's Direct3D 9 in wine.app (built with the DXMT step, bundled as Wine's d3d9)"`

### Task 4: A D3D9 program renders through mtld3d

**Files:**
- Create: `dxmt/tests/d3d9_triangle.cpp` (x64)
- Modify: `Makefile` (`DXMT_TESTS` takes `d3d9_*.cpp` too, linked `-ld3d9`), `dxmt/check.sh` (a row on "ours" only:
  D3DMetal, the reference, has no D3D9)

**Interfaces:**
- Consumes: Task 3's `wine.app`.

- [ ] Step 1: `d3d9_triangle.cpp`: a 256x256 window, `Direct3DCreate9`, a HAL device, `Clear` to (0,0,255), a
  fixed-function `D3DFVF_XYZRHW|D3DFVF_DIFFUSE` triangle in (255,0,0) covering the centre, `GetRenderTargetData`
  into a system-memory surface, `LockRect`. Prints exactly, each `ok` or `FAIL <what>`:
  `d3d9 module <ok|FAIL>` (`GetModuleHandleW(L"mtld3d.dll")` non-null after `Direct3DCreate9`),
  `d3d9 clear <ok|FAIL>` (corner pixel 0x0000ff), `d3d9 triangle <ok|FAIL>` (centre pixel 0xff0000),
  `d3d9 present <ok|FAIL>` (`Present` returns `D3D_OK` 10 times).
- [ ] Step 2: the check row: `d3d9_triangle` on "ours", every line `ok`; also run in a prefix created by the previous
  `wine.app` and updated by the launcher's prefix update (Review Focus 1). RED: the same row on a `wine.app` from
  before Task 3 (Wine's wined3d `d3d9`). Expected: `d3d9 module FAIL`.
- [ ] Step 3: GREEN on Task 3's `wine.app`. Expected: four `ok`s in both prefixes.
- [ ] Step 4: commit — `git commit -m "dxmt tests: d3d9_triangle (mtld3d on the macOS path), a check row on ours"`

### Task 5: mtld3d's conformance on our Wine

**Files:**
- Modify: `wine-arm64/winetests.sh` (`MODULES` gains `d3d9`: its x64 lane's `d3d9_test.exe`)
- Create: `dxmt/mtld3d-conformance.sh` (mtld3d's conformance runner, its `CONFORMANCE_RUN` with `--arch arm64x`,
  against `wine.app`'s `bin/wine` and `bin/wineserver`, in a prefix of its own under `build/`, the exe from the x64
  lane)

- [ ] Step 1: build the x64 `d3d9_test.exe` (`make wine-arm64-winetests` or the script). Expected: a PE32+ x86-64 exe.
- [ ] Step 2: run `dxmt/mtld3d-conformance.sh`. Expected: the runner's per-site summary against mtld3d's
  `baseline.txt`; every site at or better than the baseline, or each worse one named in the ledger with its reason
  (a difference of our Wine from wine-build's is a finding, not a pass).
- [ ] Step 3: MacNeutron's own gates unchanged: `make dxmt-check` (both lanes) and `make test`. Expected: as before.
- [ ] Step 4: commit — `git commit -m "dxmt: mtld3d's D3D9 conformance against our Wine (Wine's d3d9 tests, x64 lane)"`
