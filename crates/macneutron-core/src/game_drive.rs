//! Proton's `S:` game drive (proton:299-351): `pfx/dosdevices/s:` links to the game library's parent, so the game sees
//! itself at `S:\steamapps\common\<Game>` (Wine converts a path to its deepest drive). On by default, as in Proton;
//! `PROTON_SET_GAME_DRIVE=0` (or empty) turns it off for a game.

use std::path::Path;
use std::collections::VecDeque;

use crate::posix;

pub struct GameDrive;

impl GameDrive {
    /// Proton's try_get_game_library_dir and get_validated_steamapps_parent (proton:299-322): the link's target, None or
    /// "" to remove it. A library that fails validation gives its raw entry, the steamapps folder itself (Proton's
    /// fallback), and Proton's message in `notes`.
    pub fn target(
        environment: &std::collections::BTreeMap<String, String>,
        notes: &mut Vec<String>,
    ) -> Option<String> {
        Self::target_with(environment, posix::device, notes)
    }

    /// As `target`, with the device probe injectable so tests aren't tied to the real volumes.
    fn target_with(
        environment: &std::collections::BTreeMap<String, String>,
        device: impl Fn(&Path) -> Option<libc::dev_t>,
        notes: &mut Vec<String>,
    ) -> Option<String> {
        let install = environment.get("STEAM_COMPAT_INSTALL_PATH")?;
        let libraries = environment.get("STEAM_COMPAT_LIBRARY_PATHS")?;
        // Proton's `entry in install_path`: a substring anywhere, the first match only, an empty piece matching every
        // path (spelled out: Foundation's contains("") is false).
        let entry = libraries
            .split(':')
            .find(|part| part.is_empty() || install.contains(*part))?
            .to_string();
        if let Some(path) = posix::real_path(Path::new(&entry)) {
            let meta = std::fs::metadata(&path);
            if let Ok(meta) = meta {
                if meta.is_dir() && Self::python_name(&entry) == "steamapps" {
                    let parent = Path::new(&path).parent().map(|p| p.to_string_lossy().into_owned());
                    if let Some(parent) = parent {
                        if is_writable(Path::new(&parent)) && device(Path::new(&path)) == device(Path::new(&parent)) {
                            return Some(parent);
                        }
                    }
                }
            }
        }
        notes.push(format!("Proton: Error: unable to use parent for game drive, path {entry}"));
        Some(entry)
    }

    /// The last part of Python's os.path.realpath(path) (posixpath, not strict), which Proton checks: a part that isn't
    /// a link keeps its spelling, a link is replaced by its text, "." goes and ".." drops the part before it. Darwin's
    /// realpath(3) gives the on-disk case instead, so Mac Steam's ".../steamapps" on a legacy on-disk "SteamApps"
    /// folder would fail the check.
    pub fn python_name(path: &str) -> String {
        let mut resolved: Vec<String> = Vec::new();
        let mut pending: std::collections::VecDeque<String> =
            path.split('/').filter(|s| !s.is_empty()).map(|s| s.to_string()).collect();
        let mut hops = 0;
        while let Some(part) = pending.pop_front() {
            if part == "." {
                continue;
            }
            if part == ".." {
                resolved.pop();
                continue;
            }
            let candidate = format!("/{}{}{}", resolved.join("/"), if resolved.is_empty() { "" } else { "/" }, part);
            if hops < 40 {
                if let Ok(text) = std::fs::read_link(&candidate) {
                    hops += 1;
                    let text = text.to_string_lossy().into_owned();
                    if text.starts_with('/') {
                        resolved.clear();
                    }
                    let mut parts: VecDeque<String> = text.split('/').filter(|s| !s.is_empty()).map(|s| s.to_string()).collect();
                    parts.append(&mut pending);
                    pending = parts;
                    continue;
                }
            }
            resolved.push(part);
        }
        resolved.last().cloned().unwrap_or_else(|| "/".to_string())
    }

