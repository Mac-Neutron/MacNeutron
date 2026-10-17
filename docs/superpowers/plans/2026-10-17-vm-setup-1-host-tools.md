# VM setup, plan 1: host tools and the VM folder

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** MacNeutron.app carries the VM's host tools and installs them into the VM folder. Games use the VM only once
it also holds a complete image set.

**Architecture:**
- `eac-vm/host/stage.sh` lays out the host tools.
- `make app` puts them in `Contents/Helpers/vm`.
- `VMToolsInstaller` copies them into `~/Library/Application Support/MacNeutron/vm` the way `RuntimeInstaller` installs
  `wine.app`, and `AppModel` runs it after the runtime install.
- `MicroVM.find` starts requiring `images/manifest.sha256`.

**Tech Stack:** Swift 6 (swift-testing), POSIX shell, codesign.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md` (§3, §4.1, §4.5 step 8's Valve copy)

## Global Constraints

- The VM folder is `MicroVM.standardRoot` (`~/Library/Application Support/MacNeutron/vm`).
- The host tools, the folder's top-level items: `macneutron-vm`, `libkrun/`, `steam-forward/host/mn-steam-host`,
  `gvproxy`, `metal-host/`, `run-guest.sh`, `run-game.sh`.
- Setup never deletes or writes `state/`, `runs/`, `images/` (images are plan 2's) or any `*.tpm`.
- A VM folder that is a symlink is a developer's link to `build/eac-vm-work`. The app never writes into it.
- Tests use temporary folders only. Never open MacNeutron.app, never start or quit Steam.
- Scripts use `/usr/bin/grep`. Commits end with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.

## Review Focus

- A game starting while the tools are being replaced: the swap is deferred, and checked again after the copy (Task 2,
  `aVMStartedDuringTheCopyDefersTheSwap`).
- The developer's symlinked folder: never written, not even `share/valve` (Task 2, `aSymlinkedFolderIsLeftAlone`).
- An interrupted install leaving `<item>.new` behind: the next install removes it first (Task 2,
  `leftoversOfAnInterruptedInstallAreCleared`).
- A VM folder with tools but no images: games stay on macOS (Task 1).
- Valve's DLL updated while the VM folder exists: the new one reaches `share/valve` (Task 3).

---

### Task 1: Games need a complete image set

**Files:**
- Modify: `Sources/MacNeutronCore/MicroVM.swift:17-22` (`find`) and its doc comment.
- Modify: `eac-vm/image/build.sh`: write `$OUT/manifest.sha256` containing `dev` after the last image.
- Test: `Tests/MacNeutronCoreTests/MicroVMTests.swift`

**Interfaces:**
- Produces: `public static func MicroVM.imagesComplete(_ root: URL) -> Bool`, true when `images/manifest.sha256`
  exists. `find` returns nil without it.

- [ ] **Step 1:** In `makeVMFolder()`, also write `images/manifest.sha256` (`"dev\n"`). Add:

```swift
@Test func aVMWithoutACompleteImageSetLeavesGamesOnMacOS() throws {
    let f = try makeVMFixture()
    try FileManager.default.removeItem(at: f.vm.appending(path: "images/manifest.sha256"))
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.runner.calls.contains { $0.tool == "wine" && $0.arguments.first == game })
}
```

- [ ] **Step 2:** `swift test --filter aVMWithoutACompleteImageSetLeavesGamesOnMacOS`. Expected: FAIL (the game goes
  to run-guest.sh).
- [ ] **Step 3:** Implement `imagesComplete` and use it in `find`. Add the `manifest.sha256` line to `build.sh`, and
  create `build/eac-vm-work/images/manifest.sha256` (`dev`) by hand once so the dev folder keeps working.
- [ ] **Step 4:** `swift test`. Expected: all pass.
- [ ] **Step 5:** Commit: "MicroVM: games use the VM only with a complete image set".

### Task 2: `VMToolsInstaller`

**Files:**
- Create: `Sources/MacNeutronCore/VMToolsInstaller.swift`
- Test: `Tests/MacNeutronCoreTests/VMToolsInstallerTests.swift`

**Interfaces:**
- Consumes: `RunningProcesses.executablePaths`, `CodeIdentity.of`, `MacNeutronCore.realPath`.
- Produces:

```swift
public enum VMToolsInstallOutcome: Equatable, Sendable { case installed, unchanged, deferred(String), developerFolder }
public enum VMToolsInstaller {
    public static let items: [String]  // the Global Constraints' list, in that order
    /// tools: MacNeutron.app/Contents/Helpers/vm. vm: the VM folder (made if missing).
    @discardableResult
    public static func install(from tools: URL, into vm: URL,
                               runningExecutables: () -> [String] = RunningProcesses.executablePaths,
                               identity: (URL) -> String? = CodeIdentity.of) throws -> VMToolsInstallOutcome
    /// share/valve/steamclient64.dll from Valve's download; a nil install removes it.
    public static func copyValveClient(_ install: ValveInstall?, into vm: URL) throws
    public static func isDeveloperFolder(_ vm: URL) -> Bool  // the folder itself is a symlink
}
```

The install works like this:
1. A developer folder returns `.developerFolder`.
2. `.unchanged` when `vm/tools.identity` equals `identity(tools/macneutron-vm)` and every item exists.
3. `.deferred(path)` when a running executable's real path is under the VM folder's real path.
4. Otherwise, item by item:
   - remove a leftover `<item>.new`;
   - `cp -c -R` the item to `<item>.new`;
   - check again for a running VM; if one runs, remove the `.new` copies and return `.deferred`;
   - `renamex_np(RENAME_SWAP)` over the old item, or `rename` if there is none, then remove what was swapped out.
5. Write `tools.identity` last.

- [ ] **Step 1:** Write the tests. Each builds a fake `tools/` folder (one file per item, `libkrun/libkrun.1.dylib`,
  `steam-forward/host/mn-steam-host`, `metal-host/winemetal.so`) and injects `identity`:
  - `installsEveryItemAndRecordsTheIdentity`: every item exists in `vm/`, `vm/tools.identity` == `"a"`.
  - `aSecondInstallOfTheSameToolsIsUnchanged`: second call returns `.unchanged`, files untouched (mtime).
  - `newToolsReplaceOldOnesButNotTheGamesFiles`: pre-create `vm/state/1.img`, `vm/state/1.tpm/id`,
    `vm/runs/1/run.log`, `vm/images/manifest.sha256`. Install with identity `"b"`; those four are byte-identical, the
    items are the new ones.
  - `aRunningVMDefersTheInstall`: `runningExecutables` returns `<vm>/macneutron-vm`, so `.deferred` and nothing
    copied.
  - `aVMStartedDuringTheCopyDefersTheSwap`: `runningExecutables` returns `[]` on its first call and
    `<vm>/macneutron-vm` after; the outcome is `.deferred`, no `.new` left, the old items are still in place.
  - `leftoversOfAnInterruptedInstallAreCleared`: pre-create `vm/libkrun.new/junk`; after install no `libkrun.new` is
    left and `vm/libkrun/libkrun.1.dylib` is the new one.
  - `aSymlinkedFolderIsLeftAlone`: `vm` is a symlink to a temp dir, so `.developerFolder`, and the target stays
    empty. `copyValveClient` writes nothing there either.
  - `valveClientReachesTheVM`: `copyValveClient(ValveInstall(version: "1", folder: f))` copies
    `f/steamclient64.dll` to `vm/share/valve/steamclient64.dll`; `copyValveClient(nil, …)` removes it.
- [ ] **Step 2:** `swift test --filter VMToolsInstallerTests`. Expected: FAIL (no such type).
- [ ] **Step 3:** Implement as the Interfaces block says.
- [ ] **Step 4:** `swift test --filter VMToolsInstallerTests`, then `swift test`. Expected: all pass.
- [ ] **Step 5:** Commit: "VMToolsInstaller: the VM's host tools from the app, as wine.app is installed".

### Task 3: The app bundle carries the tools, and `AppModel` installs them

**Files:**
- Create: `eac-vm/host/stage.sh <dest>`. It copies the Global Constraints' items from `build/eac-vm-work` into `<dest>`
  (gvproxy resolved through its symlink, `steam-forward/host/mn-steam-host` only).
- Modify: `Makefile` target `app`:
  - after the `wine.app` copy: `sh eac-vm/host/stage.sh $(APP)/Contents/Helpers/vm`;
  - then `codesign --force --sign - --entitlements eac-vm/host/hv.plist $(APP)/Contents/Helpers/vm/macneutron-vm`,
    before the app's own signature.
- Modify: `Sources/MacNeutronApp/AppModel.swift`:
  - init parameters `vmTools: URL? = AppModel.bundledVMTools`, `vmRoot: URL = MicroVM.standardRoot`, and
    `vmInstaller: @escaping @Sendable (URL, URL) throws -> VMToolsInstallOutcome = { try VMToolsInstaller.install(from: $0, into: $1) }`;
  - after a successful runtime install, `installVMTools()`;
  - `pollRuntime` retries a deferred tools install;
  - after `updateValveClient` and `removeValveClient`, `VMToolsInstaller.copyValveClient`.
- Test: `Tests/MacNeutronCoreTests/AppModelTests.swift`

**Interfaces:**
- Consumes: Task 2's `VMToolsInstaller.install`, `copyValveClient`.
- Produces: `AppModel.bundledVMTools: URL?` (`Contents/Helpers/vm`, nil when absent) and
  `AppModel.vmToolsDeferred: Bool` (read by plan 4).

- [ ] **Step 1:** Tests, with a fake `vmTools` folder and a temp `vmRoot` (the existing AppModel test fixture's style):
  - `theVMToolsFollowTheRuntimeInstall`: after `installTask` finishes, `vmRoot/macneutron-vm` exists.
  - `deferredVMToolsAreRetriedByThePoll`: an injected `vmInstaller` returns `.deferred` first, then installs;
    `pollRuntime()` runs it again, and `vmToolsDeferred` goes back to false.
  - `valveFilesReachTheVM`: after `updateValveClient(force: true)` against `FakeValveCDN`,
    `vmRoot/share/valve/steamclient64.dll` equals the cache's.
- [ ] **Step 2:** `swift test --filter AppModelTests`. Expected: the three FAIL.
- [ ] **Step 3:** Implement. The install runs in `Task.detached`, like the runtime's. Errors go to `errorMessage`.
- [ ] **Step 4:** `swift test`. Then:

```sh
sh eac-vm/host/stage.sh "$TMPDIR/stage" && ls "$TMPDIR/stage" "$TMPDIR/stage/libkrun" "$TMPDIR/stage/steam-forward/host"
```

  Expected: all pass, and the seven items are listed.
- [ ] **Step 5:** Commit: "MacNeutron.app carries the VM's host tools and installs them".
