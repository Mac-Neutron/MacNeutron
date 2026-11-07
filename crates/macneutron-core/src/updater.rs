//! The app's own updates, from GitHub Releases.
//!
//! The Rust port has no HTTP client and no JSON crate: [`pick`] parses GitHub's reply with a minimal hand-rolled
//! scanner (the Swift used `JSONDecoder`), and the signature/Gatekeeper checks shell out to `codesign` and `spctl`
//! (the Swift used the Security framework).

use std::collections::BTreeMap;
use std::fmt;
use std::path::{Path, PathBuf};
use crate::app_installer::AppInstallerError;
use crate::process_runner::ProcessRunner;
use crate::valve_client::Fetch;
use crate::vm_downloads::{standard_fetch, VMDownloads};

/// A MacNeutron release on GitHub that the app can install over itself.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Release {
    pub version: String,
    /// Its GitHub page (release notes).
    pub page: String,
    pub zip: String,
    pub sums: String,
    /// `MacNeutron-<V>-Installer.zip` (spec 2026-10-25-macneutron-installer-design §3.3); None for a release
    /// without one.
    pub installer: Option<String>,
}

/// What [`update`] did: swapped the new app in (its work folder holds the old app until [`relaunch`]), or downloaded
/// and verified the release's installer, which the caller opens with `--from-app`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpdateOutcome {
    Swapped(PathBuf),
    Installer(PathBuf),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpdateError {
    /// The folder the app runs from.
    CantReplace(String),
    Checksum,
    Signature(String),
    Version(String),
    Gatekeeper(String),
}

impl fmt::Display for UpdateError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            UpdateError::CantReplace(folder) => {
                if folder.contains("/AppTranslocation/") {
                    write!(
                        formatter,
                        "MacNeutron is running from a temporary, read-only copy macOS made because it was opened from Downloads. "
                    )?;
                } else {
                    write!(formatter, "MacNeutron is running from {folder}, which it can't replace. ")?;
                }
                write!(formatter, "Download Install MacNeutron from the release page, or move MacNeutron to Applications.")
            }
            UpdateError::Checksum => {
                write!(formatter, "The download doesn't match the release's SHA256SUMS.")
            }
            UpdateError::Signature(detail) => {
                write!(formatter, "The download isn't signed by MacNeutron's developer: {detail}")
            }
            UpdateError::Version(version) => {
                write!(formatter, "The download is version {version}, not the release's.")
            }
            UpdateError::Gatekeeper(detail) => {
                write!(formatter, "macOS doesn't accept the download: {detail}")
            }
        }
    }
}

impl std::error::Error for UpdateError {}

pub const RELEASES: &str = "https://api.github.com/repos/Mac-Neutron/MacNeutron/releases?per_page=30";
pub const IDENTIFIER: &str = "io.github.chadouming.MacNeutron";
/// Install MacNeutron's (spec 2026-10-25-macneutron-installer-design §3.1).
pub const INSTALLER_IDENTIFIER: &str = "io.github.chadouming.MacNeutron.Installer";
/// What an update may be: this app, or the release after it, which moves to github.com/Mac-Neutron's identifiers
/// (2026-10-27) and publishes its app as MacNeutron-<V>-macOS.zip. 0.9.6 looks for neither, so it takes this
/// release first and this one installs that.
pub fn identifiers() -> [&'static str; 2] {
    [IDENTIFIER, "io.github.mac-neutron.MacNeutron"]
}
pub fn installer_identifiers() -> [&'static str; 2] {
    [INSTALLER_IDENTIFIER, "io.github.mac-neutron.MacNeutron.Installer"]
}

const INSTALLER_FOLDER: &str = "MacNeutron-installer-";

/// The newest release of the running build's channel (`X.Y.Z`, or `X.Y.Zp` for a performance build) above
/// `current`, from GitHub's release list.
pub fn pick(json: &[u8], current: &str) -> Option<Release> {
    let (running, running_performance) = version(current)?;
    let list = parse_releases(json);
    let mut best: Option<([i64; 3], Release)> = None;
    for entry in list {
        if entry.draft || entry.prerelease || !entry.tag_name.starts_with('v') {
            continue;
        }
        let name = entry.tag_name[1..].to_string();
        let Some((found, found_performance)) = version(&name) else { continue };
        if found_performance != running_performance || !lexicographically_precedes(&running, &found) {
            continue;
        }
        if let Some((best_numbers, _)) = &best {
            if !lexicographically_precedes(best_numbers, &found) {
                continue;
            }
        }
        let Some(zip) = entry
            .assets
            .iter()
            .find(|asset| asset.name == format!("MacNeutron-{name}.zip") || asset.name == format!("MacNeutron-{name}-macOS.zip"))
        else {
            continue;
        };
        let Some(sums) = entry.assets.iter().find(|asset| asset.name == "SHA256SUMS") else { continue };
        if !is_https(&entry.html_url) || !is_https(&zip.browser_download_url) || !is_https(&sums.browser_download_url) {
            continue;
        }
        let installer = entry
            .assets
            .iter()
            .find(|asset| asset.name == format!("MacNeutron-{name}-Installer.zip"))
            .map(|asset| asset.browser_download_url.clone());
        best = Some((
            found,
            Release {
                version: name,
                page: entry.html_url.clone(),
                zip: zip.browser_download_url.clone(),
                sums: sums.browser_download_url.clone(),
                installer: installer.filter(|url| is_https(url)),
            },
        ));
    }
    best.map(|(_, release)| release)
}

