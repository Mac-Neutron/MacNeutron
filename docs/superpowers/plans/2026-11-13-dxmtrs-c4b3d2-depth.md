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
everything a draw or Clear does with depth: `CoreDevice::depth_format()`**, read once by the front end as
`target_desc()` is: `Ok(WMT_PIXEL_FORMAT_INVALID)` for none bound, `Ok(Depth32Float | Depth32Float_Stencil8)` for an
attached one, `Err(DepthMismatch)` for one whose size differs from the colour target's (refused, 3d3 lifts it). The
pass's planar flags, the pipeline's depth and stencil formats, `NO_DEPTH_ATTACHMENT`, whether a depth-stencil state is
emitted and its stencil gating, the pre-transformed clamp lane and a Clear's planes all derive from it. A depth
texture is a core texture made on the render-target path (render-target usage, per-subresource tracking, zeroed by
the initializer), so a depth surface, a depth texture's level and the auto depth-stencil are one kind of thing. The
state comes from mtld3d-core's `depth_stencil_state` (already Metal's enums). Sampling reuses mtld3d's variant masks
and the airconv lowering unchanged. Nothing changes for the default runtime.

**Tech Stack:** Rust 1.99.0 (DXMTRS: mtld3d-core, d3d9-rt, d3d9.dll front end, dxmt-fakes), C++ (`d3d9_triangle`), sh
gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line and 3d1's paragraph). Maps:
`build/c4b3d/map-3d2.md` (this slice; file:line and excerpts for every claim below, cited as M §n and hazards as
H n), `build/c4b3d/map.md` (all of C4b-3d), `build/c4/map2-passes.md` (the pass rules). Reviews:
`build/c4b3d/s2/review-{1-design,2-core,3-exec}.md`.

## Global Constraints

- The default runtime (mtld3d) is unchanged: translator key `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9`
  and the plain d3d9.dll imports unchanged; every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]`
  and a runtime `is_core()` check; a core-only struct field is cfg'd in the struct and in every literal. Shared
  mtld3d-core functions keep mtld3d's behaviour (its unit tests pass unchanged).
- `tools/rust-check.sh` passes after every DXMTRS commit; a d3d9-rt or mtld3d-core API change carries its front-end
  call sites in the same commit. New files are `git add`ed before it; a new Clone/Copy derive needs
  `sh tools/audit.sh --update-derives`.
- No new dependency. No `HashMap` (FxHashMap). No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the
  body; lines of 100 columns at most.
- **No dxmt-core change** (ruling C4b3d2-6): D3D10-12 are untouched.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`) and `build/c4b3c/mn` (branch `c4b3d-mn`), cargo target
  `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s2/gates` (copied from `s1/gates`, `S=build/c4b3d/s2`,
  state names `c4b3d2-*`), logs and reviews in `build/c4b3d/s2`. Builds and GPU jobs under the pipeline lock, `$D`
  and the main checkout restored by each job's trap. No make in a worktree. A job tests **committed** tips.
- Commands: as C4b-3d1's plan (`. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; `cargo test --offline --target aarch64-apple-darwin
  -p <crate>`, d3d9-rt with the `dynamic_lookup` rustflags; mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`).
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses
  (0xe0-0xef here).
- Every DXMTRS commit is safe to run under core: a refusal is lifted only in the commit that makes its calls correct.
  MacNeutron's core lanes are red between Task 3's rows and Task 7's pin by design.
- dxmt-fakes is read only by d3d9-rt's tests and its own: a new fake line is printed after the existing line it
  belongs to, and no existing line's text changes.
- Benches and probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents
  too) while the x64 `dxmt/check.sh` runs.

## Scope

In: the creation gate for `EnableAutoDepthStencil` (windowed, one sample); `CreateDepthStencilSurface` (one sample);
`CreateTexture` of a depth format as an attachment (every level) and as a sampled texture at pixel slots;
`SetDepthStencilSurface`, `GetDepthStencilSurface`; `D3DRS_ZENABLE`, `ZWRITEENABLE`, `ZFUNC`, the stencil states
(two-sided included), `STENCILREF` and masks, `DEPTHBIAS`, `SLOPESCALEDEPTHBIAS`; the pre-transformed depth clamp
rule; a pixel shader's depth output; `Clear` with `D3DCLEAR_ZBUFFER`/`D3DCLEAR_STENCIL` over the whole target;
fetch4 on every eligible format.

