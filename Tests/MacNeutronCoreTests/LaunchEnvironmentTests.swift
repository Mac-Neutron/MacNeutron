import Foundation
import Testing
@testable import MacNeutronCore

private let context = try! CompatContext(environment: ["STEAM_COMPAT_DATA_PATH": "/c/42", "SteamAppId": "42"])

@Test func setsPrefixOverridesAndDefaults() {
    let env = LaunchEnvironment.build(base: ["PATH": "/usr/bin"], context: context, backend: .dxmt, logging: false)
    #expect(env["WINEPREFIX"] == "/c/42/pfx/")
    #expect(env["WINEDLLOVERRIDES"] == "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=n,b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b")
    #expect(env["WINEDEBUG"] == "-all")
    #expect(env["ROSETTA_ADVERTISE_AVX"] == nil)
    #expect(env["WINEMSYNC"] == "1")
    #expect(env["PATH"] == "/usr/bin")
}

@Test func wined3dOverridesUseBuiltins() {
    let env = LaunchEnvironment.build(base: [:], context: context, backend: .wined3d, logging: false)
    #expect(env["WINEDLLOVERRIDES"] == "dxgi=b;d3d9=b;d3d10=b;d3d10core=b;d3d11=b;d3d12=b;libxess=b;libxess_dx11=b")
}

@Test func loggingTurnsOnWineDebugChannels() {
    // MACNEUTRON_LOG=1: no seh warnings (Unreal logs through OutputDebugString) and no per-frame steamclient trace.
    let env = LaunchEnvironment.build(base: ["MACNEUTRON_LOG": "1"], context: context, backend: .dxmt, logging: true)
    #expect(env["WINEDEBUG"] == "warn+all,warn-seh,+loaddll,warn+steamclient")
    let full = LaunchEnvironment.build(base: ["MACNEUTRON_LOG": "2"], context: context, backend: .dxmt, logging: true)
    #expect(full["WINEDEBUG"] == "warn+all,+loaddll,+steamclient")
}

@Test func userSettingsWin() {
    let env = LaunchEnvironment.build(
        base: ["WINEDEBUG": "+seh", "WINEDLLOVERRIDES": "d3d11=b;xinput1_3=n"],
        context: context, backend: .dxmt, logging: true)
    #expect(env["WINEDEBUG"] == "+seh")
    #expect(env["WINEDLLOVERRIDES"] == "dxgi=n,b;d3d10core=n,b;d3d11=b;d3d12=n,b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b;xinput1_3=n")
}

@Test func optOutsDropDefaults() {
    let env = LaunchEnvironment.build(base: ["MACNEUTRON_NO_MSYNC": "1"], context: context, backend: .dxmt, logging: false)
    #expect(env["WINEMSYNC"] == nil)
    // Off means unset, even when a launch option also set it: client and server must agree.
    let set = LaunchEnvironment.build(base: ["MACNEUTRON_NO_MSYNC": "1", "WINEMSYNC": "1"], context: context,
                                      backend: .dxmt, logging: false)
    #expect(set["WINEMSYNC"] == nil)
}

@Test func mergeKeepsDisabledEntries() {
    #expect(LaunchEnvironment.mergeOverrides("a=b", user: "c=") == "a=b;c=")
}

@Test func userOverridesWin() {
    let env = LaunchEnvironment.build(base: ["WINEDLLOVERRIDES": "d3d12=b"], context: context, backend: .dxmt, logging: false)
    #expect(env["WINEDLLOVERRIDES"] == "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=b;d3d9=b;d3d10=b;libxess=b;libxess_dx11=b")
}

// XeSS's DP4a path runs SMITE 2 at ~4 FPS on Apple GPUs: our builtin libxess answers XeSS with MetalFX instead.
@Test func xessBridgeIsTheDefaultUnlessOptedOut() {
    func overrides(_ base: [String: String]) -> String? {
        LaunchEnvironment.build(base: base, context: context, backend: .dxmt, logging: false)["WINEDLLOVERRIDES"]
    }
    #expect(overrides([:])?.hasSuffix(";libxess=b;libxess_dx11=b") == true)
    #expect(overrides(["MACNEUTRON_XESS": "1"]) == "dxgi=n,b;d3d10core=n,b;d3d11=n,b;d3d12=n,b;d3d9=b;d3d10=b")
    #expect(overrides(["WINEDLLOVERRIDES": "libxess=n"])?.hasSuffix(";libxess=n;libxess_dx11=b") == true)
}

@Test func recordsPipelinesForDXMTOnly() {
    func record(_ base: [String: String], _ backend: GraphicsBackend) -> String? {
        LaunchEnvironment.build(base: base, context: context, backend: backend, logging: false)["DXMT_PIPELINE_RECORD"]
    }
    #expect(record([:], .dxmt) == "/c/42/dxmt-pipelines")
    #expect(record([:], .wined3d) == nil)
    #expect(record(["MACNEUTRON_PRECACHE": "0"], .dxmt) == nil)
    #expect(record(["DXMT_PIPELINE_RECORD": "/mine"], .dxmt) == "/mine")
}

