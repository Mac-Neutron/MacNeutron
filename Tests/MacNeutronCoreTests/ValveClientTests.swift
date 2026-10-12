import Foundation
import Testing
@testable import MacNeutronCore

private let version = "1788652215"

private func makeClient(_ cdn: FakeValveCDN) throws -> ValveClient {
    ValveClient(cache: try makeTempDir().appending(path: "Valve", directoryHint: .isDirectory), fetch: cdn.fetch)
}

private func entries(_ folder: URL) -> Set<String> {
    Set((try? FileManager.default.contentsOfDirectory(atPath: folder.path(percentEncoded: false))) ?? [])
}

@Test func installsTheSixFilesFlatWithTheirSource() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    #expect(try await client.update(force: false) == .installed(version))
    let install = try #require(ValveClient.installed(in: client.cache))
    #expect(install.version == version)
    #expect(entries(install.folder) == ["steamclient64.dll", "GameOverlayRenderer64.dll", "steamclient.dll", "Steam.dll",
                                        "GameOverlayRenderer.dll", "SteamService.exe", "source.txt"])
    #expect(try Data(contentsOf: install.steamClient64) == FakeValveCDN.sixFiles()["steamclient64.dll"])
    let source = try String(contentsOf: install.folder.appending(path: "source.txt"), encoding: .utf8)
    #expect(source.contains("url \(cdn.packageURL.absoluteString)\n"))
    #expect(source.contains("sha2 \(cdn.sha2)\n"))
    #expect(cdn.requests == [ValveClient.manifestURL, cdn.packageURL])
}

@Test func aChecksumOrSizeMismatchInstallsNothing() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    let good = cdn.manifestText
    let flipped = (cdn.sha2.hasPrefix("0") ? "1" : "0") + cdn.sha2.dropFirst()
    cdn.manifestText = good.replacingOccurrences(of: cdn.sha2, with: flipped)
    await #expect(throws: ValveClientError.checksumMismatch) { try await client.update(force: false) }
    cdn.manifestText = good.replacingOccurrences(of: "\"\(cdn.size)\"", with: "\"\(cdn.size + 1)\"")
    await #expect(throws: ValveClientError.sizeMismatch(expected: cdn.size + 1, got: cdn.size)) {
        try await client.update(force: false)
    }
    #expect(ValveClient.installed(in: client.cache) == nil)
    #expect(!entries(client.cache).contains { $0.hasPrefix(".staging") })
}

@Test func aWrongMachineIsRefused() async throws {
    var files = FakeValveCDN.sixFiles()
    files["steamclient.dll"] = peBytes(machine: PEImage.amd64, certificate: true)
    let cdn = try FakeValveCDN(dir: try makeTempDir(), files: files)
    let client = try makeClient(cdn)
    await #expect(throws: ValveClientError.wrongMachine("steamclient.dll")) { try await client.update(force: false) }
    #expect(ValveClient.installed(in: client.cache) == nil)
}

@Test func anUnsignedFileIsRefused() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir(), files: FakeValveCDN.sixFiles(certificate: false))
    let client = try makeClient(cdn)
    await #expect(throws: ValveClientError.unsigned("steamclient64.dll")) { try await client.update(force: false) }
    #expect(ValveClient.installed(in: client.cache) == nil)
}

@Test func aFailedUpdateKeepsTheInstalledVersion() async throws {
    // Valve drops or renames a file: the update fails and the copy the launcher uses stays.
    let cdn = try FakeValveCDN(dir: try makeTempDir(), version: "1")
    let client = try makeClient(cdn)
    _ = try await client.update(force: false)
    var files = FakeValveCDN.sixFiles()
    files["Steam.dll"] = nil
    try cdn.replaceZip(files: files, version: "2")
    await #expect(throws: ValveClientError.extractFailed(11)) { try await client.update(force: false) }
    let install = try #require(ValveClient.installed(in: client.cache))
    #expect(install.version == "1")
    #expect(entries(install.folder).count == 7)
    #expect(entries(client.cache) == ["1"])
}

@Test func aNewSha2ReplacesAndPrunesTheOldVersion() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir(), version: "1")
    let client = try makeClient(cdn)
    _ = try await client.update(force: false)
    var files = FakeValveCDN.sixFiles()
    files["Steam.dll"]?.append(1)
    try cdn.replaceZip(files: files, version: "2")
    #expect(try await client.update(force: false) == .installed("2"))
    #expect(ValveClient.installed(in: client.cache)?.version == "2")
    #expect(entries(client.cache) == ["2"])
}

@Test func theSameSha2IsUpToDateWithoutADownload() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    _ = try await client.update(force: false)
    #expect(try await client.update(force: false) == .upToDate(version))
    #expect(cdn.requests == [ValveClient.manifestURL, cdn.packageURL, ValveClient.manifestURL])
}

