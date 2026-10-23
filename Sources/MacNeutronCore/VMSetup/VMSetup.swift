import CryptoKit
import Darwin
import Foundation

/// `macneutron vm-setup` (spec §4.5): the VM folder's images from the app's manifest. Downloads and the image set being
/// built live in <vm>/setup; the set swaps in whole, its manifest.sha256 already written, and never while a VM runs
/// from the folder (exit 75; the next run swaps it without rebuilding). The games' own files (state/, runs/) and a
/// developer's folder (`VMToolsInstaller.isDeveloperFolder`) are never touched.
public enum VMSetup {
    /// The setup's own children (tar, mkfs.erofs), ended with it: the app quitting ends vm-setup with SIGTERM, and
    /// vm-setup ends them before it exits. A group keeps a test's children apart from the ones tests beside it run.
    enum Children {
        nonisolated(unsafe) private static var running: [(process: Process, group: Int)] = []
        private static let lock = NSLock()

        static func run(_ process: Process, group: Int = 0) throws {
            try lock.withLock {
                try process.run()
                running.append((process, group))
            }
        }

        static func finished(_ process: Process) { lock.withLock { running.removeAll { $0.process === process } } }

        /// Every child, or one group's.
        static func terminateAll(group: Int? = nil) {
            lock.withLock {
                for c in running where c.process.isRunning && (group == nil || c.group == group) { c.process.terminate() }
            }
        }
    }

