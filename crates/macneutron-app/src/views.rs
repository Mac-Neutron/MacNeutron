//! What the menu and the four windows show, as plain data: the SwiftUI views' (`MenuContent.swift`, `SetupView.swift`,
//! `SettingsView.swift`, `GamesView.swift`) texts, which controls there are, and which are enabled, for a state. The
//! AppKit layer (`ui`) draws this and redraws when it changes; nothing here needs a window, so it is unit-tested.

use std::collections::BTreeSet;

use macneutron_core::{GameSettings, GraphicsBackend, LaunchOptions, OrphanPrefix, RunAs, SteamPlayStatus};
use macneutron_core::game_settings::UPSCALE_PRESETS;
use objc2_foundation::{NSByteCountFormatter, NSByteCountFormatterCountStyle, NSString};

use crate::app_model::{AppModel, GameRow, LoginItemStatus, State, StatusDisplay};
use crate::update_model::{UpdateModel, UpdateState};

/// Everything the views read, gathered once per redraw on the main thread (the model's computed properties read disk).
#[derive(Debug, Clone)]
pub struct Inputs {
    pub state: State,
    pub status: SteamPlayStatus,
    pub updates: UpdateState,
    pub updates_enabled: bool,
    pub steam_installed: bool,
    /// `model.mode.isWanted`.
    pub steam_play_wanted: bool,
    pub has_vm_manifest: bool,
    pub vm_status: Option<String>,
    pub vm_ready: bool,
    pub vm_rebuild_off: bool,
    pub vm_folder_missing: Option<String>,
}

impl Inputs {
    pub fn read(model: &AppModel, updates: &UpdateModel) -> Inputs {
        let state = model.state();
        Inputs {
            status: state.status(),
            state,
            updates: updates.state(),
            updates_enabled: updates.enabled(),
            steam_installed: model.steam_installed(),
            steam_play_wanted: model.mode.is_wanted(),
            has_vm_manifest: model.vm_manifest.is_some(),
            vm_status: model.vm_status(),
            vm_ready: model.vm_ready(),
            vm_rebuild_off: model.vm_rebuild_off(),
            vm_folder_missing: model.vm_folder_missing(),
        }
    }

    /// `model.setupComplete`.
    pub fn setup_complete(&self) -> bool {
        self.state.runtime.is_some() && self.steam_play_wanted
    }
}

/// What a control does. The AppKit layer performs it, reading the control's value (a toggle's state, a picker's
/// choice, a field's text) where it has one.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Act {
    DismissError,
    RestartSteam,
    ShowSetup,
    ShowGames,
    OpenSteam,
    ShowCleanup,
    ShowLogs,
    ShowSettings,
    Quit,
    SetLaunchAtLogin,
    OpenLoginItems,
    SetAutomaticUpdates,
    CheckForUpdates,
    OpenURL(String),
    InstallUpdate,
    RepairRuntime,
    DownloadValve,
    RemoveValve,
    RetryVM,
    /// Asks first ("Rebuild the game VM?").
    RebuildVM,
    /// Asks first ("Turn off Steam Play mode?").
    TurnOffSteamPlay,
    TurnOnSteamPlay,
    /// A cleanup row's toggle, by app ID.
    SelectOrphan(String),
    /// Asks first ("Delete N games' data?").
    DeleteSelected,
    Search,
    /// A "Runs as" pick: asks first ("Switch versions?").
    RunsAs(u32),
    Graphics(u32),
    GameToggle(u32, GameToggle),
    GamePicker(u32, GamePicker),
    LaunchOptions(u32),
}

/// `ByteCountFormatter.string(fromByteCount:countStyle: .file)`.
pub fn file_size(bytes: i64) -> String {
    NSByteCountFormatter::stringFromByteCount_countStyle(bytes, NSByteCountFormatterCountStyle::File).to_string()
}

// MARK: The menu (MenuContent.swift)

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MenuEntry {
    /// A line of text (a disabled item, as MenuBarExtra shows `Text`).
    Text(String),
    Button(String, Act),
    Divider,
}

