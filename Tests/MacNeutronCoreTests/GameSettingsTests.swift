import Foundation
import Testing
@testable import MacNeutronCore

@Test func settingsBecomeLaunchVariables() {
    #expect(GameSettings(graphics: "dxmt", log: true, msync: false, runAs: .windows, metalFX: false).environment == [
        "MACNEUTRON_GRAPHICS": "dxmt", "MACNEUTRON_LOG": "1", "MACNEUTRON_NO_MSYNC": "1", "MACNEUTRON_NO_METALFX": "1",
        "DXMT_MAX_ANISOTROPY": "16",
    ])
    #expect(GameSettings(log: false, msync: true, metalFX: true, anisotropy: "game").environment.isEmpty)
    #expect(GameSettings(postAA: "cmaa2").environment["MACNEUTRON_POST_AA"] == "cmaa2")
    #expect(GameSettings(postAA: "off").environment["MACNEUTRON_POST_AA"] == nil)
    #expect(GameSettings(postAA: "smaa").environment["MACNEUTRON_POST_AA"] == nil)  // unknown: off
    // Apple's Metal HUD: on means MTL_HUD_ENABLED=1, off means nothing (its default).
    #expect(GameSettings(metalHUD: true).environment["MTL_HUD_ENABLED"] == "1")
    #expect(GameSettings(metalHUD: false).environment["MTL_HUD_ENABLED"] == nil)
    #expect(GameSettings().environment["MTL_HUD_ENABLED"] == nil)
    // DLSS answered by MetalFX: on means MACNEUTRON_DLSS=1, off means nothing.
    #expect(GameSettings(dlss: true).environment["MACNEUTRON_DLSS"] == "1")
    #expect(GameSettings(dlss: false).environment["MACNEUTRON_DLSS"] == nil)
    #expect(GameSettings().environment["MACNEUTRON_DLSS"] == nil)
    #expect(GameSettings(frameGen: true).environment["MACNEUTRON_FRAMEGEN"] == "1")
    #expect(GameSettings(frameGen: false).environment["MACNEUTRON_FRAMEGEN"] == nil)
}

@Test func upscalingPresetsBecomeALaunchVariable() {
    func preset(_ value: String?) -> String? { GameSettings(upscale: value).environment["MACNEUTRON_UPSCALE"] }
    #expect(preset(nil) == nil)
    #expect(preset("ultra-quality") == "ultra-quality")
    #expect(preset("ultra-performance") == "ultra-performance")
    #expect(preset("off") == nil)
    #expect(preset("dlss") == nil)  // unknown: off
}

@Test func anisotropicFilteringDefaultsTo16x() {
    func forced(_ value: String?) -> String? { GameSettings(anisotropy: value).environment["DXMT_MAX_ANISOTROPY"] }
    #expect(forced(nil) == "16")
    #expect(forced("game") == nil)  // the game's own samplers
    #expect(forced("4") == "4")
    #expect(forced("8") == "8")
    #expect(forced("16") == "16")
    #expect(forced("2") == "16")  // unknown: the default
    #expect(forced("x") == "16")
}

@Test func storeRoundTripsAndDefaultsWhenMissing() throws {
    let store = GameSettingsStore(directory: try makeTempDir().appending(path: "games"))
    #expect(try store.load("7") == GameSettings())
    try store.save(GameSettings(graphics: "wined3d", runAs: .windows), for: "7")
    #expect(try store.load("7") == GameSettings(graphics: "wined3d", runAs: .windows))
    #expect(store.runAsOverrides() == [7: .windows])
}

@Test func corruptFilesThrowAndAreSkippedByAll() throws {
    let store = GameSettingsStore(directory: try makeTempDir())
    try write("{", to: store.directory.appending(path: "8.json"))
    #expect(throws: (any Error).self) { try store.load("8") }
    #expect(store.all().isEmpty)
}

@Test func oldSettingsFilesStillLoad() throws {
    // Written by a Rosetta-era build: `avx` is gone and is ignored; the launcher reads `d3dmetal` as DXMT.
    let store = GameSettingsStore(directory: try makeTempDir())
    try write(#"{"avx":false,"graphics":"d3dmetal"}"#, to: store.directory.appending(path: "9.json"))
    #expect(try store.load("9") == GameSettings(graphics: "d3dmetal"))
    // Written before the anisotropy setting: the default, 16x.
    try write(#"{"metalFX":false,"postAA":"cmaa2"}"#, to: store.directory.appending(path: "10.json"))
    #expect(try store.load("10").environment["DXMT_MAX_ANISOTROPY"] == "16")
}
