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
- **`EvictManagedResources`** answers `D3D_OK` and marks every managed texture's levels dirty, as mtld3d's
  eviction does before it destroys the GPU copies (ruling G6-2): the next draw re-publishes the CPU copy.
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
`test_evict_bound_resources` 1 → 0, `ds_size_test` 5 → 0 (all five lines are `ValidateDevice`'s: R1, R3),
`add_dirty_rect_test` 6 → 5 (R1: the line that asserts the re-publish after an eviction; not traced by me:
6 is no surprise either). **A function that stopped at a refused call now runs on**: a
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
one keeps its guard and is reported. R1 and R2 each traced every body of the Goal's lists on a core device,
independently: none reaches a recorder call or a field a core device leaves unset. **One answers less than
mtld3d**: `GetAvailableTextureMem` reads `vram_bytes_used`, which a core device's textures feed but its
standalone surfaces (`CreateRenderTarget`, `CreateDepthStencilSurface`) do not: their core paths return
before the charge (R1 I-2, R2 I-2). Stated, not fixed here (the refunds are gated on handles the core does
not have: a change of its own, owed at C4c-7's parity if a suite asks).

`EvictManagedResources`: a core arm that runs mtld3d's own marking loop (`evict_mark_dirty` over the live
managed textures: plain front-end state) without the destroy and the snapshot mark, logs once that the core
keeps its copies, and answers `D3D_OK`. The implementer confirms that a level marked dirty is uploaded again
at the texture's next use on a core device (`core_flush_dirty`), and reports it.

### The presents

**One core branch at the top of `DeviceInner::present`** (R2 M-1), ahead of its first recorder read: on a
core device it does what `core_arms`'s present does today (the device's own present parameters for the
window, the hidden-window skip and the interval; the `CoreDevice`'s present) and returns. Everything above
it is then mtld3d's own code for all three entries, in mtld3d's order: `present_impl`'s warnings for the
ignored rects, window, region and flags, its `needs_reset()` answer (a plain field nothing sets on a core
device until C4c-6c) and its `state_code`; the swap chain's entry with its once-only warning for an
additional chain. The three guards go, and the arm `device_present` has today goes with them if nothing else
needs it. The input is the device (`&mut DeviceInner`), never the caller's object: the swap chain's wrapper
and the device's have one layout, and code that took `this` from the chain's entry would read one as the
other (R2 I-3).

- An additional chain presents the device's back buffer with **the device's** present parameters (R2 M-5).
- The chain's lock is the device's own reentrant lock (`com_ref.rs`: R1, R2, R3), so no lock is added.
- The implementer reads every line of `present_impl` and of the chain's entry that now runs on a core
  device for a recorder touch, and reports them; the per-present work of `DeviceInner::present` that the
  core branch skips (frame capture polling, the cursor's, the client-surface present) is C4d's, stated.

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
   `D3D_OK` with a managed texture bound whose level was rewritten under `D3DLOCK_NO_DIRTY_UPDATE`, and a
   quad textured with it drawn after: the new texels (the eviction re-publishes them: `visual.c:19300`).
3. `state-present`: **that a present happened is judged by a query, not by the front buffer**
   (`GetFrontBufferData` reads the back buffer on both runtimes, so it cannot tell: all three reviews). For
   each form in turn, a source rect, a destination rect, a dirty region, a destination window (**a second
   window the row makes**: `stateHwnd` is the device's own) and an additional swap chain's `Present`: an
   OCCLUSION bracket around one quad, a poll without the flag that answers `S_FALSE`, then, while the poll
   answers `S_FALSE` and for at most 5 s, **the form under test and nothing else that submits** (`qy-noflush`'s
   pattern); the poll must answer `S_OK` with the quad's exact count, and each call of the form `D3D_OK`. A
   present that does nothing leaves the poll at `S_FALSE`.
4. `state-ex`: a device of its own made by `Direct3DCreate9Ex` and `CreateDeviceEx`, windowed, **on a window
   of its own** (a second Metal view on the launch's window could fail and turn `state-runtime` red); no
   "skip" form (a row that prints one is red in every lane, and the interface is always there: R3 I2):
   `GetDisplayModeEx` of the device and of its chain (the mode of row 1, the rotation mtld3d reports),
   `PresentEx` judged as row 3 judges a present, `WaitForVBlank(0)` and `(1)`, `GetLastPresentCount`,
   `GetPresentStats`, `CheckResourceResidency`, `ComposeRects`,
   `SetGPUThreadPriority`/`GetGPUThreadPriority`, `SetConvolutionMonoKernel`: mtld3d's answers.
   `GetAvailableTextureMem` is not asked of this device.

- The rows go after `state-fftexoff` in the table. Scripts: the `check state` list in `dxmt/d3d9-check.sh`
  and the VM gate's loop for the `state` launch, 64- and 32-bit (**a row missing from either runs and is
  never judged: the exit status is dropped**, R3 I3); header prose and counts; the stale prose at
  `d3d9_triangle.cpp:3661` ("the core refuses GetCreationParameters"). No refusal list changes: `state`
  already expects none.
- Job scripts: `STATE_ROWS` 16 with a counter in `head.sh` called wherever `rows`, `mrows` and `qrows` are;
  the VM's `state` ok lines counted (16 default, 17 core, 64- and 32-bit); `jv0`'s loop runs `state`
  (rows ok 18); `jm.sh`'s tokens are `state-state-present` and `state-state-ex` (launch, then the row's own
  name); checked by eye beyond the string guard: `X64_BASE` (501d43d1), the tips in the runner scripts,
  `base-key.txt` and `base-imports.txt` taken from s5 once its `jr` printed "same".
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
  a. the core branch of `DeviceInner::present` returns without presenting when the call came through the
     swap chain's entry → `state-present` (its last cell waits out its 5 s);
  b. the same for `PresentEx` → `state-ex`;
  c. the same for a `Present` with a source rect → `state-present` (its first cell).
  (If the branch cannot tell its callers apart, the three become one mutant, "the core branch presents
  nothing", red on `state-present`, `state-ex` and every row that waits on a present; the implementer of
  Task 3 says which form the code allows.) A guard left in place needs no mutant: its row and
  `state-runtime` are red. Not held by any row or test, stated: one present a call (a double present), and
  that a present's rect is not stretched.
- `jw`: the sweep → `build/c4c/s6/sweep.md`, with the Goal's table and every function that ran further.

### Task 5: review. Task 6: spec, pin, push

As the 5 plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none.
- **New pub items, new state:** none expected; if the core branch in `DeviceInner::present` needs a function
  of the private `core_arms` module made visible to it, that visibility change is named in the commit.
- **A duplicate:** none (the bodies are mtld3d's own).
- **Not silent:** what a present ignores is warned once, as mtld3d warns it; the eviction's arm logs once.
- **Divergences, each stated:** `EvictManagedResources` keeps the core's copies (G6-2);
  `GetAvailableTextureMem` leaves out standalone surfaces on the core; `ValidateDevice` answers one pass for
  a state whose draw the core would refuse by name; a present on the core skips mtld3d's frame-capture
  polling and cursor work (C4d).

## Rulings (planning)

- **G6-1. The presents match mtld3d** (the whole back buffer into the device's window), not D3D9's letter
  (a source rect stretched, a destination rect or another window honoured). Cost if wrong: a title that
  presents into a second window or a sub-rect shows as it does on mtld3d today; honouring a destination
  rect needs dxmt-core's presenter (the x64 check and the VM's D3D10-12 gates) and is a slice of its own.
- **G6-2. `EvictManagedResources` marks, and destroys nothing, on the core** (R1 I-3 corrected the first
  form of this ruling, which did nothing at all): what an application can observe of mtld3d's eviction is
  the re-publish at the next draw, not memory (managed textures are charged to `GetAvailableTextureMem` on
  neither runtime). Cost: the GPU copies an application asked to free stay until the texture is released.
- **G6-3. `ProcessVertices` stays refused** (its staged destination needs the vertex buffers' upload path:
  map 1.A): a later slice (`test_sysmem_draw`, 5 lines, is owed at C4c-7's parity).
- **G6-4. The multisampled back buffer's writable lock and `GetDC` stay refused** (no suite meets them).
- **P-6. No probe.**

## Stated gaps

A present's rects, window and region are ignored (G6-1); nothing judges that a rect is not stretched or that
a call presents once. `ValidateDevice` beyond the default state. `GetAvailableTextureMem` and standalone
surfaces on the core. No Wine suite reaches `PresentEx` or the chain's `Present` under core: the rows are
their only cover.

## Review dispositions

Three reviews, 2026-11-19: R1 design (0 Critical, 3 Important, 11 Minor), R2 an independent path trace
(0, 3, 6), R3 execution (1, 3, 10). **Held by R1 and R2 independently**: no body the plan uncovers reaches a
recorder call or an unset field on a core device; the chain's lock is the device's; nothing can set
`needs_reset` on a core device yet; two presents back to back cannot deadlock. Folded as cited above: R3's
Critical (no planned cell could tell that a present happened: the query pattern replaces the front-buffer
cells, and the mutants are re-cut); the core branch inside `DeviceInner::present` and its input; the
eviction's arm; the memory figure's gap; a second window for two cells; no "skip" form; the registration
that fails silently and the job scripts' counters; `ds_size_test`'s prediction. Decided against a
reviewer: **charging standalone surfaces on the core now** (R1 I-2's first option: a change with refunds of
its own; stated instead).

## Execution notes

(after execution)
