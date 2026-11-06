//! The Rust counterpart of `Tests/MacNeutronCoreTests/Support.swift`: the fixtures every ported test uses.
#![allow(dead_code)]

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Mutex;

use crate::launcher_log::LauncherLog;
use crate::process_runner::{ExitStatus, ProcessRunner};
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
        let status = (self.respond)(&call);
        started(1);
        self.calls.lock().unwrap().push(call);
        Ok(status)
    }
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
