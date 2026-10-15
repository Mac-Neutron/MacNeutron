import AppKit
import Carbon.HIToolbox
import MacNeutronCore

/// The key combo that opens MacNeutron's menu: a global Shift-F9, so a fullscreen game can bring the menu up without
/// reaching for the menu bar. SwiftUI's `MenuBarExtra` has no API to open its menu, so the handler clicks the menu bar
/// item's button. A Carbon hot key is used rather than an `NSEvent` global monitor: it needs no Accessibility grant.
@MainActor
final class MenuHotKey {
    static let shared = MenuHotKey()

    private var hotKey: EventHotKeyRef?
    private var handler: EventHandlerRef?

    func install() {
        guard hotKey == nil else { return }

        var spec = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        let handlerErr = InstallEventHandler(GetApplicationEventTarget(), { _, event, _ in
            var id = EventHotKeyID()
            if let event,
               GetEventParameter(event, EventParamName(kEventParamDirectObject), EventParamType(typeEventHotKeyID),
                                 nil, MemoryLayout<EventHotKeyID>.size, nil, &id) == noErr,
               id.signature == menuHotKeySignature {
                MainActor.assumeIsolated { MenuHotKey.shared.open() }
            }
            return noErr
        }, 1, &spec, nil, &handler)

        let id = EventHotKeyID(signature: menuHotKeySignature, id: 1)
        let hotKeyErr = RegisterEventHotKey(UInt32(kVK_F9), UInt32(shiftKey), id, GetApplicationEventTarget(), 0,
                                            &hotKey)
        if handlerErr != noErr || hotKeyErr != noErr {
            LauncherLog.standard.append("menu hotkey Shift-F9 could not be installed: handler \(handlerErr), "
                                        + "hot key \(hotKeyErr)\n")
        }
    }

    /// Clicks the menu bar item, which opens its menu. Logs when it is not there, so a macOS that renames or rehouses
    /// the item is diagnosable rather than silent.
    func open() {
        for window in NSApp.windows {
            if let button = window.contentView?.firstSubview(of: NSStatusBarButton.self) {
                button.performClick(nil)
                return
            }
        }
        LauncherLog.standard.append("menu hotkey Shift-F9: no menu bar item to open\n")
    }
}

/// 'MNRG', to tell our event from another handler on the application target.
private let menuHotKeySignature: OSType = 0x4D4E5247

extension NSView {
    /// The first view of `type` in this view's subtree, this view included.
    func firstSubview<T: NSView>(of type: T.Type) -> T? {
        if let match = self as? T { return match }
        for sub in subviews {
            if let match = sub.firstSubview(of: type) { return match }
        }
        return nil
    }
}
