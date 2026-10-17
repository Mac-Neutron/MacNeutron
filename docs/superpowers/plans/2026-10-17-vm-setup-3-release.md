# VM setup, plan 3: releases carry the VM

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `make release` produces the VM parts archive and the app's `vm-manifest.json`. It signs and notarizes the
VM's host tools and `mkfs.erofs` inside the app, and ships the licences and sources they require.

**Architecture:** `release/release.sh` gains the steps plan 2 made by hand:
- the builder's image build, `recipe.py`, `parts.sh` and `manifest.py`;
- signing the new helpers inside-out under the hardened runtime;
- the source archive gaining what the new binaries' licences ask.

It still never tags, pushes or uploads: it prints the `gh release` commands, now with the parts archive.

**Tech Stack:** POSIX shell, codesign, notarytool, GNU tar (in the builder).

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md` (§4.1, §4.2, §4.7)

## Global Constraints

- **Asset name:** `MacNeutron-<version>-vm-parts.tar.xz`. Its URL in the manifest is
  `https://github.com/chadouming/MacNeutron/releases/download/v<version>/MacNeutron-<version>-vm-parts.tar.xz`.
- **Where things go in the app:** the manifest at `MacNeutron.app/Contents/Resources/vm-manifest.json`,
  `mkfs.erofs` at `Contents/Helpers/mkfs.erofs`, and the host tools at `Contents/Helpers/vm/` (plan 1).
- **Entitlements, under the hardened runtime:**
  - `macneutron-vm`: `com.apple.security.hypervisor`.
  - `mn-steam-host`: `com.apple.security.cs.disable-library-validation`, since it loads Mac Steam's library.
  - Everything else: none.
- **Licences shipped in `Contents/Resources/licenses/`:**
  - libkrun and gvproxy (Apache-2.0); libtpms (BSD-3-Clause); OpenSSL (Apache-2.0, linked into `macneutron-vm`);
  - erofs-utils (GPL-2.0-or-later); lz4 (BSD-2-Clause); zstd (BSD-3-Clause);
  - a note that Fedora's packages are fetched from Fedora on each Mac and never shipped.
- **The source archive** carries the source of everything GPL that ships (erofs-utils), plus the pins and patches of
  the rest.
- Publishing waits for the maintainer's yes. Commit trailer and `/usr/bin/grep` as plan 1.

## Review Focus

- **The manifest and the archive must agree.** The release refuses to finish if the manifest's parts hash or size
  isn't the archive's (Task 1's check).
- **The sparse state template:** GNU tar's `--sparse` in, bsdtar out, with holes kept. The extracted file uses
  kilobytes, not 16 GiB (Task 1, step 2).
- **A helper signed without the hardened runtime fails notarization.** The release checks every Mach-O in
  `Helpers/vm` before submitting (Task 2's check).
- **`mn-steam-host` without its entitlement** can't load Steam's library on a user's Mac. The release asserts it
  (Task 2's check).
- **A release whose source archive lacks erofs-utils' source** would break the GPL. `verify-sources.sh` fails it
  (Task 3).

---

### Task 1: The parts archive and the manifest in `make release`

**Files:**
- Modify: `release/release.sh`, in a new step after the runtime build:
  1. `eac-vm/builder.sh start`, then the image build. `build.sh` already runs `recipe.py`.
  2. In the builder: `tar --sparse -C <parts> -cJf <archive> .` (GNU tar).
  3. `manifest.py <archive> … <url>`, written to `$OUT/vm-manifest.json`.
  4. Check that the manifest's `parts.sha256` and `parts.size` equal the archive's.
  5. Add the archive to SHA256SUMS and SIZES.txt, and to the printed `gh release upload` line.
- Modify: `release/release.sh`'s bundle step: copy `vm-manifest.json` into `Contents/Resources`.

- [ ] **Step 1:** Run release.sh's own check mode (`sh release/release.sh --check-version 0.6.0`, and its shims'
  self-test if it has one). Expected: unchanged results.
- [ ] **Step 2:** In the builder, make an archive with the 16 GiB sparse template. On the Mac, `/usr/bin/tar -xJf`
  it into `$TMPDIR/x`. Expected: `du -k $TMPDIR/x/state-template.img` is under 20000.
- [ ] **Step 3:** A dry run of the new step (`release.sh` with its notary shim, as its tests do). Expected: the
  archive and the manifest are in `$OUT`, and the hash check passes. With the manifest's hash edited by hand, the
  check fails and names it.
- [ ] **Step 4:** Commit: "release: the VM parts archive and vm-manifest.json".

### Task 2: Signing the VM's helpers

**Files:**
- Create: `release/entitlements/macneutron-vm.plist` (hypervisor) and `release/entitlements/mn-steam-host.plist`
  (disable-library-validation).
- Modify: `release/release.sh`'s bundle step.
  - Copy `Helpers/vm` (via `eac-vm/host/stage.sh`) and `Helpers/mkfs.erofs`.
  - Sign inside-out: `libkrun/libkrun.1.dylib`, `metal-host/*.so`, `steam-forward/host/mn-steam-host` (its
    entitlements), `gvproxy`, `macneutron-vm` (its entitlements), `mkfs.erofs`, then the CLI and the app as now.
  - Use the existing `sign()` helper with `--options runtime --timestamp`.
- Add checks before notarization:
  - every Mach-O under `Helpers/vm` and `Helpers/mkfs.erofs` shows `flags=0x10000(runtime)` in `codesign -dvvv`;
  - `codesign -d --entitlements -` of `mn-steam-host` names `disable-library-validation`.
- Modify: `Makefile` `app`: also copy `build/vm-setup-tools/mkfs.erofs` to `Helpers/` (ad hoc signed, as the CLI is).

- [ ] **Step 1:** The dry run. Expected: both checks pass. Then strip the runtime flag from one helper with
  `codesign -f -s -` in the dry-run copy. Expected: the check fails and names the helper.
- [ ] **Step 2:** Commit: "release: sign the VM's helpers and mkfs.erofs under the hardened runtime".

### Task 3: Licences and sources

**Files:**
- Modify: `release/release.sh`'s licence step (R4): the Global Constraints' licences, copied from each source tree
  (libkrun-src, libtpms-src, the setup-tools sources, `Sources/CZstd/LICENSE`, gvproxy's release).
- Modify: `release/source-archive.sh`: add `eac-vm/` (already in the repo), plus a tarball of erofs-utils and lz4 at
  their pins from `build/vm-setup-tools-src`.
- Modify: `release/verify-sources.sh`: fail when the erofs-utils tarball is missing, or isn't at the commit in
  `eac-vm/setup-tools/PIN`.

- [ ] **Step 1:** The dry run. Expected: `licenses/` lists the seven licences and the Fedora note, and
  `verify-sources.sh` passes. Then remove the erofs-utils tarball from the archive. Expected: `verify-sources.sh`
  fails.
- [ ] **Step 2:** Commit: "release: licences and sources for the VM's tools".
