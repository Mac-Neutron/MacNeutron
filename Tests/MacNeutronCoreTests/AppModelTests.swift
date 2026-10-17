import AppKit
import Foundation
import ServiceManagement
import Testing
@testable import MacNeutronApp
@testable import MacNeutronCore

private let timberborn = AppInfo(appID: 1062090, name: "Timberborn", type: "game", oslist: ["windows", "macos"])
private let bongoCat = AppInfo(appID: 3419430, name: "Bongo Cat", type: "game", oslist: ["windows", "macos"])
private let cats = AppInfo(appID: 2977660, name: "Cats", type: "game", oslist: ["windows"])

/// A login item that remembers what it was asked to do.
final class FakeLoginItem: @unchecked Sendable {
    private let lock = NSLock()
    private var current: SMAppService.Status = .notRegistered
    let statusAfterRegister: SMAppService.Status
    init(statusAfterRegister: SMAppService.Status) { self.statusAfterRegister = statusAfterRegister }
    var item: LoginItem {
        LoginItem(status: { self.lock.withLock { self.current } },
                  register: { self.lock.withLock { self.current = self.statusAfterRegister } },
                  unregister: { self.lock.withLock { self.current = .notRegistered } })
    }
}

/// Stands in for `RuntimeInstaller.install`: records each call's `force` and answers with the next outcome
/// (the last one repeats). `effect` runs after the recording, like the real install's disk changes.
final class ScriptedInstaller: @unchecked Sendable {
    private let lock = NSLock()
    private var outcomes: [RuntimeInstallOutcome]
    private var forces: [Bool] = []
    let effect: @Sendable (URL, ToolLayout) throws -> Void

    init(_ outcomes: RuntimeInstallOutcome..., effect: @escaping @Sendable (URL, ToolLayout) throws -> Void = { _, _ in }) {
        self.outcomes = outcomes
        self.effect = effect
    }

    var calls: [Bool] { lock.withLock { forces } }

    var install: @Sendable (URL, ToolLayout, URL, Bool) throws -> RuntimeInstallOutcome {
        { source, layout, _, force in
            let outcome = self.lock.withLock {
                self.forces.append(force)
                return self.outcomes.count > 1 ? self.outcomes.removeFirst() : self.outcomes[0]
            }
            try self.effect(source, layout)
            return outcome
        }
    }
}

/// What the real install does to the folder: the source (a fake, unsigned wine.app) replaces the old copy.
private let copiesTheSource: @Sendable (URL, ToolLayout) throws -> Void = { source, layout in
    try? FileManager.default.removeItem(at: layout.wineApp)
    try FileManager.default.createDirectory(at: layout.root, withIntermediateDirectories: true)
    try FileManager.default.copyItem(at: source, to: layout.wineApp)
    try? FileManager.default.removeItem(at: layout.runtimeDamagedMarker)
}

@MainActor
private func makeModel(steamRunning: Bool = false,
                       loginItem: LoginItem = FakeLoginItem(statusAfterRegister: .enabled).item,
                       wineAppSource: URL? = nil, installer: ScriptedInstaller = ScriptedInstaller(.unchanged))
    async throws -> (AppModel, SteamPlayMode, FakeSteam) {
    let (mode, fake) = try makeMode()
    let apps = [timberborn, bongoCat, cats]
    try FileManager.default.createDirectory(at: mode.steam.appInfo.deletingLastPathComponent(), withIntermediateDirectories: true)
    try makeAppInfoV29(apps).write(to: mode.steam.appInfo)
    try await mode.enable(plan: MappingPlanner.plan(apps: apps, runAs: [:]))
    if !steamRunning { try await fake.quit(timeout: .seconds(1)) }
    let model = AppModel(steam: mode.steam, layout: try makeToolLayout(), mode: mode,
                         store: GameSettingsStore(directory: try makeTempDir().appending(path: "games")), loginItem: loginItem,
                         wineAppSource: wineAppSource, installer: installer.install, identity: { _ in testIdentity })
    await model.installTask?.value
    await model.refresh()
    return (model, mode, fake)
}

@MainActor @Test func runsAsChangeAppliesImmediatelyWhenSteamIsClosed() async throws {
    let (model, mode, _) = try await makeModel()
    await model.update(bongoCat.appID) { $0.runAs = .windows }
    #expect(try mode.currentMappings()["3419430"]?.tool == "macneutron-proton")
    #expect(model.status == .on)
}

