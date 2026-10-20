import Foundation

/// Where MacNeutron keeps its own files.
public enum MacNeutronPaths {
    public static var root: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/MacNeutron", directoryHint: .isDirectory)
    }
    public static var tools: URL { root.appending(path: "compatibilitytools.d", directoryHint: .isDirectory) }
    public static var games: URL { root.appending(path: "games", directoryHint: .isDirectory) }
    public static var backups: URL { root.appending(path: "backups", directoryHint: .isDirectory) }
}

/// Paths inside the native macOS Steam installation.
public struct SteamLocation: Equatable, Sendable {
    public let root: URL

    public init(root: URL = FileManager.default.homeDirectoryForCurrentUser
        .appending(path: "Library/Application Support/Steam", directoryHint: .isDirectory)) {
        self.root = root
    }

    public var bundleMacOS: URL { root.appending(path: "Steam.AppBundle/Steam/Contents/MacOS", directoryHint: .isDirectory) }
    public var bundleCompatTools: URL { bundleMacOS.appending(path: "compatibilitytools.d", directoryHint: .isDirectory) }
    public var steamDevConfig: URL { bundleMacOS.appending(path: "steam_dev.cfg") }
    public var configVDF: URL { root.appending(path: "config/config.vdf") }
    public var compatLog: URL { root.appending(path: "logs/compat_log.txt") }
    public var appInfo: URL { root.appending(path: "appcache/appinfo.vdf") }
    /// Steam's own settings; it also holds the login name (AutoLoginUser), which is never read.
    public var registryVDF: URL { root.appending(path: "registry.vdf") }

    public var isInstalled: Bool { FileManager.default.fileExists(atPath: bundleMacOS.path(percentEncoded: false)) }

    /// Every library's `steamapps` folder, the Steam root's own first, from `libraryfolders.vdf`.
    public func libraries() -> [URL] {
        let own = root.appending(path: "steamapps", directoryHint: .isDirectory)
        var result = [own]
        let file = own.appending(path: "libraryfolders.vdf")
        guard let text = try? String(contentsOf: file, encoding: .utf8),
              let nodes = try? KeyValues.parse(text) else { return result }
        for entry in nodes.node(at: ["libraryfolders"])?.children ?? [] {
            guard let path = entry.children.node(at: ["path"])?.stringValue else { continue }
            let steamapps = URL(filePath: path, directoryHint: .isDirectory).appending(path: "steamapps", directoryHint: .isDirectory)
            if !result.contains(where: { Self.samePath($0, steamapps) }) { result.append(steamapps) }
        }
        return result
    }

    /// App IDs with an `appmanifest_<id>.acf` in any library.
    public func installedAppIDs() -> Set<UInt32> {
        var ids = Set<UInt32>()
        for library in libraries() {
            let names = (try? FileManager.default.contentsOfDirectory(atPath: library.path(percentEncoded: false))) ?? []
            for name in names where name.hasPrefix("appmanifest_") && name.hasSuffix(".acf") {
                if let id = UInt32(name.dropFirst("appmanifest_".count).dropLast(".acf".count)) { ids.insert(id) }
            }
        }
        return ids
    }

    /// Steam's UI language ("english"), which the Windows client keeps in HKCU\Software\Valve\Steam\language.
    public func uiLanguage() -> String? {
        guard let text = try? String(contentsOf: registryVDF, encoding: .utf8),
              let nodes = try? KeyValues.parse(text) else { return nil }
        return nodes.node(at: ["Registry", "HKCU", "Software", "Valve", "Steam", "language"])?.stringValue
    }

    /// The `localconfig.vdf` of the account Steam logged in last (`loginusers.vdf`'s `MostRecent`), else of the only
    /// account in `userdata`; nil otherwise. The account ID makes the path and nothing else: never log or show it.
    /// Only `MostRecent` is read from `loginusers.vdf`, which also holds account names.
    public var localConfig: URL? {
        let userdata = root.appending(path: "userdata", directoryHint: .isDirectory)
        var account: String?
        if let text = try? String(contentsOf: root.appending(path: "config/loginusers.vdf"), encoding: .utf8),
           let users = (try? KeyValues.parse(text))?.node(at: ["users"]),
           let recent = users.children.first(where: { $0.children.node(at: ["MostRecent"])?.stringValue == "1" }),
           let id = UInt64(KeyValues.unescape(recent.key)) {
            account = String(id & 0xFFFF_FFFF)
        } else {
            let folders = ((try? FileManager.default.contentsOfDirectory(atPath: userdata.path(percentEncoded: false))) ?? [])
                .filter { UInt32($0) != nil }
            if folders.count == 1 { account = folders[0] }
        }
        return account.map { userdata.appending(path: "\($0)/config/localconfig.vdf") }
    }

    static func samePath(_ a: URL, _ b: URL) -> Bool {
        func normal(_ url: URL) -> String {
            var path = url.standardizedFileURL.resolvingSymlinksInPath().path(percentEncoded: false)
            while path.count > 1, path.hasSuffix("/") { path.removeLast() }
            return path
        }
        return normal(a) == normal(b)
    }
}

public enum SteamPlayError: Error, Equatable, CustomStringConvertible {
    case steamNotInstalled
    case steamRunning
    case quitTimedOut
    case configUnreadable(String)
    case verificationFailed(String)
    case planDropsMacGames(Int)

    public var description: String {
        switch self {
        case .steamNotInstalled: "Steam isn't installed in ~/Library/Application Support/Steam."
        case .steamRunning: "Steam is running. Quit Steam to apply changes."
        case .quitTimedOut: "Steam didn't quit within 30 seconds. Quit it yourself, then try again."
        case .configUnreadable(let detail): "Steam's settings file couldn't be read, so MacNeutron didn't change it (\(detail))."
        case .verificationFailed(let problem): "Steam Play mode didn't start correctly, so it was turned off again: \(problem)"
        case .planDropsMacGames(let count):
            "MacNeutron didn't update Steam: the new plan would stop protecting \(count) Mac \(count == 1 ? "game" : "games"). Steam's app list may be unreadable right now."
        }
    }
}

/// Starting and stopping Steam. A protocol so tests never touch the real client.
public protocol SteamControlling: Sendable {
    func isRunning() -> Bool
    func quit(timeout: Duration) async throws
    func launch() throws
}

public struct SteamProcess: SteamControlling {
    public init() {}

    public func isRunning() -> Bool {
        (try? SystemProcessRunner().run(URL(filePath: "/usr/bin/pgrep"), ["-x", "steam_osx"],
                                        environment: [:], output: URL(filePath: "/dev/null"))) == 0
    }

    /// Asks Steam to exit through its own URL handler, then waits for the process to go away.
    public func quit(timeout: Duration) async throws {
        guard isRunning() else { return }
        _ = try SystemProcessRunner().run(URL(filePath: "/usr/bin/open"), ["steam://exit"],
                                          environment: ProcessInfo.processInfo.environment, output: nil)
        let deadline = ContinuousClock.now + timeout
        while isRunning() {
            guard ContinuousClock.now < deadline else { throw SteamPlayError.quitTimedOut }
            try await Task.sleep(for: .milliseconds(500))
        }
    }

    public func launch() throws {
        _ = try SystemProcessRunner().run(URL(filePath: "/usr/bin/open"), ["-a", "Steam"],
                                          environment: ProcessInfo.processInfo.environment, output: nil)
    }
}
