# DXMTRS C4: the remaining steps (written 2026-11-19)

The maintainer's direction of 2026-11-19: **the autonomous run stops when C4c is done; C4d, C4e and C5 wait for
a new go-ahead.** This file says where the program stands, what is left of C4c and in what order, and what
comes after it, so that a later session can pick it up from the repository alone. The outline it refines is
`2026-11-11-dxmtrs-c4-d3d9-front-end.md` ("C4 outline"); the spec is `../specs/2026-10-29-dxmtrs-design.md`.
Every slice still gets its own code map, plan, three reviews and gates: nothing below replaces those.

## Where it stands

- Done and pushed: C4a, C4b, and of C4c the slices 1, 2a, 2b, 3a, 3b, 4a, 4b, 4c, 4d (buffers and their
  draws, dynamic buffers, every read-back form, device contexts, `ColorFill`, `UpdateSurface`, all of
  `StretchRect`). DXMTRS main 7d48da31, MacNeutron main 1f895aa8. The core runtime is opt-in
  (`runtime.mode = core` on a `core-runtime` build); the shipped default is mtld3d, unchanged.
- Done since this file was first written: **C4c-5, queries** (plan `2026-11-19-dxmtrs-c4c5-queries.md`;
  DXMTRS 501d43d1). **C4c-6a** (the getters, the stubs and the presents: plan
  `2026-11-19-dxmtrs-c4c6a-getters-presents.md`; DXMTRS d8ac1c1b) and **C4c-6c** (windowed `Reset`: plan
  `2026-11-19-dxmtrs-c4c6c-reset.md`; DXMTRS 4166a54a) are done too (**6c leaves two things undone**: fullscreen
  devices, which are slice 6d by the maintainer's decision of 2026-11-19; and the device-lost latch, which
  needs a failure report from dxmt-core's queue,
  outside `src/d3d9`: owed to the slice that runs the x64 check, 7d, or dropped at the maintainer's word); the
  map of slice 6
  (`build/c4c/s6/map.md`) cut it as 6a (below's 6a and 6b together), 6c (windowed `Reset`) and 6d (fullscreen
  devices and the window subclass: **inside C4c**, the maintainer's decision of 2026-11-19, after 7a). **6a
  leaves three of the calls listed below refused**:
  `ProcessVertices` (its staged destination needs the vertex buffers' upload path; `test_sysmem_draw` asks
  for it, so 7d owes it) and the multisampled back buffer's writable lock and `GetDC` (no suite meets them;
  7d decides whether they stay refusals).
- Wine's suites under core after 6c (`build/c4c/s6c/sweep.md`): `visual` 51 new failing lines over the default
  runtime's baseline, `device` 39, `d3d9ex` 138 now that it runs to its end (it ended early in `test_reset`
  before; 65, 115 and 66 after 4d). **Fullscreen is slice 6d**: what `device` keeps is fullscreen's
  or the window subclass's, and so is much of `d3d9ex`'s `test_reset`, `test_reset_ex` and
  `test_swapchain_parameters`. The x64 check
  (D3D10-12 on the Mac) is green at 7d48da31.
- **C4c-7a is done** (plan `2026-11-19-dxmtrs-c4c7a-ex-resz-cursor.md`; DXMTRS f5604fa7): gamma, frame
  latency, the `Ex` creators, RESZ and the cursor calls (7c folded in). The sweep after it
  (`build/c4c/s7a/sweep.md`): `visual` 20 lines over the baseline, `device` 27, `d3d9ex` 62: 87 of the 109
  wait for slice 6d, 20 for 7d, 2 are triaged.
- **C4c-6d is done** (plan `2026-11-19-dxmtrs-c4c6d-fullscreen-subclass.md`; DXMTRS 28166459): fullscreen
  devices, `Reset` into fullscreen and the window subclass on the core. The sweep after it
  (`build/c4c/s6d/sweep.md`): `device` 3 lines over the baseline (the three triaged for mtld3d), `d3d9ex` 0,
  `visual` 20; `device` executes as many checks as on the default runtime. **What is left of C4c: 7d-1** (plan
  `2026-11-20-dxmtrs-c4c7d1-last-differences.md`: the 20 lines, `ProcessVertices`, the gamma ramp on a
  fullscreen device) **and 7d-2, the gate** (the conformance lane under core with
  `dxmt/d3d9-conf-compare.sh` as the per-site parity tool; the three tile-dispatch validation reports, whose
  cause, **by reading, not yet shown by a run**, is a flag of dxmt-core's tile-barrier pipeline set in the
  Rust port where the C++ had it clear (`build/c4c/s7d/tile-dispatch.md`; which Wine function draws them is
  attributed, not confirmed): outside `src/d3d9`; the batched x64 check; the quiet-Mac probe).
