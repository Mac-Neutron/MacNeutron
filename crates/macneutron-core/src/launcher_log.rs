//! `~/Library/Logs/MacNeutron`: one line per launch in `launcher.log`, plus opt-in per-game Wine logs.

use std::path::{Path, PathBuf};
use std::sync::Mutex;

use crate::file_lock::with_file_lock;

pub const ROTATE_BYTES: u64 = 1_048_576;
pub const GAME_LOG_ROTATE_BYTES: u64 = 50 * 1_048_576;

/// LauncherLog's quiet switch, shared by its copies (a Launcher's log is a `let`; the switch follows each launch).
#[derive(Default)]
pub struct QuietFlag {
    on: Mutex<bool>,
}

impl QuietFlag {
    pub fn is_on(&self) -> bool {
        *self.on.lock().unwrap()
    }

    pub fn set(&self, value: bool) {
        *self.on.lock().unwrap() = value;
    }
}

pub struct LauncherLog {
    pub directory: PathBuf,
    quiet: QuietFlag,
}

impl LauncherLog {
    pub fn new(directory: PathBuf) -> Self {
        LauncherLog { directory, quiet: QuietFlag::default() }
    }

    pub fn standard() -> Self {
        Self::new(home().join("Library/Logs/MacNeutron"))
    }

    pub fn launcher_log(&self) -> PathBuf {
        self.directory.join("launcher.log")
    }

    /// Creates the log folder and returns `steam-<appid>.log`, first rotating it to `steam-<appid>.log.1` past
    /// `GAME_LOG_ROTATE_BYTES`. Wine writes the log itself, so it rotates only here, at launch: one long session can
    /// still pass the limit.
    pub fn game_log(&self, app_id: &str) -> PathBuf {
        let _ = std::fs::create_dir_all(&self.directory);
        let log = self.directory.join(format!("steam-{app_id}.log"));
        let _ = with_file_lock(&self.directory.join("launcher.log.lock"), || {
            if file_size(&log).map(|s| s > GAME_LOG_ROTATE_BYTES).unwrap_or(false) {
                let rotated = self.directory.join(format!("steam-{app_id}.log.1"));
                let _ = std::fs::remove_file(&rotated);
                let _ = std::fs::rename(&log, &rotated);
            }
        });
        log
    }

    /// A performance build (X.Y.Zp) launched without MACNEUTRON_LOG keeps one line a launch: its summary.
    pub fn set_quiet(&self, on: bool) {
        self.quiet.set(on);
    }

    /// The launch's summary (or its error): written even when quiet.
    pub fn summary(&self, line: &str) {
        self.write_line(line);
    }

    /// Appends a timestamped line, rotating to `launcher.log.1` past `ROTATE_BYTES`.
    /// Never fails loudly: a logging problem must not stop a game from launching.
    /// Launches overlap (Steam's setup run and the game), so rotation and the write happen under a lock beside the log,
    /// and the line goes out in one append write.
    pub fn append(&self, line: &str) {
        if !self.quiet.is_on() {
            self.write_line(line);
        }
    }

    fn write_line(&self, line: &str) {
        let _ = std::fs::create_dir_all(&self.directory);
        let path = self.launcher_log();
        let text = format!("{} {}\n", iso8601_now(), line);
        let _ = with_file_lock(&self.directory.join("launcher.log.lock"), || {
            if file_size(&path).map(|s| s > ROTATE_BYTES).unwrap_or(false) {
                let rotated = self.directory.join("launcher.log.1");
                let _ = std::fs::remove_file(&rotated);
                let _ = std::fs::rename(&path, &rotated);
            }
            use std::io::Write;
            if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
                let _ = file.write_all(text.as_bytes());
            }
        });
    }
}

fn file_size(path: &Path) -> Option<u64> {
    std::fs::metadata(path).ok().map(|m| m.len())
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

/// The current time as ISO-8601 UTC (`2026-11-04T16:30:00Z`), enough for a log line without pulling in a date crate.
fn iso8601_now() -> String {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0);
    iso8601_from_unix(secs)
}

pub(crate) fn iso8601_from_unix(secs: i64) -> String {
    let days = secs.div_euclid(86_400);
    let rem = secs.rem_euclid(86_400);
    let (hour, minute, second) = (rem / 3600, (rem % 3600) / 60, rem % 60);
    let (year, month, day) = civil_from_days(days);
    format!("{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}Z")
}

