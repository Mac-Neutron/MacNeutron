//! The links Steam's Auto-Cloud roots resolve through (spec 4.2, after Proton's `migrate_user_paths`). Steam names
//! the profile `steamuser` and its folders by their XP names; Wine names it after `$USER` with Vista names.

use std::path::{Path, PathBuf};

use crate::posix;

/// What `ProfileLinks.apply` did: notes for launcher.log, and how many links it left as real folders because files in
/// them differ from the profile's (spec D2).
#[derive(Debug, Default, PartialEq, Eq)]
pub struct ProfileLinkReport {
    pub notes: Vec<String>,
    pub conflicts: usize,
}

pub struct ProfileLinks;

/// Inside the profile: (path, relative target).
const XP_LINKS: [(&str, &str); 3] = [
    ("Local Settings/Application Data", "../AppData/Local"),
    ("Application Data", "AppData/Roaming"),
    ("My Documents", "Documents"),
];

/// The folders Steam's roots and Wine's profile name, above any game's own.
const ROOT_FOLDERS: &[&str] = &[
    "Local Settings",
    "Application Data",
    "AppData",
    "Local",
    "Roaming",
    "LocalLow",
    "My Documents",
    "Documents",
    "Saved Games",
];

impl ProfileLinks {
    /// Makes the links in `prefix`, the profile's own first, so a merge of `steamuser` lands in the Vista folders. A real
    /// folder where a link goes (Steam made it) becomes the link only when `merge`, the volume can swap, and every file
    /// in it is then present and equal at the link's target; otherwise it stays as it is. Nothing is ever deleted.
    pub fn apply(prefix: &Path, user: &str, merge: bool) -> ProfileLinkReport {
        Self::apply_with(prefix, user, merge, is_apfs)
    }

    /// As `apply`, with the volume probe injectable so tests needn't be on APFS.
    pub fn apply_with(
        prefix: &Path,
        user: &str,
        merge: bool,
        can_swap: impl Fn(&Path) -> bool,
    ) -> ProfileLinkReport {
        let mut report = ProfileLinkReport::default();
        let users = prefix.join("drive_c/users");
        let profile = users.join(user);
        if posix::kind(&profile) != posix::Kind::Directory {
            report.notes.push(format!("no profile folder users/{user}, so no Steam Cloud links"));
            return report;
        }
        let mut links: Vec<(PathBuf, String, String)> = XP_LINKS
            .iter()
            .map(|(path, target)| (profile.join(path), format!("{user}/{path}"), (*target).to_string()))
            .collect();
        // APFS compares names without case: for the Mac user "SteamUser", users/steamuser is the profile itself.
        if !user.eq_ignore_ascii_case("steamuser") {
            links.push((users.join("steamuser"), "steamuser".to_string(), user.to_string()));
        }
        for (path, name, target) in links {
            match posix::kind(&path) {
                posix::Kind::Missing => {
                    if let Some(parent) = path.parent() {
                        let _ = std::fs::create_dir_all(parent);
                    }
                    if !posix::symlink(&target, &path) {
                        report.notes.push(format!("couldn't link {name}"));
                    }
                }
                posix::Kind::Link => {
                    // Steam's watched files resolve through the link: only a dangling one is replaced. One reaching
                    // another folder (the Mac's short name changed, say) is left, or those files would vanish.
                    match posix::real_path(&path) {
                        None => {
                            if !replace_link(&path, &target) {
                                report.notes.push(format!("couldn't relink {name}"));
                            }
                        }
                        Some(current) => {
                            let expected = path.parent().map(|p| p.join(&target));
                            let expected = expected.as_deref().and_then(posix::real_path);
                            if expected != Some(current) {
                                report.conflicts += 1;
                                report.notes.push(format!(
                                    "left {name}: it links to another folder, so Steam Cloud won't sync this game here yet"
                                ));
                            }
                        }
                    }
                }
                posix::Kind::Directory if !merge => {
                    report.notes.push(format!("left {name}: Steam's files there merge at the game's next launch"));
                }
                posix::Kind::Directory if !can_swap(prefix) => {
                    report.notes.push(format!("left {name}: the volume isn't APFS, so its files can't be merged safely"));
                }
                posix::Kind::Directory => merge_and_swap(&path, &name, &target, &mut report),
                posix::Kind::File | posix::Kind::Other => {
                    report.notes.push(format!("left {name}: it isn't a folder"));
                }
            }
        }
        report
    }

    /// A new link renamed over the old one, so the path is never missing.
    pub fn replace_link(link: &Path, target: &str) -> bool {
        posix::replace_link(link, target)
    }

