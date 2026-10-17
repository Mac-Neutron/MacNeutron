import Foundation
@testable import MacNeutronCore

/// Writes a small RPM the way rpm lays one out: the lead, an empty signature header (padded to 8 bytes), a header with
/// the tags RPMReader reads, and an xz-compressed newc cpio payload. Files sharing an inode are hardlinks: newc carries
/// their bytes on the last one only.
enum RPMFixture {
    struct File { var path: String; var bytes: Data; var inode: UInt32; var mode: UInt32 = 0o100644 }

    static func write(to url: URL, name: String = "demo", version: String = "1.0", release: String = "1.fc44",
                      arch: String = "aarch64", payloadFormat: String = "cpio", files: [File]) throws {
        var rpm = Data(count: 96)
        rpm.replaceSubrange(0..<4, with: [0xed, 0xab, 0xee, 0xdb])
        rpm += header([])  // 16 bytes: already a multiple of 8
        rpm += header([(1000, name), (1001, version), (1002, release), (1022, arch), (1124, payloadFormat),
                       (1125, "xz")])
        var cpio = Data()
        let links = Dictionary(grouping: files, by: \.inode).mapValues(\.count)
        var seen: [UInt32: Int] = [:]
        for file in files {
            seen[file.inode, default: 0] += 1
            let last = seen[file.inode] == links[file.inode]
            entry(&cpio, name: "." + file.path, inode: file.inode, mode: file.mode,
                  nlink: UInt32(links[file.inode]!), bytes: last ? file.bytes : Data())
        }
        entry(&cpio, name: "TRAILER!!!", inode: 0, mode: 0, nlink: 1, bytes: Data())
        rpm += try (cpio as NSData).compressed(using: .lzma) as Data
        try rpm.write(to: url)
    }

    /// A header of STRING tags: magic, index entries, then the store.
    private static func header(_ tags: [(UInt32, String)]) -> Data {
        var index = Data(), store = Data()
        for (tag, value) in tags {
            for field in [tag, 6, UInt32(store.count), 1] { index += be(field) }
            store += Data(value.utf8) + [0]
        }
        return Data([0x8e, 0xad, 0xe8, 0x01, 0, 0, 0, 0]) + be(UInt32(tags.count)) + be(UInt32(store.count)) + index + store
    }

    private static func entry(_ cpio: inout Data, name: String, inode: UInt32, mode: UInt32, nlink: UInt32, bytes: Data) {
        let fields = [inode, mode, 0, 0, nlink, 0, UInt32(bytes.count), 0, 0, 0, 0, UInt32(name.utf8.count + 1), 0]
        cpio += Data(("070701" + fields.map { String(format: "%08X", $0) }.joined()).utf8)
        cpio += Data(name.utf8) + [0]
        cpio += Data(count: (4 - cpio.count % 4) % 4)
        cpio += bytes
        cpio += Data(count: (4 - cpio.count % 4) % 4)
    }

    private static func be(_ value: UInt32) -> Data { withUnsafeBytes(of: value.bigEndian) { Data($0) } }
}
