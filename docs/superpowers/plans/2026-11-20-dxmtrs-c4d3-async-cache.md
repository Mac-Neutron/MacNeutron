# DXMTRS C4d-3 Implementation Plan: Async Compile, Skipped Draws, Prewarm & DXMT SQLite Shader Cache (Review-Folded)

**Date:** 2026-11-20
**Status:** Finalized after 3 independent adversarial reviews (`review-1-scheduler-cache.md`, `review-2-d3d9rt-prewarm.md`, `review-3-tests-build-audit.md`).
**Scope:** Full compilation parity for the core D3D9 runtime (`d3d9-rt` on `dxmt-core`):
1. Multi-lane priority (`Urgent`, `Pipeline`, `Normal`), atomic ticket reservation, `promote`, `run_urgent_or_wait` (steal + continuation wait + RAII `in_flight` guard), `cancel_unstarted`, `Weak` `SCHEDULERS` registry, and non-blocking `Compiled::try_ready()` + `ready_or_steal()` in `dxmt-core`.
2. `Encoder::dispatch_tile_barrier` safety guard (`if recover_pso != 0`) so deferred/failed draws never emit `SetPSO(0)`.
3. Shared DXMT SQLite shader cache (`shaders_<metal_version>.db`, exact same `cache_<version>` table as D3D11) in `d3d9-rt` via fail-closed `build.rs`, single-mutex append-only `StageFunctions` (zero TOCTOU/UAF), domain-tagged 40-byte keys, `winemetal_sys::Pool::new()` on worker/prewarm steps, and non-blocking `try_close_all()` in `d3d9::dll_main` gated on `RUNTIME == Core` + cache-opened `AtomicBool`.
4. Generalized `ClearHistory<K = MetalHandle<MTLTextureKind>>`, `mark_kept_reads_generic`, `DepthStencilSnapshot::writes_stencil()`, and `DeclCode::elements_with_end()` in `mtld3d-core`.
5. Asynchronous pipeline compile, two-frame regenerated-target draw skipping, application-endpoint `note_stretch_copy` (no transient-scratch poisoning), whole-target `fill`/`clear` recording, hidden/headless `present()` frame advancement, `PassBound` soundness on deferred/failed draws, deterministic `CoreDevice::drop` teardown, and deduplicated SQLite-backed `"D3D9REC1"` pipeline recipe prewarm with union-merge persistence.
6. In-memory `CacheReader_*` / `CacheWriter_*` / `DispatchData_copyBytes` fakes in `dxmt-fakes`, `src/d3d9/rt` added to `PE_CRATES` in `tools/audit.sh`, `Cargo.lock` regenerated, derive inventory updated, and full Mac + VM gate coverage (strict `shader.asyncCompile=false`, async `shader.asyncCompile=true`, isolated `DXMT_SHADER_CACHE_PATH` / `MN_WM_CACHE_DIR`, cold/warm cache-hit assertions, and real shared-DB D3D9 + D3D11 single-table coexistence test).

**Leaves `src/d3d9/`?** Yes (`src/dxmt/src/scheduler.rs`, `src/dxmt/src/pipeline_cache.rs`, `src/dxmt/src/shader_cache.rs`, `src/dxmt/src/shader_cache/instance.rs`, `src/dxmt/src/encoder/gaps.rs`, `src/dxmt/fakes/src/lib.rs`, `tools/audit.sh`, `tools/derive_inventory.txt`, `Cargo.lock`). Requires bounded idle Mac x64 D3D10-12 check (`jx1`, never between `02:55` and `03:36`) and VM D3D10-12 gate (`JC_D3D10=1`).

**Zero winemetal / airconv changes:** `CacheReader_{alloc_init,get}`, `CacheWriter_{alloc_init,set}`, `DispatchData_{alloc_init,copyBytes}`, and `MTLDevice_newRenderPipelineStateWithVertexDescriptor` already exist in `winemetal` and `winemetal-sys`. `airconv` is untouched (`base-key.txt` stays unchanged).

---

## Task 1: `dxmt-core` Scheduler, Pipeline Cache, Non-Blocking Cache Detach, Barrier Guard & `dxmt-fakes`

### Files
- `src/dxmt/src/scheduler.rs` & `src/dxmt/src/scheduler/tests.rs`
- `src/dxmt/src/pipeline_cache.rs` & `src/dxmt/src/pipeline_cache/tests.rs`
- `src/dxmt/src/shader_cache.rs` & `src/dxmt/src/shader_cache/instance.rs`
- `src/dxmt/src/encoder/gaps.rs`
- `src/dxmt/fakes/src/lib.rs` & `src/dxmt/fakes/tests/capture.rs`

