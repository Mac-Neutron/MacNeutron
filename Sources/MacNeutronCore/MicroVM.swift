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
        return isReady(folder) ? MicroVM(root: folder) : nil
    }

    /// A launch would run in the folder's VM: macneutron-vm and a complete image set there, no tools swap under way.
    public static func isReady(_ folder: URL) -> Bool {
        let vm = folder.appending(path: "macneutron-vm").path(percentEncoded: false)
        let installing = folder.appending(path: ".installing").path(percentEncoded: false)  // VMToolsInstaller's swap
        return FileManager.default.isExecutableFile(atPath: vm) && imagesComplete(folder)
            && !FileManager.default.fileExists(atPath: installing)
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
                         "MTLD3D_CONFIG", "WINEDEBUG", "MN_MAC_HOME", "MN_INSTALL_TIMEOUT", "PROTON_EAC_RUNTIME",
                         "MN_CRASH_REPORTERS"]
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

    /// The guest's script for one launch: its facts as shell assignments, the game's install scripts
    /// (InstallScripts.shell), then run-game.sh. A performance build's (X.Y.Zp) says MACNEUTRON_PROFILE=p, which keeps
    /// run-game.sh quiet unless MACNEUTRON_LOG is set.
    func script(verb: Verb, appID: String, guest: String, arguments: [String],
                environment: [String: String], performance: Bool = false, installScript: String = "") throws -> String {
        let folder = (guest as NSString).deletingLastPathComponent
        var lines = ["VERB=\(Self.quoted(verb.rawValue))", "APPID=\(Self.quoted(appID))",
                     "GAME_DIR=\(Self.quoted(folder))",
                     "EXE=\(Self.quoted("Z:" + guest.replacingOccurrences(of: "/", with: "\\")))",
                     (["set --"] + arguments.map(Self.quoted)).joined(separator: " ")]
        if performance { lines.append("MACNEUTRON_PROFILE=\(Self.quoted("p"))") }
        var set: [String] = []
        for key in Self.passed where !(performance && Self.notPassedToPerformance.contains(key)) {
            if let value = environment[key] { lines.append("\(key)=\(Self.quoted(value))"); set.append(key) }
        }
        // The game's environment too, as a launch option is under Proton: guest DXMT and the forwarder read some
        // (DXMT_LOG_LEVEL, DXMT_ENABLE_NVEXT, MACNEUTRON_VM_D3D_TRACE), which run-game.sh doesn't export itself.
        if !set.isEmpty { lines.append("export " + set.joined(separator: " ")) }
        return lines.joined(separator: "\n") + "\n" + installScript + (try String(contentsOf: gameScript, encoding: .utf8))
    }
}

/// A Mac folder a game in the VM gets read-write (spec 2026-10-23-macneutron-vm-shared-saves §3): macneutron-vm's
/// `--user-share <tag>=<host>`, which the guest's mn-runner mounts at `guest` (the run share's `mounts` file).
public struct UserShare: Equatable, Sendable {
    public let tag: String
    public let host: String
    public let guest: String
    /// A tool's files the guest only reads (macneutron-vm's --ro-share), not the game's folders it writes.
    public var readOnly = false
}

public enum UserShareError: Error, CustomStringConvertible {
    /// ~/Documents can't be listed: macOS's privacy settings refused it to Steam, or it's missing.
    case documentsUnreadable(String)

    public var description: String {
        switch self {
        case .documentsUnreadable(let path):
            "MacNeutron can't open your Documents folder (\(path)), so a game in the VM couldn't reach its saves there. "
                + "Allow Steam in System Settings > Privacy & Security > Files and Folders, then launch the game again."
        }
    }
}

extension MicroVM {
    /// The guest prefix's profile, Proton's name for it (run-game.sh runs Wine as steamuser).
    static let guestProfile = "/home/mn/pfx/drive_c/users/steamuser"

    /// The folders Steam Cloud syncs for the game, as the guest gets them: the Mac prefix's profile (users/steamuser,
    /// or the profile it links to: macOS Wine's, named after the Mac user, ProfileLinks), made when missing as
    /// Steam's and Proton's layout has it; ~/Documents, which the profile's Documents link names; and the game's own
    /// folder in its library (`guest` is the game's path in the guest, MicroVM.guestPath's).
    static func userShares(prefix: URL, library: String, guest: String, home: String) throws -> [UserShare] {
        let fm = FileManager.default
        let users = prefix.appending(path: "drive_c/users").path(percentEncoded: false)
        var profile = users + "/steamuser"
        // One level, as Steam and macOS Wine name it (resolvingSymlinksInPath would also turn /var into /private/var).
        if let target = try? fm.destinationOfSymbolicLink(atPath: profile) {
            profile = target.hasPrefix("/") ? target : users + "/" + target
        }
        try fm.createDirectory(atPath: profile, withIntermediateDirectories: true)  // a dangling link's folder too
        let documents = home + "/Documents"
        do { _ = try fm.contentsOfDirectory(atPath: documents) } catch { throw UserShareError.documentsUnreadable(documents) }
        var shares = [UserShare(tag: "profile", host: profile, guest: guestProfile),
                      UserShare(tag: "documents", host: documents, guest: documents)]
        if let folder = gameFolder(guest) {
            shares.append(UserShare(tag: "game", host: library + "/" + folder, guest: "/mnt/games/" + folder))
        }
        return shares
    }

    /// The game's own folder in its library (its writable share), from its path in the guest; nil for a file at the
    /// library's top.
    static func gameFolder(_ guest: String) -> String? {
        let inGames = guest.hasPrefix("/mnt/games/") ? guest.dropFirst("/mnt/games/".count) : ""
        return inGames.contains("/") ? inGames.split(separator: "/").first.map(String.init) : nil
    }

    /// Where the guest mounts the Proton EasyAntiCheat Runtime (mn-runner allows /Volumes/...).
    static let guestEACRuntime = "/Volumes/Proton EasyAntiCheat Runtime"

    /// The Proton EasyAntiCheat Runtime (Steam app 1826330), which Linux Steam passes to every game under Proton: a
    /// Steam library's, or the copy Steam's console downloads (`download_depot 1826330 1826331`: Mac Steam doesn't
    /// install the Linux-only tool). A runtime is a folder with v2/.
    static func eacRuntime(steam: SteamLocation) -> String? {
        let depot = "steamapps/content/app_1826330/depot_1826331"
        let places = steam.libraries().map { $0.appending(path: "common/Proton EasyAntiCheat Runtime") }
            + [steam.root.appending(path: depot), steam.bundleMacOS.appending(path: depot)]
        return places.map { $0.path(percentEncoded: false) }.first {
            var directory: ObjCBool = false
            return FileManager.default.fileExists(atPath: $0 + "/v2", isDirectory: &directory) && directory.boolValue
        }
    }

    /// The run share's `mounts`: a `<tag> <guest path>` line per share, read by mn-runner.
    static func mountsFile(_ shares: [UserShare]) -> String { shares.map { "\($0.tag) \($0.guest)\n" }.joined() }
}
