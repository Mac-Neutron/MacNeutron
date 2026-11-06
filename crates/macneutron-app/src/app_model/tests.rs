//! AppModelTests.swift's 50 tests.

use super::*;
use macneutron_core::app_info::MAGIC_V29;
use macneutron_core::test_support::{
    make_app_info_v29, make_fake_steam, make_temp_dir, make_tool_layout, write, FakeSteam, FakeValveCDN, OK_SESSION,
    STEAM_CONFIG_FIXTURE, TEST_IDENTITY,
};
use macneutron_core::{SteamControlling, ValveClientError};
use std::collections::{HashMap, HashSet};
use std::sync::atomic::{AtomicUsize, Ordering};

fn app(app_id: u32, name: &str, oslist: &[&str]) -> AppInfo {
    let oslist: HashSet<String> = oslist.iter().map(|os| os.to_string()).collect();
    AppInfo { app_id, name: name.into(), r#type: "game".into(), oslist }
}

fn timberborn() -> AppInfo {
    app(1062090, "Timberborn", &["windows", "macos"])
}

fn bongo_cat() -> AppInfo {
    app(3419430, "Bongo Cat", &["windows", "macos"])
}

fn cats() -> AppInfo {
    app(2977660, "Cats", &["windows"])
}

/// A login item that remembers what it was asked to do.
fn fake_login_item(status_after_register: LoginItemStatus) -> Arc<LoginItem> {
    let current = Arc::new(Mutex::new(LoginItemStatus::NotRegistered));
    let (status, register, unregister) = (current.clone(), current.clone(), current);
    Arc::new(LoginItem {
        status: Box::new(move || *status.lock().unwrap()),
        register: Box::new(move || {
            *register.lock().unwrap() = status_after_register;
            Ok(())
        }),
        unregister: Box::new(move || {
            *unregister.lock().unwrap() = LoginItemStatus::NotRegistered;
            Ok(())
        }),
    })
}

type Effect = Box<dyn Fn(&Path, &ToolLayout) -> Result<(), RuntimeInstallError> + Send + Sync>;

/// Stands in for `RuntimeInstaller::install`: records each call's `force` and answers with the next outcome (the last
/// one repeats). `effect` runs after the recording, like the real install's disk changes.
struct ScriptedInstaller {
    outcomes: Mutex<Vec<RuntimeInstallOutcome>>,
    forces: Mutex<Vec<bool>>,
    effect: Effect,
}

impl ScriptedInstaller {
    fn new(outcomes: &[RuntimeInstallOutcome]) -> Arc<Self> {
        Self::with(outcomes, |_, _| Ok(()))
    }

    fn with(
        outcomes: &[RuntimeInstallOutcome],
        effect: impl Fn(&Path, &ToolLayout) -> Result<(), RuntimeInstallError> + Send + Sync + 'static,
    ) -> Arc<Self> {
        Arc::new(ScriptedInstaller {
            outcomes: Mutex::new(outcomes.to_vec()),
            forces: Mutex::new(Vec::new()),
            effect: Box::new(effect),
        })
    }

    fn calls(&self) -> Vec<bool> {
        self.forces.lock().unwrap().clone()
    }

    fn install(self: &Arc<Self>) -> RuntimeInstall {
        let me = self.clone();
        Arc::new(move |source, layout, _, force| {
            let outcome = {
                me.forces.lock().unwrap().push(force);
                let mut outcomes = me.outcomes.lock().unwrap();
                if outcomes.len() > 1 {
                    outcomes.remove(0)
                } else {
                    outcomes[0].clone()
                }
            };
            (me.effect)(source, layout)?;
            Ok(outcome)
        })
    }
}

/// What the real install does to the folder: the source (a fake, unsigned wine.app) replaces the old copy.
fn copies_the_source(source: &Path, layout: &ToolLayout) -> Result<(), RuntimeInstallError> {
    let _ = std::fs::remove_dir_all(layout.wine_app());
    std::fs::create_dir_all(&layout.root).unwrap();
    assert!(Command::new("/bin/cp").arg("-R").arg(source).arg(layout.wine_app()).status().unwrap().success());
    let _ = std::fs::remove_file(layout.runtime_damaged_marker());
    Ok(())
}

fn make_mode() -> (SteamPlayMode, Arc<FakeSteam>) {
    let (steam, root) = make_fake_steam(STEAM_CONFIG_FIXTURE);
    let fake = Arc::new(FakeSteam::new(steam.clone(), false, OK_SESSION, Some(root.join("steam-play-enabled")), false));
    let mut mode = SteamPlayMode::new(steam, &root, fake.clone());
    mode.verify_timeout = Duration::from_secs(2);
    (mode, fake)
}

fn offline_valve_client(cache: PathBuf) -> ValveClient {
    ValveClient::new(cache, Arc::new(|_| Err(ValveClientError::Http(0))), Arc::new(SystemProcessRunner::new()))
}

fn install_tools(tools: &Path, vm: &Path) -> Result<VMToolsInstallOutcome, RuntimeInstallError> {
    VMToolsInstaller::install(tools, None, vm, &SystemProcessRunner::new(), &Vec::<String>::new)
}

/// Swift's init defaults as the tests leave them, with every folder in a temp directory and no Steam watcher.
fn config(mode: &SteamPlayMode, layout: ToolLayout) -> AppModelConfig {
    AppModelConfig {
        steam: mode.steam.clone(),
        valve_client: Some(offline_valve_client(layout.valve_cache())),
        layout,
        mode: mode.clone(),
        store: GameSettingsStore::new(make_temp_dir()),
        login_item: fake_login_item(LoginItemStatus::Enabled),
        wine_app_source: None,
        launcher_binary: None,
        installer: ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged]).install(),
        identity: Arc::new(|_| Some(TEST_IDENTITY.to_string())),
        app_version: "dev".into(),
        vm_tools: None,
        vm_root: None,
        vm_installer: Arc::new(install_tools),
        vm_manifest: None,
        vm_helper: None,
        vm_setup_log: make_temp_dir().join("vm-setup.log"),
        vm_retry_interval: Duration::from_secs(30),
        log: LauncherLog::new(make_temp_dir().join("Logs")),
        poll: None,
    }
}