    /// Clones and `RENAME_SWAP` need APFS.
    pub fn is_apfs(url: &Path) -> bool {
        posix::filesystem_type(url).as_deref() == Some("apfs")
    }

    /// A logged path stops at the game's folder (spec 4.2): below it, save paths hold SteamIDs, account IDs and persona
    /// names, and launcher.log ends up in bug reports.
    pub fn redacted(path: &str) -> String {
        let parts: Vec<&str> = path.split('/').collect();
        let roots = parts.iter().take_while(|p| ROOT_FOLDERS.contains(*p)).count();
        let game: Vec<&str> = if roots < parts.len() - 1 { vec![parts[roots]] } else { vec![] };
        parts[..roots]
            .iter()
            .copied()
            .chain(game)
            .chain(std::iter::once("\u{2026}"))
            .collect::<Vec<_>>()
            .join("/")
    }
}

fn is_apfs(url: &Path) -> bool {
    ProfileLinks::is_apfs(url)
}

fn replace_link(link: &Path, target: &str) -> bool {
    ProfileLinks::replace_link(link, target)
}

/// Clones `folder`'s files into the link's target, checks every one arrived equal, then swaps the link in, the folder
/// becoming `<name> BACKUP`: one atomic step, so every path Steam watches holds the same bytes throughout.
fn merge_and_swap(folder: &Path, name: &str, target: &str, report: &mut ProfileLinkReport) {
    let parent = match folder.parent() {
        Some(p) => p,
        None => return,
    };
    let destination = parent.join(target); // through the target's links
    if let (Ok(here), Ok(there)) = (
        std::fs::metadata(folder),
        std::fs::metadata(&destination),
    ) {
        use std::os::unix::fs::MetadataExt;
        if here.dev() == there.dev() && here.ino() == there.ino() {
            report.notes.push(format!("left {name}: it is already {target}"));
            return;
        }
    }
    let files = regular_files(folder);
    let before = compare(&files, folder, &destination);
    if !before.differing.is_empty() {
        report.conflicts += 1;
        report.notes.push(format!(
            "kept {name}: {} of its files differ from the profile's (e.g. {}), so Steam Cloud won't sync them yet",
            before.differing.len(),
            ProfileLinks::redacted(&before.differing[0])
        ));
        return;
    }
    for path in &before.missing {
        let to = destination.join(path);
        if let Some(parent) = to.parent() {
            if std::fs::create_dir_all(parent).is_err() {
                report.notes.push(format!("left {name}: couldn't copy {}", ProfileLinks::redacted(path)));
                return;
            }
        }
        // ProfileLinks.swift:86 `copyItem`: an APFS clone, modification time kept.
        let from = folder.join(path);
        let copied = std::fs::copy(&from, &to).and_then(|_| {
            let modified = std::fs::metadata(&from)?.modified()?;
            std::fs::File::options().write(true).open(&to)?.set_modified(modified)
        });
        if copied.is_err() {
            report.notes.push(format!("left {name}: couldn't copy {}", ProfileLinks::redacted(path)));
            return;
        }
    }
    let after = compare(&files, folder, &destination);
    if !after.missing.is_empty() || !after.differing.is_empty() {
        report.notes.push(format!("left {name}: its files didn't all arrive in the profile"));
        return;
    }
    let path = folder.to_string_lossy().into_owned();
    let mut backup = format!("{path} BACKUP");
    let mut number = 2;
    while std::fs::symlink_metadata(&backup).is_ok() {
        backup = format!("{path} BACKUP {number}");
        number += 1;
    }
    if !posix::symlink(target, Path::new(&backup)) {
        report.notes.push(format!("left {name}: couldn't make its link"));
        return;
    }
    if !posix::rename_swap(Path::new(&backup), folder) {
        let _ = std::fs::remove_file(&backup);
        report.notes.push(format!("left {name}: couldn't swap in its link"));
        return;
    }
    let backup_name = Path::new(&backup)
        .file_name()
        .map(|s| s.to_string_lossy().into_owned())
        .unwrap_or_default();
    report.notes.push(format!(
        "merged {name} into {target} ({} files); the original is now {backup_name}",
        files.len()
    ));
}

/// Relative paths of the regular files under `folder`. Links in it are skipped, never followed, and so are
/// Finder's own files (`.DS_Store`, `._*`): no game saves them, and they would differ in every tree Finder opened.
fn regular_files(folder: &Path) -> Vec<String> {
    let mut result: Vec<String> = Vec::new();
    collect_files(folder, folder, &mut result);
    result.sort();
    result
}

