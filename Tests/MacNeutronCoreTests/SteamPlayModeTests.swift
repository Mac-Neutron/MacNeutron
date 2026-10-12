import Foundation
import Testing
@testable import MacNeutronCore

func makeMode(session: String = okSession, config: String = steamConfigFixture,
              running: Bool = false) throws -> (SteamPlayMode, FakeSteam) {
    let (steam, root) = try makeFakeSteam(config: config)
    let fake = FakeSteam(steam: steam, running: running, session: session,
                         intentFile: root.appending(path: "steam-play-enabled"))
    var mode = SteamPlayMode(steam: steam, root: root, process: fake)
    mode.verifyTimeout = .seconds(2)
    return (mode, fake)
}

let samplePlan = [
    "0": ToolMapping(tool: "macneutron", priority: 75),
    "1062090": ToolMapping(tool: "macneutron-native", priority: 250),
    "2977660": ToolMapping(tool: "macneutron", priority: 250),
]

private func exists(_ url: URL) -> Bool { FileManager.default.fileExists(atPath: url.path(percentEncoded: false)) }

@Test func enableWritesEverythingAndVerifies() async throws {
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)
    #expect(mode.isWanted)
    #expect(mode.filesIntact)
    #expect(try mode.currentMappings() == samplePlan)
    #expect(fake.launchesWithDevConfig == [true])
    #expect(mode.status(plan: samplePlan) == .on)
    #expect(try String(contentsOf: mode.steam.steamDevConfig, encoding: .utf8) == "@sSteamCmdForcePlatformType linux\n")
}

@Test func failedVerificationRollsBackWithDevConfigRemovedFirst() async throws {
    let (mode, fake) = try makeMode(session: macModeSession)
    let original = try String(contentsOf: mode.steam.configVDF, encoding: .utf8)
    await #expect(throws: SteamPlayError.verificationFailed("Steam started as a Mac client and ignored MacNeutron")) {
        try await mode.enable(plan: samplePlan)
    }
    #expect(!exists(mode.steam.steamDevConfig))
    #expect(!exists(mode.link("macneutron")))
    #expect(try String(contentsOf: mode.steam.configVDF, encoding: .utf8) == original)
    #expect(!mode.isWanted)
    #expect(fake.launchesWithDevConfig == [true, false])
}

@Test func unreadableConfigChangesNothing() async throws {
    let (mode, fake) = try makeMode(config: "\"InstallConfigStore\"\n{\n")
    await #expect(throws: SteamPlayError.self) { try await mode.enable(plan: samplePlan) }
    #expect(!exists(mode.steam.steamDevConfig))
    #expect(!exists(mode.link("macneutron")))
    #expect(fake.launchesWithDevConfig.isEmpty)
}

@Test func mappingsAreNeverWrittenWhileSteamRuns() throws {
    let (mode, _) = try makeMode(running: true)
    #expect(throws: SteamPlayError.steamRunning) { try mode.applyMappings(samplePlan) }
}

@Test func everyWriteIsBackedUpFirst() throws {
    let (mode, _) = try makeMode()
    let original = try Data(contentsOf: mode.steam.configVDF)
    try mode.applyMappings(samplePlan)
    let backups = try FileManager.default.contentsOfDirectory(at: mode.backups, includingPropertiesForKeys: nil)
    #expect(backups.count == 1)
    #expect(try Data(contentsOf: backups[0]) == original)
    #expect(try String(contentsOf: mode.steam.configVDF, encoding: .utf8).contains("\"proton_9\""))
}

@Test func syncOnlyWritesWhenThePlanChanged() async throws {
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)
    try await fake.quit(timeout: .seconds(1))
    #expect(try mode.sync(plan: samplePlan) == false)
    var newPlan = samplePlan
    newPlan["3419430"] = ToolMapping(tool: "macneutron-native", priority: 250)
    #expect(try mode.sync(plan: newPlan) == true)
    #expect(try mode.currentMappings() == newPlan)
}

@Test func statusShowsPendingChangesAndLostFiles() async throws {
    let (mode, _) = try makeMode()
    #expect(mode.status(plan: samplePlan) == .off)
    try await mode.enable(plan: samplePlan)
    var newPlan = samplePlan
    newPlan["1"] = ToolMapping(tool: "macneutron", priority: 250)
    newPlan["2977660"] = nil
    #expect(mode.status(plan: newPlan) == .restartNeeded(2))
    try FileManager.default.removeItem(at: mode.steam.steamDevConfig)  // what a Steam update does
    #expect(mode.status(plan: samplePlan) == .lost)
}

