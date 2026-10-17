import CryptoKit
import Foundation
import Testing
@testable import MacNeutronCore

/// Every file the reader hands over: its paths and its bytes.
private func read(_ rpm: URL) throws -> (RPMHeader?, [[String]: Data]) {
    var header: RPMHeader?, files: [[String]: Data] = [:]
    try RPMReader.readFiles(rpm) { h, paths, next in
        header = h
        var bytes = Data()
        while let chunk = try next() { bytes += chunk }
        files[paths] = bytes
    }
    return (header, files)
}

@Test func readsTheHeaderAndEveryFile() throws {
    let rpm = try makeTempDir().appending(path: "demo.rpm")
    try RPMFixture.write(to: rpm, files: [.init(path: "/usr/bin/demo", bytes: Data("#!/bin/sh\n".utf8), inode: 1),
                                          .init(path: "/etc/demo.conf", bytes: Data(repeating: 7, count: 300_000), inode: 2)])
    let (header, files) = try read(rpm)
    #expect(header == RPMHeader(name: "demo", version: "1.0", release: "1.fc44", arch: "aarch64", payloadCompressor: "xz"))
    #expect(files == [["/usr/bin/demo"]: Data("#!/bin/sh\n".utf8), ["/etc/demo.conf"]: Data(repeating: 7, count: 300_000)])
}

@Test func hardlinkedFilesAllGetTheirBytes() throws {
    let rpm = try makeTempDir().appending(path: "links.rpm")
    try RPMFixture.write(to: rpm, files: [.init(path: "/usr/bin/a", bytes: Data("same\n".utf8), inode: 5),
                                          .init(path: "/usr/bin/b", bytes: Data("same\n".utf8), inode: 5)])
    let (_, files) = try read(rpm)
    #expect(files == [["/usr/bin/a", "/usr/bin/b"]: Data("same\n".utf8)])
}

@Test func aNonCpioPayloadIsRefused() throws {
    let rpm = try makeTempDir().appending(path: "delta.rpm")
    try RPMFixture.write(to: rpm, payloadFormat: "drpm", files: [.init(path: "/x", bytes: Data(), inode: 1)])
    #expect(throws: VMSetupError.unsupported("drpm")) { try read(rpm) }
}

/// Real Fedora packages (kernel-core is xz, systemd zstd), when MN_RPM_SAMPLES names a folder holding them: the
/// pinned kernel comes out with the bytes of the image build's own copy.
@Test(.enabled(if: ProcessInfo.processInfo.environment["MN_RPM_SAMPLES"] != nil))
func realFedoraPackagesRead() throws {
    let folder = URL(filePath: ProcessInfo.processInfo.environment["MN_RPM_SAMPLES"]!, directoryHint: .isDirectory)
    let rpms = try FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
        .filter { $0.pathExtension == "rpm" }
    #expect(rpms.count >= 2)
    for rpm in rpms {
        var count = 0
        try RPMReader.readFiles(rpm) { _, paths, next in
            count += 1
            if paths.contains("/lib/modules/7.2.10-200.fc44.aarch64/vmlinuz") {
                var hash = SHA256()
                while let chunk = try next() { hash.update(data: chunk) }
                let expected = try Data(contentsOf: URL(filePath: ProcessInfo.processInfo.environment["MN_RPM_VMLINUZ"]!))
                #expect(Data(hash.finalize()) == Data(SHA256.hash(data: expected)))
            }
        }
        print("\(rpm.lastPathComponent): \(count) files")
        #expect(count > 0)
    }
}
