//! Exclusive `flock(2)` on a path for the duration of `body`. Separate `open`s conflict even within one
//! process, so this also serialises threads.

use std::fs::OpenOptions;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::AsRawFd;
use std::path::Path;

pub fn with_file_lock<T>(path: &Path, body: impl FnOnce() -> T) -> std::io::Result<T> {
    let file = OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .mode(0o644)
        .open(path)?;
    let fd = file.as_raw_fd();
    if unsafe { libc_flock(fd, LOCK_EX) } != 0 {
        return Err(std::io::Error::last_os_error());
    }
    let result = body();
    unsafe { libc_flock(fd, LOCK_UN) };
    Ok(result)
}

const LOCK_EX: i32 = 2;
const LOCK_UN: i32 = 8;

extern "C" {
    #[link_name = "flock"]
    fn libc_flock(fd: i32, operation: i32) -> i32;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn locks_and_runs_the_body() {
        let dir = std::env::temp_dir().join(format!("mn-lock-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("x.lock");
        let value = with_file_lock(&path, || 42).unwrap();
        assert_eq!(value, 42);
        assert!(path.exists());
        let _ = std::fs::remove_dir_all(&dir);
    }
}
