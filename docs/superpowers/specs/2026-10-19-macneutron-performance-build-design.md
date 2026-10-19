# MacNeutron's performance build (X.Y.Zp)

Status: design approved in conversation 2026-10-19; this spec awaits the maintainer's review.
First release: 0.7.1p, from the 0.7.1 sources plus this work.

## 1. Goal

A second build of each release, `X.Y.Zp`, that players can pick for speed. It runs the same games as `X.Y.Z` with
as much CPU-side performance as builds, runtime settings and profile-guided optimization give, and with logging,
dumping and debug information off.

Done when:
- `make release VERSION=0.7.1 PROFILE=p` builds `build/release/0.7.1p/` from one tree, without touching the normal
  build's folders.
- 0.7.1p is faster than 0.7.1 in SMITE 2 practice (fps, or the same fps for less CPU), each kept change measured.
- Without `MACNEUTRON_LOG`, a game run writes no game log, no guest console and no DXMT logs, stats or dumps.
- With `MACNEUTRON_LOG=1`, the player still gets a game log for a bug report.
- The gates pass (§8) and 0.7.1p is published next to 0.7.1, which stays GitHub's "Latest".

## 2. Decisions (maintainer, 2026-10-19)

| Question | Decision |
|---|---|
| What the p build is | A public alternative build next to the normal one. Players install one or the other. |
| Diagnostics | Off by default, trace code compiled out. `MACNEUTRON_LOG=1` still gives a game log. |
| How much risk | Everything: build flags, runtime tuning, and PGO. Each change measured; anything not faster is dropped. |
| PGO's timing | In 0.7.1p itself: nothing is published until PGO is in. |
| GitHub "Latest" | 0.7.1 stays Latest; 0.7.1p is listed as the opt-in performance build. |
| How the p build is made | A build profile (`MACNEUTRON_PROFILE=p`) on the same commit, not a separate branch. |
| LTO | ThinLTO wherever the toolchain allows it, never full LTO. |

Assumptions written back and not corrected:
- Releases come as pairs from now on (X.Y.Z and X.Y.Zp, one commit). 0.7.1 stays as published.
- The performance work targets what games run on: the VM path (guest FEX, guest Wine, the VM host, native DXMT,
  libkrun). The Mac-side Wine runs only games outside a Steam library; it gets §4's diagnostics changes only.
- Benches: SMITE 2 practice, 2 minutes, run once each (the maintainer's rule), on an otherwise idle Mac.
- The host's frame counter (`MN_PRESENT_STATS`) stays behind its environment variable in the p build: off unless
  set, it writes nothing, and without it nothing could measure the p build.

## 3. The profile, its folders and its version

**One switch.** `make release VERSION=X.Y.Z PROFILE=p`, or `release.sh --profile p X.Y.Z`, exports
`MACNEUTRON_PROFILE=p` and `BUILD_DIR=build/p`. Every build script reads `MACNEUTRON_PROFILE`; unset or `std` is
today's behaviour, so the normal line is unchanged by default.

**Folders.** `build/p/` starts as an APFS clone (`cp -c -R`) of `build/`'s applied source trees (`wine-arm64-src`,
`dxmt-src`, `llvm-mingw-src` and the source folders in `eac-vm-work`): no space, no download. Every p object, stage
and image lives under it. A p build never writes `build/wine-arm64`, the dev apps or `build/eac-vm-work/images`.

**The builder.** `eac-vm/image/guest-stages.sh`, `eac-vm/wine/*.sh`, `eac-vm/dxmt/*.sh`, `eac-vm/metal/build-guest.sh`
and `eac-vm/image/build.sh` name builder folders (`~/wine-build64`, `~/wine-stage`, `~/mn-steam-out`,
`~/mn-dxmt-guest`, `~/mn-d3d-guest`, `~/fex-linux`, `/var/tmp/mn-image`, ...). With the p profile each lives under
`~/p/` instead. The image build writes `build/p/eac-vm-work/images` through the builder's `out` share, which the p
build starts the builder with.

**Version.**
- `release.sh` accepts `X.Y.Zp` when `vX.Y.Z` is already a tag, and refuses an existing `vX.Y.Zp` (locally or on
  origin). The `X.Y.Z` form is unchanged.
- The app's `CFBundleShortVersionString` and `CFBundleVersion` are `X.Y.Zp`. Its About and Settings show
  "X.Y.Zp (performance build)".
