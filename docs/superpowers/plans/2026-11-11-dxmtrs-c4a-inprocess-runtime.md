# DXMTRS C4: D3D9 on the shared core — outline, and C4a (the in-process runtime) Implementation Plan

> **Superseded** (2026-11-11) by `2026-11-11-dxmtrs-c4-d3d9-front-end.md`: the maintainer asked for D3D9 as a DXMT
> front end like D3D10-12 (same core, different API level). Kept as the record of the first C4a (its work stays on the
> unpushed branches `c4`/`c4-mn`).

> **For agentic workers:** execution is **inline** (`superpowers:executing-plans`) by the session that wrote it, with
> Sonnet or Opus subagents for the larger mechanical tasks where the brief is the task text below, and one fresh Opus
> reviewer of the whole branch at the end. Steps use checkbox (`- [ ]`) syntax.

**Go-ahead.** The maintainer gave the go for C4 in chat on 2026-11-11: "Start C4" (C3's benches, the MSL deletion and
C3's release still parked for them). Standing authorization (program ledger, 2026-10-29): rulings are self-approved
after three adversarial Opus reviews; push after every subproject.

**Goal (C4, spec §5):** `d3d9.dll` on the shared core (D3D11's model, PE-side encoding), the bridge (`d3d9.dll` calls
`winemetal.dll`) and the one presenter; mtld3d's macOS window features become winemetal slots; behind a switch until
parity, measured with a 32-bit D3D9 title in the VM's lanes separately.

**Goal (C4a, this plan): the planner's PE port, the switch and the lanes.** D3D9's planner (mtld3d's encoder, draw
lowering, pass rules, compile workers) builds into `d3d9.dll` and runs in-process on the PE side, reaching Metal
through `winemetal.dll` via mtld3d's existing `mtl` facade, selected by `runtime.mode = mtld3d | core` (default
`mtld3d`, today's runtime). The linked planner sits behind a build option that is off in the shipped build until C4e
(C4-13), so a pushed main ships today's `d3d9.dll`. The gates gain a `core` lane on every leg, proven from outside the
switch. A probe (indicative, not a gate) measures the planner on the PE side per lane.

**Open for the maintainer (asked 2026-11-11, non-blocking for C4a, blocking for C4b's plan):** four rulings read
"D3D9 on the core" more loosely than §7's text: C4-1/C4-2 (D3D9 keeps its own recording, the frame packet, replayed by
its planner into core chunks, instead of recording into chunks on the API thread as D3D11 does), C4-3 (Metal's hazard
tracking kept), C4-12 (policies stay D3D9's), C4-8 (logging on mtld3d.so through C4). C4b's plan starts with a
read-only check of whether the nine whole-frame pass rules can run at the core's flush over a chunk's pass list
(with `frame_continues` known at commit): if yes, §7's literal design is reachable and the deviations shrink.

**Evidence.** Maps in `build/c4/`: `map-d3d9-runtime.md`, `map-core.md`, `map-bridge.md`, `map-frontend.md`. Reviews:
`build/c4/review-1-design.md`, `review-2-runtime.md`, `review-3-execution.md` (record at the end). Checks made while
planning (2026-11-11, DXMTRS b3673ad4):
- The planner reaches Metal only through `crate::metal`'s function API: direct Metal references per file are
  `encoder.rs` 40 of 10616 lines, `draw.rs` 1, `encoder/ops.rs` 4, `encoder/compile.rs` 3; the rest sit in
  `unix/src/metal/*` (the walker `command.rs` 123, `texture.rs` 68, ...).
- `cargo check -p mtld3d-unix --target i686-pc-windows-msvc` fails with 99 errors in Unix-only code (`crash.rs` 65,
  `log_file.rs` 10, `handlers.rs` 10, `metal.rs` 9, `translation.rs` 1, `shared/src/identity.rs` 1, guest-page
  `adopt*` 3); a lower bound (i686 only; later type errors hide behind the first).
- The VM guest's 32-bit winemetal table already has every slot (`vm/metal/wm/guest/wow64.c:106-118`); the Mac's stops
  at 152, but the Mac ships no i686 PE, so nothing in C4a calls it (Task 1 of the first draft dropped).
- winemetal.dll has no generic call export (`WMTForward` is guest-only, slot 155 only, `src/winemetal/src/guest.rs:20`).

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), Wine's unix-call ABI, meson, C (MacNeutron
`vm/metal/wm`), sh (gates).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4, C5), §6, §7, §8, §9. **Ledgers:**
`.superpowers/sdd/dxmtrs-program.md`; `.superpowers/sdd/2026-11-11-dxmtrs-c4a-inprocess-runtime/progress.md`.
**Pipeline rules:** `build/pipeline.md`: one lock (`/usr/bin/lockf -k build/pipeline.lock`) for builds, `$D` checkouts
and gates; `$D` restored to `dxmt.applied` after every job; the MacNeutron main checkout detached at `c4-mn` for a
job and restored to `main` by the job's trap (gates read its `build/` and `target/`), as C3d's jobs did.

