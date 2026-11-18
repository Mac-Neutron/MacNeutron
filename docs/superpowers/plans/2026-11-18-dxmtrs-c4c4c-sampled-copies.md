# DXMTRS C4c-4c: `StretchRect`'s scaled, converting, YUV, multisampled and overlapping copies on D3D9's core runtime

Slice 4 of C4c, third part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push).
Maps: `build/c4c/s4c/map.md` (cited "map 1a", "map 2b" ...), `build/c4c/s4b/map.md` ("4b map"),
`build/c4c/s4/map.md` ("old map"). The 4a and 4b plans (`2026-11-18-dxmtrs-c4c4a-fill-update-validation.md`,
`2026-11-18-dxmtrs-c4c4b-unscaled-copies.md`) hold the rules this one repeats by name only. Sweep:
`build/c4c/s4b/sweep.md`. Reviews: `build/c4c/s4c/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and pixels, a valid `StretchRect` **into a render target**
works whatever its shape:

- **scaled** (rects of different sizes; NONE and POINT nearest, LINEAR bilinear), from and into sub-rects;
- **converting** (formats whose bytes do not copy: X8R8G8B8 into A8R8G8B8 with alpha one, R5G6B5 to and from
  8888, 8888 to and from float targets ...), 1:1 or scaled;
- **from a YUV plain surface** (packed YUY2 and UYVY, planar YV12 and NV12), any size;
- **into a multisampled target**, and **from a multisampled target** in the shapes the whole-surface resolve
  does not cover (a sub-rect, a scaled copy, into an AUTOGENMIPMAP level, multisampled into multisampled, the
  additional swap chain's back buffer);
- **between overlapping rects of one subresource**, and scaled between two levels or faces of one texture;

and **a plain surface copied onto itself** with different rects (on the CPU).

Still refused by name (`D3DERR_NOTAVAILABLE`, the same literal): **the depth-stencil resolve** (ruling S-6: its
own slice, C4c-4d), an extended device's whole-surface copy into a non-target (4b's ruling X-2), a plain pair
no CPU route serves (4b's K-1), and a multisampled source of a format this GPU does not resolve.

Wine, predicted from 4b's sweep and the test sources (map 3c; "Test failed" lines over the default runtime's
baseline; the reviews derive the numbers, the ledger holds them before the sweep): `yuv_layout_test`,
`yuv_color_test`, `stretchrect_test`, `test_multisample_stretch_rect`, `test_blit_format_conversion`,
`clear_test`, `np2_stretch_rect_test`, `test_filling_convention`, `test_stretch_rect` each to 0;
`multisampled_depth_buffer_test` keeps what its depth resolve decides.

## Global Constraints

The 4b plan's, unchanged, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn`
(`c4c-mn`); **base DXMTRS main and MacNeutron main as C4c-4b pushed them** (the ledger names both SHAs; fetch
before the first build and before the push); job scripts in `build/c4c/s4c/gates`, copied from s4b's and edited
before their first run (guard: `/usr/bin/grep -nE 'c4c/s4b|c4c4b|C4c-4b'` over them prints nothing; VM state
names `c4c4c-*`); `base-key.txt` and `base-imports.txt` copied from s4b when 4b's `jr` and `post` printed
"same"; a rerun is a new job name; every verdict counts its rows and its `was not recorded` lines (0 wanted);
every `d3d9-check.sh` run sets `DXMT_CHECK_WORK`; the pipeline lock; no implementer runs `dxmt/toolchain.sh`;
each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- **A core arm is keyed on the device being core.** No mtld3d recorder call reachable on a core device; no
  read of a `StretchKind`'s handle below an arm.
- **The invariant: on a core device only render-target levels, standalone targets and the back buffer are
  written by the GPU.** A scratch texture of this slice is private to one call and never an application
  surface.
- **Every stretch refusal goes through `refuse_stretch()`**; a copy the core does not record logs "was not
  recorded" and answers `D3DERR_INVALIDCALL`.
- **Files outside `src/d3d9`:** none in shipped code. Rulings M-2 and F-1 below name the two places this
  slice may touch that mtld3d or the tests share.
- **The x64 check (`dxmt/check.sh`) runs once at the end of C4c-4**, which ruling S-6 makes C4c-4d: not in this
  slice's chain or post-push check unless the DXMTRS range leaves `src/d3d9` (shipped code; ruling F-1's
  test-support file does not count, and `jr.sh`/`post.sh` say which files they found).

