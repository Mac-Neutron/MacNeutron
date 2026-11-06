//! The Rust counterpart of `Tests/MacNeutronCoreTests/Support.swift`: the fixtures every ported test uses.
#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;

use crate::launcher_log::LauncherLog;
use crate::process_runner::{ExitStatus, ProcessRunner};
use crate::steam_location::{SteamControlling, SteamLocation, SteamPlayError};
use crate::tool_layout::ToolLayout;

/// A fresh temp directory whose path contains a space, like Steam's "Application Support".
pub fn make_temp_dir() -> PathBuf {
    crate::rpm_fixture::temp("test")
}

/// Writes `text` to `path`, creating parents, then makes it executable.
pub fn write(text: &str, to: &Path, executable: bool) {
    if let Some(parent) = to.parent() {
        std::fs::create_dir_all(parent).unwrap();
    }
    std::fs::write(to, text).unwrap();
    if executable {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = std::fs::metadata(to).unwrap().permissions();
        permissions.set_mode(0o755);
        std::fs::set_permissions(to, permissions).unwrap();
    }
}

/// Records every process the code under test would start, and answers with `respond`.
pub struct FakeRunner {
    calls: Mutex<Vec<Call>>,
    respond: Box<dyn Fn(&Call) -> ExitStatus + Send + Sync>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Call {
    pub tool: String,
    pub arguments: Vec<String>,
    pub environment: BTreeMap<String, String>,
    pub output: Option<PathBuf>,
}

impl FakeRunner {
    pub fn new(respond: impl Fn(&Call) -> ExitStatus + Send + Sync + 'static) -> Self {
        FakeRunner { calls: Mutex::new(Vec::new()), respond: Box::new(respond) }
    }

    pub fn always(status: ExitStatus) -> Self {
        Self::new(move |_| status)
    }

    pub fn calls(&self) -> Vec<Call> {
        self.calls.lock().unwrap().clone()
    }

    pub fn tools(&self) -> Vec<String> {
        self.calls().into_iter().map(|c| c.tool).collect()
    }
}

impl ProcessRunner for FakeRunner {
    fn run(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
    ) -> std::io::Result<ExitStatus> {
        self.run_with_started(executable, arguments, environment, output, &mut |_| {})
    }

    fn run_with_started(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
        started: &mut dyn FnMut(i32),
    ) -> std::io::Result<ExitStatus> {
        let call = Call {
            tool: executable.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(),
            arguments: arguments.to_vec(),
            environment: environment.clone(),
            output: output.map(|p| p.to_path_buf()),
        };
        // Swift's fake records the call first and never reports a child: a `started` pid would be signalled
        // (Launcher's VMRun kills a stopped launch's process group).
        let _ = started;
        self.calls.lock().unwrap().push(call.clone());
        Ok((self.respond)(&call))
    }
}

/// Records what the code under test tells the player: notifications, toasts, their updates and closes.
#[derive(Default)]
pub struct RecordingNotifier {
    state: std::sync::Arc<Mutex<Recorded>>,
}

#[derive(Default)]
struct Recorded {
    messages: Vec<String>,
    shown: Vec<String>,
    hidden: usize,
    lines: Vec<String>,
}

impl RecordingNotifier {
    pub fn posted(&self) -> Vec<String> {
        self.state.lock().unwrap().messages.clone()
    }

    /// Each toast's title, and the file it waits for.
    pub fn toasts(&self) -> Vec<String> {
        self.state.lock().unwrap().shown.clone()
    }

    pub fn toasts_closed(&self) -> usize {
        self.state.lock().unwrap().hidden
    }

    /// Every toast's updated messages, in order.
    pub fn toast_updates(&self) -> Vec<String> {
        self.state.lock().unwrap().lines.clone()
    }
}

impl crate::preflight::Notifier for RecordingNotifier {
    fn post(&self, _title: &str, message: &str) {
        self.state.lock().unwrap().messages.push(message.to_string());
    }

