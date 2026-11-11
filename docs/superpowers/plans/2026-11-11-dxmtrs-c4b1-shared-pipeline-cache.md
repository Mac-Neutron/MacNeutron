# DXMTRS C4b-1: one pipeline cache in the core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the compile scheduler and the keyed compiled-pipeline cache live in `dxmt-core`; D3D11 runs on them unchanged,
and D3D9's core path keys its fixed-function pipelines on them, building DXSO tokens and compiling only on a miss.

**Architecture:** `dxmt-core` gains `scheduler` (D3D11's `scheduler.rs`, moved verbatim) and `pipeline_cache`
(`Compiled<S>`: handle, retained PSO, optional job, spec; `PsoMap<K, S>`: lookup, insert-or-existing with
submit-after-insert through a lazily started scheduler, clear). Keys and specs stay per API: D3D11 instantiates it with
`GraphicsKey`/SHA-1; D3D9 with `FfPipelineKey`, defined in `mtld3d-core` beside the FF keys, with the miss builder
`ff_source(&FfPipelineKey)`. `d3d9-rt` (now depending on `mtld3d-core`) owns the D3D9 map and every PSO in it, so the
PSOs outlive the drain; the front end builds the key and the per-draw data only.

**Tech Stack:** Rust (DXMTRS workspace, toolchain `MTLD3D_RUST` 1.99.0), winemetal-sys, meson/ARM64X builds via
MacNeutron's `make wine-arm64`; MacNeutron gate scripts (sh).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: "draw skipping and pipeline prewarm as per-API
core policies"; the compile scheduler lifted from d3d11) and the C4 plan
`docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4-2, C4-12, C4-13). Maintainer, 2026-11-11: implement
mtld3d's optimisation in the core so every API shares it; D3D9 is a DXMT front end (same core, different API level).

## Global Constraints

- Default builds unchanged: no D3D10-12 behaviour change (same compile order, same waits, the scheduler started only
  when an inserted entry has a job, failed compiles cached as handle 0 and their draws dropped); D3D9 without
  `runtime.mode=core` unchanged.
- Translator key 051df2a0… and DXBC/DXIL output unchanged (13 corpora, `build/c3/xlate.sh`).
- `tools/rust-check.sh` all legs (fmt, gen, clippy, doc, test, audit): tests in `<stem>/tests.rs`, never inline;
  multi-line docs title / blank / body at <= 100 columns; derives in `tools/derive_inventory.txt`
  (`tools/audit.sh --update-derives`); new files git-added before the audit leg; a `len` has an `is_empty`.
- dxmt-core host tests need `--config 'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-undefined","-C","link-arg=dynamic_lookup"]'`.
- Pipeline lock, `$D` restored to `dxmt.applied`, main checkout restored to `main` by each job's trap; edits only in
  worktrees (`build/c4/b` DXMTRS `c4b`, `build/c4/b-mn` MacNeutron `c4b-mn`).
- No game runs; benches once each (bench-once rule).
- The core's can't-build log line keeps the phrase `does not build` (both gates grep it).

## Rulings

- **C4b-1 (what moves).** `scheduler.rs` moves verbatim to `dxmt_core::scheduler` (std and `dxmt_util::sync::lock`
  only); `d3d11` keeps `pub use dxmt_core::scheduler` so `exports.rs` (`MTLD3D11WaitIdle`, dlsym'd by the VM host) and
  `device.rs`'s drop keep their paths. The registry is per staticlib (one dxmt-core copy per DLL): `MTLD3D11WaitIdle`
  reaches d3d11's schedulers only, as today; C4d's D3D9 scheduler will need its own idle entry. `ManagedShader`'s
  variant single-flight stays in d3d11. `wait_idle`'s existing raw-address race (`scheduler.rs:23-28`) is not fixed
  here (pre-existing; ledgered for its own change).
- **C4b-2 (the core entry).** `Compiled<S>` generalises D3D11's `CompiledGraphics`/`CompiledCompute`: `ready()`
  identical (Acquire load; wait on the job; reload). `CompiledGraphics = Compiled<GraphicsPipelineKind>`,
  `CompiledCompute = Compiled<ComputePipelineSpec>`; their unused `Default` impls are deleted (E0117 on an alias);
  the field `kind` becomes `spec` (4 reads, 3 constructors, 3 lines in `pipeline/tests.rs`). One constructor makes a
  pending entry together with its job (`Arc::new_cyclic`), so no pending entry exists without a job and only
  `PsoMap` submits.
- **C4b-3 (D3D9 stays synchronous).** A D3D9 FF miss builds tokens and compiles on the API thread, as C4a does; its
  entries are `ready_with` (no job, no scheduler started). Async compile, draw skipping, prewarm and a non-blocking
  ready check are C4d's per-API core policies; D3D11's wait-on-ready policy is untouched.
- **C4b-4 (the D3D9 key and its owner).** `FfPipelineKey { vs: FfVsKey, ps: FfPsKey, variant: VariantKey, fvf: u32,
  stride: u32, write_mask: u8 }` and `ff_source(&FfPipelineKey) -> FfPipelineSource` live in `mtld3d-core`
  (`src/d3d9/core/src/ff_pipeline.rs`, host-tested); `d3d9-rt` gains the `mtld3d-core` dependency and owns
  `PsoMap<FfPipelineKey, ()>` as a `CoreDevice` field, dropped after `Drop::drop`'s drain. Today's arm draws only to the
  BGRA8 one-sample back buffer with blending refused, so the key is complete for it; a target format, sample count or
  blend state joins the key when the arm accepts them (C4c). Ceiling: a miss on `fvf`, `stride` or `write_mask` alone
  re-translates both stages (no per-stage cache yet). `FfVsKey`'s Hash skips `reserved` while its derived Eq compares
  it: consistent, no change.
- **C4b-5 (the VM probe).** `D3D9_BENCH=1 sh vm/dxmt/d3d9-gate.sh` writes `$S/bench`; `vm/tests/d3d9.sh` then runs
  `bench <exe> <row> <frames>` rows after the triangle rows (x64 rows only under old WoW64, bench once), each logging
  to `$O/logs/<row>` in both lanes, under `timeout -k 10 600`, the 0-frame row first; the gate checks one
  `d3d9 bench:` line, status 0 and the lane's runtime proof per row, and copies the rows to
  `$W/d3d9-bench/<row>-<wow64>-<lane>.txt`. Job C makes both `d3d9_triangle.exe` builds first (the i386 one predates
  `--bench`). The 10% line: CPU(600) − CPU(0) per frame, core vs mtld3d, same lane (guest CPU; the VM host excluded).

## Review Focus

1. D3D11 behaviour identity: compile order, the unlocked lookup, the spec built outside the lock, the recheck,
   submit after insert, the scheduler started only for a job, the null-device fake-handle path, `wait_idle` at device
   drop and `MTLD3D11WaitIdle`.
2. A D3D9 key that omits a pipeline input binds the wrong pipeline: pinned by `ff_pipeline_key_tracks_every_input`.
3. Teardown: recorded closures carry the PSO as a raw handle; the PSOs must release after the drain (the map is a
   `CoreDevice` field). A failed compile (handle 0) drops the draw, logged once per key.
4. Address space on i686 under core (cpu_heap 4 MiB blocks never shrink): measured (`vm_used_mb`), not assumed.
5. 32-bit `stride * count`: `up_byte_count(stride, count) -> Option<usize>` in `mtld3d-core` (checked), the arm
   refusing with D3DERR_INVALIDCALL on None.

---

### Task 1: the scheduler moves to the core

**Files:** `git mv src/d3d11/src/scheduler.rs src/dxmt/src/scheduler.rs` and its `scheduler/tests.rs`;
`src/dxmt/src/lib.rs` (`pub mod scheduler`); `src/d3d11/src/lib.rs` (`pub use dxmt_core::scheduler;` replaces the
module).

- [ ] Step 1: move; fix paths only. `cargo test -p dxmt-core --lib scheduler` (dynamic_lookup config). Expected:
  5 passed (a move, not a RED: ledger it).
- [ ] Step 2: `cargo test -p d3d11 --lib`; `tools/rust-check.sh`. Expected: ok; `git diff -M --stat` shows the two
  renames with path edits only.
- [ ] Step 3: commit `dxmt: the compile scheduler moves to the core (from d3d11, unchanged)`.

### Task 2: `Compiled<S>` and `PsoMap<K, S>` in the core; D3D11 on them

**Files:** create `src/dxmt/src/pipeline_cache.rs`, `src/dxmt/src/pipeline_cache/tests.rs`; modify
`src/d3d11/src/pipeline.rs` (entries, `graphics()`, `compute()`, `clear()`), `pipeline/tests.rs` (3 lines),
the `.kind` reads.

**Interfaces (produces):**
- `pub struct Compiled<S> { pub spec: Arc<S>, .. }`:
  `fn pending(spec: Arc<S>, compile: impl FnOnce(&S) -> Option<Obj> + Send + 'static) -> Arc<Self>` (job built inside
  `Arc::new_cyclic` from a Weak: upgrade, compile, store PSO, then handle with Release),
  `fn ready_with(spec: Arc<S>, handle: u64, pso: Option<Obj>) -> Arc<Self>`, `fn ready(&self) -> Option<u64>`.
- `pub struct PsoMap<K, S>`: `fn get<Q: Hash + Eq + ?Sized>(&self, key: &Q) -> Option<Arc<Compiled<S>>> where
  K: Borrow<Q>`; `fn insert_or_existing(&self, key: K, entry: Arc<Compiled<S>>, scheduler:
  &OnceLock<CompileScheduler>) -> Arc<Compiled<S>>` (recheck under the lock; insert; unlock; if the inserted entry
  has a job, `scheduler.get_or_init(CompileScheduler::default).submit(job)`; return the winner); `fn clear(&self)`;
  `fn len(&self) -> usize`; `fn is_empty(&self) -> bool`.

- [ ] Step 1: tests (`pipeline_cache/tests.rs`), no sleeps:
  `ready_waits_for_the_job_then_returns_its_handle` (the compile blocks on a channel the test releases after `get`;
  `ready()` through `get` returns the fake retained handle); `a_failed_compile_is_cached_as_none` (None twice, one
  compile); `the_second_insert_gets_the_first_entry_and_its_job_never_runs` (keep a clone of the loser; per-entry
  compile counters: winner 1, loser 0 after `wait_idle`); `ready_with_starts_no_scheduler` (the OnceLock stays
  uninitialised); `clear_releases_the_psos` (`NSObject_release` seen through the dxmt-fakes capture hook).
  Run: FAIL (module missing).
- [ ] Step 2: implement; tests pass. Mutations, each must fail a test: submit unconditionally (also for the loser);
  drop the recheck.
- [ ] Step 3: D3D11's `graphics()`/`compute()` build the spec outside the lock as today, then `Compiled::pending`
  (live device) or `Compiled::ready_with(spec, next_pso_handle(), None)` (null device), finishing through
  `insert_or_existing(.., &self.scheduler)`; `clear()` clears both maps; the `Default` impls go. `pipeline/tests.rs`
  pass (four).
- [ ] Step 4: `tools/rust-check.sh`. Commit `dxmt: a keyed compiled-pipeline cache in the core; d3d11 on it`.

### Task 3: D3D9's core FF pipelines keyed, built only on a miss

**Files:** create `src/d3d9/core/src/ff_pipeline.rs`, `src/d3d9/core/src/ff_pipeline/tests.rs`; modify
`src/d3d9/core/src/lib.rs`, `src/d3d9/rt/Cargo.toml` (`mtld3d-core`), `src/d3d9/rt/src/device.rs` (the map field),
`src/d3d9/rt/src/device/draw.rs` (`pipeline_key`'s token hash and the old map go; `draw_up` takes the key),
`src/d3d9/rt/src/device/draw/tests.rs`, `src/d3d9/src/device/core_arms.rs` (the arm passes the key and per-draw data;
no `ff_build`, no `fvf_to_elements` per draw).

**Interfaces:** consumes `PsoMap`, `Compiled::ready_with` (Task 2). Produces in `mtld3d-core`: `FfPipelineKey`
(C4b-4, `Hash + Eq + Clone`), `FfPipelineSource { vs_tokens, ps_tokens, attrs, provided_input_mask,
clip_plane_count, stride, write_mask, .. }` (everything the old `pipeline_key` hashed, `draw.rs:240-262`),
`fn ff_source(key: &FfPipelineKey) -> FfPipelineSource`, `fn up_byte_count(stride: u32, count: u32) -> Option<usize>`.
In `d3d9-rt`: `CoreDevice::draw_up(&mut self, key: &FfPipelineKey, draw: &FfDrawData) -> Result<(), DrawRefused>`
(lookup; on a miss `ff_source`, compile, `ready_with`, a handle-0 entry logged once with `does not build`; handle 0
records nothing).

- [ ] Step 1: tests. `mtld3d-core` (`ff_pipeline/tests.rs`): `ff_pipeline_key_tracks_every_input` (for each field
  of the key, two keys differing only there give sources differing in what the old `pipeline_key` hashed; equal keys
  give equal sources); `up_byte_count_refuses_an_overflow` (`u32` `checked_mul`, then `usize::try_from`: `(u32::MAX, 2)` is None on
  every host; no DrawPrimitiveUP passes 4 GiB). `d3d9-rt` (dxmt-fakes): `ff_pipeline_compiles_once_per_key` (two draws,
  one key: one pipeline creation counted at `fakes/src/lib.rs:326`; a second key: two);
  `ff_draw_up_records_pipeline_binds_and_draw` (kept, taking a key); `ff_draw_with_a_failed_pipeline_records_nothing`.
  Run: FAIL.
- [ ] Step 2: implement; tests pass; `tools/rust-check.sh` (host and PE legs).
- [ ] Step 3: commit `d3d9: core FF pipelines keyed on the FF state in the core's cache, built only on a miss`.

### Task 3b: the VM bench lane (MacNeutron)

**Files:** `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh` (C4b-5).

- [ ] Step 1: implement C4b-5's mechanism; `sh -n`; check the gate's new rows with `D3D9_GATE_TRACES` on a hand-made
  folder (a good row passes; a row without its `d3d9 bench:` line, and one whose logs lack the lane's runtime proof,
  each FAIL).
- [ ] Step 2: commit `vm: D3D9_BENCH=1 runs d3d9_triangle --bench rows in the guest, proven per lane`.

### Task 4: gates and benches (one build per lock job)

- [ ] Job A (plain build): `dxmt/check.sh` x64 and arm64ec, `wine-arm64/check.sh`, presenter, wm-probe (Mac mode),
  `xlate` 13 corpora (key unchanged), `d3d9-check` default and msl, conformance default, loopback `d3d9-check`, facade,
  `licences_test.sh <wine.app>`, plain `d3d9.dll` imports as main's.
- [ ] Job B (`DXMT_D3D9_CORE=1`): `cargo build --release` in the main checkout first (`target/release/macneutron`);
  `d3d9-check` core lane + mutation; Mac bench mtld3d vs core (0 and 600 frames) with each run's logs grepped for its
  runtime proof (core: the device line, no encoder line; mtld3d: the reverse), into `build/c4/probe-b1.md` beside
  C4a's numbers.
- [ ] Job C (VM, the core image; the key row reads Job B's wine.app): `make build/dxmt-tests/d3d9_triangle.exe
  build/dxmt-tests-i386/d3d9_triangle.exe`; native build then `wm-probe --native`, native gate, loopback, dxmt, d3d,
  present ×2, `d3d-smoke` old/new, `d3d9-gate` default and core under old and new WoW64, each with `D3D9_BENCH=1`;
  bench rows into `probe-b1.md` (x64 old WoW64; x86 old and new), `vm_used_mb` for x86 recorded.
- [ ] Core conformance stays off under core (it exits 2) in C4b-1.

### Task 5: final review, push

- [ ] Fresh Opus review of both branches; one fix pass; a d3d9 fix re-runs Job B's core lane before the push.
- [ ] Push DXMTRS `c4b` → main, MacNeutron `c4b-mn` → main with the pin and a spec §5 paragraph; privacy scan; the
  pushed pin built plain and `dxmt/check.sh` x64 once more. No tag.

---

## Review record (2026-11-11)

Three adversarial Opus reviews (`build/c4/review3-{1-design,2-d3d11,3-exec}.md`), folded: teardown (two Criticals:
recorded closures carry raw PSO handles, so the map is a `CoreDevice` field released after the drain, not cleared
before it); the VM bench mechanism (Critical: the i386 exe predated `--bench`, `tri` passed no arguments, `$G` is
wiped per run) as Task 3b; the scheduler started lazily (`&OnceLock`, D3D11's start-on-job kept); one pending
constructor with its job; tests that can fail (channels, per-entry counters, two mutations); the key and its builder
in host-tested `mtld3d-core`; `Default` impls deleted; the `.kind` count; `does not build` kept; Job B re-run after a
d3d9 fix; conformance default restored to Job A; the 10% line defined.
