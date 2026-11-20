# DXMTRS C4c-7d-2: C4c's gate

The last slice of C4c: after it the autonomous run stops (the maintainer's direction of 2026-11-19; C4d, C4e
and C5 wait for a go-ahead). Process: the C4 program's, cut to what the slice is (one line of shared code, one
script, two measurements): this plan, three adversarial reviews folded before code, the gates, one final
review, spec, pin, push. Inputs: `build/c4c/s7d/map.md` (sections 6, 7.1, 8), the investigation
`build/c4c/s7d/tile-dispatch.md`, the 7d-1 plan and its sweep. Reviews:
`build/c4c/s7d2/review-{1-design,2-core,3-exec}.md` (cited R1, R2, R3; **R3 holds the job-script list, a full
`jl` and a bounded `jx`**); the investigation of one Wine site, `build/c4c/s7d2/fetch4.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

The roadmap's words: "C4c is done when the conformance lane under core passes with a triage file whose
every line has a reason, the x64 check is green at the tip, the probe is taken, and the spec's C4c paragraph
says so." Four things:

1. **The three Metal validation reports that are left go.** By the investigation's reading, not yet shown
   by a run: dxmt-core's tile-barrier pipeline is made with `tgsize_matches_tile_size` set (since the Rust
   port; the C++ wrote 0), while its dispatch is 16x16 and no pass names a tile size, so Metal's validation
   layer reports a mismatch whenever Metal tiles a pass at another size. The barrier arms when a draw
   samples a subresource the open pass writes: under D3D9 a texture that is its own target. **The flag back
   to 0.**
2. **The conformance lane runs every suite under core and passes**: `dxmt/mtld3d-conformance.sh` with
   `D3D9_RUNTIME=core` stops refusing all but `stateblock`; its proof stays (the core device's line, none of
   mtld3d's encoder, no refusal line, no panic, no Metal validation report, no GPU hang). And
   **`dxmt/d3d9-conf-compare.sh --base-lane default --cur-lane core` passes on the two lanes' runs**: no
   site fails more often under core, no suite crashes more, executes over 0.1% fewer checks or skips a site
   the default lane measured. `dxmt/mtld3d-triaged-core.txt` holds what core fails and mtld3d does not, each
   with its reason. **Not empty as the draft predicted** (R1 C1): one site the runner files under a tolerated
   "ceiling" fails more often under core than on mtld3d in every sweep of C4c (visual.c:15686, a FETCH4 check:
   6 against 3): it is fixed here if the investigation finds a fix inside `src/d3d9`, else triaged with its
   reason. And the tool's own rule on executed counts is tighter than `d3d9ex`'s run-to-run spread on one
   build (R1 C2, R3 I1): the rule gets a floor (below).
3. **The x64 check** (D3D10-12 on the Mac, 420 rows at its last run), once, at the tip: owed for slices 5
   to 7 by the maintainer's batching, and mandatory here because item 1 leaves `src/d3d9`.
4. **The quiet-Mac probe** of the core's draw cost, owed since C4b-3d2: measured, gating nothing.

Then the spec's C4c paragraph, the roadmap and memory say C4c is done, and the run stops.

## Global Constraints

Worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`); **the base is what C4c-7d-1 pushed**;
job scripts in `build/c4c/s7d2/gates`, made from s7d's with the names replaced exactly (`s7d` is a prefix of
`s7d2`, and the old `c4c7d-*` state glob would match the new images: R3), `jx.sh` and `jp.sh` written anew
from R3's bounded forms; the guard and positive checks first; the builder VM stopped after each VM job;
`memory_pressure` printed before a long job; each DXMTRS commit passes `tools/rust-check.sh`; home path
`~`, privacy scan.

- **The one change outside `src/d3d9`** is the flag in `src/dxmt/src/encoder/gaps.rs`, with a line of
  `src/dxmt/fakes` that records it and an assertion in d3d9-rt's tile-barrier test. Nothing else of
  D3D10-12's code changes. The translator key does not move (R2).
- **Because of it the x64 check and the VM's D3D10-12 gate run. Neither can see the flag** (R1 I1, R2 I1,
  R3 I3: the x64 check runs no program that arms the barrier and validates only D3D12; the VM gate compares
  the guest with the same build on the Mac): they show **no collateral**, nothing more. What can see it on
  D3D11 is one launch of `d3d11_compute` (its pixel-shader UAV rows) under the validation layer with the
  command histogram, at the base and at the tip: added to the jobs.
- **`jr` and `post` do not run the x64 check themselves** in this slice (their copies would: the range
  leaves `src/d3d9`): `jx` is the one place, bounded and alone (R3 C1).
