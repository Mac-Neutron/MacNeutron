//! Launch-time checks. `wine.app`'s signature is verified at install; here we only confirm the pieces a launch
//! needs are present and its identity reads.

use std::collections::BTreeMap;
use std::path::PathBuf;

use crate::pe_image::{self, AMD64, ARM64, I386};
use crate::process_runner::{ProcessRunner, SystemProcessRunner};
use crate::tool_layout::ToolLayout;
use crate::verb::{LaunchRequest, Verb};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PreflightError {
    UnsupportedSystem,
    RuntimeMissing,
    ThirtyTwoBit,
    UnsupportedMachine(u16),
    UnsupportedCPU(String, Vec<String>),
}

impl PreflightError {
    /// "apple-m5" -> "Apple M5"; another name as it is.
    pub fn cpu_name(cpu: &str) -> String {
        match cpu.strip_prefix("apple-") {
            Some(rest) => format!("Apple {}", rest.to_uppercase()),
            None => cpu.to_string(),
        }
    }

    pub fn machine_name(machine: u16) -> String {
        match machine {
            0x1c4 => "ARM (32-bit)".to_string(),
            0x200 => "Itanium".to_string(),
            other => format!("machine type 0x{other:04x}"),
        }
    }
}

impl std::fmt::Display for PreflightError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PreflightError::UnsupportedSystem => {
                write!(f, "MacNeutron needs macOS 27 or later on an Apple Silicon Mac.")
            }
            PreflightError::RuntimeMissing => {
                write!(f, "MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it.")
            }
            PreflightError::ThirtyTwoBit => write!(
                f,
                "This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games in a Steam library run in its game VM."
            ),
            PreflightError::UnsupportedMachine(machine) => write!(
                f,
                "This game is built for {}, which MacNeutron can't run.",
                Self::machine_name(*machine)
            ),
            PreflightError::UnsupportedCPU(cpu, missing) => write!(
                f,
                "This MacNeutron runtime is built for {}, and this Mac's processor lacks {}. Install the MacNeutron build for this Mac.",
                Self::cpu_name(cpu),
                missing.join(", ")
            ),
        }
    }
}

impl std::error::Error for PreflightError {}

/// Launch-time checks; the identity and CPU probes are injectable so tests never touch the real machine.
/// An injectable probe of the runtime's identity, or None when it can't be read.
pub type IdentityProbe = Box<dyn Fn(&ToolLayout) -> Option<String> + Send + Sync>;

pub struct Preflight {
    pub system_supported: Box<dyn Fn() -> bool + Send + Sync>,
    pub identity: IdentityProbe,
    pub cpu_has: Box<dyn Fn(&str) -> bool + Send + Sync>,
}

impl Default for Preflight {
    fn default() -> Self {
        Preflight {
            system_supported: Box::new(Preflight::is_supported_system),
            identity: Box::new(|layout| layout.identity()),
            cpu_has: Box::new(Preflight::cpu_has),
        }
    }
}

impl Preflight {
    pub fn new(
        system_supported: Box<dyn Fn() -> bool + Send + Sync>,
        identity: IdentityProbe,
        cpu_has: Box<dyn Fn(&str) -> bool + Send + Sync>,
    ) -> Self {
        Preflight { system_supported, identity, cpu_has }
    }

    /// `hw.optional.arm.<feature>` reads 1.
    pub fn cpu_has(feature: &str) -> bool {
        sysctl_int(&format!("hw.optional.arm.{feature}")) == Some(1)
    }

    /// macOS 27 or later on Apple Silicon.
    pub fn is_supported_system() -> bool {
        let version = std::process::Command::new("/usr/bin/sw_vers")
            .arg("-productVersion")
            .output()
            .ok()
            .and_then(|o| String::from_utf8(o.stdout).ok())
            .and_then(|s| s.trim().split('.').next().and_then(|m| m.parse::<u32>().ok()))
            .unwrap_or(0);
        version >= 27 && sysctl_int("hw.optional.arm64") == Some(1)
    }

