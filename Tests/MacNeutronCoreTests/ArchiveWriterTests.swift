import Foundation
import Testing
@testable import MacNeutronCore

private let longPath = "/usr/share/" + String(repeating: "deep/", count: 38) + "file.txt"  // over 200 bytes

/// A directory, a 200-byte path, a symlink and a capability, written by `write` into `archive`; contents for files.
private func sample(_ write: (RecipeEntry, URL?) throws -> Void) throws {
    let contents = try makeTempDir().appending(path: "hello")
    try Data("hello\n".utf8).write(to: contents)
    let file = { (path: String, mode: UInt16, cap: Data?) in
        RecipeEntry(path: path, mode: mode, uid: 0, gid: 0, kind: .file(size: 6, sha256: "-", source: .overlay), capability: cap)
    }
    try write(RecipeEntry(path: "/", mode: 0o755, uid: 0, gid: 0, kind: .directory, capability: nil), nil)
    try write(RecipeEntry(path: "/usr", mode: 0o755, uid: 0, gid: 0, kind: .directory, capability: nil), nil)
    try write(file("/usr/hello", 0o644, nil), contents)
    try write(file(longPath, 0o600, nil), contents)
    try write(file("/usr/ping", 0o4755, Data([1, 0, 0, 2, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])), contents)
    try write(RecipeEntry(path: "/usr/link", mode: 0o777, uid: 0, gid: 0, kind: .symlink("hello"), capability: nil), nil)
}

private func listing(_ archive: URL) throws -> String {
    let tar = Process(), out = Pipe()
    tar.executableURL = URL(filePath: "/usr/bin/tar")
    tar.arguments = ["-tvf", archive.path]
    tar.standardOutput = out
    try tar.run()
    let text = String(decoding: out.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    tar.waitUntilExit()
    #expect(tar.terminationStatus == 0)
    return text
}

@Test func tarRoundTripsThroughBsdtar() throws {
    let archive = try makeTempDir().appending(path: "t.tar")
    FileManager.default.createFile(atPath: archive.path, contents: nil)
    var tar = TarWriter(try FileHandle(forWritingTo: archive))
    try sample { try tar.add($0, contents: $1) }
    try tar.finish()
    let text = try listing(archive)
    #expect(text.contains("-rw-r--r--  0 0      0           6 "))
    #expect(text.contains(" usr/hello\n"))
    #expect(text.contains(String(longPath.dropFirst())))
    #expect(text.contains("-rwsr-xr-x"))
    #expect(text.contains("usr/link -> hello"))
}

@Test func cpioRoundTripsThroughBsdtar() throws {
    let archive = try makeTempDir().appending(path: "t.cpio")
    FileManager.default.createFile(atPath: archive.path, contents: nil)
    var cpio = CPIOWriter(try FileHandle(forWritingTo: archive))
    try sample { try cpio.add($0, contents: $1) }
    try cpio.finish()
    let text = try listing(archive)
    #expect(text.contains(" usr/hello\n"))
    #expect(text.contains("usr/link -> hello"))
    #expect(try Data(contentsOf: archive).prefix(6) == Data("070701".utf8))
}

@Test func theCapabilityIsAPaxRecord() throws {
    let archive = try makeTempDir().appending(path: "t.tar")
    FileManager.default.createFile(atPath: archive.path, contents: nil)
    var tar = TarWriter(try FileHandle(forWritingTo: archive))
    try sample { try tar.add($0, contents: $1) }
    try tar.finish()
    let bytes = try Data(contentsOf: archive)
    let record = Data("SCHILY.xattr.security.capability=".utf8) + Data([1, 0, 0, 2, 0, 0x20])
    #expect(bytes.range(of: record) != nil)
}

@Test func aDeviceNodeIsWrittenWithItsNumbers() throws {
    let console = RecipeEntry(path: "/dev/console", mode: 0o600, uid: 0, gid: 5,
                              kind: .device(block: false, major: 5, minor: 1), capability: nil)
    let cpioFile = try makeTempDir().appending(path: "d.cpio"), tarFile = try makeTempDir().appending(path: "d.tar")
    for file in [cpioFile, tarFile] { FileManager.default.createFile(atPath: file.path, contents: nil) }
    var cpio = CPIOWriter(try FileHandle(forWritingTo: cpioFile))
    try cpio.add(console, contents: nil)
    try cpio.finish()
    var tar = TarWriter(try FileHandle(forWritingTo: tarFile))
    try tar.add(console, contents: nil)
    try tar.finish()
    for file in [cpioFile, tarFile] {
        let text = try listing(file)
        #expect(text.hasPrefix("crw-------"))
        #expect(text.contains("5,1") || text.contains("5, 1"))
    }
}
