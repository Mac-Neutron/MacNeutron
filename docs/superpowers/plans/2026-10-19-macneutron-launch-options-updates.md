# Launch Options in the App and Self-Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A per-game "Launch options" field in the Games window (with Steam's own line shown read-only and checked), and an updater that installs the next GitHub release over MacNeutron.app after the player says yes.

**Architecture:** `LaunchOptions` (MacNeutronCore) parses a line into variables and arguments; `GameSettings` carries the field and the launcher applies it under Steam's launch options. `Updater` (MacNeutronCore) picks, downloads, verifies and swaps a release; `UpdateModel` (MacNeutronApp) holds its state for the menu and Settings.

**Tech Stack:** Swift 6, SwiftPM, SwiftUI, Swift Testing, Security.framework, CryptoKit, `ditto`, `spctl`.

**Spec:** `docs/superpowers/specs/2026-10-19-macneutron-launch-options-updates-design.md`

## Global Constraints

- Never write any Steam file; `localconfig.vdf` and `loginusers.vdf` are read only.
- The Steam account ID is used for a path only: never logged, shown, or put in an error message. `AccountName` is never read.
- No new dependency. Releases come from `https://api.github.com/repos/chadouming/MacNeutron/releases?per_page=30`.
- Bundle identifier `io.github.chadouming.MacNeutron`; assets `MacNeutron-<V>.zip` and `SHA256SUMS`; tags `vX.Y.Z` / `vX.Y.Zp`.
- release.sh is not changed.
- Tests never touch the network, Steam's real files, `~/Library/Application Support/MacNeutron`, or open MacNeutron.app.
- Gates per task: that task's tests (`swift test --filter <regex>`: Swift Testing IDs are `MacNeutronCoreTests.<function>()`, no file names, so filters name functions); `make test` and `make build` at the two milestones (end of Task 4, end of Task 7). `rtk` isn't installed here: run commands plainly.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; commit locally, never push.

## Review Focus

1. A pasted Steam line with an argument before `%command%` (`/usr/bin/env A=1 -foo %command% -bar`): `-foo` and `-bar` both become arguments, in order (Task 1 test).
2. `localconfig.vdf` missing, unparsable, or `loginusers.vdf` with no `MostRecent` and several `userdata` folders: no Steam line shown, no error (Task 3 tests).
3. A release list where the newest tag is the other channel (`v0.9.0p` while running `0.8.0`), or a version `0.10.0` vs `0.9.0`: the right channel and numeric order (Task 5 test).
4. A download whose zip doesn't match `SHA256SUMS`, or whose app is ad hoc signed: an error, and the installed app's folder byte-for-byte as before (Task 6 tests).
5. The swap's second rename failing: the old app is back at its path (Task 6 test).

---

### Task 1: `LaunchOptions.parse` and the field in `GameSettings`

**Files:**
- Create: `Sources/MacNeutronCore/LaunchOptions.swift`
- Modify: `Sources/MacNeutronCore/GameSettings.swift`
- Test: `Tests/MacNeutronCoreTests/LaunchOptionsTests.swift`, `Tests/MacNeutronCoreTests/GameSettingsTests.swift`

**Interfaces:**
- Produces: `public enum LaunchOptions { public static func parse(_ line: String) -> (environment: [String: String], arguments: [String]) }`; `static func isAssignment(_ word: Substring) -> Bool` (internal, Task 3 uses it); `GameSettings.launchOptions: String?` (new last init parameter, default nil); `GameSettings.launchArguments: [String]`.

- [ ] **Step 1: Write the failing tests**

```swift
@Test func launchOptionsSplitIntoVariablesAndArguments() {
    let plain = LaunchOptions.parse("DXMT_D3D12_SM6=0 MACNEUTRON_LOG=1 -dx12 -windowed")
    #expect(plain.environment == ["DXMT_D3D12_SM6": "0", "MACNEUTRON_LOG": "1"])
    #expect(plain.arguments == ["-dx12", "-windowed"])
    let pasted = LaunchOptions.parse("/usr/bin/env A=1 -foo %command% -bar")
    #expect(pasted.environment == ["A": "1"])
    #expect(pasted.arguments == ["-foo", "-bar"])
    #expect(LaunchOptions.parse("A=1 A=2").environment == ["A": "2"])       // the later one wins
    #expect(LaunchOptions.parse("-x B=2").environment.isEmpty)               // after an argument: an argument
    #expect(LaunchOptions.parse("-x B=2").arguments == ["-x", "B=2"])
    #expect(LaunchOptions.parse("1A=2").arguments == ["1A=2"])               // not a shell name
    #expect(LaunchOptions.parse("  ").environment.isEmpty && LaunchOptions.parse("").arguments.isEmpty)
}
```

