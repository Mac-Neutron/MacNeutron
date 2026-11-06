//! A game Steam uninstalled loses its VM drive, as Linux Steam deletes its Proton prefix (spec
//! 2026-10-24-macneutron-install-scripts-design §4.9). The drive is the part of the prefix MacNeutron owns; the Mac
//! prefix (compatdata, Mac Steam's, which holds the saves) stays for "Leftover data" (OrphanPrefixes). Ported from
//! `UninstalledDrives.swift`.

use std::path::{Path, PathBuf};

use crate::orphan_prefixes::OrphanPrefixes;
use crate::posix;
use crate::steam_location::SteamLocation;

pub struct UninstalledDrives;

impl UninstalledDrives {
    /// Deletes the drive (`state/<id>.img`, `.tpm/`, `.dxmt/`; `runs/<id>/`, `runs/<id>.lock`) of every app Steam lists
    /// nowhere: in no library's `apps` list and with no app manifest. Only while Steam still lists the library the game
    /// last ran from (the launcher's `runs/<id>/library`): a reset list (Steam reinstalled) names none of a drive's
    /// games, and a drive without the record stays. Nothing when libraryfolders.vdf can't be read, and never a drive
    /// whose VM runs (run-guest.sh holds its run lock). Returns one launcher.log line per drive deleted.
    pub fn remove(vm: &Path, steam: &SteamLocation) -> Vec<String> {
        let Some(listed) = steam.listed_app_ids() else { return Vec::new() };
        let installed = steam.installed_app_ids();
        let libraries = steam.libraries();
        let state = vm.join("state");
        let runs = vm.join("runs");
        let mut lines: Vec<String> = Vec::new();
        let Ok(entries) = std::fs::read_dir(&state) else { return lines };
        let mut names: Vec<String> = entries.flatten().filter_map(|e| e.file_name().into_string().ok()).collect();
        names.sort();
        for name in names {
            // A Steam app's drive only: not a test's (fonts.img), nor 0.img.
            let Some(id) = Self::drive_id(&name) else { continue };
            if id == 0 || id >= 0x8000_0000 || listed.contains(&id) || installed.contains(&id) {
                continue;
            }
            let Ok(ran_from) = std::fs::read_to_string(runs.join(format!("{id}/library"))) else { continue };
            let ran_from = PathBuf::from(ran_from.trim_end());
            if !libraries.iter().any(|l| posix::same_path(l, &ran_from)) {
                continue;
            }
            let _ = std::fs::create_dir_all(&runs);
            let lock = posix::open_exclusive_lock(&runs.join(format!("{id}.lock")));
            let Some(_lock) = lock else { continue }; // its VM runs: the next round
            let image = state.join(&name);
            if !image.exists() {
                continue; // a concurrent refresh's
            }
            let folders = [
                state.join(format!("{id}.tpm")),
                state.join(format!("{id}.dxmt")),
                runs.join(format!("{id}")),
            ];
            let bytes = Self::file_bytes(&image) + folders.iter().map(|f| OrphanPrefixes::size(f)).sum::<i64>();
            // The drive first: one that can't go keeps its folders, the library record among them, for the next round.
            let _ = std::fs::remove_file(&image);
            if image.exists() {
                continue;
            }
            for item in folders.iter().chain(std::iter::once(&runs.join(format!("{id}.lock")))) {
                if item.is_dir() {
                    let _ = std::fs::remove_dir_all(item);
                } else {
                    let _ = std::fs::remove_file(item);
                }
            }
            lines.push(format!(
                "deleted the game VM drive of app {id}: Steam uninstalled it ({})",
                byte_count(bytes)
            ));
        }
        lines
    }

    /// `<id>.img` with a canonical id, or None when the name is anything else.
    fn drive_id(name: &str) -> Option<u32> {
        let stem = name.strip_suffix(".img")?;
        let id: u32 = stem.parse().ok()?;
        if format!("{id}.img") == name {
            Some(id)
        } else {
            None
        }
    }

    /// Total allocated bytes of one file, the same measure Finder shows.
    fn file_bytes(path: &Path) -> i64 {
        use std::os::unix::fs::MetadataExt;
        match std::fs::metadata(path) {
            Ok(meta) if meta.is_file() => meta.blocks() as i64 * 512,
            _ => 0,
        }
    }
}

