# DXMTRS C4b-3d2: the depth-stencil on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, a device is created with an auto depth-stencil, `CreateDepthStencilSurface` and
depth-format textures exist as core textures, `Set/GetDepthStencilSurface` work, draws test and write depth and
stencil with D3D9's state (bias, clamp and pixel-shader depth included), `Clear` takes Z and stencil for whole-target
clears, and depth textures are sampled (shadow compare, raw INTZ/DF16/DF24 fetch, fetch4), as under mtld3d.

**Architecture:** second of C4b-3d's five slices (3d1 targets, done; **3d2 depth-stencil**; 3d3 blending, scissor,
partial clears; 3d4 MRT; 3d5 MSAA and the Wine suite sweep). `CoreDevice` (d3d9-rt) gains a current depth-stencil
kept by reference (`DepthRef`: a stored depth texture and a level) beside the colour target. **One fact decides
everything a draw or Clear does with depth: `CoreDevice::depth_state()`**, read once a draw by the front end as
`target_desc()` is: `None` (none bound), `Attached(format)` (bound, of the colour target's size), `Detached(format)`
(bound, but not attachable to the current colour target: another size, or no target). From it the front end picks
the draw's **key format** (`PipelineKey::depth_format`: the attached format, or 0), and the key format alone then
decides the pass's planar flags, the pipeline's depth and stencil formats, `NO_DEPTH_ATTACHMENT`, whether a
depth-stencil state is encoded and its stencil gating, and the pre-transformed clamp lane. A draw whose key format
is 0 while a depth is bound draws in a pass **without** the depth (a detached draw): that is how a draw using no
plane proceeds when the depth is `Detached` or is sampled by the draw itself; a draw that uses a plane in those
cases is refused. A depth texture is a core texture made on the render-target path (render-target usage,
per-subresource tracking, zeroed by the initializer), so a depth surface, a depth texture's level and the auto
depth-stencil are one kind of thing. The state comes from mtld3d-core's `depth_stencil_state` (already Metal's
enums). Sampling reuses mtld3d's variant masks and the airconv lowering unchanged. Nothing changes for the default
runtime.

**Tech Stack:** Rust 1.99.0 (DXMTRS: mtld3d-core, d3d9-rt, d3d9.dll front end, dxmt-fakes), C++ (`d3d9_triangle`), sh
gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line and 3d1's paragraph). Maps:
`build/c4b3d/map-3d2.md` (this slice; cited as M §n and hazards as H n), `build/c4b3d/map.md` (all of C4b-3d),
`build/c4/map2-passes.md` (the pass rules). Reviews: `build/c4b3d/s2/review-{1-design,2-core,3-exec}.md` (folded;
dispositions at the end, cited as R1-R3).

## Global Constraints

- The default runtime (mtld3d) is unchanged: translator key `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9`
  and the plain d3d9.dll imports unchanged; every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]`
  and a runtime `is_core()` check; a core-only struct field or private helper is cfg'd in the struct, in every
  literal and at its definition. Shared mtld3d-core functions keep mtld3d's behaviour (its unit tests pass unchanged).
- `tools/rust-check.sh` passes after every DXMTRS commit; a d3d9-rt or mtld3d-core API change carries its front-end
  call sites in the same commit. New files are `git add`ed before it; a new Clone/Copy derive needs
  `sh tools/audit.sh --update-derives`.
- No new dependency. No `HashMap` (FxHashMap). No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the
  body; lines of 100 columns at most.
- **No dxmt-core change** (ruling C4b3d2-6; R2 confirmed it is achievable): D3D10-12 are untouched. That includes
  dxmt-core's `test_support` record printer, which the fakes use.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`) and `build/c4b3c/mn` (branch `c4b3d-mn`), cargo target
  `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s2/gates` (copied from `s1/gates`, `S=build/c4b3d/s2`,
  state names `c4b3d2-*`), logs and reviews in `build/c4b3d/s2`. Builds and GPU jobs under the pipeline lock, `$D`
  and the main checkout restored by each job's trap. No make in a worktree. A job tests **committed** tips.
- Commands: as C4b-3d1's plan (`. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; `cargo test --offline --target aarch64-apple-darwin
  -p <crate>`, d3d9-rt with the `dynamic_lookup` rustflags; mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`).
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x2e0-0x2ff** here, one a test (0xe1 and 0xe2 are taken; R3 I3).
- Every DXMTRS commit is safe to run under core: a refusal is lifted only in the commit that makes its calls correct.
  MacNeutron's core lanes are red between Task 3's rows and Task 7's pin by design.
- dxmt-fakes is read only by d3d9-rt's tests and its own: a new fake line is printed after the existing line it
  belongs to, and no existing line's text changes. The initializer's zeroing pass of a new depth texture goes through
  the same fakes and is committed first: depth tests filter it out before counting passes (R2 I2).
- Benches and probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents
  too) while the x64 `dxmt/check.sh` runs.

## Scope

In: the creation gate for `EnableAutoDepthStencil` (windowed, one sample); `CreateDepthStencilSurface` (one sample);
`CreateTexture` of a depth format as an attachment (every level) and as a sampled texture at pixel slots;
`SetDepthStencilSurface`, `GetDepthStencilSurface`; `D3DRS_ZENABLE`, `ZWRITEENABLE`, `ZFUNC`, the stencil states
(two-sided included), `STENCILREF` and masks, `DEPTHBIAS`, `SLOPESCALEDEPTHBIAS`; the pre-transformed depth clamp
rule; a pixel shader's depth output; `Clear` with `D3DCLEAR_ZBUFFER`/`D3DCLEAR_STENCIL` over the whole target;
fetch4 on every eligible format; a draw that uses no depth or stencil plane while the bound depth-stencil has
another size than the target, or while it samples the bound depth-stencil (drawn without it).

