# DXMTRS C4c-7d-1: the last differences of D3D9's core runtime from mtld3d that Wine sees

Slice 7 of C4c, second part of three (7a done; **7d-1, this one: what still differs; 7d-2: C4c's gate**, the
conformance lane under core, the batched x64 check and the probe). 7b moved to C4d (the maintainer's
decision of 2026-11-20). Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push).
Map: `build/c4c/s7d/map.md` (all of 7d, read at DXMTRS 25001b77; cited "map 3.2" ...). The 5, 6a, 6c, 7a and
6d plans hold the rules this one repeats by name only. Reviews:
`build/c4c/s7d/review-{1-design,2-core,3-exec}.md`. Ledger:
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

Refused by name on the core, `D3DERR_NOTAVAILABLE`: `ProcessVertices` into a default-pool dynamic buffer
(ruling R7d-2): `IDirect3DDevice9::ProcessVertices (into a dynamic default-pool buffer)`.

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

### Three one-predicate fixes (map 3)

1. `core_draw.rs`: the bound view's sRGB twin is picked by `mtld3d_core::sampler_state::srgb_texture_enabled`
   (the low bit), the predicate the sampler key already uses on both runtimes.
2. `texture.rs`, `core_flush_dirty`: the core texture is a volume when `depth > 1`, as `is_volume()` and
   mtld3d's upload decide; a `CreateVolumeTexture` of depth 1 is then a 2D texture in the store, and its
   upload region a 2D one. R2 reads every place the core path asks whether a texture is a volume (the view,
   the upload, `StretchRect`, the lock) for the one that still says 3D.
3. `texture.rs`, `core_flush_dirty`: each colour level, cube face and volume level whose upload is recorded
   is marked uploaded, as mtld3d's scheduler marks it. Two things change with it, both towards mtld3d:
   a `READONLY` or `NO_DIRTY_UPDATE` unlock of an uploaded level marks nothing; `EvictManagedResources`
   marks the uploaded levels dirty, so the next draw publishes the CPU copy again (today it marks none).
   R2 reads every other reader of the mark on a core device.

### `ProcessVertices` (map 2)

mtld3d's body to its last two calls, then a core arm:

- Source bytes: the buffer's CPU backing, or, for a dynamic default-pool buffer the core maps directly (no
  backing), the bytes the application last wrote, through `CoreDevice::dynamic_copy`.
- Destination: a buffer with a CPU backing gets the processed vertices copied in **and its dirty range
  widened** (`write_processed` itself marks none: on mtld3d the upload is pushed at once), so the next draw
  uploads them and a lock reads them; a dynamic default-pool destination is refused by name before anything
  is written (R7d-2).
- `D3D_OK` in place of the recorder's status.

### The gamma ramp (map 4)

- `mtld3d-core` gains the presenter's ramp type by name (an alias of `dxmt_util::GammaRamp`) and a pure
  function from D3D9's 256 entries a channel to the presenter's 1024 points: point i takes mtld3d's own
  piecewise-linear ramp at (i + 0.5) / 1024, so the presenter's linear sampling follows mtld3d's curve
  between the 8-bit codes (exact at none of them in general: ruling R7d-3). Unit-tested on the host.
- d3d9-rt's `CoreDevice` holds an optional ramp and hands it to the presenter before each present, as
  D3D11's swap chain does (`change_gamma_ramp`: an atomic compare when nothing changed; `None` puts the
  fast blit back).
- The front end's `queue_gamma`, on a core device: fullscreen and not the identity → the ramp filled and
  handed over; otherwise none. 6d's warning goes.

### Small things

