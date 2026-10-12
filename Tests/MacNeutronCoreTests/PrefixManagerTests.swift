import Foundation
import Testing
@testable import MacNeutronCore

private func makeManager(_ runner: FakeRunner, backend: GraphicsBackend = .dxmt) throws -> (PrefixManager, [String: String]) {
    let layout = try makeToolLayout()
    let context = try CompatContext(environment: steamEnvironment(dataPath: try makeTempDir().appending(path: "compatdata/42")))
    let manager = PrefixManager(context: context, layout: layout, identity: "id1", runner: runner,
                                log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")))
    return (manager, wineEnvironment(manager, backend: backend))
}

/// The environment the launcher builds for this manager's game; `extra` stands for launch options.
private func wineEnvironment(_ manager: PrefixManager, backend: GraphicsBackend = .dxmt,
                             _ extra: [String: String] = [:]) -> [String: String] {
    let base = steamEnvironment(dataPath: manager.context.dataPath).merging(extra) { _, new in new }
    return LaunchEnvironment.build(base: base, context: manager.context, backend: backend, logging: false)
}

private func stamp(_ manager: PrefixManager) -> String? {
    try? String(contentsOf: manager.context.versionFile, encoding: .utf8)
}

private func launcherLog(_ manager: PrefixManager) -> String {
    (try? String(contentsOf: manager.log.launcherLog, encoding: .utf8)) ?? ""
}

private func winebootCount(_ runner: FakeRunner) -> Int { runner.calls.filter { $0.arguments.first == "wineboot" }.count }

/// x64 code runs on FEX; without this entry, on Wine's stub.
private let registerFEX = ["reg", "add", #"HKLM\Software\Microsoft\Wow64\amd64"#, "/ve", "/d", "libarm64ecfex.dll", "/f"]
/// A crashing game must exit, not wait forever behind Wine's crash window (Steam would show it running).
private let disableCrashDialog = ["reg", "add", #"HKCU\Software\Wine\WineDbg"#, "/v", "ShowCrashDialog",
                                  "/t", "REG_DWORD", "/d", "0", "/f"]
/// A game's exclusive fullscreen changes Wine's virtual mode, never the Mac's display (XeSS plan, Task FS).
private let emulateModeset = ["reg", "add", #"HKCU\Software\Wine\X11 Driver"#, "/v", "EmulateModeset", "/d", "Y", "/f"]

@Test func freshPrefixRunsWinebootAndRecordsVersion() throws {
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    try manager.prepare(environment: env)
    #expect(runner.calls.map { [$0.tool] + $0.arguments } == [
        ["wine", "wineboot", "-u"], ["wine"] + registerFEX, ["wine"] + disableCrashDialog, ["wine"] + emulateModeset,
        ["wineserver", "-w"],
    ])
    #expect(runner.calls.allSatisfy { $0.environment["WINEPREFIX"] == manager.context.prefix.path(percentEncoded: false) })
    #expect(runner.calls[0].environment["WINEDLLOVERRIDES"]?.contains("mscoree=;mshtml=") == true)
    #expect(runner.calls[1].environment["WINEDLLOVERRIDES"] == env["WINEDLLOVERRIDES"])
    #expect(stamp(manager) == "wine.app id1 msync=1")
    #expect(PrefixManager.stamp(identity: "id1", msync: false) == "wine.app id1 msync=0")
}

@Test func failedWinebootSkipsTheRegistrySteps() throws {
    let runner = winebootCreatingPrefix(status: 3)
    let (manager, env) = try makeManager(runner)
    #expect(throws: PrefixError.winebootFailed(3)) { try manager.prepare(environment: env) }
    #expect(runner.calls.map(\.arguments) == [["wineboot", "-u"]])
    #expect(stamp(manager) == "wine.app preparing")
}

@Test func upToDatePrefixSkipsWineboot() throws {
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    try manager.prepare(environment: env)
    try manager.prepare(environment: env)
    #expect(winebootCount(runner) == 1)
    #expect(runner.calls.count == 5)
}

@Test func identityChangePreparesInPlace() throws {
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    let save = manager.context.prefix.appending(path: "drive_c/users/steamuser/save.dat")
    try write("progress", to: save)
    try write("wine.app id0 msync=1", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    #expect(winebootCount(runner) == 1)
    // Prefixes made before EmulateModeset was a default get it with the runtime that brings it.
    #expect(runner.calls.contains { $0.arguments == emulateModeset })
    #expect(try String(contentsOf: save, encoding: .utf8) == "progress")
    #expect(!FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "pfx.rosetta").path(percentEncoded: false)))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func rosettaEraPrefixIsRenamedNeverDeleted() throws {
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    try write("progress", to: manager.context.prefix.appending(path: "drive_c/users/steamuser/save.dat"))
    try write("runtime-v4.7.3", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    let renamed = manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users/steamuser/save.dat")
    #expect(try String(contentsOf: renamed, encoding: .utf8) == "progress")
    #expect(FileManager.default.fileExists(atPath: manager.context.prefix.path(percentEncoded: false)))
    #expect(!FileManager.default.fileExists(
        atPath: manager.context.prefix.appending(path: "drive_c/users/steamuser/save.dat").path(percentEncoded: false)))
    #expect(launcherLog(manager).contains("note: renamed a Rosetta-era prefix to pfx.rosetta\n"))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func renameNumbersPastTakenNamesAndKeepsTheSaves() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    let data = manager.context.dataPath
    try write("first", to: data.appending(path: "pfx.rosetta/save.dat"))
    try write("second", to: data.appending(path: "pfx.rosetta-2/save.dat"))
    try write("third", to: manager.context.prefix.appending(path: "save.dat"))  // no stamp at all: Rosetta-era too
    try write("WINE REGISTRY Version 2\n", to: manager.context.prefix.appending(path: "user.reg"))  // Wine made it, not Steam
    try manager.prepare(environment: env)
    #expect(try String(contentsOf: data.appending(path: "pfx.rosetta/save.dat"), encoding: .utf8) == "first")
    #expect(try String(contentsOf: data.appending(path: "pfx.rosetta-2/save.dat"), encoding: .utf8) == "second")
    #expect(try String(contentsOf: data.appending(path: "pfx.rosetta-3/save.dat"), encoding: .utf8) == "third")
    #expect(launcherLog(manager).contains("note: renamed a Rosetta-era prefix to pfx.rosetta-3\n"))
}

@Test func preparingStampIsRetriedInPlace() throws {
    // A failed or stopped preparation of an arm64 prefix is retried, never renamed.
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    let save = manager.context.prefix.appending(path: "drive_c/users/steamuser/save.dat")
    try write("progress", to: save)
    try write("wine.app preparing", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    #expect(winebootCount(runner) == 1)
    #expect(try String(contentsOf: save, encoding: .utf8) == "progress")
    #expect(!launcherLog(manager).contains("renamed"))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func failedFEXRegistrationFailsThePreparation() throws {
    let runner = FakeRunner { call in
        if call.arguments.first == "wineboot", let prefix = call.environment["WINEPREFIX"] {
            try? FileManager.default.createDirectory(atPath: prefix, withIntermediateDirectories: true)
        }
        return call.arguments == registerFEX ? 5 : 0
    }
    let (manager, env) = try makeManager(runner)
    #expect(throws: PrefixError.emulatorSetupFailed(5)) { try manager.prepare(environment: env) }
    #expect(!runner.calls.contains { $0.arguments == disableCrashDialog })
    #expect(stamp(manager) == "wine.app preparing")
}

@Test func msyncOnlyChangeKillsTheServerUnderTheOldMode() throws {
    let runner = winebootCreatingPrefix()
    let (manager, env) = try makeManager(runner)
    try manager.prepare(environment: env)
    let first = runner.calls.count
    try manager.prepare(environment: wineEnvironment(manager, ["MACNEUTRON_NO_MSYNC": "1"]))
    let killed = Array(runner.calls.dropFirst(first))
    #expect(killed.map { [$0.tool] + $0.arguments } == [["wineserver", "-k"]])
    #expect(killed.first?.environment["WINEMSYNC"] == "1")
    #expect(stamp(manager) == "wine.app id1 msync=0")
    #expect(launcherLog(manager).contains("note: msync changed, stopped the prefix's wineserver"))
    // And back: the server from the msync-off launch is stopped without WINEMSYNC.
    try manager.prepare(environment: env)
    #expect(runner.calls.last.map { [$0.tool] + $0.arguments } == ["wineserver", "-k"])
    #expect(runner.calls.last?.environment["WINEMSYNC"] == nil)
    #expect(winebootCount(runner) == 1)
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func copiesDXMTIntoSystem32Only() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try manager.prepare(environment: env)
    let windows = manager.context.prefix.appending(path: "drive_c/windows", directoryHint: .isDirectory)
    for dll in ["d3d10core.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll"] {
        #expect(try String(contentsOf: windows.appending(path: "system32/\(dll)"), encoding: .utf8) == "dxmt \(dll)")
    }
    #expect(!FileManager.default.fileExists(atPath: windows.appending(path: "syswow64").path(percentEncoded: false)))
}

@Test func switchingBackToDXMTFindsItsDLLs() throws {
    // The stamp doesn't name the backend: a prefix first prepared for wined3d must already hold DXMT's DLLs.
    let runner = winebootCreatingPrefix()
    let (manager, wined3d) = try makeManager(runner, backend: .wined3d)
    try manager.prepare(environment: wined3d)
    try manager.prepare(environment: wineEnvironment(manager, backend: .dxmt))
    #expect(winebootCount(runner) == 1)
    let d3d11 = manager.context.prefix.appending(path: "drive_c/windows/system32/d3d11.dll")
    #expect(try String(contentsOf: d3d11, encoding: .utf8) == "dxmt d3d11.dll")
}

@Test func missingRuntimeDLLIsAnError() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try FileManager.default.removeItem(at: manager.layout.dxmt.appending(path: "d3d11.dll"))
    #expect(throws: PrefixError.self) { try manager.prepare(environment: env) }
    #expect(stamp(manager) == "wine.app preparing")
}

@Test func concurrentLaunchesRunWinebootOnce() async throws {
    // Steam starts iscriptevaluator (`run`) and the game close together on first launch.
    let runner = winebootCreatingPrefix(delay: 0.3)
    let (manager, env) = try makeManager(runner)
    try await withThrowingTaskGroup(of: Void.self) { group in
        for _ in 0..<2 {
            group.addTask { try manager.prepare(environment: env) }
        }
        try await group.waitForAll()
    }
    #expect(winebootCount(runner) == 1)
}

@Test func systemRunnerReportsStatusAndCapturesOutput() throws {
    let log = try makeTempDir().appending(path: "out.log")
    let status = try SystemProcessRunner().run(URL(filePath: "/bin/sh"), ["-c", "echo \"$1\"; exit 3", "sh", "a b"],
                                               environment: [:], output: log)
    #expect(status == 3)
    #expect(try String(contentsOf: log, encoding: .utf8) == "a b\n")
}

private func steamFolder(_ manager: PrefixManager) -> URL {
    manager.context.prefix.appending(path: "drive_c/Program Files (x86)/Steam", directoryHint: .isDirectory)
}

@Test func steamBridgeIsCopiedIntoTheSteamFolder() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: env, steamBridge: true)
    let folder = steamFolder(manager)
    #expect(try String(contentsOf: folder.appending(path: "steam.exe"), encoding: .utf8) == "steam.exe")
    #expect(try String(contentsOf: folder.appending(path: "steamclient64.dll"), encoding: .utf8) == "lsteamclient aarch64")
    #expect(!FileManager.default.fileExists(atPath: folder.appending(path: "steamclient.dll").path(percentEncoded: false)))
    // Proton's launch path, C:\windows\system32\steam.exe; the Steam folder's copy stays for HKCR\steam.
    #expect(try String(contentsOf: manager.context.prefix.appending(path: "drive_c/windows/system32/steam.exe"),
                       encoding: .utf8) == "steam.exe")
}

