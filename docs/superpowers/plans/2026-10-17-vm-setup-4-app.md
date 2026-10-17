# VM setup, plan 4: the app sets up its VM by itself

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When MacNeutron opens and the VM folder's images aren't the app's, it runs `macneutron vm-setup` in the
background and shows its progress, with Retry on failure and "Rebuild the game VM" in Settings.

**Architecture:** `AppModel` gets a VM-setup section beside its runtime and Valve sections.
- It decides whether setup is needed by comparing `images/manifest.sha256` with the bundled manifest's hash.
- It runs the CLI as a child process and reads its `progress` lines into one notice.
- A deferral (exit 75) is retried by the existing poll.

The views only show the notice and the two buttons.

**Tech Stack:** Swift 6, SwiftUI, swift-testing.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md` (§4.6, §5)

## Global Constraints

- **Status text, exactly:**
  - `Game VM: downloading (<done> of <total>)`, in GB with two digits after the point for the total (`1.35 GB`) and
    one for done (`0.4`);
  - `Game VM: building its images (<k> of <n>)`;
  - `Game VM: ready`;
  - `Game VM: waiting for the game to quit` (exit 75);
  - on failure, `Game VM: <stderr's last line>`, with a Retry button.
- **Rebuild** deletes `vm/images` and `vm/setup` only, never `state/`, `runs/` or a `*.tpm`, and asks first:
  "Rebuild the game VM?" / "It downloads about 1.35 GB again. Games' saves and settings are kept."
- **The developer folder:** setup never starts for a symlinked VM folder (`VMToolsInstaller.isDeveloperFolder`).
- **Order:** setup starts only after the VM tools are installed (plan 1), since the CLI it runs is the app's.
- **The log:** setup's output goes to `vm-setup.log` in `LauncherLog.standard.directory`.
- Tests use temp folders and fake helpers. Never open MacNeutron.app. Commit trailer as plan 1.

## Review Focus

- **The app quits mid-setup:** the child process is terminated (no orphan), and the next start resumes from the
  cache, as plan 2 makes the CLI do (Task 1, `quittingStopsTheSetup`).
- **A setup that fails:** the notice keeps the reason until Retry, and the poll doesn't loop it (Task 1,
  `aFailedSetupWaitsForRetry`).
- **Rebuild while a game runs:** the CLI defers the swap and the notice says so; the old images keep the game running
  (Task 2, `rebuildWhileAGameRunsWaits`).
- **Progress lines split across reads:** they're parsed whole lines only (Task 1, `progressSplitAcrossReadsIsParsed`).
- **No bundled manifest** (dev runs): nothing starts and no notice shows (Task 1, `withoutAManifestNothingStarts`).

---

### Task 1: `AppModel` runs the setup

**Files:**
- Modify: `Sources/MacNeutronApp/AppModel.swift`:
  - a `// MARK: Game VM` section;
  - init parameters `vmManifest: URL? = AppModel.bundledVMManifest`, plus a `vmSetupRunner` closure that defaults to
    starting the helper;
  - the start-up call after `installVMTools()` (plan 1);
  - `pollRuntime` retries a deferral;
  - quitting terminates the child (`NSApplication.willTerminateNotification`).
- Test: `Tests/MacNeutronCoreTests/AppModelTests.swift`. The fake helper is a shell script that prints given lines
  and exits with a given status.

**Interfaces:**
- Consumes: `VMSetup.manifestHash(_:)` (plan 2), `MicroVM.imagesComplete` (plan 1), and
  `VMToolsInstaller.isDeveloperFolder` (plan 1).
- Produces:

```swift
nonisolated static var bundledVMManifest: URL?          // Contents/Resources/vm-manifest.json, nil when absent
private(set) var vmNotice: String?                      // the Global Constraints' texts; nil when no setup ran
private(set) var vmSetupRunning: Bool
var vmSetupNeeded: Bool                                  // manifest present, not a developer folder, hash differs or missing
func setUpVM() async                                    // runs `<helper> vm-setup --manifest <m> --vm <vmRoot>`; one at a time
func rebuildVM() async                                  // Task 2
static func notice(forProgressLine line: String) -> String?   // "progress download 400000000 1350000000" → "Game VM: downloading (0.4 of 1.35 GB)"
```