In `GameSettingsTests.swift`:

```swift
@Test func launchOptionsFieldSitsOverTheControls() {
    let settings = GameSettings(graphics: "dxmt", launchOptions: "MACNEUTRON_GRAPHICS=wined3d DXMT_D3D12_SM6=0 -dx12")
    #expect(settings.environment["MACNEUTRON_GRAPHICS"] == "wined3d")
    #expect(settings.environment["DXMT_D3D12_SM6"] == "0")
    #expect(settings.launchArguments == ["-dx12"])
    #expect(GameSettings().launchArguments.isEmpty)
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter 'launchOptions|settingsBecomeLaunchVariables'` — Expected: build error, `LaunchOptions` and `launchOptions` undefined.

- [ ] **Step 3: Implement**

`parse`: split on whitespace (`split(whereSeparator: \.isWhitespace)`); drop a first word `/usr/bin/env`; drop every `%command%`; take leading words while `isAssignment` (name `[A-Za-z_][A-Za-z0-9_]*` then `=`), the rest are arguments. Doc comment carries `// ponytail: no quoting; add it when someone needs a value with a space.`
`GameSettings.environment` ends with `env.merge(LaunchOptions.parse(launchOptions ?? "").environment) { _, field in field }`; `launchArguments` returns the parse's arguments.

- [ ] **Step 4: Run them to see them pass**

Run: `swift test --filter 'launchOptions|settingsBecomeLaunchVariables'` — Expected: all pass.

- [ ] **Step 5: Commit** — `git commit -m "launch options: a per-game field, parsed into variables over the controls and game arguments"`

### Task 2: The launcher adds the field's arguments

**Files:**
- Modify: `Sources/MacNeutronCore/Launcher.swift:69-85` (`launch`), `:325-328` (`withSettings`)
- Test: `Tests/MacNeutronCoreTests/LauncherTests.swift`

**Interfaces:**
- Consumes: `GameSettings.environment`, `GameSettings.launchArguments` (Task 1).
- Produces: nothing new; `launch` behaviour only.

- [ ] **Step 1: Write the failing test** (next to `gameSettingsApplyUnderneathLaunchOptions`)

