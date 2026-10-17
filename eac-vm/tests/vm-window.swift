// What covers the VM's window (Direct3D forwarding M5), for the bench's record: App Nap slowed a covered VM (SMITE 2's
// lobby at 22 fps behind a browser, 59 uncovered) until macneutron-vm held an activity against it.
// Prints "vm-window: uncovered", "vm-window: covered by <apps>" (normal windows in front that overlap it) or
// "vm-window: none".
//   swift vm-window.swift
import CoreGraphics
import Foundation

func rect(_ w: [String: Any]) -> CGRect {
  CGRect(dictionaryRepresentation: (w[kCGWindowBounds as String] as? NSDictionary ?? [:]) as CFDictionary) ?? .null
}
let shown = (CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]]) ?? []
guard let i = shown.firstIndex(where: { ($0[kCGWindowOwnerName as String] as? String) == "macneutron-vm"
                                        && ($0[kCGWindowLayer as String] as? Int) == 0 }) else {
  print("vm-window: none")
  exit(0)
}
let vm = rect(shown[i])
let over = shown[..<i].filter { ($0[kCGWindowLayer as String] as? Int) == 0 && rect($0).intersects(vm) }
  .compactMap { $0[kCGWindowOwnerName as String] as? String }
print(over.isEmpty ? "vm-window: uncovered" : "vm-window: covered by \(Set(over).sorted().joined(separator: ", "))")
