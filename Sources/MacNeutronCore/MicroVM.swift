import Foundation

/// The microVM every game runs in (eac-vm; docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md): one
/// folder holding macneutron-vm, run-guest.sh, the guest's run-game.sh, the images every game shares (images/) and each
/// game's own state disk (state/<appid>.img, run-guest.sh's APFS clone of images/state-template.img on first use).
/// MACNEUTRON_VM_DIR names the folder, else MacNeutron's own; MACNEUTRON_VM=0 runs a game on macOS as before.
public struct MicroVM: Sendable {
    public let root: URL

    public static let standardRoot = FileManager.default.homeDirectoryForCurrentUser
        .appending(path: "Library/Application Support/MacNeutron/vm", directoryHint: .isDirectory)

    /// The game's verbs only (getcompatpath and getnativepath answer about the macOS prefix), and only with
    /// macneutron-vm in the folder.
    static func find(root: URL, environment: [String: String], verb: Verb) -> MicroVM? {
        guard verb == .run || verb == .waitforexitandrun, environment["MACNEUTRON_VM"] != "0" else { return nil }
        let folder = environment["MACNEUTRON_VM_DIR"].map { URL(filePath: $0, directoryHint: .isDirectory) } ?? root
        let vm = folder.appending(path: "macneutron-vm").path(percentEncoded: false)
        return FileManager.default.isExecutableFile(atPath: vm) ? MicroVM(root: folder) : nil
    }

    var runGuest: URL { root.appending(path: "run-guest.sh") }
    var gameScript: URL { root.appending(path: "run-game.sh") }
    func state(appID: String) -> URL { root.appending(path: "state/\(appID).img") }

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
