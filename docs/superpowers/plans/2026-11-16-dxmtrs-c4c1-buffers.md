# DXMTRS C4c-1: vertex and index buffers, and the draws that read them, on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, `IDirect3DVertexBuffer9` and `IDirect3DIndexBuffer9` lock and unlock,
`DrawPrimitive` and `DrawIndexedPrimitive` draw from the bound streams (several streams, stream offsets, start
vertex, base vertex, 16- and 32-bit indices, fans, instancing, a declared stream with nothing bound), and
`GetDirect3D` answers. Same pixels and answers as under mtld3d, which keeps its behaviour.

**Architecture:** first slice of C4c (resources and sync; the C4 plan's line and `map2-sync-worklist.md` §3 S5).
C4c's slices: **1 buffers and buffer draws** (this), 2 dynamic buffers direct (`DynamicBuffer`, `plan_lock`
renames), the crossing fetch, backing release and the retained-bytes gauge, 3 read-back in all its forms, 4
`StretchRect`'s other shapes with `ColorFill` and `UpdateSurface`, 5 queries, 6 `Reset` and the getters, 7 gamma,
frame latency, the device-lost latch, device info and pages from winemetal, and the conformance lane opened to
`visual`, `device` and `d3d9ex` (C4c's gate). The sweep (`build/c4b3d/s5/sweep.md`) puts this slice first: a
refused `IDirect3DVertexBuffer9::Lock` ends `device`, and a refused `GetDirect3D` ends `d3d9ex`.

The model, one sentence: **under core every buffer is "staged"**, mtld3d's own model for its MANAGED, SYSTEMMEM
and static default-pool buffers. The front end's CPU backing is the truth and is never released; `Lock` and
`Unlock` are bookkeeping on it (range checks, the dirty range, the lock count); the GPU side is one dxmt-core
`Buffer` a D3D9 buffer, made on first use; before a draw, each buffer the draw reads uploads its dirty span, the
bytes copied into the queue's staging ring at record time and blitted in API order (work list §0.3 and §1.7
"Static VB/IB"; `CoreDevice::texture_upload` is the twin). Seven DXMTRS commits, each prefix pushable:

1. **`GetDirect3D`** (Task 2). The refusal has no reason (M §D; R1 traced the parent's reference through the core
   teardown: retained at creation, released by `release_parent`); its guard goes.
2. **Fakes** (Task 3). `dxmt-fakes` prints a pipeline's vertex descriptor and a buffer-to-buffer blit.
3. **The key's streams** (Task 4). `PipelineKey::stride` becomes one entry a stream; `pipeline_source` derives
   mtld3d's layouts (per-vertex, per-instance, constant) and places each attribute at Metal slot = its stream
   (mtld3d's numbering: streams at 0-15, the vertex uniforms at 25-30). UP draws move onto it: stream 0 inline, a
   declared stream above 0 fed zeros through a constant layout, as mtld3d feeds it (ruling 3).
4. **A rename** (Task 5a). d3d9-rt's `draw_up` is `draw`, alone in its commit (CONTRIBUTING).
5. **Buffers in d3d9-rt** (Task 5). A buffer store by `BufferId`, the upload, and the draw's new inputs: per
   stream bytes, zeros or a stored buffer at an offset; indices as bytes or a stored buffer; the first vertex,
   the base vertex, the instance count. Nothing reaches it from the front end yet.
6. **Lock and Unlock** (Task 6). Buffers of a core device are made staged; the two refusals of each buffer type go.
7. **The draws** (Task 7). `DrawPrimitive` and `DrawIndexedPrimitive` take a core arm after their shared checks.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-fakes, mtld3d-core, d3d9-rt, the d3d9.dll front end), C++
(`d3d9_triangle`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4). Program plan:
`docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4c's line). Maps: `build/c4c/map.md` (cited as
M §n), `build/c4/map2-sync-worklist.md` (W §n), `build/c4/map2-d3d11-template.md`, `build/c4/map2-deptrack.md`.
Reviews: `build/c4c/s1/review-{1-design,2-core,3-exec}.md` (folded; dispositions at the end, cited as R1-R3).
Ledger: `~/Documents/MacProton/.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: the plain d3d9.dll imports unchanged and the translator key
  equal to that of the DXMTRS main this slice sits on
  (`3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` at 82a588ba); every core-only front-end
  branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check; a core-only struct field
  or private helper is cfg'd in the struct, in every literal and at its definition; an arm added to an
  always-compiled function is one cfg'd block with its `use`s inside it. A front-end helper lands in the commit
  of its first caller (the front end is a staticlib: an uncalled private item fails rust-check). mtld3d's rules
  are called, not restated: `layout_stride`, `bound_stream_layout`, `stream_step`, `instance_count`,
  `is_instance_data`, `offset_shift`, `FanRewrite`, `d3d_to_metal_primitive`, `vertex_count`,
  `records_dirty_range`, `classify_map_mode`, `DirtyRange`. Where a rule sits inside a closure of mtld3d's
  (`encoder_draw/draw_record.rs`'s `stream_layouts_view`, the one mtld3d's draws run), it is lifted into a named
  function of mtld3d-core that both callers use, with no change of mtld3d's result (Task 4 lists them).