@Test func disableRemovesOnlyWhatEnableAdded() async throws {
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)
    try await mode.disable()
    #expect(!exists(mode.steam.steamDevConfig))
    #expect(!exists(mode.link("macneutron-native")))
    #expect(try mode.currentMappings().isEmpty)
    #expect(try String(contentsOf: mode.steam.configVDF, encoding: .utf8).contains("\"proton_9\""))
    #expect(!mode.isWanted)
    #expect(fake.launchesWithDevConfig == [true, false])
}

@Test(arguments: [
    (okSession, nil),
    (macModeSession, "Steam started as a Mac client and ignored MacNeutron"),
    ("Client version: 1\nRegistering tool macneutron, AppID 0\nRecording non-user mapping", "Steam didn't find MacNeutron's tools"),
    ("Client version: 1\nRegistering tool macneutron, AppID 0\nRegistering tool macneutron-native, AppID 0\n",
     "Steam didn't switch to Steam Play mode"),
] as [(String, String?)])
func verifiesCompatLogSessions(log: String, problem: String?) {
    #expect(SteamPlayMode.verify(log: log) == problem)
}

@Test func lastSessionIgnoresEarlierRuns() {
    #expect(SteamPlayMode.verify(log: SteamPlayMode.lastSession(of: macModeSession + okSession)) == nil)
    #expect(SteamPlayMode.verify(log: SteamPlayMode.lastSession(of: okSession + macModeSession)) != nil)
}


@Test func syncRefusesToDropMacGameProtection() async throws {
    // An unreadable app list must never turn into "no Mac games" while Steam stays in Linux mode.
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)
    try await fake.quit(timeout: .seconds(1))
    #expect(throws: SteamPlayError.planDropsMacGames(1)) {
        try mode.sync(plan: ["0": ToolMapping(tool: "macneutron", priority: 75)])
    }
    #expect(try mode.currentMappings()["1062090"]?.tool == "macneutron-native")
}

@Test func enableBuildsThePlanAfterSteamHasQuit() async throws {
    // Steam rewrites its app list on exit, so a plan built earlier can miss a game bought this session.
    let (mode, fake) = try makeMode(running: true)
    var steamWasRunning: Bool?
    try await mode.enable(planAfterQuit: {
        steamWasRunning = fake.isRunning()
        return samplePlan
    })
    #expect(steamWasRunning == false)
    #expect(try mode.currentMappings() == samplePlan)
}

@Test func intentIsRecordedBeforeSteamStartsInLinuxMode() async throws {
    // If the app dies during verification, it must still know Steam Play mode is on.
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)
    #expect(fake.launchesWithIntent == [true])
    let (failing, failingFake) = try makeMode(session: macModeSession)
    await #expect(throws: SteamPlayError.self) { try await failing.enable(plan: samplePlan) }
    #expect(failingFake.launchesWithIntent == [true, false])
    #expect(!failing.isWanted)
}

@Test func appsMappedToAnotherToolDontCountAsPending() async throws {
    // The fixture maps 440 to proton_9; a plan that also wants 440 must neither replace it nor stay "pending".
    let (mode, _) = try makeMode()
    var plan = samplePlan
    plan["440"] = ToolMapping(tool: "macneutron", priority: 250)
    try await mode.enable(plan: plan)
    #expect(try String(contentsOf: mode.steam.configVDF, encoding: .utf8).contains("\"proton_9\""))
    #expect(mode.status(plan: plan) == .on)
}

@Test func unreadableConfigLeavesSteamRunning() async throws {
    let (mode, fake) = try makeMode(config: "\"InstallConfigStore\"\n{\n", running: true)
    await #expect(throws: SteamPlayError.self) { try await mode.enable(plan: samplePlan) }
    #expect(fake.isRunning())
    #expect(fake.launchesWithDevConfig.isEmpty)
}

@Test func failureBeforeLaunchRestartsSteamInMacMode() async throws {
    let (mode, fake) = try makeMode(running: true)
    try write("not a folder", to: mode.steam.bundleCompatTools)  // makes linking the tools fail
    await #expect(throws: (any Error).self) { try await mode.enable(plan: samplePlan) }
    #expect(fake.isRunning())
    #expect(fake.launchesWithDevConfig == [false])
    #expect(!FileManager.default.fileExists(atPath: mode.steam.steamDevConfig.path(percentEncoded: false)))
}

