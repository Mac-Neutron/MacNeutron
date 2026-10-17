import Foundation

/// What stops a VM setup (`macneutron vm-setup`), each case naming what it was about.
enum VMSetupError: Error, Equatable, CustomStringConvertible {
    /// A payload compressor or format this setup doesn't read.
    case unsupported(String)
    /// A compressed stream that ends before its end.
    case truncated
    /// A compressed stream that isn't one.
    case corrupt

    var description: String {
        switch self {
        case .unsupported(let what): "vm-setup: \(what) isn't supported"
        case .truncated: "vm-setup: a package's payload is cut short"
        case .corrupt: "vm-setup: a package's payload doesn't decompress"
        }
    }
}
