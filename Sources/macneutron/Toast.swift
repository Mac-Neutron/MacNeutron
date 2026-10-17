import AppKit
import SwiftUI

/// `macneutron toast <title> <message> [file]`: the launcher's "please wait" while a game's prefix is prepared
/// (AppleScriptNotifier.progress starts it), in place of Wine's old dialog. A glass panel at the top of the screen,
/// over full-screen apps too, that takes no clicks and no focus. It goes when stdin closes (the launcher is done, or
/// gone) or the file appears (the microVM's guest wrote it).
@MainActor
enum Toast {
    private static var panel: NSPanel?

    static func run(title: String, message: String, until: String?) -> Never {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        let content = NSHostingView(rootView: ToastView(title: title, message: message))
        let size = content.fittingSize
        let panel = NSPanel(contentRect: NSRect(origin: .zero, size: size), styleMask: [.borderless, .nonactivatingPanel],
                            backing: .buffered, defer: false)
        panel.contentView = content
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false  // the view draws its own, softer
        panel.level = .statusBar
        panel.ignoresMouseEvents = true
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
        if let area = NSScreen.main?.visibleFrame {
            panel.setFrameTopLeftPoint(NSPoint(x: area.midX - size.width / 2, y: area.maxY))
        }
        panel.alphaValue = 0
        panel.orderFrontRegardless()
        Self.panel = panel
        NSAnimationContext.runAnimationGroup { $0.duration = 0.3; panel.animator().alphaValue = 1 }
        NSAccessibility.post(element: panel, notification: .announcementRequested,
                             userInfo: [.announcement: "\(title). \(message)"])
        Thread.detachNewThread {
            var byte: UInt8 = 0
            while read(0, &byte, 1) > 0 {}
            DispatchQueue.main.async { MainActor.assumeIsolated { finish() } }
        }
        if let until {
            Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { _ in
                if access(until, F_OK) == 0 { MainActor.assumeIsolated { finish() } }
            }
        }
        app.run()
        exit(0)
    }

    private static func finish() {
        guard let panel else { exit(0) }
        Self.panel = nil
        NSAnimationContext.runAnimationGroup({ $0.duration = 0.3; panel.animator().alphaValue = 0 }) { exit(0) }
    }
}

private struct ToastView: View {
    let title: String
    let message: String

    var body: some View {
        HStack(spacing: 14) {
            ProgressView()
                .controlSize(.small)
                .frame(width: 22, height: 22)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(.system(size: 13, weight: .semibold))
                Text(message)
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 14)
        .frame(width: 340, alignment: .leading)
        .glassEffect(.regular, in: .rect(cornerRadius: 20))
        .shadow(color: .black.opacity(0.18), radius: 14, y: 6)
        .padding(.horizontal, 24)
        .padding(.top, 10)
        .padding(.bottom, 30)  // room for the shadow
    }
}
