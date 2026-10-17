import Foundation
import Testing
@testable import MacNeutronCore

/// A VM folder as eac-vm/host/build.sh lays it out: an executable macneutron-vm, run-guest.sh and run-game.sh.
private func makeVMFolder() throws -> URL {
    let root = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try write("#!/bin/sh\n", to: root.appending(path: "macneutron-vm"), executable: true)
    try write("#!/bin/sh\n", to: root.appending(path: "run-guest.sh"), executable: true)
    try write("# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n", to: root.appending(path: "run-game.sh"))
    return root
}

private struct VMFixture {
    let launcher: Launcher
    let runner: FakeRunner
    let notifier: RecordingNotifier
    let vm: URL
    let env: [String: String]
    var launcherLog: String { (try? String(contentsOf: launcher.log.launcherLog, encoding: .utf8)) ?? "" }
}

/// The guest scripts run-guest.sh was given, read while it "runs" (the launcher removes them after).
final class Scripts: @unchecked Sendable {
    private let lock = NSLock()
    private var texts: [String] = []
    var all: [String] { lock.withLock { texts } }
    func add(_ path: String?) {
        if let path, let text = try? String(contentsOfFile: path, encoding: .utf8) { lock.withLock { texts.append(text) } }
    }
}

private func makeVMFixture(vmInstalled: Bool = true, status: Int32 = 0, scripts: Scripts = Scripts()) throws -> VMFixture {
    let vm = try makeVMFolder()
    let runner = FakeRunner { call in
        guard call.tool == "run-guest.sh" else { return 0 }
        scripts.add(call.arguments.first)
        return status
    }
    let notifier = RecordingNotifier()
    let launcher = Launcher(layout: try makeToolLayout(), runner: runner,
                            log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")),
                            notifier: notifier, preflight: testPreflight,
                            settings: GameSettingsStore(directory: try makeTempDir().appending(path: "games")),
                            steam: try makeSteamLocation(),
                            microVMRoot: vmInstalled ? vm : try makeTempDir().appending(path: "no-vm"))
    let env = steamEnvironment(dataPath: try makeTempDir().appending(path: "compatdata/2437170"), appID: "2437170")
    return VMFixture(launcher: launcher, runner: runner, notifier: notifier, vm: vm, env: env)
}

private let game = "/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway.exe"

@Test func everyGameRunsInTheMicroVM() throws {
    let f = try makeVMFixture(status: 3)
    let status = f.launcher.launch(["waitforexitandrun", game, "-homedir=SMITE2Alpha", "-d3d12"], environment: f.env)
    #expect(status == 3)
    let call = try #require(f.runner.calls.first)
    #expect(f.runner.calls.count == 1)  // no host prefix, no host Wine
    #expect(call.tool == "run-guest.sh")
    #expect(Array(call.arguments.dropFirst()) == ["--steam-appid", "2437170", "--ram-mib", "16384", "--steam-map",
                                                   "/Users/someone/Library/Application Support/Steam/steamapps/common=/mnt/games"])
    #expect(call.environment["MN_VM_DIR"] == f.vm.path(percentEncoded: false))
    #expect(call.environment["MN_STATE"] == f.vm.appending(path: "state/2437170.img").path(percentEncoded: false))
    #expect(call.environment["MN_SHARE"] == f.vm.appending(path: "runs/2437170").path(percentEncoded: false))
    #expect(f.launcherLog.contains("verb=waitforexitandrun appid=2437170 backend=vm"))
    #expect(f.launcherLog.contains("exit=3"))
}

@Test func theGuestScriptCarriesTheLaunch() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    var env = f.env
    env["MACNEUTRON_DLSS"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game, "-homedir=SMITE2Alpha", "it's quoted", ""], environment: env)
    let script = try #require(scripts.all.first)
    #expect(!FileManager.default.fileExists(atPath: try #require(f.runner.calls.first?.arguments.first)))
    #expect(script.contains("APPID='2437170'\n"))
    #expect(script.contains("GAME_DIR='/mnt/games/SMITE 2/Windows'\n"))
    #expect(script.contains(#"EXE='Z:\mnt\games\SMITE 2\Windows\Hemingway.exe'"# + "\n"))
    #expect(script.contains(#"set -- '-homedir=SMITE2Alpha' 'it'\''s quoted' ''"# + "\n"))
    #expect(script.contains("MACNEUTRON_DLSS='1'\n"))
    #expect(script.hasSuffix("# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n"))
}

@Test func aGamesFirstRunShowsAToastUntilTheGuestsPrefixIsReady() throws {
    let f = try makeVMFixture()
    let share = f.vm.appending(path: "runs/2437170/traces")
    try FileManager.default.createDirectory(at: share, withIntermediateDirectories: true)
    try write("", to: share.appending(path: "ready"))  // the last run's
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.toasts == ["Setting up SMITE 2 until " + share.appending(path: "ready").path(percentEncoded: false)])
    #expect(f.notifier.toastsClosed == 1)
    #expect(!FileManager.default.fileExists(atPath: share.appending(path: "ready").path(percentEncoded: false)))
    try write("", to: f.vm.appending(path: "state/2437170.img"))  // run-guest.sh made it
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.toasts.count == 1)
}

@Test func optingOutRunsOnMacOS() throws {
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_VM"] = "0"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.runner.calls.contains { $0.tool == "wine" && $0.arguments.first == game })
}

@Test func withoutAMicroVMGamesRunOnMacOS() throws {
    let f = try makeVMFixture(vmInstalled: false)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.runner.calls.contains { $0.tool == "wine" && $0.arguments.first == game })
}

@Test func pathQueriesAndGamesOutsideALibraryStayOnMacOS() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["getcompatpath", game], environment: f.env)
    _ = f.launcher.launch(["waitforexitandrun", "/Applications/Other/Game.exe"], environment: f.env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.launcherLog.contains("not in a Steam library"))
}

@Test func vmDirFromTheEnvironment() throws {
    let other = try makeVMFolder()
    let f = try makeVMFixture(vmInstalled: false)
    var env = f.env
    env["MACNEUTRON_VM_DIR"] = other.path(percentEncoded: false)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(f.runner.calls.first?.tool == "run-guest.sh")
    #expect(f.runner.calls.first?.environment["MN_VM_DIR"] == other.path(percentEncoded: false))
}