@MainActor @Test func runsAsChangeWaitsForARestartWhileSteamRuns() async throws {
    let (model, mode, _) = try await makeModel(steamRunning: true)
    await model.update(bongoCat.appID) { $0.runAs = .windows }
    #expect(try mode.currentMappings()["3419430"]?.tool == "macneutron-native")
    #expect(model.status == .restartNeeded(1))
}

@MainActor @Test func refreshKeepsTheLastGoodAppListWhenSteamsCacheIsUnreadable() async throws {
    let (model, mode, _) = try await makeModel()
    try Data("garbage".utf8).write(to: mode.steam.appInfo)
    await model.refresh()
    #expect(model.appInfoError != nil)
    #expect(model.games.map(\.id).sorted() == [1062090, 2977660, 3419430])
}

@Test func snapshotsLoadOffTheMainActor() async throws {
    let (mode, _) = try makeMode()
    try FileManager.default.createDirectory(at: mode.steam.appInfo.deletingLastPathComponent(), withIntermediateDirectories: true)
    try makeAppInfoV29([cats]).write(to: mode.steam.appInfo)
    let store = GameSettingsStore(directory: try makeTempDir())
    let layout = try makeToolLayout()
    let snapshot = await Task.detached {
        AppModel.loadSnapshot(steam: mode.steam, layout: layout, store: store, mode: mode, fallbackApps: [])
    }.value
    #expect(snapshot.apps.map(\.appID) == [2977660])
    #expect(snapshot.status == .off)
}

@MainActor @Test func loginToggleReportsWhenMacOSWantsApproval() async throws {
    let login = FakeLoginItem(statusAfterRegister: .requiresApproval)
    let (model, _, _) = try await makeModel(loginItem: login.item)
    model.setLaunchAtLogin(true)
    #expect(model.loginItemStatus == .requiresApproval)
    #expect(model.errorMessage == nil)
}

@MainActor @Test func setupWindowStaysClosedOnceSetUp() async throws {
    let (model, mode, _) = try await makeModel()
    #expect(model.layout.runtimeVersion == "test")  // the fake wine.app's CFBundleShortVersionString
    let relaunched = AppModel(steam: mode.steam, layout: model.layout, mode: mode, store: model.store,
                              loginItem: model.loginItem, wineAppSource: nil, identity: { _ in testIdentity })
    #expect(relaunched.setupComplete)  // the scene reads it before any refresh finishes
}

@MainActor @Test func aSnapshotReadBeforeAChangeDoesNotUndoIt() async throws {
    let (model, mode, _) = try await makeModel()
    let token = model.generation
    let stale = AppModel.loadSnapshot(steam: mode.steam, layout: model.layout, store: model.store, mode: mode,
                                      fallbackApps: [])
    await model.update(bongoCat.appID) { $0.runAs = .windows }
    model.apply(stale, generation: token)
    #expect(model.games.first { $0.id == bongoCat.appID }?.settings.runAs == .windows)
}

// MARK: The runtime

@MainActor @Test func startupInstallsTheBundledRuntime() async throws {
    let (mode, _) = try makeMode()
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron", directoryHint: .isDirectory))
    let installer = ScriptedInstaller(.installed, effect: copiesTheSource)
    let model = AppModel(steam: mode.steam, layout: layout, mode: mode, store: GameSettingsStore(directory: try makeTempDir()),
                         loginItem: FakeLoginItem(statusAfterRegister: .enabled).item,
                         wineAppSource: try makeToolLayout().wineApp, installer: installer.install, identity: { _ in testIdentity })
    #expect(model.runtime == nil)
    await model.installTask?.value
    #expect(installer.calls == [false])
    #expect(model.runtime == InstalledRuntime(version: "test", identity: testIdentity))
    #expect(model.runtimeNotice == nil)
}

@MainActor @Test func noBundledRuntimeInstallsNothing() async throws {
    let installer = ScriptedInstaller(.installed)
    let (model, _, _) = try await makeModel(installer: installer)
    await model.installRuntime(force: true)
    #expect(installer.calls.isEmpty)
    #expect(model.runtimeNotice == nil)
    #expect(model.runtime == InstalledRuntime(version: "test", identity: testIdentity))  // the one already there
}

