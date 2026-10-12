# Valve's signed steamclient64.dll by default — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** SteamStub games (Palworld) start with no launch option. The app downloads Valve's signed Windows client files
from Valve's CDN, and the launcher puts Valve's `steamclient64.dll` in every prefix when the runtime has Wine patch 0036.

**Architecture:**
- A new `ValveClient` in MacNeutronCore fetches Valve's manifest, then the `bins_win64` zip. It verifies the zip,
  extracts Proton's six legacycompat files flat, checks each file's machine and certificate table, and installs them
  atomically in `~/Library/Application Support/MacNeutron/Valve/<version>/`.
- `bundle.sh` writes a `genuine-steamclient` marker into `wine.app`.
- `PrefixManager.deploySteamBridge` picks the prefix's `steamclient64.dll` in this order: the override, then the cache
  (when the marker is present), then lsteamclient.
- The app runs `ValveClient` after each successful runtime install, and Settings gets a row for it.
- A `release.sh` guard keeps every Valve file out of the app.

**Tech Stack:** Swift 6 (Swift Testing, URLSession, CryptoKit), POSIX sh (`release/release.sh`, `wine-arm64/bundle.sh`,
`wine-arm64/check.sh`), `/usr/bin/unzip`, `/usr/bin/ditto`.

**Spec:** `docs/superpowers/specs/2026-10-12-macneutron-valve-steamclient-design.md` (approved; D2 = download without
asking). Amended by `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md` Part A item 1: A1's six
files, A2's extraction and A7's guard, extended with `bin/SteamService.dll`, `gameoverlayui*` and
`SteamOverlayVulkanLayer*`.

## Global Constraints

- **Never shipped by us.** Valve's files are never committed, bundled into the app or release zip, or uploaded.
  - Tests use tiny fake PE files that we build. They never use Valve's files.
  - No test touches the network or the real `~/Library/Application Support/MacNeutron`.
- **Manifest:** `https://client-update.steamstatic.com/steam_client_win64`.
  - The package is the manifest's `bins_win64` entry, downloaded from `https://client-update.steamstatic.com/<file>`.
  - Use an ephemeral `URLSession`: no cookies, no cache, no account data.
- **Cache:** `~/Library/Application Support/MacNeutron/Valve/<manifest version>/`. It holds the six files under
  Valve's names, flattened (`bin/` dropped), and a `source.txt` recording the URL, `sha2` and date.
- **The six files** (parity A1), with their PE machines:

  | File | Machine |
  |---|---|
  | `steamclient64.dll` | AMD64 |
  | `GameOverlayRenderer64.dll` | AMD64 |
  | `steamclient.dll` | i386 |
  | `Steam.dll` | i386 |
  | `GameOverlayRenderer.dll` | i386 |
  | `bin/SteamService.exe` | i386 |

- **P0 deploys `steamclient64.dll` only.** It deploys no overlay, no `SteamService` and no 32-bit files.
- **D2:** no consent prompt.
- **D4:** re-read the manifest once per app version, and download only when `bins_win64`'s `sha2` changed.
- **D5:** `MACNEUTRON_STEAMCLIENT64=<path>` keeps working, and `MACNEUTRON_STEAMCLIENT64=builtin` forces lsteamclient.
  Settings' Remove sticks until Download again is pressed.
- **Marker:** `wine.app/Contents/Resources/genuine-steamclient`, read as `ToolLayout.supportsGenuineSteamclient`.
- **Proton parity:** where we differ from Proton, the reason is a macOS one and is written next to the code. For example,
  Mac Steam has no `legacycompat` folder, so we download its files.
- **Gates:**
  - Use `/usr/bin/grep`, never bare `grep`.
  - Run only `release.sh --self-test`, never a release.
  - Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Offline or Valve unreachable at app start.** The runtime still installs and Settings shows the error. Launches use
   lsteamclient, and the next app start retries. Test: Task 5, `valveFailureLeavesTheRuntimeInstalled`.
2. **Valve changes the zip** (a member missing or renamed, or no `bins_win64` in the manifest). The update fails and the
   cached version stays in use. Test: Task 3, `aFailedUpdateKeepsTheInstalledVersion`.
