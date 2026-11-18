# DXMTRS C4c-4a: `ColorFill`, `UpdateSurface` and `StretchRect`'s validation on D3D9's core runtime

Slice 4 of C4c, first of three parts (ruling S-4 below). Process: the C4 program's (code map, this plan, three
adversarial reviews folded before code, rows first, one commit a feature, full gates once, one whole-branch
review, spec, pin, push). Map: `build/c4c/s4/map.md` (cited "map A1", "map C" ...). Sweeps:
`build/c4c/s3b/sweep.md` and `build/c4c/s3/sweep.md`. Reviews:
`build/c4c/s4/review-{1-design,2-core,3-exec}.md` (cited "R1 f3" = review 1, finding 3). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and rules:

- `IDirect3DDevice9::ColorFill` works on every surface mtld3d fills: the back buffer (an additional swap
  chain's too), a standalone render target (lockable or not, multisampled or not), a render-target texture's
  level or cube face (an AUTOGENMIPMAP one regenerates its chain), and a DEFAULT offscreen plain surface (on
  the CPU, as on mtld3d); every other surface answers mtld3d's `INVALIDCALL`, a null surface included;
- `IDirect3DDevice9::UpdateSurface` works whole: a SYSTEMMEM surface or texture level into a DEFAULT texture's
  level or cube face (CPU staging, uploaded at the next use, or at once for a render-target texture), and into a
  standalone render target or the back buffer (a region written in application order);
- `IDirect3DDevice9::StretchRect` answers mtld3d's `D3DERR_INVALIDCALL` for every call mtld3d rejects at its
  gates, before the refusal by name. A valid shape other than the multisample resolve is still refused
  (`D3DERR_NOTAVAILABLE`, the same literal): the copies are C4c-4b and 4c.

Wine's suites: `color_fill_test` (104 new failures under core), `test_flip`'s fills, the `UpdateSurface` tests
(`update_surface_test` 20, `add_dirty_rect_test` 13, `test_signed_formats` 18, `test_cube_wrap` 11,
`test_generate_mipmap` 1, `test_mipmap_lock` 2), and of `test_stretch_rect`'s 810 the 729 that want
`INVALIDCALL` (counted from the raw log: sites device.c:13393, 13405, 13434, 13437, 13447 and the "expected
0x8876086c" lines; the 81 that want `D3D_OK` are 4b's and 4c's). Those are the conformance runner's counts
("Test failed" lines); Wine's own summary for that function moves by 693, because 36 `todo_wine` lines start
to succeed and Wine counts them as failures (R3 f13).

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
  `cfg(feature = "core-runtime")` **and a test of the device being core**, ahead of code that stays as it is.
- **A core arm is keyed on the device being core, never on `core_target()` or `core_depth()` alone** (R1 f1,
  Critical): `core_target()` answers `Some(BackBuffer)` for every back buffer on either runtime
  (`surface.rs:711-713`), and the gates' default lanes run mtld3d on the build that has the feature
  (`DXMT_D3D9_CORE=1`), so `cfg` alone keeps nothing out.
- **No mtld3d recorder call may be reachable on a core device** (`push_control`, `capture_frame_bytes`,
  `schedule_upload`, `flush_current_frame_blocking`, `unix_call`): each panics there. **And no code below a
  core arm may read a `StretchKind`'s Metal handle**: on a core device it is null by construction (below).
- **One invariant kept (map G3): on a core device only render-target levels, standalone targets and the back
  buffer are ever written by the GPU.** A DEFAULT offscreen plain surface and a DEFAULT texture level that is no
  render target stay CPU-authoritative (their core texture is an upload of their staging), because the core
  cannot read a non-target texture back. 4a's `ColorFill` of a plain surface is a staging fill; 4b's
  plain-to-plain copy has to keep it.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree. **No implementer runs `dxmt/toolchain.sh`**; the compiler is
  `~/Documents/MacProton/build/dxmt-src/llvm-mingw-own/bin`.
- **Job scripts** live in `build/c4c/s4a/gates`, each copied from `build/c4c/s3b/gates` and edited **before**
  its first run; a job that has run is never edited and **a rerun is a new job name** (`ja2.sh`, `JT=jt3b`;
  `jt.sh` reads `J=${JT:?}`), so no run's evidence is overwritten (R3 f14); jobs test committed tips. Before
  any s4a job runs: `/usr/bin/grep -nE 'c4c/s3b|c4c3b|C4c-3b' build/c4c/s4a/gates/*.sh
  build/c4c/s4a/gates/*.txt` prints nothing, **and the positive checks hold** (R3 f2: a string guard cannot
  see a stale list): `blit` is in `jq.sh`'s and `jv0.sh`'s launch lists and `jv0.sh` no longer says `targets
  depth`; `jm.sh` and `jmb.sh` carry this plan's letter list; `jm.sh`'s `P` has one entry a letter;
  `jcr.sh`'s budget grep names `bt32` and `bt64`.
- **Every verdict counts its rows** (R3 f2): a launch that is not wired passes silently (`refusals_ok`'s
  default answer is success; an unknown mode runs the plain triangle). `ja`, each `jt`, `jr` and `jq` state the
  number of `blit` row lines judged (N, the Appendix's row count); a validation launch states "reports 0 and
  rows ok N+2" (`module` and `imports` print too; a launch that fell through prints 5); `jcr` and `jc` count
  the `bt64-fresh` and `bt32-fresh` ok lines.
- **Every `d3d9-check.sh` run of an s4a job sets `DXMT_CHECK_WORK`** to a folder of its own under the job's
  (not `$TMPDIR`; R3 f22).
- **A mutant names a feature-gated item only under `#[cfg(feature = "core-runtime")]`**, and every mutant is
  built once before the chain (`jmb`); the chain's `jm` reuses those builds.
- **Wanted values come from the base:** `base-key.txt` and `base-imports.txt` are copied from s3b (the range
  a1d45dd5..4eea8d68 left both unchanged: `jr` and `post` of C4c-3b printed "same"). If a fetch shows DXMTRS
  main moved, a `jb` at the new base writes them instead (R3 f24).
- **No `dxmt/check.sh` run may cross 03:35**, judged where `check.sh` starts, not where the chain does (R3
  f11: a chain queues behind `jmb`): `jr.sh` and `post.sh` read the clock right before `check.sh`, lock held,
  and wait until 03:36 when it reads 03:05 to 03:35.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller. d3d9-rt is an rlib: its primitives are their own commits (a changed signature carries its
  callers).
- One lane (default, core) per task that moves a row, where this plan names one; full gates once; no fix round
  for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8. Out, with where it goes:

- **C4c-4b:** `StretchRect`'s unscaled copies of one storage (target to target, the back buffer, plain to
  plain and plain or texture into a target, the same-texture routes, the depth copy), and the CPU conversion
  into a plain surface. **C4c-4c:** the scaled, converting and YUV copies (a sampled quad), multisampled
  destinations, the depth resolve.
- **Valid `StretchRect` shapes that look like the resolve and stay refused** (R1 f6; `resolve_shape` is
  unchanged): an additional swap chain's back buffer at either end, an AUTOGENMIPMAP destination, a pair
  `copies_bytes` refuses (X8R8G8B8 into A8R8G8B8), a rect inside the surface that is not the whole of it,
  multisampled into multisampled, the multisampled depth resolve. The sweep counts them as 4b's or 4c's.
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
target the core resolves, bound or not, multisampled or not. The view is `self.resolve(target)`'s, which is
the stored format's (R2 f8: never an sRGB twin; for a bound target it is what `clear(slot, rgba, false)` emits
today, and what mtld3d's fill writes, `unix/encoder.rs:4899`), and `rgba` is stored as given. `None` = the
whole view: `close_pass`, `enc.clear_color` (it folds into a later pass only on the identical view, else runs
alone, ahead of every reader of its subresource: R2 Q1); `Some([x, y, w, h])`: `close_pass`, the quad clear
(`with_clear_rtv`, one rect). `note_written(target, format)` as a Clear does. False, and nothing recorded,
when the target does not resolve, or the rect is empty or leaves the target: **the rect is in the texels of
the target's level and is checked against `shape(target)` with `checked_add`**, as `rect_bytes` checks one
(R2 f5). `clear` and `clear_rects` keep what they emit and share the two emit bodies with `fill`: one private
helper each for "whole view" and "rects of a view", **each the `emit` of the closure alone, moved textually**;
the three callers keep their own note and **their own `close_pass()`** (R2 f10); rects go through the existing
`scissor_rects`. The draws' state is not touched: the quad clear takes none of it, the bound targets and
depth are unchanged, and the next draw reopens its pass with Load on every slot (R2 Q1).

**`write_presented` takes a rect** (`textures.rs:500`): `write_presented(rect: DirtyRect, bytes, pitch)`.
`rect_bytes(shape, rect, ...)` already validates a rect inside the back buffer; the copy's origin and size
are the rect's; `bytes` starts at the rect's first texel, rows `pitch` apart, as `read_presented`'s `dst`
(the doc says so). Its one caller (`backbuffer_page_upload`, `surface.rs:3177`) passes `DirtyRect::full` and
changes in the same commit with the six test calls (R2 f6, R3 f16). A multisampled back buffer is still
false.

**`texture_upload` refuses a multisampled texture** (R2 f4): `if stored.desc.samples > 1 { return false; }`,
as `write_presented`, `read_back` and `depth_upload` refuse one. Until now only the front end kept one out.

Nothing else: a standalone target's region goes through `texture_upload` (3a's `core_lockable_rt_upload`
uses it whole; a region is the same call), and a texture level's through `core_flush_dirty`.

### Front end

**The classifier: `resolve_stretch_surface` learns a core device's standalone surfaces** (`device.rs:10115`).
Texture-backed endpoints already classify on core (their arm reads the texture, no handle). The two
standalone arms test a Metal handle, null on core, so today a core back buffer, render target and
depth-stencil answer `None` (a silent `INVALIDCALL`). The function takes **`core: bool`**
(`resolve_stretch_surface(surf, core)`; `dev.is_core()` from its three callers, `device.rs:9432`, `:9441`,
`:10402`; a constant `false` in a build without the feature). The order (R1 f1, Q1): **the texture arm
first** (required: `core_target()` is `Some` for a render-target texture's level and `core_depth()` for a
depth texture's, and a texture-backed surface's standalone extent is 0); then, under the feature's `cfg` and
`core`:

- `s.core_target().is_some()` (a back buffer, a standalone target) → the colour arm's info with
  `kind: StretchKind::Backbuffer(MetalHandle::NULL)`, `msaa` and `msaa_srgb` null, the rest as mtld3d
  (`standalone_width/height/format`, `render_scale()`, pool DEFAULT, `IS_RENDER_TARGET`,
  `sample_count` from `multi_sample()`);
- `s.core_depth().is_some()` → the depth arm's info with `kind: StretchKind::DepthStencil(MetalHandle::NULL)`,
  `IS_DEPTH_STENCIL`, `sample_count`;

then mtld3d's two arms as they are. A SYSTEMMEM or SCRATCH plain surface is `None` on both runtimes; a depth
texture's level takes the texture arm on both (no flag). R1 checked each surface kind of map D against this
(its Q1 table). **The default lanes are the keying rule's gate** (they run mtld3d on the feature's build):
`targets`' `rt-msaa` resolve into the back buffer, and `cf-bb`, `us-bb` and `sr-shape`'s back-buffer cells.

