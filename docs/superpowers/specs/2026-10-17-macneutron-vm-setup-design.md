# MacNeutron sets up its own game VM

Status: design approved in conversation 2026-10-17; this spec awaits the maintainer's review.
Parent: `2026-10-15-macneutron-eac-vm-route-design.md` (milestone 5, "launch from MacNeutron").

## 1. Goal

A Mac with nothing but MacNeutron.app installed runs games in the microVM. Today the VM folder
(`~/Library/Application Support/MacNeutron/vm`) exists only where a developer links `build/eac-vm-work` into it.
Without it, `MicroVM.find` finds nothing and every game runs on macOS.

Done when, on a Mac without the folder:
- MacNeutron opens, sets up the VM in the background, and shows its progress.
- Afterwards SMITE 2 launched from Steam runs in the VM.
- The guest checks pass on the result: `tests/tpm-host.sh`, the identity gate, `tests/audio.sh`.
- The setup downloads about 1.35 GB and leaves about 2.7 GB in the VM folder.

## 2. Decisions (maintainer, 2026-10-17)

| Question | Decision |
|---|---|
| Where Fedora's binaries come from | Built on each Mac from Fedora's own packages. We never redistribute them. |
| How they're built | Only the packages are downloaded. Their files are streamed into images on macOS, with no builder VM and no Fedora Cloud image. |
| The x86-64 side | FEX's whole x86-64 root (`fex-emu-rootfs-fedora`, 969 MB), used exactly as FEX ships it. |
| Where our own parts come from | A "VM parts" archive attached to each GitHub release, its hash built into the app. |
| When | Automatically, when MacNeutron opens and finds no current VM. Until the VM is ready, games run on macOS. |

## 3. The VM folder and where each piece comes from

```
vm/
  macneutron-vm  libkrun/  steam-forward/host/  gvproxy  metal-host/  run-guest.sh  run-game.sh   ← the app (§4.1)
  images/
    vmlinuz              ← kernel-core's payload, unmodified
    initramfs.img        ← assembled from a file list (§4.5)
    base.erofs           ← assembled from 166 packages and our overlay (§4.5)
    x86-64-rootfs.erofs  ← fex-emu-rootfs-fedora's default.erofs, as is (1.9 GB, lz4; §4.5)
    wine.erofs           ← VM parts (§4.2)
    state-template.img, cmdline.txt, packages.lock  ← VM parts
    manifest.sha256      ← which manifest built this set; its presence marks the set complete
  share/bin/             ← VM parts (guest tools)
  share/valve/           ← the app's Valve download (steamclient64.dll), never ours
  state/, runs/          ← per game, made by run-guest.sh, untouched by setup
  setup/                 ← downloads and the image set being built; removed after success
```

| Piece | Size (download) | Source | Checked by |
|---|---|---|---|
| Host tools | in the app | MacNeutron.app, signed and notarized | the app's signature |
| VM parts | ~200 MB (xz) | our GitHub release | SHA-256 in the app's manifest |
| 166 aarch64 packages | 177 MB | Fedora's mirrors, then Koji | SHA-256 per package in the manifest |
| fex-emu-rootfs-fedora | 969 MB | Fedora's mirrors, then Koji | SHA-256 in the manifest |
| steamclient64.dll | in the Valve download | Valve's CDN (ValveClient, today's) | ValveClient's own checks |

Fedora's signatures are checked once, at release time, when the manifest is made (`rpmkeys --checksig` in the
builder, as `eac-vm/image/build.sh` does now). The notarized app carries the hashes, so a Mac needs no OpenPGP.

## 4. Components

### 4.1 Host tools inside the app

- `make app` and `make release` copy into `MacNeutron.app/Contents/Helpers/vm/`:
  - `macneutron-vm` (with the hypervisor entitlement), `libkrun/libkrun.1.dylib`, `steam-forward/host/mn-steam-host`;
  - gvproxy (Apache-2.0; its licence in Resources), `metal-host/`, `run-guest.sh`, `run-game.sh`.
- Everything is signed with the app's identity. Under the hardened runtime:
  - `mn-steam-host` loads Mac Steam's `steamclient.dylib`, so it needs `disable-library-validation`, as `wine.app`
    has.
  - The keychain items of the TPMs (2026-10-16 work) trust that identity, so app updates keep them.
- `VMToolsInstaller` (MacNeutronCore) copies `Helpers/vm` into `vm/` on every app version. It follows
  `RuntimeInstaller`'s pattern:
  - compare the code identity, copy beside, check the signature, then swap in one step;
  - defer while a `macneutron-vm` runs from `vm/`.
- `MicroVM.find` then requires `images/manifest.sha256` as well as `macneutron-vm`. A half-built or missing image set
  leaves games on macOS.

### 4.2 The VM parts archive

`MacNeutron-<version>-vm-parts.tar.xz`, one per release, made by `make release` from the builder's outputs.

