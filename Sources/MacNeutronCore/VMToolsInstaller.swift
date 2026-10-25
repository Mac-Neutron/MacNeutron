import CryptoKit
import Darwin
import Foundation

public enum VMToolsInstallOutcome: Equatable, Sendable {
    case installed, unchanged
    /// A VM runs from the folder (its executable's path): the old tools stay until it ends.
    case deferred(String)
    /// The folder is a developer's (`isDeveloperFolder`): never written.
    case developerFolder
}

/// The microVM's host tools (MicroVM.swift), from MacNeutron.app's Contents/Helpers/vm into the VM folder, the way
/// `RuntimeInstaller` installs wine.app: each item copied beside itself, then swapped in, never while a VM runs from the
/// folder. The folder's games' files (state/, runs/) and its images/ aren't the tools' and are never touched.
public enum VMToolsInstaller {
    /// dxmt: native DXMT and its replayer, which macneutron-vm loads for Direct3D forwarding (eac-vm/d3d).
    /// dxmt-translator: native DXMT's translator key (MicroVM's `dxmtTranslator`).
    public static let items = ["macneutron-vm", "libkrun", "steam-forward", "gvproxy", "metal-host", "dxmt",
                               "run-guest.sh", "run-game.sh", "dxmt-translator"]
    /// The items that aren't code: MacNeutron.app keeps them in Contents/Resources/vm (codesign takes a script or a data
    /// file in Contents/Helpers for unsigned code, and a script's own signature doesn't survive the zip).
    public static let scripts = ["run-guest.sh", "run-game.sh", "dxmt-translator"]

    /// `scripts`: where `Self.scripts` come from, else `tools`.
    @discardableResult
    public static func install(from tools: URL, scripts: URL? = nil, into vm: URL,
                               runningExecutables: () -> [String] = RunningProcesses.executablePaths) throws
        -> VMToolsInstallOutcome {
        if isDeveloperFolder(vm) { return .developerFolder }
        let fm = FileManager.default
        let source = { (item: String) in (Self.scripts.contains(item) ? scripts ?? tools : tools).appending(path: item) }
        let stamp = vm.appending(path: "tools.identity")
        let wanted = try digest(items.map { ($0, source($0)) }) + "\n"
        let installing = vm.appending(path: ".installing")
        if (try? String(contentsOf: stamp, encoding: .utf8)) == wanted,
           items.allSatisfy({ fm.fileExists(atPath: vm.appending(path: $0).path(percentEncoded: false)) }) {
            try? fm.removeItem(at: installing)  // a crash between the stamp and the marker's removal
            return .unchanged
        }
        try fm.createDirectory(at: vm, withIntermediateDirectories: true)
        if let running = running(in: vm, runningExecutables) { return .deferred(running) }
        // These paths have no trailing "/", for cp and rename(2).
        let path = { (name: String) in vm.appending(path: name).path(percentEncoded: false) }
        for item in items {  // an interrupted install's copies
            try? fm.removeItem(atPath: path(item + ".new"))
            try? fm.removeItem(atPath: path(item + ".old"))
        }
        for item in items {
            let copy = Process()
            copy.executableURL = URL(filePath: "/bin/cp")
            copy.arguments = ["-c", "-R", source(item).path(percentEncoded: false), path(item + ".new")]
            try copy.run()
            copy.waitUntilExit()
            guard copy.terminationStatus == 0 else {
                for item in items { try? fm.removeItem(atPath: path(item + ".new")) }
                throw RuntimeInstallError.copyFailed(copy.terminationStatus)
            }
        }
        // As wine.app's install checks its copy: every Mach-O in the copies verifies, or nothing is swapped.
        for item in items {
            let status = verify(URL(filePath: path(item + ".new")))
            guard status == 0 else {
                for item in items { try? fm.removeItem(atPath: path(item + ".new")) }
                throw RuntimeInstallError.signatureInvalid(status)
            }
        }
        // Again: the copy takes a moment, and a VM started meanwhile runs from the old tools.
        if let running = running(in: vm, runningExecutables) {
            for item in items { try? fm.removeItem(atPath: path(item + ".new")) }
            return .deferred(running)
        }
        // Until the last rename a launch runs on macOS (MicroVM.find), not on a mix of old and new tools.
        fm.createFile(atPath: installing.path(percentEncoded: false), contents: nil)
        for item in items {
            let (new, old) = (path(item + ".new"), path(item))
            let swapped = fm.fileExists(atPath: old) ? renamex_np(new, old, UInt32(RENAME_SWAP)) : rename(new, old)
            guard swapped == 0 else { throw RuntimeInstallError.swapFailed(errno) }
            try? fm.removeItem(atPath: path(item + ".new"))  // the old item, after a swap
        }
        try wanted.write(to: stamp, atomically: true, encoding: .utf8)
        try? fm.removeItem(at: installing)
        return .installed
    }

    /// Valve's steamclient64.dll (`ValveClient`) as the guest's share/valve/steamclient64.dll (run-game.sh copies it into
    /// the game's prefix); a nil install removes it.
    public static func copyValveClient(_ install: ValveInstall?, into vm: URL) throws {
        if isDeveloperFolder(vm) { return }
        let destination = vm.appending(path: "share/valve/steamclient64.dll")
        guard let install else {
            try? FileManager.default.removeItem(at: destination)
            return
        }
        try RuntimeInstaller.installFile(install.steamClient64, at: destination)
    }

