import Darwin
import Foundation
import Testing
@testable import MacNeutronCore

/// A stand-in for the installer's bundled app: <dir>/MacNeutron.app, its Contents/v naming the version.
private func bundle(at dir: URL, version: String) throws -> URL {
    let app = dir.appending(path: "MacNeutron.app", directoryHint: .isDirectory)
    try write(version, to: app.appending(path: "Contents/v"))
    return app
}

private func path(_ url: URL) -> String {
    let p = url.path(percentEncoded: false)
    return p.hasSuffix("/") ? String(p.dropLast()) : p
}

/// What macOS sets on a downloaded installer's files, and its copies inherit.
private func quarantine(_ url: URL) {
    let value = "0081;00000000;Test;"
    _ = setxattr(path(url), "com.apple.quarantine", value, value.utf8.count, 0, XATTR_NOFOLLOW)
}

private func hasQuarantine(_ url: URL) -> Bool {
    getxattr(path(url), "com.apple.quarantine", nil, 0, 0, XATTR_NOFOLLOW) >= 0
}

private func version(_ app: URL) -> String? { try? String(contentsOf: app.appending(path: "Contents/v"), encoding: .utf8) }

/// The staging names an install leaves beside the target, if any.
private func staged(_ folder: URL) -> [String] {
    [".MacNeutron.app.installing", ".MacNeutron.app.old"].filter {
        FileManager.default.fileExists(atPath: path(folder.appending(path: $0)))
    }
}

/// A source with the new app and an Applications folder, with an old app in it when `old` is set.
private func setUp(old: String? = nil) throws -> (bundled: URL, apps: URL, target: URL) {
    let bundled = try bundle(at: try makeTempDir(), version: "new")
    let apps = try makeTempDir().appending(path: "Applications", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: apps, withIntermediateDirectories: true)
    if let old { _ = try bundle(at: apps, version: old) }
    return (bundled, apps, apps.appending(path: "MacNeutron.app", directoryHint: .isDirectory))
}

@Test func installsFreshAndStripsQuarantine() throws {
    let (bundled, apps, target) = try setUp()
    quarantine(bundled)
    quarantine(bundled.appending(path: "Contents/v"))
    var verified: [URL] = [], quits = 0
    try AppInstaller.install(bundled, into: target, verify: { verified.append($0) }, quit: { quits += 1; return true })
    #expect(version(target) == "new")
    #expect(!hasQuarantine(target))
    #expect(!hasQuarantine(target.appending(path: "Contents/v")))
    #expect(hasQuarantine(bundled))  // the source is only read
    #expect(verified == [bundled])
    #expect(quits == 1)
    #expect(staged(apps).isEmpty)
}

@Test func replacesTheAppInApplications() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { true })
    #expect(version(target) == "new")
    #expect(staged(apps).isEmpty)
}

@Test func aFailedVerifyChangesNothing() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    var quits = 0
    #expect(throws: UpdateError.signature("x")) {
        try AppInstaller.install(bundled, into: target, verify: { _ in throw UpdateError.signature("x") },
                                 quit: { quits += 1; return true })
    }
    #expect(quits == 0)
    #expect(version(target) == "old")
    #expect(staged(apps).isEmpty)
}

@Test func anAppThatWontQuitChangesNothing() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    #expect(throws: AppInstallerError.stillRunning) {
        try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { false })
    }
    #expect(version(target) == "old")
    #expect(staged(apps).isEmpty)
}

@Test func anUnwritableFolderChangesNothing() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    try FileManager.default.setAttributes([.posixPermissions: 0o555], ofItemAtPath: path(apps))
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: path(apps)) }
    var quits = 0
    #expect(throws: AppInstallerError.notWritable(path(apps))) {
        try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { quits += 1; return true })
    }
    #expect(quits == 0)
    #expect(version(target) == "old")
}

@Test func anInterruptedInstallsLeftoversAreCleared() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    try write("junk", to: apps.appending(path: ".MacNeutron.app.installing/junk"))
    try write("junk", to: apps.appending(path: ".MacNeutron.app.old/junk"))
    try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { true })
    #expect(version(target) == "new")
    #expect(staged(apps).isEmpty)
}

@Test func aReadOnlySourceInstalls() throws {
    let (bundled, _, target) = try setUp()
    let source = bundled.deletingLastPathComponent()
    try FileManager.default.setAttributes([.posixPermissions: 0o555], ofItemAtPath: path(source))
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: path(source)) }
    try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { true })
    #expect(version(target) == "new")
}

@Test func installerMessagesAreExact() {
    #expect(AppInstallerError.notWritable("/Applications").description
            == "Your account can't write to /Applications: drag MacNeutron.app there yourself, or use an administrator account.")
    #expect(AppInstallerError.stillRunning.description
            == "MacNeutron didn't quit within 30 seconds: quit it, then open Install MacNeutron again.")
}

@Test func aCopyThatFailsChangesNothing() throws {
    let (_, apps, target) = try setUp(old: "old")
    let missing = try makeTempDir().appending(path: "MacNeutron.app", directoryHint: .isDirectory)
    #expect {
        try AppInstaller.install(missing, into: target, verify: { _ in }, quit: { true })
    } throws: { ($0 as? AppInstallerError).map { if case .copyFailed = $0 { true } else { false } } ?? false }
    #expect(version(target) == "old")
    #expect(staged(apps).isEmpty)
}

@Test func anAppLeftAsideByACrashIsPutBack() throws {
    // A crash between Updater.swap's renames: no target, the old app beside it. A failed copy must not lose it.
    let (_, apps, target) = try setUp()
    try write("old", to: apps.appending(path: ".MacNeutron.app.old/Contents/v"))
    let missing = try makeTempDir().appending(path: "MacNeutron.app", directoryHint: .isDirectory)
    #expect(throws: (any Error).self) {
        try AppInstaller.install(missing, into: target, verify: { _ in }, quit: { true })
    }
    #expect(version(target) == "old")
    #expect(staged(apps).isEmpty)
}

@Test func aLeftoverThatWontGoStopsBeforeQuitting() throws {
    let (bundled, apps, target) = try setUp(old: "old")
    let leftover = apps.appending(path: ".MacNeutron.app.old", directoryHint: .isDirectory)
    try write("junk", to: leftover.appending(path: "junk"))
    try FileManager.default.setAttributes([.posixPermissions: 0o555], ofItemAtPath: path(leftover))
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: path(leftover)) }
    var quits = 0
    #expect(throws: AppInstallerError.leftover(path(leftover))) {
        try AppInstaller.install(bundled, into: target, verify: { _ in }, quit: { quits += 1; return true })
    }
    #expect(quits == 0)
    #expect(version(target) == "old")
    #expect(AppInstallerError.leftover("/Applications/.MacNeutron.app.old").description
            == "/Applications/.MacNeutron.app.old couldn't be removed: move it to the Trash, then open Install MacNeutron again.")
}
