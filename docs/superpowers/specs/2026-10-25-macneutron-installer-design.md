# Install MacNeutron: an installer download that replaces the app in Applications

Status: proposed 2026-10-25 (approach A, self-contained; quarantine stripped after verification).
Amends: the in-app updater (`Sources/MacNeutronCore/Updater.swift`, `Sources/MacNeutronApp/UpdateModel.swift`) and the
release (`release/release.sh`, arm64 release spec §6.3).

## 1. Goal

A player who opened MacNeutron from Downloads runs a translocated copy (`/private/var/folders/…/AppTranslocation/…`).
Settings' "Install and relaunch" then stops at "Move MacNeutron to Applications to update it." without saying where the
app is, and `/Applications/MacNeutron.app`, if any, keeps an old version (the maintainer's was 0.1.0 under 0.9.0p).

Each release gets an installer: opened by hand or by the app's update, it quits MacNeutron and replaces
`/Applications/MacNeutron.app` with the release's app, then opens it.

Success:
- Opening `Install MacNeutron.app` from Downloads while MacNeutron runs (translocated or from Applications) ends with
  that release's MacNeutron running from `/Applications`, after one confirmation.
- "Install and relaunch" from a translocated copy does the same with no confirmation.
- Every failure leaves `/Applications/MacNeutron.app` as it was and says why.

## 2. Approaches

- **A, self-contained (chosen).** `MacNeutron-<V>-Installer.zip` holds `Install MacNeutron.app`, a signed, notarized
  app with the release's notarized `MacNeutron.app` inside. What is downloaded is what is installed; it works offline;
  Gatekeeper checks one notarized bundle. Cost: one more asset of about the app zip's size per release.
- B, a small bootstrap that downloads its channel's latest zip itself: a tiny asset, but its own download and
  verification code, GitHub at install time, and "latest" rather than the version chosen.
- A `.pkg` (Installer.app with a pre-install script): needs a Developer ID Installer certificate, which the maintainer
  doesn't have.

## 3. Design

### 3.1 The installer bundle

`Install MacNeutron.app`:
- `Contents/Info.plist` from `App/Installer-Info.plist`: `CFBundleIdentifier` `io.github.chadouming.MacNeutron.Installer`,
  `CFBundleShortVersionString` and `CFBundleVersion` the release's version (set by release.sh), the app's
  `LSMinimumSystemVersion`.
- `Contents/MacOS/Install MacNeutron`: the `MacNeutronInstaller` executable target (AppKit, no window but its alerts).
- `Contents/Helpers/MacNeutron.app`: the release's notarized, stapled app, copied with ditto (its signature, ticket and
  nested wine.app stay as they are, as `Contents/Helpers/wine.app` does in the app).

### 3.2 What it does

1. **Confirm**, unless launched with `--from-app`: "Install MacNeutron <V> in Applications? MacNeutron will quit."
   (Install, Cancel). Cancel quits with nothing changed.
2. **Verify** the bundled app with `Updater.verify` (Developer ID team `49QMZXLR8S`, identifier
   `io.github.chadouming.MacNeutron`, the installer's own version, Gatekeeper). A failure is an alert, nothing changed.
3. **Quit MacNeutron**: every running application with the app's bundle identifier is asked to terminate
   (`NSRunningApplication.terminate`), then waited for, up to 30 s. One still running: an alert ("MacNeutron didn't
   quit…"), nothing changed. (MacNeutron has no quit handler: a request ends it at once; a running vm-setup is stopped
   and resumes at the next launch. Games run from the tool folder, not the app bundle, and aren't touched.)
4. **Copy** the bundled app to `/Applications/.MacNeutron.app.installing` (same volume as the target; a leftover of an
   interrupted install is removed first), then **remove its quarantine** (`com.apple.quarantine`, recursively): the
   copy inherits it from the downloaded installer, and it was verified in step 2.
5. **Swap** it in with `Updater.swap` (the old app aside to `/Applications/.MacNeutron.app.old`, the new one in; a
   failed move puts the old one back). No old app: a plain move.
6. **Remove** the old app, **open** `/Applications/MacNeutron.app`, quit.

`/Applications` not writable by the account: an alert ("Your account can't write to /Applications: drag MacNeutron.app
there yourself, or use an administrator account"). No privilege escalation.

The install logic (steps 2 to 5 with the running applications and the quit request injected) lives in `MacNeutronCore`
(`AppInstaller`), so it is tested without a running app; the executable is its AppKit front.

### 3.3 The app's "Install and relaunch"

- `Release` gains `installer: URL?`: `Updater.pick` sets it from the asset `MacNeutron-<V>-Installer.zip` (https only,
  as the others). The zip and `SHA256SUMS` stay required: older apps' updaters depend on them (release runbook: asset
  names are an API).
- `Updater.canReplace(app)` true: unchanged (download the zip, verify, swap, relaunch).
- False, and the release has an installer: download it and `SHA256SUMS`, check its line, unpack with ditto, verify
  `Install MacNeutron.app` (team, the installer's identifier, the release's version, Gatekeeper; `Updater.verify` takes
  the identifier as a parameter), open it with `--from-app` (`NSWorkspace.openApplication` with arguments), and quit.
  The installer waits for the app to be gone, so the order is safe.
- False, and no installer (0.9.1 and older releases): `UpdateError.cantReplace` names where the app runs from: "MacNeutron
  is running from <folder>, which it can't replace. Download Install MacNeutron from the release page, or move
  MacNeutron to Applications."

### 3.4 The release

After R2 (MacNeutron.app notarized and stapled), `release.sh`:
1. Builds `Install MacNeutron.app` (3.1): the binary thin arm64, `strip -S`, no `/Users/` path, as the app's.
2. Signs it (`--options runtime --timestamp`, no entitlements, inside-out: the nested app is already signed and isn't
   signed again), notarizes and staples it (`notarize_and_staple`), and requires `accepted`.
3. Zips it: `ditto -c -k --norsrc --keepParent` → `MacNeutron-<V>-Installer.zip`.
4. Checks the zip as `check_zip` does: unzipped with `/usr/bin/unzip`, the installer and its nested app pass `codesign
   --verify --strict --deep` and, in a release, `stapler validate`.
5. Adds it to `SHA256SUMS`, `SIZES.txt` and the printed `gh release create` command.

A p release does the same (`MacNeutron-<V>p-Installer.zip`). A rehearsal builds it without notarizing.

## 4. Testing

- Swift tests (`MacNeutronCoreTests`): `AppInstaller` installs fresh, replaces, rolls back a failed swap, removes the
  quarantine attribute only after verification, and leaves everything as it was when an application won't quit or
  verification fails; `Updater.pick` with and without the installer asset (and a non-https one); `install` from a
  translocated path takes the installer path (fake fetch, injected open) and, without an installer, says where the app
  runs from.
- `release.sh --self-test`: the installer zip's unzip check refuses an installer whose nested app fails to verify.
- By hand (the maintainer): open the installer from Downloads with MacNeutron running; it quits MacNeutron, replaces
  `/Applications/MacNeutron.app` and opens it without the downloaded-from-the-Internet prompt.

## 5. Not built

A DMG; asking for an administrator password; removing the copy in Downloads; a first-run "move to Applications" offer
inside MacNeutron itself; installing anywhere but `/Applications`.
