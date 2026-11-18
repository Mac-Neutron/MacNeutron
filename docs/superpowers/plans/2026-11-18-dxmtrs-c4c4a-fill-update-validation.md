# DXMTRS C4c-4a: `ColorFill`, `UpdateSurface` and `StretchRect`'s validation on D3D9's core runtime

Slice 4 of C4c, first of three parts (ruling S-4 below). Process: the C4 program's (code map, this plan, three
adversarial reviews folded before code, rows first, one commit a feature, full gates once, one whole-branch
review, spec, pin, push). Map: `build/c4c/s4/map.md` (cited "map A1", "map C" ...). Sweeps:
`build/c4c/s3b/sweep.md` and `build/c4c/s3/sweep.md`. Reviews: `build/c4c/s4/review-{1-design,2-core,3-exec}.md`.
Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and rules:

- `IDirect3DDevice9::ColorFill` works on every surface mtld3d fills: the back buffer (an additional swap
  chain's too), a standalone render target (lockable or not, multisampled or not), a render-target texture's
  level or cube face (an AUTOGENMIPMAP one regenerates its chain), and a DEFAULT offscreen plain surface (on
  the CPU, as on mtld3d); every other surface answers mtld3d's `INVALIDCALL`, a null surface included;
- `IDirect3DDevice9::UpdateSurface` works whole: a SYSTEMMEM surface or texture level into a DEFAULT texture's
  level or cube face (CPU staging, uploaded at the next use, or at once for a render-target texture), and into a
  standalone render target or the back buffer (a region written in application order);
- `IDirect3DDevice9::StretchRect` answers mtld3d's `D3DERR_INVALIDCALL` for every call mtld3d rejects, before
  the refusal by name. A valid shape other than the multisample resolve is still refused
  (`D3DERR_NOTAVAILABLE`, the same literal): the copies are C4c-4b and 4c.

Wine's suites: `color_fill_test` (104 new failures under core), `test_flip`'s fills, the `UpdateSurface` tests
(`update_surface_test` 20, `add_dirty_rect_test` 13, `test_signed_formats` 18, `test_cube_wrap` 11,
`test_generate_mipmap` 1, `test_mipmap_lock` 2), and of `test_stretch_rect`'s 810 the 729 that want
`INVALIDCALL` (counted from the raw log: sites device.c:13393, 13405, 13434, 13437, 13447 and the
"expected 0x8876086c" lines; the 81 that want `D3D_OK` are 4b's and 4c's).

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main 4eea8d68 and MacNeutron main 9bc5968f (C4c-3b). Fetch both origins before the
  first build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- **No file outside `src/d3d9` changes** in DXMTRS: not dxmt-core (`src/dxmt/src`), not its fakes, not
  `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal`, `src/nvapi`. If a task finds it needs one, it stops and
  reports.
- The default runtime (mtld3d) draws and answers exactly what it does today: its translator key, imports and
  conformance are gates. Every edit of a function mtld3d runs is a core arm under
  `cfg(feature = "core-runtime")` and a test that is false on an mtld3d device, ahead of code that stays as it
  is.
- **No mtld3d recorder call may be reachable on a core device** (`push_control`, `capture_frame_bytes`,
  `schedule_upload`, `flush_current_frame_blocking`, `unix_call`): each panics there. **And no code below a
  core arm may read a `StretchKind`'s Metal handle**: on a core device it is null by construction (below).
- **One invariant kept (advisor, map G3): on a core device only render-target levels, standalone targets and the
  back buffer are ever written by the GPU.** A DEFAULT offscreen plain surface and a DEFAULT texture level that
  is no render target stay CPU-authoritative (their core texture is an upload of their staging), because the
  core cannot read a non-target texture back. 4a's `ColorFill` of a plain surface is a staging fill; 4b's
  plain-to-plain copy has to keep it.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree. **No implementer runs `dxmt/toolchain.sh`**; the compiler is
  `~/Documents/MacProton/build/dxmt-src/llvm-mingw-own/bin`.
- **Job scripts** live in `build/c4c/s4a/gates`, each copied from `build/c4c/s3b/gates` and edited **before**
  its first run; a job that has run is never edited (a new job is a new file); jobs test committed tips. Before
  any s4a job runs, `/usr/bin/grep -rnE 'c4c/s3b|c4c3b|C4c-3b' build/c4c/s4a/gates` prints nothing.
- **A mutant names a feature-gated item only under `#[cfg(feature = "core-runtime")]`**, and every mutant is
  built once before the chain (`jmb`); the chain's `jm` reuses those builds.
- **Wanted values come from the base:** `base-key.txt` and `base-imports.txt` are copied from s3b (the range
  a1d45dd5..4eea8d68 left both unchanged: `jr` of C4c-3b printed "same" for each); `jr.sh` and `post.sh` read
  them.
- **No `dxmt/check.sh` run may cross 03:35.** A chain or a lone `jr` or `post` starts before 02:55 or after
  03:36.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller. d3d9-rt is an rlib: its primitives are their own commits.
- One lane (default, core) per task that moves a row, where this plan names one; full gates once; no fix round
  for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8. Out, with where it goes:

- **C4c-4b:** `StretchRect`'s unscaled copies of one storage (target to target, the back buffer, plain to
  plain and plain or texture into a target, the same-texture routes, the depth copy), and the CPU conversion
  into a plain surface. **C4c-4c:** the scaled, converting and YUV copies (a sampled quad), multisampled
  destinations, the depth resolve.
