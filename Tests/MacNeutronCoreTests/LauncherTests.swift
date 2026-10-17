import Foundation
import Testing
@testable import MacNeutronCore

final class RecordingNotifier: Notifier, @unchecked Sendable {
    private let lock = NSLock()
    private var messages: [String] = []
    var posted: [String] { lock.withLock { messages } }
    func post(title: String, message: String) { lock.withLock { messages.append(message) } }
}

private struct Fixture {
    let launcher: Launcher
    let runner: FakeRunner
    let notifier: RecordingNotifier
    let env: [String: String]
    var launcherLog: String { (try? String(contentsOf: launcher.log.launcherLog, encoding: .utf8)) ?? "" }
}

private func makeFixture(runner: FakeRunner = winebootCreatingPrefix(), identity: String? = testIdentity,
                         bridge: Bool = false) throws -> Fixture {
    let notifier = RecordingNotifier()
    let layout = try makeToolLayout()
    if bridge { try installFakeSteamBridge(in: layout) }
    let steam = try makeSteamLocation()
    let launcher = Launcher(layout: layout, runner: runner,
                            log: LauncherLog(directory: try makeTempDir().appending(path: "Logs")),
                            notifier: notifier, preflight: Preflight(systemSupported: { true }, identity: { _ in identity }),
                            settings: GameSettingsStore(directory: try makeTempDir().appending(path: "games")),
                            steam: steam, microVMRoot: try makeTempDir().appending(path: "no-vm"))
    let env = steamEnvironment(dataPath: try makeTempDir().appending(path: "compatdata/42"), appID: "42")
    return Fixture(launcher: launcher, runner: runner, notifier: notifier, env: env)
}

@Test func waitForExitAndRunPreparesWaitsRunsThenWaits() throws {
    // Proton's order: preparing first means a launch that queued on the prefix lock behind
    // `run iscriptevaluator.exe` finds that session's wineserver alive and waits it out.
    let runner = FakeRunner { call in
        if call.arguments.first == "wineboot", let prefix = call.environment["WINEPREFIX"] {
            try? FileManager.default.createDirectory(atPath: prefix, withIntermediateDirectories: true)
        }
        return call.arguments.first == "/g/Game.exe" ? 7 : 0
    }
    let f = try makeFixture(runner: runner)
    let status = f.launcher.launch(["waitforexitandrun", "/g/Game.exe", "-windowed"], environment: f.env)
    #expect(status == 7)
    #expect(runner.calls.map { [$0.tool] + $0.arguments } == [
        ["wine", "wineboot", "-u"],
        ["wine", "reg", "add", #"HKLM\Software\Microsoft\Wow64\amd64"#, "/ve", "/d", "libarm64ecfex.dll", "/f"],
        ["wine", "reg", "add", #"HKCU\Software\Wine\WineDbg"#, "/v", "ShowCrashDialog", "/t", "REG_DWORD", "/d", "0", "/f"],
        ["wine", "reg", "add", #"HKCU\Software\Wine\X11 Driver"#, "/v", "EmulateModeset", "/d", "Y", "/f"],
        ["wineserver", "-w"],
        ["wineserver", "-w"],
        ["wine", "/g/Game.exe", "-windowed"],
        ["wineserver", "-w"],
    ])
}

@Test func runDoesNotWaitForWineserver() throws {
    let f = try makeFixture()
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)  // prepares the prefix (its own `-w` included)
    let prepared = f.runner.calls.count
    #expect(f.launcher.launch(["run", "/g/iscriptevaluator.exe", "--get-current-step", "42"], environment: f.env) == 0)
    #expect(f.runner.calls.dropFirst(prepared).map(\.arguments) == [["/g/iscriptevaluator.exe", "--get-current-step", "42"]])
}

@Test func runInPrefixSkipsPreparation() throws {
    let f = try makeFixture()
    #expect(f.launcher.launch(["runinprefix", "/g/tool.exe"], environment: f.env) == 0)
    #expect(f.runner.calls.map(\.arguments) == [["/g/tool.exe"]])
}

