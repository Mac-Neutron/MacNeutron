# DXMTRS C4b-1: one pipeline cache in the core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the compile scheduler and the keyed compiled-pipeline cache live in `dxmt-core`; D3D11 runs on them unchanged,
and D3D9's core path keys its fixed-function pipelines on them, building DXSO tokens and compiling only on a miss.

**Architecture:** `dxmt-core` gains `scheduler` (D3D11's `scheduler.rs`, moved verbatim) and `pipeline_cache`
(`Compiled<S>`: handle, retained PSO, optional job, spec; `PsoMap<K, S>`: lookup, insert-or-existing with
submit-after-insert, clear). Keys and specs stay per API: D3D11 instantiates it with `GraphicsKey`/SHA-1, D3D9's front
end with `FfPipelineKey`. `d3d9-rt` compiles a pipeline from tokens on request and draws with a handle; it no longer
hashes tokens or keeps a pipeline map.

**Tech Stack:** Rust (DXMTRS workspace, toolchain `MTLD3D_RUST` 1.99.0), winemetal-sys, meson/ARM64X builds via
MacNeutron's `make wine-arm64`; MacNeutron gate scripts (sh).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: "draw skipping and pipeline prewarm as per-API
core policies"; the compile scheduler lifted from d3d11) and the C4 plan
`docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4-2, C4-12, C4-13). Maintainer, 2026-11-11: implement
mtld3d's optimisation in the core so every API shares it; D3D9 is a DXMT front end (same core, different API level).

## Global Constraints

- Default builds unchanged: no D3D10-12 behaviour change (same compile order, same waits, same failed-compile
  semantics: handle 0 cached, the draw dropped); D3D9 without `runtime.mode=core` unchanged.
- Translator key 051df2a0… and DXBC/DXIL output unchanged (13 corpora, `build/c3/xlate.sh`).
- `tools/rust-check.sh` all legs (fmt, gen, clippy, doc, test, audit): tests in `<stem>/tests.rs`, never inline;
  multi-line docs title / blank / body at <= 100 columns; derives in `tools/derive_inventory.txt`
  (`tools/audit.sh --update-derives`); new files git-added before the audit leg.
- dxmt-core host tests need `--config 'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-undefined","-C","link-arg=dynamic_lookup"]'`.
- Pipeline lock, `$D` restored to `dxmt.applied`, main checkout restored to `main` by each job's trap; edits only in
  worktrees (`build/c4/b` DXMTRS `c4b`, `build/c4/b-mn` MacNeutron `c4b-mn`).
- No game runs; benches once each (bench-once rule).

## Rulings

- **C4b-1 (what moves).** `scheduler.rs` moves verbatim to `dxmt_core::scheduler` (it needs only std and
  `dxmt_util::sync::lock`); `d3d11` keeps a `pub use dxmt_core::scheduler` re-export so `exports.rs`
  (`MTLD3D11WaitIdle`, dlsym'd by the VM host) and `device.rs` drop keep their paths. `ManagedShader`'s variant
  single-flight (`shader.rs:95-278`) stays in d3d11 (no second user yet).
- **C4b-2 (the core entry).** `Compiled<S>` is D3D11's `CompiledGraphics`/`CompiledCompute` with the spec generic:
  `ready()` identical (Acquire load, wait on the job, reload). `CompiledGraphics = Compiled<GraphicsPipelineKind>`,
  `CompiledCompute = Compiled<ComputePipelineSpec>` (type aliases); the field `kind` becomes `spec` (27 sites).
- **C4b-3 (D3D9 stays synchronous).** A D3D9 FF miss builds tokens and compiles on the API thread, as C4a does; the
  entry is created ready (`Compiled::ready_with`). Async compile, draw skipping and prewarm are C4d's per-API core
  policies (C4 plan); D3D11's wait-on-ready policy is untouched.
- **C4b-4 (the D3D9 key).** `FfPipelineKey { vs: FfVsKey, ps: FfPsKey, variant: VariantKey, fvf: u32, stride: u32,
  write_mask: u8 }` in the d3d9 front end (`src/d3d9/src/device/core_ff.rs`), its `PsoMap` beside the
  `Option<Box<CoreDevice>>` (`d3d9-rt` cannot see `mtld3d-core`'s key types, C4-2). `FfVsKey`'s Hash skips `reserved`
  while its derived Eq compares it: a hash over a subset of Eq's fields is consistent (equal keys hash alike), so no
  change; keys differing only in `reserved` are separate entries, harmless. The keys are still built every draw (cheap,
  allocation-free; dirty tracking is a later lever if the bench asks).
- **C4b-5 (the VM probe).** The bench runs in the VM through `vm/dxmt/d3d9-gate.sh` with `D3D9_BENCH=1`: the guest
  runs `d3d9_triangle --bench 2000 0` and `2000 600` for x64 and x86 after the triangle rows, under the lane's runtime.
  A loss over 10% CPU on any lane goes to the maintainer.

## Review Focus

1. D3D11 behaviour identity: the move must not change compile order, the recheck-then-insert window, the submit after
   insert, or `wait_idle` at device drop / `MTLD3D11WaitIdle`.
2. A D3D9 key collision or an Eq/Hash mismatch binds the wrong pipeline: two FF states differing only in a field the
   key omits (a render state `ff_build` reads but the key does not carry).
3. Teardown: D3D9's `PsoMap` holds retained PSOs; it must clear before the core device's Metal device goes, and a
   failed compile (handle 0) must drop the draw as C4a's `None` did, logged once.
4. Address space on i686 under core (cpu_heap 4 MiB blocks never shrink): measured, not assumed.
5. 32-bit `stride * count` (C4a deferred minor): the draw arm this plan rewrites uses `checked_mul`, refusing with
   D3DERR_INVALIDCALL.

---

### Task 1: the scheduler moves to the core

**Files:** move `src/d3d11/src/scheduler.rs` → `src/dxmt/src/scheduler.rs`, `src/d3d11/src/scheduler/tests.rs` →
`src/dxmt/src/scheduler/tests.rs`; `src/dxmt/src/lib.rs` (`pub mod scheduler`); `src/d3d11/src/lib.rs`
(`pub use dxmt_core::scheduler;` in place of the module); call sites unchanged.

- [ ] Step 1: `git mv` both files; fix only paths (`crate::` → the core's). Run the five scheduler tests under
  `cargo test -p dxmt-core --lib scheduler` (with the dynamic_lookup config). Expected: 5 passed (they ran in d3d11
  before; a move, not a RED — ledger it).
- [ ] Step 2: `cargo test -p d3d11 --lib` and `tools/rust-check.sh`. Expected: all ok; `git diff -M` shows renames
  with only path edits.
- [ ] Step 3: commit `dxmt: the compile scheduler moves to the core (from d3d11, unchanged)`.

### Task 2: `Compiled<S>` and `PsoMap<K, S>` in the core; D3D11 on them

**Files:** create `src/dxmt/src/pipeline_cache.rs`, `src/dxmt/src/pipeline_cache/tests.rs`; modify
`src/d3d11/src/pipeline.rs` (entries and the two get-or-create paths), the 27 `.kind` sites.

**Interfaces (produces):**
- `pub struct Compiled<S> { pub spec: Arc<S>, .. }` with `fn pending(spec: Arc<S>) -> Arc<Self>`,
  `fn ready_with(spec: Arc<S>, handle: u64, pso: Option<Obj>) -> Arc<Self>`,
  `fn attach_job(self: &Arc<Self>, compile: impl FnOnce(&S) -> Option<Obj> + Send + 'static) -> CompileJob`
  (the job upgrades a Weak, stores the PSO, then the handle with Release), `fn ready(&self) -> Option<u64>`.
- `pub struct PsoMap<K, S>` with `fn get<Q>(&self, key: &Q) -> Option<Arc<Compiled<S>>> where K: Borrow<Q>`,
  `fn insert_or_existing(&self, key: K, entry: Arc<Compiled<S>>, scheduler: &CompileScheduler) -> Arc<Compiled<S>>`
  (recheck under the lock; insert; drop the lock; submit the entry's job if any; return the winner),
  `fn clear(&self)`, `fn len(&self) -> usize`.

- [ ] Step 1: tests in `pipeline_cache/tests.rs`: `ready_waits_for_the_job_then_returns_its_handle` (a job sleeping
  20 ms that returns a fake retained handle; `ready()` returns it), `a_failed_compile_is_cached_as_none`
  (compile returns None: `ready()` is None twice, the job ran once), `insert_or_existing_keeps_the_first_entry`
  (two inserts on one key: the second gets the first's Arc; only the first's job is submitted),
  `ready_with_is_ready_without_a_job`, `clear_empties_the_map`. Run: FAIL (module missing).
- [ ] Step 2: implement in the core; tests pass.
- [ ] Step 3: D3D11's `graphics()`/`compute()` build their entries with `Compiled::pending` + `attach_job` (the
  live-device path) or `ready_with(spec, next_pso_handle(), None)` (the null-device path) and finish through
  `PsoMap::insert_or_existing`; `clear()` clears both maps. Existing `pipeline/tests.rs` pass unchanged (four).
  Mutation: submit before insert in `insert_or_existing` must fail `insert_or_existing_keeps_the_first_entry`.
- [ ] Step 4: `tools/rust-check.sh`. Commit `dxmt: a keyed compiled-pipeline cache in the core; d3d11 on it`.

### Task 3: D3D9's core FF pipelines keyed in the front end

**Files:** create `src/d3d9/src/device/core_ff.rs` (+ `core_ff/tests.rs` if host-testable, else the test lives in
`src/d3d9/rt/src/device/draw/tests.rs`); modify `src/d3d9/src/device/core_arms.rs` (draw_up arm),
`src/d3d9/rt/src/device/draw.rs` (`CoreDevice::compile_ff_pipeline(&FfPipelineSource) -> Option<Obj>`,
`FfDraw` takes `pipeline: u64`; the token-hash `pipeline_key` and the `pipelines` map go), the device release path
(clear the map before the core device drops).

**Interfaces:** consumes `PsoMap`, `Compiled::ready_with` (Task 2). Produces `FfPipelineKey` (C4b-4),
`FfPipelineSource { vs_tokens, ps_tokens, attrs, stride, write_mask, .. }`.

- [ ] Step 1: tests: `ff_pipeline_compiles_once_per_key` (rt, dxmt-fakes: two draws with one source compile once —
  the existing `ff_pipeline_is_built_once_per_key` reshaped onto `compile_ff_pipeline` + a handle, with a compile
  count); `ff_draw_up_records_pipeline_binds_and_draw` kept, now taking a handle; `ff_key_names_every_ff_build_input`
  (front end, if host-testable: two FF states differing in each render state `ff_build` reads give different
  `FfPipelineKey`s — Review Focus 2). Run: FAIL.
- [ ] Step 2: implement. The arm builds the keys, looks up `PsoMap`; on a miss builds tokens (`ff_build`), calls
  `compile_ff_pipeline`, inserts `ready_with` (handle 0 on failure, logged once per key as C4a); a handle-0 entry drops
  the draw. `stride * count` via `checked_mul` → `D3DERR_INVALIDCALL`. Tests pass; rust-check (PE legs too).
- [ ] Step 3: commit `d3d9: core FF pipelines keyed on the FF state in the front end, built only on a miss`.

### Task 4: gates and benches (one build per lock job)

- [ ] Job A (plain build): `dxmt/check.sh` x64 and arm64ec, `wine-arm64/check.sh`, presenter, wm-probe both modes,
  `xlate` 13 corpora (key unchanged), `d3d9-check` default and msl, loopback `d3d9-check`, facade, licences
  (`licences_test.sh <wine.app>`), plain `d3d9.dll` imports as main's.
- [ ] Job B (`DXMT_D3D9_CORE=1`): `d3d9-check` core lane + mutation; Mac bench mtld3d vs core (0 and 600 frames,
  logs proving the runtime), into `build/c4/probe-b1.md` beside C4a's numbers.
- [ ] Job C (VM, the core image): native, loopback, dxmt, d3d, present ×2, `d3d-smoke` old/new, `d3d9-gate` default
  and core old/new; with C4b-5's `D3D9_BENCH=1` lane (MacNeutron change: `vm/tests/d3d9.sh` runs the bench rows,
  `vm/dxmt/d3d9-gate.sh` copies their `d3d9 bench:` lines to `$G/bench-<arch>-<frames>.txt`), both runtimes, x64 and
  x86 under old and new WoW64; `vm_used_mb` for x86 recorded (Review Focus 4).

### Task 5: final review, push

- [ ] Fresh Opus review of both branches; one fix pass. Push DXMTRS `c4b` → main, MacNeutron `c4b-mn` → main with the
  pin and a spec §5 paragraph; privacy scan; the pushed pin built plain and `dxmt/check.sh` x64 once more. No tag.
