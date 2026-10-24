import Foundation
import Testing
@testable import MacNeutronCore

/// A VM folder as eac-vm/host/build.sh lays it out: an executable macneutron-vm, run-guest.sh and run-game.sh.
private func makeVMFolder() throws -> URL {
    let root = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try write("#!/bin/sh\n", to: root.appending(path: "macneutron-vm"), executable: true)
    try write("#!/bin/sh\n", to: root.appending(path: "run-guest.sh"), executable: true)
    try write("# run-game.sh\nwine steam.exe \"$EXE\" \"$@\"\n", to: root.appending(path: "run-game.sh"))
    try write("dev\n", to: root.appending(path: "images/manifest.sha256"))
    return root
}

private struct VMFixture {
    let launcher: Launcher
    let runner: FakeRunner
    let notifier: RecordingNotifier
    let vm: URL
    let env: [String: String]
    /// The launcher's home (its Documents/ made) and the game's compatdata folder.
    let home: String
    let dataPath: URL
    let scripts: Scripts
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

private func makeVMFixture(vmInstalled: Bool = true, status: Int32 = 0, scripts: Scripts = Scripts(),
                           display: (width: Int, height: Int)? = nil, version: String? = nil,
                           console: String? = nil, mountsFailed: String? = nil) throws -> VMFixture {
    let vm = try makeVMFolder()
    let layout = try makeToolLayout()
    if let version { try setRuntimeVersion(version, of: layout) }
    let runner = FakeRunner { call in
        guard call.tool == "run-guest.sh" else { return 0 }
        scripts.add(call.arguments.first)
        if let console, let share = call.environment["MN_SHARE"] {  // macneutron-vm's guest console
            try? FileManager.default.createDirectory(atPath: share, withIntermediateDirectories: true)
            try? console.write(toFile: share + "/console.log", atomically: true, encoding: .utf8)
        }
        // mn-runner's word on a mount that failed, or run-game.sh's on a profile that isn't mounted (older images)
        if let mountsFailed, let share = call.environment["MN_SHARE"] {
            try? FileManager.default.createDirectory(atPath: share + "/traces", withIntermediateDirectories: true)
            try? mountsFailed.write(toFile: share + "/traces/mounts.failed", atomically: true, encoding: .utf8)
        }
        return status
    }
    let notifier = RecordingNotifier()
    let home = try makeTempDir().appending(path: "home", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: home.appending(path: "Documents"), withIntermediateDirectories: true)
    let launcher = Launcher(layout: layout, runner: runner,
                            log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")),
                            notifier: notifier, preflight: testPreflight,
                            settings: GameSettingsStore(directory: try makeTempDir().appending(path: "games")),
                            steam: try makeSteamLocation(),
                            microVMRoot: vmInstalled ? vm : try makeTempDir().appending(path: "no-vm"),
                            display: { display }, home: String(home.path(percentEncoded: false).dropLast()))
    let dataPath = try makeTempDir().appending(path: "compatdata/2437170", directoryHint: .isDirectory)
    let env = steamEnvironment(dataPath: dataPath, appID: "2437170")
    return VMFixture(launcher: launcher, runner: runner, notifier: notifier, vm: vm, env: env,
                     home: String(home.path(percentEncoded: false).dropLast()), dataPath: dataPath, scripts: scripts)
}

private let game = "/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Windows/Hemingway.exe"

@Test func everyGameRunsInTheMicroVM() throws {
    let f = try makeVMFixture(status: 3)
    let status = f.launcher.launch(["waitforexitandrun", game, "-homedir=SMITE2Alpha", "-d3d12"], environment: f.env)
    #expect(status == 3)
    let call = try #require(f.runner.calls.first)
    #expect(f.runner.calls.count == 1)  // no host prefix, no host Wine
    #expect(call.tool == "run-guest.sh")
    let profile = f.dataPath.appending(path: "pfx/drive_c/users/steamuser").path(percentEncoded: false)
    #expect(Array(call.arguments.dropFirst()) == ["--steam-appid", "2437170", "--ram-mib", "16384", "--steam-map",
                                                   "/Users/someone/Library/Application Support/Steam/steamapps/common=/mnt/games",
                                                   "--user-share", "profile=\(profile)",
                                                   "--user-share", "documents=\(f.home)/Documents",
                                                   "--user-share", "game=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2"])
    #expect(call.environment["MN_VM_DIR"] == f.vm.path(percentEncoded: false))
    #expect(call.environment["MN_STATE"] == f.vm.appending(path: "state/2437170.img").path(percentEncoded: false))
    #expect(call.environment["MN_SHARE"] == f.vm.appending(path: "runs/2437170").path(percentEncoded: false))
    // The toast macneutron-vm shows while macOS asks for the TPM key's password.
    #expect(call.environment["MN_TOAST"] == f.launcher.layout.launcherBinary.path(percentEncoded: false))
    #expect(f.launcherLog.contains("verb=waitforexitandrun appid=2437170 backend=vm"))
    #expect(f.launcherLog.contains("exit=3"))
}