- Output: `build/release/X.Y.Zp/` with `MacNeutron-X.Y.Zp.zip`, `MacNeutron-X.Y.Zp-source.tar.gz`,
  `MacNeutron-X.Y.Zp-vm-parts.tar.xz` and `SHA256SUMS`. The VM manifest points at `vX.Y.Zp`'s parts.
- The tag `vX.Y.Zp` names the commit the p build was made from (newer than `vX.Y.Z` for 0.7.1p: it adds this work).
  Its source archive carries everything the build used, PGO profiles included (§7), so R5 and the LGPL obligations
  hold as for any release.

**Switching lines.** Both are `MacNeutron.app` with one bundle ID. A player who switches gets one VM rebuild (the
manifest hash differs); per-game state disks are kept.

## 4. Diagnostics

**Compiled out of the p build:**
- Wine, on the Mac and in the guest: `-DWINE_NO_TRACE_MSGS`. Every `TRACE` is gone. `WARN`, `FIXME` and `ERR` stay
  compiled in for `MACNEUTRON_LOG=1`. `MACNEUTRON_LOG=2`'s extra channels (`+loaddll`, `+steamclient`) are traces, so
  in the p build it logs what `=1` logs; the release notes say so.
- DXMT (the Mac build and native DXMT on the VM host): a p-only define makes `Logger::trace`, `debug` and `info`
  no-ops, and removes the capture code: `DXMT_STATS`, `DXMT_DXIL_DUMP`, the F9 pass dump and `MACNEUTRON_PERF`'s
  stats folder in the launcher. Warnings and errors stay, written only when `MACNEUTRON_LOG` is set. These are new
  `#ifdef` guards in the fork's patch series.
- libkrun: the `log` crate's `release_max_level_error` feature.
- Debug information: no `-g` in any p compile (Mac Wine, guest Wine, FEX, DXMT, libkrun, the VM host), no dSYMs,
  and full stripping of the shipped Mach-Os and ELF files.

**Off at run time unless `MACNEUTRON_LOG` is set:**
- The guest console goes to `/dev/null` instead of `runs/<appid>/console.log`. The guest kernel boots with `quiet
  loglevel=0` (in the p images' `cmdline.txt`).
- `traces/` holds only what the host needs: `ready`, `capture`, and `frames.txt` when `MN_PRESENT_STATS` is set.
  `DXMT_LOG_PATH` is set only when logging.
- The guest's `run-game.sh` writes no `mn-input.log`. The launcher doesn't pass `MACNEUTRON_VM_PROFILE` or
  `DXMT_STATS` into the guest.
- `launcher.log` gets one line per launch (game, verb, VM or macOS, exit status); its notes and the full lines only
  with `MACNEUTRON_LOG`. `vm-setup.log` is written only when a setup fails.

**Kept:** `MACNEUTRON_LOG=1` gives `steam-<appid>.log` (Wine's warnings and errors, the guest console, DXMT's
warnings and errors) and the full launcher lines. The menu's "Show logs" stays.

## 5. Build flags

| Component | Today | p build |
|---|---|---|
| Guest Wine (x86-64 under FEX) | configure's `-g -O2`, generic x86-64 | `-O3 -march=x86-64-v2`, no `-g`, ThinLTO on the Unix side. v2, not v3: the M5 has no SVE, so FEX emulates AVX with paired NEON operations, while v2's SSE4.2 and POPCNT translate one to one. |
| Guest FEX (aarch64 on the M5 vCPU) | Release, `ENABLE_LTO=ON`, `TUNE_CPU=none` | Tuned for the features the guest sees: read once from the guest's `/proc/cpuinfo` and pinned in `build-fex.sh` as an explicit `-march=armv8.x+...` (HVF may hide some of the host's). LTO checked to be thin (CMake's IPO uses `-flto=thin` with clang; forced if not). |
| VM host code (Metal executor, winemetal handler, D3D forwarding, input, audio) | `-O2` | `-O3 -mcpu=apple-m5`, ThinLTO across the C, C++ and Objective-C objects. Swift stays `-O`, whole-module. libtpms is unchanged (not hot). |
| libkrun | cargo release | `lto = "thin"`, `codegen-units = 1`, `-C target-cpu=apple-m5`. Panics keep unwinding. |
| Native DXMT (VM host) and Mac DXMT | meson `release`, `-mcpu` | `b_lto=true`, `b_lto_mode=thin`. |
| Mac Wine, Mac-side FEX | `-O3`, ThinLTO, `-mcpu=apple-m5` | Unchanged but for `-g` (§4). |
| Guest kernel | Fedora's signed kernel | Unchanged (EAC v2 needs a well-known signature); only its command line (§4, §6). |

Each row is benched in this order, each on top of the rows kept before it; a row that isn't faster reverts to
today's flags.

## 6. Runtime tuning

Candidates, tried in this order. Each is kept only if faster and if the gates still pass:

1. Guest kernel command line (never `lockdown=integrity`, `module.sig_enforce=1` or the kernel itself, which EAC v2
   depends on): `mitigations=off`; `init_on_alloc=0` (Fedora zeroes every page it hands out by default);
   `transparent_hugepage=always`; `nowatchdog`, `audit=0`; `preempt=voluntary`.
2. Guest services: systemd units the game VM doesn't need are masked in the p image (timers, the journal's disk
   writes, anything polling), so fewer wake-ups reach the vCPUs.
