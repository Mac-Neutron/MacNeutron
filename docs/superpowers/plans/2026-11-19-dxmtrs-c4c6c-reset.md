# DXMTRS C4c-6c: `Reset` on D3D9's core runtime (windowed)

Slice 6 of C4c, second part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s6/map.md`, sections 3, 4, 6 and 7 (cited "map 3.1 step 17", "hazard 4" ...); written for all of
slice 6 at DXMTRS 501d43d1, so line numbers moved by C4c-6a's commits. The 5 and 6a plans hold the rules this
one repeats by name only. Reviews: `build/c4c/s6c/review-{1-design,2-core,3-exec}.md` (cited R1, R2, R3;
**R3 holds the Wine table, the per-row table and the job-script list this plan refers to**). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core`, `Reset` and `ResetEx` of a **windowed** device, `TestCooperativeLevel` and
`CheckDeviceState` give mtld3d's answers (map 3.1, 3.3):

- A valid `Reset` re-creates the back buffer and the automatic depth-stencil at the new size and sample
  count, moves the device to another device window when one is named, puts every state at its default (an
  extended device keeps its state, as on mtld3d), binds the implicit targets again, and answers `D3D_OK`.
- mtld3d's own validation decides what is refused, in mtld3d's two ways (R1 I5): **invalid present
  parameters** (swap effect, back-buffer count, interval) answer `D3DERR_INVALIDCALL` and change nothing;
  **everything refused later** (a multisample type or an automatic depth-stencil format the device cannot
  make, a default-pool resource or an implicit surface still referenced on a plain device, no client area)
  answers `D3DERR_INVALIDCALL`, puts the states at their defaults and **leaves the device in need of a
  reset**: `TestCooperativeLevel` and every present answer `D3DERR_DEVICENOTRESET` until a `Reset` succeeds.
- `TestCooperativeLevel`: `D3DERR_DEVICENOTRESET` while a reset is needed, else `D3D_OK` (an extended device:
  always `D3D_OK`). `CheckDeviceState`: `D3D_OK`.

Refused by name on the core, each `D3DERR_NOTAVAILABLE`, after mtld3d's validation has had its say and
without marking the device as needing a reset (rulings R6c-3, R6c-5): a `Reset` into fullscreen, and a
`Reset` to a back-buffer format the core cannot make (it makes X8R8G8B8 and A8R8G8B8, as its `CreateDevice`).

Wine, by my sweep tool's counts after C4c-6a and R3's reading of each function to its end (its table has the
lines): device `test_swapchain_parameters` 62 → 15, `test_wndproc_windowed` 7 → 4, `test_reset_resources`
6 → 0, `test_swapchain` 4 → 4, `test_lockable_backbuffer` 3 → 0, `test_scissor_size` 2 → 0,
`test_reset_fullscreen` 2 → 2, `test_scene` 1 → 0, `test_swapchain_multisample_reset` 1 → 0 (88 → 25); visual
`test_backbuffer_resize` 3 → 0, `test_managed_reset` 1 → 0; d3d9ex `test_reset` 54 → 18. What stays is
fullscreen's or the window subclass's. **`d3d9ex` then runs eighteen functions it never reached under core**
(it died in `test_reset`): its total is predicted to RISE, from 57 to somewhere between 120 and 170, most of
it the `Ex` creators C4c-7a owes (`test_resource_access` alone about 70), and it may still end early
(`test_desktop_window` uses a device it did not check). Every function above also runs further than any
suite has under core: new sites are expected, and the sweep is the chain's first job.

## Global Constraints

