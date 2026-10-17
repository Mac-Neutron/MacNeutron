import Foundation

/// One entry of the tree an image is built from (eac-vm/image/recipe.py wrote it from the builder's own tree).
struct RecipeEntry: Equatable {
    enum Kind: Equatable {
        case directory, file(size: Int64, sha256: String, source: Source), symlink(String)
        /// A character or block device node (an initramfs's /dev/console: the kernel opens it before /init).
        case device(block: Bool, major: UInt32, minor: UInt32)
    }
    /// Where a file's bytes come from: a Fedora package's payload, or the parts' overlay.
    enum Source: Equatable { case package(String), overlay }
    let path: String
    /// Permission bits only (setuid, setgid and sticky included); the kind gives the file type.
    let mode: UInt16
    let uid: UInt32
    let gid: UInt32
    let kind: Kind
    /// The `security.capability` xattr's value.
    let capability: Data?
}

/// A recipe file: one entry a line, tab-separated, `type path mode uid gid size sha256 source link caps` (`-` for a
/// field the type has no use for; a tab, newline or backslash in a path or link written `\t`, `\n`, `\\`). Types: `d`,
/// `f`, `l`, and `c` or `b` for a device, whose link field is `major:minor`.
enum Recipe {
    static func parse(_ tsv: String) throws -> [RecipeEntry] {
        var entries: [RecipeEntry] = []
        for (number, line) in tsv.split(separator: "\n", omittingEmptySubsequences: false).enumerated() where !line.isEmpty {
            guard let entry = entry(line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init))
            else { throw VMSetupError.badRecipe(line: number + 1) }
            entries.append(entry)
        }
        return entries
    }

    private static func entry(_ f: [String]) -> RecipeEntry? {
        guard f.count == 10, let mode = UInt16(f[2], radix: 8), let uid = UInt32(f[3]), let gid = UInt32(f[4]) else { return nil }
        let kind: RecipeEntry.Kind
        switch f[0] {
        case "d": kind = .directory
        case "l": kind = .symlink(unescaped(f[8]))
        case "c", "b":
            let numbers = f[8].split(separator: ":").compactMap { UInt32($0) }
            guard numbers.count == 2 else { return nil }
            kind = .device(block: f[0] == "b", major: numbers[0], minor: numbers[1])
        case "f":
            guard let size = Int64(f[5]), f[6].count == 64 else { return nil }
            if f[7] == "overlay" {
                kind = .file(size: size, sha256: f[6], source: .overlay)
            } else if f[7].hasPrefix("pkg:") {
                kind = .file(size: size, sha256: f[6], source: .package(String(f[7].dropFirst(4))))
            } else { return nil }
        default: return nil
        }
        var capability: Data?
        if f[9] != "-" {
            let digits = Array(f[9].utf8)
            guard digits.count % 2 == 0 else { return nil }
            let bytes = stride(from: 0, to: digits.count, by: 2).compactMap {
                UInt8(String(decoding: digits[$0..<$0 + 2], as: UTF8.self), radix: 16)
            }
            guard bytes.count == digits.count / 2 else { return nil }
            capability = Data(bytes)
        }
        return RecipeEntry(path: unescaped(f[1]), mode: mode, uid: uid, gid: gid, kind: kind, capability: capability)
    }

    private static func unescaped(_ text: String) -> String {
        guard text.contains("\\") else { return text }
        var out = "", escape = false
        for c in text {
            if escape {
                out.append(c == "t" ? "\t" : c == "n" ? "\n" : c)
                escape = false
            } else if c == "\\" {
                escape = true
            } else {
                out.append(c)
            }
        }
        return out
    }
}
