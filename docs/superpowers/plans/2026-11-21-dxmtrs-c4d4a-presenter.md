# DXMTRS C4d-4a Final Plan: D3D9 Core Presenter Options (Non-Stalling Inline Gamma, BT.2446-A EDR, Linear Scaling, `present.maxFps` / Divided Intervals, `debug.presentOccluded`)

**Date:** 2026-11-20
**Status:** Final (folded all 3 independent adversarial reviews: `review-1-presenter.md`, `review-2-d3d9-scale.md`, `review-3-tests-gates.md`; `RenderScale` + MetalFX upscaling split cleanly into C4d-4b so every scaled readback/write/shader/MRT/stretch/occlusion path lands complete and atomic)
**Scope:** Bring D3D9 core presentation options to parity with `mtld3d` with **zero winemetal slot or wire-struct changes** and **zero D3D10-12 visual or C-ABI regressions**:
1. 6 dedicated D3D9 present fragment shaders in `src/dxmt/dxmt_command.metal` (`fs_present_d3d9_{sdr,sdr_gamma,hdr_passthrough,hdr_passthrough_gamma,hdr_bt2446,hdr_bt2446_gamma}`) using `present_data input [[stage_in]]` and `constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);` (leaving D3D11/D3D12 `fs_present_quad` untouched).
2. Opt-in Rust-only D3D9 mode on `dxmt_core::presenter::Presenter` (`Presenter::new_d3d9`), carrying per-frame `D3d9PresentArgs { gamma: Option<Arc<[u16; 1024]>>, peak_edr: f32 }` through `PassKind::Present` / `Encoder::present_d3d9` so API-thread gamma/EDR updates NEVER race with encode-thread presentation and NEVER call `wait_presented`.
3. `present_after(interval, refresh_hz, max_fps)` matching `mtld3d`'s exact `layer_pacing_for` + `vsync.max(cap)` formula (no `vsync_duration` double-division), sourcing `refresh_hz` from `reported_display_mode(pp).refresh_rate` and syncing `WMTLayerProps.display_sync_enabled`.
4. `debug.presentOccluded` forwarded into `core_arms::present`.
5. `dxmt-fakes` and `presenter/tests.rs` opt-in test hooks (preserving all 37 `golden_replays` bit-for-bit) + Mac/VM E2E tests for `INTERVAL_IMMEDIATE + present.maxFps`, `INTERVAL_TWO`, `debug.presentOccluded`, and multi-frame rapid fullscreen `SetGammaRamp`.

**Leaves `src/d3d9/`?** Yes (`src/dxmt/dxmt_command.metal`, `src/dxmt/meson.build`, `src/dxmt/src/{presenter.rs,presenter/tests.rs,encoder/pass.rs,encoder/flush.rs}`, `src/dxmt/fakes/src/lib.rs`). Requires `jx1` (bounded idle Mac x64 D3D10-12 check) and `jc1` (`JC_D3D10=1` VM D3D10-12 + VM D3D9 old/new WoW64).

---

## Task 1: `dxmt_command.metal` & `src/dxmt/meson.build`

### Files
- `src/dxmt/dxmt_command.metal`
- `src/dxmt/meson.build`

