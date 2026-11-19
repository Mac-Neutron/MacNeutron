# DXMTRS C4c-4d: `StretchRect`'s depth-stencil resolve on D3D9's core runtime

Slice 4 of C4c, fourth and last part (ruling S-6 of the 4c plan). Process: the C4 program's (code map, this
plan, three adversarial reviews folded before code, rows first, one commit a feature, full gates once, one
whole-branch review, spec, pin, push). Map: `build/c4c/s4d/map.md` (cited "map 2a" ...); the 4c plan
(`2026-11-18-dxmtrs-c4c4c-sampled-copies.md`) and the 4b plan hold the rules this one repeats by name only.
Reviews: `build/c4c/s4d/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, `StretchRect` of a multisampled depth-stencil surface into a single-sample one of
the same format and size (whole surfaces: the gates allow nothing else) answers `D3D_OK` and leaves in the
destination **sample 0 of the source's depth and of its stencil**, as mtld3d's resolve does (map 1). After it
no row of any launch meets the refusal by name; its remaining callers are an extended device's copy into a
non-target, a plain pair no CPU route serves, and a multisampled colour source of a format this GPU does not
resolve.

Wine (map 3c): `multisampled_depth_buffer_test` 5 new "Test failed" lines after 4c → 0 (visual.c:17504 once,
:17524 four times; its depth is a ramp the judged pixels read within about 0.25, so sample 0 and an average
judge alike).

## Global Constraints

The 4c plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-4c pushed** (the ledger names both SHAs; the controls run only after that push); job
scripts in `build/c4c/s4d/gates`, copied from s4c's and edited before their first run (guard:
`/usr/bin/grep -nE 'c4c/s4c|c4c4c|C4c-4c'` over them prints nothing; VM state names `c4c4d-*`; the positive
checks: row counts, launch lists, letters and tokens); `base-key.txt` and `base-imports.txt` from s4c once its
`jr` printed "same"; a rerun is a new job name; every verdict counts its rows and its `was not recorded` lines;
every `d3d9-check.sh` run sets `DXMT_CHECK_WORK`; the pipeline lock; no implementer runs `dxmt/toolchain.sh`;
each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` changes.** Metal's own depth resolve (a resolve texture on the depth attachment)
  would need new wire fields, winemetal's unix side and dxmt-core's passes (map 2e): not taken (ruling DR-1).
- **No existing texture's creation changes**: the core's depth textures keep their usage (ruling DR-2).
- **This slice ends C4c-4: the x64 check (`dxmt/check.sh`) runs here**, once in the chain (`X64=1` in `jr.sh`)
  and once in the post-push check, the Mac idle (no subagent, no build) while it runs; it does not start
  between 03:05 and 03:35.

## The mechanism

### d3d9-rt: `depth_resolve` (new, `rt/src/device/depth.rs` or a file beside it)

`pub fn depth_resolve(&mut self, src: DepthRef, dst: DepthRef) -> bool` (`#[must_use]`; `resolve_depth` is taken:
the view builder `open_pass` calls, which stays as it is: R1 f1, R2 f1). False, and nothing
recorded, unless both resolve (`depth_level`), `src` has more than one sample and `dst` one, and width,
height and pixel format are equal. One render pass of the recorder's own on the destination, no colour
attachment, that writes each texel's depth (and stencil) from sample 0 of the source:

- **The shader** (a constant of d3d9-rt: mtld3d resolves with a compute kernel, so there is no text to share):
  a vertex function for one covering triangle (`vertex_id` alone), and two fragment functions:
  `depth2d_ms<float>` at texture 0 read at `uint2(position.xy)`, sample 0, returned as `[[depth(any)]]`; and
  the same plus `texture2d_ms<uint>` at texture 1 (the stencil plane) returned as `[[stencil]]` (dxmt-core's
  own helper shaders write both from a fragment function: map 2b.5). The text states the triangle's z (0) and
  takes `[[position]]`; it is compiled with safe math (as mtld3d compiles its depth kernel), through 4c's
  compile helper made to take a source and its entry names (one helper, two texts: R1).
