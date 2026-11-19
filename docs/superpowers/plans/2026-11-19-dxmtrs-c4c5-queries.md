# DXMTRS C4c-5: queries on D3D9's core runtime

Slice 5 of C4c. Process: the C4 program's (code map, this plan, three adversarial reviews folded before code,
rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s5/map.md` (cited "map 1.2", "H1" ...). The 4c and 4d plans hold the rules this one repeats by name
only. Reviews: `build/c4c/s5/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, `IDirect3DDevice9::CreateQuery` answers as on mtld3d (EVENT and OCCLUSION exist,
every other type is `D3DERR_NOTAVAILABLE` with the out pointer untouched, a null out pointer is the support
probe), and `IDirect3DQuery9::Issue` and `GetData` give mtld3d's answers (map 1.2, 1.3):

- **EVENT**: `GetData` is `S_OK` with TRUE once the GPU work recorded before `Issue(END)` is done, `S_FALSE`
  with FALSE before; a poll submits what is recorded when the END is still in it, with or without
  `D3DGETDATA_FLUSH` (mtld3d's rule: a title that polls without the flag and without `Present` must finish).
- **OCCLUSION**: the samples that passed between BEGIN and END, exact, over any number of passes, targets and
  `Present`s; mtld3d's order rules (END first is an empty span that answers 0; END twice keeps the result; BEGIN
  twice restarts) and sizes (`min(size, 8)` low bytes of a 64-bit count; `0xdd` bytes for a query never issued);
  an open span is `S_FALSE`; an ended span is `S_FALSE` without the flag until its work is done, and with the
  flag the call waits for it. **On the core only an application's draws count** (D3D9's rule): a Clear, a
  `StretchRect`, a `ColorFill`, a resolve, an upload do not. mtld3d counts one of them, a scaled `StretchRect`
  into the bound target right after a counted draw (its quad joins the counting pass: R1 I2): a stated
  divergence and an mtld3d finding, not copied.

After it no launch and no Wine suite meets the refusal "IDirect3DDevice9::CreateQuery".

Wine (map 4.2, 5): no "Test failed" line is caused by the refusal today. `test_refcount`'s device-refcount
check of its query starts to run (device.c:1802; predicted to pass: the wrapper is unchanged).
`test_sample_attached_rendertarget` (visual.c:26344) stops skipping and runs for the first time under core
(a dynamic vertex buffer, three draws that sample the texture bound as the target, a read-back): **predicted
0 new "Test failed" lines** (its colour check sits under `todo_wine_if`: R3), a Metal validation report
possible. **The sweep's `visual` run is therefore the chain's first job** (R1 I5): a panic or a crash there
(the suite's executed count falls: sixteen later functions are lost) blocks the push; a failed line or a
report is ledgered for C4c-7's conformance lane and does not.
`test_occlusion_query` keeps skipping (it creates a fullscreen device, which the core refuses until C4c-6), so
**OCCLUSION is held by our rows alone**. `test_timestamp_query` skips on both runtimes.

## Global Constraints

The 4d plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-4d pushed**; job scripts in `build/c4c/s5/gates`, copied from s4d's and edited before
their first run (guard: `/usr/bin/grep -nE 'c4c/s4d|c4c4d|C4c-4d'` prints nothing; VM state names `c4c5-*`; the
positive checks: row counts, launch lists, letters and tokens); a rerun is a new job name; every verdict counts
its rows and its `was not recorded` lines; the pipeline lock; no implementer runs `dxmt/toolchain.sh`; each
DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device (map 1.4
  lists the six a query reaches today).
- **No file outside `src/d3d9` changes.** dxmt-core's `VisibilityQuery`, the encoder's three calls and the
  queue's clocks are used as they are (map 2). Its gap for a span that ends in a chunk with no counted draw
  (H1) is settled in d3d9-rt (ruling Q5-2), not in `src/dxmt`.
- mtld3d's code path is unchanged: its rows are this slice's reference, run first.
- **The x64 check does not run in this slice** (maintainer, 2026-11-18: once at the end of C4c-7), unless the
  DXMTRS range leaves `src/d3d9`.

## The mechanism

### d3d9-rt: `rt/src/device/query.rs` (new)

d3d9 depends on d3d9-rt, not on dxmt-core (map 3), so d3d9-rt owns the types.

- **EVENT.** `CoreDevice::event_stamp() -> u64`: the queue's `current_seq()` (the chunk being recorded; never
  0, the ring starts at 1). `CoreDevice::event_done(seq: u64) -> bool`: mtld3d's two pure rules on the core's
  clocks (`mtld3d_core::query_fence::event_completed(seq, coherent_seq)`, and when that is false and
  `event_needs_submit(seq, current_seq)`, `commit()` and the test once more). A chunk with nothing in it is
  committed like any other (`sync_to` already does so): no "clean" special case (H17 not taken). The cost is
  mtld3d's own (R1 M6): an END issued right after `Present` is stamped with the next frame's chunk, and the
  first poll after it commits what that frame has recorded so far. `Issue(BEGIN)` and `Issue(0)` on an EVENT
  query are `D3D_OK` and do nothing, as on mtld3d.