// A game in the VM uses the saves Steam Cloud syncs (spec 2026-10-23-macneutron-vm-shared-saves): its Mac prefix's
// profile, ~/Documents and its own folder, shared read-write.

@Test func aVMGameGetsItsProfileDocumentsAndFolder() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let call = try #require(f.runner.calls.first)
    let profile = f.dataPath.appending(path: "pfx/drive_c/users/steamuser").path(percentEncoded: false)
    #expect(Array(call.arguments.dropFirst(7)) == [  // after the script, appid, RAM and --steam-map pairs
        "--user-share", "profile=\(profile)",
        "--user-share", "documents=\(f.home)/Documents",
        "--user-share", "game=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2"])
    #expect(try String(contentsOf: f.vm.appending(path: "runs/2437170/mounts"), encoding: .utf8) ==
            "profile /home/mn/pfx/drive_c/users/steamuser\ndocuments \(f.home)/Documents\ngame /mnt/games/SMITE 2\n")
    #expect(f.scripts.all.first?.contains("MN_MAC_HOME='\(f.home)'") == true)
    #expect(FileManager.default.fileExists(atPath: profile))  // made: Steam's and Proton's layout
}

/// The `profile=` share run-guest.sh was given.
private func profileShare(_ f: VMFixture) throws -> String? {
    let call = try #require(f.runner.calls.first)
    return call.arguments.first { $0.hasPrefix("profile=") }.map { String($0.dropFirst("profile=".count)) }
}

@Test func aLinkedProfileIsSharedAsItsTarget() throws {
    // macOS Wine's profile is named after the Mac user; users/steamuser links to it (ProfileLinks).
    let f = try makeVMFixture()
    let users = f.dataPath.appending(path: "pfx/drive_c/users")
    try FileManager.default.createDirectory(at: users.appending(path: "chad"), withIntermediateDirectories: true)
    try FileManager.default.createSymbolicLink(atPath: users.appending(path: "steamuser").path(percentEncoded: false),
                                               withDestinationPath: "chad")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(try profileShare(f) == users.appending(path: "chad").path(percentEncoded: false))
}

@Test func aDanglingProfileLinkGetsItsFolderBack() throws {
    let f = try makeVMFixture()
    let users = f.dataPath.appending(path: "pfx/drive_c/users")
    try FileManager.default.createDirectory(at: users, withIntermediateDirectories: true)
    let link = users.appending(path: "steamuser").path(percentEncoded: false)
    try FileManager.default.createSymbolicLink(atPath: link, withDestinationPath: "chad")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(try FileManager.default.destinationOfSymbolicLink(atPath: link) == "chad")  // the link stays
    var folder: ObjCBool = false
    #expect(FileManager.default.fileExists(atPath: users.appending(path: "chad").path(percentEncoded: false),
                                           isDirectory: &folder) && folder.boolValue)
    #expect(try profileShare(f) == users.appending(path: "chad").path(percentEncoded: false))
}