Out (owners): a depth-stencil of another size than the colour target, and with it rect and partial-viewport depth
clears and `clear_into_render`'s area guard (3d3); multisampled depth surfaces and RESZ (3d5); sampling the
depth-stencil while it is attached, depth textures at vertex samplers, a dynamic depth texture's lock and upload,
the `depth.aliasSameSize` carry, `Reset`, render scale, fullscreen (C4c); the pass map's economy rules (DONT_CARE
planes, `drop_dead_store`, the later store in `same_targets`) and per-draw state dedup (C4d); the 1x1 "RT0 drop"
(not planned: mtld3d's special case for one title shape, M §2.7).

## Review Focus

1. **A pass and its pipeline agree on the planes**, for each of: no depth bound, depth explicitly unbound
   (`SetDepthStencilSurface(NULL)`), a depth-only format, a combined format, a colour-target change with the depth
   kept, a depth change with the colour kept. A disagreement is a draw Metal drops without an error (rt's
   `debug_assert` on the key; rows `ds-test`, `ds-surface`, `ds-unbound`, `ds-persist`).
2. **Stencil against a format without the plane**: `STENCILENABLE` on D24X8/D16 draws untested and unwritten, and
   `Clear(STENCIL)` alone on it records nothing (H8: unmasked it would run as a colour clear) (`ds-nostencil`; rt
   `a_stencil_clear_of_a_depth_only_format_records_nothing`).
3. **A Clear with no draw after it still lands, and Clear then draw is one pass** whose depth load action is the
   clear (`ds-clearonly`; rt `a_clear_then_a_draw_is_one_pass`), and a Z clear of a depth bound with a target of
   another size is refused, not folded into a smaller pass (H9; `ds-size`).
4. **State changes inside one pass**: a draw with `ZENABLE` off after one with it on gets a state that neither
   tests nor writes; the stencil reference follows every change, back to 0 included (`ds-zenable`, `ds-stencil`).
5. **Lifetime and order**: a depth texture written by one pass and sampled by the next is not reordered or merged
   past its writer (`ds-shadow`, `ds-intz`; rt `a_sampled_depth_waits_on_its_writer`); a depth surface released by
   the application while bound still tests (`ds-release`); forgetting the bound depth's texture unbinds it and
   closes the pass (rt unit test).

---

### Task 1: the pipeline key and the per-draw depth values (mtld3d-core)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` and `src/d3d9/core/src/pipeline_key/tests.rs`; the call sites in
`src/d3d9/src/device/core_draw.rs` and `src/d3d9/rt/src/device/draw.rs` (still passing "no depth").

**Interfaces (produces):**
- `PipelineKey { .., pub depth_format: u32 }` and `PipelineSource { .., pub depth_format: u32 }`: the attached
  depth-stencil's `WMTPixelFormat`, `WMT_PIXEL_FORMAT_INVALID` (0) for none; forwarded by `pipeline_source`.
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
`src/dxmt/fakes/src/lib.rs`.

**Interfaces (produces):**
```rust
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DepthRef { pub id: TextureId, pub level: u32 }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct DepthMismatch;                     // bound, of another size than the colour target
impl CoreDevice {
    pub fn set_depth(&mut self, depth: Option<DepthRef>) -> bool;   // false, nothing changed: Some that is no depth texture's level
    pub const fn depth(&self) -> Option<DepthRef>;
    pub fn depth_format(&self) -> Result<u32, DepthMismatch>;       // the one fact (Architecture)
    pub fn clear_depth_stencil(&mut self, planes: u32, depth: f32, stencil: u8);   // planes: depth 1, stencil 2
}
// DrawData gains:
pub depth_stencil: DepthStencilSnapshot,      // from the render states, ungated
pub stencil_ref: u8,
pub slope_scale: f32,
// DrawRefused gains `DepthSize`.
```
- A depth texture is a stored texture whose `pixel_format` has depth-stencil planes
  (`dxmt_core::format::depth_stencil_planar_flags != 0`) and `render_target: true`; `set_depth` resolves only those,
  and `texture_shape` (the colour resolver) refuses them, so neither kind resolves as the other.
- One private resolver gives the depth's `(Arc<Texture>, TextureViewKey, width, height, pixel_format)` for a level
  (a `WMT_TEXTURE_TYPE2D` view of that one level, as 3d1's level views); the pass and the Clear both use it.
- `set_depth` closes the open render pass on a change; `set_target` is unchanged (the depth persists across it);
  `forget_texture(id)` of the bound depth's texture closes the pass and unbinds it.
- `open_render_pass`: with `depth_format() == Ok(f)`, `f != 0`: `start_render_pass(planar, 0, 1, None)` with
  `planar = depth_stencil_planar_flags(f)`, `enc.attachment::<{ stage::PIXEL }>(&texture, key, Access::READ)` (the
  write is `end_pass`'s), the depth `Target` (and the stencil `Target` on the **same** attachment when `planar & 2`)
  with `WMT_LOAD_ACTION_LOAD` / `WMT_STORE_ACTION_STORE` (M §4.1). Otherwise as today. A mismatch opens no pass.
- `build_pipeline`: `info.depth_pixel_format = f` when `planar & 1`, `info.stencil_pixel_format = f` when
  `planar & 2` (M §4.1), from `source.depth_format`.
- `draw`: `Err(DrawRefused::DepthSize)` on a mismatch; `debug_assert_eq!(key.depth_format,
  self.depth_format().unwrap_or(0))`. With a depth attached, each draw encodes
  `WmtcmdRenderSetdsso { dsso, stencil_ref }` after the pipeline: the state of
  `draw.depth_stencil.gated_on_stencil_attachment(planar & 2 != 0)`, looked up by `key_from_snapshot(..).raw()` in
  `depth_states: FxHashMap<u64, Obj>` (kept for the device's life, as `pipelines`; a recorded closure names the
  handle), made on a miss from `description_from_snapshot` mapped field by field into `WMTDepthStencilInfo`
  (compare functions and stencil operations are Metal's values already; a face of a disabled stencil has
  `enabled: 0`) with `dxmt_core::library::depth_stencil_state`. Without a depth no state is encoded. The rasterizer
  record takes `scole_scale: draw.slope_scale`, `depth_bias: 0.0`.
- `clear_depth_stencil`: nothing for a mismatch or none bound; `planes &= planar`; nothing when that is 0 (H8);
  else `close_pass` and `emit(enc.clear_depth_stencil(&texture, key, 0, planes, depth.clamp(0.0, 1.0), stencil))`.
- Fakes (new lines only): `MTLDevice_newDepthStencilState #<h> cmp=<n> write=<n> front=<en,cmp,sfail,dfail,pass,rm,wm>
  back=<..>`; in a pass after the colour lines `  depth #<view> load=<n> store=<n> clear=<f>` and
  `  stencil #<view> load=<n> store=<n> clear=<n>`; after `  pso color0=<fmt>` a line `  pso depth=<fmt> stencil=<fmt>`
  when either is set; render commands `    dsso #<h> ref=<n>` and, when non-zero, `    slope <f>`; a sampler's
  compare function on a new line after the sampler's line when non-zero (Task 5 reads it).

- [ ] **Step 1: failing tests** in `depth/tests.rs` (fixture helpers `depth_desc(format, size, levels)`):
  `a_draw_with_no_depth_encodes_no_state`; `a_depth_is_attached_with_the_colour_target` (D32F_S8: both planes on
  one view, LOAD/STORE, `pso depth=260 stencil=260`, a `dsso` with the reference); `a_depth_only_format_attaches_no_stencil`
  (planar 1, `stencil=0`, the state's faces disabled though the snapshot enables stencil);
  `set_depth_takes_depth_textures_only` (an unknown id, a colour render target: false; `set_target` with a depth
  texture: false); `a_depth_change_closes_the_open_pass` and `a_target_change_keeps_the_depth`;
  `a_depth_of_another_size_is_a_mismatch` (`depth_format()` is `Err`, the draw `DepthSize`, a Z clear records
  nothing); `a_clear_then_a_draw_is_one_pass` (colour and Z|stencil clears, then a draw: one pass, colour and both
  planes loading their clear values); `a_clear_with_no_draw_is_a_pass_of_its_own`;
  `a_stencil_clear_of_a_depth_only_format_records_nothing`; `forgetting_the_bound_depth_unbinds_it`;
  `a_depth_state_is_made_once_a_key`; `a_level_of_a_depth_texture_attaches_that_level` (`mip=1`, the level's size);
  `the_slope_scale_reaches_the_rasterizer`; `a_zenable_change_in_a_pass_encodes_a_new_state`. Run `-p d3d9-rt depth`
  — Expected: do not compile (`set_depth` missing).
- [ ] **Step 2: implement** the fakes' lines, then the device.
- [ ] **Step 3: run** `-p d3d9-rt` and `-p dxmt-fakes` — pass; `sh tools/rust-check.sh` — ok.
- [ ] **Step 4: commit** `d3d9-rt: a current depth-stencil, its state and its Clear` (the front end passes
  `DepthStencilSnapshot::inert()`, reference 0, slope 0 until Task 4).

### Task 3: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s2/gates/*` (from `s1/gates`; the summary grep gains `depth`).

**Interfaces (produces):**
- A mode `depth` (`d3d9_triangle depth [strict]`, 150 s budget, rows in the appendix) on **its own device**: `main`
  sets `pp.EnableAutoDepthStencil = TRUE`, `pp.AutoDepthStencilFormat = D3DFMT_D24S8` for this mode alone (every
  other mode's device stays without one, H15). Launched in every D3D9 lane after `targets` (`launch ours fresh depth
  240 depth $strict`; VM `dp64-fresh`, `dp32-fresh`).
- Every row clears Z (and stencil where it tests it) itself: the device defaults `ZENABLE` on and a never-cleared
  depth is undefined under mtld3d (ruling C4b3d2-1). Helpers: `clearAll(color, z = 1, stencil = 0)`,
  `zquad(x0, y0, x1, y1, z, color)` (a pre-transformed coloured quad at depth `z`), reusing `until`, `scene`,
  `readTarget`, `want`, `makeRt`, `target`, `backToScreen`, `show`.
- Refusal sets in the three scripts: `depth_refusals` (appendix, five names) with its own `depth)` arm in
  `refusals_ok` and in the VM gate's `runtime` (the two-level case, as 3d1's `targets`); `texture_refusals` becomes
  the `LockRect` name alone; `fixed_refusals` becomes `IDirect3DDevice9::Draw*PrimitiveUP (a depth texture at a
  vertex sampler)`. No set becomes empty (the scripts miscount one, M §8.2).
- `tex-depthtex` and `tex-fetch4` become drawn rows: `D3D_OK` in both runtimes; `tex-fetch4` asserts its colour
  (an L8 0x80 texture gathered: 0x808080 within 1); `tex-depthtex`'s pixel stays unasserted (a never-rendered depth
  texture, undefined under mtld3d). `tex-vtxdepth` and `tex-vtxdepthfmt` stay refused rows.

- [ ] **Step 1: write the rows**, the script lists, header comments and counts.
- [ ] **Step 2: syntax** with the x86_64, i686 and aarch64 `*-w64-mingw32-clang++ -std=c++17 -fsyntax-only -Wall
  -Wextra` — Expected: no diagnostics.
- [ ] **Step 3: commit** `dxmt, vm: d3d9_triangle depth rows in every D3D9 lane (DXMTRS C4b-3d2)`.
- [ ] **Step 4: rows first, on mtld3d** (job `jr`: plain build at Task 2's commit, `d3d9-check` default and msl;
  job `jcr`: the VM default lanes, old and new WoW64) — Expected: every `depth` row ok in each lane, `tex-fetch4`
  ok with its colour. A row mtld3d fails is ruled on (fixed if the row is wrong; dropped with its limit named if
  mtld3d is), ledgered, recommitted.
- [ ] **Step 5: RED under core** (job `jq-red`: core build at Task 2's commit) — Expected: the `depth` launch fails
  at `CreateDevice` (8876086a: every row red, the runtime set red); `tex-depthtex` and `tex-fetch4` FAIL (refused);
  `textures-runtime` and `fixed-runtime` FAIL (the sets); the other launches unchanged.

### Task 4: the front end's depth-stencil (attachment, state, Clear)

**Files:** Modify `src/d3d9/src/direct3d9.rs` (`create_core_device`), `src/d3d9/src/device.rs`
(`device_create_depth_stencil_surface`, `device_set_depth_stencil_surface`'s core arm, the auto-depth predicates),
`src/d3d9/src/device/core_arms.rs` (`clear`), `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/surface.rs`,
`src/d3d9/src/texture.rs` (`core_flush_dirty`, `core_refuses_lock`), `src/d3d9/core/src/pipeline_key.rs` (+ tests).

**Interfaces:** consumes Tasks 1-2.
- **"Bound" without a Metal handle (H1).** `DeviceInner` gains a cfg'd `core_auto_depth: Option<TextureId>` and one
  predicate `has_auto_depth()` (`!depth_stencil_handle.is_null()`, or under core `core_auto_depth.is_some()`), read
  by `depth_stencil_bound()` (`device.rs:3881`) and `get_or_create_implicit_depth_stencil` (`:4111`).
  `depth_stencil_has_stencil()` and the implicit surface's `live_format()` already read `depth_stencil_format`,
  which the core device now sets. The other readers of the handle are mtld3d's frame, snapshot, auto-resize and
  Reset paths, not run under core (`:1803`, `:1952`, `:2730`, `:3319-3465`, `:5926-6094`, `:13730`; review 1 checks
  the list).
- `create_core_device`: the first refusal tests `windowed == 0 || multi_sample_type != 0` only. With
  `enable_auto_depth_stencil != 0 && auto_depth_stencil_format != 0` (format 0 is "none", as mtld3d): a core
  texture under a new id (`render_target: true`, `map_d3d_depth_format`'s format, the back buffer's size, one
  level), `core.set_depth(Some(..))`, `core_auto_depth`, `depth_stencil_format = pp.auto_depth_stencil_format`.
  `D3DRS_ZENABLE` defaults to TRUE exactly when it exists (mtld3d's rule, M §1.2).
- `Direct3DSurface9::core_depth(&self) -> Option<DepthRef>` (core-runtime): the device's `core_auto_depth` for the
  implicit depth-stencil; `{ id: core_texture, level: 0 }` for a standalone depth-stencil holding a core id;
  `{ parent's id, mip_level }` for a level of a depth-format texture; `None` otherwise. `core_target()` answers
  `None` for a standalone surface whose usage is `D3DUSAGE_DEPTHSTENCIL`.
- `CreateDepthStencilSurface` under core: mtld3d's checks in mtld3d's order with mtld3d's answers (M §2.1 steps
  2-6), a sample count above 1 refused as `IDirect3DDevice9::CreateDepthStencilSurface (multisampled)`, an extent
  past `texture_extent_fits` answered `D3DERR_INVALIDCALL` (3d1's review finding C1), then `texture_create` and
  `new_depth_stencil` with no Metal handle plus `set_core_texture`; registered as 3d1's `core_color_target` is.
- `core_flush_dirty`: a depth-format texture gets its core texture (`render_target: true`, its Metal depth format,
  its levels) and never uploads. `core_refuses_lock` covers every depth-format texture (a dynamic one made without
  the usage too): its packed upload is C4c's.
- `SetDepthStencilSurface` core arm (after the existing validation): NULL unbinds (`core.set_depth(None)`, the
  binding and `DEPTH_EXPLICITLY_UNBOUND` as today). A surface: `core_depth()` (none: refused as
  `IDirect3DDevice9::SetDepthStencilSurface (a surface the core does not attach)`), the parent texture flushed so
  its core texture exists, `core.set_depth` (false: the same refusal), then `replace_depth_stencil` and the flag
  cleared. Always `D3D_OK` for a surface the core attaches, whatever its size (the mismatch is a draw's and a
  Clear's, since either binding may change alone).
- `core_arms::clear`, in this order: Z or stencil flags with `!dev.depth_stencil_bound()` → `D3DERR_INVALIDCALL`
  (as today, no refusal line); rects or scissor → today's refusal; no colour target → `D3D_OK`; a viewport not
  covering → today's refusal; Z or stencil flags with `core.depth_format()` an `Err` → refused as
  `IDirect3DDevice9::Clear (a depth-stencil of another size than the target)`, nothing cleared; the colour clear;
  then `core.clear_depth_stencil(planes, z, stencil & 0xFF)` with `planes` from the two flags (rt masks them by
  format: `D3DCLEAR_STENCIL` on a depth-only format is D3D9's silent no-op).
- `core_draw::resolve`: `core.depth_format()` once. `Err` → `DrawFlags::DEPTH_SIZE` → `CoreRefusal::DepthSize`,
  `IDirect3DDevice9::Draw*PrimitiveUP (a depth-stencil of another size than the target)` (after `Scissor` in
  `core_draw_kinds`). `has_depth = format != 0` feeds the key's `depth_format`, `core_ps_variant`, and
  `pos_fixup(.., rhw_depth_clamp(rhw, has_depth, rs[ZENABLE] != 0), d3d_depth_bias_to_clip(rs[DEPTHBIAS], znear,
  zfar))` with `viewport_depth_range`'s range. `DrawData`: `depth_stencil: snapshot_from_state(&rs)`,
  `stencil_ref: low byte of rs[STENCILREF]`, `slope_scale: d3d_slope_scale_to_metal(rs[SLOPESCALEDEPTHBIAS], 1.0)`.
  `DrawRefused::DepthSize` maps to the same refusal.

- [ ] **Step 1: failing test** (mtld3d-core): `core_draw_kinds_cover_every_case` gains the `DEPTH_SIZE` case and
  its exhaustive match names `DepthSize` — Expected: does not compile. The behavioural RED is Task 3 Step 5.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** the tests; `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: a core device has a depth-stencil: the auto one, surfaces, textures, state, Clear`.
- [ ] **Step 5: GREEN under core, part A** (job `jq-a`) — Expected: `depth` strict: every row ok but the sampling
  rows (`ds-shadow`, `ds-intz`, `ds-df`, `ds-fetch4`, FAIL: `DrawPrimitiveUP` 8876086a) and `depth-runtime`
  (the set lacks the feedback name's line only if `ds-feedback` is refused under the old name: FAIL);
  `tex-depthtex`, `tex-fetch4`, `textures-runtime`, `fixed-runtime` still FAIL; the other launches as before.
  **Cut point** (advisor): the slice can be pushed here by moving the sampling rows to a slice of their own.

### Task 5: depth sampling and fetch4

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ tests), `src/d3d9/rt/src/device/samplers.rs` (+ tests),
`src/d3d9/src/device/core_draw.rs`.

**Interfaces:**
- `core_texture_variant(variant, masks: &SampledMasks, lod_table)` with `pub struct SampledMasks { pub volume: u16,
  pub cube: u16, pub depth: u16, pub depth_fetch: u16, pub raw_depth_red: u16, pub fetch4: u16, pub fetch4_alpha:
  u16 }` (mtld3d's fold, M §5.1, from `StageBindings`, each masked by the draw's sampled slots as volume and cube
  are today).
- rt `samplers.rs`: `compare_function: WMT_COMPARE_FUNCTION_LESS_EQUAL` when the description has `IS_COMPARE`
  (M §5.3), else 0.
- `core_draw`: per sampled pixel slot `is_compare = depth bit && !fetch bit` → `snapshot_from_state(&states,
  is_compare)`; a fetch slot's snapshot takes `force_point_filter()`; a depth texture binds in its stored format.
  `lod_table` already takes the depth and fetch4 masks.
- Refusals: `CoreRefusal::Fetch4` and `DrawFlags::FETCH4` deleted. `DepthTexture` becomes `VertexDepthTexture`
  (`... (a depth texture at a vertex sampler)`, flag `VERTEX_DEPTH_TEXTURE`, the vertex-slot fold alone). New
  `BoundDepthSampled` (`IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)`, flag
  `SAMPLES_BOUND_DEPTH`): a sampled pixel slot's texture id equals `core.depth()`'s id, whatever the level (H10).
  Order: Blending, Scissor, DepthSize, BoundDepthSampled, VertexDepthTexture, the shader checks.

- [ ] **Step 1: failing tests**: mtld3d-core `core_texture_variant_carries_the_depth_and_fetch4_masks`, the
  `core_draw_kinds` cases and exhaustive match (`BoundDepthSampled`, `VertexDepthTexture`; no `Fetch4`,
  `DepthTexture`); d3d9-rt `a_compare_sampler_is_less_equal` and `a_sampled_depth_waits_on_its_writer` (a pass
  writes a depth texture, the next samples it: two passes in order, the depth view bound in its stored format) —
  Expected: do not compile / fail on the compare line.
- [ ] **Step 2: implement.** **Step 3: run**; `sh tools/rust-check.sh` — ok.
- [ ] **Step 4: commit** `d3d9: a core device samples depth textures and takes fetch4`.
- [ ] **Step 5: GREEN under core** (job `jq`) — Expected: `depth` every row strict, `textures` and `fixed` every row,
  `targets`, `state` and `shaders` as before, each refusal set exact.

### Task 6: gates, probe and one measurement

- [ ] **jb** (core build at the tip): the stateblock conformance under core equals default; `d3d9-check` core and
  default ok.
- [ ] **jm** mutation REDs, one build each, the named row red and no launch other than `depth` changed:
  (a) `open_render_pass` never attaches the depth → `ds-test`; (b) the `dsso` command's `stencil_ref` 0 →
  `ds-stencil`; (c) `clear_depth_stencil` clears to 1.0 whatever `depth` → `ds-clearz`; (d) `pos_fixup`'s bias
  lane 0 → `ds-bias`; (e) the depth view's `first_mip` 0 → `ds-mip`; (f) the compare sampler's function 0 →
  `ds-shadow`.
- [ ] **ja** (plain build at the tip): translator key and imports unchanged; `d3d9-check` default and msl ok; Wine's
  conformance on the default runtime as C4b-3d1's.
- [ ] **jc** (VM): `vm/dxmt/d3d9-gate.sh` in the four lanes — Expected: ok.
- [ ] **jp** probe: `--bench 2000 <0|600> tex` on mtld3d and core, once each, beside 3d1's 2.57 against 1.90
  ms/frame (`build/c4b3d/s2/probe.md`). The bench's device has no depth: it measures what the slice adds to a draw
  without one (a `depth_format()` look-up, three `DrawData` fields).
- [ ] **jw** measurement, not a gate (ruling C4b3d2-11): Wine's `visual` under core, once
  (`D3D9_RUNTIME=core D3D9_CORE_MEASURE=1 sh dxmt/mtld3d-conformance.sh --only visual`; the script's stateblock
  guard and its refusal-line proof step aside under that variable, and the run writes `run-core-visual.txt`).
  Recorded in `build/c4b3d/s2/wine-visual.md`: whether it completes, crashes or hangs (30 minutes at most), the
  failing-site count against the default lane's run, the refusal names it logged by count. Input for 3d3-3d5's
  order; nothing in it blocks the push.

### Task 7: review and push

- [ ] Whole-branch review by a fresh Opus reviewer (the package, this plan, the spec, Review Focus verbatim, the
  ledger's rulings); one fix pass for Critical and Important findings, each RED then GREEN.
- [ ] Spec §5 paragraph, pin, privacy scan, push DXMTRS `c4b3d` and MacNeutron `c4b3d-mn` to their `main`s
  (fast-forward), post-push check (plain build from main, tests, x64 `dxmt/check.sh` on an idle Mac), memory, this
  slice's `benchwork`/`work` folders deleted.

## Rules check

- One fact (`depth_format()`) feeds the pass, the pipeline, the variant, the state, the clamp and the Clear, in rt,
  so a new attachment rule (3d3's sizes, 3d5's sample counts) has one place to change; the front end reads it once
  a draw, with no reference taken.
- No per-draw allocation is added: the state is a map look-up by a 64-bit key; the snapshot is 24 bytes by value.
- Front-end additions are core arms beside mtld3d's code; mtld3d's paths gain one predicate (`has_auto_depth`) whose
  value for a non-core device is today's expression.
- mtld3d-core's `depth_stencil_state`, `fetch4`, `sampler_state` and airconv's lowering are used as they are.

## Rulings

- **C4b3d2-1 (a new depth-stencil holds depth 0.0, stencil 0).** The core's initializer zeroes it, as it does a
  D3D11 depth texture; D3D9 leaves the contents undefined, mtld3d's Metal texture is uninitialised, and Wine's
  `test_depth_stencil_init` (a z = 0 quad under LESSEQUAL on a never-cleared surface) passes with 0.0. Rows clear Z
  themselves. Cost if wrong: a title that never clears Z draws nothing under the default `LESSEQUAL` where a
  driver that starts at 1.0 would have drawn.
- **C4b3d2-2 (another size than the colour target is refused).** D3D9 allows a larger depth-stencil; drawing with
  one is legal in Metal, but a whole-target D3D9 Clear of it is a partial clear of the depth (3d3's rect path), and
  the core's `clear_into_render` would fold an earlier whole-depth clear into a smaller pass and clear only the
  pass's area (H9; the guard is 3d3's by C4b3d-1). A smaller one is undefined in D3D9. So a draw, and a Clear with
  Z or stencil, refuse while the sizes differ; `SetRenderTarget` and `SetDepthStencilSurface` still succeed. Cost
  if wrong: **the ordinary D3D9 pattern of the auto depth-stencil with a smaller render target draws nothing under
  core until 3d3.**
- **C4b3d2-3 (the depth-stencil sampled while bound is refused; C4c).** Metal forbids it and the core has no
  read-only depth route D3D9 could use; mtld3d copies the attachment (map2-passes §4, about 80 lines over the depth
  copies that are C4c's, sync worklist rows 16-25). Cost if wrong: deferred renderers that read INTZ scene depth
  while testing against it draw nothing under core until C4c.
- **C4b3d2-4 (depth textures at vertex samplers stay refused; C4c).** The core's vertex sampler kinds have no depth
  kind (H13). Its refusal gets a name of its own so the strict sets stay exact. Cost if wrong: none known (no
  title case).
- **C4b3d2-5 (honest LOAD/STORE; the economy rules are C4d's).** C4b3d-1 left DONT_CARE planes, `drop_dead_store`
  and the later store in `same_targets` to this slice to take or rule out: ruled out. Each is a bandwidth saving on
  a correct pass, and each is a dxmt-core change D3D11 would inherit. `map2-passes.md` holds no other remainder for
  depth (its feedback copy is C4b3d2-3; its two characterization tests were placed by C4b3d-1). Cost if wrong:
  a depth store and load a pass that a tile-based GPU pays for; measured in C4d.
- **C4b3d2-6 (no dxmt-core change).** Attachment, Clear, the state object and the initializer are used as D3D11
  uses them. Cost if wrong: a core defect found here is fixed in dxmt-core with a D3D11 gate run added.
- **C4b3d2-7 (dynamic depth textures: created, attachable, their lock refused).** The packed upload is C4c's
  (worklist row 24); the `depth.aliasSameSize` carry is not applied under core (row 19). Cost if wrong: a title
  that locks a dynamic depth texture gets `D3DERR_NOTAVAILABLE`.
- **C4b3d2-8 (a Clear's z is clamped to [0, 1]).** Metal's clear depth must lie there; mtld3d passes the bits
  through. Cost if wrong: none (the buffer cannot hold another value).
- **C4b3d2-9 (`D3DZB_USEW` reads as TRUE; depth formats are the two float formats).** As mtld3d (H7). Cost if
  wrong: as mtld3d's.
- **C4b3d2-10 (a sixth launch).** `depth` needs its own device, about 20 s a lane. Cost if wrong: gate time.
- **C4b3d2-11 (Wine's `visual` under core is measured once, not gated).** Its proof fails on any refusal line and
  the suite meets blending, scissor, MRT, MSAA, rect clears, locks and `StretchRect` (H14). Cost if wrong: a crash
  under core in a Wine test goes unseen until 3d5's sweep; the measurement is there to see it now.
- **C4b3d2-12 (depth-stencil state objects live as long as the device).** As the pipelines; bounded by the distinct
  states an application sets. Cost if wrong: memory in a title that sweeps stencil masks.
- **C4b3d2-13 (no video-memory accounting for core depth surfaces).** As 3d1's colour targets. Cost if wrong:
  `GetAvailableTextureMem` overstates under core.
- **C4b3d2-14 (`tex-depthtex`'s pixel stays unasserted).** It samples a never-rendered depth texture. The defined
  cases are the `depth` mode's sampling rows. Cost if wrong: none.

## Appendix: the rows (`d3d9_triangle depth`)

Device: 256x256 X8R8G8B8, auto D24S8. Unless said, a row clears colour, Z = 1 and stencil = 0, draws
pre-transformed coloured quads and reads the back buffer.

| row | what it does | asserts |
|---|---|---|
| `ds-default` | `GetDepthStencilSurface`, its desc; `GetRenderState(ZENABLE)` | D24S8, 256x256, `D3DUSAGE_DEPTHSTENCIL`; TRUE |
| `ds-test` | red at z 0.25, then green at 0.75 over half of it and beyond | overlap red, beyond green |
| `ds-func` | `ZFUNC` NEVER, LESS, EQUAL, GREATER, ALWAYS against a quad at 0.5 | each function's result |
| `ds-write` | `ZWRITEENABLE` off for the near quad, then a far quad | far over near |
| `ds-zenable` | in one scene: tested draw, `ZENABLE` off draw behind it, tested draw | the untested draw shows and wrote no depth |
| `ds-clearz` | `Clear(ZBUFFER, 0.5)`; quads at 0.25 and 0.75 | near drawn, far not |
| `ds-clearonly` | `Clear(ZBUFFER, 0.25)` in one frame with no draw; next frame a quad at 0.5 without a Z clear | not drawn |
| `ds-stencil` | `Clear(STENCIL, 1)`; EQUAL with ref 1, ref 2, ref 0x101; REPLACE then EQUAL; INCR; read and write masks; ref back to 0 | each case |
| `ds-twosided` | `TWOSIDEDSTENCILMODE`: CW and CCW quads with different pass ops, then EQUAL tests | each face's op |
| `ds-bias` | coplanar quads under LESS: without bias, with `DEPTHBIAS` -0.01 | second drawn only with bias |
| `ds-slope` | a quad slanted in z drawn twice under LESS, the second with `SLOPESCALEDEPTHBIAS` -1 | drawn only with the slope term |
| `ds-clamp` | a pre-transformed quad at z 1.5: `ZENABLE` off, then on | drawn (clamped), then clipped |
| `ds-odepth` | a pixel shader writing `oDepth` 0.25 on a quad at 0.75, then a quad at 0.5 | the second is hidden |
| `ds-surface` | `CreateDepthStencilSurface` D24X8 and D16, 256x256, bound: the depth test; desc | as `ds-test`; formats echo |
| `ds-nostencil` | D24X8 bound: `STENCILENABLE` with NEVER; `Clear(STENCIL)` alone; `Clear(ZBUFFER or STENCIL)` | drawn; `D3D_OK`, colour kept; Z cleared |
| `ds-unbound` | `SetDepthStencilSurface(NULL)`: `Clear(ZBUFFER)`, `Get`, a far-behind-near draw order; rebind the implicit | `INVALIDCALL`; `NOTFOUND`; untested; tested again |
| `ds-persist` | Z written with the back buffer bound, a 256x256 render-target texture bound, a quad behind | hidden on the texture |
| `ds-texture` | a D24S8 `DEPTHSTENCIL` texture's level 0 bound | as `ds-test` |
| `ds-mip` | a two-level depth texture's level 1 with a 128x128 render target | as `ds-test` at 128 |
| `ds-release` | a depth surface bound, released by the application, drawn against | still tests |
| `ds-shadow` | depth written into a D24X8 texture (left half 0.25, right 0.75); sampled with texcoord z 0.5 | left unlit, right lit |
| `ds-intz` | the same into an INTZ texture, sampled raw | grey of each depth |
| `ds-df` | DF16 and DF24, sampled raw | red of each depth, green and blue 0 |
| `ds-fetch4` | DF24 with `GET4` and point mag filter, constant depth | the depth in every channel |
| `ds-size` | a 64x64 and a 512x512 depth surface with the 256 target: draw, `Clear(ZBUFFER)`, `Clear(TARGET)` | strict: two refusals each, colour clear ok; mtld3d: `D3D_OK`, the 512 one tests |
| `ds-feedback` | the bound depth texture at stage 0, drawn | strict: refused; mtld3d: `D3D_OK` |
| `ds-msaa` | `CreateDepthStencilSurface` with 4 samples | strict: refused; mtld3d: `D3D_OK` |
| `ds-huge` | `CreateDepthStencilSurface(16385, 16)` | `D3DERR_INVALIDCALL` in both |
| `ds-lock` | a dynamic D16 texture's `LockRect` | strict: refused; mtld3d: `D3D_OK` |

`depth_refusals`: `IDirect3DDevice9::Draw*PrimitiveUP (a depth-stencil of another size than the target)`,
`IDirect3DDevice9::Clear (a depth-stencil of another size than the target)`,
`IDirect3DDevice9::Draw*PrimitiveUP (the depth-stencil sampled while bound)`,
`IDirect3DDevice9::CreateDepthStencilSurface (multisampled)`,
`IDirect3DTexture9::LockRect (a render-target or depth texture)`.
