//! The images from recipes (spec §4.5): every file's bytes go into a store, one file per SHA-256 (so names that differ
//! only by case on the Mac's disk never meet), from the packages' payloads and the parts' overlay; then a recipe becomes
//! a tar piped into mkfs.erofs, or the initramfs's cpio.

use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use crate::cpio::CPIOWriter;
use crate::posix;
use crate::recipe::{Kind, RecipeEntry};
use crate::rpm_reader::RPMReader;
use crate::tar_writer::TarWriter;
use crate::vm_setup::Children;
use crate::vm_setup_error::VMSetupError;

/// The images from recipes, from the packages' payloads and the parts' overlay.
pub struct ImageAssembler {
    pub store: PathBuf,
}

impl ImageAssembler {
    pub fn new(store: PathBuf) -> Self {
        ImageAssembler { store }
    }

    /// Payload files whose SHA-256 is in `wanted`, each written beside the store and kept under its hash.
    pub fn collect(&self, rpm: &Path, wanted: &std::collections::HashSet<String>) -> Result<(), VMSetupError> {
        std::fs::create_dir_all(&self.store).map_err(|_| VMSetupError::Corrupt)?;
        RPMReader::read_files(rpm, |_header, _paths, next| {
            let temporary = self.store.join(format!(".incoming-{}", unique()));
            let hash = Self::copy(next, &temporary)?;
            let kept = self.store.join(&hash);
            if wanted.contains(&hash) && !kept.exists() {
                posix::rename(&temporary, &kept).then_some(()).ok_or(VMSetupError::Corrupt)?;
            } else {
                let _ = std::fs::remove_file(&temporary);
            }
            Ok(())
        })
    }

    /// The overlay's files (each named by its SHA-256), cloned into the store.
    pub fn collect_overlay(&self, folder: &Path) -> Result<(), VMSetupError> {
        std::fs::create_dir_all(&self.store).map_err(|_| VMSetupError::Corrupt)?;
        let mut names: Vec<String> = std::fs::read_dir(folder)
            .map_err(|_| VMSetupError::Missing { path: folder.to_string_lossy().into_owned() })?
            .filter_map(|e| e.ok().map(|e| e.file_name().to_string_lossy().into_owned()))
            .collect();
        names.sort();
        for name in names {
            let kept = self.store.join(&name);
            if !kept.exists() {
                // an APFS clone
                posix::clone_file(&folder.join(&name), &kept).then_some(()).ok_or(VMSetupError::Corrupt)?;
            }
        }
        Ok(())
    }

    /// One payload file to `to` (vmlinuz, FEX's default.erofs), checked against `sha256` when given (a checked
    /// package already fixes its payload's bytes).
    pub fn extract(&self, rpm: &Path, path: &str, to: &Path, sha256: Option<&str>) -> Result<(), VMSetupError> {
        let mut found = false;
        RPMReader::read_files(rpm, |_header, paths, next| {
            if found || !paths.contains(&path.to_string()) {
                return Ok(());
            }
            found = true;
            let hash = Self::copy(next, to)?;
            if let Some(expected) = sha256 {
                if hash != expected {
                    let _ = std::fs::remove_file(to);
                    return Err(VMSetupError::Mismatch { file: path.to_string() });
                }
            }
            Ok(())
        })?;
        if !found {
            return Err(VMSetupError::Missing { path: path.to_string() });
        }
        Ok(())
    }

    /// mkfs.erofs --tar=f -L label -zlz4hc out, the recipe's tar on its stdin (it reads a tar from there when given no
    /// source). A missing file stops it, and `out` is removed.
    pub fn erofs(&self, recipe: &[RecipeEntry], label: &str, mkfs: &Path, out: &Path) -> Result<(), VMSetupError> {
        let mut command = Self::mkfs(mkfs, label, out, None);
        command.stdin(Stdio::piped());
        let mut child = command.spawn().map_err(|_| VMSetupError::Tool("mkfs.erofs".into(), -1))?;
        let stdin = child.stdin.take().ok_or_else(|| VMSetupError::Tool("mkfs.erofs".into(), -1))?;
        let pid = Children::run(child, 0);
        let result = Self::write_tar(stdin, recipe, self);
        if let Err(error) = result {
            // ours: stop it; a broken pipe: it stopped
            let ours = matches!(
                error,
                VMSetupError::Missing { .. } | VMSetupError::Mismatch { .. } | VMSetupError::Unsupported(_)
            );
            if ours && Children::is_running(pid) {
                Children::terminate(pid);
            }
            let status = Children::wait(pid);
            let _ = std::fs::remove_file(out);
            if !ours {
                if let Some(code) = status.filter(|code| *code != 0) {
                    return Err(VMSetupError::Tool("mkfs.erofs".into(), code));
                }
            }
            return Err(error);
        }
        match Children::wait(pid) {
            Some(0) => Ok(()),
            Some(code) => {
                let _ = std::fs::remove_file(out);
                Err(VMSetupError::Tool("mkfs.erofs".into(), code))
            }
            None => {
                let _ = std::fs::remove_file(out);
                Err(VMSetupError::Tool("mkfs.erofs".into(), -1))
            }
        }
    }

