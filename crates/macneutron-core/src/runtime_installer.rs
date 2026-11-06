//! Installs `wine.app` and the Steam-facing tool files into a `macneutron` tool folder (spec §3.9).

use std::collections::BTreeMap;
use std::fmt;
use std::path::{Path, PathBuf};

use crate::code_identity::CodeIdentity;
use crate::key_values::{node_at, KeyValues};
use crate::launcher_log::LauncherLog;
use crate::mapping_planner::MappingPlanner;
use crate::posix;
use crate::process_runner::{ProcessRunner, SystemProcessRunner};
use crate::running_processes;
use crate::tool_layout::ToolLayout;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RuntimeInstallOutcome {
    Installed,
    Unchanged,
    /// A process runs from the installed runtime (its kernel path); nothing was changed.
    Deferred(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RuntimeInstallError {
    /// The source has no `Contents/MacOS/wine`.
    NotAWineApp(String),
    /// `codesign --verify --strict`'s exit status for the copy.
    SignatureInvalid(i32),
    /// `renamex_np`'s or `rename`'s errno.
    SwapFailed(i32),
    /// `cp -c -R`'s exit status (a full copy, outside an APFS clone, can run out of space).
    CopyFailed(i32),
    /// A copy through `copyItem` or a `rename` that the filesystem refused.
    Io(String),
}

impl fmt::Display for RuntimeInstallError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RuntimeInstallError::NotAWineApp(path) => {
                write!(f, "{path} isn't a runtime (it has no Contents/MacOS/wine).")
            }
            RuntimeInstallError::SignatureInvalid(status) => {
                write!(f, "The runtime's signature check failed (codesign exit {status}).")
            }
            RuntimeInstallError::SwapFailed(error) => {
                write!(f, "Couldn't put the new runtime in place: {}.", errno_text(*error))
            }
            RuntimeInstallError::CopyFailed(status) => write!(
                f,
                "Couldn't copy the runtime into the tool folder (cp exit {status}); check free disk space."
            ),
            RuntimeInstallError::Io(text) => write!(f, "{text}"),
        }
    }
}

impl std::error::Error for RuntimeInstallError {}

fn errno_text(error: i32) -> String {
    std::io::Error::from_raw_os_error(error).to_string()
}

/// The identity a bundle reports: `CodeIdentity::of` by default, injectable for tests.
pub type Identity = fn(&Path) -> Option<String>;

pub struct RuntimeInstaller;

impl RuntimeInstaller {
    /// The runtime tool, and with `legacy_name` its pre-rename name too, both from this folder: a mapping must never
    /// name a tool Steam doesn't know.
    pub fn compatibility_tool(legacy_name: bool) -> String {
        fn entry(name: &str, display: &str) -> String {
            format!(
                "\"{name}\"\n{{\n  \"install_path\" \".\"\n  \"display_name\" \"{display}\"\n  \"from_oslist\"  \"windows\"\n  \"to_oslist\"    \"linux\"\n}}\n\n"
            )
        }
        let legacy = if legacy_name {
            entry(MappingPlanner::LEGACY_RUNTIME_TOOL, "MacNeutron (old)")
        } else {
            String::new()
        };
        format!(
            "\"compatibilitytools\"\n{{\n  \"compat_tools\"\n  {{\n{}{legacy}  }}\n}}\n",
            entry(MappingPlanner::RUNTIME_TOOL, "MacNeutron")
        )
    }

    /// The tool folder's `compatibilitytool.vdf` still registers the pre-rename name.
    pub fn registers_legacy_name(in_root: &Path) -> bool {
        let Ok(text) = std::fs::read_to_string(in_root.join("compatibilitytool.vdf")) else {
            return false;
        };
        let Ok(nodes) = KeyValues::parse(&text) else { return false };
        node_at(&nodes, &["compatibilitytools", "compat_tools", MappingPlanner::LEGACY_RUNTIME_TOOL]).is_some()
    }

    pub fn write_compatibility_tool(in_root: &Path, legacy_name: bool) -> std::io::Result<()> {
        write_atomic(&in_root.join("compatibilitytool.vdf"), Self::compatibility_tool(legacy_name).as_bytes())
    }

    /// Steam runs the thin arm64 CLI directly (R0b passed); there's no `/bin/sh` stub in between.
    pub const TOOL_MANIFEST: &'static str = "\"manifest\"\n{\n  \"version\" \"2\"\n  \"commandline\" \"/bin/macneutron launch %verb%\"\n}\n\n";

