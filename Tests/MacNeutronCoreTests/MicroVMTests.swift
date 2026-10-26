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
                           console: String? = nil, mountsFailed: String? = nil,
                           installLog: String? = nil, crash: String? = nil, stages: [String] = [],
                           steam: SteamLocation? = nil) throws -> VMFixture {
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
        // run-game.sh's word on the install-script steps it ran
        if let installLog, let share = call.environment["MN_SHARE"] {
            try? FileManager.default.createDirectory(atPath: share + "/traces", withIntermediateDirectories: true)
            try? installLog.write(toFile: share + "/traces/installscript.log", atomically: true, encoding: .utf8)
        }
        // run-game.sh's crash reporter: its marker, the reporter's ps, and the copy of the game's report
        if let crash, let share = call.environment["MN_SHARE"] {
            let details = share + "/traces/crash-report"
            try? FileManager.default.createDirectory(atPath: details + "/report/Crash", withIntermediateDirectories: true)
            try? crash.write(toFile: share + "/traces/crash", atomically: true, encoding: .utf8)
            try? "CrashReportClie 1234\n".write(toFile: details + "/ps.txt", atomically: true, encoding: .utf8)
            try? "report\n".write(toFile: details + "/report/Crash/context.txt", atomically: true, encoding: .utf8)
        }
        // run-game.sh's stages as it goes, each long enough for the launcher's poll to see it
        if let share = call.environment["MN_SHARE"] {
            for stage in stages {
                try? FileManager.default.createDirectory(atPath: share + "/traces", withIntermediateDirectories: true)
                try? stage.write(toFile: share + "/traces/stage", atomically: true, encoding: .utf8)
                Thread.sleep(forTimeInterval: 1)
            }
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
                            steam: try steam ?? makeSteamLocation(),
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

@Test func aCrashedVMGameKeepsItsReportAndSaysWhereItIs() throws {
    // run-game.sh ends a VM whose game left a crash reporter waiting on the invisible Xvfb. The launcher keeps the
    // report beside its logs, says where, and Steam sees a failure even if the guest's status came back 0.
    let marker = """
        when=2026-10-25T21:00:00Z
        reporter=CrashReportClie
        command=C:\\Game\\CrashReportClient.exe
        report=/mnt/games/SMITE 2/Saved/Crashes/ABC
        """
    let f = try makeVMFixture(status: 0, crash: marker)
    #expect(f.launcher.launch(["waitforexitandrun", game], environment: f.env) == 1)
    #expect(f.notifier.posted.count == 1)
    let message = try #require(f.notifier.posted.first)
    #expect(message.contains("SMITE 2 crashed"))
    #expect(message.contains("crash report is in"))
    let crashes = f.launcher.log.directory.appending(path: "crashes/2437170")
    let folders = try FileManager.default.contentsOfDirectory(atPath: crashes.path(percentEncoded: false))
    #expect(folders.count == 1)
    let kept = crashes.appending(path: try #require(folders.first))
    #expect(FileManager.default.fileExists(atPath: kept.appending(path: "report/ps.txt").path(percentEncoded: false)))
    #expect(message.contains(kept.path(percentEncoded: false)))
    #expect(f.launcherLog.contains("crash: appid=2437170 reporter=CrashReportClie"))
    #expect(f.launcherLog.contains("reports=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2/Saved/Crashes/ABC"))
}

@Test func anEarlierRunsCrashMarkerIsNotReportedAgain() throws {
    let f = try makeVMFixture()
    let traces = f.vm.appending(path: "runs/2437170/traces")
    try write("reporter=OldCrash\n", to: traces.appending(path: "crash"))
    #expect(f.launcher.launch(["waitforexitandrun", game], environment: f.env) == 0)
    #expect(f.notifier.posted.isEmpty)
    #expect(!FileManager.default.fileExists(atPath: traces.appending(path: "crash").path(percentEncoded: false)))
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

@Test func aVMOnlyProfileGetsSteamsXPNameLinks() throws {
    // Steam's Auto-Cloud roots are the profile's XP names (ProfileLinks), which only a macOS launch made before: a
    // game never run on macOS kept its saves where Steam doesn't look.
    let f = try makeVMFixture()
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let profile = f.dataPath.appending(path: "pfx/drive_c/users/steamuser").path(percentEncoded: false)
    let fm = FileManager.default
    #expect(try fm.destinationOfSymbolicLink(atPath: profile + "/Local Settings/Application Data") == "../AppData/Local")
    #expect(try fm.destinationOfSymbolicLink(atPath: profile + "/Application Data") == "AppData/Roaming")
    #expect(try fm.destinationOfSymbolicLink(atPath: profile + "/My Documents") == "Documents")
}

@Test func cloudFilesSteamPutInAnXPFolderReachTheGame() throws {
    // Steam's launch download makes real XP-name folders in a profile no macOS launch prepared.
    let f = try makeVMFixture()
    let profile = f.dataPath.appending(path: "pfx/drive_c/users/steamuser")
    try write("cloud\n", to: profile.appending(path: "Local Settings/Application Data/Game/save.sav"))
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(try String(contentsOf: profile.appending(path: "AppData/Local/Game/save.sav"), encoding: .utf8) == "cloud\n")
    #expect(try FileManager.default.destinationOfSymbolicLink(
        atPath: profile.appending(path: "Local Settings/Application Data").path(percentEncoded: false)) == "../AppData/Local")
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

@Test func everyLaunchShowsAToastUntilTheGamesWindowShows() throws {
    let f = try makeVMFixture()
    let traces = f.vm.appending(path: "runs/2437170/traces")
    try FileManager.default.createDirectory(at: traces, withIntermediateDirectories: true)
    let window = traces.appending(path: "window").path(percentEncoded: false)
    try write("", to: URL(filePath: window))  // the last run's
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.toasts == ["Setting up SMITE 2 until " + window])
    #expect(f.notifier.toastsClosed == 1)
    #expect(!FileManager.default.fileExists(atPath: window))
    let call = try #require(f.runner.calls.first { $0.tool == "run-guest.sh" })
    #expect(call.environment["MN_WINDOW_SHOWN"] == window)  // macneutron-vm's word
    #expect(call.environment["MN_APP_NAME"] == "SMITE 2")
    #expect(call.environment["MN_APP_ICON"] == nil)  // no such exe here
    try write("", to: f.vm.appending(path: "state/2437170.img"))  // run-guest.sh made it
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.toasts == ["Setting up SMITE 2 until " + window, "Starting SMITE 2 until " + window])
    #expect(f.notifier.toastsClosed == 2)
}

@Test func theGuestsStagesAreTheToastsMessage() throws {
    let f = try makeVMFixture(stages: ["Updating its Windows environment\n", "Loading the game\n"])
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.notifier.toastUpdates == ["Updating its Windows environment", "Loading the game"])
}

