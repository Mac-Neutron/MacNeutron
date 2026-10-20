import Foundation

/// A MacNeutron release on GitHub that the app can install over itself.
public struct Release: Equatable, Sendable {
    public let version: String
    /// Its GitHub page (release notes).
    public let page: URL
    public let zip: URL
    public let sums: URL
}

/// The app's own updates, from GitHub Releases.
public enum Updater {
    public static let releases = URL(string: "https://api.github.com/repos/chadouming/MacNeutron/releases?per_page=30")!

    /// The newest release of the running build's channel (`X.Y.Z`, or `X.Y.Zp` for a performance build) above
    /// `current`, from GitHub's release list: drafts, prereleases and releases without `MacNeutron-<V>.zip` and
    /// `SHA256SUMS` don't count. Versions compare as numbers.
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
                  let sums = entry.assets.first(where: { $0.name == "SHA256SUMS" })
            else { continue }
            best = (found.numbers, Release(version: name, page: entry.html_url, zip: zip.browser_download_url,
                                           sums: sums.browser_download_url))
        }
        return best?.release
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
