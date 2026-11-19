# DXMTRS C4c-4c: `StretchRect`'s scaled, converting, YUV, multisampled and overlapping copies on D3D9's core runtime

Slice 4 of C4c, third part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push).
Maps: `build/c4c/s4c/map.md` (cited "map 1a", "map 2b" ...), `build/c4c/s4b/map.md` ("4b map"),
`build/c4c/s4/map.md` ("old map"). The 4a and 4b plans (`2026-11-18-dxmtrs-c4c4a-fill-update-validation.md`,
`2026-11-18-dxmtrs-c4c4b-unscaled-copies.md`) hold the rules this one repeats by name only. Reviews:
`build/c4c/s4c/review-{1-design,2-core,3-exec}.md` (cited "R1 f3" = review 1, finding 3). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and pixels, a valid `StretchRect` **into a render target**
works whatever its shape:

- **scaled** (rects of different sizes; NONE and POINT nearest, LINEAR bilinear), from and into sub-rects;
- **converting** (formats whose bytes do not copy: X8R8G8B8 into A8R8G8B8 with alpha one, R5G6B5 to and from
  8888, 8888 to and from float targets ...), 1:1 or scaled;
- **from a YUV plain surface** (packed YUY2 and UYVY, planar YV12 and NV12), any size;
- **into a multisampled target**, and **from a multisampled target** in the shapes the whole-surface resolve
  does not cover (a sub-rect, a scaled or converting copy, multisampled into multisampled);
- **between overlapping rects of one subresource**, and scaled between two levels or faces of one texture;

and **a plain surface copied onto itself** with different rects (on the CPU).

