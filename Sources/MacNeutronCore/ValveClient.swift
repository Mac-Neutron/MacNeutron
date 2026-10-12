import CryptoKit
import Darwin
import Foundation

public enum ValveClientError: Error, Equatable, CustomStringConvertible {
    /// The manifest lacks a field, or a field fails its check (named).
    case badManifest(String)
    case http(Int)
    case sizeMismatch(expected: Int64, got: Int64)
    case checksumMismatch
    /// `unzip`'s exit status (11: a file the package should hold is missing).
    case extractFailed(Int32)
    case wrongMachine(String)
    case unsigned(String)

    /// Shown in Settings.
    public var description: String {
        switch self {
        case .badManifest(let field): "Valve's update manifest isn't as expected (\(field))."
        case .http(let status): "Valve's server answered HTTP \(status)."
        case .sizeMismatch(let expected, let got): "The download from Valve is \(got) bytes, not \(expected)."
        case .checksumMismatch: "The download from Valve doesn't match its checksum."
        case .extractFailed(let status): "Couldn't unpack Valve's package (unzip exit \(status))."
        case .wrongMachine(let name): "\(name) from Valve isn't the Windows file expected."
        case .unsigned(let name): "\(name) from Valve isn't signed."
        }
    }
}

/// Valve's `steam_client_win64` manifest, as far as `bins_win64` goes. Every field is checked before a path or URL is
/// built from it.
public struct ValveManifest: Equatable, Sendable {
    public let version: String
    public let file: String
    public let size: Int64
    public let sha2: String

    public init(version: String, file: String, size: Int64, sha2: String) {
        self.version = version
        self.file = file
        self.size = size
        self.sha2 = sha2
    }

    /// A KeyValues root block (whatever its key) holding `version` and one block per package.
    public static func parse(_ text: String) throws(ValveClientError) -> ValveManifest {
        guard let root = (try? KeyValues.parse(text))?.first(where: { !$0.children.isEmpty }) else {
            throw .badManifest("root")
        }
        func string(_ key: String, in nodes: [KVNode]) -> String? { nodes.first { $0.key == key }?.stringValue }
        let digits = { (text: String) in !text.isEmpty && text.utf8.allSatisfy { (0x30...0x39).contains($0) } }
        guard let version = string("version", in: root.children), digits(version) else { throw .badManifest("version") }
        guard let package = root.children.first(where: { $0.key == "bins_win64" })?.children, !package.isEmpty else {
            throw .badManifest("bins_win64")
        }
        // A plain name, never a path: it is appended to Valve's URL.
        guard let file = string("file", in: package), let first = file.unicodeScalars.first,
              CharacterSet.alphanumerics.contains(first), !file.contains(".."),
              file.utf8.allSatisfy({ $0 < 0x80 && (CharacterSet.alphanumerics.contains(Unicode.Scalar($0))
                                                   || "._-".utf8.contains($0)) })
        else { throw .badManifest("file") }
        guard let size = string("size", in: package).flatMap({ Int64($0) }), size > 0 else { throw .badManifest("size") }
        guard let sha2 = string("sha2", in: package), sha2.count == 64,
              sha2.utf8.allSatisfy({ (0x30...0x39).contains($0) || (0x61...0x66).contains($0) })
        else { throw .badManifest("sha2") }
        return ValveManifest(version: version, file: file, size: size, sha2: sha2)
    }
}

/// One downloaded version of Valve's files: `Valve/<version>/`.
public struct ValveInstall: Equatable, Sendable {
    public let version: String
    public let folder: URL
    public var steamClient64: URL { folder.appending(path: "steamclient64.dll") }
}

public enum ValveUpdateOutcome: Equatable, Sendable {
    case installed(String)
    case upToDate(String)
}

