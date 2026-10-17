# VM setup, plan 2: assembling the images on the Mac

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `macneutron vm-setup` turns a manifest into a complete `vm/images`. It downloads Fedora's packages and our
parts and streams their files into `mkfs.erofs` on macOS. The images it makes must equal the builder's, file for file.

**Architecture:**
- **At release time**, `recipe.py` (in the builder) records the builder's trees as recipes and an overlay of the
  bytes no package holds. `parts.sh` stages the parts, and `manifest.py` writes the manifest.
- **On the Mac**, Swift reads the RPMs (xz through Compression, zstd through a vendored decoder), fills a
  content-addressed store from their payloads and the overlay, then writes tar and cpio streams from the recipes.
- **`mkfs.erofs`** (erofs-utils, built here) makes the EROFS images.
- **The gate** compares the Mac's images with the builder's and boots the guest on them.

**Tech Stack:** Swift 6 (swift-testing), C (vendored zstd decompressor, erofs-utils, lz4), Python 3 stdlib (in the
builder), POSIX shell.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md` (§3, §4.2-§4.5, §5, §6)

## Global Constraints

- **Pins:**
  - erofs-utils v1.9.4 and lz4 v1.10.0, in `eac-vm/setup-tools/PIN` as `<name> <tag> <commit>`;
  - zstd v1.5.7, decompressor only, vendored in `Sources/CZstd/` with its LICENSE.
- **mkfs.erofs:** every EROFS image is made with `mkfs.erofs --tar=f -L <label> -zlz4hc`. Labels: `mn-base`,
  `mn-wine`.
- **Disk IDs:** the guest mounts FEX's x86-64 image by disk ID (`/dev/disk/by-id/virtio-x86-64`); libkrun sends the
  block device's id as its virtio-blk serial (`devices/src/virtio/block/device.rs`).
- **The recipe TSV:** one entry per line, tab-separated:
  `type path mode uid gid size sha256 source link caps`.
  - `type`: `d`, `f` or `l`.
  - `mode`: octal permission bits only.
  - `sha256` and `size`: `-` unless `f`.
  - `source`: `pkg:<rpm file name>` or `overlay` (`-` unless `f`).
  - `link`: the symlink target, or `-`.
  - `caps`: the `security.capability` value in hex, or `-`.
  - Paths are absolute. A tab, newline or backslash in a path or link is written `\t`, `\n`, `\\`.
- **Removed from the base tree:** `/usr/lib/sysimage/rpm`, `/var/lib/dnf`, `/var/cache/dnf` and
  `/lib/modules/<kver>/vmlinuz`.
- **Free space:** setup refuses to start below 6 GB free (`--min-free-gb` overrides it for tests).
- **Exit codes:** 0 done, 75 deferred because a VM runs from the folder, 1 failed (the reason on stderr's last line),
  64 usage.
- **The manifest:** `vm-manifest.json` as spec §4.3, without the `repo` field. The client tries `releases`, then
  `updates`, then Koji.
- **Never written by setup:** `state/`, `runs/` and the `*.tpm` folders. A symlinked VM folder is left alone (plan 1).
- Downloads beyond the three pinned sources need the maintainer's yes. Commit trailer and `/usr/bin/grep` as plan 1.

## Review Focus

- **A package payload with hardlinks** (cpio newc gives the data on the last link only): every linked path gets the
  bytes (Task 4, `hardlinkedFilesAllGetTheirBytes`).
- **A recipe file whose bytes no package and no overlay holds:** setup fails naming the path, before anything is
  swapped (Task 7, `aMissingFileFailsWithItsPath`).
- **A download that returns the wrong bytes on every source:** setup fails naming the file, and the cache keeps the
  files that were good (Task 8, `aFileWrongEverywhereFailsWithItsName`).
- **A VM started during setup:** setup builds but defers the swap (exit 75), and the next run swaps without
  rebuilding (Task 9, `aRunningVMDefersTheSwapAndTheNextRunFinishesIt`).
- **Paths that differ only by case** (case-insensitive APFS): both survive into the image, since nothing is unpacked
  onto the Mac's disk (Task 2's check, Task 7's `pathsDifferingOnlyInCaseBothSurvive`).

---

### Task 1: The guest mounts FEX's image by disk ID; FEX's image is used as is

**Files:**
- Modify: `eac-vm/image/rootfs/etc/fstab`. The x86-64 line becomes
  `/dev/disk/by-id/virtio-x86-64 /usr/share/fex-emu/RootFS/fedora erofs ro,nofail 0 0`.
- Modify: `eac-vm/image/build.sh`. `x86-64-rootfs.erofs` becomes a plain copy of the package's
  `usr/share/fex-emu/RootFS/default.erofs`, with no loop mount and no `mkfs.erofs`.

- [ ] **Step 1:** Rebuild in the builder (`builder.sh start`, `build.sh` as its header says).
- [ ] **Step 2:** `sh eac-vm/tests/audio.sh` (an x86-64 ALSA program under FEX; it needs the x86-64 root mounted).
  Expected: `audio: ok`. Then `sh eac-vm/tests/tpm-host.sh`. Expected: `tpm-host: ok`.
- [ ] **Step 3:** Commit: "eac-vm image: FEX's x86-64 root as FEX ships it, mounted by disk ID".

### Task 2: Build `mkfs.erofs` on macOS; vendor zstd's decoder

**Files:**
- Create: `eac-vm/setup-tools/PIN` and `eac-vm/setup-tools/build.sh`.
  - The script clones the two pins into `build/vm-setup-tools-src/`.
  - It compiles erofs-utils' `lib/` and `mkfs/` sources, as `lib/Makefile.am` and `mkfs/Makefile.am` list them, with
    lz4's `lz4.c` and `lz4hc.c`, using plain clang and a hand-written `config.h`: `HAVE_LIBLZ4`, `LZ4HC_ENABLED`, no
    lzma, zlib, uuid, selinux or fuse.
  - Output: `build/vm-setup-tools/mkfs.erofs`. It is rebuilt when `PIN` changes (a copy beside the output, as
    libtpms does).
- Create: `Sources/CZstd/`, holding zstd v1.5.7's `lib/common/*` and `lib/decompress/*` (`.c` and `.h`),
  `include/zstd.h` and `LICENSE`.
- Modify: `Package.swift`: `.target(name: "CZstd", cSettings: [.define("ZSTD_DISABLE_ASM")])`, and `MacNeutronCore`
  depends on it.

- [ ] **Step 1:** `sh eac-vm/setup-tools/build.sh`. Expected: it prints `build/vm-setup-tools/mkfs.erofs`, and
  `mkfs.erofs --version` says 1.9.4.
- [ ] **Step 2:** The case check:
  - Build `t.tar` holding `/a/Readme`, `/a/README`, and `/bin/ping` with a `SCHILY.xattr.security.capability` pax
    record. Use Python's `tarfile` with `pax_headers`.
  - Run `mkfs.erofs --tar=f -L t -zlz4hc t.erofs t.tar`.
  - Find out whether `-` as the source reads stdin; Task 7 needs to know.
  - Copy `t.erofs` into the builder's share and check there: `sudo mount -o loop,ro`, then `ls /a` shows both files
    and `getcap /bin/ping` shows the capability.
- [ ] **Step 3:** `swift build`. Expected: it builds with CZstd.
- [ ] **Step 4:** Commit: "vm-setup tools: mkfs.erofs for macOS, zstd's decoder vendored".

### Task 3: `PayloadDecoder`

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/PayloadDecoder.swift`
- Test: `Tests/MacNeutronCoreTests/PayloadDecoderTests.swift`

**Interfaces:**
- Produces: `enum VMSetupError: Error, Equatable` in `Sources/MacNeutronCore/VMSetup/VMSetupError.swift`, with
  `.unsupported(String)` and `.truncated` here. Later tasks add their own cases there.

```swift
struct PayloadDecoder {
    init(compressor: String) throws   // "zstd" (CZstd) or "xz" (Compression, COMPRESSION_LZMA); else VMSetupError.unsupported
    mutating func decode(_ input: Data) throws -> Data   // as much output as this input gives
    mutating func finish() throws -> Data                // throws VMSetupError.truncated if the stream didn't end
}
```

- [ ] **Step 1:** The test vectors are hex literals. Make them once in the builder:
  `printf 'MacNeutron payload\n%.0s' $(seq 2000) | zstd -19 | xxd -p`, and the same through `xz -9`.
  - `zstdAndXzDecodeToTheSameBytes`: decoding each, fed in 7-byte pieces, gives the 2000 lines.
  - `aCutStreamIsTruncated`: the zstd vector without its last 4 bytes throws `.truncated` from `finish()`.
- [ ] **Step 2:** `swift test --filter PayloadDecoderTests`. Expected: FAIL.
- [ ] **Step 3:** Implement as the Interfaces block says.
- [ ] **Step 4:** `swift test --filter PayloadDecoderTests`. Expected: PASS.
- [ ] **Step 5:** Commit: "vm-setup: RPM payload decoders (zstd, xz)".

### Task 4: `RPMReader`

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/RPMReader.swift`, `Sources/MacNeutronCore/VMSetup/CPIO.swift`
- Test: `Tests/MacNeutronCoreTests/RPMReaderTests.swift` and `Tests/MacNeutronCoreTests/RPMFixture.swift`. The
  fixture is a writer: lead, an empty signature header, a header with the tags below, and an xz-compressed newc cpio,
  using Compression's encoder.

**Interfaces:**
- Produces:

```swift
struct RPMHeader { let name, version, release, arch, payloadCompressor: String }
enum RPMReader {
    /// Each payload file, in payload order: its absolute path ("./usr/x" → "/usr/x") and its bytes, in chunks.
    /// Directories and symlinks are skipped: the recipe carries them.
    static func readFiles(_ rpm: URL, _ body: (_ header: RPMHeader, _ path: String,
                                               _ chunks: (inout Data) throws -> Bool) throws -> Void) throws
}
```

- **Header tags read:** NAME 1000, VERSION 1001, RELEASE 1002, ARCH 1022, PAYLOADFORMAT 1124 (must be `cpio`),
  PAYLOADCOMPRESSOR 1125.
- **Layout:** the signature header is padded to 8 bytes; the header's index and store are big-endian.
- **cpio:** newc, magic `070701`. Hardlinks share an inode, and the data comes with the last one: hold back the
  earlier paths and give each of them the bytes too.

- [ ] **Step 1:** Tests on fixtures:
  - `readsTheHeaderAndEveryFile`: name, version, release, arch, `xz`, and two files with their exact bytes.
  - `hardlinkedFilesAllGetTheirBytes`: two paths on one inode; both get the bytes.
  - `aNonCpioPayloadIsRefused`: PAYLOADFORMAT `drpm` throws `VMSetupError.unsupported`.
- [ ] **Step 2:** `swift test --filter RPMReaderTests`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass. Then a real package:
  - Copy `kernel-core-*.rpm` (xz) and `systemd-2*.rpm` (zstd) from the builder's cache to `$TMPDIR`.
  - Add `realFedoraPackagesRead`, a test that runs only when `MN_RPM_SAMPLES=<dir>` is set. It counts each package's
    files and finds `/lib/modules/7.2.10-200.fc44.aarch64/vmlinuz`.
  - Expected: it is found, and its SHA-256 equals `sha256sum` of `build/eac-vm-work/images/vmlinuz`.
- [ ] **Step 5:** Commit: "vm-setup: an RPM reader".

### Task 5: Recipes, and the tar and cpio writers

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/Recipe.swift`, `Sources/MacNeutronCore/VMSetup/TarWriter.swift`,
  `Sources/MacNeutronCore/VMSetup/CPIOWriter.swift` (beside Task 4's reader in `CPIO.swift`, or merged into it)
- Test: `Tests/MacNeutronCoreTests/RecipeTests.swift`, `Tests/MacNeutronCoreTests/ArchiveWriterTests.swift`

**Interfaces:**
- Produces:

```swift
struct RecipeEntry: Equatable {
    enum Kind: Equatable { case directory, file(size: Int64, sha256: String, source: Source), symlink(String) }
    enum Source: Equatable { case package(String), overlay }
    let path: String; let mode: UInt16; let uid: UInt32; let gid: UInt32; let kind: Kind; let capability: Data?
}
enum Recipe { static func parse(_ tsv: String) throws -> [RecipeEntry] }   // VMSetupError.badRecipe(line:)
struct TarWriter {      // POSIX pax; a pax record for long paths/links, uid/gid, and SCHILY.xattr.security.capability
    init(_ out: FileHandle)
    mutating func add(_ entry: RecipeEntry, contents: URL?) throws   // contents for .file only
    mutating func finish() throws                                    // two zero blocks
}
struct CPIOWriter {     // newc, no compression: Fedora's kernel takes an uncompressed initramfs
    init(_ out: FileHandle)
    mutating func add(_ entry: RecipeEntry, contents: URL?) throws
    mutating func finish() throws                                    // TRAILER!!!
}
```

- [ ] **Step 1:** Tests:
  - `parsesEachKindAndTheEscapes`: one `d`, one `f` (pkg and overlay), one `l`, a path with `\t`, and a capability.
  - `aShortLineIsABadRecipe`: throws `.badRecipe(line: 2)`.
  - `tarRoundTripsThroughBsdtar`: `/usr/bin/tar -tvf` lists the paths, modes and owners written, including a 200-byte
    path.
  - `cpioRoundTripsThroughBsdtar`: `/usr/bin/tar -tvf` reads the cpio (bsdtar reads newc).
  - `theCapabilityIsAPaxRecord`: the tar's pax header holds `SCHILY.xattr.security.capability=` with the bytes.
- [ ] **Step 2:** `swift test --filter "RecipeTests|ArchiveWriterTests"`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass.
- [ ] **Step 5:** Commit: "vm-setup: recipes, tar and cpio streams".

### Task 6: `recipe.py`, `parts.sh` and `manifest.py` (release side, in the builder and on the Mac)

**Files:**
- Create: `eac-vm/image/recipe.py <root> <initramfs.img> <kver> <out> <rpm dir>...`.
  - **Payload index:** for each RPM, `rpm2archive -n` gives an uncompressed tar on stdout, which `tarfile` reads to
    map `(path, sha256) → rpm file name`.
  - **The base tree:** walk `<root>` with `lstat`, skipping the removed paths. A file whose `(path, sha256)` is in the
    map is `pkg:`; any other file is copied to `<out>/overlay/<sha256>` and marked `overlay`. Read capabilities with
    `os.getxattr(path, "security.capability")`. Write `<out>/recipe/base.tsv`.
  - **The initramfs:** unpack `<initramfs.img>` into a temporary directory as root (`lsinitrd --unpack` run there),
    and walk it the same way into `<out>/recipe/initramfs.tsv`.
  - Write `<out>/packages.json`: each RPM's file, name, version, release, arch, size and sha256.
  - Print the counts and the overlay's total size.
- Modify: `eac-vm/image/build.sh`. Keep `$WORK/root` and `$WORK/initramfs.img` after building (already true), and run
  `recipe.py "$WORK/root" "$WORK/initramfs.img" "$KVER" "$OUT/setup" "$CACHE/base" "$CACHE/x86"` last.
- Create: `eac-vm/setup/parts.sh <out dir>` (on the Mac). It stages the parts folder from `build/eac-vm-work/images`
  (its `setup/` from `recipe.py`, plus `cmdline.txt`, `packages.lock`, `state-template.img`), `share/bin`'s
  `libasound_module_pcm_mn.so`, `mn-input` and `steam.exe`, and `wine.tar`.
  - `wine.tar` is made in the builder: `sudo tar -C "$WORK/wine" -cf "$OUT/setup/wine.tar" .` at the end of
    `wine_image`.
- Create: `eac-vm/setup/manifest.py <parts archive or dir> <packages.json> <kver> <parts url> > vm-manifest.json`.
  The parts entry hashes whatever it's given. `fexRootfs.imageSha256` is the SHA-256 of `images/x86-64-rootfs.erofs`
  (Task 1 made it a plain copy).

- [ ] **Step 1:** Rebuild in the builder.
- [ ] **Step 2:** Run `recipe.py` there. Expected:
  - Counts of entries for both trees; the overlay is under 50 MB. Note the number in the ledger.
  - No `overlay` entry under `/usr/lib64` or `/usr/bin` other than our FEX binaries and `tso-litmus`. Check with
    `/usr/bin/grep -P '\toverlay\t' base.tsv | cut -f2 | sort | head -50`.
- [ ] **Step 3:** Run `parts.sh "$TMPDIR/parts"` and `manifest.py`. Expected: a parts folder and a manifest that
  `python3 -m json.tool` accepts, with 166 + 1 packages.
- [ ] **Step 4:** Commit: "eac-vm image: recipes and overlay for assembling on a Mac; parts and manifest scripts".

### Task 7: `ImageAssembler`

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/ImageAssembler.swift`
- Test: `Tests/MacNeutronCoreTests/ImageAssemblerTests.swift`. It uses Task 4's fixtures, and a fake `mkfs` script
  that copies its tar source to the output path, so the test can read the tar back.

**Interfaces:**
- Consumes: Task 4 `RPMReader.readFiles`, Task 5 `Recipe`, `TarWriter`, `CPIOWriter`.
- Produces:

```swift
struct ImageAssembler {
    let store: URL   // setup/store: one file per SHA-256
    /// Payload files whose SHA-256 is in `wanted` go into the store (each checked as it's written).
    func collect(_ rpm: URL, wanted: Set<String>) throws
    /// Overlay files (named by their SHA-256) cloned into the store.
    func collectOverlay(_ folder: URL) throws
    /// One payload file to `to`, checked against `sha256` (vmlinuz, FEX's default.erofs).
    func extract(_ rpm: URL, path: String, to: URL, sha256: String) throws
    /// mkfs.erofs --tar=f -L label -zlz4hc out <tar>, the tar written from the recipe (to a pipe if Task 2 found that
    /// mkfs.erofs reads stdin, else to setup/<label>.tar, removed after).
    func erofs(_ recipe: [RecipeEntry], label: String, mkfs: URL, out: URL) throws
    /// The same tar step for a ready tar file (wine.tar).
    static func erofs(tar: URL, label: String, mkfs: URL, out: URL) throws
    func cpio(_ recipe: [RecipeEntry], out: URL) throws
}
```

Errors: `VMSetupError.missing(path:)` when a recipe file isn't in the store, `.mismatch(file:)` when bytes don't match,
`.tool(String, Int32)` when `mkfs.erofs` fails.

- [ ] **Step 1:** Tests:
  - `aRecipeBecomesATarOfPackageAndOverlayFiles`: the read-back tar has every recipe path with the recipe's mode,
    owner and bytes.
  - `aMissingFileFailsWithItsPath`: a recipe line whose SHA-256 no package or overlay holds throws
    `.missing(path: "/etc/x")`.
  - `pathsDifferingOnlyInCaseBothSurvive`: `/a/Readme` and `/a/README` are both in the tar with their own bytes.
  - `extractChecksTheBytes`: a wrong `sha256` throws `.mismatch`.
  - `theInitramfsIsANewcCpio`: it starts with `070701` and ends with `TRAILER!!!`.
- [ ] **Step 2:** `swift test --filter ImageAssemblerTests`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass.
- [ ] **Step 5:** Commit: "vm-setup: images from recipes".

### Task 8: The manifest and downloads

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/VMSetupManifest.swift`, `Sources/MacNeutronCore/VMSetup/VMDownloads.swift`
- Test: `Tests/MacNeutronCoreTests/VMDownloadsTests.swift`

**Interfaces:**
- Consumes: `ValveClient.Fetch` (`@Sendable (URL) async throws -> URL`); `ValveClient.download` handles `https`, and
  this task adds `file:` handling: a copy into a temp file.
- Produces:

```swift
struct VMSetupManifest: Codable, Equatable {
    struct Download: Codable, Equatable { let url: URL; let size: Int64; let sha256: String }
    struct Package: Codable, Equatable { let file, name, version, release, arch: String; let size: Int64; let sha256: String }
    struct FEXRootfs: Codable, Equatable { let package: Package; let image: String; let imageSha256: String }
    struct Koji: Codable, Equatable { let base: URL; let signedKey: String }
    let version: Int; let parts: Download; let kernel: String; let packages: [Package]; let fexRootfs: FEXRootfs; let koji: Koji
    /// dl.fedoraproject.org's releases/44 and updates/44 (Everything/<arch>/os/Packages/<first letter>/), then Koji's signed copy.
    func sources(_ p: Package) -> [URL]
}
enum VMDownloads {
    /// Into cache/<sha256> unless already there and right; each source tried in turn, the whole list up to 3 times.
    static func fetch(_ sources: [URL], size: Int64, sha256: String, name: String, cache: URL,
                      fetch: ValveClient.Fetch) async throws -> URL          // VMSetupError.download(name:)
    static func fetchAll(_ items: [(sources: [URL], size: Int64, sha256: String, name: String)], cache: URL,
                         fetch: ValveClient.Fetch, progress: @Sendable (Int64, Int64) -> Void) async throws
}
```

`fetchAll` runs four at a time and reports bytes done out of the total.

- [ ] **Step 1:** Tests (a fake `Fetch` serving a dictionary of URL to bytes):
  - `sourcesAreReleasesThenUpdatesThenKoji`: for `kernel-core-7.2.10-200.fc44.aarch64.rpm`, the third URL is
    `https://kojipkgs.fedoraproject.org/packages/kernel/7.2.10/200.fc44/data/signed/6d9f90a6/aarch64/kernel-core-7.2.10-200.fc44.aarch64.rpm`.
  - `aMirrorMissFallsBackToKoji`: only the Koji URL answers, and the file lands in the cache.
  - `aFileWrongEverywhereFailsWithItsName`: every source serves wrong bytes, so `.download(name: "x.rpm")` after 3
    rounds; a good file fetched earlier in the same `fetchAll` stays in the cache.
  - `aCachedFileIsNotFetchedAgain`: a correct cached file means zero requests.
- [ ] **Step 2:** `swift test --filter VMDownloadsTests`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests pass.
- [ ] **Step 5:** Commit: "vm-setup: the manifest and checked downloads".

### Task 9: `macneutron vm-setup`

**Files:**
- Create: `Sources/MacNeutronCore/VMSetup/VMSetup.swift`
- Modify: `Sources/MacNeutronCore/CommandLineTool.swift` (the `vm-setup` case, and its usage line).
- Test: `Tests/MacNeutronCoreTests/VMSetupTests.swift`

**Interfaces:**
- Consumes: Tasks 7-8, and plan 1's `VMToolsInstaller.isDeveloperFolder`.
- Produces:
  - The CLI: `macneutron vm-setup --manifest <file> --vm <dir> [--mkfs <tool>] [--min-free-gb <n>]`. `--mkfs`
    defaults to `mkfs.erofs` beside the executable.
  - Its output, one line per step: `progress download <bytes done> <bytes total>`,
    `progress assemble <k> <n>` (vmlinuz, x86-64, base, initramfs, wine, so n = 5), `progress install 0 1`.
  - Exit codes as the Global Constraints say.
  - `public static func VMSetup.manifestHash(_ manifest: URL) -> String?`, the SHA-256 of the file's bytes (nil if
    unreadable), which plan 4 reads.

The run, in order:
1. A developer folder exits 0 with a note.
2. If `setup/images/.built-for` equals the manifest hash, skip to step 7.
3. Free space.
4. `fetchAll`: the parts, then every package.
5. `/usr/bin/tar -xJf` the parts into `setup/parts`.
6. Assemble:
   - `collectOverlay`, then `collect` each package with the recipe's wanted set;
   - `extract` vmlinuz and FEX's image;
   - `erofs` for base, `cpio` for the initramfs, `erofs(tar:)` for wine;
   - copy `cmdline.txt`, `packages.lock`, `state-template.img` and `share/bin`;
   - write `setup/images/.built-for`.
7. If a VM runs from the folder, exit 75.
8. Swap `setup/images` with `images` (`RENAME_SWAP`, or `rename`), write `images/manifest.sha256`, then remove the old
   set and `setup/`.

- [ ] **Step 1:** Tests: a manifest, fixture RPMs and a parts folder made in the test, served through a fake `Fetch`,
  with a fake `mkfs` and a temp VM folder:
  - `aFullRunLeavesACompleteImageSet`: `images/` holds the six files plus `manifest.sha256` (the manifest's hash),
    `share/bin` holds the tools, `setup/` is gone, and `state/1.img` is untouched.
  - `tooLittleSpaceIsRefusedBeforeAnyDownload`: `--min-free-gb 100000` gives exit 1 and zero requests.
  - `aRunningVMDefersTheSwapAndTheNextRunFinishesIt`: injected `runningExecutables` gives exit 75 and the old
    `images/` intact. The second run, with nothing running, makes zero requests and leaves the new set swapped in.
  - `theDevelopersFolderIsLeftAlone`: a symlinked `--vm` gives exit 0 and nothing written.
- [ ] **Step 2:** `swift test --filter VMSetupTests`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `swift test`. Expected: all pass.
- [ ] **Step 5:** Commit: "macneutron vm-setup: a VM folder's images from a manifest".

### Task 10: The gate, `eac-vm/tests/vm-setup.sh`

**Files:**
- Create: `eac-vm/tests/vm-setup.sh`

What it does:
1. Builds `.build/release/macneutron` and `build/vm-setup-tools/mkfs.erofs`.
2. Copies the builder's RPM cache to `build/vm-setup-gate/rpms`, and runs `parts.sh build/vm-setup-gate/parts`.
3. Writes a manifest with `manifest.py`, then rewrites its URLs to `file://` copies. Every package gets its
   `releases` source rewritten through a `--source-root` argument of `manifest.py`, added in this task.
4. Runs `macneutron vm-setup` into `build/vm-setup-gate/vm`, with `stage.sh` from plan 1 putting the host tools
   there.
5. Copies the four images into the builder's share. There, it loop-mounts each beside the builder's own, and diffs
   `find . -printf '%y %p %m %U %G %s\n' | sort` and each file's `sha256sum`.
6. Boots the result with `MN_VM_DIR=build/vm-setup-gate/vm eac-vm/run-guest.sh` and a script that prints:
   - `uname -r`, `cat /sys/kernel/security/lockdown` and `cat /sys/module/module/parameters/sig_enforce`;
   - `ls /dev/tpm0`;
   - the output of `/mnt/share/bin/alsa-play` (an x86-64 program under FEX; staged for the gate only).
7. Prints `vm-setup: ok`.

- [ ] **Step 1:** Run it. Expected:
  - every diff is empty;
  - the guest prints `7.2.10-200.fc44.aarch64`, `[integrity]`, `Y`, `/dev/tpm0` and alsa-play's played line;
  - then `vm-setup: ok`.
  - Record the download total and the setup's wall time in the ledger.
- [ ] **Step 2:** Fix what differs with systematic-debugging, adding to `recipe.py` rather than to the Swift side
  unless the Swift side is wrong.
- [ ] **Step 3:** Commit: "eac-vm: the vm-setup gate (Mac-built images equal the builder's)".