3. **A cache damaged by hand** (`steamclient64.dll` deleted). The launch falls back to lsteamclient with a note and
   doesn't fail. Test: Task 4, `aCacheWithoutSteamclient64FallsBackToLsteamclient`.
4. **An install stopped mid-way** (the app quit during extraction). It leaves a `.staging-*` folder, which the launcher
   never uses and the next update prunes. Test: Task 3, `aStagingFolderIsNeverInstalledAndIsPruned`.
5. **A hostile or garbled manifest:** a `version` or `file` holding `/` or `..`, or a `sha2` that isn't 64 hex digits. It
   is refused before any path is built from it. Test: Task 3, `manifestFieldsThatCouldEscapeTheCacheAreRefused`.

---

### Task 1: The release guard (parity A7)

The guard must exist before any deployment change (parity spec, Licence).

**Files:**
- Modify: `release/release.sh`
  - Add a new `check_no_valve` beside the other `check_*` functions (`:26-99`).
  - Add self-test rows before `:259`.
  - Call it in `build_app` (`:325-378`), right after the licence `README` heredoc and before `build_paths`.

**Interfaces:**
- Produces: `check_no_valve <dir>`. It prints one line per Valve file found under `<dir>`, on stderr:
  `release: <path> is one of Valve's files`. It returns 1 if it found any, and returns 0 silently otherwise.
  - The match is `find -iname`, case-insensitive, on the basename.
  - The names are `steamclient*`, `GameOverlayRenderer*`, `Steam.dll`, `SteamService*`, `tier0_s*`, `vstdlib_s*`,
    `gameoverlayui*` and `SteamOverlayVulkanLayer*`.

- [ ] **Step 1: Write the failing self-test rows**, in `self_test`'s style:
  ```sh
  mkdir -p "$T/clean/Contents/Resources/lib"
  for f in steam.exe lib/lsteamclient.dll lib/lsteamclient.so; do : > "$T/clean/Contents/Resources/$f"; done
  passes "an app without Valve's files (lsteamclient and our steam.exe are ours)" check_no_valve "$T/clean"
  for n in steamclient64.dll steamclient.dll GameOverlayRenderer64.dll Steam.dll SteamService.exe bin/SteamService.dll \
    tier0_s64.dll vstdlib_s64.dll gameoverlayui64.exe SteamOverlayVulkanLayer64.dll STEAMCLIENT64.DLL; do
    rm -rf "$T/v"; mkdir -p "$(dirname "$T/v/x/$n")"; : > "$T/v/x/$n"
    refused "Valve's $n in the app" "release: $T/v/x/$n is one of Valve's files" check_no_valve "$T/v"
  done
  ```
- [ ] **Step 2: Run them to see them fail.** `sh release/release.sh --self-test`. Expected: 12 `FAIL` lines.
  `check_no_valve` is not found yet, so the `passes` row fails, and so does each of the 11 `refused` rows, because its
  message doesn't match. The last line is `FAIL release self-test`.
- [ ] **Step 3: Implement `check_no_valve`** and add its call in `build_app`:
  `out=$(check_no_valve "$A" 2>&1) || die "A7: $out"`. Above the function, put a comment saying where these files come
  from: ValveClient downloads them from Valve to the player's Mac, and Mac Steam has no `legacycompat` folder.
- [ ] **Step 4: Run them to see them pass.** `sh release/release.sh --self-test`. Expected: every row `ok`, and the last
  line `PASS release self-test`.
- [ ] **Step 5: Commit.**
  `release: refuse an app holding any of Valve's Windows client files (parity A7)`.

### Task 2: The runtime advertises patch 0036, and ToolLayout finds the cache

**Files:**
- Modify: `wine-arm64/bundle.sh`. Next to the Steam bridge assertions (after `:288`), it writes `$R/genuine-steamclient`
  when the built Wine tree has 0036.
- Modify: `wine-arm64/check.sh`.
  - The `steamclient_cmd` step (`:809-820`) asserts the marker.
  - Where `STEAMCLIENT64` is set (`:805`), it falls back to the cache. Also update the header comment at `:8-10`.
- Modify: `Sources/MacNeutronCore/ToolLayout.swift`.
- Modify: `Tests/MacNeutronCoreTests/Support.swift`. In `makeToolLayout` (`:77-78`), the root becomes
  `makeTempDir()/compatibilitytools.d/macneutron`, the production shape. That way each test's `valveCache` is its own
  folder.