/// Valve's signed Windows client files, which Proton copies from Linux Steam's `legacycompat` folder. macOS Steam has
/// none, so the app fetches them from Valve's client-update CDN into `Valve/<version>/` on this Mac (never shipped by
/// us). The launcher puts `steamclient64.dll` in each prefix (SteamStub checks its signature).
public struct ValveClient: Sendable {
    /// Downloads `url` to a file alone in a fresh temporary folder, which the caller removes.
    public typealias Fetch = @Sendable (URL) async throws -> URL

    public static let manifestURL = URL(string: "https://client-update.steamstatic.com/steam_client_win64")!
    public static let packageBase = URL(string: "https://client-update.steamstatic.com/")!
    /// Proton's legacycompat set (parity spec A1): the zip member and its PE machine. Installed flat, under Valve's names.
    public static let files: [(member: String, machine: UInt16)] = [
        ("steamclient64.dll", PEImage.amd64), ("GameOverlayRenderer64.dll", PEImage.amd64),
        ("steamclient.dll", PEImage.i386), ("Steam.dll", PEImage.i386), ("GameOverlayRenderer.dll", PEImage.i386),
        ("bin/SteamService.exe", PEImage.i386),
    ]

    public let cache: URL
    private let fetch: Fetch
    private let runner: any ProcessRunner

    public init(cache: URL, fetch: @escaping Fetch = ValveClient.download, runner: any ProcessRunner = SystemProcessRunner()) {
        self.cache = cache
        self.fetch = fetch
        self.runner = runner
    }

    /// No cookies, no cache, no account: an ephemeral session.
    public static func download(_ url: URL) async throws -> URL {
        let session = URLSession(configuration: .ephemeral)
        defer { session.finishTasksAndInvalidate() }
        let (file, response) = try await session.download(from: url)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        guard status == 200 else {
            try? FileManager.default.removeItem(at: file)
            throw ValveClientError.http(status)
        }
        let folder = FileManager.default.temporaryDirectory.appending(path: "MacNeutron Valve \(UUID().uuidString)",
                                                                       directoryHint: .isDirectory)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let kept = folder.appending(path: url.lastPathComponent)
        try FileManager.default.moveItem(at: file, to: kept)
        return kept
    }