The 6a plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-6a pushed** (DXMTRS d8ac1c1b, MacNeutron acb8898c); job scripts in
`build/c4c/s6c/gates`, copied from s6's and edited before their first run: **every script sources its
folder's `head.sh` by path and `head.sh` empties the job's folder: a copy left pointing at s6 would wipe
s6's results** (R3 I7), so the guard (`/usr/bin/grep -nE 'c4c/s6([^c]|$)|c4c6a|C4c-6a'` prints nothing) runs
before anything else, with the positive checks (row counts, launch lists, letters and tokens, `X64_BASE`
d8ac1c1b, the runner's SHAs); VM state names `c4c6c-*`; the builder VM stopped after each VM job;
`memory_pressure` printed before a long job; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`,
privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` changes.** A GPU device-lost report would need dxmt-core's queue: not taken
  (R6c-2). The fakes record no view call and live outside `src/d3d9`: tests are worded for what they show.
- mtld3d's `Reset` is unchanged: the core's branches sit inside `reset_impl` and its helpers beside the
  recorder calls they replace (map 3.2), not in a second Reset.
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`.

## The mechanism

### d3d9-rt

- **`CoreDevice::resize(width, height, samples)`**: `drain()`, then a new back buffer and multisampled
  companion made as `attach` makes them, the presenter's `change_layer_properties` for the new size, **the
  view and the presenter kept** (R6c-1 as corrected by R2's Critical: a view made again on one window leaks
  a client surface each time on the Mac; mtld3d and D3D11 both resize in place; `drain()` is enough, no GPU
  wait: the finish thread drops a chunk's closures before it signals it done).
- **`CoreDevice::retarget(window)`** (or one function taking both: the implementer's choice, named in the
  commit): the window half of `attach` alone (the old presenter then view dropped, a view and presenter on
  the new window), for a `Reset` that names another device window; `attach` replaces the old window only on
  success (R2 M5), and a failure leaves the device presenting nothing, which the front end answers as a
  failed `Reset`.
- **The automatic depth-stencil under a fresh id** (R2 M1: the store's invariant; R1 C1): the front end makes
  the new one as device creation does and names it through `core_auto_depth`, which gains a way to be cleared
  when the new parameters ask for none (R1 I1, R2 I2: today only a setter exists, and
  `GetDepthStencilSurface` would keep answering the old surface). The old texture is forgotten unless a held
  surface of an extended device still names it.
- **`CoreDevice::reset_targets()`**: target 0 the back buffer, the extra targets none, the depth-stencil the
  automatic one or none.
- Nothing else of the `CoreDevice` is stale after a resize (R2's table: the pipeline maps, the quad's and the
  resolve's functions, the samplers, the open-bracket count, the retained bytes; the read-back buffer only
  grows; scratch textures are forgotten at each call).

### Front end: core branches inside `reset_impl` and its helpers

Each replaces a recorder-bound step of mtld3d's `Reset` (map 3.1's numbering); the front-end writes that sit
inside the replaced helpers stay (R1 I1, R2 I2: `set_backbuffer_dims`, the `depth_stencil_format` updates,
the held surfaces' detach).

- Step 4, `encoder_status()`: skipped on a core device (no latch: R6c-2).
- After step 10 and before the window mode is resolved (R1 I3, R3 I1: mtld3d's validation answers
  `D3DERR_INVALIDCALL` first, as Wine expects): the two refusals by name, fullscreen and the back-buffer
  format, `D3DERR_NOTAVAILABLE`, `NOT_RESET` not set.
- `reject_reset` and `fail_reset`: core forms without the flush and the frame reseed; `NOT_RESET` and the
  defaults as mtld3d.
- Step 15, the "resized" test: a core form **with mtld3d's held-surface terms kept** (R1 C1, R3 I6); the
  handle it tests on mtld3d is always null on a core device.
- Steps 16 to 18: `retarget` when the window changed; `resize` when the size or the sample count changed or a
  surface is held; the automatic depth-stencil re-made, kept or dropped (the core form of
  `reconcile_implicit_depth`, which keys on a handle the core does not have); nothing recorded when nothing
  changed.
- **A held implicit surface on an extended device** (R1 C1): mtld3d detaches it and it keeps its old
  textures. On the core the detach runs too; a held automatic depth-stencil keeps its old texture (its old
  id); **a held back buffer becomes inert** (ruling R6c-4): it keeps its description, names no core target,
  and a use that needs its texture answers as a surface without one does. Its memory charge is refunded as
  mtld3d refunds it (R1: `surface.rs` 1830-1860 does not on the core path today).
- Step 20, the frame reseed: skipped.
- Step 22, `reset_targets_to_implicit`: **its first recorder call is reachable on a core device and would
  panic** (the core's `SetRenderTarget` sets the autogen state: R1 I2, R2 I1): the core form regenerates the
  bound autogen target's mipmaps (`texture_generate_mips`, as `unbind_extra_render_target` does) before the
  teardown, skips the extra-target unbind (unreachable on a core device), and calls `reset_targets()`
  **inside** this helper, so a refused `Reset` gets the implicit targets too.
- Gamma and the teardown steps touch no recorder (R1) and run as they are.
- `TestCooperativeLevel` and `CheckDeviceState`: core forms ahead of `encoder_status()` (map 3.3). The
  presents already answer `D3DERR_DEVICENOTRESET` through mtld3d's own entry code (C4c-6a).
- Queries: no blocker on either runtime; an open bracket stays open across the drain. `rs-query`'s
  expectations are R1's reading of mtld3d (its section on row 10).

## Review Focus (for the final review)

- Every path of `reset_impl`, `reject_reset`, `fail_reset`, `reset_to_defaults` and
  `reset_targets_to_implicit` on a core device: no recorder call; what a failure leaves at each point.
- The resize: what still names the old back buffer after it; the companion; the layer's properties; a
  sample-count change and the depth-stencil's count.
- Held surfaces of an extended device; the automatic depth-stencil's ids and its clearing.
- `NOT_RESET`: set and cleared exactly where mtld3d sets and clears it; not by the two refusals by name.
- Which window a re-targeted device presents into (no row can tell: below).
- mtld3d unchanged; nothing outside `src/d3d9`.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`reset`** of `d3d9_triangle`, a device and a window of its own a row (cheap: R3; a `Reset`
changes the device other rows would share), judged in every lane; the expectations are mtld3d's answers
(R1's table per row and R3's are the starting point; the implementer reads the code again) and the control
confirms them. A draw after a `Reset` is judged by reading the back buffer; a present by C4c-6a's pattern
with a query made after the `Reset`. Fourteen rows:

1. `rs-size`: `Reset` to another size: the back buffer's description, the default viewport and scissor at
   the new size, a Clear and a quad read back; a zero size takes the window's client size (a plain window,
   not a maximized one: R1 I6).
2. `rs-depth` (the device has an automatic depth-stencil): the new size and format, a depth-tested draw;
   a `Reset` that drops it (`GetDepthStencilSurface` then `D3DERR_NOTFOUND`) and one that brings it back.
3. `rs-msaa`: one sample to four and back: a draw, the resolve at present, the read-back.
4. `rs-blockers`: a default-pool texture alive: `Reset` `D3DERR_INVALIDCALL`, then `TestCooperativeLevel`
   and a `Present` `D3DERR_DEVICENOTRESET`; released: `Reset` `D3D_OK`, `TestCooperativeLevel` `D3D_OK`; the
   same for a render target, a vertex buffer, a held back-buffer surface; a managed texture and a query are
   no blockers.
5. `rs-state`: render states, a bound texture, a stream source and the viewport at their defaults after a
   `Reset` (the depth test's default as the device's automatic depth-stencil makes it: R1 I6); **a quad
   drawn after the `Reset` lands in the back buffer** (a getter answers the back buffer whatever the core
   targets: R3 I4); a managed texture's content survives.
6. `rs-autogen`: an AUTOGENMIPMAP render-target texture bound at target 0 and a second target at 1, the
   application's references released, `Reset`: `D3D_OK` and a quad lands in the back buffer (the path whose
   recorder call would panic: R3 I2, so that a row meets it before Wine's `test_reset_resources` does).
7. `rs-scene`: a `Reset` inside `BeginScene` ends the scene; a refused one too (R1 I6: as mtld3d answers).
8. `rs-invalid`: invalid present parameters: `D3DERR_INVALIDCALL`, the device usable and not in need of a
   reset; a multisample type the device cannot make: `D3DERR_INVALIDCALL` and in need of one (R1 I5).
9. `rs-refuse`: **under the launch's `strict` argument only** (the core lanes; elsewhere the row does nothing
   and passes: on mtld3d a `Reset` into fullscreen would change the display mode of the machine the gates
   run on): a `Reset` into fullscreen answers `D3DERR_NOTAVAILABLE`, the device is still usable and not in
   need of a reset (`TestCooperativeLevel` `D3D_OK`, a quad lands). The refusal's name is fixed here so that
   the row's scripts and Task 3 agree: `IDirect3DDevice9::Reset (into fullscreen)`.
10. `rs-window`: a `Reset` naming another device window: a draw read back and a present. **Which window
    receives it no row can tell** (a cell proves a commit: R3's Critical; the fakes record no view call):
    stated, and the final review's to read.
11. `rs-ex`: an extended device: `ResetEx` keeps the state a plain `Reset` clears (the viewport's depth range
    as mtld3d leaves it: R1 I6) and is not blocked by a default-pool resource; `CheckDeviceState` and
    `TestCooperativeLevel` `D3D_OK`.
12. `rs-held`: an extended device whose back buffer the application holds across `ResetEx` to another size:
    the held surface keeps its old description, the device's new back buffer has the new one, a quad lands
    in the new one (R1 C1; `d3d9ex.c` 3934-3948).
13. `rs-query`: an EVENT and an OCCLUSION query issued before a `Reset` and read after it.
14. `rs-twice`: several `Reset`s in a row at changing sizes, each followed by a draw read back and a present.

- Scripts: as C4c-5 registered `queries` (`d3d9-check.sh`'s launch, `check` list and "none" refusal list:
  **`refusals_ok`'s default arm passes a launch it does not know**; the VM gate's two loops; the guest
  script's 64- and 32-bit lines). `rs-refuse` meets one refusal by name on the core: the launch takes a
  refusal list of its own (that one literal), as `fixed`, `depth` and `buffers` have. The format refusal's
  name, met by no row: `IDirect3DDevice9::Reset (a back-buffer format the core does not make)`.
- Job scripts (R3's list): a `RESET_ROWS` counter (14) at every call site, `jv0` on the launch (rows ok 16),
  `jq`'s list thirteen launches with its count, the VM counters, `jm`'s letters and tokens.
- Controls, in this order: `jv0` (the launch on mtld3d under the validation layer; a red cell is a wrong
  expectation or an mtld3d finding, settled in a second row commit before any code), `ja` (core red on
  exactly the launch's rows and its `-runtime`; each row stops at its first refused call), `jcr`.

### Task 2: `resize`, `retarget`, `reset_targets` in d3d9-rt

One commit. Tests on the fakes, by what they show (R2 I3 has the list): the textures a resize makes and
forgets, the layer's properties call, the targets after `reset_targets`, a second resize, that a drain
precedes it; **not** the view calls.

### Task 3: the front end

One commit (two if the held-surface work is cleaner apart). Lane `jt3` at the tip: every row of every launch
green strict under core; `reset` under core with the validation layer: reports 0.

### Task 4: gates

`jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Mutants (final cells from the rows as committed;
R3 gives each one's wider reach, written into `mutants-expect.txt` before any run):
  a. a `Reset` that keeps the old back buffer → `rs-size`, and `rs-twice`, `rs-msaa`, `rs-held`;
  b. the automatic depth-stencil not re-made → the rows whose device has one (`rs-depth` at least);
  c. a refused `Reset` that does not set `NOT_RESET` on the core → `rs-blockers`;
  d. the targets not put back to the implicit ones → `rs-state` (its draw), `rs-autogen`;
  e. **C4c-5's open-bracket mutant re-cut on this base**: `d3d9-check.sh` then prints "queries: no core
     device, or a refusal or can't-present line" with the device's "END was not recorded" line and the verdict
     `FAIL queries-runtime`: the first run that shows that gate red (owed since C4c-6a). The VM gate's
     same line stays unproven by a run (no mutant runs in the VM): stated.
- No probe: no file of the draw path changes (`rt/src/device/draw.rs`, `src/device/core_draw.rs`).

### Task 5: review. Task 6: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none intended.
- **New pub items:** `CoreDevice::resize`, `retarget` (or one function), `reset_targets`; a clearer for the
  front end's `core_auto_depth`. **New state:** none intended.
- **Not silent:** the two refusals by name; a failed re-target answers as a failed `Reset`.
- **Divergences, each stated:** no device-lost answer from the GPU (R6c-2); no fullscreen (R6c-3); a held
  back buffer of an extended device is inert after `ResetEx` where mtld3d keeps its texture (R6c-4); a
  back-buffer format the core cannot make is refused where mtld3d warns and goes on (R6c-5).

## Rulings (planning)

- **R6c-1. A `Reset` resizes in place and keeps its view and presenter; only a change of window makes a new
  view.** The first form of this ruling re-attached at every `Reset`; R2 showed that leaks a client surface
  and a content view each time on the Mac. Cost: about 25 lines more in d3d9-rt than a re-attach.
- **R6c-2. No GPU device-lost latch on the core**: only the front end's own `NOT_RESET`. Cost: a GPU failure
  under core shows as wrong frames, not as `D3DERR_DEVICELOST`. A finding for the maintainer.
- **R6c-3. Fullscreen is not in this slice**; a `Reset` into it is refused by name after mtld3d's validation.
  It needs the window subclass, which is C4d's window work or a slice 6d: **the maintainer's decision, asked
  2026-11-19**. Until then `test_swapchain`, `test_reset_fullscreen` and parts of four more keep lines.
- **R6c-4. A held back buffer of an extended device is inert after `ResetEx`.** Keeping its old texture alive
  under an id of its own would make the back buffer a store texture with two owners. Cost: an application
  that reads or draws its old back buffer after `ResetEx` gets an answer where mtld3d gives pixels.
- **R6c-5. A `Reset` to a back-buffer format the core cannot make is refused by name**, as its `CreateDevice`
  refuses it.
- **P-7. No probe.**

## Stated gaps

Which window a re-targeted device presents into; the VM gate's "was not recorded" line by a run; a lock or a
device context held across a `Reset`; a second thread; the cost of many resizes. `d3d9ex`'s later functions
are measured, not predicted in detail.

## Review dispositions

Three reviews, 2026-11-19: R1 design (1 Critical, 6 Important, 9 Minor), R2 recorder (1, 3, 7), R3 execution
(1, 7, 11). The three Criticals changed the design: **R2's** (a re-attach at every `Reset` leaks on the Mac:
R6c-1 reversed to a resize in place); **R1's** (an extended device's held implicit surfaces were never
detached on the core path: the held-surface terms kept, R6c-4, `rs-held`); **R3's** (nothing can tell which
window a `Reset` attaches to: stated as a gap and given to the final review, since the fakes record no view
call and a row sees only a commit). Folded as cited: the recorder call in `reset_targets_to_implicit` that
would panic and its row; the front-end writes inside the replaced helpers; the automatic depth-stencil's
fresh id and its clearing; where the refusals by name sit and that they do not set `NOT_RESET`; the format
refusal; the Goal's two kinds of rejection; the rows' cells and the four new rows; the mutants' reach; mutant
e's exact line; `d3d9ex`'s predicted rise; the job scripts' `head.sh`. Held by the reviews from the code:
`drain()` suffices before a resize; a refused `Reset`'s commit is safe; queries are no blockers; gamma and
the teardown steps touch no recorder. Decided against a reviewer: none.

