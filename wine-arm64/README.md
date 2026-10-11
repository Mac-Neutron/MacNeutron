# wine-arm64: native arm64 Wine and FEX

`make wine-arm64` builds the first stage of MacNeutron's native arm64 stack: upstream Wine 11.19 (ARM64EC and arm64),
with our patches, FEX, which runs x64 Windows code inside it, and our DXMT built for arm64 (Direct3D 11/12; D3D10's front end bundled, untested),
staged as one signed, entitled `build/wine-arm64/wine.app`. Every Windows process runs natively on arm64 with 4K
pages; only the game's x86-64 code is translated. It also carries FreeType and gnutls built from pinned source
(Windows text, dialogs and TLS), msync (on wherever `WINEMSYNC=1` is set: `check.sh`, and the launcher unless `MACNEUTRON_NO_MSYNC=1`), the Steam bridge (Proton's `lsteamclient`, built for arm64),
strict x18 toggling, and every component's licence in `Contents/Resources/licenses`.

Design, gates and risks: [`docs/superpowers/specs/2026-10-02-macneutron-native-arm64-design.md`](../docs/superpowers/specs/2026-10-02-macneutron-native-arm64-design.md)
(Wine and FEX), [`docs/superpowers/specs/2026-10-03-macneutron-arm64-dxmt-design.md`](../docs/superpowers/specs/2026-10-03-macneutron-arm64-dxmt-design.md) (DXMT)
and [`docs/superpowers/specs/2026-10-04-macneutron-ship-base-wine-design.md`](../docs/superpowers/specs/2026-10-04-macneutron-ship-base-wine-design.md) (ship-base Wine).
Results on the maintainer's Mac: [`docs/testing/acceptance-arm64-wine.md`](../docs/testing/acceptance-arm64-wine.md),
[`docs/testing/acceptance-arm64-dxmt.md`](../docs/testing/acceptance-arm64-dxmt.md) and
[`docs/testing/acceptance-arm64-ship-base.md`](../docs/testing/acceptance-arm64-ship-base.md).

This is the runtime MacNeutron ships: the app embeds this `wine.app` (`Contents/Helpers/wine.app`) and the launcher runs
every game on it. A build needs the Developer ID setup below; there is no ad-hoc signed `wine.app`.

## Requirements

- Apple Silicon, **macOS 27**, and Xcode (Apple clang).
- Homebrew `autoconf`, `bison`, `flex`, `cmake`, `ninja`, `meson`, `pkg-config` and `gettext` (its `msgfmt` builds
  Wine's translations). The build names whatever is missing and never installs it. The build itself doesn't run
  `autoconf`; the development loop needs it for a patch that changes `configure.ac`.
  No Homebrew library reaches the build: `pkg-config` looks only in `build/wine-arm64-src/deps`, `CPATH`,
  `LIBRARY_PATH`, `CFLAGS` and `CXXFLAGS` are unset, and the build stops if a flags line of Wine's `config.log` names
  `/opt/homebrew`, `/usr/local` or `/opt/local`, or if Wine's configure found a dlopened library other than FreeType,
  gnutls and libodbc.
- Xcode's Metal Toolchain, for DXMT's shaders (`xcodebuild -downloadComponent MetalToolchain`).
- Windows-side code is built with the pinned llvm-mingw, which `dxmt/toolchain.sh` fetches once (again when
  `dxmt/pins` names another one). Objects already built keep the old compiler's output: after a toolchain pin bump,
  remove `build/wine-arm64-src` (or its `wine-build`, `fex-ec`, `fex-unixlib` and `dxmt-build` folders) for a clean build.
  The arm64 Windows side targets the Apple M1 instruction set, `-march=armv8.5-a+fp16fml+aes+sha3` (`build.sh`'s and
  the `Makefile`'s `PE_MARCH`, DXMT's cross file), with generic tuning: Apple tuning (`-mcpu=apple-m1`,
  `-mtune=apple-*`, `-falign-loops`) crashes llvm-mingw's SEH unwind emitter, so never add it. Wine takes it as
  `aarch64_CFLAGS` and `arm64ec_CFLAGS`, not in `CROSSCFLAGS`: arm64ec brings in an x86_64 extra arch (ARM64EC
  modules' x64 sources), whose compiler `CROSSCFLAGS` reaches too.
- The first build downloads the five tarballs of `deps.pins` (27 MB, checked by SHA-256) and a sparse checkout of
  Proton's `lsteamclient/` folder from GitHub (18 MB of source, about 10 s here); later builds reuse them.
- **A Developer ID with the "Cross-architecture Compatibility Framework" capability** (`com.apple.developer.cross-architecture-support`)
  granted for the App ID `net.authspot.macneutron.wine` (team `49QMZXLR8S`), and a Developer ID provisioning profile for it.
  Without the entitlement the loader can't map the low 4 GB or get 4K pages, so there is no ad-hoc mode.

```sh
export MACNEUTRON_SIGN_IDENTITY="Developer ID Application: … (49QMZXLR8S)"
export MACNEUTRON_PROVISIONING_PROFILE=/path/to/the.provisionprofile   # never committed
```

**Anyone else needs their own App ID and their own grant** from Apple: only a team with the capability can produce a
working runtime. Then change `APP_ID` in `lib.sh` and the identifiers in `wine.entitlements` and `Info.plist` (the
profile check's test, `tests/profile_test.sh`, and its fixtures name the App ID too), and use your team's identity and
profile.

## Build and check

```sh
make wine-arm64        # fetch Wine, FEX and DXMT at the pins, patch, build, sign; build/wine-arm64/wine.app (a few minutes the first time)
                       # a cold first build includes the arm64 LLVM (about 2 min here) and the deps (about 3 min)
make wine-arm64-check  # boot, 4K pages, native ARM64, FEX, gates G1-G5, D2-D4 and S1-S7 (about 38 min)

make build bridge wine-arm64-tests wine-arm64-winetests dxmt-tests dxmt-tests-arm64ec presenter  # what check.sh needs besides the runtime
sh wine-arm64/check.sh g2-litmus   # named steps only (and the steps they need); see STEPS in check.sh
```

`make wine-arm64-check` builds the launcher (`make build`), the Steam bridge (`make bridge`: `steam-bridge` needs
`build/bridge/arm64/steam.exe` and `build/bridge/steamprobe.exe`), the test programs (`make wine-arm64-tests`) and Wine's conformance tests (`make wine-arm64-winetests`, two test-only Wine trees) and what the
DXMT steps run (`make dxmt-tests dxmt-tests-arm64ec presenter`: the x64 and ARM64EC D3D test programs and `present_loop`) itself, but `make wine-arm64` does not, and `check.sh` run on its own needs them all (G4
runs the launcher). Gate G4's Rosetta baseline runs the frozen reference tool (`tools/freeze-rosetta-reference.sh` makes it from the
last Rosetta-era tool folder; `MACNEUTRON_REFERENCE` names another). Each run starts from a clean prefix under `build/wine-arm64 check/`, and ends by checking that
no process of either runtime is left. Every run sees `WINEMSYNC=1`, as the launcher does: a client and its
wineserver must agree, so only the `msync` step, which starts its own servers, sets it otherwise.

The full check needs, besides the build:
- **the frozen reference** (`tools/freeze-rosetta-reference.sh`, `MACNEUTRON_REFERENCE`), which holds the Rosetta
  runtime and the imported GPTK (G4's baseline and the DXMT lanes' D3DMetal reference);
- **Steam running and logged in**, and **SMITE 2** installed in Steam's default library (`steam-bridge`, and
  `dxmt-x64`'s FSR 3 check);
- **Screen Recording** for the app that runs the check, and nothing in native full screen on the main display
  (`dxmt-present`, below). Windows appear on the display during the check.

`make wine-arm64-check` runs `licences_test.sh` (after `mode_test`, `profile_test` and `translator_key_test`) before
`check.sh`: on the staged bundle, and its `--self-test`, which must go red on a copy with a licence file deleted and on
one with an extra FEX external (gate S1). The ship-base steps (ship-base spec §10), before the DXMT steps:

| Step | What |
|---|---|
| `wxflip-x64` | Gate S4: `x64-smc` under FEX rewrites and runs code in RWX memory and its own `.text`, with 0 `trace:wxflip` lines |
| `msync` | Gate S3: `x64-sync` under FEX with `WINEMSYNC=1`, then `0`, each against a server the step starts; 19 gated rows, both mismatch directions, and the timing rows (M1) |
| `x18` | Gate S5: 16 threads checking x18 (T1), every path to unix code and back in both lanes (T2), a double enable that must reach the toggle's trap, which exits 133 in self-test mode without a crash report (T3), a suspend stress (T4), and where `ntdll.so` names x18 |
| `fonts-tls` | Gate S2: Tahoma's metrics and dialog base units (win32u's FreeType), DirectWrite's font families, schannel credentials and a PFX import (gnutls) |
| `steam-bridge` | Gate S7: the arm64 Steam bridge, below |

`lanes` (`make lanes-check`), run by name and not in the full run, measures rather than gates: `x64-sync` and `arm64-xcall` as ARM64, ARM64EC and x64 (FEX) programs in mode 1, passing when each program passes and prints its time rows (17 for `*-sync`, 16 for `*-xcall`); `tools/lanes_report.py` turns three runs' `lanes.log` into the table of `docs/testing/acceptance-arm64-release.md` (batch Task 2's baseline).

The DXMT steps, after `steam-bridge`:

| Step | What |
|---|---|
| `dxmt` | Copies the bundle's front ends into the prefix's system32, turns the crash dialog off, checks the builtin markers and `DXMT/version` |
| `dxmt-present` | Gate D2: `present_loop` (D3D11) and `d3d12_clear` (D3D12) windows on screen in both lanes, each read by `winshot`; then 20 window cycles (ARM64EC) |
| `dxmt-arm64ec` | Gate D3: `dxmt/check.sh` in arm64 mode with the ARM64EC test programs |
| `dxmt-x64` | Gate D4: the same with the x64 test programs under FEX, the FSR 3 check included |

`winshot` (`tools/winshot.c`) captures a window, so the app that runs the check (Terminal, or whatever starts `make`)
needs System Settings › Privacy & Security › Screen Recording; without it `dxmt-present` fails and names that setting.
With an app in native full screen on the main display, Wine's windows open on that display's hidden desktop Space and
`dxmt-present` fails with `winshot: no on-screen window titled …`.
The lanes compare our DXMT with D3DMetal on the frozen reference's Rosetta runtime, as `make dxmt-check` does, so they
need what it needs: the frozen reference (`MACNEUTRON_REFERENCE`).
`dxmt-x64`'s FSR 3 swap chain check also needs SMITE 2 installed in Steam's default library (its `amd_fidelityfx_dx12.dll` is read
from `~/Library/Application Support/Steam/steamapps/common/SMITE 2`, never copied): without it `dxmt-x64` fails naming the skip, and the steps after it (`g4-bench`, then the batch's `fex-vmd`,
`winetests`, `ec-hook` and `crt`) don't run.

The batch's gated steps (`BATCH`), run last, after `g4-bench`:

| Step | What |
|---|---|
| `fex-vmd` | FEX patch 0006 (batch Task 4): EVMD over x64-litmus shows MP reordering, listed instructions keep TSO (all of `run` and `worker`, or only MP's flag store and load), FEX logs its coverage; x64-bench's scalar-memory kernels run in at most 0.75 of their time with their ranges |
| `winetests` | Wine patch 0033 (batch Task 8): Wine's conformance tests `ntdll:rtl`, `kernel32:sync`, `atl:module`, `atl100:atl`, `msvcirt:msvcirt` and (batch Task 6) `msvcrt:string` in all three lanes (x64 under FEX), built by `make wine-arm64-winetests` in two test-only Wine trees; each exits with its failure count, and fails only at locations listed in `WINETESTS_FAILS` |
| `ec-hook` | Wine patch 0034 (batch Task 5): an ARM64EC program's import entries are filled; a hooked export, a hooked IAT entry and an unloaded DLL's reused range behave as without the fill; a thread frozen inside a fill doesn't hold up a hooker's VirtualProtect |
| `crt` | Wine patch 0035 (batch Task 6): msvcrt's string routines and memcpy against byte loops in all three lanes: every length to 300, every alignment, overlaps, both sides of a no-access host page |

### FreeType and gnutls

`build.sh` builds FreeType 2.14.3 and gnutls 3.8.13, with nettle 4.0 and gmp 6.3.0 linked statically into it, from the
tarballs pinned in `deps.pins` into `build/wine-arm64-src/deps` (about 3 min), with `/usr/bin/clang` and nothing from
Homebrew (`PKG_CONFIG_LIBDIR` is the deps' own), then configures Wine against them with `--with-freetype --with-gnutls`.
The step is redone only when the tarball pins or its own commands change (a hash in `deps/.complete`), and that
reconfigures Wine (one full Wine rebuild). `bundle.sh` copies `libfreetype.6.dylib` and `libgnutls.30.dylib` into
`lib/wine/aarch64-unix/`, beside the unix modules that load them by name, and checks: their `@rpath` install names;
that every bundled Mach-O depends only on `/usr/lib`, `/System` and `@` paths and has no absolute rpath; that they
export every symbol Wine looks up (46 for FreeType, 70 for gnutls); that they hold no build path; and the x18 scan:
no arm64 code in the bundle names x18 (`ntdll.so` aside, which the `x18` step checks) except the data words after
`ret` in gnutls's CRYPTOGAMS routines that `x18-allow.txt` lists. A gnutls update that moves those fails the build until the allowlist is checked again, by
reading the routines.

The same step builds FFmpeg 8.1.3 (video playback spec §6), LGPL, as five dylibs that `winedmo.so` links
(`--with-ffmpeg --without-gstreamer`). It stops unless FFmpeg's generated `config.h` and `config_components.h` show no
GPL part, no VideoToolbox, none of FFmpeg's own H.264, HEVC or AAC decoders, and exactly the decoders, demuxers,
parsers and bitstream filters `build.sh` lists; it also stops if an object with code in it was built from a file whose
licence is GPL-only (`--disable-arm-crc` keeps libavutil's GPL-2.0+ `aarch64/crc.S` out), or if the dylibs link
anything but each other by `@rpath` and the system's, or hold a build path. `bundle.sh` puts them beside `winedmo.so`.

### The Steam bridge

`wine.app` carries the Steam bridge on arm64 (ship-base spec §7): Proton's `lsteamclient` (pinned in `deps.pins`, with
`patches/lsteamclient/`), built by Wine's own build as an ARM64X `lsteamclient.dll` and an arm64 `lsteamclient.so`
that loads the arm64 slice of Mac Steam's universal `steamclient.dylib`. `make bridge` also builds an aarch64
`steam.exe` and its test helper into `build/bridge/arm64/` (one `steam.exe` per runtime, as spec §7 has it). The launcher
copies the DLL into game prefixes.

The `steam-bridge` step (before `dxmt`) runs `bridge/check.sh` in arm64 mode (no Steam needed), then
`bridge/probe.sh` in arm64 mode: the x64 `steamprobe.exe` under FEX loads SMITE 2's `steam_api64.dll` in place, with
the bundle's `lsteamclient.dll` as `steamclient64.dll`. It needs **Steam running and logged in** and **SMITE 2
installed** in Steam's default library; without either it fails naming what is missing. It passes on `init: ok`,
`steamid ok`, `persona ok`, an auth ticket of more than 0 bytes with its callback, and `fault: caught` (an access
violation after `SteamAPI_Init` still reaches SEH). The probe runs with `PROBE_REDACT=1`, so the log never holds the
SteamID or the persona name; run it by hand the same way. The x18 hits in Valve's arm64 code are reported, not gated.

## Layout

| Path | What |
|---|---|
| `pins` | Wine tag and commit, FEX commit, and the source of FEX's macOS unixlib |
| `deps.pins` | The five tarballs (FreeType, gnutls, nettle, GMP, FFmpeg), and lsteamclient's repository and commit |
| `patches/wine/`, `patches/fex/`, `patches/dxmt/`, `patches/lsteamclient/` | The patch series (`git format-patch` output, applied with `git am`): the source of truth |
| `build.sh`, `bundle.sh` | Build, then assemble and sign `wine.app`, and check the result |
| `wine.entitlements`, `Info.plist` | The loader's entitlements and the bundle's identity |
| `licenses/` | The bundle's `licenses/README` (component, licence, source) and `NOTICES.md` (notices found only in source headers); `bundle.sh` adds the licence texts and `SOURCE` |
| `x18-allow.txt` | The only x18 hits `bundle.sh`'s scan accepts (file, routine, count) |
| `check.sh`, `tests/`, `tools/` | The checks, the test programs (`x64-*`, `arm64*`), the licence test, and the helpers behind G3, G4, D2 (`winshot`) and the x18 scan (`x18scan.sh`) |
| `export.sh` | Writes commits made in the source trees back to `patches/` |

## Development loop

The patch files are applied to the pins in `build/wine-arm64-src/wine`, `fex` and `dxmt` (git trees on branch
`macneutron`).

1. Edit and commit in `build/wine-arm64-src/<wine, fex or dxmt>`. Any change there makes the next build a "development
   build", which builds the tree as it is and skips the fetch, the patching and the up-to-date check. So does a stash,
   a second branch or a second worktree in that tree. If your patch changes `configure.ac`, run `autoreconf` with
   autoconf 2.73 and commit `configure` in the same patch (as Wine patch 0001 does): the build doesn't run it.
2. `make wine-arm64`, and once (or after a change to a test program or the Swift sources) `make build wine-arm64-tests`.
3. `sh wine-arm64/check.sh <steps>` while working; `make wine-arm64-check` before committing.
4. `make wine-arm64-export` writes the commits back to `wine-arm64/patches/`.
5. Commit the patches in this repo. A commit message says why the change exists, with the failure that made it necessary.

DXMT works the same way. Its tree is `dxmt/pins`' `DXMT_COMMIT` (the frozen reference's DXMT is not rebuilt from it) plus `patches/dxmt/`, built for ARM64X with DXMT's own `build-arm64ec.txt` against
this Wine's build tree, with an arm64 LLVM 15 built once into `build/wine-arm64-src/llvm-arm64` by `dxmt/llvm.sh`.
`make wine-arm64` also builds the arm64 `dxil-probe` and `dxil-translate` into `build/wine-arm64/`. A commit in
`build/wine-arm64-src/dxmt` makes the build a development build, and DXMT's version token
(`build/wine-arm64-src/dxmt-install/version`) ends in `+dev` instead of the series hash; `make wine-arm64-export`
writes the commits to `patches/dxmt/`. DXMT's shader-translation cache and the launcher's replay stamp follow neither:
both are keyed on the translator's key (`lib.sh`'s `translator_key`, in `dxmt-install/translator` and the bundle's
`DXMT/translator`), a hash of what changes translated output as the tree has it, committed or not: `src/airconv`,
`libs/DXBCParser`, `include`, DXMT's top-level meson files, d3d11's compile arguments (`src/d3d11/d3d11_shader.cpp`,
whose cache entries key on the variant, not on the arguments), the airconv thunks (`src/winemetal/airconv_thunks.*`),
the buildtype, the LLVM pin and recipe, Apple clang's and
metal's versions. A re-fetch of the same patches or a change elsewhere in DXMT keeps players' translated shaders and
starts no replay; an edit to airconv, even uncommitted, gets new ones (`tests/translator_key_test.sh`). A DXMT
patch's message names the arm64 failure it fixes. Folding the patches into the fork (and moving the pin) is a separate
maintainer step.

lsteamclient works the same way. Its tree, `build/wine-arm64-src/lsteamclient`, is a sparse, blob-filtered checkout of
Proton's `lsteamclient/` folder at `deps.pins`' `LSTEAMCLIENT_COMMIT` (without the Steamworks SDK folders), linked
into the Wine tree as `dlls/lsteamclient` (ignored there, so the Wine tree stays applied; Wine patch 0016 registers
it). Its series is `deps.pins`' `LSTEAMCLIENT_` lines and `patches/lsteamclient/`: a tarball pin doesn't re-fetch it.
Its source is never committed here, only the patches.

Changing the pins or a patch file makes a tree with no work of its own (no change, commit, stash, other branch or
worktree) start over from the series: it is deleted and fetched again.

## Next Wine rebase

Do these when the Wine pin next moves. The first two rewrite patch files and change the series hash, so the trees
re-clone; that is why they wait for the rebase.
- Fold patches 0017, 0018 and 0019 into 0004, so strict x18 is one patch again (0017 exists only because an in-place
  rewrite of 0004 was refused during sub-project 3: `docs/testing/acceptance-arm64-ship-base.md`, "Pins and
  patches"). Run `check.sh x18` on the result.
- Add `--no-signature` to `export.sh`'s `git format-patch`. Every patch now ends with the exporting git's version
  (`2.54.0 (Apple Git-157)`), so an export after a git upgrade rewrites every patch file.
- Re-check the x18 transitions in `signal_arm64.c` against upstream's changes there (ship-base spec §13).
- Re-check the toggle layout that patch 0019 checks (`brk #1` at `os_set_custom_x18_abi_enabled` + 0x58 and + 0x78)
  on the macOS in use. When it doesn't match, the `ERR` line comes from `signal_init_process`, so it prints from every
  Wine process; once per run would be enough.
- Re-check DXMT patch 0002 (`winemetal.so` loads the MetalFX presenter, `libmacneutron-present.dylib`, in a constructor)
  against DXMT's `winemetal` sources.
- Re-check makedep's `output_module` against upstream (patch 0020): a module enabled for `arm64ec` but not `aarch64`
  (`vcruntime140_1`, `dpnsvr`) must still link its `.res`. `bundle.sh` fails naming any module whose `Makefile.in`
  sets `VER_` and that ships without a version resource.
- Keep patch 0014 even if it looks unneeded: a missing dylib then falls back silently instead of crashing
  (`docs/research/2026-10-04-ship-base/brief.md`).
- bundle.sh's prefer_native_check fails if one of lib.sh's CRT_BUILTINS (26 CRT and DirectX redistributables; the
  families' other versions are not checked) prefers native, ntdll reads another prefer-native bit, or
  version_heuristics stops sending Microsoft DLLs to LO_DEFAULT: read upstream's reason before changing CRT_BUILTINS.
  msvcp60, msvcm80 and msvcm90 prefer native upstream (0x170) and aren't listed.

## Licences

- **Wine** is LGPL-2.1+; our patches to it are too.
  - 0006: citi94's `citi94/wine-macos-arm64` commit `4a50ce17c8` ("ntdll: Handle macOS Apple Silicon W^X ..."), applied
    with `git am`, author kept. Its message has no `Source:` line; this is where it comes from
    (`docs/research/2026-10-02-native-arm64/entitled-trial.md`).
  - 0009: adapted from Madeira's LGPL Wine commits `ac650deca3` and `d88d55eee0` (branch `madeira-lgpl`).
  - 0013: adapted from CodeWeavers' `dlls/winemac.drv/d3dmetal.c` (Brendan Shanks, LGPL-2.1+) and `d3dmetal_objc.m`,
    as published in `athei/wine` branch `cx-26-patched`, and `dappermint/winecx` `713015fa9f`, `13e6a88a02`,
    `565f6386b7` (LGPL).
  - 0015: msync, by Zebediah Figura and Marc-Aurel Zent (LGPL-2.1+): CodeWeavers' CrossOver 26.3 msync as carried on
    `dappermint/winecx` branch `cx/wine1117` at `e0aa380780`, with millia ampora's msync commits there (`8df1826853`,
    `9be392b3b4`, `3a7a712d66`, `307f90fdb1`, `620d8c542f`, `a7ef7b3b01`, `ef72fdb55b`, `6d316146c2`), merged onto
    Wine 11.19. The patch's message lists our changes to it.
  - 0004, 0017, 0018 and 0019 (strict x18 toggling) are ours, following Apple's rule in `os/arch/arm64.h` and our
    design in `docs/research/2026-10-02-native-arm64/x18-boundaries.md`.
  - 0016 (registering `dlls/lsteamclient` in configure) and the other Wine patches are ours.
  - 0020 (makedep links the resources of a module built for the hybrid arch only, which lost its version resource)
    is ours and stays local: it isn't proposed upstream.
  - 0022 (`dlls/libxess` and `dlls/libxess_dx11`, stand-ins for Intel's XeSS libraries: the same exports, every
    device reported unsupported) is ours and stays local. The launcher loads them unless `MACNEUTRON_XESS=1`.
  - 0023 (msync's shm pages mapped from a zero hint; a failed mapping ends the wineserver or the process with a
    message instead of being used) is ours and stays local.
  - 0024 (`dlls/libxess`: XeSS on D3D12 answered by MetalFX's temporal upscaler through DXMT's private interface,
    DXMT patches 0004-0005) is ours and stays local. Its `xess.h` copies declarations from Intel's public headers
    (github.com/intel/xess, branch `main`, MIT since commit de0fb9c) with Intel's MIT notice.
  - 0025 (`dlls/libxess`: notes on the bridge's lock and its conventions) is ours and stays local.
  - 0026 (`dlls/libxess`: the lock note reworded) is ours and stays local.
  - 0027 (`dlls/libxess`: a far-plane depth for MetalFX when the game passes none, as Unreal does) is ours and
    stays local.
  - 0028 (`dlls/libxess`: the MetalFX upscaler made at the first Execute, not at Init; a refused Execute records
    nothing) is ours and stays local.
  - 0029 (`dlls/libxess`: refused Executes missing a texture Init's flags ask for, NOT_IMPLEMENTED for the calls
    MetalFX has nothing for, native AA at an odd output size, the logging callback's ERROR level, the pipeline
    build status after Init) is ours and stays local.
  - 0031 (msync's wait-all: every object type wakes all its waiters, as CrossOver 26.3 does, so a wait-all leg that
    only looks at an object can't take a single waiter's wake; the rollback puts back only what it took, a mutex taken
    abandoned as abandoned, and wakes its waiters; a repeated object answers `STATUS_INVALID_PARAMETER`; an abandoned
    mutex no longer spins; the shm reply port's receive right is released; the abandon store is SEQ_CST) is ours and
    stays local.
  - 0032 (`include/winnt.h`: ReadAcquire and WriteRelease are acquire loads and release stores in ARM64EC code too, not
    x86's plain loads and stores) is ours and stays local.
  - 0033 (critical sections on Windows' 2003 SP1+ LockCount encoding: a contender takes a free section even while others wait, and a Leave wakes one waiter only when no wake is in flight, so contended sections no longer convoy; one ldclral to enter and one ldsetal to leave on ARM64 and ARM64EC, lock btr and lock xadd on x86 (compiled, not run here); an explicit SpinCount spins through one slot, in ns on ARM, at most 10 µs; a recursive Enter no longer counts in LockCount) is ours and stays local.
  - 0034 (the ARM64EC auxiliary IAT filled at load, and an entry put back when a page it was resolved through is made writable) is ours and stays local.
  - 0035 (msvcrt's memmove, strlen, strnlen, strchr, strrchr, strcmp and memchr on ARM64 and ARM64EC from Arm Optimized Routines v26.07, MIT: licenses/NOTICES.md) is ours and stays local.
- **FreeType** (2.14.3) is used under the FreeType License (FTL); the bundle carries its credit in
  `licenses/README` and its texts in `licenses/freetype/`. **gnutls** (3.8.13, with its included libtasn1) is
  LGPL-2.1+ and its included libunistring LGPL-3+; **nettle** (4.0) and **GMP** (6.3.0), linked into
  `libgnutls.30.dylib`, are taken under LGPL-3+. Their texts come from the tarballs into `licenses/gnutls/`,
  `licenses/nettle/` and `licenses/gmp/`; their sources are the pinned tarballs (`deps.pins`, unmodified).
- **FFmpeg** (8.1.3) is LGPL-2.1+, built without its GPL and nonfree parts; its texts come from the tarball into
  `licenses/ffmpeg/`, its notice and the Independent JPEG Group's credit are in `licenses/NOTICES.md`, and its source
  is the pinned tarball (`deps.pins`, unmodified).
- **lsteamclient** is Steamworks-SDK-derived: Valve's Steamworks SDK licence (its `LICENSE`), except `cxx.h`, which is
  LGPL-2.1+ (CodeWeavers, from Wine); the bundle carries both in `licenses/lsteamclient/` (`LICENSE`, `NOTE`). Its
  patches are dappermint/winecx's three Mac fixes by millia ampora (`8d188ec0db`, `dada36ebab`, `6cfbd169a5`), each
  naming its source commit, author kept. It ships in the release bundle by the maintainer's decision of 2026-10-04, under Valve's
  Steamworks SDK licence. This is not legal advice.
- **FEX** is MIT, and so are our patches to it.
  - 0001: the macOS unixlib helpers, from dappermint's FEX fork, commit `4efc3abc8a`. MIT: the file it patches,
    `Source/Windows/UnixLib/FEXUnixLib.cpp`, keeps its `SPDX-License-Identifier: MIT` header, and the fork carries
    FEX's MIT `LICENSE` at that commit.
  - 0003: Madeira (`willfaust/FEX`, branch `ios-port-2607`) `fdf361f0e`, applied unchanged.
  - 0004: the CASPAL part of Madeira's `ceabf254a`, ported to our pin.
  - 0005: the dual-view JIT memory, derived from Madeira's dual-map commits `fce78cefd`, `61f11e3cc` and `6084de076`
    (the others it lists, `83e12849f`, `87b40c220` and `db4f32768`, are named there as not taken).
  - 0002 is ours.
  - 0006 (volatile-metadata ranges, clamped to their image, run every block they cover without TSO; listed GPR accesses
    and MonoHacks-flagged accesses keep it; vector accesses follow VectorTSOEnabled; x87 FIST/FISTP/FISTTP stores follow
    VectorTSOEnabled; MOVS/STOS, with or without REP, follow MemcpySetTSOEnabled; in EVMD only a bare module name covers
    a whole module; the VolatileMetadata switch is read again; a coverage line per image) is ours and stays local.
  - Madeira's commits we use are dated before 2026-08-28. Its `LICENSE-MADEIRA.md` says modifications published before
    then were granted under MIT, irrevocably. A Madeira commit published on or after that date would be GPL-3, so check
    the date before importing another. This is not legal advice.
- Each patch taken or derived from another tree names its source in its message (Wine 0006's is given above, since its
  message doesn't). Patch files keep their original authors.
- **DXMT** is LGPL-2.1+; our patches to it are too. 0001-0043 are ours.
- Upstream FEX and DXMT refuse AI-authored contributions: no patches go upstream (issue reports only).
