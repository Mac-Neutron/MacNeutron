//! Creates, upgrades and equips a game's Wine prefix at `compatdata/<appid>/pfx`. Its stamp, `compatdata/<appid>/version`,
//! names the `wine.app` that prepared it and the msync mode its wineserver runs in. Ported from `PrefixManager.swift`.

use std::collections::BTreeMap;
use std::fmt;
use std::os::unix::fs::MetadataExt;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use crate::compat_context::CompatContext;
use crate::file_lock::with_file_lock;
use crate::game_drive::GameDrive;
use crate::launch_environment::LaunchEnvironment;
use crate::launcher_log::LauncherLog;
use crate::pe_image;
use crate::player_data::PlayerData;
use crate::posix;
use crate::process_runner::{ExitStatus, ProcessRunner};
use crate::profile_links::{ProfileLinkReport, ProfileLinks};
use crate::runtime_installer::write_atomic;
use crate::steam_bridge::SteamBridge;
use crate::tool_layout::ToolLayout;
use crate::valve_client::ValveClient;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PrefixError {
    LockFailed(String),
    WinebootFailed(i32),
    EmulatorSetupFailed(i32),
    DllCopyFailed(String),
    SteamBridgeCopyFailed(String),
    /// What Swift left to Foundation's errors: a folder, a rename or a stamp the filesystem refused, or a child that
    /// couldn't be started.
    Io(String),
}

impl fmt::Display for PrefixError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PrefixError::LockFailed(reason) => write!(f, "could not lock the prefix: {reason}"),
            PrefixError::WinebootFailed(status) => {
                write!(f, "prefix setup failed (wineboot exit {status}); the next launch retries")
            }
            PrefixError::EmulatorSetupFailed(status) => {
                write!(f, "prefix setup failed (registering FEX exited {status}); the next launch retries")
            }
            PrefixError::DllCopyFailed(detail) => write!(f, "could not install graphics DLLs: {detail}"),
            PrefixError::SteamBridgeCopyFailed(detail) => write!(f, "could not install the Steam bridge: {detail}"),
            PrefixError::Io(text) => write!(f, "{text}"),
        }
    }
}

impl std::error::Error for PrefixError {}

impl From<std::io::Error> for PrefixError {
    fn from(error: std::io::Error) -> Self {
        PrefixError::Io(error.to_string())
    }
}

/// What `on_prepare` hands back: called once the preparation is done.
pub type Prepared = Box<dyn FnOnce()>;

pub struct PrefixManager {
    pub context: CompatContext,
    pub layout: ToolLayout,
    pub identity: String,
    pub runner: Arc<dyn ProcessRunner>,
    /// Shared with the launcher, whose quiet switch it follows.
    pub log: Arc<LauncherLog>,
    /// Called as a prefix starts being prepared (made, or made again for a new wine.app); what it returns, once that's
    /// done. The launcher's shows a toast meanwhile.
    pub on_prepare: Box<dyn Fn() -> Prepared + Send + Sync>,
}

impl PrefixManager {
    /// Written before wineboot, so a failed or stopped preparation is retried in place, never renamed.
    pub(crate) const PREPARING_STAMP: &'static str = "wine.app preparing";
    /// In the compat folder: the renamed prefix whose data is still to be carried, written before the rename, so a
    /// launch stopped anywhere after it carries on the next one. After the carry it becomes `CARRIED_MARKER` (or is
    /// removed).
    pub(crate) const PENDING_RECORD: &'static str = "player-data-pending";
    /// The last carry's record (its prefix's name); nothing reads it.
    pub(crate) const CARRIED_MARKER: &'static str = "player-data-carried";

    /// A manager whose `on_prepare` does nothing; set the field for the launcher's toast.
    pub fn new(
        context: CompatContext,
        layout: ToolLayout,
        identity: String,
        runner: Arc<dyn ProcessRunner>,
        log: Arc<LauncherLog>,
    ) -> Self {
        PrefixManager { context, layout, identity, runner, log, on_prepare: Box::new(|| Box::new(|| {})) }
    }

    pub fn stamp(identity: &str, msync: bool) -> String {
        format!("wine.app {identity} msync={}", if msync { 1 } else { 0 })
    }

    /// Prepares the prefix when it is missing or another `wine.app` prepared it; stops a wineserver left in the other
    /// msync mode; keeps Steam Cloud's profile links (spec §4.2); then, when asked, installs the Steam bridge. Holds the
    /// prefix lock only for this, never while the game runs. Nothing under `drive_c` is ever deleted: a Rosetta-era
    /// prefix is renamed. Returns what the links did, for the launcher to tell the player about a conflict.
    pub fn prepare(
        &self,
        environment: &BTreeMap<String, String>,
        steam_bridge: bool,
    ) -> Result<ProfileLinkReport, PrefixError> {
        std::fs::create_dir_all(&self.context.data_path)?;
        with_file_lock(&self.context.lock_file(), || self.prepare_locked(environment, steam_bridge))
            .map_err(|error| PrefixError::LockFailed(error.to_string()))?
    }

    fn prepare_locked(
        &self,
        environment: &BTreeMap<String, String>,
        steam_bridge: bool,
    ) -> Result<ProfileLinkReport, PrefixError> {
        // Wine reads WINEMSYNC with atoi; launch options may set it to anything.
        let want = Self::stamp(&self.identity, atoi(environment.get("WINEMSYNC").map_or("", String::as_str)) != 0);
        let recorded = std::fs::read_to_string(self.context.version_file()).ok().map(|s| s.trim().to_string());
        let prefix = self.context.prefix();
        let exists = prefix.exists();
        let same_wine = format!("wine.app {} msync=", self.identity);
        if exists && recorded.as_deref() == Some(want.as_str()) {
            // Up to date.
        } else if let Some(recorded) = recorded.as_deref().filter(|r| exists && r.starts_with(&same_wine)) {
            // A client and a server in different msync modes can't talk: stop the last launch's server.
            let mut old = environment.clone();
            if recorded.ends_with(" msync=1") {
                old.insert("WINEMSYNC".to_string(), "1".to_string());
            } else {
                old.remove("WINEMSYNC");
            }
            self.run(&self.layout.wineserver(), &["-k"], &old)?;
            self.log.append("note: msync changed, stopped the prefix's wineserver");
            write_atomic(&self.context.version_file(), want.as_bytes())?;
        } else {
            // An arm64 Wine doesn't adopt an x86_64 Wine's prefix. Only a rename carries: a preparation stopped after
            // one (its pending record says which) carries on its retry; an arm64 prefix prepared again never does,
            // whatever pfx.rosetta lies beside it.
            // Steam's cloud download makes pfx before the tool ever runs (no stamp, no user.reg): it is built into,
            // never renamed, or the saves it holds go missing from where Steam watches them (spec §4.3).
            let seeded = recorded.is_none() && !prefix.join("user.reg").exists();
            let carry_from = if exists && !recorded.as_deref().is_some_and(|r| r.starts_with("wine.app ")) && !seeded {
                Some(self.rename_rosetta_prefix()?)
            } else {
                std::fs::read_to_string(self.context.data_path.join(Self::PENDING_RECORD))
                    .ok()
                    .filter(|name| name.starts_with("pfx.rosetta") && !name.contains('/'))
            };
            let prepared = (self.on_prepare)();
            let result = self.prepare_stamped(environment, carry_from.as_deref(), &want);
            prepared();
            result?;
        }
        // Every launch: Steam may have made real folders where the links go since the last one.
        let user = environment.get("USER").cloned().unwrap_or_else(posix::user_name);
        let links = ProfileLinks::apply(&prefix, &user, true);
        for note in &links.notes {
            self.log.append(&format!("note: {note}"));
        }
        // Proton's game drive (setup_prefix, proton:1259): every preparation, with the bridge or without.
        for note in GameDrive::apply(&prefix, environment) {
            self.log.append(&format!("note: {note}"));
        }
        if steam_bridge {
            self.deploy_steam_bridge(environment)?;
        }
        Ok(links)
    }

    /// The preparation proper, between the preparing stamp and the wanted one.
    fn prepare_stamped(
        &self,
        environment: &BTreeMap<String, String>,
        carry_from: Option<&str>,
        want: &str,
    ) -> Result<(), PrefixError> {
        write_atomic(&self.context.version_file(), Self::PREPARING_STAMP.as_bytes())?;
        self.prepare_new(environment)?;
        // After prepare_new's `wineserver -w`: no server runs to rewrite user.reg.
        if let Some(name) = carry_from {
            self.carry_player_data(name);
        }
        write_atomic(&self.context.version_file(), want.as_bytes())?;
        Ok(())
    }

    fn run(&self, executable: &Path, arguments: &[&str], environment: &BTreeMap<String, String>) -> Result<ExitStatus, PrefixError> {
        let arguments: Vec<String> = arguments.iter().map(|a| a.to_string()).collect();
        Ok(self.runner.run(executable, &arguments, environment, None)?)
    }

