// MacNeutronVM: boots the EAC guest (Fedora's signed kernel, our EROFS images) in a libkrun microVM, in this process.
// A 1:1 Rust translation of the Swift host (macneutron-vm.swift); see that file's header for the full interface. The
// guest's virtio-gpu has the dxmt context, served by the Metal transport's executor in this process.
//   macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]
//                 [--steam-appid <id> [--steam-map <host dir>=<guest dir>]] [--state <file>] [--metal-test-ops]
//                 [--user-share <tag>=<host dir>]... [--ro-share <tag>=<host dir>]... [--tpm <dir>]
mod ffi;

use ffi::*;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::process::Command;
use std::ptr;
use std::sync::atomic::{AtomicI32, Ordering};

const MN_AUDIO_PORT: u32 = 5102;
const STEAM_VSOCK_PORT: u32 = 5101;

/// The console-copy thread and the Steam child both need a plain C string for the life of the process; leaking a
/// handful of strings mirrors the Swift host's `strdup`-and-never-free kstr().
fn kstr(s: &str) -> KrunStr {
    let cs = CString::new(s).unwrap_or_else(|_| CString::new("").unwrap());
    let p = cs.into_raw();
    KrunStr {
        data: p as *const c_char,
        len: s.len(),
    }
}

fn cstring(s: &str) -> CString {
    CString::new(s).unwrap_or_else(|_| CString::new("").unwrap())
}

fn fail(code: i32, s: &str) -> ! {
    eprintln!("{}", s);
    unsafe { _exit(code) }
}

fn errno() -> i32 {
    unsafe { *__error() }
}

fn sysctl_int(name: &str) -> i64 {
    let cs = cstring(name);
    let mut v: i32 = 0;
    let mut n = std::mem::size_of::<i32>();
    let rc = unsafe {
        sysctlbyname(
            cs.as_ptr(),
            &mut v as *mut i32 as *mut c_void,
            &mut n,
            ptr::null_mut(),
            0,
        )
    };
    if rc == 0 {
        v as i64
    } else {
        0
    }
}

fn sysctl_string(name: &str) -> String {
    let cs = cstring(name);
    let mut n: usize = 0;
    let rc = unsafe {
        sysctlbyname(cs.as_ptr(), ptr::null_mut(), &mut n, ptr::null_mut(), 0)
    };
    if rc != 0 || n == 0 {
        return String::new();
    }
    let mut buf = vec![0u8; n];
    let rc = unsafe {
        sysctlbyname(
            cs.as_ptr(),
            buf.as_mut_ptr() as *mut c_void,
            &mut n,
            ptr::null_mut(),
            0,
        )
    };
    if rc == 0 {
        let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
        String::from_utf8_lossy(&buf[..end]).into_owned()
    } else {
        String::new()
    }
}

/// Every performance core plus half the efficiency cores. Chips name their levels (M5 Pro: "Super" and
/// "Performance", no "Efficiency"), so only a level named Efficiency is halved.
fn default_cpus() -> i64 {
    let levels = sysctl_int("hw.nperflevels");
    let mut total = 0i64;
    for level in 0..levels {
        let cores = sysctl_int(&format!("hw.perflevel{}.physicalcpu", level));
        let name = sysctl_string(&format!("hw.perflevel{}.name", level));
        total += if name == "Efficiency" { cores / 2 } else { cores };
    }
    total
}

/// MN_VM_CPUS: the vCPUs when --cpus doesn't say, 1 to the Mac's cores; anything else keeps the default, and says so.
fn environment_cpus() -> Option<i64> {
    let v = std::env::var("MN_VM_CPUS").ok()?;
    if let Ok(n) = v.parse::<i64>() {
        if n >= 1 && n <= sysctl_int("hw.physicalcpu") {
            return Some(n);
        }
    }
    eprintln!(
        "macneutron-vm: MN_VM_CPUS={} isn't a core count, {} instead",
        v,
        default_cpus()
    );
    None
}

fn check(r: KrunResult, what: &str) {
    if r != KRUN_RESULT_SUCCESS {
        let name = unsafe { CStr::from_ptr(krun_result_name_cstr(r)) };
        fail(1, &format!("{}: {}", what, name.to_string_lossy()));
    }
}