## Execution notes

Executed 2026-11-19. DXMTRS `c4c`: 69d92552 (`resize`, `retarget`, `reset_targets`), 4166a54a (the front end;
the tip). MacNeutron `c4c-mn`: 65cf3bbc (the rows). Artefacts: `build/c4c/s6c` (reviews, `rc*.log`, `gates/`,
`sweep.md`); the map is `build/c4c/s6/map.md`.

- **Corrections to this plan's text, as built**: a re-target that fails is a warning and the `Reset` still
  answers `D3D_OK`, as mtld3d's own re-target does (the mechanism's sentence saying it fails the `Reset` is
  wrong); `core_auto_depth` is cleared by a `take()` inside `device.rs`, with no new public clearer; no memory
  is charged for a detached surface on the core (a core device counts no standalone surface), where the plan
  said charged and refunded.
- **Task 2 as built** (69d92552; `rc2-2.log` ok, `rc2.log` red on rustfmt alone): `attach` split into
  `make_back_buffer` and `open_window`, which `resize` and `retarget` share; `resize` always makes new
  textures (the caller calls it only when the size or the count changed or a surface is held) and cannot
  fail; `retarget` stores no window (the front end compares); `reset_targets(depth) -> bool`. No field, static,
  derive or production `unsafe` added. Seven tests; the fakes show no view call, so none is asserted. Found
  beside it: `open_window` never releases a view whose layer came back null (older than this slice).
