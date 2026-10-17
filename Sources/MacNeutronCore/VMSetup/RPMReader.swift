import Foundation

struct RPMHeader: Equatable { let name, version, release, arch, payloadCompressor: String }

/// An RPM's header and payload files, streamed: the lead (96 bytes), the signature header padded to 8 bytes, the
/// header (big-endian index and store), then the compressed newc cpio payload. Nothing here checks signatures: the
/// release checked Fedora's when it made the manifest, and the setup checks each package's SHA-256 against it.
enum RPMReader {
    static func readFiles(_ rpm: URL, _ body: (_ header: RPMHeader, _ paths: [String],
                                               _ next: () throws -> Data?) throws -> Void) throws {
        let file = try FileSource(rpm)
        _ = try file.read(exactly: 96)
        let signature = try intro(file), size = 16 + 16 * signature.entries + signature.store
        _ = try file.read(exactly: size - 16 + (8 - size % 8) % 8)
        let tags = try strings(file, [1000, 1001, 1002, 1022, 1124, 1125])
        let format = tags[1124] ?? "", compressor = tags[1125] ?? "gzip"
        guard format == "cpio" else { throw VMSetupError.unsupported(format) }
        let header = RPMHeader(name: tags[1000] ?? "", version: tags[1001] ?? "", release: tags[1002] ?? "",
                               arch: tags[1022] ?? "", payloadCompressor: compressor)
        let payload = PayloadSource(file, decoder: try PayloadDecoder(compressor: compressor))
        try CPIOReader.read(payload) { paths, next in
            try withoutActuallyEscaping(next) { try body(header, paths, $0) }
        }
    }

    /// A header's 16-byte intro: its index entries and store bytes, which follow.
    private static func intro(_ source: ByteSource) throws -> (entries: Int, store: Int) {
        let intro = try source.read(exactly: 16)
        guard intro.prefix(3) == Data([0x8e, 0xad, 0xe8]) else { throw VMSetupError.corrupt }
        return (Int(be32(intro, 8)), Int(be32(intro, 12)))
    }

    /// The header's STRING (6) and I18NSTRING (9) values for `wanted` tags.
    private static func strings(_ source: ByteSource, _ wanted: Set<UInt32>) throws -> [UInt32: String] {
        let (entries, storeSize) = try intro(source)
        let indexSize = 16 * entries
        let rest = try source.read(exactly: indexSize + storeSize)
        let store = rest.subdata(in: rest.startIndex + indexSize..<rest.endIndex)
        var out: [UInt32: String] = [:]
        for i in 0..<entries {
            let (tag, type, offset) = (be32(rest, 16 * i), be32(rest, 16 * i + 4), Int(be32(rest, 16 * i + 8)))
            guard wanted.contains(tag), type == 6 || type == 9, offset < store.count else { continue }
            out[tag] = String(decoding: store[(store.startIndex + offset)...].prefix { $0 != 0 }, as: UTF8.self)
        }
        return out
    }

    private static func be32(_ data: Data, _ at: Int) -> UInt32 {
        data[(data.startIndex + at)..<(data.startIndex + at + 4)].reduce(0) { $0 << 8 | UInt32($1) }
    }
}

/// A file's bytes, read 1 MiB at a time.
final class FileSource: ByteSource {
    private let handle: FileHandle
    private var buffer = Data()
    init(_ url: URL) throws { handle = try FileHandle(forReadingFrom: url) }
    deinit { try? handle.close() }

    func read(upTo count: Int) throws -> Data {
        if buffer.isEmpty { buffer = try handle.read(upToCount: 1 << 20) ?? Data() }
        let out = buffer.prefix(count)
        buffer.removeFirst(out.count)
        return Data(out)
    }
}

/// A compressed stream's bytes as it decompresses.
final class PayloadSource: ByteSource {
    private let file: ByteSource
    private var decoder: PayloadDecoder
    private var buffer = Data(), ended = false
    init(_ file: ByteSource, decoder: PayloadDecoder) {
        self.file = file
        self.decoder = decoder
    }

    func read(upTo count: Int) throws -> Data {
        while buffer.isEmpty && !ended {
            let compressed = try file.read(upTo: 1 << 20)
            if compressed.isEmpty {
                buffer = try decoder.finish()
                ended = true
            } else {
                buffer = try decoder.decode(compressed)
            }
        }
        let out = buffer.prefix(count)
        buffer.removeFirst(out.count)
        return Data(out)
    }
}
