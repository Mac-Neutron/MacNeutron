import CryptoKit
import Foundation
import Testing
@testable import MacNeutronCore

private func sha(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }

/// Serves fixed bytes per URL, records every request; a URL it doesn't know fails as a 404 would.
private final class FakeMirror: @unchecked Sendable {
    private let lock = NSLock()
    private var served: [URL] = []
    let files: [URL: Data]
    init(_ files: [URL: Data]) { self.files = files }
    var requests: [URL] { lock.withLock { served } }
    var fetch: ValveClient.Fetch {
        { url in
            self.lock.withLock { self.served.append(url) }
            guard let data = self.files[url] else { throw URLError(.fileDoesNotExist) }
            let out = try makeTempDir().appending(path: url.lastPathComponent)
            try data.write(to: out)
            return out
        }
    }
}

private let kernel = VMSetupManifest.Package(file: "kernel-core-7.2.10-200.fc44.aarch64.rpm", name: "kernel-core",
                                              version: "7.2.10", release: "200.fc44", arch: "aarch64", size: 5,
                                              sha256: sha(Data("core\n".utf8)))

/// What eac-vm/setup/manifest.py prints, shortened.
private let manifestJSON = """
    {"version": 1, "parts": {"url": "https://example.invalid/parts.tar.xz", "size": 3, "sha256": "\(String(repeating: "0", count: 64))"},
     "kernel": "7.2.10-200.fc44.aarch64",
     "packages": [{"file": "kernel-core-7.2.10-200.fc44.aarch64.rpm", "name": "kernel-core", "version": "7.2.10",
                   "release": "200.fc44", "arch": "aarch64", "size": 5, "sha256": "\(sha(Data("core\n".utf8)))"}],
     "fexRootfs": {"package": {"file": "fex-emu-rootfs-fedora-44^20260410.n.0-1.fc44.noarch.rpm",
                               "name": "fex-emu-rootfs-fedora", "version": "44^20260410.n.0", "release": "1.fc44",
                               "arch": "noarch", "size": 1, "sha256": "\(String(repeating: "1", count: 64))"},
                   "image": "/usr/share/fex-emu/RootFS/default.erofs", "imageSha256": "\(String(repeating: "2", count: 64))"},
     "koji": {"base": "https://kojipkgs.fedoraproject.org/packages/", "signedKey": "6d9f90a6"}}
    """

private func manifest(mirrors: [URL]? = nil) throws -> VMSetupManifest {
    var m = try JSONDecoder().decode(VMSetupManifest.self, from: Data(manifestJSON.utf8))
    m.mirrors = mirrors
    return m
}

@Test func sourcesAreReleasesThenUpdatesThenKoji() throws {
    let m = try manifest()
    #expect(m.sources(kernel).map(\.absoluteString) == [
        "https://dl.fedoraproject.org/pub/fedora/linux/releases/44/Everything/aarch64/os/Packages/k/kernel-core-7.2.10-200.fc44.aarch64.rpm",
        "https://dl.fedoraproject.org/pub/fedora/linux/updates/44/Everything/aarch64/Packages/k/kernel-core-7.2.10-200.fc44.aarch64.rpm",
        "https://kojipkgs.fedoraproject.org/packages/kernel-core/7.2.10/200.fc44/data/signed/6d9f90a6/aarch64/kernel-core-7.2.10-200.fc44.aarch64.rpm",
    ])
    // A noarch package is in the aarch64 tree on the mirrors, under noarch on Koji; "^" is escaped.
    let fex = m.sources(m.fexRootfs.package).map(\.absoluteString)
    #expect(fex[0].hasSuffix("/44/Everything/aarch64/os/Packages/f/fex-emu-rootfs-fedora-44%5E20260410.n.0-1.fc44.noarch.rpm"))
    #expect(fex[2].hasSuffix("/fex-emu-rootfs-fedora/44%5E20260410.n.0/1.fc44/data/signed/6d9f90a6/noarch/fex-emu-rootfs-fedora-44%5E20260410.n.0-1.fc44.noarch.rpm"))
}

@Test func mirrorsReplaceFedorasSources() throws {
    let m = try manifest(mirrors: [URL(filePath: "/tmp/rpms/", directoryHint: .isDirectory)])
    #expect(m.sources(kernel) == [URL(filePath: "/tmp/rpms/kernel-core-7.2.10-200.fc44.aarch64.rpm")])
}

@Test func aMirrorMissFallsBackToKoji() async throws {
    let m = try manifest(), sources = m.sources(kernel)
    let mirror = FakeMirror([sources[2]: Data("core\n".utf8)])
    let cache = try makeTempDir()
    let got = try await VMDownloads.fetch(sources, size: 5, sha256: kernel.sha256, name: kernel.file, cache: cache,
                                          fetch: mirror.fetch)
    #expect(got == cache.appending(path: kernel.sha256))
    #expect(try Data(contentsOf: got) == Data("core\n".utf8))
    #expect(mirror.requests == sources)
}

@Test func aFileWrongEverywhereFailsWithItsName() async throws {
    let good = [URL(string: "https://example.invalid/good.rpm")!], bad = [URL(string: "https://example.invalid/x.rpm")!]
    let mirror = FakeMirror([good[0]: Data("good\n".utf8), bad[0]: Data("evil\n".utf8)])
    let cache = try makeTempDir()
    await #expect(throws: VMSetupError.download(name: "x.rpm")) {
        try await VMDownloads.fetchAll([(good, 5, sha(Data("good\n".utf8)), "good.rpm"), (bad, 5, sha(Data("bad\n".utf8)), "x.rpm")],
                                       cache: cache, fetch: mirror.fetch, progress: { _, _ in })
    }
    #expect(mirror.requests.filter { $0 == bad[0] }.count == 3)
    #expect(FileManager.default.fileExists(atPath: cache.appending(path: sha(Data("good\n".utf8))).path))
}

@Test func aCachedFileIsNotFetchedAgain() async throws {
    let cache = try makeTempDir()
    try Data("core\n".utf8).write(to: cache.appending(path: kernel.sha256))
    let mirror = FakeMirror([:])
    nonisolated(unsafe) var reported: (Int64, Int64) = (0, 0)
    try await VMDownloads.fetchAll([(try manifest().sources(kernel), 5, kernel.sha256, kernel.file)], cache: cache,
                                   fetch: mirror.fetch, progress: { reported = ($0, $1) })
    #expect(mirror.requests.isEmpty)
    #expect(reported == (5, 5))
}
