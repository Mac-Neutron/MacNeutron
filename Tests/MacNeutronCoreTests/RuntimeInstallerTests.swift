import Foundation
import Testing
@testable import MacNeutronCore

/// Stands in for bin/macneutron: prints each argument on its own line.
private func makeEchoLauncher() throws -> URL {
    let url = try makeTempDir().appending(path: "macneutron")
    try write("#!/bin/sh\nfor a in \"$@\"; do echo \"$a\"; done\n", to: url, executable: true)
    return url
}

@Test func installPutsSteamExeNextToTheLauncher() throws {
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    let launcher = try makeEchoLauncher()
    try write("steam.exe v1", to: launcher.deletingLastPathComponent().appending(path: "steam.exe"))
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    #expect(try String(contentsOf: layout.steamHelper, encoding: .utf8) == "steam.exe v1")
}

@Test func toolFilesRefreshReplacesAChangedLauncher() throws {
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    let launcher = try makeEchoLauncher()
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    try write("#!/bin/sh\necho v2\n", to: launcher, executable: true)
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    #expect(try String(contentsOf: layout.launcherBinary, encoding: .utf8) == "#!/bin/sh\necho v2\n")
    #expect(FileManager.default.isExecutableFile(atPath: layout.launcherBinary.path(percentEncoded: false)))
}

@Test func identicalToolFilesAreLeftAlone() throws {
    // The app refreshes tool files at every start; a game Steam launches meanwhile must never
    // find bin/macneutron missing or half-written.
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    let launcher = try makeEchoLauncher()
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    func inode() throws -> Int? {
        (try FileManager.default.attributesOfItem(atPath: layout.launcherBinary.path(percentEncoded: false))[.systemFileNumber]
            as? NSNumber)?.intValue
    }
    let before = try inode()
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    #expect(try inode() == before)
}

@Test func installFindsSteamExeInTheAppsResources() throws {
    // In MacNeutron.app the launcher is Contents/Helpers/macneutron; steam.exe isn't Mach-O code,
    // so codesign only accepts it in Contents/Resources.
    let contents = try makeTempDir().appending(path: "MacNeutron.app/Contents", directoryHint: .isDirectory)
    let launcher = contents.appending(path: "Helpers/macneutron")
    try write("#!/bin/sh\n", to: launcher, executable: true)
    try write("steam.exe from resources", to: contents.appending(path: "Resources/steam.exe"))
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    #expect(try String(contentsOf: layout.steamHelper, encoding: .utf8) == "steam.exe from resources")
}


// MARK: Installing wine.app

/// `realpath(3)`: the kernel reports running executables under `/private/var/…`, not the `/var/…` of `makeTempDir()`.
private func realPath(_ url: URL) throws -> String {
    let resolved = try #require(realpath(url.path(percentEncoded: false), nil))
    defer { free(resolved) }
    return String(cString: resolved)
}

/// A fake, unsigned `dir/wine.app` whose identity is the text of `Contents/id`, so it travels with the bundle.
@discardableResult
private func makeFakeWineApp(at dir: URL, id: String) throws -> URL {
    let app = dir.appending(path: "wine.app", directoryHint: .isDirectory)
    try write("#!/bin/sh\n", to: app.appending(path: "Contents/MacOS/wine"), executable: true)
    try write(id, to: app.appending(path: "Contents/id"))
    return app
}

private func fakeIdentity(_ bundle: URL) -> String? {
    try? String(contentsOf: bundle.appending(path: "Contents/id"), encoding: .utf8)
}

