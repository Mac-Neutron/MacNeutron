import AppKit
import MacNeutronCore
import Observation
import ServiceManagement

/// One row of the games window.
struct GameRow: Identifiable, Equatable {
    let app: AppInfo
    let installed: Bool
    var settings: GameSettings
    /// The game's own launch options in Steam (read only); nil when it has none.
    var steamLaunchOptions: String?

    var id: UInt32 { app.appID }
    var name: String { app.name.isEmpty ? "App \(app.appID)" : app.name }
    var isDualPlatform: Bool { app.oslist.contains("macos") && app.oslist.contains("windows") }
    var runsWithMacNeutron: Bool { !app.oslist.contains("macos") || (isDualPlatform && settings.runAs == .windows) }
}

/// The app's login item; a seam so tests don't register the real app with macOS.
struct LoginItem: Sendable {
    var status: @Sendable () -> SMAppService.Status = { SMAppService.mainApp.status }
    var register: @Sendable () throws -> Void = { try SMAppService.mainApp.register() }
    var unregister: @Sendable () throws -> Void = { try SMAppService.mainApp.unregister() }
}

/// The `wine.app` in the tool folder: its `CFBundleShortVersionString` and identity (CDHash).
struct InstalledRuntime: Equatable, Sendable {
    let version: String
    let identity: String

    /// Nil when `wine.app` is missing, has no version or isn't signed.
    static func read(_ layout: ToolLayout, identity: (URL) -> String?) -> InstalledRuntime? {
        guard let version = layout.runtimeVersion, let id = identity(layout.wineApp) else { return nil }
        return InstalledRuntime(version: version, identity: id)
    }

    var label: String { ToolLayout.runtimeLabel(version: version, identity: identity) }
}

/// Everything `refresh()` reads from disk, built off the main thread (parsing the app list and sizing
/// leftover prefixes can take a while; checking Steam spawns a process).
struct Snapshot: Sendable {
    var runtime: InstalledRuntime?
    var apps: [AppInfo]
    var appInfoError: String?
    var installed: Set<UInt32>
    var settings: [String: GameSettings]
    var orphans: [OrphanPrefix]
    var status: SteamPlayStatus
    /// Each game's launch options in Steam, by app ID.
    var steamLaunchOptions: [UInt32: String] = [:]
}

/// Everything the menu, setup, games and settings views show, and every action they take.
@Observable @MainActor
final class AppModel {
    let steam: SteamLocation
    let layout: ToolLayout
    let mode: SteamPlayMode
    let store: GameSettingsStore
    let loginItem: LoginItem
    /// MacNeutron.app's `Contents/Helpers/wine.app`; nil in dev runs and tests: nothing to install.
    let wineAppSource: URL?
    private let launcherOverride: URL?
    private let installer: @Sendable (URL, ToolLayout, URL, Bool) throws -> RuntimeInstallOutcome
    private let identity: @Sendable (URL) -> String?
    /// Valve's signed Windows client files in `layout.valveCache` (`ValveClient`).
    let valveClient: ValveClient
    private let appVersion: String
    /// MacNeutron.app's `Contents/Helpers/vm`: the microVM's host tools (`VMToolsInstaller`); nil in dev runs and tests.
    let vmTools: URL?
    /// The VM folder: nil unless the caller names it (the app passes `MicroVM.standardRoot` where it ships the VM's
    /// tools), so a test never reaches the real one.
    let vmRoot: URL?
    private let vmInstaller: @Sendable (URL, URL) throws -> VMToolsInstallOutcome
    /// A VM ran from the folder at the last tools install: the poll retries it.
    private(set) var vmToolsDeferred = false
    private var vmToolsInstalling = false
    /// MacNeutron.app's `Contents/Resources/vm-manifest.json`: the image set this app's VM needs; nil in dev runs and tests.
    let vmManifest: URL?
    /// The CLI whose `vm-setup` builds it (`helper` unless a test gives its own).
    private let vmHelper: URL?
    private let vmSetupLog: URL
    /// The game VM's setup, as Settings and Setup show it; nil when none ran.
    private(set) var vmNotice: String?
    private(set) var vmSetupRunning = false
    /// The setup started by a tools install.
    private(set) var vmSetupTask: Task<Void, Never>?
    /// The last setup exited 75 (a VM ran from the folder): the poll retries it, every `vmRetryInterval` seconds.
    private var vmSetupDeferred = false
    private var vmSetupDeferredAt = Date.distantPast
    private let vmRetryInterval: TimeInterval
    /// The last setup failed: nothing retries it but Retry or Rebuild.
    private(set) var vmSetupFailed = false
    private var vmProcess: Process?