Still refused by name (`D3DERR_NOTAVAILABLE`, the same literal): **the depth-stencil resolve** (ruling S-6: its
own slice, C4c-4d), an extended device's whole-surface copy into a non-target (4b's ruling X-2), a plain pair
no CPU route serves (4b's K-1), and a multisampled standalone source of a format this GPU does not resolve.

Wine, derived by R3 from 4a's raw sweep and 4b's predicted changes ("Test failed" lines over the default
runtime's baseline; re-read against 4b's own sweep once it exists):

| function | after 4b → after 4c |
|---|---|
| `yuv_layout_test` | 992 → 0 |
| `yuv_color_test` | 106 → 0 |
| `test_multisample_stretch_rect` | 54 → 0 |
| `test_blit_format_conversion` | 8 → 0 |
| `clear_test` | 5 → 0 |
| `np2_stretch_rect_test` | 5 → 0 |
| `test_filling_convention` | 14 → 0 |
| `stretchrect_test` | 17 → 0 |
| `test_stretch_rect` (device) | 40 → 0 |
| `multisampled_depth_buffer_test` | 12 → 5 (visual.c:17504 once, :17524 four times: the depth resolve) |

## Global Constraints

The 4b plan's, unchanged, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn`
(`c4c-mn`). **The base is what C4c-4b pushed** (the ledger names both SHAs): Task 1's controls run only after
that push, with `DXTIP` the pushed DXMTRS SHA; `base-key.txt` and `base-imports.txt` are copied from s4b once
4b's `jr` printed "same" for both; if 4b's push moved its tip, the predictions here are re-derived (R3 f15).
Job scripts in `build/c4c/s4c/gates`, copied from s4b's and edited before their first run (guard:
`/usr/bin/grep -nE 'c4c/s4b|c4c4b|C4c-4b'` over them prints nothing; VM state names `c4c4c-*`; **and the
positive checks**: `head.sh`'s `BLIT_ROWS` 41; `jv0.sh` runs `blit targets msaa` and greps this slice's rows;
`jm.sh` and `jmb.sh` carry this plan's letters and tokens; `jcr.sh`/`jc.sh` want 41; R3 f10). A rerun is a new
job name (`jv0b` is its own file); every verdict counts its rows and its `was not recorded` lines (0 wanted);
every `d3d9-check.sh` run sets `DXMT_CHECK_WORK`; the pipeline lock; no implementer runs `dxmt/toolchain.sh`;
each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- **A core arm is keyed on the device being core.** No mtld3d recorder call reachable on a core device; no
  read of a `StretchKind`'s handle below an arm.
- **The invariant: on a core device only render-target levels, standalone targets and the back buffer are
  written by the GPU.** A scratch texture of this slice is private to one call and never an application
  surface.
- **Every stretch refusal goes through `refuse_stretch()`**; a copy the core does not record logs "was not
  recorded" and answers `D3DERR_INVALIDCALL`.
- **Files outside `src/d3d9`:** none. Ruling M-2 names the one mtld3d file this slice touches.
- **The x64 check (`dxmt/check.sh`) runs once at the end of C4c-4**, which ruling S-6 makes C4c-4d. `jr.sh` and
  `post.sh` test `git diff --name-only $X64_BASE..HEAD` (4b's pushed DXMTRS SHA) against `^src/d3d9/`, print
  "outside src/d3d9: <files or none>", and run the check only when there are some (R3 f11).

## The mechanism

### The shader (shared with mtld3d; ruling M-2)

mtld3d's blit source (`unix/src/metal/blit.rs:67-183`, one raw literal; map 1a quotes it) **and its two
entry-point names** (`:185-188`) move, byte for byte, into the shared crate (`mtld3d_core::stretch_rect`:
`pub const BLIT_MSL`, `BLIT_VS`, `BLIT_PS`, beside `BlitDecode`), with `const _: () =
assert!(BLIT_MSL.is_ascii());` (winemetal's thunk decodes the source as ASCII; R2 f6). mtld3d's file compiles
that constant, as before. One text, two runtimes: its `[[buffer(0)]]`, `[[texture(0)]]` and `[[sampler(0)]]`
are free in dxmt-core's direct-binding model (R2: a d3d9 pass's argument buffer is null and later binds
override).

### d3d9-rt: `stretch` (new file `rt/src/device/stretch.rs`)

`pub fn stretch(&mut self, quad: &StretchQuad) -> bool` (`#[must_use]`), with
`pub struct StretchQuad { src: TargetRef, src_rect: [u32; 4], src_view: u32, dst: TargetRef, dst_rect: [u32; 4],
linear: bool, decode: f32 }` (`src_view`: the `WMTPixelFormat` to sample through, swizzle bits included,
ruling SW; `decode`: `BlitDecode::uniform()`'s number, so no derive is added: R1 f12). One render pass that
samples the source's one subresource onto the destination rect:

- **Source**: any stored colour level (`level_shape` + `stored_texture`, as `copy`) through **a 2D view of that
  one level and slice** in `src_view` (the key `texture_target` builds; for the back buffer
  `back_buffer.key.with_format(src_view)`, R1 f13), so a level above 0 and a cube face are level 0 of the
  view. **A multisampled back buffer is a source through its presented texture**: `resolve_presented()` first,
  then `presented()` (what mtld3d samples and what Present shows; no scratch: R1 f7). `copy` (4b) gains the
  same source rule in this slice's first d3d9-rt commit. **Destination**: a target (`shape`/`resolve`: the
  drawn texture), through its stored format's view (never the sRGB twin: the copy is verbatim, map 1a),
  **multisampled or not**.
- False, and nothing recorded (the library is made only after these checks): an end that does not resolve; a
  multisampled source other than the back buffer; an empty rect or one that leaves its level (`checked_add`,
  through `surface.rs`'s `inside`, made `pub(super)`); **one texture at both ends** (`Arc::ptr_eq`: a pass
  does not sample what it attaches, and no tile barrier is recorded); a planar decode (the core's planar
  texture holds luma alone, map 2d); **a `src_view` whose base format is neither the source's stored format
  nor its twin, or that differs from the stored format on a texture without `VIEW_TWINS`** (R2 f2, f10); a
  library or pipeline Metal does not make (the failure is remembered for the device, Metal's error text logged
  once in words the launches' "unpresented" pattern matches; R2 f7, R3 m8).
- **Uniforms by a pure function** `quad_uniforms(src: &Shape, rect: [u32; 4], decode: f32) -> ([f32; 4],
  [f32; 4])`: the vertex `xform` = the rect over **the source level's own extent** (as mtld3d divides by the
  addressed subresource's extent; R2 f14), the fragment `(0.0, decode, level width, level height)` (level 0 of
  a one-level view).
- **Recorded**: `close_pass()`, then one `emit` holding the whole pass (`start_render_pass` to `end_pass`, as
  `read_rect` does; `render_open` stays false): the destination view attached LOAD/STORE with the level's
  extent as the pass's size (a pending Clear of the destination then folds in); the source accessed READ;
  **the rasterizer state: fill solid, cull none, no depth bias** (R2 f1, Critical: the optimizer merges this
  pass with a draw pass on the same view, and Metal keeps that draw's cull and fill; mtld3d's quad sets both
  for the same reason); viewport and scissor the destination rect; the pipeline of `(destination stored
  format, destination samples)`; the sampler; the two 16-byte uniforms at slot 0 of each stage; three
  vertices. Then `note_written(dst, stored format)`.
- **The record code is written in `stretch.rs`** (about forty lines that repeat what `draw.rs` and
  `open_pass` encode: a duplicate, named in the Rules check; sharing it would edit `draw.rs` and
  `recorder.rs`, which ruling P-3 keeps untouched; R2 f4). **The sampler comes from `CoreDevice::sampler`**
  (the existing cache, through `mtld3d_core::sampler_state`'s helpers, as mtld3d takes its blit sampler from
  its ordinary cache): min and mag LINEAR or POINT, mip POINT, clamp.
- **Owned by `CoreDevice`** (no static; the fields after `device`, per its drop-order doc): the library slot
  (made at first use by `MTLDevice_newLibraryWithSourceOptions` with mtld3d's options as raw values: language
  version `0x20004`, math mode 2, invariance 0; a failure remembered), the two functions, the map of
  pipelines by `(format, samples)` (`Option`, a refusal kept), built through dxmt-core's public
  `render_pipeline` as `ClearRtv`'s are.

**`scratch_like`** (new, `textures.rs`): `pub fn scratch_like(&mut self, like: TargetRef, width: u32, height:
u32) -> Option<TargetRef>`: a single-sample, one-level 2D **target** registered under a fresh id with `like`'s
stored format, **its sRGB twin and `VIEW_TWINS` when it has them** (R1 f3, R2 f2, Critical: `resolve_into`
resolves through the source's last written view and asserts the destination has it). The caller forgets it
(`forget_texture`; the recorded closures hold their own).

### Front end (`core_arms.rs`)

`StretchCall` gains `filter: u32` (set at the exit; R1 f4). `stretch_colour` keeps step 1 (the resolve) and
its plain-destination routes. For **a destination that is a target** the arm, in order:

0. **One texture at both ends, on the call's own refs** (4b's test, first: R1 f2): `Skip` → `D3D_OK`, nothing
   recorded (a multisampled target onto itself included).
1. `core_flush_dirty` of the source's parent texture (not for a planar source) and of the destination's when
   they are two textures, as 4b (R1 f5: a destination never used on the device is made here).
2. **The source settled** to a single-sample ref, a view format, a rect and a decode:

   | source | ref | view | rect | decode |
   |---|---|---|---|---|
   | a planar YUV plain surface | a scratch **texture** of the level's size, X8R8G8B8's stored format, filled by `texture_upload` from the staging decoded on the CPU (ruling Y-1) | stored | the call's | none (the decode wrote alpha ff) |
   | a packed YUV plain surface | its core texture | stored | the call's | `Yuy2` / `Uyvy` |
   | the multisampled back buffer | `BackBuffer` (the primitives read its presented texture) | stored | the call's | `blit_decode(format)` |
   | another multisampled target | `scratch_like(source, its size)`, written by `core.resolve_into` (false: `refuse_stretch()`, ruling R-1) | stored | the call's | `blit_decode(format)` |
   | a level of a texture that is no target | 4b's ref | **the swizzled view** (`view_format(stored, ti.swizzle)`) | the call's | `blit_decode(format)` |
   | anything else (a target level, a standalone target, the back buffer) | 4b's ref | stored | the call's | `blit_decode(format)` |

3. **The source's texture is still the destination's** (a single-sample one-texture pair) and the copy is not
   an unscaled `Direct`: the source region is copied (4b's `copy`) into `scratch_like(source, region size)`;
   the scratch is the source from here, its rect `(0, 0, w, h)`, view stored, decode unchanged (mtld3d's
   scratch route, map 1e).
4. **The copy**: unscaled, same bytes, single-sample destination → `core.copy` (4b's blit); anything else →
   `core.stretch` (`linear = filter == D3DTEXF_LINEAR`).
5. On success, as 4b: the destination's claim and an autogen chain's regeneration. Every scratch is forgotten
   on every path, failed ones included.

**A plain surface onto itself** (`ptr::eq(src, dst)`, both `IS_OFFSCREEN_PLAIN_DEFAULT`, route other than
`Skip`; R1 f6: nothing else takes this route, so an extended device's cube faces keep 4b's refusal): the
level's bytes are copied out (`surface_source_image(None, level)?.bytes.to_vec()`), then
`copy_bytes_to_staging_region(level, &SourceImage { that copy, .. }, Some(source rect), destination point)`
writes them back: the function's own block and bounds checks serve a DXT surface (R1 f10).

`stretch_depth` is unchanged: the resolve stays refused (S-6).

## Review Focus (for the final review)

- mtld3d unchanged: the moved shader text and names byte-identical; every other hunk behind `cfg` and the
  device test.
- `stretch`: what it attaches, samples and tracks; the rasterizer state; the uniforms; the destination's
  sample count; `note_written`; every refusal; what the device owns and when it is released.
- Every scratch: its description, who writes it, its rect and view in the quad, that it is forgotten on every
  path and never an application surface.
- The routes against mtld3d's answers for every shape the gates let through; the order of steps 0-4; the view
  rule (SW).
- The invariant.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in the Appendix: ten new `sq-*` rows in the `blit` launch (41 rows); `sr-shape` cut down to the depth
resolve; **`sc-self` loses its overlap cell, which is `sq-overlap`'s first, judged** (R3 f3); `targets`'
`rt-msaa` 32x32 cell judged; one cell added to `sc-plain2` (4b's review: the CPU route's dirty mark on a
destination that already has a core texture; a red there at `ja` is a 4b defect to fix first, not a row to
correct: R3 m6). **New helpers** (R3 f5): `stretchTo(src, srect, dst, drect, filter)` with a `why`; a judge
with an alpha mask (`rtHasA`, or a mask in `Tx`); `offered` with a usage argument (a render-target format
asked with `D3DUSAGE_RENDERTARGET`); `converts(src, dst, name)` over `CheckDeviceFormatConversion` with a skip
line; writers for UYVY, YV12 and NV12 as Wine writes them (visual.c:13028-13075); a 2-byte reader of an
R5G6B5 target through `GetRenderTargetData` into an R5G6B5 SYSTEMMEM surface.

Scripts, every place (R3 f10; two of them fail silently if half done):
- `dxmt/d3d9-check.sh` `refusals_ok`: `targets` leaves the outer case (`:173`) and the inner `targets|blit`
  case (`:176`) **and joins the "none" list (`:183`)**; the `check blit` list; prose `:46-51`, `:68-72`,
  `:156-165`.
- `vm/dxmt/d3d9-gate.sh`: `tg64-fresh tg32-fresh` leave the `stretch` loop (`:281`) **and join the no-kind
  loop (`:290`)**; the blit row loop; prose `:37-41`, `:55-59`, `:240-247`. `vm/tests/d3d9.sh` `:34-35`.
- The test file's prose: the header, the blit-mode comment (which rows call `ColorFill` and `UpdateSurface`),
  `rowRtMsaa`, `rowSrShape`, `rowScSelf`; one sentence that the refusal's name now covers a depth resolve.
- `sh -n` on the scripts.

**Rules for every new cell**: the 4b plan's, plus:
- a scaled cell judges texels whose source texel is unambiguous under nearest sampling. **Named exception**:
  a downscale by an even factor samples exactly between two source texels (64 into 16: source 4i + 2.0;
  `rt-msaa`'s 64 into 32: 2i + 1.0), so its colour edges sit on multiples of that factor and each such pair is
  one colour (R3 m1);
- `sq-scale`'s 2x POINT cell judges destination texels 31 and 32 (either side of the source edge at 16) as the
  pure colours (mutant c's cell), and its LINEAR cell judges 30 (A), 31 (¾A + ¼B), 32 (¼A + ¾B), 33 (B) within 1;
- **every converting cell judges the destination's alpha** through the alpha-mask helper; **every 565 cell
  uses `k565` colours** (the GPU rounds where the CPU truncates; replicated colours agree), is read by the new
  2-byte reader, and its way back is judged within 1;
- `sq-swizzle`'s fills tell the raw texels from the D3D colours in every channel: A4R4G4B4 `0x9c63`, L8 `0x5a`;
  `sq-yuv`'s macropixels have Y, U and V pairwise distinct (Y b4, U 60, V a0 decodes to (f3, b1, 7f) by the
  shader and by the CPU decode alike) and flat areas aligned to 2 in x (and in y for the planar formats);
- a render-target format a lane may lack is asked with `offered(.., D3DUSAGE_RENDERTARGET)`; a YUV cell asks
  `converts`; a skipped cell prints its skip line;
- `sq-order`'s and `sq-level`'s copies take their source from **another texture** than their destination
  (they are Task 3's; one texture beyond `Direct` is Task 4's, in `sq-overlap`);
- a cell that changes FILLMODE or CULLMODE restores it on every path.

Job scripts in `build/c4c/s4c/gates` (the positive checks above).

**Controls, in this order, after 4b's push:**
1. `JOBS=jv0` alone, on the row commit: `blit`, `targets`, `msaa` on mtld3d under the validation layer:
   reports 0, rows ok 43, 37, 22. **It confirms ruling SW**: predicted from source, `sq-swizzle` reads the D3D
   colours (a plain surface is a texture that is no target; mtld3d binds its swizzled view). If it does not,
   the finding is ledgered, the cells become answer-only in a second row commit, and `jv0b` (its own file)
   confirms. **First-time shapes on mtld3d under the layer** (`sr-shape`'s depth resolve, a compute pass; the
   judged overlap route): a report or a red cell there is an mtld3d finding for the maintainer, ledgered, and
   decides nothing (R3 f14).
2. `ja` on the final row commit: default and msl all green at the first run; core red on exactly the ten
   `blit-sq-*`, `targets-rt-msaa`, `targets-runtime`; nothing else (`sc-self`, `sr-shape`, `sc-plain2` green).
3. `jcr`: the VM's default lane on old and new WoW64: `bt64`/`bt32` 41 ok lines each.

Commit: "dxmt, vm: d3d9_triangle rows for StretchRect's sampled copies (DXMTRS C4c-4c)".

### Task 2: the shader's move, the back buffer's presented source, `scratch_like`, `stretch` (d3d9-rt)

Three commits. (i) "d3d9: the StretchRect quad's shader in the shared crate": the constants' move alone;
mtld3d's unit tests pass untouched. (ii) "d3d9: a multisampled back buffer is a copy's source through its
presented texture": `copy`'s source rule, with a test (a 4-sample back buffer copied from: a resolve, then
one `blit 4`). (iii) "d3d9: a sampled copy on the core runtime": `scratch_like`, `stretch`, and tests on the
fakes as they are (what they print: R2 f5; `MTLDevice_newLibraryWithSourceOptions` is exported by the
crate's own test fixture under `cfg(test)`, returning a handle the fakes never make: ruling F-1):
- `quad_uniforms`: a sub-rect of level 1 of a 64-texel texture; a decode's number carried through;
- refusals, each false with no line (drain and take the calls first): an unknown end, a multisampled source
  that is no back buffer, a non-target destination, an empty rect, a rect past its level, one texture at both
  ends (two levels of it included), a planar decode, a `src_view` of another base format, a swizzled
  `src_view` on a texture without `VIEW_TWINS`;
- a stretch records one render pass on the destination's view: the fakes' viewport and scissor lines hold the
  destination rect; the view line of the source holds its format, mip and slice (level 1; a cube's face 3);
  the sampler's filters for `linear` and not; `render 12 fill_mode=0 cull_mode=0`; the two 16-byte binds; one
  draw of three vertices; `!render_open` after it;
- **the rasterizer state under a merge** (R2 f1): a draw on D at `cull_mode: 1`, then a stretch into D: in the
  one merged pass the quad's own rasterizer record sits after the draw's and before the quad's draw (told
  apart by the stretch pipeline's `pso` handle);
- the pipeline: `pso samples=4` for a 4-sample destination; one pipeline for two stretches of one format and
  count, a second for another count (the test makes no rect clear: `ClearRtv`'s pipelines print the same
  line);
- **fences** (R2 f3: same-target render passes merge, so the follower is a blit): a Clear of level 1 of X,
  then a stretch from it: the quad's pass waits on the Clear's; a stretch from level 1 of X, then a draw on
  it: the draw's pass waits on the quad's; a stretch into level 1 (and into a cube face), then `read_back` of
  it: the blit waits on the quad's pass;
- `note_written` (R3 f2): a 4-sample target with `VIEW_TWINS` cleared through its sRGB twin, a stretch into
  it, then `resolve_into` from it: the resolve's view names the stored format;
- `scratch_like` of a twinned 4-sample target cleared through its twin, then `resolve_into` the scratch: one
  resolve, no panic (the fixture's 4-sample description gains a twin for this test; R2 f2).
  Not testable on the fakes, stated: a pipeline or sampler Metal refuses; the library's creation count.

### Task 3: scaled and converting copies between single-sample ends (front end)

`StretchCall`'s `filter`; steps 0, 1, the single-sample rows of step 2's table (packed YUV included, the
planar row and the standalone multisampled row excepted), step 4, step 5. A multisampled end, step 3 and a
planar source answer `refuse_stretch()` for now. Lane `jt3` (default, core): failing tokens predicted
`blit-sq-yuv blit-sq-ms blit-sq-overlap blit-sq-plain targets-rt-msaa targets-runtime` (planar formats are
always offered, so `sq-yuv` is red until Task 4); `targets` 1 refusal line, `blit` 1; **the `blit` launch once
under core with the validation layer** (reports 0, rows ok 39: the first real compile of the shared text, a
function and a pipeline from it, through the options call; R2 f9). Commit: "d3d9: StretchRect scales and
converts into a target on the core runtime".

### Task 4: multisampled ends, one texture at both ends, planar YUV (front end)

The rest of step 2's table, step 3. Lane `jt4`: failing tokens predicted `blit-sq-plain`; `targets` 0 lines,
`blit` 1, `msaa` 0; **`blit`, `targets`, `msaa` once under core with the layer** (rows ok 42, 37, 22).
Commit: "d3d9: StretchRect through a scratch texture on the core runtime".

### Task 5: a plain surface onto itself (front end)

Lane `jt5` at the tip: every row of every launch green strict under core, every refusal set exact (`blit` 1,
`targets` 0, `msaa` 0), `was not recorded` 0; the validation launches rows ok 43, 37, 22. Commit: "d3d9:
StretchRect of a plain surface onto itself on the core runtime".

### Task 6: gates

As the 4b plan's Task 6. **Mutants** (eight; final token lists in `gates/mutants-expect.txt` from the rows as
committed, before `jmb`; each red on exactly its tokens):
  b. the front end hands `stretch` the source rect moved to the origin → `blit-sq-scale` and every row with a
     scaled or converting source rect off the origin over a second colour;
  c. the front end hands `stretch` LINEAR for every filter → `blit-sq-scale` (texels 31 and 32), and the other
     2x POINT cells that probe at an edge;
  g. a multisampled standalone source is handed on as its unwritten scratch (the resolve skipped) →
     `blit-sq-ms targets-rt-msaa`;
  h. the overlap route skips its scratch (the copy handed straight to `copy`, which refuses one subresource's
     overlapping rects) → `blit-sq-overlap`, with `was not recorded` above 0;
  i. `stretch` tracks no source read → `blit-sq-order`;
  j. the plain self-copy writes at the source's point → `blit-sq-plain`;
  l. the front end hands `src_view` without its swizzle bits → `blit-sq-swizzle`;
  m. the front end hands `Uyvy` for YUY2 and `Yuy2` for UYVY → `blit-sq-yuv`.
  **Held by Task 2's unit tests on what the fakes print, not by a row mutant** (R2 f5, R3 f8): the viewport and
  scissor, the source view's level and slice, the destination's level and slice, the sampler's filter, the
  rasterizer state, `quad_uniforms`, `note_written` (a row cannot see it: R3 f2), the pipeline's sample count.
  **Not gated at all, stated**: decode mode 5 for a raw X source (it shows only where the source's padding
  byte is not ff; the rows' colours are opaque; `blit_decode` is mtld3d's own function and `sq-conv` judges
  alpha ff; R3 f1); a scratch's release.
- `jw`: the sweep → `build/c4c/s4c/sweep.md`, against the Goal's table.
- **Probe: none** (ruling P-3).

### Task 7: review. Task 8: spec, pin, push.

As the 4b plan's.

## Rules check

- **A static:** none (mtld3d's own `OnceLock` library stays in its unix side; the core's library slot,
  functions and pipeline map are fields of `CoreDevice`). **A config key, a wire field, a dependency, a derive,
  a lint suppression:** none. **An `export_name`:** one, test-only, in d3d9-rt's fixture (ruling F-1; not one of
  `d3d9.dll`'s API exports, which `docs/CONVENTIONS.md` covers).
- **New pub items:** `CoreDevice::stretch`, `StretchQuad`, `CoreDevice::scratch_like`;
  `mtld3d_core::stretch_rect::{BLIT_MSL, BLIT_VS, BLIT_PS}`; `surface.rs`'s `inside` becomes `pub(super)`.
  **Changed:** `copy`'s source rule for a multisampled back buffer; `StretchCall` gains `filter`. **New
  state:** three `CoreDevice` fields (the library slot, the functions, the pipeline map).
- **A duplicate, named:** `stretch.rs`'s record of a pass, a pipeline bind, a viewport, a scissor, a
  rasterizer state and a draw repeats `draw.rs`'s and `open_pass`'s encoding; sharing it would edit the draw
  path (P-3). None else: one shader text and one pair of names for both runtimes; the sampler is the cache's;
  the routes reuse 4b's refs, `copy`, the claim and the regeneration; the CPU decode is `pixel_convert`'s.
- **Not silent:** a quad Metal cannot make logs once and answers false; the arm logs "was not recorded".
- **Divergence added** (also under "Stated gaps"): planar YUV is decoded on the CPU and then sampled; a
  standalone multisampled source is resolved into a scratch where mtld3d samples the resolve its passes keep;
  a multisampled target copied onto itself at disjoint 1:1 rects keeps the copy, where mtld3d's blit lands on
  its resolve texture and the next resolve overwrites it (R1 f2).

## Rulings (planning)

- **S-6. The depth-stencil resolve is C4c-4d, not 4c.** mtld3d does it with a compute kernel that reads sample
  0 of both planes and plane blits (map 1d); dxmt-core has no depth resolve and the wire's attachments no
  resolve fields (map 2f). It shares nothing with the sampled quad. Cost if wrong: one more slice's fixed cost
  (about three hours); the x64 check moves to 4d's end with it.
- **Q-1. The quad is a pass of d3d9-rt's own, compiled from mtld3d's source**, not an internal fixed-function
  draw through `CoreDevice::draw`: the same shader gives the same texel mapping, filter and packed-YUV decode
  as the default lanes expect. The spike (`build/c4c/s4c/gates/jsp.log`) showed the plain compile call working
  from the recorder in every core launch (the Mac; the VM, 64-bit and 32-bit, old and new WoW64); it did not
  run the options call, a function or a pipeline from a source library, or this text: `jt3`'s first core run
  is that test (the options slot is in the VM's call table: R2 f9). Cost if wrong: Task 3 falls back to the
  internal draw. A compile runs once a device, on the API thread, at its first sampled copy (mtld3d: once a
  process, on its encoder side).
- **M-2. The shader text and its entry names move to the shared crate**, a hunk of mtld3d's code outside
  `cfg`: constants moved, byte-identical. Its gates: mtld3d's unit tests untouched, the default lanes, the
  translator key, the Mac's conformance.
- **F-1. d3d9-rt's test fixture exports the library-from-source call** under `cfg(test)` (the test binary
  links with dynamic lookup and the fakes do not define the symbol; R2). `src/dxmt/fakes` does not change.
- **SW. The source is sampled as mtld3d's quad samples it** (settled from source, R1 f1): through the
  swizzled view for a level of a texture that is no render target, through the stored format for a target's
  level, a standalone target, the back buffer and every scratch; decode by `blit_decode`. So an R16F or
  G16R16 target as a source gives Metal's missing channels (0, and alpha 1), as on mtld3d.
- **Y-1. Planar YUV is decoded on the CPU** from the plain surface's staging (`pixel_convert::convert_region`:
  `src_pitch` the image's, `src_slice_pitch` = pitch × height, into X8R8G8B8 at pitch w × 4; R1 f9), the whole
  level, and uploaded to a scratch, then sampled. NONE and POINT pick the texel and chroma block mtld3d's
  shader picks; the rounding agrees within Wine's tolerance of 1. Under LINEAR mtld3d filters luma only and
  the core filters decoded RGB.
- **R-1. A standalone multisampled source of a format this GPU does not resolve is refused by name.**
- **P-3. No probe**: `recorder.rs`, `draw.rs`, `target.rs` and `stored_shape` stay untouched; `stretch` is a
  new file and three fields no draw reads. `copy`'s source rule is in `surface.rs`.

## Stated gaps

Refused by name after 4c: the depth resolve (4d); an extended device's copy into a non-target; a plain pair
outside every CPU route; R-1's formats. Not built, stated:
- LINEAR on a planar source (Y-1);
- **costs a call**: a scratch target is created, zeroed by the initializer (a pass on its queue and a wait)
  and released at each call that needs one (a standalone multisampled source also pays the resolve and a
  rebuild of the capability map); a planar source pays a CPU decode of its whole level and an upload. A cache
  a source, as mtld3d keeps, is the later remedy if a title leans on these shapes (R1 f7, R2 f11);
- LINEAR sampling of a 32-bit float source is whatever Metal does on this GPU, as on mtld3d;
- the unscaled overlap route's two blits rely on their order inside one blit encoder (`sq-overlap`'s first
  cell holds it; R2 f13);
- no cell for a multisampled source into an AUTOGENMIPMAP level or a multisampled additional chain's back
  buffer (the launches' devices; the routes are the table's).

## Appendix: the rows

Launch `blit` (31 + 10 = 41 rows; the new ones after `sc-order` and before `sc-depth`, which stays last).
Surfaces are 64x64 A8R8G8B8 unless said; "target" a non-lockable standalone target.

| row | cells |
|---|---|
| `sq-scale` | a 32x32 target of four flat quadrants into a 64x64 target whole, POINT: each quadrant, and texels 31 and 32 across each edge; the same NONE; a's {8,8,24,24} into b's {5,7,37,39} (2x into a sub-rect): (5,7), (20,22), (21,23), the texel just outside, (0,0); a 64x64 target (edges at 32) into a 16x16 one, POINT; LINEAR 2x across an edge at 16: texels 30, 31, 32, 33 |
| `sq-level` | level 1 of a 2-level render-target texture (another colour than level 0) scaled into a target; a target scaled into level 1 of a second render-target texture and into face 3 of a render-target cube (face 0 unchanged); face 2 of a second cube (face 0 another colour) scaled into a target |
| `sq-conv` | the back buffer's rect (X8R8G8B8) into a target, 1:1: the colour and alpha ff; an X8R8G8B8 target cleared with alpha 40 into a target: alpha ff (the padding read first; if it does not read 40 the cell prints a skip line for the padding and still judges ff); a target of `k565` colours into an R5G6B5 target (2-byte reader) and back (within 1); a target into an A16B16G16R16F target and back, if offered; a usage-0 X8R8G8B8 texture's level into a target: alpha ff |
| `sq-swizzle` | an A4R4G4B4 plain surface filled 9c63 and an L8 one filled 5a (each if offered), 1:1 into a target: 0x99cc6633 as A, R, G, B (the D3D colour: red cc, green 66, blue 33, alpha 99; a raw read would give red 99, green cc, blue 66, alpha 33) and (5a, 5a, 5a, ff) |
| `sq-yuv` | a YUY2 and a UYVY plain surface (each if `converts`) of two flat areas with Y, U, V pairwise distinct, into a target 1:1 and 2x POINT: the decoded colours within 1; a YV12 and an NV12 one likewise |
| `sq-ms` | a target into a 4-sample target whole, then that into a third target by the whole resolve; a target's rect into a cleared 4-sample target's rect, resolved, with the texel just outside; a 4-sample target's rect into a target, 1:1 at an offset and 2x; a 4-sample target into another 4-sample one, resolved |
| `sq-overlap` | a target's {4,6,37,38} onto its own (20,18): the moved texels as a copy through a buffer gives them, the rest untouched; an additional chain's back buffer's rect onto the device's, overlapping; level 0 of a 2-level render-target texture scaled onto its level 1 |
| `sq-plain` | a plain surface's rect onto itself, disjoint and overlapping: read under a READONLY lock, then copied into a target |
| `sq-order` | in one scene: a Clear of a, a scaled copy of a into b, then a draw on a: b holds the Clear's colour; a draw on level 1 of a texture, a scaled copy from another target into it covering part of the draw, a draw beside it; the same on a cube's face 3; **a wireframe draw on b (FILLMODE restored after), then a scaled copy into b: the copy's texels are whole** |
| `sq-autogen` | a 32x32 target scaled into level 0 of an AUTOGENMIPMAP render-target texture, then a minified draw of it reads the new colour |

`sr-shape` keeps one cell: a 4-sample D24S8 surface into a single-sample one, `NOTAVAILABLE` under strict,
`D3D_OK` otherwise (the launch's one refusal line, logged at every stage). `sc-plain2` gains: q copied into a
target before the plain copy into q, and again after it. Launch `targets`: `rt-msaa`'s 32x32 cell answers
`D3D_OK` in every lane and judges (8,16) and (24,16).

## Review dispositions

Three reviews, 2026-11-18: R1 design (0 Critical, 8 Important, 8 Minor), R2 recorder (2, 3, 9), R3 execution
(2, 13, 9). Every finding folded as cited above. Decided against a reviewer's alternative:

- **R3 f1** (decode 5): mutant d dropped, not made decisive: whether an X8 target keeps a non-ff padding
  byte on both runtimes is not established; `sq-conv` carries the cell that would show it and says in its skip
  line when it cannot.
- **R3 f8** (mutants for the destination's level and slice): held by the unit test on the fakes' view and
  attachment lines; the swizzle and the packed order got mutants l and m.
- **R1 f7c** (decode only the source rect of a planar surface): not taken; the cost is stated.
- The two Criticals of R2 (the rasterizer state; the scratch's twin) and of R3 (two equivalent mutants) are
  in the mechanism, Task 2's tests and Task 6's list.

## Execution notes

Executed 2026-11-18 and 19. DXMTRS `c4c`: 64a46594 (the shader in the shared crate), a647a10d (a multisampled
back buffer as a copy source), fd1b22d9 (`stretch`, `StretchQuad`, `quad_uniforms`, `scratch_like`), 7bedadf2
(scales and converts into a target), 520b49cd (the scratch routes), 8bde3b14 (a plain surface onto itself; the
tip). MacNeutron `c4c-mn`: eab85179 (the rows). Artefacts: `build/c4c/s4c` (map, reviews, `rc*.log`, `gates/`,
`sweep.md`).

- **The spike** (`jsp`, before any 4c code, a throwaway build): a library made through
  `MTLDevice_newLibraryWithSourceOptions` from the recorder in every core launch on the Mac and in the VM's two
  WoW64 forms. Ruling Q-1 stood; the real text first compiled in `jt3`, and in the VM guest in `jc`.
- **Controls**: every prediction held at the first run. `jv0` (mtld3d under Metal's validation layer, the base):
  reports 0 in `blit`, `targets`, `msaa`; rows ok 43, 37, 22; no skip line (the X8 target's padding reads 40 on
  mtld3d, so `sq-conv`'s alpha cell is decisive); **ruling SW confirmed** (`sq-swizzle` green on mtld3d). `ja`:
  default and msl ok (41 of 41, 20 of 20); core red on exactly `targets-runtime`, `targets-rt-msaa` and the ten
  `blit-sq-*`; `was not recorded` 0. `jcr`: the VM's default lane ok on old and new WoW64, 41 `bt64`/`bt32` and
  20 `ms64`/`ms32` ok lines each.
- **Ruling O-2 held**: Task 2 ran beside the controls, and 4d's map, plan, reviews, rows and recorder commit
  beside 4c's lanes and chain; no rework.
- **Task 2 as built.** The literal moved byte for byte (5617 bytes, the same sha256 before and after). Against
  the Rules check: **ruling D-1**, d3d9-rt's `Cargo.toml` moves the workspace crate mtld3d-types from
  dev-dependencies to dependencies (the D3D sampler constants; `Cargo.lock` unchanged, no external crate); two
  `CoreDevice` fields, not three (`quad_functions: OnceLock<Option<[Obj; 2]>>`, the library released once its
  functions exist, and `quad_pipelines`); new `pub(super)` `copy_source` and `view_twins`; mtld3d gains a const
  `entry_name` helper. Every failure line says "does not build"; a refused pipeline logs its format and count
  without Metal's text (`render_pipeline` drops it; final review M1). Red rust-check runs on the way were the
  tests' or lints' own (a resolve pass prints no size on the fakes, two clippy lints, a doc shape, a fixture
  helper), no code change.
- **Tasks 3-5 as built.** Only `core_arms.rs` and one line of `device.rs`. Step 0 (one texture's identical
  rects) is in the first commit; one `with_scratch` helper forgets the scratch on every path; the back buffer's
  view comes from `map_for_device`; the planar scratch is BGRA8 with its twin; a multisampled back buffer onto
  itself goes to `stretch` (the presented texture read, the companion written). `rc4.log` was red on rustfmt and
  on one mtld3d-unix timing test (`submit_spawn_failure_joins_encoder_and_service_workers`), green at the next
  run: a flake of a loaded Mac, not this range's code (it returned six times in a row during 4d's Task 3 while
  the chain's builds ran).
- **Lanes**, each exactly as predicted (translator key the base's; `was not recorded` 0): `jt3` (7bedadf2) core
  red on `blit-sq-yuv` (its YV12 cell: the packed cells passed), `blit-sq-ms`, `blit-sq-overlap`,
  `blit-sq-plain`, `targets-rt-msaa`, `targets-runtime`, and `blit` under core with the validation layer:
  reports 0, rows ok 39; `jt4` (520b49cd) red on `blit-sq-plain` alone, `blit`, `targets`, `msaa` under the
  layer: reports 0, rows ok 42, 37, 22; `jt5` (8bde3b14) default and core ok, 41 of 41 and 20 of 20 on both,
  the three launches under the layer: reports 0, rows ok 43, 37, 22; no skip line under core either.
- **Mutants**: eight (b, c, g, h, i, j, l, m), `gates/mutants-expect.txt` written before any existed, from the
  rows as committed. Mutant j's first form made the call answer false; it was regenerated as "writes at the
  source's point" before any build.
- **The chain** at 8bde3b14 with eab85179's rows (`jmb`, `jr`, `jc`, `jq`, `jm`, `jw`; 22:28 to 00:23). `jr`:
  key and imports the base's, d3d9-check default ok (41 and 20 rows), the conformance fails anew at triaged
  sites only (both runs), "outside src/d3d9: none", "x64 check: not run". `jc`: the VM's gate ok on default and
  core, old and new WoW64, 41 `bt64`/`bt32` and 20 `ms64`/`ms32` ok lines each (one more under core), no
  "was not recorded" line: the shared shader compiles and draws in the guest, 64-bit and 32-bit. `jq`: default,
  msl and core ok with every row, stateblock on both runtimes, the eleven launches under core with the
  validation layer: reports 0 (`blit` 43, `targets` 37, `msaa` 22 rows ok). `jm`: the eight mutants red on
  their tokens, each cell and colour as `mutants-expect.txt` had it but one (h with one "was not recorded"
  line), the green tip whole. The one: b's `sq-order` fails at the wireframe copy's cell, B (6,46) rust want
  steel, which the row judges before the level cell the expectation named (A's origin is rust by that copy
  too). **Mutant i reddened `blit-sq-conv` beside the predicted `blit-sq-order`**: its back-buffer cell reads
  steel in A, a colour the row never puts in the back buffer (lilac, a rust quad), so the copy sampled the
  back buffer before the row's own Clear and quad; whose steel it was is not traced. Ruling J-2: the
  prediction undercounted the mutant's reach, as J-1 in 4b; the tip is green on both; no rerun.
- **Final review** (`build/c4c/s4c/review-final.md`, range 30080983..8bde3b14 and the rows): 0 Critical, 0
  Important, 5 Minor; "push as is once the gates are green". Forgetting a scratch right after recording is
  safe (the initializer's batch keeps the allocation until its zero-fill completes, and every commit flushes
  that batch first). Ledgered, no fix round: M1 (above); M2 the Rules check's omissions (above); M3
  `stretch.rs` restates two Metal values locally (the language version, the math mode) against "ABI constants
  have one home": their home would be the wire crate, outside `src/d3d9`, a follow-up; M4 two doc comments
  still name `copy` as the only reader of a level that is no target (fixed in 4d's front-end commit); M5
  `stretch`'s multisampled back-buffer source has no unit test of its own. Not covered by a row or test, most
  important first: a sampled copy from a multisampled back buffer; a sampled copy into the back buffer (the
  sweep's `yuv_layout_test`, `clear_test`, `test_filling_convention` reach it); a YUV source's sub-rect; a
  swizzled view of a level above 0 or of a cube face; a multisampled standalone target onto itself and a scaled
  copy into a sub-rect of a multisampled target; a scaled copy between two faces of one cube; LINEAR on a
  converting copy (the sweep's `test_blit_format_conversion`); R-1 (not reachable on this GPU); a scratch's
  release; a block-compressed plain surface onto itself off the block grid.
- **The x64 check** did not run in this slice (the range is inside `src/d3d9`); it runs at the end of C4c-4d.
- **Sweep** (`build/c4c/s4c/sweep.md`, job `jw`, gating nothing): each function as the Goal's table predicted:
  `yuv_layout_test` 992 to 0, `yuv_color_test` 106 to 0, `test_multisample_stretch_rect` 54 to 0,
  `test_stretch_rect` 40 to 0, `stretchrect_test` 17 to 0, `test_filling_convention` 14 to 0,
  `test_blit_format_conversion` 8 to 0, `np2_stretch_rect_test` 5 to 0, `clear_test` 5 to 0,
  `multisampled_depth_buffer_test` 12 to 5. New "Test failed" lines over the default runtime's baseline:
  `visual` 1278 to 70, `device` 155 to 115, `d3d9ex` 66 as before; Wine's own failed counts 1528 to 320 and 894
  to 854 (the same falls: no todo check changed).
- **Found beside the slice**: a `d3d12_xess.exe` of C4c-4a's first x64 check (the `xess-wd` hang of ruling X-1)
  had been spinning on one core since 2026-11-18 11:59, with its prefix's seven service processes; stopped by
  PID on 2026-11-19 00:25. `dxmt/check.sh`'s watchdog does not reap the process it gives up on: a follow-up.
