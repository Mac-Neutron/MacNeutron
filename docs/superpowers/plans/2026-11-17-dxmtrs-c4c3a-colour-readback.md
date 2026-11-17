# DXMTRS C4c-3a: colour read-back through locks on D3D9's core runtime

Slice 3 of C4c ("read-back in all its forms"), first half. Process: the C4 program's (code map, this plan, three
adversarial reviews folded before code, rows first, one commit a feature, full gates once, one whole-branch
review, spec, pin, push). Map: `build/c4c/s3/map.md` (cited as "map §n"). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and rules:

- a lockable render target (`CreateRenderTarget(Lockable = TRUE)`) keeps its CPU staging; `LockRect` reads the
  target back into it unless `D3DLOCK_DISCARD`, and `UnlockRect` uploads it whole unless `D3DLOCK_READONLY`, in
  application order. This is the call that ends Wine's `visual` suite under core (`getPixelColorFromSurface`,
  map §5);
- the back buffer's `LockRect` with `D3DLOCK_READONLY` reads the rect it names;
- `IDirect3DTexture9::LockRect` of a render-target texture's level reads the level back first; what the lock
  writes is uploaded at `UnlockRect` (that path exists: `publish_render_target_write`);
- `GetFrontBufferData`, device and swap-chain form, reads the texture Present shows;
- the surfaces mtld3d does not lock answer `D3DERR_INVALIDCALL` as on mtld3d, not `D3DERR_NOTAVAILABLE`: a
  non-lockable render target, a depth-stencil surface, a `GetSurfaceLevel` / `GetCubeMapSurface` surface of a
  render-target or depth texture.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main 21d0ca5f and MacNeutron main 81eeca8f (C4c-2b). Fetch both origins before the first
  build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- No line of `src/dxmt/src` (dxmt-core), `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal`, `src/nvapi`
  changes. If a task finds it needs one, it stops and reports (the D3D10-12 gate would then be owed).
- The default runtime (mtld3d) draws and answers exactly what it does today: its translator key, imports and
  conformance are gates. Every edit of a function mtld3d runs is a core arm placed under
  `cfg(feature = "core-runtime")` and `is_core()`, ahead of code that stays as it is.
- **No mtld3d recorder call may be reachable on a core device** (`push_control`, `flush_current_frame_blocking`,
  `capture_frame_bytes`, a `Read*HandleOp`, `backbuffer_handle()`): each panics there (map §8.1). Every refusal
  this slice lifts is lifted in the commit that puts the core arm in front of that path, and in no other.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree.
- **Job scripts** live in `build/c4c/s3/gates`, each copied from `build/c4c/s2b/gates` and edited **before** its
  first run; a job that has run is never edited (a new job is a new file); jobs test committed tips. **Before
  any s3 job runs, `/usr/bin/grep -rnE 'c4c/s2b|c4c2b' build/c4c/s3/gates` prints nothing.**
- **Wanted values come from the base:** control job `jb` builds the base plain and writes its translator key and
  imports list; `jr.sh` and `post.sh` read them.
- **No `dxmt/check.sh` run may cross 03:35.** A chain starts by 01:55 or from 02:40; a lone `jr` or `post` does
  not start between 03:00 and 03:30.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller (the front end is a staticlib: dead code fails the gate). d3d9-rt is an rlib: Task 2 is its
  own commit.
- One lane (default, core) per task that moves a row, where this plan names one; full gates once; no fix round
  for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8. Out, with where it goes (ruling S-2):

- **C4c-3b:** a writable lock of a lockable back buffer (`D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`), `GetDC` /
  `ReleaseDC`, `PreLoad`, the lock of a DYNAMIC depth-format texture (depth planes read and written, map §8.8),
  `GetRenderTargetData` of an additional swap chain's back buffer, and a static WRITEONLY index buffer's
  release with a read of the device's copy (the only index read-back left: map §9.1). Each stays refused by
  name under core, or (the index buffer) keeps its backing.
- **C4c-4:** `StretchRect`'s other shapes, `ColorFill`, `UpdateSurface`. So no surface made by them is read here,
  and Wine's `visual` still meets `StretchRect`'s refusal before and after this slice: **no conformance lane
  gates C4c-3a** (the core proof fails on any refusal line; map §6). The rows are the gate.
- **A multisampled back buffer's lock:** decided by what mtld3d answers in Task 1's control (ruling 5).
- mtld3d's rect pitch for a READONLY back-buffer lock (the rect's own width, not the surface's) is kept, not
  judged.

