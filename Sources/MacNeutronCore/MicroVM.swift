import CoreGraphics
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
    /// Native DXMT's own for the game, forwarded (MACNEUTRON_VM_D3D=forward): its shader cache and recorded pipelines.
    func dxmtState(appID: String) -> URL { root.appending(path: "state/\(appID).dxmt") }
    /// The native replayer of recorded pipelines (eac-vm/dxmt-native/build.sh), beside native DXMT.
    var dxmtReplay: URL { root.appending(path: "dxmt/dxmt-replay") }
    /// Native DXMT's translator key (`dxmt-translator`, eac-vm/dxmt-native/build.sh's), which keys its recordings.
    var dxmtTranslator: String? {
        (try? String(contentsOf: root.appending(path: "dxmt-translator"), encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// A game under a Steam library's steamapps/common: that folder (the guest's read-only /mnt/games) and the game's
    /// path in the guest. Nil for a game anywhere else.
    static func guestPath(_ target: String) -> (library: String, guest: String)? {
        guard let common = target.range(of: "/steamapps/common/") else { return nil }
        return (String(target[..<common.upperBound].dropLast()), "/mnt/games/" + target[common.upperBound...])
    }

    static func quoted(_ word: String) -> String { "'" + word.replacingOccurrences(of: "'", with: #"'\''"#) + "'" }

    /// The launch options the guest reads (run-game.sh), as the launcher's own environment has them.
    static let passed = ["MACNEUTRON_DLSS", "MACNEUTRON_XESS", "DXMT_ENABLE_NVEXT", "MACNEUTRON_LOG", "MN_SCREEN",
                         "MACNEUTRON_VM_PROFILE", "DXMT_STATS", "MACNEUTRON_VM_D3D", "MACNEUTRON_VM_D3D32",
                         "MACNEUTRON_VM_D3D_TRACE", "DXMT_LOG_LEVEL", "MACNEUTRON_VM_WOW64", "PROTON_USE_WOW64",
                         "MTLD3D_CONFIG", "WINEDEBUG"]
    /// What a performance build's guest doesn't get: its stats and profiler are compiled out or off.
    static let notPassedToPerformance: Set = ["MACNEUTRON_VM_PROFILE", "DXMT_STATS"]

    /// The main display's size in pixels (its current mode's, so a Retina display's backing pixels), or nil.
    public static func mainDisplay() -> (width: Int, height: Int)? {
        guard let mode = CGDisplayCopyDisplayMode(CGMainDisplayID()), mode.pixelWidth > 0, mode.pixelHeight > 0 else {
            return nil
        }
        return (mode.pixelWidth, mode.pixelHeight)
    }

    /// "WxH", each side 1 to 16384 (Xvfb's screen; MN_SCREEN from a launch option).
    static func isScreenSize(_ text: String) -> Bool {
        let sides = text.split(separator: "x", omittingEmptySubsequences: false)
        return sides.count == 2 && sides.allSatisfy { side in
            side.allSatisfy(\.isASCII) && Int(side).map { (1...16384).contains($0) } == true
        }
    }

    /// The guest's script for one launch: its facts as shell assignments, then run-game.sh. A performance build's
    /// (X.Y.Zp) says MACNEUTRON_PROFILE=p, which keeps run-game.sh quiet unless MACNEUTRON_LOG is set.
    func script(verb: Verb, appID: String, guest: String, arguments: [String],
                environment: [String: String], performance: Bool = false) throws -> String {
        let folder = (guest as NSString).deletingLastPathComponent
        var lines = ["VERB=\(Self.quoted(verb.rawValue))", "APPID=\(Self.quoted(appID))",
                     "GAME_DIR=\(Self.quoted(folder))",
                     "EXE=\(Self.quoted("Z:" + guest.replacingOccurrences(of: "/", with: "\\")))",
                     (["set --"] + arguments.map(Self.quoted)).joined(separator: " ")]
        if performance { lines.append("MACNEUTRON_PROFILE=\(Self.quoted("p"))") }
        for key in Self.passed where !(performance && Self.notPassedToPerformance.contains(key)) {
            if let value = environment[key] { lines.append("\(key)=\(Self.quoted(value))") }
        }
        return lines.joined(separator: "\n") + "\n" + (try String(contentsOf: gameScript, encoding: .utf8))
    }
}
