# DXMTRS C4d-2b Implementation Plan: Core Device-Lost Latch (Review-Folded)

**Date:** 2026-11-20
**Status:** Completed 2026-11-20 at DXMTRS `df342453` (`c86e119b` + review fix `df342453`), MacNeutron pinned and verified across `rust-check.sh`, `jt1`, `jl1`, `jq1`, `jx1` (420/420 D3D10-12), `jc1` (VM 48/48 + D3D9 old/new WoW64 x64/x86), and `jr1`.
**Scope:** Wire Metal command-buffer failure reporting from `dxmt-core` (`Queue`) through `d3d9-rt` (`CoreDevice`) into `d3d9` (`DeviceInner`) so core D3D9 devices latch `D3DERR_DEVICELOST` permanently across `TestCooperativeLevel`, `CheckDeviceState`, `Present`, `PresentEx`, `SwapChain::Present`, `Reset`, `ResetEx`, `Query::Issue`/`GetData`, draws (including fans and UP draws), synchronous GPU readbacks, `ProcessVertices`, and staged VB/IB unlocks, while keeping `Clear`/`StretchRect`/`ColorFill`/`UpdateSurface`/`RESZ` return-code parity (`D3D_OK`) without growing uncommitted core chunks, and matching `debug.failNextSubmit` end-to-end across Mac and VM.

**Leaves `src/d3d9/`?** Yes (`src/dxmt/src/queue.rs`, `src/dxmt/src/fakes.rs`, `src/dxmt/src/queue/tests.rs`, `src/dxmt/src/fence/tests.rs`, `src/dxmt/src/presenter/tests.rs`, `src/dxmt/fakes/src/lib.rs`, `src/dxmt/fakes/tests/capture.rs`, `src/d3d11/src/device/tests.rs`). Requires bounded idle Mac x64 D3D10-12 check (`jx1`, never between `02:55` and `03:36`) and VM D3D10-12 gate (`JC_D3D10=1`).

**Zero winemetal changes:** `MTLCommandBuffer_status`, `MTLCommandBuffer_error` (slot 89, `+0` AR on macOS and VM), `NSObject_description`, `NSString_lengthOfBytesUsingEncoding`, `NSString_getCString`, `WMT_COMMAND_BUFFER_STATUS_COMPLETED = 4`, and `WMT_COMMAND_BUFFER_STATUS_ERROR = 5` are already exported by `winemetal` and declared in `winemetal-sys`.

---

## Task 1: `dxmt-core` `Queue` Command-Buffer Error Latch & Parallel-Safe Test Fakes

### Files
- `src/dxmt/src/queue.rs`
- `src/dxmt/src/fakes.rs`
- `src/dxmt/src/queue/tests.rs`
- `src/dxmt/src/fence/tests.rs`
- `src/dxmt/src/presenter/tests.rs`
- `src/dxmt/fakes/src/lib.rs`
- `src/dxmt/fakes/tests/capture.rs`
- `src/d3d11/src/device/tests.rs`

