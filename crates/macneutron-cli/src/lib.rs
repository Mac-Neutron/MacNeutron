//! `macneutron` subcommands. Kept out of main.rs so they can be tested.

use std::collections::BTreeMap;
use std::ffi::CString;
use std::path::{Path, PathBuf};
use std::sync::{Arc, OnceLock};

use macneutron_core::launcher::Launcher;
use macneutron_core::launcher_log::LauncherLog;
use macneutron_core::preflight::AppleScriptNotifier;
use macneutron_core::runtime_installer::{
    default_identity, default_running_executables, standard_runner, RuntimeInstallOutcome, RuntimeInstaller,
};
use macneutron_core::tool_layout::ToolLayout;
use macneutron_core::vm_setup::VMSetup;
use macneutron_core::vm_setup_manifest::VMSetupManifest;

pub mod stop_requests;
pub mod toast;

pub const USAGE: &str = "usage: macneutron launch <verb> <target> [args...]\n       macneutron install --tool-dir <dir> --wine-app <wine.app> [--steam-exe <steam.exe>] [--force]\n       macneutron passthrough <verb> <command> [args...]\n       macneutron vm-setup --manifest <vm-manifest.json> --vm <dir> [--mkfs <mkfs.erofs>] [--min-free-gb <n>]\n       macneutron vm-setup --check-sources --manifest <vm-manifest.json>";

/// Runs one command line and returns its exit code.
pub fn run(args: &[String], environment: &BTreeMap<String, String>, executable: &Path) -> i32 {
    let Some((command, rest)) = args.split_first() else { return usage_error() };
    match command.as_str() {
        "launch" => launch(rest, environment, executable),
        "install" => install(rest, executable),
        "passthrough" => passthrough(rest, environment),
        "vm-setup" => vm_setup(rest, executable),
        _ => usage_error(),
    }
}

/// The process's single launch, set before it starts: Steam's quit request (StopRequests) ends its VM.
static LAUNCHING: OnceLock<Arc<Launcher>> = OnceLock::new();

/// Steam's Stop, a quit request to the launcher (macneutron's StopRequests): a microVM launch ends its VM.
pub fn quit_requested() {
    if let Some(launcher) = LAUNCHING.get() {
        let _ = launcher.stop_vm();
    }
}

fn launch(rest: &[String], environment: &BTreeMap<String, String>, executable: &Path) -> i32 {
    let mut launcher = Launcher::new(ToolLayout::for_executable(executable));
    launcher.notifier = Arc::new(AppleScriptNotifier::new(Some(executable.to_path_buf())));
    let launcher = Arc::new(launcher);
    install_termination_handlers(launcher.clone(), environment.clone());
    let _ = LAUNCHING.set(launcher.clone());
    launcher.launch(rest, environment)
}

/// Steam's Stop button (SIGTERM) and Ctrl-C kill the game's Wine processes, then exit. The signals are blocked and a
/// thread waits for them (Swift's DispatchSource): `terminate` starts a process and takes locks, which a signal
/// handler can't. Threads started later inherit the mask; children get an empty one (std's Command resets it).
fn install_termination_handlers(launcher: Arc<Launcher>, environment: BTreeMap<String, String>) {
    let set = block_termination_signals();
    std::thread::spawn(move || {
        let mut signal = 0;
        if unsafe { libc::sigwait(&set, &mut signal) } == 0 {
            launcher.terminate(&environment);
            std::process::exit(128 + signal);
        }
    });
}

/// SIGTERM and SIGINT blocked on the calling thread and every thread it starts later, for a sigwait thread to take.
/// [`stop_requests::run`] blocks them on the main thread before its launch thread starts, so AppKit's main thread
/// can't take Steam's SIGTERM with the default action.
pub fn block_termination_signals() -> libc::sigset_t {
    let mut set: libc::sigset_t = 0;
    unsafe {
        libc::sigemptyset(&mut set);
        libc::sigaddset(&mut set, libc::SIGTERM);
        libc::sigaddset(&mut set, libc::SIGINT);
        libc::pthread_sigmask(libc::SIG_BLOCK, &set, std::ptr::null_mut());
    }
    set
}