/// `AppModel(...)` then `await model.installTask?.value`.
fn started(config: AppModelConfig) -> Arc<AppModel> {
    let model = AppModel::new(config);
    model.start();
    model.wait_install();
    model
}

fn make_model_with(
    steam_running: bool,
    login_item: Option<Arc<LoginItem>>,
    wine_app_source: Option<PathBuf>,
    installer: &Arc<ScriptedInstaller>,
) -> (Arc<AppModel>, SteamPlayMode, Arc<FakeSteam>) {
    let (mode, fake) = make_mode();
    let apps = [timberborn(), bongo_cat(), cats()];
    std::fs::create_dir_all(mode.steam.app_info().parent().unwrap()).unwrap();
    std::fs::write(mode.steam.app_info(), make_app_info_v29(&apps, MAGIC_V29)).unwrap();
    mode.enable(&MappingPlanner::plan(&apps, &HashMap::new())).unwrap();
    if !steam_running {
        fake.quit(Duration::from_secs(1)).unwrap();
    }
    let mut config = config(&mode, make_tool_layout());
    config.store = GameSettingsStore::new(make_temp_dir().join("games"));
    if let Some(login_item) = login_item {
        config.login_item = login_item;
    }
    config.wine_app_source = wine_app_source;
    config.installer = installer.install();
    let model = started(config);
    model.refresh();
    (model, mode, fake)
}

fn make_model() -> (Arc<AppModel>, SteamPlayMode, Arc<FakeSteam>) {
    make_model_with(false, None, None, &ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged]))
}

/// A relaunch on `model`'s folders, Steam Play mode `mode`, and a runtime of `identity`.
fn relaunched(model: &AppModel, mode: &SteamPlayMode, identity: Option<&'static str>) -> AppModelConfig {
    let mut config = config(mode, model.layout.clone());
    config.store = GameSettingsStore::new(model.store.directory.clone());
    config.login_item = model.login_item.clone();
    config.identity = Arc::new(move |_| identity.map(str::to_string));
    config
}

fn exists(path: &Path) -> bool {
    path.exists()
}

fn read(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap()
}

fn row(model: &AppModel, app_id: u32) -> GameRow {
    model.state().games.into_iter().find(|row| row.id() == app_id).unwrap()
}

#[test]
fn runs_as_change_applies_immediately_when_steam_is_closed() {
    let (model, mode, _) = make_model();
    model.update(bongo_cat().app_id, |settings| settings.run_as = Some(RunAs::Windows));
    assert_eq!(mode.current_mappings().unwrap()["3419430"].tool, "macneutron-proton");
    assert_eq!(model.state().status(), SteamPlayStatus::On);
}

#[test]
fn runs_as_change_waits_for_a_restart_while_steam_runs() {
    let (model, mode, _) = make_model_with(true, None, None, &ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged]));
    model.update(bongo_cat().app_id, |settings| settings.run_as = Some(RunAs::Windows));
    assert_eq!(mode.current_mappings().unwrap()["3419430"].tool, "macneutron-native");
    assert_eq!(model.state().status(), SteamPlayStatus::RestartNeeded(1));
}

#[test]
fn refresh_keeps_the_last_good_app_list_when_steams_cache_is_unreadable() {
    let (model, mode, _) = make_model();
    std::fs::write(mode.steam.app_info(), "garbage").unwrap();
    model.refresh();
    let state = model.state();
    assert!(state.app_info_error.is_some());
    let mut ids: Vec<u32> = state.games.iter().map(GameRow::id).collect();
    ids.sort();
    assert_eq!(ids, [1062090, 2977660, 3419430]);
}

#[test]
fn snapshots_load_off_the_main_actor() {
    let (mode, _) = make_mode();
    std::fs::create_dir_all(mode.steam.app_info().parent().unwrap()).unwrap();
    std::fs::write(mode.steam.app_info(), make_app_info_v29(&[cats()], MAGIC_V29)).unwrap();
    let store = GameSettingsStore::new(make_temp_dir());
    let layout = make_tool_layout();
    let snapshot =
        std::thread::spawn(move || AppModel::load_snapshot(&mode.steam, &layout, &store, &mode, &[], &CodeIdentity::of))
            .join()
            .unwrap();
    assert_eq!(snapshot.apps.iter().map(|app| app.app_id).collect::<Vec<_>>(), [2977660]);
    assert_eq!(snapshot.status, SteamPlayStatus::Off);
}

#[test]
fn snapshot_reads_steam_launch_lines() {
    let (model, mode, _) = make_model();
    let snapshot = || AppModel::load_snapshot(&mode.steam, &model.layout, &model.store, &mode, &[], &CodeIdentity::of);
    assert!(snapshot().steam_launch_options.is_empty()); // no localconfig.vdf
    let id = cats().app_id;
    let line = "/usr/bin/env MACNEUTRON_LOG=1 %command%";
    write(
        &format!(
            "\"UserLocalConfigStore\" {{ \"Software\" {{ \"Valve\" {{ \"Steam\" {{ \"apps\" {{ \"{id}\" {{ \"LaunchOptions\" \"{line}\" }} }} }} }} }} }}"
        ),
        &mode.steam.root.join("userdata/12345/config/localconfig.vdf"),
        false,
    );
    let read = snapshot();
    assert_eq!(read.steam_launch_options, BTreeMap::from([(id, line.to_string())]));
    model.apply(read, model.state().generation);
    assert_eq!(row(&model, id).steam_launch_options.as_deref(), Some(line));
}

