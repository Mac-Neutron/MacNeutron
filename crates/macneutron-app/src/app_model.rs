//! Everything the menu, setup, games and settings views show, and every action they take (AppModel.swift).

use std::collections::{BTreeMap, BTreeSet};
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex, Weak};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use macneutron_core::runtime_installer::{default_identity, default_running_executables, standard_runner};
use macneutron_core::steam_watcher::{SteamEvent, SteamWatcher};
use macneutron_core::vm_downloads::standard_fetch;
use macneutron_core::{
    app_info, AppInfo, CodeIdentity, GameSettings, GameSettingsStore, LaunchOptions, LauncherLog, MappingPlanner,
    MicroVM, OrphanPrefix, OrphanPrefixes, RunAs, RuntimeInstallError, RuntimeInstallOutcome, RuntimeInstaller,
    SteamLocation, SteamPlayError, SteamPlayMode, SteamPlayStatus, SystemProcessRunner, ToolLayout, ToolMapping,
    UninstalledDrives, VMSetup, VMToolsInstallOutcome, VMToolsInstaller, ValveClient,
};

use crate::OnChange;

/// One row of the games window.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GameRow {
    pub app: AppInfo,
    pub installed: bool,
    pub settings: GameSettings,
    /// The game's own launch options in Steam (read only); None when it has none.
    pub steam_launch_options: Option<String>,
}

impl GameRow {
    pub fn id(&self) -> u32 {
        self.app.app_id
    }

    pub fn name(&self) -> String {
        if self.app.name.is_empty() {
            format!("App {}", self.app.app_id)
        } else {
            self.app.name.clone()
        }
    }

    pub fn is_dual_platform(&self) -> bool {
        self.app.oslist.contains("macos") && self.app.oslist.contains("windows")
    }

    pub fn runs_with_macneutron(&self) -> bool {
        !self.app.oslist.contains("macos") || (self.is_dual_platform() && self.settings.run_as == Some(RunAs::Windows))
    }
}

/// `SMAppService.Status`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LoginItemStatus {
    NotRegistered,
    Enabled,
    RequiresApproval,
    NotFound,
}

/// The app's login item; a seam so tests don't register the real app with macOS.
pub struct LoginItem {
    pub status: Box<dyn Fn() -> LoginItemStatus + Send + Sync>,
    pub register: Box<dyn Fn() -> Result<(), String> + Send + Sync>,
    pub unregister: Box<dyn Fn() -> Result<(), String> + Send + Sync>,
}

impl Default for LoginItem {
    /// ponytail: the AppKit layer supplies this (`SMAppService.mainApp`); until then it's never registered.
    fn default() -> Self {
        let unsupported = || Err("login items need the AppKit layer".to_string());
        LoginItem {
            status: Box::new(|| LoginItemStatus::NotRegistered),
            register: Box::new(unsupported),
            unregister: Box::new(unsupported),
        }
    }
}

/// The `wine.app` in the tool folder: its `CFBundleShortVersionString` and identity (CDHash).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstalledRuntime {
    pub version: String,
    pub identity: String,
}

impl InstalledRuntime {
    /// None when `wine.app` is missing, has no version or isn't signed.
    pub fn read(layout: &ToolLayout, identity: &dyn Fn(&Path) -> Option<String>) -> Option<InstalledRuntime> {
        let version = layout.runtime_version()?;
        let identity = identity(&layout.wine_app())?;
        Some(InstalledRuntime { version, identity })
    }

    pub fn label(&self) -> String {
        ToolLayout::runtime_label(Some(&self.version), Some(&self.identity))
    }
}

/// Everything `refresh` reads from disk (parsing the app list and sizing leftover prefixes can take a while; checking
/// Steam spawns a process).
#[derive(Debug, Clone)]
pub struct Snapshot {
    pub runtime: Option<InstalledRuntime>,
    pub apps: Vec<AppInfo>,
    pub app_info_error: Option<String>,
    pub installed: BTreeSet<u32>,
    pub settings: BTreeMap<String, GameSettings>,
    pub orphans: Vec<OrphanPrefix>,
    pub status: SteamPlayStatus,
    /// Each game's launch options in Steam, by app ID.
    pub steam_launch_options: BTreeMap<u32, String>,
}

/// `RuntimeInstaller::install(wine_app, layout, launcher_binary, force)`.
pub type RuntimeInstall =
    Arc<dyn Fn(&Path, &ToolLayout, &Path, bool) -> Result<RuntimeInstallOutcome, RuntimeInstallError> + Send + Sync>;
/// `VMToolsInstaller::install(tools, vm)`.
pub type VMToolsInstall = Arc<dyn Fn(&Path, &Path) -> Result<VMToolsInstallOutcome, RuntimeInstallError> + Send + Sync>;
/// A bundle's identity (`CodeIdentity::of`).
pub type Identity = Arc<dyn Fn(&Path) -> Option<String> + Send + Sync>;

