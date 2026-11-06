//! The player's data a fresh prefix takes from the Rosetta-era prefix it replaced (renamed `pfx.rosetta...`): games
//! keep settings and local saves in the user's folders and in `HKCU\Software\<Vendor>`. Reads the old prefix only.
//! Ported from `PlayerData.swift`.

use std::collections::HashSet;
use std::path::Path;

use crate::posix;

/// What a carry did: files cloned, registry sections appended, and items left behind (one that couldn't be read or
/// copied, a link, or what's under a link in the way).
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct Carried {
    pub files: usize,
    pub keys: usize,
    pub failed: usize,
}

pub struct PlayerData;

impl PlayerData {
    /// Per user folder: what is copied, and Wine's own folders inside it, which aren't.
    const FOLDERS: [&'static str; 5] = [
        "AppData/Local",
        "AppData/LocalLow",
        "AppData/Roaming",
        "Documents",
        "Saved Games",
    ];

    /// Wine's, Windows' or Steam's vendors under `HKCU\Software`, not a game's.
    const SYSTEM_VENDORS: [&'static str; 5] = ["wine", "microsoft", "classes", "policies", "valve"];

    /// The name each file is copied under, in its folder, before it is renamed into place: a carry killed mid-copy (a
    /// byte copy, off APFS) leaves its part there, never under the real name, and the retry's copy replaces it.
    pub const TEMP_NAME: &'static str = ".macneutron-carry";

    /// Appends the game-owned `user.reg` sections the new prefix has no section of, then clones the user folders'
    /// files it doesn't have yet. A bad item is skipped and counted, never ends the carry. Call only while no wineserver
    /// runs: it reads `user.reg` when it starts and rewrites it.
    pub fn carry(old: &Path, new: &Path) -> Carried {
        let mut result = Carried::default();
        match Self::carry_registry(&old.join("user.reg"), &new.join("user.reg")) {
            Ok(keys) => result.keys = keys,
            Err(_) => result.failed += 1,
        }
        let (old_users, skipped_users) = Self::users(old);
        let new_users = Self::users(new).0;
        result.failed += skipped_users;
        // wineboot names the one user after $USER, and Wine reads only that one: every old user goes there, whatever
        // the names. The old user of that name (the one Wine read) goes first, then by name: the first wins a file two
        // have.
        // Steam's own `steamuser` (its cloud files, handed back beside the new profile) is never the one Wine reads.
        let profiles: Vec<String> = if new_users.len() > 1 {
            new_users.iter().filter(|u| !u.eq_ignore_ascii_case("steamuser")).cloned().collect()
        } else {
            new_users.clone()
        };
        let live: Option<&String> = if profiles.len() == 1 { profiles.first() } else { None };
        let mut ordered: Vec<String> = Vec::new();
        ordered.extend(old_users.iter().filter(|u| Some(*u) == live).cloned());
        ordered.extend(old_users.iter().filter(|u| Some(*u) != live).cloned());
        for user in ordered {
            let target = live.cloned().unwrap_or_else(|| user.clone());
            for folder in Self::FOLDERS {
                let rel = format!("drive_c/users/{user}/{folder}");
                match Self::kind(old, &rel) {
                    Kind::Missing => continue,
                    // Wine links Documents (and the like) to the Mac's own folder: that data is on the Mac already.
                    Kind::Other => {
                        if !Self::is_link(old, &rel) {
                            result.failed += 1;
                        }
                    }
                    Kind::Directory => {
                        let destination = format!("drive_c/users/{target}/{folder}");
                        if !Self::make_directory(new, &destination) {
                            result.failed += 1;
                            continue;
                        }
                        Self::copy_tree(&old.join(&rel), &new.join(&destination), folder, &mut result);
                    }
                }
            }
        }
        result
    }

    /// The real folders in `drive_c/users`, `Public` aside, and how many entries there aren't (links, unreadable).
    fn users(prefix: &Path) -> (Vec<String>, usize) {
        let dir = prefix.join("drive_c/users");
        match Self::kind(prefix, "drive_c/users") {
            Kind::Missing => return (Vec::new(), 0),
            Kind::Other => return (Vec::new(), 1),
            Kind::Directory => {}
        }
        let Ok(entries) = std::fs::read_dir(&dir) else { return (Vec::new(), 1) };
        let names: Vec<String> = entries
            .flatten()
            .filter_map(|e| e.file_name().into_string().ok())
            .filter(|n| n != "Public")
            .collect();
        let mut real: Vec<String> = names
            .iter()
            .filter(|n| Self::kind(&dir, n) == Kind::Directory)
            .cloned()
            .collect();
        real.sort();
        let skipped = names.len() - real.len();
        (real, skipped)
    }

    /// What `base/relative` is, looked at with lstat component by component: a link anywhere on the way is `Other`.
    /// Wine links `Documents` and others to the Mac's folders, which are never read through (the data is already
    /// there) nor written through.
    fn kind(base: &Path, relative: &str) -> Kind {
        if relative.is_empty() {
            return Kind::Directory;
        }
        let mut url = base.to_path_buf();
        for part in relative.split('/') {
            url.push(part);
            match std::fs::symlink_metadata(&url) {
                Ok(meta) => {
                    if !meta.is_dir() {
                        return Kind::Other;
                    }
                }
                Err(err) => {
                    return if err.raw_os_error() == Some(libc::ENOENT) { Kind::Missing } else { Kind::Other };
                }
            }
        }
        Kind::Directory
    }

    /// True when `base/relative` itself is a symbolic link and every folder above it is real.
    fn is_link(base: &Path, relative: &str) -> bool {
        let parent = match relative.rfind('/') {
            Some(i) => &relative[..i],
            None => "",
        };
        if !parent.is_empty() && Self::kind(base, parent) != Kind::Directory {
            return false;
        }
        let Ok(meta) = std::fs::symlink_metadata(base.join(relative)) else { return false };
        meta.file_type().is_symlink()
    }

    /// True when `base/relative` is a real directory now, made where missing; false when a component is a link or a
    /// file, which is never followed or replaced.
    fn make_directory(base: &Path, relative: &str) -> bool {
        let mut url = base.to_path_buf();
        for part in relative.split('/') {
            url.push(part);
            match std::fs::symlink_metadata(&url) {
                Ok(meta) => {
                    if !meta.is_dir() {
                        return false;
                    }
                }
                Err(_) => {
                    if std::fs::create_dir(&url).is_err() {
                        return false;
                    }
                }
            }
        }
        true
    }

    /// Clones every regular file under `source` that `destination` lacks (both real directories) and counts.
    fn copy_tree(source: &Path, destination: &Path, relative: &str, result: &mut Carried) {
        let Ok(entries) = std::fs::read_dir(source) else {
            result.failed += 1;
            return;
        };
        let mut names: Vec<String> = entries.flatten().filter_map(|e| e.file_name().into_string().ok()).collect();
        names.sort();
        for name in names {
            let from = source.join(&name);
            let to = destination.join(&name);
            let path = format!("{relative}/{name}");
            let Ok(meta) = std::fs::symlink_metadata(&from) else {
                result.failed += 1; // vanished
                continue;
            };
            if meta.is_dir() {
                if Self::is_skipped(&path) {
                    continue;
                }
                if !Self::make_directory(destination, &name) {
                    result.failed += 1;
                    continue;
                }
                Self::copy_tree(&from, &to, &path, result);
            } else if meta.is_file() {
                // Never overwritten.
                if std::fs::symlink_metadata(&to).is_ok() {
                    continue;
                }
                let temp = destination.join(Self::TEMP_NAME);
                let _ = std::fs::remove_file(&temp); // a killed carry's part-copy
                // COPYFILE_CLONE: an APFS clone, else a copy; it includes COPYFILE_EXCL. RENAME_EXCL never replaces a
                // file; a volume without it (ENOTSUP) gets a plain rename: under the prefix lock with no wineserver,
                // nothing else writes there since the lstat above.
                let cloned = posix::clone_file(&from, &temp);
                let renamed = cloned && (posix::rename_excl(&temp, &to) || posix::rename(&temp, &to));
                if !renamed {
                    let _ = std::fs::remove_file(&temp);
                    result.failed += 1;
                    continue;
                }
                result.files += 1;
            } else {
                result.failed += 1; // a link, never followed, or a special file
            }
        }
    }

    /// Wine's own folders inside a copied tree, which aren't the game's.
    fn is_skipped(path: &str) -> bool {
        matches!(
            path,
            "AppData/Local/Temp" | "AppData/Local/Microsoft" | "AppData/Roaming/Microsoft"
        )
    }

    /// Appends the old `user.reg`'s sections under `Software\<Vendor>` (a game's vendor) that the new one has no
    /// section of, verbatim; returns how many. The file is replaced atomically.
    fn carry_registry(old: &Path, new: &Path) -> std::io::Result<usize> {
        if !old.exists() {
            return Ok(0);
        }
        let old_text = std::fs::read_to_string(old)?;
        let mut new_text = std::fs::read_to_string(new)?;
        let taken: HashSet<String> = Self::sections(&new_text).into_iter().map(|s| s.name.to_lowercase()).collect();
        let carried: Vec<Section> = Self::sections(&old_text)
            .into_iter()
            .filter(|section| {
                let path: Vec<&str> = section.name.split("\\\\").collect();
                path.len() > 1
                    && path[0].eq_ignore_ascii_case("software")
                    && !Self::SYSTEM_VENDORS.iter().any(|v| v.eq_ignore_ascii_case(path[1]))
                    && !taken.contains(&section.name.to_lowercase())
            })
            .collect();
        if carried.is_empty() {
            return Ok(0);
        }
        if !new_text.ends_with('\n') {
            new_text.push('\n');
        }
        for section in &carried {
            new_text.push('\n');
            new_text.push_str(&section.text);
            new_text.push('\n');
        }
        let temp = new.with_extension("reg.tmp");
        std::fs::write(&temp, new_text.as_bytes())?;
        std::fs::rename(&temp, new)?;
        Ok(carried.len())
    }

    /// A Wine registry file's sections: each `[name] time` line with the lines up to the next one, trailing blank
    /// lines dropped. Values never start a line with `[` (they start with `"` or `@`, continuations with spaces).
    fn sections(text: &str) -> Vec<Section> {
        let mut result: Vec<(String, Vec<&str>)> = Vec::new();
        for line in text.split('\n') {
            if let Some(rest) = line.strip_prefix('[') {
                if let Some(close) = rest.rfind(']') {
                    result.push((rest[..close].to_string(), vec![line]));
                    continue;
                }
            }
            if let Some(last) = result.last_mut() {
                last.1.push(line);
            }
        }
        result
            .into_iter()
            .map(|(name, mut lines)| {
                while lines.last().map(|l| l.trim().is_empty()).unwrap_or(false) {
                    lines.pop();
                }
                (name, lines.join("\n"))
            })
            .map(|(name, text)| Section { name, text })
            .collect()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct Section {
    name: String,
    text: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Kind {
    Directory,
    Missing,
    Other,
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    struct Fixture {
        dir: PathBuf,
    }

    impl Fixture {
        fn new(name: &str) -> Self {
            let dir = std::env::temp_dir().join(format!("mn-pd-{}-{}", std::process::id(), name));
            let _ = std::fs::remove_dir_all(&dir);
            std::fs::create_dir_all(&dir).unwrap();
            Fixture { dir }
        }

        fn write(&self, rel: &str, content: &str) {
            let path = self.dir.join(rel);
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent).unwrap();
            }
            std::fs::write(path, content).unwrap();
        }

        fn mkdir(&self, rel: &str) {
            std::fs::create_dir_all(self.dir.join(rel)).unwrap();
        }

        fn symlink(&self, rel: &str, target: &str) {
            let path = self.dir.join(rel);
            if let Some(parent) = path.parent() {
                std::fs::create_dir_all(parent).unwrap();
            }
            posix::symlink(target, &path);
        }

        fn file(&self, rel: &str) -> Option<String> {
            std::fs::read_to_string(self.dir.join(rel)).ok()
        }

        fn exists(&self, rel: &str) -> bool {
            std::fs::symlink_metadata(self.dir.join(rel)).is_ok()
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.dir);
        }
    }

    #[test]
    fn carries_files_and_registry_keys_once() {
        let old = Fixture::new("carry-old");
        let new = Fixture::new("carry-new");
        old.write("drive_c/users/steamuser/AppData/Local/Game/save.sav", "save");
        old.write(
            "user.reg",
            "WINE REGISTRY Version 2\n[Software\\\\Epic Games\\\\Unreal] 1\n\"AccountId\"=\"abc\"\n",
        );
        new.write("drive_c/users/steamuser/.keep", "");
        new.write("user.reg", "WINE REGISTRY Version 2\n");
        let carried = PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(carried.files, 1);
        assert_eq!(carried.keys, 1);
        assert_eq!(new.file("drive_c/users/steamuser/AppData/Local/Game/save.sav").as_deref(), Some("save"));
        assert!(new.file("user.reg").unwrap().contains("Epic Games"));
    }

    #[test]
    fn never_overwrites_and_skips_wines_own_folders() {
        let old = Fixture::new("nowover-old");
        let new = Fixture::new("nowover-new");
        old.write("drive_c/users/steamuser/AppData/Roaming/Game/a.cfg", "old");
        old.write("drive_c/users/steamuser/AppData/Roaming/Game/b.cfg", "old");
        old.write("drive_c/users/steamuser/AppData/Local/Temp/junk.tmp", "junk");
        old.write("drive_c/users/steamuser/AppData/Local/Microsoft/Windows/x.dat", "x");
        old.write("drive_c/users/steamuser/AppData/Roaming/Microsoft/Crypto/y.dat", "y");
        new.write("drive_c/users/steamuser/AppData/Roaming/Game/a.cfg", "fresh");
        let carried = PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(new.file("drive_c/users/steamuser/AppData/Roaming/Game/a.cfg").as_deref(), Some("fresh"));
        assert_eq!(new.file("drive_c/users/steamuser/AppData/Roaming/Game/b.cfg").as_deref(), Some("old"));
        assert!(!new.exists("drive_c/users/steamuser/AppData/Local/Temp/junk.tmp"));
        assert!(!new.exists("drive_c/users/steamuser/AppData/Local/Microsoft/Windows/x.dat"));
        assert!(!new.exists("drive_c/users/steamuser/AppData/Roaming/Microsoft/Crypto/y.dat"));
        assert_eq!(carried.files, 1);
    }

    #[test]
    fn maps_the_old_user_onto_the_new_one() {
        let old = Fixture::new("map-old");
        let new = Fixture::new("map-new");
        old.write("drive_c/users/steamuser/AppData/Local/Game/Saved/save.sav", "save");
        new.mkdir("drive_c/users/player");
        PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(new.file("drive_c/users/player/AppData/Local/Game/Saved/save.sav").as_deref(), Some("save"));
        assert!(!new.exists("drive_c/users/steamuser/AppData/Local/Game/Saved/save.sav"));
    }

    #[test]
    fn follows_no_links() {
        let old = Fixture::new("nolinks-old");
        let new = Fixture::new("nolinks-new");
        let mac = Fixture::new("nolinks-mac");
        mac.write("old/notes.txt", "mac");
        mac.mkdir("new");
        old.write("drive_c/users/steamuser/Saved Games/real.sav", "real");
        old.mkdir("drive_c/users/steamuser/Documents");
        old.symlink("drive_c/users/steamuser/Saved Games/linked", &mac.dir.join("old").to_string_lossy());
        new.mkdir("drive_c/users/steamuser");
        // Wine's new Documents is a link to the Mac's folder.
        new.symlink("drive_c/users/steamuser/Documents", &mac.dir.join("new").to_string_lossy());
        let carried = PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(new.file("drive_c/users/steamuser/Saved Games/real.sav").as_deref(), Some("real"));
        assert!(!new.exists("drive_c/users/steamuser/Saved Games/linked/notes.txt"));
        // Nothing was written through the Mac's Documents link either.
        let entries: Vec<_> = std::fs::read_dir(mac.dir.join("new")).unwrap().flatten().collect();
        assert!(entries.is_empty());
        assert!(carried.failed >= 1);
    }

    #[test]
    fn unreadable_registry_is_counted_and_the_files_still_carry() {
        let old = Fixture::new("badreg-old");
        let new = Fixture::new("badreg-new");
        old.write("drive_c/users/steamuser/AppData/Local/Game/save.sav", "save");
        // An old user.reg that can't be read (it's a directory).
        old.mkdir("user.reg");
        new.write("drive_c/users/steamuser/.keep", "");
        new.write("user.reg", "WINE REGISTRY Version 2\n");
        let carried = PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(carried.files, 1);
        assert_eq!(carried.keys, 0);
        assert_eq!(carried.failed, 1);
    }

    #[test]
    fn system_vendors_are_not_carried() {
        let old = Fixture::new("vendor-old");
        let new = Fixture::new("vendor-new");
        old.write(
            "user.reg",
            "WINE REGISTRY Version 2\n[Software\\\\Valve\\\\Steam] 1\n\"x\"=\"1\"\n[Software\\\\Epic] 1\n\"y\"=\"2\"\n",
        );
        new.write("user.reg", "WINE REGISTRY Version 2\n");
        let carried = PlayerData::carry(&old.dir, &new.dir);
        assert_eq!(carried.keys, 1);
        let text = new.file("user.reg").unwrap();
        assert!(text.contains("Epic"));
        assert!(!text.contains("Valve"));
    }
}
