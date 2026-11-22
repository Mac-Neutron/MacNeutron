# DXMTRS C4d-5 Implementation Plan: 7b DeviceInfo, Direct PE Logger/Crash, Unified Config, `DXMT_STATS` & Metal HUD

**Date:** 2026-11-21
**Status:** Finalized (Option A: strictly inside `src/d3d9/` and `docs/`)
**Scope:** Eliminate all remaining `mtld3d.so` calls under `runtime.mode = core` (startup, runtime, and exit), move PE logging and VEH crash reporting directly onto `d3d9.dll` in `core` mode, verify unified config precedence and key acceptance, and wire `DXMT_STATS` (`stats.txt`) plus Metal `Hud` into `d3d9-rt`.

## Architectural Boundary (Option A, Strictly Inside `src/d3d9/`)

- **Zero edits outside `src/d3d9/` and `docs/`** (no `src/dxmt/`, no `src/winemetal/`, no `tools/derive_inventory.txt`, no `tools/rust-check.sh`):
  - Define only the 7 missing symbols (`MTLDevice_name`, `MTLDevice_supportsTextureSampleCount`, `DeveloperHUDProperties_{instance,addLabel,updateLabel,remove}`, `NSString_alloc_init`) as `#[unsafe(no_mangle)]` stubs in `src/d3d9/rt/src/device/fixture.rs` (matching the existing `MTLDevice_newLibraryWithSourceOptions` precedent), keeping `src/dxmt/fakes/src/lib.rs` untouched.
  - Add no new `#[derive(Clone)]` or `#[derive(Copy)]` attributes so `tools/derive_inventory.txt` stays untouched.
  - Keep plain builds (`DXMT_D3D9_CORE=0`, `jr.sh`) byte-for-byte invariant: gate all PE logger, `DllMain` deferral, `shim` lazy-init, and `7b` changes behind `#[cfg(feature = "core-runtime")]` so `base-imports.txt`, `startup_log`, and pre-`Direct3DCreate9` `exception_handler` in `d3d9-default` remain unchanged.
  - Observe all `tools/audit.sh` bans: no `static ...: OnceLock`, no inline `mod tests {`, no `mod.rs`, no zero-arg `.join()` in PE crates, no `pub(crate)`, no `HashMap`/`HashSet`, no `thread_local!` with `Drop` (`__tlregdtor`), and doc comments wrapped within 100 columns.
- **Deferred to C4d-6 (zero winemetal slot changes in C4d-5):**
  - `SRStateReporter` (`MTL_HUD_STATE_REPORTER_DOMAINS` / `mtld3d` HUD row)
  - `declare_latency_critical_activity()` (`NSActivityUserInteractive`)
  - `debug.mainThreadChecker` under `core`

---

## Task 1: 7b `GetDeviceInfo` Off `mtld3d.so` (`src/d3d9/`)

1. **`src/d3d9/core/src/gpu_caps.rs`**:
   - Re-export `pub use mtld3d_shared::mtl::DeviceCapsFlags;` so `d3d9-rt` accesses `DeviceCapsFlags` without adding a new crate dependency.
2. **`src/d3d9/rt/src/device/info.rs` (+ `info/tests.rs`)**:
   - Define `pub struct DeviceInfo { pub name: [u8; 256], pub name_len: usize, pub registry_id: u64, pub vram_bytes: u64, pub caps: DeviceCapsFlags }` with no `Clone`/`Copy` derive.
   - Add pure helper `pub fn device_caps_from(name: &str, unified_memory: bool, mac2: bool, apple2: bool, apple7: bool, msaa: [bool; 3]) -> DeviceCapsFlags`: sets `SAMPLER_BORDER` (`mac2 && !paravirtual`), `NATIVE_PACKED16` (`apple2`), `FLOAT32_FILTERING` (`apple7 || mac2`, documented parity with `mtld3d-mtl`), `SAMPLE_COUNT_2`/`4`/`8`, `RESOLVE_NEEDS_RETIRE` (`paravirtual`), and never sets `PAGES_FROM_UNIX`.
   - Implement `pub fn query_default_device_info() -> Option<DeviceInfo>` inside `winemetal_sys::Pool::new()`:
     - Adopt `WMTCopyAllDevices()`, guard non-zero and `NSArray_count > 0`, and fetch `NSArray_object(0)` without adopting (`+0` borrowed).
     - Query `MTLDevice_name(dev)` (`+0`, do not adopt), copy UTF-8 with `WMTUTF8_STRING_ENCODING`, and fall back via `NSString_lengthOfBytesUsingEncoding` + `Vec<u8>` truncated to 255 bytes + NUL when `>= 256` bytes.
     - Query `MTLDevice_registryID`, `MTLDevice_recommendedMaxWorkingSetSize`, `MTLDevice_hasUnifiedMemory`, `MTLDevice_supportsFamily` (`APPLE2`, `APPLE7`, `MAC2`), and `MTLDevice_supportsTextureSampleCount` (`2`, `4`, `8`), then construct `DeviceInfo` via `device_caps_from`.
