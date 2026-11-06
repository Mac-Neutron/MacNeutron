//! Paths inside the native macOS Steam installation.

use std::collections::BTreeSet;
use std::fmt;
use std::path::{Path, PathBuf};

use crate::key_values::node_at;
use crate::KeyValues;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SteamLocation {
    pub root: PathBuf,
}

impl Default for SteamLocation {
    fn default() -> Self {
        Self::new(home().join("Library/Application Support/Steam"))
    }
}

impl SteamLocation {
    pub fn new(root: PathBuf) -> Self {
        SteamLocation { root }
    }

    pub fn bundle_macos(&self) -> PathBuf {
        self.root.join("Steam.AppBundle/Steam/Contents/MacOS")
    }

    pub fn bundle_compat_tools(&self) -> PathBuf {
        self.bundle_macos().join("compatibilitytools.d")
    }

    pub fn steam_dev_config(&self) -> PathBuf {
        self.bundle_macos().join("steam_dev.cfg")
    }

    pub fn config_vdf(&self) -> PathBuf {
        self.root.join("config/config.vdf")
    }

    pub fn compat_log(&self) -> PathBuf {
        self.root.join("logs/compat_log.txt")
    }

    pub fn app_info(&self) -> PathBuf {
        self.root.join("appcache/appinfo.vdf")
    }

    /// Steam's own settings; it also holds the login name (AutoLoginUser), which is never read.
    pub fn registry_vdf(&self) -> PathBuf {
        self.root.join("registry.vdf")
    }

    pub fn is_installed(&self) -> bool {
        self.bundle_macos().exists()
    }

    /// Every library's `steamapps` folder, the Steam root's own first, from `libraryfolders.vdf`.
    pub fn libraries(&self) -> Vec<PathBuf> {
        let own = self.root.join("steamapps");
        let mut result = vec![own.clone()];
        let file = own.join("libraryfolders.vdf");
        let Ok(text) = std::fs::read_to_string(&file) else { return result };
        let Ok(nodes) = KeyValues::parse(&text) else { return result };
        let Some(entries) = node_at(&nodes, &["libraryfolders"]) else { return result };
        for entry in entries.children() {
            let Some(path) = node_at(entry.children(), &["path"]).and_then(|n| n.string_value()) else {
                continue;
            };
            let steamapps = PathBuf::from(path).join("steamapps");
            if !result.iter().any(|p| same_path(p, &steamapps)) {
                result.push(steamapps);
            }
        }
        result
    }

    /// App IDs with an `appmanifest_<id>.acf` in any library.
    pub fn installed_app_ids(&self) -> BTreeSet<u32> {
        let mut ids = BTreeSet::new();
        for library in self.libraries() {
            let Ok(entries) = std::fs::read_dir(&library) else { continue };
            for entry in entries.flatten() {
                let Ok(name) = entry.file_name().into_string() else { continue };
                if let Some(rest) = name.strip_prefix("appmanifest_").and_then(|n| n.strip_suffix(".acf")) {
                    if let Ok(id) = rest.parse::<u32>() {
                        ids.insert(id);
                    }
                }
            }
        }
        ids
    }

    /// The app IDs in every library's `apps` list in steamapps/libraryfolders.vdf, a library on a disconnected drive
    /// included (its manifests can't be read, its list still names its games). None when the file can't be read or
    /// parsed (Steam rewriting it).
    pub fn listed_app_ids(&self) -> Option<BTreeSet<u32>> {
        let file = self.root.join("steamapps/libraryfolders.vdf");
        let text = std::fs::read_to_string(&file).ok()?;
        let nodes = KeyValues::parse(&text).ok()?;
        let folders = node_at(&nodes, &["libraryfolders"])?;
        let mut ids = BTreeSet::new();
        for folder in folders.children() {
            let Some(apps) = node_at(folder.children(), &["apps"]) else { continue };
            for app in apps.children() {
                if let Ok(id) = KeyValues::unescape(&app.key).parse::<u32>() {
                    ids.insert(id);
                }
            }
        }
        Some(ids)
    }

    /// Steam's UI language ("english"), which the Windows client keeps in HKCU\Software\Valve\Steam\language.
    pub fn ui_language(&self) -> Option<String> {
        let text = std::fs::read_to_string(self.registry_vdf()).ok()?;
        let nodes = KeyValues::parse(&text).ok()?;
        node_at(&nodes, &["Registry", "HKCU", "Software", "Valve", "Steam", "language"])
            .and_then(|n| n.string_value())
    }