@MainActor @Test func aDeferredInstallIsRetriedByThePoll() async throws {
    let installer = ScriptedInstaller(.deferred("/x"), .installed)
    let (model, _, _) = try await makeModel(wineAppSource: try makeToolLayout().wineApp, installer: installer)
    #expect(model.runtimeNotice == "The runtime updates after the game exits.")
    #expect(model.busy == nil)
    #expect(model.generation == 0)  // a deferral changes nothing on disk, so it doesn't refresh
    await model.pollRuntime()
    #expect(installer.calls == [false, false])
    #expect(model.runtimeNotice == nil)
    #expect(model.busy == nil)
    await model.pollRuntime()
    #expect(installer.calls.count == 2)  // nothing pending any more
}

@MainActor @Test func repairForcesAReinstall() async throws {
    let installer = ScriptedInstaller(.unchanged, .installed)
    let (model, _, _) = try await makeModel(wineAppSource: try makeToolLayout().wineApp, installer: installer)
    await model.installRuntime(force: true)
    #expect(installer.calls == [false, true])
    #expect(model.runtimeNotice == nil)
}

@MainActor @Test func oneInstallAtATime() async throws {
    let installer = ScriptedInstaller(.unchanged)
    let (model, _, _) = try await makeModel(wineAppSource: try makeToolLayout().wineApp, installer: installer)
    async let first: Void = model.installRuntime(force: true)
    async let second: Void = model.installRuntime(force: true)
    _ = await (first, second)
    #expect(installer.calls == [false, true])
}

@MainActor @Test func damagedMarkerTriggersAReinstall() async throws {
    let installer = ScriptedInstaller(.unchanged, .installed, effect: copiesTheSource)
    let (model, _, _) = try await makeModel(wineAppSource: try makeToolLayout().wineApp, installer: installer)
    await model.pollRuntime()
    #expect(installer.calls == [false])  // no marker, nothing deferred
    try write("", to: model.layout.runtimeDamagedMarker)
    await model.pollRuntime()
    #expect(installer.calls == [false, false])
    #expect(!FileManager.default.fileExists(atPath: model.layout.runtimeDamagedMarker.path(percentEncoded: false)))
}

@MainActor @Test func aFailedInstallShowsTheErrorAndIsNotRetriedEveryTick() async throws {
    let installer = ScriptedInstaller(.installed, effect: { _, _ in throw RuntimeInstallError.signatureInvalid(1) })
    let (model, _, _) = try await makeModel(wineAppSource: try makeToolLayout().wineApp, installer: installer)
    #expect(model.runtimeNotice == "The runtime's signature check failed (codesign exit 1).")
    #expect(model.runtime != nil)  // the old copy stays
    try write("", to: model.layout.runtimeDamagedMarker)
    await model.pollRuntime()
    #expect(installer.calls == [false])  // Repair retries; the poll doesn't
}

@MainActor @Test func setupCompletesWithRuntimeAndSteamPlay() async throws {
    let (model, mode, _) = try await makeModel()
    #expect(model.setupComplete)
    let unsigned = AppModel(steam: mode.steam, layout: model.layout, mode: mode, store: model.store,
                            loginItem: model.loginItem, wineAppSource: nil, identity: { _ in nil })
    #expect(!unsigned.setupComplete)
    let (off, _) = try makeMode()
    let notWanted = AppModel(steam: off.steam, layout: model.layout, mode: off, store: model.store,
                             loginItem: model.loginItem, wineAppSource: nil, identity: { _ in testIdentity })
    #expect(!notWanted.setupComplete)
}

@MainActor @Test func installRefreshesTheNativeTool() async throws {
    let (model, mode, _) = try await makeModel()
    let newCLI = "#!/bin/sh\necho new macneutron\n"
    let installer = ScriptedInstaller(.installed, effect: { [runtimeTool = mode.runtimeTool] _, _ in
        try write(newCLI, to: runtimeTool.appending(path: "bin/macneutron"), executable: true)  // the install's tool files
    })
    let updated = AppModel(steam: mode.steam, layout: model.layout, mode: mode, store: model.store, loginItem: model.loginItem,
                           wineAppSource: try makeToolLayout().wineApp, installer: installer.install,
                           identity: { _ in testIdentity })
    await updated.installTask?.value
    #expect(installer.calls == [false])
    #expect(try String(contentsOf: mode.nativeTool.appending(path: "bin/macneutron"), encoding: .utf8)
        == newCLI)
}