pub fn menu(inputs: &Inputs) -> Vec<MenuEntry> {
    use MenuEntry::*;
    let state = &inputs.state;
    let mut entries = vec![
        Text(inputs.status.menu_title()),
        Text(state.runtime.as_ref().map_or("Runtime not installed".into(), |runtime| format!("Runtime {}", runtime.label()))),
    ];
    if let Some(busy) = &state.busy {
        entries.push(Text(busy.clone()));
    }
    if let Some(error) = &state.error_message {
        entries.push(Text(error.clone()));
        entries.push(Button("Dismiss error".into(), Act::DismissError));
    }
    match inputs.status {
        SteamPlayStatus::Lost => entries.push(Button("Restore Steam Play mode".into(), Act::RestartSteam)),
        SteamPlayStatus::RestartNeeded(_) => entries.push(Button("Restart Steam".into(), Act::RestartSteam)),
        _ => {}
    }
    entries.push(Divider);
    if !inputs.setup_complete() {
        entries.push(Button("Finish setup…".into(), Act::ShowSetup));
    }
    entries.push(Button("Games…".into(), Act::ShowGames));
    entries.push(Button("Open Steam".into(), Act::OpenSteam));
    if !state.orphans.is_empty() {
        let bytes = state.orphans.iter().map(|orphan| orphan.bytes).sum();
        entries.push(Button(format!("Free up space ({})", file_size(bytes)), Act::ShowCleanup));
    }
    entries.push(Button("Show logs".into(), Act::ShowLogs));
    entries.push(Divider);
    if let Some(release) = &inputs.updates.available {
        entries.push(Button(format!("Update to {}…", release.version), Act::ShowSettings));
    }
    entries.push(Button("Settings…".into(), Act::ShowSettings));
    entries.push(Button("Quit MacNeutron".into(), Act::Quit));
    entries
}

// MARK: Settings (SettingsView.swift)

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Tone {
    Secondary,
    Orange,
    Red,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Control {
    Button { title: String, act: Act, enabled: bool, destructive: bool },
    /// A toggle inside a row: a checkbox with its title.
    Check { title: String, on: bool, act: Act, enabled: bool },
    Text(String, Tone),
    Link { title: String, url: String, enabled: bool },
}

/// One row of the grouped form.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Row {
    /// A form `Toggle`: its title, and a switch at the trailing edge.
    Switch { title: String, on: bool, act: Act, enabled: bool },
    /// `LabeledContent`: a label, and its controls at the trailing edge.
    Labeled { label: String, tone: Option<Tone>, controls: Vec<Control> },
    /// A selectable line of text.
    Note(String, Tone),
    /// `ProgressView(busy)`.
    Progress(String),
}

fn button(title: &str, act: Act, enabled: bool) -> Control {
    Control::Button { title: title.into(), act, enabled, destructive: false }
}

/// The settings form. Every control is off while `busy` (`.disabled(model.busy != nil)` on the form).
pub fn settings(inputs: &Inputs) -> Vec<Row> {
    let state = &inputs.state;
    let free = state.busy.is_none();
    let updates = &inputs.updates;
    let labeled = |label: &str, controls| Row::Labeled { label: label.into(), tone: None, controls };
    let mut rows = vec![Row::Switch {
        title: "Open MacNeutron at login".into(),
        on: state.launches_at_login(),
        act: Act::SetLaunchAtLogin,
        enabled: free,
    }];
    if state.login_item_status == LoginItemStatus::RequiresApproval {
        rows.push(Row::Labeled {
            label: "Allow MacNeutron under Login Items in System Settings".into(),
            tone: Some(Tone::Orange),
            controls: vec![button("Open Login Items", Act::OpenLoginItems, free)],
        });
    }
    if inputs.updates_enabled {
        rows.push(labeled(
            "Updates",
            vec![
                Control::Check {
                    title: "Check automatically".into(),
                    on: updates.automatic,
                    act: Act::SetAutomaticUpdates,
                    enabled: free,
                },
                button("Check now", Act::CheckForUpdates, free && !updates.working),
            ],
        ));
        if let Some(release) = &updates.available {
            rows.push(labeled(
                &format!("MacNeutron {} is available", release.version),
                vec![
                    Control::Link { title: "Release notes".into(), url: release.page.clone(), enabled: free },
                    button("Install and relaunch", Act::InstallUpdate, free && !updates.working),
                ],
            ));
        }
        if let Some(notice) = &updates.notice {
            rows.push(Row::Note(notice.clone(), Tone::Secondary));
        }
    }
    rows.push(labeled("Runtime", vec![button("Repair runtime", Act::RepairRuntime, free && !state.installing)]));
    if let Some(notice) = &state.runtime_notice {
        rows.push(Row::Note(notice.clone(), Tone::Secondary));
    }
    rows.push(labeled(
        "Steam components from Valve",
        vec![
            Control::Text(
                state.valve_version.as_ref().map_or("Not downloaded".into(), |version| format!("Version {version}")),
                Tone::Secondary,
            ),
            button("Download again", Act::DownloadValve, free && !state.valve_updating),
            button("Remove", Act::RemoveValve, free && state.valve_version.is_some() && !state.valve_updating),
        ],
    ));
    if let Some(notice) = &state.valve_notice {
        rows.push(Row::Note(notice.clone(), Tone::Secondary));
    }
    if inputs.has_vm_manifest {
        let mut controls = Vec::new();
        if state.vm_setup_failed {
            controls.push(button("Retry", Act::RetryVM, free));
        }
        controls.push(Control::Button {
            title: "Rebuild the game VM".into(),
            act: Act::RebuildVM,
            enabled: free && !(state.vm_setup_running || inputs.vm_rebuild_off),
            destructive: true,
        });
        rows.push(labeled("Game VM", controls));
        if let Some(notice) = &inputs.vm_status {
            rows.push(Row::Note(notice.clone(), Tone::Secondary));
        }
    }
    rows.push(labeled("Setup", vec![button("Run setup again", Act::ShowSetup, free)]));
    rows.push(labeled(
        "Steam Play mode",
        vec![Control::Button {
            title: "Turn off Steam Play mode".into(),
            act: Act::TurnOffSteamPlay,
            enabled: free && inputs.steam_play_wanted,
            destructive: true,
        }],
    ));
    if let Some(busy) = &state.busy {
        rows.push(Row::Progress(busy.clone()));
    }
    if let Some(error) = &state.error_message {
        rows.push(Row::Note(error.clone(), Tone::Red));
    }
    rows
}