Out (owners): a draw that tests or writes a plane of a depth-stencil of another size than the colour target, the
Z/stencil Clear of one, rect and partial-viewport depth clears and `clear_into_render`'s area guard (3d3);
multisampled depth surfaces and RESZ (3d5); a draw that tests or writes the depth-stencil while sampling it (the
copy route), depth textures at vertex samplers, a dynamic depth texture's lock and upload, the
`depth.aliasSameSize` carry, `Reset`, render scale, fullscreen (C4c); the pass map's economy rules (DONT_CARE
planes, `drop_dead_store`, the later store in `same_targets`) and per-draw state dedup (C4d); the 1x1 "RT0 drop"
(not planned: mtld3d's special case for one title shape, M §2.7).

## Review Focus

1. **A pass and its pipeline agree on the planes**, for each of: no depth bound, depth explicitly unbound
   (`SetDepthStencilSurface(NULL)`), a depth-only format, a combined format, a colour-target change with the depth
   kept, a depth change with the colour kept, a detached draw between two attached ones. A disagreement is a draw
   Metal drops without an error. Checked by the key deciding both (rt), rt's `debug_assert`, the rows `ds-test`,
   `ds-surface`, `ds-unbound`, `ds-persist`, `ds-size`, `ds-feedback`, and one core `depth` launch under the Metal
   validation layer with no report (Task 5 Step 5; R3 I4).
2. **Stencil against a format without the plane**: `STENCILENABLE` on D24X8/D16 draws untested and unwritten, and
   `Clear(STENCIL)` alone on it records nothing (H8: unmasked it would run as a colour clear) (`ds-nostencil`; rt
   `a_stencil_clear_of_a_depth_only_format_records_nothing`).
3. **A Clear with no draw after it still lands, and Clear then draw is one pass** whose depth load action is the
   clear (`ds-clearonly`; rt `a_clear_then_a_draw_is_one_pass`, `a_clear_with_no_draw_is_a_pass_of_its_own`), and a
   Z clear of a depth bound with a target of another size is refused, not folded into a smaller pass (H9;
   `ds-size`; R2 traced H9 closed with the refusal).
4. **State changes inside one pass**: a draw with `ZENABLE` off after one with it on gets a state that neither
   tests nor writes; the stencil reference follows every change, back to 0 included (`ds-zenable`, `ds-stencil`).
5. **Lifetime and order**: a depth texture written by one pass, sampled by a detached draw and written again is
   three passes in order, the reader waiting on the writer's fence (`ds-feedback`, `ds-shadow`, `ds-intz`; rt
   `a_draw_sampling_the_bound_depth_detaches_it`); a depth surface released by the application while bound still
   tests (`ds-release`); forgetting the bound depth's texture unbinds it and closes the pass (rt unit test).

---

### Task 1: the pipeline key and the per-draw depth values (mtld3d-core)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` and `src/d3d9/core/src/pipeline_key/tests.rs`; the call sites in
`src/d3d9/src/device/core_draw.rs` (`:290`, `:348`) and `src/d3d9/rt/src/device/draw.rs` (still "no depth").

**Interfaces (produces):**
- `PipelineKey { .., pub depth_format: u32 }` and `PipelineSource { .., pub depth_format: u32 }`: the attached
  depth-stencil's `WMTPixelFormat`, `WMT_PIXEL_FORMAT_INVALID` (0) for a draw without one; forwarded by
  `pipeline_source`.
- `core_ps_variant(base, ps_is_prog, point_list, linked_input_mask, has_depth: bool)`: `NO_DEPTH_ATTACHMENT` is
  inserted for a programmable stage only when `!has_depth` (and removed otherwise).
