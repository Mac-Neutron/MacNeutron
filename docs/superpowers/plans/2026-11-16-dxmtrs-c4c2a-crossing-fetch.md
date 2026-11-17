# DXMTRS C4c-2a: crossing attributes and odd stream offsets on D3D9's core runtime, the index-format helper, a buffer bench

Status: draft, before reviews. Parent: `2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4), spec §5 (`C4c's slices`).
Map: `build/c4c/s2/map.md` (cited "map §n"). Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, a draw whose vertex layout has an attribute that ends past its stream's stride, or a
stream bound at an offset that is not a multiple of four, draws what mtld3d draws, where C4c-1 refuses both by
name. The four copies of the `D3DFMT_INDEX16/32` table become one helper. `d3d9_triangle --bench` gains two modes
that draw from buffers, so that C4c-2b (dynamic buffers mapped directly) has a figure to move.

## Ruling S-1: C4c-2 is two slices

C4c-2 as the spec lists it (dynamic buffers direct, `CrossingFetch`, the WRITEONLY backing's release, the gauge
and the 32-bit cap) is split:

- **C4c-2a (this plan):** the index-format helper, crossing attributes and odd offsets, the buffer bench.
- **C4c-2b:** dynamic buffers mapped directly (dxmt-core's `DynamicBuffer`, `plan_lock`), the WRITEONLY backing's
  release, the retained-bytes gauge (G3) and the 32-bit cap.

Why: the two halves share no code (2a is the vertex descriptor and the binds; 2b is allocation and lifetime), 2a
changes no line of dxmt-core, and 2b needs the bench 2a adds to state its gain. Cost if wrong: one more push and
one more gate chain than a single slice.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- No line of `src/dxmt/src` (dxmt-core), `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal` changes. If a task
  finds it needs one, it stops and reports (the D3D10-12 gate would then be owed).
- The default runtime (mtld3d) draws exactly what it draws today: its translator key, its imports and its
  conformance are gates. The only mtld3d lines touched are the three index-format sites of Task 2.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree; job scripts live in `build/c4c/s2/gates`, test committed tips, and a job that has run
  is never edited.
- **Before the first build:** fetch both origins and rebase `c4c` / `c4c-mn` onto the C-to-Rust port's second
  landing (it moves DXMTRS's LLVM pin and the translator key); the wanted key is the new main's. If that landing
  is not pushed yet, ask that session before building.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller (the front end is a staticlib: dead code fails the gate).
- One lane (default, core) per task that moves a row; full gates once; no fix round for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8 below. Out, with where it goes:

- Dynamic buffers direct, backing release, gauge, cap: C4c-2b.
- A crossing attribute on an instanced stream, more than two crossing attributes, point and line primitives over
  a crossing layout: no row (Stated gaps); the rules are mtld3d's, shared.
- Intel and AMD GPUs: mtld3d's own open question (`streams.rs:336-341`), unchanged.

## The mechanism (what is ported, and from where)

mtld3d's crossing fetch is not a shader fetch and not a CPU re-pack (map §2.1). It lives in mtld3d-core,
`src/d3d9/core/src/streams.rs`, which d3d9-rt already links:

- `remap_crossing_attributes` moves each crossing attribute to a Metal slot of its own at descriptor offset 0,
  with its stream's layout, and that slot binds the **same** buffer advanced by the attribute's offset. Slots are
  taken among the 16 stream slots only (a stream keeps its slot while one of its attributes fits).
- A stream offset off four bytes binds at the multiple of four below, and every attribute its slots feed gains
  the remainder (`offset_shift`, `CrossingFetch::rebuild`).
- `CrossingFetch::new(attrs, layouts, shifts)` does both; `slots_of`, `check_advanced_offsets`,
  `slot_binding_offset`, `advanced_binding_offset`, `inline_vertex_span` are the draw-time helpers.

The core calls these same functions. Nothing of the rule is written twice.

## Review Focus

1. **Parity of the condition and the inputs.** mtld3d builds a fetch when `crossing != 0 || offset_shift(offsets)
   != 0` (`unix/src/draw.rs:846-858`) from `stream_layouts_view`'s layouts and offsets. Which streams contribute
   a shift there (only streams the draw reads from a vertex buffer? inline streams? a null stream?) and does
   Task 3's `pipeline_source` read the same set? Does the key then hold exactly what moves the pipeline?
2. **The error cases.** mtld3d on a `VertexFetchError` logs and widens the layout ("the draw fetches wrong
   data"). This plan refuses by name under core (ruling 2). Is every error reachable, and is the refusal
   placed before anything is recorded?
3. **Inline (UP) streams.** `bind_crossing_inline` binds `address + advance` over a payload zero-padded to the
   crossing attribute's end (`arena_alloc_zero_padded`). Does Task 4's "the fed bytes from `advance`, then
   `advance` zero bytes" read the same bytes for every vertex, the last included?
4. **Metal's validation layer.** A shifted attribute sits at a descriptor offset off four bytes and may end up
   to 3 bytes past the stride. Task 1's control runs mtld3d's own offset-2 draw under the layer first. What does
   the plan do if it reports (ruling 3)?
5. **The store and the order.** Slots that read one buffer twice: `access_buffer`/`bind_buffer` twice in one
   closure, the staged upload before the draw once (`core_flush` is per D3D9 buffer, not per slot).
6. **Rows.** Does each row fail for the mutant named beside it, and pass on mtld3d by construction (not by
   accident of zero-filled memory)?

## Tasks

### Task 1: rows, bench modes, controls (MacNeutron)

Files: `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`.

- `buf-refused` is replaced by the seven rows of the Appendix (`buffers`: 27 − 1 + 7 = 33 rows). Each cell is
  written so that it passes on mtld3d; under `strict` (core) the expectation is the same pixels, except
  `buf-wide`, which under strict answers `D3DERR_NOTAVAILABLE` with nothing drawn.
- The buffers launch's refusal set becomes one line, `BUFFER_FETCH` of Task 5 (reached by `buf-wide`); the two
  C4c-1 lines go. Both scripts, their header comments, `sh -n` on both.
- Bench: two modes beside `ff|prog|tex|texdepth|lit` (map §6.1 says how): `buf` (one static WRITEONLY vertex
  buffer of `benchDraws` RV triangles, each draw `DrawPrimitive(TRIANGLELIST, 3 * d, 1)`) and `dyn` (one
  DYNAMIC|WRITEONLY buffer of 256 triangles: each draw locks the next triangle `NOOVERWRITE`, or `DISCARD` at
  the wrap, writes it, unlocks, draws it). Same output line, modes `buf` and `dyn`. Header comment.
- Controls, before any DXMTRS code, at DXMTRS main + this commit: `ja` (Mac default, msl: every row green;
  core: the seven new rows red, the rest as C4c-1), `jcr` (VM default, old WoW64), and **`jv0`**: the buffers
  launch on the **default** runtime under Metal's validation layer (the environment `jq.sh` of C4c-1 uses for
  core launches), reports counted. `jv0` answers Review Focus 4 before code exists.
- Commit: "dxmt, vm: d3d9_triangle rows for crossing attributes and odd stream offsets; buffer bench modes
  (DXMTRS C4c-2a)".

### Task 2: the index-format helper (mtld3d-core, front end)

- `src/d3d9/core/src/convert.rs`: `pub const fn index_format(format: u32) -> Option<(IndexType, u32)>`
  (`D3DFMT_INDEX16 → (UInt16, 2)`, `D3DFMT_INDEX32 → (UInt32, 4)`, else `None`), with a unit test in
  `convert/tests.rs`.
- The four sites call it (map §5): `device.rs` `bound_index_fan`, `snapshot_bound_index_source`, the mtld3d
  `DrawIndexedPrimitiveUP`, and `core_draw.rs`'s `index_kind` (deleted; its three callers take
  `index_format` and narrow where they need a `u8`). Each site keeps its own log line for the `None` arm.
- Lane: none (no row moves; rust-check's tests and Task 7's conformance are the proof for mtld3d).
- Commit: "d3d9: one table for the index formats".

### Task 3: the key's shift and the fetch in the pipeline source (mtld3d-core)

Files: `src/d3d9/core/src/pipeline_key.rs` (+ its tests).

- `StreamKey::Bound { stride, step, rate, shift }`: `shift: u8` is `offset_shift` of the stream's offset (0-3).
  Same derives. Every constructor and match in the tree is updated in this commit (d3d9-rt tests and the front
  end's `bound_streams` pass `shift: 0` until Task 5 fills it: the offset refusal still stands in d3d9-rt until
  Task 4, so no draw is shifted before it can be fetched).
- `PipelineSource`:
  - `attrs` and `layouts` are **per Metal slot** (doc changed): the remapped ones when a fetch is built.
  - new `streams: [StreamLayout; MAX_STREAMS]`: the layouts per D3D9 stream, before any remap (what `layouts`
    was).
  - new `bindings: [(u8, u32); MAX_STREAMS]`: for slot `n`, the stream it binds and the byte advance; the
    identity `(n, 0)` without a fetch.
  - `crossing: u16` stays (the draw's offset check reads it).
  - new `fetch: Result<(), VertexFetchError>`: the error of a layout no binding can fetch.
    `crosses_stride()` is deleted; `fetchable()` returns `fetch.is_ok()`.
- `pipeline_source`: after the per-stream layouts and `crossing`, `shifts` = the `shift` of each `Bound` entry
  among `resolved.used_streams`, two bits a stream (`stream_shifts`' packing). If `crossing != 0 || shifts != 0`:
  `CrossingFetch::new(&resolved.attrs, &layouts, shifts)`; on `Ok`, `attrs`, `layouts` and `bindings` come from
  the fetch (`attrs()`, `layouts()`, `slots_of` or a new accessor `bindings()` in `streams.rs`, with a test);
  on `Err`, `fetch` holds it and the other fields keep the unmapped values.
- Tests (mtld3d-core): a crossing key's source has the attribute at a spare slot, offset 0, the stream's layout
  there and the binding `(stream, offset)`; a shifted key's attributes gain the shift; shift and crossing
  together; an attribute wider than its stride is `Err`; a key with no crossing and no shift is unchanged
  (the existing parity test `layouts_are_mtld3ds_for_every_feed` still passes over `streams`); two keys that
  differ only by `shift` are different keys with different sources.
- d3d9-rt in this commit: mechanical only (`source.layouts` → `source.streams` where it means "per stream":
  the `used` mask, the zero feed's stride, the unbound-stream warning; `crosses_stride` → `!fetchable()` with
  `DrawRefused::Crossing` kept). `build_pipeline` reads `attrs` and `layouts` as it does. No behaviour moves:
  crossing and shifted draws are still refused.
- Lane: none (no row moves). Commit: "d3d9: a stream's offset shift in the pipeline key, the crossing fetch in
  the pipeline source".

### Task 4: the binds (d3d9-rt)

Files: `src/d3d9/rt/src/device/draw.rs` (+ tests), `src/dxmt/fakes` only if a printed field is missing (the
fakes already print `attr … buffer=` and `layout <slot> …`, C4c-1).

- `DrawRefused::Crossing` and `DrawRefused::Offset` are deleted; `DrawRefused::Fetch` ("a layout no binding can
  fetch") replaces them. `pipeline()` returns `Err(Fetch)` for a source with `fetch.is_err()`; such a key builds
  no pipeline, keeps handle 0 and logs no "does not build" (as a crossing key today), and logs the
  `VertexFetchError` once at debug.
- `draw`: `used` (streams) from `source.streams`. For each slot `n` whose `source.layouts[n]` is used, with
  `(stream, advance) = source.bindings[n]` and `data = draw.streams[stream]`:
  - `StreamData::Buffer { id, offset }`: before anything is recorded, when `advance != 0`,
    `advanced_binding_offset(offset, advance, buffer.length())` must be `Ok`, else `Err(Fetch)`; the slot binds
    the buffer at `slot_binding_offset(offset, advance)`. The store lookup error stays `Buffer`.
  - `StreamData::Bytes(fed)`: the slot binds `fed[advance..]` followed by `advance` zero bytes when
    `advance != 0` (the last vertex's crossing attribute then reads zeros past the payload, as mtld3d's padded
    payload gives it), `fed` whole otherwise. `advance >= fed.len()` is `Err(Fetch)`.
  - `StreamData::Zeros`: as today (a null stream's layout steps by its extent and never crosses; a
    `debug_assert!(advance == 0)`).
- The offset refusal block is deleted: a shifted stream's own slot binds at `slot_binding_offset(offset, 0)`.
- Tests (d3d9-rt, on the fakes' capture): a crossing buffer draw prints two `layout` lines of the same stride,
  the attribute at the spare slot with offset 0, and two vertex-buffer binds of one buffer whose offsets differ
  by the attribute's offset; a shifted draw binds at the multiple of four below with the attribute offsets
  moved; an inline crossing draw binds bytes that are the payload from `advance` plus `advance` zeros; an
  advance past the buffer is `Err(Fetch)` with nothing recorded (the capture is empty); a wider-than-stride key
  is `Err(Fetch)` at every draw and builds nothing.
- The front end in this commit: `record` maps `DrawRefused::Fetch` to the two strings of Task 5 (the strings
  move here: the three old constants go, two come), and `bound_streams` fills `shift` (so the commit is whole:
  d3d9-rt no longer refuses an odd offset, and the key says it). Task 5 is then only what the front end adds
  beyond the mapping; if nothing is left, Tasks 4 and 5 are one commit and the plan's notes say so.
- Lane `jt4` (default, core): default ok; core: the seven rows green strict, `buffers-runtime` with its one
  line.
- Commit: "d3d9: crossing attributes and odd stream offsets on the core runtime".

### Task 5: the front end's remainder

- `core_draw.rs`: `UP_FETCH = "IDirect3DDevice9::Draw*PrimitiveUP (a layout no binding can fetch)"`,
  `BUFFER_FETCH = "IDirect3DDevice9::Draw*Primitive (a layout no binding can fetch)"`.
- The UP arms: what mtld3d checks before a crossing UP draw that the core arm does not (the payload's size
  against `inline_vertex_span`, the zero padding) is read against `device.rs`'s UP path; anything the answer
  depends on is taken in the same order as mtld3d's.
- Docs in the code that C4c-1 wrote and this makes untrue (`pipeline_key.rs` "the core does not have yet",
  `draw.rs:297-299`, the `record` doc) are corrected in the commit that makes them untrue.

### Task 6: gates

Job scripts in `build/c4c/s2/gates`, copied from `build/c4c/s1/gates` under new names and edited before their
first run (`head.sh`, `jt.sh`, `jq.sh` with ten core launches under the validation layer, `jm.sh`, `jr.sh`,
`jc.sh`, `jp.sh`, `jp0.sh`, `post.sh`; the wanted translator key is the rebased main's).

- `jq`: d3d9-check on default, msl, core; stateblock under default and core; ten core launches under Metal's
  validation layer with 0 reports (subject to ruling 3).
- `jm`: mutants, each a commit off the tip (`gates/mutants.txt`), each red on its row and on nothing outside
  the buffers launch:
  a. the advance dropped from a crossing slot's binding (binds the stream offset) → `buf-cross`;
  b. the shift not added to the attributes (`shifts` forced 0 in `pipeline_source`) → `buf-odd`;
  c. `shift` left out of the key's entry by the front end (always 0) → `buf-odd` (its second cell, the pipeline
     of the aligned draw reused);
  d. the inline slot binds `fed` whole → `buf-cross-up`;
  e. the spare slot taken without looking at the streams read (`!(reserved)` for `!(reserved | read)`, in a
     copy of the rule reached only by the mutant build) → `buf-cross-multi`;
  f. the advanced-offset check dropped → the d3d9-rt unit test only (no row: ruling 4); listed, not built as a
     row mutant;
  g. `index_format` swapped (16 ↔ 32) → `buf-indexed` and `buf-index32` (and mtld3d's rows in the default
     launch: this mutant alone is expected red outside the buffers launch, in `buffers` on default).
- `jr`: plain build: translator key as wanted, imports the same, d3d9-check default, the Mac Wine conformance
  with no new site, the x64 check (the minimised-presents row is known to fail while the Mac is in use: one
  rerun by the post-push check, not a loop).
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64. No D3D10-12 gate (Global Constraints; proven
  by `git diff --stat` before the push).
- `jp` / `jp0`: the probe on the tip and on the base: `--bench 2000 <0|600>` in modes `tex`, `buf`, `dyn`, on
  mtld3d and core, each run once → `build/c4c/s2/probe.md`. `jp0` needs the bench modes (MacNeutron tip) on
  the DXMTRS base: `buf` and `dyn` run there as they are (C4c-1 draws both).

### Task 7: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s2/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-2a" and the
slice list amended (2 → 2a, 2b). Plan execution notes. `dxmt/pins`. Privacy scan of both ranges. Fetch,
rebase if a main moved (then rust-check, one lane, the plain build). Push DXMTRS then MacNeutron. Post-push
check. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none.
- **A config key:** none.
- **A wire field:** none (the vertex descriptor's fields exist; no unix call changes).
- **A dependency:** none (d3d9-rt already depends on mtld3d-core).
- **A derive:** `StreamKey` keeps its derives with one more `u8` field; `PipelineSource` keeps `Debug,
  PartialEq, Eq`, so `VertexFetchError` needs `Clone, Copy` only if a test copies it (it has `Debug, PartialEq,
  Eq`); `tools/derive_inventory.txt` follows what the code derives.
- **A lint suppression:** none.
- **A duplicate:** none added; one removed (the index-format table). The crossing rule is called, not copied.
  Mutant (e) is a patch of the shared rule, built only as a mutant.
- **Not silent:** the one refusal left is logged by name and held by the buffers launch's set; the
  `VertexFetchError` is logged once a key.
- **Refusal literals:** three go (`CROSSING`, `BUFFER_CROSSING`, `OFFSET`), two come (`UP_FETCH`,
  `BUFFER_FETCH`).
- **Divergences kept on purpose:** ruling 2 (written in the spec paragraph; `docs/d3d9/STATUS.md` records
  mtld3d's, and takes the core's when core ships).

## Rulings (planning)

1. **The shift is in `StreamKey::Bound`, not a field beside the streams.** Why: it is a property of one stream's
   binding and only `Bound` streams have one. Cost if wrong: a stream bound at an odd offset that the vertex
   stage does not read splits the key (a duplicate pipeline, the gap C4c-1 already states for stride).
2. **A layout no binding can fetch is refused by name under core** (an attribute wider than its stride, an
   advanced binding outside its buffer or off four bytes, no free slot), where mtld3d logs, widens the layout
   and draws wrong data. Why: the core's rule since C4b is that what it cannot draw right is said and counted,
   and the widened draw's pixels are nothing a row can hold. Cost if wrong: a title with such a layout shows
   wrong geometry on mtld3d and none on core.
3. **If Metal's validation layer reports on a shifted attribute** (`jv0`, on mtld3d's own draw): the odd-offset
   cells run outside the validated launches (a `buffers-novalidate` filter in `jq`), the report is written in
   the spec paragraph, and nothing else changes: mtld3d ships that descriptor today. If `jv0` is clean, the
   gate is 0 reports in all ten launches. Cost if wrong: a validation report that marks a real fault is waved
   through for these cells; the pixels of the same cells are still asserted on the Mac and in the VM.
4. **No row for an advanced binding outside its buffer.** Why: the draw reads past the buffer's end in D3D9
   terms; mtld3d's pixels are undefined. The d3d9-rt test holds the refusal. Cost if wrong: unpinned wording.
5. **`buf-wide` asserts only the answer on mtld3d** (`D3D_OK`), no pixel. Why: "the draw fetches wrong data".
6. **No D3D10-12 gate.** As C4c-1's ruling 6, with the diff stat as proof.
7. **The UP crossing string is not held by a launch's set** unless a row reaches it; `buf-wide` has a UP cell
   only if the rows agent finds mtld3d answers it `D3D_OK` without drawing garbage outside the frame. Cost if
   wrong: `UP_FETCH`'s wording is unpinned.

## Stated gaps

- No row: a crossing attribute on a per-instance stream; three or more crossing attributes; a crossing layout
  under the fixed-function vertex stage (FVF layouts never cross: the stride is the application's, so
  `SetFVF` + a short stride is possible: one cell in `buf-cross` if mtld3d draws it, else stated here in the
  notes); points and lines.
- The bench's `dyn` mode on core is the staged model's worst case until C4c-2b.
- Intel and AMD GPUs: mtld3d's own open question.

## Appendix: the rows (`buffers`: `buf-refused` goes, seven come)

All on the `bufStart()` device (no depth), blue clear, colours and probe points distinct from every other
cell of the frame, each cell's bytes written so that a wrong offset or slot reads a **non-zero wrong** value.

1. **`buf-odd`**: frame 1: the RV left quad from a buffer at stream offset 0 (red), then with the same
   declaration the right quad from a buffer whose vertices start at byte 2, bound at offset 2 (green): both
   visible (the second pipeline is not the first's). Frame 2: offsets 1 and 3 (two more quads). Frame 3: offset
   6 with a start vertex of 1. Frame 4: `DrawIndexedPrimitive` at offset 2 with a base vertex.
2. **`buf-cross`**: C4c-1's stride-12 cell (`VS_CROSS`, position `FLOAT3` at 0, `TEXCOORD0 FLOAT2` at 8,
   stride 12: 0xff8000 at left) through `DrawPrimitive`; a second cell where the texcoord's bytes that a
   non-advanced binding would read give another colour.
3. **`buf-cross-up`**: the same floats through `DrawPrimitiveUP` (stride 12) and `DrawIndexedPrimitiveUP`.
4. **`buf-cross-multi`**: stream 0 at stride 12 with one attribute that fits and two that cross, stream 1
   bound and read (a `D3DCOLOR`), so a spare slot must skip stream 1's; a second cell with a stream none of
   whose attributes fits (stride 8, one `FLOAT2` at offset 4).
5. **`buf-cross-odd`**: the stride-12 layout at stream offset 2.
6. **`buf-cross-indexed`**: `DrawIndexedPrimitive` with a base vertex over the stride-12 layout; a fan over it
   (mtld3d's rewrite).
7. **`buf-wide`**: stride 8 under a `FLOAT3` position: under strict `D3DERR_NOTAVAILABLE` and the clear colour;
   elsewhere `D3D_OK` and no pixel asserted.