@Test func aGameFolderWithAnApostropheStaysOnePath() throws {
    let f = try makeVMFixture()
    let bg3 = "/Users/someone/Library/Application Support/Steam/steamapps/common/Baldur's Gate 3/bin/bg3.exe"
    _ = f.launcher.launch(["waitforexitandrun", bg3], environment: f.env)
    let call = try #require(f.runner.calls.first)
    #expect(call.arguments.contains("game=/Users/someone/Library/Application Support/Steam/steamapps/common/Baldur's Gate 3"))
    #expect(try String(contentsOf: f.vm.appending(path: "runs/2437170/mounts"), encoding: .utf8)
        .hasSuffix("game /mnt/games/Baldur's Gate 3\n"))
}

@Test func aLibraryOnAnotherDriveSharesThatLibrarysGame() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["waitforexitandrun", "/Volumes/Games/SteamLibrary/steamapps/common/X/X.exe"], environment: f.env)
    let call = try #require(f.runner.calls.first)
    #expect(call.arguments.contains("game=/Volumes/Games/SteamLibrary/steamapps/common/X"))
}

@Test func documentsThatCantBeListedRefuseTheLaunch() throws {
    // macOS's privacy settings refused Steam the Documents folder: never a run without it.
    let f = try makeVMFixture()
    let documents = f.home + "/Documents"
    try FileManager.default.setAttributes([.posixPermissions: 0], ofItemAtPath: documents)
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: documents) }
    let status = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(status != 0)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted.contains(UserShareError.documentsUnreadable(documents).description))
    #expect(UserShareError.documentsUnreadable("/D").description == "MacNeutron can't open your Documents folder (/D), so "
            + "a game in the VM couldn't reach its saves there. Allow Steam in System Settings > Privacy & Security > Files "
            + "and Folders, then launch the game again.")
}

@Test func aFailedMountIsToldToThePlayer() throws {
    let f = try makeVMFixture(status: 1, mountsFailed: "no virtiofs tag game\n")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let message = "MacNeutron couldn't share this game's saves with the game VM: no virtiofs tag game"
    #expect(f.notifier.posted.contains(message))
    #expect(f.launcherLog.contains(message))
}

@Test func anOldMountsFailedDoesNotRefuseTheNextRun() throws {
    let f = try makeVMFixture()
    try write("an earlier run's\n", to: f.vm.appending(path: "runs/2437170/traces/mounts.failed"))
    let status = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(status == 0)
    #expect(!f.notifier.posted.contains { $0.contains("couldn't share") })
}

/// A 32-bit PE in a Steam library (the VM's game folder).
private func i386GameInLibrary(named name: String = "Game.exe") throws -> String {
    let url = try makeTempDir().appending(path: "steamapps/common/Old Game/\(name)")
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(machine: PEImage.i386).write(to: url)
    return url.path(percentEncoded: false)
}

@Test func a32BitGameRunsInTheMicroVM() throws {
    let f = try makeVMFixture()
    #expect(f.launcher.launch(["waitforexitandrun", try i386GameInLibrary()], environment: f.env) == 0)
    #expect(f.runner.calls.map(\.tool) == ["run-guest.sh"])
    #expect(f.notifier.posted.isEmpty)
}

@Test func a32BitInstallerRunsInTheMicroVM() throws {
    // Proton runs a game's 32-bit redistributable installers; in the VM they run too.
    let f = try makeVMFixture()
    #expect(f.launcher.launch(["run", try i386GameInLibrary(named: "Setup.exe"), "/quiet"], environment: f.env) == 0)
    #expect(f.runner.calls.map(\.tool) == ["run-guest.sh"])
    #expect(!f.launcherLog.contains("skipped 32-bit installer"))
}

@Test func a32BitGameWaitsForTheVMLikeAnyOther() throws {
    // Before the app's setup finishes a 32-bit game gets the VM's message, not the macOS refusal.
    let f = try makeVMFixture(vmInstalled: false)
    #expect(f.launcher.launch(["waitforexitandrun", try i386GameInLibrary()], environment: f.env) == 1)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted.count == 1)
    #expect(f.notifier.posted.contains { $0.contains("game VM isn't ready") })
}

@Test func a32BitGameOptedOutOfTheVMIsRefused() throws {
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_VM"] = "0"
    #expect(f.launcher.launch(["waitforexitandrun", try i386GameInLibrary()], environment: env) == 1)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted
        == ["This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games in a Steam library run in its game VM."])
}

