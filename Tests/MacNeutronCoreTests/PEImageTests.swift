import Foundation
import Testing
@testable import MacNeutronCore

@Test func amd64AndArm64AndI386MachinesAreRead() throws {
    let dir = try makeTempDir()
    for machine in [PEImage.amd64, PEImage.arm64, PEImage.i386, 0x01c4] {
        let exe = dir.appending(path: "\(machine).exe")
        try peBytes(machine: machine).write(to: exe)
        #expect(PEImage.machine(of: exe) == machine)
    }
}

@Test func aScriptIsNotPE() throws {
    let script = try makeTempDir().appending(path: "run.sh")
    try write("#!/bin/sh\necho MZ PE\n" + String(repeating: "x", count: 200), to: script, executable: true)
    #expect(PEImage.machine(of: script) == nil)
}

@Test func aTruncatedMZIsNotPE() throws {
    let dir = try makeTempDir()
    let short = dir.appending(path: "short.exe")
    try Data([0x4D, 0x5A, 0, 0]).write(to: short)
    #expect(PEImage.machine(of: short) == nil)
    let noNT = dir.appending(path: "nont.exe")
    try peBytes(machine: PEImage.amd64).prefix(0x83).write(to: noNT)
    #expect(PEImage.machine(of: noNT) == nil)
}

@Test func aMissingFileIsNotPE() throws {
    #expect(PEImage.machine(of: try makeTempDir().appending(path: "missing.exe")) == nil)
}

@Test func machineReadsAPathWithSpaces() throws {
    let exe = try makeTempDir().appending(path: "game é/Game.exe")
    try FileManager.default.createDirectory(at: exe.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(machine: PEImage.arm64).write(to: exe)
    #expect(PEImage.machine(of: exe) == PEImage.arm64)
}

@Test func machineReadsOnlyTheHeaderOfAHugeFile() throws {
    let exe = try makeTempDir().appending(path: "Huge.exe")
    try peBytes(machine: PEImage.amd64).write(to: exe)
    let handle = try FileHandle(forWritingTo: exe)
    try handle.truncate(atOffset: 4 << 30)   // sparse, 4 GiB
    try handle.close()
    let start = ContinuousClock.now
    #expect(PEImage.machine(of: exe) == PEImage.amd64)
    #expect(ContinuousClock.now - start < .seconds(1))
}

@Test func certificateTableIsSeenInPE32AndPE32Plus() throws {
    let dir = try makeTempDir()
    for machine in [PEImage.i386, PEImage.amd64] {
        for signed in [true, false] {
            let file = dir.appending(path: "\(machine)-\(signed).dll")
            try peBytes(machine: machine, certificate: signed).write(to: file)
            #expect(PEImage.hasCertificateTable(file) == signed)
        }
    }
    let short = dir.appending(path: "short.dll")
    try peBytes(machine: PEImage.amd64, certificate: true).prefix(0x100).write(to: short)
    #expect(!PEImage.hasCertificateTable(short))
}
