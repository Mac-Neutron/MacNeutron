import Foundation

/// MacNeutron.app's vm-manifest.json (eac-vm/setup/manifest.py, spec §4.3): the parts archive and every pinned Fedora
/// package, each with its size and SHA-256 (the notarized app carries them, so a Mac needs no OpenPGP: Fedora's
/// signatures were checked when the manifest was made). Its file's hash names the image set it builds.
struct VMSetupManifest: Codable, Equatable {
    struct Download: Codable, Equatable { let url: URL; let size: Int64; let sha256: String }
    struct Package: Codable, Equatable {
        let file, name, version, release, arch: String
        let size: Int64
        let sha256: String
    }
    struct FEXRootfs: Codable, Equatable { let package: Package; let image: String; let imageSha256: String }
    struct Koji: Codable, Equatable { let base: URL; let signedKey: String }

    let version: Int
    let parts: Download
    let kernel: String
    let packages: [Package]
    let fexRootfs: FEXRootfs
    let koji: Koji
    /// Tests' local copies, which replace Fedora's sources: `<mirror>/<rpm file>`.
    var mirrors: [URL]?

    /// Where a package is fetched from, in turn: Fedora's release tree, its updates (both on dl.fedoraproject.org,
    /// noarch packages in the aarch64 tree), then Koji's signed copy, which keeps every build after the mirrors have
    /// moved on.
    func sources(_ p: Package) -> [URL] {
        if let mirrors { return mirrors.map { $0.appending(path: p.file) } }
        let fedora = p.release.firstMatch(of: /fc(\d+)/).map { String($0.1) } ?? "44"
        let arch = p.arch == "noarch" ? "aarch64" : p.arch, letter = String(p.name.prefix(1)).lowercased()
        let mirror = URL(string: "https://dl.fedoraproject.org/pub/fedora/linux/")!
        return [
            mirror.appending(path: "releases/\(fedora)/Everything/\(arch)/os/Packages/\(letter)").appending(path: p.file),
            mirror.appending(path: "updates/\(fedora)/Everything/\(arch)/Packages/\(letter)").appending(path: p.file),
            koji.base.appending(path: p.name).appending(path: p.version).appending(path: p.release)
                .appending(path: "data/signed/\(koji.signedKey)/\(p.arch)").appending(path: p.file),
        ]
    }
}
