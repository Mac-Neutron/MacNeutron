import CryptoKit
import Foundation
@testable import MacNeutronCore

/// Stands in for Valve's client-update CDN: a manifest and one `bins_win64` zip built from tiny fake PE files (never
/// Valve's). `fetch` serves both as fresh temporary copies and records every request.
final class FakeValveCDN: @unchecked Sendable {
    private let lock = NSLock()
    private let dir: URL
    private var zip: URL
    private var served: [URL] = []
    private var isOffline = false
    private var text = ""
    private(set) var packageName = ""
    private(set) var sha2 = ""
    private(set) var size: Int64 = 0

    init(dir: URL, version: String = "1788652215", files: [String: Data] = FakeValveCDN.sixFiles()) throws {
        self.dir = dir
        zip = dir.appending(path: "bins_win64.zip")
        try replaceZip(files: files, version: version)
    }

    var requests: [URL] { lock.withLock { served } }
    var offline: Bool {
        get { lock.withLock { isOffline } }
        set { lock.withLock { isOffline = newValue } }
    }
    /// The manifest served; tests edit it to break a field.
    var manifestText: String {
        get { lock.withLock { text } }
        set { lock.withLock { text = newValue } }
    }
    var packageURL: URL { URL(string: "https://client-update.steamstatic.com/\(packageName)")! }

    /// Serves `files` (zip member → bytes) as a new `bins_win64` of `version`.
    func replaceZip(files: [String: Data], version: String) throws {
        let fm = FileManager.default
        let folder = dir.appending(path: "package-\(UUID().uuidString)", directoryHint: .isDirectory)
        for (member, bytes) in files {
            let file = folder.appending(path: member)
            try fm.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
            try bytes.write(to: file)
        }
        try? fm.removeItem(at: zip)
        let status = try SystemProcessRunner().run(URL(filePath: "/usr/bin/ditto"),
                                                   ["-c", "-k", "--norsrc", folder.path(percentEncoded: false),
                                                    zip.path(percentEncoded: false)],
                                                   environment: [:], output: URL(filePath: "/dev/null"))
        guard status == 0 else { throw CocoaError(.fileWriteUnknown) }
        let data = try Data(contentsOf: zip)
        let hash = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        lock.withLock {
            sha2 = hash
            size = Int64(data.count)
            packageName = "bins_win64.zip.\(hash.prefix(40))"
            text = Self.manifest(version: version, file: packageName, size: size, sha2: hash)
        }
    }

    var fetch: ValveClient.Fetch {
        { url in
            let (offline, text, name) = self.lock.withLock {
                self.served.append(url)
                return (self.isOffline, self.text, self.packageName)
            }
            guard !offline else { throw ValveClientError.http(404) }
            let out = self.dir.appending(path: "served-\(UUID().uuidString)", directoryHint: .isDirectory)
            try FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
            let file = out.appending(path: url.lastPathComponent)
            switch url.absoluteString {
            case ValveClient.manifestURL.absoluteString: try text.write(to: file, atomically: true, encoding: .utf8)
            case "https://client-update.steamstatic.com/\(name)": try FileManager.default.copyItem(at: self.zip, to: file)
            default: throw ValveClientError.http(404)
            }
            return file
        }
    }

    /// The six legacycompat files with their machines (parity spec A1).
    static func sixFiles(certificate: Bool = true) -> [String: Data] {
        ["steamclient64.dll": peBytes(machine: PEImage.amd64, certificate: certificate),
         "GameOverlayRenderer64.dll": peBytes(machine: PEImage.amd64, certificate: certificate),
         "steamclient.dll": peBytes(machine: PEImage.i386, certificate: certificate),
         "Steam.dll": peBytes(machine: PEImage.i386, certificate: certificate),
         "GameOverlayRenderer.dll": peBytes(machine: PEImage.i386, certificate: certificate),
         "bin/SteamService.exe": peBytes(machine: PEImage.i386, certificate: certificate)]
    }

    /// Valve's layout: a root block with `version` and one block per package, `bins_win64` among others.
    static func manifest(version: String, file: String, size: Int64, sha2: String) -> String {
        func package(_ name: String, _ file: String, _ size: String, _ sha2: String) -> String {
            "\t\"\(name)\"\n\t{\n\t\t\"file\"\t\t\"\(file)\"\n\t\t\"size\"\t\t\"\(size)\"\n\t\t\"sha2\"\t\t\"\(sha2)\"\n"
                + "\t\t\"zipvz\"\t\t\"\(file).vz\"\n\t\t\"sha2vz\"\t\t\"\(String(repeating: "f", count: 64))\"\n\t}\n"
        }
        return "\"win64\"\n{\n\t\"version\"\t\t\"\(version)\"\n"
            + package("bins_codecs_win64", "bins_codecs_win64.zip.1", "7", String(repeating: "1", count: 64))
            + package("bins_win64", file, String(size), sha2)
            + package("bins_cef_win64", "bins_cef_win64.zip.2", "9", String(repeating: "2", count: 64))
            + "}\n"
    }
}
