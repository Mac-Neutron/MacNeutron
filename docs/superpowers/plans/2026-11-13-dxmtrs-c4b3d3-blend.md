# DXMTRS C4b-3d3: blending, scissor, partial clears and a depth-stencil of another size on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, draws blend (`D3DRS_ALPHABLENDENABLE`, factors, operations, separate alpha, the
blend factor colour), honour the scissor test, and take a depth-stencil of another size than the colour target;
`Clear` takes rects, the scissor and a viewport smaller than the target, for colour, depth and stencil; and
`D3DRS_SRGBWRITEENABLE` encodes in hardware, after the blender, on a target that has an sRGB view. Same pixels as
under mtld3d, which stays as it is.

**Architecture:** third of C4b-3d's five slices (3d1 targets, 3d2 depth-stencil, done; **3d3**; 3d4 MRT; 3d5 MSAA
and the Wine suite sweep). Four features, each one DXMTRS commit that lifts its own refusal, in an order where
every prefix is pushable:

1. **Scissor and size.** A draw carries its resolved scissor (the scissor rect with the test on, the drawn viewport
   with it off, as mtld3d resolves it); d3d9-rt clamps it to the pass's extent and records it at the first draw of
   each pass and on a change. The bound depth-stencil is attached whatever its size; the pass's extent is the
   minimum of the colour target's and the depth-stencil's, which is what Metal gives mtld3d. `DepthState::Detached`,
   `CoreRefusal::DepthSize` and `DrawRefused::Depth` go. dxmt-core's optimizer gets an area guard first: a
   whole-view Clear is not folded into a pass that draws to less than the view.
2. **Blending.** `PipelineKey` gains `blend: Option<CoreBlend>`, mtld3d's effective blend (its collapse rules, its
   destination-alpha clamp on a target without alpha) in mtld3d's `BlendFactor`/`BlendOperation`; d3d9-rt maps
   them to the wire's numbers by an explicit match and fills `colors[0]`. The blend factor colour is record 17
   (with the draw's stencil reference), on draws whose key reads it.
3. **Clears.** The front end resolves a Clear per plane set (colour; depth-stencil) to `Whole` or a list of
   regions with a host-tested function that mirrors mtld3d's (rects x scissor, clipped to the viewport, judged
   against that attachment's own extent). `Whole` is today's Clear pass; regions are one `ClearRtv` pass
   (dxmt-core's quad clear, D3D11's `ClearView`) on that view alone.
4. **Hardware sRGB.** A target with an sRGB twin (the back buffer, BGRA8/RGBA8 render targets) is attached through
   the twin's view while `SRGBWRITEENABLE` is on: the key names the twin's format, the pixel stage stops encoding,
   a Clear stores the linear colour. A toggle ends the pass. Other formats keep the shader encode, as under mtld3d.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-core, mtld3d-core, d3d9-rt, d3d9.dll front end), C++ (`d3d9_triangle`),
sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line, 3d1's and 3d2's
paragraphs). Maps: `build/c4b3d/map-3d3.md` (this slice; cited as M §n and its hazards as F.n), `build/c4b3d/map.md`,
`build/c4b3d/map-3d2.md`. Reviews: `build/c4b3d/s3/review-{1-design,2-core,3-exec}.md`.

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check; a
  core-only struct field or private helper is cfg'd in the struct, in every literal and at its definition. Two
  shared functions are refactored without a change of behaviour, each covered by its existing tests and the default
  lanes: `effective_blend` (its body moves to a function of `PipelineRsBits`) and `device_clear`'s region list (an
  extraction inside `device.rs`).
- **One dxmt-core change, and only that one:** the area guard in `encoder/optimize.rs` (Task 2). It reaches
  D3D10-12, so dxmt-core's and d3d11's tests and the x64 `dxmt/check.sh` are pre-push gates here, not only the
  post-push check. No other dxmt-core file changes; dxmt-fakes does not change.
- `tools/rust-check.sh` passes after every DXMTRS commit; a d3d9-rt or mtld3d-core API change carries its front-end
  call sites in the same commit. New files are `git add`ed before it; a new or removed Clone/Copy derive needs
  `sh tools/audit.sh --update-derives`.
- No new dependency. No `HashMap` (FxHashMap). No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the
  body; lines of 100 columns at most. At most three `bool` fields in a struct.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`, base bd2ab193) and `build/c4b3c/mn` (branch
  `c4b3d-mn`, base 22cb7c0), cargo target `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s3/gates`
  (`head.sh` there; state names `c4b3d3-*`), logs and reviews in `build/c4b3d/s3`. Builds and GPU jobs under the
  pipeline lock, `$D` and the main checkout restored by each job's trap. No make in a worktree. A job tests
  **committed** tips; a job script is never edited once any chain may run it (a changed job is a new file).
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; `cargo test --offline --target aarch64-apple-darwin
  -p <crate>` (d3d9-rt with the `dynamic_lookup` rustflags, as C4b-3d1's plan); mingw compilers at
  `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x300-0x33f** here, one a test (Step 0 of Task 3 greps that none is taken).
- Every DXMTRS commit is safe to run under core: a refusal is lifted only in the commit that makes its calls
  correct. MacNeutron's core lanes are red between Task 1's rows and Task 8's pin by design.
- Per task one lane (the Mac's core launch of `blend` and `depth`, job `jt`); full gates once, in Task 7. Benches
  and probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents too)
  while the x64 `dxmt/check.sh` or a bench runs.

## Scope

