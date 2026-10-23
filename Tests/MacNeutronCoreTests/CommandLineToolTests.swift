import Foundation
import Testing
@testable import MacNeutronCore

@Test func unknownCommandPrintsUsage() async {
    #expect(await CommandLineTool.run(["frobnicate"], environment: [:], executable: URL(filePath: "/x")) == 2)
    #expect(await CommandLineTool.run([], environment: [:], executable: URL(filePath: "/x")) == 2)
    #expect(CommandLineTool.usage.contains("macneutron install --tool-dir"))
    #expect(CommandLineTool.usage.contains("macneutron passthrough <verb>"))
}

@Test func optionParsingRemovesTheFlagAndValue() {
    var args = ["--tool-dir", "/a b/tool", "/Volumes/GPTK"]
    #expect(CommandLineTool.option("--tool-dir", in: &args) == "/a b/tool")
    #expect(args == ["/Volumes/GPTK"])
    #expect(CommandLineTool.option("--tarball", in: &args) == nil)
}

// MARK: install

@Test func installRequiresToolDir() async throws {
    // Never the real tool folder by default: the checks assemble their own.
    let dir = try makeTempDir()
    let source = try makeSignedWineApp(at: dir)
    let before = try FileManager.default.contentsOfDirectory(atPath: dir.path(percentEncoded: false))
    #expect(await CommandLineTool.run(["install", "--wine-app", source.path(percentEncoded: false)],
                                      environment: [:], executable: URL(filePath: "/x")) == 2)
    #expect(try FileManager.default.contentsOfDirectory(atPath: dir.path(percentEncoded: false)) == before)
}

@Test func installRequiresWineApp() async throws {
    let tool = try makeTempDir().appending(path: "tool dir", directoryHint: .isDirectory)
    #expect(await CommandLineTool.run(["install", "--tool-dir", tool.path(percentEncoded: false)],
                                      environment: [:], executable: URL(filePath: "/x")) == 2)
    #expect(!FileManager.default.fileExists(atPath: tool.path(percentEncoded: false)))
}

@Test func aMissingSteamExeChangesNothing() async throws {
    let tool = try makeTempDir().appending(path: "tool dir", directoryHint: .isDirectory)
    let source = try makeSignedWineApp(at: makeTempDir())
    #expect(await CommandLineTool.run(["install", "--tool-dir", tool.path(percentEncoded: false),
                                       "--wine-app", source.path(percentEncoded: false), "--steam-exe", "/no such/steam.exe"],
                                      environment: [:], executable: URL(filePath: "/x")) == 1)
    #expect(!FileManager.default.fileExists(atPath: tool.path(percentEncoded: false)))
}

@Test func installExitCodes() {
    #expect(CommandLineTool.installExitCode(.installed) == 0)
    #expect(CommandLineTool.installExitCode(.unchanged) == 0)
    #expect(CommandLineTool.installExitCode(.deferred("/t/wine.app/Contents/MacOS/wine")) == 3)
}

@Test func installMessages() {
    let label = "dev (0123456789ab)"
    #expect(CommandLineTool.installMessage(.installed, label: label) == "installed dev (0123456789ab)")
    #expect(CommandLineTool.installMessage(.unchanged, label: label) == "unchanged dev (0123456789ab)")
    #expect(CommandLineTool.installMessage(.deferred("/t/wine.app/Contents/MacOS/wine"), label: label)
        == "deferred: /t/wine.app/Contents/MacOS/wine is running")
}

// MARK: passthrough

@Test func appBundleResolvesToItsExecutable() throws {
    // Steam's launch entry for many Mac games is the .app folder itself (Timberborn's is).
    let app = try makeTempDir().appending(path: "Gamé Folder/My Game.app", directoryHint: .isDirectory)
    try write("#!/bin/sh\n", to: app.appending(path: "Contents/MacOS/Game Binary"), executable: true)
    try PropertyListSerialization.data(fromPropertyList: ["CFBundleExecutable": "Game Binary"], format: .xml, options: 0)
        .write(to: app.appending(path: "Contents/Info.plist"))
    #expect(CommandLineTool.passthroughTarget(app.path(percentEncoded: false)).path(percentEncoded: false)
        == app.appending(path: "Contents/MacOS/Game Binary").path(percentEncoded: false))
}

@Test func appWithoutPlistFallsBackToItsName() throws {
    let app = try makeTempDir().appending(path: "My Game.app", directoryHint: .isDirectory)
    try write("#!/bin/sh\n", to: app.appending(path: "Contents/MacOS/My Game"), executable: true)
    #expect(CommandLineTool.passthroughTarget(app.path(percentEncoded: false)).path(percentEncoded: false)
        == app.appending(path: "Contents/MacOS/My Game").path(percentEncoded: false))
}

@Test func plainPathIsKept() {
    #expect(CommandLineTool.passthroughTarget("/bin/echo").path(percentEncoded: false) == "/bin/echo")
    #expect(CommandLineTool.passthroughTarget("/no such/game").path(percentEncoded: false) == "/no such/game")
}

@Test func preferenceIsArm64eArm64ThenX86() {
    // What `arch -arm64e -arm64 -x86_64` does; Steam itself spawns tools preferring x86_64.
    #expect(CommandLineTool.passthroughArchitectures.map(\.0) == [CPU_TYPE_ARM64, CPU_TYPE_ARM64, CPU_TYPE_X86_64])
    #expect(CommandLineTool.passthroughArchitectures.map(\.1)
        == [CPU_SUBTYPE_ARM64E, CPU_SUBTYPE_ARM64_ALL, CPU_SUBTYPE_X86_64_ALL])
}

@Test func spawnRunsTheTargetAndReturnsItsPid() throws {
    // /usr/bin/arch is universal (arm64e, x86_64); it prints the slice that runs (no newline unless to a terminal).
    let out = try makeTempDir().appending(path: "arch out.txt")
    let pid = try CommandLineTool.spawn(URL(filePath: "/usr/bin/arch"), [], environment: ["PATH": "/usr/bin:/bin"],
                                        replacingThisProcess: false, standardOutput: out)
    var status: Int32 = 0
    #expect(waitpid(pid, &status, 0) == pid)
    #expect(status == 0)
    #expect(try String(contentsOf: out, encoding: .utf8) == "arm64")
}

@Test func vmSetupNeedsAManifestAndAFolder() async throws {
    // Never the real VM folder by default.
    #expect(await CommandLineTool.run(["vm-setup", "--manifest", "/nonexistent.json"], environment: [:],
                                      executable: URL(filePath: "/x")) == 2)
    #expect(await CommandLineTool.run(["vm-setup", "--vm", try makeTempDir().path], environment: [:],
                                      executable: URL(filePath: "/x")) == 2)
    // With both, it runs: a developer's folder (images saying `dev`) is left alone, and that's a success.
    let link = try makeTempDir().appending(path: "vm"), work = try makeTempDir()
    try write("dev\n", to: work.appending(path: "images/manifest.sha256"))
    try FileManager.default.createSymbolicLink(at: link, withDestinationURL: work)
    let manifest = try makeTempDir().appending(path: "vm-manifest.json")
    try write("{}", to: manifest)
    #expect(await CommandLineTool.run(["vm-setup", "--manifest", manifest.path, "--vm", link.path], environment: [:],
                                      executable: URL(filePath: "/x")) == 0)
}