- **`ColorFill`'s formats mtld3d leaves unfilled** (planar and packed YUV, DXT, R8G8B8, P8, L16, A8L8 on a
  plain surface: `D3D_OK`, surface unchanged): the same on core, nothing to build.
- **`render.scale`**: a core device is created at `RenderScale::IDENTITY` (`direct3d9.rs:2440`) and a core
  standalone surface too (map D), so no rect is scaled on core; stated, not built.
- The two silent differences the sweeps found (a volume texture bound where a 2D one is sampled;
  `D3DSAMP_SRGBTEXTURE` for values other than 0 and 1): unchanged, owed where binding is next touched.

## The mechanism

### d3d9-rt

**`fill`** (new, `recorder.rs`, beside `clear`):
`pub fn fill(&mut self, target: TargetRef, rect: Option<[u32; 4]>, rgba: [f32; 4]) -> bool`. Any colour
target the core resolves, bound or not, multisampled or not. The view is the target's own in its stored
format (`self.resolve_as(target, shape.pixel_format)`: never an sRGB twin) and `rgba` is stored as given:
`ColorFill` writes its colour verbatim whatever `D3DRS_SRGBWRITEENABLE` says (mtld3d forces sRGB write off,
`unix/encoder.rs:4899`). `None` = the whole view: `close_pass`, `enc.clear_color` (which folds into the next
pass on that view or runs alone); `Some([x, y, w, h])` inside the target: `close_pass`, the quad clear
(`with_clear_rtv`, one rect). `note_written(target, format)` as a Clear does. False, and nothing recorded,
when the target does not resolve, or the rect is empty or leaves the target. `clear` and `clear_rects` keep
their behaviour and share the two emit bodies with `fill` (one private helper each for "whole view" and
"rects of a view": no second copy of either closure). The draws' state is not touched: the quad clear takes
none of it, and the bound targets, the depth and the per-draw viewport and scissor are the front end's and
unchanged; the pass that was open is closed and the next draw reopens it with its load actions.

**`write_presented` takes a rect** (`textures.rs:500`): `write_presented(rect: DirtyRect, bytes, pitch)`.
`rect_bytes(shape, rect, ...)` already validates a rect inside the back buffer; the copy's origin and size
are the rect's. Its two callers of 3b pass `DirtyRect::full`. A multisampled back buffer is still false.

Nothing else: a standalone target's region goes through `texture_upload` (3a's `core_lockable_rt_upload`
uses it whole; a region is the same call), and a texture level's through `core_flush_dirty`.

### Front end

**The classifier: `resolve_stretch_surface` learns a core device's standalone surfaces** (`device.rs:10115`).
Texture-backed endpoints already classify on core (their arm reads the texture, no handle). The two
standalone arms test a Metal handle, null on core, so today a core back buffer, render target and
depth-stencil answer `None` (a silent `INVALIDCALL`). Under the feature's `cfg`, after the texture arm:

- `s.core_target().is_some()` (a back buffer, a standalone target) → the colour arm's info with
  `kind: StretchKind::Backbuffer(MetalHandle::NULL)`, `msaa` and `msaa_srgb` null, the rest as mtld3d
  (`standalone_width/height/format`, `render_scale()`, pool DEFAULT, `IS_RENDER_TARGET`,
  `sample_count` from `multi_sample()`);
- `s.core_depth().is_some()` → the depth arm's info with `kind: StretchKind::DepthStencil(MetalHandle::NULL)`,
  `IS_DEPTH_STENCIL`, `sample_count`.

Both tests are false on an mtld3d device (`core_target`/`core_depth` read `core_texture`, the back buffer of a
core device, or a core device's auto depth). mtld3d's two arms stay as they are below them. A SYSTEMMEM or
SCRATCH plain surface is `None` on both runtimes. **A depth texture's level** takes the texture arm on both
(no flag), as on mtld3d.

**`StretchRect`.** The early jump at `device.rs:9411-9414` goes. A core device runs mtld3d's gates (map A1
steps 3-11, all pure: the infos, `class_reject`, `check_stretch_rect_formats`, `parse_stretch_regions`, the
same-surface, planar and scaling rules) and leaves at **two exits**, each `#[cfg(feature = "core-runtime")]
if dev.is_core() { return core_arms::stretch_rect_resolve(this, [src, dst], [src_rect, dst_rect]); }`:

1. in the depth branch, after its last gate and **before** `flush_dirty_mips_for_gpu_write` (`:9521`);
2. after the scaling gate and **before** `flush_dirty_mips_for_gpu_write` (`:9703`), which is ahead of the
   CPU-convert return (`:9732`) and of the tail's `push_control`.

`stretch_rect_resolve` is unchanged: the resolve shape is recorded, anything else is the logged refusal and
`D3DERR_NOTAVAILABLE`. So a valid depth copy or resolve (exit 1), a valid copy, scaled copy or conversion
(exit 2) are refused by name, and everything mtld3d rejects is `INVALIDCALL`. `frame_dump_event` above the
gates is mtld3d's and inert on core (`frame_dump.active` false: verify, R-item).

**`ColorFill`.** The refusal at `:10380` goes; the gates run as they are (map A2 steps 2-7; the classifier
above makes step 3 right on core). Then, under the feature's `cfg` and `dev.is_core()`:

- **a render target** (`IS_RENDER_TARGET`): `core_arms::color_fill(dev, surf, &info, region, color)`:
  the target is `surf.core_target()`; a texture-backed one first `core_flush_dirty(parent, core)` (its core
  texture exists from here with every CPU write uploaded: `flush_dirty_mips_for_gpu_write` returns early on
  core) and `claim_dst_subresource_for_gpu` (pure state: the next lock reads the level back, which 3a's arm
  serves for a render-target level); `core.fill(target, rect, d3dcolor_to_rgba_f32(color))` with `rect = None`
  when the region is the whole surface; then `core.texture_generate_mips(id)` when
  `info.autogen_texture_id` is set. A false `fill` logs one error line and answers `D3DERR_INVALIDCALL`
  (never a silent `D3D_OK`). A lockable target's staging is stale after it, as on mtld3d: 3a's lock and 3b's
  `GetDC` read the target back.
- **a DEFAULT offscreen plain surface**: `color_fill_offscreen_plain` runs to `fill_staging_region`; where
  mtld3d calls `schedule_upload` a core device marks the level instead: `ti.mark_mip_dirty_rect(level, region)`
  and `dev.mark_snapshot_dirty_all()` (what a lock's unlock leaves; `core_flush_dirty` uploads it when the
  texture is next flushed). **No row can see that upload in 4a** (a plain surface is never sampled and its
  `StretchRect` out is 4b): the lock reads the staging. Stated gap; 4b's plain-source row is its gate.

