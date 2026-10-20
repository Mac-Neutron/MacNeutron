import Foundation
import Testing
@testable import MacNeutronCore

/// GitHub's release list, as much of it as the updater reads; each asset is at https://example.invalid/<tag>/<name>.
func releasesJSON(_ entries: [(tag: String, draft: Bool, prerelease: Bool, assets: [String])]) -> Data {
    let releases: [[String: Any]] = entries.map { entry in
        ["tag_name": entry.tag, "draft": entry.draft, "prerelease": entry.prerelease,
         "html_url": "https://example.invalid/\(entry.tag)",
         "assets": entry.assets.map { ["name": $0, "browser_download_url": "https://example.invalid/\(entry.tag)/\($0)"] }]
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
