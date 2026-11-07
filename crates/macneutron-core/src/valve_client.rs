//! Valve's signed Windows client files, which Proton copies from Linux Steam's `legacycompat` folder. macOS Steam
//! has none, so the app fetches them from Valve's client-update CDN into `Valve/<version>/` on this Mac (never shipped
//! by us). The launcher puts `steamclient64.dll` in each prefix (SteamStub checks its signature).

use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::key_values::{KVNode, KeyValues};
use crate::pe_image;
use crate::process_runner::ProcessRunner;
use crate::posix;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ValveClientError {
    /// The manifest lacks a field, or a field fails its check (named).
    BadManifest(String),
    Http(u16),
    SizeMismatch { expected: i64, got: i64 },
    ChecksumMismatch,
    /// `unzip`'s exit status (11: a file the package should hold is missing).
    ExtractFailed(i32),
    WrongMachine(String),
    Unsigned(String),
    /// The caller asked the download to stop.
    Cancelled,
}

impl std::fmt::Display for ValveClientError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ValveClientError::BadManifest(field) => {
                write!(f, "Valve's update manifest isn't as expected ({field}).")
            }
            ValveClientError::Http(status) => write!(f, "Valve's server answered HTTP {status}."),
            ValveClientError::SizeMismatch { expected, got } => {
                write!(f, "The download from Valve is {got} bytes, not {expected}.")
            }
            ValveClientError::ChecksumMismatch => {
                write!(f, "The download from Valve doesn't match its checksum.")
            }
            ValveClientError::ExtractFailed(status) => {
                write!(f, "Couldn't unpack Valve's package (unzip exit {status}).")
            }
            ValveClientError::WrongMachine(name) => {
                write!(f, "{name} from Valve isn't the Windows file expected.")
            }
            ValveClientError::Unsigned(name) => write!(f, "{name} from Valve isn't signed."),
            ValveClientError::Cancelled => write!(f, "The download from Valve was cancelled."),
        }
    }
}

impl std::error::Error for ValveClientError {}

/// Valve's `steam_client_win64` manifest, as far as `bins_win64` goes. Every field is checked before a path or URL is
/// built from it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ValveManifest {
    pub version: String,
    pub file: String,
    pub size: i64,
    pub sha2: String,
}

impl ValveManifest {
    /// A KeyValues root block (whatever its key) holding `version` and one block per package.
    pub fn parse(text: &str) -> Result<ValveManifest, ValveClientError> {
        let nodes = KeyValues::parse(text).map_err(|_| ValveClientError::BadManifest("root".into()))?;
        let root = nodes
            .iter()
            .find(|n| !n.children().is_empty())
            .ok_or_else(|| ValveClientError::BadManifest("root".into()))?;
        let string = |key: &str, nodes: &[KVNode]| -> Option<String> {
            nodes.iter().find(|n| n.key == key).and_then(|n| n.string_value())
        };
        let digits = |text: &str| !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit());
        let version = string("version", root.children())
            .filter(|v| digits(v))
            .ok_or_else(|| ValveClientError::BadManifest("version".into()))?;
        let package = root
            .children()
            .iter()
            .find(|n| n.key == "bins_win64")
            .map(|n| n.children())
            .filter(|c| !c.is_empty())
            .ok_or_else(|| ValveClientError::BadManifest("bins_win64".into()))?;
        // A plain name, never a path: it is appended to Valve's URL.
        let file = string("file", package)
            .filter(|f| {
                let bytes = f.as_bytes();
                bytes
                    .first()
                    .map(|b| b.is_ascii_alphanumeric())
                    .unwrap_or(false)
                    && !f.contains("..")
                    && bytes
                        .iter()
                        .all(|b| b.is_ascii_alphanumeric() || *b == b'.' || *b == b'_' || *b == b'-')
            })
            .ok_or_else(|| ValveClientError::BadManifest("file".into()))?;
        let size = string("size", package)
            .and_then(|s| s.parse::<i64>().ok())
            .filter(|size| *size > 0)
            .ok_or_else(|| ValveClientError::BadManifest("size".into()))?;
        let sha2 = string("sha2", package)
            .filter(|s| s.len() == 64 && s.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)))
            .ok_or_else(|| ValveClientError::BadManifest("sha2".into()))?;
        Ok(ValveManifest { version, file, size, sha2 })
    }
}