/// Days since 1970-01-01 to a civil date, Howard Hinnant's algorithm.
fn civil_from_days(z: i64) -> (i64, u32, u32) {
    let z = z + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = (z - era * 146_097) as u64;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    (if m <= 2 { y + 1 } else { y }, m, d)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_epoch_and_a_known_date() {
        assert_eq!(iso8601_from_unix(0), "1970-01-01T00:00:00Z");
        // 2026-11-04T00:00:00Z
        assert_eq!(iso8601_from_unix(1_793_750_400), "2026-11-04T00:00:00Z");
    }

    #[test]
    fn writes_and_rotates() {
        let dir = std::env::temp_dir().join(format!("mn-log-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let log = LauncherLog::new(dir.clone());
        log.append("hello");
        let text = std::fs::read_to_string(log.launcher_log()).unwrap();
        assert!(text.ends_with(" hello\n"));
        log.set_quiet(true);
        log.append("ignored");
        assert!(!std::fs::read_to_string(log.launcher_log()).unwrap().contains("ignored"));
        log.summary("kept");
        assert!(std::fs::read_to_string(log.launcher_log()).unwrap().contains("kept"));
        let _ = std::fs::remove_dir_all(&dir);
    }

    fn lines(path: &Path) -> Vec<String> {
        std::fs::read_to_string(path).unwrap().split('\n').filter(|l| !l.is_empty()).map(String::from).collect()
    }

    #[test]
    fn appends_timestamped_lines() {
        let log = LauncherLog::new(crate::test_support::make_temp_dir().join("Logs"));
        log.append("verb=run exit=0");
        log.append("verb=run exit=1");
        let lines = lines(&log.launcher_log());
        assert_eq!(lines.len(), 2);
        assert!(lines[1].ends_with(" verb=run exit=1"));
        assert!(lines[0].starts_with("20"));
    }

    #[test]
    fn rotates_past_one_megabyte() {
        let log = crate::test_support::make_log();
        std::fs::write(log.launcher_log(), vec![0u8; ROTATE_BYTES as usize + 1]).unwrap();
        log.append("fresh");
        assert!(log.directory.join("launcher.log.1").exists());
        assert!(std::fs::read_to_string(log.launcher_log()).unwrap().ends_with(" fresh\n"));
    }

    #[test]
    fn game_log_is_per_app() {
        let log = LauncherLog::new(crate::test_support::make_temp_dir().join("Logs"));
        assert_eq!(log.game_log("42").file_name().unwrap(), "steam-42.log");
        assert!(log.directory.exists());
    }

    #[test]
    fn game_log_rotates_past_fifty_megabytes_keeping_one() {
        let log = crate::test_support::make_log();
        let game = log.game_log("42");
        let old = log.directory.join("steam-42.log.1");
        let size = |p: &Path| std::fs::metadata(p).ok().map(|m| m.len());
        crate::test_support::write("older", &old, false);
        crate::test_support::write("", &game, false);
        let file = std::fs::OpenOptions::new().write(true).open(&game).unwrap();
        file.set_len(GAME_LOG_ROTATE_BYTES).unwrap(); // at the limit (sparse): kept
        assert_eq!(log.game_log("42"), game);
        assert_eq!(size(&game), Some(GAME_LOG_ROTATE_BYTES));
        assert_eq!(size(&old), Some(5));
        file.set_len(GAME_LOG_ROTATE_BYTES + 1).unwrap(); // past it: rotated, the older one gone
        drop(file);
        log.game_log("42");
        assert_eq!(size(&game), None);
        assert_eq!(size(&old), Some(GAME_LOG_ROTATE_BYTES + 1));
    }

    #[test]
    fn concurrent_appends_keep_every_line() {
        // Launches overlap: no append may overwrite another.
        let log = crate::test_support::make_log();
        std::thread::scope(|scope| {
            for thread in 0..8 {
                let log = &log;
                scope.spawn(move || {
                    for i in 0..50 {
                        log.append(&format!("thread {thread} line {i}"));
                    }
                });
            }
        });
        assert_eq!(lines(&log.launcher_log()).len(), 400);
    }

    #[test]
    fn a_quiet_log_writes_only_summaries() {
        // A performance build (X.Y.Zp) launched without MACNEUTRON_LOG: one line a launch, its summary.
        let log = LauncherLog::new(crate::test_support::make_temp_dir().join("Logs"));
        log.set_quiet(true);
        log.append("note: Steam client folder /x");
        assert!(!log.launcher_log().exists());
        log.summary("verb=run appid=1 exit=0");
        let lines = lines(&log.launcher_log());
        assert_eq!(lines.len(), 1);
        assert!(lines[0].ends_with(" verb=run appid=1 exit=0"));
    }
}