## The mechanism

**d3d9-rt (Task 2).** `read_back(source, dst, pitch)` becomes a call of
`read_back_rect(source, x, y, width, height, dst, pitch)`: the same blit with the rect as its origin and size
and `bytes_per_row = width * texel_bytes`; false for a rect that is empty or leaves the subresource. **The
device's read-back buffer stays sized for the whole subresource** (its length and row are the keys it is remade
on today): a rect read uses its first `width * texel_bytes * height` bytes, so one-texel locks between whole
reads allocate nothing. `read_front(dst, pitch)` reads the texture Present shows (`back_buffer.presented()`,
single-sample on a multisampled device, where `TargetRef::BackBuffer` is the companion `read_back` refuses):
the same body over that texture and the back buffer's size, format BGRA8. Both commit and wait as `read_back`
does (`close_pass`, the blit, `sync_to`). Nothing else in d3d9-rt changes: a lock's write goes up through
`texture_upload`, which takes any stored id (map §3.4).

**Standalone surfaces (Tasks 3, 4).** `surface_lock_rect` and `surface_unlock_rect` choose their path by
`live_color_handle()`, which on a core device is null for a render target and reaches the recorder for the back
buffer. With the refusal lifted and nothing else, a core lockable target would be locked as a system-memory
surface (stale bytes, no upload) and the back buffer would answer as a depth-stencil. So the core arm sits **at
that test**, after mtld3d's first checks (null out pointer, null object, an outstanding `GetDC`, the
texture-backed branch), and classifies by what the surface is to the core:

| surface on a core device | answer |
|---|---|
| back buffer (`is_back_buffer()`) | Task 4's arm |
| standalone render target (`core_texture` set, not depth) with staging | the lockable arm |
| standalone render target without staging (non-lockable, multisampled) | `D3DERR_INVALIDCALL`, mtld3d's line |
| depth-stencil surface (standalone or the auto one) | `D3DERR_INVALIDCALL`, mtld3d's line |
| a system-memory surface, a level of a texture that is no target | falls through: today's path, unchanged |
| a `GetSurfaceLevel` / `GetCubeMapSurface` surface of a render-target or depth texture | reaches mtld3d's texture-backed branch, which answers before any recorder call (`D3DERR_INVALIDCALL` for a DEFAULT non-DYNAMIC texture; a DYNAMIC depth texture goes on to the texture's own lock and its refusal) |

*The lockable arm* (mtld3d's `lockable_rt_lock_rect` / `lockable_rt_upload` in the core's words).
`CreateRenderTarget` stops forcing `lockable = 0` on a core device, so the surface gets its zeroed staging at
`linear_row_pitch`. Lock: `try_begin_lock` (a second lock or a DC: `INVALIDCALL`); unless `DISCARD`,
`core.read_back(target, staging, pitch)`, a false answer being `D3DERR_INVALIDCALL` with one error line and
the lock taken back; `lock_flags = flags`; the pointer is the staging plus `top * pitch + left * bpp` by
mtld3d's unvalidated arithmetic (one shared helper if mtld3d's can be called as it is, else the same
expression: ruling 3). Unlock: when the lock was mapped and not `READONLY`,
`core.texture_upload(id, whole surface, staging, pitch)` at once (record time: a draw recorded before the
Unlock keeps the old texels, one after sees the write), then `try_end_lock`. A false upload logs one error and
the Unlock still ends the lock. `system_memory_blit_dst` answers `None` for a surface with a core texture, so a
lockable target is no `GetRenderTargetData` / `GetFrontBufferData` destination under core either (on mtld3d its
Metal handle says so).

*The back-buffer arm.* mtld3d's order: without `READONLY` on a back buffer that is not lockable,
`INVALIDCALL`; **without `READONLY` on a lockable one, refused by name**
(`IDirect3DSurface9::LockRect (a writable lock of the back buffer)`, `NOTAVAILABLE`: 3b); `try_begin_lock`;
`parse_surface_rect` (clamped; empty: `INVALIDCALL`); a fresh page of the rect at
`linear_row_pitch(rect width, 4)`; `core.read_back_rect(BackBuffer, rect, page, pitch)`; the page is kept in
`inner.readback` and dropped at Unlock, as on mtld3d. Unlock on a core back buffer: `try_end_lock` and the page
dropped; nothing is uploaded (no writable lock exists). A multisampled back buffer: ruling 5.

