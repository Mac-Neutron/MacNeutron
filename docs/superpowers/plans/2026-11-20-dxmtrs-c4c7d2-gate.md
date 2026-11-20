# DXMTRS C4c-7d-2: C4c's gate

The last slice of C4c: after it the autonomous run stops (the maintainer's direction of 2026-11-19; C4d, C4e
and C5 wait for a go-ahead). Process: the C4 program's, cut to what the slice is (one line of shared code, one
script, two measurements): this plan, three adversarial reviews folded before code, the gates, one final
review, spec, pin, push. Inputs: `build/c4c/s7d/map.md` (sections 6, 7.1, 8), the investigation
`build/c4c/s7d/tile-dispatch.md`, the 7d-1 plan and its sweep. Reviews:
`build/c4c/s7d2/review-{1-design,2-core,3-exec}.md`. Ledger:
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
   with its reason: predicted **empty** (after 7d-1 the sweep has no such site).
3. **The x64 check** (D3D10-12 on the Mac, 420 rows at its last run), once, at the tip: owed for slices 5
   to 7 by the maintainer's batching, and mandatory here because item 1 leaves `src/d3d9`.
4. **The quiet-Mac probe** of the core's draw cost, owed since C4b-3d2: measured, gating nothing.

Then the spec's C4c paragraph, the roadmap and memory say C4c is done, and the run stops.

## Global Constraints

Worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`); **the base is what C4c-7d-1 pushed**;
job scripts in `build/c4c/s7d2/gates`, copied from s7d's (and `jx.sh` from s4d's, `jp.sh` from s5's) and
edited before their first run, with the guard and positive checks first; VM state names `c4c7d2-*`; the
builder VM stopped after each VM job; `memory_pressure` printed before a long job; each DXMTRS commit passes
`tools/rust-check.sh`; home path `~`, privacy scan.

- **The one change outside `src/d3d9`** is the flag in `src/dxmt/src/encoder/gaps.rs`, with a line of
  `src/dxmt/fakes` that records it and an assertion in d3d9-rt's tile-barrier test. Nothing else of
  D3D10-12's code changes.
- **Because of it: the x64 check and the VM's D3D10-12 gate both run** in this slice's chain.
- **The x64 check runs with the Mac idle** (no subagent, no other job), never across 03:35 (the system
  empties old `$TMPDIR` files then, the check's staged reference among them: the job waits it out), with the
  builder VM stopped and `memory_pressure` printed first. Its XeSS and DLSS rows have hung unattended before
  (the check's watchdog sends a signal a hung test ignores): the job puts its own bound on the whole check
  and, past it, ends the check's processes by their exact PIDs and reports which row hung; a rerun is a new
  job name.
- **The probe runs last and alone**, on a Mac whose one-minute load is under 3 (the job waits for it, up to
  ten minutes, and says so when it gives up); not while the maintainer is using the machine for something
  heavy: if the wait gives up twice, the probe is reported as not taken and left to the maintainer's word.
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

### The lane (MacNeutron)

`dxmt/mtld3d-conformance.sh`: the core lane's "stateblock alone" refusal goes (as the sweep jobs have cut
it since C4b-3d5), its header says the lane runs every suite; the core lane keeps its logs and its proof.
`dxmt/d3d9-conf-compare.sh`'s header loses "until C4c". No change to the runner or the baseline: the
default lane's three new sites are triaged in `dxmt/mtld3d-triaged.txt` and fail the same under core, which
the compare counts as equal.

**Not done here** (the 7d-1 plan's R7d-4): a re-baseline against Wine 11.20 (the runner's `--update-baseline`
rewrites mtld3d's own record and `CONFORMANCE.md`'s prose with it: the default runtime's housekeeping, for
the maintainer).

## Tasks

### Task 1: rows (MacNeutron)

One row, in the `parity` launch (10 rows then): **`tb-self`**: a render-target texture bound as the target
and at stage 0 at once, a quad drawn that samples it (what arms the barrier), read back: the answer and the
pixels mtld3d gives (the control settles them; R1 reads what mtld3d does there: it may refuse or snapshot),
and under the validation layer no report. If mtld3d and the core differ in what such a draw shows, the row
judges only what is common (no crash, the device still draws) and the difference is recorded: the row's
job is the validation layer's look at the barrier.

- Controls: `ja` at the base (with `parity strict` under core and the validation layer: **`tb-self` must
  show the tile-dispatch report there before the fix**; if it shows none, the row does not arm the barrier
  and is rewritten until it does, or the Wine function's own run stays the only evidence), `jcr`.

### Task 2: the flag (DXMTRS)

One commit: the flag, the fakes' line, the test's assertion.

### Task 3: the lane (MacNeutron)

One commit: the script's guard and headers. `jt` at the tip of both.

### Task 4: gates

The chain, in this order, each under the pipeline lock:
1. `jw` (the sweep, with its counts by kind): `visual` no validation report, no line over the baseline.
2. `jl` (new): the default lane's full conformance, the core lane's full conformance (its proof), then
   `d3d9-conf-compare.sh --base-lane default --cur-lane core`: passes; the triage file's lines counted.
3. `jr` (plain build: the translator key, the imports, d3d9-check).
4. `jc` **with the D3D10-12 gate** (`JC_D3D10`), default and core, both WoW64 forms; the builder stopped.
5. `jq` (sixteen launches under the validation layer).
6. `jm`: one mutant, **the flag set again** → `tb-self` under the validation layer reports (if Task 1's
   control showed the row arms the barrier; else the mutant's evidence is `jw`'s `visual` count, run on the
   mutant's build).
7. `jx`: the x64 check, alone, idle Mac, bounded.
8. `jp`: the probe, last (the tip against the base of C4c, `tex`, `buf` and `dyn`, 0 and 600 frames).

### Task 5: review. Task 6: C4c's closure

The final review of the range. Then: the spec's C4c paragraph ("C4c done": what the core runtime does, what
it refuses by name, what differs from mtld3d, each from STATUS.md; the lane's and the check's results; the
probe's figures); the roadmap's "where it stands" and "waiting on the maintainer" brought up to date (every
finding of slices 5 to 7 in one list); pin; push; post-push check; memory. **Then stop**, with a report of
C4c as a whole.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression, a new pub item:** none.
- **Outside `src/d3d9`:** one field's value in dxmt-core, one recorded field in its fakes.

## Rulings (planning)

Each with its cost if wrong.

- **R7d2-1: the flag goes back to the C++'s value in shared code**, where a D3D9-only way round (a
  pipeline and a closure of its own, or ending the pass before a self-sampling draw) would leave D3D11's
  latent copy of the report. Cost if wrong: D3D10-12 behaviour changes where a pixel-shader UAV arms the
  barrier; the x64 check and the VM's D3D10-12 gate are what would show it, and the change is the C++'s
  behaviour, which those gates' references were made with.
- **R7d2-2: the lane's script is opened for every suite under core, the baseline untouched.** Cost if
  wrong: the core lane inherits mtld3d's baseline drift against Wine 11.20 (three triaged sites today).
- **R7d2-3: the probe gates nothing.** Cost: a draw-cost regression of C4c is reported, not fixed, here.
- **R7d2-4: a hung x64 row is bounded and reported, not retried in a loop**: one rerun under a new job
  name; a second hang of the same row with every other row green is reported as that row's known flake (the
  roadmap's finding of 2026-11-18) and the check's verdict stated as "green but for it". Cost if wrong: a
  real regression in that one row is read as its flake; the maintainer's list names it.

## Stated gaps

- What the core runtime still refuses by name and where it differs from mtld3d are STATUS.md's lists (7d-1
  wrote the last of them); the spec's C4c paragraph repeats them.
- The barrier is armed once a pass (above).

## Review dispositions

(after the three reviews)

## Execution notes

(after the gates)
