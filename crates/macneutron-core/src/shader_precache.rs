//! Shader pre-caching as Steam does it for Vulkan games: our d3d12.dll records each game's pipelines in
//! `<compatdata>/dxmt-pipelines`, and after a translator or macOS update the launcher rebuilds them with
//! `dxmt-replay.exe` before the game starts. Ported from `ShaderPrecache.swift`.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use crate::compat_context::CompatContext;
use crate::graphics_backend::GraphicsBackend;
use crate::process_runner::ProcessRunner;
use crate::tool_layout::ToolLayout;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ShaderPrecache {
    pub folder: PathBuf,
    /// The builds the recordings are compiled for: `<translator key> <macOS build>` (`DXMT/translator`). A stamp from
    /// before the key (`<dxmt-version> <macOS build>`) differs, so it replays once.
    pub builds: String,
}

impl ShaderPrecache {
    pub fn new(context: &CompatContext, layout: &ToolLayout, os_build: &str) -> Self {
        ShaderPrecache {
            folder: Self::folder(context),
            builds: format!("{} {}", layout.dxmt_translator().as_deref().unwrap_or("none"), os_build),
        }
    }

    /// Recordings in `folder`, compiled for `builds` (the VM's: native DXMT's, in the game's VM state folder).
    pub fn with_folder(folder: PathBuf, builds: String) -> Self {
        ShaderPrecache { folder, builds }
    }

    /// Recordings live beside the prefix, in the game's VM state folder.
    pub fn folder(context: &CompatContext) -> PathBuf {
        context.data_path.join("dxmt-pipelines")
    }

    /// Recording and replay are DXMT's; `MACNEUTRON_PRECACHE=0` turns both off.
    pub fn enabled(backend: GraphicsBackend, environment: &BTreeMap<String, String>) -> bool {
        backend == GraphicsBackend::Dxmt && environment.get("MACNEUTRON_PRECACHE").map(|v| v != "0").unwrap_or(true)
    }

    pub fn stamp_file(&self) -> PathBuf {
        self.folder.join("replayed")
    }

    pub fn stamp(&self) -> Option<String> {
        std::fs::read_to_string(self.stamp_file()).ok().map(|s| s.trim().to_string())
    }

    pub fn recordings(&self) -> Vec<PathBuf> {
        let mut found: Vec<PathBuf> = std::fs::read_dir(&self.folder)
            .map(|entries| {
                entries
                    .filter_map(|e| e.ok())
                    .map(|e| e.path())
                    .filter(|p| p.extension().map(|e| e == "pipelines").unwrap_or(false))
                    .collect()
            })
            .unwrap_or_default();
        found.sort_by(|a, b| a.file_name().cmp(&b.file_name()));
        found
    }

    /// Recordings compiled for other builds. No stamp means the recordings came from sessions of these builds.
    pub fn needs_replay(&self) -> bool {
        match self.stamp() {
            Some(stamp) if stamp != self.builds => !self.recordings().is_empty(),
            _ => false,
        }
    }

    pub fn write_stamp(&self) {
        let _ = std::fs::create_dir_all(&self.folder);
        let _ = std::fs::write(self.stamp_file(), format!("{}\n", self.builds));
    }

    pub fn write_stamp_if_missing(&self) {
        if self.stamp().is_none() {
            self.write_stamp();
        }
    }

    /// Runs `dxmt-replay.exe` on every recording in the game's environment, minus recording; one log line each.
    /// Stops before the next recording once `stopped` (Steam's Stop) says so. While a replay runs, `progress` gets
    /// "25% (n of total)" at each quarter, from the replayer's own progress lines.
    pub fn replay(
        &self,
        layout: &ToolLayout,
        runner: &dyn ProcessRunner,
        environment: &BTreeMap<String, String>,
        stopped: &dyn Fn() -> bool,
        progress: &(dyn Fn(&str) + Sync),
        poll_interval: Duration,
    ) -> Vec<String> {
        self.each(environment, stopped, progress, poll_interval, |recording, env, output| {
            let arguments = vec![
                layout.dxmt_replay().to_string_lossy().into_owned(),
                format!("Z:{}", recording.to_string_lossy()),
            ];
            runner.run(&layout.wine(), &arguments, env, Some(output)).unwrap_or(-1)
        })
    }

