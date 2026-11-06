//! The microVM's host tools (MicroVM), from MacNeutron.app's Contents/Helpers/vm into the VM folder, the way
//! `RuntimeInstaller` installs wine.app: each item copied beside itself, then swapped in, never while a VM runs from the
//! folder. The folder's games' files (state/, runs/) and its images/ aren't the tools' and are never touched.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::posix;
use crate::process_runner::ProcessRunner;
use crate::runtime_installer::{RuntimeInstallError, RuntimeInstaller};
use crate::valve_client::ValveInstall;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum VMToolsInstallOutcome {
    Installed,
    Unchanged,
    /// A VM runs from the folder (its executable's path): the old tools stay until it ends.
    Deferred(String),
    /// The folder is a developer's (`is_developer_folder`): never written.
    DeveloperFolder,
}

pub struct VMToolsInstaller;

impl VMToolsInstaller {
    /// dxmt: native DXMT and its replayer, which macneutron-vm loads for Direct3D forwarding (vm/d3d).
    /// dxmt-translator: native DXMT's translator key (MicroVM's `dxmt_translator`).
    pub const ITEMS: [&'static str; 9] = [
        "macneutron-vm",
        "libkrun",
        "steam-forward",
        "gvproxy",
        "metal-host",
        "dxmt",
        "run-guest.sh",
        "run-game.sh",
        "dxmt-translator",
    ];

