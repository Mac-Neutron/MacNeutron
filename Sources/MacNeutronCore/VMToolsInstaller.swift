import Darwin
import Foundation

public enum VMToolsInstallOutcome: Equatable, Sendable {
    case installed, unchanged
    /// A VM runs from the folder (its executable's path): the old tools stay until it ends.
    case deferred(String)
    /// The folder is a developer's link to build/eac-vm-work: never written.
    case developerFolder
}

/// The microVM's host tools (MicroVM.swift), from MacNeutron.app's Contents/Helpers/vm into the VM folder, the way
/// `RuntimeInstaller` installs wine.app: each item copied beside itself, then swapped in, never while a VM runs from the
/// folder. The folder's games' files (state/, runs/) and its images/ aren't the tools' and are never touched.
public enum VMToolsInstaller {
    public static let items = ["macneutron-vm", "libkrun", "steam-forward", "gvproxy", "metal-host", "run-guest.sh",
                               "run-game.sh"]

    @discardableResult
    public static func install(from tools: URL, into vm: URL,
                               runningExecutables: () -> [String] = RunningProcesses.executablePaths,
                               identity: (URL) -> String? = CodeIdentity.of) throws -> VMToolsInstallOutcome {
        if isDeveloperFolder(vm) { return .developerFolder }
        let fm = FileManager.default
        let stamp = vm.appending(path: "tools.identity")
        let wanted = identity(tools.appending(path: "macneutron-vm")).map { $0 + "\n" }
        if let wanted, (try? String(contentsOf: stamp, encoding: .utf8)) == wanted,
           items.allSatisfy({ fm.fileExists(atPath: vm.appending(path: $0).path(percentEncoded: false)) }) {
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
            copy.arguments = ["-c", "-R", tools.appending(path: item).path(percentEncoded: false), path(item + ".new")]
            try copy.run()
            copy.waitUntilExit()
            guard copy.terminationStatus == 0 else {
                for item in items { try? fm.removeItem(atPath: path(item + ".new")) }
                throw RuntimeInstallError.copyFailed(copy.terminationStatus)
            }
        }
        // Again: the copy takes a moment, and a VM started meanwhile runs from the old tools.
        if let running = running(in: vm, runningExecutables) {
            for item in items { try? fm.removeItem(atPath: path(item + ".new")) }
            return .deferred(running)
        }
        for item in items {
            let (new, old) = (path(item + ".new"), path(item))
            let swapped = fm.fileExists(atPath: old) ? renamex_np(new, old, UInt32(RENAME_SWAP)) : rename(new, old)
            guard swapped == 0 else { throw RuntimeInstallError.swapFailed(errno) }
            try? fm.removeItem(atPath: path(item + ".new"))  // the old item, after a swap
        }
        try (wanted ?? "unsigned\n").write(to: stamp, atomically: true, encoding: .utf8)
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

    /// The folder itself is a symlink: a developer's link to build/eac-vm-work.
    public static func isDeveloperFolder(_ vm: URL) -> Bool {
        let path = vm.path(percentEncoded: false)
        let link = path.hasSuffix("/") ? String(path.dropLast()) : path
        return (try? FileManager.default.destinationOfSymbolicLink(atPath: link)) != nil
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