**Bases.** DXMTRS `main` b3673ad4 (branch `c4`, worktree `build/c4/wt`); MacNeutron `main` 0a1d863 (branch `c4-mn`,
worktree `build/c4/wt-mn`). Environment: `build/c4/env.sh`; PE C environment `tools/mingw-cc.sh env <target> <dir>`.

---

## C4 outline (each part gets its own plan, rebased on what landed)

- **C4a, the in-process runtime** (this plan).
- **C4b, the core's queue.** First a read-only check (above). Then the planner's encoder thread becomes the only
  recorder of a `dxmt-core` `Queue` (a D3D9 `QueueConfig` through a new `Device::with_config`: chunk count, ring
  blocks, arena trimming, the 32-bit memory profile); `submit_frame`'s walker emits each planned pass into a chunk
  through the core `Encoder` (direct binds); the core's finish thread publishes D3D9's counters. Must settle: D3D9
  chunks bypass `optimize()` (a `QueueConfig` bit) or declare their accesses (C4-3); the sync flush's "ack after
  commit" (the core has completion seqs only: a commit-level seq or the ack moves to completion, measured); which queue
  carries present and read-back until C4c; the no-copy PE-page buffers' lifetime (PE leases vs chunk retirement);
  API-thread staging (`allocate_staging_buffer(&mut self)`) against one recorder. `d3d9.dll` imports `winemetal.dll`
  from here (the core's `winemetal_sys`), and `WMTUnixCall` joins C5's retire list.
- **C4c, the core's resources and presenter.** Textures, buffers and views as core allocations; pipelines, samplers
  and depth states through the core's paths; read-back through `Staging`; queries (event, occlusion) on the core's;
  frame latency on `present_boundary`; present through `dxmt::presenter::Presenter` with D3D9's options (inverse tone
  mapping on EDR, which the core lacks and gains here, `present.maxFps`, snapshot policy, gamma from `D3DGAMMARAMP`);
  Reset; the HUD row and perf lines into `DXMT_STATS`; GPU capture.
- **C4d, the window slots.** Cursor overlay, view parking, client-surface present, EDR/screen-parameter follow and the
  occlusion observer as new d3d9-only winemetal slots (64-bit and WoW64 tables, VM catalog rows, `wm_probe` coverage),
  replacing `macdrv_wm.rs`'s absences under the core runtime on the Mac.
- **C4e, parity and the flip.** Per-site parity on every leg (Mac arm64x; VM x64 and x86, old and new WoW64); the
  32-bit title measured in the VM under §7's bar ("measures worse" goes to the maintainer before C5; a game go-ahead
  asked for then); the build option on in the shipped build; the default flips to `core`; what happens to the frame
  packet (kept as D3D9's in-process recording or replaced, per the maintainer's answer above). C5 then retires
  mtld3d.dll, mtld3d.so, the transport, the wire, the facade and `WMTUnixCall`.

## Rulings

Each default below is self-approved after the three reviews; a later fact that breaks one goes to the ledger and,
when it changes the plan, to the maintainer. **(flag)** marks a reading of the spec the maintainer may not have meant;
those are in the open question above and in the final message.

- **C4-1 (port the planner, not rewrite it) (flag).** The planner (`unix/src/encoder.rs`, `draw.rs`, `encoder/*`, the
  pass rules in `core/src/passes.rs` with their 10810 test lines) moves to the PE side as code; the core runtime
  replaces `crate::metal` stage by stage (C4a: the facade over winemetal.dll; C4b/C4c: the core). Cost if wrong: the
  frame packet stays D3D9's recording (about 7.5k lines the runtime map marks to die at C5 outlive C4e), against §7's
  "records into the core's command chunks as D3D11 does"; a redesign of the recording is C4b's to plan if the
  maintainer asks for the literal reading.
- **C4-2 (planning thread) (flag).** The planner keeps its own thread (`mtld3d-encoder`, now a PE thread) fed by the
  same `FramePacket`s; in C4b it is the core queue's only recorder (the core's invariant 6). Cost if wrong: as C4-1.
