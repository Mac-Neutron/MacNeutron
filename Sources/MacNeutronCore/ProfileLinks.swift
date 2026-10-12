import Darwin
import Foundation

/// What `ProfileLinks.apply` did: notes for launcher.log, and how many links it left as real folders because files in
/// them differ from the profile's (spec D2).
public struct ProfileLinkReport: Equatable, Sendable {
    public var notes: [String] = []
    public var conflicts = 0
}

/// The links Steam's Auto-Cloud roots resolve through (spec §4.2, after Proton's `migrate_user_paths`). Steam names the
/// profile `steamuser` and its folders by their XP names; Wine names it after `$USER` with Vista names.
public enum ProfileLinks {
    /// Inside the profile: (path, relative target).
    static let xpLinks = [("Local Settings/Application Data", "../AppData/Local"), ("Application Data", "AppData/Roaming"),
                          ("My Documents", "Documents")]

    /// Makes the links in `prefix`, the profile's own first, so a merge of `steamuser` lands in the Vista folders. A real
    /// folder where a link goes (Steam made it) becomes the link only when `merge`, the volume can swap, and every file
    /// in it is then present and equal at the link's target; otherwise it stays as it is. Nothing is ever deleted.
    public static func apply(prefix: URL, user: String, merge: Bool = true,
                             canSwap: (URL) -> Bool = ProfileLinks.isAPFS) -> ProfileLinkReport {
        var report = ProfileLinkReport()
        let users = prefix.appending(path: "drive_c/users", directoryHint: .isDirectory)
        let profile = users.appending(path: user, directoryHint: .isDirectory)
        guard kind(profile) == .directory else {
            report.notes.append("no profile folder users/\(user), so no Steam Cloud links")
            return report
        }
        var links = xpLinks.map { (profile.appending(path: $0.0), "\(user)/\($0.0)", $0.1) }
        // APFS compares names without case: for the Mac user "SteamUser", users/steamuser is the profile itself.
        if user.caseInsensitiveCompare("steamuser") != .orderedSame {
            links.append((users.appending(path: "steamuser"), "steamuser", user))
        }
        for (path, name, target) in links {
            switch kind(path) {
            case .missing:
                try? FileManager.default.createDirectory(at: path.deletingLastPathComponent(), withIntermediateDirectories: true)
                if symlink(target, path.path(percentEncoded: false)) != 0 { report.notes.append("couldn't link \(name)") }
            case .link:
                // Steam's watched files resolve through the link: only a dangling one is replaced. One reaching
                // another folder (the Mac's short name changed, say) is left, or those files would vanish.
                let current = resolved(path)
                if current == nil {
                    if !replaceLink(at: path, with: target) { report.notes.append("couldn't relink \(name)") }
                } else if current != resolved(path.deletingLastPathComponent().appending(path: target)) {
                    report.conflicts += 1
                    report.notes.append("left \(name): it links to another folder, so Steam Cloud won't sync this game here yet")
                }
            case .directory where !merge:
                report.notes.append("left \(name): Steam's files there merge at the game's next launch")
            case .directory where !canSwap(prefix):
                report.notes.append("left \(name): the volume isn't APFS, so its files can't be merged safely")
            case .directory:
                mergeAndSwap(path, name: name, target: target, &report)
            case .file, .other:
                report.notes.append("left \(name): it isn't a folder")
            }
        }
        return report
    }