    /// Which image set a manifest builds (images/manifest.sha256): the SHA-256 of its JSON with sorted keys and without
    /// the parts' URL, which names the release it's on. A release whose VM didn't change moves only that URL, and no
    /// Mac downloads its VM again.
    public static func manifestHash(_ manifest: URL) -> String? {
        guard let data = try? Data(contentsOf: manifest) else { return nil }
        var canonical = data
        if var object = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            if var parts = object["parts"] as? [String: Any] {
                parts["url"] = nil
                object["parts"] = parts
            }
            canonical = (try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])) ?? data
        }
        return SHA256.hash(data: canonical).map { String(format: "%02x", $0) }.joined()
    }

    /// 0 when the set is in place, 75 when a running VM defers the swap; throws `VMSetupError` otherwise.
    /// `progress` gets lines: `progress download <done> <total>`, `progress assemble <k> 5`, `progress install <k> 1`.
    static func run(manifest manifestFile: URL, vm: URL, mkfs: URL, minFreeGB: Int = 8,
                    fetch: @escaping ValveClient.Fetch = VMDownloads.download,
                    runningExecutables: () -> [String] = RunningProcesses.executablePaths,
                    progress: (String) -> Void = { print($0) }) async throws -> Int32 {
        if VMToolsInstaller.isDeveloperFolder(vm) {
            progress("vm-setup: \(vm.path(percentEncoded: false)) is a developer's folder: left alone")
            return 0
        }
        guard let hash = manifestHash(manifestFile) else { throw CocoaError(.fileReadNoSuchFile) }
        // One setup a folder: a crashed app's may still run when the relaunch starts another.
        try FileManager.default.createDirectory(at: vm, withIntermediateDirectories: true)
        let lock = open(vm.appending(path: "setup.lock").path(percentEncoded: false), O_RDWR | O_CREAT, 0o600)
        defer { if lock >= 0 { close(lock) } }
        guard lock >= 0, flock(lock, LOCK_EX | LOCK_NB) == 0 else {
            progress("vm-setup: another setup runs in \(vm.path(percentEncoded: false)): this one waits")
            return 75
        }
        let manifest = try JSONDecoder().decode(VMSetupManifest.self, from: Data(contentsOf: manifestFile))
        let fm = FileManager.default
        let setup = vm.appending(path: "setup", directoryHint: .isDirectory)
        let built = setup.appending(path: "images", directoryHint: .isDirectory)
        let parts = setup.appending(path: "parts", directoryHint: .isDirectory)
        try fm.createDirectory(at: setup, withIntermediateDirectories: true)
        if (try? String(contentsOf: built.appending(path: ".built-for"), encoding: .utf8)) != hash {
          do {
            try checkSpace(vm, minFreeGB)
            let cache = setup.appending(path: "cache", directoryHint: .isDirectory)
            let packages = manifest.packages + [manifest.fexRootfs.package]
            let items = [([manifest.parts.url], manifest.parts.size, manifest.parts.sha256, manifest.parts.url.lastPathComponent)]
                + packages.map { (manifest.sources($0), $0.size, $0.sha256, $0.file) }
            try await VMDownloads.fetchAll(items, cache: cache, fetch: fetch) { progress("progress download \($0) \($1)") }
            let cached = { (sha256: String) in cache.appending(path: sha256) }
            try? fm.removeItem(at: parts)
            try fm.createDirectory(at: parts, withIntermediateDirectories: true)
            try tool("/usr/bin/tar", ["-xJf", cached(manifest.parts.sha256).path(percentEncoded: false), "-C",
                                      parts.path(percentEncoded: false)])
            try? fm.removeItem(at: built)
            try fm.createDirectory(at: built, withIntermediateDirectories: true)
            let assembler = ImageAssembler(store: setup.appending(path: "store", directoryHint: .isDirectory))
            guard let kernel = manifest.packages.first(where: { $0.name == "kernel-core" }) else {
                throw VMSetupError.missing(path: "kernel-core")
            }
            try assembler.extract(cached(kernel.sha256), path: "/lib/modules/\(manifest.kernel)/vmlinuz",
                                  to: built.appending(path: "vmlinuz"), sha256: nil)
            progress("progress assemble 1 5")
            try assembler.extract(cached(manifest.fexRootfs.package.sha256), path: manifest.fexRootfs.image,
                                  to: built.appending(path: "x86-64-rootfs.erofs"), sha256: manifest.fexRootfs.imageSha256)
            progress("progress assemble 2 5")
            let base = try Recipe.parse(String(contentsOf: parts.appending(path: "recipe/base.tsv"), encoding: .utf8))
            let initramfs = try Recipe.parse(String(contentsOf: parts.appending(path: "recipe/initramfs.tsv"), encoding: .utf8))
            let wanted = Set((base + initramfs).compactMap { if case .file(_, let s, .package) = $0.kind { s } else { nil } })
            try assembler.collectOverlay(parts.appending(path: "overlay", directoryHint: .isDirectory))
            for package in manifest.packages { try assembler.collect(cached(package.sha256), wanted: wanted) }
            try assembler.erofs(base, label: "mn-base", mkfs: mkfs, out: built.appending(path: "base.erofs"))
            progress("progress assemble 3 5")
            try assembler.cpio(initramfs, out: built.appending(path: "initramfs.img"))
            try? fm.removeItem(at: assembler.store)  // done with: the setup's peak on disk is downloads, parts, images
            progress("progress assemble 4 5")
            try ImageAssembler.erofs(tar: parts.appending(path: "wine.tar"), label: "mn-wine", mkfs: mkfs,
                                     out: built.appending(path: "wine.erofs"))
            progress("progress assemble 5 5")
            for name in ["cmdline.txt", "packages.lock", "state-template.img"] {
                try fm.copyItem(at: parts.appending(path: name), to: built.appending(path: name))  // clones, sparse kept
            }
            try (hash + "\n").write(to: built.appending(path: "manifest.sha256"), atomically: true, encoding: .utf8)
            try hash.write(to: built.appending(path: ".built-for"), atomically: true, encoding: .utf8)
          } catch {
            // Keep the downloads for the next try, and nothing else: parts, store and a half-built set are gigabytes.
            for name in ["parts", "store", "images"] { try? fm.removeItem(at: setup.appending(path: name)) }
            throw error
          }
        }
        if let running = runningUnder(vm, runningExecutables) {
            progress("vm-setup: \(running) runs from the folder: the new images go in after it ends")
            return 75
        }
        progress("progress install 0 1")
        try? fm.removeItem(at: built.appending(path: ".built-for"))
        let (new, old) = (built.path(percentEncoded: false), vm.appending(path: "images").path(percentEncoded: false))
        let swapped = fm.fileExists(atPath: old) ? renamex_np(new, old, UInt32(RENAME_SWAP)) : rename(new, old)
        guard swapped == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
        // The guest's tools, once the images are in: each file replaced in one rename, at its path under share/bin
        // (the ALSA plugin of each width is in lib/ and lib64/).
        let bin = parts.appending(path: "share/bin", directoryHint: .isDirectory)
        for name in (try? fm.subpathsOfDirectory(atPath: bin.path(percentEncoded: false))) ?? [] {
            var isDirectory: ObjCBool = false
            guard fm.fileExists(atPath: bin.appending(path: name).path(percentEncoded: false), isDirectory: &isDirectory),
                  !isDirectory.boolValue else { continue }
            try RuntimeInstaller.installFile(bin.appending(path: name), at: vm.appending(path: "share/bin/\(name)"))
        }
        try fm.removeItem(at: setup)  // the old set, the downloads and the parts
        progress("progress install 1 1")
        return 0
    }

    /// The packages none of whose sources answers with their size (`head`: a URL's size, nil when it doesn't answer):
    /// the release checks every pin can still be downloaded before it ships (`vm-setup --check-sources`).
    static func missingSources(_ manifest: VMSetupManifest, head: (URL) async -> Int64?) async -> [String] {
        var missing: [String] = []
        for p in manifest.packages + [manifest.fexRootfs.package] {
            var found = false
            for url in manifest.sources(p) where await head(url) == p.size {
                found = true
                break
            }
            if !found { missing.append(p.file) }
        }
        return missing
    }

    /// A HEAD request's Content-Length, when the answer is a 200.
    static func head(_ url: URL) async -> Int64? {
        var request = URLRequest(url: url)
        request.httpMethod = "HEAD"
        guard let (_, response) = try? await URLSession(configuration: .ephemeral).data(for: request),
              let http = response as? HTTPURLResponse, http.statusCode == 200 else { return nil }
        return http.expectedContentLength
    }

    private static func checkSpace(_ vm: URL, _ gb: Int) throws {
        let values = try vm.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        if let free = values.volumeAvailableCapacityForImportantUsage, free < Int64(gb) * 1_000_000_000 {
            throw VMSetupError.noSpace(needed: gb)
        }
    }

    private static func tool(_ path: String, _ arguments: [String]) throws {
        let p = Process()
        p.executableURL = URL(filePath: path)
        p.arguments = arguments
        try Children.run(p)
        p.waitUntilExit()
        Children.finished(p)
        guard p.terminationStatus == 0 else { throw VMSetupError.tool((path as NSString).lastPathComponent, p.terminationStatus) }
    }

    private static func runningUnder(_ vm: URL, _ runningExecutables: () -> [String]) -> String? {
        let given = vm.standardizedFileURL.path(percentEncoded: false)
        let roots = [given, realPath(given) ?? given].map { $0.hasSuffix("/") ? $0 : $0 + "/" }
        return runningExecutables().first { path in roots.contains { (realPath(path) ?? path).hasPrefix($0) || path.hasPrefix($0) } }
    }
}
