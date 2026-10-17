import Foundation
import Testing
@testable import MacNeutronCore

private let sha = String(repeating: "ab", count: 32)

@Test func parsesEachKindAndTheEscapes() throws {
    let tsv = """
        d\t/\t755\t0\t0\t-\t-\t-\t-\t-
        f\t/usr/bin/ping\t755\t0\t0\t10\t\(sha)\tpkg:iputils-1.fc44.aarch64.rpm\t-\t0100000200200000
        f\t/etc/odd\\tname\t644\t0\t5\t3\t\(sha)\toverlay\t-\t-
        l\t/usr/lib/libz.so.1\t777\t0\t0\t-\t-\t-\tlibz.so.1.3\\\\x\t-

        """
    let entries = try Recipe.parse(tsv)
    #expect(entries == [
        RecipeEntry(path: "/", mode: 0o755, uid: 0, gid: 0, kind: .directory, capability: nil),
        RecipeEntry(path: "/usr/bin/ping", mode: 0o755, uid: 0, gid: 0,
                    kind: .file(size: 10, sha256: sha, source: .package("iputils-1.fc44.aarch64.rpm")),
                    capability: Data([1, 0, 0, 2, 0, 0x20, 0, 0])),
        RecipeEntry(path: "/etc/odd\tname", mode: 0o644, uid: 0, gid: 5, kind: .file(size: 3, sha256: sha, source: .overlay),
                    capability: nil),
        RecipeEntry(path: "/usr/lib/libz.so.1", mode: 0o777, uid: 0, gid: 0, kind: .symlink("libz.so.1.3\\x"), capability: nil),
    ])
}

@Test func aShortLineIsABadRecipe() throws {
    #expect(throws: VMSetupError.badRecipe(line: 2)) {
        try Recipe.parse("d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nf\t/x\t644\n")
    }
}