    /// Proton's setup_dir_drive for `s:` (proton:330-345). Returns notes for launcher.log.
    pub fn apply(prefix: &Path, environment: &std::collections::BTreeMap<String, String>) -> Vec<String> {
        let mut notes: Vec<String> = Vec::new();
        // Proton works the target out whether or not the drive is on (proton:348), so its message is logged either way.
        let target = Self::target(environment, &mut notes);
        let on = environment
            .get("PROTON_SET_GAME_DRIVE")
            .map(|v| !v.is_empty() && v != "0")
            .unwrap_or(true);
        let link = prefix.join("dosdevices/s:");
        let exists = match posix::kind(&link) {
            posix::Kind::Missing => false,
            posix::Kind::Link => true,
            // Proton raises here with the drive on (readlink) and removes a regular file with it off (D-28); a player's
            // own file is never deleted.
            _ => {
                notes.push("game drive: dosdevices/s: is not a link, left alone".to_string());
                return notes;
            }
        };
        match (on, target) {
            (true, Some(target)) if !target.is_empty() => {
                if exists {
                    if let Ok(current) = std::fs::read_link(&link) {
                        if current.to_string_lossy() == target {
                            return notes;
                        }
                    }
                }
                if let Some(parent) = link.parent() {
                    let _ = std::fs::create_dir_all(parent);
                }
                if !posix::replace_link(&link, &target) {
                    notes.push(format!("game drive: couldn't link dosdevices/s: to {target}"));
                }
            }
            _ => {
                if exists {
                    let _ = std::fs::remove_file(&link);
                }
            }
        }
        notes
    }
}

