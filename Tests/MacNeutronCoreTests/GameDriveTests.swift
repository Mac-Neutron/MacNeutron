import Darwin
import Foundation
import Testing
@testable import MacNeutronCore

/// A Steam library `<tmp>/<name>/<steamapps>/common/Game`.
private func makeLibrary(_ name: String = "Lib Ünï", steamapps: String = "steamapps") throws -> (lib: URL, apps: URL, game: URL) {
    let lib = try makeTempDir().appending(path: name, directoryHint: .isDirectory)
    let apps = lib.appending(path: steamapps, directoryHint: .isDirectory)
    let game = apps.appending(path: "common/Game", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: game, withIntermediateDirectories: true)
    return (lib, apps, game)
}

/// A folder's path without the trailing "/", as Steam passes it.
private func path(_ url: URL) -> String {
    var text = url.path(percentEncoded: false)
    if text.count > 1, text.hasSuffix("/") { text.removeLast() }
    return text
}

private func gameEnvironment(_ libraries: String, install: URL) -> [String: String] {
    ["STEAM_COMPAT_LIBRARY_PATHS": libraries, "STEAM_COMPAT_INSTALL_PATH": path(install)]
}

/// `pfx/dosdevices/s:`'s target, or nil when there is no link.
private func gameDriveLink(_ prefix: URL) -> String? {
    var buffer = [CChar](repeating: 0, count: 4096)
    let count = readlink(prefix.appending(path: "dosdevices/s:").path(percentEncoded: false), &buffer, buffer.count - 1)
    return count < 0 ? nil : String(cString: buffer)
}

@Test func theLibrarysParentIsTheTarget() throws {
    let (lib, apps, game) = try makeLibrary()
    let other = try makeTempDir().appending(path: "Other/steamapps")
    var notes: [String] = []
    let target = GameDrive.target(environment: gameEnvironment("\(path(other)):\(path(apps))", install: game), notes: &notes)
    #expect(target == realPath(path(lib)))
    #expect(notes.isEmpty)
}

@Test func theFirstSubstringMatchWins() throws {
    // Proton's `entry in install_path` (proton:319): "<root>/L" matches "<root>/L2/…" first, fails validation, and
    // Proton doesn't try the next entry.
    let root = try makeTempDir()
    let apps = root.appending(path: "L2/steamapps", directoryHint: .isDirectory)
    let game = apps.appending(path: "common/Game", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: game, withIntermediateDirectories: true)
    var notes: [String] = []
    let first = path(root.appending(path: "L"))
    #expect(GameDrive.target(environment: gameEnvironment("\(first):\(path(apps))", install: game), notes: &notes) == first)
    #expect(notes == ["Proton: Error: unable to use parent for game drive, path \(first)"])
}

@Test func anEmptyEntryMatchesAndRemovesTheDrive() throws {
    // An empty piece matches every install path (proton:319); its fallback, "", removes s: (proton:333-335).
    let (_, apps, game) = try makeLibrary()
    let environment = gameEnvironment("::\(path(apps))", install: game)
    var notes: [String] = []
    #expect(GameDrive.target(environment: environment, notes: &notes) == "")
    #expect(notes == ["Proton: Error: unable to use parent for game drive, path "])
    let prefix = try makeTempDir().appending(path: "pfx", directoryHint: .isDirectory)
    _ = GameDrive.apply(prefix: prefix, environment: gameEnvironment(path(apps), install: game))
    #expect(gameDriveLink(prefix) != nil)
    _ = GameDrive.apply(prefix: prefix, environment: environment)
    #expect(gameDriveLink(prefix) == nil)
}

@Test func aLegacySteamAppsFolderNamedSteamappsIsTheLibrary() throws {
    // Mac Steam passes ".../steamapps" for a folder that may be "SteamApps" on disk (an old install). Python's realpath,
    // which Proton uses, keeps that spelling; Darwin's realpath(3) gives the on-disk case.
    let (lib, onDisk, _) = try makeLibrary(steamapps: "SteamApps")
    let apps = path(onDisk).replacingOccurrences(of: "/SteamApps", with: "/steamapps")
    var notes: [String] = []
    let environment = ["STEAM_COMPAT_LIBRARY_PATHS": apps, "STEAM_COMPAT_INSTALL_PATH": apps + "/common/Game"]
    #expect(GameDrive.target(environment: environment, notes: &notes) == realPath(path(lib)))
    #expect(notes.isEmpty)
}