In: `D3DRS_ALPHABLENDENABLE`, `SRCBLEND`, `DESTBLEND`, `BLENDOP`, `SEPARATEALPHABLENDENABLE`, `SRCBLENDALPHA`,
`DESTBLENDALPHA`, `BLENDOPALPHA`, `BLENDFACTOR`; `D3DRS_SCISSORTESTENABLE` and `SetScissorRect` for draws and
`Clear`; `Clear` with rects (any `count`), under the scissor, and bounded by a viewport that does not cover the
attachment, for `TARGET`, `ZBUFFER` and `STENCIL`; draws and clears with a depth-stencil larger or smaller than
the colour target; `D3DRS_SRGBWRITEENABLE` in hardware on targets with an sRGB twin; the two inputs from
C4b-3d2's final review (the pre-transformed clamp follows the bound depth-stencil, not the key; `Clear(STENCIL)`
of a depth-only depth-stencil of another size does nothing).

Out (owners): more than one colour target, per-target write masks and blending (3d4); multisampling,
alpha-to-coverage, `CreateOffscreenPlainSurface`, the Wine suite sweep (3d5); `StretchRect`, `ColorFill`, `Reset`,
render scale, fullscreen, the bound depth-stencil sampled by a draw that uses its planes, vertex-sampler depth,
dynamic depth locks (C4c); per-draw state dedup beyond the scissor's, and a Clear whose view differs from the next
pass's (ruling 7) (C4d); `D3DBLEND_SRCCOLOR2`/`INVSRCCOLOR2` (Zero under mtld3d, so Zero here); the 1x1 "RT0 drop".

## Review Focus

1. **A scissor never leaks.** Metal keeps an encoder's scissor; the optimizer merges passes. A draw after a
   scissored draw, in the same pass or in the next pass on the same targets, draws through its own rect: test off
   after test on, a rect changed between draws, a pass break (a rect clear, a target switch and back) between two
   draws of one rect. Rows `sc-pass`, `sc-break`; rt `the_scissor_is_recorded_at_each_pass_start`.
2. **A scissor Metal rejects is never recorded.** The rect is clamped to the pass's extent (the minimum of the
   attachments), and a draw whose clamped rect is empty records nothing: a rect outside the target, inverted,
   negative, larger than the target, a viewport off the target with the test off, a small depth-stencil under a
   large target. Rows `sc-empty`, `sc-clamp`, `sz-small`; one core `blend` launch under the Metal validation layer
   with no report (Task 7).
3. **The pipeline blends what D3D9 asked.** The wire's factor numbers are not mtld3d's (F.1: destination colour
   and alpha swap under a cast); the back buffer counts as alpha-bearing and an X8 render target does not (F.3);
   a disabled blend with stale factors is one pipeline; MIN/MAX ignore factors; BOTH* set both sides. Rows
   `bl-factors`, `bl-destalpha`, `bl-off`, `bl-ops`, `bl-both`; rt `wire_blend_numbers`.
4. **The blend factor colour reaches every draw that reads it**, after a pass break too, and record 17's stencil
   reference never disturbs a stencil test (F.4). Rows `bl-factor`, `bl-stencil`.
5. **A Clear clears exactly its area**: `count == 0` with rects clears nothing; null rects ignore `count`; rects
   are clipped by the scissor and the viewport; a region that covers the attachment is the whole clear; the write
   mask, blending and the scissor of earlier draws do not reach a clear quad; a Clear between draws keeps the
   order; a whole Z Clear of a depth-stencil larger than the next pass's area clears all of it (the area guard).
   Rows `cl-*`, `sz-clear`, `ds-size`; mtld3d-core `clear_area_*`; dxmt-core
   `a_clear_is_not_folded_into_a_smaller_pass`.

---

### Task 1: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`.

