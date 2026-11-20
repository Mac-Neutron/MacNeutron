# DXMTRS C4d-2a: C4c Deferrals Inside `src/d3d9/` and Slice 6e

Second implementation slice of C4d. Purely inside `src/d3d9/` (plus MacNeutron `dxmt/tests/d3d9_triangle.cpp` rows and gate row counts): zero changes to `src/dxmt/`, `src/winemetal/`, `tools/derive_inventory.txt`, D3D10-12, or the translator key.

Inputs:
- Maintainer authorization (2026-11-20): fold Slice 6e into C4d, include Fix B (`fetch4.md`) and MSAA back buffer writable `LockRect`/`GetDC` (and device-lost latch, which is C4d-2b since it touches `src/dxmt/src/queue.rs`).
- `build/c4d/map-2-deferrals-6e.md`
- `build/c4c/s7d2/fetch4.md` (Fix B and `tex-unwritten`)
- `docs/superpowers/plans/2026-11-19-dxmtrs-c4c6d-fullscreen-subclass.md` (`R6d-3`, `ws-size`)
- Adversarial reviews folded: `build/c4d/s2a/review-1-design.md`, `review-2-core.md`, `review-3-tests.md`
- Ledger: `.superpowers/sdd/2026-11-20-dxmtrs-c4d/progress.md`

## Goal

Close three self-contained D3D9 core deferrals/divergences inside `src/d3d9/`:
1. **Fix B (`fetch4.md`)**: Zero texture and system-memory surface staging at creation on both runtimes (`mtld3d` and `core`), matching Wine's zeroed-allocation semantics for never-written texels.
2. **Multisampled back buffer writable `LockRect` and `GetDC`**: Remove the remaining two `refuse_under_core!` gates on lockable multisampled back buffers by writing through a transient 1x scratch texture and `CoreDevice::stretch`, and fix `mtld3d`'s 100%-scale MSAA upload bug in `surface.rs`.
3. **Slice 6e (windowed core back buffer resize)**: Make a windowed core device's back buffer, automatic depth-stencil, viewport, and scissor follow `WM_SIZE` as `mtld3d` does, with faithful error handling on allocation failure.

## Architecture (All Review Findings Folded)

### Part 1: Texture Staging Zero-Fill at Creation (`fetch4.md` Fix B)
Files:
- `src/d3d9/core/src/page_box.rs`
- `src/d3d9/core/src/page_box_pool.rs`
- `src/d3d9/core/src/page_box_pool/tests.rs`
- `src/d3d9/src/device.rs`
- `src/d3d9/src/texture.rs`
- `tests/d3d9/conformance/CONFORMANCE.md`

Details:
- Add `PageBox::zero_fill(&mut self)` in `src/d3d9/core/src/page_box.rs` using `unsafe { core::ptr::write_bytes(self.ptr.as_ptr(), 0, self.len) }` (avoids Rust validity UB from forming `&mut [u8]` over uninitialized tail/recycled bytes from `try_new_uninit` parked in the staging lane).
- Add `StagingTake::try_take_zeroed(&mut self, logical_len: usize) -> Option<PageBox>` next to `StagingTake::try_take` in `src/d3d9/core/src/page_box_pool.rs`:
  - Pool hit: call `pb.zero_fill()` before returning.
  - Pool miss (or pool disabled): allocate via `PageBox::try_new_zeroed(logical_len)`.
- Leave `StagingTake::try_take` unchanged so rename/overwrite callers (`take_staging_for` / `FreshBox`) pay no extra zeroing.
- Switch all four texture-creation `try_take` sites in `src/d3d9/src/device.rs` (`:7027` planar YUV, `:7041` 2D / DEFAULT offscreen plain, `:7547` volume, `:7769` cube) to `try_take_zeroed`.
- Also switch `CreateOffscreenPlainSurface` `SYSTEMMEM`/`SCRATCH` (`src/d3d9/src/device.rs:10883`) from `PageBox::try_new_uninit(bytes)` to `PageBox::try_new_zeroed(bytes)` (R1 I1 / R2 M4).
- In `src/d3d9/src/texture.rs` (`:921` `ensure_staging_for_lock`, `:970` `ensure_staging_for_write`): update `ensure_zeroed_staging` to use `try_take_zeroed`, and when `mtld3d` re-materializes dropped static `D3DPOOL_DEFAULT` 2D staging for a level with `!was_uploaded[level]` on a non-`DISCARD` lock/partial write, route through `ensure_zeroed_staging` so unwritten texels stay zeroed (R1 I2).
- Keep `PAGEBOX_POOL.drain_staging()` in `device_release` (`src/d3d9/src/device.rs`, matching `EncoderThread::shutdown` for 32-bit VM memory parity), updating its doc comment to state the memory-retention rationale.
- Update obsolete "uninitialized initial staging" comments in `src/d3d9/src/device.rs:7004-7007`, `src/d3d9/core/src/page_box_pool.rs:173,321`, and `tests/d3d9/conformance/CONFORMANCE.md:1622-1628` (keeping `ceiling` pins for un-recorded legs). Never place banned strings (`wined3d`, `visual.c`, `Wine ... test_fetch4`, SHAs, ISO dates) in `*.rs` or `docs/d3d9/*.md` (`tools/audit.sh:600-645`).
- Add host unit tests in the existing external `src/d3d9/core/src/page_box_pool/tests.rs` (NEVER inline `mod tests {` per `tools/audit.sh:196`) verifying `try_take_zeroed` returns zeroed bytes on fresh allocation, dirty recycled pool hit, recycled `try_new_uninit` box, and zero-length requests.