/// Runs the install against a fake source: `cp` copies with FileManager, `codesign` answers `codesignStatus`.
/// `scans` answers the install's running-executables scans in turn (the last one repeats); `running` is one answer.
private func installFake(_ source: URL, into layout: ToolLayout, launcher: URL? = nil, steamExe: URL? = nil,
                         force: Bool = false, codesignStatus: Int32 = 0, cpStatus: Int32 = 0, running: [String] = [],
                         scans: [[String]]? = nil, log: LauncherLog? = nil) throws -> (RuntimeInstallOutcome, FakeRunner) {
    let runner = FakeRunner { call in
        if call.tool == "cp" {
            guard cpStatus == 0 else { return cpStatus }
            try? FileManager.default.copyItem(atPath: call.arguments[2], toPath: call.arguments[3])
        }
        return call.tool == "codesign" ? codesignStatus : 0
    }
    var answers = scans ?? [running]
    let outcome = try RuntimeInstaller.install(
        wineApp: source, layout: layout, launcherBinary: try launcher ?? makeEchoLauncher(), steamExe: steamExe,
        force: force, runner: runner, runningExecutables: { answers.count > 1 ? answers.removeFirst() : answers[0] },
        identity: fakeIdentity, log: try log ?? LauncherLog(directory: makeTempDir()))
    return (outcome, runner)
}

/// A tool folder whose installed `wine.app` has identity `installed`.
private func makeInstalledLayout(id installed: String = "A") throws -> ToolLayout {
    let layout = try makeToolLayout()
    try write(installed, to: layout.wineApp.appending(path: "Contents/id"))
    return layout
}

private func exists(_ url: URL) -> Bool { FileManager.default.fileExists(atPath: url.path(percentEncoded: false)) }

@Test func equalIdentitySkipsTheCopyButWritesToolFiles() throws {
    let layout = try makeInstalledLayout(id: "A")
    let (outcome, runner) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "A"), into: layout)
    #expect(outcome == .unchanged)
    #expect(runner.calls.isEmpty)
    #expect(exists(layout.launcherBinary))
    #expect(exists(layout.root.appending(path: "toolmanifest.vdf")))
    #expect(exists(layout.wineserver))  // the installed copy, untouched
}

@Test func differentIdentityReplacesTheRuntime() throws {
    let layout = try makeInstalledLayout(id: "A")
    let source = try makeFakeWineApp(at: makeTempDir(), id: "B")
    let (outcome, runner) = try installFake(source, into: layout)
    #expect(outcome == .installed)
    #expect(fakeIdentity(layout.wineApp) == "B")
    #expect(runner.calls.map(\.tool) == ["cp", "codesign"])
    let new = layout.root.appending(path: "wine.app.new").path(percentEncoded: false)
    #expect(runner.calls[0].arguments == ["-c", "-R", source.path(percentEncoded: false), new])
    #expect(runner.calls[1].arguments == ["--verify", "--strict", new])
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
    #expect(!exists(layout.wineserver))  // the old copy is gone
}

@Test func forceReinstallsOverAnEqualIdentity() throws {
    let layout = try makeInstalledLayout(id: "A")
    let (outcome, runner) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "A"), into: layout, force: true)
    #expect(outcome == .installed)
    #expect(runner.calls.map(\.tool) == ["cp", "codesign"])
    #expect(!exists(layout.wineserver))
}

@Test func damagedMarkerReinstallsAndIsCleared() throws {
    let layout = try makeInstalledLayout(id: "A")
    try write("", to: layout.runtimeDamagedMarker)
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "A"), into: layout)
    #expect(outcome == .installed)
    #expect(!exists(layout.runtimeDamagedMarker))
    #expect(!exists(layout.wineserver))
}

@Test func aRunningRuntimeProcessDefers() throws {
    let layout = try makeInstalledLayout(id: "A")
    let log = LauncherLog(directory: try makeTempDir())
    let server = try realPath(layout.wineserver)
    let (outcome, runner) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout,
                                            running: ["/usr/bin/true", server], log: log)
    #expect(outcome == .deferred(server))
    #expect(runner.calls.isEmpty)
    #expect(fakeIdentity(layout.wineApp) == "A")
    #expect(try String(contentsOf: log.launcherLog, encoding: .utf8).contains("install deferred: \(server) is running"))
}

