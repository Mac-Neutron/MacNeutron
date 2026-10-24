import Darwin
import Foundation

/// A game Steam uninstalled loses its VM drive, as Linux Steam deletes its Proton prefix (spec
/// 2026-10-24-macneutron-install-scripts-design §4.9). The drive is the part of the prefix MacNeutron owns; the Mac
/// prefix (compatdata, Mac Steam's, which holds the saves) stays for "Leftover data" (OrphanPrefixes).
/// ponytail: the TPM's 32-byte key stays in the login keychain: it's macneutron-vm's item, and deleting it from the app
/// could raise a keychain prompt.
public enum UninstalledDrives {
    /// Deletes the drive (state/<id>.img, .tpm/, .dxmt/; runs/<id>/, runs/<id>.lock) of every app Steam lists nowhere:
    /// in no library's `apps` list and with no app manifest. Only while Steam still lists the library the game last ran
    /// from (the launcher's runs/<id>/library): a reset list (Steam reinstalled) names none of a drive's games, and a
    /// drive without the record stays. Nothing when libraryfolders.vdf can't be read, and never a drive whose VM runs
    /// (run-guest.sh holds its run lock). Returns one launcher.log line per drive deleted.
    public static func remove(vm: URL, steam: SteamLocation) -> [String] {
        guard let listed = steam.listedAppIDs() else { return [] }
        let installed = steam.installedAppIDs(), libraries = steam.libraries()
        let fm = FileManager.default
        let state = vm.appending(path: "state", directoryHint: .isDirectory)
        let runs = vm.appending(path: "runs", directoryHint: .isDirectory)
        var lines: [String] = []
        for name in ((try? fm.contentsOfDirectory(atPath: state.path(percentEncoded: false))) ?? []).sorted() {
            // A Steam app's drive only: not a test's (fonts.img), nor 0.img.
            guard name.hasSuffix(".img"), let id = UInt32(name.dropLast(4)), "\(id).img" == name, id != 0,
                  id < 0x8000_0000, !listed.contains(id), !installed.contains(id),
                  let ranFrom = try? String(contentsOf: runs.appending(path: "\(id)/library"), encoding: .utf8),
                  libraries.contains(where: { SteamLocation.samePath($0, URL(filePath: ranFrom, directoryHint: .isDirectory)) })
            else { continue }
            try? fm.createDirectory(at: runs, withIntermediateDirectories: true)
            let lock = open(runs.appending(path: "\(id).lock").path(percentEncoded: false),
                            O_RDONLY | O_CREAT | O_EXLOCK | O_NONBLOCK, 0o644)
            guard lock >= 0 else { continue }  // its VM runs: the next round
            defer { close(lock) }
            let image = state.appending(path: name)
            guard fm.fileExists(atPath: image.path(percentEncoded: false)) else { continue }  // a concurrent refresh's
            let folders = [state.appending(path: "\(id).tpm"), state.appending(path: "\(id).dxmt"),
                           runs.appending(path: "\(id)")]
            let bytes = Int64((try? image.resourceValues(forKeys: [.totalFileAllocatedSizeKey]))?.totalFileAllocatedSize ?? 0)
                + folders.reduce(0) { $0 + OrphanPrefixes.size(of: $1) }
            // The drive first: one that can't go keeps its folders, the library record among them, for the next round.
            try? fm.removeItem(at: image)
            guard !fm.fileExists(atPath: image.path(percentEncoded: false)) else { continue }
            for item in folders + [runs.appending(path: "\(id).lock")] { try? fm.removeItem(at: item) }
            lines.append("deleted the game VM drive of app \(id): Steam uninstalled it "
                         + "(\(ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)))")
        }
        return lines
    }
}
