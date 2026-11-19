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
  flag the call waits for it. Only an application's draws count: a Clear, a `StretchRect`, a `ColorFill`, a
  resolve, an upload do not.

After it no launch and no Wine suite meets the refusal "IDirect3DDevice9::CreateQuery".

Wine (map 4.2, 5): no "Test failed" line is caused by the refusal today. `test_refcount`'s device-refcount
check of its query starts to run (device.c:1802; predicted to pass: the wrapper is unchanged).
`test_sample_attached_rendertarget` (visual.c:26344) stops skipping and runs for the first time under core:
**not predicted** (it samples the texture bound as the target, under `todo_wine_if`); the sweep measures it.
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
  committed like any other (`sync_to` already does so): no "clean" special case (H17 not taken; cost: one empty
  command buffer at the first poll of an END issued with nothing recorded).
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
    every later pass for the device's life).
- **Draws count, nothing else does.** `draw.rs`: `enc.bump_visibility_result_offset()` right before each of the
  two draw records (map 2.2; with no query active it tests one counter and records nothing).
  **Every render pass d3d9-rt records that draws starts with the visibility mode "disabled"** (H20, new, from
  the core's merge rule: a merged pass runs in the earlier pass's encoder, so every pass re-emits its state;
  the encoder's offset bookkeeping starts each recorded pass at "no offset" and records a mode only on a
  change, so a pass with no query active records none, and merged behind a counting pass it would count its
  draws: an application's draws after END as well as d3d9-rt's own quads): one
  `WmtcmdRenderSetvisibilitymode { DISABLED }` where the draw pass starts (`open_pass`'s closure), in
  `encode_triangle_state` (the `StretchRect` quad and the depth resolve) and after `quad.begin` in the two
  clear-quad closures (`recorder.rs`, `depth.rs`). Metal takes the disabled mode with or without a result
  buffer, and a counted draw's own record follows it in the same pass. D3D10-12 share the shape (a draw pass
  with no query active behind a counting one): a follow-up for the maintainer, not this slice's.

### Front end

- `device_create_query`: the `refuse_under_core!` goes; the rest (the type table, the support probe, the
  wrapper, the device reference) is mtld3d's.
- `QueryInner` gains, behind the feature, the core's state (an EVENT's seq is the existing `end_seq`; an
  OCCLUSION's `Occlusion`), guarded as the wrapper's other state is (the API lock is held in every entry).
- `query_issue`, `query_get_data` and `close_released_span` each take a core arm keyed on the device, placed
  **before `encoder_status()`** (an mtld3d recorder read; the core has no latched failure an application
  sees: H16) and, in `GetData`, **after the two config opt-ins** (`query.eventImmediate`,
  `query.flushImmediate`: plain config reads that keep their meaning on both runtimes). The D3D answers stay
  in the front end and are mtld3d's functions where they exist (`write_occlusion`, the `0xdd` fill, the BOOL
  width): `Never` → `S_OK` + `0xdd`; `Pending` → `S_FALSE` and what mtld3d writes then (row 5 judges the buffer
  untouched; the control confirms it); `Count(n)` → `S_OK` + `n`.
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
a depth-stencil a row binds itself), judged in every lane; each count is of pretransformed quads whose edges
sit on pixel boundaries, so it is exact; each wait is `GetData(NULL, 0, D3DGETDATA_FLUSH)` polled for at most
5 s (Wine's `wait_query`). Twelve rows:

1. `qy-support`: the support probe and the creation for EVENT and OCCLUSION (`D3D_OK`), and for VCACHE,
   VERTEXSTATS, TIMESTAMP, TIMESTAMPDISJOINT, TIMESTAMPFREQ and type 3 (`D3DERR_NOTAVAILABLE`, the out pointer
   still `0xdeadbeef`); `GetType`, `GetDataSize` (4 and 4), `GetDevice`; the device's reference count one up
   with a query alive and back after its Release.
2. `qy-event`: never issued: `GetData(&b, 4, 0)` `S_OK`; a quad, `Issue(END)`, the wait, then `GetData(&b, 4,
   0)` `S_OK` with TRUE; `GetData(NULL, 0, 0)` `S_OK`; **a second END after another quad, polled with flags 0
   and no `Present`, is `S_OK` within 5 s** (the poll submits).
3. `qy-count`: BEGIN, a 64x32 quad in a scene, END, the wait: `GetData(&d, 4, 0)` is 2048; size 2 writes the
   low WORD alone; size 8 the count and a zero high DWORD; size 0 leaves the buffer; then, a depth-stencil
   bound and Z on, an occluder at z 0.1 over the quad's left half and the quad at z 0.5: 1024.
4. `qy-empty`: never issued: `GetData(NULL, 0, FLUSH)` `S_OK`, and four bytes of `0xdd` with the next DWORD
   untouched; END first: 0; BEGIN, END with no draw: 0; BEGIN|END in one call: 0; 1000 BEGIN/END pairs in one
   scene with no draw: 0.
5. `qy-open`: BEGIN, a quad, `GetData(&d, 4, FLUSH)` while open: `S_FALSE`, `d` untouched; END, the wait, the
   count.
6. `qy-twice`: BEGIN, quad A (2048), BEGIN, quad B (600), END: 600; END again `D3D_OK` and still 600.
7. `qy-present`: BEGIN, a quad, `Present`, END with nothing drawn after it: the count (H1); BEGIN, quad A,
   `Present`, quad B, END: A + B.
8. `qy-two`: Q1 BEGIN, A, Q2 BEGIN, B, Q1 END, C, Q2 END: Q1 = A + B, Q2 = B + C.
9. `qy-pass`: on a 64x64 target T: BEGIN, quad A, **a Clear with a rect, a scaled `StretchRect` into a rect of
   T, a `ColorFill` of a rect of T**, quad B, then a second target U bound and quad C, END: A + B + C; T's
   texels show the Clear, the copy and the fill. **Then a draw after END** (H20): BEGIN, quad A on T, END, a
   quad on U, quad D on T, all in one scene with no read between: A alone.
10. `qy-release`: BEGIN, a quad, the query released while open; a new query: BEGIN, quad B, END: B.
11. `qy-reuse`: one query three times (three sizes of quad), each count read; then BEGIN, quad, END, BEGIN,
    another quad, END with no read between: the second count.
12. `qy-noflush`: BEGIN, a quad, END; `GetData(&d, 4, 0)` polled across `Present`s for at most 5 s: the count.
    (Without the flag and without a `Present` it finishes on neither runtime: stated, not judged.)

- Scripts (map 4.1; half-done registrations fail silently): `dxmt/d3d9-check.sh` (the launch, its `check`
  list, **`queries` in the "none" refusal list**, the header), `vm/dxmt/d3d9-gate.sh` (the row loop, the
  `runtime core` names, the header), `vm/tests/d3d9.sh` (`qu64-fresh`, `qu32-fresh`, the header), and
  `vm/tests/d3d9-gate-bench-test.sh` if it lists launches; the test file's header. `sh -n`.
- Job scripts: `QUERY_ROWS` 12 with a `qrows` counter in `head.sh`; `jcr.sh` and `jc.sh` count `qu64`/`qu32`
  ok lines (12 each, one more under core); `jv0.sh` runs `queries`; `jq.sh`'s list gains `queries` (twelve
  launches under validation).