@MainActor @Test func startLeavesTheNativeToolToTheInstall() async throws {
    // Over an older MacNeutron the tool folder's CLI predates the `passthrough` verb until the install writes the new
    // one: a start must not point the Mac-game tool at it first (here the install is deferred by a running game).
    let (model, mode, _) = try await makeModel()
    let native = mode.tools.appending(path: SteamPlayMode.nativeToolName)
    let oldManifest = "\"manifest\"\n{\n  \"version\" \"2\"\n  \"commandline\" \"/passthrough.sh %verb%\"\n}\n"
    try write("#!/bin/sh\necho old CLI\n", to: mode.tools.appending(path: "macneutron/bin/macneutron"), executable: true)
    try write("#!/bin/sh\n", to: native.appending(path: "passthrough.sh"), executable: true)
    try write(oldManifest, to: native.appending(path: "toolmanifest.vdf"))
    let installer = ScriptedInstaller(.deferred("/x"))
    let started = AppModel(steam: mode.steam, layout: model.layout, mode: mode, store: model.store, loginItem: model.loginItem,
                           wineAppSource: try makeToolLayout().wineApp, installer: installer.install,
                           identity: { _ in testIdentity })
    await started.installTask?.value
    #expect(installer.calls == [false])
    #expect(FileManager.default.fileExists(atPath: native.appending(path: "passthrough.sh").path(percentEncoded: false)))
    #expect(try String(contentsOf: native.appending(path: "toolmanifest.vdf"), encoding: .utf8) == oldManifest)
}

@MainActor @Test func replacingRosettaEraEntryPointsWhileSteamRunsAsksForARestart() async throws {
    // Steam may keep the tool manifests it read at its start (Ruling 27): after an install that removed `proton`,
    // the menu says to restart Steam until it does.
    let installer = ScriptedInstaller(.unchanged, .installed, effect: { _, layout in
        try? FileManager.default.removeItem(at: layout.root.appending(path: "proton"))
    })
    let (model, _, _) = try await makeModel(steamRunning: true, wineAppSource: try makeToolLayout().wineApp,
                                            installer: installer)
    #expect(model.status == .on)
    try write("#!/bin/sh\n", to: model.layout.root.appending(path: "proton"), executable: true)
    await model.installRuntime()
    #expect(model.status == .restartNeeded(1))
    await model.restartSteam()
    #expect(model.status == .on)
}

@MainActor @Test func replacingThePassthroughScriptWhileSteamRunsAsksForARestart() async throws {
    // The Mac-game tool's old entry point is `passthrough.sh`; installNativeTool removes it after the install.
    let (model, mode, _) = try await makeModel(steamRunning: true, wineAppSource: try makeToolLayout().wineApp,
                                               installer: ScriptedInstaller(.unchanged, .installed))
    #expect(model.status == .on)
    try write("#!/bin/sh\n", to: mode.tools.appending(path: "\(SteamPlayMode.nativeToolName)/passthrough.sh"), executable: true)
    await model.installRuntime()
    #expect(model.status == .restartNeeded(1))
}

// MARK: Valve's Steam components (spec D1, D2, D4, D5)

/// A tool folder whose runtime has Wine 0036's marker.
private func supportingLayout() throws -> ToolLayout {
    let layout = try makeToolLayout()
    try write("0036\n", to: layout.wineApp.appending(path: "Contents/Resources/genuine-steamclient"))
    return layout
}

/// One app start: the bundled runtime is already installed (`.unchanged`), then the automatic Valve check runs.
@MainActor
private func makeValveModel(layout: ToolLayout, cdn: FakeValveCDN, appVersion: String = "1.0") async throws -> AppModel {
    let (mode, _) = try makeMode()
    let model = AppModel(steam: mode.steam, layout: layout, mode: mode, store: GameSettingsStore(directory: try makeTempDir()),
                         loginItem: FakeLoginItem(statusAfterRegister: .enabled).item,
                         wineAppSource: try makeToolLayout().wineApp, installer: ScriptedInstaller(.unchanged).install,
                         identity: { _ in testIdentity },
                         valveClient: ValveClient(cache: layout.valveCache, fetch: cdn.fetch), appVersion: appVersion)
    await model.installTask?.value
    await model.valveTask?.value
    return model
}

@MainActor @Test func startupDownloadsValvesFilesWhenTheRuntimeSupportsThem() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let model = try await makeValveModel(layout: try supportingLayout(), cdn: cdn)
    #expect(model.valveVersion == "1788652215")
    #expect(model.valveNotice == nil)
    #expect(cdn.requests == [ValveClient.manifestURL, cdn.packageURL])
}

