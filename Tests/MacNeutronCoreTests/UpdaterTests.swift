import CryptoKit
import Foundation
import Testing
@testable import MacNeutronCore

/// GitHub's release list, as much of it as the updater reads; each asset is at <base>/<tag>/<name>.
func releasesJSON(_ entries: [(tag: String, draft: Bool, prerelease: Bool, assets: [String])],
                  base: String = "https://example.invalid") -> Data {
    let releases: [[String: Any]] = entries.map { entry in
        ["tag_name": entry.tag, "draft": entry.draft, "prerelease": entry.prerelease,
         "html_url": "\(base)/\(entry.tag)",
         "assets": entry.assets.map { ["name": $0, "browser_download_url": "\(base)/\(entry.tag)/\($0)"] }]
    }
    return try! JSONSerialization.data(withJSONObject: releases)
}

@Test func pickTakesTheNewestOfTheRunningChannel() throws {
    let both = ["MacNeutron-%@.zip", "SHA256SUMS"]
    func entry(_ v: String, draft: Bool = false, pre: Bool = false, assets: [String]? = nil)
        -> (tag: String, draft: Bool, prerelease: Bool, assets: [String]) {
        ("v" + v, draft, pre, (assets ?? both).map { $0.replacingOccurrences(of: "%@", with: v) })
    }
    let json = releasesJSON([entry("0.9.0"), entry("0.10.0"), entry("0.11.0", draft: true), entry("0.12.0", pre: true),
                             entry("0.13.0", assets: ["SHA256SUMS"]), entry("0.10.0p"), entry("0.9.1p")])
    let normal = try #require(Updater.pick(json, current: "0.8.0"))
    #expect(normal.version == "0.10.0")  // numeric, not text, order
    #expect(normal.zip.lastPathComponent == "MacNeutron-0.10.0.zip")
    #expect(normal.page.absoluteString == "https://example.invalid/v0.10.0")
    #expect(Updater.pick(json, current: "0.8.0p")?.version == "0.10.0p")  // the performance channel
    #expect(Updater.pick(json, current: "0.10.0") == nil)  // nothing newer
    #expect(Updater.pick(json, current: "dev") == nil)
    #expect(Updater.pick(Data("{".utf8), current: "0.8.0") == nil)
}

@Test func pickIgnoresReleasesWithoutHTTPSLinks() {
    let entry = [(tag: "v0.9.0", draft: false, prerelease: false, assets: ["MacNeutron-0.9.0.zip", "SHA256SUMS"])]
    #expect(Updater.pick(releasesJSON(entry), current: "0.8.0") != nil)
    #expect(Updater.pick(releasesJSON(entry, base: "file:///Applications"), current: "0.8.0") == nil)
    #expect(Updater.pick(releasesJSON(entry, base: "http://example.invalid"), current: "0.8.0") == nil)
}

@Test func aWorkFolderHoldingTheOnlyAppIsKept() throws {
    let dir = try makeTempDir()
    let app = dir.appending(path: "MacNeutron.app"), work = dir.appending(path: "work")
    try write("old", to: work.appending(path: "old.app/v"))
    Updater.discard(work, keepingFor: app)  // a swap whose roll-back failed: the app's path is empty
    #expect(FileManager.default.fileExists(atPath: work.appending(path: "old.app/v").path(percentEncoded: false)))
    try write("new", to: app.appending(path: "v"))
    Updater.discard(work, keepingFor: app)
    #expect(!FileManager.default.fileExists(atPath: work.path(percentEncoded: false)))
}

@Test func canReplaceNeedsAWritableFolderOutsideTranslocation() throws {
    #expect(Updater.canReplace(try makeTempDir().appending(path: "MacNeutron.app")))
    #expect(!Updater.canReplace(URL(filePath: "/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app")))
}

@Test func swapPutsTheNewAppInPlaceAndRollsBack() throws {
    let dir = try makeTempDir()
    let app = dir.appending(path: "MacNeutron.app"), new = dir.appending(path: "new/MacNeutron.app")
    try write("old", to: app.appending(path: "v"))
    try write("new", to: new.appending(path: "v"))
    try Updater.swap(new, into: app, aside: dir.appending(path: "old.app"))
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "new")
    #expect(throws: (any Error).self) {
        try Updater.swap(dir.appending(path: "missing.app"), into: app, aside: dir.appending(path: "old2.app"))
    }
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "new")  // rolled back
}