@Test func deferralMatchesTheKernelsRealPath() throws {
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron", directoryHint: .isDirectory))
    try makeFakeWineApp(at: layout.root, id: "A")
    let kernelPath = try realPath(layout.wine)
    // $TMPDIR is under /var, a symlink to /private/var, on macOS; the kernel reports the latter.
    #expect(kernelPath != layout.wine.path(percentEncoded: false))
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout,
                                       running: [kernelPath])
    #expect(outcome == .deferred(kernelPath))
    // A sibling whose name only starts like wine.app doesn't count.
    let sibling = try realPath(layout.root) + "/wine.app2/Contents/MacOS/wine"
    let (other, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout, running: [sibling])
    #expect(other == .installed)
}

@Test func aRunningRosettaProcessDefersTheInstall() throws {
    let layout = try makeInstalledLayout(id: "A")
    let rosettaWine = layout.root.appending(path: "Libraries/Wine/bin/wine")
    try write("#!/bin/sh\n", to: rosettaWine, executable: true)
    let running = try realPath(rosettaWine)
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout, running: [running])
    #expect(outcome == .deferred(running))
    #expect(exists(rosettaWine))
}

@Test func aDeferredInstallLeavesTheOldRuntimeWorking() throws {
    let layout = try makeInstalledLayout(id: "A")
    try write("#!/bin/sh\necho old\n", to: layout.launcherBinary, executable: true)
    let running = try realPath(layout.wine)
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout, running: [running])
    #expect(outcome == .deferred(running))
    #expect(fakeIdentity(layout.wineApp) == "A")
    #expect(FileManager.default.isExecutableFile(atPath: layout.wine.path(percentEncoded: false)))
    #expect(exists(layout.wineserver))
    // The CLI and the runtime change together: the old CLI stays with the old runtime.
    #expect(try String(contentsOf: layout.launcherBinary, encoding: .utf8) == "#!/bin/sh\necho old\n")
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
}

@Test func leftoversFromAnInterruptedInstallAreCleaned() throws {
    // A copy interrupted mid-way left wine.app.new (and an older scheme's wine.app.old); `cp -R` into an existing
    // folder would nest the source inside it.
    let layout = try makeInstalledLayout(id: "A")
    try write("half", to: layout.root.appending(path: "wine.app.new/Contents/MacOS/wine"))
    try write("old", to: layout.root.appending(path: "wine.app.old/Contents/MacOS/wine"))
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout)
    #expect(outcome == .installed)
    #expect(fakeIdentity(layout.wineApp) == "B")
    #expect(!exists(layout.wineApp.appending(path: "wine.app")))
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
    #expect(!exists(layout.root.appending(path: "wine.app.old")))
    #expect(FileManager.default.isExecutableFile(atPath: layout.wine.path(percentEncoded: false)))
}

@Test func aBadSignatureLeavesTheOldRuntime() throws {
    let layout = try makeInstalledLayout(id: "A")
    #expect(throws: RuntimeInstallError.signatureInvalid(1)) {
        _ = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout, codesignStatus: 1)
    }
    #expect(fakeIdentity(layout.wineApp) == "A")
    #expect(exists(layout.wineserver))
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
}

@Test func aGameStartedDuringTheCopyDefersTheSwap() throws {
    // The copy and its signature check take seconds; a game started meanwhile runs from the old wine.app, which the
    // swap would delete under it. The scan is repeated right before the swap.
    let layout = try makeInstalledLayout(id: "A")
    let log = LauncherLog(directory: try makeTempDir())
    let server = try realPath(layout.wineserver)
    let (outcome, runner) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout,
                                            scans: [[], ["/usr/bin/true", server]], log: log)
    #expect(outcome == .deferred(server))
    #expect(runner.calls.map(\.tool) == ["cp", "codesign"])
    #expect(fakeIdentity(layout.wineApp) == "A")
    #expect(exists(layout.wineserver))
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
    #expect(try String(contentsOf: log.launcherLog, encoding: .utf8).contains("install deferred: \(server) is running"))
}