    fn prepare_new(&self, environment: &BTreeMap<String, String>) -> Result<(), PrefixError> {
        let wine = self.layout.wine();
        let mut boot = environment.clone();
        // No Mono or Gecko prompt.
        boot.insert(
            "WINEDLLOVERRIDES".to_string(),
            LaunchEnvironment::merge_overrides(
                environment.get("WINEDLLOVERRIDES").map_or("", String::as_str),
                Some("mscoree,mshtml="),
            ),
        );
        let status = self.run(&wine, &["wineboot", "-u"], &boot)?;
        if status != 0 {
            return Err(PrefixError::WinebootFailed(status));
        }
        // x64 code runs on FEX; without this entry, on Wine's stub xtajit64, and every x64 game fails.
        let fex = self.run(
            &wine,
            &["reg", "add", r"HKLM\Software\Microsoft\Wow64\amd64", "/ve", "/d", "libarm64ecfex.dll", "/f"],
            environment,
        )?;
        if fex != 0 {
            return Err(PrefixError::EmulatorSetupFailed(fex));
        }
        // A crashing game must exit, not wait behind Wine's crash window while Steam shows it running.
        // ponytail: best effort; if reg fails, crashes still work, they just show the dialog.
        self.run(
            &wine,
            &["reg", "add", r"HKCU\Software\Wine\WineDbg", "/v", "ShowCrashDialog", "/t", "REG_DWORD", "/d", "0", "/f"],
            environment,
        )?;
        // A game's exclusive fullscreen changes Wine's virtual mode, scaled to the screen, never the Mac's display
        // (win32u reads it for winemac too, despite the key's name). Existing prefixes get it with the runtime that
        // brings it: a new wine.app changes the stamp, which prepares them again. Best effort, as above.
        self.run(
            &wine,
            &["reg", "add", r"HKCU\Software\Wine\X11 Driver", "/v", "EmulateModeset", "/d", "Y", "/f"],
            environment,
        )?;
        // Whatever the game's graphics setting: wined3d's overrides ignore them, and switching back to DXMT doesn't
        // change the stamp. The identity covers DXMT, so they're refreshed whenever wine.app changes.
        for name in ToolLayout::DXMT_DLLS {
            let source = self.layout.dxmt().join(name);
            let destination = format!("drive_c/windows/system32/{name}");
            if !source.exists() {
                return Err(PrefixError::DllCopyFailed(format!(
                    "{destination}: missing from the runtime ({})",
                    source.display()
                )));
            }
            self.install(&source, &destination)
                .map_err(|error| PrefixError::DllCopyFailed(format!("{destination}: {error}")))?;
        }
        self.run(&self.layout.wineserver(), &["-w"], environment)?;
        Ok(())
    }

    /// `pfx` → the first free `pfx.rosetta`, `pfx.rosetta-2`, … (the saves inside stay where the player can find them);
    /// returns the new name, recorded as pending first.
    fn rename_rosetta_prefix(&self) -> Result<String, PrefixError> {
        let data = &self.context.data_path;
        let prefix = self.context.prefix();
        let mut name = "pfx.rosetta".to_string();
        let mut number = 1;
        while data.join(&name).exists() {
            number += 1;
            name = format!("pfx.rosetta-{number}");
        }
        write_atomic(&data.join(Self::PENDING_RECORD), name.as_bytes())?;
        let old = data.join(&name);
        std::fs::rename(&prefix, &old)?;
        // Steam's cloud download made users/steamuser beside the Rosetta-era profile (`crossover`): its files go
        // straight back where Steam watches them, for ProfileLinks to merge (spec §4.3); if they can't, the rename is
        // undone.
        // ponytail: two renames, so a kill between them leaves those paths missing for one exit pass; a RENAME_SWAP of
        // a staged pfx would close that window.
        if let Some(cloud) = Self::steams_own_folder(&old) {
            let users = prefix.join("drive_c/users");
            let moved = std::fs::create_dir_all(&users).and_then(|_| std::fs::rename(&cloud, users.join("steamuser")));
            if let Err(error) = moved {
                for folder in [users.clone(), prefix.join("drive_c"), prefix.clone()] {
                    let _ = std::fs::remove_dir(folder); // only the empty folders made just above
                }
                let _ = std::fs::rename(&old, &prefix);
                return Err(error.into());
            }
        }
        self.log.append(&format!("note: renamed a Rosetta-era prefix to {name}"));
        Ok(name)
    }

    /// `users/steamuser` when Steam made it: a real folder beside another profile (Wine names its own after `$USER`, the
    /// Rosetta-era Wine `crossover`).
    fn steams_own_folder(prefix: &Path) -> Option<PathBuf> {
        let users = prefix.join("drive_c/users");
        let is_real_folder = |name: &str| posix::kind(&users.join(name)) == posix::Kind::Directory;
        let names: Vec<String> = std::fs::read_dir(&users)
            .map(|entries| entries.flatten().filter_map(|e| e.file_name().into_string().ok()).collect())
            .unwrap_or_default();
        let beside = names
            .iter()
            .any(|name| !["public", "steamuser"].contains(&name.to_lowercase().as_str()) && is_real_folder(name));
        (is_real_folder("steamuser") && beside).then(|| users.join("steamuser"))
    }

    /// The fresh prefix gets the renamed one's settings and local saves (spec §14, amending §3.4 step 1). What isn't
    /// carried only costs the player that: the old prefix is still there, untouched.
    fn carry_player_data(&self, name: &str) {
        let data = &self.context.data_path;
        let carried = PlayerData::carry(&data.join(name), &self.context.prefix());
        let failed = if carried.failed > 0 { format!(", {} not carried", carried.failed) } else { String::new() };
        self.log.append(&format!(
            "note: carried the player's data from {name}: {} files, {} registry keys{failed}",
            carried.files, carried.keys
        ));
        // A pending record left behind would carry this prefix again into a live one (a re-preparation stopped and
        // retried); the marker is never read, so if it can't be renamed it goes.
        let pending = data.join(Self::PENDING_RECORD);
        if !posix::rename(&pending, &data.join(Self::CARRIED_MARKER)) {
            let _ = std::fs::remove_file(&pending);
        }
    }

    /// Every launch: prefixes made before the bridge existed get it too, and a runtime update replaces it.
    fn deploy_steam_bridge(&self, environment: &BTreeMap<String, String>) -> Result<(), PrefixError> {
        for (source, destination) in SteamBridge::prefix_files(&self.layout, self.steam_client64(environment)?) {
            self.install(&source, &destination)
                .map_err(|error| PrefixError::SteamBridgeCopyFailed(format!("{destination}: {error}")))?;
        }
        Ok(())
    }

    /// The prefix's `steamclient64.dll`, as Proton keeps Valve's signed one there (SteamStub checks its signature): the
    /// launch option's choice, else the app's download from Valve when the runtime can use it (Wine 0036), else None
    /// for lsteamclient. Chosen and logged per launch, so a change takes effect at the next one (the sizes differ).
    fn steam_client64(&self, environment: &BTreeMap<String, String>) -> Result<Option<PathBuf>, PrefixError> {
        let variable = SteamBridge::STEAMCLIENT64_VARIABLE;
        if let Some(path) = environment.get(variable).filter(|p| !p.is_empty()) {
            if path == "builtin" {
                self.log.append(&format!("note: steamclient64.dll is lsteamclient ({variable}=builtin)"));
                return Ok(None);
            }
            // Logged first, so a bad path is diagnosable.
            self.log.append(&format!("note: steamclient64.dll from {path} ({variable})"));
            if pe_image::machine(Path::new(path)) != Some(pe_image::AMD64) {
                return Err(PrefixError::SteamBridgeCopyFailed(format!(
                    "{variable}: {path} is missing or not an x64 DLL"
                )));
            }
            return Ok(Some(PathBuf::from(path)));
        }
        if !self.layout.supports_genuine_steamclient() {
            self.log.append("note: steamclient64.dll is lsteamclient: this runtime has no genuine-steamclient marker");
            return Ok(None);
        }
        let Some(valve) = ValveClient::installed(&self.layout.valve_cache()) else {
            self.log.append("note: steamclient64.dll is lsteamclient: Valve's isn't downloaded");
            return Ok(None);
        };
        self.log.append(&format!(
            "note: steamclient64.dll from Valve's client {} ({})",
            valve.version,
            valve.steam_client64().display()
        ));
        Ok(Some(valve.steam_client64()))
    }

    /// For a game started without the bridge (launch option, or a runtime without it): its steam_api loads the client
    /// DLL an earlier launch left (its registry values persist), and the bridge can then abort the game. Without these
    /// files the game just finds no Steam.
    pub fn remove_steam_bridge(&self) -> Result<(), PrefixError> {
        with_file_lock(&self.context.lock_file(), || {
            // steam.exe stays: HKCR\steam sends the game's steam:// links through it to Mac Steam (parity plan R10).
            for name in ["steamclient64.dll", "steamclient.dll"] {
                let _ = std::fs::remove_file(self.context.prefix().join(format!("{}/{name}", SteamBridge::PREFIX_FOLDER)));
            }
        })
        .map_err(|error| PrefixError::LockFailed(error.to_string()))
    }

