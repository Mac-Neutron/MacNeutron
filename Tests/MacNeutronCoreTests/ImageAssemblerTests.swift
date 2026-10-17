import CryptoKit
import Foundation
import Testing
@testable import MacNeutronCore

private func sha(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }

/// Stands in for mkfs.erofs (`--tar=f -L <label> -zlz4hc <out> [tar]`): the tar it's given becomes <out>.
private func fakeMkfs() throws -> URL {
    let tool = try makeTempDir().appending(path: "mkfs.erofs")
    try write("#!/bin/sh\nif [ -n \"$6\" ]; then cp \"$6\" \"$5\"; else cat > \"$5\"; fi\n", to: tool, executable: true)
    return tool
}

private func tar(_ arguments: [String]) throws -> Data {
    let p = Process(), out = Pipe()
    p.executableURL = URL(filePath: "/usr/bin/tar")
    p.arguments = arguments
    p.standardOutput = out
    try p.run()
    let data = out.fileHandleForReading.readDataToEndOfFile()
    p.waitUntilExit()
    return data
}

private func dir(_ path: String) -> RecipeEntry {
    RecipeEntry(path: path, mode: 0o755, uid: 0, gid: 0, kind: .directory, capability: nil)
}

private func file(_ path: String, _ bytes: Data, _ source: RecipeEntry.Source, mode: UInt16 = 0o644) -> RecipeEntry {
    RecipeEntry(path: path, mode: mode, uid: 0, gid: 0, kind: .file(size: Int64(bytes.count), sha256: sha(bytes), source: source),
                capability: nil)
}

private struct Fixture {
    let assembler: ImageAssembler
    let rpm: URL
    let overlay: URL
}

/// A package with /usr/bin/demo and the two /a files, and an overlay holding "from the overlay\n".
private func makeFixture() throws -> Fixture {
    let tmp = try makeTempDir()
    let rpm = tmp.appending(path: "demo-1.0-1.fc44.aarch64.rpm")
    try RPMFixture.write(to: rpm, files: [.init(path: "/usr/bin/demo", bytes: Data("demo\n".utf8), inode: 1, mode: 0o100755),
                                          .init(path: "/a/Readme", bytes: Data("lower\n".utf8), inode: 2),
                                          .init(path: "/a/README", bytes: Data("UPPER\n".utf8), inode: 3)])
    let overlay = tmp.appending(path: "overlay", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: overlay, withIntermediateDirectories: true)
    try Data("from the overlay\n".utf8).write(to: overlay.appending(path: sha(Data("from the overlay\n".utf8))))
    return Fixture(assembler: ImageAssembler(store: tmp.appending(path: "store", directoryHint: .isDirectory)), rpm: rpm,
                   overlay: overlay)
}

@Test func aRecipeBecomesATarOfPackageAndOverlayFiles() throws {
    let f = try makeFixture()
    let pkg = RecipeEntry.Source.package(f.rpm.lastPathComponent)
    let recipe = [dir("/"), dir("/usr"), dir("/usr/bin"), file("/usr/bin/demo", Data("demo\n".utf8), pkg, mode: 0o755),
                  dir("/etc"), file("/etc/motd", Data("from the overlay\n".utf8), .overlay)]
    try f.assembler.collect(f.rpm, wanted: Set(recipe.compactMap { if case .file(_, let s, _) = $0.kind { s } else { nil } }))
    try f.assembler.collectOverlay(f.overlay)
    let out = try makeTempDir().appending(path: "base.erofs")
    try f.assembler.erofs(recipe, label: "mn-base", mkfs: try fakeMkfs(), out: out)
    let listing = String(decoding: try tar(["-tvf", out.path]), as: UTF8.self)
    #expect(listing.contains("-rwxr-xr-x  0 0      0           5 ") && listing.contains(" usr/bin/demo\n"))
    #expect(try tar(["-xOf", out.path, "etc/motd"]) == Data("from the overlay\n".utf8))
    #expect(try tar(["-xOf", out.path, "usr/bin/demo"]) == Data("demo\n".utf8))
}

@Test func aMissingFileFailsWithItsPath() throws {
    let f = try makeFixture()
    let recipe = [dir("/"), dir("/etc"), file("/etc/x", Data("nobody has this\n".utf8), .overlay)]
    try f.assembler.collectOverlay(f.overlay)
    let out = try makeTempDir().appending(path: "base.erofs")
    #expect(throws: VMSetupError.missing(path: "/etc/x")) {
        try f.assembler.erofs(recipe, label: "mn-base", mkfs: try fakeMkfs(), out: out)
    }
}

@Test func pathsDifferingOnlyInCaseBothSurvive() throws {
    let f = try makeFixture()
    let pkg = RecipeEntry.Source.package(f.rpm.lastPathComponent)
    let recipe = [dir("/"), dir("/a"), file("/a/Readme", Data("lower\n".utf8), pkg), file("/a/README", Data("UPPER\n".utf8), pkg)]
    try f.assembler.collect(f.rpm, wanted: [sha(Data("lower\n".utf8)), sha(Data("UPPER\n".utf8))])
    let out = try makeTempDir().appending(path: "base.erofs")
    try f.assembler.erofs(recipe, label: "mn-base", mkfs: try fakeMkfs(), out: out)
    #expect(try tar(["-xOf", out.path, "a/Readme"]) == Data("lower\n".utf8))
    #expect(try tar(["-xOf", out.path, "a/README"]) == Data("UPPER\n".utf8))
}

@Test func extractChecksTheBytes() throws {
    let f = try makeFixture()
    let to = try makeTempDir().appending(path: "demo")
    try f.assembler.extract(f.rpm, path: "/usr/bin/demo", to: to, sha256: sha(Data("demo\n".utf8)))
    #expect(try Data(contentsOf: to) == Data("demo\n".utf8))
    #expect(throws: VMSetupError.mismatch(file: "/usr/bin/demo")) {
        try f.assembler.extract(f.rpm, path: "/usr/bin/demo", to: to, sha256: sha(Data("other\n".utf8)))
    }
}

@Test func theInitramfsIsANewcCpio() throws {
    let f = try makeFixture()
    try f.assembler.collectOverlay(f.overlay)
    let out = try makeTempDir().appending(path: "initramfs.img")
    try f.assembler.cpio([dir("/"), dir("/etc"), file("/etc/motd", Data("from the overlay\n".utf8), .overlay)], out: out)
    let bytes = try Data(contentsOf: out)
    #expect(bytes.prefix(6) == Data("070701".utf8))
    #expect(bytes.range(of: Data("TRAILER!!!".utf8)) != nil)
}
