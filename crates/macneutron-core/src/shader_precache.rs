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

    // Swift's other 12 ShaderPrecacheTests drive `Launcher.launch` (not ported yet): they come with launcher.rs.

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