/// What `AppModel::new` takes: Swift's init parameters. `standard()` is the app's own; tests replace what they fake.
pub struct AppModelConfig {
    pub steam: SteamLocation,
    pub layout: ToolLayout,
    pub mode: SteamPlayMode,
    pub store: GameSettingsStore,
    pub login_item: Arc<LoginItem>,
    /// MacNeutron.app's `Contents/Helpers/wine.app`; None in dev runs and tests: nothing to install.
    pub wine_app_source: Option<PathBuf>,
    pub launcher_binary: Option<PathBuf>,
    pub installer: RuntimeInstall,
    pub identity: Identity,
    /// None: `ValveClient` on `layout.valve_cache()`, fetching from Valve.
    pub valve_client: Option<ValveClient>,
    /// `CFBundleVersion`: the automatic Valve check runs once per app version (spec D4).
    pub app_version: String,
    /// MacNeutron.app's `Contents/Helpers/vm`: the microVM's host tools; None in dev runs and tests.
    pub vm_tools: Option<PathBuf>,
    /// The VM folder: None unless the caller names it (the app passes `MicroVM::standard_root()` where it ships the
    /// VM's tools), so a test never reaches the real one.
    pub vm_root: Option<PathBuf>,
    pub vm_installer: VMToolsInstall,
    /// MacNeutron.app's `Contents/Resources/vm-manifest.json`; None in dev runs and tests.
    pub vm_manifest: Option<PathBuf>,
    /// The CLI whose `vm-setup` builds the VM (`AppModel::helper()` unless a test gives its own).
    pub vm_helper: Option<PathBuf>,
    pub vm_setup_log: PathBuf,
    pub vm_retry_interval: Duration,
    /// launcher.log: a game Steam uninstalled whose VM drive went (UninstalledDrives).
    pub log: LauncherLog,
    /// The Steam watcher's tick (Swift: 3 s, started by init); None starts no watcher, as in tests, which drive
    /// `poll_runtime` themselves.
    pub poll: Option<Duration>,
}

impl AppModelConfig {
    /// Swift's defaults: the real Steam, tool folder, settings, login item and bundled files.
    pub fn standard() -> Self {
        let scripts = AppModel::bundled_vm_scripts();
        AppModelConfig {
            steam: SteamLocation::default(),
            layout: ToolLayout::new(ToolLayout::default_root()),
            mode: SteamPlayMode::default(),
            store: GameSettingsStore::default(),
            login_item: Arc::new(LoginItem::default()),
            wine_app_source: AppModel::bundled_wine_app(),
            launcher_binary: None,
            installer: Arc::new(|source, layout, launcher, force| {
                let runner = standard_runner();
                let log = LauncherLog::standard();
                let installed = RuntimeInstaller::install(
                    source,
                    layout,
                    launcher,
                    None,
                    force,
                    &runner,
                    &default_running_executables,
                    default_identity,
                    &log,
                );
                installed.map(|(outcome, _)| outcome)
            }),
            identity: Arc::new(CodeIdentity::of),
            valve_client: None,
            app_version: AppModel::app_version(),
            vm_tools: AppModel::bundled_vm_tools(),
            vm_root: None,
            vm_installer: Arc::new(move |tools, vm| {
                VMToolsInstaller::install(tools, scripts.as_deref(), vm, &standard_runner(), &default_running_executables)
            }),
            vm_manifest: AppModel::bundled_vm_manifest(),
            vm_helper: None,
            vm_setup_log: LauncherLog::standard().directory.join("vm-setup.log"),
            vm_retry_interval: Duration::from_secs(30),
            log: LauncherLog::standard(),
            poll: Some(Duration::from_secs(3)),
        }
    }
}

/// What the views show. Read whole with `AppModel::state()`.
#[derive(Debug, Clone)]
pub struct State {
    pub runtime: Option<InstalledRuntime>,
    /// Setup's runtime text while installing, deferred or failed; None once installed.
    pub runtime_notice: Option<String>,
    /// Never `busy`: a deferred install retries for a whole game session, and `busy` blocks Steam actions.
    pub installing: bool,
    /// The version of Valve's files the launcher puts in prefixes; None when none is downloaded.
    pub valve_version: Option<String>,
    /// "Downloading…" while working, the failure after one; None otherwise.
    pub valve_notice: Option<String>,
    pub valve_updating: bool,
    pub games: Vec<GameRow>,
    pub orphans: Vec<OrphanPrefix>,
    pub app_info_error: Option<String>,
    pub login_item_status: LoginItemStatus,
    pub busy: Option<String>,
    pub error_message: Option<String>,
    /// A VM ran from the folder at the last tools install: the poll retries it.
    pub vm_tools_deferred: bool,
    /// The game VM's setup, as Settings and Setup show it; None when none ran.
    pub vm_notice: Option<String>,
    pub vm_setup_running: bool,
    /// The last setup failed: nothing retries it but Retry or Rebuild.
    pub vm_setup_failed: bool,
    /// Bumped by every change the model makes on disk.
    pub generation: u64,
    vm_tools_installing: bool,
    /// The last setup exited 75 (a VM ran from the folder): the poll retries it, every `vm_retry_interval`.
    vm_setup_deferred: bool,
    vm_setup_deferred_at: Option<Instant>,
    /// A deferred install's `force`, retried by the poll.
    deferred_force: Option<bool>,
    /// The last install threw: the poll doesn't retry it on `runtime-damaged` (Repair does).
    install_failed: bool,
    /// What the last refresh read; `status()` adds the restart an install asked for.
    read_status: SteamPlayStatus,
    /// An install replaced the tools' Rosetta-era entry points (`proton`, `passthrough.sh`) while Steam ran, and
    /// Steam may keep the manifests it read at its start (Ruling 27). Cleared when Steam quits or is restarted here.
    tools_changed_under_steam: bool,
    apps: Vec<AppInfo>,
}

impl State {
    pub fn status(&self) -> SteamPlayStatus {
        match &self.read_status {
            SteamPlayStatus::On if self.tools_changed_under_steam => SteamPlayStatus::RestartNeeded(1),
            SteamPlayStatus::RestartNeeded(count) if self.tools_changed_under_steam => {
                SteamPlayStatus::RestartNeeded(count + 1)
            }
            status => status.clone(),
        }
    }

    /// On, or waiting for the user's approval in System Settings.
    pub fn launches_at_login(&self) -> bool {
        matches!(self.login_item_status, LoginItemStatus::Enabled | LoginItemStatus::RequiresApproval)
    }
}