#[test]
fn login_toggle_reports_when_macos_wants_approval() {
    let login = fake_login_item(LoginItemStatus::RequiresApproval);
    let (model, _, _) =
        make_model_with(false, Some(login), None, &ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged]));
    model.set_launch_at_login(true);
    assert_eq!(model.state().login_item_status, LoginItemStatus::RequiresApproval);
    assert_eq!(model.state().error_message, None);
}

#[test]
fn setup_window_stays_closed_once_set_up() {
    let (model, mode, _) = make_model();
    assert_eq!(model.layout.runtime_version().as_deref(), Some("test")); // the fake wine.app's version
    let relaunched = AppModel::new(relaunched(&model, &mode, Some(TEST_IDENTITY)));
    assert!(relaunched.setup_complete()); // the scene reads it before any refresh finishes
}

#[test]
fn a_snapshot_read_before_a_change_does_not_undo_it() {
    let (model, mode, _) = make_model();
    let token = model.state().generation;
    let stale = AppModel::load_snapshot(&mode.steam, &model.layout, &model.store, &mode, &[], &CodeIdentity::of);
    model.update(bongo_cat().app_id, |settings| settings.run_as = Some(RunAs::Windows));
    model.apply(stale, token);
    assert_eq!(row(&model, bongo_cat().app_id).settings.run_as, Some(RunAs::Windows));
}

// MARK: The runtime

fn test_runtime() -> Option<InstalledRuntime> {
    Some(InstalledRuntime { version: "test".into(), identity: TEST_IDENTITY.into() })
}

#[test]
fn startup_installs_the_bundled_runtime() {
    let (mode, _) = make_mode();
    let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
    let installer = ScriptedInstaller::with(&[RuntimeInstallOutcome::Installed], copies_the_source);
    let mut config = config(&mode, layout);
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.installer = installer.install();
    let model = AppModel::new(config);
    assert_eq!(model.state().runtime, None);
    model.start();
    model.wait_install();
    assert_eq!(installer.calls(), [false]);
    assert_eq!(model.state().runtime, test_runtime());
    assert_eq!(model.state().runtime_notice, None);
}

#[test]
fn no_bundled_runtime_installs_nothing() {
    let installer = ScriptedInstaller::new(&[RuntimeInstallOutcome::Installed]);
    let (model, _, _) = make_model_with(false, None, None, &installer);
    model.install_runtime(true);
    assert!(installer.calls().is_empty());
    assert_eq!(model.state().runtime_notice, None);
    assert_eq!(model.state().runtime, test_runtime()); // the one already there
}

#[test]
fn a_deferred_install_is_retried_by_the_poll() {
    let installer = ScriptedInstaller::new(&[RuntimeInstallOutcome::Deferred("/x".into()), RuntimeInstallOutcome::Installed]);
    let (model, _, _) = make_model_with(false, None, Some(make_tool_layout().wine_app()), &installer);
    let state = model.state();
    assert_eq!(state.runtime_notice.as_deref(), Some("The runtime updates after the game exits."));
    assert_eq!(state.busy, None);
    assert_eq!(state.generation, 0); // a deferral changes nothing on disk, so it doesn't refresh
    model.poll_runtime();
    assert_eq!(installer.calls(), [false, false]);
    assert_eq!(model.state().runtime_notice, None);
    assert_eq!(model.state().busy, None);
    model.poll_runtime();
    assert_eq!(installer.calls().len(), 2); // nothing pending any more
}

#[test]
fn repair_forces_a_reinstall() {
    let installer = ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged, RuntimeInstallOutcome::Installed]);
    let (model, _, _) = make_model_with(false, None, Some(make_tool_layout().wine_app()), &installer);
    model.install_runtime(true);
    assert_eq!(installer.calls(), [false, true]);
    assert_eq!(model.state().runtime_notice, None);
}

#[test]
fn one_install_at_a_time() {
    // Swift's two `async let`s interleave on the main actor; real threads need the first install to take a moment.
    let installer = ScriptedInstaller::with(&[RuntimeInstallOutcome::Unchanged], |_, _| {
        std::thread::sleep(Duration::from_millis(300));
        Ok(())
    });
    let (model, _, _) = make_model_with(false, None, Some(make_tool_layout().wine_app()), &installer);
    std::thread::scope(|scope| {
        scope.spawn(|| model.install_runtime(true));
        scope.spawn(|| model.install_runtime(true));
    });
    assert_eq!(installer.calls(), [false, true]);
}

#[test]
fn damaged_marker_triggers_a_reinstall() {
    let installer =
        ScriptedInstaller::with(&[RuntimeInstallOutcome::Unchanged, RuntimeInstallOutcome::Installed], copies_the_source);
    let (model, _, _) = make_model_with(false, None, Some(make_tool_layout().wine_app()), &installer);
    model.poll_runtime();
    assert_eq!(installer.calls(), [false]); // no marker, nothing deferred
    write("", &model.layout.runtime_damaged_marker(), false);
    model.poll_runtime();
    assert_eq!(installer.calls(), [false, false]);
    assert!(!exists(&model.layout.runtime_damaged_marker()));
}

