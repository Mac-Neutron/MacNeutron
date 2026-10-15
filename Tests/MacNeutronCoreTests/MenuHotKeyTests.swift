import AppKit
import Testing
@testable import MacNeutronApp

/// `MenuHotKey.open` reaches the menu bar item with this search: it walks a window's subtree for the first view of the
/// wanted type (SwiftUI's MenuBarExtra puts the item's button somewhere inside its status bar window).
@Test @MainActor func findsAViewNestedInTheSubtree() {
    let root = NSView()
    let middle = NSView()
    let button = NSButton()
    middle.addSubview(button)
    root.addSubview(middle)

    #expect(root.firstSubview(of: NSButton.self) === button)
    #expect(root.firstSubview(of: NSTextField.self) == nil)
    #expect(button.firstSubview(of: NSButton.self) === button)  // the view itself counts
}