3. FEX settings in the p image's `Config.json` (`FEX_MULTIBLOCK=0` stays: it is needed for correctness): x87 reduced
   precision; the `mman` self-modifying-code mode instead of tracking; with the vCPU already in hardware TSO, FEX's
   own TSO fallbacks off; a larger maximum block size.
4. VM host: the ring's spin times (host consumer 2 ms, guest 50 µs) longer, trading CPU power for fewer parks (a
   park costs 26-48 µs, a spin 0.32 µs); vCPU and render threads at the user-interactive QoS class if they aren't.
5. Guest Wine: `winemenubuilder` and the other prefix-setup helpers off in game prefixes (faster start).

Security trade-offs (`mitigations=off`, `init_on_alloc=0`) apply only inside the game VM, which runs nothing but the
game, and are listed in the release notes. Changes that alter what games compute (x87 precision, the SMC mode) are
also checked on Palworld, which must start and reach its title screen.

## 7. PGO

**Components.**

| Component | How |
|---|---|
| Native DXMT on the VM host, airconv included | meson `b_pgo=generate`, then `use` (clang IR PGO) |
| VM host C, C++ and Objective-C | `-fprofile-generate`, then `-fprofile-use` |
| libkrun | rustc `-Cprofile-generate`, then `-Cprofile-use`; rustup's `llvm-tools` for `llvm-profdata` |
| Guest FEX | clang PGO in the builder, the instrumented FEX running in the VM |
| Guest Wine, Unix side (`ntdll.so`, `win32u.so`, `winex11.so`, ...) | clang PGO, running under FEX |

Not profiled: the guest Wine's PE side (its DLLs are built without a C runtime; linking LLVM's profile runtime into
them is fragile, so they get §5's flags only), the Mac Wine (off the hot path), the guest kernel (signed).

**Recording.** Instrumented binaries write in LLVM's continuous mode (`%c` in `LLVM_PROFILE_FILE`; on Linux with
`-mllvm -runtime-counter-relocation`): the counters live in a file mapped to disk, so a process killed at the end of
a run still leaves a whole profile. Guest profiles go to the run's share (`/mnt/share/pgo/`), host profiles to a
folder beside it; `llvm-profdata merge` makes one `.profdata` per component. Training runs, once each:
1. SMITE 2 practice, 2560x1440, forwarded D3D, from an empty shader cache (shader translation profiled), 2 minutes.
2. The same with a warm cache.
3. Palworld from launch to its title screen, then 2 minutes there.

