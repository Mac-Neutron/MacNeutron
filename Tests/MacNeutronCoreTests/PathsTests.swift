import Foundation
import Testing
@testable import MacNeutronCore

@Test func readsSteamCompatEnvironment() throws {
    let context = try CompatContext(environment: [
        "STEAM_COMPAT_DATA_PATH": "/Users/me/Library/Application Support/Steam/steamapps/compatdata/42",
        "SteamAppId": "42",
    ])
    #expect(context.appID == "42")
    #expect(context.prefix.path(percentEncoded: false)
        == "/Users/me/Library/Application Support/Steam/steamapps/compatdata/42/pfx/")
    #expect(context.versionFile.lastPathComponent == "version")
    #expect(context.lockFile.lastPathComponent == "macneutron.lock")
}

@Test func appIDDefaultsToZero() throws {
    let context = try CompatContext(environment: ["STEAM_COMPAT_DATA_PATH": "/tmp/x", "SteamAppId": ""])
    #expect(context.appID == "0")
}

@Test func missingDataPathIsAnError() {
    #expect(throws: CompatContextError.missing("STEAM_COMPAT_DATA_PATH")) {
        try CompatContext(environment: ["SteamAppId": "42"])
    }
}

@Test func layoutPathsFollowWineApp() {
    let layout = ToolLayout(root: URL(filePath: "/t/macneutron", directoryHint: .isDirectory))
    let app = "/t/macneutron/wine.app/Contents"
    #expect(layout.wineApp.path(percentEncoded: false) == "/t/macneutron/wine.app/")
    #expect(layout.wine.path(percentEncoded: false) == "\(app)/MacOS/wine")
    #expect(layout.wineserver.path(percentEncoded: false) == "\(app)/Resources/bin/wineserver")
    #expect(layout.dxmt.path(percentEncoded: false) == "\(app)/Resources/DXMT/aarch64-windows/")
    #expect(layout.dxmtTranslatorFile.path(percentEncoded: false) == "\(app)/Resources/DXMT/translator")
    #expect(layout.dxmtReplay.path(percentEncoded: false) == "\(app)/Resources/DXMT/aarch64-windows/dxmt-replay.exe")
    #expect(layout.lsteamclient.path(percentEncoded: false) == "\(app)/Resources/lib/wine/aarch64-windows/lsteamclient.dll")
    #expect(layout.lsteamclientUnix.path(percentEncoded: false) == "\(app)/Resources/lib/wine/aarch64-unix/lsteamclient.so")
    #expect(layout.launcherBinary.path(percentEncoded: false) == "/t/macneutron/bin/macneutron")
    #expect(layout.steamHelper.path(percentEncoded: false) == "/t/macneutron/bin/steam.exe")
    #expect(layout.runtimeDamagedMarker.path(percentEncoded: false) == "/t/macneutron/runtime-damaged")
}

@Test func valveCacheSitsBesideTheToolFolders() {
    // ~/Library/Application Support/MacNeutron/{compatibilitytools.d/macneutron, Valve}
    #expect(ToolLayout(root: ToolLayout.defaultRoot).valveCache.path(percentEncoded: false)
        == FileManager.default.homeDirectoryForCurrentUser
            .appending(path: "Library/Application Support/MacNeutron/Valve", directoryHint: .isDirectory).path(percentEncoded: false))
}

@Test func genuineSteamclientSupportIsTheRuntimesMarker() throws {
    let layout = try makeToolLayout()
    #expect(!layout.supportsGenuineSteamclient)
    try write("0036\n", to: layout.wineApp.appending(path: "Contents/Resources/genuine-steamclient"))
    #expect(layout.supportsGenuineSteamclient)
}

@Test func layoutFromExecutableIsTwoLevelsUp() {
    let layout = ToolLayout(executable: URL(filePath: "/t/macneutron/bin/macneutron"))
    #expect(layout.root.path(percentEncoded: false).hasSuffix("/t/macneutron/"))
}

@Test func runtimeVersionComesFromWineAppInfoPlist() throws {
    let layout = ToolLayout(root: try makeTempDir())
    #expect(layout.runtimeVersion == nil)
    let plist = layout.wineApp.appending(path: "Contents/Info.plist")
    try write("<plist><dict><key>CFBundleShortVersionString</key><string>0.1.0</string></dict></plist>", to: plist)
    #expect(layout.runtimeVersion == "0.1.0")
    // Read each time: the app keeps one layout while installs swap wine.app underneath it.
    try write("<plist><dict><key>CFBundleShortVersionString</key><string>0.1.1</string></dict></plist>", to: plist)
    #expect(layout.runtimeVersion == "0.1.1")
}

@Test func runtimeLabelFormats() {
    #expect(ToolLayout.runtimeLabel(version: "test", identity: testIdentity) == "test (0123456789ab)")
    #expect(ToolLayout.runtimeLabel(version: nil, identity: nil) == "unknown (unsigned)")
}