### Part 2: Multisampled Back Buffer Writable `LockRect` and `GetDC`
Files:
- `src/d3d9/rt/src/device/textures.rs`
- `src/d3d9/rt/src/device/swapchain/tests.rs`
- `src/d3d9/src/surface.rs`
- `docs/d3d9/STATUS.md`

Details:
- `CoreDevice::read_presented` already resolves MSAA `companion` -> `presented` and reads back, so readback for writable `LockRect` and `GetDC` already works.
- Extend `CoreDevice::write_presented(&mut self, rect: DirtyRect, bytes: &[u8], pitch: usize) -> bool` (`src/d3d9/rt/src/device/textures.rs:602-633`):
  1. Keep `rect_bytes(shape, rect, bytes.len(), pitch)` validation **before** branching on `shape.samples > 1` so every refused call emits zero Metal commands and never overflows `pitch` (R2 C3 / R1 I3).
  2. When `shape.samples > 1`:
     - Pre-check `self.quad_pipeline(shape.pixel_format, shape.samples)` and `self.quad_sampler(false)` **before** creating or uploading the scratch texture so a refusal leaves no dangling blit/staging/open blit pass (R2 C3).
     - Allocate a transient 1-level, 1-sample `WMT_PIXEL_FORMAT_BGRA8_UNORM` 2D scratch texture (`TextureId::new_unique()`, `rect.w x rect.h`, `CoreTextureFlags::empty()` so no pointless `init_texture_zero` runs) via `self.texture_create(...)`.
     - Verify `self.stored_texture(scratch)` has a non-zero Metal handle (`current().is_some_and(|a| a.texture() != 0)`); if not, call `self.forget_texture(scratch)` and return `false`.
     - Build `Blit` directly for `(0, 0, rect.w, rect.h)` (`row_bytes = rect.w * 4`, `rows = rect.h`, `images = 1`) and call `self.upload(scratch_tex, 0, &copy, bytes, (pitch, 0))` directly (no `usize -> u32` narrowing; matches 1x path).
     - Invoke `self.stretch(&StretchQuad { src: TargetRef::Texture { id: scratch, level: 0, face: None }, src_rect: [0, 0, rect.w, rect.h], src_view: shape.pixel_format, dst: TargetRef::BackBuffer, dst_rect: [rect.x, rect.y, rect.w, rect.h], linear: false, decode: BlitDecode::None.uniform() })`.
     - Unconditionally call `self.forget_texture(scratch)` before returning.
     - Note: `self.stretch` binds MSAA `companion` with `WMT_LOAD_ACTION_LOAD` / `WMT_STORE_ACTION_STORE`, preserves untouched pixels on partial rects, disables visibility results for open occlusion brackets, closes the upload blit pass and resets `PassBound`, and calls `self.note_written(TargetRef::BackBuffer, shape.pixel_format)`.