    /// The same natively (Direct3D forwarding: native DXMT in the VM host): the native `tool` on each recording, into the
    /// shader cache at `cache` (DXMT_SHADER_CACHE_PATH, as macneutron-vm gives native DXMT), before the VM boots.
    #[allow(clippy::too_many_arguments)] // mirrors ShaderPrecache.swift's replayNative
    pub fn replay_native(
        &self,
        tool: &Path,
        cache: &Path,
        runner: &dyn ProcessRunner,
        environment: &BTreeMap<String, String>,
        stopped: &dyn Fn() -> bool,
        progress: &(dyn Fn(&str) + Sync),
        poll_interval: Duration,
    ) -> Vec<String> {
        let mut env = environment.clone();
        env.insert("DXMT_SHADER_CACHE_PATH".into(), format!("{}/", cache.to_string_lossy()));
        self.each(&env, stopped, progress, poll_interval, |recording, env, output| {
            let arguments = vec![recording.to_string_lossy().into_owned()];
            runner.run(tool, &arguments, env, Some(output)).unwrap_or(-1)
        })
    }

    fn each(
        &self,
        environment: &BTreeMap<String, String>,
        stopped: &dyn Fn() -> bool,
        progress: &(dyn Fn(&str) + Sync),
        poll_interval: Duration,
        run: impl Fn(&Path, &BTreeMap<String, String>, &Path) -> i32,
    ) -> Vec<String> {
        let mut env = environment.clone();
        env.remove("DXMT_PIPELINE_RECORD");
        let output = self.folder.join("replay.log");
        let mut lines = Vec::new();
        for recording in self.recordings() {
            if stopped() {
                break;
            }
            let _ = std::fs::remove_file(&output);
            let done = AtomicBool::new(false);
            let status = std::thread::scope(|scope| {
                scope.spawn(|| {
                    let mut quarter = 0;
                    while !done.load(Ordering::Acquire) {
                        std::thread::sleep(poll_interval);
                        // ponytail: rereads the whole output each poll; it stays a few hundred bytes (one line per 10%).
                        let Some((n, total)) = Self::last_progress(&output).filter(|&(_, t)| t > 0) else {
                            continue;
                        };
                        let reached = n * 4 / total;
                        if reached > quarter && reached < 4 {
                            quarter = reached;
                            progress(&format!("{}% ({} of {})", reached * 25, n, total));
                        }
                    }
                });
                let status = run(&recording, &env, &output);
                done.store(true, Ordering::Release);
                status
            });
            let text = std::fs::read_to_string(&output).unwrap_or_default();
            let result = text.lines().rev().find(|l| l.starts_with("replay: "));
            let name = recording.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            lines.push(format!("precache: {name} exit={status} {}", result.unwrap_or("no result")));
        }
        lines
    }

    /// The last `replay progress <n>/<total>` line dxmt-replay.exe wrote.
    fn last_progress(output: &Path) -> Option<(usize, usize)> {
        let text = std::fs::read_to_string(output).ok()?;
        let line = text.lines().rev().find(|l| l.starts_with("replay progress "))?;
        let (n, total) = line["replay progress ".len()..].split_once('/')?;
        if total.contains('/') {
            return None;
        }
        Some((n.trim().parse().ok()?, total.trim().parse().ok()?))
    }

    /// `kern.osversion`, the macOS build (e.g. 25A354): Metal's compiler changes with it.
    pub fn macos_build() -> String {
        let name = std::ffi::CString::new("kern.osversion").unwrap();
        let mut size = 0usize;
        let mut buffer = vec![0u8; 1];
        unsafe {
            libc::sysctlbyname(name.as_ptr(), std::ptr::null_mut(), &mut size, std::ptr::null_mut(), 0);
            buffer.resize(size.max(1), 0);
            libc::sysctlbyname(name.as_ptr(), buffer.as_mut_ptr().cast(), &mut size, std::ptr::null_mut(), 0);
        }
        let end = buffer.iter().position(|&b| b == 0).unwrap_or(buffer.len());
        String::from_utf8_lossy(&buffer[..end]).into_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, make_tool_layout, steam_environment, write, FakeRunner};
    use std::io::Write as _;
    use std::sync::{Arc, Mutex};
    use std::time::Instant;