- Controls after 4d's push, in this order: `JOBS=jv0` (`queries` on mtld3d under the validation layer:
  reports 0, rows ok 14: **the rows' expectations are mtld3d's answers and are confirmed here; a red cell is
  either a wrong expectation, corrected in a second row commit with the reason, or an mtld3d finding,
  ledgered**); `ja` (default and msl green; core red on exactly the twelve `queries-qy-*` tokens and
  `queries-runtime`: one refusal line where none is wanted); `jcr` (the VM's default lane: 12 `qu64` and 12
  `qu32` ok lines; every other launch as 4d left it).

Commit: "dxmt, vm: d3d9_triangle rows for queries (DXMTRS C4c-5)".

### Task 2: queries in d3d9-rt

One commit, "d3d9: EVENT and OCCLUSION queries in the core recorder". Tests on the fakes (what they can show
is settled first: H19; a fake that cannot run a read-back still shows the records): `event_done` true at a
retired seq, false and one commit at the recording seq, no commit at an older unretired seq; the bracket's
life (one begin and one end closure a bracket: BEGIN twice records end then begin; Release while open records
an end; END first records nothing and polls `Count(0)`; END twice records one end); `occlusion_poll` `Never`,
`Pending` while open with and without the flag, no commit without the flag; **H1: BEGIN, a draw, commit, END,
poll with the flag: `Count`** (with the settle removed: `Pending` for ever: the implementer's own red check);
a draw between BEGIN and END records the visibility mode before its draw record and none with no query
active; the quad of `stretch`, the depth resolve and both clear quads record the disabled mode.

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
  c. `event_done` never commits → `qy-event` (its last cell);
  d. no pass records the disabled mode → `qy-pass` (the first count holds the copy's texels, the second quad
     D), **where the later pass merges behind the counting one**; a mutant that stays green is ledgered as
     "not held by a row" and the record stays (it costs one record a pass).
  Held by unit tests: one end a begin (H2, H3), the fresh `Arc` a BEGIN (H4), no commit without the flag.
- `jw`: the sweep → `build/c4c/s5/sweep.md`.
- **Probe: once** (`draw.rs` changes: one call a draw). The `tex` bench under core, base against tip, one run
  each (maintainer: benches once), with the load printed beside it.

### Task 5: review. Task 6: spec, pin, push

As the 4d plan's, without the x64 check.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none (d3d9-rt already
  depends on dxmt-core and on mtld3d-core).
- **New pub items:** `Occlusion`, `OcclusionAnswer`, `CoreDevice::{event_stamp, event_done, occlusion_begin,
  occlusion_end, occlusion_poll, occlusion_release}`. **New state:** the query's core state on `QueryInner`.
- **A duplicate, named:** the bracket's order rules exist in mtld3d's `visibility.rs` over its mailbox; the
  core's are the same answers over dxmt-core's `VisibilityQuery` (the mailbox is the PE/native split the core
  does not have).
- **Not silent:** nothing is refused; a poll that cannot finish answers `S_FALSE`.
- **Divergence added:** none intended. Stated: the core has no slot cap (mtld3d answers `u32::MAX` past 1024
  slots a frame, H10); mtld3d rescales a count by its render scale, the core has none.

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
- **Q5-6. The opt-in config keys keep working under core.**
- **P-5. One probe**, since a draw file changes.

## Stated gaps

The count on a multisampled target is Metal's (samples) on both runtimes: not judged. `test_occlusion_query`
does not run under core until fullscreen devices exist (C4c-6). An OCCLUSION poll without the flag and without
a `Present` never finishes, as on mtld3d. No slot cap. A query's count across a `Reset` (C4c-6).

## Review dispositions

(after the reviews)

## Execution notes

(after execution)
