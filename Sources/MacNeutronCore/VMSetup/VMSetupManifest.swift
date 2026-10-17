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
        /// The source package it was built from (`kernel-7.2.10-200.fc44.src.rpm` for kernel-core): Koji files every
        /// binary package under its source package's name, version and release.
        var sourceRPM: String? = nil
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

    /// The name, version and release Koji files a package under: its source package's, from name-version-release.src.rpm
    /// split from the right (names have dashes, versions and releases don't); its own without one.
    static func source(_ p: Package) -> (name: String, version: String, release: String) {
        guard let rpm = p.sourceRPM, rpm.hasSuffix(".src.rpm") else { return (p.name, p.version, p.release) }
        let parts = rpm.dropLast(".src.rpm".count).split(separator: "-").map(String.init)
        guard parts.count >= 3 else { return (p.name, p.version, p.release) }
        return (parts.dropLast(2).joined(separator: "-"), parts[parts.count - 2], parts[parts.count - 1])
    }

    /// Where a package is fetched from, in turn: Fedora's release tree, its updates (both on dl.fedoraproject.org,
    /// noarch packages in the aarch64 tree), then Koji's signed copy, which keeps every build after the mirrors have
    /// moved on.
    func sources(_ p: Package) -> [URL] {
        if let mirrors { return mirrors.map { $0.appending(path: p.file) } }
        let fedora = p.release.firstMatch(of: /fc(\d+)/).map { String($0.1) } ?? "44"
        let arch = p.arch == "noarch" ? "aarch64" : p.arch, letter = String(p.name.prefix(1)).lowercased()
        let mirror = URL(string: "https://dl.fedoraproject.org/pub/fedora/linux/")!
        let source = Self.source(p)
        return [
            mirror.appending(path: "releases/\(fedora)/Everything/\(arch)/os/Packages/\(letter)").appending(path: p.file),
            mirror.appending(path: "updates/\(fedora)/Everything/\(arch)/Packages/\(letter)").appending(path: p.file),
            koji.base.appending(path: source.name).appending(path: source.version).appending(path: source.release)
                .appending(path: "data/signed/\(koji.signedKey)/\(p.arch)").appending(path: p.file),
        ]
    }
}
