import Foundation

/// Shader pre-caching as Steam does it for Vulkan games (docs/superpowers/specs/2026-09-30-macneutron-pipeline-cache-design.md
/// §3.7): our d3d12.dll records each game's pipelines in `<compatdata>/dxmt-pipelines`, and after a translator or
/// macOS update the launcher rebuilds them with `dxmt-replay.exe` before the game starts.
public struct ShaderPrecache: Sendable {
    public let folder: URL
    /// The builds the recordings are compiled for: `<translator key> <macOS build>` (`DXMT/translator`). A stamp from
    /// before the key (`<dxmt-version> <macOS build>`) differs, so it replays once.
    public let builds: String

    public init(context: CompatContext, layout: ToolLayout, osBuild: String = ShaderPrecache.macOSBuild()) {
        folder = Self.folder(for: context)
        builds = "\(layout.dxmtTranslator ?? "none") \(osBuild)"
    }

    public static func folder(for context: CompatContext) -> URL { context.dataPath.appending(path: "dxmt-pipelines") }

    /// Recording and replay are DXMT's; `MACNEUTRON_PRECACHE=0` turns both off.
    public static func enabled(backend: GraphicsBackend, environment: [String: String]) -> Bool {
        backend == .dxmt && environment["MACNEUTRON_PRECACHE"] != "0"
    }

    public var stampFile: URL { folder.appending(path: "replayed") }

    var stamp: String? {
        (try? String(contentsOf: stampFile, encoding: .utf8))?.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    public var recordings: [URL] {
        ((try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension == "pipelines" }
            .map { folder.appending(path: $0.lastPathComponent) } // the folder as given, not symlinks resolved
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    /// Recordings compiled for other builds. No stamp means the recordings came from sessions of these builds.
    public var needsReplay: Bool {
        guard let stamp, stamp != builds else { return false }
        return !recordings.isEmpty
    }

    public func writeStamp() {
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        try? (builds + "\n").write(to: stampFile, atomically: true, encoding: .utf8)
    }

    public func writeStampIfMissing() {
        if stamp == nil { writeStamp() }
    }

    /// Runs `dxmt-replay.exe` on every recording in the game's environment, minus recording; one log line each.
    /// Stops before the next recording once `stopped` (Steam's Stop) says so. While a replay runs, `progress` gets
    /// "25% (n of total)" at each quarter, from the replayer's own progress lines.
    public func replay(layout: ToolLayout, runner: any ProcessRunner, environment: [String: String],
                       stopped: () -> Bool = { false }, progress: @escaping @Sendable (String) -> Void = { _ in },
                       pollInterval: TimeInterval = 1) -> [String] {
        var env = environment
        env.removeValue(forKey: "DXMT_PIPELINE_RECORD")
        let output = folder.appending(path: "replay.log")
        var lines: [String] = []
        for recording in recordings {
            if stopped() { break }
            try? FileManager.default.removeItem(at: output)
            let done = StopFlag()
            let poller = Thread {
                var quarter = 0
                while !done.isSet {
                    Thread.sleep(forTimeInterval: pollInterval)
                    // ponytail: rereads the whole output each poll; it stays a few hundred bytes (one line per 10%).
                    guard let (n, total) = Self.lastProgress(in: output), total > 0 else { continue }
                    let reached = n * 4 / total
                    if reached > quarter && reached < 4 {
                        quarter = reached
                        progress("\(reached * 25)% (\(n) of \(total))")
                    }
                }
            }
            poller.start()
            defer { done.set() }
            let status = (try? runner.run(layout.wine, [layout.dxmtReplay.path(percentEncoded: false),
                                                        "Z:" + recording.path(percentEncoded: false)],
                                          environment: env, output: output)) ?? -1
            let text = (try? String(contentsOf: output, encoding: .utf8)) ?? ""
            let result = text.split(whereSeparator: \.isNewline).last { $0.hasPrefix("replay: ") }
            lines.append("precache: \(recording.lastPathComponent) exit=\(status) \(result.map(String.init) ?? "no result")")
        }
        return lines
    }

    /// The last `replay progress <n>/<total>` line dxmt-replay.exe wrote.
    static func lastProgress(in output: URL) -> (Int, Int)? {
        guard let text = try? String(contentsOf: output, encoding: .utf8),
              let line = text.split(whereSeparator: \.isNewline).last(where: { $0.hasPrefix("replay progress ") })
        else { return nil }
        let parts = line.dropFirst("replay progress ".count).split(separator: "/")
        guard parts.count == 2, let n = Int(parts[0]), let total = Int(parts[1]) else { return nil }
        return (n, total)
    }

    /// `kern.osversion`, the macOS build (e.g. 25A354): Metal's compiler changes with it.
    public static func macOSBuild() -> String {
        var size = 0
        sysctlbyname("kern.osversion", nil, &size, nil, 0)
        var buffer = [CChar](repeating: 0, count: max(size, 1))
        sysctlbyname("kern.osversion", &buffer, &size, nil, 0)
        return String(cString: buffer)
    }
}