@Test func theGuestScriptCarriesTheLaunch() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    var env = f.env
    env["MACNEUTRON_DLSS"] = "1"
    env["MACNEUTRON_VM_D3D"] = "forward"
    env["MACNEUTRON_VM_D3D32"] = "forward"
    env["MACNEUTRON_VM_WOW64"] = "1"
    env["PROTON_USE_WOW64"] = "1"
    env["MTLD3D_CONFIG"] = "debug.capsAll=true"
    _ = f.launcher.launch(["waitforexitandrun", game, "-homedir=SMITE2Alpha", "it's quoted", ""], environment: env)
    let script = try #require(scripts.all.first)
    #expect(!FileManager.default.fileExists(atPath: try #require(f.runner.calls.first?.arguments.first)))
    #expect(script.contains("APPID='2437170'\n"))
    #expect(script.contains("GAME_DIR='/mnt/games/SMITE 2/Windows'\n"))
    #expect(script.contains(#"EXE='Z:\mnt\games\SMITE 2\Windows\Hemingway.exe'"# + "\n"))
    #expect(script.contains(#"set -- '-homedir=SMITE2Alpha' 'it'\''s quoted' ''"# + "\n"))
    #expect(script.contains("MACNEUTRON_DLSS='1'\n"))
    #expect(script.contains("MACNEUTRON_VM_D3D='forward'\n"))
    #expect(script.contains("MACNEUTRON_VM_D3D32='forward'\n"))
    #expect(script.contains("MACNEUTRON_VM_WOW64='1'\n"))
    #expect(script.contains("PROTON_USE_WOW64='1'\n"))
    #expect(script.contains("MTLD3D_CONFIG='debug.capsAll=true'\n"))
    // Exported, so Wine's processes get them (a forwarder trace, guest DXMT's NVIDIA extension or log level)
    let exported = try #require(script.split(separator: "\n").first { $0.hasPrefix("export ") })
    for key in ["MACNEUTRON_DLSS", "MACNEUTRON_VM_D3D", "MTLD3D_CONFIG"] {
        #expect(exported.split(separator: " ").contains(Substring(key)))
    }
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

@Test func aTPMThatDoesNotStartIsToldToThePlayer() throws {
    // macneutron-vm's 7, with no run.status: its TPM refused to start, so the guest never ran.
    let f = try makeVMFixture(status: 7)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.posted.contains { $0.contains("TPM") })
    // A game that itself ends with 7 leaves run.status: nothing to tell.
    let g = try makeVMFixture(status: 7)
    let share = g.vm.appending(path: "runs/2437170")
    try FileManager.default.createDirectory(at: share, withIntermediateDirectories: true)
    try write("7", to: share.appending(path: "run.status"))
    _ = g.launcher.launch(["waitforexitandrun", game], environment: g.env)
    #expect(!g.notifier.posted.contains { $0.contains("TPM") })
}

/// A game in a Steam library runs in the VM or not at all: never on macOS while the setup is still going.
private func expectRefusedUntilTheVMIsReady(_ f: VMFixture) {
    let status = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(status != 0)
    #expect(f.runner.calls.isEmpty)  // no guest, no host prefix, no host Wine
    #expect(f.notifier.posted.contains { $0.contains("game VM isn't ready") })
    #expect(f.launcherLog.contains("game VM isn't ready"))
}

@Test func aGameBeforeTheVMSetupFinishesIsRefused() throws {
    let f = try makeVMFixture()
    try FileManager.default.removeItem(at: f.vm.appending(path: "images/manifest.sha256"))
    expectRefusedUntilTheVMIsReady(f)
}

@Test func optingOutRunsOnMacOS() throws {
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_VM"] = "0"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.runner.calls.contains { $0.tool == "wine" && $0.arguments.first == game })
}

@Test func aGameBeforeTheVMToolsAreInstalledIsRefused() throws {
    expectRefusedUntilTheVMIsReady(try makeVMFixture(vmInstalled: false))
}