- [ ] **Step 1:** Tests:
  - `noticeTextsAreExact`: `notice(forProgressLine:)` for the three progress kinds gives the Global Constraints'
    strings. `progress download 400000000 1350000000` gives `Game VM: downloading (0.4 of 1.35 GB)`, and
    `progress assemble 2 5` gives `Game VM: building its images (2 of 5)`.
  - `aNeededSetupStartsAfterTheTools`: with a manifest and an empty VM folder, the fake helper is called with
    `vm-setup --manifest <m> --vm <root>` after the tools install, and `vmNotice` ends `Game VM: ready`.
  - `aCurrentImageSetStartsNothing`: `images/manifest.sha256` equal to the manifest's hash means no helper call.
  - `aDeferralIsRetriedByThePoll`: the helper exits 75, so the notice is `Game VM: waiting for the game to quit`;
    `pollRuntime()` with the helper now exiting 0 gives `ready`.
  - `aFailedSetupWaitsForRetry`: exit 1 with stderr `vm-setup: no space`, so the notice is
    `Game VM: vm-setup: no space`; three `pollRuntime()` calls make no further helper call; `setUpVM()` runs it again.
  - `progressSplitAcrossReadsIsParsed`: the helper writes `progress assem`, sleeps 0.1 s, then writes `ble 1 5\n`;
    the notice becomes `building its images (1 of 5)`.
  - `withoutAManifestNothingStarts`: `vmManifest: nil` means no helper call and `vmNotice == nil`.
  - `quittingStopsTheSetup`: a helper that sleeps 60 s; posting `willTerminateNotification` ends it, and its pid is
    gone within 2 s.
- [ ] **Step 2:** `swift test --filter AppModelTests`. Expected: the new ones FAIL.
- [ ] **Step 3:** Implement.
  - The child process: `Process` with a stdout `Pipe`. Its `readabilityHandler` buffers to newlines and hops to the
    main actor per line.
  - stderr's last line is kept for the failure text.
  - Both streams are appended to `vm-setup.log`.
- [ ] **Step 4:** `swift test`. Expected: all pass.
- [ ] **Step 5:** Commit: "MacNeutron sets up its game VM by itself".

### Task 2: The views, and Rebuild

**Files:**
- Modify: `Sources/MacNeutronApp/SettingsView.swift`, a "Game VM" row beside the runtime and Valve rows:
  - `vmNotice` as secondary text;
  - Retry (when the last run failed) and "Rebuild the game VM" (`role: .destructive`, behind the Global Constraints'
    `confirmationDialog`).
- Modify: `Sources/MacNeutronApp/SetupView.swift`: one more detail line showing `vmNotice` while setup runs.
- Modify: `AppModel.rebuildVM()`: remove `vm/images` and `vm/setup`, then `setUpVM()`.

- [ ] **Step 1:** Tests:
  - `rebuildKeepsTheGamesFiles`: `state/1.img`, `state/1.tpm/id` and `runs/1/run.log` survive `rebuildVM()`;
    `images/` is gone before the helper runs.
  - `rebuildWhileAGameRunsWaits`: the helper exits 75, so the notice is `waiting for the game to quit`.
- [ ] **Step 2:** `swift test --filter AppModelTests`. Expected: FAIL.
- [ ] **Step 3:** Implement the method and the views.
- [ ] **Step 4:** `swift test`, then `swift build -c release`. Expected: all pass, and it builds.
- [ ] **Step 5:** Commit: "Settings: the game VM's status, Retry and Rebuild".

### Task 3: The clean-account run (the maintainer's)

These checks are the maintainer's, on a release candidate, since this session never opens MacNeutron.app.

- [ ] On a macOS user account that has never run MacNeutron, unzip the candidate and open it.
  - The status goes `downloading` → `building` → `ready` with no other prompt.
  - `~/Library/Application Support/MacNeutron/vm` then holds the tools, `images/` (about 2.7 GB) and
    `share/valve/steamclient64.dll`.
- [ ] Launch SMITE 2 from Steam. It runs in the VM: its window comes from `macneutron-vm` and `run.log` is in
  `vm/runs/2437170/`.
- [ ] Record the download size and the setup time in the research README's §11.