/// Approximates `ByteCountFormatter.string(fromByteCount:countStyle:.file)`: decimal units, one fraction digit.
fn byte_count(bytes: i64) -> String {
    const UNITS: [&str; 6] = ["bytes", "KB", "MB", "GB", "TB", "PB"];
    let mut value = bytes as f64;
    let mut unit = 0;
    while value >= 1000.0 && unit < UNITS.len() - 1 {
        value /= 1000.0;
        unit += 1;
    }
    if unit == 0 {
        format!("{} {}", bytes, UNITS[unit])
    } else {
        format!("{:.1} {}", value, UNITS[unit])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Steam's libraryfolders.vdf: library "0" (the Steam root) with `apps`, and `more` as a second library's.
    fn library_folders(root: &Path, apps: &[&str], more: Option<(&str, &[&str])>) -> String {
        fn library(key: &str, path: &str, apps: &[&str]) -> String {
            format!(
                "\t\"{key}\"\n\t{{\n\t\t\"path\"\t\t\"{path}\"\n\t\t\"apps\"\n\t\t{{\n{}    \t\t}}\n\t}}\n",
                apps.iter().map(|a| format!("\t\t\t\"{a}\"\t\t\"1000\"\n")).collect::<String>()
            )
        }
        let more = more.map(|(p, a)| library("1", p, a)).unwrap_or_default();
        format!("\"libraryfolders\"\n{{\n{}{}}}\n", library("0", &root.to_string_lossy(), apps), more)
    }

    struct Env {
        _root: PathBuf,
        steam: SteamLocation,
        vm: PathBuf,
    }

    impl Drop for Env {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self._root);
        }
    }

    fn write(path: &Path, content: &str) {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        std::fs::write(path, content).unwrap();
    }

    /// A Steam root listing 231350 (installed) and 555 (listed, its library's drive disconnected: no manifest),
    /// beside a VM folder with drives for 231350, 555, 999 (uninstalled), 0 and a test's named drive.
    fn make_env(name: &str) -> Env {
        let root = std::env::temp_dir().join(format!("mn-ud-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&root);
        let steam_root = root.join("steam");
        std::fs::create_dir_all(steam_root.join("steamapps")).unwrap();
        let vm = root.join("vm");
        for n in ["231350.img", "555.img", "999.img", "0.img", "fonts.img", "999.tpm/id", "999.dxmt/x"] {
            write(&vm.join("state").join(n), "disk");
        }
        for id in ["231350", "555", "999", "0"] {
            write(&vm.join("runs").join(id).join("library"), &steam_root.join("steamapps").to_string_lossy());
        }
        write(&vm.join("runs/999/console.log"), "console");
        write(&vm.join("runs/999.lock"), "");
        let steam = SteamLocation::new(steam_root);
        write(
            &steam.root.join("steamapps/libraryfolders.vdf"),
            &library_folders(&steam.root, &["231350", "555"], None),
        );
        write(&steam.root.join("steamapps/appmanifest_231350.acf"), "\"AppState\"\n{\n\"appid\" \"231350\"\n}\n");
        Env { _root: root, steam, vm }
    }

    fn exists(vm: &Path, path: &str) -> bool {
        std::fs::symlink_metadata(vm.join(path)).is_ok()
    }

    const APP999: [&str; 5] = ["state/999.img", "state/999.tpm", "state/999.dxmt", "runs/999", "runs/999.lock"];
    const KEPT: [&str; 4] = ["state/231350.img", "state/555.img", "state/0.img", "state/fonts.img"];

    #[test]
    fn an_uninstalled_games_drive_goes() {
        let env = make_env("goes");
        let lines = UninstalledDrives::remove(&env.vm, &env.steam);
        for path in APP999 {
            assert!(!exists(&env.vm, path), "{path} is still there");
        }
        for path in KEPT {
            assert!(exists(&env.vm, path), "{path} was deleted");
        }
        assert_eq!(lines.len(), 1);
        assert!(lines[0].starts_with("deleted the game VM drive of app 999: Steam uninstalled it ("));
    }

    #[test]
    fn nothing_goes_without_library_folders() {
        let env = make_env("nolf");
        let file = env.steam.root.join("steamapps/libraryfolders.vdf");
        let _ = std::fs::remove_file(&file);
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        write(&file, "\"libraryfolders\"\n{\n"); // Steam rewriting it
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        for path in APP999.iter().chain(KEPT.iter()) {
            assert!(exists(&env.vm, path), "{path} was deleted");
        }
    }

    #[test]
    fn a_running_vms_drive_stays() {
        let env = make_env("running");
        // run-guest.sh's lockf holds it while the VM runs.
        let held = posix::open_exclusive_lock(&env.vm.join("runs/999.lock")).unwrap();
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        assert!(exists(&env.vm, "state/999.img"));
        drop(held);
        assert_eq!(UninstalledDrives::remove(&env.vm, &env.steam).len(), 1);
        assert!(!exists(&env.vm, "state/999.img"));
    }

    #[test]
    fn a_drive_from_a_library_steam_no_longer_lists_stays() {
        let env = make_env("reset");
        write(&env.vm.join("runs/999/library"), "/Volumes/Games/steamapps");
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        let _ = std::fs::remove_file(env.vm.join("runs/999/library"));
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        assert!(exists(&env.vm, "state/999.img"));
    }

    #[test]
    fn a_drive_that_cant_be_removed_is_not_logged_as_deleted() {
        use std::os::unix::fs::PermissionsExt;
        let env = make_env("readonly");
        let state = env.vm.join("state");
        let mut perms = std::fs::metadata(&state).unwrap().permissions();
        perms.set_mode(0o555);
        std::fs::set_permissions(&state, perms).unwrap();
        assert!(UninstalledDrives::remove(&env.vm, &env.steam).is_empty());
        assert!(exists(&env.vm, "state/999.img"));
        assert!(exists(&env.vm, "runs/999/library")); // so the next round tries again
        let mut perms = std::fs::metadata(&state).unwrap().permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&state, perms).unwrap();
    }
}