    private(set) var runtime: InstalledRuntime?
    /// Setup's runtime text while installing, deferred or failed; nil once installed.
    private(set) var runtimeNotice: String?
    /// The install started by init.
    private(set) var installTask: Task<Void, Never>?
    /// Never `busy`: a deferred install retries for a whole game session, and `busy` blocks Steam actions.
    private(set) var installing = false
    /// A deferred install's `force`, retried by the poll.
    private var deferredForce: Bool?
    /// The last install threw: the poll doesn't retry it on `runtime-damaged` (Repair does).
    private var installFailed = false
    /// What the last refresh read; `status` adds the restart an install asked for.
    private var readStatus: SteamPlayStatus = .off
    /// An install replaced the tools' Rosetta-era entry points (`proton`, `passthrough.sh`) while Steam ran, and
    /// Steam may keep the manifests it read at its start (Ruling 27). Cleared when Steam quits or is restarted here.
    private var toolsChangedUnderSteam = false
    var status: SteamPlayStatus {
        switch readStatus {
        case .on where toolsChangedUnderSteam: .restartNeeded(1)
        case .restartNeeded(let count) where toolsChangedUnderSteam: .restartNeeded(count + 1)
        default: readStatus
        }
    }
    /// The version of Valve's files the launcher puts in prefixes; nil when none is downloaded.
    private(set) var valveVersion: String?
    /// "Downloading…" while working, the failure after one; nil otherwise.
    private(set) var valveNotice: String?
    private(set) var valveUpdating = false
    /// The automatic check a successful runtime install starts.
    private(set) var valveTask: Task<Void, Never>?
    private(set) var games: [GameRow] = []
    private(set) var orphans: [OrphanPrefix] = []
    private(set) var appInfoError: String?
    private(set) var loginItemStatus: SMAppService.Status = .notRegistered
    var busy: String?
    var errorMessage: String?

    private var apps: [AppInfo] = []
    private var watcher = SteamWatcher()
    private var pollTask: Task<Void, Never>?
    /// Bumped by every change the model makes on disk.
    private(set) var generation = 0

