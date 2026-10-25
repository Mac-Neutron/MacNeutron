# Install MacNeutron Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every release ships `MacNeutron-<V>-Installer.zip`, whose `Install MacNeutron.app` quits MacNeutron and replaces `/Applications/MacNeutron.app` with the release's app; the app's "Install and relaunch" uses it when it can't replace itself.

**Architecture:** The install logic is `AppInstaller` in MacNeutronCore (verify, quit and the target injected, so it's tested on temp folders); a new executable target `MacNeutronInstaller` is its AppKit front (alerts, quitting by bundle id, opening the result). `Updater` learns the installer asset and an `update` entry point that picks the swap or the installer; `release.sh` builds, signs, notarizes, zips and checks the installer after R2.

**Tech Stack:** Swift 6 (SwiftPM, Swift Testing), AppKit, Security; POSIX sh (release.sh), codesign, notarytool, ditto.

**Spec:** `docs/superpowers/specs/2026-10-25-macneutron-installer-design.md`

## Global Constraints

- Asset names: `MacNeutron-<V>.zip` and `SHA256SUMS` unchanged (older apps' updaters read them); new `MacNeutron-<V>-Installer.zip` (`MacNeutron-<V>p-Installer.zip` for a p release).
- Bundle identifiers: the app `io.github.chadouming.MacNeutron` (`Updater.identifier`), the installer `io.github.chadouming.MacNeutron.Installer`.
- The installer bundle is `Install MacNeutron.app`, executable `Contents/MacOS/Install MacNeutron`, the release's app at `Contents/Helpers/MacNeutron.app`.
- Install target `/Applications/MacNeutron.app`; staging names beside it `.MacNeutron.app.installing` and `.MacNeutron.app.old`.
- `LSMinimumSystemVersion` 27.0; binaries thin arm64, `strip -S`, no `/Users/` path.
- Tests never touch the real `/Applications` or quit a real app; `swift test` stays green (516 tests before this plan).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; no pushes; no hook bypasses.

## Review Focus

1. The app's update path quits MacNeutron before the installer looks: no MacNeutron running must count as quit (Task 2's `quitMacNeutron` returns true for none).
2. A crash between the swap and the old app's removal leaves `.MacNeutron.app.old`; the next run must clear both staging names first (Task 1 test `leftoversOfAnInterruptedInstallAreCleared`).
3. The installer runs from a read-only place (translocation, a mounted image): it only reads its bundled app (Task 1 test `aReadOnlySourceInstalls`).
4. The account can't write to the target's folder: nothing is quit or copied (Task 1 test `anUnwritableFolderChangesNothing`).
5. A release without an installer, opened from Downloads: the message must say where the app runs from (Task 3 test `updateWithoutAnInstallerSaysWhereTheAppRuns`).

---

### Task 1: AppInstaller

**Files:**
- Create: `Sources/MacNeutronCore/AppInstaller.swift`
- Test: `Tests/MacNeutronCoreTests/AppInstallerTests.swift`

**Interfaces:**
- Consumes: `Updater.swap(_ new: URL, into app: URL, aside: URL) throws` (existing), `SystemProcessRunner` (existing).
- Produces:
  - `public enum AppInstaller { public static let target: URL /* /Applications/MacNeutron.app */; public static func install(_ bundled: URL, into target: URL, verify: (URL) throws -> Void, quit: () -> Bool) throws }`
  - `public enum AppInstallerError: Error, Equatable, CustomStringConvertible { case notWritable(String); case stillRunning }`, descriptions exactly:
    - `notWritable(folder)`: `"Your account can't write to \(folder): drag MacNeutron.app there yourself, or use an administrator account."`
    - `stillRunning`: `"MacNeutron didn't quit within 30 seconds: quit it, then open Install MacNeutron again."`

`install` order (spec §3.2 steps 2-5): `verify(bundled)`; the target's folder writable, else `notWritable(<folder path, no trailing />)`; `quit()` true, else `stillRunning`; remove leftover `.MacNeutron.app.installing` and `.MacNeutron.app.old` beside the target; `ditto` the bundled app to `.MacNeutron.app.installing`; remove `com.apple.quarantine` from every file and folder in the copy (`removexattr` with `XATTR_NOFOLLOW`, `ENOATTR` ignored); `Updater.swap` into the target (aside `.MacNeutron.app.old`), or a move when there's no target; remove `.MacNeutron.app.old`. Any throw before the swap leaves the target as it was; the staging copy is removed on the way out. A failed swap's roll-back is `Updater.swap`'s, already tested (`swapPutsTheNewAppInPlaceAndRollsBack`).

Test helpers (in the test file): `bundle(at: URL, version: String) -> URL` writes `<dir>/MacNeutron.app/Contents/v` containing the version; `quarantine(_ url: URL)` sets `com.apple.quarantine` to `"0081;00000000;Test;"` with `setxattr`; `hasQuarantine(_ url: URL) -> Bool`.

- [ ] **Step 1: Write the failing tests**

```swift
@Test func installsFreshAndStripsQuarantine()        // no target: target/Contents/v == "new"; hasQuarantine false for the
                                                      // bundle and Contents/v (both quarantined in the source); verify got
                                                      // the bundled URL once; quit called once; no staging names left
@Test func replacesTheAppInApplications()            // target v "old" -> "new"; no .MacNeutron.app.old left
@Test func aFailedVerifyChangesNothing()             // verify throws UpdateError.signature("x"): that error; quit never
                                                      // called; target v "old"; no staging names
@Test func anAppThatWontQuitChangesNothing()         // quit returns false: AppInstallerError.stillRunning; target v "old";
                                                      // no .MacNeutron.app.installing
@Test func anUnwritableFolderChangesNothing()        // target's folder chmod 0o555 (restored in defer):
                                                      // .notWritable(<folder path>); quit never called
@Test func leftoversOfAnInterruptedInstallAreCleared() // junk in both staging names: installed, both gone
@Test func aReadOnlySourceInstalls()                 // bundled's parent chmod 0o555 (restored in defer): installed
@Test func installerMessagesAreExact()               // both descriptions, word for word (Interfaces above)
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter AppInstallerTests`
Expected: compile failure, `cannot find 'AppInstaller' in scope`.

- [ ] **Step 3: Implement `AppInstaller` and `AppInstallerError` in `Sources/MacNeutronCore/AppInstaller.swift`**

The doc comment names the spec section; the quarantine walk uses `FileManager.enumerator(atPath:)` plus the bundle itself.

- [ ] **Step 4: Run them to see them pass, then the whole suite**

Run: `swift test --filter AppInstallerTests` then `swift test`
Expected: 8 passed; then the whole suite passes.

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/AppInstaller.swift Tests/MacNeutronCoreTests/AppInstallerTests.swift
git commit -m "MacNeutronCore: AppInstaller, the installer's verify, quit, copy and swap into Applications"
```

### Task 2: The installer app

**Files:**
- Modify: `Package.swift` (product and `.executableTarget(name: "MacNeutronInstaller", dependencies: ["MacNeutronCore"])`)
- Create: `Sources/MacNeutronInstaller/main.swift`, `App/Installer-Info.plist`
- Modify: `Sources/MacNeutronCore/Updater.swift` (`verify` public, with an identifier)

**Interfaces:**
- Consumes: Task 1's `AppInstaller.install`, `AppInstaller.target`; `CodeIdentity.team(of:)`.
- Produces:
  - `public static let installerIdentifier = "io.github.chadouming.MacNeutron.Installer"` on `Updater`.
  - `public static func verify(_ bundle: URL, team: String, version: String, log: URL, identifier: String = Updater.identifier) throws` (was internal, fixed identifier); its missing-bundle detail becomes `"the download holds no \(bundle.lastPathComponent)"`.
  - The executable `.build/release/MacNeutronInstaller`, argument `--from-app` (no confirmation).

`App/Installer-Info.plist`: `CFBundleIdentifier` the installer's, `CFBundleName` and `CFBundleDisplayName` `Install MacNeutron`, `CFBundleExecutable` `Install MacNeutron`, `CFBundlePackageType` `APPL`, `CFBundleShortVersionString` `0.1.0`, `CFBundleVersion` `1` (release.sh sets both), `LSMinimumSystemVersion` `27.0`, `NSHighResolutionCapable` true.

`main.swift` (spec §3.2): regular activation policy and activate; `bundled` = `Bundle.main.bundleURL/Contents/Helpers/MacNeutron.app`; `version` = its own `CFBundleShortVersionString`; team = `CodeIdentity.team(of: Bundle.main.bundleURL)`, nil: alert `"This installer isn't signed by MacNeutron's developer."` and exit 1. Without `--from-app`, an `NSAlert` `"Install MacNeutron \(version) in Applications?"`, informative text `"MacNeutron will quit."`, buttons Install and Cancel (Cancel: exit 0). Then `AppInstaller.install(bundled, into: AppInstaller.target, verify: { try Updater.verify($0, team:, version:, log: <temp dir>/install-macneutron-spctl.txt) }, quit: quitMacNeutron)`; a throw: an alert with the error's `description` (`CustomStringConvertible`, else `localizedDescription`), exit 1. Success: `/usr/bin/open /Applications/MacNeutron.app`, exit 0. `quitMacNeutron() -> Bool`: `terminate()` every `NSRunningApplication` with `Updater.identifier`, run the run loop in 0.2 s steps until all are terminated or 30 s pass; true when all are (none running is true).

- [ ] **Step 1: Make `verify` public with the identifier parameter; rerun the updater tests**

Run: `swift test --filter UpdaterTests`
Expected: pass (`installRefusesAndChangesNothing` still sees a signature refusal that isn't the missing-bundle one).

- [ ] **Step 2: Add the target, `main.swift` and `App/Installer-Info.plist`; build it**

Run: `swift build -c release --product MacNeutronInstaller && lipo -archs .build/release/MacNeutronInstaller`
Expected: builds; `arm64`.

The executable's alerts need a person: Task 4's rehearsal builds and signs it, Task 5 runs it.

- [ ] **Step 3: Run the whole suite and commit**

Run: `swift test` (expected: all pass), then:

```bash
git add Package.swift Sources/MacNeutronInstaller/main.swift App/Installer-Info.plist Sources/MacNeutronCore/Updater.swift
git commit -m "Install MacNeutron: the installer app, AppInstaller's AppKit front"
```

### Task 3: The updater uses the installer

**Files:**
- Modify: `Sources/MacNeutronCore/Updater.swift`, `Sources/MacNeutronApp/UpdateModel.swift`
- Test: `Tests/MacNeutronCoreTests/UpdaterTests.swift`

**Interfaces:**
- Consumes: Task 2's `Updater.verify(…, identifier:)`, `Updater.installerIdentifier`.
- Produces:
  - `Release.installer: URL?` (`public var installer: URL? = nil`, so existing initializers compile).
  - `UpdateError.cantReplace(String)` (the app's folder), description exactly `"MacNeutron is running from \(folder), which it can't replace. Download Install MacNeutron from the release page, or move MacNeutron to Applications."`
  - `public static func installer(_ release: Release, team: String, fetch: ValveClient.Fetch? = nil) async throws -> URL`: downloads `release.installer` and `SHA256SUMS`, checks the line for `MacNeutron-<V>-Installer.zip`, unpacks with ditto into a new temporary folder, `verify(…, identifier: installerIdentifier)` with the release's version; returns `<folder>/new/Install MacNeutron.app`. A failure removes the folder.
  - `public enum UpdateOutcome: Equatable { case swapped(work: URL); case installer(URL) }` and `public static func update(_ release: Release, app: URL, team: String, fetch: ValveClient.Fetch? = nil) async throws -> UpdateOutcome`: `.installer(...)` when `!canReplace(app)` and the release has an installer, else `.swapped(work: install(...))`.

`pick` sets `installer` from the asset `MacNeutron-<V>-Installer.zip` when its link is https (the release is still offered without one). `install` throws `cantReplace(app.deletingLastPathComponent().path(percentEncoded: false))` without a trailing `/`. The download, checksum and unpack in `install` and `installer` are one private helper. `UpdateModel.install()` calls `update`: `.swapped(let work)` relaunches as now; `.installer(let bundle)` opens it with `NSWorkspace.shared.openApplication(at:configuration:)` (arguments `["--from-app"]`, awaited); both then `NSApplication.shared.terminate(nil)`.

- [ ] **Step 1: Write the failing tests in `UpdaterTests.swift`**

```swift
@Test func pickRecordsTheInstallerAsset()
// assets [zip, SHA256SUMS, "MacNeutron-0.9.2-Installer.zip"]: installer?.lastPathComponent == "MacNeutron-0.9.2-Installer.zip";
// without it: installer == nil and the release is still picked; the installer link alone made http (replace
// "https://example.invalid/v0.9.2/MacNeutron-0.9.2-Installer.zip" in the JSON): installer == nil, release picked

@Test func updateFromATranslocatedAppTakesTheInstaller() async throws
// app /private/var/folders/x/AppTranslocation/y/d/MacNeutron.app; the installer zip is an ad hoc signed bundle renamed
// "Install MacNeutron.app" (makeSignedWineApp), zipped as release.sh zips, SHA256SUMS right; the fake fetch records URLs:
// throws UpdateError.signature with detail != "the download holds no Install MacNeutron.app"; fetched exactly
// [installer URL, SHA256SUMS URL]

@Test func updateWithoutAnInstallerSaysWhereTheAppRuns() async throws
// same app, installer nil: throws UpdateError.cantReplace("/private/var/folders/x/AppTranslocation/y/d") whose
// description is the exact text above; fetch never called
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter UpdaterTests`
Expected: compile failure (`installer`, `update`, `cantReplace(String)` don't exist).

- [ ] **Step 3: Implement the Interfaces above in `Updater.swift` and wire `UpdateModel.install()`**

- [ ] **Step 4: Run them to see them pass, then the whole suite**

Run: `swift test --filter UpdaterTests` then `swift test`
Expected: pass; all pass.

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/Updater.swift Sources/MacNeutronApp/UpdateModel.swift Tests/MacNeutronCoreTests/UpdaterTests.swift
git commit -m "Updater: a translocated app updates through the release's installer, and says where it runs otherwise"
```

### Task 4: The release builds the installer

**Files:**
- Modify: `release/release.sh`

**Interfaces:**
- Consumes: `.build/release/MacNeutronInstaller` and `App/Installer-Info.plist` (Task 2); `notarize_and_staple`, `accepted`, `die` (release/lib.sh).
- Produces: `$OUT/MacNeutron-$V-Installer.zip` (`IZIP`), in `SHA256SUMS`, `SIZES.txt` and the printed `gh release create` command.

Changes (spec §3.4):
- The "run make build bridge" input loop also requires `$ROOT/.build/release/MacNeutronInstaller`.
- `check_zip <zip> <bundle>...` replaces the fixed one: unzips `<zip>` with `/usr/bin/unzip`; each bundle, in the order given, passes `codesign --verify --strict --deep` (its output to stderr, then `die "the unzipped <bundle> fails codesign --verify --strict --deep"`, one line) and, when `$mode` is release, `stapler validate`; then `PASS <zip name> unzips (unzip) to bundles that verify: <bundles>`. The app's call: `check_zip "$OUT/$ZIP" MacNeutron.app MacNeutron.app/Contents/Helpers/wine.app`.
- `build_installer` after the app's zip check: `I="$OUT/Install MacNeutron.app"`; plist copied and versioned with PlistBuddy, `LSMinimumSystemVersion` 27.0 checked; the binary copied, `lipo -archs` arm64, `strip -S`, no `/Users/`; `ditto "$A" "$I/Contents/Helpers/MacNeutron.app"`; `codesign -f -s "$MACNEUTRON_SIGN_IDENTITY" --options runtime --timestamp "$I"` (not `--deep`); `codesign --verify --strict --deep "$I"`. In a release: `notarize_and_staple "$I" "$OUT/notary-installer"` and `accepted "$I"` (else die `R2: Gatekeeper rejects $I`), `PASS the installer accepted`. Then `ditto -c -k --norsrc --keepParent "$I" "$OUT/$IZIP"` and `check_zip "$OUT/$IZIP" "Install MacNeutron.app/Contents/Helpers/MacNeutron.app" "Install MacNeutron.app"`.
- `shasum` and the `SIZES.txt` loop include `$IZIP`; the printed `gh release create` lists `'$OUT/$IZIP'` before `SHA256SUMS`.

- [ ] **Step 1: Add the failing self-test row**

In `self_test`: an ad hoc signed `Install MacNeutron.app` (plist, `/usr/bin/true` as executable) holding an ad hoc signed `Contents/Helpers/MacNeutron.app`, nested signed first; then a byte appended to the nested app's `Contents/Info.plist`; zipped with `ditto -c -k --norsrc --keepParent`. Row:
`refused "an installer whose nested app fails to verify" "release: the unzipped Install MacNeutron.app/Contents/Helpers/MacNeutron.app fails codesign --verify --strict --deep" check_zip "$T/installer.zip" "Install MacNeutron.app/Contents/Helpers/MacNeutron.app" "Install MacNeutron.app"` (with `mode=rehearse` set in the row's subshell).

Run: `sh release/release.sh --self-test`
Expected: `FAIL an installer whose nested app fails to verify` (today's `check_zip` takes no arguments).

- [ ] **Step 2: Implement the changes above; rerun the self-test**

Run: `sh release/release.sh --self-test`
Expected: `PASS release self-test` (rerun once if only "a VM manifest naming other parts" fails: a known 1-in-16 flake).

- [ ] **Step 3: Rehearse the whole release**

Run, with the signing variables, `sh release/release.sh --rehearse 99.0.0` in the background (about 15 minutes).
Expected: every `PASS` line of a release but L6's and R2's (a rehearsal doesn't notarize), plus the installer zip's `check_zip` PASS; a p rehearsal isn't needed (the same code with `V=X.Y.Zp`); `build/release/rehearse-99.0.0/MacNeutron-99.0.0-Installer.zip` listed in `SHA256SUMS` and `SIZES.txt`.

- [ ] **Step 4: Commit**

```bash
git add release/release.sh
git commit -m "release: Install MacNeutron, signed, notarized and zipped beside the app"
```

### Task 5: By hand (the maintainer, at the next release)

- [ ] With MacNeutron running (from Downloads or Applications), open `Install MacNeutron.app` from the unzipped `MacNeutron-<V>-Installer.zip`: one confirmation, MacNeutron quits, `/Applications/MacNeutron.app` is version `<V>` and opens without the downloaded-from-the-Internet prompt.
- [ ] From a MacNeutron opened from Downloads, Settings' "Install and relaunch" for the next release does the same with no confirmation.
