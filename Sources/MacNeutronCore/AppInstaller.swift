import Darwin
import Foundation

public enum AppInstallerError: Error, Equatable, CustomStringConvertible {
    /// The target's folder (no trailing "/").
    case notWritable(String)
    case stillRunning
    /// ditto's exit status.
    case copyFailed(Int32)

    public var description: String {
        switch self {
        case .notWritable(let folder):
            "Your account can't write to \(folder): drag MacNeutron.app there yourself, or use an administrator account."
        case .stillRunning: "MacNeutron didn't quit within 30 seconds: quit it, then open Install MacNeutron again."
        case .copyFailed(let status): "MacNeutron.app couldn't be copied into Applications (ditto exited \(status))."
        }
    }
}

/// Install MacNeutron's work (spec 2026-10-25-macneutron-installer-design §3.2, steps 2-5): the bundled app verified,
/// MacNeutron quit, a copy beside the target without the downloaded installer's quarantine, swapped in. The installer
/// app (Sources/MacNeutronInstaller) is its front: the confirmation, the alerts, quitting by bundle id, the reopening.
public enum AppInstaller {
    public static let target = URL(filePath: "/Applications/MacNeutron.app", directoryHint: .isDirectory)

    /// Every failure before the swap leaves `target` as it was, and a failed swap puts it back (`Updater.swap`).
    /// `quit` asks every running MacNeutron to quit and says whether none is left.
    public static func install(_ bundled: URL, into target: URL, verify: (URL) throws -> Void, quit: () -> Bool) throws {
        try verify(bundled)
        let fm = FileManager.default
        let folder = target.deletingLastPathComponent()
        guard fm.isWritableFile(atPath: path(folder)) else { throw AppInstallerError.notWritable(path(folder)) }
        guard quit() else { throw AppInstallerError.stillRunning }
        let staging = folder.appending(path: ".MacNeutron.app.installing", directoryHint: .isDirectory)
        let aside = folder.appending(path: ".MacNeutron.app.old", directoryHint: .isDirectory)
        // A crash between Updater.swap's renames left the only app aside: it goes back first (Updater.discard keeps
        // it too).
        if !fm.fileExists(atPath: path(target)), fm.fileExists(atPath: path(aside)) {
            try fm.moveItem(at: aside, to: target)
        }
        for leftover in [staging, aside] { try? fm.removeItem(at: leftover) }  // an interrupted install's
        defer { try? fm.removeItem(at: staging) }
        let status = try SystemProcessRunner().run(URL(filePath: "/usr/bin/ditto"), [path(bundled), path(staging)],
                                                   environment: [:], output: nil)
        guard status == 0 else { throw AppInstallerError.copyFailed(status) }
        try stripQuarantine(staging)  // verified above: the downloaded-from-the-Internet prompt is redundant
        if fm.fileExists(atPath: path(target)) {
            try Updater.swap(staging, into: target, aside: aside)
            try? fm.removeItem(at: aside)
        } else {
            try fm.moveItem(at: staging, to: target)
        }
    }

    /// `com.apple.quarantine` off the bundle and everything in it; a file without it is fine.
    static func stripQuarantine(_ bundle: URL) throws {
        let root = path(bundle)
        let inside = (FileManager.default.enumerator(atPath: root)?.allObjects as? [String]) ?? []
        for file in [root] + inside.map({ root + "/" + $0 }) {
            if removexattr(file, "com.apple.quarantine", XATTR_NOFOLLOW) != 0, errno != ENOATTR {
                throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
            }
        }
    }

    /// The path without a trailing "/", for ditto and the messages.
    private static func path(_ url: URL) -> String {
        let p = url.path(percentEncoded: false)
        return p.hasSuffix("/") ? String(p.dropLast()) : p
    }
}
