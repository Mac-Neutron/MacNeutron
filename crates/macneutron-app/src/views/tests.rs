//! The views' texts and enabled controls for a state, without a window.

use super::*;
use crate::app_model::InstalledRuntime;
use macneutron_core::{AppInfo, Release};
use std::collections::HashSet;
use std::path::PathBuf;

fn inputs(state: State) -> Inputs {
    Inputs {
        state,
        status: SteamPlayStatus::Off,
        updates: UpdateState::default(),
        updates_enabled: false,
        steam_installed: true,
        steam_play_wanted: false,
        has_vm_manifest: false,
        vm_status: None,
        vm_ready: false,
        vm_rebuild_off: false,
        vm_folder_missing: None,
    }
}

fn state(change: impl FnOnce(&mut State)) -> State {
    let mut state = State::default();
    change(&mut state);
    state
}

fn runtime() -> InstalledRuntime {
    InstalledRuntime { version: "1.2".into(), identity: "0123456789abcdef".into() }
}

fn row(app_id: u32, name: &str, oslist: &[&str], installed: bool) -> GameRow {
    let oslist: HashSet<String> = oslist.iter().map(|os| os.to_string()).collect();
    GameRow {
        app: AppInfo { app_id, name: name.into(), r#type: "game".into(), oslist },
        installed,
        settings: GameSettings::default(),
        steam_launch_options: None,
    }
}

fn orphan(app_id: &str, bytes: i64) -> OrphanPrefix {
    OrphanPrefix { app_id: app_id.into(), url: PathBuf::from("/nowhere"), bytes }
}

fn titles(entries: &[MenuEntry]) -> Vec<String> {
    entries
        .iter()
        .map(|entry| match entry {
            MenuEntry::Text(text) => format!("text {text}"),
            MenuEntry::Button(title, _) => title.clone(),
            MenuEntry::Divider => "-".into(),
        })
        .collect()
}

#[test]
fn menu_before_setup() {
    let entries = menu(&inputs(State::default()));
    assert_eq!(
        titles(&entries),
        [
            "text Steam Play mode off",
            "text Runtime not installed",
            "-",
            "Finish setup…",
            "Games…",
            "Open Steam",
            "Show logs",
            "-",
            "Settings…",
            "Quit MacNeutron"
        ]
    );
}

#[test]
fn menu_with_everything_to_say() {
    let mut state = state(|s| {
        s.runtime = Some(runtime());
        s.busy = Some("Restarting Steam…".into());
        s.error_message = Some("It broke".into());
        s.orphans = vec![orphan("1", 1_000_000), orphan("2", 500_000)];
    });
    state.games = vec![];
    let mut inputs = inputs(state);
    inputs.steam_play_wanted = true;
    inputs.status = SteamPlayStatus::RestartNeeded(2);
    inputs.updates.available = Some(Release {
        version: "2.0.0".into(),
        page: "https://example.invalid".into(),
        zip: String::new(),
        sums: String::new(),
        installer: None,
    });
    let entries = menu(&inputs);
    assert_eq!(
        titles(&entries),
        [
            "text Restart Steam to apply 2 changes",
            "text Runtime 1.2 (0123456789ab)",
            "text Restarting Steam…",
            "text It broke",
            "Dismiss error",
            "Restart Steam",
            "-",
            "Games…",
            "Open Steam",
            &format!("Free up space ({})", file_size(1_500_000)),
            "Show logs",
            "-",
            "Update to 2.0.0…",
            "Settings…",
            "Quit MacNeutron"
        ]
    );
    assert_eq!(file_size(1_500_000), "1.5 MB");
    assert!(entries.contains(&MenuEntry::Button("Dismiss error".into(), Act::DismissError)));
}

#[test]
fn a_lost_mode_offers_to_restore_it() {
    let mut inputs = inputs(State::default());
    inputs.status = SteamPlayStatus::Lost;
    assert!(menu(&inputs).contains(&MenuEntry::Button("Restore Steam Play mode".into(), Act::RestartSteam)));
}

fn enabled(rows: &[Row], wanted: &str) -> Option<bool> {
    rows.iter().find_map(|row| match row {
        Row::Switch { title, enabled, .. } if title == wanted => Some(*enabled),
        Row::Labeled { controls, .. } => controls.iter().find_map(|control| match control {
            Control::Button { title, enabled, .. } | Control::Check { title, enabled, .. } if title == wanted => {
                Some(*enabled)
            }
            _ => None,
        }),
        _ => None,
    })
}

#[test]
fn settings_controls_follow_the_state() {
    let mut inputs = inputs(state(|s| {
        s.valve_version = Some("1700000000".into());
        s.vm_setup_failed = true;
    }));
    inputs.has_vm_manifest = true;
    inputs.steam_play_wanted = true;
    let rows = settings(&inputs);
    assert_eq!(enabled(&rows, "Repair runtime"), Some(true));
    assert_eq!(enabled(&rows, "Remove"), Some(true));
    assert_eq!(enabled(&rows, "Retry"), Some(true));
    assert_eq!(enabled(&rows, "Rebuild the game VM"), Some(true));
    assert_eq!(enabled(&rows, "Turn off Steam Play mode"), Some(true));
    assert_eq!(enabled(&rows, "Check now"), None, "updates need a team");
    assert!(rows.contains(&Row::Labeled {
        label: "Steam components from Valve".into(),
        tone: None,
        controls: vec![
            Control::Text("Version 1700000000".into(), Tone::Secondary),
            button("Download again", Act::DownloadValve, true),
            button("Remove", Act::RemoveValve, true),
        ],
    }));

    inputs.state.installing = true;
    inputs.state.valve_updating = true;
    inputs.state.vm_setup_running = true;
    inputs.steam_play_wanted = false;
    let rows = settings(&inputs);
    assert_eq!(enabled(&rows, "Repair runtime"), Some(false));
    assert_eq!(enabled(&rows, "Download again"), Some(false));
    assert_eq!(enabled(&rows, "Remove"), Some(false));
    assert_eq!(enabled(&rows, "Rebuild the game VM"), Some(false));
    assert_eq!(enabled(&rows, "Turn off Steam Play mode"), Some(false));

    inputs.state.vm_setup_running = false;
    inputs.vm_rebuild_off = true;
    assert_eq!(enabled(&settings(&inputs), "Rebuild the game VM"), Some(false));
}

#[test]
fn a_busy_model_disables_every_setting() {
    let mut inputs = inputs(state(|s| {
        s.busy = Some("Restarting Steam…".into());
    }));
    inputs.updates_enabled = true;
    let rows = settings(&inputs);
    for title in ["Open MacNeutron at login", "Check automatically", "Check now", "Repair runtime", "Run setup again"] {
        assert_eq!(enabled(&rows, title), Some(false), "{title}");
    }
    assert_eq!(rows.last(), Some(&Row::Progress("Restarting Steam…".into())));
}

#[test]
fn settings_show_updates_and_the_login_approval() {
    let mut inputs = inputs(state(|s| {
        s.login_item_status = LoginItemStatus::RequiresApproval;
        s.error_message = Some("Nope".into());
    }));
    inputs.updates_enabled = true;
    inputs.updates.working = true;
    inputs.updates.notice = Some("Downloading and checking MacNeutron 2.0.0…".into());
    inputs.updates.available =
        Some(Release { version: "2.0.0".into(), page: "https://page".into(), zip: String::new(), sums: String::new(), installer: None });
    let rows = settings(&inputs);
    assert_eq!(
        rows[0],
        Row::Switch { title: "Open MacNeutron at login".into(), on: true, act: Act::SetLaunchAtLogin, enabled: true }
    );
    assert_eq!(rows[1], Row::Labeled {
        label: "Allow MacNeutron under Login Items in System Settings".into(),
        tone: Some(Tone::Orange),
        controls: vec![button("Open Login Items", Act::OpenLoginItems, true)],
    });
    assert_eq!(enabled(&rows, "Check now"), Some(false));
    assert_eq!(enabled(&rows, "Install and relaunch"), Some(false));
    assert!(rows.contains(&Row::Labeled {
        label: "MacNeutron 2.0.0 is available".into(),
        tone: None,
        controls: vec![
            Control::Link { title: "Release notes".into(), url: "https://page".into(), enabled: true },
            button("Install and relaunch", Act::InstallUpdate, false),
        ],
    }));
    assert!(rows.contains(&Row::Note("Downloading and checking MacNeutron 2.0.0…".into(), Tone::Secondary)));
    assert_eq!(rows.last(), Some(&Row::Note("Nope".into(), Tone::Red)));
}

#[test]
fn the_rebuild_dialog_names_a_missing_folder() {
    let mut inputs = inputs(State::default());
    assert_eq!(rebuild_confirmation(&inputs).message, "It downloads about 1.35 GB again. Games' saves and settings are kept.");
    inputs.vm_folder_missing = Some("/Volumes/Games/vm".into());
    assert_eq!(
        rebuild_confirmation(&inputs).message,
        "It downloads about 1.35 GB and starts a new VM here. The games' disks in the missing folder won't be used."
    );
}

#[test]
fn cleanup_names_its_rows_and_counts_the_deletion() {
    let state = state(|s| {
        s.games = vec![row(10, "Cats", &["windows"], false)];
        s.orphans = vec![orphan("10", 2_000_000), orphan("20", 3_000)];
    });
    let none = BTreeSet::new();
    let view = cleanup(&state, &none);
    assert_eq!(view.rows.iter().map(|row| row.name.as_str()).collect::<Vec<_>>(), ["Cats", "App 20"]);
    assert_eq!(view.rows[0].size, "2 MB");
    assert!(!view.delete_enabled);

    let one: BTreeSet<String> = ["10".to_string()].into();
    assert!(cleanup(&state, &one).delete_enabled);
    assert_eq!(
        delete_confirmation(&state, &one),
        Confirmation {
            title: "Delete 1 game's data?".into(),
            message: "Cats\n\nThis can't be undone.".into(),
            button: "Delete".into()
        }
    );
    let both: BTreeSet<String> = ["10".to_string(), "20".to_string(), "gone".to_string()].into();
    assert_eq!(delete_confirmation(&state, &both).title, "Delete 2 games' data?");
    assert_eq!(delete_confirmation(&state, &both).message, "Cats, App 20\n\nThis can't be undone.");
}

#[test]
fn setup_summaries_prefer_installed_games() {
    let a = row(1, "A", &["windows"], false);
    let b = row(2, "B", &["windows"], false);
    let c = row(3, "C", &["windows"], false);
    let d = row(4, "D", &["windows"], false);
    assert_eq!(summary(&[]), "None");
    assert_eq!(summary(&[&a, &b]), "A, B");
    assert_eq!(summary(&[&a, &b, &c, &d]), "A, B, C…");
    let installed: Vec<GameRow> =
        (1..=5).map(|id| row(id, &format!("I{id}"), &["windows"], true)).collect();
    let refs: Vec<&GameRow> = installed.iter().chain([&a]).collect();
    assert_eq!(summary(&refs), "I1, I2, I3, I4…");
    assert_eq!(summary(&[&a, &installed[0]]), "I1…");
}

#[test]
fn setup_steps_follow_the_state() {
    let mut inputs = inputs(state(|s| {
        s.games = vec![row(1, "Cats", &["windows"], true), row(2, "Mac Game", &["macos"], false)];
        s.installing = true;
    }));
    inputs.steam_installed = false;
    let view = setup(&inputs);
    assert_eq!(view.steps.iter().map(|step| step.title.as_str()).collect::<Vec<_>>(), ["Steam", "Runtime", "Steam Play"]);
    assert_eq!(view.steps[0].detail, "Install Steam for Mac first.");
    assert_eq!(view.steps[1].detail, "Not installed.");
    assert_eq!(view.steps[1].action, StepAction::Spinner);
    assert_eq!(
        view.steps[2].action,
        StepAction::Button { title: "Turn on and restart Steam".into(), act: Act::TurnOnSteamPlay, enabled: false }
    );
    assert_eq!(
        view.games,
        Some(["Stay native (1)".into(), "Run with MacNeutron (1)".into(), "Mac Game".into(), "Cats".into()])
    );

    inputs.steam_installed = true;
    inputs.state.runtime = Some(runtime());
    inputs.state.installing = false;
    inputs.has_vm_manifest = true;
    inputs.steam_play_wanted = true;
    let view = setup(&inputs);
    assert_eq!(view.steps[1].detail, "Runtime 1.2 (0123456789ab) installed");
    assert_eq!(view.steps[2].title, "Game VM");
    assert_eq!(view.steps[2].detail, "Sets itself up after the runtime.");
    assert!(matches!(view.steps[3].action, StepAction::Button { enabled: true, .. }));
    assert!(view.steps[3].done);
    assert_eq!(view.games, None);

    inputs.state.runtime_notice = Some("Installing the runtime…".into());
    inputs.vm_ready = true;
    let view = setup(&inputs);
    assert_eq!(view.steps[1].detail, "Installing the runtime…");
    assert_eq!(view.steps[2].detail, "Ready.");
}

#[test]
fn games_cells_show_pickers_only_where_they_apply() {
    let dual = row(1, "Dual", &["windows", "macos"], true);
    let windows = row(2, "Win", &["windows"], false);
    let mac = row(3, "Mac", &["macos"], false);
    assert_eq!(runs_as_cell(&dual), Cell::Picker { options: &RUNS_AS, selected: "mac".into() });
    assert_eq!(runs_as_cell(&windows), Cell::Text("Windows".into()));
    assert_eq!(runs_as_cell(&mac), Cell::Text("Mac version".into()));
    assert_eq!(graphics_cell(&dual), Cell::Text("Native".into()));
    assert_eq!(graphics_cell(&windows), Cell::Picker { options: &GRAPHICS, selected: String::new() });

    let mut old = windows.clone();
    old.settings.graphics = Some("d3dmetal".into());
    assert_eq!(graphics_cell(&old), Cell::Picker { options: &GRAPHICS, selected: String::new() });
    old.settings.graphics = Some("wined3d".into());
    assert_eq!(graphics_cell(&old), Cell::Picker { options: &GRAPHICS, selected: "wined3d".into() });

    let mut switched = dual.clone();
    switched.settings.run_as = Some(RunAs::Windows);
    assert_eq!(runs_as_cell(&switched), Cell::Picker { options: &RUNS_AS, selected: "windows".into() });
    assert!(matches!(graphics_cell(&switched), Cell::Picker { .. }));
    assert_eq!(run_as_setting("mac"), None);
    assert_eq!(run_as_setting("windows"), Some(RunAs::Windows));
}

#[test]
fn the_search_ignores_case_and_the_detail_needs_a_macneutron_game() {
    let rows = vec![row(1, "Timberborn", &["windows", "macos"], true), row(2, "Cats", &["windows"], false)];
    assert_eq!(filter(&rows, "").len(), 2);
    assert_eq!(filter(&rows, "TIMBER").iter().map(GameRow::id).collect::<Vec<_>>(), [1]);
    assert!(filter(&rows, "dog").is_empty());
    assert_eq!(detail(&rows, Some(2)).map(|row| row.id()), Some(2));
    assert_eq!(detail(&rows, Some(1)), None, "a Mac game runs natively");
    assert_eq!(detail(&rows, None), None);
}

#[test]
fn game_toggles_store_only_what_differs_from_the_default() {
    let mut settings = GameSettings::default();
    assert!(GameToggle::Msync.is_on(&settings));
    assert!(GameToggle::MetalFX.is_on(&settings));
    assert!(!GameToggle::Log.is_on(&settings));
    assert!(!GameToggle::FrameGen.is_enabled(&settings));
    GameToggle::DLSS.set(&mut settings, true);
    assert_eq!(settings.dlss, Some(true));
    assert!(GameToggle::FrameGen.is_enabled(&settings));
    GameToggle::Msync.set(&mut settings, false);
    assert_eq!(settings.msync, Some(false));
    GameToggle::Msync.set(&mut settings, true);
    assert_eq!(settings.msync, None);
    GameToggle::DLSS.set(&mut settings, false);
    assert_eq!(settings.dlss, None);
    assert_eq!(GameToggle::ALL.map(GameToggle::title), ["Log", "Metal HUD", "msync", "MetalFX upscaling", "DLSS", "Frame generation"]);
}

#[test]
fn game_pickers_show_unknown_values_as_the_default() {
    let mut settings = GameSettings::default();
    assert_eq!(GamePicker::ALL.map(|picker| picker.selection(&settings)), ["off", "off", "16"]);
    settings.upscale = Some("huge".into());
    settings.post_aa = Some("fxaa".into());
    settings.anisotropy = Some("2".into());
    assert_eq!(GamePicker::ALL.map(|picker| picker.selection(&settings)), ["off", "off", "16"]);
    GamePicker::Upscale.set(&mut settings, "balanced");
    GamePicker::PostAA.set(&mut settings, "cmaa2");
    GamePicker::Anisotropy.set(&mut settings, "game");
    assert_eq!(GamePicker::ALL.map(|picker| picker.selection(&settings)), ["balanced", "cmaa2", "game"]);
    GamePicker::Anisotropy.set(&mut settings, "16");
    GamePicker::Upscale.set(&mut settings, "off");
    assert_eq!((&settings.anisotropy, &settings.upscale), (&None, &None));
    assert!(GamePicker::Upscale.is_enabled(&settings));
    settings.metal_fx = Some(false);
    assert!(!GamePicker::Upscale.is_enabled(&settings));
    assert!(GamePicker::Anisotropy.is_enabled(&settings));
}

#[test]
fn launch_options_save_only_a_change() {
    let mut settings = GameSettings::default();
    assert_eq!(launch_options_change(&settings, "  "), None);
    assert_eq!(launch_options_change(&settings, " -dx12\t"), Some(Some("-dx12".into())));
    settings.launch_options = Some("-dx12".into());
    assert_eq!(launch_options_change(&settings, "-dx12 "), None);
    assert_eq!(launch_options_change(&settings, ""), Some(None));
}

#[test]
fn steam_launch_lines_list_their_problems() {
    let mut game = row(2, "Cats", &["windows"], false);
    assert!(steam_launch_lines(&game).is_empty());
    game.steam_launch_options = Some("MACNEUTRON_GRAPHICS=dxmt %command%".into());
    game.settings.graphics = Some("wined3d".into());
    assert_eq!(
        steam_launch_lines(&game),
        [
            ("Steam's launch options: MACNEUTRON_GRAPHICS=dxmt %command%".to_string(), Tone::Secondary),
            ("Steam can't start the game with this line: begin it with /usr/bin/env".to_string(), Tone::Orange),
            ("Steam's launch options override: MACNEUTRON_GRAPHICS".to_string(), Tone::Orange),
        ]
    );
}

#[test]
fn the_restart_bar_shows_only_while_a_restart_is_needed() {
    assert_eq!(restart_bar(&SteamPlayStatus::On), None);
    assert_eq!(restart_bar(&SteamPlayStatus::RestartNeeded(1)), Some("Restart Steam to apply 1 change".into()));
}
