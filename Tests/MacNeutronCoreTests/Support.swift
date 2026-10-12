import Foundation
@testable import MacNeutronCore

/// This test process's folder, removed when it exits. A run that didn't exit (crash, Ctrl-C) leaves its
/// folder behind; the next run sweeps it once it's a day old. Only "run …" folders are swept.
private func runDir() -> URL {
    FileManager.default.temporaryDirectory.appending(path: "macneutron tests/run \(getpid())", directoryHint: .isDirectory)
}

private let sweptAndCleanedAtExit: Void = {
    let fm = FileManager.default
    let parent = runDir().deletingLastPathComponent()
    let dayAgo = Date(timeIntervalSinceNow: -86_400)
    for name in (try? fm.contentsOfDirectory(atPath: parent.path(percentEncoded: false))) ?? [] where name.hasPrefix("run ") {
        let old = parent.appending(path: name, directoryHint: .isDirectory)
        if let modified = try? old.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate, modified < dayAgo {
            try? fm.removeItem(at: old)
        }
    }
    atexit { try? FileManager.default.removeItem(at: runDir()) }
}()

/// A fresh temp directory whose path contains a space, like Steam's "Application Support".
func makeTempDir() throws -> URL {
    _ = sweptAndCleanedAtExit
    let dir = runDir().appending(path: UUID().uuidString, directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    return dir
}

func write(_ text: String, to url: URL, executable: Bool = false) throws {
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try text.write(to: url, atomically: true, encoding: .utf8)
    if executable {
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path(percentEncoded: false))
    }
}

/// Records every process the code under test would start, and answers with `respond`.
final class FakeRunner: ProcessRunner, @unchecked Sendable {
    struct Call: Equatable {
        let tool: String
        let arguments: [String]
        let environment: [String: String]
        let output: URL?
    }

    private let lock = NSLock()
    private var recorded: [Call] = []
    private let respond: @Sendable (Call) -> Int32

    init(respond: @escaping @Sendable (Call) -> Int32 = { _ in 0 }) { self.respond = respond }

    var calls: [Call] { lock.withLock { recorded } }

    func run(_ executable: URL, _ arguments: [String], environment: [String: String], output: URL?) throws -> Int32 {
        let call = Call(tool: executable.lastPathComponent, arguments: arguments, environment: environment, output: output)
        lock.withLock { recorded.append(call) }
        return respond(call)
    }
}

/// A FakeRunner whose `wineboot` creates the prefix like the real one does.
func winebootCreatingPrefix(status: Int32 = 0, delay: TimeInterval = 0) -> FakeRunner {
    FakeRunner { call in
        if call.arguments.first == "wineboot", let prefix = call.environment["WINEPREFIX"] {
            Thread.sleep(forTimeInterval: delay)
            if status == 0 { try? FileManager.default.createDirectory(atPath: prefix, withIntermediateDirectories: true) }
            return status
        }
        return 0
    }
}

/// A tool folder with a fake, unsigned `wine.app`: executable wine/wineserver, DXMT's DLLs, translator key and replayer,
/// both halves of lsteamclient, and `CFBundleShortVersionString` `test`. Tests that launch inject the identity.
func makeToolLayout() throws -> ToolLayout {
    // The installed shape, MacNeutron/compatibilitytools.d/macneutron: `valveCache` is then this test's own folder.
    let layout = ToolLayout(root: try makeTempDir().appending(path: "compatibilitytools.d/macneutron", directoryHint: .isDirectory))
    try write("#!/bin/sh\n", to: layout.wine, executable: true)
    try write("#!/bin/sh\n", to: layout.wineserver, executable: true)
    let plist = ["CFBundleExecutable": "wine", "CFBundleShortVersionString": "test"]
    try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
        .write(to: layout.wineApp.appending(path: "Contents/Info.plist"))
    for dll in ToolLayout.dxmtDLLs { try write("dxmt \(dll)", to: layout.dxmt.appending(path: dll)) }
    try write("dxmt replay", to: layout.dxmtReplay)
    try write("dxmt-test\n", to: layout.dxmtTranslatorFile)
    try write("lsteamclient aarch64", to: layout.lsteamclient)
    try write("lsteamclient.so", to: layout.lsteamclientUnix)
    return layout
}

