# DXMTRS C4c-6a: the getters, the stubs and the presents' answers on D3D9's core runtime

Slice 6 of C4c, first part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s6/map.md` (cited "map 1.A", "map 5.3" ...), which covers all of slice 6; this plan takes its
groups A and B. The 4d and 5 plans hold the rules this one repeats by name only. Reviews:
`build/c4c/s6/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core`, the calls mtld3d answers from plain state or constants, and the presents, give
mtld3d's answers instead of a refusal by name. Nothing new is built: **mtld3d's own bodies run where they
touch no recorder, and the presents share the core's one present.**

- **Getters** (map 1.A): `GetCreationParameters`, `GetDisplayMode` (device and swap chain),
  `GetDisplayModeEx` (device and swap chain), `GetAvailableTextureMem`, `GetRasterStatus` (device and swap
  chain), `GetLastPresentCount`, `GetPresentStats`.
- **Stubs and obsolete calls** (mtld3d's answers, whatever they are): `SetDialogBoxMode`, `SetClipStatus`,
  `GetClipStatus`, the four palette calls, `SetSoftwareVertexProcessing`, `GetSoftwareVertexProcessing`,
  `SetNPatchMode`, `GetNPatchMode`, `DrawRectPatch`, `DrawTriPatch`, `DeletePatch`,
  `SetConvolutionMonoKernel`, `ComposeRects`, `GetGPUThreadPriority`, `SetGPUThreadPriority`,
  `CheckResourceResidency`, `WaitForVBlank`, `ValidateDevice`.
- **`EvictManagedResources`** answers `D3D_OK` and evicts nothing (ruling G6-2).
- **Presents**: `Present` with a source rect, a destination rect, a destination window or a dirty region,
  `PresentEx`, and `IDirect3DSwapChain9::Present` present **the whole back buffer into the device's window**,
  as mtld3d does, with mtld3d's warnings for what is ignored (map 2.1; D3D11's `Present1` ignores its
  parameters too). An additional swap chain's `Present` presents the device's back buffer, as on mtld3d.

Still refused after it, each by name: `Reset`, `ResetEx`, `TestCooperativeLevel`, `CheckDeviceState` and
fullscreen devices (C4c-6c); `ProcessVertices` (ruling G6-3); the multisampled back buffer's writable lock
and `GetDC` (ruling G6-4); gamma, frame latency, the `Ex` creators, RESZ, the cursor (C4c-7).

Wine (map 5.3; "new" lines over the default runtime's baseline, after 4d's sweep): device
`test_get_display_mode` 3 → 0, `test_creation_parameters` 3 → 0, `test_vidmem_accounting` 2 → 0,
`test_set_palette` 2 → 0, `test_unsupported_shaders` 1 → 0, `test_pixel_format` 1 → 0; d3d9ex
`test_swapchain_get_displaymode_ex` 9 → 0; visual `test_mvp_software_vertex_shaders` 3 → 0,
`test_evict_bound_resources` 1 → 0, `ds_size_test` 5 → 0 to 2 (three of its lines are `ValidateDevice`'s; the
other two follow from it or not: not traced). **A function that stopped at a refused call now runs on**: a
site behind it can show for the first time, and the sweep says which.

## Global Constraints

The 5 plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-5 pushed**; job scripts in `build/c4c/s6/gates`, copied from s5's and edited before
their first run (guard: `/usr/bin/grep -nE 'c4c/s5|c4c5|C4c-5'` prints nothing; VM state names `c4c6a-*`;
the positive checks: row counts, launch lists, letters and tokens, `X64_BASE`); a rerun is a new job name;
the pipeline lock; **the builder VM stopped after each VM job; `memory_pressure` printed before a long job**;
no implementer runs `dxmt/toolchain.sh`; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`,
privacy scan (no application of this machine named in a tracked file).

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` changes** (docs aside). A destination sub-rect or another window honoured for
  real would need dxmt-core's presenter (map 2.3): not taken (ruling G6-1).
- mtld3d's behaviour is unchanged: where a guard goes, the body that then runs on a core device is the one
  mtld3d always ran.
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`.

## The mechanism

### The plain entry points

