//! The processes running on this Mac, by executable.

const MAXPATHLEN: usize = 1024;
const PROC_PIDPATHINFO_MAXSIZE: usize = 4 * MAXPATHLEN;

extern "C" {
    fn proc_listallpids(buffer: *mut i32, buffersize: i32) -> i32;
    fn proc_pidpath(pid: i32, buffer: *mut u8, buffersize: u32) -> i32;
}

/// The kernel's executable path of every process this user can see (`/private/var/…`, not argv, which Wine
/// rewrites). Processes of other users and ones that exit mid-scan are skipped.
pub fn executable_paths() -> Vec<String> {
    unsafe {
        let estimate = proc_listallpids(std::ptr::null_mut(), 0);
        if estimate <= 0 {
            return Vec::new();
        }
        // Room for processes started since.
        let mut pids = vec![0i32; estimate as usize + 64];
        let count = proc_listallpids(pids.as_mut_ptr(), (pids.len() * 4) as i32);
        if count <= 0 {
            return Vec::new();
        }
        pids.truncate(count as usize);
        let mut paths = Vec::new();
        for pid in pids {
            let mut buffer = vec![0u8; PROC_PIDPATHINFO_MAXSIZE];
            let length = proc_pidpath(pid, buffer.as_mut_ptr(), buffer.len() as u32);
            if length > 0 {
                buffer.truncate(length as usize);
                paths.push(String::from_utf8_lossy(&buffer).into_owned());
            }
        }
        paths
    }
}
