//! Starts child processes. A trait so tests can record calls instead of running Wine.

use std::collections::BTreeMap;
use std::path::Path;
use std::process::Stdio;

/// 128 + signal if killed, else the exit status.
pub type ExitStatus = i32;

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
        let mut command = std::process::Command::new(executable);
        command.args(arguments);
        // An explicit environment replaces the inherited one, matching Foundation's `Process.environment`.
        command.env_clear();
        command.envs(environment);

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

        let mut child = command.spawn()?;
        started(child.id() as i32);
        let status = child.wait()?;
        drop(handle);
        Ok(status_code(status))
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