#[test]
fn a_failed_install_shows_the_error_and_is_not_retried_every_tick() {
    let installer =
        ScriptedInstaller::with(&[RuntimeInstallOutcome::Installed], |_, _| Err(RuntimeInstallError::SignatureInvalid(1)));
    let (model, _, _) = make_model_with(false, None, Some(make_tool_layout().wine_app()), &installer);
    assert_eq!(model.state().runtime_notice.as_deref(), Some("The runtime's signature check failed (codesign exit 1)."));
    assert!(model.state().runtime.is_some()); // the old copy stays
    write("", &model.layout.runtime_damaged_marker(), false);
    model.poll_runtime();
    assert_eq!(installer.calls(), [false]); // Repair retries; the poll doesn't
}

#[test]
fn setup_completes_with_runtime_and_steam_play() {
    let (model, mode, _) = make_model();
    assert!(model.setup_complete());
    let unsigned = AppModel::new(relaunched(&model, &mode, None));
    assert!(!unsigned.setup_complete());
    let (off, _) = make_mode();
    let not_wanted = AppModel::new(relaunched(&model, &off, Some(TEST_IDENTITY)));
    assert!(!not_wanted.setup_complete());
}

#[test]
fn install_refreshes_the_native_tool() {
    let (model, mode, _) = make_model();
    let new_cli = "#!/bin/sh\necho new macneutron\n";
    let runtime_tool = mode.runtime_tool();
    let installer = ScriptedInstaller::with(&[RuntimeInstallOutcome::Installed], move |_, _| {
        write(new_cli, &runtime_tool.join("bin/macneutron"), true); // the install's tool files
        Ok(())
    });
    let mut config = relaunched(&model, &mode, Some(TEST_IDENTITY));
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.installer = installer.install();
    let _updated = started(config);
    assert_eq!(installer.calls(), [false]);
    assert_eq!(read(&mode.native_tool().join("bin/macneutron")), new_cli);
}

#[test]
fn start_leaves_the_native_tool_to_the_install() {
    // Over an older MacNeutron the tool folder's CLI predates the `passthrough` verb until the install writes the new
    // one: a start must not point the Mac-game tool at it first (here the install is deferred by a running game).
    let (model, mode, _) = make_model();
    let native = mode.tools.join(SteamPlayMode::NATIVE_TOOL_NAME);
    let old_manifest = "\"manifest\"\n{\n  \"version\" \"2\"\n  \"commandline\" \"/passthrough.sh %verb%\"\n}\n";
    write("#!/bin/sh\necho old CLI\n", &mode.tools.join("macneutron/bin/macneutron"), true);
    write("#!/bin/sh\n", &native.join("passthrough.sh"), true);
    write(old_manifest, &native.join("toolmanifest.vdf"), false);
    let installer = ScriptedInstaller::new(&[RuntimeInstallOutcome::Deferred("/x".into())]);
    let mut config = relaunched(&model, &mode, Some(TEST_IDENTITY));
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.installer = installer.install();
    let _started = started(config);
    assert_eq!(installer.calls(), [false]);
    assert!(exists(&native.join("passthrough.sh")));
    assert_eq!(read(&native.join("toolmanifest.vdf")), old_manifest);
}

#[test]
fn replacing_rosetta_era_entry_points_while_steam_runs_asks_for_a_restart() {
    // Steam may keep the tool manifests it read at its start (Ruling 27): after an install that removed `proton`, the
    // menu says to restart Steam until it does.
    let installer =
        ScriptedInstaller::with(&[RuntimeInstallOutcome::Unchanged, RuntimeInstallOutcome::Installed], |_, layout| {
            let _ = std::fs::remove_file(layout.root.join("proton"));
            Ok(())
        });
    let (model, _, _) = make_model_with(true, None, Some(make_tool_layout().wine_app()), &installer);
    assert_eq!(model.state().status(), SteamPlayStatus::On);
    write("#!/bin/sh\n", &model.layout.root.join("proton"), true);
    model.install_runtime(false);
    assert_eq!(model.state().status(), SteamPlayStatus::RestartNeeded(1));
    model.restart_steam();
    assert_eq!(model.state().status(), SteamPlayStatus::On);
}

#[test]
fn replacing_the_passthrough_script_while_steam_runs_asks_for_a_restart() {
    // The Mac-game tool's old entry point is `passthrough.sh`; install_native_tool removes it after the install.
    let installer = ScriptedInstaller::new(&[RuntimeInstallOutcome::Unchanged, RuntimeInstallOutcome::Installed]);
    let (model, mode, _) = make_model_with(true, None, Some(make_tool_layout().wine_app()), &installer);
    assert_eq!(model.state().status(), SteamPlayStatus::On);
    write("#!/bin/sh\n", &mode.tools.join(format!("{}/passthrough.sh", SteamPlayMode::NATIVE_TOOL_NAME)), true);
    model.install_runtime(false);
    assert_eq!(model.state().status(), SteamPlayStatus::RestartNeeded(1));
}

// MARK: Valve's Steam components (spec D1, D2, D4, D5)

/// A tool folder whose runtime has Wine 0036's marker.
fn supporting_layout() -> ToolLayout {
    let layout = make_tool_layout();
    write("0036\n", &layout.wine_app().join("Contents/Resources/genuine-steamclient"), false);
    layout
}

fn cdn_client(layout: &ToolLayout, cdn: &Arc<FakeValveCDN>) -> ValveClient {
    ValveClient::new(layout.valve_cache(), cdn.fetch(), Arc::new(SystemProcessRunner::new()))
}

/// One app start: the bundled runtime is already installed (`Unchanged`), then the automatic Valve check runs.
fn make_valve_model(layout: &ToolLayout, cdn: &Arc<FakeValveCDN>, app_version: &str) -> Arc<AppModel> {
    let (mode, _) = make_mode();
    let mut config = config(&mode, layout.clone());
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.valve_client = Some(cdn_client(layout, cdn));
    config.app_version = app_version.into();
    let model = started(config);
    model.wait_valve();
    model
}

