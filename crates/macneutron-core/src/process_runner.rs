//! Starts child processes. A trait so tests can record calls instead of running Wine.

use std::collections::BTreeMap;
use std::path::Path;
use std::process::Stdio;

/// 128 + signal if killed, else the exit status.
pub type ExitStatus = i32;

/// Exit status and macOS process resource/exit diagnostics captured before reaping the child.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcessExit {
    pub status: i32,
    pub killed_for_memory: bool,
    pub exit_detail: Option<u32>,
    pub peak_footprint_bytes: Option<u64>,
}

pub trait ProcessRunner: Send + Sync {
    /// Runs `executable` to completion and returns its exit status (128 + signal if killed).
    /// Output is appended to `output` when given, otherwise inherited from this process.
    fn run(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
    ) -> std::io::Result<ExitStatus>;

    /// As `run`, telling `started` the child's process ID once it runs.
    fn run_with_started(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
        started: &mut dyn FnMut(i32),
    ) -> std::io::Result<ExitStatus>;

    /// As `run_with_started`, returning exit detail (`NOTE_EXIT_MEMORY`) and peak physical footprint.
    fn run_with_exit_detail(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
        started: &mut dyn FnMut(i32),
    ) -> std::io::Result<ProcessExit> {
        let status = self.run_with_started(executable, arguments, environment, output, started)?;
        Ok(ProcessExit {
            status,
            killed_for_memory: false,
            exit_detail: None,
            peak_footprint_bytes: None,
        })
    }
}

/// A runner that starts real children through `std::process`.
pub struct SystemProcessRunner;

impl Default for SystemProcessRunner {
    fn default() -> Self {
        SystemProcessRunner
    }
}

impl SystemProcessRunner {
    pub fn new() -> Self {
        SystemProcessRunner
    }
}

fn spawn_child(
    executable: &Path,
    arguments: &[String],
    environment: &BTreeMap<String, String>,
    output: Option<&Path>,
) -> std::io::Result<(std::process::Child, Option<std::fs::File>)> {
    let mut command = std::process::Command::new(executable);
    command.args(arguments);
    // An explicit environment replaces the inherited one, matching Foundation's `Process.environment`.
    command.env_clear();
    command.envs(environment);
    // The leader of a process group of its own, as Foundation's Process makes it: Steam's Stop signals the group
    // of a microVM launch (Launcher::stop_vm).
    use std::os::unix::process::CommandExt;
    command.process_group(0);

    let mut handle: Option<std::fs::File> = None;
    if let Some(path) = output {
        let file = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)?;
        let clone = file.try_clone()?;
        command.stdout(Stdio::from(clone));
        command.stderr(Stdio::from(file.try_clone()?));
        handle = Some(file);
    }

    let child = command.spawn()?;
    Ok((child, handle))
}

fn pid_peak_footprint(pid: i32) -> Option<u64> {
    let mut info: libc::rusage_info_v4 = unsafe { std::mem::zeroed() };
    let rc = unsafe {
        libc::proc_pid_rusage(
            pid,
            libc::RUSAGE_INFO_V4,
            (&mut info as *mut libc::rusage_info_v4).cast::<libc::rusage_info_t>(),
        )
    };
    (rc == 0).then_some(info.ri_lifetime_max_phys_footprint)
}

struct KqueueFd(libc::c_int);

impl Drop for KqueueFd {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.0);
        }
    }
}

enum WatchState {
    Registered(KqueueFd),
    AlreadyExited,
    Unavailable,
}

fn register_exit_watch(pid: i32) -> WatchState {
    let raw = unsafe { libc::kqueue() };
    if raw < 0 {
        return WatchState::Unavailable;
    }
    let kq = KqueueFd(raw);
    let changelist = libc::kevent {
        ident: pid as libc::uintptr_t,
        filter: libc::EVFILT_PROC,
        flags: libc::EV_ADD | libc::EV_ONESHOT,
        fflags: libc::NOTE_EXIT | libc::NOTE_EXIT_DETAIL,
        data: 0,
        udata: std::ptr::null_mut(),
    };
    loop {
        let rc = unsafe {
            libc::kevent(
                kq.0,
                &changelist,
                1,
                std::ptr::null_mut(),
                0,
                std::ptr::null(),
            )
        };
        if rc == 0 {
            return WatchState::Registered(kq);
        }
        match std::io::Error::last_os_error().raw_os_error() {
            Some(libc::EINTR) => continue,
            Some(libc::ESRCH) => return WatchState::AlreadyExited,
            _ => return WatchState::Unavailable,
        }
    }
}

