# DXMTRS C4: D3D9 as a DXMT front end — outline, and C4a (core prerequisites and the first slice) Implementation Plan

> **For agentic workers:** execution is **inline** (`superpowers:executing-plans`) by the session that wrote it, with
> Opus or Sonnet subagents for the larger tasks where the brief is the task text below, and one fresh Opus reviewer of
> the whole branch at the end. Steps use checkbox (`- [ ]`) syntax.

**Go-ahead and direction.** "Start C4" (maintainer, chat, 2026-11-11). Mid-way through the first C4a plan
(`2026-11-11-dxmtrs-c4a-inprocess-runtime.md`, mtld3d's own planner in `d3d9.dll`), the maintainer said: "I want D3D9
to be an extension of DXMT the same way D3D10-12 are. Same core, different api level support." This plan supersedes
that one. Standing authorization (program ledger, 2026-10-29): rulings self-approved after three adversarial Opus
reviews; push after every subproject.

**Goal (C4, spec §5 and §7):** `d3d9.dll` is a DXMT front end like `d3d11.dll`: COM objects and API state on the API
thread, recording into `dxmt-core`'s chunks (closures the core's `Encoder` runs on the encode thread), the core's hazard
model (untracked allocations, `access_*`, deptrack), optimizer, queue, presenter, shader entry and (by C4d) Logger;
D3D9-specific code is front-end state and API-level translation. Behind `runtime.mode = mtld3d | core` until parity;
C5 retires mtld3d.dll, mtld3d.so, the packet wire, the Unix encoder and the facade.

**Goal (C4a):** the core's prerequisites and the first vertical slice. A core hazard fix (its own push, for every API);
then, under `runtime.mode=core`, `d3d9_triangle`'s rows (device, clear, one fixed-function draw, present, vsync,
read-back) on the Mac (x64) and in the VM (x64; x86 under old and new WoW64), and an indicative reading of the core
runtime's CPU and address-space cost per lane. The shipped build is unchanged (a build option, off).

**Evidence.** `build/c4/map-*.md`; `build/c4/map2-d3d11-template.md` (D3D11 as the template; the slice file by file,
§9), `map2-passes.md`, `map2-sync-worklist.md`, `map2-deptrack.md`. Reviews `build/c4/review2-{1-design,2-core,
3-execution}.md` (record at the end).

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), Metal through winemetal, meson, C/C++ (MacNeutron
`dxmt/tests`, `vm/metal/wm`), sh (gates).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5, §6, §7, §8. **Ledgers:**
`.superpowers/sdd/dxmtrs-program.md`; `.superpowers/sdd/2026-11-11-dxmtrs-c4-d3d9-front-end/progress.md`.
**Pipeline rules:** `build/pipeline.md`: one lock for builds, `$D` checkouts and gates; one build per lock job, its
gates in the same job; `$D` back to `dxmt.applied` after each job; the MacNeutron main checkout detached at `c4r-mn`
for a job and restored by its trap.

**Bases.** DXMTRS `main` b3673ad4 → `c4r` (`build/c4/r`); MacNeutron `main` 0a1d863 → `c4r-mn` (`build/c4/r-mn`). The
superseded `c4`/`c4-mn` stay as they are.

---

## C4 outline (each part gets its own plan, rebased on what landed)

Slice order follows `map2-sync-worklist.md` §3; each part ends with its rows green under the core lane.

- **C4a — prerequisites and the first slice** (this plan).
- **C4b — draws, textures, passes:** programmable draws, constants (`bind_bytes`), MRT, textures and samplers, depth
  ops (work-list 16, 19, 24, 25, 26), mipmaps (11, 35), the upload model (bytes into the core's staging ring at record
  time, blits in API order: `texture_versions.rs`, the upload command buffer and frame-head prefix retire), the pass
  binder (the surviving ~2,300 lines of `passes.rs`), the core optimizer's generic changes (`drop_dead_store`,
  `clear_into_render`'s area guard, DONT_CARE planes and the later store in `same_targets`, `Resolve::discard_source`),
  feedback loops (depth copy route; colour self-sampling detected before any `access_*`), `map2-passes.md`'s two
  characterization tests, rect/viewport Clear. Gate: conformance stateblock under the core lane on the Mac.
