//! MacNeutron.app's vm-manifest.json (vm/setup/manifest.py, spec §4.3): the parts archive and every pinned Fedora
//! package, each with its size and SHA-256 (the notarized app carries them, so a Mac needs no OpenPGP: Fedora's
//! signatures were checked when the manifest was made). Its file's hash names the image set it builds.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Download {
    pub url: String,
    pub size: i64,
    pub sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Package {
    pub file: String,
    pub name: String,
    pub version: String,
    pub release: String,
    pub arch: String,
    pub size: i64,
    pub sha256: String,
    /// The source package it was built from (`kernel-7.2.10-200.fc44.src.rpm` for kernel-core): Koji files every
    /// binary package under its source package's name, version and release.
    #[serde(rename = "sourceRPM", default, skip_serializing_if = "Option::is_none")]
    pub source_rpm: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FEXRootfs {
    pub package: Package,
    pub image: String,
    #[serde(rename = "imageSha256")]
    pub image_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Koji {
    pub base: String,
    #[serde(rename = "signedKey")]
    pub signed_key: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct VMSetupManifest {
    pub version: i64,
    pub parts: Download,
    pub kernel: String,
    pub packages: Vec<Package>,
    #[serde(rename = "fexRootfs")]
    pub fex_rootfs: FEXRootfs,
    pub koji: Koji,
    /// Tests' local copies, which replace Fedora's sources: `<mirror>/<rpm file>`.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub mirrors: Option<Vec<String>>,
}

impl VMSetupManifest {
    pub fn parse(text: &str) -> Result<VMSetupManifest, serde_json::Error> {
        serde_json::from_str(text)
    }

    /// The name, version and release Koji files a package under: its source package's, from name-version-release.src.rpm
    /// split from the right (names have dashes, versions and releases don't); its own without one.
    pub fn source(package: &Package) -> (String, String, String) {
        let Some(rpm) = &package.source_rpm else {
            return (package.name.clone(), package.version.clone(), package.release.clone());
        };
        let Some(stem) = rpm.strip_suffix(".src.rpm") else {
            return (package.name.clone(), package.version.clone(), package.release.clone());
        };
        let parts: Vec<&str> = stem.split('-').collect();
        if parts.len() < 3 {
            return (package.name.clone(), package.version.clone(), package.release.clone());
        }
        let name = parts[..parts.len() - 2].join("-");
        (name, parts[parts.len() - 2].to_string(), parts[parts.len() - 1].to_string())
    }

    /// Where a package is fetched from, in turn: Fedora's release tree, its updates (both on dl.fedoraproject.org,
    /// noarch packages in the aarch64 tree), then Koji's signed copy, which keeps every build after the mirrors have
    /// moved on.
    pub fn sources(&self, package: &Package) -> Vec<String> {
        if let Some(mirrors) = &self.mirrors {
            return mirrors.iter().map(|mirror| append(mirror, &package.file)).collect();
        }
        let fedora = fedora_release(&package.release);
        let arch = if package.arch == "noarch" { "aarch64" } else { &package.arch };
        let letter = package.name.chars().next().unwrap_or('a').to_lowercase();
        let letter = letter.to_string();
        let mirror = "https://dl.fedoraproject.org/pub/fedora/linux/";
        let (name, version, release) = Self::source(package);
        vec![
            append(mirror, &format!("releases/{fedora}/Everything/{arch}/os/Packages/{letter}/{}", package.file)),
            append(mirror, &format!("updates/{fedora}/Everything/{arch}/Packages/{letter}/{}", package.file)),
            append(
                &self.koji.base,
                &format!(
                    "{name}/{version}/{release}/data/signed/{}/{}/{}",
                    self.koji.signed_key, package.arch, package.file
                ),
            ),
        ]
    }
}

/// `/fc(\d+)/` on the release, defaulting to "44".
fn fedora_release(release: &str) -> String {
    let bytes = release.as_bytes();
    let mut i = 0;
    while i + 2 < bytes.len() {
        if bytes[i] == b'f' && bytes[i + 1] == b'c' {
            let mut j = i + 2;
            while j < bytes.len() && bytes[j].is_ascii_digit() {
                j += 1;
            }
            if j > i + 2 {
                return release[i + 2..j].to_string();
            }
        }
        i += 1;
    }
    "44".to_string()
}

/// `appending(path:)`: joins a URL and a path component, percent-encoding what a URL path may not carry (`^` among it).
fn append(base: &str, component: &str) -> String {
    let base = if base.ends_with('/') { base.to_string() } else { format!("{base}/") };
    let encoded: String = component
        .bytes()
        .map(|b| {
            if b.is_ascii_alphanumeric() || matches!(b, b'-' | b'.' | b'_' | b'~' | b'/') {
                (b as char).to_string()
            } else {
                format!("%{b:02X}")
            }
        })
        .collect();
    format!("{base}{encoded}")
}

#[cfg(test)]
mod tests {
    use super::*;

    const KERNEL_SHA: &str = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    fn sha64(byte: char) -> String {
        std::iter::repeat_n(byte, 64).collect()
    }

    fn manifest_json() -> String {
        format!(
            "{{\"version\": 1, \"parts\": {{\"url\": \"https://example.invalid/parts.tar.xz\", \"size\": 3, \"sha256\": \"{}\"}},\
     \"kernel\": \"7.2.10-200.fc44.aarch64\",\n     \"packages\": [{{\"file\": \"kernel-core-7.2.10-200.fc44.aarch64.rpm\", \"name\": \"kernel-core\", \"version\": \"7.2.10\",\n                   \"release\": \"200.fc44\", \"arch\": \"aarch64\", \"size\": 5, \"sha256\": \"{}\"}}],\n     \"fexRootfs\": {{\"package\": {{\"file\": \"fex-emu-rootfs-fedora-44^20260410.n.0-1.fc44.noarch.rpm\",\n                               \"name\": \"fex-emu-rootfs-fedora\", \"version\": \"44^20260410.n.0\", \"release\": \"1.fc44\",\n                               \"arch\": \"noarch\", \"size\": 1, \"sha256\": \"{}\"}},\n                   \"image\": \"/usr/share/fex-emu/RootFS/default.erofs\", \"imageSha256\": \"{}\"}},\n     \"koji\": {{\"base\": \"https://kojipkgs.fedoraproject.org/packages/\", \"signedKey\": \"6d9f90a6\"}}}}",
            sha64('0'),
            KERNEL_SHA,
            sha64('1'),
            sha64('2'),
        )
    }

    fn manifest() -> VMSetupManifest {
        VMSetupManifest::parse(&manifest_json()).unwrap()
    }

    fn kernel() -> Package {
        Package {
            file: "kernel-core-7.2.10-200.fc44.aarch64.rpm".into(),
            name: "kernel-core".into(),
            version: "7.2.10".into(),
            release: "200.fc44".into(),
            arch: "aarch64".into(),
            size: 5,
            sha256: KERNEL_SHA.into(),
            source_rpm: Some("kernel-7.2.10-200.fc44.src.rpm".into()),
        }
    }

    #[test]
    fn sources_are_releases_then_updates_then_koji() {
        let m = manifest();
        assert_eq!(m.sources(&kernel()), vec![
            "https://dl.fedoraproject.org/pub/fedora/linux/releases/44/Everything/aarch64/os/Packages/k/kernel-core-7.2.10-200.fc44.aarch64.rpm",
            "https://dl.fedoraproject.org/pub/fedora/linux/updates/44/Everything/aarch64/Packages/k/kernel-core-7.2.10-200.fc44.aarch64.rpm",
            "https://kojipkgs.fedoraproject.org/packages/kernel/7.2.10/200.fc44/data/signed/6d9f90a6/aarch64/kernel-core-7.2.10-200.fc44.aarch64.rpm",
        ]);
        let fex = m.sources(&m.fex_rootfs.package);
        assert!(fex[0].ends_with("/44/Everything/aarch64/os/Packages/f/fex-emu-rootfs-fedora-44%5E20260410.n.0-1.fc44.noarch.rpm"), "{}", fex[0]);
        assert!(fex[2].ends_with("/fex-emu-rootfs-fedora/44%5E20260410.n.0/1.fc44/data/signed/6d9f90a6/noarch/fex-emu-rootfs-fedora-44%5E20260410.n.0-1.fc44.noarch.rpm"), "{}", fex[2]);
    }

    #[test]
    fn mirrors_replace_fedoras_sources() {
        let mut m = manifest();
        m.mirrors = Some(vec!["/tmp/rpms/".into()]);
        assert_eq!(m.sources(&kernel()), vec!["/tmp/rpms/kernel-core-7.2.10-200.fc44.aarch64.rpm"]);
    }

    #[test]
    fn decodes_the_manifests_fields() {
        let m = manifest();
        assert_eq!(m.version, 1);
        assert_eq!(m.kernel, "7.2.10-200.fc44.aarch64");
        assert_eq!(m.parts.url, "https://example.invalid/parts.tar.xz");
        assert_eq!(m.fex_rootfs.package.name, "fex-emu-rootfs-fedora");
        assert_eq!(m.koji.signed_key, "6d9f90a6");
    }
}