@Test func steamFilesComeWithTheBridge() throws {
    // The rest of Proton's setup_steam_files: Steam's config folder (CEG looks for it) and libraryfolders.vdf.
    let (manager, _) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: wineEnvironment(manager, ["STEAM_COMPAT_LIBRARY_PATHS": "/Steam/steamapps"]),
                        steamBridge: true)
    let folder = steamFolder(manager)
    #expect(FileManager.default.fileExists(atPath: folder.appending(path: "config").path(percentEncoded: false)))
    #expect(try String(contentsOf: folder.appending(path: "steamapps/libraryfolders.vdf"), encoding: .utf8)
        == SteamBridge.libraryFolders(steamapps: "/Steam/steamapps"))
}

private func inode(_ url: URL) throws -> UInt64? {
    try FileManager.default.attributesOfItem(atPath: url.path(percentEncoded: false))[.systemFileNumber] as? UInt64
}

@Test func unchangedBridgeFilesAreNotRecopied() throws {
    // lsteamclient.dll is 57 MB: a launch must not rewrite it when nothing changed.
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: env, steamBridge: true)
    let client = steamFolder(manager).appending(path: "steamclient64.dll")
    let before = try inode(client)
    try manager.prepare(environment: env, steamBridge: true)
    #expect(before != nil)
    #expect(try inode(client) == before)
}

