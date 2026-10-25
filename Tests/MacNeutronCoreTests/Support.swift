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

/// The runtime's version as wine.app's Info.plist gives it: "0.7.1p" makes a performance build (X.Y.Zp).
func setRuntimeVersion(_ version: String, of layout: ToolLayout) throws {
    let plist = ["CFBundleExecutable": "wine", "CFBundleShortVersionString": version]
    try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
        .write(to: layout.wineApp.appending(path: "Contents/Info.plist"))
}

/// What launcher, prefix and precache tests use: no codesign, a fixed identity.
let testIdentity = "0123456789abcdef0123456789abcdef01234567"
let testPreflight = Preflight(systemSupported: { true }, identity: { _ in testIdentity })

func steamEnvironment(dataPath: URL, appID: String = "3419430") -> [String: String] {
    ["STEAM_COMPAT_DATA_PATH": dataPath.path(percentEncoded: false), "SteamAppId": appID, "PATH": "/usr/bin:/bin"]
}

/// A Steam root in a temp folder; `steamClient` puts a steamclient.dylib in the bundle's MacOS folder.
func makeSteamLocation(steamClient: Bool = true) throws -> SteamLocation {
    let steam = SteamLocation(root: try makeTempDir().appending(path: "Steam", directoryHint: .isDirectory))
    if steamClient { try write("dylib", to: steam.bundleMacOS.appending(path: "steamclient.dylib")) }
    return steam
}


/// `steam.exe` in the tool folder: with the fake wine.app's lsteamclient, the bridge counts as installed.
func installFakeSteamBridge(in layout: ToolLayout) throws {
    try write("steam.exe", to: layout.steamHelper)
}

/// A minimal PE: 64-byte DOS header with `e_lfanew` 0x80, padding, `PE\0\0`, the COFF header, then an optional header
/// (PE32 for i386, PE32+ otherwise) with 16 data directories. `certificate` fills directory 4, the Certificate Table, as
/// an Authenticode-signed file has it.
func peBytes(machine: UInt16, certificate: Bool = false) -> Data {
    let pe32 = machine == PEImage.i386
    let optional = 0x98, optionalSize = pe32 ? 0xE0 : 0xF0
    var bytes = [UInt8](repeating: 0, count: optional + optionalSize)
    func put(_ value: Int, at offset: Int, size: Int) {
        for i in 0..<size { bytes[offset + i] = UInt8((value >> (8 * i)) & 0xFF) }
    }
    bytes[0] = 0x4D; bytes[1] = 0x5A
    bytes[0x3C] = 0x80
    bytes[0x80] = 0x50; bytes[0x81] = 0x45
    put(Int(machine), at: 0x84, size: 2)
    put(optionalSize, at: 0x80 + 20, size: 2)
    put(pe32 ? 0x10B : 0x20B, at: optional, size: 2)
    put(16, at: optional + (pe32 ? 92 : 108), size: 4)
    if certificate {
        let directory = optional + (pe32 ? 128 : 144)
        put(0x400, at: directory, size: 4)
        put(0x10, at: directory + 4, size: 4)
    }
    return Data(bytes)
}

/// An amd64 image whose .rsrc (RVA 0x1000, file offset 0x400) holds `icons` as RT_ICON 1...n and one RT_GROUP_ICON
/// listing them in order, each resource in language 0x409.
func peBytes(icons: [(width: UInt8, bits: UInt16, data: [UInt8])]) -> Data {
    let rva = 0x1000, raw = 0x400, sub = 0x8000_0000, n = icons.count
    func le(_ value: Int, _ size: Int) -> [UInt8] { (0..<size).map { UInt8((value >> (8 * $0)) & 0xFF) } }
    func dir(_ entries: [(id: Int, offset: Int)]) -> [UInt8] {
        [UInt8](repeating: 0, count: 14) + le(entries.count, 2) + entries.flatMap { le($0.id, 4) + le($0.offset, 4) }
    }
    let iconNames = 32, iconLanguages = iconNames + 16 + 8 * n, groupNames = iconLanguages + 24 * n
    let groupLanguage = groupNames + 24, leaves = groupLanguage + 24, group = leaves + 16 * (n + 1)
    var rsrc = dir([(3, sub | iconNames), (14, sub | groupNames)])
    rsrc += dir(icons.indices.map { ($0 + 1, sub | (iconLanguages + 24 * $0)) })
    for i in icons.indices { rsrc += dir([(0x409, leaves + 16 * i)]) }
    rsrc += dir([(1, sub | groupLanguage)]) + dir([(0x409, leaves + 16 * n)])
    var image = group + 6 + 14 * n
    for icon in icons { rsrc += le(rva + image, 4) + le(icon.data.count, 4) + le(0, 8); image += icon.data.count }
    rsrc += le(rva + group, 4) + le(6 + 14 * n, 4) + le(0, 8)
    rsrc += le(0, 2) + le(1, 2) + le(n, 2)
    for (i, icon) in icons.enumerated() {
        rsrc += [icon.width, icon.width, 0, 0] + le(1, 2) + le(Int(icon.bits), 2) + le(icon.data.count, 4) + le(i + 1, 2)
    }
    rsrc += icons.flatMap(\.data)

    var bytes = [UInt8](peBytes(machine: PEImage.amd64))
    let optional = 0x98
    bytes[0x86] = 1  // NumberOfSections
    bytes.replaceSubrange((optional + 128)..<(optional + 136), with: le(rva, 4) + le(rsrc.count, 4))  // directory 2
    bytes += [UInt8](".rsrc".utf8) + [0, 0, 0] + le(rsrc.count, 4) + le(rva, 4) + le(rsrc.count, 4) + le(raw, 4)
    bytes += [UInt8](repeating: 0, count: raw - bytes.count) + rsrc
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