- **No dxmt-core, d3d11 or winemetal change**, `src/dxmt/src/test_support.rs` included. `direct::bind_buffer`,
  `Buffer`, `Queue::allocate_staging_buffer` and the buffer-to-buffer blit record exist and are `pub` (R2).
  `src/dxmt/fakes` (test support; d3d9-rt's tests alone link it) gains printed lines. Should a task find that
  dxmt-core must change, it stops: the change is a ruling and a commit of its own with its unit test before the
  task's, and the x64 `dxmt/check.sh` and the VM's D3D10-12 gate join Task 8.
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code;
  a commit's body follows CONTRIBUTING §Changes. A d3d9-rt or mtld3d-core API change carries its front-end call
  sites and every literal of a changed struct in the same commit (the compiler lists them). The PE front end
  compiles only inside rust-check: grep the import list of an edited front-end file first. A new or removed
  Clone/Copy derive needs `sh tools/audit.sh --update-derives` before rust-check (Tasks 4 and 5). If rust-check
  or a job's build stops at "fetching DXMT's Rust crates", `cargo fetch --locked` in both worktrees, then rerun
  under a new job name.
- No new dependency. No `HashMap` (the store is an `FxHashMap`, as the textures'). No `pub(crate)`. Doc blocks:
  a one-line title, a blank doc line, the body; lines of 100 columns at most; `#[must_use]` on a new
  value-returning pub fn, `const fn` where it can be. A struct holds at most one `bool`, a function at most three.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4c`, base DXMTRS main 82a588ba) and `build/c4b3c/mn`
  (branch `c4c-mn`, base MacNeutron main 342934a9), cargo target `build/c4b3c/cargo`; gate scripts in
  `build/c4c/s1/gates` (copied from 3d5's, state names `c4c1-*`), logs and reviews in `build/c4c/s1`. Builds and
  GPU jobs as `caffeinate -i /usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock sh <job>.sh > <job>.log
  2>&1`, spelled out (zsh does not split a command held in a variable). No make in a worktree. A job tests
  **committed** tips (`DXTIP`/`MNTIP`); a job script that has run is never edited; each run takes its own folder.
  After a restart of the Mac the builder VM is started by hand with the two image shares
  (`build/vm-work/images`, `build/p/eac-vm-work/images`).
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; crate tests as `tools/rust-check.sh`'s test leg
  runs them; mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/` (the row program's syntax check:
  `-std=c++17 -fsyntax-only -Wall -Wextra`, for x86_64, i686 and aarch64).
- **Another session** (the C/C++ port: DXMTRS branch `c-rust`, MacNeutron `c-rust-mn`) shares the lock, the main
  checkout (left detached while its job runs: a job here exits 90 or 91 then, and is rerun) and both mains. Its
  branches change `src/d3d9/shim`, `src/d3d9/src/arm64_crt.rs`, `tools/rust-check.sh`, `tools/audit.sh`,
  `Cargo.lock`, `src/winemetal/sys`, and replace `airconv_thunks.c` with `sm50.rs`, which moves the translator
  key; none of this slice's files today. Before the pin: fetch both origins. After a rebase onto a moved DXMTRS
  main: `cargo fetch --locked`, rust-check on the rebased tip, one lane and the plain build (the key then equals
  the new main's, the imports unchanged), and the pin names the rebased commit.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x3c0-0x3ff** here, one a test (`/usr/bin/grep -rnoE '0x3[c-f][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src`
  finds none today).
- Every DXMTRS commit is safe to run under core: until Task 7 no draw reads a buffer, and until Task 6 no buffer
  locks. MacNeutron's core lanes are red by design, from Task 1's rows:
  - until Task 6: `state-lock` (strict asks `D3D_OK`) and `state-runtime` (one refusal line, the buffer lock,
    against a set of none);
  - until Task 7: every row of the `buffers` launch but `buf-d3d` (green from Task 2), `buf-lock` and
    `buf-refused` (green from Task 6: the refused draw is its strict answer), and `buffers-runtime` (other
    refusal lines than its set). `buf-null-up` turns green at Task 4.
  Should the slice stop before Task 7, the `buffers` kind leaves the three scripts' lists in the commit that
  pins; stopped before Task 6, `state-lock` and the `state` launch's one-line set go back too.
- Per task one lane (the Mac's `d3d9-check` on default and core); full gates once, in Task 8. Benches and probes
  run once, on an idle Mac. No gate, build or VM job while the maintainer is gaming. Autonomous work stops at 95%
  weekly usage.

## Scope

In: `IDirect3DVertexBuffer9::Lock`/`Unlock` and `IDirect3DIndexBuffer9::Lock`/`Unlock` for every pool and usage
a buffer is created with today; `DrawPrimitive` and `DrawIndexedPrimitive` for every primitive type mtld3d draws
(points, lines, line strips, triangle lists and strips, fans), with `StartVertex`, `BaseVertexIndex` (signed),
`StartIndex`, 16- and 32-bit indices, stream offsets that are multiples of four, several streams, a stride of
zero, `SetStreamSourceFreq` instancing on indexed draws, a declared stream with nothing bound (buffer and UP
draws), a buffer drawn while locked; `GetDirect3D`.

Out (owners): a DYNAMIC default-pool buffer mapped directly and renamed on `DISCARD` (it is correct here, and
slow: each lock uploads the whole buffer, in a blit that splits the render pass), an attribute that ends past
its stream's stride and a consumed stream's offset off four bytes (`CrossingFetch`; both stay refusals by name,
for UP and buffer draws), the WRITEONLY backing release, the retained-bytes gauge and the i686 cap (C4c-2); a
buffer of another device (refused by name: ruling 9); `ProcessVertices`, patches,
`SetSoftwareVertexProcessing` (later slices; mtld3d refuses patches too); per-draw dedup of binds and a hoisted
upload (C4d); the conformance lane for the three suites (C4c-7).

## Review Focus

1. **Parity of the layouts.** For every stream the vertex stage consumes: is the layout `pipeline_source` derives
   exactly the one `stream_layouts_view` gives mtld3d for the same binding (stride, zero stride with any
   frequency word, the three kinds of word, inline, nothing bound)? Is every resolved attribute provided, as
   mtld3d provides it?
2. **Order.** Is a buffer's upload always before the draw that reads it and after every earlier draw that read
   the older bytes, by fences, including: a draw while locked, two draws around an overwrite, an upload that
   commits the chunk, the index buffer, a read many passes back? Can the flush close a pass or commit after the
   draw's key and targets were resolved?
3. **Lifetime.** A buffer released while a recorded draw names it; a device torn down with buffers alive; the
   store's entry of a released buffer; a buffer of another device (refused before its bytes are touched).
4. **The mtld3d body under core.** Does any line `vb_lock`, `ib_lock`, the unlocks, the constructors or the code
   the new arms call reach mtld3d's recorder (`Mtld3dOnly`, `current_frame`), the retention queue,
   `coherent_seq_arc` or a unix call on a core device? (`bound_index_fan` and `generated_fan_source` do: the
   arms call neither.)
5. **Metal's rules.** A slot the pipeline reads and the draw does not bind; a nil buffer; offsets; the validation
   layer's verdict in the lanes.
6. **Rows.** Does each row discriminate what its mutant breaks, on pixels the default runtime produced first?
7. **Execution.** Each commit's lane expectation; the script edits; the red-by-design list; the probe.

## Tasks

### Task 1: rows (MacNeutron) and the gate scripts

- [ ] **Step 1: the `buffers` kind** in `dxmt/tests/d3d9_triangle.cpp` (appendix: 27 rows `buf-*`; a
  `buffersRows(IDirect3DDevice9 *, bool strict)` table and a branch in `main`, as `msaaRows`; the plain device of
  the `state` kind: no auto depth-stencil). Helpers reused: `row`, `CK`, `Com`, `scene` (`:2477`), `until`
  (`:2484`), `rtHas` (`:4496`), the read path through `GetRenderTargetData`. A prologue `bufStart()` runs before
  every row, after `reset()`: `reset()` restores no stream, frequency or index state, so it unbinds streams 0
  and 1, sets both frequency words to 1 and `SetIndices(NULL)`; each row sets its own declaration or FVF.
  In-program budget 180 s.
- [ ] **Step 2: changed rows.** `state-lock` (`rowLock`): strict asks `D3D_OK` for the `Lock` and the `Unlock`.
  The stale comment at `:1099` ("the core refuses texture locks until C4b-3b") goes.
- [ ] **Step 3: scripts.**
  - `dxmt/d3d9-check.sh`: `launch ours fresh buffers 240 buffers $strict` after `check msaa` (the kill is 60 s
    past the budget, as every 180 s kind); `check buffers "<the 27 names>"`. `state` moves to the arm of
    `refusals_ok` that asks for exactly zero refusal lines (`state_refusals` goes, with its arm).
    `buffers_refusals` is one single-quoted value of three lines with no trailing newline:
    "IDirect3DDevice9::Draw*Primitive (an attribute crossing the stride)", "IDirect3DDevice9::Draw*Primitive (a
    stream offset off four bytes)", "IDirect3DDevice9::Draw*Primitive (a buffer of another device)". The
    `unpresented` pattern gains `buffer has no core form` (no apostrophe: the pattern is single-quoted). The
    header block, `refusals_ok`'s comment and the kinds list name the new kind and `state`'s empty set.
  - `vm/dxmt/d3d9-gate.sh`: the same constants and `unpresented` entry; `buffers` in the `runtime` helper's kind
    arm; `for r in bf64-fresh bf32-fresh; do check "$r runtime core" "$(runtime "$G/logs/$r" buffers)" ok; done`
    beside the existing loops; `st64-fresh` and `st32-fresh` move to the no-kind loop; the row loop over the 27
    names; the header.
  - `vm/tests/d3d9.sh`: `tri d3d9_triangle.exe bf64-fresh 240 buffers $strict` and the 32-bit twin; the header.
- [ ] **Step 4:** three mingw syntax checks; `sh -n` of the three scripts; commit.
- [ ] **Step 5: control on the Mac** (`ja`: default and msl green on every new and changed row; core red exactly
  on the Global Constraints' list). A row red on the default runtime is a wrong expectation or a defect of
  mtld3d: the row is corrected to what mtld3d does (ledgered as parity), never the other way.
- [ ] **Step 6: control in the VM** (`jcr`: default, old WoW64, x64 and x86).

### Task 2: `GetDirect3D` (front end)

**Files:** `src/d3d9/src/device.rs` (`device_get_direct3d`).

- [ ] Remove the `refuse_under_core!` of `device_get_direct3d`; the mtld3d body answers. The commit body names
  the parent's path under core: `addref_parent_direct3d9` in `create_core_device`, `release_parent` in the core
  teardown arm (R1 read both: no leak to fix).
- [ ] rust-check; commit; lane `jt2` (core: `buf-d3d` green, the rest as the control).

### Task 3: dxmt-fakes

**Files:** `src/dxmt/fakes/src/lib.rs` (and its tests).

- [ ] `MTLDevice_newRenderPipelineStateWithVertexDescriptor`'s fake prints, after its present line, one line a
  used attribute (`  attr <index> format=<f> offset=<o> buffer=<slot>`) and one a used layout (`  layout <slot>
  stride=<s> step=<n> rate=<r>`), in index order. Every existing pipeline gains lines (attribute 0, layout 0);
  no expectation moves because no test indexes past the pipeline line (they find lines with `at`, `contains`,
  `starts_with`): rust-check's d3d9-rt tests are the proof.
- [ ] The blit decoder, beside the texture blits it decodes, prints `blit copy_to_buffer_from_buffer #<src>
  src_offset=<o> #<dst> dst_offset=<o> length=<copy_length>` for `WMT_BLIT_COMMAND_COPY_FROM_BUFFER_TO_BUFFER`.
- [ ] One test of each line. rust-check; commit. No lane (test support only).

### Task 4: the key's streams (mtld3d-core, d3d9-rt, front end)

**Files:** `src/d3d9/core/src/pipeline_key.rs` (+ `pipeline_key/tests.rs`), `src/d3d9/core/src/streams.rs`,
`src/d3d9/core/src/encoder_draw/draw_record.rs`, `src/d3d9/core/src/draw_data.rs`,
`src/d3d9/core/src/pipeline_state.rs`, `src/d3d9/rt/src/device/draw.rs` (+ tests, `fixture.rs`),
`src/d3d9/src/device/core_draw.rs`, `src/d3d9/core/examples/emit_corpus.rs` if it names the key.

- [ ] **mtld3d-core, lifted rules** (no change of any mtld3d result; mtld3d-core's tests and Task 8's plain build
  are the proof). In `streams.rs`:
  - `inline_stream_layout(stride, extent)` and `null_stream_layout(extent)`: the Inline and Null arms of
    `stream_layouts_view`'s closure (`draw_record.rs:942-955`), which now calls them; the uncalled copy
    `draw_data::stream_layouts` calls them too (it has no caller in the tree: noted for the maintainer, not
    removed here).
  - `stepped_stream_layout(app_stride, extent, step, rate)`: the whole of `bound_stream_layout` with the
    zero-stride arm first (a stride of 0 is `Constant` over the extent, whatever `step` and `rate`), then
    `StreamLayout { stride: layout_stride(app_stride, extent), step, step_rate: rate }`;
    `bound_stream_layout(s, e, f)` becomes `let (step, rate) = stream_step(f); stepped_stream_layout(s, e, step,
    rate)`.
  - `StreamLayout::crosses(extent)` (`pipeline_state.rs`): the comparison of `stream_layouts_with`, which now
    calls it.
- [ ] **`StreamKey`** (`pipeline_key.rs`; derives `Clone, Copy, Debug, Default, PartialEq, Eq, Hash`): `Unused`
  (default) | `Inline { stride }` | `Bound { stride, step, rate }` (the step and rate of `stream_step(freq)`: a
  frequency word's count is not in the key, or an instanced draw would build a pipeline a count) | `Null`.
  `PipelineKey::stride` → `streams: [StreamKey; MAX_STREAMS]`. The front end fills an entry for each stream of
  the bound declaration's `stream_mask()` (stream 0 alone without a declaration) and leaves the rest `Unused`.
- [ ] **`pipeline_source`**: every resolved attribute is kept and provided, at `slot = its stream`; for each
  stream with an attribute, the layout from the key's entry and the stream's extent (`resolved.extents`):
  `Inline` → `inline_stream_layout`, `Bound` → `stepped_stream_layout`, `Null` or `Unused` →
  `null_stream_layout`. `PipelineSource` gains a fourth element of `attrs` (the slot), `layouts: [StreamLayout;
  MAX_STREAMS]` (`UNUSED` for a stream without an attribute) and `crossing: u16` (bit n:
  `layouts[n].crosses(extent)`), and loses `stream0_extent` and `stride`; `crosses_stride()` is `crossing != 0`.
  The filter on buffer 0 goes, with its comment and the doc of `crosses_stride`; the test
  `elements_off_stream_0_are_not_provided` is inverted on purpose (they are provided, at their stream's slot).
- [ ] **d3d9-rt**: `build_pipeline` writes each attribute at its slot and each used layout with `step as u32`
  (mtld3d's `VertexStepFunction` has Metal's numbers: Constant 0, PerVertex 1, PerInstance 2; the wire has no
  constants for them) and its rate; `STEP_PER_VERTEX` and `slot::VERTICES` go. `pipeline` returns the entry (the
  draw needs its layouts beside the handle). `DrawData::vertices` → `streams: [StreamData; MAX_STREAMS]` with
  `StreamData::{None, Bytes(&[u8]), Zeros}` for now (derives `Clone, Copy`); the closure binds, for each slot the
  pipeline's layouts use, the stream's bytes (`direct::bind_bytes`) or, for `Zeros` and for a used slot whose
  data is `None` (a `debug_assert`), `layouts[slot].stride.min(4096)` bytes of mtld3d-core's
  `NULL_STREAM_ZEROS`, with a once-logged warning past 4096 as mtld3d's. `insert_pipeline`'s error line names
  the key another way and keeps the words "does not build" (the gates grep them).
- [ ] **Front end** (`core_draw.rs`): `resolve` takes the streams instead of a stride; a UP draw's are `Inline {
  stride }` at 0 and `Null` at each other declared stream; its data `Bytes` at 0 and `Zeros` there.
- [ ] **Tests.** rt, on the fakes' new lines and with the literal step numbers: a two-stream declaration under a
  UP draw builds a pipeline with stream 0's attributes at buffer 0 under `step=1` at the call's stride and
  stream 1's at buffer 1 under `step=0 rate=0` of the element's extent, and the draw binds zeros of that length
  at slot 1; a `Bound` entry with `PerInstance` rate 2 prints `step=2 rate=2`; a zero stride prints `step=0`,
  with a per-vertex and with a per-instance entry; two keys that differ in one stream's stride are two
  pipelines; crossing on stream 1 refuses as crossing on stream 0 does. mtld3d-core: `pipeline_source`'s layouts
  against `stream_layouts_view`'s over a `VertexView` of the same bindings, stream by stream, for inline, bound,
  zero stride (with `INDEXEDDATA`, `INSTANCEDATA | 2` and `INSTANCEDATA | 0` words), per-instance, instance
  rate 0 and nothing bound; and `bound_stream_layout` equal to its old body on those words (the lift's proof).
- [ ] `sh tools/audit.sh --update-derives`; rust-check; commit; lane `jt4` (default ok; core: the nine existing
  launches as before, `buf-null-up` green, the rest of `buffers` and `state` as the control).

### Task 5a: the rename (d3d9-rt)

- [ ] `CoreDevice::draw_up` → `draw`: its 81 call sites in `rt/src/device/{draw,depth,swapchain,target,
  recorder}/tests.rs`, `draw.rs` and the front end's `core_draw.rs`, and nothing else. rust-check; commit. No
  lane.

### Task 5: buffers in d3d9-rt

**Files:** `src/d3d9/rt/src/device.rs` (the store's field, the re-exports), new
`src/d3d9/rt/src/device/buffers.rs` and `buffers/tests.rs`, `src/d3d9/rt/src/device/draw.rs` (+ tests,
`fixture.rs`), `src/d3d9/src/device/core_draw.rs` (the changed literals).

- [ ] **The store**: `buffers: FxHashMap<BufferId, Arc<Buffer>>` on `CoreDevice`. `has_buffer(id)`;
  `forget_buffer(id)` (the entry goes; recorded draws hold the `Arc`); `buffer_create(id, length) -> bool`:
  `Buffer::new(length, ..)`, `allocate(BufferAllocationFlags::GPU_PRIVATE)` (exactly that flag: `GPU_READONLY`
  turns the dependency tracking off, `SUBALLOCATE_FROM_ONE_PAGE` and `CPU_PLACED` have no use here), `rename` on
  the API thread now (a `Buffer` never renamed has no current allocation and binds nil; `debug_assert` that
  there was none), false and no entry when the allocation's handle is 0. No zeroing: the caller's first upload
  carries every byte of `length`.
- [ ] **`buffer_upload(id, offset, bytes) -> bool`**: false, and nothing recorded, when the store lacks `id`,
  `bytes` is empty, the range ends past the buffer, or the staging block's mapping is null (logged, as
  `texture_upload`). Otherwise the bytes go into `allocate_staging_buffer(len, 16)` now, the open render pass
  ends (`open_blit_pass`), and a closure takes `access_buffer::<{ stage::COMPUTE }>(.., Access::WRITE)` and
  encodes `WmtcmdBlitCopyFromBufferToBuffer` (`copy_length`) at `allocation offset + offset`, or nothing when
  the access answers none; `staged_bytes` and the commit past `staged_budget` as `texture_upload`.
- [ ] **The draw**: `StreamData` gains `Buffer { id, offset }`; `Indices::bytes` → `data: IndexData::{Bytes(&[u8]),
  Buffer { id, offset }}` (`Clone, Copy`); `DrawData` gains `vertex_start: u32`, `base_vertex: i32` and
  `instancing: Option<(u32, u16)>` (stream 0's frequency word and the mask of bound streams whose word is
  instance data; none for a draw that never instances). The instance count is `streams::instance_count(word,
  mask & used != 0)` with `used` the streams the pipeline's layouts use: mtld3d's rule
  (`unix/src/draw.rs:877-886`), 1 without `instancing`. Before any record: a used stream's `Buffer` offset with
  `offset_shift(offset) != 0` is `DrawRefused::Offset` (mtld3d shifts used bound streams alone; an unconsumed
  stream at an odd offset draws); a `Buffer` whose id the store lacks is `DrawRefused::Buffer`. A stored buffer
  binds with `direct::bind_buffer` (READ tracked) at the stream's offset; an index buffer resolves with
  `access_buffer::<{ stage::VERTEX }>(.., Access::READ)` and draws at `allocation offset + offset`, the draw
  skipped when it answers none.
- [ ] **Tests** (fakes): an upload prints the blit with its offsets and length and ends an open render pass; an
  upload past the budget commits; a draw from a stored buffer prints `render 2 buffer=#.. offset=.. index=<slot>`
  per bound stream then `render 19` with `vertex_start`; an indexed one prints `render 20` with the stored
  buffer's handle, `index_buffer_offset`, `base_vertex` (a negative one too) and `instance_count`; instancing
  counts only when a used stream is in the mask (a masked stream the stage does not consume gives 1); an odd
  offset on a used stream refuses and on an unconsumed one draws; a forgotten buffer's recorded draw still
  encodes; an unknown id is `DrawRefused::Buffer`. **Order, on the fence lines the fakes print** (pass order
  alone proves nothing: allocations are untracked, only fence waits order the GPU): draw, upload, draw of one
  vertex buffer: the blit waits on the fence the first render pass's vertex half updates, and the second render
  pass's vertex wait names the blit's fence; the same for an index buffer; the same across an upload that
  commits on the budget (the wait sits in the next command buffer); and the far case, 33 render passes of other
  draws between the first draw and the upload.
- [ ] `sh tools/audit.sh --update-derives`; rust-check; commit; lane `jt5` (as `jt4`).

### Task 6: Lock and Unlock (front end)

**Files:** `src/d3d9/src/vertex_buffer.rs`, `src/d3d9/src/index_buffer.rs`, `src/d3d9/src/device.rs`
(`retire_released_buffer`).

- [ ] **Creation**: a buffer of a core device takes `BufferMapMode::Staged` whatever `classify_map_mode` says
  (one cfg'd `if` in each constructor, before `dirty`'s initialiser, so it starts full-dirty as every staged
  buffer). `classify_backing` is left alone (the class only picks a gauge counter): the backing is never
  released because the core `Unlock` never reaches `release_backing_after_upload`.
- [ ] **`Lock`**: the refusal goes. Under core, a lock of a buffer mtld3d would map directly
  (`classify_map_mode(usage, pool) == Direct`: default pool and DYNAMIC) records the whole buffer as dirty,
  whatever window it announces (ruling 1: mtld3d shows every byte written through such a pointer). The
  "D3DLOCK_DISCARD on a non-DYNAMIC (Staged) buffer" warning asks `classify_map_mode` instead of the forced
  mode. Walk the body line by line under core (Review Focus 4) and write the walk into the commit body.
- [ ] **`Unlock`**: the refusal goes; under core it ends after the lock count's bookkeeping (no transient page,
  no `push_stage_upload`, no `release_backing_after_upload`): the dirty range waits for the draw.
- [ ] **Release**: `retire_released_buffer`'s core arm calls `core.forget_buffer(buffer_id)`.
- [ ] rust-check; commit; lane `jt6` (core: `state-lock`, `buf-lock` and `buf-refused` green, `state-runtime`
  with zero refusals; the rest of `buffers` red).

### Task 7: the draws (front end)

**Files:** `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/device.rs` (`device_draw_primitive`,
`device_draw_indexed_primitive`), `src/d3d9/src/vertex_buffer.rs`, `src/d3d9/src/index_buffer.rs`
(`core_flush`).

- [ ] **`core_flush(&mut self, core: &mut CoreDevice) -> bool`** on each buffer's inner (cfg'd): a buffer the
  store lacks is created at the backing's padded length (`current_backing_len()`, mtld3d's device buffer's) and
  uploads the whole padded backing (zeroed past the logical length); otherwise its dirty span, if any, uploads.
  The dirty range is cleared only after an accepted upload and only when no lock is out (a locked buffer
  uploads again at the next draw, mtld3d's `flush_staged_if_mapped`). False when the create or the upload was
  refused: the draw is then dropped, logged once ("d3d9 core: a draw's buffer has no core form; the draw is
  dropped"), `D3D_OK`.
- [ ] **Placement**: each entry point's `refuse_under_core!` goes; after the checks both runtimes share (the
  missing layout source, a count of zero), one cfg'd block calls `core_draw::draw_primitive(dev, primitive,
  start_vertex, primitive_count)` or `core_draw::draw_indexed_primitive(dev, primitive, base_vertex,
  start_index, primitive_count)` and returns its answer on a core device. The arms read the bound buffers'
  state themselves (`bound_buffers()`, `declared_stream_mask`, the backings' pointer and padded length) and
  call none of `snapshot_bound_*`, `flush_mapped_bound_buffers`, `bound_index_fan`, `generated_fan_source`,
  `draw_bound_triangle_fan` (they stamp submit sequences or write into `current_frame`, which a core device
  does not have).
- [ ] **Answers, in mtld3d's order** (R1 I2; the commit body lists them against `device.rs:12758-12908` and
  `:12995-13152`):
  - `DrawPrimitive`: a fan first: the rewrite cannot address it → `D3DERR_INVALIDCALL`; no declared stream with
    a buffer → `D3DERR_INVALIDCALL`. Otherwise: an unknown primitive type → `INVALIDCALL`; a vertex count of
    zero → `INVALIDCALL`; no declared stream with a buffer → `INVALIDCALL` ("no vertex buffer bound").
  - `DrawIndexedPrimitive`, not a fan: an unknown primitive type → `INVALIDCALL`; a vertex count of zero →
    `INVALIDCALL`; no declared stream with a buffer → `D3D_OK`, nothing drawn, the index buffer not looked at;
    then no index buffer, or an index format that is neither → `INVALIDCALL`.
  - `DrawIndexedPrimitive`, a fan (before the primitive type): no index buffer → `INVALIDCALL`; the fan's
    indices from `start_index` end past the index backing's padded length → `INVALIDCALL`; the rewrite answers
    none → `INVALIDCALL`; then no declared stream with a buffer → `D3D_OK`.
- [ ] **Streams**: for each stream of `declared_stream_mask`: a bound buffer of this device → `core_flush`, key
  `Bound { stride, step, rate }` from `stream_step(freq)`, data `Buffer { id, offset }`; nothing bound → `Null`
  and `Zeros`. A bound vertex or index buffer whose `device_inner` is not the drawing device refuses the draw
  before any flush: "IDirect3DDevice9::Draw*Primitive (a buffer of another device)" (ruling 9). The flush of
  every buffer the draw reads, the index buffer included, runs before `resolve` reads the targets and builds
  the key: an upload ends the open pass and may commit.
- [ ] **Refusals from d3d9-rt**: `DrawRefused::Offset` → "IDirect3DDevice9::Draw*Primitive (a stream offset off
  four bytes)"; `DrawRefused::Crossing` → "IDirect3DDevice9::Draw*Primitive (an attribute crossing the stride)"
  (the UP arm keeps its own literal). The flush may have uploaded by then; no draw is recorded.
  `DrawRefused::Buffer` takes the dropped-draw log above.
- [ ] **Fans**: `DrawPrimitive` → `FanRewrite::sequential(start_vertex, primitive_count)` as index bytes, vertex
  start 0, base vertex 0. `DrawIndexedPrimitive` → `FanRewrite::indexed(bytes from start_index * size to the
  padded length, size, base_vertex, primitive_count)`, as `bound_index_fan` calls it: the rewrite folds the base
  into its indices, so the draw's base vertex is 0. Both draw a triangle list of index bytes.
- [ ] **Indexed, not a fan**: indices `Buffer { id, offset: start_index * size }`, base vertex as given.
- [ ] **Instancing** (mtld3d's `IndexView` match): `instancing` = stream 0's frequency word and the mask of
  bound declared streams with `is_instance_data`, for `DrawIndexedPrimitive` (its fans included) and for a
  `DrawPrimitive` fan that leaves mtld3d's shared pattern (more than `FAN_PATTERN_MAX_TRIANGLES` triangles, or a
  start vertex past `i32::MAX`: mtld3d draws it from a generated list, which instances); none for every other
  `DrawPrimitive`.
- [ ] rust-check; commit; lane `jt7` (default ok; core: every row of `buffers` green strict, `buffers-runtime`
  with its three refusal lines, all other launches as before).

### Task 8: gates, mutants, the probe

- [ ] **`jq`**: the three Mac lanes; the stateblock conformance under core equal to default; all ten core
  launches under Metal's validation layer with no report.
- [ ] **Mutants**: commits off the tip, on no branch (a temporary index and `git commit-tree`), listed in
  `gates/mutants.txt` in letter order; `jm.sh` is rewritten for this slice before its first run (it loops the
  letters present, runs the `buffers` launch under core, and reads each mutant's red rows). One line each, no
  unused item left in the PE build (`min`, a swap, `a || b`); each red on its row (the rows in brackets fall
  with it), none in another launch (no other row declares a stream above 0, and UP draws pass start 0, base 0,
  one instance and byte indices): (a) the draw's `vertex_start` is 0 → `buf-start` [`buf-dynamic`]; (b)
  `base_vertex` is 0 → `buf-base`; (c) `core_flush` skips a locked buffer → `buf-held`; (d) the upload goes to
  offset 0 → `buf-relock` [`buf-dynamic`]; (e) every attribute is placed at slot 0 → `buf-streams` [`buf-null`,
  `buf-null-up`, `buf-zero-stride`, `buf-instanced`]; (f) a `Null` stream's attributes are not provided (the old
  zero fill) → `buf-null` [`buf-null-up`]; (g) the instance count is 1 → `buf-instanced`; (h) the index offset
  ignores `start_index` → `buf-indexed` [`buf-index32`, `buf-managed`]; (i) a zero stride keys its word's step →
  `buf-zero-stride`; (j) a bound stream's offset is not added → `buf-offset` [`buf-streams`]; (k) the indexed
  fan's draw carries the base again → `buf-ifan`. One more at rt level, by `cargo test` alone: the upload's
  access is `READ` → the order tests.
- [ ] **Plain build** (`jr`): translator key and imports unchanged; `d3d9-check` default; the Mac's Wine
  conformance with no new failing site (mtld3d-core's lifted functions are on the default runtime's path); the
  x64 `dxmt/check.sh`. A lone untriaged `device.c:1945` (test_cursor's handle: 3d5's ruling T8-1) is rerun once
  at the same tip; twice is a finding.
- [ ] **VM** (`jc`): the D3D9 lanes on default and core, old and new WoW64. The D3D10-12 gate is not run
  (ruling 6).
- [ ] **Probe** (`jp`, then `jp0`, a copy with its own folder, at `DXTIP=82a588ba`; once each, waiting for a
  quiet Mac): tex and texdepth, mtld3d and core. The bench issues `DrawPrimitiveUP` alone: it measures what the
  key's sixteen entries cost a UP draw, not a buffer draw or an upload. Into `build/c4c/s1/probe.md` beside
  C4b-3d3's quiet figures; this is also the quiet-Mac take two slices owe, if the Mac is quiet.

### Task 9: review, spec, pin, push

- [ ] Whole-branch review (Opus; the Review Focus verbatim; the ledger's rulings); one fix pass.
- [ ] Spec §5: a C4c-1 paragraph and C4c's slice list. Execution notes here. `dxmt/pins`.
- [ ] Privacy scan; fetch both origins and rebase as the Global Constraints say if they moved; push DXMTRS then
  MacNeutron; post-push check from main (`post.sh`); CI; scratch deleted by full path; memory.

## Rules check

- New types and their derives: `StreamKey` (mtld3d-core; `Clone, Copy, Debug, Default, PartialEq, Eq, Hash`: a
  field of `PipelineKey`, which derives the last five, in an array built by value); `StreamData`, `IndexData`
  (d3d9-rt; `Clone, Copy`: arrays in the tests' `const fn` fixture). Recorded with `--update-derives` in Tasks 4
  and 5. No new `Default` beside `StreamKey`'s.
- New fields: `PipelineKey::streams` (replacing `stride`); `PipelineSource::{layouts, crossing}` and a fourth
  element of its `attrs` (replacing `stream0_extent`, `stride`); `DrawData::{streams, vertex_start, base_vertex,
  instancing}` (replacing `vertices`); `Indices::data` (replacing `bytes`); `CoreDevice::buffers`;
  `DrawRefused::{Buffer, Offset}`. `StreamData` and `IndexData` join d3d9-rt's re-exports.
- New functions: the four lifted rules of Task 4; the store's four; `core_flush` on each buffer; the two draw
  arms. Two new files (`rt/src/device/buffers.rs`, `buffers/tests.rs`). No new static, config key, wire field,
  dependency or lint suppression; `NULL_STREAM_ZEROS` is mtld3d-core's existing immutable static.
- Duplicates looked for: the layouts (lifted out of the closure mtld3d runs, not restated); the fan rewrite, the
  primitive map, the frequency rules (called); the upload (a twin of `texture_upload`: buffer bytes are one row
  with no repack, so what they share is the queue's own allocation call and the budget's three lines);
  `core_flush` twice (the two buffer types are twins throughout mtld3d). The draw arms repeat the order of
  mtld3d's checks because mtld3d's helpers at those points write into its recorder.
- Not silent: the three refusals are logged by name and held by the `buffers` launch's set; a draw dropped for a
  buffer with no core form is logged once and fails its launch.
- Refusal literals: two go from each buffer type (`Lock`, `Unlock`), plus `DrawPrimitive`,
  `DrawIndexedPrimitive` and `GetDirect3D` (80 − 7), and three come (the buffer draws' crossing, offset and
  other-device): 76.

## Rulings (planning)

1. **Every buffer is staged under core in this slice, DYNAMIC ones included, and a lock of a buffer mtld3d maps
   directly dirties the whole buffer.** Why: one mechanism gives every lock flag its meaning (the backing is the
   truth, uploads are in API order); mtld3d's direct mapping shows every byte written through the pointer,
   inside the announced window or not, so a narrow upload would drop bytes mtld3d draws (R1 I4). Cost if wrong:
   none in pixels against mtld3d but the case of ruling 10; a title that appends to a dynamic buffer between
   draws pays a whole-buffer upload and a pass split a draw until C4c-2.
2. **The upload is at the draw, not at `Unlock`.** Why: one site covers the unlock, the draw of a locked buffer
   and the first use; a buffer locked many times between draws uploads once. Cost if wrong: a reader that is not
   a draw would see stale GPU bytes (there is none: `ProcessVertices` is refused).
3. **A declared stream with nothing bound is fed zeros through a constant layout, under UP draws too**, where
   today the core leaves its attributes "not provided". Why: mtld3d feeds it so and provides every resolved
   attribute (R1 read both sides): a short float element then reads w = 1 where the translator's zero gives 0.
   The fixed-function stage moves the same way. Cost if wrong: `buf-null` and `buf-null-up` are the judge, first
   on mtld3d.
4. **Crossing attributes and odd offsets of consumed streams stay refused** (C4c-2, with `CrossingFetch`). Cost
   if wrong: a title with such a layout draws nothing from those buffers under the opt-in runtime until then.
5. **The key holds a stream's step and rate, not its frequency word.** Cost if wrong: none (the word's count is
   the draw's instance count, passed beside the key).
6. **No D3D10-12 gate.** Why: no dxmt-core, d3d11 or winemetal line changes; the fakes are test support only
   d3d9-rt links. Cost if wrong: a D3D10-12 regression ships unseen, which needs a change the Global Constraints
   forbid.
7. **`GetDirect3D` is in this slice**, ahead of C4c-6's getters. Why: the sweep's third fatal call, one guard.
8. **Buffers are created in the store lazily, at their first draw**, not at `CreateVertexBuffer` (the work
   list's row 43). Why: the textures' model; a buffer never drawn costs no GPU memory. Cost if wrong: the first
   draw of a large buffer pays its allocation and whole upload in the frame, as mtld3d's first upload does.
9. **A buffer of another device is refused by name.** Why: the dirty range is one a buffer and the stores one a
   device, so a second device would draw stale bytes and keep an entry nothing forgets (R1 I5, R2 I4). Cost if
   wrong: a title sharing buffers between two devices draws nothing from them under core (what mtld3d does there
   was not established).
10. **Kept divergence, in the core's favour:** a static WRITEONLY default-pool buffer locked again after its
    first unlock keeps the bytes it held, where mtld3d (which released the backing) uploads zeros for what the
    title does not rewrite (R1 m10). D3D9 keeps them. No row sits on this shape.
11. **A `DrawPrimitive` fan too long for mtld3d's shared pattern instances as mtld3d's does** (R2 M3): parity
    with an accident, one condition. Cost if wrong: none against mtld3d.

## Stated gaps

- No row for: `ProcessVertices`; more than two streams; a buffer bound at two streams; point and line-strip
  primitives from buffers (one line-list cell); an instanced draw under the fixed-function stage; a buffer of
  another device (the refusal's rt and front-end paths have no row: two devices in one launch); the `mask &
  used` rule of instancing (Task 5's test of the printed count is its proof: coincident instances show no
  pixel).
- The staged model's cost for dynamic buffers is not measured here (C4c-2 replaces it). In a 32-bit process a
  first draw sends a whole buffer through the staging ring on top of a backing that is never released; with 32
  chunks in flight the worst case is about 1 GiB of staging: C4c-2's cap is sized against it.
- A declared stream the vertex stage does not consume still splits the key: a duplicate pipeline, not a wrong
  one.
- A buffer released after its device's final Release reaches a freed `DeviceInner` (older than this slice, under
  both runtimes); the core arm's `forget_buffer` turns that read into a write. For the maintainer.
- `draw_data::stream_layouts` has no caller in the tree. For the maintainer.
- The Wine suites are not a gate until C4c-7; the next sweep is after C4c-3 (the last of the three fatal calls).

## Appendix: the rows (`buffers`, 27)

The device: back buffer 256x256 X8R8G8B8, no auto depth-stencil. "quad" is a six-vertex triangle list unless
said; vertices are pre-transformed `XYZRHW|DIFFUSE` and pixel-aligned unless said; "read" is the back buffer
through `GetRenderTargetData`. "left" is the quad (16,64)-(112,192), probed at (64,128); "right" is
(144,64)-(240,192), probed at (192,128). Buffers are default-pool, usage 0, unless said. Each row clears to
blue first, so black is a drawn colour. Expectations are what the default runtime draws (Task 1's control
decides); a cell marked (answer) asserts the HRESULT mtld3d gives, which the row's comment then names.

- `buf-d3d`: `GetDirect3D` is `D3D_OK` and returns `main`'s `IDirect3D9` pointer; its reference count is one
  more after the call than before (read through `AddRef`/`Release`); a null out pointer is `D3DERR_INVALIDCALL`.
- `buf-lock`: a 64-byte vertex buffer: `Lock(0, 0, &p, 0)` is `D3D_OK` with `p` non-null; bytes written are read
  back under a `READONLY` lock after `Unlock`; `Lock` at an offset past the length is `D3DERR_INVALIDCALL`; two
  locks then two unlocks are each `D3D_OK`; `GetDesc` says the size, pool, usage and FVF given. The same for a
  16-bit index buffer.
- `buf-draw`: a red quad at the left: left red, right blue.
- `buf-start`: one buffer holding a red triangle inside the left quad then a green quad at the right;
  `DrawPrimitive(TRIANGLELIST, 3, 2)`: right green, left blue.
- `buf-offset`: the same buffer bound with `OffsetInBytes` of three vertices (60 bytes) and start 0: the same
  pixels.
- `buf-strip`: a four-vertex strip at the left: red. A line-list cell: one horizontal line at y = 200 from x =
  16 to 240: (128,200) its colour, (128,202) blue.
- `buf-fan`: a five-vertex buffer whose vertices 1-4 make the right quad as a fan and whose vertex 0 is at the
  left; `DrawPrimitive(TRIANGLEFAN, 1, 2)`: right red, left blue.
- `buf-indexed`: four vertices (the left quad) and a 16-bit index buffer holding six zeros (two degenerate
  triangles) then the quad's six indices; `DrawIndexedPrimitive(TRIANGLELIST, 0, 0, 4, 6, 2)`: left red.
- `buf-index32`: the same with 32-bit indices.
- `buf-base`: eight vertices (a red quad at the left, then a green quad at the right) and the indices of
  vertices 0-3; base vertex 4: right green, left blue. In a second frame, indices rewritten as those of vertices
  4-7 and base vertex -4: left red, right blue.
- `buf-ifan`: a nine-vertex buffer: vertices 0-3 a green quad at the bottom, (128,224), vertex 4 at the left,
  vertices 5-8 the right quad as a red fan; a 16-bit index buffer with two zeros then 1, 2, 3, 4;
  `DrawIndexedPrimitive(TRIANGLEFAN, 4, 0, 9, 2, 2)`: right red, left and bottom blue. A second cell with
  indices 9, 10, 11, 12 and base vertex -4: the same pixels (a base applied twice would draw the bottom quad). A
  third with a 32-bit index buffer: the same.
- `buf-managed`: managed vertex and index buffers: `buf-indexed`'s pixels; then a `READONLY` lock of the vertex
  buffer and a draw: the same pixels. A SYSTEMMEM-pool vertex buffer drawn with `DrawPrimitive`: (answer), and
  the left pixel mtld3d gives.
- `buf-dynamic`: `D3DUSAGE_DYNAMIC|WRITEONLY`, room for nine vertices: `Lock(DISCARD)`, a red triangle inside
  the left quad, `Unlock`, draw; `Lock` of the next three vertices with `NOOVERWRITE`, a green triangle inside
  the right quad, `Unlock`, `DrawPrimitive` from 3: both. Then `Lock` of vertices 3-5 with `NOOVERWRITE` again,
  writing a white triangle at the bottom, (128,224), into vertices 6-8, past the announced window, `Unlock`,
  `DrawPrimitive` from 6: white there (mtld3d maps such a buffer directly: ruling 1).
- `buf-order`: a red quad at the left is drawn; the same range is locked with flags 0 and overwritten with a
  green quad at the right; drawn, with no Clear between: left red and right green (the first draw kept its
  bytes).
- `buf-discard`: a dynamic buffer: draw red at the left, `Lock(DISCARD)` and write green at the right, draw:
  both.
- `buf-held`: `Lock`, write a red quad at the left, draw while locked; write a green quad at the right through
  the same pointer over the same vertices, draw again with no Clear between, `Unlock`: left red (the first
  draw's bytes) and right green (the bytes written after it).
- `buf-relock`: a white quad at the left, drawn once; then `Lock(offset of vertex 2, 20 bytes)` rewrites that
  vertex's colour to red; cleared and drawn: a pixel 8 inside that vertex's corner has green below 128, and a
  pixel 8 inside the corner of the quad's other triangle that no red vertex touches is white.
- `buf-streams`: a declaration with `POSITIONT` float4 on stream 0 and `COLOR0` `D3DCOLOR` on stream 1, two
  buffers; stream 1 holds four red entries then six green, bound at an offset of 16 bytes: the left quad green.
- `buf-null`: a declaration `POSITION` float3 on stream 0 and `TEXCOORD0` float3 on stream 1, a vs_1_1 that
  writes the position and `oD0 = v1.wwww`, a ps that passes the colour, nothing bound at stream 1: the left
  quad's colour from a buffer draw (white if the element's w reads 1, black if 0: what mtld3d gives, expected
  white); the same with a `D3DCOLOR` element: black, on the blue clear. A third cell, Wine's `test_null_stream`
  shape: a fixed-function draw whose declaration names a stream 1 element the stage does not consume, nothing
  bound there: `D3D_OK` and the quad.
- `buf-null-up`: `buf-null`'s first two cells under `DrawPrimitiveUP`.
- `buf-zero-stride`: `buf-streams` with stream 1 holding green, red, white, black, red, white, bound at offset 0
  with stride 0: pixels 8 inside two opposite corners of the left quad are both green.
- `buf-instanced`: vs_3_0 and ps_3_0; stream 0 a small quad (four vertices, six indices) with `INDEXEDDATA | 3`,
  stream 1 three x offsets (0, 80, 160) with `INSTANCEDATA | 1`: `DrawIndexedPrimitive` shows three quads (a
  pixel in each); `DrawPrimitive` of the same quad as a list shows one, at the first offset (non-indexed draws
  never instance). A layout cell: stream 1's word `INSTANCEDATA | 0`: the first offset's pixel drawn, the other
  two blue (a constant stream).
- `buf-shader`: a float3 `POSITION` buffer under vs_1_1 with a constant transform and a ps_2_0 colour: the quad;
  and the fixed-function untransformed path (`XYZ|DIFFUSE` with a world matrix that halves x): a pixel inside
  the narrowed quad drawn, one inside the unscaled quad's edge blue.
- `buf-tex`: `XYZRHW|TEX1` from a buffer with a 2x2 texture, point-sampled: the four quadrant colours.
- `buf-nobuffer`: `DrawPrimitive` with nothing bound at stream 0: `D3DERR_INVALIDCALL`; `DrawIndexedPrimitive`
  with vertices bound and no index buffer: `D3DERR_INVALIDCALL`; `DrawIndexedPrimitive` with neither: `D3D_OK`
  and nothing drawn; an indexed fan with no index buffer: `D3DERR_INVALIDCALL`; an indexed fan whose start index
  plus count ends past the index buffer's pages: `D3DERR_INVALIDCALL`; a primitive count of 0: `D3D_OK` and
  nothing drawn; primitive type 7: `D3DERR_INVALIDCALL`. (Each as mtld3d answers: the control decides.)
- `buf-release`: a buffer is drawn, unbound and released, the frame presented, a new buffer drawn: its pixels.
- `buf-refused`: a stream offset of 2: under strict `D3DERR_NOTAVAILABLE` and nothing drawn, otherwise mtld3d's
  pixels; a stride of 12 under an element that ends at 16: the same pair.

## Review dispositions

- **R1** (design; 1 Critical, 6 Important, 10 Minor): C1 → `stepped_stream_layout` holds the zero-stride arm;
  I1 → the lift is out of `stream_layouts_view`, the parity test against it; I2 → Task 7's three orders and
  `buf-nobuffer`'s cells; I3 → the indexed fan's base folded once, the arm calling none of mtld3d's fan helpers,
  `buf-ifan`'s negative base and mutant (k); I4 → ruling 1's widened locks and `buf-dynamic`'s third cell; I5 →
  ruling 9; I6 → the rows' colours and junk pinned, the blue clear, mutants (i), (j), (k), the SYSTEMMEM,
  `READONLY`, `test_null_stream` and 32-bit fan cells, the `mask & used` rule as a stated gap. Minors → the
  backing class left alone and the forced mode before `dirty`; the offset refusal over used streams, in d3d9-rt;
  ruling 11; the padded length; the 4096 clamp; the inverted test named; the DISCARD warning; `state-runtime`;
  `core_flush`'s false; ruling 10.
- **R2** (core; 0 Critical, 4 Important, 8 Minor): I1 → as R1's C1; I2 → `buffer_create`'s recipe; I3 → the
  order tests on fences and the rt-level mutant; I4 → ruling 9. Minors → the clamp; the null-mapped check and
  the closures' none; ruling 11; `step as u32`; the fakes alone, `copy_length`; `pipeline` returns the entry;
  the 32-bit peak and the late release as stated gaps; 27 rows.
- **R3** (exec; 1 Critical, 10 Important, 13 Minor): C1 → `core_flush` in Task 7; I1 → the red-by-design list;
  I2 → 27; I3 → the 240 s kill; I4 → `bufStart()` and no auto depth-stencil; I5 → the pattern without an
  apostrophe, `sh -n`; I6, I7 → as R1's; I8 → `mutants.txt`, `jm.sh` rewritten, `jp0`; I9 → the other session's
  files and the rebase's gates; I10 → the derives, `Indices::data`, two files. Minors → the dead constants; "does
  not build"; Task 5a; no `test_support.rs`; the fakes' reason; the scripts' headers and the VM loop;
  `buf-relock`'s pixels; the mutants' fallen rows; `jc.sh`'s header (edited before its first run); the cursor
  flake; the probe's wording; the commands.