#[test]
fn startup_downloads_valves_files_when_the_runtime_supports_them() {
    let cdn = FakeValveCDN::new(&make_temp_dir());
    let model = make_valve_model(&supporting_layout(), &cdn, "1.0");
    assert_eq!(model.state().valve_version.as_deref(), Some("1788652215"));
    assert_eq!(model.state().valve_notice, None);
    assert_eq!(cdn.requests(), [ValveClient::MANIFEST_URL.to_string(), cdn.package_url()]);
}

#[test]
fn no_marker_no_download() {
    let cdn = FakeValveCDN::new(&make_temp_dir());
    let model = make_valve_model(&make_tool_layout(), &cdn, "1.0");
    assert!(cdn.requests().is_empty());
    assert_eq!(model.state().valve_version, None);
}

#[test]
fn valve_failure_leaves_the_runtime_installed() {
    // Offline at app start: the runtime is fine, launches use lsteamclient, the next app start retries.
    let cdn = FakeValveCDN::new(&make_temp_dir());
    cdn.set_offline(true);
    let layout = supporting_layout();
    let model = make_valve_model(&layout, &cdn, "1.0");
    let state = model.state();
    assert!(state.runtime.is_some());
    assert_eq!(state.runtime_notice, None);
    assert_eq!(state.error_message, None);
    assert_eq!(state.valve_version, None);
    assert!(state.valve_notice.is_some());
    cdn.set_offline(false);
    let next = make_valve_model(&layout, &cdn, "1.0");
    assert_eq!(next.state().valve_version.as_deref(), Some("1788652215"));
}

#[test]
fn remove_sticks_across_app_starts() {
    let cdn = FakeValveCDN::new(&make_temp_dir());
    let layout = supporting_layout();
    let model = make_valve_model(&layout, &cdn, "1.0");
    model.remove_valve_client();
    assert_eq!(model.state().valve_version, None);
    let next = make_valve_model(&layout, &cdn, "2.0");
    assert_eq!(cdn.requests().len(), 2);
    assert_eq!(next.state().valve_version, None);
    next.update_valve_client(true);
    assert_eq!(next.state().valve_version.as_deref(), Some("1788652215"));
}

#[test]
fn a_new_app_version_checks_again() {
    let cdn = FakeValveCDN::new(&make_temp_dir());
    let layout = supporting_layout();
    make_valve_model(&layout, &cdn, "1");
    make_valve_model(&layout, &cdn, "1");
    assert_eq!(cdn.requests().len(), 2);
    make_valve_model(&layout, &cdn, "2");
    let manifest = ValveClient::MANIFEST_URL.to_string();
    assert_eq!(cdn.requests(), [manifest.clone(), cdn.package_url(), manifest]);
}

// MARK: The microVM's host tools

/// A stand-in for MacNeutron.app's Contents/Helpers/vm.
fn make_vm_tools() -> PathBuf {
    let tools = make_temp_dir().join("vm-tools");
    for file in [
        "macneutron-vm",
        "libkrun/libkrun.1.dylib",
        "steam-forward/host/mn-steam-host.app/Contents/MacOS/mn-steam-host",
        "gvproxy",
        "metal-host/winemetal.so",
        "dxmt/d3d12.dylib",
        "run-guest.sh",
        "run-game.sh",
        "dxmt-translator",
    ] {
        write(&format!("{file}\n"), &tools.join(file), true);
    }
    tools
}

/// One app start whose runtime is already installed, with the app's VM tools and a VM folder of the test's own.
fn make_vm_model(vm_root: &Path, change: impl FnOnce(&mut AppModelConfig)) -> Arc<AppModel> {
    let (mode, _) = make_mode();
    let mut config = config(&mode, make_tool_layout());
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.valve_client = Some(offline_valve_client(make_temp_dir()));
    config.vm_tools = Some(make_vm_tools());
    config.vm_root = Some(vm_root.to_path_buf());
    change(&mut config);
    let model = started(config);
    model.wait_valve();
    model
}

// Spec 2026-10-24-macneutron-install-scripts-design §4.9: a game Steam uninstalled loses its VM drive at a refresh.
#[test]
fn a_refresh_deletes_an_uninstalled_games_drive() {
    let vm_root = make_temp_dir().join("vm");
    let logs = make_temp_dir().join("Logs");
    let model = make_vm_model(&vm_root, |config| config.log = LauncherLog::new(logs.clone()));
    let steam = model.steam.root.to_string_lossy().into_owned();
    write(
        &format!("\"libraryfolders\"\n{{\n\"0\"\n{{\n\"path\" \"{steam}\"\n\"apps\"\n{{\n}}\n}}\n}}\n"),
        &model.steam.root.join("steamapps/libraryfolders.vdf"),
        false,
    );
    let drive = vm_root.join("state/999.img");
    write("disk", &drive, false);
    // The launcher's record of where it ran from.
    write(&model.steam.root.join("steamapps").to_string_lossy(), &vm_root.join("runs/999/library"), false);
    model.refresh();
    assert!(!exists(&drive));
    let text = std::fs::read_to_string(LauncherLog::new(logs).launcher_log()).unwrap_or_default();
    assert!(text.contains("deleted the game VM drive of app 999: Steam uninstalled it ("), "{text}");
}

#[test]
fn the_vm_folder_is_never_the_real_one_unless_named() {
    // Deferred, so that nothing is written even where this fails.
    let (mode, _) = make_mode();
    let mut config = config(&mode, make_tool_layout());
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.valve_client = Some(offline_valve_client(make_temp_dir()));
    config.vm_tools = Some(make_vm_tools());
    config.vm_installer = Arc::new(|_, vm| Ok(VMToolsInstallOutcome::Deferred(vm.to_string_lossy().into_owned())));
    let model = started(config);
    assert_eq!(model.vm_root, None);
}