pub struct AppModel {
    pub steam: SteamLocation,
    pub layout: ToolLayout,
    pub mode: SteamPlayMode,
    pub store: GameSettingsStore,
    pub login_item: Arc<LoginItem>,
    pub wine_app_source: Option<PathBuf>,
    /// Valve's signed Windows client files in `layout.valve_cache()`.
    pub valve_client: ValveClient,
    pub vm_tools: Option<PathBuf>,
    pub vm_root: Option<PathBuf>,
    pub vm_manifest: Option<PathBuf>,
    pub log: LauncherLog,
    launcher_override: Option<PathBuf>,
    installer: RuntimeInstall,
    identity: Identity,
    app_version: String,
    vm_installer: VMToolsInstall,
    vm_helper: Option<PathBuf>,
    vm_setup_log: PathBuf,
    vm_retry_interval: Duration,
    poll: Option<Duration>,
    state: Mutex<State>,
    on_change: OnChange,
    /// The install started by `start`, the Valve check a runtime install starts, the setup a tools install starts.
    install_task: Mutex<Option<JoinHandle<()>>>,
    valve_task: Mutex<Option<JoinHandle<()>>>,
    vm_setup_task: Mutex<Option<JoinHandle<()>>>,
    /// The running setup's pid, and whether the app is quitting (a setup started then is stopped at once).
    vm_process: Mutex<(Option<u32>, bool)>,
    me: Weak<AppModel>,
}

impl AppModel {
    /// Reads what the scene decides at launch whether to open the setup window on; `start` begins the rest.
    pub fn new(config: AppModelConfig) -> Arc<AppModel> {
        let valve_client = config.valve_client.unwrap_or_else(|| {
            ValveClient::new(config.layout.valve_cache(), standard_fetch(), Arc::new(SystemProcessRunner::new()))
        });
        let state = State {
            runtime: InstalledRuntime::read(&config.layout, &*config.identity),
            runtime_notice: None,
            installing: false,
            valve_version: ValveClient::installed(&valve_client.cache).map(|install| install.version),
            valve_notice: None,
            valve_updating: false,
            games: Vec::new(),
            orphans: Vec::new(),
            app_info_error: None,
            login_item_status: LoginItemStatus::NotRegistered,
            busy: None,
            error_message: None,
            vm_tools_deferred: false,
            vm_notice: None,
            vm_setup_running: false,
            vm_setup_failed: false,
            generation: 0,
            vm_tools_installing: false,
            vm_setup_deferred: false,
            vm_setup_deferred_at: None,
            deferred_force: None,
            install_failed: false,
            read_status: SteamPlayStatus::Off,
            tools_changed_under_steam: false,
            apps: Vec::new(),
        };
        Arc::new_cyclic(|me| AppModel {
            steam: config.steam,
            layout: config.layout,
            mode: config.mode,
            store: config.store,
            login_item: config.login_item,
            wine_app_source: config.wine_app_source,
            valve_client,
            vm_tools: config.vm_tools,
            vm_root: config.vm_root,
            vm_manifest: config.vm_manifest,
            log: config.log,
            launcher_override: config.launcher_binary,
            installer: config.installer,
            identity: config.identity,
            app_version: config.app_version,
            vm_installer: config.vm_installer,
            vm_helper: config.vm_helper,
            vm_setup_log: config.vm_setup_log,
            vm_retry_interval: config.vm_retry_interval,
            poll: config.poll,
            state: Mutex::new(state),
            on_change: OnChange::default(),
            install_task: Mutex::new(None),
            valve_task: Mutex::new(None),
            vm_setup_task: Mutex::new(None),
            vm_process: Mutex::new((None, false)),
            me: me.clone(),
        })
    }

    /// What Swift's init started: a refresh, the bundled runtime's install, and the Steam watcher (with `poll`).
    pub fn start(&self) {
        self.spawn(|model| model.refresh());
        *self.install_task.lock().unwrap() = Some(self.spawn(|model| model.install_runtime(false)));
        if let Some(interval) = self.poll {
            self.watch_steam(interval);
        }
    }

    /// A copy of everything the views show.
    pub fn state(&self) -> State {
        self.state.lock().unwrap().clone()
    }

    /// The UI's redraw hook (see the crate docs).
    pub fn set_on_change(&self, listener: impl Fn() + Send + Sync + 'static) {
        self.on_change.set(listener);
    }

    /// The error's Dismiss button.
    pub fn dismiss_error(&self) {
        self.mutate(|state| state.error_message = None);
    }

    /// `NSApplication.willTerminateNotification`'s observer: quitting ends a setup's child, and the next start resumes
    /// from its downloads. The AppKit layer calls it.
    pub fn will_terminate(&self) {
        let mut process = self.vm_process.lock().unwrap();
        process.1 = true;
        if let Some(pid) = process.0 {
            terminate(pid);
        }
    }

    pub fn wait_install(&self) {
        join(&self.install_task);
    }

    pub fn wait_valve(&self) {
        join(&self.valve_task);
    }

    pub fn wait_vm_setup(&self) {
        join(&self.vm_setup_task);
    }

    pub fn setup_complete(&self) -> bool {
        self.state.lock().unwrap().runtime.is_some() && self.mode.is_wanted()
    }

    pub fn steam_installed(&self) -> bool {
        self.steam.is_installed()
    }

    /// `Bundle.main.bundleURL`: the .app when this runs from `Contents/MacOS`, else the executable's folder.
    fn bundle_url() -> PathBuf {
        let executable = std::env::current_exe().unwrap_or_default();
        let folder = executable.parent().map(Path::to_path_buf).unwrap_or_default();
        if folder.ends_with("Contents/MacOS") {
            folder.parent().and_then(Path::parent).map(Path::to_path_buf).unwrap_or(folder)
        } else {
            folder
        }
    }

