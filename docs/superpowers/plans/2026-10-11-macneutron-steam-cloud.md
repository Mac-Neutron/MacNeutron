# Steam Cloud for Windows Games Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Steam Auto-Cloud resolves and syncs Windows games run through MacNeutron without any path that can lose a save.

**Architecture:** The Windows tool registers as `macneutron-proton` (Steam maps cloud roots only for names containing
"proton") from the same folder, keeping the old name until no mapping uses it. A new `ProfileLinks` unit gives every
prefix Proton's `steamuser` and XP-name links, merging Steam-made folders only by clone → verify → atomic swap.
`PrefixManager` builds into Steam-seeded prefixes instead of renaming them; the app warns before destructive actions.

**Tech Stack:** Swift 6, SwiftPM, swift-testing, Darwin (`renamex_np`, `statfs`), SwiftUI.

**Spec:** `docs/superpowers/specs/2026-10-11-macneutron-steam-cloud-design.md` (approved 2026-10-11, D1–D6 as recommended).

## Global Constraints

- Steam tool names: `macneutron-proton` (display `MacNeutron`); legacy `macneutron` (display `MacNeutron (old)`) while
  the vdf still has it; `macneutron-native` unchanged. The tool folder and its bundle link stay named `macneutron`.
- Nothing under `drive_c` is ever deleted. A replaced real folder becomes `<name> BACKUP`, then `<name> BACKUP 2`, ….
- launcher.log and notifications never carry a SteamID: every run of 6+ digits in a logged path becomes `<n>`.
- Links are relative: `Local Settings/Application Data` → `../AppData/Local`, `Application Data` → `AppData/Roaming`,
  `My Documents` → `Documents`, `users/steamuser` → `<user>`.
- Don't touch `SteamBridge.swift` or `PrefixManager.deploySteamBridge` (another session owns them). Rebase onto `main`
  before Task 3.
- Never launch Steam, games or MacNeutron.app; never read real `steamapps/compatdata` or
  `~/Library/Application Support/MacNeutron`. Tests use temp dirs only.
- Per task: run that task's filter (`swift test --filter <Suite>`). Full `make build test` after Task 3 and in Task 6.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

## Review Focus

1. **Wine links the profile's `Documents` to the Mac's `~/Documents`.** A seeded `steamuser/My Documents/G/a` must land
   in the linked Mac folder, not stop the merge. Test in Task 2 (`myDocumentsMergesThroughWinesDocumentsLink`).
2. **A launch killed between clone and swap.** The next launch finds the clones already there (equal) and finishes the
   swap. Test in Task 2 (`aRerunAfterClonesFinishesTheSwap`).
3. **A file in Steam's tree where the profile has a folder (or the reverse).** No swap; the original folder is
   untouched. Test in Task 2 (`fileAgainstFolderRefusesTheMerge`).
4. **`$USER` names no profile in the prefix** (Steam started with another `USER`, or wineboot failed). Nothing is
   linked, one note. Test in Task 2 (`missingProfileLinksNothing`).
5. **A second merge after an earlier one left `steamuser BACKUP`.** The new backup is `steamuser BACKUP 2`. Test in
   Task 2 (`backupNameSkipsTakenNames`).

---

### Task 1: Rename the Windows tool and move its mappings

**Files:**
- Modify: `Sources/MacNeutronCore/MappingPlanner.swift:21,82`
- Modify: `Sources/MacNeutronCore/RuntimeInstaller.swift:33-48,148`
- Modify: `Sources/MacNeutronCore/SteamPlayMode.swift:16,37,47,85-86,116-121,208,216,250-255`
- Modify: `Sources/MacNeutronCore/LaunchEnvironment.swift:8`
- Test: `Tests/MacNeutronCoreTests/MappingPlannerTests.swift`, `RuntimeInstallerTests.swift`, `SteamPlayModeTests.swift`,
  `LaunchEnvironmentTests.swift`, `AppModelTests.swift:81`