fn collect_files(root: &Path, dir: &Path, result: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        match posix::kind(&path) {
            posix::Kind::Directory => {
                collect_files(root, &path, result); // ProfileLinks.swift:120 enumerates the whole tree
                continue;
            }
            posix::Kind::File => {}
            _ => continue,
        }
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name == ".DS_Store" || name.starts_with("._") {
            continue;
        }
        if let Ok(rel) = path.strip_prefix(root) {
            result.push(rel.to_string_lossy().into_owned());
        }
    }
}

/// The result of comparing a folder's files against the destination: what is absent, and what differs.
struct Diff {
    missing: Vec<String>,
    differing: Vec<String>,
}

/// `files` (relative to `source`) that `destination` lacks, and those it has with other content.
fn compare(files: &[String], source: &Path, destination: &Path) -> Diff {
    // ponytail: contentsEqual reads both copies, twice per merge; fine for save folders, hash once if it ever isn't.
    let mut missing: Vec<String> = Vec::new();
    let mut differing: Vec<String> = Vec::new();
    for path in files {
        let to = destination.join(path);
        if !to.exists() {
            missing.push(path.clone());
        } else if !files_equal(&source.join(path), &to) {
            differing.push(path.clone());
        }
    }
    Diff { missing, differing }
}

fn files_equal(a: &Path, b: &Path) -> bool {
    match (std::fs::read(a), std::fs::read(b)) {
        (Ok(a), Ok(b)) => a == b,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, write};
    use std::os::unix::fs::symlink as unix_symlink;

    /// A prefix whose Wine profile `users/<user>` has the Vista folders wineboot makes; `documents` stands for the Mac's
    /// own folder, which Wine links `Documents` to.
    fn make_prefix(user: &str, documents: Option<&Path>) -> PathBuf {
        let prefix = make_temp_dir().join("pfx");
        let home = prefix.join(format!("drive_c/users/{user}"));
        for folder in ["AppData/Local", "AppData/Roaming", "AppData/LocalLow", "Saved Games"] {
            std::fs::create_dir_all(home.join(folder)).unwrap();
        }
        match documents {
            Some(documents) => {
                std::fs::create_dir_all(documents).unwrap();
                unix_symlink(documents, home.join("Documents")).unwrap();
            }
            None => std::fs::create_dir_all(home.join("Documents")).unwrap(),
        }
        prefix
    }

    fn users(prefix: &Path, path: &str) -> PathBuf {
        prefix.join(format!("drive_c/users/{path}"))
    }

    fn link(prefix: &Path, path: &str) -> Option<String> {
        std::fs::read_link(users(prefix, path)).ok().map(|p| p.to_string_lossy().into_owned())
    }

    fn is_real_folder(prefix: &Path, path: &str) -> bool {
        std::fs::symlink_metadata(users(prefix, path)).map(|m| m.is_dir()).unwrap_or(false)
    }

    fn read(prefix: &Path, path: &str) -> Option<String> {
        std::fs::read_to_string(users(prefix, path)).ok()
    }

    fn seed(prefix: &Path, path: &str, text: &str) {
        write(text, &users(prefix, path), false);
    }

    fn apply(prefix: &Path, user: &str) -> ProfileLinkReport {
        ProfileLinks::apply(prefix, user, true)
    }

    #[test]
    fn an_empty_profile_gets_all_four_links() {
        let prefix = make_prefix("u", None);
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "u/Local Settings/Application Data").as_deref(), Some("../AppData/Local"));
        assert_eq!(link(&prefix, "u/Application Data").as_deref(), Some("AppData/Roaming"));
        assert_eq!(link(&prefix, "u/My Documents").as_deref(), Some("Documents"));
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(report, ProfileLinkReport::default());
    }

    #[test]
    fn seeded_steamuser_merges_into_vista_folders() {
        // Steam's cloud download wrote WinAppDataLocal's files under its XP name before MacNeutron ever ran.
        let prefix = make_prefix("u", None);
        seed(&prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud");
        let report = apply(&prefix, "u");
        assert_eq!(read(&prefix, "u/AppData/Local/G/s.sav").as_deref(), Some("cloud"));
        // the path Steam watches
        assert_eq!(read(&prefix, "steamuser/Local Settings/Application Data/G/s.sav").as_deref(), Some("cloud"));
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(read(&prefix, "steamuser BACKUP/Local Settings/Application Data/G/s.sav").as_deref(), Some("cloud"));
        assert_eq!(report.conflicts, 0);
    }

    #[test]
    fn a_differing_file_changes_nothing() {
        let prefix = make_prefix("u", None);
        seed(&prefix, "u/AppData/Local/G/s.sav", "local");
        seed(&prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud");
        let report = apply(&prefix, "u");
        assert!(is_real_folder(&prefix, "steamuser"));
        assert_eq!(read(&prefix, "steamuser/Local Settings/Application Data/G/s.sav").as_deref(), Some("cloud"));
        assert_eq!(read(&prefix, "u/AppData/Local/G/s.sav").as_deref(), Some("local"));
        assert!(!is_real_folder(&prefix, "steamuser BACKUP"));
        assert_eq!(report.conflicts, 1);
    }

    #[test]
    fn an_identical_file_merges() {
        let prefix = make_prefix("u", None);
        seed(&prefix, "u/AppData/Roaming/G/a.cfg", "same");
        seed(&prefix, "steamuser/Application Data/G/a.cfg", "same");
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(report.conflicts, 0);
    }

    #[test]
    fn a_second_run_changes_nothing() {
        let prefix = make_prefix("u", None);
        seed(&prefix, "steamuser/Saved Games/G/x", "cloud");
        apply(&prefix, "u");
        assert_eq!(apply(&prefix, "u"), ProfileLinkReport::default());
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(read(&prefix, "u/Saved Games/G/x").as_deref(), Some("cloud"));
    }

    #[test]
    fn a_profile_named_steamuser_gets_no_steamuser_link() {
        let prefix = make_prefix("steamuser", None);
        apply(&prefix, "steamuser");
        assert!(is_real_folder(&prefix, "steamuser"));
        assert_eq!(link(&prefix, "steamuser/Application Data").as_deref(), Some("AppData/Roaming"));
    }

    #[test]
    fn without_merge_real_folders_stay() {
        // The app's pass while Steam is closed only adds links; merging is the launch's job.
        let prefix = make_prefix("u", None);
        seed(&prefix, "steamuser/Application Data/G/a.cfg", "cloud");
        let report = ProfileLinks::apply(&prefix, "u", false);
        assert!(is_real_folder(&prefix, "steamuser"));
        assert_eq!(read(&prefix, "u/AppData/Roaming/G/a.cfg"), None);
        assert_eq!(report.notes.len(), 1);
        assert_eq!(report.conflicts, 0);
    }

    #[test]
    fn no_swap_support_leaves_folders_alone() {
        let prefix = make_prefix("u", None);
        seed(&prefix, "steamuser/Application Data/G/a.cfg", "cloud");
        let report = ProfileLinks::apply_with(&prefix, "u", true, |_| false);
        assert!(is_real_folder(&prefix, "steamuser"));
        assert_eq!(report.notes.len(), 1);
    }

    #[test]
    fn logged_paths_stop_at_the_games_folder() {
        // Save paths hold SteamIDs, account IDs in hex, persona names; launcher.log ends up in bug reports (spec 4.2).
        let prefix = make_prefix("u", None);
        seed(&prefix, "u/AppData/Local/G/0110000103a5b7c9/x", "local");
        seed(&prefix, "steamuser/Local Settings/Application Data/G/0110000103a5b7c9/x", "cloud");
        let report = apply(&prefix, "u");
        assert!(report.notes.iter().any(|n| n.contains("(e.g. Local Settings/Application Data/G/\u{2026})")));
        assert!(!report.notes.join("").contains("a5b7c9"));
    }

    // Review Focus

    #[test]
    fn my_documents_merges_through_wines_documents_link() {
        let documents = make_temp_dir().join("mac documents");
        let prefix = make_prefix("u", Some(&documents));
        seed(&prefix, "steamuser/My Documents/G/a", "cloud");
        apply(&prefix, "u");
        assert_eq!(std::fs::read_to_string(documents.join("G/a")).unwrap(), "cloud");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
    }

    #[test]
    fn a_rerun_after_clones_finishes_the_swap() {
        // A launch killed after cloning but before the swap left equal copies in the profile.
        let prefix = make_prefix("u", None);
        seed(&prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud");
        seed(&prefix, "u/AppData/Local/G/s.sav", "cloud");
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(report.conflicts, 0);
    }

    #[test]
    fn file_against_folder_refuses_the_merge() {
        let prefix = make_prefix("u", None);
        seed(&prefix, "u/AppData/Roaming/G/inner", "local folder's file");
        seed(&prefix, "steamuser/Application Data/G", "a file where the profile has a folder");
        apply(&prefix, "u");
        assert!(is_real_folder(&prefix, "steamuser"));
        assert_eq!(
            read(&prefix, "steamuser/Application Data/G").as_deref(),
            Some("a file where the profile has a folder")
        );
    }

    #[test]
    fn missing_profile_links_nothing() {
        let prefix = make_prefix("someone else", None);
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser"), None);
        assert_eq!(link(&prefix, "someone else/Application Data"), None);
        assert_eq!(report.notes.len(), 1);
    }

    #[test]
    fn backup_name_skips_taken_names() {
        let prefix = make_prefix("u", None);
        std::fs::create_dir_all(users(&prefix, "steamuser BACKUP")).unwrap();
        seed(&prefix, "steamuser/Saved Games/G/x", "cloud");
        apply(&prefix, "u");
        assert_eq!(read(&prefix, "steamuser BACKUP 2/Saved Games/G/x").as_deref(), Some("cloud"));
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
    }

    // Final review fixes

    #[test]
    fn a_profile_named_steamuser_in_any_case_is_never_merged_into_itself() {
        // APFS is case-insensitive: for the Mac user "SteamUser", users/steamuser *is* the profile.
        let prefix = make_prefix("SteamUser", None);
        seed(&prefix, "SteamUser/AppData/Local/G/s.sav", "local");
        let report = apply(&prefix, "SteamUser");
        assert!(is_real_folder(&prefix, "SteamUser"));
        assert_eq!(read(&prefix, "SteamUser/AppData/Local/G/s.sav").as_deref(), Some("local"));
        assert!(!is_real_folder(&prefix, "steamuser BACKUP"));
        assert_eq!(report.conflicts, 0);
    }

    #[test]
    fn a_link_to_another_real_folder_is_left_alone() {
        // The Mac's short name changed, or two accounts share a library: Steam's watched files resolve through the old
        // link, and repointing it would make them vanish before Steam's exit pass.
        let prefix = make_prefix("u", None);
        seed(&prefix, "old/AppData/Local/G/s.sav", "cloud-synced");
        unix_symlink("old", users(&prefix, "steamuser")).unwrap();
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("old"));
        assert_eq!(read(&prefix, "steamuser/AppData/Local/G/s.sav").as_deref(), Some("cloud-synced"));
        assert_eq!(report.conflicts, 1);
    }

    #[test]
    fn a_dangling_link_is_replaced() {
        let prefix = make_prefix("u", None);
        unix_symlink("gone", users(&prefix, "steamuser")).unwrap();
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(report, ProfileLinkReport::default());
    }

    #[test]
    fn an_equivalent_link_is_kept() {
        // Proton writes "./AppData/Roaming"; it reaches the same folder, so it isn't churned.
        let prefix = make_prefix("u", None);
        unix_symlink("./AppData/Roaming", users(&prefix, "u/Application Data")).unwrap();
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "u/Application Data").as_deref(), Some("./AppData/Roaming"));
        assert_eq!(report, ProfileLinkReport::default());
    }

    #[test]
    fn finders_own_files_never_block_a_merge() {
        // Opening both trees in Finder (to back up saves, say) leaves a different .DS_Store in each.
        let prefix = make_prefix("u", None);
        seed(&prefix, "u/AppData/Local/G/.DS_Store", "finder here");
        seed(&prefix, "steamuser/Local Settings/Application Data/G/.DS_Store", "finder there");
        seed(&prefix, "steamuser/Local Settings/Application Data/G/._s.sav", "resource fork");
        seed(&prefix, "steamuser/Local Settings/Application Data/G/s.sav", "cloud");
        let report = apply(&prefix, "u");
        assert_eq!(link(&prefix, "steamuser").as_deref(), Some("u"));
        assert_eq!(read(&prefix, "u/AppData/Local/G/s.sav").as_deref(), Some("cloud"));
        assert_eq!(report.conflicts, 0);
    }

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    #[test]
    fn makes_the_expected_links() {
        let prefix = temp("links");
        std::fs::create_dir_all(prefix.join("drive_c/users/alice")).unwrap();
        let report = ProfileLinks::apply_with(&prefix, "alice", true, |_| false);
        assert_eq!(report.conflicts, 0);
        assert!(std::fs::symlink_metadata(prefix.join("drive_c/users/alice/My Documents")).is_ok());
        assert!(prefix.join("drive_c/users/steamuser").exists());
        let _ = std::fs::remove_dir_all(&prefix);
    }

    #[test]
    fn no_profile_is_noted() {
        let prefix = temp("noprofile");
        std::fs::create_dir_all(prefix.join("drive_c/users")).unwrap();
        let report = ProfileLinks::apply_with(&prefix, "alice", true, |_| false);
        assert_eq!(report.notes.len(), 1);
        let _ = std::fs::remove_dir_all(&prefix);
    }

    #[test]
    fn redacts_below_the_game_folder() {
        assert_eq!(ProfileLinks::redacted("AppData/Local/Game/12345/save"), "AppData/Local/Game/\u{2026}");
    }
}