- Test: `Tests/MacNeutronCoreTests/PathsTests.swift`.

**Rulings in this plan:**
- The spec says `build.sh` writes the marker. Here `bundle.sh` writes it, because the release's `wine.app` is staged by
  `bundle.sh --release`, not `build.sh`. Both paths then carry it.
- `check.sh` keeps `VALVE_STEAMCLIENT64` and `build/valve` ahead of the cache, so a check run on the maintainer's Mac
  never reads `~/Library/Application Support/MacNeutron`.

**Interfaces:**
- Produces, in `ToolLayout`:
  - `public var supportsGenuineSteamclient: Bool`: `Contents/Resources/genuine-steamclient` exists in `wine.app`.
  - `public var valveCache: URL`: `root/../../Valve`. For `defaultRoot`, that is
    `~/Library/Application Support/MacNeutron/Valve`.

- [ ] **Step 1: Write the failing Swift rows.**
  ```swift
  @Test func valveCacheSitsBesideTheToolFolders() {
      #expect(ToolLayout(root: ToolLayout.defaultRoot).valveCache.path(percentEncoded: false)
          == FileManager.default.homeDirectoryForCurrentUser.appending(path: "Library/Application Support/MacNeutron/Valve").path(percentEncoded: false))
  }
  @Test func genuineSteamclientSupportIsTheRuntimesMarker() throws {
      let layout = try makeToolLayout()
      #expect(!layout.supportsGenuineSteamclient)
      try write("", to: layout.wineApp.appending(path: "Contents/Resources/genuine-steamclient"))
      #expect(layout.supportsGenuineSteamclient)
  }
  ```
  Also write the failing check.sh row. In `steamclient_cmd`, after the DLL check:
  ```sh
  [ -f "$TOOL/Contents/Resources/genuine-steamclient" ] \
    || { echo "FAIL steamclient: wine.app has no genuine-steamclient marker (bundle.sh writes it with Wine 0036)"; return 1; }
  ```
- [ ] **Step 2: Run them to see them fail.**
  - `swift test --filter PathsTests`: it fails to compile (no `valveCache` or `supportsGenuineSteamclient`).
  - `sh wine-arm64/check.sh steamclient`: it prints `FAIL steamclient: wine.app has no genuine-steamclient marker …`.
- [ ] **Step 3: Implement.**
  - `bundle.sh`:
    ```sh
    # Wine 0036 (genuine steamclient64.dll): the launcher deploys Valve's DLL only to a runtime that says so.
    if LC_ALL=C /usr/bin/grep -q setup_steamclient_trampolines "$B/wine-arm64-src/wine/dlls/ntdll/loader.c"; then
      echo 0036 > "$R/genuine-steamclient"
    fi
    ```
    Put it before signing, with the other Resources writes.
  - `check.sh`: when `$STEAMCLIENT64` doesn't exist, it takes the last match of
    `"$HOME/Library/Application Support/MacNeutron/Valve/"[0-9]*/steamclient64.dll`. The override and `build/valve` stay
    ahead of the cache.
  - The two `ToolLayout` properties, and the `makeToolLayout` root change.
- [ ] **Step 4: Run them to see them pass.**
  - `swift test` → all pass. The 290 tests from before are still there, plus the new ones.
  - Then `make wine-arm64`, which re-bundles because `bundle.sh` changed.
  - Then `sh wine-arm64/check.sh steamclient` → `PASS` for the step. `build/valve` is present on the maintainer's Mac.
- [ ] **Step 5: Commit.**
  `runtime: advertise Wine 0036 as genuine-steamclient; ToolLayout reads it and names the Valve cache`.

### Task 3: ValveClient (download, verify, extract, check, install, prune)

**Files:**
- Modify: `Sources/MacNeutronCore/PEImage.swift`. Add a certificate-table check.
- Create: `Sources/MacNeutronCore/ValveClient.swift`.
- Modify: `Tests/MacNeutronCoreTests/Support.swift`. `peBytes` gains a full header and a `certificate` flag.
- Create: `Tests/MacNeutronCoreTests/FakeValveCDN.swift`. Task 5 uses it too.
- Test: `Tests/MacNeutronCoreTests/PEImageTests.swift`, `Tests/MacNeutronCoreTests/ValveClientTests.swift`.