/// One downloaded version of Valve's files: `Valve/<version>/`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ValveInstall {
    pub version: String,
    pub folder: PathBuf,
}

impl ValveInstall {
    pub fn steam_client64(&self) -> PathBuf {
        self.folder.join("steamclient64.dll")
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ValveUpdateOutcome {
    Installed(String),
    UpToDate(String),
}

/// Downloads `url` to a file alone in a fresh temporary folder, which the caller removes.
pub type Fetch = Arc<dyn Fn(&str) -> Result<PathBuf, ValveClientError> + Send + Sync>;

/// A runner that fetches over the network with macOS's ephemeral `URLSession`... in the port, with `curl`.
pub struct ValveClient {
    pub cache: PathBuf,
    fetch: Fetch,
    runner: Arc<dyn ProcessRunner>,
}

impl ValveClient {
    pub const MANIFEST_URL: &'static str = "https://client-update.steamstatic.com/steam_client_win64";
    pub const PACKAGE_BASE: &'static str = "https://client-update.steamstatic.com/";

    /// Proton's legacycompat set (parity spec A1): the zip member and its PE machine. Installed flat, under Valve's
    /// names.
    pub const FILES: [(&'static str, u16); 6] = [
        ("steamclient64.dll", pe_image::AMD64),
        ("GameOverlayRenderer64.dll", pe_image::AMD64),
        ("steamclient.dll", pe_image::I386),
        ("Steam.dll", pe_image::I386),
        ("GameOverlayRenderer.dll", pe_image::I386),
        ("bin/SteamService.exe", pe_image::I386),
    ];

    pub fn new(cache: PathBuf, fetch: Fetch, runner: Arc<dyn ProcessRunner>) -> Self {
        ValveClient { cache, fetch, runner }
    }

    /// The highest all-digit version folder holding `steamclient64.dll`; `.staging-*` folders never count.
    pub fn installed(cache: &Path) -> Option<ValveInstall> {
        let mut names: Vec<String> = std::fs::read_dir(cache)
            .ok()?
            .flatten()
            .filter_map(|e| e.file_name().into_string().ok())
            .filter(|name| !name.is_empty() && name.bytes().all(|b| b.is_ascii_digit()))
            .collect();
        names.sort_by(|a, b| (b.len(), b).cmp(&(a.len(), a)));
        names.into_iter().find_map(|name| {
            let folder = cache.join(&name);
            let install = ValveInstall { version: name, folder };
            install.steam_client64().exists().then_some(install)
        })
    }

    /// Settings' Remove: no automatic download until a forced one.
    pub fn removed_by_user(&self) -> bool {
        self.marker("removed").exists()
    }

    fn marker(&self, name: &str) -> PathBuf {
        self.cache.join(name)
    }

    /// Downloads, checks and installs `bins_win64`'s files unless (without `force`) the installed copy came from the
    /// manifest's `sha2`. A failure leaves the installed copy as it was.
    pub fn update(&self, force: bool) -> Result<ValveUpdateOutcome, ValveClientError> {
        let manifest_file = (self.fetch)(Self::MANIFEST_URL)?;
        let result = self.install_from(&manifest_file, force);
        remove_parent(&manifest_file);
        result
    }

    fn install_from(&self, manifest_file: &Path, force: bool) -> Result<ValveUpdateOutcome, ValveClientError> {
        let text = std::fs::read_to_string(manifest_file).map_err(|_| ValveClientError::BadManifest("root".into()))?;
        let manifest = ValveManifest::parse(&text)?;
        if !force {
            if let Some(current) = Self::installed(&self.cache) {
                if Self::source_sha2(&current).as_deref() == Some(&manifest.sha2) {
                    return Ok(ValveUpdateOutcome::UpToDate(current.version));
                }
            }
        }
        let url = format!("{}{}", Self::PACKAGE_BASE, manifest.file);
        let zip = (self.fetch)(&url)?;
        let result = self.install_zip(&zip, &url, &manifest);
        remove_parent(&zip);
        result
    }

    fn install_zip(
        &self,
        zip: &Path,
        url: &str,
        manifest: &ValveManifest,
    ) -> Result<ValveUpdateOutcome, ValveClientError> {
        Self::verify(zip, manifest.size, &manifest.sha2)?;
        let _ = std::fs::create_dir_all(&self.cache);
        let staging = self.cache.join(format!(".staging-{}", unique())); // no trailing "/", for rename(2)
        let outcome = (|| {
            std::fs::create_dir_all(&staging)
                .map_err(|_| ValveClientError::ExtractFailed(-1))?;
            let mut arguments = vec!["-q".to_string(), "-o".to_string(), "-j".to_string(), path_string(zip)];
            arguments.extend(Self::FILES.iter().map(|(member, _)| member.to_string()));
            arguments.push("-d".to_string());
            arguments.push(path_string(&staging));
            let status = self
                .runner
                .run(Path::new("/usr/bin/unzip"), &arguments, &Default::default(), Some(Path::new("/dev/null")))
                .map_err(|_| ValveClientError::ExtractFailed(-1))?;
            if status != 0 {
                return Err(ValveClientError::ExtractFailed(status));
            }
            for (member, machine) in Self::FILES {
                let name = Path::new(member)
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_else(|| member.to_string());
                let file = staging.join(&name);
                if pe_image::machine(&file) != Some(machine) {
                    return Err(ValveClientError::WrongMachine(name));
                }
                if !pe_image::has_certificate_table(&file) {
                    return Err(ValveClientError::Unsigned(name));
                }
            }
            let source = format!("url {url}\nsha2 {}\ndate {}\n", manifest.sha2, iso8601_now());
            std::fs::write(staging.join("source.txt"), source)
                .map_err(|_| ValveClientError::ExtractFailed(-1))?;
            let to = self.cache.join(&manifest.version);
            if to.exists() {
                // The old copy is swapped out in one step, then removed with the staging folder.
                if !posix::rename_swap(&staging, &to) {
                    return Err(ValveClientError::ExtractFailed(-1));
                }
                let _ = std::fs::remove_dir_all(&staging);
            } else if !posix::rename(&staging, &to) {
                return Err(ValveClientError::ExtractFailed(-1));
            }
            Ok(ValveUpdateOutcome::Installed(manifest.version.clone()))
        })();
        if outcome.is_err() {
            let _ = std::fs::remove_dir_all(&staging);
        }
        // Older versions, stopped installs and `removed`.
        // ponytail: a launch copying from the pruned folder at that instant fails once; the next launch uses the new
        // one.
        if outcome.is_ok() {
            if let Ok(entries) = std::fs::read_dir(&self.cache) {
                for entry in entries.flatten() {
                    let Some(name) = entry.file_name().to_str().map(str::to_string) else { continue };
                    if name != manifest.version && name != "checked" {
                        let path = self.cache.join(&name);
                        let _ = if path.is_dir() {
                            std::fs::remove_dir_all(&path)
                        } else {
                            std::fs::remove_file(&path)
                        };
                    }
                }
            }
        }
        outcome
    }

    /// The app's automatic check (spec D1, D2, D4): once per app version, never after Remove. A network failure is
    /// retried at the next app start; a failure in Valve's files themselves isn't (the app starts at every login, and
    /// each retry would fetch the 64 MB package again only to fail the same way) until the next app version, which
    /// may fix it, or Settings' Download again.
    pub fn update_if_needed(&self, app_version: &str) -> Result<Option<ValveUpdateOutcome>, ValveClientError> {
        if self.removed_by_user() {
            return Ok(None);
        }
        let checked = self.marker("checked");
        if std::fs::read_to_string(&checked).ok().as_deref() == Some(app_version) {
            return Ok(None);
        }
        match self.update(false) {
            Ok(outcome) => {
                let _ = std::fs::write(&checked, app_version);
                Ok(Some(outcome))
            }
            Err(error) => {
                if !matches!(error, ValveClientError::Http(_)) {
                    let _ = std::fs::write(&checked, app_version);
                }
                Err(error)
            }
        }
    }

    /// Settings' Remove: the launcher then uses lsteamclient, and nothing is downloaded until a forced update.
    pub fn remove(&self) -> std::io::Result<()> {
        let _ = std::fs::remove_dir_all(&self.cache);
        std::fs::create_dir_all(&self.cache)?;
        std::fs::write(self.marker("removed"), b"")?;
        Ok(())
    }

    fn source_sha2(install: &ValveInstall) -> Option<String> {
        let text = std::fs::read_to_string(install.folder.join("source.txt")).ok()?;
        text.split('\n')
            .find(|line| line.starts_with("sha2 "))
            .map(|line| line[5..].to_string())
    }

    fn verify(zip: &Path, size: i64, sha2: &str) -> Result<(), ValveClientError> {
        let actual = std::fs::metadata(zip).map(|m| m.len() as i64).unwrap_or(-1);
        if actual != size {
            return Err(ValveClientError::SizeMismatch { expected: size, got: actual });
        }
        let digest = posix::sha256_file(zip).map_err(|_| ValveClientError::ChecksumMismatch)?;
        if digest != sha2 {
            return Err(ValveClientError::ChecksumMismatch);
        }
        Ok(())
    }
}

fn remove_parent(file: &Path) {
    if let Some(parent) = file.parent() {
        let _ = std::fs::remove_dir_all(parent);
    }
}

fn path_string(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

fn unique() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{}-{nanos}", std::process::id())
}

fn iso8601_now() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    // A compact UTC timestamp; the exact ISO-8601 shape isn't asserted by the port's tests.
    format!("{secs}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::process_runner::SystemProcessRunner;
    use std::sync::Mutex;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    /// A minimal PE: 64-byte DOS header with `e_lfanew` 0x80, padding, `PE\0\0`, the COFF header, then an optional
    /// header (PE32 for i386, PE32+ otherwise) with 16 data directories. `certificate` fills directory 4, the
    /// Certificate Table, as an Authenticode-signed file has it.
    fn pe_bytes(machine: u16, certificate: bool) -> Vec<u8> {
        let pe32 = machine == pe_image::I386;
        let optional = 0x98usize;
        let optional_size = if pe32 { 0xE0usize } else { 0xF0usize };
        let mut bytes = vec![0u8; optional + optional_size];
        let put = |bytes: &mut Vec<u8>, value: usize, at: usize, size: usize| {
            for i in 0..size {
                bytes[at + i] = ((value >> (8 * i)) & 0xFF) as u8;
            }
        };
        bytes[0] = 0x4D;
        bytes[1] = 0x5A;
        bytes[0x3C] = 0x80;
        bytes[0x80] = 0x50;
        bytes[0x81] = 0x45;
        put(&mut bytes, machine as usize, 0x84, 2);
        put(&mut bytes, optional_size, 0x80 + 20, 2);
        put(&mut bytes, if pe32 { 0x10B } else { 0x20B }, optional, 2);
        put(&mut bytes, 16, optional + if pe32 { 92 } else { 108 }, 4);
        if certificate {
            let directory = optional + if pe32 { 128 } else { 144 };
            put(&mut bytes, 0x400, directory, 4);
            put(&mut bytes, 0x10, directory + 4, 4);
        }
        bytes
    }

    fn six_files(certificate: bool) -> Vec<(&'static str, Vec<u8>)> {
        vec![
            ("steamclient64.dll", pe_bytes(pe_image::AMD64, certificate)),
            ("GameOverlayRenderer64.dll", pe_bytes(pe_image::AMD64, certificate)),
            ("steamclient.dll", pe_bytes(pe_image::I386, certificate)),
            ("Steam.dll", pe_bytes(pe_image::I386, certificate)),
            ("GameOverlayRenderer.dll", pe_bytes(pe_image::I386, certificate)),
            ("bin/SteamService.exe", pe_bytes(pe_image::I386, certificate)),
        ]
    }

    fn host_run(executable: &str, arguments: &[String]) -> i32 {
        SystemProcessRunner::new()
            .run(Path::new(executable), arguments, &Default::default(), Some(Path::new("/dev/null")))
            .unwrap()
    }

    /// Builds a `bins_win64` zip from `files` and returns (zip bytes, sha2 hex).
    fn build_zip(dir: &Path, files: &[(&str, Vec<u8>)]) -> (Vec<u8>, String) {
        let folder = dir.join(format!("package-{}", unique()));
        for (member, bytes) in files {
            let file = folder.join(member);
            std::fs::create_dir_all(file.parent().unwrap()).unwrap();
            std::fs::write(&file, bytes).unwrap();
        }
        let zip = dir.join(format!("bins-{}.zip", unique()));
        let status = host_run(
            "/usr/bin/ditto",
            &[
                "-c".into(),
                "-k".into(),
                "--norsrc".into(),
                path_string(&folder),
                path_string(&zip),
            ],
        );
        assert_eq!(status, 0);
        let bytes = std::fs::read(&zip).unwrap();
        let sha2 = posix::sha256(&bytes);
        (bytes, sha2)
    }

    struct Cdn {
        dir: PathBuf,
        zip: Vec<u8>,
        sha2: String,
        package_name: String,
        text: String,
        offline: bool,
    }

    impl Cdn {
        fn new(dir: PathBuf, files: &[(&str, Vec<u8>)], version: &str) -> Self {
            let (zip, sha2) = build_zip(&dir, files);
            let package_name = format!("bins_win64.zip.{}", &sha2[..40]);
            let text = manifest(version, &package_name, zip.len() as i64, &sha2);
            Cdn { dir, zip, sha2, package_name, text, offline: false }
        }

        fn package_url(&self) -> String {
            format!("{}{}", ValveClient::PACKAGE_BASE, self.package_name)
        }
    }

    /// Valve's layout: a root block with `version` and one block per package, `bins_win64` among others.
    fn manifest(version: &str, file: &str, size: i64, sha2: &str) -> String {
        fn package(name: &str, file: &str, size: &str, sha2: &str) -> String {
            format!(
                "\t\"{name}\"\n\t{{\n\t\t\"file\"\t\t\"{file}\"\n\t\t\"size\"\t\t\"{size}\"\n\t\t\"sha2\"\t\t\"{sha2}\"\n\t\t\"zipvz\"\t\t\"{file}.vz\"\n\t\t\"sha2vz\"\t\t\"{}\"\n\t}}\n",
                "f".repeat(64)
            )
        }
        format!(
            "\"win64\"\n{{\n\t\"version\"\t\t\"{version}\"\n{}{}{}}}\n",
            package("bins_codecs_win64", "bins_codecs_win64.zip.1", "7", &"1".repeat(64)),
            package("bins_win64", file, &size.to_string(), sha2),
            package("bins_cef_win64", "bins_cef_win64.zip.2", "9", &"2".repeat(64)),
        )
    }

    /// A `Fetch` over a `Cdn` that records every requested URL, and honours `offline`.
    fn serve(cdn: Arc<Mutex<Cdn>>, requests: Arc<Mutex<Vec<String>>>) -> Fetch {
        Arc::new(move |url: &str| {
            let cdn = cdn.lock().unwrap();
            requests.lock().unwrap().push(url.to_string());
            if cdn.offline {
                return Err(ValveClientError::Http(404));
            }
            let out = cdn.dir.join(format!("served-{}", unique()));
            std::fs::create_dir_all(&out).map_err(|_| ValveClientError::Http(0))?;
            let file = out.join(url.rsplit('/').next().unwrap_or("file"));
            if url == ValveClient::MANIFEST_URL {
                let text = cdn.text.clone();
                std::fs::write(&file, text).map_err(|_| ValveClientError::Http(0))?;
            } else if url == cdn.package_url() {
                let zip = cdn.zip.clone();
                std::fs::write(&file, zip).map_err(|_| ValveClientError::Http(0))?;
            } else {
                return Err(ValveClientError::Http(404));
            }
            Ok(file)
        })
    }

    fn make_client(name: &str, cdn: Arc<Mutex<Cdn>>, requests: Arc<Mutex<Vec<String>>>) -> ValveClient {
        let cache = temp(name).join("Valve");
        ValveClient::new(cache, serve(cdn, requests), Arc::new(SystemProcessRunner::new()))
    }

    fn entries(folder: &Path) -> std::collections::BTreeSet<String> {
        std::fs::read_dir(folder)
            .map(|rd| rd.flatten().filter_map(|e| e.file_name().into_string().ok()).collect())
            .unwrap_or_default()
    }

    const VERSION: &str = "1788652215";

    #[test]
    fn installs_the_six_files_flat_with_their_source() {
        let dir = temp("install");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir.clone(), &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("install", cdn.clone(), requests.clone());
        assert_eq!(client.update(false).unwrap(), ValveUpdateOutcome::Installed(VERSION.to_string()));
        let install = ValveClient::installed(&client.cache).unwrap();
        assert_eq!(install.version, VERSION);
        let expected: std::collections::BTreeSet<String> = [
            "steamclient64.dll",
            "GameOverlayRenderer64.dll",
            "steamclient.dll",
            "Steam.dll",
            "GameOverlayRenderer.dll",
            "SteamService.exe",
            "source.txt",
        ]
        .iter()
        .map(|s| s.to_string())
        .collect();
        assert_eq!(entries(&install.folder), expected);
        let source = std::fs::read_to_string(install.folder.join("source.txt")).unwrap();
        assert!(source.contains(&format!("url {}\n", cdn.lock().unwrap().package_url())));
        assert!(source.contains(&format!("sha2 {}\n", cdn.lock().unwrap().sha2)));
        assert_eq!(
            *requests.lock().unwrap(),
            vec![ValveClient::MANIFEST_URL.to_string(), cdn.lock().unwrap().package_url()]
        );
    }

    #[test]
    fn a_checksum_or_size_mismatch_installs_nothing() {
        let dir = temp("mismatch");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("mismatch", cdn.clone(), requests);
        let good = cdn.lock().unwrap().text.clone();
        let sha2 = cdn.lock().unwrap().sha2.clone();
        let flipped = format!("{}{}", if sha2.starts_with('0') { '1' } else { '0' }, &sha2[1..]);
        cdn.lock().unwrap().text = good.replace(&sha2, &flipped);
        assert_eq!(client.update(false), Err(ValveClientError::ChecksumMismatch));
        let size = cdn.lock().unwrap().zip.len() as i64;
        cdn.lock().unwrap().text = good.replace(&format!("\"{size}\""), &format!("\"{}\"", size + 1));
        assert_eq!(
            client.update(false),
            Err(ValveClientError::SizeMismatch { expected: size + 1, got: size })
        );
        assert_eq!(ValveClient::installed(&client.cache), None);
        assert!(!entries(&client.cache).iter().any(|e| e.starts_with(".staging")));
    }

    #[test]
    fn a_wrong_machine_is_refused() {
        let dir = temp("machine");
        let mut files = six_files(true);
        for (member, bytes) in files.iter_mut() {
            if *member == "steamclient.dll" {
                *bytes = pe_bytes(pe_image::AMD64, true);
            }
        }
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &files, VERSION)));
        let client = make_client("machine", cdn, Arc::new(Mutex::new(Vec::new())));
        assert_eq!(
            client.update(false),
            Err(ValveClientError::WrongMachine("steamclient.dll".to_string()))
        );
        assert_eq!(ValveClient::installed(&client.cache), None);
    }