@Test func aFailedCopyNamesItsCause() throws {
    let layout = try makeInstalledLayout(id: "A")
    #expect(throws: RuntimeInstallError.copyFailed(1)) {
        _ = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "B"), into: layout, cpStatus: 1)
    }
    #expect(RuntimeInstallError.copyFailed(1).description
        == "Couldn't copy the runtime into the tool folder (cp exit 1); check free disk space.")
    #expect(fakeIdentity(layout.wineApp) == "A")
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
}

@Test func installedToolFilesAreNotQuarantined() throws {
    // MacNeutron.app downloaded in a zip is quarantined, and so are the CLI and steam.exe inside it; Steam execs the
    // CLI the install writes, which has no stapled ticket of its own.
    let launcher = try makeEchoLauncher()
    let steamExe = launcher.deletingLastPathComponent().appending(path: "steam.exe")
    try write("steam.exe", to: steamExe)
    let quarantine = "0083;66f00000;Safari;"
    for file in [launcher, steamExe] {
        #expect(setxattr(file.path(percentEncoded: false), "com.apple.quarantine", quarantine, quarantine.utf8.count, 0, 0) == 0)
    }
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: launcher)
    for file in [layout.launcherBinary, layout.steamHelper] {
        let rc = getxattr(file.path(percentEncoded: false), "com.apple.quarantine", nil, 0, 0, 0)
        let err = errno  // read before anything else can change it
        #expect(rc == -1 && err == ENOATTR, "\(file.lastPathComponent)")
    }
    #expect(getxattr(launcher.path(percentEncoded: false), "com.apple.quarantine", nil, 0, 0, 0) > 0)  // the source keeps it
}

@Test func aSourceWithoutTheLoaderIsNotAWineApp() throws {
    let layout = try makeInstalledLayout(id: "A")
    let source = try makeTempDir().appending(path: "wine.app", directoryHint: .isDirectory)
    try write("B", to: source.appending(path: "Contents/id"))
    #expect(throws: RuntimeInstallError.notAWineApp(source.path(percentEncoded: false))) {
        _ = try installFake(source, into: layout)
    }
    #expect(fakeIdentity(layout.wineApp) == "A")
}

@Test func rosettaEraEntriesAreRemoved() throws {
    let layout = try makeInstalledLayout(id: "A")
    let stale = ToolLayout.rosettaEraEntries + ["proton"]
    for name in ToolLayout.rosettaEraEntries { try write("old", to: layout.root.appending(path: name).appending(path: "file")) }
    try write("#!/bin/sh\n", to: layout.root.appending(path: "proton"), executable: true)  // the stub is a file
    let (outcome, _) = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "A"), into: layout)
    #expect(outcome == .unchanged)
    for name in stale { #expect(!exists(layout.root.appending(path: name)), "\(name)") }
    #expect(exists(layout.wine))
}

@Test func installWorksUnderASpacedNonASCIIPath() throws {
    let layout = ToolLayout(root: try makeTempDir().appending(path: "Application Support/é/macneutron",
                                                                directoryHint: .isDirectory))
    try makeFakeWineApp(at: layout.root, id: "A")
    let source = try makeFakeWineApp(at: makeTempDir().appending(path: "Gamé Folder"), id: "B")
    let running = try realPath(layout.wine)
    #expect(try installFake(source, into: layout, running: [running]).0 == .deferred(running))
    let (outcome, _) = try installFake(source, into: layout)
    #expect(outcome == .installed)
    #expect(fakeIdentity(layout.wineApp) == "B")
    #expect(exists(layout.launcherBinary))
}

@Test func steamExeOptionIsUsedWhenGiven() throws {
    // A dev build's .build/release/ has no steam.exe beside the CLI.
    let layout = try makeInstalledLayout(id: "A")
    let steamExe = try makeTempDir().appending(path: "arm64 bridge/steam.exe")
    try write("steam.exe arm64", to: steamExe)
    _ = try installFake(try makeFakeWineApp(at: makeTempDir(), id: "A"), into: layout, steamExe: steamExe)
    #expect(try String(contentsOf: layout.steamHelper, encoding: .utf8) == "steam.exe arm64")
}