    /// Installs `wine_app` into `layout`, unless a process runs from the installed runtime (a new client can't talk to
    /// an older running wineserver). The copy is checked before it replaces the old one in a single swap, so there is
    /// never a moment without a working `wine.app`. Tool files are written on every install that isn't deferred, so
    /// the CLI and the runtime change together.
    #[allow(clippy::too_many_arguments)]
    pub fn install(
        wine_app: &Path,
        layout: &ToolLayout,
        launcher_binary: &Path,
        steam_exe: Option<&Path>,
        force: bool,
        runner: &dyn ProcessRunner,
        running_executables: &dyn Fn() -> Vec<String>,
        identity: Identity,
        log: &LauncherLog,
    ) -> Result<(RuntimeInstallOutcome, Vec<String>), RuntimeInstallError> {
        let source_path = wine_app.to_string_lossy().into_owned();
        // 1. The kernel reports real paths (/private/var/…), so compare against the folders' real paths.
        let watched: Vec<String> = [layout.wine_app(), layout.root.join("Libraries")]
            .iter()
            .filter_map(|p| posix::real_path(p))
            .map(|p| format!("{p}/"))
            .collect();
        let running = |log: &LauncherLog| -> Option<String> {
            running_executables()
                .into_iter()
                .find(|path| watched.iter().any(|root| path.starts_with(root.as_str())))
                .map(|path| {
                    log.append(&format!("install deferred: {path} is running"));
                    path
                })
        };
        if !wine_app.join("Contents/MacOS/wine").is_file() || !is_executable(&wine_app.join("Contents/MacOS/wine")) {
            return Err(RuntimeInstallError::NotAWineApp(source_path));
        }
        if let Some(path) = running(log) {
            return Ok((RuntimeInstallOutcome::Deferred(path), Vec::new()));
        }
        // 2. Leftovers of an interrupted install (`cp -R` into an existing folder would nest the source in it).
        // These paths have no trailing "/", for cp and rename(2).
        let new = layout.root.join("wine.app.new");
        let _ = std::fs::remove_dir_all(&new);
        let _ = std::fs::remove_dir_all(layout.root.join("wine.app.old"));
        // 3.
        let damaged = layout.runtime_damaged_marker().exists();
        let installed = identity(&layout.wine_app());
        let mut calls = Vec::new();
        let outcome = if !force && !damaged && installed.is_some() && installed == identity(wine_app) {
            RuntimeInstallOutcome::Unchanged
        } else {
            // 4. An APFS clone on the same volume, a full copy otherwise.
            std::fs::create_dir_all(&layout.root).map_err(|e| RuntimeInstallError::Io(e.to_string()))?;
            let new_path = new.to_string_lossy().into_owned();
            calls.push(format!("cp -c -R {source_path} {new_path}"));
            let copied = runner
                .run(
                    Path::new("/bin/cp"),
                    &["-c".into(), "-R".into(), source_path.clone(), new_path.clone()],
                    &BTreeMap::new(),
                    None,
                )
                .unwrap_or(-1);
            if copied != 0 {
                let _ = std::fs::remove_dir_all(&new);
                return Err(RuntimeInstallError::CopyFailed(copied));
            }
            calls.push(format!("codesign --verify --strict {new_path}"));
            let verified = runner
                .run(
                    Path::new("/usr/bin/codesign"),
                    &["--verify".into(), "--strict".into(), new_path.clone()],
                    &BTreeMap::new(),
                    None,
                )
                .unwrap_or(-1);
            if verified != 0 {
                let _ = std::fs::remove_dir_all(&new);
                return Err(RuntimeInstallError::SignatureInvalid(verified));
            }
            // Again: the copy and its check take seconds, and a game started meanwhile runs from the old copy.
            if let Some(path) = running(log) {
                let _ = std::fs::remove_dir_all(&new);
                return Ok((RuntimeInstallOutcome::Deferred(path), calls));
            }
            let target = layout.root.join("wine.app");
            let swapped = if target.exists() { posix::rename_swap(&new, &target) } else { posix::rename(&new, &target) };
            if !swapped {
                let error = std::io::Error::last_os_error().raw_os_error().unwrap_or(0);
                let _ = std::fs::remove_dir_all(&new);
                return Err(RuntimeInstallError::SwapFailed(error));
            }
            let _ = std::fs::remove_dir_all(&new); // the old copy, after a swap
            let _ = std::fs::remove_file(layout.runtime_damaged_marker());
            RuntimeInstallOutcome::Installed
        };
        // 5.
        Self::write_tool_files(layout, launcher_binary, steam_exe).map_err(|e| RuntimeInstallError::Io(e.to_string()))?;
        // 6. The Rosetta-era runtime's entries, and its /bin/sh stub.
        for name in ToolLayout::ROSETTA_ERA_ENTRIES.iter().chain(std::iter::once(&"proton")) {
            let path = layout.root.join(name);
            let _ = std::fs::remove_dir_all(&path);
            let _ = std::fs::remove_file(&path);
        }
        Ok((outcome, calls))
    }