- **C4c-7d-1 is done** (DXMTRS 78e0ae50): under core Wine's three suites have no line over the runner's
  baseline and meet no refusal; one site under a baseline ceiling (visual.c:15686) fails 6 times against
  mtld3d's 3 (7d-2's first task). **What is left of C4c is 7d-2** (plan
  `2026-11-20-dxmtrs-c4c7d2-gate.md`).
- **C4c-7d-2 is done, and with it C4c** (2026-11-20; DXMTRS 91a0fc4c, which MacNeutron pins). The conformance
  lane under core passes and the per-site compare of the two lanes is ok with an **empty**
  `dxmt/mtld3d-triaged-core.txt` (visual.c:15686 at 3 on both: a core device's release now drains the texture
  staging pool); no Metal validation report is left (the tile barrier's flag is back at the C++'s value in
  dxmt-core); the x64 check batched for slices 5 to 7 is green (420 rows, 0 failed), the VM's D3D10-12 gate
  too. The probe was taken once (`build/c4c/s7d2/probe.md`): the core costs 1.2 to 2.3 times
  mtld3d's CPU a frame on the three benches (3.53 against 1.57 ms textured, 3.38 against 1.63 from a static
  buffer, 2.58 against 2.13 dynamic); **a cost added by C4c's slices 3 to 7 is not excluded** (below, the
  findings). **The run stops here**: nothing below "After C4c" is started
  without a go-ahead. The spec's "C4c is done" paragraph says what the core runtime does, what it refuses by
  name and where it differs from mtld3d.

## C4c, what is left

What the core still refuses by name (the literals in `src/d3d9/src` at 501d43d1), grouped as the slices
would take them. The grouping is a proposal; each slice's map decides.

### C4c-6: `Reset`, the getters, the swap chain's presents

- **6a, answers that need no new mechanism** (mtld3d answers them from plain state or constants):
  `GetCreationParameters`, `GetDisplayMode` (device and swap chain), `GetDisplayModeEx`,
  `GetAvailableTextureMem`, `GetRasterStatus` (device and swap chain), `GetClipStatus`/`SetClipStatus`,
  `GetSoftwareVertexProcessing`/`SetSoftwareVertexProcessing`, `GetNPatchMode`/`SetNPatchMode`,
  the palette calls, `ValidateDevice`, `EvictManagedResources`, `CheckResourceResidency`,
  `SetDialogBoxMode`, `SetConvolutionMonoKernel`, `ComposeRects`, the patch draws, `ProcessVertices`
  (whatever mtld3d answers: several are refusals or no-ops there too). Expected to clear most of `device`'s
  small functions (`test_get_display_mode`, `test_creation_parameters`, `test_set_palette`,
  `test_vidmem_accounting`).
- **6b, presents**: `IDirect3DSwapChain9::Present`, `Present` with rects, a dirty region or another window,
  `PresentEx`, `GetLastPresentCount`, `GetPresentStats`, `WaitForVBlank`; the multisampled back buffer's
  writable lock and `GetDC`. The one presenter and its window target are dxmt-core's: a present into
  another window or a sub-rect may need a change there (then outside `src/d3d9`: the x64 check in that
  slice).