@Test func getCompatPathConvertsThroughWinepath() throws {
    let f = try makeFixture()
    #expect(f.launcher.launch(["getcompatpath", "/g/save"], environment: f.env) == 0)
    #expect(f.runner.calls.last?.arguments == ["winepath.exe", "-w", "/g/save"])
}

@Test func gameArgumentsPassThroughUnchanged() throws {
    let f = try makeFixture()
    let args = ["/Steam Library/My Game/Game.exe", "--name=\"Player One\"", "a b", "ünïcode", ""]
    _ = f.launcher.launch(["run"] + args, environment: f.env)
    #expect(f.runner.calls.last?.arguments == args)
}

@Test func unknownVerbFailsWithoutRunningAnything() throws {
    let f = try makeFixture()
    #expect(f.launcher.launch(["destroyprefix", "/g/Game.exe"], environment: f.env) == 1)
    #expect(f.runner.calls.isEmpty)
}

@Test func launchingOutsideSteamFailsCleanly() throws {
    let f = try makeFixture()
    #expect(f.launcher.launch(["run", "/g/Game.exe"], environment: ["PATH": "/usr/bin"]) == 1)
    #expect(f.runner.calls.isEmpty)
}

@Test func invalidGraphicsSettingStillLaunchesWithDefault() throws {
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_GRAPHICS"] = "vulkan"
    #expect(f.launcher.launch(["run", "/g/Game.exe"], environment: env) == 0)
    #expect(f.runner.calls.last?.environment["WINEDLLOVERRIDES"]?.hasPrefix("dxgi=n,b;d3d10core=n,b;d3d11=n,b") == true)
    let logged = try String(contentsOf: f.launcher.log.launcherLog, encoding: .utf8)
    #expect(logged.contains("backend=dxmt"))
    #expect(logged.contains("note=unknown MACNEUTRON_GRAPHICS 'vulkan'"))
}

@Test func missingRuntimeNotifiesAndFails() throws {
    let f = try makeFixture(identity: nil)
    #expect(f.launcher.launch(["run", "/g/Game.exe"], environment: f.env) == 1)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted == ["MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it."])
    #expect(FileManager.default.fileExists(atPath: f.launcher.layout.runtimeDamagedMarker.path(percentEncoded: false)))
}

private let thirtyTwoBitText = "This game is 32-bit. MacNeutron 0.1 runs 64-bit games only; 32-bit support is planned."

private func i386Exe(named name: String) throws -> String {
    let url = try makeTempDir().appending(path: name)
    try peBytes(machine: PEImage.i386).write(to: url)
    return url.path(percentEncoded: false)
}

@Test func thirtyTwoBitGameIsRefusedWithItsMessage() throws {
    let f = try makeFixture()
    #expect(f.launcher.launch(["waitforexitandrun", try i386Exe(named: "Game.exe")], environment: f.env) == 1)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted == [thirtyTwoBitText])
    #expect(f.launcherLog.contains("error: \(thirtyTwoBitText)"))
}

@Test func runSkipsA32BitInstaller() throws {
    // Steam's install scripts start redistributable installers with `run`; a 32-bit one can't run, and isn't the game.
    let f = try makeFixture()
    #expect(f.launcher.launch(["run", try i386Exe(named: "Setup.exe"), "/quiet"], environment: f.env) == 0)
    #expect(f.runner.calls.isEmpty)
    #expect(f.notifier.posted.isEmpty)
    #expect(f.launcherLog.contains("skipped 32-bit installer Setup.exe"))
}

@Test func failureMessagesReachTheGameLog() throws {
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    #expect(f.launcher.launch(["waitforexitandrun", try i386Exe(named: "Game.exe")], environment: env) == 1)
    #expect(try String(contentsOf: f.launcher.log.gameLog(appID: "42"), encoding: .utf8).contains(thirtyTwoBitText))
}