@Test func aLibraryThatFailsValidationGetsProtonsFallback() throws {
    // The basename is checked as written, after realpath: "SteamApps" isn't "steamapps".
    let (_, wrongCase, wrongCaseGame) = try makeLibrary(steamapps: "SteamApps")
    var notes: [String] = []
    #expect(GameDrive.target(environment: gameEnvironment(path(wrongCase), install: wrongCaseGame), notes: &notes)
        == path(wrongCase))
    #expect(notes.count == 1)

    // A parent the player can't write.
    let (lib, apps, game) = try makeLibrary()
    chmod(path(lib), 0o555)
    defer { chmod(path(lib), 0o755) }
    notes = []
    #expect(GameDrive.target(environment: gameEnvironment(path(apps), install: game), notes: &notes) == path(apps))
    #expect(notes.count == 1)

    // steamapps on another volume than its parent.
    let (_, mounted, mountedGame) = try makeLibrary()
    notes = []
    let target = GameDrive.target(environment: gameEnvironment(path(mounted), install: mountedGame),
                                  device: { $0.hasSuffix("/steamapps") ? 1 : 2 }, notes: &notes)
    #expect(target == path(mounted))
    #expect(notes.count == 1)
}

@Test func noMatchOrMissingVariablesMeansNoDrive() throws {
    let (_, apps, game) = try makeLibrary()
    var notes: [String] = []
    #expect(GameDrive.target(environment: gameEnvironment("/nowhere/steamapps", install: game), notes: &notes) == nil)
    #expect(GameDrive.target(environment: ["STEAM_COMPAT_LIBRARY_PATHS": path(apps)], notes: &notes) == nil)
    #expect(GameDrive.target(environment: ["STEAM_COMPAT_INSTALL_PATH": path(game)], notes: &notes) == nil)
    #expect(notes.isEmpty)
}

@Test func applyLinksRetargetsAndRemoves() throws {
    let prefix = try makeTempDir().appending(path: "pfx", directoryHint: .isDirectory)
    let (lib, apps, game) = try makeLibrary()
    let first = gameEnvironment(path(apps), install: game)
    _ = GameDrive.apply(prefix: prefix, environment: first)
    #expect(gameDriveLink(prefix) == realPath(path(lib)))

    let (lib2, apps2, game2) = try makeLibrary("Second")
    _ = GameDrive.apply(prefix: prefix, environment: gameEnvironment(path(apps2), install: game2))
    #expect(gameDriveLink(prefix) == realPath(path(lib2)))

    // PROTON_SET_GAME_DRIVE: "0" and "" turn it off; anything else, "false" included, leaves it on (proton:57-58).
    for value in ["0", ""] {
        _ = GameDrive.apply(prefix: prefix, environment: first)
        _ = GameDrive.apply(prefix: prefix, environment: first.merging(["PROTON_SET_GAME_DRIVE": value]) { $1 })
        #expect(gameDriveLink(prefix) == nil)
    }
    _ = GameDrive.apply(prefix: prefix, environment: first.merging(["PROTON_SET_GAME_DRIVE": "false"]) { $1 })
    #expect(gameDriveLink(prefix) == realPath(path(lib)))

    _ = GameDrive.apply(prefix: prefix, environment: gameEnvironment("/nowhere/steamapps", install: game))
    #expect(gameDriveLink(prefix) == nil)
}

@Test func aNonLinkAtSIsLeftAlone() throws {
    let prefix = try makeTempDir().appending(path: "pfx", directoryHint: .isDirectory)
    let file = prefix.appending(path: "dosdevices/s:")
    try write("the player's", to: file)
    let (_, apps, game) = try makeLibrary()
    let notes = GameDrive.apply(prefix: prefix, environment: gameEnvironment(path(apps), install: game))
    #expect(try String(contentsOf: file, encoding: .utf8) == "the player's")
    #expect(notes == ["game drive: dosdevices/s: is not a link, left alone"])
}