    /// `path` in the app bundle, when it exists.
    fn bundled(path: &str) -> Option<PathBuf> {
        let url = Self::bundle_url().join(path);
        url.exists().then_some(url)
    }

    /// `CFBundleVersion`, "dev" outside an app bundle.
    pub fn app_version() -> String {
        let info = Self::bundle_url().join("Contents/Info.plist");
        plist::Value::from_file(info)
            .ok()
            .and_then(|value| value.as_dictionary()?.get("CFBundleVersion")?.as_string().map(str::to_string))
            .unwrap_or_else(|| "dev".to_string())
    }

    /// `Contents/Resources/vm-manifest.json`: the image set this app's VM needs.
    pub fn bundled_vm_manifest() -> Option<PathBuf> {
        Self::bundled("Contents/Resources/vm-manifest.json")
    }

    /// `Contents/Resources/vm`: the VM tools' scripts (`VMToolsInstaller::SCRIPTS`).
    pub fn bundled_vm_scripts() -> Option<PathBuf> {
        Self::bundled("Contents/Resources/vm")
    }

    pub fn bundled_vm_tools() -> Option<PathBuf> {
        Self::bundled("Contents/Helpers/vm")
    }

    pub fn bundled_wine_app() -> Option<PathBuf> {
        Self::bundled("Contents/Helpers/wine.app")
    }

    /// The `macneutron` CLI inside the app bundle (or next to the executable during development).
    pub fn helper() -> PathBuf {
        let executable = std::env::current_exe()
            .unwrap_or_else(|_| PathBuf::from(std::env::args().next().unwrap_or_default()));
        let folder = executable.parent().map(Path::to_path_buf).unwrap_or_default();
        let bundled = folder.parent().map(|contents| contents.join("Helpers/macneutron"));
        match bundled {
            Some(bundled) if bundled.exists() => bundled,
            _ => folder.join("macneutron"),
        }
    }

    pub fn refresh(&self) {
        let (mine, fallback) = {
            let state = self.state.lock().unwrap();
            (state.generation, state.apps.clone())
        };
        let snapshot = Self::load_snapshot(&self.steam, &self.layout, &self.store, &self.mode, &fallback, &*self.identity);
        self.apply(snapshot, mine);
        // A game Steam uninstalled loses its VM drive, as Linux Steam deletes its Proton prefix (spec
        // 2026-10-24-macneutron-install-scripts-design §4.9): the launcher only runs during launches, the app all along.
        if let Some(root) = &self.vm_root {
            for line in UninstalledDrives::remove(root, &self.steam) {
                self.log.summary(&line);
            }
        }
    }

