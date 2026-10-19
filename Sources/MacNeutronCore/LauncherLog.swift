import Foundation

/// `~/Library/Logs/MacNeutron`: one line per launch in `launcher.log`, plus opt-in per-game Wine logs.
public struct LauncherLog: Sendable {
    public static let rotateBytes = 1_048_576
    public static let gameLogRotateBytes = 50 * 1_048_576
    public let directory: URL
    private let quiet = QuietFlag()

    public init(directory: URL) { self.directory = directory }

    public static var standard: LauncherLog {
        LauncherLog(directory: FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Logs/MacNeutron", directoryHint: .isDirectory))
    }

    public var launcherLog: URL { directory.appending(path: "launcher.log") }

    /// Creates the log folder and returns `steam-<appid>.log`, first rotating it to `steam-<appid>.log.1` past
    /// `gameLogRotateBytes`. Wine writes the log itself, so it rotates only here, at launch: one long session can
    /// still pass the limit.
    public func gameLog(appID: String) -> URL {
        let fm = FileManager.default
        try? fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let log = directory.appending(path: "steam-\(appID).log")
        _ = try? withFileLock(at: directory.appending(path: "launcher.log.lock")) {  // launches overlap
            if let size = (try? fm.attributesOfItem(atPath: log.path(percentEncoded: false)))?[.size] as? NSNumber,
               size.intValue > Self.gameLogRotateBytes {
                let rotated = directory.appending(path: "steam-\(appID).log.1")
                try? fm.removeItem(at: rotated)
                try? fm.moveItem(at: log, to: rotated)
            }
        }
        return log
    }

    /// A performance build (X.Y.Zp) launched without MACNEUTRON_LOG keeps one line a launch: its summary.
    public func setQuiet(_ on: Bool) { quiet.set(on) }

    /// The launch's summary (or its error): written even when quiet.
    public func summary(_ line: String) { writeLine(line) }

    /// Appends a timestamped line, rotating to `launcher.log.1` past `rotateBytes`.
    /// Never throws: a logging problem must not stop a game from launching.
    /// Launches overlap (Steam's setup run and the game), so rotation and the write happen under a lock beside the log,
    /// and the line goes out in one O_APPEND write.
    public func append(_ line: String) {
        if !quiet.isOn { writeLine(line) }
    }

    private func writeLine(_ line: String) {
        let fm = FileManager.default
        try? fm.createDirectory(at: directory, withIntermediateDirectories: true)
        let path = launcherLog.path(percentEncoded: false)
        let data = Data("\(Date().formatted(.iso8601)) \(line)\n".utf8)
        _ = try? withFileLock(at: directory.appending(path: "launcher.log.lock")) {
            if let size = (try? fm.attributesOfItem(atPath: path))?[.size] as? NSNumber, size.intValue > Self.rotateBytes {
                let rotated = directory.appending(path: "launcher.log.1")
                try? fm.removeItem(at: rotated)
                try? fm.moveItem(at: launcherLog, to: rotated)
            }
            let fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0o644)
            guard fd >= 0 else { return }
            defer { close(fd) }
            data.withUnsafeBytes { _ = write(fd, $0.baseAddress, $0.count) }
        }
    }
}

/// LauncherLog's quiet switch, shared by its copies (a Launcher's log is a let; the switch follows each launch).
final class QuietFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var on = false
    var isOn: Bool { lock.withLock { on } }
    func set(_ value: Bool) { lock.withLock { on = value } }
}