- **The source's planes.** Depth: the source texture itself (its usage has shader read, map 2a).
  **Stencil needs an `X32_Stencil8` view, which Metal gives only to a texture made with the pixel-format-view
  usage**; the core's depth textures have none. So for a format with stencil the source is first copied
  (4b's `copy_depth`, one blit between two multisampled depth textures of one count, proven in 4b) into **a
  scratch** multisampled depth texture of the same format, size and count made with that usage
  (`texture_create` under a fresh id with `RENDER_TARGET | VIEW_TWINS`), and both planes are read from the
  scratch; the scratch is forgotten on every path. A format without stencil (D16, D24X8: `Depth32Float`)
  reads the source directly.
- **Recorded**: `close_pass()` (after the copy into the scratch, if any), then one `emit` holding the whole
  pass: the destination's depth plane (and stencil plane) attached for writing, load DONT_CARE (every texel
  is written) and store STORE, the level's extent as the pass's size; the pipeline of the destination's
  format (depth and stencil pixel formats set, one sample, no colour); a depth-stencil state of depth compare
  ALWAYS with depth write, and, **only when the format has a stencil plane**, stencil REPLACE on pass with
  compare ALWAYS and write mask ff **on both faces**: a D3D9 state snapshot handed to the existing
  `depth_state_handle` (no new field; **a 0 handle answers false**: Metal's default state writes no depth; R2
  f4); viewport (depth 0 to 1) and scissor the whole level; rasterizer fill solid, cull none; the source's plane views bound READ at fragment
  textures 0 and 1; three vertices.
- **Order of work** (R2 f5): the functions, the pipeline and the state are made first; only then the scratch
  is created and the copy recorded, so a pass Metal cannot make records nothing.
- **Owned by `CoreDevice`** (fields after `device`; no static): the functions' slot (compiled once a device; a
  failure remembered and logged with "does not build") and the pipelines by destination format.
- **It never merges** with a draw's pass (no colour target against one or more: R1, R2 f6), so the draw state
  of a neighbour cannot leak into it, and a later draw on the destination waits on it.

### Front end