@Test func postAntiAliasingReachesWine() {
    let env = LaunchEnvironment.build(base: GameSettings(postAA: "cmaa2").environment, context: context, backend: .dxmt,
                                      logging: false)
    #expect(env["MACNEUTRON_POST_AA"] == "cmaa2")
}

// MACNEUTRON_UPSCALE reports a smaller desktop to the game (XeSS's ratios); the MetalFX presenter upscales the window
// back up. Wine's Mac driver reads the ratio from MACNEUTRON_DISPLAY_SCALE.
@Test func upscalePresetsReportASmallerDesktop() {
    func ratio(_ base: [String: String]) -> String? {
        LaunchEnvironment.build(base: base, context: context, backend: .dxmt, logging: false)["MACNEUTRON_DISPLAY_SCALE"]
    }
    #expect(ratio(["MACNEUTRON_UPSCALE": "ultra-quality"]) == "0.7692")
    #expect(ratio(["MACNEUTRON_UPSCALE": "quality"]) == "0.6667")
    #expect(ratio(["MACNEUTRON_UPSCALE": "balanced"]) == "0.5882")
    #expect(ratio(["MACNEUTRON_UPSCALE": "performance"]) == "0.5")
    #expect(ratio(["MACNEUTRON_UPSCALE": "ultra-performance"]) == "0.3333")
    // Off by default; an unknown preset, the user's own ratio, or MetalFX off changes nothing.
    #expect(ratio([:]) == nil)
    #expect(ratio(["MACNEUTRON_UPSCALE": "nonsense"]) == nil)
    #expect(ratio(["MACNEUTRON_UPSCALE": "quality", "MACNEUTRON_DISPLAY_SCALE": "0.9"]) == "0.9")
    #expect(ratio(["MACNEUTRON_UPSCALE": "quality", "MACNEUTRON_NO_METALFX": "1"]) == nil)

    // The presenter sharpens what it upscales (MetalFX's spatial scaler has no sharpness of its own), unless the user
    // set their own strength; no upscale, no sharpening.
    func sharpen(_ base: [String: String]) -> String? {
        LaunchEnvironment.build(base: base, context: context, backend: .dxmt, logging: false)["MACNEUTRON_SHARPEN"]
    }
    #expect(sharpen(["MACNEUTRON_UPSCALE": "quality"]) == LaunchEnvironment.upscaleSharpness)
    #expect(sharpen(["MACNEUTRON_UPSCALE": "quality", "MACNEUTRON_SHARPEN": "0.9"]) == "0.9")
    #expect(sharpen(["MACNEUTRON_UPSCALE": "quality", "MACNEUTRON_SHARPEN": "0"]) == "0")
    #expect(sharpen([:]) == nil)
    #expect(sharpen(["MACNEUTRON_UPSCALE": "quality", "MACNEUTRON_NO_METALFX": "1"]) == nil)
}

// MACNEUTRON_DLSS=1 turns DXMT's NVIDIA extension on (an NVIDIA adapter, DXMT's nvapi64 and nvngx), on DXMT only;
// the user's own DXMT_ENABLE_NVEXT wins.
@Test func dlssTurnsOnDXMTsNvidiaExtension() {
    func nvext(_ base: [String: String], _ backend: GraphicsBackend = .dxmt) -> String? {
        LaunchEnvironment.build(base: base, context: context, backend: backend, logging: false)["DXMT_ENABLE_NVEXT"]
    }
    #expect(nvext([:]) == nil)
    #expect(nvext(["MACNEUTRON_DLSS": "1"]) == "1")
    #expect(nvext(["MACNEUTRON_DLSS": "0"]) == nil)
    #expect(nvext(["MACNEUTRON_DLSS": "1"], .wined3d) == nil)
    #expect(nvext(["MACNEUTRON_DLSS": "1", "DXMT_ENABLE_NVEXT": "0"]) == "0")
}

@Test func anisotropicFilteringReachesWine() {
    func forced(_ settings: GameSettings, _ options: [String: String] = [:]) -> String? {
        LaunchEnvironment.build(base: settings.environment.merging(options) { _, option in option }, context: context,
                                backend: .dxmt, logging: false)["DXMT_MAX_ANISOTROPY"]
    }
    #expect(forced(GameSettings()) == "16")
    #expect(forced(GameSettings(anisotropy: "8")) == "8")
    #expect(forced(GameSettings(anisotropy: "game")) == nil)
    #expect(forced(GameSettings(), ["DXMT_MAX_ANISOTROPY": "0"]) == "0")  // a launch option wins
}

@Test func steamsProtonFlagIsRemoved() {
    // Steam sets it for tools named "proton"; the game's environment stays as it was before the rename.
    let env = LaunchEnvironment.build(base: ["STEAM_COMPAT_PROTON": "1"], context: context, backend: .dxmt, logging: false)
    #expect(env["STEAM_COMPAT_PROTON"] == nil)
}
