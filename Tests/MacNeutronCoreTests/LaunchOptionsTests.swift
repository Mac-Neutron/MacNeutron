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