@Test func failedPrefixSetupNotifiesAndSkipsGame() throws {
    let f = try makeFixture(runner: winebootCreatingPrefix(status: 5))
    #expect(f.launcher.launch(["waitforexitandrun", "/g/Game.exe"], environment: f.env) == 1)
    #expect(!f.runner.calls.contains { $0.arguments.first == "/g/Game.exe" })
    #expect(f.notifier.posted.count == 1)
}

@Test func macneutronLogSendsGameOutputToPerGameLog() throws {
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let gameLog = f.launcher.log.gameLog(appID: "42")
    #expect(f.runner.calls.last?.output == gameLog)
    #expect(try String(contentsOf: gameLog, encoding: .utf8).contains("WINEDEBUG=warn+all,warn-seh,+loaddll,warn+steamclient\n"))
}

@Test func macneutronLog2KeepsEveryWarningAndTheSteamTrace() throws {
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_LOG"] = "2"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let gameLog = f.launcher.log.gameLog(appID: "42")
    #expect(f.runner.calls.last?.output == gameLog)
    #expect(f.runner.calls.last?.environment["WINEDEBUG"] == "warn+all,+loaddll,+steamclient")
}

@Test func anisotropicFilteringReachesTheGame() throws {
    let f = try makeFixture()
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    #expect(f.runner.calls.last?.environment["DXMT_MAX_ANISOTROPY"] == "16")  // no settings file: the default
    try f.launcher.settings.save(GameSettings(anisotropy: "game"), for: "42")
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    #expect(f.runner.calls.last?.environment["DXMT_MAX_ANISOTROPY"] == nil)
}

@Test func everyLaunchIsLoggedWithVersions() throws {
    let f = try makeFixture()
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    let line = try String(contentsOf: f.launcher.log.launcherLog, encoding: .utf8)
    #expect(line.hasSuffix("verb=run appid=42 backend=dxmt runtime=test (0123456789ab) exit=0\n"))
}

@Test func terminateKillsThePrefixWineserverUnderTheGamesMsync() throws {
    // The server runs with the game's WINEMSYNC; a client in the other mode can't talk to it.
    let f = try makeFixture()
    f.launcher.terminate(environment: f.env)
    #expect(f.runner.calls.map { [$0.tool] + $0.arguments } == [["wineserver", "-k"]])
    #expect(f.runner.calls[0].environment["WINEPREFIX"]?.hasSuffix("compatdata/42/pfx/") == true)
    #expect(f.runner.calls[0].environment["WINEMSYNC"] == "1")
    try f.launcher.settings.save(GameSettings(msync: false), for: "42")
    f.launcher.terminate(environment: f.env)
    #expect(f.runner.calls.count == 2)
    #expect(f.runner.calls[1].environment["WINEMSYNC"] == nil)
}

@Test func gameSettingsApplyUnderneathLaunchOptions() throws {
    let f = try makeFixture()
    try f.launcher.settings.save(GameSettings(graphics: "wined3d", log: true, msync: false), for: "42")
    var env = f.env
    env["MACNEUTRON_GRAPHICS"] = "dxmt"  // typed into Steam's launch options: wins
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let wine = try #require(f.runner.calls.last?.environment)
    #expect(wine["WINEDLLOVERRIDES"]?.hasPrefix("dxgi=n,b;d3d10core=n,b;d3d11=n,b") == true)  // dxmt
    #expect(wine["WINEDEBUG"] == "warn+all,warn-seh,+loaddll,warn+steamclient")
    #expect(wine["WINEMSYNC"] == nil)
}

@Test func oldGraphicsValueRunsDXMTWithANote() throws {
    let f = try makeFixture()
    try f.launcher.settings.save(GameSettings(graphics: "dxvk"), for: "42")
    #expect(f.launcher.launch(["run", "/g/Game.exe"], environment: f.env) == 0)
    #expect(f.runner.calls.last?.environment["WINEDLLOVERRIDES"]?.hasPrefix("dxgi=n,b;d3d10core=n,b;d3d11=n,b") == true)
    let line = try #require(f.launcherLog.split(separator: "\n").last { $0.contains(" verb=run ") })
    #expect(line.contains("backend=dxmt"))
    #expect(line.contains("'dxvk' was removed in 0.1, using dxmt"))
}