```swift
@Test func launchOptionsFieldAppliesUnderSteamAndAddsArguments() throws {
    let f = try makeFixture()
    try f.launcher.settings.save(GameSettings(launchOptions: "MACNEUTRON_GRAPHICS=wined3d DXMT_D3D12_SM6=0 -dx12"), for: "42")
    var env = f.env
    env["MACNEUTRON_GRAPHICS"] = "dxmt"  // Steam's launch options win over the field
    #expect(f.launcher.launch(["waitforexitandrun", "/g/Game.exe", "-windowed"], environment: env) == 0)
    let game = try #require(f.runner.calls.first { $0.arguments.first == "/g/Game.exe" })
    #expect(game.arguments == ["/g/Game.exe", "-windowed", "-dx12"])
    #expect(game.environment["DXMT_D3D12_SM6"] == "0")
    #expect(game.environment["WINEDLLOVERRIDES"]?.hasPrefix("dxgi=n,b;d3d10core=n,b;d3d11=n,b") == true)  // dxmt
    _ = f.launcher.launch(["run", "/g/Setup.exe"], environment: env)                    // an install script's run
    #expect(f.runner.calls.last { $0.arguments.first == "/g/Setup.exe" }?.arguments == ["/g/Setup.exe"])
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `swift test --filter launchOptionsFieldAppliesUnderSteamAndAddsArguments` — Expected: FAIL, arguments lack `-dx12`.

- [ ] **Step 3: Implement**

Load the settings once: `withSettings` returns `(environment: [String: String], arguments: [String])` (the unreadable-file path keeps today's defaults and no arguments). Make `request` a `var`; when `request.verb == .waitforexitandrun`, `request = LaunchRequest(verb: request.verb, target: request.target, arguments: request.arguments + arguments)`, before anything reads `request.arguments` (both the Mac and VM paths use it).

- [ ] **Step 4: Run the launcher's settings and argument tests**

Run: `swift test --filter 'launchOptionsField|gameSettingsApply|gameArguments|unreadableGameSettings|gameGoesThroughSteamExe'` — Expected: all pass.

- [ ] **Step 5: Commit** — `git commit -m "launcher: the launch options field's arguments follow the game's own when Steam starts it"`

### Task 3: Steam's own line, read-only, and its warnings

**Files:**
- Modify: `Sources/MacNeutronCore/SteamLocation.swift` (add `localConfig`), `Sources/MacNeutronCore/LaunchOptions.swift`
- Test: `Tests/MacNeutronCoreTests/SteamLocationTests.swift`, `Tests/MacNeutronCoreTests/LaunchOptionsTests.swift`

**Interfaces:**
- Consumes: `LaunchOptions.isAssignment`, `LaunchOptions.parse` (Task 1); `KeyValues.parse`, `[KVNode].node(at:)`.
- Produces: `SteamLocation.localConfig: URL?`; `LaunchOptions.steamLines(localConfig: URL) -> [UInt32: String]`; `LaunchOptions.problems(steamLine: String, appEnvironment: [String: String]) -> [String]`.

- [ ] **Step 1: Write the failing tests**

`SteamLocationTests.swift` — a temp Steam root; fake IDs only (SteamID64 `76561197960278073` → account `12345`):

```swift
@Test func localConfigIsTheMostRecentUsers() throws {
    let steam = SteamLocation(root: try makeTempDir())
    #expect(steam.localConfig == nil)                                          // no users at all
    try write("", to: steam.root.appending(path: "userdata/12345/config/localconfig.vdf"))
    #expect(steam.localConfig?.path(percentEncoded: false).hasSuffix("userdata/12345/config/localconfig.vdf") == true)  // the only folder
    try write("", to: steam.root.appending(path: "userdata/999/config/localconfig.vdf"))
    #expect(steam.localConfig == nil)                                          // two folders, no MostRecent
    try write("\"users\"\n{\n\t\"76561197960278073\"\n\t{\n\t\t\"MostRecent\"\t\t\"1\"\n\t}\n}\n",
              to: steam.root.appending(path: "config/loginusers.vdf"))
    #expect(steam.localConfig?.path(percentEncoded: false).hasSuffix("userdata/12345/config/localconfig.vdf") == true)
}
```

`LaunchOptionsTests.swift`:

```swift
@Test func steamLinesAreEachAppsLaunchOptions() throws {
    let file = try makeTempDir().appending(path: "localconfig.vdf")
    try write("""
        "UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {
          "42" { "LaunchOptions" "/usr/bin/env MACNEUTRON_LOG=1 %command%" }
          "43" { "LaunchOptions" "" }
          "44" { "LastPlayed" "1" }
        } } } } }
        """, to: file)
    #expect(LaunchOptions.steamLines(localConfig: file) == [42: "/usr/bin/env MACNEUTRON_LOG=1 %command%"])
    try write("{ not vdf", to: file)
    #expect(LaunchOptions.steamLines(localConfig: file).isEmpty)
    #expect(LaunchOptions.steamLines(localConfig: file.appending(path: "missing")).isEmpty)
}