- Remove both `refuse_under_core!` gates in `src/d3d9/src/surface.rs` (`backbuffer_lock_readback` and `surface_get_dc`).
- **Fix `mtld3d` 100%-scale MSAA backbuffer upload bug** (`src/d3d9/src/surface.rs:3248`, R1 C1 / R2 C2 / R3 C1): change `if scale.is_identity()` to `if scale.is_identity() && inner.live_multi_sample().sample_count <= 1` so MSAA back buffers at 100% scale route through `UploadResampledOp` and update the MSAA companion texture rather than only the 1x resolve texture.
- **Guard `backbuffer_page_upload` against stale page dimensions** (`src/d3d9/src/surface.rs:3185-3196`, R1 I4): if `WM_SIZE` resized the back buffer while a `LockRect`/`GetDC` page was open, check `(page_w, page_h)` against the current back buffer dimensions before calling `write_presented` (skip upload cleanly if dimensions no longer match).
- Update stale comments (`src/d3d9/rt/src/device/textures.rs:598`, `src/d3d9/src/surface.rs:2811`) and `docs/d3d9/STATUS.md`.
- Update `src/d3d9/rt/src/device/swapchain/tests.rs:298` (`a_refused_back_buffer_write_records_nothing`, which previously asserted MSAA refusal) to test refusal via bad rect/short buffer/pitch while adding dedicated MSAA `write_presented` tests (valid partial/full write, scratch forgotten, refused bad rect records nothing).

### Part 3: Slice 6e (Windowed Core Device Back Buffer Resize)
Files:
- `src/d3d9/rt/src/device/swapchain.rs`
- `src/d3d9/rt/src/device/swapchain/tests.rs`
- `src/d3d9/rt/src/device/draw/tests.rs`
- `src/d3d9/src/device.rs`
- `src/d3d9/src/cursor.rs`
- `src/d3d9/src/direct3d9.rs`
- `docs/d3d9/STATUS.md`

Details:
- **`CoreDevice::make_back_buffer`** (`src/d3d9/rt/src/device/swapchain.rs:235-283`, R1 C2 / R2 C1):
  - Reject `width == 0 || height == 0`: set `self.back_buffer = None`, bump `self.target_gen`, return without touching `self.dirty`.
  - After `texture.allocate(...)`, verify `alloc.texture() != 0`; if zero, set `self.back_buffer = None`, bump `self.target_gen`, return.
  - For `samples > 1`, after companion `allocate(...)`, verify `comp_alloc.texture() != 0` **before** calling `initializer.init_texture_zero` and setting `self.dirty = true`; on failure set `self.back_buffer = None`, bump `self.target_gen`, return without touching `self.dirty`.
  - Add `pub const fn has_back_buffer(&self) -> bool { self.back_buffer.is_some() }`.
- **`CoreDevice::resize(&mut self, width: u32, height: u32, samples: u8) -> bool`** (`src/d3d9/rt/src/device/swapchain.rs:131-138`):
  - Mark `#[must_use]`.
  - After `self.drain(); self.make_back_buffer(width, height, samples);`: if `self.back_buffer.is_none()`, return `false` **before** calling `set_layer_size` (never push rejected/zero dims into `CAMetalLayer`). Otherwise call `set_layer_size` and return `true`.
- **`reset_impl` & `reset_recreate_resources`** (`src/d3d9/src/device.rs:5688, 6132-6135`, R1 C2 / R2 I3):
  - In `reset_impl`: include `core && dev.core.as_deref().is_none_or(|c| !c.has_back_buffer())` in `recreated_for_a_change` alongside `!core && dev.backbuffer_handle.is_null()` so same-size recovery `Reset` recreates a failed back buffer.
  - In `reset_recreate_resources`: if `!core.resize(w, h, samples)`, forget `self.core_auto_depth` (`if let Some(id) = dev.core_auto_depth.take() { core.forget_texture(id); }`), set `dev.depth_stencil_format = 0`, and return `Err(D3DERR_INVALIDCALL)`.