@Test func unreadableGameSettingsAreIgnored() throws {
    let f = try makeFixture()
    try write("{ not json", to: f.launcher.settings.directory.appending(path: "42.json"))
    #expect(f.launcher.launch(["run", "/g/Game.exe"], environment: f.env) == 0)
    #expect(try String(contentsOf: f.launcher.log.launcherLog, encoding: .utf8).contains("ignoring unreadable game settings for 42"))
    #expect(f.runner.calls.last?.environment["DXMT_MAX_ANISOTROPY"] == "16")  // the defaults still apply
}

@Test func gameGoesThroughSteamExeWhenTheBridgeIsInstalled() throws {
    let f = try makeFixture(bridge: true)
    #expect(f.launcher.launch(["waitforexitandrun", "/Steam Library/My Game/Game.exe", "-windowed"], environment: f.env) == 0)
    let game = try #require(f.runner.calls.first { $0.arguments.first == SteamBridge.steamExe })
    // The game's macOS path, raw: steam.exe converts it (Proton's run_process), to S: in a Steam library.
    #expect(game.arguments == [#"C:\windows\system32\steam.exe"#, "/Steam Library/My Game/Game.exe", "-windowed"])
    #expect(game.environment["STEAM_COMPAT_CLIENT_INSTALL_PATH"]
        == String(f.launcher.steam.bundleMacOS.path(percentEncoded: false).dropLast()))
    #expect(game.environment["MACNEUTRON_STEAM_ACCOUNT"] == nil)  // Proton writes no ActiveUser (parity L2)
    let prefix = try CompatContext(environment: f.env).prefix
    #expect(FileManager.default.fileExists(
        atPath: prefix.appending(path: "drive_c/Program Files (x86)/Steam/steamclient64.dll").path(percentEncoded: false)))
}

@Test func runInPrefixNeverGoesThroughSteamExe() throws {
    let f = try makeFixture(bridge: true)
    _ = f.launcher.launch(["runinprefix", "/g/tool.exe"], environment: f.env)
    #expect(f.runner.calls.map(\.arguments) == [["/g/tool.exe"]])
}

@Test func escapeHatchStartsTheGameDirectly() throws {
    let f = try makeFixture(bridge: true)
    var env = f.env
    env["MACNEUTRON_NO_STEAM_BRIDGE"] = "1"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    #expect(f.runner.calls.last?.arguments == ["/g/Game.exe"])
    #expect(f.launcherLog.contains("note: Steam bridge disabled by launch option"))
}

@Test func missingBridgeStartsTheGameDirectly() throws {
    let f = try makeFixture()
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    #expect(f.runner.calls.last?.arguments == ["/g/Game.exe"])
    #expect(f.launcherLog.contains("note: Steam bridge not installed"))
}

@Test func steamsLanguageReachesSteamExe() throws {
    let f = try makeFixture(bridge: true)
    try write(registryVDF(language: "french"), to: f.launcher.steam.registryVDF)
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_STEAM_LANGUAGE"] == "french")
    var env = f.env
    env["MACNEUTRON_STEAM_LANGUAGE"] = "latam"  // a launch option wins
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_STEAM_LANGUAGE"] == "latam")
}

@Test func steamsClientPathIsLoggedAndCheckedForTheLibrary() throws {
    let f = try makeFixture(bridge: true)
    try FileManager.default.removeItem(at: f.launcher.steam.bundleMacOS.appending(path: "steamclient.dylib"))
    var env = f.env
    env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = "/nowhere"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    #expect(f.launcherLog.contains("(Steam passed /nowhere)"))
    #expect(f.launcherLog.contains("note: steamclient.dylib not found in"))
}

