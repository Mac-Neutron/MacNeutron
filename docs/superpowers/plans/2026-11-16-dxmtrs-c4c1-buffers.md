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
"Static VB/IB"; `CoreDevice::texture_upload` is the twin). Six DXMTRS commits, each prefix pushable:

1. **`GetDirect3D`** (Task 2). The refusal has no reason (M §D: the parent is stored and retained under core as
   under mtld3d); its guard goes.
2. **Fakes** (Task 3). `dxmt-fakes` prints a pipeline's vertex descriptor and a buffer-to-buffer blit.
3. **The key's streams** (Task 4). `PipelineKey::stride` becomes one entry a stream; `pipeline_source` derives
   mtld3d's layouts (per-vertex, per-instance, constant) and places each attribute at Metal slot = its stream
   (mtld3d's numbering: streams at 0-15, the uniforms at 25-30). UP draws move onto it: stream 0 inline, a
   declared stream above 0 fed zeros through a constant layout, as mtld3d feeds it (ruling 3).
4. **Buffers in d3d9-rt** (Task 5). A buffer store by `BufferId`, the upload, and the draw's new inputs: per
   stream bytes, zeros or a stored buffer at an offset; indices as bytes or a stored buffer; the first vertex,
   the base vertex, the instance count. Nothing reaches it from the front end yet.
5. **Lock and Unlock** (Task 6). Buffers of a core device are made staged with a backing that is never released;
   the two refusals of each buffer type go.
6. **The draws** (Task 7). `DrawPrimitive` and `DrawIndexedPrimitive` take a core arm after their shared checks.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-fakes, mtld3d-core, d3d9-rt, the d3d9.dll front end), C++
(`d3d9_triangle`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4). Program plan:
`docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4c's line). Maps: `build/c4c/map.md` (cited as
M §n), `build/c4/map2-sync-worklist.md` (W §n), `build/c4/map2-d3d11-template.md`. Reviews:
`build/c4c/s1/review-{1-design,2-core,3-exec}.md`. Ledger:
`~/Documents/MacProton/.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check;
  a core-only struct field or private helper is cfg'd in the struct, in every literal and at its definition; an
  arm added to an always-compiled function is one cfg'd block with its `use`s inside it. mtld3d's rules are
  called, not restated: `layout_stride`, `bound_stream_layout`, `stream_step`, `instance_count`,
  `is_instance_data`, `stream_shifts`, `FanRewrite`, `d3d_to_metal_primitive`, `vertex_count`,
  `records_dirty_range`, `DirtyRange`. Where a rule sits inside mtld3d-only code (the closure of
  `draw_data::stream_layouts`, the comparison in `stream_layouts_with`), it is lifted into a named function of
  mtld3d-core that both callers use, with no change of mtld3d's result (Task 4 lists them).
- **No dxmt-core, d3d11 or winemetal change.** `direct::bind_buffer`, `Buffer`, `Queue::allocate_staging_buffer`
  and the buffer-to-buffer blit record exist (M §B.3). `src/dxmt/fakes` (test support) gains printed lines. Should
  a task find that dxmt-core must change, it stops: the change is a ruling and a commit of its own with its unit
  test before the task's, and the x64 `dxmt/check.sh` and the VM's D3D10-12 gate join Task 8.
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code;
  a commit's body follows CONTRIBUTING §Changes. A d3d9-rt or mtld3d-core API change carries its front-end call
  sites and every literal of a changed struct in the same commit. The PE front end compiles only inside
  rust-check: grep the import list of an edited front-end file first. A new or removed Clone/Copy derive needs
  `sh tools/audit.sh --update-derives` before rust-check. If rust-check or a job's build stops at "fetching
  DXMT's Rust crates", `cargo fetch --locked` in both worktrees, then rerun under a new job name.
- No new dependency. No `HashMap` (the store is an `FxHashMap`, as the textures'). No `pub(crate)`. Doc blocks:
  a one-line title, a blank doc line, the body; lines of 100 columns at most; `#[must_use]` on a new
  value-returning pub fn, `const fn` where it can be. A struct holds at most one `bool`, a function at most three.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4c`, base DXMTRS main 82a588ba) and `build/c4b3c/mn`
  (branch `c4c-mn`, base MacNeutron main 342934a9), cargo target `build/c4b3c/cargo`; gate scripts in
  `build/c4c/s1/gates` (copied from `build/c4b3d/s5/gates`, state names `c4c1-*`), logs and reviews in
  `build/c4c/s1`. Builds and GPU jobs under the pipeline lock, the command spelled out in full (zsh does not
  split one held in a variable). No make in a worktree. A job tests **committed** tips (`DXTIP`/`MNTIP`); a job
  script that has run is never edited. Another session (the C/C++ port, branch `c-rust-mn`) shares the lock, the
  main checkout and DXMTRS main: fetch both origins before the pin and rebase onto what moved; its files are the
  remaining C, C++ and Objective-C ones, not `src/d3d9`, but `src/dxmt/fakes` and `test_support.rs` are checked
  for overlap before Task 3's commit.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x3c0-0x3ff** here, one a test (`/usr/bin/grep -rnoE '0x3[c-f][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src`
  finds none today).
