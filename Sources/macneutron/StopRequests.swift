import AppKit
import MacNeutronCore

/// Steam's Stop on macOS asks the processes it tracks to quit, as apps (NSRunningApplication's terminate, which reaches
/// only one whose event loop runs): the launcher waits as one, an accessory (no Dock icon), its launch on another
/// thread. A quit request ends a microVM launch's VM (Launcher.stopVM); the launcher exits when the launch returns.
@MainActor
enum StopRequests {
    private final class Delegate: NSObject, NSApplicationDelegate {
        func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
            CommandLineTool.quitRequested()
            return .terminateCancel
        }
    }
    private static let delegate = Delegate()

    static func run(_ launch: @escaping @Sendable () async -> Int32) -> Never {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        app.delegate = delegate
        Task.detached { exit(await launch()) }
        app.run()
        exit(0)
    }
}