`core_arms::stretch_depth`: unequal sample counts (the gates let through only "source more than one,
destination one") no longer refuse: `core.depth_resolve(source, destination)`; a missing ref or `false` → the
error line ("d3d9 core: StretchRect resolving depth {source:?} into {destination:?} was not recorded →
INVALIDCALL") and `D3DERR_INVALIDCALL`. Equal counts and equal refs are 4b's, unchanged. The docs of the
module, of `stretch_depth` and of `refuse_stretch` say which shapes still refuse by name.

## Review Focus (for the final review)

- The pass: what it attaches, in which planes and with which actions; the pipeline's formats; the
  depth-stencil state; that nothing of the device's draw state is read or changed; pass state after it.
- The scratch: its description and usage, the copy into it, its views, its release on every path.
- Sample 0, both planes, for both stored formats; the source untouched.
- Ordering against a Clear and a draw on the source and on the destination.
- mtld3d unchanged; nothing outside `src/d3d9`.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

- Launch `blit`: **`sr-shape` is replaced by `sd-resolve`** (41 rows still), judged in every lane:
  1. a 4-sample D24S8 surface, bound with a 4-sample target (a Clear and a draw reach a depth-stencil only
     beside a target of its sample count), cleared to Z 0.25 and stencil 3; a single-sample D24S8 surface bound
     with a single-sample target and cleared to Z 1.0 and stencil 0; the resolve with no draw between; with
     the destination bound: a quad at Z 0.1 under stencil EQUAL 3 draws, then a quad at Z 0.5 under LESSEQUAL
     does not, then a quad at Z 0.1 under stencil EQUAL 0 does not (as `sc-depth`'s `depthPair`);
  2. the source again, bound with its 4-sample target: a quad at Z 0.1 draws and one at Z 0.5 does not (the
     source is untouched; its read was ordered), read through the multisample resolve of that target;
  3. a drawn source: the 4-sample surface cleared to 1.0, a quad at Z 0.3 over its left half, the resolve;
     on the destination a quad at Z 0.5 draws on the right half and not on the left;
  4. a D16 pair the same way as cell 1, depth alone, the drawing quad first; **then, before any read-back, an
     unjudged quad on the D16 source bound with its 4-sample target** (mutant c's cell: a Clear of the source
     that is not ordered before the resolve folds into that draw; R1 f3, R3).
  Cell 3 in full (R3 f7): the quad over (0,0)-(48,16) (asymmetric, so a flipped or transposed read shows), the
  probes at least 8 texels from every quad edge (edge texels depend on the sample pattern), stencil off.
  Every resolve is made between scenes (a depth `StretchRect` inside a scene is refused by D3D9; both
  runtimes accept it: a stated gap). State restored on every path (`depthBack`).
- Launch `msaa`: new row `ms-dres` after `ms-dcopy` (21 rows): the device's 4-sample auto depth-stencil
  cleared to Z 0.25 and stencil 3, resolved into a single-sample 256x256 D24S8 surface cleared to 1.0 and 0;
  that surface bound with a single-sample 256x256 target for its Clear and its judging (a Clear reaches a
  depth-stencil only beside a target of its sample count): the three quads of cell 1.
- Scripts (map 3b; half-done moves fail silently): `dxmt/d3d9-check.sh`: `blit` leaves `refusals_ok`'s outer
  case and its `set_` line **and joins the "none" list**; `stretch_refusals` goes if nothing uses it; the
  `check blit` and `check msaa` lists; prose. `vm/dxmt/d3d9-gate.sh`: `bt64-fresh bt32-fresh` take `runtime`
  with no kind, the `stretch` arm and variable go if unused; both row loops; prose. `vm/tests/d3d9.sh` prose.
  The test file's header (its row counts and the `sd` prefix) and the rows' comments, `depthBack`'s comment;
  the `msaa` count 20 → 21 in `d3d9-check.sh`'s, the gate's and the guest script's prose; **`jcr.sh` and
  `jc.sh` want `MSAA_ROWS` 21** (a row missing from the VM gate's loop would otherwise pass unjudged: R3 f4);
  `jv0.sh`'s row grep. `sh -n`.
- Controls after 4c's push, in this order: `JOBS=jv0` (`blit`, `targets`, `msaa` on mtld3d under the
  validation layer: reports 0, rows ok 43, 37, 23; mtld3d's resolve is its compute route, judged for the first
  time by pixels: a red cell or a report is an mtld3d finding, ledgered, and the cell is dropped or made
  answer-only in a second row commit before any code); `ja` (default and msl green at the first run; core red
  on exactly `blit-sd-resolve`, `msaa-ms-dres`, `blit-runtime` (one refusal line where none is wanted) and
  `msaa-runtime`); `jcr` (the VM's default lane, `bt` 41 and `ms` 21 ok lines each).

Commit: "dxmt, vm: d3d9_triangle rows for StretchRect's depth-stencil resolve (DXMTRS C4c-4d)".

### Task 2: `depth_resolve` (d3d9-rt)

One commit, "d3d9: a depth-stencil resolve on the core runtime". Tests on the fakes as they are (the fixture
already exports the library call): refusals, each false with no line (equal counts, a multisampled
destination, unequal sizes, unequal formats, a colour id, a single-sample source); a stencil format records a
blit (the copy into the scratch) and then one render pass with no colour target, depth and stencil attached,
the pipeline, the depth-stencil state, two texture binds whose view lines hold the scratch's depth view and its
`X32_Stencil8` view (multisample type), one draw of three vertices, `!render_open` after; a depth-only format
records no blit and one bind; the pipeline once a format. **What the fakes cannot show** (they live outside
`src/d3d9`; R2 f3): a pipeline's depth and stencil formats, a colourless pass's size, a view line for a
texture's own full view, a texture-to-texture copy's ends: Task 3's validation run and the rows hold those.
**Fences** (R1 f2, R2 f2, R3 f1: a draw that binds a depth-stencil writes it, so it waits on every read since
the last write): a depth Clear of a depth-only source, then the resolve: its pass waits on the Clear's; the
resolve, then a draw with the destination bound: the draw's pass waits on the resolve's; **the resolve, then a
draw with the source bound: the draw waits** on what read the source (the copy into the scratch for a stencil
format, the resolve's pass for a depth-only one); a pending Clear of the destination, then the resolve: the
Clear folds into the pass and is dropped (DR-3).

### Task 3: the front end

`stretch_depth`'s arm and docs. Lane `jt3` (default, core) at the tip: every row of every launch green strict
under core; refusal lines `blit` 0, `targets` 0, `msaa` 0; `was not recorded` 0; **`blit` and `msaa` under
core with the validation layer** (reports 0, rows ok 43 and 23: the first real compile of the new text and the
first stencil view of the scratch). Commit: "d3d9: StretchRect resolves a depth-stencil surface on the core
runtime".

### Task 4: gates

As the 4c plan's Task 6: `jmb`, then `jr`, `jc`, `jq`, `jm`, `jw`. **The x64 check is its own job, `jx`,
after the chain** (R3 f5, f6: inside `jr` it starts at a time nobody sees and can be skipped silently): the
plain build at the tip and `dxmt/check.sh`, forced; **nothing else runs while it does** (no subagent, no other
job: a hold from its start to its end); it does not start between 02:55 and 03:35 (the longest run on record
took 27 minutes); a lone red row is run once more as `jx2` before anything else is concluded, as 4a's `jx`.
The post-push check does the same. Mutants (final tokens in `gates/mutants-expect.txt` from the rows as committed):
  a. the fragment function returns stencil 0 → `blit-sd-resolve` (cell 1's EQUAL 3 quad), `msaa-ms-dres`;
  b. the scratch is read without the copy into it (zeros) → `blit-sd-resolve`, `msaa-ms-dres`;
  c. the depth-only path binds its source without tracking the read → `blit-sd-resolve` (cell 4: the
     source's Clear lands after the resolve); a stencil format's source is read by 4b's `copy_depth`, which
     this mutant leaves alone;
  d. the depth-only fragment function returns 1.0 → `blit-sd-resolve` (cell 4).
  Held by the unit tests: the attachments' planes and actions, the depth-stencil state's record, the stencil
  view; by Task 3's validation run: the pipeline's formats. Not gated, stated: which sample is read (no row and no Wine test tells sample 0 from an average;
  the shader's literal 0 is what the review reads).
- `jw`: the sweep → `build/c4c/s4d/sweep.md`.
- **Probe: none** (no draw file changes; `depth.rs` gains a method no draw calls).

### Task 5: review. Task 6: spec, pin, push

As the 4c plan's; the post-push check runs the x64 check (the Mac idle). The spec's slice list closes C4c-4.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none.
- **New pub items:** `CoreDevice::depth_resolve`. **New state:** the functions' slot, the pipelines by format
  and the depth-stencil state's handle on `CoreDevice`.
- **A duplicate, named:** the pass's record code repeats the shape of 4c's `stretch` (itself a named duplicate
  of the draw path's); a second shader constant, because mtld3d has no fragment resolve to share.
- **Not silent:** a pass Metal cannot make logs once and answers false; the arm logs "was not recorded".
- **Divergence added:** mtld3d resolves through a compute kernel and plane blits, the core through a render
  pass that writes depth and stencil; both take sample 0. A source with stencil is copied once more than on
  mtld3d copies every multisampled source, for the same usage reason (map 1d); the core copies only a source
  with stencil.

## Rulings (planning)

- **DR-1. The resolve is a render pass of d3d9-rt's own**, not Metal's depth-attachment resolve (new wire
  fields, winemetal and dxmt-core: outside `src/d3d9`, and mtld3d documents a device bug on that route) and
  not a port of mtld3d's compute kernel and plane pool (more machinery for the same sample 0). Cost if wrong:
  Task 3's first validation run shows it, and the compute route is the fallback.
- **DR-2. A stencil source is read through a scratch made with the view usage**, not by giving every core
  depth texture that usage (a change to every depth buffer's creation, with a cost no probe has measured).
  Cost: a multisampled depth texture created, zeroed through the initializer (a pass on its queue and a wait
  across queues) and copied at each stencil resolve; at 1440p and 4 samples about 75 to 120 MB for the call.
- **DR-3. Load DONT_CARE on the destination**: the gates allow only whole surfaces, and the pass writes every
  texel of both planes. Cost if wrong: a format whose stencil plane the pass does not write keeps garbage;
  the D16 cell and the stencil cells hold the two formats.
- **SP-1. No separate spike**: the Metal facts the pass rests on (a colourless pass, `depth2d_ms`, the
  multisampled stencil view, `[[stencil]]` with REPLACE) are first run by Task 3's lane under the validation
  layer, right after Task 2. dxmt-core's own helpers and mtld3d's kernel use each of them singly. Cost if
  wrong: Task 2 is redone on mtld3d's compute route, about three hours; that is a redesign, not a switch (R1).
- **FP-1. A resolve Metal cannot make answers `INVALIDCALL` with an error line** ("was not recorded"), where
  mtld3d answers `D3D_OK` with a warning: the gates then see it.
- **P-4. No probe.**

## Stated gaps

No row tells sample 0 from another reduction. A depth `StretchRect` inside a scene is accepted on both
runtimes where D3D9 refuses it. A scratch a call (as 4c's). A depth-texture level is never a
depth-stencil endpoint of `StretchRect` (the classifier gives it no flag; the gates answer `INVALIDCALL` on
both runtimes).

## Review dispositions

Three reviews, 2026-11-18: R1 design (0 Critical, 4 Important, 9 Minor), R2 recorder (0, 4, 7), R3 execution
(1, 7, 11). Folded as cited above: the method's name; the corrected fence tests (R3's Critical: the draw on
the source waits); what the fakes show; the state through `depth_state_handle`; mutant c's cell and mutant d;
cell 3's rect and probes; the VM's row count; the x64 check as its own job. Decided against a reviewer's
alternative: **R1's spike** (SP-1: Task 3's first lane is the test); **a direct stencil view of the source
without the view usage** (it would remove the scratch if Metal allowed it, but Apple's text ties views in
another format to that usage, and the validation layer is not the last word on a usage rule: DR-2 stays; an
experiment for later if the scratch's cost shows). The Goal's list of the refusal's remaining callers is
re-derived from 4c's tip when Task 3 is written.

## Execution notes

(after execution)