For each entry point of map 1.A marked "none" in its recorder column, the `refuse_under_core!` line goes
and nothing else changes. **Before deleting a guard the implementer reads the whole body and everything it
calls** for a recorder touch (`Mtld3dOnly` fields, `push_control`, `unix_call`, `current_frame`, `encoder`,
the flush calls, `encoder_status()`): the map's "none" is another agent's reading. A body that does touch
one keeps its guard and is reported. Two to check by name: `ValidateDevice` reads the stage bindings and the
bound textures' formats (front-end state a core device also keeps: to confirm); `GetAvailableTextureMem`
reads `vram_bytes_used`, which a core device's textures and standalone surfaces also feed (map hazard 10:
the back buffer and the automatic depth-stencil are not counted on either runtime: to confirm for mtld3d).

`EvictManagedResources`: a core arm that answers `D3D_OK` and does nothing.

### The presents

One front-end function, the core's present as `core_arms` has it today, made to take the call's flags; it
is what `Present`, `PresentEx` and the swap chain's `Present` reach on a core device.

- The rects, the destination window and the dirty region are ignored with mtld3d's own warning helpers
  (`warn_ignored_present_arguments`, `warn_ignored_present_flags`: plain logging, to confirm), then the
  present as today (the hidden-window skip included).
- `IDirect3DSwapChain9::Present`: the chain's entry takes its own lock on mtld3d; the arm must hold whatever
  lock `device_present`'s arm holds while it uses the `CoreDevice` (map hazard 6: whether the two locks are
  one object is not established; the implementer reads `com_ref.rs` and says which, and takes the device's
  when they differ). An additional chain warns once and presents the device, as mtld3d.
- **Not added here**: the `needs_reset()` answer (`D3DERR_DEVICENOTRESET`) and `state_code`: nothing can set
  either on a core device until `Reset` exists (C4c-6c takes both; map hazard 11).

## Review Focus (for the final review)

- Every guard removed: that the body it uncovers reaches no recorder call on a core device, on every path.
- The presents: one present a call; the lock held around the `CoreDevice`; the additional chain.
- mtld3d unchanged; nothing outside `src/d3d9`.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Four new rows in the `state` launch (12 → 16), judged in every lane (the expectations are mtld3d's answers,
read from its code by the implementer and confirmed by the control):

1. `state-getters`: `GetCreationParameters` (the adapter, the device type, the focus window, the behaviour
   flags the device was made with; a null pointer `D3DERR_INVALIDCALL`); `GetDisplayMode(0)` (the size and
   refresh of the desktop mode as `IDirect3D9::GetAdapterDisplayMode` reports it, the format mtld3d
   reports), `GetDisplayMode(1)` and a null pointer `D3DERR_INVALIDCALL`; the swap chain's `GetDisplayMode`
   the same mode; `GetRasterStatus` of the device and of the chain: mtld3d's answer; `GetAvailableTextureMem`
   above zero, and lower by at least 2 MiB with a 1024x1024 A8R8G8B8 default-pool texture alive (Wine's own
   test, device.c:10244).
2. `state-stubs`: each of the stubs and obsolete calls above with mtld3d's answer, its null-pointer answer
   where it has one; `ValidateDevice` with the default state (`D3D_OK`, one pass); `EvictManagedResources`
   `D3D_OK` with a managed texture bound, and a quad textured with it drawn after: its colour.
3. `state-present`: the back buffer cleared one colour with a quad of another; `Present` with a source rect
   inside the quad, then with a destination rect, a dirty region and another window (`stateHwnd`), each
   `D3D_OK`; after the first, the front buffer (`GetFrontBufferData`) shows the clear colour outside the
   quad: the whole back buffer was presented, not the rect stretched. An additional swap chain's `Present`:
   `D3D_OK`.
4. `state-ex`: a device of its own made by `Direct3DCreate9Ex` and `CreateDeviceEx`, windowed (the row
   prints "skip" where that interface is not offered): `GetDisplayModeEx` of the device and of its chain
   (the mode of row 1, the rotation mtld3d reports), `PresentEx` (`D3D_OK`, and the front buffer shows what
   was drawn), `WaitForVBlank(0)` and `(1)`, `GetLastPresentCount`, `GetPresentStats`,
   `CheckResourceResidency`, `ComposeRects`, `SetGPUThreadPriority`/`GetGPUThreadPriority`,
   `SetConvolutionMonoKernel`: mtld3d's answers.