### Design
1. **`src/dxmt/src/queue.rs`**:
   - Import `MTLCommandBuffer_error` and `wire::{WMT_COMMAND_BUFFER_STATUS_COMPLETED, WMT_COMMAND_BUFFER_STATUS_ERROR}` from `winemetal_sys`.
   - Add `has_error: AtomicBool` (initialized to `AtomicBool::new(false)`) to `Shared`.
   - Expose on `Queue` (all doc lines <= 100 columns, `title / blank /// / body`):
     ```rust
     /// Whether any finished Metal command buffer on this queue failed.
     ///
     /// Latches true permanently once the finish thread observes a nil or non-completed
     /// command buffer; chunks continue retiring normally so callers never hang.
     #[must_use]
     pub fn has_error(&self) -> bool {
         self.shared.has_error.load(Ordering::Acquire)
     }
     ```
   - In `finish(shared: &Shared)` (removing `// ponytail: no device-error report yet`):
     ```rust
     if let Some(cmdbuf) = &chunk.cmdbuf {
         // SAFETY: a live command buffer the chunk holds.
         let mut status = unsafe { MTLCommandBuffer_status(cmdbuf.handle()) };
         if status <= WMT_COMMAND_BUFFER_STATUS_SCHEDULED {
             // SAFETY: a live command buffer the chunk holds.
             unsafe { MTLCommandBuffer_waitUntilCompleted(cmdbuf.handle()) };
             // SAFETY: the same live command buffer after completion.
             status = unsafe { MTLCommandBuffer_status(cmdbuf.handle()) };
         }
         if status != WMT_COMMAND_BUFFER_STATUS_COMPLETED
             && !shared.has_error.swap(true, Ordering::AcqRel)
         {
             if status == WMT_COMMAND_BUFFER_STATUS_ERROR {
                 log_command_buffer_error(cmdbuf.handle(), seq);
             } else {
                 crate::log::err(&format!(
                     "dxmt queue: Metal command buffer {seq} finished with unexpected status {status}"
                 ));
             }
         }
     } else if !shared.has_error.swap(true, Ordering::AcqRel) {
         crate::log::err(&format!(
             "dxmt queue: Metal command buffer {seq} allocation returned nil"
         ));
     }
     ```
     - **Anti-hang invariant**: `shared.frame_latency.signal(...)`, `chunk.reset()`, and `shared.ring.finish(seq)` ALWAYS run unconditionally afterwards so D3D10/D3D11/D3D12 and D3D9 callers waiting on `wait_cpu`, `present_boundary`, `Device::drop`, or `MTLD3D11WaitIdle` never hang.
     - Note: log text intentionally avoids substrings in MacNeutron's `unpresented` and `refused` grep patterns.
   - Helper `fn log_command_buffer_error(cmdbuf: u64, seq: u64)`:
     - Declares `let _pool = Pool::new();` FIRST so `err` drops before `_pool` drains (load-bearing on VM `ghandles.c`/`htable.c`).
     - Calls `let raw = unsafe { MTLCommandBuffer_error(cmdbuf) };` (`+0`, autoreleased/unretained).
     - Retains via `unsafe { Obj::retain(raw) }` (NEVER `Obj::adopt`).
     - Formats via `crate::library::description(&err)`; if non-empty, logs `dxmt queue: Metal command buffer {seq} completed with error: {desc}`, else logs `dxmt queue: Metal command buffer {seq} completed with error status`.
2. **Cross-Module Test Fakes (`src/dxmt/src/fakes.rs`, `fence/tests.rs`, `presenter/tests.rs`, `queue/tests.rs`)**:
   - Move `#[unsafe(export_name = "NSAutoreleasePool_alloc_init")]` from `src/dxmt/src/presenter/tests.rs` into `src/dxmt/src/fakes.rs` with a `thread_local!` hook (returning benign `0x730e` when unset), registered by `presenter::tests` alongside `log_object_calls` in `reset()` and its `syncwait` worker thread. This prevents in-process `Queue` tests from racing with `presenter::tests` golden output.
   - Move `#[unsafe(export_name = "MTLCommandQueue_commandBuffer")]` from `src/dxmt/src/fence/tests.rs` into `src/dxmt/src/fakes.rs` with a `thread_local!` hook (installed by `fence::tests::reset()` so `queue=0xe00 -> 0x3001` assertions stay unchanged) and a default path that mints globally unique cmdbuf handles via `AtomicU64` (`0x8000_0001..`) and notifies `queue::tests` of `(queue, cmdbuf)`.
   - In `src/dxmt/src/queue/tests.rs`:
     - Allocate globally unique queue handles via `AtomicU64` (`0x7100..`) in `fake_new_command_queue(device, _)` and record `queue -> device` in a `LazyLock<Mutex<FxHashMap<...>>>` (no `HashMap`, no `OnceLock`, no unlisted derives).
     - Support per-device armed failure scripts (`ScheduledThenError`, `UnexpectedStatus(u64)`, `ReturnNil`) and per-device `error_calls` counter + optional fake error handle for `MTLCommandBuffer_error`.
     - Add `MTLCommandBuffer_error` export.
   - In `src/dxmt/fakes/src/lib.rs`:
     - Add `#[unsafe(export_name = "MTLCommandBuffer_error")] const extern "C-unwind" fn fake_cmdbuf_error(_cmdbuf: u64) -> u64 { 0 }`.
     - Add `pub fn fail_next_command_buffer(device: u64)` backed by `BTreeMap` fields on `State` (`fail_next: BTreeMap<u64, u32>`, `cmdbuf_status: BTreeMap<u64, u64>`).
     - Consume `fail_next` inside **`fake_status(cmdbuf)`** (ONLY called by `Queue::finish()`, never by `ResourceInitializer::flush`!): first `fake_status` for an armed device records `cmdbuf -> SCHEDULED` and returns `SCHEDULED`; `fake_wait_until_completed(cmdbuf)` transitions `SCHEDULED -> ERROR`; second `fake_status` returns `ERROR`. All other devices/cmdbufs return `COMPLETED`.
   - In `src/d3d11/src/device/tests.rs`:
     - Add `#[unsafe(export_name = "MTLCommandBuffer_error")] const extern "C-unwind" fn fake_cmdbuf_error(_cmdbuf: u64) -> u64 { 0 }`.