**`UpdateSurface`.** The refusal at `:8454` goes. Branches A and C (map A3) are CPU staging copies and
`publish_staging_write`, which is armed since C4b-3d1: unchanged. Branch B's entry test
(`dst_parent.is_null() && !dst_surf.metal_color_handle().is_null()`, `:8620`) gains, under the feature's
`cfg`, `|| dst_surf.core_target().is_some()` (with `dst_parent` null: a standalone target or a back buffer;
false on mtld3d). In `update_surface_into_color_target` the ten rejects are pure and stay; before
`dst_surf.render_scale()` (`:8803`) a core device leaves through
`core_arms::update_color_target(dev, dst_surf, origin, (region.w, region.h), &rows, row_bytes)`:

- the back buffer (`TargetRef::BackBuffer`): `core.write_presented(DirtyRect{origin, extent}, &rows,
  row_bytes)`;
- a standalone target (`TargetRef::Texture{id, ..}`): `core.texture_upload(id, UploadRegion{level 0, origin,
  extent}, &rows, row_bytes, row_bytes * h)`.

False logs one error line and answers `D3DERR_INVALIDCALL`. The rows are the tight vector mtld3d builds
(converted when the formats differ); reject 6 keeps expanded storages out, so the rows are the storage's.

**Refusal literals.** Removed: `IDirect3DDevice9::ColorFill`, `IDirect3DDevice9::UpdateSurface`. Kept:
`IDirect3DDevice9::StretchRect (other than a multisample resolve)`. New: none.

## Review Focus (for the final review)

- No recorder call reachable on a core device from the three entries; no read of a null handle below an arm.
- Every hunk in a function mtld3d runs is gated (`cfg` + a test false on mtld3d); mtld3d's order of answers
  unchanged.
- The classifier's two core arms against every surface kind of map D (lockable target, additional chain's
  back buffer, auto and standalone depth-stencil, SYSTEMMEM plain, depth texture level).
- `fill`: the view's format, the stored colour, pass state after it, a multisampled target, the back buffer.
- The region writes: origin, extent, pitch, order against draws before and after.
- The invariant above (who is GPU-authoritative on core).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`blit`** in `dxmt/tests/d3d9_triangle.cpp` (rows in the Appendix), wired like `buffers`
(C4c-1): `main`'s mode list, `blitRows`, the header prose; `dxmt/d3d9-check.sh` (a `launch ... blit` line with
`$strict`, a `check blit` row list, a `blit` case in `refusals_ok` wanting exactly `stretch_refusals`, the
header prose), `vm/dxmt/d3d9-gate.sh` (row list, the 64- and 32-bit launches `bl64-fresh bl32-fresh` in the
`runtime ... stretch` loop, prose), `vm/tests/d3d9.sh` (the launch and its budget, prose). `sh -n` on the
three scripts. The device is `main`'s (256x256 X8R8G8B8 windowed, no auto depth-stencil); a row that needs a
depth-stencil surface makes one.

**Rules for every new cell:** a lock is released before its pixels are judged; every drawn-pixel cell runs in
`until()` with its objects made inside the attempt; colours have every channel non-zero and are pairwise
distinct within a row; no cell judges an out structure after a failed call; **every region write uses a rect
off the origin, a destination point different from the source origin, and a width that is no multiple of 4**
(advisor); **every GPU write cell has a draw recorded before it and one after it on the same target** whose
pixels pin the order. A cell that wants `INVALIDCALL` wants it in every lane. The `sr-shape` cells want
`D3D_OK` on mtld3d and, strict, `D3DERR_NOTAVAILABLE` (the pattern of `rt-msaa`'s last cell,
`d3d9_triangle.cpp:6239`); their pixels are judged in 4b and 4c, not here.

Job scripts in `build/c4c/s4a/gates` from s3b's (the guard above; state names `c4c4a-*`): `head.sh`,
`ctl.sh` (`ja`, `jv0`, `jcr`), `ja.sh`, `jv0.sh` (the `blit` launch), `jcr.sh`, `jt.sh`, `jq.sh` (eleven
launches), `jr.sh`, `jc.sh`, `jw.sh`, `jmb.sh`, `jm.sh`, `post.sh`, `chain.sh` (`jr jc jq jm jw`).

