import Darwin
import Foundation

public enum PrefixError: Error, Equatable, CustomStringConvertible {
    case lockFailed(String)
    case winebootFailed(Int32)
    case emulatorSetupFailed(Int32)
    case dllCopyFailed(String)
    case steamBridgeCopyFailed(String)

    public var description: String {
        switch self {
        case .lockFailed(let reason): "could not lock the prefix: \(reason)"
        case .winebootFailed(let status): "prefix setup failed (wineboot exit \(status)); the next launch retries"
        case .emulatorSetupFailed(let status):
            "prefix setup failed (registering FEX exited \(status)); the next launch retries"
        case .dllCopyFailed(let detail): "could not install graphics DLLs: \(detail)"
        case .steamBridgeCopyFailed(let detail): "could not install the Steam bridge: \(detail)"
        }
    }
}

/// Creates, upgrades and equips a game's Wine prefix at `compatdata/<appid>/pfx`. Its stamp,
/// `compatdata/<appid>/version`, names the `wine.app` that prepared it and the msync mode its wineserver runs in.
public struct PrefixManager: Sendable {
    public let context: CompatContext
    public let layout: ToolLayout
    public let identity: String
    public let runner: any ProcessRunner
    public let log: LauncherLog

    public init(context: CompatContext, layout: ToolLayout, identity: String, runner: any ProcessRunner, log: LauncherLog) {
        self.context = context
        self.layout = layout
        self.identity = identity
        self.runner = runner
        self.log = log
    }

    public static func stamp(identity: String, msync: Bool) -> String { "wine.app \(identity) msync=\(msync ? 1 : 0)" }
    /// Written before wineboot, so a failed or stopped preparation is retried in place, never renamed.
    static let preparingStamp = "wine.app preparing"
    /// In the compat folder: the renamed prefix whose data is still to be carried, written before the rename, so a
    /// launch stopped anywhere after it carries on the next one. After the carry it becomes `carriedMarker` (or is
    /// removed).
    static let pendingRecord = "player-data-pending"
    /// The last carry's record (its prefix's name); nothing reads it.
    static let carriedMarker = "player-data-carried"

    /// Prepares the prefix when it is missing or another `wine.app` prepared it; stops a wineserver left in the other
    /// msync mode; keeps Steam Cloud's profile links (spec §4.2); then, when asked, installs the Steam bridge. Holds the
    /// prefix lock only for this, never while the game runs. Nothing under `drive_c` is ever deleted: a Rosetta-era
    /// prefix is renamed. Returns what the links did, for the launcher to tell the player about a conflict.
    @discardableResult
    public func prepare(environment: [String: String], steamBridge: Bool = false) throws -> ProfileLinkReport {
        let fm = FileManager.default
        try fm.createDirectory(at: context.dataPath, withIntermediateDirectories: true)
        return try withFileLock(at: context.lockFile) {
            // Wine reads WINEMSYNC with atoi; launch options may set it to anything.
            let want = Self.stamp(identity: identity, msync: atoi(environment["WINEMSYNC"] ?? "") != 0)
            let recorded = (try? String(contentsOf: context.versionFile, encoding: .utf8))?
                .trimmingCharacters(in: .whitespacesAndNewlines)
            let exists = fm.fileExists(atPath: context.prefix.path(percentEncoded: false))
            if exists, recorded == want {
                // Up to date.
            } else if exists, let recorded, recorded.hasPrefix("wine.app \(identity) msync=") {
                // A client and a server in different msync modes can't talk: stop the last launch's server.
                var old = environment
                old["WINEMSYNC"] = recorded.hasSuffix(" msync=1") ? "1" : nil
                _ = try runner.run(layout.wineserver, ["-k"], environment: old, output: nil)
                log.append("note: msync changed, stopped the prefix's wineserver")
                try want.write(to: context.versionFile, atomically: true, encoding: .utf8)
            } else {
                // An arm64 Wine doesn't adopt an x86_64 Wine's prefix. Only a rename carries: a preparation stopped
                // after one (its pending record says which) carries on its retry; an arm64 prefix prepared again
                // never does, whatever pfx.rosetta lies beside it.
                var carryFrom: String?
                // Steam's cloud download makes pfx before the tool ever runs (no stamp, no user.reg): it is built
                // into, never renamed, or the saves it holds go missing from where Steam watches them (spec §4.3).
                let seeded = recorded == nil
                    && !fm.fileExists(atPath: context.prefix.appending(path: "user.reg").path(percentEncoded: false))
                if exists, recorded?.hasPrefix("wine.app ") != true, !seeded {
                    carryFrom = try renameRosettaPrefix()
                } else {
                    carryFrom = (try? String(contentsOf: context.dataPath.appending(path: Self.pendingRecord), encoding: .utf8))
                        .flatMap { $0.hasPrefix("pfx.rosetta") && !$0.contains("/") ? $0 : nil }
                }
                try Self.preparingStamp.write(to: context.versionFile, atomically: true, encoding: .utf8)
                try prepareNew(environment: environment)
                // After prepareNew's `wineserver -w`: no server runs to rewrite user.reg.
                if let carryFrom { carryPlayerData(from: carryFrom) }
                try want.write(to: context.versionFile, atomically: true, encoding: .utf8)
            }
            // Every launch: Steam may have made real folders where the links go since the last one.
            let links = ProfileLinks.apply(prefix: context.prefix, user: environment["USER"] ?? NSUserName())
            for note in links.notes { log.append("note: \(note)") }
            if steamBridge {
                try deploySteamBridge(environment: environment)
                writeSteamFiles(environment: environment)
            }
            return links
        }
    }