3. **`src/d3d9/src/direct3d9.rs` (`device_info()`)**:
   - Under `#[cfg(feature = "core-runtime")]`, when `crate::is_core()`, call `d3d9_rt::device::query_default_device_info()` (emitting `log_once_warn!` and returning empty `CachedDeviceInfo` on `None`) instead of `unix_call(&mut GetDeviceInfoParams)`.
4. **Unit tests (`src/d3d9/rt/src/device/info/tests.rs`)**:
   - Exhaustive `device_caps_from` matrix (Apple Silicon, Paravirtual, Mac2, Bronze, MSAA subsets, `PAGES_FROM_UNIX` always clear) plus `query_default_device_info()` against `fixture.rs` stubs.

---

## Task 2: Direct PE Logger, Lock-Free VEH Crash Handler, & Lazy `mtld3d.dll` Shim

1. **`src/d3d9/shim/src/lib.rs`**:
   - Under `#[cfg(feature = "core-runtime")]`, in `DllMain(DLL_PROCESS_ATTACH)`, only call `DisableThreadLibraryCalls(instance)` and store `instance` in `static INSTANCE: AtomicPtr<c_void>`.
   - Move `mtld3d_shared::init_logger_to`, `__wine_init_unix_call()`, and `log_identity(instance)` into a `std::sync::Once`-guarded helper invoked at the start of `mtld3d_unix_call()` (recording init status in an `AtomicI32`, never `static ...: OnceLock`).
   - Result: `core` mode never `dlopen`s or invokes `mtld3d.so`; `mtld3d` mode initializes on its first unixcall; `bundle.sh` and `tri64-nomarker`/`tri32-nomarker` stay green.
2. **`src/d3d9/core/src/pe_log.rs` (+ `pe_log/tests.rs`, host-tested in `mtld3d-core`)**:
   - Cross-platform `PeLog` state machine:
     - `Pending` buffers early lines up to `BACKLOG_CAP = 1 MiB` (appending one truncation notice on overflow).
     - `open(dir, stem, pid)` is idempotent on repeat calls (`Direct3DCreate9`), stores `Lazy` target `(dir, stem, pid)`, flushes non-empty `Pending` immediately (so `d3d9_dynamic.exe` emits `config: runtime.mode = core` before exit), and creates no file when `RUST_LOG=off` / `DXMT_LOG_LEVEL=none` writes zero lines.
     - On first line write in `Lazy`, creates `dir` (`create_dir_all`), calls `prune_old_logs(dir, KEEP - 1 = 9)` removing only regular `*.log` files (`symlink_metadata().is_file()` + `remove_file`, never `remove_dir_all`) sorted by mtime, then atomically creates `<stem>-<pid>.log` or `<stem>-<pid>-<n>.log` (`n = 2..=1024`) via `OpenOptions::create_new(true)`.
     - Transitions to `Stderr` fallback only on file open/write failure (never echoes to `stderr` while a log file is open), and never calls `log::*!` while holding the mutex.
   - Host unit tests in `pe_log/tests.rs` covering backlog cap/truncation, lazy creation (0 lines -> 0 files), idempotent re-open, collision suffixing, mtime pruning via `File::set_modified` (leaving non-`.log` files and subdirs untouched), and stderr fallback.
