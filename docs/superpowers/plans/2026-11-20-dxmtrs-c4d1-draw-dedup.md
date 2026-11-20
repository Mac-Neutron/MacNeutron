# DXMTRS C4d-1: Core Draw-Path Deduplication (Post-Review Final Plan)

First implementation slice of C4d. Purely inside `src/d3d9/` (DXMTRS `c4d` branch based at `91a0fc4c`, MacNeutron `c4d-mn` based at `10a554a0`): zero changes to `src/dxmt`, `src/winemetal`, `tools/derive_inventory.txt`, D3D10-12, or the translator key.

Inputs:
- `build/c4d/map-1-draw-cost.md` (hot-path audit vs `mtld3d`)
- `build/c4d/s0/probe-ab.md` (one-window A/B probe `21d0ca5f` vs `91a0fc4c` + `/usr/bin/sample` CPU profiles)
- Reviews: `build/c4d/s1/review-{1-design,2-core,3-exec}.md` (R1, R2, R3 — all Critical, Important, and Minor findings folded below)
- Ledger: `.superpowers/sdd/2026-11-20-dxmtrs-c4d/progress.md`

## Goal

Eliminate the per-draw redundant CPU work on `runtime.mode = core` measured in C4d-0:
- **Baseline (`91a0fc4c`, Round 2 load 2.53)**: `tex` 2.70 ms/frame (vs mtld3d 1.22, +121%), `buf` 2.75 ms/frame (vs mtld3d 1.23, +124%), `dyn` 2.82 ms/frame (vs mtld3d 1.47, +92%).
- **Profile split**: API thread = 59% (`tex`) / 75% (`buf`); `dxmt-encode-thr` = 38% (`tex`) / 23% (`buf`) (AGX `encodeAndEmitRenderState`, `setViewports`, `setVertexBytes`, `_MTLRenderCommandEncoder_encodeCommands`); `dxmt-finish-thr` = 1.5-3.6%.
- **Target**: Close the CPU gap with `mtld3d` on `tex`, `buf`, and `dyn` while keeping zero regressions in conformance, Metal validation, and `dxmt-core` render-pass merging.

## Architecture

### Layer 1: `d3d9-rt` (`CoreDevice`)

Files: `src/d3d9/rt/src/device.rs`, `src/d3d9/rt/src/device/pass_bound.rs` (new), `src/d3d9/rt/src/device/draw.rs`, `src/d3d9/rt/src/device/draw/tests.rs`, `src/d3d9/rt/src/device/recorder.rs`, `src/d3d9/rt/src/device/buffers.rs`, `src/d3d9/rt/src/device/buffers/tests.rs`, `src/d3d9/rt/src/device/target.rs`, `src/d3d9/rt/src/device/depth.rs`, `src/d3d9/rt/src/device/textures.rs`, `src/d3d9/rt/src/device/swapchain.rs`.

1. **Monotonic `target_gen: u64` on `CoreDevice`** (R2 M3, R1 M2):
   - Bumped on `set_target`, `set_extra_target`, `set_depth`, `make_back_buffer` (covers `attach` and `resize`), `forget_texture` when bound as `target`/`extras`/`depth`, and `texture_create` when replacing an `id` bound as `target`/`extras`/`depth`.
   - Exposed via `pub fn target_gen(&self) -> u64`.

2. **Per-device PSO LRU memo (`PsoMemo`) on `CoreDevice`** (R1 I3, R2 C2, R2 M4):
   - 24-entry MRU storing `(key_gen: u64, key_hash: u64, key: PipelineKey, entry: Arc<Compiled<Option<PipelineSource>>>, used_mask: u16)` (plus compact active binding list so draws avoid rescanning 16 slots twice).
   - Hash `PipelineKey` strictly with `rustc_hash::FxHasher` (never `DefaultHasher`).
   - `key_gen` reaches `CoreDevice::draw` via `DrawData::key_gen: u64` (default `0` for direct callers/tests).
   - Index-0 O(1) hit: when `key_gen != 0 && self.pso_memo[0].key_gen == key_gen`, borrow `&self.pso_memo[0]` directly (zero `Arc::clone`/`drop`, zero hashing, zero Mutex lock!) with `debug_assert_eq!(&self.pso_memo[0].key, key)`.
   - On `key_gen` miss, scan the 24 entries by `(key_hash, key)`; on hit promote to index 0 and update `entry.key_gen = key_gen` (if `key_gen != 0`).
   - On full miss, check `refused_layout(key)` (`Err(DrawRefused::Fetch)`), look up / insert in `PsoMap`, and **only** insert into `PsoMemo` when `entry.ready().is_some()` and `entry.spec.as_ref().is_some()` (never poison memo with refused/failed pipelines).

