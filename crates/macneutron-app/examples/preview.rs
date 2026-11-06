//! Draws the menu and the four windows from a made-up state and writes them to a folder (`menu.txt`, `<Scene>.png`),
//! to look at the AppKit port without running the app. Nothing real is touched: HOME is a fresh temporary folder, the
//! model is never started (no install, no Steam watcher), Steam is never asked anything, and nothing is clicked.
//!
//! `cargo run -p macneutron-app --example preview -- <folder>`

use std::collections::{BTreeMap, BTreeSet, HashSet};
use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, SystemTime};

use objc2::MainThreadMarker;
use objc2_app_kit::{NSApplication, NSApplicationActivationPolicy};

use macneutron_app::app_model::{AppModel, AppModelConfig, InstalledRuntime, Snapshot};
use macneutron_app::ui;
use macneutron_app::update_model::{PlistDefaults, UpdateModel, UpdateServices};
use macneutron_core::test_support::{releases_json, RecordingNotifier};
use macneutron_core::{AppInfo, GameSettings, OrphanPrefix, RunAs, SteamPlayStatus, VMToolsInstallOutcome};

fn app(app_id: u32, name: &str, oslist: &[&str]) -> AppInfo {
    let oslist: HashSet<String> = oslist.iter().map(|os| os.to_string()).collect();
    AppInfo { app_id, name: name.into(), r#type: "game".into(), oslist }
}

fn main() {
    // First: every default path (Steam, the tool folder, logs, preferences, the VM) is under HOME.
    let home = std::env::temp_dir().join(format!("macneutron-preview-{}", std::process::id()));
    std::fs::create_dir_all(&home).unwrap();
    std::env::set_var("HOME", &home);
    let out = std::env::args().nth(1).map(PathBuf::from).unwrap_or_else(|| home.join("shots"));
    std::fs::create_dir_all(&out).unwrap();

    let mut config = AppModelConfig::standard();
    config.installer = Arc::new(|_, _, _, _| panic!("the preview installs nothing"));
    config.vm_installer = Arc::new(|_, _| Ok(VMToolsInstallOutcome::Unchanged));
    config.wine_app_source = None;
    config.vm_tools = None;
    config.vm_manifest = Some(home.join("vm-manifest.json"));
    config.poll = None;
    let model = AppModel::new(config);

    let mut settings = BTreeMap::new();
    settings.insert(
        "2977660".to_string(),
        GameSettings { dlss: Some(true), upscale: Some("quality".into()), ..GameSettings::default() },
    );
    settings.insert("3419430".to_string(), GameSettings { run_as: Some(RunAs::Windows), ..GameSettings::default() });
    model.apply(
        Snapshot {
            runtime: Some(InstalledRuntime { version: "1.0.0".into(), identity: "0123456789abcdef".into() }),
            apps: vec![
                app(1062090, "Timberborn", &["windows", "macos"]),
                app(3419430, "Bongo Cat", &["windows", "macos"]),
                app(2977660, "Cats", &["windows"]),
                app(813780, "Age of Empires II: Definitive Edition", &["windows"]),
                app(570, "Dota 2", &["macos", "windows", "linux"]),
            ],
            app_info_error: None,
            installed: BTreeSet::from([2977660, 570, 813780]),
            settings,
            orphans: vec![OrphanPrefix { app_id: "440".into(), url: home.join("440"), bytes: 1_234_567_890 }],
            status: SteamPlayStatus::RestartNeeded(1),
            steam_launch_options: BTreeMap::from([(2977660, "DXMT_D3D12_SM6=0 %command%".to_string())]),
        },
        0,
    );

    let reply = releases_json(&[("v9.0.0", false, false, &["MacNeutron-9.0.0.zip", "SHA256SUMS"])], "https://example.invalid");
    let services = UpdateServices {
        defaults: Box::new(PlistDefaults::new(home.join("defaults.plist"))),
        notifier: Arc::new(RecordingNotifier::default()),
        get: Arc::new(move |_| Ok(reply.clone())),
        now: Box::new(SystemTime::now),
        open: Box::new(|_, _| panic!("the preview installs nothing")),
        terminate: Box::new(|| panic!("the preview quits by itself")),
        hourly: false,
    };
    let updates = UpdateModel::new("1.0.0", home.join("MacNeutron.app"), Some("TEAMID".into()), services);
    updates.check();

    let mtm = MainThreadMarker::new().unwrap();
    NSApplication::sharedApplication(mtm).setActivationPolicy(NSApplicationActivationPolicy::Prohibited);
    ui::preview(mtm, model, updates, &out, Some(2977660));
    std::thread::sleep(Duration::from_millis(10));
    let _ = std::fs::remove_dir_all(home.join("Library"));
    println!("{}", out.display());
}