3. **Unit Tests**:
   - `src/dxmt/src/queue/tests.rs`:
     1. `command_buffer_error_latches_has_error_without_stalling` (unique device `0xD6`): normal `COMPLETED` chunk keeps `has_error() == false`; armed `SCHEDULED -> ERROR` chunk with `signal_frame_latency(1)` retires cleanly (`wait_cpu` and `present_boundary(true)` do not stall), flips `has_error() == true`; second errored chunk + third `COMPLETED` chunk keep `has_error() == true`, never stall, and `error_calls(0xD6) == 1` (proving one-shot latch without touching thread-local `log::take_captured()`).
     2. `nil_and_unexpected_status_command_buffers_latch_has_error` (unique devices `0xD7`, `0xD8`): proves both `chunk.cmdbuf == None` (macOS nil alloc) and `status == NOT_ENQUEUED (0)` (VM nil handle) latch `has_error() == true` without stalling.
     3. `log_command_buffer_error_formats_nil_and_retained_error_on_calling_thread`: synchronous same-thread test calling `super::log_command_buffer_error` directly and asserting `crate::log::take_captured()` (level `4`, contains `seq`) AND verifying balanced `Obj::retain`/`NSObject_release` (`+0` ownership) via `crate::fakes::log_object_calls`.
   - `src/dxmt/fakes/tests/capture.rs`:
     - Add test with unique device `0xf5` exercising `dxmt_fakes::fail_next_command_buffer(0xf5)` on `Queue` and asserting `queue.has_error()`.

---

## Task 2: `d3d9-rt` `CoreDevice::has_error`

### Files
- `src/d3d9/rt/src/device.rs`
- `src/d3d9/rt/src/device/tests.rs`

### Design
1. `src/d3d9/rt/src/device.rs`:
   - Expose `CoreDevice::has_error(&self) -> bool { self.device.queue().has_error() }`.
2. `src/d3d9/rt/src/device/tests.rs`:
   - Add `command_buffer_error_latches_core_device_has_error` using `crate::device::fixture::core_on_fakes(0x520)` wrapped in `returns_within_10s(...)`:
     - Asserts `!core.has_error()` initially and after a normal `clear` + `drain`.
     - Calls `dxmt_fakes::fail_next_command_buffer(0x520)`, records `clear` + `drain` -> asserts `core.has_error()`.
     - Records another `clear` + `present(true, 0.0)` + `drain` -> asserts `core.has_error()` stays true and returns without stalling.

---

## Task 3: `d3d9` Front-End Latch Wiring & Parity

### Files
- `src/d3d9/src/device.rs`
- `src/d3d9/src/device/extended.rs`
- `src/d3d9/src/device/core_arms.rs`
- `src/d3d9/src/device/core_draw.rs`
- `src/d3d9/src/direct3d9.rs`
- `src/d3d9/src/query.rs`
- `src/d3d9/src/surface.rs`
- `src/d3d9/src/swapchain.rs`
- `src/d3d9/src/texture.rs`
- `src/d3d9/src/vertex_buffer.rs`
- `src/d3d9/src/index_buffer.rs`
- `docs/d3d9/STATUS.md`

