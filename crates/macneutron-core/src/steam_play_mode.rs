//! Turns Steam Play mode on and off and keeps Steam's mappings current (spec §4).

use std::collections::{BTreeMap, BTreeSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crate::compat_context::CompatContext;
use crate::file_lock::with_file_lock;
use crate::key_values::{node_at, set_block_at, KVNode};
use crate::launcher_log::iso8601_from_unix;
use crate::mapping_planner::{MappingPlanner, ToolMapping};
use crate::paths::MacNeutronPaths;
use crate::posix;
use crate::profile_links::ProfileLinks;
use crate::runtime_installer::{write_atomic, RuntimeInstaller};
use crate::steam_location::{SteamControlling, SteamLocation, SteamPlayError, SteamProcess};
use crate::KeyValues;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SteamPlayStatus {
    Off,
    On,
    RestartNeeded(usize),
    Lost,
    /// Something MacNeutron needs can't be read; the message says what.
    Problem(String),
}

/// Turns Steam Play mode on and off and keeps Steam's mappings current (spec §4).
/// Invariant: `steam_dev.cfg` never exists unless both tools resolve through Steam's bundle and every
/// Mac game is mapped. Writes happen in the order that keeps it true even when interrupted.
#[derive(Clone)]
pub struct SteamPlayMode {
    pub steam: SteamLocation,
    pub tools: PathBuf,
    pub backups: PathBuf,
    pub intent_file: PathBuf,
    pub process: Arc<dyn SteamControlling>,
    pub verify_timeout: Duration,
    pub quit_timeout: Duration,
    /// The Mac user Steam runs as: Wine names each prefix's profile after `$USER`.
    pub profile_user: String,
}

impl Default for SteamPlayMode {
    fn default() -> Self {
        Self::new(SteamLocation::default(), &MacNeutronPaths::root(), Arc::new(SteamProcess))
    }
}

impl SteamPlayMode {
    /// The runtime tool's folder and bundle link: the Steam tool name changed (spec D1), the folder didn't.
    pub const RUNTIME_FOLDER_NAME: &'static str = "macneutron";
    pub const NATIVE_TOOL_NAME: &'static str = MappingPlanner::NATIVE_TOOL;
    pub const DEV_CONFIG: &'static str = "@sSteamCmdForcePlatformType linux\n";
    const MAPPING_PATH: [&'static str; 5] = ["InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"];

    pub fn new(steam: SteamLocation, root: &Path, process: Arc<dyn SteamControlling>) -> Self {
        SteamPlayMode {
            steam,
            tools: root.join("compatibilitytools.d"),
            backups: root.join("backups"),
            intent_file: root.join("steam-play-enabled"),
            process,
            verify_timeout: Duration::from_secs(60),
            quit_timeout: Duration::from_secs(30),
            profile_user: posix::user_name(),
        }
    }

    pub(crate) fn runtime_tool(&self) -> PathBuf {
        self.tools.join(Self::RUNTIME_FOLDER_NAME)
    }

    pub(crate) fn native_tool(&self) -> PathBuf {
        self.tools.join(Self::NATIVE_TOOL_NAME)
    }

    pub(crate) fn link(&self, name: &str) -> PathBuf {
        self.steam.bundle_compat_tools().join(name)
    }

    /// The user turned Steam Play mode on and hasn't turned it off.
    pub fn is_wanted(&self) -> bool {
        self.intent_file.exists()
    }

    /// Everything `enable` writes is still in place (a Steam update can wipe its bundle).
    pub fn files_intact(&self) -> bool {
        self.steam.steam_dev_config().exists()
            && self.link(Self::RUNTIME_FOLDER_NAME).join("toolmanifest.vdf").exists()
            && self.link(Self::NATIVE_TOOL_NAME).join("toolmanifest.vdf").exists()
    }

    pub fn status(&self, plan: &BTreeMap<String, ToolMapping>) -> SteamPlayStatus {
        if !self.is_wanted() {
            return SteamPlayStatus::Off;
        }
        if !self.files_intact() {
            return SteamPlayStatus::Lost;
        }
        if self.process.is_running() {
            if let Ok(log) = std::fs::read_to_string(self.steam.compat_log()) {
                if Self::verify(Self::last_session(&log)).is_some() {
                    return SteamPlayStatus::Lost;
                }
            }
        }
        match self.pending_changes(plan) {
            Ok(0) => SteamPlayStatus::On,
            Ok(pending) => SteamPlayStatus::RestartNeeded(pending),
            Err(error) => SteamPlayStatus::Problem(error.to_string()),
        }
    }

    // MARK: Flows

    pub fn enable(&self, plan: &BTreeMap<String, ToolMapping>) -> Result<(), SteamPlayError> {
        self.enable_with(|| Ok(plan.clone()))
    }

    /// Builds the plan only once Steam has quit: Steam rewrites its app list on exit.
    pub fn enable_with(
        &self,
        make_plan: impl FnOnce() -> Result<BTreeMap<String, ToolMapping>, SteamPlayError>,
    ) -> Result<(), SteamPlayError> {
        if !self.steam.is_installed() {
            return Err(SteamPlayError::SteamNotInstalled);
        }
        self.read_config()?; // before touching Steam: an unreadable file must leave it running
        let was_running = self.process.is_running();
        self.process.quit(self.quit_timeout)?;
        let prepared = (|| -> Result<_, SteamPlayError> {
            let plan = make_plan()?;
            self.read_config()?; // Steam may have rewritten it on exit
            let backup = self.backup_config()?;
            self.install_native_tool()?;
            self.link_tools()?;
            let migrating = self.has_legacy_mappings()?;
            self.apply_mappings(&plan)?;
            write(Self::DEV_CONFIG, &self.steam.steam_dev_config())?; // last
            Ok((backup, migrating))
        })();
        let (backup, migrating) = match prepared {
            Ok(prepared) => prepared,
            Err(error) => {
                let _ = std::fs::remove_file(self.steam.steam_dev_config());
                self.unlink_tools();
                if was_running {
                    let _ = self.process.launch(); // back in Mac mode, as the user had it
                }
                return Err(error);
            }
        };
        // Recorded before Steam starts in Linux mode, so a crash during verification can't hide that it's on.
        write("", &self.intent_file)?;
        let log_before = std::fs::read(self.steam.compat_log()).unwrap_or_default();
        self.process.launch()?;
        if let Some(problem) = self.wait_for_verification(log_before.len(), &log_before[..log_before.len().min(256)]) {
            self.roll_back(backup.as_deref());
            return Err(SteamPlayError::VerificationFailed(problem));
        }
        self.finish_rename(migrating); // after verification: a rollback puts the legacy mappings back
        Ok(())
    }

    pub fn disable(&self) -> Result<(), SteamPlayError> {
        self.process.quit(self.quit_timeout)?;
        let _ = std::fs::remove_file(self.steam.steam_dev_config()); // first
        self.unlink_tools();
        self.apply_mappings(&BTreeMap::new())?;
        let _ = std::fs::remove_file(&self.intent_file);
        self.process.launch()
    }

    /// Brings `config.vdf` up to date with `plan`. Call only while Steam is closed.
    /// Refuses any plan that would drop a Mac game's `macneutron-native` mapping: with Steam in Linux mode,
    /// that game's files would be deleted at the next start. Apps never legitimately leave the plan.
    pub fn sync(&self, plan: &BTreeMap<String, ToolMapping>) -> Result<bool, SteamPlayError> {
        if !self.is_wanted() || !self.files_intact() || self.pending_changes(plan)? == 0 {
            return Ok(false);
        }
        let dropped = self
            .current_mappings()?
            .iter()
            .filter(|(app, mapping)| mapping.tool == Self::NATIVE_TOOL_NAME && !plan.contains_key(*app))
            .count();
        if dropped > 0 {
            return Err(SteamPlayError::PlanDropsMacGames(dropped as i64));
        }
        let migrating = self.has_legacy_mappings()?;
        self.apply_mappings(plan)?;
        self.finish_rename(migrating);
        Ok(true)
    }

    pub(crate) fn has_legacy_mappings(&self) -> Result<bool, SteamPlayError> {
        Ok(self.current_mappings()?.values().any(|m| m.tool == MappingPlanner::LEGACY_RUNTIME_TOOL))
    }

    /// After `apply_mappings` moved every mapping off the runtime's pre-rename name, Steam no longer needs that name.
    /// Best effort: a vdf that keeps it costs one more entry in Steam's tool list, never a mapping to a missing tool.
    pub(crate) fn finish_rename(&self, was_migrating: bool) {
        if was_migrating {
            self.link_profiles();
        }
        if !RuntimeInstaller::registers_legacy_name(&self.runtime_tool()) || self.has_legacy_mappings().ok() != Some(false) {
            return;
        }
        let _ = RuntimeInstaller::write_compatibility_tool(&self.runtime_tool(), false);
    }

    /// Spec D3: while Steam is closed and the mappings move, every prefix with a Wine profile gets its Steam Cloud links,
    /// so Steam's first resolved pass finds the Mac's saves where Proton prefixes keep them. Only adds links (merging
    /// is the launch's job); a prefix skipped here gets them at its next launch.
    pub(crate) fn link_profiles(&self) {
        for library in self.steam.libraries() {
            let compatdata = library.join("compatdata");
            let Ok(entries) = std::fs::read_dir(&compatdata) else { continue };
            for entry in entries.flatten() {
                let path = compatdata.join(entry.file_name());
                let environment =
                    BTreeMap::from([("STEAM_COMPAT_DATA_PATH".to_string(), path.to_string_lossy().into_owned())]);
                let Ok(context) = CompatContext::new(&environment) else { continue };
                // Another tool's prefix has no profile of this name: it gets no lock file either.
                let profile = context.prefix().join(format!("drive_c/users/{}", self.profile_user));
                if !profile.is_dir() {
                    continue;
                }
                let _ = with_file_lock(&context.lock_file(), || {
                    ProfileLinks::apply(&context.prefix(), &self.profile_user, false)
                });
            }
        }
    }

    // MARK: Building blocks

    pub fn pending_changes(&self, plan: &BTreeMap<String, ToolMapping>) -> Result<usize, SteamPlayError> {
        let block = self.mapping_block()?;
        let current = MappingPlanner::current(&block);
        let claimed = MappingPlanner::claimed_by_other_tools(&block);
        let keys: BTreeSet<&String> = current.keys().chain(plan.keys()).filter(|k| !claimed.contains(*k)).collect();
        Ok(keys.into_iter().filter(|k| current.get(*k) != plan.get(*k)).count())
    }

    pub fn current_mappings(&self) -> Result<BTreeMap<String, ToolMapping>, SteamPlayError> {
        Ok(MappingPlanner::current(&self.mapping_block()?))
    }

    fn mapping_block(&self) -> Result<Vec<KVNode>, SteamPlayError> {
        Ok(node_at(&self.read_config()?, &Self::MAPPING_PATH).map(|n| n.children().to_vec()).unwrap_or_default())
    }

    /// Replaces MacNeutron's entries in `CompatToolMapping` with `plan`: backup, edit, re-parse check, atomic write.
    pub fn apply_mappings(&self, plan: &BTreeMap<String, ToolMapping>) -> Result<(), SteamPlayError> {
        if self.process.is_running() {
            return Err(SteamPlayError::SteamRunning);
        }
        let mut nodes = self.read_config()?;
        let existing = node_at(&nodes, &Self::MAPPING_PATH).map(|n| n.children().to_vec()).unwrap_or_default();
        set_block_at(&mut nodes, &Self::MAPPING_PATH, MappingPlanner::merged(&existing, plan));
        let text = KeyValues::serialize(&nodes);
        if KeyValues::parse(&text).ok().as_ref() != Some(&nodes) {
            return Err(SteamPlayError::ConfigUnreadable("the edited file didn't read back identically".into()));
        }
        self.backup_config()?;
        write(&text, &self.steam.config_vdf())?;
        Ok(())
    }

    pub(crate) fn read_config(&self) -> Result<Vec<KVNode>, SteamPlayError> {
        let file = self.steam.config_vdf();
        if !file.exists() {
            return Ok(Vec::new());
        }
        let text = std::fs::read_to_string(&file).map_err(|e| SteamPlayError::ConfigUnreadable(e.to_string()))?;
        KeyValues::parse(&text).map_err(|e| SteamPlayError::ConfigUnreadable(e.to_string()))
    }

    /// Copies `config.vdf` to `backups/config-<timestamp>.vdf`, keeping the newest 10.
    pub(crate) fn backup_config(&self) -> Result<Option<PathBuf>, SteamPlayError> {
        let config = self.steam.config_vdf();
        if !config.exists() {
            return Ok(None);
        }
        std::fs::create_dir_all(&self.backups)?;
        let backup = self.backups.join(format!("config-{}.vdf", backup_stamp()));
        // ponytail: overwrites a same-millisecond backup (Swift's copyItem would throw); both hold the same bytes.
        std::fs::copy(&config, &backup)?;
        let mut all: Vec<String> = std::fs::read_dir(&self.backups)
            .map(|entries| {
                entries
                    .flatten()
                    .filter_map(|e| e.file_name().into_string().ok())
                    .filter(|n| n.starts_with("config-"))
                    .collect()
            })
            .unwrap_or_default();
        all.sort();
        for old in &all[..all.len().saturating_sub(10)] {
            let _ = posix::remove_item(&self.backups.join(old));
        }
        Ok(Some(backup))
    }

    /// The Mac-game tool: a copy of the runtime tool's CLI, run as `macneutron passthrough %verb%` (R0b: Steam
    /// launches a thin arm64 tool binary). The runtime install writes that CLI first.
    pub fn install_native_tool(&self) -> Result<(), SteamPlayError> {
        let native = self.native_tool();
        RuntimeInstaller::install_file(&self.runtime_tool().join("bin/macneutron"), &native.join("bin/macneutron"))?; // before the manifest
        write(
            &format!(
                "\"compatibilitytools\"\n{{\n  \"compat_tools\"\n  {{\n    \"{}\"\n    {{\n      \"install_path\" \".\"\n      \"display_name\" \"macOS native\"\n      \"from_oslist\"  \"macos\"\n      \"to_oslist\"    \"linux\"\n    }}\n  }}\n}}\n",
                Self::NATIVE_TOOL_NAME
            ),
            &native.join("compatibilitytool.vdf"),
        )?;
        write(
            "\"manifest\"\n{\n  \"version\" \"2\"\n  \"commandline\" \"/bin/macneutron passthrough %verb%\"\n}\n",
            &native.join("toolmanifest.vdf"),
        )?;
        let _ = posix::remove_item(&native.join("passthrough.sh"));
        Ok(())
    }

    pub(crate) fn link_tools(&self) -> Result<(), SteamPlayError> {
        std::fs::create_dir_all(self.steam.bundle_compat_tools())?;
        for (name, target) in [(Self::RUNTIME_FOLDER_NAME, self.runtime_tool()), (Self::NATIVE_TOOL_NAME, self.native_tool())] {
            let link = self.link(name);
            let _ = posix::remove_item(&link);
            std::os::unix::fs::symlink(&target, &link)?;
        }
        Ok(())
    }

    pub(crate) fn unlink_tools(&self) {
        for name in [Self::RUNTIME_FOLDER_NAME, Self::NATIVE_TOOL_NAME] {
            let _ = posix::remove_item(&self.link(name));
        }
    }

    fn roll_back(&self, backup: Option<&Path>) {
        let _ = std::fs::remove_file(self.steam.steam_dev_config()); // first
        let _ = std::fs::remove_file(&self.intent_file);
        let _ = self.process.quit(self.quit_timeout);
        self.unlink_tools();
        if let Some(backup) = backup {
            let _ = posix::remove_item(&self.steam.config_vdf());
            let _ = std::fs::copy(backup, self.steam.config_vdf());
        }
        let _ = self.process.launch();
    }

    fn wait_for_verification(&self, size: usize, head: &[u8]) -> Option<String> {
        let deadline = Instant::now() + self.verify_timeout;
        let mut problem = Some("Steam didn't write its compatibility log".to_string());
        loop {
            if let Ok(data) = std::fs::read(self.steam.compat_log()) {
                // Steam appends each session; a file that no longer begins as it did (the first line has a
                // timestamp) was started over, so all of it is the new session.
                let session = if data.len() >= size && data.starts_with(head) { &data[size..] } else { &data[..] };
                if !session.is_empty() {
                    match Self::verify(&String::from_utf8_lossy(session)) {
                        None => return None,
                        found => problem = found,
                    }
                }
            }
            std::thread::sleep(Duration::from_secs(1));
            if Instant::now() >= deadline {
                return problem;
            }
        }
    }

    /// None when a Steam session's compat log shows both tools registered in Linux mode; otherwise the problem.
    pub(crate) fn verify(log: &str) -> Option<String> {
        if log.contains("Ignoring tool macneutron") {
            return Some("Steam started as a Mac client and ignored MacNeutron".into());
        }
        // Either name: a Steam session started before the rename registered only the old one.
        let runtime = [MappingPlanner::RUNTIME_TOOL, MappingPlanner::LEGACY_RUNTIME_TOOL]
            .iter()
            .any(|tool| log.contains(&format!("Registering tool {tool},")));
        if !runtime || !log.contains(&format!("Registering tool {},", Self::NATIVE_TOOL_NAME)) {
            return Some("Steam didn't find MacNeutron's tools".into());
        }
        if !log.contains("Recording non-user mapping") {
            return Some("Steam didn't switch to Steam Play mode".into());
        }
        None
    }

    /// The text from the last "Client version:" line on: the current Steam session.
    pub(crate) fn last_session(log: &str) -> &str {
        log.rfind("Client version:").map_or(log, |start| &log[start..])
    }
}

/// `Date()` as Swift's `.iso8601` year/month/day/time with milliseconds (UTC, no zone), colons made dashes:
/// `2026-11-06T04-57-24.855`.
fn backup_stamp() -> String {
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default();
    let seconds = iso8601_from_unix(now.as_secs() as i64);
    format!("{}.{:03}", seconds.trim_end_matches('Z'), now.subsec_millis()).replace(':', "-")
}

/// Swift's `write(atomically: true)`, parents created.
fn write(text: &str, to: &Path) -> std::io::Result<()> {
    write_atomic(to, text.as_bytes())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_fake_steam, write, FakeSteam, MAC_MODE_SESSION, OK_SESSION, STEAM_CONFIG_FIXTURE};

    const BROKEN_CONFIG: &str = "\"InstallConfigStore\"\n{\n";

    fn make_mode(session: &str, config: &str, running: bool) -> (SteamPlayMode, Arc<FakeSteam>) {
        let (steam, root) = make_fake_steam(config);
        let fake = Arc::new(FakeSteam::new(steam.clone(), running, session, Some(root.join("steam-play-enabled")), false));
        let mut mode = SteamPlayMode::new(steam, &root, fake.clone());
        mode.verify_timeout = Duration::from_secs(2);
        (mode, fake)
    }

    fn ok_mode() -> (SteamPlayMode, Arc<FakeSteam>) {
        make_mode(OK_SESSION, STEAM_CONFIG_FIXTURE, false)
    }

    fn sample_plan() -> BTreeMap<String, ToolMapping> {
        BTreeMap::from([
            ("0".to_string(), ToolMapping::new("macneutron", 75)),
            ("1062090".to_string(), ToolMapping::new("macneutron-native", 250)),
            ("2977660".to_string(), ToolMapping::new("macneutron", 250)),
        ])
    }

    fn read(path: &Path) -> String {
        std::fs::read_to_string(path).unwrap()
    }

    #[test]
    fn enable_writes_everything_and_verifies() {
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        assert!(mode.is_wanted());
        assert!(mode.files_intact());
        assert_eq!(mode.current_mappings().unwrap(), sample_plan());
        assert_eq!(fake.launches_with_dev_config(), [true]);
        assert_eq!(mode.status(&sample_plan()), SteamPlayStatus::On);
        assert_eq!(read(&mode.steam.steam_dev_config()), "@sSteamCmdForcePlatformType linux\n");
    }

    #[test]
    fn failed_verification_rolls_back_with_dev_config_removed_first() {
        let (mode, fake) = make_mode(MAC_MODE_SESSION, STEAM_CONFIG_FIXTURE, false);
        let original = read(&mode.steam.config_vdf());
        assert_eq!(
            mode.enable(&sample_plan()),
            Err(SteamPlayError::VerificationFailed("Steam started as a Mac client and ignored MacNeutron".into()))
        );
        assert!(!mode.steam.steam_dev_config().exists());
        assert!(!mode.link("macneutron").exists());
        assert_eq!(read(&mode.steam.config_vdf()), original);
        assert!(!mode.is_wanted());
        assert_eq!(fake.launches_with_dev_config(), [true, false]);
    }

    #[test]
    fn unreadable_config_changes_nothing() {
        let (mode, fake) = make_mode(OK_SESSION, BROKEN_CONFIG, false);
        assert!(matches!(mode.enable(&sample_plan()), Err(SteamPlayError::ConfigUnreadable(_))));
        assert!(!mode.steam.steam_dev_config().exists());
        assert!(!mode.link("macneutron").exists());
        assert!(fake.launches_with_dev_config().is_empty());
    }

    #[test]
    fn mappings_are_never_written_while_steam_runs() {
        let (mode, _) = make_mode(OK_SESSION, STEAM_CONFIG_FIXTURE, true);
        assert_eq!(mode.apply_mappings(&sample_plan()), Err(SteamPlayError::SteamRunning));
    }

    #[test]
    fn every_write_is_backed_up_first() {
        let (mode, _) = ok_mode();
        let original = std::fs::read(mode.steam.config_vdf()).unwrap();
        mode.apply_mappings(&sample_plan()).unwrap();
        let backups: Vec<PathBuf> = std::fs::read_dir(&mode.backups).unwrap().map(|e| e.unwrap().path()).collect();
        assert_eq!(backups.len(), 1);
        assert_eq!(std::fs::read(&backups[0]).unwrap(), original);
        assert!(read(&mode.steam.config_vdf()).contains("\"proton_9\""));
    }

    #[test]
    fn sync_only_writes_when_the_plan_changed() {
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        fake.quit(Duration::from_secs(1)).unwrap();
        assert_eq!(mode.sync(&sample_plan()), Ok(false));
        let mut new_plan = sample_plan();
        new_plan.insert("3419430".into(), ToolMapping::new("macneutron-native", 250));
        assert_eq!(mode.sync(&new_plan), Ok(true));
        assert_eq!(mode.current_mappings().unwrap(), new_plan);
    }

    #[test]
    fn status_shows_pending_changes_and_lost_files() {
        let (mode, _) = ok_mode();
        assert_eq!(mode.status(&sample_plan()), SteamPlayStatus::Off);
        mode.enable(&sample_plan()).unwrap();
        let mut new_plan = sample_plan();
        new_plan.insert("1".into(), ToolMapping::new("macneutron", 250));
        new_plan.remove("2977660");
        assert_eq!(mode.status(&new_plan), SteamPlayStatus::RestartNeeded(2));
        std::fs::remove_file(mode.steam.steam_dev_config()).unwrap(); // what a Steam update does
        assert_eq!(mode.status(&sample_plan()), SteamPlayStatus::Lost);
    }

    #[test]
    fn disable_removes_only_what_enable_added() {
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        mode.disable().unwrap();
        assert!(!mode.steam.steam_dev_config().exists());
        assert!(!mode.link("macneutron-native").exists());
        assert!(mode.current_mappings().unwrap().is_empty());
        assert!(read(&mode.steam.config_vdf()).contains("\"proton_9\""));
        assert!(!mode.is_wanted());
        assert_eq!(fake.launches_with_dev_config(), [true, false]);
    }

    #[test]
    fn verifies_compat_log_sessions() {
        let cases: [(&str, Option<&str>); 4] = [
            (OK_SESSION, None),
            (MAC_MODE_SESSION, Some("Steam started as a Mac client and ignored MacNeutron")),
            (
                "Client version: 1\nRegistering tool macneutron, AppID 0\nRecording non-user mapping",
                Some("Steam didn't find MacNeutron's tools"),
            ),
            (
                "Client version: 1\nRegistering tool macneutron, AppID 0\nRegistering tool macneutron-native, AppID 0\n",
                Some("Steam didn't switch to Steam Play mode"),
            ),
        ];
        for (log, problem) in cases {
            assert_eq!(SteamPlayMode::verify(log).as_deref(), problem, "{log}");
        }
    }

    #[test]
    fn last_session_ignores_earlier_runs() {
        let mac_then_ok = format!("{MAC_MODE_SESSION}{OK_SESSION}");
        let ok_then_mac = format!("{OK_SESSION}{MAC_MODE_SESSION}");
        assert_eq!(SteamPlayMode::verify(SteamPlayMode::last_session(&mac_then_ok)), None);
        assert!(SteamPlayMode::verify(SteamPlayMode::last_session(&ok_then_mac)).is_some());
    }

    #[test]
    fn sync_refuses_to_drop_mac_game_protection() {
        // An unreadable app list must never turn into "no Mac games" while Steam stays in Linux mode.
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        fake.quit(Duration::from_secs(1)).unwrap();
        let plan = BTreeMap::from([("0".to_string(), ToolMapping::new("macneutron", 75))]);
        assert_eq!(mode.sync(&plan), Err(SteamPlayError::PlanDropsMacGames(1)));
        assert_eq!(mode.current_mappings().unwrap()["1062090"].tool, "macneutron-native");
    }

    #[test]
    fn enable_builds_the_plan_after_steam_has_quit() {
        // Steam rewrites its app list on exit, so a plan built earlier can miss a game bought this session.
        let (mode, fake) = make_mode(OK_SESSION, STEAM_CONFIG_FIXTURE, true);
        let mut steam_was_running = None;
        mode.enable_with(|| {
            steam_was_running = Some(fake.is_running());
            Ok(sample_plan())
        })
        .unwrap();
        assert_eq!(steam_was_running, Some(false));
        assert_eq!(mode.current_mappings().unwrap(), sample_plan());
    }

    #[test]
    fn intent_is_recorded_before_steam_starts_in_linux_mode() {
        // If the app dies during verification, it must still know Steam Play mode is on.
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        assert_eq!(fake.launches_with_intent(), [true]);
        let (failing, failing_fake) = make_mode(MAC_MODE_SESSION, STEAM_CONFIG_FIXTURE, false);
        assert!(failing.enable(&sample_plan()).is_err());
        assert_eq!(failing_fake.launches_with_intent(), [true, false]);
        assert!(!failing.is_wanted());
    }

    #[test]
    fn apps_mapped_to_another_tool_dont_count_as_pending() {
        // The fixture maps 440 to proton_9; a plan that also wants 440 must neither replace it nor stay "pending".
        let (mode, _) = ok_mode();
        let mut plan = sample_plan();
        plan.insert("440".into(), ToolMapping::new("macneutron", 250));
        mode.enable(&plan).unwrap();
        assert!(read(&mode.steam.config_vdf()).contains("\"proton_9\""));
        assert_eq!(mode.status(&plan), SteamPlayStatus::On);
    }

    #[test]
    fn unreadable_config_leaves_steam_running() {
        let (mode, fake) = make_mode(OK_SESSION, BROKEN_CONFIG, true);
        assert!(matches!(mode.enable(&sample_plan()), Err(SteamPlayError::ConfigUnreadable(_))));
        assert!(fake.is_running());
        assert!(fake.launches_with_dev_config().is_empty());
    }

    #[test]
    fn failure_before_launch_restarts_steam_in_mac_mode() {
        let (mode, fake) = make_mode(OK_SESSION, STEAM_CONFIG_FIXTURE, true);
        write("not a folder", &mode.steam.bundle_compat_tools(), false); // makes linking the tools fail
        assert!(mode.enable(&sample_plan()).is_err());
        assert!(fake.is_running());
        assert_eq!(fake.launches_with_dev_config(), [false]);
        assert!(!mode.steam.steam_dev_config().exists());
    }

    /// A mode whose Steam starts its compat log over at launch, after `old` was in it.
    fn mode_replacing_log(old: &str) -> SteamPlayMode {
        let (steam, root) = make_fake_steam(STEAM_CONFIG_FIXTURE);
        write(old, &steam.compat_log(), false);
        let fake = Arc::new(FakeSteam::new(steam.clone(), false, OK_SESSION, None, true));
        let mut mode = SteamPlayMode::new(steam, &root, fake);
        mode.verify_timeout = Duration::from_secs(2);
        mode
    }

    #[test]
    fn verification_handles_a_compat_log_that_starts_over() {
        // If Steam truncates its log at startup, the new session is shorter than the old file.
        let mode = mode_replacing_log(&"[old] Client version: 1 and more\n".repeat(200));
        mode.enable(&sample_plan()).unwrap();
        assert!(mode.is_wanted());
    }

    #[test]
    fn verification_handles_a_started_over_log_that_outgrew_the_old_one() {
        // Steam appends every session to one log; a log started over can grow past the old size before the first poll.
        let mode = mode_replacing_log(&"[2026-09-26 09:00:00] Client version: 1788652215\n".repeat(2));
        mode.enable(&sample_plan()).unwrap();
        assert!(mode.is_wanted());
    }

    #[test]
    fn status_reports_an_unreadable_config() {
        let (mode, _) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        write(BROKEN_CONFIG, &mode.steam.config_vdf(), false);
        match mode.status(&sample_plan()) {
            SteamPlayStatus::Problem(message) => assert!(message.contains("couldn't be read"), "{message}"),
            other => panic!("expected .problem, got {other:?}"),
        }
    }

    #[test]
    fn native_tool_runs_the_cli_passthrough() {
        // R0b: Steam launches a thin arm64 tool binary, so the Mac-game tool runs a copy of the CLI.
        use std::os::unix::fs::PermissionsExt;
        let (mode, _) = ok_mode();
        mode.enable(&sample_plan()).unwrap();
        let native = mode.link("macneutron-native");
        let manifest = read(&native.join("toolmanifest.vdf"));
        assert!(manifest.contains("\"commandline\" \"/bin/macneutron passthrough %verb%\""));
        let cli = native.join("bin/macneutron");
        assert_eq!(std::fs::read(&cli).unwrap(), std::fs::read(mode.runtime_tool().join("bin/macneutron")).unwrap());
        assert_ne!(std::fs::metadata(&cli).unwrap().permissions().mode() & 0o111, 0);
    }

    #[test]
    fn stale_script_is_removed() {
        let (mode, _) = ok_mode();
        let script = mode.native_tool().join("passthrough.sh");
        write("#!/bin/sh\n", &script, true);
        mode.install_native_tool().unwrap();
        assert!(!script.exists());
    }

    #[test]
    fn verify_accepts_either_runtime_name() {
        // A Steam session started before the update registered only the old name.
        assert_eq!(SteamPlayMode::verify(OK_SESSION), None);
        assert_eq!(SteamPlayMode::verify(&OK_SESSION.replace("tool macneutron,", "tool macneutron-proton,")), None);
    }

    #[test]
    fn sync_moves_legacy_mappings_then_drops_the_legacy_name() {
        let (mode, fake) = ok_mode();
        mode.enable(&sample_plan()).unwrap(); // the old name, as installed before the rename
        let folder = mode.tools.join(SteamPlayMode::RUNTIME_FOLDER_NAME);
        RuntimeInstaller::write_compatibility_tool(&folder, true).unwrap();
        fake.quit(Duration::from_secs(1)).unwrap();
        let renamed = renamed_plan(&sample_plan());
        assert_eq!(mode.sync(&renamed), Ok(true));
        assert_eq!(mode.current_mappings().unwrap(), renamed);
        assert!(!RuntimeInstaller::registers_legacy_name(&folder));
        assert!(folder.join("compatibilitytool.vdf").exists());
    }

    /// `plan` as a build after the rename makes it.
    fn renamed_plan(plan: &BTreeMap<String, ToolMapping>) -> BTreeMap<String, ToolMapping> {
        plan.iter()
            .map(|(app, m)| {
                let tool = if m.tool == "macneutron" { "macneutron-proton" } else { &m.tool };
                (app.clone(), ToolMapping::new(tool, m.priority))
            })
            .collect()
    }

    /// `compatdata/<id>/pfx/drive_c/users` in the fake Steam's own library, with the Wine profile `u` in it.
    fn make_prefix_users(mode: &SteamPlayMode, id: &str) -> PathBuf {
        let users = mode.steam.root.join(format!("steamapps/compatdata/{id}/pfx/drive_c/users"));
        for folder in ["AppData/Local", "AppData/Roaming", "Documents"] {
            // as wineboot makes them
            std::fs::create_dir_all(users.join("u").join(folder)).unwrap();
        }
        users
    }

    fn mode_for_user_u() -> (SteamPlayMode, Arc<FakeSteam>) {
        let (mut mode, fake) = ok_mode();
        mode.profile_user = "u".into();
        (mode, fake)
    }

    #[test]
    fn migration_links_existing_prefixes() {
        // Spec D3: Steam's first resolved pass then finds the Mac's saves where Proton prefixes keep them.
        let (mode, fake) = mode_for_user_u();
        mode.enable(&sample_plan()).unwrap();
        let users = make_prefix_users(&mode, "10");
        fake.quit(Duration::from_secs(1)).unwrap();
        assert_eq!(mode.sync(&renamed_plan(&sample_plan())), Ok(true));
        assert_eq!(std::fs::read_link(users.join("steamuser")).unwrap(), PathBuf::from("u"));
    }

    #[test]
    fn migration_never_merges_seeded_folders() {
        let (mode, fake) = mode_for_user_u();
        mode.enable(&sample_plan()).unwrap();
        let users = make_prefix_users(&mode, "11");
        write("cloud", &users.join("steamuser/Application Data/G/x"), false);
        fake.quit(Duration::from_secs(1)).unwrap();
        assert_eq!(mode.sync(&renamed_plan(&sample_plan())), Ok(true));
        assert!(std::fs::symlink_metadata(users.join("steamuser")).unwrap().is_dir());
    }

    #[test]
    fn sync_without_legacy_mappings_touches_no_prefix() {
        let (mode, fake) = mode_for_user_u();
        mode.enable(&renamed_plan(&sample_plan())).unwrap();
        let users = make_prefix_users(&mode, "12");
        fake.quit(Duration::from_secs(1)).unwrap();
        let mut plan = renamed_plan(&sample_plan());
        plan.insert("3419430".into(), ToolMapping::new("macneutron-native", 250));
        assert_eq!(mode.sync(&plan), Ok(true));
        assert!(!users.join("steamuser").exists());
    }
}