### Design
1. **`src/dxmt/src/scheduler.rs`** (preserve ALL existing D3D11/D3D10 signatures, `pub type Scheduler = CompileScheduler;`, `pub use wait_idle as wait_schedulers_idle;`, zero `println!`, zero `.join()`):
   - **Fix `SCHEDULERS` UAF:** Change `SCHEDULERS` from `Mutex<Vec<usize>>` to `LazyLock<Mutex<Vec<Weak<SchedulerInner>>>>`. In `wait_idle`, clone live `Weak`s / upgrade to `Arc<SchedulerInner>` (dropping dead `Weak`s) without any `unsafe` pointer deref; in `Drop`, retain only entries where `w.as_ptr() != Arc::as_ptr(&self.inner)` and `w.strong_count() > 0`.
   - **3-lane priority queue + atomic ticket reservation:**
     - `#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)] pub struct JobTicket(u64);` (non-zero; `next_ticket` uses `AtomicU64` starting at `1`, skipping `0` on wrap).
     - `#[derive(Clone, Copy, PartialEq, Eq, Debug)] pub enum CompileLane { Urgent, Pipeline, Normal }`
     - `QueueState` holds `urgent`, `pipelines`, `normal: VecDeque<(JobTicket, CompileJob)>`, `in_flight: usize`, `idle: usize`, `worker_count: usize`, `stop: bool`.
     - Helper methods on `QueueState`: `total_queued(&self) -> usize`, `is_empty(&self) -> bool`, `pop(&mut self) -> Option<(CompileLane, JobTicket, CompileJob)>`, `push(&mut self, lane: CompileLane, ticket: JobTicket, job: CompileJob)`.
     - Every check (`wait_idle`, worker wait condition, worker exit condition `st.stop && st.is_empty()`, idle notification `st.is_empty() && st.in_flight == 0`, worker demand scaling `st.total_queued() + st.in_flight > st.worker_count`) uses ALL three lanes.
     - Worker loop tracks `st.idle += 1` before `work_cv.wait` and `st.idle -= 1` immediately on wake.
     - Worker continuations (`if let Some(dep) = continuation`) re-queue into the **popped lane** (`dep` with a fresh non-zero ticket if `!dep.is_done()`, then `job` keeping its original `ticket`).
     - Wrap each `job.run_step()` in `InFlightGuard` (RAII `Drop` decrements `in_flight` and notifies `idle_cv` when `st.is_empty() && st.in_flight == 0` if unwinding in host tests, or disarms on normal completion path that already holds `st`).
     - Add methods on `CompileScheduler`:
       - `pub fn reserve_ticket(&self) -> JobTicket` (allocates non-zero `JobTicket` atomically on `SchedulerInner` before `PsoMap` inserts!).
       - `pub fn submit_lane_with_ticket(&self, ticket: JobTicket, job: CompileJob, lane: CompileLane)`
       - `pub fn submit_lane(&self, job: CompileJob, lane: CompileLane) -> JobTicket`
       - `pub fn promote(&self, ticket: JobTicket) -> bool`: moves unstarted `ticket` from `pipelines`/`normal` to back of `urgent` (returns `true` if in `urgent`, `false` if already running/done).
       - `pub fn cancel_unstarted(&self)`: clears `urgent`, `pipelines`, and `normal` under lock, notifies `idle_cv` if `in_flight == 0` (prevents skipped-draw backlog from stalling `CoreDevice::drop`).
       - `pub fn run_urgent_or_wait(&self, ticket: Option<JobTicket>, job: &CompileJob, share_with_idle: bool)`:
         - Fast path: `if job.is_done() { return; }`.
         - Under a SINGLE lock acquisition of `inner.state`:
           1. Find unstarted job by `ticket` (or by `Arc::ptr_eq(&queued.inner, &job.inner)` if `ticket` is `None`).
           2. If found in `pipelines`/`normal`, move to back of `urgent`.
           3. Look up its position in `urgent`. If `share_with_idle && pos < st.idle`, leave it for the idle worker (release lock, call `job.wait()`).
           4. Otherwise remove `(stolen_ticket, stolen_job)` from `urgent` and increment `st.in_flight += 1` BEFORE dropping `st`!
         - Outside the lock (guarded by `InFlightGuard`): call `let continuation = stolen_job.run_step();`.
         - Re-lock `st`, disarm guard, `st.in_flight -= 1`:
           - If `let Some(dep) = continuation`: push `dep` (fresh ticket if `!dep.is_done()`) and `stolen_job` (keeping `stolen_ticket`) into `urgent`, `work_cv.notify_all()`, drop `st`, and **call `job.wait()`** (never return while `!job.is_done()`!).
           - Else if `st.is_empty() && st.in_flight == 0`: `idle_cv.notify_all()`.
     - Replace `thread::yield_now()` in `CompileScheduler::drop` with `dxmt_util::sync::wait_until_finished(h)` (or `thread::sleep(Duration::from_millis(1))` loop while `!h.is_finished()`, avoiding 100% CPU busy-spin on slow compiles; verify `dxmt_util::sync` exports or use `1ms` sleep matching `queue.rs:607`).