    private func prepareNew(environment: [String: String]) throws {
        var boot = environment
        // No Mono or Gecko prompt.
        boot["WINEDLLOVERRIDES"] = LaunchEnvironment.mergeOverrides(environment["WINEDLLOVERRIDES"] ?? "",
                                                                    user: "mscoree,mshtml=")
        let status = try runner.run(layout.wine, ["wineboot", "-u"], environment: boot, output: nil)
        guard status == 0 else { throw PrefixError.winebootFailed(status) }
        // x64 code runs on FEX; without this entry, on Wine's stub xtajit64, and every x64 game fails.
        let fex = try runner.run(layout.wine, ["reg", "add", #"HKLM\Software\Microsoft\Wow64\amd64"#, "/ve",
                                               "/d", "libarm64ecfex.dll", "/f"], environment: environment, output: nil)
        guard fex == 0 else { throw PrefixError.emulatorSetupFailed(fex) }
        // A crashing game must exit, not wait behind Wine's crash window while Steam shows it running.
        // ponytail: best effort; if reg fails, crashes still work, they just show the dialog.
        _ = try runner.run(layout.wine, ["reg", "add", #"HKCU\Software\Wine\WineDbg"#, "/v", "ShowCrashDialog",
                                         "/t", "REG_DWORD", "/d", "0", "/f"], environment: environment, output: nil)
        // A game's exclusive fullscreen changes Wine's virtual mode, scaled to the screen, never the Mac's display
        // (win32u reads it for winemac too, despite the key's name). Existing prefixes get it with the runtime that
        // brings it: a new wine.app changes the stamp, which prepares them again. Best effort, as above.
        _ = try runner.run(layout.wine, ["reg", "add", #"HKCU\Software\Wine\X11 Driver"#, "/v", "EmulateModeset",
                                         "/d", "Y", "/f"], environment: environment, output: nil)
        // Whatever the game's graphics setting: wined3d's overrides ignore them, and switching back to DXMT
        // doesn't change the stamp. The identity covers DXMT, so they're refreshed whenever wine.app changes.
        for name in ToolLayout.dxmtDLLs {
            let source = layout.dxmt.appending(path: name), destination = "drive_c/windows/system32/\(name)"
            guard FileManager.default.fileExists(atPath: source.path(percentEncoded: false)) else {
                throw PrefixError.dllCopyFailed("\(destination): missing from the runtime (\(source.path(percentEncoded: false)))")
            }
            do { try install(source, at: destination) } catch {
                throw PrefixError.dllCopyFailed("\(destination): \(error.localizedDescription)")
            }
        }
        _ = try runner.run(layout.wineserver, ["-w"], environment: environment, output: nil)
    }