- **C4-3 (hazards stay Metal's) (flag).** Through C4, D3D9's resources keep Metal's tracked hazard mode: in C4a the
  facade passes storage bits only (`mtl/src/metal/descriptors.rs:274`), as the VM does today. In C4b D3D9's chunks
  must either bypass the core's `optimize()`, whose render/render merge moves passes past each other on fence sets
  alone (`src/dxmt/src/encoder/optimize.rs:93-101, 140-146`), or declare every access; C4b's plan picks, with a RED
  test for write / sample elsewhere / rewrite. Cost if wrong: a stale-read reorder if C4b gets this wrong (C4a is not
  exposed: the facade has no optimizer); D3D11's untracked model for D3D9 is a later measured option.
- **C4-4 (the switch).** `runtime.mode = mtld3d | core` in `mtld3d.conf`, `MTLD3D_CONFIG` and `DXMT_CONFIG` (which
  reaches D3D9 as `shader.backend` does, and which the conformance runner leaves alone), read by
  `mtld3d_core::config`; latched process-wide at the first `create_interface` (`src/d3d9/src/lib.rs:302`, before
  `install_page_allocator`), `mtld3d` until then; an unknown value warns and keeps `mtld3d`; `core` in a build without
  the option warns once ("runtime.mode=core: not built in; mtld3d") and keeps `mtld3d`. Cost if wrong: a key rename.
- **C4-5 (the bridge in C4a).** `winemetal.dll` gains one `extern "C"` export, `WMTUnixCall(uint32_t slot, void
  *params) -> int32_t` (both `.def` files), refusing `slot >= 158 || slot == 83` (the one index with no 64-bit handler)
  with `STATUS_INVALID_PARAMETER` (0xC000000D) and otherwise calling `transport::unix_call`; 153-155 pass (the Mac's
  table answers NOT_IMPLEMENTED there). The core runtime finds it with `LoadLibraryW(L"winemetal.dll")` and
  `GetProcAddress`; a failure fails the device's creation with one error line (`D3DERR_NOTAVAILABLE`), never a panic
  and never a fallback. From C4b `d3d9.dll` imports `winemetal.dll` through the core; `WMTUnixCall` retires at C5. Cost
  if wrong: one export listed in the export comparison.
- **C4-6 (in-process dispatch).** `src/d3d9/src/unix_call.rs`'s `unix_call<T: Thunk>` stays the one seam: under
  `mtld3d` it calls `mtld3d.dll`'s `mtld3d_unix_call` as today; under `core` it calls `mtld3d_unix::dispatch(code,
  args)` linked into `d3d9.dll`, except the logging thunks (C4-8). `mtld3d-unix` becomes `crate-type = ["rlib"]` and
  `mtld3d.so` is built with `cargo rustc --crate-type cdylib` (meson `unix/meson.build`), unless Task 1's spike shows
  the two-type form links clean on all four PE targets. Under `core`, `mtld3d.dll` stays imported, so `mtld3d.so` loads
  idle (no thread, no Metal; its image and statics: 64-bit Unix memory on the Mac and under new WoW64, 32-bit under
  old WoW64). Cost if wrong: Task 1 finds it before anything depends on it.
- **C4-7 (window features under core in C4a).** On the Mac under `core`, the facade's `macdrv_wm.rs` stands in (no
  cursor overlay, view parking, client-surface bypass, EDR follow or screen-parameter filter; layer from winemetal's
  slot 72) until C4d. A core-lane conformance site worse for that reason goes in `dxmt/mtld3d-triaged-core.txt` with its
  cause; any other worse site is a C4a defect.
- **C4-8 (logging stays on mtld3d.so through C4) (flag).** `InitLogger`, `OpenLog` and `WriteLog` go to `mtld3d.dll`
  under both runtimes; the linked crate's `log::` calls reach the PE side's logger (one `log` facade per binary).
  `log_file.rs`'s file code and the three logging handlers are not built for PE targets; `log_file::append_stats`
  (`DXMT_STATS`) under `core` writes through the PE side's `\\?\unix\` path form, or the stats line is
  ledgered absent until C4c. Cost if wrong: none for C4; the Logger move is C5's prerequisite (ledger).
- **C4-9 (shader backend under core).** The core runtime honours `shader.backend`: AIR through winemetal's SM50 slots
  (as the VM's facade), MSL through slot 157. Gated lanes: mtld3d × default, mtld3d × msl, core × default.
- **C4-10 (the VM guest under core).** A `guest-vm` feature runs from `d3d9` through `mtld3d-unix` to `mtld3d-mtl`
  (and `mtld3d-core` where its guest cfgs live), set from meson's `guest_vm` as d3d11's is; every "VM guest" spelled
  `cfg(target_os = "linux")` becomes `any(target_os = "linux", feature = "guest-vm")` (Task 4 lists them); the PE
  side's `PAGES_FROM_UNIX` is set only under the guest cfg (the Mac's page boxes keep the PE allocator). Slot 77's
  bitcode reaches a 32-bit caller under new WoW64: the guest's x86_64 `winemetal.so` allocates it in low memory
  (`galloc`) when the process is marked WoW64 (MacNeutron `vm/metal/wm/slots.c:264-268`), which changes the guest image
  for every lane, so every VM lane is re-gated.
- **C4-11 (front-end cleanups out of C4).** The COM layer, Logger, `qi_table` and `PrivateData` gaps
  (`map-frontend.md` §3) are not C4's; the Logger is C5's prerequisite.
- **C4-12 (per-API policies stay D3D9's) (flag).** Draw skipping and pipeline prewarm stay in D3D9's planner ("per-API
  core policies" read as "chosen per API"). Cost if wrong: a later lift of the compile scheduler into the core.
- **C4-13 (what every C4 push keeps).** A meson option `d3d9_core_runtime` (default `false`) adds the planner to
  `d3d9.dll` (feature `core-runtime` on `d3d9`: `mtld3d-unix` and its `disk-cache`/zstd, forwarded `perf-tracking`
  and `perf-build`); MacNeutron's dev builds for C4's gates pass it on (`DXMT_D3D9_CORE=1`), the shipped build does
  not until C4e. So every C4 push keeps the shipped `d3d9.dll`, the default runtime, the translator key 051df2a0…,
  DXBC/DXIL output and D3D10-12 behaviour unchanged; the only shipped change is `winemetal.dll`'s one export. No tag,
  no release. Cost if wrong: none to the shipped build; the core lanes need the dev build.
- **C4-14 (32-bit title).** The VM measurement of a real 32-bit title is C4e's, with a game go-ahead asked for then.

## Review Focus

1. A 32-bit process under `core` in the VM: every SM50 output pointer and every no-copy buffer comes from low memory
   the guest marshals (Task 3's VM rows, the `page boxes: pages from the unix side` line).
2. A device created before the mode latches, a second device, a run-time load of `d3d9.dll` (the `d3d9_dynamic`
   row): one mode per process (Task 5).
3. Logging from the in-process planner before the PE logger is registered: lines dropped, never a crash.
4. Two runtimes in one process: under `core` no thunk but the three logging ones reaches mtld3d.so (Task 5's route
   test; Task 8's `encoder host: pe` lines and no `encoder host: unix`).
5. `WMTUnixCall` with slot 83, 158 or `u32::MAX`: refused on the PE side (Task 2's test).
6. Statics that were one per image and are now shared (the page allocator hook and live-byte counters,
   `core/src/page_box.rs:28-64, 287-293`, handed by address at `src/d3d9/src/encoder.rs:162`; `mem_watch`'s
   baseline): Task 4's inventory says each one's in-process meaning.
7. Unix paths reaching Win32 file calls under `core` (shader cache, present-gate file: `src/d3d9/src/encoder.rs:131-143`,
   `direct3d9.rs:2408`): a warm second run reads the cache (Task 8).

---

## Tasks

### Task 0: Ledger, plan, the maintainer's question

- [ ] Step 1: Create the plan's ledger with the pre-flight rows and every ruling; append "C4a started" to the program
  ledger with both branch tips.
- [ ] Step 2: Commit this plan and the three reviews' dispositions on `c4-mn`.
- [ ] Step 3: The open question above goes to the maintainer in chat (non-blocking).

### Task 1: link spike (answers C4-6 before anything depends on it)

Throwaway branch `c4-spike` off `c4`; nothing from it lands except the ledgered answers.
- [ ] Step 1: Give `mtld3d-unix` `crate-type = ["cdylib", "rlib"]`, make `d3d9` depend on it, stub the Windows errors
  with `#[cfg(not(windows))]` on whole modules, and run `tools/cargo-staticlib.sh` (meson's command) for `d3d9` on
  `aarch64-pc-windows-msvc`, `arm64ec-pc-windows-msvc`, `x86_64-pc-windows-msvc` and `i686-pc-windows-msvc`. Record:
  whether Cargo links a `mtld3d_unix.dll` (cargo#4881); the same with `["rlib"]` only; the staticlib sizes.
- [ ] Step 2: Link the ARM64X pair with `tools/arm64x-link.sh` from the two halves (no Wine run). Record success and the
  `d3d9.dll` size, its imports (`llvm-objdump -p`) against the shipped one's.
- [ ] Step 3: Ledger the answers as rulings (crate type; build form of `mtld3d.so`); delete the branch's worktree (the
  branch stays, per the never-delete-branches rule).