@Test func threeGamesRunSteamExeFromTheSteamFolder() throws {
    // Proton runs C:\windows\system32\steam.exe, except for three games it runs from the Steam folder (proton:2109-2115),
    // keyed on SteamGameId.
    let expected = ["311210": #"C:\Program Files (x86)\Steam\steam.exe"#, "1985810": #"C:\Program Files (x86)\Steam\steam.exe"#,
                    "1549250": #"C:\Program Files (x86)\Steam\steam.exe"#, "480": #"C:\windows\system32\steam.exe"#]
    for (id, path) in expected {
        let f = try makeFixture(bridge: true)
        var env = f.env
        env["SteamGameId"] = id
        _ = f.launcher.launch(["waitforexitandrun", "/g/Game.exe"], environment: env)
        let game = f.runner.calls.first { $0.arguments.first?.hasSuffix(#"\steam.exe"#) == true }
        #expect(game?.arguments.first == path, "SteamGameId \(id)")
    }
}

@Test func escapeHatchKeepsSteamExeForSteamLinks() throws {
    // The game's steam:// links go through HKCR\steam to the Steam folder's steam.exe: without the bridge they still
    // reach Mac Steam (parity plan R10).
    let f = try makeFixture(bridge: true)
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    var env = f.env
    env["MACNEUTRON_NO_STEAM_BRIDGE"] = "1"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let prefix = try CompatContext(environment: f.env).prefix
    for path in ["drive_c/Program Files (x86)/Steam/steam.exe", "drive_c/windows/system32/steam.exe"] {
        #expect(FileManager.default.fileExists(atPath: prefix.appending(path: path).path(percentEncoded: false)), "\(path)")
    }
}

@Test func escapeHatchTakesTheBridgeOutOfThePrefix() throws {
    // Seen in acceptance: a game's steam_api loads the client DLL an earlier launch left (its registry
    // values persist), and without Steam's client path the bridge aborts the game. Without the files,
    // the game just finds no Steam.
    let f = try makeFixture(bridge: true)
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    let folder = try CompatContext(environment: f.env).prefix.appending(path: "drive_c/Program Files (x86)/Steam")
    #expect(FileManager.default.fileExists(atPath: folder.appending(path: "steamclient64.dll").path(percentEncoded: false)))
    var env = f.env
    env["MACNEUTRON_NO_STEAM_BRIDGE"] = "1"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    for name in ["steamclient64.dll", "steamclient.dll"] {
        #expect(!FileManager.default.fileExists(atPath: folder.appending(path: name).path(percentEncoded: false)))
    }
}

@Test func anyDirectStartTakesLeftoverBridgeFilesOut() throws {
    // Same abort as with the escape hatch when the runtime or tool folder loses a bridge file.
    let f = try makeFixture(bridge: true)
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    try FileManager.default.removeItem(at: f.launcher.layout.steamHelper)
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    let folder = try CompatContext(environment: f.env).prefix.appending(path: "drive_c/Program Files (x86)/Steam")
    #expect(!FileManager.default.fileExists(atPath: folder.appending(path: "steamclient64.dll").path(percentEncoded: false)))
}

@Test func gameLogsHideTheSteamLoginName() throws {
    // People post game logs in bug reports, and Steam passes the account's login name as SteamUser and SteamAppUser.
    // MacNeutron adds no account variable of its own. (Fake values.)
    let f = try makeFixture(bridge: true)
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    env["SteamUser"] = "fakelogin"
    env["SteamAppUser"] = "fakeapplogin"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let log = try String(contentsOf: f.launcher.log.gameLog(appID: "42"), encoding: .utf8)
    #expect(log.contains("SteamUser=<redacted>\n"))
    #expect(log.contains("SteamAppUser=<redacted>\n"))
    #expect(!log.contains("fakelogin") && !log.contains("fakeapplogin"))
    let game = f.runner.calls.last?.environment
    #expect(game?["MACNEUTRON_STEAM_ACCOUNT"] == nil)
    #expect(game?["SteamUser"] == "fakelogin" && game?["SteamAppUser"] == "fakeapplogin")  // only the header hides them
}

@Test func gameLogsHideSecretLookingVariables() throws {
    // Steam started from a terminal passes that shell's environment on, API tokens included. (Fake values.)
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_LOG"] = "1"
    let secrets = ["GITHUB_TOKEN": "fake-gh", "SOME_API_KEY": "fake-api", "AWS_SECRET_ACCESS_KEY": "fake-aws",
                   "DB_PASSWORD": "fake-pw", "SSH_AUTH_SOCK": "/tmp/fake-sock", "Session_Token": "fake-lower"]
    env.merge(secrets) { $1 }
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    let log = try String(contentsOf: f.launcher.log.gameLog(appID: "42"), encoding: .utf8)
    for (key, value) in secrets {
        #expect(log.contains("\(key)=<redacted>\n"), "\(key)")
        #expect(!log.contains(value), "\(key)")
    }
    #expect(log.contains("MACNEUTRON_LOG=1\n"))  // ordinary variables stay readable
    #expect(f.runner.calls.last?.environment["GITHUB_TOKEN"] == "fake-gh")  // only the header hides them
}

@Test func presenterIsAskedForByDefault() throws {
    let f = try makeFixture(bridge: true)
    _ = f.launcher.launch(["waitforexitandrun", "/g/Game.exe"], environment: f.env)
    let game = try #require(f.runner.calls.first { $0.arguments.first == SteamBridge.steamExe })
    #expect(game.environment["MACNEUTRON_PRESENT"] == "1")
    #expect(f.runner.calls.allSatisfy { $0.environment["DYLD_INSERT_LIBRARIES"] == nil })
}

@Test func optingOutLeavesThePresenterOff() throws {
    let f = try makeFixture()
    var env = f.env
    env["MACNEUTRON_NO_METALFX"] = "1"
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: env)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_PRESENT"] == nil)
    try f.launcher.settings.save(GameSettings(metalFX: false), for: "42")
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_PRESENT"] == nil)
}

@Test func postAntiAliasingKeepsThePresenterWithMetalFXOff() throws {
    // The presenter runs CMAA2 and reads MACNEUTRON_NO_METALFX itself to skip only its upscaling.
    let f = try makeFixture()
    try f.launcher.settings.save(GameSettings(metalFX: false, postAA: "cmaa2"), for: "42")
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: f.env)
    let env = try #require(f.runner.calls.last?.environment)
    #expect(env["MACNEUTRON_PRESENT"] == "1")
    #expect(env["MACNEUTRON_NO_METALFX"] == "1")
    #expect(env["MACNEUTRON_POST_AA"] == "cmaa2")
    var off = f.env
    off["MACNEUTRON_POST_AA"] = "off"  // a launch option wins over the game's setting
    _ = f.launcher.launch(["run", "/g/Game.exe"], environment: off)
    #expect(f.runner.calls.last?.environment["MACNEUTRON_PRESENT"] == nil)
}

@Test func toolCommandsGetNoPresenter() throws {
    let f = try makeFixture()
    _ = f.launcher.launch(["runinprefix", "/g/tool.exe"], environment: f.env)
    _ = f.launcher.launch(["getcompatpath", "/g/save"], environment: f.env)
    #expect(!f.runner.calls.isEmpty)
    #expect(f.runner.calls.allSatisfy { $0.environment["MACNEUTRON_PRESENT"] == nil })
}

@Test func aCloudConflictPostsANotification() throws {
    // Spec D2: both copies are kept, and only the player can choose between them.
    let runner = FakeRunner { call in
        if call.arguments.first == "wineboot", let prefix = call.environment["WINEPREFIX"] {
            try? write("local", to: URL(filePath: prefix).appending(path: "drive_c/users/u/AppData/Local/G/s.sav"))
        }
        return 0
    }
    let f = try makeFixture(runner: runner)
    let prefix = URL(filePath: f.env["STEAM_COMPAT_DATA_PATH"]!).appending(path: "pfx")
    try write("cloud", to: prefix.appending(path: "drive_c/users/steamuser/Local Settings/Application Data/G/s.sav"))
    _ = f.launcher.launch(["waitforexitandrun", "/g/Game.exe"], environment: f.env.merging(["USER": "u"]) { _, new in new })
    #expect(f.notifier.posted == [
        "Steam Cloud saves for this game differ from the ones on this Mac, so MacNeutron kept both. Details are in launcher.log.",
    ])
}