**Interfaces:**
- Produces:
  - `MappingPlanner.runtimeTool = "macneutron-proton"`, `MappingPlanner.legacyRuntimeTool = "macneutron"`.
  - `MappingPlanner.isOurs(_:)` is `tool.hasPrefix("macneutron")` (literal).
  - `SteamPlayMode.runtimeFolderName = "macneutron"`: folder and bundle link (`runtimeTool`, `link`, `filesIntact`,
    `linkTools`, `unlinkTools` use it). `SteamPlayMode.runtimeToolName` is removed.
  - `RuntimeInstaller.compatibilityTool(legacyName: Bool) -> String`.
  - `RuntimeInstaller.registersLegacyName(in root: URL) -> Bool`: the vdf at `root` parses and has
    `compatibilitytools/compat_tools/macneutron`.
  - `RuntimeInstaller.writeCompatibilityTool(in root: URL, legacyName: Bool) throws`.
  - `SteamPlayMode.finishRename(wasMigrating: Bool)`: Task 4 adds the prefix pass inside it.

- [ ] **Step 1: Write the failing tests**

```swift
// MappingPlannerTests
@Test func windowsGamesMapToTheProtonNamedTool() {
    #expect(MappingPlanner.plan(apps: [], runAs: [:])["0"] == ToolMapping(tool: "macneutron-proton", priority: 75))
}
@Test func legacyAndNativeEntriesAreOurs() {
    let existing = [KVNode.block("1", [.string("name", "macneutron"), .string("config", ""), .string("priority", "250")]),
                    KVNode.block("2", [.string("name", "macneutron-native"), .string("config", ""), .string("priority", "250")])]
    #expect(MappingPlanner.claimedByOtherTools(in: existing).isEmpty)
    #expect(MappingPlanner.current(in: existing).count == 2)
}
// RuntimeInstallerTests
@Test func toolFilesKeepTheLegacyNameOnlyWhileTheVdfHasIt() throws  // fresh dir → only "macneutron-proton";
    // dir whose vdf has the old single "macneutron" entry → both keys, legacy display "MacNeutron (old)";
    // after writeCompatibilityTool(legacyName: false) a rewrite via writeToolFiles stays without it.
// SteamPlayModeTests
@Test func verifyAcceptsEitherRuntimeName()   // okSession (legacy) and okSession with "macneutron-proton," both → nil
@Test func syncMovesLegacyMappingsThenDropsTheLegacyName() async throws
    // enable(plan: samplePlan) (legacy names), write a two-entry vdf into the runtime folder, quit,
    // sync(plan: samplePlan with "macneutron-proton") == true; currentMappings has no "macneutron";
    // RuntimeInstaller.registersLegacyName(in: runtime folder) == false
// LaunchEnvironmentTests
@Test func steamsProtonFlagIsRemoved() {
    let env = LaunchEnvironment.build(base: ["STEAM_COMPAT_PROTON": "1"], context: context, backend: .dxmt, logging: false)
    #expect(env["STEAM_COMPAT_PROTON"] == nil)
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter 'MappingPlannerTests|RuntimeInstallerTests|SteamPlayModeTests|LaunchEnvironmentTests'`
Expected: the new tests fail (old name, missing functions).

- [ ] **Step 3: Implement**

- `verify(log:)`: `Ignoring tool macneutron` stays a literal. Registration accepts either `Registering tool
  macneutron-proton,` or `Registering tool macneutron,`, plus the native tool.
- `finishRename(wasMigrating:)`: runs after `applyMappings` in both `sync` and `enable`, with `wasMigrating` read
  from `currentMappings()` before the apply. It calls `writeCompatibilityTool(in: runtimeTool, legacyName: false)`
  when the vdf exists and still has the legacy name.
- `writeToolFiles`: passes `registersLegacyName(in: layout.root)`.
- `LaunchEnvironment.build`: removes `STEAM_COMPAT_PROTON`.
- Leave the test fixtures that write `"macneutron"` mappings as they are; they now stand for legacy entries. Update
  `AppModelTests:81` to expect `macneutron-proton`.

- [ ] **Step 4: Run them to see them pass**

Same command. Expected: all pass, including the existing suites.

- [ ] **Step 5: Commit** — `steam cloud: register the Windows tool as macneutron-proton, keeping the old name until its mappings move`

### Task 2: ProfileLinks

**Files:**
- Create: `Sources/MacNeutronCore/ProfileLinks.swift`
- Test: `Tests/MacNeutronCoreTests/ProfileLinksTests.swift`