- [ ] **Step 1: write** the `blend` launch kind (`d3d9_triangle blend [strict]`): the depth kind's device (auto
  D24S8), its runner `reset() && depthReset() && r.run()`, budget 180 s, the rows of the appendix's groups `sc`,
  `sz`, `bl`, `cl` (32; the `sr` group is Task 6's). `reset()` gains `ALPHABLENDENABLE FALSE`, `SRCBLEND ONE`,
  `DESTBLEND ZERO`, `BLENDOP ADD`, `SEPARATEALPHABLENDENABLE FALSE`, `SRCBLENDALPHA ONE`, `DESTBLENDALPHA ZERO`,
  `BLENDOPALPHA ADD`, `BLENDFACTOR 0xffffffff`, `SCISSORTESTENABLE FALSE` (the rect is reset by its
  `SetRenderTarget`). `ds-size` (depth kind) becomes a result in every lane (appendix). Headers: the kind, its row
  count, "the depth launch's three refused calls".
- [ ] **Step 2: scripts.** `d3d9-check.sh`: `launch ours fresh blend 240 blend $strict`, `check blend "<rows>"`,
  `blend_refusals=` (empty: the launch's log holds no refusal line; `refusals_ok` handles an empty set as count
  0), `depth_refusals` loses the two "of another size" lines. `d3d9-gate.sh`: `for r in bl64-fresh bl32-fresh`,
  the same two sets, the core block. `d3d9.sh`: two `tri` lines (`bl64-fresh`, `bl32-fresh`, 240 s) and the
  header; its row counter's class becomes `[a-z0-9-]*` (F.12).
- [ ] **Step 3: compile** `d3d9_triangle.cpp` with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall
  -Wextra`) — Expected: no diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle blend rows in every D3D9 lane (DXMTRS C4b-3d3)`.
- [ ] **Step 5: the controls.** Job `ja` (DXMTRS bd2ab193, this commit; `DXMT_D3D9_CORE=1` build, `dxmt-tests`,
  `d3d9-check.sh` default, then `D3D9_RUNTIME=core`). Expected: default `d3d9-check: ok` with every `blend` row
  and `ds-size` ok; core: the `blend` launch red on the rows that blend, scissor or clear partially, and
  `depth-runtime` red (five refusal lines against three). A row red on the default runtime is a wrong row or a
  defect of mtld3d: fix the row; a defect is ledgered as a ruling, the row then asserts what both runtimes should
  give and is listed as red-on-default in the gates until the defect's own fix (not this slice's, unless it is a
  one-line fix as the stencil Clear's was).

### Task 2: the area guard (dxmt-core)

**Files:** Modify `src/dxmt/src/encoder/optimize.rs` and its tests file.

- [ ] **Step 1: failing test** `a_clear_is_not_folded_into_a_smaller_pass`: a depth Clear of a 128x128 view, then
  a render pass attaching that view with `render_target_width/height` 64x32 — two passes remain, the render pass
  loads; the same with the pass at 128x128 — one pass, load action CLEAR; the same with width and height 0 (unset)
  — one pass; a colour Clear of a 128x128 view into a 64x64 pass — two passes. Run `-p dxmt-core optimize` —
  Expected: the first and last cases fail (folded).
- [ ] **Step 2: implement** in `clear_into_render`, before the plane logic: `if render.render_target_width != 0
  && render.render_target_height != 0 && (render.render_target_width < clear.width ||
  render.render_target_height < clear.height) { return false; }`. Its doc gains the sentence that a load action
  reaches the pass's area only.
- [ ] **Step 3: run** dxmt-core's and d3d11's tests, `sh tools/rust-check.sh` — Expected: ok, no other test moved.
- [ ] **Step 4: commit** `dxmt-core: a whole-view Clear is not folded into a pass that draws to less than the view`.

### Task 3: the scissor, and a depth-stencil of any size (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ `pipeline_key/tests.rs`); `src/d3d9/rt/src/device.rs`,
`device/depth.rs`, `device/draw.rs`, `device/recorder.rs` and their tests, `device/fixture.rs`;
`src/d3d9/src/device/core_draw.rs`, `core_arms.rs`.

**Interfaces (produces):**
- mtld3d-core: `pub const fn drawn_rect(v: &D3DVIEWPORT9, target: (u32, u32)) -> (u32, u32, u32, u32)` (now pub);
  `pub fn core_scissor(test: bool, rect: D3DRECT, v: &D3DVIEWPORT9, target: (u32, u32)) -> [u32; 4]` (`x, y,
  width, height`: `scissor_region(rect)` with the test on, `drawn_rect` with it off);
  `pub const fn clamp_rect(rect: [u32; 4], extent: (u32, u32)) -> Option<[u32; 4]>` (`x = min(x, W)`, `width =
  min(width, W - x)`, the same in y; `None` when either side is 0);
  `core_key_depth(bound: Option<u32>, samples_bound: bool, uses_plane: bool) -> Result<u32, CoreRefusal>` (no
  `attachable`); `DrawFlags::SCISSOR`, `CoreRefusal::Scissor` and `CoreRefusal::DepthSize` removed.
- d3d9-rt: `CoreDevice::depth_format(&self) -> Option<u32>` and `depth_size(&self) -> Option<(u32, u32)>` replace
  `depth_state()`; `DepthState` and `DrawRefused::Depth` removed; `DrawData { .., pub scissor: [u32; 4] }`;
  `CoreDevice { .., pass_scissor: Option<[u32; 4]> }` (none after a pass opens or closes).
- Behaviour: `resolve_depth` resolves the bound depth-stencil whatever its size; `open_pass` sets
  `render_target_width/height` to the minimum of the colour target's and the attached depth-stencil's; `draw_up`
  clamps `draw.scissor` to that extent, records nothing and answers `Ok(())` when it is empty, and records
  `WmtcmdRenderSetscissorrects` (one rect, `allocate_cpu`) after the viewport when `pass_scissor` is not that
  rect. The front end's clamp lane is `rhw_depth_clamp(layout.has_rhw(), bound.is_some(), zenable)` (mtld3d
  decides on the attached depth-stencil, which a draw that samples it still has).
- `core_arms::clear` keeps refusing what Task 5 owns: its size test becomes `depth_flags != 0 &&
  core.depth_size() != core.target_size()` with the same text.

- [ ] **Step 0:** `/usr/bin/grep -rn '0x3[0-3][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src` — Expected: no
  device handle in 0x300-0x33f.
- [ ] **Step 1: failing tests.** mtld3d-core: `core_scissor_follows_mtld3d` (test on: a plain rect, a negative
  origin clamps to 0, an inverted rect is empty at its origin; test off: the viewport, and the target for an
  unset viewport), `clamp_rect_cases` (inside, right and bottom overhang, origin at the edge, origin past it,
  zero size), `core_key_depth_covers_every_case` re-tabled without `attachable`, `core_draw_kinds_cover_every_case`
  without the scissor case. rt (`depth/tests.rs`, `draw/tests.rs`): `a_depth_of_another_size_attaches` (128x128
  and 16x16 against the 64x32 back buffer: the pass has the depth lines and its area is 64x32, resp. 16x16),
  `the_scissor_is_recorded_at_each_pass_start` (two draws of one rect: one `render 14`; a `close_pass` between
  them: two), `a_scissor_change_is_recorded`, `the_scissor_is_clamped_to_the_pass` (a 300x300 rect on 64x32
  records 64x32; with a 16x16 depth, 16x16), `an_empty_scissor_draws_nothing` (no pass, no pipeline built).
  Expected: do not compile.
- [ ] **Step 2: implement.** Front end: `DrawData.scissor = core_scissor(rs[SCISSORTESTENABLE] != 0,
  dev.scissor_rect(), &viewport, target)`; `key_depth` reads `depth_format()`.