/// A confirmation dialog: its title, message and confirming (destructive) button.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Confirmation {
    pub title: String,
    pub message: String,
    pub button: String,
}

pub fn rebuild_confirmation(inputs: &Inputs) -> Confirmation {
    Confirmation {
        title: "Rebuild the game VM?".into(),
        message: if inputs.vm_folder_missing.is_none() {
            "It downloads about 1.35 GB again. Games' saves and settings are kept."
        } else {
            "It downloads about 1.35 GB and starts a new VM here. The games' disks in the missing folder won't be used."
        }
        .into(),
        button: "Rebuild".into(),
    }
}

pub fn turn_off_confirmation() -> Confirmation {
    Confirmation {
        title: "Turn off Steam Play mode?".into(),
        message: "Windows games will be removed from disk and need downloading again if you turn this back on. Saves inside their prefixes are kept.".into(),
        button: "Turn off and restart Steam".into(),
    }
}

// MARK: Free up space (CleanupView, SettingsView.swift)

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CleanupRow {
    pub app_id: String,
    pub name: String,
    pub size: String,
    pub selected: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Cleanup {
    pub rows: Vec<CleanupRow>,
    pub delete_enabled: bool,
}

/// A leftover prefix's game name, or "App <id>" for a game no longer in the list.
pub fn orphan_name(orphan: &OrphanPrefix, games: &[GameRow]) -> String {
    games
        .iter()
        .find(|row| row.id().to_string() == orphan.app_id)
        .map_or_else(|| format!("App {}", orphan.app_id), GameRow::name)
}

pub fn cleanup(state: &State, selected: &BTreeSet<String>) -> Cleanup {
    Cleanup {
        rows: state
            .orphans
            .iter()
            .map(|orphan| CleanupRow {
                app_id: orphan.app_id.clone(),
                name: orphan_name(orphan, &state.games),
                size: file_size(orphan.bytes),
                selected: selected.contains(&orphan.app_id),
            })
            .collect(),
        delete_enabled: !selected.is_empty(),
    }
}

/// The selected prefixes still listed (`chosen`).
pub fn chosen(state: &State, selected: &BTreeSet<String>) -> Vec<OrphanPrefix> {
    state.orphans.iter().filter(|orphan| selected.contains(&orphan.app_id)).cloned().collect()
}

pub fn delete_confirmation(state: &State, selected: &BTreeSet<String>) -> Confirmation {
    let chosen = chosen(state, selected);
    let names: Vec<String> = chosen.iter().map(|orphan| orphan_name(orphan, &state.games)).collect();
    Confirmation {
        title: format!("Delete {} {} data?", chosen.len(), if chosen.len() == 1 { "game's" } else { "games'" }),
        message: names.join(", ") + "\n\nThis can't be undone.",
        button: "Delete".into(),
    }
}

// MARK: Setup (SetupView.swift)

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StepAction {
    None,
    Spinner,
    /// The prominent "Turn on and restart Steam".
    Button { title: String, act: Act, enabled: bool },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Step {
    pub done: bool,
    pub title: String,
    pub detail: String,
    pub action: StepAction,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Setup {
    pub steps: Vec<Step>,
    /// While Steam Play is off: the headings ("Stay native (n)", "Run with MacNeutron (n)") and their summaries.
    pub games: Option<[String; 4]>,
    pub busy: Option<String>,
    pub error: Option<String>,
    /// False while busy: the whole view is disabled.
    pub enabled: bool,
}

/// Up to four installed games' names, else up to three of any, with "…" when there are more; "None" for none.
pub fn summary(rows: &[&GameRow]) -> String {
    let installed: Vec<String> = rows.iter().filter(|row| row.installed).map(|row| row.name()).collect();
    let names: Vec<String> = if installed.is_empty() {
        rows.iter().take(3).map(|row| row.name()).collect()
    } else {
        installed.into_iter().take(4).collect()
    };
    if names.is_empty() {
        return "None".into();
    }
    names.join(", ") + if rows.len() > names.len() { "…" } else { "" }
}

pub fn setup(inputs: &Inputs) -> Setup {
    let state = &inputs.state;
    let mut steps = vec![
        Step {
            done: inputs.steam_installed,
            title: "Steam".into(),
            detail: if inputs.steam_installed { "Steam for Mac is installed." } else { "Install Steam for Mac first." }
                .into(),
            action: StepAction::None,
        },
        Step {
            done: state.runtime.is_some(),
            title: "Runtime".into(),
            detail: state.runtime_notice.clone().unwrap_or_else(|| {
                state.runtime.as_ref().map_or("Not installed.".into(), |runtime| format!("Runtime {} installed", runtime.label()))
            }),
            action: if state.installing { StepAction::Spinner } else { StepAction::None },
        },
    ];
    if inputs.has_vm_manifest {
        steps.push(Step {
            done: inputs.vm_ready,
            title: "Game VM".into(),
            detail: inputs.vm_status.clone().unwrap_or_else(|| {
                if inputs.vm_ready { "Ready." } else { "Sets itself up after the runtime." }.into()
            }),
            action: if state.vm_setup_running { StepAction::Spinner } else { StepAction::None },
        });
    }
    steps.push(Step {
        done: inputs.steam_play_wanted,
        title: "Steam Play".into(),
        detail: "Steam restarts. Your Mac games stay native and protected.".into(),
        action: StepAction::Button {
            title: "Turn on and restart Steam".into(),
            act: Act::TurnOnSteamPlay,
            enabled: state.runtime.is_some() && inputs.steam_installed,
        },
    });
    let games = (!inputs.steam_play_wanted).then(|| {
        let native: Vec<&GameRow> = state.games.iter().filter(|row| !row.runs_with_macneutron()).collect();
        let windows: Vec<&GameRow> = state.games.iter().filter(|row| row.runs_with_macneutron()).collect();
        [
            format!("Stay native ({})", native.len()),
            format!("Run with MacNeutron ({})", windows.len()),
            summary(&native),
            summary(&windows),
        ]
    });
    Setup { steps, games, busy: state.busy.clone(), error: state.error_message.clone(), enabled: state.busy.is_none() }
}

// MARK: Games (GamesView.swift)

/// The rows the search shows (`localizedCaseInsensitiveContains`).
pub fn filter(games: &[GameRow], search: &str) -> Vec<GameRow> {
    if search.is_empty() {
        return games.to_vec();
    }
    let needle = NSString::from_str(search);
    games.iter().filter(|row| NSString::from_str(&row.name()).localizedCaseInsensitiveContainsString(&needle)).cloned().collect()
}

/// A table cell: a picker (its options as (tag, title), and the selected tag) or secondary text.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Cell {
    Picker { options: &'static [(&'static str, &'static str)], selected: String },
    Text(String),
}

pub const RUNS_AS: [(&str, &str); 2] = [("mac", "Mac version"), ("windows", "Windows version")];
pub const GRAPHICS: [(&str, &str); 3] = [("", "Default (DXMT)"), ("dxmt", "DXMT"), ("wined3d", "wined3d (OpenGL)")];

pub fn runs_as_cell(row: &GameRow) -> Cell {
    if row.is_dual_platform() {
        let selected = if row.settings.run_as == Some(RunAs::Windows) { "windows" } else { "mac" };
        Cell::Picker { options: &RUNS_AS, selected: selected.into() }
    } else {
        Cell::Text(if row.app.oslist.contains("macos") { "Mac version" } else { "Windows" }.into())
    }
}

/// A value from before 0.1 (d3dmetal, dxvk) runs DXMT, so it shows as the default.
pub fn graphics_cell(row: &GameRow) -> Cell {
    if row.runs_with_macneutron() {
        let selected = row.settings.graphics.as_deref().and_then(GraphicsBackend::from_str).map_or("", GraphicsBackend::raw_value);
        Cell::Picker { options: &GRAPHICS, selected: selected.into() }
    } else {
        Cell::Text("Native".into())
    }
}

/// The "Runs as" setting a confirmed switch writes: Mac is the default, so it's stored as none.
pub fn run_as_setting(tag: &str) -> Option<RunAs> {
    (tag == "windows").then_some(RunAs::Windows)
}

pub fn switch_confirmation() -> Confirmation {
    Confirmation {
        title: "Switch versions?".into(),
        message: "Steam Cloud keeps this game's saves in a different place for each version. Back up your saves before switching.".into(),
        button: "Switch".into(),
    }
}

/// The selected game's toggles.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GameToggle {
    Log,
    MetalHUD,
    Msync,
    MetalFX,
    DLSS,
    FrameGen,
}

impl GameToggle {
    pub const ALL: [GameToggle; 6] =
        [GameToggle::Log, GameToggle::MetalHUD, GameToggle::Msync, GameToggle::MetalFX, GameToggle::DLSS, GameToggle::FrameGen];

    pub fn title(self) -> &'static str {
        match self {
            GameToggle::Log => "Log",
            GameToggle::MetalHUD => "Metal HUD",
            GameToggle::Msync => "msync",
            GameToggle::MetalFX => "MetalFX upscaling",
            GameToggle::DLSS => "DLSS",
            GameToggle::FrameGen => "Frame generation",
        }
    }

    pub fn help(self) -> Option<&'static str> {
        Some(match self {
            GameToggle::Log | GameToggle::Msync => return None,
            GameToggle::MetalHUD => "Apple's Metal performance HUD over the game (FPS, frame times), with DXMT's own pass, queue and MetalFX lines on it: the same as MTL_HUD_ENABLED=1",
            GameToggle::MetalFX => "Upscales with Apple's MetalFX when the game renders below its window or the display's pixel density.",
            GameToggle::DLSS => "Experimental: the game sees an NVIDIA GPU, and its DLSS option upscales with MetalFX's temporal upscaler (MACNEUTRON_DLSS=1). Direct3D 12 games on DXMT.",
            GameToggle::FrameGen => "Experimental: MetalFX interpolates a frame between each two the game renders, while the game runs DLSS and is slow enough for it to help (MACNEUTRON_FRAMEGEN=1; it turns itself off when it would hold the game back). More frames shown, a little more latency; the HUD may wobble in motion.",
        })
    }

    fn field(self, settings: &mut GameSettings) -> &mut Option<bool> {
        match self {
            GameToggle::Log => &mut settings.log,
            GameToggle::MetalHUD => &mut settings.metal_hud,
            GameToggle::Msync => &mut settings.msync,
            GameToggle::MetalFX => &mut settings.metal_fx,
            GameToggle::DLSS => &mut settings.dlss,
            GameToggle::FrameGen => &mut settings.frame_gen,
        }
    }

    /// What it means unset.
    pub fn default_value(self) -> bool {
        matches!(self, GameToggle::Msync | GameToggle::MetalFX)
    }

    pub fn is_on(self, settings: &GameSettings) -> bool {
        self.field(&mut settings.clone()).unwrap_or(self.default_value())
    }

    /// Frame generation needs DLSS on.
    pub fn is_enabled(self, settings: &GameSettings) -> bool {
        self != GameToggle::FrameGen || settings.dlss == Some(true)
    }

    /// Stores `on`, or none when it's the default.
    pub fn set(self, settings: &mut GameSettings, on: bool) {
        *self.field(settings) = (on != self.default_value()).then_some(on);
    }
}

