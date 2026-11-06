//! The Darwin/POSIX filesystem calls the port needs that `std::fs` doesn't expose: symlink-aware stat, atomic link
//! replacement, `RENAME_SWAP`/`RENAME_EXCL`, `copyfile` clones, `statfs` type names, and `O_EXLOCK`. Everything
//! `unsafe` about the platform lives here so the rest of the crate reads like the Swift it replaces.

use std::ffi::CStr;
use std::io;
use std::os::unix::ffi::OsStrExt;
use std::path::Path;

/// What a path is, looked at without following the final link: the same set the Swift code switches on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Missing,
    Link,
    Directory,
    File,
    Other,
}

/// What `path` is, via `lstat(2)`; a missing path is `Missing`, any other error `Other`.
pub fn kind(path: &Path) -> Kind {
    let cpath = match cpath(path) {
        Some(c) => c,
        None => return Kind::Other,
    };
    let mut info: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::lstat(cpath.as_ptr(), &mut info) } != 0 {
        return if io::Error::last_os_error().raw_os_error() == Some(libc::ENOENT) {
            Kind::Missing
        } else {
            Kind::Other
        };
    }
    match info.st_mode & libc::S_IFMT {
        libc::S_IFLNK => Kind::Link,
        libc::S_IFDIR => Kind::Directory,
        libc::S_IFREG => Kind::File,
        _ => Kind::Other,
    }
}

/// `st_dev` of `path` via `stat(2)`, following links.
pub fn device(path: &Path) -> Option<libc::dev_t> {
    let cpath = cpath(path)?;
    let mut info: libc::stat = unsafe { std::mem::zeroed() };
    (unsafe { libc::stat(cpath.as_ptr(), &mut info) } == 0).then_some(info.st_dev)
}

/// `realpath(3)`, or None when `path` doesn't resolve.
pub fn real_path(path: &Path) -> Option<String> {
    let cpath = cpath(path)?;
    let resolved = unsafe { libc::realpath(cpath.as_ptr(), std::ptr::null_mut()) };
    if resolved.is_null() {
        return None;
    }
    let text = unsafe { CStr::from_ptr(resolved) }.to_string_lossy().into_owned();
    unsafe { libc::free(resolved as *mut libc::c_void) };
    Some(text)
}

/// `symlink(2)`: a link at `link` pointing to `target`.
pub fn symlink(target: &str, link: &Path) -> bool {
    let (Some(ctarget), Some(clink)) = (cstr(target), cpath(link)) else { return false };
    unsafe { libc::symlink(ctarget.as_ptr(), clink.as_ptr()) == 0 }
}

/// A new link renamed over the old one, so the path is never missing.
pub fn replace_link(link: &Path, target: &str) -> bool {
    let path = link.as_os_str();
    let mut new = path.to_os_string();
    new.push(".macneutron-new");
    let new = Path::new(&new);
    let _ = std::fs::remove_file(new);
    if !symlink(target, new) {
        return false;
    }
    if rename(new, link) {
        true
    } else {
        let _ = std::fs::remove_file(new);
        false
    }
}

/// `rename(2)`.
pub fn rename(from: &Path, to: &Path) -> bool {
    let (Some(cfrom), Some(cto)) = (cpath(from), cpath(to)) else { return false };
    unsafe { libc::rename(cfrom.as_ptr(), cto.as_ptr()) == 0 }
}

/// `renamex_np` with `RENAME_SWAP`: `from` and `to` exchange places atomically.
pub fn rename_swap(from: &Path, to: &Path) -> bool {
    renamex(from, to, libc::RENAME_SWAP)
}

/// `renamex_np` with `RENAME_EXCL`: never replaces an existing `to`.
pub fn rename_excl(from: &Path, to: &Path) -> bool {
    renamex(from, to, libc::RENAME_EXCL)
}

fn renamex(from: &Path, to: &Path, flags: u32) -> bool {
    let (Some(cfrom), Some(cto)) = (cpath(from), cpath(to)) else { return false };
    unsafe { libc::renamex_np(cfrom.as_ptr(), cto.as_ptr(), flags) == 0 }
}

/// `copyfile(2)` with `COPYFILE_CLONE`: an APFS clone when the volume supports it, else a byte copy;
/// it includes `COPYFILE_EXCL`.
pub fn clone_file(from: &Path, to: &Path) -> bool {
    let (Some(cfrom), Some(cto)) = (cpath(from), cpath(to)) else { return false };
    unsafe {
        libc::copyfile(cfrom.as_ptr(), cto.as_ptr(), std::ptr::null_mut(), libc::COPYFILE_CLONE as libc::copyfile_flags_t) == 0
    }
}

