//! `macneutron vm-setup` (spec §4.5): the VM folder's images from the app's manifest. Downloads and the image set being
//! built live in <vm>/setup; the set swaps in whole, its manifest.sha256 already written, and never while a VM runs
//! from the folder (exit 75; the next run swaps it without rebuilding). The games' own files (state/, runs/) and a
//! developer's folder (`VMToolsInstaller.isDeveloperFolder`) are never touched.

use std::path::Path;
use std::process::{Child, Command};
use std::sync::Mutex;

use crate::image_assembler::ImageAssembler;
use crate::posix;
use crate::recipe::{Kind, Recipe};
use crate::runtime_installer::RuntimeInstaller;
use crate::valve_client::Fetch;
use crate::vm_downloads::{DownloadItem, Pause, Progress, VMDownloads};
use crate::vm_setup_error::VMSetupError;
use crate::vm_setup_manifest::VMSetupManifest;
use crate::vm_tools_installer::VMToolsInstaller;

pub struct VMSetup;

/// The setup's own children (tar, mkfs.erofs), ended with it: the app quitting ends vm-setup with SIGTERM, and
/// vm-setup ends them before it exits. A group keeps a test's children apart from the ones tests beside it run.
pub struct Children;

impl Children {
    fn running() -> &'static Mutex<Vec<(Child, i32)>> {
        static RUNNING: Mutex<Vec<(Child, i32)>> = Mutex::new(Vec::new());
        &RUNNING
    }

    /// Spawns `command`, records the child, and hands back its pid so the caller can wait on it.
    pub fn spawn(command: &mut Command) -> Result<u32, VMSetupError> {
        let child = command.spawn().map_err(|_| VMSetupError::Tool("spawn".into(), -1))?;
        let pid = child.id();
        Self::running().lock().unwrap().push((child, 0));
        Ok(pid)
    }

    /// Records an already-spawned child (the pipe cases take its stdin first).
    pub fn run(child: Child, group: i32) -> u32 {
        let pid = child.id();
        Self::running().lock().unwrap().push((child, group));
        pid
    }

    /// Waits on the child with `pid` and returns its exit code (nil when it ended by signal).
    pub fn wait(pid: u32) -> Option<i32> {
        let (mut child, _) = Self::take(pid)?;
        match child.wait() {
            Ok(status) => status.code(),
            Err(_) => None,
        }
    }

    /// Removes the child with `pid` from the list and returns it.
    pub fn take(pid: u32) -> Option<(Child, i32)> {
        let mut guard = Self::running().lock().unwrap();
        let index = guard.iter().position(|(child, _)| child.id() == pid)?;
        Some(guard.remove(index))
    }

    pub fn is_running(pid: u32) -> bool {
        let guard = Self::running().lock().unwrap();
        guard.iter().any(|(child, _)| child.id() == pid)
    }

    /// Stops the child with `pid` (SIGTERM).
    pub fn terminate(pid: u32) {
        let mut guard = Self::running().lock().unwrap();
        if let Some((child, _)) = guard.iter_mut().find(|(child, _)| child.id() == pid) {
            let _ = child.kill();
        }
    }

    /// Every child, or one group's.
    pub fn terminate_all(group: Option<i32>) {
        for (child, g) in Self::running().lock().unwrap().iter_mut() {
            if group.map(|group| group == *g).unwrap_or(true) {
                let _ = child.kill();
            }
        }
    }
}

impl VMSetup {
    /// Which image set a manifest builds (images/manifest.sha256): the SHA-256 of its JSON with sorted keys and without
    /// the parts' URL, which names the release it's on. A release whose VM didn't change moves only that URL, and no
    /// Mac downloads its VM again.
    pub fn manifest_hash(manifest: &Path) -> Option<String> {
        let data = std::fs::read_to_string(manifest).ok()?;
        let mut value: serde_json::Value = serde_json::from_str(&data).ok()?;
        if let Some(parts) = value.get_mut("parts").and_then(|p| p.as_object_mut()) {
            parts.remove("url");
        }
        // serde_json's map is a BTreeMap, so keys serialize sorted, matching JSONSerialization's .sortedKeys.
        let canonical = serde_json::to_string(&value).ok()?;
        Some(posix::sha256(canonical.as_bytes()))
    }

