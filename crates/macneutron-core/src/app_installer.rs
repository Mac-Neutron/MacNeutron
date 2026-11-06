//! Spec 2026-10-25-macneutron-installer-design §3.2, steps 2-5: the bundled app verified, MacNeutron quit, a copy
//! beside the target without the downloaded installer's quarantine, swapped in. `Sources/MacNeutronInstaller` is its
//! front: the confirmation, the alerts, quitting by bundle id, the reopening.

use std::fmt;
use std::path::{Path, PathBuf};

use crate::posix;
use crate::process_runner::ProcessRunner;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum AppInstallerError {
    /// The target's folder (no trailing "/"): your account can't write there.
    NotWritable(String),
    StillRunning,
    /// ditto's exit status.
    CopyFailed(i32),
    /// An interrupted install's staging copy that couldn't be removed.
    Leftover(String),
}

impl fmt::Display for AppInstallerError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppInstallerError::NotWritable(folder) => write!(
                formatter,
                "Your account can't write to {folder}: drag MacNeutron.app there yourself, or use an administrator account."
            ),
            AppInstallerError::StillRunning => write!(
                formatter,
                "MacNeutron didn't quit within 30 seconds: quit it, then open Install MacNeutron again."
            ),
            AppInstallerError::CopyFailed(status) => write!(
                formatter,
                "MacNeutron.app couldn't be copied into Applications (ditto exited {status})."
            ),
            AppInstallerError::Leftover(path) => write!(
                formatter,
                "{path} couldn't be removed: move it to the Trash, then open Install MacNeutron again."
            ),
        }
    }
}

impl std::error::Error for AppInstallerError {}

/// Where the app goes.
pub fn target() -> PathBuf {
    PathBuf::from("/Applications/MacNeutron.app")
}

/// The path without a trailing "/", for ditto and the messages.
fn trimmed(path: &Path) -> String {
    let mut text = path.to_string_lossy().into_owned();
    while text.len() > 1 && text.ends_with('/') {
        text.pop();
    }
    text
}

/// Every failure before the swap leaves `target` as it was, and a failed swap puts it back
/// ([`crate::updater::swap`]). `quit` asks every running MacNeutron to quit and says whether none is left.
///
/// `verify` and `quit` are injected so the installer binary's Security-framework checks and its
/// `lsappinfo`/`osascript` work can be replaced in tests.
#[allow(clippy::too_many_arguments)]
pub fn install<Verify, Quit>(
    bundled: &Path,
    target: &Path,
    runner: &dyn ProcessRunner,
    verify: &Verify,
    quit: &Quit,
) -> Result<(), AppInstallerError>
where
    Verify: Fn(&Path) -> Result<(), AppInstallerError>,
    Quit: Fn() -> bool,
{
    verify(bundled)?;
    let folder = target.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("/"));
    if !writable(&folder) {
        return Err(AppInstallerError::NotWritable(trimmed(&folder)));
    }
    let staging = folder.join(".MacNeutron.app.installing");
    let aside = folder.join(".MacNeutron.app.old");
    // A crash between updater::swap's renames left the only app aside: it goes back first
    // (updater::discard keeps it too).
    if !exists(target) && exists(&aside) {
        std::fs::rename(&aside, target).map_err(|_| AppInstallerError::Leftover(trimmed(&aside)))?;
    }
    // An interrupted install's, cleared before MacNeutron quits: one left would fail the swap.
    for leftover in [&staging, &aside] {
        if exists(leftover) {
            remove_item(leftover.as_path()).map_err(|_| AppInstallerError::Leftover(trimmed(leftover)))?;
        }
    }
    if !quit() {
        return Err(AppInstallerError::StillRunning);
    }
    let arguments = vec![trimmed(bundled), trimmed(&staging)];
    let status = runner
        .run(Path::new("/usr/bin/ditto"), &arguments, &Default::default(), None)
        .map_err(|_| AppInstallerError::CopyFailed(-1))?;
    let result = (|| {
        if status != 0 {
            return Err(AppInstallerError::CopyFailed(status));
        }
        strip_quarantine(&staging).map_err(|_| AppInstallerError::Leftover(trimmed(&staging)))?;
        if exists(target) {
            crate::updater::swap(&staging, target, &aside)?;
            let _ = remove_item(&aside);
        } else {
            std::fs::rename(&staging, target).map_err(|_| AppInstallerError::Leftover(trimmed(target)))?;
        }
        Ok(())
    })();
    let _ = remove_item(&staging);
    result
}