/// What launcher, prefix and precache tests use: no codesign, a fixed identity.
let testIdentity = "0123456789abcdef0123456789abcdef01234567"
let testPreflight = Preflight(systemSupported: { true }, identity: { _ in testIdentity })

func steamEnvironment(dataPath: URL, appID: String = "3419430") -> [String: String] {
    ["STEAM_COMPAT_DATA_PATH": dataPath.path(percentEncoded: false), "SteamAppId": appID, "PATH": "/usr/bin:/bin"]
}

/// A Steam root in a temp folder. `loginUsers` becomes config/loginusers.vdf; `steamClient` puts a
/// steamclient.dylib in the bundle's MacOS folder.
func makeSteamLocation(loginUsers: String? = nil, steamClient: Bool = true) throws -> SteamLocation {
    let steam = SteamLocation(root: try makeTempDir().appending(path: "Steam", directoryHint: .isDirectory))
    if let loginUsers { try write(loginUsers, to: steam.loginUsers) }
    if steamClient { try write("dylib", to: steam.bundleMacOS.appending(path: "steamclient.dylib")) }
    return steam
}

/// One user block of loginusers.vdf. SteamID64 76561197960265728 + n belongs to account n.
func loginUser(account: Int, timestamp: Int, mostRecent: Bool? = nil) -> String {
    var block = "\t\"\(76561197960265728 + account)\"\n\t{\n"
        + "\t\t\"AccountName\"\t\t\"user\(account)\"\n\t\t\"Timestamp\"\t\t\"\(timestamp)\"\n"
    if let mostRecent { block += "\t\t\"MostRecent\"\t\t\"\(mostRecent ? 1 : 0)\"\n" }
    return block + "\t}\n"
}

func loginUsersFile(_ users: String...) -> String { "\"users\"\n{\n" + users.joined() + "}\n" }

/// `steam.exe` in the tool folder: with the fake wine.app's lsteamclient, the bridge counts as installed.
func installFakeSteamBridge(in layout: ToolLayout) throws {
    try write("steam.exe", to: layout.steamHelper)
}

/// A minimal PE: 64-byte DOS header with `e_lfanew` 0x80, padding, `PE\0\0`, then the COFF machine.
func peBytes(machine: UInt16) -> Data {
    var bytes = [UInt8](repeating: 0, count: 0x86)
    bytes[0] = 0x4D; bytes[1] = 0x5A
    bytes[0x3C] = 0x80
    bytes[0x80] = 0x50; bytes[0x81] = 0x45
    bytes[0x84] = UInt8(machine & 0xFF); bytes[0x85] = UInt8(machine >> 8)
    return Data(bytes)
}

/// An ad-hoc-signed `dir/wine.app` whose `Contents/MacOS/wine` is a copy of `loader` (never run). Sign it last:
/// anything written into the bundle afterwards breaks `codesign --verify`.
@discardableResult
func makeSignedWineApp(at dir: URL, loader: URL = URL(filePath: "/usr/bin/true"), shortVersion: String = "test",
                       bundleVersion: String = "1") throws -> URL {
    let bundle = dir.appending(path: "wine.app", directoryHint: .isDirectory)
    let macOS = bundle.appending(path: "Contents/MacOS", directoryHint: .isDirectory)
    try FileManager.default.createDirectory(at: macOS, withIntermediateDirectories: true)
    let plist: [String: String] = ["CFBundleExecutable": "wine", "CFBundleIdentifier": "test.wine",
                                   "CFBundleShortVersionString": shortVersion, "CFBundleVersion": bundleVersion]
    try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
        .write(to: bundle.appending(path: "Contents/Info.plist"))
    try FileManager.default.copyItem(at: loader, to: macOS.appending(path: "wine"))
    let status = try SystemProcessRunner().run(URL(filePath: "/usr/bin/codesign"),
                                               ["-s", "-", "-f", bundle.path(percentEncoded: false)],
                                               environment: [:], output: URL(filePath: "/dev/null"))  // "replacing existing signature"
    guard status == 0 else { throw CocoaError(.executableLoad) }
    return bundle
}
