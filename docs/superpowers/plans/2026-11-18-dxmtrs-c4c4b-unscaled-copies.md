# DXMTRS C4c-4b: `StretchRect`'s unscaled copies on D3D9's core runtime

Slice 4 of C4c, second of three parts (ruling S-4 of the 4a plan). Process: the C4 program's (code map, this
plan, three adversarial reviews folded before code, rows first, one commit a feature, full gates once, one
whole-branch review, spec, pin, push). Maps: `build/c4c/s4b/map.md` (cited "map 1a", "map 2b" ...) and the
whole slice's `build/c4c/s4/map.md` ("old map A1" ...). The 4a plan
(`2026-11-18-dxmtrs-c4c4a-fill-update-validation.md`, "4a plan") holds the rules this one repeats by name
only. Sweep: `build/c4c/s4a/sweep.md`. Reviews: `build/c4c/s4b/review-{1-design,2-core,3-exec}.md` (cited
"R1 f3" = review 1, finding 3). Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, a `StretchRect` that copies texels 1:1 (equal-size rects, no scaling, no
multisampled endpoint) works with mtld3d's answer and pixels:

- **into a render target** (a standalone target lockable or not, a render-target texture's level or cube face,
  the back buffer, an additional swap chain's back buffer) **from** any of those, from a DEFAULT texture's
  level that is no target, or from a DEFAULT offscreen plain surface, when the two formats' bytes copy
  (`copies_bytes`); sub-rects and distinct origins included; an AUTOGENMIPMAP destination regenerates its
  chain; two levels or faces of one texture, two disjoint rects of one surface, and the additional chain's back
  buffer against the device's included;
- **a plain surface into another plain surface**, on the CPU: a raw copy when the two stagings hold the same
  bytes, mtld3d's CPU conversion otherwise (a YUV source included: R1 f2);
- **the depth-stencil copy**: two depth-stencil surfaces of one format, size and sample count, whole.

Still refused by name (`D3DERR_NOTAVAILABLE`, the same literal; C4c-4c): a scaled copy, a copy that converts
**into a target** (a YUV decode into a target included), any multisampled endpoint the resolve arm declines,
the depth resolve, and the shapes of "Stated gaps".

Wine, predicted from the raw log of 4a's sweep and the test sources (R3 f14; "Test failed" lines over the
default runtime's baseline, under core):

| function | before → after 4b | what is left |
|---|---|---|
| `test_pointsize` (visual) | 570 → 0 | all 570 follow its one copy of a lockable target into the back buffer |
| `test_stretch_rect` (device) | 81 → 40 | the converting copies into a target (4c) |
| `stretchrect_test` (visual) | 51 → 17 | four scaled sources, four lines each, and one table line |
| `test_scene` (device) | 5 → 1 | `EndScene` after `Reset` |
| `depth_blit_test` (visual) | 15 → 0 | |
| `test_mipmap_autogen` (visual) | 8 → 0 | |
| `test_blit_format_conversion` (visual) | 10 → 8 | its A8R8G8B8 into X8R8G8B8 case is a byte copy |

`test_flip` is not 4b's (0 since 4a; its copy is a resolve).

## Global Constraints

The 4a plan's, unchanged, with this slice's names: worktrees `build/c4b3c/dx` (branch `c4c`) and
`build/c4b3c/mn` (branch `c4c-mn`); **base DXMTRS main 3d5dcda3, MacNeutron main 4355afd9**; job scripts in
`build/c4c/s4b/gates`, copied from `build/c4c/s4a/gates` and edited before their first run (the guard:
`/usr/bin/grep -nE 'c4c/s4a|c4c4a|C4c-4a' build/c4c/s4b/gates/*.sh build/c4c/s4b/gates/*.txt` prints nothing,
and the positive checks of the 4a plan hold with this plan's row counts, letters and tokens; VM state names
`c4c4b-*`); `base-key.txt` and `base-imports.txt` are copied from s4a (4a's `jr` and `post` printed "same":
the key is still `e1074f4f…`; R3 m4); a rerun is a new job name; every verdict counts its rows; every
`d3d9-check.sh` run sets `DXMT_CHECK_WORK`; no `dxmt/check.sh` run crosses 03:35; the pipeline lock; no
implementer runs `dxmt/toolchain.sh`; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy
scan.

Restated because this slice leans on them:

- **No file outside `src/d3d9` changes** in DXMTRS. `src/dxmt/fakes` stays as it is, although it prints a
  texture-to-texture blit only as `blit 4` (map 2f; ruling G-1).
- **A core arm is keyed on the device being core**, never on `core_target()` or `core_depth()` alone.
- **No mtld3d recorder call reachable on a core device; no read of a `StretchKind`'s handle below an arm.**
- **The invariant: on a core device only render-target levels, standalone targets and the back buffer are
  ever written by the GPU.** This slice keeps it in two places at once: the front end tests **the destination
  itself** (not the pair's class: a core device may be extended, and an extended device's whole-surface copy
  skips `class_reject`, map 1a), and d3d9-rt's copy resolves its destination through the target-only `shape`.
- **Every verdict counts the core's error lines** (ruling N-1): `notrec <folder>` prints
  `/usr/bin/grep -rhF 'was not recorded' <folder> | wc -l` (one sum, not a count a file; R3 f10) and 0 is
  wanted in every lane: a primitive's `false` on a shape mtld3d accepts would otherwise hide as a plain
  `INVALIDCALL`. Where it runs: `ja`, each `jt`, `jq` on each work folder's `mtld3d-logs`; `jc` on
  `build/vm-work/share/traces/d3d9/logs` right after each core gate and before the next one wipes it; `jm`
  for each mutant before its work folder is removed (mutants e expects more than 0).
- **The x64 check (`dxmt/check.sh`) runs once at the end of C4c-4**, not in this slice's chain or post-push
  check (the maintainer, 2026-11-18: "Yes, once at end"), unless the DXMTRS range leaves `src/d3d9`: `jr.sh` and
  `post.sh` test `git diff --name-only 3d5dcda3..HEAD` and say which. 4c's chain and post-push check run it.
- **Every stretch refusal goes through one function** (`core_arms::refuse_stretch()`, the one place that holds
  the literal): the refusal logs once a string pointer, not once a text (`runtime.rs:81-87`), and the gates
  want exactly one line a launch (R3 m2).

## Scope

In: Tasks 1-8. Out, with where it goes: see "Stated gaps" (each still answers `NOTAVAILABLE` by name).

## The mechanism

### d3d9-rt

**A source lookup** (`textures.rs`): one private body, `stored_shape(id, level, face, target_only: bool) ->
Option<Shape>` (today's `texture_shape` with its `RENDER_TARGET` test under `target_only`; **one map lookup**,
R2 f7: `texture_shape` is behind every draw's `shape`). `texture_shape(..)` = `stored_shape(.., true)`: its
answers and its tests are unchanged. `level_shape(..)` = `stored_shape(.., false)`: any stored colour
texture's level (a depth format and a volume are none); its only caller is `copy`. Its doc says a
block-compressed level's `texel_bytes` is its block's, and that such a level never reaches `copy` (the
destination is a target, the formats are equal; R2 f8).

**`copy`** (new, `surface.rs`, beside `resolve_into`):
`pub fn copy(&mut self, src: TargetRef, src_rect: [u32; 4], dst: TargetRef, dst_origin: [u32; 2]) -> bool`
(`#[must_use]`). `src` names **any stored colour level** (`TargetRef::Texture` through `level_shape` and
`stored_texture`, never through `shape` or `resolve`, which refuse a non-target) or the back buffer
(`resolve(BackBuffer)`); `dst` names a target (`shape(dst)` and `resolve(dst)`, unchanged). `TargetRef`'s doc
gains the sentence that a copy's source may be a level that is no target. `[x, y, w, h]` in the texels of the
source's level. False, and nothing recorded, when:

- either end does not resolve; either end has more than one sample (the back buffer's `samples` is its
  drawn texture's: a multisampled back buffer is refused at both ends, so `drawn()` is `presented()` here);
- the two stored pixel formats differ (`Shape::pixel_format`; the sRGB twin is not looked at: a blit copies
  bytes);
- the rect is empty or leaves the source's level; `dst_origin` plus the size leaves the destination's
  (`checked_add`, as `rect_bytes` checks);
- both ends are **one texture** (`Arc::ptr_eq`) and mtld3d's own rule
  `mtld3d_core::stretch_rect::same_surface_route(src_region, dst_region, src_level, dst_level, src_slice,
  dst_slice)` is not `Direct` (identical or overlapping rects of one subresource; R2 f5: the front end and
  the primitive ask one predicate). Ruling SS decides whether a `Direct` pair of one subresource, or of one
  texture, is recorded at all.

Recorded: `open_blit_pass()` (it closes an open render pass and shares the blit pass with uploads), then one
`emit`: `access_texture::<{ stage::COMPUTE }>(&src, level, slice, Access::READ)`, the same for `dst` with
`Access::WRITE`, `encode_blit_command(record)`. **The record is built by a pure function** (R2 f6),
`blit_record(src: (u64, &Shape), rect: [u32; 4], dst: (u64, &Shape), origin: [u32; 2]) ->
WmtcmdBlitCopyFromTextureToTexture` (both slices, both levels, `src_origin`, `src_size` with depth 1,
`dst_origin`; map 2a), as `plane_copies` and `scissor_rects` are, so its seven fields are unit-tested. No
`note_written` (a blit writes no view format, and `last_written` is read for a multisampled source only; map
2b, R2) and no `staged_bytes` (nothing is staged). **The two `access_texture` calls are what orders the copy**
against the passes around it (texture allocations are untracked; R2 f1): Task 2's fence tests, the `sc-order`
row and mutants l and m hold them.

**`copy_depth`** (new, `depth.rs`): `pub fn copy_depth(&mut self, src: DepthRef, dst: DepthRef) -> bool`
(`#[must_use]`). Both through `depth_level`; false, and nothing recorded, unless both resolve, the two refs
differ, and width, height, pixel format and samples are equal; **more than one sample is accepted or refused
by ruling DM**. Recorded as `copy` records, the whole level, slice 0 (one texture-to-texture copy moves both
planes: the wire has no plane option; `sc-depth`'s stencil cells judge it).

Docs made wrong by a texture-to-texture blit are reworded in the same commits (R2 f8): `textures.rs:470-472`
and `:503-504`, `surface.rs:32-34` say which blits they mean (buffer to texture, texture to buffer).

### Front end

`device_stretch_rect` keeps its gates and its two exits. The exits call two new arms in `core_arms.rs`;
`stretch_rect_resolve` is deleted (its two callers are the exits; `resolve_shape` stays, private).

**Exit 1** (the depth branch): `core_arms::stretch_depth(this, [src, dst], [&src_info, &dst_info])`.
- Unequal sample counts (the resolve) → `refuse_stretch()`.
- `core_depth()` of both; **equal refs → `D3D_OK`, nothing recorded** (R1 f1, R2 f2: the gates have no
  same-surface test, and mtld3d's encoder answers its `Skip`).
- `core.copy_depth`; a missing ref or `false` → the error line ("d3d9 core: StretchRect of depth {src:?} into
  {dst:?} was not recorded → INVALIDCALL") and `D3DERR_INVALIDCALL`.

**Exit 2** (colour): `core_arms::stretch_colour(this, &obj, &StretchCall { surfaces, infos, regions, rects,
scaling, planar_endpoint, expand_packed16 })` (one struct, private to `device`; no handle in it is read;
`obj: &Direct3DDevice9` is passed beside it for `convert_stretch_dst_staging`, R1 f6). In order:

1. `resolve_shape(this, surfaces, rects).is_some()` → `D3D_OK` (4a's, unchanged, first).
2. **Refused by name** (4c), before any state changes: `scaling`; either info's `sample_count > 1`;
   `cross_format` with a destination that is not `IS_OFFSCREEN_PLAIN_DEFAULT`, where `cross_format =
   planar_endpoint || !copies_bytes(src.format, dst.format, expand_packed16)` (mtld3d's own expression,
   `device.rs:9745`; R1 f2: a planar or packed YUV source into a plain surface goes on to step 4, as on
   mtld3d).
3. **The destination is a target** (`dst_info.flags` has `IS_RENDER_TARGET`): the GPU route.
   - The refs: `dst.core_target()`; the source's is `src.core_target()`, or for a level that is no target
     `TargetRef::Texture { id: parent.texture_id(), level: src.mip_level(), face: src.cube_face() }` (map
     1c). A missing ref → the error line and `INVALIDCALL`.
   - **One texture at both ends** (both refs `BackBuffer`, or equal ids): `same_surface_route(src_region,
     dst_region, src_info.mip_level, dst_info.mip_level, src_info.slice.unwrap_or(0),
     dst_info.slice.unwrap_or(0))` (six arguments, as `ops.rs:797-799` calls it): `Skip` → `D3D_OK`, nothing
     recorded; `Scratch` (here: overlapping rects of one subresource) → `refuse_stretch()` (4c); `Direct` →
     goes on (ruling SS).
   - `core_flush_dirty` of the source's parent texture and of the destination's (once when they are one):
     the source's core texture exists from here with every CPU write uploaded (a level that is no target, a
     plain surface: created and uploaded whole at first use, map 1b), and the destination's owed uploads are
     recorded ahead of the copy in the same blit pass (in-encoder order, as an upload then `generate_mips`
     relies on today; R2).
   - `core.copy(source, [x, y, w, h] of src_region, destination, [x, y] of dst_region)`; `false` → the error
     line ("d3d9 core: StretchRect of {source:?} {src_region:?} into {destination:?} was not recorded →
     INVALIDCALL") and `INVALIDCALL`.
   - On success, as `color_fill`: `claim_dst_subresource_for_gpu(dst, dst_info)` when the destination has a
     parent texture; `core.texture_generate_mips(id)` when `dst_info.autogen_texture_id` is set.
4. **The destination is no target**: the CPU routes, in this order (R1 f5: the same-surface test comes before
   any borrow of the two textures):
   1. the two parents are one texture (or `ptr::eq(src, dst)`): `same_surface_route` `Skip` → `D3D_OK`; any
      other → `refuse_stretch()` (both CPU helpers need two textures; ruling SP);
   2. both ends `IS_OFFSCREEN_PLAIN_DEFAULT` with two distinct non-null parents:
      - **the stagings hold the same bytes** (ruling K-1): the D3D formats are equal, or `!cross_format` and
        neither format `is_expanded_upload`s on this device (then each staging holds its Metal texture's
        bytes, and a raw copy is mtld3d's blit byte for byte: R1 f3) →
        `dst_tex.inner_mut().copy_sub_region_from(dst level, src_tex.inner(), src level, Some((l, t, r, b)),
        (x, y))` (**`(left, top, right, bottom)` in `i32`, the form at `device.rs:9957-9962`**, not `[x, y, w,
        h]`); it marks the written region dirty, so the plain surface's next use as a source uploads it and
        its lock reads it. No `materialize_subresource_for_cpu_read` first: on a core device a plain surface
        is never GPU-authoritative (the only claim is step 3's, on targets; R1 f12);
      - else `pixel_convert::can_convert(src, dst)` → `super::convert_stretch_dst_staging(..)` as it is (pure
        CPU on core; its `flush_converted_mips` returns at once there, map 1e);
      - else `refuse_stretch()`;
      - a `false` or an answer other than `D3D_OK` from either helper → the step's error line ("... was not
        recorded → INVALIDCALL"), so N-1 sees the conversion route's failures too (R1 f9);
   3. **anything else** (an extended device's whole-surface copy into a level that is no target, or from a
      source that is no plain surface) → `refuse_stretch()` (ruling X-2).

The module doc and the arms' docs say what is copied, what is refused and why the destination is tested.
mtld3d's code behind the exits is untouched.

**What is GPU-authoritative afterwards:** a target destination, as after `ColorFill` (a lockable target and a
back buffer read the GPU back at their next lock; a render-target level is claimed, and its owed CPU writes
were uploaded first: R1 "checked"). A plain destination stays CPU-authoritative (dirty, uploaded at its next
use as a source); where mtld3d claims it for the GPU and reads it back at the next lock, the core never needs
to (the invariant).

## Review Focus (for the final review)

- The invariant: every path that records a GPU write names a target, in the front end and in `copy`.
- No recorder call and no null-handle read reachable on a core device from either exit; every return path.
- `copy`'s refusals against every endpoint kind of old map D; the same-texture rule; `blit_record`'s fields.
- The two tracker accesses: the subresource each names.
- The CPU routes: the raw-copy key, the rect's form, the dirty marks, what a later lock and copy read.
- One call site for the refusal literal; mtld3d unchanged (every hunk behind `cfg` and the device test).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in the Appendix: thirteen new `sc-*` rows in the `blit` launch (appended after `sr-shape`, `sc-depth`
last), `sr-shape` cut down to the cells that stay refused, `sr-reject` one cell longer, and in the `msaa`
launch `ms-desc`'s strict expectation and the new `ms-dcopy` right after it (ruling DM). Wiring: `blitRows`,
`msaaRows`, the header prose (CPP:7558-7559 no longer says the `sr-*` rows call neither `ColorFill` nor
`UpdateSurface`: `sc-*` rows do); `dxmt/d3d9-check.sh`: the `check blit` and `check msaa` lists, and **the
`msaa` launch's refusal set** at `:170` (outer label), `:173` (inner label) and `:180` (the "none" list), prose
`:59-62`, `:154-162`; `vm/dxmt/d3d9-gate.sh`: both row loops, `:278` (drop `ms64-fresh ms32-fresh` from the
`stretch` loop) and `:287` (add them to the "none" loop), prose `:48-51`, `:243-244` (**not `:258`**, the
`stretch` set `targets` and `blit` still use; R3 m1); `vm/tests/d3d9.sh`'s prose (`:29-35` row counts).
`sh -n` on the scripts. `targets` and `blit` keep their one line (`rt-msaa`'s scaled cell; `sr-shape`).

**Rules for every new cell** (the 4a plan's, plus):
- every copy cell that judges pixels has **a source rect off the origin, a destination origin different from
  the source's, and a width that is no multiple of 4**, and judges a texel inside the copied rect, one just
  outside it and one at the destination's (0,0); exceptions, named in the Appendix: whole copies, Wine's
  shapes, and a destination nothing wrote before the copy (inside texels only; R3 m7);
- a row's surfaces, levels and faces are each written a different colour before a copy that must tell them
  apart (R3 m8: a source level 1 differs from level 0 at the probed texels; cube faces 0, 1, 2, 3, 4 differ);
- **no cell judges pixels copied from a source nothing has written**: such a cell judges the answer alone;
- **every depth cell draws a quad that must draw before the quad that must not**, on the same attachments,
  same FVF, in the same `until()` attempt (R3 f6: mtld3d drops a draw while its pipeline builds, and a cell
  that only wants "nothing drawn" would pass untested);
- **`sc-depth` and `ms-dcopy` restore state on every path**, passed or not: `SetDepthStencilSurface(nullptr)`
  (the launch's own for `msaa`), ZENABLE as the launch had it, STENCILENABLE FALSE, ZFUNC LESSEQUAL,
  ZWRITEENABLE TRUE (R3 f9: `reset()` undoes no depth state), and `sc-depth` binds a 64x64 target for its
  Clears and draws;
- a strict-refused cell is spelled as `sr-shape`'s are (`NOTAVAILABLE` under strict, `D3D_OK` otherwise);
- `sc-conv` reads its R5G6B5 lock with a 2-byte reader as `plainFills` does (R3 m6).

Job scripts in `build/c4c/s4b/gates` from s4a's: `head.sh` (`rows()` with `BLIT_ROWS=31`; a twin for the
`msaa` launch with `MSAA_ROWS=20` under outcome A, 19 under B (R3 f11); `notrec()`), `ctl.sh`, `ja.sh`,
`jv0.sh` (**the `blit` and `msaa` launches** on mtld3d under Metal's validation layer: rows ok 33 and 22, the
two extra being `module` and `imports`), `jcr.sh` and `jc.sh` (`bt64`/`bt32` ok lines 31 each, 32 under core
with the "runtime core" line; `ms64`/`ms32` 20 and 21), `jt.sh`, `jq.sh`, `jr.sh`, `jw.sh`, `jmb.sh`, `jm.sh`,
`post.sh`, `chain.sh`.

**Controls, in this order** (R3 f8), each failing set and row count written in the ledger before its run:
1. **`jv0` first**, on the row commit: reports 0 and the row counts. It decides rulings SS and DM, and the
   evidence is **the judged pixels of `sc-same`, `sc-self` and `ms-dcopy` on mtld3d together with a silent
   validation layer**, not the silence alone. A report under `sc-same` switches SS to outcome B1; under
   `sc-self`, to B2; under `ms-desc` or `ms-dcopy`, or `ms-dcopy` red, switches DM to outcome B. A report under
   any other row is an mtld3d finding, ledgered for the maintainer, and decides nothing (this is the first
   `msaa` launch on mtld3d under the validation layer; R3 f7). Expected and no finding: mtld3d's "render.scale"
   warning line for `ms-desc`'s copy (`ops.rs:637-643`).
2. If an outcome B holds: a second row commit makes the named cells strict-refused (SS) or drops `ms-dcopy`,
   puts `ms-desc` back to `NOTAVAILABLE` under strict and the `msaa` set back to its one line (DM), and `jv0b`
   confirms it. The plan's predictions below then read their B column.
3. `ja` on the final row commit: default and msl: every row green on mtld3d at the first run, no expectation
   corrected (a red row is a wrong row or a finding, settled before any code). Core, the failing tokens
   predicted (R3 table a): the thirteen `blit-sc-*`, and under outcome A `msaa-ms-desc`, `msaa-ms-dcopy`,
   `msaa-runtime`; nothing else (`sr-shape` and `sr-reject` green: their cells are refused or rejected at the
   base too). `notrec` 0.
4. `jcr`: the VM's default lane on old and new WoW64, the ok lines counted.

Commit: "dxmt, vm: d3d9_triangle rows for StretchRect's unscaled copies (DXMTRS C4c-4b)".

### Task 2: `copy` and `copy_depth` (d3d9-rt)

As "d3d9-rt" above; two commits. Tests on the fakes as they are (each "records no line" check drains and
takes the calls first: a target's creation queues its zeroing; R2 f9):

- `blit_record`: sentinel handles and distinct values in every field: both slices, both levels, both
  origins, the size, which end is the source.
- refusals, each false with no line: an unknown id; a depth id as a colour source; a non-target as
  destination; a multisampled end; unequal formats; an empty rect; a rect past the source's level 1 that fits
  level 0; an origin past the destination; overlapping and identical rects of one subresource; the back
  buffer at both ends with overlapping rects.
- a copy records exactly one `blit 4` inside a blit pass, after an open render pass's end (`!render_open`); a
  copy from a texture that is no target records one; a copy shares the blit pass of an upload recorded just
  before it and follows it; one texture, level 0 to level 1: one `blit 4`.
- **the fences** (R2 f1; the form of `rt/surface/tests.rs:129-161` and `:231-281`): `set_target` on level 1
  of a render-target texture, `clear`, then `copy` from that level: the copy's blit pass waits (`blit 6
  fence=`) on a fence the Clear's pass updates; `copy` into level 1 (and into face 3 of a cube), then a real
  draw on it (`draw(&key(..), &triangle(..))`: an empty pass is dropped): the draw's pass waits on a fence
  the copy's blit pass updates (`blit 7 fence=`).
- `texture_shape`'s existing tests pass untouched.
- `copy_depth`: its refusals (one ref at both ends included); its one `blit 4`; the fence pair (a depth
  Clear of the source before, a draw with the destination bound after).

Commits: "d3d9: a texture-to-texture copy on the core runtime"; "d3d9: a depth-stencil copy on the core
runtime".

### Task 3: copies into a target (front end)

`refuse_stretch()`, the `StretchCall` struct, `core_arms::stretch_colour` steps 1-3, exit 2's call; step 4
answers `refuse_stretch()` for now; exit 1 still calls `stretch_rect_resolve`, whose refusal goes through
`refuse_stretch()`. Lane `jt3` (default, core): green strict under core: `sc-rt sc-bb sc-lockable sc-rtex
sc-tex sc-plain sc-same sc-self sc-autogen sc-order`; failing tokens predicted: `blit-sc-plain2`,
`blit-sc-conv`, `blit-sc-depth`, and under outcome A `msaa-ms-desc`, `msaa-ms-dcopy`, `msaa-runtime`;
default: all green; `notrec` 0. **The `blit` launch once under core with Metal's validation layer** (reports
0, rows ok 33 less the three red). Commit: "d3d9: StretchRect copies into a target on the core runtime".

### Task 4: plain surface to plain surface (front end)

Step 4. Lane `jt4` (default, core): `sc-plain2` and `sc-conv` green strict; failing tokens predicted:
`blit-sc-depth`, and the three `msaa` tokens under outcome A. Commit: "d3d9: StretchRect between plain
surfaces on the core runtime".

### Task 5: the depth-stencil copy (front end)

`core_arms::stretch_depth`, exit 1's call, `stretch_rect_resolve` deleted. Lane `jt5` (default, core) at the
tip: every row of every launch green strict under core, every launch's refusal set exact (`targets` 1,
`blit` 1, `msaa` 0 under outcome A and 1 under B), `notrec` 0 everywhere, **and the `blit` and `msaa`
launches once under core with Metal's validation layer** (reports 0, rows ok 33 and 22). Commit: "d3d9:
StretchRect copies a depth-stencil surface on the core runtime".

### Task 6: gates

As the 4a plan's Task 6 (`jmb`, then `jr`, `jc`, `jq`, `jm`, `jw`; the pre-ruled flakes; a hang of
`check.sh`'s `xess-wd` row is run once more under a new job name, as 4a's `jx`). **Mutants** are commits off
the tip, each red on exactly its tokens; `gates/mutants-expect.txt` names the cell and the texel or answer
from the rows as committed, before `jmb`, and `jm.sh`'s `P` holds the token lists (the lists below are the
plan's from the Appendix; the file's are final):

  a. the target route hands `copy` (0, 0) as the destination origin → `blit-sc-rt blit-sc-bb
     blit-sc-lockable blit-sc-rtex blit-sc-tex blit-sc-plain blit-sc-same blit-sc-self blit-sc-order` (not
     the CPU rows; in `sc-same` the copy becomes an identical or overlapping pair: `false`, an error line);
  b. the target route hands `copy` the source rect moved to the origin → the same list less a row whose
     every source rect is at the origin (none, by the cell rules);
  c. the ref built for a source that is no target names level 0 → `blit-sc-tex` (its level-1 cell);
  e. the target route skips the source's `core_flush_dirty` → `blit-sc-tex blit-sc-plain blit-sc-plain2`
     (each first copy from a source with no core texture answers `INVALIDCALL` with an error line: `notrec`
     above 0 is expected here; R3 f3; `sc-autogen`'s source is drawn before its copy, so it has one);
  e2. it skips that flush only when the source's core texture exists → `blit-sc-tex blit-sc-plain` (their
     owed-upload cells read the old texels);
  f. no `texture_generate_mips` after a copy → `blit-sc-autogen`;
  g. the raw plain copy (`copy_sub_region_from`'s call alone, not the conversion's; R3 m11) writes at the
     destination's (0, 0) → `blit-sc-plain2`;
  h. `copy_depth` copies the destination into the source → `blit-sc-depth`, and `msaa-ms-dcopy` under
     outcome A (R3 f4);
  l. `copy` tracks no source read → `blit-sc-order` (its Clear-copy-draw cell);
  m. `copy` tracks its destination write at level 0, slice 0 → `blit-sc-order` (its draw-copy-draw cells on
     level 1 and on a face).

  Not gated by a row mutant, stated: `blit_record`'s seven fields (Task 2's unit test; the rows judge their
  pixels; R2 f6, R3 f5); the destination test (a GPU write of a non-target needs an extended device, which
  the rows do not make; the unit test of `copy`'s destination refusal holds the recorder's side); the claim
  after a copy (as 4a's ruling T1-1); `stretch_depth`'s equal-refs answer and the single refusal function
  (their cells and the refusal counts fail without them).
- `jw` (gates nothing): the sweep → `build/c4c/s4b/sweep.md`, per function before and after against the
  table in "Goal", each number with its unit (4a's sweep is "before").
- **Probe: none** (ruling P-2).

### Task 7: review

One whole-branch Opus review (`build/c4c/s4b/review-final.md`), given the diff and this plan; Critical and
Important fixed with a red cell first where a pixel can show it; Minors fixed if one line, else ledgered.

### Task 8: spec, pin, push

Spec §5 paragraph "C4c-4b" and the slice list. Plan execution notes. `dxmt/pins`. Privacy scan of both
ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron (the SHA written out). Post-push
check. CI. Memory. Scratch deleted by full path once each verdict is ledgered.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none (the record
  `WmtcmdBlitCopyFromTextureToTexture` and its executor exist). **A dependency:** none (d3d9-rt already uses
  `mtld3d_core`). **A lint suppression:** none. **A derive:** none.
- **New state:** none on the device, a surface or a texture.
- **New pub items:** `CoreDevice::copy`, `CoreDevice::copy_depth`; `pub(super)` `level_shape`, private
  `stored_shape` and `blit_record`; `core_arms::stretch_colour`, `stretch_depth`, `refuse_stretch`; the struct
  `StretchCall` (private to `device`). **Deleted:** `core_arms::stretch_rect_resolve`. **Unsafe:** the derefs
  of the two parent textures in `stretch_colour` (for `core_flush_dirty`, for the two CPU helpers), each with
  a SAFETY comment as `resolve_shape`'s, one operation a block, taken only after the same-texture test.
- **A duplicate:** none meant. `texture_shape` and `level_shape` are one body; the overlap rule is mtld3d's
  `same_surface_route` in both places; the CPU routes are mtld3d's helpers; the rect checks of `copy` use the
  `inside` test `rect_bytes` and `fill` have (if a third copy of that three-line closure would result, it
  becomes one private function in `surface.rs` in the same commit, used by the three).
- **Not silent:** a copy that answers false logs an error in its arm and answers `INVALIDCALL`, and the gates
  count those lines; the refusal keeps its name and its one call site.
- **Divergence added:** a plain destination is copied on the CPU where mtld3d blits and claims it (same
  bytes; the invariant's reason). **Answers that differ from mtld3d:** the shapes of "Stated gaps" are
  `NOTAVAILABLE`.
- **Docs in the same change:** the three d3d9-rt comments above; `core_arms.rs`'s module doc; DXMTRS's
  `STATUS.md`, `COVERAGE.md`, `CONFORMANCE.md` say nothing of the core runtime: Task 8 confirms no edit is
  owed against the final diff.

## Rulings (planning)

- **S-5. What 4b copies.** Unscaled copies with no multisampled endpoint; everything else that is valid
  stays refused by name for 4c. Cost if wrong: a shape a game needs waits one slice; the core runtime is
  opt-in.
- **G-1. Geometry is unit-tested through the pure record builder and pixel-gated by the rows; the fakes stay
  as they are.** Decoding opcode 4 in `src/dxmt/fakes` would be a change outside `src/d3d9`. Row mutants
  cover the front end's plumbing (a, b, c) and the tracker accesses (l, m), not each record field. Cost if
  wrong: a field right in `blit_record` and wrong in what reaches it; the rows' pixels are that gate.
- **K-1. The raw plain copy is keyed on the stagings holding the same bytes**: equal D3D formats, or bytes
  that copy with neither format expanded on upload; else the codec; else refused by name. Cost if wrong: a
  pair outside all three (none known) answers `NOTAVAILABLE` where mtld3d copies. A8R8G8B8 into X8R8G8B8 keeps
  the source's alpha byte in the padding, as mtld3d's blit does (`sc-plain2`'s cell judges with `want`'s
  colour mask all the same).
- **SS. One texture at both ends is one blit when mtld3d's rule says `Direct`.** Outcome A: recorded.
  Outcome B1 (a validation report under `sc-same` on mtld3d): `copy` refuses every pair of one subresource,
  the front end refuses them by name (4c's scratch), `sc-same`'s two copies become strict-refused cells.
  Outcome B2 (a report under `sc-self`): `copy` refuses every `Arc::ptr_eq` pair and `sc-self`'s level and
  face copies become strict-refused too. In both, mtld3d's blit is a follow-up for the maintainer.
- **DM. A multisampled depth pair of equal counts is one blit**, as mtld3d records it: its encoder forces a
  quad for a multisampled destination, a depth surface is no render target, and the pair lands in the
  "render.scale" branch, one `copy_texture_to_texture_sub_rect` with a misleading warning (`ops.rs:604-607`,
  `:625-668`; R1 f13, R2 f4; the in-tree comments about multisampled blits disagree with each other).
  Outcome A: `copy_depth` accepts it, `ms-desc`'s strict answer is `D3D_OK`, `ms-dcopy` judges its pixels,
  the `msaa` launch's refusal set is empty. Outcome B: `copy_depth` and `stretch_depth` refuse more than one
  sample by name, `msaa` keeps its one line, `ms-dcopy` is dropped, mtld3d's blit is a follow-up.
- **SP. A plain surface copied onto itself** with rects that are not identical is refused by name. Cost if
  wrong: as S-5; a CPU `memmove` inside one staging is the later fix.
- **X-2. An extended device's whole-surface copy into a non-target** is refused by name, as today. Cost if
  wrong: a D3D9Ex title that copies texture to texture gets `NOTAVAILABLE` under core (unmeasured; `d3d9ex`'s
  suite dies in `test_reset` before its tests of it). The half with two CPU-authoritative ends could take
  step 4's raw copy; it has no row (the launches make no extended device), so it waits for one.
- **N-1. `was not recorded` is counted in every verdict** and wanted 0 (today's count is 0: the only two
  literals are 4a's, and its core logs hold none).
- **O-1. Task 2 runs beside Task 1**, not after the controls (the maintainer asked for speed): the two
  d3d9-rt primitives depend on no row. Task 3 starts once `jv0` has settled SS and DM, beside `ja` and `jcr`.
  Cost if wrong: an outcome B means a small edit of `copy`'s same-texture rule or of `copy_depth`'s sample
  test; a row corrected by `ja` changes no front-end code.
- **P-2. No probe.** No draw file and no line of `recorder.rs` changes; `texture_shape` keeps one map lookup
  and its answers. If the range touches `recorder.rs`, `draw.rs` or `target.rs`, or `texture_shape` gains a
  second lookup, one bench pair, once.

## Stated gaps

Refused by name after 4b (the sweep counts them as 4c's): scaled copies; converting copies into a target, a
YUV decode into one included; a multisampled source's sub-rect or a multisampled destination; the depth
resolve; overlapping rects of one subresource; a plain surface onto itself; an extended device's copy into a
non-target; the resolve-like shapes `resolve_shape` declines when they are multisampled (the 4a plan's list).

Not built, stated:
- a lock of a plain surface that mtld3d would read back from the GPU reads the staging the CPU route wrote;
- a block-compressed plain pair copied with a rect off the block grid: the raw copy rounds out to whole
  blocks and writes outside the destination rect where mtld3d hands Metal an unaligned origin (what Metal
  does is not established; wined3d refuses it). Not judged; the grid check belongs to the gates, on both
  runtimes, in a later slice (R1 f8);
- an additional swap chain of another size than the device's: its surface reports the device's extent, so a
  rect past the device's back buffer is `INVALIDCALL` at the gates on both runtimes (R1 f7);
- a depth-texture level as an endpoint is `INVALIDCALL` at the format gate on both runtimes (a `sr-reject`
  cell records it).

## Appendix: the rows

Surfaces are 64x64 A8R8G8B8 unless said; "RT" a non-lockable standalone target; colours from the file's
constants, pairwise distinct in a row; "judged" = inside the copied rect, just outside it, and the
destination's (0,0). Launch `blit` (18 + 13 = 31 rows; the new ones after `sr-shape`, in this order):

| row | cells |
|---|---|
| `sc-rt` | RT a drawn in two colours (the second inside {9,7,30,29}), RT b cleared a third: a into b whole; b cleared again, a's {9,7,30,29} into b at (33,18), judged |
| `sc-bb` | a's {5,9,38,40} into the back buffer at (101,61), judged; the back buffer's {101,61,134,92} into an X8R8G8B8 RT at (7,11), judged; a's {3,6,36,40} into an additional chain's back buffer at (150,90), read from the device's back buffer (R3 f12); a's {0,0,64,64} into the back buffer's {0,0,64,64} (Wine's `test_pointsize` shape, an exception to the origin rule) |
| `sc-lockable` | a lockable target written under a lock, its {3,5,28,30} into a at (20,10), judged; a's {6,2,31,27} into the lockable target at (22,30), read under a READONLY lock, judged |
| `sc-rtex` | render-target textures x, y (2 levels): x's level 0 written by `UpdateSurface` and never drawn, its {5,3,26,24} into y's level 0 at (30,34), y not used before (inside texels only), y then drawn on the back buffer; x's and y's level 1 each cleared a colour, x level 1 {3,2,18,15} into y level 1 at (9,12), judged; a render-target cube with faces 0 and 3 cleared two colours: a's {4,6,27,29} into face 3 at (31,22), face 3 judged by `GetRenderTargetData`, face 0 unchanged; face 2 of a second cube (faces 0 and 2 cleared two colours) {2,3,25,20} into a at (33,40) (a source face other than 0) |
| `sc-tex` | a usage-0 DEFAULT texture (2 levels), level 0 and level 1 written different colours by `UpdateSurface`, never bound: level 0's {7,5,30,28} into a at (26,31), judged; level 1 {2,3,17,16} into a at (5,6), judged; a second `UpdateSurface` of level 0's {8,8,19,20}, then level 0's {7,5,30,28} into a again: the new texels (the owed upload) |
| `sc-plain` | a plain surface written under a lock, its {6,4,29,31} into a at (27,25), judged; `ColorFill` of the plain surface's {8,8,20,20}, then the same copy again: the fill's texels (4a's stated gap: the plain surface's upload) |
| `sc-plain2` | plain p's {6,3,29,30} into plain q (written first) at (31,20), q read under a READONLY lock, judged; q's {30,19,55,45} into a at (4,6) (the CPU copy reaches the GPU); p into an X8R8G8B8 plain surface, a rect at an offset (unequal D3D formats, same bytes; the colour mask); p onto p, identical rects: `D3D_OK`, unchanged; a plain surface nothing wrote into q: the answer alone |
| `sc-conv` | p's {5,4,26,27} into an R5G6B5 plain surface at (30,31) and p whole into a second one, read under a lock as 565 values; if `CheckDeviceFormat` offers a YUY2 plain surface: one written under a lock, whole into an X8R8G8B8 plain surface, one texel (else `d3d9 sc-conv-yuy2 skip`) |
| `sc-same` | (one subresource; ruling SS) a's {3,2,16,18} onto a's (40,32): copied, source intact; an additional chain's back buffer's {4,6,37,38} onto the device's at (150,120), read from the device's back buffer |
| `sc-self` | (one texture, two subresources) level 0 {8,8,40,40} of a 2-level render-target texture onto its level 1 whole (an exception: Wine-like whole destination); cube face 1 {3,4,26,25} onto face 4 at (30,33) of one cube, faces 1 and 4 cleared two colours first; a's identical rects: `D3D_OK`, unchanged; overlapping rects of a ({0,0,32,32} onto (16,16)): strict-refused |
| `sc-autogen` | a into level 0 of an AUTOGENMIPMAP render-target texture, then a minified draw of it on the back buffer reads the new colour (as `cf-autogen`); an X8R8G8B8 AUTOGENMIPMAP texture that is no target, written by `UpdateSurface` from a SYSTEMMEM surface and drawn once, whole into level 0 of an X8R8G8B8 AUTOGENMIPMAP render-target texture, then a minified draw of that reads the source's colour (Wine's `test_mipmap_autogen` shape; R3 f13) |
| `sc-order` | all in one scene, no `EndScene` between: (S1) a cleared KA, a's rect into b, then a draw on a: b holds KA; (S2) y level 1 (a render-target texture's) drawn, a's rect into y level 1 covering part of the draw, a draw on y level 1 beside it: the copy shows over the first draw and under nothing; the same on face 3 of a cube; a copy into the bound target then a draw on it |
| `sc-depth` | (last; a 64x64 target bound) D24S8 d1 cleared to Z 0.25 and stencil 3; d2 bound and cleared to Z 1.0, stencil 0, then d1 into the bound d2 with no draw between (as `depth_blit_test`); with d2 bound: a quad at Z 0.1 under a stencil test EQUAL 3 draws, then a quad at Z 0.5 under LESSEQUAL leaves the target alone, then EQUAL 0 does not draw; then d1 bound: a quad at Z 0.1 draws and one at Z 0.5 does not (the source's read was ordered); d1 onto d1 whole: `D3D_OK` in every lane; a D16 pair the same way, depth alone, the drawing quad first; state restored on every path |

`sr-shape` keeps: the back buffer (X8R8G8B8) into an A8R8G8B8 target; the scaled LINEAR cell; and gains a
target into a 4-sample target, a 4-sample target's sub-rect into a target, a plain surface onto itself with
different rects. Each `NOTAVAILABLE` under strict, `D3D_OK` otherwise, no pixels. Its nine other cells are the
`sc-*` rows' now. `sr-reject` gains one cell: a depth texture's level into a target, `INVALIDCALL` in every
lane (the format gate).

Launch `msaa` (ruling DM, outcome A; 19 + 1 = 20 rows): `ms-desc`'s equal-count depth copy answers `D3D_OK`
under strict; new row `ms-dcopy` right after it: the 4-sample auto depth-stencil cleared to Z 0.25 and stencil
3, a 4-sample D24S8 surface bound and cleared to Z 1.0 and stencil 0, the copy with no draw between; that
surface bound with the back buffer: a quad at Z 0.1 under EQUAL 3 draws, then a quad at Z 0.5 under LESSEQUAL
leaves the back buffer alone; state restored on every path.

## Review dispositions

Three reviews, 2026-11-18: R1 design (0 Critical, 4 Important, 9 Minor), R2 recorder (0, 3, 6), R3 execution
(0, 14, 13). Every finding folded as cited above, except:

- **R1 f11** (widen step 4 to an extended device's two CPU-authoritative ends): not taken, no row can reach
  it; X-2 restated with its real cost.
- **R3 f5** (mutants for the destination level, the source slice and the size): replaced by R2 f6's unit
  test of the pure record builder; the cube-face source cell it asked for is added (`sc-rtex`).
- **R2 f1's "level-1 or face-k endpoint in each ordering cell"**: taken for the destination (S2); the source
  ordering cell (S1) is a standalone target, and the unit fence test holds a level-1 source.
- Sound as checked by the reviews and relied on: the tracker orders a pending Clear and an open pass against
  the blit; a read then a write of one subresource in one record hits no assert; a fresh target's zeroing
  precedes the chunk; `copies_bytes` agrees with the core's stored formats on every core device; staging is
  never dropped on core; lockable targets and the back buffer read the GPU back at each lock.

## Execution notes

Executed 2026-11-18. DXMTRS `c4c`: e949c472 (`copy`, `blit_record`, `level_shape`), a6741461 (`copy_depth`),
5d3a8bc3 (copies into a target), e8f01b97 (plain to plain), 30080983 (the depth-stencil copy; the tip).
MacNeutron `c4c-mn`: 3654dc9b (the rows). Artefacts: `build/c4c/s4b` (map, reviews, `rc*.log`, `gates/`).

- **Rulings SS and DM: outcome A for both**, decided by `jv0` before any front-end code: on mtld3d under Metal's
  validation layer the `blit` launch (rows ok 33) and the `msaa` launch (rows ok 22) report nothing, and
  `sc-same`, `sc-self`, `sc-depth`, `ms-desc`, `ms-dcopy` judge their pixels green; the one expected
  "render.scale ... copying the overlap" warning shows. The first `msaa` launch on mtld3d under the layer found
  nothing else. `jt5` showed the same two facts on core.
- **Controls**: every prediction held at the first run, no expectation corrected. `ja`: default and msl ok
  (`blit` 31 of 31, `msaa` 20 of 20); core red on exactly the thirteen `blit-sc-*` tokens and `msaa-runtime`,
  `msaa-ms-desc`, `msaa-ms-dcopy`; `was not recorded` 0. `jcr`: the VM's default lane ok on old and new WoW64,
  31 `bt64`/`bt32` and 20 `ms64`/`ms32` ok lines each.
- **Rows as written** (Task 1): each source's second colour is a quad larger than the copied rect, so a copied
  rect is one colour and no probe sits on a quad's edge; `sc-autogen`'s source is drawn before its copy (it has
  a core texture: mutant e leaves the row green); the cells that cannot show a wrong source origin (uniform
  cube faces, `sc-order`'s first copy) are named in the section comment, and each row has a cell that can;
  `sc-conv` runs outside `until()` (CPU only); helpers `copyTo`, `twoTone`, `drawnTwo`, `cleared`, `plain64`,
  `faceOf`, `lockHas`, `has565`, `depthBack`, `depthPair`.
- **Ruling O-1 held**: Task 2 ran beside Task 1; no outcome B, no rework.
- **Task 2 as built.** `blit_record` is `pub(super)` (`copy_depth` calls it), not private. `rect_bytes` and
  `copy` share one private `inside()` in `surface.rs`; `fill` keeps its closure in `recorder.rs`, which stays
  untouched (the Rules check said one function for three callers: two of three share; final review M2).
  `copy`'s refusals run before `same_surface_route`, whose additions are unchecked. The implementer also ran two
  red checks of its own: the accesses moved to level 0, slice 0 turn the two colour fence tests red
  (`rc2m.log`); `copy_depth`'s accesses dropped turn its two red (`rc2m2.log`). **No unit test holds the
  direction of a copy** (a swapped depth pair still touches both textures): mutant h and the rows hold it.
  `target.rs` is in the range by `TargetRef`'s doc sentence alone: P-2 holds, no probe.
- **Tasks 3-5 as built.** `refuse_stretch()` is private, `#[cold] #[inline(never)]`, and returns the answer;
  `obj` reaches `stretch_colour` in Task 4's commit; `same_surface_route` is computed once, ahead of the split;
  the same-bytes key reads the device's mapping (`map_for_device`) for `is_expanded_upload`; the unsafe blocks
  are more than the Rules check named (the two surface derefs in each arm, the source parent's shared `as_ref()`
  for the ref of a level that is no target), each with its SAFETY comment.
- **Lanes**, each exactly as predicted (translator key the base's; `was not recorded` 0): `jt3` (5d3a8bc3) core
  red on `blit-sc-plain2`, `blit-sc-conv`, `blit-sc-depth` and the three `msaa` tokens, the `blit` launch under
  core with the validation layer: reports 0, rows ok 30; `jt4` (e8f01b97) red on `blit-sc-depth` and the three
  `msaa` tokens; `jt5` (30080983) default and core ok, 31 of 31 and 20 of 20 on both, `blit` and `msaa` under core
  with the layer: reports 0, rows ok 33 and 22.
- **Mutants**: ten (a, b, c, e, e2, f, g, h, l, m), `gates/mutants-expect.txt` written before any existed. Against
  the plan's lists, from the rows as committed: a and b also redden `blit-sc-plain2` (its plain-into-target cell
  is the GPU route); e reddens `blit-sc-tex`, `blit-sc-plain`, `blit-sc-plain2` (not `sc-rtex`: `UpdateSurface`
  into a render-target texture uploads at once).
- **The chain** at 30080983 with 3654dc9b's rows (`jmb`, `jr`, `jc`, `jq`, `jm`, `jw`; 17:24 to 20:26, the lock
  shared with 4c's spike and recorder checks). `jr`: key and imports the base's, d3d9-check default ok (31 and
  20 rows), "x64 check: not run"; **the conformance had one untriaged site**, device.c:1945 (`test_cursor`:
  `GetCursorInfo`'s flags, the host session's cursor; Wine's own comment calls the test unreliable). Ruling
  C-1: not this range's code; the conformance alone ran once more at the tip (`jrc`): only the three triaged
  sites, 1945 not reproduced, not traced. `jc`: the VM's gate ok on default and core, old and new WoW64, 31
  `bt64`/`bt32` and 20 `ms64`/`ms32` ok lines each (one more under core), no error line in the VM's logs.
  `jq`: default, msl and core ok with every row, stateblock on both runtimes, the eleven launches under core
  with the validation layer: reports 0. `jm`: nine mutants red on exactly their tokens, each answer and colour
  as `mutants-expect.txt` had it (a with one error line, e with three, as predicted), the green tip whole.
  **Mutant m reddened `blit-sc-rtex` and `blit-sc-self` beside the predicted `blit-sc-order`**: with the
  destination's write tracked at level 0, slice 0, a Clear of the destination's level 1 or face is not ordered
  before the blit, and those rows read the Clear's colour. Ruling J-1: the prediction undercounted the
  mutant's reach; the tip is green on all three and the unit fence tests go red on the same mutation
  (`rc2m.log`); no rerun.
- **Final review** (`build/c4c/s4b/review-final.md`, range 3d5dcda3..30080983 and the rows): 0 Critical, 0
  Important, 3 Minor; "push as is". Traced sound: mtld3d unchanged (the two exit calls, behind `cfg` and
  `dev.is_core()`); the invariant in the front end and in `copy`; every return path of both arms; the borrows of
  the two parent textures; the direction and the record of both copies; the raw copy's key and rect form; one
  call site for the literal. Ledgered, no fix round: M1 `core_arms.rs`'s module doc lists the refused shapes
  less three (`stretch_colour`'s own doc names them; 4c rewrites the list); M2 `inside` (above); M3 this
  section's corrections of the plan's text. Not covered by a row or test, most important first: the CPU route's
  dirty mark when the destination already has a core texture (`sc-plain2`'s q is made whole after the copy: a
  cell for 4c); the claim after a copy into a level; a cube face of a texture that is no target as a source;
  extended-device paths; the ordering of a multisampled depth copy's source read; the order inside one blit
  encoder, held by pixels alone.
- **The x64 check** did not run in this slice (the range is inside `src/d3d9`); it runs at the end of C4c-4.
- **Sweep** (`build/c4c/s4b/sweep.md`, job `jw`, gating nothing): each function as the Goal's table predicted:
  `test_pointsize` 570 to 0, `test_stretch_rect` 81 to 40, `stretchrect_test` 51 to 17, `test_scene` 5 to 1,
  `depth_blit_test` 15 to 0, `test_mipmap_autogen` 8 to 0, `test_blit_format_conversion` 10 to 8; also
  `multisampled_depth_buffer_test` 17 to 12. New "Test failed" lines over the default runtime's baseline:
  `visual` 1912 to 1278, `device` 200 to 155, `d3d9ex` 66 as before; Wine's own failed counts 2162 to 1528 and
  939 to 894 (the same falls: no todo check changed).
