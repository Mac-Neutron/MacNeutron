import Darwin
import Dispatch
import Foundation

/// `macneutron` subcommands. Kept out of main.swift so they can be tested.
public enum CommandLineTool {
    public static let usage = """
        usage: macneutron launch <verb> <target> [args...]
               macneutron install --tool-dir <dir> --wine-app <wine.app> [--steam-exe <steam.exe>] [--force]
               macneutron passthrough <verb> <command> [args...]
               macneutron vm-setup --manifest <vm-manifest.json> --vm <dir> [--mkfs <mkfs.erofs>] [--min-free-gb <n>]
               macneutron vm-setup --check-sources --manifest <vm-manifest.json>
        """

    public static func run(_ args: [String], environment: [String: String], executable: URL) async -> Int32 {
        guard let command = args.first else { return usageError() }
        let rest = Array(args.dropFirst())
        switch command {
        case "launch":
            let launcher = Launcher(layout: ToolLayout(executable: executable), notifier: AppleScriptNotifier(toast: executable))
            installTerminationHandlers(launcher, environment: environment)
            return launcher.launch(rest, environment: environment)
        case "install":
            var args = rest
            let force = args.contains("--force")
            args.removeAll { $0 == "--force" }
            // Both required: the checks assemble their own tool folders, never the installed one.
            guard let dir = option("--tool-dir", in: &args), let wineApp = option("--wine-app", in: &args) else {
                return usageError()
            }
            let steamExe = option("--steam-exe", in: &args)
            guard args.isEmpty else { return usageError() }
            if let steamExe, !FileManager.default.fileExists(atPath: steamExe) {  // before the runtime changes
                return failure(CocoaError(.fileNoSuchFile, userInfo: [NSFilePathErrorKey: steamExe]))
            }
            let layout = ToolLayout(root: URL(filePath: dir, directoryHint: .isDirectory))
            do {
                let outcome = try RuntimeInstaller.install(
                    wineApp: URL(filePath: wineApp, directoryHint: .isDirectory), layout: layout,
                    launcherBinary: executable, steamExe: steamExe.map { URL(filePath: $0) }, force: force)
                print(installMessage(outcome, label: ToolLayout.runtimeLabel(version: layout.runtimeVersion,
                                                                             identity: layout.identity)))
                return installExitCode(outcome)
            } catch {
                return failure(error)
            }
        case "passthrough":
            // Steam's Mac-game tool: `<verb> <command> [args…]`. Replacing this process keeps Steam tracking its PID.
            guard rest.count >= 2 else { return usageError() }
            let target = passthroughTarget(rest[1])
            do {
                _ = try spawn(target, Array(rest.dropFirst(2)), environment: environment, replacingThisProcess: true)
            } catch let error as POSIXError {
                FileHandle.standardError.write(Data(
                    "macneutron: can't run \(target.path(percentEncoded: false)): \(String(cString: strerror(error.code.rawValue)))\n".utf8))
            } catch {
                return failure(error)
            }
            return 1  // a successful exec doesn't return
        case "vm-setup":
            var args = rest
            if args.first == "--check-sources" {  // the release: every pinned package still downloads from somewhere
                args.removeFirst()
                guard let manifest = option("--manifest", in: &args), args.isEmpty else { return usageError() }
                do {
                    let m = try JSONDecoder().decode(VMSetupManifest.self, from: Data(contentsOf: URL(filePath: manifest)))
                    let missing = await VMSetup.missingSources(m, head: VMSetup.head)
                    for file in missing { FileHandle.standardError.write(Data("vm-setup: no source serves \(file)\n".utf8)) }
                    return missing.isEmpty ? 0 : 1
                } catch {
                    return failure(error)
                }
            }
            // Both required: tests and the gate set up their own folders, never the installed one.
            guard let manifest = option("--manifest", in: &args), let vm = option("--vm", in: &args) else {
                return usageError()
            }
            let mkfs = option("--mkfs", in: &args).map { URL(filePath: $0) }
                ?? executable.deletingLastPathComponent().appending(path: "mkfs.erofs")
            let minFree = option("--min-free-gb", in: &args).flatMap { Int($0) } ?? 8
            guard args.isEmpty else { return usageError() }
            setvbuf(stdout, nil, _IOLBF, 0)  // the app reads the progress lines as they come
            // The app quitting sends SIGTERM: tar and mkfs.erofs end with this process, not after it.
            signal(SIGTERM, SIG_IGN)
            let stop = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .global())
            stop.setEventHandler {
                VMSetup.Children.terminateAll()
                exit(143)
            }
            stop.resume()
            defer { stop.cancel() }
            do {
                return try await VMSetup.run(manifest: URL(filePath: manifest), vm: URL(filePath: vm, directoryHint: .isDirectory),
                                             mkfs: mkfs, minFreeGB: minFree)
            } catch {
                FileHandle.standardError.write(Data("\(error)\n".utf8))
                return 1
            }
        default:
            return usageError()
        }
    }

    /// Removes `--name value` from `args` and returns the value.
    static func option(_ name: String, in args: inout [String]) -> String? {
        guard let index = args.firstIndex(of: name), index + 1 < args.count else { return nil }
        let value = args[index + 1]
        args.removeSubrange(index...index + 1)
        return value
    }

    /// 0 when installed or unchanged, 3 when deferred.
    static func installExitCode(_ outcome: RuntimeInstallOutcome) -> Int32 {
        if case .deferred = outcome { return 3 }
        return 0
    }

    static func installMessage(_ outcome: RuntimeInstallOutcome, label: String) -> String {
        switch outcome {
        case .installed: "installed \(label)"
        case .unchanged: "unchanged \(label)"
        case .deferred(let path): "deferred: \(path) is running"
        }
    }

    /// `<app>.app` → `<app>.app/Contents/MacOS/<CFBundleExecutable>`, as `passthrough.sh` did; anything else unchanged.
    /// Wider than the script: a folder without a readable Info.plist resolves to its own name (the script exec'd the
    /// folder, which fails).
    static func passthroughTarget(_ path: String) -> URL {
        let url = URL(filePath: path)
        var isFolder: ObjCBool = false
        guard FileManager.default.fileExists(atPath: path, isDirectory: &isFolder), isFolder.boolValue else { return url }
        let plist = (try? Data(contentsOf: url.appending(path: "Contents/Info.plist")))
            .flatMap { try? PropertyListSerialization.propertyList(from: $0, format: nil) as? [String: Any] }
        let name = plist?["CFBundleExecutable"] as? String ?? url.deletingPathExtension().lastPathComponent
        return url.appending(path: "Contents/MacOS").appending(path: name)
    }

    /// arm64e, arm64, x86_64: what `arch -arm64e -arm64 -x86_64` does. Steam itself spawns tools preferring x86_64.
    static let passthroughArchitectures: [(cpu_type_t, cpu_subtype_t)] = [
        (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64E), (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL),
        (CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL),
    ]

    /// `posix_spawn`s `executable` with `passthroughArchitectures` as its slice preference and returns its pid.
    /// `replacingThisProcess` (`POSIX_SPAWN_SETEXEC`) execs in place and returns only on failure.
    static func spawn(_ executable: URL, _ arguments: [String], environment: [String: String],
                      replacingThisProcess: Bool, standardOutput: URL? = nil) throws -> pid_t {
        var attributes: posix_spawnattr_t?
        posix_spawnattr_init(&attributes)
        defer { posix_spawnattr_destroy(&attributes) }
        var types = passthroughArchitectures.map(\.0)
        var subtypes = passthroughArchitectures.map(\.1)
        var set = 0
        var status = posix_spawnattr_setarchpref_np(&attributes, types.count, &types, &subtypes, &set)
        if status == 0, replacingThisProcess { status = posix_spawnattr_setflags(&attributes, Int16(POSIX_SPAWN_SETEXEC)) }
        var actions: posix_spawn_file_actions_t?
        posix_spawn_file_actions_init(&actions)
        defer { posix_spawn_file_actions_destroy(&actions) }
        if status == 0, let standardOutput {
            status = posix_spawn_file_actions_addopen(&actions, 1, standardOutput.path(percentEncoded: false),
                                                      O_WRONLY | O_CREAT | O_TRUNC, 0o644)
        }
        let path = executable.path(percentEncoded: false)
        let argv = ([path] + arguments).map { strdup($0) } + [nil]
        let envp = environment.map { strdup("\($0.key)=\($0.value)") } + [nil]
        defer { (argv + envp).forEach { free($0) } }
        var pid: pid_t = 0
        if status == 0 { status = posix_spawn(&pid, path, &actions, &attributes, argv, envp) }
        guard status == 0 else { throw POSIXError(POSIXErrorCode(rawValue: status) ?? .EINVAL) }
        return pid
    }

    // ponytail: global signal sources for the process's single launch; never mutated after setup.
    nonisolated(unsafe) private static var signalSources: [any DispatchSourceSignal] = []

    /// Steam's Stop button (SIGTERM) and Ctrl-C kill the game's Wine processes, then exit.
    static func installTerminationHandlers(_ launcher: Launcher, environment: [String: String]) {
        for sig in [SIGTERM, SIGINT] {
            signal(sig, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: sig, queue: .global())
            source.setEventHandler {
                launcher.terminate(environment: environment)
                exit(128 + sig)
            }
            source.resume()
            signalSources.append(source)
        }
    }

    private static func usageError() -> Int32 {
        FileHandle.standardError.write(Data((usage + "\n").utf8))
        return 2
    }

    private static func failure(_ error: any Error) -> Int32 {
        FileHandle.standardError.write(Data("macneutron: \(error)\n".utf8))
        return 1
    }
}