- [ ] **Step 3: run** the three crates' tests and `rust-check` — Expected: ok. `a_depth_of_another_size_is_detached`
  and the `Detached` assertions are replaced, not kept.
- [ ] **Step 4: commit** `d3d9: the scissor test and a depth-stencil of any size on the core runtime`.
- [ ] **Step 5: lane** `jt` — Expected under core: every `sc-*` and `sz-*` row ok but `sz-clear`'s and `ds-size`'s
  Z Clear of the larger depth-stencil (Task 5); `bl-*`, `cl-*` red; default unchanged.

### Task 4: blending (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_state.rs`, `pipeline_key.rs` and their tests;
`src/d3d9/rt/src/device/draw.rs`, `device/target.rs`, `device/textures.rs`, `device/swapchain.rs` and tests,
`device/fixture.rs`; `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/texture.rs`, `device.rs` (the two colour
`CoreTextureDesc` literals).

**Interfaces (produces):**
- mtld3d-core `pipeline_state.rs`: `effective_blend(s)` becomes `effective_blend_rs(&s.rs, s.has_color_output())`
  (private, same body);
  `#[derive(Clone, Debug, PartialEq, Eq, Hash)] pub struct CoreBlend { pub src: BlendFactor, pub dst: BlendFactor,
  pub op: BlendOperation, pub src_alpha: BlendFactor, pub dst_alpha: BlendFactor, pub op_alpha: BlendOperation }`
  with `pub const fn reads_blend_color(&self) -> bool` (any factor `BlendColor` or `OneMinusBlendColor`);
  `pub fn core_blend(rs: &[u32; RENDER_STATE_COUNT], target_has_alpha: bool) -> Option<CoreBlend>`: the six
  states through `render_state::enum_value` into a `PipelineRsBits` (flags `BLEND_ENABLE`,
  `SEPARATE_ALPHA_BLEND` as the draw snapshot sets them, device.rs:13768-13796), `effective_blend_rs(.., true)`,
  `None` when not enabled, else each factor through `d3d_to_metal_blend_rt(f, target_has_alpha)` and each
  operation through `d3d_to_metal_blend_op`, as `key_from_snapshot` does without extra targets.
- `PipelineKey { .., pub blend: Option<CoreBlend> }`, `PipelineSource { .., pub blend: Option<CoreBlend> }`
  (forwarded by `pipeline_source`); `DrawFlags::BLENDING` and `CoreRefusal::Blending` removed.
- d3d9-rt: `CoreTextureDesc { .., pub has_alpha: bool }` (kept in `Stored`); `CoreDevice::target_desc(&self) ->
  Option<TargetDesc>` with `pub struct TargetDesc { pub size: (u32, u32), pub format: u32, pub has_alpha: bool }`
  (the back buffer: `has_alpha: true`, as mtld3d's frame start has it); `DrawData { .., pub blend_color:
  Option<[f32; 4]> }`; private `const fn wire_factor(f: BlendFactor) -> u8` and `wire_op(o: BlendOperation) -> u8`
  (explicit matches onto `WMT_BLEND_FACTOR_*` / `WMT_BLEND_OPERATION_*`, as unix `metal/texture.rs:848-862`).
- Behaviour: `build_pipeline` sets `colors[0].blending_enabled = 1` and the six fields for `Some(blend)`;
  `draw_up` records `WmtcmdRenderSetblendcolor { r#type: WMT_RENDER_COMMAND_SET_BLEND_FACTOR_AND_STENCIL_REF, ..,
  stencil_ref: draw.stencil_ref }` after the depth-stencil state when `draw.blend_color` is `Some`. Front end:
  `blend = core_blend(&rs, desc.has_alpha)`, `blend_color = blend.reads_blend_color().then(||
  d3dcolor_to_rgba_f32(rs[D3DRS_BLENDFACTOR]))`; a colour texture's `has_alpha` is
  `map_d3d_format(d3d_format).is_none_or(FormatMapping::has_alpha)` (mtld3d's, device.rs:10841).

- [ ] **Step 1: failing tests.** mtld3d-core: `core_blend_follows_effective_blend` (off with stale factors →
  `None`; SRCALPHA/INVSRCALPHA; separate alpha off → alpha equals colour; on → its own; BOTHSRCALPHA source; MAX →
  One/One; DESTALPHA with and without target alpha → DestinationAlpha, One; INVDESTALPHA → Zero without;
  out-of-range DWORDs take the defaults), `reads_blend_color_cases`, the key table grows by `blend` (`[Case; 18]`),
  `core_draw_kinds_cover_every_case` loses the blending case; the existing `effective_blend` tests pass unchanged.
  rt: `wire_blend_numbers` (all 13 factors and 5 operations against the `WMT_*` constants; destination colour is 6
  and destination alpha 8), `the_blend_colour_is_recorded_with_the_stencil_ref` (`render 17` after `render 16`,
  `stencil_ref` equal; absent for `blend_color: None`), `a_blend_is_its_own_pipeline` (two keys differing in
  `blend` build two pipelines). Expected: do not compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** tests, `rust-check`, `audit.sh --update-derives` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: blending on the core runtime`.
- [ ] **Step 5: lane** `jt` — Expected under core: `bl-*` ok too.

### Task 5: clears (mtld3d-core, d3d9-rt, front end)

**Files:** Create `src/d3d9/core/src/clear_area.rs` and `clear_area/tests.rs` (declared in `lib.rs`); create
`src/d3d9/rt/src/device/clear.rs` and `clear/tests.rs` (the two whole clears move there from `recorder.rs` and
`depth.rs` with their tests); modify `src/d3d9/src/device.rs` (`device_clear`), `device/core_arms.rs`.