    /// Writes Steam's tool files, then installs the launcher and `steam_exe`, or the `steam.exe` it finds beside the
    /// launcher. Safe to repeat: identical files are skipped, and new ones are renamed into place, so a game Steam
    /// launches meanwhile never finds the launcher missing.
    pub fn write_tool_files(layout: &ToolLayout, launcher_binary: &Path, steam_exe: Option<&Path>) -> std::io::Result<()> {
        std::fs::create_dir_all(&layout.root)?;
        Self::install_file(launcher_binary, &layout.launcher_binary())?; // before the manifest that runs it
        // The legacy name goes only once Steam Play mode has moved its mappings (SteamPlayMode.finishRename).
        let legacy = Self::registers_legacy_name(&layout.root);
        Self::write_compatibility_tool(&layout.root, legacy)?;
        write_atomic(&layout.root.join("toolmanifest.vdf"), Self::TOOL_MANIFEST.as_bytes())?;
        // Next to the launcher, or in MacNeutron.app's Contents/Resources: steam.exe isn't Mach-O code,
        // so codesign won't accept it in Contents/Helpers.
        let helpers = launcher_binary.parent().unwrap_or_else(|| Path::new("/"));
        let candidates = [helpers.join("steam.exe"), helpers.parent().unwrap_or(Path::new("/")).join("Resources/steam.exe")];
        let found = steam_exe.map(|p| p.to_path_buf()).or_else(|| candidates.into_iter().find(|p| p.exists()));
        if let Some(steam_exe) = found {
            Self::install_file(&steam_exe, &layout.steam_helper())?;
        }
        Ok(())
    }