@Test func steamLineProblems() {
    #expect(LaunchOptions.problems(steamLine: "MACNEUTRON_LOG=1 %command%", appEnvironment: [:])
        == ["Steam can't start the game with this line: begin it with /usr/bin/env"])
    #expect(LaunchOptions.problems(steamLine: "/usr/bin/env MACNEUTRON_LOG=1 DXMT_MAX_ANISOTROPY=4 %command%",
                                   appEnvironment: ["DXMT_MAX_ANISOTROPY": "16", "MACNEUTRON_LOG": "1", "X": "1"])
        == ["Steam's launch options override: DXMT_MAX_ANISOTROPY, MACNEUTRON_LOG"])
    #expect(LaunchOptions.problems(steamLine: "/usr/bin/env %command% -dx12", appEnvironment: ["A": "1"]).isEmpty)
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter 'localConfig|steamLine'` — Expected: build error, `localConfig`, `steamLines` and `problems` undefined.

- [ ] **Step 3: Implement**

`localConfig`: in `config/loginusers.vdf`'s `users` block, the child whose `MostRecent` is `"1"`; account = `UInt64(key)! & 0xFFFFFFFF`; else, when `userdata` holds exactly one folder, that one; else nil. Read only `MostRecent` (the file also holds account names). `steamLines`: path `["UserLocalConfigStore", "Software", "Valve", "Steam", "apps"]`, each child with a non-empty `LaunchOptions` `stringValue`; any read or parse failure gives `[:]`. `problems`: the copy above, verbatim; the "can't start" check is the first word being an assignment; the override list is the sorted intersection of the line's variables (`parse`) with `appEnvironment`'s keys, as one line.

- [ ] **Step 4: Run them to see them pass**

Run: `swift test --filter 'localConfig|steamLine'` — Expected: all pass.

- [ ] **Step 5: Commit** — `git commit -m "launch options: read each game's Steam launch line and say when it's broken or overrides the app"`

### Task 4: The Games window's field and Steam line (milestone 1)

**Files:**
- Modify: `Sources/MacNeutronApp/AppModel.swift` (`GameRow`, `Snapshot`, `loadSnapshot`, `apply`), `Sources/MacNeutronApp/GamesView.swift`, `README.md` (§ "Per-game options")
- Test: `Tests/MacNeutronCoreTests/AppModelTests.swift`

**Interfaces:**
- Consumes: `SteamLocation.localConfig`, `LaunchOptions.steamLines`, `LaunchOptions.problems` (Task 3); `GameSettings.launchOptions` (Task 1).
- Produces: `Snapshot.steamLaunchOptions: [UInt32: String]`; `GameRow.steamLaunchOptions: String?`.

- [ ] **Step 1: Write the failing test** `snapshotReadsSteamLaunchLines` — `loadSnapshot` on a fake Steam (`makeFakeSteam`) with `userdata/12345/config/localconfig.vdf` giving app 42 `"/usr/bin/env MACNEUTRON_LOG=1 %command%"`: `#expect(snapshot.steamLaunchOptions == [42: "/usr/bin/env MACNEUTRON_LOG=1 %command%"])`; with the file absent, `[:]`.

- [ ] **Step 2: Run it to see it fail** — `swift test --filter snapshotReadsSteamLaunchLines` — Expected: build error, no `steamLaunchOptions`.

- [ ] **Step 3: Implement**

`loadSnapshot` sets `steamLaunchOptions: steam.localConfig.map(LaunchOptions.steamLines) ?? [:]`; `apply` copies each row's line into `GameRow`. In `GamesView`'s selected-game panel, a third row: `TextField("Launch options", text:)` over a `@State` draft (reset when the selection changes), saved with `model.update(row.id) { $0.launchOptions = draft.isEmpty ? nil : draft }` on `.onSubmit` and when focus leaves (`@FocusState`). Its `.help`: "Variables and game arguments, e.g. DXMT_D3D12_SM6=0 -dx12. In the game VM only the variables MacNeutron passes to the VM reach the game (its logging, DLSS and a few DXMT ones); DXMT's reach its renderer on the Mac." When `row.steamLaunchOptions` is set: `Text("Steam's launch options: \(line)")` (secondary, `.textSelection(.enabled)`) and each `LaunchOptions.problems(steamLine: line, appEnvironment: row.settings.environment)` in orange. README: a sentence before the table saying the Games window's "Launch options" field takes the same variables (and game arguments) without `/usr/bin/env` or `%command%`, and that Steam's own line wins over it.

- [ ] **Step 4: Milestone gate** — `swift test --filter snapshotReadsSteamLaunchLines`, then `make test` and `make build` — Expected: all pass, build clean.

- [ ] **Step 5: Commit** — `git commit -m "Games window: a launch options field per game, with Steam's own line and its problems"`

### Task 5: Picking the release

**Files:**
- Create: `Sources/MacNeutronCore/Updater.swift`
- Test: `Tests/MacNeutronCoreTests/UpdaterTests.swift`