    /// Copies `source` into the prefix unless the copy there has its size and modification time (lsteamclient.dll is
    /// 57 MB); the copy gets the source's times, nanoseconds included, so it matches at the next launch.
    fn install(&self, source: &Path, destination: &str) -> std::io::Result<()> {
        let target = self.context.prefix().join(destination);
        let src = std::fs::metadata(source)?;
        if let Ok(dst) = std::fs::metadata(&target) {
            if dst.size() == src.size() && dst.mtime() == src.mtime() && dst.mtime_nsec() == src.mtime_nsec() {
                return Ok(());
            }
        }
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent)?;
        }
        if target.exists() {
            std::fs::remove_file(&target)?;
        }
        // An APFS clone where it can, as Foundation's copyItem.
        if !posix::clone_file(source, &target) {
            return Err(std::io::Error::last_os_error());
        }
        posix::copy_times(source, &target)
    }
}

/// C's `atoi`: leading whitespace, an optional sign, then digits up to the first non-digit; 0 when there are none.
/// Only whether it is 0 matters here.
/// ponytail: an overflow wraps (C leaves it undefined); nobody sets WINEMSYNC past `int`.
fn atoi(text: &str) -> i32 {
    let rest = text.trim_start_matches([' ', '\t', '\n', '\u{0B}', '\u{0C}', '\r']);
    let (negative, rest) = match rest.as_bytes().first() {
        Some(b'-') => (true, &rest[1..]),
        Some(b'+') => (false, &rest[1..]),
        _ => (false, rest),
    };
    let value = rest
        .bytes()
        .take_while(u8::is_ascii_digit)
        .fold(0i32, |value, digit| value.wrapping_mul(10).wrapping_add((digit - b'0') as i32));
    if negative { value.wrapping_neg() } else { value }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::graphics_backend::GraphicsBackend;
    use crate::process_runner::SystemProcessRunner;
    use crate::test_support::{
        install_fake_steam_bridge, make_temp_dir, make_tool_layout, pe_bytes, steam_environment,
        wineboot_creating_prefix, write, Call, FakeRunner,
    };
    use std::sync::Mutex;

    type Env = BTreeMap<String, String>;

    const APP_ID: &str = "3419430";

    fn make_manager_in(runner: &Arc<FakeRunner>, compat: &str, backend: GraphicsBackend) -> (PrefixManager, Env) {
        let layout = make_tool_layout();
        let context = CompatContext::new(&steam_environment(&make_temp_dir().join(compat), APP_ID)).unwrap();
        let log = Arc::new(LauncherLog::new(make_temp_dir().join("Logs")));
        let manager = PrefixManager::new(context, layout, "id1".to_string(), runner.clone(), log);
        let env = wine_environment(&manager, backend, &[]);
        (manager, env)
    }

    fn make_manager(runner: &Arc<FakeRunner>) -> (PrefixManager, Env) {
        make_manager_in(runner, "compatdata/42", GraphicsBackend::Dxmt)
    }

    /// The environment the launcher builds for this manager's game; `extra` stands for launch options.
    fn wine_environment(manager: &PrefixManager, backend: GraphicsBackend, extra: &[(&str, &str)]) -> Env {
        let mut base = steam_environment(&manager.context.data_path, APP_ID);
        for (key, value) in extra {
            base.insert(key.to_string(), value.to_string());
        }
        LaunchEnvironment::build(&base, &manager.context, backend, false)
    }

    fn dxmt_environment(manager: &PrefixManager, extra: &[(&str, &str)]) -> Env {
        wine_environment(manager, GraphicsBackend::Dxmt, extra)
    }

    fn stamp(manager: &PrefixManager) -> Option<String> {
        std::fs::read_to_string(manager.context.version_file()).ok()
    }

    fn launcher_log(manager: &PrefixManager) -> String {
        std::fs::read_to_string(manager.log.launcher_log()).unwrap_or_default()
    }

    fn wineboot_count(runner: &FakeRunner) -> usize {
        runner.calls().iter().filter(|c| c.arguments.first().map(String::as_str) == Some("wineboot")).count()
    }

    fn command(call: &Call) -> Vec<String> {
        std::iter::once(call.tool.clone()).chain(call.arguments.iter().cloned()).collect()
    }

    fn line(tool: &str, arguments: &[&str]) -> Vec<String> {
        std::iter::once(tool).chain(arguments.iter().copied()).map(String::from).collect()
    }

    fn read(path: &Path) -> String {
        std::fs::read_to_string(path).unwrap()
    }

    fn put(text: &str, path: &Path) {
        write(text, path, false);
    }

    fn runner(fake: FakeRunner) -> Arc<FakeRunner> {
        Arc::new(fake)
    }

    /// x64 code runs on FEX; without this entry, on Wine's stub.
    const REGISTER_FEX: [&str; 7] = ["reg", "add", r"HKLM\Software\Microsoft\Wow64\amd64", "/ve", "/d", "libarm64ecfex.dll", "/f"];
    /// A crashing game must exit, not wait forever behind Wine's crash window (Steam would show it running).
    const DISABLE_CRASH_DIALOG: [&str; 10] =
        ["reg", "add", r"HKCU\Software\Wine\WineDbg", "/v", "ShowCrashDialog", "/t", "REG_DWORD", "/d", "0", "/f"];
    /// A game's exclusive fullscreen changes Wine's virtual mode, never the Mac's display (XeSS plan, Task FS).
    const EMULATE_MODESET: [&str; 8] = ["reg", "add", r"HKCU\Software\Wine\X11 Driver", "/v", "EmulateModeset", "/d", "Y", "/f"];

    #[test]
    fn fresh_prefix_runs_wineboot_and_records_version() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        manager.prepare(&env, false).unwrap();
        let calls = runner.calls();
        assert_eq!(
            calls.iter().map(command).collect::<Vec<_>>(),
            vec![
                line("wine", &["wineboot", "-u"]),
                line("wine", &REGISTER_FEX),
                line("wine", &DISABLE_CRASH_DIALOG),
                line("wine", &EMULATE_MODESET),
                line("wineserver", &["-w"]),
            ]
        );
        let prefix = format!("{}/", manager.context.prefix().display());
        assert!(calls.iter().all(|c| c.environment.get("WINEPREFIX") == Some(&prefix)));
        assert!(calls[0].environment.get("WINEDLLOVERRIDES").is_some_and(|v| v.contains("mscoree=;mshtml=")));
        assert_eq!(calls[1].environment.get("WINEDLLOVERRIDES"), env.get("WINEDLLOVERRIDES"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
        assert_eq!(PrefixManager::stamp("id1", false), "wine.app id1 msync=0");
    }

    #[test]
    fn failed_wineboot_skips_the_registry_steps() {
        let runner = runner(wineboot_creating_prefix(3, 0.0));
        let (manager, env) = make_manager(&runner);
        assert_eq!(manager.prepare(&env, false).unwrap_err(), PrefixError::WinebootFailed(3));
        assert_eq!(runner.calls().into_iter().map(|c| c.arguments).collect::<Vec<_>>(), vec![vec!["wineboot", "-u"]]);
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app preparing"));
    }

    #[test]
    fn up_to_date_prefix_skips_wineboot() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        manager.prepare(&env, false).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(wineboot_count(&runner), 1);
        assert_eq!(runner.calls().len(), 5);
    }

    #[test]
    fn identity_change_prepares_in_place() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        let save = manager.context.prefix().join("drive_c/users/steamuser/save.dat");
        put("progress", &save);
        put("wine.app id0 msync=1", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        assert_eq!(wineboot_count(&runner), 1);
        // Prefixes made before EmulateModeset was a default get it with the runtime that brings it.
        assert!(runner.calls().iter().any(|c| c.arguments == EMULATE_MODESET));
        assert_eq!(read(&save), "progress");
        assert!(!manager.context.data_path.join("pfx.rosetta").exists());
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn rosetta_era_prefix_is_renamed_never_deleted() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        put("progress", &manager.context.prefix().join("drive_c/users/steamuser/save.dat"));
        put("runtime-v4.7.3", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        let renamed = manager.context.data_path.join("pfx.rosetta/drive_c/users/steamuser/save.dat");
        assert_eq!(read(&renamed), "progress");
        assert!(manager.context.prefix().exists());
        assert!(!manager.context.prefix().join("drive_c/users/steamuser/save.dat").exists());
        assert!(launcher_log(&manager).contains("note: renamed a Rosetta-era prefix to pfx.rosetta\n"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn rename_numbers_past_taken_names_and_keeps_the_saves() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        let data = manager.context.data_path.clone();
        put("first", &data.join("pfx.rosetta/save.dat"));
        put("second", &data.join("pfx.rosetta-2/save.dat"));
        put("third", &manager.context.prefix().join("save.dat")); // no stamp at all: Rosetta-era too
        put("WINE REGISTRY Version 2\n", &manager.context.prefix().join("user.reg")); // Wine made it, not Steam
        manager.prepare(&env, false).unwrap();
        assert_eq!(read(&data.join("pfx.rosetta/save.dat")), "first");
        assert_eq!(read(&data.join("pfx.rosetta-2/save.dat")), "second");
        assert_eq!(read(&data.join("pfx.rosetta-3/save.dat")), "third");
        assert!(launcher_log(&manager).contains("note: renamed a Rosetta-era prefix to pfx.rosetta-3\n"));
    }

    #[test]
    fn preparing_stamp_is_retried_in_place() {
        // A failed or stopped preparation of an arm64 prefix is retried, never renamed.
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        let save = manager.context.prefix().join("drive_c/users/steamuser/save.dat");
        put("progress", &save);
        put("wine.app preparing", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        assert_eq!(wineboot_count(&runner), 1);
        assert_eq!(read(&save), "progress");
        assert!(!launcher_log(&manager).contains("renamed"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn failed_fex_registration_fails_the_preparation() {
        let runner = runner(FakeRunner::new(|call| {
            if call.arguments.first().map(String::as_str) == Some("wineboot") {
                if let Some(prefix) = call.environment.get("WINEPREFIX") {
                    let _ = std::fs::create_dir_all(prefix);
                }
            }
            if call.arguments == REGISTER_FEX { 5 } else { 0 }
        }));
        let (manager, env) = make_manager(&runner);
        assert_eq!(manager.prepare(&env, false).unwrap_err(), PrefixError::EmulatorSetupFailed(5));
        assert!(!runner.calls().iter().any(|c| c.arguments == DISABLE_CRASH_DIALOG));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app preparing"));
    }

    #[test]
    fn msync_only_change_kills_the_server_under_the_old_mode() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, env) = make_manager(&runner);
        manager.prepare(&env, false).unwrap();
        let first = runner.calls().len();
        manager.prepare(&dxmt_environment(&manager, &[("MACNEUTRON_NO_MSYNC", "1")]), false).unwrap();
        let killed: Vec<Call> = runner.calls()[first..].to_vec();
        assert_eq!(killed.iter().map(command).collect::<Vec<_>>(), vec![line("wineserver", &["-k"])]);
        assert_eq!(killed[0].environment.get("WINEMSYNC").map(String::as_str), Some("1"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=0"));
        assert!(launcher_log(&manager).contains("note: msync changed, stopped the prefix's wineserver"));
        // And back: the server from the msync-off launch is stopped without WINEMSYNC.
        manager.prepare(&env, false).unwrap();
        let last = runner.calls().last().cloned().unwrap();
        assert_eq!(command(&last), line("wineserver", &["-k"]));
        assert_eq!(last.environment.get("WINEMSYNC"), None);
        assert_eq!(wineboot_count(&runner), 1);
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn copies_dxmt_into_system32_only() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        manager.prepare(&env, false).unwrap();
        let windows = manager.context.prefix().join("drive_c/windows");
        for dll in ["d3d10core.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll"] {
            assert_eq!(read(&windows.join(format!("system32/{dll}"))), format!("dxmt {dll}"));
        }
        assert!(!windows.join("syswow64").exists());
    }

    #[test]
    fn switching_back_to_dxmt_finds_its_dlls() {
        // The stamp doesn't name the backend: a prefix first prepared for wined3d must already hold DXMT's DLLs.
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, wined3d) = make_manager_in(&runner, "compatdata/42", GraphicsBackend::WineD3D);
        manager.prepare(&wined3d, false).unwrap();
        manager.prepare(&dxmt_environment(&manager, &[]), false).unwrap();
        assert_eq!(wineboot_count(&runner), 1);
        let d3d11 = manager.context.prefix().join("drive_c/windows/system32/d3d11.dll");
        assert_eq!(read(&d3d11), "dxmt d3d11.dll");
    }

    #[test]
    fn missing_runtime_dll_is_an_error() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        std::fs::remove_file(manager.layout.dxmt().join("d3d11.dll")).unwrap();
        assert!(matches!(manager.prepare(&env, false), Err(PrefixError::DllCopyFailed(_))));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app preparing"));
    }

    #[test]
    fn concurrent_launches_run_wineboot_once() {
        // Steam starts iscriptevaluator (`run`) and the game close together on first launch.
        let runner = runner(wineboot_creating_prefix(0, 0.3));
        let (manager, env) = make_manager(&runner);
        std::thread::scope(|scope| {
            let handles: Vec<_> = (0..2).map(|_| scope.spawn(|| manager.prepare(&env, false))).collect();
            for handle in handles {
                handle.join().unwrap().unwrap();
            }
        });
        assert_eq!(wineboot_count(&runner), 1);
    }

    #[test]
    fn system_runner_reports_status_and_captures_output() {
        let log = make_temp_dir().join("out.log");
        let arguments: Vec<String> = ["-c", "echo \"$1\"; exit 3", "sh", "a b"].map(String::from).to_vec();
        let status = SystemProcessRunner.run(Path::new("/bin/sh"), &arguments, &Env::new(), Some(&log)).unwrap();
        assert_eq!(status, 3);
        assert_eq!(read(&log), "a b\n");
    }

    fn steam_folder(manager: &PrefixManager) -> PathBuf {
        manager.context.prefix().join("drive_c/Program Files (x86)/Steam")
    }

    #[test]
    fn steam_bridge_is_copied_into_the_steam_folder() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&env, true).unwrap();
        let folder = steam_folder(&manager);
        assert_eq!(read(&folder.join("steam.exe")), "steam.exe");
        assert_eq!(read(&folder.join("steamclient64.dll")), "lsteamclient aarch64");
        assert!(!folder.join("steamclient.dll").exists());
        // Proton's launch path, C:\windows\system32\steam.exe; the Steam folder's copy stays for HKCR\steam.
        assert_eq!(read(&manager.context.prefix().join("drive_c/windows/system32/steam.exe")), "steam.exe");
    }

    fn inode(path: &Path) -> Option<u64> {
        std::fs::metadata(path).ok().map(|m| m.ino())
    }

    #[test]
    fn unchanged_bridge_files_are_not_recopied() {
        // lsteamclient.dll is 57 MB: a launch must not rewrite it when nothing changed.
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&env, true).unwrap();
        let client = steam_folder(&manager).join("steamclient64.dll");
        let before = inode(&client);
        manager.prepare(&env, true).unwrap();
        assert!(before.is_some());
        assert_eq!(inode(&client), before);
    }

    #[test]
    fn changed_bridge_file_is_recopied() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&env, true).unwrap();
        put("lsteamclient v2", &manager.layout.lsteamclient());
        manager.prepare(&env, true).unwrap();
        let client = steam_folder(&manager).join("steamclient64.dll");
        assert_eq!(read(&client), "lsteamclient v2");
        let modified = |path: &Path| std::fs::metadata(path).and_then(|m| m.modified()).ok();
        assert_eq!(modified(&client), modified(&manager.layout.lsteamclient()));
    }

    #[test]
    fn steam_client64_override_replaces_lsteamclient_in_the_prefix() {
        // The SteamStub spike: SteamStub checks the signature of the prefix's steamclient64.dll, so Valve's goes where
        // lsteamclient.dll goes; the next launch without the option puts lsteamclient.dll back.
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        let genuine = make_temp_dir().join("valve/steamclient64.dll");
        std::fs::create_dir_all(genuine.parent().unwrap()).unwrap();
        std::fs::write(&genuine, pe_bytes(pe_image::AMD64, false)).unwrap();
        let path = genuine.to_string_lossy().into_owned();
        manager.prepare(&dxmt_environment(&manager, &[("MACNEUTRON_STEAMCLIENT64", &path)]), true).unwrap();
        let client = steam_folder(&manager).join("steamclient64.dll");
        assert_eq!(std::fs::read(&client).unwrap(), pe_bytes(pe_image::AMD64, false));
        assert!(launcher_log(&manager).contains(&format!("steamclient64.dll from {path}")));
        manager.prepare(&env, true).unwrap();
        assert_eq!(read(&client), "lsteamclient aarch64");
    }

    #[test]
    fn steam_client64_override_that_is_missing_or_not_x64_fails_the_launch() {
        let (manager, _) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        let thirty_two_bit = make_temp_dir().join("steamclient.dll");
        std::fs::write(&thirty_two_bit, pe_bytes(pe_image::I386, false)).unwrap();
        for path in [thirty_two_bit.to_string_lossy().into_owned(), "/nonexistent/steamclient64.dll".to_string()] {
            let env = dxmt_environment(&manager, &[("MACNEUTRON_STEAMCLIENT64", &path)]);
            assert!(matches!(manager.prepare(&env, true), Err(PrefixError::SteamBridgeCopyFailed(_))));
        }
    }

    /// Valve's DLL as the app leaves it in `layout.valve_cache()/<version>/`, with or without the runtime's 0036 marker.
    fn cache_valve_client(layout: &ToolLayout, marker: bool) -> PathBuf {
        if marker {
            put("0036\n", &layout.wine_app().join("Contents/Resources/genuine-steamclient"));
        }
        let dll = layout.valve_cache().join("1788652215/steamclient64.dll");
        std::fs::create_dir_all(dll.parent().unwrap()).unwrap();
        std::fs::write(&dll, pe_bytes(pe_image::AMD64, true)).unwrap();
        dll
    }

    #[test]
    fn the_cached_valve_dll_goes_in_the_prefix_when_the_runtime_supports_it() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        let dll = cache_valve_client(&manager.layout, true);
        manager.prepare(&env, true).unwrap();
        assert_eq!(std::fs::read(steam_folder(&manager).join("steamclient64.dll")).unwrap(), pe_bytes(pe_image::AMD64, true));
        assert!(launcher_log(&manager)
            .contains(&format!("note: steamclient64.dll from Valve's client 1788652215 ({})", dll.display())));
    }

    #[test]
    fn without_the_marker_the_cache_is_ignored() {
        // An older runtime can't load Valve's DLL (its tier0_s64 import): the game would get no Steam.
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        cache_valve_client(&manager.layout, false);
        manager.prepare(&env, true).unwrap();
        assert_eq!(read(&steam_folder(&manager).join("steamclient64.dll")), "lsteamclient aarch64");
        assert!(launcher_log(&manager)
            .contains("note: steamclient64.dll is lsteamclient: this runtime has no genuine-steamclient marker"));
    }

    #[test]
    fn without_a_cache_it_is_lsteamclient() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        put("0036\n", &manager.layout.wine_app().join("Contents/Resources/genuine-steamclient"));
        manager.prepare(&env, true).unwrap();
        assert_eq!(read(&steam_folder(&manager).join("steamclient64.dll")), "lsteamclient aarch64");
        assert!(launcher_log(&manager).contains("note: steamclient64.dll is lsteamclient: Valve's isn't downloaded"));
    }

    #[test]
    fn a_cache_without_steamclient64_falls_back_to_lsteamclient() {
        // A cache damaged by hand: the launch still starts, on lsteamclient.
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        let dll = cache_valve_client(&manager.layout, true);
        put("sha2 x\n", &dll.parent().unwrap().join("source.txt"));
        std::fs::remove_file(&dll).unwrap();
        manager.prepare(&env, true).unwrap();
        assert_eq!(read(&steam_folder(&manager).join("steamclient64.dll")), "lsteamclient aarch64");
        assert!(launcher_log(&manager).contains("note: steamclient64.dll is lsteamclient: Valve's isn't downloaded"));
    }

    #[test]
    fn builtin_forces_lsteamclient_over_the_cache() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        cache_valve_client(&manager.layout, true);
        manager.prepare(&dxmt_environment(&manager, &[("MACNEUTRON_STEAMCLIENT64", "builtin")]), true).unwrap();
        let client = steam_folder(&manager).join("steamclient64.dll");
        assert_eq!(read(&client), "lsteamclient aarch64");
        assert!(launcher_log(&manager).contains("note: steamclient64.dll is lsteamclient (MACNEUTRON_STEAMCLIENT64=builtin)"));
        manager.prepare(&env, true).unwrap();
        assert_eq!(std::fs::read(&client).unwrap(), pe_bytes(pe_image::AMD64, true));
    }

    #[test]
    fn a_path_override_beats_the_cache() {
        let (manager, _) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        cache_valve_client(&manager.layout, true);
        let other = make_temp_dir().join("steamclient64.dll");
        std::fs::write(&other, pe_bytes(pe_image::AMD64, false)).unwrap();
        let path = other.to_string_lossy().into_owned();
        manager.prepare(&dxmt_environment(&manager, &[("MACNEUTRON_STEAMCLIENT64", &path)]), true).unwrap();
        assert_eq!(std::fs::read(steam_folder(&manager).join("steamclient64.dll")).unwrap(), pe_bytes(pe_image::AMD64, false));
    }

    /// `STEAM_COMPAT_LIBRARY_PATHS` and `STEAM_COMPAT_INSTALL_PATH` for a game in a fresh library named `name`: the
    /// steamapps folder, the game's folder, and the library's path.
    fn library_variables(name: &str, steamapps: &str) -> (String, String, String) {
        let lib = make_temp_dir().join(name);
        std::fs::create_dir_all(lib.join(format!("{steamapps}/common/Game"))).unwrap();
        let lib_path = lib.to_string_lossy().into_owned();
        let apps = format!("{lib_path}/{steamapps}");
        (apps.clone(), format!("{apps}/common/Game"), lib_path)
    }

    fn game_drive(manager: &PrefixManager) -> Option<String> {
        std::fs::read_link(manager.context.prefix().join("dosdevices/s:")).ok().map(|p| p.to_string_lossy().into_owned())
    }

    #[test]
    fn prepare_links_the_game_drive_with_or_without_the_bridge() {
        let runner = runner(wineboot_creating_prefix(0, 0.0));
        let (manager, _) = make_manager(&runner);
        let (apps, install, lib) = library_variables("Lib Ünï", "steamapps");
        let vars = [("STEAM_COMPAT_LIBRARY_PATHS", apps.as_str()), ("STEAM_COMPAT_INSTALL_PATH", install.as_str())];
        manager.prepare(&dxmt_environment(&manager, &vars), false).unwrap();
        assert_eq!(game_drive(&manager), posix::real_path(Path::new(&lib)));
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&dxmt_environment(&manager, &vars), true).unwrap();
        assert_eq!(game_drive(&manager), posix::real_path(Path::new(&lib)));
        assert_eq!(wineboot_count(&runner), 1);
    }

    #[test]
    fn prepare_logs_the_game_drive_notes() {
        let (manager, _) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        let (apps, install, _) = library_variables("Lib Ünï", "SteamApps");
        let vars = [("STEAM_COMPAT_LIBRARY_PATHS", apps.as_str()), ("STEAM_COMPAT_INSTALL_PATH", install.as_str())];
        manager.prepare(&dxmt_environment(&manager, &vars), false).unwrap();
        assert!(launcher_log(&manager)
            .contains(&format!("note: Proton: Error: unable to use parent for game drive, path {apps}")));
        assert_eq!(game_drive(&manager), Some(apps));
    }

    #[test]
    fn up_to_date_prefix_still_gets_the_steam_bridge() {
        // Prefixes prepared before the bridge existed (SMITE 2's on the maintainer's Mac) must get it too.
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        manager.prepare(&env, false).unwrap();
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&env, true).unwrap();
        assert!(steam_folder(&manager).join("steamclient64.dll").exists());
    }

    #[test]
    fn prefix_without_bridge_request_gets_no_steam_folder() {
        let (manager, env) = make_manager(&runner(wineboot_creating_prefix(0, 0.0)));
        install_fake_steam_bridge(&manager.layout);
        manager.prepare(&env, false).unwrap();
        assert!(!steam_folder(&manager).exists());
    }

    // Carrying the player's data out of a renamed Rosetta-era prefix

    type BootStatus = Box<dyn Fn() -> ExitStatus + Send + Sync>;
    type PrefixHook = Box<dyn Fn(&Path) + Send + Sync>;

    /// How `wineboot_making_user`'s wineboot behaves: the user it makes, `Documents` a link to `documents` when given
    /// (Wine links it to the Mac's), wineboot's exit (non-zero: nothing made), what `after_boot` adds to the new
    /// prefix, and `at_wait`, which sees the prefix when `wineserver -w` runs.
    struct Boot {
        user: &'static str,
        documents: Option<PathBuf>,
        boot_status: BootStatus,
        after_boot: PrefixHook,
        at_wait: PrefixHook,
    }

    impl Default for Boot {
        fn default() -> Self {
            Boot {
                user: "steamuser",
                documents: None,
                boot_status: Box::new(|| 0),
                after_boot: Box::new(|_| {}),
                at_wait: Box::new(|_| {}),
            }
        }
    }

    /// A FakeRunner whose `wineboot` makes a prefix like Wine's: `users/<user>` with Wine's own folders and `user.reg`.
    fn wineboot_making_user(boot: Boot) -> Arc<FakeRunner> {
        runner(FakeRunner::new(move |call| {
            let Some(path) = call.environment.get("WINEPREFIX") else { return 0 };
            let prefix = PathBuf::from(path);
            if call.arguments.first().map(String::as_str) == Some("wineboot") {
                let status = (boot.boot_status)();
                if status != 0 {
                    return status;
                }
                let home = prefix.join(format!("drive_c/users/{}", boot.user));
                for folder in ["AppData/Local/Microsoft", "AppData/Local/Temp", "AppData/LocalLow", "AppData/Roaming/Microsoft", "Saved Games"] {
                    let _ = std::fs::create_dir_all(home.join(folder));
                }
                let _ = std::fs::create_dir_all(prefix.join("drive_c/users/Public/Documents"));
                match &boot.documents {
                    Some(documents) => {
                        let _ = std::os::unix::fs::symlink(documents, home.join("Documents"));
                    }
                    None => {
                        let _ = std::fs::create_dir_all(home.join("Documents"));
                    }
                }
                let _ = std::fs::write(prefix.join("user.reg"), NEW_USER_REG);
                (boot.after_boot)(&prefix);
            } else if call.tool == "wineserver" && call.arguments == ["-w"] {
                (boot.at_wait)(&prefix);
            }
            0
        }))
    }

    fn wineboot_as(user: &'static str) -> Arc<FakeRunner> {
        wineboot_making_user(Boot { user, ..Boot::default() })
    }

    const NEW_USER_REG: &str = r#"WINE REGISTRY Version 2