- **Task 3 as built** (4166a54a; `rc3-3.log` ok; `rc3-2.log` red on one doc title of 101 columns, shortened):
  core arms inside `retarget_device_window`, `reset_recreate_resources`, `reconcile_implicit_depth`,
  `reset_targets_to_implicit`, `detach_held_implicit_surfaces`; a private `remake_core_auto_depth`; the
  resize first, then the re-target. An inert held back buffer answers `D3DERR_INVALIDCALL` for
  `GetRenderTargetData`, `LockRect`, `UpdateSurface`, `StretchRect` at either end, `ColorFill` and `GetDC`,
  and `SetRenderTarget` refuses it by name; none reaches a recorder call.
- **The run was paused once** at the maintainer's word (a presentation): the runner stopped by PID, the job
  that had just started and a queued Rust gate cancelled, the checkouts restored by the job's own trap; the
  killed `ja` was run again as `ja2`. The gate and the commit of Task 3 were then made by a script, the
  implementer having handed back its tree uncommitted.
- **Controls**, every prediction held at the first run. `jv0`: `reset` on mtld3d under the validation layer:
  reports 0, rows ok 16: the fourteen rows' expectations are mtld3d's answers. `ja2`: default and msl ok; core
  red on exactly `reset-runtime` and the fourteen rows. `jcr`: the VM's default lane ok, 14 `rs64`/`rs32`.