**Interfaces:**
- Consumes: `ProcessRunner` and `SystemProcessRunner` (existing), `PEImage.machine(of:)` and `KeyValues.parse`.
- Produces:
  ```swift
  extension PEImage {
      /// The optional header's Certificate Table (data directory 4) has a non-zero size: the file carries an
      /// Authenticode signature. Not a validity check.
      public static func hasCertificateTable(_ url: URL) -> Bool
  }
  public struct ValveManifest: Equatable, Sendable {
      public let version: String, file: String, size: Int64, sha2: String
      public static func parse(_ text: String) throws(ValveClientError) -> ValveManifest
  }
  public struct ValveInstall: Equatable, Sendable {
      public let version: String, folder: URL
      public var steamClient64: URL { folder.appending(path: "steamclient64.dll") }
  }
  public enum ValveUpdateOutcome: Equatable, Sendable { case installed(String), upToDate(String) }
  public enum ValveClientError: Error, Equatable, CustomStringConvertible {
      case badManifest(String), http(Int), sizeMismatch(expected: Int64, got: Int64), checksumMismatch,
           extractFailed(Int32), wrongMachine(String), unsigned(String)
  }
  public struct ValveClient: Sendable {
      public typealias Fetch = @Sendable (URL) async throws -> URL   // downloads to a temp file the caller owns
      public static let manifestURL: URL        // https://client-update.steamstatic.com/steam_client_win64
      public static let packageBase: URL        // https://client-update.steamstatic.com/
      public static let files: [(member: String, machine: UInt16)]   // the six, in the Global Constraints order
      public let cache: URL
      public init(cache: URL, fetch: @escaping Fetch = ValveClient.download, runner: any ProcessRunner = SystemProcessRunner())
      public static func download(_ url: URL) async throws -> URL
      public static func installed(in cache: URL) -> ValveInstall?
      public var removedByUser: Bool
      public func update(force: Bool) async throws -> ValveUpdateOutcome
      public func updateIfNeeded(appVersion: String) async throws -> ValveUpdateOutcome?
      public func remove() throws
  }
  ```
  `FakeValveCDN` is a `final class FakeValveCDN: @unchecked Sendable`:
  - `init(dir: URL, version: String, files: [String: Data]) throws` builds `files` (zip member → bytes) into a zip with
    `/usr/bin/ditto -c -k <folder> <zip>`, then writes a manifest for it. `sha2` and `size` are computed from the zip.
  - `var fetch: ValveClient.Fetch` serves the manifest URL and `packageBase/<file>` as fresh temp copies. It throws
    `ValveClientError.http(404)` for anything else, or for every URL while `offline` is true.
  - `var requests: [URL]`.
  - `var offline = false`.
  - `func replaceZip(files: [String: Data], version: String) throws` changes what it serves.
  - `static func sixFiles(certificate: Bool = true) -> [String: Data]` holds the six members with their right machines,
    built with `peBytes`.

**The cache folder:**
- `<version>/`: the six files flat, plus `source.txt` (`url <url>\nsha2 <hex>\ndate <ISO 8601>\n`).
- `checked`: the app version that last checked.
- `removed`: an empty marker meaning the player removed the files.
- `.staging-<UUID>/`: an install in progress.

**`update(force:)`, in order:**
1. Fetch and parse the manifest.
2. Unless `force`: if `installed(in:)`'s `source.txt` holds the manifest's `sha2`, return `.upToDate(version)` without
   fetching the zip.
3. Fetch the zip and check its size, then its SHA-256. Use CryptoKit, hashing in 1 MB chunks.
4. Extract into `.staging-<UUID>` with `/usr/bin/unzip -q -o -j <zip> <six members> -d <staging>`. A non-zero exit throws
   `extractFailed(status)`. unzip exits 11 when a named member is missing.
5. Check every file's machine and certificate table.
6. Write `source.txt`.
7. Move the staging folder into place:
   - `rename` it to `<version>` if that doesn't exist yet;
   - otherwise swap it in with `renamex_np(…, RENAME_SWAP)`, then remove what was swapped out.