    /// `pfx` → the first free `pfx.rosetta`, `pfx.rosetta-2`, … (the saves inside stay where the player can find them);
    /// returns the new name, recorded as pending first.
    private func renameRosettaPrefix() throws -> String {
        var name = "pfx.rosetta"
        var number = 1
        while FileManager.default.fileExists(atPath: context.dataPath.appending(path: name).path(percentEncoded: false)) {
            number += 1
            name = "pfx.rosetta-\(number)"
        }
        try name.write(to: context.dataPath.appending(path: Self.pendingRecord), atomically: true, encoding: .utf8)
        let fm = FileManager.default, old = context.dataPath.appending(path: name)
        try fm.moveItem(at: context.prefix, to: old)
        // Steam's cloud download made users/steamuser beside the Rosetta-era profile (`crossover`): its files go straight
        // back where Steam watches them, for ProfileLinks to merge (spec §4.3); if they can't, the rename is undone.
        // ponytail: two renames, so a kill between them leaves those paths missing for one exit pass; a RENAME_SWAP of
        // a staged pfx would close that window.
        if let cloud = Self.steamsOwnFolder(in: old) {
            let users = context.prefix.appending(path: "drive_c/users")
            do {
                try fm.createDirectory(at: users, withIntermediateDirectories: true)
                try fm.moveItem(at: cloud, to: users.appending(path: "steamuser"))
            } catch {
                for folder in [users, users.deletingLastPathComponent(), context.prefix] {
                    rmdir(folder.path(percentEncoded: false))  // only the empty folders made just above
                }
                try? fm.moveItem(at: old, to: context.prefix)
                throw error
            }
        }
        log.append("note: renamed a Rosetta-era prefix to \(name)")
        return name
    }

    /// `users/steamuser` when Steam made it: a real folder beside another profile (Wine names its own after `$USER`, the
    /// Rosetta-era Wine `crossover`).
    static func steamsOwnFolder(in prefix: URL) -> URL? {
        let users = prefix.appending(path: "drive_c/users")
        func isRealFolder(_ name: String) -> Bool {
            var info = stat()
            return lstat(users.appending(path: name).path(percentEncoded: false), &info) == 0 && info.st_mode & S_IFMT == S_IFDIR
        }
        let names = (try? FileManager.default.contentsOfDirectory(atPath: users.path(percentEncoded: false))) ?? []
        guard isRealFolder("steamuser"),
              names.contains(where: { !["public", "steamuser"].contains($0.lowercased()) && isRealFolder($0) }) else { return nil }
        return users.appending(path: "steamuser")
    }

    /// The fresh prefix gets the renamed one's settings and local saves (spec §14, amending §3.4 step 1). What isn't
    /// carried only costs the player that: the old prefix is still there, untouched.
    private func carryPlayerData(from name: String) {
        let carried = PlayerData.carry(from: context.dataPath.appending(path: name, directoryHint: .isDirectory),
                                       to: context.prefix)
        log.append("note: carried the player's data from \(name): \(carried.files) files, \(carried.keys) registry keys"
                   + (carried.failed > 0 ? ", \(carried.failed) not carried" : ""))
        // A pending record left behind would carry this prefix again into a live one (a re-preparation stopped and
        // retried); the marker is never read, so if it can't be renamed it goes.
        let pending = context.dataPath.appending(path: Self.pendingRecord).path(percentEncoded: false)
        if rename(pending, context.dataPath.appending(path: Self.carriedMarker).path(percentEncoded: false)) != 0 {
            unlink(pending)
        }
    }

