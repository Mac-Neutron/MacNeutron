import Foundation
import Testing
@testable import MacNeutronCore

@Test func launchOptionsSplitIntoVariablesAndArguments() {
    let plain = LaunchOptions.parse("DXMT_D3D12_SM6=0 MACNEUTRON_LOG=1 -dx12 -windowed")
    #expect(plain.environment == ["DXMT_D3D12_SM6": "0", "MACNEUTRON_LOG": "1"])
    #expect(plain.arguments == ["-dx12", "-windowed"])
    let pasted = LaunchOptions.parse("/usr/bin/env A=1 -foo %command% -bar")
    #expect(pasted.environment == ["A": "1"])
    #expect(pasted.arguments == ["-foo", "-bar"])
    #expect(LaunchOptions.parse("A=1 A=2").environment == ["A": "2"])  // the later one wins
    #expect(LaunchOptions.parse("-x B=2").environment.isEmpty)  // after an argument: an argument
    #expect(LaunchOptions.parse("-x B=2").arguments == ["-x", "B=2"])
    #expect(LaunchOptions.parse("1A=2").arguments == ["1A=2"])  // not a shell name
    #expect(LaunchOptions.parse("  ").environment.isEmpty && LaunchOptions.parse("").arguments.isEmpty)
}

@Test func steamLinesAreEachAppsLaunchOptions() throws {
    let file = try makeTempDir().appending(path: "localconfig.vdf")
    try write("""
        "UserLocalConfigStore" { "Software" { "Valve" { "Steam" { "apps" {
          "42" { "LaunchOptions" "/usr/bin/env MACNEUTRON_LOG=1 %command%" }
          "43" { "LaunchOptions" "" }
          "44" { "LastPlayed" "1" }
        } } } } }
        """, to: file)
    #expect(LaunchOptions.steamLines(localConfig: file) == [42: "/usr/bin/env MACNEUTRON_LOG=1 %command%"])
    try write("{ not vdf", to: file)
    #expect(LaunchOptions.steamLines(localConfig: file).isEmpty)
    #expect(LaunchOptions.steamLines(localConfig: file.appending(path: "missing")).isEmpty)
}

@Test func steamLineProblems() {
    #expect(LaunchOptions.problems(steamLine: "MACNEUTRON_LOG=1 %command%", appEnvironment: [:])
        == ["Steam can't start the game with this line: begin it with /usr/bin/env"])
    #expect(LaunchOptions.problems(steamLine: "/usr/bin/env MACNEUTRON_LOG=1 DXMT_MAX_ANISOTROPY=4 %command%",
                                   appEnvironment: ["DXMT_MAX_ANISOTROPY": "16", "MACNEUTRON_LOG": "1", "X": "1"])
        == ["Steam's launch options override: DXMT_MAX_ANISOTROPY, MACNEUTRON_LOG"])
    #expect(LaunchOptions.problems(steamLine: "/usr/bin/env %command% -dx12", appEnvironment: ["A": "1"]).isEmpty)
}