@Test func forceDownloadsTheSameVersionAgain() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    _ = try await client.update(force: false)
    #expect(try await client.update(force: true) == .installed(version))
    #expect(cdn.requests == [ValveClient.manifestURL, cdn.packageURL, ValveClient.manifestURL, cdn.packageURL])
    #expect(entries(client.cache) == [version])
    #expect(entries(try #require(ValveClient.installed(in: client.cache)).folder).count == 7)
}

@Test func aStagingFolderIsNeverInstalledAndIsPruned() async throws {
    // An install stopped mid-way (the app quit during extraction).
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    let stale = client.cache.appending(path: ".staging-x/steamclient64.dll")
    try FileManager.default.createDirectory(at: stale.deletingLastPathComponent(), withIntermediateDirectories: true)
    try peBytes(machine: PEImage.amd64, certificate: true).write(to: stale)
    #expect(ValveClient.installed(in: client.cache) == nil)
    _ = try await client.update(force: false)
    #expect(entries(client.cache) == [version])
}

@Test func updateIfNeededChecksOncePerAppVersion() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    #expect(try await client.updateIfNeeded(appVersion: "1.0") == .installed(version))
    #expect(try await client.updateIfNeeded(appVersion: "1.0") == nil)
    #expect(cdn.requests.count == 2)
    #expect(try await client.updateIfNeeded(appVersion: "1.1") == .upToDate(version))
    #expect(cdn.requests.count == 3)
}

@Test func removeSticksUntilAForcedUpdate() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    _ = try await client.update(force: false)
    try client.remove()
    #expect(ValveClient.installed(in: client.cache) == nil)
    #expect(client.removedByUser)
    #expect(try await client.updateIfNeeded(appVersion: "2.0") == nil)
    #expect(cdn.requests.count == 2)
    #expect(try await client.update(force: true) == .installed(version))
    #expect(!client.removedByUser)
}

@Test func manifestFieldsThatCouldEscapeTheCacheAreRefused() {
    let sha = String(repeating: "a", count: 64)
    let cases: [(String, String)] = [
        (FakeValveCDN.manifest(version: "../1", file: "bins_win64.zip.x", size: 5, sha2: sha), "version"),
        (FakeValveCDN.manifest(version: "1", file: "a/b.zip", size: 5, sha2: sha), "file"),
        (FakeValveCDN.manifest(version: "1", file: "..", size: 5, sha2: sha), "file"),
        (FakeValveCDN.manifest(version: "1", file: "x.zip", size: 0, sha2: sha), "size"),
        (FakeValveCDN.manifest(version: "1", file: "x.zip", size: 5, sha2: String(sha.dropLast())), "sha2"),
        (FakeValveCDN.manifest(version: "1", file: "x.zip", size: 5, sha2: sha.uppercased()), "sha2"),
        ("\"win64\"\n{\n\t\"version\"\t\t\"1\"\n}\n", "bins_win64"),
    ]
    for (text, field) in cases {
        #expect(throws: ValveClientError.badManifest(field)) { try ValveManifest.parse(text) }
    }
}

@Test func manifestParsesValvesLayout() throws {
    let sha = String(repeating: "c", count: 64)
    let manifest = try ValveManifest.parse(FakeValveCDN.manifest(version: "1788652215", file: "bins_win64.zip.36f5",
                                                                 size: 63_700_191, sha2: sha))
    #expect(manifest == ValveManifest(version: "1788652215", file: "bins_win64.zip.36f5", size: 63_700_191, sha2: sha))
}

@Test func offlineIsAnError() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    cdn.offline = true
    await #expect(throws: ValveClientError.http(404)) { try await client.update(force: false) }
    #expect(ValveClient.installed(in: client.cache) == nil)
}

@Test func aContentFailureIsNotDownloadedAgainUntilTheNextAppVersion() async throws {
    // Valve's package stops matching what we extract: the app is a login item, so each start must not fetch 64 MB
    // again only to fail the same way. A MacNeutron update (a new app version) may fix it, so that retries.
    var files = FakeValveCDN.sixFiles()
    files["Steam.dll"] = nil
    let cdn = try FakeValveCDN(dir: try makeTempDir(), files: files)
    let client = try makeClient(cdn)
    await #expect(throws: ValveClientError.extractFailed(11)) { try await client.updateIfNeeded(appVersion: "1.0") }
    #expect(cdn.requests.count == 2)
    #expect(try await client.updateIfNeeded(appVersion: "1.0") == nil)
    #expect(cdn.requests.count == 2)
    await #expect(throws: ValveClientError.extractFailed(11)) { try await client.updateIfNeeded(appVersion: "1.1") }
    #expect(cdn.requests.count == 4)
}

@Test func aNetworkFailureIsRetriedAtTheNextStart() async throws {
    let cdn = try FakeValveCDN(dir: try makeTempDir())
    let client = try makeClient(cdn)
    cdn.offline = true
    await #expect(throws: ValveClientError.http(404)) { try await client.updateIfNeeded(appVersion: "1.0") }
    cdn.offline = false
    #expect(try await client.updateIfNeeded(appVersion: "1.0") == .installed(version))
}
