# DXMTRS C4c-6c: `Reset` on D3D9's core runtime (windowed)

Slice 6 of C4c, second part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s6/map.md`, sections 3, 4, 6 and 7 (cited "map 3.1 step 17", "hazard 4" ...); written for all of
slice 6 at DXMTRS 501d43d1, so line numbers moved by C4c-6a's commits. The 5 and 6a plans hold the rules this
one repeats by name only. Reviews: `build/c4c/s6c/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

**Status: draft, before its reviews.** Written 2026-11-19 with the week's usage near its stop; the reviews
start after the weekly reset.

## Goal

Under `runtime.mode = core`, `Reset` and `ResetEx` of a **windowed** device, `TestCooperativeLevel` and
`CheckDeviceState` give mtld3d's answers (map 3.1, 3.3):

- A valid `Reset` re-creates the back buffer and the automatic depth-stencil at the new size, format and
  sample count, moves the device to another device window when one is named, puts every state at its default
  (an extended device keeps its state, as on mtld3d), binds the implicit targets again, and answers `D3D_OK`.
- mtld3d's own validation decides what is refused and how: invalid present parameters
  (`D3DERR_INVALIDCALL`, state unchanged); a default-pool resource or an implicit surface still referenced
  on a plain device (`D3DERR_INVALIDCALL`, **the device then needs a reset**: `TestCooperativeLevel` and
  every present answer `D3DERR_DEVICENOTRESET` until a `Reset` succeeds).
- `TestCooperativeLevel`: `D3DERR_DEVICENOTRESET` while a reset is needed, else `D3D_OK` (an extended device:
  always `D3D_OK`). `CheckDeviceState`: `D3D_OK`.

Still refused after it: fullscreen devices and a `Reset` into fullscreen (C4c-6d or C4d: ruling R6c-3), and
C4c-7's calls.

Wine: `Reset` is what most of the remaining lines wait for. By my sweep tool's counts after C4c-6a
(`build/c4c/s6/sweep.md`; the map's table differs for two functions and is not used): device
`test_swapchain_parameters` 62, `test_wndproc_windowed` 7, `test_reset_resources` 6, `test_swapchain` 4,
`test_lockable_backbuffer` 3, `test_scissor_size` 2, `test_reset_fullscreen` 2, `test_scene` 1,
`test_swapchain_multisample_reset` 1; visual `test_backbuffer_resize` 3, `test_managed_reset` 1; d3d9ex
`test_reset` 54 and the suite's early end. **Which of these clear, and which keep lines that are fullscreen's
or the window subclass's, is review 3's table, written into this section before any run**; and every one of
these functions runs further than any suite has under core, so new sites are expected, not predicted.

## Global Constraints

The 6a plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-6a pushed** (DXMTRS d8ac1c1b, MacNeutron acb8898c); job scripts in
`build/c4c/s6c/gates`, copied from s6's and edited before their first run (guard and positive checks as
before; VM state names `c4c6c-*`); the builder VM stopped after each VM job; `memory_pressure` printed before
a long job; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` changes.** A GPU device-lost report would need dxmt-core's queue (map 3.3,
  headline 6): not taken (ruling R6c-2).
- mtld3d's `Reset` is unchanged: the core's branches sit inside `reset_impl` beside the recorder calls they
  replace (map 3.2), not in a second Reset.
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`.

## The mechanism

### d3d9-rt