- **6c, `Reset`**: `Reset`, `ResetEx`, `TestCooperativeLevel`, `CheckDeviceState`, the device-lost latch
  (G1 of the outline), what survives a reset and what must be released first (the default-pool rule), the
  back buffer's and the automatic depth-stencil's re-creation, queries and state blocks across it, and
  **fullscreen devices** (`CreateDevice` on a core process refuses one today, which keeps
  `test_occlusion_query` and others from running). `Reset` is what ends `d3d9ex` early (and probably what
  `test_swapchain_parameters`'s 62 lines wait for: not traced), so this slice moves the sweep most and will
  uncover sites nobody has seen under core.

### C4c-7: the rest, and C4c's gate

- **7a (done, with 7c)**: `SetGammaRamp`/`GetGammaRamp`, `SetMaximumFrameLatency`/`GetMaximumFrameLatency`,
  `SetGPUThreadPriority`/`GetGPUThreadPriority`, the `Ex` creators (`CreateRenderTargetEx`,
  `CreateOffscreenPlainSurfaceEx`, `CreateDepthStencilSurfaceEx`), `SetRenderState(RESZ)` (`resz_test`, 31
  lines: a depth resolve through a render state; 4d's `depth_resolve` is the mechanism).
- **7b**: the device's information and pages from winemetal (thunks 1, 22, 23: `GetDeviceInfo`,
  `AllocPages`, `FreePages` leave mtld3d.so).
  **This touches winemetal: outside `src/d3d9`, so the slice runs the x64 check itself.**
  **What the 7a map found (`build/c4c/s7a/map.md`, sections 3 and 7), put to the maintainer:** the slice
  cannot do what this line says inside C4c. The logger's thunks (`InitLogger`, `OpenLog`, `WriteLog`) reach
  `mtld3d.so` on every core run whatever 7b does (their move is C4d's); `GetDeviceInfo` can be answered on
  the PE side from winemetal's existing exports with no file outside `src/d3d9`; memory pages on the Mac
  have no winemetal call (a new export, five files outside `src/d3d9`). **The maintainer's decision of
  2026-11-20: all of 7b moves to C4d**, with the logger's move.
- **7c (done in 7a)**: the cursor calls answer as mtld3d; the window subclass, which keeps the Direct3D
  cursor at `WM_SETCURSOR`, comes with 6d.
- **7d, C4c's gate**: the conformance lane for `visual`, `device` and `d3d9ex` under core
  (`dxmt/mtld3d-conformance.sh` with `D3D9_RUNTIME=core`), per-site parity with the default runtime: every
  site that fails under core and not on mtld3d is fixed or triaged with its reason in
  `dxmt/mtld3d-triaged-core.txt` (empty today). The known silent differences are owed here: a volume
  texture bound where a 2D one is sampled (six validation reports in `volume_srgb_test`),
  `D3DSAMP_SRGBTEXTURE` values other than 0 and 1 (`srgbtexture_test`), `add_dirty_rect_test`'s five
  colours (a managed texture's `READONLY`/`NO_DIRTY_UPDATE` writes showing in the next draw),
  `ds_size_test`, `test_sysmem_draw`, `test_mvp_software_vertex_shaders`, `test_backbuffer_resize`.
  From 7a's map and reviews, also 7d's: `ProcessVertices` needs one front-end arm (its staged destination
  marks dirty and answers `D3D_OK` in place of the two recorder calls); `srgbtexture_test`'s cause is read
  (`core_draw.rs` tests `!= 0` where the shared predicate tests the low bit); a multisampled RESZ between
  two pixel formats (skipped on core, converted by mtld3d); RESZ into a dynamic depth texture (refused);
  one mtld3d control (a one-sample D24X8 source into a cleared INTZ: does mtld3d zero it?); a kept scratch
  for the stencil-format multisampled RESZ; **the gamma ramp applied on a fullscreen core device** (it
  leaves `src/d3d9`, and 7d runs the x64 check anyway); the stale `StretchRect` refusal name; a re-baseline
  against Wine 11.20 before the per-site parity.
  Also owed at C4c's end: **the x64 check once for slices 5 to 7** (maintainer, 2026-11-18) and **the
  quiet-Mac probe** of the core's draw cost that slices since C4b-3d2 left open.

Order: 6a, 6b, 6c, 7a (with 7c's cursor calls folded in: the 7a plan's ruling R7a-1), **6d** (fullscreen
devices, `Reset` into fullscreen and the window subclass on the core: 87 of the sweep's lines, and the
cursor's `WM_SETCURSOR` re-assert 7a leaves out), 7d (7b moved to C4d). 6c, 6d and 7d are the ones whose
size is not known:
6c because `Reset` opens
code paths no suite has reached under core, 7d because the count of sites is only known once 6c lands.

**C4c is done when** the conformance lane under core passes with a triage file whose every line has a
reason, the x64 check is green at the tip, the probe is taken, and the spec's C4c paragraph says so. The run
stops there. **Met on 2026-11-20** (the triage file is empty; the probe taken once, a cost added by slices 3 to 7 not
excluded).

## After C4c (not started without a go-ahead)

From the outline, unchanged in scope; sizes are guesses from the pace so far (about nine hours a sub-slice).

- **C4d, policies, presenter options, logging, window** (about 5 to 8 days): async compile with skipped
  draws and pipeline prewarm as core policies (the compile scheduler lifted from d3d11 into the core, shared);
  D3D9's shaders in DXMT's cache under the translator key; inverse tone mapping and `present.maxFps` as
  presenter options; the Logger's move and the config and crash-report convergence (spec §7); the HUD and
  perf lines into `DXMT_STATS`; the window features (cursor overlay, view parking, client-surface present,
  EDR and occlusion observers) as d3d9-only winemetal slots; the native arm64 half. **Most of it changes
  `src/dxmt` or winemetal**: each slice runs the x64 check (27 minutes, the Mac left alone) and the VM's
  D3D10-12 gates, which C4c's slices did not need. The core's draw cost against mtld3d (22 to 35% more CPU
  on textured draws at the last probes, more on lit ones) is settled here, by the dedup the earlier slices
  deferred to it.
- **C4e, parity and the flip** (about 2 to 4 days, plus the maintainer's): per-site parity on every leg
  (Mac, VM, 64- and 32-bit); a 32-bit D3D9 title in the VM, **which needs the maintainer's go-ahead to run a
  game**; the benches against mtld3d, once each; the option on in the shipped build; the default flipped
  to core; `d3d9_triangle`'s `module` row changed for C5. A release is the maintainer's decision.
- **C5, mtld3d retires** (about 2 to 3 days): `mtld3d.dll`, `mtld3d.so`, mtld3d's transport and wire, the
  `mtl` facade and C3's interim pieces go, `d3d9-rt` merges into `d3d9`; the mtld3d-only tests and gate
  lanes go with them.

## Waiting on the maintainer

Decisions: the go-ahead for C4d and after; a game run in the VM (C4e); the benches and the release (C4e).
**A windowed core device's back buffer does not follow its window** (the 6d plan's ruling R6d-3): proposed as
a slice 6e, else C4d's; **asked twice, not answered, so by the stated default it is C4d's** and was not
started. **From 6d, for C4d's presenter work**: a fullscreen mode below the desktop's is magnified
with the nearest-neighbour filter under emulated mode sets (R6d-5); a fullscreen core device presents into a
window another application covers; the gates' one covering row covers the display three times a launch. The
cursor on core without the window subclass (7a's ruling R7a-7): closed by slice 6d.
**Left undone inside C4c, each a decision**: the device-lost latch (no GPU failure latches a device-lost
answer under core: it needs a failure report from dxmt-core's queue); the multisampled back buffer's writable
lock and `GetDC` (refused by name; no suite meets them); 7b (the log, the device's information and the pages
still come from `mtld3d.so` under core: C4d, the decision of 2026-11-20).
Findings from C4c-3 to C4c-7d-2 that are not C4c's to fix (each is in a slice's execution notes):

- From C4c-7d-2. **Shared with D3D10-12**: the tile barrier's flag was a difference the Rust port introduced
  (fixed); no D3D10-12 program in the gates dispatches the barrier, and every run here saw 32x32 tiles, so
  whether a 16x16 dispatch is valid under a smaller tile size is not known; the barrier is armed once a pass
  (upstream's design); when the tile pipeline fails to build, a fake handle is used (older than C4c);
  dxmt-core folds a `Clear` into a pass that samples its own target (a difference from mtld3d, recorded).
  **The conformance runner**: a baseline "ceiling" hides a count that differs between the runtimes
  (visual.c:15686 failed 6 times against 3 through all of C4c and no sweep showed it: the compare tool does
  now); the baseline is older than Wine 11.20 (three `device` sites are triaged for it). **Both runtimes**: a
  texture's staging is not zeroed at creation, where Wine zero-fills (`test_fetch4` depends on allocator
  contents; a fix is written up in `build/c4c/s7d2/fetch4.md`, one memset a level, cost not measured); ATI1
  is sampled with alpha one where Wine replicates red (the three failures left at visual.c:15686, on both
  runtimes).
- From C4c-7d-1 and 7a, the core under D3D9 (STATUS.md has each): `ProcessVertices` with a stream stride below
  the FVF's vertex size on a directly mapped source aborts the process (an application error on both
  runtimes; mtld3d reads its page padding); each change of the gamma ramp makes the next present wait for the
  queued frames and rebuild the presenter's pipelines (a game that fades through the ramp stalls each frame),
  and no gate sees a ramp on screen; the frame latency bounds the queue without the display's pace
  (`present.maxFps` is C4d's); `GetAvailableTextureMem` does not count `CreateRenderTarget` and
  `CreateDepthStencilSurface` surfaces; a multisampled RESZ between two pixel formats is skipped (mtld3d
  converts it); `cursor.scale` and `cursor.software` are not followed.
- **The core's draw cost, for C4d's first probe**: on the textured bench its CPU a frame has read either about
  2.2 ms (2.05 to 2.57) or 3.3 to 4.1 since C4b-3d, mtld3d's 1.47 to 1.93 throughout; the cause of the two
  bands is not established and the Mac's load does not explain it. C4c's closing probe read 3.53 (`tex`) and
  3.38 (`buf`) where C4c-2b's tip had read 2.32 and 2.63 in another window: **a cost added by slices 3 to 7
  is not excluded**. The run that tells: C4c-2b's tip (DXMTRS 21d0ca5f) against C4c's (91a0fc4c), `tex` and
  `buf`, in one window; then a profile, before the dedup.
- The core lane's logs hold two error lines "no Metal view for window" (`visual`, `d3d9ex`; in every sweep
  since 6d, no failing site with them): not traced.

- mtld3d, from C4c-7d-1's control: a one-sample RESZ from a D24X8 depth-stencil leaves an INTZ destination
  reading zero (the core leaves it as it was); texture staging is not zeroed at creation on either runtime
  (Wine zero-fills; `build/c4c/s7d2/fetch4.md`).
- mtld3d, from C4c-7a's rows: a RESZ with a colour texture of the depth-stencil's size at stage 0 leaves
  that texture zeroed (the copy is queued, then refused for its formats; not traced); a RESZ with the bound
  depth-stencil's own texture at stage 0 sends Metal a copy of the texture onto itself (read, not run).

- `dxmt/check.sh`: its watchdog sends TERM, which a hung test ignores, and does not reap: a hung
  `d3d12_xess.exe` spun for twelve hours on 2026-11-18, and one hang stalls every later row; the XeSS test
  hung twice in two days, untraced; its staged reference lives in `$TMPDIR`, which the system empties at
  03:35.
- An unattended gate stalled for three and a half hours on 2026-11-19 while the Mac was out of memory; the
  builder VM (16 GB) had been left running for three days. Gates now stop it after each VM job.
- mtld3d: a scaled `StretchRect` into the bound target right after a counted draw is counted by an
  occlusion query (the core does not count it, as D3D9); a `READONLY` lock of a re-created `WRITEONLY`
  backing; `test_cursor`'s device.c:1945 flake in the default conformance; the startup-failure tests'
  deadline, widened from 5 s to 30 s in C4c-4d after five red gate runs (what slowed them is untraced).
- The core under D3D9, found by C4c-5's sweep: a draw that samples the texture bound as its target draws a
  Metal validation report from the pass barrier ("threadsPerTile (16, 16) must equal tile size (32, 32)":
  three reports attributed to `test_sample_attached_rendertarget` by elimination: new with the slice that
  lets it run, one a draw of its three). **Closed by 7d-2**: the flag of the tile barrier's pipeline.
- dxmt-core (shared with D3D10-12): a span whose END sits alone in a chunk is never issued (settled in
  d3d9-rt for D3D9; the shape exists for D3D11); a draw pass with no query active, merged behind a
  counting pass, counts its draws (D3D9's passes now start with counting off); `render_pipeline` drops
  Metal's error text; two Metal constants restated in d3d9-rt (`stretch.rs`, `depth.rs`).
- The core under D3D9: the back buffer of a single-sample device is not zeroed at creation; a full
  back-buffer lock aborts when its page cannot be allocated; a dynamic buffer's guard page and a second
  thread's `Lock`; a scratch texture is made at each call that needs one (a cache later); extended-device
  paths have no row.
- From C4c-6c: d3d9-rt's `open_window` never releases a Metal view whose layer came back null; a `Reset` back
  to a window the device left makes a second view on it (by a reading of the window driver, each view made on
  a window leaves a client surface until the window is destroyed); a lock or device context held across
  `ResetEx` on the old back buffer has no row; no row sees the automatic depth-stencil's sample count.
- The build: the code-signing timestamp service drops out for hours (`make wine-arm64` then fails in
  codesign: retry); `d3d9-check.sh`'s "shaders, state" message fires for any launch.

## How to resume

- The ledger, `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md` (outside the repository's tracked files),
  holds every result, ruling and SHA in order; each slice's plan in this folder ends with its execution
  notes.
- Worktrees `build/c4b3c/dx` (branch `c4c`) and `build/c4b3c/mn` (branch `c4c-mn`); a slice's artefacts in
  `build/c4c/s<slice>` (map, reviews, gate logs, job scripts, sweep); the next slice's job scripts are copied
  from the previous slice's and checked by row counts, launch lists and tokens, not only by a string guard.
- The process a slice follows, and the rulings that changed it (recorder work and the next slice's planning
  beside a slice's gates; the x64 check batched; mutants' expected cells written before any is built), are in
  the 4b, 4c and 4d plans.
