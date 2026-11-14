# DXMTRS C4b-3d3: blending, scissor, partial clears and a depth-stencil of another size on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, `Clear` takes rects, the scissor and a viewport smaller than the target, for
colour, depth and stencil; draws honour the scissor test and take a depth-stencil of another size than the colour
target; `D3DRS_SRGBWRITEENABLE` encodes in hardware, after the blender, on a target that has an sRGB view; and
draws blend (`D3DRS_ALPHABLENDENABLE`, factors, operations, separate alpha, the blend factor colour). Same pixels
as under mtld3d, which keeps its behaviour.

**Architecture:** third of C4b-3d's five slices (3d1 targets, 3d2 depth-stencil, done; **3d3**; 3d4 MRT; 3d5 MSAA
and the Wine suite sweep). Four features, each one DXMTRS commit that lifts its own refusals, in an order where
every commit is safe to run and every prefix is a pushable cut (R1 I2, R3 I3, R3 M9 set the order):

1. **Clears** (Task 3). The front end resolves a Clear per plane set (colour; depth-stencil) to `Whole` or a list
   of regions with host-tested functions that are mtld3d's own (its region list, its viewport clip and its cover
   test move to mtld3d-core and both runtimes call them), each judged against that attachment's own extent.
   `Whole` is today's Clear pass; regions are one `ClearRtv` pass (dxmt-core's quad clear, D3D11's `ClearView`) on
   that view alone. dxmt-core changes first (Task 2): the optimizer does not fold a whole-view Clear into a pass
   that draws to less than the view, and `ClearRtv`'s stencil-only clear gets a fragment that writes no depth.
2. **Scissor and size** (Task 4). A draw carries its resolved scissor (the scissor rect with the test on, the
   drawn viewport with it off, as mtld3d resolves it); d3d9-rt clamps it to the pass's extent and records it at the
   first draw of each pass and on a change. The bound depth-stencil is attached whatever its size; the pass's
   extent is the minimum of the colour target's and the depth-stencil's, which is what Metal gives mtld3d.
3. **Hardware sRGB** (Task 5). A target with an sRGB twin (the back buffer, BGRA8/RGBA8 render targets) is
   attached through the twin's view by a draw or Clear made with `SRGBWRITEENABLE` on: the key names the twin's
   format, the pixel stage stops encoding, a Clear stores the linear colour. d3d9-rt keeps no sRGB state: the
   key's colour format and the Clear's argument pick the view, and a pass on the other view ends first.
4. **Blending** (Task 6). `PipelineKey` gains `blend: Option<CoreBlend>`, mtld3d's effective blend (its collapse
   rules, its destination-alpha clamp on a target without alpha) in mtld3d's `BlendFactor`/`BlendOperation`;
   d3d9-rt maps them to the wire's numbers by an explicit match and fills `colors[0]`. The blend factor colour is
   record 17 (with the draw's stencil reference), on draws whose key reads it.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-core, mtld3d-core, d3d9-rt, the d3d9.dll front end, mtld3d's unix
encoder for one moved function), C++ (`d3d9_triangle`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line, 3d1's and 3d2's
paragraphs). Maps: `build/c4b3d/map-3d3.md` (this slice; cited as M §n and its hazards as F.n), `build/c4b3d/map.md`,
`build/c4b3d/map-3d2.md`. Reviews: `build/c4b3d/s3/review-{1-design,2-core,3-exec}.md` (folded; dispositions at
the end, cited as R1-R3).

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check; a
  core-only struct field or private helper is cfg'd in the struct, in every literal and at its definition. Three
  pieces of mtld3d's code move or split without a change of behaviour, each with its unit tests and the default
  lanes (the Mac's Wine conformance included, Task 7): `device_clear`'s region list and `intersect_d3d_rects`
  (front end → mtld3d-core `clear_regions`), `clip_rect_to_viewport` (unix encoder → mtld3d-core), and
  `effective_blend` (its body becomes a function of `PipelineRsBits`). `region_covers_extent` becomes `pub`.
- **Two dxmt-core changes, in Task 2, and only those:** the area guard in `encoder/optimize.rs` and the
  stencil-only fragment choice in `library/clear_rtv.rs`. They reach D3D10-12 (D3D12 keeps its own optimizer, R2),
  so dxmt-core's and d3d11's tests, the x64 `dxmt/check.sh` and the VM's `vm/dxmt/gate.sh` are pre-push gates here.
  dxmt-fakes does not change. Should the stencil-only quad still not build on Metal (Task 3's lane), the fix stays
  inside `clear_rtv.rs` and `encoder/gaps.rs`'s `set_depth_stencil_attachment` (ruling 11).
- `tools/rust-check.sh` passes after every DXMTRS commit; a d3d9-rt or mtld3d-core API change carries its front-end
  call sites and every literal of a changed struct in the same commit (the compiler lists them; `CoreTextureDesc`,
  `DrawData` and `PipelineKey` literals are in rt's `fixture.rs`, `samplers.rs` and tests, the front end's
  `texture.rs`, `device.rs`, `direct3d9.rs`, `core_draw.rs`, and `pipeline_key/tests.rs`). New files are `git
  add`ed before it; a new or removed Clone/Copy derive needs `sh tools/audit.sh --update-derives`.
- No new dependency (d3d9-rt reaches mtld3d's blend enums through a re-export of mtld3d-core, which it already
  depends on). No `HashMap`. No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body; lines of
  100 columns at most. **No struct gains a `bool` field and no function a fourth `bool` parameter** (CONVENTIONS
  "Booleans pack"; clippy's limits): this plan adds none.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`, base bd2ab193) and `build/c4b3c/mn` (branch
  `c4b3d-mn`, base 22cb7c0), cargo target `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s3/gates`
  (`head.sh` there; state names `c4b3d3-*`; `jq0.sh`, `jq1.sh` are taken), logs and reviews in `build/c4b3d/s3`.
  Builds and GPU jobs under the pipeline lock, `$D` and the main checkout restored by each job's trap. No make in
  a worktree. A job tests **committed** tips; a job script that has run is never edited (a changed job is a new
  file); each run of the per-task lane takes its own folder (`JT=jt3` ...).
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; crate tests as `tools/rust-check.sh`'s test leg
  runs them (the `dynamic_lookup` rustflags for d3d9-rt, dxmt-core and d3d11; `--features d3d11/front-end` for
  d3d11); mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x300-0x33f** here, one a test (Task 3 Step 0 greps that none is taken).
- Every DXMTRS commit is safe to run under core: a refusal is lifted only in the commit that makes its calls
  correct. MacNeutron's core lanes are red between Task 1's rows and Task 8's pin by design; should the slice stop
  at an earlier task, the rows of the tasks not done leave the lists in the commit that pins.
- Per task one lane (the Mac's `d3d9-check` on default and core, job `jt`); full gates once, in Task 7. Benches and
  probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents too)
  while the x64 `dxmt/check.sh` or a bench runs.

## Scope

In: `Clear` with rects (any `count`), under the scissor, and bounded by a viewport that does not cover the
attachment, for `TARGET`, `ZBUFFER` and `STENCIL`; `D3DRS_SCISSORTESTENABLE` and `SetScissorRect` for draws; draws
and clears with a depth-stencil larger or smaller than the colour target; `D3DRS_SRGBWRITEENABLE` in hardware on
targets with an sRGB twin; `D3DRS_ALPHABLENDENABLE`, `SRCBLEND`, `DESTBLEND`, `BLENDOP`,
`SEPARATEALPHABLENDENABLE`, `SRCBLENDALPHA`, `DESTBLENDALPHA`, `BLENDOPALPHA`, `BLENDFACTOR`; the two inputs from
C4b-3d2's final review (the pre-transformed clamp follows the bound depth-stencil, not the key; `Clear(STENCIL)`
of a depth-only depth-stencil of another size does nothing).

Out (owners): more than one colour target, per-target write masks and blending (3d4); multisampling,
alpha-to-coverage, `CreateOffscreenPlainSurface`, the Wine suite sweep (3d5); `StretchRect`, `ColorFill`, `Reset`,
render scale, fullscreen, the bound depth-stencil sampled by a draw that uses its planes, vertex-sampler depth,
dynamic depth locks, queries (C4c); per-draw state dedup beyond the scissor's, and a Clear whose view differs from
the next pass's (ruling 7) (C4d); `D3DBLEND_SRCCOLOR2`/`INVSRCCOLOR2` (Zero under mtld3d, so Zero here); the 1x1
"RT0 drop".

## Review Focus

1. **A scissor never leaks.** Metal keeps an encoder's scissor; the optimizer merges passes; `open_pass` itself
   ends a pass when a draw's depth kind changes. A draw after a scissored draw, in the same pass or in the next
   one, draws through its own rect: test off after test on, a rect changed between draws, a pass break between two
   draws of one rect, a depth-less draw between two depth draws. Rows `sc-pass`, `sc-break`; rt
   `the_scissor_is_recorded_at_each_pass_start`, `a_depth_kind_switch_records_the_scissor_again`.
2. **A scissor Metal rejects is never recorded.** The rect is clamped to the pass's extent (the minimum of the
   attachments), and a draw whose clamped rect is empty records nothing: a rect outside the target, inverted,
   negative, larger than the target, a viewport off the target with the test off, a small depth-stencil under a
   large target. Rows `sc-empty`, `sc-clamp`, `sz-small`; every core launch once under the Metal validation layer
   with no report (Task 7).