@MainActor @Test func noMarkerNoDownload() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let model = try await makeValveModel(layout: try makeToolLayout(), cdn: cdn)
    #expect(cdn.requests.isEmpty)
    #expect(model.valveVersion == nil)
}

@MainActor @Test func valveFailureLeavesTheRuntimeInstalled() async throws {
    // Offline at app start: the runtime is fine, launches use lsteamclient, the next app start retries.
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    cdn.offline = true
    let layout = try supportingLayout()
    let model = try await makeValveModel(layout: layout, cdn: cdn)
    #expect(model.runtime != nil)
    #expect(model.runtimeNotice == nil)
    #expect(model.errorMessage == nil)
    #expect(model.valveVersion == nil)
    #expect(model.valveNotice != nil)
    cdn.offline = false
    let next = try await makeValveModel(layout: layout, cdn: cdn)
    #expect(next.valveVersion == "1788652215")
}

@MainActor @Test func removeSticksAcrossAppStarts() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let layout = try supportingLayout()
    let model = try await makeValveModel(layout: layout, cdn: cdn)
    model.removeValveClient()
    #expect(model.valveVersion == nil)
    let next = try await makeValveModel(layout: layout, cdn: cdn, appVersion: "2.0")
    #expect(cdn.requests.count == 2)
    #expect(next.valveVersion == nil)
    await next.updateValveClient(force: true)
    #expect(next.valveVersion == "1788652215")
}

@MainActor @Test func aNewAppVersionChecksAgain() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let layout = try supportingLayout()
    _ = try await makeValveModel(layout: layout, cdn: cdn, appVersion: "1")
    _ = try await makeValveModel(layout: layout, cdn: cdn, appVersion: "1")
    #expect(cdn.requests.count == 2)
    _ = try await makeValveModel(layout: layout, cdn: cdn, appVersion: "2")
    #expect(cdn.requests == [ValveClient.manifestURL, cdn.packageURL, ValveClient.manifestURL])
}

// MARK: The microVM's host tools

/// A stand-in for MacNeutron.app's Contents/Helpers/vm.
private func makeVMTools() throws -> URL {
    let tools = try makeTempDir().appending(path: "vm-tools", directoryHint: .isDirectory)
    for file in ["macneutron-vm", "libkrun/libkrun.1.dylib", "steam-forward/host/mn-steam-host", "gvproxy",
                 "metal-host/winemetal.so", "run-guest.sh", "run-game.sh"] {
        try write("\(file)\n", to: tools.appending(path: file), executable: true)
    }
    return tools
}

/// One app start whose runtime is already installed, with the app's VM tools and a VM folder of the test's own.
@MainActor
private func makeVMModel(vmRoot: URL, layout: ToolLayout? = nil, cdn: FakeValveCDN? = nil,
                         vmInstaller: (@Sendable (URL, URL) throws -> VMToolsInstallOutcome)? = nil) async throws -> AppModel {
    let (mode, _) = try makeMode()
    let layout = try layout ?? makeToolLayout()
    let model = AppModel(steam: mode.steam, layout: layout, mode: mode, store: GameSettingsStore(directory: try makeTempDir()),
                         loginItem: FakeLoginItem(statusAfterRegister: .enabled).item,
                         wineAppSource: try makeToolLayout().wineApp, installer: ScriptedInstaller(.unchanged).install,
                         identity: { _ in testIdentity },
                         valveClient: try cdn.map { ValveClient(cache: layout.valveCache, fetch: $0.fetch) }
                             ?? ValveClient(cache: try makeTempDir()),
                         vmTools: try makeVMTools(), vmRoot: vmRoot,
                         vmInstaller: vmInstaller ?? { try VMToolsInstaller.install(from: $0, into: $1, runningExecutables: { [] }) })
    await model.installTask?.value
    await model.valveTask?.value
    return model
}

@MainActor @Test func theVMToolsFollowTheRuntimeInstall() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    let model = try await makeVMModel(vmRoot: vmRoot)
    #expect(FileManager.default.fileExists(atPath: vmRoot.appending(path: "macneutron-vm").path))
    #expect(!model.vmToolsDeferred)
}