**Interfaces:**
- Produces:
  - `struct ProfileLinkReport: Equatable, Sendable { var notes: [String] = []; var conflicts = 0 }`. `conflicts`
    counts links left as real folders because files differ (D2).
  - `enum ProfileLinks { static func apply(prefix: URL, user: String, merge: Bool = true, canSwap: (URL) -> Bool = ProfileLinks.isAPFS) -> ProfileLinkReport }`
  - `static func isAPFS(_ url: URL) -> Bool`: `statfs` `f_fstypename == "apfs"`.

Behaviour (spec §4.2):
- The profile is `drive_c/users/<user>` and must be a real directory (lstat); otherwise there's one note and nothing
  is done.
- The three XP links inside the profile come first, then `users/steamuser` → `<user>` (skipped when `user` is
  `steamuser`).
- Per link path:
  - **missing:** create the parent folders and the link;
  - **a link with the wrong target:** a new link at `<path>.macneutron-new`, then `rename` over the old one;
  - **a real directory:** merge when `merge` is true and `canSwap(prefix)`; otherwise one note;
  - **anything else:** one note.
- Merge:

```
files    = regular files under the folder, relative paths; symbolic links in it are skipped (enumerator never follows them)
compare  = for each: target/rel missing → missing; !FileManager.contentsEqual → differing   (target/rel resolves through target's links)
differing non-empty → conflicts += 1, note "kept <name>: N files differ from the profile (e.g. <rel>)", stop
clone missing: createDirectory(parent, intermediate) + FileManager.copyItem; any failure → note, stop
compare again: missing or differing non-empty → note, stop
backup = first free "<name> BACKUP", "<name> BACKUP 2", … (lstat); symlink(target) at backup;
renamex_np(backup, path, RENAME_SWAP); on failure unlink the backup link, note
```

- Logged relative paths go through `replacing(/[0-9]{6,}/, with: "<n>")`.

- [ ] **Step 1: Write the failing tests** (a temp `pfx/drive_c/users/<user>` with `AppData/Local`, `AppData/Roaming`, `Documents`)

```swift
@Test func anEmptyProfileGetsAllFourLinks()
    // readlink: users/u/Local Settings/Application Data == "../AppData/Local", users/u/Application Data == "AppData/Roaming",
    // users/u/My Documents == "Documents", users/steamuser == "u"; report == ProfileLinkReport()
@Test func seededSteamuserMergesIntoVistaFolders()
    // users/steamuser/Local Settings/Application Data/G/s.sav == "cloud" → users/u/AppData/Local/G/s.sav == "cloud";
    // users/steamuser is a link; "users/steamuser BACKUP/Local Settings/Application Data/G/s.sav" == "cloud"
@Test func aDifferingFileChangesNothing()
    // profile AppData/Local/G/s.sav == "local", seeded == "cloud": steamuser still a real dir, both files unchanged,
    // conflicts == 1, no "steamuser BACKUP"
@Test func anIdenticalFileMerges()          // same content both sides → link made, conflicts == 0
@Test func aSecondRunChangesNothing()       // apply twice → second report == ProfileLinkReport(), same readlinks
@Test func aProfileNamedSteamuserGetsNoSteamuserLink()   // user "steamuser": XP links made, users/steamuser stays a dir
@Test func withoutMergeRealFoldersStay()    // merge: false and seeded steamuser → steamuser a dir, one note, conflicts == 0
@Test func noSwapSupportLeavesFoldersAlone()   // canSwap: { _ in false } → steamuser a dir, one note
@Test func loggedPathsHideLongNumbers()     // differing file under G/76561198000000000/x → note contains "G/<n>/x", not the digits
// Review Focus
@Test func myDocumentsMergesThroughWinesDocumentsLink()  // users/u/Documents → link to a temp "mac documents" dir;
    // seeded steamuser/My Documents/G/a → "mac documents/G/a" exists; steamuser is a link
@Test func aRerunAfterClonesFinishesTheSwap()  // profile already has the equal clone of every seeded file → swap happens
@Test func fileAgainstFolderRefusesTheMerge()  // seeded .../G is a file, profile .../G a folder → no swap, seeded file intact
@Test func missingProfileLinksNothing()        // no users/u → no links anywhere, notes.count == 1
@Test func backupNameSkipsTakenNames()         // existing "steamuser BACKUP" dir → new backup is "steamuser BACKUP 2"
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter ProfileLinksTests`. Expected: compile failure (no `ProfileLinks`).