#[test]
fn the_vm_tools_follow_the_runtime_install() {
    let vm_root = make_temp_dir().join("vm");
    let model = make_vm_model(&vm_root, |_| {});
    assert!(exists(&vm_root.join("macneutron-vm")));
    assert!(!model.state().vm_tools_deferred);
}

#[test]
fn deferred_vm_tools_are_retried_by_the_poll() {
    let vm_root = make_temp_dir().join("vm");
    let calls = Arc::new(AtomicUsize::new(0));
    let model = make_vm_model(&vm_root, |config| {
        config.vm_installer = Arc::new(move |tools, vm| {
            if calls.fetch_add(1, Ordering::SeqCst) == 0 {
                return Ok(VMToolsInstallOutcome::Deferred(vm.join("macneutron-vm").to_string_lossy().into_owned()));
            }
            install_tools(tools, vm)
        })
    });
    assert!(model.state().vm_tools_deferred);
    assert!(!exists(&vm_root.join("macneutron-vm")));
    model.poll_runtime();
    assert!(!model.state().vm_tools_deferred);
    assert!(exists(&vm_root.join("macneutron-vm")));
}

#[test]
fn valve_files_reach_the_vm() {
    let vm_root = make_temp_dir().join("vm");
    let layout = supporting_layout();
    let cdn = FakeValveCDN::new(&make_temp_dir());
    let model = make_vm_model(&vm_root, |config| {
        config.valve_client = Some(cdn_client(&layout, &cdn));
        config.layout = layout.clone();
    });
    let cached = ValveClient::installed(&model.valve_client.cache).unwrap().steam_client64();
    assert_eq!(std::fs::read(vm_root.join("share/valve/steamclient64.dll")).unwrap(), std::fs::read(cached).unwrap());
}

// MARK: The game VM's setup

/// A stand-in for the app's CLI: each run appends its arguments to `calls`, then runs `body`.
fn fake_helper(body: &str) -> (PathBuf, PathBuf) {
    let dir = make_temp_dir();
    let (calls, helper) = (dir.join("calls"), dir.join("macneutron"));
    write(&format!("#!/bin/sh\necho \"$*\" >> '{}'\n{body}\n", calls.display()), &helper, true);
    (helper, calls)
}

fn calls(file: &Path) -> Vec<String> {
    let text = std::fs::read_to_string(file).unwrap_or_default();
    text.split('\n').filter(|line| !line.is_empty()).map(str::to_string).collect()
}

/// One app start whose runtime is installed, with the app's VM tools, a manifest, and `helper` as its CLI.
fn make_setup_model(
    helper: &Path,
    vm_root: &Path,
    manifest: Option<&Path>,
    await_setup: bool,
    change: impl FnOnce(&mut AppModelConfig),
) -> Arc<AppModel> {
    let (mode, _) = make_mode();
    let mut config = config(&mode, make_tool_layout());
    config.wine_app_source = Some(make_tool_layout().wine_app());
    config.valve_client = Some(offline_valve_client(make_temp_dir()));
    config.app_version = "1.0".into();
    config.vm_tools = Some(make_vm_tools());
    config.vm_root = Some(vm_root.to_path_buf());
    config.vm_manifest = manifest.map(Path::to_path_buf);
    config.vm_helper = Some(helper.to_path_buf());
    config.vm_retry_interval = Duration::ZERO;
    change(&mut config);
    let model = started(config);
    if await_setup {
        model.wait_vm_setup();
    }
    model
}

fn make_manifest() -> PathBuf {
    let manifest = make_temp_dir().join("vm-manifest.json");
    write("{\"version\": 1}\n", &manifest, false);
    manifest
}

/// `vm_root`'s images are `manifest`'s.
fn current_images(manifest: &Path, vm_root: &Path) {
    write(&format!("{}\n", VMSetup::manifest_hash(manifest).unwrap()), &vm_root.join("images/manifest.sha256"), false);
}

fn wait_for(file: &Path) {
    let start = Instant::now();
    while !exists(file) && start.elapsed() < Duration::from_secs(5) {
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[test]
fn notice_texts_are_exact() {
    let notice = AppModel::notice_for_progress_line;
    assert_eq!(notice("progress download 400000000 1350000000").as_deref(), Some("Game VM: downloading (0.4 of 1.35 GB)"));
    assert_eq!(notice("progress assemble 2 5").as_deref(), Some("Game VM: building its images (2 of 5)"));
    assert_eq!(notice("progress install 0 1"), None);
    assert_eq!(notice("vm-setup: a note"), None);
}

#[test]
fn a_needed_setup_starts_after_the_tools() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let order = make_temp_dir().join("order");
    let (helper, calls_file) = fake_helper(&format!(
        "[ -e '{}/macneutron-vm' ] && echo tools-first >> '{}'\necho 'progress download 400000000 1350000000'\necho 'progress assemble 2 5'\nexit 0",
        vm_root.display(),
        order.display()
    ));
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert_eq!(calls(&calls_file), [format!("vm-setup --manifest {} --vm {}", manifest.display(), vm_root.display())]);
    assert_eq!(read(&order), "tools-first\n");
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: ready"));
}

#[test]
fn a_current_image_set_starts_nothing() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    current_images(&manifest, &vm_root);
    let (helper, calls_file) = fake_helper("exit 0");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(calls(&calls_file).is_empty());
    assert_eq!(model.state().vm_notice, None);
}