## The mechanism

### The shader (shared with mtld3d; ruling M-2)

mtld3d's blit source (`unix/src/metal/blit.rs:67-183`: `mtld3d_blit_vs`, `mtld3d_blit_ps`; map 1a quotes it)
moves, byte for byte, into the shared crate (`mtld3d_core::stretch_rect`, a `pub const BLIT_MSL: &str` beside
`BlitDecode`), and `unix/src/metal/blit.rs` compiles that constant. One text, two runtimes; mtld3d compiles
the same bytes with the same options.

### d3d9-rt: `stretch` (new file `rt/src/device/stretch.rs`)

`pub fn stretch(&mut self, quad: &StretchQuad) -> bool` (`#[must_use]`), with
`pub struct StretchQuad { src: TargetRef, src_rect: [u32; 4], src_view: u32, dst: TargetRef, dst_rect: [u32; 4],
linear: bool, decode: BlitDecode }` (`src_view`: the `WMTPixelFormat` to sample the source through, its
swizzle bits included; ruling SW). One render pass that samples the source's one subresource onto the
destination rect:

- **Source**: any stored colour level (`level_shape` + `stored_texture`, as `copy`) or the single-sample back
  buffer, through **a 2D view of that one level and slice** (the key `texture_target` builds, with `src_view`'s
  format), so a level above 0 and a cube face are sampled as level 0 of the view. **Destination**: a target
  (`shape`/`resolve`), through its stored format's view (never the sRGB twin: the copy is verbatim, map 1a),
  **multisampled or not**.
- False, and nothing recorded: an end that does not resolve; a multisampled source; an empty rect or one that
  leaves its level (`checked_add`); **one texture at both ends** (`Arc::ptr_eq`: a pass does not sample what it
  attaches; the front end goes through a scratch); a planar decode (`Yv12`, `Nv12`: the core's planar texture
  holds luma alone, map 2d; the front end decodes on the CPU); a library, pipeline or sampler Metal does not
  make (logged once).
