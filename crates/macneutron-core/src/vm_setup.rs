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
        let guard = Self::running().lock().unwrap();
        if let Some((child, _)) = guard.iter().find(|(child, _)| child.id() == pid) {
            sigterm(child.id());
        }
    }

    /// Every child, or one group's.
    pub fn terminate_all(group: Option<i32>) {
        for (child, g) in Self::running().lock().unwrap().iter() {
            if group.map(|group| group == *g).unwrap_or(true) {
                sigterm(child.id());
            }
        }
    }
}

/// Swift's `Process.terminate()`: SIGTERM, which tar and mkfs.erofs can clean up after (not SIGKILL).
fn sigterm(pid: u32) {
    unsafe { libc::kill(pid as libc::pid_t, libc::SIGTERM) };
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::micro_vm::MicroVM;
    use crate::rpm_fixture::{self, File};
    use crate::test_support::{make_temp_dir, write};
    use crate::valve_client::Fetch;
    use crate::vm_downloads::standard_fetch;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;

    fn sha(data: &[u8]) -> String {
        posix::sha256(data)
    }

    fn sha_file(path: &Path) -> String {
        posix::sha256_file(path).unwrap()
    }

    fn run_tool(tool: &str, arguments: &[&str]) {
        assert!(Command::new(tool).args(arguments).status().unwrap().success(), "{tool} {arguments:?}");
    }

    /// Counts the requests of the setup's real downloader (file: URLs here).
    struct Requests(Arc<AtomicUsize>);

    impl Requests {
        fn new() -> Self {
            Requests(Arc::new(AtomicUsize::new(0)))
        }

        fn made(&self) -> usize {
            self.0.load(Ordering::SeqCst)
        }

        fn fetch(&self) -> Fetch {
            let count = Arc::clone(&self.0);
            let inner = standard_fetch();
            Arc::new(move |url: &str| {
                count.fetch_add(1, Ordering::SeqCst);
                inner(url)
            })
        }
    }

    struct SetupFixture {
        manifest: PathBuf,
        mkfs: PathBuf,
        vm: PathBuf,
    }

    fn size(path: &Path) -> u64 {
        std::fs::metadata(path).unwrap().len()
    }

    /// A kernel package (vmlinuz and /usr/bin/demo), FEX's root package, and parts whose recipes build a tiny base and
    /// initramfs, all served from file: URLs; mkfs.erofs stands in as a script that keeps the tar it's given.
    fn make_setup() -> SetupFixture {
        let tmp = make_temp_dir();
        let rpms = tmp.join("rpms");
        std::fs::create_dir_all(&rpms).unwrap();
        let kver = "7.2.10-200.fc44.aarch64";
        let demo = b"demo\n";
        let init_bytes = b"#!/bin/sh\n";
        let kernel = rpms.join("kernel-core-7.2.10-200.fc44.aarch64.rpm");
        rpm_fixture::write(
            &kernel,
            &rpm_fixture::rpm_bytes("kernel-core", "7.2.10", "200.fc44", "aarch64", "cpio", &[
                File { path: format!("/lib/modules/{kver}/vmlinuz"), bytes: b"kernel\n".to_vec(), inode: 1 },
                File { path: "/usr/bin/demo".into(), bytes: demo.to_vec(), inode: 2 },
            ]),
        );
        let fex = rpms.join("fex-emu-rootfs-fedora-1-1.fc44.noarch.rpm");
        let erofs = b"an x86-64 root\n";
        rpm_fixture::write(
            &fex,
            &rpm_fixture::rpm_bytes("fex-emu-rootfs-fedora", "1", "1.fc44", "noarch", "cpio", &[File {
                path: "/usr/share/fex-emu/RootFS/default.erofs".into(),
                bytes: erofs.to_vec(),
                inode: 1,
            }]),
        );
        let parts = tmp.join("parts");
        write(
            &format!(
                "d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nd\t/usr\t755\t0\t0\t-\t-\t-\t-\t-\nd\t/usr/bin\t755\t0\t0\t-\t-\t-\t-\t-\n\
                 f\t/usr/bin/demo\t755\t0\t0\t5\t{}\tpkg:{}\t-\t-\n",
                sha(demo),
                kernel.file_name().unwrap().to_string_lossy()
            ),
            &parts.join("recipe/base.tsv"),
            false,
        );
        write(
            &format!("d\t/\t755\t0\t0\t-\t-\t-\t-\t-\nf\t/init\t755\t0\t0\t10\t{}\toverlay\t-\t-\n", sha(init_bytes)),
            &parts.join("recipe/initramfs.tsv"),
            false,
        );
        std::fs::create_dir_all(parts.join("overlay")).unwrap();
        std::fs::write(parts.join("overlay").join(sha(init_bytes)), init_bytes).unwrap();
        write("wine\n", &tmp.join("wine/bin/wine"), false);
        run_tool("/usr/bin/tar", &["-C", tmp.join("wine").to_str().unwrap(), "-cf", parts.join("wine.tar").to_str().unwrap(), "."]);
        for (name, text) in [
            ("cmdline.txt", "root=LABEL=mn-base\n"),
            ("packages.lock", "kernel-core\n"),
            ("state-template.img", "empty ext4\n"),
            ("share/bin/mn-input", "input\n"),
            ("share/bin/lib/plugin.so", "a 32-bit plugin\n"),
        ] {
            write(text, &parts.join(name), false);
        }
        let archive = tmp.join("parts.tar.xz");
        run_tool("/usr/bin/tar", &["-C", parts.to_str().unwrap(), "-cJf", archive.to_str().unwrap(), "."]);
        let package = |url: &Path, name: &str, version: &str, release: &str, arch: &str| {
            serde_json::json!({
                "file": url.file_name().unwrap().to_string_lossy(), "name": name, "version": version,
                "release": release, "arch": arch, "size": size(url), "sha256": sha_file(url),
            })
        };
        let manifest = serde_json::json!({
            "version": 1,
            "parts": {"url": format!("file://{}", archive.display()), "size": size(&archive), "sha256": sha_file(&archive)},
            "kernel": kver,
            "packages": [package(&kernel, "kernel-core", "7.2.10", "200.fc44", "aarch64")],
            "fexRootfs": {"package": package(&fex, "fex-emu-rootfs-fedora", "1", "1.fc44", "noarch"),
                          "image": "/usr/share/fex-emu/RootFS/default.erofs", "imageSha256": sha(erofs)},
            "koji": {"base": "https://kojipkgs.fedoraproject.org/packages/", "signedKey": "6d9f90a6"},
            "mirrors": [format!("file://{}/", rpms.display())],
        });
        let manifest_file = tmp.join("vm-manifest.json");
        std::fs::write(&manifest_file, serde_json::to_vec(&manifest).unwrap()).unwrap();
        let mkfs = tmp.join("mkfs.erofs");
        write("#!/bin/sh\nif [ -n \"$6\" ]; then cp \"$6\" \"$5\"; else cat > \"$5\"; fi\n", &mkfs, true);
        SetupFixture { manifest: manifest_file, mkfs, vm: tmp.join("vm") }
    }

    fn set_up(f: &SetupFixture, requests: &Requests, min_free_gb: i64, running: &dyn Fn() -> Vec<String>) -> Result<i32, VMSetupError> {
        let pause: Pause = Arc::new(|_| {});
        VMSetup::run(&f.manifest, &f.vm, &f.mkfs, min_free_gb, &requests.fetch(), &pause, running, &|_| {})
    }

    fn none() -> Vec<String> {
        Vec::new()
    }

    #[test]
    fn a_full_run_leaves_a_complete_image_set() {
        let f = make_setup();
        write("a game's disk\n", &f.vm.join("state/1.img"), false);
        assert_eq!(set_up(&f, &Requests::new(), 0, &none), Ok(0));
        let images = f.vm.join("images");
        for name in [
            "vmlinuz", "initramfs.img", "base.erofs", "x86-64-rootfs.erofs", "wine.erofs", "state-template.img",
            "cmdline.txt", "packages.lock",
        ] {
            assert!(images.join(name).exists(), "{name}");
        }
        assert_eq!(
            std::fs::read_to_string(images.join("manifest.sha256")).unwrap(),
            format!("{}\n", VMSetup::manifest_hash(&f.manifest).unwrap())
        );
        assert_eq!(std::fs::read(images.join("vmlinuz")).unwrap(), b"kernel\n");
        assert_eq!(std::fs::read(images.join("x86-64-rootfs.erofs")).unwrap(), b"an x86-64 root\n");
        assert_eq!(std::fs::read(f.vm.join("share/bin/mn-input")).unwrap(), b"input\n");
        // A tool in a folder of share/bin (the ALSA plugin per width, lib/ and lib64/): at the same path.
        assert_eq!(std::fs::read(f.vm.join("share/bin/lib/plugin.so")).unwrap(), b"a 32-bit plugin\n");
        assert_eq!(std::fs::read(f.vm.join("state/1.img")).unwrap(), b"a game's disk\n");
        assert!(!f.vm.join("setup").exists());
        assert!(MicroVM::images_complete(&f.vm));
    }

    #[test]
    fn a_failed_setup_leaves_only_its_downloads() {
        // A failure keeps the cache for the next try and nothing else: parts, store and a half-built set are gigabytes.
        let f = make_setup();
        write("#!/bin/sh\nexit 1\n", &f.mkfs, true);
        assert_eq!(set_up(&f, &Requests::new(), 0, &none), Err(VMSetupError::Tool("mkfs.erofs".into(), 1)));
        let left: Vec<String> = std::fs::read_dir(f.vm.join("setup"))
            .unwrap()
            .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        assert_eq!(left, vec!["cache".to_string()]);
    }

    #[test]
    fn too_little_space_is_refused_before_any_download() {
        let f = make_setup();
        let requests = Requests::new();
        assert!(matches!(set_up(&f, &requests, 100_000, &none), Err(VMSetupError::NoSpace { .. })));
        assert_eq!(requests.made(), 0);
    }

    #[test]
    fn a_running_vm_defers_the_swap_and_the_next_run_finishes_it() {
        let f = make_setup();
        write("old\n", &f.vm.join("images/manifest.sha256"), false);
        let vm_executable = f.vm.join("macneutron-vm").to_string_lossy().into_owned();
        assert_eq!(set_up(&f, &Requests::new(), 0, &|| vec![vm_executable.clone()]), Ok(75));
        // The store goes once the images are built: the setup's peak on disk is downloads, parts and images.
        assert!(!f.vm.join("setup/store").exists());
        assert_eq!(std::fs::read_to_string(f.vm.join("images/manifest.sha256")).unwrap(), "old\n");
        let requests = Requests::new();
        assert_eq!(set_up(&f, &requests, 0, &none), Ok(0));
        assert_eq!(requests.made(), 0);
        assert_eq!(
            std::fs::read_to_string(f.vm.join("images/manifest.sha256")).unwrap(),
            format!("{}\n", VMSetup::manifest_hash(&f.manifest).unwrap())
        );
    }

    #[test]
    fn the_developers_folder_is_left_alone() {
        let f = make_setup();
        let target = make_temp_dir().join("vm-work");
        write("dev\n", &target.join("images/manifest.sha256"), false); // vm/image/build.sh's
        std::os::unix::fs::symlink(&target, &f.vm).unwrap();
        assert_eq!(set_up(&f, &Requests::new(), 0, &none), Ok(0));
        let left: Vec<String> = std::fs::read_dir(&target)
            .unwrap()
            .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        assert_eq!(left, vec!["images".to_string()]);
        assert_eq!(std::fs::read_to_string(target.join("images/manifest.sha256")).unwrap(), "dev\n");
    }

    #[test]
    fn the_manifests_hash_leaves_out_where_the_parts_are() {
        // A release whose VM didn't change moves only the parts' URL: the image set is the same, nothing downloads again.
        let dir = make_temp_dir();
        let (a, b, c) = (dir.join("a.json"), dir.join("b.json"), dir.join("c.json"));
        write(r#"{"version": 1, "parts": {"url": "https://x/v0.6.0/p.tar.xz", "size": 3, "sha256": "aa"}, "kernel": "k"}"#, &a, false);
        write(r#"{"kernel": "k", "parts": {"sha256": "aa", "size": 3, "url": "https://x/v0.6.1/p.tar.xz"}, "version": 1}"#, &b, false);
        write(r#"{"version": 1, "parts": {"url": "https://x/v0.6.1/p.tar.xz", "size": 3, "sha256": "bb"}, "kernel": "k"}"#, &c, false);
        assert_eq!(VMSetup::manifest_hash(&a), VMSetup::manifest_hash(&b));
        assert_ne!(VMSetup::manifest_hash(&a), VMSetup::manifest_hash(&c));
    }

    #[test]
    fn a_second_setup_in_the_folder_waits() {
        // A crashed app's setup still running when the relaunch starts another: one at a time, the other waits (75).
        let f = make_setup();
        let requests = Requests::new();
        std::fs::create_dir_all(&f.vm).unwrap();
        let held = posix::open_flock(&f.vm.join("setup.lock")).expect("lock");
        assert_eq!(set_up(&f, &requests, 0, &none), Ok(75));
        assert_eq!(requests.made(), 0);
        drop(held);
    }
}