    /// Copies `source` to `destination` through a temporary file and `rename(2)`, unless they already match. The copy
    /// carries no quarantine.
    pub fn install_file(source: &Path, destination: &Path) -> std::io::Result<()> {
        if posix::same_path(source, destination) {
            return Ok(());
        }
        let contents = std::fs::read(source)?;
        if std::fs::read(destination).map(|d| d == contents).unwrap_or(false) {
            posix::remove_xattr(destination, "com.apple.quarantine"); // an earlier copy's
            return Ok(());
        }
        if let Some(parent) = destination.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let mut temporary = destination.as_os_str().to_os_string();
        temporary.push(".new");
        let temporary = PathBuf::from(temporary);
        let _ = std::fs::remove_file(&temporary);
        std::fs::copy(source, &temporary)?;
        // The copy keeps a downloaded app's quarantine, and Steam execs these files directly (no stapled ticket).
        posix::remove_xattr(&temporary, "com.apple.quarantine");
        if !posix::rename(&temporary, destination) {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }
}

fn is_executable(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    std::fs::metadata(path).map(|m| m.permissions().mode() & 0o111 != 0).unwrap_or(false)
}

/// Writes `bytes` to `path` through a temporary file and a rename, so a reader never sees a half-written file.
fn write_atomic(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let mut temporary = path.as_os_str().to_os_string();
    temporary.push(".new");
    let temporary = PathBuf::from(temporary);
    std::fs::write(&temporary, bytes)?;
    if !posix::rename(&temporary, path) {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}

/// What the CLI's `install` uses when nothing is injected: the real system's identity and process scan.
pub fn default_identity(bundle: &Path) -> Option<String> {
    CodeIdentity::of(bundle)
}

pub fn default_running_executables() -> Vec<String> {
    running_processes::executable_paths()
}

pub fn standard_runner() -> SystemProcessRunner {
    SystemProcessRunner::new()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::code_identity::CodeIdentity;
    use crate::key_values::{node_at, KeyValues};
    use crate::launcher_log::LauncherLog;
    use crate::test_support::{make_signed_wine_app, make_temp_dir, write, FakeRunner};

    /// Stands in for bin/macneutron: prints each argument on its own line.
    fn make_echo_launcher() -> PathBuf {
        let url = make_temp_dir().join("macneutron");
        write("#!/bin/sh\nfor a in \"$@\"; do echo \"$a\"; done\n", &url, true);
        url
    }

    /// A tool folder whose installed `wine.app` has identity `installed`.
    fn make_installed_layout(id: &str) -> ToolLayout {
        let layout = crate::test_support::make_tool_layout();
        write(id, &layout.wine_app().join("Contents/id"), false);
        layout
    }

    /// A fake, unsigned `dir/wine.app` whose identity is the text of `Contents/id`, so it travels with the bundle.
    fn make_fake_wine_app(dir: &Path, id: &str) -> PathBuf {
        let app = dir.join("wine.app");
        write("#!/bin/sh\n", &app.join("Contents/MacOS/wine"), true);
        write(id, &app.join("Contents/id"), false);
        app
    }

    fn fake_identity(bundle: &Path) -> Option<String> {
        std::fs::read_to_string(bundle.join("Contents/id")).ok()
    }

    fn exists(path: &Path) -> bool {
        path.exists()
    }

    #[allow(clippy::too_many_arguments)]
    fn install_fake(
        source: &Path,
        layout: &ToolLayout,
        launcher: Option<&Path>,
        steam_exe: Option<&Path>,
        force: bool,
        codesign_status: i32,
        cp_status: i32,
        running: &[String],
        scans: Option<Vec<Vec<String>>>,
        log_dir: Option<PathBuf>,
    ) -> (Result<RuntimeInstallOutcome, RuntimeInstallError>, FakeRunner, LauncherLog) {
        let runner = FakeRunner::new(move |call| {
            if call.tool == "cp" {
                if cp_status != 0 {
                    return cp_status;
                }
                let _ = copy_recursive(Path::new(&call.arguments[2]), Path::new(&call.arguments[3]));
            }
            if call.tool == "codesign" {
                codesign_status
            } else {
                0
            }
        });
        let answers = std::cell::RefCell::new(scans.unwrap_or_else(|| vec![running.to_vec()]));
        let launcher = launcher.map(|p| p.to_path_buf()).unwrap_or_else(make_echo_launcher);
        let log = LauncherLog::new(log_dir.unwrap_or_else(make_temp_dir));
        let outcome = RuntimeInstaller::install(
            source,
            layout,
            &launcher,
            steam_exe,
            force,
            &runner,
            &|| {
                let mut answers = answers.borrow_mut();
                if answers.len() > 1 {
                    answers.remove(0)
                } else {
                    answers[0].clone()
                }
            },
            fake_identity as Identity,
            &log,
        );
        (outcome.map(|(o, _)| o), runner, log)
    }

    #[test]
    fn install_puts_steam_exe_next_to_the_launcher() {
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        let launcher = make_echo_launcher();
        write("steam.exe v1", &launcher.parent().unwrap().join("steam.exe"), false);
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        assert_eq!(std::fs::read_to_string(layout.steam_helper()).unwrap(), "steam.exe v1");
    }

    #[test]
    fn tool_files_refresh_replaces_a_changed_launcher() {
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        let launcher = make_echo_launcher();
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        write("#!/bin/sh\necho v2\n", &launcher, true);
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        assert_eq!(std::fs::read_to_string(layout.launcher_binary()).unwrap(), "#!/bin/sh\necho v2\n");
        use std::os::unix::fs::PermissionsExt;
        assert!(std::fs::metadata(layout.launcher_binary()).unwrap().permissions().mode() & 0o111 != 0);
    }

    #[test]
    fn identical_tool_files_are_left_alone() {
        // The app refreshes tool files at every start; a game Steam launches meanwhile must never
        // find bin/macneutron missing or half-written.
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        let launcher = make_echo_launcher();
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        use std::os::unix::fs::MetadataExt;
        let inode = |p: &Path| std::fs::metadata(p).ok().map(|m| m.ino());
        let before = inode(&layout.launcher_binary());
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        assert_eq!(inode(&layout.launcher_binary()), before);
    }

    #[test]
    fn install_finds_steam_exe_in_the_apps_resources() {
        // In MacNeutron.app the launcher is Contents/Helpers/macneutron; steam.exe isn't Mach-O code,
        // so codesign only accepts it in Contents/Resources.
        let contents = make_temp_dir().join("MacNeutron.app/Contents");
        let launcher = contents.join("Helpers/macneutron");
        write("#!/bin/sh\n", &launcher, true);
        write("steam.exe from resources", &contents.join("Resources/steam.exe"), false);
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        assert_eq!(std::fs::read_to_string(layout.steam_helper()).unwrap(), "steam.exe from resources");
    }

    #[test]
    fn equal_identity_skips_the_copy_but_writes_tool_files() {
        let layout = make_installed_layout("A");
        let source = make_fake_wine_app(&make_temp_dir(), "A");
        let (outcome, runner, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Unchanged);
        assert!(runner.calls().is_empty());
        assert!(exists(&layout.launcher_binary()));
        assert!(exists(&layout.root.join("toolmanifest.vdf")));
        assert!(exists(&layout.wineserver())); // the installed copy, untouched
    }

    #[test]
    fn different_identity_replaces_the_runtime() {
        let layout = make_installed_layout("A");
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, runner, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Installed);
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("B"));
        assert_eq!(runner.tools(), vec![String::from("cp"), String::from("codesign")]);
        let new = layout.root.join("wine.app.new").to_string_lossy().into_owned();
        let calls = runner.calls();
        assert_eq!(
            calls[0].arguments,
            vec![
                String::from("-c"),
                String::from("-R"),
                source.to_string_lossy().into_owned(),
                new.clone()
            ]
        );
        assert_eq!(calls[1].arguments, vec![String::from("--verify"), String::from("--strict"), new]);
        assert!(!exists(&layout.root.join("wine.app.new")));
        assert!(!exists(&layout.wineserver())); // the old copy is gone
    }