- **Uniforms by a pure function** `quad_uniforms(src: &Shape, rect: [u32; 4], decode) -> ([f32; 4], [f32; 4])`
  (the vertex `xform` = rect over the level's extent, as mtld3d computes it; the fragment `(0.0, decode,
  level width, level height)`: with a one-level view the level uniform is 0), unit-tested.
- **Recorded**: `close_pass()`, then one `emit`: a render pass on the destination view (load LOAD, store
  STORE), the source accessed READ, viewport and scissor the destination rect, the pipeline of
  `(destination stored format, destination samples)`, the sampler (min and mag LINEAR or NEAREST, mip NEAREST,
  clamp), three vertices. Then `note_written(dst, stored format)`: unlike a blit the quad writes through a
  view, and a later resolve or Present of a multisampled destination reads that note.
- **Owned by `CoreDevice`** (no static): the library (made at first use by
  `MTLDevice_newLibraryWithSourceOptions` with mtld3d's options, MSL 2.4 and fast math; the spike
  `build/c4c/s4c/gates/jsp.log` showed the call working in every core launch on the Mac and in the VM, 64-bit
  and 32-bit, old and new WoW64), the two functions, a map of pipelines by `(format, samples)`, the two
  samplers.

### Front end (`core_arms.rs`)

`stretch_colour` keeps step 1 (the resolve) and its plain-destination routes. Its refusal of step 2 goes; for
**a destination that is a target** the arm now, in order:

1. **settles the source** to a single-sample ref and a decode:
   - a planar YUV plain surface: its staging decoded on the CPU (`pixel_convert::convert_region` into
     X8R8G8B8, the whole level) and uploaded into a scratch texture; decode none;
   - a packed YUV plain surface: its own core texture (`core_flush_dirty`), decode `Yuy2` or `Uyvy`;
   - a multisampled source: `core.resolve_into(source, scratch)` (a scratch target of its size and stored
     format); false (a format this GPU does not resolve) → `refuse_stretch()`;
   - anything else: 4b's ref (`core_target()`, or the parent texture's level after `core_flush_dirty`); decode
     `blit_decode(src format)` (mode 5 for the three X formats, as mtld3d);
2. **one texture at both ends** (4b's test; a scratch source is never the destination's): `Skip` → `D3D_OK`;
   `Direct` and unscaled → 4b's `copy`; otherwise (overlapping rects, or a scaled copy between two levels or
   faces) the source region is copied into a scratch target of its size and stored format, and the scratch is
   the source from here;
3. **the copy**: unscaled, same bytes, single-sample destination → `core.copy` (4b's blit); anything else →
   `core.stretch` (the filter: LINEAR when the call says LINEAR; the view format by ruling SW);
4. on success, as 4b: the destination's claim and an autogen chain's regeneration; every scratch is forgotten
   (`forget_texture`: the recorded closures hold their own).

A scratch is `core.texture_create(TextureId::new_unique(), ..)` (a target when a blit or a resolve writes it,
uploaded by `texture_upload` when the CPU fills it) inside one helper that forgets it on every path.

**A plain surface onto itself** (step 4 of 4b's arm, `SameSurfaceRoute` other than `Skip`): the source rect's
rows are copied out of the staging, then written back at the destination point by
`copy_bytes_to_staging_region` (one texture, so no second borrow), overlapping or not.

`stretch_depth` is unchanged: the resolve stays refused (S-6).

## Review Focus (for the final review)

- mtld3d unchanged: the moved shader text byte-identical; every other hunk behind `cfg` and the device test.
- `stretch`: what it attaches, samples and tracks; the uniforms; the destination's sample count; `note_written`;
  pass state after it; every refusal; what the device owns.
- Every scratch: who writes it, in what format, that it is forgotten on every path and never reachable as an
  application surface.
- The routes against mtld3d's answers for every shape the gates let through; the one-texture rule.
- The invariant.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in the Appendix: ten new `sq-*` rows in the `blit` launch, `sr-shape` cut down to the one shape still
refused (the depth resolve), `sc-self`'s overlap cell and `targets`' `rt-msaa` 32x32 cell judged instead of
strict-refused, one cell added to `sc-plain2` (4b's review: the CPU route's dirty mark on a destination that
already has a core texture). Scripts: the `check blit` lists and row loops; **the refusal sets**: `targets`
moves to "none" (`d3d9-check.sh`'s `refusals_ok`, `d3d9-gate.sh`'s `stretch` loop), `blit` keeps its one line
(`sr-shape`), `msaa` stays none; prose; `sh -n`.

**Rules for every new cell**: the 4b plan's (rects off the origin, widths no multiple of 4, inside / just
outside / (0,0) texels, written colours distinct, no judgement of unwritten texels, depth state restored), plus:
- a scaled cell judges texels **whose source texel is unambiguous under nearest sampling** (no probe on a
  source texel boundary), and a LINEAR cell judges one texel of each flat area and one texel whose two source
  texels differ, against the value computed from the mapping of map 1a, within 1;
- **every converting cell reads the destination's alpha** where the destination has one (X8R8G8B8 into
  A8R8G8B8 reads ff), and a 16-bit destination is read with the 2-byte reader;
- a format a lane may lack is asked with `CheckDeviceFormat` (`offered`) and skipped with a skip line; YUV
  cells use `CheckDeviceFormatConversion` where Wine's tests do;
- each scaled or converting cell has, somewhere in its row, a destination the copy does not cover whole.

Job scripts in `build/c4c/s4c/gates` from s4b's (`head.sh` exists from the spike: `BLIT_ROWS=41`,
`MSAA_ROWS=20`; `jv0.sh` runs `blit`, `targets` and `msaa` on mtld3d under the validation layer).

**Controls, in this order:**
1. `jv0` on the row commit: reports 0 and the row counts. **It decides ruling SW**: `sq-swizzle`'s cells on
   mtld3d say what mtld3d's quad samples a swizzled source through. If they read the D3D colours (luminance
   replicated, 4444's channels in place), the row stays as written. If they read the raw stored texels, that is
   an mtld3d finding for the maintainer; the cells become answer-only in a second row commit (`jv0b` confirms),
   and the core still binds the swizzled view (the D3D answer).
2. `ja` on the final row commit: default and msl all green at the first run; core red on exactly the ten
   `blit-sq-*`, `blit-sc-self` (its overlap cell), `targets-rt-msaa`, `targets-runtime`, and nothing else
   (`sr-shape` green: the depth resolve is refused at the base too; `sc-plain2`'s new cell green: it is 4b's
   code).
3. `jcr`: the VM's default lane on old and new WoW64, row lines counted.

Commit: "dxmt, vm: d3d9_triangle rows for StretchRect's sampled copies (DXMTRS C4c-4c)".

### Task 2: the shader's move and `stretch` (d3d9-rt)

Two commits. (i) "d3d9: the StretchRect quad's shader in the shared crate": the constant's move alone;
`git diff` shows the same lines removed and added; mtld3d's unit tests pass untouched. (ii) "d3d9: a sampled
copy on the core runtime": `stretch` and its tests on the fakes:
- `quad_uniforms`: a sub-rect of level 1 of a 64-texel texture; each decode's number;
- refusals, each false with no line (drain and take the calls first): an unknown end, a multisampled source,
  a non-target destination, an empty rect, a rect past its level, one texture at both ends (two levels of it
  included), a planar decode;
- a stretch records one render pass on the destination's view with one draw of three vertices, after the open
  pass's end; `!render_open` after it; the pipeline call once for two stretches into one format and count,
  twice when the count differs; a 4-sample destination's pass names its count;
- **fences** (4b's form): a Clear of the source level, then a stretch from it: the quad's pass waits on the
  Clear's; a stretch into level 1 (and into a cube face), then a real draw on it: the draw waits on the quad's
  pass;
- `note_written`: a stretch into a 4-sample target, then `resolve_into` from it, resolves through the stored
  format.

The fakes need `MTLDevice_newLibraryWithSourceOptions` (ruling F-1).

### Task 3: scaled and converting copies between single-sample ends (front end)

The source's ref and decode for every single-sample source that is no planar surface, step 3's choice between
`copy` and `stretch`, ruling SW's view format, the claim and the autogen chain. A multisampled end, one texture
at both ends beyond `Direct`, and a planar source still answer `refuse_stretch()`. Lane `jt3` (default, core):
green strict under core: `sq-scale sq-level sq-conv sq-swizzle sq-order sq-autogen` and `sq-yuv`'s packed
cells' row if no planar format is offered; failing tokens predicted and written in the ledger before the run.
**The `blit` launch once under core with the validation layer.** Commit: "d3d9: StretchRect scales and
converts into a target on the core runtime".

### Task 4: scratch routes (front end)

The scratch helper; a multisampled source; a multisampled destination; one texture at both ends (overlap,
scaled levels and faces); the planar decode. Lane `jt4`: `sq-ms sq-overlap sq-yuv` green, `sc-self` and
`targets`' `rt-msaa` green, `targets` with no refusal line. **`blit`, `targets` and `msaa` once under core with
the validation layer.** Commit: "d3d9: StretchRect through a scratch texture on the core runtime".

### Task 5: a plain surface onto itself (front end)

Lane `jt5` at the tip: every row of every launch green strict under core, every refusal set exact (`blit` 1,
`targets` 0, `msaa` 0), `was not recorded` 0. Commit: "d3d9: StretchRect of a plain surface onto itself on the
core runtime".

### Task 6: gates

As the 4b plan's Task 6. Mutants (final lists in `gates/mutants-expect.txt` from the rows as committed, before
`jmb`):
  a. `stretch`'s viewport and scissor at the destination's (0, 0);
  b. the vertex uniform's origin zeroed (the source rect read from the level's origin);
  c. the front end hands `stretch` LINEAR for every filter;
  d. the front end hands decode none for an X source (alpha not forced);
  e. the source view names level 0, slice 0 of its texture;
  f. no `note_written` after a stretch;
  g. the multisampled source is sampled without its resolve into the scratch (the arm skips the resolve and
     hands the scratch as is);
  h. the overlap route copies straight (4b's blit refuses it: an error line), or its scratch copy reads the
     destination's origin;
  i. `stretch` tracks no source read;
  j. the plain self-copy writes at the source's point.
  Each red on exactly its tokens. Not gated by a row mutant, stated: `quad_uniforms`'s fields (the unit test);
  the destination's pipeline sample count (a wrong one is a validation report in `jq`); the scratch's release
  (no leak is a test's to see: the final review reads every path).
- `jw`: the sweep → `build/c4c/s4c/sweep.md`.
- **Probe: none** if `recorder.rs`, `draw.rs`, `target.rs` and `texture_shape` are untouched; `stretch` adds
  fields to `CoreDevice`, which no draw reads.

### Task 7: review. Task 8: spec, pin, push.

As the 4b plan's.

## Rules check

- **A static:** none (mtld3d's own `OnceLock` library stays in its unix side; the core's library, functions,
  pipelines and samplers are fields of `CoreDevice`). **A config key, a wire field, a dependency, a derive, a
  lint suppression:** none.
- **New pub items:** `CoreDevice::stretch`, `StretchQuad`; `mtld3d_core::stretch_rect::BLIT_MSL`. **New
  state:** the five `CoreDevice` fields above.
- **A duplicate:** none meant: one shader text for both runtimes; `stretch`'s rect checks use `surface.rs`'s
  `inside`; the routes reuse 4b's refs, `copy`, the claim and the regeneration; the CPU decode is
  `pixel_convert`'s.
- **Not silent:** a quad Metal cannot make logs once and answers false; the arm logs "was not recorded".
- **Divergence added:** planar YUV is decoded on the CPU and then sampled (ruling Y-1); a multisampled source
  is resolved into a scratch where mtld3d samples the resolve its passes keep. Stated under "Stated gaps".

## Rulings (planning)

- **S-6. The depth-stencil resolve is C4c-4d, not 4c.** mtld3d does it with a compute kernel that reads sample
  0 of both planes and plane blits (map 1d); dxmt-core has no depth resolve and the wire's attachments no
  resolve fields (map 2f). It shares nothing with the sampled quad. Cost if wrong: one more slice's fixed cost
  (about three hours); the x64 check moves to 4d's end with it.
- **Q-1. The quad is a pass of d3d9-rt's own, compiled from mtld3d's source, not an internal fixed-function
  draw through `CoreDevice::draw`.** The same shader gives the same texel mapping, filter and packed-YUV
  decode as the default lanes expect; a fixed-function draw would couple the copy to the front end's draw
  resolution and its half-pixel fixup and cannot decode YUV. The spike settled the one unknown (compiling MSL
  from the PE side in every lane). Cost if wrong: a Metal source compile at a device's first sampled copy
  (once a device; mtld3d pays the same).
- **M-2. The shader text moves to the shared crate**, a hunk of mtld3d's code outside `cfg`: a constant's
  move, byte-identical. Its gates: mtld3d's unit tests untouched, the default lanes, the translator key, the
  Mac's conformance. Cost if wrong: caught by those; the alternative (a second copy in d3d9-rt) is a duplicate
  the rules forbid.
- **F-1. The test fakes gain the library-from-source call** if d3d9-rt's unit tests cannot provide the symbol
  from their own fixture (tried first: an export in `rt/src/device/fixture.rs` under `cfg(test)`). If the
  fakes crate (`src/dxmt/fakes`, test support, in no shipped binary) must change, it gains that one export,
  and the x64 check is still not owed (no shipped file outside `src/d3d9` changes).
- **SW. A swizzled source is sampled through its D3D format's view** (`view_format(stored, swizzle)`: an X
  format's alpha one, L8 replicated, A4R4G4B4's channels in place), from the device's mapping of the source's
  D3D format. Decode mode 5 stays for the X formats, as mtld3d has it. What mtld3d's own quad does with L8 and
  A4R4G4B4 is not established (map open question 1): `jv0` on `sq-swizzle` says, and decides whether the row
  judges pixels (Task 1).
- **Y-1. Planar YUV is decoded on the CPU** from the plain surface's staging and uploaded to a scratch, then
  sampled: the core's planar texture holds luma alone. NONE and POINT match mtld3d within the decode's
  rounding (Wine's tolerance is 1); under LINEAR mtld3d filters luma only and the core filters decoded RGB.
  Cost if wrong: a LINEAR planar copy differs from mtld3d's at chroma edges; fixing the core's planar texture
  layout is the later remedy.
- **R-1. A multisampled source of a format this GPU does not resolve is refused by name.** Cost if wrong:
  32-bit float multisampled targets before Apple9 answer `NOTAVAILABLE` where mtld3d resolves in its pass.
- **P-3. No probe** (Task 6's condition).

## Stated gaps

Refused by name after 4c: the depth resolve (4d); an extended device's copy into a non-target; a plain pair
outside every CPU route; R-1's formats. Not built, stated: LINEAR on a planar source (Y-1); a scratch texture
is made and released at each call that needs one (a cache is the later remedy if a title leans on these
shapes); LINEAR sampling of a 32-bit float source is whatever Metal does on this GPU, as on mtld3d.

## Appendix: the rows

Launch `blit` (31 + 10 = 41 rows; the new ones after `sc-order` and before `sc-depth`, which stays last).
Surfaces are 64x64 A8R8G8B8 unless said.

| row | cells |
|---|---|
| `sq-scale` | a 32x32 target of four flat quadrants into a 64x64 target whole, POINT (2x: each quadrant's texels) and NONE; a's {8,8,24,24} into b's {5,7,37,39} (2x into a sub-rect): inside, the texel just outside, (0,0); a 64x64 target into a 16x16 one, POINT (the nearest texel of each quadrant); LINEAR 2x of a two-colour edge: one texel in each flat area and the blended one |
| `sq-level` | level 1 of a 2-level render-target texture (cleared another colour than level 0) scaled into a target; a target scaled into level 1 of a texture and into face 3 of a cube (face 0 unchanged); face 2 of a cube scaled into a target |
| `sq-conv` | the back buffer's rect (X8R8G8B8) into an A8R8G8B8 target, 1:1: the colour and alpha ff; an A8R8G8B8 target into an R5G6B5 one (2-byte read) and back; an A8R8G8B8 target into an A16B16G16R16F one and back, if offered; a usage-0 X8R8G8B8 texture's level into an A8R8G8B8 target (alpha ff) |
| `sq-swizzle` | an A4R4G4B4 plain surface and an L8 one (each if offered), written under a lock, 1:1 into an A8R8G8B8 target: the D3D colours (ruling SW; the control decides whether pixels are judged) |
| `sq-yuv` | a YUY2 and a UYVY plain surface (each if the conversion is offered) of flat macropixel areas into a target, 1:1 and 2x POINT: the decoded colours within 1; a YV12 and an NV12 one likewise, POINT |
| `sq-ms` | a target into a 4-sample target whole, then that into a third target by the whole resolve: the colours (the note); a target's rect into a 4-sample target's rect, resolved and judged with the texel just outside; a 4-sample target's rect into a target, 1:1 at an offset and 2x; a 4-sample target into another 4-sample one, resolved and judged |
| `sq-overlap` | a target's {4,6,37,38} onto its own (20,18), overlapping: the moved texels as a copy through a buffer gives them; an additional chain's back buffer's rect onto the device's, overlapping; level 0 of a 2-level render-target texture scaled onto its level 1 |
| `sq-plain` | a plain surface's rect onto itself, disjoint and overlapping: read under a READONLY lock, then copied into a target (the CPU write reaches the GPU) |
| `sq-order` | in one scene: a Clear of a, a scaled copy of a into b, then a draw on a: b holds the Clear's colour; a draw on level 1 of a texture, a scaled copy into it covering part of the draw, a draw beside it; the same on a cube's face 3 |
| `sq-autogen` | a 32x32 target scaled into level 0 of an AUTOGENMIPMAP render-target texture, then a minified draw of it reads the new colour |

`sr-shape` keeps one cell: a 4-sample D24S8 surface into a single-sample one, `NOTAVAILABLE` under strict,
`D3D_OK` otherwise (the launch's one refusal line). `sc-self`'s overlap cell becomes `sq-overlap`'s first.
`sc-plain2` gains: q copied into a target before the plain copy into q, and again after it (the dirty mark).
Launch `targets`: `rt-msaa`'s 32x32 cell answers `D3D_OK` in every lane and its colour is judged.

**The quad's fields and the cell that decides each:** destination origin and size `sq-scale` (sub-rect);
source origin `sq-scale`; source level `sq-level`; source slice `sq-level` (face 2); destination level and
slice `sq-level`; filter `sq-scale` (LINEAR against POINT); decode 5 `sq-conv` (alpha); the view's swizzle
`sq-swizzle`; the packed decodes `sq-yuv`; the destination's sample count and the note `sq-ms`.

## Review dispositions

(after the three reviews)

## Execution notes

(after execution)
