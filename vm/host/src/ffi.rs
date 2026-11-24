// Hand-written FFI to libkrun's v2 C API (vm/libkrun/include/libkrun.h, libkrun_trap.h, libkrun_input.h,
// libkrun_tpm.h), the host's C helpers (bridge.h: executor, winemetal loader, Direct3D dispatch, input.c, audio.c,
// tpm.c), and the POSIX/System calls the Swift host used. No bindgen: the surface is small and fixed.
#![allow(non_camel_case_types, non_snake_case, dead_code)]

use core::ffi::{c_char, c_int, c_uint, c_void};

// ---- libkrun primitive types (libkrun.h) ----

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KrunStr {
    pub data: *const c_char,
    pub len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct KrunBytes {
    pub data: *const u8,
    pub len: usize,
}

pub type KrunResult = u64;
pub const KRUN_RESULT_SUCCESS: KrunResult = 0;

// Opaque handles: the header typedefs these to void*.
macro_rules! opaque {
    ($($n:ident),* $(,)?) => { $( #[repr(C)] pub struct $n { _private: [u8; 0] } )* };
}
opaque!(
    KrunMmioDeviceManager,
    KrunFsDevice,
    KrunConsoleDevice,
    KrunConsoleBuilder,
    KrunPayload,
    KrunVmmBuilder,
    KrunVmm,
    KrunBalloonDevice,
    KrunRngDevice,
    KrunVsockDevice,
    KrunBlockDevice,
    KrunNetDevice,
    KrunGpuDevice,
    KrunAttachDevice,
    KrunDeviceManager,
    KrunError,
    KrunInputDevice,
    KrunTpmDevice,
);

// KRUN_KERNEL_FORMAT_*, KRUN_DISK_FORMAT_*, KRUN_FREE_PAGE_REPORTING_MODE_*, KRUN_LOG_LEVEL_*, KRUN_LOG_STYLE_*,
// KRUN_NET_FLAGS_*.
pub const KRUN_KERNEL_FORMAT_PE_GZ: u32 = 2;
pub const KRUN_DISK_FORMAT_RAW: u32 = 0;
pub const KRUN_FREE_PAGE_REPORTING_MODE_REPLACE: u32 = 1;
pub const KRUN_LOG_LEVEL_WARN: u32 = 2;
pub const KRUN_LOG_STYLE_AUTO: u32 = 0;
pub const KRUN_NET_FLAGS_VFKIT: u32 = 1;

pub type KrunTrapFn = unsafe extern "C" fn(ctx_id: u32, value: u64, opaque: *mut c_void);

// ---- libkrun v2 functions (the ones macneutron-vm uses) ----

extern "C" {
    pub fn krun_init_log(target: c_int, level: u32, style: u32, options: u32, err_out: *mut *mut KrunError) -> KrunResult;
    pub fn krun_result_name_cstr(r: KrunResult) -> *const c_char;
    pub fn krun_error_result(handle: *mut KrunError) -> u64;

    pub fn krun_mmio_device_manager_new() -> *mut KrunMmioDeviceManager;
    pub fn krun_mmio_device_manager_add(handle: *mut KrunMmioDeviceManager, device: *mut KrunAttachDevice);

    pub fn krun_block_device_new(id: KrunStr, disk_image_path: KrunStr, format: u32, err_out: *mut *mut KrunError) -> *mut KrunBlockDevice;
    pub fn krun_block_device_set_read_only(handle: *mut KrunBlockDevice, read_only: bool);

    pub fn krun_fs_device_new(tag: KrunStr, host_path: KrunStr, err_out: *mut *mut KrunError) -> *mut KrunFsDevice;
    pub fn krun_fs_device_new_read_only(tag: KrunStr, host_path: KrunStr, err_out: *mut *mut KrunError) -> *mut KrunFsDevice;
    pub fn krun_fs_device_new_simplified(tag: KrunStr, host_path: KrunStr, err_out: *mut *mut KrunError) -> *mut KrunFsDevice;

    pub fn krun_console_device_builder() -> *mut KrunConsoleBuilder;
    pub fn krun_console_builder_add_tty_port(handle: *mut KrunConsoleBuilder, name: KrunStr, tty_fd: c_int, result: *mut u32, err_out: *mut *mut KrunError) -> KrunResult;
    pub fn krun_console_builder_add_inout_port(handle: *mut KrunConsoleBuilder, name: KrunStr, input_fd: c_int, output_fd: c_int, result: *mut u32, err_out: *mut *mut KrunError) -> KrunResult;
    pub fn krun_console_builder_build(handle: *mut KrunConsoleBuilder, err_out: *mut *mut KrunError) -> *mut KrunConsoleDevice;

    pub fn krun_rng_device_new(err_out: *mut *mut KrunError) -> *mut KrunRngDevice;
    pub fn krun_balloon_device_new(err_out: *mut *mut KrunError) -> *mut KrunBalloonDevice;
    pub fn krun_balloon_device_set_free_page_reporting_mode(handle: *mut KrunBalloonDevice, mode: u32);

    pub fn krun_gpu_device_without_display(shm_size: usize) -> *mut KrunGpuDevice;

    pub fn krun_vsock_device_new(cid: u64, tsi_features: u32, err_out: *mut *mut KrunError) -> *mut KrunVsockDevice;
    pub fn krun_vsock_device_add_unix_port(handle: *mut KrunVsockDevice, port: u32, path: KrunStr, listen: bool);

    pub fn krun_net_device_new_unixgram_path(id: KrunStr, path: KrunStr, mac: KrunBytes, features: u32, flags: u32, err_out: *mut *mut KrunError) -> *mut KrunNetDevice;

    pub fn krun_payload_load_external(kernel_path: KrunStr, format: u32, initrd_path: KrunStr, cmdline: KrunStr, err_out: *mut *mut KrunError) -> *mut KrunPayload;

    pub fn krun_vmm_builder_new() -> *mut KrunVmmBuilder;
    pub fn krun_vmm_builder_vcpus(handle: *mut *mut KrunVmmBuilder, count: u8, err_out: *mut *mut KrunError) -> KrunResult;
    pub fn krun_vmm_builder_ram_mib(handle: *mut *mut KrunVmmBuilder, mib: u32, err_out: *mut *mut KrunError) -> KrunResult;
    pub fn krun_vmm_builder_payload(handle: *mut *mut KrunVmmBuilder, payload: *mut KrunPayload);
    pub fn krun_vmm_builder_devices(handle: *mut *mut KrunVmmBuilder, devices: *mut KrunDeviceManager);
    pub fn krun_vmm_builder_build(handle: *mut *mut KrunVmmBuilder, err_out: *mut *mut KrunError) -> *mut KrunVmm;
    pub fn krun_vmm_run(handle: *mut KrunVmm);

    pub fn krun_set_trap_handler(fn_: Option<KrunTrapFn>, opaque: *mut c_void) -> i32;
}

// ---- host C helpers (bridge.h and its headers) ----

extern "C" {
    // vm/metal/host/executor.h
    pub fn mn_executor_start(handler: Option<mn_op_fn>, hooks: *const MnExecutorHooks);
    pub fn mn_executor_trap(ctx_id: u32, ring_blob: u64, opaque: *mut c_void);
    pub fn mn_testops(ctx_id: u32, ring_state: *mut c_void, op: u32, p: *const c_void, len: u32, reply: *mut c_void, cap: u32, reply_len: *mut u32) -> i32;

    // vm/metal/wm/host/loader.h, dispatch.h: extern const struct mn_executor_hooks.
    pub static mn_wm_hooks: MnExecutorHooks;
    pub fn mn_wm_load(dir: *const c_char) -> c_int;

    // vm/d3d/host/dispatch.h
    pub static mn_vm_hooks: MnExecutorHooks;
    pub fn mn_vm_dispatch(ctx_id: u32, ring_state: *mut c_void, op: u32, p: *const c_void, len: u32, reply: *mut c_void, cap: u32, reply_len: *mut u32) -> i32;
    pub fn mn_vm_dispatch_test(ctx_id: u32, ring_state: *mut c_void, op: u32, p: *const c_void, len: u32, reply: *mut c_void, cap: u32, reply_len: *mut u32) -> i32;
    pub fn mn_d3d_set_dir(dir: *const c_char);
    pub fn mn_d3d_set_trace(path: *const c_char);

    // vm/host/input.h, audio.h, gamepad.h, tpm.h
    pub fn mn_input_device(share: *const c_char, err: *mut *mut KrunError) -> *mut KrunInputDevice;
    pub fn mn_audio_listen(path: *const c_char) -> c_int;
    pub fn mn_audio_set_focus_active(active: c_int);
    pub fn mn_gamepad_init(wr_fds: *const c_int, rd_fds: *const c_int, nslots: c_int) -> c_int;
    pub fn mn_gamepad_shutdown();
    pub fn mn_gamepad_set_paused(paused: c_int);
    pub fn mn_tpm_start(dir: *const c_char) -> c_int;
    pub fn mn_tpm_device(err: *mut *mut KrunError) -> *mut KrunTpmDevice;
}

// mn_op_fn: int32_t (*)(uint32_t, void*, uint32_t, const void*, uint32_t, void*, uint32_t, uint32_t*).
pub type mn_op_fn = unsafe extern "C" fn(u32, *mut c_void, u32, *const c_void, u32, *mut c_void, u32, *mut u32) -> i32;

#[repr(C)]
pub struct MnExecutorHooks {
    pub progress: Option<unsafe extern "C" fn(u32, *mut c_void)>,
    pub context_end: Option<unsafe extern "C" fn(u32)>,
    pub ring_end: Option<unsafe extern "C" fn(u32, *mut c_void)>,
}

// ---- POSIX / libSystem ----

extern "C" {
    pub fn sysctlbyname(name: *const c_char, oldp: *mut c_void, oldlenp: *mut usize, newp: *mut c_void, newlen: usize) -> c_int;
    pub fn open(path: *const c_char, oflag: c_int, ...) -> c_int;
    pub fn pread(fd: c_int, buf: *mut c_void, count: usize, offset: i64) -> isize;
    pub fn ftruncate(fd: c_int, length: i64) -> c_int;
    pub fn access(path: *const c_char, mode: c_int) -> c_int;
    pub fn unlink(path: *const c_char) -> c_int;
    pub fn openpty(amaster: *mut c_int, aslave: *mut c_int, name: *mut c_char, termp: *mut c_void, winp: *mut c_void) -> c_int;
    pub fn read(fd: c_int, buf: *mut c_void, count: usize) -> isize;
    pub fn write(fd: c_int, buf: *const c_void, count: usize) -> isize;
    pub fn close(fd: c_int) -> c_int;
    pub fn flock(fd: c_int, operation: c_int) -> c_int;
    pub fn fcntl(fd: c_int, cmd: c_int, ...) -> c_int;
    pub fn getdtablesize() -> c_int;
    pub fn getpid() -> c_int;
    pub fn usleep(usec: u32) -> c_int;
    pub fn strerror(errnum: c_int) -> *mut c_char;
    pub fn setenv(name: *const c_char, value: *const c_char, overwrite: c_int) -> c_int;
    pub fn strdup(s: *const c_char) -> *mut c_char;
    pub fn strlen(s: *const c_char) -> usize;
    pub fn _exit(code: c_int) -> !;
    pub fn exit(code: c_int) -> !;
    pub fn kill(pid: c_int, sig: c_int) -> c_int;
    pub fn atexit(fn_: extern "C" fn()) -> c_int;
    pub fn __error() -> *mut c_int;
    pub fn fstat(fd: c_int, buf: *mut stat) -> c_int;
    pub fn confstr(name: c_int, buf: *mut c_char, len: usize) -> usize;
    pub fn kqueue() -> c_int;
    pub fn kevent(
        kq: c_int,
        changelist: *const Kevent,
        nchanges: c_int,
        eventlist: *mut Kevent,
        nevents: c_int,
        timeout: *const Timespec,
    ) -> c_int;
    pub fn proc_pid_rusage(pid: c_int, flavor: c_int, buffer: *mut RusageInfoV4) -> c_int;
    pub fn waitpid(pid: c_int, status: *mut c_int, options: c_int) -> c_int;
    pub fn signal(sig: c_int, handler: Sighandler) -> usize;
    pub fn getrlimit(resource: c_int, rlp: *mut Rlimit) -> c_int;
    pub fn setrlimit(resource: c_int, rlp: *const Rlimit) -> c_int;
    pub fn pthread_set_qos_class_self_np(qos_class: c_uint, relative_priority: c_int) -> c_int;
    pub fn pipe(fds: *mut c_int) -> c_int;
    pub fn clock_gettime(clock_id: c_int, tp: *mut Timespec) -> c_int;
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Rlimit {
    pub rlim_cur: u64,
    pub rlim_max: u64,
}

pub type Sighandler = extern "C" fn(c_int);

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Kevent {
    pub ident: usize,
    pub filter: i16,
    pub flags: u16,
    pub fflags: u32,
    pub data: isize,
    pub udata: *mut c_void,
}

#[repr(C)]
pub struct RusageInfoV4 {
    pub ri_uuid: [u8; 16],
    pub ri_user_time: u64,
    pub ri_system_time: u64,
    pub ri_pkg_idle_wkups: u64,
    pub ri_interrupt_wkups: u64,
    pub ri_pageins: u64,
    pub ri_wired_size: u64,
    pub ri_resident_size: u64,
    pub ri_phys_footprint: u64,
    pub ri_proc_start_abstime: u64,
    pub ri_proc_exit_abstime: u64,
    pub ri_child_user_time: u64,
    pub ri_child_system_time: u64,
    pub ri_child_pkg_idle_wkups: u64,
    pub ri_child_interrupt_wkups: u64,
    pub ri_child_pageins: u64,
    pub ri_child_elapsed_abstime: u64,
    pub ri_diskio_bytesread: u64,
    pub ri_diskio_byteswritten: u64,
    pub ri_cpu_time_qos_default: u64,
    pub ri_cpu_time_qos_maintenance: u64,
    pub ri_cpu_time_qos_background: u64,
    pub ri_cpu_time_qos_utility: u64,
    pub ri_cpu_time_qos_legacy: u64,
    pub ri_cpu_time_qos_user_initiated: u64,
    pub ri_cpu_time_qos_user_interactive: u64,
    pub ri_billed_system_time: u64,
    pub ri_serviced_system_time: u64,
    pub ri_logical_writes: u64,
    pub ri_lifetime_max_phys_footprint: u64,
    pub ri_instructions: u64,
    pub ri_cycles: u64,
    pub ri_billed_energy: u64,
    pub ri_serviced_energy: u64,
    pub ri_interval_max_phys_footprint: u64,
    pub ri_runnable_time: u64,
}

// struct stat: only st_size is read (the ext4-grow check). st_size is off_t at the offset the macOS ABI puts it, so
// the layout matches Apple's. The leading fields vary by struct padding, so this mirrors <sys/stat.h>.
#[repr(C)]
pub struct stat {
    pub st_dev: i32,
    pub st_mode: u16,
    pub st_nlink: u16,
    pub st_ino: u64,
    pub st_uid: u32,
    pub st_gid: u32,
    pub st_rdev: i32,
    pub st_atimespec: Timespec,
    pub st_mtimespec: Timespec,
    pub st_ctimespec: Timespec,
    pub st_birthtimespec: Timespec,
    pub st_size: i64,
    pub st_blocks: i64,
    pub st_blksize: i32,
    pub st_flags: u32,
    pub st_gen: u32,
    pub st_lspare: i32,
    pub st_qspare0: i64,
    pub st_qspare1: i64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Timespec {
    pub tv_sec: i64,
    pub tv_nsec: i64,
}

pub const O_RDWR: c_int = 0x0002;
pub const O_WRONLY: c_int = 0x0001;
pub const O_NONBLOCK: c_int = 0x0004;
pub const O_CREAT: c_int = 0x0200;
pub const O_TRUNC: c_int = 0x0400;
pub const O_CLOEXEC: c_int = 0x1000000;
pub const F_SETFD: c_int = 2;
pub const F_GETFL: c_int = 3;
pub const F_SETFL: c_int = 4;
pub const F_SETNOSIGPIPE: c_int = 73;
pub const FD_CLOEXEC: c_int = 1;
pub const QOS_CLASS_USER_INTERACTIVE: c_uint = 0x21;
pub const RLIMIT_NOFILE: c_int = 8;
pub const RLIM_INFINITY: u64 = 0x7fff_ffff_ffff_ffff;
pub const CLOCK_REALTIME: c_int = 0;
pub const LOCK_EX: c_int = 0x0002;
pub const LOCK_NB: c_int = 0x0004;
pub const R_OK: c_int = 0x0004;
pub const W_OK: c_int = 0x0002;
pub const X_OK: c_int = 0x0001;
pub const F_OK: c_int = 0x0000;
pub const EINTR: c_int = 4;
pub const SIGHUP: c_int = 1;
pub const SIGINT: c_int = 2;
pub const SIGTERM: c_int = 15;
pub const EVFILT_PROC: i16 = -5;
pub const EV_ADD: u16 = 0x0001;
pub const EV_ONESHOT: u16 = 0x0010;
pub const EV_ERROR: u16 = 0x4000;
pub const NOTE_EXIT: u32 = 0x8000_0000;
pub const NOTE_EXIT_DETAIL: u32 = 0x0200_0000;
pub const NOTE_EXIT_MEMORY: u32 = 0x0002_0000;
pub const RUSAGE_INFO_V4: c_int = 4;

// ---- AppKit / Metal shim (appkit_shim.m, metal_cache.m) ----
extern "C" {
    pub fn mn_begin_activity();
    pub fn mn_run_appkit_main_loop();
    pub fn mn_register_wake_callback(cb: extern "C" fn(*mut c_void), ctx: *mut c_void);
    pub fn mn_register_focus_callback(cb: extern "C" fn(c_int, *mut c_void), ctx: *mut c_void);
    pub fn mn_prepare_metal_shader_cache();
    pub fn mn_gpu_is_paravirtual() -> bool;
    pub fn mn_metal_memory_info(
        name_buf: *mut c_char,
        name_len: usize,
        allocated_out: *mut u64,
        recommended_out: *mut u64,
    ) -> bool;
    pub fn mn_task_memory_info(
        footprint_out: *mut u64,
        rss_out: *mut u64,
        compressed_out: *mut u64,
    ) -> bool;
}