8. Prune every entry except `<version>` and `checked`, which also removes `removed`.

Every failure removes the temp zip and the staging folder, and leaves the installed version untouched.

**`installed(in:)`:** the folder with the numerically highest all-digit name that holds `steamclient64.dll`.

**`updateIfNeeded(appVersion:)`:**
- Returns nil when `removedByUser` is set.
- Returns nil when a version is installed and `checked` holds `appVersion`.
- Otherwise it runs `update(force: false)`, then writes `appVersion` to `checked`.

**`remove()`:** empties the cache, then writes `removed`.

**`ValveManifest.parse`:**
- Reads the first root block, whatever its key.
- Takes that block's `version`, and from its `bins_win64` child the `file`, `size` and `sha2`.
- Refuses each of the following with `badManifest(<field>)`:
  - a missing field;
  - a `version` that isn't all digits;
  - a `file` outside `[A-Za-z0-9._-]+` or containing `..`;
  - a `sha2` that isn't 64 lowercase hex digits;
  - a `size` that isn't a positive integer.

- [ ] **Step 1: Write the failing tests.**
  - `PEImageTests.certificateTableIsSeenInPE32AndPE32Plus`: `peBytes(machine: PEImage.i386, certificate: true)` and
    `peBytes(machine: PEImage.amd64, certificate: true)` → true; `certificate: false` → false; a truncated file → false.
  - `peBytes` layout:
    - `e_lfanew` is 0x80.
    - `SizeOfOptionalHeader` is 0xE0 (PE32, for i386) or 0xF0 (PE32+).
    - The magic is 0x10B or 0x20B.
    - `NumberOfRvaAndSizes` is 16, at optional+92 (PE32) or optional+108 (PE32+).
    - Data directory 4 is at optional+128 (PE32) or optional+144 (PE32+). With `certificate`, it holds offset 0x400
      and size 0x10.
    - The old 0x86-byte prefix stays the same, so the existing truncation tests hold.
  - `ValveClientTests`. Each uses `FakeValveCDN` and a fresh `cache = makeTempDir()/Valve`:
    - `installsTheSixFilesFlatWithTheirSource`:
      - `update(force: false)` is `.installed("1788652215")`;
      - `installed(in:)` is that version;
      - all six names are in the folder (`SteamService.exe` with no `bin/`);
      - `source.txt` holds the package URL and the `sha2`;
      - `requests` is `[manifest, package]`.
    - `aChecksumOrSizeMismatchInstallsNothing`: the manifest's `sha2` is changed by one digit → `checksumMismatch`; its
      `size` changed → `sizeMismatch`. In both cases `installed(in:)` is nil and the cache holds no `.staging-*`.
    - `aWrongMachineIsRefused`: `steamclient.dll` built as AMD64 → `wrongMachine("steamclient.dll")`.
    - `anUnsignedFileIsRefused`: `sixFiles(certificate: false)` → `unsigned("steamclient64.dll")`. This is the first
      file checked.
    - `aFailedUpdateKeepsTheInstalledVersion` (Review Focus 2):
      - install version 1;
      - `replaceZip` with version 2 and no `Steam.dll` → `extractFailed(11)`;
      - `installed(in:)` is still version 1, with its six files.
    - `aNewSha2ReplacesAndPrunesTheOldVersion`: install 1, then `replaceZip` with version 2 → `.installed("2")`. The
      cache's entries are exactly `["2"]`.
    - `theSameSha2IsUpToDateWithoutADownload`: a second `update(force: false)` → `.upToDate`, and `requests` grows by the
      manifest only.
    - `forceDownloadsTheSameVersionAgain`: `update(force: true)` fetches the package again and still installs. It
      replaces the folder in place.
    - `aStagingFolderIsNeverInstalledAndIsPruned` (Review Focus 4):
      - a `.staging-x/steamclient64.dll` in the cache → `installed(in:)` is nil;
      - after an update, `.staging-x` is gone.
    - `updateIfNeededChecksOncePerAppVersion`:
      - `("1.0")` installs;
      - `("1.0")` again → nil, with no request;
      - `("1.1")` → `.upToDate`, with one manifest request.
    - `removeSticksUntilAForcedUpdate`:
      - `remove()` → `installed` is nil and `removedByUser` is true;
      - `updateIfNeeded("2.0")` → nil, with no request;
      - `update(force: true)` installs and clears `removedByUser`.
    - `manifestFieldsThatCouldEscapeTheCacheAreRefused` (Review Focus 5): `version` `../1`, `file` `a/b.zip`, `file`
      `..`, a 63-digit `sha2`, and a manifest with no `bins_win64` each throw `badManifest`.
    - `manifestParsesValvesLayout`: a manifest with other package blocks before and after `bins_win64` (each with
      `file`, `size`, `sha2`, `zipvz` and `sha2vz`) → the `bins_win64` values.
    - `offlineIsAnError`: `offline = true` → throws, and nothing is installed.