    /// The same for a ready tar file (the parts' wine.tar).
    pub fn erofs_from_tar(tar: &Path, label: &str, mkfs: &Path, out: &Path) -> Result<(), VMSetupError> {
        let mut command = Self::mkfs(mkfs, label, out, Some(tar));
        let pid = Children::spawn(&mut command)?;
        match Children::wait(pid) {
            Some(0) => Ok(()),
            Some(code) => {
                let _ = std::fs::remove_file(out);
                Err(VMSetupError::Tool("mkfs.erofs".into(), code))
            }
            None => {
                let _ = std::fs::remove_file(out);
                Err(VMSetupError::Tool("mkfs.erofs".into(), -1))
            }
        }
    }

    pub fn cpio(&self, recipe: &[RecipeEntry], out: &Path) -> Result<(), VMSetupError> {
        let file = File::create(out).map_err(|_| VMSetupError::Corrupt)?;
        let mut writer = CPIOWriter::new(std::io::BufWriter::new(file));
        for entry in recipe {
            writer.add(entry, self.contents(entry)?.as_deref())?;
        }
        writer.finish()
    }

    /// A file entry's bytes in the store.
    fn contents(&self, entry: &RecipeEntry) -> Result<Option<PathBuf>, VMSetupError> {
        let Kind::File { sha256, .. } = &entry.kind else { return Ok(None) };
        let url = self.store.join(sha256);
        if !url.exists() {
            return Err(VMSetupError::Missing { path: entry.path.clone() });
        }
        Ok(Some(url))
    }

    fn mkfs(tool: &Path, label: &str, out: &Path, source: Option<&Path>) -> Command {
        let mut command = Command::new(tool);
        command.arg("--tar=f").arg("-L").arg(label).arg("-zlz4hc").arg(out);
        if let Some(source) = source {
            command.arg(source);
        }
        command.stdout(Stdio::null());
        command
    }

    /// The file's chunks into `to`, returning their SHA-256.
    fn copy(next: &mut dyn FnMut() -> Result<Option<Vec<u8>>, VMSetupError>, to: &Path) -> Result<String, VMSetupError> {
        let file = File::create(to).map_err(|_| VMSetupError::Corrupt)?;
        let mut handle = std::io::BufWriter::new(file);
        let mut hash = posix::Sha256::new();
        while let Some(chunk) = next()? {
            hash.update(&chunk);
            handle.write_all(&chunk).map_err(|_| VMSetupError::Corrupt)?;
        }
        handle.flush().map_err(|_| VMSetupError::Corrupt)?;
        Ok(hash.finalize())
    }

    fn write_tar(stdin: std::process::ChildStdin, recipe: &[RecipeEntry], assembler: &ImageAssembler) -> Result<(), VMSetupError> {
        let mut tar = TarWriter::new(stdin);
        for entry in recipe {
            tar.add(entry, assembler.contents(entry)?.as_deref())?;
        }
        tar.finish()
    }
}