impl ProcessRunner for SystemProcessRunner {
    fn run(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
    ) -> std::io::Result<ExitStatus> {
        self.run_with_started(executable, arguments, environment, output, &mut |_| {})
    }

    fn run_with_started(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
        started: &mut dyn FnMut(i32),
    ) -> std::io::Result<ExitStatus> {
        let (mut child, handle) = spawn_child(executable, arguments, environment, output)?;
        started(child.id() as i32);
        let status = child.wait()?;
        drop(handle);
        Ok(status_code(status))
    }

    fn run_with_exit_detail(
        &self,
        executable: &Path,
        arguments: &[String],
        environment: &BTreeMap<String, String>,
        output: Option<&Path>,
        started: &mut dyn FnMut(i32),
    ) -> std::io::Result<ProcessExit> {
        let (mut child, handle) = spawn_child(executable, arguments, environment, output)?;
        let pid = child.id() as i32;
        let watch = register_exit_watch(pid);
        started(pid);
        let (killed_for_memory, exit_detail, peak_footprint_bytes) = match watch {
            WatchState::Registered(kq) => {
                let mut event: libc::kevent = unsafe { std::mem::zeroed() };
                let n = loop {
                    let r = unsafe {
                        libc::kevent(kq.0, std::ptr::null(), 0, &mut event, 1, std::ptr::null())
                    };
                    if r < 0 && std::io::Error::last_os_error().raw_os_error() == Some(libc::EINTR) {
                        continue;
                    }
                    break r;
                };
                if n > 0 && (event.flags & libc::EV_ERROR) == 0 {
                    let detail = event.data as u32;
                    let killed = (detail & libc::NOTE_EXIT_MEMORY) != 0;
                    // Query before child.wait() so the zombie has not been reaped yet.
                    let peak = pid_peak_footprint(pid);
                    (killed, Some(detail), peak)
                } else {
                    (false, None, None)
                }
            }
            WatchState::AlreadyExited => (false, None, pid_peak_footprint(pid)),
            WatchState::Unavailable => (false, None, None),
        };
        let status = child.wait()?;
        drop(handle);
        Ok(ProcessExit {
            status: status_code(status),
            killed_for_memory,
            exit_detail,
            peak_footprint_bytes,
        })
    }
}

fn status_code(status: std::process::ExitStatus) -> ExitStatus {
    use std::os::unix::process::ExitStatusExt;
    if let Some(signal) = status.signal() {
        128 + signal
    } else {
        status.code().unwrap_or(0)
    }
}

/// The `$PATH` a Finder-launched GUI app actually sees is minimal; extend it with the usual tool locations.
pub fn resolved_path() -> String {
    let mut parts: Vec<String> = Vec::new();
    if let Some(existing) = std::env::var_os("PATH") {
        for segment in existing.to_string_lossy().split(':') {
            parts.push(segment.to_string());
        }
    }
    for extra in ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"] {
        if !parts.iter().any(|p| p == extra) {
            parts.push(extra.to_string());
        }
    }
    parts.join(":")
}

/// The launcher folder, so a tool binary a game calls can find its siblings.
pub fn tool_path(tool_root: &Path) -> String {
    format!("{}/bin:{}", tool_root.display(), resolved_path())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exit_detail_reports_clean_exit_and_peak_footprint() {
        let runner = SystemProcessRunner::new();
        let args = vec!["-c".to_string(), "exit 0".to_string()];
        let exit = runner
            .run_with_exit_detail(Path::new("/bin/sh"), &args, &BTreeMap::new(), None, &mut |_| {})
            .unwrap();
        assert_eq!(exit.status, 0);
        assert!(!exit.killed_for_memory);
        assert!(exit.peak_footprint_bytes.is_some());
    }

    #[test]
    fn exit_detail_reports_sigkilled_child_and_peak_footprint() {
        let runner = SystemProcessRunner::new();
        let args = vec!["-c".to_string(), "sleep 60".to_string()];
        let (tx, rx) = std::sync::mpsc::channel();
        let killer = std::thread::spawn(move || {
            let pid: i32 = rx.recv().unwrap();
            std::thread::sleep(std::time::Duration::from_millis(30));
            unsafe { libc::kill(pid, libc::SIGKILL) };
        });
        let exit = runner
            .run_with_exit_detail(
                Path::new("/bin/sh"),
                &args,
                &BTreeMap::new(),
                None,
                &mut |pid| {
                    let _ = tx.send(pid);
                },
            )
            .unwrap();
        killer.join().unwrap();
        assert_eq!(exit.status, 128 + libc::SIGKILL);
        assert!(!exit.killed_for_memory);
        assert!(exit.peak_footprint_bytes.is_some());
    }
}