Controls at the base + the rows, each failing set written in the ledger before its run:
- `ja`: default and msl: **every row green on mtld3d at the first run, no expectation corrected** (a row red
  on mtld3d is a wrong row or a finding: fixed or dropped before any code, and the ledger says which). Core,
  the failing tokens predicted: every `blit-cf-*` and `blit-us-*` row, `blit-sr-reject`, `blit-runtime`, and
  nothing else (`blit-sr-shape` is green at the base: the early jump already answers `NOTAVAILABLE`; the
  ledger lists the exact tokens once the rows exist).
- `jv0`: the `blit` launch on mtld3d under Metal's validation layer: 0 reports.
- `jcr`: the VM's default lane on old and new WoW64.

Commit: "dxmt, vm: d3d9_triangle's blit launch: rows for ColorFill, UpdateSurface and StretchRect's answers
(DXMTRS C4c-4a)".

### Task 2: `fill` and the rect of `write_presented` (d3d9-rt)

As "d3d9-rt" above; two commits. Tests on the fakes as they are. `fill`: whole of an unbound target (one
clear line on that target's view in its stored format, the pass that was open closed, the next draw's pass
loads its own targets); a rect (the quad clear's lines: begin, one scissored rect, end); a target with an
sRGB twin fills through the plain view and the colour is not encoded; the back buffer; a multisampled target;
an empty rect, a rect past the edge and an unknown id answer false with no line; `clear`'s and `clear_rects`'s
existing exact-line asserts hold unchanged. `write_presented`: a rect's copy line (origin, size,
bytes-per-row), a rect past the edge false with no line, 3b's tests updated to pass the whole rect. Commits:
"d3d9: a fill of any colour target on the core runtime", "d3d9: write_presented takes a rect".

### Task 3: the classifier and `StretchRect`'s answers (front end)

The two core arms of `resolve_stretch_surface`, the early jump removed, the two exits; `core_arms.rs`'s
module doc and `stretch_rect_resolve`'s doc say the gates ran. `ColorFill` and `UpdateSurface` stay refused.
Lane `jt3` (default, core): `sr-reject` and `sr-shape` green strict under core; failing tokens predicted:
every `blit-cf-*`, every `blit-us-*`, `blit-runtime`. Commit: "d3d9: StretchRect answers mtld3d's
INVALIDCALL on the core runtime".

### Task 4: `ColorFill` (front end)

The refusal removed, `core_arms::color_fill`, the plain surface's marks. Lane `jt4` (default, core): every
`cf-*` row green strict; failing tokens predicted: every `blit-us-*`, `blit-runtime`. Commit: "d3d9:
ColorFill on the core runtime".

### Task 5: `UpdateSurface` (front end)

The refusal removed, branch B's entry test, `core_arms::update_color_target`. Lane `jt5` (default, core) at
the tip: every row of every launch green strict under core, every launch's refusal set exact (`blit`,
`targets`, `msaa`: the one `StretchRect` line each), **and the `blit` launch once under core with Metal's
validation layer** (0 reports: the quad clear on unbound and multisampled views and the region blits are legal
Metal). Commit: "d3d9: UpdateSurface on the core runtime".

### Task 6: gates

