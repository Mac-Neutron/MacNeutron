import Darwin
import Foundation

/// The player's data a fresh prefix takes from the Rosetta-era prefix it replaced (renamed `pfx.rosetta…`): games keep
/// settings and local saves in the user's folders and in `HKCU\Software\<Vendor>`. Reads the old prefix only.
enum PlayerData {
    /// Per user folder: what is copied, and Wine's own folders inside it, which aren't.
    static let folders = ["AppData/Local", "AppData/LocalLow", "AppData/Roaming", "Documents", "Saved Games"]
    static let skipped: Set = ["AppData/Local/Temp", "AppData/Local/Microsoft", "AppData/Roaming/Microsoft"]
    /// `HKCU\Software\<Vendor>` keys that are Wine's, Windows' or Steam's, not a game's.
    static let systemVendors: Set = ["wine", "microsoft", "classes", "policies", "valve"]

    /// What a carry did: files cloned, registry sections appended, and items left behind (one that couldn't be read or
    /// copied, a link, or what's under a link in the way).
    struct Carried { var files = 0, keys = 0, failed = 0 }

    /// Appends the game-owned `user.reg` sections the new prefix has no section of, then clones the user folders' files
    /// it doesn't have yet. A bad item is skipped and counted, never ends the carry. Call only while no wineserver runs:
    /// it reads `user.reg` when it starts and rewrites it.
    static func carry(from old: URL, to new: URL) -> Carried {
        var result = Carried()
        do { result.keys = try carryRegistry(from: old.appending(path: "user.reg"), to: new.appending(path: "user.reg")) }
        catch { result.failed += 1 }
        let (oldUsers, skippedUsers) = users(in: old), newUsers = users(in: new).names
        result.failed += skippedUsers
        // wineboot names the one user after $USER, and Wine reads only that one: every old user goes there, whatever
        // the names. The old user of that name (the one Wine read) goes first, then by name: the first wins a file two
        // have.
        // Steam's own `steamuser` (its cloud files, handed back beside the new profile) is never the one Wine reads.
        let profiles = newUsers.count > 1 ? newUsers.filter { $0.lowercased() != "steamuser" } : newUsers
        let live = profiles.count == 1 ? profiles[0] : nil
        for user in oldUsers.filter({ $0 == live }) + oldUsers.filter({ $0 != live }) {
            let target = live ?? user
            for folder in folders {
                switch kind(old, "drive_c/users/\(user)/\(folder)") {
                case .missing: continue
                // Wine links Documents (and the like) to the Mac's own folder: that data is on the Mac already.
                case .other: if !isLink(old, "drive_c/users/\(user)/\(folder)") { result.failed += 1 }
                case .directory:
                    let destination = "drive_c/users/\(target)/\(folder)"
                    guard makeDirectory(new, destination) else { result.failed += 1; continue }
                    copyTree(old.appending(path: "drive_c/users/\(user)/\(folder)"), to: new.appending(path: destination),
                             relative: folder, &result)
                }
            }
        }
        return result
    }

    /// The real folders in `drive_c/users`, `Public` aside, and how many entries there aren't (links, unreadable).
    private static func users(in prefix: URL) -> (names: [String], skipped: Int) {
        let dir = prefix.appending(path: "drive_c/users")
        switch kind(prefix, "drive_c/users") {
        case .missing: return ([], 0)
        case .other: return ([], 1)
        case .directory: break
        }
        guard let all = try? FileManager.default.contentsOfDirectory(atPath: dir.path(percentEncoded: false)) else {
            return ([], 1)
        }
        let names = all.filter { $0 != "Public" }
        let real = names.filter { kind(dir, $0) == .directory }.sorted()
        return (real, names.count - real.count)
    }

    private enum Kind { case directory, missing, other }

    /// What `base/relative` is, looked at with lstat component by component: a link anywhere on the way is `other`.
    /// Wine links `Documents` and others to the Mac's folders, which are never read through (the data is already
    /// there) nor written through.
    private static func kind(_ base: URL, _ relative: String) -> Kind {
        var url = base
        for part in relative.split(separator: "/") {
            url.append(path: String(part))
            var info = stat()
            guard lstat(url.path(percentEncoded: false), &info) == 0 else { return errno == ENOENT ? .missing : .other }
            guard info.st_mode & S_IFMT == S_IFDIR else { return .other }
        }
        return .directory
    }

    /// True when `base/relative` itself is a symbolic link and every folder above it is real.
    private static func isLink(_ base: URL, _ relative: String) -> Bool {
        let parent = (relative as NSString).deletingLastPathComponent
        guard parent.isEmpty || kind(base, parent) == .directory else { return false }
        var info = stat()
        return lstat(base.appending(path: relative).path(percentEncoded: false), &info) == 0 && info.st_mode & S_IFMT == S_IFLNK
    }

