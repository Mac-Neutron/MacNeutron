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

    fn progress(&self, _title: &str, _message: &str, _until: Option<PathBuf>) -> ToastHandle {
        // A live toast needs the `macneutron toast` helper on the other end of a pipe, which the CLI provides.
        let _ = &self.toast;
        ToastHandle::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