fn made<T>(handle: *mut T, err: *mut *mut KrunError, what: &str) -> *mut T {
    if !err.is_null() {
        let e = unsafe { *err };
        if !e.is_null() {
            let r = unsafe { krun_error_result(e) };
            check(r, what);
        }
    }
    if handle.is_null() {
        fail(1, &format!("{}: no handle", what));
    }
    handle
}

fn flag_present(args: &[String], flag: &str) -> bool {
    args.iter().any(|a| a == flag)
}

fn main() {
    let argv_raw: Vec<String> = std::env::args().collect();
    let metal_test_ops = flag_present(&argv_raw, "--metal-test-ops");
    let print_cpus = flag_present(&argv_raw, "--print-cpus");
    let print_cmdline = flag_present(&argv_raw, "--print-cmdline");
    let argv: Vec<String> = argv_raw
        .iter()
        .filter(|a| {
            !["--metal-test-ops", "--print-cpus", "--print-cmdline"].contains(&a.as_str())
        })
        .cloned()
        .collect();

    // Options are (flag, value) pairs starting at index 1; the last flag without a value is dropped, as in Swift.
    let mut opts: HashMap<String, String> = HashMap::new();
    let mut i = 1usize;
    while i + 1 < argv.len() {
        opts.insert(argv[i].clone(), argv[i + 1].clone());
        i += 2;
    }

    let cpus = opts
        .get("--cpus")
        .and_then(|s| s.parse::<i64>().ok())
        .or_else(environment_cpus)
        .unwrap_or_else(default_cpus);
    if print_cpus {
        println!("{}", cpus);
        unsafe { _exit(0) }
    }

    let images = match opts.get("--images") {
        Some(v) => v.clone(),
        None => fail(
            64,
            "usage: macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]",
        ),
    };
    let share = match opts.get("--share") {
        Some(v) => v.clone(),
        None => fail(
            64,
            "usage: macneutron-vm --images <dir> --share <dir> [--cpus N] [--ram-mib M] [--console <file>] [--timeout <s>]",
        ),
    };
    let ram_mib: u32 = opts
        .get("--ram-mib")
        .and_then(|s| s.parse::<u32>().ok())
        .unwrap_or(8192);
    let console = opts
        .get("--console")
        .cloned()
        .unwrap_or_else(|| format!("{}/console.log", share));
    let timeout: i64 = opts
        .get("--timeout")
        .and_then(|s| s.parse::<i64>().ok())
        .unwrap_or(0);
    let steam_appid = opts.get("--steam-appid").cloned();
    let steam_map = opts.get("--steam-map").cloned().unwrap_or_else(|| {
        let home = std::env::var("HOME").unwrap_or_default();
        format!(
            "{}/Library/Application Support/Steam/steamapps/common=/mnt/games",
            home
        )
    });

    let path = |n: &str| format!("{}/{}", images, n);
    let state = opts.get("--state").cloned().unwrap_or_else(|| {
        // <images>/../state/default.img
        let parent = std::path::Path::new(&images)
            .parent()
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| std::path::PathBuf::from("/"));
        parent.join("state/default.img").to_string_lossy().into_owned()
    });
    let exe_dir = std::path::Path::new(&argv_raw[0])
        .parent()
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .to_string_lossy()
        .into_owned();

    for f in ["vmlinuz", "initramfs.img", "cmdline.txt", "base.erofs", "x86-64-rootfs.erofs", "wine.erofs"]
        .iter()
        .map(|n| path(n))
        .chain(std::iter::once(state.clone()))
    {
        if unsafe { access(cstring(&f).as_ptr(), R_OK) } != 0 {
            fail(2, &format!("missing image: {}", f));
        }
    }

    // One VM per state disk: two writers would corrupt its ext4. The lock lives as long as this process.
    let state_lock = unsafe { open(cstring(&state).as_ptr(), O_RDWR) };
    if state_lock < 0 || unsafe { flock(state_lock, LOCK_EX | LOCK_NB) } != 0 {
        fail(3, &format!("{} is in use", state));
    }

    // A state disk shorter than its ext4 filesystem grows back to the filesystem's size (see the Swift comment).
    let mut superblock = [0u8; 1024];
    let got = unsafe {
        pread(
            state_lock,
            superblock.as_mut_ptr() as *mut c_void,
            1024,
            1024,
        )
    };
    if got == 1024 && superblock[56] == 0x53 && superblock[57] == 0xef {
        let le32 = |at: usize| -> u64 {
            (0..4).fold(0u64, |acc, k| {
                acc | ((superblock[at + k] as u64) << (8 * k as u64))
            })
        };
        let hi = if le32(0x60) & 0x80 != 0 {
            le32(0x150) << 32
        } else {
            0
        };
        let blocks = le32(4) | hi;
        let bytes = blocks << (10 + le32(24) as u32);
        let mut st: ffi::stat = unsafe { std::mem::zeroed() };
        if unsafe { fstat(state_lock, &mut st) } == 0 && (st.st_size as u64) < bytes {
            if unsafe { ftruncate(state_lock, bytes as i64) } != 0 {
                fail(
                    3,
                    &format!("{}: can't grow it back to its filesystem's {} bytes", state, bytes),
                );
            }
            eprintln!(
                "macneutron-vm: {} was {} bytes, its filesystem {}: grown back",
                state, st.st_size, bytes
            );
        }
    }

    // MN_VM_CMDLINE_EXTRA: tick and RCU parameters appended to the images' command line, for measuring. Allowlisted
    // whole, and refused whole (the kernel takes a parameter's last value, so lockdown=, module.sig_enforce= or init=
    // must never get in).
    let cmdline_extra: String = {
        match std::env::var("MN_VM_CMDLINE_EXTRA") {
            Ok(v) if !v.is_empty() => {
                let allowed = ["nohz_full=", "rcu_nocbs=", "nohz=", "skew_tick=", "irqaffinity="];
                let words: Vec<&str> = v.split(' ').collect();
                let ok = words.iter().all(|w| {
                    allowed.iter().any(|a| w.starts_with(a))
                        && w.chars().all(|c| {
                            c.is_ascii() && (c.is_ascii_alphanumeric() || "=_,-".contains(c))
                        })
                });
                if ok {
                    format!(" {}", words.join(" "))
                } else {
                    eprintln!(
                        "macneutron-vm: MN_VM_CMDLINE_EXTRA={} refused: only {}",
                        v,
                        allowed.join(" ")
                    );
                    String::new()
                }
            }
            _ => String::new(),
        }
    };
    let cmdline = format!(
        "{}{}",
        std::fs::read_to_string(path("cmdline.txt"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default(),
        cmdline_extra
    );
    if print_cmdline {
        println!("{}", cmdline);
        unsafe { _exit(0) }
    }

    // MN_KRUN_LOG: libkrun log level, 0 (off) to 5 (trace); warnings and errors by default.
    let log_level: u32 = std::env::var("MN_KRUN_LOG")
        .ok()
        .and_then(|s| s.parse::<u32>().ok())
        .unwrap_or(KRUN_LOG_LEVEL_WARN);
    let mut err: *mut KrunError = ptr::null_mut();
    check(
        unsafe { krun_init_log(-1, log_level, KRUN_LOG_STYLE_AUTO, 0, &mut err) },
        "log level",
    );

    let devices = unsafe { krun_mmio_device_manager_new() };
    let disk = |id: &str, name: &str, read_only: bool| {
        let file = if name.starts_with('/') {
            name.to_string()
        } else {
            path(name)
        };
        let mut e: *mut KrunError = ptr::null_mut();
        let d = made(
            unsafe {
                krun_block_device_new(
                    kstr(id),
                    kstr(&file),
                    KRUN_DISK_FORMAT_RAW,
                    &mut e,
                )
            },
            &mut e,
            name,
        );
        unsafe { krun_block_device_set_read_only(d, read_only) };
        let mut e2: *mut KrunError = ptr::null_mut();
        let d2 = made(d as *mut KrunAttachDevice, &mut e2, name);
        unsafe { krun_mmio_device_manager_add(devices, d2) };
    };

    // The guest console (hvc0) goes to the console file. libkrun makes port 0 hvc0 only for a terminal, so it gets a
    // pty whose other side is copied into the file.
    let console_fd = unsafe {
        open(
            cstring(&console).as_ptr(),
            O_WRONLY | O_CREAT | O_TRUNC,
            0o644,
        )
    };
    let mut pty_master: c_int = -1;
    let mut pty_slave: c_int = -1;
    if console_fd < 0
        || unsafe {
            openpty(
                &mut pty_master,
                &mut pty_slave,
                ptr::null_mut(),
                ptr::null_mut(),
                ptr::null_mut(),
            )
        } != 0
    {
        let msg = unsafe { CStr::from_ptr(strerror(errno())) };
        fail(1, &format!("console: {}", msg.to_string_lossy()));
    }
    std::thread::spawn(move || {
        let mut buf = [0u8; 65536];
        loop {
            let n = unsafe { read(pty_master, buf.as_mut_ptr() as *mut c_void, buf.len()) };
            if n <= 0 {
                break;
            }
            let mut off = 0usize;
            while off < n as usize {
                let w = unsafe {
                    write(
                        console_fd,
                        buf[off..].as_ptr() as *const c_void,
                        n as usize - off,
                    )
                };
                if w <= 0 {
                    return;
                }
                off += w as usize;
            }
        }
    });
    let consoles = unsafe { krun_console_device_builder() };
    let mut port: u32 = 0;
    let mut e: *mut KrunError = ptr::null_mut();
    check(
        unsafe { krun_console_builder_add_tty_port(consoles, kstr(""), pty_slave, &mut port, &mut e) },
        "console port",
    );
    let mut e: *mut KrunError = ptr::null_mut();
    let console_dev = made(unsafe { krun_console_builder_build(consoles, &mut e) }, &mut e, "console");
    unsafe { krun_mmio_device_manager_add(devices, console_dev as *mut KrunAttachDevice) };

    disk("base", "base.erofs", true);
    disk("x86-64", "x86-64-rootfs.erofs", true);
    disk("wine", "wine.erofs", true);
    disk("state", &state, false);

    let mut e: *mut KrunError = ptr::null_mut();
    let share_dev = made(
        unsafe { krun_fs_device_new(kstr("share"), kstr(&share), &mut e) },
        &mut e,
        "share",
    );
    unsafe { krun_mmio_device_manager_add(devices, share_dev as *mut KrunAttachDevice) };

    // The Mac's Steam library (the map's host side), read-only as tag `games` at /mnt/games.
    let games: String = steam_map.chars().take_while(|&c| c != '=').collect();
    if unsafe { access(cstring(&games).as_ptr(), R_OK) } == 0 {
        let mut e: *mut KrunError = ptr::null_mut();
        let g = made(
            unsafe { krun_fs_device_new_read_only(kstr("games"), kstr(&games), &mut e) },
            &mut e,
            "games",
        );
        unsafe { krun_mmio_device_manager_add(devices, g as *mut KrunAttachDevice) };
    }

    let mut i = 1usize;
    while i + 1 < argv.len() {
        if argv[i] == "--user-share" {
            let share = &argv[i + 1];
            let eq = match share.find('=') {
                Some(e) => e,
                None => fail(64, &format!("--user-share <tag>=<host dir>, not {}", share)),
            };
            let tag = &share[..eq];
            let dir = &share[eq + 1..];
            if unsafe { access(cstring(dir).as_ptr(), R_OK | W_OK) } != 0 {
                fail(2, &format!("missing share: {}", dir));
            }
            let mut e: *mut KrunError = ptr::null_mut();
            let d = made(
                unsafe { krun_fs_device_new_simplified(kstr(tag), kstr(dir), &mut e) },
                &mut e,
                tag,
            );
            unsafe { krun_mmio_device_manager_add(devices, d as *mut KrunAttachDevice) };
        }
        i += 2;
    }
    let mut i = 1usize;
    while i + 1 < argv.len() {
        if argv[i] == "--ro-share" {
            let share = &argv[i + 1];
            let eq = match share.find('=') {
                Some(e) => e,
                None => fail(64, &format!("--ro-share <tag>=<host dir>, not {}", share)),
            };
            let tag = &share[..eq];
            let dir = &share[eq + 1..];
            if unsafe { access(cstring(dir).as_ptr(), R_OK) } != 0 {
                fail(2, &format!("missing share: {}", dir));
            }
            let mut e: *mut KrunError = ptr::null_mut();
            let d = made(
                unsafe { krun_fs_device_new_read_only(kstr(tag), kstr(dir), &mut e) },
                &mut e,
                tag,
            );
            unsafe { krun_mmio_device_manager_add(devices, d as *mut KrunAttachDevice) };
        }
        i += 2;
    }

    let mut e: *mut KrunError = ptr::null_mut();
    let rng = made(unsafe { krun_rng_device_new(&mut e) }, &mut e, "rng");
    unsafe { krun_mmio_device_manager_add(devices, rng as *mut KrunAttachDevice) };

    // Free page reporting gives the guest's freed memory back to macOS: Replace swaps reported pages for fresh memory.
    let mut e: *mut KrunError = ptr::null_mut();
    let balloon = made(unsafe { krun_balloon_device_new(&mut e) }, &mut e, "balloon");
    unsafe { krun_balloon_device_set_free_page_reporting_mode(balloon, KRUN_FREE_PAGE_REPORTING_MODE_REPLACE) };
    unsafe { krun_mmio_device_manager_add(devices, balloon as *mut KrunAttachDevice) };

    // The Metal transport: winemetal.so beside our stand-ins for Wine's libraries (vm/metal/wm).
    let metal_host = std::fs::canonicalize(&argv_raw[0])
        .ok()
        .and_then(|p| p.parent().map(|p| p.to_path_buf()))
        .map(|p| p.join("metal-host"))
        .unwrap_or_else(|| std::path::PathBuf::from("metal-host"))
        .to_string_lossy()
        .into_owned();
    let wm_loaded = unsafe { mn_wm_load(cstring(&metal_host).as_ptr()) } == 0;
    if !wm_loaded {
        eprintln!(
            "macneutron-vm: no winemetal.so in {}: Metal calls are refused",
            metal_host
        );
    }
    // Direct3D forwarding: native DXMT beside macneutron-vm, loaded by the first Direct3D record.
    let dxmt_dir = std::path::Path::new(&metal_host)
        .parent()
        .map(|p| p.join("dxmt"))
        .unwrap_or_else(|| std::path::PathBuf::from("dxmt"));
    unsafe { mn_d3d_set_dir(cstring(&dxmt_dir.to_string_lossy()).as_ptr()) };
    let _ = std::fs::create_dir_all(format!("{}/traces", share));
    if std::env::var("DXMT_LOG_LEVEL").as_deref() != Ok("none") {
        unsafe { setenv(cstring("DXMT_LOG_PATH").as_ptr(), cstring(&format!("{}/traces", share)).as_ptr(), 0) };
    }
    if let Ok(state) = std::env::var("MN_D3D_STATE") {
        let _ = std::fs::create_dir_all(format!("{}/pipelines", state));
        unsafe { setenv(cstring("DXMT_SHADER_CACHE_PATH").as_ptr(), cstring(&format!("{}/", state)).as_ptr(), 0) };
        if std::env::var("MACNEUTRON_PRECACHE").as_deref() != Ok("0") {
            unsafe {
                setenv(
                    cstring("DXMT_PIPELINE_RECORD").as_ptr(),
                    cstring(&format!("{}/pipelines", state)).as_ptr(),
                    0,
                )
            };
        }
    }
    if std::env::var("MACNEUTRON_VM_D3D_TRACE").as_deref() == Ok("1") {
        unsafe { mn_d3d_set_trace(cstring(&format!("{}/traces/d3d-host.txt", share)).as_ptr()) };
    }

    let vm_hooks: *const MnExecutorHooks = ptr::addr_of!(mn_vm_hooks);
    unsafe {
        if wm_loaded && metal_test_ops {
            mn_executor_start(Some(mn_vm_dispatch_test), vm_hooks);
        } else if wm_loaded {
            mn_executor_start(Some(mn_vm_dispatch), vm_hooks);
        } else if metal_test_ops {
            mn_executor_start(Some(mn_testops), ptr::null());
        } else {
            mn_executor_start(None, ptr::null());
        }
    }
    // Drained rings: a guest flush, a store to its process's trap blob, runs the ring on that vCPU.
    unsafe { krun_set_trap_handler(Some(mn_executor_trap), ptr::null_mut()) };

    let mut e: *mut KrunError = ptr::null_mut();
    let gpu = made(unsafe { krun_gpu_device_without_display(8usize << 30) }, &mut e, "gpu");
    unsafe { krun_mmio_device_manager_add(devices, gpu as *mut KrunAttachDevice) };

    // The keyboard and mouse: virtio-input, fed by the game window, replayed by the guest's agent.
    let mut e: *mut KrunError = ptr::null_mut();
    let input = made(unsafe { mn_input_device(cstring(&share).as_ptr(), &mut e) }, &mut e, "input");
    unsafe { krun_mmio_device_manager_add(devices, input as *mut KrunAttachDevice) };

    if timeout > 0 {
        // A guest that never powers off, or a keychain that waits on a dialog, must not hold the host and state.img's
        // lock forever.
        let console = console.clone();
        std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_secs(timeout as u64));
            eprintln!(
                "timeout: the guest still runs after {} s, see {}",
                timeout, console
            );
            unsafe { _exit(4) }
        });
    }

    if let Some(dir) = opts.get("--tpm").cloned() {
        if unsafe { mn_tpm_start(cstring(&dir).as_ptr()) } != 0 {
            fail(7, &format!("the TPM in {} did not start", dir));
        }
        let mut e: *mut KrunError = ptr::null_mut();
        let tpm = made(unsafe { mn_tpm_device(&mut e) }, &mut e, "TPM");
        unsafe { krun_mmio_device_manager_add(devices, tpm as *mut KrunAttachDevice) };
    }

    // The guest reaches the host over vsock: its sound (MN_AUDIO_PORT) and, for a game, Steam (5101).
    let mut e: *mut KrunError = ptr::null_mut();
    let vsock = made(unsafe { krun_vsock_device_new(3, 0, &mut e) }, &mut e, "vsock device");
    let audio_sock = format!("{}", unsafe {
        CStr::from_ptr(NSTemporaryDirectory()).to_string_lossy()
    }) + &format!("mn-audio-{}.sock", unsafe { getpid() });
    if unsafe { mn_audio_listen(cstring(&audio_sock).as_ptr()) } == 0 {
        unsafe { krun_vsock_device_add_unix_port(vsock, MN_AUDIO_PORT, kstr(&audio_sock), false) };
    } else {
        let msg = unsafe { CStr::from_ptr(strerror(errno())) };
        eprintln!("macneutron-vm: no sound ({})", msg.to_string_lossy());
    }

    // Steam forwarding: mn-steam-host serves the guest's lsteamclient calls against the Mac's Steam.
    let steam_host_pid = AtomicI32::new(0);
    if let Some(appid) = steam_appid {
        let sock = format!("{}", unsafe {
            CStr::from_ptr(NSTemporaryDirectory()).to_string_lossy()
        }) + &format!("mn-steam-{}.sock", unsafe { getpid() });
        unsafe { unlink(cstring(&sock).as_ptr()) };
        let host_bin = format!(
            "{}/steam-forward/host/mn-steam-host.app/Contents/MacOS/mn-steam-host",
            exe_dir
        );
        let child = Command::new(&host_bin)
            .args([
                "--socket",
                &sock,
                "--appid",
                &appid,
                "--map",
                &steam_map,
                "--parent",
                &unsafe { getpid() }.to_string(),
            ])
            .spawn();
        match child {
            Ok(c) => steam_host_pid.store(c.id() as i32, Ordering::SeqCst),
            Err(e) => fail(5, &format!("mn-steam-host: {}", e)),
        }
        let pid = steam_host_pid.load(Ordering::SeqCst);
        // atexit equivalent: kill the Steam host when this process ends (in case _exit paths are taken).
        register_atexit_kill(pid);
        let mut waited = 0;
        while unsafe { access(cstring(&sock).as_ptr(), F_OK) } != 0 {
            if waited >= 50 || !is_running(pid) {
                fail(5, "mn-steam-host did not start");
            }
            unsafe { usleep(100_000) };
            waited += 1;
        }
        unsafe { krun_vsock_device_add_unix_port(vsock, STEAM_VSOCK_PORT, kstr(&sock), false) };
    }
    let mut e: *mut KrunError = ptr::null_mut();
    let vsock2 = made(vsock as *mut ffi::KrunAttachDevice, &mut e, "vsock");
    unsafe { krun_mmio_device_manager_add(devices, vsock2) };

    // Network: gvproxy answers the guest's frames with the Mac's own sockets, over a unixgram socket in vfkit's framing.
    if unsafe { access(cstring(&format!("{}/gvproxy", exe_dir)).as_ptr(), X_OK) } == 0 {
        let sock = format!("{}", unsafe {
            CStr::from_ptr(NSTemporaryDirectory()).to_string_lossy()
        }) + &format!("mn-net-{}.sock", unsafe { getpid() });
        unsafe { unlink(cstring(&sock).as_ptr()) };
        let script = "\"$0\" -listen-vfkit \"unixgram://$1\" -ssh-port -1 -log-file \"$1.log\" & g=$!\n\
             while kill -0 \"$2\" 2>/dev/null && kill -0 $g 2>/dev/null; do sleep 1; done\n\
             kill $g 2>/dev/null; rm -f \"$1\" \"$1.log\"\n";
        let child = Command::new("/bin/sh")
            .args(["-c", script, &format!("{}/gvproxy", exe_dir), &sock, &unsafe { getpid() }.to_string()])
            .spawn();
        let pid = match child {
            Ok(c) => c.id() as i32,
            Err(e) => fail(6, &format!("gvproxy: {}", e)),
        };
        let mut waited = 0;
        while unsafe { access(cstring(&sock).as_ptr(), F_OK) } != 0 {
            if waited >= 50 || !is_running(pid) {
                fail(6, &format!("gvproxy did not start, see {}.log", sock));
            }
            unsafe { usleep(100_000) };
            waited += 1;
        }
        let mac: [u8; 6] = [0x5a, 0x94, 0xef, 0xe4, 0x0c, 0xee];
        let mut e: *mut KrunError = ptr::null_mut();
        let net = unsafe {
            krun_net_device_new_unixgram_path(
                kstr("net0"),
                kstr(&sock),
                KrunBytes {
                    data: mac.as_ptr(),
                    len: 6,
                },
                0,
                KRUN_NET_FLAGS_VFKIT,
                &mut e,
            )
        };
        let net = made(net, &mut e, "network");
        unsafe { krun_mmio_device_manager_add(devices, net as *mut KrunAttachDevice) };
    }

    // Fedora's vmlinuz is an EFI zboot image; our libkrun's PE loader reads its header.
    let mut e: *mut KrunError = ptr::null_mut();
    let payload = made(
        unsafe {
            krun_payload_load_external(
                kstr(&path("vmlinuz")),
                KRUN_KERNEL_FORMAT_PE_GZ,
                kstr(&path("initramfs.img")),
                kstr(&cmdline),
                &mut e,
            )
        },
        &mut e,
        "kernel",
    );
    let builder = unsafe { krun_vmm_builder_new() };
    let mut e: *mut KrunError = ptr::null_mut();
    check(
        unsafe { krun_vmm_builder_vcpus(builder, cpus as u8, &mut e) },
        &format!("{} vCPUs", cpus),
    );
    let mut e: *mut KrunError = ptr::null_mut();
    check(
        unsafe { krun_vmm_builder_ram_mib(builder, ram_mib, &mut e) },
        &format!("{} MiB", ram_mib),
    );
    unsafe { krun_vmm_builder_payload(builder, payload) };
    unsafe { krun_vmm_builder_devices(builder, devices as *mut KrunDeviceManager) };
    eprintln!("macneutron-vm: {} vCPUs, {} MiB", cpus, ram_mib);
    let mut e: *mut KrunError = ptr::null_mut();
    let vmm = made(
        unsafe { krun_vmm_builder_build(builder, &mut e) },
        &mut e,
        "build the VM",
    );

    // The VM runs off the main thread; the main thread runs AppKit (the game window and Metal share its run loop).
    let vmm_addr = vmm as usize;
    std::thread::spawn(move || {
        unsafe { krun_vmm_run(vmm_addr as *mut KrunVmm) };
        // exit, not _exit: atexit runs, which is what flushes an instrumented PGO profile (a training run's whole point)
        // and what the Swift host's exit(0) did.
        unsafe { exit(0) }
    });

    // The activity (AppKit shim) lasts as long as the VM, and the run loop keeps the main thread alive.
    unsafe { mn_run_appkit_main_loop() };
}

/// Non-blocking liveness check for a spawned child (its pid): mirrors Swift's `Process.isRunning`.
fn is_running(pid: i32) -> bool {
    unsafe { kill(pid, 0) == 0 }
}

/// The Steam host's pid, read by the atexit handler the Swift host installs with `atexit { kill(pid, SIGTERM) }`. Rust
/// has no closure-capturing atexit, so the pid lives in a static the handler reads.
static STEAM_HOST_PID: AtomicI32 = AtomicI32::new(0);

extern "C" fn kill_steam_host_at_exit() {
    let pid = STEAM_HOST_PID.load(Ordering::SeqCst);
    if pid > 0 {
        unsafe { kill(pid, SIGTERM) };
    }
}

/// atexit(kill(SIGTERM)) for the Steam host: registered once when it starts.
fn register_atexit_kill(pid: i32) {
    STEAM_HOST_PID.store(pid, Ordering::SeqCst);
    unsafe { atexit(kill_steam_host_at_exit) };
}