fn install(rest: &[String], executable: &Path) -> i32 {
    let mut args: Vec<String> = rest.to_vec();
    let force = args.iter().any(|a| a == "--force");
    args.retain(|a| a != "--force");
    // Both required: the checks assemble their own tool folders, never the installed one.
    let (Some(dir), Some(wine_app)) = (option("--tool-dir", &mut args), option("--wine-app", &mut args)) else {
        return usage_error();
    };
    let steam_exe = option("--steam-exe", &mut args);
    if !args.is_empty() {
        return usage_error();
    }
    if let Some(steam_exe) = &steam_exe {
        if !Path::new(steam_exe).exists() {
            return failure(&format!("macneutron: no such file: {steam_exe}"));
        }
    }
    let layout = ToolLayout::new(PathBuf::from(&dir));
    let log = LauncherLog::standard();
    let outcome = RuntimeInstaller::install(
        Path::new(&wine_app),
        &layout,
        executable,
        steam_exe.as_deref().map(Path::new),
        force,
        &standard_runner(),
        &default_running_executables,
        default_identity,
        &log,
    );
    match outcome {
        Ok((outcome, _)) => {
            let label = ToolLayout::runtime_label(layout.runtime_version().as_deref(), layout.identity().as_deref());
            println!("{}", install_message(&outcome, &label));
            install_exit_code(&outcome)
        }
        Err(error) => failure(&format!("macneutron: {error}")),
    }
}

fn passthrough(rest: &[String], environment: &BTreeMap<String, String>) -> i32 {
    // Steam's Mac-game tool: `<verb> <command> [args…]`. Replacing this process keeps Steam tracking its PID.
    if rest.len() < 2 {
        return usage_error();
    }
    let target = passthrough_target(&rest[1]);
    let arguments: Vec<String> = rest[2..].to_vec();
    match spawn(&target, &arguments, environment, true, None) {
        Ok(_) => 1, // a successful exec doesn't return
        Err(code) => {
            let message = std::io::Error::from_raw_os_error(code).to_string();
            eprintln!("macneutron: can't run {}: {message}", target.display());
            1
        }
    }
}

fn vm_setup(rest: &[String], executable: &Path) -> i32 {
    let mut args: Vec<String> = rest.to_vec();
    if args.first().map(String::as_str) == Some("--check-sources") {
        // the release: every pinned package still downloads from somewhere
        args.remove(0);
        let Some(manifest) = option("--manifest", &mut args) else { return usage_error() };
        if !args.is_empty() {
            return usage_error();
        }
        let Ok(text) = std::fs::read_to_string(&manifest) else {
            return failure(&format!("macneutron: no such file: {manifest}"));
        };
        let Ok(value) = VMSetupManifest::parse(&text) else {
            return failure("macneutron: the manifest isn't JSON");
        };
        let missing = VMSetup::missing_sources(&value, &|url| VMSetup::head(url));
        for file in &missing {
            eprintln!("vm-setup: no source serves {file}");
        }
        return if missing.is_empty() { 0 } else { 1 };
    }
    // Both required: tests and the gate set up their own folders, never the installed one.
    let (Some(manifest), Some(vm)) = (option("--manifest", &mut args), option("--vm", &mut args)) else {
        return usage_error();
    };
    let mkfs = option("--mkfs", &mut args).map(PathBuf::from).unwrap_or_else(|| {
        executable.parent().unwrap_or(Path::new("/")).join("mkfs.erofs")
    });
    let min_free = option("--min-free-gb", &mut args).and_then(|text| text.parse::<i64>().ok()).unwrap_or(8);
    if !args.is_empty() {
        return usage_error();
    }
    // The app quitting sends SIGTERM: tar and mkfs.erofs end with this process, not after it.
    install_setup_handlers();
    let fetch = macneutron_core::vm_downloads::standard_fetch();
    let pause: macneutron_core::vm_downloads::Pause = std::sync::Arc::new(macneutron_core::vm_downloads::VMDownloads::backoff);
    let progress: &'static (dyn Fn(&str) + Sync) = {
        fn report(text: &str) {
            println!("{text}");
        }
        &report
    };
    match VMSetup::run(
        Path::new(&manifest),
        Path::new(&vm),
        &mkfs,
        min_free,
        &fetch,
        &pause,
        &default_running_executables,
        progress,
    ) {
        Ok(code) => code,
        Err(error) => failure(&format!("{error}")),
    }
}