    use crate::launcher::tests::{args, read, test_launcher};
    use crate::launcher::Launcher;
    use crate::test_support::{Call, RecordingNotifier, TEST_IDENTITY};
    use std::sync::{OnceLock, Weak};

    struct PrecacheFixture {
        launcher: Arc<Launcher>,
        runner: Arc<FakeRunner>,
        notifier: Arc<RecordingNotifier>,
        env: BTreeMap<String, String>,
        folder: PathBuf,
        builds: String,
    }

    impl PrecacheFixture {
        fn stamp(&self) -> Option<String> {
            std::fs::read_to_string(self.folder.join("replayed")).ok()
        }

        fn replays(&self) -> Vec<Call> {
            self.runner.calls().into_iter().filter(is_replay).collect()
        }

        fn launcher_log(&self) -> String {
            read(&self.launcher.log.launcher_log())
        }

        fn launch(&self, argv: &[&str], env: &BTreeMap<String, String>) -> i32 {
            self.launcher.launch(&args(argv), env)
        }

        fn ran_the_game(&self) -> Option<Call> {
            self.runner.calls().into_iter().find(|c| c.arguments == ["/g/Game.exe"])
        }

        fn record(&self, name: &str) {
            write("rec", &self.folder.join(name), false);
        }

        fn stamped(&self, text: &str) {
            write(text, &self.folder.join("replayed"), false);
        }
    }

    fn is_replay(call: &Call) -> bool {
        call.arguments.first().is_some_and(|a| a.ends_with("dxmt-replay.exe"))
    }

    /// A launcher on the fake wine.app (DXMT `fork123`, translator `tr123`, and its replayer). The fake dxmt-replay.exe
    /// writes a result line to its output and returns `replay_status`, after `on_replay` with the launcher.
    fn make_precache_fixture_with(replay_status: i32, on_replay: Option<fn(&Launcher, &Call)>) -> PrecacheFixture {
        let layout = make_tool_layout();
        write("fork123\n", &layout.wine_app().join("Contents/Resources/DXMT/version"), false);
        write("tr123\n", &layout.dxmt_translator_file(), false);
        let this: Arc<OnceLock<Weak<Launcher>>> = Arc::default();
        let me = this.clone();
        let runner = Arc::new(FakeRunner::new(move |call| {
            if call.arguments.first().map(String::as_str) == Some("wineboot") {
                if let Some(prefix) = call.environment.get("WINEPREFIX") {
                    let _ = std::fs::create_dir_all(prefix);
                }
            }
            if is_replay(call) {
                if let Some(output) = &call.output {
                    let _ = std::fs::write(
                        output,
                        "replay progress 2/2\r\nreplay: 2 pipelines (1 graphics, 1 compute), 2 created, 0 failed, 0 bad records, 5 ms\r\n",
                    );
                    if let (Some(on_replay), Some(launcher)) = (on_replay, me.get().and_then(Weak::upgrade)) {
                        on_replay(&launcher, call);
                    }
                    return replay_status;
                }
            }
            0
        }));
        let notifier = Arc::new(RecordingNotifier::default());
        let data = make_temp_dir().join("compatdata/42");
        let launcher = Arc::new(test_launcher(
            layout,
            runner.clone(),
            notifier.clone(),
            Some(TEST_IDENTITY),
            make_temp_dir().join("no-vm"),
        ));
        let _ = this.set(Arc::downgrade(&launcher));
        let env = steam_environment(&data, "42");
        let context = CompatContext::new(&env).unwrap();
        PrecacheFixture {
            launcher,
            runner,
            notifier,
            env,
            folder: ShaderPrecache::folder(&context),
            builds: format!("tr123 {}", ShaderPrecache::macos_build()),
        }
    }

    fn make_precache_fixture() -> PrecacheFixture {
        make_precache_fixture_with(0, None)
    }

    const GAME: [&str; 2] = ["waitforexitandrun", "/g/Game.exe"];

