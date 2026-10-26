import Foundation

/// A crash the game VM ended for. A game's own crash reporter (Unreal's `CrashReportClient`, and the other engines'
/// kin) opens on the guest's Xvfb, where no one can see or click it, and waits: `steam.exe` waits for it, so the VM would
/// stay up until Steam's Stop. `run-game.sh` recognizes one, keeps its report in the run share (`traces/crash-report`),
/// writes `traces/crash`, and ends the prefix.
///
/// The Mac reads that marker after the run: the report's guest paths are translated back to the prefix and library the
/// Mac knows, and the launcher keeps a copy beside its own logs so the player can find it.
public struct VMCrash: Sendable {
    /// The marker `run-game.sh` writes once it has seen a reporter.
    public static let marker = "traces/crash"
    /// The reporter's `ps` snapshot, its command line, the copy of its report, and the guest paths it came from.
    public static let details = "traces/crash-report"

    public let when: String
    public let reporter: String
    public let command: String
    /// The crash folders the guest found, in the Mac's paths.
    public let reports: [String]
    /// The share's copy of what the reporter left (`ps.txt`, `report/`), for the launcher to keep.
    public let detailsURL: URL
    /// The marker as the guest wrote it: the game log gets it whole.
    public let raw: String

    /// The marker's fields (`key=value` a line, `report=` one guest path a line), or nil without one.
    public static func read(share: URL, prefix: URL, library: String) -> VMCrash? {
        let markerURL = share.appending(path: marker)
        guard let raw = try? String(contentsOf: markerURL, encoding: .utf8), !raw.isEmpty else { return nil }
        var fields: [String: String] = [:]
        var reports: [String] = []
        for line in raw.split(separator: "\n", omittingEmptySubsequences: true) {
            guard let eq = line.firstIndex(of: "=") else { continue }
            let key = String(line[..<eq]), value = String(line[line.index(after: eq)...])
            if key == "report" {
                if let path = macPath(value, prefix: prefix, library: library) { reports.append(path) }
            } else {
                fields[key] = value
            }
        }
        return VMCrash(when: fields["when"] ?? "", reporter: fields["reporter"] ?? "a crash reporter",
                       command: fields["command"] ?? "", reports: reports,
                       detailsURL: share.appending(path: details), raw: raw)
    }

    /// A guest crash-report path as the Mac has it: the profile (`/home/mn/pfx/drive_c/users/steamuser`) is the Mac
    /// prefix's, shared into the VM, and `/mnt/games` is the Steam library (`--steam-map`). Anything else is left out:
    /// a path the Mac can't open is worse than none.
    public static func macPath(_ guest: String, prefix: URL, library: String) -> String? {
        let profile = "/home/mn/pfx/drive_c/users/steamuser"
        if guest == profile || guest.hasPrefix(profile + "/") {
            return prefix.path(percentEncoded: false) + "/drive_c/users/steamuser" + guest.dropFirst(profile.count)
        }
        let games = "/mnt/games/"
        if guest.hasPrefix(games) {
            var root = library
            while root.hasSuffix("/") { root.removeLast() }
            return root + "/" + guest.dropFirst(games.count)
        }
        return nil
    }

    /// One `launcher.log` line: what ran, and where its report is on the Mac.
    public var summary: String {
        var line = "reporter=\(reporter)"
        if !when.isEmpty { line += " when=\(when)" }
        if !reports.isEmpty { line += " reports=\(reports.joined(separator: ","))" }
        return line
    }
}
