# DXMTRS C4c-2a: crossing attributes and odd stream offsets on D3D9's core runtime, the index-format helper, a buffer bench

Status: three adversarial reviews folded (`build/c4c/s2/review-{1-design,2-core,3-exec}.md`; dispositions at the
end). Parent: `2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4), spec §5 (`C4c's slices`). Map:
`build/c4c/s2/map.md` (cited "map §n"). Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, a draw whose vertex layout has an attribute that ends past its stream's stride, or a
stream bound at an offset that is not a multiple of four, draws what mtld3d draws, where C4c-1 refuses both by
name. The copies of the `D3DFMT_INDEX16/32` table become one helper. `d3d9_triangle --bench` gains two modes
that draw from buffers, so that C4c-2b (dynamic buffers mapped directly) has a figure to move.

## Ruling S-1: C4c-2 is two slices

C4c-2 as the spec lists it is split:

- **C4c-2a (this plan):** the index-format helper, crossing attributes and odd offsets, the buffer bench.
- **C4c-2b:** dynamic buffers mapped directly (dxmt-core's `DynamicBuffer`, `plan_lock`), the WRITEONLY backing's
  release, the retained-bytes gauge (G3) and the 32-bit cap.

Why: the two halves share no code (2a is the vertex descriptor and the binds; 2b is allocation and lifetime), 2a
changes no line of dxmt-core, and 2b needs the bench 2a adds to state its gain. Cost if wrong: one more push and
one more gate chain than a single slice.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- No line of `src/dxmt/src` (dxmt-core, its `test_support.rs` included), `src/d3d11`, `src/d3d12`, `src/dxgi`,
  `src/winemetal` changes. If a task finds it needs one, it stops and reports (the D3D10-12 gate would then be
  owed). `src/dxmt/fakes` is test support and may change.
- The default runtime (mtld3d) draws exactly what it draws today: its translator key, its imports and its
  conformance are gates. The only mtld3d lines touched are the index-format sites of Task 2.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree. Job scripts live in `build/c4c/s2/gates`, each written (or copied from
  `build/c4c/s1/gates` and edited, `S=$B/c4c/s2` in `head.sh` first of all) **before** its first run; an s1
  script is never run as it is (its `head.sh` empties `s1/gates/$J`); a job that has run is never edited; jobs
  test committed tips.
- **Before the first build:** fetch both origins and rebase `c4c` / `c4c-mn` onto the C-to-Rust port's second
  landing (it moves DXMTRS's LLVM pin and the translator key). If that landing is not pushed yet, ask that
  session before building. The **base** of this slice is then that DXMTRS main and that MacNeutron main, named
  in the ledger; `jp0` and the wanted values below are taken there.
- **Wanted values come from the base, not from old files:** control job `jb` (Task 1) builds the base plain and
  writes its translator key and its imports list; `jr.sh` and `post.sh` are written after it, with that key and
  that imports file (not C4c-1's key, not the C4b-2 imports file).
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper or constant lands in
  the commit of its first caller (the front end is a staticlib: dead code fails the gate).
- One lane (default, core) per task that moves a row; full gates once; no fix round for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-6 below. Out, with where it goes:

- Dynamic buffers direct, backing release, gauge, cap: C4c-2b.
- A crossing attribute on a per-instance stream, three or more crossing attributes, point and line primitives
  over a crossing layout: no row (Stated gaps); the rules are mtld3d's, shared.
- Intel and AMD GPUs: mtld3d's own open question (`streams.rs:336-341`), unchanged.

## The mechanism (what is ported, and from where)

mtld3d's crossing fetch is not a shader fetch and not a CPU re-pack (map §2.1). Its rules live in mtld3d-core,
`src/d3d9/core/src/streams.rs`, which d3d9-rt already links:

- `remap_crossing_attributes` moves each crossing attribute to a Metal slot of its own at descriptor offset 0,
  with its stream's layout, and that slot binds the **same** buffer advanced by the attribute's offset. Slots are
  taken among the 16 stream slots only; the vertex stage's own buffers sit at slots 25-30 (R2).
- A stream offset off four bytes binds at the multiple of four below, and every attribute its slots feed gains
  the remainder (`offset_shift`, `CrossingFetch::rebuild`).

The core builds the remap with `CrossingFetch::new` when a pipeline's source is made, and keeps its three
results as plain data (the remapped attributes, the per-slot layouts, the per-slot `(stream, advance)`), since
`CrossingFetch` derives nothing a `PipelineSource` field needs. At the draw it binds each slot with
`slot_binding_offset` and checks advanced buffer bindings with `advanced_binding_offset`: mtld3d's functions.
One formula is the core's own: an inline slot's bytes (Task 4), equal to mtld3d's padded payload read from the
advance (R1's proof, review-1 §4).

Parity established by R1 (review-1 "Parity, in short"), which the tasks rely on: the streams that shift are the
same on both sides (named by the declaration, a buffer bound, feeding a consumed attribute; inline and null
streams never shift); `crossing` uses the same extents; the layouts handed to the fetch are the same values for
every feed; a zero-stride or null stream never crosses; the length checked is the padded backing on both sides;
indexed draws, base and start vertex, fans and instancing need nothing special.

## Review Focus (for the final review)

1. `pipeline_source`'s shifts and remap against mtld3d's `stream_layouts_view` + `crossing_fetch` for every feed.
2. No `Err` after `open_pass`; the refusal of a key that cannot be fetched is before anything is recorded in
   d3d9-rt (the front end's `core_flush` of the draw's buffers has already run: an upload for a refused draw is
   harmless and stated).
3. The inline slot's bytes for the last vertex.
4. What the validation layer said (`jv0`, `jq`) and how ruling 3 was applied.
5. One buffer at two slots: one upload, two binds, one retention.
6. Each mutant traced to its row.

## Tasks

### Task 1: rows, bench modes, scripts, controls (MacNeutron)

Files: `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`,
`vm/tests/d3d9-gate-bench-test.sh` (its core cases name the `crossing` kind), `vm/tests/d3d9.sh` (says "27
rows").

- **`buffers`:** `buf-refused` is replaced by the seven rows of the Appendix (27 − 1 + 7 = 33 rows; every place
  that lists or counts them). Each cell passes on mtld3d by construction; under `strict` the expectation is the
  same pixels, except `buf-wide`, which under strict answers `D3DERR_NOTAVAILABLE` with nothing drawn.
- **`shaders`:** the existing `crossing` row (`rowCrossing`, a UP draw, `FLOAT2` at 20 under stride 16) is
  fetched after Task 4. Under strict it now expects `D3D_OK` and mtld3d's pixels; its "until C4c" comment goes.
- **Refusal sets, both scripts:** the buffers launch holds one line, `BUFFER_FETCH` (Task 4; reached by
  `buf-wide`); the shaders launch moves to the zero-refusal case (the VM gate's `crossing` kind goes, and the
  bench test's core cases follow). Header comments; `sh -n` on each script; the scripts' own tests under
  `vm/tests` run where they need no VM.
- **A skip argument:** `d3d9_triangle buffers [strict] [skip=<row>[,<row>…]]`: a listed row prints
  `d3d9 <row> skip` and is not run; d3d9-check counts a skipped row as absent unless its caller passes the same
  list (`D3D9_BUFFERS_SKIP`, default empty: no lane uses it but `jv0` and, under ruling 3, `jq`).
- **Bench:** two modes beside `ff|prog|tex|texdepth|lit`: `buf` (one static WRITEONLY vertex buffer of
  `benchDraws` RV triangles; each draw `DrawPrimitive(TRIANGLELIST, 3 * d, 1)`) and `dyn` (one
  DYNAMIC|WRITEONLY buffer of 256 triangles and a cursor that runs across frames: each draw locks the next
  triangle `NOOVERWRITE`, or the first with `DISCARD` at the wrap, writes it, unlocks, draws it). A failed
  `Lock`, `Unlock` or draw counts in `failed`. Before timing, as `lit` does, one frame is read back and its
  centre must be the triangle's colour, else `failed++` (a dropped buffer draw answers `D3D_OK`). Same output
  line, modes `buf` and `dyn`. Header comment.
- **Scripts written now:** `s2/gates/head.sh`, `ctl.sh`, `ja.sh`, `jcr.sh`, `jb.sh`, `jv0.sh`.
- **Controls**, before any DXMTRS code, at the base + this commit:
  - `jb`: the base built plain: its translator key and imports list written to `s2/gates/base-key.txt` and
    `s2/gates/base-imports.txt` (the wanted values of `jr` and `post`).
  - `ja` (Mac): default and msl green on every row. Core, **red by design, exactly:** `buf-odd`, `buf-cross`,
    `buf-cross-up`, `buf-cross-multi`, `buf-cross-odd`, `buf-cross-indexed` (six: `buf-wide` is green by
    accident, the old refusal answers `D3DERR_NOTAVAILABLE` too); `buffers-runtime` (three old lines against a
    one-line set); `shaders-crossing` and `shaders-runtime`. Everything else as C4c-1.
  - `jcr` (VM default, old WoW64): green; the buffers launch's duration recorded (budget 180 s, kill 240 s).
  - `jv0` (Mac, **default** runtime, not strict, under Metal's validation layer, the buffers launch, written
    for the default runtime rather than copied from `jq.sh`'s core environment): run (i) with every row, (ii)
    with `skip=buf-odd,buf-cross-odd`. It proves the layer is on (the layer's own "Metal API Validation
    Enabled" line in the launch's output; if that line cannot be captured, a positive control is found before
    `jq` counts anything), proves mtld3d ran (no `d3d9 core: device on dxmt-core` line), prints rows ok / not
    ok, and prints every report line with the row lines around it. (i) against (ii) answers whether a shifted
    attribute draws a report; ruling 3 is applied from that, in the ledger, before Task 4.
- Commit: "dxmt, vm: d3d9_triangle rows for crossing attributes and odd stream offsets, the shaders crossing
  row drawn under core, a row skip, buffer bench modes (DXMTRS C4c-2a)".

### Task 2: the index-format helper (mtld3d-core, front end)

- `src/d3d9/core/src/convert.rs`: `pub const fn index_format(format: u32) -> Option<(IndexType, u8)>`
  (`D3DFMT_INDEX16 → (UInt16, 2)`, `D3DFMT_INDEX32 → (UInt32, 4)`, else `None`; the size as `u8` so that every
  site widens with `From`), with a unit test in `convert/tests.rs`.
- The sites call it (map §5): `device.rs` `bound_index_fan`, `snapshot_bound_index_source`, the mtld3d
  `DrawIndexedPrimitiveUP`, the create-time check (`device.rs:7739`, `.is_none()`), and `core_draw.rs`'s
  `index_kind` (deleted; its three callers take `index_format`). The three mtld3d sites keep their own log line
  for the `None` arm; the core sites have none today and gain none.
- No derive changes (`tools/derive_inventory.txt` untouched). Lane: none (no row moves; rust-check's tests, then
  Task 5's conformance and rows).
- Commit: "d3d9: one table for the index formats".

### Task 3: the key's shift and the fetch in the pipeline source (mtld3d-core; d3d9-rt and front end mechanical)

Files: `src/d3d9/core/src/pipeline_key.rs` and `streams.rs` (+ tests); every constructor or match of
`StreamKey::Bound` and every reader of `PipelineSource::layouts`, `crossing`, `crosses_stride` in `src/d3d9`
(review-2 §1 lists them; `/usr/bin/grep -rn` again before editing).

- `StreamKey::Bound { stride, step, rate, shift }`: `shift: u8` is `offset_shift` of the stream's offset (0-3).
  Same derives. In this commit every constructor passes `shift: 0` (the front end's `bound_streams` too): the
  offset refusal still stands in d3d9-rt, so no draw is shifted before it can be fetched.
- `PipelineSource`:
  - `attrs` and `layouts` are **per Metal slot** (doc changed): the remapped ones when a fetch is built.
  - new `streams: [StreamLayout; MAX_STREAMS]`: the layouts per D3D9 stream, before any remap.
  - new `bindings: [(u8, u32); MAX_STREAMS]`: for slot `n`, the stream it binds and the byte advance; the
    identity `(n, 0)` without a fetch.
  - new `fetch: Result<(), VertexFetchError>`; `fetchable()` returns `fetch.is_ok()`.
  - `crossing` and `crosses_stride()` stay in this commit.
- `pipeline_source`: after the per-stream layouts and `crossing`, `shifts` = the `shift` of each `Bound` entry
  among `resolved.used_streams`, two bits a stream (`stream_shifts`' packing). If `crossing != 0 || shifts != 0`:
  `CrossingFetch::new(&resolved.attrs, &layouts, shifts)`; on `Ok`, `attrs`, `layouts` and `bindings` come from
  the fetch (a new accessor `CrossingFetch::bindings()` in `streams.rs`, with a test); on `Err`, `fetch` holds
  it and the other fields keep the unmapped values.
- **d3d9-rt keeps refusing in this commit:** `pipeline()` and `insert_pipeline` refuse and build nothing on
  `crossing != 0 || !fetchable()` (still `DrawRefused::Crossing`), so the two tests that expect a crossing draw
  refused with no pipeline (`rt/src/device/draw/tests.rs`, "a crossing draw is refused", "a crossing on stream
  1 is refused") pass unchanged. `source.layouts` → `source.streams` where the code means "per stream" (the
  `used` mask, the zero feed's stride, the unbound-stream warning). No behaviour moves.
- Tests (mtld3d-core): a crossing key's source has the attribute at a spare slot, offset 0, the stream's layout
  there and the binding `(stream, offset)`; a shifted key's attributes gain the shift, on stream 0 and on
  stream 1 alone; shift and crossing together; an attribute wider than its stride is `Err`; a key with no
  crossing and no shift is unchanged (the parity test `layouts_are_mtld3ds_for_every_feed` passes over
  `streams`); two keys that differ only by `shift` are different keys with different sources.
- Lane: none. Commit: "d3d9: a stream's offset shift in the pipeline key, the crossing fetch in the pipeline
  source".

### Task 4: the binds and the front end (d3d9-rt, front end)

Files: `src/d3d9/rt/src/device/draw.rs` (+ `draw/tests.rs`, `buffers/tests.rs`), `src/d3d9/core/src/
pipeline_key.rs` (the leftovers), `src/d3d9/src/device/core_draw.rs`, `src/dxmt/fakes` only if a capture line is
missing.

- `DrawRefused::Crossing` and `DrawRefused::Offset` go; `DrawRefused::Fetch` ("a layout no binding can fetch")
  comes. `pipeline()` returns `Err(Fetch)` for a source with `fetch.is_err()`; such a key builds no pipeline,
  keeps handle 0, logs no "does not build", and logs the `VertexFetchError` once a key at debug, in words that
  none of the gates' patterns match (`is not implemented under runtime.mode = core`, `does not build`,
  `has no core form`). `PipelineSource::crossing` and `crosses_stride()` are deleted if nothing reads them.
- `draw`: `used` (streams) from `source.streams`: the reads, the instancing rule and the zero feed keep
  meaning streams. Before `open_pass`, for each slot `n` whose `source.layouts[n]` is used, with
  `(stream, advance) = source.bindings[n]` and `data = draw.streams[stream]`:
  - `StreamData::Buffer { id, offset }`: the store lookup (error `Buffer`, as today); when `advance != 0`,
    `advanced_binding_offset(offset, advance, buffer.length())` must be `Ok`, else `Err(Fetch)` (the length is
    the padded backing's, as mtld3d's check has it); the slot binds the buffer at
    `slot_binding_offset(offset, advance)`. A `debug_assert!` that the key's `shift` of that stream is
    `offset_shift(offset)`.
  - `StreamData::Bytes(fed)`: the slot's bytes are `slot_bytes(fed, advance)`: `fed` whole for advance 0, else
    `fed.get(advance..)` (empty when past the end) zero-filled to `fed.len()`. **No refusal:** mtld3d always
    draws an inline crossing layout (its payload is padded to `inline_vertex_span`, R1 I1, R2 I1). `slot_bytes`
    is a pure function with a test by value (the capture prints a byte bind's length only, R2 I2).
  - `StreamData::Zeros`: as today, `debug_assert!(advance == 0)`.
- The offset refusal block is deleted.
- Front end (`core_draw.rs`): `UP_FETCH = "IDirect3DDevice9::Draw*PrimitiveUP (a layout no binding can fetch)"`
  and `BUFFER_FETCH = "IDirect3DDevice9::Draw*Primitive (a layout no binding can fetch)"` replace `CROSSING`,
  `BUFFER_CROSSING`, `OFFSET`; `record` maps `DrawRefused::Fetch` to the arm's string and
  `D3DERR_NOTAVAILABLE`; `bound_streams` fills `shift`. The UP arms need nothing else (mtld3d's only other UP
  answer is the over-4-GiB `INVALIDCALL`, R3 I5). Docs C4c-1 wrote that this makes untrue are corrected here.
- Tests (d3d9-rt): the two "refused" tests are rewritten to the new behaviour, with `buffers/tests.rs`'s
  odd-offset test: a crossing buffer draw prints two `layout` lines of the same stride, the attribute at the
  spare slot with offset 0, and two vertex-buffer binds of one buffer whose offsets differ by the attribute's
  offset; a shifted draw binds at the multiple of four below with the attribute offsets moved; a shifted
  stream 1 with stream 0 aligned moves only stream 1's attributes; an inline crossing draw binds two byte
  ranges of equal length; an advanced binding past the buffer is `Err(Fetch)` with no render pass in the
  drained calls; a wider-than-stride key is `Err(Fetch)` at every draw and builds nothing.
- Lane `jt4` (default, core): default ok; core ok: every row green strict, `buffers-runtime` with its one line,
  `shaders-runtime` with none.
- Commit: "d3d9: crossing attributes and odd stream offsets on the core runtime".

### Task 5: gates

Scripts written now (after `jb`): `jt.sh`, `jq.sh`, `jm.sh`, `jr.sh`, `jc.sh`, `jp.sh`, `jp0.sh`, `post.sh`.

- `jq`: d3d9-check on default, msl, core; stateblock under default and core; ten core launches under Metal's
  validation layer: the layer proven on as in `jv0`, 0 reports, rows ok (buffers: 35), subject to ruling 3.
- `jm`: core launches (the PE DLLs swapped, as C4c-1's); mutants as commits off the tip (`gates/mutants.txt`),
  each red on its row and on nothing outside the buffers launch:
  a. the advance dropped from a crossing slot's buffer binding → `buf-cross`;
  b. `shifts` forced 0 in `pipeline_source` → `buf-odd`;
  c. every stream takes stream 0's shift in `pipeline_source` → `buf-odd` (its stream-1 frame);
  d. the inline slot binds `fed` whole → `buf-cross-up`;
  e. a slot reads `draw.streams[n]` where it must read `draw.streams[bindings[n].0]` → `buf-cross-multi`.
  The advanced-offset check and `index_format` have no row mutant: a unit test each (ruling 4; a swapped index
  table also turns three launches red, which `jm`'s verdict does not model).
- `jr`: plain build: translator key = `base-key.txt`, imports = `base-imports.txt`, d3d9-check default, the Mac
  Wine conformance with no new site, the x64 check (the minimised-presents row fails while the Mac is in use:
  one rerun by the post-push check, not a loop).
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64. No D3D10-12 gate (ruling 6; `git diff --stat
  <base>..<tip>` before the push).
- `jp` / `jp0`: the probe on the tip and on the base (the rebased DXMTRS main, with this MacNeutron tip for the
  bench modes): `--bench 2000 <0|600>` in modes `tex`, `buf`, `dyn`, on mtld3d and core, each once, each under
  a kill timer; proof per run: the runtime line, 0 refusals, 0 "has no core form" drops, `failed 0`. →
  `build/c4c/s2/probe.md`. The base draws both modes (C4c-1), at a volume its rows do not reach (2000
  lock-draw cycles a frame): a `dyn` run that fails or is killed at the base is a finding, written down, not a
  blocker.

### Task 6: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s2/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-2a" and the
slice list amended (2 → 2a, 2b). Plan execution notes. `dxmt/pins`. Privacy scan of both ranges. Fetch,
rebase if a main moved (then rust-check, one lane, the plain build). Push DXMTRS then MacNeutron. Post-push
check. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** none. **A lint
  suppression:** none.
- **A derive:** none added or removed (`StreamKey` gains a `u8` field under its derives; `VertexFetchError`'s
  `Debug, PartialEq, Eq` are what a `PipelineSource` field needs); `tools/derive_inventory.txt` does not change.
- **New pub items** (`index_format`, `CrossingFetch::bindings`, the `PipelineSource` fields, `fetchable`,
  `DrawRefused::Fetch`): each with its doc.
- **A duplicate:** none added; one removed (the index-format table). The crossing rule is called, not copied;
  `slot_bytes` is the one formula of the core's own.
- **Not silent:** the one refusal left is logged by name and held by the buffers launch's set; the
  `VertexFetchError` is logged once a key.
- **Refusal literals:** three go (`CROSSING`, `BUFFER_CROSSING`, `OFFSET`), two come (`UP_FETCH`,
  `BUFFER_FETCH`).
- **Divergence kept on purpose:** ruling 2, written in the spec paragraph (`docs/d3d9/STATUS.md` has no line on
  mtld3d's widening today and takes the core's divergences when core ships).

## Rulings (planning)

1. **The shift is in `StreamKey::Bound`.** Why: it is a property of one stream's binding. Cost if wrong: a
   stream bound at an odd offset that the vertex stage does not read splits the key (a duplicate pipeline).
2. **A layout no binding can fetch is refused by name under core**, where mtld3d logs, widens the layout and
   draws wrong data. Reachable: an attribute wider than its stride; a bound buffer whose stream offset is
   within one attribute offset of its padded end; a declaration of more than 16 attributes that crosses or is
   shifted. (An unaligned advance and a stream past the table cannot happen: declaration offsets and stream
   numbers are checked at creation.) Why: what the core cannot draw right is said and counted. Cost if wrong:
   such a title shows wrong geometry on mtld3d and none on core.
3. **The validation layer and shifted attributes.** `jv0` decides. If mtld3d's shifted cells draw no report:
   the gate is 0 reports in all ten core launches. If they do: `jq`'s validated buffers launch runs with
   `skip=buf-odd,buf-cross-odd`, the report is quoted in the spec paragraph, and the two rows are still
   asserted in every unvalidated lane on the Mac and in the VM. Why: mtld3d ships that descriptor today. Cost
   if wrong: a report that marks a real fault is waved through for those cells.
4. **No row for an advanced binding outside its buffer, nor for `index_format`.** A unit test each.
5. **`buf-wide` asserts only the answer on mtld3d** (`D3D_OK`), no pixel, and has no UP cell (mtld3d's widened
   UP draw reads undefined memory): `UP_FETCH`'s wording is pinned by no launch.
6. **No D3D10-12 gate.** As C4c-1's ruling 6, with the diff stat as proof.

## Stated gaps

- No row: a crossing attribute on a per-instance stream; three or more crossing attributes; points and lines
  over a crossing layout; a declaration of more than 16 attributes (the core has no cap: it is refused only
  when it crosses or is shifted, where mtld3d rejects it at its wire).
- A refused draw's buffers were already flushed by the front end (an upload for nothing).
- The bench's `dyn` mode on core is the staged model's worst case until C4c-2b.
- Intel and AMD GPUs: mtld3d's own open question.

## Appendix: the rows

All `buffers` rows on the `bufStart()` device (no depth), blue clear, colours and probe points distinct within
a frame. **Every cell writes the bytes a wrong offset, slot or tail would read with a non-zero wrong value**:
buffers are filled past the last vertex with a marked tail (as C4c-1's `f[13]`), never left zero, and at least
one cell of each crossing row colours from a byte **past the stride** (a vertex-shader variant of `VS_CROSS`
that writes `v1.y`, the bytes 12-16 of a stride-12 vertex, to blue).

1. **`buf-odd`**: frame 1: the RV left quad from a buffer at offset 0 (red), then with the same declaration
   the right quad from a buffer whose vertices start at byte 2, bound at offset 2 (green): both visible.
   Frame 2: offsets 1 and 3. Frame 3: offset 6 with a start vertex of 1. Frame 4: `DrawIndexedPrimitive` at
   offset 2 with a base vertex. Frame 5: two streams (position on stream 0 at offset 0, a `D3DCOLOR` on stream
   1 at offset 2): only stream 1 shifts.
2. **`buf-cross`**: the stride-12 layout (position `FLOAT3` at 0, `TEXCOORD0 FLOAT2` at 8) through
   `DrawPrimitive`: C4c-1's cell (0xff8000), a cell with the `v1.y` variant, and a cell under `SetFVF` with a
   short stride (the fixed-function vertex stage reaches the same fetch) if mtld3d draws it as constructed,
   else the reason in the notes.
3. **`buf-cross-up`**: the same floats through `DrawPrimitiveUP` (stride 12) and `DrawIndexedPrimitiveUP`;
   the `v1.y` cell's last vertex reads the zero tail on both runtimes (asserted as such).
4. **`buf-cross-multi`**: stream 0 at stride 12 with one attribute that fits and two that cross, stream 1
   bound and read (a `D3DCOLOR`), stream 2 unbound: a spare slot's number is not its stream's. A second cell
   with a stream none of whose attributes fits (stride 8, one `FLOAT2` at offset 4).
5. **`buf-cross-odd`**: the stride-12 layout at stream offset 2.
6. **`buf-cross-indexed`**: `DrawIndexedPrimitive` with a base vertex over the stride-12 layout; a fan over it.
7. **`buf-wide`**: stride 8 under a `FLOAT3` position, the buffer holding count × 12 bytes: under strict
   `D3DERR_NOTAVAILABLE` and the clear colour; elsewhere `D3D_OK` and no pixel asserted.

`shaders`: `crossing` (`rowCrossing`) expects `D3D_OK` and mtld3d's pixels under strict.

## Review dispositions

- **R1** (design; 3 Critical, 3 Important, 9 Minor): C1 → Task 1's `shaders` row, both scripts and the bench
  test; C2 → Task 3 keeps refusing on `crossing != 0 || !fetchable()`; C3 → mutant (e) is the d3d9-rt one;
  I1 → no inline refusal, `slot_bytes`; I2 → `buf-odd`'s frame 5 and mutant (c); I3 → the `v1.y` variant and
  written tails. Minors → six rows red, not seven; Task 4 fills `shift`; "no render pass" for "empty capture";
  `crossing` deleted if unread; the FVF cell; ruling 2's reachable list; no UP cell in `buf-wide`; the one
  formula of the core's own named; the `debug_assert` on the shift.
- **R2** (core; 2 Critical, 3 Important, 7 Minor): C1, C2 → as R1's C2, C3; I1 → as R1's I1, and no `Err`
  after `open_pass`; I2 → `slot_bytes` tested by value; I3 → the skip argument, `jv0` written for the default
  runtime, run twice, the layer proven on. Minors → the size as `u8`; ruling 2's list; the 16-attribute gap;
  the mechanism's wording; one Task 4; "before anything is recorded" scoped to d3d9-rt; the drained-calls
  assertion.
- **R3** (exec; 4 Critical, 7 Important, 10 Minor): C1 → as R1's C1, with `vm/tests`; C2, C3 → as above;
  C4 → as R2's I3; I1 → the red-by-design list; I2 → no `index_format` row mutant; I3 → the scripts each task
  writes, `s2/gates`, never an s1 script unedited; I4 → `jb` and the wanted values from the base; I5 → Tasks
  4 and 5 are one; I6 → mutants (b) and (c) differ; I7 → the bench's read-back, `failed`, the cursor, the kill
  timer, the drop line in the proof. Minors → `vm/tests/d3d9.sh`; one refusal line; `buf-wide`'s buffer and
  the tails; the duration in `jcr`; the fifth index site and the log lines; the inventory unchanged; 35 rows
  ok; the base named at the rebase; the debug log's words; `dyn`'s volume at the base.
