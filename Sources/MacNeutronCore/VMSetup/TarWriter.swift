import Foundation

/// A POSIX pax tar of recipe entries, for `mkfs.erofs --tar=f`: a pax record for a path or link over 100 bytes, for
/// owners or sizes too big for ustar's octal fields, and for a file capability (`SCHILY.xattr.security.capability`).
/// Times are 0: images compare by content. Paths are written relative ("usr/bin/x", the root as "./").
struct TarWriter {
    private let out: FileHandle
    init(_ out: FileHandle) { self.out = out }

    /// `contents` for a file: its bytes, whose size goes in the header.
    mutating func add(_ entry: RecipeEntry, contents: URL?) throws {
        var name = entry.path == "/" ? "./" : String(entry.path.drop { $0 == "/" })
        var type: UInt8 = UInt8(ascii: "0"), link = "", size: Int64 = 0, device: (UInt32, UInt32) = (0, 0)
        switch entry.kind {
        case .directory:
            type = UInt8(ascii: "5")
            if !name.hasSuffix("/") { name += "/" }
        case .symlink(let target):
            type = UInt8(ascii: "2")
            link = target
        case .device(let block, let major, let minor):
            type = UInt8(ascii: block ? "4" : "3")
            device = (major, minor)
        case .file:
            guard let contents else { throw CocoaError(.fileNoSuchFile) }
            size = (try FileManager.default.attributesOfItem(atPath: contents.path(percentEncoded: false))[.size]
                as? NSNumber)?.int64Value ?? 0
        }
        var records: [(String, Data)] = []
        if name.utf8.count > 100 { records.append(("path", Data(name.utf8))) }
        if link.utf8.count > 100 { records.append(("linkpath", Data(link.utf8))) }
        if entry.uid > 0o7777777 { records.append(("uid", Data(String(entry.uid).utf8))) }
        if entry.gid > 0o7777777 { records.append(("gid", Data(String(entry.gid).utf8))) }
        if size > 0o77777777777 { records.append(("size", Data(String(size).utf8))) }
        if let capability = entry.capability { records.append(("SCHILY.xattr.security.capability", capability)) }
        if !records.isEmpty {
            let body = records.map(Self.record).reduce(Data(), +)
            try write(header(name: "PaxHeaders/" + String(name.suffix(80)), mode: 0o644, uid: 0, gid: 0,
                             size: Int64(body.count), type: UInt8(ascii: "x"), link: ""))
            try write(padded(body))
        }
        try write(header(name: name, mode: entry.mode, uid: entry.uid, gid: entry.gid, size: size, type: type, link: link,
                         device: device))
        if let contents, case .file = entry.kind {
            let file = try FileHandle(forReadingFrom: contents)
            defer { try? file.close() }
            var written: Int64 = 0
            while let chunk = try file.read(upToCount: 1 << 20), !chunk.isEmpty {
                try write(chunk)
                written += Int64(chunk.count)
            }
            try write(Data(count: Int((512 - written % 512) % 512)))
        }
    }

    mutating func finish() throws { try write(Data(count: 1024)) }

    private func write(_ data: Data) throws { try out.write(contentsOf: data) }

    /// "<length> <key>=<value>\n", the length counting itself.
    private static func record(_ item: (String, Data)) -> Data {
        let rest = Data(" \(item.0)=".utf8) + item.1 + Data("\n".utf8)
        var length = rest.count + 1
        while String(length).count + rest.count != length { length = String(length).count + rest.count }
        return Data(String(length).utf8) + rest
    }

    private func padded(_ data: Data) -> Data { data + Data(count: (512 - data.count % 512) % 512) }

    private func header(name: String, mode: UInt16, uid: UInt32, gid: UInt32, size: Int64, type: UInt8,
                        link: String, device: (UInt32, UInt32) = (0, 0)) -> Data {
        var h = Data(count: 512)
        func put(_ text: String, _ at: Int, _ width: Int) {
            let bytes = Array(text.utf8.prefix(width))
            h.replaceSubrange(at..<at + bytes.count, with: bytes)
        }
        func octal(_ value: Int64, _ at: Int, _ width: Int) {
            let digits = String(value, radix: 8)
            put(String(repeating: "0", count: max(0, width - 1 - digits.count)) + digits, at, width - 1)
        }
        put(name, 0, 100)
        octal(Int64(mode), 100, 8)
        octal(Int64(min(uid, 0o7777777)), 108, 8)
        octal(Int64(min(gid, 0o7777777)), 116, 8)
        octal(min(size, 0o77777777777), 124, 12)
        octal(0, 136, 12)
        h[156] = type
        put(link, 157, 100)
        put("ustar", 257, 6)
        put("00", 263, 2)
        octal(Int64(device.0), 329, 8)
        octal(Int64(device.1), 337, 8)
        put("        ", 148, 8)
        let sum = h.reduce(0) { $0 + Int64($1) }
        octal(sum, 148, 7)
        h[155] = UInt8(ascii: " ")
        return h
    }
}