3. **Dynamic buffer hot path (`src/d3d9/rt/src/device/buffers.rs`)** (R1 C4, R2 I5, R3 1.1):
   - Add `next_generation: u64` (starts at `1`) on `BufferStore`, and `generation: u64` on `Stored` (assigned fresh monotonic `next_generation` on `buffer_create`, `dynamic_create`, and whenever `dynamic_lock` succeeds in `self.rename(...)`).
   - Cache `mapped: *mut u8` on `buffers::Dynamic` (initialized in `dynamic_create`, refreshed on `rename`).
   - In `CoreDevice::dynamic_lock`: fast-path `flags & (D3DLOCK_NOOVERWRITE | D3DLOCK_READONLY) != 0` directly via `self.buffers.map.get(&id)` (bounds-checking `offset <= dynamic.length` and returning `dynamic.mapped.add(offset as usize)` with zero `Arc::clone`, zero `coherent_seq()`, and zero `Mutex::lock`).
   - Combine `last_use = seq` stamping directly into `CoreDevice::draw`'s stream/index `buffers.map.get_mut(&id)` lookup (only writing if `last_use != seq`), eliminating `stamp_dynamic`'s second 16-slot loop and hash lookups completely! (Keep `stamp_dynamic` only if any dynamic buffer was touched, OR stamp `last_use` right after `self.emit(...)` from a stack `[BufferId; 17]` of dynamic IDs collected during validation before `PassBound` filtering so deduped dynamic binds ALWAYS stamp `last_use`!)

4. **Strict 3-phase ordering in `CoreDevice::draw`** (R2 C1 — CRITICAL):
   - **Phase 1 (Pure Validation, ZERO `PassBound` reads/writes)**: validate target/samples/colors/depth/scissor/pipeline/stream offsets/buffer existence & collect dynamic IDs (`[BufferId; 17]`) and used bindings on the stack.
   - **Phase 2 (`self.open_pass(with_depth, colors)`)**: opens pass if needed (closing prior pass and calling `self.pass_bound.reset()` whenever a new pass opens). `close_pass()` ALSO calls `self.pass_bound.reset()`.
   - **Phase 3 (All `PassBound` diffing & mutation + `self.emit(...)` + stamp dynamic `last_use`)**: runs strictly AFTER `open_pass()`, guaranteeing Draw 0 of every pass emits full Metal state even when `with_depth` or sRGB color format splits a pass!

