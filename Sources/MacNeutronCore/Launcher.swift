import Foundation

/// Set by `Launcher.terminate` (Steam's Stop): the launch in progress starts nothing more, as a shader replay can run
/// for minutes before the game. Shared by every copy of the launcher.
final class StopFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var stopped = false
    var isSet: Bool { lock.withLock { stopped } }
    func set() { lock.withLock { stopped = true } }
}

/// Implements the `proton` verbs Steam calls on a compatibility tool.
public struct Launcher: Sendable {
    public let layout: ToolLayout
    public let runner: any ProcessRunner
    public let log: LauncherLog
    public let notifier: any Notifier
    public let preflight: Preflight
    public let settings: GameSettingsStore
    public let steam: SteamLocation
    /// Where the microVM is installed (MicroVM.swift): every game runs in it when it is.
    public let microVMRoot: URL
    /// The Mac's display in pixels (MicroVM.mainDisplay): the guest's screen, unless a launch option sets MN_SCREEN.
    public let display: @Sendable () -> (width: Int, height: Int)?
    let stopRequested = StopFlag()

    public init(layout: ToolLayout, runner: any ProcessRunner = SystemProcessRunner(), log: LauncherLog = .standard,
                notifier: any Notifier = AppleScriptNotifier(), preflight: Preflight = Preflight(),
                settings: GameSettingsStore = GameSettingsStore(), steam: SteamLocation = SteamLocation(),
                microVMRoot: URL = MicroVM.standardRoot,
                display: @escaping @Sendable () -> (width: Int, height: Int)? = MicroVM.mainDisplay) {
        self.layout = layout
        self.runner = runner
        self.log = log
        self.notifier = notifier
        self.preflight = preflight
        self.settings = settings
        self.steam = steam
        self.microVMRoot = microVMRoot
        self.display = display
    }