- `core_arms.rs`: the refusal's name becomes `IDirect3DDevice9::StretchRect (a pair the core does not
  copy)`: one literal (its once-only log is keyed on the string's address), no gate script names the old one.
- The auto-resize's info line says the back buffer keeps its size, without claiming a scaling; the windowed
  `SetGammaRamp` text and the refresh-rate warning say what holds on both runtimes (6d's review m-4, m-5).

## Review Focus (for the final review)

- The uploaded mark: every reader of it on a core device after the change (the lock paths, the eviction,
  the staging's release).
- `ProcessVertices`: no recorder call on a core device; the dirty range; the refusal before any write.
- The gamma ramp: who owns it, when the presenter sees it, what a `Reset`, a re-target and a windowed
  `Reset` do to it.
- mtld3d's answers unchanged.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`parity`** of `d3d9_triangle`, a device and a window of its own a row, judged in every lane;
the expectations are mtld3d's answers and the control confirms them. Rows (R1 gives mtld3d's answer for each
call, R3 the helpers and whether each cell can be observed):

1. `sr-lsb`: a texture whose sRGB decode shows (a mid grey), sampled with `D3DSAMP_SRGBTEXTURE` 0, 1, 2, 3
   and 0x7e41882a: decoded for the odd values, raw for the even ones.
2. `vo-one`: a 1x1x1 volume texture (managed; and a system-memory one sent to a default-pool twin with
   `UpdateTexture`) sampled by the fixed-function stage with three texture coordinates: its texel's colour,
   with `D3DSAMP_SRGBTEXTURE` off and on.
3. `dr-keep`: a managed texture written and drawn (uploaded); a `READONLY` lock writes another colour, a
   draw: **the first colour**; a `NO_DIRTY_UPDATE` lock writes a third, a draw: the first; `AddDirtyRect` of
   a part: that part alone shows the third; `EvictManagedResources`, a draw: the third everywhere.
4. `pv-sysmem`: `ProcessVertices` of four untransformed vertices into a system-memory XYZRHW buffer:
   `D3D_OK`, and a lock reads the viewport's transform of each (`test_sysmem_draw`'s check).
5. `pv-draw`: the same into a managed buffer, **then a quad drawn from it**: it lands where the transform
   put it (the dirty range reached the core); a second `ProcessVertices` at another offset and a draw.
6. `pv-dynsrc`: the source a default-pool dynamic buffer, the destination system memory: `D3D_OK` and the
   transformed vertices.
7. `pv-refuse`, **under the launch's `strict` argument only**: the destination a default-pool dynamic
   buffer: `D3DERR_NOTAVAILABLE`, the device still draws.
8. `rz-cross`, a control: a one-sample D24X8 surface as the depth-stencil, an INTZ destination cleared to
   1.0 at stage 0, RESZ: `D3D_OK` and the destination still 1.0. **If mtld3d fails the cell** (it zeroed a
   colour texture in 7a's control), that is the answer R7a-4 lacked: the cell becomes strict-only in a
   second row commit and the finding goes to the maintainer's list.
9. `gm-full`: a fullscreen device (the guard, the flag, the desktop's mode): a rising ramp set and read
   back; three frames drawn, read back unchanged (the read is before the ramp) and presented; the identity
   set; a `Reset` to windowed and one frame. **No cell sees the ramp on screen** (stated): the row is the
   validation layer's look at the presenter's ramp path.

- Scripts: as C4c-6d registered `fullscreen`, with a refusal list of one literal (`pv-refuse`'s).
- Job scripts: a `prows` counter (9); `jq`'s list sixteen launches; the VM counters; `jm`'s letters and
  tokens; new runners.
- Controls: `ja` (the launch on mtld3d under the validation layer: reports 0; default, msl, core), `jcr`.
  Under strict core at the base, by the map's reading (R3 confirms each): red `sr-lsb`, `vo-one` (with six
  validation reports in the validation launch of the tip's lane, none after), `dr-keep`, `pv-sysmem`,
  `pv-draw`, `pv-dynsrc` (each at the refused call) and `parity-runtime`; green `pv-refuse` (the blanket
  refusal answers it), `rz-cross`, `gm-full`.

### Task 2: the three predicates (DXMTRS)

One commit each (three), each with STATUS.md's line where one changes.

### Task 3: `ProcessVertices` (DXMTRS)

One commit: the arm, the narrowed refusal, STATUS.md.

### Task 4: the gamma ramp (DXMTRS)

One commit: `mtld3d-core`'s function and its tests, d3d9-rt's ramp, the front end's arm, STATUS.md.

### Task 5: the small things (DXMTRS)

One commit: the refusal's name, the three log texts. **One `jt` at this tip**: every row of every launch
green strict under core; `parity` and `fullscreen` under core with the validation layer: reports 0.

### Task 6: gates

`mutants-expect.txt`, `jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Mutants:
  d. the `ProcessVertices` arm does not widen the dirty range → `pv-draw` (`pv-sysmem` green: a lock reads
     the backing);
  e. the arm reads a dynamic source's backing → `pv-dynsrc`;
  h. the uploaded mark set for level 0 only → `dr-keep`, if a level above 0 is in its cells (R3 says, or the
     mutant goes).
- **Not mutants**: the three predicates (the control shows each row red at the base; the fix is the
  predicate); the gamma ramp (no row sees it: its cover is the unit test of the pure function, the
  validation layer and the final review).

### Task 7: review. Task 8: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none intended.
- **New pub items:** `mtld3d_core::gamma`'s alias and function; a ramp setter on `CoreDevice`. **New
  state:** the ramp on `CoreDevice`.
- **Not silent:** the one refusal by name.

## Rulings (planning)

Each with its cost if wrong.

- **R7d-1: 7d is cut in two**: this slice inside `src/d3d9`, then the gate (the lane's script, the
  tile-dispatch reports, which may need dxmt-core, the x64 check, the probe). Cost if wrong: one more
  review cycle; what it buys is that the x64 check and the probe run once, on the final code.
- **R7d-2: `ProcessVertices` into a default-pool dynamic buffer is refused by name.** Such a buffer has no
  CPU backing on core (the core maps it); writing it would go through the core's dynamic lock, a path no
  suite meets (Wine's one call writes system memory). Cost if wrong: a game that processes vertices into a
  dynamic buffer gets `D3DERR_NOTAVAILABLE` and its log line.
- **R7d-3: the gamma ramp follows mtld3d's curve by resampling it at the presenter's 1024 points**, the
  presenter's own linear sampling in between: not exact at the 8-bit codes (the error is below the ramp's
  curvature over 1/1024; not measured). Cost if wrong: brightness a fraction of a code off mtld3d's under
  the opt-in runtime; no gate sees a ramp on either runtime.
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
  `StretchRect`'s pairs, the draw refusals, `ProcessVertices` into a dynamic buffer) are named in STATUS.md.

## Review dispositions

(after the three reviews)

## Execution notes

(after the gates)