@MainActor @Test func deferredVMToolsAreRetriedByThePoll() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    nonisolated(unsafe) var calls = 0
    let lock = NSLock()
    let model = try await makeVMModel(vmRoot: vmRoot, vmInstaller: { tools, vm in
        if lock.withLock({ calls += 1; return calls == 1 }) { return .deferred(vm.appending(path: "macneutron-vm").path) }
        return try VMToolsInstaller.install(from: tools, into: vm, runningExecutables: { [] })
    })
    #expect(model.vmToolsDeferred)
    #expect(!FileManager.default.fileExists(atPath: vmRoot.appending(path: "macneutron-vm").path))
    await model.pollRuntime()
    #expect(!model.vmToolsDeferred)
    #expect(FileManager.default.fileExists(atPath: vmRoot.appending(path: "macneutron-vm").path))
}

@MainActor @Test func valveFilesReachTheVM() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    let layout = try supportingLayout()
    let model = try await makeVMModel(vmRoot: vmRoot, layout: layout, cdn: try FakeValveCDN(dir: try makeTempDir()))
    let cached = try #require(ValveClient.installed(in: model.valveClient.cache)).steamClient64
    #expect(try Data(contentsOf: vmRoot.appending(path: "share/valve/steamclient64.dll")) == Data(contentsOf: cached))
}

// MARK: The game VM's setup

/// A stand-in for the app's CLI: each run appends its arguments to `calls`, then runs `body`.
private func fakeHelper(_ body: String) throws -> (helper: URL, calls: URL, dir: URL) {
    let dir = try makeTempDir()
    let calls = dir.appending(path: "calls"), helper = dir.appending(path: "macneutron")
    try write("#!/bin/sh\necho \"$*\" >> '\(calls.path)'\n\(body)\n", to: helper, executable: true)
    return (helper, calls, dir)
}

private func calls(_ file: URL) -> [String] {
    ((try? String(contentsOf: file, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}

/// One app start whose runtime is installed, with the app's VM tools, a manifest, and `helper` as its CLI.
@MainActor
private func makeSetupModel(helper: URL, vmRoot: URL, manifest: URL?, awaitSetup: Bool = true,
                            notifications: NotificationCenter = NotificationCenter()) async throws -> AppModel {
    let (mode, _) = try makeMode()
    let model = AppModel(steam: mode.steam, layout: try makeToolLayout(), mode: mode,
                         store: GameSettingsStore(directory: try makeTempDir()),
                         loginItem: FakeLoginItem(statusAfterRegister: .enabled).item,
                         wineAppSource: try makeToolLayout().wineApp, installer: ScriptedInstaller(.unchanged).install,
                         identity: { _ in testIdentity }, valveClient: ValveClient(cache: try makeTempDir()),
                         vmTools: try makeVMTools(), vmRoot: vmRoot,
                         vmInstaller: { try VMToolsInstaller.install(from: $0, into: $1, runningExecutables: { [] }) },
                         vmManifest: manifest, vmHelper: helper, vmSetupLog: try makeTempDir().appending(path: "vm-setup.log"),
                         notifications: notifications)
    await model.installTask?.value
    if awaitSetup { await model.vmSetupTask?.value }
    return model
}

private func makeManifest() throws -> URL {
    let manifest = try makeTempDir().appending(path: "vm-manifest.json")
    try write("{\"version\": 1}\n", to: manifest)
    return manifest
}

@Test func noticeTextsAreExact() {
    #expect(AppModel.notice(forProgressLine: "progress download 400000000 1350000000") == "Game VM: downloading (0.4 of 1.35 GB)")
    #expect(AppModel.notice(forProgressLine: "progress assemble 2 5") == "Game VM: building its images (2 of 5)")
    #expect(AppModel.notice(forProgressLine: "progress install 0 1") == nil)
    #expect(AppModel.notice(forProgressLine: "vm-setup: a note") == nil)
}

@Test func progressSplitAcrossReadsIsParsed() {
    var buffer = Data("progress assem".utf8)
    #expect(AppModel.takeLines(&buffer).isEmpty)
    buffer += Data("ble 1 5\nprogress ass".utf8)
    #expect(AppModel.takeLines(&buffer) == ["progress assemble 1 5"])
    #expect(buffer == Data("progress ass".utf8))
}

@MainActor @Test func aNeededSetupStartsAfterTheTools() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    let fake = try fakeHelper("[ -e '\(vmRoot.path)/macneutron-vm' ] && echo tools-first >> '\(try makeTempDir().path)/x'\n"
                              + "echo 'progress download 400000000 1350000000'\necho 'progress assemble 2 5'\nexit 0")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    #expect(calls(fake.calls) == ["vm-setup --manifest \(manifest.path) --vm \(vmRoot.path)"])
    #expect(FileManager.default.fileExists(atPath: vmRoot.appending(path: "macneutron-vm").path))
    #expect(model.vmNotice == "Game VM: ready")
}

@MainActor @Test func aCurrentImageSetStartsNothing() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    try write(VMSetup.manifestHash(manifest)! + "\n", to: vmRoot.appending(path: "images/manifest.sha256"))
    let fake = try fakeHelper("exit 0")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    #expect(calls(fake.calls).isEmpty)
    #expect(model.vmNotice == nil)
}

@MainActor @Test func aDeferralIsRetriedByThePoll() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    let flag = try makeTempDir().appending(path: "ran")
    let fake = try fakeHelper("[ -e '\(flag.path)' ] && exit 0\ntouch '\(flag.path)'\nexit 75")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    #expect(model.vmNotice == "Game VM: waiting for the game to quit")
    await model.pollRuntime()
    #expect(model.vmNotice == "Game VM: ready")
    #expect(calls(fake.calls).count == 2)
}