@Test func changedBridgeFileIsRecopied() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: env, steamBridge: true)
    try write("lsteamclient v2", to: manager.layout.lsteamclient)
    try manager.prepare(environment: env, steamBridge: true)
    let client = steamFolder(manager).appending(path: "steamclient64.dll")
    #expect(try String(contentsOf: client, encoding: .utf8) == "lsteamclient v2")
    let modified = { (url: URL) throws -> Date? in
        try FileManager.default.attributesOfItem(atPath: url.path(percentEncoded: false))[.modificationDate] as? Date
    }
    #expect(try modified(client) == modified(manager.layout.lsteamclient))
}

@Test func steamClient64OverrideReplacesLsteamclientInThePrefix() throws {
    // The SteamStub spike: SteamStub checks the signature of the prefix's steamclient64.dll, so Valve's goes where
    // lsteamclient.dll goes; the next launch without the option puts lsteamclient.dll back.
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    let genuine = try makeTempDir().appending(path: "valve/steamclient64.dll")
    try FileManager.default.createDirectory(at: genuine.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(machine: PEImage.amd64).write(to: genuine)
    let path = genuine.path(percentEncoded: false)
    try manager.prepare(environment: wineEnvironment(manager, ["MACNEUTRON_STEAMCLIENT64": path]), steamBridge: true)
    let client = steamFolder(manager).appending(path: "steamclient64.dll")
    #expect(try Data(contentsOf: client) == peBytes(machine: PEImage.amd64))
    #expect(launcherLog(manager).contains("steamclient64.dll from \(path)"))
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try String(contentsOf: client, encoding: .utf8) == "lsteamclient aarch64")
}

@Test func steamClient64OverrideThatIsMissingOrNotX64FailsTheLaunch() throws {
    let (manager, _) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    let thirtyTwoBit = try makeTempDir().appending(path: "steamclient.dll")
    try peBytes(machine: PEImage.i386).write(to: thirtyTwoBit)
    for path in [thirtyTwoBit.path(percentEncoded: false), "/nonexistent/steamclient64.dll"] {
        let env = wineEnvironment(manager, ["MACNEUTRON_STEAMCLIENT64": path])
        #expect(throws: PrefixError.self) { try manager.prepare(environment: env, steamBridge: true) }
    }
}

/// Valve's DLL as the app leaves it in `layout.valveCache/<version>/`, with or without the runtime's 0036 marker.
@discardableResult
private func cacheValveClient(_ layout: ToolLayout, version: String = "1788652215", marker: Bool = true) throws -> URL {
    if marker { try write("0036\n", to: layout.wineApp.appending(path: "Contents/Resources/genuine-steamclient")) }
    let dll = layout.valveCache.appending(path: "\(version)/steamclient64.dll")
    try FileManager.default.createDirectory(at: dll.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(machine: PEImage.amd64, certificate: true).write(to: dll)
    return dll
}

@Test func theCachedValveDLLGoesInThePrefixWhenTheRuntimeSupportsIt() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    let dll = try cacheValveClient(manager.layout)
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try Data(contentsOf: steamFolder(manager).appending(path: "steamclient64.dll"))
        == peBytes(machine: PEImage.amd64, certificate: true))
    #expect(launcherLog(manager).contains(
        "note: steamclient64.dll from Valve's client 1788652215 (\(dll.path(percentEncoded: false)))"))
}

@Test func withoutTheMarkerTheCacheIsIgnored() throws {
    // An older runtime can't load Valve's DLL (its tier0_s64 import): the game would get no Steam.
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try cacheValveClient(manager.layout, marker: false)
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try String(contentsOf: steamFolder(manager).appending(path: "steamclient64.dll"), encoding: .utf8)
        == "lsteamclient aarch64")
    #expect(launcherLog(manager).contains("note: steamclient64.dll is lsteamclient: this runtime has no genuine-steamclient marker"))
}

@Test func withoutACacheItIsLsteamclient() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try write("0036\n", to: manager.layout.wineApp.appending(path: "Contents/Resources/genuine-steamclient"))
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try String(contentsOf: steamFolder(manager).appending(path: "steamclient64.dll"), encoding: .utf8)
        == "lsteamclient aarch64")
    #expect(launcherLog(manager).contains("note: steamclient64.dll is lsteamclient: Valve's isn't downloaded"))
}

@Test func aCacheWithoutSteamclient64FallsBackToLsteamclient() throws {
    // A cache damaged by hand: the launch still starts, on lsteamclient.
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    let dll = try cacheValveClient(manager.layout)
    try write("sha2 x\n", to: dll.deletingLastPathComponent().appending(path: "source.txt"))
    try FileManager.default.removeItem(at: dll)
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try String(contentsOf: steamFolder(manager).appending(path: "steamclient64.dll"), encoding: .utf8)
        == "lsteamclient aarch64")
    #expect(launcherLog(manager).contains("note: steamclient64.dll is lsteamclient: Valve's isn't downloaded"))
}

@Test func builtinForcesLsteamclientOverTheCache() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try cacheValveClient(manager.layout)
    try manager.prepare(environment: wineEnvironment(manager, ["MACNEUTRON_STEAMCLIENT64": "builtin"]), steamBridge: true)
    let client = steamFolder(manager).appending(path: "steamclient64.dll")
    #expect(try String(contentsOf: client, encoding: .utf8) == "lsteamclient aarch64")
    #expect(launcherLog(manager).contains("note: steamclient64.dll is lsteamclient (MACNEUTRON_STEAMCLIENT64=builtin)"))
    try manager.prepare(environment: env, steamBridge: true)
    #expect(try Data(contentsOf: client) == peBytes(machine: PEImage.amd64, certificate: true))
}

