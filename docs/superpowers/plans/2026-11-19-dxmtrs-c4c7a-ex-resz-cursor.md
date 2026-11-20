# DXMTRS C4c-7a: gamma, frame latency, the `Ex` creators, RESZ and the cursor calls on D3D9's core runtime

Slice 7 of C4c, first part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s7a/map.md` (all of slice 7, read at DXMTRS 4166a54a; cited "map 2.4" ...). The 5, 6a and 6c plans
hold the rules this one repeats by name only. Reviews: `build/c4c/s7a/review-{1-design,2-core,3-exec}.md`
(cited R1, R2, R3; **R1 holds mtld3d's answer for every call of every row and the Wine table, R2 the trace
of each un-guarded body, R3 the helpers each row reuses, the registration edits and the job-script list**).
Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap:
`2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core` these calls stop answering `D3DERR_NOTAVAILABLE` and give mtld3d's answers:

- `SetGammaRamp`, `GetGammaRamp`: mtld3d's own bodies (map 2.1: they store and report a ramp and touch no
  recorder; a ramp is applied on a fullscreen device only, which the core does not make yet).
- `SetMaximumFrameLatency`, `GetMaximumFrameLatency`: mtld3d's own bodies (map 2.2), **and the bound is
  enforced**: the core's queue waits for the frame that many presents back, at the value mtld3d would
  enforce (the application's, or `present.maxFrameLatency` when it names one), from device creation on.
- `CreateRenderTargetEx`, `CreateOffscreenPlainSurfaceEx`, `CreateDepthStencilSurfaceEx`: mtld3d's own
  bodies (map 2.3: a usage check, the base creator, the reported usage).
- `SetRenderState(D3DRS_POINTSIZE, RESZ)`: the bound depth-stencil is copied (one sample) or resolved (more)
  into the depth texture bound at stage 0, at the call, and the state is stored (map 2.4).
- `SetCursorProperties`, `SetCursorPosition`, `ShowCursor`: mtld3d's own bodies (map 4: user32 and a
  message to a view the core device does not have, which is a no-op). **The answers are mtld3d's; the
  pointer on screen is not** (R7a-7): without the window subclass the Direct3D cursor shows only until the
  window's next `WM_SETCURSOR`.

Refused by name on the core, `D3DERR_NOTAVAILABLE`: RESZ into a **`D3DUSAGE_DYNAMIC` depth texture** while a
depth-stencil is bound (R7a-5): `IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth texture)`.

Wine, **in the runner's unit, "Test failed" lines over mtld3d's baseline**, by the map's table of the sweep
after C4c-6c, R1's reading of each function to its end and my reading of `resz_test` (its five RESZ calls:
a 2-sample D24S8 surface into an INTZ texture; a 2-sample automatic depth-stencil into one; no texture at
stage 0; a 1-sample surface into one; no depth-stencil bound):

| suite | function | now | predicted |
|---|---|---|---|
| visual | `resz_test` | 31 | 0 |
| device | `test_cursor` | 12 | 0 (to watch: device.c:1945, and the READONLY locks at 1960 and 2003) |
| device | `test_cursor_pos` | 0 (skipped) or 1 | 0, skipped or run (its last check is marked flaky) |
| d3d9ex | `test_resource_access` | 60 | 0 |
| d3d9ex | `test_frame_latency` | 9 | 0 |
| d3d9ex | `test_format_unknown` | 4 | 0 |
| d3d9ex | `test_create_depth_stencil_surface_ex` | 3 | 0 |
| d3d9ex | `test_user_memory` | 0 | 0 (it runs further: site 819 fails as on mtld3d, a baseline line) |

Totals over the baseline: `visual` 51 → 20, `device` 39 → 27, `d3d9ex` 138 → 62. What stays: fullscreen and
the window subclass (87: the maintainer's decision), `ProcessVertices` (5) and the three silent differences
(15), which are 7d's, and the two triaged lines. **Wine's own summary line counts differently** (it adds the
checks that succeed inside a todo block): about 33 such successes appear in `d3d9ex` (d3d9ex.c:4483 x30,
4026, 813, 830) where 76 failures go, so its summary stays near 138 while the runner's count falls to 62.

## Global Constraints

The 6c plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-6c pushed** (DXMTRS 4166a54a, MacNeutron ad9cbaee); job scripts in
`build/c4c/s7a/gates`, copied from s6c's and edited before their first run (R3's list, section 5), with the
guard (`/usr/bin/grep -nE 'c4c/s6c|c4c6c|C4c-6c'` over `*.sh *.txt` prints nothing) and R3's positive checks
run before anything else; VM state names `c4c7a-*`; the builder VM stopped after each VM job;
`memory_pressure` printed before a long job; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`,
privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` and `docs/` changes.** mtld3d's answers are unchanged: where a body runs for
  both runtimes, it is mtld3d's body as it stands.
- **No row calls `SetCursorPosition`** or asserts on the pointer's position: a row does not move the pointer
  of the machine the gates run on (Wine's `test_cursor_pos` does, in the conformance run and the sweep, as
  it always has).
- **A core log line of this slice contains none of the gates' can't-present words** (`was not recorded`,
  `does not build`, the rest of `d3d9-check.sh`'s `unpresented` pattern): the rows take RESZ's skip paths on
  purpose (R2 I-2, R3 I3).
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`. **No probe**: no file of the draw path changes (`rt/src/device/draw.rs`,
  `src/device/core_draw.rs`).

