import Foundation
import Testing
@testable import MacNeutronCore

private func cdHash(_ bundle: URL) throws -> String? {
    let log = bundle.deletingLastPathComponent().appending(path: "codesign-d.log")
    try? FileManager.default.removeItem(at: log)
    _ = try SystemProcessRunner().run(URL(filePath: "/usr/bin/codesign"), ["-dvvv", bundle.path(percentEncoded: false)],
                                      environment: [:], output: log)
    return try String(contentsOf: log, encoding: .utf8).split(separator: "\n")
        .first { $0.hasPrefix("CDHash=") }.map { String($0.dropFirst("CDHash=".count)) }
}

@Test func identityIs40HexAndMatchesCodesign() throws {
    let bundle = try makeSignedWineApp(at: try makeTempDir())
    let identity = try #require(CodeIdentity.of(bundle))
    #expect(identity.count == 40)
    #expect(identity.allSatisfy { "0123456789abcdef".contains($0) })
    #expect(identity == (try cdHash(bundle)))
}

@Test func bundlesDifferingOnlyInTheLoaderDiffer() throws {
    let a = try makeSignedWineApp(at: try makeTempDir())
    let b = try makeSignedWineApp(at: try makeTempDir(), loader: URL(filePath: "/usr/bin/false"))
    #expect(CodeIdentity.of(a) != nil)
    #expect(CodeIdentity.of(a) != CodeIdentity.of(b))
}

@Test func bundlesDifferingOnlyInInfoPlistDiffer() throws {
    let a = try makeSignedWineApp(at: try makeTempDir())
    let b = try makeSignedWineApp(at: try makeTempDir(), bundleVersion: "2")
    #expect(CodeIdentity.of(a) != nil)
    #expect(CodeIdentity.of(a) != CodeIdentity.of(b))
}

@Test func resigningIdenticalBitsKeepsTheIdentity() throws {
    let dir = try makeTempDir()
    let bundle = try makeSignedWineApp(at: dir)
    let before = try #require(CodeIdentity.of(bundle))
    let status = try SystemProcessRunner().run(URL(filePath: "/usr/bin/codesign"),
                                               ["-s", "-", "-f", bundle.path(percentEncoded: false)],
                                               environment: [:], output: dir.appending(path: "resign.log"))
    #expect(status == 0)
    #expect(CodeIdentity.of(bundle) == before)
}

@Test func unsignedOrMissingBundleHasNoIdentity() throws {
    let dir = try makeTempDir()
    #expect(CodeIdentity.of(dir.appending(path: "missing.app")) == nil)
    let unsigned = dir.appending(path: "unsigned.app", directoryHint: .isDirectory)
    let macOS = unsigned.appending(path: "Contents/MacOS", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: macOS, withIntermediateDirectories: true)
    try PropertyListSerialization.data(fromPropertyList: ["CFBundleExecutable": "wine"], format: .xml, options: 0)
        .write(to: unsigned.appending(path: "Contents/Info.plist"))
    try write("#!/bin/sh\n", to: macOS.appending(path: "wine"), executable: true)
    #expect(CodeIdentity.of(unsigned) == nil)
}

@Test func adHocSignedAppsHaveNoTeam() throws {
    #expect(CodeIdentity.team(of: try makeSignedWineApp(at: try makeTempDir())) == nil)
}