### Design
1. **Feature-gated core failure latch on `DeviceInner` (`src/d3d9/src/device.rs`, `direct3d9.rs`)**:
   - Gate `AtomicI32` import, `DeviceFlags::FAIL_NEXT_SUBMIT = 1 << 7` (last free bit in `u8`), `DeviceInner::core_failure: AtomicI32` (init `D3D_OK`), and private `fn record_core_failure(&self, status: i32, cause: &str) -> i32` behind `#[cfg(feature = "core-runtime")]` so plain-build `leg_clippy` and `jr1` stay 100% clean and byte-identical in imports.
   - In `create_core_device` (`direct3d9.rs`): include `DeviceFlags::FAIL_NEXT_SUBMIT` in `initial_flags` when `cfg.fail_next_submit` is true.
   - `DeviceInner::encoder_status(&self) -> Result<(), i32>`:
     - Under `#[cfg(feature = "core-runtime")]` when `self.encoder.get().is_none()`:
       - `mtld3d_core::encoder_failure::known_status(&self.core_failure)?;`
       - If `self.core.as_deref().is_some_and(d3d9_rt::device::CoreDevice::has_error)`:
         `return Err(self.record_core_failure(mtld3d_types::D3DERR_DEVICELOST, "a Metal command buffer completed with an error"));`
       - `return Ok(());`
     - Else (mtld3d): unchanged (`self.recording_status()?; self.encoder.status()`).
   - `DeviceInner::known_encoder_status(&self)` & `latched_encoder_status(&self)`:
     - Under `#[cfg(feature = "core-runtime")]` when `self.encoder.get().is_none()`:
       `return mtld3d_core::encoder_failure::known_status(&self.core_failure);`
     - Make `latched_encoder_status` / `known_encoder_status` accessible to `core_draw` / `core_arms` (module visibility `super` within `device`).
2. **Present / Reset / Cooperative Level / Auto-Resize Ordering (`device.rs`, `extended.rs`, `swapchain.rs`)**:
   - Remove early `if dev.needs_reset()` from `present_impl` (`device.rs:6488`) and `swapchain_present` (`swapchain.rs:371`).
   - In `DeviceInner::present(&mut self)`:
     - Core branch (`#[cfg(feature = "core-runtime")] if self.encoder.get().is_none()`):
       1. `if let Err(hr) = self.encoder_status() { return hr; }` (permanent `DEVICELOST` takes precedence over `DEVICENOTRESET`, matching `TestCooperativeLevel`/`CheckDeviceState`/`Reset`!).
       2. `if self.needs_reset() { return mtld3d_types::D3DERR_DEVICENOTRESET; }` (before `FAIL_NEXT_SUBMIT`, so a `NOT_RESET` device does not consume `FAIL_NEXT_SUBMIT`).
       3. If `self.flags.contains(DeviceFlags::FAIL_NEXT_SUBMIT)`: remove flag, log `"d3d9 core: debug.failNextSubmit refused the frame submission"`, return `self.record_core_failure(mtld3d_types::D3DERR_DEVICELOST, "debug.failNextSubmit refused the submission")`.
       4. Run `if let Some(core) = self.core.as_deref_mut() { core_arms::present(core, &self.present_params); }`.
       5. Return `self.encoder_status().map_or_else(|hr| hr, |()| D3D_OK)`.
     - `mtld3d` branch: check `if let Err(hr) = self.encoder_status() { self.discard_unsendable_frame(); return hr; }`, then `if self.needs_reset() { return mtld3d_types::D3DERR_DEVICENOTRESET; }`, then existing body.
   - `device_test_cooperative_level` (`device.rs:5082-5092`): remove `!core` bypass and stale comment. Plain core devices check `encoder_status()` before `needs_reset()`; extended devices still return `D3D_OK` unconditionally.
   - `check_device_state` (`device/extended.rs:235-239`): remove `is_core()` bypass and stale comment.
   - `reset_impl` (`device.rs:5554`): remove `!core` bypass (`if let Err(hr) = dev.encoder_status() { return hr; }`) and update stale comment at `5500-5502`.
   - `apply_core_auto_resize` (`device.rs:3491`): add `if self.encoder_status().is_err() { return; }` at top so `WM_SIZE` on a lost core device does not allocate or set `NOT_RESET`.
3. **Queries (`src/d3d9/src/query.rs`)**:
   - Move `if let Err(hr) = unsafe { &*inner.device_inner }.encoder_status() { return hr; }` ABOVE `issue_on_core` (`query.rs:435`) and ABOVE `get_data_on_core` (`query.rs:537`).
   - Inside `get_data_on_core` (`query.rs:182-216`): re-check `if let Err(hr) = dev.encoder_status() { return Some(hr); }` after `rt.event_done(...)` and after `rt.occlusion_poll(...)` before writing output or returning `D3D_OK`/`S_FALSE`.
   - Keep `release_on_core` untouched so open occlusion brackets still close cleanly on drop.
