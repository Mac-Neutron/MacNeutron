import Darwin
import Foundation

/// Proton's S: game drive (proton:299-351): `pfx/dosdevices/s:` links to the game library's parent, so the game sees
/// itself at `S:\steamapps\common\<Game>` (Wine converts a path to its deepest drive). On by default, as in Proton;
/// `PROTON_SET_GAME_DRIVE=0` (or empty) turns it off for a game.
enum GameDrive {
    /// Proton's try_get_game_library_dir and get_validated_steamapps_parent (proton:299-322): the link's target, nil or
    /// "" to remove it. A library that fails validation gives its raw entry, the steamapps folder itself (Proton's
    /// fallback), and Proton's message in `notes`.
    static func target(environment: [String: String], device: (String) -> dev_t? = GameDrive.device,
                       notes: inout [String]) -> String? {
        guard let install = environment["STEAM_COMPAT_INSTALL_PATH"],
              let libraries = environment["STEAM_COMPAT_LIBRARY_PATHS"] else { return nil }
        // Proton's `entry in install_path`: a substring anywhere, the first match only, an empty piece matching every
        // path (spelled out: Foundation's contains("") is false).
        guard let entry = libraries.split(separator: ":", omittingEmptySubsequences: false).map(String.init)
            .first(where: { $0.isEmpty || install.contains($0) }) else { return nil }
        var isFolder: ObjCBool = false
        if let path = realPath(entry), FileManager.default.fileExists(atPath: path, isDirectory: &isFolder), isFolder.boolValue,
           pythonName(entry) == "steamapps" {
            let parent = (path as NSString).deletingLastPathComponent
            if access(parent, W_OK) == 0, let own = device(path), own == device(parent) { return parent }
        }
        notes.append("Proton: Error: unable to use parent for game drive, path \(entry)")
        return entry
    }

    /// The last part of Python's os.path.realpath(path) (posixpath, not strict), which Proton checks: a part that isn't
    /// a link keeps its spelling, a link is replaced by its text, "." goes and ".." drops the part before it. Darwin's
    /// realpath(3) gives the on-disk case instead, so Mac Steam's ".../steamapps" on a legacy on-disk "SteamApps"
    /// folder would fail the check.
    static func pythonName(_ path: String) -> String {
        var resolved: [Substring] = []
        var pending = path.split(separator: "/")[...]
        var hops = 0
        while let part = pending.popFirst() {
            if part == "." { continue }
            if part == ".." { _ = resolved.popLast(); continue }
            let candidate = "/" + (resolved + [part]).joined(separator: "/")
            if hops < 40, let text = try? FileManager.default.destinationOfSymbolicLink(atPath: candidate) {  // ELOOP
                hops += 1
                if text.hasPrefix("/") { resolved = [] }
                pending = ArraySlice(text.split(separator: "/") + pending)
            } else {
                resolved.append(part)
            }
        }
        return resolved.last.map(String.init) ?? "/"
    }

    /// stat(2)'s st_dev.
    static func device(_ path: String) -> dev_t? {
        var info = stat()
        return stat(path, &info) == 0 ? info.st_dev : nil
    }

    /// Proton's setup_dir_drive for `s:` (proton:330-345). Returns notes for launcher.log.
    static func apply(prefix: URL, environment: [String: String]) -> [String] {
        var notes: [String] = []
        // Proton works the target out whether or not the drive is on (proton:348), so its message is logged either way.
        let target = Self.target(environment: environment, notes: &notes)
        let on = environment["PROTON_SET_GAME_DRIVE"].map { !$0.isEmpty && $0 != "0" } ?? true
        let link = prefix.appending(path: "dosdevices/s:")
        var info = stat()
        let exists = lstat(link.path(percentEncoded: false), &info) == 0
        if exists, info.st_mode & S_IFMT != S_IFLNK {
            // Proton raises here with the drive on (readlink) and removes a regular file with it off (D-28); a player's
            // own file is never deleted.
            notes.append("game drive: dosdevices/s: is not a link, left alone")
            return notes
        }
        guard on, let target, !target.isEmpty else {
            if exists { unlink(link.path(percentEncoded: false)) }
            return notes
        }
        if exists, (try? FileManager.default.destinationOfSymbolicLink(atPath: link.path(percentEncoded: false))) == target {
            return notes
        }
        try? FileManager.default.createDirectory(at: link.deletingLastPathComponent(), withIntermediateDirectories: true)
        if !ProfileLinks.replaceLink(at: link, with: target) {
            notes.append("game drive: couldn't link dosdevices/s: to \(target)")
        }
        return notes
    }
}