    #[test]
    fn force_reinstalls_over_an_equal_identity() {
        let layout = make_installed_layout("A");
        let source = make_fake_wine_app(&make_temp_dir(), "A");
        let (outcome, runner, _) = install_fake(&source, &layout, None, None, true, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Installed);
        assert_eq!(runner.tools(), vec![String::from("cp"), String::from("codesign")]);
        assert!(!exists(&layout.wineserver()));
    }

    #[test]
    fn damaged_marker_reinstalls_and_is_cleared() {
        let layout = make_installed_layout("A");
        write("", &layout.runtime_damaged_marker(), false);
        let source = make_fake_wine_app(&make_temp_dir(), "A");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Installed);
        assert!(!exists(&layout.runtime_damaged_marker()));
        assert!(!exists(&layout.wineserver()));
    }

    #[test]
    fn a_running_runtime_process_defers() {
        let layout = make_installed_layout("A");
        let log_dir = make_temp_dir();
        let server = posix::real_path(&layout.wineserver()).unwrap();
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let running = vec!["/usr/bin/true".to_string(), server.clone()];
        let (outcome, runner, log) = install_fake(&source, &layout, None, None, false, 0, 0, &running, None, Some(log_dir));
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(server.clone()));
        assert!(runner.calls().is_empty());
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
        let text = std::fs::read_to_string(log.launcher_log()).unwrap();
        assert!(text.contains(&format!("install deferred: {server} is running")));
    }

    #[test]
    fn deferral_matches_the_kernels_real_path() {
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        make_fake_wine_app(&layout.root, "A");
        let kernel_path = posix::real_path(&layout.wine()).unwrap();
        // $TMPDIR is under /var, a symlink to /private/var, on macOS; the kernel reports the latter.
        assert_ne!(kernel_path, layout.wine().to_string_lossy().into_owned());
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, std::slice::from_ref(&kernel_path), None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(kernel_path.clone()));
        // A sibling whose name only starts like wine.app doesn't count.
        let sibling = format!("{}/wine.app2/Contents/MacOS/wine", posix::real_path(&layout.root).unwrap());
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (other, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[sibling], None, None);
        assert_eq!(other.unwrap(), RuntimeInstallOutcome::Installed);
    }

    #[test]
    fn a_running_rosetta_process_defers_the_install() {
        let layout = make_installed_layout("A");
        let rosetta_wine = layout.root.join("Libraries/Wine/bin/wine");
        write("#!/bin/sh\n", &rosetta_wine, true);
        let running = posix::real_path(&rosetta_wine).unwrap();
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, std::slice::from_ref(&running), None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(running));
        assert!(exists(&rosetta_wine));
    }

    #[test]
    fn a_deferred_install_leaves_the_old_runtime_working() {
        let layout = make_installed_layout("A");
        write("#!/bin/sh\necho old\n", &layout.launcher_binary(), true);
        let running = posix::real_path(&layout.wine()).unwrap();
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, std::slice::from_ref(&running), None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(running));
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
        use std::os::unix::fs::PermissionsExt;
        assert!(std::fs::metadata(layout.wine()).unwrap().permissions().mode() & 0o111 != 0);
        assert!(exists(&layout.wineserver()));
        // The CLI and the runtime change together: the old CLI stays with the old runtime.
        assert_eq!(std::fs::read_to_string(layout.launcher_binary()).unwrap(), "#!/bin/sh\necho old\n");
        assert!(!exists(&layout.root.join("wine.app.new")));
    }

    #[test]
    fn leftovers_from_an_interrupted_install_are_cleaned() {
        // A copy interrupted mid-way left wine.app.new (and an older scheme's wine.app.old); `cp -R` into an existing
        // folder would nest the source inside it.
        let layout = make_installed_layout("A");
        write("half", &layout.root.join("wine.app.new/Contents/MacOS/wine"), false);
        write("old", &layout.root.join("wine.app.old/Contents/MacOS/wine"), false);
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Installed);
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("B"));
        assert!(!exists(&layout.wine_app().join("wine.app")));
        assert!(!exists(&layout.root.join("wine.app.new")));
        assert!(!exists(&layout.root.join("wine.app.old")));
        use std::os::unix::fs::PermissionsExt;
        assert!(std::fs::metadata(layout.wine()).unwrap().permissions().mode() & 0o111 != 0);
    }

    #[test]
    fn a_bad_signature_leaves_the_old_runtime() {
        let layout = make_installed_layout("A");
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 1, 0, &[], None, None);
        assert_eq!(outcome.unwrap_err(), RuntimeInstallError::SignatureInvalid(1));
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
        assert!(exists(&layout.wineserver()));
        assert!(!exists(&layout.root.join("wine.app.new")));
    }

    #[test]
    fn a_game_started_during_the_copy_defers_the_swap() {
        // The copy and its signature check take seconds; a game started meanwhile runs from the old wine.app, which the
        // swap would delete under it. The scan is repeated right before the swap.
        let layout = make_installed_layout("A");
        let log_dir = make_temp_dir();
        let server = posix::real_path(&layout.wineserver()).unwrap();
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let scans = vec![vec![], vec!["/usr/bin/true".to_string(), server.clone()]];
        let (outcome, runner, log) = install_fake(&source, &layout, None, None, false, 0, 0, &[], Some(scans), Some(log_dir));
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(server.clone()));
        assert_eq!(runner.tools(), vec![String::from("cp"), String::from("codesign")]);
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
        assert!(exists(&layout.wineserver()));
        assert!(!exists(&layout.root.join("wine.app.new")));
        let text = std::fs::read_to_string(log.launcher_log()).unwrap();
        assert!(text.contains(&format!("install deferred: {server} is running")));
    }

    #[test]
    fn a_failed_copy_names_its_cause() {
        let layout = make_installed_layout("A");
        let source = make_fake_wine_app(&make_temp_dir(), "B");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 1, &[], None, None);
        assert_eq!(outcome.unwrap_err(), RuntimeInstallError::CopyFailed(1));
        assert_eq!(
            RuntimeInstallError::CopyFailed(1).to_string(),
            "Couldn't copy the runtime into the tool folder (cp exit 1); check free disk space."
        );
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
        assert!(!exists(&layout.root.join("wine.app.new")));
    }

    #[test]
    fn installed_tool_files_are_not_quarantined() {
        // MacNeutron.app downloaded in a zip is quarantined, and so are the CLI and steam.exe inside it; Steam execs the
        // CLI the install writes, which has no stapled ticket of its own.
        let launcher = make_echo_launcher();
        let steam_exe = launcher.parent().unwrap().join("steam.exe");
        write("steam.exe", &steam_exe, false);
        let quarantine = "0083;66f00000;Safari;";
        for file in [&launcher, &steam_exe] {
            assert_eq!(set_xattr(file, "com.apple.quarantine", quarantine.as_bytes()), 0);
        }
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        RuntimeInstaller::write_tool_files(&layout, &launcher, None).unwrap();
        for file in [layout.launcher_binary(), layout.steam_helper()] {
            assert!(!has_xattr(&file, "com.apple.quarantine"), "{}", file.display());
        }
        assert!(has_xattr(&launcher, "com.apple.quarantine")); // the source keeps it
    }

    #[test]
    fn a_source_without_the_loader_is_not_a_wine_app() {
        let layout = make_installed_layout("A");
        let source = make_temp_dir().join("wine.app");
        write("B", &source.join("Contents/id"), false);
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(
            outcome.unwrap_err(),
            RuntimeInstallError::NotAWineApp(source.to_string_lossy().into_owned())
        );
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("A"));
    }

    #[test]
    fn rosetta_era_entries_are_removed() {
        let layout = make_installed_layout("A");
        let mut stale: Vec<&str> = ToolLayout::ROSETTA_ERA_ENTRIES.to_vec();
        stale.push("proton");
        for name in ToolLayout::ROSETTA_ERA_ENTRIES {
            write("old", &layout.root.join(name).join("file"), false);
        }
        write("#!/bin/sh\n", &layout.root.join("proton"), true); // the stub is a file
        let source = make_fake_wine_app(&make_temp_dir(), "A");
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Unchanged);
        for name in stale {
            assert!(!exists(&layout.root.join(name)), "{name}");
        }
        assert!(exists(&layout.wine()));
    }

    #[test]
    fn install_works_under_a_spaced_non_ascii_path() {
        let layout = ToolLayout::new(make_temp_dir().join("Application Support/é/macneutron"));
        make_fake_wine_app(&layout.root, "A");
        let source = make_fake_wine_app(&make_temp_dir().join("Gamé Folder"), "B");
        let running = posix::real_path(&layout.wine()).unwrap();
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, std::slice::from_ref(&running), None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Deferred(running));
        let (outcome, _, _) = install_fake(&source, &layout, None, None, false, 0, 0, &[], None, None);
        assert_eq!(outcome.unwrap(), RuntimeInstallOutcome::Installed);
        assert_eq!(fake_identity(&layout.wine_app()).as_deref(), Some("B"));
        assert!(exists(&layout.launcher_binary()));
    }

    #[test]
    fn steam_exe_option_is_used_when_given() {
        // A dev build's .build/release/ has no steam.exe beside the CLI.
        let layout = make_installed_layout("A");
        let steam_exe = make_temp_dir().join("arm64 bridge/steam.exe");
        write("steam.exe arm64", &steam_exe, false);
        let source = make_fake_wine_app(&make_temp_dir(), "A");
        let _ = install_fake(&source, &layout, None, Some(&steam_exe), false, 0, 0, &[], None, None);
        assert_eq!(std::fs::read_to_string(layout.steam_helper()).unwrap(), "steam.exe arm64");
    }

    #[test]
    fn manifest_points_at_the_cli() {
        // R0b: Steam launches a thin arm64 tool binary, so the manifest runs the CLI itself; no /bin/sh stub.
        let layout = ToolLayout::new(make_temp_dir().join("macneutron"));
        RuntimeInstaller::write_tool_files(&layout, &make_echo_launcher(), None).unwrap();
        let manifest = std::fs::read_to_string(layout.root.join("toolmanifest.vdf")).unwrap();
        assert!(manifest.contains("\"commandline\" \"/bin/macneutron launch %verb%\""));
        assert!(!exists(&layout.root.join("proton")));
    }

    #[test]
    fn first_install_clones_verifies_and_swaps_in() {
        let source = make_signed_wine_app(&make_temp_dir().join("Helpers dir"), Path::new("/usr/bin/true"), "test", "1");
        let layout = ToolLayout::new(make_temp_dir().join("Application Support/macneutron"));
        let log = LauncherLog::new(make_temp_dir());
        let launcher = make_echo_launcher();
        let install = || {
            RuntimeInstaller::install(
                &source,
                &layout,
                &launcher,
                None,
                false,
                &crate::runtime_installer::standard_runner(),
                &|| Vec::new(),
                default_identity,
                &log,
            )
        };
        assert_eq!(install().unwrap().0, RuntimeInstallOutcome::Installed);
        let identity = CodeIdentity::of(&source).unwrap();
        assert_eq!(CodeIdentity::of(&layout.wine_app()), Some(identity));
        let status = std::process::Command::new("/usr/bin/codesign")
            .args(["--verify", "--strict"])
            .arg(layout.wine_app())
            .status()
            .unwrap();
        assert_eq!(status.code(), Some(0));
        assert!(!exists(&layout.root.join("wine.app.new")));
        assert_eq!(install().unwrap().0, RuntimeInstallOutcome::Unchanged);
    }

    #[test]
    fn tool_files_keep_the_legacy_name_only_while_the_vdf_has_it() {
        // Steam maps cloud roots only for tool names containing "proton"; the old name stays until its mappings move.
        let fresh = ToolLayout::new(make_temp_dir().join("macneutron"));
        RuntimeInstaller::write_tool_files(&fresh, &make_echo_launcher(), None).unwrap();
        let fresh_text = std::fs::read_to_string(fresh.root.join("compatibilitytool.vdf")).unwrap();
        let fresh_vdf = KeyValues::parse(&fresh_text).unwrap();
        let tools = node_at(&fresh_vdf, &["compatibilitytools", "compat_tools"]).unwrap();
        assert_eq!(
            tools.children().iter().map(|n| n.key.clone()).collect::<Vec<_>>(),
            vec!["macneutron-proton"]
        );
        let display = node_at(&fresh_vdf, &["compatibilitytools", "compat_tools", "macneutron-proton", "display_name"]).unwrap();
        assert_eq!(display.string_value().as_deref(), Some("MacNeutron"));
        assert!(!RuntimeInstaller::registers_legacy_name(&fresh.root));

        let updated = ToolLayout::new(make_temp_dir().join("macneutron"));
        write(
            "\"compatibilitytools\"\n{\n  \"compat_tools\"\n  {\n    \"macneutron\"\n    {\n      \"install_path\" \".\"\n      \"display_name\" \"MacNeutron\"\n      \"from_oslist\"  \"windows\"\n      \"to_oslist\"    \"linux\"\n    }\n  }\n}\n",
            &updated.root.join("compatibilitytool.vdf"),
            false,
        );
        RuntimeInstaller::write_tool_files(&updated, &make_echo_launcher(), None).unwrap();
        let both_text = std::fs::read_to_string(updated.root.join("compatibilitytool.vdf")).unwrap();
        let both = KeyValues::parse(&both_text).unwrap();
        let tools = node_at(&both, &["compatibilitytools", "compat_tools"]).unwrap();
        assert_eq!(
            tools.children().iter().map(|n| n.key.clone()).collect::<Vec<_>>(),
            vec!["macneutron-proton", "macneutron"]
        );
        let display = node_at(&both, &["compatibilitytools", "compat_tools", "macneutron", "display_name"]).unwrap();
        assert_eq!(display.string_value().as_deref(), Some("MacNeutron (old)"));

        RuntimeInstaller::write_compatibility_tool(&updated.root, false).unwrap();
        RuntimeInstaller::write_tool_files(&updated, &make_echo_launcher(), None).unwrap();
        assert!(!RuntimeInstaller::registers_legacy_name(&updated.root));
    }

    fn set_xattr(path: &Path, name: &str, value: &[u8]) -> i32 {
        use std::ffi::CString;
        use std::os::unix::ffi::OsStrExt;
        let cpath = CString::new(path.as_os_str().as_bytes()).unwrap();
        let cname = CString::new(name).unwrap();
        unsafe {
            libc::setxattr(
                cpath.as_ptr(),
                cname.as_ptr(),
                value.as_ptr() as *const libc::c_void,
                value.len(),
                0,
                0,
            )
        }
    }

    fn has_xattr(path: &Path, name: &str) -> bool {
        use std::ffi::CString;
        use std::os::unix::ffi::OsStrExt;
        let cpath = CString::new(path.as_os_str().as_bytes()).unwrap();
        let cname = CString::new(name).unwrap();
        unsafe { libc::getxattr(cpath.as_ptr(), cname.as_ptr(), std::ptr::null_mut(), 0, 0, 0) > 0 }
    }

    /// `FileManager.copyItem`: copies a file or a whole tree, like the `cp -c -R` the fake stands in for (std's copy is file-only, so the swap would find no source directory to rename into place).
    fn copy_recursive(source: &Path, destination: &Path) -> std::io::Result<()> {
        if source.is_dir() {
            std::fs::create_dir_all(destination)?;
            for entry in std::fs::read_dir(source)? {
                let entry = entry?;
                copy_recursive(&entry.path(), &destination.join(entry.file_name()))?;
            }
            Ok(())
        } else {
            std::fs::copy(source, destination).map(|_| ())
        }
    }
}