/// `com.apple.quarantine` off the bundle and everything in it; a file without it is fine. A file without u+w
/// (EACCES) gets it for the removal, then its mode back.
pub fn strip_quarantine(bundle: &Path) -> std::io::Result<()> {
    let root = trimmed(bundle);
    let mut files = vec![root.clone()];
    let mut stack = vec![PathBuf::from(&root)];
    while let Some(folder) = stack.pop() {
        let entries = match std::fs::read_dir(&folder) {
            Ok(entries) => entries,
            Err(_) => continue,
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let name = path.to_string_lossy().into_owned();
            files.push(name);
            if posix::kind(&path) == posix::Kind::Directory {
                stack.push(path);
            }
        }
    }
    for file in files {
        let path = Path::new(&file);
        let mut failed = remove_errno(path);
        if failed == libc::EACCES {
            if let Some(mode) = posix::mode(path) {
                if posix::chmod(path, mode | libc::S_IWUSR as libc::mode_t) {
                    failed = remove_errno(path);
                    posix::chmod(path, mode);
                }
            }
        }
        if failed != 0 && failed != libc::ENOATTR {
            return Err(std::io::Error::from_raw_os_error(failed));
        }
    }
    Ok(())
}

/// `removexattr`, giving the errno rather than a bool.
fn remove_errno(path: &Path) -> i32 {
    if posix::remove_xattr(path, "com.apple.quarantine") {
        return 0;
    }
    std::io::Error::last_os_error().raw_os_error().unwrap_or(0)
}

fn exists(path: &Path) -> bool {
    std::fs::symlink_metadata(path).is_ok()
}

fn writable(path: &Path) -> bool {
    let Some(cpath) = std::ffi::CString::new(path.as_os_str().as_encoded_bytes()).ok() else {
        return false;
    };
    // `access(W_OK)`: the check FileManager.isWritableFile makes.
    unsafe { libc::access(cpath.as_ptr(), libc::W_OK) == 0 }
}