    /// codesign --verify --strict on every Mach-O under `url` (an item's copy): 0, or the first failure's status.
    static func verify(_ url: URL) -> Int32 {
        let fm = FileManager.default
        let root = url.path(percentEncoded: false)
        let files = [root] + ((fm.enumerator(atPath: root)?.allObjects as? [String]) ?? []).map { root + "/" + $0 }
        for file in files where isMachO(file) {
            let check = Process()
            check.executableURL = URL(filePath: "/usr/bin/codesign")
            check.arguments = ["--verify", "--strict", file]
            check.standardError = FileHandle.nullDevice
            guard (try? check.run()) != nil else { return -1 }
            check.waitUntilExit()
            if check.terminationStatus != 0 { return check.terminationStatus }
        }
        return 0
    }

    private static func isMachO(_ path: String) -> Bool {
        guard let handle = FileHandle(forReadingAtPath: path), let magic = try? handle.read(upToCount: 4) else { return false }
        try? handle.close()
        return [[0xcf, 0xfa, 0xed, 0xfe], [0xca, 0xfe, 0xba, 0xbe], [0xce, 0xfa, 0xed, 0xfe]].contains(Array(magic))
    }

    /// SHA-256 over every item's files: their paths, kinds and bytes (a symlink's target), so any change to any item is
    /// a new set of tools (one binary's code identity misses a changed script or library).
    static func digest(_ items: [(name: String, url: URL)]) throws -> String {
        var hash = SHA256()
        let fm = FileManager.default
        func add(_ text: String) { hash.update(data: Data((text + "\0").utf8)) }
        for (name, url) in items {
            var paths = [""]
            if let walker = fm.enumerator(atPath: url.path(percentEncoded: false)) {
                paths += (walker.allObjects as? [String] ?? []).sorted()
            }
            for relative in paths {
                let file = relative.isEmpty ? url : url.appending(path: relative)
                let path = file.path(percentEncoded: false)
                if let target = try? fm.destinationOfSymbolicLink(atPath: path) {
                    add("l \(name)/\(relative) \(target)")
                } else if (try? fm.attributesOfItem(atPath: path)[.type] as? FileAttributeType) == .typeDirectory {
                    add("d \(name)/\(relative)")
                } else {
                    add("f \(name)/\(relative)")
                    hash.update(data: try Data(contentsOf: file, options: .alwaysMapped))
                }
            }
        }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }

    /// A developer's folder (build/eac-vm-work, usually linked here): its images are eac-vm's own build, whose
    /// manifest.sha256 says `dev` (eac-vm/image/build.sh). Any other folder, linked or not (a player's VM moved to another
    /// drive), is the app's to update.
    public static func isDeveloperFolder(_ vm: URL) -> Bool {
        (try? String(contentsOf: vm.appending(path: "images/manifest.sha256"), encoding: .utf8))?
            .trimmingCharacters(in: .whitespacesAndNewlines) == "dev"
    }

    /// The folder is a link to a folder that isn't there (its drive disconnected, or a developer's build/ deleted): the
    /// link's target. Nil for any other folder.
    public static func missingTarget(_ vm: URL) -> String? {
        guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: linkPath(vm)),
              !FileManager.default.fileExists(atPath: linkPath(vm)) else { return nil }
        return target
    }

    /// Rebuild's way past a missing folder: the link goes, so a setup makes a real folder there. True if it removed one.
    public static func removeDeadLink(_ vm: URL) -> Bool {
        guard missingTarget(vm) != nil else { return false }
        return (try? FileManager.default.removeItem(atPath: linkPath(vm))) != nil
    }

    /// The folder is a link (to anywhere, there or not).
    public static func isLink(_ vm: URL) -> Bool {
        (try? FileManager.default.destinationOfSymbolicLink(atPath: linkPath(vm))) != nil
    }

    /// Rebuild's way past a developer's folder linked here: only the link goes (the developer's folder is never touched),
    /// so a setup makes a real folder there. True if it removed one; a developer's own folder (no link) stays.
    public static func removeDeveloperLink(_ vm: URL) -> Bool {
        guard isLink(vm), isDeveloperFolder(vm) else { return false }
        return (try? FileManager.default.removeItem(atPath: linkPath(vm))) != nil
    }

    /// The folder's path without its trailing "/", which would follow a link.
    private static func linkPath(_ vm: URL) -> String {
        let path = vm.path(percentEncoded: false)
        return path.hasSuffix("/") ? String(path.dropLast()) : path
    }

    /// The first running executable under the folder, by its real path (the kernel reports /private/var/…) or as given.
    private static func running(in vm: URL, _ runningExecutables: () -> [String]) -> String? {
        let given = vm.standardizedFileURL.path(percentEncoded: false)
        let roots = [given, realPath(given) ?? given].map { $0.hasSuffix("/") ? $0 : $0 + "/" }
        return runningExecutables().first { path in
            roots.contains { (realPath(path) ?? path).hasPrefix($0) || path.hasPrefix($0) }
        }
    }
}