**`StretchRect`.** The early jump at `device.rs:9411-9414` goes. A core device runs mtld3d's gates (map A1
steps 3-11, all pure: the infos, `class_reject`, `check_stretch_rect_formats`, `parse_stretch_regions`, the
same-surface, planar and scaling rules) and leaves at **two exits**, each `#[cfg(feature = "core-runtime")]
if dev.is_core() { return core_arms::stretch_rect_resolve(this, [src, dst], [src_rect, dst_rect]); }`:

1. in the depth branch, after its last gate and **before** `flush_dirty_mips_for_gpu_write` (`:9521`);
2. after the scaling gate and **before** `flush_dirty_mips_for_gpu_write` (`:9703`), which is ahead of the
   CPU-convert return (`:9732`) and of the tail's `push_control`.

`stretch_rect_resolve` is unchanged: the resolve shape is recorded, anything else is the logged refusal and
`D3DERR_NOTAVAILABLE`. R1 traced every gate ahead of the exits: none reads a handle or records, no path
reaches `push_control` or the CPU convert around an exit, `frame_dump` is inert on core, and `resolve_shape`
behind the gates accepts exactly what it accepted before. **One class of answer differs** (R1 f5): the
failures mtld3d finds while running, behind the exits (a failed read or conversion in the CPU route, the depth
branch's missing-texture `else`), are not reached on core; the call is refused by name.

**`ColorFill`.** The refusal at `:10380` goes; the gates run as they are (map A2 steps 2-7; the classifier
above makes step 3 right on core). Then, under the feature's `cfg` and `dev.is_core()`, ahead of `:10440`:

- **a render target** (`IS_RENDER_TARGET`): `core_arms::color_fill(dev, surf, &info, region, color)`:
  the target is `surf.core_target()`; a texture-backed one first `core_flush_dirty(parent, core)` (its core
  texture exists from here with every CPU write uploaded; the arm does not call
  `flush_dirty_mips_for_gpu_write`, as `resolve_shape` and `GetRenderTargetData` do not, R1 f9);
  `core.fill(target, rect, d3dcolor_to_rgba_f32(color))`, `rect = None` when the region is the whole surface,
  **the colour as given whatever `D3DRS_SRGBWRITEENABLE` says**; then, **only when the fill answered true**
  (R1 f8), `claim_dst_subresource_for_gpu` for a texture-backed target (pure state: needed for a cube face,
  whose lock has no claim of its own, `texture.rs:7160-7169`; redundant for a 2D level, whose lock and `GetDC`
  claim themselves, `texture.rs:4793`, `:1011`; kept for both, as mtld3d has it) and
  `core.texture_generate_mips(id)` when `info.autogen_texture_id` is set. A false `fill` logs one error line
  and answers `D3DERR_INVALIDCALL` (never a silent `D3D_OK`); the line's wording holds none of the
  can't-present substrings `d3d9-check.sh:127` fails a launch on (R3 f21). A lockable target's staging is
  stale after it, as on mtld3d: 3a's lock and 3b's `GetDC` read the target back.
- **a DEFAULT offscreen plain surface**: `color_fill_offscreen_plain` runs to `fill_staging_region`; where
  mtld3d calls `schedule_upload` a core device marks the filled rect dirty (`ti.mark_mip_dirty_rect(level,
  region)`) and the snapshot dirty (`dev.mark_snapshot_dirty_all()`); `core_flush_dirty` uploads it when the
  texture is next flushed (R1 f11). **No row can see that upload in 4a** (a plain surface is never sampled and
  its `StretchRect` out is 4b): the lock reads the staging. Stated gap; 4b's plain-source row is its gate.

**`UpdateSurface`.** The refusal at `:8454` goes. Branches A and C (map A3) are CPU staging copies and
`publish_staging_write`, which is armed since C4b-3d1: unchanged (R1 Q5: no recorder call and no null-handle
branch for any destination; a render-target destination uploads its pending rect at once, any other at its
next bind). Branch B's entry test (`dst_parent.is_null() && !dst_surf.metal_color_handle().is_null()`,
`:8620`) gains, under the feature's `cfg`, `|| (core && dst_surf.core_target().is_some())` with `core` the
device's `is_core()` (R1 f1: the bare test is true for an mtld3d back buffer). In
`update_surface_into_color_target` the ten rejects are pure and stay; before `dst_surf.render_scale()`
(`:8803`) a core device leaves through
`core_arms::update_color_target(dev, dst_surf, origin, (region.w, region.h), &rows, row_bytes)`:

- the back buffer (`TargetRef::BackBuffer`): `core.write_presented(DirtyRect{origin, extent}, &rows,
  row_bytes)`;
- a standalone target (`TargetRef::Texture{id, ..}`): `core.texture_upload(id, UploadRegion{level 0, slice 0,
  x, y, z 0, w, h, depth 1}, &rows, row_bytes, row_bytes * h)`.

False logs one error line and answers `D3DERR_INVALIDCALL`. The rows are the tight vector mtld3d builds
(converted when the formats differ), `row_bytes` apart from the region's first texel, which is what both
primitives read (R1 Q5); no core target or back buffer is ever stored widened (`device.rs:7907-7923`,
`direct3d9.rs:2350-2356`), so reject 6 is inert there. `UpdateSurface` never calls the classifier.

**Refusal literals.** Removed: `IDirect3DDevice9::ColorFill`, `IDirect3DDevice9::UpdateSurface`. Kept:
`IDirect3DDevice9::StretchRect (other than a multisample resolve)`. New: none.

## Review Focus (for the final review)

- No recorder call reachable on a core device from the three entries; no read of a null handle below an arm.
- Every hunk in a function mtld3d runs is gated by `cfg` **and the device being core**; mtld3d's order of
  answers unchanged; nothing keyed on `core_target()`/`core_depth()` alone.
