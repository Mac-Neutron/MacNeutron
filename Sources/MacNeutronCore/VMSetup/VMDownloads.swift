import CryptoKit
import Foundation

/// The setup's downloads, each into cache/<sha256> and checked for its size and SHA-256 before it's kept.
enum VMDownloads {
    /// https through ValveClient's ephemeral session; file: URLs (tests' mirrors) copied.
    static let download: ValveClient.Fetch = { url in
        guard url.isFileURL else { return try await ValveClient.download(url) }
        let copy = FileManager.default.temporaryDirectory.appending(path: "MacNeutron VM \(UUID().uuidString)")
        try FileManager.default.copyItem(at: url, to: copy)
        return copy
    }

    /// The cached copy if it's right, else each source in turn, the whole list up to three times.
    static func fetch(_ sources: [URL], size: Int64, sha256: String, name: String, cache: URL,
                      fetch: ValveClient.Fetch) async throws -> URL {
        let kept = cache.appending(path: sha256)
        if matches(kept, size: size, sha256: sha256) { return kept }
        try FileManager.default.createDirectory(at: cache, withIntermediateDirectories: true)
        for _ in 0..<3 {
            for url in sources {
                guard let file = try? await fetch(url) else { continue }
                if matches(file, size: size, sha256: sha256) {
                    try? FileManager.default.removeItem(at: kept)
                    try FileManager.default.moveItem(at: file, to: kept)
                    return kept
                }
                try? FileManager.default.removeItem(at: file)
            }
        }
        throw VMSetupError.download(name: name)
    }

    /// Every item, four at a time; `progress` gets the bytes of the files done (cached ones count at once) and the total.
    static func fetchAll(_ items: [(sources: [URL], size: Int64, sha256: String, name: String)], cache: URL,
                         fetch: @escaping ValveClient.Fetch, progress: (Int64, Int64) -> Void) async throws {
        let total = items.reduce(0) { $0 + $1.size }
        var done: Int64 = 0
        try await withThrowingTaskGroup(of: Int64.self) { group in
            var next = items.makeIterator()
            func start() -> Bool {
                guard let item = next.next() else { return false }
                group.addTask {
                    _ = try await Self.fetch(item.sources, size: item.size, sha256: item.sha256, name: item.name,
                                             cache: cache, fetch: fetch)
                    return item.size
                }
                return true
            }
            for _ in 0..<4 where start() {}
            while let size = try await group.next() {
                done += size
                progress(done, total)
                _ = start()
            }
        }
    }

    static func matches(_ file: URL, size: Int64, sha256: String) -> Bool {
        let path = file.path(percentEncoded: false)
        guard (try? FileManager.default.attributesOfItem(atPath: path)[.size] as? NSNumber)?.int64Value == size,
              let handle = try? FileHandle(forReadingFrom: file) else { return false }
        defer { try? handle.close() }
        var hash = SHA256()
        while let chunk = try? handle.read(upToCount: 1 << 20), !chunk.isEmpty { hash.update(data: chunk) }
        return hash.finalize().map { String(format: "%02x", $0) }.joined() == sha256
    }
}