2. **`src/dxmt/src/pipeline_cache.rs`** (keep `Compiled::handle` field name & all existing APIs):
   - Add `#[derive(PartialEq, Eq, Debug)] pub enum CompileState { Pending, Ready(u64), Failed }` (omit `Clone`/`Copy` unless needed).
   - Add `state: AtomicU8` (`0 = PENDING`, `1 = READY`, `2 = FAILED`) and `ticket: AtomicU64` (`0` = none) to `Compiled<S>`.
   - `Compiled::pending`: initializes `state = PENDING`, `ticket = 0`. Inside compile closure, wrap `compile(&compiled.spec)` in `let _pool = winemetal_sys::Pool::new();`, then write in strict order:
     1. `*lock(&compiled.pso) = pso;`
     2. `compiled.handle.store(handle, Ordering::Release);`
     3. `compiled.state.store(if handle != 0 { STATE_READY } else { STATE_FAILED }, Ordering::Release);`
     (Keep `job` intact: `CompileJob::run_step` already drops `step_fn` and captures upon completion.)
   - `Compiled::ready_with`: initializes `state = if handle != 0 { STATE_READY } else { STATE_FAILED }`, `ticket = 0`.
   - `Compiled::try_ready(&self) -> CompileState`: `Acquire` load on `state`, returning `Ready(self.handle.load(Ordering::Acquire))`, `Failed`, or `Pending`.
   - `Compiled::ready(&self) -> Option<u64>`: fast-path `STATE_READY` and `STATE_FAILED` without locking `self.job`.
   - `Compiled::ticket(&self) -> Option<JobTicket>`: `NonZero` check on `self.ticket.load(Ordering::Acquire)`.
   - `Compiled::ready_or_steal(&self, scheduler: Option<&CompileScheduler>) -> Option<u64>`:
     - Fast path `match self.try_ready()`: `Ready(h) => Some(h)`, `Failed => None`, `Pending => { ... }`.
     - Clone `job = lock(&self.job).clone()`. If `Some(job)`: if `let Some(sched) = scheduler`, call `sched.run_urgent_or_wait(self.ticket(), &job, false)` else `job.wait()`.
     - Re-read `match self.try_ready() { CompileState::Ready(h) => Some(h), _ => None }`.
   - `PsoMap::insert_or_existing_with_lane`:
     - Keep <= 5 args (under `clippy::too_many_arguments` limit of 7): e.g. `insert_or_existing_lane(&self, key: K, entry: Arc<Compiled<S>>, scheduler: &OnceLock<CompileScheduler>, init: impl FnOnce() -> CompileScheduler, lane: CompileLane) -> Arc<Compiled<S>>`.
     - If `entry` has a job, initialize `let sched = scheduler.get_or_init(init);` and pre-reserve `let ticket = sched.reserve_ticket();` (stored in `entry.ticket` with `Release`) BEFORE inserting into `self.map` (only init scheduler if the key is actually a miss so cache hits don't start workers: check `map.get(&key)` -> if hit, return existing; on miss with `job`, call `let sched = scheduler.get_or_init(init); let ticket = sched.reserve_ticket(); entry.ticket.store(ticket.raw(), Ordering::Release); map.insert(key, Arc::clone(&entry)); drop(map); sched.submit_lane_with_ticket(ticket, job, lane);`, which has ZERO lock-order hazard because `get_or_init` never acquires `self.map`, AND `entry.ticket` is already visible to any concurrent reader the instant `map.insert` publishes `entry`!).
3. **`src/dxmt/src/shader_cache.rs` & `instance.rs`**:
   - Add `Store::try_close(&self)` using `self.reader.try_lock()` and `self.writer.try_lock()` (drops reader first, then writer; if `WouldBlock`, skips without hanging).
   - Add `pub fn try_close_all()` in `instance.rs` using `CACHES.try_lock()` -> calls `store.try_close()` on each store without blocking (safe under Windows/Wine `DLL_PROCESS_DETACH` loader lock after `ExitProcess` kills worker threads!).
4. **`src/dxmt/src/encoder/gaps.rs`**:
   - Guard line ~1512 inside `dispatch_tile_barrier`: only emit recovery `WmtcmdRenderSetpso { pso: recover_pso }` when `recover_pso != 0`, preventing `[MTLRenderCommandEncoder setRenderPipelineState:nil]` if a pass triggers a tile barrier before any concrete PSO is bound (e.g. a failed deferred draw).
5. **`src/dxmt/fakes/src/lib.rs`**:
   - Store byte payloads (`dispatch_data: BTreeMap<u64, Vec<u8>>`) in `fake_dispatch_data` (`DispatchData_alloc_init`) and export `DispatchData_copyBytes` (matching `src/winemetal/unix/src/g1.rs:636-664`: null `buffer` returns full byte length `data.len() as u64`, non-null copies `min(len, data.len())` and returns bytes copied).
   - Add in-memory cache store (`cache_dbs: BTreeMap<String, BTreeMap<Vec<u8>, Vec<u8>>>`, reader/writer handle -> path) and export `CacheReader_alloc_init`, `CacheReader_get`, `CacheWriter_alloc_init`, `CacheWriter_set` (plus helper `reset_shader_cache()` for clean isolated host tests).

---

## Task 2: `mtld3d-core` Shared Primitives (`ClearHistory<K>`, `DepthStencilSnapshot::writes_stencil`, `DeclCode::elements_with_end`)

### Files
- `src/d3d9/core/src/async_compile.rs` & `tests.rs`
- `src/d3d9/core/src/depth_stencil_state.rs`
- `src/d3d9/core/src/passes.rs`
- `src/d3d9/core/src/pipeline_key.rs`

### Design
1. **`async_compile.rs`**:
   - Generalize `ClearHistory<K = MetalHandle<MTLTextureKind>>` with generic methods for `K: Copy + Eq + Hash`: `new`, `is_empty`, `begin_frame`, `record_key`, `regenerated`, `tracks`, `mark_feeds_persistent_key`, `feeds_persistent`, `forget`, `clear`.
   - Keep `impl ClearHistory<MetalHandle<MTLTextureKind>>` methods `record` and `mark_feeds_persistent` filtering `texture.is_null()` so `mtld3d-unix` needs zero churn.
   - Add `pub fn mark_kept_reads_generic<K: Copy + Eq + Hash>(reads: &[(usize, K)], history: &mut ClearHistory<K>, mut pass_is_kept: impl FnMut(&ClearHistory<K>, usize) -> bool)` and delegate `mark_kept_reads` to it.
2. **`depth_stencil_state.rs` & `passes.rs`**:
   - Add `pub const fn writes_stencil(&self) -> bool` on `DepthStencilSnapshot` (matching `passes.rs:6359-6369`: `stencil_enable != 0 && (front/back write mask != 0 and any op != KEEP)`), and have `passes.rs::draw_writes_stencil` delegate to it.
3. **`pipeline_key.rs`**:
   - Add `pub fn elements_with_end(&self) -> &[D3DVERTEXELEMENT9]` on `DeclCode` returning `&self.0` (including the exact terminator element verbatim) so prewarm recipe serialization round-trips canonical and non-canonical `D3DDECL_END` identically.

---

## Task 3: `d3d9-rt` SQLite Shader Cache, Fail-Closed `build.rs`, Single-Mutex `StageFunctions`, & Recipe Prewarm

### Files
- `src/d3d9/rt/build.rs` (new)
- `src/d3d9/rt/Cargo.toml` (add `dxmt-util = { workspace = true }`)
- `Cargo.lock` (regenerated via `cargo generate-lockfile` / `cargo metadata` for `--locked --offline` builds!)
- `src/d3d9/rt/src/lib.rs` (expose `close_shader_cache_on_detach()`)
- `src/d3d9/rt/src/device/shader_cache.rs` (new) & `src/d3d9/rt/src/device/shader_cache/tests.rs` (new)
- `src/d3d9/src/lib.rs` (gated detach call)

### Design
1. **Fail-closed `src/d3d9/rt/build.rs`** (matching `src/d3d12/build.rs`):
   - Emit `cargo:rerun-if-env-changed=DXMT_TRANSLATOR_KEY_H` and `DXMT_VERSION_H`.
   - If `DXMT_TRANSLATOR_KEY_H` is unset/empty, emit `DXMT_TRANSLATOR_KEY=""` (host cargo test/clippy); if set, emit `cargo:rerun-if-changed=...` and **panic** if the file is missing or lacks `#define DXMT_TRANSLATOR_KEY "..."`.
   - Same fail-closed handling for `DXMT_VERSION_H` (fallback `"dev"` only when unset/empty).
2. **`shader_cache.rs`**:
   - `pub const fn shader_cache_version() -> u64`: `dxmt_core::shader_cache::version(key_or_ver, AIRCONV_VERSION)` (exact byte-for-byte match with `d3d11` and `d3d12`).
   - `CACHE_OPENED: AtomicBool = AtomicBool::new(false)`: set to `true` only when `open_store` opens `dxmt_core::shader_cache::instance(metal_version, shader_cache_version())`.
   - `pub fn close_shader_cache_on_detach()`: if `CACHE_OPENED.load(Ordering::Acquire)`, calls `dxmt_core::shader_cache::try_close_all()` (zero work and zero `LazyLock` init if cache was never opened; never deadlocks on killed workers under `ExitProcess`).
   - Log `DXMT_TRANSLATOR_KEY` / cache version at `gate_level()` when opening the cache so `d3d9.dll` retains the key under fat LTO (for `vm/dxmt/d3d9-gate.sh` and diagnostics).
   - **Domain-tagged 40-byte stage keys** (`stage_cache_key`):
     - `[0..20]` = `Sha1` of `b"d3d9-air-v1"` + stage byte + `spec.to_wire()` + DXSO tokens LE.
     - `[20..40]` = `Sha1` of entry name + `metal_version.to_le_bytes()` + `[D3D9_VS_WIRE as u8, D3D9_PS_WIRE as u8]`.
     - (Note: `airconv::dxso` compiles to AIR 2.6 / Metal 3.1 regardless of `common.metal_version`; pass `SM50_SHADER_METAL_310` cleanly without modifying `airconv`.)
   - **Single-mutex append-only `StageFunctions`** (ZERO TOCTOU, ZERO UAF):
     - `enum StageEntry { Compiling(Arc<StageGate>), Done(Option<Obj>) }`
     - `struct StageFunctions { table: Mutex<FxHashMap<StageKey, StageEntry>>, cache: Option<Arc<Store>>, stats: CompileStats }`
     - Loop: lock `table`:
       - `Some(StageEntry::Done(opt))` -> return `opt.as_ref().map(Obj::handle)` (table is strictly append-only; `Done` entries are NEVER overwritten or dropped while `StageFunctions` lives!).
       - `Some(StageEntry::Compiling(gate))` -> clone `gate`, drop `table`, wait on `gate`, continue loop.
       - `None` -> insert `StageEntry::Compiling(Arc::clone(&gate))`, drop `table`, compile outside lock with a `GateGuard` that guarantees cleanup on early return/unwind.
     - Outside lock: wrap compilation in `let _pool = winemetal_sys::Pool::new();`:
       1. If `store.has_reader()` -> read `&cache_key` from SQLite:
          - Hit + `load_function` ok -> `stats.cache_hits.fetch_add(1, Relaxed)`, return `Some(fn_obj)`.
          - Hit + load error -> log warning, fall through to miss.
          - Miss -> `stats.cache_misses.fetch_add(1, Relaxed)`.
       2. Compile via `initialize_dxso` -> `compile` -> `compiled_data` -> if `store.has_writer()`, `store.write(&cache_key, &data)` -> `load_function` -> `stats.stage_compiles.fetch_add(1, Relaxed)`.
     - Under `table` lock: replace `Compiling` with `Done(result)` (never overwrite an existing `Done`), drop `table`, mark gate done & `notify_all()`.
   - **Deduplicated `"D3D9REC1"` pipeline recipe store (`RecipeStore`)**:
     - Key `[u8; 40]`: `[0..20] = Sha1(b"d3d9-recipes-v1")`, `[20..40] = Sha1(metal_version.to_le_bytes())`.
     - Binary format `"D3D9REC1"` (explicit little-endian, zero zstd, zero struct-memory dumps):
       - Header: `b"D3D9REC1"` (8 B) + `shader_count: u32` + `decl_count: u32` + `recipe_count: u32` (bounded by `MAX_RECIPES = 4096`, total blob read cap `MAX_RECIPE_BLOB = 64 * 1024 * 1024`).
       - `shaders`: `[len_u32: u32, tokens: [u32; len]]*` -> decoded into `Vec<ShaderCode>` (`ShaderCode::from_parts(ProgramId::from_tokens(&toks), Arc::from(toks))`, sharing one `Arc` across all variants!).
       - `decls`: `[len_u32: u32, elements_with_end: [D3DVERTEXELEMENT9; len]]*` -> decoded via `DeclCode::new(elems)` preserving exact `with_end` terminator verbatim!
       - `recipes`: explicit field-by-field encoding of `PipelineKey` (`vs` referencing shader index or `FfVsKey`, `ps` referencing shader index or `FfPsKey`, `decl` index or `None`, `streams`, `color_format`, `write_mask`, `blend`, `extras`, `depth_format`, `samples`, `alpha_to_coverage`).
       - Corrupt/truncated/unknown-version bytes return empty `Vec` with one warning log (never panic).
     - `DispatchData_copyBytes` 2-call readback: first call `(h, null, 0)` returns full length `n`, validate `n <= MAX_RECIPE_BLOB`, `usize::try_from(n)`, allocate `Vec<u8>`, second call copies bytes.
     - `RecipeRecorder`: seeded from decoded recipes (tracks `recipes_loaded`), records live built `PipelineKey`s (marking `dirty` only when a **new** key is inserted, up to `MAX_RECIPES`). On `CoreDevice::drop` (after workers & prewarm stopped), if `dirty` and `store.has_writer()`, re-reads any existing blob from `store` and writes `existing ∪ recorded` (capped at `MAX_RECIPES`), avoiding warm-run wipeout!
3. **`src/d3d9/src/lib.rs`**:
   - Inside `dll_main` (`reason == DLL_PROCESS_DETACH && USED.load(Ordering::Relaxed)`), before `TerminateProcess(proc, status)`:
     ```rust
     #[cfg(feature = "core-runtime")]
     if RUNTIME.get() == Some(RuntimeMode::Core) {
         d3d9_rt::close_shader_cache_on_detach();
     }
     ```
     (Check `RuntimeLatch::get()` in `mtld3d_core::runtime_latch` and add `pub fn get(&self) -> Option<RuntimeMode>` if not already exposed.)

---

## Task 4: `d3d9-rt` Async Compile, Draw Skipping, Deferred Encode-Time PSO, `PassBound` Soundness & Prewarm Lifecycle

### Files
- `src/d3d9/rt/src/device.rs`, `draw.rs`, `recorder.rs`, `depth.rs`, `textures.rs`, `swapchain.rs`, `sync.rs`
- `src/d3d9/src/direct3d9.rs`, `src/d3d9/src/device.rs`, `src/d3d9/src/device/core_arms.rs`
- `src/d3d9/rt/src/device/tests.rs` & `src/d3d9/rt/src/device/draw/tests.rs`

### Design
1. **`CoreOptions` bitflags (do NOT add bare `bool` fields to `CoreDevice`; already at 3 bools!)**:
   - `CoreOptions`: `SHADER_CACHE = 1 << 0`, `ASYNC_COMPILE = 1 << 1`, `DISCARD_BACK_BUFFER = 1 << 2`, `LOGGED_SKIP = 1 << 3`, `LOGGED_DEFERRED_FAIL = 1 << 4` (per-device log-once flags, zero global statics, no `mtld3d-shared` dependency).
   - Keep `CoreDevice::new(mtl, profile)` defaulting to `CoreOptions::empty()` (existing unit tests stay synchronous/deterministic). Add `CoreDevice::with_options(mtl, profile, options)`.
   - `create_core_device` passes `SHADER_CACHE` (`cfg.shader_cache_enable`), `ASYNC_COMPILE` (`cfg.shader_async_compile`), and `DISCARD_BACK_BUFFER` (`pp.swap_effect == D3DSWAPEFFECT_DISCARD`).
   - On EVERY successful `Reset` / `ResetEx` in `src/d3d9/src/device.rs` (both `resized == true` and `resized == false`, strictly `#[cfg(feature = "core-runtime")]`): call `core.set_discard_back_buffer(stored.swap_effect == D3DSWAPEFFECT_DISCARD)` and `core.reset_clear_history()` (which clears BOTH `ClearHistory` and `PassReadTracker`).
   - On windowed auto-resize (`CoreDevice::resize()` in `swapchain.rs`), call `self.clear_history.forget(ClearTarget::BackBuffer)` (preserving other textures' streaks while not letting a freshly allocated non-discard back buffer inherit the old one's streak).
2. **`CompileContext`, `PrewarmHandle`, and strict `CoreDevice::drop` ordering**:
   - `CompileContext` (`Arc<CompileContext>`): retained `mtl_device: Obj` (`Obj::retain(device.device().handle())`), `metal_version: u32`, `functions: Arc<StageFunctions>`, `recipes: Arc<Mutex<RecipeRecorder>>`.
   - `CoreDevice` holds:
     - `pipelines: Arc<PsoMap<PipelineKey, Option<PipelineSource>>>`
     - `scheduler: Option<Arc<OnceLock<CompileScheduler>>>`
     - `prewarm: Option<PrewarmHandle>` (spawned as the **very last** step of `CoreDevice::with_options`, after all init, when `SHADER_CACHE` is enabled and `store.has_reader()` and `mtl.handle() > 0x1000` or test-enabled; uses `mtld3d_core::shader_prewarm::PrewarmHandle` and never calls `.join()` directly! Each prewarm iteration wraps in `let _pool = winemetal_sys::Pool::new();`, skips keys already in `pipelines`, and skips `insert_ready` if `build_pipeline` returns `None`).
   - Exact `CoreDevice::drop` sequence:
     1. `if let Some(mut prewarm) = self.prewarm.take() { prewarm.cancel_and_join(); }`
     2. `self.drain();` (executes any queued deferred draws on `dxmt-encode-thr`, and `finish()` calls `chunk.reset()`, dropping closure clones of `Arc<OnceLock<CompileScheduler>>`).
     3. `self.pso_memo.clear(); self.pipelines.clear();`
     4. If `let Some(sched_arc) = self.scheduler.take()`: if `let Some(s) = sched_arc.get() { s.cancel_unstarted(); }` then `drop(sched_arc)` (strong count is now 1 -> `CompileScheduler::drop` joins all workers cleanly BEFORE step 5 and BEFORE `device` drops!).
     5. Flush dirty recipes (`existing ∪ recorded`) to `Store` and log shader cache + compile summary line at `gate_level()` (with `cache_hits`, `cache_misses`, `stage_compiles`, `async_compiled`, `draws_skipped`, `recipes_loaded`, `recipes_saved`, `prewarmed_psos`, avoiding any substring matched by `d3d9-check.sh`'s error regex).
     6. Implicit field drops (`device: Device` drops last).
3. **`ClearHistory<ClearTarget>` & `PassReadTracker`**:
   - `#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)] pub enum ClearTarget { BackBuffer, Texture(TextureId) }`.
   - Uniform subresource key everywhere: `slice | (level << 16)` (color, depth, and stencil).
   - Whole-target clear recording:
     - `recorder.rs`: `CoreDevice::clear` (`rect.is_none()` or whole extent) & `CoreDevice::fill` (`rect.is_none()` or whole extent) record `ClearPlanes::COLOR`.
     - `depth.rs`: `CoreDevice::clear_depth` (`rect.is_none()` or whole extent) records `ClearPlanes::DEPTH` and/or `ClearPlanes::STENCIL` (masked to actual depth/stencil planes of the format).
   - Application-endpoint stretch/copy/resolve/RESZ tracking (ZERO transient-scratch poisoning):
     - Expose `pub fn note_stretch_copy(&mut self, src: TargetRef, dst: TargetRef, planes: ClearPlanes, whole_dst: bool)` on `CoreDevice` (plus depth-ref variant or `ClearTarget` + subresource).
     - Call ONCE from `core_arms::stretch_colour` (including `resolve_shape`), `core_arms::stretch_depth`, and `core_arms::resz` using the real application source & destination surfaces (not low-level `copy`/`stretch`/`resolve_into`/`copy_depth` which use transient scratches!).
   - Pass-read tracking:
     - Add `pub id: Option<TextureId>` to `TextureBind`, populated directly in `CoreDevice::texture_bind` (`textures.rs:321-354`; `None` in `black_bind`).
     - In `open_pass`: when `ASYNC_COMPILE` is set, push a `PassRecord` capturing ONLY the attached `pass_set()` color targets (`colors[slot] != 0`) and bound depth target (bounded by `MAX_TRACKED_PASSES = 4096`).
     - In `draw`: when `ASYNC_COMPILE` is set, update current pass `writes_stencil |= with_depth && has_stencil && draw.depth_stencil.writes_stencil()`, and inside `if !fast_state` when `should_bind_texture` returns `true` and `t.id` is `Some(id)`, push `(current_pass, ClearTarget::Texture(id))` (deduplicated per pass).
     - Keep `PassReadTracker` intact across mid-frame `CoreDevice::commit()`; evaluate `note_frame_reads()` (`mark_kept_reads_generic`) + clear tracker + `clear_history.begin_frame()` unconditionally on **BOTH** paths of `CoreDevice::present()` (including the hidden/headless early-return branch at `swapchain.rs:183-191`!).
   - `forget_texture(id)`: calls `self.clear_history.forget(ClearTarget::Texture(id))`.
   - `targets_rebuilt(&self, planes: ClearPlanes) -> bool`: checks ONLY attached `pass_set()` color slots (not mismatched `extras`) + depth/stencil planes requested by `planes` (`DEPTH` iff `with_depth && (depth_planes & 1 != 0) && draw.depth_stencil.uses_depth()`, `STENCIL` iff `with_depth && (depth_planes & 2 != 0) && draw.depth_stencil.stencil_enable != 0`).
4. **Async Draw Resolution & `PassBound` Soundness (`src/d3d9/rt/src/device/draw.rs`)**:
   - `PsoMemo`: stores ONLY `CompileState::Ready(pso)` entries (never pending or failed).
   - `resolve_pipeline`:
     - Hit in `PsoMemo` -> `ResolvedPso::Ready(pso)`.
     - Hit in `PsoMap`:
       - `CompileState::Ready(pso)` -> insert into `PsoMemo`, return `Ready(pso)`.
       - `CompileState::Failed` -> return `Err(Fetch)` if `refused_layout`, else `Err(Pipeline)`.
       - `CompileState::Pending` -> return `ResolvedPso::Pending(entry)`.
     - Miss:
       - Compute `source = pipeline_source(key)` synchronously (fast, pure Rust).
       - If `None` or `refused_layout`, `insert_ready` handle 0 and return `Err(Fetch | Pipeline)`.
       - If `!ASYNC_COMPILE`: compile synchronously (`build_pipeline`), `insert_ready`, record recipe on success, return `Ready(h)` or `Err(Pipeline)`.
       - If `ASYNC_COMPILE`:
         - Choose `CompileLane::Pipeline` if both VS and PS are already `Done` in `ctx.functions`, else `CompileLane::Normal`.
         - Build `Compiled::pending` closure running `build_pipeline(&ctx, src)` (recording recipe + incrementing `async_compiled` on success), submit via `insert_or_existing_lane(..., || CompileScheduler::with_bounds(2, 4), lane)`.
         - Match `entry.try_ready()` exhaustively: `Ready(h)` -> memoize & return `Ready(h)`; `Failed` -> return `Err(Pipeline)`; `Pending` -> return `ResolvedPso::Pending(entry)`.
   - Handling `ResolvedPso::Pending(compiled)` in `CoreDevice::draw`:
     - Validate target, depth, scissor, streams, and stored buffers FIRST (preserving exact `DrawRefused` priority).
     - Check skip predicate: `self.options.contains(CoreOptions::ASYNC_COMPILE) && self.open_brackets.is_none_or(|n| n == 0) && self.targets_rebuilt(planes)`.
     - If skippable -> increment `draws_skipped`, log once via `CoreOptions::LOGGED_SKIP` at `gate_level()`, return `Ok(())` without calling `open_pass` or mutating `pass_bound`.
     - If NOT skippable (persistent target, frame 1 non-discard, or active occlusion query):
       - Promote job to `Urgent` if `scheduler` + `ticket` present.
       - Call `open_pass`.
       - Force `fast_state = false`.
       - Run `if !fast_state { ... }` block (diffing `dsso`, `blend`, `viewport`, `scissor`, `rasterizer`, `binds`, `textures`, `samplers`).
       - **AFTER** `if !fast_state { ... }`: set `self.pass_bound.pso = 0; self.pass_bound.last_draw_gen = 0;` (so `draw.rs:728` NEVER overwrites it!).
       - Gate `no_state_changes` on `matches!(pso_emit, PsoEmit::None)` so deferred draws ALWAYS route through `emit_general` (never Closures A/B/C).
       - Inside `emit_general` on `dxmt-encode-thr`:
         1. Resolve `pso_handle` (`match &pso { PsoEmit::None => None, PsoEmit::Handle(h) => Some(*h), PsoEmit::Deferred(c, sched) => c.ready_or_steal(sched.get()) }`).
         2. Emit all non-PSO state deltas (`dsso`, `blend`, `viewport`, `scissor`, `rasterizer`, vertex buffers, constant binds, textures, samplers) UNCONDITIONALLY so Metal encoder state stays 100% synced with `PassBound`.
         3. If `let Some(h) = pso_handle`: emit `WmtcmdRenderSetpso { pso: h }` **BEFORE** `resolve_render_pass_barrier` (preserving `last_pso = h` for tile-barrier recovery!).
         4. If `!textures_emit.is_empty()`: ALWAYS run `enc.resolve_render_pass_barrier(Encoder::dispatch_tile_barrier)` (safe even when `last_pso == 0` thanks to Task 1's `if recover_pso != 0` guard in `gaps.rs`!).
         5. If `matches!(pso, PsoEmit::Deferred(..)) && pso_handle.is_none()`: compile failed, so return WITHOUT emitting `bump_visibility_result_offset()` or `WmtcmdRenderDraw*`!
         6. Otherwise emit `bump_visibility_result_offset()` and `WmtcmdRenderDraw*`!

---

## Task 5: Audit Updates, Tests, Mac/VM Gates & Documentation

### Files
- DXMTRS: `tools/audit.sh` (add `src/d3d9/rt` to `PE_CRATES`), `tools/derive_inventory.txt` (`--update-derives`), `docs/d3d9/STATUS.md` (clean of forbidden strings)
- MacNeutron (`c4d-mn`): `dxmt/d3d9-check.sh`, `dxmt/mtld3d-conformance.sh`, `vm/tests/d3d9.sh`, `vm/dxmt/d3d9-gate.sh`
- C4d-3 gate scripts: `build/c4d/s3/gates/{head,jt,jl,jq,jx,jc,jr,post}.sh`

### Verification & Gate Strategy
1. **Host unit tests (`tools/rust-check.sh` + explicit `--features d3d9/core-runtime,d3d9/perf-build` clippy)**:
   - `dxmt-core`: 3-lane ordering, `promote`, `run_urgent_or_wait` steal + multi-step continuation + `share_with_idle` + `cancel_unstarted` + `Weak` `SCHEDULERS` concurrent drop; `Compiled::try_ready` (`Pending`/`Ready`/`Failed`) + `ready_or_steal` + atomic ticket visibility; `Store::try_close` when locked (`WouldBlock` does not deadlock).
   - `mtld3d-core`: `ClearHistory<K>`, `mark_kept_reads_generic`, `DepthStencilSnapshot::writes_stencil`, `DeclCode::elements_with_end`.
   - `d3d9-rt` (using new `dxmt-fakes` cache/dispatch-data fakes):
     - `StageFunctions` concurrent dedup (2 threads same stage -> 1 compile, append-only never drops live `Obj`).
     - SQLite stage key determinism/disjointness + deduplicated `"D3D9REC1"` round-trip (canonical & non-canonical `D3DDECL_END` hitting `PsoMap::get`) + corrupt blob tolerance + warm-run `existing ∪ recorded` union merge.
     - Deterministic prewarm test (barrier-synchronized: `prewarmed_psos == N`, `0` live compiles).
     - Async draw skip on regenerated target (2 whole clears, no occlusion query, hidden `present()` advancing frames) -> 0 draws emitted; NOT skipped on frame 1 non-discard, persistent target, `feeds_persistent` target (excluding transient `StretchRect` scratch!), or active occlusion query -> deferred draw resolves on encode thread!
     - `PassBound` soundness: deferred draw (both succeeding and failing compile) emits non-PSO state + tile barrier without `SetPSO(0)`, resets `pass_bound.pso = 0` and `last_draw_gen = 0`, and next concrete draw re-emits `SetPSO`.
     - `CoreDevice::drop` with active prewarm + queued skipped draws completes cleanly and fast (`cancel_unstarted`).
2. **Mac & VM Integration Gates**:
   - **Strict launches (`shader.asyncCompile=false`)**: Pass `shader.asyncCompile=false` on all `strict` launches in `dxmt/d3d9-check.sh`, `vm/tests/d3d9.sh`, `s3/gates/jt.sh`, and `s3/gates/jq.sh`.
   - **Isolated cache directories**:
     - Mac (`d3d9-check.sh`, `jt.sh`, `jq.sh`, `mtld3d-conformance.sh`): set `DXMT_SHADER_CACHE_PATH` inside `$WORK` (never pollute `~/Library/Caches`).
     - VM (`vm/dxmt/d3d9-gate.sh`): export wiped `MN_WM_CACHE_DIR="$G/wm-cache"` so host-remapped VM SQLite DBs are isolated per run AND `sqlite3`-inspectable from the Mac host after VM exit.
   - **Async + Cold/Warm SQLite Cache + Real D3D11 Coexistence Check (`d3d9-check.sh` under `RT=core`)**:
     1. Cold run (`shader.asyncCompile=true`, fresh `DXMT_SHADER_CACHE_PATH=$WORK/cache-coexist`, `d3d9_triangle.exe fresh` + `shaders` non-strict): assert `ok`, `shaders_<ver>.db` exists, `cache_misses > 0`, `recipes_saved > 0`, and `sqlite3` shows 1 `cache_<ver>` table matching real `DXMT_TRANSLATOR_KEY` + `AIRCONV_VERSION` (not `"dev"`).
     2. Run a D3D11 test (`d3d11_render.exe` or `d3d11_draw.exe`) against the **EXACT SAME** `DXMT_SHADER_CACHE_PATH=$WORK/cache-coexist` -> assert STILL 1 `cache_%` table in `shaders_<ver>.db` (D3D11 did not drop D3D9's table!).
     3. Warm run (`shader.asyncCompile=true`, same `DXMT_SHADER_CACHE_PATH=$WORK/cache-coexist`, `d3d9_triangle.exe fresh` + `shaders` non-strict): assert `ok`, `cache_hits > 0`, `cache_misses == 0`, `recipes_loaded > 0` (proving D3D9 hit its SQLite cache AND D3D11 did not drop its rows!).
   - **Metal validation under async compile**: in `jt.sh` and `jq.sh`, run an async (`shader.asyncCompile=true`, non-strict) cold + warm `shaders` launch under `MTL_DEBUG_LAYER=1` (`0` reports!).
   - **Full gate sequence**: `rust-check.sh` + combined `core-runtime,perf-build` clippy -> `jt1` -> `jl1` -> `jq1` -> `jx1` (bounded idle Mac x64 D3D10-12) -> `jc1` (`JC_D3D10=1` VM D3D10-12 + VM D3D9 old/new WoW64 x64/x86 with wiped `MN_WM_CACHE_DIR` + SQLite inspect) -> `jr1` (plain build, translator key & imports unchanged) -> fresh final code review -> push + `post.sh` + CI.