5. **Per-pass `PassBound` tracker (`src/d3d9/rt/src/device/pass_bound.rs`)** (R1 I4, R2 I3, R2 M1, R2 M2, R3 1.4):
   - Replaces `CoreDevice::pass_scissor`. Never derives `Clone` or `Copy`; keeps `< 2` `bool` fields per struct (use `u8` / `Option` / `bitflags` as needed).
   - **Whole-state fast-path `last_draw_gen: u64`**: cleared to `0` on `reset()`. When `draw.key_gen != 0 && self.render_open && self.pass_bound.last_draw_gen == draw.key_gen && self.pass_bound.pso != 0` (meaning target, depth, PSO, DSSO, blend color, viewport, scissor, rasterizer, all constant `draw.binds`, and all `draw.textures` are identical to the previous draw in THIS open pass — only streams/indices/draw-counts can differ!): bypass PSO/DSSO/blend/viewport/scissor/rasterizer/constant-bytes/texture-sampler checks and `depth_state_handle` completely (1 integer compare skips ~500B/draw of `memcmp`!), proceeding directly to stream/index diffing! (Set `last_draw_gen = draw.key_gen` after a full Phase 3 diff when `draw.key_gen != 0`.)
   - **Individual tracked fields** (all slot accesses use `.get_mut(usize::from(slot))`, falling back to unconditional emit if out of bounds):
     - `pso: u64`
     - `dsso: Option<(u64, u8)>` (`(handle, stencil_ref)`)
     - `blend_color: Option<[u32; 4]>` (bit equality; passes current `draw.stencil_ref` when emitting)
     - `viewport: Option<[u64; 6]>`
     - `scissor: Option<[u32; 4]>`
     - `rasterizer: Option<(u8, u8, u32)>` (`(fill_mode, cull_mode, slope_scale.to_bits())`)
     - `vertex_buffers: [Option<BoundBuffer>; 32]` (`struct BoundBuffer { id: BufferId, generation: u64, offset: u64 }`)
     - `vertex_bytes: [BytesSlot; 32]`, `pixel_bytes: [BytesSlot; 32]` (`struct BytesSlot { valid: bool, data: Vec<u8> }`; `reset()` sets `valid = false` keeping capacity warm; binding a vertex buffer at `slot` sets `vertex_bytes[slot].valid = false`; binding vertex bytes at `slot` sets `vertex_buffers[slot] = None`; `StreamData::Bytes` emits unconditionally and clears both `vertex_buffers[slot]` and `vertex_bytes[slot].valid = false`)
     - **Independent texture & sampler arrays** (R1 I4):
       - `vertex_textures: [Option<BoundTex>; 4]`, `pixel_textures: [Option<BoundTex>; 16]` (`struct BoundTex { texture: usize, key: TextureViewKey }`, ABA-safe within one open pass because draw 0 retains `Arc<Texture>` until `commit` -> `close_pass` -> `reset` and `d3d9-rt` never renames textures)
       - `vertex_samplers: [Option<usize>; 4]`, `pixel_samplers: [Option<usize>; 16]` (`None` = unset in pass, `Some(0)` = null sampler, `Some(ptr)` = `Arc::as_ptr(&sampler)`)
     - `forget_buffer(id)` clears matching entries in `pass_bound.vertex_buffers`.
     - `resolve_render_pass_barrier(Encoder::dispatch_tile_barrier)` runs whenever any texture is newly emitted (`!textures_emit.is_empty()`).

6. **Compact zero-allocation fast-path closures** (R1 I2, R2 I1, R2 I2, R3 1.3, R3 1.4):
   - Every compact closure MUST call `enc.bump_visibility_result_offset()` before `WmtcmdRenderDraw` / `WmtcmdRenderDrawIndexed`.
   - **Closure A (Zero-state-change non-indexed — `buf` & `dyn` 255/256 draws)**: truly `Copy`, captures only scalar `(primitive_type, vertex_start, vertex_count, instances)` (zero `Vec`, zero `Arc`).
   - **Closure B (Zero-state-change stored indexed)**: zero `Vec`, captures `(Arc<Buffer>, offset, primitive_type, index_type, index_count, instances, base_vertex)` calling `enc.access_buffer` + `bump_visibility_result_offset` + `WmtcmdRenderDrawIndexed`.
   - **Closure C (Zero-state-change single inline UP vertex draw — `tex` draws 1..1999)**: strictly gated on `indexed.is_none()`, zero state/buffer/texture/constant changes, and **exactly 1** `Stage::Vertex` `StreamData::Bytes` bind (so `CrossingFetch` >=2 slots and `DrawIndexedPrimitiveUP`/`TRIANGLEFAN` fall through to the general path!). Copies the UP bytes directly into `Chunk::allocate_cpu(len, 16)` (zero heap allocation for ALL UP sizes!) or passes via `direct::bind_bytes::<{ stage::VERTEX }>` from that chunk slice, then calls `enc.bump_visibility_result_offset()` + `WmtcmdRenderDraw`. (Wait: verify `Chunk::allocate_cpu` visibility from `d3d9-rt` — if `Chunk::allocate_cpu` is private to `dxmt-core`, use an inline `[u8; 256]` stack array + `len: u16` for `<= 256` bytes and `Vec<u8>` above 256 bytes, keeping the diff 100% inside `src/d3d9/` with ZERO `src/dxmt/` changes!)

### Layer 2: `d3d9` (`DeviceInner` / `core_draw.rs`)