;; All keys relative to \\User\\S-1-5-21-0-0-0-1000

#arch=win64

[Software\\Wine\\WineDbg] 1759600000
#time=1dc35f0a0000000
"ShowCrashDialog"=dword:00000000

[Software\\Taken Vendor\\Game] 1759600000
#time=1dc35f0a0000000
"Fresh"="new"
"#;

    /// What a Rosetta-era Wine left: Wine's and Windows' keys, Steam's, and the games' own.
    const OLD_USER_REG: &str = r#"WINE REGISTRY Version 2
;; All keys relative to \\User\\S-1-5-21-0-0-0-1000

#arch=win64

[Control Panel\\Desktop] 1700000000
"FontSmoothing"="2"

[Software\\Epic Games\\Unreal Engine\\Identifiers] 1700000000
#time=1da0000000000000
"AccountId"="abc"

[Software\\Microsoft\\Windows\\CurrentVersion\\Explorer] 1700000000
"Old"="1"

[Software\\Unity\\UnityEditor] 1700000000

[software\\unity technologies\\Game] 1700000000
#time=1da0000000000000
"Screenmanager Resolution Width_h182942802"=dword:00000a00
"data_h1"=hex:01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,10,11,12,13,14,15,16,\
  17,18,19,1a

[Software\\Classes\\Thing] 1700000000
@="x"