- [ ] **Step 2: Run them to see them fail.** `swift test --filter 'PEImageTests|ValveClientTests'`. Expected: a compile
  failure (no `hasCertificateTable` or `ValveClient`).
- [ ] **Step 3: Implement** `PEImage.hasCertificateTable` and `ValveClient.swift`, as in the Interfaces above.
  - The default `download` uses `URLSession(configuration: .ephemeral)` and `download(from:)`. Any status other than 200
    throws `http(status)`. It moves the file into a fresh temp folder before returning.
  - Write this ponytail comment at the prune: `// ponytail: a launch copying from the pruned folder at that instant fails
    once; the next launch uses the new one.`
- [ ] **Step 4: Run them to see them pass.** `swift test` → all pass.
- [ ] **Step 5: Commit.**
  `ValveClient: fetch Valve's Windows client files from Valve's CDN into a verified, versioned cache`.

### Task 4: The prefix gets Valve's steamclient64.dll

**Files:**
- Modify: `Sources/MacNeutronCore/PrefixManager.swift`, `deploySteamBridge` (`:180-197`).
- Modify: `Sources/MacNeutronCore/SteamBridge.swift`. Change the `steamClient64Variable` doc comment (`:17-19`): it is no
  longer a spike, and it says what `builtin` does.
- Modify: `README.md`, "Per-game options", with a new row after `MACNEUTRON_NO_STEAM_BRIDGE`.
- Test: `Tests/MacNeutronCoreTests/PrefixManagerTests.swift`.

**Interfaces:**
- Consumes: `ToolLayout.supportsGenuineSteamclient` and `.valveCache` (Task 2); `ValveClient.installed(in:)` and
  `ValveInstall.steamClient64` (Task 3).
- Produces: no new API. These are the launcher.log lines, exact, one per launch with the bridge:
  - `note: steamclient64.dll from <path> (MACNEUTRON_STEAMCLIENT64)` (existing)
  - `note: steamclient64.dll is lsteamclient (MACNEUTRON_STEAMCLIENT64=builtin)`
  - `note: steamclient64.dll from Valve's client <version> (<path>)`
  - `note: steamclient64.dll is lsteamclient: Valve's isn't downloaded`
  - `note: steamclient64.dll is lsteamclient: this runtime has no genuine-steamclient marker`

- [ ] **Step 1: Write the failing tests.** Use a helper `cacheValveClient(_ layout:, version:)` that writes
  `layout.valveCache/<version>/steamclient64.dll` as `peBytes(machine: PEImage.amd64, certificate: true)`.
  - `theCachedValveDLLGoesInThePrefixWhenTheRuntimeSupportsIt`: marker plus cache → the prefix's `steamclient64.dll`
    equals the cached bytes, and the log has the "from Valve's client" line.
  - `withoutTheMarkerTheCacheIsIgnored`: cache only → lsteamclient, plus the "no genuine-steamclient marker" line.
  - `withoutACacheItIsLsteamclient`: marker only → lsteamclient, plus the "isn't downloaded" line.
  - `aCacheWithoutSteamclient64FallsBackToLsteamclient` (Review Focus 3): marker, and a cache version folder holding only
    `source.txt` → `prepare` doesn't throw, the prefix gets lsteamclient, and the log has the "isn't downloaded" line.
  - `builtinForcesLsteamclientOverTheCache`: marker, cache, and `MACNEUTRON_STEAMCLIENT64=builtin` → lsteamclient, plus
    the builtin line. Then the next launch without it → Valve's.
  - `aPathOverrideBeatsTheCache`: marker, cache, and `=<path>` to a different AMD64 file → that file.
  - The two existing override tests stay unchanged.