- `jmb` before the chain: every mutant builds.
- `jr`, `jc`, `jq` as C4c-3b's (`jq`: eleven core launches under validation, 0 reports).
- `jm`: mutants as commits off the tip, each red on its named row or rows and on nothing else;
  `gates/mutants-expect.txt` names, before the chain runs, the cell and the texel or answer that changes:
  a. `blit:sr-reject`: a core device leaves `StretchRect` for the resolve arm right after the filter check
     (the base's jump) → the first `INVALIDCALL` cell answers `NOTAVAILABLE`;
  b. `blit:sr-shape`: the classifier's core depth arm answers `None` → the valid depth copy answers
     `INVALIDCALL` where strict wants `NOTAVAILABLE`;
  c. `blit:cf-rt`: `color_fill`'s rect is dropped (the whole target filled) → the texel outside the rect;
  d. `blit:cf-srgb` (or the cell of `cf-rt` that sets `SRGBWRITEENABLE`): `fill` picks the sRGB twin's view →
     the stored bytes are the encoded colour;
  e. `blit:cf-rtex`: the texture arm skips `claim_dst_subresource_for_gpu` → the lock after the fill reads
     the staging's old colour;
  f. `blit:cf-rtex`: the texture arm skips `core_flush_dirty` → the fill of a texture never used before
     fails (no core texture): the answer and the texel;
  g. `blit:cf-autogen`: no `texture_generate_mips` → the minified sample reads the old chain;
  h. `blit:us-rt`: `update_color_target` writes at the origin (0, 0) → the texel at the destination point;
  i. `blit:us-bb`: the back buffer's write ignores the rect's origin → the texel at the destination point;
  j. `blit:cf-unbound`: `fill` does not close the open pass → named beforehand from the fakes' order (if the
     fakes cannot show a pixel difference the mutant is dropped and the cell stays, ledgered).
  Not gated by a mutant, stated: the plain surface's dirty marks (no row sees the upload until 4b); branches A
  and C of `UpdateSurface` (no core code: their rows are the gate that the armed helpers serve them); the
  classifier's colour arm (every `cf-*` and `us-*` row on a standalone target fails without it).
- `jw` (gates nothing): the sweep → `build/c4c/s4a/sweep.md`, in Wine's own counts: `color_fill_test`,
  `test_flip`, the six `UpdateSurface` functions, `test_stretch_rect` (predicted 810 → 81), `stretchrect_test`,
  `depth_blit_test`, `test_multisample_stretch_rect` before and after; the refusal lines met. It also corrects
  3a's per-call figures (map: `test_cube_wrap` is `UpdateSurface`'s, `test_filling_convention` is
  `StretchRect`'s, `test_sysmem_draw` is `ProcessVertices`'s; checked against the raw log).
- **Pre-ruled, no rerun for them** (memory): the VM or Mac `visual` count 211159 with failed 29 and
  `visual.c:25196` at 0; "fence-order 1 0"; "presents to a minimised window" when HID idle time at the failure
  is under ten minutes. A build that died on a code-signing timestamp is run again. Anything else red is
  investigated before the push.
