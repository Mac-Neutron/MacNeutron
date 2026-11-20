# DXMTRS C4c-7d-1: the last differences of D3D9's core runtime from mtld3d that Wine sees

Slice 7 of C4c, second part of three (7a done; **7d-1, this one: what still differs; 7d-2: C4c's gate**, the
conformance lane under core, the batched x64 check and the probe). 7b moved to C4d (the maintainer's
decision of 2026-11-20). Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push).
Map: `build/c4c/s7d/map.md` (all of 7d, read at DXMTRS 25001b77; cited "map 3.2" ...). The 5, 6a, 6c, 7a and
6d plans hold the rules this one repeats by name only. Reviews:
`build/c4c/s7d/review-{1-design,2-core,3-exec}.md` (cited R1, R2, R3; **R1 holds mtld3d's answer for every call of
every row and the Wine table, R2 every reader of the uploaded mark and the walks of the buffer and ramp paths,
R3 the per-row table, the registration edits, the mutant table and the job-script list**). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

After C4c-6d, Wine's three suites under `runtime.mode = core` fail 20 lines the default runtime does not
(all in `visual`; `device` keeps three triaged lines, `d3d9ex` none). This slice clears the 20, and with
them the one refusal a suite still meets and six of the nine Metal validation reports (causes by the map's
reading; R1 and R2 check each):

- `srgbtexture_test`, 4 lines: the core's draw picks the sRGB view for `D3DSAMP_SRGBTEXTURE != 0`, where the
  shared predicate both runtimes key their sampler on reads the low bit. Fix: the predicate (map 3.1).
- `volume_srgb_test`, 6 lines and six validation reports: a `CreateVolumeTexture` of depth 1 is a 2D texture on
  mtld3d and a 3D one in the core's store, while both declare the shader's sampler 2D. Fix: the core texture's
  kind from the depth, as `is_volume()` (map 3.2).
- `add_dirty_rect_test`, 5 lines: the core's upload never marks a colour level as uploaded, so a `READONLY` or
  `NO_DIRTY_UPDATE` unlock always marks it dirty. Fix: the mark, as mtld3d's upload sets it (map 3.3).
- `test_sysmem_draw`, 5 lines: `ProcessVertices` is refused by name. Fix: a core arm (map 2).

Predicted: `visual` 20 → 0 lines over the baseline (its "Test failed" count 39 → 19, every one a line mtld3d
has too), `device` 3 (triaged), `d3d9ex` 0. The three tile-dispatch validation reports stay (7d-2).

Also in the slice, owed by earlier ones:

- **The gamma ramp applied on a fullscreen core device** (the 6d plan's ruling R6d-4): the presenter's
  ramp, fed as D3D11 feeds it.
- The stale name of `StretchRect`'s refusal; the two log texts 6d's final review found false on one runtime.
- One control row for 7a's ruling R7a-4 (does mtld3d change the destination of a one-sample RESZ between two
  pixel formats?).

No refusal is added: `ProcessVertices` into a default-pool dynamic buffer is served (R7d-2).

## Global Constraints