    /// The items that aren't code: MacNeutron.app keeps them in Contents/Resources/vm (codesign takes a script or a
    /// data file in Contents/Helpers for unsigned code, and a script's own signature doesn't survive the zip).
    pub const SCRIPTS: [&'static str; 3] = ["run-guest.sh", "run-game.sh", "dxmt-translator"];

    /// `scripts`: where `SCRIPTS` come from, else `tools`.
    pub fn install(
        tools: &Path,
        scripts: Option<&Path>,
        vm: &Path,
        runner: &dyn ProcessRunner,
        running_executables: &dyn Fn() -> Vec<String>,
    ) -> Result<VMToolsInstallOutcome, RuntimeInstallError> {
        if Self::is_developer_folder(vm) {
            return Ok(VMToolsInstallOutcome::DeveloperFolder);
        }
        let source = |item: &str| -> PathBuf {
            if Self::SCRIPTS.contains(&item) {
                scripts.unwrap_or(tools).join(item)
            } else {
                tools.join(item)
            }
        };
        let stamp = vm.join("tools.identity");
        let items: Vec<(String, PathBuf)> = Self::ITEMS.iter().map(|i| (i.to_string(), source(i))).collect();
        let wanted = format!("{}\n", Self::digest(&items)?);
        let installing = vm.join(".installing");
        let stamp_matches = std::fs::read_to_string(&stamp).map(|s| s == wanted).unwrap_or(false);
        if stamp_matches && Self::ITEMS.iter().all(|item| vm.join(item).exists()) {
            let _ = std::fs::remove_file(&installing); // a crash between the stamp and the marker's removal
            return Ok(VMToolsInstallOutcome::Unchanged);
        }
        std::fs::create_dir_all(vm).map_err(|e| RuntimeInstallError::Io(e.to_string()))?;
        if let Some(running) = running_under(vm, running_executables) {
            return Ok(VMToolsInstallOutcome::Deferred(running));
        }
        for item in Self::ITEMS.iter() {
            let _ = remove_item(&vm.join(format!("{item}.new")));
            let _ = remove_item(&vm.join(format!("{item}.old")));
        }
        for item in Self::ITEMS.iter() {
            let source_path = source(item).to_string_lossy().into_owned();
            let new_path = vm.join(format!("{item}.new")).to_string_lossy().into_owned();
            let status = runner
                .run(
                    Path::new("/bin/cp"),
                    &["-c".into(), "-R".into(), source_path, new_path],
                    &BTreeMap::new(),
                    None,
                )
                .unwrap_or(-1);
            if status != 0 {
                for item in Self::ITEMS.iter() {
                    let _ = remove_item(&vm.join(format!("{item}.new")));
                }
                return Err(RuntimeInstallError::CopyFailed(status));
            }
        }
        // As wine.app's install checks its copy: every Mach-O in the copies verifies, or nothing is swapped.
        for item in Self::ITEMS.iter() {
            let status = Self::verify(&vm.join(format!("{item}.new")), runner);
            if status != 0 {
                for item in Self::ITEMS.iter() {
                    let _ = remove_item(&vm.join(format!("{item}.new")));
                }
                return Err(RuntimeInstallError::SignatureInvalid(status));
            }
        }
        // Again: the copy takes a moment, and a VM started meanwhile runs from the old tools.
        if let Some(running) = running_under(vm, running_executables) {
            for item in Self::ITEMS.iter() {
                let _ = remove_item(&vm.join(format!("{item}.new")));
            }
            return Ok(VMToolsInstallOutcome::Deferred(running));
        }
        // Until the last rename a launch runs on macOS (MicroVM::find), not on a mix of old and new tools.
        let _ = std::fs::write(&installing, b"");
        for item in Self::ITEMS.iter() {
            let new = vm.join(format!("{item}.new"));
            let old = vm.join(item);
            let swapped = if old.exists() { posix::rename_swap(&new, &old) } else { posix::rename(&new, &old) };
            if !swapped {
                return Err(RuntimeInstallError::SwapFailed(std::io::Error::last_os_error().raw_os_error().unwrap_or(0)));
            }
            let _ = remove_item(&vm.join(format!("{item}.new"))); // the old item, after a swap
        }
        std::fs::write(&stamp, wanted.as_bytes()).map_err(|e| RuntimeInstallError::Io(e.to_string()))?;
        let _ = std::fs::remove_file(&installing);
        Ok(VMToolsInstallOutcome::Installed)
    }

    /// Valve's steamclient64.dll (`ValveClient`) as the guest's share/valve/steamclient64.dll (run-game.sh copies it into
    /// the game's prefix); a None install removes it.
    pub fn copy_valve_client(install: Option<&ValveInstall>, vm: &Path) -> std::io::Result<()> {
        if Self::is_developer_folder(vm) {
            return Ok(());
        }
        let destination = vm.join("share/valve/steamclient64.dll");
        let Some(install) = install else {
            let _ = std::fs::remove_file(&destination);
            return Ok(());
        };
        RuntimeInstaller::install_file(&install.steam_client64(), &destination)
    }

    /// codesign --verify --strict on every Mach-O under `url` (an item's copy): 0, or the first failure's status.
    pub fn verify(url: &Path, runner: &dyn ProcessRunner) -> i32 {
        let root = url.to_string_lossy().into_owned();
        let mut files = vec![url.to_path_buf()];
        files.extend(walk(url).into_iter().map(|relative| url.join(relative)));
        for file in files {
            if !Self::is_macho(&file) {
                continue;
            }
            let status = runner
                .run(
                    Path::new("/usr/bin/codesign"),
                    &["--verify".into(), "--strict".into(), file.to_string_lossy().into_owned()],
                    &BTreeMap::new(),
                    None,
                )
                .unwrap_or(-1);
            if status != 0 {
                return status;
            }
        }
        let _ = root;
        0
    }

    fn is_macho(path: &Path) -> bool {
        use std::io::Read;
        let Ok(mut file) = std::fs::File::open(path) else { return false };
        let mut magic = [0u8; 4];
        if file.read_exact(&mut magic).is_err() {
            return false;
        }
        matches!(
            magic,
            [0xcf, 0xfa, 0xed, 0xfe] | [0xca, 0xfe, 0xba, 0xbe] | [0xce, 0xfa, 0xed, 0xfe]
        )
    }

    /// SHA-256 over every item's files: their paths, kinds and bytes (a symlink's target), so any change to any item is
    /// a new set of tools (one binary's code identity misses a changed script or library).
    pub fn digest(items: &[(String, PathBuf)]) -> Result<String, RuntimeInstallError> {
        let mut hash = posix::Sha256::new();
        for (name, url) in items {
            let mut relative_paths: Vec<String> = vec![String::new()];
            if url.exists() {
                relative_paths.extend(walk(url));
            }
            relative_paths.sort();
            for relative in relative_paths {
                let file = if relative.is_empty() { url.clone() } else { url.join(&relative) };
                match posix::kind(&file) {
                    posix::Kind::Link => {
                        let target = std::fs::read_link(&file)
                            .map(|t| t.to_string_lossy().into_owned())
                            .unwrap_or_default();
                        hash.update(format!("l {name}/{relative} {target}\0").as_bytes());
                    }
                    posix::Kind::Directory => {
                        hash.update(format!("d {name}/{relative}\0").as_bytes());
                    }
                    _ => {
                        hash.update(format!("f {name}/{relative}\0").as_bytes());
                        let bytes = std::fs::read(&file).map_err(|e| RuntimeInstallError::Io(e.to_string()))?;
                        hash.update(&bytes);
                    }
                }
            }
        }
        Ok(hash.finalize())
    }

    /// A developer's folder (build/vm-work, usually linked here): its images are vm's own build, whose
    /// manifest.sha256 says `dev` (vm/image/build.sh). Any other folder, linked or not (a player's VM moved to another
    /// drive), is the app's to update.
    pub fn is_developer_folder(vm: &Path) -> bool {
        std::fs::read_to_string(vm.join("images/manifest.sha256"))
            .map(|text| text.trim() == "dev")
            .unwrap_or(false)
    }

    /// The folder is a link to a folder that isn't there (its drive disconnected, or a developer's build/ deleted): the
    /// link's target. None for any other folder.
    pub fn missing_target(vm: &Path) -> Option<String> {
        let target = std::fs::read_link(Self::link_path(vm)).ok()?;
        if vm.exists() {
            return None;
        }
        Some(target.to_string_lossy().into_owned())
    }

    /// Rebuild's way past a missing folder: the link goes, so a setup makes a real folder there. True if it removed one.
    pub fn remove_dead_link(vm: &Path) -> bool {
        if Self::missing_target(vm).is_none() {
            return false;
        }
        std::fs::remove_file(Self::link_path(vm)).is_ok()
    }

    /// The folder is a link (to anywhere, there or not).
    pub fn is_link(vm: &Path) -> bool {
        std::fs::read_link(Self::link_path(vm)).is_ok()
    }

    /// Rebuild's way past a developer's folder linked here: only the link goes (the developer's folder is never touched),
    /// so a setup makes a real folder there. True if it removed one; a developer's own folder (no link) stays.
    pub fn remove_developer_link(vm: &Path) -> bool {
        if !Self::is_link(vm) || !Self::is_developer_folder(vm) {
            return false;
        }
        std::fs::remove_file(Self::link_path(vm)).is_ok()
    }

    /// The folder's path without its trailing "/", which would follow a link.
    fn link_path(vm: &Path) -> PathBuf {
        let text = vm.to_string_lossy().into_owned();
        PathBuf::from(text.strip_suffix('/').unwrap_or(&text))
    }
}

/// `FileManager.removeItem`: removes a file, a symlink or a whole tree, like an item's copy (`libkrun.new` is a folder).
fn remove_item(path: &Path) -> std::io::Result<()> {
    if posix::kind(path) == posix::Kind::Directory {
        std::fs::remove_dir_all(path)
    } else {
        std::fs::remove_file(path)
    }
}

/// Every path under `root`, relative to it, sorted; empty when `root` isn't readable.
fn walk(root: &Path) -> Vec<String> {
    let mut found = Vec::new();
    walk_into(root, root, &mut found);
    found.sort();
    found
}
fn walk_into(root: &Path, current: &Path, found: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(current) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        if let Ok(relative) = path.strip_prefix(root) {
            found.push(relative.to_string_lossy().into_owned());
        }
        if posix::kind(&path) == posix::Kind::Directory {
            walk_into(root, &path, found);
        }
    }
}