- [ ] **Step 3: Implement `ProfileLinks` in `Sources/MacNeutronCore/ProfileLinks.swift`**

Write the merge as described above (`PlayerData.copyTree` doesn't fit: it refuses links in the target). Add a
`ponytail:` comment naming the ceiling: `contentsEqual` reads both copies twice, which is fine for save folders.

- [ ] **Step 4: Run them to see them pass**

Same command. Expected: all pass.

- [ ] **Step 5: Commit** — `steam cloud: ProfileLinks, Proton's steamuser and XP-name links with a clone-verify-swap merge`

### Task 3: Prefix guard, links on every launch, conflict notification

First: `git rebase main` (another session changes `PrefixManager.deploySteamBridge`; keep these edits away from it).

**Files:**
- Modify: `Sources/MacNeutronCore/PrefixManager.swift:53-90`
- Modify: `Sources/MacNeutronCore/Launcher.swift:85-94`
- Test: `Tests/MacNeutronCoreTests/PrefixManagerTests.swift`, `LauncherTests.swift`

**Interfaces:**
- Consumes: `ProfileLinks.apply(prefix:user:)`, `ProfileLinkReport` (Task 2).
- Produces: `@discardableResult public func prepare(environment: [String: String], steamBridge: Bool = false) throws -> ProfileLinkReport`.

- [ ] **Step 1: Write the failing tests**

```swift
// PrefixManagerTests (runner: winebootMakingUser("u"), env with "USER": "u")
@Test func aSeededPrefixIsPreparedInPlace()
    // pfx/drive_c/users/steamuser/Local Settings/Application Data/G/s.sav, no version, no user.reg →
    // no pfx.rosetta; wineboot ran; users/u/AppData/Local/G/s.sav == content; stamp == "wine.app id1 msync=1"
@Test func linksAreKeptOnEveryLaunch()   // prepare, remove users/steamuser link, prepare (up to date) → link back
@Test func aConflictIsReportedAndLogged() // differing file → prepare returns conflicts == 1; launcherLog contains "note: kept steamuser"
// LauncherTests
@Test func aCloudConflictPostsANotification()
    // waitforexitandrun with a seeded differing file (runner making users/<USER> from env) → notifier.posted ==
    // ["Steam Cloud saves for this game differ from the ones on this Mac, so MacNeutron kept both. Details are in launcher.log."]
```

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter 'PrefixManagerTests|LauncherTests'`. Expected: the new tests fail.

- [ ] **Step 3: Implement**

- `prepare`: `seeded = exists && recorded == nil && !exists(pfx/user.reg)`. A seeded prefix takes the same branch as
  a fresh one (no `renameRosettaPrefix`, no carry).
- After the stamp branches, inside the lock and before `deploySteamBridge`:
  `ProfileLinks.apply(prefix: context.prefix, user: environment["USER"] ?? NSUserName())`. Append each note as
  `note: <note>` and return the report.
- `Launcher`: for `.run` and `.waitforexitandrun`, post the exact message above when `conflicts > 0`.
- Any existing rename test whose fixture has neither a stamp nor `user.reg` gets a `user.reg`, so it keeps testing the
  rename path.

- [ ] **Step 4: Run them to see them pass**, then the milestone gate: `make build test`. Expected: all green.

- [ ] **Step 5: Commit** — `steam cloud: prepare Steam-seeded prefixes in place and keep profile links on every launch`

### Task 4: Link existing prefixes while the mappings move (D3)

**Files:**
- Modify: `Sources/MacNeutronCore/SteamPlayMode.swift` (`finishRename`, new `linkProfiles`, `profileUser`)
- Test: `Tests/MacNeutronCoreTests/SteamPlayModeTests.swift`

**Interfaces:**
- Consumes: `finishRename(wasMigrating:)` (Task 1), `ProfileLinks.apply(prefix:user:merge:)` (Task 2), `withFileLock(at:_:)`.
- Produces: `public var profileUser: String = NSUserName()` on `SteamPlayMode`; `func linkProfiles()`.

- [ ] **Step 1: Write the failing tests**

```swift
@Test func migrationLinksExistingPrefixes() async throws
    // fake Steam root's steamapps/compatdata/10/pfx/drive_c/users/u/AppData/Local exists; mode.profileUser = "u";
    // legacy mappings → sync → readlink(.../users/steamuser) == "u"
@Test func migrationNeverMergesSeededFolders() async throws
    // compatdata/11 with users/u and a real users/steamuser/x → after sync users/steamuser is still a directory
@Test func syncWithoutLegacyMappingsTouchesNoPrefix() async throws
    // already-migrated mappings + a plan change → no users/steamuser link created
```

- [ ] **Step 2: Run them to see them fail** — `swift test --filter SteamPlayModeTests`.

- [ ] **Step 3: Implement**

`finishRename(wasMigrating: true)` calls `linkProfiles()` before dropping the legacy name. `linkProfiles` loops over
`steam.libraries()` → `compatdata/<id>/pfx`. Only prefixes where `drive_c/users/<profileUser>` is a real directory
are touched, and each is handled under `withFileLock(at: compatdata/<id>/macneutron.lock)` with `merge: false`.
Errors and notes are ignored: a prefix that is skipped gets its links at its next launch.

- [ ] **Step 4: Run them to see them pass.**

- [ ] **Step 5: Commit** — `steam cloud: link existing prefixes while Steam is closed and the mappings move`

### Task 5: Warnings in the app

**Files:**
- Modify: `Sources/MacNeutronApp/SettingsView.swift:45-77` (`CleanupView`)
- Modify: `Sources/MacNeutronApp/GamesView.swift:22-30`

The text is fixed (spec §4.5):
- **CleanupView:**
  - Under the title, a secondary line: `These folders hold games' Windows settings and saves. Saves that aren't in Steam
    Cloud exist nowhere else.`
  - "Delete selected" sets `@State confirming = true` instead of deleting.
  - `.confirmationDialog("Delete \(n) \(n == 1 ? "game's" : "games'") data?", isPresented: $confirming)` has the
    button `Delete` (`role: .destructive`), which runs today's delete, and the message `This can't be undone.`
- **GamesView:**
  - The "Runs as" picker's setter stores `(row.id, value)` in `@State pendingRunAs` instead of saving.
  - `.confirmationDialog("Switch versions?", isPresented: …)` has the button `Switch`, which runs today's
    `model.update`, plus Cancel, and the message `Steam Cloud keeps this game's saves in a different place for each
    version. Back up your saves before switching.`

- [ ] **Step 1: Implement both dialogs.** There are no view tests in this repo.
- [ ] **Step 2: `make build`**. Expected: builds with no new warnings.
- [ ] **Step 3: Commit** — `app: confirm before deleting leftover data and before switching a game's version`

### Task 6: Maintainer protocol and final gate

**Files:**
- Create: `docs/testing/acceptance-steam-cloud.md`
- Modify: `docs/superpowers/specs/2026-10-11-macneutron-steam-cloud-design.md:3` (Status: approved; implemented)

- [ ] **Step 1: Write the protocol** from spec §7, with exact commands:
  - **Paths:** `S="$HOME/Library/Application Support/Steam"` and
    `T="$HOME/Library/Application Support/MacNeutron/compatibilitytools.d/macneutron"`.
  - **Backups:** `cp -c`.
  - **Phase A vdf:** the exact two-entry vdf text to paste.
  - **Log checks:** `grep -n 'Registering tool macneutron' "$S/logs/compat_log.txt" | tail -4`. For the cloud log,
    `grep -n 'Pal/Saved\|WinAppDataLocal' "$S/logs/cloud_log.txt" | tail -40`, piped through the redaction `sed`
    from `/private/tmp/claude-501/steam-cloud-probes/steam-mechanics/R.sh`, copied into the doc.
  - **Listing names under `compatdata/1623730/pfx/drive_c/users`:** `find … -maxdepth 6 | sed …`, redacted.
  - **Phase B checks:**
    - `grep -c '"name"[[:space:]]*"macneutron"$' "$S/config/config.vdf"` gives 0;
    - `launcher.log` has a `note: … steamuser` line;
    - the exit pass shows `Watching` with no delete lines for `Pal/Saved`.
  - **Back-out steps** for each phase.
- [ ] **Step 2: Full gate:** `make build test`. Expected: all green.
- [ ] **Step 3: Commit** — `docs: Steam Cloud acceptance protocol for the maintainer`
