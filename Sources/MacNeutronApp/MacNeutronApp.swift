import AppKit
import MacNeutronCore
import SwiftUI

@main
struct MacNeutronApp: App {
    @State private var model = AppModel(vmRoot: AppModel.bundledVMTools != nil ? MicroVM.standardRoot : nil)
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    init() {
        NSApplication.shared.setActivationPolicy(.accessory)  // menu-bar app, even when run unbundled
    }

    var body: some Scene {
        MenuBarExtra {
            MenuContent().environment(model)
        } label: {
            Image(systemName: model.status.symbol)
        }

        Window("Set up MacNeutron", id: "setup") {
            SetupView().environment(model)
        }
        .windowResizability(.contentSize)
        .defaultLaunchBehavior(model.setupComplete ? .suppressed : .presented)

        Window("Games", id: "games") {
            GamesView().environment(model)
        }

        Window("Free up space", id: "cleanup") {
            CleanupView().environment(model)
        }
        .windowResizability(.contentSize)

        Window("MacNeutron Settings", id: "settings") {
            SettingsView().environment(model)
        }
        .windowResizability(.contentSize)
    }
}

/// Opens a scene's window, or brings it back, in front of other apps. A menu-bar app is never active on its own.
@MainActor func show(_ id: String, with openWindow: OpenWindowAction) {
    openWindow(id: id)  // opens it, or orders it to the front of our own windows
    raiseWindow(id)
}

/// Brings the scene's open window to this Space and in front of other apps, then asks to activate.
@MainActor func raiseWindow(_ id: String) {
    let app = NSApplication.shared
    if let window = app.windows.first(where: { ($0.isVisible || $0.isMiniaturized) && isWindow($0.identifier?.rawValue, of: id) }) {
        window.collectionBehavior.insert(.moveToActiveSpace)  // before activating: the window comes to this Space
        if window.isMiniaturized { window.deminiaturize(nil) }
        window.makeKeyAndOrderFront(nil)
        window.orderFrontRegardless()
    }
    // ponytail: activate() is refused for a MenuBarExtra click (FB23508310, Apple forum 836619); this soft-deprecated
    // call, made inside the click's action, is the forum sample's working path. If a 27.x drops it, use a temporary
    // .regular activation policy while a window is open.
    app.activate(ignoringOtherApps: true)
}

/// Whether an NSWindow belongs to a `Window(id:)` scene.
// ponytail: SwiftUI's "<id>-AppWindow-<n>" identifier is undocumented; if it changes, only minimised and other-Space
// windows lose out, activation still runs.
func isWindow(_ identifier: String?, of scene: String) -> Bool {
    identifier?.hasPrefix(scene + "-AppWindow-") == true
}

/// Installs the app's global menu hot key, Shift-F9 (see MenuHotKey); an App has nowhere else to do it.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        MenuHotKey.shared.install()
    }
}