    /// True when `base/relative` is a real directory now, made where missing; false when a component is a link or a
    /// file, which is never followed or replaced.
    private static func makeDirectory(_ base: URL, _ relative: String) -> Bool {
        var url = base
        for part in relative.split(separator: "/") {
            url.append(path: String(part))
            var info = stat()
            if lstat(url.path(percentEncoded: false), &info) == 0 {
                guard info.st_mode & S_IFMT == S_IFDIR else { return false }
            } else if mkdir(url.path(percentEncoded: false), 0o755) != 0 {
                return false
            }
        }
        return true
    }

    /// The name each file is copied under, in its folder, before it is renamed into place: a carry killed mid-copy (a
    /// byte copy, off APFS) leaves its part there, never under the real name, and the retry's copy replaces it.
    static let tempName = ".macneutron-carry"

    /// Clones every regular file under `source` that `destination` lacks (both real directories) and counts.
    private static func copyTree(_ source: URL, to destination: URL, relative: String, _ result: inout Carried) {
        guard let names = try? FileManager.default.contentsOfDirectory(atPath: source.path(percentEncoded: false)) else {
            result.failed += 1
            return
        }
        for name in names {
            let from = source.appending(path: name), to = destination.appending(path: name), path = "\(relative)/\(name)"
            var info = stat()
            guard lstat(from.path(percentEncoded: false), &info) == 0 else { result.failed += 1; continue }  // vanished
            switch info.st_mode & S_IFMT {
            case S_IFDIR:
                guard !skipped.contains(path) else { continue }
                guard makeDirectory(destination, name) else { result.failed += 1; continue }
                copyTree(from, to: to, relative: path, &result)
            case S_IFREG:
                var existing = stat()
                guard lstat(to.path(percentEncoded: false), &existing) != 0 else { continue }  // never overwritten
                let temp = destination.appending(path: tempName).path(percentEncoded: false)
                unlink(temp)  // a killed carry's part-copy
                // COPYFILE_CLONE: an APFS clone, else a copy; it includes COPYFILE_EXCL. RENAME_EXCL never replaces a
                // file; a volume without it (ENOTSUP) gets a plain rename: under the prefix lock with no wineserver,
                // nothing else writes there since the lstat above.
                guard copyfile(from.path(percentEncoded: false), temp, nil, copyfile_flags_t(COPYFILE_CLONE)) == 0,
                      renamex_np(temp, to.path(percentEncoded: false), UInt32(RENAME_EXCL)) == 0
                        || (errno == ENOTSUP && rename(temp, to.path(percentEncoded: false)) == 0) else {
                    unlink(temp)
                    result.failed += 1
                    continue
                }
                result.files += 1
            default: result.failed += 1  // a link, never followed, or a special file
            }
        }
    }

    /// Appends the old `user.reg`'s sections under `Software\<Vendor>` (a game's vendor) that the new one has no
    /// section of, verbatim; returns how many. The file is replaced atomically.
    private static func carryRegistry(from old: URL, to new: URL) throws -> Int {
        guard FileManager.default.fileExists(atPath: old.path(percentEncoded: false)) else { return 0 }
        let oldText = try String(contentsOf: old, encoding: .utf8)
        var newText = try String(contentsOf: new, encoding: .utf8)
        let taken = Set(sections(of: newText).map { $0.name.lowercased() })
        let carried = sections(of: oldText).filter { section in
            let path = section.name.components(separatedBy: #"\\"#)
            return path.count > 1 && path[0].lowercased() == "software" && !systemVendors.contains(path[1].lowercased())
                && !taken.contains(section.name.lowercased())
        }
        guard !carried.isEmpty else { return 0 }
        if !newText.hasSuffix("\n") { newText += "\n" }
        for section in carried { newText += "\n" + section.text + "\n" }
        try newText.write(to: new, atomically: true, encoding: .utf8)
        return carried.count
    }

    /// A Wine registry file's sections: each `[name] time` line with the lines up to the next one, trailing blank
    /// lines dropped. Values never start a line with `[` (they start with `"` or `@`, continuations with spaces).
    private static func sections(of text: String) -> [(name: String, text: String)] {
        var result: [(name: String, lines: [Substring])] = []
        for line in text.split(separator: "\n", omittingEmptySubsequences: false) {
            if line.hasPrefix("["), let close = line.lastIndex(of: "]") {
                result.append((String(line[line.index(after: line.startIndex)..<close]), [line]))
            } else if !result.isEmpty {
                result[result.count - 1].lines.append(line)
            }
        }
        return result.map { section in
            var lines = section.lines
            while lines.last?.trimmingCharacters(in: .whitespaces).isEmpty == true { lines.removeLast() }
            return (section.name, lines.joined(separator: "\n"))
        }
    }
}