3. **`src/d3d9/src/lib.rs` & `src/d3d9/src/log_sink.rs`**:
   - Gate the PE logger path behind `#[cfg(feature = "core-runtime")]` so plain `DXMT_D3D9_CORE=0` builds are unchanged.
   - In `DllMain(DLL_PROCESS_ATTACH)` under `core-runtime`: defer `InitLoggerParams` out of `DllMain`; install the PE logger and VEH immediately so early lines buffer in `PeLog::Pending`.
   - In `create_interface()`: call `let mode = runtime_mode(&cfg);` to latch `RUNTIME` before `log_sink::open(&cfg, mode)`:
     - When `mode == RuntimeMode::Core` (strictly `RUNTIME.get()`, never `cfg.runtime_mode`, preserving native ARM64 fallback on `mtld3d.so`): open `PeLog` directly on PE, store `file.as_raw_handle() as isize` in `LOG_HANDLE: AtomicIsize` (`Release`), write synchronously unbuffered under `Mutex<PeLog>` (no background thread, preventing loss of `shader cache summary:` on `TerminateProcess`), and log `"log file: <path>"` (bound inside `info!` for `perf-build` cleanliness).
     - When `mode == RuntimeMode::Mtld3d`: send `InitLoggerParams` + `OpenLogParams`, spawn the `mtld3d-log` thread as before, and forward `Pending` backlog to `mtld3d.so`.
   - VEH `write_crash` / `write_fault`: if `RUNTIME.get() == Some(RuntimeMode::Mtld3d)`, forward via `unix_call(WriteLogParams)`. Otherwise (`Core` or pre-`Direct3DCreate9` `None`), load `LOG_HANDLE` (`Acquire`): if valid (`!= 0 && != -1`), call kernel32 `WriteFile` in `u32` chunks with `&mut written`; if invalid and `write_crash`, fall back to `WriteFile(GetStdHandle(STD_ERROR_HANDLE), ...)`; if `write_fault`, drop silently without creating any file. Zero heap allocation, zero TLS, zero mutex.
   - In `DllMain(DLL_PROCESS_DETACH)`: when `!USED`, after `veh::uninstall()`, call `log_sink::close_on_unload()` (stores `-1` to `LOG_HANDLE` and drops `File`) so probe-and-unload cycles leak zero Win32 handles.

---

## Task 3: Unified Config Audit & Verification

- Preserve exact precedence: `default < AppProfile < dxmt.conf < mtld3d.conf < DXMT_CONFIG < MTLD3D_CONFIG`.
- Verify `DXMT_CONFIG`-only (`MTLD3D_CONFIG` unset), `DXMT_CONFIG_FILE`, and foreign `dxmt.*` / `d3d11.*` keys (`d3d11.preferredMaxFrameRate=60`) pass silently with zero `unknown config key` warnings in both unit tests and gate runs.

---

## Task 4: `DXMT_STATS` + Metal `Hud` (`src/d3d9/rt/` & `src/d3d9/`)

1. **`src/d3d9/rt/src/stats.rs`**:
   - Mirror `src/d3d12/src/stats.rs`:
     - Under `#[cfg(not(feature = "perf-build"))]`: `static STATS_ONCE: Once`, `static STATS_ACTIVE: AtomicBool`; `pub fn init_once()` calls `dxmt_core::stats::init(dxmt_core::settings::settings().enabled(Flags::STATS))` once (setting `STATS_ACTIVE`), `pub fn frame()` calls `dxmt_core::stats::frame()`, and `pub fn flush_stats_on_detach()` checks `STATS_ACTIVE.swap(false, AcqRel)` before `dxmt_core::stats::exit()`.
     - Under `#[cfg(feature = "perf-build")]`: `pub const fn init_once() {}`, `pub const fn frame() {}`, `pub const fn flush_stats_on_detach() {}`.
   - Re-export `pub use stats::flush_stats_on_detach;` from `src/d3d9/rt/src/lib.rs`.
   - Call `stats::init_once()` in `CoreDevice::with_options` before `Device::with_config`, call `stats::frame()` unconditionally in `CoreDevice::present` (both visible and hidden branches), and call `d3d9_rt::flush_stats_on_detach()` next to `d3d9_rt::close_shader_cache_on_detach()` in `src/d3d9/src/lib.rs` `DLL_PROCESS_DETACH` before `TerminateProcess`.