    /// Clones `folder`'s files into the link's target, checks every one arrived equal, then swaps the link in, the
    /// folder becoming `<name> BACKUP`: one atomic step, so every path Steam watches holds the same bytes throughout.
    private static func mergeAndSwap(_ folder: URL, name: String, target: String, _ report: inout ProfileLinkReport) {
        let fm = FileManager.default
        let destination = folder.deletingLastPathComponent().appending(path: target)  // through the target's links
        var here = stat(), there = stat()
        if stat(folder.path(percentEncoded: false), &here) == 0, stat(destination.path(percentEncoded: false), &there) == 0,
           here.st_dev == there.st_dev, here.st_ino == there.st_ino {
            report.notes.append("left \(name): it is already \(target)")  // never merged into itself
            return
        }
        let files = regularFiles(under: folder)
        let before = compare(files, folder, destination)
        guard before.differing.isEmpty else {
            report.conflicts += 1
            report.notes.append("kept \(name): \(before.differing.count) of its files differ from the profile's"
                                + " (e.g. \(redacted(before.differing[0]))), so Steam Cloud won't sync them yet")
            return
        }
        for path in before.missing {
            let to = destination.appending(path: path)
            do {
                try fm.createDirectory(at: to.deletingLastPathComponent(), withIntermediateDirectories: true)
                try fm.copyItem(at: folder.appending(path: path), to: to)  // an APFS clone, modification time kept
            } catch {
                report.notes.append("left \(name): couldn't copy \(redacted(path))")
                return
            }
        }
        let after = compare(files, folder, destination)
        guard after.missing.isEmpty, after.differing.isEmpty else {
            report.notes.append("left \(name): its files didn't all arrive in the profile")
            return
        }
        let path = folder.path(percentEncoded: false)
        var backup = path + " BACKUP", number = 2
        var info = stat()
        while lstat(backup, &info) == 0 {
            backup = path + " BACKUP \(number)"
            number += 1
        }
        guard symlink(target, backup) == 0 else {
            report.notes.append("left \(name): couldn't make its link")
            return
        }
        guard renamex_np(backup, path, UInt32(RENAME_SWAP)) == 0 else {
            unlink(backup)
            report.notes.append("left \(name): couldn't swap in its link")
            return
        }
        report.notes.append("merged \(name) into \(target) (\(files.count) files); the original is now"
                            + " \(URL(filePath: backup).lastPathComponent)")
    }

    /// Relative paths of the regular files under `folder`. Links in it are skipped, never followed.
    private static func regularFiles(under folder: URL) -> [String] {
        guard let items = FileManager.default.enumerator(atPath: folder.path(percentEncoded: false)) else { return [] }
        var result: [String] = []
        for case let path as String in items where kind(folder.appending(path: path)) == .file { result.append(path) }
        return result.sorted()
    }

    /// `files` (relative to `source`) that `destination` lacks, and those it has with other content.
    private static func compare(_ files: [String], _ source: URL, _ destination: URL) -> (missing: [String], differing: [String]) {
        // ponytail: contentsEqual reads both copies, twice per merge; fine for save folders, hash once if it ever isn't.
        let fm = FileManager.default
        var missing: [String] = [], differing: [String] = []
        for path in files {
            let to = destination.appending(path: path).path(percentEncoded: false)
            if !fm.fileExists(atPath: to) {
                missing.append(path)
            } else if !fm.contentsEqual(atPath: source.appending(path: path).path(percentEncoded: false), andPath: to) {
                differing.append(path)
            }
        }
        return (missing, differing)
    }

    /// `realpath(3)`, or nil when `url` doesn't resolve to anything.
    private static func resolved(_ url: URL) -> String? {
        guard let path = realpath(url.path(percentEncoded: false), nil) else { return nil }
        defer { free(path) }
        return String(cString: path)
    }

    /// A new link renamed over the old one, so the path is never missing.
    private static func replaceLink(at url: URL, with target: String) -> Bool {
        let path = url.path(percentEncoded: false), new = path + ".macneutron-new"
        unlink(new)
        guard symlink(target, new) == 0, rename(new, path) == 0 else {
            unlink(new)
            return false
        }
        return true
    }

    /// Clones and `RENAME_SWAP` need APFS.
    public static func isAPFS(_ url: URL) -> Bool {
        var info = statfs()
        guard statfs(url.path(percentEncoded: false), &info) == 0 else { return false }
        return withUnsafeBytes(of: info.f_fstypename) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) } == "apfs"
    }

    /// Logged paths can hold a SteamID (Palworld's do), and launcher.log ends up in bug reports.
    static func redacted(_ path: String) -> String { path.replacing(/[0-9]{6,}/, with: "<n>") }

    private enum Kind { case missing, link, directory, file, other }

    private static func kind(_ url: URL) -> Kind {
        var info = stat()
        guard lstat(url.path(percentEncoded: false), &info) == 0 else { return errno == ENOENT ? .missing : .other }
        switch info.st_mode & S_IFMT {
        case S_IFLNK: return .link
        case S_IFDIR: return .directory
        case S_IFREG: return .file
        default: return .other
        }
    }
}
