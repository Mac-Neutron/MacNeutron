# DXMTRS C4c-4b: `StretchRect`'s unscaled copies on D3D9's core runtime

Slice 4 of C4c, second of three parts (ruling S-4 of the 4a plan). Process: the C4 program's (code map, this
plan, three adversarial reviews folded before code, rows first, one commit a feature, full gates once, one
whole-branch review, spec, pin, push). Maps: `build/c4c/s4b/map.md` (cited "map 1a", "map 2b" ...) and the
whole slice's `build/c4c/s4/map.md` ("old map A1" ...). The 4a plan
(`2026-11-18-dxmtrs-c4c4a-fill-update-validation.md`, "4a plan") holds the rules this one repeats by name
only. Sweep: `build/c4c/s4a/sweep.md`. Reviews: `build/c4c/s4b/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, a `StretchRect` that copies texels 1:1 (equal-size rects, no scaling, no
multisampled endpoint) works with mtld3d's answer and pixels:

- **into a render target** (a standalone target lockable or not, a render-target texture's level or cube face,
  the back buffer, an additional swap chain's back buffer) **from** any of those, from a DEFAULT texture's
  level that is no target, or from a DEFAULT offscreen plain surface, when the two formats' bytes copy
  (`copies_bytes`); sub-rects and distinct origins included; an AUTOGENMIPMAP destination regenerates its
  chain; two levels or faces of one texture, two disjoint rects of one surface, and the additional chain's back
  buffer against the device's included;
- **a plain surface into another plain surface**, on the CPU: a raw copy when the D3D formats are equal, the
  CPU conversion when they differ (mtld3d's `convert_stretch_dst_staging` for a cross-format pair);
- **the depth-stencil copy**: two depth-stencil surfaces of one format, size and sample count, whole.

Still refused by name (`D3DERR_NOTAVAILABLE`, the same literal; C4c-4c): a scaled copy, a copy that converts
into a target, a planar or packed YUV decode, any multisampled endpoint the resolve arm declines, the depth
resolve, and the shapes of "Stated gaps".

Wine (new "Test failed" lines under core, from 4a's sweep; map 3c): `test_pointsize` 570 (its copy of a
lockable target into the back buffer), `test_stretch_rect` 81 → about 40 (the 41 same-bytes and
plain-to-plain expectations), `stretchrect_test`'s 1:1 copies (51 → its scaled ones), `test_flip`,
`test_scene`, `depth_blit_test` 15 → fewer (three copies and the colours they decide), `test_mipmap_autogen`.
Predictions are per function, in "Test failed" lines, and are written in the ledger before the sweep.

## Global Constraints

The 4a plan's, unchanged, with this slice's names: worktrees `build/c4b3c/dx` (branch `c4c`) and
`build/c4b3c/mn` (branch `c4c-mn`); **base DXMTRS main 3d5dcda3, MacNeutron main 4355afd9**; job scripts in
`build/c4c/s4b/gates`, copied from `build/c4c/s4a/gates` and edited before their first run (the guard:
`/usr/bin/grep -nE 'c4c/s4a|c4c4a|C4c-4a' build/c4c/s4b/gates/*.sh build/c4c/s4b/gates/*.txt` prints nothing,
and the positive checks of the 4a plan hold with this plan's row count, letters and tokens; VM state names
`c4c4b-*`); a rerun is a new job name; every verdict counts its rows; every `d3d9-check.sh` run sets
`DXMT_CHECK_WORK`; no `dxmt/check.sh` run crosses 03:35; the pipeline lock; no implementer runs
`dxmt/toolchain.sh`; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

Restated because this slice leans on them:

- **No file outside `src/d3d9` changes** in DXMTRS. In particular `src/dxmt/fakes` stays as it is, although it
  prints a texture-to-texture blit only as `blit 4` (map 2f; ruling G-1).
- **A core arm is keyed on the device being core**, never on `core_target()` or `core_depth()` alone.
- **No mtld3d recorder call reachable on a core device; no read of a `StretchKind`'s handle below an arm.**
- **The invariant: on a core device only render-target levels, standalone targets and the back buffer are
  ever written by the GPU.** This slice keeps it in two places at once: the front end tests **the destination
  itself** (not the pair's class: a core device may be extended, and an extended device's whole-surface copy
  skips `class_reject`, map 1a), and d3d9-rt's copy resolves its destination through the target-only `shape`.
- **Every verdict also counts the core's error lines**: `/usr/bin/grep -rhc 'was not recorded'` over a
  launch's logs is 0 in every lane (ruling N-1): a primitive's `false` on a shape mtld3d accepts would
  otherwise hide as a plain `INVALIDCALL`.

## Scope

In: Tasks 1-8. Out, with where it goes: see "Stated gaps" (each still answers `NOTAVAILABLE` by name).

## The mechanism

### d3d9-rt

**A source lookup** (`textures.rs`): `level_shape(id, level, face) -> Option<Shape>`, the body of
`texture_shape` without its `RENDER_TARGET` test: any stored colour texture's level (a depth format is none),
`face` a cube's exactly when it is a cube. `texture_shape` becomes `level_shape` filtered by the flag; its
answers and its tests are unchanged (the order of its three `None` tests does not show). Nothing else calls
`level_shape` but `copy`.

**`copy`** (new, `surface.rs`, beside `resolve_into`):
`pub fn copy(&mut self, src: TargetRef, src_rect: [u32; 4], dst: TargetRef, dst_origin: [u32; 2]) -> bool`
(`#[must_use]`). `src` names **any stored colour level** (`TargetRef::Texture` through `level_shape`, so a
texture that is no target too) or the back buffer; `dst` names a target (`shape(dst)`, unchanged). `[x, y, w,
h]` in the texels of the source's level. False, and nothing recorded, when:

- either end does not resolve; either end has more than one sample (the back buffer's `samples` is its
  drawn texture's: a multisampled back buffer is refused at both ends);
- the two stored pixel formats differ (`Shape::pixel_format`; the sRGB twin is not looked at: a blit copies
  bytes);
- the rect is empty or leaves the source's level; `dst_origin` plus the size leaves the destination's
  (`checked_add`, as `rect_bytes` checks);
- both ends are one subresource (`Arc::ptr_eq` of the two textures, equal level and slice) **and the rects
  overlap** (identical included). Disjoint rects of one subresource are copied (ruling SS).

Recorded: `open_blit_pass()` (it closes an open render pass and shares the blit pass with uploads), then one
`emit`: `access_texture::<{ stage::COMPUTE }>(&src, level, slice, Access::READ)`, the same for `dst` with
`Access::WRITE`, `encode_blit_command(WmtcmdBlitCopyFromTextureToTexture { .. })` with both slices, both
levels, `src_origin`, `src_size` (depth 1), `dst_origin` (map 2a: D3D11's fill, `copy.rs:1408-1418`). No
`note_written` (a blit writes no view format, as `texture_upload` and `write_presented`; map 2b) and no
`staged_bytes` (nothing is staged).

**`copy_depth`** (new, `depth.rs`): `pub fn copy_depth(&mut self, src: DepthRef, dst: DepthRef) -> bool`
(`#[must_use]`). Both through `depth_level`; false, and nothing recorded, unless both resolve, they are two
textures or two levels, and width, height, pixel format and samples are equal; **more than one sample is
accepted or refused by ruling DM** (Task 1's control decides). Recorded as `copy` records, the whole level,
slice 0 (Metal copies both planes of a depth-stencil texture: no plane option, map 2a).

### Front end

`device_stretch_rect` keeps its gates and its two exits. The exits call two new arms in `core_arms.rs`
instead of `stretch_rect_resolve`, which becomes the first step of the colour arm.

**Exit 1** (the depth branch; everything in scope: both infos): `core_arms::stretch_depth(this, [src, dst],
[&src_info, &dst_info])`. Unequal sample counts (the resolve; the gate let through only "source more than one,
destination one") → the refusal by name. Else `core_depth()` of both and `core.copy_depth`; a missing ref or
`false` → an error line ("d3d9 core: StretchRect of depth {src:?} into {dst:?} was not recorded →
INVALIDCALL") and `D3DERR_INVALIDCALL`.

**Exit 2** (colour; in scope: both infos, both regions, `scaling`, `planar_endpoint`, `expand_packed16`,
both raw rects): `core_arms::stretch_colour(this, &StretchCall { surfaces, infos, regions, rects, scaling,
planar_endpoint, expand_packed16 })` (one struct, private to `device`; no handle in it is read). In order:

1. `resolve_shape(this, surfaces, rects).is_some()` → `D3D_OK` (4a's, unchanged, first: a multisample resolve
   is not a copy).
2. **Refused by name** (4c), before any state changes: `scaling`; `planar_endpoint`; either info's
   `sample_count > 1`; `cross_format` (`!copies_bytes(src.format, dst.format, expand_packed16)`) with a
   destination that is not `IS_OFFSCREEN_PLAIN_DEFAULT`.
3. **The destination is a target** (`dst_info.flags` has `IS_RENDER_TARGET`): the GPU route.
   - The refs: `dst.core_target()`; the source's is `src.core_target()`, or for a level that is no target
     `TargetRef::Texture { id: parent.texture_id(), level: src.mip_level(), face: src.cube_face() }` (map
     1c). A missing ref → the error line and `INVALIDCALL`.
   - **One texture at both ends** (both refs `BackBuffer`, or equal ids; map 1g): mtld3d's own rule,
     `mtld3d_core::stretch_rect::same_surface_route(src_region, dst_region, levels, slices)`: `Skip` →
     `D3D_OK`, nothing recorded; `Scratch` (here: overlapping rects of one subresource) → refused by name
     (4c); `Direct` → goes on.
   - `core_flush_dirty` of the source's parent texture and of the destination's (once when they are one):
     the source's core texture exists from here with every CPU write uploaded (a level that is no target, a
     plain surface: created and uploaded whole at first use, map 1b), and the destination's owed uploads land
     before the copy.
   - `core.copy(source, [x, y, w, h] of src_region, destination, [x, y] of dst_region)`; `false` → the error
     line ("d3d9 core: StretchRect of {source:?} {src_region:?} into {destination:?} was not recorded →
     INVALIDCALL") and `INVALIDCALL`.
   - On success, as `color_fill`: `claim_dst_subresource_for_gpu(dst, dst_info)` when the destination has a
     parent texture; `core.texture_generate_mips(id)` when `dst_info.autogen_texture_id` is set.
4. **The destination is no target**: the CPU routes, **only for a plain surface into a different plain
   surface** (both `IS_OFFSCREEN_PLAIN_DEFAULT`, two distinct non-null parent textures):
   - equal D3D formats → `dst_tex.inner_mut().copy_sub_region_from(dst level, src_tex.inner(), src level,
     Some(rect), point)` (map 1e: a raw block copy between the two stagings; it marks the written region
     dirty, so the plain surface's next use as a source uploads it and its lock reads it); `false` → the
     error line and `INVALIDCALL`;
   - unequal D3D formats (ruling K-1: `copies_bytes` compares Metal formats, the stagings hold D3D bytes):
     `pixel_convert::can_convert(src, dst)` → `super::convert_stretch_dst_staging(..)` as it is (pure CPU on
     core; its `flush_converted_mips` returns at once there, map 1e), else refused by name;
   - the same plain surface at both ends: `same_surface_route` `Skip` → `D3D_OK`; any other → refused by name
     (both CPU helpers need two textures; ruling SP);
   - **anything else** (an extended device's whole-surface copy into a level that is no target, or from a
     source that is no plain surface) → refused by name (ruling X-2).

`stretch_rect_resolve` is deleted (its two callers are the exits); the module doc and the arms' docs say what
is copied, what is refused and why the destination is tested. mtld3d's code behind the exits is untouched.

**What is GPU-authoritative afterwards:** a target destination, as after `ColorFill`. A plain destination
stays CPU-authoritative (dirty, uploaded at its next use as a source); where mtld3d claims it for the GPU and
reads it back at the next lock, the core never needs to (the invariant).

## Review Focus (for the final review)

- The invariant: every path that records a GPU write names a target, in the front end and in `copy`.
- No recorder call and no null-handle read reachable on a core device from either exit; every return path.
- `copy`'s refusals against every endpoint kind of old map D; the same-subresource rule; origins, levels and
  slices of both ends.
- Order against draws, Clears and uploads before and after a copy (the blit pass, the tracker).
- The CPU routes: formats, the dirty marks, what a later lock and a later copy read.
- mtld3d unchanged: every hunk in a function it runs is behind `cfg` and the device test.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in the Appendix: twelve new `sc-*` rows in the `blit` launch, `sr-shape` cut down to the cells that stay
refused, `ms-desc`'s strict expectation and one new `msaa` row by ruling DM. Wiring: `blitRows`, the header
prose, `dxmt/d3d9-check.sh`'s `check blit` list, `vm/dxmt/d3d9-gate.sh`'s row loop (`bt64-fresh`,
`bt32-fresh`), the `msaa` lists if the row is added; `sh -n` on the scripts. **The `msaa` launch's refusal
set** (`d3d9-check.sh:173`, `d3d9-gate.sh:258` and `:278-279`) moves to "none" when ruling DM's outcome A
holds (`ms-desc`'s copy was its only refused call, map 3b); `targets` and `blit` keep their one line
(`rt-msaa`'s scaled cell; `sr-shape`).

**Rules for every new cell** (the 4a plan's, plus):
- every copy cell that judges pixels has **a source rect off the origin, a destination origin different from
  the source's, and a width that is no multiple of 4**, and judges a texel inside the copied rect, one just
  outside it and one at the destination's (0,0); exceptions, named: whole copies and Wine's shapes;
- **each field of the blit has a cell only it decides** (the fakes cannot, ruling G-1): a source level of 1,
  a destination level of 1, a source cube face other than 0, a destination cube face other than 0, both
  origins;
- **no cell judges pixels copied from a source nothing has written** (its staging's first content is not
  established, map 1d): such a cell judges the answer alone;
- a copy cell has a draw or a Clear recorded before it on the source and a draw after it on the destination
  where the Appendix says "ordered";
- the depth cells clear and copy **with no draw between** (as `depth_blit_test`), and judge both planes;
- a strict-refused cell is spelled as `sr-shape`'s are (`NOTAVAILABLE` under strict, `D3D_OK` otherwise).

Job scripts in `build/c4c/s4b/gates` from s4a's: `head.sh` (`rows()` wants this plan's row count; a new
`notrec()` prints the `was not recorded` count of a work folder's logs), `ctl.sh`, `ja.sh`, `jv0.sh` (**the
`blit` and `msaa` launches** on mtld3d under Metal's validation layer), `jcr.sh`, `jt.sh`, `jq.sh`, `jr.sh`,
`jc.sh`, `jw.sh`, `jmb.sh`, `jm.sh`, `post.sh`, `chain.sh`.

Controls at the base + the rows, each failing set and row count written in the ledger before its run:
- `ja`: default and msl: every row green on mtld3d at the first run, no expectation corrected (a red row is a
  wrong row or a finding, settled before any code). Core, the failing tokens predicted: every `blit-sc-*` row
  (each meets the refusal), `msaa-ms-desc` and `msaa-runtime` under outcome A, and nothing else; `sr-shape`
  green (its remaining cells are refused at the base too).
- `jv0`: reports 0 and the row counts. **It settles the two Metal facts** (rulings DM and SS): a validation
  report on `sc-self`'s disjoint cell or on the multisampled depth copy is a finding about mtld3d (ledgered
  for the maintainer, not fixed here) and switches the ruling to its outcome B before any code.
- `jcr`: the VM's default lane on old and new WoW64, `bt64`/`bt32` ok lines counted.

Commit: "dxmt, vm: d3d9_triangle rows for StretchRect's unscaled copies (DXMTRS C4c-4b)".

### Task 2: `copy` and `copy_depth` (d3d9-rt)

As "d3d9-rt" above; two commits. Tests on the fakes as they are: each refusal answers false and records no
line (unknown id, a depth id as a colour source, a non-target as destination, a multisampled end, unequal
formats, an empty rect, a rect past the source's level 1 that fits level 0, an origin past the destination,
overlapping and identical rects of one subresource); a copy records exactly one `blit 4` inside a blit pass,
after an open render pass's end (`!render_open`) and before the next draw's pass, which loads; a copy from a
texture that is no target records one; a copy shares the blit pass of an upload recorded just before it and
follows it; `texture_shape`'s existing tests pass untouched; `copy_depth`'s refusals and its one `blit 4`.
**Geometry is not asserted here** (ruling G-1).

Commits: "d3d9: a texture-to-texture copy on the core runtime"; "d3d9: a depth-stencil copy on the core
runtime".

### Task 3: copies into a target (front end)

The `StretchCall` struct, `core_arms::stretch_colour` steps 1-3, exit 2's call; step 4 answers the refusal
by name for now. Lane `jt3` (default, core): green strict under core: `sc-rt sc-bb sc-lockable sc-rtex sc-tex
sc-plain sc-self sc-autogen sc-order`; failing tokens predicted: `blit-sc-plain2`, `blit-sc-conv`,
`blit-sc-depth`, and `msaa-ms-desc`, `msaa-runtime` under outcome A; default: all green. **The `blit` launch
once under core with Metal's validation layer** (reports 0). Commit: "d3d9: StretchRect copies into a target
on the core runtime".

### Task 4: plain surface to plain surface (front end)

Step 4. Lane `jt4` (default, core): `sc-plain2` and `sc-conv` green strict; failing tokens predicted:
`blit-sc-depth`, and the two `msaa` tokens under outcome A. Commit: "d3d9: StretchRect between plain surfaces
on the core runtime".

### Task 5: the depth-stencil copy (front end)

`core_arms::stretch_depth`, exit 1's call, `stretch_rect_resolve` deleted. Lane `jt5` (default, core) at the
tip: every row of every launch green strict under core, every launch's refusal set exact, `was not recorded`
0 everywhere, **and the `blit` and `msaa` launches once under core with Metal's validation layer** (reports
0). Commit: "d3d9: StretchRect copies a depth-stencil surface on the core runtime".

### Task 6: gates

As the 4a plan's Task 6 (`jmb`, then `jr`, `jc`, `jq`, `jm`, `jw`; the pre-ruled flakes; a hang of
`check.sh`'s `xess-wd` row is run once more under a new job name, as 4a's `jx`). Mutants, as commits off the
tip, each red on exactly its tokens; `gates/mutants-expect.txt` names the cell and the texel or answer from
the rows as written, before the chain:

  a. `copy` writes at the destination's (0, 0) → every row with a destination origin off (0, 0);
  b. `copy` reads from the source's (0, 0) → every row with a source rect off the origin;
  c. `copy` reads level 0 of its source → the source-level cells (`sc-tex`, `sc-rtex`, `sc-self`);
  d. `copy` writes slice 0 of its destination → the destination-face cells (`sc-rtex`, `sc-self`);
  e. the target route skips the source's `core_flush_dirty` → `sc-tex`'s and `sc-plain`'s first copies answer
     `INVALIDCALL` with an error line (no core texture), and the owed-upload cells read the old texels;
  f. no `texture_generate_mips` after a copy → `sc-autogen`;
  g. the plain route copies to the destination's (0, 0) → `sc-plain2`;
  h. `copy_depth` copies the destination into the source → `sc-depth`.

  Not gated by a mutant, stated: the destination test (a GPU write of a non-target needs an extended device,
  which the rows do not make; the unit test of `copy`'s destination refusal holds the recorder's side);
  `copy`'s source face (if no row has a source face other than 0 that a destination-face mutant leaves alone,
  the Appendix adds one); the claim after a copy (as 4a's ruling T1-1).
- `jw` (gates nothing): the sweep → `build/c4c/s4b/sweep.md`, per function before and after with the unit
  named (4a's sweep is "before").
- **Probe: none** (ruling P-2).

### Task 7: review

One whole-branch Opus review (`build/c4c/s4b/review-final.md`), given the diff and this plan; Critical and
Important fixed with a red cell first where a pixel can show it; Minors fixed if one line, else ledgered.

### Task 8: spec, pin, push

Spec §5 paragraph "C4c-4b" and the slice list. Plan execution notes. `dxmt/pins`. Privacy scan of both
ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron (the SHA written out). Post-push
check. CI. Memory. Scratch deleted by full path once each verdict is ledgered.

## Rulings (planning)

- **S-5. What 4b copies.** Unscaled copies with no multisampled endpoint; everything else that is valid
  stays refused by name for 4c. Cost if wrong: a shape a game needs waits one slice; the core runtime is
  opt-in.
- **G-1. Geometry is gated by rows and mutants, not by the fakes.** Decoding opcode 4 in `src/dxmt/fakes`
  would be a change outside `src/d3d9` (every D3D10-12 unit test reads those logs). The rows give each blit
  field a cell only it decides, and mutants a-d turn those cells red. Cost if wrong: a field no cell decides
  is wrong unseen; the Appendix's table of fields is the check, and the final review reads it.
- **K-1. The CPU route is keyed on the D3D formats.** Equal → raw copy; unequal → conversion when the codec
  has the pair, else refused by name. `copies_bytes` is not asked there. Cost if wrong: a pair of equal Metal
  bytes and unequal D3D formats outside the codec answers `NOTAVAILABLE` where mtld3d copies; none is known
  among the formats a plain surface takes.
- **SS. Disjoint rects of one subresource are one blit**, as mtld3d's `Direct` route. Outcome B, if `jv0`
  shows Metal's validation layer rejecting it on mtld3d: the core refuses the shape by name (4c's scratch),
  `copy` refuses every same-subresource pair, the cell becomes a strict-refused one, and mtld3d's blit is a
  follow-up for the maintainer.
- **DM. A multisampled depth pair of equal counts is one blit**, as mtld3d records it (map 1g; Metal's
  documentation asks for equal sample counts, which reads as allowing them; d3d9-rt's own comments say the
  opposite of colour blits). Outcome A: `copy_depth` accepts it, `ms-desc`'s strict answer is `D3D_OK`, a new
  `msaa` row judges its pixels, and the `msaa` launch's refusal set is empty. Outcome B, if `jv0` shows a
  validation report on mtld3d or the row is red there: `copy_depth` and the front end refuse more than one
  sample by name, `msaa` keeps its one line, the new row is not added, and mtld3d's blit is a follow-up.
- **SP. A plain surface copied onto itself** with rects that are not identical is refused by name. Cost if
  wrong: as S-5; a CPU `memmove` inside one staging is the later fix.
- **X-2. An extended device's whole-surface copy into a non-target** is refused by name, as today. Cost if
  wrong: `d3d9ex`'s tests of it stay red (the suite dies in `test_reset` first).
- **N-1. `was not recorded` is counted in every verdict** and wanted 0. If a launch legitimately prints one
  at the base, Task 1 names it and the count wanted becomes that number.
- **P-2. No probe.** No draw file and no line of `recorder.rs` changes; `texture_shape`'s answers are
  unchanged and it is not on a draw's path more than before (one call deeper, inlined). If the range touches
  `recorder.rs`, `draw.rs` or `target.rs`, one bench pair, once.

## Stated gaps

Refused by name after 4b (the sweep counts them as 4c's): scaled copies; converting copies into a target;
YUV decodes; a multisampled source's sub-rect or a multisampled destination; the depth resolve; overlapping
rects of one subresource; a plain surface onto itself; an extended device's copy into a non-target; the
resolve-like shapes `resolve_shape` declines when they are multisampled (the 4a plan's list).

Not built, stated: a lock of a plain surface that mtld3d would read back from the GPU reads the staging the
CPU route wrote (same bytes, by the invariant); a depth-texture level as a `StretchRect` endpoint (the
classifier gives it no depth flag: Task 1's control records mtld3d's answer in a `sr-reject` cell, and the
core's is the same gate's); an additional swap chain's back buffer of another size than the device's (`copy`
checks the rect against the device's: an error line, counted by N-1).

## Appendix: the rows

Surfaces are 64x64 A8R8G8B8 unless said; "RT" a non-lockable standalone target; colours from the file's
constants, pairwise distinct in a row. Launch `blit` (row count: 18 + 12 = 30):

| row | cells |
|---|---|
| `sc-rt` | RT a (drawn: two colours) into RT b whole; a's {9,7,30,29} into b at (33,18): inside, outside, (0,0); ordered |
| `sc-bb` | a's {5,9,38,40} into the back buffer at (101,61); the back buffer's {101,61,134,92} into an X8R8G8B8 RT at (7,11); a whole into the back buffer's {0,0,64,64} (Wine's `test_pointsize` shape); ordered |
| `sc-lockable` | a lockable target written under a lock, its {3,5,28,30} into a at (20,10); a's rect into the lockable target, then read under a READONLY lock |
| `sc-rtex` | render-target textures x, y (2 levels): x's level 0 written by `UpdateSurface` and never drawn, its rect into y's level 0 (y not used before), y drawn on the back buffer; x level 1 {3,2,18,15} into y level 1 at (9,12) (source and destination level 1); RT a into face 3, level 0 of a render-target cube at an offset, face 3 read by `GetRenderTargetData`, face 0 unchanged |
| `sc-tex` | a usage-0 DEFAULT texture (2 levels) written by `UpdateSurface`, never bound: level 0's rect into a; level 1 whole into a at (5,6); a second `UpdateSurface` of a sub-rect, then the copy again (the owed upload) |
| `sc-plain` | a plain surface written under a lock, its rect into a at an offset; `ColorFill` of a rect of the plain surface, then the copy again (4a's stated gap: the plain surface's upload) |
| `sc-plain2` | plain p's {6,3,29,30} into plain q (written first) at (31,20), q read under a READONLY lock: inside, outside, (0,0); q whole into a (the CPU copy reaches the GPU); p into an X8R8G8B8 plain surface (unequal D3D formats, equal bytes); p onto p, identical rects: `D3D_OK`, unchanged; a plain surface nothing wrote into q: the answer alone |
| `sc-conv` | p into an R5G6B5 plain surface, whole and a rect at an offset, read under a lock as 565 values |
| `sc-self` | a's {0,0,13,16} onto a's (40,32) (disjoint): copied, source intact; identical rects: `D3D_OK`, unchanged; level 0 {8,8,40,40} of an RT texture onto its level 1 whole; cube face 1 onto face 4 of one cube (a source face other than 0); an additional chain's back buffer's {0,0,33,32} onto the device's at (64,64); overlapping rects of a: strict-refused |
| `sc-autogen` | a into level 0 of an AUTOGENMIPMAP render-target texture, then a minified draw of it on the back buffer reads the new colour (as `cf-autogen`); an AUTOGENMIPMAP texture that is no target, never written, into that level: the answer alone (Wine's `test_mipmap_autogen` shape) |
| `sc-depth` | D24S8 d1 cleared to Z 0.25 and stencil 3, d2 to Z 1.0 and stencil 0, d1 into d2 with no draw between; with d2 bound: a quad at Z 0.5 under LESSEQUAL leaves the target alone, a quad at Z 0.1 under a stencil test EQUAL 3 draws, EQUAL 0 does not; a D16 pair, depth alone |
| `sc-order` | in one scene: a draw on a, a's copy into b with the pass open, a draw on b: b holds the copy under the later draw; a Clear of a, then the copy at once: b holds the Clear's colour; a copy into the bound target, then a draw on it |

`sr-shape` keeps: the back buffer (X8R8G8B8) into an A8R8G8B8 target; the scaled LINEAR cell; and gains a
target into a 4-sample target, a 4-sample target's sub-rect into a target, a plain surface onto itself with
different rects. Each `NOTAVAILABLE` under strict, `D3D_OK` otherwise, no pixels. Its nine other cells are the
`sc-*` rows' now. `sr-reject` gains one cell: a depth texture's level into a target, wanting what mtld3d
answers in the control (predicted `INVALIDCALL`).

**The blit's fields and the cell that decides each:** source origin `sc-rt`; destination origin `sc-rt`;
source level `sc-rtex` (level 1 to level 1), `sc-tex` (level 1 to level 0); destination level `sc-rtex`,
`sc-self` (level 0 to level 1); source slice `sc-self` (face 1); destination slice `sc-rtex` (face 3),
`sc-self` (face 4); size `sc-rt` (the texel just outside).

Launch `msaa` (ruling DM, outcome A): `ms-desc`'s equal-count depth copy answers `D3D_OK` under strict; new
row `ms-dcopy`: the 4-sample auto depth-stencil cleared to Z 0.25, copied into a 4-sample D24S8 surface
cleared to 1.0, that surface bound with the back buffer: a quad at Z 0.5 under LESSEQUAL leaves the back
buffer alone.

## Review dispositions

(after the three reviews)

## Execution notes

(after execution)
