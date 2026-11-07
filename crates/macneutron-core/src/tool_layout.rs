//! Where everything lives inside the `macneutron` compatibility tool folder.

use std::path::{Path, PathBuf};

use crate::code_identity::CodeIdentity;
use crate::paths::MacNeutronPaths;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolLayout {
    pub root: PathBuf,
}

impl ToolLayout {
    pub fn new(root: PathBuf) -> Self {
        ToolLayout { root }
    }

    /// `<root>/bin/macneutron` -> `<root>`.
    pub fn for_executable(executable: &Path) -> Self {
        let resolved = std::fs::canonicalize(executable).unwrap_or_else(|_| executable.to_path_buf());
        let root = resolved
            .parent()
            .and_then(|p| p.parent())
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| PathBuf::from("/"));
        ToolLayout { root }
    }

    pub fn default_root() -> PathBuf {
        MacNeutronPaths::tools().join("macneutron")
    }

    /// The runtime: an arm64 Wine with DXMT, FEX and lsteamclient inside, installed from MacNeutron.app.
    pub fn wine_app(&self) -> PathBuf {
        self.root.join("wine.app")
    }

    fn resources(&self) -> PathBuf {
        self.wine_app().join("Contents/Resources")
    }

    pub fn wine(&self) -> PathBuf {
        self.wine_app().join("Contents/MacOS/wine")
    }

    pub fn wineserver(&self) -> PathBuf {
        self.resources().join("bin/wineserver")
    }

    pub fn dxmt(&self) -> PathBuf {
        self.resources().join("DXMT/aarch64-windows")
    }

    /// The DLLs every prefix gets in `system32`.
    pub const DXMT_DLLS: [&'static str; 6] = [
        "d3d10core.dll",
        "d3d11.dll",
        "d3d12.dll",
        "dxgi.dll",
        "nvapi64.dll",
        "nvngx.dll",
    ];

    /// The key of DXMT's shader translator: a hash of what changes translated output (wine-arm64/build.sh), not DXMT's
    /// version, so a DXMT update that leaves the translator alone keeps the game's translated shaders.
    pub fn dxmt_translator_file(&self) -> PathBuf {
        self.resources().join("DXMT/translator")
    }

    /// The CPU the runtime was built for, then the `hw.optional.arm` features its code may use, one a line (absent
    /// for the generic build: every Apple Silicon Mac runs it).
    pub fn cpu_file(&self) -> PathBuf {
        self.resources().join("cpu")
    }

    pub fn dxmt_translator(&self) -> Option<String> {
        std::fs::read_to_string(self.dxmt_translator_file())
            .ok()
            .map(|s| s.trim().to_string())
    }

    /// DXMT's pipeline replayer (shader pre-caching), run by the launcher under Wine.
    pub fn dxmt_replay(&self) -> PathBuf {
        self.dxmt().join("dxmt-replay.exe")
    }

    /// The Steam client bridge (Proton's lsteamclient), both halves.
    pub fn lsteamclient(&self) -> PathBuf {
        self.resources().join("lib/wine/aarch64-windows/lsteamclient.dll")
    }

    pub fn lsteamclient_unix(&self) -> PathBuf {
        self.resources().join("lib/wine/aarch64-unix/lsteamclient.so")
    }

    pub fn launcher_binary(&self) -> PathBuf {
        self.root.join("bin/macneutron")
    }

    /// MacNeutron's `steam.exe`, installed next to the launcher.
    pub fn steam_helper(&self) -> PathBuf {
        self.root.join("bin/steam.exe")
    }

    /// `steam.exe` and both halves of the bridge are present.
    pub fn steam_bridge_installed(&self) -> bool {
        [self.steam_helper(), self.lsteamclient_unix(), self.lsteamclient()]
            .iter()
            .all(|p| p.exists())
    }

    /// Written by `bundle.sh` when the runtime has Wine 0036, which makes a genuine `steamclient64.dll`'s exports jump to
    /// lsteamclient. An older runtime would fail to load Valve's DLL (its `tier0_s64` import), so the game gets no Steam.
    pub fn supports_genuine_steamclient(&self) -> bool {
        self.resources().join("genuine-steamclient").exists()
    }

    /// `MacNeutron/Valve`, beside `MacNeutron/compatibilitytools.d`: Valve's Windows client files, downloaded by the
    /// app from Valve (`ValveClient`).
    pub fn valve_cache(&self) -> PathBuf {
        self.root
            .parent()
            .and_then(|p| p.parent())
            .map(|p| p.join("Valve"))
            .unwrap_or_else(|| PathBuf::from("Valve"))
    }

    /// Written by a launch that finds `wine.app` missing or unreadable; the app then reinstalls it.
    pub fn runtime_damaged_marker(&self) -> PathBuf {
        self.root.join("runtime-damaged")
    }

    /// `wine.app`'s `CFBundleShortVersionString`, read from disk each time (`Bundle` caches per path, and an install
    /// swaps the bundle under the same path).
    pub fn runtime_version(&self) -> Option<String> {
        let data = std::fs::read(self.wine_app().join("Contents/Info.plist")).ok()?;
        let value: plist::Value = plist::from_bytes(&data).ok()?;
        value
            .as_dictionary()?
            .get("CFBundleShortVersionString")?
            .as_string()
            .map(|s| s.to_string())
    }

    /// `wine.app`'s CDHash, or None when it is missing or unsigned.
    pub fn identity(&self) -> Option<String> {
        CodeIdentity::of(&self.wine_app())
    }

    /// A performance build's runtime (X.Y.Zp, spec 2026-10-19-macneutron-performance-build): traces compiled out, and
    /// quiet unless MACNEUTRON_LOG is set.
    pub fn is_performance_build(&self) -> bool {
        Self::is_performance_build_version(self.runtime_version().as_deref())
    }

    pub fn is_performance_build_version(version: Option<&str>) -> bool {
        version.map(|v| v.ends_with('p')).unwrap_or(false)
    }

    /// `<version> (<identity, 12 hex digits>)`, for launcher.log and the menu; `<version> (performance build) (...)` for
    /// X.Y.Zp.
    pub fn runtime_label(version: Option<&str>, identity: Option<&str>) -> String {
        let build = if Self::is_performance_build_version(version) {
            " (performance build)"
        } else {
            ""
        };
        let ident = match identity {
            Some(i) => i.chars().take(12).collect(),
            None => "unsigned".to_string(),
        };
        format!("{}{} ({})", version.unwrap_or("unknown"), build, ident)
    }

    /// What the Rosetta-era runtime left in the tool folder; the install removes them.
    pub const ROSETTA_ERA_ENTRIES: [&'static str; 8] = [
        "Libraries",
        "gptk",
        "gptk.json",
        "gptk.staging",
        "lib",
        "dxmt-version",
        "runtime-version",
        "runtime.staging",
    ];
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layout_paths_follow_wine_app() {
        let layout = ToolLayout::new(PathBuf::from("/t/macneutron"));
        let app = "/t/macneutron/wine.app/Contents";
        assert_eq!(layout.wine_app(), PathBuf::from("/t/macneutron/wine.app"));
        assert_eq!(layout.wine(), PathBuf::from(format!("{app}/MacOS/wine")));
        assert_eq!(layout.wineserver(), PathBuf::from(format!("{app}/Resources/bin/wineserver")));
        assert_eq!(layout.dxmt(), PathBuf::from(format!("{app}/Resources/DXMT/aarch64-windows")));
        assert_eq!(layout.dxmt_translator_file(), PathBuf::from(format!("{app}/Resources/DXMT/translator")));
        assert_eq!(layout.dxmt_replay(), PathBuf::from(format!("{app}/Resources/DXMT/aarch64-windows/dxmt-replay.exe")));
        assert_eq!(layout.lsteamclient(), PathBuf::from(format!("{app}/Resources/lib/wine/aarch64-windows/lsteamclient.dll")));
        assert_eq!(layout.lsteamclient_unix(), PathBuf::from(format!("{app}/Resources/lib/wine/aarch64-unix/lsteamclient.so")));
        assert_eq!(layout.launcher_binary(), PathBuf::from("/t/macneutron/bin/macneutron"));
        assert_eq!(layout.steam_helper(), PathBuf::from("/t/macneutron/bin/steam.exe"));
        assert_eq!(layout.runtime_damaged_marker(), PathBuf::from("/t/macneutron/runtime-damaged"));
    }

    #[test]
    fn valve_cache_sits_beside_the_tool_folders() {
        let layout = ToolLayout::new(ToolLayout::default_root());
        let home = std::env::var_os("HOME").map(PathBuf::from).unwrap();
        assert_eq!(layout.valve_cache(), home.join("Library/Application Support/MacNeutron/Valve"));
    }

    #[test]
    fn runtime_label_formats() {
        assert_eq!(ToolLayout::runtime_label(Some("test"), Some("0123456789abcdef")), "test (0123456789ab)");
        assert_eq!(ToolLayout::runtime_label(None, None), "unknown (unsigned)");
    }

    #[test]
    fn a_performance_runtime_is_labelled() {
        assert_eq!(
            ToolLayout::runtime_label(Some("0.7.1p"), Some("0123456789abcdef")),
            "0.7.1p (performance build) (0123456789ab)"
        );
        assert_eq!(
            ToolLayout::runtime_label(Some("0.7.1"), Some("0123456789abcdef")),
            "0.7.1 (0123456789ab)"
        );
    }

    #[test]
    fn genuine_steamclient_support_is_the_runtimes_marker() {
        use crate::test_support::{make_tool_layout, write};
        let layout = make_tool_layout();
        assert!(!layout.supports_genuine_steamclient());
        write("0036\n", &layout.wine_app().join("Contents/Resources/genuine-steamclient"), false);
        assert!(layout.supports_genuine_steamclient());
    }

    #[test]
    fn layout_from_executable_is_two_levels_up() {
        let layout = ToolLayout::for_executable(Path::new("/t/macneutron/bin/macneutron"));
        assert!(layout.root.ends_with("t/macneutron"));
    }

    #[test]
    fn runtime_version_comes_from_wine_app_info_plist() {
        use crate::test_support::{make_temp_dir, write};
        let layout = ToolLayout::new(make_temp_dir());
        assert_eq!(layout.runtime_version(), None);
        let plist = layout.wine_app().join("Contents/Info.plist");
        let body = |v: &str| format!("<plist><dict><key>CFBundleShortVersionString</key><string>{v}</string></dict></plist>");
        write(&body("0.1.0"), &plist, false);
        assert_eq!(layout.runtime_version().as_deref(), Some("0.1.0"));
        // Read each time: the app keeps one layout while installs swap wine.app underneath it.
        write(&body("0.1.1"), &plist, false);
        assert_eq!(layout.runtime_version().as_deref(), Some("0.1.1"));
    }
}
