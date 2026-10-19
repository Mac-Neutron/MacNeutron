# MacNeutron performance build (X.Y.Zp) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make release VERSION=0.7.1 PROFILE=p` builds MacNeutron 0.7.1p — diagnostics compiled out, ThinLTO/-O3/CPU
tuning, measured runtime tuning and PGO on the VM path — in `build/p`, and it is published next to 0.7.1.

**Architecture:** One profile variable, `MACNEUTRON_PROFILE=p`, read by every build script, with `BUILD_DIR=build/p`
isolating every output (and `BUILDER_HOME=/home/bench/p` isolating the builder's). The launcher and the app tell a p
runtime by its version suffix (`0.7.1p`) and turn their runtime diagnostics off unless `MACNEUTRON_LOG` is set. PGO
profiles live in `pgo/` and are used whenever the profile is p.

**Tech Stack:** POSIX sh build scripts, Swift (MacNeutronCore, MacNeutronApp, XCTest), clang/Apple clang, meson
(DXMT), CMake (FEX), cargo (libkrun), Wine's configure, the Fedora builder VM.

**Spec:** `docs/superpowers/specs/2026-10-19-macneutron-performance-build-design.md`

## Global Constraints

- `MACNEUTRON_PROFILE` unset or `std` = today's behaviour, byte for byte where the build is reproducible. Nothing in the normal line changes.
- A p build writes only under `build/p/`, `build/release/X.Y.Zp/` and the builder's `/home/bench/p` and `/var/tmp/mn-image-p`.
- ThinLTO wherever the toolchain allows; never full LTO (maintainer, 2026-10-19).
- Kernel: never change `lockdown=integrity`, `module.sig_enforce=1` or the signed kernel.
- `FEX_MULTIBLOCK=0` stays in every build.
- Benches: once each, SMITE 2 practice, 2 minutes, idle Mac; each bench batch only with the maintainer's go-ahead (it starts the game on their Steam account; never while they play).
- Downloads (`rustup component add llvm-tools`, a new sysroot package) only after the maintainer's yes.
- Standing rules: never open MacNeutron.app or start/quit Steam; no push, tag or publish without a yes (0.7.1p's publish is approved, after §9's checkpoint); privacy scan before committing docs; commits end with the session's Co-Authored-By line.
- Wine trace compile-out: `-DWINE_NO_TRACE_MSGS` only (WARN/FIXME/ERR stay).
- Guest Wine: `-O3 -march=x86-64-v2`. Guest FEX: `-march=armv8.6-a+fp16+fp16fml+sha3+crypto` (the guest's `/proc/cpuinfo`, read 2026-10-19, without SME).
- Assets: `MacNeutron-X.Y.Zp.zip`, `MacNeutron-X.Y.Zp-source.tar.gz`, `MacNeutron-X.Y.Zp-vm-parts.tar.xz`, `SHA256SUMS`; manifest URL `https://github.com/chadouming/MacNeutron/releases/download/vX.Y.Zp/MacNeutron-X.Y.Zp-vm-parts.tar.xz`.

## Deviations from the spec (found while planning)

- §6 item 4 (ring spin, QoS) is dropped: the default forwarded D3D path runs on batched traps with no spin loop
  (`MN_EXECUTOR_SPIN_US` and the guest's 50 µs `spin_us` serve only `MACNEUTRON_VM_D3D=guest`), and every ring and
  executor thread already runs at `QOS_CLASS_USER_INTERACTIVE` (`host_ring.c:10`, `executor.c:184`).
- §3's builder isolation is done with one variable, `BUILDER_HOME`, honoured by `eac-vm/builder.sh` (ssh runs with
  `HOME=$BUILDER_HOME`; `sync` resolves relative guest paths under it), instead of renaming each builder folder.
- The p image build writes through a second builder share, `outp` at `/mnt/out-p`, so the builder keeps `out` for
  the normal images.

## Review Focus

1. A normal build after this work: `BUILD_DIR` unset must resolve every path exactly as before — a missed or doubled `${BUILD_DIR:-…}` sends a normal build into the wrong folder. Pinned by Task 2's grep check and by Task 2's normal-profile gate run.
2. A player switching 0.7.1 → 0.7.1p (and back): the VM folder already holds the other line's images and per-game state disks; setup must swap images and keep `state/`. Pinned by Task 10's switch run of `vm-setup.sh`.
3. `MACNEUTRON_LOG=1` on the p build must still produce a log a bug report can use (Wine warnings, guest console, DXMT warnings/errors). Pinned by Task 10's diagnostics runs.
4. A game outside a Steam library on the p build (Mac Wine path, traces compiled out) still runs. Pinned by `make release`'s R3 draw row on the p build (Task 10).
5. PGO profiles recorded from older sources: the p build must still compile (warnings silenced, never errors). Pinned by Task 8's step that builds with a profile whose stamp names an older commit.

---

### Task 1: The p profile and its version in release.sh and make

**Files:**
- Modify: `release/release.sh` (argument `case` at the main flow ~line 590; `check_version`; `OUT=`; the printed publish commands ~line 711; the parts manifest URL ~line 675)
- Modify: `Makefile:113-116` (`release:` target)
- Create: `release/p-seed.sh`

**Interfaces:**
- Produces: `release.sh --profile p X.Y.Z` (mode release) and `release.sh --rehearse --profile p X.Y.Z`; inside release.sh, `RV` = the release's version (`X.Y.Z` or `X.Y.Zp`), used for `OUT`, asset names, `CFBundleShortVersionString`/`CFBundleVersion`, the manifest URL and the printed commands. `check_version_p <repo> <X.Y.Z>`: returns 1 with `release: vX.Y.Z isn't a tag (a p release builds an existing release's version)` or `release: vX.Y.Zp is already a tag`. `make release VERSION=X.Y.Z PROFILE=p`. `release/p-seed.sh`: makes `build/p/` from `build/` with `cp -c -R` of `wine-arm64-src`, `dxmt-src`, `llvm-mingw-src`, `vm-setup-tools-src`, and `eac-vm-work/{libkrun-src,libtpms-src,wine-linux,fex-linux,gvproxy-v0.8.9,share}` (each skipped if already present); exports nothing.

- [ ] **Step 1: Write the failing self-test rows** in `self_test`, beside the existing version rows:
  `refused "a p release of a version that isn't a tag" "release: v0.0.9 isn't a tag (a p release builds an existing release's version)" check_version_p "$T/repo" 0.0.9`;
  `refused "a p release that is already a tag" "release: v0.0.0p is already a tag" check_version_p "$T/repo" 0.0.0` (after `git tag v0.0.0p` in the fixture repo);
  `passes "a p release of a tagged version" check_version_p "$T/repo" 0.0.0` (fixture with `v0.0.0` and no `v0.0.0p`);
  `exits "--profile with an unknown profile" 2 "$usage" --profile q 0.0.1`.
- [ ] **Step 2: Run** `sh release/release.sh --self-test`. Expected: the four new rows FAIL, the rest ok.
- [ ] **Step 3: Implement** `check_version_p`, the `--profile p` forms in the argument `case` (setting `PROFILE=p`, `RV="${V}p"`, `export MACNEUTRON_PROFILE=p BUILD_DIR="$ROOT/build/p" BUILDER_HOME=/home/bench/p`), and `RV` everywhere the version names an output. For a p release, `check_origin_tag` checks `$RV`; `check_version` is replaced by `check_version_p`. The printed `gh release create` gets `--latest=false` for a p release. `Makefile`'s `release:` passes `--profile $(PROFILE)` when `PROFILE` is set and runs `sh release/p-seed.sh` first.
- [ ] **Step 4: Run** `sh release/release.sh --self-test`. Expected: `PASS release self-test`.
- [ ] **Step 5: Commit** `release: a p profile (release.sh --profile p X.Y.Z → X.Y.Zp in build/p)`.

### Task 2: Every build script honours BUILD_DIR; the builder honours BUILDER_HOME

**Files:**
- Modify: every script `grep -l '\$ROOT/build' eac-vm dxmt wine-arm64 release` lists without `BUILD_DIR` (58 on 2026-10-19), replacing `$ROOT/build` with `${BUILD_DIR:-$ROOT/build}` — except `$ROOT/build/release`, which stays (releases always land in `build/release`).
- Modify: `eac-vm/builder.sh` (`ssh`, `sync`)
- Modify: callers that pass `/home/bench/<x>` to `builder.sh sync` (`eac-vm/image/guest-stages.sh`, `eac-vm/wine/sync.sh`, `eac-vm/dxmt/build.sh`, `eac-vm/metal/build-guest.sh`, `eac-vm/d3d/*.sh` that sync): pass `<x>` relative.
- Modify: `eac-vm/image/build.sh`: `OUT=${OUT:-/mnt/out${MACNEUTRON_PROFILE:+-$MACNEUTRON_PROFILE}}`, `WORK=${WORK:-/var/tmp/mn-image${MACNEUTRON_PROFILE:+-$MACNEUTRON_PROFILE}}`; with `MACNEUTRON_PROFILE=p`, mount `outp` at `/mnt/out-p` (`sudo mount -t virtiofs outp /mnt/out-p`) when it isn't mounted.
- Create: `release/tests/build-dir.sh`

**Interfaces:**
- Produces: `BUILDER_HOME` (default `/home/bench`): `builder.sh ssh <cmd>` runs `export HOME=$BUILDER_HOME MACNEUTRON_PROFILE=…; mkdir -p "$HOME"; cd "$HOME"; <cmd>`; `builder.sh sync <host dir> <guest dir>` resolves a relative `<guest dir>` under `$BUILDER_HOME`. `builder.sh start` with `VM_SHARES` that names `outp=…/build/p/eac-vm-work/images` beside `out=…`.
- `release/tests/build-dir.sh`: prints `build-dir: ok` or `build-dir: FAIL <file>...`.

- [ ] **Step 1: Write `release/tests/build-dir.sh`**: lists files under `eac-vm dxmt wine-arm64 release` matching `\$ROOT/build` but not `BUILD_DIR` and not only `\$ROOT/build/release`; FAIL naming them. Also FAILs on any `builder.sh sync` caller that passes an absolute `/home/bench/` guest path.
- [ ] **Step 2: Run** `sh release/tests/build-dir.sh`. Expected: FAIL listing the scripts.
- [ ] **Step 3: Make the replacements and the builder.sh/caller changes** above (mechanical; review each `sed` hunk).
- [ ] **Step 4: Run** `sh release/tests/build-dir.sh`. Expected: `build-dir: ok`. Then, normal profile, `sh eac-vm/tests/discard.sh` and `sh eac-vm/tests/d3d-smoke.sh`. Expected: `discard: ok`, `d3d-smoke: ok` (a normal build still finds everything in `build/`).
- [ ] **Step 5: Restart the builder with both shares** (`builder.sh stop`; `VM_SHARES="out=$PWD/build/eac-vm-work/images,outp=$PWD/build/p/eac-vm-work/images" builder.sh start`) and record it in memory `builder-out-share.md`.
- [ ] **Step 6: Commit** `build: every script honours BUILD_DIR; the builder BUILDER_HOME (the p profile's folders)`.

### Task 3: Wine — traces compiled out, no -g, guest flags

**Files:**
- Modify: `wine-arm64/build.sh:359-372` (Mac Wine flags), `wine-arm64/bundle.sh` (strip), `eac-vm/wine/build.sh` (`wine)` configure)
- Modify: `release/release.sh` (p-only check rows)

**Interfaces:**
- Produces: in `wine-arm64/lib.sh`, `P_FLAGS_DEFAULT="wine fex host krun dxmt"` and `p_flag <component>` (true when `MACNEUTRON_PROFILE=p` and the component is in `${MACNEUTRON_P_FLAGS:-$P_FLAGS_DEFAULT}`): a component not listed keeps today's optimisation flags under p (compile-outs and `-g` removal still apply). The five build scripts (`wine-arm64/build.sh`, `eac-vm/wine/build.sh` via its caller's environment, `eac-vm/libkrun/build.sh`, `eac-vm/host/build.sh`, `eac-vm/dxmt-native/build.sh`, `eac-vm/image/build-fex.sh`) source `wine-arm64/lib.sh` for it; builder-side scripts get `MACNEUTRON_P_FLAGS` exported by `builder.sh ssh`. `release.sh`'s `check_no_traces <file> <string>` and `check_no_debug_info <dir>`.

- [ ] **Step 1: Write the failing p rows in `self_test`**: `check_no_traces` refused on a fixture file containing the probe string, passes on one without; `check_no_debug_info` refused on a fixture Mach-O built with `-g` (`cc -g -c`), passes on one built without. Pick the probe string: a format string used only by a `TRACE(` in `dlls/ntdll/unix/virtual.c` of the applied Wine tree (`grep -n 'TRACE(' … | head`; confirm with `grep -c` that it appears in no `WARN`/`ERR`/`FIXME`), and record it as `WINE_TRACE_PROBE` in `release/lib.sh`.
- [ ] **Step 2: Run** `sh release/release.sh --self-test`. Expected: the new rows FAIL.
- [ ] **Step 3: Implement.** Mac Wine under p: `-g` dropped from all four CFLAGS variables, `-DWINE_NO_TRACE_MSGS` added to them; `bundle.sh --release` and release.sh's own pass over the app's Mach-Os (`release.sh:464`) strip with `strip -x` instead of `strip -S` under p (the OSO check after it stays). Guest Wine under p: `CFLAGS="-O3 -march=x86-64-v2 -DWINE_NO_TRACE_MSGS"` (plus `-flto=thin` and `LDFLAGS=-flto=thin` when `wine` is in `MACNEUTRON_P_FLAGS`), `x86_64_CFLAGS="-O3 -march=x86-64-v2 -DWINE_NO_TRACE_MSGS"`; `make install` followed by `llvm-strip --strip-all` over the stage's `.so` and `.dll`/`.exe`. `release.sh` under p runs `check_no_traces` on the release wine.app's `lib/wine/aarch64-unix/ntdll.so` and the parts' guest `ntdll.so`, and `check_no_debug_info` on the app and the unpacked parts.
- [ ] **Step 4: Run** `sh release/release.sh --self-test` (PASS), then `MACNEUTRON_PROFILE=p BUILD_DIR=$PWD/build/p sh wine-arm64/build.sh` and `strings build/p/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/ntdll.so | grep -cF "$WINE_TRACE_PROBE"`. Expected: `0` (and nonzero for `build/wine-arm64`'s).
- [ ] **Step 5: Commit** `wine: the p profile compiles TRACE out, drops -g, and builds the guest at -O3 x86-64-v2`.

### Task 4: DXMT — logging and capture compiled out, ThinLTO

**Files:**
- Create: `wine-arm64/patches/dxmt/00NN-perf-build-option.patch` (next free number; made in `build/wine-arm64-src/dxmt` and exported with `make wine-arm64-export`)
- Modify: `wine-arm64/build.sh:468-480` (Mac DXMT meson), `eac-vm/dxmt-native/build.sh:19-21`, `dxmt/pins` (the new fork commit)

**Interfaces:**
- Produces: DXMT meson option `perf_build` (boolean, default false) defining `DXMT_PERF_BUILD`. Under it: `Logger::trace`, `debug`, `info` are empty inline functions in `src/util/log/log.hpp`; the bodies of `src/d3d12/d3d12_stats.cpp` and `d3d12_dxil_dump.cpp` and their call sites (`d3d12_command_queue.cpp`, `d3d12_swapchain.cpp` F9 handling, `d3d12_command_encoder.hpp`, `d3d11_device.cpp`) are compiled out, `getenv("DXMT_STATS")`/`DXMT_DXIL_DUMP` never read.
- Consumes: `MACNEUTRON_P_FLAGS` (Task 3): `dxmt` listed → `-Db_lto=true -Db_lto_mode=thin`.

- [ ] **Step 1: Write the failing check**: `make dxmt-tests` builds; add to `release.sh` under p `check_no_traces` on native DXMT's `libd3d12.dylib` with a string passed only to `Logger::info` in `d3d12_device.cpp` (record it as `DXMT_INFO_PROBE` in `release/lib.sh`, confirmed info-only with `grep`).
- [ ] **Step 2: Run** `strings build/p/dxmt-native/install/lib/libd3d12.dylib | grep -cF "$DXMT_INFO_PROBE"` on a p build made before the change. Expected: nonzero (FAIL).
- [ ] **Step 3: Implement the patch**, commit it in the fork tree, export it, pass `-Dperf_build=true` (and the LTO options) in both DXMT builds under p. Under p, `MACNEUTRON_PERF=1` in the launcher is ignored (Task 7).
- [ ] **Step 4: Run** the p build of both DXMTs, the probe count (expected `0`), and `make dxmt-tests` + `sh eac-vm/tests/d3d-smoke.sh` with `BUILD_DIR=$PWD/build/p` (expected `d3d-smoke: ok`).
- [ ] **Step 5: Push the fork commit** to the fork's `macneutron` branch over SSH (release.sh's `check_dxmt` needs it published; covered by the publish yes) and update `dxmt/pins`.
- [ ] **Step 6: Commit** `dxmt: a perf_build option (logging below warn and the capture code compiled out) for the p profile`.

### Task 5: libkrun, the VM host and guest FEX — p flags

**Files:**
- Modify: `eac-vm/libkrun/build.sh`, `eac-vm/host/build.sh`, `eac-vm/image/build-fex.sh`

**Interfaces:**
- Consumes: `MACNEUTRON_P_FLAGS` (Task 3).

- [ ] **Step 1: Implement libkrun under p**: `CARGO_PROFILE_RELEASE_LTO=thin`, `CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1`, `-C target-cpu=apple-m5` appended (0x1f-separated) to `CARGO_ENCODED_RUSTFLAGS` when `krun` is listed; always `--features log/release_max_level_error` added under p.
- [ ] **Step 2: Implement the VM host under p**: every `clang`/`clang++` line `-O3 -mcpu=apple-m5 -flto=thin` (when `host` is listed), `swiftc -O -wmo`.
- [ ] **Step 3: Implement guest FEX under p**: `-DCMAKE_C_FLAGS=… -DCMAKE_CXX_FLAGS="-march=armv8.6-a+fp16+fp16fml+sha3+crypto"` when `fex` is listed; check `grep -o 'flto=[a-z]*' "$SRC/build/build.ninja" | sort -u` prints only `flto=thin`; if not, set `-DENABLE_LTO=OFF` and add `-flto=thin` to both flag variables.
- [ ] **Step 4: Verify**: under p, `otool -l build/p/eac-vm-work/libkrun/libkrun.1.dylib | grep -c __DWARF` is `0`, and `sh eac-vm/tests/discard.sh` and `steam-forward.sh` with `BUILD_DIR=$PWD/build/p` pass (after Task 6's images exist; until then, the host-side builds only).
- [ ] **Step 5: Commit** `eac-vm: the p profile's flags for libkrun (ThinLTO, apple-m5), the VM host (-O3, ThinLTO) and guest FEX`.

### Task 6: The p images — quiet kernel, builder isolation, guest stages under p

**Files:**
- Modify: `eac-vm/image/build.sh` (cmdline line ~137), `eac-vm/image/guest-stages.sh`

- [ ] **Step 1: Implement**: under p, `cmdline.txt` gets `quiet loglevel=0` plus `$MN_P_CMDLINE` (empty until Task 9 keeps candidates); guest stages and the image build run with `BUILDER_HOME=/home/bench/p` (Task 2) and write `build/p/eac-vm-work`.
- [ ] **Step 2: Build the p guest stages and images**: `MACNEUTRON_PROFILE=p BUILD_DIR=$PWD/build/p BUILDER_HOME=/home/bench/p sh eac-vm/image/guest-stages.sh wine forwarder dxmt d3d fex`, then the p image build. Add `check_untouched` to release.sh under p: it records `stat -f '%m %z'` of `build/wine-arm64/wine.app/Contents/Info.plist`, `build/eac-vm-work/images/manifest.sha256` and `build/eac-vm-work/images/base.erofs` when the release starts and refuses (`release: the p build changed <file>`) if any differs at the end; run the same comparison by hand around this step (Review Focus 1).
- [ ] **Step 3: Gate**: with `BUILD_DIR=$PWD/build/p`: `wine.sh` (all ok, `wine-11.20`), `discard.sh`, `d3d-smoke.sh`, `steam-forward.sh` (copy the p `guest-client` into `build/p/eac-vm-work/share/bin` first).
- [ ] **Step 4: Commit** `eac-vm image: the p images (quiet kernel, built under build/p and the builder's p home)`.

### Task 7: Runtime diagnostics off in a p runtime

**Files:**
- Modify: `Sources/MacNeutronCore/ToolLayout.swift`, `LauncherLog.swift`, `Launcher.swift`, `MicroVM.swift`; `Sources/MacNeutronApp/AppModel.swift` (`setUpVM`, ~lines 380-460); `eac-vm/guest/run-game.sh`; `eac-vm/host/macneutron-vm.swift:170-172`
- Test: `Tests/MacNeutronCoreTests/LauncherLogTests.swift`, `LauncherTests.swift`, `MicroVMTests.swift`, `AppModelTests.swift`

**Interfaces:**
- Produces: `ToolLayout.isPerformanceBuild: Bool` (`runtimeVersion?.hasSuffix("p") == true`), and the `<version> (<identity>)` description at `ToolLayout.swift:80` reading `0.7.1p (performance build) (<identity>)` for a p runtime (the menu and Settings show it); `LauncherLog.verbose: Bool` (default `true`; `append(_:)` writes only when verbose) and `LauncherLog.summary(_ line: String)` (always writes); `MicroVM.script(…)` emits `MACNEUTRON_PROFILE='p'` when the layout is p, and leaves `MACNEUTRON_VM_PROFILE` and `DXMT_STATS` out of `passed` then.

- [ ] **Step 1: Write the failing tests**:
  `testQuietLogWritesOnlySummaries` — a `LauncherLog` with `verbose = false`: `append("note: x")` leaves launcher.log absent; `summary("verb=run appid=1 exit=0")` writes exactly that line.
  `testPerformanceRuntimeLaunchesQuietly` — a Launcher over a layout whose wine.app Info.plist says `0.7.1p`, no `MACNEUTRON_LOG`, a VM game: the fake runner's run-guest arguments contain `--console /dev/null`; its environment has `DXMT_LOG_LEVEL=none`; launcher.log has exactly one line, starting `verb=`; no `steam-<appid>.log` exists.
  `testPerformanceRuntimeLogsWhenAsked` — same with `MACNEUTRON_LOG=1`: no `--console`, no `DXMT_LOG_LEVEL`, the game log exists, launcher.log has its notes.
  `testPerformanceScriptNamesProfile` — `MicroVM.script` for a p layout contains `MACNEUTRON_PROFILE='p'` and no `DXMT_STATS=` line even when the environment sets `DXMT_STATS=1`.
  `testPerformanceIgnoresPerfStats` — p layout with `MACNEUTRON_PERF=1`: no `DXMT_DXIL_DUMP` in the launch environment.
  `testPerformanceRuntimeDescription` — a p layout's description contains `0.7.1p (performance build)`; a `0.7.1` layout's doesn't.
  `testVMSetupLogOnlyOnFailure` — AppModel for a p app version: a setup exiting 0 leaves no vm-setup.log; one exiting 1 writes its output there.
- [ ] **Step 2: Run** `swift test --filter 'LauncherLogTests|LauncherTests|MicroVMTests|AppModelTests'`. Expected: the new tests FAIL.
- [ ] **Step 3: Implement**: Launcher sets `log.verbose = !(layout.isPerformanceBuild && !logging)` and writes its final `verb=…` lines with `summary`; under p without logging it adds `--console /dev/null` to the run-guest arguments and `DXMT_LOG_LEVEL=none` to the environment (and passes `DXMT_LOG_LEVEL` into the guest via `passed`). AppModel buffers vm-setup's output under p and writes it only for an exit other than 0 and 75 (version from `Bundle.main`'s `CFBundleShortVersionString`). `macneutron-vm` sets `DXMT_LOG_PATH` only when `DXMT_LOG_LEVEL` isn't `none`. `run-game.sh`: with `MACNEUTRON_PROFILE=p` and no `MACNEUTRON_LOG`, `mn-input`'s output goes to `/dev/null` and `WINEDLLOVERRIDES` keeps its value.
- [ ] **Step 4: Run** the filter again (PASS) and `swift test` (all PASS).
- [ ] **Step 5: Commit** `launcher, app: a p runtime logs one line per launch and keeps the VM quiet unless MACNEUTRON_LOG`.

### Task 8: The bench harness for both games

**Files:**
- Modify: `eac-vm/tests/bench-smite2.sh` (BUILD_DIR from Task 2; `BENCH_GAME`)
- Create: `eac-vm/tests/palworld-render.sh` (guest script, as `smite2-render.sh` for app 1623730)

**Interfaces:**
- Produces: `BENCH_GAME=smite2|palworld bench-smite2.sh <label> [seconds]` writing `build/perf/bench-<label>-<time>.json`; `palworld-render.sh` starts Palworld's shipping exe from `/mnt/games/Palworld` (find the exe the Steam launch entry names in `appmanifest_1623730.acf`/the game folder) with forwarded D3D, as `smite2-render.sh` does.

- [ ] **Step 1: Implement** and run (with the maintainer's go-ahead) `BENCH_GAME=palworld sh eac-vm/tests/bench-smite2.sh palworld-std 120` on the normal build. Expected: a JSON with an fps over 0 once the title screen's two 5 s windows reach 30 fps.
- [ ] **Step 2: Commit** `eac-vm tests: bench-smite2.sh benches Palworld's title screen too`.

### Task 9: Measure §5's rows and §6's candidates (procedure)

**Files:**
- Modify: `docs/superpowers/specs/2026-10-19-macneutron-performance-build-design.md` §10 (one row per run)
- Modify (only for kept candidates): `eac-vm/image/build.sh` (`MN_P_CMDLINE` default), `eac-vm/image/rootfs-p/` (create: p-only overlay — `etc/systemd/system/<unit>` symlinks to `/dev/null` for masked units; `usr/share/fex-emu/Config.json` with kept FEX settings), `eac-vm/guest/run-game.sh` (`winemenubuilder.exe=d` under p)

- [ ] **Step 1: Ask the maintainer for the bench go-ahead** (a batch of about 15 runs of 2 minutes each).
- [ ] **Step 2: Baseline**: `bench-smite2.sh std-0.7.1 120` on the normal build.
- [ ] **Step 3: §5 rows**, each a p build with `MACNEUTRON_P_FLAGS` growing in the spec's order (`wine`, `+fex`, `+host`, `+krun`, `+dxmt`), benched as `p-flags-<list>`. A row slower than the previous kept row is removed from `P_FLAGS_DEFAULT` in `wine-arm64/lib.sh`.
- [ ] **Step 4: §6 candidates**, each benched on the kept flags: kernel arguments via the p images' `cmdline.txt` (edit, bench, restore): `mitigations=off`, `init_on_alloc=0`, `transparent_hugepage=always`, `nowatchdog audit=0`, `preempt=voluntary`; FEX settings via run-game.sh environment for the bench (`FEX_X87REDUCEDPRECISION=1`; `FEX_SMCCHECKS=mman`; `FEX_TSOENABLED=0` only if the vCPU's EnTSO is on, else skip; `FEX_MAXINST=10000`); systemd masks and `winemenubuilder.exe=d` need an image build, benched together as one row. FEX and SMC candidates also pass `eac.sh`'s bootstrapper row and Palworld reaching its title screen.
- [ ] **Step 5: Bake the kept candidates** into `MN_P_CMDLINE`, `rootfs-p/` (build.sh copies it over `rootfs/` under p) and run-game.sh; rebuild the p images; record the security trade-offs kept for the notes.
- [ ] **Step 6: Commit** `p profile: the measured flags and runtime settings (spec §10)`.

### Task 10: PGO — tools, instrumented builds, training, profiles

**Files:**
- Create: `pgo/README.md` (one paragraph: what each file is, how to re-record), `pgo/<component>.profdata` and `pgo/<component>.stamp` for `dxmt-native`, `vm-host`, `libkrun`, `fex`, `wine-unix`
- Create: `release/pgo.sh` (`pgo.sh generate|merge`)
- Modify: the five components' build scripts (`MACNEUTRON_PGO=generate|use`), `eac-vm/sysroot/packages.txt` (x86-64 `compiler-rt`), `release/release.sh` (p refuses without profiles; path check covers `pgo/`)

**Interfaces:**
- Produces: `MACNEUTRON_PGO` (unset under p = `use` when `pgo/<component>.profdata` exists; `generate` = instrumented, continuous mode). Instrumented binaries write `LLVM_PROFILE_FILE=<dir>/<component>-%m%c.profraw`: host `build/p/pgo-raw/`, guest `/mnt/share/pgo/`. `release/pgo.sh merge` → `llvm-profdata merge` (Apple's via `xcrun` for host components, rustup's for libkrun, the builder's for fex and wine-unix) into `pgo/*.profdata`, writing each `.stamp` as `<component> <git HEAD> <tree revs>`. Every p compile adds `-ffile-prefix-map=<root>=.` and, where accepted, `-fprofile-prefix-map=<root>=.`.

- [ ] **Step 1: Ask the maintainer** for the two downloads: `rustup component add llvm-tools` and Fedora 44's x86-64 `compiler-rt` added to the sysroot (`eac-vm/sysroot/packages.txt`, rebuild with `eac-vm/sysroot/build.sh`).
- [ ] **Step 2: Write the failing self-test rows**: p without `pgo/*.profdata` refused (`release: the p build needs pgo/<component>.profdata (release/pgo.sh)`); a profile naming `$HOME/` refused (the path check's message).
- [ ] **Step 3: Implement** the `generate`/`use` hooks: clang `-fprofile-generate` (+ `-mllvm -runtime-counter-relocation` on Linux) / `-fprofile-use=<file> -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled`; meson `-Db_pgo=generate|use` with `-fprofile-use` path in `c_args`/`cpp_args`; rustc `-Cprofile-generate=<dir>` / `-Cprofile-use=<file>`.
- [ ] **Step 4: Build instrumented p** (all five), ask for the go-ahead, run the three training runs (spec §7: SMITE 2 cold cache, warm, Palworld title), then `sh release/pgo.sh merge`. Expected: five `.profdata`, none naming `/Users/` (`strings | grep -c /Users/` = 0).
- [ ] **Step 5: Build p with `use`**, bench `p-pgo` against the Task 9 build; per component, a slower result drops that component's profile (it is deleted from `pgo/` and listed in the notes as not used).
- [ ] **Step 6: Stale-profile check** (Review Focus 5): rebuild guest Wine under p with its profile's stamp pointing at the previous commit after touching one Wine source file; expected: builds with no error.
- [ ] **Step 7: Run** `sh release/release.sh --self-test` (PASS) and commit `pgo: profiles for the p build (native DXMT, VM host, libkrun, guest FEX, guest Wine's Unix side)`.

### Task 11: Gates, diagnostics runs, final bench, release

**Files:**
- Modify: spec §10 (final rows)
- Create: `build/release/0.7.1p/` (by make), the notes in the session's scratchpad

- [ ] **Step 1: Gates on the p build** (`BUILD_DIR=$PWD/build/p`): `wine.sh`, `discard.sh`, `d3d-smoke.sh`, `steam-forward.sh`, `vm-setup.sh`, `eac.sh` (bootstrapper row), `make dxmt-tests`. Expected: all ok but `eac.sh`'s client row (reported).
- [ ] **Step 2: Line switch** (Review Focus 2): run `vm-setup.sh`'s setup into a temp VM folder holding the normal images and a `state/1.img`, with the p manifest. Expected: the p images swapped in, `state/1.img` unchanged (same inode and size).
- [ ] **Step 3: Diagnostics runs** with a p dev app (`make app` under p into `build/p/dev/MacNeutron.app`; the maintainer opens it once so it installs its runtime): one VM launch without `MACNEUTRON_LOG` (expected: no `steam-<appid>.log`, no `runs/<appid>/console.log`, `traces/` holding only `ready` and `capture`, one new `launcher.log` line) and one with `MACNEUTRON_LOG=1` (expected: game log with Wine `warn:`/`err:` lines, the guest console, DXMT warnings/errors). The maintainer launches both from Steam.
- [ ] **Step 4: Final bench** (go-ahead): 0.7.1 vs p in SMITE 2 and Palworld; fill spec §10; commit the spec.
- [ ] **Step 5: `make release VERSION=0.7.1 PROFILE=p`** with the signing variables. Expected: every `PASS` row, the p rows included (`check_no_traces`, `check_no_debug_info`), and the printed commands with `v0.7.1p` and `--latest=false`.
- [ ] **Step 6: Checkpoint** — show the maintainer the final table and wait for the go.
- [ ] **Step 7: Publish**: push the branch to `origin/main`, `git tag v0.7.1p <commit>`, push the tag, `gh release create v0.7.1p --target <commit> --latest=false --title 'MacNeutron 0.7.1p (performance build)' --notes-file <notes> <four assets>`; check that `gh release list --limit 3` still marks v0.7.1 `Latest`. The notes cover spec §9's list.
- [ ] **Step 8: Memory**: a `performance-build` memory (the profile, BUILDER_HOME/outp, pgo re-recording, what was kept) and the index line.
