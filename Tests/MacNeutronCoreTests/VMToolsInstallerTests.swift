import Foundation
import Testing
@testable import MacNeutronCore

/// A stand-in for MacNeutron.app's Contents/Helpers/vm: each item, its text naming the tools' version.
private func makeTools(_ version: String) throws -> URL {
    let tools = try makeTempDir().appending(path: "vm-tools", directoryHint: .isDirectory)
    for file in ["macneutron-vm", "libkrun/libkrun.1.dylib", "steam-forward/host/mn-steam-host", "gvproxy",
                 "metal-host/winemetal.so", "run-guest.sh", "run-game.sh"] {
        try write("\(version) \(file)\n", to: tools.appending(path: file), executable: true)
    }
    return tools
}

private func text(_ url: URL) -> String? { try? String(contentsOf: url, encoding: .utf8) }

@Test func installsEveryItemAndRecordsTheIdentity() throws {
    let tools = try makeTools("a"), vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    let outcome = try VMToolsInstaller.install(from: tools, into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    #expect(outcome == .installed)
    for item in VMToolsInstaller.items { #expect(FileManager.default.fileExists(atPath: vm.appending(path: item).path)) }
    #expect(text(vm.appending(path: "libkrun/libkrun.1.dylib")) == "a libkrun/libkrun.1.dylib\n")
    #expect(text(vm.appending(path: "tools.identity")) == "a\n")
}

@Test func aSecondInstallOfTheSameToolsIsUnchanged() throws {
    let tools = try makeTools("a"), vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try VMToolsInstaller.install(from: tools, into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    let file = vm.appending(path: "macneutron-vm").path
    let before = try FileManager.default.attributesOfItem(atPath: file)[.modificationDate] as? Date
    let outcome = try VMToolsInstaller.install(from: tools, into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    #expect(outcome == .unchanged)
    #expect(try FileManager.default.attributesOfItem(atPath: file)[.modificationDate] as? Date == before)
}

@Test func newToolsReplaceOldOnesButNotTheGamesFiles() throws {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try VMToolsInstaller.install(from: try makeTools("a"), into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    let kept = ["state/1.img", "state/1.tpm/id", "runs/1/run.log", "images/manifest.sha256"]
    for path in kept { try write("game \(path)\n", to: vm.appending(path: path)) }
    let outcome = try VMToolsInstaller.install(from: try makeTools("b"), into: vm, runningExecutables: { [] },
                                               identity: { _ in "b" })
    #expect(outcome == .installed)
    for path in kept { #expect(text(vm.appending(path: path)) == "game \(path)\n") }
    #expect(text(vm.appending(path: "metal-host/winemetal.so")) == "b metal-host/winemetal.so\n")
    #expect(text(vm.appending(path: "tools.identity")) == "b\n")
}

@Test func aRunningVMDefersTheInstall() throws {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: vm, withIntermediateDirectories: true)
    let running = vm.appending(path: "macneutron-vm").path
    let outcome = try VMToolsInstaller.install(from: try makeTools("a"), into: vm, runningExecutables: { [running] },
                                               identity: { _ in "a" })
    #expect(outcome == .deferred(running))
    #expect(!FileManager.default.fileExists(atPath: vm.appending(path: "run-guest.sh").path))
}

@Test func aVMStartedDuringTheCopyDefersTheSwap() throws {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try VMToolsInstaller.install(from: try makeTools("a"), into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    let running = vm.appending(path: "macneutron-vm").path
    nonisolated(unsafe) var calls = 0
    let outcome = try VMToolsInstaller.install(from: try makeTools("b"), into: vm, runningExecutables: {
        calls += 1
        return calls == 1 ? [] : [running]
    }, identity: { _ in "b" })
    #expect(outcome == .deferred(running))
    #expect(text(vm.appending(path: "macneutron-vm")) == "a macneutron-vm\n")
    let leftovers = try FileManager.default.contentsOfDirectory(atPath: vm.path).filter { $0.hasSuffix(".new") }
    #expect(leftovers.isEmpty)
}

@Test func leftoversOfAnInterruptedInstallAreCleared() throws {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    try write("junk\n", to: vm.appending(path: "libkrun.new/junk"))
    try VMToolsInstaller.install(from: try makeTools("a"), into: vm, runningExecutables: { [] }, identity: { _ in "a" })
    #expect(!FileManager.default.fileExists(atPath: vm.appending(path: "libkrun.new").path))
    #expect(text(vm.appending(path: "libkrun/libkrun.1.dylib")) == "a libkrun/libkrun.1.dylib\n")
}

@Test func aSymlinkedFolderIsLeftAlone() throws {
    let target = try makeTempDir().appending(path: "eac-vm-work", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: target, withIntermediateDirectories: true)
    let vm = try makeTempDir().appending(path: "vm")
    try FileManager.default.createSymbolicLink(at: vm, withDestinationURL: target)
    let outcome = try VMToolsInstaller.install(from: try makeTools("a"), into: vm, runningExecutables: { [] },
                                               identity: { _ in "a" })
    #expect(outcome == .developerFolder)
    let valve = try makeTempDir()
    try write("valve\n", to: valve.appending(path: "steamclient64.dll"))
    try VMToolsInstaller.copyValveClient(ValveInstall(version: "1", folder: valve), into: vm)
    #expect(try FileManager.default.contentsOfDirectory(atPath: target.path).isEmpty)
}

@Test func valveClientReachesTheVM() throws {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    let valve = try makeTempDir()
    try write("valve 1\n", to: valve.appending(path: "steamclient64.dll"))
    try VMToolsInstaller.copyValveClient(ValveInstall(version: "1", folder: valve), into: vm)
    let copied = vm.appending(path: "share/valve/steamclient64.dll")
    #expect(text(copied) == "valve 1\n")
    try VMToolsInstaller.copyValveClient(nil, into: vm)
    #expect(!FileManager.default.fileExists(atPath: copied.path))
}

@Test func theScriptsComeFromTheirOwnFolder() throws {
    // MacNeutron.app keeps them in Contents/Resources/vm: codesign takes a script in Contents/Helpers for unsigned code.
    let tools = try makeTools("a"), scripts = try makeTempDir().appending(path: "vm-scripts", directoryHint: .isDirectory)
    for name in ["run-guest.sh", "run-game.sh"] {
        try FileManager.default.removeItem(at: tools.appending(path: name))
        try write("script \(name)\n", to: scripts.appending(path: name), executable: true)
    }
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    #expect(try VMToolsInstaller.install(from: tools, scripts: scripts, into: vm, runningExecutables: { [] },
                                         identity: { _ in "a" }) == .installed)
    #expect(text(vm.appending(path: "run-guest.sh")) == "script run-guest.sh\n")
    #expect(text(vm.appending(path: "macneutron-vm")) == "a macneutron-vm\n")
}