@Test func aPathOverrideBeatsTheCache() throws {
    let (manager, _) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try cacheValveClient(manager.layout)
    let other = try makeTempDir().appending(path: "steamclient64.dll")
    try peBytes(machine: PEImage.amd64).write(to: other)
    try manager.prepare(environment: wineEnvironment(manager, ["MACNEUTRON_STEAMCLIENT64": other.path(percentEncoded: false)]),
                        steamBridge: true)
    #expect(try Data(contentsOf: steamFolder(manager).appending(path: "steamclient64.dll")) == peBytes(machine: PEImage.amd64))
}

/// `STEAM_COMPAT_LIBRARY_PATHS` and `STEAM_COMPAT_INSTALL_PATH` for a game in a fresh library named `name`.
private func libraryVariables(_ name: String = "Lib Ünï", steamapps: String = "steamapps") throws -> (vars: [String: String], lib: String, apps: String) {
    let lib = try makeTempDir().appending(path: name, directoryHint: .isDirectory)
    let game = lib.appending(path: "\(steamapps)/common/Game", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: game, withIntermediateDirectories: true)
    let libPath = String(lib.path(percentEncoded: false).dropLast()), apps = libPath + "/" + steamapps
    return (["STEAM_COMPAT_LIBRARY_PATHS": apps, "STEAM_COMPAT_INSTALL_PATH": apps + "/common/Game"], libPath, apps)
}

private func gameDrive(_ manager: PrefixManager) -> String? {
    try? FileManager.default.destinationOfSymbolicLink(
        atPath: manager.context.prefix.appending(path: "dosdevices/s:").path(percentEncoded: false))
}

@Test func prepareLinksTheGameDriveWithOrWithoutTheBridge() throws {
    let runner = winebootCreatingPrefix()
    let (manager, _) = try makeManager(runner)
    let (vars, lib, _) = try libraryVariables()
    try manager.prepare(environment: wineEnvironment(manager, vars))
    #expect(gameDrive(manager) == realPath(lib))
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: wineEnvironment(manager, vars), steamBridge: true)
    #expect(gameDrive(manager) == realPath(lib))
    #expect(winebootCount(runner) == 1)
}

@Test func prepareLogsTheGameDriveNotes() throws {
    let (manager, _) = try makeManager(winebootCreatingPrefix())
    let (vars, _, apps) = try libraryVariables(steamapps: "SteamApps")
    try manager.prepare(environment: wineEnvironment(manager, vars))
    #expect(launcherLog(manager).contains("note: Proton: Error: unable to use parent for game drive, path \(apps)"))
    #expect(gameDrive(manager) == apps)
}

@Test func upToDatePrefixStillGetsTheSteamBridge() throws {
    // Prefixes prepared before the bridge existed (SMITE 2's on the maintainer's Mac) must get it too.
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try manager.prepare(environment: env)
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: env, steamBridge: true)
    #expect(FileManager.default.fileExists(
        atPath: steamFolder(manager).appending(path: "steamclient64.dll").path(percentEncoded: false)))
}

@Test func prefixWithoutBridgeRequestGetsNoSteamFolder() throws {
    let (manager, env) = try makeManager(winebootCreatingPrefix())
    try installFakeSteamBridge(in: manager.layout)
    try manager.prepare(environment: env)
    #expect(!FileManager.default.fileExists(atPath: steamFolder(manager).path(percentEncoded: false)))
}

// MARK: Carrying the player's data out of a renamed Rosetta-era prefix

/// A FakeRunner whose `wineboot` makes a prefix like Wine's: `users/<user>` with Wine's own folders, `Documents` a
/// link to `documents` when given (Wine links it to the Mac's), and `user.reg`. `atWait` sees the prefix when
/// `wineserver -w` runs; `afterBoot` adds to the new prefix; `bootStatus` is wineboot's exit (non-zero: nothing made).
private func winebootMakingUser(_ user: String = "steamuser", documents: URL? = nil, userReg: String = newUserReg,
                                bootStatus: @escaping @Sendable () -> Int32 = { 0 },
                                afterBoot: @escaping @Sendable (URL) -> Void = { _ in },
                                atWait: @escaping @Sendable (URL) -> Void = { _ in }) -> FakeRunner {
    FakeRunner { call in
        guard let path = call.environment["WINEPREFIX"] else { return 0 }
        let prefix = URL(filePath: path, directoryHint: .isDirectory)
        if call.arguments.first == "wineboot" {
            let status = bootStatus()
            guard status == 0 else { return status }
            let home = prefix.appending(path: "drive_c/users/\(user)", directoryHint: .isDirectory)
            for folder in ["AppData/Local/Microsoft", "AppData/Local/Temp", "AppData/LocalLow", "AppData/Roaming/Microsoft",
                           "Saved Games"] {
                try? FileManager.default.createDirectory(at: home.appending(path: folder), withIntermediateDirectories: true)
            }
            try? FileManager.default.createDirectory(at: prefix.appending(path: "drive_c/users/Public/Documents"),
                                                     withIntermediateDirectories: true)
            if let documents {
                try? FileManager.default.createSymbolicLink(at: home.appending(path: "Documents"), withDestinationURL: documents)
            } else {
                try? FileManager.default.createDirectory(at: home.appending(path: "Documents"), withIntermediateDirectories: true)
            }
            try? userReg.write(to: prefix.appending(path: "user.reg"), atomically: true, encoding: .utf8)
            afterBoot(prefix)
        } else if call.tool == "wineserver", call.arguments == ["-w"] {
            atWait(prefix)
        }
        return 0
    }
}

private let newUserReg = """
    WINE REGISTRY Version 2
    ;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000

    #arch=win64

    [Software\\\\Wine\\\\WineDbg] 1759600000
    #time=1dc35f0a0000000
    "ShowCrashDialog"=dword:00000000

    [Software\\\\Taken Vendor\\\\Game] 1759600000
    #time=1dc35f0a0000000
    "Fresh"="new"

    """