**Interfaces:**
- Produces: `public struct Release: Equatable, Sendable { public let version: String; public let page: URL; public let zip: URL; public let sums: URL }`; `public enum Updater { public static let releases: URL; public static func pick(_ json: Data, current: String) -> Release? }`; test helper `func releasesJSON(_ entries: [(tag: String, draft: Bool, prerelease: Bool, assets: [String])]) -> Data` (internal, in `UpdaterTests.swift`; Task 7 reuses it), giving each asset `browser_download_url` `https://example.invalid/<tag>/<name>` and `html_url` `https://example.invalid/<tag>`.

- [ ] **Step 1: Write the failing test**

```swift
@Test func pickTakesTheNewestOfTheRunningChannel() throws {
    let both = ["MacNeutron-%@.zip", "SHA256SUMS"]
    func entry(_ v: String, draft: Bool = false, pre: Bool = false, assets: [String]? = nil) -> (tag: String, draft: Bool, prerelease: Bool, assets: [String]) {
        ("v" + v, draft, pre, (assets ?? both).map { $0.replacingOccurrences(of: "%@", with: v) })
    }
    let json = releasesJSON([entry("0.9.0"), entry("0.10.0"), entry("0.11.0", draft: true), entry("0.12.0", pre: true),
                             entry("0.13.0", assets: ["SHA256SUMS"]), entry("0.10.0p"), entry("0.9.1p")])
    let normal = try #require(Updater.pick(json, current: "0.8.0"))
    #expect(normal.version == "0.10.0")                                         // numeric, not text, order
    #expect(normal.zip.lastPathComponent == "MacNeutron-0.10.0.zip")
    #expect(normal.page.absoluteString == "https://example.invalid/v0.10.0")
    #expect(Updater.pick(json, current: "0.8.0p")?.version == "0.10.0p")        // the performance channel
    #expect(Updater.pick(json, current: "0.10.0") == nil)                       // nothing newer
    #expect(Updater.pick(json, current: "dev") == nil)
    #expect(Updater.pick(Data("{".utf8), current: "0.8.0") == nil)
}
```

- [ ] **Step 2: Run it to see it fail** — `swift test --filter pickTakesTheNewest` — Expected: build error, `Updater` undefined.

- [ ] **Step 3: Implement** — `releases` = the URL in Global Constraints. A version is `X.Y.Z` with an optional `p`, parsed to `[Int]` + a performance flag (anything else: nil). Decode `tag_name`, `draft`, `prerelease`, `html_url`, `assets[].name`, `assets[].browser_download_url` with private `Decodable` structs; keep releases per the spec's rule; return the maximum by `[Int]` (`lexicographicallyPrecedes`) above `current`.

- [ ] **Step 4: Run it to see it pass** — `swift test --filter pickTakesTheNewest` — Expected: pass.

- [ ] **Step 5: Commit** — `git commit -m "updater: pick the newest GitHub release of the running build's channel"`

### Task 6: Download, verify, swap

**Files:**
- Modify: `Sources/MacNeutronCore/Updater.swift`, `Sources/MacNeutronCore/CodeIdentity.swift`
- Test: `Tests/MacNeutronCoreTests/UpdaterTests.swift`

