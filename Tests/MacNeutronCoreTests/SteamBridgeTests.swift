import Foundation
import Testing
@testable import MacNeutronCore

@Test func windowsPathMapsUnixPathsToDriveZ() {
    #expect(SteamBridge.windowsPath("/Users/me/Steam Library/Gäme/Game.exe") == #"Z:\Users\me\Steam Library\Gäme\Game.exe"#)
    #expect(SteamBridge.windowsPath(#"C:\Games\Game.exe"#) == #"C:\Games\Game.exe"#)
}

@Test func clientDirectoryKeepsSteamsValueWhenItHoldsTheLibrary() throws {
    let steam = try makeSteamLocation()
    let other = try makeTempDir()
    try write("dylib", to: other.appending(path: "steamclient.dylib"))
    let value = other.path(percentEncoded: false)
    #expect(SteamBridge.clientDirectory(steamValue: value, steam: steam) == value)
}

@Test func clientDirectoryFallsBackToTheSteamBundle() throws {
    let steam = try makeSteamLocation()
    let bundle = String(steam.bundleMacOS.path(percentEncoded: false).dropLast())  // no trailing slash
    #expect(SteamBridge.clientDirectory(steamValue: steam.root.path(percentEncoded: false), steam: steam) == bundle)
    #expect(SteamBridge.clientDirectory(steamValue: nil, steam: steam) == bundle)
}

/// macOS Steam's registry.vdf with `language` under HKCU/Software/Valve/Steam, and a sibling block's own `language`.
func registryVDF(language: String) -> String {
    "\"Registry\"\n{\n\t\"HKCU\"\n\t{\n\t\t\"Software\"\n\t\t{\n\t\t\t\"Valve\"\n\t\t\t{\n\t\t\t\t\"Steam\"\n\t\t\t\t{\n"
        + "\t\t\t\t\t\"language\"\t\t\"\(language)\"\n\t\t\t\t}\n\t\t\t\t\"Steamsteamglobal\"\n\t\t\t\t{\n"
        + "\t\t\t\t\t\"language\"\t\t\"german\"\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n"
}

@Test func uiLanguageComesFromRegistryVDF() throws {
    let steam = try makeSteamLocation()
    try write(registryVDF(language: "french"), to: steam.registryVDF)
    #expect(steam.uiLanguage() == "french")
    #expect(try makeSteamLocation().uiLanguage() == nil)
}

@Test func mostRecentUserIsTheActiveAccount() throws {
    let steam = try makeSteamLocation(loginUsers: loginUsersFile(
        loginUser(account: 1, timestamp: 200, mostRecent: false),
        loginUser(account: 2, timestamp: 100, mostRecent: true)))
    #expect(steam.activeAccountID() == 2)
}

@Test func newestTimestampWinsWithoutMostRecent() throws {
    // The maintainer's loginusers.vdf has no MostRecent key at all.
    let steam = try makeSteamLocation(loginUsers: loginUsersFile(
        loginUser(account: 1, timestamp: 100), loginUser(account: 2, timestamp: 300), loginUser(account: 3, timestamp: 200)))
    #expect(steam.activeAccountID() == 2)
}

@Test func noLoginUsersMeansNoAccount() throws {
    #expect(try makeSteamLocation().activeAccountID() == nil)
    #expect(try makeSteamLocation(loginUsers: "\"users\"\n{\n}\n").activeAccountID() == nil)
}

@Test func bridgeNeedsSteamExeAndBothHalvesOfTheClient() throws {
    let layout = try makeToolLayout()
    #expect(!layout.steamBridgeInstalled)  // wine.app has lsteamclient; the tool folder has no steam.exe yet
    try installFakeSteamBridge(in: layout)
    #expect(layout.steamBridgeInstalled)
    for half in [layout.lsteamclientUnix, layout.lsteamclient] {
        let saved = try Data(contentsOf: half)
        try FileManager.default.removeItem(at: half)
        #expect(!layout.steamBridgeInstalled)
        try saved.write(to: half)
    }
    #expect(layout.steamBridgeInstalled)
}
