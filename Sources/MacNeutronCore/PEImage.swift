import Foundation

/// The COFF machine type of a Windows executable, read from its headers only (game exes can be many GB).
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
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe), data.count >= 64 else { return false }
        func value(at offset: Int, size: Int) -> Int? {
            guard offset >= 0, offset + size <= data.count else { return nil }
            return (0..<size).reduce(0) { $0 | Int(data[data.startIndex + offset + $1]) << (8 * $1) }
        }
        guard let lfanew = value(at: 0x3C, size: 4), value(at: lfanew, size: 4) == 0x4550,  // "PE\0\0"
              let magic = value(at: lfanew + 24, size: 2), magic == 0x10B || magic == 0x20B else { return false }
        let optional = lfanew + 24, pe32 = magic == 0x10B
        guard let count = value(at: optional + (pe32 ? 92 : 108), size: 4), count > 4,
              let size = value(at: optional + (pe32 ? 128 : 144) + 4, size: 4) else { return false }
        return size > 0
    }
}