- **OCCLUSION.** `pub struct Occlusion` (plain data the front end's query owns): the current bracket
  `Option<Arc<VisibilityQuery>>`, whether it is open, and the END's seq.
  - `CoreDevice::occlusion_begin(&mut Occlusion)`: an open bracket is ended first (its END recorded: H2: the
    encoder's active count is a plain counter); then **a fresh `Arc` a BEGIN** (H2, H4: a chunk still to be
    issued may hold the old one) and one `emit` of `enc.begin_visibility_query`.
  - `CoreDevice::occlusion_end(&mut Occlusion)`: open → one `emit` of `enc.end_visibility_query`, the END's
    seq = `current_seq()` (the closure replays in that chunk), closed. Never begun → a bracket that answers 0
    with nothing recorded (mtld3d's `OpenAndClose`). Already closed → nothing (mtld3d's `Keep`).
  - `CoreDevice::occlusion_poll(&mut Occlusion, flush: bool) -> OcclusionAnswer` (`Never`, `Pending`,
    `Count(u64)`): never issued → `Never`; open → `Pending`, flag or not; closed → `value()`; when that is
    `None` and `flush`, `sync_to(end_seq)` and `value()` again; **when the END's chunk has retired
    (`coherent_seq >= end_seq`) and `value()` is still `None`, `issue(end_seq, &[])`** (H1: dxmt-core makes no
    read-back for a chunk without results, so a span whose END sits alone in a chunk is never issued; the
    public call adds nothing and marks the END's chunk issued; every earlier chunk's read-back ran before the
    ring finished it, map 2.1). Without the flag nothing is committed (mtld3d: an OCCLUSION poll without the
    flag never submits).
  - `CoreDevice::occlusion_release(&mut Occlusion)`: an open bracket is ended (H3: the encoder would count on
    every later pass for the device's life). **The device counts its open brackets** (one up a recorded
    begin, one down a recorded end) and, when it is dropped with any left, logs one error line, "d3d9 core: N
    occlusion queries' END was not recorded": the gates' `was not recorded` count then holds the front end's
    Release arm, which no count of a row can show (R3 I4).
  - `Issue(0)` on an OCCLUSION query is `D3D_OK` and does nothing.
- **Draws count, nothing else does.** `draw.rs`: `enc.bump_visibility_result_offset()` right before each of the
  two draw records (map 2.2; with no query active it tests one counter and records nothing).
  **From a device's first OCCLUSION BEGIN on, the passes that can merge with a counting pass start with the
  visibility mode "disabled"** (H20, new, from the core's merge rule: an earlier pass's records are put in
  front of the later pass's and run in one encoder, so every pass re-emits its state; the encoder's offset
  bookkeeping starts each recorded pass at "no offset" and records a mode only on a change, so a pass with no
  query active records none, and merged behind a counting pass it would count its draws: an application's
  draws after END as well as the `StretchRect` quad; R2 confirmed both merges from the code). One
  `WmtcmdRenderSetvisibilitymode { DISABLED }`, before any bump of its pass, at **two sites**: where the draw
  pass starts (`open_pass`'s closure) and in `encode_triangle_state` (the `StretchRect` quad; the depth
  resolve shares the helper and gets it too, though a colourless pass merges with no draw pass). **Not in
  the clear quads** (R2 C1, R1 M1: their pass carries an argument buffer and another array length, so it
  never merges with a d3d9-rt pass, and a record after a `quad.begin` that started no pass would land outside
  a pass). Metal takes the disabled mode without a result buffer (mtld3d records it in such passes), and a
  counted draw's own record follows it. A device that never begins an occlusion query records nothing new
  (one flag on `CoreDevice`, set by the first `occlusion_begin`): its passes and their unit tests are
  unchanged. D3D10-12 share the shape (a draw pass with no query active behind a counting one): a follow-up
  for the maintainer, not this slice's.

### Front end

- `device_create_query`: the `refuse_under_core!` goes; the rest (the type table, the support probe, the
  wrapper, the device reference) is mtld3d's.
- `QueryInner` gains, behind the feature, the core's state (an EVENT's seq is the existing `end_seq`; an
  OCCLUSION's `Occlusion`). The inner is only ever borrowed shared today and its state is atomics (R1 I4), so
  the `Occlusion` sits in **a `RefCell`** (CONVENTIONS' first rung; no borrow is held across a call that can
  re-enter the query), or, if `QueryInner` must stay `Sync`, behind the texture's pattern (`InPtrMut` and an
  `inner_mut` with its `unsafe` block and SAFETY comment). The implementer takes the first that compiles
  under the lints and says which.