## The mechanism

### Guards removed (gamma, latency, the `Ex` creators, the cursor calls)

Ten `refuse_under_core!` sites go (map 1: sites 3, 4, 13 to 20). The bodies behind them are mtld3d's.
**R2 traced each to its leaves** (its table), the paths a `Reset`, a present and the device's release take
through the gamma and cursor state included: none reaches `encoder`, `current_frame`, `push_control`, an
encoder `unix_call` or `encoder_status()` on a core device (a few leaves scanned for those names, not read
line by line: R2 marks which; the final review re-reads them against the diff).

- gamma: plain fields; `queue_gamma` queues only on a fullscreen device.
- latency: `FrameLatency` fields.
- the `Ex` creators: `usage_accepted`, the base creator (already served on core), `report_usage` (on a
  default-pool plain surface it ORs restricted-content bits into the owned texture's `d3d_usage`; no reader
  compares that field for equality: R1 M-8).
- the cursor calls: user32, the cursor state, the bitmap's own `GetDesc` and `LockRect(READONLY)`, and
  `send_overlay_state`, which returns on a null view (a core device's). Not installed on core, and not to be
  installed by this slice: the window subclass (its `WM_SIZE` arm reaches a recorder flush: R2 I-1) and
  `CursorState::retarget` (it installs the subclass: R2 m-4).

The two-argument arm of `refuse_under_core!` loses its last user in Task 4 and goes with it.

### Frame latency enforced

No new item (R2 m-2). At the create, the queue profile the front end already hands to the core device
carries the bound: `profile.max_latency = enforced_frame_latency(None, cfg.present_max_frame_latency)` (3
unless the configuration names a number). At a successful `SetMaximumFrameLatency` on a core device, after
mtld3d's `bound.set(stored)`: `core.device().queue().set_max_latency(bound.enforced())` (an atomic store
read at each present's boundary: no clamp, no ring; never 0, which `frame_latency()` maps to the default;
lowering it to 1 cannot deadlock, one queue a core device: R2). Both under
`cfg(feature = "core-runtime")`.

### RESZ

One front-end arm, `core_arms::resz` (`pub fn` in the private module as its siblings, `#[cold]
#[inline(never)]` as mtld3d's `resolve_resz_depth`, named in the module's `//!` list), called where
`resolve_resz_depth` is, in place of the guard. It answers the refusal to the entry point, which returns it
before the state-block record and the store; on every other path the store that follows is mtld3d's code
(so `GetRenderState(POINTSIZE)` reads the magic back). In this order (R1 I-3, R2 m-1):