- [ ] **Step 2: Run them to see them fail.** `swift test --filter PrefixManagerTests`. Expected: 5 failures.
  - The cache row gets lsteamclient bytes.
  - The builtin row throws, because `builtin` is read as a path today.
  - The three fallback rows lack their log lines.
  - `aPathOverrideBeatsTheCache` already passes.
- [ ] **Step 3: Implement** the order in `deploySteamBridge`:
  1. A non-empty override: `builtin` → `nil` (lsteamclient); anything else → today's path check.
  2. If `layout.supportsGenuineSteamclient`, then `ValveClient.installed(in: layout.valveCache)`, whose `steamClient64`
     must exist.
  3. lsteamclient.

  Log the matching line, and keep `prefixFiles` and `install` unchanged.

  README row: `/usr/bin/env MACNEUTRON_STEAMCLIENT64=builtin %command%`: put MacNeutron's own Steam bridge in the
  prefix instead of Valve's signed steamclient64.dll. Some games with Valve's DRM then stop with "Application load
  error 3". `=<path>` names another copy of Valve's DLL.
- [ ] **Step 4: Run them to see them pass.** `swift test` → all pass.
- [ ] **Step 5: Commit.**
  `launcher: Valve's signed steamclient64.dll in every prefix when the runtime supports it (override, cache, builtin)`.

### Task 5: The app downloads without asking, and Settings shows it

**Files:**
- Modify: `Sources/MacNeutronApp/AppModel.swift`, in the init (`:102-122`) and `installRuntime` (`:187-219`), plus a new
  `// MARK: Valve` section.
- Modify: `Sources/MacNeutronApp/SettingsView.swift`. A row after "Runtime" (`:21-25`).
- Test: `Tests/MacNeutronCoreTests/AppModelTests.swift`.

**Interfaces:**
- Consumes: `ValveClient` (Task 3), `ToolLayout.supportsGenuineSteamclient` and `.valveCache` (Task 2), and
  `FakeValveCDN` (Task 3).
- Produces, in `AppModel`:
  - New init parameters, last: `valveClient: ValveClient? = nil`, which defaults to `ValveClient(cache: layout.valveCache)`,
    and `appVersion: String = AppModel.appVersion`, which is `Bundle.main`'s `CFBundleVersion`, or `"dev"`.
  - `private(set) var valveVersion: String?`: `ValveClient.installed(in:)`'s version, read at init and after every
    change.
  - `private(set) var valveNotice: String?`: `"Downloading Steam components from Valve…"` while working, the error's
    `description` after a failure, and nil otherwise.
  - `private(set) var valveUpdating = false`.
  - `private(set) var valveTask: Task<Void, Never>?`: the automatic check that a successful install starts.
  - `func updateValveClient(force: Bool) async`: one at a time. It runs `force ? update(force: true) :
    updateIfNeeded(appVersion:)` off the main actor.
  - `func removeValveClient()`.

Behaviour:
- After the runtime install succeeds (`.installed` or `.unchanged`, never `.deferred` or failed), and only when
  `layout.supportsGenuineSteamclient`, `installRuntime` sets `valveTask = Task { await updateValveClient(force: false) }`.
- A Valve failure never sets `runtimeNotice` or `errorMessage`. It only sets `valveNotice`.

- [ ] **Step 1: Write the failing tests.** Use `makeToolLayout()` plus the marker, `wineAppSource:
  try makeToolLayout().wineApp`, `ScriptedInstaller(.unchanged)` with no effect (so the marker stays), and
  `FakeValveCDN`. Await `installTask` then `valveTask`.
  - `startupDownloadsValvesFilesWhenTheRuntimeSupportsThem`: `valveVersion == "1788652215"`, `valveNotice == nil`, and
    the requests are `[manifest, package]`.
  - `noMarkerNoDownload`: no marker → `requests` is empty and `valveVersion` is nil.
  - `valveFailureLeavesTheRuntimeInstalled` (Review Focus 1): `offline` → `runtime` is set, `runtimeNotice` is nil,
    `errorMessage` is nil, `valveVersion` is nil, and `valveNotice` is non-nil. Then a second model on the same layout
    and cache, standing for the next app start, with `offline = false` → it installs.
  - `removeSticksAcrossAppStarts`: download, then `removeValveClient()` → `valveVersion` is nil. A new model on the same
    cache → no new requests. Then `updateValveClient(force: true)` → installed again.
  - `aNewAppVersionChecksAgain`: download with `appVersion: "1"`; a new model with `"2"` → one more manifest request
    (`upToDate`, no package).