/// Not a translocated copy (macOS runs an app opened from Downloads from one), and its folder is writable.
pub fn can_replace(app: &Path) -> bool {
    !app.to_string_lossy().contains("/AppTranslocation/") && writable_folder(app)
}

fn writable_folder(app: &Path) -> bool {
    let folder = app.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("/"));
    let Ok(cpath) = std::ffi::CString::new(folder.as_os_str().as_encoded_bytes()) else { return false };
    unsafe { libc::access(cpath.as_ptr(), libc::W_OK) == 0 }
}

/// Downloads `release`, checks it against its SHA256SUMS, verifies the app inside ([`verify`]) and swaps it in for
/// `app`. Returns the work folder, which holds the old app until [`relaunch`] removes it. Every failure before the
/// swap leaves `app` as it was.
#[allow(clippy::too_many_arguments)]
pub fn install(
    release: &Release,
    over: &Path,
    team: &str,
    runner: &dyn ProcessRunner,
    fetch: &Fetch,
) -> Result<PathBuf, UpdateError> {
    if !can_replace(over) {
        let folder = over.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("/"));
        return Err(UpdateError::CantReplace(trimmed(&folder)));
    }
    // On the app's volume, so the swap is two renames.
    let work = item_replacement_directory(over).map_err(|_| UpdateError::CantReplace(trimmed(over)))?;
    let result = (|| {
        let unpacked = unpack(&release.zip, &release.sums, &work, runner, fetch)?;
        let new = unpacked.join("MacNeutron.app");
        verify(&new, team, &release.version, &work.join("spctl.txt"), runner, &identifiers())?;
        swap(&new, over, &work.join("old.app")).map_err(|error| UpdateError::CantReplace(error.to_string()))?;
        Ok(())
    })();
    match result {
        Ok(()) => Ok(work),
        Err(error) => {
            discard(&work, over);
            Err(error)
        }
    }
}

/// The release's installer (spec 2026-10-25-macneutron-installer-design §3.3), downloaded, checked against
/// SHA256SUMS, unpacked into a new temporary folder and verified: its `Install MacNeutron.app`, which the caller
/// opens with `--from-app`. A failure removes the folder.
pub fn installer(
    release: &Release,
    team: &str,
    runner: &dyn ProcessRunner,
    fetch: &Fetch,
) -> Result<PathBuf, UpdateError> {
    let Some(asset) = &release.installer else {
        return Err(UpdateError::Checksum);
    };
    let work = std::env::temp_dir().join(format!("{INSTALLER_FOLDER}{}", uuid_like()));
    std::fs::create_dir_all(&work).map_err(|_| UpdateError::Checksum)?;
    let result = (|| {
        let unpacked = unpack(asset, &release.sums, &work, runner, fetch)?;
        let bundle = unpacked.join("Install MacNeutron.app");
        verify(&bundle, team, &release.version, &work.join("spctl.txt"), runner, &installer_identifiers())?;
        Ok(bundle)
    })();
    match result {
        Ok(bundle) => Ok(bundle),
        Err(error) => {
            let _ = std::fs::remove_dir_all(&work);
            Err(error)
        }
    }
}

/// Every `installer` folder in `dir` removed. Install MacNeutron sweeps them once it's done (its own included, not
/// before: the app it copies is in it), and the app when opening one fails.
pub fn remove_installer_folders(dir: &Path) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name.starts_with(INSTALLER_FOLDER) {
            let _ = std::fs::remove_dir_all(entry.path());
        }
    }
}

/// Settings' Install and relaunch: the release's installer when this copy can't be replaced (a translocated one, or
/// an unwritable folder) and the release has one; otherwise [`install`], which refuses a copy it can't replace. The
/// installer is taken only when it could write `target`'s folder: a standard account's MacNeutron isn't quit for an
/// install that would fail ([`app_installer`] checks the same).
pub fn update(
    release: &Release,
    app: &Path,
    team: &str,
    target: &Path,
    runner: &dyn ProcessRunner,
    fetch: &Fetch,
) -> Result<UpdateOutcome, UpdateFailure> {
    if !can_replace(app) && release.installer.is_some() {
        let folder = target.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("/"));
        if !writable_folder(target) {
            return Err(UpdateFailure::NotWritable(trimmed(&folder)));
        }
        return installer(release, team, runner, fetch).map(UpdateOutcome::Installer).map_err(UpdateFailure::Update);
    }
    install(release, app, team, runner, fetch).map(UpdateOutcome::Swapped).map_err(UpdateFailure::Update)
}