@Test func theGamesExeIconGoesToTheVMsWindow() throws {
    let f = try makeVMFixture()
    let exe = try makeTempDir().appending(path: "steamapps/common/Some Game/Game.exe")
    try FileManager.default.createDirectory(at: exe.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(icons: [(0, 32, [0x89, 0x50, 0x4E, 0x47])]).write(to: exe)
    _ = f.launcher.launch(["waitforexitandrun", exe.path(percentEncoded: false)], environment: f.env)
    let call = try #require(f.runner.calls.first { $0.tool == "run-guest.sh" })
    #expect(call.environment["MN_APP_NAME"] == "Some Game")
    let icon = try #require(call.environment["MN_APP_ICON"])
    #expect(try Data(contentsOf: URL(filePath: icon)) == PEImage.icon(of: exe))
}

@Test func theEACRuntimeIsMountedReadOnlyInTheGamesVM() throws {
    // Linux Steam passes the Proton EasyAntiCheat Runtime to every game under Proton; Mac Steam leaves it in a library,
    // or Steam's console downloads it (Mac Steam doesn't install the Linux-only tool).
    let steamRoot = try makeTempDir().appending(path: "Steam", directoryHint: .isDirectory)
    let places = ["steamapps/common/Proton EasyAntiCheat Runtime", "steamapps/content/app_1826330/depot_1826331",
                  "Steam.AppBundle/Steam/Contents/MacOS/steamapps/content/app_1826330/depot_1826331"]
    for place in places {
        let f = try makeVMFixture(steam: SteamLocation(root: steamRoot))
        let runtime = steamRoot.appending(path: place).path(percentEncoded: false)
        try FileManager.default.createDirectory(atPath: runtime, withIntermediateDirectories: true)  // no v2/: not one
        _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
        #expect(!(f.runner.calls.last?.arguments.contains("--ro-share") ?? true))
        #expect(!(f.scripts.all.last ?? "").contains("PROTON_EAC_RUNTIME"))
        try FileManager.default.createDirectory(atPath: runtime + "/v2", withIntermediateDirectories: true)
        _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
        let call = try #require(f.runner.calls.last { $0.tool == "run-guest.sh" })
        let at = try #require(call.arguments.firstIndex(of: "--ro-share"))
        #expect(call.arguments[at + 1] == "eac-runtime=" + runtime)
        let mounts = try String(contentsOf: f.vm.appending(path: "runs/2437170/mounts"), encoding: .utf8)
        #expect(mounts.hasSuffix("eac-runtime /Volumes/Proton EasyAntiCheat Runtime\n"))
        let script = f.scripts.all.last ?? ""
        #expect(script.contains("PROTON_EAC_RUNTIME='/Volumes/Proton EasyAntiCheat Runtime'\n"))
        let exported = script.split(separator: "\n").first { $0.hasPrefix("export ") } ?? ""
        #expect(exported.split(separator: " ").contains("PROTON_EAC_RUNTIME"))  // Wine's processes get it
        try FileManager.default.removeItem(atPath: runtime)
    }
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

@Test func aCrashReporterOverrideReachesTheGuest() throws {
    // A reporter with another name is a launch option, so the launcher's environment must reach run-game.sh.
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    var env = f.env
    env["MN_CRASH_REPORTERS"] = "mygamecrash"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let script = try #require(scripts.all.first)
    #expect(script.contains("MN_CRASH_REPORTERS='mygamecrash'"))
    #expect(script.contains("export ") && script.contains(" MN_CRASH_REPORTERS"))
}

@Test func aLoggedVMGamesLogEndsWithTheGuestConsole() throws {
    let f = try makeVMFixture(console: "[    0.1] Linux version\n")
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    let log = try String(contentsOf: f.launcher.log.directory.appending(path: "steam-2437170.log"), encoding: .utf8)
    #expect(log.hasSuffix("--- the VM's console ---\n[    0.1] Linux version\n"))
}

// Steam's install scripts (spec 2026-10-24-macneutron-install-scripts-design): the launcher reads them from the
// game's app manifest and puts them in front of run-game.sh, which runs them and says how each step went.

/// A game in a temp library (the fixture's app 2437170) whose app manifest lists `script`.
private func gameWithInstallScript(_ script: String) throws -> String {
    let steamapps = try makeTempDir().appending(path: "steamapps", directoryHint: .isDirectory)
    try write("\"AppState\"\n{\n\"installdir\" \"Game\"\n\"InstallScripts\"\n{\n\"2437171\" \"installscript.vdf\"\n}\n}\n",
              to: steamapps.appending(path: "appmanifest_2437170.acf"))
    try write(script, to: steamapps.appending(path: "common/Game/installscript.vdf"))
    return steamapps.appending(path: "common/Game/game.exe").path(percentEncoded: false)
}

@Test func aGamesInstallScriptsReachTheGuest() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts)
    let exe = try gameWithInstallScript(#"""
        "InstallScript"
        {
            "Registry" { "HKEY_CURRENT_USER\\Software\\Game" { "string" { "v" "1" } } }
            "Run Process" { "setup" { "HasRunKey" "HKEY_CURRENT_USER\\Software\\Game" "process 1" "%INSTALLDIR%\\setup.exe" } }
        }
        """#)
    let registry = f.vm.appending(path: "runs/2437170/installscript.reg")
    _ = f.launcher.launch(["waitforexitandrun", exe], environment: f.env)
    // The Registry entries go as a UTF-16 file in the run share (run-game.sh imports it when it changed).
    let data = try Data(contentsOf: registry)
    #expect(data.prefix(2) == Data([0xFF, 0xFE]))
    #expect(String(data: data.dropFirst(2), encoding: .utf16LittleEndian)?.contains(#""v"="1""#) == true)
    _ = f.launcher.launch(["run", exe], environment: f.env)
    #expect(scripts.all.count == 2)
    let game = try #require(scripts.all.first), run = try #require(scripts.all.last)
    let body = try #require(game.range(of: "# run-game.sh"))
    #expect(game.range(of: "mn_install_script() {\n").map { $0.lowerBound < body.lowerBound } == true)
    #expect(game.contains(#"  mn_run 'HKEY_CURRENT_USER\Software\Game' 'setup' '1' 'user.reg' 'Software\\Game' '"setup"' '/mnt/games/Game' 'Z:\mnt\games\Game\setup.exe'"#
                          + "\n"))
    // Mac Steam never sends an install script's `run`: Proton's generic verb gets none, and no earlier registry file.
    #expect(!run.contains("mn_install_script"))
    #expect(!FileManager.default.fileExists(atPath: registry.path(percentEncoded: false)))
}

@Test func installScriptResultsReachLauncherLog() throws {
    let f = try makeVMFixture(installLog: "x86 Update 4 exit 0\nFmSysInfo exit 1\n")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(f.launcherLog.contains("install script 2437170: x86 Update 4 exit 0\n"))
    #expect(f.launcherLog.contains("install script 2437170: FmSysInfo exit 1\n"))
}

@Test func aPerformanceRuntimeLogsOnlyTheInstallScriptsThatFailed() throws {
    let f = try makeVMFixture(version: "0.7.1p", installLog: "x86 Update 4 exit 0\nFmSysInfo exit 1\n")
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(!f.launcherLog.contains("x86 Update 4"))
    #expect(f.launcherLog.contains("install script 2437170: FmSysInfo exit 1\n"))
}

@Test func anEarlierRunsInstallScriptResultsAreNotReported() throws {
    let f = try makeVMFixture()
    try write("FmSysInfo exit 1\n", to: f.vm.appending(path: "runs/2437170/traces/installscript.log"))
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(!f.launcherLog.contains("install script"))
}

@Test func installScriptNotesReachLauncherLog() throws {
    let f = try makeVMFixture()
    let exe = try gameWithInstallScript(#""InstallScript" { "Firewall" { "Game" "%INSTALLDIR%\\game.exe" } }"#)
    _ = f.launcher.launch(["waitforexitandrun", exe], environment: f.env)
    #expect(f.launcherLog.split(separator: "\n").contains { $0.contains("install script 2437170: ") && $0.contains("Firewall") })
}

// Where a game ran from, for the app's cleanup of uninstalled games' drives (UninstalledDrives): a drive whose library
// Steam's list no longer names is kept.
@Test func aVMRunRecordsItsLibrary() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    #expect(try String(contentsOf: f.vm.appending(path: "runs/2437170/library"), encoding: .utf8)
            == "/Users/someone/Library/Application Support/Steam/steamapps")
}

// An installer that waited for a click nobody could give costs the timeout at every launch: the player hears of it,
// and a launch option sets the timeout.
@Test func aTimedOutInstallScriptIsToldToThePlayer() throws {
    let scripts = Scripts()
    let f = try makeVMFixture(scripts: scripts, installLog: "x86 Update 4 exit 0\nUE4 Prerequisites exit 124\n")
    var env = f.env
    env["MN_INSTALL_TIMEOUT"] = "120"
    _ = f.launcher.launch(["waitforexitandrun", game], environment: env)
    #expect(f.notifier.posted == ["SMITE 2's install step \"UE4 Prerequisites\" was stopped after waiting too long, maybe "
                                  + "for a click in a window the game VM can't show. It runs again at the next launch."])
    #expect(scripts.all.first?.contains("MN_INSTALL_TIMEOUT='120'\n") == true)
}