/// What a Rosetta-era Wine left: Wine's and Windows' keys, Steam's, and the games' own.
private let oldUserReg = """
    WINE REGISTRY Version 2
    ;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000

    #arch=win64

    [Control Panel\\\\Desktop] 1700000000
    "FontSmoothing"="2"

    [Software\\\\Epic Games\\\\Unreal Engine\\\\Identifiers] 1700000000
    #time=1da0000000000000
    "AccountId"="abc"

    [Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer] 1700000000
    "Old"="1"

    [Software\\\\Unity\\\\UnityEditor] 1700000000

    [software\\\\unity technologies\\\\Game] 1700000000
    #time=1da0000000000000
    "Screenmanager Resolution Width_h182942802"=dword:00000a00
    "data_h1"=hex:01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,10,11,12,13,14,15,16,\\
      17,18,19,1a

    [Software\\\\Classes\\\\Thing] 1700000000
    @="x"

    [Software\\\\Policies\\\\Thing] 1700000000

    [Software\\\\Valve\\\\Steam] 1700000000
    "SteamPath"="c:\\\\program files (x86)\\\\steam"

    [Software\\\\Wine\\\\Direct3D] 1700000000
    "renderer"="gl"

    [Software\\\\taken vendor\\\\game] 1700000000
    "Fresh"="old"

    """

/// A Rosetta-era `pfx` (no stamp) whose user `user` has `files` (paths under `drive_c/users/<user>/`, content = path).
private func rosettaPrefix(_ manager: PrefixManager, user: String = "steamuser", files: [String],
                           userReg: String = oldUserReg) throws {
    let home = manager.context.prefix.appending(path: "drive_c/users/\(user)", directoryHint: .isDirectory)
    for file in files { try write(file, to: home.appending(path: file)) }
    try write(userReg, to: manager.context.prefix.appending(path: "user.reg"))
}

private func newPrefixFile(_ manager: PrefixManager, _ path: String) -> String? {
    try? String(contentsOf: manager.context.prefix.appending(path: "drive_c/users/\(path)"), encoding: .utf8)
}

private func makeCarryManager(_ runner: FakeRunner) throws -> (PrefixManager, [String: String]) {
    let layout = try makeToolLayout()
    let context = try CompatContext(environment: steamEnvironment(dataPath: try makeTempDir().appending(path: "compat data/42")))
    let manager = PrefixManager(context: context, layout: layout, identity: "id1", runner: runner,
                                log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")))
    return (manager, wineEnvironment(manager))
}

/// SMITE 2 keeps its settings in AppData/Local/SMITE2Alpha/Saved: a fresh prefix reset them (XeSS, ~4 FPS; with the
/// player's settings the main lobby runs at ~36 FPS on both runtimes).
@Test func renamedPrefixCarriesThePlayersDataIntoTheFreshOne() throws {
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    let files = ["AppData/Local/SMITE2Alpha/Saved/Config/Windows/GameUserSettings.ini",
                 "AppData/Local/SMITE2Alpha/Saved/SaveGames/HWGameUserSettings.sav",
                 "AppData/LocalLow/Studio/Game/prefs.json", "AppData/Roaming/Game/save 1.dat",
                 "Documents/My Games/Game/settings.cfg", "Saved Games/Game/slot1.sav"]
    try rosettaPrefix(manager, files: files)
    try manager.prepare(environment: env)
    for file in files { #expect(newPrefixFile(manager, "steamuser/\(file)") == file) }
    // Read-only: the old prefix still has everything.
    let old = manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users/steamuser")
    for file in files { #expect(try String(contentsOf: old.appending(path: file), encoding: .utf8) == file) }
    #expect(launcherLog(manager).contains(
        "note: carried the player's data from pfx.rosetta: 6 files, 3 registry keys\n"))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func carryNeverOverwritesAndSkipsWinesOwnFolders() throws {
    let (manager, env) = try makeCarryManager(winebootMakingUser(afterBoot: { prefix in
        try? write("fresh", to: prefix.appending(path: "drive_c/users/steamuser/AppData/Roaming/Game/a.cfg"))
    }))
    try rosettaPrefix(manager, files: ["AppData/Roaming/Game/a.cfg", "AppData/Roaming/Game/b.cfg",
                                       "AppData/Local/Temp/junk.tmp", "AppData/Local/Microsoft/Windows/x.dat",
                                       "AppData/Roaming/Microsoft/Crypto/y.dat"])
    try write("public", to: manager.context.prefix.appending(path: "drive_c/users/Public/Documents/p.txt"))
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Roaming/Game/a.cfg") == "fresh")
    #expect(newPrefixFile(manager, "steamuser/AppData/Roaming/Game/b.cfg") == "AppData/Roaming/Game/b.cfg")
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/Temp/junk.tmp") == nil)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/Microsoft/Windows/x.dat") == nil)
    #expect(newPrefixFile(manager, "steamuser/AppData/Roaming/Microsoft/Crypto/y.dat") == nil)
    #expect(newPrefixFile(manager, "Public/Documents/p.txt") == nil)
    #expect(launcherLog(manager).contains("pfx.rosetta: 1 files, 3 registry keys\n"))
}

@Test func carryMapsTheOldUserOntoTheNewOne() throws {
    // wineboot names the user after $USER: a prefix made under another name gets the new single user's folder.
    let (manager, env) = try makeCarryManager(winebootMakingUser("player"))
    try rosettaPrefix(manager, user: "steamuser", files: ["AppData/Local/Game/Saved/save.sav"])
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "player/AppData/Local/Game/Saved/save.sav") == "AppData/Local/Game/Saved/save.sav")
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/Game/Saved/save.sav") == nil)
}

@Test func carryFollowsNoLinks() throws {
    // Wine links Documents to the Mac's ~/Documents: the old link isn't followed, the new one isn't written through.
    let mac = try makeTempDir()
    try write("mac", to: mac.appending(path: "old/notes.txt"))
    let (manager, env) = try makeCarryManager(winebootMakingUser(documents: mac.appending(path: "new")))
    try FileManager.default.createDirectory(at: mac.appending(path: "new"), withIntermediateDirectories: true)
    try rosettaPrefix(manager, files: ["Documents/My Games/Game/settings.cfg", "Saved Games/real.sav"])
    let old = manager.context.prefix.appending(path: "drive_c/users/steamuser")
    try FileManager.default.createSymbolicLink(at: old.appending(path: "Saved Games/linked"),
                                               withDestinationURL: mac.appending(path: "old"))
    try manager.prepare(environment: env)
    #expect(try FileManager.default.contentsOfDirectory(atPath: mac.appending(path: "new").path(percentEncoded: false)) == [])
    #expect(newPrefixFile(manager, "steamuser/Saved Games/real.sav") == "Saved Games/real.sav")
    #expect(newPrefixFile(manager, "steamuser/Saved Games/linked/notes.txt") == nil)
    // Each link in the way counts: the new Documents, and Saved Games/linked.
    #expect(launcherLog(manager).contains("pfx.rosetta: 1 files, 3 registry keys, 2 not carried\n"))
}

