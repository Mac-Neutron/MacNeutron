import Foundation

/// The microVM every game runs in (eac-vm; docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md): one
/// folder holding macneutron-vm, run-guest.sh, the guest's run-game.sh, the images every game shares (images/) and each
/// game's own state disk (state/<appid>.img, run-guest.sh's APFS clone of images/state-template.img on first use), TPM
/// (state/<appid>.tpm, sealed under a key in the login keychain) and share (runs/<appid>: the run's script, results and
/// console), so games run side by side.
/// MACNEUTRON_VM_DIR names the folder, else MacNeutron's own; MACNEUTRON_VM=0 runs a game on macOS as before. A game in
/// a Steam library runs in the VM or not at all: the launcher refuses it until the VM is ready.
public struct MicroVM: Sendable {
    public let root: URL

    public static let standardRoot = FileManager.default.homeDirectoryForCurrentUser
        .appending(path: "Library/Application Support/MacNeutron/vm", directoryHint: .isDirectory)

    /// The game's verbs go to the VM (getcompatpath and getnativepath answer about the macOS prefix), unless
    /// MACNEUTRON_VM=0.
    static func wanted(environment: [String: String], verb: Verb) -> Bool {
        (verb == .run || verb == .waitforexitandrun) && environment["MACNEUTRON_VM"] != "0"
    }

    /// The VM, once it's ready: macneutron-vm and a complete image set in the folder, no tools swap under way.
    static func find(root: URL, environment: [String: String], verb: Verb) -> MicroVM? {
        guard wanted(environment: environment, verb: verb) else { return nil }
        let folder = environment["MACNEUTRON_VM_DIR"].map { URL(filePath: $0, directoryHint: .isDirectory) } ?? root
        let vm = folder.appending(path: "macneutron-vm").path(percentEncoded: false)
        let installing = folder.appending(path: ".installing").path(percentEncoded: false)  // VMToolsInstaller's swap
        return FileManager.default.isExecutableFile(atPath: vm) && imagesComplete(folder)
            && !FileManager.default.fileExists(atPath: installing) ? MicroVM(root: folder) : nil
    }

    /// The setup (`macneutron vm-setup`) writes images/manifest.sha256 last, once the whole set is in place; the image
    /// build in eac-vm writes `dev` there.
    public static func imagesComplete(_ root: URL) -> Bool {
        FileManager.default.fileExists(atPath: root.appending(path: "images/manifest.sha256").path(percentEncoded: false))
    }

    var runGuest: URL { root.appending(path: "run-guest.sh") }
    var gameScript: URL { root.appending(path: "run-game.sh") }
    func state(appID: String) -> URL { root.appending(path: "state/\(appID).img") }
    func share(appID: String) -> URL { root.appending(path: "runs/\(appID)") }

    /// A game under a Steam library's steamapps/common: that folder (the guest's read-only /mnt/games) and the game's
    /// path in the guest. Nil for a game anywhere else.
    static func guestPath(_ target: String) -> (library: String, guest: String)? {
        guard let common = target.range(of: "/steamapps/common/") else { return nil }
        return (String(target[..<common.upperBound].dropLast()), "/mnt/games/" + target[common.upperBound...])
    }

    static func quoted(_ word: String) -> String { "'" + word.replacingOccurrences(of: "'", with: #"'\''"#) + "'" }

    /// The launch options the guest reads (run-game.sh), as the launcher's own environment has them.
    static let passed = ["MACNEUTRON_DLSS", "MACNEUTRON_XESS", "DXMT_ENABLE_NVEXT", "MACNEUTRON_LOG"]

    /// The guest's script for one launch: its facts as shell assignments, then run-game.sh.
    func script(verb: Verb, appID: String, guest: String, arguments: [String],
                environment: [String: String]) throws -> String {
        let folder = (guest as NSString).deletingLastPathComponent
        var lines = ["VERB=\(Self.quoted(verb.rawValue))", "APPID=\(Self.quoted(appID))",
                     "GAME_DIR=\(Self.quoted(folder))",
                     "EXE=\(Self.quoted("Z:" + guest.replacingOccurrences(of: "/", with: "\\")))",
                     (["set --"] + arguments.map(Self.quoted)).joined(separator: " ")]
        for key in Self.passed { if let value = environment[key] { lines.append("\(key)=\(Self.quoted(value))") } }
        return lines.joined(separator: "\n") + "\n" + (try String(contentsOf: gameScript, encoding: .utf8))
    }
}