### Task 2: `WMTUnixCall`

**Files:** `src/winemetal/winemetal.h` (prototype outside the guest block), new `src/winemetal/src/call.rs` (the
export; `pub fn is_callable(slot: u32) -> bool`), `src/winemetal/src/lib.rs` (module), `winemetal.def`,
`winemetal-guest.def`; regenerate `src/winemetal/src/protos.rs` and `src/winemetal/sys/src/decls.rs`
(`tools/winemetal-layouts.sh --protos`, `--decls`; its `gen` leg also scans `call.rs` for `slot::`).
**Interfaces:** Produces `WMTUnixCall(slot: u32, params: *mut c_void) -> i32` (`extern "C"`, as every export) and
`winemetal_pe::call::is_callable`.

- [ ] Step 1: Write `call::tests::is_callable_refuses_83_and_past_the_table` (0..158 except 83 true; 83, 158,
  `u32::MAX` false) in `winemetal-pe`, and in the Unix crate (package `winemetal`, with the `dynamic_lookup`
  rustflags its tests need) `table::tests::only_slot_83_is_empty` (every `FUNCS[i]` but 83 is `Some`), which pins the
  predicate to the table. Run both. Expected: the first fails (no function), the second passes (the invariant holds
  today).
- [ ] Step 2: Implement; tests pass; `tools/rust-check.sh` ok.
- [ ] Step 3: Commit `winemetal: WMTUnixCall, a call by slot for a DLL with no unix lib of its own (D3D9's in-process
  runtime, C4a)`.