/// [`update`]'s failures: the update's own, or the target-folder check's.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum UpdateFailure {
    Update(UpdateError),
    NotWritable(String),
}

impl fmt::Display for UpdateFailure {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            UpdateFailure::Update(error) => error.fmt(formatter),
            UpdateFailure::NotWritable(folder) => AppInstallerError::NotWritable(folder.clone()).fmt(formatter),
        }
    }
}

impl std::error::Error for UpdateFailure {}

/// `asset` and `sums` downloaded, the asset checked against its line in SHA256SUMS and unpacked with ditto into
/// `<work>/new`, which is returned.
fn unpack(asset: &str, sums: &str, work: &Path, runner: &dyn ProcessRunner, fetch: &Fetch) -> Result<PathBuf, UpdateError> {
    let zip = fetch(asset).map_err(|_| UpdateError::Checksum)?;
    let result = (|| {
        let sums_file = fetch(sums).map_err(|_| UpdateError::Checksum)?;
        let name = asset.rsplit('/').next().unwrap_or("").to_string();
        let text = std::fs::read_to_string(&sums_file).unwrap_or_default();
        let _ = std::fs::remove_file(&sums_file);
        let line = text.lines().find_map(|line| {
            let parts: Vec<&str> = line.split_whitespace().collect();
            (parts.len() == 2 && parts[1] == name).then(|| parts[0].to_string())
        });
        let size = std::fs::metadata(&zip).ok().map(|meta| meta.len() as i64);
        let (Some(digest), Some(size)) = (line, size) else { return Err(UpdateError::Checksum) };
        if !VMDownloads::matches(&zip, size, &digest) {
            return Err(UpdateError::Checksum);
        }
        let unpacked = work.join("new");
        let arguments = vec![
            "-x".to_string(),
            "-k".to_string(),
            zip.to_string_lossy().into_owned(),
            unpacked.to_string_lossy().into_owned(),
        ];
        let _ = runner.run(
            Path::new("/usr/bin/ditto"),
            &arguments,
            &BTreeMap::new(),
            Some(&work.join("ditto.txt")),
        );
        Ok(unpacked)
    })();
    let _ = std::fs::remove_file(&zip);
    result
}

/// Removes the work folder, unless the app's own path is empty: then the folder holds the only copy of the app (a
/// swap whose roll-back failed too), and it stays.
pub fn discard(work: &Path, keeping_for: &Path) {
    if std::fs::symlink_metadata(keeping_for).is_err() {
        return;
    }
    let _ = std::fs::remove_dir_all(work);
}

/// The code requirement of a bundle that is one of `identifiers`, signed by `team` with a Developer ID.
pub fn requirement(team: &str, identifiers: &[&str]) -> String {
    let either = identifiers.iter().map(|id| format!("identifier \"{id}\"")).collect::<Vec<_>>().join(" or ");
    format!("anchor apple generic and ({either}) and certificate leaf[subject.OU] = \"{team}\"")
}

/// The bundle's signature is valid (strict, every architecture, nested code), it is one of `identifiers` signed by
/// `team`, it is `version`, and Gatekeeper accepts it (Developer ID and notarization, as release.sh's R2 checks).
pub fn verify(
    bundle: &Path,
    team: &str,
    version: &str,
    log: &Path,
    runner: &dyn ProcessRunner,
    identifiers: &[&str],
) -> Result<(), UpdateError> {
    if !bundle.join("Contents/Info.plist").exists() {
        let name = bundle.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        return Err(UpdateError::Signature(format!("the download holds no {name}")));
    }
    let requirement = requirement(team, identifiers);
    let arguments = vec![
        "--verify".to_string(),
        "--strict=all".to_string(),
        "--check-notarization".to_string(),
        "-R".to_string(),
        format!("={requirement}"),
        bundle.to_string_lossy().into_owned(),
    ];
    let status = runner.run(Path::new("/usr/bin/codesign"), &arguments, &BTreeMap::new(), Some(log));
    if !matches!(status, Ok(0)) {
        let said = std::fs::read_to_string(log).unwrap_or_default().trim().to_string();
        return Err(UpdateError::Signature(if said.is_empty() { "its signature can't be read".into() } else { said }));
    }
    let found = plist_version(bundle).unwrap_or_else(|| "unknown".into());
    if found != version {
        return Err(UpdateError::Version(found));
    }
    let arguments = vec![
        "--assess".to_string(),
        "--type".to_string(),
        "exec".to_string(),
        bundle.to_string_lossy().into_owned(),
    ];
    let status = runner.run(Path::new("/usr/sbin/spctl"), &arguments, &BTreeMap::new(), Some(log)).unwrap_or(-1);
    if status != 0 {
        let said = std::fs::read_to_string(log).unwrap_or_default().trim().to_string();
        return Err(UpdateError::Gatekeeper(if said.is_empty() { format!("spctl exited {status}") } else { said }));
    }
    Ok(())
}

