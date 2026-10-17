import CryptoKit
import Darwin
import Foundation

/// The images from recipes (spec §4.5): every file's bytes go into a store, one file per SHA-256 (so names that differ
/// only by case on the Mac's disk never meet), from the packages' payloads and the parts' overlay; then a recipe becomes
/// a tar piped into mkfs.erofs, or the initramfs's cpio.
struct ImageAssembler {
    let store: URL

    /// Payload files whose SHA-256 is in `wanted`, each written beside the store and kept under its hash.
    func collect(_ rpm: URL, wanted: Set<String>) throws {
        try FileManager.default.createDirectory(at: store, withIntermediateDirectories: true)
        try RPMReader.readFiles(rpm) { _, _, next in
            let temporary = store.appending(path: ".incoming-\(UUID().uuidString)")
            let hash = try Self.copy(next, to: temporary)
            let kept = store.appending(path: hash).path(percentEncoded: false)
            if wanted.contains(hash), !FileManager.default.fileExists(atPath: kept) {
                guard rename(temporary.path(percentEncoded: false), kept) == 0 else { throw POSIXError(.EIO) }
            } else {
                try FileManager.default.removeItem(at: temporary)
            }
        }
    }

    /// The overlay's files (each named by its SHA-256), cloned into the store.
    func collectOverlay(_ folder: URL) throws {
        let fm = FileManager.default
        try fm.createDirectory(at: store, withIntermediateDirectories: true)
        for name in try fm.contentsOfDirectory(atPath: folder.path(percentEncoded: false)) {
            let kept = store.appending(path: name)
            if !fm.fileExists(atPath: kept.path(percentEncoded: false)) {
                try fm.copyItem(at: folder.appending(path: name), to: kept)  // an APFS clone
            }
        }
    }

    /// One payload file to `to` (vmlinuz, FEX's default.erofs), checked against `sha256` when given (a checked
    /// package already fixes its payload's bytes).
    func extract(_ rpm: URL, path: String, to: URL, sha256: String?) throws {
        var found = false
        try RPMReader.readFiles(rpm) { _, paths, next in
            guard !found, paths.contains(path) else { return }
            found = true
            let hash = try Self.copy(next, to: to)
            guard sha256 == nil || hash == sha256 else {
                try? FileManager.default.removeItem(at: to)
                throw VMSetupError.mismatch(file: path)
            }
        }
        if !found { throw VMSetupError.missing(path: path) }
    }

    /// mkfs.erofs --tar=f -L label -zlz4hc out, the recipe's tar on its stdin (it reads a tar from there when given no
    /// source). A missing file stops it, and `out` is removed.
    func erofs(_ recipe: [RecipeEntry], label: String, mkfs: URL, out: URL) throws {
        let input = Pipe()
        // A mkfs.erofs that dies makes the next write fail with EPIPE instead of ending this process (this pipe only).
        _ = fcntl(input.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)
        let tool = Self.mkfs(mkfs, label: label, out: out, source: nil)
        tool.standardInput = input
        try VMSetup.Children.run(tool)
        defer { VMSetup.Children.finished(tool) }
        do {
            var tar = TarWriter(input.fileHandleForWriting)
            for entry in recipe { try tar.add(entry, contents: try contents(entry)) }
            try tar.finish()
            try input.fileHandleForWriting.close()
        } catch {
            try? input.fileHandleForWriting.close()
            if tool.isRunning, error is VMSetupError { tool.terminate() }  // ours: stop it; a broken pipe: it stopped
            tool.waitUntilExit()
            try? FileManager.default.removeItem(at: out)
            if !(error is VMSetupError), tool.terminationReason == .exit, tool.terminationStatus != 0 {
                throw VMSetupError.tool("mkfs.erofs", tool.terminationStatus)
            }
            throw error
        }
        tool.waitUntilExit()
        guard tool.terminationStatus == 0 else {
            try? FileManager.default.removeItem(at: out)
            throw VMSetupError.tool("mkfs.erofs", tool.terminationStatus)
        }
    }

    /// The same for a ready tar file (the parts' wine.tar).
    static func erofs(tar: URL, label: String, mkfs: URL, out: URL) throws {
        let tool = Self.mkfs(mkfs, label: label, out: out, source: tar)
        try VMSetup.Children.run(tool)
        tool.waitUntilExit()
        VMSetup.Children.finished(tool)
        guard tool.terminationStatus == 0 else {
            try? FileManager.default.removeItem(at: out)
            throw VMSetupError.tool("mkfs.erofs", tool.terminationStatus)
        }
    }

    func cpio(_ recipe: [RecipeEntry], out: URL) throws {
        FileManager.default.createFile(atPath: out.path(percentEncoded: false), contents: nil)
        let handle = try FileHandle(forWritingTo: out)
        defer { try? handle.close() }
        var cpio = CPIOWriter(handle)
        for entry in recipe { try cpio.add(entry, contents: try contents(entry)) }
        try cpio.finish()
    }

    /// A file entry's bytes in the store.
    private func contents(_ entry: RecipeEntry) throws -> URL? {
        guard case .file(_, let sha256, _) = entry.kind else { return nil }
        let url = store.appending(path: sha256)
        guard FileManager.default.fileExists(atPath: url.path(percentEncoded: false)) else {
            throw VMSetupError.missing(path: entry.path)
        }
        return url
    }

    private static func mkfs(_ tool: URL, label: String, out: URL, source: URL?) -> Process {
        let p = Process()
        p.executableURL = tool
        p.arguments = ["--tar=f", "-L", label, "-zlz4hc", out.path(percentEncoded: false)]
            + (source.map { [$0.path(percentEncoded: false)] } ?? [])
        p.standardOutput = FileHandle.nullDevice
        return p
    }

    /// The file's chunks into `to`, returning their SHA-256.
    private static func copy(_ next: () throws -> Data?, to: URL) throws -> String {
        FileManager.default.createFile(atPath: to.path(percentEncoded: false), contents: nil)
        let handle = try FileHandle(forWritingTo: to)
        defer { try? handle.close() }
        var hash = SHA256()
        while let chunk = try next() {
            hash.update(data: chunk)
            try handle.write(contentsOf: chunk)
        }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
}
