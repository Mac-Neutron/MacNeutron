import Foundation
import Testing
@testable import MacNeutronCore

private func request(_ verb: Verb, _ target: URL) -> LaunchRequest {
    LaunchRequest(verb: verb, target: target.path(percentEncoded: false), arguments: [])
}

private func exe(machine: UInt16) throws -> URL {
    let url = try makeTempDir().appending(path: "My Game.exe")
    try peBytes(machine: machine).write(to: url)
    return url
}

private func markerExists(_ layout: ToolLayout) -> Bool {
    FileManager.default.fileExists(atPath: layout.runtimeDamagedMarker.path(percentEncoded: false))
}

@Test func unsupportedSystemIsRefusedFirst() throws {
    let layout = ToolLayout(root: try makeTempDir())  // no wine.app either
    let preflight = Preflight(systemSupported: { false }, identity: { _ in nil })
    #expect(throws: PreflightError.unsupportedSystem) {
        try preflight.check(layout, request: request(.waitforexitandrun, try exe(machine: PEImage.i386)))
    }
    #expect(!markerExists(layout))
    #expect(PreflightError.unsupportedSystem.description == "MacNeutron needs macOS 27 or later on an Apple Silicon Mac.")
}

@Test func unreadableIdentityIsDamagedAndMarked() throws {
    let layout = try makeToolLayout()
    let preflight = Preflight(systemSupported: { true }, identity: { _ in nil })
    #expect(throws: PreflightError.runtimeMissing) {
        try preflight.check(layout, request: request(.run, layout.wine))
    }
    #expect(markerExists(layout))
    #expect(PreflightError.runtimeMissing.description
        == "MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it.")
}

@Test func missingWineserverIsDamagedAndMarked() throws {
    let layout = try makeToolLayout()
    try FileManager.default.removeItem(at: layout.wineserver)
    #expect(throws: PreflightError.runtimeMissing) {
        try testPreflight.check(layout, request: request(.run, layout.wine))
    }
    #expect(markerExists(layout))
}

// A runtime built for one CPU (Resources/cpu: its name, then the features its code may use) refuses a Mac without one
// of them, before anything runs; the generic build has no such file.
@Test func aRuntimeBuiltForANewerCPUIsRefused() throws {
    let layout = try makeToolLayout()
    try "apple-m5\nFEAT_CSSC\nFEAT_SME2\nFEAT_HBC\n".write(to: layout.cpuFile, atomically: true, encoding: .utf8)
    let m4 = Preflight(systemSupported: { true }, identity: { _ in testIdentity }, cpuHas: { $0 != "FEAT_SME2" && $0 != "FEAT_HBC" })
    #expect(throws: PreflightError.unsupportedCPU("apple-m5", ["FEAT_SME2", "FEAT_HBC"])) {
        try m4.check(layout, request: request(.run, layout.wine))
    }
    #expect(PreflightError.unsupportedCPU("apple-m5", ["FEAT_SME2"]).description
        == "This MacNeutron runtime is built for Apple M5, and this Mac's processor lacks FEAT_SME2. Install the MacNeutron build for this Mac.")
    let m5 = Preflight(systemSupported: { true }, identity: { _ in testIdentity }, cpuHas: { _ in true })
    #expect(try m5.check(layout, request: request(.run, layout.wine)) == testIdentity)
    try FileManager.default.removeItem(at: layout.cpuFile)
    #expect(try m4.check(layout, request: request(.run, layout.wine)) == testIdentity)
    #expect(!markerExists(layout))
}

@Test func x64AndArm64TargetsPass() throws {
    let layout = try makeToolLayout()
    for machine in [PEImage.amd64, PEImage.arm64] {
        #expect(try testPreflight.check(layout, request: request(.waitforexitandrun, try exe(machine: machine))) == testIdentity)
    }
    #expect(!markerExists(layout))
}

@Test func i386TargetIsThirtyTwoBit() throws {
    let layout = try makeToolLayout()
    #expect(throws: PreflightError.thirtyTwoBit) {
        try testPreflight.check(layout, request: request(.waitforexitandrun, try exe(machine: PEImage.i386)))
    }
    #expect(PreflightError.thirtyTwoBit.description
        == "This game is 32-bit. MacNeutron 0.1 runs 64-bit games only; 32-bit support is planned.")
}

@Test func otherMachineIsNamed() throws {
    let layout = try makeToolLayout()
    #expect(throws: PreflightError.unsupportedMachine(0x1c4)) {
        try testPreflight.check(layout, request: request(.waitforexitandrun, try exe(machine: 0x1c4)))
    }
    #expect(PreflightError.unsupportedMachine(0x1c4).description
        == "This game is built for ARM (32-bit), which MacNeutron can't run.")
    #expect(PreflightError.unsupportedMachine(0x200).description
        == "This game is built for Itanium, which MacNeutron can't run.")
    #expect(PreflightError.unsupportedMachine(0x5032).description
        == "This game is built for machine type 0x5032, which MacNeutron can't run.")
}

@Test func nonPETargetIsNotChecked() throws {
    let layout = try makeToolLayout()
    let script = try makeTempDir().appending(path: "launch.bat")
    try write("@echo off\r\nstart game.exe\r\n", to: script)
    #expect(try testPreflight.check(layout, request: request(.waitforexitandrun, script)) == testIdentity)
    let missing = try makeTempDir().appending(path: "Missing.exe")
    #expect(try testPreflight.check(layout, request: request(.waitforexitandrun, missing)) == testIdentity)
}

@Test func targetIsCheckedOnlyForWaitForExitAndRun() throws {
    let layout = try makeToolLayout()
    let i386 = try exe(machine: PEImage.i386)
    for verb in Verb.allCases where verb != .waitforexitandrun {
        #expect(try testPreflight.check(layout, request: request(verb, i386)) == testIdentity)
    }
}

@Test func notificationTextIsEscapedForAppleScript() {
    #expect(AppleScriptNotifier.quoted(#"Game "X" at C:\Games"#) == #""Game \"X\" at C:\\Games""#)
}