fn plist_version(bundle: &Path) -> Option<String> {
    let value = plist::Value::from_file(bundle.join("Contents/Info.plist")).ok()?;
    value.as_dictionary()?.get("CFBundleShortVersionString")?.as_string().map(str::to_string)
}

/// `app` moves to `aside` and `new` takes its place; if `new` can't, `app` moves back.
pub fn swap(new: &Path, app: &Path, aside: &Path) -> Result<(), AppInstallerError> {
    std::fs::rename(app, aside).map_err(|_| AppInstallerError::Leftover(trimmed(aside)))?;
    if let Err(error) = std::fs::rename(new, app) {
        let _ = std::fs::rename(aside, app);
        return Err(AppInstallerError::Leftover(error.to_string()));
    }
    Ok(())
}

/// Once this process exits, removes `work` (the old app) and opens `app`. The caller quits right after.
pub fn relaunch(app: &Path, work_path: &Path) -> std::io::Result<()> {
    let script = r#"while kill -0 "$0" 2>/dev/null; do sleep 0.2; done; rm -rf "$1"; exec /usr/bin/open "$2""#;
    std::process::Command::new("/bin/sh")
        .args(["-c", script])
        .arg(std::process::id().to_string())
        .arg(work_path)
        .arg(app)
        .spawn()
        .map(|_| ())
}

/// `X.Y.Z` or `X.Y.Zp` as numbers and the performance flag; None for anything else (a dev build's "dev").
pub fn version(text: &str) -> Option<([i64; 3], bool)> {
    let performance = text.ends_with('p');
    let body = if performance { &text[..text.len() - 1] } else { text };
    let parts: Vec<&str> = body.split('.').collect();
    if parts.len() != 3 {
        return None;
    }
    let mut numbers = [0i64; 3];
    for (index, part) in parts.iter().enumerate() {
        numbers[index] = part.parse::<i64>().ok()?;
        if numbers[index] < 0 {
            return None;
        }
    }
    Some((numbers, performance))
}

fn lexicographically_precedes(left: &[i64; 3], right: &[i64; 3]) -> bool {
    left < right
}

fn is_https(url: &str) -> bool {
    url.starts_with("https://")
}

fn trimmed(path: &Path) -> String {
    let mut text = path.to_string_lossy().into_owned();
    while text.len() > 1 && text.ends_with('/') {
        text.pop();
    }
    text
}

fn item_replacement_directory(app: &Path) -> std::io::Result<PathBuf> {
    let folder = app.parent().unwrap_or(Path::new("/"));
    let work = folder.join(format!(".MacNeutron-update-{}", uuid_like()));
    std::fs::create_dir_all(&work)?;
    Ok(work)
}

fn uuid_like() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let now = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{now}-{}", std::process::id())
}

/// Just enough of GitHub's release list for [`pick`]: a `serde_json`-shaped parse without the dependency.
fn parse_releases(json: &[u8]) -> Vec<GitHubRelease> {
    let Ok(text) = std::str::from_utf8(json) else { return Vec::new() };
    let mut parser = Json { bytes: text.as_bytes(), at: 0 };
    let Some(value) = parser.value() else { return Vec::new() };
    let Some(list) = value.as_array() else { return Vec::new() };
    list.iter()
        .filter_map(|entry| {
            let object = entry.as_object()?;
            let tag_name = object.get("tag_name")?.as_string()?.to_string();
            let draft = object.get("draft").and_then(JsonValue::as_bool).unwrap_or(false);
            let prerelease = object.get("prerelease").and_then(JsonValue::as_bool).unwrap_or(false);
            let html_url = object.get("html_url")?.as_string()?.to_string();
            let assets = object
                .get("assets")?
                .as_array()?
                .iter()
                .filter_map(|asset| {
                    let asset = asset.as_object()?;
                    Some(Asset {
                        name: asset.get("name")?.as_string()?.to_string(),
                        browser_download_url: asset.get("browser_download_url")?.as_string()?.to_string(),
                    })
                })
                .collect();
            Some(GitHubRelease { tag_name, draft, prerelease, html_url, assets })
        })
        .collect()
}

#[derive(Debug)]
struct GitHubRelease {
    tag_name: String,
    draft: bool,
    prerelease: bool,
    html_url: String,
    assets: Vec<Asset>,
}

#[derive(Debug)]
struct Asset {
    name: String,
    browser_download_url: String,
}

#[derive(Debug, Clone, PartialEq)]
enum JsonValue {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<JsonValue>),
    Object(BTreeMap<String, JsonValue>),
}

impl JsonValue {
    fn as_array(&self) -> Option<&Vec<JsonValue>> {
        match self {
            JsonValue::Array(items) => Some(items),
            _ => None,
        }
    }