4. **Draws & `ProcessVertices` (`src/d3d9/src/device/core_draw.rs`, `device.rs`)**:
   - `draw_primitive` (`core_draw.rs:1148`): check `dev.latched_encoder_status()` after `if count == 0`, BEFORE `bound_streams(dev)`.
   - `sequential_fan` (`core_draw.rs:1246`): check `dev.known_encoder_status()` after `FanRewrite::sequential`, BEFORE `bound_streams(dev)`.
   - `draw_indexed_primitive` (`core_draw.rs:1197`): check `dev.latched_encoder_status()` after `if count == 0`, BEFORE `bound_streams(dev)` / `bound_index(dev)`.
   - `indexed_fan` (`core_draw.rs:1302`): check `dev.known_encoder_status()` after `FanRewrite::indexed`, BEFORE `bound_streams(dev)`.
   - `draw_primitive_up` & `draw_indexed_primitive_up`: check `dev.encoder_status()` AFTER all `INVALIDCALL` validation (including `FanRewrite`), BEFORE `draw(...)`. In `device.rs:13721` and `device.rs:14884`, reset `stream0` (and `index_buffer` for indexed UP) when `hr == D3D_OK || matches!(hr, mtld3d_types::D3DERR_DEVICELOST | mtld3d_types::E_OUTOFMEMORY)`.
   - `device_process_vertices` (`device.rs:15099-15105`): check `if let Err(hr) = dev.encoder_status() { return hr; }` before `core_write_processed`, and return `dev.encoder_status().map_or_else(|hr| hr, |()| D3D_OK)` after (updating stale comment at `15097-15098`).