- **Lane `jt3`** at the tip: default and core ok, 14 of 14 on both at the first run; `reset` under core with
  the validation layer: reports 0, rows ok 16; key the base's.
- **The chain** (`jw` first, `jr`, `jc`, `jq`, then `jmb` and `jm`; 17:46 to 19:38), each job as predicted.
  `jr`: key and imports the base's, the conformance fails anew at triaged sites only. `jc`: the VM's gate ok
  on default and core, old and new WoW64, 14 `rs64`/`rs32` (15 under core). `jq`: three lanes ok, the thirteen
  launches under core with the validation layer: reports 0.
- **Mutants** (`gates/mutants-expect.txt` written before any build): a red on its seven rows and on
  `rs-window` (left unpredicted); c on `rs-blockers` at the cell named; d on `rs-ex` and on `rs-depth` and
  `rs-held` (left unpredicted); e `FAIL queries-runtime` with the lines named: **the first run that shows
  the gate scripts' "was not recorded" entry red** (the VM gate's same entry is still unproven by a run).
  **b left `rs-msaa` green where red was predicted**: with the depth-stencil not re-made at a change of
  sample count, the row's descriptions come from front-end state and its draw uses no depth test. Ruling
  J-3: the prediction overcounted; the row cannot see the automatic depth-stencil's sample count (the final
  review names the same gap); no rerun.