    fn as_object(&self) -> Option<&BTreeMap<String, JsonValue>> {
        match self {
            JsonValue::Object(object) => Some(object),
            _ => None,
        }
    }

    fn as_string(&self) -> Option<&str> {
        match self {
            JsonValue::String(text) => Some(text),
            _ => None,
        }
    }

    fn as_bool(&self) -> Option<bool> {
        match self {
            JsonValue::Bool(value) => Some(*value),
            _ => None,
        }
    }
}

struct Json<'a> {
    bytes: &'a [u8],
    at: usize,
}

impl Json<'_> {
    fn value(&mut self) -> Option<JsonValue> {
        self.skip_whitespace();
        match self.peek()? {
            b'{' => self.object(),
            b'[' => self.array(),
            b'"' => self.string().map(JsonValue::String),
            b't' => self.literal("true", JsonValue::Bool(true)),
            b'f' => self.literal("false", JsonValue::Bool(false)),
            b'n' => self.literal("null", JsonValue::Null),
            _ => self.number(),
        }
    }

    fn object(&mut self) -> Option<JsonValue> {
        self.at += 1;
        let mut object = BTreeMap::new();
        self.skip_whitespace();
        if self.peek()? == b'}' {
            self.at += 1;
            return Some(JsonValue::Object(object));
        }
        loop {
            self.skip_whitespace();
            let key = self.string()?;
            self.skip_whitespace();
            if self.peek()? != b':' {
                return None;
            }
            self.at += 1;
            let value = self.value()?;
            object.insert(key, value);
            self.skip_whitespace();
            match self.peek()? {
                b',' => self.at += 1,
                b'}' => {
                    self.at += 1;
                    return Some(JsonValue::Object(object));
                }
                _ => return None,
            }
        }
    }

    fn array(&mut self) -> Option<JsonValue> {
        self.at += 1;
        let mut items = Vec::new();
        self.skip_whitespace();
        if self.peek()? == b']' {
            self.at += 1;
            return Some(JsonValue::Array(items));
        }
        loop {
            items.push(self.value()?);
            self.skip_whitespace();
            match self.peek()? {
                b',' => self.at += 1,
                b']' => {
                    self.at += 1;
                    return Some(JsonValue::Array(items));
                }
                _ => return None,
            }
        }
    }

    fn string(&mut self) -> Option<String> {
        if self.peek()? != b'"' {
            return None;
        }
        self.at += 1;
        let mut text = String::new();
        loop {
            let byte = self.peek()?;
            self.at += 1;
            match byte {
                b'"' => return Some(text),
                b'\\' => {
                    let escape = self.peek()?;
                    self.at += 1;
                    match escape {
                        b'"' => text.push('"'),
                        b'\\' => text.push('\\'),
                        b'/' => text.push('/'),
                        b'b' => text.push('\u{0008}'),
                        b'f' => text.push('\u{000C}'),
                        b'n' => text.push('\n'),
                        b'r' => text.push('\r'),
                        b't' => text.push('\t'),
                        b'u' => {
                            let hex = std::str::from_utf8(self.bytes.get(self.at..self.at + 4)?).ok()?;
                            let code = u32::from_str_radix(hex, 16).ok()?;
                            self.at += 4;
                            text.push(char::from_u32(code)?);
                        }
                        _ => return None,
                    }
                }
                _ => {
                    // The byte may be part of a multi-byte character; take the whole run.
                    let start = self.at - 1;
                    while self.peek().map(|b| b & 0xC0 != 0x80).unwrap_or(false) && self.peek()? != b'"' && self.peek()? != b'\\' {
                        self.at += 1;
                    }
                    text.push_str(std::str::from_utf8(self.bytes.get(start..self.at)?).ok()?);
                }
            }
        }
    }

    fn number(&mut self) -> Option<JsonValue> {
        let start = self.at;
        while self.peek().map(|b| b.is_ascii_digit() || matches!(b, b'-' | b'+' | b'.' | b'e' | b'E')).unwrap_or(false) {
            self.at += 1;
        }
        if start == self.at {
            return None;
        }
        std::str::from_utf8(self.bytes.get(start..self.at)?).ok()?.parse::<f64>().ok().map(JsonValue::Number)
    }

    fn literal(&mut self, word: &str, value: JsonValue) -> Option<JsonValue> {
        if self.bytes.get(self.at..self.at + word.len())? == word.as_bytes() {
            self.at += word.len();
            Some(value)
        } else {
            None
        }
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.at).copied()
    }

    fn skip_whitespace(&mut self) {
        while self.peek().map(|b| b.is_ascii_whitespace()).unwrap_or(false) {
            self.at += 1;
        }
    }
}