Files: `src/d3d9/src/device.rs`, `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/texture.rs`, `src/d3d9/src/surface.rs`, `src/d3d9/src/vertex_buffer.rs`, `src/d3d9/src/index_buffer.rs`.

1. **`CoreDrawCache` on `DeviceInner`** (R1 C1, C2, C3, I5, I6, I7, I8, M3; R2 C2; R3 1.2, 1.4):
   - Declared **before** `core` on `DeviceInner` (safe drop order, sibling field for split-borrow).
   - `DrawCache` holds `dirty: bool`, `key_gen: u64` (NEVER reset by `clear()`! Monotonic, wrapping past 0), `cached: Option<ResolvedCoreDraw>`.
   - `ResolvedCoreDraw` holds `point_list: bool`, `streams: [StreamKey; MAX_STREAMS as usize]`, `target_gen: u64`, `key: PipelineKey`, `bytes: Vec<u8>`, `bind_ranges: Vec<(Stage, u8, Range<usize>)>`, `textures: Vec<TextureBind>`, `sampled_pixel_mask: u16`, `sampled_vertex_mask: u8`, `cached_pixel_lods: [u32; 16]`, `cached_vertex_lods: [u32; 4]`, and precomputed `DrawData` fields (`viewport`, `scissor`, `fill_mode`, `cull_mode`, `slope_scale`, `depth_stencil`, `stencil_ref`, `blend_color`). No `draw_flags`; refusals leave `dirty = true` / `cached = None`.
   - `any_bound_texture_dirty_or_lod_changed(dev, cached)` checks over set bits of `sampled_pixel_mask` / `sampled_vertex_mask`:
     - `TextureInner::has_any_dirty(&self)` (`self.dirty_mask != 0 || self.cube.as_deref().is_some_and(|c| c.dirty_masks.iter().any(|&m| m != 0))`) (R1 C1)
     - `tex.inner().lod() != cached_lod` (R1 C2 — catches cross-device `SetLOD` with zero statics!)
   - **Invalidation sites (`invalidate_core_draw()` sets `dirty = true`; `clear()` sets `cached = None; dirty = true` preserving `key_gen`)**:
     - `mark_snapshot_dirty`: calls `self.invalidate_core_draw()` **unconditionally** (even if `ff_aware_mask` strips bits to empty: R3 1.2, R1 I7).
     - `mark_snapshot_dirty_all`: calls `self.core_draw_cache.clear()` (covers `StateBlock::Apply`, `Reset`, `UnlockRect`/`UnlockBox`/`AddDirtyRect`/`ReleaseDC`, `ColorFill`, `UpdateSurface`, `UpdateTexture`: R1 C3, R3 1.2).
     - Explicit `invalidate_core_draw()` at every `is_core()` bypass: `set_viewport`, `SetRenderTarget(1..=3)`, `SetDepthStencilSurface` core arm, `set_vertex_texture_slot`, `set_vertex_sampler_slot_state`, `propagate_vs_const_delta`/`propagate_ps_const_delta`, `texture_set_lod` (pixel + vertex), `UpdateTexture` on `this` device.
     - Explicit `core_draw_cache.clear()` in `device_set_texture` (pixel & vertex), `evict_managed_resources` (before `is_core()` return), `reset_to_defaults`, `rebind_after_extended_reset`, `device_release` (before `core.take()`), and `finalize_texture`/`finalize_surface` when `core.forget_texture` runs (R1 C3).
     - Do **NOT** invalidate in `SetStreamSource`, `SetStreamSourceFreq`, `SetIndices`, `reset_stream0()`, or `reset_indices()` (`cached.streams == *streams` already covers them: R1 I5, R3 1.4).
   - **Zero-allocation `record()` & bitmask stream loops**:
     - Fill stack array `[Bind<'_>; 16]` instead of `binds.binds()` `Vec` (R1 I6, R3 1.4).
     - In `bound_streams` / `flush`, iterate only set bits of `declared` (`while mask != 0 { ... }`), and cache `core_created: bool` on `VertexBufferInner`/`IndexBufferInner` (cleared on `forget_buffer`/eviction) to avoid `core.has_buffer` hash lookup on clean non-WRITEONLY buffers (R3 1.4).

