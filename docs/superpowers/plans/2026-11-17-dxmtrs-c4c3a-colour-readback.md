# DXMTRS C4c-3a: colour read-back through locks on D3D9's core runtime

Slice 3 of C4c ("read-back in all its forms"), first half. Process: the C4 program's (code map, this plan, three
adversarial reviews folded before code, rows first, one commit a feature, full gates once, one whole-branch
review, spec, pin, push). Map: `build/c4c/s3/map.md` (cited as "map §n"). Reviews:
`build/c4c/s3/review-{1-design,2-core,3-exec}.md`. Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and rules:

- a lockable render target (`CreateRenderTarget(Lockable = TRUE)`) keeps its CPU staging; `LockRect` reads the
  target back into it unless `D3DLOCK_DISCARD`, and `UnlockRect` uploads it whole unless `D3DLOCK_READONLY`, in
  application order. By the map's reading of Wine's source this is the call that ends the `visual` suite under
  core (`getPixelColorFromSurface`, map §5); Task 7's sweep says whether it is;
- the back buffer's `LockRect` with `D3DLOCK_READONLY` reads the rect it names, on a multisampled device too;
- `IDirect3DTexture9::LockRect` of a render-target texture's level reads the level back first; what the lock
  writes is uploaded at `UnlockRect` (that path exists: `publish_render_target_write`);
