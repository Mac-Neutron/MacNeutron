import Foundation

/// The COFF machine type, signature and icon of a Windows executable, read from its headers and resources only (game
/// exes can be many GB).
public enum PEImage {
    public static let i386: UInt16 = 0x014c, amd64: UInt16 = 0x8664, arm64: UInt16 = 0xAA64

    /// The COFF `Machine` of a PE file, or nil when `url` isn't one (missing, unreadable, a script, truncated).
    /// ARM64EC images report `amd64`, ARM64X images `arm64`.
    public static func machine(of url: URL) -> UInt16? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        // read(upToCount:) returns fresh Data, so index 0 is the first byte.
        guard let dos = try? handle.read(upToCount: 64), dos.count == 64, dos[0] == 0x4D, dos[1] == 0x5A  // "MZ"
        else { return nil }
        let lfanew = (0..<4).reduce(UInt32(0)) { $0 | UInt32(dos[0x3C + $1]) << (8 * $1) }
        guard (try? handle.seek(toOffset: UInt64(lfanew))) != nil,
              let nt = try? handle.read(upToCount: 6), nt.count == 6, nt.starts(with: [0x50, 0x45, 0, 0])  // "PE\0\0"
        else { return nil }
        return UInt16(nt[4]) | UInt16(nt[5]) << 8
    }

    /// The optional header's Certificate Table (data directory 4) has a non-zero size: the file carries an Authenticode
    /// signature. Not a validity check (Wine's WinVerifyTrust, and SteamStub, do that).
    public static func hasCertificateTable(_ url: URL) -> Bool {
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe),
              let lfanew = data.le(at: 0x3C, 4), data.le(at: lfanew, 4) == 0x4550,  // "PE\0\0"
              let magic = data.le(at: lfanew + 24, 2), magic == 0x10B || magic == 0x20B else { return false }
        let optional = lfanew + 24, pe32 = magic == 0x10B
        guard let count = data.le(at: optional + (pe32 ? 92 : 108), 4), count > 4,
              let size = data.le(at: optional + (pe32 ? 128 : 144) + 4, 4) else { return false }
        return size > 0
    }

    /// The icon Windows shows for the file, its first RT_GROUP_ICON's largest image (the deepest on a tie), as a
    /// one-image .ico; nil when it has none. Mapped: only the headers and that resource's pages are read.
    public static func icon(of url: URL) -> Data? {
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe),
              let lfanew = data.le(at: 0x3C, 4), data.le(at: lfanew, 4) == 0x4550,
              let sections = data.le(at: lfanew + 6, 2), let optionalSize = data.le(at: lfanew + 20, 2),
              let magic = data.le(at: lfanew + 24, 2), magic == 0x10B || magic == 0x20B else { return nil }
        let optional = lfanew + 24, directories = optional + (magic == 0x10B ? 96 : 112)
        guard let count = data.le(at: directories - 4, 4), count > 2,
              let rsrcRVA = data.le(at: directories + 16, 4), rsrcRVA != 0 else { return nil }
        func file(_ rva: Int) -> Int? {  // through the section table
            for i in 0..<sections {
                let header = optional + optionalSize + 40 * i
                if let va = data.le(at: header + 12, 4), let raw = data.le(at: header + 16, 4),
                   let pointer = data.le(at: header + 20, 4), rva >= va, rva < va + raw { return pointer + rva - va }
            }
            return nil
        }
        guard let rsrc = file(rsrcRVA) else { return nil }
        // A resource directory's entry for `id`, or its first entry (a name or an ID) when nil.
        func entry(_ directory: Int, _ id: Int?) -> Int? {
            guard let named = data.le(at: directory + 12, 2), let ids = data.le(at: directory + 14, 2) else { return nil }
            for i in 0..<(named + ids) {
                guard let name = data.le(at: directory + 16 + 8 * i, 4),
                      let offset = data.le(at: directory + 20 + 8 * i, 4) else { return nil }
                if id == nil || name == id { return offset }
            }
            return nil
        }
        // Type, then name, then the first language: the data's file range.
        func resource(type: Int, id: Int?) -> Range<Int>? {
            let sub = 0x8000_0000
            guard let names = entry(rsrc, type), names & sub != 0,
                  let languages = entry(rsrc + (names & ~sub), id), languages & sub != 0,
                  let leaf = entry(rsrc + (languages & ~sub), nil), leaf & sub == 0,
                  let rva = data.le(at: rsrc + leaf, 4), let size = data.le(at: rsrc + leaf + 4, 4),
                  let offset = file(rva), offset + size <= data.count else { return nil }
            return offset..<(offset + size)
        }
        guard let group = resource(type: 14, id: nil), let n = data.le(at: group.lowerBound + 4, 2), n > 0,
              6 + 14 * n <= group.count else { return nil }
        // A width byte of 0 is 256.
        func rank(_ e: Int) -> (Int, Int) { ((Int(data[data.startIndex + e]) + 255) % 256, data.le(at: e + 6, 2) ?? 0) }
        let best = (0..<n).map { group.lowerBound + 6 + 14 * $0 }.max { rank($0) < rank($1) }!
        guard let id = data.le(at: best + 12, 2), let image = resource(type: 3, id: id) else { return nil }
        func le32(_ value: Int) -> Data { withUnsafeBytes(of: UInt32(value).littleEndian) { Data($0) } }
        let s = data.startIndex
        // ICONDIR, then the group entry's width, height, colours, reserved, planes and bit count, size and offset.
        return Data([0, 0, 1, 0, 1, 0]) + data[(s + best)..<(s + best + 8)] + le32(image.count) + le32(22)
            + data[(s + image.lowerBound)..<(s + image.upperBound)]
    }
}

private extension Data {
    /// The little-endian unsigned integer of `size` bytes at `offset`, or nil past the end.
    func le(at offset: Int, _ size: Int) -> Int? {
        guard offset >= 0, offset + size <= count else { return nil }
        return (0..<size).reduce(0) { $0 | Int(self[startIndex + offset + $1]) << (8 * $1) }
    }
}