- **The x64 check runs with the Mac idle** (no subagent, no other job), never started between 02:55 and
  03:36 (the system empties old `$TMPDIR` files at 03:35, the check's staged reference among them), with the
  builder VM stopped and `memory_pressure` printed first (the stall of 2026-11-19 was the system's
  out-of-memory window as much as two hung programs). A bound of 3600 s on the whole check; past it the
  job ends the check's processes by exact PID after matching each command's prefix, never by name, and
  reports which row hung; a rerun is a new job name.
- **The probe runs last and alone**, after its own build, when the one-minute load is under 3 (the job
  waits up to ten minutes, then exits without benching); if that happens twice the probe is reported as not
  taken and left to the maintainer.
- mtld3d's answers are unchanged.

## The mechanism

### The tile barrier's flag (dxmt-core)

`src/dxmt/src/encoder/gaps.rs`: the tile-barrier pipeline's `tgsize_matches_tile_size: 0`, as the C++ made
it. The fakes' pipeline record gains the flag and d3d9-rt's tile-barrier test asserts it is clear (R2 says
exactly where). **What is not known by reading** (the investigation's list): whether a 16x16 dispatch is
valid under every tile size Metal picks (the "at most the tile size" rule is stated for another call), and
whether any D3D10-12 row takes the barrier. The runs decide: the `visual` suite under core with the
validation layer (three reports now, none wanted), the rows that sample their own target under validation,
and the D3D10-12 gates on the Mac (the x64 check) and in the VM.

A second finding of the investigation stays out: a second draw that samples its own target in one pass gets
no barrier (the tracker arms it once a pass). It is recorded for the maintainer; nothing in a suite or a row
shows a wrong pixel from it.

### The lane and the parity tool (MacNeutron)

`dxmt/mtld3d-conformance.sh`: the core lane's "stateblock alone" refusal goes (as the sweep jobs have cut
it since C4b-3d5); a full core run writes the one stamped `run-core.txt` the compare wants (R1); its header
says so; the core lane keeps its logs and its proof. No change to the runner or the baseline.

`dxmt/d3d9-conf-compare.sh`: **the executed-count rule gets a floor**: a suite fails it when the current
lane executes over 0.1% **and more than 16 checks** fewer (0.1% of `d3d9ex` is 2.4 checks, and the default
lane alone has read 2426 to 2438 on one build; `device`'s 0.1% is 160 checks and is unchanged in effect).
A site the runner files under "ceiling" stays compared count against count (that is how visual.c:15686
shows). Its header loses "until C4c".

**One rerun rule, written before the run** (R3 I1): if the compare's only findings are sites the runner
marks flaky in either lane's history (device.c:1945, d3d9ex.c:3007 are known), `jl` runs once more under a
new job name; the second run's verdict stands.

**Not done here**: a re-baseline against Wine 11.20 (the default runtime's housekeeping, for the
maintainer); the runner's "ceiling" pins keep hiding what a site's count does on either runtime, which is
how the FETCH4 site went unseen for eleven sweeps (R1): said in the roadmap.

## Tasks

### Task 0: the FETCH4 site

By `build/c4c/s7d2/fetch4.md`: the cases that fail under core alone at visual.c:15686. A fix inside
`src/d3d9` (one commit, a cell in an existing FETCH4 row or a new `parity` row, its control red at the
base) if the investigation gives one; else a line in `dxmt/mtld3d-triaged-core.txt` with the reason, and
the difference in STATUS.md.

### Task 1: the row (MacNeutron)

One row in the `parity` launch (10 rows then; the registration's four places and the jobs' counts, 9 → 10
and 11 → 12: R3 I4): **`tb-self`**, Wine-shaped (R1 I3, R2 I2, R3 I5): a **64x64** A8R8G8B8 one-sample
render-target texture (the size at which the reports came), cleared and **read back** (the pass ends: the
core would otherwise fold the Clear into the pass that samples it, which mtld3d refuses to do: R2's
finding, recorded), then bound as target 0 and at stage 0 at once and one quad drawn that samples it. Cells:
the draw's answer, a later ordinary draw on the device, and the target's pixels **only if the control shows
the two runtimes agree** (Wine itself wraps that pixel in a todo). The row's job is the validation layer:
- Controls: `ja` at the base with `parity strict` under core and the validation layer: **a tile-dispatch
  report must show there** (parity showed none at 7d-1, so any is this row's); if none shows, the pass is
  not tiled at 32x32 or the barrier does not arm (the command histogram tells which), and the visual
  suite's own three reports stay the evidence. `jcr`.

### Task 2: the flag (DXMTRS)

One commit: the flag; the fakes' line `tile tgsize=<value>` after the tile samples line; the assertion in
d3d9-rt's tile-barrier test. **The implementer shows the new assertion red with the flag at 1, then green
at 0** (that is the mutant: R3 C2).

### Task 3: the lane and the tool (MacNeutron)

One commit: the lane script's guard and header, the compare's floor and header, the triage line if Task 0
ends in one. `jt` at the tip of both repositories.

### Task 4: gates

No `jmb`, `jm` or `jw` (the base is the mutant; `jl`'s core lane is the sweep). In this order, each under
the pipeline lock (R3's durations: about 100 minutes):
1. `jl` (new, R3's form): a `DXMT_D3D9_CORE=1` build; each lane's old run file deleted first; the default
   lane's full conformance, then the core lane's with its proof; both run files copied into the job's
   folder only if newer than the lane's start, raw logs kept; the validation reports counted by kind (none
   wanted); then `d3d9-conf-compare.sh --base-lane default --cur-lane core`: passes, the triage file's
   lines printed.
2. `jq` (sixteen launches under the validation layer; `parity` 12 rows ok), **and `d3d11_compute` once
   under the validation layer with the command histogram**: no report, and the tile dispatch counted (the
   barrier still dispatches); the same launch ran at the base in `ja`.
3. `jc` with the D3D10-12 gate (`JC_D3D10`), default and core, both WoW64 forms; the builder stopped.
4. `jr` (plain build: the translator key, the imports, d3d9-check, the default conformance), without the
   x64 check.
5. `jx`: the x64 check, alone, idle, bounded.
6. `jp`: the probe: one build at the tip, **core against mtld3d on that build** over `tex`, `buf` and `dyn`
   at 0 and 600 frames (R1 I2, R3 C3: C4c's base refused buffer draws under core, so a base-against-tip
   figure exists for `tex` alone; the two runtimes on one build is the figure a closing report wants).

### Task 5: review. Task 6: C4c's closure

The final review of the range. Then: the spec's C4c paragraph ("C4c done": what the core runtime does, what
it refuses by name, what differs from mtld3d, each from STATUS.md; the lane's and the check's results; the
probe's figures); the roadmap's "where it stands" and "waiting on the maintainer" brought up to date (every
finding of slices 5 to 7 in one list); pin; push; post-push check (told the x64 check ran at the tip);
memory. **Then stop**, with a report of C4c as a whole.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression, a new pub item:** none.
- **Outside `src/d3d9`:** one field's value in dxmt-core, one recorded field in its fakes.

## Rulings (planning)

Each with its cost if wrong.

- **R7d2-1: the flag goes back to the C++'s value in shared code** (the Rust port set it with no stated
  reason: R2's history), where a D3D9-only way round (a closure of its own at d3d9-rt's draw, or ending the
  pass before a self-sampling draw) would leave D3D11's copy of the report. Not done instead: naming a tile
  size on every pass, which would re-tile all of D3D10-12's passes at a cost nobody has measured. **Cost if
  wrong, stated as it is**: no gate of D3D10-12 exercises the barrier; the evidence is one validation launch
  of `d3d11_compute` and "no collateral" from the x64 check and the VM gate. Whether a 16x16 dispatch is
  valid under every tile size Metal picks (it could pick one below 16) is not stated by any header in the
  tree: the runs here see 32x32 tiles only.
- **R7d2-2: the lane's script is opened for every suite under core, the baseline untouched.** Cost if
  wrong: the core lane inherits mtld3d's baseline drift against Wine 11.20 (three triaged sites today).
- **R7d2-3: the probe gates nothing, and compares the two runtimes on one build.** Cost: a draw-cost
  regression of C4c is reported, not fixed, here.
- **R7d2-4: a hung x64 row is bounded and reported, not retried in a loop**: one rerun under a new job
  name; a second hang of the same row with every other row green is reported as that row's known flake (the
  roadmap's finding of 2026-11-18) and the check's verdict stated as "green but for it". Cost if wrong: a
  real regression in that one row is read as its flake; the maintainer's list names it.

## Stated gaps

- What the core runtime still refuses by name and where it differs from mtld3d are STATUS.md's lists (7d-1
  wrote the last of them); the spec's C4c paragraph repeats them.
- The barrier is armed once a pass (above).

## Review dispositions

Three reviews: **five Critical between them, all in the gate's plan, none in the flag** (which all three
confirm: one tile pipeline, one consumer, the C++ wrote 0).

- **R1** (2 Critical, 6 Important, 7 Minor): C1 the FETCH4 site under its "ceiling" (Task 0; the triage
  file not empty); C2 the executed rule against `d3d9ex`'s spread (the floor); I1 the D3D10-12 gates cannot
  see the flag (`d3d11_compute` under validation; R7d2-1's cost reworded); I2 the probe's base (the two
  runtimes on one build); I3 `tb-self` Wine-shaped; I4 only 32x32 tiles are seen (stated); I5 the host
  test's assertion is the mutant's kill; I6 `jl`'s outputs.
- **R2** (no Critical, 2 Important): I1 as R1 I1; I2 the 64x64 target and the histogram so that "arms" and
  "reports" are two facts; M1 the fakes' line and the assertion's place; the Clear's fold into a
  self-sampling pass (a difference from mtld3d, recorded; the row ends the pass first); the tracker's
  once-a-pass barrier is upstream's design (the note reworded); a pre-existing fake handle when the tile
  pipeline fails to build (for the maintainer's list).
- **R3** (3 Critical, 7 Important, 8 Minor): C1 `jr` and `post` would each run an unbounded x64 check; C2
  the mutant cannot be judged by the row lanes (dropped for the base's reports and the test shown red); C3
  the probe as R1 I2, its wait after the build; I1 the rerun rule; I2 stale run files; I3 as R1 I1; I4, I5
  the row's files and shape; I6 `jw` dropped; I7 the bounded `jx`; the script list and its copy guard.

## Execution notes

(after the gates)