/// The default downloader: the same one the VM setup uses.
pub fn default_fetch() -> Fetch {
    standard_fetch()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_signed_wine_app, make_temp_dir, releases_json, write};
    use std::collections::HashMap;
    use std::sync::Mutex;

    #[test]
    fn pick_takes_the_newest_of_the_running_channel() {
        let assets = |version: &str| vec![format!("MacNeutron-{version}.zip"), "SHA256SUMS".to_string()];
        let owned: Vec<(String, bool, bool, Vec<String>)> = vec![
            ("v0.9.0".into(), false, false, assets("0.9.0")),
            ("v0.10.0".into(), false, false, assets("0.10.0")),
            ("v0.11.0".into(), true, false, assets("0.11.0")),
            ("v0.12.0".into(), false, true, assets("0.12.0")),
            ("v0.13.0".into(), false, false, vec!["SHA256SUMS".to_string()]),
            ("v0.10.0p".into(), false, false, assets("0.10.0p")),
            ("v0.9.1p".into(), false, false, assets("0.9.1p")),
        ];
        let refs: Vec<(&str, bool, bool, Vec<&str>)> = owned
            .iter()
            .map(|(tag, draft, pre, assets)| (tag.as_str(), *draft, *pre, assets.iter().map(String::as_str).collect()))
            .collect();
        let borrowed: Vec<(&str, bool, bool, &[&str])> =
            refs.iter().map(|(t, d, p, a)| (*t, *d, *p, a.as_slice())).collect();
        let json = releases_json(&borrowed, "https://example.invalid");
        let normal = pick(&json, "0.8.0").unwrap();
        assert_eq!(normal.version, "0.10.0");
        assert!(normal.zip.ends_with("MacNeutron-0.10.0.zip"));
        assert_eq!(normal.page, "https://example.invalid/v0.10.0");
        assert_eq!(pick(&json, "0.8.0p").unwrap().version, "0.10.0p");
        assert!(pick(&json, "0.10.0").is_none());
        assert!(pick(&json, "dev").is_none());
        assert!(pick(b"{", "0.8.0").is_none());
    }

    #[test]
    fn pick_ignores_releases_without_https_links() {
        let entries: [(&str, bool, bool, &[&str]); 1] =
            [("v0.9.0", false, false, &["MacNeutron-0.9.0.zip", "SHA256SUMS"])];
        assert!(pick(&releases_json(&entries, "https://example.invalid"), "0.8.0").is_some());
        assert!(pick(&releases_json(&entries, "file:///Applications"), "0.8.0").is_none());
        assert!(pick(&releases_json(&entries, "http://example.invalid"), "0.8.0").is_none());
    }

    #[test]
    fn a_work_folder_holding_the_only_app_is_kept() {
        let dir = make_temp_dir();
        let app = dir.join("MacNeutron.app");
        let work = dir.join("work");
        write("old", &work.join("old.app/v"), false);
        discard(&work, &app);
        assert!(work.join("old.app/v").exists());
        write("new", &app.join("v"), false);
        discard(&work, &app);
        assert!(!work.exists());
    }

    #[test]
    fn can_replace_needs_a_writable_folder_outside_translocation() {
        assert!(can_replace(&make_temp_dir().join("MacNeutron.app")));
        assert!(!can_replace(Path::new("/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app")));
    }

    #[test]
    fn swap_puts_the_new_app_in_place_and_rolls_back() {
        let dir = make_temp_dir();
        let app = dir.join("MacNeutron.app");
        let new = dir.join("new/MacNeutron.app");
        write("old", &app.join("v"), false);
        write("new", &new.join("v"), false);
        swap(&new, &app, &dir.join("old.app")).unwrap();
        assert_eq!(std::fs::read_to_string(app.join("v")).unwrap(), "new");
        assert!(swap(&dir.join("missing.app"), &app, &dir.join("old2.app")).is_err());
        assert_eq!(std::fs::read_to_string(app.join("v")).unwrap(), "new");
    }

    #[test]
    fn install_refuses_and_changes_nothing() {
        let dir = make_temp_dir();
        let app = dir.join("MacNeutron.app");
        write("old", &app.join("v"), false);
        let source = make_temp_dir();
        let bundle = source.join("MacNeutron.app");
        let loader = source.join("loader");
        write("#!/bin/sh\n", &loader, true);
        let signed = make_signed_wine_app(&source, &loader, "0.9.0", "1");
        std::fs::rename(&signed, &bundle).unwrap();
        let zip = source.join("MacNeutron-0.9.0.zip");
        let sums = source.join("SHA256SUMS");
        let status = std::process::Command::new("/usr/bin/ditto")
            .args(["-c", "-k", "--norsrc", "--keepParent"])
            .arg(&bundle)
            .arg(&zip)
            .status()
            .unwrap();
        assert!(status.success());
        let hash = crate::posix::sha256_file(&zip).unwrap();
        let release = Release {
            version: "0.9.0".into(),
            page: "https://example.invalid/v0.9.0".into(),
            zip: "https://example.invalid/v0.9.0/MacNeutron-0.9.0.zip".into(),
            sums: "https://example.invalid/v0.9.0/SHA256SUMS".into(),
            installer: None,
        };
        let files: HashMap<String, std::path::PathBuf> =
            HashMap::from([(release.zip.clone(), zip), (release.sums.clone(), sums.clone())]);
        let fetch: Fetch = std::sync::Arc::new(move |url: &str| {
            let source = files.get(url).ok_or(crate::valve_client::ValveClientError::Http(404))?;
            let copy = make_temp_dir().join(url.rsplit('/').next().unwrap_or("f"));
            std::fs::copy(source, &copy).map_err(|_| crate::valve_client::ValveClientError::Http(0))?;
            Ok(copy)
        });
        write(&format!("{}  MacNeutron-0.9.0.zip\n", "0".repeat(64)), &sums, false);
        let error = install(&release, &app, "ABCDE12345", &no_runner(), &fetch).unwrap_err();
        assert_eq!(error, UpdateError::Checksum);
        assert_eq!(std::fs::read_to_string(app.join("v")).unwrap(), "old");

        write(&format!("{hash}  MacNeutron-0.9.0.zip\n"), &sums, false);
        let error = install(&release, &app, "ABCDE12345", &no_runner(), &fetch).unwrap_err();
        assert!(matches!(error, UpdateError::Signature(_)), "got {error:?}");
        assert_eq!(std::fs::read_to_string(app.join("v")).unwrap(), "old");
    }

    fn no_runner() -> crate::process_runner::SystemProcessRunner {
        crate::process_runner::SystemProcessRunner
    }

    #[test]
    fn pick_records_the_installer_asset() {
        let with: [(&str, bool, bool, &[&str]); 1] = [(
            "v0.9.2",
            false,
            false,
            &["MacNeutron-0.9.2.zip", "SHA256SUMS", "MacNeutron-0.9.2-Installer.zip"],
        )];
        let picked = pick(&releases_json(&with, "https://example.invalid"), "0.9.1").unwrap();
        assert!(picked.installer.unwrap().ends_with("MacNeutron-0.9.2-Installer.zip"));
        let without: [(&str, bool, bool, &[&str]); 1] =
            [("v0.9.2", false, false, &["MacNeutron-0.9.2.zip", "SHA256SUMS"])];
        assert!(pick(&releases_json(&without, "https://example.invalid"), "0.9.1").unwrap().installer.is_none());
    }

    #[test]
    fn update_without_an_installer_says_where_the_app_runs() {
        let fetched = std::sync::Arc::new(Mutex::new(Vec::<String>::new()));
        let recorder = std::sync::Arc::clone(&fetched);
        let fetch: Fetch = std::sync::Arc::new(move |url: &str| {
            recorder.lock().unwrap().push(url.to_string());
            Err(crate::valve_client::ValveClientError::Http(0))
        });
        let release = Release {
            version: "0.9.2".into(),
            page: "https://example.invalid/v0.9.2".into(),
            zip: "https://example.invalid/v0.9.2/MacNeutron-0.9.2.zip".into(),
            sums: "https://example.invalid/v0.9.2/SHA256SUMS".into(),
            installer: None,
        };
        let translocated = Path::new("/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app");
        let target = Path::new("/Applications/MacNeutron.app");
        let error = update(&release, translocated, "ABCDE12345", target, &no_runner(), &fetch).unwrap_err();
        assert_eq!(
            error,
            UpdateFailure::Update(UpdateError::CantReplace("/private/var/folders/x/AppTranslocation/y/d".into()))
        );
        assert_eq!(
            UpdateError::CantReplace("/private/var/folders/x/AppTranslocation/y/d".into()).to_string(),
            "MacNeutron is running from a temporary, read-only copy macOS made because it was opened from Downloads. Download Install MacNeutron from the release page, or move MacNeutron to Applications."
        );
        assert_eq!(
            UpdateError::CantReplace("/Volumes/Games".into()).to_string(),
            "MacNeutron is running from /Volumes/Games, which it can't replace. Download Install MacNeutron from the release page, or move MacNeutron to Applications."
        );
        assert!(fetched.lock().unwrap().is_empty());
    }

    #[test]
    fn update_refuses_the_installer_when_applications_isnt_writable() {
        use std::os::unix::fs::PermissionsExt;
        let apps = make_temp_dir();
        let folder = apps.to_string_lossy().into_owned();
        let mut permissions = std::fs::metadata(&apps).unwrap().permissions();
        permissions.set_mode(0o555);
        std::fs::set_permissions(&apps, permissions).unwrap();
        let release = Release {
            version: "0.9.2".into(),
            page: "https://example.invalid/v0.9.2".into(),
            zip: "https://example.invalid/v0.9.2/MacNeutron-0.9.2.zip".into(),
            sums: "https://example.invalid/v0.9.2/SHA256SUMS".into(),
            installer: Some("https://example.invalid/v0.9.2/MacNeutron-0.9.2-Installer.zip".into()),
        };
        let fetch: Fetch = std::sync::Arc::new(|_| Err(crate::valve_client::ValveClientError::Http(0)));
        let translocated = Path::new("/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app");
        let target = apps.join("MacNeutron.app");
        let error = update(&release, translocated, "ABCDE12345", &target, &no_runner(), &fetch).unwrap_err();
        assert_eq!(error, UpdateFailure::NotWritable(folder));
        let mut back = std::fs::metadata(&apps).unwrap().permissions();
        back.set_mode(0o755);
        std::fs::set_permissions(&apps, back).unwrap();
    }

    #[test]
    fn installer_folders_are_removed_and_nothing_else() {
        let tmp = make_temp_dir();
        write("x", &tmp.join("MacNeutron-installer-A/new/Install MacNeutron.app/Contents/v"), false);
        write("x", &tmp.join("MacNeutron-installer-B/spctl.txt"), false);
        write("x", &tmp.join("MacNeutron-other/v"), false);
        write("x", &tmp.join("install-macneutron-spctl.txt"), false);
        remove_installer_folders(&tmp);
        let mut names: Vec<String> = std::fs::read_dir(&tmp)
            .unwrap()
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        names.sort();
        assert_eq!(names, vec!["MacNeutron-other", "install-macneutron-spctl.txt"]);
    }

    #[test]
    fn update_from_a_translocated_app_takes_the_installer() {
        let source = make_temp_dir();
        let bundle = source.join("Install MacNeutron.app");
        let loader = source.join("loader");
        write("#!/bin/sh\n", &loader, true);
        let signed = make_signed_wine_app(&source, &loader, "0.9.2", "1");
        std::fs::rename(&signed, &bundle).unwrap();
        let zip = source.join("MacNeutron-0.9.2-Installer.zip");
        let sums = source.join("SHA256SUMS");
        let status = std::process::Command::new("/usr/bin/ditto")
            .args(["-c", "-k", "--norsrc", "--keepParent"])
            .arg(&bundle)
            .arg(&zip)
            .status()
            .unwrap();
        assert!(status.success());
        let hash = crate::posix::sha256_file(&zip).unwrap();
        write(&format!("{hash}  MacNeutron-0.9.2-Installer.zip\n"), &sums, false);
        let release = Release {
            version: "0.9.2".into(),
            page: "https://example.invalid/v0.9.2".into(),
            zip: "https://example.invalid/v0.9.2/MacNeutron-0.9.2.zip".into(),
            sums: "https://example.invalid/v0.9.2/SHA256SUMS".into(),
            installer: Some("https://example.invalid/v0.9.2/MacNeutron-0.9.2-Installer.zip".into()),
        };
        let fetched = std::sync::Arc::new(Mutex::new(Vec::<String>::new()));
        let recorder = std::sync::Arc::clone(&fetched);
        let zip_path = zip.clone();
        let sums_path = sums.clone();
        let fetch: Fetch = std::sync::Arc::new(move |url: &str| {
            recorder.lock().unwrap().push(url.to_string());
            let source = if url.ends_with("SHA256SUMS") { &sums_path } else { &zip_path };
            let copy = make_temp_dir().join(url.rsplit('/').next().unwrap_or("f"));
            std::fs::copy(source, &copy).map_err(|_| crate::valve_client::ValveClientError::Http(0))?;
            Ok(copy)
        });
        let translocated = Path::new("/private/var/folders/x/AppTranslocation/y/d/MacNeutron.app");
        let target = writable_target();
        let error = update(&release, translocated, "ABCDE12345", &target, &no_runner(), &fetch).unwrap_err();
        let UpdateFailure::Update(UpdateError::Signature(detail)) = error else {
            panic!("expected a signature refusal, got {error:?}");
        };
        assert_ne!(detail, "the download holds no Install MacNeutron.app");
        assert_eq!(*fetched.lock().unwrap(), vec![release.installer.clone().unwrap(), release.sums.clone()]);
    }

    fn writable_target() -> PathBuf {
        make_temp_dir().join("MacNeutron.app")
    }

    #[test]
    fn pick_takes_the_mac_os_asset_name() {
        let entry: [(&str, bool, bool, &[&str]); 1] =
            [("v0.9.8", false, false, &["MacNeutron-0.9.8-macOS.zip", "SHA256SUMS"])];
        let picked = pick(&releases_json(&entry, "https://example.invalid"), "0.9.7").unwrap();
        assert!(picked.zip.ends_with("MacNeutron-0.9.8-macOS.zip"));
    }

    #[test]
    fn updates_may_carry_either_bundle_id() {
        assert_eq!(identifiers(), ["io.github.chadouming.MacNeutron", "io.github.mac-neutron.MacNeutron"]);
        assert_eq!(
            installer_identifiers(),
            ["io.github.chadouming.MacNeutron.Installer", "io.github.mac-neutron.MacNeutron.Installer"]
        );
    }
}