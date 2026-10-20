import Foundation
import Testing
@testable import MacNeutronCore

@Test func findsLibrariesAndInstalledApps() throws {
    let (steam, _) = try makeFakeSteam()
    let extra = try makeTempDir().appending(path: "Games Drive", directoryHint: .isDirectory)
    try write("""
        "libraryfolders"
        {
        \t"0"
        {
        \t\t"path"\t\t"\(steam.root.path(percentEncoded: false))"
        \t}
        \t"1"
        \t{
        \t\t"path"\t\t"\(extra.path(percentEncoded: false))"
        \t}
        }
        """, to: steam.root.appending(path: "steamapps/libraryfolders.vdf"))
    try write("", to: steam.root.appending(path: "steamapps/appmanifest_1062090.acf"))
    try write("", to: extra.appending(path: "steamapps/appmanifest_2977660.acf"))
    #expect(steam.libraries().count == 2)
    #expect(steam.installedAppIDs() == [1062090, 2977660])
}

@Test func localConfigIsTheMostRecentUsers() throws {
    let steam = SteamLocation(root: try makeTempDir())
    #expect(steam.localConfig == nil)  // no users at all
    try write("", to: steam.root.appending(path: "userdata/12345/config/localconfig.vdf"))
    #expect(steam.localConfig?.path(percentEncoded: false).hasSuffix("userdata/12345/config/localconfig.vdf") == true)  // the only folder
    try write("", to: steam.root.appending(path: "userdata/999/config/localconfig.vdf"))
    #expect(steam.localConfig == nil)  // two folders, no MostRecent
    try write("\"users\"\n{\n\t\"76561197960278073\"\n\t{\n\t\t\"MostRecent\"\t\t\"1\"\n\t}\n}\n",
              to: steam.root.appending(path: "config/loginusers.vdf"))
    #expect(steam.localConfig?.path(percentEncoded: false).hasSuffix("userdata/12345/config/localconfig.vdf") == true)
}