2. **Metal `Hud` (`dxmt_core::hud::Hud`, existing winemetal slots `62..=65`)**:
   - Add `hud: Option<dxmt_core::hud::Hud>` on `CoreDevice`.
   - In `CoreDevice::attach`: lazily initialize once (`self.hud.get_or_insert_with(...)`) calling `hud.initialize("d3d9", &dxmt_core::info::version_text(9, 0x9300, env!("DXMT_VERSION")))` (no `DXMT_HUD` env var and no draw pass).
   - In `CoreDevice::present`: strictly inside the visible branch right after `self.device.queue().present_boundary(true)`, under `#[cfg(all(debug_assertions, not(feature = "perf-build")))]`, call `hud.update_statistics(&stats, self.device.queue().current_frame_seq().wrapping_sub(1))` (zero release/perf-build overhead).
3. **`src/d3d9/rt/src/device/fixture.rs`**:
   - Add the 7 `#[unsafe(no_mangle)]` stubs (`MTLDevice_name`, `MTLDevice_supportsTextureSampleCount`, `NSString_alloc_init`, `DeveloperHUDProperties_{instance,addLabel,updateLabel,remove}`).
4. **Child-process unit test (`src/d3d9/rt/src/stats/tests.rs`)**:
   - Under `#[cfg(not(feature = "perf-build"))]`, spawn a child test process with `DXMT_STATS=1` + `DXMT_DXIL_DUMP=<tmpdir>`, record frames, call `flush_stats_on_detach()`, and assert `<tmpdir>/stats.txt` contains the summary header and `counters:`.

---

## Task 5: Mac & VM Gate Assertions (`mn/` & `~/Documents/MacProton/build/c4d/s5/gates/`)

1. **`dxmt/d3d9-check.sh` & `vm/dxmt/d3d9-gate.sh`**:
   - For `RT = core` non-`arm64` launches, assert:
     - Zero lines matching `mtld3d::(unix|shim)|\] mtld3d\.so |\] mtld3d\.dll ` in the core launch logs (proving `mtld3d.so` was never loaded or called in `core` mode).
     - A PE `.log` file exists and contains `] d3d9.dll ` and (for device-creating launches) `d3d9 core: device on dxmt-core` (with `d3d9_dynamic.exe` checking `config: runtime.mode = core` and `] d3d9.dll `, and `stale` continuing to verify `mtld3d.dll` marker restoration plus core PE logging).
   - Keep `tri64-nomarker`/`tri32-nomarker` and Mac `arm64` fallback untouched and green.
2. **Gate staging (`~/Documents/MacProton/build/c4d/s5/gates/`)**:
   - Stage `head.sh`, `jt.sh`, `jl.sh`, `jq.sh`, `jc.sh` (`JC_D3D10=0` default, `SCALE_ROWS:-7`), `jr.sh`, `post.sh`, `base-key.txt`, and `base-imports.txt`.
   - In `jt.sh` and `jq.sh`, run the three C4d-5 probes under `core` mode:
     - Scratch zero-`mtld3d.so` smoke check: clone `ours`, delete `mtld3d.so`, run `d3d9_triangle.exe` under `runtime.mode=core`, assert `clear`, `triangle`, `present` all `ok`, PE log exists, and zero `mtld3d.so` lines.
     - `DXMT_CONFIG`-only probe: unset `MTLD3D_CONFIG`, set `DXMT_CONFIG="runtime.mode=core;log.dir=<dir>;d3d11.preferredMaxFrameRate=60"`, run `d3d9_triangle.exe`, assert `ok`, PE log + `d3d9 core: device on dxmt-core`, and 0 `unknown config key` warnings.
     - `DXMT_STATS=1` + `DXMT_DXIL_DUMP=<dir>` + `MTL_HUD_ENABLED=1` under `MTL_DEBUG_LAYER=1`: assert 0 Metal API Validation reports and `<dir>/stats.txt` matches `^# [0-9:]+ [0-9.]+ s, [1-9][0-9]* frames \(` and `^counters:`.
3. **Required gates (Option A, strictly inside `src/d3d9/`)**:
   - `dxmt/run-rust-check.sh` (including `core-runtime,perf-build` on `arm64ec`/`i686` and host `cargo test -p d3d9-rt --features perf-build`), `jt`, `jl`, `jq`, `jc` (VM D3D9 old/new WoW64 x64/x86), `jr` (plain build, `base-imports.txt` and `base-key.txt` invariant), fresh final adversarial code review, push, `post.sh`, CI green.