fn unique() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let n = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    format!("{n}")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::posix;
    use crate::recipe::{RecipeEntry, Source};
    use crate::rpm_fixture::{self, File};
    use std::collections::HashSet;
    use std::process::Command;

    fn sha(data: &[u8]) -> String {
        posix::sha256(data)
    }

    fn temp(name: &str) -> PathBuf {
        crate::rpm_fixture::temp(name)
    }

    /// Stands in for mkfs.erofs (`--tar=f -L <label> -zlz4hc <out> [tar]`): the tar it's given becomes <out>.
    fn fake_mkfs() -> PathBuf {
        let tool = temp(&format!("mkfs-{}", unique())).join("mkfs.erofs");
        std::fs::write(&tool, b"#!/bin/sh\nif [ -n \"$6\" ]; then cp \"$6\" \"$5\"; else cat > \"$5\"; fi\n").unwrap();
        let mut perms = std::fs::metadata(&tool).unwrap().permissions();
        use std::os::unix::fs::PermissionsExt;
        perms.set_mode(0o755);
        std::fs::set_permissions(&tool, perms).unwrap();
        tool
    }

    fn tar(arguments: &[&str]) -> Vec<u8> {
        let out = Command::new("/usr/bin/tar").args(arguments).output().unwrap();
        out.stdout
    }

    fn dir(path: &str) -> RecipeEntry {
        RecipeEntry { path: path.into(), mode: 0o755, uid: 0, gid: 0, kind: Kind::Directory, capability: None }
    }

    fn file(path: &str, bytes: &[u8], source: Source, mode: u16) -> RecipeEntry {
        RecipeEntry {
            path: path.into(),
            mode,
            uid: 0,
            gid: 0,
            kind: Kind::File { size: bytes.len() as i64, sha256: sha(bytes), source },
            capability: None,
        }
    }

    struct Fixture {
        assembler: ImageAssembler,
        rpm: PathBuf,
        overlay: PathBuf,
    }

    /// A package with /usr/bin/demo and the two /a files, and an overlay holding "from the overlay\n".
    fn make_fixture() -> Fixture {
        let tmp = temp(&format!("fixture-{}", unique()));
        let rpm = tmp.join("demo-1.0-1.fc44.aarch64.rpm");
        rpm_fixture::write(
            &rpm,
            &rpm_fixture::rpm_bytes("demo", "1.0", "1.fc44", "aarch64", "cpio", &[
                File { path: "/usr/bin/demo".into(), bytes: b"demo\n".to_vec(), inode: 1 },
                File { path: "/a/Readme".into(), bytes: b"lower\n".to_vec(), inode: 2 },
                File { path: "/a/README".into(), bytes: b"UPPER\n".to_vec(), inode: 3 },
            ]),
        );
        let overlay = tmp.join("overlay");
        std::fs::create_dir_all(&overlay).unwrap();
        std::fs::write(overlay.join(sha(b"from the overlay\n")), b"from the overlay\n").unwrap();
        Fixture { assembler: ImageAssembler::new(tmp.join("store")), rpm, overlay }
    }

    fn wanted_of(recipe: &[RecipeEntry]) -> HashSet<String> {
        recipe
            .iter()
            .filter_map(|e| match &e.kind {
                Kind::File { sha256, source: Source::Package(_), .. } => Some(sha256.clone()),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn a_recipe_becomes_a_tar_of_package_and_overlay_files() {
        let f = make_fixture();
        let pkg = Source::Package(f.rpm.file_name().unwrap().to_string_lossy().into_owned());
        let recipe = vec![
            dir("/"),
            dir("/usr"),
            dir("/usr/bin"),
            file("/usr/bin/demo", b"demo\n", pkg, 0o755),
            dir("/etc"),
            file("/etc/motd", b"from the overlay\n", Source::Overlay, 0o644),
        ];
        f.assembler.collect(&f.rpm, &wanted_of(&recipe)).unwrap();
        f.assembler.collect_overlay(&f.overlay).unwrap();
        let out = temp(&format!("tar-{}", unique())).join("base.erofs");
        f.assembler.erofs(&recipe, "mn-base", &fake_mkfs(), &out).unwrap();
        let listing = String::from_utf8_lossy(&tar(&["-tvf", out.to_str().unwrap()])).into_owned();
        assert!(listing.contains("-rwxr-xr-x  0 0      0           5 ") && listing.contains(" usr/bin/demo\n"), "{listing}");
        assert_eq!(tar(&["-xOf", out.to_str().unwrap(), "etc/motd"]), b"from the overlay\n");
        assert_eq!(tar(&["-xOf", out.to_str().unwrap(), "usr/bin/demo"]), b"demo\n");
    }

    #[test]
    fn a_missing_file_fails_with_its_path() {
        let f = make_fixture();
        let recipe = vec![dir("/"), dir("/etc"), file("/etc/x", b"nobody has this\n", Source::Overlay, 0o644)];
        f.assembler.collect_overlay(&f.overlay).unwrap();
        let out = temp(&format!("missing-{}", unique())).join("base.erofs");
        let error = f.assembler.erofs(&recipe, "mn-base", &fake_mkfs(), &out).unwrap_err();
        assert_eq!(error, VMSetupError::Missing { path: "/etc/x".into() });
    }

    #[test]
    fn paths_differing_only_in_case_both_survive() {
        let f = make_fixture();
        let pkg = Source::Package(f.rpm.file_name().unwrap().to_string_lossy().into_owned());
        let recipe = vec![
            dir("/"),
            dir("/a"),
            file("/a/Readme", b"lower\n", pkg.clone(), 0o644),
            file("/a/README", b"UPPER\n", pkg, 0o644),
        ];
        let wanted: HashSet<String> = [sha(b"lower\n"), sha(b"UPPER\n")].into_iter().collect();
        f.assembler.collect(&f.rpm, &wanted).unwrap();
        let out = temp(&format!("case-{}", unique())).join("base.erofs");
        f.assembler.erofs(&recipe, "mn-base", &fake_mkfs(), &out).unwrap();
        assert_eq!(tar(&["-xOf", out.to_str().unwrap(), "a/Readme"]), b"lower\n");
        assert_eq!(tar(&["-xOf", out.to_str().unwrap(), "a/README"]), b"UPPER\n");
    }

    #[test]
    fn extract_checks_the_bytes() {
        let f = make_fixture();
        let to = temp(&format!("extract-{}", unique())).join("demo");
        f.assembler.extract(&f.rpm, "/usr/bin/demo", &to, Some(&sha(b"demo\n"))).unwrap();
        assert_eq!(std::fs::read(&to).unwrap(), b"demo\n");
        let error = f.assembler.extract(&f.rpm, "/usr/bin/demo", &to, Some(&sha(b"other\n"))).unwrap_err();
        assert_eq!(error, VMSetupError::Mismatch { file: "/usr/bin/demo".into() });
    }

    #[test]
    fn the_initramfs_is_a_newc_cpio() {
        let f = make_fixture();
        f.assembler.collect_overlay(&f.overlay).unwrap();
        let out = temp(&format!("cpio-{}", unique())).join("initramfs.img");
        f.assembler
            .cpio(&[dir("/"), dir("/etc"), file("/etc/motd", b"from the overlay\n", Source::Overlay, 0o644)], &out)
            .unwrap();
        let bytes = std::fs::read(&out).unwrap();
        assert_eq!(&bytes[..6], b"070701");
        assert!(bytes.windows(10).any(|w| w == b"TRAILER!!!"));
    }

    #[test]
    fn a_mkfs_that_dies_says_so() {
        // It exits before reading its tar: the setup names mkfs.erofs and its status, not a broken pipe.
        let f = make_fixture();
        let big = vec![1u8; 4 << 20];
        f.assembler.collect_overlay(&f.overlay).unwrap();
        std::fs::create_dir_all(&f.assembler.store).unwrap();
        std::fs::write(f.assembler.store.join(sha(&big)), &big).unwrap();
        let dying = temp(&format!("dying-{}", unique())).join("mkfs.erofs");
        std::fs::write(&dying, b"#!/bin/sh\nexit 3\n").unwrap();
        use std::os::unix::fs::PermissionsExt;
        let mut perms = std::fs::metadata(&dying).unwrap().permissions();
        perms.set_mode(0o755);
        std::fs::set_permissions(&dying, perms).unwrap();
        let out = temp(&format!("dieout-{}", unique())).join("base.erofs");
        let error = f
            .assembler
            .erofs(&[dir("/"), file("/big", &big, Source::Overlay, 0o644)], "mn-base", &dying, &out)
            .unwrap_err();
        assert_eq!(error, VMSetupError::Tool("mkfs.erofs".into(), 3));
    }

    #[test]
    fn a_damaged_store_file_is_caught() {
        // A file an earlier run left in the store, changed since: its bytes aren't the recipe's.
        let f = make_fixture();
        let bytes = b"from the overlay\n";
        f.assembler.collect_overlay(&f.overlay).unwrap();
        std::fs::write(f.assembler.store.join(sha(bytes)), b"changed!\n").unwrap();
        let out = temp(&format!("damaged-{}", unique())).join("base.erofs");
        let error = f
            .assembler
            .erofs(&[dir("/"), dir("/etc"), file("/etc/motd", bytes, Source::Overlay, 0o644)], "mn-base", &fake_mkfs(), &out)
            .unwrap_err();
        assert_eq!(error, VMSetupError::Mismatch { file: "/etc/motd".into() });
    }
}