    fn progress(&self, title: &str, _message: &str, until: Option<PathBuf>) -> crate::preflight::ToastHandle {
        let until = until.map(|u| format!(" until {}", u.to_string_lossy())).unwrap_or_default();
        self.state.lock().unwrap().shown.push(format!("{title}{until}"));
        let (update, close) = (self.state.clone(), self.state.clone());
        crate::preflight::ToastHandle {
            update: Box::new(move |line| update.lock().unwrap().lines.push(line.to_string())),
            close: Box::new(move || close.lock().unwrap().hidden += 1),
        }
    }
}

/// macOS Steam's registry.vdf with `language` under HKCU/Software/Valve/Steam, and a sibling block's own `language`.
pub fn registry_vdf(language: &str) -> String {
    format!(
        "\"Registry\"\n{{\n\t\"HKCU\"\n\t{{\n\t\t\"Software\"\n\t\t{{\n\t\t\t\"Valve\"\n\t\t\t{{\n\t\t\t\t\"Steam\"\n\t\t\t\t{{\n\t\t\t\t\t\"language\"\t\t\"{language}\"\n\t\t\t\t}}\n\t\t\t\t\"Steamsteamglobal\"\n\t\t\t\t{{\n\t\t\t\t\t\"language\"\t\t\"german\"\n\t\t\t\t}}\n\t\t\t}}\n\t\t}}\n\t}}\n}}\n"
    )
}

/// A FakeRunner whose `wineboot` creates the prefix like the real one does.
pub fn wineboot_creating_prefix(status: ExitStatus, delay: f64) -> FakeRunner {
    FakeRunner::new(move |call| {
        if call.arguments.first().map(String::as_str) == Some("wineboot") {
            if let Some(prefix) = call.environment.get("WINEPREFIX") {
                if delay > 0.0 {
                    std::thread::sleep(std::time::Duration::from_secs_f64(delay));
                }
                if status == 0 {
                    let _ = std::fs::create_dir_all(prefix);
                }
            }
            return status;
        }
        0
    })
}

/// A tool folder with a fake, unsigned `wine.app`: executable wine/wineserver, DXMT's DLLs, translator key and replayer,
/// both halves of lsteamclient, and `CFBundleShortVersionString` `test`. Tests that launch inject the identity.
pub fn make_tool_layout() -> ToolLayout {
    // The installed shape, MacNeutron/compatibilitytools.d/macneutron: `valve_cache` is then this test's own folder.
    let layout = ToolLayout::new(make_temp_dir().join("compatibilitytools.d/macneutron"));
    write("#!/bin/sh\n", &layout.wine(), true);
    write("#!/bin/sh\n", &layout.wineserver(), true);
    write_info_plist(&layout.wine_app(), "test");
    for dll in ToolLayout::DXMT_DLLS {
        write(&format!("dxmt {dll}"), &layout.dxmt().join(dll), false);
    }
    write("dxmt replay", &layout.dxmt_replay(), false);
    write("dxmt-test\n", &layout.dxmt_translator_file(), false);
    write("lsteamclient aarch64", &layout.lsteamclient(), false);
    write("lsteamclient.so", &layout.lsteamclient_unix(), false);
    layout
}

/// The runtime's version as wine.app's Info.plist gives it: "0.7.1p" makes a performance build (X.Y.Zp).
pub fn set_runtime_version(version: &str, layout: &ToolLayout) {
    write_info_plist(&layout.wine_app(), version);
}

/// `CFBundleExecutable` wine, `CFBundleShortVersionString` `version`.
fn write_info_plist(bundle: &Path, version: &str) {
    let mut plist = plist::Dictionary::new();
    plist.insert("CFBundleExecutable".into(), plist::Value::from("wine"));
    plist.insert("CFBundleShortVersionString".into(), plist::Value::from(version));
    let path = bundle.join("Contents/Info.plist");
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    plist::Value::Dictionary(plist).to_file_xml(&path).unwrap();
}

/// What launcher, prefix and precache tests use: no codesign, a fixed identity.
pub const TEST_IDENTITY: &str = "0123456789abcdef0123456789abcdef01234567";

pub fn steam_environment(data_path: &Path, app_id: &str) -> BTreeMap<String, String> {
    let mut environment = BTreeMap::new();
    environment.insert("STEAM_COMPAT_DATA_PATH".into(), data_path.to_string_lossy().into_owned());
    environment.insert("SteamAppId".into(), app_id.to_string());
    environment.insert("PATH".into(), "/usr/bin:/bin".into());
    environment
}

/// `steam.exe` in the tool folder: with the fake wine.app's lsteamclient, the bridge counts as installed.
pub fn install_fake_steam_bridge(layout: &ToolLayout) {
    write("steam.exe", &layout.steam_helper(), false);
}

/// A minimal PE: 64-byte DOS header with `e_lfanew` 0x80, padding, `PE\0\0`, the COFF header, then an optional header
/// (PE32 for i386, PE32+ otherwise) with 16 data directories.
pub fn pe_bytes(machine: u16, certificate: bool) -> Vec<u8> {
    let pe32 = machine == crate::pe_image::I386;
    let optional = 0x98;
    let optional_size = if pe32 { 0xE0 } else { 0xF0 };
    let mut bytes = vec![0u8; optional + optional_size];
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

/// Writes the little-endian `size`-byte `value` at `at`.
fn put(bytes: &mut [u8], value: usize, at: usize, size: usize) {
    for i in 0..size {
        bytes[at + i] = ((value >> (8 * i)) & 0xFF) as u8;
    }
}

/// An ad-hoc-signed `dir/wine.app` whose `Contents/MacOS/wine` is a copy of `loader` (never run). Sign it last:
/// anything written into the bundle afterwards breaks `codesign --verify`.
pub fn make_signed_wine_app(at: &Path, loader: &Path, short_version: &str, bundle_version: &str) -> PathBuf {
    let bundle = at.join("wine.app");
    let macos = bundle.join("Contents/MacOS");
    std::fs::create_dir_all(&macos).unwrap();
    let mut plist = plist::Dictionary::new();
    plist.insert("CFBundleExecutable".into(), plist::Value::from("wine"));
    plist.insert("CFBundleIdentifier".into(), plist::Value::from("test.wine"));
    plist.insert("CFBundleShortVersionString".into(), plist::Value::from(short_version));
    plist.insert("CFBundleVersion".into(), plist::Value::from(bundle_version));
    plist::Value::Dictionary(plist).to_file_xml(bundle.join("Contents/Info.plist")).unwrap();
    std::fs::copy(loader, macos.join("wine")).unwrap();
    let status = Command::new("/usr/bin/codesign")
        .args(["-s", "-", "-f"])
        .arg(&bundle)
        .status()
        .unwrap();
    assert!(status.success(), "codesign failed for {}", bundle.display());
    bundle
}

/// A `LauncherLog` in a fresh temp directory, for tests that assert on its contents.
pub fn make_log() -> LauncherLog {
    LauncherLog::new(make_temp_dir())
}

/// An amd64 image whose .rsrc (RVA 0x1000, file offset 0x400) holds `icons` (width byte, bit count, data) as RT_ICON
/// 1...n and one RT_GROUP_ICON listing them in order, each resource in language 0x409.
pub fn pe_bytes_icons(icons: &[(u8, u16, Vec<u8>)]) -> Vec<u8> {
    let (rva, raw, sub, n) = (0x1000usize, 0x400usize, 0x8000_0000usize, icons.len());
    let le = |value: usize, size: usize| -> Vec<u8> { (0..size).map(|i| ((value >> (8 * i)) & 0xFF) as u8).collect() };
    let dir = |entries: &[(usize, usize)]| -> Vec<u8> {
        let mut out = vec![0u8; 14];
        out.extend(le(entries.len(), 2));
        for (id, offset) in entries {
            out.extend(le(*id, 4));
            out.extend(le(*offset, 4));
        }
        out
    };
    let icon_names = 32;
    let icon_languages = icon_names + 16 + 8 * n;
    let group_names = icon_languages + 24 * n;
    let group_language = group_names + 24;
    let leaves = group_language + 24;
    let group = leaves + 16 * (n + 1);
    let mut rsrc = dir(&[(3, sub | icon_names), (14, sub | group_names)]);
    let names: Vec<(usize, usize)> = (0..n).map(|i| (i + 1, sub | (icon_languages + 24 * i))).collect();
    rsrc.extend(dir(&names));
    for i in 0..n {
        rsrc.extend(dir(&[(0x409, leaves + 16 * i)]));
    }
    rsrc.extend(dir(&[(1, sub | group_language)]));
    rsrc.extend(dir(&[(0x409, leaves + 16 * n)]));
    let mut image = group + 6 + 14 * n;
    for icon in icons {
        rsrc.extend(le(rva + image, 4));
        rsrc.extend(le(icon.2.len(), 4));
        rsrc.extend(le(0, 8));
        image += icon.2.len();
    }
    rsrc.extend(le(rva + group, 4));
    rsrc.extend(le(6 + 14 * n, 4));
    rsrc.extend(le(0, 8));
    rsrc.extend(le(0, 2));
    rsrc.extend(le(1, 2));
    rsrc.extend(le(n, 2));
    for (i, icon) in icons.iter().enumerate() {
        rsrc.extend([icon.0, icon.0, 0, 0]);
        rsrc.extend(le(1, 2));
        rsrc.extend(le(icon.1 as usize, 2));
        rsrc.extend(le(icon.2.len(), 4));
        rsrc.extend(le(i + 1, 2));
    }
    for icon in icons {
        rsrc.extend(&icon.2);
    }

    let mut bytes = pe_bytes(crate::pe_image::AMD64, false);
    let optional = 0x98;
    bytes[0x86] = 1; // NumberOfSections
    let mut dir2 = le(rva, 4);
    dir2.extend(le(rsrc.len(), 4));
    bytes.splice((optional + 128)..(optional + 136), dir2); // directory 2
    bytes.extend(b".rsrc\0\0\0");
    bytes.extend(le(rsrc.len(), 4));
    bytes.extend(le(rva, 4));
    bytes.extend(le(rsrc.len(), 4));
    bytes.extend(le(raw, 4));
    bytes.resize(raw, 0);
    bytes.extend(rsrc);
    bytes
}

/// Port of `makeAppInfoV29` (AppInfoFixture.swift): a minimal appinfo.vdf v29 the way Steam lays it out.
pub fn make_app_info_v29(apps: &[crate::app_info::AppInfo], magic: u32) -> Vec<u8> {
    fn index(strings: &mut Vec<String>, key: &str) -> u32 {
        if let Some(found) = strings.iter().position(|s| s == key) {
            return found as u32;
        }
        strings.push(key.to_string());
        (strings.len() - 1) as u32
    }
    fn capitalized(s: &str) -> String {
        let mut c = s.chars();
        c.next().map(|f| f.to_uppercase().collect::<String>() + &c.as_str().to_lowercase()).unwrap_or_default()
    }
    let mut strings: Vec<String> = Vec::new();
    let mut body: Vec<u8> = Vec::new();
    for app in apps {
        let mut kv: Vec<u8> = Vec::new();
        kv.push(0x00);
        kv.extend(index(&mut strings, "appinfo").to_le_bytes());
        kv.push(0x02);
        kv.extend(index(&mut strings, "appid").to_le_bytes());
        kv.extend(app.app_id.to_le_bytes());
        kv.push(0x00);
        kv.extend(index(&mut strings, "common").to_le_bytes());
        let mut oslist: Vec<&String> = app.oslist.iter().collect();
        oslist.sort();
        let oslist = oslist.iter().map(|s| s.as_str()).collect::<Vec<_>>().join(",");
        for (key, value) in [("name", app.name.clone()), ("type", capitalized(&app.r#type)), ("oslist", oslist)] {
            kv.push(0x01);
            kv.extend(index(&mut strings, key).to_le_bytes());
            kv.extend(value.as_bytes());
            kv.push(0);
        }
        kv.push(0x07);
        kv.extend(index(&mut strings, "gameid").to_le_bytes());
        kv.extend((app.app_id as u64).to_le_bytes());
        kv.extend([0x08, 0x08, 0x08]);
        let header_len = 4 + 4 + 8 + 20 + 4 + 20;
        body.extend(app.app_id.to_le_bytes());
        body.extend(((header_len + kv.len()) as u32).to_le_bytes());
        body.extend(vec![0u8; header_len]);
        body.extend(kv);
    }
    body.extend(0u32.to_le_bytes());
    let table_offset = (4 + 4 + 8 + body.len()) as u64;
    let mut table = (strings.len() as u32).to_le_bytes().to_vec();
    for s in &strings {
        table.extend(s.as_bytes());
        table.push(0);
    }
    let mut out = magic.to_le_bytes().to_vec();
    out.extend(1u32.to_le_bytes());
    out.extend(table_offset.to_le_bytes());
    out.extend(body);
    out.extend(table);
    out
}

/// Port of `makeSteamLocation`: a Steam root in a temp folder; `steam_client` puts a steamclient.dylib in the
/// bundle's MacOS folder.
pub fn make_steam_location(steam_client: bool) -> crate::steam_location::SteamLocation {
    let steam = crate::steam_location::SteamLocation::new(make_temp_dir().join("Steam"));
    if steam_client {
        write("dylib", &steam.bundle_macos().join("steamclient.dylib"), false);
    }
    steam
}

/// `testPreflight`: no system check, no codesign, a fixed identity.
pub fn test_preflight() -> crate::preflight::Preflight {
    crate::preflight::Preflight::new(
        Box::new(|| true),
        Box::new(|_| Some(TEST_IDENTITY.to_string())),
        Box::new(|_| true),
    )
}

// From KeyValuesTests.swift and FakeSteam.swift.

/// Shaped like Steam's config.vdf: tabs, nested blocks, escaped JSON inside a value.
pub const STEAM_CONFIG_FIXTURE: &str = "\"InstallConfigStore\"\n{\n\t\"Software\"\n\t{\n\t\t\"Valve\"\n\t\t{\n\t\t\t\"Steam\"\n\t\t\t{\n\t\t\t\t\"AutoUpdateWindowEnabled\"\t\t\"0\"\n\t\t\t\t\"Recent\"\t\t\"{\\\"version\\\":2,\\\"data\\\":[]}\"\n\t\t\t\t\"CompatToolMapping\"\n\t\t\t\t{\n\t\t\t\t\t\"440\"\n\t\t\t\t\t{\n\t\t\t\t\t\t\"name\"\t\t\"proton_9\"\n\t\t\t\t\t\t\"config\"\t\t\"\"\n\t\t\t\t\t\t\"priority\"\t\t\"250\"\n\t\t\t\t\t}\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n";

pub const OK_SESSION: &str = "[2026-09-27 12:00:00] Client version: 1788652215
[2026-09-27 12:00:00] Registering tool macneutron, AppID 0
[2026-09-27 12:00:00] Registering tool macneutron-native, AppID 0
[2026-09-27 12:00:01] Recording non-user mapping for 3064690 at priority 100 to tool proton-10
";

pub const MAC_MODE_SESSION: &str = "[2026-09-27 12:00:00] Client version: 1788652215
[2026-09-27 12:00:00] Registering tool macneutron, AppID 0
[2026-09-27 12:00:00] Ignoring tool macneutron as it's for a different target platform linux.
";

/// A Steam install in a temp folder: app bundle, config.vdf, and MacNeutron's runtime tool with a fake CLI.
pub fn make_fake_steam(config: &str) -> (SteamLocation, PathBuf) {
    let base = make_temp_dir();
    let steam = SteamLocation::new(base.join("Steam"));
    std::fs::create_dir_all(steam.bundle_macos()).unwrap();
    write(config, &steam.config_vdf(), false);
    let root = base.join("MacNeutron");
    write("\"manifest\" {}", &root.join("compatibilitytools.d/macneutron/toolmanifest.vdf"), false);
    write("#!/bin/sh\necho fake macneutron\n", &root.join("compatibilitytools.d/macneutron/bin/macneutron"), true);
    (steam, root)
}

/// Stands in for the Steam client: launching appends `session` to compat_log.txt.
pub struct FakeSteam {
    running: Mutex<bool>,
    dev_config_at_launch: Mutex<Vec<bool>>,
    intent_at_launch: Mutex<Vec<bool>>,
    pub steam: SteamLocation,
    pub session: String,
    pub intent_file: Option<PathBuf>,
    pub replace_log_on_launch: bool,
}

impl FakeSteam {
    pub fn new(
        steam: SteamLocation,
        running: bool,
        session: &str,
        intent_file: Option<PathBuf>,
        replace_log_on_launch: bool,
    ) -> Self {
        FakeSteam {
            running: Mutex::new(running),
            dev_config_at_launch: Mutex::new(Vec::new()),
            intent_at_launch: Mutex::new(Vec::new()),
            steam,
            session: session.to_string(),
            intent_file,
            replace_log_on_launch,
        }
    }

    pub fn launches_with_dev_config(&self) -> Vec<bool> {
        self.dev_config_at_launch.lock().unwrap().clone()
    }

    pub fn launches_with_intent(&self) -> Vec<bool> {
        self.intent_at_launch.lock().unwrap().clone()
    }
}

impl SteamControlling for FakeSteam {
    fn is_running(&self) -> bool {
        *self.running.lock().unwrap()
    }

    fn quit(&self, _timeout: std::time::Duration) -> Result<(), SteamPlayError> {
        *self.running.lock().unwrap() = false;
        Ok(())
    }

    fn launch(&self) -> Result<(), SteamPlayError> {
        let has_dev_config = self.steam.steam_dev_config().exists();
        let has_intent = self.intent_file.as_ref().is_some_and(|f| f.exists());
        *self.running.lock().unwrap() = true;
        self.dev_config_at_launch.lock().unwrap().push(has_dev_config);
        self.intent_at_launch.lock().unwrap().push(has_intent);
        let old = if self.replace_log_on_launch {
            String::new()
        } else {
            std::fs::read_to_string(self.steam.compat_log()).unwrap_or_default()
        };
        write(&(old + &self.session), &self.steam.compat_log(), false);
        Ok(())
    }
}