    /// The `localconfig.vdf` of the account Steam logged in last (`loginusers.vdf`'s `MostRecent`), else of the only
    /// account in `userdata`; nil otherwise. The account ID makes the path and nothing else: never log or show it.
    /// Only `MostRecent` is read from `loginusers.vdf`, which also holds account names.
    pub fn local_config(&self) -> Option<PathBuf> {
        let userdata = self.root.join("userdata");
        let account = self
            .most_recent_account(&userdata)
            .or_else(|| self.only_account(&userdata));
        account.map(|acct| userdata.join(format!("{acct}/config/localconfig.vdf")))
    }

    fn most_recent_account(&self, _userdata: &Path) -> Option<String> {
        let text = std::fs::read_to_string(self.root.join("config/loginusers.vdf")).ok()?;
        let nodes = KeyValues::parse(&text).ok()?;
        let users = node_at(&nodes, &["users"])?;
        let recent = users.children().iter().find(|u| {
            node_at(u.children(), &["MostRecent"]).and_then(|n| n.string_value()).as_deref() == Some("1")
        })?;
        let id = KeyValues::unescape(&recent.key).parse::<u64>().ok()?;
        Some((id & 0xFFFF_FFFF).to_string())
    }

    fn only_account(&self, userdata: &Path) -> Option<String> {
        let entries = std::fs::read_dir(userdata).ok()?;
        let folders: Vec<String> = entries
            .flatten()
            .filter_map(|e| e.file_name().into_string().ok())
            .filter(|n| n.parse::<u32>().is_ok())
            .collect();
        if folders.len() == 1 {
            Some(folders[0].clone())
        } else {
            None
        }
    }
}

fn same_path(a: &Path, b: &Path) -> bool {
    fn normal(path: &Path) -> String {
        let resolved = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        let mut s = resolved.to_string_lossy().into_owned();
        while s.len() > 1 && s.ends_with('/') {
            s.pop();
        }
        s
    }
    normal(a) == normal(b)
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SteamPlayError {
    SteamNotInstalled,
    SteamRunning,
    QuitTimedOut,
    ConfigUnreadable(String),
    VerificationFailed(String),
    PlanDropsMacGames(i64),
}

impl fmt::Display for SteamPlayError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SteamPlayError::SteamNotInstalled => {
                write!(f, "Steam isn't installed in ~/Library/Application Support/Steam.")
            }
            SteamPlayError::SteamRunning => write!(f, "Steam is running. Quit Steam to apply changes."),
            SteamPlayError::QuitTimedOut => {
                write!(f, "Steam didn't quit within 30 seconds. Quit it yourself, then try again.")
            }
            SteamPlayError::ConfigUnreadable(detail) => write!(
                f,
                "Steam's settings file couldn't be read, so MacNeutron didn't change it ({detail})."
            ),
            SteamPlayError::VerificationFailed(problem) => write!(
                f,
                "Steam Play mode didn't start correctly, so it was turned off again: {problem}"
            ),
            SteamPlayError::PlanDropsMacGames(count) => write!(
                f,
                "MacNeutron didn't update Steam: the new plan would stop protecting {count} Mac {}. Steam's app list may be unreadable right now.",
                if *count == 1 { "game" } else { "games" }
            ),
        }
    }
}

impl std::error::Error for SteamPlayError {}

/// Starting and stopping Steam. A trait so tests never touch the real client.
pub trait SteamControlling: Send + Sync {
    fn is_running(&self) -> bool;
    /// Asks Steam to exit through its own URL handler, then waits for the process to go away.
    fn quit(&self, timeout: std::time::Duration) -> Result<(), SteamPlayError>;
    fn launch(&self) -> Result<(), SteamPlayError>;
}

pub struct SteamProcess;

impl Default for SteamProcess {
    fn default() -> Self {
        SteamProcess
    }
}

impl SteamProcess {
    pub fn new() -> Self {
        SteamProcess
    }
}

impl SteamControlling for SteamProcess {
    fn is_running(&self) -> bool {
        std::process::Command::new("/usr/bin/pgrep")
            .args(["-x", "steam_osx"])
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
    }

    fn quit(&self, timeout: std::time::Duration) -> Result<(), SteamPlayError> {
        if !self.is_running() {
            return Ok(());
        }
        let _ = std::process::Command::new("/usr/bin/open").arg("steam://exit").status();
        let deadline = std::time::Instant::now() + timeout;
        while self.is_running() {
            if std::time::Instant::now() >= deadline {
                return Err(SteamPlayError::QuitTimedOut);
            }
            std::thread::sleep(std::time::Duration::from_millis(500));
        }
        Ok(())
    }

    fn launch(&self) -> Result<(), SteamPlayError> {
        let _ = std::process::Command::new("/usr/bin/open").args(["-a", "Steam"]).status();
        Ok(())
    }
}