3. **A Clear clears exactly its area**: `count == 0` with rects clears nothing; null rects ignore `count`; rects
   are clipped by the scissor and the viewport; a region that covers the attachment is the whole clear, judged
   against the depth-stencil's own extent for Z and stencil; the write mask, blending and the scissor of earlier
   draws do not reach a clear quad; a Clear between draws keeps the order; a whole Z Clear of a depth-stencil
   larger than the next pass's area clears all of it (the area guard); a stencil-only rect clear writes stencil
   and leaves depth. Rows `cl-*`, `sz-clear`, `ds-size`; mtld3d-core `clear_area_*`, `clear_regions_*`; dxmt-core
   `a_clear_is_not_folded_into_a_smaller_pass`, `a_stencil_only_clear_draws_no_depth`.
4. **The pipeline blends what D3D9 asked.** The wire's factor numbers are not mtld3d's (F.1: destination colour
   and alpha swap under a cast); the back buffer counts as alpha-bearing and an X8 render target does not (F.3);
   a disabled blend with stale factors is one pipeline; MIN/MAX ignore factors; BOTH* set both sides; sRGB write
   blends in linear space on a twin target. Rows `bl-factors`, `bl-destalpha`, `bl-off`, `bl-ops`, `bl-both`,
   `sr-blend`; rt `wire_blend_numbers`.
5. **The blend factor colour reaches every draw that reads it**, after a pass break too, and record 17's stencil
   reference is the draw's own (F.4). Rows `bl-factor`, `bl-pass`, `bl-stencil`.

---

### Task 1: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`.

- [ ] **Step 1: write** the `blend` launch kind (`d3d9_triangle blend [strict]`): `main`'s device with the auto
  D24S8 (`if (depth || blend || depthBench)`), the depth kind's prologue (`rtSurf`, `sysSurf`, `dsSurf`), budget
  180 s, and per row `reset() && depthReset() && blendStart() && r.run()`, where `blendStart()` clears Z to 1.0
  and stencil to 0 over the whole depth-stencil and sets `ZENABLE FALSE` (a row that tests depth turns it on: R3
  I1, R1 M7). The 36 rows of the appendix (`cl` 11, `sc` 6, `sz` 3, `sr` 3, `bl` 13). New helpers: `scissor(x1,
  y1, x2, y2)`, `clearRects(count, rects, flags, color, z, stencil)`, `alphaOf` (a `readTarget` + `want` with
  mask `0xff000000`: `frames()` masks alpha). `reset()` gains `ALPHABLENDENABLE FALSE`, `SRCBLEND ONE`,
  `DESTBLEND ZERO`, `BLENDOP ADD`, `SEPARATEALPHABLENDENABLE FALSE`, `SRCBLENDALPHA ONE`, `DESTBLENDALPHA ZERO`,
  `BLENDOPALPHA ADD`, `BLENDFACTOR 0xffffffff`, `SCISSORTESTENABLE FALSE`, and stage 0-7 `ALPHAARG1 TEXTURE`,
  `ALPHAARG2 CURRENT` (the rect is reset by its `SetRenderTarget`). Depth kind: `ds-size` becomes a result in
  every lane, and `ds-rhwclip` is new (31 rows; appendix). Headers: the kind, the counts, "the depth launch's
  three refused calls".
