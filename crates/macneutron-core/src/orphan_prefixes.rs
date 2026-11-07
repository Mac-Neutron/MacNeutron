//! A game's Wine prefix left behind after the game was uninstalled (Steam doesn't delete it on macOS). Ported from
//! `OrphanPrefixes.swift`.

use std::path::{Path, PathBuf};

use crate::steam_location::SteamLocation;

/// A leftover prefix: its app ID, its folder, and how much it holds.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OrphanPrefix {
    pub app_id: String,
    pub url: PathBuf,
    pub bytes: i64,
}

pub struct OrphanPrefixes;

impl OrphanPrefixes {
    /// The prefixes of uninstalled games: a numeric `compatdata` folder whose app ID has no `appmanifest_*.acf` in any
    /// library. Non-Steam shortcuts (IDs with the high bit set) never have an appmanifest and are skipped.
    pub fn find(steam: &SteamLocation) -> Vec<OrphanPrefix> {
        let installed = steam.installed_app_ids();
        let mut result: Vec<OrphanPrefix> = Vec::new();
        for library in steam.libraries() {
            let compatdata = library.join("compatdata");
            let Ok(entries) = std::fs::read_dir(&compatdata) else { continue };
            let mut names: Vec<String> = entries
                .flatten()
                .filter_map(|e| e.file_name().into_string().ok())
                .collect();
            names.sort();
            for name in names {
                // Non-Steam shortcuts (IDs with the high bit set) never have an appmanifest.
                let Ok(id) = name.parse::<u32>() else { continue };
                if id == 0 || id >= 0x8000_0000 || installed.contains(&id) {
                    continue;
                }
                let url = compatdata.join(&name);
                result.push(OrphanPrefix {
                    app_id: name,
                    bytes: Self::size(&url),
                    url,
                });
            }
        }
        result
    }

    /// Removes every prefix, stopping at the first failure.
    pub fn delete(prefixes: &[OrphanPrefix]) -> std::io::Result<()> {
        for prefix in prefixes {
            std::fs::remove_dir_all(&prefix.url)?;
        }
        Ok(())
    }

    /// Allocated bytes of the regular files under `folder`, the same measure Finder shows.
    pub fn size(folder: &Path) -> i64 {
        let mut total: i64 = 0;
        collect_size(folder, &mut total);
        total
    }
}

fn collect_size(folder: &Path, total: &mut i64) {
    let Ok(entries) = std::fs::read_dir(folder) else { return };
    for entry in entries.flatten() {
        let Ok(meta) = entry.metadata() else { continue };
        if meta.is_dir() {
            collect_size(&entry.path(), total);
        } else if meta.is_file() {
            use std::os::unix::fs::MetadataExt;
            *total += meta.blocks() as i64 * 512;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fake_steam(name: &str) -> (SteamLocation, PathBuf) {
        let root = std::env::temp_dir().join(format!("mn-orphan-{}-{}", std::process::id(), name));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(root.join("steamapps")).unwrap();
        let steam = SteamLocation::new(root.clone());
        (steam, root)
    }

    #[test]
    fn lists_only_prefixes_of_uninstalled_games() {
        let (steam, root) = fake_steam("list");
        let compatdata = root.join("steamapps/compatdata");
        std::fs::create_dir_all(compatdata.join("1062090/pfx")).unwrap();
        std::fs::write(compatdata.join("1062090/pfx/system.reg"), b"installed").unwrap();
        std::fs::write(root.join("steamapps/appmanifest_1062090.acf"), b"manifest").unwrap();
        std::fs::create_dir_all(compatdata.join("2977660/pfx/drive_c")).unwrap();
        std::fs::write(compatdata.join("2977660/pfx/drive_c/big.bin"), vec![b'x'; 10_000]).unwrap();
        std::fs::create_dir_all(compatdata.join("0/pfx")).unwrap();
        std::fs::write(compatdata.join("0/pfx/x"), b"shared").unwrap();

        let orphans = OrphanPrefixes::find(&steam);
        assert_eq!(orphans.iter().map(|o| o.app_id.clone()).collect::<Vec<_>>(), vec!["2977660"]);
        assert!(orphans[0].bytes >= 10_000);
        OrphanPrefixes::delete(&orphans).unwrap();
        assert!(OrphanPrefixes::find(&steam).is_empty());
        assert!(compatdata.join("1062090").exists());
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn non_steam_shortcuts_are_never_listed() {
        // Shortcut IDs have the high bit set and never get an appmanifest; their prefix holds real games and saves.
        let (steam, root) = fake_steam("shortcut");
        let compatdata = root.join("steamapps/compatdata");
        std::fs::create_dir_all(compatdata.join("3141592653/pfx/drive_c")).unwrap();
        std::fs::write(compatdata.join("3141592653/pfx/drive_c/save.dat"), b"save").unwrap();
        assert!(OrphanPrefixes::find(&steam).is_empty());
        let _ = std::fs::remove_dir_all(&root);
    }
}
