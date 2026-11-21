# DXMTRS C4d-4b Final Plan: Full `RenderScale`, `render.lodBias`, & MetalFX Spatial Upscaling on D3D9 Core

**Date:** 2026-11-20
**Status:** Final (folded all 3 independent adversarial reviews: `review-1-presenter.md`, `review-2-d3d9-scale.md`, `review-3-tests-gates.md`; executed cleanly right after C4d-4a)
**Scope:** Complete `render.scale` + `render.lodBias` + MetalFX `MTLFXSpatialScaler` support on `runtime.mode = core` with zero winemetal slot changes and zero half-scaled corruption across every D3D9 draw, clear, stretch, mip, query, and CPU readback/write path.

**Leaves `src/d3d9/`?** Yes (`src/dxmt/fakes/src/lib.rs` for MetalFX scaler fakes; optionally `src/dxmt/src/presenter.rs` if a per-present `skip_gamma` flag is needed for gamma-before-upscale). Requires `jx1` (idle Mac D3D10-12) and `jc1` (`JC_D3D10=1` VM D3D10-12 + VM D3D9 old/new WoW64).

---

## Task 1: `mtld3d-core` & `dxmt-fakes` Foundations

### Files
- `src/d3d9/core/src/present.rs` + `src/d3d9/unix/src/metal/command.rs`
- `src/d3d9/core/src/shader_create.rs` + `src/d3d9/core/src/pipeline_key.rs`
- `src/dxmt/fakes/src/lib.rs`

### Design
1. **Promote settle hysteresis to `mtld3d-core`**:
   - Move `SETTLED_PRESENTS = 30`, `PresentGeometry`, and `geometry_settled` from `src/d3d9/unix/src/metal/command.rs:1241-1303` into `src/d3d9/core/src/present.rs` (`pub`), reusing them in both `mtld3d` and `d3d9-rt`.
   - Use exact enlarging predicate: `src.0 <= dst.0 && src.1 <= dst.1 && (src.0 < dst.0 || src.1 < dst.1)` (never triggers MetalFX on mixed-axis shrink!).
2. **`CoreShaderMeta::reads_vpos` & `PosFixup` scale**:
   - Store `reads_vpos: bool` on `CoreShaderMeta` (`src/d3d9/core/src/shader_create.rs:120-140`).
   - Update `core_ps_variant` (`src/d3d9/core/src/pipeline_key.rs:528-530`) to accept `vpos_scale: bool` and set `VariantFlags::VPOS_SCALE` when `target_scale != RenderScale::IDENTITY && ps_meta.reads_vpos`.
   - Update `pos_fixup` (`src/d3d9/core/src/pipeline_key.rs:887-897`) to accept `point_scale: f32` for lane `[3]` (instead of hardcoding `1.0`), using physical viewport dimensions for lanes `[0..2]` while `FfState::fill_xyzrhw_row` keeps logical viewport dimensions.
3. **`dxmt-fakes` MetalFX scaler fakes (`src/dxmt/fakes/src/lib.rs`)**:
   - Export `MTLDevice_supportsFXSpatialScaler(device: u64) -> bool` (default `true`, controllable per-device via `dxmt_fakes::set_spatial_scaler_supported(device, bool)`).
   - Export `MTLDevice_newSpatialScaler(device: u64, info: *const WMTFXSpatialScalerInfo) -> u64` (returns `0` when unsupported or configured to fail, else `made_of(device)` and records `MTLDevice_newSpatialScaler {in_w}x{in_h}->{out_w}x{out_h} fmt={color}/{out}`).
   - Export `MTLCommandBuffer_encodeSpatialScale(cmdbuf: u64, scaler: u64, color: u64, output: u64, fence: u64)` (records `MTLCommandBuffer_encodeSpatialScale scaler=#{scaler} color=#{color} output=#{output} fence=#{fence}`).

---

## Task 2: `CoreDevice` Separate Render/Drawable Dims, MetalFX Spatial Upscaling, & Scaled Readback/Write Helpers (`d3d9-rt`)