### Task 3: the facade's table on the PE side, and the guest's low bitcode

**Files:** `src/d3d9/mtl/src/table.rs` (one call function per table; a `cfg(windows)` loader), `src/d3d9/mtl/Cargo.toml`
(feature `guest-vm`), the facade's guest cfgs (`mtl/src/metal/command.rs:1363` and any other `target_os = "linux"`
meaning "guest"); `tools/rust-check.sh` (`-p mtld3d-mtl` in `PE_P`, placed before any `--features`). MacNeutron:
`vm/metal/wm/slots.c` (slot 77's bitcode from `galloc` when the process is marked WoW64) with a test in
`vm/metal/wm/tests` beside `sm50-args32-test.c`.
**Interfaces:** Consumes `WMTUnixCall`. Produces the same `Table` API on PE targets.

- [ ] Step 1: Write `table::tests::calls_reach_the_slot_with_the_params` (host, a stand-in call function records slot
  and pointer) and the MacNeutron test `sm50-bitcode-low-test` (a WoW64-marked process's slot-77 output pointer is
  below 2 GiB). Run `cargo test -p mtld3d-mtl --lib` and the C test. Expected: both FAIL.
- [ ] Step 2: Implement: Unix keeps `dlopen`/`dlsym`; `cfg(windows)` loads `winemetal.dll` and `WMTUnixCall`, the
  error path as C4-5; `mark_wow64` and `takes_table64` are no-ops on PE targets (Wine routes an i386 caller to the
  WoW64 table; an i686 build lays SM50's structs out 32-bit); the guest cfgs read `any(target_os = "linux", feature =
  "guest-vm")`. MacNeutron: the low allocation.
- [ ] Step 3: Host tests, the C test, and clippy on the PE targets clean. Commits: DXMTRS `d3d9: the mtl facade calls
  winemetal.dll on the PE side`; MacNeutron `vm: slot 77's bitcode below 2 GiB for a WoW64 process`.

### Task 4: `mtld3d-unix` builds for the PE targets (Opus subagent; this task is its brief)

**Environment:** `. build/c4/env.sh`; PE C env as `tools/rust-check.sh`'s `pe` function; baseline first: `cargo test
-p mtld3d-unix` (AIRCONV env) and record the test count.
**Files and what each needs (Windows = `cfg(windows)`; guest = `any(target_os = "linux", feature = "guest-vm")`):**
- `src/d3d9/unix/Cargo.toml`: crate type per Task 1's ruling; feature `guest-vm` (→ `mtld3d-mtl/guest-vm`,
  `mtld3d-core/guest-vm` if Task 4 finds core guest cfgs); `perf-tracking`/`perf-build` unchanged.
- `src/d3d9/unix/src/lib.rs`: the exported tables and `#[global_allocator]` off Windows only; `pub fn dispatch(code:
  u32, args: *mut c_void) -> i32` over `DISPATCH_TABLE` (out of range: -1); `crash_linux` under guest.
- `crash.rs`: not built on Windows (d3d9.dll's crash handler covers the process).
- `log_file.rs`: the file code and `init_logger_handler`/`write_log_handler`/`open_log_handler` not built on Windows
  (C4-8); `append_stats` per C4-8; the capture's `next_trace_path` (Windows: the `\\?\unix\` form or capture off,
  ledgered).
- `handlers.rs`: rusage and malloc-zone statistics report unknown on Windows; the guest arm (`:569-588`) under guest;
  `PAGES_FROM_UNIX` (`:216-219`) set only under guest.
- `metal.rs` (`:68-146`): page allocation: guest arm under guest (the `WMTMemoryAlloc` path); the Mac PE arm is never
  reached under `core` (no `PAGES_FROM_UNIX`): `unreachable` with a log line, not `VirtualAlloc`.
- `translation.rs`: Rosetta detection answers `None` on Windows (`:26`), the guest arm (`:51`) under guest.
- `mtld3d-core` `guest_pages.rs`, `owned.rs`, `page_box.rs`: lift the `cfg(not(windows))` off the `adopt*` API (the
  call sites at `encoder.rs:7237, 9905`, `encoder/ops.rs:367` stay as they are).
- `shared/src/identity.rs:111`: the Windows form.
- Statics inventory (Review Focus 6): list every `static` in `mtld3d-unix`, `mtld3d-core` and `mtld3d-shared` that the
  PE side and the Unix side each held a copy of, and what one shared copy means in-process; ledger it; fix only what
  double-counts (`mem_watch`'s live bytes).
- `tools/rust-check.sh`: `-p mtld3d-unix` in `PE_P` before any `--features`.
- Do not touch snmalloc's configuration (its `std::os::fd` line is a warning).
**Interfaces:** Produces `mtld3d_unix::dispatch`.

- [ ] Step 1: Add `-p mtld3d-unix` to `PE_P`; run the clippy leg. Expected: FAIL (the errors above, and more behind them).
- [ ] Step 2: Write `tests::dispatch_reaches_the_table` (host: the handler for `GetDeviceInfo` is the table's; out of
  range answers -1). Expected: FAIL (no function).
- [ ] Step 3: Port until clippy is clean on all four PE targets with and without `guest-vm`; host tests unchanged in
  count and green; the statics inventory ledgered.
- [ ] Step 4: `tools/rust-check.sh` ok. Commit `d3d9: mtld3d-unix builds as a library for the PE targets`.

### Task 5: the switch and the in-process dispatch in `d3d9.dll`

**Files:** `src/d3d9/core/src/config.rs` (+ tests): `RuntimeMode { Mtld3d, Core }`, field `runtime_mode`, key
`runtime.mode`; `pub fn thunk_route(code: u32, mode: RuntimeMode) -> Route { Mtld3d, InProcess }` in the same crate
(`mtld3d-core` already depends on `mtld3d-shared`'s `Thunks`); `mtld3d.conf`; `src/d3d9/Cargo.toml` (feature
`core-runtime` = dep `mtld3d-unix` with `disk-cache`; `perf-tracking`/`perf-build`/`guest-vm` forwarded with `?`);
`src/d3d9/meson.build` + `meson.options` (`d3d9_core_runtime`, default false, adding `core-runtime` to the d3d9
features and `DXMT_TRANSLATOR_KEY_H` to `d3d9_env`; `guest-vm` from `guest_vm`); `src/d3d9/src/unix_call.rs` (route);
`src/d3d9/src/lib.rs` (latch in `create_interface` after `config::load()`, before `install_page_allocator`);
`src/d3d9/src/encoder.rs:131-143` and `direct3d9.rs:2408` (under `core`, Windows paths for the shader cache and the
present-gate file); the encoder service's create handler logs `encoder host: pe` or `encoder host: unix` (a
compile-time fact of the module that runs, the gates' proof from outside the switch); the device's `runtime: <mode>`
line; the facade's load line `mtl facade: winemetal.dll (WMTUnixCall)`; `docs/d3d9/ARCHITECTURE.md` (the two
runtimes).
**Interfaces:** Consumes `mtld3d_unix::dispatch`. Produces `RuntimeMode`, `thunk_route`, `unix_call::runtime()`.

- [ ] Step 1: Config tests `runtime_mode_defaults_to_mtld3d`, `runtime_mode_core_in_the_file`,
  `mtld3d_config_env_overrides_runtime_mode`, `dxmt_config_runtime_mode_reaches_d3d9`,
  `runtime_mode_bad_value_warns_and_keeps_mtld3d`; route tests `thunk_route_keeps_logging_on_mtld3d`,
  `thunk_route_sends_every_other_thunk_in_process_under_core` (all 24 codes), `thunk_route_is_mtld3d_under_mtld3d`.
  Expected: FAIL (no field, no function).
- [ ] Step 2: Implement both; tests pass.
- [ ] Step 3: The PE wiring above; clippy on the PE targets with and without `core-runtime` (this crate has no host
  tests: clippy is the step's check, the gates are its behaviour check).
- [ ] Step 4: `tools/rust-check.sh` ok. Commit `d3d9: runtime.mode switches between mtld3d.so and the in-process
  runtime (built with d3d9_core_runtime)`.

### Task 6: build and link

Under the lock; `$D` at `c4`'s tip, restored after; the MacNeutron main checkout detached at `c4-mn`, restored by
trap.
- [ ] Step 1: MacNeutron: `DXMT_D3D9_CORE=1` passes `-Dd3d9_core_runtime=true` in the wine-arm64 and guest builds
  (a dev-build variable; the release scripts do not set it). Commit `wine-arm64, vm: DXMT_D3D9_CORE builds D3D9's
  in-process runtime`.
- [ ] Step 2: `make wine-arm64` with and without it. Expected: both link; without it `d3d9.dll` is byte-identical to a
  `c4`-less build of b3673ad4 except nothing (record); with it larger (record), imports unchanged; `winemetal.dll`'s
  exports gain exactly `WMTUnixCall` (`llvm-objdump -p` before/after, listed difference).
- [ ] Step 3: The guest stages and image with it (x86_64, i686). Expected: link; the image carries `winemetal.dll` in
  `system32` and `syswow64` beside `d3d9.dll` (add it in the image build if missing, same commit as Step 1).

### Task 7: gates with a runtime lane (MacNeutron)

The mtld3d lanes' names, stamps, verdict lines and outputs stay byte-identical; only the core lane is new.
**Files:** `dxmt/d3d9-check.sh` (`D3D9_RUNTIME=core`: `runtime.mode=core` in `DXMT_CONFIG`; one log folder per launch;
each row requires `encoder host: pe` and the facade's load line and forbids `encoder host: unix`; verdict `d3d9-check:
ok (core)`; the default verdict unchanged for `dxmt/check.sh` item 11); `dxmt/mtld3d-conformance.sh` (core lane:
`runtime.mode=core;log.dir=<dir>` in `DXMT_CONFIG` for one subtest to prove the runtime by the same two lines, then
`runtime.mode=core` for the run; output `run-core-default.txt`, stamp `... lane core`); `dxmt/d3d9-conf-compare.sh`
(`--base-lane <l> --cur-lane <l>`, defaults `msl`/`default`; findings `<cur>-worse` etc. with `air-` kept for the
defaults; `--triaged` per lane, `dxmt/mtld3d-triaged-core.txt` for C4-7 sites); `dxmt/tests/d3d9-conf-compare-test.sh`
(new cases `runtime-ok`, `runtime-swapped`, `runtime-worse`, `runtime-triaged`; the 21 existing unchanged);
`vm/dxmt/d3d9-gate.sh` and `vm/tests/d3d9.sh` (a `runtime` share file beside `backend`; core outputs
`conf-<leg>-<wow>-core.txt`; mtld3d.so's Linux unit tests in the mtld3d lane only; the core rows require the facade
line, `encoder host: pe` and, in the guest, `page boxes: pages from the unix side`).

- [ ] Step 1: Comparator cases first. Expected: FAIL on the new cases; implement; all pass.
- [ ] Step 2: Script lanes; mutation RED: the core lane with its `runtime.mode` entry removed fails every row on the
  `encoder host: pe` check.
- [ ] Step 3: Commit `dxmt, vm: D3D9 gates take a core runtime lane, proven by the module that ran`.

### Task 8: gates and the probe

Under the lock, as Task 6; no game.
- [ ] Step 1: Mac, shipped-path rows: `dxmt/check.sh` x64 and arm64ec (all passed), `wine-arm64/check.sh`,
  `presenter/check.sh`, `dxmt/wm-probe-check.sh` (both modes), the export comparison, translator key unchanged
  (`build/c3d/key.sh` against 051df2a0…, and the key line in the core lane's logs equal to it), DXBC/DXIL identity (13
  corpora), licences and notices (`mtld3d-notices.py` for the dev build's `d3d9.dll`), the layout32 checks
  (`dxmt/mtld3d-layout32-check.sh`, `vm/metal/layout32-check.sh`), the facade test (`dxmt/mtld3d-facade-test.sh`) and
  the loopback `d3d9-check` (`MTLD3D_SO`).
- [ ] Step 2: Mac D3D9: `d3d9-check` mtld3d × default, mtld3d × msl, core × default; conformance in the same three
  lanes; `d3d9-conf-compare.sh --base-lane default --cur-lane core` ok; a warm second core run reads its cache
  (Review Focus 7).
- [ ] Step 3: VM (rebuilt image, C4-10): `vm/dxmt/gate.sh`, `vm/d3d/gate.sh`, `vm/d3d/loopback-gate.sh`, the native
  winemetal gate (`vm/dxmt-native/gate.sh`), `present-gate` (guest and forward), `d3d-smoke` (old and new WoW64),
  `d3d9-gate` in the three lanes under old and new WoW64; the comparator per leg.
- [ ] Step 4: The probe (indicative): a fixed workload, `d3d9_triangle --draws 2000 --frames 600` (a MacNeutron
  test-program mode added in this task, no readback per frame), perf-tracking dev build; per lane, the planner
  thread's CPU time per frame (VM: `/proc/<pid>/task/*/stat` by thread name; Mac: `ps -M`), the process's peak virtual
  size (x86 lanes), and wall time; mtld3d vs core on Mac arm64x (base: the loopback facade `mtld3d.so`, so the only
  change is where the planner runs) and VM x64, x86 old WoW64, x86 new WoW64. `build/c4/probe.md`; a loss over 10% on
  a lane goes to the maintainer with C4b's plan; §7's 32-bit question stays open until C4e.

### Task 9: final review, fix pass, push

- [ ] Step 1: Fresh Opus reviewer of both branches (review package, this plan's Review Focus, the ledger's rulings);
  one fix pass (each Critical/Important RED→GREEN, suite green).
- [ ] Step 2: Push (standing authorization): DXMTRS `c4` → `main` (fast-forward), MacNeutron `c4-mn` → `main` with the
  pin bump (`dxmt/pins`) and the spec's §5 "C4a done" paragraph; privacy scan before each push; the pushed pin built
  (shipped form, no `DXMT_D3D9_CORE`) and `dxmt/check.sh` x64 run once more. No tag, no release.
- [ ] Step 3: Program ledger and memory (`dxmtrs.md`); the maintainer's message: the flagged rulings, the probe.

---

## Review record (2026-11-11)

R1 design (`build/c4/review-1-design.md`), R2 runtime (`review-2-runtime.md`), R3 execution (`review-3-execution.md`).
Every Critical and Important folded:
- R1-C1 optimizer reorder → C4-3's cost line and C4b's must-settle list. R1-C2 packet model → C4-1/C4-2 flagged with
  the real cost; the open question; C4b's first check; C4e decides the packet's fate.
- R1-I1, R3-I6, R2-I10 probe → Task 8 Step 4 (fixed workload, new WoW64, per-thread CPU, peak VM size, loopback
  base, indicative). R1-I2 → the loopback base. R1-I3, R3-I2, R2 minor latch → C4-4 (`create_interface`). R1-I4 →
  Review Focus 6, Task 4 inventory. R1-I5, R2-I3 paths → Task 5, Review Focus 7. R1-I6 → C4-5 and C4b. R1-I7, R3-I5
  `wmt-exports.sh` → the export comparison in Tasks 6/8. R1-I8 outline gaps → C4b/C4c lists. R1-I9, R2 minor
  `translation.rs` → Task 4. R1-I10, R2-I5, R3-I4 features → C4-13, Task 5.
- R2-C1 guest cfgs → C4-10, Tasks 3/4/5. R2-C2 bitcode → C4-10, Task 3. R2-R4, R3-C1 translator key → Task 5 (meson),
  Task 8 Step 1. R2-R6, R3-I3 crate type → Task 1 spike, C4-6. R2-R7 adopt API → Task 4. R2-R8 ABI → C4-5 (`extern
  "C"`). R2-R9, R3-I1 slot 83 → C4-5, Task 2. R2 minor/R3-I11 missing winemetal.dll → C4-5.
- R3-C2 conformance proof → Task 7 (`DXMT_CONFIG`, module-identifying lines). R3-I7 thread discrimination → `encoder
  host: pe|unix`. R3-I8 callers → Task 7's byte-identical mtld3d lanes, per-launch logs, triaged-core file. R3-I9 §8
  gates → Task 8 Steps 1 and 3. R3-I10 main checkout → Pipeline rules line, Tasks 6/8.
- Advisor (2026-11-11): Task 1 of the first draft (Mac WoW64 table) dropped (no caller, no gate); the link spike moved
  first; C4-13 rewritten (build option off in the shipped build); the four flagged readings put to the maintainer now.
- Minors ledgered, not folded: R1 (53 vs 43 call sites: 53 is grep's count of `unix_call(` including the definition
  and perf wrappers), R2 (`mem_watch` baseline: in Review Focus 6 anyway), R3 (framegen modes: C4a changes no presenter
  path; `present-gate` forward added).