- **A probe is owed if the range touches `rt/src/device/{draw,recorder}.rs` or `src/device/core_draw.rs`**
  (C4c-3b's ruling 8). `fill` lives in `recorder.rs`; `clear` and `clear_rects` change shape (shared helpers),
  not what they emit: the fakes' exact-line asserts for both are the proof, and **no probe is run** if those
  asserts are untouched in the diff (ruling P-1; if an assert had to change, one bench pair, once).
- No D3D10-12 gate while `git diff --stat <base>..<tip>` shows no file outside `src/d3d9`.

### Task 7: review

One whole-branch Opus review (`build/c4c/s4a/review-final.md`), given the diff and this plan; Critical and
Important fixed with a red cell first where a pixel can show it; Minors fixed if one line, else ledgered.

### Task 8: spec, pin, push

Spec §5 paragraph "C4c-4a" and the slice list (4 splits in 4a, 4b, 4c). Plan execution notes. `dxmt/pins`.
Privacy scan of both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron (the SHA
written out: zsh reads `$T:r` as a modifier). Post-push check (not across 03:35). CI. Memory. Scratch deleted
by full path.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** none. **A lint
  suppression:** none. **A derive:** none.
- **New state:** none on the device, a surface or a texture (the dirty marks, `level_authority` and the
  snapshot flag are mtld3d's).
- **New pub items:** `CoreDevice::fill`; `core_arms::color_fill`, `core_arms::update_color_target`
  (`pub(super)` or as the module's others). **Changed signature:** `write_presented` (a rect). **Unsafe:**
  none new expected (the arms take references the callers already hold); any that appears carries a SAFETY
  comment, one operation a block.
- **A duplicate:** none meant. `fill` shares `clear`'s and `clear_rects`'s emit bodies; the classifier is
  mtld3d's function with two arms, not a second validator; the region rows are mtld3d's vector; the plain
  surface's fill is mtld3d's `fill_staging_region`.
- **Not silent:** a `fill` or a region write that fails logs an error in its arm and answers `INVALIDCALL`;
  the refusal keeps its name; the gates fail on an unexpected refusal line.
- **Divergence added:** none meant. mtld3d's fill of a render target and its region update are ordered among
  the passes, as the core's are. **Kept, mtld3d's and now the core's too:** `ColorFill` clamps its rect and
  answers `D3D_OK` for an empty one; a format with no fill encoding on a plain surface is `D3D_OK`, unfilled;
  `StretchRect` has no scene check; a lockable target's staging is stale after a GPU fill until its next lock.
- **Docs in the same change:** DXMTRS's `STATUS.md`, `COVERAGE.md`, `CONFORMANCE.md` and `mtld3d.conf` say
  nothing of the core runtime: no edit owed; Task 8 confirms it against the final diff.

## Rulings (planning)

- **S-4. C4c-4 is three slices.** 4a: what needs no GPU-to-GPU copy (this plan). 4b: unscaled copies of one
  storage (a texture-to-texture blit primitive; CPU routes for plain surfaces). 4c: the sampled quad (scaled,
  converting, YUV), multisampled destinations, the depth resolve. Why: each needs one new kind of primitive, and
  each moves a nameable set of Wine's tests. Cost if wrong: one more push and gate round than a two-way split.
- **V-1. `StretchRect`'s validation is mtld3d's own gates run on core**, with the classifier's two arms, not a
  second validator in `core_arms`. Why: one order of answers on both runtimes. Cost if wrong: a gate that reads
  something a core surface lacks misanswers silently; `sr-reject`'s cells and the review's kind-by-kind trace
  are the guard.
- **V-2. A valid `StretchRect` other than the resolve stays refused by its old literal.** The exact-one refusal
  counts of `targets` and `msaa` hold (their two strict cells are valid shapes), and `blit` joins the set. Cost
  if wrong: a count gate fails in `ja`/`jt3`, cheaply.
- **F-1. `fill` uses the plain view and stores the colour as given.** Why: mtld3d's fill is verbatim bytes.
  Cost if wrong: an sRGB target's fill is off by the transfer curve; `cf-srgb`'s cell and mutant d see it.
- **F-2. A plain surface's `ColorFill` stays on the CPU and only marks the level** (the invariant). Cost if
  wrong: none seen in 4a (the upload is unobservable); 4b gates it.
- **U-1. Branch B writes regions with `texture_upload` and a rect-taking `write_presented`**, no new
  primitive. Cost if wrong: a pitch or origin error, which the off-origin, odd-width cells show.
- **L-1. A new launch `blit`** rather than more rows in `targets` (27 rows, 240 s budget). Cost: one more
  launch in every lane (about 2 minutes a full gate), script plumbing in Task 1.
- **P-1.** As Task 6 says: no probe unless a `clear` assert changed.

## Stated gaps

- A plain surface's upload after `ColorFill` under core (4b).
- `ColorFill` and `UpdateSurface` on a second device, and across `Reset` (C4c-6).
- `UpdateSurface` into a lockable target leaves its staging stale until the next lock, as on mtld3d; no cell
  holds a lock across it (a held lock is `INVALIDCALL` by the entry's own check).
- `StretchRect`'s `sr-shape` cells judge an answer, not pixels (4b, 4c).

## Appendix: the rows (launch `blit`)

Sizes are small (64x64 or 32x32 unless said); "RT" = `CreateRenderTarget` A8R8G8B8, "RT-tex" = a DEFAULT
texture with `D3DUSAGE_RENDERTARGET`, "plain" = `CreateOffscreenPlainSurface` DEFAULT, "sys" = the same in
SYSTEMMEM. Reads: `GetRenderTargetData` into a sys surface, or a lock where the kind locks.

| row | cells |
|---|---|
| `cf-rt` | a non-lockable RT: Clear to A while bound, `ColorFill` whole to B (read), `ColorFill` rect {5,7,18,16} to C, a small quad D drawn after over the rect's corner: outside = B, inside = C, the quad = D; the same rect fill with `SRGBWRITEENABLE` on: the bytes are the colour's (cell "srgb") |
| `cf-lockable` | a lockable RT: fill rect, `LockRect` READONLY reads it; lock-write a block, fill another rect, lock reads both |
| `cf-bb` | the back buffer bound: draw, fill rect, draw; read through the row helpers; the fill colour's alpha is not judged (X8) |
| `cf-unbound` | RT-tex X bound and drawn to (open pass), `ColorFill` of RT Y (not bound), another draw to X: X holds both draws and nothing of the fill, Y holds the fill |
| `cf-rtex` | RT-tex 64x64, 2 levels: a fill as the first use of the texture (level 0, rect); `LockRect` level 0 reads it; lock-write level 0 colour A, unlock, fill rect B, lock reads A outside and B inside; fill of level 1 whole, read by `GetRenderTargetData` of the level; a cube RT-tex face 3 |
| `cf-autogen` | an AUTOGENMIPMAP RT-tex 64x64: fill level 0 whole, draw a 4x4 quad sampling it with a POINT mip filter: the fill's colour (before the fill the same draw reads the cleared colour) |
| `cf-msaa` | a 4-sample RT: fill whole and a rect, resolve by `StretchRect` into a 1-sample RT (the supported shape), read |
| `cf-plain` | plain A8R8G8B8 and R5G6B5: fill whole, rect {4,4,8,8}, a rect hanging over the right edge (clamped), an inverted rect (`D3D_OK`, nothing changes); the lock reads the packed value |
| `cf-reject` | `INVALIDCALL`: null surface; a sys surface; a depth-stencil surface; a level of a DEFAULT texture with usage 0, of a DYNAMIC one, of a MANAGED one; a plain DXT1 surface with rect {5,5,7,7}. `D3D_OK`: a plain DXT1 surface with rect {4,4,8,8}; a plain YUY2 or UYVY surface where the format is available (skipped otherwise) |
| `us-tex` | sys 16x16 → a DEFAULT texture's level 0 (branch A), rect {1,2,14,13} to point (3,1); the texture drawn: texels inside and outside the region; again with another colour (a second upload) |
| `us-level` | a sys texture's level 0 and level 1 → a DEFAULT texture's levels (branch C), with a rect and a point; a cube texture's face 2; each drawn |
| `us-rtex` | sys → an RT-tex level: draw into it, update a region, draw a quad over part of the region: read by `GetRenderTargetData`: the three zones |
| `us-rt` | sys → a non-lockable RT and a lockable RT (branch B): draw, region {1,2,14,13} to point (5,3), draw; read (`GetRenderTargetData`; the lock for the lockable one) |
| `us-bb` | sys → the back buffer (branch B): draw, the same region and point, draw; read through the row helpers |
| `us-conv` | a sys R5G6B5 surface → an X8R8G8B8 RT (branch B) and → an A8R8G8B8 DEFAULT texture (branch A): the converted values (colours exact in 5-6-5) |
| `us-reject` | `INVALIDCALL`: null source, null destination; a locked source; a 4-sample RT destination; a DEFAULT source texture level; a MANAGED destination level; the source rect larger than the destination level; a DXT1 pair with a 2x2 rect of a 4x4 level; a source and a destination that are levels of one texture |
| `sr-reject` | `INVALIDCALL` in every lane: a flipped source rect; a destination rect past the edge; RT → plain; RT-tex level → a usage-0 DEFAULT texture level; a usage-0 level → plain; a sys surface as source and as destination; a MANAGED level as source; a depth-stencil surface → RT; RT → a depth-stencil surface; a depth pair of two sizes; a depth pair with a sub-rect; plain → plain scaled; one surface onto itself scaled; a 1-sample depth → a 4-sample depth |
| `sr-shape` | `D3D_OK` on mtld3d, strict `NOTAVAILABLE`: RT → RT whole, same format (exit 2); a depth-stencil pair of one format and size, whole (exit 1); plain A8R8G8B8 → plain R5G6B5 whole (the CPU-convert return, behind exit 2); RT-tex 32 → RT 64 scaled LINEAR (exit 2) |

## Review dispositions

(filled after the three reviews)

## Execution notes

(filled during execution)