### Design
1. In `src/dxmt/meson.build`, add `depend_files : 'dxmt_command_constants.hpp'` to `dxmt_command_air` so incremental builds never reuse a stale `dxmt_command.metallib`.
2. In `src/dxmt/dxmt_command.metal`:
   - Add `#include <metal_matrix>` and `#include <metal_geometric>` (needed for `float3x3` and `dot` in BT.2446-A).
   - Leave `fs_present_quad` (line 307) and all `kPresentFCIndex_*` constants 100% untouched.
   - Add `struct D3D9HdrUniforms { float l_hdr_nits; float p_hdr; float log2_p_hdr; float inv_p_minus_one; };` (16 bytes, `#[repr(C)]` match in Rust).
   - Port `mtld3d`'s exact `apply_gamma_ramp`, `srgb_eotf`, `pq_oetf`, `pq_eotf`, `bt2446_ictcp` math verbatim from `src/d3d9/unix/src/metal/present.msl:50-272`, ensuring gamma runs BEFORE `srgb_eotf` and `bt2446_ictcp`.
   - Define 6 dedicated entry points taking `present_data input [[stage_in]]` (matching `vs_present_quad`'s `[[user(coord)]]` output) with zero function constants so Metal API Validation (`jq1`) sees exact bindings on every frame:
     1. `fs_present_d3d9_sdr`: `texture2d<float> source [[texture(0)]]`
     2. `fs_present_d3d9_sdr_gamma`: `texture(0)`, `constant ushort4 *gamma_lut [[buffer(1)]]` (2048B)
     3. `fs_present_d3d9_hdr_passthrough`: `texture(0)`
     4. `fs_present_d3d9_hdr_passthrough_gamma`: `texture(0)`, `buffer(1)`
     5. `fs_present_d3d9_hdr_bt2446`: `texture(0)`, `constant D3D9HdrUniforms &u [[buffer(0)]]` (16B)
     6. `fs_present_d3d9_hdr_bt2446_gamma`: `texture(0)`, `buffer(0)`, `buffer(1)`
   - All 6 sample via `constexpr sampler s(coord::normalized, filter::linear, address::clamp_to_edge);`.
3. Verify MSL syntax directly with `xcrun -sdk macosx metal -c -std=metal3.1 -O2 src/dxmt/dxmt_command.metal -o /tmp/dxmt_command_c4d4a.air` before running full build/gates.

---

## Task 2: Opt-In Rust-Only D3D9 Mode on `Presenter` & Per-Frame `Encoder::present_d3d9`

### Files
- `src/dxmt/src/presenter.rs`
- `src/dxmt/src/presenter/tests.rs`
- `src/dxmt/src/encoder/pass.rs`
- `src/dxmt/src/encoder/flush.rs`

### Design
1. **Zero C-ABI / D3D10-12 changes**:
   - Keep `DXMTPresentMetadata` (12 bytes), `src/dxmt/src/presenter/abi.rs`, `Presenter::new`, `Presenter::with_prefetch`, `FrameGen`, and all 37 `golden_replays` 100% untouched.
2. **Encapsulated D3D9 state on `presenter::State`** (avoids `clippy::struct_excessive_bools` without `#[allow]`):
   - Add `d3d9: Option<D3d9State>` to `presenter::State`:
     - `hdr_enable: bool`
     - `mode: D3d9LayerMode` (`Unset | Sdr | Hdr`)
     - `display_sync: bool`
     - `psos: FxHashMap<(D3d9PresentVariant, u32), Option<Arc<Obj>>>`
   - `#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)] enum D3d9PresentVariant { Sdr, SdrGamma, HdrPassthrough, HdrPassthroughGamma, HdrBt2446, HdrBt2446Gamma }`
   - `#[repr(C)] #[derive(Clone, Copy, Debug)] struct D3D9HdrUniforms { l_hdr_nits: f32, p_hdr: f32, log2_p_hdr: f32, inv_p_minus_one: f32 }` (exact `mtld3d` `hdr_uniforms(peak)` math: `l_hdr_nits = 203.0 * peak`, `p_hdr = 1.0 + 32.0 * (l_hdr_nits / 10000.0).powf(1.0 / 2.4)`, etc., verified against `src/d3d9/unix/src/metal/present.rs:160-166`).
3. **`Presenter::new_d3d9(device, metal_layer, cmd_library, width, height, hdr_enable) -> Arc<Self>`**:
   - Skips allocating the unused 1024x1 `RGBA32Float` `gamma_lut_texture`!
   - Initializes `layer_props` with `drawable_width = width`, `drawable_height = height`, `pixel_format = WMT_PIXEL_FORMAT_BGRA8_UNORM`, `framebuffer_only = 0`, `display_sync_enabled = 1`.
4. **Dedicated API-thread sync (`Presenter::prepare_d3d9_present(&self, width, height, display_sync) -> f32`)**:
   - Queries `MetalLayer_getEDRValue(layer, &mut edr)` (slot 97, returns `1.0/1.0` when off-screen, zero `WMTHDRMetadata` marshaling).
   - Computes `want_hdr = hdr_enable && edr.maximum_potential_edr_color_component_value > 1.0 && edr.maximum_potential_edr_color_component_value.is_finite()`.
   - Updates `CAMetalLayer` ONLY when `mode`, `(width, height)`, or `display_sync` actually changed (NEVER calling `wait_presented`):
     - `SDR -> HDR`: `MetalLayer_setProps` (`WMT_PIXEL_FORMAT_RGBA16_FLOAT`, `width`, `height`, `display_sync`) first, then `MetalLayer_setColorSpace(WMT_COLOR_SPACE_HDR_SC_RGB)`, then `invalidate_drawable()`.
     - `HDR -> SDR`: `MetalLayer_setColorSpace(WMT_COLOR_SPACE_SRGB)` first, then `MetalLayer_setProps` (`WMT_PIXEL_FORMAT_BGRA8_UNORM`, `width`, `height`, `display_sync`), then `invalidate_drawable()`.
     - Same mode, size or `display_sync` changed: `MetalLayer_setProps` preserving current mode's `pixel_format`, then `invalidate_drawable()`.
   - Returns `peak_edr = if want_hdr && edr.maximum_edr_color_component_value > 1.0 && edr.maximum_edr_color_component_value.is_finite() { edr.maximum_edr_color_component_value } else { 1.0 }`.
5. **Per-frame `D3d9PresentArgs` on `PassKind::Present` & `Encoder::present_d3d9`** (eliminates API-thread vs encode-thread gamma race!):
   - `pub struct D3d9PresentArgs { pub gamma: Option<Arc<[u16; 1024]>>, pub peak_edr: f32 }`
   - Stored as `d3d9: Option<D3d9PresentArgs>` on `PassKind::Present` (`None` for D3D11/D3D12 `Encoder::present`).
   - Inside `Encoder::encode_present` -> `Presenter::encode_d3d9`:
     - Acquires drawable (bails cleanly if `texture == 0`).
     - Determines `drawable_fmt = { let f = MTLTexture_pixelFormat(tex); if f == 0 { layer_props.pixel_format } else { f } }`.
     - Computes `can_blit`: ONLY allowed when `args.gamma.is_none()` AND `drawable_fmt == WMT_PIXEL_FORMAT_BGRA8_UNORM` AND source/drawable sizes match (`src_w == dst_w && src_h == dst_h && src_w > 0 && src_h > 0`) AND `source_format == drawable_fmt`! When `can_blit` is true, copies via blit encoder; otherwise selects exact `D3d9PresentVariant`:
       - `drawable_fmt == WMT_PIXEL_FORMAT_RGBA16_FLOAT`: `peak_edr > 1.0` -> `HdrBt2446[Gamma]`, else `HdrPassthrough[Gamma]`.
       - Otherwise: `Sdr[Gamma]`.
     - Looks up `(variant, drawable_fmt)` in `d3d9.psos`. On miss, drops `MutexGuard`, wraps `Pool::new()`, creates `MTLLibrary_newFunction` + `MTLDevice_newRenderPipelineState`, re-locks and inserts-if-absent (`Option<Arc<Obj>>`), keeping the `Arc<Obj>` alive outside the lock across command encoding.
     - Encodes render pass: `SetPSO`, `SetViewports`, `SetFragmentTexture(0, backbuffer)`, `SetFragmentBytes(0, 16B HdrUniforms)` ONLY on `HdrBt2446*`, `SetFragmentBytes(1, 2048B gamma)` ONLY on `*_Gamma`, `DrawPrimitives(Triangle, 0, 3)`. Never binds `gamma_lut_texture` at `texture(1)`.
6. **Unit tests in `src/dxmt/src/presenter/tests.rs`**:
   - Add opt-in `drawable_pixel_format: Option<u32>`, `drawable_size: Option<(u64, u64)>`, and `edr_override: Option<WMTEDRValue>` on `Fake` (all `None` by default -> all 37 `golden_replays` remain byte-for-byte unchanged!) + export `MetalLayer_getEDRValue` stub.
   - Add tests verifying:
     - Toggling/changing inline gamma across back-to-back frames triggers ZERO `wait_presented` calls, ZERO redundant PSO rebuilds, and binds exact per-frame LUT bytes at `buffer(1)` without frame-to-frame overwrite.
     - Two-step EDR bootstrap (`potential=2.0, current=1.0` -> sets `RGBA16Float` then `HDR_SC_RGB` and encodes `HdrPassthrough`; next frame `potential=2.0, current=2.0` -> encodes `HdrBt2446` with 16B `buffer(0)` + 2048B `buffer(1)` when gamma active; drop back to `potential=1.0` -> sets `SRGB` then `BGRA8Unorm` and encodes `Sdr`).
     - `can_blit` is disabled whenever `gamma.is_some()` or `drawable_fmt == RGBA16Float`.

---

## Task 3: `present_after`, `debug.presentOccluded`, Non-Stalling Gamma, & HDR Wiring in `d3d9-rt` and `d3d9`

### Files
- `src/d3d9/rt/src/device.rs`
- `src/d3d9/rt/src/device/swapchain.rs`
- `src/d3d9/rt/src/device/swapchain/tests.rs`
- `src/d3d9/src/direct3d9.rs`
- `src/d3d9/src/device.rs`
- `src/d3d9/src/device/core_arms.rs`
- `src/dxmt/fakes/src/lib.rs`

### Design
1. **`CoreOptions::HDR_ENABLE`**:
   - Add `const HDR_ENABLE = 1 << 5;` to `CoreOptions` (`src/d3d9/rt/src/device.rs`): zero extra `bool` fields on `CoreDevice` (respects `clippy::struct_excessive_bools`).
   - Forward `cfg.hdr_enable` from `create_core_device` (`src/d3d9/src/direct3d9.rs`) into `CoreOptions::HDR_ENABLE`.
2. **Exact `present_after` & `display_sync` (`src/d3d9/rt/src/device/swapchain.rs`)**:
   - Signature: `pub fn present_after(interval: u32, refresh_hz: u32, max_fps: u32) -> f64`
   - Formula (no `vsync_duration` double-division):
     ```rust
     let rate = if refresh_hz == 0 { 60 } else { refresh_hz };
     let pacing = mtld3d_core::present::layer_pacing_for(interval, rate, max_fps);
     let vsync = if pacing.display_sync { 1.0 / f64::from(rate) } else { 0.0 };
     let cap = if pacing.max_fps > 0 { 1.0 / f64::from(pacing.max_fps) } else { 0.0 };
     vsync.max(cap)
     ```
   - Pass `display_sync = pacing.display_sync` into `CoreDevice::present(visible, after, display_sync)` -> `presenter.prepare_d3d9_present(w, h, display_sync)`.
   - In `core_arms::present` (`src/d3d9/src/device/core_arms.rs`):
     - Source `refresh_hz` from `dev.reported_display_mode(pp).refresh_rate` (matching `mtld3d`'s `resolve_layer_pacing`, honoring `pp.full_screen_refresh_rate_in_hz` and emitting the same warn-once on `DisplaySync::Fallthrough` / zero refresh rate).
     - Forward `cfg.present_max_fps` and `cfg.present_occluded`:
       `let visible = cfg.present_occluded || (window != 0 && unsafe { IsWindowVisible(window) } != 0 && !dxmt_util::wsi::is_minimized(window));`
3. **Inline `[u16; 1024]` gamma on `CoreDevice`**:
   - Store `gamma: Option<Arc<[u16; 1024]>>` on `CoreDevice` (reusing existing `mtld3d_core::gamma::to_lut(ramp)` from `src/d3d9/core/src/gamma.rs:107`; delete obsolete `DXMTGammaRamp` 1024-entry upload path).
   - Survives `CoreDevice::retarget` automatically because `self.gamma` lives on `CoreDevice` and `open_window` constructs `Presenter::new_d3d9(..., self.options.contains(CoreOptions::HDR_ENABLE))`!
   - Update `CoreDevice::set_gamma_ramp` signature if cleaner (takes `Option<[u16; 1024]>` directly, wrapping in `Arc::new`, or keeps `Option<([u16; 256], [u16; 256], [u16; 256])>`; taking `Option<[u16; 1024]>` directly reuses `mtld3d_core::gamma::to_lut` cleanly!).
   - Update `src/d3d9/src/device.rs:4263` log message to:
     `"d3d9 core: the gamma ramp is applied inline by the presenter of a fullscreen device"`
     (preserves `d3d9 core: .*gamma ramp.*appl` grep contract in `gates/head.sh`!).
4. **`CoreDevice::resize` preserves HDR mode**:
   - Store `drawable_size: (u32, u32)` on `CoreDevice` (preparing cleanly for C4d-4b too!).
   - `set_layer_size` calls `presenter.set_d3d9_drawable_size(width, height)` instead of `change_layer_properties(BGRA8_UNORM, SRGB, ...)`, so `Reset` / auto-resize NEVER clobbers HDR `RGBA16Float` layers!
5. **`dxmt-fakes` & `swapchain/tests.rs` updates**:
   - In `src/dxmt/fakes/src/lib.rs`:
     - Add opt-in per-device `edr: Option<WMTEDRValue>`, `drawable_format: Option<u32>`, `drawable_size: Option<(u64, u64)>` (all `None` by default so zero existing tests break).
     - Log `MetalLayer_setProps` and `MetalLayer_setColorSpace`; record `after=` on `MTLCommandBuffer_presentDrawableAfterMinimumDuration`; decode `WMT_RENDER_COMMAND_SET_FRAGMENT_BYTES` (render record 11) in `fake_render_encode_commands`.
   - In `src/d3d9/rt/src/device/swapchain/tests.rs`:
     - Update `a_retarget_keeps_the_back_buffer_and_makes_a_presenter` (no longer expects unused `1024x1x1` `gamma_lut_texture`; asserts `made.is_empty()` and verifies `CreateMetalViewFromHWND` + `Presenter` retarget).
     - Update `present_after_follows_the_interval` for 3-arg `present_after(interval, refresh_hz, max_fps)` covering `IMMEDIATE+maxFps`, `ONE` with `maxFps > refresh_hz` vs `maxFps < refresh_hz`, `TWO`/`THREE`/`FOUR` with `refresh_hz = 60` and `0`.
     - Add tests for non-stalling inline gamma (`buffer(1)` 2048B, surviving `retarget`), HDR two-step EDR mode switching, and `debug.presentOccluded`.

---

## Task 4: MacNeutron E2E Tests & Verification Gates

### Files (MacNeutron `c4d-mn`)
- `dxmt/tests/d3d9_triangle.cpp`:
  - Add `present-pace` (or extend `vsync` / `fullscreen` / `parity`):
    - `INTERVAL_IMMEDIATE` + `present.maxFps=30` (15 presents take >= 0.40s)
    - `INTERVAL_TWO` pacing
    - Multi-frame rapid fullscreen `SetGammaRamp` updates in `gm-full` (3 distinct ramps presented back-to-back without stalling)
    - Hidden window present with `debug.presentOccluded=true` vs `false`
- `dxmt/d3d9-check.sh`, `vm/tests/d3d9.sh`, `vm/dxmt/d3d9-gate.sh`, `build/c4d/s4/gates/{head,jt,jl,jq,jx,jc,jr,post}.sh`:
  - Update row counts & `nval` in lockstep; run `xcrun -sdk macosx metal` smoke check, `tools/audit.sh --update-derives`, `tools/rust-check.sh` 6/6, combined `core-runtime,perf-build` clippy (`arm64ec`, `i686`), single-threaded `dxmt-core`/`d3d9-rt` tests, `jt1`, `jl1`, `jq1` (0 Metal validation reports), `jx1` (idle Mac D3D10-12), `jc1` (`JC_D3D10=1` VM D3D10-12 + VM D3D9 old/new WoW64), `jr1` (plain build), fresh final review, push, `post.sh`, CI.