    /// The packages none of whose sources answers with their size (`head`: a URL's size, nil when it doesn't answer):
    /// the release checks every pin can still be downloaded before it ships (`vm-setup --check-sources`).
    pub fn missing_sources(manifest: &VMSetupManifest, head: &dyn Fn(&str) -> Option<i64>) -> Vec<String> {
        let mut missing = Vec::new();
        for package in manifest.packages.iter().chain(std::iter::once(&manifest.fex_rootfs.package)) {
            let found = manifest.sources(package).into_iter().any(|url| head(&url) == Some(package.size));
            if !found {
                missing.push(package.file.clone());
            }
        }
        missing
    }

    #[allow(clippy::too_many_arguments)]
    pub fn run(
        manifest: &Path,
        vm: &Path,
        mkfs: &Path,
        min_free_gb: i64,
        fetch: &Fetch,
        pause: &Pause,
        running_executables: &dyn Fn() -> Vec<String>,
        progress: &'static (dyn Fn(&str) + Sync),
    ) -> Result<i32, VMSetupError> {
        if VMToolsInstaller::is_developer_folder(vm) {
            progress(&format!(
                "vm-setup: {} is a developer's folder: left alone",
                vm.display()
            ));
            return Ok(0);
        }
        let Some(hash) = Self::manifest_hash(manifest) else {
            return Err(VMSetupError::Missing { path: manifest.display().to_string() });
        };
        // One setup a folder: a crashed app's may still run when the relaunch starts another.
        std::fs::create_dir_all(vm).map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        let lock = posix::open_flock(&vm.join("setup.lock"));
        let Some(_lock) = lock else {
            progress(&format!("vm-setup: another setup runs in {}: this one waits", vm.display()));
            return Ok(75);
        };
        let text = std::fs::read_to_string(manifest).map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        let manifest_value = VMSetupManifest::parse(&text).map_err(|_| VMSetupError::Corrupt)?;
        let setup = vm.join("setup");
        let built = setup.join("images");
        let parts = setup.join("parts");
        std::fs::create_dir_all(&setup).map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        let already = std::fs::read_to_string(built.join(".built-for")).ok() == Some(hash.clone());
        if !already {
            match Self::build(&manifest_value, &hash, vm, mkfs, min_free_gb, fetch, pause, &setup, &built, &parts, progress) {
                Ok(()) => {}
                Err(error) => {
                    // Keep the downloads for the next try, and nothing else: parts, store and a half-built set are gigabytes.
                    for name in ["parts", "store", "images"] {
                        let _ = std::fs::remove_dir_all(setup.join(name));
                    }
                    return Err(error);
                }
            }
        }
        if let Some(running) = Self::running_under(vm, running_executables) {
            progress(&format!(
                "vm-setup: {running} runs from the folder: the new images go in after it ends"
            ));
            return Ok(75);
        }
        progress("progress install 0 1");
        let _ = std::fs::remove_file(built.join(".built-for"));
        let old = vm.join("images");
        let swapped = if old.exists() { posix::rename_swap(&built, &old) } else { posix::rename(&built, &old) };
        if !swapped {
            return Err(VMSetupError::Missing { path: format!("rename {} -> {}", built.display(), old.display()) });
        }
        // The guest's tools, once the images are in: each file replaced in one rename, at its path under share/bin
        // (the ALSA plugin of each width is in lib/ and lib64/).
        for name in walk_names(&parts.join("share/bin")) {
            let source = parts.join("share/bin").join(&name);
            if posix::kind(&source) == posix::Kind::Directory {
                continue;
            }
            let target = vm.join("share/bin").join(&name);
            if let Some(parent) = target.parent() {
                let _ = std::fs::create_dir_all(parent);
            }
            RuntimeInstaller::install_file(&source, &target)
                .map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        }
        let _ = std::fs::remove_dir_all(&setup); // the old set, the downloads and the parts
        progress("progress install 1 1");
        Ok(0)
    }

