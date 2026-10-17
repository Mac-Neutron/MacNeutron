import Foundation

/// What stops a VM setup (`macneutron vm-setup`), each case naming what it was about.
enum VMSetupError: Error, Equatable, CustomStringConvertible {
    /// A payload compressor or format this setup doesn't read.
    case unsupported(String)
    /// A compressed stream that ends before its end.
    case truncated
    /// A compressed stream that isn't one.
    case corrupt
    /// A recipe line (1-based) that isn't one.
    case badRecipe(line: Int)
    /// A recipe file whose bytes no package and no overlay holds.
    case missing(path: String)
    /// A file whose bytes aren't the ones the manifest or recipe names.
    case mismatch(file: String)
    /// A tool (mkfs.erofs, tar) that exited with a status other than 0.
    case tool(String, Int32)
    /// A download no source served right, three times over.
    case download(name: String)

    var description: String {
        switch self {
        case .unsupported(let what): "vm-setup: \(what) isn't supported"
        case .truncated: "vm-setup: a package's payload is cut short"
        case .corrupt: "vm-setup: a package's payload doesn't decompress"
        case .badRecipe(let line): "vm-setup: the parts' recipe is damaged at line \(line)"
        case .missing(let path): "vm-setup: no package holds \(path)"
        case .mismatch(let file): "vm-setup: \(file) isn't what the manifest names"
        case .tool(let name, let status): "vm-setup: \(name) exited with \(status)"
        case .download(let name): "vm-setup: couldn't download \(name)"
        }
    }
}