#[test]
fn a_deferral_is_retried_by_the_poll() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let flag = make_temp_dir().join("ran");
    let (helper, calls_file) = fake_helper(&format!("[ -e '{0}' ] && exit 0\ntouch '{0}'\nexit 75", flag.display()));
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: waiting for the game to quit"));
    model.poll_runtime();
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: ready"));
    assert_eq!(calls(&calls_file).len(), 2);
}

#[test]
fn a_failed_setup_waits_for_retry() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let (helper, calls_file) = fake_helper("echo 'vm-setup: it needs 6 GB free' >&2\nexit 1");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: vm-setup: it needs 6 GB free"));
    for _ in 0..3 {
        model.poll_runtime();
    }
    assert_eq!(calls(&calls_file).len(), 1);
    model.set_up_vm();
    assert_eq!(calls(&calls_file).len(), 2);
}

#[test]
fn without_a_manifest_nothing_starts() {
    let vm_root = make_temp_dir().join("vm");
    let (helper, calls_file) = fake_helper("exit 0");
    let model = make_setup_model(&helper, &vm_root, None, true, |_| {});
    assert!(calls(&calls_file).is_empty());
    assert_eq!(model.state().vm_notice, None);
}

#[test]
fn quitting_stops_the_setup() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let pid_file = make_temp_dir().join("pid");
    let (helper, _) = fake_helper(&format!("echo $$ > '{}'\nexec sleep 60", pid_file.display()));
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), false, |_| {});
    let start = Instant::now();
    wait_for(&pid_file);
    let pid: i32 = read(&pid_file).trim().parse().unwrap();
    model.will_terminate(); // NSApplication.willTerminateNotification
    model.wait_vm_setup();
    assert!(start.elapsed() < Duration::from_secs(10));
    assert_ne!(unsafe { libc::kill(pid, 0) }, 0);
}

#[test]
fn rebuild_keeps_the_games_files() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    current_images(&manifest, &vm_root);
    let kept = ["state/1.img", "state/1.tpm/id", "runs/1/run.log"];
    for path in kept {
        write(&format!("game {path}\n"), &vm_root.join(path), false);
    }
    write("half a download\n", &vm_root.join("setup/cache/x"), false);
    let seen = make_temp_dir().join("setup-seen");
    let (helper, calls_file) =
        fake_helper(&format!("[ -e '{}/setup' ] && touch '{}'\nexit 0", vm_root.display(), seen.display()));
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(calls(&calls_file).is_empty()); // the images are current
    model.rebuild_vm();
    assert_eq!(calls(&calls_file).len(), 1);
    assert!(!exists(&seen));
    for path in kept {
        assert_eq!(read(&vm_root.join(path)), format!("game {path}\n"));
    }
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: ready"));
}

#[test]
fn rebuild_while_a_game_runs_waits() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    current_images(&manifest, &vm_root);
    let (helper, _) = fake_helper("exit 75");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    model.rebuild_vm();
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: waiting for the game to quit"));
    assert!(exists(&vm_root.join("images/manifest.sha256")));
}

/// Like the real setup, it leaves the manifest's images behind.
fn images_writing_helper(link: &Path, manifest: &Path) -> (PathBuf, PathBuf) {
    let hash = VMSetup::manifest_hash(manifest).unwrap();
    let link = link.display();
    fake_helper(&format!("mkdir -p '{link}/images'\necho {hash} > '{link}/images/manifest.sha256'"))
}

fn entries(folder: &Path) -> Vec<String> {
    let mut names: Vec<String> =
        std::fs::read_dir(folder).unwrap().map(|entry| entry.unwrap().file_name().into_string().unwrap()).collect();
    names.sort();
    names
}

#[test]
fn rebuild_replaces_a_link_to_a_missing_folder() {
    // The folder linked to a developer's build/vm-work, then build/ was deleted: Rebuild makes a real folder.
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    std::os::unix::fs::symlink("/nonexistent/build/vm-work", &vm_root).unwrap();
    let (helper, calls_file) = images_writing_helper(&vm_root, &manifest);
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(calls(&calls_file).is_empty());
    assert_eq!(model.vm_folder_missing().as_deref(), Some("/nonexistent/build/vm-work"));
    assert_eq!(
        model.vm_status().as_deref(),
        Some("Game VM: its folder /nonexistent/build/vm-work is missing. Reconnect its drive, or Rebuild to start a new VM here.")
    );
    assert_eq!(model.state().error_message, None);
    model.rebuild_vm();
    assert!(std::fs::read_link(&vm_root).is_err());
    assert!(exists(&vm_root.join("macneutron-vm")));
    assert_eq!(calls(&calls_file).len(), 1);
    assert_eq!(model.vm_status().as_deref(), Some("Game VM: ready"));
    assert!(model.vm_ready());
}

#[test]
fn rebuild_replaces_a_link_to_a_developers_folder() {
    // A release app on the folder a developer linked to build/vm-work: Rebuild makes a real folder, and the developer's
    // folder keeps everything it had.
    let (dev, manifest) = (make_temp_dir().join("vm-work"), make_manifest());
    write("dev\n", &dev.join("images/manifest.sha256"), false);
    write("game\n", &dev.join("state/1.img"), false);
    let vm_root = make_temp_dir().join("vm");
    std::os::unix::fs::symlink(&dev, &vm_root).unwrap();
    let (helper, calls_file) = images_writing_helper(&vm_root, &manifest);
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(calls(&calls_file).is_empty());
    assert!(!model.vm_rebuild_off());
    model.rebuild_vm();
    assert!(std::fs::read_link(&vm_root).is_err());
    assert!(exists(&vm_root.join("macneutron-vm")));
    assert_eq!(calls(&calls_file).len(), 1);
    assert_eq!(model.vm_status().as_deref(), Some("Game VM: ready"));
    assert_eq!(entries(&dev), ["images", "state"]);
    assert_eq!(read(&dev.join("images/manifest.sha256")), "dev\n");
    assert_eq!(read(&dev.join("state/1.img")), "game\n");
}