/// `removexattr(2)`: drop `name` from `path`. Missing attributes are not an error.
pub fn remove_xattr(path: &Path, name: &str) -> bool {
    let (Some(cpath), Some(cname)) = (cpath(path), cstr(name)) else { return false };
    unsafe { libc::removexattr(cpath.as_ptr(), cname.as_ptr(), 0) == 0 }
}

/// `lstat(2)`'s `st_mode`, without following the final link.
pub fn mode(path: &Path) -> Option<libc::mode_t> {
    let cpath = cpath(path)?;
    let mut info: libc::stat = unsafe { std::mem::zeroed() };
    (unsafe { libc::lstat(cpath.as_ptr(), &mut info) } == 0).then_some(info.st_mode)
}

/// `lchmod(2)`: set `path`'s mode without following the final link.
pub fn chmod(path: &Path, mode: libc::mode_t) -> bool {
    let Some(cpath) = cpath(path) else { return false };
    unsafe { libc::lchmod(cpath.as_ptr(), mode) == 0 }
}

/// Set `path`'s access and modification times from `source`, keeping nanoseconds.
pub fn copy_times(source: &Path, dest: &Path) -> io::Result<()> {
    use std::os::unix::fs::MetadataExt;
    let source_meta = std::fs::metadata(source)?;
    let atime = libc::timespec {
        tv_sec: source_meta.atime(),
        tv_nsec: source_meta.atime_nsec() as _,
    };
    let mtime = libc::timespec {
        tv_sec: source_meta.mtime(),
        tv_nsec: source_meta.mtime_nsec() as _,
    };
    let times = [atime, mtime];
    let cdest = cpath(dest).ok_or_else(|| io::Error::from_raw_os_error(libc::EINVAL))?;
    if unsafe { libc::utimensat(libc::AT_FDCWD, cdest.as_ptr(), times.as_ptr(), 0) } != 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

/// `statfs(2)`'s filesystem type name, e.g. "apfs".
pub fn filesystem_type(path: &Path) -> Option<String> {
    let cpath = cpath(path)?;
    let mut info: libc::statfs = unsafe { std::mem::zeroed() };
    if unsafe { libc::statfs(cpath.as_ptr(), &mut info) } != 0 {
        return None;
    }
    let name = unsafe { CStr::from_ptr(info.f_fstypename.as_ptr()) };
    Some(name.to_string_lossy().into_owned())
}

/// Open a file holding an exclusive, non-blocking lock (`O_EXLOCK | O_NONBLOCK`), or None when another holder has it.
/// The returned file releases the lock when dropped.
///
/// `O_CLOEXEC` matters here: the lock lives on the open file description, so a helper process that inherited the fd
/// across `fork`+`exec` would keep the description, and the file, locked after the holder dropped it.
pub fn open_exclusive_lock(path: &Path) -> Option<std::fs::File> {
    use std::os::unix::io::FromRawFd;
    let cpath = cpath(path)?;
    let fd = unsafe {
        libc::open(
            cpath.as_ptr(),
            libc::O_RDONLY | libc::O_CREAT | libc::O_EXLOCK | libc::O_NONBLOCK | libc::O_CLOEXEC,
            0o644,
        )
    };
    (fd >= 0).then(|| unsafe { std::fs::File::from_raw_fd(fd) })
}

/// Open `path` (`O_RDWR | O_CREAT | O_CLOEXEC`, mode 0600) and take a non-blocking exclusive `flock` on it, or None when
/// another holder has it. Unlike [`open_exclusive_lock`], the lock is taken by `flock` rather than `O_EXLOCK`:
/// a `setup.lock` may be left over by a crashed setup, so the file is opened even when its lock is held.
/// `O_CLOEXEC` matters for the same reason: a helper spawned while the lock is held must not keep the lock alive.
pub fn open_flock(path: &Path) -> Option<std::fs::File> {
    use std::os::unix::io::FromRawFd;
    let cpath = cpath(path)?;
    let fd = unsafe {
        libc::open(cpath.as_ptr(), libc::O_RDWR | libc::O_CREAT | libc::O_CLOEXEC, 0o600)
    };
    if fd < 0 {
        return None;
    }
    if unsafe { libc::flock(fd, libc::LOCK_EX | libc::LOCK_NB) } != 0 {
        unsafe { libc::close(fd) };
        return None;
    }
    Some(unsafe { std::fs::File::from_raw_fd(fd) })
}

/// The current user's login name: `$USER`, else `getpwuid(getuid())`, else "".
pub fn user_name() -> String {
    if let Some(user) = std::env::var_os("USER") {
        if !user.is_empty() {
            return user.to_string_lossy().into_owned();
        }
    }
    let uid = unsafe { libc::getuid() };
    let mut buf = vec![0u8; 1024];
    let mut pwd: libc::passwd = unsafe { std::mem::zeroed() };
    let mut result: *mut libc::passwd = std::ptr::null_mut();
    let rc = unsafe {
        libc::getpwuid_r(
            uid,
            &mut pwd,
            buf.as_mut_ptr() as *mut libc::c_char,
            buf.len(),
            &mut result,
        )
    };
    if rc == 0 && !result.is_null() && !pwd.pw_name.is_null() {
        return unsafe { CStr::from_ptr(pwd.pw_name) }.to_string_lossy().into_owned();
    }
    String::new()
}

/// The SHA-256 of `bytes` as 64 lowercase hex characters, via CommonCrypto's `CC_SHA256`.
pub fn sha256(bytes: &[u8]) -> String {
    let mut digest = [0u8; 32];
    unsafe {
        CC_SHA256(bytes.as_ptr() as *const libc::c_void, bytes.len() as u32, digest.as_mut_ptr());
    }
    let mut out = String::with_capacity(64);
    for byte in digest {
        out.push_str(&format!("{byte:02x}"));
    }
    out
}

/// The SHA-256 of a file's contents, read in 1 MiB chunks, as 64 lowercase hex characters.
pub fn sha256_file(path: &Path) -> io::Result<String> {
    use std::io::Read;
    let mut file = std::fs::File::open(path)?;
    let mut digest = [0u8; 32];
    let mut context: CC_SHA256_CTX = unsafe { std::mem::zeroed() };
    unsafe { CC_SHA256_Init(&mut context) };
    let mut buffer = vec![0u8; 1 << 20];
    loop {
        let read = file.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        unsafe { CC_SHA256_Update(&mut context, buffer.as_ptr() as *const libc::c_void, read as u32) };
    }
    unsafe { CC_SHA256_Final(digest.as_mut_ptr(), &mut context) };
    let mut out = String::with_capacity(64);
    for byte in digest {
        out.push_str(&format!("{byte:02x}"));
    }
    Ok(out)
}

/// A streaming SHA-256 over bytes fed to it, matching CryptoKit's `SHA256` in the Swift this replaces.
///
/// The Swift code hashes a file's chunks as it copies them (a store file could have changed since); the port keeps
/// the same one-pass shape with a stacked `CC_SHA256_CTX`.
pub struct Sha256 {
    context: CC_SHA256_CTX,
}

impl Default for Sha256 {
    fn default() -> Self {
        Self::new()
    }
}

impl Sha256 {
    pub fn new() -> Self {
        let mut context: CC_SHA256_CTX = unsafe { std::mem::zeroed() };
        unsafe { CC_SHA256_Init(&mut context) };
        Sha256 { context }
    }

    pub fn update(&mut self, bytes: &[u8]) {
        unsafe {
            CC_SHA256_Update(&mut self.context, bytes.as_ptr() as *const libc::c_void, bytes.len() as u32)
        };
    }

    /// The 64 lowercase hex characters of everything fed so far.
    pub fn finalize(self) -> String {
        let mut digest = [0u8; 32];
        let mut context = self.context;
        unsafe { CC_SHA256_Final(digest.as_mut_ptr(), &mut context) };
        let mut out = String::with_capacity(64);
        for byte in digest {
            out.push_str(&format!("{byte:02x}"));
        }
        out
    }
}

#[allow(non_camel_case_types)]
#[repr(C)]
struct CC_SHA256_CTX {
    state: [u32; 8],
    count: [u32; 2],
    buffer: [u8; 64],
}

extern "C" {
    fn CC_SHA256(data: *const libc::c_void, len: u32, md: *mut u8) -> *mut u8;
    fn CC_SHA256_Init(c: *mut CC_SHA256_CTX) -> *mut CC_SHA256_CTX;
    fn CC_SHA256_Update(c: *mut CC_SHA256_CTX, data: *const libc::c_void, len: u32)
        -> *mut CC_SHA256_CTX;
    fn CC_SHA256_Final(md: *mut u8, c: *mut CC_SHA256_CTX) -> *mut u8;
}

/// `FileManager.removeItem`: removes a file, a symlink or a whole tree, like an item's copy (`libkrun.new` is a folder).
pub fn remove_item(path: &Path) -> io::Result<()> {
    if kind(path) == Kind::Directory {
        std::fs::remove_dir_all(path)
    } else {
        std::fs::remove_file(path)
    }
}

/// `samePath`: standardized, symlink-resolved, trailing slashes trimmed.
pub fn same_path(a: &Path, b: &Path) -> bool {
    normal(a) == normal(b)
}

fn normal(path: &Path) -> String {
    let mut text = real_path(path).unwrap_or_else(|| path.to_string_lossy().into_owned());
    while text.len() > 1 && text.ends_with('/') {
        text.pop();
    }
    text
}

fn cpath(path: &Path) -> Option<std::ffi::CString> {
    std::ffi::CString::new(path.as_os_str().as_bytes()).ok()
}

fn cstr(text: &str) -> Option<std::ffi::CString> {
    std::ffi::CString::new(text).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> std::path::PathBuf {
        crate::rpm_fixture::temp(name)
    }

    #[test]
    fn classifies_paths() {
        let dir = temp("kind");
        let file = dir.join("f");
        std::fs::write(&file, b"x").unwrap();
        assert_eq!(kind(&dir), Kind::Directory);
        assert_eq!(kind(&file), Kind::File);
        assert_eq!(kind(&dir.join("missing")), Kind::Missing);
        symlink("f", &dir.join("l"));
        assert_eq!(kind(&dir.join("l")), Kind::Link);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn resolves_and_compares() {
        let dir = temp("same");
        let file = dir.join("f");
        std::fs::write(&file, b"x").unwrap();
        assert!(real_path(&file).is_some());
        assert!(same_path(&dir, &dir));
        assert!(!same_path(&dir, &file));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn swaps_and_clones() {
        let dir = temp("swap");
        let a = dir.join("a");
        let b = dir.join("b");
        std::fs::write(&a, b"a").unwrap();
        std::fs::write(&b, b"b").unwrap();
        assert!(rename_swap(&a, &b));
        assert_eq!(std::fs::read(&a).unwrap(), b"b");
        let dest = dir.join("c");
        assert!(clone_file(&a, &dest));
        assert_eq!(std::fs::read(&dest).unwrap(), b"b");
        // RENAME_EXCL never clobbers an existing destination.
        assert!(!rename_excl(&dest, &a));
        let fresh = dir.join("d");
        assert!(rename_excl(&dest, &fresh));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn hashes_sha256() {
        assert_eq!(
            sha256(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        let dir = temp("sha");
        let file = dir.join("f");
        std::fs::write(&file, b"abc").unwrap();
        assert_eq!(
            sha256_file(&file).unwrap(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn reports_apfs_and_names() {
        let dir = temp("apfs");
        assert_eq!(filesystem_type(&dir).as_deref(), Some("apfs"));
        assert!(!user_name().is_empty());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn exclusive_lock_conflicts_and_closes_on_exec() {
        use std::os::unix::io::AsRawFd;
        let dir = temp("lock");
        let path = dir.join("held.lock");
        let held = open_exclusive_lock(&path).unwrap();
        // A second holder is refused while the first is alive (separate open, same lock).
        assert!(open_exclusive_lock(&path).is_none());
        // The lock on `held` must not survive exec in a child: were the fd inherited, the child would keep the
        // open file description, and with it the lock, alive after `held` is dropped.
        assert_ne!(unsafe { libc::fcntl(held.as_raw_fd(), libc::F_GETFD) } & libc::FD_CLOEXEC, 0);
        drop(held);
        assert!(open_exclusive_lock(&path).is_some());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn flock_conflicts_and_closes_on_exec() {
        use std::os::unix::io::AsRawFd;
        let dir = temp("flock");
        let path = dir.join("setup.lock");
        let held = open_flock(&path).unwrap();
        // A second holder is refused while the first is alive (separate open, same flock).
        assert!(open_flock(&path).is_none());
        // The lock on `held` must not survive exec in a child: were the fd inherited, the child would keep the
        // open file description, and with it the lock, alive after `held` is dropped.
        assert_ne!(unsafe { libc::fcntl(held.as_raw_fd(), libc::F_GETFD) } & libc::FD_CLOEXEC, 0);
        drop(held);
        assert!(open_flock(&path).is_some());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