    #[test]
    fn the_game_records_into_its_compat_folder() {
        let f = make_precache_fixture();
        f.launch(&GAME, &f.env);
        let run = f.ran_the_game().unwrap();
        assert_eq!(run.environment["DXMT_PIPELINE_RECORD"], f.folder.to_string_lossy());
        assert!(f.folder.to_string_lossy().ends_with("compatdata/42/dxmt-pipelines"));
    }

    #[test]
    fn the_first_session_stamps_without_replaying() {
        let f = make_precache_fixture();
        assert_eq!(f.launch(&GAME, &f.env), 0);
        assert!(f.replays().is_empty());
        assert_eq!(f.stamp(), Some(format!("{}\n", f.builds)));
    }

    #[test]
    fn recordings_without_a_stamp_are_not_replayed() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.launch(&GAME, &f.env);
        assert!(f.replays().is_empty());
        assert_eq!(f.stamp(), Some(format!("{}\n", f.builds)));
    }

    #[test]
    fn the_same_builds_do_not_replay() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped(&format!("{}\n", f.builds));
        f.launch(&GAME, &f.env);
        assert!(f.replays().is_empty());
    }

    // The stamp is the translator's key (arm64 release Ruling 46), not DXMT's version: a DXMT update that leaves the
    // translator alone replays nothing.
    #[test]
    fn a_dxmt_update_that_keeps_the_translator_does_not_replay() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped(&format!("{}\n", f.builds));
        write("fork456\n", &f.launcher.layout.wine_app().join("Contents/Resources/DXMT/version"), false);
        f.launch(&GAME, &f.env);
        assert!(f.replays().is_empty());
    }

    #[test]
    fn a_changed_translator_replays() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped(&format!("{}\n", f.builds));
        write("tr456\n", &f.launcher.layout.dxmt_translator_file(), false);
        f.launch(&GAME, &f.env);
        assert_eq!(f.replays().len(), 1);
        assert_eq!(f.stamp(), Some(format!("tr456 {}\n", ShaderPrecache::macos_build())));
    }

    // A stamp from before the key (`<dxmt commit>+<series> <macOS build>`) mismatches once, then the new stamp holds.
    #[test]
    fn a_version_era_stamp_replays_once() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped(&format!(
            "1fba8d25b5e29ab49012d633676a6b0d4b3b96c5+cd6d4065e615 {}\n",
            ShaderPrecache::macos_build()
        ));
        f.launch(&GAME, &f.env);
        f.launch(&GAME, &f.env);
        assert_eq!(f.replays().len(), 1);
        assert_eq!(f.stamp(), Some(format!("{}\n", f.builds)));
    }

    #[test]
    fn changed_builds_replay_every_recording_before_the_game() {
        let f = make_precache_fixture();
        let (a, b) = (f.folder.join("A.exe.pipelines"), f.folder.join("B.exe.pipelines"));
        f.record("A.exe.pipelines");
        f.record("B.exe.pipelines");
        f.stamped("old 1A2\n");
        assert_eq!(f.launch(&GAME, &f.env), 0);
        let replayer = f.launcher.layout.dxmt_replay().to_string_lossy().into_owned();
        let replayed: Vec<Vec<String>> = f.replays().into_iter().map(|c| c.arguments).collect();
        assert_eq!(
            replayed,
            [
                vec![replayer.clone(), format!("Z:{}", a.to_string_lossy())],
                vec![replayer, format!("Z:{}", b.to_string_lossy())],
            ]
        );
        assert!(f.replays().iter().all(|c| !c.environment.contains_key("DXMT_PIPELINE_RECORD")));
        let calls = f.runner.calls();
        let last_replay = calls.iter().rposition(is_replay).unwrap();
        let game_run = calls.iter().position(|c| c.arguments == ["/g/Game.exe"]).unwrap();
        assert!(last_replay < game_run);
        assert_eq!(f.stamp(), Some(format!("{}\n", f.builds)));
        assert!(f.notifier.posted().is_empty()); // the toast, not notifications
        assert_eq!(f.notifier.toasts().last().map(String::as_str), Some("Preparing the game's shaders"));
        assert_eq!(f.notifier.toasts_closed(), f.notifier.toasts().len());
        assert!(f.launcher_log().contains(
            "precache: A.exe.pipelines exit=0 replay: 2 pipelines (1 graphics, 1 compute), 2 created, 0 failed, 0 bad records, 5 ms"
        ));
    }

    #[test]
    fn a_failed_replay_still_starts_the_game() {
        let f = make_precache_fixture_with(1, None);
        f.record("Game.exe.pipelines");
        f.stamped("old 1A2\n");
        assert_eq!(f.launch(&GAME, &f.env), 0);
        assert!(f.ran_the_game().is_some());
        assert_eq!(f.stamp(), Some(format!("{}\n", f.builds)));
        assert!(f.launcher_log().contains("precache: Game.exe.pipelines exit=1 "));
    }

    #[test]
    fn the_run_verb_neither_replays_nor_stamps() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped("old 1A2\n");
        f.launch(&["run", "/g/Game.exe"], &f.env);
        assert!(f.replays().is_empty());
        assert_eq!(f.stamp().as_deref(), Some("old 1A2\n"));
    }

    #[test]
    fn precache_can_be_turned_off() {
        let f = make_precache_fixture();
        f.record("Game.exe.pipelines");
        f.stamped("old 1A2\n");
        let mut env = f.env.clone();
        env.insert("MACNEUTRON_PRECACHE".into(), "0".into());
        f.launch(&GAME, &env);
        assert!(f.replays().is_empty());
        assert_eq!(f.ran_the_game().unwrap().environment.get("DXMT_PIPELINE_RECORD"), None);
        assert_eq!(f.stamp().as_deref(), Some("old 1A2\n"));
    }

    #[test]
    fn stopping_during_the_replay_starts_no_game() {
        // Steam's Stop (SIGTERM) arrives while the first recording replays.
        let f = make_precache_fixture_with(0, Some(|launcher, call| launcher.terminate(&call.environment)));
        f.record("A.exe.pipelines");
        f.record("B.exe.pipelines");
        f.stamped("old 1A2\n");
        f.launch(&GAME, &f.env);
        assert_eq!(f.replays().len(), 1);
        assert!(f.ran_the_game().is_none());
        assert_eq!(f.stamp().as_deref(), Some("old 1A2\n")); // the next launch prepares the shaders again
    }

    #[test]
    fn the_replay_reports_its_progress_by_quarters() {
        let layout = make_tool_layout();
        let data = make_temp_dir().join("compatdata/42");
        let context = CompatContext::new(&steam_environment(&data, "42")).unwrap();
        let precache = ShaderPrecache::new(&context, &layout, "26A1");
        write("rec", &precache.folder.join("Game.exe.pipelines"), false);
        // A replay of 20 pipelines that appends its progress as it goes, as dxmt-replay.exe does. After each of the first
        // three lines it waits (up to 5 s) for that quarter's message, so a late poll can't skip one.
        let messages: Arc<Mutex<Vec<String>>> = Arc::default();
        let seen = messages.clone();
        let runner = FakeRunner::new(move |call| {
            let Some(output) = &call.output else { return 1 };
            let Ok(mut file) = std::fs::File::create(output) else { return 1 };
            let texts = [
                "replay progress 5/20\r\n",
                "replay progress 10/20\r\n",
                "replay progress 15/20\r\n",
                "replay progress 20/20\r\nreplay: 20 pipelines (20 graphics, 0 compute), 20 created, 0 failed, 0 bad records, 9 ms\r\n",
            ];
            for (i, text) in texts.iter().enumerate() {
                file.write_all(text.as_bytes()).unwrap();
                let deadline = Instant::now() + Duration::from_secs(5);
                while i < 3 && seen.lock().unwrap().len() <= i && Instant::now() < deadline {
                    std::thread::sleep(Duration::from_millis(10));
                }
            }
            0
        });
        let lines = precache.replay(
            &layout,
            &runner,
            &BTreeMap::new(),
            &|| false,
            &|m| messages.lock().unwrap().push(m.to_string()),
            Duration::from_millis(50),
        );
        assert_eq!(*messages.lock().unwrap(), ["25% (5 of 20)", "50% (10 of 20)", "75% (15 of 20)"]);
        assert_eq!(
            lines,
            ["precache: Game.exe.pipelines exit=0 replay: 20 pipelines (20 graphics, 0 compute), 20 created, 0 failed, 0 bad records, 9 ms"]
        );
    }
}