- Every DXMTRS commit is safe to run under core: until Task 7 no draw reads a buffer, and until Task 6 no buffer
  locks. Between Task 1's rows and Task 7, MacNeutron's core lanes are red by design on the `buffers` launch
  (every row but `buf-d3d` after Task 2 and `buf-lock` after Task 6; `buffers-runtime`: more refusal lines than
  its set) and on `state-lock` until Task 6 (strict now asks `D3D_OK`). The UP cells of `buf-null-up` turn green
  at Task 4. Should the slice stop before Task 7, the `buffers` kind leaves the three scripts' lists in the
  commit that pins, and `state-lock` keeps its strict form only if Task 6 landed.
- Per task one lane (the Mac's `d3d9-check` on default and core); full gates once, in Task 8. Benches and probes
  run once, on an idle Mac. No gate, build or VM job while the maintainer is gaming. Autonomous work stops at 95%
  weekly usage.

## Scope

In: `IDirect3DVertexBuffer9::Lock`/`Unlock` and `IDirect3DIndexBuffer9::Lock`/`Unlock` for every pool and usage
a buffer is created with today; `DrawPrimitive` and `DrawIndexedPrimitive` for every primitive type mtld3d draws
(points, lines, line strips, triangle lists and strips, fans), with `StartVertex`, `BaseVertexIndex` (signed),
`StartIndex`, 16- and 32-bit indices, stream offsets that are multiples of four, several streams, a stride of
zero, `SetStreamSourceFreq` instancing on indexed draws, a declared stream with nothing bound (buffer and UP
draws), a buffer drawn while locked, a buffer of another device; `GetDirect3D`.

Out (owners): a DYNAMIC default-pool buffer mapped directly and renamed on `DISCARD` (it is correct here, and
slow: each unlock-then-draw is a ring copy and a blit that splits the render pass), an attribute that ends past
its stream's stride and a stream offset off four bytes (`CrossingFetch`; both stay refusals by name, for UP and
buffer draws), the WRITEONLY backing release, the retained-bytes gauge and the i686 cap (C4c-2);
`ProcessVertices`, patches, `SetSoftwareVertexProcessing` (later C4c slices, or never: mtld3d refuses patches
too); per-draw dedup of binds and a hoisted upload (C4d); the conformance lane for the three suites (C4c-7).

## Review Focus