- `pos_fixup(v, target, clamp_z: bool, depth_bias: f32) -> [f32; 5]`: lane 2 is `clamp_z`, lane 4 `depth_bias`.
- `pub const fn rhw_depth_clamp(rhw: bool, has_depth: bool, depth_enable: bool) -> bool` =
  `rhw && !(has_depth && depth_enable)` (mtld3d's rule, M §3.3).

- [ ] **Step 1: failing tests**: the key table gains a `depth_format` case (two keys differing only in it are
  unequal and their sources carry it); `core_ps_variant_keeps_depth_output_with_a_depth` (prog + `has_depth` →
  flag absent; prog + no depth → present; FF untouched); `pos_fixup_carries_clamp_and_bias`;
  `rhw_depth_clamp_follows_mtld3d` (the four cases of M §3.3). Run `-p mtld3d-core pipeline_key` — Expected: does
  not compile (missing field and parameters).
- [ ] **Step 2: implement**; front-end and rt call sites pass `depth_format: 0`, `has_depth: false`,
  `clamp_z: layout.has_rhw()`, `depth_bias: 0.0` (today's behaviour).
- [ ] **Step 3: run** the tests, then `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 4: commit** `mtld3d-core: the pipeline key carries the depth-stencil's format; the clamp and bias lanes`.

### Task 2: the core device's depth-stencil (dxmt-fakes, d3d9-rt)

**Files:** Create `src/d3d9/rt/src/device/depth.rs` and `depth/tests.rs`; modify `src/d3d9/rt/src/device.rs`
(fields), `device/recorder.rs`, `device/draw.rs`, `device/textures.rs`, `device/target.rs`, `device/fixture.rs`,
`src/dxmt/fakes/src/lib.rs`, and `src/d3d9/src/device/core_draw.rs` (the `DrawData` literal near `:508`).

**Interfaces (produces):**
```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DepthRef { pub id: TextureId, pub level: u32 }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DepthState { None, Attached(u32), Detached(u32) }      // the bound depth's WMTPixelFormat
impl CoreDevice {
    pub fn set_depth(&mut self, depth: Option<DepthRef>) -> bool;   // false, nothing changed: Some that is no depth texture's level
    pub const fn depth(&self) -> Option<DepthRef>;
    pub fn depth_state(&self) -> DepthState;                        // the one fact (Architecture)
    pub fn clear_depth_stencil(&mut self, planes: u32, depth: f32, stencil: u8);   // planes: depth 1, stencil 2
}
// DrawData gains:
pub depth_stencil: DepthStencilSnapshot,      // from the render states, ungated
pub stencil_ref: u8,
pub slope_scale: f32,
// DrawRefused gains `Depth`: the key names a depth format the device cannot attach.
```
- A depth texture is a stored texture whose `pixel_format` has depth-stencil planes
  (`dxmt_core::format::depth_stencil_planar_flags != 0`) and `render_target: true`; `set_depth` resolves only those,
  and `texture_shape` (the colour resolver) refuses them, so neither kind resolves as the other.
- `depth_state()`: `None` with no depth bound (a bound depth whose texture is gone cannot occur: `forget_texture`
  unbinds); `Attached(f)` when the colour target resolves and the depth level's size equals its size; else
  `Detached(f)` (R1 M-3).
- One private resolver gives the depth's `(Arc<Texture>, TextureViewKey, width, height, pixel_format)`: the view is
  `WMT_TEXTURE_TYPE2D`, the stored format, `first_mip = level`, `mip_count 1`, `first_slice 0`, **`array_size 1`**
  (R2 M2); the pass and the Clear both use it.
- `set_depth` closes the open render pass on a change; `set_target` is unchanged (the depth persists across it);
  `forget_texture(id)` of the bound depth's texture closes the pass and unbinds it.
- **The key decides the pass.** `draw` takes `with_depth = key.depth_format != 0` and
  `planar = depth_stencil_planar_flags(key.depth_format)` (no second look-up a draw, R3). The device remembers
  whether the open render pass has the depth (`render_depth: bool`); a draw of the other kind closes the pass and
  opens its own. Opening a pass with the depth checks `depth_state() == Attached(key.depth_format)` **before
  anything is emitted**, else `Err(DrawRefused::Depth)` (R2 M4); `debug_assert` the same for a pass already open.
- `open_render_pass(with_depth)`: with the depth, `start_render_pass(planar, 0, 1, None)`, one
  `enc.attachment::<{ stage::PIXEL }>(&texture, key, Access::READ)` call for the depth `Target` and a second for the
  stencil `Target` when `planar & 2` (`Attachment` is not `Clone`, R2 M1; the write is `end_pass`'s), each
  `WMT_LOAD_ACTION_LOAD` / `WMT_STORE_ACTION_STORE` (M §4.1). Without it, as today.
- `build_pipeline`: `info.depth_pixel_format = f` when `planar & 1`, `info.stencil_pixel_format = f` when
  `planar & 2`, from `source.depth_format`.
- With the depth, each draw encodes `WmtcmdRenderSetdsso { dsso, stencil_ref }` after the pipeline: the state of
  `draw.depth_stencil.gated_on_stencil_attachment(planar & 2 != 0)`, looked up by `key_from_snapshot(..).raw()` in
  `depth_states: FxHashMap<u64, Obj>` (kept for the device's life, as `pipelines`; a recorded closure names the
  handle), made on a miss from `description_from_snapshot` mapped field by field into `WMTDepthStencilInfo`
  (compare functions and stencil operations are Metal's values already; a disabled stencil's faces have
  `enabled: 0`) with `dxmt_core::library::depth_stencil_state`. A `None` from it encodes handle 0 (Metal's default
  state), is logged once and is not cached (R2 M3). Without the depth no state is encoded. The rasterizer record
  takes `scole_scale: draw.slope_scale`, `depth_bias: 0.0`.
- `clear_depth_stencil`: nothing unless `depth_state()` is `Attached(f)`; `planes &= planar(f)`; nothing when that
  is 0 (H8); else `close_pass` and `emit(enc.clear_depth_stencil(&texture, key, 0, planes, z, stencil))` with
  `z = depth.max(0.0).min(1.0)` (a NaN becomes 0; R1 M-6).
- Fakes (new lines only; R2 I2, R3 M3): `MTLDevice_newDepthStencilState #<h> cmp=<n> write=<n>
  front=<en,cmp,sfail,dfail,pass,rm,wm> back=<..>`; in a pass, after the colour lines and only when that
  attachment's texture is non-zero, `  depth #<view> load=<n> store=<n> clear=<f>` and `  stencil #<view> load=<n>
  store=<n> clear=<n>`; after `  pso color0=<fmt>` a line `  pso depth=<fmt> stencil=<fmt>` when either is set. The
  state command, the slope and a sampler's compare function are already printed (`render 16 dsso=#<h>
  stencil_ref=<n>`, `render 12 ... scole_scale=<hex>`, the sampler line's `compare=`).

- [ ] **Step 1: failing tests** in `depth/tests.rs` (fixture helper `depth_desc(format, size, levels)`; passes are
  told apart by their depth lines, not by counts alone):
  `a_draw_with_no_depth_encodes_no_state`; `a_depth_is_attached_with_the_colour_target` (D32F_S8: both planes on
  one view, LOAD/STORE, `pso depth=260 stencil=260`, a `dsso` with the reference);
  `a_depth_only_format_attaches_no_stencil` (planar 1, `stencil=0`, the state's faces disabled though the snapshot
  enables stencil); `set_depth_takes_depth_textures_only` (an unknown id, a colour render target: false;
  `set_target` with a depth texture: false); `a_depth_change_closes_the_open_pass` and
  `a_target_change_keeps_the_depth`; `a_depth_of_another_size_is_detached` (larger and smaller: `Detached`, a key
  with its format `DrawRefused::Depth` and nothing emitted, a key with format 0 draws in a pass with no depth line,
  a Z clear records nothing); `a_detached_draw_between_two_attached_ones_is_three_passes` (same sizes, key format
  f, 0, f); `a_clear_then_a_draw_is_one_pass` (colour and Z|stencil clears, then a draw: one pass, colour and both
  planes loading their clear values); `a_clear_with_no_draw_is_a_pass_of_its_own`;
  `a_stencil_clear_of_a_depth_only_format_records_nothing`; `a_clear_depth_is_clamped` (1.5 → `clear=1`, NaN → 0);
  `forgetting_the_bound_depth_unbinds_it`; `a_depth_state_is_made_once_a_key`;
  `a_level_of_a_depth_texture_attaches_that_level` (the view's `mip=1`, the level's size);
  `the_slope_scale_reaches_the_rasterizer`; `a_zenable_change_in_a_pass_encodes_a_new_state`. Run `-p d3d9-rt depth`
  — Expected: do not compile (`set_depth` missing).
- [ ] **Step 2: implement** the fakes' lines, then the device.
- [ ] **Step 3: run** `-p d3d9-rt` and `-p dxmt-fakes` — pass; `sh tools/rust-check.sh` — ok.
- [ ] **Step 4: commit** `d3d9-rt: a current depth-stencil, its state and its Clear` (the front end passes
  `DepthStencilSnapshot::inert()`, reference 0, slope 0 until Task 4).

### Task 3: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s2/gates/{jq,jm,jcr,jp}.sh`.

**Interfaces (produces):**
- A mode `depth` (`d3d9_triangle depth [strict]`, 150 s budget, rows in the appendix) on **its own device**: `main`
  sets `pp.EnableAutoDepthStencil = TRUE`, `pp.AutoDepthStencilFormat = D3DFMT_D24S8` for this mode (and for
  `--bench ... tex depth`) alone; every other mode's device stays without one (H15).
- `depthRows()` reads `GetRenderState(D3DRS_ZENABLE)` and fetches the implicit depth surface **before** its loop
  (`ds-default` asserts the captured default), and after `reset()` each row runs `depthReset()`: `ZENABLE` TRUE,
  `ZWRITEENABLE` TRUE, `ZFUNC` LESSEQUAL, `STENCILENABLE` FALSE, the stencil functions and operations and their CCW
  twins at defaults, `STENCILREF` 0, both masks 0xFFFFFFFF, `TWOSIDEDSTENCILMODE` FALSE, `DEPTHBIAS` and
  `SLOPESCALEDEPTHBIAS` 0, stage 0 `MIPMAPLODBIAS` `GET1` then 0, `SetDepthStencilSurface(implicit)` (R3 I2).
- Every Z or stencil clear a row relies on is inside the retried body (each `frames()`/`until()` attempt), and
  **every "not drawn" assertion has a positive control** drawn by the same pipeline in the same pass (R3 I1: under
  mtld3d the first frames leave a draw out while its pipeline builds). Helpers: `clearAll(color, z = 1, stencil =
  0)`, `zquad(x0, y0, x1, y1, z, color)` (a pre-transformed coloured quad at depth `z`), reusing `until`, `scene`,
  `readTarget`, `want`, `makeRt`, `target`, `backToScreen`, `tquad`. The shared helpers draw at z 0.5 with Z on.
  No draw or Z clear runs between a `SetRenderTarget` and a `SetDepthStencilSurface` while the sizes differ,
  except in `ds-size`.
- Refusal sets (appendix): `depth_refusals` (five names); `texture_refusals` becomes the `LockRect` name alone;
  `fixed_refusals` becomes `IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a vertex sampler)`. No set
  becomes empty (the scripts miscount one, M §8.2).
- `tex-depthtex` becomes a drawn row (`D3D_OK`, pixel unasserted: a never-rendered depth texture). `tex-fetch4`
  becomes a drawn row over a 2x2 L8 texture of four distinct values, asserting that the red, green and blue
  channels hold three different texels (the order as mtld3d draws it, written into the row); a new row
  `tex-fetch4a` does the same over A8 (the alpha gather). `tex-vtxdepth` and `tex-vtxdepthfmt` stay refused rows.
- **Script edit points** (R3 I7): `dxmt/d3d9-check.sh` header (`:45-47`), the sets (`:137-143`), `refusals_ok`'s
  comment (`:130-135`), its outer pattern (`:149`) gaining `depth`, its inner case (`:151-152`) gaining explicit
  `targets)` **and** `depth)` arms (today's `*)` hands an unnamed mode the targets set), `launch ours fresh depth
  240 depth $strict` and `check depth "<rows>"` **before** the mtld3d-lane refusal grep (`:208`), the `textures`
  row list gaining `tex-fetch4a`; `vm/dxmt/d3d9-gate.sh` header (`:26-37`), `runtime()`'s comment (`:169-173`), the
  sets (`:174-181`), the outer pattern (`:188`) and inner arms (`:189-191`), a `dp64-fresh dp32-fresh` row loop and
  a runtime check with `depth`; `vm/tests/d3d9.sh` header (`:17-18`) and two `tri` lines after `tg32-fresh`
  (`:106`). Gates: `jq.sh`'s summary grep (done), `jcr.sh` timing `dp64`/`dp32` against the 150 s budget, `jm.sh`
  letters `a b c d e f`, six SHAs in `mutants.txt`, the pairs of Task 6, and a line per mutant failing unless every
  item of its `d3d9-check: FAIL` line starts with `depth` (R3 I6); `jp.sh` gains the `tex depth` runs.

- [ ] **Step 1: write the rows**, the script lists, header comments and counts.
- [ ] **Step 2: syntax** with the x86_64, i686 and aarch64 `*-w64-mingw32-clang++ -std=c++17 -fsyntax-only -Wall
  -Wextra` — Expected: no diagnostics.
- [ ] **Step 3: commit** `dxmt, vm: d3d9_triangle depth rows in every D3D9 lane (DXMTRS C4b-3d2)`.
- [ ] **Step 4: rows first, on mtld3d** (job `jr`: plain build at Task 2's commit, `d3d9-check` default and msl;
  job `jcr`: the VM default lanes, old and new WoW64) — Expected: every `depth` row ok in each lane, `tex-fetch4`
  and `tex-fetch4a` ok. A row mtld3d fails is ruled on (fixed if the row is wrong; dropped with its limit named if
  mtld3d is), ledgered, recommitted.
- [ ] **Step 5: RED under core** (job `jq-red`: core build at Task 2's commit) — Expected: the `depth` launch fails
  at `CreateDevice` (8876086a: no row line, the runtime set red); `tex-depthtex`, `tex-fetch4`, `tex-fetch4a` FAIL
  (refused); `textures-runtime` and `fixed-runtime` FAIL (the sets); the other launches unchanged.

### Task 4: the front end's depth-stencil (attachment, state, Clear)

**Files:** Modify `src/d3d9/src/direct3d9.rs` (`create_core_device`), `src/d3d9/src/device.rs`
(`device_create_depth_stencil_surface`, `device_set_depth_stencil_surface`'s core arm, the auto-depth predicate,
`device_clear`'s call at `:11234`), `src/d3d9/src/device/core_arms.rs` (`clear`, which gains `z` and `stencil`),
`src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/surface.rs`, `src/d3d9/src/texture.rs` (`core_flush_dirty`,
`core_refuses_lock`), `src/d3d9/core/src/pipeline_key.rs` (+ tests).

**Interfaces:** consumes Tasks 1-2.
- **"Bound" without a Metal handle (H1; R1's per-reader table).** `DeviceInner` gains a cfg'd
  `core_auto_depth: Option<TextureId>` and one predicate `has_auto_depth()` (`!depth_stencil_handle.is_null()`, or
  under core `core_auto_depth.is_some()`), read by `depth_stencil_bound()` (`device.rs:3881`) and
  `get_or_create_implicit_depth_stencil` (`:4111`). `depth_stencil_has_stencil()` and the implicit surface's
  `live_format()` read `depth_stencil_format`, which the core device now sets. The other readers of the handle are
  not reached under core (`device.rs:1803`, `:1952`, `:2730`, `:3319-3465`, `:4078`, `:4577`, `:5926-6094`,
  `:13730`; `surface.rs:1487`, `:1980`).
- `create_core_device`: the first refusal tests `windowed == 0 || multi_sample_type != 0` only. With
  `enable_auto_depth_stencil != 0 && auto_depth_stencil_format != 0` (format 0 is "none", as mtld3d): a core
  texture under a new id (`render_target: true`, `map_d3d_depth_format`'s format, the back buffer's size, one
  level), `core.set_depth(Some(..))`, `core_auto_depth`, `depth_stencil_format = pp.auto_depth_stencil_format`.
  `D3DRS_ZENABLE` defaults to TRUE exactly when it exists (mtld3d's rule, M §1.2).
- `Direct3DSurface9::core_depth(&self) -> Option<DepthRef>` (core-runtime): the device's `core_auto_depth` for the
  implicit depth-stencil; `{ id: core_texture, level: 0 }` for a standalone depth-stencil holding a core id;
  `{ parent's id, mip_level }` for a level of a depth-format texture with `D3DUSAGE_DEPTHSTENCIL`; `None`
  otherwise. `core_target()` answers `None` for a standalone surface whose usage is `D3DUSAGE_DEPTHSTENCIL`.
- `CreateDepthStencilSurface` under core: mtld3d's checks in mtld3d's order with mtld3d's answers (M §2.1 steps
  2-6), a sample count above 1 refused as `IDirect3DDevice9::CreateDepthStencilSurface (multisampled)`, an extent
  past `texture_extent_fits` answered `D3DERR_INVALIDCALL` (3d1's review finding C1), then `texture_create` and
  `new_depth_stencil` with no Metal handle plus `set_core_texture`; registered as 3d1's `core_color_target` is.
- `core_flush_dirty`: a depth-format texture gets its core texture (`render_target: true`, its Metal depth format,
  its levels) and never uploads. `core_refuses_lock` gains one clause: a depth-format texture with
  `D3DUSAGE_DYNAMIC` (its packed upload is C4c's); everything else answers as today (R1 M-1, R3 M10).
- `SetDepthStencilSurface` core arm (after the existing validation): NULL unbinds (`core.set_depth(None)`, the
  binding and `DEPTH_EXPLICITLY_UNBOUND` as today). A surface: one of another device, or with no `core_depth()`,
  is refused as `IDirect3DDevice9::SetDepthStencilSurface (a surface the core does not attach)` (R1 M-7); the
  parent texture is flushed so its core texture exists; `core.set_depth` (false: the same refusal); then
  `replace_depth_stencil` and the flag cleared. Always `D3D_OK` for a surface the core attaches, whatever its size.
- **The draw's depth decision** (mtld3d-core, unit-tested):
  `pub const fn core_key_depth(bound: Option<u32>, attachable: bool, samples_bound: bool, uses_plane: bool) ->
  Result<u32, CoreRefusal>`: no depth → `Ok(0)`; attachable and not sampled → `Ok(format)`; otherwise (detached,
  or sampled by the draw) `Ok(0)` when `!uses_plane`, else `Err(DepthSize)` for a detached one and
  `Err(BoundDepthSampled)` for a sampled one (the latter reachable from Task 5). `uses_plane` is
  `gated.uses_depth() || gated.stencil_enable != 0` for the render states' snapshot gated on the format's stencil
  plane. Names: `IDirect3DDevice9::Draw*PrimitiveUP (a depth-stencil of another size than the target)`,
  `... (the depth-stencil sampled while bound)`.
- `core_draw::resolve`: `core.depth_state()` once; the key's `depth_format` from `core_key_depth`;
  `has_depth = depth_format != 0` feeds `core_ps_variant` and `pos_fixup(.., rhw_depth_clamp(rhw, has_depth,
  rs[ZENABLE] != 0), d3d_depth_bias_to_clip(rs[DEPTHBIAS], znear, zfar))` with `viewport_depth_range`'s range.
  `DrawData`: `depth_stencil: snapshot_from_state(&rs)`, `stencil_ref: low byte of rs[STENCILREF]`,
  `slope_scale: d3d_slope_scale_to_metal(rs[SLOPESCALEDEPTHBIAS], 1.0)`. `DrawRefused::Depth` maps to the
  `DepthSize` refusal.
- `core_arms::clear(this, rects, flags, color, z, stencil)`, in this order: Z or stencil flags with
  `!dev.depth_stencil_bound()` → `D3DERR_INVALIDCALL` (as today, no refusal line); rects or scissor → today's
  refusal; no colour target → `D3D_OK`; a viewport not covering → today's refusal; Z or stencil flags with
  `core.depth_state()` `Detached` → refused as `IDirect3DDevice9::Clear (a depth-stencil of another size than the
  target)`, nothing cleared; the colour clear; then `core.clear_depth_stencil(planes, z, stencil & 0xFF)` with
  `planes` from the two flags (rt masks them by format: `D3DCLEAR_STENCIL` on a depth-only format is D3D9's silent
  no-op, as mtld3d has it).

- [ ] **Step 1: failing tests** (mtld3d-core): `core_key_depth_covers_every_case` (the eight-row table above);
  `core_draw_kinds_cover_every_case`'s exhaustive match names `DepthSize` — Expected: does not compile. The
  behavioural RED is Task 3 Step 5.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** the tests; `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: a core device has a depth-stencil: the auto one, surfaces, textures, state, Clear`.
- [ ] **Step 5: GREEN under core, part A** (job `jq-a`) — Expected, `depth` strict: every row ok but `ds-shadow`,
  `ds-intz`, `ds-df`, `ds-fetch4` and `ds-feedback` (`DrawPrimitiveUP` 8876086a: sampling is still refused under
  its old name) and `depth-runtime` (five lines against five names, one differing: `(a depth texture sampled)` in
  place of `(the depth-stencil sampled while bound)`); `textures`: `tex-depthtex`, `tex-fetch4`, `tex-fetch4a` and
  `textures-runtime` FAIL; `fixed-runtime` FAIL (the old name); the other launches as before.

### Task 5: depth sampling and fetch4

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ tests), `src/d3d9/rt/src/device/samplers.rs` (+ tests),
`src/d3d9/rt/src/device/depth/tests.rs`, `src/d3d9/src/device/core_draw.rs`.

**Interfaces:**
- `core_texture_variant(variant, masks: &SampledMasks, lod_table)` with `pub struct SampledMasks { pub volume: u16,
  pub cube: u16, pub depth: u16, pub depth_fetch: u16, pub raw_depth_red: u16, pub fetch4: u16, pub fetch4_alpha:
  u16 }` (mtld3d's fold, M §5.1, from `StageBindings`, masked as volume and cube are today).
- rt `samplers.rs`: `compare_function: WMT_COMPARE_FUNCTION_LESS_EQUAL` when the description has `IS_COMPARE`
  (M §5.3), else 0.
- `core_draw`: per sampled pixel slot `is_compare = depth bit && !fetch bit` → `snapshot_from_state(&states,
  is_compare)`; a fetch slot's snapshot takes `force_point_filter()`; a depth texture binds in its stored format
  (R1 and R2: a legal sampled view; no `R32X8X32` view). A depth slot never falls back to the black bind
  (`debug_assert`; every depth texture has a core form since Task 4; R2 M6). `samples_bound` for `core_key_depth`:
  a sampled pixel slot's texture id equals `core.depth()`'s id, whatever the level. The tracker cannot see a pass
  sampling its own depth (the write is recorded at `end_pass`): the front end's decision is the only guard (R2 M5).
- Refusals: `CoreRefusal::Fetch4` and `DrawFlags::FETCH4` deleted. `DepthTexture` becomes `VertexDepthTexture`
  (`... (a depth texture at a vertex sampler)`, flag `VERTEX_DEPTH_TEXTURE`, the vertex-slot fold alone).
  `BoundDepthSampled` comes from `core_key_depth`. Order in `resolve`: `core_draw_kinds` (Blending, Scissor,
  VertexDepthTexture, the shader checks), then `core_key_depth`.

- [ ] **Step 1: failing tests**: mtld3d-core `core_texture_variant_carries_the_depth_and_fetch4_masks`, the
  `core_draw_kinds` cases and exhaustive match (`VertexDepthTexture`; no `Fetch4`, `DepthTexture`); d3d9-rt
  `a_compare_sampler_is_less_equal` — Expected: do not compile / fail on `compare=`. A **characterization** test
  (expected green at once, it exercises Task 2's passes and the core's tracker; R2 I1, R3 M12):
  `a_draw_sampling_the_bound_depth_detaches_it`: a draw to (X, T), a detached draw to X sampling T, a draw to
  (X, T): three render encoders in order, T's view bound in its stored format, and the second carrying a
  `WAIT_FOR_FENCE` on the fence the first updates.
- [ ] **Step 2: implement.** **Step 3: run**; `sh tools/rust-check.sh` — ok.
- [ ] **Step 4: commit** `d3d9: a core device samples depth textures and takes fetch4`.
- [ ] **Step 5: GREEN under core** (job `jq`) — Expected: `depth` every row strict, `textures` and `fixed` every row,
  `targets`, `state` and `shaders` as before, each refusal set exact. Then, in the same job, the `depth` launch once
  more under core with `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog`, counted as `dxmt/check.sh`'s
  `invalid()` does (`:105-112`, `:312-314`) — Expected: the layer on, 0 reports.

### Task 6: gates, probe and one measurement

- [ ] **jb** (core build at the tip): the stateblock conformance under core equals default; `d3d9-check` core and
  default ok.
- [ ] **jm** mutation REDs, one build each, the named row red and every failing item in the `depth` launch:
  (a) `depth_state()` answers `None` for a bound depth → `ds-test` (pass, pipeline and state agree on "no depth":
  a clean red, R3 I5); (b) the `dsso` command's `stencil_ref` 0 → `ds-stencil`; (c) `clear_depth_stencil` clears
  to 1.0 whatever `depth` → `ds-clearz`; (d) `pos_fixup`'s bias lane 0 → `ds-bias`; (e) the depth view's
  `first_mip` 0 → `ds-mip` (its level-0 probe); (f) the compare sampler's function 0 → `ds-shadow`.
- [ ] **ja** (plain build at the tip): translator key and imports unchanged; `d3d9-check` default and msl ok; Wine's
  conformance on the default runtime as C4b-3d1's.
- [ ] **jc** (VM): `vm/dxmt/d3d9-gate.sh` in the four lanes — Expected: ok.
- [ ] **jp** probe: `--bench 2000 <0|600> tex` and `--bench 2000 <0|600> tex depth` (the same draws on a device
  with the auto D24S8, Z on) on mtld3d and core, once each (`build/c4b3d/s2/probe.md`): the first beside 3d1's
  2.57 against 1.90 ms/frame, the second the cost of the depth path every title takes (R3 M13).
- [ ] **jw** measurement, not a gate (ruling C4b3d2-11), after `ja` (its `run-default.txt` is the base): a core
  build, then the conformance runner directly from `jw.sh` (the wrapper's `runner()` cargo line with `--only
  visual`, the core lane's `DXMT_CONFIG`, `--log info`, `MTLD3D_CONFORMANCE_TIMEOUT_SECS=1800`, its own output file
  `build/c4b3d/s2/gates/jw/run-core-visual.txt`), then `d3d9-conf-compare.sh --base-lane default --cur-lane core
  --suites visual`. The wrapper script is not changed. Recorded in `build/c4b3d/s2/wine-visual.md`: completes,
  crashes or times out (a validation report is a crash there), failing sites against the default lane, the refusal
  names logged by count. Input for 3d3-3d5's order; nothing in it blocks the push.

### Task 7: review and push

- [ ] Whole-branch review by a fresh Opus reviewer (the package, this plan, the spec, Review Focus verbatim, the
  ledger's rulings); one fix pass for Critical and Important findings, each RED then GREEN.
- [ ] Spec §5 paragraph, pin, privacy scan, push DXMTRS `c4b3d` and MacNeutron `c4b3d-mn` to their `main`s
  (fast-forward), post-push check (plain build from main, tests, x64 `dxmt/check.sh` on an idle Mac), memory, this
  slice's `benchwork`/`work` folders deleted.

## Rules check

- One fact (`depth_state()`) and one decision (`core_key_depth`, unit-tested) feed the key; the key feeds the pass,
  the pipeline, the variant, the state and the clamp in rt. A new attachment rule (3d3's sizes, 3d5's sample counts,
  mtld3d's sample-count detach) changes `depth_state()` alone.
- No per-draw allocation is added, and one texture-store look-up a draw with a depth bound (`depth_state()`): the
  state is a map look-up by a 64-bit key; the snapshot is 20 bytes by value.
- Front-end additions are core arms beside mtld3d's code; mtld3d's paths gain one predicate (`has_auto_depth`) whose
  value for a non-core device is today's expression.
- mtld3d-core's `depth_stencil_state`, `fetch4`, `sampler_state` and airconv's lowering are used as they are.

## Rulings

- **C4b3d2-1 (a new depth-stencil holds depth 0.0, stencil 0).** The core's initializer zeroes it, as it does a
  D3D11 depth texture; D3D9 leaves the contents undefined, mtld3d's Metal texture is uninitialised, and Wine's
  `test_depth_stencil_init` (a z = 0 quad under LESSEQUAL on a never-cleared surface) passes with 0.0. Rows clear Z
  themselves. Cost if wrong: a title that never clears Z draws nothing under the default `LESSEQUAL` where a
  driver that starts at 1.0 would have drawn.
- **C4b3d2-2 (another size than the colour target: detached; a draw or Clear that needs it is refused).** D3D9
  allows a larger depth-stencil. A whole-target D3D9 Clear of one is a partial clear of the depth (3d3's rect
  path), and the core's `clear_into_render` would fold an earlier whole-depth clear into a smaller pass and clear
  only the pass's area (H9; the guard is 3d3's by C4b3d-1); R2 traced that no pass attaching a depth of another
  size means no such fold. So the depth is not attached while the sizes differ: a draw that tests or writes no
  plane draws (half-resolution post-processing, bloom and mip-chain passes with the auto depth left bound; R1
  I-1), a draw that does, and a Clear with Z or stencil, refuse. A smaller one is undefined in D3D9 and takes the
  same rule. Cost if wrong: **a depth-tested draw to a render target smaller than the bound depth-stencil (shadow
  maps drawn against the auto depth, a default `ZENABLE` left on for a post-process quad) draws nothing under core
  until 3d3.**
- **C4b3d2-3 (the depth-stencil sampled while bound: detached for a draw that uses no plane, else refused; C4c).**
  Metal forbids sampling an attachment and the core has no read-only depth route D3D9 could use; mtld3d copies the
  attachment (map2-passes §4, about 80 lines over the depth copies that are C4c's). A lighting pass that reads
  INTZ scene depth with Z and stencil off needs no copy: it draws in a pass without the depth (R1 I-4, the
  detached draw this slice has anyway). Cost if wrong: a draw that depth-tests against the depth-stencil it
  samples (soft particles drawn with Z on) draws nothing under core until C4c; each detached draw between attached
  ones splits the pass (a load and a store of colour and depth).
- **C4b3d2-4 (depth textures at vertex samplers stay refused; C4c).** The core's vertex sampler kinds have no depth
  kind (H13). Its refusal gets a name of its own so the strict sets stay exact. Cost if wrong: none known.
- **C4b3d2-5 (honest LOAD/STORE; the economy rules are C4d's).** C4b3d-1 left DONT_CARE planes, `drop_dead_store`
  and the later store in `same_targets` to this slice to take or rule out: ruled out. Each is a bandwidth saving on
  a correct pass, and each is a dxmt-core change D3D11 would inherit (R2: `take_targets` and `drop_cleared_stores`
  lose economy only here). `map2-passes.md` holds no other remainder for depth. Cost if wrong: a depth store and
  load a pass; measured in C4d.
- **C4b3d2-6 (no dxmt-core change).** Attachment, Clear, the state object and the initializer are used as D3D11
  uses them (R2 confirmed each). Cost if wrong: a core defect found here is fixed in dxmt-core with a D3D11 gate
  run added.
- **C4b3d2-7 (dynamic depth textures: created, sampled, their lock refused).** They have no `DEPTHSTENCIL` usage,
  so they are never attached; the packed upload is C4c's (worklist row 24), so under core one only ever holds
  zeros. The `depth.aliasSameSize` carry is not applied under core (row 19). Cost if wrong: a title that locks a
  dynamic depth texture gets `D3DERR_NOTAVAILABLE`.
- **C4b3d2-8 (a Clear's z is clamped to [0, 1], NaN to 0).** Metal's clear depth must lie there; mtld3d passes the
  bits through. Cost if wrong: none (the buffer cannot hold another value).
- **C4b3d2-9 (`D3DZB_USEW` reads as TRUE; depth formats are the two float formats).** As mtld3d (H7). Cost if
  wrong: as mtld3d's.
- **C4b3d2-10 (a sixth launch).** `depth` needs its own device, about 20 s a lane. Cost if wrong: gate time.
- **C4b3d2-11 (Wine's `visual` under core is measured once, not gated).** Its proof fails on any refusal line and
  the suite meets blending, scissor, MRT, MSAA, rect clears, locks and `StretchRect` (H14). Cost if wrong: a crash
  under core in a Wine test goes unseen until 3d5's sweep; the measurement is there to see it now.
- **C4b3d2-12 (depth-stencil state objects live as long as the device).** As the pipelines; bounded by the distinct
  states an application sets. Cost if wrong: memory in a title that sweeps stencil masks.
- **C4b3d2-13 (no video-memory accounting for core depth surfaces).** As 3d1's colour targets;
  `GetAvailableTextureMem` is refused under core. Cost if wrong: none today.
- **C4b3d2-14 (`tex-depthtex`'s pixel stays unasserted).** It samples a never-rendered depth texture. The defined
  cases are the `depth` mode's sampling rows. Cost if wrong: none.
- **C4b3d2-15 (a mtld3d defect seen, not fixed here).** mtld3d's `depth_stencil_has_stencil` reads a standalone
  format, 0 for a texture's level, so `Clear(STENCIL)` on a bound D24S8 or INTZ texture level is dropped under
  mtld3d (R1 M-2); the core clears it. No row asserts stencil on a texture-backed depth, so the lanes agree on
  every row. The default runtime is not changed by this slice. Cost if wrong: a divergence between the runtimes
  with the core the correct one.

## Stated gaps (R3 M14)

No row or test for: `EnableAutoDepthStencil` with format 0 (needs a third device); the fetch4 mask's interaction
with a non-point mag filter; depth sampling through a programmable pixel shader's `texldl`/`texldd` forms; a
vertex-stage depth bias other than the one programmable case in `ds-bias`.

## Appendix: the rows (`d3d9_triangle depth`)

Device: 256x256 X8R8G8B8, auto D24S8. Unless said, a row clears colour, Z = 1 and stencil = 0 inside its retried
body, draws pre-transformed coloured quads and reads the back buffer. Depth textures a row samples, attaches or
feeds back are 256x256.

| row | what it does | asserts |
|---|---|---|
| `ds-default` | `GetDepthStencilSurface`, its desc; the `ZENABLE` read before the first reset | D24S8, 256x256, `D3DUSAGE_DEPTHSTENCIL`; TRUE |
| `ds-test` | red at z 0.25, then green at 0.75 over half of it and beyond | overlap red, beyond green |
| `ds-func` | `ZFUNC` NEVER, LESS, EQUAL (the same vertices drawn twice), GREATER, ALWAYS against a quad at 0.5, each beside a control | each function's result |
| `ds-write` | `ZWRITEENABLE` off for the near quad, then a far quad | far over near; a control |
| `ds-zenable` | in one scene: tested draw, `ZENABLE` off draw behind it, tested draw | the untested draw shows and wrote no depth |
| `ds-clearz` | `Clear(ZBUFFER, 0.5)`; quads at 0.25 and 0.75 | near drawn, far not |
| `ds-clearonly` | surface A bound and cleared to 0.25, surface B bound, cleared and drawn, A rebound with no clear: quads at 0.2 and 0.5 | 0.2 shows, 0.5 hidden |
| `ds-stencil` | `Clear(STENCIL, 1)`; EQUAL with ref 1, ref 2, ref 0x101; REPLACE then EQUAL; INCR; read and write masks; ref back to 0 | each case, with a control |
| `ds-twosided` | `TWOSIDEDSTENCILMODE`, `CULLMODE` NONE: CW and CCW quads with different pass ops, then EQUAL tests | each face's op |
| `ds-bias` | coplanar quads under LESS: without bias, with `DEPTHBIAS` -0.01; the same through a programmable vertex shader | drawn only with bias, both ways |
| `ds-slope` | a quad slanted in z drawn twice under LESS, the second with `SLOPESCALEDEPTHBIAS` -1 | drawn only with the slope term |
| `ds-clamp` | a pre-transformed quad at z 1.5: `ZENABLE` off, then on (with a control at 0.5) | drawn (clamped), then clipped |
| `ds-odepth` | a pixel shader writing `oDepth` 0.25 on a quad at 0.75, then a quad at 0.5 half over it | hidden where over it, drawn beside it |
| `ds-surface` | `CreateDepthStencilSurface` D24X8 and D16, 256x256, bound: the depth test; desc | as `ds-test`; formats echo |
| `ds-nostencil` | D24X8 bound: `STENCILENABLE` with NEVER; `Clear(ZBUFFER, 0.5)` then `Clear(STENCIL)` alone; quads at 0.4 and 0.6 | drawn; `D3D_OK`, colour kept; 0.4 shows, 0.6 hidden |
| `ds-unbound` | `SetDepthStencilSurface(NULL)`: `Clear(ZBUFFER)`, `Get`, a far-after-near draw order, a quad at z 1.5 with `ZENABLE` on; rebind the implicit | `INVALIDCALL`; `NOTFOUND`; untested; clamped and drawn; tested again |
| `ds-persist` | Z written with the back buffer bound, a 256x256 render-target texture bound, a quad behind it half over the written area | hidden half, drawn half |
| `ds-texture` | a D24S8 `DEPTHSTENCIL` texture's level 0 bound | as `ds-test` |
| `ds-mip` | a two-level depth texture: level 0 with a 256 target cleared to 0.25; level 1 with a 128 target cleared to 1 and the `ds-test` pair, a corner left untouched; level 0 rebound with no clear, a 0.4 quad over that corner and a 0.2 control | `ds-test` at 128; 0.4 hidden, 0.2 shows |
| `ds-release` | a depth surface bound, released by the application, drawn against | still tests |
| `ds-shadow` | depth written into a D24X8 texture (left half 0.25, right 0.75); sampled by `tquad`s with texcoord z 0.5 at texel centres away from the boundary | left unlit, right lit |
| `ds-intz` | the same into an INTZ texture, sampled raw; again with LINEAR filters a quarter texel off the boundary | grey of each depth; the texel's own depth (point forced) |
| `ds-df` | DF16 and DF24, sampled raw | red of each depth, green and blue 0 |
| `ds-fetch4` | DF24 with `GET4` and point mag filter, constant depth | the depth in every channel |
| `ds-size` | a 512x512 depth surface with the 256 target: a draw with Z and stencil off, a draw with Z on, `Clear(ZBUFFER)`, `Clear(TARGET)` | the Z-off draw and the colour clear in both runtimes; strict: the Z-on draw and the Z clear refused; mtld3d: `D3D_OK` and the depth tests |
| `ds-feedback` | the bound INTZ depth at stage 0: sampled with Z and stencil off; sampled with Z on | the depth's grey in both runtimes; strict: the Z-on draw refused; mtld3d: `D3D_OK` |
| `ds-msaa` | `CreateDepthStencilSurface` with 4 samples | strict: refused; mtld3d: any answer, a surface made is released |
| `ds-huge` | `CreateDepthStencilSurface(16385, 16)`, of zero width, of a colour format | `D3DERR_INVALIDCALL` in both |
| `ds-lock` | a dynamic D16 texture's `LockRect` (unlocked only on success) | strict: refused; mtld3d: `D3D_OK` |

`depth_refusals`: `IDirect3DDevice9::Draw*PrimitiveUP (a depth-stencil of another size than the target)`,
`IDirect3DDevice9::Clear (a depth-stencil of another size than the target)`,
`IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)`,
`IDirect3DDevice9::CreateDepthStencilSurface (multisampled)`,
`IDirect3DTexture9::LockRect (a render-target or depth texture)`.

## Review dispositions

- **R1 (design; no Critical).** I-1 taken: the detached draw, ruling C4b3d2-2 rewritten. I-2 taken: `ds-clearonly`
  no longer crosses a Present (mtld3d discards an unsampled depth there). I-3 taken with R3 C1 (`ds-mip`'s level-0
  probe). I-4 taken: C4b3d2-3 rewritten. M-1, M-3, M-5, M-6, M-7, M-8, M-11 taken; M-2 ledgered as C4b3d2-15; M-4
  taken with R3 C2 (`jw` runs the runner itself). Not verified by R1 and left to the rows: state blocks with
  `ZENABLE` defaulting TRUE, a fixed-function stage sampling a depth texture (`ds-shadow` uses one), the
  pre-transformed program's clamp lane (`ds-clamp`).
- **R2 (core; no Critical).** I1 taken (the three-pass shape and the fence wait, as a characterization test). I2
  taken (existing lines kept, new pass lines only when attached, the initializer's pass filtered). M1-M6 taken.
- **R3 (execution; two Critical).** C1 taken (`ds-mip`). C2 taken (`jw.sh` runs the runner, 1800 s, after `ja`).
  I1-I7 taken (controls, `depthReset`, handles 0x2e0-0x2ff, the validation-layer launch, mutant (a), `jm.sh`, the
  script edit points). M1-M10, M12, M13, M15 taken; M11: the cut point is dropped; M14: three cases added to rows
  and one rt test, the rest in "Stated gaps"; M16 (the guest's row counter skips hyphenated names) stands, as for
  `rt-*`.

## Execution notes (2026-11-13)

- Built as planned in four commits on DXMTRS (0e9e4bfd the key and the lanes, cb8f73a1 the runtime, aa44e671 the
  front end, 63f9fda0 sampling); the rows are 29. Under core the depth launch was refused at CreateDevice before
  Task 4, passed 24 of 29 strict after it (the five sampling rows left) and 29 of 29 after Task 5, with no report
  under the Metal validation layer.
- Changes from the plan's text, each ledgered: `CoreDevice` keeps `open_depth: Option<DepthRef>` where the plan had a
  bool (clippy's limit of three bools in a struct); the draw tests' helpers moved to `fixture.rs`; `core_key_depth`
  alone decides (no `DEPTH_SIZE` draw flag); a depth texture's block is `(1, 1, 4)` (it is never uploaded or read
  back); `SampledMasks` derives `Debug, Default` only; the cut point is dropped; the sampled-while-bound rt test is a
  characterization test on a second colour target.
- Incident: `gates/jq.sh` was edited while a chain was executing it (sh reads a script as it runs); the original
  bytes were restored 27 s later, before that job's subshell ended, and the run finished cleanly. Rule since: a
  changed job gets a new file name.
- Gates: the six mutants each red on their named row, in no other launch; stateblock under core equal to default; the
  plain build's translator key and imports unchanged; the VM's core lanes ok on old and new WoW64. The VM's default
  lanes first failed on executed-test floors only (`visual` 60 short in three of four legs, `d3d9ex` 1 short in one:
  `test_map_synchronisation`'s timing skip on a Mac in use) and passed in full on the rerun at the same commits.
- Probe (`build/c4b3d/s2/probe.md`): inconclusive. Single runs did not repeat on a Mac in use (identical code 0.5-0.7
  ms a frame apart), so C4b-3d1's PE side and this slice's also ran interleaved over five rounds, against the one-run
  rule. Two comparisons read the tip higher, the interleaved one lower: no regression is measurable, the direction
  is unknown. C4b-3d3's probe retakes it on a quiet Mac, each tip on its own full build.
- Wine's `visual` under core (ruling C4b3d2-11, a measurement): the device is created, and the suite stops at
  `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)`, which the core does not implement.
- Final review: no Critical, no Important, 11 Minor, all deferred (the ledger and the close-out list them). Two are
  inputs to C4b-3d3: the pre-transformed clamp follows the key, so a Z-on, write-off, ALWAYS draw whose depth-stencil
  is left out is drawn past the far plane where mtld3d clips; `Clear(STENCIL)` of a depth-only depth-stencil of
  another size is refused where mtld3d does nothing.