fn is_writable(path: &Path) -> bool {
    use std::os::unix::ffi::OsStrExt;
    let Ok(cpath) = std::ffi::CString::new(path.as_os_str().as_bytes()) else { return false };
    unsafe { libc::access(cpath.as_ptr(), libc::W_OK) == 0 }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    #[test]
    fn python_name_follows_links_and_dots() {
        let dir = std::env::temp_dir().join(format!("mn-gd-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("real/steamapps")).unwrap();
        posix::symlink("real", &dir.join("link"));
        let name = GameDrive::python_name(&format!("{}/link/steamapps", dir.display()));
        assert_eq!(name, "steamapps");
        assert_eq!(GameDrive::python_name("/a/b/../c"), "c");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn off_removes_the_link() {
        let dir = std::env::temp_dir().join(format!("mn-gd2-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(dir.join("dosdevices")).unwrap();
        posix::symlink("/tmp/somewhere", &dir.join("dosdevices/s:"));
        let mut env = BTreeMap::new();
        env.insert("PROTON_SET_GAME_DRIVE".to_string(), "0".to_string());
        let _ = GameDrive::apply(&dir, &env);
        assert!(std::fs::symlink_metadata(dir.join("dosdevices/s:")).is_err());
        let _ = std::fs::remove_dir_all(&dir);
    }

    use crate::test_support::{make_temp_dir, write};
    use std::path::PathBuf;

    /// A Steam library `<tmp>/<name>/<steamapps>/common/Game`.
    fn make_library(name: &str, steamapps: &str) -> (PathBuf, PathBuf, PathBuf) {
        let lib = make_temp_dir().join(name);
        let apps = lib.join(steamapps);
        let game = apps.join("common/Game");
        std::fs::create_dir_all(&game).unwrap();
        (lib, apps, game)
    }

    fn default_library() -> (PathBuf, PathBuf, PathBuf) {
        make_library("Lib \u{dc}n\u{ef}", "steamapps")
    }

    fn p(path: &Path) -> String {
        let text = path.to_string_lossy().into_owned();
        if text.len() > 1 { text.trim_end_matches('/').to_string() } else { text }
    }

    fn env(libraries: &str, install: &str) -> BTreeMap<String, String> {
        BTreeMap::from([
            ("STEAM_COMPAT_LIBRARY_PATHS".to_string(), libraries.to_string()),
            ("STEAM_COMPAT_INSTALL_PATH".to_string(), install.to_string()),
        ])
    }

    fn real(path: &str) -> String {
        posix::real_path(Path::new(path)).unwrap()
    }

    /// `pfx/dosdevices/s:`'s target, or None when there is no link.
    fn drive_link(prefix: &Path) -> Option<String> {
        std::fs::read_link(prefix.join("dosdevices/s:")).ok().map(|t| t.to_string_lossy().into_owned())
    }

    fn target(environment: &BTreeMap<String, String>, notes: &mut Vec<String>) -> Option<String> {
        GameDrive::target(environment, notes)
    }

    #[test]
    fn the_librarys_parent_is_the_target() {
        let (lib, apps, game) = default_library();
        let other = make_temp_dir().join("Other/steamapps");
        let mut notes = vec![];
        let t = target(&env(&format!("{}:{}", p(&other), p(&apps)), &p(&game)), &mut notes);
        assert_eq!(t, Some(real(&p(&lib))));
        assert!(notes.is_empty());
    }

    #[test]
    fn the_first_substring_match_wins() {
        // Proton's `entry in install_path` (proton:319): "<root>/L" matches "<root>/L2/..." first, fails validation, and
        // Proton doesn't try the next entry.
        let root = make_temp_dir();
        let apps = root.join("L2/steamapps");
        let game = apps.join("common/Game");
        std::fs::create_dir_all(&game).unwrap();
        let mut notes = vec![];
        let first = p(&root.join("L"));
        assert_eq!(target(&env(&format!("{first}:{}", p(&apps)), &p(&game)), &mut notes), Some(first.clone()));
        assert_eq!(notes, vec![format!("Proton: Error: unable to use parent for game drive, path {first}")]);
    }

    #[test]
    fn an_empty_entry_matches_and_removes_the_drive() {
        // An empty piece matches every install path (proton:319); its fallback, "", removes s: (proton:333-335).
        let (_, apps, game) = default_library();
        let environment = env(&format!("::{}", p(&apps)), &p(&game));
        let mut notes = vec![];
        assert_eq!(target(&environment, &mut notes), Some(String::new()));
        assert_eq!(notes, vec!["Proton: Error: unable to use parent for game drive, path ".to_string()]);
        let prefix = make_temp_dir().join("pfx");
        GameDrive::apply(&prefix, &env(&p(&apps), &p(&game)));
        assert!(drive_link(&prefix).is_some());
        GameDrive::apply(&prefix, &environment);
        assert!(drive_link(&prefix).is_none());
    }

    #[test]
    fn a_legacy_steamapps_folder_named_steamapps_is_the_library() {
        // Mac Steam passes ".../steamapps" for a folder that may be "SteamApps" on disk (an old install). Python's
        // realpath, which Proton uses, keeps that spelling; Darwin's realpath(3) gives the on-disk case.
        let (lib, on_disk, _) = make_library("Lib \u{dc}n\u{ef}", "SteamApps");
        let apps = p(&on_disk).replace("/SteamApps", "/steamapps");
        let mut notes = vec![];
        let t = target(&env(&apps, &format!("{apps}/common/Game")), &mut notes);
        assert_eq!(t, Some(real(&p(&lib))));
        assert!(notes.is_empty());
    }

    #[test]
    fn a_library_link_follows_its_text_as_pythons_realpath_does() {
        // Python's realpath resolves a link by its text: "<other>/steamapps" -> "<lib>/steamapps" passes the check even
        // when the folder is "SteamApps" on disk, where realpath(3) gives "SteamApps".
        let (lib, _, _) = make_library("Lib \u{dc}n\u{ef}", "SteamApps");
        let link = make_temp_dir().join("steamapps");
        posix::symlink(&format!("{}/steamapps", p(&lib)), &link);
        let mut notes = vec![];
        let l = p(&link);
        assert_eq!(target(&env(&l, &format!("{l}/common/Game")), &mut notes), Some(real(&p(&lib))));
        assert!(notes.is_empty());

        // A link spelled "SteamApps" fails it, as under Proton.
        let wrong = make_temp_dir().join("steamapps");
        posix::symlink(&format!("{}/SteamApps", p(&lib)), &wrong);
        notes.clear();
        let w = p(&wrong);
        assert_eq!(target(&env(&w, &format!("{w}/common/Game")), &mut notes), Some(w.clone()));
        assert_eq!(notes.len(), 1);

        // Hop by hop (a relative link to that link), a trailing slash, and a link text ending in "/.", which Python drops.
        let hops = make_temp_dir();
        posix::symlink("hop", &hops.join("steamapps"));
        posix::symlink(&l, &hops.join("hop"));
        let dot = make_temp_dir().join("steamapps");
        posix::symlink(&format!("{}/steamapps/.", p(&lib)), &dot);
        for entry in [format!("{}/steamapps", p(&hops)), format!("{l}/"), p(&dot)] {
            notes.clear();
            let t = target(&env(&entry, &format!("{entry}/common/Game")), &mut notes);
            assert_eq!(t, Some(real(&p(&lib))), "{entry}");
            assert!(notes.is_empty(), "{entry}");
        }
    }

    #[test]
    fn a_library_that_fails_validation_gets_protons_fallback() {
        // The basename is checked as written, after realpath: "SteamApps" isn't "steamapps".
        let (_, wrong_case, wrong_case_game) = make_library("Lib \u{dc}n\u{ef}", "SteamApps");
        let mut notes = vec![];
        assert_eq!(target(&env(&p(&wrong_case), &p(&wrong_case_game)), &mut notes), Some(p(&wrong_case)));
        assert_eq!(notes.len(), 1);

        // A parent the player can't write.
        let (lib, apps, game) = default_library();
        posix::chmod(&lib, 0o555);
        notes.clear();
        let t = target(&env(&p(&apps), &p(&game)), &mut notes);
        posix::chmod(&lib, 0o755);
        assert_eq!(t, Some(p(&apps)));
        assert_eq!(notes.len(), 1);

        // steamapps on another volume than its parent.
        let (_, mounted, mounted_game) = default_library();
        notes.clear();
        let t = GameDrive::target_with(
            &env(&p(&mounted), &p(&mounted_game)),
            |path| Some(if path.to_string_lossy().ends_with("/steamapps") { 1 } else { 2 }),
            &mut notes,
        );
        assert_eq!(t, Some(p(&mounted)));
        assert_eq!(notes.len(), 1);
    }

    #[test]
    fn no_match_or_missing_variables_means_no_drive() {
        let (_, apps, game) = default_library();
        let mut notes = vec![];
        assert_eq!(target(&env("/nowhere/steamapps", &p(&game)), &mut notes), None);
        let mut only_libraries = env(&p(&apps), "");
        only_libraries.remove("STEAM_COMPAT_INSTALL_PATH");
        assert_eq!(target(&only_libraries, &mut notes), None);
        let mut only_install = env("", &p(&game));
        only_install.remove("STEAM_COMPAT_LIBRARY_PATHS");
        assert_eq!(target(&only_install, &mut notes), None);
        assert!(notes.is_empty());
    }

    #[test]
    fn apply_links_retargets_and_removes() {
        let prefix = make_temp_dir().join("pfx");
        let (lib, apps, game) = default_library();
        let first = env(&p(&apps), &p(&game));
        GameDrive::apply(&prefix, &first);
        assert_eq!(drive_link(&prefix), Some(real(&p(&lib))));

        let (lib2, apps2, game2) = make_library("Second", "steamapps");
        GameDrive::apply(&prefix, &env(&p(&apps2), &p(&game2)));
        assert_eq!(drive_link(&prefix), Some(real(&p(&lib2))));

        // PROTON_SET_GAME_DRIVE: "0" and "" turn it off; anything else, "false" included, leaves it on (proton:57-58).
        let with = |value: &str| {
            let mut e = first.clone();
            e.insert("PROTON_SET_GAME_DRIVE".into(), value.into());
            e
        };
        for value in ["0", ""] {
            GameDrive::apply(&prefix, &first);
            GameDrive::apply(&prefix, &with(value));
            assert_eq!(drive_link(&prefix), None);
        }
        GameDrive::apply(&prefix, &with("false"));
        assert_eq!(drive_link(&prefix), Some(real(&p(&lib))));

        GameDrive::apply(&prefix, &env("/nowhere/steamapps", &p(&game)));
        assert_eq!(drive_link(&prefix), None);
    }

    #[test]
    fn a_non_link_at_s_is_left_alone() {
        let prefix = make_temp_dir().join("pfx");
        let file = prefix.join("dosdevices/s:");
        write("the player's", &file, false);
        let (_, apps, game) = default_library();
        let notes = GameDrive::apply(&prefix, &env(&p(&apps), &p(&game)));
        assert_eq!(std::fs::read_to_string(&file).unwrap(), "the player's");
        assert_eq!(notes, vec!["game drive: dosdevices/s: is not a link, left alone".to_string()]);
    }
}