**Interfaces (produces):**
- mtld3d-core `clear_area.rs`: `#[derive(Debug, PartialEq, Eq)] pub enum ClearArea { Whole, Regions(Vec<[u32;
  4]>) }` (regions `x, y, width, height`, none empty; an empty list clears nothing);
  `pub fn clear_area(regions: Option<&[(i32, i32, i32, i32)]>, viewport: (u32, u32, u32, u32), extent: (u32,
  u32)) -> ClearArea`: `None` is the viewport as the one region; each `(x1, y1, x2, y2)` is clipped to the
  viewport (inverted, empty or disjoint ones dropped, negative edges at 0: unix `clip_rect_to_viewport`,
  encoder.rs:1613); any clipped region with `x == 0 && y == 0 && width >= extent.0 && height >= extent.1` makes
  the result `Whole` (mtld3d's `region_covers_extent`); otherwise each is `clamp_rect`ed to `extent` and the
  non-empty ones are the list.
- Front end: `fn clear_regions(dev: &DeviceInner, count: u32, rects: *const c_void) -> Option<Vec<(i32, i32, i32,
  i32)>>` in `device.rs`, `device_clear`'s present block moved as it is (the scissor read included);
  `device_clear` calls it. `core_arms::clear(this, count, rects, flags, color, z, stencil)`: mtld3d's
  `INVALIDCALL` first; `viewport = drawn_rect(&dev.viewport(), target)`; for `TARGET`, `clear_area(regions,
  viewport, target)`; for the depth flags (planes masked by the format: a stencil flag on a depth-only format
  drops out, and no plane left is no clear), `clear_area(regions, viewport, depth_size)`. No refusal is left in
  it.
- d3d9-rt `clear.rs`: `clear(rgba)`, `clear_depth_stencil(planes, depth, stencil)` (whole, as today, the latter
  whatever the colour target's size); `pub fn clear_rects(&mut self, rects: &[[u32; 4]], rgba: [f32; 4])` and
  `pub fn clear_depth_stencil_rects(&mut self, rects: &[[u32; 4]], planes: u32, depth: f32, stencil: u8)`: nothing
  for an empty list or no plane; `close_pass()`; one closure: `enc.with_clear_rtv(|crtv, enc| { crtv.begin(enc,
  texture, key, 0, planes); each rect: crtv.clear_color(enc, rect, 0, rgba) | crtv.clear_depth_stencil(enc, rect,
  depth, stencil); crtv.end(enc) })` on the target's (resp. the depth-stencil's) own view; depth clamped as the
  whole clear's.

- [ ] **Step 1: failing tests.** mtld3d-core `clear_area_*`: `none_with_a_covering_viewport_is_whole`,
  `none_with_a_small_viewport_is_that_region`, `an_oversized_viewport_covers`, `no_rects_clear_nothing`
  (`Some(&[])` → `Regions(vec![])`), `rects_are_clipped_to_the_viewport`, `a_rect_covering_the_extent_is_whole`,
  `a_rect_past_the_extent_is_clamped`, `a_larger_depth_is_not_covered_by_the_colour_viewport` (viewport 0,0,256,256
  against 512x512 → that region), `inverted_and_negative_rects`. rt: `a_rect_clear_is_a_pass_of_its_own` (draw,
  rect clear, draw: three passes, in order, the middle one attaching the view alone with one scissor a rect),
  `a_rect_depth_clear_attaches_the_depth_alone`, `a_stencil_rect_clear_of_a_depth_only_format_records_nothing`,
  `an_empty_list_records_nothing`, `a_whole_depth_clear_of_a_larger_depth_is_recorded` (128x128 against 64x32).
  Should the fakes give `with_clear_rtv` no library (the closure then records nothing), the rt tests assert the
  pass break alone and the gap is stated; the rows are then the only proof of the quad.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** tests and `rust-check` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: Clear with rects, the scissor and a partial viewport on the core runtime`.
- [ ] **Step 5: lane** `jt` — Expected: default ok; core `blend` and `depth` launches ok on every row, the depth
  launch's log with exactly its three refusal lines, the blend launch's with none.

### Task 6: hardware sRGB write (d3d9-rt, mtld3d-core, front end; rows)

**Files:** Modify `src/d3d9/rt/src/device.rs`, `device/target.rs`, `device/textures.rs`, `device/swapchain.rs`,
`device/recorder.rs`, `device/clear.rs` and tests; `src/d3d9/core/src/pipeline_key.rs` (+ tests);
`src/d3d9/src/device/core_draw.rs`, `core_arms.rs`, `device.rs` (`CreateRenderTarget`'s literal), `texture.rs`;
MacNeutron: the `sr` rows and the lists.

**Interfaces (produces):**
- d3d9-rt: `CoreTextureDesc { .., pub srgb_format: u32 }` (the sRGB twin's `WMTPixelFormat`, 0 for none; non-zero
  only with `view_twins`); the back buffer is created with `WMT_TEXTURE_USAGE_PIXEL_FORMAT_VIEW` and has the twin
  `WMT_PIXEL_FORMAT_BGRA8_UNORM_SRGB`; `CoreDevice::set_srgb_write(&mut self, on: bool)` (a change ends the open
  render pass when the current target has a twin); `TargetDesc { .., pub srgb: bool }` (the pass attaches the
  twin: `format` is then the twin's). A render pass, a Clear and a rect clear attach the twin's view while it is
  on; the tile-barrier key and `draw_up`'s assert name the attached format; a read-back and Present keep the
  stored format's view.
- mtld3d-core: `core_ps_variant(base, ps_is_prog, point_list, linked_input_mask, has_depth, hardware_srgb: bool)`
  removes `VariantFlags::SRGB_WRITE` when `hardware_srgb`.
- Front end: `core.set_srgb_write(rs[SRGBWRITEENABLE] != 0)` before `target_desc()` in a draw and in `clear`; the
  Clear colour is CPU-encoded only when `!desc.srgb`; a colour texture's and `CreateRenderTarget`'s
  `srgb_format` is `pixel_format.srgb_twin()`'s (and `view_twins` true with it).

- [ ] **Step 1: rows** `sr-blend`, `sr-notwin`, `sr-clear`, `sr-toggle` (appendix) and the lists (36 rows);
  compile; commit `dxmt, vm: d3d9_triangle sRGB-write rows under blending (DXMTRS C4b-3d3)`. Lane `jt` — Expected:
  default ok; core `sr-blend` and `sr-toggle` red (blended before the encode's inverse), the others ok.
- [ ] **Step 2: failing tests.** rt: `srgb_write_attaches_the_twin` (the pass's colour line and `pso color0=` name
  the twin; off again: the stored format), `a_toggle_ends_the_pass`, `a_target_without_a_twin_keeps_its_format`,
  `a_clear_and_its_pass_share_the_view` (srgb on: Clear then draw is one pass), `a_clear_before_a_toggle_is_its_own_pass`
  (ruling 7's cost, pinned), `read_back_reads_the_stored_view`. mtld3d-core: `core_ps_variant_rules` grows the
  hardware case (flag removed) and keeps the shader case.
- [ ] **Step 3: implement; run** tests and `rust-check`.
- [ ] **Step 4: commit** `d3d9: sRGB write in hardware on the core runtime's targets that have an sRGB view`.
- [ ] **Step 5: lane** `jt` — Expected: both runtimes ok on all 36 `blend` rows, and on `srgb`, `rt-srgb`,
  `tex-srgb`, `tex-vtxsrgb` (jobs' other launches).

### Task 7: gates, mutants, probe

- [ ] **Step 1: `jq`** (Mac): `DXMT_D3D9_CORE=1` build; `dxmt-tests`; `d3d9-check.sh` default, `D3D9_BACKEND=msl`,
  `D3D9_RUNTIME=core`; `dxmt/mtld3d-conformance.sh D3D9_RUNTIME=core --only stateblock` equal to default; one core
  `blend` launch under the Metal validation layer (as C4b-3d2's Task 5 Step 5) with no report. Expected: all ok.
- [ ] **Step 2: mutants** (`jm`, one build and one core `d3d9-check` each; each red on its row and in no other
  launch): (a) `wire_factor` swaps destination colour and alpha → `bl-factors`; (b) `has_alpha` true for every
  target → `bl-destalpha`; (c) `pass_scissor` kept across `close_pass` → `sc-break`; (d) the area guard removed →
  `sz-clear`; (e) `clear_area` skips the viewport clip → `cl-viewport`; (f) the twin never attached (d3d9-rt reads every
  `srgb_format` as 0, so the shader path runs whole) → `sr-blend` and `sr-toggle`, no other row.
- [ ] **Step 3: plain build** (`jr`): no `DXMT_D3D9_CORE`; translator key and d3d9.dll imports unchanged;
  `d3d9-check.sh` default ok; **x64 `dxmt/check.sh`: all passed** (idle Mac).
- [ ] **Step 4: VM** (`jc`): default and core lanes, old and new WoW64, x64 and x86: `d3d9-gate` ok (a default
  lane short only on executed-test floors is rerun once before it is blamed).
- [ ] **Step 5: probe** (`jp`, once, idle Mac): this tip on its own full build, `--bench 2000 <0|600> tex` and
  `tex depth`, mtld3d and core; written to `build/c4b3d/s3/probe.md` beside `jq1`'s retake of 2acb1d99 and
  63f9fda0. A core delta between tips smaller than the spread of the three builds' mtld3d figures is reported as
  not measurable.

### Task 8: review, spec, pin, push

- [ ] Whole-branch review (most capable model; the plan's Review Focus verbatim; the ledger's rulings); one fix
  pass, each fix RED→GREEN with the suite green; minors deferred to the ledger.
- [ ] Spec §5: a C4b-3d3 paragraph after 3d2's. Execution notes at this plan's end. `dxmt/pins` to the DXMTRS tip.
- [ ] Privacy scan of both ranges; push DXMTRS `c4b3d` → `main`, then MacNeutron `c4b3d-mn` → `main`; post-push
  check from main (plain build, tests, `d3d9-check`, x64 check); CI; scratch of this slice deleted by full path;
  memory.

## Rules check

- **A dxmt-core change** (the first of C4): four lines in `clear_into_render`, with its unit test. Allowed because
  the fold is wrong for every front end (a load action reaches the pass's area only, M §A.3, F.6); D3D11 accepts a
  depth view larger than its render targets (`validate_set_render_targets`) and has the same hole. Gated by
  dxmt-core's and d3d11's tests and the x64 check before the push.
- New files: `clear_area.rs` (pure, host-tested, mtld3d-core: "pure functionality is factored out of d3d9") and
  d3d9-rt's `clear.rs`, each with `<stem>/tests.rs`. No `mod.rs`.
- New types: `CoreBlend` (Clone: a key is cloned into the pipeline map; no Copy), `ClearArea`, `TargetDesc` (two
  bools after Task 6). `CoreTextureDesc` reaches three bools (`view_twins`, `render_target`, `has_alpha`), the
  limit. Removed: `DepthState`, `DrawRefused::Depth`, `CoreRefusal::{Blending, Scissor, DepthSize}`,
  `DrawFlags::{BLENDING, SCISSOR}`; the derive inventory is updated for both directions.
- No static, no config key, no wire field, no dependency, no lint suppression. Metal wire values come from
  `winemetal_sys::wire` (d3d9-rt) and D3D9 constants from `mtld3d-types`; mtld3d-core keeps mtld3d's enums and
  never restates a wire number.
- No duplicate of mtld3d's blend rules (one `effective_blend_rs`) or of its Clear region list (one
  `clear_regions`); `clear_area` restates the unix encoder's viewport clip and cover test, which live in a crate
  mtld3d-core cannot depend on: stated in its doc, with the unix functions named.

## Rulings (planning)

1. **The area guard goes into dxmt-core**, not around it in d3d9-rt (a deferred Clear held on the API thread until
   the next pass opens). Why: the 3d2 plan assigned it here; it is the root cause and four lines. Cost if wrong: a
   D3D11 title that clears a depth view and then draws to a smaller target pays one more pass a frame; found by
   the x64 check or a bench, reverted by moving the test into d3d9-rt.
2. **The guard holds for colour too.** A colour Clear before a pass narrowed by a smaller depth-stencil stays a
   pass of its own and clears the whole target, where mtld3d's load action may reach the narrowed area only. Why:
   D3D9's Clear is of the target. Cost if wrong: a pixel difference from mtld3d outside the depth-stencil's
   extent, in a configuration D3D9 calls invalid; no row asserts it.
3. **A depth-stencil smaller than the target is attached**, the pass confined to it, as Metal confines mtld3d's.
   Cost if wrong: a title that binds a smaller depth-stencil and expects draws over the whole target sees them
   cut, under both runtimes alike.
4. **The scissor is recorded once a pass and on change**, not every draw. Why: a draw's records already cost the
   core; one record a pass is what a merged pass needs. Cost if wrong: a merge shape nobody traced lets a rect
   leak; the fix is to record it every draw.
5. **An empty scissor records no draw.** Why: no pixel can be written, and Metal's zero-size scissor is not
   something to learn from the validation layer. Cost if wrong: none visible until queries exist (C4c): an
   occlusion query around such a draw must still count zero.
6. **The blend colour is recorded only by draws whose key reads it**, with record 17 and the draw's stencil
   reference. Why: record 43 has no printer in the fakes; 17 is D3D11's. Cost if wrong: a reference written by a
   draw without a depth-stencil is stale for the next depth draw, which always sends its own with `SET_DSSO`.
7. **A Clear and the next pass may name different views** when `SRGBWRITEENABLE` differs between them: the Clear
   is then a pass of its own (correct pixels, one more pass), as under mtld3d. Pinned by an rt test, handed to
   C4d. Cost if wrong: a title that clears with it off and draws with it on pays a load and a store of the target
   a frame under core; C4d re-keys the Clear.
8. **Hardware sRGB follows mtld3d's twin table** (`PixelFormat::srgb_twin`: RGBA8, BGRA8 among render targets),
   not dxmt-core's wider `recall_srgb`. Cost if wrong: none against mtld3d; a wider table is a later choice for
   both runtimes.
9. **`ds-size` asserts a result, and rows are controls first**: every row is green on the default runtime before
   any core work; a row red there is a wrong row or a ledgered defect of mtld3d.

## Stated gaps

- No row draws a blended, scissored or cleared **multisampled** target (3d5) or a second colour target (3d4).
- The fakes print no blend field of a pipeline: the wire mapping is proven by `wire_blend_numbers` and the rows,
  not by a recorded pipeline's text.
- Wine's `visual` (`clear_test`, `test_blend`, `test_clear_different_size_surfaces`, `ds_size_test`,
  `srgbwrite_format_test`) does not run under core until 3d5; the rows stand in.

## Appendix: the rows (`d3d9_triangle blend`, and `ds-size`)

Back buffer 256x256 X8R8G8B8, auto D24S8. Quads are pre-transformed (`RV`, `rdraw`/`zquad`) unless said; a blended
quad's alpha is its diffuse alpha, with stage 0 `ALPHAOP SELECTARG1`, `ALPHAARG1 DIFFUSE` set by the row.
`frames(BLUE, ..)` clears to 0x000000ff (alpha 0). Every expectation is confirmed on the default runtime in Task 1
Step 5; a value below that mtld3d does not give is corrected there, not in the runtime.

**sc (6, Task 3).**
- `sc-rect`: rect (64,64)-(192,192), test on, a full-target red quad: inside red, outside blue.
- `sc-off`: the same rect, test off: red everywhere probed.
- `sc-empty`: test on with, in turn, an inverted rect, a zero-size rect, a rect past the target (300,300)-(400,400)
  and a negative one (-50,-50)-(-10,-10): nothing drawn, each `DrawPrimitiveUP` `D3D_OK`.
- `sc-clamp`: a 64x64 render-target texture, rect (0,0)-(256,256), test on: the whole texture red (read back);
  rect (-32,-32)-(32,32): the top-left quarter only.
- `sc-pass`: one frame: rect A, red quad; rect B, green quad; test off, a white quad over the bottom strip: each
  where it should be, nothing of red in B.
- `sc-break`: rect A, a red quad over the whole target; a pass break that leaves the rect alone (a second
  depth-stencil surface bound, Z off); a green quad over the whole target with no scissor call since: green
  inside A, blue outside it.

**sz (3, and `ds-size` in the depth kind; Task 3, `sz-clear` and `ds-size` complete with Task 5).**
- `ds-size` (depth kind): a 512x512 D24X8 surface with the back buffer: Z Clear `D3D_OK`; a far quad then a near
  quad overlapping it with `LESSEQUAL`: near wins where both are, in every lane.
- `sz-large`: a 512 D24S8 with a 64x64 render-target texture: depth-tested and stencil-tested quads behave as on
  an equal-size pair.
- `sz-clear` (the guard): a 512 D24X8 with a 512x512 render-target texture: draw a near quad (z 0.1) over all of
  it; whole Z Clear to 1.0; switch to a 64x64 texture with the depth kept, draw one depth-tested quad; back to
  the 512 texture: a quad at z 0.5 under `LESSEQUAL` shows at (400,400) (the Clear reached past the small pass).
- `sz-small`: a 128x128 D24X8 with the back buffer, a full-target Z-tested quad: (64,64) is the quad's colour and
  every call answers `D3D_OK`; (200,200), outside the depth-stencil's extent, is not asserted (mask 0: ruling 2).

**bl (13, Task 4).**
- `bl-alpha`: SRCALPHA/INVSRCALPHA, red at alpha 128 over blue: (128,0,127).
- `bl-add`: ONE/ONE, (64,64,64) twice over blue: (128,128,255).
- `bl-ops`: SUBTRACT, REVSUBTRACT, MIN, MAX in four cells over a (100,150,200) clear with source (150,100,50),
  ONE/ONE: (50,0,0), (0,50,150), (100,100,50), (150,150,200); MIN and MAX again with ZERO/ZERO: the same.
- `bl-factors`: cells over a clear whose colour and alpha differ ((40,80,120), alpha 200, on an A8R8G8B8 texture
  target): source x DESTCOLOR, source x INVDESTCOLOR, dest x SRCCOLOR, dest x INVSRCCOLOR, source x DESTALPHA,
  SRCALPHASAT: each cell's value from the D3D9 formula.
- `bl-both`: BOTHSRCALPHA and BOTHINVSRCALPHA as the source factor with a contrary DESTBLEND: as
  SRCALPHA/INVSRCALPHA and its reverse.
- `bl-destalpha`: DESTALPHA/INVDESTALPHA: on the back buffer after a Clear with alpha 0x40 (reads the stored
  alpha), on an X8R8G8B8 texture target (source x 1 + dest x 0) and on an A8R8G8B8 one.
- `bl-separate`: on an A8R8G8B8 target, colour SRCALPHA/INVSRCALPHA and alpha ONE/ZERO with
  `SEPARATEALPHABLENDENABLE`: colour blended, alpha the source's; with it off: alpha blended.
- `bl-factor`: BLENDFACTOR/INVBLENDFACTOR with `D3DRS_BLENDFACTOR` 0x80404040 then 0xffc0c0c0 between two quads of
  one pass; and as the alpha factors on an A8R8G8B8 target.
- `bl-stencil`: a stencil-tested quad (`EQUAL` 5), then a blended quad reading the blend factor with
  `STENCILENABLE` off, then the stencil-tested quad's twin with `STENCILREF` 5 unchanged: both stencil quads pass.
- `bl-off`: blend states left at SRCALPHA/ZERO/MAX with `ALPHABLENDENABLE` off: opaque.
- `bl-mask`: `COLORWRITEENABLE` red|alpha under ONE/ONE: only red moves.
- `bl-prog`: a ps_2_0 writing its constant (alpha 0.25) under SRCALPHA/INVSRCALPHA, and the fixed-function
  untransformed path (`fdraw`) at alpha 128.
- `bl-pass`: a blended quad reading the blend factor; a pass break; the same quad again elsewhere with no state
  call: the same colour.

**cl (10, Task 5).**
- `cl-rects`: `Clear(2, rects, TARGET, green)` over blue: both rects green, between them blue.
- `cl-count`: `Clear(0, rects, ..)`: nothing; `Clear(5, NULL, ..)`: the whole target.
- `cl-scissor`: test on, rect A: `Clear(0, NULL, TARGET|ZBUFFER, ..)` clears A only (colour probed; depth by a
  tested quad); with one rect crossing A: their overlap only.
- `cl-viewport`: viewport (64,64,128,128): `Clear(0, NULL, TARGET|ZBUFFER|STENCIL)` clears the viewport only; a
  rect crossing its edge is cut there.
- `cl-z`: `Clear(1, rect, ZBUFFER, z 0.2)` after a whole clear to 1.0: a quad at z 0.5 `LESSEQUAL` is hidden
  inside the rect and shows outside.
- `cl-stencil`: `Clear(1, rect, STENCIL, 7)` after a whole clear to 0: a quad under `EQUAL 7` shows inside only;
  on a D24X8 surface the same Clear answers `D3D_OK` and changes nothing.
- `cl-mask`: with `COLORWRITEENABLE` red only and blending ONE/ONE left on: a whole Clear and a rect Clear both
  store the full clear colour.
- `cl-order`: red quad; rect Clear to green over half of it; white quad over half of that: white over green over
  red where they stack.
- `cl-level`: a 64x64 render-target texture's level 1 (32x32): a rect Clear (8,8)-(24,24) and a viewport-bounded
  one; read back.
- `cl-bigz`: a 512 D24X8 with the back buffer and viewport (0,0,256,256): `Clear(ZBUFFER)` (a partial clear of
  the depth-stencil) then tested quads: as an equal-size pair gives.

**sr (4, Task 6).**
- `sr-blend`: `SRGBWRITEENABLE` on, back buffer, Clear to linear 0.5 grey, a white quad at alpha 128 under
  SRCALPHA/INVSRCALPHA: the linear blend's encoded value (not the encoded values' blend); the same on an A8R8G8B8
  texture target.
- `sr-notwin`: the same on a target without a twin (the first of `rt-formats`' formats mtld3d gives none): the
  shader path's value, equal in both runtimes.
- `sr-clear`: on: a whole Clear and a rect Clear to linear 0.5 both read 188; off: 128; a Clear with it off
  followed by draws with it on: both right.
- `sr-toggle`: one frame: a blended quad with it on, one with it off, one with it on again: each cell its own
  space's value.