private final class LockedBox<T>: @unchecked Sendable {
    private let lock = NSLock()
    private var stored: T
    init(_ value: T) { stored = value }
    var value: T {
        get { lock.withLock { stored } }
        set { lock.withLock { stored = newValue } }
    }
}

@Test func carryAppendsTheGamesRegistryKeysAfterTheServerStopped() throws {
    let atWait = LockedBox<String?>(nil)
    let (manager, env) = try makeCarryManager(winebootMakingUser(atWait: { prefix in
        atWait.value = try? String(contentsOf: prefix.appending(path: "user.reg"), encoding: .utf8)
    }))
    try rosettaPrefix(manager, files: [])
    try manager.prepare(environment: env)
    // Untouched while wineserver ran.
    #expect(atWait.value == newUserReg)
    let carried = """

        [Software\\\\Epic Games\\\\Unreal Engine\\\\Identifiers] 1700000000
        #time=1da0000000000000
        "AccountId"="abc"

        [Software\\\\Unity\\\\UnityEditor] 1700000000

        [software\\\\unity technologies\\\\Game] 1700000000
        #time=1da0000000000000
        "Screenmanager Resolution Width_h182942802"=dword:00000a00
        "data_h1"=hex:01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,10,11,12,13,14,15,16,\\
          17,18,19,1a

        """
    #expect(try String(contentsOf: manager.context.prefix.appending(path: "user.reg"), encoding: .utf8)
        == newUserReg + carried)
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 0 files, 3 registry keys\n"))
}

@Test func unreadableRegistryIsCountedAndTheFilesStillCarry() throws {
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["AppData/Local/Game/save.sav"])
    // An old user.reg that can't be read.
    try FileManager.default.removeItem(at: manager.context.prefix.appending(path: "user.reg"))
    try FileManager.default.createDirectory(at: manager.context.prefix.appending(path: "user.reg"),
                                            withIntermediateDirectories: true)
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/Game/save.sav") == "AppData/Local/Game/save.sav")
    #expect(launcherLog(manager).contains(
        "note: carried the player's data from pfx.rosetta: 1 files, 0 registry keys, 1 not carried\n"))
    #expect(stamp(manager) == "wine.app id1 msync=1")
    #expect(FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "pfx.rosetta/user.reg")
        .path(percentEncoded: false)))
}

@Test func preparingInPlaceCarriesNothing() throws {
    // Only a rename carries: an arm64 prefix prepared again keeps its own data, and a pfx.rosetta beside it is left be.
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try write("old", to: manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"))
    try write("wine.app id0 msync=1", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(!launcherLog(manager).contains("carried"))
}

@Test func unreadableFileDoesNotStopTheCarry() throws {
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["AppData/Local/A/locked.sav", "AppData/Local/B/ok.sav", "Saved Games/G/s.sav"])
    let locked = manager.context.prefix.appending(path: "drive_c/users/steamuser/AppData/Local/A/locked.sav")
    try FileManager.default.setAttributes([.posixPermissions: 0], ofItemAtPath: locked.path(percentEncoded: false))
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/A/locked.sav") == nil)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/B/ok.sav") == "AppData/Local/B/ok.sav")
    #expect(newPrefixFile(manager, "steamuser/Saved Games/G/s.sav") == "Saved Games/G/s.sav")
    #expect(launcherLog(manager).contains(
        "note: carried the player's data from pfx.rosetta: 2 files, 3 registry keys, 1 not carried\n"))
}

@Test func linkedAppDataInTheOldPrefixIsNotFollowed() throws {
    let mac = try makeTempDir()
    try write("mac", to: mac.appending(path: "AppData/Local/G/s.sav"))
    try write("mac", to: mac.appending(path: "user/AppData/Local/G/u.sav"))
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["Saved Games/G/real.sav"])
    let users = manager.context.prefix.appending(path: "drive_c/users")
    try FileManager.default.createSymbolicLink(at: users.appending(path: "steamuser/AppData"),
                                               withDestinationURL: mac.appending(path: "AppData"))
    try FileManager.default.createSymbolicLink(at: users.appending(path: "linkeduser"),
                                               withDestinationURL: mac.appending(path: "user"))
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/u.sav") == nil)
    #expect(newPrefixFile(manager, "linkeduser/AppData/Local/G/u.sav") == nil)
    #expect(newPrefixFile(manager, "steamuser/Saved Games/G/real.sav") == "Saved Games/G/real.sav")
    // The linked user folder, and AppData's three folders.
    #expect(launcherLog(manager).contains("pfx.rosetta: 1 files, 3 registry keys, 4 not carried\n"))
}

@Test func linkedAppDataInTheNewPrefixIsNotWrittenThrough() throws {
    let mac = try makeTempDir()
    let (manager, env) = try makeCarryManager(winebootMakingUser(afterBoot: { prefix in
        let appData = prefix.appending(path: "drive_c/users/steamuser/AppData")
        try? FileManager.default.removeItem(at: appData)
        try? FileManager.default.createSymbolicLink(at: appData, withDestinationURL: mac)
    }))
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav", "AppData/Roaming/G/r.sav", "Saved Games/G/real.sav"])
    try manager.prepare(environment: env)
    #expect(try FileManager.default.contentsOfDirectory(atPath: mac.path(percentEncoded: false)) == [])
    #expect(newPrefixFile(manager, "steamuser/Saved Games/G/real.sav") == "Saved Games/G/real.sav")
    #expect(launcherLog(manager).contains("pfx.rosetta: 1 files, 3 registry keys, 2 not carried\n"))
}