@Test func verificationHandlesACompatLogThatStartsOver() async throws {
    // If Steam truncates its log at startup, the new session is shorter than the old file.
    let (steam, root) = try makeFakeSteam()
    try write(String(repeating: "[old] Client version: 1 and more\n", count: 200), to: steam.compatLog)
    let fake = FakeSteam(steam: steam, replaceLogOnLaunch: true)
    var mode = SteamPlayMode(steam: steam, root: root, process: fake)
    mode.verifyTimeout = .seconds(2)
    try await mode.enable(plan: samplePlan)
    #expect(mode.isWanted)
}

@Test func verificationHandlesAStartedOverLogThatOutgrewTheOldOne() async throws {
    // Steam appends every session to one log; a log started over can grow past the old size before the first poll.
    let (steam, root) = try makeFakeSteam()
    try write(String(repeating: "[2026-09-26 09:00:00] Client version: 1788652215\n", count: 2), to: steam.compatLog)
    let fake = FakeSteam(steam: steam, replaceLogOnLaunch: true)
    var mode = SteamPlayMode(steam: steam, root: root, process: fake)
    mode.verifyTimeout = .seconds(2)
    try await mode.enable(plan: samplePlan)
    #expect(mode.isWanted)
}

@Test func statusReportsAnUnreadableConfig() async throws {
    let (mode, _) = try makeMode()
    try await mode.enable(plan: samplePlan)
    try write("\"InstallConfigStore\"\n{\n", to: mode.steam.configVDF)
    guard case .problem(let message) = mode.status(plan: samplePlan) else {
        Issue.record("expected .problem, got \(mode.status(plan: samplePlan))")
        return
    }
    #expect(message.contains("couldn't be read"))
}

@Test func nativeToolRunsTheCLIPassthrough() async throws {
    // R0b: Steam launches a thin arm64 tool binary, so the Mac-game tool runs a copy of the CLI.
    let (mode, _) = try makeMode()
    try await mode.enable(plan: samplePlan)
    let native = mode.link("macneutron-native")
    let manifest = try String(contentsOf: native.appending(path: "toolmanifest.vdf"), encoding: .utf8)
    #expect(manifest.contains("\"commandline\" \"/bin/macneutron passthrough %verb%\""))
    let cli = native.appending(path: "bin/macneutron")
    #expect(try Data(contentsOf: cli) == Data(contentsOf: mode.runtimeTool.appending(path: "bin/macneutron")))
    #expect(FileManager.default.isExecutableFile(atPath: cli.path(percentEncoded: false)))
}

@Test func staleScriptIsRemoved() throws {
    let (mode, _) = try makeMode()
    let script = mode.nativeTool.appending(path: "passthrough.sh")
    try write("#!/bin/sh\n", to: script, executable: true)
    try mode.installNativeTool()
    #expect(!FileManager.default.fileExists(atPath: script.path(percentEncoded: false)))
}

@Test func verifyAcceptsEitherRuntimeName() {
    // A Steam session started before the update registered only the old name.
    #expect(SteamPlayMode.verify(log: okSession) == nil)
    #expect(SteamPlayMode.verify(log: okSession.replacingOccurrences(of: "tool macneutron,", with: "tool macneutron-proton,")) == nil)
}

@Test func syncMovesLegacyMappingsThenDropsTheLegacyName() async throws {
    let (mode, fake) = try makeMode()
    try await mode.enable(plan: samplePlan)  // the old name, as installed before the rename
    let folder = mode.tools.appending(path: SteamPlayMode.runtimeFolderName)
    try RuntimeInstaller.writeCompatibilityTool(in: folder, legacyName: true)
    try await fake.quit(timeout: .seconds(1))
    let renamed = samplePlan.mapValues { $0.tool == "macneutron" ? ToolMapping(tool: "macneutron-proton", priority: $0.priority) : $0 }
    #expect(try mode.sync(plan: renamed) == true)
    #expect(try mode.currentMappings() == renamed)
    #expect(!RuntimeInstaller.registersLegacyName(in: folder))
    #expect(FileManager.default.fileExists(atPath: folder.appending(path: "compatibilitytool.vdf").path(percentEncoded: false)))
}
