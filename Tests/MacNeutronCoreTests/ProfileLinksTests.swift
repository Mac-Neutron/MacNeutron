import Foundation
import Testing
@testable import MacNeutronCore

/// A prefix whose Wine profile `users/<user>` has the Vista folders wineboot makes; `documents` stands for the Mac's
/// own folder, which Wine links `Documents` to.
private func makePrefix(user: String = "u", documents: URL? = nil) throws -> URL {
    let prefix = try makeTempDir().appending(path: "pfx", directoryHint: .isDirectory)
    let home = prefix.appending(path: "drive_c/users/\(user)")
    for folder in ["AppData/Local", "AppData/Roaming", "AppData/LocalLow", "Saved Games"] {
        try FileManager.default.createDirectory(at: home.appending(path: folder), withIntermediateDirectories: true)
    }
    if let documents {
        try FileManager.default.createDirectory(at: documents, withIntermediateDirectories: true)
        try FileManager.default.createSymbolicLink(at: home.appending(path: "Documents"), withDestinationURL: documents)
    } else {
        try FileManager.default.createDirectory(at: home.appending(path: "Documents"), withIntermediateDirectories: true)
    }
    return prefix
}

private func users(_ prefix: URL, _ path: String) -> URL { prefix.appending(path: "drive_c/users/\(path)") }
private func link(_ prefix: URL, _ path: String) -> String? {
    try? FileManager.default.destinationOfSymbolicLink(atPath: users(prefix, path).path(percentEncoded: false))
}
private func isRealFolder(_ prefix: URL, _ path: String) -> Bool {
    var info = stat()
    return lstat(users(prefix, path).path(percentEncoded: false), &info) == 0 && info.st_mode & S_IFMT == S_IFDIR
}
private func read(_ prefix: URL, _ path: String) -> String? { try? String(contentsOf: users(prefix, path), encoding: .utf8) }
private func seed(_ prefix: URL, _ path: String, _ text: String) throws { try write(text, to: users(prefix, path)) }

@Test func anEmptyProfileGetsAllFourLinks() throws {
    let prefix = try makePrefix()
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(link(prefix, "u/Local Settings/Application Data") == "../AppData/Local")
    #expect(link(prefix, "u/Application Data") == "AppData/Roaming")
    #expect(link(prefix, "u/My Documents") == "Documents")
    #expect(link(prefix, "steamuser") == "u")
    #expect(report == ProfileLinkReport())
}