@Test func aGameWhileTheVMToolsSwapIsRefused() throws {
    let f = try makeVMFixture()
    try write("", to: f.vm.appending(path: ".installing"))
    expectRefusedUntilTheVMIsReady(f)
}

@Test func pathQueriesAndGamesOutsideALibraryStayOnMacOS() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["getcompatpath", game], environment: f.env)
    _ = f.launcher.launch(["waitforexitandrun", "/Applications/Other/Game.exe"], environment: f.env)
    #expect(!f.runner.calls.contains { $0.tool == "run-guest.sh" })
    #expect(f.launcherLog.contains("not in a Steam library"))
}

@Test func forwardingKeepsNativeDXMTsStateWithTheGame() throws {
    // Forwarded, native DXMT runs in macneutron-vm: its shader cache and recordings in the game's VM state folder, and
    // the game's launch options in its environment (M3 Task 8).
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_VM_D3D"] = "forward"
    env["DXMT_D3D12_SM6"] = "0"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let call = try #require(f.runner.calls.last)
    #expect(call.environment["MN_D3D_STATE"] == f.vm.appending(path: "state/2437170.dxmt").path(percentEncoded: false))
    #expect(call.environment["DXMT_D3D12_SM6"] == "0")
    var guest = f.env
    guest["MACNEUTRON_VM_D3D"] = "guest"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: guest)
    #expect(f.runner.calls.last?.environment["MN_D3D_STATE"] == nil)
}

// Forwarding is the VM's default since M5 passed (2026-10-19, the D3D forwarding spec §8): native DXMT on the Mac draws
// for every game; MACNEUTRON_VM_D3D=guest keeps DXMT in the guest.
@Test func vmGamesForwardDirect3DByDefault() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(scripts.all.last?.contains("\nMACNEUTRON_VM_D3D='forward'\n") == true)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_VM_D3D"] == "forward")
    #expect(f.runner.calls.last?.environment["MN_D3D_STATE"] != nil)
    var env = f.env
    env["MACNEUTRON_VM_D3D"] = "guest"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(scripts.all.last?.contains("\nMACNEUTRON_VM_D3D='guest'\n") == true)
    #expect(f.runner.calls.last?.environment["MN_D3D_STATE"] == nil)
}

@Test func theVMsPrecacheReplaysNativelyBeforeTheBoot() throws {
    let f = try makeVMFixture()
    let pipelines = f.vm.appending(path: "state/2437170.dxmt/pipelines")
    try write("recorded\n", to: pipelines.appending(path: "game.pipelines"))
    try write("an older build\n", to: pipelines.appending(path: "replayed"))
    var env = f.env
    env["MACNEUTRON_VM_D3D"] = "forward"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(f.runner.calls.map(\.tool) == ["dxmt-replay", "run-guest.sh"])
    let replay = try #require(f.runner.calls.first)
    #expect(replay.arguments == [pipelines.appending(path: "game.pipelines").path(percentEncoded: false)])
    #expect(replay.environment["DXMT_SHADER_CACHE_PATH"]
            == f.vm.appending(path: "state/2437170.dxmt").path(percentEncoded: false) + "/")
    #expect(replay.environment["DXMT_PIPELINE_RECORD"] == nil)
    let stamp = try String(contentsOf: pipelines.appending(path: "replayed"), encoding: .utf8)
    #expect(stamp != "an older build\n")
    #expect(f.notifier.toasts.contains { $0.contains("shaders") })
}

@Test func theVMsPrecacheIsStampedWithNativeDXMTsKey() throws {
    // The VM's recordings are native DXMT's: its translator key (dxmt-translator in the VM folder), not the
    // macOS runtime's, which a dev VM folder needn't match (M3's deferred minors).
    let f = try makeVMFixture()
    try write("native-key\n", to: f.vm.appending(path: "dxmt-translator"))
    let pipelines = f.vm.appending(path: "state/2437170.dxmt/pipelines")
    try write("recorded\n", to: pipelines.appending(path: "game.pipelines"))
    try write("an older build\n", to: pipelines.appending(path: "replayed"))
    var env = f.env
    env["MACNEUTRON_VM_D3D"] = "forward"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let stamp = try String(contentsOf: pipelines.appending(path: "replayed"), encoding: .utf8)
    #expect(stamp.hasPrefix("native-key "))
}

