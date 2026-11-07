//! Where the Steam bridge lives inside a prefix, and the small decisions the launcher makes about it.
//! The bridge itself (Proton's lsteamclient) ships with the runtime; `steam.exe` ships with MacNeutron.

use std::path::{Path, PathBuf};

use crate::steam_location::SteamLocation;
use crate::tool_layout::ToolLayout;

pub struct SteamBridge;

impl SteamBridge {
    /// The `steam.exe` the launcher runs: Proton's, in `system32` (proton:2121-2122).
    pub const STEAM_EXE: &'static str = r"C:\windows\system32\steam.exe";

    /// The Steam folder's copy, which `HKCR\steam` names and which three games run (proton:2109-2115).
    pub const PROGRAM_FILES_STEAM_EXE: &'static str = r"C:\Program Files (x86)\Steam\steam.exe";

    /// `steam.exe` as Proton runs it for `game_id` (SteamGameId): from the Steam folder for Call of Duty: Black Ops
    /// III, Black Ops Cold War and Undecember. Only the location: Proton's 32-bit helper there needs WoW64
    /// (parity D-14).
    pub fn steam_exe(game_id: Option<&str>) -> &'static str {
        match game_id {
            Some("311210") | Some("1985810") | Some("1549250") => Self::PROGRAM_FILES_STEAM_EXE,
            _ => Self::STEAM_EXE,
        }
    }

    /// The folder `steam.exe` names in the registry, relative to the prefix.
    pub const PREFIX_FOLDER: &'static str = "drive_c/Program Files (x86)/Steam";

    /// A launch option for the prefix's `steamclient64.dll`: an absolute Mac path to another copy of Valve's signed
    /// DLL, or `builtin` for lsteamclient.dll (no Valve DLL). Without it the launcher uses the app's download from
    /// Valve when the runtime has Wine patch 0036, which makes the genuine DLL's exports jump to lsteamclient.dll's.
    pub const STEAMCLIENT64_VARIABLE: &'static str = "MACNEUTRON_STEAMCLIENT64";

    /// What the launcher copies into a prefix: (source, destination relative to the prefix).
    pub fn prefix_files(layout: &ToolLayout, steam_client64: Option<PathBuf>) -> Vec<(PathBuf, String)> {
        vec![
            (layout.steam_helper(), "drive_c/windows/system32/steam.exe".to_string()),
            (layout.steam_helper(), format!("{}/steam.exe", Self::PREFIX_FOLDER)),
            (
                steam_client64.unwrap_or_else(|| layout.lsteamclient()),
                format!("{}/steamclient64.dll", Self::PREFIX_FOLDER),
            ),
        ]
    }

    /// Steam's `STEAM_COMPAT_CLIENT_INSTALL_PATH` when that folder holds `steamclient.dylib`,
    /// otherwise the Steam bundle's MacOS folder, where macOS Steam keeps it.
    pub fn client_directory(steam_value: Option<&str>, steam: &SteamLocation) -> String {
        if let Some(value) = steam_value {
            if Path::new(value).join("steamclient.dylib").exists() {
                return value.to_string();
            }
        }
        let mut bundle = path_string(&steam.bundle_macos());
        if bundle.ends_with('/') {
            bundle.pop();
        }
        bundle
    }
}

fn path_string(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::steam_location::SteamLocation;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    fn steam_location(name: &str, with_client: bool) -> SteamLocation {
        let root = temp(name).join("Steam");
        let steam = SteamLocation::new(root);
        if with_client {
            let macos = steam.bundle_macos();
            std::fs::create_dir_all(&macos).unwrap();
            std::fs::write(macos.join("steamclient.dylib"), b"dylib").unwrap();
        }
        steam
    }

    #[test]
    fn steam_exe_uses_the_steam_folder_for_the_three_games() {
        assert_eq!(SteamBridge::steam_exe(None), SteamBridge::STEAM_EXE);
        assert_eq!(SteamBridge::steam_exe(Some("311210")), SteamBridge::PROGRAM_FILES_STEAM_EXE);
        assert_eq!(SteamBridge::steam_exe(Some("1985810")), SteamBridge::PROGRAM_FILES_STEAM_EXE);
        assert_eq!(SteamBridge::steam_exe(Some("1549250")), SteamBridge::PROGRAM_FILES_STEAM_EXE);
        assert_eq!(SteamBridge::steam_exe(Some("12345")), SteamBridge::STEAM_EXE);
    }

    #[test]
    fn client_directory_keeps_steams_value_when_it_holds_the_library() {
        let steam = steam_location("keep", true);
        let other = temp("other");
        std::fs::write(other.join("steamclient.dylib"), b"dylib").unwrap();
        let value = other.to_string_lossy().into_owned();
        assert_eq!(SteamBridge::client_directory(Some(&value), &steam), value);
    }

    #[test]
    fn client_directory_falls_back_to_the_steam_bundle() {
        let steam = steam_location("fallback", true);
        let bundle = steam.bundle_macos().to_string_lossy().trim_end_matches('/').to_string();
        let root = steam.root.to_string_lossy().into_owned();
        assert_eq!(SteamBridge::client_directory(Some(&root), &steam), bundle);
        assert_eq!(SteamBridge::client_directory(None, &steam), bundle);
    }

    use crate::test_support::registry_vdf;

    #[test]
    fn ui_language_comes_from_registry_vdf() {
        let steam = crate::test_support::make_steam_location(true);
        crate::test_support::write(&registry_vdf("french"), &steam.registry_vdf(), false);
        assert_eq!(steam.ui_language().as_deref(), Some("french"));
        assert_eq!(crate::test_support::make_steam_location(true).ui_language(), None);
    }

    #[test]
    fn bridge_needs_steam_exe_and_both_halves_of_the_client() {
        use crate::test_support::{install_fake_steam_bridge, make_tool_layout};
        let layout = make_tool_layout();
        assert!(!layout.steam_bridge_installed()); // wine.app has lsteamclient; the tool folder has no steam.exe yet
        install_fake_steam_bridge(&layout);
        assert!(layout.steam_bridge_installed());
        for half in [layout.lsteamclient_unix(), layout.lsteamclient()] {
            let saved = std::fs::read(&half).unwrap();
            std::fs::remove_file(&half).unwrap();
            assert!(!layout.steam_bridge_installed());
            std::fs::write(&half, saved).unwrap();
        }
        assert!(layout.steam_bridge_installed());
    }
}