@Test func seededSteamuserMergesIntoVistaFolders() throws {
    // Steam's cloud download wrote WinAppDataLocal's files under its XP name before MacNeutron ever ran.
    let prefix = try makePrefix()
    try seed(prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(read(prefix, "u/AppData/Local/G/s.sav") == "cloud")
    #expect(read(prefix, "steamuser/Local Settings/Application Data/G/s.sav") == "cloud")  // the path Steam watches
    #expect(link(prefix, "steamuser") == "u")
    #expect(read(prefix, "steamuser BACKUP/Local Settings/Application Data/G/s.sav") == "cloud")
    #expect(report.conflicts == 0)
}

@Test func aDifferingFileChangesNothing() throws {
    let prefix = try makePrefix()
    try seed(prefix, "u/AppData/Local/G/s.sav", "local")
    try seed(prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(isRealFolder(prefix, "steamuser"))
    #expect(read(prefix, "steamuser/Local Settings/Application Data/G/s.sav") == "cloud")
    #expect(read(prefix, "u/AppData/Local/G/s.sav") == "local")
    #expect(!isRealFolder(prefix, "steamuser BACKUP"))
    #expect(report.conflicts == 1)
}

@Test func anIdenticalFileMerges() throws {
    let prefix = try makePrefix()
    try seed(prefix, "u/AppData/Roaming/G/a.cfg", "same")
    try seed(prefix, "steamuser/Application Data/G/a.cfg", "same")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(link(prefix, "steamuser") == "u")
    #expect(report.conflicts == 0)
}

@Test func aSecondRunChangesNothing() throws {
    let prefix = try makePrefix()
    try seed(prefix, "steamuser/Saved Games/G/x", "cloud")
    _ = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(ProfileLinks.apply(prefix: prefix, user: "u") == ProfileLinkReport())
    #expect(link(prefix, "steamuser") == "u")
    #expect(read(prefix, "u/Saved Games/G/x") == "cloud")
}

@Test func aProfileNamedSteamuserGetsNoSteamuserLink() throws {
    let prefix = try makePrefix(user: "steamuser")
    _ = ProfileLinks.apply(prefix: prefix, user: "steamuser")
    #expect(isRealFolder(prefix, "steamuser"))
    #expect(link(prefix, "steamuser/Application Data") == "AppData/Roaming")
}

@Test func withoutMergeRealFoldersStay() throws {
    // The app's pass while Steam is closed only adds links; merging is the launch's job.
    let prefix = try makePrefix()
    try seed(prefix, "steamuser/Application Data/G/a.cfg", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u", merge: false)
    #expect(isRealFolder(prefix, "steamuser"))
    #expect(read(prefix, "u/AppData/Roaming/G/a.cfg") == nil)
    #expect(report.notes.count == 1)
    #expect(report.conflicts == 0)
}

@Test func noSwapSupportLeavesFoldersAlone() throws {
    let prefix = try makePrefix()
    try seed(prefix, "steamuser/Application Data/G/a.cfg", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u", canSwap: { _ in false })
    #expect(isRealFolder(prefix, "steamuser"))
    #expect(report.notes.count == 1)
}

@Test func loggedPathsHideLongNumbers() throws {
    // Palworld's save path contains the SteamID; launcher.log ends up in bug reports.
    let prefix = try makePrefix()
    try seed(prefix, "u/AppData/Local/G/76561198000000000/x", "local")
    try seed(prefix, "steamuser/Local Settings/Application Data/G/76561198000000000/x", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(report.notes.contains { $0.contains("G/<n>/x") })
    #expect(!report.notes.joined().contains("76561198"))
}

// MARK: Review Focus

@Test func myDocumentsMergesThroughWinesDocumentsLink() throws {
    let documents = try makeTempDir().appending(path: "mac documents", directoryHint: .isDirectory)
    let prefix = try makePrefix(documents: documents)
    try seed(prefix, "steamuser/My Documents/G/a", "cloud")
    _ = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(try String(contentsOf: documents.appending(path: "G/a"), encoding: .utf8) == "cloud")
    #expect(link(prefix, "steamuser") == "u")
}

@Test func aRerunAfterClonesFinishesTheSwap() throws {
    // A launch killed after cloning but before the swap left equal copies in the profile.
    let prefix = try makePrefix()
    try seed(prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud")
    try seed(prefix, "u/AppData/Local/G/s.sav", "cloud")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(link(prefix, "steamuser") == "u")
    #expect(report.conflicts == 0)
}

@Test func fileAgainstFolderRefusesTheMerge() throws {
    let prefix = try makePrefix()
    try seed(prefix, "u/AppData/Roaming/G/inner", "local folder's file")
    try seed(prefix, "steamuser/Application Data/G", "a file where the profile has a folder")
    _ = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(isRealFolder(prefix, "steamuser"))
    #expect(read(prefix, "steamuser/Application Data/G") == "a file where the profile has a folder")
}

@Test func missingProfileLinksNothing() throws {
    let prefix = try makePrefix(user: "someone else")
    let report = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(link(prefix, "steamuser") == nil)
    #expect(link(prefix, "someone else/Application Data") == nil)
    #expect(report.notes.count == 1)
}

@Test func backupNameSkipsTakenNames() throws {
    let prefix = try makePrefix()
    try FileManager.default.createDirectory(at: users(prefix, "steamuser BACKUP"), withIntermediateDirectories: true)
    try seed(prefix, "steamuser/Saved Games/G/x", "cloud")
    _ = ProfileLinks.apply(prefix: prefix, user: "u")
    #expect(read(prefix, "steamuser BACKUP 2/Saved Games/G/x") == "cloud")
    #expect(link(prefix, "steamuser") == "u")
}