    /// A HEAD request's Content-Length, when the answer is a 200.
    pub fn head(url: &str) -> Option<i64> {
        let output = Command::new("/usr/bin/curl").args(["-sI", url]).output().ok()?;
        if !output.status.success() {
            return None;
        }
        let text = String::from_utf8_lossy(&output.stdout);
        let mut status_ok = false;
        let mut length: Option<i64> = None;
        for line in text.lines() {
            if let Some(rest) = line.strip_prefix("HTTP/") {
                status_ok = rest.split_whitespace().nth(1) == Some("200");
            } else if let Some((name, value)) = line.split_once(':') {
                if name.eq_ignore_ascii_case("content-length") {
                    length = value.trim().parse::<i64>().ok();
                }
            }
        }
        if status_ok {
            length
        } else {
            None
        }
    }

    /// The download-and-assemble half, whose failure drops everything but the downloads.
    #[allow(clippy::too_many_arguments)]
    fn build(
        manifest: &VMSetupManifest,
        hash: &str,
        vm: &Path,
        mkfs: &Path,
        min_free_gb: i64,
        fetch: &Fetch,
        pause: &Pause,
        setup: &Path,
        built: &Path,
        parts: &Path,
        progress: &'static (dyn Fn(&str) + Sync),
    ) -> Result<(), VMSetupError> {
        Self::check_space(vm, min_free_gb)?;
        let cache = setup.join("cache");
        let mut packages = manifest.packages.clone();
        packages.push(manifest.fex_rootfs.package.clone());
        let mut items = vec![DownloadItem {
            sources: vec![manifest.parts.url.clone()],
            size: manifest.parts.size,
            sha256: manifest.parts.sha256.clone(),
            name: last_component(&manifest.parts.url),
        }];
        for package in &packages {
            items.push(DownloadItem {
                sources: manifest.sources(package),
                size: package.size,
                sha256: package.sha256.clone(),
                name: package.file.clone(),
            });
        }
        let total: i64 = items.iter().map(|item| item.size).sum();
        progress(&format!("progress download 0 {total}"));
        let report: Progress = {
            std::sync::Arc::new(move |done: i64, total: i64| progress(&format!("progress download {done} {total}")))
        };
        VMDownloads::fetch_all(&items, &cache, fetch, pause, &report)?;
        let cached = |sha256: &str| cache.join(sha256);
        let _ = std::fs::remove_dir_all(parts);
        std::fs::create_dir_all(parts).map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        Self::tool(
            "/usr/bin/tar",
            &[
                "-xJf".into(),
                cached(&manifest.parts.sha256).display().to_string(),
                "-C".into(),
                parts.display().to_string(),
            ],
        )?;
        let _ = std::fs::remove_dir_all(built);
        std::fs::create_dir_all(built).map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        let assembler = ImageAssembler::new(setup.join("store"));
        let kernel = manifest
            .packages
            .iter()
            .find(|package| package.name == "kernel-core")
            .ok_or(VMSetupError::Missing { path: "kernel-core".into() })?;
        assembler.extract(
            &cached(&kernel.sha256),
            &format!("/lib/modules/{}/vmlinuz", manifest.kernel),
            &built.join("vmlinuz"),
            None,
        )?;
        progress("progress assemble 1 5");
        assembler.extract(
            &cached(&manifest.fex_rootfs.package.sha256),
            &manifest.fex_rootfs.image,
            &built.join("x86-64-rootfs.erofs"),
            Some(&manifest.fex_rootfs.image_sha256),
        )?;
        progress("progress assemble 2 5");
        let base_text = read_text(&parts.join("recipe/base.tsv"))?;
        let initramfs_text = read_text(&parts.join("recipe/initramfs.tsv"))?;
        let base = Recipe::parse(&base_text)?;
        let initramfs = Recipe::parse(&initramfs_text)?;
        let wanted: std::collections::HashSet<String> = base
            .iter()
            .chain(initramfs.iter())
            .filter_map(|entry| match &entry.kind {
                Kind::File { sha256, source: crate::recipe::Source::Package(_), .. } => Some(sha256.clone()),
                _ => None,
            })
            .collect();
        assembler.collect_overlay(&parts.join("overlay"))?;
        for package in &manifest.packages {
            assembler.collect(&cached(&package.sha256), &wanted)?;
        }
        assembler.erofs(&base, "mn-base", mkfs, &built.join("base.erofs"))?;
        progress("progress assemble 3 5");
        assembler.cpio(&initramfs, &built.join("initramfs.img"))?;
        let _ = std::fs::remove_dir_all(&assembler.store); // done with: the setup's peak on disk is downloads, parts, images
        progress("progress assemble 4 5");
        ImageAssembler::erofs_from_tar(&parts.join("wine.tar"), "mn-wine", mkfs, &built.join("wine.erofs"))?;
        progress("progress assemble 5 5");
        for name in ["cmdline.txt", "packages.lock", "state-template.img"] {
            posix::clone_file(&parts.join(name), &built.join(name)).then_some(()).ok_or(VMSetupError::Corrupt)?;
        }
        std::fs::write(built.join("manifest.sha256"), format!("{hash}\n"))
            .map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        std::fs::write(built.join(".built-for"), hash)
            .map_err(|e| VMSetupError::Missing { path: e.to_string() })?;
        Ok(())
    }