/// The selected game's pickers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GamePicker {
    Upscale,
    PostAA,
    Anisotropy,
}

impl GamePicker {
    pub const ALL: [GamePicker; 3] = [GamePicker::Upscale, GamePicker::PostAA, GamePicker::Anisotropy];

    pub fn title(self) -> &'static str {
        match self {
            GamePicker::Upscale => "Upscaling",
            GamePicker::PostAA => "Anti-aliasing (post)",
            GamePicker::Anisotropy => "Anisotropic filtering",
        }
    }

    pub fn options(self) -> &'static [(&'static str, &'static str)] {
        match self {
            GamePicker::Upscale => &[
                ("off", "Off"),
                ("ultra-quality", "Ultra Quality (1.3x)"),
                ("quality", "Quality (1.5x)"),
                ("balanced", "Balanced (1.7x)"),
                ("performance", "Performance (2.0x)"),
                ("ultra-performance", "Ultra Performance (3.0x)"),
            ],
            GamePicker::PostAA => &[("off", "Off"), ("cmaa2", "CMAA2")],
            GamePicker::Anisotropy => &[("game", "Game's choice"), ("4", "4x"), ("8", "8x"), ("16", "16x (default)")],
        }
    }

    pub fn help(self) -> &'static str {
        match self {
            GamePicker::Upscale => "reports a smaller desktop to the game and upscales the window with MetalFX: much faster, a little softer. Needs \"MetalFX upscaling\" on, and the game's own upscaler off",
            GamePicker::PostAA => "for games without their own anti-aliasing",
            GamePicker::Anisotropy => "Sharper textures on surfaces seen at an angle",
        }
    }

    /// The default tag, stored as none.
    fn default_tag(self) -> &'static str {
        match self {
            GamePicker::Upscale | GamePicker::PostAA => "off",
            GamePicker::Anisotropy => "16",
        }
    }

    /// The tag shown: unknown values run at the default, so they show as it.
    pub fn selection(self, settings: &GameSettings) -> String {
        let value = match self {
            GamePicker::Upscale => settings.upscale.as_deref().filter(|value| UPSCALE_PRESETS.contains(value)),
            GamePicker::PostAA => settings.post_aa.as_deref().filter(|value| *value == "cmaa2"),
            GamePicker::Anisotropy => settings.anisotropy.as_deref().filter(|value| ["game", "4", "8"].contains(value)),
        };
        value.unwrap_or(self.default_tag()).into()
    }

    /// The render scale needs the MetalFX upscaler.
    pub fn is_enabled(self, settings: &GameSettings) -> bool {
        self != GamePicker::Upscale || settings.metal_fx != Some(false)
    }

    pub fn set(self, settings: &mut GameSettings, tag: &str) {
        let value = (tag != self.default_tag()).then(|| tag.to_string());
        match self {
            GamePicker::Upscale => settings.upscale = value,
            GamePicker::PostAA => settings.post_aa = value,
            GamePicker::Anisotropy => settings.anisotropy = value,
        }
    }
}

