import Foundation
import Security

/// A MacNeutron release on GitHub that the app can install over itself.
public struct Release: Equatable, Sendable {
    public let version: String
    /// Its GitHub page (release notes).
    public let page: URL
    public let zip: URL
    public let sums: URL
    /// `MacNeutron-<V>-Installer.zip` (spec 2026-10-25-macneutron-installer-design §3.3); nil for a release without one.
    public var installer: URL? = nil
}

/// What `Updater.update` did: swapped the new app in (its work folder holds the old app until `relaunch`), or downloaded
/// and verified the release's installer, which the caller opens with --from-app.
public enum UpdateOutcome: Equatable, Sendable {
    case swapped(work: URL)
    case installer(URL)
}

public enum UpdateError: Error, Equatable, CustomStringConvertible {
    /// The folder the app runs from.
    case cantReplace(String)
    case checksum
    case signature(String)
    case version(String)
    case gatekeeper(String)

    public var description: String {
        switch self {
        case .cantReplace(let folder):
            (folder.contains("/AppTranslocation/")
                ? "MacNeutron is running from a temporary, read-only copy macOS made because it was opened from Downloads. "
                : "MacNeutron is running from \(folder), which it can't replace. ")
                + "Download Install MacNeutron from the release page, or move MacNeutron to Applications."
        case .checksum: "The download doesn't match the release's SHA256SUMS."
        case .signature(let detail): "The download isn't signed by MacNeutron's developer: \(detail)"
        case .version(let version): "The download is version \(version), not the release's."
        case .gatekeeper(let detail): "macOS doesn't accept the download: \(detail)"
        }
    }
}

/// The app's own updates, from GitHub Releases.
public enum Updater {
    public static let releases = URL(string: "https://api.github.com/repos/Mac-Neutron/MacNeutron/releases?per_page=30")!
    public static let identifier = "io.github.chadouming.MacNeutron"
    /// Install MacNeutron's (spec 2026-10-25-macneutron-installer-design §3.1).
    public static let installerIdentifier = "io.github.chadouming.MacNeutron.Installer"
    /// What an update may be: this app, or the release after it, which moves to github.com/Mac-Neutron's identifiers
    /// (2026-10-27) and publishes its app as MacNeutron-<V>-macOS.zip. 0.9.6 looks for neither, so it takes this
    /// release first and this one installs that.
    public static let identifiers = [identifier, "io.github.mac-neutron.MacNeutron"]
    public static let installerIdentifiers = [installerIdentifier, "io.github.mac-neutron.MacNeutron.Installer"]

    /// The newest release of the running build's channel (`X.Y.Z`, or `X.Y.Zp` for a performance build) above
    /// `current`, from GitHub's release list: drafts, prereleases, releases without `MacNeutron-<V>.zip` and
    /// `SHA256SUMS`, and releases with a link that isn't https don't count. Versions compare as numbers.
    public static func pick(_ json: Data, current: String) -> Release? {
        guard let running = version(current),
              let list = try? JSONDecoder().decode([GitHubRelease].self, from: json) else { return nil }
        var best: (numbers: [Int], release: Release)?
        for entry in list where !entry.draft && !entry.prerelease && entry.tag_name.hasPrefix("v") {
            let name = String(entry.tag_name.dropFirst())
            guard let found = version(name), found.performance == running.performance,
                  running.numbers.lexicographicallyPrecedes(found.numbers),
                  best.map({ $0.numbers.lexicographicallyPrecedes(found.numbers) }) ?? true,
                  let zip = entry.assets.first(where: { [
                      "MacNeutron-\(name).zip", "MacNeutron-\(name)-macOS.zip",
                  ].contains($0.name) }),
                  let sums = entry.assets.first(where: { $0.name == "SHA256SUMS" }),
                  // The reply's links are opened and downloaded: https only (a file: or http: one is skipped).
                  [entry.html_url, zip.browser_download_url, sums.browser_download_url].allSatisfy({ $0.scheme == "https" })
            else { continue }
            // The installer is optional, and https only like the rest (spec 2026-10-25-macneutron-installer-design §3.3).
            let installer = entry.assets.first(where: { $0.name == "MacNeutron-\(name)-Installer.zip" })?
                .browser_download_url
            best = (found.numbers, Release(version: name, page: entry.html_url, zip: zip.browser_download_url,
                                           sums: sums.browser_download_url,
                                           installer: installer?.scheme == "https" ? installer : nil))
        }
        return best?.release
    }

    /// Not a translocated copy (macOS runs an app opened from Downloads from one), and its folder is writable.
    public static func canReplace(_ app: URL) -> Bool {
        !app.path(percentEncoded: false).contains("/AppTranslocation/")
            && FileManager.default.isWritableFile(atPath: app.deletingLastPathComponent().path(percentEncoded: false))
    }