**Interfaces:**
- Consumes: `Release` (Task 5); `ValveClient.Fetch`; `VMDownloads.download`, `VMDownloads.matches`.
- Produces: `CodeIdentity.team(of: URL) -> String?`; `public enum UpdateError: Error, Equatable, CustomStringConvertible { case cantReplace, checksum, signature(String), version(String), gatekeeper(String) }`; `Updater.canReplace(_ app: URL) -> Bool`; `Updater.install(_ release: Release, over app: URL, team: String, fetch: ValveClient.Fetch? = nil) async throws -> URL` (nil: `VMDownloads.download`, which is internal so it can't be a public default) (returns the work folder holding the old app); `Updater.swap(_ new: URL, into app: URL, aside: URL) throws`; `Updater.relaunch(_ app: URL, removing work: URL) throws`.

Copy for `UpdateError.description`: `cantReplace` "Move MacNeutron to Applications to update it."; `checksum` "The download doesn't match the release's SHA256SUMS."; `signature(d)` "The download isn't signed by MacNeutron's developer: \(d)"; `version(v)` "The download is version \(v), not the release's."; `gatekeeper(d)` "macOS doesn't accept the download: \(d)".

- [ ] **Step 1: Write the failing tests**

```swift
@Test func adHocSignedAppsHaveNoTeam() throws {
    #expect(CodeIdentity.team(of: try makeSignedWineApp(at: try makeTempDir())) == nil)
}

@Test func canReplaceNeedsAWritableFolderOutsideTranslocation() throws {
    #expect(Updater.canReplace(try makeTempDir().appending(path: "MacNeutron.app")))
    #expect(!Updater.canReplace(URL(filePath: "/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app")))
}

@Test func swapPutsTheNewAppInPlaceAndRollsBack() throws {
    let dir = try makeTempDir()
    let app = dir.appending(path: "MacNeutron.app"), new = dir.appending(path: "new/MacNeutron.app")
    try write("old", to: app.appending(path: "v")); try write("new", to: new.appending(path: "v"))
    try Updater.swap(new, into: app, aside: dir.appending(path: "old.app"))
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "new")
    #expect(throws: (any Error).self) { try Updater.swap(dir.appending(path: "missing.app"), into: app, aside: dir.appending(path: "old2.app")) }
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "new")  // rolled back
}
```

`installRefusesAndChangesNothing`: an installed app `dir/MacNeutron.app` (a file `v` = "old"); a release zip made with `ditto -c -k --keepParent` of `makeSignedWineApp` renamed to `MacNeutron.app` (ad hoc), its `SHA256SUMS` line computed in the test; `fetch` maps the release's URLs to local files (`{ url in url.lastPathComponent == "SHA256SUMS" ? sums : zip }` copied to temp). Expect `UpdateError.signature` for the ad hoc zip with team `"ABCDE12345"`, `UpdateError.checksum` with a wrong sums line, and after each `v` still reads "old".

- [ ] **Step 2: Run them to see them fail** — `swift test --filter 'adHocSignedApps|canReplace|swapPuts|installRefuses|identity|bundlesDiffering|resigning'` — Expected: build error.

- [ ] **Step 3: Implement**

`team(of:)`: `kSecCodeInfoTeamIdentifier` from `SecCodeCopySigningInformation`, as `of` reads `kSecCodeInfoUnique`. `canReplace`: no `/AppTranslocation/` in the path and the parent folder writable. `install`: refuse `!canReplace` first; work folder `FileManager.default.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: app, create: true)`; fetch both assets; the zip's line in `SHA256SUMS` (last field equals the zip's name) must equal its hash (`VMDownloads.matches` with the file's own size); `/usr/bin/ditto -x -k <zip> <work>/new`; verify `<work>/new/MacNeutron.app` with `SecStaticCodeCheckValidityWithErrors` (flags `kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode`) against `anchor apple generic and identifier "io.github.chadouming.MacNeutron" and certificate leaf[subject.OU] = "<team>"`, then its `CFBundleShortVersionString` == `release.version`, then `/usr/sbin/spctl --assess --type exec` exits 0; then `swap(new, into: app, aside: <work>/old.app)`. Any throw before `swap` removes the work folder. `swap`: rename `app` → `aside`, rename `new` → `app`; if the second fails, rename `aside` back and rethrow. `relaunch`: a `Process` running `/bin/sh -c 'while kill -0 "$0" 2>/dev/null; do sleep 0.2; done; rm -rf "$1"; exec /usr/bin/open "$2"'` with this pid, the work folder and the app; not waited on.

- [ ] **Step 4: Run them to see them pass** — `swift test --filter 'adHocSignedApps|canReplace|swapPuts|installRefuses|identity|bundlesDiffering|resigning'` — Expected: all pass.

- [ ] **Step 5: Commit** — `git commit -m "updater: download, check against SHA256SUMS, verify the developer's signature and Gatekeeper, swap"`

### Task 7: `UpdateModel`, the menu and Settings (milestone 2)

**Files:**
- Create: `Sources/MacNeutronApp/UpdateModel.swift`
- Modify: `Sources/MacNeutronApp/MacNeutronApp.swift`, `Sources/MacNeutronApp/MenuContent.swift`, `Sources/MacNeutronApp/SettingsView.swift`, `README.md` (§ "Download and set up")
- Test: `Tests/MacNeutronCoreTests/UpdateModelTests.swift`

**Interfaces:**
- Consumes: `Updater.releases`, `Updater.pick`, `Updater.install`, `Updater.relaunch`, `UpdateError`, `Release` (Tasks 5-6); `CodeIdentity.team(of:)`; `AppleScriptNotifier`; `RecordingNotifier` and `releasesJSON` (tests).
- Produces: `@Observable @MainActor final class UpdateModel` with `init(current: String, app: URL, team: String?, defaults: UserDefaults = .standard, notifier: any Notifier = AppleScriptNotifier(), get: @escaping @Sendable (URL) async throws -> Data = <ephemeral URLSession GET with Accept: application/vnd.github+json>, now: @escaping () -> Date = Date.init)`, `var enabled: Bool` (team != nil), `var automatic: Bool` (defaults key `checkForUpdates`, default true), `private(set) var available: Release?`, `private(set) var notice: String?`, `private(set) var working: Bool`, `func check() async`, `func checkIfDue() async`, `func install() async`.

- [ ] **Step 1: Write the failing tests** (a `UserDefaults(suiteName: UUID().uuidString)`, a recorded `get`, a fixed `now`)

```swift
@Test func checkFindsAReleaseAndNotifiesOnce() async throws { ... }
// team "ABCDE12345", current "0.8.0", get returns releasesJSON with v0.9.0: after check(), available?.version == "0.9.0",
// notifier.posted == ["MacNeutron 0.9.0 is available."]; a second check() posts nothing more.

@Test func checkIfDueWaitsADayAndHonoursTheSwitch() async throws { ... }
// checkIfDue() at t calls get once; again at t+23h: no call; at t+25h: a call; with automatic = false: no call.

@Test func devBuildsNeverCheck() async throws { ... }
// team nil: enabled == false, check() and checkIfDue() never call get.
```

- [ ] **Step 2: Run them to see them fail** — `swift test --filter 'checkFindsARelease|checkIfDue|devBuildsNeverCheck'` — Expected: build error.

- [ ] **Step 3: Implement**

`check()`: GET `Updater.releases`, `pick` against `current`, store `lastUpdateCheck` = `now()`; a release not equal to defaults' `notifiedUpdate` posts `"MacNeutron \(v) is available."` (title "MacNeutron") and records it; failures go to `notice`, never thrown. `checkIfDue()`: when `enabled && automatic` and 24 h have passed since `lastUpdateCheck`. In `init`, when enabled, a `Task` runs `checkIfDue()` now and then every hour. `install()`: `working = true`, `notice` "Downloading MacNeutron \(v)…", `Updater.install(available, over: app, team: team)`, then `Updater.relaunch` and `NSApplication.shared.terminate(nil)`; a failure sets `notice` to the error's description and `working = false`.
`MacNeutronApp`: `@State private var updates = UpdateModel(current: <CFBundleShortVersionString or "dev">, app: Bundle.main.bundleURL, team: CodeIdentity.team(of: Bundle.main.bundleURL))`, passed with `.environment(updates)` to `MenuContent` and `SettingsView`. Menu: when `updates.available` is set, `Button("Update to \(v)…") { show("settings", with: openWindow) }` above "Settings…". Settings, only when `updates.enabled`: a `LabeledContent("Updates")` with `Toggle("Check automatically")` and `Button("Check now")` (disabled while `working`); when available, "MacNeutron \(v) is available" with `Link("Release notes", destination: page)` and `Button("Install and relaunch")` (disabled while `working`); `notice` underneath, secondary and selectable. The Form's existing `.disabled(model.busy != nil)` covers "not during a Steam action". README "Download and set up": a step-3 sentence that a release build checks GitHub daily, the menu offers "Update to X.Y.Z…", and Settings installs it and relaunches; the app must be in a folder you can write to (e.g. `/Applications`).

- [ ] **Step 4: Milestone gate** — `swift test --filter 'checkFindsARelease|checkIfDue|devBuildsNeverCheck'`, then `make test` and `make build` — Expected: all pass, build clean.

- [ ] **Step 5: Commit** — `git commit -m "app: offer and install updates from the menu and Settings"`

### After Task 7

Hand the maintainer the end-to-end check: it needs a published release that contains this work, installed in `/Applications`, and a newer release (a `--rehearse` build can't stand in: it isn't notarized, so Gatekeeper refuses it, which is itself the refusal check). The agent never opens MacNeutron.app.