- **`DeviceInner::apply_auto_resize(&mut self, new_width: u32, new_height: u32)`** (`src/d3d9/src/device.rs:3382-3491`, R1 C2, R1 I4, R2 I1, R2 I2, R2 M2):
  1. Move the shared guards (`new_width == 0 || new_height == 0`, `DeviceFlags::RELEASING`, `self.fullscreen.is_some()`, same-size `new_width == self.backbuffer_width && new_height == self.backbuffer_height`) ABOVE the core branch.
  2. Keep `if self.current_frame.get().is_none()` (NOT `self.is_core()`) so a missing core never falls through into `Mtld3dOnly::deref`:
     - Under `#[cfg(feature = "core-runtime")]`:
       - `let Some(core) = self.core.as_deref_mut() else { return; };`
       - Snapshot `old_auto_depth = self.core_auto_depth`, `rebind_depth = old_auto_depth.is_some_and(|id| core.depth() == Some(DepthRef { id, level: 0 }))`, `rt0_is_back_buffer = core.target() == TargetRef::BackBuffer`, and `samples = core_sample_count(self.present_params.multi_sample_type)`.
       - Call `let back_buffer_made = core.resize(new_width, new_height, samples);`.
       - If `!back_buffer_made`:
         - Do NOT update `present_params` or allocate new auto depth.
         - If `let Some(id) = self.core_auto_depth.take()` -> `core.forget_texture(id);`
         - `self.depth_stencil_format = 0;`
         - `self.flags.insert(DeviceFlags::NOT_RESET);`
         - `self.clear_core_draw_cache();`
         - `return;`
       - If `back_buffer_made`:
         - Update `self.set_backbuffer_dims(new_width, new_height)` and `self.set_present_params(pp)` with updated `back_buffer_width`/`back_buffer_height` (also updates `implicit_swapchain`).
         - Call `let _ = remake_core_auto_depth(self, self.depth_stencil_format);` (auto depth failure already clears `depth_stencil_format` and drops `core_auto_depth`).
         - If `rebind_depth` and `let Some(core) = self.core.as_deref_mut()`: `core.set_depth(self.core_auto_depth.map(|id| DepthRef { id, level: 0 }));`.
         - If `rt0_is_back_buffer`: set viewport to `(0, 0, new_width, new_height)` preserving `min_z`/`max_z` on extended devices (`self.is_extended()`) and resetting to `0.0..1.0` on plain devices, and set scissor to `full_target_scissor(new_width, new_height)`.
         - Call `self.clear_core_draw_cache();`
         - `return;`
     - Under `#[cfg(not(feature = "core-runtime"))]`: `return;`.
- Update all 7 existing `core.resize(...)` test calls in `swapchain/tests.rs` and `draw/tests.rs:2204`, plus add host tests for `core.resize(0, 64, 1) == false` (no `CAMetalLayer` mutation, `has_back_buffer() == false`), `core.resize(w, h, s) == true` (`target_gen` bumped, `pass_bound` reset).
- Update stale comments claiming core `apply_auto_resize` returns at once (`device.rs:3382-3386, 4820`, `cursor.rs:22, 1566, 1751`, `direct3d9.rs:2498-2499`) and `docs/d3d9/STATUS.md`.

### Part 4: MacNeutron Row Tests & Gate Counts
Files:
- `dxmt/tests/d3d9_triangle.cpp`
- `dxmt/d3d9-check.sh`
- `vm/dxmt/d3d9-gate.sh`
- `vm/tests/d3d9.sh`
- `build/c4d/s2a/gates/{head,jt,jl,jq,jc,jr,post}.sh`

Details (R1 C3, R1 I5, R3 C2-C4, R3 I1-I3, R3 M2):
1. **`parity` launch (`10 -> 11` rows): `tex-unwritten`**:
   - Do NOT model on `rowLate` (`stateD3d == nullptr` in `parity`). Run two sub-cells:
     - **Cell (a) same-device LIFO pool hit**: on the shared `dev`, create 2x2 `D3DFMT_A8R8G8B8` `D3DPOOL_MANAGED` texture, fill all 4 texels red (`0xFFFF0000`), draw/upload so it lives, release it into `PAGEBOX_POOL`; immediately create a second 2x2 `A8R8G8B8` `MANAGED` texture, lock & write ONLY texel `(0,0) = TGRN` (leaving `(1,1)` untouched), draw and sample both `(0,0)` (assert `TGRN`, non-vacuous control) and `(1,1)` (assert `0x00000000` black in BOTH RGB and alpha, which deterministically reads RED without Fix B!).
     - **Cell (b) cross-device release**: via `RsDevice` (visible 256x256 window on `direct3d`), create & fill a 2x2 red `MANAGED` texture, release texture BEFORE releasing the `RsDevice`, then create a 2x2 `MANAGED` texture on `dev`, write only `(0,0) = TGRN`, sample `(0,0)` = `TGRN` and `(1,1)` = black in RGB & alpha.
