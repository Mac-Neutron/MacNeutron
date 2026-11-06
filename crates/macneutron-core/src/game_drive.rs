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
            path.split('/').map(|s| s.to_string()).collect();
        let mut hops = 0;
        while let Some(part) = pending.pop_front() {
            if part == "." {
                continue;
            }
            if part == ".." {
                resolved.pop();
                continue;
            }
            let candidate = format!("/{}/{}", resolved.join("/"), part);
            if hops < 40 {
                if let Ok(text) = std::fs::read_link(&candidate) {
                    hops += 1;
                    let text = text.to_string_lossy().into_owned();
                    if text.starts_with('/') {
                        resolved.clear();
                    }
                    let mut parts: VecDeque<String> = text.split('/').map(|s| s.to_string()).collect();
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
}