#[test]
fn a_developers_own_folder_turns_rebuild_off_and_says_why() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    write("dev\n", &vm_root.join("images/manifest.sha256"), false);
    let (helper, calls_file) = fake_helper("exit 0");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(model.vm_rebuild_off());
    assert_eq!(
        model.vm_status().as_deref(),
        Some("Game VM: a developer's folder (its images are vm's own build). MacNeutron never changes it, so Rebuild is off.")
    );
    model.rebuild_vm();
    assert!(calls(&calls_file).is_empty());
    assert_eq!(entries(&vm_root), ["images"]);
}

#[test]
fn a_missing_folder_waits_for_its_drive() {
    // A player's VM on a drive that isn't connected: the app waits, and carries on once the drive is back.
    let (drive, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let vm_root = make_temp_dir().join("vm");
    std::os::unix::fs::symlink(&drive, &vm_root).unwrap();
    write("game\n", &drive.join("state/1.img"), false);
    let away = drive.parent().unwrap().join("unplugged");
    std::fs::rename(&drive, &away).unwrap();
    let (helper, calls_file) = fake_helper("exit 0");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    model.poll_runtime();
    assert!(model.vm_folder_missing().is_some());
    assert!(!model.vm_ready());
    assert!(calls(&calls_file).is_empty());
    std::fs::rename(&away, &drive).unwrap(); // plugged back in
    model.poll_runtime();
    model.wait_vm_setup();
    assert_eq!(model.vm_folder_missing(), None);
    assert!(exists(&drive.join("macneutron-vm")));
    assert_eq!(calls(&calls_file).len(), 1);
    assert_eq!(read(&drive.join("state/1.img")), "game\n");
}

#[test]
fn ready_means_the_launcher_would_run_it() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    current_images(&manifest, &vm_root);
    let (helper, _) = fake_helper("exit 0");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |_| {});
    assert!(model.vm_ready());
    std::fs::remove_file(vm_root.join("macneutron-vm")).unwrap();
    assert!(!model.vm_setup_needed());
    assert!(!model.vm_ready()); // the launcher would refuse a game: Setup mustn't say Ready
}

#[test]
fn progress_split_across_writes_is_one_line() {
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let log = make_temp_dir().join("vm-setup.log");
    let (helper, _) = fake_helper("printf 'progress assem'\nsleep 0.2\nprintf 'ble 1 5\\n'\nexit 0");
    make_setup_model(&helper, &vm_root, Some(&manifest), true, |config| config.vm_setup_log = log.clone());
    assert!(read(&log).contains("progress assemble 1 5\n"));
}

#[test]
fn a_setup_starts_with_a_fresh_status() {
    // Retry after a failure, or a first run: not the old text, and not nothing, while the first file downloads.
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let started_file = make_temp_dir().join("started");
    let (helper, _) = fake_helper(&format!("touch '{}'\nsleep 1\nexit 0", started_file.display()));
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), false, |_| {});
    wait_for(&started_file);
    assert_eq!(model.state().vm_notice.as_deref(), Some("Game VM: starting…"));
    model.wait_vm_setup();
}

#[test]
fn a_deferral_waits_before_its_retry() {
    // A game session is long: the poll (every 3 s) retries a deferred setup every 30 s, not every tick.
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let (helper, calls_file) = fake_helper("exit 75");
    let model = make_setup_model(&helper, &vm_root, Some(&manifest), true, |config| {
        config.vm_retry_interval = Duration::from_secs(30)
    });
    model.poll_runtime();
    assert_eq!(calls(&calls_file).len(), 1);
}

#[test]
fn only_one_tools_install_at_a_time() {
    let vm_root = make_temp_dir().join("vm");
    let count = Arc::new(AtomicUsize::new(0));
    let counted = count.clone();
    let model = make_vm_model(&vm_root, |config| {
        config.vm_installer = Arc::new(move |tools, vm| {
            counted.fetch_add(1, Ordering::SeqCst);
            std::thread::sleep(Duration::from_millis(300));
            install_tools(tools, vm)
        })
    });
    let before = count.load(Ordering::SeqCst);
    std::thread::scope(|scope| {
        scope.spawn(|| model.install_vm_tools());
        scope.spawn(|| model.install_vm_tools());
    });
    assert_eq!(count.load(Ordering::SeqCst) - before, 1);
}

#[test]
fn without_the_apps_vm_tools_there_is_no_vm_folder() {
    // A dev run or a test never names the real ~/Library folder.
    let (model, _, _) = make_model();
    assert_eq!(model.vm_root, None);
}

#[test]
fn a_performance_build_keeps_the_setup_log_for_failures() {
    // X.Y.Zp: vm-setup.log is written only when a setup fails (not 0, not 75).
    let (vm_root, manifest) = (make_temp_dir().join("vm"), make_manifest());
    let log = make_temp_dir().join("vm-setup.log");
    let performance = |config: &mut AppModelConfig| {
        config.vm_setup_log = log.clone();
        config.app_version = "0.7.1p".into();
    };
    let (ok, _) = fake_helper("echo 'progress assemble 1 5'\nexit 0");
    make_setup_model(&ok, &vm_root, Some(&manifest), true, performance);
    assert!(!exists(&log));
    let other = make_temp_dir().join("vm");
    let (bad, _) = fake_helper("echo 'progress assemble 1 5'\necho 'no disk space' >&2\nexit 1");
    make_setup_model(&bad, &other, Some(&manifest), true, performance);
    assert!(read(&log).contains("no disk space"));
}
