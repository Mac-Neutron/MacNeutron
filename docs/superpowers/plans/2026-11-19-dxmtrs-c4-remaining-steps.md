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
  devices, the open decision; and the device-lost latch, which needs a failure report from dxmt-core's queue,
  outside `src/d3d9`: owed to a slice that runs the x64 check, 7b, or dropped at the maintainer's word); the
  map of slice 6
  (`build/c4c/s6/map.md`) cut it as 6a (below's 6a and 6b together), 6c (windowed `Reset`) and 6d (fullscreen
  devices and the window subclass, or a move to C4d). **6a leaves three of the calls listed below refused**:
  `ProcessVertices` (its staged destination needs the vertex buffers' upload path; `test_sysmem_draw` asks
  for it, so 7d owes it) and the multisampled back buffer's writable lock and `GetDC` (no suite meets them;
  7d decides whether they stay refusals).
- Wine's suites under core after 6c (`build/c4c/s6c/sweep.md`): `visual` 51 new failing lines over the default
  runtime's baseline, `device` 39, `d3d9ex` 138 now that it runs to its end (it ended early in `test_reset`
  before; 65, 115 and 66 after 4d). **Fullscreen is the open decision**: what `device` keeps is fullscreen's
  or the window subclass's, and so is much of `d3d9ex`'s `test_reset`, `test_reset_ex` and
  `test_swapchain_parameters`. The x64 check
  (D3D10-12 on the Mac) is green at 7d48da31.

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

- **7a**: `SetGammaRamp`/`GetGammaRamp`, `SetMaximumFrameLatency`/`GetMaximumFrameLatency`,
  `SetGPUThreadPriority`/`GetGPUThreadPriority`, the `Ex` creators (`CreateRenderTargetEx`,
  `CreateOffscreenPlainSurfaceEx`, `CreateDepthStencilSurfaceEx`), `SetRenderState(RESZ)` (`resz_test`, 31
  lines: a depth resolve through a render state; 4d's `depth_resolve` is the mechanism).
- **7b**: the device's information and pages from winemetal (thunks 1, 22, 23: `GetDeviceInfo`,
  `AllocPages`, `FreePages` leave mtld3d.so).
  **This touches winemetal: outside `src/d3d9`, so the slice runs the x64 check itself.**
- **7c**: the cursor calls (`ShowCursor`, `SetCursorProperties`, `SetCursorPosition`) if the map shows
  they can be had without the window work of C4d; otherwise they move to C4d with a line here saying so.
- **7d, C4c's gate**: the conformance lane for `visual`, `device` and `d3d9ex` under core
  (`dxmt/mtld3d-conformance.sh` with `D3D9_RUNTIME=core`), per-site parity with the default runtime: every
  site that fails under core and not on mtld3d is fixed or triaged with its reason in
  `dxmt/mtld3d-triaged-core.txt` (empty today). The known silent differences are owed here: a volume
  texture bound where a 2D one is sampled (six validation reports in `volume_srgb_test`),
  `D3DSAMP_SRGBTEXTURE` values other than 0 and 1 (`srgbtexture_test`), `add_dirty_rect_test`'s five
  colours (a managed texture's `READONLY`/`NO_DIRTY_UPDATE` writes showing in the next draw),
  `ds_size_test`, `test_sysmem_draw`, `test_mvp_software_vertex_shaders`, `test_backbuffer_resize`.
  Also owed at C4c's end: **the x64 check once for slices 5 to 7** (maintainer, 2026-11-18) and **the
  quiet-Mac probe** of the core's draw cost that slices since C4b-3d2 left open.

Order: 6a, 6b, 6c, 7a, 7b, 7c, 7d. 6c and 7d are the two whose size is not known: 6c because `Reset` opens
code paths no suite has reached under core, 7d because the count of sites is only known once 6c lands.

**C4c is done when** the conformance lane under core passes with a triage file whose every line has a
reason, the x64 check is green at the tip, the probe is taken, and the spec's C4c paragraph says so. The run
stops there.

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
Findings from C4c-3 to C4c-5 that are not C4c's to fix (each is in a slice's execution notes):

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
  lets it run, one a draw of its three), untraced; owed at 7d.
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