- **C4c — resources and sync:** lockable buffers (`DynamicBuffer` renames, Lock-time decisions), creation contents
  (42, 43), every read-back form, render scale (33, 48 and the downscaled read-back), StretchRect and ColorFill,
  EVENT/OCCLUSION queries, Reset, gamma, frame latency, the device-lost latch (G1), the 32-bit retained-bytes gauge (G3),
  device info and pages from winemetal (thunks 1, 22, 23: `GetDeviceInfo`, `AllocPages`, `FreePages` leave mtld3d.so).
  Gate: conformance visual, device and d3d9ex per-site parity on the Mac.
- **C4d — policies, presenter options, logging, window:** async compile with skipped draws and pipeline prewarm as core
  policies (the compile scheduler lifted from d3d11 into the core, shared), D3D9's shaders in DXMT's cache keyed by the
  translator key, inverse tone mapping and `present.maxFps` as presenter options, the Logger move and spec §7's config
  and crash-report convergence, the HUD and perf lines into `DXMT_STATS`, the window features as d3d9-only winemetal
  slots (VM catalog rows), the native arm64 half (C4-15).
- **C4e — parity and the flip:** per-site parity on every leg, the 32-bit title in the VM (a game go-ahead asked for
  then; §7's bar) with G2 and the dropped present wait measured, the option on in the shipped build, the default flips
  to `core`, `d3d9_triangle`'s `module` row changed for C5. C5 retires mtld3d and merges `d3d9-rt` into `d3d9` (C4-2).

## Rulings

Self-approved after the three reviews; **(flag)** marks a deviation from D3D11's model or the spec's text that the
maintainer sees in the final message. A later fact that breaks one goes to the ledger and, when it changes the plan, to
the maintainer.

- **C4-1 (one recording path).** The core runtime records like D3D11 (API thread → `Queue::chunk().emit(closure)`),
  with one live path (no copy of D3D11's 54 `is_live()` fake branches); its host tests run closures on the core over
  winemetal fakes (C4-5). Under `core`, mtld3d's `FrameRecorder`, encoder thread and packet wire are never built or
  started (`EncoderThread::spawn`, `CreateCommandQueue`, `CreateBackbuffer`, `AttachMetalLayer` are not called).
- **C4-2 (a recording crate until C5) (flag).** The front end's core recording layer is a new host-testable crate,
  `src/d3d9/rt` (`d3d9-rt`, LGPL-2.1-or-later), linked into `d3d9.dll` under `core-runtime`; `src/d3d9/src` keeps COM
  and API state and calls it. Reason: `src/d3d9/src` has no host tests and is not known to compile on the host
  (`map-frontend.md` §6), and D3D11's one crate is host-testable. End state: C5 merges `d3d9-rt` and the COM front end
  into one `d3d9` crate (behind a `front-end` feature, as d3d11) once the COM layer moves onto `dxmt-util`. C4b's binder
  lands in `d3d9-rt`. Cost if wrong: a merge at C5 that touches every file of `d3d9-rt`.
- **C4-3 (behind a build option until C4e).** Meson `d3d9_core_runtime` (default false) builds `d3d9` with
  `core-runtime` for the arm64ec, x86_64 and i686 Rust targets (C4-15: not aarch64); MacNeutron's `DXMT_D3D9_CORE=1`
  passes it in dev builds only. `runtime.mode=core` in a build without it warns once and keeps `mtld3d`. Every C4a push
  keeps the default runtime, the translator key 051df2a0…, DXBC/DXIL output and D3D10-12 behaviour (C4-6 is a separate
  push, a deliberate D3D10-12 change).
- **C4-4 (what still reaches mtld3d.so under core in C4a) (flag).** `d3d9.dll` still imports `mtld3d.dll`. Under `core`
  these thunks still go there: `InitLogger`, `OpenLog`, `WriteLog` (logging until C4d) and `GetDeviceInfo` (adapter
  identity, caps and format gates from mtld3d.so's Metal device until C4c). The core's `MTLDevice` is winemetal's
  (`WMTCopyAllDevices`), the one whose registry ID `GetDeviceInfo` reported. Page boxes under `core` in the VM come
  from winemetal's `WMTMemoryAlloc`/`WMTMemoryFree` (`winemetal-sys`, guest-vm), not `AllocPages`; on the Mac from the
  PE allocator as today. Cost if wrong: two private paths until C4c/C4d.
- **C4-5 (test support).** `dxmt-core` gains a feature `headless` exposing `Encoder::headless`, `Device` creation on a
  fake handle and `format_pass_records` (no symbols); a new dev-only crate `src/dxmt/fakes` (`dxmt-fakes`) holds the
  `export_name` winemetal fakes a front-end test needs (device, queue, command buffer and commit, render and blit
  encoders with a capture of `renderCommandEncoder`/`encodeCommands`/`endEncoding`, render pipeline, SM50 slots, slot
  72's view and layer, drawable, `dxmt_log`). Only `d3d9-rt`'s tests depend on it and they run in their own cargo
  command, so no binary links two definitions of a fake (d3d11's, d3d12's and the core's own test fakes stay as they
  are).
- **C4-6 (the deptrack fix, its own push first) (flag).** `deptrack.rs`'s two unconditional render-half waits get the
  64-id gate (`map2-deptrack.md` §5; `review2-2-core.md` M2 re-derived it sound: every strong bit then lies in
  `[id-63, id-1]`). RED: the host test of §5 (A writes X; B writes T 64 ids later; R 128 ids after A writes X and reads
  T; R's wait closure, computed with `collect_and_simplify_waits` run for every filler half, must contain B), and an
  optimizer-level test that the gate does not let a merge move a writer past a reader. The `== tracker` golden is
  regenerated with a mechanical check that every new wait set is a subset of the old and that the changed lines are
  only the gated waits; `deptrack.rs`'s module doc says where it now departs from the C++ (which has the gap). D3D10-12
  change only in those waits; it can remove an old strong wait, so its cost if wrong is a missing hazard edge: the VM's
  `d3d11_hazards` (with a `lane64` smoke mode, run only by name, not in "all") and `d3d11_draw` rows run against
  b3673ad4's on the same image before it is pushed.
- **C4-7 (queue profile).** `Device::with_config(mtl, version, QueueConfig)`; `QueueConfig` gains the queue staging
  ring's and the argument-buffer ring's block sizes and the rings' lifetime (today constants); `gpu_heap` (no reader)
  is deleted; the `ResourceInitializer`'s ring (32 MiB, not placed) is left alone and recorded. D3D9's profile starts
  at D3D11's values (32 chunks: D3D9 allows a frame latency up to 30, G5), placed ring blocks 1 MiB on i686; measured
  by the probe and tuned in C4c.
- **C4-8 (direct binds).** `dxmt/src/encoder/direct.rs`: `bind_buffer::<S>`, `bind_texture::<S>` (tracking the bound
  view's whole mip and slice range), `bind_sampler::<S>`, `bind_bytes::<S>` (setBytes up to 4 KiB; beyond, the encode
  thread's argument-buffer ring and a buffer bind). No change to `render_bindings` (D3D11 starts four kinds of pass with
  `arguments: None`: `immediate.rs:523`, `encode.rs:712`, `clear.rs:654, 808`); the four empty argument-buffer binds per
  D3D9 pass are a measured cost, not a C4a change. A render pass re-emits all its state: a merge test pins it.
- **C4-9 (shaders through the core).** `dxmt_core::shader::initialize_dxso(bytes, args)` beside `initialize` (which
  keeps refusing non-DXBC): it refuses DXBC, adds the `SM50_SHADER_D3D9` argument and returns the same handle type, so
  `compile`, `compiled_data` and `load_function` serve D3D9 unchanged. The slice's FF stages go `ff_build` → DXSO →
  `initialize_dxso` → a library; the pipeline through winemetal (slot 156, vertex descriptor). No shader cache yet
  (C4d). The 32-bit VM guest's slots 76/77/156 are MacNeutron's (`vm/metal/wm/guest/wow64.c` and `sm50-args32.c` carry
  the D3D9 node and every slot); DXMTRS's Mac WoW64 table (no 32-bit caller on the Mac) is out of C4a's path.
- **C4-10 (logging in C4a).** `d3d9.dll` defines `dxmt_log` (the core's log hook, `dxmt/src/log.rs:21-25`) as d3d11
  does, forwarding to the `log` facade the PE side routes to mtld3d.so (C4-4); the Logger moves in C4d. The COM layer,
  `PrivateData` and the API lock stay per API (spec §7).
- **C4-11 (Present on the core).** A chunk closure around `Encoder::present`, then `signal_frame_latency`, commit, then
  `present_boundary`, in D3D11's order (`swapchain.rs:676-688, 719-729`); a hidden window skips all three (G4) and
  presents nothing; the layer from slot 72. `SetPresentWaitPolicy`/`hurry_presentation` are dropped and
  `paced_present_seq` has no equivalent (G2) (flag): up to about one refresh more run-ahead under vsync and a read-back
  right after a vsync'd Present may wait up to one refresh, measured in C4e.
- **C4-12 (sync and pass discipline).** `sync_to(seq)` commits when `seq >= current_seq()`, then `wait_cpu`; `drain()`
  commits if dirty, then `wait_cpu(current_seq() - 1)`. The recorder closes any open render pass before every core
  helper that begins a pass (Present, Clear pass, blit, resolve): `Encoder::begin` overwrites an open pass in release
  builds (`pass.rs:599-600, 628`). Read-back goes through `dxmt::staging::Staging` with `use_copy_destination`, the
  source tracked READ so the blit waits on the render pass.
- **C4-13 (unsupported calls under core in C4a).** An API the slice does not implement answers as mtld3d's front end
  does for an unsupported feature (`D3DERR_NOTAVAILABLE`/`E_NOTIMPL`) and logs once; a rect- or viewport-limited Clear
  is one of them (the core's Clear covers whole views; C4b). Conformance under core waits for C4b.
- **C4-14 (kept from the superseded C4a).** Onto `c4r`: the `runtime.mode` key, its config-wire byte and the
  `RuntimeMode` derive line (from 76f1b535 and 2f4361f4; not `runtime.rs`/`thunk_route` nor `pub mod runtime;`); a
  host-tested `RuntimeLatch` (first mode wins; a second device of the other mode is refused) in `mtld3d-core`. Onto
  `c4r-mn`: 589ca3e (gate lanes; proof lines rewritten in Task 8) and 2b3b922 (`DXMT_D3D9_CORE`). Not kept:
  `WMTUnixCall`, the facade's PE table, the `mtld3d-unix` PE port, `lease-adoption`, the rlib switch, the arm64ec zstd
  shim (no zstd in `d3d9.dll` until C4d's cache) and the guest's low-bitcode change (DXMT passes `Data` on as a value).
- **C4-15 (the native arm64 half stays on mtld3d) (flag).** On the Mac `winemetal.dll` is ARM64EC only
  (`wine-arm64/build.sh:505-507`; all 154 members of `winemetal.dll.a` are ARM64EC), so `d3d9.dll`'s aarch64 half cannot
  import it: that half is built without `core-runtime`, and a native arm64 process under `runtime.mode=core` warns once
  and runs on mtld3d. C4d decides between an ARM64X `winemetal.dll` and keeping native arm64 D3D9 on mtld3d until C5.
  Cost if wrong: native arm64 D3D9 programs (rare) are the last on mtld3d.

## Review Focus

1. A render pass re-emitting less than all its state (merges concatenate records): Task 4's merge test.
2. A Present or read-back on a clean chunk; `max_latency + 2` hidden Presents then a visible one (no hang): Task 6.
3. A Clear or Present behind an open render pass (C4-12): Task 6.
4. A 32-bit process's placed rings and arena (C4-7, `BlockPool` never shrinks): the probe's address space after a
   burst (Task 10).
5. Two runtimes in one process: `RuntimeLatch` (Task 0) and the refusal at `CreateDevice` (Task 5).

---

## Tasks

### Task 0: ledger, plan, keepers

- [ ] Step 1: The plan's ledger with pre-flight rows and every ruling; program ledger line; the superseded plan's
  header gains "Superseded by 2026-11-11-dxmtrs-c4-d3d9-front-end.md" (on `c4r-mn`, copied from `c4-mn`).
- [ ] Step 2: Onto `c4r`: the `runtime.mode` parts (C4-14), plus `RuntimeLatch` with tests
  `latch_takes_the_first_mode`, `latch_refuses_the_other_mode` (RED first). Onto `c4r-mn`: 589ca3e, 2b3b922.
- [ ] Step 3: `tools/rust-check.sh` ok on `c4r` (audit included); `sh dxmt/tests/d3d9-conf-compare-test.sh` ok.

### Task 1: the deptrack fix (C4-6), its own subproject

**Files:** `src/dxmt/src/deptrack.rs` (`:328-330`, `:426-428`, the module doc), `deptrack/tests.rs`,
`encoder/optimize/tests.rs`, the `== tracker` golden; MacNeutron `dxmt/tests/d3d11_hazards.cpp` (`lane64` by name).
- [ ] Step 1: The two RED tests (C4-6). Run `cargo test -p dxmt-core --lib deptrack optimize`. Expected: the closure test
  FAILS, the optimizer test passes (a guard, not a RED: ledger it).
- [ ] Step 2: The gate; tests pass; the golden regenerated with the subset check.
- [ ] Step 3: `lane64` in `d3d11_hazards.cpp`. Gates (one lock job, plain build): `dxmt/check.sh` x64 and arm64ec,
  `wine-arm64/check.sh`; VM `vm/dxmt/gate.sh` (d3d11_hazards incl. `lane64` by name, d3d11_draw) and the same rows from
  b3673ad4's build on the same image (A/B); `vm/d3d/gate.sh`; `d3d-smoke`. Final review of the two commits (Opus), push
  (DXMTRS main, MacNeutron pin), program ledger.

### Task 2: the queue profile (C4-7)

**Files:** `src/dxmt/src/device.rs`, `queue.rs`, `encoder/gaps.rs:1553-1594`, `queue/tests.rs`.
- [ ] Step 1: `with_config_reaches_the_queue`, `ring_blocks_follow_the_config` (on the core's own test fakes). FAIL.
- [ ] Step 2: Implement; d3d11/d3d12 on the default config; `cargo test --workspace` (the rust-check test leg) green.
- [ ] Step 3: Commit `dxmt: Device::with_config, the rings' sizes and lifetime in QueueConfig`.

### Task 3: test support (C4-5)

**Files:** `src/dxmt/Cargo.toml` (feature `headless`), `src/dxmt/src/encoder.rs` (`headless` under the feature),
`src/dxmt/src/test_support.rs`, new `src/dxmt/fakes` (`dxmt-fakes`), root `Cargo.toml` (member), `tools/rust-check.sh`
(a test line for `d3d9-rt` alone).
- [ ] Step 1: `dxmt-fakes`' own test `commit_captures_a_render_pass` (a `Device` on a fake handle, one render pass with one
  draw, commit: the capture lists the render encoder, the records, `endEncoding`). FAIL.
- [ ] Step 2: Implement. `cargo test --workspace` and `cargo test -p dxmt-fakes` green.
- [ ] Step 3: Commit `dxmt: a headless feature and dxmt-fakes, a front end's winemetal fakes with a render-encoder capture`.

### Task 4: direct binds and the DXSO entry (C4-8, C4-9)

**Files:** `src/dxmt/src/encoder/direct.rs` (+ tests), `encoder/optimize/tests.rs` (merge test), `src/dxmt/src/shader.rs`
(`initialize_dxso`, + tests).
- [ ] Step 1: Tests: `bind_texture_tracks_the_view_then_records`, `bind_bytes_inline_up_to_4k_and_ring_beyond`,
  `merged_passes_keep_each_pass_state`, `initialize_dxso_refuses_dxbc`, `initialize_dxso_adds_the_d3d9_argument`. FAIL.
- [ ] Step 2: Implement; DXBC/DXIL identity untouched (no translator change); `cargo test --workspace` green.
- [ ] Step 3: Commit `dxmt: direct binds by slot and a DXSO entry for D3D9's front end`.

### Task 5: `d3d9.dll` on the core (link, switch, device)

**Files:** new crate `src/d3d9/rt` (`d3d9-rt`: `lib.rs`, `device.rs` with `CoreDevice`), root `Cargo.toml`;
`src/d3d9/Cargo.toml` (`core-runtime = ["dep:d3d9-rt"]`, `guest-vm` → `d3d9-rt/guest-vm` → `dxmt-core/guest-vm` and
`winemetal-sys/guest-vm`, `perf-*` forwarded); `src/meson.build` (`subdir('dxmt')` before `subdir('d3d9')`, checked that
nothing in d3d9 feeds dxmt), `src/d3d9/meson.build` and `meson.options` (`d3d9_core_runtime`: `core-runtime` for the
arm64ec/x86_64/i686 staticlibs, the core's metallib and the translator key in `d3d9_env`, `guest-vm` from
`guest_vm`, `winemetal` in the x86_64/i686 links and the ARM64X link's EC half), `tools/arm64x-link.sh` (an EC-only
import library argument), `src/d3d9/src/{lib.rs,direct3d9.rs}` (`dxmt_log`; the latch; under `core`, `CreateDevice`
builds a `CoreDevice` (C4-4's device choice, the D3D9 queue profile) instead of the mtld3d calls (C4-1); a device logs
`d3d9 core: device on dxmt-core (<n> chunks)`), `tools/rust-check.sh` (`-p d3d9-rt` in HOST_P and PE_P; a clippy line
for `d3d9` with `core-runtime` on arm64ec/x86_64/i686).
- [ ] Step 1: `d3d9-rt` test `core_device_comes_up_on_the_fakes` (D3D9's profile reaches the queue). FAIL (no crate).
- [ ] Step 2: Implement; clippy on the PE targets with and without `core-runtime`; a meson build of `d3d9.dll` without
  the option: its import table and section sizes as `main`'s (the `.rdata` bytes differ between two links: compared by
  `llvm-readobj --sections` sizes and `--coff-imports`).
- [ ] Step 3: Commit `d3d9: d3d9-rt, the D3D9 front end's recording on dxmt-core, behind d3d9_core_runtime`.

### Task 6: Clear, Present and read-back (slices 1-2)

**Files:** `src/d3d9/rt/src/{swapchain,surface,recorder,sync}.rs` (+ tests); under `core`, the call sites in
`src/d3d9/src/device.rs` (`Clear`, `Present`, `BeginScene`/`EndScene`, `GetBackBuffer`, `GetRenderTarget`,
`GetRenderTargetData`, `CreateOffscreenPlainSurface`, presentation interval) and `surface.rs` (`LockRect` of a SYSTEMMEM
surface).
- [ ] Step 1: Tests on `dxmt-fakes`: `clear_becomes_the_pass_load_action`, `clear_behind_an_open_pass_closes_it`,
  `present_records_present_signals_commits_then_bounds`, `hidden_presents_never_hang` (`max_latency + 2` hidden, then
  one visible), `readback_waits_on_the_render_pass_then_its_seq`, `drain_on_a_clean_chunk_returns`. FAIL.
- [ ] Step 2: Implement (C4-11, C4-12): back buffer and depth as core textures, a presenter per swapchain on slot 72's
  layer, `sync_to`/`drain`, read-back through `Staging`.
- [ ] Step 3: Commit `d3d9: Clear, Present and read-back on the core (runtime.mode=core)`.

### Task 7: one fixed-function draw (slice 3)

**Files:** `src/d3d9/rt/src/{shader,pipeline,draw}.rs` (+ tests); under `core`, `SetFVF`, `SetRenderState` (the FF
key's states), `DrawPrimitiveUP` in `device.rs`.
- [ ] Step 1: Tests: `ff_draw_up_records_pipeline_binds_and_draw` (the capture shows the pipeline, the vertex bytes and
  one draw inside the cleared pass), `ff_pipeline_is_built_once_per_key`. FAIL.
- [ ] Step 2: Implement (C4-9).
- [ ] Step 3: Commit `d3d9: a fixed-function DrawPrimitiveUP on the core`.

### Task 8: gate lanes and the probe program (MacNeutron)

**Files:** `dxmt/d3d9-check.sh`, `dxmt/mtld3d-conformance.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh` (the core
lane's proof: `d3d9 core: device on dxmt-core` present and mtld3d.so's encoder-service start line (its exact string,
named in the commit) absent; no `shader backend:` row and no kinds or conformance rows under core in C4a; the arm64
row under core expects the C4-15 fallback warning); `dxmt/tests/d3d9_triangle.cpp` (`--bench <draws> <frames>`: no
read-back, immediate interval, prints frames, wall ms, process CPU ms (`GetProcessTimes`) and the address space in use
(`GlobalMemoryStatusEx`) at the end).
- [ ] Step 1: Comparator and script checks (`sh -n`, the comparator test); the bench mode builds (`make dxmt-tests`).
- [ ] Step 2: Commit `dxmt, vm: the core lane proves d3d9's core device; d3d9_triangle --bench`.

### Task 9: builds and gates

One build per lock job, its gates in the job.
- [ ] Job A (plain build, `make wine-arm64`): `dxmt/check.sh` x64 and arm64ec, `wine-arm64/check.sh`,
  `presenter/check.sh`, wm-probe (both modes), translator key unchanged, DXBC/DXIL identity (13 corpora), `d3d9-check`
  default and msl lanes, conformance default, the facade test, the loopback `d3d9-check`, licences and notices
  (`mtld3d-notices.py`, `d3d9-rt` included), the plain `d3d9.dll`'s imports as `main`'s (its `.text` grows by the
  runtime latch, Task 5's ruling).
- [ ] Job B (the core pair: a second DXMT build dir with `-Dd3d9_core_runtime=true`, its ARM64X `d3d9.dll` and
  `mtld3d.dll` swapped in through `MTLD3D_PE`): `d3d9-check` with `D3D9_RUNTIME=core` (rows module, clear, triangle,
  present, imports, dynamic, vsync, stale; arm64 expecting the fallback); mutation RED (the lane without `runtime.mode`
  fails every row on its proof).
- [ ] Job C (VM, image with `DXMT_D3D9_CORE=1`): `vm/dxmt/gate.sh`, `vm/d3d/gate.sh`, `vm/d3d/loopback-gate.sh`,
  `vm/dxmt-native/gate.sh`, `present-gate` (guest and forward, `MACNEUTRON_FRAMEGEN` modes as today), `d3d-smoke` (old
  and new WoW64), `d3d9-gate` default lane, and the core lane's `d3d9_triangle` rows for x64 and x86 under old and new
  WoW64.

### Task 10: the probe, final review, push

- [ ] Step 1: The probe (indicative): `d3d9_triangle --bench 2000 600` mtld3d vs core on the Mac x64 lane and the VM's
  x64, x86 old WoW64 and x86 new WoW64; CPU ms per frame, wall ms per frame, address space in use after the run;
  `build/c4/probe.md`. A loss over 10% on a lane goes to the maintainer with C4b's plan.
- [ ] Step 2: Fresh Opus reviewer of both branches; one fix pass (Critical/Important RED→GREEN, suite green).
- [ ] Step 3: Push (standing authorization): DXMTRS `c4r` → `main`, MacNeutron `c4r-mn` → `main` with the pin and the
  spec's §5 "C4a done" paragraph; privacy scan; the pushed pin built plain and `dxmt/check.sh` x64 once more. No tag, no
  release. Ledgers and memory; the maintainer's message: the (flag) rulings, the deptrack fix, the probe.

---

## Review record (2026-11-11)

`build/c4/review2-1-design.md` (1C/8I/7M), `review2-2-core.md` (1C/5I/9M), `review2-3-execution.md` (1C/12I/6M). Folded:
- ARM64EC-only `winemetal.dll` (design C1, execution C1) → C4-15, C4-3, Tasks 5, 8, 9.
- 32-bit WoW64 SM50 (core C1) → C4-9: ruled not on C4a's path (the VM guest's tables are MacNeutron's and carry the
  D3D9 node and slots 76/77/156; the Mac has no 32-bit caller); the VM x86 rows of Job C prove it.
- Thunks still reaching mtld3d.so (design I1, execution I5) → C4-4, C4-1, Tasks 5-7. `dxmt_log` (design I2,
  execution I2) → C4-10. Shaders around the core (design I3, core I1) → C4-9, Task 4. Crate deviation (design I4) →
  C4-2. Deptrack cost and scope (design I5, core M1-M3, execution I8) → C4-6, Task 1. Probe (design I6, execution
  I11) → Tasks 8, 10. Fakes (design I7, core I5, execution I2) → C4-5, Task 3. Work-list homes (design I8) → outline.
- Open pass lost (core I2), read-back race (core I3), Present order (core I4) → C4-11, C4-12, Task 6. M5/M6/M7/M8/M9 →
  C4-8, C4-7, C4-12, C4-13.
- Task 0's derive line (execution I1) → C4-14. rust-check legs (execution I3) → Tasks 3, 5. Meson order, metallib,
  guest-vm (execution I4) → Task 5. Proof strings (execution I6) → Task 8. D3D11 records (design M1, execution I7,
  core M4) → C4-8 (no `render_bindings` change). One build per job (execution I9) → Task 9. §8 gates (execution I10) →
  Task 9. Untested latch (execution I12) → C4-14, Task 0.
- Minors not folded, ledgered: design M3 (C4b's gate is stateblock, visual moves to C4c), M4 (presentation interval:
  Task 6's call list), M5/M6 (C4-1, outline), execution minors (golden generator: C4-6's subset check; syswow64 check
  dropped).