@MainActor @Test func aFailedSetupWaitsForRetry() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    let fake = try fakeHelper("echo 'vm-setup: it needs 6 GB free' >&2\nexit 1")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    #expect(model.vmNotice == "Game VM: vm-setup: it needs 6 GB free")
    for _ in 0..<3 { await model.pollRuntime() }
    #expect(calls(fake.calls).count == 1)
    await model.setUpVM()
    #expect(calls(fake.calls).count == 2)
}

@MainActor @Test func withoutAManifestNothingStarts() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    let fake = try fakeHelper("exit 0")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: nil)
    #expect(calls(fake.calls).isEmpty)
    #expect(model.vmNotice == nil)
}

@MainActor @Test func quittingStopsTheSetup() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    let pidFile = try makeTempDir().appending(path: "pid")
    let fake = try fakeHelper("echo $$ > '\(pidFile.path)'\nexec sleep 60")
    let center = NotificationCenter()  // its own: other tests' models run alongside
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest, awaitSetup: false,
                                         notifications: center)
    let start = Date()
    while !FileManager.default.fileExists(atPath: pidFile.path), Date().timeIntervalSince(start) < 5 {
        try await Task.sleep(for: .milliseconds(20))
    }
    let pid = try #require(Int32(String(contentsOf: pidFile, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)))
    center.post(name: NSApplication.willTerminateNotification, object: nil)
    await model.vmSetupTask?.value
    #expect(Date().timeIntervalSince(start) < 10)
    #expect(kill(pid, 0) != 0)
}

@MainActor @Test func rebuildKeepsTheGamesFiles() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    try write(VMSetup.manifestHash(manifest)! + "\n", to: vmRoot.appending(path: "images/manifest.sha256"))
    let kept = ["state/1.img", "state/1.tpm/id", "runs/1/run.log"]
    for path in kept { try write("game \(path)\n", to: vmRoot.appending(path: path)) }
    try write("half a download\n", to: vmRoot.appending(path: "setup/cache/x"))
    let seen = try makeTempDir().appending(path: "setup-seen")
    let fake = try fakeHelper("[ -e '\(vmRoot.path)/setup' ] && touch '\(seen.path)'\nexit 0")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    #expect(calls(fake.calls).isEmpty)  // the images are current
    await model.rebuildVM()
    #expect(calls(fake.calls).count == 1)
    #expect(!FileManager.default.fileExists(atPath: seen.path))
    for path in kept {
        #expect(try String(contentsOf: vmRoot.appending(path: path), encoding: .utf8) == "game \(path)\n")
    }
    #expect(model.vmNotice == "Game VM: ready")
}

@MainActor @Test func rebuildWhileAGameRunsWaits() async throws {
    let vmRoot = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory), manifest = try makeManifest()
    try write(VMSetup.manifestHash(manifest)! + "\n", to: vmRoot.appending(path: "images/manifest.sha256"))
    let fake = try fakeHelper("exit 75")
    let model = try await makeSetupModel(helper: fake.helper, vmRoot: vmRoot, manifest: manifest)
    await model.rebuildVM()
    #expect(model.vmNotice == "Game VM: waiting for the game to quit")
    #expect(FileManager.default.fileExists(atPath: vmRoot.appending(path: "images/manifest.sha256").path))
}