- `GetFrontBufferData`, device and swap-chain form, reads the back buffer as it is now (mtld3d's front buffer);
- the surfaces mtld3d does not lock answer `D3DERR_INVALIDCALL` as on mtld3d, not `D3DERR_NOTAVAILABLE`: a
  non-lockable render target, a depth-stencil surface, a `GetSurfaceLevel` / `GetCubeMapSurface` surface of a
  render-target or depth texture.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main 21d0ca5f and MacNeutron main 81eeca8f (C4c-2b). Fetch both origins before the first
  build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- **No file outside `src/d3d9` changes** in DXMTRS: not dxmt-core (`src/dxmt/src`), not its fakes
  (`src/dxmt/fakes`), not `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal`, `src/nvapi`. If a task finds it
  needs one, it stops and reports (the D3D10-12 gate would then be owed).
- The default runtime (mtld3d) draws and answers exactly what it does today: its translator key, imports and
  conformance are gates. Every edit of a function mtld3d runs is a core arm under
  `cfg(feature = "core-runtime")` and `is_core()`, ahead of code that stays as it is.
- **No mtld3d recorder call may be reachable on a core device** (`push_control`,
  `flush_current_frame_blocking`, `capture_frame_bytes`, a `Read*HandleOp`): each panics there (map §8.1).
  Every refusal this slice lifts is lifted in the commit that puts the core arm in front of that path, and in
  no other. (`backbuffer_handle()` and `live_color_handle()` do not panic: they read fields that are null for
  every surface of a core device, which is what makes an unarmed path take the wrong branch silently.)
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree.
- **Job scripts** live in `build/c4c/s3/gates`, each copied from `build/c4c/s2b/gates` and edited **before** its
  first run; a job that has run is never edited (a new job is a new file); jobs test committed tips. **Before
  any s3 job runs, `/usr/bin/grep -rnE 'c4c/s2b|c4c2b|buffers' build/c4c/s3/gates` shows only lines the ledger
  lists as meant** (the copies carry more than paths: Task 1 says what changes in each).
- **Wanted values come from the base:** control job `jb` builds the base plain and writes its translator key and
  imports list; `jr.sh` and `post.sh` read them.
- **No `dxmt/check.sh` run may cross 03:35.** The chain runs `jr` first (its check ends about 32 minutes in): a
  chain or a lone `jr` or `post` starts before 02:55 or after 03:36.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller (the front end is a staticlib: dead code fails the gate). d3d9-rt is an rlib: Task 2 is its
  own commit.
- One lane (default, core) per task that moves a row, where this plan names one; full gates once; no fix round
  for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8. Out, with where it goes (ruling S-2):

- **C4c-3b:** a writable lock of a lockable back buffer (`D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`), a lock of an
  additional swap chain's back buffer and its `GetRenderTargetData`, `GetDC` / `ReleaseDC`, `PreLoad`, the lock
  of a DYNAMIC depth-format texture (depth planes read and written, map §8.8), and a static WRITEONLY index
  buffer's release with a read of the device's copy (the only index read-back left: map §9.1). Each stays
  refused by name under core, or (the index buffer) keeps its backing.
- **C4c-4:** `StretchRect`'s other shapes, `ColorFill`, `UpdateSurface`. Wine's `visual` meets `StretchRect`'s
  refusal before and after this slice, so **no conformance lane gates C4c-3a** (the core proof fails on any
  refusal line; map §6). The rows are the gate; a sweep that gates nothing shows where the suites stop.
- The core's single-sample back buffer is not zeroed at creation (`rt/swapchain.rs`): a read before any Clear
  or draw returns what the allocation held. Older than this slice; stated, and a follow-up.

## The mechanism

**d3d9-rt (Task 2).** `read_back(source, dst, pitch)` keeps its contract (a whole single-sample colour
subresource; false for a multisampled one). New: `read_presented(rect, dst, pitch)`, the back buffer as Present
would show it now, `rect` (mtld3d-core's `DirtyRect`, texels of the BGRA8 back buffer) at `dst`'s pitch:

- when the back buffer has a multisampled companion, **the resolve `present()` records is recorded first**
  (the companion into the single-sample texture, through the view its last pass or Clear wrote it in); that
  code moves from `present()` into a helper both call. Present resolves again every frame, so nothing else
  changes; on a plain back buffer the texture drawn into is the texture shown and nothing is resolved;
- then the blit of `rect` from that texture (the command's origin and size; `bytes_per_row = rect.w * 4`),
  `close_pass`, `sync_to`, and the rows copied out **by the rect's row**;
- false, and nothing recorded, for an empty rect, a rect that leaves the back buffer (`checked_add`), a pitch
  under the row or a short destination. Sizes are computed in `u64`.

Both share one body. **The device's read-back buffer is kept whenever it is long enough and remade only to
grow** (today it is remade whenever the length or the row differs; the row is never read by d3d9-rt): Wine
alternates whole reads of targets of different sizes with back-buffer reads, and in a 32-bit process each
remake is a placed allocation. Its ceiling: the largest read keeps its bytes for the device's life.

**Standalone surfaces (Tasks 3, 4).** `surface_lock_rect` chooses its path by `live_color_handle()`, null for
every surface of a core device. With the refusal lifted and no arm, a core lockable target would lock as a
system-memory surface (stale bytes, no upload at Unlock) and the back buffer would answer the depth-stencil
line. The core arm sits **at that test**, after mtld3d's first checks (null out pointer, null object, an
outstanding `GetDC`, the texture-backed branch, none of which reaches the recorder), with two cases:

| surface on a core device | answer |
|---|---|
| a back buffer (`is_back_buffer()`) | Task 4's arm |
| a surface with a core texture that is no depth-stencil (`CreateRenderTarget`) | with staging: the lockable arm; without (non-lockable, multisampled): `D3DERR_INVALIDCALL` with mtld3d's "non-lockable render target" line |

Everything else is the unchanged code, which already answers on a core device without the recorder: a
system-memory surface locks as today; a depth-stencil surface gets mtld3d's `INVALIDCALL` line; a
`GetSurfaceLevel` / `GetCubeMapSurface` surface of a render-target or depth texture returns from the
texture-backed branch (`INVALIDCALL` for a DEFAULT texture that is not DYNAMIC, before `cube_lock_rect` or the
texture's own lock).

`surface_unlock_rect` has no such test and gets **two named spots**: (1) inside its `system_memory` branch, in
place of the `live_color_handle()` term of the upload's condition: a core surface with a core texture, mapped,
not `READONLY`, uploads, then `try_end_lock` as today; (2) at the head of the standalone path, before
`was_mapped` / `wrote`: a core back buffer ends its lock, clears `lock_flags`, drops the page, and returns,
so mtld3d's write-back is never consulted. A non-lockable target or a depth-stencil falls to the unchanged
tail (`try_end_lock`: `INVALIDCALL`), which is recorder-free on core.

*The lockable arm* (mtld3d's `lockable_rt_lock_rect` / `lockable_rt_upload` in the core's words).
`CreateRenderTarget` stops forcing `lockable = 0` on a core device, so the surface gets its zeroed staging at
`linear_row_pitch`. Lock: `try_begin_lock` (a second lock or a DC: `INVALIDCALL`); unless `DISCARD`,
`core.read_back(target, staging, pitch)`, a false answer being `D3DERR_INVALIDCALL` with one error line and
the lock taken back; `lock_flags = flags`; the pointer is the staging plus `top * pitch + left * bpp` by
mtld3d's unvalidated arithmetic (ruling 3). Unlock (spot 1): `core.texture_upload(id, whole surface, staging,
pitch)` at once (record time: it ends an open render pass, a draw recorded before keeps the old texels, one
after sees the write, and the tracker orders a later read after it); a false upload logs one error and the
Unlock still ends the lock. `system_memory_blit_dst` answers `None` for a surface with a core texture, so a
lockable target is no `GetRenderTargetData` / `GetFrontBufferData` destination under core either (on mtld3d its
Metal handle says so).

*The back-buffer arm.* mtld3d's order: without `READONLY` on a back buffer that is not lockable,
`INVALIDCALL`; without `READONLY` on a lockable one, **refused by name**
(`IDirect3DSurface9::LockRect (a writable lock of the back buffer)`, `NOTAVAILABLE`: 3b; it sits where
mtld3d's flag check does, so a second lock or an empty rect on such a device is refused too); an additional
swap chain's back buffer, **refused by name** (`IDirect3DSurface9::LockRect (an additional swap chain's back
buffer)`: it aliases the device's under core, and `GetRenderTargetData` refuses it for the same reason);
`try_begin_lock`; `parse_surface_rect` (clamped; empty: `INVALIDCALL`, the lock taken back); a **zeroed** page
of the rect at `linear_row_pitch(rect width, 4)`; `core.read_presented(rect, page, pitch)`, false being
`INVALIDCALL` with one error line and the lock taken back; `lock_flags = flags`; the page kept in
`inner.readback`. Unlock: spot 2.

**Render-target texture levels (Task 5).** `core_refuses_lock` keeps only the DYNAMIC depth-format case and its
two literals become `IDirect3DTexture9::LockRect (a depth texture)` / `UnlockRect (a depth texture)`; a texture
of depth-stencil usage then reaches mtld3d's own `INVALIDCALL` for a depth format that is not DYNAMIC. The read
is one arm at the head of `materialize_subresource_from_gpu`, the seam every reader goes through (map §8.1):

1. return to mtld3d's body unless `ti.device_inner != 0` and the device is a core device;
2. `core_flush_dirty(ti, core)` (the core texture exists and holds every CPU write owed, as
   `GetRenderTargetData`'s arm does);
3. only then the level's staging slice (the flush reads the same pages), and
   `core.read_back(TargetRef::Texture { id, level, face: ti.cube.is_some().then_some(face) }, staging,
   mip_bytes_per_row(level))`;
4. false (a level the core does not resolve, no staging, a failed read): **one error line and `false`**, so the
   claim stays and the lock answers `INVALIDCALL`. There is no "nothing on the GPU, keep the staging" branch:
   on a core device every render-target texture is in the store after the flush, and such a branch would turn
   a wrong face or level into `D3D_OK` with zeros;
5. `true`, with no bookkeeping: the caller (`move_subresource_to_staging`) clears the claim and counts the
   level as uploaded. mtld3d's `ensure_staging` / `prepare_staging_read_back` are skipped on purpose: a core
   device never drops staging and nothing reads it past record time.

`claim_render_target_for_cpu_read` already marks every lock of a target level, so each lock reads the level
again, as on mtld3d. A target's stored bytes are its D3D format's (no widened format is a target: map §8.7).

**The front buffer (Task 6).** One arm in `read_front_buffer`, which both entry points call after their own
checks: mtld3d's destination check and `reject_readback` against the back buffer's size and format, then
`core.read_presented(the whole back buffer, destination pages, its pitch)`, false being `INVALIDCALL` with one
error line, and `note_system_memory_written`. The two refusals go.

## Review Focus (for the final review)

- No mtld3d recorder call reachable on a core device from any path this slice opens (`GetDC` stays refused for
  every surface: nothing lifted lets a DC be taken; `read_level_from_gpu` stays unreachable because a core
  device never drops staging).
- The default runtime's functions are textually unchanged outside the arms.
- The lock state machine (`try_begin_lock` / `try_end_lock`, `lock_flags`, `readback`) on every failure path of
  the arms, and on a device made with `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER` (no row: ruling 9).
- A lockable target's staging and the target after Lock / Clear / draw / Unlock in each order.
- `read_presented`'s bounds, the grow-only buffer against a larger read after a smaller one, the copy-out row.
- The resolve helper: Present's behaviour is unchanged (the same closure, the same order).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in `dxmt/tests/d3d9_triangle.cpp` (Appendix; `tex-rtlock` needs a forward declaration, its helpers are
defined after `textureRows`). **Rules for every new cell:** a lock is unlocked before its pixels are judged
(`readTarget`'s pattern), so a retried attempt never finds the surface locked; every drawn-pixel cell runs in
`until()` with its objects made inside the attempt (the back buffer is the one object that persists); the
colours (clear, triangle, block, scribble, rect texel, the two back-buffer points) have every channel non-zero
and are pairwise distinct; no cell judges the out structure after a failed lock; `targetRows`' time budget is
raised if four more rows need it.

Scripts: `dxmt/d3d9-check.sh` (row lists; `texture_refusals` deleted and `textures` moved to the launches held
to 0 refusal lines; the texture line of `depth_refusals` takes the new depth literal; the header prose and
row counts) and the same in `vm/dxmt/d3d9-gate.sh` (`:231,235,242-245`, the `tx` loop without a kind) and
`vm/tests/d3d9.sh`'s comments; the stale comment at `d3d9_triangle.cpp:1111`. `sh -n` on both scripts;
syntax-only compile on the four targets.

Job scripts in `build/c4c/s3/gates`, from s2b's: `head.sh`, `jcr.sh`, `jc.sh` (state names `c4c3a-*`);
`jv0.sh` loops over `targets textures msaa` in place of `buffers`; `jm.sh` takes `jm2.sh`'s three-try build
from the start, reads SHAs alone from `mutants.txt`, names a launch and a row per mutant and filters per
launch in place of `buffers-`; `jw.sh` from `build/c4b3d/s5/gates/jw2.sh` (the sweep); a chain in the order
`jr`, `jc`, `jq`, `jm`, `jw`; no `jp`.

Controls at the base + the rows:
- `jb`: the base built plain: key and imports written.
- `ja`: default and msl: **every row green on mtld3d at the first run, no expectation corrected** (a row red on
  mtld3d is a wrong row or a finding: fixed or dropped before any code, and the ledger says which). Core, the
  failing tokens written in the ledger before the run; predicted: `targets-rt-lockable targets-rt-nolock
  targets-rt-bblock targets-rt-front targets-runtime textures-tex-rtlock textures-runtime depth-runtime
  msaa-ms-bblock msaa-ms-front msaa-runtime` and nothing else.
- `jv0`: the `targets`, `textures` and `msaa` launches on mtld3d under Metal's validation layer: **0 reports
  inside the new rows**; a report in an older row (these launches never ran under the layer on mtld3d) is
  recorded with its row as a finding and does not fail the control.
- `jcr`: the VM's default lane on old WoW64 (and the `tg32` launch's time against its budget).

Commit: "dxmt, vm: d3d9_triangle rows for locks of render targets, the back buffer and render-target textures,
and the front buffer's read (DXMTRS C4c-3a)".

### Task 2: `read_presented` (d3d9-rt)

As "d3d9-rt" above. Tests on the fakes as they are (a copy line shows its source, level, slice, size and row;
a resolve shows as its own line; **no origin and no buffer handle**, so the origin is gated by mutant f and
"the same buffer" by `Arc::ptr_eq` on the device's read-back field):
- a rect read copies that size at the rect's row from the back buffer's texture, closes the open pass and
  waits for its chunk; a whole read of a target is unchanged (the three existing exact-line asserts hold);
- on a multisampled back buffer the resolve line comes before the copy and the copy's source is the
  single-sample texture; on a plain one no resolve; `present()` after it still records its own resolve;
- the buffer: a small read after a large one keeps it (`Arc::ptr_eq`); a larger read after a small one makes
  a longer one; a rect read first, before any whole read, works;
- the copy-out: from a pre-patterned mapping, a 2x2 rect at a destination pitch larger than its row lands row
  by row (the fakes' memory is writable);
- an empty rect, a rect past the edge, a short destination and a pitch under the row record nothing and
  answer false;
- (the mechanism Task 3 leans on) a Clear, then `texture_upload` into that target, then a draw: the pass after
  the upload loads, it does not clear.

Commit: "d3d9: the back buffer as Present would show it, read back by rect on the core runtime".

### Task 3: lockable render targets and the surfaces that do not lock (front end)

The lock arm's second case and Unlock's spot 1, the staging kept at `CreateRenderTarget`,
`system_memory_blit_dst`'s exclusion. `refused_under_core(this, true)` refuses **only a back buffer** from here
(Task 4 lifts that). `GetDC`, `ReleaseDC`, `PreLoad` keep `refused_under_core(this, false)`. Lane `jt3`
(default, core): `rt-lockable`, `rt-nolock` green under core; the failing tokens written before the run,
predicted: `targets-rt-bblock targets-rt-front targets-runtime textures-tex-rtlock textures-runtime
depth-runtime msaa-ms-bblock msaa-ms-front msaa-runtime`. Commit: "d3d9: lockable render targets lock on the
core runtime".

### Task 4: the back buffer's read-only lock (front end)

The lock arm's first case, Unlock's spot 2, the two named refusals. The surface refusal's last case goes:
`refused_under_core` loses its parameter and its texture-level computation (every caller left passes `false`).
Commit: "d3d9: the back buffer locks read-only on the core runtime".

### Task 5: render-target texture levels (front end)

`core_refuses_lock` narrowed, the two literals renamed, the arm in `materialize_subresource_from_gpu`. Every
caller of that seam on a core device is listed in the ledger with what reaches it (the texture lock; the cube
lock, where a target cube is refused before it; `UpdateTexture` and the CPU-copy writers through
`move_subresource_to_staging`; `materialize_subresource_for_cpu_read` behind `StretchRect`'s and `GetDC`'s
refusals; `read_level_from_gpu`, unreachable). Commit: "d3d9: a render-target texture's level locks on the
core runtime".

### Task 6: the front buffer (front end)

The arm in `read_front_buffer`; the device's and the swap chain's refusals removed. Lane `jt6` (default, core)
at the tip: every row green strict under core, every launch's refusal set exact. Commit: "d3d9:
GetFrontBufferData on the core runtime".

### Task 7: gates

- `jr`: plain build: translator key = `base-key.txt`, imports = `base-imports.txt`, d3d9-check default, the Mac
  Wine conformance with no new site, the x64 check.
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64 (the `tg32` / `ms32` launches are the 32-bit
  placed read-back buffer's proof).
- `jq`: d3d9-check on default, msl, core; the stateblock conformance on default and core; the core launches
  under Metal's validation layer (the layer proven on), 0 reports, rows ok.
- `jm`: mutants as commits off the tip (`gates/mutants.txt`: SHAs alone), each red on its named row and on
  nothing outside its launch. **`gates/mutants-expect.txt` names, before the chain runs, the cell and the
  texel or the answer that changes for each** (C4c-2b's mutant i):
  a. the lockable lock never reads back → `rt-lockable` (draw, lock READONLY: the triangle's texel reads the
     staging's zeros);
  b. the lockable Unlock's upload dropped → `rt-lockable` (write a block, read the target: the old texel);
  c. the Unlock uploads after a READONLY lock too → `rt-lockable` (the scribbled texel reaches the target);
  d. `system_memory_blit_dst`'s exclusion dropped → `rt-lockable` (`GetRenderTargetData` of a 64x64 target
     into the 64x64 lockable one answers `D3D_OK`);
  e. the lockable lock's pointer ignores the rect → `rt-lockable` (the texel written lands at (0, 0); (8, 8)
     keeps its old, different texel);
  f. `read_presented` copies from origin (0, 0) → `rt-bblock` (a one-texel lock reads the corner's colour,
     which differs from the point's);
  g. the texture arm answers `true` without reading → `tex-rtlock` (the drawn texel reads as the staging's
     zeros);
  h. `read_presented` records no resolve → `ms-front` and `ms-bblock` (the texel of the frame presented
     before, where the cell wants the frame drawn since);
  i. the texture arm reads level 0 whatever the level → `tex-rtlock` (level 1's texel).
  No mutant drops the `DISCARD` skip (a read-back the lock then overwrites changes no pixel),
  `core_flush_dirty` before the texture read (every write is published at its Unlock) or `reject_readback`
  in the front buffer's arm (mtld3d's own check, called; `rt-front`'s larger destination is its cell):
  stated, not gated.
- `jw` (gates nothing): the sweep of `visual`, `device` and `d3d9ex` under core as C4b-3d5's `jw2` ran it →
  `build/c4c/s3/sweep.md`: where each suite stops now. It says whether the Goal's first claim held.
- **Pre-ruled, no rerun for them** (memory; C4c-2b's notes): the VM or Mac `visual` count 211159 with failed 29
  and `visual.c:25196` at 0; the x64 check's nvapi display-count row when `system_profiler` shows one display
  at the failure; the hazards program's reference run printing "fence-order 1 0"; "presents to a minimised
  window" when HID idle time at the failure is under ten minutes. A build that died on "A timestamp was
  expected but was not found" or "The timestamp service is not available" is run again; no other signing
  failure is excused. Anything else red is investigated before the push.
- **No probe** (ruling 7) unless the range touches `rt/src/device/{draw,recorder}.rs` or
  `src/device/core_draw.rs`.
- No D3D10-12 gate while `git diff --stat <base>..<tip>` shows no file outside `src/d3d9`.

### Task 8: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s3/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-3a"; the slice
list (3 becomes 3a done and 3b; "the next sweep is after slice 3" becomes this slice's sweep and its result);
**C4c-2b's sentence "Index buffers keep their backing (a fan reads it; read-back is C4c-3)" is corrected** (a
directly mapped index buffer has no backing and a fan reads it through `dynamic_copy`; a staged one keeps its
backing; the static WRITEONLY release is 3b's). The stale comment in `surface.rs` on C4b-3d. Plan execution
notes. `dxmt/pins`. Privacy scan of both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then
MacNeutron. Post-push check (not across 03:35). CI. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** none. **A lint
  suppression:** none (the rect is one `DirtyRect` argument: no argument-count lint). **A derive:** none
  (`DirtyRect` is inventoried).
- **New state:** none on the device (the read-back buffer exists; its key loses the row); on a surface, nothing
  new (`system_memory`, `lock_flags`, `readback` and the lock flags are mtld3d's fields, now also used by the
  core arms).
- **New pub items:** `read_presented`, with its doc. **Unsafe:** the pointer arithmetic of the lockable arm and
  the page slices each carry a SAFETY comment, one operation a block; the back-buffer page is zeroed before a
  slice of it exists.
- **A duplicate:** the lockable arm repeats mtld3d's lock order (begin, read, flags, pointer) over another
  primitive; the rect arithmetic is shared if it can be (ruling 3). `parse_surface_rect`, `reject_readback`,
  `linear_row_pitch`, `try_begin_lock` / `try_end_lock` are called, not copied. `read_back` and
  `read_presented` share one body; Present and `read_presented` share the resolve.
- **Not silent:** a read-back or an upload that fails logs an error in its arm; every refusal has its name; the
  gates fail on an unexpected refusal line. **New refusal literals:** `IDirect3DSurface9::LockRect (a writable
  lock of the back buffer)`, `IDirect3DSurface9::LockRect (an additional swap chain's back buffer)`,
  `IDirect3DTexture9::LockRect (a depth texture)`, `IDirect3DTexture9::UnlockRect (a depth texture)`.
- **Divergence added:** none. **Kept, mtld3d's and now the core's too:** `IDirect3DTexture9::LockRect` of a
  DEFAULT render-target texture succeeds (D3D9 refuses it); the back buffer locks READONLY without
  `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`; a READONLY back-buffer lock reports the rect's own pitch; the front
  buffer is the back buffer as it is now.
- **Docs in the same change:** DXMTRS's `STATUS.md`, `COVERAGE.md`, `CONFORMANCE.md` and `mtld3d.conf` say
  nothing of the core runtime and the default runtime is unchanged: no edit owed (review 3 checked); Task 8
  confirms it against the final diff.

## Rulings (planning)

- **S-2. C4c-3 is split:** 3a is colour read-back on the two primitives that exist (`read_back`,
  `texture_upload`); 3b is what needs a new one (depth planes both ways, a write into the back buffer, GDI over
  a store, a buffer read). Why: 3a alone moves `visual` past the call the map names and is four small arms;
  3b's pieces have the least demand (map §8.8, §8.12). Cost if wrong: one more gate chain.
- **1. The lock arm sits at the colour test, inside mtld3d's function,** and Unlock's at two named spots. Why:
  mtld3d's first answers then hold by construction. Cost if wrong: an order difference in an error path no row
  sees.
- **2. A lockable target reads back whole and uploads whole,** as mtld3d does, though the lock names a rect.
  Why: parity (a draw between Lock and Unlock is overwritten on both). Cost if wrong: Wine's one-texel locks of
  a lockable target cost a whole read, a commit and a wait each, as on mtld3d.
- **3. The rect arithmetic of the lockable lock is mtld3d's, unvalidated.** Shared when mtld3d's function splits
  cleanly into "state and read" and "pointer"; otherwise the same expression in the arm with a comment naming
  its twin. Cost if wrong: two copies of four lines.
- **4. The read-back buffer only grows.** Why: reads of different sizes alternate; the row was never a real
  key. Cost if wrong: the largest read's bytes stay for the device's life (a 32-bit process holds a 1080p
  frame's 8 MiB).
- **5. A multisampled back buffer is resolved before it is read** (the lock and the front buffer), by the
  resolve Present records. Why: mtld3d resolves at every flush, a read included
  (`core/src/passes.rs:6260-6297`), so its lock and its front read return the frame as drawn so far; reading
  what the last Present resolved would differ mid-frame, return unwritten memory before the first Present, and
  never resolve on a hidden window (Present skips there). Cost if wrong: one resolve pass a read on a
  multisampled device.
- **6. `GetRenderTargetData` of a multisampled back buffer stays `INVALIDCALL`** (mtld3d's answer, the `ms-desc`
  row): only the lock and the front buffer resolve.
- **7. No probe.** The slice adds no work to a draw. Cost if wrong: a cost on `GetRenderTargetData` nobody
  measured (the bench's proof read would show it in the next slice's probe).
- **8. No conformance lane; one sweep that gates nothing.** Cost if wrong: none; the lane cannot pass before
  C4c-4 to 7.
- **9. The two back-buffer refusals have no row** (the row program makes one device without the lockable flag
  and locks no additional chain's buffer; 3b's work). Cost if wrong: a wrong literal, or a lock-state fault on
  a lockable-back-buffer device, that nobody sees until 3b: the final review reads that path.
- **10. No fakes edit:** the blit's origin is gated by a mutant on a row, not by a unit test. Why: a change
  under `src/dxmt/fakes` is outside `src/d3d9`. Cost if wrong: an origin fault shows in `jt6`, not in
  rust-check.

## Stated gaps

- A draw between a lockable target's Lock and Unlock is overwritten by the Unlock (mtld3d's rule).
- Every lock of a target level and every non-DISCARD lock of a lockable target is a whole read, a commit and a
  wait; a core render-target texture keeps CPU staging for every level for life (map §8.2), which in a 32-bit
  process is address space mtld3d gives back.
- The rows read A8R8G8B8 and X8R8G8B8 targets; a lockable target in a 16-bit, float or 10-bit format takes the
  same path with no row.
- A lock of a target that is bound and drawn while locked: not checked on either runtime.
- The x86_64 VM guest's mapping of the read-back buffer is proven by the rows (`GetRenderTargetData` reads
  through it in every core VM lane today), not by source.
- No row: the two back-buffer refusals (ruling 9); a read before any Clear or draw (Scope); a lock held across
  Present; a read on a hidden window.

## Appendix: the rows

Core at the base (before any code): **R** red by design, **G** green already. The rules of Task 1 apply.

1. **`rt-lockable`** (targets) **R**. A 64x64 A8R8G8B8 `CreateRenderTarget(.., TRUE, ..)`.
   - draw then lock: bound, cleared to one colour, one triangle of another; `LockRect(NULL, READONLY)`: pitch
     256, a texel inside the triangle and one outside read as drawn;
   - Clear then lock with no draw between: the clear colour;
   - write then read: `LockRect(NULL, 0)` (the pointer shows the target's pixels), a block rewritten,
     `UnlockRect`; `GetRenderTargetData` into a system-memory surface: the block, and the triangle untouched
     elsewhere;
   - Clear, then lock-write-unlock of a block, then a draw elsewhere: the block and the Clear both hold;
   - application order: draw, lock-write-unlock, a second draw over half the block: each texel is its last
     writer's;
   - `DISCARD`: the whole surface written, read back whole;
   - `READONLY` then a write through the pointer: the target's texel is unchanged after `UnlockRect`;
   - a rect lock `{8, 8, 16, 16}`: one texel written through `pBits` lands at (8, 8), where another colour was;
   - a second `LockRect` while locked and a second `UnlockRect`: `D3DERR_INVALIDCALL`;
   - `GetRenderTargetData(a 64x64 non-lockable target of the same format, the lockable target)`:
     `D3DERR_INVALIDCALL`.
2. **`rt-nolock`** (targets) **R**. `LockRect` answers `D3DERR_INVALIDCALL` on: a `Lockable = FALSE` target; a
   multisampled `CreateRenderTarget`; a `CreateDepthStencilSurface(D24S8)` (the launch has no auto one); a
   render-target texture's `GetSurfaceLevel(0)`; a render-target cube's `GetCubeMapSurface` face. No
   `UnlockRect` after a failed lock (until Task 5 a level's Unlock still meets the old texture refusal).
3. **`rt-bblock`** (targets) **R**. Draw to the back buffer; `GetBackBuffer`; `LockRect(&{x, y, x+1, y+1},
   READONLY)` reads that texel, at two points of different colours neither of which is the corner's;
   `LockRect(NULL, READONLY)` reads both through the pitch it reports; `LockRect(NULL, 0)`:
   `D3DERR_INVALIDCALL`; an empty rect: `D3DERR_INVALIDCALL`, and a lock after it succeeds; a second lock while
   locked: `D3DERR_INVALIDCALL`; `UnlockRect` twice: the second `D3DERR_INVALIDCALL`.
4. **`tex-rtlock`** (textures), rewritten **R**. A 64x64 render-target texture with two levels: draw into level
   0 and, in another colour, into level 1; `tex->LockRect(0, NULL, READONLY)` reads a drawn texel and a cleared
   one; `LockRect(1, ..)` reads level 1's; `LockRect(0, NULL, 0)`, a block rewritten, `UnlockRect`;
   `GetRenderTargetData` of level 0: the block and the rest as drawn; a draw after the write lands over it.
5. **`rt-front`** (targets) **R**. After a draw (no Present needed), `GetFrontBufferData(0, system-memory
   surface of the back buffer's size)` reads the frame drawn; `GetFrontBufferData(1, ..)`:
   `D3DERR_INVALIDCALL`; a null destination, and one larger in both dimensions (512x512):
   `D3DERR_INVALIDCALL`; the implicit swap chain's own `GetFrontBufferData` reads the same texel.
6. **`ms-bblock`** (msaa) **R**: frame A drawn and presented; frame B (another colour at the point) drawn; the
   multisampled back buffer's `LockRect(&point, READONLY)` reads B. **`ms-front`** (msaa) **R**: the same
   sequence, `GetFrontBufferData` reads B.
7. **`ds-lock`** (depth) **G** as a row; its launch's refusal set changes literal (red as `depth-runtime` until
   Task 5).

## Review dispositions

- **R1** (design; 0 Critical, 4 Important, 13 Minor). I1 → the premise corrected (null handles, not a panic):
  the constraint, the mechanism's first paragraph, a two-case table. I2 → Unlock's two named spots. I3 →
  rulings 5 and 6 rewritten: the resolve before a read (`read_presented`), no multisample refusal, `ms-bblock`
  and `ms-front` assert the frame drawn since the last Present. I4 → the texture arm's five steps, no
  keep-the-staging branch, the face's encoding, no bookkeeping. Minors → `lock_flags` set and Unlock's spot 2
  ahead of the write-back; the refusal's position stated; a zeroed page; the grow-only buffer (ruling 4); the
  additional chain's back buffer refused by name; the table reworded; level 1 mandatory with mutant i; the
  Clear-upload-draw cell; no Unlock after `rt-nolock`'s failed locks; `refused_under_core` loses its
  parameter; the kept divergences named; the unzeroed back buffer in Scope; the sweep in Task 7.
- **R2** (core; 0 Critical, 3 Important, 12 Minor). I1 → the buffer reused only when long enough, the copy-out
  by the rect's row, the two tests. I2 → no fakes edit (ruling 10): `Arc::ptr_eq`, the origin left to mutant
  f; the constraint names the fakes. I3 → as R1's I4. Minors → as R1's on the page, `lock_flags`, the Unlock
  spots; the slice formed after the flush; `checked_add` and `u64`; ruling 4's rationale; the
  Clear-upload-draw unit test; error lines in every arm; the rect's units.
- **R3** (exec; 3 Critical, 7 Important, minors). C1 → mutant d's cell reads a 64x64 target. C2 → decided in the
  plan from the source, not from `ja` (and by R1's I3 the answer is a resolve, so no refusal set changes for
  `msaa`). C3 → as R2's I2. I1 → the failing tokens of `ja` and `jt3`, `<launch>-runtime` included. I2 → the
  rect as `DirtyRect`. I3 → the rows' rules (unlock before judging, objects inside the attempt). I4 →
  `rt-front`'s larger destination. I5 → `mutants-expect.txt`, `jm` from `jm2`, `jv0`'s launches, no `jp`. I6 →
  `jr` first and the start window. I7 → `jv0` judges the new rows. Minors → the premise; `rt-front` needs no
  Present; `rt-nolock`'s cells; the colours' rule; the forward declaration; the caller ledger with
  `read_level_from_gpu`; the scripts' prose and `sh -n`; the stale comment's line; the flakes' literals and
  the HID test; the Goal's claim marked as the map's reading with the sweep to check it; the budget.