/// Removes `--name value` from `args` and returns the value.
pub fn option(name: &str, args: &mut Vec<String>) -> Option<String> {
    let index = args.iter().position(|a| a == name)?;
    if index + 1 >= args.len() {
        return None;
    }
    let value = args[index + 1].clone();
    args.drain(index..=index + 1);
    Some(value)
}

/// 0 when installed or unchanged, 3 when deferred.
pub fn install_exit_code(outcome: &RuntimeInstallOutcome) -> i32 {
    if matches!(outcome, RuntimeInstallOutcome::Deferred(_)) {
        3
    } else {
        0
    }
}

pub fn install_message(outcome: &RuntimeInstallOutcome, label: &str) -> String {
    match outcome {
        RuntimeInstallOutcome::Installed => format!("installed {label}"),
        RuntimeInstallOutcome::Unchanged => format!("unchanged {label}"),
        RuntimeInstallOutcome::Deferred(path) => format!("deferred: {path} is running"),
    }
}

/// `<app>.app` → `<app>.app/Contents/MacOS/<CFBundleExecutable>`, as `passthrough.sh` did; anything else unchanged.
/// Wider than the script: a folder without a readable Info.plist resolves to its own name (the script exec'd the
/// folder, which fails).
pub fn passthrough_target(path: &str) -> PathBuf {
    let url = Path::new(path);
    if !std::fs::metadata(url).map(|m| m.is_dir()).unwrap_or(false) {
        return url.to_path_buf();
    }
    let name = read_bundle_executable(url).unwrap_or_else(|| {
        url.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default()
    });
    url.join("Contents/MacOS").join(name)
}

fn read_bundle_executable(app: &Path) -> Option<String> {
    let text = std::fs::read(app.join("Contents/Info.plist")).ok()?;
    let value = plist::Value::from_reader(std::io::Cursor::new(text)).ok()?;
    value.as_dictionary()?.get("CFBundleExecutable")?.as_string().map(|s| s.to_string())
}

/// arm64e, arm64, x86_64: what `arch -arm64e -arm64 -x86_64` does. Steam itself spawns tools preferring x86_64.
/// The values are `<mach/machine.h>`'s, which libc doesn't re-export.
pub const CPU_TYPE_X86_64: libc::cpu_type_t = 0x0100_0007;
pub const CPU_TYPE_ARM64: libc::cpu_type_t = 0x0100_000c;
pub const CPU_SUBTYPE_X86_64_ALL: libc::cpu_subtype_t = 3;
pub const CPU_SUBTYPE_ARM64_ALL: libc::cpu_subtype_t = 0;
pub const CPU_SUBTYPE_ARM64E: libc::cpu_subtype_t = 2;

pub const PASSTHROUGH_ARCHITECTURES: [(libc::cpu_type_t, libc::cpu_subtype_t); 3] = [
    (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64E),
    (CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL),
    (CPU_TYPE_X86_64, CPU_SUBTYPE_X86_64_ALL),
];