    /// Downloads `release`, checks it against its SHA256SUMS, verifies the app inside (`verify`) and swaps it in for
    /// `app`. Returns the work folder, which holds the old app until `relaunch` removes it. Every failure before the
    /// swap leaves `app` as it was. `fetch` nil: `VMDownloads.download`.
    public static func install(_ release: Release, over app: URL, team: String,
                               fetch: ValveClient.Fetch? = nil) async throws -> URL {
        guard canReplace(app) else {
            let folder = app.deletingLastPathComponent().path(percentEncoded: false)
            throw UpdateError.cantReplace(folder.hasSuffix("/") ? String(folder.dropLast()) : folder)
        }
        let fm = FileManager.default
        // On the app's volume, so the swap is two renames.
        let work = try fm.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: app, create: true)
        do {
            let unpacked = try await unpack(release.zip, sums: release.sums, into: work, fetch: fetch ?? VMDownloads.download)
            let new = unpacked.appending(path: "MacNeutron.app", directoryHint: .isDirectory)
            try verify(new, team: team, version: release.version, log: work.appending(path: "spctl.txt"))
            try swap(new, into: app, aside: work.appending(path: "old.app", directoryHint: .isDirectory))
        } catch {
            discard(work, keepingFor: app)
            throw error
        }
        return work
    }

    /// The release's installer (spec 2026-10-25-macneutron-installer-design §3.3), downloaded, checked against
    /// SHA256SUMS, unpacked into a new temporary folder and verified: its `Install MacNeutron.app`, which the caller opens
    /// with --from-app. A failure removes the folder.
    public static func installer(_ release: Release, team: String, fetch: ValveClient.Fetch? = nil) async throws -> URL {
        guard let asset = release.installer else { throw URLError(.fileDoesNotExist) }
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appending(path: installerFolder + UUID().uuidString, directoryHint: .isDirectory)
        try fm.createDirectory(at: work, withIntermediateDirectories: true)
        do {
            let unpacked = try await unpack(asset, sums: release.sums, into: work, fetch: fetch ?? VMDownloads.download)
            let bundle = unpacked.appending(path: "Install MacNeutron.app", directoryHint: .isDirectory)
            try verify(bundle, team: team, version: release.version, log: work.appending(path: "spctl.txt"),
                       identifiers: installerIdentifiers)
            return bundle
        } catch {
            try? fm.removeItem(at: work)
            throw error
        }
    }

    /// Every `installer` folder in `dir` removed. Install MacNeutron sweeps them once it's done (its own included, not
    /// before: the app it copies is in it), and the app when opening one fails.
    public static func removeInstallerFolders(in dir: URL = FileManager.default.temporaryDirectory) {
        let fm = FileManager.default
        for name in (try? fm.contentsOfDirectory(atPath: dir.path(percentEncoded: false))) ?? []
        where name.hasPrefix(installerFolder) {
            try? fm.removeItem(at: dir.appending(path: name, directoryHint: .isDirectory))
        }
    }

    private static let installerFolder = "MacNeutron-installer-"

    /// Settings' Install and relaunch: the release's installer when this copy can't be replaced (a translocated one, or
    /// an unwritable folder) and the release has one; otherwise `install`, which refuses a copy it can't replace. The
    /// installer is taken only when it could write `target`'s folder: a standard account's MacNeutron isn't quit for an
    /// install that would fail (AppInstaller checks the same).
    public static func update(_ release: Release, app: URL, team: String, target: URL = AppInstaller.target,
                              fetch: ValveClient.Fetch? = nil) async throws -> UpdateOutcome {
        if !canReplace(app), release.installer != nil {
            let folder = target.deletingLastPathComponent().path(percentEncoded: false)
            guard FileManager.default.isWritableFile(atPath: folder) else {
                throw AppInstallerError.notWritable(folder.hasSuffix("/") ? String(folder.dropLast()) : folder)
            }
            return .installer(try await installer(release, team: team, fetch: fetch))
        }
        return .swapped(work: try await install(release, over: app, team: team, fetch: fetch))
    }

    /// `asset` and `sums` downloaded, the asset checked against its line in SHA256SUMS and unpacked with ditto into
    /// `<work>/new`, which is returned.
    private static func unpack(_ asset: URL, sums sumsURL: URL, into work: URL,
                               fetch: ValveClient.Fetch) async throws -> URL {
        let fm = FileManager.default
        let zip = try await fetch(asset)
        defer { try? fm.removeItem(at: zip) }
        let sums = try await fetch(sumsURL)
        defer { try? fm.removeItem(at: sums) }
        let name = asset.lastPathComponent
        let line = try String(contentsOf: sums, encoding: .utf8).split(separator: "\n")
            .map { $0.split(whereSeparator: \.isWhitespace) }
            .first { $0.count == 2 && $0[1] == name }
        let size = (try? fm.attributesOfItem(atPath: zip.path(percentEncoded: false))[.size] as? NSNumber)?.int64Value
        guard let line, let size, VMDownloads.matches(zip, size: size, sha256: String(line[0])) else {
            throw UpdateError.checksum
        }
        let unpacked = work.appending(path: "new", directoryHint: .isDirectory)
        _ = try SystemProcessRunner().run(URL(filePath: "/usr/bin/ditto"),
                                          ["-x", "-k", zip.path(percentEncoded: false), unpacked.path(percentEncoded: false)],
                                          environment: [:], output: work.appending(path: "ditto.txt"))
        return unpacked
    }

    /// Removes the work folder, unless the app's own path is empty: then the folder holds the only copy of the app (a
    /// swap whose roll-back failed too), and it stays.
    static func discard(_ work: URL, keepingFor app: URL) {
        guard FileManager.default.fileExists(atPath: app.path(percentEncoded: false)) else { return }
        try? FileManager.default.removeItem(at: work)
    }

    /// The code requirement of a bundle that is one of `identifiers`, signed by `team` with a Developer ID.
    static func requirement(team: String, identifiers: [String]) -> String {
        let either = identifiers.map { "identifier \"\($0)\"" }.joined(separator: " or ")
        return "anchor apple generic and (\(either)) and certificate leaf[subject.OU] = \"\(team)\""
    }

    /// The bundle's signature is valid (strict, every architecture, nested code), it is one of `identifiers` signed by
    /// `team`, it is `version`, and Gatekeeper accepts it (Developer ID and notarization, as release.sh's R2 checks).
    public static func verify(_ bundle: URL, team: String, version: String, log: URL,
                              identifiers: [String] = Updater.identifiers) throws {
        guard FileManager.default.fileExists(atPath: bundle.appending(path: "Contents/Info.plist").path(percentEncoded: false)) else {
            throw UpdateError.signature("the download holds no \(bundle.lastPathComponent)")
        }
        var code: SecStaticCode?
        var requirement: SecRequirement?
        let text = Updater.requirement(team: team, identifiers: identifiers)
        guard SecStaticCodeCreateWithPath(bundle as CFURL, [], &code) == errSecSuccess, let code,
              SecRequirementCreateWithString(text as CFString, [], &requirement) == errSecSuccess
        else { throw UpdateError.signature("its signature can't be read") }
        var failure: Unmanaged<CFError>?
        let flags = SecCSFlags(rawValue: kSecCSStrictValidate | kSecCSCheckAllArchitectures | kSecCSCheckNestedCode)
        guard SecStaticCodeCheckValidityWithErrors(code, flags, requirement, &failure) == errSecSuccess else {
            throw UpdateError.signature(failure.map { CFErrorCopyDescription($0.takeRetainedValue()) as String } ?? "invalid")
        }
        let plist = NSDictionary(contentsOf: bundle.appending(path: "Contents/Info.plist"))
        let found = plist?["CFBundleShortVersionString"] as? String
        guard found == version else { throw UpdateError.version(found ?? "unknown") }
        let status = try SystemProcessRunner().run(URL(filePath: "/usr/sbin/spctl"),
                                                   ["--assess", "--type", "exec", bundle.path(percentEncoded: false)],
                                                   environment: [:], output: log)
        guard status == 0 else {
            let said = (try? String(contentsOf: log, encoding: .utf8))?.trimmingCharacters(in: .whitespacesAndNewlines)
            throw UpdateError.gatekeeper(said ?? "spctl exited \(status)")
        }
    }

    /// `app` moves to `aside` and `new` takes its place; if `new` can't, `app` moves back.
    public static func swap(_ new: URL, into app: URL, aside: URL) throws {
        let fm = FileManager.default
        try fm.moveItem(at: app, to: aside)
        do {
            try fm.moveItem(at: new, to: app)
        } catch {
            try? fm.moveItem(at: aside, to: app)
            throw error
        }
    }

    /// Once this process exits, removes `work` (the old app) and opens `app`. The caller quits right after.
    public static func relaunch(_ app: URL, removing work: URL) throws {
        let process = Process()
        process.executableURL = URL(filePath: "/bin/sh")
        process.arguments = ["-c", #"while kill -0 "$0" 2>/dev/null; do sleep 0.2; done; rm -rf "$1"; exec /usr/bin/open "$2""#,
                             String(getpid()), work.path(percentEncoded: false), app.path(percentEncoded: false)]
        try process.run()
    }

    /// `X.Y.Z` or `X.Y.Zp` as numbers and the performance flag; nil for anything else (a dev build's "dev").
    static func version(_ text: String) -> (numbers: [Int], performance: Bool)? {
        let performance = text.hasSuffix("p")
        let numbers = (performance ? text.dropLast() : Substring(text)).split(separator: ".", omittingEmptySubsequences: false)
            .map { Int($0) }
        guard numbers.count == 3, numbers.allSatisfy({ ($0 ?? -1) >= 0 }) else { return nil }
        return (numbers.compactMap { $0 }, performance)
    }

    private struct GitHubRelease: Decodable {
        let tag_name: String
        let draft: Bool
        let prerelease: Bool
        let html_url: URL
        let assets: [Asset]
    }

    private struct Asset: Decodable {
        let name: String
        let browser_download_url: URL
    }
}