| Path in the archive | What |
|---|---|
| `wine/` | the Wine stage with DXMT's guest DLLs and `winemetal.so`, and the Steam forwarder's `lsteamclient.so` (today's `wine_image`) |
| `base/` | our FEX binaries, `tso-litmus`, `rootfs/` (eac-vm/image/rootfs) |
| `overlay.tar` | what package scripts and our build steps wrote into the base tree (§4.4) |
| `recipe/base.tsv`, `recipe/initramfs.tsv` | the two file lists (§4.4) |
| `share/bin/` | the guest tools (sound plugin, mn-input, steam.exe) |
| `state-template.img` | sparse, empty ext4 |
| `cmdline.txt`, `packages.lock` | as today |

### 4.3 The manifest

`MacNeutron.app/Contents/Resources/vm-manifest.json`, written by `make release`; its SHA-256 names an image set.

```json
{
  "version": 1,
  "parts": {"url": "https://github.com/…/releases/download/v0.6.0/MacNeutron-0.6.0-vm-parts.tar.xz",
            "size": 0, "sha256": "…"},
  "kernel": "7.2.10-200.fc44.aarch64",
  "packages": [
    {"file": "kernel-core-7.2.10-200.fc44.aarch64.rpm", "name": "kernel-core", "version": "7.2.10",
     "release": "200.fc44", "arch": "aarch64", "repo": "updates", "size": 24775793, "sha256": "…"}
  ],
  "fexRootfs": {"file": "fex-emu-rootfs-fedora-…noarch.rpm", "…": "…", "image": "usr/share/fex-emu/RootFS/default.erofs",
                "imageSha256": "…"},
  "koji": {"base": "https://kojipkgs.fedoraproject.org/packages/", "signedKey": "6d9f90a6"}
}
```

Each package is downloaded from the first of these that answers with the right size and hash:
1. `https://dl.fedoraproject.org/pub/fedora/linux/<releases|updates>/44/Everything/<arch>/…/Packages/<letter>/<file>`
2. Koji's signed copy: `<koji.base><name>/<version>/<release>/data/signed/<signedKey>/<arch>/<file>`. Mirrors drop
   superseded updates (the pinned 7.2.10 kernel is already gone from them, replaced by 7.2.11); Koji keeps every build.

### 4.4 The recipe: making the Mac's tree equal the builder's

`dnf --installroot` runs package scripts, and the base image has our build steps on top. Plain unpacking does neither.
So the release records the tree the builder actually produced, and the Mac rebuilds exactly that.

- At release time, `eac-vm/image/recipe.py` runs in the builder against `$WORK/root` and the RPM cache. Each entry
  becomes one line of `recipe/base.tsv`:

  ```
  type  path  mode  uid  gid  size  sha256  source  [link target]
  ```

  - A file whose bytes equal a package payload's file at the same path gets `source` = `pkg:<file name>`.
  - Anything else (passwd, the library cache, enabled units, hwdb.bin, our rootfs/ and FEX) gets `source` =
    `overlay`, and its bytes go into `overlay.tar`.
  - Directories, symlinks and device-less special files carry their metadata only.
  - `security.capability` xattrs are recorded. No other xattrs are, since the guest has no SELinux policy.
- Dropped from the tree on purpose: the RPM and dnf databases (`/usr/lib/sysimage/rpm`, `/var/lib/dnf`), since the
  guest never runs dnf, and `vmlinuz` in the modules folder, which boots from outside the image.
- `recipe/initramfs.tsv` does the same for dracut's output. Its files come from packages, or from the overlay for
  dracut's own scripts and depmod's outputs.

### 4.5 Assembly on the Mac (`macneutron vm-setup`)

A subcommand of the CLI in `Contents/Helpers`, so the app stays responsive and the work can be stopped and tested.
It prints one progress line per step (`progress <step> <done> <total>`).

1. **Free space:** refuse below 6 GB free. Downloads take 1.35 GB and the image set about 2.7 GB, both in `vm/setup/`.
2. **Downloads,** four at a time, into `setup/cache/<sha256>`. Each is checked for size and SHA-256, and deleted and
   retried (three times) on a mismatch.
3. **Reading RPMs** (`RPMReader`, MacNeutronCore): lead, signature header, header, then the payload.
   - The payload is a zstd-compressed cpio (newc). Zstd is decoded with a bundled libzstd (BSD).
   - Paths, modes, owners and file capabilities come from the header's file tags. The cpio entries are matched to
     them by path.
4. **`base.erofs`:** the recipe's lines in order become a tar stream.
   - Each file's bytes come from its package's payload or the overlay, and are checked against the recipe's SHA-256
     as they pass.
   - The stream goes into the bundled `mkfs.erofs --tar=f -L mn-base -zlz4hc` (erofs-utils 1.9.4, GPL-2.0+, which
     builds on macOS).
   - Nothing is unpacked onto the Mac's disk, which is case-insensitive.
5. **`initramfs.img`:** the initramfs recipe becomes a newc cpio, gzip-compressed with the system zlib.
6. **`vmlinuz`** comes from kernel-core's payload, and **`x86-64-rootfs.erofs`** from fex-emu-rootfs-fedora's, each
   checked against the manifest.
   - FEX's image keeps its own label, so the guest mounts it by its virtio-blk serial instead
     (`/dev/disk/by-id/virtio-x86-64` in `fstab`; libkrun passes the block device's id as the serial, to be confirmed
     in plan 2).
   - If the serial isn't exposed, the fallback is to rewrite the superblock's volume name and checksum in the copy.
7. **`wine.erofs`:** the parts' `wine/` as a tar stream through `mkfs.erofs -L mn-wine -zlz4hc`. The rest of
   `images/` and `share/bin` are copied from the parts.
8. **Swap in:**
   - `setup/images` replaces `vm/images` in one `renamex_np(RENAME_SWAP)`, then `manifest.sha256` is written. This is
     deferred while a VM runs.
   - The old set and `setup/` are then deleted.
   - `share/valve/steamclient64.dll` is copied from the Valve cache, at this step and after every Valve update.

### 4.6 In the app (`AppModel`)

- **On start:** after the runtime install, if `vm/images/manifest.sha256` doesn't match the app's manifest (or is
  missing), start `macneutron vm-setup` in the background.
  - An older complete set keeps running games while a new one builds.
  - With no set at all, games run on macOS.
- **Status line,** beside the Valve notice:
  - "Game VM: downloading (0.4 of 1.35 GB)", "Game VM: building its images (2 of 4)", then "Game VM: ready".
  - On failure, the reason and a Retry button.
- **Settings:** "Rebuild the game VM" deletes `vm/images` (not `state/`, `runs/` or any TPM) and runs the setup.
- **Logs:** setup's own log goes to the launcher log folder (`vm-setup.log`).

### 4.7 Releases

- `make release` also:
  - runs `build.sh` in the builder;
  - runs `recipe.py` and packs the parts archive;
  - writes `vm-manifest.json` into the app;
  - adds the archive to the release's assets and SHA256SUMS.
- Publishing still waits for the maintainer's yes.
- **Licences:**
  - The source archive gains the erofs-utils and zstd pins and their sources, because `mkfs.erofs` (GPL) ships in
    the app.
  - The parts archive is ours: Wine (LGPL), DXMT, FEX (MIT), the forwarder, and generated configuration data.
  - Fedora's packages and Valve's DLL are never in it.

## 5. Failure handling

- **Downloads:** a download that fails or mismatches its hash three times stops the setup with that file's name.
  Earlier downloads stay in `setup/cache` for the next try.
- **Assembly:** an assembly error (a recipe hash mismatch, `mkfs.erofs` failing) stops the setup. The old `vm/images`
  is never touched before the swap.
- **No space:** the setup is refused before anything downloads.
- **Stopping:** quitting the app stops the setup; the next start resumes from the cache.
- **Kept separate:** per-game state disks and TPMs belong to `state/` and are never deleted by setup, rebuild or
  update.

## 6. Testing

- **Unit tests (Swift):**
  - manifest parsing;
  - picking a source (a mirror miss falls back to Koji);
  - refusing a hash mismatch;
  - `RPMReader` on a synthetic RPM the test writes (lead, headers, zstd cpio);
  - the tar and cpio writers;
  - `VMToolsInstaller`'s swap and deferral;
  - `AppModel`'s setup state with a fake runner.
- **`eac-vm/tests/vm-setup.sh`** (dev Mac, the release gate):
  - runs `macneutron vm-setup` into a temporary VM folder, from `file://` copies of the builder's cache and parts;
  - compares each built image's file listing (path, type, mode, owner, size, SHA-256) with the builder's own images;
  - then boots the guest checks against the result.
- **A clean run:** a full setup on a clean user account before the release that ships it.

## 7. Plans, in order

1. **Host tools and the VM folder:** `Helpers/vm` in `make app`, `VMToolsInstaller`, `MicroVM.find`'s completeness
   check, and copying Valve's DLL.
2. **Assembly:** `RPMReader`, the tar and cpio streams, bundled `mkfs.erofs` and libzstd, `macneutron vm-setup`,
   `recipe.py`, the fstab change, and `tests/vm-setup.sh` against the builder.
3. **Releases:** the parts archive, `vm-manifest.json`, and `make release`'s new assets and source pins.
4. **The app:** automatic setup, the status line, Retry and Rebuild.

## 8. Out of scope

- The EAC runtime (Steam app 1826330): how it reaches the Mac is still open (parent spec §11).
- Secure Boot firmware (parent milestone 6).
- Updates by delta: a new manifest downloads every changed package again. Unchanged ones are re-downloaded too,
  because the cache is removed after each success.