**Render-target texture levels (Task 5).** `core_refuses_lock` keeps only the DYNAMIC depth-format case and its
two literals become `IDirect3DTexture9::LockRect (a depth texture)` / `UnlockRect (a depth texture)`; a texture
of depth-stencil usage then reaches mtld3d's own `INVALIDCALL` for a depth format that is not DYNAMIC (checked
to sit before any recorder call). The read is one arm at the head of `materialize_subresource_from_gpu`, the
seam every reader goes through (map §8.1): on a core device, `core_flush_dirty` first (the core texture exists
and holds every CPU write owed, as `GetRenderTargetData`'s arm does), then
`core.read_back(TargetRef::Texture { id, level, face }, the level's staging, mip_bytes_per_row)`, then the
bookkeeping mtld3d runs after its own read (the authority bit cleared, the level counted as uploaded). A level
the core cannot resolve (not a target) returns what mtld3d returns for a texture with nothing on the GPU: the
staging as it is. `claim_render_target_for_cpu_read` already marks every lock of a target level, so each lock
reads the level again, as on mtld3d. Formats: a target's stored bytes are its D3D format's (no widened format
is a target: map §8.7), so no conversion.

**The front buffer (Task 6).** One arm in `read_front_buffer`, which both entry points call after their own
checks: mtld3d's destination check and `reject_readback` against the back buffer's size and format, then
`core.read_front(destination pages, its pitch)` and `note_system_memory_written`. The two refusals go.

## Review Focus (for the final review)

- No mtld3d recorder call reachable on a core device from any path this slice opens (`GetDC` paths stay
  refused: check that nothing lifted lets a DC be taken).
- The default runtime's functions are textually unchanged outside the arms.
- The lock state machine (`try_begin_lock` / `try_end_lock`, `lock_flags`, `readback`) is left consistent on
  every failure path of the arms.
- A lockable target's staging and the target after Lock / draw / Unlock in each order.
- `read_back_rect`'s bounds and the shared buffer's length.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in `dxmt/tests/d3d9_triangle.cpp` (Appendix), `dxmt/d3d9-check.sh` and `vm/dxmt/d3d9-gate.sh` (row lists;
`texture_refusals` goes and the `textures` launch joins the launches held to 0 refusal lines; the texture line
of `depth_refusals` takes the new depth literal; `vm/tests` fixtures follow). `strict` cells that today assert
`NOTAVAILABLE` (`tex-rtlock`) are rewritten to the result. Syntax-only compile on the four targets.

Job scripts in `build/c4c/s3/gates` (the grep above clean). Controls at the base + the rows:
- `jb`: the base built plain: key and imports written.
- `ja`: default and msl: **every row green on mtld3d at the first run, no expectation corrected** (a row that
  is red on mtld3d is a wrong row or a finding: it is fixed or dropped before any code, and the ledger says
  which). Core: red exactly on the rows the Appendix marks, written down before the run.
- `jv0`: the `targets` and `textures` launches on mtld3d under Metal's validation layer, 0 reports.
- `jcr`: the VM's default lane on old WoW64.
- Ruling 5's decision is read from `ja` (the `msaa` launch's back-buffer cell) and written in the ledger before
  Task 4.

Commit: "dxmt, vm: d3d9_triangle rows for locks of render targets, the back buffer and render-target textures,
and the front buffer's read (DXMTRS C4c-3a)".

### Task 2: `read_back_rect` and `read_front` (d3d9-rt)