@Test func manifestPointsAtTheCLI() throws {
    // R0b: Steam launches a thin arm64 tool binary, so the manifest runs the CLI itself; no /bin/sh stub.
    let layout = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    try RuntimeInstaller.writeToolFiles(layout: layout, launcherBinary: try makeEchoLauncher())
    let manifest = try String(contentsOf: layout.root.appending(path: "toolmanifest.vdf"), encoding: .utf8)
    #expect(manifest.contains("\"commandline\" \"/bin/macneutron launch %verb%\""))
    #expect(!exists(layout.root.appending(path: "proton")))
}

@Test func firstInstallClonesVerifiesAndSwapsIn() throws {
    let source = try makeSignedWineApp(at: makeTempDir().appending(path: "Helpers dir"))
    let layout = ToolLayout(root: try makeTempDir().appending(path: "Application Support/macneutron",
                                                                directoryHint: .isDirectory))
    let log = LauncherLog(directory: try makeTempDir())
    let launcher = try makeEchoLauncher()
    func install() throws -> RuntimeInstallOutcome {
        try RuntimeInstaller.install(wineApp: source, layout: layout, launcherBinary: launcher,
                                     runningExecutables: { [] }, log: log)
    }
    #expect(try install() == .installed)
    let identity = try #require(CodeIdentity.of(source))
    #expect(CodeIdentity.of(layout.wineApp) == identity)
    #expect(try SystemProcessRunner().run(URL(filePath: "/usr/bin/codesign"),
                                          ["--verify", "--strict", layout.wineApp.path(percentEncoded: false)],
                                          environment: [:], output: URL(filePath: "/dev/null")) == 0)
    #expect(!exists(layout.root.appending(path: "wine.app.new")))
    #expect(try install() == .unchanged)
}

@Test func toolFilesKeepTheLegacyNameOnlyWhileTheVdfHasIt() throws {
    // Steam maps cloud roots only for tool names containing "proton"; the old name stays until its mappings move.
    let fresh = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    try RuntimeInstaller.writeToolFiles(layout: fresh, launcherBinary: try makeEchoLauncher())
    let freshVdf = try KeyValues.parse(try String(contentsOf: fresh.root.appending(path: "compatibilitytool.vdf"), encoding: .utf8))
    #expect(freshVdf.node(at: ["compatibilitytools", "compat_tools"])?.children.map(\.key) == ["macneutron-proton"])
    #expect(freshVdf.node(at: ["compatibilitytools", "compat_tools", "macneutron-proton", "display_name"])?.stringValue == "MacNeutron")
    #expect(!RuntimeInstaller.registersLegacyName(in: fresh.root))

    let updated = ToolLayout(root: try makeTempDir().appending(path: "macneutron"))
    try write("""
        "compatibilitytools"
        {
          "compat_tools"
          {
            "macneutron"
            {
              "install_path" "."
              "display_name" "MacNeutron"
              "from_oslist"  "windows"
              "to_oslist"    "linux"
            }
          }
        }

        """, to: updated.root.appending(path: "compatibilitytool.vdf"))  // the file every install before the rename wrote
    try RuntimeInstaller.writeToolFiles(layout: updated, launcherBinary: try makeEchoLauncher())
    let both = try KeyValues.parse(try String(contentsOf: updated.root.appending(path: "compatibilitytool.vdf"), encoding: .utf8))
    #expect(both.node(at: ["compatibilitytools", "compat_tools"])?.children.map(\.key) == ["macneutron-proton", "macneutron"])
    #expect(both.node(at: ["compatibilitytools", "compat_tools", "macneutron", "display_name"])?.stringValue == "MacNeutron (old)")

    try RuntimeInstaller.writeCompatibilityTool(in: updated.root, legacyName: false)
    try RuntimeInstaller.writeToolFiles(layout: updated, launcherBinary: try makeEchoLauncher())
    #expect(!RuntimeInstaller.registersLegacyName(in: updated.root))
}