    /// Returns the runtime's identity. A missing or unreadable runtime writes the damaged marker, which makes the
    /// app reinstall it. The target's machine type is checked only for the game (`waitforexitandrun`); a target that
    /// isn't PE (a script, a missing file) isn't checked. A 32-bit game passes when it runs in the microVM (new WoW64).
    pub fn check(
        &self,
        layout: &ToolLayout,
        request: &LaunchRequest,
        in_micro_vm: bool,
    ) -> Result<String, PreflightError> {
        if !(self.system_supported)() {
            return Err(PreflightError::UnsupportedSystem);
        }
        let wine_ok = std::fs::metadata(layout.wine()).map(|m| is_executable(&m)).unwrap_or(false);
        let wineserver_ok = std::fs::metadata(layout.wineserver()).map(|m| is_executable(&m)).unwrap_or(false);
        let identity = (self.identity)(layout);
        let (Some(_), Some(_), Some(identity)) = (wine_ok.then_some(()), wineserver_ok.then_some(()), identity) else {
            let _ = std::fs::write(layout.runtime_damaged_marker(), b"");
            return Err(PreflightError::RuntimeMissing);
        };
        // A runtime built for a newer CPU (wine-arm64/build.sh's MACNEUTRON_CPU) would die on an illegal instruction.
        if let Ok(text) = std::fs::read_to_string(layout.cpu_file()) {
            let lines: Vec<String> = text
                .lines()
                .map(|l| l.trim().to_string())
                .filter(|l| !l.is_empty())
                .collect();
            if let Some(cpu) = lines.first() {
                let missing: Vec<String> = lines[1..].iter().filter(|f| !(self.cpu_has)(f)).cloned().collect();
                if !missing.is_empty() {
                    return Err(PreflightError::UnsupportedCPU(cpu.clone(), missing));
                }
            }
        }
        if request.verb == Verb::WaitForExitAndRun {
            if let Some(machine) = pe_image::machine(std::path::Path::new(&request.target)) {
                match machine {
                    AMD64 | ARM64 => {}
                    I386 => {
                        if !in_micro_vm {
                            return Err(PreflightError::ThirtyTwoBit);
                        }
                    }
                    other => return Err(PreflightError::UnsupportedMachine(other)),
                }
            }
        }
        Ok(identity)
    }
}

fn is_executable(meta: &std::fs::Metadata) -> bool {
    use std::os::unix::fs::PermissionsExt;
    meta.permissions().mode() & 0o111 != 0
}

fn sysctl_int(name: &str) -> Option<i32> {
    let mut value: i32 = 0;
    let mut size = std::mem::size_of::<i32>();
    let cname = std::ffi::CString::new(name).ok()?;
    let rc = unsafe {
        sysctlbyname(
            cname.as_ptr(),
            &mut value as *mut i32 as *mut std::ffi::c_void,
            &mut size,
            std::ptr::null_mut(),
            0,
        )
    };
    if rc == 0 {
        Some(value)
    } else {
        None
    }
}

extern "C" {
    fn sysctlbyname(
        name: *const std::ffi::c_char,
        oldp: *mut std::ffi::c_void,
        oldlenp: *mut usize,
        newp: *mut std::ffi::c_void,
        newlen: usize,
    ) -> i32;
}

/// Tells the user about launch-blocking failures; Steam itself shows nothing useful.
pub trait Notifier: Send + Sync {
    fn post(&self, title: &str, message: &str);
    /// A toast while something long runs, until it's closed or `until` exists.
    fn progress(&self, title: &str, message: &str, until: Option<PathBuf>) -> ToastHandle;
}

/// A toast on screen: `update` replaces its message, `close` takes it away; updates after the close are dropped.
pub struct ToastHandle {
    pub update: Box<dyn Fn(&str) + Send + Sync>,
    pub close: Box<dyn Fn() + Send + Sync>,
}

impl Default for ToastHandle {
    fn default() -> Self {
        ToastHandle {
            update: Box::new(|_| {}),
            close: Box::new(|| {}),
        }
    }
}

/// Posts through `osascript`, which needs no app bundle or entitlement. Toasts are `toast`'s;
/// without one, none.
pub struct AppleScriptNotifier {
    toast: Option<PathBuf>,
}

impl AppleScriptNotifier {
    pub fn new(toast: Option<PathBuf>) -> Self {
        AppleScriptNotifier { toast }
    }

    /// An AppleScript string literal: backslashes and quotes escaped.
    pub fn quoted(text: &str) -> String {
        format!("\"{}\"", text.replace('\\', "\\\\").replace('\"', "\\\""))
    }
}

impl Notifier for AppleScriptNotifier {
    fn post(&self, title: &str, message: &str) {
        let script = format!(
            "display notification {} with title {}",
            Self::quoted(message),
            Self::quoted(title)
        );
        let _ = SystemProcessRunner {}.run(
            std::path::Path::new("/usr/bin/osascript"),
            &["-e".to_string(), script],
            &BTreeMap::new(),
            None,
        );
    }