/// The first running executable under the folder, by its real path (the kernel reports /private/var/…) or as given.
fn running_under(vm: &Path, running_executables: &dyn Fn() -> Vec<String>) -> Option<String> {
    let given = vm.to_string_lossy().into_owned();
    let roots: Vec<String> = [given.clone(), posix::real_path(Path::new(&given)).unwrap_or(given)]
        .into_iter()
        .map(|root| if root.ends_with('/') { root } else { format!("{root}/") })
        .collect();
    running_executables().into_iter().find(|path| {
        let real = posix::real_path(Path::new(path)).unwrap_or_else(|| path.clone());
        roots.iter().any(|root| real.starts_with(root.as_str()) || path.starts_with(root.as_str()))
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::micro_vm::MicroVM;
    use crate::test_support::{make_temp_dir, write, FakeRunner};
    use crate::verb::Verb;
    use std::collections::BTreeMap;

    /// A stand-in for MacNeutron.app's Contents/Helpers/vm: each item, its text naming the tools' version.
    fn make_tools(version: &str) -> PathBuf {
        let tools = make_temp_dir().join("vm-tools");
        for file in [
            "macneutron-vm",
            "libkrun/libkrun.1.dylib",
            "steam-forward/host/mn-steam-host.app/Contents/MacOS/mn-steam-host",
            "gvproxy",
            "metal-host/winemetal.so",
            "dxmt/d3d12.dylib",
            "dxmt/dxmt-replay",
            "run-guest.sh",
            "run-game.sh",
            "dxmt-translator",
        ] {
            write(&format!("{version} {file}\n"), &tools.join(file), true);
        }
        tools
    }

    fn text(url: &Path) -> Option<String> {
        std::fs::read_to_string(url).ok()
    }

    fn empty_folder() -> PathBuf {
        make_temp_dir().join("vm")
    }

    /// `cp -c -R` as a FakeRunner's answer: copies the tree (std's copy is file-only, so the swap would find no source
    /// directory to rename into place) and every Mach-O verifies.
    fn copying_runner() -> FakeRunner {
        FakeRunner::new(|call| {
            if call.tool == "cp" {
                let _ = copy_recursive(Path::new(&call.arguments[2]), Path::new(&call.arguments[3]));
            }
            0
        })
    }

    /// `FileManager.copyItem`: copies a file or a whole tree, like the `cp -c -R` the fake stands in for.
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

    fn install(tools: &Path, scripts: Option<&Path>, vm: &Path, running: &[String]) -> Result<VMToolsInstallOutcome, RuntimeInstallError> {
        let runner = copying_runner();
        let running = running.to_vec();
        VMToolsInstaller::install(tools, scripts, vm, &runner, &|| running.clone())
    }

    #[test]
    fn installs_every_item_and_records_the_identity() {
        let tools = make_tools("a");
        let vm = empty_folder();
        assert_eq!(install(&tools, None, &vm, &[]).unwrap(), VMToolsInstallOutcome::Installed);
        for item in VMToolsInstaller::ITEMS {
            assert!(vm.join(item).exists(), "{item}");
        }
        assert_eq!(text(&vm.join("libkrun/libkrun.1.dylib")).unwrap(), "a libkrun/libkrun.1.dylib\n");
        assert_eq!(text(&vm.join("tools.identity")).unwrap().chars().count(), 65);
    }

    #[test]
    fn native_dxmt_reaches_the_vm() {
        // Direct3D forwarding's native DXMT and its replayer (macneutron-vm loads them from the folder's dxmt/: M3 Task 9).
        let tools = make_tools("a");
        let vm = empty_folder();
        let _ = install(&tools, None, &vm, &[]).unwrap();
        assert_eq!(text(&vm.join("dxmt/d3d12.dylib")).unwrap(), "a dxmt/d3d12.dylib\n");
        assert_eq!(text(&vm.join("dxmt/dxmt-replay")).unwrap(), "a dxmt/dxmt-replay\n");
    }

    #[test]
    fn a_second_install_of_the_same_tools_is_unchanged() {
        let tools = make_tools("a");
        let vm = empty_folder();
        install(&tools, None, &vm, &[]).unwrap();
        let file = vm.join("macneutron-vm");
        let before = std::fs::metadata(&file).unwrap().modified().unwrap();
        assert_eq!(install(&tools, None, &vm, &[]).unwrap(), VMToolsInstallOutcome::Unchanged);
        assert_eq!(std::fs::metadata(&file).unwrap().modified().unwrap(), before);
    }

    #[test]
    fn new_tools_replace_old_ones_but_not_the_games_files() {
        let vm = empty_folder();
        install(&make_tools("a"), None, &vm, &[]).unwrap();
        let kept = ["state/1.img", "state/1.tpm/id", "runs/1/run.log", "images/manifest.sha256"];
        for path in kept {
            write(&format!("game {path}\n"), &vm.join(path), false);
        }
        assert_eq!(install(&make_tools("b"), None, &vm, &[]).unwrap(), VMToolsInstallOutcome::Installed);
        for path in kept {
            assert_eq!(text(&vm.join(path)).unwrap(), format!("game {path}\n"));
        }
        assert_eq!(text(&vm.join("metal-host/winemetal.so")).unwrap(), "b metal-host/winemetal.so\n");
        assert_eq!(text(&vm.join("tools.identity")).unwrap().chars().count(), 65);
    }

    #[test]
    fn a_running_vm_defers_the_install() {
        let vm = empty_folder();
        std::fs::create_dir_all(&vm).unwrap();
        let running = vm.join("macneutron-vm").to_string_lossy().into_owned();
        assert_eq!(
            install(&make_tools("a"), None, &vm, std::slice::from_ref(&running)).unwrap(),
            VMToolsInstallOutcome::Deferred(running)
        );
        assert!(!vm.join("run-guest.sh").exists());
    }

    #[test]
    fn a_vm_started_during_the_copy_defers_the_swap() {
        let vm = empty_folder();
        install(&make_tools("a"), None, &vm, &[]).unwrap();
        let running = vm.join("macneutron-vm").to_string_lossy().into_owned();
        let count_new = |vm: &Path| std::fs::read_dir(vm).unwrap().flatten().filter(|e| e.file_name().to_string_lossy().ends_with(".new")).count();
        let before = count_new(&vm);
        let calls = std::cell::Cell::new(0);
        let runner = copying_runner();
        let outcome = VMToolsInstaller::install(&make_tools("b"), None, &vm, &runner, &|| {
            calls.set(calls.get() + 1);
            if calls.get() == 1 {
                Vec::new()
            } else {
                vec![running.clone()]
            }
        })
        .unwrap();
        assert_eq!(outcome, VMToolsInstallOutcome::Deferred(running));
        assert_eq!(text(&vm.join("macneutron-vm")).unwrap(), "a macneutron-vm\n");
        assert_eq!(count_new(&vm), before);
    }

    #[test]
    fn leftovers_of_an_interrupted_install_are_cleared() {
        let vm = empty_folder();
        write("junk\n", &vm.join("libkrun.new/junk"), false);
        install(&make_tools("a"), None, &vm, &[]).unwrap();
        assert!(!vm.join("libkrun.new").exists());
        assert_eq!(text(&vm.join("libkrun/libkrun.1.dylib")).unwrap(), "a libkrun/libkrun.1.dylib\n");
    }

    #[test]
    fn a_developers_folder_is_left_alone() {
        // build/vm-work linked in: vm/image/build.sh's images say `dev`.
        let target = make_temp_dir().join("vm-work");
        write("dev\n", &target.join("images/manifest.sha256"), false);
        let vm = make_temp_dir().join("vm");
        std::os::unix::fs::symlink(&target, &vm).unwrap();
        assert_eq!(install(&make_tools("a"), None, &vm, &[]).unwrap(), VMToolsInstallOutcome::DeveloperFolder);
        let valve = make_temp_dir();
        write("valve\n", &valve.join("steamclient64.dll"), false);
        let valve = ValveInstall { version: "1".into(), folder: valve };
        VMToolsInstaller::copy_valve_client(Some(&valve), &vm).unwrap();
        let contents: Vec<String> = std::fs::read_dir(&target)
            .unwrap()
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        assert_eq!(contents, vec![String::from("images")]);
        assert!(!VMToolsInstaller::remove_dead_link(&vm));
        assert_eq!(VMToolsInstaller::missing_target(&vm), None);
    }

    #[test]
    fn a_folder_moved_and_linked_back_is_updated() {
        // A player's VM moved to another drive with a link left behind: still the app's to update.
        let target = make_temp_dir().join("vm");
        write("1234\n", &target.join("images/manifest.sha256"), false);
        let vm = make_temp_dir().join("vm");
        std::os::unix::fs::symlink(&target, &vm).unwrap();
        assert!(!VMToolsInstaller::is_developer_folder(&vm));
        assert_eq!(install(&make_tools("a"), None, &vm, &[]).unwrap(), VMToolsInstallOutcome::Installed);
        assert_eq!(text(&target.join("libkrun/libkrun.1.dylib")).unwrap(), "a libkrun/libkrun.1.dylib\n");
    }

    #[test]
    fn a_link_to_a_missing_folder_is_reported_then_replaced_on_request() {
        // Its drive disconnected, or a developer's build/ deleted: nothing removes the link but `removeDeadLink`.
        let vm = make_temp_dir().join("vm");
        let link = vm.to_string_lossy().into_owned(); // no trailing "/": the link itself
        std::os::unix::fs::symlink("/Volumes/Gone/vm", &link).unwrap();
        assert!(!VMToolsInstaller::is_developer_folder(&vm));
        assert_eq!(VMToolsInstaller::missing_target(&vm).as_deref(), Some("/Volumes/Gone/vm"));
        assert!(VMToolsInstaller::remove_dead_link(&vm));
        assert_eq!(std::fs::read_link(&link).ok(), None);
        assert_eq!(VMToolsInstaller::missing_target(&vm), None);
        let _ = install(&make_tools("a"), None, &vm, &[]).unwrap();
        assert_eq!(text(&vm.join("libkrun/libkrun.1.dylib")).unwrap(), "a libkrun/libkrun.1.dylib\n");
    }

    #[test]
    fn valve_client_reaches_the_vm() {
        let vm = empty_folder();
        let valve = make_temp_dir();
        write("valve 1\n", &valve.join("steamclient64.dll"), false);
        let valve = ValveInstall { version: "1".into(), folder: valve };
        VMToolsInstaller::copy_valve_client(Some(&valve), &vm).unwrap();
        let copied = vm.join("share/valve/steamclient64.dll");
        assert_eq!(text(&copied).unwrap(), "valve 1\n");
        VMToolsInstaller::copy_valve_client(None, &vm).unwrap();
        assert!(!copied.exists());
    }

    #[test]
    fn the_scripts_come_from_their_own_folder() {
        // MacNeutron.app keeps them in Contents/Resources/vm: codesign takes a script or a data file (the translator key)
        // in Contents/Helpers for unsigned code.
        let tools = make_tools("a");
        let scripts = make_temp_dir().join("vm-scripts");
        for name in VMToolsInstaller::SCRIPTS {
            std::fs::remove_file(tools.join(name)).unwrap();
            write(&format!("script {name}\n"), &scripts.join(name), true);
        }
        let vm = empty_folder();
        assert_eq!(
            install(&tools, Some(&scripts), &vm, &[]).unwrap(),
            VMToolsInstallOutcome::Installed
        );
        assert_eq!(text(&vm.join("run-guest.sh")).unwrap(), "script run-guest.sh\n");
        assert_eq!(text(&vm.join("macneutron-vm")).unwrap(), "a macneutron-vm\n");
    }

    #[test]
    fn tools_change_when_any_item_does() {
        // A release that changes only a script, libkrun or gvproxy still reaches the folder.
        let tools = make_tools("a");
        let vm = empty_folder();
        install(&tools, None, &vm, &[]).unwrap();
        write("b run-game.sh\n", &tools.join("run-game.sh"), true);
        assert_eq!(install(&tools, None, &vm, &[]).unwrap(), VMToolsInstallOutcome::Installed);
        assert_eq!(text(&vm.join("run-game.sh")).unwrap(), "b run-game.sh\n");
    }

    #[test]
    fn a_tool_with_a_broken_signature_is_refused() {
        // As wine.app's install checks its copy: a Mach-O whose signature doesn't verify never replaces the old tools.
        let tools = make_tools("a");
        let vm = empty_folder();
        install(&tools, None, &vm, &[]).unwrap();
        let vm_tool = tools.join("macneutron-vm");
        std::fs::remove_file(&vm_tool).unwrap();
        std::fs::copy("/usr/bin/true", &vm_tool).unwrap();
        // Changes its bytes after the signature: no longer verifies.
        {
            use std::io::Write;
            let mut file = std::fs::OpenOptions::new().append(true).open(&vm_tool).unwrap();
            file.write_all(b"x").unwrap();
        }
        let runner = crate::runtime_installer::standard_runner();
        let outcome = VMToolsInstaller::install(&tools, None, &vm, &runner, &|| Vec::new());
        assert!(matches!(outcome, Err(RuntimeInstallError::SignatureInvalid(_))), "{outcome:?}");
        assert_eq!(text(&vm.join("macneutron-vm")).unwrap(), "a macneutron-vm\n");
        assert!(!vm.join("macneutron-vm.new").exists());
    }

    #[test]
    fn games_wait_while_the_tools_swap() {
        // The swap goes item by item: until its last rename, a launch runs on macOS instead of mixed tools.
        let tools = make_tools("a");
        let vm = empty_folder();
        install(&tools, None, &vm, &[]).unwrap();
        assert!(!vm.join(".installing").exists());
        write("dev\n", &vm.join("images/manifest.sha256"), false);
        assert!(MicroVM::find(&vm, &BTreeMap::new(), Verb::WaitForExitAndRun).is_some());
        write("", &vm.join(".installing"), false);
        assert!(MicroVM::find(&vm, &BTreeMap::new(), Verb::WaitForExitAndRun).is_none());
    }
}