    /// Every launch: prefixes made before the bridge existed get it too, and a runtime update replaces it.
    func deploySteamBridge(environment: [String: String]) throws {
        var steamClient64: URL?
        if let path = environment[SteamBridge.steamClient64Variable], !path.isEmpty {
            // Logged first, so a bad path is diagnosable; per launch, so dropping the option puts lsteamclient.dll
            // back (its size differs).
            log.append("note: steamclient64.dll from \(path) (\(SteamBridge.steamClient64Variable))")
            guard PEImage.machine(of: URL(filePath: path)) == PEImage.amd64 else {
                throw PrefixError.steamBridgeCopyFailed(
                    "\(SteamBridge.steamClient64Variable): \(path) is missing or not an x64 DLL")
            }
            steamClient64 = URL(filePath: path)
        }
        for (source, destination) in SteamBridge.prefixFiles(layout: layout, steamClient64: steamClient64) {
            do { try install(source, at: destination) } catch {
                throw PrefixError.steamBridgeCopyFailed("\(destination): \(error.localizedDescription)")
            }
        }
    }

    /// The rest of Proton's setup_steam_files: Steam's `config` folder, which CEG checks for, and
    /// `steamapps/libraryfolders.vdf`. Best effort: a game starts without them. `removeSteamBridge` leaves them, as
    /// Proton never removes them.
    private func writeSteamFiles(environment: [String: String]) {
        let steam = context.prefix.appending(path: SteamBridge.prefixFolder, directoryHint: .isDirectory)
        for folder in ["config", "steamapps"] {
            try? FileManager.default.createDirectory(at: steam.appending(path: folder), withIntermediateDirectories: true)
        }
        guard let paths = environment["STEAM_COMPAT_LIBRARY_PATHS"] else { return }
        try? SteamBridge.libraryFolders(steamapps: paths)
            .write(to: steam.appending(path: "steamapps/libraryfolders.vdf"), atomically: true, encoding: .utf8)
    }

    /// For a game started without the bridge (launch option, or a runtime without it): its steam_api
    /// loads the client DLL an earlier launch left (its registry values persist), and the bridge can
    /// then abort the game. Without these files the game just finds no Steam.
    public func removeSteamBridge() throws {
        try withFileLock(at: context.lockFile) {
            for name in ["steam.exe", "steamclient64.dll", "steamclient.dll"] {
                try? FileManager.default.removeItem(at: context.prefix.appending(path: "\(SteamBridge.prefixFolder)/\(name)"))
            }
        }
    }

    /// Copies `source` into the prefix unless the copy there has its size and modification time (lsteamclient.dll
    /// is 57 MB); the copy gets the source's times. `stat`/`utimensat` keep nanoseconds, which
    /// `FileManager.setAttributes` truncates to microseconds (the copy would then never match).
    private func install(_ source: URL, at destination: String) throws {
        let fm = FileManager.default
        let target = context.prefix.appending(path: destination)
        let from = source.path(percentEncoded: false), to = target.path(percentEncoded: false)
        var src = stat(), dst = stat()
        guard stat(from, &src) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        if stat(to, &dst) == 0, dst.st_size == src.st_size, dst.st_mtimespec.tv_sec == src.st_mtimespec.tv_sec,
           dst.st_mtimespec.tv_nsec == src.st_mtimespec.tv_nsec {
            return
        }
        try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
        if fm.fileExists(atPath: to) { try fm.removeItem(at: target) }
        try fm.copyItem(at: source, to: target)
        var times = [src.st_atimespec, src.st_mtimespec]
        guard utimensat(AT_FDCWD, to, &times, 0) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
    }
}

/// Exclusive `flock(2)` on `url` for the duration of `body`. Separate `open`s conflict even
/// within one process, so this also serialises threads.
func withFileLock<T>(at url: URL, _ body: () throws -> T) throws -> T {
    let fd = open(url.path(percentEncoded: false), O_CREAT | O_RDWR, 0o644)
    guard fd >= 0 else { throw PrefixError.lockFailed(String(cString: strerror(errno))) }
    defer { close(fd) }
    guard flock(fd, LOCK_EX) == 0 else { throw PrefixError.lockFailed(String(cString: strerror(errno))) }
    defer { flock(fd, LOCK_UN) }
    return try body()
}