- Scripts: the `check state` list in `dxmt/d3d9-check.sh`; the VM gate's and the guest script's `state`
  rows if they list them by name (as `blit`'s are); header prose and counts. No refusal list changes: `state`
  already expects none.
- Job scripts: `STATE_ROWS` 16 with a counter wherever `rows`, `mrows` and `qrows` are called; `jv0` runs
  `state`; the VM counters for the `state` launch's lines.
- Controls after C4c-5's push, in this order: `JOBS=jv0` (`state` on mtld3d under the validation layer:
  reports 0, rows ok 18; a red cell is a wrong expectation, corrected in a second row commit with the
  reason, or an mtld3d finding, ledgered, its cell dropped or made answer-only before any code); `ja`
  (default and msl green; core red on exactly the four new `state-*` tokens and `state-runtime`); `jcr` (the
  VM's default lane: the `state` launch's ok lines four more than at the base, 64- and 32-bit).

Commit: "dxmt, vm: d3d9_triangle rows for the getters, the stubs and the presents (DXMTRS C4c-6a)".

### Task 2: the plain entry points

One commit, "d3d9: the getters and the stubs answer on the core runtime": the guards, `EvictManagedResources`'s
arm, the docs that list what the core refuses. The report lists, for each guard removed, the body's calls
and why none is a recorder call; and each guard kept, with the call that keeps it.

### Task 3: the presents

One commit, "d3d9: Present's other forms on the core runtime". Lane `jt3` (default, core) at the tip: every
row of every launch green strict under core; no refusal line in `state`; `was not recorded` 0; `state` under
core with the validation layer: reports 0, rows ok 18.

### Task 4: gates

`jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`); no probe (no draw file changes); no x64 check.
Mutants (final cells in `gates/mutants-expect.txt` from the rows as committed):
  a. the swap chain's `Present` arm answers `D3D_OK` without presenting → `state-present`, if a cell of it
     judges the front buffer after the chain's present (Task 1 adds that cell if the harness can);
  b. `PresentEx`'s arm answers `D3D_OK` without presenting → `state-ex` (its front-buffer cell).
  A guard left in place needs no mutant: its row and `state-runtime` are red.
- `jw`: the sweep → `build/c4c/s6/sweep.md`, with the Goal's table and every function that ran further.

### Task 5: review. Task 6: spec, pin, push

As the 5 plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none.
- **New pub items, new state:** none expected (one front-end function for the three presents).
- **A duplicate:** none (the bodies are mtld3d's own).
- **Not silent:** what a present ignores is warned once, as mtld3d warns it.
- **Divergence added:** `EvictManagedResources` frees nothing on the core (G6-2).

## Rulings (planning)

- **G6-1. The presents match mtld3d** (the whole back buffer into the device's window), not D3D9's letter
  (a source rect stretched, a destination rect or another window honoured). Cost if wrong: a title that
  presents into a second window or a sub-rect shows as it does on mtld3d today; honouring a destination
  rect needs dxmt-core's presenter (the x64 check and the VM's D3D10-12 gates) and is a slice of its own.
- **G6-2. `EvictManagedResources` evicts nothing on the core**: a managed texture's core texture stays, its
  CPU copy is authoritative as before. Cost: video memory an application asked to free stays in use until
  the texture is released; `GetAvailableTextureMem` does not rise after it.
- **G6-3. `ProcessVertices` stays refused** (its staged destination needs the vertex buffers' upload path:
  map 1.A): a later slice (`test_sysmem_draw`, 5 lines, is owed at C4c-7's parity).
- **G6-4. The multisampled back buffer's writable lock and `GetDC` stay refused** (no suite meets them).
- **P-6. No probe.**

## Stated gaps

A present's rects, window and region are ignored (G6-1). The `needs_reset` answer of a present (C4c-6c).
`ValidateDevice` beyond the default state.

## Review dispositions

(after the reviews)

## Execution notes

(after execution)