[Software\\Policies\\Thing] 1700000000

[Software\\Valve\\Steam] 1700000000
"SteamPath"="c:\\program files (x86)\\steam"

[Software\\Wine\\Direct3D] 1700000000
"renderer"="gl"

[Software\\taken vendor\\game] 1700000000
"Fresh"="old"
"#;

    /// A Rosetta-era `pfx` (no stamp) whose user `user` has `files` (paths under `drive_c/users/<user>/`, content =
    /// path).
    fn rosetta_prefix(manager: &PrefixManager, user: &str, files: &[&str]) {
        let home = manager.context.prefix().join(format!("drive_c/users/{user}"));
        for file in files {
            put(file, &home.join(file));
        }
        put(OLD_USER_REG, &manager.context.prefix().join("user.reg"));
    }

    fn new_prefix_file(manager: &PrefixManager, path: &str) -> Option<String> {
        std::fs::read_to_string(manager.context.prefix().join(format!("drive_c/users/{path}"))).ok()
    }

    fn make_carry_manager(runner: &Arc<FakeRunner>) -> (PrefixManager, Env) {
        make_manager_in(runner, "compat data/42", GraphicsBackend::Dxmt)
    }

    fn contents(folder: &Path) -> Vec<String> {
        std::fs::read_dir(folder).unwrap().flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect()
    }

    /// SMITE 2 keeps its settings in AppData/Local/SMITE2Alpha/Saved: a fresh prefix reset them (XeSS, ~4 FPS; with
    /// the player's settings the main lobby runs at ~36 FPS on both runtimes).
    #[test]
    fn renamed_prefix_carries_the_players_data_into_the_fresh_one() {
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        let files = [
            "AppData/Local/SMITE2Alpha/Saved/Config/Windows/GameUserSettings.ini",
            "AppData/Local/SMITE2Alpha/Saved/SaveGames/HWGameUserSettings.sav",
            "AppData/LocalLow/Studio/Game/prefs.json",
            "AppData/Roaming/Game/save 1.dat",
            "Documents/My Games/Game/settings.cfg",
            "Saved Games/Game/slot1.sav",
        ];
        rosetta_prefix(&manager, "steamuser", &files);
        manager.prepare(&env, false).unwrap();
        for file in files {
            assert_eq!(new_prefix_file(&manager, &format!("steamuser/{file}")).as_deref(), Some(file));
        }
        // Read-only: the old prefix still has everything.
        let old = manager.context.data_path.join("pfx.rosetta/drive_c/users/steamuser");
        for file in files {
            assert_eq!(read(&old.join(file)), file);
        }
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 6 files, 3 registry keys\n"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn carry_never_overwrites_and_skips_wines_own_folders() {
        let (manager, env) = make_carry_manager(&wineboot_making_user(Boot {
            after_boot: Box::new(|prefix| put("fresh", &prefix.join("drive_c/users/steamuser/AppData/Roaming/Game/a.cfg"))),
            ..Boot::default()
        }));
        rosetta_prefix(
            &manager,
            "steamuser",
            &[
                "AppData/Roaming/Game/a.cfg",
                "AppData/Roaming/Game/b.cfg",
                "AppData/Local/Temp/junk.tmp",
                "AppData/Local/Microsoft/Windows/x.dat",
                "AppData/Roaming/Microsoft/Crypto/y.dat",
            ],
        );
        put("public", &manager.context.prefix().join("drive_c/users/Public/Documents/p.txt"));
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Roaming/Game/a.cfg").as_deref(), Some("fresh"));
        assert_eq!(
            new_prefix_file(&manager, "steamuser/AppData/Roaming/Game/b.cfg").as_deref(),
            Some("AppData/Roaming/Game/b.cfg")
        );
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/Temp/junk.tmp"), None);
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/Microsoft/Windows/x.dat"), None);
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Roaming/Microsoft/Crypto/y.dat"), None);
        assert_eq!(new_prefix_file(&manager, "Public/Documents/p.txt"), None);
        assert!(launcher_log(&manager).contains("pfx.rosetta: 1 files, 3 registry keys\n"));
    }

    #[test]
    fn carry_maps_the_old_user_onto_the_new_one() {
        // wineboot names the user after $USER: a prefix made under another name gets the new single user's folder.
        let (manager, env) = make_carry_manager(&wineboot_as("player"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/Game/Saved/save.sav"]);
        manager.prepare(&env, false).unwrap();
        assert_eq!(
            new_prefix_file(&manager, "player/AppData/Local/Game/Saved/save.sav").as_deref(),
            Some("AppData/Local/Game/Saved/save.sav")
        );
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/Game/Saved/save.sav"), None);
    }

    #[test]
    fn carry_follows_no_links() {
        // Wine links Documents to the Mac's ~/Documents: the old link isn't followed, the new one isn't written through.
        let mac = make_temp_dir();
        put("mac", &mac.join("old/notes.txt"));
        let (manager, env) =
            make_carry_manager(&wineboot_making_user(Boot { documents: Some(mac.join("new")), ..Boot::default() }));
        std::fs::create_dir_all(mac.join("new")).unwrap();
        rosetta_prefix(&manager, "steamuser", &["Documents/My Games/Game/settings.cfg", "Saved Games/real.sav"]);
        let old = manager.context.prefix().join("drive_c/users/steamuser");
        std::os::unix::fs::symlink(mac.join("old"), old.join("Saved Games/linked")).unwrap();
        manager.prepare(&env, false).unwrap();
        assert!(contents(&mac.join("new")).is_empty());
        assert_eq!(new_prefix_file(&manager, "steamuser/Saved Games/real.sav").as_deref(), Some("Saved Games/real.sav"));
        assert_eq!(new_prefix_file(&manager, "steamuser/Saved Games/linked/notes.txt"), None);
        // Each link in the way counts: the new Documents, and Saved Games/linked.
        assert!(launcher_log(&manager).contains("pfx.rosetta: 1 files, 3 registry keys, 2 not carried\n"));
    }

    #[test]
    fn carry_appends_the_games_registry_keys_after_the_server_stopped() {
        let at_wait: Arc<Mutex<Option<String>>> = Arc::new(Mutex::new(None));
        let seen = at_wait.clone();
        let (manager, env) = make_carry_manager(&wineboot_making_user(Boot {
            at_wait: Box::new(move |prefix| *seen.lock().unwrap() = std::fs::read_to_string(prefix.join("user.reg")).ok()),
            ..Boot::default()
        }));
        rosetta_prefix(&manager, "steamuser", &[]);
        manager.prepare(&env, false).unwrap();
        // Untouched while wineserver ran.
        assert_eq!(at_wait.lock().unwrap().as_deref(), Some(NEW_USER_REG));
        let carried = r#"
[Software\\Epic Games\\Unreal Engine\\Identifiers] 1700000000
#time=1da0000000000000
"AccountId"="abc"

[Software\\Unity\\UnityEditor] 1700000000

[software\\unity technologies\\Game] 1700000000
#time=1da0000000000000
"Screenmanager Resolution Width_h182942802"=dword:00000a00
"data_h1"=hex:01,02,03,04,05,06,07,08,09,0a,0b,0c,0d,0e,0f,10,11,12,13,14,15,16,\
  17,18,19,1a
"#;
        assert_eq!(read(&manager.context.prefix().join("user.reg")), format!("{NEW_USER_REG}{carried}"));
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 0 files, 3 registry keys\n"));
    }

    #[test]
    fn unreadable_registry_is_counted_and_the_files_still_carry() {
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/Game/save.sav"]);
        // An old user.reg that can't be read.
        std::fs::remove_file(manager.context.prefix().join("user.reg")).unwrap();
        std::fs::create_dir_all(manager.context.prefix().join("user.reg")).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(
            new_prefix_file(&manager, "steamuser/AppData/Local/Game/save.sav").as_deref(),
            Some("AppData/Local/Game/save.sav")
        );
        assert!(launcher_log(&manager)
            .contains("note: carried the player's data from pfx.rosetta: 1 files, 0 registry keys, 1 not carried\n"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
        assert!(manager.context.data_path.join("pfx.rosetta/user.reg").exists());
    }

    #[test]
    fn preparing_in_place_carries_nothing() {
        // Only a rename carries: an arm64 prefix prepared again keeps its own data, and a pfx.rosetta beside it is left
        // be.
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        put("old", &manager.context.data_path.join("pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"));
        put("wine.app id0 msync=1", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav"), None);
        assert!(!launcher_log(&manager).contains("carried"));
    }

    #[test]
    fn unreadable_file_does_not_stop_the_carry() {
        use std::os::unix::fs::PermissionsExt;
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/A/locked.sav", "AppData/Local/B/ok.sav", "Saved Games/G/s.sav"]);
        let locked = manager.context.prefix().join("drive_c/users/steamuser/AppData/Local/A/locked.sav");
        std::fs::set_permissions(&locked, std::fs::Permissions::from_mode(0o000)).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/A/locked.sav"), None);
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/B/ok.sav").as_deref(), Some("AppData/Local/B/ok.sav"));
        assert_eq!(new_prefix_file(&manager, "steamuser/Saved Games/G/s.sav").as_deref(), Some("Saved Games/G/s.sav"));
        assert!(launcher_log(&manager)
            .contains("note: carried the player's data from pfx.rosetta: 2 files, 3 registry keys, 1 not carried\n"));
    }

    #[test]
    fn linked_app_data_in_the_old_prefix_is_not_followed() {
        let mac = make_temp_dir();
        put("mac", &mac.join("AppData/Local/G/s.sav"));
        put("mac", &mac.join("user/AppData/Local/G/u.sav"));
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["Saved Games/G/real.sav"]);
        let users = manager.context.prefix().join("drive_c/users");
        std::os::unix::fs::symlink(mac.join("AppData"), users.join("steamuser/AppData")).unwrap();
        std::os::unix::fs::symlink(mac.join("user"), users.join("linkeduser")).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav"), None);
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/u.sav"), None);
        assert_eq!(new_prefix_file(&manager, "linkeduser/AppData/Local/G/u.sav"), None);
        assert_eq!(new_prefix_file(&manager, "steamuser/Saved Games/G/real.sav").as_deref(), Some("Saved Games/G/real.sav"));
        // The linked user folder, and AppData's three folders.
        assert!(launcher_log(&manager).contains("pfx.rosetta: 1 files, 3 registry keys, 4 not carried\n"));
    }

    #[test]
    fn linked_app_data_in_the_new_prefix_is_not_written_through() {
        let mac = make_temp_dir();
        let target = mac.clone();
        let (manager, env) = make_carry_manager(&wineboot_making_user(Boot {
            after_boot: Box::new(move |prefix| {
                let app_data = prefix.join("drive_c/users/steamuser/AppData");
                let _ = std::fs::remove_dir_all(&app_data);
                let _ = std::os::unix::fs::symlink(&target, &app_data);
            }),
            ..Boot::default()
        }));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav", "AppData/Roaming/G/r.sav", "Saved Games/G/real.sav"]);
        manager.prepare(&env, false).unwrap();
        assert!(contents(&mac).is_empty());
        assert_eq!(new_prefix_file(&manager, "steamuser/Saved Games/G/real.sav").as_deref(), Some("Saved Games/G/real.sav"));
        assert!(launcher_log(&manager).contains("pfx.rosetta: 1 files, 3 registry keys, 2 not carried\n"));
    }

    /// A wineboot that fails the first time only.
    fn failing_first_boot() -> BootStatus {
        let boots = Mutex::new(0);
        Box::new(move || {
            let mut boots = boots.lock().unwrap();
            *boots += 1;
            if *boots == 1 { 1 } else { 0 }
        })
    }

    /// A first preparation stopped after the rename (here: wineboot failed) is retried in place, and carries then.
    fn failed_first_preparation() -> (PrefixManager, Env) {
        let (manager, env) =
            make_carry_manager(&wineboot_making_user(Boot { boot_status: failing_first_boot(), ..Boot::default() }));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav"]);
        assert_eq!(manager.prepare(&env, false).unwrap_err(), PrefixError::WinebootFailed(1));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app preparing"));
        assert!(!launcher_log(&manager).contains("carried"));
        (manager, env)
    }

    #[test]
    fn retry_after_a_failed_wineboot_carries() {
        let (manager, env) = failed_first_preparation();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav").as_deref(), Some("AppData/Local/G/s.sav"));
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"));
        assert!(manager.context.data_path.join("player-data-carried").exists());
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn later_launch_does_not_carry_again() {
        let (manager, env) = failed_first_preparation();
        manager.prepare(&env, false).unwrap();
        // The player deletes the carried save; another preparation is stopped and retried.
        std::fs::remove_file(manager.context.prefix().join("drive_c/users/steamuser/AppData/Local/G/s.sav")).unwrap();
        put("wine.app preparing", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav"), None);
        assert_eq!(launcher_log(&manager).matches("carried the player's data").count(), 1);
    }

    #[test]
    fn up_to_date_prefix_never_carries() {
        let runner = wineboot_as("steamuser");
        let (manager, env) = make_carry_manager(&runner);
        put("old", &manager.context.data_path.join("pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"));
        std::fs::create_dir_all(manager.context.prefix().join("drive_c/users/steamuser")).unwrap();
        put("wine.app id1 msync=1", &manager.context.version_file());
        manager.prepare(&env, false).unwrap();
        assert_eq!(wineboot_count(&runner), 0);
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav"), None);
        assert!(!launcher_log(&manager).contains("carried"));
    }

    #[test]
    fn wines_link_to_the_macs_documents_is_not_counted() {
        // Wine links the old user's Documents to the Mac's ~/Documents: that data is on the Mac already. (Fake folder.)
        let mac = make_temp_dir();
        put("mac", &mac.join("Documents/notes.txt"));
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav"]);
        std::os::unix::fs::symlink(mac.join("Documents"), manager.context.prefix().join("drive_c/users/steamuser/Documents"))
            .unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/Documents/notes.txt"), None);
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"));
    }

    #[test]
    fn each_rename_gets_its_own_carry() {
        // An earlier rename's marker doesn't stop the retry of a later rename whose preparation was stopped.
        let (manager, env) =
            make_carry_manager(&wineboot_making_user(Boot { boot_status: failing_first_boot(), ..Boot::default() }));
        put("", &manager.context.data_path.join("player-data-carried"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav"]);
        assert_eq!(manager.prepare(&env, false).unwrap_err(), PrefixError::WinebootFailed(1));
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav").as_deref(), Some("AppData/Local/G/s.sav"));
    }

    #[test]
    fn stopped_reprepare_of_an_arm64_prefix_carries_nothing() {
        // Task FR (#2): a runtime update prepares an arm64 prefix in place; stopped, its retry finds the preparing
        // stamp and a pfx.rosetta an older launcher renamed without carrying (no marker). The player has been playing
        // on the arm64 prefix since: nothing comes back from the old one.
        let (manager, env) =
            make_carry_manager(&wineboot_making_user(Boot { boot_status: failing_first_boot(), ..Boot::default() }));
        put("old", &manager.context.data_path.join("pfx.rosetta/drive_c/users/steamuser/AppData/Local/G/s.sav"));
        put(OLD_USER_REG, &manager.context.data_path.join("pfx.rosetta/user.reg"));
        std::fs::create_dir_all(manager.context.prefix().join("drive_c/users/steamuser")).unwrap();
        put("wine.app id0 msync=1", &manager.context.version_file());
        assert_eq!(manager.prepare(&env, false).unwrap_err(), PrefixError::WinebootFailed(1));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app preparing"));
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav"), None);
        assert!(!launcher_log(&manager).contains("carried"));
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn launch_stopped_between_the_rename_and_the_stamp_still_carries() {
        // Task FR (#6): the rename is recorded before the move, so a launch that ends right after the move, before the
        // preparing stamp (here: the stamp can't be written), still carries on the next launch.
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav"]);
        std::fs::create_dir_all(manager.context.version_file()).unwrap();
        assert!(manager.prepare(&env, false).is_err());
        assert!(manager.context.data_path.join("pfx.rosetta/user.reg").exists());
        std::fs::remove_dir_all(manager.context.version_file()).unwrap();
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav").as_deref(), Some("AppData/Local/G/s.sav"));
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"));
    }

    #[test]
    fn a_copy_cut_short_leaves_only_its_temporary_name() {
        // Task FR (#7): each file is copied under a temporary name and renamed into place, so a carry killed mid-copy
        // (a byte copy off APFS) leaves no part-file under the real name for the retry to keep. Its leftover (made here
        // by hand: a process can't be killed mid-copy in a test) is replaced by the retry's copy and gone after it.
        let (manager, env) = failed_first_preparation();
        let leftover = manager.context.prefix().join("drive_c/users/steamuser/AppData/Local/G/.macneutron-carry");
        put("trunc", &leftover);
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "steamuser/AppData/Local/G/s.sav").as_deref(), Some("AppData/Local/G/s.sav"));
        assert!(!leftover.exists());
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"));

        // Task FR2: a folder (not ours to remove) at the temporary name stops that copy; the file never reaches its
        // real name by another way, and it is counted.
        let (blocked, blocked_env) = failed_first_preparation();
        put("game's", &blocked.context.prefix().join("drive_c/users/steamuser/AppData/Local/G/.macneutron-carry/x"));
        blocked.prepare(&blocked_env, false).unwrap();
        assert_eq!(new_prefix_file(&blocked, "steamuser/AppData/Local/G/s.sav"), None);
        assert_eq!(new_prefix_file(&blocked, "steamuser/AppData/Local/G/.macneutron-carry/x").as_deref(), Some("game's"));
        assert!(launcher_log(&blocked)
            .contains("note: carried the player's data from pfx.rosetta: 0 files, 3 registry keys, 1 not carried\n"));
    }

    #[test]
    fn a_pending_record_that_cant_become_the_marker_is_removed() {
        // Task FR2: the pending record outliving its carry would carry that prefix again into a live one (an arm64
        // re-preparation stopped and retried); the marker is never read, so a failed rename unlinks the record.
        let (manager, env) = make_carry_manager(&wineboot_as("steamuser"));
        rosetta_prefix(&manager, "steamuser", &["AppData/Local/G/s.sav"]);
        put("", &manager.context.data_path.join("player-data-carried/x"));
        manager.prepare(&env, false).unwrap();
        assert!(launcher_log(&manager).contains("note: carried the player's data from pfx.rosetta: 1 files, 3 registry keys\n"));
        assert!(!manager.context.data_path.join("player-data-pending").exists());
    }

    #[test]
    fn several_old_users_all_go_to_the_one_wine_reads() {
        // Task FR (#8): wineboot makes one user (named after $USER); every old user's files go there. Task FR2: the old
        // user of that name (the one Wine read) wins a file others have, then the first by name.
        let (manager, env) = make_carry_manager(&wineboot_as("player"));
        // Not `steamuser`: beside another profile that one is Steam's own folder, handed back to the new prefix (I3).
        rosetta_prefix(&manager, "olduser", &["AppData/Local/G/both.sav", "AppData/Local/G/steam.sav"]);
        rosetta_prefix(&manager, "macuser", &["AppData/Local/G/both.sav", "Saved Games/G/mac.sav"]);
        rosetta_prefix(&manager, "player", &["AppData/Local/G/live.sav"]);
        rosetta_prefix(&manager, "aaa", &["AppData/Local/G/live.sav"]);
        put("macuser's", &manager.context.prefix().join("drive_c/users/macuser/AppData/Local/G/both.sav"));
        put("player's", &manager.context.prefix().join("drive_c/users/player/AppData/Local/G/live.sav"));
        manager.prepare(&env, false).unwrap();
        assert_eq!(new_prefix_file(&manager, "player/AppData/Local/G/live.sav").as_deref(), Some("player's"));
        assert_eq!(new_prefix_file(&manager, "player/AppData/Local/G/both.sav").as_deref(), Some("macuser's"));
        assert_eq!(
            new_prefix_file(&manager, "player/AppData/Local/G/steam.sav").as_deref(),
            Some("AppData/Local/G/steam.sav")
        );
        assert_eq!(new_prefix_file(&manager, "player/Saved Games/G/mac.sav").as_deref(), Some("Saved Games/G/mac.sav"));
        assert_eq!(new_prefix_file(&manager, "olduser/AppData/Local/G/steam.sav"), None);
        assert_eq!(new_prefix_file(&manager, "macuser/Saved Games/G/mac.sav"), None);
    }

    // Steam Cloud (spec §4.2, §4.3)

    /// A launch by Steam for the Mac user `u`, whose Wine profile is `users/u`.
    fn make_cloud_manager() -> (PrefixManager, Env) {
        let (manager, mut env) = make_carry_manager(&wineboot_as("u"));
        env.insert("USER".to_string(), "u".to_string());
        (manager, env)
    }

    #[test]
    fn a_seeded_prefix_is_prepared_in_place() {
        // Steam's cloud download made pfx before MacNeutron ever ran: no stamp, no user.reg.
        let (manager, env) = make_cloud_manager();
        let seeded = manager.context.prefix().join("drive_c/users/steamuser/Local Settings/Application Data/G/s.sav");
        put("cloud", &seeded);
        manager.prepare(&env, false).unwrap();
        assert!(!manager.context.data_path.join("pfx.rosetta").exists());
        assert_eq!(new_prefix_file(&manager, "u/AppData/Local/G/s.sav").as_deref(), Some("cloud"));
        assert_eq!(read(&seeded), "cloud"); // the path Steam watches, now through the links
        assert_eq!(stamp(&manager).as_deref(), Some("wine.app id1 msync=1"));
    }

    #[test]
    fn links_are_kept_on_every_launch() {
        let (manager, env) = make_cloud_manager();
        manager.prepare(&env, false).unwrap();
        let steamuser = manager.context.prefix().join("drive_c/users/steamuser");
        assert_eq!(std::fs::read_link(&steamuser).unwrap(), PathBuf::from("u"));
        std::fs::remove_file(&steamuser).unwrap();
        manager.prepare(&env, false).unwrap(); // up to date: no wineboot
        assert_eq!(std::fs::read_link(&steamuser).unwrap(), PathBuf::from("u"));
    }

    #[test]
    fn a_cloud_conflict_is_reported_and_logged() {
        let (manager, env) = make_cloud_manager();
        manager.prepare(&env, false).unwrap();
        let users = manager.context.prefix().join("drive_c/users");
        std::fs::remove_file(users.join("steamuser")).unwrap(); // as if Steam had made a real folder
        put("local", &users.join("u/AppData/Local/G/s.sav"));
        put("cloud", &users.join("steamuser/Local Settings/Application Data/G/s.sav"));
        let report = manager.prepare(&env, false).unwrap();
        assert_eq!(report.conflicts, 1);
        assert!(launcher_log(&manager).contains("note: kept steamuser: 1 of its files differ"));
    }

    #[test]
    fn a_renamed_rosetta_prefix_leaves_steams_cloud_files_where_steam_watches() {
        // A Rosetta-era prefix (profile `crossover`, user.reg, no wine.app stamp) that Steam's cloud download has since
        // written into: the rename must not take Steam's files out from under its exit pass (final review, I3).
        let (manager, env) = make_cloud_manager();
        rosetta_prefix(&manager, "crossover", &["AppData/Roaming/G/local.cfg"]);
        let watched = manager.context.prefix().join("drive_c/users/steamuser/Local Settings/Application Data/G/s.sav");
        put("cloud", &watched);
        manager.prepare(&env, false).unwrap();
        assert_eq!(read(&watched), "cloud");
        assert_eq!(new_prefix_file(&manager, "u/AppData/Local/G/s.sav").as_deref(), Some("cloud"));
        // Still carried.
        assert_eq!(
            new_prefix_file(&manager, "u/AppData/Roaming/G/local.cfg").as_deref(),
            Some("AppData/Roaming/G/local.cfg")
        );
        let old = manager.context.data_path.join("pfx.rosetta/drive_c/users");
        assert!(!old.join("steamuser").exists());
        assert!(old.join("crossover/AppData/Roaming/G/local.cfg").exists());
    }
}