/// `posix_spawn`s `executable` with [`PASSTHROUGH_ARCHITECTURES`] as its slice preference and returns its pid.
/// `replacing_this_process` (`POSIX_SPAWN_SETEXEC`) execs in place and returns only on failure.
pub fn spawn(
    executable: &Path,
    arguments: &[String],
    environment: &BTreeMap<String, String>,
    replacing_this_process: bool,
    standard_output: Option<&Path>,
) -> Result<libc::pid_t, i32> {
    unsafe {
        let mut attributes: libc::posix_spawnattr_t = std::mem::zeroed();
        let mut actions: libc::posix_spawn_file_actions_t = std::mem::zeroed();
        let mut status = libc::posix_spawnattr_init(&mut attributes);
        if status == 0 {
            status = libc::posix_spawn_file_actions_init(&mut actions);
        }
        let mut types: Vec<libc::cpu_type_t> = PASSTHROUGH_ARCHITECTURES.iter().map(|a| a.0).collect();
        let mut subtypes: Vec<libc::cpu_subtype_t> = PASSTHROUGH_ARCHITECTURES.iter().map(|a| a.1).collect();
        let mut set: libc::size_t = 0;
        if status == 0 {
            status = libc::posix_spawnattr_setarchpref_np(
                &mut attributes,
                types.len(),
                types.as_mut_ptr(),
                subtypes.as_mut_ptr(),
                &mut set,
            );
        }
        if status == 0 && replacing_this_process {
            status = libc::posix_spawnattr_setflags(&mut attributes, libc::POSIX_SPAWN_SETEXEC as i16);
        }
        if status == 0 {
            if let Some(output) = standard_output {
                if let Ok(path) = CString::new(output.as_os_str().as_encoded_bytes()) {
                    status = libc::posix_spawn_file_actions_addopen(
                        &mut actions,
                        1,
                        path.as_ptr(),
                        libc::O_WRONLY | libc::O_CREAT | libc::O_TRUNC,
                        0o644,
                    );
                }
            }
        }
        let path = CString::new(executable.as_os_str().as_encoded_bytes()).unwrap_or_default();
        let mut argv: Vec<*mut libc::c_char> = Vec::with_capacity(arguments.len() + 2);
        argv.push(libc::strdup(path.as_ptr()));
        for argument in arguments {
            if let Ok(argument) = CString::new(argument.as_str()) {
                argv.push(libc::strdup(argument.as_ptr()));
            }
        }
        argv.push(std::ptr::null_mut());
        let mut envp: Vec<*mut libc::c_char> = environment
            .iter()
            .filter_map(|(key, value)| CString::new(format!("{key}={value}")).ok())
            .map(|entry| libc::strdup(entry.as_ptr()))
            .collect();
        envp.push(std::ptr::null_mut());
        let mut pid: libc::pid_t = 0;
        if status == 0 {
            status = libc::posix_spawn(&mut pid, path.as_ptr(), &actions, &attributes, argv.as_ptr(), envp.as_ptr());
        }
        for pointer in argv.into_iter().chain(envp) {
            if !pointer.is_null() {
                libc::free(pointer as *mut libc::c_void);
            }
        }
        libc::posix_spawn_file_actions_destroy(&mut actions);
        libc::posix_spawnattr_destroy(&mut attributes);
        if status != 0 {
            return Err(status);
        }
        Ok(pid)
    }
}

/// Steam's Stop button (SIGTERM) and Ctrl-C end vm-setup's children, then exit.
fn install_setup_handlers() {
    for signal in [libc::SIGTERM, libc::SIGINT] {
        unsafe { libc::signal(signal, stop_setup as *const () as libc::sighandler_t) };
    }
}

extern "C" fn stop_setup(signal: libc::c_int) {
    macneutron_core::vm_setup::Children::terminate_all(None);
    std::process::exit(128 + signal)
}

fn usage_error() -> i32 {
    eprintln!("{USAGE}");
    2
}