/// A first preparation stopped after the rename (here: wineboot failed) is retried in place, and carries then.
private func failedFirstPreparation() throws -> (PrefixManager, [String: String]) {
    let boots = LockedBox(0)
    let (manager, env) = try makeCarryManager(winebootMakingUser(bootStatus: {
        boots.value += 1
        return boots.value == 1 ? 1 : 0
    }))
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav"])
    #expect(throws: PrefixError.winebootFailed(1)) { try manager.prepare(environment: env) }
    #expect(stamp(manager) == "wine.app preparing")
    #expect(!launcherLog(manager).contains("carried"))
    return (manager, env)
}

@Test func retryAfterAFailedWinebootCarries() throws {
    let (manager, env) = try failedFirstPreparation()
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == "AppData/Local/G/s.sav")
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"))
    #expect(FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "player-data-carried")
        .path(percentEncoded: false)))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func laterLaunchDoesNotCarryAgain() throws {
    let (manager, env) = try failedFirstPreparation()
    try manager.prepare(environment: env)
    // The player deletes the carried save; another preparation is stopped and retried.
    try FileManager.default.removeItem(at: manager.context.prefix.appending(path: "drive_c/users/steamuser/AppData/Local/G/s.sav"))
    try write("wine.app preparing", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(launcherLog(manager).components(separatedBy: "carried the player's data").count == 2)
}

@Test func upToDatePrefixNeverCarries() throws {
    let runner = winebootMakingUser()
    let (manager, env) = try makeCarryManager(runner)
    try write("old", to: manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"))
    try FileManager.default.createDirectory(at: manager.context.prefix.appending(path: "drive_c/users/steamuser"),
                                            withIntermediateDirectories: true)
    try write("wine.app id1 msync=1", to: manager.context.versionFile)
    try manager.prepare(environment: env)
    #expect(winebootCount(runner) == 0)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(!launcherLog(manager).contains("carried"))
}

@Test func winesLinkToTheMacsDocumentsIsNotCounted() throws {
    // Wine links the old user's Documents to the Mac's ~/Documents: that data is on the Mac already. (Fake folder.)
    let mac = try makeTempDir()
    try write("mac", to: mac.appending(path: "Documents/notes.txt"))
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav"])
    try FileManager.default.createSymbolicLink(at: manager.context.prefix.appending(path: "drive_c/users/steamuser/Documents"),
                                               withDestinationURL: mac.appending(path: "Documents"))
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/Documents/notes.txt") == nil)
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"))
}

@Test func eachRenameGetsItsOwnCarry() throws {
    // An earlier rename's marker doesn't stop the retry of a later rename whose preparation was stopped.
    let boots = LockedBox(0)
    let (manager, env) = try makeCarryManager(winebootMakingUser(bootStatus: {
        boots.value += 1
        return boots.value == 1 ? 1 : 0
    }))
    try write("", to: manager.context.dataPath.appending(path: "player-data-carried"))
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav"])
    #expect(throws: PrefixError.winebootFailed(1)) { try manager.prepare(environment: env) }
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == "AppData/Local/G/s.sav")
}

@Test func stoppedReprepareOfAnArm64PrefixCarriesNothing() throws {
    // Task FR (#2): a runtime update prepares an arm64 prefix in place; stopped, its retry finds the preparing stamp
    // and a pfx.rosetta an older launcher renamed without carrying (no marker). The player has been playing on the
    // arm64 prefix since: nothing comes back from the old one.
    let boots = LockedBox(0)
    let (manager, env) = try makeCarryManager(winebootMakingUser(bootStatus: {
        boots.value += 1
        return boots.value == 1 ? 1 : 0
    }))
    try write("old", to: manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"))
    try write(oldUserReg, to: manager.context.dataPath.appending(path: "pfx.rosetta/user.reg"))
    try FileManager.default.createDirectory(at: manager.context.prefix.appending(path: "drive_c/users/steamuser"),
                                            withIntermediateDirectories: true)
    try write("wine.app id0 msync=1", to: manager.context.versionFile)
    #expect(throws: PrefixError.winebootFailed(1)) { try manager.prepare(environment: env) }
    #expect(stamp(manager) == "wine.app preparing")
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(!launcherLog(manager).contains("carried"))
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func launchStoppedBetweenTheRenameAndTheStampStillCarries() throws {
    // Task FR (#6): the rename is recorded before the move, so a launch that ends right after the move, before the
    // preparing stamp (here: the stamp can't be written), still carries on the next launch.
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav"])
    try FileManager.default.createDirectory(at: manager.context.versionFile, withIntermediateDirectories: true)
    #expect(throws: (any Error).self) { try manager.prepare(environment: env) }
    #expect(FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "pfx.rosetta/user.reg")
        .path(percentEncoded: false)))
    try FileManager.default.removeItem(at: manager.context.versionFile)
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == "AppData/Local/G/s.sav")
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"))
}

@Test func aCopyCutShortLeavesOnlyItsTemporaryName() throws {
    // Task FR (#7): each file is copied under a temporary name and renamed into place, so a carry killed mid-copy (a
    // byte copy off APFS) leaves no part-file under the real name for the retry to keep. Its leftover (made here by
    // hand: a process can't be killed mid-copy in a test) is replaced by the retry's copy and gone after it.
    let (manager, env) = try failedFirstPreparation()
    let leftover = manager.context.prefix.appending(path: "drive_c/users/steamuser/AppData/Local/G/.macneutron-carry")
    try write("trunc", to: leftover)
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "steamuser/AppData/Local/G/s.sav") == "AppData/Local/G/s.sav")
    #expect(!FileManager.default.fileExists(atPath: leftover.path(percentEncoded: false)))
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"))

    // Task FR2: a folder (not ours to remove) at the temporary name stops that copy; the file never reaches its real
    // name by another way, and it is counted.
    let (blocked, blockedEnv) = try failedFirstPreparation()
    try write("game's", to: blocked.context.prefix.appending(path: "drive_c/users/steamuser/AppData/Local/G/.macneutron-carry/x"))
    try blocked.prepare(environment: blockedEnv)
    #expect(newPrefixFile(blocked, "steamuser/AppData/Local/G/s.sav") == nil)
    #expect(newPrefixFile(blocked, "steamuser/AppData/Local/G/.macneutron-carry/x") == "game's")
    #expect(launcherLog(blocked).contains("note: carried the player's data from pfx.rosetta: 0 files, 3 registry keys, 1 not carried\n"))
}