- [ ] **Step 2: scripts.** `d3d9-check.sh`: the header; `launch ours fresh blend 240 blend $strict` and `check
  blend "<rows>"` before the mtld3d-lane refusal grep; `refusals_ok` gets an arm `blend)` that requires **zero**
  refusal lines (an empty set is one empty line to `wc -l`: R3 C1, R1 I4); `depth_refusals` loses the two "of
  another size" lines. `d3d9-gate.sh`: the header, `runtime()`'s comment and a zero-line arm, `for r in
  bl64-fresh bl32-fresh` row loops, the core block. `d3d9.sh`: the header, two `tri` lines (`bl64-fresh`,
  `bl32-fresh`, 240 s) right after `dp32-fresh`, and the row counter's class becomes `[a-z0-9-]*`.
- [ ] **Step 3: compile** `d3d9_triangle.cpp` with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall
  -Wextra`) — Expected: no diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle blend rows in every D3D9 lane (DXMTRS C4b-3d3)`.
- [ ] **Step 5: the controls.** Job `ja` (DXMTRS bd2ab193, this commit; `DXMT_D3D9_CORE=1` build, `dxmt-tests`,
  `d3d9-check.sh` default, then `D3D9_RUNTIME=core`). Expected: default `d3d9-check: ok` with every `blend` row,
  `ds-size` and `ds-rhwclip` ok; core: the `blend` launch red on every row that clears partially, scissors, binds
  a depth-stencil of another size or blends, `blend-runtime` red (refusal lines), `ds-size`, `ds-rhwclip` and
  `depth-runtime` red (five lines against three). A row red on the default runtime is a wrong row or a defect of
  mtld3d: fix the row; a defect is ledgered as a ruling, the row then asserts what both runtimes should give and
  is listed as red-on-default until the defect's own fix (this slice's only if it is a one-line fix).
- [ ] **Step 6: the VM's default lanes once** (`jcr`: old WoW64, x64 and x86, as C4b-3d2's): `bl64-fresh` and
  `bl32-fresh` green inside their budget — Expected: ok; a budget miss raises the kind's budget before any core
  work.

### Task 2: dxmt-core (the area guard; the stencil-only quad)

**Files:** Modify `src/dxmt/src/encoder/optimize.rs` and `optimize/tests.rs`; `src/dxmt/src/library/clear_rtv.rs`
and `clear_rtv/tests.rs`.

- [ ] **Step 1: failing tests.** `a_clear_is_not_folded_into_a_smaller_pass`, with a new sized, format-taking
  texture helper (the file's `texture(at)` is a 64x64 RGBA8, whose depth clear would read as a colour clear) and a
  pass helper that sets the depth target (`LOAD`, `dsv_planar_flags`) and `render_target_width/height`: a
  `WMT_PIXEL_FORMAT_DEPTH32_FLOAT` 128x128 view cleared, then a pass attaching it at 64x32 — two passes, the
  render pass loads; at 128x128 — one pass, load action CLEAR; at 0x0 (unset) — one pass; a colour Clear of a
  128x128 view into a 64x64 pass — two passes. `a_stencil_only_clear_draws_no_depth`: `kind(DEPTH32_FLOAT_STENCIL8,
  2)` is `Kind::Float`, `kind(.., 1)` and `kind(.., 3)` are `Kind::Depth`. Expected: the first and last guard
  cases fail (folded); the kind test fails (`Depth`).
- [ ] **Step 2: implement.** In `clear_into_render`, first: `if render.render_target_width != 0 &&
  render.render_target_height != 0 && (render.render_target_width < clear.width || render.render_target_height <
  clear.height) { return false; }`; its doc and the module doc (which says the behaviour is the C++'s) state the
  deviation: a load action reaches the pass's area only. In `clear_rtv.rs`, `kind` answers `Depth` only when
  `dsv_flag & 1 != 0` (a stencil-only pipeline has no depth format, and Metal rejects a fragment that writes
  depth without one; the float fragment's colour has no attachment and is dropped).
- [ ] **Step 3: run** dxmt-core's and d3d11's tests, `sh tools/rust-check.sh` — Expected: ok, no other test moved.
- [ ] **Step 4: commit** `dxmt-core: no fold of a whole-view Clear into a smaller pass; a stencil-only quad clear
  writes no depth`.

### Task 3: clears (mtld3d-core, unix, d3d9-rt, front end)

**Files:** Create `src/d3d9/core/src/clear_area.rs` and `clear_area/tests.rs` (declared in `lib.rs`); modify
`src/d3d9/core/src/passes.rs` (`region_covers_extent` pub), `pipeline_key.rs` (+ tests);
`src/d3d9/unix/src/encoder.rs`; create `src/d3d9/rt/src/device/clear.rs` and `clear/tests.rs` (`mod clear;`; the
two whole clears move there from `recorder.rs` and `depth.rs` with their tests); modify rt `device/depth.rs`;
`src/d3d9/src/device.rs` (`device_clear`), `device/core_arms.rs`.

**Interfaces (produces):**
- mtld3d-core `clear_area.rs`:
  `pub fn intersect_d3d_rects(a, b: (i32, i32, i32, i32)) -> Option<(i32, i32, i32, i32)>` (moved);
  `pub fn clear_regions(rects: Option<Vec<(i32, i32, i32, i32)>>, scissor: Option<(i32, i32, i32, i32)>) ->
  Option<Vec<(i32, i32, i32, i32)>>` (`device_clear`'s block: `rects` is `None` for a null pointer, `scissor` is
  `Some(scissor_region(..))` with the test on);
  `pub fn clip_rect_to_viewport(rect: (i32, i32, i32, i32), vp: (u32, u32, u32, u32)) -> Option<(u32, u32, u32,
  u32)>` (moved from unix `encoder.rs:1613`, which imports it);
  `pub const fn clamp_rect(rect: [u32; 4], extent: (u32, u32)) -> Option<[u32; 4]>` (`x = min(x, W)`, `width =
  min(width, W - x)`, the same in y; `None` when either side is 0);
  `#[derive(Debug, PartialEq, Eq)] pub enum ClearArea { Whole, Regions(Vec<[u32; 4]>) }` (`x, y, width, height`,
  none empty; an empty list clears nothing);
  `pub fn clear_area(regions: Option<&[(i32, i32, i32, i32)]>, viewport: (u32, u32, u32, u32), extent: (u32,
  u32)) -> ClearArea`: `None` is the viewport as the one region, the others `clip_rect_to_viewport`ed; any region
  for which `region_covers_extent(region, extent)` makes it `Whole`; otherwise each is `clamp_rect`ed to `extent`
  and the non-empty ones are the list.
- mtld3d-core `pipeline_key.rs`: `pub const fn drawn_rect(v, target) -> (u32, u32, u32, u32)` (now pub);
  `viewport_covers` and its test are deleted (no caller left).
