//! A crash the game VM ended for.
//!
//! A game's own crash reporter (Unreal's `CrashReportClient`, and the other engines' kin) opens on the guest's Xvfb,
//! where no one can see or click it, and waits: `steam.exe` waits for it, so the VM would stay up until Steam's
//! Stop. `run-game.sh` recognizes one, keeps its report in the run share (`traces/crash-report`), writes
//! `traces/crash`, and ends the prefix.
//!
//! The Mac reads that marker after the run: the report's guest paths are translated back to the prefix and library
//! the Mac knows, and the launcher keeps a copy beside its own logs so the player can find it.

use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VMCrash {
    /// The marker `run-game.sh` writes once it has seen a reporter.
    pub when: String,
    pub reporter: String,
    pub command: String,
    /// The crash folders the guest found, in the Mac's paths.
    pub reports: Vec<String>,
    /// The share's copy of what the reporter left (`ps.txt`, `report/`), for the launcher to keep.
    pub details_url: PathBuf,
    /// The marker as the guest wrote it: the game log gets it whole.
    pub raw: String,
}

impl VMCrash {
    /// The marker `run-game.sh` writes once it has seen a reporter.
    pub const MARKER: &'static str = "traces/crash";
    /// The reporter's `ps` snapshot, its command line, the copy of its report, and the guest paths it came from.
    pub const DETAILS: &'static str = "traces/crash-report";

    /// The marker's fields (`key=value` a line, `report=` one guest path a line), or nil without one.
    pub fn read(share: &Path, prefix: &Path, library: &str) -> Option<VMCrash> {
        let marker_url = share.join(Self::MARKER);
        let raw = std::fs::read_to_string(&marker_url).ok()?;
        if raw.is_empty() {
            return None;
        }
        let mut fields: Vec<(String, String)> = Vec::new();
        let mut reports: Vec<String> = Vec::new();
        for line in raw.split('\n').filter(|l| !l.is_empty()) {
            let Some(eq) = line.find('=') else { continue };
            let key = &line[..eq];
            let value = &line[eq + 1..];
            if key == "report" {
                if let Some(path) = Self::mac_path(value, prefix, library) {
                    reports.push(path);
                }
            } else {
                fields.retain(|(k, _)| k != key);
                fields.push((key.to_string(), value.to_string()));
            }
        }
        let field = |key: &str| fields.iter().find(|(k, _)| k == key).map(|(_, v)| v.clone());
        Some(VMCrash {
            when: field("when").unwrap_or_default(),
            reporter: field("reporter").unwrap_or_else(|| "a crash reporter".to_string()),
            command: field("command").unwrap_or_default(),
            reports,
            details_url: share.join(Self::DETAILS),
            raw,
        })
    }

    /// A guest crash-report path as the Mac has it: the profile (`/home/mn/pfx/drive_c/users/steamuser`) is the Mac
    /// prefix's, shared into the VM, and `/mnt/games` is the Steam library (`--steam-map`). Anything else is left
    /// out: a path the Mac can't open is worse than none.
    pub fn mac_path(guest: &str, prefix: &Path, library: &str) -> Option<String> {
        let profile = "/home/mn/pfx/drive_c/users/steamuser";
        if guest == profile || guest.starts_with(&format!("{profile}/")) {
            return Some(format!(
                "{}/drive_c/users/steamuser{}",
                path_string(prefix),
                &guest[profile.len()..]
            ));
        }
        let games = "/mnt/games/";
        if let Some(rest) = guest.strip_prefix(games) {
            let root = library.trim_end_matches('/');
            return Some(format!("{root}/{rest}"));
        }
        None
    }

    /// One `launcher.log` line: what ran, and where its report is on the Mac.
    pub fn summary(&self) -> String {
        let mut line = format!("reporter={}", self.reporter);
        if !self.when.is_empty() {
            line.push_str(&format!(" when={}", self.when));
        }
        if !self.reports.is_empty() {
            line.push_str(&format!(" reports={}", self.reports.join(",")));
        }
        line
    }
}

fn path_string(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    fn write_marker(share: &Path, text: &str) {
        let path = share.join(VMCrash::MARKER);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
    }

    #[test]
    fn a_crash_marker_maps_its_guest_reports_back_to_the_mac() {
        let share = temp("share");
        let prefix = temp("prefix");
        let library = "/Users/someone/Library/Application Support/Steam/steamapps/common";
        write_marker(
            &share,
            "when=2026-10-25T21:00:00Z\n\
             reporter=CrashReportClie\n\
             pid=1234\n\
             command=C:\\Games\\Game\\Binaries\\CrashReportClient.exe -app=\"Game\"\n\
             report=/home/mn/pfx/drive_c/users/steamuser/AppData/Local/Game/Saved/Crashes/ABC\n\
             report=/mnt/games/Some Game/Saved/Crashes/DEF\n\
             report=/usr/share/something/else\n",
        );
        let details = share.join(VMCrash::DETAILS);
        std::fs::create_dir_all(&details).unwrap();
        std::fs::write(details.join("ps.txt"), b"ps\n").unwrap();

        let crash = VMCrash::read(&share, &prefix, library).expect("a crash");
        assert_eq!(crash.when, "2026-10-25T21:00:00Z");
        assert_eq!(crash.reporter, "CrashReportClie");
        assert!(crash.command.contains("-app=\"Game\""));
        assert_eq!(
            crash.reports,
            vec![
                format!("{}/drive_c/users/steamuser/AppData/Local/Game/Saved/Crashes/ABC", path_string(&prefix)),
                format!("{library}/Some Game/Saved/Crashes/DEF"),
            ]
        );
        assert_eq!(crash.details_url, share.join(VMCrash::DETAILS));
        assert!(crash.raw.contains("reporter=CrashReportClie"));
        assert!(crash.summary().contains("reports="));
    }

    #[test]
    fn a_marker_without_reports_still_says_what_ran() {
        let share = temp("bare");
        write_marker(&share, "when=2026-10-25T21:00:00Z\nreporter=BugSplat\n");
        let crash = VMCrash::read(&share, &temp("p2"), "/tmp/lib").expect("a crash");
        assert_eq!(crash.reporter, "BugSplat");
        assert!(crash.reports.is_empty());
        assert!(crash.command.is_empty());
    }

    #[test]
    fn no_marker_is_no_crash() {
        assert_eq!(VMCrash::read(&temp("none"), &temp("p3"), "/tmp/lib"), None);
    }

    #[test]
    fn a_guest_path_outside_the_shares_is_not_offered() {
        let prefix = temp("p4");
        assert_eq!(VMCrash::mac_path("/mnt/share/traces/x", &prefix, "/tmp/lib"), None);
        assert_eq!(VMCrash::mac_path("/home/mn/pfx/drive_c/windows/x", &prefix, "/tmp/lib"), None);
    }
}