/// Removes a file or a directory, since `std::fs::remove_file` no-ops on directories.
fn remove_item(path: &Path) -> std::io::Result<()> {
    match std::fs::symlink_metadata(path) {
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        Err(error) => Err(error),
        Ok(meta) if meta.is_dir() => std::fs::remove_dir_all(path),
        Ok(_) => std::fs::remove_file(path),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_support::{make_temp_dir, write};
    use std::os::unix::fs::PermissionsExt;

    /// A stand-in for the installer's bundled app: <dir>/MacNeutron.app, its Contents/v naming the version.
    fn bundle_at(at: &Path, version: &str) -> PathBuf {
        let app = at.join("MacNeutron.app");
        write(version, &app.join("Contents/v"), false);
        app
    }

    fn path(url: &Path) -> String {
        let mut text = url.to_string_lossy().into_owned();
        while text.len() > 1 && text.ends_with('/') {
            text.pop();
        }
        text
    }

    /// What macOS sets on a downloaded installer's files, and its copies inherit.
    fn quarantine(url: &Path) {
        let value = "0081;00000000;Test;";
        let name = std::ffi::CString::new("com.apple.quarantine").unwrap();
        let path = std::ffi::CString::new(path(url)).unwrap();
        let ok = unsafe {
            libc::setxattr(
                path.as_ptr(),
                name.as_ptr(),
                value.as_ptr() as *const libc::c_void,
                value.len(),
                0,
                libc::XATTR_NOFOLLOW,
            ) == 0
        };
        assert!(ok, "setxattr failed for {}", url.display());
    }

    fn has_quarantine(url: &Path) -> bool {
        let name = std::ffi::CString::new("com.apple.quarantine").unwrap();
        let path = std::ffi::CString::new(path(url)).unwrap();
        unsafe { libc::getxattr(path.as_ptr(), name.as_ptr(), std::ptr::null_mut(), 0, 0, libc::XATTR_NOFOLLOW) >= 0 }
    }

    fn version(app: &Path) -> Option<String> {
        std::fs::read_to_string(app.join("Contents/v")).ok()
    }

    /// The staging names an install leaves beside the target, if any.
    fn staged(folder: &Path) -> Vec<String> {
        [".MacNeutron.app.installing", ".MacNeutron.app.old"]
            .iter()
            .filter(|name| folder.join(name).exists())
            .map(|name| name.to_string())
            .collect()
    }

    struct SetUp {
        bundled: PathBuf,
        apps: PathBuf,
        target: PathBuf,
    }

    /// A source with the new app and an Applications folder, with an old app in it when `old` is set.
    fn set_up(old: Option<&str>) -> SetUp {
        let bundled = bundle_at(&make_temp_dir(), "new");
        let apps = make_temp_dir().join("Applications");
        std::fs::create_dir_all(&apps).unwrap();
        if let Some(old) = old {
            let _ = bundle_at(&apps, old);
        }
        let target = apps.join("MacNeutron.app");
        SetUp { bundled, apps, target }
    }

    #[test]
    fn installs_fresh_and_strips_quarantine() {
        let s = set_up(None);
        quarantine(&s.bundled);
        quarantine(&s.bundled.join("Contents/v"));
        let verified = std::cell::RefCell::new(Vec::new());
        let quits = std::cell::Cell::new(0);
        let verify = |b: &Path| {
            verified.borrow_mut().push(b.to_path_buf());
            Ok(())
        };
        let quit = || {
            quits.set(quits.get() + 1);
            true
        };
        install(&s.bundled, &s.target, &ok_runner(), &verify, &quit).unwrap();
        assert_eq!(version(&s.target).as_deref(), Some("new"));
        assert!(!has_quarantine(&s.target));
        assert!(!has_quarantine(&s.target.join("Contents/v")));
        assert!(has_quarantine(&s.bundled));
        assert_eq!(verified.borrow().as_slice(), std::slice::from_ref(&s.bundled));
        assert_eq!(quits.get(), 1);
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn replaces_the_app_in_applications() {
        let s = set_up(Some("old"));
        install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &|| true).unwrap();
        assert_eq!(version(&s.target).as_deref(), Some("new"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn a_failed_verify_changes_nothing() {
        let s = set_up(Some("old"));
        let quits = std::cell::Cell::new(0);
        let verify = |_: &Path| Err(AppInstallerError::StillRunning);
        let quit = || {
            quits.set(quits.get() + 1);
            true
        };
        let error = install(&s.bundled, &s.target, &ok_runner(), &verify, &quit).unwrap_err();
        assert_eq!(error, AppInstallerError::StillRunning);
        assert_eq!(quits.get(), 0);
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn an_app_that_wont_quit_changes_nothing() {
        let s = set_up(Some("old"));
        let error = install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &|| false).unwrap_err();
        assert_eq!(error, AppInstallerError::StillRunning);
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn an_unwritable_folder_changes_nothing() {
        let s = set_up(Some("old"));
        let mut permissions = std::fs::metadata(&s.apps).unwrap().permissions();
        permissions.set_mode(0o555);
        std::fs::set_permissions(&s.apps, permissions).unwrap();
        let quits = std::cell::Cell::new(0);
        let quit = || {
            quits.set(quits.get() + 1);
            true
        };
        let error = install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &quit).unwrap_err();
        assert_eq!(error, AppInstallerError::NotWritable(path(&s.apps)));
        assert_eq!(quits.get(), 0);
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        let mut back = std::fs::metadata(&s.apps).unwrap().permissions();
        back.set_mode(0o755);
        std::fs::set_permissions(&s.apps, back).unwrap();
    }

    #[test]
    fn an_interrupted_installs_leftovers_are_cleared() {
        let s = set_up(Some("old"));
        write("junk", &s.apps.join(".MacNeutron.app.installing/junk"), false);
        write("junk", &s.apps.join(".MacNeutron.app.old/junk"), false);
        install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &|| true).unwrap();
        assert_eq!(version(&s.target).as_deref(), Some("new"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn a_read_only_source_installs() {
        let s = set_up(None);
        let source = s.bundled.parent().unwrap().to_path_buf();
        let mut permissions = std::fs::metadata(&source).unwrap().permissions();
        permissions.set_mode(0o555);
        std::fs::set_permissions(&source, permissions).unwrap();
        install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &|| true).unwrap();
        assert_eq!(version(&s.target).as_deref(), Some("new"));
        let mut back = std::fs::metadata(&source).unwrap().permissions();
        back.set_mode(0o755);
        std::fs::set_permissions(&source, back).unwrap();
    }

    #[test]
    fn installer_messages_are_exact() {
        assert_eq!(
            AppInstallerError::NotWritable("/Applications".into()).to_string(),
            "Your account can't write to /Applications: drag MacNeutron.app there yourself, or use an administrator account."
        );
        assert_eq!(
            AppInstallerError::StillRunning.to_string(),
            "MacNeutron didn't quit within 30 seconds: quit it, then open Install MacNeutron again."
        );
    }

    #[test]
    fn a_copy_that_fails_changes_nothing() {
        let s = set_up(Some("old"));
        let missing = make_temp_dir().join("MacNeutron.app");
        let error = install(&missing, &s.target, &ok_runner(), &|_| Ok(()), &|| true).unwrap_err();
        assert!(matches!(error, AppInstallerError::CopyFailed(_)), "got {error:?}");
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn an_app_left_aside_by_a_crash_is_put_back() {
        let s = set_up(None);
        write("old", &s.apps.join(".MacNeutron.app.old/Contents/v"), false);
        let missing = make_temp_dir().join("MacNeutron.app");
        assert!(install(&missing, &s.target, &ok_runner(), &|_| Ok(()), &|| true).is_err());
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        assert!(staged(&s.apps).is_empty());
    }

    #[test]
    fn a_leftover_that_wont_go_stops_before_quitting() {
        let s = set_up(Some("old"));
        let leftover = s.apps.join(".MacNeutron.app.old");
        write("junk", &leftover.join("junk"), false);
        let mut permissions = std::fs::metadata(&leftover).unwrap().permissions();
        permissions.set_mode(0o555);
        std::fs::set_permissions(&leftover, permissions).unwrap();
        let quits = std::cell::Cell::new(0);
        let quit = || {
            quits.set(quits.get() + 1);
            true
        };
        let error = install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &quit).unwrap_err();
        assert_eq!(error, AppInstallerError::Leftover(path(&leftover)));
        assert_eq!(quits.get(), 0);
        assert_eq!(version(&s.target).as_deref(), Some("old"));
        let mut back = std::fs::metadata(&leftover).unwrap().permissions();
        back.set_mode(0o755);
        std::fs::set_permissions(&leftover, back).unwrap();
        assert_eq!(
            AppInstallerError::Leftover("/Applications/.MacNeutron.app.old".into()).to_string(),
            "/Applications/.MacNeutron.app.old couldn't be removed: move it to the Trash, then open Install MacNeutron again."
        );
    }

    #[test]
    fn a_read_only_file_loses_its_quarantine_and_keeps_its_mode() {
        let s = set_up(None);
        let file = s.bundled.join("Contents/read-only");
        write("x", &file, false);
        quarantine(&file);
        let mut permissions = std::fs::metadata(&file).unwrap().permissions();
        permissions.set_mode(0o444);
        std::fs::set_permissions(&file, permissions).unwrap();
        install(&s.bundled, &s.target, &ok_runner(), &|_| Ok(()), &|| true).unwrap();
        let installed = s.target.join("Contents/read-only");
        assert!(!has_quarantine(&installed));
        assert_eq!(std::fs::metadata(&installed).unwrap().permissions().mode() & 0o777, 0o444);
    }

    /// A runner whose `ditto` copies source to destination, so the install's steps line up as the real one's do.
    /// A source that isn't there fails, as the real ditto does.
    fn ok_runner() -> crate::test_support::FakeRunner {
        crate::test_support::FakeRunner::new(|call| {
            if call.tool == "ditto" {
                let (Some(from), Some(to)) = (call.arguments.first(), call.arguments.get(1)) else { return 1 };
                if !Path::new(from).exists() {
                    return 1;
                }
                let _ = copy_recursive(Path::new(from), Path::new(to));
            }
            0
        })
    }

    fn copy_recursive(from: &Path, to: &Path) -> std::io::Result<()> {
        if from.is_file() {
            return std::fs::copy(from, to).map(|_| ());
        }
        std::fs::create_dir_all(to)?;
        for entry in std::fs::read_dir(from)? {
            let entry = entry?;
            let target = to.join(entry.file_name());
            if entry.file_type()?.is_dir() {
                copy_recursive(&entry.path(), &target)?;
            } else {
                std::fs::copy(entry.path(), &target)?;
            }
        }
        Ok(())
    }
}
