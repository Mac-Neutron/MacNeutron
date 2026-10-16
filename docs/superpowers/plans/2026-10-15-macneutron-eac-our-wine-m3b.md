# EAC microVM, milestone 3b: our Wine for x86-64 Linux Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MacNeutron's Wine, with Proton's EAC loader changes and the Steam forwarder as its lsteamclient Unix half,
runs in the microVM under FEX, and the Steam bridge's probe reports Steam initialised and a 234-byte auth ticket
from the Mac's Steam.

**Architecture:** A Linux clone of our Wine tree carries Proton 11's EAC commits as a small series
(`eac-vm/wine-patches/`). The builder builds Wine's tools natively, then cross-builds Wine for `x86_64-linux-gnu`
against the pinned x86-64 sysroot (grown with Wine's `-devel` packages) and for `x86_64-windows` with clang and lld.
The install becomes a fourth read-only image, `wine.erofs`, mounted at `/opt/mn-wine`. The forwarder replaces the
installed `lsteamclient.so` and replays the host's registry writes into the prefix through the guest's ntdll. A
headless Xvfb gives Wine a display.

**Tech Stack:** Wine (C, autotools), clang/lld cross-compiling, Fedora 44 packages, EROFS, the 3a forwarder.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.5, §5.9, §8 milestone 3b.

## Global Constraints

- Worktree `.claude/worktrees/upstream-master`; commit locally, never push.
- The Wine source is `build/wine-arm64-src/wine` at its current HEAD (`b378b8e7`, our macOS series on upstream
  master). Never check out another branch there. Linux work happens in a clone, `build/eac-vm-work/wine-linux`.
- Proton's commits are ported with their authorship and a `Proton:` line naming the original commit.
- Every package the build or the image uses is pinned (cache, signature check, lock), as `eac-vm/image` and
  `eac-vm/sysroot` already do.
- DXMT's DLLs are not part of this milestone: without the Metal transport (milestone 4) they can't render. Rendering
  in 3c is software.
- Steam tests use app 480 with the probe's redaction; Mac Steam must already be running.
- Gates assert. Commit messages end with the two trailer lines this session's system reminder gives.

## Review Focus

1. **A macOS-only patch that breaks or changes the Linux build** (msync, x18, W^X, winemac, libxess). The Linux
   build must not compile them in silently changed form: each is either inert on Linux or disabled at configure time,
   listed in Task 3's notes.
2. **The forwarder without a host** (the VM started without `--steam-appid`). Steam calls fail with an error status,
   and Wine keeps running. Test in Task 5.
3. **Registry writes replayed into the wrong hive or a missing key path.** Values land under the prefix's
   `HKCU\Software\Valve\Steam`. Test in Task 5 (`reg query`).
4. **A 32-bit caller** (WoW64). Refused with an error, never forwarded with the wrong layout. Test in Task 5.

---

### Task 1: Proton's EAC changes on our Wine

**Files:** Create `eac-vm/wine-patches/BASE` (the base commit), `eac-vm/wine-patches/000N-*.patch`.

- [ ] **Step 1:** Clone (`git clone --local --branch macneutron build/wine-arm64-src/wine build/eac-vm-work/wine-linux`),
  branch `eac`. Cherry-pick by patch file from `ValveSoftware/wine` `proton_11.0`: `94b768eae185` (EAC runtime DLL path),
  `cc9a65cb0cfc` (bridge load order), `ad5919f49039` (launcher directory), `9b70c83c6b02` (launcher locale),
  `16cae6c395d2` (DNS workaround). Resolve conflicts against upstream master by hand; note each in the commit.
- [ ] **Step 2:** `git diff --check` clean; export with `format-patch --no-signature`; commit.

### Task 2: Wine's build dependencies in the sysroot, Xvfb in the image

**Files:** Modify `eac-vm/sysroot/packages.txt` (freetype-devel, gnutls-devel, libX11-devel, libXext-devel,
libXrandr-devel, libXi-devel, libXcursor-devel, libXfixes-devel, libXinerama-devel, libXcomposite-devel,
libXrender-devel, mesa-libGL-devel, alsa-lib-devel, fontconfig-devel), `eac-vm/sysroot/packages.lock`;
`eac-vm/image/packages.txt` (xorg-x11-server-Xvfb), `eac-vm/image/packages.lock`.

- [ ] **Step 1:** Rebuild both with `UPDATE_LOCK=1`, then again without: the locks hold.
- [ ] **Step 2:** List which of Wine's runtime libraries the guest's FEX RootFS lacks (`ldd`-equivalent over the built
  Wine in Task 3); record the answer for Task 4.
- [ ] **Step 3:** Commit.

### Task 3: Wine for x86-64 Linux

**Files:** Create `eac-vm/wine/build.sh` (builder: sync from the clone without `.git`; native tools with
`make __tooldeps__`; `configure --host=x86_64-linux-gnu --with-wine-tools=… --enable-archs=x86_64
--prefix=/opt/mn-wine --disable-tests` with the sysroot's `CC` and `PKG_CONFIG_LIBDIR`, `x86_64_CC="clang -target
x86_64-windows"`; `make -j`; `make install DESTDIR=…`), `eac-vm/wine/notes.md` (what each macOS patch does on Linux).

- [ ] **Step 1 (RED):** In the microVM, `/opt/mn-wine/bin/wine --version` fails (no image yet).
- [ ] **Step 2:** Build. Expected: configure reports the X11, FreeType, GnuTLS, OpenGL and ALSA support as found;
  `make` succeeds; `notes.md` lists each macOS patch as inert or disabled (Review Focus 1).
- [ ] **Step 3:** Commit.

### Task 4: `wine.erofs` in the guest

**Files:** Modify `eac-vm/image/build.sh` (`wine.erofs`, label `mn-wine`, from Task 3's install, plus any runtime
library Task 2 found missing, pinned), `eac-vm/image/rootfs/etc/fstab` (`/opt/mn-wine`),
`eac-vm/host/macneutron-vm.swift` (fourth disk); create `eac-vm/tests/wine.sh`.

- [ ] **Step 1:** `tests/wine.sh` (asserting): `wine --version` names our tree; under `Xvfb :0`, `wineboot -i` in a
  fresh prefix on `/home` completes, and `wine cmd /c echo hi` prints `hi`.
- [ ] **Step 2:** Commit, with the image size and the first-boot `wineboot` time.

### Task 5: The forwarder as Wine's lsteamclient Unix half, and the probe

**Files:** Create `eac-vm/steam-forward/guest.c` (constructor: `mn_client_init(mn_vsock_connect, replay, NULL)`;
`replay` writes each value under the prefix's HKCU through `NtCreateKey`/`NtSetValueKey`), extend
`eac-vm/steam-forward/build-guest.sh` (`lsteamclient.so` linked against the built `ntdll.so`); modify
`eac-vm/image/build.sh` (install it over Wine's `x86_64-unix/lsteamclient.so`); build `steam.exe` and
`steamprobe.exe` for x86-64 with our llvm-mingw on the Mac; create `eac-vm/tests/probe.sh`.

- [ ] **Step 1 (RED):** `tests/probe.sh` (prefix set up as MacNeutron's launcher does: lsteamclient as
  `C:\Program Files (x86)\Steam\steamclient64.dll`, `steam.exe`; the probe with a game's `steam_api64.dll` from the
  shared library) without `--steam-appid`: Steam init fails with an error status, Wine keeps running (Review Focus 2).
- [ ] **Step 2 (GREEN):** With `--steam-appid 480`: `init ok`, `steamid ok`, `auth ticket 234 bytes`;
  `reg query HKCU\Software\Valve\Steam /v language` finds the replayed value (Review Focus 3); a 32-bit call is refused
  (Review Focus 4).
- [ ] **Step 3:** Results in the research README §5; commit; whole-plan review by one fresh reviewer.