    /// Returns the exit code for Steam. Never throws: every failure is logged and becomes exit 1.
    public func launch(_ argv: [String], environment steamEnvironment: [String: String]) -> Int32 {
        var environment = steamEnvironment
        let request: LaunchRequest
        let context: CompatContext
        do {
            request = try LaunchRequest.parse(argv)
            context = try CompatContext(environment: environment)
        } catch {
            return fail("\(error)", argv: argv, notify: false)
        }
        do {
            environment = try withSettings(environment, appID: context.appID)
        } catch {
            log.append("note: ignoring unreadable game settings for \(context.appID): \(error)")
            // The defaults still apply (16x anisotropic filtering is a default, not an empty setting).
            environment = GameSettings().environment.merging(environment) { _, launchOption in launchOption }
        }
        let logging = environment["MACNEUTRON_LOG"] == "1" || environment["MACNEUTRON_LOG"] == "2"
        let gameLog = logging ? log.gameLog(appID: context.appID) : nil
        let identity: String
        do {
            identity = try preflight.check(layout, request: request)
        } catch {
            return fail(error.description, argv: argv, notify: true, gameLog: gameLog)
        }
        // Steam's install scripts start redistributable installers with `run`; a 32-bit one can't run here.
        if request.verb == .run, PEImage.machine(of: URL(filePath: request.target)) == PEImage.i386 {
            log.append("skipped 32-bit installer \(URL(filePath: request.target).lastPathComponent)")
            return 0
        }

        if MicroVM.wanted(environment: environment, verb: request.verb) {
            if let (library, guest) = MicroVM.guestPath(request.target) {
                // In the VM or not at all: a launch before the app's setup finishes is refused, never run on macOS.
                guard let vm = MicroVM.find(root: microVMRoot, environment: environment, verb: request.verb) else {
                    return fail("MacNeutron's game VM isn't ready yet: open MacNeutron, and launch the game again "
                                + "once its Settings say \"Game VM: ready\".", argv: argv, notify: true, gameLog: gameLog)
                }
                return runInMicroVM(vm, request, context, library: library, guest: guest, environment, gameLog, argv)
            }
            log.append("note: \(request.target) is not in a Steam library: it runs on macOS")
        }

        let (backend, note) = GraphicsBackend.select(requested: environment["MACNEUTRON_GRAPHICS"])
        var env = LaunchEnvironment.build(base: environment, context: context, backend: backend, logging: logging)
        // MACNEUTRON_PERF=1: DXMT's frame statistics, for eac-vm/tests/perf-compare.sh. The user's own settings win.
        // DXMT keeps stats.txt in its DXIL dump folder, which also turns on its capture mode: every new shader, root
        // signature and pipeline is saved there too (a little CPU, and files that build up in the folder): the
        // folder is emptied at each launch, unless the user named another. When their settings or the backend keep
        // stats.txt out of the folder, the log says so (the record's fps would come out empty).
        if environment["MACNEUTRON_PERF"] == "1" {
            var why: String?
            if backend != .dxmt {
                why = "needs the DXMT backend"
            } else if env["DXMT_STATS"] != nil {
                why = "DXMT_STATS is set"
            } else {
                let folder = perfFolder(appID: context.appID)
                if env["DXMT_DXIL_DUMP"] == nil {
                    try? FileManager.default.removeItem(at: folder)
                } else {
                    why = "DXMT_DXIL_DUMP is set"
                }
                try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                env["DXMT_STATS"] = "1"
                env["DXMT_DXIL_DUMP"] = env["DXMT_DXIL_DUMP"] ?? folder.path(percentEncoded: false)
            }
            if let why { log.append("note: MACNEUTRON_PERF=1: no frame statistics in the perf folder: \(why)") }
        }
        let steamBridge = usesSteamBridge(request.verb, env)
        if steamBridge { addSteamClient(to: &env) }
        // The MetalFX presenter inside wine.app reads this; only the game's verbs ask for it. With MetalFX off it still
        // runs for post-AA, and reads MACNEUTRON_NO_METALFX itself to skip only its upscaling.
        if request.verb == .run || request.verb == .waitforexitandrun,
           env["MACNEUTRON_NO_METALFX"] != "1" || env["MACNEUTRON_POST_AA"] == "cmaa2", env["MACNEUTRON_PRESENT"] == nil {
            env["MACNEUTRON_PRESENT"] = "1"
        }
        if let gameLog { writeHeader(to: gameLog, request: request, environment: env) }
        let notifier = notifier, setup = Self.setupToast(request.target)
        let prefix = PrefixManager(context: context, layout: layout, identity: identity, runner: runner, log: log,
                                   onPrepare: { notifier.progress(title: setup.title, message: setup.message, until: nil).close })

        do {
            let status: Int32
            switch request.verb {
            case .runinprefix:
                status = try runGame(request, env, gameLog, throughSteam: false)
            case .run:
                notifyAboutCloudConflicts(try prefix.prepare(environment: env, steamBridge: steamBridge))
                if !steamBridge { try prefix.removeSteamBridge() }
                status = try runGame(request, env, gameLog, throughSteam: steamBridge)
            case .waitforexitandrun:
                // Prepare first (Proton's order): a launch queued on the prefix lock behind
                // `run iscriptevaluator.exe` then finds that session's wineserver alive, and
                // `-w` waits for the redistributable installers to finish.
                notifyAboutCloudConflicts(try prefix.prepare(environment: env, steamBridge: steamBridge))
                if !steamBridge { try prefix.removeSteamBridge() }
                _ = try runner.run(layout.wineserver, ["-w"], environment: env, output: nil)
                // Shader pre-caching: after a DXMT or macOS update, rebuild the recorded pipelines before the game.
                let precache = ShaderPrecache.enabled(backend: backend, environment: env)
                    ? ShaderPrecache(context: context, layout: layout) : nil
                if let precache, precache.needsReplay {
                    let toast = notifier.progress(title: "Preparing \(Self.gameName(request.target))'s shaders",
                                                  message: "DXMT or macOS changed: they're rebuilt once, before it starts.",
                                                  until: nil)
                    let lines = precache.replay(layout: layout, runner: runner, environment: env,
                                                stopped: { stopRequested.isSet }, progress: toast.update)
                    toast.close()
                    for line in lines { log.append(line) }
                    if !stopRequested.isSet { precache.writeStamp() }
                }
                if stopRequested.isSet {
                    // Steam's Stop during the replay: start nothing; with the stamp unchanged, the next launch replays.
                    status = 128 + SIGTERM
                } else {
                    status = try runGame(request, env, gameLog, throughSteam: steamBridge)
                    // Keep Steam's "running" state until every process in the prefix is gone
                    // (covers launchers that start the real game and exit).
                    _ = try runner.run(layout.wineserver, ["-w"], environment: env, output: nil)
                    precache?.writeStampIfMissing()
                }
            case .getcompatpath, .getnativepath:
                try prefix.prepare(environment: env)
                let flag = request.verb == .getcompatpath ? "-w" : "-u"
                status = try runner.run(layout.wine, ["winepath.exe", flag, request.target], environment: env, output: nil)
            }
            var line = "verb=\(request.verb.rawValue) appid=\(context.appID) backend=\(backend.rawValue)"
                + " runtime=\(ToolLayout.runtimeLabel(version: layout.runtimeVersion, identity: identity)) exit=\(status)"
            if let note { line += " note=\(note)" }
            log.append(line)
            return status
        } catch {
            return fail("\(error)", argv: argv, notify: true, gameLog: gameLog)
        }
    }