- **`CoreDevice::reattach(...)`** (name to settle in review): what a successful `Reset` needs of the
  recorder, in one call: `drain()` (everything recorded is submitted and done), the current window dropped
  (its presenter first, then its Metal view, as the `Window`'s drop does), then **`attach` again** with the
  new window, size and sample count: a new back buffer, its multisampled companion, a new view and presenter.
  `attach` already does all of it at device creation, so no second code path sizes a back buffer (ruling
  R6c-1; the map's option was a `resize` that keeps the view). To confirm in review 2: that a view can be
  made again on a window whose earlier view was just released; that nothing recorded before the drain still
  names the old back buffer or companion; what the read-back pages and scratch state sized to the old back
  buffer do (hazards 2, 3).
- **The automatic depth-stencil**: re-created by the front end as it creates it at device creation
  (`forget_texture` of its id, which drops a binding to it, then `texture_create` under the same id with the
  new size and count, then `set_depth`), or forgotten when the new parameters ask for none (map 3.1 step 17).
- **`CoreDevice::reset_targets()`**: target 0 the back buffer, the extra targets none, the depth-stencil the
  automatic one or none (map 3.1 step 22).

### Front end: core branches inside `reset_impl`

Each replaces one recorder-bound step of mtld3d's `Reset` (map 3.1's numbering); everything else in
`reset_impl` is front-end state and runs as it is.

- Step 4, `encoder_status()`: skipped on a core device (no latch: R6c-2).
- Steps 7, 8, 10 and the two failure helpers (`reject_reset`, `fail_reset`): the core's form commits what is
  recorded, sets `NOT_RESET` on a plain device, applies the defaults mtld3d applies, and returns mtld3d's
  answer; no flush call, no frame reseed.
- Step 15, the "resized" test: a core form (the size, the format's sample count or the window changed), since
  the handle it tests is always null on a core device.
- Steps 16 to 18: `reattach` when resized or re-targeted, the automatic depth-stencil re-created or dropped,
  `commit()` alone when nothing changed.
- Step 20, the frame reseed: skipped.
- Step 22, the targets: `reset_targets_to_implicit`'s two recorder calls guarded (they fire only for state a
  core device may or may not set: map 9, to settle), and `reset_targets()` on the core.
- Steps 19 and 21, gamma: left as mtld3d's code if it is recorder-free on a core device, else guarded and
  named for C4c-7a (map 9: not read).
- A request for fullscreen (`Windowed = FALSE`) on a core device: refused by name with
  `D3DERR_NOTAVAILABLE`, before anything changes (R6c-3).
- `TestCooperativeLevel` and `CheckDeviceState`: core forms ahead of `encoder_status()` (map 3.3). The
  presents already answer `D3DERR_DEVICENOTRESET` through mtld3d's own entry code (C4c-6a).
- Queries across a `Reset` (hazard 2): a query is no blocker on mtld3d; on the core an open bracket stays
  open across the drain and its END is recorded when the application issues it. What mtld3d answers for a
  query issued before a `Reset` and read after it is read from its code by review 1 and judged by a row.

## Review Focus (for the final review)

- Every path of `reset_impl` on a core device: no recorder call; the order drain, drop, attach; what a
  failure leaves behind at each point (a `Reset` that fails after the old window is dropped).
- Old-size state that outlives a `Reset`: the read-back pages, scratch textures, the viewport and scissor,
  a held implicit surface on an extended device (hazard 4), bound textures.
- `NOT_RESET`: set and cleared exactly where mtld3d sets and clears it.
- mtld3d unchanged; nothing outside `src/d3d9`.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`reset`** of `d3d9_triangle` with a device and a window of its own a row (a `Reset` changes
the device the launch's other rows would share), judged in every lane; the expectations are mtld3d's
answers, read from its code and confirmed by the control. Rows (the list is review 1's to correct):

1. `rs-size`: `Reset` to another size: `D3D_OK`; the back buffer's description, the default viewport and
   scissor at the new size; a Clear and a quad read back at the new size; a `Reset` with a zero size takes
   the window's client size.
2. `rs-depth`: the automatic depth-stencil at the new size and format, a depth-tested draw; a `Reset` that
   drops it (`GetDepthStencilSurface` then `D3DERR_NOTFOUND`) and one that brings it back.
3. `rs-msaa`: one sample to four and back: a draw, the resolve at present, the read-back.
4. `rs-blockers`: with a default-pool texture alive, `Reset` is `D3DERR_INVALIDCALL`;
   `TestCooperativeLevel` then `D3DERR_DEVICENOTRESET`, a `Present` too; the texture released, `Reset`
   `D3D_OK`, `TestCooperativeLevel` `D3D_OK`; the same for a render target, a vertex buffer, a held back
   buffer surface; a managed texture and a query are no blockers.
5. `rs-state`: render states, a bound texture, a stream source, the viewport and a bound target are at their
   defaults after a `Reset`; a managed texture's content survives it (drawn before and after).
6. `rs-scene`: a `Reset` inside `BeginScene` ends the scene (`EndScene` then `D3DERR_INVALIDCALL`).
7. `rs-invalid`: invalid present parameters: `D3DERR_INVALIDCALL`, the device still usable and not in need
   of a reset.
8. `rs-window`: a `Reset` naming another device window: presents (judged as C4c-6a judges a present) and a
   draw read back.
9. `rs-ex`: an extended device: `ResetEx` keeps the state a plain `Reset` clears and is not blocked by a
   default-pool resource; `CheckDeviceState` `D3D_OK`; `TestCooperativeLevel` `D3D_OK`.
10. `rs-query`: an EVENT and an OCCLUSION query issued before a `Reset` and read after it.
11. `rs-twice`: several `Reset`s in a row at changing sizes, each followed by a draw and a present (the views
    and presenters do not pile up: the launch's log holds no "no Metal view" line).

- Scripts and job scripts: as C4c-5 registered `queries` (the launch in `d3d9-check.sh` with the "none"
  refusal list, the VM gate's loop and `runtime core` names, the guest script's 64- and 32-bit lines; a
  `RESET_ROWS` counter in the job scripts, `jv0` on the new launch, `jq`'s list thirteen launches).
- Controls after the reviews, in this order: `jv0` (the launch on mtld3d under the validation layer, every
  row; a red cell is a wrong expectation or an mtld3d finding, settled in a second row commit before any
  code), `ja` (core red on exactly the launch's rows and its `-runtime`), `jcr` (the VM's default lane).

### Task 2: `reattach` and `reset_targets` in d3d9-rt. Task 3: the front end

One commit each. Unit tests on the fakes for Task 2 (the order of the drain, the drop and the attach; the
targets after `reset_targets`; a second `reattach`). Lane `jt3` at the tip: every row of every launch green
strict under core; `reset` under core with the validation layer: reports 0.

### Task 4: gates

`jmb`, then the chain (`jw` first: these suites run further than ever under core, and a panic or a hang there
blocks), `jr`, `jc`, `jq`, `jm`. Mutants (final cells from the rows as committed):
  a. a `Reset` that keeps the old back buffer (no `reattach`) → `rs-size`;
  b. the automatic depth-stencil not re-created → `rs-depth`;
  c. a refused `Reset` that does not set `NOT_RESET` on the core → `rs-blockers`;
  d. the targets not put back to the implicit ones → `rs-state`;
  e. **C4c-5's open-bracket mutant re-cut on this base** (the front end's Release arm removed), judged by
     `d3d9-check.sh`'s own `queries-runtime` token: the first run that shows the gate scripts' "was not
     recorded" line red (owed since C4c-6a).
- No probe unless a draw file changes.

### Task 5: review. Task 6: spec, pin, push

As the 6a plan's.

## Rules check (to complete after the reviews)

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none intended.
- **New pub items:** `CoreDevice::reattach`, `CoreDevice::reset_targets`. **New state:** none intended.
- **Not silent:** a `Reset` into fullscreen is refused by name.
- **Divergences, each stated:** no device-lost answer from the GPU (R6c-2); no fullscreen (R6c-3).

## Rulings (planning)

- **R6c-1. A `Reset` re-attaches** (drain, drop the window, `attach` anew) instead of resizing in place.
  Cost if wrong: a view and a presenter made at each `Reset` (rare, but a title that resets on every window
  resize pays it), and if a view cannot be re-made on a window in time, the map's `resize` (about 40 lines)
  replaces it.
- **R6c-2. No GPU device-lost latch on the core**: only the front end's own `NOT_RESET`. dxmt-core reports no
  command-buffer failure, and adding one changes the shared queue (the x64 check, D3D10-12's gates). Cost: a
  GPU failure under core shows as wrong frames, not as `D3DERR_DEVICELOST`. A finding for the maintainer.
- **R6c-3. Fullscreen is not in this slice.** It needs the window subclass (focus loss, the cover's
  re-assertion, the auto-resize, which is recorder-bound on mtld3d: map 4), which is C4d's window work or a
  slice 6d of its own; **to put to the maintainer with this slice's report**, since it decides whether C4c's
  conformance gate can pass without a triage line for every fullscreen test.
- **P-7. No probe** unless a draw file changes.

## Stated gaps

(after the reviews)

## Review dispositions

(after the reviews)

## Execution notes

(after execution)