- d3d9-rt: `CoreDevice::depth_size(&self) -> Option<(u32, u32)>` (the bound depth-stencil's level);
  `clear_depth_stencil(planes, depth, stencil)` clears the bound depth-stencil whole **whatever the colour
  target's size**; `pub fn clear_rects(&mut self, rects: &[[u32; 4]], rgba: [f32; 4])` and `pub fn
  clear_depth_stencil_rects(&mut self, rects: &[[u32; 4]], planes: u32, depth: f32, stencil: u8)`: nothing for an
  empty list or no plane the format has; `close_pass()`; one closure: `enc.with_clear_rtv(|crtv, enc| {
  crtv.begin(enc, Arc::clone(&texture), key, 0, planes); each rect as a WMTScissorRect: crtv.clear_color(enc,
  rect, 0, rgba) | crtv.clear_depth_stencil(enc, rect, depth, stencil); crtv.end(enc) })` on the target's (resp.
  the depth-stencil's) own view; depth clamped as the whole clear's. Draws keep `depth_state()` until Task 4.
- Front end: `device_clear` calls `clear_regions((!rects.is_null()).then(|| clear_target_rects(count, rects)),
  scissor_on.then_some(scissor))`. `core_arms::clear(this, count, rects, flags, color, z, stencil)`: mtld3d's
  `INVALIDCALL` first; the same `clear_regions`; `viewport = drawn_rect(&dev.viewport(), target)`; for `TARGET`,
  `clear_area(regions, viewport, target)`; for the depth flags (planes masked by the format first: a stencil flag
  on a depth-only format drops out, and no plane left is no clear), `clear_area(regions, viewport, depth_size)`.
  No refusal is left in it.

- [ ] **Step 0:** `/usr/bin/grep -rn '0x3[0-3][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src` — Expected: no
  device handle in 0x300-0x33f.
- [ ] **Step 1: failing tests.** mtld3d-core: `clear_regions_follow_device_clear` (null rects: `None` without the
  scissor, the scissor's one region with it, an empty list for an empty scissor; a list: itself, clipped by the
  scissor, emptied; `Some(vec![])` stays empty), `clip_rect_to_viewport_cases` (the unix tests' cases, moved if
  any exist there), `clamp_rect_cases`, and `clear_area_*`: `none_with_a_covering_viewport_is_whole`,
  `none_with_a_small_viewport_is_that_region`, `an_oversized_viewport_covers`, `no_rects_clear_nothing`,
  `rects_are_clipped_to_the_viewport`, `a_rect_covering_the_extent_is_whole`, `a_rect_past_the_extent_is_clamped`,
  `a_larger_depth_is_not_covered_by_the_colour_viewport` (0,0,256,256 against 512x512 → that region),
  `inverted_and_negative_rects`. rt (`clear/tests.rs`; the fakes make `with_clear_rtv` live, R2):
  `a_rect_clear_is_a_pass_of_its_own` (draw, rect clear, draw: three encoders in order, the middle one with
  `render 36` and `render 19 .. vertex_count=3` once a rect), `a_rect_depth_clear_attaches_the_depth_alone` (no
  `target0` line), `a_stencil_rect_clear_of_a_depth_only_format_records_nothing`, `an_empty_list_records_nothing`,
  `a_whole_depth_clear_of_another_size_is_recorded` (128x128 against the 64x32 back buffer: today nothing is
  recorded). No test counts depth-stencil states across a rect clear (the first one makes four). Expected: do not
  compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** mtld3d-core's, the unix crate's and d3d9-rt's tests, `rust-check` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: Clear with rects, the scissor and a partial viewport on the core runtime`.
- [ ] **Step 5: lane** `JT=jt3` — Expected: default ok. Core: every `cl-*` row ok but the parts that draw under
  the scissor test or with a depth-stencil of another size (`cl-scissor`, `cl-bigz`, `cl-smallz`); `sc-*` (test
  on), `sz-*`, `bl-*` (but `bl-off`), `sr-blend`, `sr-toggle`, `ds-size`, `ds-rhwclip` red; `sr-clear` ok.
  `cl-stencil` green is the proof of Task 2's quad on Metal; red there with no pipeline is ruling 11's case.

### Task 4: the scissor, and a depth-stencil of any size (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ tests); `src/d3d9/rt/src/device.rs`, `device/depth.rs`,
`device/draw.rs`, `device/recorder.rs` and their tests, `device/fixture.rs`; `src/d3d9/src/device/core_draw.rs`,
`core_arms.rs`.

**Interfaces (produces):**
- mtld3d-core: `pub fn core_scissor(test: bool, rect: D3DRECT, v: &D3DVIEWPORT9, target: (u32, u32)) -> [u32; 4]`
  (`scissor_snapshot_rect(rect).map(u32::from)` with the test on, `drawn_rect` with it off);
  `core_key_depth(bound: Option<u32>, samples_bound: bool, uses_plane: bool) -> Result<u32, CoreRefusal>` (no
  `attachable`); `DrawFlags::SCISSOR`, `CoreRefusal::Scissor` and `CoreRefusal::DepthSize` removed (the
  `refusals` array and its exhaustive match in `core_draw_kinds_cover_every_case` follow).
- d3d9-rt: `CoreDevice::depth_format(&self) -> Option<u32>` replaces `depth_state()`; `DepthState` removed;
  `DrawRefused::Depth` **stays**, for a key that names a depth format the bound depth-stencil does not have (none
  bound, or another format: `forgetting_the_bound_depth_unbinds_it` keeps its assertion); `DrawData { .., pub
  scissor: [u32; 4] }` (the fixture's default is `[0, 0, u32::MAX, u32::MAX]`, which the clamp makes the pass's
  extent); `CoreDevice { .., pass_scissor: Option<[u32; 4]> }`, set to `None` in exactly one place: where
  `open_pass` begins a new pass.
- Behaviour: `resolve_depth` resolves the bound depth-stencil whatever its size; `open_pass` sets
  `render_target_width/height` to the minimum of the colour target's and the attached depth-stencil's; `draw_up`
  clamps `draw.scissor` to that extent **before anything else** and records nothing and answers `Ok(())` when it
  is empty; **after `open_pass`** it compares the clamped rect with `pass_scissor` and, when they differ, records
  `WmtcmdRenderSetscissorrects` (one rect, `allocate_cpu`) after the viewport. The front end's clamp lane is
  `rhw_depth_clamp(layout.has_rhw(), bound.is_some(), zenable)` (mtld3d decides on the attached depth-stencil,
  which a draw that samples it still has).

- [ ] **Step 1: failing tests.** mtld3d-core: `core_scissor_follows_mtld3d` (test on: a plain rect, a negative
  origin clamps to 0, an inverted rect is empty at its origin; test off: the viewport, and the target for an
  unset viewport), `core_key_depth_covers_every_case` re-tabled, `core_draw_kinds_cover_every_case` without the
  scissor case. rt: `a_depth_of_another_size_attaches` (128x128 and 16x16 against the 64x32 back buffer: the pass
  has the depth lines; its `target0` size is 64x32, resp. 16x16) replacing `a_depth_of_another_size_is_detached`
  and every other `depth_state()` assertion (`depth/tests.rs:294-330`, `:453`),
  `the_scissor_is_recorded_at_each_pass_start` (two draws of one rect: one `render 14`; a `close_pass` between
  them: two), `a_scissor_change_is_recorded`, `a_depth_kind_switch_records_the_scissor_again` (a depth draw, a
  depth-less draw, a depth draw, one rect: three passes, three `render 14`), `the_scissor_is_clamped_to_the_pass`
  (a 300x300 rect on 64x32 records 64x32; with a 16x16 depth, 16x16), `an_empty_scissor_draws_nothing` (no pass,
  no pipeline built). Expected: do not compile.
- [ ] **Step 2: implement.** Front end: `DrawData.scissor = core_scissor(rs[SCISSORTESTENABLE] != 0,
  dev.scissor_rect(), &viewport, target)`; `key_depth` reads `depth_format()`.
- [ ] **Step 3: run** the three crates' tests and `rust-check` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: the scissor test and a depth-stencil of any size on the core runtime`.
- [ ] **Step 5: lane** `JT=jt4` — Expected under core: every `cl-*`, `sc-*`, `sz-*` row, `ds-size` and
  `ds-rhwclip` ok, `depth-runtime` ok (three lines); `bl-*` (but `bl-off`), `sr-blend`, `sr-toggle` red and
  `blend-runtime` red on the blending refusal alone.

### Task 5: hardware sRGB write (d3d9-rt, mtld3d-core, front end)

**Files:** Modify `src/d3d9/rt/src/device.rs`, `device/target.rs`, `device/textures.rs`, `device/swapchain.rs`,
`device/recorder.rs`, `device/clear.rs`, `device/draw.rs` and tests, `device/fixture.rs`;
`src/d3d9/core/src/pipeline_key.rs` (+ tests); `src/d3d9/src/device/core_draw.rs`, `core_arms.rs`, `device.rs`
(`CreateRenderTarget`'s literal), `texture.rs`.

**Interfaces (produces):**
- d3d9-rt: `CoreTextureDesc { .., pub srgb_format: u32 }` (the sRGB twin's `WMTPixelFormat`, 0 for none; non-zero
  only with `view_twins`); the back buffer is created with `WMT_TEXTURE_USAGE_PIXEL_FORMAT_VIEW` and has the twin
  `WMT_PIXEL_FORMAT_BGRA8_UNORM_SRGB`; `Shape { .., srgb_format: u32 }`;
  `CoreDevice::target_desc(&self, srgb_write: bool) -> Option<((u32, u32), u32)>` (the format a pass made with
  that state attaches: the twin's when the target has one and `srgb_write`; its one caller is `core_draw.rs`'s
  `core_target_of`); `clear(rgba, srgb_write: bool)` and `clear_rects(rects, rgba, srgb_write: bool)`;
  `CoreDevice { .., open_format: u32 }` (the colour format the open render pass attached).
- Behaviour: `draw_up` attaches the view of `key.color_format`, which must be the target's stored format or its
  twin (the debug assert says so); `open_pass` reopens when the open pass's format differs; the tile-barrier key
  names the attached format; a Clear and a rect clear take the twin's view when asked and the target has one; a
  read-back and Present keep the stored format's view.
- mtld3d-core: `pub fn core_srgb_variant(variant: VariantKey, hardware: bool) -> VariantKey` (removes
  `VariantFlags::SRGB_WRITE` when `hardware`); `core_ps_variant` is unchanged.
- Front end: `srgb = rs[SRGBWRITEENABLE] != 0`; `target_desc(srgb)`; `hardware = srgb && format != the stored
  format` (rt's `target_desc(false)`); the variant through `core_srgb_variant`; the Clear colour is CPU-encoded
  only when not `hardware`; a colour texture's and `CreateRenderTarget`'s `srgb_format` is
  `pixel_format.srgb_twin()`'s (and `view_twins` true with it).

- [ ] **Step 1: failing tests.** rt: `srgb_write_attaches_the_twin` (the pass's colour line and `pso color0=`
  name the twin; a key with the stored format: the stored view), `a_format_switch_ends_the_pass`,
  `a_target_without_a_twin_keeps_its_format`, `a_clear_and_its_pass_share_the_view` (both on the twin: one pass),
  `a_clear_on_the_other_view_is_its_own_pass` (ruling 7's cost, pinned), `read_back_reads_the_stored_view`.
  mtld3d-core: `core_srgb_variant_rules`. Expected: do not compile.
- [ ] **Step 2: implement; run** tests and `rust-check`.
- [ ] **Step 3: commit** `d3d9: sRGB write in hardware on the core runtime's targets that have an sRGB view`.
- [ ] **Step 4: lane** `JT=jt5` — Expected: as after Task 4, with `srgb`, `rt-srgb`, `tex-srgb`, `tex-vtxsrgb`
  and `sr-clear` ok in both runtimes.

### Task 6: blending (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_state.rs`, `pipeline_key.rs` and their tests;
`src/d3d9/rt/src/device/draw.rs` and tests, `device/fixture.rs`; `src/d3d9/src/device/core_draw.rs`.

**Interfaces (produces):**
- mtld3d-core `pipeline_state.rs`: `pub use mtld3d_shared::mtl::{BlendFactor, BlendOperation};` (d3d9-rt has no
  other path to them: R2 C1, R3 I5); `effective_blend(s)` becomes `effective_blend_rs(&s.rs,
  s.has_color_output())` (private, same body);
  `#[derive(Clone, Debug, PartialEq, Eq, Hash)] pub struct CoreBlend { pub src: BlendFactor, pub dst: BlendFactor,
  pub op: BlendOperation, pub src_alpha: BlendFactor, pub dst_alpha: BlendFactor, pub op_alpha: BlendOperation }`
  with `pub const fn reads_blend_color(&self) -> bool` (any factor `BlendColor` or `OneMinusBlendColor`);
  `pub fn core_blend(rs: &[u32; RENDER_STATE_COUNT], target_has_alpha: bool) -> Option<CoreBlend>`: the six
  states through `render_state::enum_value` into a `PipelineRsBits` (flags `BLEND_ENABLE`,
  `SEPARATE_ALPHA_BLEND` as the draw snapshot sets them, device.rs:13768-13796), `effective_blend_rs(.., true)`,
  `None` when not enabled, else each of the four factors through `d3d_to_metal_blend_rt(f, target_has_alpha)` and
  each operation through `d3d_to_metal_blend_op`, as `key_from_snapshot` does without extra targets.
- `PipelineKey { .., pub blend: Option<CoreBlend> }`, `PipelineSource { .., pub blend: Option<CoreBlend> }`
  (forwarded by `pipeline_source`; the key's doc no longer says blending is refused); `DrawFlags::BLENDING` and
  `CoreRefusal::Blending` removed.
- d3d9-rt: `DrawData { .., pub blend_color: Option<[f32; 4]> }`; private `const fn wire_factor(f: BlendFactor) ->
  u8` and `wire_op(o: BlendOperation) -> u8` (explicit matches onto `WMT_BLEND_FACTOR_*` /
  `WMT_BLEND_OPERATION_*`, as unix `metal/texture.rs:848-862`). `build_pipeline` sets
  `colors[0].blending_enabled = 1` and the six fields for `Some(blend)`; `draw_up` records
  `WmtcmdRenderSetblendcolor { r#type: WMT_RENDER_COMMAND_SET_BLEND_FACTOR_AND_STENCIL_REF, .., stencil_ref:
  draw.stencil_ref }` after the depth-stencil state when `draw.blend_color` is `Some`.
- Front end (wiring, no rt state): `has_alpha` of the bound colour target is `true` for a back buffer
  (`is_back_buffer()`, as mtld3d's frame start and its back-buffer bind have it) and
  `map_d3d_format(format).is_none_or(FormatMapping::has_alpha)` for any other surface (mtld3d's,
  device.rs:10841); `blend = core_blend(&rs, has_alpha)`; `blend_color = blend.reads_blend_color().then(||
  d3dcolor_to_rgba_f32(rs[D3DRS_BLENDFACTOR]))`.

- [ ] **Step 1: failing tests.** mtld3d-core: `core_blend_follows_effective_blend` (off with stale factors →
  `None`; SRCALPHA/INVSRCALPHA; separate alpha off → alpha equals colour; on → its own; BOTHSRCALPHA source; MAX →
  One/One; DESTALPHA with and without target alpha → DestinationAlpha, One, in the colour and the alpha slot;
  INVDESTALPHA → Zero without; out-of-range DWORDs take the defaults), `reads_blend_color_cases`, the key table
  grows by `blend` (`[Case; 18]`), `core_draw_kinds_cover_every_case` loses the blending case and its refusal;
  the existing `effective_blend` tests pass unchanged. rt: `wire_blend_numbers` (all 13 factors and 5 operations
  against the `WMT_*` constants; destination colour is 6 and destination alpha 8),
  `the_blend_colour_is_recorded_with_the_stencil_ref` (`render 17` after `render 16`, `stencil_ref` equal; absent
  for `blend_color: None`), `a_blend_is_its_own_pipeline` (two keys differing in `blend` build two pipelines).
  Expected: do not compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** tests, `rust-check`, `audit.sh --update-derives` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: blending on the core runtime`.
- [ ] **Step 5: lane** `JT=jt6` — Expected: both runtimes `d3d9-check: ok`: all 36 `blend` rows, the depth
  launch's log with exactly its three refusal lines, the blend launch's with none.

### Task 7: gates, mutants, probe

- [ ] **Step 1: `jq`** (Mac): `DXMT_D3D9_CORE=1` build; `dxmt-tests`; `d3d9-check.sh` default, `D3D9_BACKEND=msl`,
  `D3D9_RUNTIME=core`; `dxmt/mtld3d-conformance.sh D3D9_RUNTIME=core --only stateblock` equal to default; **every
  core launch** (`shaders`, `state`, `textures`, `fixed`, `targets`, `depth`, `blend`) once under the Metal
  validation layer (`MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog`, counted as C4b-3d2's `jqv` did) with
  no report and the rows still ok. Expected: all ok.
- [ ] **Step 2: mutants** (`jm`, one build and one core `d3d9-check` each; each red on its row, and on no row of
  another launch): (a) `wire_factor`: `DestinationColor => WMT_BLEND_FACTOR_DESTINATION_ALPHA` → `bl-factors`;
  (b) the front end's `has_alpha` is `true` for every target → `bl-destalpha`; (c) `open_pass` does not reset
  `pass_scissor` → `sc-break`; (d) the area guard as `if false && ..` → `sz-clear`; (e) `clear_area` clips rects
  to the extent, not the viewport → `cl-viewport` (and `cl-level`, same launch); (f) d3d9-rt reads every
  `srgb_format` as 0 (the shader path runs whole) → `sr-blend` and `sr-toggle`; (g) the front end judges the
  depth planes against the colour target's extent → `cl-bigz`.
- [ ] **Step 3: plain build** (`jr`): no `DXMT_D3D9_CORE`; translator key and d3d9.dll imports unchanged;
  `d3d9-check.sh` default ok; the Mac's default-runtime Wine conformance (`dxmt/mtld3d-conformance.sh`, as
  C4b-3d2's `ja` ran it) with no new failing site; **x64 `dxmt/check.sh`: all passed** (idle Mac).
- [ ] **Step 4: VM** (`jc`): `vm/dxmt/gate.sh` (D3D10-12 over the changed dxmt-core) ok; default and core lanes,
  old and new WoW64, x64 and x86: `d3d9-gate` ok (a default lane short only on executed-test floors is rerun once
  before it is blamed).
- [ ] **Step 5: probe** (`jp`, once, idle Mac): this tip on its own full build, `--bench 2000 <0|600> tex` and
  `tex depth`, mtld3d and core; written to `build/c4b3d/s3/probe.md` beside `jq1`'s retake. A single run's spread
  is 0.32 ms a frame there (mtld3d `tex`, 1.50 against 1.82): a core `tex` or `texdepth` delta against 63f9fda0
  below that is reported as not measurable.

### Task 8: review, spec, pin, push

- [ ] Whole-branch review (most capable model; the plan's Review Focus verbatim; the ledger's rulings); one fix
  pass, each fix RED→GREEN with the suite green; minors deferred to the ledger.
- [ ] Spec §5: a C4b-3d3 paragraph after 3d2's. Execution notes at this plan's end. `dxmt/pins` to the DXMTRS tip.
- [ ] Privacy scan of both ranges; push DXMTRS `c4b3d` → `main`, then MacNeutron `c4b3d-mn` → `main`; post-push
  check from main (plain build, tests, `d3d9-check`, x64 check); CI; scratch of this slice deleted by full path;
  memory.

## Rules check

- **dxmt-core changes** (the first of C4), both in Task 2, each with its unit test: four lines in
  `clear_into_render` (the fold is wrong for every front end: a load action reaches the pass's area only, M §A.3,
  F.6; D3D11 accepts a depth view larger than its render targets and has the same hole) and one condition in
  `clear_rtv.rs`'s `kind` (a path D3D11 never takes: it passes `dsv_flag` 0 or 1). Gated by dxmt-core's and
  d3d11's tests, the x64 check and the VM's D3D10-12 gate before the push.
- **Moves of mtld3d's code, no duplicate:** the Clear region list and `intersect_d3d_rects` (front end →
  `clear_regions`, mtld3d-core, with tests: "pure functionality is factored out of d3d9", and no helper takes
  `DeviceInner`), `clip_rect_to_viewport` (unix → mtld3d-core), `region_covers_extent` (pub), `effective_blend`'s
  body (one `effective_blend_rs`). `core_scissor` is `scissor_snapshot_rect` and `drawn_rect`, not a restatement.
- New files: mtld3d-core `clear_area.rs`, d3d9-rt `clear.rs`, each with `<stem>/tests.rs`. No `mod.rs`.
- New types: `CoreBlend` (Clone: a key is cloned into the pipeline map; no Copy), `ClearArea`. No new `bool`
  field or parameter anywhere: d3d9-rt's sRGB choice is the key's format and an argument, the open pass's a
  `u32`; the target's alpha is computed by the front end. Removed: `DepthState`, `CoreRefusal::{Blending, Scissor,
  DepthSize}`, `DrawFlags::{BLENDING, SCISSOR}`, `viewport_covers`; the derive inventory follows.
- A re-export, not a dependency: `mtld3d_core::pipeline_state::{BlendFactor, BlendOperation}`.
- No static, no config key, no wire field, no lint suppression. Metal wire values come from `winemetal_sys::wire`
  (d3d9-rt) and D3D9 constants from `mtld3d-types`; mtld3d-core keeps mtld3d's enums and never restates a wire
  number.

## Rulings (planning)

1. **The area guard goes into dxmt-core**, not around it in d3d9-rt (a deferred Clear held on the API thread until
   the next pass opens). Why: the 3d2 plan assigned it here; it is the root cause and four lines. Cost if wrong: a
   D3D11 title that binds a depth view larger than its render targets, or targets of unequal sizes, pays one more
   pass at each Clear; unmeasured (no bench here runs D3D11), accepted: the guard changes no D3D11 pixel except to
   make the Clear whole, which the API requires.
2. **A colour Clear, whole or of rects, is of the colour target.** Before a pass narrowed by a smaller
   depth-stencil a whole Clear stays a pass of its own, and a rect clear attaches the colour view alone, where
   mtld3d's reach the narrowed area only. Why: D3D9's Clear is of the target (Wine's `ds_size_test`). Cost if
   wrong: a pixel difference from mtld3d outside the depth-stencil's extent, in a configuration D3D9 calls
   invalid; no row asserts it.
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
   a frame under core.
8. **Hardware sRGB follows mtld3d's twin table** (`PixelFormat::srgb_twin`: RGBA8, BGRA8 among render targets),
   not dxmt-core's wider `recall_srgb`. Cost if wrong: none against mtld3d.
9. **Rows are controls first**: every row is green on the default runtime before any core work; a row red there
   is a wrong row or a ledgered defect of mtld3d.
10. **`bl-destalpha`'s back-buffer case asserts parity, not D3D9**: mtld3d reads the stored alpha of an X8R8G8B8
    back buffer where D3D9 reads 1 (its back buffer is BGRA8 whatever the format asked). The core does the same.
    Cost if wrong: a title blending by destination alpha on an X8 back buffer differs from Windows under both
    runtimes; the fix is one line in each, with the row.
11. **If the stencil-only quad still does not build on Metal** (Task 3's lane: `cl-stencil` red, no pipeline), the
    fix stays inside dxmt-core's `clear_rtv.rs` and `set_depth_stencil_attachment` (a stencil-only clear of a
    combined format then declares and attaches the depth plane, unwritten), with its test, in a commit of its own
    before Task 3's. Cost if wrong: a third dxmt-core change on a path only D3D9 takes.
12. **The order is clears, scissor and size, sRGB, blending.** Why: each commit then lifts only what is right (a
    depth-stencil of another size is clearable before a draw may test it; blending never meets the shader-side
    sRGB encode on a twin target). Cost if wrong: none in the result; a stop before Task 6 leaves blending
    refused, a clean cut.

## Stated gaps

- No row draws a blended, scissored or cleared **multisampled** target (3d5) or a second colour target (3d4).
- The fakes print no blend field of a pipeline and no rect of a `ClearRtv` scissor (records 35, 36, 44, 45 print
  their number only), nor the area of a pass without a colour attachment: the wire mapping is proven by
  `wire_blend_numbers` and the rows, a rect clear's rects and a depth-only pass's area by the rows.
- A `ClearRtv` pipeline that does not build is silent (`begin` returns, no log): d3d9-rt cannot see it; the rows
  under the validation layer are the proof.
- That a load action reaches the pass's area only is Metal's behaviour, stated in the tree by an mtld3d comment:
  mutant (d) on `sz-clear` is this plan's proof of it.
- Wine's `visual` (`clear_test`, `test_blend`, `test_clear_different_size_surfaces`, `ds_size_test`,
  `srgbwrite_format_test`) does not run under core until 3d5; the rows stand in.
- Not examined by review: the presenter with a back buffer that has `PIXEL_FORMAT_VIEW` usage (Task 5's lane and
  the VM lanes present every frame), and blending on 32-bit float targets (no row).

## Appendix: the rows

Back buffer 256x256 X8R8G8B8, auto D24S8. Quads are pre-transformed (`RV`, `rdraw`/`zquad`) unless said; `ZENABLE`
is off unless the row turns it on (the runner cleared Z to 1.0 and stencil to 0). A blended quad's alpha is its
diffuse alpha, with stage 0 `ALPHAOP SELECTARG1`, `ALPHAARG1 DIFFUSE` set by the row. `frames(BLUE, ..)` clears
to 0x000000ff (alpha 0) and resets the scissor rect each frame (its `SetRenderTarget`), so a row sets its rect
inside its draw. Every expectation is confirmed on the default runtime in Task 1 Step 5; a value below that
mtld3d does not give is corrected there, not in the runtime.

**cl (11, Task 3).**
- `cl-rects`: `Clear(2, rects, TARGET, green)` over blue: both rects green, between them blue.
- `cl-count`: `Clear(0, rects, ..)`: nothing; `Clear(5, NULL, ..)`: the whole target.
- `cl-scissor`: test on, rect A: `Clear(0, NULL, TARGET|ZBUFFER, ..)` clears A only (colour probed; depth by a
  tested quad drawn with the test off again); with one rect crossing A: their overlap only.
- `cl-viewport`: viewport (64,64,128,128): `Clear(0, NULL, TARGET|ZBUFFER|STENCIL)` clears the viewport only; a
  rect crossing its edge is cut there.
- `cl-z`: `Clear(1, rect, ZBUFFER, z 0.2)` after a whole clear to 1.0: a quad at z 0.5 `LESSEQUAL` is hidden
  inside the rect and shows outside.
- `cl-stencil`: `Clear(1, rect, STENCIL, 7)` after a whole clear to z 0.3, stencil 0: a quad under `EQUAL 7`
  shows inside only, and a quad at z 0.5 `LESSEQUAL` is still hidden inside (the depth was left); on a D24X8
  surface the same Clear answers `D3D_OK` and changes nothing.
- `cl-mask`: with `COLORWRITEENABLE` red only: a whole Clear and a rect Clear both store the full clear colour.
- `cl-order`: red quad; rect Clear to green over half of it; white quad over half of that: white over green over
  red where they stack.
- `cl-level`: a 64x64 render-target texture's level 1 (32x32): a rect Clear (8,8)-(24,24) and a viewport-bounded
  one; a cube render target's face 3: a rect Clear; read back.
- `cl-bigz`: a 512 D24X8 with a 512x512 render-target texture: a near quad (z 0.1) over all of it, depth written.
  The back buffer bound (viewport 256x256), `Clear(ZBUFFER, 1.0)`: back on the 512 texture a quad at z 0.5
  `LESSEQUAL` shows at (100,100) and is hidden at (400,400). Then the back buffer again with the viewport set to
  (0,0,512,512) and the same Clear: the quad shows at (400,400) too.
- `cl-smallz`: a 128 D24X8 with the back buffer: `Clear(1, (64,64)-(200,200), ZBUFFER, 0.2)` answers `D3D_OK`,
  and a quad at z 0.5 is hidden at (100,100) and shows at (32,32).

**sc (6, Task 4).**
- `sc-rect`: rect (64,64)-(192,192), test on, a full-target red quad: inside red, outside blue.
- `sc-off`: the same rect, test off: red everywhere probed.
- `sc-empty`: test on with, in turn, an inverted rect, a zero-size rect, a rect past the target (300,300)-(400,400)
  and a negative one (-50,-50)-(-10,-10); then test off with the viewport at (300,300): nothing drawn, each
  `DrawPrimitiveUP` `D3D_OK`.
- `sc-clamp`: a 64x64 render-target texture, rect (0,0)-(256,256), test on: the whole texture red (read back);
  rect (-32,-32)-(32,32): the top-left quarter only.
- `sc-pass`: one frame: rect A, red quad; rect B, green quad; test off, a white quad over the bottom strip: each
  where it should be, nothing of red in B.
- `sc-break`: rect A, a red quad over the whole target; a pass break that leaves the rect alone (a second
  depth-stencil surface bound); a green quad over the whole target with no scissor call since: green inside A,
  blue outside it.

**sz (3, Task 4; and `ds-size`, `ds-rhwclip` in the depth kind).**
- `ds-size` (depth kind): a 512x512 D24X8 surface with the back buffer: Z Clear `D3D_OK`; a far quad then a near
  quad overlapping it with `LESSEQUAL`: near wins where both are, in every lane.
- `ds-rhwclip` (depth kind): an INTZ texture bound as the depth-stencil and sampled by the draw, `ZFUNC ALWAYS`,
  `ZWRITEENABLE` off, a pre-transformed quad at z 1.5: not drawn (clipped past the far plane, no clamp while a
  depth-stencil is bound with Z on); the same quad at z 0.5 is drawn.
- `sz-large`: a 512 D24S8 with a 64x64 render-target texture, Z and stencil cleared: depth-tested and
  stencil-tested quads behave as on an equal-size pair.
- `sz-clear` (the guard): a 512 D24X8 with a 512x512 render-target texture: a near quad (z 0.1) over all of it;
  whole Z Clear to 1.0; switch to a 64x64 texture with the depth kept, one depth-tested quad; back to the 512
  texture: a quad at z 0.5 under `LESSEQUAL` shows at (400,400) (the Clear reached past the small pass).
- `sz-small`: a 128x128 D24X8 with the back buffer, Z cleared, a full-target Z-tested quad: (64,64) is the quad's
  colour and every call answers `D3D_OK`; (200,200), outside the depth-stencil's extent, is not asserted (mask 0:
  ruling 2).

**sr (3, Tasks 5 and 6).**
- `sr-clear` (Task 5): `SRGBWRITEENABLE` on: a whole Clear and a rect Clear to linear 0.5 both read 188; off: 128;
  a Clear with it off followed by an opaque quad with it on: both right.
- `sr-blend` (Task 6): on, back buffer, Clear to 128 grey (stored 188), a black quad at alpha 128 under
  SRCALPHA/INVSRCALPHA: 137 (the linear blend, encoded; the encoded values' blend would be 94); the same on an
  A8R8G8B8 texture target; and on a target without a twin (the first of `rt-formats`' formats that has none) the
  shader path's value, equal in both runtimes.
- `sr-toggle` (Task 6): one frame: a blended quad with it on, one with it off, one with it on again: each cell
  its own space's value.

**bl (13, Task 6).**
- `bl-alpha`: SRCALPHA/INVSRCALPHA, red at alpha 128 over blue: (128,0,127).
- `bl-add`: ONE/ONE, (64,64,64) twice over blue: (128,128,255).
- `bl-ops`: SUBTRACT, REVSUBTRACT, MIN, MAX in four cells over a (100,150,200) clear with source (150,100,50),
  ONE/ONE: (50,0,0), (0,50,150), (100,100,50), (150,150,200); MIN and MAX again with ZERO/ZERO: the same.
- `bl-factors`: cells on an A8R8G8B8 texture target cleared to (40,80,120) with alpha 200 (colour and alpha
  differ): source x DESTCOLOR, source x INVDESTCOLOR, dest x SRCCOLOR, dest x INVSRCCOLOR, source x DESTALPHA,
  SRCALPHASAT: each cell's value from the D3D9 formula.
- `bl-both`: BOTHSRCALPHA and BOTHINVSRCALPHA as the source factor with a contrary DESTBLEND: as
  SRCALPHA/INVSRCALPHA and its reverse.
- `bl-destalpha`: DESTALPHA/INVDESTALPHA after a Clear with alpha 0x40: on the back buffer (reads the stored
  alpha: ruling 10), on an X8R8G8B8 texture target cleared with alpha 0x40 too (source x 1 + dest x 0) and on an
  A8R8G8B8 one.
- `bl-separate`: on an A8R8G8B8 target, colour SRCALPHA/INVSRCALPHA and alpha ONE/ZERO with
  `SEPARATEALPHABLENDENABLE`: colour blended, alpha the source's; with it off: alpha blended (alpha read with
  `alphaOf`).
- `bl-factor`: BLENDFACTOR/INVBLENDFACTOR with `D3DRS_BLENDFACTOR` 0x80404040 then 0xffc0c0c0 between two quads of
  one pass; and as the alpha factors on an A8R8G8B8 target.
- `bl-stencil`: stencil written to 5 over a strip; a blended quad that reads the blend factor **and** is
  stencil-tested (`EQUAL`, `STENCILREF` 5) shows on the strip only; with `STENCILREF` 4, not at all.
- `bl-off`: blend states left at SRCALPHA/ZERO/MAX with `ALPHABLENDENABLE` off: opaque.
- `bl-mask`: `COLORWRITEENABLE` red|alpha under ONE/ONE: only red moves.
- `bl-prog`: a ps_2_0 writing its constant (alpha 0.25) under SRCALPHA/INVSRCALPHA, and the fixed-function
  untransformed path (`fdraw`) at alpha 128.
- `bl-pass`: a blended quad reading the blend factor; a pass break (a second depth-stencil bound); the same quad
  again elsewhere with no state call: the same colour.

## Review dispositions

- **R1** (design; 0 Critical, 5 Important, 9 Minor): I1 → Task 2's `kind` fix and ruling 11; I2 → the order
  (ruling 12); I3 → `bl-stencil` stencil-tests the blended quad; I4 → the zero-line arm; I5 → `cl-bigz` probes
  past the viewport, mutant (g); M1 → `sr-blend` with a black quad; M2 → ruling 10; M3 → the X8 clear alpha; M4 →
  ruling 2 widened; M5, M6 → gone with the order (no interim refusal); M7 → `reset()`'s alpha arguments,
  `blendStart()`; M8 → `ds-rhwclip`; M9 → `cl-level`'s cube face, `cl-smallz`.
- **R2** (core; 1 Critical, 3 Important, 8 Minor): C1 → the re-export; I1 → `DrawRefused::Depth` stays; I2 → the
  scissor is decided after `open_pass`, with its rt test; I3 → Task 2, ruling 11, the validation layer on every
  launch; M1 → `Arc::clone` and `WMTScissorRect` in the interface; M2 → the hedge dropped, the rt tests assert the
  fakes' lines; M3 → the fixture's default scissor; M4 → `target_desc` changes (Task 5); M5 → ruling 2; M6, M7 →
  stated gap, ruling 1's text; M8 → Task 2's helpers.
- **R3** (exec; 1 Critical, 11 Important, 12 Minor): C1 → the zero-line arm in both scripts; I1 → `blendStart()`;
  I2, I3, M9 → the order and each lane's expectation rewritten; I4 → no new bool (`core_srgb_variant`, the key's
  format, `open_format`); I5 → the re-export; I6 → `DrawRefused::Depth` stays, the `depth_state()` assertions
  named; I7 → one reset site, mutant (c) on it; I8 → the X8 clear alpha; I9 → every launch under the validation
  layer; I10 → `vm/dxmt/gate.sh`; I11 → the moves instead of restatements; M1, M2 → the literal rule, the
  `refusals` array, `viewport_covers` deleted; M3, M4 → Task 2's helpers, flags and module doc; M5 → the mutants'
  one-line forms; M6, M7 → the helpers and the script edit points; M8 → Task 1 Step 6; M10 → a folder a run, the
  lane's summary line; M11 → no bool added, `clear_regions` pure in mtld3d-core; M12 → the Mac conformance in
  `jr`, the Task 3 rt test that is red before it, the stated gaps, the probe's threshold.