## Tasks

### Task 1: Host RED tests in `d3d9-rt` (`src/d3d9/rt/src/device/draw/tests.rs` & `buffers/tests.rs`)
Add comprehensive host tests (strictly in `tests.rs` submodules, never inline in `.rs`, per `tools/audit.sh:196`) covering:
1. Same-state draws in one pass deduplicate PSO, DSSO, blend color, viewport, scissor, rasterizer, vertex buffer, constant bytes, textures, and samplers.
2. Actual `dxmt-core` `optimize()` render-pass merge safety (`draw -> blit/close_pass -> draw` merges into 1 encoder with 2 full sets of state/binds) AND toggling `with_depth` / color format between draws using the same vertex buffer re-emits `bind_buffer` (R2 C1).
3. Helper passes (`clear_rects`, `stretch`, `depth_resolve`) between identical draws re-emit all state.
4. `dynamic_lock(DISCARD)` rebinds vertex buffer AND index buffer in the same open pass; `D3DLOCK_NOOVERWRITE` fast-path returns cached pointer without rename; `forget_buffer` + recreate same ID rebinds.
5. `StreamData::Bytes` (UP) <-> `StreamData::Buffer` slot cross-invalidation, crossing-fetch (`advance != 0`) for both Buffer and Bytes, and `IndexData::Buffer` / `IndexData::Bytes` draws.
6. Constant byte mutation (same length different bytes, different length) on vertex & pixel stages.
7. Independent vertex & pixel texture/view/sampler changes (sRGB view flip, sampler swap including `None`), plus `tb-self` self-sampling tile barrier (`render 41`) with deduped PSO recovery.
8. Occlusion queries (`bump_visibility_result_offset`) on all compact fast-path closures (non-indexed, stored indexed, UP).
9. `PsoMemo` (`key_gen` O(1) hit, MRU hit refreshing `key_gen`, `key_gen == 0` bypass, >24 eviction, refused layout not cached) and `target_gen` bumps on every mutator.
Show the new dedup tests RED before implementing Layer 1.

### Task 2: Implement Layer 1 in `d3d9-rt`
Implement `PassBound`, `PsoMemo`, `target_gen`, `BufferStore` monotonic `generation` + `NOOVERWRITE` cached pointer + inline `last_use` stamping, 3-phase `CoreDevice::draw`, and compact fast-path closures. Update `buffers/tests.rs:770-784` (`a_nooverwrite_lock_between_draws_records_nothing`) for deduped `binds.len() == 1` (R1 I1, R2 I4). Verify `git add` new files, all host tests GREEN, and `tools/rust-check.sh` GREEN. Commit Layer 1.

### Task 3: Implement Layer 2 in `d3d9`
Implement `CoreDrawCache` (`DrawCache`, `ResolvedCoreDraw`, `TextureInner::has_any_dirty`, LOD check, stack `[Bind; 16]`, set-bit stream loops, `core_created` buffer flag) and wire every invalidation/clear site. Verify `tools/rust-check.sh` GREEN. Commit Layer 2.

### Task 4: Gates & Post-Dedup A/B Probe (`build/c4d/s1/gates/`)
Copy/fix gate scripts from `s7d2` & `s0` with all R3 gate fixes (`SLICE=s1`, seeded `base-key.txt` and `base-imports.txt`, `PARITY_ROWS:-10`, `DXMT_WMTCMD_HISTOGRAM=$G/...` never `=1`, merged `jh.sh` render-41 histogram check in `jt1`, `MN_STATE=$B/vm-work/state/c4d-job.img` + VM stop in `jc1` and its trap, propagated exit codes, `memory_pressure` logged, quiet-load settle + `tex buf dyn` sample + `wineserver` cleanup in `jp_c4d1`):
1. `jt1`: `DXMT_D3D9_CORE=1` build, `d3d9-check.sh` default & core, `JTVAL=parity` validation + `render 41` histogram.
2. `jl1`: full conformance default & core + `d3d9-conf-compare.sh --base-lane default --cur-lane core` (0 validation reports, empty `mtld3d-triaged-core.txt`).
3. `jq1`: 16 launches under core + Metal validation (0 reports).
4. `jc1`: VM D3D9 gate default & core, 64- & 32-bit, old & new WoW64; builder VM stopped.
5. `jp_c4d1`: one-window A/B probe (`91a0fc4c` vs C4d-1 tip on `tex`, `buf`, `dyn`, `core` vs `mtld3d`, 2 rounds) + `/usr/bin/sample` on `tex`, `buf`, `dyn`.
6. `jr1`: plain build (`DXMT_D3D9_CORE` unset), translator key & `d3d9.dll` imports unchanged, default conformance green.

