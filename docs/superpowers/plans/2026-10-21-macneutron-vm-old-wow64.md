# MacNeutron old WoW64 in the VM (M3, M4) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 32-bit game in the VM runs with a 32-bit Unix side (Wine's `i386-unix`), as Proton runs it by default, with
Steam and Direct3D on Metal there; new WoW64 stays opt-in.

**Architecture:** A second guest Wine build (i686, `--with-wine64`) over a multilib sysroot is installed beside the x86-64
one; Wine's loader then runs 32-bit images with the i386 loader unless `WINEARCH=wow64`. MacNeutron's own Unix modules
(lsteamclient's forwarder, the guest `winemetal.so`, `mtld3d.so`) get i686 builds that speak the 32-bit layouts their
callers already use under new WoW64.

**Tech Stack:** the Fedora builder (clang, lld, Fedora 44 i686 packages), FEX's x32 layer in the guest, Rust 1.99.0
`i686-unknown-linux-gnu`.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-vm-old-wow64-design.md`

## Global Constraints

- New WoW64 keeps working: `MACNEUTRON_VM_WOW64=1` or `PROTON_USE_WOW64=1` gives today's behaviour.
- The macOS path is untouched (no i386 Unix side there).
- Every 32-bit Unix module's parameter structs match its PE side's layout (size and offsets), checked by a build step.
- Downloads approved by the maintainer (2026-10-21): Fedora's i686 packages, the Rust i686 Linux target. No others.
- One VM run at a time; the builder starts with the `out`/`outp` shares; signing env exported for signing builds.
- Commits: repo `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; DXMT fork `Assisted-by: Claude Code: claude-opus-5-5`.

## Review Focus

1. A 32-bit process's memory for the transport (blobs, rings) and for no-copy buffers is below 4 GiB in its own
   address space; a 32-bit Unix module never truncates a 64-bit handle.
2. Layout drift between an i386 PE side (MSVC ABI: 8-byte `uint64_t`) and an i686 Linux side (4-byte): every struct
   with a 64-bit field after a 32-bit one, and every struct whose size isn't a multiple of 8.
3. A 64-bit prefix (no `syswow64` content of the 32-bit build) used by an old-WoW64 32-bit program: wineboot updates it.
4. `WINEARCH=wow64` set by the user while `i386-unix` exists: new WoW64, as today, including D3D and Steam.
5. Sound in a 32-bit process: the i686 ALSA plugin and `libasound` found by the 32-bit loader, not the 64-bit ones.

---

### Task 1: the spike — a 32-bit Unix side runs in the guest

**Files:** Create `eac-vm/sysroot/packages-i686.txt`, `eac-vm/sysroot/packages-multilib.lock`,
`eac-vm/sysroot/cc-i686.sh`, `eac-vm/tests/old-wow64.sh`; Modify `eac-vm/sysroot/build.sh` (MULTILIB=1),
`eac-vm/wine/build.sh` (`wine32`; `install` with `WINE32=1`)

- [ ] **Step 1:** the multilib sysroot (`MULTILIB=1 sh mn-sysroot/build.sh` in the builder) and the 32-bit build
  (`build.sh wine32`). Expected: `i386-unix/ntdll.so` is i386 ELF; `i386-unix/wine` and `wine-preloader` exist.
- [ ] **Step 2:** `metal-probe32` (the transport's probe for i686) and `old-wow64.sh`; an image from a stage with both
  builds (`WINE32=1`). Expected: rows 1-5 ok (i386-unix present, cmd32 x86, no wow64.dll under old WoW64, wow64.dll with
  `WINEARCH=wow64`, every probe step from a 32-bit process). A FAIL in rows 2-5 stops the plan with a report.
- [ ] **Step 3:** commit.

### Task 2: old WoW64 by default in the image and the launch

**Files:** Modify `eac-vm/wine/build.sh` (32-bit install by default), `eac-vm/image/guest-stages.sh` (the `wine` stage
builds `wine32`), `eac-vm/guest/run-game.sh` (the mode), `Sources/MacNeutronCore/MicroVM.swift` (pass
`MACNEUTRON_VM_WOW64`), `eac-vm/image/stamps.sh` if the stamp names builds
- Test: `Tests/MacNeutronCoreTests` (the variable reaches the guest script), `eac-vm/tests/old-wow64.sh`, `wow64.sh`

- [ ] **Step 1:** test that `MicroVM`'s guest script carries `MACNEUTRON_VM_WOW64` and `PROTON_USE_WOW64` when set
  (RED), then the change (GREEN).
- [ ] **Step 2:** `run-game.sh`: `WINEARCH=wow64` when either is 1. Image rebuilt; `wine.erofs` size measured.
- [ ] **Step 3:** run `old-wow64.sh` and `wow64.sh` (the latter with `MACNEUTRON_VM_WOW64=1` semantics: its rows are new
  WoW64's) in the guest. Expected: all rows ok.
- [ ] **Step 4:** commit.

### Task 3: sound for the 32-bit Unix side

**Files:** Modify the guest audio plugin's build (the ALSA plugin over vsock: its i686 build), `eac-vm/image/build.sh`
(`lib/wine/mn-i386`: i686 `libasound.so.2` from the multilib sysroot and the plugin), `run-game.sh` (the 32-bit
loader's library path)
- [ ] **Step 1:** a 32-bit program opening the default ALSA device in the guest (`eac-vm/tests/audio.sh`'s 32-bit row,
  RED before), then the build and image change (GREEN). Commit.

### Task 4: Steam for the 32-bit Unix side

**Files:** Modify `eac-vm/steam-forward` (an i686 build whose `__wine_unix_call_funcs` are the WoW64 entry points),
`eac-vm/image/guest-stages.sh` (the `forwarder` stage builds both), `eac-vm/image/build.sh` (`i386-unix/lsteamclient.so`)
- [ ] **Step 1:** layout check of the forwarder's 32-bit params (i686 Linux against i686 PE). Then the build.
- [ ] **Step 2:** the 32-bit Steam probe (app 480) under old WoW64 and under `WINEARCH=wow64`. Commit.

### Task 5: Metal for the 32-bit Unix side

**Files:** Create `eac-vm/metal/layout32-check.sh` (clang `-fdump-record-layouts` of winemetal's and mnd3d's structs on
i686-w64-mingw32 and i686-redhat-linux, sizes and offsets compared); Modify `eac-vm/metal/build-guest.sh` (an i686
`winemetal.so`, its 64-bit table compiled with the 32-bit PE layouts), `dxmt/mtld3d-linux-cargo.sh` (the target as a
parameter, the multilib sysroot), `dxmt/mtld3d-build.sh --guest` (an i686 `mtld3d.so`), `eac-vm/dxmt/stage.sh`,
`eac-vm/image/build.sh` (`i386-unix`), mtld3d's params if their layouts differ
- [ ] **Step 1:** the layout check (RED: the i686 Linux compile of `winemetal_thunks.h` differs, e.g.
  `unixcall_mtltexture_newtextureview`), then the build that matches (GREEN), and the same for mtld3d's params.
- [ ] **Step 2:** the i686 builds, staged and imaged. Commit.

### Task 6: the gates under old WoW64

- [ ] **Step 1:** in the guest under old WoW64: `d3d-smoke.sh`'s 32-bit rows, `eac-vm/dxmt/d3d9-gate.sh` (triangle32 and
  the x86 conformance lane), the 32-bit Steam probe; and under `MACNEUTRON_VM_WOW64=1` the same as before. Regression:
  `eac-vm/dxmt/gate.sh`, `metal-wm.sh`, `wow64.sh`, the launcher's tests.
- [ ] **Step 2:** commit. Held for the maintainer: Rogue Company's EAC (`easyanticheat_x86.so` mapped, EAC's verdict) and a
  32-bit D3D9 game.