- **Final review** (`build/c4c/s6c/review-final.md`): 0 Critical, 0 Important, 6 Minor; "push as is". **Which
  window a re-targeted device presents into: correct in every case by its reading** (the named window, or
  the current one for none, is what `retarget` gets and what is stored; the present uses the stored one).
  Ledgered: this plan's stale sentences (above); `resize`'s doc says nothing waits on the GPU though the
  drain does; a refusal by name has already ended a plain device's state-block recording;
  `CheckDeviceState` on core logs mtld3d's latch line; the inert back buffer's `LockRect` log names a
  depth-stencil. Not covered by a row: the window; the depth-stencil's sample count; the core's targets
  after a refused `Reset`; held surfaces beyond `GetDesc`; autogen mipmaps regenerated by a `Reset`; a return
  to a window the device left (a second view there); a same-size `Reset` commits nothing.
- **Sweep** (`build/c4c/s6c/sweep.md`): the Goal's table held within two lines (`test_reset` 16 for 18;
  `test_cursor_pos` 1 to 0 unnamed); `device` 103 to 39, `visual` 55 to 51, `d3d9ex` to its end for the first
  time and 57 to 138 (predicted 120 to 170). `device` executes 18 checks fewer and skips once more:
  `test_cursor_pos` skipped in that run ("Could not warp the cursor": the host's cursor), so its line is not
  cleared, only not reached. Metal validation: the nine reports of C4c-5's sweep, none new.
- **No probe, no x64 check.**