@Test func installRefusesAndChangesNothing() async throws {
    let dir = try makeTempDir()
    let app = dir.appending(path: "MacNeutron.app")
    try write("old", to: app.appending(path: "v"))
    // The release: an ad hoc signed MacNeutron.app, zipped as release.sh zips it.
    let source = try makeTempDir()
    let bundle = source.appending(path: "MacNeutron.app")
    try FileManager.default.moveItem(at: try makeSignedWineApp(at: source, shortVersion: "0.9.0"), to: bundle)
    let zip = source.appending(path: "MacNeutron-0.9.0.zip"), sums = source.appending(path: "SHA256SUMS")
    #expect(try SystemProcessRunner().run(URL(filePath: "/usr/bin/ditto"),
                                          ["-c", "-k", "--norsrc", "--keepParent", bundle.path(percentEncoded: false),
                                           zip.path(percentEncoded: false)], environment: [:], output: nil) == 0)
    let hash = SHA256.hash(data: try Data(contentsOf: zip)).map { String(format: "%02x", $0) }.joined()
    let release = Release(version: "0.9.0", page: URL(string: "https://example.invalid/v0.9.0")!,
                          zip: URL(string: "https://example.invalid/v0.9.0/MacNeutron-0.9.0.zip")!,
                          sums: URL(string: "https://example.invalid/v0.9.0/SHA256SUMS")!)
    let fetch: ValveClient.Fetch = { url in
        let copy = try makeTempDir().appending(path: url.lastPathComponent)
        try FileManager.default.copyItem(at: url.lastPathComponent == "SHA256SUMS" ? sums : zip, to: copy)
        return copy
    }

    try write("\(String(repeating: "0", count: 64))  MacNeutron-0.9.0.zip\n", to: sums)
    await #expect(throws: UpdateError.checksum) {
        _ = try await Updater.install(release, over: app, team: "ABCDE12345", fetch: fetch)
    }
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "old")

    try write("\(hash)  MacNeutron-0.9.0.zip\n", to: sums)
    let error = await #expect(throws: UpdateError.self) {
        _ = try await Updater.install(release, over: app, team: "ABCDE12345", fetch: fetch)
    }
    guard case .signature(let detail)? = error else { Issue.record("expected a signature refusal, got \(String(describing: error))"); return }
    #expect(detail != "the download holds no MacNeutron.app")  // unzipped, then refused by the signature check
    #expect(try String(contentsOf: app.appending(path: "v"), encoding: .utf8) == "old")
}

@Test func pickRecordsTheInstallerAsset() throws {
    let with = [(tag: "v0.9.2", draft: false, prerelease: false,
                 assets: ["MacNeutron-0.9.2.zip", "SHA256SUMS", "MacNeutron-0.9.2-Installer.zip"])]
    let picked = try #require(Updater.pick(releasesJSON(with), current: "0.9.1"))
    #expect(picked.installer?.lastPathComponent == "MacNeutron-0.9.2-Installer.zip")
    let without = [(tag: "v0.9.2", draft: false, prerelease: false, assets: ["MacNeutron-0.9.2.zip", "SHA256SUMS"])]
    #expect(try #require(Updater.pick(releasesJSON(without), current: "0.9.1")).installer == nil)
    // The installer's link alone made http: the release is still offered, without it.
    var list = try #require(try JSONSerialization.jsonObject(with: releasesJSON(with)) as? [[String: Any]])
    var assets = try #require(list[0]["assets"] as? [[String: Any]])
    assets[2]["browser_download_url"] = "http://example.invalid/v0.9.2/MacNeutron-0.9.2-Installer.zip"
    list[0]["assets"] = assets
    let http = try #require(Updater.pick(try JSONSerialization.data(withJSONObject: list), current: "0.9.1"))
    #expect(http.installer == nil)
}

/// The URLs a fake fetch was asked for, in order.
private final class Fetched: @unchecked Sendable {
    private let lock = NSLock()
    private var list: [URL] = []
    var urls: [URL] { lock.withLock { list } }
    func add(_ url: URL) { lock.withLock { list.append(url) } }
}

private let translocated = URL(filePath: "/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app")

