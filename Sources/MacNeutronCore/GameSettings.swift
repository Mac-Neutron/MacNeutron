import Foundation

/// Per-game choices from the app's games window. Every field is optional; nil means default.
public struct GameSettings: Codable, Equatable, Sendable {
    public var graphics: String?
    public var log: Bool?
    public var msync: Bool?
    public var runAs: RunAs?
    public var metalFX: Bool?
    /// Post-process anti-aliasing in the presenter: "off" or "cmaa2" (any other value is off).
    public var postAA: String?
    /// Anisotropic filtering: nil (16x), "game" (the game's own samplers), "4", "8" or "16".
    public var anisotropy: String?
    /// Upscaling (the launcher's MACNEUTRON_UPSCALE): the game renders into a back buffer this much smaller than its
    /// window and the presenter upscales it with MetalFX. nil (off) or one of `upscalePresets`.
    public var upscale: String?
    /// Apple's Metal performance HUD over the game (MTL_HUD_ENABLED): FPS, frame times, and the labels DXMT and the
    /// presenter publish. nil/false: off.
    public var metalHUD: Bool?
    /// DLSS answered by MetalFX (MACNEUTRON_DLSS): the game sees an NVIDIA GPU, and its DLSS option upscales with
    /// MetalFX's temporal upscaler. nil/false: off.
    public var dlss: Bool?
    /// Frame generation on MetalFX (MACNEUTRON_FRAMEGEN): a frame interpolated between each two the game renders, when
    /// it runs DLSS on MetalFX. nil/false: off.
    public var frameGen: Bool?
    /// The Games window's launch options (`LaunchOptions.parse`): variables over the controls' and game arguments.
    /// Steam's own launch options still win over both. nil: none.
    public var launchOptions: String?

    /// MACNEUTRON_UPSCALE's presets, XeSS's ratios: the back buffer keeps this fraction of the window's pixels.
    public static let upscalePresets = ["ultra-quality", "quality", "balanced", "performance", "ultra-performance"]

    public init(graphics: String? = nil, log: Bool? = nil, msync: Bool? = nil, runAs: RunAs? = nil, metalFX: Bool? = nil,
                postAA: String? = nil, anisotropy: String? = nil, upscale: String? = nil, metalHUD: Bool? = nil,
                dlss: Bool? = nil, frameGen: Bool? = nil, launchOptions: String? = nil) {
        self.graphics = graphics
        self.log = log
        self.msync = msync
        self.runAs = runAs
        self.metalFX = metalFX
        self.postAA = postAA
        self.anisotropy = anisotropy
        self.upscale = upscale
        self.metalHUD = metalHUD
        self.dlss = dlss
        self.frameGen = frameGen
        self.launchOptions = launchOptions
    }

    /// The launch-option variables these settings stand for (`runAs` is for the mapping planner only).
    public var environment: [String: String] {
        var env: [String: String] = [:]
        if let graphics { env["MACNEUTRON_GRAPHICS"] = graphics }
        if log == true { env["MACNEUTRON_LOG"] = "1" }
        if msync == false { env["MACNEUTRON_NO_MSYNC"] = "1" }
        if metalFX == false { env["MACNEUTRON_NO_METALFX"] = "1" }
        if postAA == "cmaa2" { env["MACNEUTRON_POST_AA"] = "cmaa2" }  // any other value: off
        // DXMT's floor on trilinear samplers' anisotropy (DXMT 0030): Metal's anisotropic filter cancels a negative
        // LOD bias (an upscaler's) past the anisotropy ratio, so 16x by default; unknown values get it too.
        switch anisotropy {
        case "game": break
        case "4", "8": env["DXMT_MAX_ANISOTROPY"] = anisotropy
        default: env["DXMT_MAX_ANISOTROPY"] = "16"
        }
        // The upscaler reports the desktop smaller than it is (its preset picks how much smaller) so the game renders
        // fewer pixels; the launcher turns the preset into MACNEUTRON_DISPLAY_SCALE and drops it when MetalFX is off.
        // Unknown values are off.
        if let upscale, Self.upscalePresets.contains(upscale) { env["MACNEUTRON_UPSCALE"] = upscale }
        // Apple's Metal HUD, the same variable a launch option sets (a user's own value wins where the launcher merges
        // environments); its log (MTL_HUD_LOG_ENABLED) and layout variables stay launch options.
        if metalHUD == true { env["MTL_HUD_ENABLED"] = "1" }
        if dlss == true { env["MACNEUTRON_DLSS"] = "1" }
        if frameGen == true { env["MACNEUTRON_FRAMEGEN"] = "1" }
        env.merge(LaunchOptions.parse(launchOptions ?? "").environment) { _, field in field }
        return env
    }

    /// The launch options field's game arguments, after the game's own.
    public var launchArguments: [String] { LaunchOptions.parse(launchOptions ?? "").arguments }
}

/// `games/<appid>.json`, written atomically so the launcher never reads half a file.
public struct GameSettingsStore: Sendable {
    public let directory: URL

    public init(directory: URL = MacNeutronPaths.games) { self.directory = directory }

    func file(_ appID: String) -> URL { directory.appending(path: "\(appID).json") }

    /// Missing file → defaults. A file that exists but can't be decoded throws.
    public func load(_ appID: String) throws -> GameSettings {
        let url = file(appID)
        guard FileManager.default.fileExists(atPath: url.path(percentEncoded: false)) else { return GameSettings() }
        return try JSONDecoder().decode(GameSettings.self, from: Data(contentsOf: url))
    }

    public func save(_ settings: GameSettings, for appID: String) throws {
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        try encoder.encode(settings).write(to: file(appID), options: .atomic)
    }

    /// Every readable settings file, keyed by app ID.
    public func all() -> [String: GameSettings] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: directory.path(percentEncoded: false))) ?? []
        var result: [String: GameSettings] = [:]
        for name in names where name.hasSuffix(".json") {
            let appID = String(name.dropLast(".json".count))
            if let settings = try? load(appID) { result[appID] = settings }
        }
        return result
    }

    /// `runAs` choices for the mapping planner.
    public func runAsOverrides() -> [UInt32: RunAs] {
        var result: [UInt32: RunAs] = [:]
        for (appID, settings) in all() {
            if let id = UInt32(appID), let runAs = settings.runAs { result[id] = runAs }
        }
        return result
    }
}