pub const LAUNCH_OPTIONS_PROMPT: &str = "DXMT_D3D12_SM6=0 -dx12";
pub const LAUNCH_OPTIONS_HELP: &str = "Variables and game arguments, e.g. DXMT_D3D12_SM6=0 -dx12. In the game VM only the variables MacNeutron passes to the VM reach the game (its logging, DLSS and a few DXMT ones); DXMT's reach its renderer on the Mac.";

/// The launch options a field's draft saves (trimmed of spaces and tabs; empty is none), or None when it's unchanged.
pub fn launch_options_change(settings: &GameSettings, draft: &str) -> Option<Option<String>> {
    let value = draft.trim_matches([' ', '\t']);
    (value != settings.launch_options.as_deref().unwrap_or("")).then(|| (!value.is_empty()).then(|| value.to_string()))
}

/// Under the field: the game's own Steam launch line, and what's wrong with it (orange).
pub fn steam_launch_lines(row: &GameRow) -> Vec<(String, Tone)> {
    let Some(line) = &row.steam_launch_options else { return Vec::new() };
    let mut lines = vec![(format!("Steam's launch options: {line}"), Tone::Secondary)];
    lines.extend(
        LaunchOptions::problems(line, &row.settings.environment()).into_iter().map(|problem| (problem, Tone::Orange)),
    );
    lines
}

/// The selected game's options pane: only for a game MacNeutron runs.
pub fn detail(rows: &[GameRow], selection: Option<u32>) -> Option<GameRow> {
    rows.iter().find(|row| Some(row.id()) == selection && row.runs_with_macneutron()).cloned()
}

/// The bar under the table while Steam needs a restart: its orange text.
pub fn restart_bar(status: &SteamPlayStatus) -> Option<String> {
    matches!(status, SteamPlayStatus::RestartNeeded(_)).then(|| status.menu_title())
}

#[cfg(test)]
mod tests;