    fn check_space(vm: &Path, gb: i64) -> Result<(), VMSetupError> {
        if let Some(free) = available_capacity(vm) {
            if free < gb * 1_000_000_000 {
                return Err(VMSetupError::NoSpace { needed: gb });
            }
        }
        Ok(())
    }

    fn tool(path: &str, arguments: &[String]) -> Result<(), VMSetupError> {
        let mut command = Command::new(path);
        command.args(arguments);
        let pid = Children::spawn(&mut command).map_err(|_| VMSetupError::Tool(last_component(path), -1))?;
        match Children::wait(pid) {
            Some(0) => Ok(()),
            Some(code) => Err(VMSetupError::Tool(last_component(path), code)),
            None => Err(VMSetupError::Tool(last_component(path), -1)),
        }
    }

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
}

fn last_component(path: &str) -> String {
    Path::new(path).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| path.to_string())
}

fn read_text(path: &Path) -> Result<String, VMSetupError> {
    std::fs::read_to_string(path).map_err(|_| VMSetupError::Missing { path: path.display().to_string() })
}

/// Every path under `root`, relative to it, sorted; empty when `root` isn't readable.
fn walk_names(root: &Path) -> Vec<String> {
    let mut found = Vec::new();
    walk_names_into(root, root, &mut found);
    found.sort();
    found
}

fn walk_names_into(root: &Path, current: &Path, found: &mut Vec<String>) {
    let Ok(entries) = std::fs::read_dir(current) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        if let Ok(relative) = path.strip_prefix(root) {
            found.push(relative.to_string_lossy().into_owned());
        }
        if posix::kind(&path) == posix::Kind::Directory {
            walk_names_into(root, &path, found);
        }
    }
}

/// Free bytes the volume holding `path` reports for important usage (Swift's volumeAvailableCapacityForImportantUsage).
fn available_capacity(path: &Path) -> Option<i64> {
    use std::ffi::CString;
    use std::os::unix::ffi::OsStrExt;
    let c = CString::new(path.as_os_str().as_bytes()).ok()?;
    let mut stat: libc::statvfs = unsafe { std::mem::zeroed() };
    let rc = unsafe { libc::statvfs(c.as_ptr(), &mut stat) };
    if rc != 0 {
        return None;
    }
    Some(stat.f_bavail as i64 * stat.f_frsize as i64)
}