5. **Synchronous GPU Readbacks & Non-Error GPU Commands (`core_arms.rs`, `device.rs`, `surface.rs`, `texture.rs`, `vertex_buffer.rs`, `index_buffer.rs`)**:
   - `core_arms::get_render_target_data`, `DeviceInner::read_front_buffer`, `core_backbuffer_read_into_lock`: check `dev.encoder_status()` BEFORE and AFTER readback (before `note_system_memory_written()` / `OutPtr::write_opt`), returning `hr`.
   - `readback_full_backbuffer`, `core_lockable_rt_read_back`, `core_read_subresource_back`: check `dev.encoder_status().is_err()` before and after readback, returning `None`/`false`.
   - `core_arms::{clear, stretch_depth, stretch_colour (GPU target path), color_fill, update_color_target, resz}` and `surface.rs::{backbuffer_page_upload, core_lockable_rt_upload}`: after argument validation, if `dev.latched_encoder_status().is_err()`, return `D3D_OK` (`Ok(())` for `resz`, early return for uploads) without emitting into `CoreDevice` (prevents unbounded post-failure `Chunk` growth while matching `mtld3d` return codes).
   - `vb_unlock` & `ib_unlock`: on core, when `matches!(inner.map_mode, BufferMapMode::Staged) && inner.dirty.span().is_some()` (and before `vb_unlock`'s immediate `core_flush`), check `if let Err(hr) = dev.encoder_status() { return hr; }`.
6. **Documentation (`docs/d3d9/STATUS.md`)**:
   - Update lines 123-133 ("Three differences" -> "Two differences", removing the GPU failure latch caveat).

---

## Task 4: MacNeutron End-to-End Tests (`lost` Mode) & Gate Counts

### Files
- `dxmt/tests/d3d9_triangle.cpp`
- `dxmt/d3d9-check.sh`
- `vm/tests/d3d9.sh`
- `vm/dxmt/d3d9-gate.sh`

### Design
1. **`dxmt/tests/d3d9_triangle.cpp`**:
   - Add `lost` mode (`"lost"` / `"lost strict"`, 2 rows: `lost-plain` and `lost-ex`), dispatched in `main()` just like `reset`/`fullscreen`, launched with `debug.failNextSubmit=true`:
     - **`lost-plain`**:
       - Creates plain windowed device via `rsDevice(r)`.
       - Asserts `TestCooperativeLevel() == D3D_OK`.
       - First `Present() == D3DERR_DEVICELOST`.
       - `TestCooperativeLevel() == D3DERR_DEVICELOST`.
       - Second `Present() == D3DERR_DEVICELOST`.
       - Inner scope: `GetSwapChain(0)` -> `SwapChain::Present() == D3DERR_DEVICELOST` (released before `Reset`).
       - `Reset(&pp) == D3DERR_DEVICELOST` (latch not cleared) and `TestCooperativeLevel() == D3DERR_DEVICELOST`.
       - `Clear(0, nullptr, D3DCLEAR_TARGET, TBLK, 1.0f, 0) == D3D_OK` (parity: Clear still returns `D3D_OK`).
       - `SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE)` + `BeginScene` + `DrawPrimitiveUP` -> `D3DERR_DEVICELOST` + `EndScene` + verifies `GetStreamSource(0)` is reset to `nullptr`.
       - `CreateQuery(D3DQUERYTYPE_EVENT)` -> `Issue(D3DISSUE_END) == D3DERR_DEVICELOST` and `GetData(...) == D3DERR_DEVICELOST`.
       - `GetFrontBufferData` into a systemmem surface -> `D3DERR_DEVICELOST`.
     - **`lost-ex`**:
       - Creates extended windowed device via `rsDevice(r, D3DFMT_UNKNOWN, true)` (uses `r.ex.p` directly).
       - Asserts `TestCooperativeLevel() == D3D_OK` and `CheckDeviceState(r.w.h) == D3D_OK`.
       - First `PresentEx(0) == D3DERR_DEVICELOST`.
       - `TestCooperativeLevel() == D3D_OK` (always `D3D_OK` on extended devices).
       - `CheckDeviceState(r.w.h) == D3DERR_DEVICELOST`.
       - Second `PresentEx(0) == D3DERR_DEVICELOST`.
       - `Present() == D3DERR_DEVICELOST`.
       - Inner scope: `GetSwapChain(0)` -> `SwapChain::Present() == D3DERR_DEVICELOST` (released before `ResetEx`).
       - `ResetEx(&pp, nullptr) == D3DERR_DEVICELOST` and `Reset(&pp) == D3DERR_DEVICELOST`.
       - `TestCooperativeLevel() == D3D_OK`.
2. **`dxmt/d3d9-check.sh`**:
   - Inside `launch()`: append `;debug.failNextSubmit=true` to `cfg` strictly when `[ "$name" = lost ]` (NO outer `MTLD3D_CONFIG=...` assignment leak and NO subshell losing `fail`).
   - Add `lost` to `refusals_ok` allowlist (`line 216`).
   - Launch `launch ours fresh lost 180 lost $strict` and `check lost "lost-plain lost-ex"`.
   - Update header comments.
3. **`vm/tests/d3d9.sh` & `vm/dxmt/d3d9-gate.sh`**:
   - In `vm/tests/d3d9.sh`: inside `tri()`, append `;debug.failNextSubmit=true` strictly when `case $name in lo*-*)`; run `lo64-fresh` and `lo32-fresh` (`d3d9_triangle.exe lost [$strict]`, 180 s).
   - In `vm/dxmt/d3d9-gate.sh`: verify `lo64-fresh` and `lo32-fresh` rows (`lost-plain`, `lost-ex`), add `lo64-fresh lo32-fresh` to core `runtime` check loop (`lines 356-360`), and update comments.

---

## Task 5: Gates & Verification (`build/c4d/s2b/gates/`)

Because `src/dxmt/src/queue.rs` leaves `src/d3d9/`:
1. `LC_ALL=C sh tools/rust-check.sh` (all 6 legs: fmt, gen, clippy across all targets/features including plain `d3d9` + `perf-tracking` + `perf-build`, doc, tests, audit).
2. `jt1`: Mac `d3d9-check` default & core (with `lrows` want 2) + Metal validation on `parity`, `msaa`, `fullscreen`, `lost` (with `;debug.failNextSubmit=true` on `lost`).
3. `jl1`: Full Wine `d3d9_test.exe` conformance default & core + `conf-compare` (0 core validation reports).
4. `jq1`: Mac `d3d9-check` default/msl/core + all **17** core modes under Metal validation (`min_ok=4` for `lost`, `10` for others, `nval == 17`, `0` reports).
5. `jx1`: Bounded idle Mac x64 D3D10-12 check (`sh dxmt/check.sh`, never between `02:55` and `03:36`).
6. `jc1` with `JC_D3D10=1`: VM D3D10-12 gate (`vm/dxmt/gate.sh`) + VM D3D9 gate default & core across old/new WoW64, x64/x86 (`lo64`/`lo32` = 2 default, 3 core).
7. `jr1`: Plain build (no `DXMT_D3D9_CORE`), translator key & `d3d9.dll` imports unchanged, `d3d9-default` (with `lost`!) & `conf-default` green.
8. Fresh final adversarial review, push DXMTRS & MacNeutron, `post.sh` (with `X64_BASE` set to `jx1`'s commit or verifying `x64`), DXMTRS CI green.