2. **`msaa` launch (`21 -> 23` rows): `ms-lblock` and `ms-lbdc`**:
   - Both create their own 4x MSAA lockable device (`D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` + `D3DSWAPEFFECT_DISCARD`, since `msaa`'s shared device has `pp.Flags = 0`).
   - Both read back via `msHas(...)` (resolving MSAA back buffer -> 1x RT -> `GetRenderTargetData`), never `rtHas`, and reset render states inside `until()`.
   - **`ms-lblock`**: draw green full-screen -> writable `LockRect` on back buffer writing red patch on left half -> `UnlockRect` -> draw blue quad on top-right quadrant -> read back via `msHas` and assert red (lock patch), blue (post-unlock draw), and green (untouched region).
   - **`ms-lbdc`**: draw green full-screen -> `GetDC` -> GDI `FillRect` red patch on left half -> `ReleaseDC` -> draw blue quad on top-right quadrant -> read back via `msHas` and assert red, blue, and green.
3. **`fullscreen` launch (`9` rows): update `ws-size`**:
   - Use `rsDevice(r, D3DFMT_D24S8)` so automatic depth-stencil exists and is tested across `WM_SIZE`.
   - Remove the `strict` divergence (`expect` core to follow `WM_SIZE` identically to `mtld3d`).
   - Verify back buffer & viewport grow when resized, draw past `x = 256` derived from `(w, h)` and read back to confirm larger back buffer.
   - Verify plain device resets `min_z`/`max_z` (`0.25..0.75 -> 0.0..1.0`) while an extended device (`rsDevice(rx, D3DFMT_UNKNOWN, true)`) preserves `min_z`/`max_z` across `WM_SIZE`.
   - Verify custom RT0 keeps its own `GetViewport` and `GetScissorRect` across a second `SetWindowPos` (confirming `GetClientRect` actually changed!).
   - Verify custom depth-stencil (`customDs`) remains bound and depth-tests correctly across `WM_SIZE`, explicit `SetDepthStencilSurface(nullptr)` stays unbound across `WM_SIZE`, and rebinding the back buffer via `GetBackBuffer` + `SetRenderTarget` restores auto-resize viewport tracking.
4. **Update counts & comments everywhere**:
   - `dxmt/tests/d3d9_triangle.cpp` (`parity` 11, `msaa` 23, `ws-size` comments)
   - `dxmt/d3d9-check.sh` (parity 11, msaa 23, stale `ws-size` comment)
   - `vm/dxmt/d3d9-gate.sh` (parity 11, msaa 23, stale comment)
   - `vm/tests/d3d9.sh` (parity 11, msaa 23, stale comment)
   - `build/c4d/s2a/gates/{head,jc,jt}.sh` (`MSAA_ROWS:-23`, `PARITY_ROWS:-11`, `JTVAL:-parity msaa fullscreen`)

## Tasks

### Task 1: Host RED tests in DXMTRS (`page_box_pool/tests.rs`, `d3d9-rt` tests)
Write host tests first covering:
- `StagingTake::try_take_zeroed` (fresh zeroed, dirty recycled pool hit, recycled `try_new_uninit` box, zero-length).
- `CoreDevice::write_presented` on 4x MSAA (full & sub-rect upload via 1x scratch + `stretch` into MSAA `companion`, scratch forgotten, bad rect/short buffer/pitch refused with zero Metal calls).
- `CoreDevice::resize` returning `bool` (`resize(0, 64, 1) == false` leaves `has_back_buffer() == false` and does not mutate `CAMetalLayer`; valid resize returns `true`, bumps `target_gen`, resets `pass_bound`).

### Task 2: Implement Parts 1, 2, 3 in DXMTRS (`c4d`)
Implement Fix B (`PageBox::zero_fill`, `try_take_zeroed`, 4 texture sites + offscreen plain + `texture.rs`), MSAA `write_presented` + `surface.rs` gate removal + `mtld3d` 100%-scale MSAA fix + stale-page guard, and Slice 6e (`make_back_buffer` allocation checks, `resize -> bool`, `has_back_buffer`, `reset_impl`, `reset_recreate_resources`, `apply_auto_resize`), plus `STATUS.md` & `CONFORMANCE.md` and comment cleanups. Verify `LC_ALL=C sh tools/rust-check.sh` GREEN.

### Task 3: Add MacNeutron rows & update counts (`c4d-mn`)
Update `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`. Verify `sh -n` and `make dxmt-tests`.

### Task 4: Gates (`build/c4d/s2a/gates/`)
Run `jt1`, `jl1` (full Wine conformance default & core + compare, checking `visual.c:15686`), `jq1` (16 validation launches, 0 reports), `jc1` (VM D3D9 gate default & core old/new WoW64 x64/x86; builder VM stopped), `jr1` (plain build key & imports unchanged).

### Task 5: Final Review, Docs/Spec/Ledger, Privacy Scan, Pin, Push, Post-Push

## Rules check
- **New `static` / config key / wire field / dependency / `Clone`/`Copy` derive / lint suppression / em dash**: none.
- **Outside `src/d3d9/`**: none (`tools/derive_inventory.txt` untouched; translator key unchanged; x64 check not required for C4d-2a).