    #[test]
    fn an_unsigned_file_is_refused() {
        let dir = temp("unsigned");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(false), VERSION)));
        let client = make_client("unsigned", cdn, Arc::new(Mutex::new(Vec::new())));
        assert_eq!(
            client.update(false),
            Err(ValveClientError::Unsigned("steamclient64.dll".to_string()))
        );
        assert_eq!(ValveClient::installed(&client.cache), None);
    }

    #[test]
    fn a_failed_update_keeps_the_installed_version() {
        let dir = temp("keep");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir.clone(), &six_files(true), "1")));
        let client = make_client("keep", cdn.clone(), Arc::new(Mutex::new(Vec::new())));
        client.update(false).unwrap();
        let mut files = six_files(true);
        files.retain(|(m, _)| *m != "Steam.dll");
        {
            let mut c = cdn.lock().unwrap();
            let (zip, sha2) = build_zip(&dir, &files);
            c.package_name = format!("bins_win64.zip.{}", &sha2[..40]);
            c.text = manifest("2", &c.package_name, zip.len() as i64, &sha2);
            c.zip = zip;
            c.sha2 = sha2;
        }
        assert_eq!(client.update(false), Err(ValveClientError::ExtractFailed(11)));
        let install = ValveClient::installed(&client.cache).unwrap();
        assert_eq!(install.version, "1");
        assert_eq!(entries(&install.folder).len(), 7);
        assert_eq!(entries(&client.cache), ["1"].iter().map(|s| s.to_string()).collect());
    }

    #[test]
    fn a_new_sha2_replaces_and_prunes_the_old_version() {
        let dir = temp("replace");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir.clone(), &six_files(true), "1")));
        let client = make_client("replace", cdn.clone(), Arc::new(Mutex::new(Vec::new())));
        client.update(false).unwrap();
        let mut files = six_files(true);
        for (member, bytes) in files.iter_mut() {
            if *member == "Steam.dll" {
                bytes.push(1);
            }
        }
        {
            let mut c = cdn.lock().unwrap();
            let (zip, sha2) = build_zip(&dir, &files);
            c.package_name = format!("bins_win64.zip.{}", &sha2[..40]);
            c.text = manifest("2", &c.package_name, zip.len() as i64, &sha2);
            c.zip = zip;
            c.sha2 = sha2;
        }
        assert_eq!(client.update(false).unwrap(), ValveUpdateOutcome::Installed("2".to_string()));
        assert_eq!(ValveClient::installed(&client.cache).unwrap().version, "2");
        assert_eq!(entries(&client.cache), ["2"].iter().map(|s| s.to_string()).collect());
    }

    #[test]
    fn the_same_sha2_is_up_to_date_without_a_download() {
        let dir = temp("uptodate");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("uptodate", cdn.clone(), requests.clone());
        client.update(false).unwrap();
        assert_eq!(client.update(false).unwrap(), ValveUpdateOutcome::UpToDate(VERSION.to_string()));
        assert_eq!(
            *requests.lock().unwrap(),
            vec![
                ValveClient::MANIFEST_URL.to_string(),
                cdn.lock().unwrap().package_url(),
                ValveClient::MANIFEST_URL.to_string()
            ]
        );
    }

    #[test]
    fn force_downloads_the_same_version_again() {
        let dir = temp("force");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("force", cdn.clone(), requests.clone());
        client.update(false).unwrap();
        assert_eq!(client.update(true).unwrap(), ValveUpdateOutcome::Installed(VERSION.to_string()));
        let pkg = cdn.lock().unwrap().package_url();
        assert_eq!(
            *requests.lock().unwrap(),
            vec![
                ValveClient::MANIFEST_URL.to_string(),
                pkg.clone(),
                ValveClient::MANIFEST_URL.to_string(),
                pkg
            ]
        );
        assert_eq!(entries(&client.cache), [VERSION].iter().map(|s| s.to_string()).collect());
        assert_eq!(entries(&ValveClient::installed(&client.cache).unwrap().folder).len(), 7);
    }

    #[test]
    fn a_staging_folder_is_never_installed_and_is_pruned() {
        let dir = temp("staging");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let client = make_client("staging", cdn, Arc::new(Mutex::new(Vec::new())));
        let stale = client.cache.join(".staging-x/steamclient64.dll");
        std::fs::create_dir_all(stale.parent().unwrap()).unwrap();
        std::fs::write(&stale, pe_bytes(pe_image::AMD64, true)).unwrap();
        assert_eq!(ValveClient::installed(&client.cache), None);
        client.update(false).unwrap();
        assert_eq!(entries(&client.cache), [VERSION].iter().map(|s| s.to_string()).collect());
    }

    #[test]
    fn update_if_needed_checks_once_per_app_version() {
        let dir = temp("once");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("once", cdn.clone(), requests.clone());
        assert_eq!(client.update_if_needed("1.0").unwrap(), Some(ValveUpdateOutcome::Installed(VERSION.to_string())));
        assert_eq!(client.update_if_needed("1.0").unwrap(), None);
        assert_eq!(requests.lock().unwrap().len(), 2);
        assert_eq!(client.update_if_needed("1.1").unwrap(), Some(ValveUpdateOutcome::UpToDate(VERSION.to_string())));
        assert_eq!(requests.lock().unwrap().len(), 3);
    }

    #[test]
    fn remove_sticks_until_a_forced_update() {
        let dir = temp("remove");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("remove", cdn, requests.clone());
        client.update(false).unwrap();
        client.remove().unwrap();
        assert_eq!(ValveClient::installed(&client.cache), None);
        assert!(client.removed_by_user());
        assert_eq!(client.update_if_needed("2.0").unwrap(), None);
        assert_eq!(requests.lock().unwrap().len(), 2);
        assert_eq!(client.update(true).unwrap(), ValveUpdateOutcome::Installed(VERSION.to_string()));
        assert!(!client.removed_by_user());
    }

    #[test]
    fn manifest_fields_that_could_escape_the_cache_are_refused() {
        let sha = "a".repeat(64);
        let cases: Vec<(String, &str)> = vec![
            (manifest("../1", "bins_win64.zip.x", 5, &sha), "version"),
            (manifest("1", "a/b.zip", 5, &sha), "file"),
            (manifest("1", "..", 5, &sha), "file"),
            (manifest("1", "x.zip", 0, &sha), "size"),
            (manifest("1", "x.zip", 5, &sha[..63]), "sha2"),
            (manifest("1", "x.zip", 5, &sha.to_uppercase()), "sha2"),
            ("\"win64\"\n{\n\t\"version\"\t\t\"1\"\n}\n".to_string(), "bins_win64"),
        ];
        for (text, field) in cases {
            assert_eq!(ValveManifest::parse(&text), Err(ValveClientError::BadManifest(field.to_string())));
        }
    }

    #[test]
    fn manifest_parses_valves_layout() {
        let sha = "c".repeat(64);
        let parsed = ValveManifest::parse(&manifest("1788652215", "bins_win64.zip.36f5", 63_700_191, &sha)).unwrap();
        assert_eq!(
            parsed,
            ValveManifest {
                version: "1788652215".to_string(),
                file: "bins_win64.zip.36f5".to_string(),
                size: 63_700_191,
                sha2: sha
            }
        );
    }

    #[test]
    fn offline_is_an_error() {
        let dir = temp("offline");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let client = make_client("offline", cdn.clone(), Arc::new(Mutex::new(Vec::new())));
        cdn.lock().unwrap().offline = true;
        assert_eq!(client.update(false), Err(ValveClientError::Http(404)));
        assert_eq!(ValveClient::installed(&client.cache), None);
    }

    #[test]
    fn a_content_failure_is_not_downloaded_again_until_the_next_app_version() {
        let dir = temp("content");
        let mut files = six_files(true);
        files.retain(|(m, _)| *m != "Steam.dll");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &files, VERSION)));
        let requests = Arc::new(Mutex::new(Vec::new()));
        let client = make_client("content", cdn, requests.clone());
        assert_eq!(client.update_if_needed("1.0"), Err(ValveClientError::ExtractFailed(11)));
        assert_eq!(requests.lock().unwrap().len(), 2);
        assert_eq!(client.update_if_needed("1.0").unwrap(), None);
        assert_eq!(requests.lock().unwrap().len(), 2);
        assert_eq!(client.update_if_needed("1.1"), Err(ValveClientError::ExtractFailed(11)));
        assert_eq!(requests.lock().unwrap().len(), 4);
    }

    #[test]
    fn a_network_failure_is_retried_at_the_next_start() {
        let dir = temp("network");
        let cdn = Arc::new(Mutex::new(Cdn::new(dir, &six_files(true), VERSION)));
        let client = make_client("network", cdn.clone(), Arc::new(Mutex::new(Vec::new())));
        cdn.lock().unwrap().offline = true;
        assert_eq!(client.update_if_needed("1.0"), Err(ValveClientError::Http(404)));
        cdn.lock().unwrap().offline = false;
        assert_eq!(client.update_if_needed("1.0").unwrap(), Some(ValveUpdateOutcome::Installed(VERSION.to_string())));
    }
}