1. No texture at stage 0: mtld3d's one warning, nothing recorded.
2. A stage-0 texture that is not of a depth format: a skip (the core has no depth level for it).
3. Source = `core.depth()`, the bound depth-stencil, attached to the current pass or not (mtld3d's source
   is the binding too: R1 M-1). None: a skip. **So a dynamic depth texture with no depth-stencil bound is
   `D3D_OK` and nothing, as on mtld3d.**
4. The stage-0 texture is a `D3DUSAGE_DYNAMIC` depth texture: the refusal by name (R7a-5). Nothing has been
   recorded or made before it.
5. Destination = `DepthRef { id: texture_id, level: 0 }` after `texture::core_flush_dirty` has made the
   texture's core form (a depth texture never bound or sampled has none yet; its core form carries
   `RENDER_TARGET`, so the core's depth levels answer for it with or without `D3DUSAGE_DEPTHSTENCIL`: R1,
   R2). **Not** `Surface::core_depth()`: that names a level only of a texture made with
   `D3DUSAGE_DEPTHSTENCIL`, and a RESZ destination may be a plain depth texture (map 2.4 b).
6. `depth_resolve(src, dst)` when the source has more than one sample, else `copy_depth(src, dst)` (C4c-4b
   and 4d's: each ends the open pass first, so the draws recorded so far come before it, as mtld3d's
   stream order has them). False (sizes, formats or sample counts the core does not pair; the source being
   the destination) is a skip.

A skip is one `log_once_warn!` a reason, worded `d3d9 core: RESZ skipped: {reason} ({source:?} into
{destination:?})` (none of the gates' words), nothing recorded but, from step 5 on, the destination's core
form, and `D3D_OK`, as mtld3d's own skips answer. A non-dynamic depth texture has no dirty level (its lock
is refused), so nothing uploads over the resolve afterwards; the texture's next sampling bind reads it
(R2: no epoch to bump on core; an occlusion bracket and the present's fold are safe).

## Review Focus (for the final review)

- Every body un-guarded: no recorder call on a core device on any path (R2's traces against the diff, its
  scanned leaves read).
- RESZ: the destination named without `D3DUSAGE_DEPTHSTENCIL`; the refusal after the source check, before
  the store and before anything is made; every other path through the store; `src == dst`; the skip texts
  against the gates' pattern.
- The latency value at creation and at Set (no gate sees either: this reading is their only cover).
- mtld3d's answers unchanged (the diff outside `cfg(feature = "core-runtime")` and core branches).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`extras`** of `d3d9_triangle`, a device and a window of its own a row (`rsDevice`'s frame, an
extended device where a row says so), judged in every lane; the expectations are mtld3d's answers (R1's
table 1 per call, R3's per-row table for the helpers; the implementer reads the code again) and the control
confirms them. Common rules (R3): `OnDevice(d, bb)` with the back buffer, not null; every sub-cell named in
its `why`; a row's clears, draws, RESZ and reads inside one `until()` attempt with fresh objects; no `skip`
line ever (a missing 2-sample or 4-sample offer is a FAIL). Fifteen rows:

1. `gm-ramp`: Get into a buffer filled with a marker reads the identity (`index * 257`); a rising ramp set
   and read back; a ramp in which no channel rises is ignored (Get unchanged); a null ramp to Set and to Get
   and swap chain 1 change nothing (Get on swap chain 1 leaves the marker).
2. `fl-latency` (extended): Get 3; Set 30, Get 30; Set 31 `D3DERR_INVALIDCALL`, Get 30; Set 0, Get 3;
   Get(NULL) `D3DERR_INVALIDCALL`; after Set 1, three frames each drawn, read back, then presented; one
   more after Set 0.
3. `ex-rt` (extended): `CreateRenderTargetEx` with usage 0: `D3D_OK`, its description, a Clear and a quad
   read back through it; a refused usage: `D3DERR_INVALIDCALL` and the out slot **as passed** (a marker in
   a raw pointer, never a `Com<>`); `D3DFMT_UNKNOWN`: `D3DERR_INVALIDCALL` and the slot **null**.