    init(steam: SteamLocation = SteamLocation(), layout: ToolLayout = ToolLayout(root: ToolLayout.defaultRoot),
         mode: SteamPlayMode = SteamPlayMode(), store: GameSettingsStore = GameSettingsStore(),
         loginItem: LoginItem = LoginItem(), wineAppSource: URL? = AppModel.bundledWineApp, launcherBinary: URL? = nil,
         installer: @escaping @Sendable (URL, ToolLayout, URL, Bool) throws -> RuntimeInstallOutcome = {
             try RuntimeInstaller.install(wineApp: $0, layout: $1, launcherBinary: $2, force: $3)
         },
         identity: @escaping @Sendable (URL) -> String? = CodeIdentity.of,
         valveClient: ValveClient? = nil, appVersion: String = AppModel.appVersion,
         vmTools: URL? = AppModel.bundledVMTools, vmRoot: URL? = nil,
         vmInstaller: @escaping @Sendable (URL, URL) throws -> VMToolsInstallOutcome = {
             try VMToolsInstaller.install(from: $0, scripts: AppModel.bundledVMScripts, into: $1)
         },
         vmManifest: URL? = AppModel.bundledVMManifest, vmHelper: URL? = nil,
         vmSetupLog: URL = LauncherLog.standard.directory.appending(path: "vm-setup.log"),
         notifications: NotificationCenter = .default, vmRetryInterval: TimeInterval = 30) {
        self.steam = steam
        self.layout = layout
        self.mode = mode
        self.store = store
        self.loginItem = loginItem
        self.wineAppSource = wineAppSource
        self.launcherOverride = launcherBinary
        self.installer = installer
        self.identity = identity
        self.valveClient = valveClient ?? ValveClient(cache: layout.valveCache)
        self.appVersion = appVersion
        self.vmTools = vmTools
        self.vmRoot = vmRoot
        self.vmRetryInterval = vmRetryInterval
        self.vmInstaller = vmInstaller
        self.vmManifest = vmManifest
        self.vmHelper = vmHelper
        self.vmSetupLog = vmSetupLog
        valveVersion = ValveClient.installed(in: self.valveClient.cache)?.version
        // Read now, not in the first refresh: the scene decides at launch whether to open the setup window.
        runtime = InstalledRuntime.read(layout, identity: identity)
        Task { await refresh() }
        installTask = Task { [weak self] in await self?.installRuntime() }
        startWatchingSteam()
        // Quitting ends a setup's child: the next start resumes from its downloads.
        notifications.addObserver(forName: NSApplication.willTerminateNotification, object: nil,
                                  queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.vmProcess?.terminate() }
        }
    }

    var setupComplete: Bool { runtime != nil && mode.isWanted }
    var steamInstalled: Bool { steam.isInstalled }

    /// `CFBundleVersion`: the automatic Valve check runs once per app version (spec D4).
    nonisolated static var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "dev"
    }

    /// `Contents/Resources/vm-manifest.json`: the image set this app's VM needs.
    nonisolated static var bundledVMManifest: URL? {
        let manifest = Bundle.main.bundleURL.appending(path: "Contents/Resources/vm-manifest.json")
        return FileManager.default.fileExists(atPath: manifest.path(percentEncoded: false)) ? manifest : nil
    }

    /// `Contents/Resources/vm`: the VM tools' scripts (`VMToolsInstaller.scripts`).
    nonisolated static var bundledVMScripts: URL? {
        let scripts = Bundle.main.bundleURL.appending(path: "Contents/Resources/vm", directoryHint: .isDirectory)
        return FileManager.default.fileExists(atPath: scripts.path(percentEncoded: false)) ? scripts : nil
    }

    nonisolated static var bundledVMTools: URL? {
        let tools = Bundle.main.bundleURL.appending(path: "Contents/Helpers/vm", directoryHint: .isDirectory)
        return FileManager.default.fileExists(atPath: tools.path(percentEncoded: false)) ? tools : nil
    }

    nonisolated static var bundledWineApp: URL? {
        let app = Bundle.main.bundleURL.appending(path: "Contents/Helpers/wine.app", directoryHint: .isDirectory)
        return FileManager.default.fileExists(atPath: app.path(percentEncoded: false)) ? app : nil
    }

    /// The `macneutron` CLI inside the app bundle (or next to the executable during development).
    var helper: URL {
        let executable = Bundle.main.executableURL ?? URL(filePath: CommandLine.arguments[0])
        let bundled = executable.deletingLastPathComponent().deletingLastPathComponent()
            .appending(path: "Helpers/macneutron")
        return FileManager.default.fileExists(atPath: bundled.path(percentEncoded: false))
            ? bundled : executable.deletingLastPathComponent().appending(path: "macneutron")
    }

    func refresh() async {
        let (steam, layout, store, mode, fallback, identity) = (self.steam, self.layout, self.store, self.mode, apps, self.identity)
        let mine = generation
        let snapshot = await Task.detached {
            Self.loadSnapshot(steam: steam, layout: layout, store: store, mode: mode, fallbackApps: fallback, identity: identity)
        }.value
        apply(snapshot, generation: mine)
    }

    /// Drops a snapshot read before a settings change or cleanup: it would put the old state back.
    func apply(_ snapshot: Snapshot, generation mine: Int) {
        guard mine == generation else { return }
        runtime = snapshot.runtime
        apps = snapshot.apps
        appInfoError = snapshot.appInfoError
        games = snapshot.apps
            .filter { MappingPlanner.mappableTypes.contains($0.type) && !$0.oslist.isDisjoint(with: ["windows", "macos"]) }
            .map { GameRow(app: $0, installed: snapshot.installed.contains($0.appID),
                           settings: snapshot.settings[String($0.appID)] ?? GameSettings(),
                           steamLaunchOptions: snapshot.steamLaunchOptions[$0.appID]) }
            .sorted { ($0.installed ? 0 : 1, $0.name.lowercased()) < ($1.installed ? 0 : 1, $1.name.lowercased()) }
        orphans = snapshot.orphans
        readStatus = snapshot.status
        loginItemStatus = loginItem.status()
    }

    /// Reads everything from disk. On an unreadable app list it keeps `fallbackApps`: an empty list
    /// would plan away every Mac game's protection.
    nonisolated static func loadSnapshot(steam: SteamLocation, layout: ToolLayout, store: GameSettingsStore,
                                         mode: SteamPlayMode, fallbackApps: [AppInfo],
                                         identity: (URL) -> String? = CodeIdentity.of) -> Snapshot {
        var apps = fallbackApps
        var appInfoError: String?
        do { apps = try AppInfoReader.read(steam.appInfo) } catch { appInfoError = "\(error)" }
        let plan = MappingPlanner.plan(apps: apps, runAs: store.runAsOverrides())
        return Snapshot(runtime: InstalledRuntime.read(layout, identity: identity), apps: apps,
                        appInfoError: appInfoError, installed: steam.installedAppIDs(), settings: store.all(),
                        orphans: OrphanPrefixes.find(in: steam), status: mode.status(plan: plan),
                        steamLaunchOptions: steam.localConfig.map(LaunchOptions.steamLines(localConfig:)) ?? [:])
    }

    func plan() -> [String: ToolMapping] {
        MappingPlanner.plan(apps: apps, runAs: store.runAsOverrides())
    }

    // MARK: Runtime

    /// Installs the bundled `wine.app` (spec §3.9) off the main thread. Deferred while a game runs from the old one.
    func installRuntime(force: Bool = false) async {
        guard let source = wineAppSource, !installing else { return }
        installing = true
        if deferredForce == nil { runtimeNotice = "Installing the runtime…" }  // a retry keeps the deferral's text
        let (installer, layout, launcher) = (self.installer, self.layout, launcherOverride ?? helper)
        let entryPoints = [layout.root.appending(path: "proton"),
                           mode.tools.appending(path: "\(SteamPlayMode.nativeToolName)/passthrough.sh")]
        let hadOldEntryPoints = entryPoints.contains { FileManager.default.fileExists(atPath: $0.path(percentEncoded: false)) }
        let result = await Task.detached { Result { try installer(source, layout, launcher, force) } }.value
        installing = false
        deferredForce = nil
        installFailed = false
        switch result {
        case .success(.deferred):
            deferredForce = force
            runtimeNotice = "The runtime updates after the game exits."
            return  // nothing changed on disk: no refresh every 3 s for the whole game session
        case .success:
            runtimeNotice = nil
            if mode.isWanted {
                do { try mode.installNativeTool() } catch { errorMessage = "\(error)" }
            }
            if hadOldEntryPoints, await Task.detached(operation: { [mode] in mode.process.isRunning() }).value {
                toolsChangedUnderSteam = true
            }
        case .failure(let error):
            installFailed = true
            runtimeNotice = "\(error)"
        }
        generation += 1  // a refresh that read the folder mid-install would put the old runtime back
        runtime = InstalledRuntime.read(layout, identity: identity)
        if case .success = result { await installVMTools() }
        // D2: no question asked; the files come from Valve's CDN to this Mac, as Linux Steam's legacycompat does for
        // Proton (macOS Steam has no legacycompat). Only for a runtime that can use them (Wine 0036).
        if case .success = result, layout.supportsGenuineSteamclient {
            valveTask = Task { [weak self] in await self?.updateValveClient(force: false) }
        }
        await refresh()
    }

    /// Each poll tick: retry a deferred install; reinstall when a launch found the runtime damaged.
    func pollRuntime() async {
        if vmToolsDeferred { await installVMTools() }
        if vmSetupDeferred, !vmSetupRunning, Date().timeIntervalSince(vmSetupDeferredAt) >= vmRetryInterval {
            await setUpVM()
        }
        if let force = deferredForce {
            await installRuntime(force: force)
        } else if !installFailed, FileManager.default.fileExists(atPath: layout.runtimeDamagedMarker.path(percentEncoded: false)) {
            await installRuntime()
        }
    }

    // MARK: Valve

    /// The automatic check (`force` false: once per app version, never after Remove) or Settings' Download again. One
    /// at a time; a failure shows in Settings only, and launches keep lsteamclient or the last good copy.
    func updateValveClient(force: Bool) async {
        guard !valveUpdating else { return }
        valveUpdating = true
        valveNotice = "Downloading Steam components from Valve…"
        let (client, version) = (valveClient, appVersion)
        let failure: (any Error)? = await Task.detached {
            do {
                _ = force ? try await client.update(force: true) : try await client.updateIfNeeded(appVersion: version)
                return nil
            } catch { return error }
        }.value
        valveUpdating = false
        valveNotice = failure.map { ($0 as? ValveClientError)?.description ?? $0.localizedDescription }
        valveVersion = ValveClient.installed(in: valveClient.cache)?.version
        copyValveClientToVM()
    }

    /// Settings' Remove (spec D5): prefixes get lsteamclient from the next launch, and nothing is downloaded again until
    /// Download again.
    func removeValveClient() {
        do { try valveClient.remove() } catch { valveNotice = error.localizedDescription }
        valveVersion = ValveClient.installed(in: valveClient.cache)?.version
        copyValveClientToVM()
    }

    // MARK: The microVM's host tools

    /// From `Contents/Helpers/vm` into the VM folder after each runtime install, off the main thread; deferred while a
    /// VM runs from the folder (the poll retries). Never into a developer's linked folder.
    func installVMTools() async {
        guard let tools = vmTools, let root = vmRoot, !vmToolsInstalling else { return }
        vmToolsInstalling = true
        let install = vmInstaller
        let result = await Task.detached { Result { try install(tools, root) } }.value
        vmToolsInstalling = false
        vmToolsDeferred = false
        switch result {
        case .success(.deferred): vmToolsDeferred = true
        case .success:
            copyValveClientToVM()
            if vmSetupNeeded, !vmSetupRunning { vmSetupTask = Task { [weak self] in await self?.setUpVM() } }
        case .failure(let error): errorMessage = "\(error)"
        }
    }

    // MARK: The game VM's images

    /// The app ships a manifest, the folder isn't a developer's link, and its images aren't the manifest's.
    var vmSetupNeeded: Bool {
        guard let vmManifest, let vmRoot, !VMToolsInstaller.isDeveloperFolder(vmRoot) else { return false }
        let current = try? String(contentsOf: vmRoot.appending(path: "images/manifest.sha256"), encoding: .utf8)
        return current?.trimmingCharacters(in: .whitespacesAndNewlines) != VMSetup.manifestHash(vmManifest)
    }

    /// `macneutron vm-setup` for the app's manifest (spec §4.6), its progress lines read into `vmNotice` and both its
    /// streams appended to vm-setup.log. One at a time; a deferral (75) waits for the poll, a failure for Retry.
    func setUpVM() async {
        guard let vmManifest, let vmRoot, !vmSetupRunning else { return }
        vmSetupRunning = true
        vmSetupDeferred = false
        vmSetupFailed = false
        vmNotice = "Game VM: starting…"
        let process = Process()
        process.executableURL = vmHelper ?? helper
        let folder = vmRoot.path(percentEncoded: false)
        process.arguments = ["vm-setup", "--manifest", vmManifest.path(percentEncoded: false),
                             "--vm", folder.hasSuffix("/") ? String(folder.dropLast()) : folder]
        let out = Pipe(), err = Pipe()
        process.standardOutput = out
        process.standardError = err
        // Set before it runs: the end of a process that exits at once isn't missed.
        let exited = AsyncStream<Void> { done in process.terminationHandler = { _ in done.finish() } }
        // A performance build (X.Y.Zp) keeps the setup's output for a failure only.
        let quiet = ToolLayout.isPerformanceBuild(version: appVersion)
        var held = Data()
        func openLog() -> FileHandle? {
            try? FileManager.default.createDirectory(at: vmSetupLog.deletingLastPathComponent(), withIntermediateDirectories: true)
            if !FileManager.default.fileExists(atPath: vmSetupLog.path(percentEncoded: false)) {
                FileManager.default.createFile(atPath: vmSetupLog.path(percentEncoded: false), contents: nil)
            }
            let log = try? FileHandle(forWritingTo: vmSetupLog)
            _ = try? log?.seekToEnd()
            return log
        }
        let log = quiet ? nil : openLog()
        func record(_ data: Data) { if quiet { held.append(data) } else { try? log?.write(contentsOf: data) } }
        var status: Int32 = -1, lastError = ""
        do {
            try process.run()
            vmProcess = process
            async let errors = Self.readAll(err.fileHandleForReading)
            for try await line in out.fileHandleForReading.bytes.lines {  // whole lines, however they're written
                record(Data((line + "\n").utf8))
                if let notice = Self.notice(forProgressLine: line) { vmNotice = notice }
            }
            let stderr = await errors
            record(stderr)
            lastError = String(decoding: stderr, as: UTF8.self).split(separator: "\n").last.map(String.init) ?? ""
            for await _ in exited {}
            status = process.terminationStatus
        } catch {
            lastError = "\(error)"
        }
        try? log?.close()
        if quiet, status != 0, status != 75, let failed = openLog() {
            try? failed.write(contentsOf: held)
            try? failed.close()
        }
        vmProcess = nil
        vmSetupRunning = false
        switch status {
        case 0: vmNotice = "Game VM: ready"
        case 75:
            vmSetupDeferred = true
            vmSetupDeferredAt = Date()
            vmNotice = "Game VM: waiting for the game to quit"
        default:
            vmSetupFailed = true
            vmNotice = "Game VM: " + (lastError.isEmpty ? "the setup stopped (\(status))" : lastError)
        }
    }

    /// Settings' Rebuild: the setup's downloads and any half-built set go, then a fresh set is built and swapped in (the
    /// setup waits while a game runs from the old one, then removes it). Games' disks, TPMs and runs are never touched.
    func rebuildVM() async {
        guard vmManifest != nil, let vmRoot, !vmSetupRunning, !VMToolsInstaller.isDeveloperFolder(vmRoot) else { return }
        try? FileManager.default.removeItem(at: vmRoot.appending(path: "setup", directoryHint: .isDirectory))
        await setUpVM()
    }

    /// "progress download <done> <total>" and "progress assemble <k> <n>" as the status line says them; nil otherwise.
    nonisolated static func notice(forProgressLine line: String) -> String? {
        let words = line.split(separator: " ")
        guard words.count == 4, words[0] == "progress", let done = Double(words[2]), let total = Double(words[3]) else {
            return nil
        }
        switch words[1] {
        case "download": return String(format: "Game VM: downloading (%.1f of %.2f GB)", done / 1e9, total / 1e9)
        case "assemble": return "Game VM: building its images (\(words[2]) of \(words[3]))"
        default: return nil
        }
    }

    /// A stream's bytes to its end, without holding a thread while it waits.
    nonisolated static func readAll(_ handle: FileHandle) async -> Data {
        var data = Data()
        do { for try await byte in handle.bytes { data.append(byte) } } catch {}
        return data
    }

    /// Valve's steamclient64.dll for the guest (the VM folder's share/valve), only where the app ships the VM.
    private func copyValveClientToVM() {
        guard vmTools != nil, let vmRoot else { return }
        do {
            try VMToolsInstaller.copyValveClient(ValveClient.installed(in: valveClient.cache), into: vmRoot)
        } catch { errorMessage = "\(error)" }
    }

    // MARK: Setup

    func enableSteamPlay() async {
        guard appInfoError == nil else {
            errorMessage = "MacNeutron can't read Steam's app list, so it can't protect your Mac games: \(appInfoError ?? "")"
            return
        }
        await run("Turning on Steam Play mode and restarting Steam…") { [mode, steam, store] in
            // enable quits Steam itself (after its checks, so a failure leaves Steam running) and then asks
            // for the plan: Steam rewrites its app list on exit.
            try await mode.enable(planAfterQuit: { @Sendable in
                let apps: [AppInfo]
                do { apps = try AppInfoReader.read(steam.appInfo) } catch { throw AppInfoUnreadable(detail: "\(error)") }
                return MappingPlanner.plan(apps: apps, runAs: store.runAsOverrides())
            })
        }
    }

    func disableSteamPlay() async {
        await run("Turning off Steam Play mode and restarting Steam…") { [mode] in try await mode.disable() }
    }

    /// Quit Steam, write pending mappings (or restore lost files), start Steam again.
    func restartSteam() async {
        if status == .lost {
            await enableSteamPlay()
            return
        }
        await run("Restarting Steam…") { [mode] in
            try await mode.process.quit(timeout: .seconds(30))
            await self.refresh()
            if self.appInfoError == nil { try mode.sync(plan: self.plan()) }
            try mode.process.launch()
        }
    }

    // MARK: Games

    /// Saves a game's settings. A "Runs as" change is written to Steam right away when Steam is closed;
    /// otherwise it waits for a restart ("restart needed").
    func update(_ appID: UInt32, _ change: (inout GameSettings) -> Void) async {
        guard let index = games.firstIndex(where: { $0.id == appID }) else { return }
        generation += 1  // a refresh already reading from disk would put the old settings back
        change(&games[index].settings)
        do {
            try store.save(games[index].settings, for: String(appID))
        } catch {
            errorMessage = "Couldn't save settings for \(games[index].name): \(error.localizedDescription)"
        }
        let (mode, plan, readable) = (self.mode, plan(), appInfoError == nil)
        let result = await Task.detached { () -> (SteamPlayStatus, String?) in
            var failure: String?
            if readable, !mode.process.isRunning() {
                do { try mode.sync(plan: plan) } catch { failure = "\(error)" }
            }
            return (mode.status(plan: plan), failure)
        }.value
        readStatus = result.0
        if let failure = result.1 { errorMessage = failure }
    }

    func cleanUp(_ selected: [OrphanPrefix]) async {
        generation += 1
        do { try OrphanPrefixes.delete(selected) } catch { errorMessage = error.localizedDescription }
        await refresh()
    }

    // MARK: Settings

    /// On, or waiting for the user's approval in System Settings.
    var launchesAtLogin: Bool { loginItemStatus == .enabled || loginItemStatus == .requiresApproval }

    func setLaunchAtLogin(_ on: Bool) {
        do {
            if on { try loginItem.register() } else { try loginItem.unregister() }
        } catch {
            errorMessage = "Couldn't change the login item: \(error.localizedDescription)"
        }
        loginItemStatus = loginItem.status()
    }

    // MARK: Plumbing

    private func run(_ message: String, _ work: @escaping () async throws -> Void) async {
        guard busy == nil else { return }  // one Steam-changing action at a time
        busy = message
        errorMessage = nil
        do {
            try await work()
            toolsChangedUnderSteam = false  // every action here restarts or quits Steam
        } catch { errorMessage = "\(error)" }
        busy = nil
        await refresh()
    }

    /// Every 3 s: retry a deferred or damaged runtime install; when Steam quits, bring its mappings up to date;
    /// 20 s after it starts, re-check the files.
    /// Once a minute, re-read Steam's app list so games bought meanwhile show up as "restart needed".
    /// The Steam check and the disk reads run off the main thread.
    private func startWatchingSteam() {
        pollTask = Task { [weak self] in
            var ticks = 0
            while !Task.isCancelled {
                ticks += 1
                guard let self else { return }
                await self.pollRuntime()
                if ticks % 20 == 0, self.busy == nil { await self.refresh() }
                let mode = self.mode
                let running = await Task.detached { mode.process.isRunning() }.value
                switch self.watcher.observe(running: running) {
                case .quit?:
                    self.toolsChangedUnderSteam = false
                    await self.refresh()  // Steam rewrites its app list on exit
                    if self.busy == nil, self.appInfoError == nil {
                        do { try self.mode.sync(plan: self.plan()) } catch { self.errorMessage = "\(error)" }
                        await self.refresh()
                    }
                case .launched?:
                    try? await Task.sleep(for: .seconds(20))
                    await self.refresh()
                case nil:
                    break
                }
                try? await Task.sleep(for: .seconds(3))
            }
        }
    }
}

struct AppInfoUnreadable: Error, CustomStringConvertible {
    let detail: String
    var description: String { "MacNeutron can't read Steam's app list, so it can't protect your Mac games: \(detail)" }
}

extension SteamPlayStatus {
    var menuTitle: String {
        switch self {
        case .off: "Steam Play mode off"
        case .on: "Steam Play mode on"
        case .restartNeeded(let count): "Restart Steam to apply \(count) \(count == 1 ? "change" : "changes")"
        case .lost: "Steam Play mode was turned off by a Steam update"
        case .problem(let message): message
        }
    }

    var symbol: String {
        switch self {
        case .off: "atom"
        case .on: "atom"
        case .restartNeeded: "exclamationmark.circle"
        case .lost, .problem: "exclamationmark.triangle"
        }
    }
}
