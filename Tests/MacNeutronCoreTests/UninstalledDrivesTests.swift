import Darwin
import Foundation
import Testing
@testable import MacNeutronCore

// A game Steam uninstalled loses its VM drive, as Linux Steam deletes its Proton prefix (spec
// 2026-10-24-macneutron-install-scripts-design §4.9).

/// Steam's libraryfolders.vdf: library "0" (the Steam root) with `apps`, and `more` as a second library's.
private func libraryFolders(root: URL, apps: [String], more: (path: String, apps: [String])? = nil) -> String {
    func library(_ key: String, _ path: String, _ apps: [String]) -> String {
        "\t\"\(key)\"\n\t{\n\t\t\"path\"\t\t\"\(path)\"\n\t\t\"apps\"\n\t\t{\n"
            + apps.map { "\t\t\t\"\($0)\"\t\t\"1000\"\n" }.joined() + "\t\t}\n\t}\n"
    }
    return "\"libraryfolders\"\n{\n" + library("0", root.path(percentEncoded: false), apps)
        + (more.map { library("1", $0.path, $0.apps) } ?? "") + "}\n"
}

/// A Steam root listing 231350 (installed) and 555 (listed, its library's drive disconnected: no manifest).
private func makeSteam() throws -> SteamLocation {
    let steam = try makeSteamLocation()
    try write(libraryFolders(root: steam.root, apps: ["231350", "555"]),
              to: steam.root.appending(path: "steamapps/libraryfolders.vdf"))
    try write("\"AppState\"\n{\n\"appid\" \"231350\"\n}\n", to: steam.root.appending(path: "steamapps/appmanifest_231350.acf"))
    return steam
}

/// A VM folder with drives for 231350, 555, 999 (uninstalled, with its TPM, shader cache and run share), 0 and a
/// test's named drive, each run from the Steam root's library (the launcher's runs/<id>/library).
private func makeVM(steam: SteamLocation) throws -> URL {
    let vm = try makeTempDir().appending(path: "vm", directoryHint: .isDirectory)
    for name in ["231350.img", "555.img", "999.img", "0.img", "fonts.img", "999.tpm/id", "999.dxmt/x"] {
        try write("disk", to: vm.appending(path: "state/\(name)"))
    }
    for id in ["231350", "555", "999", "0"] {
        try write(steam.root.appending(path: "steamapps").path(percentEncoded: false), to: vm.appending(path: "runs/\(id)/library"))
    }
    try write("console", to: vm.appending(path: "runs/999/console.log"))
    try write("", to: vm.appending(path: "runs/999.lock"))
    return vm
}

private func exists(_ vm: URL, _ path: String) -> Bool {
    FileManager.default.fileExists(atPath: vm.appending(path: path).path(percentEncoded: false))
}

private let app999 = ["state/999.img", "state/999.tpm", "state/999.dxmt", "runs/999", "runs/999.lock"]
private let kept = ["state/231350.img", "state/555.img", "state/0.img", "state/fonts.img"]

@Test func anUninstalledGamesDriveGoes() throws {
    let steam = try makeSteam()
    let vm = try makeVM(steam: steam)
    let lines = UninstalledDrives.remove(vm: vm, steam: steam)
    for path in app999 { #expect(!exists(vm, path), "\(path) is still there") }
    for path in kept { #expect(exists(vm, path), "\(path) was deleted") }
    #expect(lines.count == 1)
    #expect(lines.first?.hasPrefix("deleted the game VM drive of app 999: Steam uninstalled it (") == true)
}

@Test func nothingGoesWithoutLibraryFolders() throws {
    let steam = try makeSteam()
    let vm = try makeVM(steam: steam)
    let file = steam.root.appending(path: "steamapps/libraryfolders.vdf")
    try FileManager.default.removeItem(at: file)
    #expect(UninstalledDrives.remove(vm: vm, steam: steam) == [])
    try write("\"libraryfolders\"\n{\n", to: file)  // Steam rewriting it
    #expect(UninstalledDrives.remove(vm: vm, steam: steam) == [])
    for path in app999 + kept { #expect(exists(vm, path), "\(path) was deleted") }
}

@Test func aRunningVMsDriveStays() throws {
    let steam = try makeSteam()
    let vm = try makeVM(steam: steam)
    // run-guest.sh's lockf holds it while the VM runs.
    let fd = open(vm.appending(path: "runs/999.lock").path(percentEncoded: false), O_RDONLY | O_EXLOCK)
    #expect(fd >= 0)
    #expect(UninstalledDrives.remove(vm: vm, steam: steam) == [])
    #expect(exists(vm, "state/999.img"))
    close(fd)
    #expect(UninstalledDrives.remove(vm: vm, steam: steam).count == 1)
    #expect(!exists(vm, "state/999.img"))
}

@Test func listedAppIDsReadsEveryLibrary() throws {
    let steam = try makeSteamLocation()
    let file = steam.root.appending(path: "steamapps/libraryfolders.vdf")
    #expect(steam.listedAppIDs() == nil)
    try write(libraryFolders(root: steam.root, apps: ["231350"], more: ("/Volumes/Games", ["555", "872200"])), to: file)
    #expect(steam.listedAppIDs() == [231350, 555, 872200])
    try write("\"libraryfolders\"\n{\n", to: file)
    #expect(steam.listedAppIDs() == nil)
}

// Steam's library list reset (Steam reinstalled) or a library taken off it: its games' drives are kept, as are drives
// no launch has recorded a library for.
@Test func aDriveFromALibrarySteamNoLongerListsStays() throws {
    let steam = try makeSteam()
    let vm = try makeVM(steam: steam)
    try write("/Volumes/Games/steamapps", to: vm.appending(path: "runs/999/library"))
    #expect(UninstalledDrives.remove(vm: vm, steam: steam) == [])
    try FileManager.default.removeItem(at: vm.appending(path: "runs/999/library"))
    #expect(UninstalledDrives.remove(vm: vm, steam: steam) == [])
    #expect(exists(vm, "state/999.img"))
}