@Test func dlssTurnsOnNativeDXMTsNvidiaExtension() throws {
    // Forwarded, native DXMT in macneutron-vm reads its own environment: DLSS's NVIDIA extension too (M3 Task 6).
    let f = try makeVMFixture()
    var env = f.env
    env["MACNEUTRON_DLSS"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(f.runner.calls.last?.environment["DXMT_ENABLE_NVEXT"] == "1")
    let g = try makeVMFixture()
    _ = g.launcher.launch(["waitforexitandrun", game], environment: g.env)
    #expect(g.runner.calls.last?.environment["DXMT_ENABLE_NVEXT"] == nil)
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

@Test func theGuestsScreenIsTheMacsDisplay() throws {
    // Its pixels: the guest's X screen and the most a game is offered; the host window takes its shape.
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts, display: (3456, 2234))
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(scripts.all.last?.contains("\nMN_SCREEN='3456x2234'\n") == true)
    #expect(f.runner.calls.last?.environment["MN_SCREEN"] == "3456x2234")
    // A launch option's size wins; one that isn't WxH doesn't.
    var env = f.env
    env["MN_SCREEN"] = "2560x1600"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(scripts.all.last?.contains("\nMN_SCREEN='2560x1600'\n") == true)
    env["MN_SCREEN"] = "big; reboot"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(scripts.all.last?.contains("\nMN_SCREEN='3456x2234'\n") == true)
    #expect(f.runner.calls.last?.environment["MN_SCREEN"] == "3456x2234")
}

@Test func withoutADisplayTheGuestKeepsItsOwnScreen() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(scripts.all.last?.contains("MN_SCREEN") == false)  // run-game.sh's 1920x1080
}

@Test func perfTurnsOnTheGuestProfiler() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(scripts.all.last?.contains("MACNEUTRON_VM_PROFILE") == false)
    #expect(scripts.all.last?.contains("DXMT_STATS") == false)
    var env = f.env
    env["MACNEUTRON_PERF"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(scripts.all.last?.contains("\nMACNEUTRON_VM_PROFILE='1'\n") == true)
    // Frames are counted on the host: DXMT_STATS's timing would cost the guest a clock read under FEX per D3D12 call.
    #expect(scripts.all.last?.contains("DXMT_STATS") == false)
    #expect(f.runner.calls.last?.environment["MN_PRESENT_STATS"]
            == f.vm.appending(path: "runs/2437170/traces/frames.txt").path(percentEncoded: false))
}

/// run-guest.sh as a real process group, a shell and its child (as lockf, the guest's script and macneutron-vm are).
private struct GroupRunner: ProcessRunner {
    func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?) throws -> Int32 {
        try run(executable, arguments, environment: environment, output: output, started: { _ in })
    }
    func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?,
             started: @Sendable (pid_t) -> Void) throws -> Int32 {
        guard executable.lastPathComponent == "run-guest.sh" else { return 0 }
        return try SystemProcessRunner().run(URL(filePath: "/bin/sh"), ["-c", "sleep 60 & wait"], environment: [:],
                                             output: nil, started: started)
    }
}

final class Status: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Int32?
    var status: Int32? { lock.withLock { value } }
    func set(_ v: Int32) { lock.withLock { value = v } }
}

// Steam's Stop on macOS asks the processes it tracks to quit, the launcher among them (main.swift): a VM launch's
// VM ends, the whole run-guest.sh group, and the launch returns.
@Test func steamsStopEndsTheVM() throws {
    let launcher = Launcher(layout: try makeToolLayout(), runner: GroupRunner(),
                            log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")),
                            notifier: RecordingNotifier(), preflight: testPreflight,
                            settings: GameSettingsStore(directory: try makeTempDir().appending(path: "games")),
                            steam: try makeSteamLocation(), microVMRoot: try makeVMFolder(), display: { nil })
    let env = steamEnvironment(dataPath: try makeTempDir().appending(path: "compatdata/2437170"), appID: "2437170")
    #expect(!launcher.stopVM())  // nothing running yet
    let result = Status()
    Thread.detachNewThread { result.set(launcher.launch(["waitforexitandrun", game], environment: env)) }
    let start = Date()
    while !launcher.vmRun.running, Date().timeIntervalSince(start) < 10 { Thread.sleep(forTimeInterval: 0.05) }
    #expect(launcher.vmRun.running)
    #expect(launcher.stopVM())
    while result.status == nil, Date().timeIntervalSince(start) < 20 { Thread.sleep(forTimeInterval: 0.05) }
    #expect(result.status == 128 + SIGTERM)
}