- `query_issue`, `query_get_data` and `close_released_span` each take a core arm keyed on the device. A core
  device never reaches `encoder_status()` (an mtld3d recorder read; the core has no latched failure an
  application sees: H16), and in `close_released_span` the arm sits **before the `span_open()` test** (the
  mailbox state it reads is never armed on a core device: R3 I4). `GetData`'s order on a core device, as
  mtld3d's (R1 I3): EVENT: `query.eventImmediate` (`S_OK`), else `event_done`. OCCLUSION: the core's answer;
  `Never` → `S_OK` + `0xdd`; `Count(n)` → `S_OK` + `n`; `Pending` with the flag and `query.flushImmediate` →
  `S_OK` + `u32::MAX` (mtld3d's stub, an open span included); `Pending` otherwise → `S_FALSE` with nothing
  written. The writes are mtld3d's functions (`write_occlusion`, the `0xdd` fill, the BOOL width).
- Docs: `docs/d3d9/STATUS.md` and the module docs that list what the core refuses.

## Review Focus (for the final review)

- The bracket's life: BEGIN twice, END first, END twice, BEGIN after a result, Release while open; one `end`
  recorded for every `begin`.
- H1's settle: that `coherent_seq >= end_seq` implies every read-back of the span ran; that `issue(end_seq,
  &[])` cannot lose a count or trip the debug assertion; that a late read-back cannot double-count.
- What counts: the two draw sites and only them; every pass of d3d9-rt's own that draws.
- `event_done`'s commit: when it runs, that it cannot run with a lock or a pass state the caller relies on.
- mtld3d unchanged; nothing outside `src/d3d9`; the i686 build.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`queries`** of `d3d9_triangle` (the twelfth row table; a 256x256 single-sample back buffer and
a depth-stencil a row binds itself and unbinds on every path, `depthBack`), judged in every lane. Each count
is of the file's pretransformed quads, exact by the top-left rule (their edges sit on pixel centres: R3),
compared with **a new exact helper** (`want()` allows one a byte) after **a new wait helper**:
`GetData(NULL, 0, D3DGETDATA_FLUSH)` polled for at most 5 s (Wine's `wait_query`). **Every counting cell is
one `until` attempt, BEGIN through the read and the compare** (R1 I1: mtld3d leaves a draw out of its frame
while its pipeline builds, unless a query counts: a quad drawn before BEGIN, an occluder, can be missing at
the first attempt; under strict a cell has one attempt). A row that fails must fail, not crash, when
`CreateQuery` answers `D3DERR_NOTAVAILABLE` (the core before Task 3). Twelve rows:

1. `qy-support`: the support probe and the creation for EVENT and OCCLUSION (`D3D_OK`), and for VCACHE,
   VERTEXSTATS, TIMESTAMP, TIMESTAMPDISJOINT, TIMESTAMPFREQ and type 3 (`D3DERR_NOTAVAILABLE`, the out pointer
   still `0xdeadbeef`); `GetType`, `GetDataSize` (4 and 4), `GetDevice`; the device's reference count one up
   with a query alive and back after its Release (`GetDevice`'s own reference given back first).
2. `qy-event`: never issued: `GetData(&b, 4, 0)` `S_OK`; a quad, `Issue(END)`, the wait, then `GetData(&b, 4,
   0)` `S_OK` with TRUE; `GetData(NULL, 0, 0)` `S_OK`; **a second END after another quad, polled with flags 0
   and no `Present`, is `S_OK` within 5 s** (the poll submits).
3. `qy-count`: BEGIN, a 64x32 quad in a scene, END, the wait: `GetData(&d, 4, 0)` is 2048; size 2 writes the
   low WORD alone; size 8 the count and a zero high DWORD; size 0 leaves the buffer; then, a depth-stencil
   bound, cleared to 1.0 and Z on (LESSEQUAL), **an occluder at z 0.1 over the quad's left half drawn before
   BEGIN** and the quad at z 0.5 inside the span: 1024; then **the usual proxy draw** (R1 I6: colour writes
   and Z writes off, Z test on, the one draw mtld3d keeps only while a query counts): the same quad again
   behind the same occluder, 1024, and the target's texels unchanged.
4. `qy-empty`: never issued: `GetData(NULL, 0, FLUSH)` `S_OK`, and four bytes of `0xdd` with the next DWORD
   untouched; END first: 0; BEGIN, END with no draw: 0; BEGIN|END in one call: 0; last, 400 BEGIN/END pairs
   in one scene with no draw and the wait right after: 0 (400, not Wine's 1000: on mtld3d a pair takes two of
   a frame's 1024 slots, R1 M4).
5. `qy-open`: BEGIN, a quad, `GetData(&d, 4, FLUSH)` while open: `S_FALSE`, `d` untouched; END, the wait, the
   count.
6. `qy-twice`: BEGIN, quad A (2048), BEGIN, quad B (600), END: 600; END again `D3D_OK` and still 600.
7. `qy-present`: BEGIN, a quad, `Present`, END with nothing drawn after it: the count (H1); BEGIN, quad A,
   `Present`, quad B, END: A + B.
8. `qy-two`: Q1 BEGIN, A, Q2 BEGIN, B, Q1 END, C, Q2 END: Q1 = A + B, Q2 = B + C.
9. `qy-pass`: on a 64x64 target T: BEGIN, quad A, **a Clear with a rect, a scaled `StretchRect` into a rect of
   T, a `ColorFill` of a rect of T**, quad B, then a second target U bound and quad C, END: A + B + C; T's
   texels show the Clear, the copy and the fill (the rect Clear comes right after A on purpose: on mtld3d it
   ends the counting pass, so the copy after it is not counted there either). **Then a draw after END** (H20):
   BEGIN, quad A on T, END, a quad on U, quad D on T, all in one scene with nothing between A and D that
   commits or reads, no depth-stencil bound, nothing of T sampled: A alone (T's two passes merge around U's:
   R2 I1). **No cell puts a `StretchRect` right after a counted draw**: mtld3d counts that quad (the Goal's
   divergence), so the cell cannot be green in every lane; the record in the quad's pass is held by a unit
   test (Task 2), not by a row.
10. `qy-release`: BEGIN, a quad, the query released while open; a new query: BEGIN, quad B, END: B.
11. `qy-reuse`: one query three times (three sizes of quad), each count read; then BEGIN, quad, END, BEGIN,
    another quad, END with no read between: the second count.
12. `qy-noflush`: BEGIN, a quad, END; `GetData(&d, 4, 0)` polled across `Present`s for at most 5 s: the count.
    (Without the flag and without a `Present` it finishes on neither runtime: stated, not judged.)

- Scripts (map 4.1; half-done registrations fail silently): `dxmt/d3d9-check.sh` (the launch, its `check`
  list, **`queries` in the "none" refusal list**, the header), `vm/dxmt/d3d9-gate.sh` (the row loop, the
  `runtime core` names, the header), `vm/tests/d3d9.sh` (`qu64-fresh`, `qu32-fresh`, the header), and
  `vm/tests/d3d9-gate-bench-test.sh` if it lists launches; the test file's header. `sh -n`.
- Job scripts: `QUERY_ROWS` 12 with a `qrows` counter in `head.sh` **and a call of it wherever `rows` and
  `mrows` are called**; `jcr.sh` and `jc.sh` count `qu64`/`qu32` ok lines (12 each, one more under core);
  `jv0.sh` runs `queries`; `jq.sh`'s list gains `queries` **and prints how many launches it ran under
  validation (want 12: its list has no other positive check, R3)**; `chain.sh` runs `jw` first. Beyond the
  string guard, checked by eye before the first run (R3): `X64_BASE`, the lane runner's SHAs, `jm.sh`'s
  letters and tokens.
- Controls after 4d's push, in this order: `JOBS=jv0` (`queries` on mtld3d under the validation layer:
  reports 0, rows ok 14: **the rows' expectations are mtld3d's answers and are confirmed here; a red cell is
  either a wrong expectation, corrected in a second row commit with the reason, or an mtld3d finding,
  ledgered, its cell dropped or made answer-only in that second commit before any code** (R3 I2: a cell left
  red would keep every later default lane red)); `ja` (default and msl green; core red on exactly the twelve
  `queries-qy-*` tokens and `queries-runtime`: one refusal line where none is wanted; the launch's exit and
  its twelve FAIL lines are both printed, so a crash is not read as twelve failures); `jcr` (the VM's default
  lane: 12 `qu64` and 12 `qu32` ok lines; every other launch as 4d left it).

Commit: "dxmt, vm: d3d9_triangle rows for queries (DXMTRS C4c-5)".

### Task 2: queries in d3d9-rt

One commit, "d3d9: EVENT and OCCLUSION queries in the core recorder" (not split, against R1 M9: with the
flag a device without queries records what it recorded before, so the commit's reach on existing passes is
the bump call alone, which the existing core launches and the probe hold). Tests on the fakes, **worded by
what the fakes show** (R2 I2, R3 I3: they complete every command buffer at once, and print a visibility-mode
record as its type alone, with no mode, offset or buffer):
- EVENT: `event_done` of a retired seq is true with `current_seq` unchanged; of the recording seq it commits
  (`current_seq` one up) and, the fakes finishing at once, answers true at that call or the next; of an empty
  chunk the same (the first empty-chunk commit a test exercises: R2). No assertion of "false": it would race
  the finish thread.
- The bracket's life, as closures recorded: BEGIN twice records begin, end, begin; Release while open records
  an end and leaves the device's open count at 0; END first records nothing and polls `Count(0)`; END twice
  records one end; a device dropped with a bracket open logs the error line.
- `occlusion_poll`: `Never`; `Pending` while open with and without the flag; without the flag `current_seq`
  does not move.
- **H1**: BEGIN, a draw, commit, END, poll with the flag: `Count` (with the settle removed, `Pending` for
  ever: the implementer's own red check, its log kept).
- Records, as counts and positions in a pass's print: before any BEGIN on the device a draw pass prints no
  visibility-mode record; after one, with no query active, exactly one, before the first draw record; with a
  query active, two before the first draw record (the pass's own, then the bump's); the `StretchRect` quad's
  pass prints one before its draw record after a BEGIN on the device and none before; the clear quads none.
  **Which mode a record carries is held by `qy-pass` and mutant d alone.**

### Task 3: the front end

`device_create_query`, the three core arms, docs. One commit, "d3d9: queries on the core runtime". Lane `jt3`
(default, core) at the tip: every row of every launch green strict under core; refusal lines `queries` 0;
`was not recorded` 0; **`queries` under core with the validation layer**: reports 0, rows ok 14 (the first
real visibility buffer on a d3d9-rt pass). The VM's forwarded path is first run by the chain's `jc` (H13).

### Task 4: gates

As the 4d plan's Task 4 without `jx`: `jmb`, then `jr`, `jc`, `jq`, `jm`, `jw`. Mutants (final tokens in
`gates/mutants-expect.txt` from the rows as committed):
  a. the draw closure does not bump → every count is 0: `qy-count`, `qy-open`, `qy-twice`, `qy-present`,
     `qy-two`, `qy-pass`, `qy-release`, `qy-reuse`, `qy-noflush`;
  b. H1's settle removed → `qy-present` (its first cell waits out its 5 s);
  c. `event_done` never commits → `qy-event` (its first wait: nothing presents; R1 M2, R2, R3);
  d. no pass records the disabled mode → `qy-pass` (its second cell: the count holds quad D; the first cell
     stays green, its copy never merges behind A: R1 M2, R2 I1); a mutant that stays green is ledgered as
     "not held by a row" and the record stays (it costs one record a pass);
  e. the front end's Release arm removed → `queries` green in its rows, **one `was not recorded` line** (the
     device's count of open brackets at its end), which `jm` prints a mutant.
  Held by unit tests: one end a begin (H2, H3), the fresh `Arc` a BEGIN (H4), no commit without the flag,
  the quad's record. A mutant that waits out 5 s cells must still end inside the launch's budget (b: one
  cell; c: one cell, the row stops at its first failure).
- `jw`: the sweep → `build/c4c/s5/sweep.md`.
- **Probe: once** (`draw.rs` changes: one call a draw), after the chain, **nothing else running** (as the x64
  check's hold). Jobs `jp0` (the base, `DXTIP` set) and `jp` (the tip) from `build/c4c/s2b/gates/jp.sh` (R3
  I1): each its own `DXMT_D3D9_CORE=1` build, `d3d9_triangle --bench 2000 0 tex` and `--bench 2000 600 tex`
  under core with `debug.presentOccluded=true`, one run each (maintainer: benches once) under the 300 s kill
  timer, after the script's wait for a one-minute load under 3 (ten minutes at most; the load is printed
  either way), each run proven (the runtime that ran, no refusal, `failed 0`) → `build/c4c/s5/probe.md`. It
  measures CPU a frame with no query active; the read-back buffer a chunk with a counted draw allocates is
  not measured (R2). **It does not gate the push** unless the tip is slower than the base by more than 10%
  on a Mac the load line shows quiet; otherwise the figure and the load go in the notes.

### Task 5: review. Task 6: spec, pin, push

As the 4d plan's, without the x64 check.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none (d3d9-rt already
  depends on dxmt-core and on mtld3d-core).
- **New pub items:** `Occlusion` (`Default`), `OcclusionAnswer` (`Debug`, `PartialEq`, `Eq`, `Clone`, `Copy`:
  derives, for the tests' comparisons), `CoreDevice::{event_stamp, event_done, occlusion_begin, occlusion_end,
  occlusion_poll, occlusion_release}`. **New state:** the query's core state on `QueryInner` (a `RefCell`, or
  an `inner_mut` with one `unsafe` block: above); on `CoreDevice` the flag "an occlusion query has begun" and
  the count of open brackets.
- **Duplicates, named:** the bracket's order rules exist in mtld3d's `visibility.rs` over its mailbox; the
  core's are the same answers over dxmt-core's `VisibilityQuery` (the mailbox is the PE/native split the core
  does not have). `OcclusionAnswer` restates mtld3d's `QueryStatus` with the count in it (that type lives with
  the mailbox). The hand-built disabled record repeats the shape `Queries::visibility_mode` builds (private
  to dxmt-core, and keyed on its own bookkeeping).
- **Not silent:** nothing is refused; a poll that cannot finish answers `S_FALSE`; a bracket left open at the
  device's end logs an error.
- **Divergences added, each stated:** a scaled `StretchRect` into the bound target right after a counted draw
  is counted by mtld3d and not by the core (D3D9 counts draws); END first answers 0 at once on the core, and
  on mtld3d only once that frame retired when polled without the flag (R1 M5; the rows wait with the flag);
  the core has no slot cap (mtld3d answers `u32::MAX` past 1024 slots a frame, H10); mtld3d rescales a count
  by its render scale, the core has none.

## Rulings (planning)

- **Q5-1. dxmt-core's `VisibilityQuery` as it is, a fresh one a BEGIN**, owned by d3d9-rt's `Occlusion`. Cost
  if wrong: an allocation a BEGIN (48 bytes).
- **Q5-2. H1 is settled in d3d9-rt** (`issue(end_seq, &[])` once the END's chunk retired), not by changing
  `take_visibility` in `src/dxmt` (D3D10-12 share it: the x64 check, and a behaviour change for them). Cost if
  wrong: `qy-present` shows it at the first lane; the fallback is the `src/dxmt` change with the x64 check in
  this slice. The gap exists for D3D11 too: a follow-up for the maintainer.
- **Q5-3. EVENT on the ring's clocks with mtld3d's rules**, not D3D11's shared-event sequence (a record to
  emit and a stall counter). `coherent_seq` is the last chunk whose command buffer the finish thread waited
  to completion and whose read-backs it then dropped (`queue.rs` `finish`: `waitUntilCompleted`,
  `chunk.reset()`, `ring.finish(seq)`, in that order), so "retired" is GPU-done with every result issued: the
  same clock the read-back locks wait on. Cost if wrong: none seen.
- **Q5-4. A poll with the flag waits (`sync_to`)** for an ended span, as mtld3d blocks until the END's frame
  retires.
- **Q5-5. Every drawing pass d3d9-rt records starts with the disabled mode** (H20). Cost if wrong: one
  record a pass. If `qy-pass` is red under core all the same, the choice is between another d3d9-rt record
  and a change in `src/dxmt` (which brings the x64 check into this slice): decided then, ledgered.
- **Q5-6. The opt-in config keys keep working under core**, in mtld3d's order (above). Held by no row and no
  test (the d3d9 crate has no host tests of its entries): by review alone, stated.
- **Q5-7. The core does not count a `StretchRect`**, where mtld3d counts one shape of it. Cost if wrong: a
  title that relies on mtld3d's count there, which D3D9 does not give either. Follow-up for the maintainer:
  mtld3d's scaled `StretchRect` quad joins a counting pass.
- **Q5-8. The disabled record only after a device's first OCCLUSION BEGIN** (R2). Cost if wrong: none seen:
  before it no pass of the device has counted.
- **Q5-9. Open brackets counted on the device, an error line at its end** (R3 I4), so the gates' existing
  `was not recorded` count holds the front end's Release arm. Cost: one counter.
- **P-5. One probe**, since a draw file changes.

## Stated gaps

The count on a multisampled target is Metal's (samples) on both runtimes: not judged. `test_occlusion_query`
does not run under core until fullscreen devices exist (C4c-6). An OCCLUSION poll without the flag and without
a `Present` never finishes, as on mtld3d. No slot cap. A query's count across a `Reset` (C4c-6). An EVENT poll
that commits while a dynamic buffer is mapped (legal by the recorder's code: R2; no test). No row for a
`StretchRect` right after a counted draw (the divergence above). The i686 visibility buffer's placement and
the VM guest's read-back of it are first run by the chain's `jc`.

## Review dispositions

Three reviews, 2026-11-19: R1 design (0 Critical, 6 Important, 9 Minor), R2 recorder (1, 2, 8), R3 execution
(0, 4, 13). **Held by all three from the code**: the H1 settle (no read-back runs after `coherent_seq >=
end_seq` is seen; the settle adds an empty range, so it neither loses nor doubles a count; a query leaves
`pending` in its END chunk with or without results), the fresh `Arc` a BEGIN (needed: `end()` can mark a span
ready while a read-back still holds it), a draw pass kept open across BEGIN or END counts the right draws,
EVENT's commit from `GetData` is legal, the two bump sites cover every draw path, every row's expectation is
mtld3d's answer by reading. Folded as cited above: R2's Critical (the clear-quad sites dropped: unneeded, and
a record outside a pass there); the divergence on `StretchRect` and what `qy-pass` holds; counting cells as
`until` attempts; the proxy-draw cell; `GetData`'s order with the opt-ins; the `RefCell`; the unit tests
reworded to what the fakes print; the Release arm's place and its gate; the probe spelled out; the control's
rule for a red cell; the sweep first; mutants c and d's cells, mutant e. Decided against a reviewer: **R1's
split of the bump into its own commit** (Q5-8 leaves a device without queries recording what it did);
**R1's and R2's cell with a `StretchRect` right after a counted draw** (red on mtld3d by I2's own finding;
a unit test holds the record's place, nothing holds its mode there: stated). Not taken: a kill timer on the
validation launches (the waits are bounded at 5 s a cell).

## Execution notes

(after execution)
