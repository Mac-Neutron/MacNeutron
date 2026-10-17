import Foundation

/// Bytes read in order: a file's, or a payload's as it decompresses.
protocol ByteSource: AnyObject {
    /// Up to `count` bytes; empty only at the end.
    func read(upTo count: Int) throws -> Data
}

extension ByteSource {
    /// Exactly `count` bytes; `.truncated` if the source ends first.
    func read(exactly count: Int) throws -> Data {
        var out = Data()
        while out.count < count {
            let chunk = try read(upTo: count - out.count)
            if chunk.isEmpty { throw VMSetupError.truncated }
            out += chunk
        }
        return out
    }
}

/// A newc ("070701") cpio archive, as RPM payloads are: each regular file once, with every path that shares its bytes
/// (hardlinks: newc carries the bytes on the set's last entry), and a reader of those bytes. Directories, symlinks and
/// special files are skipped: the recipe carries them.
enum CPIOReader {
    static func read(_ source: ByteSource, _ body: (_ paths: [String], _ next: () throws -> Data?) throws -> Void) throws {
        var held: [UInt32: [String]] = [:]  // hardlinks whose bytes come later
        while true {
            let header = try source.read(exactly: 110)
            guard header.prefix(6) == Data("070701".utf8) else { throw VMSetupError.corrupt }
            let field = { (i: Int) throws -> UInt32 in
                guard let value = UInt32(String(decoding: header[(6 + i * 8)..<(14 + i * 8)], as: UTF8.self), radix: 16)
                else { throw VMSetupError.corrupt }
                return value
            }
            let (inode, mode, nlink, size, nameSize) = (try field(0), try field(1), try field(4), Int(try field(6)), Int(try field(11)))
            let name = String(decoding: try source.read(exactly: nameSize).prefix { $0 != 0 }, as: UTF8.self)
            _ = try source.read(exactly: (4 - (110 + nameSize) % 4) % 4)
            if name == "TRAILER!!!" { break }
            let path = name.hasPrefix("./") ? String(name.dropFirst()) : name.hasPrefix("/") ? name : "/" + name
            var paths = [path]
            if mode & 0o170000 != 0o100000 {
                paths = []
            } else if nlink > 1 {
                paths = held[inode, default: []] + [path]
                if size == 0 && paths.count < nlink {
                    held[inode] = paths
                    paths = []
                } else {
                    held[inode] = nil
                }
            }
            try emit(paths, size: size, from: source, body)
        }
        for paths in held.values { try body(paths) { nil } }  // an empty file's links: no entry carried bytes
    }

    /// The file's bytes to `body` when it has paths, then whatever it left unread and the padding.
    private static func emit(_ paths: [String], size: Int, from source: ByteSource,
                             _ body: ([String], () throws -> Data?) throws -> Void) throws {
        var left = size
        let next = { () throws -> Data? in
            guard left > 0 else { return nil }
            let chunk = try source.read(upTo: min(left, 1 << 20))
            if chunk.isEmpty { throw VMSetupError.truncated }
            left -= chunk.count
            return chunk
        }
        if !paths.isEmpty { try body(paths, next) }
        while try next() != nil {}
        _ = try source.read(exactly: (4 - size % 4) % 4)
    }
}

/// A newc cpio of recipe entries, uncompressed: the initramfs (Fedora's kernel takes one as is). Paths are written
/// relative ("usr/bin/x", the root as "."); capabilities don't apply to an initramfs.
struct CPIOWriter {
    private let out: FileHandle
    private var inode: UInt32 = 0
    private var written = 0
    init(_ out: FileHandle) { self.out = out }

    mutating func add(_ entry: RecipeEntry, contents: URL?) throws {
        inode += 1
        let name = entry.path == "/" ? "." : String(entry.path.drop { $0 == "/" })
        switch entry.kind {
        case .directory:
            try write(name: name, mode: 0o040000 | UInt32(entry.mode), entry: entry, nlink: 2, bytes: Data())
        case .symlink(let target):
            try write(name: name, mode: 0o120000 | UInt32(entry.mode), entry: entry, nlink: 1, bytes: Data(target.utf8))
        case .device(let block, let major, let minor):
            try write(name: name, mode: (block ? 0o060000 : 0o020000) | UInt32(entry.mode), entry: entry, nlink: 1,
                      bytes: Data(), rdev: (major, minor))
        case .file:
            guard let contents else { throw CocoaError(.fileNoSuchFile) }
            try write(name: name, mode: 0o100000 | UInt32(entry.mode), entry: entry, nlink: 1,
                      bytes: try Data(contentsOf: contents))
        }
    }

    mutating func finish() throws {
        inode = 0
        try write(name: "TRAILER!!!", mode: 0, entry: nil, nlink: 1, bytes: Data())
    }

    private mutating func write(name: String, mode: UInt32, entry: RecipeEntry?, nlink: UInt32, bytes: Data,
                                rdev: (UInt32, UInt32) = (0, 0)) throws {
        let fields: [UInt32] = [inode, mode, entry?.uid ?? 0, entry?.gid ?? 0, nlink, 0, UInt32(bytes.count), 0, 0,
                                rdev.0, rdev.1, UInt32(name.utf8.count + 1), 0]
        var record = Data(("070701" + fields.map { String(format: "%08X", $0) }.joined()).utf8) + Data(name.utf8) + [0]
        record += Data(count: (4 - (written + record.count) % 4) % 4)
        record += bytes
        record += Data(count: (4 - (written + record.count) % 4) % 4)
        try out.write(contentsOf: record)
        written += record.count
    }
}