1. **Parity of the layouts.** For every stream the vertex stage consumes: is the layout `pipeline_source` derives
   exactly the one `draw_data::stream_layouts` gives mtld3d for the same binding (stride, zero stride, frequency
   word, inline, nothing bound)? Is "consumes" the same set (`AttrSnapshot::used_streams` against the resolved
   attributes' streams)? Does moving UP draws' other-stream elements from "not provided, the translator's zero"
   to "provided, fetched from zero bytes" match mtld3d in every component (the w of a short float element)?
2. **Order.** Is a buffer's upload always before the draw that reads it and after every earlier draw that read
   the older bytes, including: a draw while locked, two draws around an overwrite without `DISCARD`, an upload
   that commits the chunk (the staging budget), a buffer bound at two streams, the index buffer of a fan (read on
   the CPU)? Can the flush close a pass the draw's key was already resolved against?
3. **Lifetime.** A buffer released while a recorded draw names it; a buffer of another device; a device torn
   down with buffers alive; the store's entry of a released buffer.
4. **The mtld3d body under core.** Does any line `vb_lock`, `ib_lock`, the unlocks, the buffers' constructors or
   the code the new arms call reach mtld3d's recorder (`Mtld3dOnly`), the retention queue, `coherent_seq_arc` or
   a unix call on a core device?
5. **Metal's rules.** Vertex buffer offsets and index buffer offsets as Metal takes them; a constant layout's
   bytes; a slot the pipeline reads and the draw does not bind (the validation layer's verdict in the lanes).
6. **Rows.** Does each row discriminate what its mutant breaks, on pixels the default runtime produced first?
   Are the bands and exact values derivable by hand? What has no row?
7. **Execution.** Each commit's lane expectation; the script edits; the red-by-design list; the probe.

## Tasks

### Task 1: rows (MacNeutron) and the gate scripts

- [ ] **Step 1: the `buffers` kind** in `dxmt/tests/d3d9_triangle.cpp` (appendix: 24 rows `buf-*`; a
  `buffersRows(IDirect3DDevice9 *, bool strict)` table and a branch in `main`, as `msaaRows`; a device with an
  auto D24S8, as `mrt`'s). Helpers reused: `row`, `CK`, `Com`, `frames`' read path (`GetRenderTargetData` into
  `sysSurf`), the `until`, `scene`, `rtHas` family of the later kinds. Budget 180 s.
- [ ] **Step 2: changed rows.** `state-lock` (`rowLock`): strict asks `D3D_OK` for the `Lock` and the `Unlock`.
  The stale comment at `:1099` ("the core refuses texture locks until C4b-3b") goes.
- [ ] **Step 3: scripts.** `dxmt/d3d9-check.sh`: `launch ours fresh buffers 180 buffers $strict`, `check buffers
  "<the 24 names>"`; `state` moves to the branch of `refusals_ok` that asks for exactly zero refusal lines
  (`state_refusals` goes); `buffers_refusals` holds two lines, "IDirect3DDevice9::Draw*Primitive (an attribute
  crossing the stride)" and "IDirect3DDevice9::Draw*Primitive (a stream offset off four bytes)"; the
  `unpresented` pattern gains "buffer is not the core's". `vm/dxmt/d3d9-gate.sh` and `vm/tests/d3d9.sh`: the
  same (`bf64-fresh`, `bf32-fresh`; `runtime <folder> buffers`; `state` to zero).
- [ ] **Step 4: three mingw syntax checks** (x86_64, i686, aarch64), then commit.
- [ ] **Step 5: control on the Mac** (`ja`: default and msl green on every new and changed row; core red exactly
  on the Global Constraints' list). A row red on the default runtime is a wrong expectation or a defect of
  mtld3d: the row is corrected to what mtld3d does (ledgered as parity), never the other way.
- [ ] **Step 6: control in the VM** (`jcr`: default, old WoW64, x64 and x86).

### Task 2: `GetDirect3D` (front end)

**Files:** `src/d3d9/src/device.rs` (`device_get_direct3d`).

- [ ] Trace the parent's reference under core: `create_core_device` retains it (`addref_parent_direct3d9`) and
  stores it; follow the core device's teardown to its `release_parent` and write the path into the commit body.
  If the core teardown does not release it, that is this commit's fix too (a leak today).
- [ ] Remove the `refuse_under_core!` of `device_get_direct3d`; the mtld3d body answers.
- [ ] rust-check; commit; lane `jt2` (core: `buf-d3d` green, the rest as the control).

### Task 3: dxmt-fakes

**Files:** `src/dxmt/fakes/src/lib.rs`; `src/dxmt/src/test_support.rs` only if the blit line belongs there.

- [ ] `MTLDevice_newRenderPipelineStateWithVertexDescriptor`'s fake prints, after its present line, one line a
  used attribute (`  attr <index> format=<f> offset=<o> buffer=<slot>`) and one a used layout (`  layout <slot>
  stride=<s> step=<fn> rate=<r>`), in index order; a descriptor with neither prints nothing more (no existing
  expectation moves: grep the tests that match the pipeline line by prefix first, and adjust those that take the
  call's whole text).
- [ ] The blit decoder prints `blit copy_to_buffer_from_buffer #<src> src_offset=<o> #<dst> dst_offset=<o>
  size=<n>` for `WMT_BLIT_COMMAND_COPY_FROM_BUFFER_TO_BUFFER` (today the record prints as its type alone).
- [ ] One test of each line in the fakes' own tests. rust-check (dxmt-core's, d3d11's and d3d9-rt's tests are the
  proof nothing moved); commit. No lane (test support only).

### Task 4: the key's streams (mtld3d-core, d3d9-rt, front end)

**Files:** `src/d3d9/core/src/pipeline_key.rs` (+ tests), `src/d3d9/core/src/streams.rs`,
`src/d3d9/core/src/draw_data.rs`, `src/d3d9/rt/src/device/draw.rs` (+ tests, fixture),
`src/d3d9/src/device/core_draw.rs`.

- [ ] **mtld3d-core, lifted rules** (no change of any mtld3d result; its tests are the proof): in `streams.rs`
  `inline_stream_layout(stride, extent)` and `null_stream_layout(extent)` (the two arms of
  `draw_data::stream_layouts`' closure, which now calls them), `stepped_stream_layout(app_stride, extent, step,
  rate)` (the body of `bound_stream_layout` after `stream_step`, which now calls it), and
  `StreamLayout::crosses(extent)` (the comparison of `stream_layouts_with`, which now calls it).
- [ ] **`StreamKey`** (`pipeline_key.rs`): `Unused` (default) | `Inline { stride }` | `Bound { stride, step,
  rate }` (the step and rate of `stream_step(freq)`: a frequency word's count is not in the key, or an instanced
  draw would build a pipeline a count) | `Null`. `PipelineKey::stride` → `streams: [StreamKey; MAX_STREAMS]`.
  The front end fills an entry for each stream of the bound declaration's `stream_mask()` (stream 0 alone
  without a declaration) and leaves the rest `Unused`.
- [ ] **`pipeline_source`**: every resolved attribute is kept, at `buffer = its stream`; for each stream with an
  attribute, the layout from the key's entry and the stream's extent (`resolved.extents`): `Inline` →
  `inline_stream_layout`, `Bound` → `stepped_stream_layout`, `Null` or `Unused` → `null_stream_layout`.
  `PipelineSource` gains `attrs: Vec<(index, format, offset, slot)>`, `layouts: [StreamLayout; MAX_STREAMS]`
  (`UNUSED` for a stream without an attribute) and `crossing: u16` (bit n: `layouts[n].crosses(extent)`), and
  loses `stream0_extent` and `stride`; `crosses_stride()` is `crossing != 0`. `provided_input_mask` is every
  kept attribute's bit again (the filter on buffer 0 goes).
- [ ] **d3d9-rt**: `build_pipeline` writes each attribute at its slot and each used layout with its step
  function and rate (the wire's `MTLVertexStepFunction` numbers of mtld3d's `VertexStepFunction`, name by name,
  as `wire_factor` does for blends). `DrawData::vertices` → `streams: [StreamData; MAX_STREAMS]` with
  `StreamData::{None, Bytes(&[u8]), Zeros}` for now; the closure binds, for each slot the pipeline's layouts
  use, the stream's bytes (`direct::bind_bytes`) or `layouts[slot].stride` bytes of mtld3d-core's
  `NULL_STREAM_ZEROS`; a used slot whose data is `None` binds the zeros too (and is a `debug_assert`).
  `draw_up` keeps its name until Task 5.
- [ ] **Front end** (`core_draw.rs`): `resolve` takes the streams instead of a stride; a UP draw's are `Inline {
  stride }` at 0 and `Null` at each other declared stream; its data `Bytes` at 0 and `Zeros` there.
- [ ] **Tests** (rt, on the fakes' new lines): a two-stream declaration under a UP draw builds a pipeline with
  stream 0's attributes at buffer 0 and layout 0 per-vertex at the call's stride, stream 1's at buffer 1 under a
  constant layout of the element's extent, and the draw binds zeros of that length at slot 1; a `Bound` entry
  with `PerInstance` rate 2 prints that step and rate; a zero stride prints a constant layout; two keys that
  differ in one stream's stride are two pipelines; crossing on stream 1 refuses as crossing on stream 0 does.
  mtld3d-core: `pipeline_source` against `stream_layouts` on the same bindings, stream by stream (the parity
  test of Review Focus 1), for inline, bound, zero-stride, per-instance, instance-rate-0 and null.
- [ ] rust-check; commit; lane `jt4` (default ok; core: all nine existing launches as before, the two UP cells
  of `buf-null-up` green if the control showed the w of a short element, the rest of `buffers` as the control).

### Task 5: buffers in d3d9-rt

**Files:** `src/d3d9/rt/src/device.rs`, new `src/d3d9/rt/src/device/buffers.rs` (+ `buffers/tests.rs`),
`src/d3d9/rt/src/device/draw.rs` (+ tests), `src/d3d9/src/device/core_draw.rs` (the rename's call sites).

- [ ] **The store**: `buffers: FxHashMap<BufferId, Arc<Buffer>>` on `CoreDevice`. `has_buffer(id)`;
  `buffer_create(id, length)` (a dxmt-core `Buffer` with a private allocation, as D3D11 makes a default-usage
  buffer without initial data, M §B.3; no zeroing: its first upload carries every byte, mtld3d's rule that a
  staged buffer starts full-dirty); `forget_buffer(id)` (the entry goes; recorded draws hold the `Arc`).
- [ ] **`buffer_upload(id, offset, bytes) -> bool`**: false when the store lacks `id`, `bytes` is empty or the
  range ends past the buffer. Otherwise the bytes go into `allocate_staging_buffer(len, 16)` now, the open render
  pass ends (`open_blit_pass`), and a closure takes `access_buffer::<COMPUTE>(.., WRITE)` and encodes
  `WmtcmdBlitCopyFromBufferToBuffer` at `allocation offset + offset`; `staged_bytes` and the commit past
  `staged_budget` as `texture_upload`.
- [ ] **The draw**: `draw_up` → `draw`. `StreamData` gains `Buffer { id, offset }`; `Indices::bytes` →
  `data: IndexData::{Bytes(&[u8]), Buffer { id, offset }}`; `DrawData` gains `vertex_start: u32`, `base_vertex:
  i32`, and `instancing: Option<(u32, u16)>` (stream 0's frequency word and the mask of bound streams whose word
  is instance data, for an indexed draw of bound streams; none otherwise). The instance count is
  `streams::instance_count(word, mask & used != 0)` with `used` the streams the pipeline's layouts use: mtld3d's
  rule (`unix/src/draw.rs:877-886`), 1 without `instancing`. A stored buffer binds with `direct::bind_buffer`
  (READ tracked) at the stream's offset; an index buffer resolves with `access_buffer::<VERTEX>(.., READ)` and
  draws at `allocation offset + offset`. A `Buffer` whose id the store lacks records nothing:
  `DrawRefused::Buffer`.
- [ ] **Tests** (fakes): an upload prints the blit with the offsets and closes an open render pass; an upload
  past the budget commits; a draw from a stored buffer prints `render 2 buffer=#.. offset=.. index=<slot>` per
  bound stream then `render 19` with `vertex_start`; an indexed one prints `render 20` with the stored buffer's
  handle, `index_buffer_offset`, `base_vertex` (a negative one too) and `instance_count`; instancing counts only
  when a used stream is in the mask; draw, upload, draw is three passes in that order (render, blit, render);
  a forgotten buffer's recorded draw still encodes; an unknown id is `DrawRefused::Buffer`.
- [ ] rust-check; commit; lane `jt5` (as `jt4`).

### Task 6: Lock and Unlock (front end)

**Files:** `src/d3d9/src/vertex_buffer.rs`, `src/d3d9/src/index_buffer.rs`, `src/d3d9/src/device.rs`
(`retire_released_buffer`, the two creators' call of the constructors).

- [ ] **Creation**: a buffer of a core device takes `BufferMapMode::Staged` and a backing class that is never
  released, whatever `classify_map_mode` and `classify_backing` say for its usage and pool (one cfg'd `if` in
  each constructor; `VertexBufferCreateInfo` and its index twin already carry `device_inner`). It starts
  full-dirty, as every staged buffer.
- [ ] **`Lock`**: the refusal goes. Walk the body line by line under core first (Review Focus 4) and write the
  walk into the commit body: the staged branch records the dirty range and hands out the backing's pointer;
  nothing in it may reach the recorder, the retention queue, `coherent_seq_arc` or `restore_backing`.
- [ ] **`Unlock`**: the refusal goes; under core it ends after the lock count's bookkeeping (no transient page,
  no `push_stage_upload`, no `release_backing_after_upload`): the dirty range waits for the draw.
- [ ] **`core_flush(&mut self, core: &mut CoreDevice) -> bool`** on each buffer's inner (cfg'd): a buffer the
  store lacks is created and uploads its whole backing; otherwise its dirty span, if any, uploads; the dirty
  range is cleared only when no lock is out (a locked buffer uploads again at the next draw, mtld3d's
  `flush_staged_if_mapped`). False when the upload was refused.
- [ ] **Release**: `retire_released_buffer`'s core arm calls `core.forget_buffer(buffer_id)`.
- [ ] rust-check; commit; lane `jt6` (core: `state-lock` and `buf-lock` green; `state-runtime` with zero
  refusals).

### Task 7: the draws (front end)

**Files:** `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/device.rs` (`device_draw_primitive`,
`device_draw_indexed_primitive`).

- [ ] **Placement**: each entry point's `refuse_under_core!` goes; after the checks both runtimes share (the
  missing layout source, a count of zero), one cfg'd block calls `core_draw::draw_primitive(dev, primitive,
  start_vertex, primitive_count)` or `core_draw::draw_indexed_primitive(dev, primitive, base_vertex,
  start_index, primitive_count)` and returns its answer on a core device. The arms answer what mtld3d answers at
  each later check, in its order (walk `device.rs:12758-12857` and `:13056-13152` and list each check and answer
  in the commit body): an unknown primitive type, a vertex count of zero, no declared stream with a buffer
  ("no vertex buffer bound"), no index buffer, an index format, a fan the rewrite cannot address.
- [ ] **Streams**: for each stream of `declared_stream_mask`: a bound buffer → `core_flush`, key `Bound {
  stride, step, rate }` from `stream_step(freq)`, data `Buffer { id, offset }`; nothing bound → `Null` and
  `Zeros`. The flush of every buffer the draw reads (and the index buffer) runs first, before `resolve` reads
  the targets and builds the key: an upload ends the open pass and may commit.
- [ ] **Refusals**, before any record: `stream_shifts(bound streams) != 0` → "IDirect3DDevice9::Draw*Primitive
  (a stream offset off four bytes)"; `DrawRefused::Crossing` → "IDirect3DDevice9::Draw*Primitive (an attribute
  crossing the stride)" (the UP arm keeps its own literal). `DrawRefused::Buffer` logs once "a draw's buffer is
  not the core's; the draw is dropped" and answers `D3D_OK`.
- [ ] **Fans**: `DrawPrimitive` → `FanRewrite::sequential(start_vertex, primitive_count)` as index bytes, vertex
  start 0; `DrawIndexedPrimitive` → `FanRewrite::indexed` over the index buffer's CPU bytes from `start_index`
  (the backing is never released under core), base vertex kept. Both draw a triangle list of index bytes.
- [ ] **Indexed**: indices `Buffer { id, offset: start_index * size }`; `instancing` = stream 0's frequency word
  and the mask of bound declared streams with `is_instance_data`. Non-indexed draws and sequential fans pass
  none (mtld3d: they never instance); an indexed fan passes it (mtld3d's `IndexView` match, M §B.1).
- [ ] rust-check; commit; lane `jt7` (default ok; core: every row of `buffers` green strict,
  `buffers-runtime` with its two refusal lines, all other launches as before).

### Task 8: gates, mutants, the probe

- [ ] **`jq`**: the three Mac lanes; the stateblock conformance under core equal to default; all ten core
  launches under Metal's validation layer with no report.
- [ ] **Mutants** (`jm`; one line each, no unused item left in the PE build; each red on its row and on nothing
  outside `buffers-*`): (a) the draw's `vertex_start` is 0 → `buf-start`; (b) `base_vertex` is 0 → `buf-base`;
  (c) `core_flush` skips a locked buffer → `buf-held`; (d) the upload goes to offset 0 → `buf-relock`; (e) every
  attribute is placed at slot 0 → `buf-streams`; (f) a null stream's attributes are not provided (the old zero
  fill) → `buf-null` (if the control showed no difference, the mutant is dropped and the gap stated); (g) the
  instance count is 1 → `buf-instanced`; (h) the index offset ignores `start_index` → `buf-indexed`.
- [ ] **Plain build** (`jr`): translator key and imports unchanged; `d3d9-check` default; the Mac's Wine
  conformance with no new failing site (mtld3d-core's lifted functions are on the default runtime's path); the
  x64 `dxmt/check.sh`.
- [ ] **VM** (`jc`): the D3D9 lanes on default and core, old and new WoW64. The D3D10-12 gate is not run
  (dxmt-core, d3d11 and winemetal are unchanged: ruling 6).
- [ ] **Probe** (`jp`, once, waiting for a quiet Mac): tex and texdepth, mtld3d and core, this tip against
  82a588ba in the same window (the key grew by sixteen entries, hashed a draw), into `build/c4c/s1/probe.md`
  beside C4b-3d3's quiet figures; this is also the quiet-Mac take two slices owe, if the Mac is quiet.

### Task 9: review, spec, pin, push

- [ ] Whole-branch review (Opus; the Review Focus verbatim; the ledger's rulings); one fix pass.
- [ ] Spec §5: a C4c-1 paragraph and C4c's slice list. Execution notes here. `dxmt/pins`.
- [ ] Privacy scan; fetch-check both origins and rebase if the other session moved them; push DXMTRS then
  MacNeutron; post-push check from main; CI; scratch deleted by full path; memory.

## Rules check

- New types: `StreamKey` (mtld3d-core), `StreamData`, `IndexData` (d3d9-rt). New fields: `PipelineKey::streams`
  (replacing `stride`), `PipelineSource::{layouts, crossing}` and a fourth element of its `attrs` (replacing
  `stream0_extent`, `stride`), `DrawData::{streams, vertex_start, base_vertex, instancing}` (replacing
  `vertices`), `CoreDevice::buffers`, `DrawRefused::Buffer`. New functions: the four lifted rules of Task 4,
  the store's four, `core_flush` on each buffer, the two draw arms. One new file (`rt/src/device/buffers.rs`,
  with its tests). No new static, config key, wire field, dependency or lint suppression; `NULL_STREAM_ZEROS` is
  mtld3d-core's existing immutable static.
- Duplicates looked for: the layouts (lifted, not restated); the fan rewrite, the primitive map, the frequency
  rules (called); the upload (a twin of `texture_upload`: buffer bytes are one row, with no repack, so the
  shared part is the ring allocation and the budget, which stay two call sites of the queue's own function).
  `vb_lock` and `ib_lock` are already twins in mtld3d; the core adds one `core_flush` to each, eight lines.
- Not silent: the two refusals are logged by name and held by the `buffers` launch's set; a draw dropped for a
  buffer the store lacks is logged once and fails its launch.
- Three refusal literals go from each buffer type's pair (`Lock`, `Unlock`), plus `DrawPrimitive`,
  `DrawIndexedPrimitive` and `GetDirect3D`: 80 → 75, with 2 added (the buffer draws' crossing and offset).

## Rulings (planning)

1. **Every buffer is staged under core in this slice, DYNAMIC ones included.** Why: one mechanism gives every
   lock flag its meaning (the backing is the truth, uploads are in API order), and the direct mapping with
   renames is its own slice with its own controls. Cost if wrong: none in pixels; a title that appends to a
   dynamic buffer between draws pays a pass split a draw until C4c-2.
2. **The upload is at the draw, not at `Unlock`.** Why: one site covers the unlock, the draw of a locked buffer
   and the first use; a buffer locked many times between draws uploads once. Cost if wrong: a reader that is not
   a draw would see stale GPU bytes (there is none: `ProcessVertices` is refused).
3. **A declared stream with nothing bound is fed zeros through a constant layout, under UP draws too**, where
   today the core leaves its attributes "not provided". Why: mtld3d feeds it so, and Metal's fetch of a short
   float element from zeros gives w = 1 where the translator's zero gives 0. Cost if wrong (the control shows no
   difference, or the other value): the UP path moved for nothing; `buf-null-up` is the judge, first on mtld3d.
4. **Crossing attributes and odd stream offsets stay refused** (C4c-2, with `CrossingFetch`). Cost if wrong: a
   title with such a layout draws nothing from those buffers under the opt-in runtime until then.
5. **The key holds a stream's step and rate, not its frequency word.** Cost if wrong: none (the word's count is
   the draw's instance count, passed beside the key).
6. **No D3D10-12 gate.** Why: no dxmt-core, d3d11 or winemetal line changes; the fakes are test support. Cost if
   wrong: a D3D10-12 regression from this slice ships unseen, which needs a change the Global Constraints forbid.
7. **`GetDirect3D` is in this slice**, ahead of C4c-6's getters. Why: the sweep's third fatal call, one guard.
8. **Buffers are created in the store lazily, at their first draw**, not at `CreateVertexBuffer` (the work
   list's row 43). Why: the textures' model; a buffer never drawn costs no GPU memory. Cost if wrong: the first
   draw of a large buffer pays its allocation and whole upload in the frame, as mtld3d's first upload does.

## Stated gaps

- No row for: `ProcessVertices`; more than two streams; a buffer bound at two streams; point and line-strip
  primitives from buffers (one line-list cell); an instanced draw under the fixed-function stage.
- The staged model's cost for dynamic buffers is not measured here (C4c-2 replaces it).
- A declared stream the vertex stage does not consume still splits the key (two pipelines that differ in an
  entry nothing reads): a duplicate pipeline, not a wrong one.
- The Wine suites are not a gate until C4c-7; the next sweep is after C4c-3 (the last of the three fatal calls).

## Appendix: the rows (`buffers`, 24)

The device: back buffer 256x256 X8R8G8B8, auto D24S8. "quad" is two triangles; vertices are pre-transformed
`XYZRHW|DIFFUSE` and pixel-aligned unless said; "read" is the back buffer through `GetRenderTargetData`. Buffers
are default-pool, usage 0, unless said. Each row clears to black first. Expectations are what the default runtime
draws (Task 1's control decides); a cell marked (answer) asserts the HRESULT mtld3d gives.

- `buf-d3d`: `GetDirect3D` is `D3D_OK` and returns `main`'s `IDirect3D9` pointer; its reference count is one
  more after the call than before (read through `AddRef`/`Release`); a null out pointer is `D3DERR_INVALIDCALL`.
- `buf-lock`: a 64-byte vertex buffer: `Lock(0, 0, &p, 0)` is `D3D_OK` with `p` non-null; bytes written are read
  back under a `READONLY` lock after `Unlock`; `Lock` at an offset past the length is `D3DERR_INVALIDCALL`; two
  locks then two unlocks are each `D3D_OK`; `GetDesc` says the size, pool, usage and FVF given. The same for a
  16-bit index buffer.
- `buf-draw`: a quad over (64,64)-(192,192) in red as a six-vertex list: (128,128) red, (32,32) black.
- `buf-start`: one buffer holding a red triangle then a green quad elsewhere; `DrawPrimitive(TRIANGLELIST, 3,
  2)`: the quad's pixel green, the triangle's black.
- `buf-offset`: the same buffer bound with `OffsetInBytes` of three vertices (a multiple of four) and start 0:
  the same pixels.
- `buf-strip`: a four-vertex strip: the quad's pixels. `buf-fan`: a five-vertex buffer whose vertices 1-4 make a
  fan, `DrawPrimitive(TRIANGLEFAN, 1, 2)`: its pixels, and vertex 0's corner black. A line list cell in
  `buf-strip`: one horizontal line at y = 200, a pixel on it and one beside it.
- `buf-indexed`: four vertices and a 16-bit index buffer holding six junk indices then the quad's six;
  `DrawIndexedPrimitive(TRIANGLELIST, 0, 0, 4, 6, 2)`: the quad.
- `buf-index32`: the same with 32-bit indices.
- `buf-base`: eight vertices (a red quad then a green quad elsewhere), indices 0-3's; base vertex 4 draws the
  green quad alone; then indices written as 4-7's with base vertex -4: the red quad alone.
- `buf-ifan`: an indexed fan from a start index of 2 with base vertex 4: its pixels.
- `buf-managed`: managed vertex and index buffers: `buf-indexed`'s pixels.
- `buf-dynamic`: `D3DUSAGE_DYNAMIC|WRITEONLY`: `Lock(DISCARD)`, a red triangle, `Unlock`, draw; `Lock` at the
  next three vertices with `NOOVERWRITE`, a green triangle elsewhere, `Unlock`, `DrawPrimitive` from 3: both.
- `buf-order`: a red quad at the left is drawn; the same range is locked with flags 0 and overwritten with a
  green quad at the right; drawn: red at the left and green at the right (the first draw kept its bytes).
- `buf-discard`: a dynamic buffer: draw red left, `Lock(DISCARD)` and write green right, draw: both.
- `buf-held`: `Lock`, write a red quad at the left, draw while locked; write a green quad at the right through
  the same pointer over the same vertices, draw again with no Clear between, `Unlock`: red at the left (the first
  draw's bytes) and green at the right (the bytes written after it).
- `buf-relock`: a white quad drawn once; then `Lock(offset of vertex 2, one vertex)` rewrites that vertex's
  colour to red; cleared and drawn: the pixel nearest that corner is redder than white's (R 255, G below 128),
  the opposite corner white.
- `buf-streams`: a declaration with `POSITIONT` on stream 0 and `COLOR0` on stream 1, two buffers, stream 1
  bound at an offset of 16 bytes past four junk colours: the quad in stream 1's colour (green).
- `buf-null`: a declaration `POSITION` float3 on stream 0 and `TEXCOORD0` float3 on stream 1, vs_1_1 that writes
  the position and `oD0 = v1.wwww`, nothing bound at stream 1: the quad's colour from a buffer draw (white if
  the element's w reads 1, black if 0: what mtld3d gives); the same with a `D3DCOLOR` element: black.
- `buf-null-up`: `buf-null`'s two cells under `DrawPrimitiveUP`.
- `buf-zero-stride`: `buf-streams` with stream 1 bound at stride 0: every vertex takes the first colour.
- `buf-instanced`: vs_3_0 and ps_3_0; stream 0 a small quad with `INDEXEDDATA | 3`, stream 1 three x offsets
  with `INSTANCEDATA | 1`: `DrawIndexedPrimitive` shows three quads; `DrawPrimitive` of the same two triangles
  shows one, at the first offset (non-indexed draws never instance); with stream 1's word `INSTANCEDATA | 0` the
  indexed draw's quads all sit at the first offset (a constant stream), under three instances.
- `buf-shader`: a float3 `POSITION` buffer under vs_1_1 with a constant transform and a ps_2_0 colour: the quad;
  and the fixed-function untransformed path (`XYZ|DIFFUSE` with a world matrix that halves x): the narrowed quad.
- `buf-tex`: `XYZRHW|TEX1` from a buffer with a 2x2 texture, point-sampled: the four quadrant colours.
- `buf-nobuffer`: `DrawPrimitive` with nothing bound at stream 0 (answer); `DrawIndexedPrimitive` with vertices
  bound and no index buffer (answer); a primitive count of 0 is `D3D_OK` and draws nothing; primitive type 7
  (answer).
- `buf-release`: a buffer is drawn, unbound and released, the frame presented, a new buffer drawn: its pixels.
- `buf-refused`: a stream offset of 2: under strict `D3DERR_NOTAVAILABLE` and nothing drawn, otherwise mtld3d's
  pixels; a stride of 12 under an element that ends at 16: the same pair.