4. `ex-ds` (extended): `CreateDepthStencilSurfaceEx` with usage 0: its description, bound, a near quad
   hiding a far one drawn after it; a refused usage (slot as passed); `D3DFMT_UNKNOWN` (slot null); a
   restricted-content usage as `GetDesc` reports it.
5. `ex-plain` (extended): `CreateOffscreenPlainSurfaceEx` with usage 0 in the default, system-memory and
   scratch pools: a texel written under a lock and read under a second; the managed pool:
   `D3DERR_INVALIDCALL`; a refused usage; a restricted-content usage in `GetDesc` (0x800).

The RESZ rows share one shape (R3 I4, I6, I7): 256x256; a source depth-stencil bound **beside a target of
its sample count**; four flat depth bands drawn into it (`zquad` at 0.125, 0.375, 0.625, 0.875, ZFUNC
ALWAYS): every band differs from 0 (the core's fresh depth texture) and from 1; a destination made with
`D3DUSAGE_DEPTHSTENCIL` first cleared to 1.0 through its level beside a 1-sample target, so a RESZ that did
nothing reads 0xff, never a band; the destination read with the fixed-function sample every lane already
proves (`sampleAt`, as `ds-intz`) into a 1-sample target, **with no depth-stencil bound**.

6. `rz-msaa`: a 4-sample D24S8 surface (the count `ms-dres` runs in every lane) into an INTZ texture:
   `D3D_OK`, `GetRenderState(POINTSIZE)` the magic, the four bands.
7. `rz-auto`: a device whose back buffer and automatic depth-stencil have 2 samples (`resz_test`'s second
   part; `rsDevice` gains a sample-count parameter): the same from the automatic depth-stencil.
8. `rz-single`: a 1-sample D24S8 surface into the INTZ texture: the copy, POINTSIZE read back.
9. `rz-plain`: the destination an INTZ texture made **without** `D3DUSAGE_DEPTHSTENCIL` (so never cleared:
   its content before is undefined on mtld3d and zero on core, and the bands differ from both), **a fresh
   texture a source**: from the 4-sample source, then another from the 1-sample source.
10. `rz-edges`: each `D3D_OK` and the destination still at its 1.0 clear: no texture at stage 0 (the
    destination elsewhere); no depth-stencil bound; a destination of another size (512x512); a colour
    render-target texture of the source's size at stage 0, still at its clear colour. A 1-sample source.
11. `rz-self`, **under the launch's `strict` argument only**: the bound depth-stencil's own texture at
    stage 0, no draw between the bind and the RESZ: `D3D_OK`, POINTSIZE the magic, the texture's content as
    it was (read after it is unbound as the depth-stencil). Not run on mtld3d, which sends Metal a copy of
    a texture onto itself there (an mtld3d finding for the maintainer's list, not exercised by a gate).
12. `rz-refuse`, **under `strict` only**: RESZ into a `D3DUSAGE_DYNAMIC` D24S8 texture with a D24S8
    depth-stencil bound: `D3DERR_NOTAVAILABLE`; POINTSIZE read before (0x3f800000) and unchanged after; a
    quad still lands. **Green at the base already** (the blanket guard gives the same answer: R3 I1); mutant
    f is what shows it red.
13. `cu-props`: `SetCursorProperties` with a null bitmap `D3DERR_INVALIDCALL`; an opaque 32x32 A8R8G8B8
    offscreen plain surface of the default pool and one of the scratch pool `D3D_OK`; a size that is no
    power of two and a format other than A8R8G8B8 `D3DERR_INVALIDCALL`; a hot spot of (40, 40) as mtld3d
    answers (predicted `D3D_OK`; the control settles it). The cursor is never shown.
14. `cu-show`: `ShowCursor` TRUE → 0, TRUE → 0 (nothing latches without a cursor), `SetCursorProperties`
    (opaque bitmap) `D3D_OK`, TRUE → 0, TRUE → 1, FALSE → 1, FALSE → 0, TRUE → 0; **the row ends on
    `ShowCursor(TRUE)` on every path** (a hide is not undone at the device's release: R3 I9).
15. `cu-setcursor`: the row's window given a class cursor (the arrow) and a window procedure that forwards
    to `DefWindowProc`; `SetCursorProperties`, `ShowCursor(TRUE)`, `GetCursor()` noted; the row **sends**
    `WM_SETCURSOR` (client area) itself and compares `GetCursor()`: the same handle on mtld3d (its subclass
    consumes the message). **Under `strict` the row expects the core's gap**: the class cursor is back
    (R7a-7), a cell that turns red the day the core gets the subclass, which is when it is rewritten. Ends
    on `ShowCursor(TRUE)`; the class cursor put back as it was.

- Scripts: as C4c-6c registered `reset` (R3's Q2 has each edit with its line: `d3d9-check.sh`'s launch,
  `check` list, refusal list and `refusals_ok` arm; the VM gate's rows loop, list and core loop; the guest
  script's 64- and 32-bit lines; `d3d9_triangle.cpp`'s mode, dispatch and rows table). The launch's refusal
  list is one literal, `rz-refuse`'s. A missing `check` line or `refusals_ok` arm is silent: the control
  catches the second (`extras-runtime` must be red there).
- Job scripts (R3 section 5): an `exrows` counter (15) beside `rsrows` at every call site; **`jv0`'s loop
  rides in `ja`** (one build fewer: R3 M8) on `extras` with its own grep of the launch's rows; `jq`'s list
  fourteen launches, "want 14"; the VM counters `ex64`/`ex32`; `jm`'s letters and tokens; `jmb` checks that
  each mutant descends from the tip; new runners, not copies (`JTVAL="extras"`).
- Controls, in this order: `ja` (the launch on mtld3d under the validation layer: reports 0, rows ok 17 of
  15 rows, module and imports; a red cell is a wrong expectation or an mtld3d finding, settled in a second
  row commit before any code. Then default, msl and core: **core red on fourteen rows, every one but
  `rz-refuse`, and on `extras-runtime`**), `jcr` (the VM on the default runtime: 15 a width; if the VM does
  not offer `rz-auto`'s 2 samples the row moves to 4 in the second row commit).

### Task 2: gamma, frame latency, the `Ex` creators (DXMTRS)

One commit: seven guards removed; the profile's bound at the create; the queue call at Set; STATUS.md's
lines for the three.

### Task 3: RESZ (DXMTRS)

One commit: `core_arms::resz`, the narrowed refusal, the guard gone; STATUS.md's line.

### Task 4: the cursor calls (DXMTRS)

One commit, apart so that it can be dropped whole (R7a-1): three guards removed, the macro's two-argument
arm deleted, STATUS.md's line (R7a-7's words). **One `jt` at this tip** (the rows map onto the tasks, so a
red row names its commit; each commit passes rust-check: the lighter-testing rule, not an omission): every
row of every launch green strict under core; `extras` under core with the validation layer: reports 0.

### Task 5: gates

`mutants-expect.txt`, `jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Four mutants (R3 section
4; no guard put back: a guard is a local early return the control already showed red with the same rows,
and it would add `extras-runtime` to the cells):
  a. RESZ always copies, never resolves → `rz-msaa`, `rz-auto`, `rz-plain` (its 4-sample cell);
  b. RESZ takes a destination only when it was made with `D3DUSAGE_DEPTHSTENCIL` → `rz-plain`;
  f. the narrowed refusal placed after the state store → `rz-refuse` (its POINTSIZE cell), nothing else;
  g. the arm returns before the store → the rows that read POINTSIZE back: `rz-msaa`, `rz-auto`,
     `rz-single`, `rz-self`.
- **No row and no mutant sees the latency's enforcement** (a present cannot tell how far the CPU ran ahead)
  or `SetCursorPosition`; gamma, the `Ex` creators and the cursor answers have the control's red as their
  proof. Stated here, not found as a NOT-RED later.

### Task 6: review. Task 7: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression, a new pub item of
  d3d9-rt:** none (the arm answers a plain `Result`/`Option` of the refusal's code: no enum, no derive).
- **New items:** `core_arms::resz` (front end). **New state:** none.
- **Not silent:** the one refusal by name; each RESZ skip warns once a reason.
- **Divergences, each stated in STATUS.md's core section:** RESZ into a dynamic depth texture refused
  (R7a-5); a multisampled source of another pixel format than the destination's skipped where mtld3d
  converts (R7a-4); the Direct3D cursor lost at the window's next `WM_SETCURSOR`, the cursor scale fixed at
  1 and `cursor.software` ignored (R7a-7); the latency bound is the queue's, counted against the GPU's
  retirement where mtld3d also waits for the display's pace, and a present to a hidden window counts no
  frame (R7a-2).

## Rulings (planning)

Each with its cost if wrong.

- **R7a-1: the roadmap's 7c (the cursor calls) is folded into 7a.** The map shows the three bodies need no
  window work for their answers (the roadmap's own condition), and a slice of three deletions would spend a
  whole review cycle. Cost if wrong (`test_cursor` needs the subclass, or a body reaches the recorder):
  Task 4's commit is dropped, a row commit removes `cu-props`, `cu-show` and `cu-setcursor` (15 → 12 at
  every counter), and the cursor calls go to C4d with a line in the roadmap; the rest of the slice stands.
- **R7a-2: the latency bound is enforced through dxmt-core's queue**, at the value mtld3d's `enforced()`
  gives, from the create and at each Set. Cost if wrong: frames paced differently from mtld3d for a game
  that sets a bound other than 3 or presents display-synced (the queue waits for the GPU's retirement of the
  frame N presents back; mtld3d also for the display-paced floor): pacing, not answers; no gate sees it.
- **R7a-3: RESZ's source is the core's bound depth-stencil.** Parity: mtld3d's current depth texture is the
  binding, restored into every frame, whether a pass attaches it or not (R1 M-1). No cost case found.
- **R7a-4: a pair the core's copy or resolve does not take is skipped with a warning and `D3D_OK`.**
  Single-sample pairs of two formats are parity (mtld3d's blit skips them too). **A multisampled source is
  a difference**: mtld3d's transfer kernel writes depth between `Depth32Float` and `Depth32Float_Stencil8`,
  so a multisampled D24X8, D16 or D32 source into INTZ, and a D24S8 or D24FS8 source into DF24 or DF16,
  resolve on mtld3d and are skipped on core. Cost if wrong: such a game (a multisampled D24X8 automatic
  depth-stencil with an INTZ texture is plausible) samples a depth texture nothing wrote, with one warning
  naming both; 7d's parity pass decides whether the conversion is owed before C4c closes.
- **R7a-5: RESZ into a `D3DUSAGE_DYNAMIC` depth texture is refused by name** (with a depth-stencil bound;
  without one it is mtld3d's no-op). Its CPU staging would upload over the resolve at the next bind unless
  the level is claimed for the GPU and read back at the next lock: a mechanism of its own, met by no suite.
  Cost if wrong: a game doing it gets `D3DERR_NOTAVAILABLE` from a `SetRenderState` and its log line; 7d
  decides whether it stays.
- **R7a-6: gamma is stored and reported, never applied**, as on mtld3d for a windowed device. Cost if
  wrong: none while the core makes windowed devices only; fullscreen's slice owes the presenter's ramp.
- **R7a-7: the cursor calls run without the window subclass, and that is kept in the slice** (R2 I-1 argued
  for dropping Task 4). Wine's `DefWindowProc` sets the window class's cursor at every `WM_SETCURSOR` in
  the client area; mtld3d's subclass consumes that message while its cursor shows; the core has no
  subclass. So for a window class with a cursor (the usual arrow) the Direct3D cursor shows until the
  pointer next moves, then the class cursor, and a hidden cursor comes back the same way; in Wine's retina
  mode the pointer is half mtld3d's size (scale fixed at 1) and `cursor.software` does nothing. **Why
  kept**: against the core as it is today (every cursor call refused, so the class cursor always) nothing
  gets worse, the answers become mtld3d's (twelve Wine lines), and `cu-setcursor` pins the gap in a row
  instead of leaving it inferred. Cost if wrong: a game that treats a successful `SetCursorProperties` as a
  promise shows the class arrow where under the refusal it might have drawn its own pointer; opt-in runtime;
  C4d's window work owes the subclass. **Put to the maintainer at the slice's wrap-up.**
- **P-7a: no probe; X-7a: no x64 check** (constraints above). Cost if wrong: a draw-cost change unseen
  until 7d's probe; a regression outside D3D9 unseen until 7d's check. Neither is expected from a diff that
  touches neither the draw path nor a file outside `src/d3d9`.

**Not ruled here, put to the maintainer:** the roadmap's 7b cannot do what it says inside C4c (map 3 and
7.2: the logger's thunks keep `mtld3d.so` on the core's path whatever 7b does, and memory pages on the Mac
need a new winemetal export). 7a does not depend on the answer.

## Stated gaps

- No row for `SetCursorPosition`; no row or mutant for the latency's enforcement; gamma's rows are its only
  evidence (no Wine test calls it).
- A stencil-format multisampled RESZ (D24S8 into INTZ, the usual pair) makes and copies a multisampled
  scratch texture at every call (`depth_resolve`'s, C4c-4d): a cost no probe sees here; 7d (R2 m-5).
- The cursor's window handle is stale after a core `Reset` that names another window (read at release
  only; harmless: R2 m-4).
- mtld3d copies a texture onto itself for RESZ with the bound depth-stencil's own texture at stage 0 (no
  guard in its encoder): for the maintainer's list.
- `GetAvailableTextureMem` does not count the `Ex` creators' surfaces on core, as it does not count the base
  creators' (documented in STATUS.md).

## Review dispositions

Three reviews, no Critical. Folded:

- **R1** (3 Important, 9 Minor): I-1 the Wine table's unit named, `test_user_memory` added, the todo
  successes; I-2 `rz-edges`' destinations cleared first, the self case its own strict-only row, reads with
  no depth-stencil; I-3 the refusal after the source check, `rz-refuse` on a dynamic D24S8; M-1 R7a-3 is
  parity; M-2 R7a-4's pairs named; M-3 R7a-2's display-pace clause; M-4 to M-7 the rows' cells (rising
  ramp and marker, `cu-show`'s sequence, the hot spot, the two slot shapes, the managed pool); M-9 the
  lines to watch. M-8 noted in the mechanism.
- **R2** (2 Important, 10 Minor): I-1 **not taken as proposed** (drop Task 4): kept with the cost restated
  and a row for the gap (R7a-7), and put to the maintainer; I-2 the skip text and the reads; m-1 the order;
  m-2 no new d3d9-rt method, the profile at the create; m-3, m-4, m-5, m-8 stated; m-6 the colour case
  kept at the source's size (R1 and R3 read mtld3d's format check there; the control decides), the self
  case off mtld3d; m-7 the Review Focus's wording; m-9 `pub fn`, the `//!` list, `#[cold]`, the macro's
  arm, no derive, "and `docs/`", STATUS.md's lines per task.
- **R3** (10 Important, 12 Minor): I1 `rz-refuse` green at the base, mutant f; I2 guard mutants dropped for
  f and g; I3 the skip text; I4 to I7 the RESZ rows' shape (pre-fills, a fresh texture a source, targets of
  the source's count, FAIL never skip, `sampleAt` and flat bands, no pixel-shader cell: Wine's `resz_test`
  is that path's cover); I8 the drop path's row commit; I9 the opaque bitmap and the row's end on TRUE; I10
  the scripts' positive checks and `jmb`'s ancestry check; M1 the counts; M2 to M7 the rows' details; M8
  `jv0` in `ja`; M9 one `jt`; M10, M11 the constraint's wording and the guard's reach; M12 sub-cells named.
  **Not taken**: `rz-msaa` at 2 samples as `resz_test` (4, the count every lane already proves; `rz-auto`
  keeps 2).

## Execution notes

(after the gates)