    /// The game in the microVM (MicroVM.swift): run-guest.sh boots it with the game's state disk and Steam library,
    /// Steam forwarded to the Mac's for the game's app ID, and returns the game's status once every process in its
    /// prefix is gone (run-game.sh). A VM per game, side by side: the same game twice is refused (75).
    /// ponytail: Steam's Stop ends only the launcher, and a fixed 16 GiB of RAM, until milestone 5's agent and sizing.
    private func runInMicroVM(_ vm: MicroVM, _ request: LaunchRequest, _ context: CompatContext, library: String,
                              guest: String, _ launchEnvironment: [String: String], _ gameLog: URL?, _ argv: [String]) -> Int32 {
        // The guest's screen is the Mac's display, so a game is offered its resolutions up to the display's own.
        var environment = launchEnvironment
        if environment["MN_SCREEN"].map(MicroVM.isScreenSize) != true {
            environment["MN_SCREEN"] = display().map { "\($0.width)x\($0.height)" }
        }
        // MACNEUTRON_PERF=1: the guest profiler (run-game.sh); frames are counted on the host (MN_PRESENT_STATS below),
        // where DXMT_STATS's timing would cost the guest a clock read under FEX for every D3D12 call.
        let perf = environment["MACNEUTRON_PERF"] == "1"
        if perf { environment["MACNEUTRON_VM_PROFILE"] = "1" }
        do {
            let script = try vm.script(verb: request.verb, appID: context.appID, guest: guest,
                                       arguments: request.arguments, environment: environment)
            let file = FileManager.default.temporaryDirectory
                .appending(path: "macneutron-vm-\(context.appID)-\(UUID().uuidString).sh")
            try script.write(to: file, atomically: true, encoding: .utf8)
            defer { try? FileManager.default.removeItem(at: file) }
            // A game's first run makes its prefix in the guest, behind no window: a toast until run-game.sh says it's
            // ready (the last run's word removed first).
            var shown: @Sendable () -> Void = {}
            if !FileManager.default.fileExists(atPath: vm.state(appID: context.appID).path(percentEncoded: false)) {
                let ready = vm.share(appID: context.appID).appending(path: "traces/ready")
                try? FileManager.default.removeItem(at: ready)
                let setup = Self.setupToast(request.target)
                shown = notifier.progress(title: setup.title, message: setup.message, until: ready).close
            }
            defer { shown() }
            var env = environment
            env["MN_VM_DIR"] = vm.root.path(percentEncoded: false)
            env["MN_STATE"] = vm.state(appID: context.appID).path(percentEncoded: false)
            env["MN_SHARE"] = vm.share(appID: context.appID).path(percentEncoded: false)
            env["MN_TOAST"] = layout.launcherBinary.path(percentEncoded: false)  // eac-vm/host/tpm.c's, while macOS asks
            // DLSS's NVIDIA extension in native DXMT too (Direct3D forwarding: it reads macneutron-vm's environment)
            if environment["MACNEUTRON_DLSS"] == "1", env["DXMT_ENABLE_NVEXT"] == nil { env["DXMT_ENABLE_NVEXT"] = "1" }
            // Forwarded, native DXMT's shader cache and recordings are the game's, in its VM state folder (macneutron-vm
            // points DXMT at them); after a translator or macOS update its recordings replay natively before the boot.
            var precache: ShaderPrecache?
            if environment["MACNEUTRON_VM_D3D"] == "forward" {
                let state = vm.dxmtState(appID: context.appID)
                env["MN_D3D_STATE"] = state.path(percentEncoded: false)
                if ShaderPrecache.enabled(backend: .dxmt, environment: environment) {
                    precache = ShaderPrecache(folder: state.appending(path: "pipelines"),
                                              builds: "\(vm.dxmtTranslator ?? layout.dxmtTranslator ?? "none") "
                                                  + ShaderPrecache.macOSBuild())
                }
                if let precache, precache.needsReplay {
                    let toast = notifier.progress(title: "Preparing \(Self.gameName(request.target))'s shaders",
                                                  message: "DXMT or macOS changed: they're rebuilt once, before it starts.",
                                                  until: nil)
                    let lines = precache.replayNative(tool: vm.dxmtReplay, cache: state, runner: runner, environment: env,
                                                      stopped: { stopRequested.isSet }, progress: toast.update)
                    toast.close()
                    for line in lines { log.append(line) }
                    if stopRequested.isSet { return 128 + SIGTERM } // Steam's Stop: no boot; the next launch replays
                    precache.writeStamp()
                }
            }
            defer { precache?.writeStampIfMissing() }
            if perf {
                env["MN_PRESENT_STATS"] = vm.share(appID: context.appID).appending(path: "traces/frames.txt")
                    .path(percentEncoded: false)
            }
            env["MN_RUN_TIMEOUT"] = env["MN_RUN_TIMEOUT"] ?? "86400"
            let status = try runner.run(vm.runGuest, [file.path(percentEncoded: false), "--steam-appid", context.appID,
                                                      "--ram-mib", "16384", "--steam-map", "\(library)=/mnt/games"],
                                        environment: env, output: gameLog)
            log.append("verb=\(request.verb.rawValue) appid=\(context.appID) backend=vm vm=\(vm.root.path(percentEncoded: false))"
                       + " exit=\(status)")
            // macneutron-vm's 7: the game's TPM didn't start, so the guest never ran (run-guest.sh removed the last
            // run.status first).
            let ran = vm.share(appID: context.appID).appending(path: "run.status")
            if status == 7, !FileManager.default.fileExists(atPath: ran.path(percentEncoded: false)) {
                notifier.post(title: "MacNeutron", message: "This game's TPM didn't start: the login keychain refused its"
                              + " key, or its files couldn't be read. The game's log has the reason.")
            }
            return status
        } catch {
            return fail("\(error)", argv: argv, notify: true, gameLog: gameLog)
        }
    }