@Test func aPerformanceRuntimeLaunchesQuietly() throws {
    // X.Y.Zp without MACNEUTRON_LOG: no guest console, DXMT logs nothing, one launcher line, no game log.
    let f = try makeVMFixture(version: "0.7.1p")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let call = try #require(f.runner.calls.first { $0.tool == "run-guest.sh" })
    #expect(call.arguments.suffix(2) == ["--console", "/dev/null"])
    #expect(call.environment["DXMT_LOG_LEVEL"] == "none")
    let lines = f.launcherLog.split(separator: "\n")
    #expect(lines.count == 1)
    #expect(lines.first?.contains(" verb=waitforexitandrun appid=2437170 backend=vm") == true)
    #expect(!FileManager.default.fileExists(atPath: f.launcher.log.directory.appending(path: "steam-2437170.log").path(percentEncoded: false)))
}

@Test func aPerformanceRuntimeLogsWhenAsked() throws {
    let f = try makeVMFixture(version: "0.7.1p")
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let call = try #require(f.runner.calls.first { $0.tool == "run-guest.sh" })
    #expect(!call.arguments.contains("--console"))
    #expect(call.environment["DXMT_LOG_LEVEL"] == nil)
    #expect(call.output == f.launcher.log.directory.appending(path: "steam-2437170.log"))  // the VM's output is the game log
}

@Test func aPerformanceScriptNamesItsProfile() throws {
    // The guest's run-game.sh keeps quiet for a p runtime; DXMT_STATS and the guest profiler aren't passed.
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts, version: "0.7.1p")
    var env = f.env
    env["DXMT_STATS"] = "1"
    env["MACNEUTRON_PERF"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let script = try #require(scripts.all.first)
    #expect(script.contains("MACNEUTRON_PROFILE='p'\n"))
    #expect(script.contains("DXMT_LOG_LEVEL='none'\n"))
    #expect(!script.contains("DXMT_STATS="))
    #expect(!script.contains("MACNEUTRON_VM_PROFILE="))
}

@Test func aNormalRuntimeKeepsItsConsole() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let call = try #require(f.runner.calls.first { $0.tool == "run-guest.sh" })
    #expect(!call.arguments.contains("--console"))
    #expect(!(scripts.all.first ?? "").contains("MACNEUTRON_PROFILE="))
}

@Test func aVMGameGetsWineDebugAsOnMacOS() throws {
    // LaunchEnvironment's WINEDEBUG: nothing unless logging, MACNEUTRON_LOG=1 and =2's channels, the user's own wins.
    for (log, user, want) in [(nil, nil, "-all"), ("1", nil, "warn+all,warn-seh,+loaddll,warn+steamclient"),
                              ("2", nil, "warn+all,+loaddll,+steamclient"), ("1", "+relay", "+relay")] {
        let scripts = Scripts()
        let f = try makeVMFixture(scripts: scripts, version: "0.7.1p")
        var env = f.env
        env["MACNEUTRON_LOG"] = log
        env["WINEDEBUG"] = user
        _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
        #expect((scripts.all.first ?? "").contains("WINEDEBUG='\(want)'\n"), "MACNEUTRON_LOG=\(log ?? "-")")
    }
}

@Test func aLoggedVMGamesLogEndsWithTheGuestConsole() throws {
    let f = try makeVMFixture(console: "[    0.1] Linux version\n")
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let log = try String(contentsOf: f.launcher.log.directory.appending(path: "steam-2437170.log"), encoding: .utf8)
    #expect(log.hasSuffix("--- the VM's console ---\n[    0.1] Linux version\n"))
}