As "d3d9-rt" above. Tests on the fakes (they show the blit's origin, size, row and the passes): a rect read
blits that origin and size at the rect's row; a whole read is unchanged; a one-texel read after a whole read
makes no new buffer (the same buffer handle in both blits) and a whole read after it does not either; an empty
rect and a rect past the edge record nothing and answer false; a short destination answers false;
`read_front` on a multisampled back buffer blits the single-sample texture, on a plain one the back buffer;
each closes the open pass and waits for its chunk. Commit: "d3d9: a rect of a target and the presented
texture read back on the core runtime".

### Task 3: lockable render targets and the surfaces that do not lock (front end)

The arm at the colour test of `surface_lock_rect` / `surface_unlock_rect` for standalone surfaces, the staging
kept at `CreateRenderTarget`, `system_memory_blit_dst`'s exclusion. `refused_under_core(this, true)` stops
refusing every surface **but the back buffer** (Task 4 lifts that): standalone targets, depth-stencil
surfaces, target and depth texture levels. `GetDC`, `ReleaseDC`, `PreLoad` keep `refused_under_core(this,
false)`. Lane `jt3` (default, core): `rt-lockable`, `rt-nolock` green under core; red under core: the rows of
Tasks 4-6 alone. Commit: "d3d9: lockable render targets lock on the core runtime".

### Task 4: the back buffer's read-only lock (front end)

The back-buffer arm, ruling 5's branch, the surface refusal's last case removed (`refused_under_core`'s `true`
callers then refuse nothing: the helper keeps its `false` callers). Commit: "d3d9: the back buffer locks
read-only on the core runtime".

### Task 5: render-target texture levels (front end)

`core_refuses_lock` narrowed, the two literals renamed, the arm in `materialize_subresource_from_gpu`. Every
caller of that seam on a core device is listed in the ledger with what reaches it (the lock, `UpdateTexture`
and the CPU-copy writers through `move_subresource_to_staging`; `GetDC`'s prep stays behind its refusal).
Commit: "d3d9: a render-target texture's level locks on the core runtime".

### Task 6: the front buffer (front end)

The arm in `read_front_buffer`; the device's and the swap chain's refusals removed. Lane `jt6` (default, core)
at the tip: every row green strict under core. Commit: "d3d9: GetFrontBufferData on the core runtime".

### Task 7: gates

- `jq`: d3d9-check on default, msl, core; the stateblock conformance on default and core; the core launches
  under Metal's validation layer (the layer proven on), 0 reports, rows ok.
- `jm`: mutants as commits off the tip (`gates/mutants.txt`), each red on its named row and cell and on nothing
  outside that launch. **For each, the pixel or the answer that changes is written in `mutants.txt` before the
  chain runs** (C4c-2b's mutant i):
  a. the lockable lock never reads back → `rt-lockable` (draw, lock READONLY: the triangle's texel reads 0);
  b. the lockable Unlock's upload dropped → `rt-lockable` (write a block, read the target: the old texel);
  c. the Unlock uploads after a READONLY lock too → `rt-lockable` (the scribbled texel reaches the target);
  d. `system_memory_blit_dst`'s exclusion dropped → `rt-lockable` (`GetRenderTargetData` into a lockable
     target answers `D3D_OK`);
  e. the lockable lock's pointer ignores the rect → `rt-lockable` (the texel written lands at (0, 0));
  f. `read_back_rect` blits from origin (0, 0) → `rt-bblock` (the one-texel lock reads the corner's colour);
  g. the texture arm returns without reading → `tex-rtlock` (the drawn texel reads as the staging's zeros);
  h. `read_front` reads the companion's `TargetRef` → `ms-front` (`D3DERR_INVALIDCALL` for pixels), if ruling
     6 keeps that cell; else `read_front_buffer`'s arm skips `reject_readback` → `rt-front` (a wrong-size
     destination answers `D3D_OK`).
  No mutant drops the `DISCARD` skip (a read-back that the lock then overwrites changes no pixel) or
  `core_flush_dirty` before the texture read (every write is published at its Unlock): stated, not gated.
- `jr`: plain build: translator key = `base-key.txt`, imports = `base-imports.txt`, d3d9-check default, the Mac
  Wine conformance with no new site, the x64 check.
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64.
- **Pre-ruled, no rerun for them** (memory, C4c-2b's notes): the VM or Mac `visual` count 211159 with failed 29
  and `visual.c:25196` at 0; the x64 check's nvapi display-count row when `system_profiler` shows one display
  (read at the failure); the hazards program's reference run printing "fence-order 1 0"; "presents to a
  minimised window" with the maintainer at the Mac. A build lost to the code-signing timestamp service is run
  again (a new job file reusing what was built). Anything else red is investigated before the push.
- **No probe** (ruling 7) unless the range touches `rt/src/device/{draw,recorder}.rs` or
  `src/device/core_draw.rs`.
- No D3D10-12 gate if `git diff --stat <base>..<tip>` shows no file outside `src/d3d9`.

### Task 8: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s3/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-3a"; the slice
list (3 becomes 3a done, 3b); **C4c-2b's sentence "Index buffers keep their backing (a fan reads it; read-back
is C4c-3)" is corrected** (a directly mapped index buffer has no backing and a fan reads it through
`dynamic_copy`; a staged one keeps its backing; the static WRITEONLY release is 3b's). Stale comments the map
names (`surface.rs` on C4b-3d, `d3d9_triangle.cpp:1099`). Plan execution notes. `dxmt/pins`. Privacy scan of
both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron. Post-push check (not across
03:35). CI. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** none. **A lint
  suppression:** none. **A derive:** none expected; recorded with `--update-derives` if a small type needs one.
- **New state:** none on the device (the read-back buffer exists); on a surface, nothing new (`system_memory`,
  `lock_flags`, `readback`, the lock flags are mtld3d's fields, now also used by the core arms).
- **New pub items:** `read_back_rect`, `read_front`, each with its doc. **Unsafe:** the pointer arithmetic of
  the lockable arm and the page slices each carry a SAFETY comment, one operation a block.
- **A duplicate:** the lockable arm repeats mtld3d's lock order (begin, read, flags, pointer) over another
  primitive; the rect arithmetic is shared if it can be (ruling 3). `parse_surface_rect`,
  `classify_color_surface_lock`'s cases, `reject_readback`, `linear_row_pitch`, `try_begin_lock` /
  `try_end_lock` are called, not copied. `read_front` and `read_back_rect` share one body.
- **Not silent:** a read-back or an upload that fails logs an error; every remaining refusal has its name; the
  gates fail on an unexpected refusal line.
- **Divergence:** none added. **Kept:** mtld3d's rect pitch on the back buffer; mtld3d's front buffer (the
  texture Present shows, whatever the swap effect).
- **Docs in the same change** (DXMTRS `docs/d3d9/TESTING.md`'s companion-edit rule): `docs/d3d9/STATUS.md` or
  `COVERAGE.md` only if they state something this slice changes for the default runtime; the core runtime's
  state is this repository's spec. Task 8 checks and writes the result.

## Rulings (planning)

- **S-2. C4c-3 is split:** 3a is colour read-back on the two primitives that exist (`read_back`,
  `texture_upload`); 3b is what needs a new one (depth planes both ways, a write into the back buffer, GDI over
  a store, a buffer read). Why: 3a alone moves `visual` past its fatal call and is four small arms; 3b's pieces
  have the least demand (map §8.8, §8.12). Cost if wrong: one more gate chain.
- **1. The arm sits at the colour test, inside mtld3d's function,** not at the function's head. Why: mtld3d's
  first answers (null pointers, a DC outstanding, the texture-backed branch) then hold by construction. Cost if
  wrong: an order difference in an error path no row sees.
- **2. A lockable target reads back whole and uploads whole,** as mtld3d does, though the lock names a rect.
  Why: parity (a draw between Lock and Unlock is overwritten on both). Cost if wrong: Wine's one-texel locks of
  a lockable target cost a whole read each, as on mtld3d.
- **3. The rect arithmetic of the lockable lock is mtld3d's, unvalidated.** Shared when mtld3d's function splits
  cleanly into "state and read" and "pointer"; otherwise the same expression in the arm with a comment naming
  its twin. Cost if wrong: two copies of four lines.
- **4. The read-back buffer keeps its whole-subresource size for a rect read.** Why: Wine alternates one-texel
  and whole reads. Cost if wrong: none measurable (the buffer exists for `GetRenderTargetData` already).
- **5. A multisampled back buffer's READONLY lock follows mtld3d's answer in control `ja`:** (A)
  `D3DERR_INVALIDCALL` there → the arm answers the same before any read; (B) pixels there → under core it is
  refused by name (`IDirect3DSurface9::LockRect (a multisampled back buffer)`) and joins 3b (it needs a resolve
  into the presented texture outside Present), the `msaa` launch's expected refusals gain that line, and the
  cell asserts `NOTAVAILABLE` under strict. Cost if wrong: (B) leaves one refusal Wine's device tests may meet.
- **6. `GetFrontBufferData` on a multisampled device reads the presented single-sample texture** (what Present
  last resolved), by `read_front`; the cell `ms-front` stays only if mtld3d passes it in `ja` with pixels that
  do not depend on whether a Present came first; otherwise it is dropped and stated. Cost if wrong: a front
  read before the first Present returns the initial texture's bytes.
- **7. No probe.** The slice adds no work to a draw; `read_back`'s whole form is one call deeper. Cost if wrong:
  a cost on `GetRenderTargetData` nobody measured (the bench's proof read would show it in the next slice's
  probe).
- **8. No conformance lane** (Scope). Cost if wrong: none; the lane cannot pass before C4c-4 to 7.
- **9. The writable back-buffer lock's refusal has no row** (the row program makes its device without the
  lockable flag; a second device per launch is 3b's work with the lock itself). Cost if wrong: a wrong literal
  nobody reads until 3b.

## Stated gaps

- A draw between a lockable target's Lock and Unlock is overwritten by the Unlock (mtld3d's rule).
- Every lock of a target level and every non-DISCARD lock of a lockable target is a whole read, a commit and a
  wait; a core render-target texture keeps CPU staging for every level for life (map §8.2), which in a 32-bit
  process is address space mtld3d gives back.
- The read of a lockable target in a format mtld3d renders to but the core's `read_back` has not met in a row
  (16-bit, float, 10-bit): the rows use A8R8G8B8 and X8R8G8B8, plus one cell in another format if the `targets`
  launch already draws to one.
- A lock of a target that is bound and drawn while locked: not checked on either runtime.
- No row: the writable back-buffer refusal (ruling 9); `GetFrontBufferData` before the first Present; a lock
  after `Present` beyond one cell; an additional swap chain's back buffer locked (it aliases the implicit one).

## Appendix: the rows

Core today (before any code) is marked: **R** red by design, **G** green already.

1. **`rt-lockable`** (targets) **R**. A 64x64 A8R8G8B8 `CreateRenderTarget(.., TRUE, ..)`.
   - draw then lock: bound, cleared to one colour, one triangle of another; `LockRect(NULL, READONLY)`: pitch
     256, a texel inside the triangle and one outside read as drawn; `UnlockRect`;
   - Clear then lock with no draw between: the clear colour;
   - write then read: `LockRect(NULL, 0)` (the pointer shows the target's pixels), a block rewritten,
     `UnlockRect`; `GetRenderTargetData` into a system-memory surface: the block, and the triangle untouched
     elsewhere;
   - application order: draw, lock-write-unlock, a second draw over half the block: each texel is its last
     writer's;
   - `DISCARD`: the whole surface written, read back whole;
   - `READONLY` then a write through the pointer: the target's texel is unchanged after `UnlockRect`;
   - a rect lock `{8, 8, 16, 16}`: one texel written through `pBits` lands at (8, 8);
   - a second `LockRect` while locked and a second `UnlockRect`: `D3DERR_INVALIDCALL`;
   - `GetRenderTargetData(back buffer, the lockable target)`: `D3DERR_INVALIDCALL`.
2. **`rt-nolock`** (targets) **R**. `LockRect` answers `D3DERR_INVALIDCALL` on: a `Lockable = FALSE` target; the
   depth-stencil surface of `GetDepthStencilSurface` (if the launch has one, else a
   `CreateDepthStencilSurface`); a render-target texture's `GetSurfaceLevel(0)`; a render-target cube's
   `GetCubeMapSurface` face. The out structure is as mtld3d leaves it only where D3D9 defines it.
3. **`rt-bblock`** (targets) **R**. Draw to the back buffer; `GetBackBuffer`; `LockRect(&{x, y, x+1, y+1},
   READONLY)` reads that texel (two points of different colours); `LockRect(NULL, READONLY)` reads both through
   the pitch it reports; `LockRect(NULL, 0)`: `D3DERR_INVALIDCALL`; an empty rect: `D3DERR_INVALIDCALL`; a second
   lock while locked: `D3DERR_INVALIDCALL`; `UnlockRect` twice: the second `D3DERR_INVALIDCALL`.
4. **`tex-rtlock`** (textures), rewritten **R**. A render-target texture: draw into level 0;
   `tex->LockRect(0, NULL, READONLY)` reads a drawn texel and a cleared one; `LockRect(0, NULL, 0)`, a block
   rewritten, `UnlockRect`; `GetRenderTargetData` of the level: the block and the rest as drawn; a draw after
   the write lands over it; a second level, if the texture has one, reads what was drawn into it.
5. **`rt-front`** (targets) **R**. `GetFrontBufferData(0, system-memory surface of the back buffer's size)` after
   a draw and a Present reads the frame presented; `GetFrontBufferData(1, ..)`: `D3DERR_INVALIDCALL`; a null
   destination and one of another size: `D3DERR_INVALIDCALL`; the implicit swap chain's own
   `GetFrontBufferData` reads the same texel. (The row presents: it is placed where the launch's own frame
   check is not disturbed, or it draws the launch's frame again after it.)
6. **`ms-bblock`** (msaa): the multisampled back buffer's `LockRect(.., READONLY)`: ruling 5's cell, written to
   pass on mtld3d whatever it answers there (pixels, or `D3DERR_INVALIDCALL`), strict under core per the
   branch. **`ms-front`** (msaa): ruling 6's cell.
7. **`ds-lock`** (depth) **G**: unchanged but for the literal its launch expects.