- The classifier's arms against every surface kind of map D, on both runtimes.
- `fill`: the view's format, the stored colour, pass state after it, a multisampled target, the back buffer,
  a level and a face.
- The region writes: origin, extent, pitch, order against draws before and after.
- The invariant above (who is GPU-authoritative on core).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`blit`** in `dxmt/tests/d3d9_triangle.cpp` (rows in the Appendix), wired in every place
`buffers` is (R3 f2's table): the header prose, `main`'s mode flag and dispatch, `blitRows` with its
in-program budget (180 s); `dxmt/d3d9-check.sh`: a `launch ours fresh blit 240 blit $strict` line, a `check
blit` row list, **a `blit` case in `refusals_ok`** wanting exactly `stretch_refusals` (both the outer and the
inner label), the header prose; `vm/dxmt/d3d9-gate.sh`: the row loop, the launches **`bt64-fresh bt32-fresh`**
in the `runtime ... stretch` loop (R3 f1, Critical: `bl64`/`bl32` are the blend launch's names), prose;
`vm/tests/d3d9.sh`: the two `tri` lines (240 s) and prose. `sh -n` on the three scripts. The device is `main`'s
(256x256 X8R8G8B8 windowed, no auto depth-stencil); a row that needs a depth-stencil surface makes one.

**Rules for every new cell:**
- a lock is released before its pixels are judged; every drawn-pixel cell runs in `until()` with its objects
  made inside the attempt; colours have every channel non-zero and are pairwise distinct within a row, **and
  every GPU fill colour has a channel in 0x10..0xf0** (R1 f7: an sRGB encode leaves 0 and 255 alone); no cell
  judges an out structure after a failed call;
- **every region write (`UpdateSurface`, a rect `ColorFill` of a target) uses a rect off the origin, a
  destination point different from the source origin, and a width that is no multiple of 4**; exceptions,
  named: the DXT1 cells and Wine's `{4,4,8,8}` (block rules);
- **every GPU write cell has a draw recorded before it and one after it on the same target** whose pixels pin
  the order; exceptions, named: `cf-rtex`'s first-use cell (nothing precedes it by design; it judges texels
  **inside** the rect only, R1 f14), cells whose earlier content is a CPU lock-write, and `cf-plain` (CPU);
- **`sr-*` rows call neither `ColorFill` nor `UpdateSurface`, and `cf-*` rows do not call `UpdateSurface`**
  (R3 f10: the lanes' predicted tokens depend on it); `us-tex` gives its destination a known content by a
  whole `UpdateSurface` of another colour first;
- a cell that wants `INVALIDCALL` wants it in every lane. An `sr-shape` cell is spelled
  `if (hr != (strictMode ? D3DERR_NOTAVAILABLE : D3D_OK)) return fail("StretchRect (<shape>)", hr);` (R3 f12:
  `rt-msaa`'s last cell accepts any answer outside strict, which is weaker); its pixels are judged in 4b and
  4c, not here;
- a format that a lane may lack is asked with `CheckDeviceFormat`; a skipped cell prints `d3d9 <row>-<what>
  skip` and the ledger says which lanes skipped (R3 f20).

Job scripts in `build/c4c/s4a/gates` from s3b's (the guards above; state names `c4c4a-*`): `head.sh`,
`ctl.sh` (`ja`, `jv0`, `jcr`), `ja.sh`, `jv0.sh` (the `blit` launch alone), `jcr.sh`, `jt.sh`, `jq.sh`
(eleven launches), `jr.sh`, `jc.sh`, `jw.sh`, `jmb.sh`, `jm.sh` (**`P` holds verdict tokens**, several a
letter, R3 f3), `post.sh`, `chain.sh` (`jr jc jq jm jw`).

Controls at the base + the rows, each failing set and each row count written in the ledger before its run:
- `ja`: default and msl: **every row green on mtld3d at the first run, no expectation corrected** (a row red
  on mtld3d is a wrong row or a finding: fixed or dropped before any code, and the ledger says which; a
  `ColorFill` of a 4-sample target and DXT1 plain surfaces are first exercised here). Core, the failing tokens
  predicted (R3 derived the same): `blit-runtime` (three refusal lines where one is wanted), every `blit-cf-*`,
  every `blit-us-*`, `blit-sr-reject`, and nothing else (`blit-sr-shape` is green at the base: the early jump
  already answers `NOTAVAILABLE`).
- `jv0`: the `blit` launch on mtld3d under Metal's validation layer: reports 0, rows ok N+2.
- `jcr`: the VM's default lane on old and new WoW64, the `bt64`/`bt32` ok lines counted.

Commit: "dxmt, vm: d3d9_triangle's blit launch: rows for ColorFill, UpdateSurface and StretchRect's answers
(DXMTRS C4c-4a)".

### Task 2: `fill`, the rect of `write_presented`, `texture_upload`'s refusal (d3d9-rt)

As "d3d9-rt" above; two commits. Tests on the fakes as they are (R2 f1, f2: what they can and cannot show):

- `fill`, whole of an **unbound** target Y while a pass on X is open (draw on X, `fill(Y)`, draw on X):
  `!render_open` right after the fill; the Clear pass on Y's handle with its `clear=` colour **as given**;
  every pass on X is `load=1 store=1` with no clear line and holds two draws in total; no other pass on Y. (The
  two X passes may merge at flush: nothing asserts a pass count.) The same with a rect fill (the quad's pass
  never merges).
- `fill` of a rect: one `render 36`, `target0` the view at the level's size, `color0 load=1 store=1`, the
  fragment bytes' length; **the fakes print no scissor coordinates and no quad colour**: the coordinates are
  pinned by a new unit test of the pure `scissor_rects` (`[x, y, w, h]` to the scissor's fields) and by the GPU
  rows.
- a target with an sRGB twin: a whole fill's clear line is the given colour on the plain view; a rect fill
  attaches the texture's own handle and no sRGB view is made.
- the back buffer; a multisampled target (whole: the multisampled texture's handle; rect: `samples=4` on its
  pass); **level 1** (`fill(level 1, Some(rect))` on the `mip=1` view; a rect inside level 0 and past level 1
  is false with no line); **a cube face** (`fill(face 3, None)` on the `slice=3` view) (R2 f5).
- an empty rect, a rect past the edge and an unknown id answer false with no line.
- order (R2 f7): `fill(level 0)` then `texture_generate_mips`: the Clear's pass before the mipmap blit;
  `fill(Y)`, `texture_upload` into Y, a draw on Y: the Clear, the blit, then a pass that loads.
- `clear`'s and `clear_rects`'s existing asserts stay untouched (the list is R2 f1's).
- `write_presented`: a rect off the origin with an odd width: the copy line's origin, size and
  `bytes_per_row = w * 4`, the next draw's pass at `load=1`; a rect past the edge false with no line; 3b's
  tests pass the whole rect. `texture_upload` into a multisampled target records nothing.

Commits: "d3d9: a fill of any colour target on the core runtime"; "d3d9: write_presented takes a rect, and
an upload refuses a multisampled texture" (with `surface.rs:3177`).

### Task 3: the classifier and `StretchRect`'s answers (front end)

`resolve_stretch_surface(surf, core)` with its two arms, the early jump removed, the two exits;
`core_arms.rs`'s module doc and `stretch_rect_resolve`'s doc say the gates ran. `ColorFill` and
`UpdateSurface` stay refused. Lane `jt3` (default, core): `sr-reject` and `sr-shape` green strict under core,
`targets` and `msaa` whole (their one refusal line each: R3 traced every call in them); failing tokens
predicted: every `blit-cf-*`, every `blit-us-*`, `blit-runtime`; default: all green. Commit: "d3d9:
StretchRect answers mtld3d's INVALIDCALL on the core runtime".

### Task 4: `ColorFill` (front end)

The refusal removed, `core_arms::color_fill`, the plain surface's marks. Lane `jt4` (default, core): every
`cf-*` row green strict; failing tokens predicted: every `blit-us-*`, `blit-runtime` (two refusal lines).
Commit: "d3d9: ColorFill on the core runtime".

### Task 5: `UpdateSurface` (front end)

The refusal removed, branch B's entry test, `core_arms::update_color_target`. Lane `jt5` (default, core) at
the tip: every row of every launch green strict under core, every launch's refusal set exact (`blit`,
`targets`, `msaa`: the one `StretchRect` line each), **and the `blit` launch once under core with Metal's
validation layer** (reports 0, rows ok N+2: the quad clear on unbound and multisampled views, on a level and
a face, and the region blits are legal Metal). Commit: "d3d9: UpdateSurface on the core runtime".

### Task 6: gates

- `jmb` before the chain: every mutant builds.
- `jr`, `jc`, `jq` as C4c-3b's (`jq`: eleven core launches under validation, 0 reports, each with its row
  count).
- `jm`: mutants as commits off the tip, each red on exactly its named tokens; `gates/mutants-expect.txt`
  names, before the chain runs and from the rows as written, the cell and the texel or answer that changes:
  a. `blit-sr-reject`: a core device leaves `StretchRect` for the resolve arm right after the filter check
     (the base's jump) → the first `INVALIDCALL` cell answers `NOTAVAILABLE`;
  b. `blit-sr-reject`: the classifier's core depth arm reports one sample → the "1-sample depth into a
     4-sample depth" cell is an equal pair, reaches exit 1 and answers `NOTAVAILABLE` (R1 f3: "the arm answers
     `None`" would also redden `msaa-ms-desc` and `msaa-runtime`; the arm's existence is gated by `sr-shape`'s
     depth cell and `ms-desc`);
  c. `blit-cf-rt,blit-cf-lockable,blit-cf-bb,blit-cf-rtex,blit-cf-msaa` (and `blit-cf-unbound` if its fill is
     a rect): `color_fill` drops its rect (the whole target filled) → each row's texel outside its rect (R3
     f4);
  d. `blit-cf-rt`: `color_fill` hands `fill` the sRGB-encoded colour when `D3DRS_SRGBWRITEENABLE` is set →
     the "srgb" cell's bytes (R3 f5: a twin-view mutation in d3d9-rt would redden every fill row; the plain
     view is Task 2's fake test's to hold);
  e. `blit-cf-rtex`: the texture arm skips `claim_dst_subresource_for_gpu` → the cube face's `LockRect` after
     its fill reads the staging's older colour (R1 f2, R3 f6: equivalent on a 2D level);
  f. `blit-cf-rtex`: the texture arm skips `core_flush_dirty` → the first-use fill finds no core texture: the
     answer `INVALIDCALL`;
  g. `blit-cf-autogen`: no `texture_generate_mips` → the last draw's texel reads the old chain;
  h. `blit-us-rt,blit-us-conv`: `update_color_target`'s texture arm writes at (0, 0) → the texel at each
     destination point (R3 f7);
  i. `blit-us-bb`: the back-buffer arm writes at (0, 0) → the texel at the destination point;
  j. `blit-sr-shape`: exit 2 placed below the CPU-convert return → the plain A8R8G8B8 into plain R5G6B5 cell
     answers `D3D_OK` where strict wants `NOTAVAILABLE` (on a device that expands packed 16-bit formats the
     call would panic instead; this Mac's does not: the ledger records which before `jmb`).
  Not gated by a mutant, stated: `fill` leaving the pass open (Task 2's fake test asserts `!render_open`, and
  in the unit tests' build the missing `close_pass` aborts in dxmt-core's `begin`; as a row mutant it would
  redden most `cf-*` rows, R2 f3, R3 f8); the keying on the device (a default-lane fault: `cf-bb`, `us-bb`,
  `rt-msaa` and `sr-shape`'s back-buffer cells in every default lane); the plain surface's dirty marks (no row
  sees the upload until 4b); branches A and C of `UpdateSurface` (no core code: their rows gate the armed
  helpers); the classifier's colour arm (every `cf-*` cell on a standalone target or the back buffer, every
  resolve of `msaa` and `targets`, and `sr-shape`'s target cells fail without it; R1 f4).
- `jw` (gates nothing): the sweep → `build/c4c/s4a/sweep.md`: per function the runner's new failures before
  and after (`color_fill_test`, `test_flip`, the six `UpdateSurface` functions, `test_stretch_rect` predicted
  810 → 81, `stretchrect_test`, `depth_blit_test`, `test_multisample_stretch_rect`), suite totals in Wine's own
  summary count, each number with its unit; the refusal lines met. It also corrects 3a's per-call figures
  (`test_cube_wrap` is `UpdateSurface`'s, `test_filling_convention` is `StretchRect`'s, `test_sysmem_draw` is
  `ProcessVertices`'s; checked against the raw log).
- **Pre-ruled, no rerun for them** (memory): the VM or Mac `visual` count 211159 with failed 29 and
  `visual.c:25196` at 0; "fence-order 1 0"; "presents to a minimised window" when HID idle time at the failure
  is under ten minutes. A build that died on a code-signing timestamp is run again (a new job name). Anything
  else red is investigated before the push.
- **Ruling P-1, the probe.** The range touches `rt/src/device/recorder.rs`. No probe is run if (a) every
  existing `clear` and `clear_rects` test (R2 f1's list) is untouched in the diff, and (b) the final review
  confirms the two helpers are textual moves of the closures with each caller's own `close_pass`. Otherwise
  one bench pair, once.
- No D3D10-12 gate while `git diff --stat <base>..<tip>` shows no file outside `src/d3d9`.

### Task 7: review

One whole-branch Opus review (`build/c4c/s4a/review-final.md`), given the diff and this plan; Critical and
Important fixed with a red cell first where a pixel can show it; Minors fixed if one line, else ledgered.

### Task 8: spec, pin, push

Spec §5 paragraph "C4c-4a" and the slice list (4 splits in 4a, 4b, 4c). Plan execution notes. `dxmt/pins`.
Privacy scan of both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron (the SHA
written out: zsh reads `$T:r` as a modifier). Post-push check. CI. Memory. Scratch deleted by full path, each
once its verdict is ledgered: `build/c4c/s4a/gates/{ja,jv0,jt3,jt4,jt5,jq,jr}/work*` and any other
`DXMT_CHECK_WORK` folder a job made (about 3.7 GiB each; R3 f15).

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** none. **A lint
  suppression:** none. **A derive:** none.
- **New state:** none on the device, a surface or a texture (the dirty marks, `level_authority` and the
  snapshot flag are mtld3d's).
- **New pub items:** `CoreDevice::fill`; `core_arms::color_fill`, `core_arms::update_color_target` (as the
  module's others). **Changed signatures:** `write_presented` (a rect); `resolve_stretch_surface` (`core`).
  **Unsafe** (R1 f10): one deref of the destination's parent texture in `core_arms::color_fill` (and of the
  surface if the arm takes the raw pointer), each with a SAFETY comment as `resolve_shape`'s, one operation a
  block.
- **A duplicate:** none meant. `fill` shares `clear`'s and `clear_rects`'s emit bodies; the classifier is
  mtld3d's function with two arms, not a second validator; the region rows are mtld3d's vector; the plain
  surface's fill is mtld3d's `fill_staging_region`.
- **Not silent:** a `fill` or a region write that answers false logs an error in its arm and answers
  `INVALIDCALL`; the refusal keeps its name; the gates fail on an unexpected refusal line. **One silent case
  stated** (R2 f9): a quad clear whose pipeline Metal refuses records nothing on the encode thread after `fill`
  answered true, as today's `clear_rects`.
- **Divergence added:** none meant. mtld3d's fill of a render target and its region update are ordered among
  the passes, as the core's are. **Answers that differ from mtld3d** (R1 Q8): a valid `StretchRect` other than
  the accepted resolve is `NOTAVAILABLE`; mtld3d's run-time `INVALIDCALL`s behind the exits are not reached.
  **Kept, mtld3d's and now the core's too:** `ColorFill` clamps its rect and answers `D3D_OK` for an empty
  one; a format with no fill encoding on a plain surface is `D3D_OK`, unfilled; `StretchRect` has no scene
  check; a lockable target's staging is stale after a GPU fill or a region write until its next lock.
- **Docs in the same change:** DXMTRS's `STATUS.md`, `COVERAGE.md`, `CONFORMANCE.md` and `mtld3d.conf` say
  nothing of the core runtime: no edit owed; Task 8 confirms it against the final diff.

## Rulings (planning)

- **S-4. C4c-4 is three slices.** 4a: what needs no GPU-to-GPU copy (this plan). 4b: unscaled copies of one
  storage (a texture-to-texture blit primitive; CPU routes for plain surfaces). 4c: the sampled quad (scaled,
  converting, YUV), multisampled destinations, the depth resolve. Why: each needs one new kind of primitive, and
  each moves a nameable set of Wine's tests. Cost if wrong: one more push and gate round than a two-way split.
- **V-1. `StretchRect`'s validation is mtld3d's own gates run on core**, with the classifier's two arms keyed
  on the device, not a second validator in `core_arms`. Why: one order of answers on both runtimes. Cost if
  wrong: a gate that reads something a core surface lacks misanswers silently; R1's kind-by-kind trace,
  `sr-reject` and `sr-shape`'s cell per endpoint kind are the guard.
- **V-2. A valid `StretchRect` other than the resolve stays refused by its old literal.** The exact-one refusal
  counts of `targets` and `msaa` hold (their two strict cells are valid shapes; R3 traced every call of both
  launches), and `blit` joins the set. Cost if wrong: a count gate fails in `ja`/`jt3`, cheaply.
- **F-1. `fill` uses the plain view and stores the colour as given.** Why: mtld3d's fill is verbatim bytes.
  Cost if wrong: an sRGB target's fill is off by the transfer curve; the fake test (view) and `cf-rt`'s "srgb"
  cell with mutant d (state) see it.
- **F-2. A plain surface's `ColorFill` stays on the CPU and only marks the level** (the invariant). Cost if
  wrong: none seen in 4a (the upload is unobservable); 4b gates it.
- **U-1. Branch B writes regions with `texture_upload` and a rect-taking `write_presented`**, no new
  primitive. Cost if wrong: a pitch or origin error, which the off-origin, odd-width cells show.
- **L-1. A new launch `blit`** rather than more rows in `targets` (27 rows, 240 s budget). Cost: one more
  launch in every lane (the chain about 100-110 minutes where 3b's took 87), script plumbing in Task 1, and
  the silent-wiring hazard the row counts answer.
- **M-1. Mutant j is the exit's placement, not the open pass** (R2 f3, R3 f8): the pass mutant reddens most
  fill rows and the unit test already aborts on it. Cost if wrong: none; a gate exists either way.
- **P-1.** As Task 6 says.

## Stated gaps

- A plain surface's upload after `ColorFill` under core (4b).
- `ColorFill` and `UpdateSurface` on a second device, and across `Reset` (C4c-6).
- `UpdateSurface` into a lockable target leaves its staging stale until the next lock, as on mtld3d; no cell
  holds a lock across it (a held lock is `INVALIDCALL` by the entry's own check).
- `StretchRect`'s `sr-shape` cells judge an answer, not pixels (4b, 4c).
- A quad clear whose pipeline Metal refuses (Rules check).
- The Metal rules R2 marked as assumptions (a Clear pass on a multisampled private texture, a blit into a
  render-target texture, the clear colour's channel order on BGRA storage): held by the validation launches
  and the rows' pixels, not by reading.

## Appendix: the rows (launch `blit`)

Sizes are small (64x64 or 32x32 unless said); "RT" = `CreateRenderTarget` A8R8G8B8, "RT-tex" = a DEFAULT
texture with `D3DUSAGE_RENDERTARGET`, "plain" = `CreateOffscreenPlainSurface` DEFAULT, "sys" = the same in
SYSTEMMEM. Reads: `GetRenderTargetData` into a sys surface, or a lock where the kind locks (existing helpers:
`readTarget`, `texel`, `want`, `rtHas`, `underLock`, `underLevelLock`, `k565`, `msHas`; R3 Q4).

| row | cells |
|---|---|
| `cf-rt` | a non-lockable RT: Clear to A while bound, `ColorFill` whole to B (read), a draw, `ColorFill` rect {5,7,18,16} to C, a small quad D drawn after over the rect's corner: outside = B (and the first draw), inside = C, the quad = D; the same rect fill with `SRGBWRITEENABLE` on: the bytes are the colour's (cell "srgb") |
| `cf-lockable` | a lockable RT: fill rect, `LockRect` READONLY reads it; lock-write a block, fill another rect, lock reads both |
| `cf-bb` | the back buffer bound: draw, fill rect, draw; read through the row helpers; alpha is not judged (X8); then an additional swap chain's back buffer (`CreateAdditionalSwapChain`, `GetBackBuffer`, as `rt-chain`): a rect fill of it shows in the device's back buffer |
| `cf-unbound` | RT-tex X bound and drawn to (open pass), `ColorFill` of RT Y (not bound) whole, another draw to X: X holds both draws and nothing of the fill, Y holds the fill |
| `cf-rtex` | RT-tex 64x64, 2 levels: a rect fill as the first use of the texture (judged inside the rect only); lock-write level 0 colour A, unlock, fill rect B, the lock reads A outside and B inside; a fill of level 1 whole, read by `GetRenderTargetData` of the level; a cube RT-tex: lock-write face 3 colour A, fill face 3 whole with B, **`LockRect` READONLY of face 3 reads B** (mutant e's cell) |
| `cf-autogen` | an AUTOGENMIPMAP RT-tex 64x64, in this order (R1 f13): bind the texture, Clear, bind the back buffer (the chain regenerates), draw a 4x4 quad sampling it with a POINT mip filter (the cleared colour), `ColorFill` level 0 whole, the same draw again (the fill's colour), with no `SetRenderTarget` naming the texture between the fill and the last draw |
| `cf-msaa` | a 4-sample RT: fill whole and a rect, resolve by `StretchRect` into a 1-sample RT (the supported shape), read |
| `cf-plain` | plain A8R8G8B8 and R5G6B5: fill whole, rect {4,4,8,8}, a rect hanging over the right edge (clamped), an inverted rect (`D3D_OK`, nothing changes); the lock reads the packed value |
| `cf-reject` | `INVALIDCALL`: null surface; a sys surface; a depth-stencil surface; a level of a DEFAULT texture with usage 0, of a DYNAMIC one, of a MANAGED one; a plain DXT1 surface with rect {5,5,7,7}. `D3D_OK`: a plain DXT1 surface with rect {4,4,8,8}; a plain YUY2 or UYVY surface where `CheckDeviceFormat` offers it (else the skip line) |
| `us-tex` | sys 16x16 → a DEFAULT texture's level 0 (branch A): a whole update with colour A first, the texture drawn; then rect {1,2,14,13} to point (3,1) with colour B, drawn again: B inside the region, A outside (a second, dirty upload) |
| `us-level` | a sys texture's level 0 and level 1 → a DEFAULT texture's levels (branch C), with a rect and a point; a cube texture's face 2; **each destination drawn once before its update and once after** (R3 f18: the update arrives as a dirty upload) |
| `us-rtex` | sys → an RT-tex level: draw into it, update a region, draw a quad over part of the region; read by `GetRenderTargetData`: the three zones |
| `us-rt` | sys → a non-lockable RT and a lockable RT (branch B): draw, region {1,2,14,13} to point (5,3), draw; read (`GetRenderTargetData`; the lock for the lockable one) |
| `us-bb` | sys → the back buffer (branch B): draw, the same region and point, draw; read through the row helpers |
| `us-conv` | a sys R5G6B5 surface → an X8R8G8B8 RT (branch B) and → an A8R8G8B8 DEFAULT texture (branch A), each with a rect and a point; the expected 8-bit values are the codec's bit replication of the 5-6-5 fields (`expand5`/`expand6`, `core/src/pixel_convert.rs:265-272`; 0x1f widens to 0xff), R1 f15 |
| `us-reject` | `INVALIDCALL`: null source, null destination; a locked source; a 4-sample RT destination; a DEFAULT source texture level; a MANAGED destination level; the source rect larger than the destination level; a DXT1 pair with a 2x2 rect of a 4x4 level; a source and a destination that are levels of one texture |
| `sr-reject` | `INVALIDCALL` in every lane: a flipped source rect; a destination rect past the edge; RT → plain; RT-tex level → a usage-0 DEFAULT texture level; a usage-0 level → plain; a sys surface as source and as destination; a MANAGED level as source; a depth-stencil surface → RT; RT → a depth-stencil surface; a depth pair of two sizes; a depth pair with a sub-rect; a 1-sample depth → a 4-sample depth (mutant b's cell); plain → plain scaled; one surface onto itself scaled |
| `sr-shape` | each `D3D_OK` on mtld3d, strict `NOTAVAILABLE`, **in this order** (R3 f3: a target cell logs the refusal before the depth cell): RT → RT whole, same format; the back buffer → RT with rect {0,0,64,64} and RT → the back buffer with the same destination rect; a lockable RT → RT; RT → an additional swap chain's back buffer (64x64 rects); RT → an RT-tex cube face; plain → RT; RT-tex level → RT-tex level; RT-tex 32 → RT 64 scaled LINEAR; plain A8R8G8B8 → plain R5G6B5 whole (the CPU-convert return, mutant j's cell); a depth-stencil pair of one format and size, whole (exit 1) (R3 f9: one cell an endpoint kind) |

N = 18 rows.

## Review dispositions

Three Opus reviews of the plan as committed at bff44181; every finding folded above unless said.

- **R1 (design)**: f1 Critical (the core arms keyed on `core_target()` alone fire for an mtld3d back buffer on
  the feature's build: `ColorFill` of it would answer `D3D_OK` and fill nothing) → the classifier takes
  `core`, branch B's term too, a Global Constraint, the default lanes named as its gate. f2 (mutant e
  equivalent on a 2D level) → the cube-face lock cell. f3 (mutant b reddens `msaa`) → b narrowed to the sample
  count. f4-f15 Minor, all folded (the `us-*` wording, the run-time answers behind the exits, the refused
  resolve-like shapes, a mid channel for the sRGB cell, the claim after a true fill, the unsafe count, the
  marks' wording, one caller, `cf-autogen`'s order, the first-use cell's judged texels, `us-conv`'s bytes).
- **R2 (core)**: nothing Critical. f1 (the fakes print neither the quad's coordinates nor its colour) →
  Task 2's asserts rewritten, a `scissor_rects` unit test, P-1's two conditions. f2 (the X passes merge) →
  the unbound test asserts nothing about pass counts. f10 (mutant d reddens every fill row) → d moved to the
  front end. f3 (mutant j predictable) → the pass mutant dropped for the exit's placement (M-1). f4
  (`texture_upload` and a multisampled texture) → the refusal added. f5-f9 folded (level and face tests,
  the caller and doc, the two order tests, `resolve` not `resolve_as`, the quad's silent case stated).
- **R3 (exec)**: f1 Critical (the VM names `bl64`/`bl32` are blend's) → `bt64`/`bt32`. f2 (silent wiring) →
  the positive checks and the row counts in every verdict. f3-f8 (mutants' red sets) → `P` holds tokens; b, c,
  d, e, h, i, j as rewritten. f9 (`sr-reject` cannot see the classifier; endpoint kinds with no cell; no cell
  for an additional chain's fill) → `sr-shape`'s cell per kind, `cf-bb`'s chain cell. f10 (rows that would
  break the lanes' predictions) → the two rules. f11 (the 03:35 guard before the lock) → the clock read in
  `jr.sh` and `post.sh`. f12 (the strict pattern) → spelled out. f13-f25 Minor, folded (units, reruns, scratch,
  the commit that carries the caller, rule exceptions, `us-level`'s draws, budgets, the skip line, error
  wording, `DXMT_CHECK_WORK`, the guard's scope, `jb` after a rebase, the two orders).

## Execution notes

(filled during execution)