    /// Drops a snapshot read before a settings change or cleanup: it would put the old state back.
    pub fn apply(&self, snapshot: Snapshot, mine: u64) {
        let login = (self.login_item.status)();
        self.mutate(|state| {
            if mine != state.generation {
                return;
            }
            state.runtime = snapshot.runtime;
            state.app_info_error = snapshot.app_info_error;
            let mut games: Vec<GameRow> = snapshot
                .apps
                .iter()
                .filter(|app| {
                    MappingPlanner::is_mappable_type(&app.r#type)
                        && (app.oslist.contains("windows") || app.oslist.contains("macos"))
                })
                .map(|app| GameRow {
                    app: app.clone(),
                    installed: snapshot.installed.contains(&app.app_id),
                    settings: snapshot.settings.get(&app.app_id.to_string()).cloned().unwrap_or_default(),
                    steam_launch_options: snapshot.steam_launch_options.get(&app.app_id).cloned(),
                })
                .collect();
            games.sort_by_cached_key(|row| (!row.installed, row.name().to_lowercase()));
            state.games = games;
            state.apps = snapshot.apps;
            state.orphans = snapshot.orphans;
            state.read_status = snapshot.status;
            state.login_item_status = login;
        });
    }

    /// Reads everything from disk. On an unreadable app list it keeps `fallback_apps`: an empty list would plan away
    /// every Mac game's protection.
    pub fn load_snapshot(
        steam: &SteamLocation,
        layout: &ToolLayout,
        store: &GameSettingsStore,
        mode: &SteamPlayMode,
        fallback_apps: &[AppInfo],
        identity: &dyn Fn(&Path) -> Option<String>,
    ) -> Snapshot {
        let (apps, app_info_error) = match app_info::read(&steam.app_info()) {
            Ok(apps) => (apps, None),
            Err(error) => (fallback_apps.to_vec(), Some(error.to_string())),
        };
        let plan = MappingPlanner::plan(&apps, &store.run_as_overrides());
        Snapshot {
            runtime: InstalledRuntime::read(layout, identity),
            app_info_error,
            installed: steam.installed_app_ids(),
            settings: store.all(),
            orphans: OrphanPrefixes::find(steam),
            status: mode.status(&plan),
            steam_launch_options: steam.local_config().map(|file| LaunchOptions::steam_lines(&file)).unwrap_or_default(),
            apps,
        }
    }

    pub fn plan(&self) -> BTreeMap<String, ToolMapping> {
        let apps = self.state.lock().unwrap().apps.clone();
        MappingPlanner::plan(&apps, &self.store.run_as_overrides())
    }

    // MARK: Runtime

    /// Installs the bundled `wine.app` (spec §3.9). Deferred while a game runs from the old one.
    pub fn install_runtime(&self, force: bool) {
        let Some(source) = self.wine_app_source.clone() else { return };
        let started = self.mutate(|state| {
            if state.installing {
                return false;
            }
            state.installing = true;
            if state.deferred_force.is_none() {
                state.runtime_notice = Some("Installing the runtime…".into()); // a retry keeps the deferral's text
            }
            true
        });
        if !started {
            return;
        }
        let launcher = self.launcher_override.clone().unwrap_or_else(Self::helper);
        let entry_points = [
            self.layout.root.join("proton"),
            self.mode.tools.join(format!("{}/passthrough.sh", SteamPlayMode::NATIVE_TOOL_NAME)),
        ];
        let had_old_entry_points = entry_points.iter().any(|entry| entry.exists());
        let result = (self.installer)(&source, &self.layout, &launcher, force);
        match &result {
            Ok(RuntimeInstallOutcome::Deferred(_)) => {
                self.mutate(|state| {
                    state.installing = false;
                    state.install_failed = false;
                    state.deferred_force = Some(force);
                    state.runtime_notice = Some("The runtime updates after the game exits.".into());
                });
                return; // nothing changed on disk: no refresh every 3 s for the whole game session
            }
            Ok(_) => {
                self.mutate(|state| {
                    state.installing = false;
                    state.deferred_force = None;
                    state.install_failed = false;
                    state.runtime_notice = None;
                });
                if self.mode.is_wanted() {
                    if let Err(error) = self.mode.install_native_tool() {
                        self.mutate(|state| state.error_message = Some(error.to_string()));
                    }
                }
                if had_old_entry_points && self.mode.process.is_running() {
                    self.mutate(|state| state.tools_changed_under_steam = true);
                }
            }
            Err(error) => self.mutate(|state| {
                state.installing = false;
                state.deferred_force = None;
                state.install_failed = true;
                state.runtime_notice = Some(error.to_string());
            }),
        }
        let runtime = InstalledRuntime::read(&self.layout, &*self.identity);
        self.mutate(|state| {
            state.generation += 1; // a refresh that read the folder mid-install would put the old runtime back
            state.runtime = runtime;
        });
        if result.is_ok() {
            self.install_vm_tools();
        }
        // D2: no question asked; the files come from Valve's CDN to this Mac, as Linux Steam's legacycompat does for
        // Proton (macOS Steam has no legacycompat). Only for a runtime that can use them (Wine 0036).
        if result.is_ok() && self.layout.supports_genuine_steamclient() {
            *self.valve_task.lock().unwrap() = Some(self.spawn(|model| model.update_valve_client(false)));
        }
        self.refresh();
    }

    /// Each poll tick: retry a deferred install; reinstall when a launch found the runtime damaged.
    pub fn poll_runtime(&self) {
        if self.state.lock().unwrap().vm_tools_deferred {
            self.install_vm_tools();
        }
        let retry_setup = {
            let state = self.state.lock().unwrap();
            state.vm_setup_deferred
                && !state.vm_setup_running
                && state.vm_setup_deferred_at.is_none_or(|at| at.elapsed() >= self.vm_retry_interval)
        };
        if retry_setup {
            self.set_up_vm();
        }
        let (deferred_force, install_failed) = {
            let state = self.state.lock().unwrap();
            (state.deferred_force, state.install_failed)
        };
        if let Some(force) = deferred_force {
            self.install_runtime(force);
        } else if !install_failed && self.layout.runtime_damaged_marker().exists() {
            self.install_runtime(false);
        }
    }

    // MARK: Valve

    /// The automatic check (`force` false: once per app version, never after Remove) or Settings' Download again. One
    /// at a time; a failure shows in Settings only, and launches keep lsteamclient or the last good copy.
    pub fn update_valve_client(&self, force: bool) {
        let started = self.mutate(|state| {
            if state.valve_updating {
                return false;
            }
            state.valve_updating = true;
            state.valve_notice = Some("Downloading Steam components from Valve…".into());
            true
        });
        if !started {
            return;
        }
        let failure = if force {
            self.valve_client.update(true).err()
        } else {
            self.valve_client.update_if_needed(&self.app_version).err()
        };
        let version = ValveClient::installed(&self.valve_client.cache).map(|install| install.version);
        self.mutate(|state| {
            state.valve_updating = false;
            state.valve_notice = failure.map(|error| error.to_string());
            state.valve_version = version;
        });
        self.copy_valve_client_to_vm();
    }

    /// Settings' Remove (spec D5): prefixes get lsteamclient from the next launch, and nothing is downloaded again until
    /// Download again.
    pub fn remove_valve_client(&self) {
        let removed = self.valve_client.remove();
        let version = ValveClient::installed(&self.valve_client.cache).map(|install| install.version);
        self.mutate(|state| {
            if let Err(error) = removed {
                state.valve_notice = Some(error.to_string());
            }
            state.valve_version = version;
        });
        self.copy_valve_client_to_vm();
    }

    // MARK: The microVM's host tools

    /// From `Contents/Helpers/vm` into the VM folder after each runtime install; deferred while a VM runs from the
    /// folder or the folder is missing (the poll retries). Never into a developer's folder.
    pub fn install_vm_tools(&self) {
        let (Some(tools), Some(root)) = (&self.vm_tools, &self.vm_root) else { return };
        let started = self.mutate(|state| {
            if state.vm_tools_installing {
                return false;
            }
            // Maybe a disconnected drive: the poll carries on once it's back, and only Rebuild replaces the link.
            if VMToolsInstaller::missing_target(root).is_some() {
                state.vm_tools_deferred = true;
                return false;
            }
            state.vm_tools_installing = true;
            true
        });
        if !started {
            return;
        }
        let result = (self.vm_installer)(tools, root);
        self.mutate(|state| {
            state.vm_tools_installing = false;
            state.vm_tools_deferred = matches!(result, Ok(VMToolsInstallOutcome::Deferred(_)));
        });
        match result {
            Ok(VMToolsInstallOutcome::Deferred(_)) => {}
            Ok(_) => {
                self.copy_valve_client_to_vm();
                if self.vm_setup_needed() && !self.state.lock().unwrap().vm_setup_running {
                    *self.vm_setup_task.lock().unwrap() = Some(self.spawn(|model| model.set_up_vm()));
                }
            }
            Err(error) => self.mutate(|state| state.error_message = Some(error.to_string())),
        }
    }

    // MARK: The game VM's images

    /// The app ships a manifest, the folder isn't a developer's, and its images aren't the manifest's.
    pub fn vm_setup_needed(&self) -> bool {
        let (Some(manifest), Some(root)) = (&self.vm_manifest, &self.vm_root) else { return false };
        if VMToolsInstaller::is_developer_folder(root) {
            return false;
        }
        let current = std::fs::read_to_string(root.join("images/manifest.sha256")).ok();
        current.map(|text| text.trim().to_string()) != VMSetup::manifest_hash(manifest)
    }

    /// The folder is a link to a folder that isn't there (its drive disconnected): the link's target.
    pub fn vm_folder_missing(&self) -> Option<String> {
        self.vm_root.as_deref().and_then(VMToolsInstaller::missing_target)
    }

    /// The folder is a developer's own, not a link to one (which Rebuild replaces): never written, so Rebuild is off.
    pub fn vm_rebuild_off(&self) -> bool {
        self.vm_root
            .as_deref()
            .is_some_and(|root| VMToolsInstaller::is_developer_folder(root) && !VMToolsInstaller::is_link(root))
    }

    /// The Game VM line Setup and Settings show: a missing folder's, a developer's own folder's, else the setup's.
    pub fn vm_status(&self) -> Option<String> {
        if let Some(missing) = self.vm_folder_missing() {
            return Some(format!(
                "Game VM: its folder {missing} is missing. Reconnect its drive, or Rebuild to start a new VM here."
            ));
        }
        if self.vm_rebuild_off() {
            return Some(
                "Game VM: a developer's folder (its images are vm's own build). MacNeutron never changes it, so Rebuild is off."
                    .into(),
            );
        }
        self.state.lock().unwrap().vm_notice.clone()
    }

    /// A game would run: the images are the manifest's and the launcher's own check (`MicroVM::is_ready`) passes.
    pub fn vm_ready(&self) -> bool {
        !self.state.lock().unwrap().vm_setup_running
            && !self.vm_setup_needed()
            && self.vm_root.as_deref().is_some_and(MicroVM::is_ready)
    }

    /// `macneutron vm-setup` for the app's manifest (spec §4.6), its progress lines read into `vm_notice` and both its
    /// streams appended to vm-setup.log. One at a time; a deferral (75) waits for the poll, a failure for Retry.
    pub fn set_up_vm(&self) {
        let (Some(manifest), Some(root)) = (&self.vm_manifest, &self.vm_root) else { return };
        let started = self.mutate(|state| {
            if state.vm_setup_running {
                return false;
            }
            state.vm_setup_running = true;
            state.vm_setup_deferred = false;
            state.vm_setup_failed = false;
            state.vm_notice = Some("Game VM: starting…".into());
            true
        });
        if !started {
            return;
        }
        let folder = root.to_string_lossy();
        let folder = folder.strip_suffix('/').unwrap_or(&folder);
        // A performance build (X.Y.Zp) keeps the setup's output for a failure only.
        let quiet = ToolLayout::is_performance_build_version(Some(&self.app_version));
        let mut held = Vec::new();
        let mut log = if quiet { None } else { self.open_setup_log() };
        let mut record = |data: &[u8]| {
            if quiet {
                held.extend_from_slice(data);
            } else if let Some(log) = log.as_mut() {
                let _ = log.write_all(data);
            }
        };
        let status: i32;
        let last_error: String;
        let spawned = Command::new(self.vm_helper.clone().unwrap_or_else(Self::helper))
            .arg("vm-setup")
            .arg("--manifest")
            .arg(manifest)
            .arg("--vm")
            .arg(folder)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn();
        match spawned {
            Ok(mut child) => {
                {
                    let mut process = self.vm_process.lock().unwrap();
                    process.0 = Some(child.id());
                    if process.1 {
                        terminate(child.id()); // the app quit while it started
                    }
                }
                let mut stderr = child.stderr.take().unwrap();
                let errors = std::thread::spawn(move || {
                    let mut data = Vec::new();
                    let _ = stderr.read_to_end(&mut data);
                    data
                });
                let mut out = BufReader::new(child.stdout.take().unwrap());
                let mut line = Vec::new();
                // Whole lines, however they're written.
                while matches!(out.read_until(b'\n', &mut line), Ok(n) if n > 0) {
                    let text = String::from_utf8_lossy(&line);
                    let text = text.strip_suffix('\n').unwrap_or(&text);
                    record(format!("{text}\n").as_bytes());
                    if let Some(notice) = Self::notice_for_progress_line(text) {
                        self.mutate(|state| state.vm_notice = Some(notice));
                    }
                    line.clear();
                }
                let stderr = errors.join().unwrap_or_default();
                record(&stderr);
                let text = String::from_utf8_lossy(&stderr);
                last_error = text.split('\n').rfind(|line| !line.is_empty()).unwrap_or("").to_string();
                status = child.wait().map(exit_status).unwrap_or(-1);
            }
            Err(error) => {
                status = -1;
                last_error = error.to_string();
            }
        }
        drop(log);
        if quiet && status != 0 && status != 75 {
            if let Some(mut failed) = self.open_setup_log() {
                let _ = failed.write_all(&held);
            }
        }
        self.vm_process.lock().unwrap().0 = None;
        self.mutate(|state| {
            state.vm_setup_running = false;
            state.vm_notice = Some(match status {
                0 => "Game VM: ready".into(),
                75 => {
                    state.vm_setup_deferred = true;
                    state.vm_setup_deferred_at = Some(Instant::now());
                    "Game VM: waiting for the game to quit".into()
                }
                _ => {
                    state.vm_setup_failed = true;
                    if last_error.is_empty() {
                        format!("Game VM: the setup stopped ({status})")
                    } else {
                        format!("Game VM: {last_error}")
                    }
                }
            });
        });
    }

    fn open_setup_log(&self) -> Option<std::fs::File> {
        if let Some(folder) = self.vm_setup_log.parent() {
            let _ = std::fs::create_dir_all(folder);
        }
        std::fs::OpenOptions::new().create(true).append(true).open(&self.vm_setup_log).ok()
    }

    /// Settings' Rebuild: the setup's downloads and any half-built set go, then a fresh set is built and swapped in (the
    /// setup waits while a game runs from the old one, then removes it). Games' disks, TPMs and runs are never touched.
    pub fn rebuild_vm(&self) {
        let Some(root) = &self.vm_root else { return };
        if self.vm_manifest.is_none() || self.state.lock().unwrap().vm_setup_running {
            return;
        }
        // A link to a missing folder (a disconnected drive, a developer's deleted build/) or to a developer's folder: a
        // real folder with the app's tools takes its place (the link's target is never touched), and their install starts
        // the setup (the new folder has no images).
        if VMToolsInstaller::remove_dead_link(root) || VMToolsInstaller::remove_developer_link(root) {
            self.install_vm_tools();
            self.wait_vm_setup();
            return;
        }
        if self.vm_rebuild_off() {
            return;
        }
        let _ = std::fs::remove_dir_all(root.join("setup"));
        self.set_up_vm();
    }

    /// "progress download <done> <total>" and "progress assemble <k> <n>" as the status line says them; None otherwise.
    pub fn notice_for_progress_line(line: &str) -> Option<String> {
        let words: Vec<&str> = line.split(' ').filter(|word| !word.is_empty()).collect();
        let [first, kind, done, total] = words[..] else { return None };
        let (Ok(done_value), Ok(total_value)) = (done.parse::<f64>(), total.parse::<f64>()) else { return None };
        if first != "progress" {
            return None;
        }
        match kind {
            "download" => Some(format!(
                "Game VM: downloading ({:.1} of {:.2} GB)",
                done_value / 1e9,
                total_value / 1e9
            )),
            "assemble" => Some(format!("Game VM: building its images ({done} of {total})")),
            _ => None,
        }
    }

    /// Valve's steamclient64.dll for the guest (the VM folder's share/valve), only where the app ships the VM.
    fn copy_valve_client_to_vm(&self) {
        let Some(root) = &self.vm_root else { return };
        if self.vm_tools.is_none() {
            return;
        }
        let installed = ValveClient::installed(&self.valve_client.cache);
        if let Err(error) = VMToolsInstaller::copy_valve_client(installed.as_ref(), root) {
            self.mutate(|state| state.error_message = Some(error.to_string()));
        }
    }

    // MARK: Setup

    pub fn enable_steam_play(&self) {
        let unreadable = self.state.lock().unwrap().app_info_error.clone();
        if let Some(error) = unreadable {
            self.mutate(|state| state.error_message = Some(app_info_unreadable(&error)));
            return;
        }
        self.run("Turning on Steam Play mode and restarting Steam…", |model| {
            // enable quits Steam itself (after its checks, so a failure leaves Steam running) and then asks for the
            // plan: Steam rewrites its app list on exit.
            model.mode.enable_with(|| {
                let apps = app_info::read(&model.steam.app_info())
                    .map_err(|error| SteamPlayError::Io(app_info_unreadable(&error.to_string())))?;
                Ok(MappingPlanner::plan(&apps, &model.store.run_as_overrides()))
            })
        });
    }

    pub fn disable_steam_play(&self) {
        self.run("Turning off Steam Play mode and restarting Steam…", |model| model.mode.disable());
    }

    /// Quit Steam, write pending mappings (or restore lost files), start Steam again.
    pub fn restart_steam(&self) {
        if self.state().status() == SteamPlayStatus::Lost {
            self.enable_steam_play();
            return;
        }
        self.run("Restarting Steam…", |model| {
            model.mode.process.quit(Duration::from_secs(30))?;
            model.refresh();
            if model.state.lock().unwrap().app_info_error.is_none() {
                model.mode.sync(&model.plan())?;
            }
            model.mode.process.launch()
        });
    }

    // MARK: Games

    /// Saves a game's settings. A "Runs as" change is written to Steam right away when Steam is closed; otherwise it
    /// waits for a restart ("restart needed").
    pub fn update(&self, app_id: u32, change: impl FnOnce(&mut GameSettings)) {
        let changed = self.mutate(|state| {
            let index = state.games.iter().position(|row| row.id() == app_id)?;
            state.generation += 1; // a refresh already reading from disk would put the old settings back
            change(&mut state.games[index].settings);
            Some((state.games[index].settings.clone(), state.games[index].name()))
        });
        let Some((settings, name)) = changed else { return };
        if let Err(error) = self.store.save(&settings, &app_id.to_string()) {
            self.mutate(|state| state.error_message = Some(format!("Couldn't save settings for {name}: {error}")));
        }
        let (plan, readable) = (self.plan(), self.state.lock().unwrap().app_info_error.is_none());
        let mut failure = None;
        if readable && !self.mode.process.is_running() {
            failure = self.mode.sync(&plan).err().map(|error| error.to_string());
        }
        let status = self.mode.status(&plan);
        self.mutate(|state| {
            state.read_status = status;
            if failure.is_some() {
                state.error_message = failure;
            }
        });
    }

    pub fn clean_up(&self, selected: &[OrphanPrefix]) {
        self.mutate(|state| state.generation += 1);
        if let Err(error) = OrphanPrefixes::delete(selected) {
            self.mutate(|state| state.error_message = Some(error.to_string()));
        }
        self.refresh();
    }

    // MARK: Settings

    pub fn set_launch_at_login(&self, on: bool) {
        let result = if on { (self.login_item.register)() } else { (self.login_item.unregister)() };
        let status = (self.login_item.status)();
        self.mutate(|state| {
            if let Err(error) = result {
                state.error_message = Some(format!("Couldn't change the login item: {error}"));
            }
            state.login_item_status = status;
        });
    }

    // MARK: Plumbing

    fn mutate<R>(&self, change: impl FnOnce(&mut State) -> R) -> R {
        let result = change(&mut self.state.lock().unwrap());
        self.on_change.notify();
        result
    }

    /// `work` on a new thread, with the model kept alive until it's done.
    fn spawn(&self, work: impl FnOnce(&AppModel) + Send + 'static) -> JoinHandle<()> {
        let model = self.me.upgrade().expect("an AppModel lives in an Arc");
        std::thread::spawn(move || work(&model))
    }

    fn run(&self, message: &str, work: impl FnOnce(&AppModel) -> Result<(), SteamPlayError>) {
        let started = self.mutate(|state| {
            if state.busy.is_some() {
                return false; // one Steam-changing action at a time
            }
            state.busy = Some(message.to_string());
            state.error_message = None;
            true
        });
        if !started {
            return;
        }
        let result = work(self);
        self.mutate(|state| {
            match result {
                Ok(()) => state.tools_changed_under_steam = false, // every action here restarts or quits Steam
                Err(error) => state.error_message = Some(error.to_string()),
            }
            state.busy = None;
        });
        self.refresh();
    }

    /// Every `interval`: retry a deferred or damaged runtime install; when Steam quits, bring its mappings up to date;
    /// 20 s after it starts, re-check the files. Every 20 ticks, re-read Steam's app list so games bought meanwhile show
    /// up as "restart needed". Ends with the model.
    fn watch_steam(&self, interval: Duration) {
        let me = self.me.clone();
        std::thread::spawn(move || {
            let mut watcher = SteamWatcher::new();
            let mut ticks: u64 = 0;
            loop {
                ticks += 1;
                let Some(model) = me.upgrade() else { return };
                model.poll_runtime();
                if ticks.is_multiple_of(20) &&model.state.lock().unwrap().busy.is_none() {
                    model.refresh();
                }
                match watcher.observe(model.mode.process.is_running()) {
                    Some(SteamEvent::Quit) => {
                        model.mutate(|state| state.tools_changed_under_steam = false);
                        model.refresh(); // Steam rewrites its app list on exit
                        let state = model.state();
                        if state.busy.is_none() && state.app_info_error.is_none() {
                            if let Err(error) = model.mode.sync(&model.plan()) {
                                model.mutate(|state| state.error_message = Some(error.to_string()));
                            }
                            model.refresh();
                        }
                    }
                    Some(SteamEvent::Launched) => {
                        drop(model);
                        std::thread::sleep(Duration::from_secs(20));
                        let Some(model) = me.upgrade() else { return };
                        model.refresh();
                    }
                    None => {}
                }
                std::thread::sleep(interval);
            }
        });
    }
}

fn app_info_unreadable(detail: &str) -> String {
    format!("MacNeutron can't read Steam's app list, so it can't protect your Mac games: {detail}")
}

/// `Process.terminate()`.
fn terminate(pid: u32) {
    unsafe {
        libc::kill(pid as libc::pid_t, libc::SIGTERM);
    }
}

/// `Process.terminationStatus`: the exit code, or the signal that ended it.
fn exit_status(status: std::process::ExitStatus) -> i32 {
    use std::os::unix::process::ExitStatusExt;
    status.code().or(status.signal()).unwrap_or(-1)
}

/// Swift's `await task?.value`.
fn join(task: &Mutex<Option<JoinHandle<()>>>) {
    let handle = task.lock().unwrap().take();
    if let Some(handle) = handle {
        let _ = handle.join();
    }
}

/// The menu bar's text and SF Symbol for a status.
pub trait StatusDisplay {
    fn menu_title(&self) -> String;
    fn symbol(&self) -> &'static str;
}

impl StatusDisplay for SteamPlayStatus {
    fn menu_title(&self) -> String {
        match self {
            SteamPlayStatus::Off => "Steam Play mode off".into(),
            SteamPlayStatus::On => "Steam Play mode on".into(),
            SteamPlayStatus::RestartNeeded(count) => {
                format!("Restart Steam to apply {count} {}", if *count == 1 { "change" } else { "changes" })
            }
            SteamPlayStatus::Lost => "Steam Play mode was turned off by a Steam update".into(),
            SteamPlayStatus::Problem(message) => message.clone(),
        }
    }

    fn symbol(&self) -> &'static str {
        match self {
            SteamPlayStatus::Off | SteamPlayStatus::On => "atom",
            SteamPlayStatus::RestartNeeded(_) => "exclamationmark.circle",
            SteamPlayStatus::Lost | SteamPlayStatus::Problem(_) => "exclamationmark.triangle",
        }
    }
}

#[cfg(test)]
mod tests;
