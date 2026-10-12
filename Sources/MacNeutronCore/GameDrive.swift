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
           path.split(separator: "/").last == "steamapps" {
            let parent = (path as NSString).deletingLastPathComponent
            if access(parent, W_OK) == 0, let own = device(path), own == device(parent) { return parent }
        }
        notes.append("Proton: Error: unable to use parent for game drive, path \(entry)")
        return entry
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
            // Proton aborts on this (readlink raises); a player's own file is never deleted.
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