    /// `MACNEUTRON_PERF=1`'s folder for a game's DXMT frame statistics on macOS (perf-compare.sh reads its stats.txt).
    public func perfFolder(appID: String) -> URL {
        log.directory.appending(path: "perf/\(appID)", directoryHint: .isDirectory)
    }

    /// The toast while a game's prefix is prepared.
    static func setupToast(_ target: String) -> (title: String, message: String) {
        ("Setting up \(gameName(target))", "Preparing its Windows environment. The first launch takes a minute or two.")
    }

    /// A game's folder in steamapps/common (Steam's name for it, near enough), or "the game".
    static func gameName(_ target: String) -> String {
        let name = target.range(of: "/steamapps/common/").flatMap { target[$0.upperBound...].split(separator: "/").first }
        return name.map(String.init) ?? "the game"
    }

    /// Per-game settings from the app sit underneath; variables from Steam launch options win.
    private func withSettings(_ environment: [String: String], appID: String) throws -> [String: String] {
        try settings.load(appID).environment.merging(environment) { _, launchOption in launchOption }
    }

    /// Steam's Stop button sends SIGTERM: kill every Wine process in the game's prefix, in the environment the
    /// launch built (the server only talks to a client in its own msync mode).
    public func terminate(environment: [String: String]) {
        stopRequested.set()
        guard let context = try? CompatContext(environment: environment) else { return }
        let merged = (try? withSettings(environment, appID: context.appID)) ?? environment
        let env = LaunchEnvironment.build(base: merged, context: context,
                                          backend: GraphicsBackend.select(requested: merged["MACNEUTRON_GRAPHICS"]).backend,
                                          logging: false)
        _ = try? runner.run(layout.wineserver, ["-k"], environment: env, output: nil)
    }