The 6d plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-6d pushed**; job scripts in `build/c4c/s7d/gates`, copied from s6d's and edited
before their first run, with the guard and the positive checks (R3 lists them) run before anything else; VM
state names `c4c7d-*`; the builder VM stopped after each VM job; `memory_pressure` printed before a long
job; each DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` and `docs/` changes** (the gamma ramp's type is named through `mtld3d-core`,
  which already depends on `dxmt-util`: map 4; if that route does not hold, the gamma commit moves to 7d-2,
  which runs the x64 check). mtld3d's answers are unchanged.
- A core log line of this slice contains none of the gates' can't-present words.
- A row that makes a fullscreen device obeys the 6d plan's machine constraint (the guard first,
  `D3DCREATE_NOWINDOWCHANGES`, the desktop's mode).
- **The x64 check does not run in this slice** (7d-2 runs it), unless the range leaves `src/d3d9` and
  `docs/`. **No probe here**: `src/device/core_draw.rs` changes by one predicate; 7d-2's quiet-Mac probe
  measures the tip.

## The mechanism

### Three predicates (map 3; R1 checked each against mtld3d, R2 every reader)

1. `core_draw.rs`: the bound view's sRGB twin is picked by `mtld3d_core::sampler_state::srgb_texture_enabled`
   (the low bit), the predicate the sampler key already uses on both runtimes, for pixel and vertex stages.
2. `texture.rs`, `core_flush_dirty`: the core texture is a volume when `depth > 1`, as `is_volume()` and
   mtld3d's upload decide; a `CreateVolumeTexture` of depth 1 is then a 2D texture in the store and its upload
   region a 2D one. No other path of the core treats it as 3D (R2).
3. `texture.rs`, `core_flush_dirty`: **a level is marked uploaded only if its dirty bit was set when the flush
   began and its upload was recorded** (R1 I-1, R2 C2: a texture's first upload carries every level, and
   marking those nothing wrote would keep a later `READONLY` first lock of such a level from the GPU, which
   mtld3d uploads: its mark follows dirty bits, and a new texture's mask is empty). Cube faces and volume
   levels the same. Two things change with the mark, both towards mtld3d: a `READONLY` or `NO_DIRTY_UPDATE`
   unlock of an uploaded level marks nothing (cubes' and volumes' `NO_DIRTY` always marks, on both runtimes);
   `EvictManagedResources` marks the uploaded levels dirty again. No reader of the mark reaches the recorder,
   and the core never releases staging (R2 M2).

### `ProcessVertices` (map 2; R2 C1, I3)

mtld3d's body and answers to its last two calls, then a core arm, every test on the buffer's map mode (not
its pool and usage):

- Source bytes: the CPU backing, or, **before mtld3d's released-backing check**, for a buffer the core maps
  directly, the range `[first, first + count)` of what the application last wrote (`CoreDevice::dynamic_copy`).
- Destination, after all of mtld3d's `D3DERR_INVALIDCALL` checks, nothing written when nothing is processed:
  - a buffer whose backing mirrors the GPU's bytes: the vertices copied in and the dirty range widened by the
    written window (never by "size 0", which means "to the end");
  - a buffer whose backing was released after an upload and is re-created zeroed (a default-pool
    `WRITEONLY` one): **the written window alone goes to the core, as a core `Unlock` of that window sends
    it** (`vb_unlock`'s own rule: widening the range there would upload zeros over the GPU's bytes);
  - a buffer the core maps directly (a default-pool dynamic one): the vertices written through the core's
    dynamic lock of the window, as mtld3d, which answers `D3D_OK` and draws it (R7d-2).
- The arm does not call `write_processed` (its page allocation and its stage upload are the recorder's);
  `D3D_OK` in place of the recorder's status.

### The gamma ramp (map 4; R1 I-2, I-3, R2 I1, I2)

- **The presenter only ever sees the 256 8-bit codes** (the back buffer is BGRA8, read exactly): so the
  1024-point lookup is filled to be **exact at every code** whatever the sampler's weights: point i takes
  the D3D9 ramp's entry `((2i + 1) * 255 + 1024) / 2048` (the code whose sample position i is nearest),
  scaled to [0, 1]. `mtld3d-core` gains that pure function, host-tested at all 256 codes against mtld3d's own
  curve, and the presenter's ramp type by name (an alias of `dxmt_util::GammaRamp`; no `Cargo.lock` change,
  no file outside `src/d3d9`: R2).
- d3d9-rt's `CoreDevice` owns **one ramp for its life**; a fill bumps its version, **only when the content
  changes** (the presenter skips a version it has seen, and every new version makes the next present wait
  for the queued frames and rebuild both of its pipelines); it is handed to the presenter before
  `synchronize` at each present while it applies, never at version 0, and `None` otherwise (the fast blit
  comes back). A new presenter (a re-target) takes the ramp at its next present by itself.
- The front end's `queue_gamma`/`reapply_gamma`, on a core device: fullscreen and not the identity → filled
  and applying; otherwise not applying. 6d's warning goes; one info line, once, when a ramp first applies
  (the gates count it: R3 I9).

### Small things

- `core_arms.rs`: the refusal's name becomes `IDirect3DDevice9::StretchRect (a pair the core does not
  copy)`: one literal (its once-only log is keyed on the string's address); no script or test names the old
  one (R2).
- The auto-resize's info line says the back buffer keeps its size, without claiming a scaling; the windowed
  `SetGammaRamp` text and the refresh-rate warning say what holds on both runtimes (6d's review m-4, m-5).
- No comment of DXMTRS cites a Wine test's name (`tools/audit.sh` refuses it: R2 M11).

## Review Focus (for the final review)

- The uploaded mark: every reader of it on a core device after the change (the lock paths, the eviction,
  the staging's release).
- `ProcessVertices`: no recorder call on a core device; the dirty range; the refusal before any write.
- The gamma ramp: who owns it, when the presenter sees it, what a `Reset`, a re-target and a windowed
  `Reset` do to it.
- mtld3d's answers unchanged.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`parity`** of `d3d9_triangle`, judged in every lane; the expectations are mtld3d's answers
(R1's table) and the control confirms them. **The texture and buffer rows run on the launch's main device
with `reset()`** (the read-back helpers they need read through the main device's surface: R3 I2); `rz-cross`
and `gm-full` have a device and a window of their own. Nine rows (R3's per-row table has the helpers):

1. `sr-lsb`: a texture whose sRGB decode shows, sampled with `D3DSAMP_SRGBTEXTURE` 0, 1, 2, 3 and
   0x7e41882a: decoded for the odd values, raw for the even ones.
2. `vo-one`: a 1x1x1 volume texture (managed; and a system-memory one sent to a default-pool twin with
   `UpdateTexture`) sampled by the fixed-function stage with three texture coordinates: its texel's colour,
   with `D3DSAMP_SRGBTEXTURE` off and on.
3. `dr-keep`: a managed texture of **two levels**. Bound and drawn before anything wrote it, then a
   `READONLY` lock writes a colour: **the next draw shows it** (the very first lock of a level uploads, on
   mtld3d and so on core: the cell mutant i turns red). Then written and drawn (uploaded); a `READONLY` lock
   writes another colour, a draw: the earlier one, **at level 0 and at level 1** (mutant h's cell); a
   `NO_DIRTY_UPDATE` lock, a draw: unchanged; `AddDirtyRect` of a part: what mtld3d uploads for it (R1: the
   rectangle); `EvictManagedResources`, a draw: the last written colours everywhere.
4. `pv-sysmem`: `ProcessVertices` of four untransformed vertices into a system-memory XYZRHW buffer:
   `D3D_OK`, and a lock reads the viewport's transform of each.
5. `pv-draw`: the destination a **default-pool `WRITEONLY`** buffer of eight vertices: `ProcessVertices` of
   the first four, a quad drawn from them (the buffer is uploaded and its backing released); `ProcessVertices`
   of four others at offset 4 (the source's FVF and stream set again), then **both quads drawn: the second
   lands (mutant d's cell) and the first is still there (mutant f's: zeros over the GPU's bytes)**.
6. `pv-dynsrc`: the source a default-pool dynamic buffer, the destination system memory: `D3D_OK` and the
   transformed vertices.
7. `pv-dyndst`: the destination a default-pool dynamic buffer: `D3D_OK` and a quad drawn from it lands.
8. `rz-cross`: (a) a one-sample D24X8 depth-stencil into a **DF24** texture (one Metal format on both
   runtimes): the copy, read back; (b) the control for 7a's ruling R7a-4: the same source into an INTZ
   texture cleared to 1.0: `D3D_OK` and the destination still 1.0. **Cell (b) is expected to fail on
   mtld3d** (its copy is queued and then refused for the formats, the route that zeroed a colour texture in
   7a's control): if the control shows it, the cell becomes strict-only in a second row commit and the
   finding is recorded; if it passes, R7a-4's "parity" stands.
9. `gm-full`: a fullscreen device (the guard, the flag, the desktop's mode): a rising ramp set and read
   back; frames drawn, read back unchanged (the read is before the ramp) and presented; **a `Reset` to
   windowed with the ramp still set, a frame, and a `Reset` back**; the identity set. No cell sees the ramp
   on screen (stated); under core the launch's log must hold the "ramp applies" line (the job scripts count
   it), and the validation layer watches the presenter's path.

- Scripts: `parity` registered as `fullscreen` is (the zero-refusal arm: no refusal is expected), with its
  `check` list; the VM gate's rows loop and kind-less core loop; the guest script's two lines.
- Job scripts (R3 section 6): a `prows` counter (9); `ja`'s validation launches are `parity` on mtld3d and
  **`parity strict` under core** (at the base it shows `vo-one`'s wrong texture type as reports: R3 I4);
  `jt` and `jq` count the "ramp applies" line in `parity`'s core log (want 1 or more); `jw` prints the
  validation reports a kind and the new failing sites' count (R3 I10); `jq`'s list sixteen launches; the VM
  counters; `jm`'s letters and tokens; new runners.
- Controls: `ja`, `jcr`. Under strict core at the base (R3's table): red `sr-lsb`, `dr-keep`, `pv-sysmem`,
  `pv-draw`, `pv-dynsrc`, `pv-dyndst` (each at its first wrong cell) and `parity-runtime` (the
  `ProcessVertices` refusal line in a launch that expects none); `vo-one` red or not (a 3D texture where a
  2D one is declared is undefined without the validation layer; with it, reports); green `rz-cross`'s DF24
  cell (R3 says, or the control does) and `gm-full`.

### Task 2: the three predicates (DXMTRS)

One commit each (three), each with STATUS.md's line where one changes.

### Task 3: `ProcessVertices` (DXMTRS)

One commit: the arm (its three destinations, the direct source), STATUS.md.

### Task 4: the gamma ramp (DXMTRS)

One commit: `mtld3d-core`'s function and its tests (every one of the 256 codes; the version moves on a
changed ramp only and is never 0 while one applies), d3d9-rt's ramp (no d3d9-rt test presents with a ramp:
its fakes have no texture write), the front end's arm, STATUS.md (the cost of a ramp change; HDR output
skips the ramp, as the presenter's does for D3D11).

### Task 5: the small things (DXMTRS)

One commit: the refusal's name, the three log texts. **One `jt` at this tip**: every row of every launch
green strict under core; `parity` under core with the validation layer: reports 0, the ramp's line present.

### Task 6: gates

`mutants-expect.txt`, `jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Five mutants (R3's table):
  d. the arm sends nothing to the core for a re-created backing → `pv-draw` (its second quad);
  e. the arm reads a directly mapped source's backing → `pv-dynsrc`;
  f. the arm widens the dirty range on a re-created backing → `pv-draw` (its first quad gone: R2 C1's bug);
  h. the uploaded mark set for level 0 only → `dr-keep` (its level-1 cell);
  i. the uploaded mark set for every level a first upload carries → `dr-keep` (its very-first-lock cell).
- **Not mutants**: the two other predicates (the control shows `sr-lsb` red at the base; `vo-one` by its
  reports); the gamma ramp (no row sees it: the pure function's test at every code, the counted log line,
  the validation layer and the final review are its cover).

### Task 7: review. Task 8: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none intended.
- **New pub items:** `mtld3d_core::gamma`'s alias and function (the presenter's point count by its constant,
  not a literal; the float casts by the workspace's lint rules); a ramp setter on `CoreDevice`. **New state:**
  the one ramp on `CoreDevice` (a second construction of the presenter's ramp type beside DXGI's: stated).
- **Not silent:** the ramp's info line; no refusal added.

## Rulings (planning)

Each with its cost if wrong.

- **R7d-1: 7d is cut in two**: this slice inside `src/d3d9`, then the gate (the lane's script, the
  tile-dispatch reports, which may need dxmt-core, the x64 check, the probe). Cost if wrong: one more
  review cycle; what it buys is that the x64 check and the probe run once, on the final code.
- **R7d-2: `ProcessVertices` into a default-pool dynamic buffer is served through the core's dynamic lock**
  (the draft refused it; R1 I-4: mtld3d answers `D3D_OK` and draws it, and the lock is there). Cost if
  wrong: the core's lock of a dynamic buffer outside an application's own `Lock` meets a state nobody
  exercised (a lock the application holds on the same buffer); `pv-dyndst` covers the plain case.
- **R7d-3: the gamma ramp is exact at the 256 codes** (the draft resampled mtld3d's curve between codes;
  R1 I-2: the presenter never sees a value between two codes, and that fill lifted black by up to 2.6 codes).
  **Its cost: each change of the ramp makes the next present wait for every queued frame and rebuild the
  presenter's two pipelines** (the presenter's design, as for D3D11): a game that fades through the ramp
  every frame stalls every frame under the opt-in runtime, where mtld3d passes the ramp with the frame. HDR
  output skips the ramp. Stated in STATUS.md; no gate sees a ramp on either runtime.
- **R7d-4: not in C4c**: the present's pace (`present.maxFps`, the requested rate), the video-memory figure
  of standalone surfaces, RESZ's two stated differences, a windowed device following its window (6d's
  R6d-3), a re-baseline of mtld3d's conformance against Wine 11.20 (the parity tool compares the two
  runtimes' runs, not the baseline). Each is in STATUS.md or the roadmap for C4d or the maintainer. Cost:
  the opt-in runtime keeps those differences.