**Keeping them.** `pgo/<component>.profdata` in the repository, each with `pgo/<component>.stamp` naming the commit
and trees it was recorded from. The p build uses them; `release.sh --profile p` refuses to run without them; the
source archive carries them. Profiles from older sources still apply (changed functions lose their optimization;
clang's `-Wprofile-instr-out-of-date` is silenced); a component's profile is re-recorded when it changes
substantially. Every p compile uses `-ffile-prefix-map` (and `-fprofile-prefix-map` where clang has it) so the
profiles name repository-relative paths, and the release's check for build and home paths covers `pgo/`.

**Measuring.** PGO is benched once against §5 and §6's build, kept per component only if faster.

## 8. Testing

**`release.sh --self-test`, new refusals:** a version that is neither `X.Y.Z` nor `X.Y.Zp`; `X.Y.Zp` without the
tag `vX.Y.Z`; an existing `vX.Y.Zp`; a p build without its profiles; profiles naming a home or build path.

**On the built p release (`release.sh`, p profile only):**
- Traces compiled out: a known Wine `TRACE` format string and a DXMT `debug`/`info` string are absent from the p
  binaries (the guest's and the Mac's `ntdll.so`, native DXMT) and present in a normal build's.
- No debug information: no `__DWARF` segment or `.debug_*` section and no dSYM in the app or the VM parts.
- Isolation: `build/wine-arm64`, `build/eac-vm-work/images` and the builder's normal folders have the same stamps
  before and after the p build.

**Diagnostics, one VM run each:** without `MACNEUTRON_LOG`, no `steam-<appid>.log`, no `console.log`, `traces/` with
only `ready` and `capture`, one new `launcher.log` line. With `MACNEUTRON_LOG=1`, a game log with Wine's warnings,
the guest console and DXMT's warnings and errors.

**Gates on the final p build:** `eac-vm/tests/wine.sh`, `discard.sh`, `d3d-smoke.sh`, `steam-forward.sh`,
`vm-setup.sh`, `eac.sh` (bootstrapper row; the client row has failed since 2026-10-16 and is reported, not chased),
`make dxmt-tests`, and `make release`'s R2, R3, R4, L6 and R5.

**Benches** (once each, SMITE 2 practice, 2 minutes, idle Mac): 0.7.1 as the baseline; each §5 row; each §6
candidate; PGO per component; finally 0.7.1p against 0.7.1 in SMITE 2 and Palworld (fps, and the CPU time of
`macneutron-vm` and the guest). Every run goes in §10's table.

## 9. Release

1. `make release VERSION=0.7.1 PROFILE=p` builds `build/release/0.7.1p/`.
2. Checkpoint: the maintainer sees the final table (0.7.1 against 0.7.1p, fps and CPU in both games).
3. `git tag v0.7.1p <commit>`, push the tag, `gh release create v0.7.1p --latest=false` with the four assets. The notes
   say: what is faster and by how much; what is off; `MACNEUTRON_LOG=1` for a log (and that `=2` gives the same);
   the security trade-offs inside the VM; that switching between 0.7.1 and 0.7.1p rebuilds the VM once.

Effort: profile and diagnostics about a day; flags and tuning with their benches about a day; PGO two to three days;
gates and release half a day.

## 10. Results

SMITE 2's lobby is capped near 60 fps, so CPU (macneutron-vm's cores, the guest's vCPUs included) decides there.
Single runs vary by up to about 10% (the same build, run twice, gave guest times 2.405 and 2.659).

| Run | Build | fps | CPU (cores) | Kept |
|---|---|---|---|---|
| smite2-std | 0.7.1 | 59.7 | 3.577 | baseline |
| smite2-p | §4 + §5 (all rows) | 59.9 | 3.196 | yes (−10.7%) |
| kernel: mitigations=off / init_on_alloc=0 / transparent_hugepage=always / nowatchdog audit=0 / preempt=voluntary | p + one | 59.3-59.8 | 3.248 / 3.363 / 3.397 / 3.394 / 3.444 | no |
| FEX: X87ReducedPrecision / SMCChecks=mman / MaxInst=10000 | p + one | 59.7-59.8 | 3.217 / 3.471 / 3.249 | no |
| FEX: TSOEnabled=0 | p + env | 59.6 | 3.017 | no: HostTSO (fex patch 0028) already does it; noise |
| services masked + TSOEnabled=0 | p image | 59.9 | 3.296 | no |
| PGO host + guest | p + profiles | 59.6 | 3.259 (host threads 0.402 vs 0.274) | no |
| PGO guest only | p + guest profile | 59.8 | 3.431 | no |
| palworld-p (TSOEnabled=0 env, = p) | p | 119.9 | 1.487 | |
| palworld-p-pgo | p + profiles | 120.0 | 1.423 | no (SMITE 2 slower) |

Final, both builds warm: SMITE 2 0.7.1 3.484 cores at 58.3 fps, 0.7.1p 3.274 at 59.6 fps (−6.0%); Palworld 0.7.1 1.534
at 120.0 fps, 0.7.1p 1.403 at 119.5 fps (−8.5%). With the first SMITE 2 pair (−10.7%), every pair favours the p build.