private func release092(installer: Bool) -> Release {
    var release = Release(version: "0.9.2", page: URL(string: "https://example.invalid/v0.9.2")!,
                          zip: URL(string: "https://example.invalid/v0.9.2/MacNeutron-0.9.2.zip")!,
                          sums: URL(string: "https://example.invalid/v0.9.2/SHA256SUMS")!)
    if installer { release.installer = URL(string: "https://example.invalid/v0.9.2/MacNeutron-0.9.2-Installer.zip")! }
    return release
}

@Test func updateFromATranslocatedAppTakesTheInstaller() async throws {
    // The installer: an ad hoc signed bundle named as the release's, zipped as release.sh zips it.
    let source = try makeTempDir()
    let bundle = source.appending(path: "Install MacNeutron.app")
    try FileManager.default.moveItem(at: try makeSignedWineApp(at: source, shortVersion: "0.9.2"), to: bundle)
    let zip = source.appending(path: "MacNeutron-0.9.2-Installer.zip"), sums = source.appending(path: "SHA256SUMS")
    #expect(try SystemProcessRunner().run(URL(filePath: "/usr/bin/ditto"),
                                          ["-c", "-k", "--norsrc", "--keepParent", bundle.path(percentEncoded: false),
                                           zip.path(percentEncoded: false)], environment: [:], output: nil) == 0)
    let hash = SHA256.hash(data: try Data(contentsOf: zip)).map { String(format: "%02x", $0) }.joined()
    try write("\(hash)  MacNeutron-0.9.2-Installer.zip\n", to: sums)
    let release = release092(installer: true), fetched = Fetched()
    let fetch: ValveClient.Fetch = { url in
        fetched.add(url)
        let copy = try makeTempDir().appending(path: url.lastPathComponent)
        try FileManager.default.copyItem(at: url.lastPathComponent == "SHA256SUMS" ? sums : zip, to: copy)
        return copy
    }
    let error = await #expect(throws: UpdateError.self) {
        _ = try await Updater.update(release, app: translocated, team: "ABCDE12345", target: try writableTarget(), fetch: fetch)
    }
    guard case .signature(let detail)? = error else { Issue.record("expected a signature refusal, got \(String(describing: error))"); return }
    #expect(detail != "the download holds no Install MacNeutron.app")  // unzipped, then refused by the signature check
    #expect(fetched.urls == [release.installer, release.sums])
}

@Test func updateWithoutAnInstallerSaysWhereTheAppRuns() async throws {
    let fetched = Fetched()
    let fetch: ValveClient.Fetch = { url in fetched.add(url); throw URLError(.badURL) }
    await #expect(throws: UpdateError.cantReplace("/private/var/folders/x/AppTranslocation/y/d")) {
        _ = try await Updater.update(release092(installer: false), app: translocated, team: "ABCDE12345", fetch: fetch)
    }
    #expect(UpdateError.cantReplace("/private/var/folders/x/AppTranslocation/y/d").description
            == "MacNeutron is running from a temporary, read-only copy macOS made because it was opened from Downloads. "
            + "Download Install MacNeutron from the release page, or move MacNeutron to Applications.")
    #expect(UpdateError.cantReplace("/Volumes/Games").description
            == "MacNeutron is running from /Volumes/Games, which it can't replace. "
            + "Download Install MacNeutron from the release page, or move MacNeutron to Applications.")
    #expect(fetched.urls.isEmpty)
}

/// A target in a fresh, writable folder: tests never depend on /Applications.
private func writableTarget() throws -> URL {
    try makeTempDir().appending(path: "MacNeutron.app", directoryHint: .isDirectory)
}

@Test func updateRefusesTheInstallerWhenApplicationsIsntWritable() async throws {
    // A standard account: the installer couldn't write there either, so nothing is downloaded and MacNeutron stays.
    let apps = try makeTempDir()
    let folder = String(apps.path(percentEncoded: false).dropLast())
    try FileManager.default.setAttributes([.posixPermissions: 0o555], ofItemAtPath: folder)
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: folder) }
    let fetched = Fetched()
    let fetch: ValveClient.Fetch = { url in fetched.add(url); throw URLError(.badURL) }
    await #expect(throws: AppInstallerError.notWritable(folder)) {
        _ = try await Updater.update(release092(installer: true), app: translocated, team: "ABCDE12345",
                                     target: apps.appending(path: "MacNeutron.app"), fetch: fetch)
    }
    #expect(fetched.urls.isEmpty)
}