    /// The highest all-digit version folder holding `steamclient64.dll`; `.staging-*` folders never count.
    public static func installed(in cache: URL) -> ValveInstall? {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: cache.path(percentEncoded: false))) ?? []
        return names.filter { !$0.isEmpty && $0.utf8.allSatisfy { (0x30...0x39).contains($0) } }
            .sorted { ($0.count, $0) > ($1.count, $1) }
            .lazy.map { ValveInstall(version: $0, folder: cache.appending(path: $0, directoryHint: .isDirectory)) }
            .first { FileManager.default.fileExists(atPath: $0.steamClient64.path(percentEncoded: false)) }
    }

    /// Settings' Remove: no automatic download until a forced one.
    public var removedByUser: Bool { FileManager.default.fileExists(atPath: marker("removed").path(percentEncoded: false)) }

    private func marker(_ name: String) -> URL { cache.appending(path: name) }

    /// Downloads, checks and installs `bins_win64`'s files unless (without `force`) the installed copy came from the
    /// manifest's `sha2`. A failure leaves the installed copy as it was.
    public func update(force: Bool) async throws -> ValveUpdateOutcome {
        let manifestFile = try await fetch(Self.manifestURL)
        defer { try? FileManager.default.removeItem(at: manifestFile.deletingLastPathComponent()) }
        let manifest = try ValveManifest.parse(try String(contentsOf: manifestFile, encoding: .utf8))
        if !force, let current = Self.installed(in: cache), Self.sha2(of: current) == manifest.sha2 {
            return .upToDate(current.version)
        }
        let url = Self.packageBase.appending(path: manifest.file)
        let zip = try await fetch(url)
        defer { try? FileManager.default.removeItem(at: zip.deletingLastPathComponent()) }
        try Self.verify(zip, size: manifest.size, sha2: manifest.sha2)

        let fm = FileManager.default
        try fm.createDirectory(at: cache, withIntermediateDirectories: true)
        let staging = cache.appending(path: ".staging-\(UUID().uuidString)")  // no trailing "/", for rename(2)
        do {
            try fm.createDirectory(at: staging, withIntermediateDirectories: true)
            let status = try runner.run(URL(filePath: "/usr/bin/unzip"),
                                        ["-q", "-o", "-j", zip.path(percentEncoded: false)] + Self.files.map(\.member)
                                            + ["-d", staging.path(percentEncoded: false)],
                                        environment: [:], output: URL(filePath: "/dev/null"))
            guard status == 0 else { throw ValveClientError.extractFailed(status) }
            for (member, machine) in Self.files {
                let name = URL(filePath: member).lastPathComponent, file = staging.appending(path: name)
                guard PEImage.machine(of: file) == machine else { throw ValveClientError.wrongMachine(name) }
                guard PEImage.hasCertificateTable(file) else { throw ValveClientError.unsigned(name) }
            }
            try "url \(url.absoluteString)\nsha2 \(manifest.sha2)\ndate \(Date().ISO8601Format())\n"
                .write(to: staging.appending(path: "source.txt"), atomically: true, encoding: .utf8)
            let from = staging.path(percentEncoded: false), to = cache.appending(path: manifest.version).path(percentEncoded: false)
            if fm.fileExists(atPath: to) {
                // The old copy is swapped out in one step, then removed with the staging folder.
                guard renamex_np(from, to, UInt32(RENAME_SWAP)) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
                try? fm.removeItem(at: staging)
            } else {
                guard rename(from, to) == 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
            }
        } catch {
            try? fm.removeItem(at: staging)
            throw error
        }
        // Older versions, stopped installs and `removed`.
        // ponytail: a launch copying from the pruned folder at that instant fails once; the next launch uses the new one.
        for name in (try? fm.contentsOfDirectory(atPath: cache.path(percentEncoded: false))) ?? []
        where name != manifest.version && name != "checked" {
            try? fm.removeItem(at: cache.appending(path: name))
        }
        return .installed(manifest.version)
    }

    /// The app's automatic check (spec D1, D2, D4): once per app version, never after Remove.
    public func updateIfNeeded(appVersion: String) async throws -> ValveUpdateOutcome? {
        guard !removedByUser else { return nil }
        let checked = marker("checked")
        if Self.installed(in: cache) != nil, (try? String(contentsOf: checked, encoding: .utf8)) == appVersion { return nil }
        let outcome = try await update(force: false)
        try appVersion.write(to: checked, atomically: true, encoding: .utf8)
        return outcome
    }

    /// Settings' Remove: the launcher then uses lsteamclient, and nothing is downloaded until a forced update.
    public func remove() throws {
        try? FileManager.default.removeItem(at: cache)
        try FileManager.default.createDirectory(at: cache, withIntermediateDirectories: true)
        try Data().write(to: marker("removed"))
    }

    private static func sha2(of install: ValveInstall) -> String? {
        (try? String(contentsOf: install.folder.appending(path: "source.txt"), encoding: .utf8))?
            .split(separator: "\n").first { $0.hasPrefix("sha2 ") }.map { String($0.dropFirst(5)) }
    }

    private static func verify(_ zip: URL, size: Int64, sha2: String) throws {
        let actual = (try FileManager.default.attributesOfItem(atPath: zip.path(percentEncoded: false))[.size] as? NSNumber)?
            .int64Value ?? -1
        guard actual == size else { throw ValveClientError.sizeMismatch(expected: size, got: actual) }
        let handle = try FileHandle(forReadingFrom: zip)
        defer { try? handle.close() }
        var hasher = SHA256()
        while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty { hasher.update(data: chunk) }
        guard hasher.finalize().map({ String(format: "%02x", $0) }).joined() == sha2 else {
            throw ValveClientError.checksumMismatch
        }
    }
}
