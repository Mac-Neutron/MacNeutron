import Foundation
import Security

/// A MacNeutron release on GitHub that the app can install over itself.
public struct Release: Equatable, Sendable {
    public let version: String
    /// Its GitHub page (release notes).
    public let page: URL
    public let zip: URL
    public let sums: URL
}

public enum UpdateError: Error, Equatable, CustomStringConvertible {
    case cantReplace
    case checksum
    case signature(String)
    case version(String)
    case gatekeeper(String)

    public var description: String {
        switch self {
        case .cantReplace: "Move MacNeutron to Applications to update it."
        case .checksum: "The download doesn't match the release's SHA256SUMS."
        case .signature(let detail): "The download isn't signed by MacNeutron's developer: \(detail)"
        case .version(let version): "The download is version \(version), not the release's."
        case .gatekeeper(let detail): "macOS doesn't accept the download: \(detail)"
        }
    }
}

/// The app's own updates, from GitHub Releases.
public enum Updater {
    public static let releases = URL(string: "https://api.github.com/repos/chadouming/MacNeutron/releases?per_page=30")!
    public static let identifier = "io.github.chadouming.MacNeutron"

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
                  let zip = entry.assets.first(where: { $0.name == "MacNeutron-\(name).zip" }),
                  let sums = entry.assets.first(where: { $0.name == "SHA256SUMS" }),
                  // The reply's links are opened and downloaded: https only (a file: or http: one is skipped).
                  [entry.html_url, zip.browser_download_url, sums.browser_download_url].allSatisfy({ $0.scheme == "https" })
            else { continue }
            best = (found.numbers, Release(version: name, page: entry.html_url, zip: zip.browser_download_url,
                                           sums: sums.browser_download_url))
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
        guard canReplace(app) else { throw UpdateError.cantReplace }
        let fm = FileManager.default
        // On the app's volume, so the swap is two renames.
        let work = try fm.url(for: .itemReplacementDirectory, in: .userDomainMask, appropriateFor: app, create: true)
        do {
            let fetch = fetch ?? VMDownloads.download
            let zip = try await fetch(release.zip)
            defer { try? fm.removeItem(at: zip) }
            let sums = try await fetch(release.sums)
            defer { try? fm.removeItem(at: sums) }
            let name = release.zip.lastPathComponent
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
            let new = unpacked.appending(path: "MacNeutron.app", directoryHint: .isDirectory)
            try verify(new, team: team, version: release.version, log: work.appending(path: "spctl.txt"))
            try swap(new, into: app, aside: work.appending(path: "old.app", directoryHint: .isDirectory))
        } catch {
            discard(work, keepingFor: app)
            throw error
        }
        return work
    }

    /// Removes the work folder, unless the app's own path is empty: then the folder holds the only copy of the app (a
    /// swap whose roll-back failed too), and it stays.
    static func discard(_ work: URL, keepingFor app: URL) {
        guard FileManager.default.fileExists(atPath: app.path(percentEncoded: false)) else { return }
        try? FileManager.default.removeItem(at: work)
    }

    /// The app's signature is valid (strict, every architecture, nested code), it is this app signed by `team`, it is
    /// `version`, and Gatekeeper accepts it (Developer ID and notarization, as release.sh's R2 checks).
    static func verify(_ bundle: URL, team: String, version: String, log: URL) throws {
        guard FileManager.default.fileExists(atPath: bundle.appending(path: "Contents/Info.plist").path(percentEncoded: false)) else {
            throw UpdateError.signature("the download holds no MacNeutron.app")
        }
        var code: SecStaticCode?
        var requirement: SecRequirement?
        let text = "anchor apple generic and identifier \"\(identifier)\" and certificate leaf[subject.OU] = \"\(team)\""
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