### Task 5: Final Review, Docs/Spec/Ledger, Pin, Push, Post-Push
Fresh adversarial review of full C4d-1 diff; update spec & ledger; privacy scan (`~`); restore `$D` to `dxmt.applied`; push DXMTRS `c4d` -> `main` and MacNeutron `c4d-mn` -> `main`; run `post.sh` (`X64_BASE=91a0fc4c`); verify CI green.

## Rules check
- **New `static`**: none (cross-device `SetLOD` checked by comparing cached LODs against `tex.inner().lod()`, zero statics).
- **New config key / env var / wire field / dependency**: none.
- **New `Clone` / `Copy` derive**: none (`tools/derive_inventory.txt` stays untouched!).
- **New lint suppression / `pub(crate)` / `mod.rs` / glob import / type alias / inline `mod tests` / em dash**: none.
- **Outside `src/d3d9/`**: none (`Chunk::allocate_cpu` only used if already `pub`, else inline `[u8; 256]` keeps zero changes outside `src/d3d9/`).

## Execution notes

- Landed 2026-11-20 at DXMTRS `main` `ff3c5c5f6091f71512c099cfdcfdc0cbc74d7790` (initial implementation `27a3d515`, review fix `ff3c5c5f`). Strictly inside `src/d3d9/` (18 files, zero changes outside `src/d3d9/`, `tools/derive_inventory.txt` untouched, translator key unchanged).
- Three plan reviews folded (`build/c4d/s1/review-{1-design,2-core,3-exec}.md`) and fresh adversarial final review completed (`build/c4d/s1/review-final.md`: 0 Critical, 1 Important `last_draw_gen` clear on `key_gen == 0` bypass draws + regression test, 2 Minor items all resolved in `ff3c5c5f`).
- Gates:
  - `LC_ALL=C sh tools/rust-check.sh`: fmt, gen, clippy, doc, test (246 `d3d9-rt` + workspace tests), audit (1271 files) all green.
  - `jt1` (`27a3d515`) & `jt2` (`ff3c5c5f`): exit 0 (`d3d9-default` ok, `d3d9-core` ok, `parity` Metal validation 0 reports, `render 41 = 1`, `was not recorded = 0`, `gamma ramp = 1`).
  - `jl1` (`27a3d515`): exit 0 (default & core Wine conformance; `core validation reports: 0`, `conf-compare: ok`, `triaged-core lines: 0`).
  - `jq1` (`27a3d515`): `d3d9-default`, `d3d9-msl`, `d3d9-core` all ok, 16/16 Metal validation launches `reports 0`, `not ok 0`, exact row counts.
  - `jc1` (`27a3d515`): exit 0 (VM D3D9 gate default & core across old & new WoW64, x64 & x86; builder VM stopped).
  - `jr3` (`ff3c5c5f`, plain build): translator key & `d3d9.dll` imports identical to base, `d3d9-default` ok, `air kinds 3 files`, `visual` 30/211219, `stateblock` 0/14738 (3 default-lane focus/cursor sites under display sleep).
  - `jp_c4d1` (`91a0fc4c` vs `ff3c5c5f`, quiet Mac, `build/c4d/s1/probe.md`): core net CPU ms/frame dropped 41% to 49% (`tex` 3.87 -> 1.97 vs mtld3d 2.00; `buf` 3.38 -> 1.90 vs mtld3d 1.63; `dyn` 3.72 -> 2.18 vs mtld3d 2.17 in Round 2; 2400-frame core net CPU 1.51 / 1.45 / 1.74 ms/f); `_MTLRenderCommandEncoder_encodeCommands` collapsed to 1.1% to 2.3% of wall time on `dxmt-encode-thr`.