    private func runGame(_ request: LaunchRequest, _ env: [String: String], _ gameLog: URL?,
                         throughSteam: Bool) throws -> Int32 {
        let game = [request.target] + request.arguments
        // The macOS path as Steam gave it: steam.exe converts it (Proton's run_process), to S: for a game in a library.
        let command = throughSteam ? [SteamBridge.steamExe(forGameID: env["SteamGameId"]), request.target] + request.arguments
            : game
        return try runner.run(layout.wine, command, environment: env, output: gameLog)
    }

    /// Spec D2: Steam's copy and the Mac's were both kept, and only the player can choose between them.
    private func notifyAboutCloudConflicts(_ links: ProfileLinkReport) {
        guard links.conflicts > 0 else { return }
        notifier.post(title: "MacNeutron", message: "Steam Cloud saves for this game differ from the ones on this Mac,"
                      + " so MacNeutron kept both. Details are in launcher.log.")
    }

    /// `run` and `waitforexitandrun` start the game through `steam.exe` when the bridge is installed,
    /// as Proton does; the log says why when they don't.
    private func usesSteamBridge(_ verb: Verb, _ environment: [String: String]) -> Bool {
        guard verb == .run || verb == .waitforexitandrun else { return false }
        if environment["MACNEUTRON_NO_STEAM_BRIDGE"] == "1" {
            log.append("note: Steam bridge disabled by launch option")
            return false
        }
        guard layout.steamBridgeInstalled else {
            log.append("note: Steam bridge not installed")
            return false
        }
        return true
    }

    /// Tells the runtime's lsteamclient where macOS Steam's client library is, and steam.exe Steam's language.
    private func addSteamClient(to env: inout [String: String]) {
        let passed = env["STEAM_COMPAT_CLIENT_INSTALL_PATH"]
        let client = SteamBridge.clientDirectory(steamValue: passed, steam: steam)
        env["STEAM_COMPAT_CLIENT_INSTALL_PATH"] = client
        log.append("note: Steam client folder \(client) (Steam passed \(passed ?? "nothing"))")
        if !FileManager.default.fileExists(atPath: client + "/steamclient.dylib") {
            log.append("note: steamclient.dylib not found in \(client)")
        }
        if env["MACNEUTRON_STEAM_LANGUAGE"] == nil { env["MACNEUTRON_STEAM_LANGUAGE"] = steam.uiLanguage() }
    }

    /// People post game logs in bug reports: Steam passes the account's login name as SteamUser and SteamAppUser.
    static let redactedKeys: Set<String> = ["SteamUser", "SteamAppUser"]

    /// The rest of the environment comes from whatever started Steam (a terminal's shell can carry API tokens):
    /// any name that reads like a secret is hidden too.
    static func isRedacted(_ key: String) -> Bool {
        let name = key.uppercased()
        return redactedKeys.contains(key)
            || ["TOKEN", "SECRET", "PASSWORD", "PASSWD", "CREDENTIAL", "APIKEY", "API_KEY", "AUTH"].contains { name.contains($0) }
            || name.hasSuffix("_KEY")
    }

    private func writeHeader(to gameLog: URL, request: LaunchRequest, environment: [String: String]) {
        var text = "=== \(Date().formatted(.iso8601)) \(request.verb.rawValue) \(request.target) \(request.arguments)\n"
        for key in environment.keys.sorted() {
            text += "\(key)=\(Self.isRedacted(key) ? "<redacted>" : environment[key]!)\n"
        }
        append(text, to: gameLog)
    }

    private func append(_ text: String, to gameLog: URL) {
        if let handle = try? FileHandle(forWritingTo: gameLog) {
            defer { try? handle.close() }
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: Data(text.utf8))
        } else {
            try? Data(text.utf8).write(to: gameLog)
        }
    }

    private func fail(_ message: String, argv: [String], notify: Bool, gameLog: URL? = nil) -> Int32 {
        log.append("error: \(message) argv=\(argv)")
        if let gameLog { append("macneutron: \(message)\n", to: gameLog) }
        FileHandle.standardError.write(Data("macneutron: \(message)\n".utf8))
        if notify { notifier.post(title: "MacNeutron", message: message) }
        return 1
    }
}
