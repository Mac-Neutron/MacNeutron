//! Easy Anti-Cheat (EAC Linux v1: Legacy EAC 1 & EOS EAC 2) game detection and Proton runtime inspection.

use std::collections::VecDeque;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::steam_location::SteamLocation;

pub const EAC_RUNTIME_APP_ID: u32 = 1826330;
pub const EAC_RUNTIME_DEPOT_ID: u32 = 1826331;
pub const EAC_INSTALL_URL: &str = "steam://install/1826330";
pub const EAC_CONSOLE_URL: &str = "steam://open/console";
pub const EAC_DEPOT_COMMAND: &str = "download_depot 1826330 1826331";

const MAX_SCAN_DEPTH: usize = 3;
const MAX_SCAN_ENTRIES: usize = 2048;
const MAX_ENTRIES_PER_DIR: usize = 512;
const MAX_SETTINGS_BYTES: u64 = 256 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EacKind {
    Legacy,
    Eos,
}

impl EacKind {
    pub fn label(self) -> &'static str {
        match self {
            EacKind::Legacy => "Legacy",
            EacKind::Eos => "EOS",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EacRuntimeInfo {
    pub path: PathBuf,
    pub has_x64: bool,
    pub has_eos: bool,
    pub has_x86: bool,
}

impl EacRuntimeInfo {
    pub fn is_complete(&self) -> bool {
        self.has_x64 && self.has_eos && self.has_x86
    }

    pub fn missing_components(&self) -> Vec<&'static str> {
        let mut missing = Vec::new();
        if !self.has_x64 {
            missing.push("easyanticheat_x64");
        }
        if !self.has_eos {
            missing.push("easyanticheat (EOS)");
        }
        if !self.has_x86 {
            missing.push("easyanticheat_x86");
        }
        missing
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EacRuntimeStatus {
    Missing,
    Incomplete(EacRuntimeInfo),
    Ready(EacRuntimeInfo),
}

impl EacRuntimeStatus {
    pub fn path(&self) -> Option<&Path> {
        match self {
            EacRuntimeStatus::Missing => None,
            EacRuntimeStatus::Incomplete(info) | EacRuntimeStatus::Ready(info) => Some(&info.path),
        }
    }

    pub fn is_ready(&self) -> bool {
        matches!(self, EacRuntimeStatus::Ready(_))
    }
}

/// Candidate host directories where Valve's `Proton EasyAntiCheat Runtime` (app 1826330 / depot 1826331) may live.
pub fn candidate_paths(steam: &SteamLocation) -> Vec<PathBuf> {
    let libraries = steam.libraries();
    let depot = format!("content/app_{EAC_RUNTIME_APP_ID}/depot_{EAC_RUNTIME_DEPOT_ID}");
    let mut paths = Vec::new();
    let mut push_unique = |candidate: PathBuf| {
        if !paths.contains(&candidate) {
            paths.push(candidate);
        }
    };

    for library in &libraries {
        push_unique(library.join("common/Proton EasyAntiCheat Runtime"));
    }
    for library in &libraries {
        push_unique(library.join(&depot));
    }
    push_unique(steam.bundle_macos().join("steamapps").join(&depot));
    paths
}

/// Inspects a candidate Proton EasyAntiCheat Runtime directory. Returns `None` unless `dir/v2` exists as a directory.
pub fn inspect_path(dir: &Path) -> Option<EacRuntimeInfo> {
    let v2 = dir.join("v2");
    if !v2.is_dir() {
        return None;
    }
    let has_x64 = v2.join("lib64/easyanticheat_x64.so").is_file() && v2.join("lib64/easyanticheat_x64.dll").is_file();
    let has_eos = v2.join("lib64/easyanticheat.so").is_file() && v2.join("lib64/easyanticheat.dll").is_file();
    let has_x86 = v2.join("lib32/easyanticheat_x86.so").is_file() && v2.join("lib32/easyanticheat_x86.dll").is_file();
    Some(EacRuntimeInfo {
        path: dir.to_path_buf(),
        has_x64,
        has_eos,
        has_x86,
    })
}

/// Scans `candidate_paths(steam)`: prefers the first `Ready` (complete) runtime, else the first `Incomplete` runtime
/// (has `v2/` with missing components), else `Missing`.
pub fn runtime_status(steam: &SteamLocation) -> EacRuntimeStatus {
    let mut first_incomplete = None;
    for candidate in candidate_paths(steam) {
        let Some(info) = inspect_path(&candidate) else {
            continue;
        };
        if info.is_complete() {
            return EacRuntimeStatus::Ready(info);
        }
        if first_incomplete.is_none() {
            first_incomplete = Some(info);
        }
    }
    match first_incomplete {
        Some(info) => EacRuntimeStatus::Incomplete(info),
        None => EacRuntimeStatus::Missing,
    }
}

/// Returns the best available Proton EasyAntiCheat Runtime path, preferring complete installations over incomplete `v2/`
/// directories.
pub fn find_runtime(steam: &SteamLocation) -> Option<PathBuf> {
    runtime_status(steam).path().map(Path::to_path_buf)
}

/// Bounded BFS detection of Easy Anti-Cheat inside an installed game directory. Never follows symlinks.
pub fn detect_game(game_dir: &Path) -> Option<EacKind> {
    let Ok(meta) = std::fs::symlink_metadata(game_dir) else {
        return None;
    };
    if !meta.is_dir() {
        return None;
    }

    let mut queue = VecDeque::new();
    queue.push_back((game_dir.to_path_buf(), 0usize));
    let mut entries_seen = 0usize;
    let mut legacy_found = false;

    while let Some((dir, depth)) = queue.pop_front() {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten().take(MAX_ENTRIES_PER_DIR) {
            if entries_seen >= MAX_SCAN_ENTRIES {
                return legacy_found.then_some(EacKind::Legacy);
            }
            entries_seen += 1;

            let Ok(ft) = entry.file_type() else {
                continue;
            };
            if ft.is_symlink() {
                continue;
            }

            let name = entry.file_name();
            let Some(name) = name.to_str() else {
                continue;
            };

            if ft.is_dir() {
                if name.eq_ignore_ascii_case("easyanticheat_eos") {
                    return Some(EacKind::Eos);
                }
                if name.eq_ignore_ascii_case("easyanticheat") {
                    if eac_dir_has_eos_settings(&entry.path()) {
                        return Some(EacKind::Eos);
                    }
                    legacy_found = true;
                }
                if depth < MAX_SCAN_DEPTH {
                    queue.push_back((entry.path(), depth + 1));
                }
            } else if ft.is_file() {
                if is_eos_indicator_file(name) {
                    return Some(EacKind::Eos);
                }
                if is_legacy_indicator_file(name) {
                    legacy_found = true;
                }
            }
        }
    }

    legacy_found.then_some(EacKind::Legacy)
}

/// Returns `true` if any launch argument disables Easy Anti-Cheat (`-noeac` or `/noeac`, ASCII case-insensitive).
pub fn launch_args_disable_eac(arguments: &[String]) -> bool {
    arguments
        .iter()
        .any(|arg| arg.eq_ignore_ascii_case("-noeac") || arg.eq_ignore_ascii_case("/noeac"))
}

fn is_eos_indicator_file(name: &str) -> bool {
    name.eq_ignore_ascii_case("start_protected_game.exe")
        || name.eq_ignore_ascii_case("easyanticheat_eos_setup.exe")
        || name.eq_ignore_ascii_case("easyanticheat_eos_x64.dll")
        || name.eq_ignore_ascii_case("easyanticheat_eos_x86.dll")
}

fn is_legacy_indicator_file(name: &str) -> bool {
    name.eq_ignore_ascii_case("easyanticheat_x64.dll")
        || name.eq_ignore_ascii_case("easyanticheat_x86.dll")
        || name.eq_ignore_ascii_case("easyanticheat.dll")
        || name.eq_ignore_ascii_case("easyanticheat_setup.exe")
}

fn eac_dir_has_eos_settings(dir: &Path) -> bool {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return false;
    };
    for entry in entries.flatten() {
        let Ok(ft) = entry.file_type() else {
            continue;
        };
        if !ft.is_file() {
            continue;
        }
        let name = entry.file_name();
        let Some(name) = name.to_str() else {
            continue;
        };
        if !name.eq_ignore_ascii_case("settings.json") {
            continue;
        }
        let Ok(file) = File::open(entry.path()) else {
            continue;
        };
        let mut buf = Vec::new();
        if file.take(MAX_SETTINGS_BYTES).read_to_end(&mut buf).is_err() {
            continue;
        }
        let text = String::from_utf8_lossy(&buf).to_ascii_lowercase();
        if text.contains("\"deploymentid\"") {
            return true;
        }
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, write};

    const ALL_RUNTIME_FILES: [&str; 6] = [
        "v2/lib64/easyanticheat_x64.so",
        "v2/lib64/easyanticheat_x64.dll",
        "v2/lib64/easyanticheat.so",
        "v2/lib64/easyanticheat.dll",
        "v2/lib32/easyanticheat_x86.so",
        "v2/lib32/easyanticheat_x86.dll",
    ];

    fn make_complete_runtime(dir: &Path) {
        for rel in ALL_RUNTIME_FILES {
            write("stub", &dir.join(rel), false);
        }
    }

    fn steam_with_secondary_library() -> (SteamLocation, PathBuf) {
        let base = make_temp_dir();
        let steam = SteamLocation::new(base.join("Steam"));
        let extra = base.join("Games Drive");
        write(
            &format!(
                "\"libraryfolders\"\n{{\n\t\"0\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n\t\"1\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n}}\n",
                steam.root.display(),
                extra.display()
            ),
            &steam.root.join("steamapps/libraryfolders.vdf"),
            false,
        );
        (steam, extra)
    }

    #[test]
    fn candidate_paths_cover_primary_secondary_and_bundle_and_deduplicate() {
        let (steam, extra) = steam_with_secondary_library();
        let candidates = candidate_paths(&steam);
        assert_eq!(
            candidates,
            vec![
                steam.root.join("steamapps/common/Proton EasyAntiCheat Runtime"),
                extra.join("steamapps/common/Proton EasyAntiCheat Runtime"),
                steam.root.join("steamapps/content/app_1826330/depot_1826331"),
                extra.join("steamapps/content/app_1826330/depot_1826331"),
                steam.bundle_macos().join("steamapps/content/app_1826330/depot_1826331"),
            ]
        );
    }

    #[test]
    fn runtime_status_classifies_missing_incomplete_and_ready() {
        let (steam, extra) = steam_with_secondary_library();
        assert_eq!(runtime_status(&steam), EacRuntimeStatus::Missing);
        assert_eq!(find_runtime(&steam), None);

        // Bare v2/ in secondary library common/ is Incomplete.
        let secondary_common = extra.join("steamapps/common/Proton EasyAntiCheat Runtime");
        std::fs::create_dir_all(secondary_common.join("v2")).unwrap();
        let status = runtime_status(&steam);
        assert!(!status.is_ready());
        assert_eq!(status.path(), Some(secondary_common.as_path()));
        match status {
            EacRuntimeStatus::Incomplete(info) => {
                assert!(!info.is_complete());
                assert_eq!(
                    info.missing_components(),
                    vec!["easyanticheat_x64", "easyanticheat (EOS)", "easyanticheat_x86"]
                );
            }
            other => panic!("expected Incomplete, got {other:?}"),
        }

        // Complete runtime in secondary depot takes precedence over bare v2/ in earlier candidate.
        let secondary_depot = extra.join("steamapps/content/app_1826330/depot_1826331");
        make_complete_runtime(&secondary_depot);
        let ready = runtime_status(&steam);
        assert!(ready.is_ready());
        assert_eq!(ready.path(), Some(secondary_depot.as_path()));
        assert_eq!(find_runtime(&steam), Some(secondary_depot));
    }

    #[test]
    fn detect_game_identifies_eos_legacy_and_non_eac() {
        // SMITE 2 layout: Windows/EasyAntiCheat/Settings.json with deploymentid + Windows/start_protected_game.exe
        let smite = make_temp_dir().join("SMITE 2");
        write(
            "{\"productid\":\"abc\",\"sandboxid\":\"def\",\"deploymentid\":\"ghi\"}",
            &smite.join("Windows/EasyAntiCheat/Settings.json"),
            false,
        );
        write("mz", &smite.join("Windows/start_protected_game.exe"), false);
        assert_eq!(detect_game(&smite), Some(EacKind::Eos));

        // EasyAntiCheat_EOS directory alone
        let eos_dir = make_temp_dir().join("EosGame");
        std::fs::create_dir_all(eos_dir.join("EasyAntiCheat_EOS")).unwrap();
        assert_eq!(detect_game(&eos_dir), Some(EacKind::Eos));

        // Legacy EAC 1 without deploymentid
        let legacy = make_temp_dir().join("LegacyGame");
        write("mz", &legacy.join("EasyAntiCheat/EasyAntiCheat_x64.dll"), false);
        write("{\"title\":\"Legacy\"}", &legacy.join("EasyAntiCheat/Settings.json"), false);
        assert_eq!(detect_game(&legacy), Some(EacKind::Legacy));

        // Legacy folder seen before sibling EOS indicator still upgrades to Eos
        let mixed = make_temp_dir().join("MixedGame");
        write("{\"title\":\"Legacy\"}", &mixed.join("EasyAntiCheat/Settings.json"), false);
        write("mz", &mixed.join("z_start_dir/EasyAntiCheat_EOS_Setup.exe"), false);
        assert_eq!(detect_game(&mixed), Some(EacKind::Eos));

        // Non-EAC game
        let plain = make_temp_dir().join("PlainGame");
        write("mz", &plain.join("Game.exe"), false);
        assert_eq!(detect_game(&plain), None);
    }

    #[test]
    fn detect_game_ignores_symlink_cycles() {
        let dir = make_temp_dir().join("SymlinkGame");
        std::fs::create_dir_all(dir.join("sub")).unwrap();
        std::os::unix::fs::symlink(&dir, dir.join("sub/loop")).unwrap();
        assert_eq!(detect_game(&dir), None);

        // Symlinked EasyAntiCheat directory is ignored as well
        let eac_target = make_temp_dir().join("Elsewhere");
        std::fs::create_dir_all(eac_target.join("EasyAntiCheat_EOS")).unwrap();
        std::os::unix::fs::symlink(eac_target.join("EasyAntiCheat_EOS"), dir.join("EasyAntiCheat_EOS")).unwrap();
        assert_eq!(detect_game(&dir), None);
    }

    #[test]
    fn detect_game_bounds_per_directory_scan_so_large_sibling_cannot_starve_eac_dir() {
        let dir = make_temp_dir().join("HugeAssetGame");
        let assets = dir.join("AAA_Assets");
        std::fs::create_dir_all(&assets).unwrap();
        for i in 0..650 {
            std::fs::write(assets.join(format!("asset_{i:04}.pak")), b"").unwrap();
        }
        write("mz", &dir.join("ZZZ_Windows/start_protected_game.exe"), false);
        assert_eq!(detect_game(&dir), Some(EacKind::Eos));
    }

    #[test]
    fn launch_args_disable_eac_checks_case_insensitive_flags() {
        assert!(!launch_args_disable_eac(&["-d3d12".into()]));
        assert!(launch_args_disable_eac(&["-NOEAC".into()]));
        assert!(launch_args_disable_eac(&["-noeac".into()]));
        assert!(launch_args_disable_eac(&["/NoEac".into()]));
    }
}