- [ ] **Step 2: Run them to see them fail.** `swift test --filter AppModelTests`. Expected: a compile failure (no
  `valveClient:` parameter).
- [ ] **Step 3: Implement** the AppModel part.
  - SettingsView:
    ```swift
    LabeledContent("Steam components from Valve") {
        HStack {
            Text(model.valveVersion.map { "Version \($0)" } ?? "Not downloaded").foregroundStyle(.secondary)
            Button("Download again") { Task { await model.updateValveClient(force: true) } }.disabled(model.valveUpdating)
            Button("Remove") { model.removeValveClient() }.disabled(model.valveVersion == nil || model.valveUpdating)
        }
    }
    if let notice = model.valveNotice { Text(notice).foregroundStyle(.secondary).textSelection(.enabled) }
    ```
  - Comment at the automatic check: `// D2: no question asked; the files come from Valve's CDN to this Mac, as Linux
    Steam's legacycompat does for Proton (macOS Steam has no legacycompat).`
- [ ] **Step 4: Run them to see them pass.** `swift test` → all pass. `swift build -c release` is clean.
- [ ] **Step 5: Commit.**
  `app: download Valve's Steam components without asking after a runtime install; Settings shows, refreshes and removes them`.

### Task 6: Acceptance

**Files:**
- Create: `docs/testing/acceptance-valve-steamclient.md`.
- Modify: `README.md`. The "Per-game options" row from Task 4, and one paragraph under the Steam bridge text. Find the
  section with `/usr/bin/grep -n "Steam bridge" README.md`. The paragraph says:
  - the app downloads Valve's signed Windows client files from Valve (about 64 MB) when the runtime supports them;
  - they stay on the Mac and are never in MacNeutron's downloads;
  - Settings shows, refreshes and removes them.

- [ ] **Step 1: Milestone gates.**
  - `swift test`: all pass.
  - `sh release/release.sh --self-test`: `PASS release self-test`.
  - `sh wine-arm64/check.sh steamclient`: `PASS`.
  - `make bridge-check`: all pass, which shows SMITE 2's bridge path is unchanged.
  Redirect long output to `build/valve-steamclient/logs/` and read the tails.
- [ ] **Step 2: Build the dev app.** `make app APP=build/dev/MacNeutron.app` with the signing variables. This build also
  fixes the tool-name regression (parity Part A item 0), so after it the mappings are counted.
- [ ] **Step 3: Write the acceptance doc**, with the gate results and these maintainer rows, filled from the
  maintainer's session:
  1. Quit MacNeutron and open `build/dev/MacNeutron.app`. Settings shows "Version <n>" with no question asked, and
     `~/Library/Application Support/MacNeutron/Valve/<n>/` holds the six files and `source.txt`.
  2. Remove Palworld's `MACNEUTRON_STEAMCLIENT64` launch option and start it. It reaches the title screen, and
     launcher.log has `steamclient64.dll from Valve's client <n>`.
  3. SMITE 2 still runs with the bridge.
  4. Settings → Remove, then start Palworld. launcher.log says `is lsteamclient: Valve's isn't downloaded`, and the
     SteamStub error is back. Then Download again, and Palworld starts again.
  5. Steam's mappings: `macneutron-proton` for every Windows game and none for `macneutron` (parity §0).
- [ ] **Step 4: Privacy scan, then commit.** Write home as `~`, with no SteamIDs or account names. The scan is
  `/usr/bin/grep -nE "/Users/|steamid|[0-9]{17}" docs/testing/acceptance-valve-steamclient.md README.md`, which must
  print nothing. Commit:
  `docs: Valve's steamclient64.dll acceptance and README`.
- [ ] **Step 5: Ask the maintainer to run rows 1-5.** Record the results in the acceptance doc and commit them.