fn failure(message: &str) -> i32 {
    eprintln!("{message}");
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    /// A fresh temp directory whose path contains a space, like Steam's "Application Support".
    fn temp_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let unique = format!("mn-cli-{}-{}-test", std::process::id(), COUNTER.fetch_add(1, Ordering::Relaxed));
        let path = std::env::temp_dir().join(unique);
        std::fs::create_dir_all(&path).unwrap();
        path
    }

    fn write(text: &str, to: &Path, executable: bool) {
        if let Some(parent) = to.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        std::fs::write(to, text).unwrap();
        if executable {
            use std::os::unix::fs::PermissionsExt;
            let mut permissions = std::fs::metadata(to).unwrap().permissions();
            permissions.set_mode(0o755);
            std::fs::set_permissions(to, permissions).unwrap();
        }
    }

    /// An ad-hoc-signed `dir/wine.app` (its Contents/MacOS/wine is a copy of the loader, never run).
    fn make_signed_wine_app(at: &Path) -> PathBuf {
        let bundle = at.join("wine.app");
        let macos = bundle.join("Contents/MacOS");
        std::fs::create_dir_all(&macos).unwrap();
        let mut plist = plist::Dictionary::new();
        plist.insert("CFBundleExecutable".into(), plist::Value::from("wine"));
        plist.insert("CFBundleIdentifier".into(), plist::Value::from("test.wine"));
        plist.insert("CFBundleShortVersionString".into(), plist::Value::from("test"));
        plist.insert("CFBundleVersion".into(), plist::Value::from("1"));
        plist::Value::Dictionary(plist)
            .to_file_xml(bundle.join("Contents/Info.plist"))
            .unwrap();
        std::fs::copy("/usr/bin/true", macos.join("wine")).unwrap();
        let status = std::process::Command::new("/usr/bin/codesign")
            .args(["-s", "-", "-f"])
            .arg(&bundle)
            .status()
            .unwrap();
        assert!(status.success(), "codesign failed for {}", bundle.display());
        bundle
    }

    fn run_with(args: &[&str], executable: &Path) -> i32 {
        let args: Vec<String> = args.iter().map(|a| a.to_string()).collect();
        run(&args, &BTreeMap::new(), executable)
    }

    fn contents(dir: &Path) -> Vec<String> {
        let mut names: Vec<String> = std::fs::read_dir(dir)
            .unwrap()
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        names.sort();
        names
    }

    #[test]
    fn unknown_command_prints_usage() {
        let executable = Path::new("/x");
        assert_eq!(run_with(&["frobnicate"], executable), 2);
        assert_eq!(run_with(&[], executable), 2);
        assert!(USAGE.contains("macneutron install --tool-dir"));
        assert!(USAGE.contains("macneutron passthrough <verb>"));
    }

    #[test]
    fn option_parsing_removes_the_flag_and_value() {
        let mut args = vec!["--tool-dir".to_string(), "/a b/tool".to_string(), "/Volumes/GPTK".to_string()];
        assert_eq!(option("--tool-dir", &mut args).as_deref(), Some("/a b/tool"));
        assert_eq!(args, vec![String::from("/Volumes/GPTK")]);
        assert_eq!(option("--tarball", &mut args), None);
    }

    #[test]
    fn install_requires_tool_dir() {
        // Never the real tool folder by default: the checks assemble their own.
        let dir = temp_dir();
        let source = make_signed_wine_app(&dir);
        let before = contents(&dir);
        assert_eq!(
            run_with(&["install", "--wine-app", &source.to_string_lossy()], Path::new("/x")),
            2
        );
        assert_eq!(contents(&dir), before);
    }

    #[test]
    fn install_requires_wine_app() {
        let tool = temp_dir().join("tool dir");
        assert_eq!(
            run_with(&["install", "--tool-dir", &tool.to_string_lossy()], Path::new("/x")),
            2
        );
        assert!(!tool.exists());
    }

    #[test]
    fn a_missing_steam_exe_changes_nothing() {
        let tool = temp_dir().join("tool dir");
        let source = make_signed_wine_app(&temp_dir());
        assert_eq!(
            run_with(
                &[
                    "install",
                    "--tool-dir",
                    &tool.to_string_lossy(),
                    "--wine-app",
                    &source.to_string_lossy(),
                    "--steam-exe",
                    "/no such/steam.exe",
                ],
                Path::new("/x"),
            ),
            1
        );
        assert!(!tool.exists());
    }

    #[test]
    fn install_exit_codes() {
        assert_eq!(install_exit_code(&RuntimeInstallOutcome::Installed), 0);
        assert_eq!(install_exit_code(&RuntimeInstallOutcome::Unchanged), 0);
        assert_eq!(
            install_exit_code(&RuntimeInstallOutcome::Deferred("/t/wine.app/Contents/MacOS/wine".into())),
            3
        );
    }

    #[test]
    fn install_messages() {
        let label = "dev (0123456789ab)";
        assert_eq!(install_message(&RuntimeInstallOutcome::Installed, label), "installed dev (0123456789ab)");
        assert_eq!(install_message(&RuntimeInstallOutcome::Unchanged, label), "unchanged dev (0123456789ab)");
        assert_eq!(
            install_message(&RuntimeInstallOutcome::Deferred("/t/wine.app/Contents/MacOS/wine".into()), label),
            "deferred: /t/wine.app/Contents/MacOS/wine is running"
        );
    }

    #[test]
    fn app_bundle_resolves_to_its_executable() {
        // Steam's launch entry for many Mac games is the .app folder itself (Timberborn's is).
        let app = temp_dir().join("Gam\u{e9} Folder/My Game.app");
        write("#!/bin/sh\n", &app.join("Contents/MacOS/Game Binary"), true);
        let mut plist = plist::Dictionary::new();
        plist.insert("CFBundleExecutable".into(), plist::Value::from("Game Binary"));
        plist::Value::Dictionary(plist)
            .to_file_xml(app.join("Contents/Info.plist"))
            .unwrap();
        assert_eq!(
            passthrough_target(&app.to_string_lossy()).to_string_lossy(),
            app.join("Contents/MacOS/Game Binary").to_string_lossy()
        );
    }

    #[test]
    fn app_without_plist_falls_back_to_its_name() {
        let app = temp_dir().join("My Game.app");
        write("#!/bin/sh\n", &app.join("Contents/MacOS/My Game"), true);
        assert_eq!(
            passthrough_target(&app.to_string_lossy()).to_string_lossy(),
            app.join("Contents/MacOS/My Game").to_string_lossy()
        );
    }

    #[test]
    fn plain_path_is_kept() {
        assert_eq!(passthrough_target("/bin/echo").to_string_lossy(), "/bin/echo");
        assert_eq!(passthrough_target("/no such/game").to_string_lossy(), "/no such/game");
    }

    #[test]
    fn preference_is_arm64e_arm64_then_x86() {
        // What `arch -arm64e -arm64 -x86_64` does; Steam itself spawns tools preferring x86_64.
        assert_eq!(
            PASSTHROUGH_ARCHITECTURES.map(|a| a.0),
            [CPU_TYPE_ARM64, CPU_TYPE_ARM64, CPU_TYPE_X86_64]
        );
        assert_eq!(
            PASSTHROUGH_ARCHITECTURES.map(|a| a.1),
            [CPU_SUBTYPE_ARM64E, CPU_SUBTYPE_ARM64_ALL, CPU_SUBTYPE_X86_64_ALL]
        );
    }

    #[test]
    fn spawn_runs_the_target_and_returns_its_pid() {
        // /usr/bin/arch is universal (arm64e, x86_64); it prints the slice that runs (no newline unless to a terminal).
        let out = temp_dir().join("arch out.txt");
        let environment: BTreeMap<String, String> = [("PATH".to_string(), "/usr/bin:/bin".to_string())].into_iter().collect();
        let pid = spawn(Path::new("/usr/bin/arch"), &[], &environment, false, Some(&out)).unwrap();
        let mut status: libc::c_int = 0;
        assert_eq!(unsafe { libc::waitpid(pid, &mut status, 0) }, pid);
        assert_eq!(status, 0);
        assert_eq!(std::fs::read_to_string(&out).unwrap(), "arm64");
    }

    #[test]
    fn vm_setup_needs_a_manifest_and_a_folder() {
        // Never the real VM folder by default.
        assert_eq!(
            run_with(&["vm-setup", "--manifest", "/nonexistent.json"], Path::new("/x")),
            2
        );
        assert_eq!(
            run_with(&["vm-setup", "--vm", &temp_dir().to_string_lossy()], Path::new("/x")),
            2
        );
        // With both, it runs: a developer's folder (images saying `dev`) is left alone, and that's a success.
        let link = temp_dir().join("vm");
        let work = temp_dir();
        write("dev\n", &work.join("images/manifest.sha256"), false);
        std::os::unix::fs::symlink(&work, &link).unwrap();
        let manifest = temp_dir().join("vm-manifest.json");
        write("{}", &manifest, false);
        assert_eq!(
            run_with(
                &["vm-setup", "--manifest", &manifest.to_string_lossy(), "--vm", &link.to_string_lossy()],
                Path::new("/x"),
            ),
            0
        );
    }
}
