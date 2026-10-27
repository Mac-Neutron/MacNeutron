import AppKit
import MacNeutronCore

/// Install MacNeutron (spec 2026-10-25-macneutron-installer-design §3.2): asks once (not when the app's update opened
/// it with --from-app), then AppInstaller verifies its bundled MacNeutron.app, quits MacNeutron and puts the app in
/// /Applications, which it opens. Every failure is an alert, and /Applications/MacNeutron.app stays as it was.

/// Every running MacNeutron asked to quit, waited for up to 30 s; true when none is left (none running is true: the
/// app's update quits it right after opening the installer).
@MainActor
func quitMacNeutron() -> Bool {
    let apps = Updater.identifiers.flatMap { NSRunningApplication.runningApplications(withBundleIdentifier: $0) }
    apps.forEach { $0.terminate() }
    let deadline = Date().addingTimeInterval(30)
    while apps.contains(where: { !$0.isTerminated }), Date() < deadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.2))
    }
    return apps.allSatisfy(\.isTerminated)
}

@MainActor
func alert(_ message: String, informative: String = "", buttons: [String] = ["OK"]) -> NSApplication.ModalResponse {
    let alert = NSAlert()
    alert.messageText = message
    alert.informativeText = informative
    buttons.forEach { alert.addButton(withTitle: $0) }
    return alert.runModal()
}

@MainActor
func run() -> Int32 {
    defer { Updater.removeInstallerFolders() }  // the app's update unpacked this installer into one
    NSApplication.shared.setActivationPolicy(.regular)
    NSApplication.shared.activate()
    let me = Bundle.main.bundleURL
    let bundled = me.appending(path: "Contents/Helpers/MacNeutron.app", directoryHint: .isDirectory)
    let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    guard let team = CodeIdentity.team(of: me) else {
        _ = alert("This installer isn't signed by MacNeutron's developer.")
        return 1
    }
    if !CommandLine.arguments.contains("--from-app"),
       alert("Install MacNeutron \(version) in Applications?", informative: "MacNeutron will quit.",
             buttons: ["Install", "Cancel"]) != .alertFirstButtonReturn {
        return 0
    }
    let log = FileManager.default.temporaryDirectory.appending(path: "install-macneutron-spctl.txt")
    do {
        try AppInstaller.install(bundled, into: AppInstaller.target,
                                 verify: { try Updater.verify($0, team: team, version: version, log: log) },
                                 quit: quitMacNeutron)
    } catch {
        _ = alert((error as? AppInstallerError)?.description ?? (error as? UpdateError)?.description
                  ?? error.localizedDescription)
        return 1
    }
    let open = Process()
    open.executableURL = URL(filePath: "/usr/bin/open")
    open.arguments = [AppInstaller.target.path(percentEncoded: false)]
    try? open.run()
    open.waitUntilExit()
    return 0
}

exit(MainActor.assumeIsolated { run() })