- **P-7d1: no probe; X-7d1: no x64 check** (7d-2 runs both).

## Stated gaps

- The gamma ramp's effect has no gate.
- `rz-cross` may show an mtld3d defect; it is recorded, not fixed.
- The refusals the core keeps (map 1: the back buffer's formats, targets and depth-stencils it does not
  attach, the multisampled back buffer's writable lock and `GetDC`, RESZ into a dynamic depth texture,
  `StretchRect`'s pairs, the draw refusals) are named in STATUS.md.
- A second draw that samples its own target in one pass gets no barrier (the core's tracker arms it once a
  pass: the tile-dispatch investigation, `build/c4c/s7d/tile-dispatch.md`): not this slice's, for 7d-2's notes.

## Review dispositions

Three reviews: **two Critical (R2), both in the draft's mechanism, both folded**.

- **R1** (5 Important, 11 Minor): the four causes hold against the code; I-1 the mark follows dirty bits;
  I-2 the ramp's fill exact at the codes; I-3 the version bumped on a change only, handed before
  `synchronize`, never at 0; I-4 the dynamic destination served (R7d-2 reversed); I-5 `rz-cross`'s DF24
  cell and its INTZ cell as the control; the minors on the rows (mutant d's cell, two levels, no "size 0"
  widening, the direct source's range, the `Reset` to windowed with a live ramp). Not taken: a
  vertex-sampler sRGB cell (optional). The Wine table: every check after the first failing line passes, no
  other caller turns red: `visual` 20 → 0, `device` 3 triaged, `d3d9ex` 0.
- **R2** (2 Critical, 3 Important, 12 Minor): **C1** a re-created destination's window sent alone, never by
  widening; **C2** as R1 I-1; I1 one ramp for the device's life; I2 the cost of a ramp change stated; I3
  the map mode as the test, the direct source before the released-backing check; M3 the arm off
  `write_processed`; M5, M6 the presenter call's place and `None`; M8 no d3d9-rt present test with a ramp;
  M11 the Rules check. The depth-1 volume, the alias route and the renamed literal are clean.
- **R3** (10 Important, 9 Minor): I1 `rz-cross` (taken as a control first, strict-only after if it fails,
  not strict-only from the start: the control's answer is what R7a-4 lacked); I2 the main device for the
  texture and buffer rows; I3 the registration's model; I4 `parity strict` under the validation layer in
  `ja`; I5, I6 `dr-keep`'s two levels, its very-first-lock cell and mutant i; I7 `pv-draw` on a `WRITEONLY`
  default-pool buffer, mutants d and f; I8 superseded (no refusal); I9 the counted log line; I10 `jw`'s
  counts; its job-script list.

## Execution notes

- **Commits.** MacNeutron: rows 1b75890f (launch `parity`, nine rows), 7f792edf (`rz-cross`'s INTZ cell
  under strict alone, after the control), f12d3c68 (`pv-draw` rewritten after the final review). DXMTRS:
  1ecbc9c5, 4941a88c, 6aee94ba (the three predicates, one gate run split exactly), 5e4744c2
  (`ProcessVertices`), e4359245 (the ramp; its first gate run failed clippy on a fourth bool in
  `CoreDevice`, fixed by a struct), e5530477 (small things), 78e0ae50 (STATUS.md after the final review).
  The pin is 78e0ae50; the chain ran at e5530477, the last lane and the last mutant at 78e0ae50.
- **Controls**: as written beforehand. The core at the base: red `sr-lsb`, `vo-one` (black; four reports of
  a 3D texture's type under the validation layer), `dr-keep` (at its READONLY cell), the four `pv` rows and
  `parity-runtime`; green `rz-cross`, `gm-full`. mtld3d (Mac, the validation layer, the VM): green but
  **`rz-cross`'s INTZ cell: the destination cleared to 1.0 reads 0 after a one-sample RESZ from D24X8**.
  So 7a's ruling R7a-4 was wrong to call that pair parity: the core skips it and keeps the destination,
  mtld3d queues a copy Metal's side refuses and the destination reads zero (why: not traced). The cell went
  strict-only (7f792edf); STATUS.md's RESZ bullet has four differences; the finding is on the maintainer's
  list.
- **Lane and chain** (`jt1`, then `jmb`, `jw`, `jr`, `jc`, `jq`, `jm`): every row green on both runtimes
  (`parity` 9 of 9); `parity` under core with the validation layer: no report, the ramp's line once; the
  plain build's key and imports the base's; conformance at triaged sites only; the VM on both runtimes, both
  forms, both widths; sixteen launches under validation, no report; **mutants d, e, f, h, i red on exactly
  the cells written before their builds, no other row**.
- **Sweep** (`build/c4c/s7d/sweep.md`): as predicted: `visual` 20 to 0 over the baseline, `device` 3
  triaged, `d3d9ex` 0; no refusal line in any suite; three validation reports left, the tile dispatch.
  **The Goal's "every one a line mtld3d has too" is not quite so**: visual.c:15686 is a line mtld3d has, but
  it fails 6 times under core and 3 on mtld3d (the runner's ceiling hid the count; 7d-2's plan review found
  it; 7d-2's first task).
- **Final review** (`build/c4c/s7d/review-final.md`): no Critical; two Important, neither in the code. I1:
  `pv-draw` wrote one window after the release and so could not tell a window sent alone from one joined to
  the dirty range (review 2's C1 in its own form); mutant f was the stronger "whole buffer". The row now
  writes two windows around vertices the GPU holds (f12d3c68) and **mutant g, the window joined to the
  range, is red at "4-7 still B" and nowhere else** (`jm2`; `jt2` green on both runtimes with the new row).
  The VM ran the row's first form only. I2: STATUS.md, above. Minors recorded, not fixed: a stream stride
  below the FVF's vertex size aborts on a directly mapped source where mtld3d reads on (M1: the pure
  function's check); four edge answers that differ (M2); the refused-upload fallback joins the range, as
  `Unlock`'s own does (M3); a dynamic destination's lock may rename the buffer (M4); the direct source is
  copied to its end each call (M5); `vo-one`, `gm-full` and the cube and volume marks are covered by
  reading or the validation layer only (M6).
- **No probe, no x64 check** (7d-2).
