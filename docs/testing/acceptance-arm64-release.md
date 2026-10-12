# Acceptance: sub-project 5, the arm64-only release

Spec: `docs/superpowers/specs/2026-10-04-macneutron-arm64-release-design.md` (§6.2 R0 and R0b, §9; §14 wins on
conflict). Manual, on the maintainer's Mac (macOS 27, Apple Silicon), with the notary profile `macneutron` (an App Store
Connect API key), the Developer ID identity and the provisioning profile for `net.authspot.macneutron.wine`.

## R0

`release/r0.sh` (Task 1), 2026-10-04, on the staged `build/wine-arm64/wine.app` built at `f386639` (version `dev`, the
MetalFX presenter inside).

- `sh release/r0.sh notarize` (`release/lib.sh` `notarize_and_staple`):
  - `syspolicy_check notary-submission`: 56 warnings, no errors, all "Resources directory contains Mach-o binaries"
    (Wine's `Contents/Resources/bin/*` and `lib/wine/aarch64-unix/*`). Warnings don't stop the script; errors do.
  - Submission `c4a00d9d-aa15-4765-a5a9-07915318f370`: `Accepted` ("Processing complete"). The only submission.
  - `stapler staple`: "The staple and validate action worked!"; `stapler validate`: "The validate action worked!".
  - `syspolicy_check distribution`: "App passed all pre-distribution checks and is ready for distribution."
  - Also checked by hand: `codesign --verify --strict --deep` passes; `spctl -a -vvv -t exec`: `accepted`,
    `source=Notarized Developer ID`.
  - The loader's CDHash, before and after stapling (equal):
    - `before staple: CDHash=58b87f8cf286b82eef4afad6581ce788571e640c`
    - `after staple: CDHash=58b87f8cf286b82eef4afad6581ce788571e640c`
  - Quarantine on `build/release/r0/quarantined/wine.app`: `0081;6ac2f042;Safari;C97E0BFF-657D-4FD2-B401-0B9D5232E03C`.
- `sh release/r0.sh online`: the quarantined copy cloned to `build/release/r0/online/Application Support/wine.app`,
  `arm64-hello.exe` run with the clone's loader as a launchd job (`gui/<uid>`, `net.authspot.macneutron.r0`) in a fresh
  prefix: `wProcessorArchitecture=12 dwPageSize=4096 NtMajorVersion=10`, `PASS arm64-hello`, `status=0`. Afterwards
  the clone's quarantine flags read `00c1` (0x0040 set: Gatekeeper assessed it and let it run), the job was gone and no
  process ran the clone's binaries.
- Offline (2026-10-05T00:45:40Z, run by the maintainer with Wi-Fi and Ethernet disconnected; the guard required
  `api.apple-cloudkit.com` and `ocsp.apple.com` to be Not Reachable): a fresh clone of the quarantined copy at
  `…/offline/Application Support/wine.app` ran `arm64-hello.exe` through launchd: `PASS arm64-hello`, `status=0`.
  Caveat: the online run had already assessed this CDHash, so syspolicyd may have answered from its cache; this proves
  the copy launches with Apple unreachable, not that the stapled ticket alone was used.

## R0b

2026-10-04, with the maintainer. Steam on macOS has no Compatibility tab, so the two mappings were switched in
`config.vdf` with Steam closed (backup kept; restored and checked afterwards): SMITE 2 (2437170) to `r0b-probe`,
Timberborn (1062090) to `r0b-probe-native`, priority 250. Play on each ran only the probe, a thin arm64 binary:

- `r0b-probe`: `arch=arm64 translated=0 argv=…/compatibilitytools.d/r0b-probe/./bin/r0b-probe|launch|waitforexitandrun|…/SMITE 2/Windows/Hemingway.exe|…`
- `r0b-probe-native`: `arch=arm64 translated=0 argv=…/compatibilitytools.d/r0b-probe-native/./bin/r0b-probe-native|passthrough|waitforexitandrun|…/Timberborn/Timberborn.app`

R0b: PASS

## R1, R3, R4, R5: `release/release.sh` and its rehearsal

`release/release.sh` (Task 12, then Task 12b: no build path in the release, and the hardening of Rulings 20 and 22),
2026-10-05, with the signing variables and Steam running and logged in. The runs below were made at `d1972a4`, and
again after the final review's fixes at `83b1c15` (the self-test's four new rows, the zip's unzip check and the
numbers in the table are from `83b1c15`). Nothing was submitted to Apple: the notarized run (R2, L6's notarized row on
the release bundle) is Task 14's.

- `make release VERSION=0.0.1-rc` (at `d1972a4`): `release.sh --check-version` runs before make's prerequisites, so it
  printed only `release: VERSION 0.0.1-rc is not MAJOR.MINOR.PATCH` and make's own `make: *** [release] Error 2`, built
  nothing, and exited 2. `make release VERSION=01.0.0` the same (exit 2).
- R1, `sh release/release.sh --self-test` (at `d1972a4`): 35 `ok`, `PASS release self-test`. Each refusal on its own
  input in a temporary clone whose origin is a local bare clone (the DXMT case with a local bare repository as the
  fork): a version that isn't MAJOR.MINOR.PATCH, one with a leading zero (`01.0.0`, `0.00.1`), `v0.0.0` already a tag,
  `v0.0.2` a tag on origin only (pushed to the local origin, deleted locally), release.sh exiting 2 with only the version
  line (`0.0.1-rc`, `--check-version 01.0.0`) or only the usage line (`0.1.0 --rehearse`, `--rehearse 0.1.0 x`,
  `--self-test x`, `--check-version`, `--rehearse`, `--bogus`, no argument), each README string (`Rosetta 2`,
  `import-gptk`, `doesn't redistribute`), a dirty tree, a HEAD not in origin/main, a tree that isn't applied, a SOURCE
  with `WINE_SERIES=dev`, with a `+dirty` commit or with another commit than HEAD, an unpublished DXMT commit; and the
  passing case of each (`0.10.0` among them). At `83b1c15`: 39 `ok`, the four new ones through a PATH shim for `xcrun`
  (never the real notarytool): a notary profile notarytool can't use (refused with the other exit-1 refusals), a usable
  one, a staple that fails twice and is retried, a staple that keeps failing and stops after six tries.
- `sh release/release.sh --rehearse 0.0.0` (at `d1972a4`, 2 min 20 s; `build/release/rehearse-0.0.0/`, with its
  `REHEARSAL` file):
  - Refusals: READMEs, the four trees `applied`, `DXMT_COMMIT` published on the fork, `wine-arm64: up to date`. The
    rehearsal skips the clean-tree, origin/main and origin tag refusals.
  - The release `wine.app` (`bundle.sh --release`): every assertion passed after stripping (signature, entitlements,
    licences, minos, timestamps, links, x18 counts, builtin markers, CHPE metadata). Its `SOURCE` names HEAD.
  - No build path (Ruling 20): no file in the stripped `wine.app` (bundle.sh, before signing) or in `MacNeutron.app`
    (release.sh, before signing) contains the repository's path, the build folder's or `$HOME/`. Wine's unix and PE
    compiles map `build/wine-arm64-src/` away (`-ffile-prefix-map`: its critical-section names read
    `wine\dlls\kernel32\profile.c: PROFILE_CritSect`); DXMT's `.metal` files compile on stdin
    (`wine-arm64/tools/xcrun-metal.sh`): metal wrote each file's absolute path into the AIR module's
    `air.source_file_name`, which airconv erases before linking (never read), and ignores the prefix maps. Before the
    maps (the assertion added to `ce3e06d`, on the trees built without them), the rehearsal stopped at bundle.sh on 247
    files (`winemetal.so`, `msi.dll`, `d3dx9_33.dll`, …). After: `grep -rlaF -e <repository> -e "$HOME/" MacNeutron.app` → 0 files. What remains under
    `/Users/` is 71 `/Users/runner/…` strings in 6 files (FEX's `libarm64ecfex.dll`, DXMT's front ends and
    `dxmt-replay.exe`), from the downloaded llvm-mingw runtime, built elsewhere.
  - R3: `smoke.sh` on it (`info wine.app: …/build/release/rehearse-0.0.0/wine.app (0.0.0)`): every row PASS (the
    notarized row names the bundle it checked, `build/release/r0/wine.app`, R0's); the bridge probe through an assembled
    tool folder: `init: ok`, `steamid ok`, auth ticket 234 bytes; `present_loop.exe 1280 720 0 0 120 0` on DXMT through
    it: `frames 120, avg frame 8.104 ms` (`8.089 ms` at `83b1c15`).
  - R4: `licences_test.sh --app` on `MacNeutron.app`: `PASS licences_test`, `PASS licences_test --app` (and red by hand
    on a copy without `Contents/Resources/licenses/LICENSE`, on one with neither the README's pointer nor `wine.app`'s
    `licenses/macneutron/LICENSE`, and on one whose `LICENSE.TXT` differs from `wine.app`'s `llvm-mingw/` copy). The
    app: both Swift binaries `arm64` and stripped (`strip -S`: no `OSO` entries, no path under `/Users/`), version
    `0.0.0`, minimum `27.0`, hardened runtime and a secure timestamp on the CLI and the app, no entitlements,
    `codesign --verify --strict --deep` passes, the nested `wine.app`'s CDHash equal to the release bundle's; `spctl`
    rejects it (`Unnotarized Developer ID`), as expected before notarization.
  - The zip (at `83b1c15`): written with `ditto -c -k --norsrc`, so it holds no AppleDouble (`._*`) entries; release.sh
    unzips it with `/usr/bin/unzip` into a temporary folder and `codesign --verify --strict --deep` passes on the
    extracted `MacNeutron.app` and its `wine.app` (`PASS MacNeutron-0.0.0.zip unzips (unzip) to a MacNeutron.app and
    wine.app that verify`; in release mode also `stapler validate` on both). The embedded provisioning profile carries
    no download metadata (`kMDItemWhereFroms`) or quarantine.
  - R5: `PASS sources`. `verify-sources.sh` was also red, by hand, on a wrong `WINE_SERIES`, `FEX_SUBMODULE_fmt`,
    `GMP_SHA256`, `MACNEUTRON_COMMIT` and `LLVM_TAG`, a missing `dxmt-nvapi.tar`, an edited lsteamclient file, an SDK
    file added to `lsteamclient.tar`, a wrong Wine patch count, and a submodule row that names another submodule's
    commit and tar (with SOURCE to match): its commit isn't the gitlink FEX records at its applied commit.

| Size (rehearsal at `83b1c15`) | |
|---|---|
| `wine.app` before stripping | 1,368,024 KB |
| `wine.app` after stripping (unsigned; `.a` files and Wine's developer tools deleted) | 459,108 KB |
| `MacNeutron-0.0.0.zip` (rehearsal, not notarized, no AppleDouble entries) | 139,129,246 bytes |
| `MacNeutron-0.0.0-source.tar.gz` | 106,953,079 bytes |

The rehearsal's `SHA256SUMS` describe unpublished rehearsal files and aren't recorded; the release's go here in Task 14.

## Full runs (Task 14 Step 1)

2026-10-05, with the signing variables and Steam running and logged in; logs in `build/sp5-acceptance/` (first run)
and `build/sp5-acceptance-2/` (second run).

| | `21f2cb4` (before the final review) | `83b1c15` (after its fixes) |
|---|---|---|
| `make test` | 213 passed | 219 passed |
| `make smoke` | 15/15 (notarized row PASS) | 15/15 (notarized row PASS) |
| `make bridge-check` | 15 `ok` | 15 `ok` |
| `make presenter-check` | 12 `ok` | 12 `ok` |
| `make wine-arm64-check` | every step PASS, `dxmt-arm64ec` and `g4-bench` included (17 min) | every step to `dxmt` PASS; `dxmt-present` FAIL (see below) |
| `release.sh --self-test` | 35 `ok` | 39 `ok` |
| `release.sh --rehearse 0.0.0` | — | PASS (above) |

At `83b1c15`, `dxmt-present` failed with `winshot: screencapture of window … failed`: the Mac's screen had locked
(`CGSSessionScreenIsLocked` true at 02:11), and a window capture needs an unlocked screen. `make wine-arm64-check` stops
at a failed step, so `dxmt-present`, `dxmt-arm64ec`, `dxmt-x64` and `g4-bench` were rerun at `83b1c15` with the screen
unlocked (`sh wine-arm64/check.sh dxmt-present dxmt-arm64ec dxmt-x64 g4-bench`, after `make build wine-arm64
wine-arm64-tests dxmt-tests presenter dxmt-tests-arm64ec`: `wine-arm64: up to date`; 15 min): PASS boot, fex, dxmt,
dxmt-present, dxmt-arm64ec, dxmt-x64, g4-bench, orphans. With the first run, every step of `make wine-arm64-check`
passed at `83b1c15`. `PASS orphans` held in both.


## Gate S: SMITE 2

2026-10-05. SMITE 2 (Unreal Engine 5.5.4) on 0.1.0 stopped at "The following component(s) are required to run this
program: Microsoft Visual C++ Runtime". Its bootstrap, `Hemingway.exe`, reads the file version of
`C:\windows\system32\vcruntime140_1.dll` (Wine maps its own builtin for that, whatever the prefix's copy is), and the
builtin had no version resource (`GetFileVersionInfoSize` fails; under the Rosetta-era x86_64 Wine it was 14.50.35719).

Cause: Wine's `tools/makedep.c`. `vcruntime140_1` (and `dpnsvr`) are enabled for `x86_64,arm64ec` only, so in the
`arm64ec,aarch64` build the module is linked for the disabled aarch64 with arm64ec as its link arch, and
`output_module` linked aarch64's resource list, which is empty: the `.res` built from the `VER_` variables went only
into arm64ec's. Fix: Wine patch 0020 links the link arch's resources when the arch is disabled (an ARM64X module is
unchanged and gets its resources once). `bundle.sh` now fails naming any shipped module whose `Makefile.in` sets
`VER_` and that has no `VS_FIXEDFILEINFO`; before the patch it named `vcruntime140_1.dll dpnsvr.exe`.

Verified at the patch's commit: the rebuild relinked only those two modules; `vcruntime140_1.dll` carries 14.50.35719.0
and `dpnsvr.exe` 5.3.0.900; `msvcp140_2.dll` and `kernel32.dll` (ARM64X) still have one `VERSIONINFO`
(`llvm-readobj --coff-resources`). SMITE 2's bootstrap, run with Steam's arguments through a scratch tool folder and
prefix (no Steam bridge), reads `vcruntime140_1.dll`'s version (14.50.35719.0), shows no message box and starts
`Hemingway-Win64-Shipping.exe`. The game then stops at its own error dialog: its log says `Adapter only supports up to
Feature Level 'SM5', requested Feature Level was 'SM6'` (DXMT reports shader model 5.1 by default). `make test` 219
passed, `make smoke` 15/15, `sh dxmt/check.sh` 200 `ok` (all passed), `make bridge-check` 15 `ok`.

## Gate S follow-up: loading time and the player's settings (Task P2)

2026-10-05. With its settings back, SMITE 2's frame rate recovered (from ~4 FPS), but its engine took 31-97 s to
initialise on 0.1.0 against 16 s on the Rosetta-era runtime.

**Loading.** Each `NtCreateThreadEx` maps a stack and thread data through `map_free_area` → `try_map_free_area`
(`dlls/ntdll/unix/virtual.c`), which tries one fixed `mach_vm_map` per 64 KB step through every range the host owns
without Wine tracking it as a view. A benchmark creating 2,000 threads (`CreateThread`, each sleeping; x64 under FEX,
through `macneutron launch` in a scratch tool folder) made 500 in 0.36 s and only 978 in 600 s on the 0.1.0 Wine;
`WINEDEBUG=+virtual` showed ~2,960 failed probes per allocation through a 185 MB host range above `0x100230000` and 3,567
per process start through the reserved area under the top of the address space, and the perf investigation saw ~7.2
million per allocation once the 385 GB guard region under `0x7000000000` was in the way. Wine patch 0021 asks
`mach_vm_region` for the region in the way after a failed probe and continues past it (to its end bottom-up, to its
start minus the size top-down, aligned; every skipped candidate overlaps it). The benchmark now makes 2,000 threads in
0.71-0.77 s (x64) and 0.22-0.25 s (arm64; Rosetta-era runtime: 0.57 s). Under `+virtual`, a 2,000-thread x64 run
showed at most 345 failed probes per allocation; a second run, summarised with a count of the patch's "skipping host
region" lines, had 622,451 failed probes, each followed by a whole-region skip (none by a 64 KB step), at most 324 per
allocation.

SMITE 2, scratch prefix cloned from one with the good settings, Steam bridge on, `env -i` (engine initialised = from the
log's first line to `Engine is initialized`; lobby = launch to `Took … to LoadMap(…L_MainLobby_P)`):

| Runtime | Engine initialised | Launch to lobby | Lobby FPS (60 s window) |
| --- | --- | --- | --- |
| 0.1.0 | 96.8 s, 44.7 s | 102.4 s, 49.3 s | 35.9, 36.3 |
| with patch 0021 | 9.2 s, 8.9 s | 26.3 s, 24.8 s (includes preparing the prefix in place) | 36.0 |

(Rosetta-era DXMT: 14.5 s. Every run logged in through the bridge (`Result=Success`). In one patched run the game
moved on to the Jungle Practice match lobby 9 s after the main lobby without input from the test, so that run's frame
rate is left out; the 59 FPS measured earlier in this prefix came from a window that also covered that match lobby.
In the main lobby alone, both runtimes run at ~36 FPS.)

**Settings.** The fresh prefix that replaces a Rosetta-era one (renamed `pfx.rosetta`) started SMITE 2 with its
defaults (XeSS, ~4 FPS). The launcher now carries the player's data into it (spec §14, amending §3.4 step 1): the user
folders' `AppData/Local`, `AppData/LocalLow`, `AppData/Roaming`, `Documents` and `Saved Games` files it lacks, cloned,
never overwriting, and the games' `HKCU\Software\<Vendor>` keys from `user.reg`. On a clone of the scratch Rosetta-era
prefix, a launch logged `note: carried the player's data from pfx.rosetta: 17 files, 1 registry keys`; SMITE 2's
`Saved` folder matched the old one, and the Unreal Engine key survived the wineserver's next save of `user.reg`.

## XeSS default (Task X1)

2026-10-05. XeSS's path for GPUs other than Intel's (DP4a) is emulated on Apple GPUs: SMITE 2 with XeSS ran at ~3.8
FPS on every translator, and a fresh prefix makes XeSS its default. Wine patch 0022 adds builtin `libxess.dll` and
`libxess_dx11.dll` with the exports of SMITE 2's (XeSS SDK 2.0.1.41: 72 and 51, same names and order in both views of
the ARM64X module) that report the device unsupported; the launcher loads them (`libxess,libxess_dx11=b`) unless
`MACNEUTRON_XESS=1` (spec §14, amending §§3.6-3.7).

SMITE 2, each run in a fresh scratch compat folder (no settings carried), Steam bridge on (`Result=Success`), `env -i`,
tool folder from `macneutron install` with the new `wine.app`; lobby FPS from 20 s after the lobby's
`LoadMap` (`fps4.py`; its windows, bounded by the log's lines, were 51.4 s and 53.8 s):

| Run | Hemingway.log | Upscaler | Lobby FPS |
| --- | --- | --- | --- |
| default | `LogXeSSRHI: Loading XeSS library 2.0.1 on Apple RHI D3D12`, then `LogXeSSRHI: Intel XeSS effect NOT supported, result: -1`; the process maps the stand-in from `wine.app`, not the game's `libxess.dll` | FSR 3 (`r.FidelityFX.FSR3.Enabled = "1"`) | 60.0 |
| `MACNEUTRON_XESS=1` | `LogXeSSRHI: Intel XeSS effect supported`, `LogXeSSModule: XeSS successfully initialized`; the game's `libxess.dll` is mapped | XeSS (`CallApplySettingFunction XeSS`) | 4.1 |

(The game's defaults differ from the player's settings measured in Task P2, ~36 FPS at 2560x1440 High.) Also run:
`make test` (236 passed), `wine-arm64: up to date` after the rebuild, `bundle.sh`'s assertions (the version-resource
one included: both stand-ins set `VER_`), `make smoke` 15/15, `dxmt/check.sh` (x64 lane) 200 ok 0 FAIL,
`make bridge-check` 15 ok.

## msync shm pages (Task M1)

2026-10-05. A benchmark creating 1,500 threads after a D3D12 device (`allocbench` x64, through the launcher, `env -i`,
msync on, 60 s watchdog per run) hung in 7 of 41 runs on the dev `wine.app` with patches through 0022. Each hang
printed `msync: error: mach_vm_map failed with 3: (os/kern) no space available`, then `wineserver crashed`: the
server's `get_shm()` mapped a new shm page with `VM_FLAGS_ANYWHERE` from an uninitialized address (the kernel searches
from it), and its memset then wrote through that address. 0.1.0 has the same code. Wine patch 0023 starts the search
at 0 and ends the server with a message naming the page if a mapping still fails; on the client side, a failed or empty
reply is never mapped or released, and a page that can't be mapped ends the process instead of handing out NULL plus
an offset. With it, msync on, none of 82 runs hung or printed `mach_vm_map failed` (60 on the development build of
the patch, 20 on the build of the commit that adds it, plus each prefix's first run). Also run on that build:
`wine-arm64: up to date`, `check.sh boot fex g1-threads g1-seh msync x18 dxmt` all PASS, `make smoke` 15/15,
`dxmt/check.sh` (x64 lane) 200 ok 0 FAIL, `make bridge-check` 15 ok.

## Translator-keyed shader cache and replay stamp (Task C1)

2026-10-05. DXMT's translation cache was keyed on `git describe` of its tree and the launcher's replay stamp on
`DXMT/version`: a re-fetch of the same DXMT patches (git am makes new commits) missed every translated function and
forced the replay, and an uncommitted airconv edit kept serving the old translations. Both now follow `lib.sh`'s
`translator_key` (DXMT patch 0003; `DXMT/translator`; spec §14). `wine-arm64/tests/translator_key_test.sh` 17 ok (red
on today's `git describe` keying: 12 FAIL). Two re-fetches of the real DXMT tree (HEADs `a41a976`, `89c7bd7`) gave
one key, `148014f4…`, as did the dev and applied builds (`DXMT/version` `+dev` → `+5bb2319a456d`); a byte in
`src/d3d12` kept it, one in `src/airconv` changed it. A key change without a meson setup regenerated
`dxmt_translator_key.h` and recompiled `dxmt_shader_cache.cpp` alone (17 ninja steps).

SMITE 2 on a clone of the perf study's compat folder (stamp `1fba8d2…+cd6d4065e615 26A434`), `env -i`, bridge on,
`DXMT_SHADER_CACHE_PATH` in scratch, tool folder from `macneutron install`: the first launch logged
`precache: Hemingway-Win64-Shipping.exe.pipelines exit=0 replay: 20730 pipelines (17408 graphics, 3322 compute),
20730 created, 0 failed, 0 bad records, 6375 ms`, stamped `148014f4… 26A434`, and reached the lobby; the cache's one
table is `cache_1987945399250107254` (FNV-1a of the key and `AIRCONV_VERSION`), 27,178 entries. The relaunch logged no
`precache:` line, reached the lobby (bridge `Result=Success`), and left the table at 27,178 entries. Also run:
`make test` 239 passed, `wine-arm64: up to date`, `make smoke` 15/15, `dxmt/check.sh` (x64 lane) 200 ok 0 FAIL,
`make bridge-check` 15 ok.

## XeSS answered by MetalFX: the bridge (XeSS plan, Task 2)

2026-10-06. Wine patch 0024 gives the builtin `libxess.dll` real D3D12 calls: a context per
`xessD3D12CreateContext` on a device with DXMT's `IMTLD3D12DeviceExt` (DXMT patches 0004-0005), one MetalFX temporal
upscaler per context made at `xessD3D12Init`, and `xessD3D12Execute` recording `TemporalUpscale` into the game's
command list. On a device without DXMT's interface `xessD3D12CreateContext` still returns -1 (the check's run passes an
object without it: under wined3d, Wine made no D3D12 device). `libxess_dx11.dll` keeps
0022's stand-ins. Jitter and motion vectors follow Intel's XeSS-SR Developer Guide 2.0 (the jitter moves the samples
by -jitter; motion vectors point from the current frame to the previous one) and pass to MetalFX unchanged; the
plan's negated mapping scored below bilinear on the test (2.0x: 18.75 dB against 22.17; 24.68 with Intel's). SMITE 2's
logging run (Task 3) still has to confirm what Unreal passes.

`dxmt/tests/d3d12_xess.cpp` loads `libxess.dll` by full path from a copy of itself (only `libxess=b` makes that the
builtin) and drives XeSS's API at 2560x1440, 64 jittered frames of the spike's scene. On the applied build
(`dxmt/check.sh`, x64 lane, 223 ok 0 FAIL, `dxmt-check: all passed`):

| Mode | Input | PSNR (dB) | Bilinear (dB) |
| --- | --- | --- | --- |
| aa (against each pixel's average) | 2560x1440 | 34.86 | 31.68 |
| quality | 1504x846 | 26.38 | 22.04 |
| balanced | 1280x720 | 24.68 | 22.17 |
| performance | 1112x626 | 25.45 | 21.01 |
| ultraperf | 854x480 | 23.66 | 19.18 |

`cycles` (re-initialised context, destroyed before its list runs, 20 more made and destroyed, a history reset: 22.56
dB against 24.68 converged and 22.17 bilinear), `flags` (bits 5 and 30 accepted, bit 9 -4, XeFX 0.0.0, version 2.0.1,
a destroyed context -8) and `unsupported` (an object without DXMT's interface) all ok; Metal's validation layer logged nothing over
`performance cycles`. MetalFX on macOS 27.0.1 never returns a temporal upscaler's memory: a native program creating
and releasing one keeps ~232 MB per upscaler (`currentAllocatedSize`; retain count 2 at creation), so the 20 contexts
grow the GPU memory by ~5.2 GB whatever the bridge releases. Also run: `make test` 239 passed, `make smoke` 15/15,
`make bridge-check` 15 ok, both series applied from a fresh fetch (Wine 24 of 24, DXMT 5 of 5), the translator key
unchanged.

## XeSS answered by MetalFX: before the game run (XeSS plan, Task 2b)

2026-10-06. DXMT patch 0006 keeps the last 4 MetalFX temporal upscalers a device made and hands one that only the
device still holds (its D3D12 object released, every allocator that recorded it reset) to the next
`CreateTemporalScaler` of the same Metal description: macOS 27.0.1's MetalFX never frees one (~232 MB each), so a
game re-initialising XeSS now reuses instead of growing. A reused upscaler's first upscale resets its history. A
TYPELESS colour or output (R16G16B16A16, R32G32B32A32, R10G10B10A2, R8G8B8A8, B8G8R8A8) is read as its float or unorm
variant through a same-layout view; other TYPELESS formats stay refused. Wine patch 0025 only adds notes: the bridge's
lock limit (a destroy waits for at most one call inside that context) and the rulings Task 3 checks (pass-through
jitter and velocity signs, the optimal input as the dynamic minimum, the literal pre-exposure).

New rows, both lanes of `make dxmt-check` (226 ok each, `dxmt-check: all passed`): `upscale typeless` 24.90 dB against
22.17 bilinear (already passed before 0006: DXMT maps R16G16B16A16_TYPELESS to RGBA16Float), `upscale typeless32`
24.90 (refused before 0006), `xess cycles` with the GPU memory judged (a context made while the destroyed one's
upscaler is still in the unsubmitted list gets a new one: +231 MB; 20 contexts made and destroyed: +28 MB, was +5194;
balanced and performance alternating 10 times: +255 MB, was +2572), `xess reuse` (1440x810 textures, content
1280x720 → 1152x648 → 1024x576 kept the upscaler, 23.84 against 20.93; RG32F motion vectors made one new one, 24.66
against 22.17; `recreates 1`), and `xess flags` with real frames for bits 1, 4, 7 and 0 (24.68, 24.68, 24.68, 24.69
against 22.17). Without their Init bits the same frames score 19.09 (NDC), 21.86 (jittered) and 20.52 (high-res), all
below bilinear; inverted depth scores 24.66 either way: this scene barely depends on depth.

## XeSS answered by MetalFX: the pool cap (XeSS plan, Task 2c)

2026-10-06. DXMT patch 0007 caps the upscaler pool by released upscalers (Ruling 7): every upscaler in use stays
pooled, and at most 4 released ones beside them; past that the oldest released one is dropped. 0006 kept the last 4
made, so a live context's upscaler could be pushed out and, once released, never reused. A `static_assert` on
`WMTFXTemporalScalerInfo`'s size (52) makes a new field join `SameTemporalScaler`'s comparison. Wine patch 0026 rewords
the bridge's lock note: a destroy, and the lookups queued behind it, wait for every call whose lookup already holds
the list lock, each one pass or at most one upscaler creation.

Both lanes of `make dxmt-check`: 227 ok each, `dxmt-check: all passed`. `xess cycles` now also keeps one context live
while 5 other settings (aa, quality, performance, ultra performance at 2560x1440, balanced at 1920x1080) are made and
released in turn, then makes the first 4 again: `five 879 rerun 0` MB (0006: `five 1106 rerun 978`), and the live
context still upscales after it, 24.68 against 22.17 bilinear. New rows: `upscale typeless10` (R10G10B10A2_TYPELESS
colour and output) 24.34 against 21.91, and `upscale bad` refusing a B8G8R8X8_TYPELESS and an R16G16_TYPELESS colour
with E_INVALIDARG; both already passed on 0006 (they pin its behaviour). Metal's validation: 0 messages, 6 upscale
modes and 2 XeSS modes ok.

## XeSS answered by MetalFX: compressed scratch, private storage (XeSS plan, Task 2d)

2026-10-06. DXMT patch 0008 gives the bridge's depth, motion vector and output scratch textures lossless compression
(every access uses the scratch's own layout; `DXMT_D3D12_COMPRESSION=0` turns it off). DXMT patch 0009 (Rulings 11
and 12) makes heaps the CPU can't see (DEFAULT, CUSTOM with no CPU pages) Private, with the committed textures on such
heaps and the textures and buffers placed in them; committed buffers and CPU-visible heaps stay Shared; sizes are
still asked for Shared (Private measured the same on the M5 Pro). A 2D UAV texture of a format Metal renders to also
gets RenderTarget usage (MetalFX's output usage is 0x7), so MetalFX writes a UAV-only output directly; the scratch and
copy stay for the outputs it can't write, counted by `DXMT_STATS` as `upscale output copied`.
`DXMT_D3D12_PRIVATE=0` keeps everything Shared. Write/ReadFromSubresource refuse textures on GPU-only heaps.

Both lanes of `make dxmt-check`: `dxmt-check: all passed`, no FAIL. New rows: `upscale direct` (committed UAV-only
output) and `upscale placed` (placed in a DEFAULT heap) 24.90 against 22.17 bilinear with 0 outputs copied (the
scratch path gave 24.90 in Task 2c); `upscale direct` with `DXMT_D3D12_PRIVATE=0` copies (64). `hazard placed-uav`
(a buffer placed in a DEFAULT heap, written by a dispatch, copied to READBACK, mapped) reads 1048576, on our DXMT and
on D3DMetal. Every other upscale and XeSS PSNR is unchanged; `xess cycles` reports `growth 0 ab 227` MB (Task 2c:
`growth 28 ab 255`). Metal's validation: 0 messages in the upscale (8 modes), XeSS (2) and hazards runs.

## XeSS on MetalFX in SMITE 2 (XeSS plan, Task 3)

2026-10-06. SMITE 2 in a scratch prefix (an APFS clone of the Gate S follow-up's player-settings prefix with the fresh
game defaults' `HWGameUserSettings.sav`, which selects XeSS; the clone alone keeps TAA), `env -i`, a tool folder from
`macneutron install` with the dev `wine.app`, Steam bridge on, lobby only. The external display was off: every run below
is on the built-in display, where the game's fullscreen window makes XeSS's output 1728x1117 (balanced, input 864x559),
not the player's 2560x1440. The 2560x1440 runs are Task 3c (Ruling 15). *Corrected in Task F3:* run 3 left the lobby for
the Jungle Practice match (`game-m3.log:5485` LoadMap of the match lobby with transition tag JunglePractice, `:11790`
SeamlessTravel to the practice map), so its trace is an in-match frame, not an uncapped lobby; it is the only arm64
in-match trace (1728x1117 output, XeSS Balanced, input 864x559). By the resolution study's evidence (its leading
hypothesis, not a verdict), the in-game Resolution setting doesn't change the render size in borderless (windowed
fullscreen, `FullscreenMode=1`): the game saves it, but the back buffer stays at the desktop size, so only resolution
scale or the upscaler's quality changes what the GPU renders (the XeSS plan's resolution study; in the one logged
session with a real change, on 0.1.0, 2560x1440 → 1920x1080 and back, FPS stayed at 86-92 and the presenter saw no
smaller drawable).

**Logging run.** `MACNEUTRON_LOG=1` doesn't show the bridge's parameter dumps: its `WINEDEBUG=+err,+warn` turns on
channels named `err` and `warn`, not the warn class (`warn+xess` does; since Task 3b the launcher's logging mode sets
`warn+all,+loaddll,+steamclient`, Ruling 18; since Task FU that is `MACNEUTRON_LOG=2`, and `MACNEUTRON_LOG=1` sets
`warn+all,warn-seh,+loaddll,warn+steamclient`). Hemingway.log still has the bridge's one-time messages, through the
game's logging callback. The first run found the bridge refusing every frame:
`LogXeSSSDK: Warning: xessD3D12Execute: … depth 0000000000000000, … input 864x559`, then `Failed to execute XeSS,
result: -4` about 50 times a second. Unreal's XeSS plugin initialises with flags 0x101 (high-res motion vectors, auto
exposure) and passes no depth texture, which XeSS's header allows with high-res motion vectors. Wine patch 0027 makes a
colour-sized far-plane depth for MetalFX when the game passes none (MetalFX's validation wants depth and colour the
same size; an output-sized one drew 2 messages a frame). `d3d12_xess`'s `flags` gains Unreal's call (`nodepth`: 24.67
dB against 22.17 bilinear; it failed with -4 before the fix) and runs under Metal's validation in a row of its own (0
messages). With 0027, a fresh clone, `WINEDEBUG=+err,+warn,+loaddll,+steamclient,warn+xess` (pointers and floats
shortened):

```
warn:xess:init output 1728x1117, quality 102, flags 0x101, temp heaps 0 0, pipeline library 0
warn:xess:execute flags 0x101, input 864x559, output 1728x1117; formats colour 26 depth 40 motion 34 output 26
  mask 0; sizes colour 864x560 depth 864x560 motion 1728x1117 output 1728x1120; bases colour 0,0 depth 0,0 motion
  0,0 mask 0,0 output 0,0; depth texture 0, exposure texture 0, mask 0; jitter -0.250000,0.166667 scale 1,1;
  velocity scale 1,1; exposure 1.000000; reset 1
```

and no failed Execute. Conventions from it (Ruling 16: the jitter's sign and the high-res motion vectors' units and sign
are settled in Task 5's captures, not from a log):
- Velocity in pixels (bit 4 clear), at the output size (bit 0): the motion texture is 1728x1117, R16G16_FLOAT, velocity
  scale 1. The bridge passes them as output pixels, current to previous (Ruling 2); their real units and sign, and the
  NDC Y sign, can't be read off one dump: the maintainer's captures judge them (Task 5).
- Jitter (-0.25, 0.17) in the first frame: input pixels within ±0.5, as XeSS defines it; its sign is likewise for the
  captures. Not jittered motion vectors (bit 7 clear), so the jitter's sign inside them doesn't arise.
- No depth, no inverted depth (bit 1 clear): `DepthReversed` doesn't arise; the bridge's depth is a constant 1.0.
- Exposure: auto (bit 8), no exposure texture (bit 2 clear), `exposureScale` 1.0, so `PreExposure` stays literal
  (Ruling 4): neutral.
- Formats: colour and output R11G11B10_FLOAT (26), motion R16G16_FLOAT; no responsive mask. Init's guessed upscaler
  (RGBA16F) never matched them: since Wine 0028 (Ruling 17) Init makes none and the first Execute makes it from the
  game's textures.
- Every base is 0, the output's included (`outputColorBase` 0,0; the output texture is 1728x1120 for a 1728x1117
  output): MetalFX's direct path with a non-zero output offset still hasn't run in a game.

**Measurements** (the same prefix after the logging run; the trace 6 s, `gpu-trace.py --label MetalFX_Temporal`;
lobby FPS by `fps4.py` from 20 s and from 100 s after the lobby's `LoadMap`, run 3's corrected by `dxmt/tools/fps.py`),
all at 1728x1117; the 2560x1440 runs are Task 3c (Ruling 15):

| Run | Lobby FPS (+20 s / +100 s) | Frame period, GPU busy (ms) | Upscale GPU ms per frame (p90) | GPU idle just before / after (ms) |
| --- | --- | --- | --- | --- |
| 1 | 32.04 / 59.37 (30 s window) | 19.73, 17.45 | 0.94 (0.98) | 0.00 / 0.00 |
| 2 | 59.98 / 59.80 | 16.66, 13.85 | 0.94 (0.97) | 0.00 / 0.00 |
| 3 (match) | — / 118.96 over the 8 s fps.py can count (118 by the trace) | 8.45, 8.20 | 1.01 (1.07) | 0.00 / 0.00 |
| 4 | 59.97 / 59.80 | 16.63, 14.41 | 0.93 (1.02) | 0.00 / 0.00 |
| `DXMT_STATS=1` | 59.97 / 59.80 | 16.64, 14.33 | 0.95 (1.03) | 0.00 / 0.00 |
| `DXMT_D3D12_PRIVATE=0` | 59.97 / 59.78 | 16.66, 14.59 | 0.97 (1.08) | 0.00 / 0.00 |

- - MetalFX labels its passes: the upscale is 3 compute passes a frame (`MetalFX_Temporal_BBR_Pre/Mid/PostProcessing`),
  0.95 ms end to end with no idle inside; the presenter's spatial scaler (`MetalFX_Scale`, `MetalFX_Sharpen`) adds 0.57
  ms. The lobby runs at its 60 FPS cap; run 1 was at ~32 for its first ~90 s. Run 3 was in the Jungle Practice match at
  about 118-121 FPS (the trace's 8.45 ms period; log samples of the draft and the match read 110-133), not an uncapped
  lobby, which explains its lighter frames. *Corrected in Task F3:* `fps4.py`'s 35.60 for run 3's +100 s window is a
  frame-counter wrap artefact, not a rate: Unreal logs the frame counter mod 1000, and fps4.py counted log gaps of 9-29
  s (over 1000 frames at 120 FPS) mod 1000. `dxmt/tools/fps.py` leaves out gaps that could hide a wrap: 118.96 for that
  window (over the 8 s of 72 it can count), and the lobby runs as before within 0.2 FPS. Its +20 s window (107.62) mixes
  the lobby's 60 FPS with draft and loading bursts of 120-310 (fps.py: 139.18 over the 19 s it can count), so it isn't a
  rate of either. Run 1's slow start is not explained.
- `DXMT_STATS=1`: `upscale output copied` 0 over 46 reports (MetalFX writes SMITE's output directly), 1.0 temporal
  upscale passes a frame. With `DXMT_D3D12_PRIVATE=0`: 12735 copied, FPS unchanged at the cap.
- XeSS init (`Loading XeSS library` to `XeSS successfully initialized`): 1 ms in every run; Intel's own XeSS took 15.0 s
  in the Gate S follow-up's prefix. The MetalFX upscaler is made at the first Execute (Ruling 17), outside that
  interval: ~13 ms warm, ~45 ms the first in a process.

**Split decision:** no split needed at 1728x1117 — the upscale costs ~0.95 ms of GPU time a frame and the GPU never
idles before or after it (the spike's ~2.2 ms commit-to-start wait doesn't appear inline). Task 4 doesn't run unless
the 2560x1440 runs say otherwise.

Also run after 0027: both lanes of `make dxmt-check` (`dxmt-check: all passed` twice, 236 ok per lane, no FAIL; every
other upscale and XeSS PSNR as in Task 2d), `make test` 239 passed, `make smoke` 15/15, `make bridge-check` 15 ok,
the Wine series from a fresh fetch (27 of 27, the applied tree), the translator key unchanged. `gpu-trace.py` gains
`--label SUBSTR`.

### Built-in display session (Task 3c)

2026-10-07, 12:13-12:39, the built-in display only (the external one is unavailable). SMITE 2 through a scratch tool
folder from `.build/release/macneutron install` with the current dev `wine.app` (DXMT 0029, Wine 0029), `env -i`,
Steam bridge on, main lobby only, camera idle. Prefixes are APFS clones of Task 3's `compat-l3` (fresh game defaults:
XeSS Balanced, borderless 1728x1117). Each run: wait for the lobby's `LoadMap`, bring the game window to the front
(System Events, every 10 s; the game was frontmost at the trace in every run but `blits1`), wait 60 s (150 s for
`bounds`), record a 6 s Metal System Trace, copy Hemingway.log, stop (`wineserver -k`/`-w`, `WINEMSYNC=1`; no
leftover pids after any run). Every run had `DXMT_STATS=1`; from `base2` on its report was saved under `DXMT_DXIL_DUMP`
(`base1`/`blits1` had no dump folder, so no report).
Frame period and GPU busy are `gpu-trace.py`'s medians (p90 from the same per-frame lists); FPS is `dxmt/tools/fps.py`
from 20 s after the lobby's `LoadMap` for 60 s (it includes the trace, except for `bounds`, whose trace ran ~150 s after
the lobby); the upscale is `--label MetalFX_Temporal`.

**Uncapping the lobby.** The fresh `.sav` has no `LobbyMaxFPS` entry, so the game applies its 60 FPS lobby cap. The
maintainer's old `.sav` (sp5/perf's `compat-x`) stores `LobbyMaxFPS` = `"0"` (version 1). The runs after `blits1`
use the fresh `.sav` with that one entry added to its `SavedSettingVersions` and `SavedSettingsConfig` maps (each
map's size and count adjusted; nothing else changed); the game loaded it and ran the lobby at 65-72 FPS. `base1` and
`blits1` ran before that, at the cap.

| Arm | Setup | FPS (fps.py / stats median) | Frame period ms (p90) | GPU busy ms (p90) | Upscale GPU ms (p90) | Timestamps given their own encoder per frame |
| --- | --- | --- | --- | --- | --- | --- |
| `base1` (capped) | default | 59.43 / — | 16.65 (17.07) | 13.50 (13.76) | 0.95 (0.98) | — |
| `blits1` (capped) | `DXMT_D3D12_TIMESTAMP_BLITS=1` | 59.30 / — | 16.62 (17.10) | 14.18 (14.53) | 0.94 (1.01) | — |
| `base2` | default | 67.96 / 65.5 | 14.59 (15.20) | 13.76 (14.12) | 0.95 (0.98) | 2.0 |
| `blits2` | `DXMT_D3D12_TIMESTAMP_BLITS=1` | 66.43 / 65.0 | 15.05 (15.65) | 13.90 (14.26) | 0.93 (0.97) | 100.6 |
| `base3` | default | 68.34 / 66.3 | 14.58 (15.20) | 13.75 (14.12) | 0.94 (0.97) | 2.0 |
| `blits3` | `DXMT_D3D12_TIMESTAMP_BLITS=1` | 66.92 / 65.1 | 15.03 (15.60) | 13.91 (14.24) | 0.85 (0.95) | 100.8 |
| `r-xess` | RetinaMode, ini windowed 2560x1440 (output inferred), XeSS Balanced | 55.93 / 55.9 | 17.76 (18.59) | 16.56 (16.96) | 1.76 (1.93) | 2.0 |
| `r-taa` | as `r-xess`, TAA | 36.63 / 36.9 | 26.45 (27.78) | 24.86 (25.34) | none (no temporal upscale) | 2.0 |
| `bounds` | `DXMT_DXIL_BOUNDS=off` | 68.52 / 67.7 | 14.54 (15.20) | 13.77 (14.13) | 0.93 (0.96) | 2.0 |

- **B4 (timestamps ride the next encoder) is kept.** Uncapped A/B/A/B: `DXMT_D3D12_TIMESTAMP_BLITS=1` gives 100.6-100.8
  timestamps their own encoder per frame (≈80 "several waiting", 16 "end of call", 4.7 "next encoder takes none")
  against 2.0 by default. It costs 0.45-0.47 ms of frame period (14.58-14.59 → 15.03-15.05), 0.15 ms of GPU busy and
  0.22-0.25 ms more GPU idle between encoders; 1.5 FPS by fps.py. The two default runs agree within 0.01 ms, as do
  the two blits runs; the GPU's clock state isn't controlled, though (the counter run below, same default settings,
  was 1.1 ms faster), so the 0.45 ms holds for this state and the sign for all. The capped pair isn't a measure of
  either (frame time is the cap's), and `blits1`'s window wasn't frontmost at the trace.
- **The split is still not needed (Task 4 doesn't run).** At 1728x1117 the upscale is 0.85-0.95 ms; at the 2560x1440
  arm (inferred output) it is 1.76 ms (p90 1.93), under the ≤ 5 ms criterion, and the GPU idle just before and just
  after it is 0.00 ms (p90 0.00) in every run with the upscale. The Retina arms' window and output size is the ini
  setting, not a logged 2560x1440: with RetinaMode the game saw a 3456x2234 desktop (`CacheSupportedResolutions`) and
  logged `ApplyResolution - Applying Resolution 1117,1728` four times in each Retina run (game-r-xess.log and
  game-r-taa.log :1401/1422/1454/1901), the same as `base2`; no log line says 2560x1440 (the `.sav`'s `Resolution`
  entry is an index, left alone, and `WINEDEBUG=warn+xess` printed nothing without the launcher's logging mode). The
  output is inferred at ≈3.6-3.7 MP: 1.76 ms is 1.85-1.89 times the 0.93-0.95 ms at 1728x1117 (2560x1440 predicts 1.81
  ms, a 1728x1117 output 0.95 ms, a 3456x2234 one ≈3.8 ms), and the TAA arm, the same prefix but for the `.sav`'s
  `XeSS` entry, runs at `p8`'s 2560x1440 TAA lobby numbers (26.45/24.86 against 26.17 p10/27.44 median and 25.23 ms
  frame/busy, resfps/bound.md:39). Even a 3456x2234 output stays under 5 ms.
- **XeSS Balanced vs native TAA at 2560x1440 (lobby; ini size, output inferred above):** 17.76 against 26.45 ms frame
  period, 16.56 against 24.86 ms GPU busy: XeSS through the bridge takes 8.7 ms off the lobby frame (55.9 against 36.6
  FPS). The TAA arm drops the `.sav`'s `XeSS` entry (both maps); no `MetalFX_Temporal` interval ran and DXMT counted
  no temporal upscale. Its 26.45/24.86 ms match sp5's `p8` 2560x1440 TAA lobby (26.17 p10 / 25.23 ms busy,
  resfps/bound.md:39). This is a lobby number: gframe's ≈ −2 ms estimate is for a match frame, which this session didn't
  measure.
- **Bounds checks: no-go for the `texture_buffer` form (gate 2).** `DXMT_DXIL_BOUNDS=off` keys its own shader cache
  (`d3d12_shader_cache.cpp`), so it compiled afresh: its first stats report ran 8.4 FPS, and FPS was mostly 67-70 (one
  61.7 report) for the last 40 s before the trace (150 s after the lobby; fps.py's 68.52 covers +20-80 s, the compile
  settling). Frame
  14.54 ms and GPU busy 13.77 ms against 14.58-14.59 and 13.75-13.76 for the default; the Vertex channel is 8.0 % of
  the window (≈1.16 ms a frame) against 7.8-7.9 % (≈1.14-1.15 ms): GPU busy and Vertex within ±0.03 ms (frame
  0.04-0.05 ms), far below the 0.15 ms threshold. This is the lobby, not the m3/E7 match spot gate 2 names; Ruling 35
  closes gate 2 on it (the static estimate, ≤ 0.1 ms, agrees).
- **GPU counters (gate 1): not obtained.** A run with `--instrument 'Metal GPU Counters'` added to the Metal System
  Trace template (`--attach`) recorded the `gpu-counter-info` and `gpu-counter-value` tables with no rows. Its frame was
  13.50 ms (GPU busy 12.78 ms, upscale 0.78 ms), faster than the default runs, which isn't explained. Gate 1 stays open.

## DXMT runtime fixes from the reviews and studies (XeSS plan, Task F1)

2026-10-06 (Ruling 21). DXMT patches 0010-0015, each with a row in `dxmt/check.sh`:
- 0010: ExecuteIndirect's resolver writes a VERTEX_BUFFER_VIEW argument at the slot's entry in the table of the slots
  the pipeline uses (it wrote the raw slot). `indirect vbv-gap` (slot 2 of a pipeline using slots 0 and 2) drew the
  IA-bound tags before (`2,0:207 3,0:200 2,1:208`) and draws D3DMetal's `2,0:107 3,0:300 2,1:108` now.
- 0011: a placed texture or buffer holds a private reference on its heap, so an app's release of the heap no longer
  takes it out of the residency set while its resources live (`Release` still answers 0, as on D3DMetal).
  `hazard heap-released` renders, samples and reads back 257 before and after (a destroyed heap showed nothing here);
  `DXMT_STATS` counted `heaps destroyed 1` before and none now.
- 0012: buffer `Map` refuses every heap the CPU can't see (a buffer placed in a CUSTOM heap with no CPU pages returned
  S_OK and a null pointer; D3DMetal maps it); `DXMT_D3D12_PRIVATE=0` also drops the RenderTarget usage 0009 gave
  renderable 2D UAVs (Ruling 14).
- 0013: the upscale's begin and end blits, and any blit or compute encoder left without commands, carry a 4-byte fill
  of a scratch buffer, as timestamp blits do, so Metal can't drop them with their fences.
- 0014: `DXMT_STATS` counts `upscale input copied` (a depth/stencil depth: 64 of 64; readable inputs: 0).
- 0015: ResizeBuffers keeps the swapchain flags it's given (`resize flags 0x2 0x2`; `0x0 0x0` before), and vsync
  pacing re-reads the window's display's refresh rate at ResizeBuffers and on leaving fullscreen (it was the creation
  display's, and off after leaving fullscreen). Not tested: no check moves a window between displays.

Bridge rows: `upscale direct` and `placed` match the copy path (`DXMT_D3D12_PRIVATE=0`) within 0.05 dB (24.90 each);
`upscale direct-offset` writes at (64, 32) of a 2624x1472 output directly (0 copies) at 24.90, as the copy path;
`xess nodepth` reads the bridge's own depth back: every texel 1.0 (inverted: 0.0).

Both lanes of `make dxmt-check`: `dxmt-check: all passed` twice, 244 ok per lane (Task 3b: 238), no FAIL; every
other upscale and XeSS PSNR as in Task 3b. The translator key is unchanged (no airconv change).

## Timestamps ride the next encoder (XeSS plan, Task F4)

2026-10-06 (Ruling 23; F1's leftovers, Rulings 24-25). DXMT patches 0016-0019, each with a row in `dxmt/check.sh`:
- 0016: ResizeBuffers keeps the FRAME_LATENCY_WAITABLE_OBJECT flag a swapchain was created with (DXGI can't add or
  remove it later): `resize waitable 0x42 1` (`0x2 0` before: the flag and the waitable object were lost).
- 0017: `DXMT_STATS` says why each lone timestamp blit was needed (`lone timestamps (another counter buffer)`,
  `(several waiting)`, `(next encoder takes none)`, `(end of call)`) and, for the base-pass merge (B6), why a render
  pass that could have joined the previous one's Metal pass didn't (`render pass merges refused (timestamps)`,
  `(resolver)`, `(barrier)`, `(attachments)`, `(other)`).
- 0018: each queue samples every timestamp, whatever its query heap, into one counter buffer (a 4096-slot ring: Metal
  allows 32 counter buffers per process, measured), so an encoder takes the timestamps waiting before it at its start
  and its own at its end in one attachment; those before a clear or resolve pass, or at the end of the call, ride the
  previous encoder's end. The queue writes each slot's value to its queries once the command buffer completes, and
  CPU resolves copy them. `DXMT_D3D12_TIMESTAMP_BLITS=1` restores the heaps' own counter buffers.
- 0019 (review fix): a call that samples timestamps owes that write on the CPU, as resolves already did, so its
  queue's fences signal after it (an app may release the query heap once the fence passed): `hazard sampled`, a
  timestamp sampled without a resolve, then Signal, counts `fence signals deferred to the CPU 1` (none on 0018).

Counts before (the same build with only 0017; `DXMT_STATS`, each run alone):

| Run | Lone timestamp blits | Why |
|---|---|---|
| `d3d12_hazards ts-many` (201 timestamps, two heaps, render/compute/copy, list start and end; the row's final form, 203 with a second heavy dispatch, counts 53 with the switch: + 1 several waiting) | 52 | 49 another counter buffer, 3 end of call |
| `d3d12_timestamp` | 3 | end of call (a list of timestamps alone) |
| `d3d12_hazards ts-start` | 2 | several waiting |
| `d3d12_hazards two-heaps` | 2 | another counter buffer |
| `d3d12_hazards after-own-blit` | 1 | another counter buffer |
| `d3d12_hazards deferred` | 1 | end of call (a list of a timestamp alone) |

After 0018: ts-many, ts-start, two-heaps and after-own-blit 0; d3d12_timestamp 1 and deferred 1 (calls with no other
encoder: one blit for all their timestamps). With `DXMT_D3D12_TIMESTAMP_BLITS=1`, the counts before. ts-many's 203
values are nonzero and in submission order, and the gap around its heavy dispatch is at least half the same dispatch's
time measured right after it in the list (about 134 000 ticks each; the GPU's clock moves the dispatch between 62 000
and 1 400 000 over runs, so a reference from another submission failed 1 run in 4 with DXMT_D3D12_OVERLAP=1).
`upscale timestamps` (two timestamps before each of 64 MetalFX upscales, one after) takes none of its own (128 with
the switch), in order, the upscale (about 7 ms) between the last two, and no validation message.

F1's leftovers: `hazard heap-released` places a texture in each of two heaps, releases both heaps, and the first
texture after the readback (`heaps destroyed 1`: held while it lives, freed after; 2 with the texture's hold removed,
measured on a dev build); `indirect vbv-gap` adds a VERTEX_BUFFER_VIEW for slot 5, above the pipeline's highest
used slot, and the next record still draws its own tags; `d3d12_xess`'s HookDepth table is pinned by a comment to
IMTLD3D12CommandListExt's 4 methods; the `d3d12_api library` row compares D3DMetal's lines but for its
`serialize-race` line, and pins ours to `overflow 0` (Ruling 25).

Both lanes of `make dxmt-check` on the applied build: `dxmt-check: all passed` twice, 246 ok per lane, no FAIL. The
translator key is unchanged (no airconv change).

## Shader translator fixes, one translator-key change (XeSS plan, Task F2)

2026-10-07 (Rulings 21-22; F4's leftovers). DXMT patches 0020-0026, each with a row in `dxmt/check.sh`. 0022-0025
change airconv, so the translator key changes once, on purpose (Ruling 22): `e66d8fd3…` → `b3eea49c…`; every user's
shader cache rebuilds once.
- 0020: a queue that never sampled timestamps (no counter ring) resolving timestamps another queue sampled read the
  heap's unused counter buffer: `hazard ts-queues` (queue A samples, queue B resolves behind a fence) read `0` before,
  `1` now. Only a queue whose ring couldn't be created falls back to the heap's buffer.
- 0021 and 0023: vertex buffer slots a pipeline reads but nobody bound point at a zeroed buffer with stride 0 (D3D12: a
  4 KB one, also for null VERTEX_BUFFER_VIEW arguments of ExecuteIndirect; D3D11: the zeroed dummy constant buffer),
  and pulled vertex attributes no longer check for a null buffer (the branch cost 3-5 % of vertex fetch in the
  vertex-fetch study). Unbound slots now read zeros widened by the format, as on D3DMetal: `vsread ia` and
  `d3d11 ia` (R32G32B32A32 bound, R32G32 and R32G32B32A32 never bound, R32_UINT a null view) read
  `1,2,3,4 0,0,0,1 0,0,0,0 0,0,0,1` (before: `0,0,0,0` for every unbound attribute; D3DMetal: as now).
  `indirect vbv-null` (a null view argument after a bound one): `3,0:300`, the stale tags not read. 0026 looks the
  zeroed buffer up once per vertex buffer table, not once per unbound slot and command (each lookup takes a lock).
- 0022: a non-precise `mad` (DXIL FMad without `!dx.precise`) becomes one `air.fma`, as Metal Shader Converter does;
  precise ones stay a multiply and an add. `dxil/mad.hlsl` (a = 1 + n 2^-13, b = 1 - n 2^-13, c = -1): against a CPU
  reference, `mad fused 64 precise 64 of 64` (before: 32, the odd threads rounding the product first); the group
  matches D3DMetal (it differed at word 0 before). Offline, four SMITE 2 shaders: a vertex shader 12 fma, a pixel
  shader 38, a compute shader 82, each replacing a multiply and an add.
- 0024: DXIL barriers without the sync bit (GroupMemoryBarrier 8, AllMemoryBarrier 10, DeviceMemoryBarrier 2)
  emit `air.atomic.fence` (they emitted nothing below Metal 3.2; mode 8 nothing at all). Offline: `dxil/barriers.hlsl`
  0 → 3 fences, SMITE 2's compute shader with 12 mode-8 barriers 0 → 12. The group matches D3DMetal (a guard: the
  results can't depend on a fence alone), so the fences have a row of their own: `dxil-translate --flags` now lists
  each `air.atomic.fence` (flags:scope) and counts `air.wg.barrier`, and dxmt-check wants `fence=2:1,5:3,7:3
  barrier=3` for `barriers.dxil` (RED against airconv at 0023: `fence=- barrier=3`).
- 0025: `DXMT_DXIL_BOUNDS=off` (measurement only) drops the typed `Buffer<>` read checks and the raw and structured
  ones; the shader cache keys it apart. `bounds` with it reads past views (`5 6 7 8 19 20 21 22 21 22 23 24 20 21 0 0
  13 14 15 16 17 18 19 20 20 19 20 0 1 2 3 4`, every read inside the test's buffer) and inside them as without it;
  `vsread` reads as without it. Offline, selects in SMITE 2's vertex shader 82 → 13, compute 79 → 24.

Both lanes of `make dxmt-check` on the applied build (0026): `dxmt-check: all passed` twice, 257 ok per lane (225
rows, 32 dxil-probe lines), no FAIL. `vbv-null` is one more ExecuteIndirect with a resolver pass, so the stats rows now
want 16390 calls, 3 resolve passes and 14 with `DXMT_D3D12_INDIRECT=icb`.

## Presenter, records and tools (XeSS plan, Task F3)

2026-10-07 (Rulings 21, 28). DXMT patch 0027; the translator key is unchanged (`b3eea49c…`).
- F2's open question: DXC's `-Gis` (IEEE strictness) marks every `mad` precise. `dxil/mad.hlsl` compiled with it has
  `!dx.precise` on both FMads (without it, one), and the entry point's shader flags read 16 in both (raw buffers;
  DisableMathRefactoring, 2, is never set), so 0022's `dx.precise` test already keeps `-Gis` shaders unfused and a
  module-flag gate would never fire. No airconv change. The dxmt-check row that counts the fast-math flags of vertex
  and geometry shaders is now named for what it checks: `vertex and geometry shaders carry no reassoc, contract or
  arcp flags` (a non-precise mad's `air.fma` is an explicit call).
- 0027: `IASetVertexBuffers` with no views unbinds the slots (it returned early, leaving the old views bound). `vsread
  unbind` (slots 1 and 3 bound to data, then `IASetVertexBuffers(1, 3, NULL)`) read `1,2,3,4 1,2,0,1 3,4,1,1.5
  1.07374e+09,0,0,1` before and reads `1,2,3,4 0,0,0,1 0,0,0,0 0,0,0,1` now, as unbound slots do. D3DMetal ignores
  such a call (it reads the old data), so the row has no D3DMetal twin.
- The presenter still scales into a texture of its own and copies it into the overlay's drawable (gframe's B1 not
  done): a CAMetalLayer drawable has managed storage (`framebufferOnly` or not; measured on this Mac), and MetalFX's
  spatial scaler wants a private output. Scaling straight into the drawable drew the right picture but 199 Metal
  validation messages in 200 frames ("outputTexture must have private storage mode"), so it isn't shipped.
- Task 3's section above is corrected: run 3 was the Jungle Practice match, `fps4.py`'s 35.60 is a counter-wrap
  artefact (about 118-121), and by the resolution study's evidence the in-game Resolution in borderless doesn't change
  the render size.
- `dxmt/tools/fps.py` (from `fps4.py`): frame rate from an Unreal log's mod-1000 frame counter, leaving out log gaps
  that could hide a wrap (longer than 1000 frames at 1.5x the window's highest rate between lines 0.5-2 s apart). Run 3
  +100 s: 118.96 (fps4.py 35.60); the lobby windows of Task 3 move by at most 0.2 FPS (run 1's +100 s reads 59.37
  under both tools).
- `dxmt/tools/gpu-trace.py` stops with `too few whole frames` when a trace has fewer than 2 whole frames (4 in all: the
  first and last are cut off, and a frame period needs two), instead of a `statistics` traceback.

Both lanes of `make dxmt-check` on the applied build (0027): `dxmt-check: all passed` twice, 264 ok per lane, no FAIL.

## The final review's findings (XeSS plan, Task FR)

2026-10-07 (Ruling 32). DXMT patches 0028-0029, Wine patch 0029; the translator key changes a second time before any
release: `b3eea49c…` → `735ba5c4…`.
- 0028: DeviceMemoryBarrierWithGroupSync and AllMemoryBarrierWithGroupSync fence device memory at Metal 3.1 (D3D12's
  version), then wait for the group, as Metal Shader Converter does; the fence used to come only at 3.2, so a group
  could read another group's stale results after the barrier. `dxil/barriers.hlsl` translates to
  `fence=2:1,5:3,5:3,7:3,7:3 barrier=3` (was `fence=2:1,5:3,7:3`). D3D11's DXBC sync does the same, and its
  GroupMemoryBarrier (a TGSM fence without the group sync) fences now (it emitted nothing): `dxbc/sync.hlsl`, compiled
  by Wine's D3DCompile into `sync.dxbc`, gave `fence=-` and gives the same line. `dxil-translate` takes DXBC compute
  shaders (`.dxbc`) for this.
- 0029: a command allocator released without a Reset runs its encoders' destructors. An XeSS Execute recorded into
  such an allocator's list kept its MetalFX upscaler from DXMT's pool: the next context's first Execute grew GPU
  memory by 232 MB; now by 0.
- Wine 0029 (`libxess`): Execute refuses (-4) a missing exposure texture or responsive mask that Init's flags ask for
  (it returned 0); the calls MetalFX has nothing for (exposure multiplier, responsive mask clip, legacy scale factors,
  network choice, dumps, profiling) answer a live context -7 NOT_IMPLEMENTED (was -8); native AA at 1728x1117 asks
  for 1728x1117 (was 1728x1116, a whole-frame resample: 35.26 dB against 31.83 now, 29.27 against 26.77 then); a
  callback registered at ERROR gets refusals at ERROR (it got none); xessGetPipelineBuildStatus answers -12 after Init
  (was 0).
- Tests that couldn't fail: `heap-released` no longer prints the heap's Release() answer (always 0, Ruling 24; the
  DXMT_STATS row discriminates); `nodepth` no longer reads the inverted depth back: fresh memory reads 0.0 uncleared
  too (a build that skipped that clear passed, even with a far plane at 1.0 released just before); the check's
  `lone` prints `no stats` when a run wrote none.
- The launcher's player-data carry: the rename writes `player-data-pending` before it moves `pfx`, and only that
  record carries, so an interrupted re-preparation of an arm64 prefix takes nothing from a `pfx.rosetta` beside it,
  and a launch stopped between the rename and the preparing stamp still carries; files are copied under a temporary
  name and renamed into place; several old users all go to the one user Wine reads.
- `licenses/NOTICES.md` quotes the Intel notice of the XeSS headers `libxess.dll` is built from; `licences_test.sh`
  checks for it.

Both lanes of `make dxmt-check` on the applied build (DXMT 0029, Wine 0029): `dxmt-check: all passed`, 264 ok per lane,
no FAIL. (The first ARM64EC run's replay row read 0: the launcher log rotated at 1 MB between that run's line count and
its replay, so the row read the new, one-line file; the replay line was in `launcher.log.1`, and the lane rerun passed.)

## CMAA2 post-AA in the presenter (XeSS plan, Task CMAA2; Ruling 36)

Per game, off by default: "Anti-aliasing (post): Off / CMAA2" in the Games window (`GameSettings.postAA`) or
`MACNEUTRON_POST_AA=cmaa2`. The presenter runs our Metal port of Intel's CMAA2 (`presenter/cmaa2.metal`, Apache-2.0,
Extra Sharpness, preset HIGH; `libmacneutron-present.metallib` beside the dylib) in place on every SDR frame, before
any MetalFX upscale: an 8-bit one through an sRGB view of the drawable, a 10-bit one (RGB10A2/BGR10A2, which have no
sRGB view) with the sRGB curve decoded and encoded in the shader and its blends kept at 10 bits (10:10:10:2 working
colours, a function constant). HDR layers pass through. With MetalFX off
the launcher still loads the presenter when post-AA is on, and the presenter skips only its upscale. Notices:
`licenses/macneutron/CMAA2-LICENSE.txt`, a NOTICES.md section, the README entry; `licences_test.sh` checks "CMAA2" and
the file (red before they existed, and its self-test deletes the file).

`make presenter-check` (the native part drives wine.app's presenter through its own present hook on layers outside any
window, `presenter/tests/cmaa2_check.m`; the Wine part runs `present_loop.exe` through the launcher), all ok:

| Check | Result |
| --- | --- |
| Off: dense and sparse test frames, 2560x1440 and 1728x1117 | byte for byte |
| Flat gradient frame, on | unchanged |
| Silhouette frame, on: pixels changed / more than 1 px from an edge | 6.70 ‰ / 0 (2560x1440); 6.72 ‰ / 0 (1728x1117) |
| 1-px glyph strokes, stroke-to-background contrast kept (through the sRGB view) | 83.3 % (2560x1440), 83.2 % (1728x1117); the study's raw-read probe: 85.9 % |
| RGB10A2 (10-bit SDR), both sizes: off / flat / silhouettes / glyphs | byte for byte / unchanged / 6.70 ‰ and 6.72 ‰, 0 far from edges / 83.3 % |
| RGB10A2: blended channel values that are codes no 8-bit value gives | 74.3 % (2560x1440), 74.4 % (1728x1117), ≥ 60 % required; blends packed through 8-bit sRGB (the 8-bit path's packing) give 27.6 % / 28.7 % |
| HDR (RGBA16Float) layer, on | unchanged, `left alone` logged, no CMAA2 |
| Upscale (640x360 to 1280x720): game-size drawable changed; MetalFX output against the off run | 30801 px changed; differs (CMAA2 runs before MetalFX) |
| Metal shader validation, then API validation, every native run | exit 0 (both layers at once crash inside MetalTools on a view of a drawable's texture, so they run one after the other) |
| Wine: full size, 640x360 upscaled, MetalFX off, fp16 | `CMAA2 1280x720` once; `CMAA2 640x360` before `MetalFX`, checkerboard intact; with `MACNEUTRON_NO_METALFX=1` CMAA2 and no MetalFX; fp16 no CMAA2 |

**Cost.** The check prints the GPU time of the present's command buffer, which holds only CMAA2: at 1728x1117 0.155 /
0.223 / 0.712 ms (flat / sparse / dense median), at 2560x1440 0.283 / 0.388 / 1.258 ms. That is 2.5-4x the study's
probe (0.059 / 0.090 / 0.330 at 2560x1440). The shader's own cost matches the probe: the same encoder, run from a
scratch bench on a drawable's texture that is not re-acquired each frame, costs 0.066 / 0.084 / 0.281 ms at 2560x1440.
Anything run on a freshly acquired, presented drawable of these offscreen layers is 3-5x slower (a plain 9-tap kernel:
0.13 ms, against 0.67 ms), and a blit into the drawable first in the same command buffer did not remove that. The
mechanism isn't pinned down, and a game acquires a fresh drawable every frame too, so the in-game cost is open: Phase
2's to measure (synthesis §6). On an RGB10A2 layer (the same harness): at 1728x1117 0.269 / 0.351 / 0.815 ms, at
2560x1440 0.492 / 0.581 / 1.411 ms; the shader's sRGB decode of every colour load is about 0.07-0.14 ms of that
(measured against a build without it).

Also: `make test` (246), `make smoke` 15/15, `make bridge-check` (all ok), `licences_test` and its self-test PASS. The
build is a development one (the DXMT and Wine trees carry another task's diagnostic commits), so `make wine-arm64`
rebuilds incrementally each time rather than saying up to date. Not yet seen in a game.

## Emulated fullscreen: background behaviour and per-size MetalFX fallback (XeSS plan, Task FS; Rulings 36-37)

- **EmulateModeset on by default.** Every prefix the launcher prepares gets `HKCU\Software\Wine\X11 Driver`
  `EmulateModeset=Y` beside `ShowCrashDialog` (`PrefixManager.prepareNew`): a game's exclusive Fullscreen changes
  Wine's virtual mode, scaled to the screen, never the Mac's display. Existing prefixes get it with the runtime that
  brings it (a new wine.app changes the prefix stamp, which prepares them again in place). RetinaMode stays off.
- **Black letterbox bars** (Wine 0030, `winemac: Fill fullscreen windows' letterbox area with black.`): exported as
  committed; seen only in the maintainer's game check.
- **Leaving fullscreen in the background minimises** (DXMT 0032, D3D11 and D3D12): when the game leaves fullscreen
  while its window isn't in front, the window goes to the Dock (`info:  Leaving fullscreen in the background:
  minimising`, once per leave) instead of sitting fullscreen-sized in a corner of the screen.
- **The windowed rect is saved before the mode change** (DXMT 0031): leaving fullscreen restores the window as it was,
  even when the game resized it on WM_DISPLAYCHANGE.
- **D3D12 presents to a minimised window are skipped** (DXMT 0033): S_OK (flip model), the back buffers rotate, the
  frame latency object is released, no drawable is asked for; a 60 Hz sleep stands in for the drawable's pacing.
- **Presenter:** a MetalFX refusal falls back to the linear filter for that drawable and target size only; the next
  size upscales again (was: the rest of the session).
- DXMT 0030 (`DXMT_MAX_ANISOTROPY`, a diagnostic switch, nothing changes when unset) is exported with them.

Checks (`dxmt/tests/d3d12_fullscreen.cpp`, run alone after the lanes; it refuses to run without EmulateModeset, so the
Mac's display never changes):

| Check | Before (red) | After |
| --- | --- | --- |
| Leave fullscreen in front (D3D12, D3D11; the window fits itself to each mode on WM_DISPLAYCHANGE) | `fg-leave 1 0 0,35,640,486` | `fg-leave 1 0 same` |
| Leave it with another window in front | `bg-leave 1 0`, no log line | `bg-leave 1 1`, one `Leaving fullscreen in the background` |
| 31 D3D12 frames to the minimised window, each waited on the latency object and fenced | presented through Metal (no skip counter) | `minimised presents 0x0 0 31 fast`, `presents skipped (window minimised) 31`, slowest ~20-26 ms |
| The restored window presents | `0x0` | `0x0` |
| Presenter: MetalFX made to refuse 1280x720 (`MACNEUTRON_PRESENT_REFUSE`, test-only), window resized to 960x540 | `linear filter` 1, `MetalFX 640x360 -> 960x540` 0 | 1, 1 |

`make dxmt-check` all passed in both lanes (x64 and ARM64EC programs), `make presenter-check` all ok (31), `make test`
(246), `make smoke` 15 PASS, `make bridge-check` all ok; translator key unchanged (`735ba5c4…`); Wine 30 of 30 and
DXMT 33 of 33 apply from the pins to the trees' exact trees. Not yet seen in a game: the maintainer's check is
bg-synthesis.md §3 (Cmd-Tab away and back twice, 30 s in the background, Dock and Mission Control, quit from the Dock),
plus the bar colour.

## Follow-ups before the push: anisotropic filtering, fullscreen, presenter, logging, gradient bias (XeSS plan, Task FU)

2026-10-08. DXMT patches 0034-0037; Wine unchanged (0030). Each item has a row that failed first, except 0035 (no
second display here) and the framebufferOnly gate (the test layers already have it off); the BGR10A2 rows are new
coverage.
- **Anisotropic filtering 16x by default** (maintainer's decision). `GameSettings.anisotropy`: none (16x), `game` (no
  override), `4`, `8`, `16`; anything else runs at 16x. It becomes `DXMT_MAX_ANISOTROPY` (DXMT 0030's floor on
  trilinear, non-comparison samplers; unset for `game`), under the launch options as every setting. Games window:
  "Anisotropic filtering: Game's choice / 4x / 8x / 16x (default)". Settings files from before load as 16x. dxmt-check:
  `lod k=8 s0 Sample` (a trilinear sampler, 8:1 footprint) is `1.00` through the launcher's default and `4.00` with
  `DXMT_MAX_ANISOTROPY=0` and on D3DMetal; every other `dxmt/check.sh` run passes `DXMT_MAX_ANISOTROPY=0`, so its
  comparisons with D3DMetal stay on the programs' own samplers.
- **Leaving fullscreen in the background is decided by process** (DXMT 0034): the game's own popup in front (a dialog,
  an overlay owned by its window) no longer minimises it; another process's window, or none, does. `popup-leave 1 1`
  before, `1 0` after (D3D12 and D3D11); `bg-leave 1 1` and the one log line unchanged. `dxgi.handleAltTab` keeps its
  exact-window check.
- **No minimise when fullscreen moves to another output** (DXMT 0035): `SetFullscreenState(TRUE, <other output>)`'s
  leave half. No row: this Mac has one display.
- **A skipped present to a minimised window counts as a frame** (DXMT 0036): DXMT_STATS's frame counts sum to 32 (the 31
  skipped and the restored one; 1 before), so its 5 s reports go on while the game is in the Dock.
- **Gradient samples keep the sampler's MipLODBias** (DXMT 0037, airconv): DXIL SampleGrad and DXBC sample_d scale both
  derivatives by 2^bias, as D3D adds the bias to their LOD and Metal's gradient sample takes none. `d3d12_lod` (the blur
  study's sweep): `k=8 s6 SampleGrad` (anisotropy 4, bias -1, 8:1) `2.00` → `1.00`, D3D's value (D3DMetal: 2);
  `k=1 s1` `1.00` → `0.00`, `k=1 s2` `1.00` → `2.00`; 84 cases, every one without a sampler bias or not by gradients
  as D3DMetal. Offline (`dxil-translate --flags`): `grad=7/7` for `lod.ps.dxil`, `grad=1/1` for `dxbc/grad.dxbc`
  (`0/1` before). This changes the translator key, `735ba5c4…` → `d6863ecc…` (unreleased: the last release's users
  rebuild their shader caches once with this one anyway).
- **The fullscreen test's guard** reads EmulateModeset as Wine does, the program's `AppDefaults` key first (`guard
  emulated` before, `guard not emulated` after).
- **Presenter**: a refused size asks MetalFX once (`test refusal at 1280x720` 1, before 0: no count); a refusal that
  doesn't depend on the size (no MetalFX on the GPU, a pixel format it can't scale; `MACNEUTRON_PRESENT_REFUSE=all`)
  lasts for the layer (`linear filter` 1, `MetalFX` 0; before: 0, 2). CMAA2: framebufferOnly is turned off only for
  the 8- and 10-bit layers CMAA2 handles; every frames run counts pixels whose alpha changed (0; a mutant writing
  alpha 0 counts 290673); BGR10A2 has its rows now. `postAA` values other than `cmaa2` are off in the launch variables
  and the Games window.
- **Logging**: `MACNEUTRON_LOG=1` sets `WINEDEBUG=warn+all,warn-seh,+loaddll,warn+steamclient` (no seh warnings,
  Unreal's OutputDebugString flood, and no per-frame steamclient trace: SMITE 2's log had reached 917 MB);
  `MACNEUTRON_LOG=2` keeps `warn+all,+loaddll,+steamclient`. `steam-<appid>.log` past 50 MB moves to
  `steam-<appid>.log.1` at the next launch (one old file kept).

`make dxmt-check` (dev build of 0034-0037): `dxmt-check: all passed` in both lanes (x64 and ARM64EC programs), 290
and 280 ok, the new rows in both. A first run failed one timing row in the x64 lane, `and with
DXMT_D3D12_TIMESTAMP_BLITS=1, 53 get blits of their own` (`hazard ts-many 0`: `heavy 133750 alone 1329583`, the
dispatch measured alone ten times its usual ~134000 ticks while other lanes ran); alone it passed 5 of 5 (`alone`
133833-220208) and the whole check passed on the rerun. `make presenter-check` 62 ok, `make test` 251 tests,
`make smoke` 15 PASS, `make bridge-check` 15 ok and `PASS probe redaction`, `licences_test` PASS. DXMT 37 of 37 and
Wine 30 of 30 apply from the pins to the trees' exact trees.

## The maintainer's gate (XeSS plan, Task 5)

2026-10-07/08, SMITE 2 in a scratch copy on the built-in display (the external display was unavailable; the maintainer
accepted judging there), the dev runtime of each step, Steam bridge on, practice and lobby only.

- **Image:** XeSS Balanced through the bridge at 1728x1117 and, with RetinaMode, 3456x2234; then emulated exclusive
  Fullscreen (EmulateModeset) at 1728x1080 and 2560x1440 with black letterbox bars, minimise on Cmd-Tab and a scaled
  return, and 16x anisotropic filtering: "looked pretty good". CMAA2 on SMITE's 10-bit swapchain (no 8-bit step):
  "Looks good to me". The earlier "textures are blurry" report traced to Metal's anisotropic filter cancelling
  Unreal's negative LOD bias on angled surfaces; 16x by default (Task FU) moves that loss to much steeper angles.
- **Memory:** several 10-15-minute runs, footprint flat after the lobby load (about 6.0-6.5 GB, GPU 0.8-1.1 GB) and
  clean quits. The 30-minute soak was waived by the maintainer (Ruling 42); one attempt recorded nothing because the
  sampler's process pattern didn't match Wine's command line.
- **Upscale cost:** 0.93-1.01 ms at 1728x1117 output and 1.76 ms at ~2560x1440 (Tasks 3, 3c): under the spec's 5 ms.

## msync wait-all fixes (batch Task 1)

2026-10-09. Wine patch 0031 (`msync: Fix wait-all wakeups, rollback and duplicate objects.`); DXMT, FEX unchanged.
`x64-sync` has 19 gated rows (5 new) and 7 time rows (3 new); `check.sh msync` also gates mode 1's duplicate answers
(`MSYNC_MODES` picks the modes; default `1 0`). Every new row failed first in mode 1 and passed in mode 0 on the 0030
runtime (`build/batch-msync/red10k-*.log`, `red-mode0-1.log`):
- **`wait-all-single-waiter`** (B1): `FAIL … auto-event round 0 (A first): the single waiter still asleep 100 ms after
  the signal` in 6 of 6 mode-1 runs; `ok` after.
- **`wait-all-rollback-wake`** (B2): `stalls 19`, `3`, `1` (2,000 releases: `0`, `2`, `6`, so `RELEASES` is 10,000);
  `stalls 0`, no timeouts after.
- **`wait-all-owned-mutex`** (B3): `probe-hits` 110672168, 125748292, 136435857 and `1 owner releases failed` (one
  rollback freed the owner's mutex for good); `probe-hits 0` after.
- **`wait-all-duplicate`** (B4): mode 1 `auto-event spun semaphore success mutex success` → `invalid-parameter` for
  all three; mode 0 `success` for all three, before and after (the wineserver has no duplicate check).
- **`wait-all-abandoned-mutex`** (B7): `still running after 1 s` → `ok` (`WAIT_ABANDONED_0`, the mutex released).
- B6 (a mutex taken abandoned goes back abandoned) has no deterministic trigger; it is read in the diff. R5 and R9,
  static: `_mach_port_mod_refs` in `ntdll.so`'s imports 0 → 1; `_msync_abandon_mutexes`: `stlr` 0 → 1, the merged
  `str d8, [x21]` 1 → 0.

**B1 shipped as option (a), wake-all for every type on both sides** (as CrossOver 26.3). Option (b), the Ruling R11
default (non-alertable wait-all legs parked through the pump), passed every row but failed the bar: mode 1's
`wait-all-wake` 10566-11288 ns against mode 0's 9139-9222 in the same three runs (`green-b-*.log`). Under (a) every
waiter parked on an object's word wakes at each signal and races for the object, so a wait-all leg that only looks at
it can no longer take the one wake a single waiter needed: who gets an auto-reset event or a mutex is settled by the
take, not by the kernel's choice of one sleeper, at the price of waking them all: `auto-handoff-8`, 8 threads on one
auto-reset event, 692 (681–713) ns per handoff against (b)'s wake-one 626 (617–633) in the same machine state.

| Row (ns), median (min–max) of 3 | mode 1 before | mode 1 after | mode 0 after |
|---|---|---|---|
| `wait-all-wake` | 4804 (3383–4918) | 5822 (5494–5845) | 9278 (9224–9386) |
| `wait-all-poll` | 113 (89–121) | 118 (118–118) | 8768 (8730–8850) |
| `auto-handoff-8` | 634 (631–657) | 692 (681–713) | 7632 (7550–7722) |

Option (b), same three-run format: mode 1 `wait-all-wake` 11274 (10566–11288), `wait-all-poll` 98 (96–101),
`auto-handoff-8` 626 (617–633); mode 0 9196 (9139–9222), 8644 (8618–8766), 7552 (7520–7624).

All on AC power, lid open, idle, each run under `caffeinate -i`, back to back ("before": the 10,000-release harness on
the 0030 runtime; "after": the dev build of 0031). The 2,000-release "before" runs measured `wait-all-wake` 3420
(3358–3430), `wait-all-poll` 86 (86–90), `auto-handoff-8` 614 (602–702); against them `wait-all-poll` after lies
wholly above. The machine moved between the sets: the control rows this patch doesn't touch read `uncontended-wait` /
`uncontended-signal` 62-65 / 44-46 in 2,000-release runs 1-2 and 10,000-release run 1, 114-125 / 77-91 in the other
three before runs, and 136-143 / 99-100 in every run after (the sync report's band: 102-143 / 77-101). The poll path
does strictly less work after (one index compare instead of a `single_waiters` increment and decrement), and (a) and
(b) run the same poll code yet measured 118 and 96-101: a build or machine effect, not the path's work.

`w1-wins` / `w2-wins` per run (10,000 releases):

| Run | rollback-wake | owned-mutex |
|---|---|---|
| before 1-3 (mode 1) | 1102/8899, 919/9082, 1042/8959 | 1023/8978, 938/9063, 1124/8877 |
| after (a) 1-3, mode 1 | 991/9010, 1006/8995, 913/9088 | 3346/6655, 3467/6534, 3600/6401 |
| after (a) 1-3, mode 0 | 4831/5170, 4850/5151, 4784/5217 | 5001/5000 ×3 |
| (b) 1-3, mode 1 | 1536/8465, 1483/8518, 1389/8612 | 7173/2828, 7366/2635, 7227/2774 |

(2,000 releases, before: rollback-wake 221/1780, 228/1773, 224/1777; owned-mutex 216/1785, 233/1768, 214/1787. The
wins add up to one more than the releases: the shutdown releases X once more.) The msync step, both modes, stays far
under its 300 s cap (a whole `check.sh msync` run, with `boot` and `fex`: 77-78 s).

`make wine-arm64-check` (dev build of 0031): every step `PASS`, then `PASS orphans` (2134 s), its `msync` step with
the 19 rows in both modes and mode 1's duplicate line; `make test` 251 tests, `make smoke` 15 PASS, `make bridge-check`
15 ok and `PASS probe redaction`; `make media-check` as before (`FAIL media-mf: FAIL arm64-media-mf: stage=video-type
hr=0xc00d5212; FAIL x64-media-mf: stage=video-type hr=0xc00d5212`). Wine 31 of 31 apply from the pin to the build
tree's exact tree.

## Measurement lanes: baseline (batch Task 2)

2026-10-09; no runtime change. The bundle measured is the applied build of Wine 0031 that Task 1 left (`wine.applied`
2c06186, `build/wine-arm64/version` `c12564359f1e…`, `SOURCE` at 8c3aa6e), staged by `make wine-arm64` before this
task's edits.

`make lanes-check` (`check.sh lanes`, run by name, outside the full run) runs each program from one source in three
lanes, ARM64, ARM64EC and x64 under FEX (its defaults: the caller's `FEX_*` variables are dropped), in mode 1 against
the prefix's server, with `WINEDEBUG=-all` and the crash dialog off. It passes when each program prints
`PASS <program>` and its 16 time rows, and writes each row as `info <program> <row> <ns>`:
- `x64-sync`, `arm64ec-sync`, `arm64-sync`: Task 1's 19 gated rows, which pass in all three lanes, and its 7 time rows,
  plus `cs-uncontended`, `srw-uncontended`, `cs-contended-4`, `srw-contended-4`, `waitonaddress-uncontended`,
  `waitonaddress-contended-4`, `wait-any-wake`, `alertable-wake` and `auto-pool-8` (Ruling R20). Each is the median of
  21 batches, in ns per operation. The PASS and FAIL lines name the program.
- `arm64-xcall`, `arm64ec-xcall`, `x64-xcall` (`-O2 -fno-builtin`, so each CRT routine is a call): 16 rows of calls
  into Wine's DLLs, each the median of 100 batches after one of warm-up, in ns per call.

The new `x64-sync` rows also run in the `msync` step's x64 lane, in both modes. The whole `check.sh msync` run (with
`boot` and `fex`) went from 77-78 s to 104-106 s (4 runs), so its 300 s cap stays.

The baseline is three runs of `caffeinate -i sh wine-arm64/check.sh lanes`, back to back (10:16-10:24). Each run was
on AC power at its start and end, with the lid open. Before each, `pgrep -lx wineserver` and
`pgrep -lf wine-arm64/check.sh` printed nothing. The load average was 4.2-4.6, from processes outside the task. The
logs are in `build/lanes/baseline/run{1,2,3}.log`. The table is
`python3 wine-arm64/tools/lanes_report.py build/lanes/baseline`, with cells `median (min–max)` of the three runs in ns
and the last two columns differences of medians:

| row | arm64 | arm64ec | x64 | arm64ec − arm64 | x64 − arm64ec |
|---|---|---|---|---|---|
| sync uncontended-wait | 83.0 (77.0–84.0) | 76.0 (73.0–78.0) | 127 (110–142) | -7.0 | +51.0 |
| sync uncontended-signal | 38.0 (37.0–38.0) | 42.0 (36.0–43.0) | 90.0 (84.0–99.0) | +4.0 | +48.0 |
| sync cross-process-wake | 4252 (553–4907) | 4245 (4140–4351) | 4366 (4246–4462) | -7.0 | +121 |
| sync create-close | 28048 (28030–28077) | 28292 (28232–28319) | 28710 (28653–28807) | +244 | +418 |
| sync wait-all-wake | 5903 (5820–6190) | 6148 (5854–6161) | 5953 (5640–6051) | +245 | -195 |
| sync wait-all-poll | 85.0 (77.0–85.0) | 89.0 (88.0–89.0) | 122 (120–130) | +4.0 | +33.0 |
| sync auto-handoff-8 | 642 (617–644) | 724 (638–740) | 703 (681–725) | +82.0 | -21.0 |
| sync cs-uncontended | 12.0 (12.0–13.0) | 13.0 (10.0–14.0) | 85.0 (77.0–87.0) | +1.0 | +72.0 |
| sync srw-uncontended | 31.0 (28.0–32.0) | 33.0 (26.0–34.0) | 108 (100–112) | +2.0 | +75.0 |
| sync cs-contended-4 | 30.0 (28.0–30.0) | 31.0 (28.0–31.0) | 200 (197–201) | +1.0 | +169 |
| sync srw-contended-4 | 96.0 (84.0–100) | 97.0 (95.0–102) | 192 (190–194) | +1.0 | +95.0 |
| sync waitonaddress-uncontended | 18.0 (17.0–18.0) | 17.0 (15.0–18.0) | 63.0 (63.0–64.0) | -1.0 | +46.0 |
| sync waitonaddress-contended-4 | 4652 (4630–4862) | 4616 (4598–4659) | 4617 (4593–4716) | -36.0 | +1.0 |
| sync wait-any-wake | 7899 (7652–8284) | 7676 (7484–7940) | 7729 (7668–7774) | -223 | +53.0 |
| sync alertable-wake | 7817 (7308–8016) | 7810 (7600–7888) | 7856 (7756–8014) | -7.0 | +46.0 |
| sync auto-pool-8 | 50943 (50940–50953) | 48953 (47954–49872) | 50954 (50946–50962) | -1990 | +2001 |
| xcall get-current-thread-id | 0.7 (0.7–0.7) | 1.7 (1.7–1.8) | 24.6 (24.3–24.8) | +1.0 | +22.9 |
| xcall get-last-error | 0.9 (0.9–0.9) | 2.8 (2.4–2.8) | 25.6 (25.6–25.7) | +1.9 | +22.8 |
| xcall tls-get-value | 0.9 (0.9–0.9) | 2.5 (2.2–2.5) | 25.2 (24.6–26.1) | +1.6 | +22.7 |
| xcall get-tick-count | 0.7 (0.7–0.7) | 1.8 (1.7–1.8) | 24.5 (24.4–24.8) | +1.1 | +22.7 |
| xcall qpc | 15.4 (15.4–15.4) | 16.8 (16.8–16.9) | 43.3 (43.0–43.4) | +1.4 | +26.5 |
| xcall istream-addref | 3.5 (3.5–3.5) | 3.5 (3.5–3.5) | 27.2 (27.1–27.2) | 0.0 | +23.7 |
| xcall memcpy-16 | 2.0 (1.9–2.2) | 3.0 (2.6–3.0) | 25.9 (25.7–25.9) | +1.0 | +22.9 |
| xcall memcpy-16-offset | 2.8 (2.8–3.3) | 3.6 (3.5–4.1) | 27.1 (26.9–27.4) | +0.8 | +23.5 |
| xcall memcpy-256 | 3.9 (3.7–4.3) | 4.6 (4.3–5.0) | 28.0 (27.8–28.1) | +0.7 | +23.4 |
| xcall memcpy-256-offset | 5.5 (5.0–7.8) | 6.2 (6.2–6.3) | 29.5 (29.3–29.7) | +0.7 | +23.3 |
| xcall memcpy-4k | 43.6 (43.5–43.9) | 45.2 (45.1–47.7) | 57.8 (57.8–58.0) | +1.6 | +12.6 |
| xcall memcpy-4k-offset | 45.7 (45.4–47.7) | 40.1 (40.0–46.5) | 74.0 (73.9–74.3) | -5.6 | +33.9 |
| xcall memcpy-1m | 15670 (15480–15710) | 15340 (15250–15700) | 15300 (15290–15320) | -330 | -40.0 |
| xcall memcpy-1m-offset | 15140 (14220–15320) | 15760 (15620–15770) | 16300 (16090–16350) | +620 | +540 |
| xcall strlen-1k | 235 (235–236) | 237 (237–237) | 262 (262–271) | +2.4 | +25.1 |
| xcall qsort-4k | 104860 (104720–106830) | 164640 (163050–168000) | 3552710 (3547900–3558450) | +59780 | +3388070 |

- x64 − arm64ec is FEX's entry and the x64→EC transition: +22.7 to +22.9 ns on the four trivial exports
  (`get-current-thread-id`, `get-last-error`, `tls-get-value`, `get-tick-count`).
- arm64ec − arm64 is the EC thunks and the aux-IAT checker: +1.0 to +1.9 ns on the same four.
- x64 `istream-addref`, 27.2 ns, is the bare crossing: a vtable slot with no fast-forward sequence. The body,
  `InterlockedIncrement`, is 3.5 ns in both native lanes.
- x64 `get-current-thread-id` − x64 `istream-addref` is the fast-forward sequence's cost: 24.6 − 27.2 = −2.6 ns. The
  two bodies differ by 2.8 ns (3.5 against 0.7 in the ARM64 lane), so the sequence costs about 0.2 ns, within the
  noise.
- x64 `get-last-error` − x64 `get-current-thread-id` is the alias hop: +1.0 ns (ARM64EC +1.1, ARM64 +0.2).

`qsort-4k`'s comparator is the program's own code. In the x64 lane each of its ~49,000 comparisons crosses back from
the ARM64EC `qsort` into x64 code: 3.55 ms against 0.16 ms in the ARM64EC lane, about 69 ns per comparison.

**`auto-pool-8` (Ruling R20)** measures what option (a), every wake a wake-all, costs a parked pool:
- 8 workers wait in `WaitForSingleObject(e, 1000)` on one auto-reset event.
- Main sets it every 50 µs, spinning on `QueryPerformanceCounter`, and only once the last set was taken.
- The worker that takes it does 10 µs of busy work.
- A batch is 10,000 sets.

The row is (process CPU − main's CPU) per set. R20 named `GetThreadTimes` for main's share, but Wine on macOS has no
per-thread CPU time: `get_thread_times()` in `dlls/ntdll/unix/thread.c` is implemented only for Linux and FreeBSD, and
`ThreadTimes` on the current thread falls back to the process's `times()`. Every run's `info auto-pool-8` line shows
`main-thread-cpu` equal to `process-cpu`. Main spins for the whole batch, so the row takes its wall time as its CPU
time. `times()` counts 10 ms ticks, so a 0.5 s batch resolves 1 µs per set.

A scratch check in the ARM64 and x64 lanes confirmed the method:
- The cost per set doesn't follow the period: 49.7, 52.0 and 54.9 µs at 25, 50 and 100 µs.
- It grows with the number of waiters: 13.0, 16.5, 25.5 and 52.0 µs with 1, 2, 4 and 8.
- It is 0.0 with main alone, or with 8 workers parked on an event nobody sets.

Under (a), then, each extra parked waiter costs about 5.6 µs of CPU per set. The 8-waiter pool spends about 51 µs per
set, of which 10 µs is the work, where a single waiter spends 13 µs.

In the `msync` step's x64 lane (three runs back to back, AC, idle), mode 1 measured 51945 (51944–51948) ns per set and
mode 0 12958 (12943–12961). Mode 0's number leaves out the wineserver's CPU, because the server is another process.

`make test` passed 251 tests. `check.sh msync` printed `PASS msync` and `PASS orphans` in all four runs, both modes,
with all 19 gated rows and the 16 time rows.

## PE M1 baseline (batch Task 3)

2026-10-09. Two changes, both on the arm64 Windows (PE) side:
- **Wine patch 0032** (`include: Use acquire/release for ReadAcquire and WriteRelease on ARM64EC.`). ARM64EC code
  defines `__x86_64__`, so `winnt.h`'s x86 branch made ReadAcquire, WriteRelease and their 64-bit and pointer forms
  plain loads and stores in ARM64EC code. The guard is now `(defined(__x86_64__) && !defined(__arm64ec__)) ||
  defined(__i386__)`, the idiom of `InterlockedCompareExchange128`.
- **The instruction set.** Every PE build targets Apple M1's, `-march=armv8.5-a+fp16fml+aes+sha3`, so atomics are
  LSE instructions instead of LL/SC loops. Tuning stays generic: Apple tuning crashes llvm-mingw's SEH unwind emitter.
  - Wine (and lsteamclient, one of its DLLs) gets it as `aarch64_CFLAGS` and `arm64ec_CFLAGS` (Ruling R25), not in
    `CROSSCFLAGS`. `--enable-archs=arm64ec` brings in an x86_64 extra arch for ARM64EC modules' x64 sources (9 compiles
    of 5 sources, such as `ntdll/signal_x86_64.c`). `CROSSCFLAGS` reaches that x86 compiler too, which rejects an ARM
    `-march`.
  - FEX gets it as `-DCMAKE_C_FLAGS`/`-DCMAKE_CXX_FLAGS`, keeping `-DTUNE_CPU=none`.
  - DXMT gets it from its ARM64X cross file (DXMT patch 0038, `build: Target the Apple M1 instruction set in the ARM64X
    cross file.`). `build.sh` now hashes that cross file into DXMT's setup inputs.
  - `steam.exe`, its test helper and the ARM64EC DXMT test programs get it from the `Makefile`'s `PE_MARCH`.
  - The `wine-arm64/tests` programs (the lanes) keep their flags, so before/after measures the runtime.

`build.sh` now runs `pe_baseline_check` (`lib.sh`) on what Wine's `make` built. It fails the build unless:
- ntdll's ARM64EC `sync.o` holds acquire loads and release stores (0032);
- ntdll.dll's ARM64 and ARM64EC code (the ranges in its CHPE CodeMap) both hold LSE atomics.

It failed first on the 0031 build with `ntdll's arm64ec sync.o: 0 acquire loads, 0 release stores (include/winnt.h's
ARM64EC guard)`. With 0032 alone, it failed with `ntdll.dll's ARM64 code: 0 LSE atomics (the PE side isn't on the M1
baseline)`. It passes on the full rebuild.

**Codegen**, instruction counts from `llvm-objdump -d` (patterns as in `pe_baseline_check`; LL/SC = `ldaxr`/`stlxr`):

| Binary | LSE | LL/SC | acquire | release |
|---|---|---|---|---|
| `ntdll.dll`, ARM64 range | 0 → 124 | 236 → 0 | 19 → 19 | 4 → 4 |
| `ntdll.dll`, ARM64EC range | 0 → 124 | 236 → 0 | 1 → 20 | 0 → 4 |
| ntdll's ARM64EC `sync.o` | 0 → 62 | 114 → 0 | 0 → 14 | 0 → 3 |
| `libarm64ecfex.dll` | 0 → 317 | 1304 → 749 | 360 → 372 | 98 → 98 |
| DXMT's `aarch64-windows/d3d11.dll` (ARM64X) | 0 → 840 | 2750 → 1374 | 610 → 630 | 74 → 74 |

The ranges come from ntdll.dll's CHPE CodeMap. Before, they were `0x1000-0x72930` and `0x73000-0xE0028`; after,
`0x1000-0x72EA8` and `0x73000-0xE05A0`. "Before" was measured on the 0031 build, and its ntdll rows equal the plan's
bb65ef0 figures.
The LL/SC left in FEX and DXMT is most likely code the flag doesn't compile (not traced instruction by instruction):
- Both link llvm-mingw's prebuilt libc++ statically (neither imports a C++ DLL), and its
  `aarch64-w64-mingw32/lib/libc++.a` holds 1438 LL/SC and 0 LSE.
- FEX also has 47 `wfe` exclusive-monitor waits.

Other checks:
- `build.ninja` carries the flag on 108 of DXMT's lines and 231 of FEX's.
- `make -n -B` of the ARM64EC `present_loop`/`d3d12_api` and `bridge` prints it 4 times.
- `make -n -B` of the lanes' ARM64 and ARM64EC programs prints it 0 times.
- No Apple tuning appears in any build file, and the build logs have no SEH unwind or backend error.

**Licence re-scan** (native REPORT §5 item 8). The flag can't change what links: `ff_crc32_aarch64`'s references
follow `av_crc`, not the flag, and Wine's zlib is built `-DZ_SOLO`. `llvm-nm` over every file of the bundle's
`lib/wine/aarch64-windows` found no `ff_crc32_aarch64`. It scanned 1003 files; the positive control, `colorcnv.dll`,
has 15029 symbols.

**Gates**, on the development build (both trees committed, not yet exported; `DXMT/version`
`1fba8d25b5e29ab49012d633676a6b0d4b3b96c5+dev`), with Steam running:
- `make wine-arm64-check` passed every step. It rebuilt the ARM64EC DXMT test programs with `PE_MARCH`. Also passed:
  `translator_key_test`, `mode_test`, `profile_test` and both `licences_test` runs.
  - Both lanes recorded a D3D11 frame time of 8.3 ms, against 4.4-4.7 ms in Task 1's gate. That is not graded, and it
    is not this change. 8.3 ms is one 120 Hz refresh. Earlier gates on builds without the flag recorded 8.3-8.9 ms too
    (the video plan's `t1-wine-arm64-check-dxmt-x64-run1.log`, `t1r1-check-steps-after-dxmt-present.log`).
- `make test`: 251 tests passed.
- `make smoke`: 15/15.
- `make bridge-check`: 15 `ok`. `steam.exe` is now built with `PE_MARCH`.
- `make media-check`: unchanged,
  `FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf: stage=video-type hr=0xc00d5212`.

**Export.** Wine is 32/32 and DXMT 38/38 from a fresh fetch, each with a `HEAD^{tree}` equal to the build tree's. The
applied build's `DXMT/version` is `1fba8d25b5e29ab49012d633676a6b0d4b3b96c5+8f881ce0db2b`.

**The lanes, before and after.** "Before" is Task 2's baseline (Wine 0031, no flag). "After" is three runs of
`caffeinate -i sh wine-arm64/check.sh lanes` on the applied build above, back to back (13:00-13:08). Each run was on AC
at its start and end, with the lid open. Before each, `pgrep -lx wineserver` and `pgrep -lf wine-arm64/check.sh`
printed nothing. The one-minute load average was 3.7-5.2, against the baseline's 4.2-4.6. The test programs are Task
2's: their sources and flags didn't change, and the exes differ from the baseline's only in the PE timestamp. The logs
are in `build/lanes/m1/run{1,2,3}.log`. The table is
`python3 wine-arm64/tools/lanes_report.py build/lanes/baseline build/lanes/m1`, in ns:

| row | lane | before | after | Δ % | outside the band |
|---|---|---|---|---|---|
| sync uncontended-wait | arm64 | 83.0 (77.0–84.0) | 88.0 (76.0–88.0) | +6.0 | no |
| sync uncontended-wait | arm64ec | 76.0 (73.0–78.0) | 76.0 (66.0–80.0) | +0.0 | no |
| sync uncontended-wait | x64 | 127 (110–142) | 142 (128–143) | +11.8 | no |
| sync uncontended-signal | arm64 | 38.0 (37.0–38.0) | 43.0 (37.0–43.0) | +13.2 | no |
| sync uncontended-signal | arm64ec | 42.0 (36.0–43.0) | 42.0 (37.0–42.0) | +0.0 | no |
| sync uncontended-signal | x64 | 90.0 (84.0–99.0) | 99.0 (94.0–99.0) | +10.0 | no |
| sync cross-process-wake | arm64 | 4252 (553–4907) | 4208 (3544–4294) | -1.0 | no |
| sync cross-process-wake | arm64ec | 4245 (4140–4351) | 4138 (4132–4176) | -2.5 | no |
| sync cross-process-wake | x64 | 4366 (4246–4462) | 4290 (3670–4549) | -1.7 | no |
| sync create-close | arm64 | 28048 (28030–28077) | 28144 (25459–28163) | +0.3 | no |
| sync create-close | arm64ec | 28292 (28232–28319) | 28247 (28200–28427) | -0.2 | no |
| sync create-close | x64 | 28710 (28653–28807) | 28947 (25790–29106) | +0.8 | no |
| sync wait-all-wake | arm64 | 5903 (5820–6190) | 5888 (5877–6110) | -0.3 | no |
| sync wait-all-wake | arm64ec | 6148 (5854–6161) | 5844 (3290–6130) | -4.9 | no |
| sync wait-all-wake | x64 | 5953 (5640–6051) | 5764 (5315–5966) | -3.2 | no |
| sync wait-all-poll | arm64 | 85.0 (77.0–85.0) | 86.0 (85.0–87.0) | +1.2 | no |
| sync wait-all-poll | arm64ec | 89.0 (88.0–89.0) | 87.0 (58.0–88.0) | -2.2 | no |
| sync wait-all-poll | x64 | 122 (120–130) | 116 (116–131) | -4.9 | no |
| sync auto-handoff-8 | arm64 | 642 (617–644) | 627 (619–692) | -2.3 | no |
| sync auto-handoff-8 | arm64ec | 724 (638–740) | 627 (536–666) | -13.4 | no |
| sync auto-handoff-8 | x64 | 703 (681–725) | 686 (664–702) | -2.4 | no |
| sync cs-uncontended | arm64 | 12.0 (12.0–13.0) | 6.0 (3.0–6.0) | -50.0 | yes |
| sync cs-uncontended | arm64ec | 13.0 (10.0–14.0) | 7.0 (4.0–7.0) | -46.2 | yes |
| sync cs-uncontended | x64 | 85.0 (77.0–87.0) | 81.0 (80.0–81.0) | -4.7 | no |
| sync srw-uncontended | arm64 | 31.0 (28.0–32.0) | 13.0 (8.0–13.0) | -58.1 | yes |
| sync srw-uncontended | arm64ec | 33.0 (26.0–34.0) | 14.0 (8.0–14.0) | -57.6 | yes |
| sync srw-uncontended | x64 | 108 (100–112) | 92.0 (92.0–92.0) | -14.8 | yes |
| sync cs-contended-4 | arm64 | 30.0 (28.0–30.0) | 104 (100–107) | +246.7 | yes |
| sync cs-contended-4 | arm64ec | 31.0 (28.0–31.0) | 98.0 (84.0–99.0) | +216.1 | yes |
| sync cs-contended-4 | x64 | 200 (197–201) | 198 (197–200) | -1.0 | no |
| sync srw-contended-4 | arm64 | 96.0 (84.0–100) | 101 (101–104) | +5.2 | yes |
| sync srw-contended-4 | arm64ec | 97.0 (95.0–102) | 87.0 (74.0–90.0) | -10.3 | yes |
| sync srw-contended-4 | x64 | 192 (190–194) | 201 (200–202) | +4.7 | yes |
| sync waitonaddress-uncontended | arm64 | 18.0 (17.0–18.0) | 11.0 (11.0–11.0) | -38.9 | yes |
| sync waitonaddress-uncontended | arm64ec | 17.0 (15.0–18.0) | 14.0 (13.0–14.0) | -17.6 | yes |
| sync waitonaddress-uncontended | x64 | 63.0 (63.0–64.0) | 56.0 (55.0–56.0) | -11.1 | yes |
| sync waitonaddress-contended-4 | arm64 | 4652 (4630–4862) | 4628 (4612–4667) | -0.5 | no |
| sync waitonaddress-contended-4 | arm64ec | 4616 (4598–4659) | 4563 (4546–4669) | -1.1 | no |
| sync waitonaddress-contended-4 | x64 | 4617 (4593–4716) | 4711 (4682–4741) | +2.0 | no |
| sync wait-any-wake | arm64 | 7899 (7652–8284) | 7848 (7584–8224) | -0.6 | no |
| sync wait-any-wake | arm64ec | 7676 (7484–7940) | 7656 (7470–8046) | -0.3 | no |
| sync wait-any-wake | x64 | 7729 (7668–7774) | 7772 (7754–7890) | +0.6 | no |
| sync alertable-wake | arm64 | 7817 (7308–8016) | 7786 (7635–8096) | -0.4 | no |
| sync alertable-wake | arm64ec | 7810 (7600–7888) | 7778 (7759–7853) | -0.4 | no |
| sync alertable-wake | x64 | 7856 (7756–8014) | 8014 (7639–8134) | +2.0 | no |
| sync auto-pool-8 | arm64 | 50943 (50940–50953) | 50970 (50952–51942) | +0.1 | no |
| sync auto-pool-8 | arm64ec | 48953 (47954–49872) | 49948 (47967–49956) | +2.0 | no |
| sync auto-pool-8 | x64 | 50954 (50946–50962) | 50957 (50950–51940) | +0.0 | no |
| xcall get-current-thread-id | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-current-thread-id | arm64ec | 1.7 (1.7–1.8) | 1.7 (1.7–1.7) | +0.0 | no |
| xcall get-current-thread-id | x64 | 24.6 (24.3–24.8) | 24.2 (24.2–24.4) | -1.6 | no |
| xcall get-last-error | arm64 | 0.9 (0.9–0.9) | 0.9 (0.9–0.9) | +0.0 | no |
| xcall get-last-error | arm64ec | 2.8 (2.4–2.8) | 2.4 (2.4–2.8) | -14.3 | no |
| xcall get-last-error | x64 | 25.6 (25.6–25.7) | 25.8 (25.6–25.9) | +0.8 | no |
| xcall tls-get-value | arm64 | 0.9 (0.9–0.9) | 0.9 (0.9–0.9) | +0.0 | no |
| xcall tls-get-value | arm64ec | 2.5 (2.2–2.5) | 2.5 (2.4–2.5) | +0.0 | no |
| xcall tls-get-value | x64 | 25.2 (24.6–26.1) | 24.6 (24.4–25.1) | -2.4 | no |
| xcall get-tick-count | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-tick-count | arm64ec | 1.8 (1.7–1.8) | 1.8 (1.8–1.9) | +0.0 | no |
| xcall get-tick-count | x64 | 24.5 (24.4–24.8) | 24.5 (24.3–24.7) | +0.0 | no |
| xcall qpc | arm64 | 15.4 (15.4–15.4) | 15.4 (15.4–16.7) | +0.0 | no |
| xcall qpc | arm64ec | 16.8 (16.8–16.9) | 16.9 (16.9–17.2) | +0.6 | no |
| xcall qpc | x64 | 43.3 (43.0–43.4) | 43.4 (43.2–43.5) | +0.2 | no |
| xcall istream-addref | arm64 | 3.5 (3.5–3.5) | 1.5 (1.5–1.6) | -57.1 | yes |
| xcall istream-addref | arm64ec | 3.5 (3.5–3.5) | 1.5 (1.5–1.6) | -57.1 | yes |
| xcall istream-addref | x64 | 27.2 (27.1–27.2) | 24.8 (24.8–24.8) | -8.8 | yes |
| xcall memcpy-16 | arm64 | 2.0 (1.9–2.2) | 2.1 (1.8–2.2) | +5.0 | no |
| xcall memcpy-16 | arm64ec | 3.0 (2.6–3.0) | 2.8 (2.6–3.0) | -6.7 | no |
| xcall memcpy-16 | x64 | 25.9 (25.7–25.9) | 25.6 (25.5–25.9) | -1.2 | no |
| xcall memcpy-16-offset | arm64 | 2.8 (2.8–3.3) | 3.0 (2.8–3.3) | +7.1 | no |
| xcall memcpy-16-offset | arm64ec | 3.6 (3.5–4.1) | 4.1 (3.4–4.2) | +13.9 | no |
| xcall memcpy-16-offset | x64 | 27.1 (26.9–27.4) | 27.1 (27.1–27.3) | +0.0 | no |
| xcall memcpy-256 | arm64 | 3.9 (3.7–4.3) | 3.5 (3.5–3.8) | -10.3 | no |
| xcall memcpy-256 | arm64ec | 4.6 (4.3–5.0) | 5.0 (4.1–5.2) | +8.7 | no |
| xcall memcpy-256 | x64 | 28.0 (27.8–28.1) | 27.8 (27.6–27.9) | -0.7 | no |
| xcall memcpy-256-offset | arm64 | 5.5 (5.0–7.8) | 5.2 (4.6–6.0) | -5.5 | no |
| xcall memcpy-256-offset | arm64ec | 6.2 (6.2–6.3) | 6.5 (5.8–6.7) | +4.8 | no |
| xcall memcpy-256-offset | x64 | 29.5 (29.3–29.7) | 29.2 (29.1–29.3) | -1.0 | no |
| xcall memcpy-4k | arm64 | 43.6 (43.5–43.9) | 44.3 (43.9–45.2) | +1.6 | no |
| xcall memcpy-4k | arm64ec | 45.2 (45.1–47.7) | 45.2 (45.0–45.2) | +0.0 | no |
| xcall memcpy-4k | x64 | 57.8 (57.8–58.0) | 57.6 (57.3–61.3) | -0.3 | no |
| xcall memcpy-4k-offset | arm64 | 45.7 (45.4–47.7) | 47.9 (45.6–48.8) | +4.8 | no |
| xcall memcpy-4k-offset | arm64ec | 40.1 (40.0–46.5) | 41.1 (40.5–41.3) | +2.5 | no |
| xcall memcpy-4k-offset | x64 | 74.0 (73.9–74.3) | 74.5 (74.2–89.9) | +0.7 | no |
| xcall memcpy-1m | arm64 | 15670 (15480–15710) | 15510 (15480–16780) | -1.0 | no |
| xcall memcpy-1m | arm64ec | 15340 (15250–15700) | 15090 (13770–16770) | -1.6 | no |
| xcall memcpy-1m | x64 | 15300 (15290–15320) | 15260 (15260–15350) | -0.3 | no |
| xcall memcpy-1m-offset | arm64 | 15140 (14220–15320) | 15330 (14850–15390) | +1.3 | no |
| xcall memcpy-1m-offset | arm64ec | 15760 (15620–15770) | 15500 (14130–15760) | -1.6 | no |
| xcall memcpy-1m-offset | x64 | 16300 (16090–16350) | 16360 (16280–16520) | +0.4 | no |
| xcall strlen-1k | arm64 | 235 (235–236) | 234 (233–234) | -0.4 | yes |
| xcall strlen-1k | arm64ec | 237 (237–237) | 236 (236–244) | -0.3 | no |
| xcall strlen-1k | x64 | 262 (262–271) | 262 (262–262) | -0.1 | no |
| xcall qsort-4k | arm64 | 104860 (104720–106830) | 107980 (105460–109690) | +3.0 | no |
| xcall qsort-4k | arm64ec | 164640 (163050–168000) | 164470 (162830–164850) | -0.1 | no |
| xcall qsort-4k | x64 | 3552710 (3547900–3558450) | 3371800 (3361860–3371990) | -5.1 | yes |

What moved outside the band:
- **Uncontended locks got faster** in the native lanes, x64 less so:
  - `cs-uncontended`: 12 → 6 ns (ARM64) and 13 → 7 (ARM64EC); x64 isn't outside the band.
  - `srw-uncontended`: 31 → 13, 33 → 14 and 108 → 92 (x64).
  - `waitonaddress-uncontended`: 18 → 11, 17 → 14 and 63 → 56.
- **`cs-contended-4` got slower in both native lanes**: 30 → 104 ns (ARM64, +247 %) and 31 → 98 (ARM64EC, +216 %).
  The x64 lane's 200 → 198 didn't move.
  - The ARM64 lane changed only in its instruction set: 0032 touches ARM64EC code alone. So the ARM64 lane's
    regression comes from the flag.
  - The row is 4 threads doing 10,000 lock/increment/unlock cycles each, timed as throughput.
    `InitializeCriticalSection` is `RtlInitializeCriticalSection`, which sets spin count 0. So
    `RtlEnterCriticalSection` takes the lock with an `InterlockedIncrement` of `LockCount`. In the ARM64 `sync.o`
    that is now one `ldaddal`.
  - A likely reading, not verified: under LL/SC, the releasing thread mostly took the lock again before a woken
    waiter ran. With LSE the lock changes hands more often.
- **`srw-contended-4`** moved a little, in mixed directions: +5 % (ARM64), −10 % (ARM64EC), +5 % (x64).
- **`waitonaddress-contended-4`** didn't move outside the band in any lane.
- **xcall:**
  - `istream-addref`, an `InterlockedIncrement` that is now one LSE add: 3.5 → 1.5 ns in both native lanes, and
    27.2 → 24.8 under FEX.
  - x64 `qsort-4k`: −5.1 %.
  - ARM64 `strlen-1k`: −0.4 %, a 1 ns step between tight ranges.
  - No other xcall row moved outside the band.
- `auto-handoff-8` (ARM64EC 724 → 627) and the sleeping wakes (`wait-all-wake`, `wait-any-wake`, `alertable-wake`,
  `cross-process-wake`) stayed inside the band.

## FEX volatile metadata (batch Task 4)

2026-10-09. **Step 8 run with the maintainer (2026-10-09, SMITE 2 A/B on the 0.3.0 rehearsal runtime): no regression
with the metadata on; escape hatch FEX_VOLATILEMETADATA=0.** Ruling R33 had waived the A/B to ship every FEX change in
0.3.0; the maintainer then ran it before the release (Step 8, below). FEX 0006 is exported from `macneutron` f59f44c, on
4adb8a1 (0005). It was acba840 before the review's fix round (below), then aba3995; f59f44c corrects only aba3995's
message. The final gate ran on a bundle built from the applied trees.

FEX patch 0006 (`Core: Run every block a volatile-metadata range covers without TSO.`), ours and local only (Ruling
R15):
- **The block rule.** FEX ran a block without TSO only when the block held a listed volatile access. So a block the
  metadata marks safe kept TSO, and EVMD's documented range and module forms (`Config.json.in:582-597`) did nothing:
  they list no instruction. Now every block inside a valid range runs without TSO, except for its listed accesses and
  the accesses MonoHacks flags with `FLAG_FORCE_TSO` (Unity's SPSC ring buffer), which keep it. A block that straddles a
  range's edge keeps the default, TSO on. Listed GPR accesses keep TSO; vector accesses follow VectorTSOEnabled; x87
  FIST/FISTP/FISTTP stores follow VectorTSOEnabled; MOVS/STOS, with or without REP, follow MemcpySetTSOEnabled.
  VectorTSOEnabled is off by default: the JIT's vector TSO loads and stores add their barriers only under it
  (`MemoryOps.cpp:848`), and FIST/FISTP/FISTTP stores are non-TSO without it (`X87.cpp:182`; FISTTP is `FIST` with
  truncation, `X87Tables.cpp:412`, with the 64-bit and 16-bit forms at `:475` and `:527`). REP MOVS/STOS take their
  ordering from MemcpySetTSOEnabled alone: the backend reads it and never sees ForceTSO (`MemoryOps.cpp:1854, 2089`).
  Plain MOVS/STOS are non-TSO while MemcpySetTSOEnabled is off, the default (`Config.json.in:484`); with it on they
  take the AutoTSO loads and stores (`OpcodeDispatcher.cpp:3223, 3295`), whose `IsTSOEnabled` checks ForceTSO first
  (`OpcodeDispatcher.h:2431-2441`), so in a covered range the block rule decides: TSO when listed, none when not.
- **Ranges stay inside their image.** The PE tables' ranges and EVMD's are clamped to the mapping and empty ones
  dropped; PE-listed instructions outside the image are dropped. The image's unmap, when FEX sees it on a thread with
  FEX state, removes the ForceTSO ranges and instructions over its whole view (`HandleImageUnmap` →
  `RemoveForceTSOInformation`; verified by reading), and the coverage line never counts more than SizeOfImage. An
  unmap from a thread without FEX state skips that removal (`ARM64EC/Module.cpp:847`, upstream and pre-existing), so
  its ranges would outlive it into a later mapping at the same base.
- **Only a bare module name** (`mod`) disables TSO for the whole module. `mod;` and `mod;;<instructions>` list no range
  (before, both fell back to the whole module, and the second dropped its instructions).
- **`FEX_VOLATILEMETADATA=0`** skips an image's PE tables again; EVMD, an explicit setting, still applies. No binary
  in the repository carries PE metadata, so the PE side of the switch was checked by reading it; SMITE 2's run B
  (Step 8) is its first run. A litmus run with
  `FEX_VOLATILEMETADATA=0` and the whole-module EVMD still logged its coverage line and showed MP reordering (3,642 of
  10⁶).
- **The coverage line.** With `FEX_SILENTLOG=0`, FEX prints one line per image with metadata or EVMD, for example
  `I 284 volatile metadata: x64-litmus.exe at 140000000: 0 instructions, 1 ranges, 98304 bytes`.

**SMITE 2's metadata** (Step 1, the controller, read-only): `Hemingway-Win64-Shipping.exe` has
`VolatileMetadataPointer: 0x14AC6715C`. That is non-zero, so its MSVC tables now take effect. Step 8 ran the game both
ways (below).

**The step.** `check.sh fex-vmd` is the first of the batch's gated steps (`BATCH`), run after the rest. It runs
x64-litmus with EVMD over the whole module, then the same range with every instruction of the litmus kernels `run` and
`worker` listed (262), then the same range with only MP's flag accesses listed, in address order run's load and worker's
store (`0x1680,0x18b2`), MSVC's shape, where listed accesses order their unlisted neighbours. It then runs x64-bench
three times with FEX's defaults and three times with ranges over its two scalar-memory kernels, alternating:
`x64-bench.exe;0x38e0-0x3950,0x3950-0x39b0`. `mem_seq_read` and `mem_seq_write` are now `NOINLINE`, so each has a symbol
of its own. Every check runs, and the last line names each failure. `VMD_CALIBRATE=1` adds three runs with
`FEX_TSOENABLED=0`, reported and not gated. TSO off everywhere breaks x64-bench's own SPSC ring
(`mt_spsc_ring: item … read out of order`, after the mem_seq rows), so those runs' missing rows are an `info` line,
not a failure.

**RED** (`VMD_CALIBRATE=1 sh wine-arm64/check.sh fex-vmd`, before 0006 and before the fix round) failed on exactly the
five expected checks: the whole-module litmus count (MP forbidden=0), the two missing coverage lines and both
`mem_seq_*` ratios. The listed control passed at 0. FEX's old line was a `DFmt` that does print (`MSG_LEVEL` is INFO,
above DEBUG), but it read `Loaded volatile metadata for 140000000: 0 entries`. **Calibration:** with TSO off everywhere,
the kernels take 0.50 (`mem_seq_read`) and 0.39 (`mem_seq_write`) of their default time, so the bar `VMD_RATIO=0.75` can
pass. An earlier RED run, before the tso-off gating fix, gave 0.50 and 0.40.

**GREEN** (`sh wine-arm64/check.sh fex-vmd g2-litmus`, 0006 in a development build): `PASS g2-litmus` (the default
run's MP, LB, 2+2W and IRIW at 0 of 10⁷; the control at MP 22,990), `PASS fex-vmd`, `PASS orphans`. With their ranges,
both kernels ran at the speed TSO off gave them in RED (0.460 against 0.461 s, 0.395 against 0.389 s).

| Row (s, median of 3) | before 0006: default | before 0006: with its range | before 0006: `FEX_TSOENABLED=0` | 0006: default | 0006: with its range | ratio |
|---|---|---|---|---|---|---|
| x64-bench `mem_seq_read` | 0.917 | 0.925 | 0.461 | 0.950 | 0.460 | 0.48 |
| x64-bench `mem_seq_write` | 1.003 | 1.003 | 0.389 | 1.025 | 0.395 | 0.39 |

| x64-litmus, MP forbidden of 10⁷ | before 0006 (Step 3) | 0006 (Step 5) |
|---|---|---|
| no EVMD (G2's default run) | 0 | 0 |
| EVMD over the whole module | 0 | 19,170 |
| the same, `run` and `worker` listed | 0 | 0 |
| the same, only run's flag load and worker's flag store listed | — (the check post-dates Step 3) | — (added in the fix round: 0 there on acba840 and aba3995, and 0 at the final gate) |

**The lanes, before and after.** "Before" is Task 3's after-runs (`build/lanes/m1/`), "after" three runs of
`caffeinate -i sh wine-arm64/check.sh lanes` on the 0006 development build, back to back (13:54-14:03). Each run was
on AC at its start and end, with the lid open. Before each, `pgrep -lx wineserver` and `pgrep -lf wine-arm64/check.sh`
printed nothing. The one-minute load average was 2.95-4.89, against Task 3's 3.7-5.2. The test programs are Task 2's
(not rebuilt; x64-bench isn't a lane). The logs are in `build/lanes/t4/run{1,2,3}.log`. The table is
`python3 wine-arm64/tools/lanes_report.py build/lanes/m1 build/lanes/t4`, in ns:

| row | lane | before | after | Δ % | outside the band |
|---|---|---|---|---|---|
| sync uncontended-wait | arm64 | 88.0 (76.0–88.0) | 68.0 (67.0–80.0) | -22.7 | no |
| sync uncontended-wait | arm64ec | 76.0 (66.0–80.0) | 76.0 (68.0–81.0) | +0.0 | no |
| sync uncontended-wait | x64 | 142 (128–143) | 128 (126–143) | -9.9 | no |
| sync uncontended-signal | arm64 | 43.0 (37.0–43.0) | 33.0 (33.0–38.0) | -23.3 | no |
| sync uncontended-signal | arm64ec | 42.0 (37.0–42.0) | 42.0 (38.0–42.0) | +0.0 | no |
| sync uncontended-signal | x64 | 99.0 (94.0–99.0) | 88.0 (88.0–99.0) | -11.1 | no |
| sync cross-process-wake | arm64 | 4208 (3544–4294) | 4158 (4141–4256) | -1.2 | no |
| sync cross-process-wake | arm64ec | 4138 (4132–4176) | 4124 (4121–4267) | -0.3 | no |
| sync cross-process-wake | x64 | 4290 (3670–4549) | 4285 (4264–4357) | -0.1 | no |
| sync create-close | arm64 | 28144 (25459–28163) | 27613 (27401–27858) | -1.9 | no |
| sync create-close | arm64ec | 28247 (28200–28427) | 28074 (27739–28574) | -0.6 | no |
| sync create-close | x64 | 28947 (25790–29106) | 28798 (28569–28878) | -0.5 | no |
| sync wait-all-wake | arm64 | 5888 (5877–6110) | 5538 (1142–5759) | -5.9 | yes |
| sync wait-all-wake | arm64ec | 5844 (3290–6130) | 5991 (5910–6150) | +2.5 | no |
| sync wait-all-wake | x64 | 5764 (5315–5966) | 5226 (5141–5633) | -9.3 | no |
| sync wait-all-poll | arm64 | 86.0 (85.0–87.0) | 86.0 (78.0–97.0) | +0.0 | no |
| sync wait-all-poll | arm64ec | 87.0 (58.0–88.0) | 88.0 (86.0–89.0) | +1.1 | no |
| sync wait-all-poll | x64 | 116 (116–131) | 111 (111–117) | -4.3 | no |
| sync auto-handoff-8 | arm64 | 627 (619–692) | 620 (612–643) | -1.1 | no |
| sync auto-handoff-8 | arm64ec | 627 (536–666) | 618 (602–690) | -1.4 | no |
| sync auto-handoff-8 | x64 | 686 (664–702) | 664 (655–718) | -3.2 | no |
| sync cs-uncontended | arm64 | 6.0 (3.0–6.0) | 6.0 (6.0–6.0) | +0.0 | no |
| sync cs-uncontended | arm64ec | 7.0 (4.0–7.0) | 6.0 (6.0–7.0) | -14.3 | no |
| sync cs-uncontended | x64 | 81.0 (80.0–81.0) | 81.0 (81.0–82.0) | +0.0 | no |
| sync srw-uncontended | arm64 | 13.0 (8.0–13.0) | 14.0 (13.0–14.0) | +7.7 | no |
| sync srw-uncontended | arm64ec | 14.0 (8.0–14.0) | 12.0 (12.0–14.0) | -14.3 | no |
| sync srw-uncontended | x64 | 92.0 (92.0–92.0) | 92.0 (91.0–93.0) | +0.0 | no |
| sync cs-contended-4 | arm64 | 104 (100–107) | 105 (102–108) | +1.0 | no |
| sync cs-contended-4 | arm64ec | 98.0 (84.0–99.0) | 100 (97.0–102) | +2.0 | no |
| sync cs-contended-4 | x64 | 198 (197–200) | 198 (192–200) | +0.0 | no |
| sync srw-contended-4 | arm64 | 101 (101–104) | 103 (100–105) | +2.0 | no |
| sync srw-contended-4 | arm64ec | 87.0 (74.0–90.0) | 86.0 (85.0–89.0) | -1.1 | no |
| sync srw-contended-4 | x64 | 201 (200–202) | 200 (198–201) | -0.5 | no |
| sync waitonaddress-uncontended | arm64 | 11.0 (11.0–11.0) | 11.0 (11.0–12.0) | +0.0 | no |
| sync waitonaddress-uncontended | arm64ec | 14.0 (13.0–14.0) | 15.0 (15.0–15.0) | +7.1 | yes |
| sync waitonaddress-uncontended | x64 | 56.0 (55.0–56.0) | 59.0 (55.0–60.0) | +5.4 | no |
| sync waitonaddress-contended-4 | arm64 | 4628 (4612–4667) | 4645 (4602–4706) | +0.4 | no |
| sync waitonaddress-contended-4 | arm64ec | 4563 (4546–4669) | 4678 (4610–4707) | +2.5 | no |
| sync waitonaddress-contended-4 | x64 | 4711 (4682–4741) | 4765 (4756–4771) | +1.1 | yes |
| sync wait-any-wake | arm64 | 7848 (7584–8224) | 7759 (7562–8204) | -1.1 | no |
| sync wait-any-wake | arm64ec | 7656 (7470–8046) | 7924 (7658–7959) | +3.5 | no |
| sync wait-any-wake | x64 | 7772 (7754–7890) | 7889 (7812–7966) | +1.5 | no |
| sync alertable-wake | arm64 | 7786 (7635–8096) | 7930 (7585–8178) | +1.8 | no |
| sync alertable-wake | arm64ec | 7778 (7759–7853) | 7552 (7501–8087) | -2.9 | no |
| sync alertable-wake | x64 | 8014 (7639–8134) | 8021 (7886–8027) | +0.1 | no |
| sync auto-pool-8 | arm64 | 50970 (50952–51942) | 50947 (50945–50949) | -0.0 | yes |
| sync auto-pool-8 | arm64ec | 49948 (47967–49956) | 49946 (49943–49948) | -0.0 | no |
| sync auto-pool-8 | x64 | 50957 (50950–51940) | 51940 (51923–51943) | +1.9 | no |
| xcall get-current-thread-id | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-current-thread-id | arm64ec | 1.7 (1.7–1.7) | 1.7 (1.7–1.7) | +0.0 | no |
| xcall get-current-thread-id | x64 | 24.2 (24.2–24.4) | 24.3 (24.2–24.4) | +0.4 | no |
| xcall get-last-error | arm64 | 0.9 (0.9–0.9) | 0.9 (0.9–0.9) | +0.0 | no |
| xcall get-last-error | arm64ec | 2.4 (2.4–2.8) | 2.4 (2.4–3.0) | +0.0 | no |
| xcall get-last-error | x64 | 25.8 (25.6–25.9) | 25.6 (25.6–25.9) | -0.8 | no |
| xcall tls-get-value | arm64 | 0.9 (0.9–0.9) | 0.9 (0.9–0.9) | +0.0 | no |
| xcall tls-get-value | arm64ec | 2.5 (2.4–2.5) | 2.5 (2.4–2.6) | +0.0 | no |
| xcall tls-get-value | x64 | 24.6 (24.4–25.1) | 24.6 (24.4–25.1) | +0.0 | no |
| xcall get-tick-count | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-tick-count | arm64ec | 1.8 (1.8–1.9) | 1.7 (1.7–1.8) | -5.6 | no |
| xcall get-tick-count | x64 | 24.5 (24.3–24.7) | 24.0 (23.9–24.0) | -2.0 | yes |
| xcall qpc | arm64 | 15.4 (15.4–16.7) | 15.4 (15.4–15.8) | +0.0 | no |
| xcall qpc | arm64ec | 16.9 (16.9–17.2) | 16.9 (16.9–17.9) | +0.0 | no |
| xcall qpc | x64 | 43.4 (43.2–43.5) | 43.4 (43.2–43.9) | +0.0 | no |
| xcall istream-addref | arm64 | 1.5 (1.5–1.6) | 1.5 (1.5–1.6) | +0.0 | no |
| xcall istream-addref | arm64ec | 1.5 (1.5–1.6) | 1.5 (1.5–1.7) | +0.0 | no |
| xcall istream-addref | x64 | 24.8 (24.8–24.8) | 24.8 (24.8–24.8) | +0.0 | no |
| xcall memcpy-16 | arm64 | 2.1 (1.8–2.2) | 2.0 (2.0–2.3) | -4.8 | no |
| xcall memcpy-16 | arm64ec | 2.8 (2.6–3.0) | 3.0 (3.0–3.3) | +7.1 | no |
| xcall memcpy-16 | x64 | 25.6 (25.5–25.9) | 26.1 (25.8–26.3) | +2.0 | no |
| xcall memcpy-16-offset | arm64 | 3.0 (2.8–3.3) | 3.0 (3.0–3.2) | +0.0 | no |
| xcall memcpy-16-offset | arm64ec | 4.1 (3.4–4.2) | 3.5 (3.3–4.3) | -14.6 | no |
| xcall memcpy-16-offset | x64 | 27.1 (27.1–27.3) | 27.3 (27.2–27.3) | +0.7 | no |
| xcall memcpy-256 | arm64 | 3.5 (3.5–3.8) | 3.5 (3.5–4.1) | +0.0 | no |
| xcall memcpy-256 | arm64ec | 5.0 (4.1–5.2) | 4.9 (4.8–5.3) | -2.0 | no |
| xcall memcpy-256 | x64 | 27.8 (27.6–27.9) | 27.8 (27.8–27.9) | +0.0 | no |
| xcall memcpy-256-offset | arm64 | 5.2 (4.6–6.0) | 5.4 (5.1–5.4) | +3.8 | no |
| xcall memcpy-256-offset | arm64ec | 6.5 (5.8–6.7) | 6.2 (5.7–6.4) | -4.6 | no |
| xcall memcpy-256-offset | x64 | 29.2 (29.1–29.3) | 29.6 (29.5–30.2) | +1.4 | yes |
| xcall memcpy-4k | arm64 | 44.3 (43.9–45.2) | 44.1 (44.0–44.2) | -0.5 | no |
| xcall memcpy-4k | arm64ec | 45.2 (45.0–45.2) | 47.8 (45.3–48.1) | +5.8 | yes |
| xcall memcpy-4k | x64 | 57.6 (57.3–61.3) | 58.0 (58.0–59.5) | +0.7 | no |
| xcall memcpy-4k-offset | arm64 | 47.9 (45.6–48.8) | 45.6 (45.6–45.7) | -4.8 | no |
| xcall memcpy-4k-offset | arm64ec | 41.1 (40.5–41.3) | 40.2 (39.9–40.3) | -2.2 | yes |
| xcall memcpy-4k-offset | x64 | 74.5 (74.2–89.9) | 74.4 (73.6–79.2) | -0.1 | no |
| xcall memcpy-1m | arm64 | 15510 (15480–16780) | 15530 (15330–15780) | +0.1 | no |
| xcall memcpy-1m | arm64ec | 15090 (13770–16770) | 15380 (15230–15380) | +1.9 | no |
| xcall memcpy-1m | x64 | 15260 (15260–15350) | 15300 (15160–15310) | +0.3 | no |
| xcall memcpy-1m-offset | arm64 | 15330 (14850–15390) | 15390 (15360–16000) | +0.4 | no |
| xcall memcpy-1m-offset | arm64ec | 15500 (14130–15760) | 15710 (14490–15760) | +1.4 | no |
| xcall memcpy-1m-offset | x64 | 16360 (16280–16520) | 16330 (16320–16360) | -0.2 | no |
| xcall strlen-1k | arm64 | 234 (233–234) | 236 (236–239) | +0.9 | yes |
| xcall strlen-1k | arm64ec | 236 (236–244) | 238 (238–238) | +0.7 | no |
| xcall strlen-1k | x64 | 262 (262–262) | 265 (263–273) | +1.0 | yes |
| xcall qsort-4k | arm64 | 107980 (105460–109690) | 110760 (105000–110970) | +2.6 | no |
| xcall qsort-4k | arm64ec | 164470 (162830–164850) | 164450 (163980–166180) | -0.0 | no |
| xcall qsort-4k | x64 | 3371800 (3361860–3371990) | 3389330 (3379690–3394150) | +0.5 | yes |

What moved outside the band: 11 rows, each within ±7 %:
- ARM64: `wait-all-wake` −5.9 % (one run at 1142 ns), `auto-pool-8` −0.0 % (its 10 ms-tick quantisation),
  `strlen-1k` +0.9 %.
- ARM64EC: `waitonaddress-uncontended` +7.1 % (14 → 15 ns), `memcpy-4k` +5.8 %, `memcpy-4k-offset` −2.2 %.
- x64: `waitonaddress-contended-4` +1.1 %, `get-tick-count` −2.0 %, `memcpy-256-offset` +1.4 %, `strlen-1k` +1.0 %,
  `qsort-4k` +0.5 %.

The lane programs are mingw builds with no volatile metadata, and no lane run sets EVMD. So 0006's only path in them is
the range lookup per block, which finds no range. The ARM64 lane doesn't load FEX at all, and three of its rows moved
too.

**R6.** Both differences from Task 2's baseline table are below R6's 5 ns threshold (native REPORT §3 R6): x64
`get-last-error` − x64 `get-current-thread-id` is 1.0 ns (25.6 − 24.6), and x64 `get-current-thread-id` − x64
`istream-addref` is −2.6 ns (24.6 − 27.2).

**Gates**, on the development build (FEX acba840 committed, not exported, before the fix round), with Steam running:
- `make wine-arm64-check` passed every step in 2473 s, `fex-vmd` last, then `PASS orphans`. Its `fex-vmd` saw MP
  forbidden=1,347 with the whole-module range and 0 with `run` and `worker` listed. The ratios were 0.49 (0.447 of
  0.906 s) and 0.38 (0.383 of 1.013 s). `g2-litmus`'s default run was at 0 on all four patterns, and its control at
  MP 22,348. Both D3D11 lanes recorded 4.5-4.6 ms frame times.
  - G4 (measured, not gated) still has `mem_seq_read` and `mem_seq_write` as FEX's worst rows against Rosetta: 2.22
    and 2.01, with FEX's defaults. `fex-vmd`'s runs with ranges in the same gate took 0.447 and 0.383 s; Rosetta's
    were 0.409 and 0.497 s there.
  - G4's `call_*` rows moved with x64-bench's new layout, as `docs/testing/acceptance-arm64-wine.md` warns. From
    Task 3's gate to this one: `call_chain64` 1.072 → 1.027, `call_virtual` 1.177 → 0.946, `call_std_function`
    1.392 → 1.324.
  - The gate started on AC and ended on battery: the power went off during it. It grades no timing except
    `fex-vmd`'s ratio, which passed with room.
- `make test`: 251 tests passed.
- `make smoke`: 15/15.
- `make bridge-check`: 15 `ok`.
- `make media-check`: unchanged,
  `FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf: stage=video-type hr=0xc00d5212`.

**The review's fix round** (FEX acba840 amended to aba3995, whose message f59f44c corrects; Ruling R29 kept
`VMD_CALIBRATE`'s tso-off runs ungated). x64-litmus runs of 10⁷ under each EVMD, on acba840 and then on aba3995 (MP
forbidden, then FEX's coverage line):

| EVMD for x64-litmus.exe | acba840 | aba3995 |
|---|---|---|
| `;0x0-0x18000;0x1680,0x18b2` (run's flag load, worker's flag store) | 0; 2 instructions, 1 ranges, 98304 bytes | 0; the same |
| `;0x0-0x18000;0x18b2` (the store only) | 37,565 | 137,251 |
| `;0x0-0x18000;0x1680` (the load only) | 3 | 0 |
| `;0x0-0x18000` (none listed) | 65,466 | 41,218 |
| `;` | 25,717; 0 instructions, 1 ranges, 98304 bytes | 0; no line |
| `;;0x1680,0x18b2` | 17,800; 0 instructions, 1 ranges, 98304 bytes | 0; 2 instructions, 0 ranges, 0 bytes |
| `;0x0-0x100000` | 30,523; 0 instructions, 1 ranges, 1048576 bytes | 33,371; 0 instructions, 1 ranges, 98304 bytes |
| (bare `x64-litmus.exe`) | | 45,782; 0 instructions, 1 ranges, 98304 bytes |

- `fex-vmd` gains the flag-accesses run (`info fex-vmd x64-litmus flags <offsets> MP forbidden=<n>`, which has to be
  0, and a coverage line of 2 instructions). It passed on acba840 and passes on aba3995. A temporary `check.sh` that
  listed only the store failed it: `MP forbidden=26057: a listed access didn't order its neighbours`, with the
  coverage-count check. Unlisting the load is caught every time. Unlisting the store showed 3 and 0 reorderings: a
  writer's lost release is rarely visible on this Mac.
- FEX's `ExtendedVolatileMetadata` APITest gains three cases: `hl2_linux;`, `hl2_linux;;0x1,0x2` and a range clamped
  by `ApplyFEXExtendedVolatileMetadata`. Built with llvm-mingw as a static ARM64 exe and run under this Wine, they
  fail 3 of 12 on acba840's parser and pass 12 of 12 (65 assertions) on aba3995's.
- `sh wine-arm64/check.sh fex-vmd g2-litmus` on aba3995: `PASS g2-litmus` (0 on all four; control MP 27,138),
  `PASS fex-vmd` (whole module 36,807, listed 0, flags 0), `PASS orphans`. The ratios, 0.50 (0.460 of 0.921 s) and
  0.39 (0.391 of 1.005 s), are provisional: the Mac was on battery (R19). `make test`: 251 passed.

**Step 8, SMITE 2 with and without its metadata** (2026-10-09, the maintainer at the Mac; it supersedes R33's waiver).
It ran after Steps 9-10, on the 0.3.0 rehearsal app (`build/release/rehearse-0.3.0/MacNeutron.app`, built from
9297719 with FEX 0001-0006 exported; `launcher.log`: `runtime=0.3.0 (6dc3da12c12a)`), on the built-in display and AC
power: the lobby and one practice match each. The Steam launch options:
- A, 0006 on: `/usr/bin/env DXMT_D3D12_SM6=1 MACNEUTRON_LOG=1 FEX_SILENTLOG=0 %command%`
- B, the PE tables off: `/usr/bin/env DXMT_D3D12_SM6=1 MACNEUTRON_LOG=1 FEX_SILENTLOG=0 FEX_VOLATILEMETADATA=0 %command%`

A first launch of A was aborted: its launch line lacked `DXMT_D3D12_SM6=1`, which Unreal Engine 5's Shader Model 6
check needs, so the game reported no DirectX 12 support. That was the launch line, not the runtime (`exit=0`, 33 s);
its log already had the coverage lines below for `Hemingway.exe`, `Hemingway-Win64-Shipping.exe` and
`OpenColorIO_2_3.dll`.

Run A's log (`steam-2437170.log`) has 16 `volatile metadata:` lines: the launcher `Hemingway.exe` in its own process,
then 15 images in the game's. Run B's has none: the switch was read for every image's PE tables (no EVMD was set).

| image (run A) | listed instructions | ranges | bytes |
|---|---|---|---|
| `Hemingway.exe` | 41 | 7 | 19,663 |
| `Hemingway-Win64-Shipping.exe` | 197,416 | 18 | 119,767,488 |
| `OpenColorIO_2_3.dll` | 9,028 | 6 | 3,041,465 |
| `EOSSDK-Win64-Shipping.dll` | 49,059 | 5 | 9,058,497 |
| `GFSDK_Aftermath_Lib.x64.dll` | 680 | 16 | 3,521,999 |
| `amd_fidelityfx_dx12.dll` | 54 | 7 | 204,035 |
| `sl.interposer.dll` | 19 | 7 | 7,345 |
| `sl.common.dll` | 29 | 6 | 7,538 |
| `sl.deepdvc.dll` | 11 | 3 | 4,547 |
| `sl.dlss_g.dll` | 19 | 4 | 5,330 |
| `sl.pcl.dll` | 11 | 3 | 4,554 |
| `sl.reflex.dll` | 19 | 3 | 4,907 |
| `onnxruntime.dll` | 1,978 | 9 | 9,851,343 |
| `DirectML.dll` | 1,544 | 7 | 2,826,363 |
| `steam_api64.dll` | 23 | 3 | 32,254 |
| `vivoxsdk.dll` | 1,793 | 4 | 2,539,753 |

This is the first run of 0006's PE path: FEX read and applied 16 images' MSVC tables (the lines above), and SMITE 2
ran under 0006's block rule over their ranges. Before it, only EVMD had run. The run doesn't show whether any range
needed clamping, and unmap removal is still verified by reading only.

Neither run faulted: no `err:seh` line, no FEX error line, and `launcher.log` has `exit=0` for both. A read-only
sampler (`ps` on the game's process every 5 s) gave:

| | A (0006) | B (`FEX_VOLATILEMETADATA=0`) |
|---|---|---|
| session | 3 min 20 s (21:27:33-21:30:53) | 3 min 16 s (21:32:45-21:36:01) |
| in-game CPU: mean of the samples from 30 s on | 360 % (34 samples) | 378 % (33 samples) |
| peak RSS | 4.2 GB | 4.7 GB |

Both RSS peaks are the sample just before the exit sample; from 30 s on, both stayed at 3.7-3.9 GB until then.
Without the exit sample (40 % and 20 % CPU) the CPU means are 369 % and 389 %, the same ~5 % gap. The maintainer:
"Both felt smooth and the FPS was roughly the same". That is the only frame-rate evidence: neither run recorded frame
times, GPU load or per-thread CPU, and the sampler's whole-process CPU can't rule out a main- or render-thread
bottleneck. One short run each, with different match content, so the ~5 % less CPU with 0006 is indicative only.
**Step 8 passes**: no regression with SMITE 2's metadata on. `FEX_VOLATILEMETADATA=0` after `/usr/bin/env` in a
game's launch options stays the escape hatch.

**Export (Step 9).** `make wine-arm64-export` wrote
`wine-arm64/patches/fex/0006-Core-Run-every-block-a-volatile-metadata-range-cover.patch`, the only new file under
`wine-arm64/patches`. A fresh fetch of `FEX_COMMIT` with the six patches applied by `git am` gave `applied 6/6` and a
`HEAD^{tree}` equal to the build tree's (`57f89ad6…`). `make wine-arm64` on the applied trees took 39 s, with no
"development build" line: `fex.applied` is f59f44c, and `build/wine-arm64/version` is `da4150186a9f…`.

**Final gate (Step 10)**, on the applied build (FEX f59f44c = 0006 exported), with Steam running. The logs are the
batch's `t4-final-gate-wine-arm64-check.log`, `t4-final-gate-fex-vmd-step.log`, `t4-final-gate-test.log`,
`t4-final-gate-smoke.log` and `t4-final-gate-bridge-check.log`:
- `make wine-arm64-check` passed every step in 3397 s, `fex-vmd` last, then `PASS orphans`.
  - `fex-vmd`: MP forbidden=2,845 over the whole module, 0 with `run` and `worker` listed, 0 with only the flag load
    and store listed.
  - The ratios were 0.48 (0.455 of 0.955 s) and 0.39 (0.405 of 1.042 s). They are provisional: the Mac was on battery
    (R19), as for the fix round's 0.50 and 0.39. The only AC figures are Step 5's 0.48 and 0.39 (acba840, a
    development build). The first gate's 0.49 and 0.38 were measured at its end, on battery (`fex-vmd` runs last).
    R33's AC re-measure of the exported build was not run, so these ratios stay provisional.
  - `g2-litmus`'s default run was 0 on all four patterns; its control saw MP 100,857.
  - Both D3D11 lanes recorded 8.3 ms, one 120 Hz refresh, as in Task 3's gate.
  - G4 (measured, not gated): `mem_seq_read` 2.31 and `mem_seq_write` 2.05 against Rosetta with FEX's defaults;
    `call_chain64` 1.056, `call_virtual` 1.077, `call_std_function` 1.334.
- `make test`: 252 tests passed (`main` gained 83908fa's app test meanwhile).
- `make smoke`: 15/15.
- `make bridge-check`: 15 `ok`.
- `make media-check`: not re-run on this build (the first gate's result is above).


## Critical sections on the Windows encoding (batch Task 8)

2026-10-10. **Wine patch 0033 (`ntdll: Use the Windows 2003 SP1+ LockCount encoding for critical sections.`, `macneutron`
79874a0 on 0771adf) ships as C1: `CS_SPIN_DEFAULT 0`, the Windows 2003 SP1+ LockCount encoding, barging, one claim in
flight, explicit SpinCount spins through one slot.** It is ours and local only. It halves the ARM lanes' contended
critical section (cs-contended-4 81/96 → 47/47 ns), keeps cs-uncontended at 6/7 ns and leaves the long-hold tail flat,
but misses design §9's lanes confirmation (a) and (b) (≤ 30/31 ns on ARM, x64 ≤ 1.5 × uncontended). The rulings that
got here: R36 (C1 unless C2-b1000 meets all three rows), R38 (a test fix in 0033 so ntdll:rtl reports in every lane),
R39 (kernel32's two failures are listed), R40 (measure where C1's contended cost goes), R42/R44 (try a nap that skips the
waker's alert; one bounded iteration, then ship C1 if O1 fails). The follow-up is at the end of this section.

**Step 0, G0 (the controller's; the design's stop-a-thread test, AC power, 1000/1000 each):** stop-C1 -s 0 points
0-5 236/490/0/0/274/0 (points 2, 3 and 5 unreachable at budget 0); stop-C1 -s 4000 75/549/24/292/52/8; stop-C2-b1000
-s 0 63/555/20/286/64/12; -s 4000 68/559/14/292/58/9. The interleaving model passed at C1 (3,2,2,1) and C2 (2,2,2,1) and
catches m-woken-takes-slot-keeping-claim (`cs-design/g0/summary.txt`).

**The winetests step.** `make wine-arm64-winetests` builds `ntdll`, `kernel32`, `atl`, `atl100` and `msvcirt`'s test
programs in two test-only Wine trees on wine-build's tools (`wine-tests-arm64`: aarch64; `wine-tests-ec`: arm64ec with
x86_64 as a full PE arch), 15 programs in 47 s; `wine-build` stays `--disable-tests` and nothing is staged into
wine.app. Its configure needs bison ≥ 3, so `winetests.sh` takes build.sh's `need_tool bison bison keg` (macOS ships
2.3). `check.sh winetests` (in `BATCH` after `fex-vmd`) runs `ntdll:rtl`, `kernel32:sync`, `atl:module`, `atl100:atl` and
`msvcirt:msvcirt` in all three lanes (x64 under FEX), crash dialog off and no `WINETEST_*` of the caller's; a test passes
when it prints its summary line, exits with that line's failure count and fails only at locations in `WINETESTS_FAILS`.
The whole check.sh run (boot, fex, winetests) takes 80-82 s; with the mutant below it hit its 1800 s cap, as designed.
Before any program existed it failed naming all 15 lane/test pairs (`exit 53, no failures`).

**RED-0, the baseline on Wine 0032** (three runs, 80/81/81 s). Every lane printed `rtl.c:3080` and `:3111: Test marked
todo: expected LockCount == -2, got 0`, and the critical-section awk (rtl.c `:2943-3123`, kernel32 `test_crit_section`
`:2937-3026`, `module.c:75`, `atl.c:157`, msvcirt's lock checks) printed nothing. Two pre-existing problems:
- `ntdll:rtl` printed no summary line (exit 5) in the arm64ec and x64 lanes: `test_RtlDestroyHeap`'s breakpoint handler
  takes its `__x86_64__` branch (`Rip += 1`) in ARM64EC and x64 builds, but the ARM64X ntdll's `DbgBreakPoint`
  (`heap.c:1629-1632`) is a 4-byte ARM64 `brk`, so execution resumed at a misaligned PC (`+seh`: EXCEPTION_BREAKPOINT at
  `…CA44`, then an access violation at `…CA45`). **Ruling R38:** 0033 also changes that line to advance by 1 only over an
  int3 (`0xcc`), else by 4, a test fix the lanes need to report at all; one line, no key moved. With it (three more runs
  on 0032): ntdll:rtl 600076 tests, 54 todo, 0 failures in all three lanes, and no further failure exposed.
- kernel32:sync fails at `sync.c:416` and `:420` in all three lanes and all runs (`test_mutex`: a mutex whose owner
  TerminateThread ends is never seen abandoned, `got 0x102`). Under `WINEMSYNC=0` the same program has 0 failures, so it is
  msync's (Ruling R39: listed, a follow-up). **`WINETESTS_FAILS` = `arm64:sync.c:416 arm64:sync.c:420 arm64ec:sync.c:416
  arm64ec:sync.c:420 x64:sync.c:416 x64:sync.c:420`**; no rtl.c location was listed, so nothing was re-keyed. The fourth
  run printed `PASS winetests`.
- Not a failure: `err:sync:RtlLeaveCriticalSection section 00006FFFFA205560 "?" is not acquired` appears in the x64 lane's
  kernel32:sync stderr in 3 of 8 runs on 0032 and 1 of 2 on 0033, always at that address (some x64-side static section is
  over-left); recorded, not chased.

**The long-hold row.** x64-sync gains `cs-hold-10us-p999` (Ruling R36's H10-class row): 4 threads, 500 times each Enter,
10 µs held, Leave, 10 µs outside (QPC busy-waits); per batch the p99.9 of the 2,000 waits (the third largest), the row
the median of 21 batches. `SYNC_ROWS=17` failed first (`arm64-sync printed 16 of 17 time rows`), then a lanes run passed
with 99 rows. The msync step runs it in both modes.

**RED-1** (the five new tests in rtl.c, on 0032): exactly the eight expected failures per lane (`rtl.c:3079`, `:3109`:
`expected LockCount == -2, got 0`; `one parked waiter: LockCount 1, expected -6`; `a claim in flight: LockCount 0,
expected -3`; `TryEnter with a claim in flight failed, LockCount 0`; `after a recursive Enter: LockCount 1, RecursionCount
2, expected -2, 2`; `after a recursive TryEnter: LockCount 2, RecursionCount 3, expected -2, 3`; `after two Leaves:
LockCount 0, RecursionCount 1, expected -2, 1`); the stress, semaphore and suspend tests passed on the NT4 encoding.

**GREEN (C1, H1 = `8d9bc932069ef7070748eeb76e9d06178850c16ee8b5176fc753cdf22275a5a3`):** `PASS winetests`, `PASS
orphans`; ntdll:rtl 600122 tests, 52 todo, 0 failures in each lane; kernel32:sync 2 failures (the listed ones);
atl:module 550/0, atl100:atl 229/0, msvcirt 6027/0. `Test marked todo: expected LockCount == -2` 0 times, `wait timed out`
0 times in every `.err`, and the awk (rtl.c's range now `:2943-3299`) prints nothing. `pe_baseline_check` passes:
arm64ec `sync.o` has 16 acquire loads and 3 release stores (14 / 3 before, acceptance :1018).

| Function / object (aarch64 and arm64ec `sync.o`, the same) | before (0032) | after (0033) |
|---|---|---|
| `RtlEnterCriticalSection` | 2 casal, 2 ldaddal | 1 ldclral |
| `RtlTryEnterCriticalSection` | 1 casal, 1 ldaddal | 1 ldclral |
| `RtlLeaveCriticalSection` | 2 ldaddal | 1 ldsetal |
| `cs_enter_slow` / `cs_leave_slow` | — | 3 casal / 1 casal |
| whole object `ldclral` / `ldsetal` / `CNTVCT_EL0` / `isb` | 0 0 0 0 | 7 8 2 1 (the extra ldclral/ldsetal are Enter/Leave inlined into `RtlAcquireResource*`, `RtlReleaseResource`, `RtlDeleteResource`, `RtlSleepConditionVariableCS`) |

x86_64, compiled only (`wine-tests-ec/dlls/ntdll/x86_64-windows/sync.o`): Enter 1 `lock btrl`, TryEnter 1 `lock btrl`,
Leave 1 `lock xaddl`, `cs_enter_slow` 3 `lock cmpxchgl`, `cs_leave_slow` 1 `lock cmpxchgl` (llvm-objdump prints `lock` on
its own line; counted with the lines joined).

**The mutant** (uncommitted: Leave claims while a claim is in flight): `FAIL winetests: timed out after 1800 s; info
winetests arm64 ntdll:rtl no summary line (exit 71)`, `rtl.c:3183: Test failed: stress, SpinCount 0: the threads weren't
done in 60 s` (and SpinCount 4000), 12 `wait timed out` in ntdll's stderr and 60 in kernel32's. The stress tests detect
a double claim. Restored: hash = H1.

**The lanes.** Every set is three back-to-back `caffeinate -i sh wine-arm64/check.sh lanes` runs, AC at every start and
end, `pgrep` idle checks (wineserver, check.sh, `bin-stop|cs-harness|wine-tests-`) empty, ntdll's disassembly hash in each
wrapper log: t8-before (H0, 01:28-01:36, one-minute load at start 1.66/2.60/2.80), t8-c1-first (H1, 2.16 median), t8-c2
(C2-b1000 uncommitted, H2 `de39b044…`, 2.46), t8-c1 (H1, the re-measure Step 10 asks for, 2.13), all within 2 of before's.
`build/lanes/t8/` is t8-c1 (the shipped variant). Before (0032) against t8 (0033, C1):

| row | lane | before | after | Δ % | outside the band |
|---|---|---|---|---|---|
| sync uncontended-wait | arm64 | 77.0 (67.0–83.0) | 83.0 (77.0–131) | +7.8 | no |
| sync uncontended-wait | arm64ec | 76.0 (76.0–84.0) | 77.0 (76.0–77.0) | +1.3 | no |
| sync uncontended-wait | x64 | 126 (125–142) | 124 (122–127) | -1.6 | no |
| sync uncontended-signal | arm64 | 37.0 (32.0–38.0) | 38.0 (37.0–63.0) | +2.7 | no |
| sync uncontended-signal | arm64ec | 42.0 (42.0–44.0) | 42.0 (42.0–42.0) | +0.0 | no |
| sync uncontended-signal | x64 | 99.0 (93.0–100) | 92.0 (86.0–93.0) | -7.1 | no |
| sync cross-process-wake | arm64 | 4109 (4108–4110) | 4122 (4099–4144) | +0.3 | no |
| sync cross-process-wake | arm64ec | 4202 (3528–4215) | 3842 (3481–4088) | -8.6 | no |
| sync cross-process-wake | x64 | 4210 (3736–4570) | 4318 (4210–4394) | +2.6 | no |
| sync create-close | arm64 | 26476 (26044–26957) | 26872 (26608–26927) | +1.5 | no |
| sync create-close | arm64ec | 26538 (24041–27328) | 27093 (26892–27179) | +2.1 | no |
| sync create-close | x64 | 27614 (27094–28112) | 27712 (27652–27898) | +0.4 | no |
| sync wait-all-wake | arm64 | 5383 (3188–5958) | 6014 (3950–6140) | +11.7 | no |
| sync wait-all-wake | arm64ec | 5914 (5490–5988) | 6062 (5770–6118) | +2.5 | no |
| sync wait-all-wake | x64 | 5600 (5228–5637) | 5643 (5088–5690) | +0.8 | no |
| sync wait-all-poll | arm64 | 78.0 (58.0–85.0) | 85.0 (62.0–85.0) | +9.0 | no |
| sync wait-all-poll | arm64ec | 87.0 (79.0–88.0) | 87.0 (87.0–87.0) | +0.0 | no |
| sync wait-all-poll | x64 | 118 (117–118) | 117 (117–117) | -0.8 | no |
| sync auto-handoff-8 | arm64 | 583 (575–593) | 589 (498–616) | +1.0 | no |
| sync auto-handoff-8 | arm64ec | 598 (582–608) | 587 (585–616) | -1.8 | no |
| sync auto-handoff-8 | x64 | 653 (628–658) | 657 (649–693) | +0.6 | no |
| sync cs-uncontended | arm64 | 6.0 (3.0–6.0) | 6.0 (6.0–6.0) | +0.0 | no |
| sync cs-uncontended | arm64ec | 7.0 (7.0–8.0) | 7.0 (4.0–7.0) | +0.0 | no |
| sync cs-uncontended | x64 | 90.0 (65.0–102) | 91.0 (83.0–92.0) | +1.1 | no |
| sync srw-uncontended | arm64 | 14.0 (8.0–15.0) | 15.0 (13.0–15.0) | +7.1 | no |
| sync srw-uncontended | arm64ec | 15.0 (13.0–15.0) | 15.0 (8.0–15.0) | +0.0 | no |
| sync srw-uncontended | x64 | 103 (73.0–115) | 103 (91.0–103) | +0.0 | no |
| sync cs-contended-4 | arm64 | 81.0 (56.0–89.0) | 47.0 (43.0–49.0) | -42.0 | yes |
| sync cs-contended-4 | arm64ec | 96.0 (95.0–99.0) | 47.0 (43.0–47.0) | -51.0 | yes |
| sync cs-contended-4 | x64 | 178 (177–187) | 175 (173–177) | -1.7 | no |
| sync srw-contended-4 | arm64 | 134 (117–135) | 144 (141–144) | +7.5 | yes |
| sync srw-contended-4 | arm64ec | 135 (131–135) | 141 (138–144) | +4.4 | yes |
| sync srw-contended-4 | x64 | 204 (203–205) | 203 (202–205) | -0.5 | no |
| sync waitonaddress-uncontended | arm64 | 12.0 (11.0–12.0) | 12.0 (12.0–12.0) | +0.0 | no |
| sync waitonaddress-uncontended | arm64ec | 15.0 (14.0–15.0) | 15.0 (14.0–15.0) | +0.0 | no |
| sync waitonaddress-uncontended | x64 | 56.0 (56.0–56.0) | 58.0 (56.0–60.0) | +3.6 | no |
| sync waitonaddress-contended-4 | arm64 | 4392 (4299–4435) | 4452 (4406–4471) | +1.4 | no |
| sync waitonaddress-contended-4 | arm64ec | 4432 (4416–4465) | 4455 (4418–4473) | +0.5 | no |
| sync waitonaddress-contended-4 | x64 | 4442 (4424–4482) | 4469 (4440–4512) | +0.6 | no |
| sync wait-any-wake | arm64 | 7196 (7044–7515) | 7190 (7059–7734) | -0.1 | no |
| sync wait-any-wake | arm64ec | 7131 (7062–7255) | 7046 (6776–7094) | -1.2 | no |
| sync wait-any-wake | x64 | 7152 (6986–7440) | 7170 (7167–7261) | +0.3 | no |
| sync alertable-wake | arm64 | 7162 (7156–8215) | 7418 (7222–7684) | +3.6 | no |
| sync alertable-wake | arm64ec | 6944 (6928–7301) | 7150 (6744–7172) | +3.0 | no |
| sync alertable-wake | x64 | 7190 (7108–7404) | 7260 (7233–7437) | +1.0 | no |
| sync auto-pool-8 | arm64 | 45949 (42990–51937) | 51936 (51933–51939) | +13.0 | no |
| sync auto-pool-8 | arm64ec | 45944 (44952–50932) | 50962 (50950–51931) | +10.9 | yes |
| sync auto-pool-8 | x64 | 46970 (45947–51932) | 51931 (50936–51934) | +10.6 | no |
| sync cs-hold-10us-p999 | arm64 | 48700 (48200–50600) | 50700 (49500–51100) | +4.1 | no |
| sync cs-hold-10us-p999 | arm64ec | 49000 (46900–49300) | 50300 (49400–51200) | +2.7 | yes |
| sync cs-hold-10us-p999 | x64 | 49100 (48500–50600) | 50000 (49700–55800) | +1.8 | no |
| xcall get-current-thread-id | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-current-thread-id | arm64ec | 1.7 (1.5–1.7) | 1.7 (1.7–1.7) | +0.0 | no |
| xcall get-current-thread-id | x64 | 24.2 (24.1–24.2) | 24.2 (24.1–24.6) | +0.0 | no |
| xcall get-last-error | arm64 | 0.9 (0.9–0.9) | 0.9 (0.9–0.9) | +0.0 | no |
| xcall get-last-error | arm64ec | 2.4 (2.4–2.8) | 2.4 (2.2–2.4) | +0.0 | no |
| xcall get-last-error | x64 | 24.3 (24.2–24.4) | 25.7 (25.1–25.9) | +5.8 | yes |
| xcall tls-get-value | arm64 | 0.9 (0.8–0.9) | 0.9 (0.8–0.9) | +0.0 | no |
| xcall tls-get-value | arm64ec | 2.4 (2.3–2.8) | 2.5 (2.4–2.5) | +4.2 | no |
| xcall tls-get-value | x64 | 24.4 (24.4–24.6) | 24.4 (24.3–24.4) | +0.0 | no |
| xcall get-tick-count | arm64 | 0.7 (0.7–0.7) | 0.7 (0.7–0.7) | +0.0 | no |
| xcall get-tick-count | arm64ec | 1.7 (1.7–1.7) | 1.7 (1.7–1.8) | +0.0 | no |
| xcall get-tick-count | x64 | 23.8 (23.8–24.5) | 23.8 (23.7–23.8) | +0.0 | no |
| xcall qpc | arm64 | 15.4 (15.4–15.4) | 15.4 (15.4–15.4) | +0.0 | no |
| xcall qpc | arm64ec | 16.8 (16.8–16.8) | 16.8 (16.8–16.8) | +0.0 | no |
| xcall qpc | x64 | 43.2 (43.2–45.7) | 43.0 (42.9–43.1) | -0.5 | yes |
| xcall istream-addref | arm64 | 1.5 (1.5–1.5) | 1.5 (1.5–1.5) | +0.0 | no |
| xcall istream-addref | arm64ec | 1.5 (1.5–1.5) | 1.5 (1.5–1.5) | +0.0 | no |
| xcall istream-addref | x64 | 24.3 (24.3–24.5) | 24.4 (24.2–24.7) | +0.4 | no |
| xcall memcpy-16 | arm64 | 1.9 (1.9–2.1) | 1.9 (1.9–2.0) | +0.0 | no |
| xcall memcpy-16 | arm64ec | 2.6 (2.6–2.8) | 3.0 (2.6–3.0) | +15.4 | no |
| xcall memcpy-16 | x64 | 25.4 (25.4–25.4) | 25.6 (25.4–25.6) | +0.8 | no |
| xcall memcpy-16-offset | arm64 | 3.0 (3.0–3.0) | 3.0 (2.8–3.0) | +0.0 | no |
| xcall memcpy-16-offset | arm64ec | 3.5 (3.4–3.9) | 3.5 (3.4–3.9) | +0.0 | no |
| xcall memcpy-16-offset | x64 | 26.9 (26.7–27.3) | 26.9 (26.6–27.2) | +0.0 | no |
| xcall memcpy-256 | arm64 | 3.9 (3.5–4.1) | 4.1 (3.5–4.1) | +5.1 | no |
| xcall memcpy-256 | arm64ec | 5.0 (4.4–5.4) | 5.2 (4.6–5.4) | +4.0 | no |
| xcall memcpy-256 | x64 | 27.6 (27.2–27.8) | 27.6 (27.5–27.6) | +0.0 | no |
| xcall memcpy-256-offset | arm64 | 5.0 (4.6–5.0) | 5.4 (4.8–5.4) | +8.0 | no |
| xcall memcpy-256-offset | arm64ec | 6.2 (5.9–6.2) | 5.9 (5.9–6.2) | -4.8 | no |
| xcall memcpy-256-offset | x64 | 28.8 (28.8–29.2) | 29.1 (28.8–29.1) | +1.0 | no |
| xcall memcpy-4k | arm64 | 43.6 (43.1–43.7) | 43.5 (42.8–44.1) | -0.2 | no |
| xcall memcpy-4k | arm64ec | 45.1 (44.9–46.2) | 44.9 (44.9–44.9) | -0.4 | no |
| xcall memcpy-4k | x64 | 57.4 (57.3–80.8) | 57.8 (57.8–58.1) | +0.7 | no |
| xcall memcpy-4k-offset | arm64 | 45.7 (45.6–53.8) | 45.6 (45.6–45.6) | -0.2 | no |
| xcall memcpy-4k-offset | arm64ec | 39.9 (39.5–44.5) | 39.8 (39.7–40.2) | -0.3 | no |
| xcall memcpy-4k-offset | x64 | 88.8 (83.4–90.7) | 73.7 (73.5–90.3) | -17.0 | no |
| xcall memcpy-1m | arm64 | 15500 (15480–15610) | 15620 (15600–15640) | +0.8 | no |
| xcall memcpy-1m | arm64ec | 15350 (15330–15440) | 15340 (15320–15410) | -0.1 | no |
| xcall memcpy-1m | x64 | 15220 (15190–15230) | 15240 (15240–15270) | +0.1 | yes |
| xcall memcpy-1m-offset | arm64 | 15330 (15300–15350) | 15320 (15240–15350) | -0.1 | no |
| xcall memcpy-1m-offset | arm64ec | 15680 (15660–15700) | 15700 (15620–15710) | +0.1 | no |
| xcall memcpy-1m-offset | x64 | 16280 (16240–16290) | 16270 (16250–16270) | -0.1 | no |
| xcall strlen-1k | arm64 | 231 (231–231) | 232 (231–243) | +0.4 | yes |
| xcall strlen-1k | arm64ec | 233 (233–234) | 234 (234–234) | +0.1 | no |
| xcall strlen-1k | x64 | 260 (258–261) | 259 (259–259) | -0.3 | no |
| xcall qsort-4k | arm64 | 103350 (99580–106600) | 107060 (103180–111800) | +3.6 | no |
| xcall qsort-4k | arm64ec | 158790 (157810–160070) | 158950 (156530–159350) | +0.1 | no |
| xcall qsort-4k | x64 | 3324030 (3321660–3329380) | 3328250 (3317700–3455620) | +0.1 | no |

- cs-contended-4: 81/96/178 → **47/47/175** (C1's first set read 48/43/174). Task 4's figures were 105/100/198; the
  before set's arm64 had one fast run (56).
- The rows outside the band on the worse side are srw-contended-4 arm64/arm64ec, auto-pool-8 arm64ec and cs-hold
  arm64ec (+2.7 %); in C1's first set they were create-close arm64 and srw-contended-4 arm64ec. Only srw-contended-4
  arm64ec repeats, and 0033 touches neither SRW locks nor the event pool; auto-pool-8 is quantised in ~1 ms steps
  (45.9k/50.9k/51.9k) and the before set's median sits on its low runs. Read as noise; xcall get-last-error x64 (+5.8 %)
  reads the same in both C1 sets and is not a critical-section path.

**R36: C1 against C2-b1000** (medians; C2 is `CS_SPIN_DEFAULT 1000`, uncommitted, `PASS winetests` with 0 `wait timed
out` first):

| R36 criterion | C1 (`t8-c1`) | C2-b1000 (`t8-c2`) | bar |
|---|---|---|---|
| cs-contended-4 arm64 / arm64ec | 47 / 47 | 90 / 92 | ≤ 30 / ≤ 31 |
| x64 cs-contended-4 ÷ x64 cs-uncontended | 175 / 91 = 1.92 | 181 / 92 = 1.97 | ≤ 1.5 |
| x64 cs-contended-4 | 175 | 181 | C2 below C1: no |
| cs-hold-10us-p999 (ns) | 50700 / 50300 / 50000 | 57100 / 51300 / 63000 | ≤ 2,000,000 |

C1 ships. Design §9's confirmation for it: (a) **missed** (47/47 against 30/31), (b) **missed** (1.92 against 1.5), (c)
met (6/7), (d) the rows above, read as noise. The x64 lane barely moved (178 → 175).

**Where C1's contended cost goes (Ruling R40,** an instrumented ntdll, uncommitted, counters per lock dumped at each
`DeleteCriticalSection` behind `WINE_CS_STATS=1`; the fast path's instructions identical to C1's; three lanes runs per
build; full figures in task-8-report.md's Phase 1). Per lock operation in cs-contended-4 (arm64 / arm64ec / x64):
claims (= wakes issued) 0.0185 / 0.0170 / 0.142; 34-40 % of them futile (the woken waiter finds a barger and parks
again); 91 / 91 / 74 % of holds taken while a claim was in flight; owner changes 0.047 / 0.047 / 0.258; the unwait (token
xchg + RtlWakeAddressSingle) 1.09 / 1.15 / 0.76 µs mean, of which the `NtAlertThreadByThreadId` round trip is ≥ 98 %
XNU's `os_sync_wake` (a null Wine syscall through the same dispatcher, x18 toggles and q0-q31 saves costs 8.8 / 9.4 ns).
So on ARM about 20 of the 43-47 ns (an upper bound, thread time) is the releasing thread inside the kernel's wake; on
x64 0.14 × 0.76 µs ≈ 107 ns of thread time covers the whole 93 ns above the 92 ns FEX-transition base. A thread woken
from a real sleep runs 4.7-4.9 µs after its claim (the hold row), which sets that row's ~50 µs p99.9. Every contending
lane thread ran on CPUs 12-17 (the 6-core Super cluster); 20 % of contended wakes resume on another core.

**A cheaper waker (Rulings R42-R44), measured, not shipped.** The design round (cs-wake, `cs-design/wake-design-result.json`)
picked N: a critical-section waiter's first wait is a 3 µs timed nap that no waker interrupts, and a Leave that claims
a napping waiter only marks its futex entry woken and skips the alert syscall (ARM only; public RtlWaitOnAddress/Wake*
unchanged; it also closes the unlocked `entry.addr` read). SN added a 20 µs fresh-contender poll. On the native harness
(design §8 rows, ABAB against C1, -e 10 and -e 287, 3-4 rounds; `t8-p2-*` in the workspace):

| row | N (fixed nap) | NT (adaptive nap, per thread) |
|---|---|---|
| K4 ns/op | 0.46-0.71 × C1 | 0.54-0.74 × C1 |
| XQ-K4 / XQ-B40-4 ns/op | 0.71-0.98 / 0.68-0.92 | 0.82-1.00 / 0.65-0.87 |
| F: frame thread p99.9 (≤ 250 µs) | 61-74 µs | 74 µs |
| H10 p99.9 (≤ 1.10 × C1) / CPU | 0.88-1.29 / +11-16 % | 1.00-1.04 / +0-2 % |
| O1 (36 threads, 1 µs hold): throughput / overhead CPU / p99.9 | 0.85-0.89 / 2.6-3.2 × / 0.93-1.29 | 1.01-1.07 / 1.17-1.31 × / 0.86-1.09 |

- G0: stop-N, stop-SN and stop-NA (the per-lock adaptive variant) each PASS 1000/1000 at -s 0 and -s 4000 with every
  reachable point nonzero (stop-NA -s 4000's point 5 needed -r 5000: 8 of 5000).
- N's nap is pure cost wherever waits outlast it: in O1 99 % of naps expire, and every one is an extra timer wakeup and
  reschedule (2 µs, 3 µs and 4 µs naps alike). **NT** (Ruling R44): after two expiries in a row a thread skips its nap for
  its next 2^k parks (k ≤ 6), and a woken nap clears that. A per-lock history lagged behind the run queue under
  oversubscription (30 % of O1's parks still napped); per thread, O1 naps 3 % of parks.
- **O1 still fails design §9's CPU gate (G3(c): extra overhead CPU needs half as much throughput gain; a nap is not a
  spin budget):** NT +17-31 % overhead CPU for +1-7 % throughput at both dispatcher values; a 2^10 cap passes at -e 10
  only. Even rare naps change O1's dynamics (token thefts +40 %, idle −10 %, owner changes up). Per R44, 0033 stays C1;
  the N/NT patches are kept (`t8-p2-n.patch`, `t8-p2-nt.patch`) for the follow-up.

**Gates** (C1 = 79874a0, staged ntdll H1; Steam up, `pgrep -lx steam_osx` 15329, before the run; AC):
- `make wine-arm64-check`: every step PASS up to `dxmt`, with `msync` printing `info msync 1 cs-hold-10us-p999 48100` and
  `info msync 0 cs-hold-10us-p999 49900`; then `FAIL dxmt-present: arm64ec present_loop: winshot: screencapture of window
  62235 failed` with present_loop's 3000 frames done (8.3 ms average) and the console locked (`IOConsoleLocked` true):
  Ruling R17's case, to be re-run by name once the screen is unlocked. The steps after it, run by name
  (`check.sh dxmt-arm64ec dxmt-x64 g4-bench fex-vmd winetests`): all PASS (dxmt-arm64ec 449 s, dxmt-x64 903 s; fex-vmd's
  litmus MP 11,194 with EVMD, 0 listed, mem_seq 0.46 / 0.41 of default; winetests as in GREEN), `PASS orphans`. The
  full run takes about 38 min (326 s to dxmt-present, then 1,971 s), which README `:60` now says; the winetests step adds
  ~80 s.
- `make test`: 252 tests passed. `make smoke`: 15/15. `make bridge-check`: passed. `make media-check`: unchanged
  (`FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf: stage=video-type
  hr=0xc00d5212`).
- Export: `make wine-arm64-export` wrote one new file, `wine-arm64/patches/wine/0033-ntdll-Use-the-Windows-2003-SP1-LockCount-encoding-fo.patch`;
  the fresh-fetch proof (a shallow clone of the pin, `git am` of the 33 patches) printed `applied 33/33` and
  `tree-equal` (tree 0df0c0b2…). A rebuild in applied mode stages H1 again.

**Assumptions.**
- The contention test (−6, −3, −4), the recursion test and the stress test's LockSemaphore check assume the 2003 SP1+
  encoding as Microsoft documents it. They were not run on Windows (no testbot: no upstream submission).
- The x86 branch (x86_64, i686) is compiled, not run: wine.app has only the ARM64X ntdll, so the x64 lane runs the
  ARM64EC branch under FEX. x86_64 codegen is above; i686 codegen is design §3.2's `cs-proto.c`, not the patched sync.c.
  wow64 i386 is not built, so the conclusion's i386 run can't happen here.
- 0033 refuses `__arm__` (ARMv7 PE) with `#error`; MacNeutron never builds it.
- DebugInfo's ContentionCount counts consumed claims, including those of claimed waiters that re-park (design §2.2), so
  it can exceed the contended acquisitions.
- `cs-hold-10us-p999` is the median over 21 batches of each batch's p99.9 (the 3rd-largest of 2,000 waits), not the
  harness's pooled H10 p99.9; R36's 2 ms bar is applied to it as an H10-class estimate.
- SRW locks are unchanged (design §10).

**Follow-up.** C1 misses §9 (a)/(b) at 47/47/175 ns. The measured cause is the releasing thread's kernel wake (≈ 1 µs
in `os_sync_wake`, ~0.02 claims per op on ARM, 0.14 on x64). A waker that skips the alert for a napping waiter (N/NT)
halves K4 on the harness but costs CPU in the oversubscribed O1 row; a design that keeps the waker cheap without a timed
nap, or a nap whose expiries cost less under oversubscription, is the next step, measured on the lanes. Also open:
msync's abandoned mutex on TerminateThread (kernel32 `sync.c:416/:420`), and the x64 kernel32 "not acquired" ERR.

## ARM64EC auxiliary IAT (batch Task 5)

2026-10-10. **Wine patch 0034 (`ntdll: Fill the ARM64EC auxiliary IAT and revert an entry when what it was resolved
through is made writable.`, `macneutron` 0c40afb on 79874a0, after fix rounds 1 and 2 (Rulings R46-R48); 6fe9c96 before them) fills an ARM64EC importer's auxiliary IAT at load and puts
an entry back on its check stub when a page it was resolved through is made writable (Ruling R13, option B).** It is
ours and local only. The arm64ec lane's import calls drop to, or toward, the arm64 lane's cost: get-current-thread-id,
get-last-error and tls-get-value 1.7-2.5 → 0.7 ns, get-tick-count 1.7 → 1.5 on the final 0034 (1.1 on 6fe9c96 before
the fix rounds; 0.7-1.6 with 0035, bimodal within a set, cause not found), memcpy-16 3.0 → 1.9.

What 0034 does (`dlls/ntdll/signal_arm64ec.c`, `loader.c`, `ntdll_misc.h`):
- **The fill.** Once a module's imports are bound (`fixup_imports`, not under `+relay` or `+snoop`), each entry whose
  auxiliary and `AuxiliaryIATCopy` values are equal and inside the importer (the import's check stub) is resolved as
  `arm64x_check_call` would: EC code as is, an FFS to its EC target, one `ff 25` hop to either. Only memory inside a
  loaded image is read; anything else (syscall stubs, `allocate_stub`'s stubs, an alias whose slot isn't bound yet) keeps
  the checker, and so does anything resolved through a page that is writable. The auxiliary IAT is written through the
  raw `NtProtectVirtualMemory` syscall, never the wrapper.
- **What a filled entry records:** the importer's IAT entry, then each code address and `ff 25` slot read (`dep[4]`),
  and their pages in a 65,536-bit hash, so a protect anywhere else costs a few bit tests and no lock.
- **The revert.** A protect to a writable protection in this process (the `NtProtectVirtualMemory` wrapper, both paths,
  after the protect and so before the hooker writes, and after FEX's post-notify) and, from another process, a writable post-protect, a flush or a
  write on the cross-process work list put back to its stub every entry with a dependency in the range; an overflowed
  list puts back everything. Entries resolved through other pages stay filled.
- **The order** (each page's bit, then its one query per fill, then a second resolve, under `aux_lock`): a hooker on
  another thread either sees the bit and reverts after the fill, or protected before the query (which then sees the page
  writable), or wrote and restored before it (the second resolve then reads the patched bytes). A page is read only once
  its query says it is committed and readable, so the fill skips unreadable pages; a page made no-access between its
  query and the read can still fault (the known minor under Gaps).
- **Suspension.** The fill, the forget and the revert hold `aux_lock` as a syscall callback, so a suspend that arrives
  meanwhile waits for the doorbell, as it does for FEX's own lock holders. From the cross-process work list FEX runs the
  revert with InSimulation set, so leaving the callback there leaves the doorbell to FEX.
- **The forget.** `free_modref` drops a module's entries before it unmaps the view, without writing them.

**The `ec-hook` step** (`check.sh`, in `BATCH` after `winetests`; crash dialog off): `arm64ec-hook.exe` (ARM64EC) prints
`ok`/`FAIL` for six rows: `filled` (GetTickCount, an FFS export, and GetLastError, kernel32's `ff 25` alias to
kernelbase, hold EC code outside the exe); `ffs-hook` (`mov eax, 0x5eed; ret` written over kernel32's GetTickCount FFS
after a VirtualProtect: the ARM64EC call gets 0x5eed, and the real value once the bytes are back); `per-entry`
(GetLastError's entry, on other pages, is still filled after that hook); `iat-hook` (the exe's regular IAT entry for
TlsGetValue made writable and pointed at an EC function: the ARM64EC call runs it); `unload` (version.dll loaded and
freed, its old range allocated, filled with 0x5a and protected writable: no byte changes); `suspend` (fix round 1: one
thread loads and frees version.dll while another, for 3 s, does what MinHook does, SuspendThread, GetThreadContext,
VirtualProtect of kernel32's GetTickCount FFS page to RWX and back, ResumeThread; the hooker must finish within 15 s).

**RED → GREEN** (`check.sh ec-hook`, logs `t5-red.log`, `t5-green-step3.log`, `t5-green-step4.log`, `t5-green.log`):
- On 0033: `FAIL filled` twice (the entries are the exe's stubs, 0x140002C20 and 0x140002BC0), `FAIL ffs-hook` and `FAIL
  iat-hook` (not filled before the hook), `FAIL per-entry`, `ok unload`: `4 of 5 rows failed (first: filled)`.
- The fill alone: `ok filled`, `ok per-entry`, `ok unload`; `ffs-hook` (GetTickCount returned 946230175, the real
  count) and `iat-hook` (TlsGetValue returned 0) fail: hooks applied after load miss ARM64EC callers.
- With the revert: only `unload` fails, `version.dll's old range changed at +0xb000 after FreeLibrary`: a stale entry's
  stub written into the new allocation (version.dll's auxiliary IAT is at 0xB000 in this build).
- With the forget: five `ok`, `PASS arm64ec-hook`, `PASS ec-hook`, `PASS orphans`.
- `check.sh winetests ec-hook` on the final code: `PASS winetests`, failing only at the six listed `sync.c:416/:420`
  locations, and `PASS ec-hook`.

**Lanes** (`build/lanes/t5/`, against `build/lanes/t8/`; three runs back to back under `caffeinate -i`, AC at every start
and end, idle checks empty, load 1.76-1.99, 167 s each; `lanes_report.py build/lanes/t8 build/lanes/t5`):

| Row (ns), median (min–max) of 3 | arm64, t8 | arm64ec, t8 | arm64ec, t5 | arm64ec − arm64, t8 → t5 |
|---|---|---|---|---|
| xcall `get-current-thread-id` | 0.7 (0.7–0.7) | 1.7 (1.7–1.7) | 0.7 (0.7–0.7) | +1.0 → 0.0 |
| xcall `get-last-error` | 0.9 (0.9–0.9) | 2.4 (2.2–2.4) | 0.7 (0.7–0.7) | +1.5 → −0.2 |
| xcall `tls-get-value` | 0.9 (0.8–0.9) | 2.5 (2.4–2.5) | 0.7 (0.7–0.7) | +1.6 → −0.2 |
| xcall `get-tick-count` | 0.7 (0.7–0.7) | 1.7 (1.7–1.8) | 1.1 (1.1–1.1) | +1.0 → +0.4 |
| xcall `qpc` | 15.4 (15.4–15.4) | 16.8 (16.8–16.8) | 15.9 (15.9–15.9) | +1.4 → +0.5 |
| xcall `memcpy-16` | 1.9 (1.9–2.0) | 3.0 (2.6–3.0) | 1.9 (1.9–2.2) | +1.1 → 0.0 |
| sync `cs-uncontended` | 6.0 (6.0–6.0) | 7.0 (4.0–7.0) | 6.0 (6.0–6.0) | +1.0 → 0.0 |

- All four target rows are lower and outside the band. So are the arm64ec lane's qpc (−5.4 %), memcpy-16/-256 and their
  offset forms (−15 to −37 %), waitonaddress-uncontended (15 → 10 ns) and uncontended-signal (42 → 38).
- The arm64 lane doesn't move (it has no auxiliary IAT); cs-uncontended arm64ec 7 → 6 is inside the band.
- Outside the band on the worse side: xcall get-tick-count x64 23.8 → 24.6 (+3.4 %), tls-get-value x64 24.4 → 24.6
  (+0.8 %) and sync alertable-wake arm64ec (+2.1 %). The x64 lane's other xcall rows don't move and alertable-wake is a
  7 µs wake; not explained, read as session variance.

**Gates** (0034 = 6fe9c96; Steam up, `pgrep -lx steam_osx` 15329, before the run; AC at start and end):
- `make wine-arm64-check`: every step PASS up to `dxmt`, among them `isec`, `viewec`, `x18` (0 bad x18 checks, 0 crash
  reports) and `msync` (`info msync 1 cs-hold-10us-p999 49300`, `info msync 0 … 49500`); then `FAIL dxmt-present:
  arm64ec present_loop: winshot: screencapture of window 62727 failed` with present_loop's 3000 frames done (8.324 ms
  average) and the console locked (`IOConsoleLocked` Yes): Ruling R17's case, as in Task 8. The steps after it, run by
  name (`check.sh dxmt-arm64ec dxmt-x64 g4-bench fex-vmd winetests ec-hook`, 1,954 s): all PASS and `PASS orphans`
  (g4-bench geomean 0.938 / 0.911 / calls 1.076; fex-vmd MP 15,834 with EVMD, 0 listed, 0 flags-only, mem_seq 0.49 /
  0.39 of default; winetests failing only at the six listed locations).
- `make test`: 252 tests passed. `make smoke`: 15/15. `make bridge-check`: passed (15 `ok`). `make media-check`:
  unchanged (`FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf: stage=video-type
  hr=0xc00d5212`).
- Export: `make wine-arm64-export` wrote one new file,
  `wine-arm64/patches/wine/0034-ntdll-Fill-the-ARM64EC-auxiliary-IAT-and-revert-an-e.patch`; the fresh-fetch proof
  printed `applied 34/34` and `tree-equal` (tree 658f66dc…). An applied-mode rebuild stages the same ntdll (its
  disassembly's SHA-256 is unchanged).

**Where the code differs from the plan's text**, each for a reason found while building it:
- `aux_revert`'s lock-free test is the page hash alone, without the plan's "nothing filled" count check: a fill sets the
  bits before its page query but counts an entry only after it, so a hooker whose protect lands between the two would
  read a count of 0, skip the revert and then write while the entry is filled. A range over 64 pages takes the lock and
  scans, whether or not anything is filled.
- The fill queries pages with the raw syscall: ntdll's `NtQueryVirtualMemory` is hybrid-patchable, and its call from EC
  code goes through `__os_arm64x_dispatch_call` on the x64 export (`#NtQueryVirtualMemory` → `EXP+#NtQueryVirtualMemory`
  in the object), so an x64 hook on it would run under `aux_lock`, where a VirtualProtect, or FEX processing the
  cross-process list before it compiles the hook, would take the lock again. `NtQueryVirtualMemory` becomes a
  pass-through wrapper over `syscall_NtQueryVirtualMemory`, like `NtQuerySystemInformation`; the x64 view's syscall
  stub (`signal_x86_64.c`) is unchanged.
- The `ec-hook` step sets ShowCrashDialog=0 in its prefix before running the program (the rows write x64 code into
  kernel32 and an IAT entry).
- The array is sized for all of the module's IAT entries, an upper bound on its candidates, rather than counted first.

**Gaps.**
- A protect or a write from another process takes effect here before FEX next processes the work list: an x64
  syscall, a block compile or a context sync (`Source/Windows/ARM64EC/Module.cpp:520, 562, 568`). An ARM64EC thread's
  own syscalls don't drain it, so in a process that never enters FEX the window has no bound. Calls in it skip the hook.
- The cross-process revert is untested: every `ec-hook` row hooks from inside the process (review finding F3, not in
  fix round 1).
- Known minor (Rulings R47, R48; recorded, not fixed): a page made `PAGE_NOACCESS` between the fill's query and its read
  still faults under the loader lock and `aux_lock`. The fault goes through `pResetToConsistentState` to FEX's
  access-violation handler, which takes ThreadCreationMutex; a thread that holds that mutex and waits on `aux_lock` (FEX's
  write-fault untrap, which protects through the wrapper's first path) closes a cycle. So a narrow hang is possible: it
  needs that no-access protect inside a fill and an untrap of a hashed page at the same moment.
- Delay-load IATs are not filled; syscall stubs stay on the checker.
- An entry resolved through a page that was writable at load keeps the checker for the process's lifetime (a fill is
  never retried).
- A hooker that writes an auxiliary IAT entry itself (an ARM64EC-aware IAT hook) is not tracked: a later revert through
  another dependency's page puts the stub back over its value.
- No Windows primary source says which entries Windows fills or how it detects patching; the per-entry revert is ours.

**Fix round 1 (Ruling R46; 0034 amended and re-exported, R46a).** The review found two Important problems and one minor:
- **F1.** A fill held `aux_lock` as plain EC code (neither InSyscallCallback nor InSimulation set), so a suspend stopped it
  on the spot. A hooker that freezes every other thread and then VirtualProtects a hashed page (MinHook) waited for
  `aux_lock` forever, inside FEX's ThreadCreationMutex, and the process hung. Fixed: `arm64ec_fill_aux_iat` and
  `arm64ec_forget_aux_iat` hold the lock between `enter_syscall_callback` and `leave_syscall_callback`. Nothing under
  the lock calls a wrapper: the object's calls there are `LdrFindEntryForAddress`, `memcpy` and the raw
  `syscall_NtQueryVirtualMemory` / `syscall_NtProtectVirtualMemory`.
- **F2.** The wrapper's second path reverted between FEX's pre- and post-protect notify, inside ThreadCreationMutex.
  Fixed: the revert comes after the post-notify, before `leave_syscall_callback`.
- **Minor.** The fill read target-code and slot pages without knowing they were readable. Now every page it reads is
  queried first (committed, readable, not a guard page), through the same once-per-fill cache. Every query now sets the
  page's hash bit just before it, so the order above holds for every page the fill queries.
- **The `suspend` row, RED on 6fe9c96:** three runs out of three, `FAIL suspend: the hooker's VirtualProtect waited 15 s with
  the loading thread suspended (24-27 rounds, 0 loads)`; on 0033 (control, 79874a0, no fill) `ok suspend` with 58,128 /
  58,263 rounds and 2,262 / 2,500 loads. **GREEN on the fix:** three runs out of three, all six rows `ok`, 54,758-55,527 rounds and
  2,000-2,504 loads (`t5-f1-*.log`).
- `check.sh winetests ec-hook`: `PASS winetests` (only the six listed locations) and `PASS ec-hook`. `make test`: 252
  passed.
- A diagnostic counting each module's filled entries (arm64ec, version.dll loaded) prints the same counts on 6fe9c96 and
  on the fix: the exe 38, kernel32 905, kernelbase 297, ucrtbase 160, version.dll 46, xtajit64 5
  (`t5-f1-auxcount-*.txt`, source `t5-f1-auxcount.c`, in the batch workspace).
- Lanes, two idle runs on AC (load 1.75-2.20; `t5-f1-lanes-run{1,2}.log`): the arm64ec xcall rows get-current-thread-id,
  get-last-error and tls-get-value read 0.7 ns as in t5, memcpy-16 1.9. get-tick-count reads 1.5 / 1.6 against t5's 1.1
  and qpc 15.9 / 16.6, while the arm64 lane, which has no auxiliary IAT, moved too (qpc 15.4 → 16.5, memcpy-4k-offset
  45.6 → 55.7 / 48.9). With the filled counts unchanged, qpc and the arm64 lane read as session variance. get-tick-count
  doesn't: fix round 1's 0034 (t5-f1) and the final 0034 (t6-control-0034 1.5/1.6/1.5; R47/R48 changed only the revert
  path) both read 1.5-1.6, so 1.1 was 6fe9c96's; later sets mix 0.7-0.9 and 1.5-1.6 runs in one session.
- Re-export: `git status --short wine-arm64/patches` shows only ` M …/0034-ntdll-Fill-the-ARM64EC-auxiliary-IAT-and-revert-an-e.patch`;
  the fresh-fetch proof prints `applied 34/34` and `tree-equal` (tree f7b61747…); an applied-mode rebuild stages the
  tested ntdll (disassembly SHA-256 137d8999…).
- **Ruling R47:** the revert's own `aux_lock` hold is bracketed too (`enter_syscall_callback` when InSyscallCallback isn't
  set, the matching leave), so every holder can only be suspended cooperatively. The three acquisition sites: the fill,
  the forget and `aux_revert`, all bracketed; `aux_revert`'s callers are the wrapper's two paths (InSyscallCallback
  already set, so the bracket is a no-op there) and the cross-process work list. Read in the code: every way into
  `KiUserEmulationDispatcher`, and so into FEX's BeginSimulation → SyncThreadContext → the work list, sets InSimulation
  first (`unix/signal_arm64.c:346` restore_context, `:1521` the SIGUSR2 return to emulated code, `signal_arm64ec.c`
  dispatch_syscall), and FEX's HandleSyscall and PreCompile run inside the JIT, which sets it (`Module.S` enter_jit);
  InSyscallCallback is not set on those paths. 0034 amended again (c2c443d), re-exported (only 0034's file changed),
  `applied 34/34`, `tree-equal` (tree af2fbae3…); the applied-mode rebuild stages the tested ntdll (disassembly SHA-256
  55d29f8a…). `ec-hook` 3/3 with all six rows `ok` (49,211-53,574 rounds, 1,984-2,046 loads); `check.sh winetests
  ec-hook`: only the six listed locations.
- **The full gate on c2c443d** (Steam up, `pgrep -lx steam_osx` 15329; AC at start and end): `make wine-arm64-check`
  every step PASS up to `dxmt` (`msync` cs-hold-10us-p999 48,500 / 50,200; x18 0 bad, 0 crash reports), then `FAIL
  dxmt-present: arm64ec present_loop: winshot: screencapture of window 63272 failed` with present_loop's 3000 frames done
  (8.325 ms) and the console locked (R17). The steps after it by name (`dxmt-arm64ec dxmt-x64 g4-bench fex-vmd winetests
  ec-hook`, 1,962 s): all PASS and `PASS orphans` (g4-bench geomean 0.907 / 0.898 / calls 1.103; fex-vmd MP 1,997 with
  EVMD, 0 listed, 0 flags-only, mem_seq 0.49 / 0.38 of default). `make test` 252 passed, `make smoke` 15/15, `make
  bridge-check` passed (15 `ok`), `make media-check` unchanged. Logs `t5-r47-*.log`.
- **Fix round 2 (Ruling R48).** The cross-process work list's revert ran before the Post entry's own
  `pNotifyMemoryProtect( …, TRUE, … )`, so it waited for `aux_lock` inside the ThreadCreationMutex the Pre entry's notify
  took. It now runs after the switch: after FEX's notify for that entry, still when `pNotifyMemoryProtect` is NULL, and
  with its R47 bracket. The "can't fault" and "no lock cycle" claims were corrected here and in the 0034 commit message
  (the known minor under Gaps). 0034 amended again (0c40afb), re-exported (only 0034's file changed), `applied 34/34`
  and `tree-equal` (tree 3fd8e8e3…). `ec-hook` 3/3 with all six rows `ok` (49,391-49,470 rounds, 2,046-3,901 loads);
  `check.sh winetests`: only the six listed locations; `make smoke` 15/15. `make test`: the first run failed one Swift
  test, `oneInstallAtATime()` (`AppModelTests.swift:192`, `installer.calls` was `[false, true, true]`); three re-runs
  passed 252/252. No Swift source has changed since fb0710a, so it is a flaky test, not 0034. The full gate is deferred to
  the batch's final run (R48). Logs `t5-r48-*.log`.

## CRT string routines from Arm Optimized Routines (batch Task 6)

2026-10-10. **Wine patch 0035 (`msvcrt: Use Arm Optimized Routines for memmove, strlen, strnlen, strchr, strrchr,
strcmp and memchr on ARM64.`, `macneutron` 887f9c1 on 0c40afb) replaces those byte loops, and memmove's scalar
shift-merge, with Arm's AdvSIMD routines on ARM64 and ARM64EC** in every DLL that builds `dlls/msvcrt/string.c`
(msvcrt, ucrtbase, msvcr80-120). It is ours and local only. `strlen-1k` drops from 232-234 ns to 9.6-9.8 ns in the
native lanes and from 259 to 32.8 ns from x64 code; the 16-byte copies halve.

What 0035 does:
- **`dlls/msvcrt/aor_string.h`** (new; `SPDX-License-Identifier: MIT`, then the provenance, the files' copyright
  lines, the MIT text and the list of changes): seven `static` naked helpers, `__attribute__((naked, aligned(64)))`,
  each one `asm` statement transcribed mechanically from Arm Optimized Routines v26.07 (commit
  `4be260a5117480382690c6d8c300bc784e927d76`): `aor_memmove` (`memcpy-advsimd.S`), `aor_strlen` (`strlen.S`),
  `aor_strnlen` (`strnlen.S`), `aor_strchr` (`strchr-mte.S`), `aor_strrchr` (`strrchr-mte.S`), `aor_strcmp`
  (`strcmp.S`), `aor_memchr` (`memchr.S`). Register `#define`s are `.req`/`.unreq`, constants `.set`, `L(x)` is
  `.L<routine>_x`; only the little-endian production branches; ENTRY/END, `.cfi_*`, BTI and `asmdefs.h` dropped, every
  instruction and `.p2align` kept. The one register change: `memcpy-advsimd.S`'s `tmp1` is x11, not x14 (ARM64EC can't
  use x13, x14, x23, x24, x28 or v16-v31). strchr/strrchr are the `-mte` versions (v0-v5). No SEH directives.
- **`string.c`**: under `#if defined(__aarch64__) || defined(__arm64ec__)` strlen, strnlen, strchr, strrchr and memchr
  return their helper's result, memmove gets an `#elif` after its x86-64 branch (memcpy already calls memmove), and
  strcmp keeps answering -1/0/1 (`(r > 0) - (r < 0)` of AOR's byte difference). The exports stay C, so ARM64EC keeps
  their entry thunks: `#strlen` is `b aor_strlen`, `strlen` in the x64 view enters through the thunk.
- **The transcription check** (`t6-transcription-check.log`, from `t6-aor-check.sh`, independent of the generator
  `t6-aor-transcribe.py`): per routine, the original's little-endian production lines and the helper's asm strings
  (minus `.req`/`.unreq`/`.set`) are identical (125, 81, 53, 44, 78, 85 and 59 lines); the `#define`s equal the
  `.req`/`.set` lines except `tmp1 x14` → `x11`; every `.req` has its `.unreq`. Every helper's section is
  `IMAGE_SCN_ALIGN_64BYTES`, and in the DLLs each helper sits on a 64-byte boundary.

**The download** (Ruling R22: the maintainer's OK for these files only). From
`raw.githubusercontent.com/ARM-software/optimized-routines/4be260a5117480382690c6d8c300bc784e927d76/` into a new
folder outside the repository, read, never built or run (`t6-aor-fetch.log`): every `git hash-object` equals the
plan's blob SHA-1 and every size its byte count; `LICENSE` begins `MIT OR Apache-2.0 WITH LLVM-exception`. SHA-256:

```
650afbf29f214451e02241adc42534e82c9d6ae2b38e2444b92b5a1ffcaf9346  LICENSE
8c910a128f14751fa9708d1c90c1de3ed215b472d294fc882270b377d3889ab5  string/aarch64/asmdefs.h
f423b85a6afab446252f6533c7398a70f85b4f259e61d1ecbeb70b1373ab784d  string/aarch64/memcpy-advsimd.S
49ada8b95569d0501539de234d33a879dd79eb1beb87156714488f998b30bff6  string/aarch64/strlen.S
892c50170bd2c5f3c9019687368d27cc872b54318457b9ea6bebb4f227d78f1a  string/aarch64/strnlen.S
fc0fb0a03b09ba4d3bbe25c80ba97a370cea9c8823be9ec2a9de385d8b0c355b  string/aarch64/strchr-mte.S
e0228e76de429ab392e3eac9631a81dfd175550ecfe4daa86906ded7497501ce  string/aarch64/strrchr-mte.S
11acc3dc966fd98411d46b07abeaea8c53b37c1d6eae2c4792265d9dd574431b  string/aarch64/strcmp.S
c73139a81e16172db0ac807162ecfeff3e7c711babcbeb5e7ff9bbdb0435f3c3  string/aarch64/memchr.S
```

**Licences.** Taken under MIT (Apache-2.0 stays out of the LGPL tree). `licenses/NOTICES.md` has the section `## Arm
Optimized Routines (in Wine: msvcrt.dll, ucrtbase.dll, msvcr80.dll-msvcr120.dll)` (the patch's file, repository, tag,
commit, the seven files, modified, the MIT election, the LICENSE's `Copyright (c) 1999-2022, Arm Limited.` and the
per-file lines); `licenses/README`'s Wine entry points to it. `licences_test.sh` checks the section's heading; on the
0034 bundle it printed `MISSING NOTICES.md section for Wine's Arm Optimized Routines` and `FAIL licences_test`
(`t6-licences-red.log`); on the 0035 bundle `PASS licences_test` and `PASS licences_test self-test`, whose new red is
the heading renamed (`t6-licences-green.log`). The release source archive carries the files without a change to
`release/source-archive.sh`: its `tree wine` is `git archive --prefix=wine/ HEAD` of the applied Wine tree, which holds
`wine/dlls/msvcrt/aor_string.h` (40,988 bytes, the committed blob; `t6-source-archive.log`), and `MacNeutron.tar` holds
patch 0035.

**The `crt` step** (`check.sh`, in `BATCH` after `ec-hook`; crash dialog off): `arm64-crt.c`, built `-fno-builtin` as
`arm64-crt.exe`, `arm64ec-crt.exe` and `x64-crt.exe` (under FEX); all eight routines are imports from
`api-ms-win-crt-*` (ucrtbase) in all three exes. Each routine is compared with a byte-loop reference; the bytes a
routine may read but must ignore hold what it looks for (0 before strlen/strnlen strings, `(char)c` before and after
strchr/strrchr strings and memchr ranges, 0 after `p + maxlen + 1` for strnlen, different bytes around strcmp's two
strings); string bodies put 0x01, 0x7f, 0x80 and 0xff in every 8 bytes. memmove and memcpy: n 0-300 × source and
destination offset 0-15 into a 0xee-filled 1 KB buffer compared whole, memmove's overlaps (k −64…64), memmove(NULL,
NULL, 0); strlen/strnlen: length 0-300 × offset 0-31, maxlen 0, L/2, L, L+1, SIZE_MAX, unterminated at the guard,
strnlen(NULL, 0); strchr/strrchr: length 0-64 × offset 0-15 × eleven `c`; strcmp: length 0-300 × 16 × 16 offsets,
equal and with ('a','b'), (0x01,0xff), (0xff,0x01), ('a',0) at 0, L/2, L-1; memchr: n 0-300 × offset 0-31 × five
bytes × four places, SIZE_MAX with the byte on the guard's last readable byte, memchr(NULL, c, 0). Page edges for every
routine: two 96 KB allocations, [0, 32K) and [64K, 96K) no access (16K host pages: 4K guards wouldn't fault), data
ending on `base + 64K − 1` and starting on `base + 32K`, length 0-300, one copy or strcmp operand in each allocation,
memmove's backward overlaps at both guards. `IsBadReadPtr` confirms all four guards before any case. Cases per lane:
memmove 155,016, memcpy 77,658, strlen 10,234, strnlen 51,472, strchr and strrchr 17,972 each, strcmp 1,006,458,
memchr 198,632; the step takes about 7 s.
- **Oracle, on 0034's byte loops** (`t6-oracle*.log`): all eight `ok` in all three lanes, `PASS crt`.
- **The test sees faults and mismatches** (scratch mutants of the harness, never in the repository; `t6-mutants.log`):
  one wrong routine each gave `FAIL arm64-crt: 8 of 8 routines failed`, each routine's first mismatch named (memmove
  `n 63 src+0 dst+0, byte dst+63: got 0x55, wanted 0xee`, memchr `n 1 +0 c 0x100 at -1: got p+1, wanted NULL`, …); a
  strlen that reads 16 bytes past its terminator gave `FAIL arm64-crt: strlen end guard, len 0 +63: exception
  0xc0000005`.
- **On 0035** (`t6-green*.log`): all eight `ok` with the same counts in all three lanes, `PASS crt`, `PASS orphans`.

**The register gate.** `ec_regs_check` (`lib.sh`, called by `build.sh` after `pe_baseline_check`) disassembles the
seven ARM64EC `string.o` with the toolchain's `llvm-objdump` (by path), dies if it fails or prints fewer than 1,000
instruction lines, and dies on x13, x14, x23, x24, x28 or v/q/d/s/b/h16-31. A scratch object with `mov x14, x0`
(llvm-mingw only warns `register X14 is disallowed on ARM64EC`) stops it with `wine-arm64: msvcrt's ARM64EC string.o
uses a register x64 code can't hold: mov x14, x0`, exit 1, and one with `mov v16.16b, v0.16b` likewise; 0034's and
0035's trees pass silently (`t6-red-gate.log`, `t6-codegen-after.log`). **Codegen**: `uminp` in ucrtbase.dll's
`aor_strlen` counts 0 before (no such symbol; `#strlen` was the 6-instruction byte loop) and 4 after (2 in each half);
each of the seven DLLs has its 4.

**Wine's own tests.** `msvcrt:string` joins the winetests step (`WINETESTS`, `winetests.sh`'s `MODULES`). On 0034 it ran
410,075 tests with 0 failures in each lane (`t6-winetests-0034*.log`), so no location was needed; on 0035 the same,
and the step passed with only the six listed `sync.c:416/:420` locations (`t6-winetests-0035*.log`).

**Lanes** (`build/lanes/t6/`, the 0035 applied build, against `build/lanes/t5/`, Task 5's three-run set; `t5-f1/` has
two runs). Three runs back to back under `caffeinate -i`, 10:36:47-10:44:45, AC at every start and end, idle checks
empty, load 2.26 → 3.09; Steam (running, never touched here) held one core at about 100 %
(`t6-lanes-run{1,2,3}.log`, `lanes_report.py build/lanes/t5 build/lanes/t6` in `t6-lanes-t5-vs-t6.log`):

| row (ns), median (min–max) of 3 | arm64 t5 → t6 | arm64ec t5 → t6 | x64 t5 → t6 |
|---|---|---|---|
| `strlen-1k` | 232 (232–232) → 9.8 (9.5–9.9) | 234 (232–241) → 9.6 (9.1–9.7) | 259 (259–259) → 32.8 (32.4–33.4) |
| `memcpy-16` | 1.9 (1.9–1.9) → 0.9 (0.9–0.9) | 1.9 (1.9–2.2) → 0.9 (0.9–0.9) | 25.6 (25.6–25.8) → 25.7 (25.4–25.9) |
| `memcpy-16-offset` | 2.6 (2.6–2.8) → 1.1 (0.9–1.1) | 2.8 (2.8–2.8) → 0.9 (0.9–1.1) | 27.2 (27.1–27.3) → 25.8 (25.5–26.1) |
| `memcpy-256` | 4.1 (3.5–4.1) → 3.0 (3.0–3.1) | 3.9 (3.7–4.1) → 3.2 (3.2–3.3) | 27.6 (27.4–27.8) → 26.4 (26.1–26.8) |
| `memcpy-256-offset` | 5.0 (4.8–5.0) → 3.3 (3.3–3.3) | 5.0 (4.9–5.3) → 3.3 (3.2–3.4) | 29.0 (28.9–29.1) → 26.4 (26.1–26.4) |
| `memcpy-4k` | 43.8 (43.8–43.9) → 43.9 (43.9–43.9) | 44.7 (44.6–47.2) → 44.3 (44.3–44.3) | 57.9 (57.5–58.0) → 55.8 (53.2–59.0) |
| `memcpy-4k-offset` | 45.6 (45.5–45.6) → 44.1 (44.1–44.1) | 39.4 (39.3–45.8) → 44.3 (44.2–44.3) | 74.0 (73.7–89.4) → 62.2 (53.3–68.2) |
| `memcpy-1m` | 15520 (15500–15620) → 15340 (15330–15360) | 15410 (15380–16350) → 15320 (15300–15420) | 15260 (15230–15260) → 15250 (15230–15280) |
| `memcpy-1m-offset` | 15310 (15290–15380) → 15370 (14100–15380) | 15660 (14580–16740) → 15360 (15350–15390) | 16280 (16270–16280) → 15280 (15270–15470) |
| `qsort-4k` | 103600 (101910–106950) → 107740 (105980–111630) | 159180 (157790–164870) → 163130 (162590–164090) | 3290480 (3290250–3295970) → 3360200 (3353320–3362680) |

- `strlen-1k` is lower and outside the band in all three lanes (−96 %, −96 %, −87 %).
- Outside the band and lower: memcpy-16 and -16-offset, -256 and -256-offset in both native lanes (−18 to −68 %) and
  in x64 except memcpy-16 (−4 to −9 %); memcpy-4k-offset arm64 and x64; memcpy-4k arm64ec; memcpy-1m arm64;
  memcpy-1m-offset x64. The others are inside the band. The `-offset` rows move source and destination together (by
  one byte), so they measure co-aligned copies, not the mutually misaligned case where the C loop was weakest.
- memcpy-4k-offset arm64ec 39.4 → 44.3 is inside the band against t5 but worse and outside the band against the
  same-session control (39.5 (39.4–42.1)): AOR copies 4 KB in about 44 ns at any alignment in both native lanes, where
  the arm64ec build's C loop did the co-aligned 1-byte offset in 39.5 (and the arm64 build's in 45.6-47.8).
- **A same-session control**: Wine 0c40afb (0034) checked out and built, three runs (10:46:44-10:54:58, AC, idle
  checks empty, load 3.09 → 2.96, Steam still busy; `build/lanes/t6-control-0034/`, `t6-control-lanes-run*.log`,
  `t6-lanes-control-vs-t6.log`, `t6-lanes-t5-vs-control.log`), then `macneutron` again and the applied build (same
  ucrtbase/msvcrt disassembly). Against t5 the control already shows cs-hold-10us-p999 at 70-90 µs (t5 49-50 µs) and
  x64 qsort-4k at 3.37 ms (t5 3.29 ms): the session's, not 0035's, whose code those rows' timed paths don't call.
  Against the control, the string rows above keep their moves, and the non-string rows outside the band are
  cs-hold-10us-p999 x64 (70 → 98 µs, ranges 69-73 and 93-102), auto-handoff-8 arm64ec (571 → 650 ns), create-close
  x64 (+1.8 %) and uncontended-wait arm64 (+5 %) on the worse side, wait-all-poll and wait-all-wake arm64 on the better
  side. Their timed paths are kernel32, kernelbase and ntdll calls, and those DLLs are built `-nodefaultlibs`: they
  never reach msvcrt's routines. Read as this session's noise (one core busy throughout).

**Gates** (0035 = 887f9c1, applied build; Steam up, `pgrep -lx steam_osx` 15329, before each run; AC at every start
and end):
- `make wine-arm64-check` (10:56-11:09, `t6-gate-check.log`): the four repo tests PASS, every step from `macos`
  through `dxmt-present` PASS (`x18`: 0 bad checks, 0 crash reports; `msync`'s cs-hold-10us-p999 95,300 / 57,200 in this
  busy session; `dxmt-present` with the screen unlocked), then `FAIL dxmt-arm64ec: … FAIL timestamps from any query
  heap ride the call's encoders: got [:hazard ts-many 0]`: d3d12_hazards' GPU timing check (`heavy 160875 alone
  383209`; it wants heavy ≥ alone / 2, and `alone` is usually about 134,000 ticks), the row whose flake is recorded
  under the XeSS plan's follow-ups (`alone` ten times its usual value while other lanes ran). The steps after it, run
  by name (`check.sh dxmt-arm64ec dxmt-x64 g4-bench fex-vmd winetests ec-hook crt`, 11:12-11:45,
  `t6-gate-rest.log`): all PASS and `PASS orphans`, ts-many `heavy 133917 alone 133125` (g4-bench geomean 0.940 /
  0.922 / calls 1.086; fex-vmd MP 28,040 with EVMD, 0 listed, 0 flags-only, mem_seq 0.46 / 0.40 of default;
  winetests only the six listed locations, msvcrt:string 0 failures in each lane; `crt` PASS).
- `make test`: 252 tests passed. `make smoke`: 15 PASS. `make bridge-check`: passed (15 `ok`). `make media-check`:
  unchanged (`FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf:
  stage=video-type hr=0xc00d5212`). Logs `t6-gate-{test,smoke,bridge-check,media-check}.log`.
- Export: `make wine-arm64-export` wrote one new file,
  `wine-arm64/patches/wine/0035-msvcrt-Use-Arm-Optimized-Routines-for-memmove-strlen.patch`; the fresh-fetch proof
  printed `applied 35/35` and `tree-equal` (tree 7dc06830…; `t6-fresh-fetch.log`). The applied-mode rebuild stages the
  tested ucrtbase.dll and msvcrt.dll (their disassembly's SHA-256 unchanged, `t6-applied-build.log`).
- Not covered: the mutually misaligned copy (the lanes' `-offset` rows are co-aligned); a game's own use. The routines
  read within the 16- or 32-byte aligned chunk that holds a string's last byte, never past its 4 KB page (strlen's
  unaligned first 32 bytes only when they can't cross one), so they fault nowhere the byte loops didn't.

## Redistributable builtins (batch Task 7)

2026-10-10. **`bundle.sh` now fails if the CRT and DirectX redistributables' builtins (the 26 in `CRT_BUILTINS`) could
stop replacing a game's own x64 copies** (native REPORT §3 R7, Ruling R14). Wine replaces a Microsoft DLL in a game's
folder with its builtin of that name because `version_heuristics` sends a DLL whose CompanyName is Microsoft to
`LO_DEFAULT` (`dlls/ntdll/unix/loadorder.c:431`) and the builtin doesn't prefer native (Wine's 0x10 in the
DllCharacteristics, `tools/winebuild/build.h:210`, read by `dlls/ntdll/unix/unix_private.h:430`). A builtin that
prefers native, or a rebase that changes either rule, leaves the game's x64 copy running under FEX, silently. No Wine
patch: Wine stays at 0035 (887f9c1).

**The guard.** `lib.sh`'s `prefer_native_check <aarch64-windows dir> <Wine source tree>`, called by `bundle.sh` after
the version-resource check, before staging (a failure stages nothing). It dies when one of `CRT_BUILTINS` has 0x10 set
(or its DllCharacteristics can't be read), when `unix_private.h` no longer defines
`IMAGE_DLLCHARACTERISTICS_PREFER_NATIVE` as `0x0010`, or when `loadorder.c` no longer has the row
`{'M','i','c','r','o','s','o','f','t',0}, LO_DEFAULT }`. `CRT_BUILTINS`, 26 names:

```
ucrtbase vcruntime140 vcruntime140_1 msvcp140 msvcp140_1 msvcp140_2 msvcp140_atomic_wait msvcp140_codecvt_ids
concrt140 vcomp140 msvcr120 msvcp120 msvcr100 msvcp100 d3dcompiler_43 d3dcompiler_47 d3dx9_43
d3dx10_43 d3dx11_43 xaudio2_7 xaudio2_9 x3daudio1_7 xapofx1_5 xinput1_3 xinput1_4 xinput9_1_0
```

On today's bundle all 26 read `0x160` and `mfplat.dll` `0x170` (`t7-guard-messages.log`). `msvcp60`, `msvcm80` and
`msvcm90` also prefer native (`0x170`), upstream's choice, and aren't listed; the README's "Next Wine rebase" says to
read upstream's reason before changing the list. The guard's messages, from scratch copies (`t7-guard-messages.log`):
`ucrtbase.dll prefers native (0x170): a game's x64 copy would run under FEX` (mfplat.dll copied over it), `can't read
xinput9_1_0.dll's DllCharacteristics` (missing), `ntdll no longer reads prefer-native as 0x0010
(dlls/ntdll/unix/unix_private.h)` (0x0020), `loadorder.c's version_heuristics no longer sends Microsoft DLLs to
LO_DEFAULT` (the row removed); each exit 1, and today's bundle and tree exit 0.

**The test.** `wine-arm64/tests/prefer_native_test.sh <wine.app>`, in `make wine-arm64-check` after
`translator_key_test.sh`: the staged bundle and the Wine tree pass; scratch copies of the 26 DLLs with `ucrtbase.dll`
replaced by `mfplat.dll` fail saying `ucrtbase.dll prefers native`; a scratch `dlls/ntdll/unix` whose `loadorder.c`
sends Microsoft DLLs to `LO_NATIVE_BUILTIN` (the brief's `sed`, checked to have changed the file) fails saying
`version_heuristics no longer sends`. Red first, before `lib.sh` had the guard: `prefer_native_check: command not
found`, `FAIL prefer_native_test: today's bundle and Wine tree`, exit 1 (`t7-red.log`); then `PASS prefer_native_test`
(`t7-green.log`). `make wine-arm64` staged the bundle with the guard passing (`t7-build.log`); with `msvcp60` added to
the list for one run, it stopped with `wine-arm64: msvcp60.dll prefers native (0x170): a game's x64 copy would run
under FEX`, staging nothing (`t7-bundle-red.log`), and the list was put back.

**The audit** (the controller's, Step 3): SMITE 2's lobby, logging on (`MACNEUTRON_LOG=1`), the log of 2026-10-09:

```
Loaded L"C:\\windows\\system32\\xaudio2_9.dll" … : builtin
Loaded L"~\\…\\SMITE 2\\Windows\\Engine\\Binaries\\ThirdParty\\Windows\\XAudio2_9\\x64\\xaudio2_9redist.dll" … : native
```

No `amd_ags_x64.dll`. The game's `xaudio2_9redist.dll` exports XAudio2Create, CreateAudioReverb,
CreateAudioVolumeMeter, CreateFX, X3DAudioCalculate and X3DAudioInitialize. Wine has no `xaudio2_9redist` builtin, so
the game's copy stays mapped and runs under FEX, but it hands its work to the builtin `xaudio2_9`: only its creation
entry runs as x64 code (builtin-overrides §5.2, rank 4). **xaudio2_9redist.dll loads native but delegates
to the builtin xaudio2_9 (SMITE 2 lobby log, 2026-10-09); the forwarder was not built (maintainer, 2026-10-10).** Steps
4-6 (Wine 0036, the `redist` step) were skipped; Wine 0036 stays free.

**Lanes** (`build/lanes/t7/` against `build/lanes/t6/`; the same Wine 0035 applied build (887f9c1, clean), FEX and test
programs as t6, so the only change is `bundle.sh`'s check, which runs before staging and writes nothing into the
bundle). Three runs back to back under `caffeinate -i`, 12:16:18-12:24:43, AC at every start and end, idle checks empty,
load 2.33 / 5.28 / 4.16 at the starts and 3.36 at the end; Steam (running, never touched here) was idle at the first
start and at about 99 % of one core from run 1's end into run 2, idle again by run 2's end (`t7-lanes-run{1,2,3}.log`;
`lanes_report.py build/lanes/t6 build/lanes/t7` in `t7-lanes-t6-vs-t7.log`, the t7 table in `t7-lanes-table.md`).
17 of the 99 rows are outside the band, all small:

| row | lane | t6 | t7 | Δ % |
|---|---|---|---|---|
| sync uncontended-wait | arm64 | 80.0 (77.0–83.0) | 72.0 (39.0–76.0) | −10.0 |
| sync wait-any-wake | x64 | 7708 (7532–7836) | 7953 (7923–8037) | +3.2 |
| sync alertable-wake | arm64ec | 7588 (7380–7630) | 7816 (7650–7822) | +3.0 |
| sync alertable-wake | x64 | 7684 (7579–7684) | 7878 (7872–7896) | +2.5 |
| sync auto-pool-8 | arm64ec | 49937 (48955–49946) | 47946 (46972–47959) | −4.0 |
| sync auto-pool-8 | x64 | 50946 (50944–50954) | 48958 (46970–48961) | −3.9 |
| xcall qpc | arm64 | 15.4 (15.4–15.4) | 15.8 (15.5–16.5) | +2.6 |
| xcall qpc | arm64ec | 16.0 (16.0–16.0) | 17.1 (16.1–18.6) | +6.9 |
| xcall qpc | x64 | 43.2 (43.0–43.7) | 44.9 (43.9–46.1) | +3.9 |
| xcall istream-addref | x64 | 24.7 (24.7–24.8) | 25.7 (25.6–26.1) | +4.0 |
| xcall memcpy-16 | x64 | 25.7 (25.4–25.9) | 26.3 (26.2–27.5) | +2.3 |
| xcall memcpy-256 | arm64ec | 3.2 (3.2–3.3) | 3.5 (3.4–3.6) | +9.4 |
| xcall memcpy-256 | x64 | 26.4 (26.1–26.8) | 27.4 (26.9–28.4) | +3.8 |
| xcall memcpy-256-offset | arm64ec | 3.3 (3.2–3.4) | 3.5 (3.5–3.6) | +6.1 |
| xcall memcpy-256-offset | x64 | 26.4 (26.1–26.4) | 26.8 (26.5–27.9) | +1.5 |
| xcall memcpy-4k-offset | arm64 | 44.1 (44.1–44.1) | 44.3 (44.2–47.2) | +0.5 |
| xcall qsort-4k | x64 | 3360200 (3353320–3362680) | 3376750 (3372700–3549350) | +0.5 |

They go both ways, in rows whose code comes from the same sources in both sets; the report's other 82 rows are inside
the band, `strlen-1k` among them (9.8 / 9.6 / 32.8 → 9.9 / 9.7 / 34.0 ns). **A second set** with Steam quiet
(`build/lanes/t7b/`, the bundle restaged on the commit, same Wine; 13:12:36-13:20:16, AC, idle checks empty, load 2.08 /
3.08 / 3.26 at the starts, Steam 0.2-0.3 % until run 3's end; `t7b-lanes-run{1,2,3}.log`) has 6 rows outside the band
against t6, all within 1.0-2.7 % and both ways: create-close x64 −1.6 %, auto-pool-8 x64 −2.0 %, alertable-wake
arm64ec +1.7 %, memcpy-16 x64 +2.7 %, memcpy-256-offset x64 +1.9 %, qsort-4k x64 +1.0 % (`t7b-lanes-t6-vs-t7b.log`).
qpc and the arm64ec memcpy rows are back inside. t7 against t7b, the same build twice, has 5 rows outside
(`t7b-lanes-t7-vs-t7b.log`): that is this measurement's floor between sessions, so the rows above are noise, not this
task's, whose change is in no lane's code.

**Gates** (Wine 0035 = 887f9c1, applied build, the bundle staged by `bundle.sh` with the guard; Steam up, `pgrep -lx
steam_osx` 15329, read-only, before each run; AC at every start and end; the screen unlocked):
- `make wine-arm64-check`, **one uninterrupted pass** (12:25:49-13:05:59, `t7-gate-check.log`), the batch's final gate:
  the six repo tests pass (`prefer_native_test` among them), then every step from `macos` through `crt` PASS and
  `PASS orphans`, exit 0. `g2-litmus` PASS (TSO on); `x18`: 35,598,100,000 checks, 0 bad, 0 crash reports; `msync`'s
  cs-hold-10us-p999 112,500 / 108,500; `dxmt-present` and `dxmt-arm64ec` PASS on the first run; g4-bench geomean
  0.949 / 0.908 / calls 1.104; fex-vmd MP 22,158 with EVMD over the module, 0 listed, 0 flags-only, mem_seq 0.46 /
  0.41 with the ranges (0.95 / 1.03 without); winetests only the six listed `sync.c:416/:420`, msvcrt:string 410,075
  tests and 0 failures in each lane; `ec-hook` and `crt` PASS.
- `make test`: 252 tests passed. `make smoke`: 15 PASS. `make bridge-check`: 15 `ok`, exit 0. `make media-check`:
  unchanged (`FAIL media-mf: FAIL arm64-media-mf: stage=video-type hr=0xc00d5212; FAIL x64-media-mf:
  stage=video-type hr=0xc00d5212`). Logs `t7-gate-{test,smoke,bridge-check,media-check}.log`.
- Not covered: a game that ships one of the 26 DLLs in its folder is not run here (the guard checks the rules that send
  it to the builtin, not a game's load); `CRT_BUILTINS` is a fixed list, so a redistributable builtin Wine adds later is
  guarded only once someone lists it.

## SMITE 2 on Wine 0033-0035 (batch final review)

2026-10-10, the first real-game run of Wine 0033 (critical sections on the Windows 2003 SP1+ encoding), 0034 (the
ARM64EC auxiliary IAT) and 0035 (Arm Optimized Routines in msvcrt), together with 0031, 0032 and FEX 0006. The final
whole-branch review asked for it before the next tag.

- **Runtime:** a development `build/MacNeutron.app` from `make app` at c7afa42 (`runtime=dev (128b17e148e7)` in
  `launcher.log`; its ntdll is `wine-11.19-35-g887f9c1`). Built-in display, AC power, Steam bridge on.
- **Launch line:** `/usr/bin/env DXMT_D3D12_SM6=1 MACNEUTRON_LOG=1 FEX_SILENTLOG=0 %command%`; the maintainer played the
  lobby and a practice match (game process 14:31:29-14:33:44, 2 min 15 s).
- **Result:** `exit=0`. The new log section has no `wait timed out`, no unhandled exception, no page fault and no
  `err:seh` line; it ends with the normal RPC shutdown and `AppPolicyGetProcessTerminationMethod`.
  - `exception outside of stack limits` warnings appear as in the earlier runs (3,998 here; 3,350 and 12,619 in the
    2026-10-09 FEX 0006 runs A and B), so they predate these patches.
- **FEX 0006** applied the same 16 images' tables as run A (`Hemingway-Win64-Shipping.exe`: 18 ranges, 119,767,488
  bytes).
- **DLLs loaded native:** the same set as run A (the game's x64 libraries, DXMT's `d3d12`/`dxgi`, the bridge's
  `steam.exe`, `xaudio2_9redist.dll`).
- **Load:** mean process CPU 381% over the in-game samples (after the first 30 s), peak RSS 5.0 GB; run A read 360%
  and 4.2 GB. The maintainer reported that the frame rate felt higher ("I feel like we won FPS"); no frame counter was
  recorded, so a higher CPU share at a higher frame rate is not a regression signal. One short session: treat the
  numbers as indicative.
- **Not covered:** a third-party hooking overlay (Discord, MinHook-style mods) loading during start-up, which is 0034's
  suspend case; an oversubscribed CPU; a long session.

## Shader Model 6.7 by default (DXMT 0039)

2026-10-10, for 0.4.0. **DXMT patch 0039 (`d3d12: Report the Shader Model 6 features by default.`, `macneutron`
0593068 on 179738a)** turns `SM6Caps()` on unless `DXMT_D3D12_SM6=0` (`d3d12_dxil_dump.cpp:63`, `== "1"` →
`!= "0"`); capture mode still forces it. Only `CheckFeatureSupport`'s answers change (its eight `SM6Caps()` reads in
`d3d12_device.cpp`): Shader Model `min(requested, 6.7)`, feature level 12_1, binding tier 3, wave ops with 32 lanes,
both 64-bit atomic bits. `src/d3d12` is outside `translator_key`, so translated shaders and recordings stay valid.
6.7 is the highest `D3D_SHADER_MODEL` in the llvm-mingw headers the build uses; no 6.8 or 6.9 operation is translated.
The translator has no shader model gate: an operation it lacks fails its own pipeline.

- Export: only `0039-*` is new in `wine-arm64/patches/dxmt`; a fresh fetch of the pin applies 39/39, `tree-equal`.
- Gate (`make dxmt-check`, both lanes, AC; `.superpowers/sdd/2026-10-10-sm6-default/dxmt-check.log`): `dxmt-check:
  all passed` twice, 570 `ok`, 0 `FAIL`. `d3d12_clear` now asks for 6_7, and the default run reads `shader model 0x67
  (hr 0x00000000)`, `resource binding tier 3` and `feature level 0xc100, wave ops 1, atomic64 1`. Capture mode with
  `DXMT_D3D12_SM6=0` still reads 0x67. `DXMT_D3D12_SM6=0` outside capture mode reads 0x51, tier 2, `0xb100, wave ops 0,
  atomic64 0`. The rows now also match the call's `hr` and start from an environment without `DXMT_D3D12_SM6` or
  `DXMT_DXIL_DUMP`. Each pattern was checked against the run's own outputs in both lanes: 2 matches in its file, 0 in
  the other two. `make test`: 252 passed.
- Game: SMITE 2 has run on this code path since 2026-10-01 with `DXMT_D3D12_SM6=1`, most recently on Wine 0033-0035 (the
  section above); 0039 makes that the default. No other Direct3D 12 game is on record with these answers.
- Not covered: a Direct3D 12 game that took its Shader Model 5.1 path in 0.3 and now takes its Shader Model 6 one.

## DXIL Shader Model 6.8 (DXMT 0040-0060)

2026-10-11. DXMT's DXIL translator learns the Shader Model 6.1-6.8 operations its caps require, one DXMT patch per task,
and the device then reports 6.8: spec `docs/superpowers/specs/2026-10-10-dxmt-dxil-sm68-design.md`, plan
`docs/superpowers/plans/2026-10-10-dxmt-dxil-sm68.md`, rulings S-R1 to S-R8 (`.superpowers/brainstorm-sm/rulings.md`).
Every translator task translates SMITE 2's captured shaders (`~/dxil-smite2`, 11,101) and `dxmt/tests/dxbc` offline
before and after: `dxil-translate`'s plain `ok` lines now end in `air=<16 hex>` (FNV-1a 64 of the translated metallibs,
in translation order) and `dxmt/tools/corpus-diff.py` compares two runs (outputs in `build/sm68/corpus/`, never
committed). The baseline at DXMT 0039, run twice with no difference (translation is deterministic):
`summary 11101/11101 ok, mean 23.5 ms, slowest 2816.7 ms cs-e15dfce8559f19fe.dxil`, and `summary 2/2 ok` for DXBC.
The translator key before the series (S-R7) is `d6863eccd27c3bbe36a00feeb31dbcd430a45ad9b3d0fdeb39336f9a0c1dfe2a`; no
release or tag until the last task records the new one.

**Summary (Task 16, 2026-10-12).**
- **Caps on the default device, before → after** (`d3d12_clear`, DXMT 0059):
  - shader model `0x67` → `0x68`;
  - OPTIONS11 heap `0` → `1`;
  - OPTIONS21 comparison sampling with gradient or bias `0` → `1`, extended command info `0` → `1`;
  - OPTIONS22 `E_NOTIMPL` → answered.
- **OPTIONS9's two 64-bit atomic bits** were already 1 and are now honest. Every 64-bit atomic goes through the
  translator's lock path (0054, 0055, 0057; ruling R-A64), so no atomic cap is over-reported. `Int64ShaderOps` stays
  FALSE until the i64 audit (the next spec). Float atomics need no cap bit (Task 14).
- **64-bit atomics, for the maintainer:**
  - **Known non-conformance:** a 32-bit atomic or a plain store racing a 64-bit atomic on the same 8 bytes isn't atomic
    with respect to it.
  - **Costs** (spec §3.8; M5 Pro, GPU ns per operation; uncontended / one address per simdgroup / all threads on one
    address):

    | Kind | Lock path | Native 32-bit | Slowdown |
    |---|---|---|---|
    | Buffers and typed buffers | 0.131 / 3.06 / 421 | 0.012-0.017 / 0.25 / 0.69 | ~8-11x / 12x / ~600x |
    | Textures (`RG32Uint`) | 0.161 / 1.87 / 628 | 0.021 / 0.30 / 0.71 | ~5-8x / 6x / ~900x |
    | Groupshared (+ 256 / 1,024 lanes on 1 address) | 0.059 / 0.875 / 7.5 / 19.2 | 0.011 / 0.042 / 0.10 / 0.21 | 5x / 21x / 75x / 90x |

  - **The cliff:** thousands of threads on one address take seconds per dispatch (65,536 threads into 1-16 locks:
    1.4-2.5 s).
  - 64-bit atomics: not measured on M1 (Apple7): the 14a-14c groups ran on an M5 Pro only; M1 is not claimed until
    they run on one.
- **Translator key:** `d6863ecc…` → `c588d3fc4ca5bf64ef6f202ba6d5aa9eae99189996738866942dfb39dfe274b5` (with 0060; it was
  `17e0b1ee…` at 0059).
- **Corpus, 0039 → 0060:** 0059 reports caps only, and 0060 touches `src/airconv` again.
  - SMITE 2: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 6 changed, 11095 unchanged`. These are the six pixel
    shaders the per-task bullets above explain.
  - DXBC: `2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`.
- **Final tree** (DXMT 0060, after the final review's fix): `dxmt-check: all passed` twice, 373 `ok` (x64 lane) and 363
  `ok` (ARM64EC lane), 0 `FAIL`. `make test`: 320 passed. Milestone 3, on 0059, had 369 and 359 `ok` and 290 tests.
- **What rests on offline translation or has no row:**
  - Op 109 is dropped (no HLSL intrinsic emits it).
  - Op 75 and the pixel-shader `SampleCmpBias`/`Grad` lowering are checked offline only, so OPTIONS21's comparison flag
    rests on compute behaviour.
  - A WaveSize refusal answers `E_FAIL`, where Microsoft's runtime likely answers `E_INVALIDARG`.
  - No row covers `WaveMatch`/`WaveMultiPrefix*` masking in pixel shaders, `QuadVote` in 2D compute, the indexed
    geometry-shader start-vertex path, 64-bit atomics in a geometry pipeline, or the lock table's re-zero on a
    command-buffer error.
- **Release notes**, for the next version's Graphics section: "DXMT reports Shader Model 6.8.
  ResourceDescriptorHeap, compute-shader derivatives, float and 64-bit atomics and the Shader Model 6.5-6.8 wave and
  quad operations are translated. This update changes the shader translator: each game rebuilds its shaders once, at
  its next start."

- **0040 (`airconv: Lower IsHelperLane, QuadVote, Msad, the sample-position queries and ViewID.`):** `IsHelperLane`
  (221) is `air.simd_is_helper_thread` in a pixel shader and false elsewhere; `QuadVote` (222) is `air.quad_any` or
  `air.quad_all`; `Msad` (50) calls airconv's `dxmt.msad.i32`; `Texture2DMSGetSamplePosition` (75) and
  `RenderTargetGetSamplePosition` (76) call `dxmt.sample_pos.i32` with the texture's or the render target's sample
  count; `RenderTargetGetSampleCount` (77) is `air.get_num_samples`; `ViewID` (138) is 0. `InitializeDXIL` sets
  `use_msad` and `use_samplepos`, so the compiles link the helpers; 76 and 77 no longer fail as "resource handle
  through phi/select". The op enum also gains the numbers later tasks lower (165-167, 244, 245, 254-257).
  - Rows: `DXIL helperquad matches its CPU reference` and `DXIL msad matches its CPU reference` read `match`
    (`dxmt/tests/expect.py`, our DXMT only); `ViewID, the render target's sample count and position read 0, 1 and
    (0,0)` reads `yes` (`sm6x.ps` draws `triangle2`'s pixels; its zero term includes `IsHelperLane()`, which DXC keeps
    as op 221 in a pixel shader). `triangle2.hlsl`'s vertex shader adds `msad4(id, uint2(id, 0), 0).x` (0) to `uv`, so
    the existing triangle rows run `Msad` in a vertex shader, the geometry pipeline included. `dxil-translate` accepts
    15/16 behaviour shaders (heap is still refused) and 36/36 test shaders (`36:36` with no NaN or infinity assumed
    away; 12 vertex and geometry shaders without reassoc, contract or arcp).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`. `corpus-diff.py` fails on an `ok` line
    without `air=` and on a shader missing from the second run.
  - Gates: `make dxmt-check` (both lanes; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-1-fix1-dxmt-check.log`):
    `dxmt-check: all passed` twice, 508 rows `ok`, 0 `FAIL`. `make test`: 252 passed.
  - Found: airconv's Metal helpers (`air_samplepos`, `air_msad`) are compiled with Metal's fast math (instruction flags
    and function attributes such as `"no-nans-fp-math"`). The first build left `nnan=4 ninf=4` in both `sm6x` shaders,
    and "no translated shader assumes NaN or infinity away" read `36:34`. The DXIL float policy moved from `LowerBody`
    into `StripFastMath`, which a DXIL compile also runs on the whole module once it has linked a helper, dropping the
    helpers' fast-math function attributes too (D3D11's helpers are unchanged).
  - Found: upstream's geometry pipeline links helpers on the geometry shader's `shader_info`, though its object function
    runs the vertex shader, so a DXIL vertex shader's `msad` was an undefined symbol there ("Failed to create geometry
    PSO: ... Undefined symbols", with the `msad` vertex shader on the first 0040). A DXIL pipeline now also links the
    vertex shader's helpers; D3D11 keeps upstream's behaviour.
  - Found: DXC 1.9.2607 folds `IsHelperLane()` to false in a compute shader, so `helperquad.dxil` has no op 221
    (`dxil-probe`) and its slot 0 checks DXC; `sm6x.ps` runs the pixel shader path.
  - Not covered at runtime: op 75 (`sm6x.cs` is translated offline only) and op 76 on a multisampled target. The rows
    run a one-sample target, whose position is (0,0) for any index, so a wrong position table, a swapped argument or a
    wrong index operand would not show yet.
- **0041 (`airconv: Leave helper lanes out of pixel shader wave operations.`):** a pixel shader whose entry function
  lacks `"waveops-include-helper-lanes"` (`[WaveOpsIncludeHelperLanes]`, read in `InitializeDXIL`) leaves its helper
  lanes out of wave operations, as D3D does; Metal's simdgroup functions count them. The lanes taking part are the
  ballot of `!air.simd_is_helper_thread()` at each operation: `WaveIsFirstLane` (110) and `WaveReadLaneFirst` (118)
  use the lowest of them; `WaveAnyTrue` (113), `WaveActiveBallot` (116), `WaveAllBitCount` (135) and
  `WavePrefixBitCount` (136) see false from a helper, `WaveAllTrue` (114) true; `WaveActiveAllEqual` (115) compares
  with the first lane taking part; `WaveActiveOp`/`WavePrefixOp` (119/121) and `WaveActiveBit` (120) get each
  operation's identity from a helper (a float sum's is -0.0). 111, 112, 117 and the quad operations are unchanged, and
  so is every other stage, or a pixel shader with the attribute.
  - Rows: `pixel-shader wave ops leave helper lanes out by default`, `and count them with
    [WaveOpsIncludeHelperLanes]` and `the always-green image drew, its 12 sampled pixels green` read `ok`.
    - `helper.hlsl`: `helper.ps` checks each wave operation, and `helper.psinclude` checks a helper's vote. Both are
      compared with `helper.psgreen`'s image hash, whose 12 sampled pixels the third row reads as green.
    - Every masked operation has a check whose helper operand differs from that operation's identity:
      - sum, prefix sum and prefix product (121 kind 1) of `1u + h`;
      - float min, max and sum (+inf, -inf, -0.0);
      - signed `int` min and max;
      - a `WaveActiveBitXor` of lane-distinct bits.
      The existing `1u - h` sums stay as the references for 135 and 136.
    - Before the patch the first row read `FAIL`, with 4 of the 12 sampled pixels red. It also reads `FAIL` with the
      patch when only one operation goes unmasked. That held for each of three single-op mutations: 118 alone, 110
      alone, and 119's sum alone (3, 3 and 4 sampled pixels red).
    - `dxil-translate` accepts 39/39 test shaders: `39:39` with no NaN or infinity assumed away.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 6 changed, 11095 unchanged`, the changed shaders
    `ps-2da27d2ee0b8be5d`, `ps-2e65d63c8b2298cf`, `ps-4e6f229dfb1baa34`, `ps-86b411ed29256949`, `ps-cd6b6058f8957a24`
    and `ps-de7c851d2fbd9e11`. These are six pixel shaders, and their first lane is now a covered one.
    - The controller's read-only `dxil-probe -S` over `~/dxil-smite2` found that each of the six calls
      `waveReadLaneFirst` (118) once. They are the only pixel shaders in the corpus that use any of ops 110-121 or
      135-136.
    - No compute, vertex or geometry shader changed.
    - DXBC: `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`.
  - Gates: `make dxmt-check` (both lanes; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-2-fix1-dxmt-check.log`):
    `dxmt-check: all passed` twice, 514 rows `ok`, 0 `FAIL`. `make test`: 252 passed.
- **0042 (`airconv: Lower WaveMatch and the multi-prefix wave operations.`):** `WaveMatch` (165) returns
  `{lanes 0-31, 0, 0, 0}`, `WaveActiveBallot`'s shape. It loops over the distinct values: each pass reads the first
  remaining lane's value (`air.simd_broadcast_first`), and the lanes equal to it take their ballot and leave. Floats
  compare with `==`, and the first lane (`air.simd_is_first`) always leaves, so a NaN ends the loop too and a lane's own
  bit is always set. `WaveMultiPrefixOp` (166: sum, and, or, xor, product) folds, in lane order, the active lanes of
  `mask0` below this one: 32 shuffles a call, from 0 (+0.0 for a float sum), all ones (and) or 1 (product).
  `WaveMultiPrefixBitCount` (167) is the popcount of the ballot ANDed with the same lanes. In a pixel shader helpers
  are left out as in 0041. `AddWaveInputs` defines the lane index for 165-167.
  - Rows: `DXIL wavematch matches its CPU reference` is `ok` (`expect.py` prints `match`). `wavematch.hlsl`
    partitions each wave by `key = (w >> 9) & 3` and writes `WaveMatch(key)` (its lanes 32-127 as 0), a float
    `WaveMatch`, the five `WaveMultiPrefix` kinds and `WaveMultiPrefixCountBits` over that mask, and an integer-valued
    float prefix sum. The groups are uneven (6 to 9 lanes), differ between the two waves, and the fourth key first
    appears at lane 4. `dxil-translate` accepts 16/17 behaviour shaders.
    - In the float `WaveMatch`, the key-3 lanes hold a quiet NaN, and each one's mask is only its own lane.
    - Slots 10-14 run under `if (id.x % 3 != 0)`, and the other threads write `0xe15e` to slot 10. In those slots
      `WaveMatch(key)` sees only the lanes that are on, and the sum, product, and and count prefixes over the outer
      `m` skip m's lanes that are off.
    - Before the patch: `fail wavematch.dxil DXIL: dx.op.waveMatch.i32 (165) not supported` (15/17). `dxil-probe -S`
      shows all fourteen calls kept: `waveMatch.i32` and `.f32` (on `select …, float 0x7FF8000000000000, …`),
      `waveMultiPrefixOp.i32` kinds 0-4, `.f32` kind 0 and `waveMultiPrefixBitCount`; then, in the block behind
      `urem %id, 3`, `waveMatch.i32`, kinds 0, 4 and 1, and `waveMultiPrefixBitCount`.
    - With the patch, the row reads `FAIL` under each of nine single-op mutations, each on its own build. The first
      seven ran before the NaN and slots 10-14 were added:
      - 165 without the loop: thread 2 slot 0;
      - 165 as four passes reading lanes 0-3 (it assumes they hold every value): thread 4 slot 0, the 13 lanes whose
        key first appears at lane 4;
      - 165's float compare dropped: slot 2 only, all 64 threads;
      - 166 without `mask0`: slots 3-7 and 9, not 8;
      - 166's and starting at 0: slot 5 only;
      - 166's float sum starting at -0.0: slot 9 only, the 8 lanes that start a partition;
      - 167 without the lanes-below mask: slot 8 only.
      The float-compare mutation also shows the loop ending through `simd_is_first` alone. On the final test:
      - 165 without the `simd_is_first` exit: a NaN first lane never leaves. The program's 10 s fence wait gives up
        (`GPU work never finished`), and the row reads `wavematch missing`;
      - 166 without the filter for lanes that are on: slots 11-13 only (sum, product, and).
    - Found: the first version keyed on `w & 3`, which is `lane & 3` (`InWord`'s multiplier 2654435761 is 1 mod 4):
      four stride-4 groups headed by lanes 0-3, the same in both waves. The four-pass break passed it, so the key moved
      to bits 9-10 (ruling R-T3a).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted (no
    SMITE 2 shader calls 165-167); DXBC `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`.
  - Gates: `make dxmt-check` (both lanes; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-3-r2-dxmt-check.log`):
    `dxmt-check: all passed` twice, 516 rows `ok`, 0 `FAIL`. `make test`: 252 passed.
  - Not covered at runtime (spec §8): helper lanes in 165-167. They were checked once, by hand: two scratch pixel
    shaders calling 165-167, one with `[WaveOpsIncludeHelperLanes]`, translated with `dxil-translate` (`2/2 ok`).
    They aren't committed, and no row repeats the check.
- **0043 (`airconv: Refuse a WaveSize range that excludes 32 lanes.`):** `ReadEntry` reads `[WaveSize]` into
  `EntryInfo::wave_size_min` and `wave_size_max` (0 without it): entry property 11 (WaveSize, `!{N}`) sets both to N,
  and 23 (WaveSizeRange, 6.8: `!{min, max, preferred}`) sets min and max, a max of 0 meaning min alone.
  `InitializeDXIL` refuses a shader whose range leaves out 32, the only width Apple GPUs run (the device reports
  `WaveLaneCountMin` and `Max` 32): `DXIL: [WaveSize(<min>, <max>)] excludes this device's 32-lane waves`. D3D12's
  `CreateComputePipelineState` then answers `E_FAIL` (`CachedShader::Parse`). The preferred size is ignored. Refused
  test shaders live in `dxmt/tests/dxil/reject/`, which `dxil-translate` and D3D12 must refuse.
  - Rows: `DXIL wavesize32`, `wavesize32r` and `wavesize matches its CPU reference` are `ok` (slot 0
    `WaveGetLaneCount()` 32, slot 1 the lane). `wavesize32` is `[WaveSize(32)]` at cs_6_6 (property 11, `!{i32 32}`),
    `wavesize32r` the same source at cs_6_8 (property 23, `!{i32 32, i32 0, i32 0}`), and `wavesize`
    `[WaveSize(16, 64, 32)]` at cs_6_8 (`!{i32 16, i32 64, i32 32}`); `dxil-probe -S` shows each. `D3D12 refuses a
    pipeline whose [WaveSize] excludes 32` reads 2 (`group wavesize64 fail 0x80004005` and `group wavesizerange …`,
    Lane C); `dxil-translate refuses every out-of-scope shader` reads `0/2` and `a [WaveSize] range without 32 is
    refused, naming it` reads `WaveSize(64, 64)] excludes;WaveSize(8, 16)] excludes;` (Lane E). `dxil-translate`
    accepts 19/20 behaviour shaders.
    - `reject/wavesize64` is `[WaveSize(64)]` at cs_6_6 (`!{i32 64}`); `reject/wavesizerange` is `[WaveSize(8, 16)]`
      at cs_6_8 (`!{i32 8, i32 16, i32 0}`). The plan had `(64, 128)` there: with both minimums above 32, a check
      that only tested `min <= 32` passed every row. Now each half of the check has a shader only it refuses.
    - Before the patch: `summary 2/2 ok`, and both reject groups ran, `WaveGetLaneCount()` 32: `[WaveSize(64)]` ran
      at 32 lanes without a word.
    - With the patch, single mutations, each on its own build: max 0 kept as 0 (wavesize32r refused, `18/20`); only
      `min <= 32` tested (wavesizerange runs: `1/2`, the D3D12 row 1); only `32 <= max` tested (wavesize64 runs);
      property 11 ignored (wavesize64 runs); 23's preferred read as max (the naming row reads `WaveSize(8, 8)]`).
      Each turns at least one row `FAIL`.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`.
    - Found: the plan's reason was that SMITE 2 has no `[WaveSize]`, but a read-only `dxil-probe -S` over
      `~/dxil-smite2` finds 48 cs_6_6 shaders with property 11 `!{i32 32}`, `[WaveSize(32)]`. They are accepted
      and translate as before (the property changes no code), which is the case `wavesize32` checks. No shader in
      the corpus has property 23 or another size.
  - Gates: `make dxmt-check` (both lanes; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-4-dxmt-check.log`):
    `dxmt-check: all passed` twice, 528 rows `ok`, 0 `FAIL`. `make test`: 252 passed. A first run, started with
    `taskpolicy -b` while another session's GPU probe ran, timed out XeSS and upscale runs (the 120 s watchdog) and a
    minimised-present timing row in the x64 lane; none touch DXIL, and the rerun on an idle machine passed.
- **0044 (`airconv: Carry 16-bit signature elements as 32-bit values.`):** signature registers stay 32-bit, so a
  `loadInput` (4) of a `half` reads the register as a float and `fptrunc`s it (an `i16` one already truncated), and a
  `storeOutput` (5) of a `half` `fpext`s it to a float; of an `i16`, it sign-extends when the element's component type
  is `I16` and zero-extends otherwise (`U16`). Before, a half's bits crossed the register zero-extended (a denormal
  float to the rasterizer and the render target) and a half read took a float's low 16 bits. The conversion is keyed
  on the call's overload, so the geometry shader's reads of vertex outputs convert the same way; `Cast` is unchanged.
  DXC gives min precision (`min16float`, `min16uint`, `min16int`) and native 16-bit types (`-enable-16bit-types`) the
  same overloads and component types (`F16`, `U16`, `I16`; `dxil-probe -S`), so both forms take this path.
  - Rows (Lane C): `the float form of the 16-bit varyings draws as D3DMetal`, `min16float and min16uint varyings draw
    as float ones`, `and float16_t and uint16_t ones` and `and a float pixel shader reads them as float, uint and int`
    read `yes`. `varying16.hlsl` draws one triangle as float (`vs32`/`ps32`), min precision (`vs16`/`ps16`,
    `-D MIN16`) and native 16-bit (`vs16n`/`ps16n`, `-D NATIVE16 -enable-16bit-types`), each at 6.6: a 16-bit IA
    input (`POSITION`, from an `R32G32_FLOAT` buffer), an interpolated 16-bit `TEXCOORD0`, two flat 16-bit integers
    and a 16-bit `SV_Target`. Both 16-bit forms draw the float form's image (the same hash, `c7a3ffaf12c9a820`). The
    fourth row runs `vs16` with `ps32`, whose 32-bit loads see the registers whole.
    - The plan's shader had one flat `uint` tag of 200, which zero- and sign-extension both carry, and its 16-bit pixel
      shaders truncate what they read, so neither row could see the integer extension. The tag is now `0x80C8` (bit 15
      set; alpha `tag >> 8` = 128) and a flat `bias` of -56 (`int`, `min16int`, `int16_t`; blue `bias + 128` = 72)
      takes the blue channel in place of `1 - uv.x`.
    - The first row anchors the float form, which the other three compare with, on D3DMetal (`tri-v32-ref`, the same
      hash): a break common to all four forms passes those three but fails it. Shown with a mutation that reads every
      `loadInput` as 0 (all four draw only the clear colour): the first row `FAIL`, the other three `ok`. No row runs
      the 16-bit forms on D3DMetal; a one-off run drew both within 1/255 of ours.
    - D3DMetal creates the pipeline and draws only the clear colour for `vs16` with `ps32`, so the fourth row has no
      D3DMetal reference: it checks what our registers hold.
    - Before the patch the three 16-bit rows read `FAIL` with every sampled pixel black: the IA corners (0 and 2)
      read as 0 and the triangle collapsed. With the patch, single mutations, each on its own build: the load's
      `fptrunc` dropped (all three, black); the store's `fpext` dropped (all three; the 16-bit forms draw
      `00000000`, the fourth row reads uv as 0); every `i16` zero-extended (fourth row, blue 255); every `i16`
      sign-extended (fourth row, alpha 255); `I16` and `U16` swapped (fourth row, both 255). These ran before the
      first row was added.
    - Covered by code reading only, no row: the geometry shader's reads of 16-bit vertex outputs (the same load site),
      16-bit IA formats, 16-bit integer render targets, and centroid or sample 16-bit varyings.
    - `dxil-translate` accepts 45/45 test shaders: `45:45` with no NaN or infinity assumed away, and 15 vertex and
      geometry shaders without reassoc, contract or arcp.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`. A read-only `dxil-probe -S` over
    `~/dxil-smite2` finds no `loadInput` or `storeOutput` with an `f16` or `i16` overload.
  - Gates: `make dxmt-check` (both lanes; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-5-r1-dxmt-check.log`):
    `dxmt-check: all passed` twice, 536 rows `ok`, 0 `FAIL`. `make test`: 252 passed.
- **0045 (`airconv: Pass programmable texture offsets through.`):** a texture offset that isn't a constant (Shader
  Model 6.7's programmable offsets, and `TextureGather`'s at any model) was read as 0. `TextureLoad` (66) and the
  `Sample*` family (60-65) now take the runtime value wrapped to D3D's low 4 bits, sign-extended (`(x << 28) >> 28`);
  `TextureGather` (73) and `TextureGatherCmp` (74) pass it through unwrapped, as the DXBC path's `gather4_po` does.
  Constant offsets keep their immediate form. In `nt/`, both `AIRBuilder::GetOffset` forms are public (the new one
  builds the offset vector from values), `CreateSample`, `CreateSampleCmp` and `CreateSampleGrad` gain `Value *Offset`
  forms, and `CreateSampleGrad`'s immediate form now forwards to its value form, which the D3D11 converter calls too.
  - Rows: `DXIL offsets matches its CPU reference` reads `match` (`dxmt/tests/expect.py`, our DXMT only), and
    `dxil-translate` accepts 20/21 behaviour shaders. `offsets.hlsl` (cs_6_7) reads `Tex` from texel (4, 4) with
    offsets from `{-4, -1, 0, 3, 12, 13, -13, -15}` picked by the thread's input word (all 64 x, y pairs for `Load`
    and `SampleLevel`; 16 pairs from the first four for `GatherRed` and `GatherGreen`), and stores each texel's
    bytes. DXC keeps every offset a runtime value (`dxil-probe -S`: loaded from `@kOff` at a runtime index).
    - Beyond the plan's shader: slot 3, `GatherGreen` with the same offsets. Red holds only x, so with slots 0-2
      alone a gather whose y offset reads 0 passed (shown with that mutation).
    - Before the patch the row read `FAIL` (thread 0 slot 0: `ff808080`, texel (4, 4)): 63 of 64 threads wrong in
      `Load` and `SampleLevel`, 48 in each gather. Single mutations, each on its own build: `Load`'s runtime offsets
      0 (slot 0 only, 63 threads); `Sample*`'s offsets 0 (slot 1, 63; and `DXIL textures matches D3DMetal`, whose
      constant offset goes too); the gathers' offsets 0 (slots 2 and 3, 48 each); the gathers' y offset 0 (slot 3
      only); the wrap removed (slot 0, 48 threads); the wrap zero-extended (`lshr`; slot 0, 48); x and y swapped
      (all four slots, and the textures row); constant offsets 0 (the textures row only).
    - Found: removing the wrap leaves `SampleLevel` right on this M5 Pro: the sampler keeps a runtime offset's low 4
      bits itself, as spec §2's probe found on Apple10. Only `Load`, whose offset we add to the position, shows the
      wrap; the `Sample*` wrap stays for D3D's rule on any GPU. A one-off D3DMetal run of the shader (not a row;
      `build/sm68/logs/exec-t6-ref.txt`) matches the CPU reference in slots 1-3 on all 64 threads and in slot 0 on the
      16 whose offsets lie in [-8, 7]; for the other 48 it reads 0 (`Load` unwrapped, out of bounds).
    - Found (code reading; no symptom seen): the sample ops' offsets were read up front for every op after the
      `Load`/`Store` switch, so `CalculateLOD` (81, seven arguments) read arguments 7 and 8 past its last one, and a
      gather read its channel as o2. Harmless while only constants were taken; now the `Sample*` cases read theirs
      each on its own.
    - Covered by code reading only: gathers passing their offsets unwrapped (the shader's gather offsets lie in
      [-8, 7], where the wrap changes nothing, and Metal's gather keeps [-8, 7], the gap spec §3.6 records); runtime
      offsets on `Sample`, `SampleBias`, `SampleGrad`, `SampleCmp`, `SampleCmpLevelZero` and `TextureGatherCmp` (the
      same lambda and overloads as the rows' ops); 3D, array and multisampled textures (a multisampled `Load` adds its
      offset to the position as before, since `GetOffset` has no multisampled form).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged` (`grad.dxbc`'s `sample_d` goes through
    `CreateSampleGrad`'s forwarding). A read-only `dxil-probe -S` over `~/dxil-smite2` finds 81,131 calls of ops
    60-66, 73 and 74, 295 of them with a non-zero offset, every offset a constant.
  - Gates (the maintainer's process change of 2026-10-11: one lane between milestones): `dxmt/check.sh` in the ARM64EC
    lane (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-6-dxmt-check.log`): `dxmt-check: all passed`, 264 rows `ok`
    (263 before, plus the offsets row), 0 `FAIL`. The x64 lane and `make test` (no Swift touched) were not run.
- **0046 (`airconv: Emulate derivatives and implicit-LOD sampling in compute shaders.`):** a Metal kernel may not call
  `air.dfdx`/`air.dfdy` (pipeline creation refuses it), and an implicit-LOD sample in a kernel read LOD 0. In a DXIL
  compute shader, `DerivCoarseX`..`DerivFineY` (83-86) now read the quad with `air.quad_shuffle`, from Metal's quad lane
  `l` = simd lane & 3 (never D3D's flattened ID): coarse `s(1) - s(0)` (x) and `s(2) - s(0)` (y), fine
  `s(l | 1) - s(l & 2)` and `s(l | 2) - s(l & 1)`. `Sample`, `SampleBias` and `SampleCmp` (60, 61, 64) take the quad's
  coarse gradients of their coordinates through the gradient path (scaled by 2^(sampler bias), `SampleBias` by
  2^(bias + sampler bias)); `SampleCmp` through the new `AIRBuilder::CreateSampleCmpGrad`
  (`air.sample_compare_depth_2d_grad.f32` and its array and cube forms). `CalculateLOD` (81) is
  `0.5 * log2(max(|ddx · size|², |ddy · size|²))` with mip 0's size, the clamped form clamped to the view's
  `[0, mips - 1]`. `AddWaveInputs` defines the simd lane input for these ops in a compute shader. Pixel shaders keep
  `air.dfdx`/`air.dfdy` and Metal's LOD. Also (ruling R-T6a, from Task 6's review): constant `Load`/`Sample*` offsets
  wrap to their low 4 bits as runtime ones do (`((c & 15) ^ 8) - 8`, the identity on [-8, 7]; gathers stay unwrapped).
  - Harness: `common.hlsli` gains `Depth` (t6: 8x8 `R32_TYPELESS` with `ALLOW_DEPTH_STENCIL`, 4 mips, mip m all
    0.125 + 0.25 m, each mip copied whole from an upload buffer, `R32_FLOAT` SRV), `Cmp` (s2: static point comparison,
    `LESS_EQUAL`, clamp) and `Lod64(lod) = round(lod * 64)`; descriptor slots are t0-t6 at 0-6 and u0-u4 at 7-11. The 13
    D3DMetal groups read `ok` with the new layout on both runtimes before any translator change.
  - Rows: `DXIL deriv matches its CPU reference` reads `match`, and `dxil-translate` accepts 21/22 behaviour shaders.
    `deriv.hlsl` (cs_6_6, `numthreads(64, 1, 1)`) takes the four derivatives of `v = gi * gi` (fine and coarse differ),
    and, with coordinates stepping 2^k / 8 per lane (k = quad % 5, LOD exactly k), `Depth`'s unclamped and clamped LOD,
    `Sample`, `SampleBias(-1)`, `SampleCmp(0.5)` and `Tex`'s unclamped LOD at `uv * (1, 2)` (the larger gradient:
    k + 1). `dxil-probe -S`: DXC keeps all ten calls (83-86; 81 three times, one clamped; 60, 61, 64).
    `DXIL offsets matches its CPU reference` gains slots 4 (`Load` at the constant offset (12, -13), texel (0, 7)) and
    5 (`int2(o.x, -13)`, a runtime x and a constant y); DXC at cs_6_7 keeps both constants as `i32 12, i32 -13`.
    - Before the patch: `dxil-translate` failed `deriv.dxil` (`kernel function main uses illegal functions
      (air.dfdx.f32, air.dfdy.f32)`; 20/22), its D3D12 pipeline failed `0x80004005`, and the offsets row read `FAIL`
      (slots 4 and 5, all 64 threads: `00000000` for texel (0, 7)'s `ff70e000`, the unwrapped offset out of bounds).
    - Single mutations, each on its own build, each failing only its own slot: fine derivatives reading the coarse
      lanes (slots 2 and 3, 32 threads each); the LOD taking the smaller gradient (slot 9, 64 threads; slots 4 and 5
      are isotropic and stay right); `SampleCmp`'s gradients zero (slot 8, the 36 threads with k >= 2); `SampleBias`
      dropping the shader's bias (slot 7, the 36 threads with k in 1..3).
    - A one-off D3DMetal run of `deriv` (not a row) agrees with the CPU reference on the fine derivatives, `Sample`
      and `SampleCmp` (slots 2, 3, 6, 8, all 64 threads). It differs elsewhere: coarse derivatives equal its fine ones,
      `CalculateLOD` reads 0, and `SampleBias` reads `Sample`'s mip.
    - Covered by code reading only: half-precision derivatives; implicit-LOD sampling of 1D, 3D, array and cube
      textures; a non-zero sampler `MipLODBias` (the static samplers have none); `CreateSampleCmpGrad`'s array and
      cube forms. `CalculateLOD` on a cube in a compute shader keeps Metal's LOD (`ponytail:` comment). 2D compute
      groups are Task 8's: their quads are Metal's lane quads until then.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`. A read-only `dxil-probe -S` over the 3,343
    compute shaders of `~/dxil-smite2` finds no call of ops 60, 61, 64, 81 or 83-86.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-7-dxmt-check.log`): `dxmt-check: all passed`, 265 rows `ok`
    (264 before, plus the deriv row), 0 `FAIL`; the 13 `matches D3DMetal` rows read `ok` with the new descriptor
    layout, so D3DMetal creates and fills the depth texture too. The x64 lane and `make test` were not run.
- **0047 (`airconv: Lay 2D compute quads out as D3D's 2x2 blocks.`):** Metal's kernel quads are four consecutive lanes of
  the group's flat thread index; a compute shader whose `numthreads` X and Y are both even has 2×2 quads of
  `SV_GroupThreadID` instead (`HLSL_SM_6_6_Derivatives.md`), so its derivatives, quad reads and votes took a row of four.
  When such a shader calls any of ops 60, 61, 64, 81, 83-86, 122, 123, 222 or 255, `InitializeDXIL` sets
  `DXILShader::quads_2d` (the struct is in `dxil_types.hpp`) and `AddComputeHandlers` always defines Metal's flat index
  `t` and the group position, and its prologue gives thread `t` the D3D IDs of spec §3.3 (`slice = X * Y`,
  `z = t / slice`, `r = t % slice`, `q = r >> 2`, `l = r & 3`, `x = 2 (q % (X / 2)) + (l & 1)`,
  `y = 2 (q / (X / 2)) + (l >> 1)`): `SV_GroupThreadID` is (x, y, z), `SV_GroupIndex` D3D's flattening of it,
  `SV_DispatchThreadID` `SV_GroupID * numthreads + (x, y, z)`. Metal's quad lane `l` (0046's derivatives read the simd
  lane) is now D3D's quad lane. 1D groups and shaders without those ops keep today's IDs and IR.
  - Rows: `DXIL deriv2d matches its CPU reference`, the same for `deriv2dwide` and `deriv2dodd`, read `match`, and
    `dxil-translate` accepts 24/25 behaviour shaders. `deriv2d.hlsl` (cs_6_6) has one body under three `numthreads`:
    (8, 8, 1) (`deriv2d`: rows of 8 fit a 32-lane simdgroup), (32, 2, 1) (`deriv2dwide`: they don't) and (6, 6, 1)
    (`deriv2dodd`: not a power of two; rows 36-63 stay 0). Row `SV_GroupIndex` holds the four derivatives of
    `v = x² + 3y² + xy` (different along both axes; coarse and fine differ), `QuadReadAcrossX/Y/Diagonal` and
    `QuadReadLaneAt(…, 3)` of the group index, `SV_GroupThreadID` packed, and `Tex`'s unclamped LOD with one k per quad
    (64 k). `dxil-probe -S`: DXC keeps 83-86 once each, 123 three times, 122 and 81 once, 95 and 96, and the three
    `numthreads`.
    - Before the patch: all three rows `FAIL` at thread 0 slot 1 (`got 40800000 want 40400000`: `ddy_coarse` read lane
      2 of a row, v(2, 0) - v(0, 0) = 4, for v(0, 1) - v(0, 0) = 3); mismatching threads per slot 0-9
      [48, 64, 0, 62, 0, 64, 64, 48, 0, 32] (8x8), [48, 64, 0, 64, 0, 64, 64, 48, 0, 32] (32x2) and
      [24, 36, 0, 36, 0, 36, 36, 24, 0, 18] (6x6). The 1D `quad` row stays `ok` against D3DMetal before and after.
    - Single mutations, each on its own build: the remap off (`quads_2d` false) fails the three rows as before the
      patch; `FlattenedThreadIdInGroup` returning Metal's `t` instead of D3D's index fails them at slot 5 (rows land on
      the wrong threads: slots 0-3 and 5-9 mismatch, 24 to 64 threads each). Every other row stays `ok`.
    - A one-off D3DMetal run of the three groups (not a row) agrees with the CPU reference on slots 2-8 (fine
      derivatives, the four quad reads, `SV_GroupThreadID`) in all three layouts: D3DMetal lays 2D quads out as 2×2
      blocks too. It differs on coarse derivatives (equal to its fine ones) and `CalculateLOD` (0), as Task 7 found.
    - Covered by code reading only: `SV_DispatchThreadID` and `SV_GroupID` past group 0 (the harness dispatches one
      group), Z > 1, and `QuadVote` (222) and `SampleCmpBias` (255) as triggers. In a remapped shader the wave lanes
      no longer follow `SV_GroupIndex` order (D3D defines no relation for 2D groups).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted; DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`. Measured first with a read-only
    `dxil-probe -S` scan of the 3,343 compute shaders of `~/dxil-smite2`: 1,783 have X and Y even, 10 call one of the
    ops (all `numthreads` 1D), none both; the two whose `numthreads` the scan could not read call none of them.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-8-dxmt-check.log`): `dxmt-check: all passed`, 313 rows `ok` after
    `info arm64 mode` (Task 7's log counts 310 the same way; plus the three new rows), 0 `FAIL`. The x64 lane and
    `make test` were not run.
- **0048 (`airconv: Translate ResourceDescriptorHeap and SamplerDescriptorHeap.`):** dynamic resources
  (`createHandleFromHeap`, op 218) are translated. `InitializeDXIL` gives each distinct `ResourceProperties` that
  `annotateHandle` puts on a `createHandleFromHeap` handle a range of its own (`Resource::heap`, `heap_props`): space
  `kHeapSpace` (`~0u`), lower bound 0, unbounded, IDs from `kHeapRangeBase` (`1 << 24`), class, kind, element type,
  stride and flags decoded from the properties (spec §2 item 5). `ResolveHandle` finds the range by the properties and
  returns 218's index (argument 1) as the register; `Lower` and `Cleanup` treat 218 as the other handle ops. The
  root-argument contract with Task 10: when the root signature's `Flags & 0xC00`, two qwords follow the last
  parameter's, the CBV/SRV/UAV heap's GPU base, then the sampler heap's; `setup_binding_rootsig` declares them (`heap`,
  `sampler_heap`) only in a shader with heap ranges, before `builder.Build`, and maps view-heap ranges to `heap` and
  sampler-heap ranges to `sampler_heap` at descriptor offset 0. `ConvertDXIL` refuses a heap-indexing shader with no
  root signature, or without 0x400 for views (checked first) or 0x800 for samplers, naming the flag; the DXIL
  geometry pipeline runs the same check (code reading only: no row has a heap-indexing VS or GS).
  `RootSignatureFlags(bytecode, length)` reads a blob's flags. `InitializeDXIL` also refuses a 64-bit groupshared
  atomic (`atomicrmw`/`cmpxchg` of i64 on addrspace(3)), naming it, until Task 14c.
  - The canary moves: `d3d12_dxil.exe`'s fourth shader is `dxil/reject/gatherraw.hlsl` (cs_6_7,
    `Texture2D<uint>.GatherRaw`, op 223), printed `canary hr=`; its root signature gains a table of t0 and a static
    point sampler at s0, so only the op is refused; every `printf` is flushed. `dxil-translate`'s root signature skips
    heap ranges and sets flags 0xC00, so offline translation reads heaps through the contract above (shaders without
    heap ranges gain no field). `heap.hlsl` is rewritten: each resource through the heaps (even slots) and through its
    table or root binding (odd slots), including a CBV at heap slot 12 and a sampler heap read; it runs in Task 10.
  - Rows: `an out-of-scope DXIL op fails only its pipeline` (`canary hr=0x80004001`), `the unsupported op is named in
    the log` (`Failed to compile cs shader: DXIL: dx.op.textureGatherRaw`), the capture rows still count four shaders
    and one root signature, and `newer device interfaces (5-8) answer as on D3DMetal` and `a graphics pipeline with
    sample count 0 fares as on D3DMetal` read `ok` (D3DMetal creates the canary's pipeline: `canary hr=0x00000000`);
    `D3D12 refuses a pipeline whose [WaveSize] excludes 32, or with a 64-bit groupshared atomic` (3);
    `heap indexing without its root signature flag is refused` (`group heap-noflag fail 0x80004001;group
    heap-noflag-sampler fail 0x80004001;`) and `and the log names the missing flag`
    (`flag CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;flag SAMPLER_HEAP_DIRECTLY_INDEXED;`); Lane E: `dxil-translate accepts every
    behaviour shader` (25/25), `dxil-translate refuses every out-of-scope shader` (0/4), `the canary is refused naming
    its op` and `a 64-bit groupshared atomic is refused naming it` (1 each).
  - `dxil-probe -S`: `heap.dxil` keeps 9 `createHandleFromHeap` calls (one non-uniform, one on the sampler heap) with 8
    distinct properties (raw SRV `{11, 0}`, Texture2D `{2, 1033}`, sampler `{14, 0}`, typed `{10, 1033}`, structured
    `{524, 20}`, RWBuffer<uint> `{4106, 261}`, RWByteAddressBuffer `{4107, 0}`, CBV `{13, 80}`); `gatherraw.dxil` keeps
    `dx.op.textureGatherRaw.i32` (223); `gsatomic64.dxil` keeps `atomicrmw add i64` on an addrspace(3) global.
  - Before the patch: `dxil-translate` over `dxmt/tests/dxil` read `fail heap.dxil DXIL: dx.op.createHandleFromHeap
    (dynamic resources) not supported`, `summary 24/25`; over `reject`, `fail gatherraw.dxil DXIL:
    dx.op.textureGatherRaw.i32 (223) not supported` (the same line after the patch) and `fail gsatomic64.dxil
    Compilation failed due to an interrupted connection: XPC_ERROR_CONNECTION_INTERRUPTED. This error occurred after
    multiple retries.` (this run took 0.2 s for the folder, not the spike's slow failure).
  - A one-off AIR dump of `heap.dxil` (a scratch build of `dxil-translate`, not a row): the root-argument struct has
    the tool's 9 table fields, then `heap` (field 9) and `sampler_heap` (field 10); the heap loads index descriptors 0,
    1, 2, 3, 7, 8 and 12 of field 9, and descriptor 1 of field 10.
  - Single mutations, each on its own build, against a cut-down check of this task's rows: the view-flag check off
    fails `and the log names the missing flag` (`flag SAMPLER_HEAP_DIRECTLY_INDEXED;` twice); the sampler-flag check
    off fails both noflag rows (`heap-noflag-sampler` creates its pipeline); `dxil-translate` without flags 0xC00 fails
    the 25/25 row (`fail heap.dxil DXIL: ResourceDescriptorHeap needs root signature flag
    CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED (0x400)`, 24/25). Every other row stays `ok`.
  - Covered by code reading only until Task 10 dispatches a heap: the qword order and descriptor offset 0 at run time,
    non-uniform heap indices as per-lane loads, and the CBV through the heap. Also by code reading: the decoded UAV
    counter, ROV, globally-coherent and comparison-sampler bits. The `cmpxchg` refusal has no row (only `atomicrmw add`
    has); a one-off scratch shader (`InterlockedCompareExchange` on a `groupshared uint64_t`, which DXC emits as
    `cmpxchg i64` on addrspace(3)) reads `DXIL: 64-bit groupshared atomic (cmpxchg) not supported`. The `exec-reject`
    row's `gsatomic64` line can't tell the named refusal from a Metal failure (both would be `E_FAIL`, by code
    reading); the Lane E row names it.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`, as predicted (no
    SMITE 2 shader indexes a heap or has a 64-bit groupshared atomic); DXBC `corpus-diff: 2/2 ok, 0 new failures, 0
    fixed, 0 changed, 2 unchanged`.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-9-dxmt-check.log`): `dxmt-check: all passed`, 317 rows `ok` after
    `info arm64 mode` (Task 8's 313, plus the two noflag rows and the two Lane E naming rows), 0 `FAIL`. The x64 lane and
    `make test` were not run (the milestone's full run is the controller's).
- **0049 (`d3d12: Pass the bound descriptor heaps to shaders that index them.`):** the runtime half of 0048's contract.
  `CalculateRootSignatureQwordOffsets` appends two qwords after the parameters' when the root signature's
  `Flags & 0xC00` and returns where (`MTLD3D12RootSignature::HeapQwordOffset`, `~0u` without); `UploadQwords` counts
  them, so `ExecuteIndirect`'s per-command copies of the staging carry them. The compute and graphics staging arrays
  grow to 66 qwords. `SetDescriptorHeaps` (a no-op before) records each heap's `GetGPUDescriptorHandleForHeapStart()`
  by its type (0 for a type not bound), writes both bases into each staging whose root signature has the qwords, and
  marks both root arguments dirty; `SetComputeRootSignature` and `SetGraphicsRootSignature` write the bases recorded so
  far. `Reset` forgets them. Runtime only: `src/airconv` is untouched, so no corpus run (the DXBC pair is unaffected
  too).
  - Rows: `DXIL heap matches its CPU reference` and `DXIL heap-swap matches its CPU reference` (`match`),
    `a pixel shader reads its texture and sampler through the descriptor heaps` (`yes`: `tri-heap`'s 12 pixels against
    D3DMetal's run of the same, `tri-heap-ref`), `and D3DMetal's heap sampler draws other pixels than the point one`
    (`differ`: `tri-heap-ref`'s hash against `tri-ref`'s, so the anchor can't drift to point sampling), and `and without
    the heap flags its pipeline is refused, naming the flag` (the logged run's
    `flag CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;triangle fail 0x80004001;`: the graphics path of 0048's check; the program's
    output goes to the log too, so one run gives both); `dxil-translate accepts the test shaders` 46/46 and
    `no translated shader assumes NaN or infinity away` 46:46 (`triangle2.psheap`).
  - `d3d12_dxil_exec.cpp`: when a group named `heap…` is asked for (D3DMetal's run asks for none), `root_heap` (`root`
    plus 3 root constants at b1, which `heap.hlsl` doesn't read: they round up to two qwords, so the heap qwords sit at
    4 and 5; flags 0xC00), a shader-visible sampler heap (`[0]` linear clamp, `[1]` point clamp), a CBV of the constant
    buffer at view-heap slot 12 (the heap now has 13 descriptors) and `heap2` (`heap`'s views, slot 0 a raw view of
    1024 zero words). `heap` binds the heaps, then the root signature, then the constants, and dispatches once;
    `heap-swap` dispatches the same, then (a UAV barrier between) binds `heap2`, points the table at it and dispatches
    again, printing the second dispatch. `expect.py`: each even slot 0-14 equals the next odd one; slot 11 also
    `((i + 1) & 63) * 7 + 1`, slot 1 `InWord(i)` and slot 3 `max(64 (i >> 3) - 32, 0)` from the CPU, so reads that went
    nowhere can't pass as equal zeros; `heap-swap` reads 0 in slots 0 and 1. `heap.hlsl`'s slots 2 and 3 now sample
    `Tex` linear (sampler heap `[0]`, and the static `Linear`) at texel corners, `uv = (i & 7, i >> 3) / 8`, and store
    `round(green * 510)`: rows `y - 1` and `y` blend to `64 y - 32`, which a point sampler (`64 y` or `64 (y - 1)`) can't
    give. `d3d12_triangle.exe <vs> <ps> [gs] [--heap]`: `--heap` adds flags 0xC00 and binds a shader-visible sampler
    heap of one linear clamp sampler beside the view heap. `triangle2.psheap` (ps_6_6): `psmain` with `Tex` and its
    sampler from the heaps; `dxil-probe -S` keeps two `createHandleFromHeap` (view index 0 `{2, 1033}`, sampler-heap
    index 0 `{14, 0}`) and no other resource; `heap.dxil` keeps its 9, the sampler-heap one now at index 0, two
    `sampleLevel` and two `Round_ne` (26). DXC rewrote no other `.dxil`.
  - Before the patch (cut-down check of these rows and their neighbours): `DXIL heap` `FAIL` at thread 0 slot 6 (`got
    00000000 want 00005a5a`), `heap-swap` at thread 0 slot 0 (`got c6ef3675 want 00000000`: the unwritten qwords read
    whatever followed the root arguments), `tri-heap` `FAIL` (every sampled pixel `ff2323..`); the refusal row already
    `ok` (0048's check).
  - Single mutations, each on its own build: the two bases swapped (by heap type) fails both heap rows and `tri-heap`;
    the view base one descriptor (32 bytes) late fails the same three; the root constants rounded down
    (`Num32BitValues / 2`, so the heap qwords start at 3, not 4) fails both heap rows (`tri-heap` stays `ok`: its root
    signature has no root constants). Every other row stays `ok`.
  - The sampler heap (ruling R-T10a): first, with point samplers through the heaps (`heap.hlsl` at texel centres,
    `--heap` point clamp, `tri-heap` against `tri-ours`), two more mutations passed every row: the sampler heap's base
    one descriptor late, and never written (0). On this GPU a sampler descriptor read from zeros acts as Metal's default
    sampler (nearest, clamp to edge), which samples as point clamp does, and linear and point agree at texel centres.
    The tests now sample linear where it differs from point (above); both mutations, each on its own build, fail `DXIL
    heap` and `heap-swap` (thread 8 slot 2: `got 00000040 want 00000020`, row 1 picked whole) and `tri-heap`; every
    other row stays `ok`. Ours and D3DMetal draw the 12 sampled pixels of `tri-heap` alike (6 of them other than
    `tri-ref`'s); their whole-image hashes differ (`d48d13f5a9af4b6c` against `f25c093a064b7c1d`), so the row compares
    pixels within 1/255, as the other triangle rows do. No address mode is tested through the heap (wrap would need a
    sampler the static ones don't match). By code reading only: the heap qwords through `ExecuteIndirect`.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-10-dxmt-check.log`): `dxmt-check: all passed`, 322 lines `ok`
    after `info arm64 mode` counted as Task 9 counted its 317 (276 rows, Task 9's 272 plus the four above, and 46
    `dxil-translate` lines, one more for `triangle2.psheap`), 0 `FAIL`. After R-T10a's test changes, on 0049 rebuilt
    (`task-10a-dxmt-check.log`, same folder): `dxmt-check: all passed`, 323 lines `ok` (277 rows: one more, D3DMetal's
    heap sampler row), 0 `FAIL`; the shader counts stay 46/46 and 46:46 (no new shader file). The x64 lane and
    `make test` were not run.
- **0050 (`airconv: Lower Shader Model 6.8's Barrier().`):** `BarrierByMemoryType` (244) and `BarrierByMemoryHandle`
  (245), which DXC emits only for the Shader Model 6.8 `Barrier()` intrinsic, become the op-80 mode they spell and take
  op 80's lowering (shared, not copied): `(sem & 1 ? 1 : 0) | (uav ? (sem & 4 ? 2 : 4) : 0) | (mem & 2 ? 8 : 0)`, with
  `uav = mem & 1` for 244 and true for 245 (its handle is a UAV); node memory bits are ignored.
  - `dxil/barrier68.hlsl` (cs_6_8): `barriers.hlsl` with each legacy barrier as its `Barrier()` form. `dxil-probe -S`
    keeps six calls and no op 80: 244 `(2, 2)`, `(2, 3)`, `(1, 4)`, 245 on `RWTyped`'s handle with `5`, 244 `(3, 4)`,
    `(3, 5)`, which are `barriers.hlsl`'s modes 8, 9, 2, 3, 10, 11. Its translated AIR is byte-identical to
    `barriers.dxil`'s (`air=fa82eb64cc634454` both), and its 1024 words equal `barriers`' word for word (a cut-down
    check). DXC rewrote no other `.dxil`.
  - Rows: `DXIL barrier68 matches its CPU reference` (`expect_barrier68`: slot 0 `in_word(63 - t)`, slot 1
    `((t + 1) & 63) * 3 + 1`, slot 2 `((t + 2) & 63) + 7 + ((t + 3) & 63) * 5`); Lane E `and Shader Model 6.8's Barrier()
    fences as the legacy barriers it spells` (`fence=2:1,5:3,5:3,7:3,7:3 barrier=3`, the `barriers` row's value);
    `dxil-translate accepts every behaviour shader` 26/26.
  - Before the patch: `fail barrier68.dxil DXIL: dx.op.barrierByMemoryType (244) not supported`, `summary 25/26`.
  - Single mutations of the mapping, each on its own build, against a cut-down check (the DXIL groups against D3DMetal
    and their CPU references, and Lane E's `dxil-translate` rows): device scope dropped (UAV always mode 4) fails the
    new fence row (`fence=2:1,5:1,5:1,7:1,7:1 barrier=3`); group sync dropped fails it (`fence=2:1,2:1,5:3,5:3,7:3,7:3
    barrier=0`) and the runtime row (thread 0 slot 0: `got 00000000 want efa6f28f`, the other simdgroup's groupshared
    word not yet written); group-shared memory dropped fails the fence row (`fence=5:3,5:3,5:3,5:3 barrier=3`). Every
    other row stays `ok`. The runtime row is a race without the sync: on the group-sync mutant it failed 7 of 10
    separate runs (and the cut-down check's one), and on the patch it matched 10 of 10. The fence row is the
    deterministic guard for all three; the scope and memory-type mutants change nothing a single group can see.
  - By code reading only: a UAV barrier at group scope (mode 4), a sync with no memory (mode 1), and node memory flags.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0048, as
    predicted (no SMITE 2 shader calls `Barrier()`); DXBC `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2
    unchanged`.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-11-dxmt-check.log`): `dxmt-check: all passed`, 325 lines `ok`
    after `info arm64 mode` (Task 10's 323 plus the two rows above; the 26/26 row changed in place), 0 `FAIL`. The x64
    lane and `make test` were not run.
- **0051 (`airconv: Lower SV_StartVertexLocation and SV_StartInstanceLocation.`):** `StartVertexLocation` (256) and
  `StartInstanceLocation` (257) read the values airconv already builds the vertex shader's IDs from,
  `base_vertex_id` and `base_instance_id`, cast to the call's type: `[[base_vertex]]` and `[[base_instance]]` in a vertex
  function, the draw's arguments in a geometry pipeline's object function (fields 2 and 3 of a draw, 3 and 4 of an
  indexed one). Any other stage refuses them by name.
  - Shaders: `shaders/indirect.hlsl`'s `vsvid` (vs_6_6: cells tagged `600 + SV_VertexID - VERT`, VERT being the vertex
    fetched) and `vsstart` (vs_6_8: `1000 + 100 * (SV_StartVertexLocation + 10) + SV_StartInstanceLocation`), and
    `triangle2.hlsl`'s `vsstart` (vs_6_8: `vsmain` moved off screen unless the two read 5 and 3). `dxil-probe -S` keeps
    `dx.op.startVertexLocation(256)` and `dx.op.startInstanceLocation(257)` in both vs_6_8 shaders. DXC rewrote no other
    `.dxil`.
  - `d3d12_indirect.exe <vs> <ps> <cs> [<vsid> <psid> [<vsvid> [<vsstart>]]]` draws its cell runs once per vertex shader
    given (lines `indirect <run>`, `indirect vid-<run>`, `indirect start-<run>`), each ending with a `direct` run: the
    drawn records as `DrawInstanced(3, 2, 6, 7)`, `DrawInstanced(6, 1, 9, 0)`, `DrawIndexedInstanced(6, 2, 3, -1, 7)` and
    `DrawIndexedInstanced(3, 1, 9, 2, 2)`. `d3d12_triangle.exe … --start <v> <i>` puts the corners at vertex `v` and draws
    `DrawInstanced(3, 1, v, i)`.
  - **The `SV_VertexID` convention**, as DXMT (ExecuteIndirect native and `DXMT_D3D12_INDIRECT=icb`, and direct) and
    D3DMetal both draw it, line for line:
    `vid-native-draw 2,0:594 3,0:591 4,0:591 2,1:594`, `vid-counted 2,0:594 2,1:594`,
    `vid-native-indexed 0,0:600 4,0:601 5,0:601 7,0:598 4,1:601 5,1:601` (and `vid-aliased-indexed` the same),
    `vid-vbv-gap` and `vid-vbv-null` `2,0:594 3,0:591 2,1:594`,
    `vid-direct 2,0:594 3,0:591 4,0:601 5,0:601 7,0:598 2,1:594 4,1:601 5,1:601`. So `SV_VertexID` counts from 0 on a
    non-indexed draw (`600 - start`: 594 for start 6, 591 for 9), and is the index without `BaseVertexLocation` on an
    indexed one (`600 - base`: 601 for -1, 598 for 2). DXMT computes it as `[[vertex_id]] - [[base_vertex]]`, so on
    non-indexed draws Metal's `[[base_vertex]]` is the draw's `vertexStart`, not 0.
  - Rows: Lane E's `indirect draws read from the argument buffer draw what D3D12 says` (ours, D3DMetal, icb) gains the
    `direct` line `direct 2,0:107 3,0:100 4,0:107 5,0:107 7,0:102 2,1:108 4,1:108 5,1:108`; `SV_VertexID under start and
    base vertex locations reads as on D3DMetal` (the `vid-` lines above); `SV_StartVertexLocation and
    SV_StartInstanceLocation read the draw's` and the same on `indirect-icb` (`start-native-draw 2,0:2607 3,0:2900
    4,0:2900 2,1:2607`, `start-counted 2,0:2607 2,1:2607`, `start-native-indexed` and `start-aliased-indexed 0,0:2000
    4,0:1907 5,0:1907 7,0:2202 4,1:1907 5,1:1907`, `start-direct 2,0:2607 3,0:2900 4,0:1907 5,0:1907 7,0:2202 2,1:2607
    4,1:1907 5,1:1907`: start vertex 6, 9, 0, -1 and 2, start instance 7, 0 and 2); Lane C's `a vertex shader reads
    start vertex 5 and start instance 3` and `and so does one in a geometry pipeline` (`tri-start` and `trigs-start`
    draw `tri-ours`'s and `trigs-ours`'s images exactly: hashes `a6cc70be915cf615`, `36d4e042e7f93e39`); shader counts
    `49/49`, `49:49` and the vertex/geometry flags row `18`. The stats runs keep the five original shaders and their rows
    read as before (`list.ExecuteIndirect calls 16390`, `ExecuteIndirect native 8195;indirect command buffers created
    1026;indirect resolve passes 3;`, `indirect resolve passes 14;`): the `direct` run issues no `ExecuteIndirect`.
  - **`ExtendedCommandInfoSupported` may be TRUE** (Task 15): every non-indexed `start-` cell reads the draw's
    `StartVertexLocation` (6 and 9) directly, through `ExecuteIndirect` natively and with `DXMT_D3D12_INDIRECT=icb`, and
    the geometry pipeline reads 5.
  - Before the patch: `fail indirect.vsstart.dxil DXIL: dx.op.startInstanceLocation.i32 (257) not supported` (257 comes
    first in the function), the same for `triangle2.vsstart`, `dxil-translate` `47/49`; both programs' vs_6_8 pipelines
    refused (`0x80004001`), so no `start-` line and `triangle fail` for `tri-start` and `trigs-start`. The `vid-` and
    `direct` rows already read `ok`: `SV_VertexID` was right before the patch.
  - Single mutations, each on its own build, against a cut-down check (Lane C's triangle rows, Lane E's indirect rows
    with the stats runs, the `dxil-translate` counts; 21 rows): 256 reading 0 fails both `start-` rows (`start-native-draw
    2,0:2007 3,0:2000 …`) and both Lane C rows; 257 reading 0 fails the same four (`start-native-draw 2,0:2600 …`); the
    object function's 256 and 257 reading 0 while its vertex fetch still uses the draw's arguments fails only `and so
    does one in a geometry pipeline`. Every other row stays `ok`.
  - Limits: `triangle2.vsstart` can't tell the two values apart (start vertex 3 and instance 5 also move it by 0); the
    `start-` rows can (a swap reads 2706, not 2607). By code reading only: an indexed draw in a geometry pipeline (fields
    3 and 4 of its arguments).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0050, as
    predicted (no SMITE 2 shader is Shader Model 6.8); DXBC `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2
    unchanged`.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-12-dxmt-check.log`): `dxmt-check: all passed`, 333 lines `ok`
    after `info arm64 mode` (Task 11's 325 plus the five rows above and three `dxil-translate` lines for the new
    shaders; the `direct` line and the count rows changed in place), 0 `FAIL`. The x64 lane and `make test` were not
    run.
- **0052 (`airconv: Lower SampleCmpBias and SampleCmpGrad.`):** `SampleCmpBias` (255) is `SampleCmp` with `SampleBias`'s
  bias (the shader's plus the sampler's `MipLODBias`) and min-LOD clamp: `air.sample_compare_*` with that bias in a pixel
  shader, and in a compute shader 0046's comparison-gradient builder with the quad's coarse gradients scaled by 2^bias.
  `SampleCmpGrad` (254) is `air.sample_compare_<depth kind>_grad` with its gradients scaled by 2^(sampler bias) and the
  clamp, as `SampleGrad`; cubes take no offset (the builder drops it). Both mark their texture sampled and compared in
  `ScanUsage`, so it binds as a depth texture, and a compute shader calling 255 gets the SIMD lane inputs as 60, 61 and
  64 do. `CalculateLOD` (81) with a comparison sampler needed nothing new. `SampleGrad` now shares its gradient vectors
  with 254 and emits the same IR: every test shader's `air=` but the two new ones is unchanged.
  - Shaders: `dxil/cmp68.hlsl` (cs_6_8, nine slots on `Depth` with the point comparison sampler `Cmp`, deriv's quads:
    LOD k = q % 5) and `shaders/cmp68.hlsl` (ps_6_8 `psmain`: `SampleCmpBias(C, uv, 0.5, 1) + SampleCmpGrad(C, uv, 0.5,
    ddx(uv), ddy(uv))`, Metal's own LOD path, translated offline only). `dxil-probe -S` keeps three `sampleCmpGrad (254)`,
    two `sampleCmpBias (255)`, two `calculateLOD (81)` on `Cmp`, `sampleCmp (64)` and `sampleCmpLevelZero (65)` in the
    compute shader (coordinates and gradients SSA, the offset form's `i32 1, i32 0`, clamps `undef`), and one 254 and one
    255 in the pixel shader. DXC rewrote no other `.dxil`.
  - Rows: `DXIL cmp68 matches its CPU reference` (`expect_cmp68`, 1.0 or 0.0 as f32 bits where LESS_EQUAL passes on mip
    m's 0.125 + 0.25 m: `SampleCmpGrad` 0.5 at LOD k, `SampleCmpBias` 0.5 at k - 1 and k + 1, `CalculateLevelOfDetail`
    and `…Unclamped` on `Cmp` as `64 min(k, 3)` and `64 k`, `SampleCmp` 0.5, `SampleCmpLevelZero` 0.1, `SampleCmpGrad`
    0.7 with doubled gradients at LOD k + 1, and the offset form as slot 0); `dxil-translate accepts every behaviour
    shader` 27/27; `dxil-translate accepts the test shaders` 50/50; `no translated shader assumes NaN or infinity away`
    50:50.
  - Before the patch: `fail cmp68.dxil DXIL: dx.op.sampleCmpGrad.f32 (254) not supported` (26/27), `fail cmp68.ps.dxil
    DXIL: dx.op.sampleCmpBias.f32 (255) not supported` (49/50, 49:49), and D3D12 `group cmp68 fail 0x80004001`.
  - Single mutations, each on its own build, against a cut-down check (the 15 Shader Model 6.1-6.8 groups against their
    CPU references and the four `dxil-translate` count rows), cmp68's mismatching threads per slot: compute
    `SampleCmpBias` dropping the shader's bias fails slots 1 and 2 (12 threads each); `SampleCmpGrad` taking the quad's
    coarse gradients instead of its own fails slot 7 (12); `SampleCmpGrad` comparing against 0.5 instead of its value
    fails slot 7 (12); compute `SampleCmpBias` comparing against its bias operand fails slots 1 and 2 (40, 48);
    `ScanUsage` not marking 254/255's texture compared leaves `cmp68` passing (its `SampleCmp` marks `Depth`) but
    `dxil-translate` died on `cmp68.ps.dxil` (before the fix round below: a non-depth binding reaches the comparison
    builder, which returns no texel), failing the three shader-count rows. Every other row stays `ok`.
  - Limits: the static comparison sampler is point-filtered with `MipLODBias` 0, so the 2^(sampler bias) folds and a
    linear comparison filter are by code reading only, as are the clamp operand, the 2D array and cube forms and the
    pixel-shader lowering (offline translation only: OPTIONS21's `SampleCmpGradientAndBiasSupported` rests on the
    compute rows). Slot 8's offset can't move a sample of a uniform mip: it shows the offset form translates and runs.
    Comparison `CalculateLOD` in a compute shader is 0046's ALU LOD, which reads no sampler: it ignores the sampler's
    bias and MinLOD/MaxLOD whatever the sampler, and a cube's reads 0, Metal's kernel LOD (hardening pass).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0051, as
    predicted (no SMITE 2 shader is Shader Model 6.8); DXBC `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2
    unchanged`.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-13-dxmt-check.log`): `dxmt-check: all passed`, 335 lines `ok`
    after `info arm64 mode` (Task 12's 333 plus the `cmp68` row and the `dxil-probe` line for `cmp68.ps.dxil`; the three
    count rows changed in place), 0 `FAIL`. The x64 lane and `make test` were not run.
  - **Fix round (review, ruling R-T13a): a 1D comparison refused by name, not a crash.** airconv binds a `Texture1D` or
    `Texture1DArray` as a 2D colour texture whether or not it is compared (a depth kind is never chosen for 1D), so the
    comparison builders return no texel for it, and the shared `result` dereferenced that null: `dxil-translate`
    exited 139 with no output on a ps_6_8 `Texture1D<float>.SampleCmpBias` or `.SampleCmpGrad` (refused by name before
    0052) and on a ps_6_0 `.SampleCmp` (a crash older than this series). DXC accepts all three. `result` now refuses a
    call with no texel by name (`Unsupported`), which covers 64, 65, 74, 254 and 255 and the compute gradient path; it
    is dead code for every comparison that binds as depth (every test shader's `air=` unchanged). Mapping a compared 1D
    texture to `depth_2d` is left out: it would change the D3D11 path too. The amended 0052 carries it.
    - Shaders: `dxil/reject/cmp1d.hlsl`'s `psbias` (ps_6_8, 255), `psgrad` (ps_6_8, 254) and `pscmp` (ps_6_0, 64), as
      `cmp1dbias.dxil`, `cmp1dgrad.dxil`, `cmp1d.dxil`; `dxil-probe -S` keeps each op.
    - Rows: `a Texture1D SampleCmpBias is refused naming its op`, the same for `SampleCmpGrad` and `SampleCmp`
      (`fail cmp1dbias.dxil DXIL: dx.op.sampleCmpBias.f32 (255) not supported`, `… sampleCmpGrad.f32 (254) …`,
      `… sampleCmp.f32 (64) …`); `dxil-translate refuses every out-of-scope shader` `0/4` → `0/7`. The D3D12 reject row
      (`exec-reject`, 3) is unchanged: the new shaders are pixel shaders, which `d3d12_dxil_exec` doesn't run.
    - Before the guard: `dxil-translate` over `dxil/reject` exits 139 with no line (each new shader alone too), so the
      count row reads nothing and the three named rows read 0.
    - Corpus after the amend: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged`
      against 0051; DXBC `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`.
    - Gates (R-LITE, ARM64EC lane; `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-13-fix1-dxmt-check.log`):
      `dxmt-check: all passed`, 338 lines `ok` after `info arm64 mode` (the 335 above plus the three named rows; the
      `0/7` row changed in place), 0 `FAIL`. The x64 lane and `make test` were not run.
- **0053 (`airconv, d3d12: Float atomics on R32_FLOAT typed UAVs through an R32Uint view.`):** Shader Model 6.6's float
  `InterlockedExchange`, `InterlockedCompareStoreFloatBitwise` and `InterlockedCompareExchangeFloatBitwise` reach DXIL
  as i32 atomics on the float's bits, on every resource that may hold them. Raw, structured and groupshared locations
  already took the integer paths and are unchanged: for groupshared, DXC 1.9.2607 emits `atomicrmw xchg i32` and
  `cmpxchg i32` on a bitcast `float addrspace(3)*`, never a float `atomicrmw`, so the translator has nothing to bitcast
  and no code was added for it. A typed `RWBuffer<float>` or `RWTexture*<float>` was refused (Metal's texture atomics
  take integer textures only). Now a typed UAV (a texture or typed buffer) with element type F32 that op 78 or 79
  reaches is `UnorderedAccessViewInfo::atomic_view` (`ScanUsage` marks it `atomic`): `FillResourceMaps` declares it
  `uint`, `GetUAVTexture` reads its handle from the descriptor's third qword (`GetTextureDescriptor`'s new field-index
  argument, 2 here, 0 elsewhere: same IR), its loads and stores bitcast through `Cast`, its atomics take the integer
  path. The DXBC path never sets it. Runtime: `SRVTextureGPUStorage` (which `UAVTextureGPUStorage` aliases) and
  `UAVTexelBufferGPUStorage` name their third qword `atomic_resource_id` (`padding[0]` before); the texel-buffer
  descriptor's fourth qword stays `padding`, reserved for Task 14b. `CreateUnorderedAccessView` on a texture whose view
  format is `R32Float` also creates the same view as `R32Uint` (every D3D12 texture has `PixelFormatView` usage), and
  on a typed buffer of `R32Float` an `R32Uint` buffer view (which gets `ShaderAtomic` usage); both texture and
  texel-buffer `AddUnorderedAccessView` take that view as an explicit `std::optional` and always write its GPU resource
  ID, or 0, into the third qword, and the two SRV writers write 0 there, so a reused slot never keeps a stale ID.
  `CopyDescriptors` copies all 32 bytes. One `R32Uint` view per `R32_FLOAT` UAV, used or not (a `ponytail:` comment
  names making it on a shader's first atomic use instead). Float atomics have no cap bit: Shader Model 6.6 requires
  them (spec §4).
  - Shader: `dxil/floatatomics.hlsl` (cs_6_6): on a raw (`Out`'s word 15 of each thread's row), a structured
    (`AtomicSF`), a groupshared (`G`), a typed-buffer (`RWTypedF`, `R32_FLOAT`) and a typed-texture (`AtomicTexF`,
    `R32_FLOAT`) location each thread owns, all starting at +0.0: compare-store against -0.0 (bitwise no match), exchange
    0 -> a, compare-store against b (no match), compare-exchange a -> b, compare-store b -> c, exchange c -> 0; slots
    3k..3k+2 hold the three exchanges' returns 0, a, c (a = i * 0.5 + 0.25, c = i * 4 + 1). Two inputs differ from the
    plan's shader, the expected words unchanged: the leading -0.0 compare-store, and b a quiet NaN with payload i + 1
    (the plan had b = -i - 0.75), so a float == compare would store c at the first step (slot 3k = c) and miss at the
    NaN one (slot 3k+2 = b). `dxil-probe -S` keeps 16 `atomicCompareExchange.i32` (79) and 8 `atomicBinOp.i32` (78, op 8)
    on the four UAVs (compare `i32 -2147483648` for -0.0) and four `cmpxchg i32` and two `atomicrmw xchg i32` on
    `addrspace(3)`. DXC rewrote no other `.dxil`.
  - `common.hlsli` and `d3d12_dxil_exec.cpp`: `AtomicSF` (u5, `AtomicS`'s buffer as 64 floats, stride 4), `RWTypedF`
    (u6, `RWTyped`'s buffer as 64 `R32_FLOAT` elements) and `AtomicTexF` (u7, a new 8x8 `R32_FLOAT` texture, zeroed
    before each group as `AtomicTex` is), in a third table range (3 UAVs from register 5 at offset 13; `root_heap`
    shares it) at view-heap slots 13-15, the heap (and `heap2`) growing to 16. D3DMetal's run creates them too: its 13
    groups read nothing that moved, and every `matches D3DMetal` row stayed `ok` with the new harness on the old
    translator.
  - Rows: `DXIL floatatomics matches its CPU reference` (`expect_floatatomics`: f32 0, a, c in each of slots 0, 3, 6, 9
    and 12, slot 15 0) and `dxil-translate accepts every behaviour shader` 28/28. Anchor (not a row): D3DMetal's run of
    `floatatomics` matches the same reference, -0.0 and NaN steps included.
  - Before the patch: `fail floatatomics.dxil DXIL: dx.op.atomicCompareExchange.i32 (79) not supported` (27/28; the
    leading compare-store is the first op), D3D12 `group floatatomics fail 0x80004001`.
  - Single mutations, each on its own build, against a cut-down check (the 13 D3DMetal comparison groups and `mad`,
    the 16 Shader Model 6.1-6.8 groups against their CPU references, the `dxil-translate` count; 31 rows),
    `floatatomics`'s mismatching threads per slot: `FillResourceMaps` never setting `atomic_view` fails the pipeline
    (`0x80004001`) and the count (27/28); the texture descriptor's third qword always 0 fails slots 13 and 14 (64 each:
    a null texture returns 0); the texel-buffer descriptor's always 0 fails slots 10 and 11 (64 each); a synthetic
    rewrite of each groupshared `cmpxchg` into a float == compare (in the converter's module load: the path is Metal's
    own, so there is no translator code to break) fails slots 6 (c: the -0.0 step stored) and 8 (`7fc00002` for thread
    1: the NaN step missed), 64 each. `GetUAVTexture` reading the first qword (the `R32Float` view) for an
    `atomic_view` passes: this GPU performs the atomics on an `R32Float` texture reached through a `uint` texture handle,
    which Metal doesn't define (its texture atomics are for integer formats); the `R32Uint` view is the defined path,
    and the two third-qword mutations show it is the one read. Under `MTL_DEBUG_LAYER=1` and `MTL_SHADER_VALIDATION=1`
    neither the patch nor that mutation logs a validation error. Every other row stays `ok` in every mutation.
  - Limits: plain loads and stores on an `atomic_view` UAV (the `Cast` bitcasts through the `R32Uint` view) are by code
    reading only: no slot exercises them. `atomic_view` is per resource range: in a bound array of `RWTexture*<float>`
    or `RWBuffer<float>`, or the one heap range all dynamic `RWTexture2D<float>` share (one ResourceProperties), an
    atomic on any element sends every element through the third qword, so an element whose view isn't `R32_FLOAT`
    (`R16_FLOAT`, `R8_UNORM`) reads 0 and drops writes, where D3D12 would serve it (hardening pass). 1D, 2D array and
    3D `R32_FLOAT` UAVs take the same path by code reading.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0052, as
    predicted (no SMITE 2 shader has a float atomic on a typed UAV: none was refused); DXBC `corpus-diff: 2/2 ok, 0 new
    failures, 0 fixed, 0 changed, 2 unchanged`. Every test shader's `air=` in `dxmt/tests/dxil` and `dxmt/tests/shaders`
    but `floatatomics.dxil`'s is unchanged.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-14-dxmt-check.log`): `dxmt-check: all passed`, 339 lines `ok`
    after `info arm64 mode` (Task 13's 338 plus the `floatatomics` row; the 28/28 row changed in place), 0 `FAIL`; all
    13 `DXIL <group> matches D3DMetal` rows `ok` with D3DMetal running the grown harness. The x64 lane and `make test`
    were not run.
- **0054 (`airconv, d3d12: 64-bit buffer atomics through a lock table.`):** Shader Model 6.6's 64-bit atomics (ops 78
  and 79 with the i64 overload) on raw, structured, root-descriptor and heap-indexed buffers (ruling R-A64; spec 3.8;
  mechanism and measurements from the i64 spike, `.superpowers/brainstorm-sm/i64-spike-result.json`). Metal's own 64-bit
  atomics are never emitted: the translator wraps each operation in a u32 lock from a per-device table and reads and
  writes the location's two halves as relaxed 32-bit atomics between `air.atomic.fence(1, 5, 2)` calls. Translator:
  `kAtomic64Locks = 1 << 16` (`airconv_public.h`); `InitializeDXIL` sets `ShaderInfo::use_atomic64_locks` for an i64 op
  78 or 79, and `setup_binding_rootsig` then declares the root-argument field `atomic64_locks` (a device `uint *`) last:
  after the heap fields when the root signature has flags 0xC00 (declared then even in a shader that indexes no heap),
  else after the last parameter's. `BindingMap::atomic64_locks` is its index and `GetAtomic64Locks` loads it. Each stage
  of a geometry pipeline binds with its own `ShaderInfo`, so the two stages' flags need no OR (unlike the helper links,
  which read the geometry shader's `ShaderInfo` for a module holding vertex code too). `Lowering::UnderLock` is the
  spike's hybrid: one `cmpxchg i32` per lane, the winners run the critical section inside that branch; then, while
  `air.simd_any(pending)`, the first pending lane (`air.simd_is_first`, reached by pending lanes only) spins, runs it
  and releases (a relaxed store of 0 after the fence); no spin cap. `Lowering::Atomic64` takes the element pointer as
  the i32 path forms it and the lock `((a >> 3) ^ (a >> 19)) & 0xFFFF` of its 32-bit address `a`, computes the new value
  with LLVM's `buildAtomicRMWValue` (signed compares for IMin and IMax), and stores CompareExchange's and CompareStore's
  only on a 64-bit match. A pixel shader's helper lanes (`air.simd_is_helper_thread`, whatever
  `[WaveOpsIncludeHelperLanes]` says) and an out-of-bounds element skip the path and read 0. Typed buffers and textures
  stay refused by name until 0055. Runtime: `MTLD3D12Device::Atomic64Locks()` creates the 256 KiB table on first use
  (shared storage, zeroed, `RegisterResidencyAndVA` as the null buffers; the device's residency set covers every queue)
  and `ResetAtomic64Locks()` zeroes it again after a command buffer's `Device error`; every root signature has one more
  trailing qword, `MTLD3D12RootSignature::LockQwordOffset`, which `SetComputeRootSignature` and
  `SetGraphicsRootSignature` always write (staging arrays 66 → 67 qwords).
  - The atomic `load`/`store` form, checked first (Step 1): the i32 raw-buffer RMW temporarily emitted as `load atomic
    i32 … monotonic, align 4`, `air.atomic.fence(1, 5, 2)` and `store atomic i32 … monotonic, align 4` built a Metal
    pipeline (`ok atomics.dxil`, its `air=` changed from `762afff838f50a63` to `1288625436a164ed` and back once
    reverted), so the lock path uses those forms, not the `air.atomic.global.xchg`/`or 0` fallback.
  - Rows (Lane C, last, in a run of their own: a deadlock costs only its own 120 s watchdog): `DXIL atomic64 matches its
    CPU reference` (`dxil/atomic64.hlsl`, one group of 64 threads under `root_a64`: every op kind on a thread-owned
    raw and structured qword, `expect.py`'s `own64`; 64 adds of `V = 0x100000001` on one qword through four views (raw,
    structured, the root UAV, `ResourceDescriptorHeap[16]`) whose olds are `{0..63} * V`; a UMax whose olds pass the
    linearizability multiset check; a CAS-counter permutation of `{0..63}`; an out-of-bounds add reading 0; the finals
    by plain loads after a barrier); `DXIL atomic64c matches its CPU reference` and `… atomic64c-rootheap …`
    (`dxil/atomic64c.hlsl`, 4,096 threads: a storm of adds on one qword ending at `4096 * V` and 1,024 qwords with four
    adds each, every old's `k` marked once in a bitmap); `a pixel shader's 64-bit adds count each covered pixel once`
    and `and a vertex shader's 64-bit max keeps the largest vertex's value` (`d3d12_triangle.exe --atomic64` with
    `triangle2.psatomic64`/`vsatomic64`: `atomic64 pixels 3475 sum 3475 max 00000001fffffff2`). Lane E: `the 64-bit
    buffer lock path fences device memory at device scope` (`fences atomic64c.dxil` = `1:2 `). The lock qword sits at
    qword 7 under `root_a64` (CBV 0, table 1, constants 2-3, root UAV 4, heap bases 5-6; `atomic64` and
    `atomic64c-rootheap`), at qword 2 under the plain root signature (`atomic64c`) and at qword 3 in `tri-atomic64` (no
    heap flags). Counts: 30/30 behaviour shaders, 52/52 and `52:52` test shaders, 19 vertex and geometry shaders.
  - Before the patch: `fail atomic64.dxil DXIL: dx.op.atomicBinOp.i64 (78) not supported`, the same for `atomic64c`,
    `triangle2.vsatomic64` and `triangle2.psatomic64` (28/30, 50/52); in the gate exactly the ten predicted rows failed
    (`atomic64 fail 0x80004001` for the three groups, no `atomic64` line for the triangle, the empty fence row and the
    four counts), and all 13 `DXIL <group> matches D3DMetal` rows stayed `ok` with D3DMetal running the grown harness
    (the 16 KiB `a64` buffer and its three views at heap slots 16-18).
  - Single mutations, each on its own build and a full ARM64EC-lane check: the naive loop in `UnderLock` (spin until
    acquired, then the section) fails all five runtime rows: the triangle's command buffer ends in Metal's `Caused GPU
    Hang Error` (`atomic64 pixels 4096 sum 0 max 0000000000000000`: the macOS GPU watchdog fired here, which the spike
    never saw for its spin shapes), `atomic64` prints after a GPU hang with its four-view olds wrong (the runtime's
    re-zero ran on that `Device error`), and `atomic64c` and `atomic64c-rootheap` are `missing` (`GPU work never
    finished`); no `d3d12_dxil_exec` survived. Removing both fences fails the fence row only (`got [- ]`): `atomic64c`
    and `atomic64` still match, so the runtime groups give no fence evidence on this GPU and the Lane E row is the
    proof (the spike measured 397 to 1,600 bad of 1,600 dispatches without fences, in another loop shape). Removing the
    helper-lane guard passes every row (`psatomic64`'s `air=` did change, `42d4bc864c82b36f` → `1223c24d557baa6b`):
    helper lanes' device atomics were inert on this GPU. The guard stays: D3D gives helper lanes no UAV side effects
    and Metal promises nothing either way.
  - Limits (spec 3.8, "Not supported"): a 32-bit atomic or a plain store on either half of a qword while a 64-bit
    atomic runs on it is not atomic with respect to it; a plain 64-bit load may see one half updated; two devices
    sharing memory have separate tables. A thread storm on one address is slow (the spike: ~600x a native 32-bit
    atomic). The re-zero after a fault has no row (a GPU fault can't be forced safely), though the naive mutation's
    hang exercised it. Nothing ran on an M1 (Apple7): its scheduling, which forward progress rests on, is unmeasured.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0053, as
    predicted (no SMITE 2 shader has a 64-bit atomic, and a shader without one declares no lock field); DXBC
    `corpus-diff: 2/2 ok, 0 new failures, 0 fixed, 0 changed, 2 unchanged`. Every earlier test shader's `air=` in
    `dxmt/tests/dxil` and `dxmt/tests/shaders` is unchanged.
  - Gates (one lane, ruling R-LITE): `dxmt/check.sh` in the ARM64EC lane after the commit and export
    (`.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-14a-dxmt-check.log`): `dxmt-check: all passed`, 347 lines `ok`
    after `info arm64 mode` (Task 14's 339, the six new rows and the two new shaders' `dxil-probe` lines; the four
    count rows changed in place), 0 `FAIL`. The x64 lane and `make test` were not run.
- **0055 (`airconv, d3d12: 64-bit atomics on typed buffers and R32G32_UINT textures.`):** Shader Model 6.6's 64-bit
  atomics on `RWBuffer<uint64_t>`/`<int64_t>` and `RWTexture*<uint64_t>`/`<int64_t>` (`R32G32_UINT`), through 0054's
  lock table (ruling R-A64; spec 3.8).
  - Typed buffers take the raw path. The texel-buffer UAV descriptor's fourth qword, `buffer_address` (`padding` since
    0053), holds `UAVBuffer->current()->gpuAddress()`. The element sits at `base + 8 * (index + first element)` and is
    skipped, reading 0, from the view's element count. So a raw and a typed view of one qword take one lock, and no
    `RG32Uint` texture buffer is accessed.
  - `UnorderedAccessViewInfo::address64` marks a typed UAV that an i64 op 78/79 reaches. It goes by use because DXC 1.9
    declares `RWBuffer<uint64_t>` U32 with an Atomic64Use tag (`!{i32 0, i32 5, i32 3, i32 1}`), which heap handles
    lack. `GetUAVTexture` then loads the qword as `TextureDescirptor::BufferAddress`.
  - Textures are keyed by `(x, y)` alone: `h = x * 0x9E3779B1 + y; h ^= h >> 16`. They are read and written through the
    `<4 x i32>` texel under `air.atomic.fence(5, 5, 2)`, each write followed by `air.fence_texture`.
  - Rows:
    - `DXIL atomic64typed matches its CPU reference` (`dxil/atomic64typed.hlsl`): every op kind on a typed element and
      a texel; 64 adds of `V` through a raw and a typed view of one qword, whose olds are `{0..63} * V`; four texels'
      Nanite-shaped maxima, linearizable; a texel CAS counter, `{0..63}`; an out-of-view typed add reading 0, which
      didn't land.
    - `atomic64c` and `-rootheap` gain a 4,096-thread storm on a typed element and on a texel (words 128-639 all
      ones).
    - The Lane E fence row now reads `1:2 5:2 `.
    - Count: 31/31 behaviour shaders.
  - Before the patch: the two shaders failed `dx.op.atomicBinOp.i64 (78) not supported` (29/31). In the gate, exactly
    five rows failed, and all D3DMetal rows stayed `ok` with the grown harness (slots 19-22, heap 23).
  - Single mutations:
    - The typed key from the view-relative index fails `atomic64typed` at slots 8-9.
    - Device-only texture fences fail only the fence row: `atomic64typed` (one threadgroup) and the texel storm (no
      re-reads) don't see stale texture reads on this GPU. Accepted (ruling R-T14b1): the Lane E fence row is the guard, as for 0054's fences; hardening item 23 tries a 64-group texel CAS counter that re-reads across cores.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0054; DXBC
    `2/2 ok, … 0 changed, 2 unchanged`. Every earlier test shader's `air=` is unchanged.
  - Gates (R-LITE, ARM64EC lane), one run with 0056 on top (ruling R-PAL1;
    `.superpowers/sdd/2026-10-10-dxmt-dxil-sm68/task-14b-0056-dxmt-check.log`): `dxmt-check: all passed`, 348 lines `ok`
    after `info arm64 mode`, 0 `FAIL`. The x64 lane and `make test` were not run.

- **0056 (`airconv: Drop lifetime markers on hoisted constant arrays.`):** Palworld crashed on New Game (and, once its
  saves were in the prefix, on loading a world): two `vs_6_6` vertex shaders, and a compute shader seen later, failed
  with `DXIL: a static const array used other than by loads`. DXC hoists a local array that only ever holds constants
  to a global (`<name>.hca`, e.g. `NeighborOffset.i.i.i.i.0.hca`) and, from Shader Model 6.6, keeps the array's
  `llvm.lifetime.start/end` markers on it through a constant bitcast. `MoveConstantsToConstantSpace`'s `Retarget` now
  drops those markers (and the bitcast that only fed them) instead of refusing the shader. Outside the plan's tasks
  (ruling R-PAL1): Task 14c became 0057 and Task 15 0058.
  - Row: `dxil/buffers.hlsl` slot 15 reads a local `int2` array of constants in a `[loop]`; DXC 1.9 (`cs_6_6`) gives
    `kNeighbor.i.0.hca` and `.1.hca` with lifetime markers, as Palworld's shaders have. Before the patch,
    `dxil-translate` failed it offline (`fail buffers.dxil DXIL: a static const array used other than by loads`); after
    it, `DXIL buffers matches D3DMetal` holds with the new slot.
  - Palworld's captured shaders (`~/dxil-palworld`, never committed): 189/191 → 191/191 offline.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0055; DXBC
    `2/2 ok, 0 changed`. Fresh fetch: `applied 56/56`, tree-equal.
  - Gate: the run above (348 `ok`, 0 `FAIL`).

- **0057 (`airconv: 64-bit groupshared atomics through a threadgroup lock array.`):** Shader Model 6.6's 64-bit atomics on
  groupshared memory (LLVM `atomicrmw`/`cmpxchg` of i64 on addrspace(3), no dx.op), which Task 9 refused by name.
  - A shader with one gets an `[N x i32] addrspace(3)` lock array (`gs_locks`): N = 256 (1 KiB), or the largest power
    of two whose `N * 4` fits beside the shader's own groupshared memory in 32768 bytes. When not even one lock fits,
    `InitializeDXIL` refuses: `DXIL: 64-bit groupshared atomics need 4 bytes of threadgroup memory for a lock; the shader
    uses <bytes> of 32768`. An `atomicrmw` other than xchg, add, and, or, xor, max, min, umax, umin is refused by name.
  - Thread i (its flattened index, defined for such a shader even when no dx.op reads it) zeroes slots i, i + group, …
    at the body's start; then one threadgroup barrier.
  - Each atomic takes lock `(trunc32(ptrtoint(ptr)) >> 3) & (N - 1)` through 0054's `UnderLock` with threadgroup fences
    at threadgroup scope (`2:1`), reads and writes the qword with plain loads and stores, and a compare-exchange stores
    only on a match. Lock forms: `cmpxchg i32` (relaxed) and `store atomic i32` (monotonic) on the addrspace(3) lock
    build a pipeline, so the `air.atomic.local.*` fallback is unused.
  - Rows: `DXIL gsatomic64 matches its CPU reference` (every op kind on a thread-owned qword, a carry-crossing sum, a
    UMax, a CAS counter, finals by plain loads) and `DXIL gsatomic64c …` (1,024 threads on two qwords, the olds as
    bitmaps); `reject/gsatomic64full` (32768 bytes of groupshared) refused by name offline and its pipeline
    `0x80004005` in D3D12; the fence-and-barrier row `2:1 barrier=3`; 33/33 behaviour shaders, reject 0/7.
  - Before the patch: both shaders failed Task 9's refusal (31/33); gsatomic64full failed with Task 9's text.
  - Single mutations (offline, deterministic): no zeroing barrier gives `2:1 barrier=2`; no threadgroup fences gives
    `- barrier=3`. The naive-loop mutation was not run (ruling R-T14c1: the same `UnderLock` 0054 mutated).
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0056; DXBC
    `2/2 ok, 0 changed`; Palworld's capture 191/191. Fresh fetch: `applied 57/57`, tree-equal.
  - Gates (R-LITE, ARM64EC lane): the first run (`task-14c-dxmt-check.log`) had every 14c row `ok` and one known flake
    (`XeSS reports no device for an object without DXMT's interface`); the rerun on the final build
    (`task-14c-dxmt-check-rerun.log`): `dxmt-check: all passed`, 351 lines `ok`, 0 `FAIL`.

- **0058 (`airconv, d3d12: Skip a null descriptor heap; refuse malformed WaveSize and f64 wave ops.`):** the first part of
  the hardening pass (ruling R-HARD; H1 items 4, 10, 20 and 21).
  - `SetDescriptorHeaps` skips a null entry instead of dereferencing it. Row: `heap-swap` now binds `{null, sampler heap}`
    between its two dispatches, and `DXIL heap-swap matches its CPU reference` holds. The RED is by reading: the old loop
    called `GetDesc()` on every entry.
  - A WaveSize or WaveSizeRange property too short to read is refused (`DXIL: malformed WaveSize metadata`) instead of
    skipped; WaveMatch and WaveMultiPrefixOp on f64 are refused by name. Neither is reachable from DXC output (the device
    reports no double ops), so the evidence is reading and the corpus.
  - Corpus: `corpus-diff: 11101/11101 ok, 0 new failures, 0 fixed, 0 changed, 11101 unchanged` against 0057; DXBC
    `2/2 ok, 0 changed`. Fresh fetch: `applied 58/58`, tree-equal. Gate (ARM64EC): `dxmt-check: all passed`, 351 `ok`.
  - Deferred (rulings R-HARD2, R-HARD3): items 2 (compute cube LOD projection), 7 (heap ranges sharing usage) and 14
    (per-range `atomic_view`), and the test-only items.
- **0059 (`d3d12: Report Shader Model 6.8.`):** Task 15. The device reports what Tasks 1-14c translate.
  - `CheckFeatureSupport(SHADER_MODEL)` answers up to 6.8: a request for 0x69 reads `shader model 0x68` (it read 0x67), and a
    6.7 request still reads 0x67.
  - OPTIONS11's `AtomicInt64OnDescriptorHeapResourceSupported` is `SM6Caps()` (it was zero-filled): heap handles resolve to
    the same pointers as bound ones, so the lock path (0054, 0055) covers them. OPTIONS9 keeps both 64-bit atomic bits.
    `Int64ShaderOps` stays FALSE until an i64 audit (the 64-bit cbufferLoad is refused).
  - OPTIONS21 reports `SampleCmpGradientAndBiasSupported` and `ExtendedCommandInfoSupported` (Tasks 11, 12); work graphs stay
    tier 0. OPTIONS22 (feature 65) is answered instead of `E_NOTIMPL`: no reordering, no byte-offset views, 1D dispatch
    limits 65535.
  - Before: `options11 heap 0`, `options21 … grad/bias 0, extended command info 0`, `options22 hr 0x80004001`. After, on
    the default device: `options9 typed 1 groupshared 1`, `options11 heap 1`, `options21 hr 0x00000000 work graphs 0,
    sample cmp grad/bias 1, extended command info 1`, `options22 hr 0x00000000 reorders 0, byte offset views 0, max 1D
    dispatch 65535 65535`. With `DXMT_D3D12_SM6=0`: shader model 0x51, OPTIONS9/11/21 all 0, OPTIONS22 answered.
  - `src/airconv` is untouched: no corpus run. Fresh fetch: `applied 59/59`, tree-equal.
  - Milestone 3 (both lanes and `make test`): the first run had one `FAIL`, D3DMetal's own hazards run (the reference,
    not our DXMT), which reported `fence-order 1 0` and stopped there; the rerun passed, `dxmt-check: all passed` twice
    (728 `ok`). `make test`: 290 passed.

- **0060 (`airconv: Let a simdgroup with no live lane run wave ops over its helpers.`):** the final review's one
  finding graded up, by effect (Important: a GPU hang).
  - **The bug.** A pixel shader without `[WaveOpsIncludeHelperLanes]` masked its wave operations to the non-helper
    lanes (0041). In a waterfall loop, once the covered lanes had left, only helper lanes remained, and their mask was
    empty. WaveReadLaneFirst then read lane 32, WaveActiveMin returned the identity, and the helpers never left the
    loop.
  - **The fix.** Such a simdgroup now runs the operation over all its active lanes, as Metal does. Covered lanes'
    results are unchanged. Spec §3.2 gains the clause.
  - **The rows.** `helper.pswaterfall` (WaveReadLaneFirst) and `helper.pswaterfallmin` (WaveActiveMin) run a waterfall
    loop bounded at 1,000 rounds on a triangle whose edges hold helper quads. Each pixel paints red if any lane of its
    quad hit the bound.
    - RED, on 0059: both images differed from the all-green image (`312098d48c753442` against `7b66487a6ba982e0`) in
      both lanes. The helpers did spin on Apple's GPU.
    - GREEN: both images are the all-green one in both lanes.
    - `dxil-translate` now counts 54 test shaders, not 52.
  - **Corpus**, against 0058: `11101/11101 ok, 0 new failures, 0 fixed, 6 changed`. The six pixel shaders with
    WaveReadLaneFirst each get one more ballot. DXBC: `2/2 ok, 0 changed`.
  - Fresh fetch: `applied 60/60`, tree-equal.
  - Not reachable in SMITE 2 or Palworld: their pixel shaders call their one wave op in the entry block, outside any
    loop.

### SMITE 2 on DXMT 0040-0059 (2026-10-12)

- **Build:** the maintainer's session on `build/dev/MacNeutron.app` (`make app` at e5aeb43: DXMT 0059 and Proton parity
  P0-1). launcher.log has `runtime=dev (48329b3d1c02)`, the dev app's `wine.app`.
- **Session:** about 2.5 minutes (launcher lines 10:52:01Z to 10:54:31Z), `exit=0`.
- **Shader rebuild:** the replay rebuilt the recorded pipelines once for the new translator key: 24,813 pipelines (21,354
  graphics, 3,459 compute), 24,813 created, 0 failed, 0 bad records, 10,616 ms.
- **Steam client DLL:** Valve's client 1788652215, from the app's download.
- **Game log:** not written for this session (no `MACNEUTRON_LOG=1`), so there are no `not supported` or
  `Failed to compile` counts.
- **Not covered:** a second start (for "no rebuild the second time") and a match. Also, 0060 came after this session
  and changes six of SMITE 2's pixel shaders, those with a wave op. A logged session on a 0060 build (a match and a
  second start, `MACNEUTRON_LOG=1`) closes spec §8 item 7.
- **Maintainer's verdict:** "working fine".


### SMITE 2 on DXMT 0040-0060, logged (2026-10-12)

- **Build:** `build/dev/MacNeutron.app`, built from `main` with 0060. launcher.log has `runtime=dev (7329939d0d19)`.
  The launch line was `/usr/bin/env MACNEUTRON_LOG=1 %command%`.
- **Session:** about 2 minutes (launcher lines 12:38:54Z to 12:40:44Z), `exit=0`.
- **Shader rebuild:** the replay rebuilt the recorded pipelines once for the 0060 key: 24,814 (21,355 graphics, 3,459
  compute), 24,814 created, 0 failed, 0 bad records, 10,580 ms.
- **Game log:** 0 DXIL refusals. `not supported`: 682, and `Failed to compile`: 2. Those are the same counts as SMITE
  2's logged session of 2026-10-10, before this series, so there are 0 new:
  - the two compile lines are Unreal's material warnings about two rock assets;
  - the `not supported` lines come from OpenGL, wined3d and Streamline.
- **Steam client DLL:** Valve's client 1788652215.
- **Not covered:** a second start (no rebuild the second time). The session was too short for a full match.
- **Maintainer:** the run is done, and no problem was reported.

### Rulings made while executing the plan (from its ledger, verbatim)

- Ruling: work on main in the repo and on branch macneutron in build/wine-arm64-src/dxmt, as every earlier plan — commits stay local (no push without a new yes) — cost if wrong: one branch move.
- Ruling: pre-flight = the two plan critics (coverage, executability) + a fixer + a recheck (wf_5d8ccbc8-7a8, wf_caf4af84-8ee) instead of a controller-only scan — cost if wrong: a conflict surfaces in a task review.
- Ruling S-R6 (from rulings.md): builds and gates run under taskpolicy -b while the critical-section harness timing (/private/tmp/claude-501/bakeoff ab2.sh/p4.sh, wf_6697997a-0e7) is active.
- Ruling R-T1a: fix round 1 = make the PS IsHelperLane path tested now (sm6x.hlsl psmain zero term), corpus-diff fails on missing air= and on missing shaders (every later task relies on it), the geometry-pipeline helper link from the DXIL vertex shader's flags (+ a VS+GS row if one compile suffices, else ledger), strip the helpers' fast-math function attributes, restore the doc comment, ignore __pycache__, record the sample-position runtime gap in the acceptance bullet — cost if wrong: one round.
- Ruling R-T1b: 0040 is unpushed and unreleased, so the fix amends DXMT d2eb6ae and re-exports 0040 with a new 40/40 proof (R46a precedent) — cost if wrong: none.
- Ruling R-T2a: fix round 1 (test-only, no DXMT change, 0041 unchanged): add sum/prefix-sum/prefix-product terms whose helper operand differs from the identity (keep the existing 1u-h lines that protect 135/136), one float and one signed-int min/max/sum line, a lane-distinct BitXor, a content check that psgreen's 12 sampled pixels are all green, acceptance wording with the op-level confirmation; RED by single-op mutation for 118, 110 and 119-sum — cost if wrong: one round.
- Task 3: implementer DONE_WITH_CONCERNS — DXMT b1c67c0 (0042), repo d6b3cf8; RED 'waveMatch.i32 (165) not supported' → GREEN 16/17, wavematch row ok both lanes; six single-op breaks each fail the row; corpus 0 changed, DXBC 0; dxmt-check 516 ok both lanes; make test 252; fresh fetch 42/42. Additions beyond brief (D3D 6.5 spec): float WaveMatch NaN-safe exit; MultiPrefix ignores inactive lanes. Ruling R-T3a: irregular partitions ((w >> 4) & 3) before review, new repo commit, DXMT unchanged — the brief's inputs gave lane&3 partitions in every wave — cost if wrong: none.
- Ruling R-T3b: one test-only fix round — a NaN lane in the float WaveMatch call (singleton masks expected; removing the guard must hang to the watchdog = FAIL), one divergent row (ops under a lane-dependent if, CPU reference over active lanes only), a comment pinning the +0.0 prefix choice, and the two acceptance wording fixes; the f64 guard and the other coverage minors are deferred (recorded here) — cost if wrong: one round.
- Ruling R-A64 (maintainer: "Can we also fix the 64-bit atomic instead of disabling it ?"): implement — insert Tasks 14a (raw/structured buffers + per-device lock table + trailing root-argument qword, DXMT 0054), 14b (typed buffers via the raw path + RG32Uint textures, 0055), 14c (groupshared lock array, 0056) before Task 15 (→ 0057); Task 9's canary becomes TextureGatherRaw and its gsatomic64 refusal stays until 14c with the corrected reason (compiler-service crash); Task 15: OPTIONS9 honest (S-R2 over-report removed), OPTIONS11 AtomicInt64OnDescriptorHeapResource TRUE, Int64ShaderOps stays FALSE (i64 audit = next spec); runtime re-zeroes the lock table on command-buffer error; acceptance does not claim M1 until run on an M1 — cost if wrong: three tasks of work and a GPU-time cost only for shaders that use 64-bit atomics.
- Ruling R-T5a: test/doc-only fix round — add a D3DMetal run of the float pair (tri-v32-ref) and a same_pixels row (precedent check.sh:549/:552), reword the D3DMetal vs16+ps32 cause to the observation, and the acceptance bullet states the GS/IA/integer-target/centroid paths are by code reading only — cost if wrong: one short round.
- Ruling R-LITE (maintainer 2026-10-11: "Can we lighten the test to speedup the development?"), effective from Task 6: per-task gates = the full check in ONE lane (ARM64EC; the translator is lane-independent), no make test (no Swift touched); full both-lane dxmt-check + make test only at milestones (after Task 9, after Task 14c, at Task 15) and the final review; single-mutation REDs: one or two per new mechanism; task reviews: 2 lenses (sonnet spec+gate, opus translator), skeptics only on Critical/Important; Minor findings get no fix round — collected for one test-hardening pass before Task 15 (or folded into the next task touching the same file); small test/doc-only fix rounds are re-checked by the controller reading the diff, no re-review workflow. Kept: main RED/GREEN, P-corpus incl. DXBC pair on translator tasks, P-export + fresh-fetch proof, P-compile, privacy scan — cost if wrong: a regression in an unrelated row surfaces at the next milestone (bisect ≤ 3 tasks).
- Ruling R-T6a (R-LITE): no fix round; the constant-branch wrap ((c & 15) ^ 8) - 8 for Load/Sample (gathers keep wrap=false) plus offsets.hlsl rows for a constant out-of-range Load and a mixed constant/runtime Load are folded into Task 7 (it edits dxil_lower_resources.cpp and uses Task 6's Offsets()); identity on [-8, 7], so corpus and DXBC pair stay 0 changed.
- Task 10: implementer DONE_WITH_CONCERNS — DXMT 0e24023 (0049), repo 67f008c; ARM64EC lane all passed (276); RED heap/heap-swap/tri-heap fail before; swap and index+1 mutations fail; root-constant rounding fails heap rows; gap: sampler-heap base late or zero passes (zero sampler = point clamp on this GPU, texel-centre sampling). Ruling R-T10a: close the sampler-heap hole before review (linear/wrap heap sampler, off-centre coordinates, CPU/D3DMetal expectations; RED for late and zero sampler-heap base).
- Ruling R-T13a: fix round 1 — guard `if (!texel) return Unsupported(call);` at the top of result (covers 64, 65, 74, 254, 255 and the compute coarse path); reject shaders for Texture1D 254/255 and 64 with named-refusal rows (Lane E offline; counts per the ladder); correct the report/acceptance/ledger premise; amend DXMT d4538d1 (unpushed, R46a) and re-export 0052 with a new 52/52 proof; mapping compared 1D to depth_2d stays out (changes DXBC; a later task). HARDENING item (13) is superseded by this round.
- Ruling R-T14a: task-14b-brief.md:21 says UAVTexelBufferGPUStorage::padding[1]; after 0053 the field is scalar 'padding' (d3d12_descriptor_heap.cpp:44) — brief corrected before dispatch — cost if wrong: none (name only).
- Ruling R-SPIKE1: after Task 14a's review is dispatched, the SM 6.8 pipeline pauses before Task 14b while the controller builds the steamclient64 spike (Wine loader patch in the shared build/wine-arm64-src/wine tree + make app) for the maintainer's Palworld test — the maintainer's launch failures take priority and the build tree is shared — cost if wrong: Task 14b starts ~1-2 h later.
- Ruling R-T14b0: before dispatching 14b, correct its brief — `MemFlags` is an enum class without operator| (air_builder.hpp:91): use a cast or add the operator; and the 14b/14c briefs' 'a geometry pipeline ORs its two stages' flags' is wrong: each stage binds with its own ShaderInfo (dxbc_converter_gs.cpp:138, :572) — cost if wrong: an implementer round trip.
- Ruling R-T14b1: accept mutation 1 as unobservable at runtime on this GPU, as Task 14a's fences were (its review: the fence row proves emission, not need); the Lane E fence row is the guard. HARDENING PASS += (23) a texel CAS counter across 64 groups in atomic64c (cross-core re-read, the spike's device-only failure shape) to try to make Device-only texture fences fail at runtime. Accept the address64 deviation (evidence: DXC 1.9 output). Finish: export 0055 + fresh-fetch proof, final ARM64EC gate, repo commit, acceptance bullet — cost if wrong: a missing texture fence on another GPU family goes unseen until hardening.
- Ruling R-NATIVE: executor switched to native (superpowers:executing-plans) for the rest of the plan — maintainer 2026-10-11: "Implement remaining task as native instead of subagent" — per-task gate is the controller's own TDD + R-LITE gate; one fresh whole-branch review at the end stays — cost if wrong: fewer per-task second opinions.
- Ruling R-PAL1: Palworld's crash on New Game (two vs_6_6 shaders: DXC's hoisted local constant arrays <name>.hca keep llvm.lifetime markers; MoveConstantsToConstantSpace refused them) is fixed as DXMT 0056 = 833202b between 14b (0055) and 14c, so 14c → 0057 and Task 15 → 0058 (plan, globals, 14c brief renumbered); test: dxil/buffers.hlsl slot 15 (a local int2 array of constants read in a [loop]; DXC 1.9 cs_6_6 reproduces kNeighbor.i.0.hca + markers), RED offline 'fail buffers.dxil DXIL: a static const array used other than by loads' → GREEN; Palworld capture ~/dxil-palworld 189/191 → 191/191; corpus 0056 vs 0055 0 changed, DXBC 2/2 0 changed; fresh fetch DXMT 56/56 tree-equal; ONE ARM64EC gate covers 14b and 0056 (task-14b-0056-dxmt-check.log) — maintainer's game first — cost if wrong: a gate failure needs bisecting between two commits.
- Task 14c: start (native, BASE repo 927556a, DXMT 833202b). Ruling R-T14c0: corpus baseline is 0056 (Palworld fix, renumbering left 'against 0055'); a lock deadlock shows as a Metal GPU-hang error (14a), not the watchdog — Step 5's naive-loop RED expects that — cost if wrong: none.
- Ruling R-T14c1: Step 5's naive-loop mutation (an exec deadlock, a Metal GPU-hang error) is not run: UnderLock is the same code Task 14a mutated (its naive loop hung the GPU), and R-LITE asks one or two mutations per new mechanism — the two offline mutations (zeroing barrier, threadgroup fences) cover what 14c adds — cost if wrong: a threadgroup-specific forward-progress problem shows only in the gate's exec rows, which pass.
- Ruling R-HARD: the hardening pass splits. H1 now (behaviour a game can hit): (10) SetDescriptorHeaps skips a null entry; (14) per-range atomic_view (option a); (7) heap ranges sharing usage; (2) cube CalculateLOD in compute refused by name; (4) WaveSize malformed-node fail-open, preferred != 32, f64 ops 165/166 Unsupported; (20) pStaging const pointer; (21) Simd() non-speculatable comment. H2 deferred (test-only additions, no behaviour change): (1) compute CalculateLOD bias/MinLOD/MaxLOD row + fix, (3), (5), (6), (9), (11), (12), (15), (16), (17), (18), (19), (22), (23) — listed for after Task 16 or the next SM work. Batch RED: H1's rows go in first and one ARM64EC gate on the unfixed DXMT shows them failing, then the fixes and one gate GREEN — maintainer's Proton-parity directive (2026-10-12) takes the time H2 would — cost if wrong: test gaps stay open longer; no player-visible behaviour is deferred.
- Ruling R-HARD2: (2) moves to H2 as 'major-face projection' — a named refusal would fail the pipeline, which Unreal treats as fatal (a crash), worse than today's approximate LOD — cost if wrong: compute cube LODs stay approximate. (10)'s RED is by reading (Heaps[i]->GetDesc() on a null entry dereferences null); its GREEN is the heap-swap rows with the null call in the next gate. (4)'s fixes are unreachable from DXC output (malformed WaveSize nodes, f64 wave ops with DoubleOps FALSE): evidence is corpus 0 changed + reading.
- Ruling R-HARD3: (7) heap-range usage keying and (14) per-range atomic_view move after the Proton-parity P0 work and the app-upscaling spec (the maintainer's newer requests); both limits stay documented in the acceptance record — cost if wrong: a rare shader mixing those patterns keeps the documented wrong result longer. The H1 fixes done (10, 4, 20, 21) land as DXMT 0058; Task 15 becomes 0059.
- Task 16: complete (README f9667e2; summary and SMITE 2 session da0f265). Ruling: the summary's corpus line runs 0039 → 0058, not 0057 (renumbering R-PAL1/R-HARD3 made 0058 the last src/airconv patch) — cost if wrong: none. Ruling: summary and session in one commit (same file) — cost: none. Session facts: runtime dev (48329b3d1c02), ~2.5 min, exit=0, replay 24813/24813 in 10.6 s, no game log (no MACNEUTRON_LOG=1), no second start or match; verdict 'working fine'.
- Final: Ruling: re-graded "masked wave ops give no usable result when every active lane is a helper (waterfall loop spins: GPU hang)" Minor → Important by effect — a GPU hang (device removed, fatal in Unreal) in any game with a waterfall loop in a pixel shader; the RED proved the spin on Apple's GPU — cost if wrong: one extra ballot per pixel-shader wave op.
- Final: Ruling: corpus summary range 0039 → 0060 and the translator key c588d3fc… replace the 0059 values in the acceptance summary — cost if wrong: none.
- Final: Ruling (declined list, 62 items): set aside by the reviewers as accepted rulings, documented limits or polish (e.g. compute cube LOD projection, per-range atomic_view, heap-range usage keying — R-HARD2/R-HARD3; WaveSize E_FAIL vs E_INVALIDARG; 1D comparison refusals; lock-table re-zero on GPU fault) — stand as recorded in the acceptance record — cost if wrong: the documented limits stay longer.