    fn progress(&self, title: &str, message: &str, until: Option<PathBuf>) -> ToastHandle {
        use std::io::Write;
        use std::process::Stdio;
        use std::sync::{Arc, Mutex};
        let Some(toast) = &self.toast else { return ToastHandle::default() };
        let mut command = std::process::Command::new(toast);
        command.args(["toast", title, message]);
        if let Some(until) = until {
            command.arg(until);
        }
        command.stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null());
        let Ok(mut child) = command.spawn() else { return ToastHandle::default() };
        // Each line it reads is its new message; it goes when its stdin closes: here, or when this process ends.
        let input = Arc::new(Mutex::new(child.stdin.take()));
        std::thread::spawn(move || {
            let _ = child.wait();
        });
        let sender = input.clone();
        ToastHandle {
            update: Box::new(move |line| {
                if let Some(stdin) = sender.lock().unwrap().as_mut() {
                    // A toast that died costs an update, not the launcher (Rust ignores SIGPIPE).
                    let _ = stdin.write_all(format!("{}\n", line.replace('\n', " ")).as_bytes());
                }
            }),
            close: Box::new(move || {
                input.lock().unwrap().take();
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pe_image;
    use crate::test_support::{make_temp_dir, make_tool_layout, pe_bytes, test_preflight, write, TEST_IDENTITY};
    use std::path::Path;

    fn request(verb: Verb, target: &Path) -> LaunchRequest {
        LaunchRequest { verb, target: target.to_string_lossy().into_owned(), arguments: vec![] }
    }

    fn exe(machine: u16) -> PathBuf {
        let path = make_temp_dir().join("My Game.exe");
        std::fs::write(&path, pe_bytes(machine, false)).unwrap();
        path
    }

    fn marker_exists(layout: &ToolLayout) -> bool {
        layout.runtime_damaged_marker().exists()
    }

    fn check(preflight: &Preflight, layout: &ToolLayout, verb: Verb, target: &Path) -> Result<String, PreflightError> {
        preflight.check(layout, &request(verb, target), false)
    }

    #[test]
    fn unsupported_system_is_refused_first() {
        let layout = ToolLayout::new(make_temp_dir()); // no wine.app either
        let preflight = Preflight::new(Box::new(|| false), Box::new(|_| None), Box::new(|_| true));
        assert_eq!(
            check(&preflight, &layout, Verb::WaitForExitAndRun, &exe(pe_image::I386)),
            Err(PreflightError::UnsupportedSystem)
        );
        assert!(!marker_exists(&layout));
        assert_eq!(
            PreflightError::UnsupportedSystem.to_string(),
            "MacNeutron needs macOS 27 or later on an Apple Silicon Mac."
        );
    }

    #[test]
    fn unreadable_identity_is_damaged_and_marked() {
        let layout = make_tool_layout();
        let preflight = Preflight::new(Box::new(|| true), Box::new(|_| None), Box::new(|_| true));
        assert_eq!(check(&preflight, &layout, Verb::Run, &layout.wine()), Err(PreflightError::RuntimeMissing));
        assert!(marker_exists(&layout));
        assert_eq!(
            PreflightError::RuntimeMissing.to_string(),
            "MacNeutron's runtime is missing or damaged. Open MacNeutron to repair it."
        );
    }

    #[test]
    fn missing_wineserver_is_damaged_and_marked() {
        let layout = make_tool_layout();
        std::fs::remove_file(layout.wineserver()).unwrap();
        assert_eq!(check(&test_preflight(), &layout, Verb::Run, &layout.wine()), Err(PreflightError::RuntimeMissing));
        assert!(marker_exists(&layout));
    }

    // A runtime built for one CPU (Resources/cpu: its name, then the features its code may use) refuses a Mac without one
    // of them, before anything runs; the generic build has no such file.
    #[test]
    fn a_runtime_built_for_a_newer_cpu_is_refused() {
        let layout = make_tool_layout();
        write("apple-m5\nFEAT_CSSC\nFEAT_SME2\nFEAT_HBC\n", &layout.cpu_file(), false);
        let probe = |has: fn(&str) -> bool| {
            Preflight::new(Box::new(|| true), Box::new(|_| Some(TEST_IDENTITY.to_string())), Box::new(has))
        };
        let m4 = probe(|f| f != "FEAT_SME2" && f != "FEAT_HBC");
        assert_eq!(
            check(&m4, &layout, Verb::Run, &layout.wine()),
            Err(PreflightError::UnsupportedCPU("apple-m5".into(), vec!["FEAT_SME2".into(), "FEAT_HBC".into()]))
        );
        assert_eq!(
            PreflightError::UnsupportedCPU("apple-m5".into(), vec!["FEAT_SME2".into()]).to_string(),
            "This MacNeutron runtime is built for Apple M5, and this Mac's processor lacks FEAT_SME2. Install the MacNeutron build for this Mac."
        );
        let m5 = probe(|_| true);
        assert_eq!(check(&m5, &layout, Verb::Run, &layout.wine()), Ok(TEST_IDENTITY.to_string()));
        std::fs::remove_file(layout.cpu_file()).unwrap();
        assert_eq!(check(&m4, &layout, Verb::Run, &layout.wine()), Ok(TEST_IDENTITY.to_string()));
        assert!(!marker_exists(&layout));
    }

    #[test]
    fn x64_and_arm64_targets_pass() {
        let layout = make_tool_layout();
        for machine in [pe_image::AMD64, pe_image::ARM64] {
            assert_eq!(
                check(&test_preflight(), &layout, Verb::WaitForExitAndRun, &exe(machine)),
                Ok(TEST_IDENTITY.to_string())
            );
        }
        assert!(!marker_exists(&layout));
    }

    #[test]
    fn i386_target_is_thirty_two_bit() {
        let layout = make_tool_layout();
        assert_eq!(
            check(&test_preflight(), &layout, Verb::WaitForExitAndRun, &exe(pe_image::I386)),
            Err(PreflightError::ThirtyTwoBit)
        );
        assert_eq!(
            PreflightError::ThirtyTwoBit.to_string(),
            "This game is 32-bit. On macOS MacNeutron runs 64-bit games only; 32-bit games in a Steam library run in its game VM."
        );
    }

    #[test]
    fn other_machine_is_named() {
        let layout = make_tool_layout();
        assert_eq!(
            check(&test_preflight(), &layout, Verb::WaitForExitAndRun, &exe(0x1c4)),
            Err(PreflightError::UnsupportedMachine(0x1c4))
        );
        assert_eq!(
            PreflightError::UnsupportedMachine(0x1c4).to_string(),
            "This game is built for ARM (32-bit), which MacNeutron can't run."
        );
        assert_eq!(
            PreflightError::UnsupportedMachine(0x200).to_string(),
            "This game is built for Itanium, which MacNeutron can't run."
        );
        assert_eq!(
            PreflightError::UnsupportedMachine(0x5032).to_string(),
            "This game is built for machine type 0x5032, which MacNeutron can't run."
        );
    }

    #[test]
    fn non_pe_target_is_not_checked() {
        let layout = make_tool_layout();
        let script = make_temp_dir().join("launch.bat");
        write("@echo off\r\nstart game.exe\r\n", &script, false);
        let ok = Ok(TEST_IDENTITY.to_string());
        assert_eq!(check(&test_preflight(), &layout, Verb::WaitForExitAndRun, &script), ok);
        let missing = make_temp_dir().join("Missing.exe");
        assert_eq!(check(&test_preflight(), &layout, Verb::WaitForExitAndRun, &missing), ok);
    }

    #[test]
    fn target_is_checked_only_for_wait_for_exit_and_run() {
        let layout = make_tool_layout();
        let i386 = exe(pe_image::I386);
        for verb in Verb::all().into_iter().filter(|v| *v != Verb::WaitForExitAndRun) {
            assert_eq!(check(&test_preflight(), &layout, verb, &i386), Ok(TEST_IDENTITY.to_string()));
        }
    }

    #[test]
    fn a_toasts_updates_reach_it_as_lines() {
        // A stand-in for `macneutron toast`: what it reads on stdin, until the launcher closes it.
        let dir = make_temp_dir();
        let (got, done, toast) = (dir.join("stdin.txt"), dir.join("done"), dir.join("toast"));
        write(&format!("#!/bin/sh\ncat > '{}'\n: > '{}'\n", got.display(), done.display()), &toast, true);
        let shown = AppleScriptNotifier::new(Some(toast)).progress("T", "M", None);
        (shown.update)("25% (5 of 20)");
        (shown.update)("50% (10 of 20)");
        (shown.close)();
        (shown.update)("late"); // after close: dropped, the launcher carries on
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
        while !done.exists() && std::time::Instant::now() < deadline {
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert_eq!(std::fs::read_to_string(&got).unwrap(), "25% (5 of 20)\n50% (10 of 20)\n");
    }

    #[test]
    fn names_cpus_and_machines() {
        assert_eq!(PreflightError::cpu_name("apple-m5"), "Apple M5");
        assert_eq!(PreflightError::cpu_name("generic"), "generic");
        assert_eq!(PreflightError::machine_name(0x1c4), "ARM (32-bit)");
        assert_eq!(PreflightError::machine_name(0x200), "Itanium");
        assert_eq!(PreflightError::machine_name(0x1234), "machine type 0x1234");
    }

    #[test]
    fn quotes_applescript_strings() {
        assert_eq!(AppleScriptNotifier::quoted(r#"say "hi" \o/"#), r#""say \"hi\" \\o/""#);
    }
}
