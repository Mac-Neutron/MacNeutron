import CryptoKit
import Foundation

/// The setup's downloads, each into cache/<sha256> and checked for its size and SHA-256 before it's kept.
enum VMDownloads {
    /// An ephemeral session's download (no cookies, no cache), or a copy of a file: URL (tests' mirrors), as one file
    /// in the temporary folder, which the caller moves or removes.
    static let download: ValveClient.Fetch = { url in
        let fm = FileManager.default
        let kept = fm.temporaryDirectory.appending(path: "MacNeutron VM \(UUID().uuidString)")
        if url.isFileURL {
            try fm.copyItem(at: url, to: kept)
            return kept
        }
        let session = URLSession(configuration: .ephemeral)
        defer { session.finishTasksAndInvalidate() }
        let (file, response) = try await session.download(from: url)
        guard (response as? HTTPURLResponse)?.statusCode == 200 else {
            try? fm.removeItem(at: file)
            throw URLError(.badServerResponse)
        }
        try fm.moveItem(at: file, to: kept)
        return kept
    }

    /// Between rounds: a short network drop shouldn't use up every try.
    static let backoff: @Sendable (Int) async -> Void = { try? await Task.sleep(for: .seconds($0)) }

    /// The cached copy if it's right, else each source in turn, the whole list up to three times.
    static func fetch(_ sources: [URL], size: Int64, sha256: String, name: String, cache: URL,
                      fetch: ValveClient.Fetch, pause: @Sendable (Int) async -> Void = backoff) async throws -> URL {
        let kept = cache.appending(path: sha256)
        if matches(kept, size: size, sha256: sha256) { return kept }
        try FileManager.default.createDirectory(at: cache, withIntermediateDirectories: true)
        for round in 0..<3 {
            if round > 0 { await pause(round == 1 ? 1 : 4) }
            for url in sources {
                try Task.checkCancellation()
                let file: URL
                do { file = try await fetch(url) } catch is CancellationError { throw CancellationError() } catch { continue }
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
                         fetch: @escaping ValveClient.Fetch, pause: @escaping @Sendable (Int) async -> Void = backoff,
                         progress: (Int64, Int64) -> Void) async throws {
        let total = items.reduce(0) { $0 + $1.size }
        var done: Int64 = 0
        progress(0, total)
        try await withThrowingTaskGroup(of: Int64.self) { group in
            var next = items.makeIterator()
            func start() -> Bool {
                guard let item = next.next() else { return false }
                group.addTask {
                    _ = try await Self.fetch(item.sources, size: item.size, sha256: item.sha256, name: item.name,
                                             cache: cache, fetch: fetch, pause: pause)
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