### Files
- `src/d3d9/rt/src/device.rs`
- `src/d3d9/rt/src/device/swapchain.rs`
- `src/d3d9/rt/src/device/surface.rs`
- `src/d3d9/rt/src/device/query.rs`

### Design
1. **Separate render vs drawable dimensions on `CoreDevice`**:
   - Add `pub fn metalfx_available(mtl: &Obj) -> bool` calling `MTLDevice_supportsFXSpatialScaler(mtl.handle())` (slot 68, instant query, zero GPU allocation).
   - Update `attach` / `resize` to take `(render_w, render_h)` and `(drawable_w, drawable_h)` separately; store `drawable_size: (u32, u32)` on `CoreDevice` so `retarget` preserves drawable size (not `back_buffer.width/height`).
   - Allocate back buffer (and MSAA companion) at `(render_w, render_h)` and configure `CAMetalLayer` at `(drawable_w, drawable_h)`.
2. **MetalFX spatial upscaling in `CoreDevice::present` (zero winemetal wire changes, Metal-Validation-clean)**:
   - Track `PresentGeometry { src: (u32, u32), dst: (u32, u32) }` and `settled_count: u32` (`SETTLED_PRESENTS = 30`).
   - Keep a single active (or small bounded) `UpscaleResources { src: (u32, u32), dst: (u32, u32), scaler: Arc<Scaler>, upscaled_output: Arc<Texture>, gamma_scratch: Option<Arc<Texture>> }`. Drop cached resources AND reset `settled_count = 0` inside `resize()` and `retarget()` after `self.drain()` so `_MTLCommandBuffer_encodeSpatialScale` never pins old VRAM textures.
   - `upscaled_output` is a drawable-sized `StorageModePrivate` (`GPU_PRIVATE`) `BGRA8Unorm` `dxmt_core::resource::Texture` with usage `WMT_TEXTURE_USAGE_RENDER_TARGET | WMT_TEXTURE_USAGE_SHADER_READ | WMT_TEXTURE_USAGE_SHADER_WRITE` (satisfies MetalFX's private-output validation rule!).
   - When `src.0 <= dst.0 && src.1 <= dst.1 && (src.0 < dst.0 || src.1 < dst.1)` AND `settled_count >= 30` AND `metalfx_available`:
     - Verify `scaler.scaler().is_some() && scaler.fence().is_some()` (never record a null scaler!). On failure, log once (without `"does not build"` or `"nothing presents"`) and fall back to the linear `Presenter` shader!
     - **Unified zero-winemetal pass chain (works for SDR, SDR+gamma, HDR, and HDR+gamma without feeding HDR > 1.0 into `Perceptual` MetalFX!)**:
       1. `resolve_presented()` (MSAA -> single-sample render-sized `BGRA8Unorm` back buffer).
       2. If `gamma.is_some()`: run `fs_present_d3d9_sdr_gamma` into render-sized `BGRA8Unorm` `gamma_scratch`, then `enc.upscale(&gamma_scratch, &upscaled_output, scaler)` (`Perceptual`), then `enc.present_d3d9(&upscaled_output, ..., D3d9PresentArgs { gamma: None, peak_edr })` (so `Presenter` runs `can_blit` on SDR or HDR `srgb_eotf` + `bt2446_ictcp` on HDR `RGBA16Float`, NEVER double-applying gamma!).
       3. If `gamma.is_none()`: `enc.upscale(&backbuffer, &upscaled_output, scaler)` (`Perceptual`), then `enc.present_d3d9(&upscaled_output, ..., D3d9PresentArgs { gamma: None, peak_edr })`.
     - Emit a once-logged diagnostic line (`gate_level()`) when MetalFX engages on frame 30 (e.g., `"d3d9 core: MetalFX spatial scaler engaged ({}x{} -> {}x{})"`) so E2E gates can confirm MetalFX genuinely ran!
   - While `settled_count < 30` (or unsupported / mixed-axis), present `&backbuffer` directly via `Presenter::encode_d3d9` (bilinear `fs_present_d3d9_*`).
3. **Scaled CPU readback & write resampling helpers (`src/d3d9/rt/src/device/surface.rs`)**:
   - When a readback (`read_back`, `read_presented`) requests logical `(logical_w, logical_h)` differing from physical `(phys_w, phys_h)`: allocate a logical-size scratch texture, run `core.stretch` (render -> logical), and copy the scratch (or requested logical sub-rect) into the staging readback buffer.
   - When a CPU write (`write_presented`, `texture_upload` on scaled RT/backbuffer) supplies logical pixels for a scaled target: upload into a logical-size scratch texture, then run `core.stretch` (logical -> physical) into the target.
4. **Occlusion query logical pixel rescaling (`src/d3d9/rt/src/device/query.rs`)**:
   - Track the active `RenderScale` (or scale factor) on counted draws / occlusion queries and rescale the returned visibility sample count by `logical_area / render_area` matching `mtld3d_core::visibility` (`src/d3d9/core/src/visibility.rs:439-447`).

---

## Task 3: Wire All `RenderScale` Sites Across `src/d3d9/`

### Files
- `src/d3d9/src/direct3d9.rs` (`create_core_device`)
- `src/d3d9/src/device.rs` (`apply_core_auto_resize`, `reset_recreate_resources`, `remake_core_auto_depth`, `core_color_target`, `core_depth_surface`, `GetFrontBufferData`, `UpdateSurface`)
- `src/d3d9/src/surface.rs` (`core_target`, `core_depth`, backbuffer `LockRect`/`GetDC`/`UnlockRect`/`ReleaseDC`, lockable standalone RT `LockRect`/`UnlockRect`/`GetDC`)
- `src/d3d9/src/texture.rs` (`core_flush_dirty`, `core_read_subresource_back`)
- `src/d3d9/src/device/core_draw.rs` (`core_draw::resolve`, `core_target_of`, `lod_table`, `PosFixup`, `VPOS_SCALE` + `PS_DRAW_SLOT`, `slope_scale`)
- `src/d3d9/src/device/core_arms.rs` (`Clear`, `ColorFill`, `StretchRect`, `resz`, `GetRenderTargetData`, `UpdateSurface`)

### Complete Checklist (Every Site Verified by Review 2)
1. **`create_core_device`**: resolve `RenderScale` via `cfg.render_scale_percent` + `d3d9_rt::device::metalfx_available(&mtl)` (same rule as `resolve_render_scale` in `direct3d9.rs:2091-2096`); store on `DeviceInner`; attach core with `(render_w, render_h)` and `(logical_w, logical_h)`; create auto depth-stencil at `(render_w, render_h)` storing `dev.render_scale` on its `Direct3DSurface9`.
2. **`Reset` & `apply_core_auto_resize` & `remake_core_auto_depth`**: pass `(render_w, render_h)` + `(logical_w, logical_h)` to `core.resize`; recreate auto depth at render dims with `dev.render_scale`.
3. **`core_color_target` & `core_depth_surface`**: call `dev.scale_for_created_target(width, height)`, allocate core texture at render dims, store resolved `RenderScale` on `Direct3DSurface9`.
4. **`core_flush_dirty` & shortened mip chains**: create core textures using `ti.render_extent()` and `ti.metal_levels()`; map logical mip `level` via `ti.metal_level(level)` in `surface.core_target()` / `surface.core_depth()` (prevents out-of-bounds Metal mip validation crashes and fixes scaled `INTZ`/`DF24` `RESZ`!).
5. **`core_draw::resolve`**:
   - Resolve `target0_extent: TargetExtent` directly from bound RT0.
   - Pass `if cfg.render_lod_bias && target0_extent.scale != RenderScale::IDENTITY { target0_extent.scale.lod_bias() } else { 0.0 }` ONLY to pixel-stage `lod_table` (NOT `vertex_lod_table`).
   - Compute viewport & scissor using `target0_extent` (far-edge pinned!).
   - Set `VPOS_SCALE` and bind 16-byte `build_ps_draw_bytes(target0_extent.scale)` at `PS_DRAW_SLOT` when `target0_extent.scale != RenderScale::IDENTITY && ps_meta.reads_vpos`.
   - Pass physical viewport to `pos_fixup` with lane `[3] = target0_extent.scale.factor()`; keep logical viewport for `FfState::fill_xyzrhw_row`.
   - Pass `target0_extent.scale.factor()` to `d3d_slope_scale_to_metal`.
6. **`Clear` (`core_arms.rs:89-152`)**: compute clear rects in logical space and scale per-slot / per-depth using each attachment's own `TargetExtent` (handles mixed-scale MRTs cleanly!).
7. **`ColorFill` (`core_arms.rs:835-879`)**: scale logical fill rect through destination `TargetExtent`.
8. **`StretchRect` (`core_arms.rs:341-600, 982-1042`)**:
   - Scale src & dst rects via their respective `TargetExtent`s.
   - Route to `core.stretch` whenever **physical** src/dst dims differ (not just logical `call.scaling`).
   - Pass physical `source` dims to `core.scratch_like` for MSAA resolve scratch.
   - Check `same_surface_route` overlap on **physical** rects while keeping `whole_dst` logical/full-target for `ClearHistory`.
9. **All 7 CPU readback/write paths**: wire `GetFrontBufferData`, back-buffer `LockRect`/`GetDC`/`UnlockRect`/`ReleaseDC`, `GetRenderTargetData`, lockable standalone RT `LockRect`/`UnlockRect`/`GetDC`, scaled RT `Direct3DTexture9` `LockRect`/`UnlockRect`, and `UpdateSurface` to the resampling helpers in `d3d9-rt`.

---

## Task 4: Unit Tests, E2E Tests, & Verification Gates
1. **Host unit tests (`tools/rust-check.sh` + combined clippy + single-threaded)**:
   - `d3d9-rt`: `SETTLED_PRESENTS = 30` hysteresis (frames 1..29 linear shader, frame 30+ `MTLCommandBuffer_encodeSpatialScale` into private output texture); mixed-axis shrink stays on linear shader; `set_spatial_scaler_supported(dev, false)` & null `newSpatialScaler` fall back cleanly; resize/retarget drops old scaler & resets `settled_count`; scaled readback/write round-trips; occlusion query rescaling.
2. **Mac & VM E2E (`d3d9_triangle.cpp`, `d3d9-check.sh`, `vm/tests/d3d9.sh`, `vm/dxmt/d3d9-gate.sh`, `jt`/`jq`)**:
   - Keep `MACNEUTRON_NO_METALFX=1` (suppresses `libmacneutron-present.dylib`, NOT D3D9 MetalFX!).
   - Run a `scale` mode with `MTLD3D_CONFIG="runtime.mode=core;render.scale=0.5;render.lodBias=true;debug.presentOccluded=true"` presenting 40+5 frames under `MTL_DEBUG_LAYER=1` (`0` reports!), verifying all 7 `scale` rows (`scale-desc`, `scale-draw`, `scale-stretch`, `scale-vpos`, `scale-lod`, `scale-write`, `scale-mfx`):
     - Logical `D3DSURFACE_DESC` dims preserved (`scale-desc`)
     - Viewport/scissor/`Clear`/`ColorFill`/`GetRenderTargetData` readbacks pass (`scale-draw`)
     - `StretchRect` + sub-rect + readonly backbuffer `LockRect` pass (`scale-stretch`)
     - `ps_3_0` `vPos` coordinate mapping passes (`scale-vpos`)
     - `render.lodBias = true` mip selection compensation passes (`scale-lod`)
     - Sub-viewport scaling + writable backbuffer `LockRect(0)` resampling pass (`scale-write`)
     - 40 presents + occlusion query logical area rescaling + `D3DGAMMARAMP` gamma pass + MetalFX spatial upscaling (`scale-mfx`), and diagnostic line confirms `MetalFX spatial scaler engaged` on frame 30.
   - Full gate suite: `rust-check.sh`, `jt1`, `jl1`, `jq1`, `jx1`, `jc1` (`JC_D3D10=1`), `jr1`, fresh final review, push, `post.sh`, CI.