@Test func aPendingRecordThatCantBecomeTheMarkerIsRemoved() throws {
    // Task FR2: the pending record outliving its carry would carry that prefix again into a live one (an arm64
    // re-preparation stopped and retried); the marker is never read, so a failed rename unlinks the record.
    let (manager, env) = try makeCarryManager(winebootMakingUser())
    try rosettaPrefix(manager, files: ["AppData/Local/G/s.sav"])
    try write("", to: manager.context.dataPath.appending(path: "player-data-carried/x"))
    try manager.prepare(environment: env)
    #expect(launcherLog(manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"))
    #expect(!FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "player-data-pending")
        .path(percentEncoded: false)))
}

@Test func severalOldUsersAllGoToTheOneWineReads() throws {
    // Task FR (#8): wineboot makes one user (named after $USER); every old user's files go there. Task FR2: the old
    // user of that name (the one Wine read) wins a file others have, then the first by name.
    let (manager, env) = try makeCarryManager(winebootMakingUser("player"))
    // Not `steamuser`: beside another profile that one is Steam's own folder, handed back to the new prefix (I3).
    try rosettaPrefix(manager, user: "olduser", files: ["AppData/Local/G/both.sav", "AppData/Local/G/steam.sav"])
    try rosettaPrefix(manager, user: "macuser", files: ["AppData/Local/G/both.sav", "Saved Games/G/mac.sav"])
    try rosettaPrefix(manager, user: "player", files: ["AppData/Local/G/live.sav"])
    try rosettaPrefix(manager, user: "aaa", files: ["AppData/Local/G/live.sav"])
    try write("macuser's", to: manager.context.prefix.appending(path: "drive_c/users/macuser/AppData/Local/G/both.sav"))
    try write("player's", to: manager.context.prefix.appending(path: "drive_c/users/player/AppData/Local/G/live.sav"))
    try manager.prepare(environment: env)
    #expect(newPrefixFile(manager, "player/AppData/Local/G/live.sav") == "player's")
    #expect(newPrefixFile(manager, "player/AppData/Local/G/both.sav") == "macuser's")
    #expect(newPrefixFile(manager, "player/AppData/Local/G/steam.sav") == "AppData/Local/G/steam.sav")
    #expect(newPrefixFile(manager, "player/Saved Games/G/mac.sav") == "Saved Games/G/mac.sav")
    #expect(newPrefixFile(manager, "olduser/AppData/Local/G/steam.sav") == nil)
    #expect(newPrefixFile(manager, "macuser/Saved Games/G/mac.sav") == nil)
}

// MARK: Steam Cloud (spec §4.2, §4.3)

/// A launch by Steam for the Mac user `u`, whose Wine profile is `users/u`.
private func makeCloudManager() throws -> (PrefixManager, [String: String]) {
    let (manager, env) = try makeCarryManager(winebootMakingUser("u"))
    return (manager, env.merging(["USER": "u"]) { _, new in new })
}

@Test func aSeededPrefixIsPreparedInPlace() throws {
    // Steam's cloud download made pfx before MacNeutron ever ran: no stamp, no user.reg.
    let (manager, env) = try makeCloudManager()
    let seeded = manager.context.prefix.appending(path: "drive_c/users/steamuser/Local Settings/Application Data/G/s.sav")
    try write("cloud", to: seeded)
    try manager.prepare(environment: env)
    #expect(!FileManager.default.fileExists(atPath: manager.context.dataPath.appending(path: "pfx.rosetta").path(percentEncoded: false)))
    #expect(newPrefixFile(manager, "u/AppData/Local/G/s.sav") == "cloud")
    #expect(try String(contentsOf: seeded, encoding: .utf8) == "cloud")  // the path Steam watches, now through the links
    #expect(stamp(manager) == "wine.app id1 msync=1")
}

@Test func linksAreKeptOnEveryLaunch() throws {
    let (manager, env) = try makeCloudManager()
    try manager.prepare(environment: env)
    let steamuser = manager.context.prefix.appending(path: "drive_c/users/steamuser").path(percentEncoded: false)
    #expect(try FileManager.default.destinationOfSymbolicLink(atPath: steamuser) == "u")
    try FileManager.default.removeItem(atPath: steamuser)
    try manager.prepare(environment: env)  // up to date: no wineboot
    #expect(try FileManager.default.destinationOfSymbolicLink(atPath: steamuser) == "u")
}

@Test func aCloudConflictIsReportedAndLogged() throws {
    let (manager, env) = try makeCloudManager()
    try manager.prepare(environment: env)
    let users = manager.context.prefix.appending(path: "drive_c/users")
    try FileManager.default.removeItem(at: users.appending(path: "steamuser"))  // as if Steam had made a real folder
    try write("local", to: users.appending(path: "u/AppData/Local/G/s.sav"))
    try write("cloud", to: users.appending(path: "steamuser/Local Settings/Application Data/G/s.sav"))
    let report = try manager.prepare(environment: env)
    #expect(report.conflicts == 1)
    #expect(launcherLog(manager).contains("note: kept steamuser: 1 of its files differ"))
}

@Test func aRenamedRosettaPrefixLeavesSteamsCloudFilesWhereSteamWatches() throws {
    // A Rosetta-era prefix (profile `crossover`, user.reg, no wine.app stamp) that Steam's cloud download has since
    // written into: the rename must not take Steam's files out from under its exit pass (final review, I3).
    let (manager, env) = try makeCloudManager()
    try rosettaPrefix(manager, user: "crossover", files: ["AppData/Roaming/G/local.cfg"])
    let watched = manager.context.prefix.appending(path: "drive_c/users/steamuser/Local Settings/Application Data/G/s.sav")
    try write("cloud", to: watched)
    try manager.prepare(environment: env)
    #expect(try String(contentsOf: watched, encoding: .utf8) == "cloud")
    #expect(newPrefixFile(manager, "u/AppData/Local/G/s.sav") == "cloud")
    #expect(newPrefixFile(manager, "u/AppData/Roaming/G/local.cfg") == "AppData/Roaming/G/local.cfg")  // still carried
    let old = manager.context.dataPath.appending(path: "pfx.rosetta/drive_c/users")
    #expect(!FileManager.default.fileExists(atPath: old.appending(path: "steamuser").path(percentEncoded: false)))
    #expect(FileManager.default.fileExists(atPath: old.appending(path: "crossover/AppData/Roaming/G/local.cfg").path(percentEncoded: false)))
}
