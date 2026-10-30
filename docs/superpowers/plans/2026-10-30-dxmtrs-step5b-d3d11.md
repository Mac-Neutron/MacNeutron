# DXMTRS step 5b: the D3D11 track (queue, encoder, D3D11 and the D3D10 layer, one switch) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `d3d11.dll` (Mac ARM64EC, guest x86_64 and i686) and the native `d3d11.dylib` become Rust in one switch:
D3D11's front end, the D3D10 object layer inside it, and the core's queue and encoder (5a's lane E) go live
together, every outside contract kept, every answer first recorded from the C++.

**Architecture:** Characterization first: a COM-level probe (`d3d11_probe`, step 3's `d3d11_dxgi` pattern), a Mac
A/B over 14 D3D11/D3D10 programs (today's 12 plus the new `d3d11_draw` and `d3d11_hazards`) through the VM gate's own
`ref.sh`/`compare.py`, cold-cache AIR rows, `wmtcmd` histograms and per-record dumps, the i686 programs in the guest,
and host goldens, all recorded from the C++. Then a skeleton fixes the crate's cross-lane API and generates the
~1,100 COM vtable slots with 5c's `--com`; twelve lanes port disjoint files on `step5b` (from 5a's `step5w`, after 5c's
groundwork Tasks 1-4, P23), behind a component feature `front-end` so nothing links live. The switch flips that
feature, deletes the C++, and rides the pin that moves the translator key anyway (step 4's Z or 4b); every gate runs
once.

**Tech Stack:** Rust 1.99.0 (staticlibs for `aarch64`, `arm64ec`, `x86_64`, `i686-pc-windows-msvc`,
`aarch64-apple-darwin`), meson, llvm-mingw, POSIX sh + `jq` (generators), C++17 (the probe, host harnesses),
winemetal's C API and `SM50*`, `sqlite3` (`/usr/bin`, read-only dumps), `python3 -I` (comparators).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (approved, 2026-10-30 revisions: §3 layout, §5 step 5
and its PACE paragraph, §6, §7, §8). Inputs: `build/step5/map/0-porting-map.md` (`M0`), `1-core.md` .. `5-d3d9-needs.md`
(`M1`..`M5`), `6-critic.md` (accepted), `7-decisions.md` (5-D1..5-D14, binding); plan 5a
`2026-10-30-dxmtrs-step5a-core-seams.md` (`5a`: S0, S1-S4, lanes R and E, its P-rulings and Task 20's gate block),
`2026-10-30-dxmtrs-step4a-translator.md` (`4a`: P7's key, lane P1's `airconv-ir`), `2026-10-30-dxmtrs-step3-d3d10core-dxgi.md`
(`step3`: dxgi's seams, the probe pattern, the whole-Rust-DLL meson recipe), `2026-10-30-dxmtrs-stepR-layout.md`,
`build/step2/reports/gates-{mac,native,vm}.md`, `build/pipeline.md`.

**Rebase note.** Written ahead on 2026-10-30 against DXMTRS da6ed2d (C++), `stepR-layout` dcf027f, `step3-s0` 4fb43ea
and plan 5a as written. Paths are the post-R layout (`src/<component>/`, one root workspace; step R is moving
`mtld3d/` to `src/d3d9` on `stepR-layout`); step 3's crates are `src/dxgi`, `src/d3d10` (its plan's `R/windows/…`
after R's rebase). It runs after 5a's Task 22 (main pushed, `step5w` handed off) and after 5c's Tasks 1-4 land on
`step5w` (P23). Task 0 Step 1 re-reads every path and line cited here, DXMTRS and MacNeutron (5c's plan by task and
name: its line numbers move), and ledgers each that moved.

Paths: `<M>` = `~/Documents/MacProton`, `$B` = `<M>/build`, `$D` = `$B/wine-arm64-src/dxmt`, `$S` =
`$B/step5b`, `$S5a` = `$B/step5a` (5a's kit), `$W` = `$B/eac-vm-work`, `$R` = `~/.rustup/toolchains`. Commands run
from `<M>` unless a step says otherwise. Ledger: `.superpowers/sdd/2026-10-30-dxmtrs-step5b-d3d11/progress.md`.

## Decisions

Binding (`7-decisions.md`), short form:

- **5-D1** Three plans; this is 5b (the D3D11 track: encoder and context with D3D11's front end and the D3D10 layer,
  the queue with it, one switch of `d3d11.dll`). 5b and 5c run in parallel after 5a's seams.
- **5-D2** S5 dropped: the queue's sequencing goes live with D3D11's switch (5a wrote it in lane E). Cost: the queue
  is not checked separately; Task 19 names the gate rows that pin each lane R/E behaviour live.
- **5-D3** P2 dropped: natively the dummy cbuffer keeps getting null from `aligned_alloc(65536, 4096)`; the Rust
  context passes null natively, as today.
- **5-D4** C++ asserts are `debug_assert!` (shipped behaviour since `b_ndebug=if-release`). Here: `D3D11_ASSERT`.
- **5-D5** The drawable prefetch stays off. D3D11 presents from the encode thread, as today.
- **5-D6** The switch deletes `src/d3d11/d3d11_shader.cpp` (a key input), so it rides the next pin that moves the key
  anyway (step 4's), never a key move of its own; after Z the key hashes `airconv`'s resolved dependencies, not
  `Cargo.lock`. D3D11 takes the DXBC signature parser from step 4's `airconv-ir`, never `libs/DXBCParser`.
- **5-D7** Forwarder headers stay at their paths: `src/d3d11/d3d11_interfaces.hpp` survives the switch (no DXMTRS C++
  includes it after it; the forwarder does).
- **5-D8** Natively `win32_event.c` exports the Win32 calls the Rust needs (5a Task 3 landed them).
- **5-D9** i386 `d3d12.dll`: 5c's. Here the i386 `d3d11.dll` is built, staged and gated the same way.
- **5-D10** Per task only its own checks; full gates once at this switch; then one fix pass; push and pin by the
  controller under the standing authorization.
- **5-D11** Spec amendments are 5a's; this plan adds its corrections to §5 at the end (Task 21).
- **5-D12** The milestone D3D11 title is asked before this plan's switch gates; no bench runs in 5b.
- **5-D13** The core's API stays shaped for D3D9 (C4): binding by index, sizes as config, no D3D11 type in `dxmt-core`.
- **5-D14** Three adversarial Opus plan reviewers.

Plan rulings (each: decision — why — cost if wrong):

- **P1 Branches.** DXMTRS `step5b` from `step5w`'s head as 5a Task 22 Step 6 ledgers it, 5c's Tasks 1-4 included
  (P23); lanes `step5b-<lane>` in `$S/wt-<lane>`; MacNeutron `step5b-mn` from `dxmtrs` (worktree `$S/wt-mn`), rebased
  per P24. `step5b` never reaches `main` before the switch. The W code 5b switches on (lanes R, E) lands with 5b's push;
  if 5c pushes first, `step5b` rebases onto it (shared W modules already on `main`). Lane D's code (5c's
  `src/d3d12/src/{barrier,…}`) rides along unreferenced while 5c has not switched; Task 18 Step 3's link maps show none
  of it is linked (no member of the `d3d12` crate's staticlib in any LB's `d3d11.dll`/`d3d12.dll`; a table diff
  against a reference that holds the same unreferenced code would prove nothing). — No dead D3D11 Rust ships before
  its switch (5a P1). — one rebase.
- **P2 The switch's base moves the key** (5-D6, 4a P7). The switch (Task 18) sits on `step5b` rebased onto the
  key-moving commit: Z's head on `step4a-translator` if Z has not pinned (both ride Z's bump, as 4a P7 allows), else
  `main` with 4b. If neither is ready when the lanes are, 5b holds at Task 17. — Players' caches turn over once per
  translator move, never for D3D11 alone. — 5b may wait for 4b.
- **P3 The key's D3D11 file.** `src/d3d11/src/shader/args.rs` holds exactly what decides a translation's output and its
  cache entry: the global flags (`d3d11.sampleNaNToZero` → `SM50_SHADER_FLAG_SAMPLE_NAN_TO_ZERO`, `d3d11.defuseFma` →
  `SM50_SHADER_FLAG_DEFUSE_FMA`, `d3d11_shader.cpp:11-23`), the metal version passed, each variant's argument chain,
  digest and function name (`"<vs|ps|cs|vshs|…>_" + sha1[0..8] + "_" + digest hex`). No scheduling, no Metal call. MacNeutron's
  `translator_key` (v2, 4a Z2's `lib.sh`; Task 16 edits it on Z2's commit, P24) hashes `src/d3d11/d3d11_shader.cpp`
  when it exists, else `src/d3d11/src/shader/args.rs`, and dies when neither does (older pins keep their key). — 4a P7
  asks 5b for "a compile-arguments file". — an unrelated edit to `args.rs` moves the key (review rule).
- **P4 Characterization from the C++, twice, before the port.** Early references (Task 2, on 5a's pushed `main`): the
  probe's goldens, its log file, the host goldens. Runtime references (Task 17, on the switch's base, right before the
  switch): Mac A/B, AIR rows, histograms, VM/native/present references, performance. Each recorded twice and equal;
  each new comparator gets one mutation RED. — Runtime outputs may move with the translator (Z, 4b); COM answers don't.
  — one extra lock job.
- **P5 Crate layout.** One crate `d3d11` (`src/d3d11`, staticlib, 5a S0) holds the front end, the D3D10 object layer
  (module `d3d10`: its wrappers live by value inside D3D11 objects and read their fields) and the D3D11-only core
  pieces 5a P14 handed over (`BindingSet`, subresource sets, the compile scheduler, `dxmt_names`, keyed mutex, D3DKMT
  shared-resource layouts). `dxmt-core` gains only what the queue or D3D9 shares: `device` (Metal-version choice),
  `frame_stats` (the queue writes it), and the encoder gaps Task 0's audit finds (`shader_cache::version` is 5c's,
  P12). — Fewest
  crates; one owner per concept. — a move at C4 if D3D9 wants the scheduler.
- **P6 COM objects.** `#[repr(C)]`, primary vtable first; each interface the C++ answers at another pointer
  (multiple inheritance: `IMTLMinLODClampable`, the device's `IMTLD3D11DeviceExt`/`IMTLSwapChainFactory`; tear-offs
  embedded by value: `MTLDXGIResource`, `MTLDXGIKeyedMutex`, `D3D11DXGISurface`, the D3D10 wrapper,
  `MTLD3D11ContextExt`, `D3D11Multithread`) is a vtable field at its own address, reached back by `container_of`.
  QI sets, pointer relations (`same=`), and the three refcount schemes (`ComObject` `com_object.hpp:40-73`, 5c's
  `dxmt_util::com::ComObject`; `ManagedDeviceChild` `d3d11_device_child.hpp:93-136`, zero valid, first public ref
  AddRefs the device, private zero `Recycle`s, `d3d11::com::Managed`; tear-offs forward) are the C++'s, recorded by the
  probe. Not step 3 D6's "same `this`". — rules.txt's
  companions and `q != obj` (`host/native.cpp:290`). — none.
- **P7 Vtables and D3D records generated, by 5c's `--com`** (P23). `sh tools/winemetal-layouts.sh --com <header>
  <module> <Interface>… "$LM/bin"` (5c Task 2's mode, landed on `step5w`) prints `src/d3d11/src/abi/vtbl.rs` from
  clang's `-ast-dump=json` of the C view (`-DCINTERFACE`) of the headers each build compiles against (llvm-mingw's
  sysroot on the four PE targets, `include/native` on darwin) for Task 3's interface list, read with `jq`: each
  `I…Vtbl` with every slot `unsafe extern "system-unwind" fn(..)` (P20), its per-target size and per-slot
  parameter-byte asserts. Task 3 adds to that mode a records list (`--records <Record>…` → `abi/records.rs`): each
  record the slots name, enums as their integer, unions as `union`, bitfields as one integer, `-fdump-record-layouts`
  size and offset asserts per target (the layouts mode's form); it fails on a type it cannot map. The records list is
  5b's addition to 5c's script, named in P23. — ~1,100 slots and ~100 records; spec §7 allows generation "where that
  saves work"; `jq` is installed, bindgen is not (installing it needs the maintainer); one generator for both front
  ends. — a records mode in 5c's script.
- **P8 One context, generic; captures live until the chunk retires.** `Context<M: Mode>` with `Immediate` and
  `Deferred` (the C++ template `#include`d twice, `d3d11_context_imm.cpp:7`, `_def.cpp:3`). Each of the 127 commands is
  written once as an `Fn(&mut Encoder) + Send + 'static` closure: `M::emit_st`/`emit_op` place it with `Chunk::emit`
  (immediate, runs once) or `CommandList::emit_fn` (deferred, may run again: `forward_rc`'s copy,
  `d3d11_context_def.cpp:25-27`). Core objects reach the encoder by reference, so the immediate path clones nothing per
  run. **Drop timing, a requirement on lane E that Task 6 changes:** a recorded command's captures drop when its chunk
  resets on the finish thread, after `waitUntilCompleted` and before `coherent` is signalled, on both paths, never when
  the command runs: the C++ runs a lambda on the encode thread and destroys it in `CommandList::reset`
  (`dxmt_command_list.hpp:62-71`), which `chunk.reset()` calls between the wait and `cpu_coherent.signal`
  (`dxmt_command_queue.cpp:196-213`). So `Chunk::emit` stores its closure as `Fn` (run by reference, dropped in `reset`;
  5a E1's `FnOnce` trampoline dropped it at the run), and `Chunk::retain` becomes generic (`retain<T: Send +
  'static>(T)`, dropped at reset). What must live that long, captured or `retain`ed: the present command's
  `SyncFrameState` (`d3d11_swapchain.cpp:719-740, 810, 828`), whose drop signals the waitable swap chain's
  frame-latency fence that `SyncFrame` waits on before the next present (`:752-759`); `ExecuteCommandList`'s private
  ref to the list (`d3d11_context_imm.cpp:606, 646-650`), whose release recycles it and frees its staging blocks
  (`d3d11_context_impl.cpp:5459-5488`, `d3d11_context_def.cpp:500-511`); every `Arc` a command uses. The C++'s raw
  captures (cached state objects `d3d11_context_impl.cpp:4881, 4892`; the swap chain's `this`,
  `d3d11_swapchain.cpp:808-833`) cross in one `SendPtr<T>` newtype whose SAFETY names both arguments: (a) the device's
  state caches own every state object until the device's `Drop`, which commits and waits for its queue and the compile
  scheduler before any field drops (Task 9; the C++ does not drain: `~MTLD3D11DXGIDevice` destroys `d3d11_device_`
  before `device`, `d3d11_device.cpp:1310-1312`, and `~CommandQueue` only stops and joins, `dxmt_command_queue.cpp:75-91`,
  so this is a deliberate change, ledgered); (b) the swap chain's `Drop` waits GPU idle (`d3d11_swapchain.cpp:208-209`)
  before its fields go, and `coherent` is signalled only after the chunk's reset (`dxmt_command_queue.cpp:212-213`), so
  no command still holds `this`; one recording thread per context under the device mutex
  (`dxmt_command_queue.hpp:245-250`). — map 2 §7.2; dropped at the run, the fence would signal at encode (the CPU runs
  further ahead) and a recycled list would overwrite copy sources the GPU has not read; no gate sees either (check.sh's
  latency rows are D3D12's). — a lane-E change in Task 6.
- **P9 The encoder state machine** (`CommandBufferState`, `PreDraw`, `InvalidateCurrentPass`) stays in `d3d11` (M0
  M2 Q3); C4 moves what D3D9 shares.
- **P10 Not implemented, natively** (B1). The 19 `UNIMPLEMENTED` sites (`d3d11_context_impl.cpp` 12, `_def` 2,
  `_imm` 1, `d3d11_device.cpp` 4; the macro is `d3d11_private.h:14-25`) log the C++'s text (P11), then on Windows
  `abort()`; natively they call `wmt_native_not_implemented()` and return `E_NOTIMPL` (HRESULT methods), nothing
  (`void`), or the call's failure value for inner paths (`Map` on a default resource: `E_NOTIMPL` from `Map`). The
  flag is a C `_Thread_local` in the native `winemetal.dylib` (`src/nativemetal/win32_event.c`), read and cleared by
  the exported `int MTLD3DNotImplemented(void)`, which the VM host `dlsym`s (optional) and checks after every
  forwarded call (gen.py's template, the six `host/native.cpp` sites), keeping `catch (...)` for D3D12's C++.
  5c's Task 1 lands the flag on `step5w` and 5c's `step5c-mn` the host plumbing (P23, P24); 5b adopts both. No method
  becomes implemented (B10: `rules.txt:442-460`, `coverage.py`'s `AS_DXMT` unchanged). Every one of the 19 sites is
  probed natively (Task 1's `notimpl`). **Unreachable.** `UNREACHABLE` (`d3d11_private.h:27-29`: a log, then
  `__builtin_unreachable`) logs P11's text, then returns the method's failure value (`E_INVALIDARG` for `Create*`,
  nothing for `void`); it never panics and never sets the not-implemented flag (sites `d3d11_context_impl.cpp:1266,
  3872, 3905, 3908, 3996, 4099, 4102, 4150, 4153, 4205, 4208, 4259`). `DXMT_UNREACHABLE` (`dxmt_context.hpp:40`, no
  log; `d3d11_context_impl.cpp:260`, `d3d11_device.cpp:97`) gets a listed defined result in step 3 D7's form, ledgered
  by Task 0 Step 3: `:97`, `CreateBuffer`'s default case on an invalid `Usage`, which an app reaches, returns
  `E_INVALIDARG` with the out pointer nulled; each is probed as a `NOT_ON_C` case (the C++ is undefined there). — Rust
  can't throw into the host's `catch`, and an `unreachable!()` would abort the game on PE and the VM host natively. —
  one gen.py change in this pin bump.
- **P11 Logging.** `dxmt_log` (5a P5) is defined in `d3d11`'s `lib.rs` over a `dxmt_util::log::Logger` named
  `d3d11.log` (`d3d11.cpp:31`), so the core's Rust logs into `<exe>_d3d11.log`. Texts are the C++'s, except the source
  location `UNIMPLEMENTED`/`UNREACHABLE` print (`__FILE__`, `__FUNCTION__`), which becomes `file!()` and the D3D11
  method; the log golden masks that field. No tool reads `d3d11.log` (grep of `dxmt`, `eac-vm`, `wine-arm64`,
  `presenter`, `release`). — none.
- **P12 The cache version: 5c's form** (P23). `dxmt_core::shader_cache::version(key_or_version: &str,
  airconv_version: u32) -> u64` (5c Task 3, on `step5w`) = `ShaderCacheVersion()` (`dxmt_shader_cache.cpp:11-19`):
  FNV-1a over the translator key (or `DXMT_VERSION` when empty), then `AIRCONV_VERSION`'s four bytes.
  `src/d3d11/build.rs` reads the generated `dxmt_translator_key.h` and `version.h` from paths `src/d3d11/meson.build`
  passes (`DXMT_TRANSLATOR_KEY_H`, `DXMT_VERSION_H`; the cargo custom target `depends:` on the `vcs_tag` target), exactly
  as 5c P8's `C/build.rs`, with its unset fallback, and emits them as `rustc-env` for the front end only;
  `AIRCONV_VERSION` is `winemetal_wire::AIRCONV_VERSION`. `dxmt-core` takes no build-time input. The C++ stays for
  `d3d12.dll` until 5c's switch. — `vcs_tag` writes `version.h` at build time, so there is no configure-time string to
  pass, and every Rust target shares one `rust_target_dir` (root `meson.build:221`): an env-dependent build script in
  `dxmt-core`, built by `d3d11`'s target with the variables and by `d3d12`'s without, would rebuild both fat-LTO
  staticlibs on every make. — none.
- **P13 Compile scheduler.** `d3d11::scheduler` is `task_scheduler` (`dxmt_tasks.hpp:31-173`: two workers growing to
  twice `available_parallelism`, continuations) with the process registry `MTLD3D11WaitIdle` polls every 1 ms for at
  most 10 s (`dxmt_tasks.cpp`, `d3d11.cpp:244-251`). Worker stacks 1 MiB on every target (≥ the C++'s: PE
  `CreateThread` 1 MiB, `thread.hpp:55`; std's 512 KiB natively), so translations keep 4a P4's room. **Every thread
  body the Rust spawns** (these workers; lane E's encode, finish and event-listener threads, Task 6) takes the core's
  abort-on-panic guard first (5a P19's guard, 5c P20's thread rule; no work on PE under `panic = "abort"`). — Under the
  native `production-unwind` a panic ends only its thread, where the C++ calls `std::terminate`: a dead worker leaves
  `GetPipeline` ("may block", `d3d11_context_impl.cpp:4843`) waiting forever on the encode thread, a dead encode or
  finish thread stops the ring and hangs the VM host's vCPU instead of aborting. — none.
- **P14 Forwarder contracts by rule** (M4 §1.4): B2 CPU-visible buffers and WRITE_DISCARD renames are allocated on the
  calling thread through winemetal's creation slot, and `Map` returns that buffer's own pointer; B3 Win32 events through
  `wmt_native_set_event`; B4 `GuestPaces` (natively, with WSI hooks, no frame-latency wait, `d3d11_swapchain.cpp:742-759`);
  B5 WSI hooks; B6 the device mutex (a recursive spin lock, a no-op until `SetMultithreadProtected(TRUE)`,
  `d3d11_multithread.cpp`) and `DO_NOT_WAIT` maps with `d3d11.ignoreMapFlagNoWait`; B7 pieces (native only,
  `E_NOTIMPL` on a deferred context); B9 companions (P6). — none.
- **P15 i686.** The i386 guest `d3d11.dll` from `i686-pc-windows-msvc`: stdcall slots and exports (`--kill-at`, step 3
  D11), `cfg(target_arch = "x86")` for the C++'s `__i386__` branches (`d3d11_buffer.cpp:101`,
  `d3d11_texture_dynamic.cpp:133`; the core's are lane R/E's), `sm50_ptr64_t` handles as `u64`, `SharedResourceData`
  and keyed-mutex names one layout on i686 and x86_64 (read across processes), variant digests hashing `size_t` at the
  target's width as the C++ does. UT runs only on `aarch64-apple-darwin`, so: layouts are `const` asserts (CK's i686
  and x86_64 clippy evaluate them); the digest takes the `size_t` width as a parameter, tested at 4 and 8; a `__i386__`
  branch is a function of the target, tested both ways on the host; and the D3D11 programs run as i686 in the guest
  (Task 1's `d3d11-guest.sh programs`: the C++ at Task 17 into `$S/ref-i386`, the Rust at Task 19). — No D3D11
  program ran as i686 before, so the i386 buffer, dynamic and linear texture, staging and occlusion-readback branches
  and an i686 cache were never exercised. — one guest run per side.
- **P16 Binary identity at the switch** (5a P16's form, new ED11 in `$S/ident-diff.py`): `d3d11.dll` name, machine,
  exports and ordinals (`@18 @22 @23 @24`), builtin marker, VERSIONINFO (`version.rc`) equal; imports ⊆ Task 2's
  C++ table ∪ Rust std's set (step 2's list) ∪ the Win32 calls the Rust makes, each dropped or added symbol listed;
  the C++ runtime's imports may go. Native `d3d11.dylib`: `nm -gU` = the `.def` names it defines today plus
  `MTLD3D11WaitIdle` (5a P16's list), `otool -L` equal, OSO 0, `$HOME` 0. — none.
- **P17 Deletions.** The switch deletes every C++ file of `src/d3d11` except `d3d11_interfaces.hpp`, and the D3D10
  layer's `src/d3d10/d3d10_{buffer,device,state_object,texture,util,view}.cpp` and `d3d10_{buffer,device,input_layout,
  query,shader,state_object,texture,util,view}.hpp` (not `d3d10_blob.cpp`: d3d12's). The core C++ (`src/dxmt`) stays
  while D3D12's C++ links it: `d3d12.dll` uses `InternalCommandLibrary` (`d3d12_device.hpp:29`, `d3d12_device.cpp:366`,
  `d3d12_swapchain.cpp:182`) from `dxmt_command.cpp`, whose object also defines `EmulatedCommandContext`,
  `DepthStencilBlitContext`, `MTLFXMVScaleContext` and `TileBarrierContext` and so pulls `dxmt_context.cpp`, and with
  it `dxmt_command_queue.cpp` and `dxmt_counter.cpp` (today's native `d3d12.dylib` defines `CommandQueue::CommandQueue`
  and `ArgumentEncodingContext::startRenderPass`). Its deletion goes by the link closure (`nm -u` of the objects what
  remains links pulls, never includes): at 5c's switch or Phase F, or in Task 18 Step 4 if 5c has switched first.
  `libs/DXBCParser`, `Metal.hpp` and step 3's kept dxgi headers stay for D3D12's C++ (5c). — Deletion over addition;
  d3d12's bytes unchanged. — the core C++ outlives `d3d11.dll`'s by one switch.
- **P18 Pre-switch linking.** `d3d11`'s exports, vtable statics and `dxmt_log` sit behind the crate feature
  `front-end`, set by a variable in `src/d3d11/meson.build` (`d3d11_front_end = false`, never a root option: key).
  Host tests and msvc builds (CK) run with it on; the switch sets it true. — The C++ and Rust both define
  `D3D11CreateDevice` otherwise. — none.
- **P19 Performance.** No Rust PGO (M0 M4 Q5). Each once (bench-once), Mac x64 and native: the per-command rows,
  `d3d11_draw.exe <S> perf` (Task 1): in-program QPC-timed sections, after one warm-up pass, of N=100k each of
  {bind+Draw}, {Map discard+Unmap+Draw} and {DrawIndexed}, a 10k-draw deferred list's record+Finish+Execute, and the
  encode thread's drain (a `Flush` after 100k recorded draws until an event query answers), each printed as ns per
  call under a prefix no gate compares (`perfinfo`); `present_loop`'s frame time (check.sh's `D3D11 frame time` info
  line, labelled display-bound); check.sh's timing lines; the probe's `full` call-rate lines. A row over 5% gets one
  control run on `$S/ref-wine.app` (natively `$S/ref-native`) in the same session (5a); a confirmed loss goes to the
  maintainer before the push (spec §8 parity). — Per-command encode cost is map 0 §4's first risk; `present_loop` is
  display-bound (step 2: 8.291 vs 8.298 ms), check.sh's timing lines are D3D12's and the translator's, and a program's
  wall time is the launcher's and a cold cache's. — none.
- **P20 Unwinding natively** (5a P19, 5c P20). Every vtable slot is `extern "system-unwind"` (5c's `--com`: `"C-unwind"`
  on darwin; under PE's `panic = "abort"` it behaves as `"system"`), every export natively `extern "C-unwind"`; each
  slot, export and thread body (P13) holds the core's abort-on-panic guard (so an Objective-C exception from winemetal
  reaches the VM host's `catch`, and a Rust panic aborts). The native staticlib builds `production-unwind`. The
  companion vtables D3D11 fills (`IMTLD3D11ContextExt`/`Ext1`/`Ext2`, `IMTLD3D11DeviceExt`, through which
  `TemporalUpscale` reaches MetalFX) are 5b's own `"system-unwind"` structs in `src/d3d11/src/abi/ext.rs` (Task 3),
  layout-asserted equal to `dxmt_util::ext`'s, whose slots stay nvngx's and nvapi's `extern "system"` callers' copy
  (5c P6's answer): a `"system"` slot would turn an Objective-C exception there into an abort of the VM host. — Keeps
  step 2's D4; one ABI string for both front ends. — a guard per call natively (no work unless panicking).
- **P21 The milestone title** (5-D12): the controller asks it at Task 0 and ledgers the answer; Task 19 needs it
  recorded (a title, or "none yet"). The bench is spec §8's end-of-step-5 milestone, run only with a go-ahead.
- **P22 SM50 declarations and chain mirrors.** 5c Task 1's `winemetal_sys::sm50` (on `step5w`, P23) declares the 12
  `AIRCONV_API` functions (`airconv_public.h:325-366`) with the mirrors D3D12 passes, pinned by `sm50-abi.h` and
  `--seam-abi`, and ports `SM50GetErrorMessageString`'s 256-byte cut as `sm50::error_message`. Task 5 adds to that
  same pair whatever of D3D11's set it lacks (the nine argument-chain structs, `SM50_COMPILED_BITCODE`,
  `SM50_IA_INPUT_ELEMENT` with its bitfields as one LSB-first `u32`; Task 0 Step 1 lists the gap), with per-target
  asserts. 4a's `airconv` keeps its own `abi.rs` mirrors (it cannot be a PE dependency: it links LLVM); listed as a
  duplicate. `GetArgumentIndex` (`airconv_public.h:201`) is re-implemented. — 5a P12 left `SM50*` to the front ends;
  one declaration set. — none.
- **P23 The 5c groundwork handshake** (mirrors 5c P2; one owner per item, decided). 5c's Tasks 1-4 land on `step5w`
  before Task 0 cuts `step5b`; both controllers order it so (program ledger); if they land later, `step5b` rebases
  past them (5c P2) and no 5b task re-implements one meanwhile, coding against 5c's declared names. 5c owns:
  `winemetal_sys::sm50` and the native not-implemented flag (`wmt_native_not_implemented`, `MTLD3DNotImplemented`,
  tested in C by 5c's `win32-test.cpp`; 5c Task 1); `--com`, `dxmt_util::com::ComObject`, `dxmt_util::ext`,
  `dxmt_util::private_data` and the QI-error latch (5c Task 2); `dxmt_core::shader_cache::version` and the
  generated-header `build.rs` form (5c Task 3, 5c P8); `dxmt_util::wsi` (5c Task 4). 5b therefore has no
  `tools/com-vtables.sh`, no `com::Counts`, no SM50 declarations, flag or env plumbing of its own; 5b's Task 3 and every
  lane use the landed names. 5b keeps, named here and ledgered for 5c's controller at Task 0: `dxmt_util::sha1` (5b
  Task 4); the WSI calls 5c's `wsi` lacks (`is_foreground`, `WsiMode`, any other Task 0 Step 1 finds; 5b Task 4);
  `--com`'s records list (5b Task 3, P7); the chain structs `sm50` lacks (5b Task 5, P22); `d3d11::com`'s `Managed`
  scheme (`ManagedDeviceChild`, `d3d11_device_child.hpp:93-136`: zero valid, the first public ref AddRefs the device,
  private zero `Recycle`s; not `ComObject`'s), `TearOff`, `SendPtr`, `container_of!`, `iface_of` (5b Task 3). — Two
  designs of one API cannot be reconciled by a rebase, and lanes would code against names that never exist. — 5b's
  start waits on 5c's groundwork.
- **P24 MacNeutron's base follows the switch's base.** `step5b-mn` holds Task 1's programs and Task 16's edits; both
  run with the scripts the switch's DXMTRS tree needs. Riding Z (P2): before Task 17 `step5b-mn` rebases onto
  `step4-mn` (Z2's `lib.sh` key v2, the `mtld3d-tools.sh` `PATH` and `cargo fetch` order, `dxmt/tools.sh`, `make
  llvm-nolto`), Task 16 commits on Z2's commit, and 4a Z5 squashes both into one pin bump. Riding 4b: it rebases onto
  `dxmtrs` after 4b's pin. Either way it also rebases past what `step5c-mn` landed on `dxmtrs` (the flag's
  `gen.py`/`host/native.cpp`/`host/dispatch.c` plumbing, `build-tests.sh`): Task 16 then adopts 5c's plumbing and
  keeps only the D3D11 key entry and its test rows. From that rebase on, RL/RC take Z2's inputs
  (`AIRCONV_LLVM=$PWD/build/wine-arm64-src/llvm-arm64-nolto AIRCONV_XCRUN=$PWD/wine-arm64/tools/xcrun-metal.sh`), one
  `make llvm-nolto` lock job runs before the first RL on the rebased `step5b` (Task 17 Step 0), and `$S/lane-build.sh`
  is re-checked against Z1's meson environment before Task 17 Step 0's LBs. — Z's tree built with `dxmtrs`'s scripts
  fails (4a T1's `${AIRCONV_LLVM:?}`, the v1 key's `d3d11_shader.cpp` entry), and two `*-mn` squashes from one fork
  conflict. — one or two rebases of `step5b-mn`.
- **P25 `airconv-ir` through a scaffold** (5c P3's form). Lane `st`'s branch starts at one scaffold commit
  (`scaffold: airconv-ir from step4a-translator <sha>`: `git checkout <4a P1's sha> -- src/airconv/ir`; its root
  `members`, `default-members` and `[workspace.dependencies]` lines added by the controller; `Cargo.lock` from one
  `cargo metadata --offline`; nothing of 4a's T1). The input-layout commit is the lane's last. The lane's other commits
  rebase onto `step5b`, re-run CK there and merge; the input-layout commit rebases onto `step5b` once the switch's base
  has Z (P2), the scaffold dropped. The scaffold moves the key (before Z the key hashes `src/airconv` wholesale): the
  one exception to the key rule, ledgered, never on `step5b`. — 5-D6 forbids the C++ copies; a merge of
  `step4a-translator` would bring T1's root, `rust-check.sh`, layouts and CONVENTIONS edits into a lane. — Task 8's
  input layouts land only after Z.
- **P26 Record dump knob.** `DXMT_WMTCMD_DUMP=<file>` beside 5a P10's histogram knob (winemetal's Rust Unix side, Mac
  and native): one line per encoded record with every field, handles renumbered by first appearance, GPU addresses as
  (buffer, offset) through a range map of the buffers winemetal created; unset, one latched flag test per
  `*_encodeCommands` call. Recorded from the C++ twice (Task 2 Step 4 for the lanes' slices, Task 17 for the gate),
  positions differing between the two runs masked; Task 17 diffs the two references and re-cuts the slices on a
  change. — The histograms count (encoder, record type) only, so a right record with a wrong viewport, scissor, depth
  bias, blend constant, stencil ref, LOD bias, argument offset, blit region or index offset passes them (the D3D11
  analogue of 5a review C2). — a second dev knob in the shipped image.

## Global Constraints

- Agents never push, merge to `main`, fetch or download. Task 21's push, pin and pinned re-fetch run under the
  maintainer's standing authorization, by the controller only. No MacNeutron.app, Steam or game; nothing under
  `~/Library/Application Support/MacNeutron` (7-decisions allows none). Kill by exact PID. No bench (5-D12). Home
  written `~`; privacy scan before every push. Commits end with `Co-Authored-By: Claude Opus 5.5
  <noreply@anthropic.com>`; DXMTRS subjects `<area>: sentence`.
- **The lock (`build/pipeline.md`).** Every build or install of MacNeutron's runtime or a DXMT build into the shared
  folders, every gate or check reading installed outputs, and every checkout in `$D` runs as ONE job:
  `/usr/bin/lockf -k <M>/build/pipeline.lock sh -c 'git -C $D checkout -q --detach <sha> && <build> && <tests>'`,
  logging the commit and wine.app's cdhash; nothing left in `$D`; never waiting on a review. A job needing MacNeutron
  at `step5b-mn` starts with `git -C <M> checkout -q --detach step5b-mn` and ends (`trap … EXIT`) with `git -C <M>
  checkout -q dxmtrs` (5a Task 20's form; rule 6 keeps VM jobs on the main checkout's `eac-vm/builder.sh`); from
  Task 17 on, `step5b-mn` is P24's rebased branch. Editing only in worktrees; MacNeutron worktrees export
  `BUILD_DIR=<M>/build` in the environment. A host-only command that reads `$D` or installed outputs by default (such
  as `make -C eac-vm/d3d`) is given its inputs explicitly from a worktree or a private copy, never the defaults.
- `make wine-arm64`, `eac-vm/dxmt/build.sh`, `eac-vm/host/build.sh` need `MACNEUTRON_SIGN_IDENTITY` and
  `MACNEUTRON_PROVISIONING_PROFILE`; the builder starts with `VM_SHARES` (builder-out-share note); VM commands export
  `MN_STATE=<M>/build/eac-vm-work/state/step2-gates.img`.
- **Rust.** `PATH=$R/1.99.0-aarch64-apple-darwin/bin:$PATH`, `--offline`, `CARGO_TARGET_DIR=$S/cargo-<lane>`.
  - **UT(<crates>)**: 5a's form (`--target aarch64-apple-darwin`, `-Wl,-undefined,dynamic_lookup`), `d3d11` with
    `--features front-end`.
  - **CK(<crates>)** (each task's check): nightly `cargo fmt --all --check`; `cargo clippy --offline -p <crates>
    --all-targets -- -D warnings`, `d3d11` with `--features d3d11/front-end` (added only when `d3d11` is among
    <crates>: cargo refuses a `pkg/feature` of an unselected package), on `aarch64-apple-darwin` and the four
    `-pc-windows-msvc` targets each crate ships on; `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps -p <crates>`; UT;
    `git -C <wt> add -A && sh tools/audit.sh` → `audit: clean (<n>)`. **RL** (5a's full `tools/rust-check.sh` form)
    runs only at Tasks 3, 17 Step 0, 18 and 19 (**RC** on `$D`); from P24's rebase on, both carry Z2's inputs
    (`AIRCONV_LLVM=$PWD/build/wine-arm64-src/llvm-arm64-nolto AIRCONV_XCRUN=$PWD/wine-arm64/tools/xcrun-metal.sh`).
  - **LB(<kind>) [p]**: `sh $S/lane-build.sh <wt> <mac|native|guest64|guest32> [p]` → `lane-build: <kind> ok` (5a
    P11's script). Host work, no lock.
- **Translator key** (5a's form): equal Task 0's value before and after every DXMTRS commit up to Task 17 Step 0's
  rebase, then equal to P2's base's value up to Task 18's switch; no edit of
  `include/`, the root `meson.build`/`meson.options`, `src/airconv`, `libs/DXBCParser`,
  `src/winemetal/airconv_thunks.{c,h}`, nor (before Task 18) `src/d3d11/d3d11_shader.cpp`. One exception: lane `st`'s
  scaffold commit and what sits on it (P25), lane-only, ledgered, never on `step5b`. At the pin it moves exactly once,
  to the value P2/P3 give.
- Conventions (`docs/CONVENTIONS.md`): no `#[allow]`/`#[expect]`; `unsafe` one operation a block with `SAFETY:`; no
  `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; `FxHashMap`; derives only with a callsite through
  `tools/audit.sh --update-derives`; statics `LazyLock` or `Once` only.
- Behaviour is the C++'s wherever anything outside the DLL sees it: HRESULTs, out-parameter bytes, QI answers and
  pointer relations, refcount return values, files, log text (P11), cache paths, keys and table names, thread names
  (`dxmt-encode-thread`, `dxmt-finish-thread`).
- zsh: quote `"====="`; gating scans use `/usr/bin/grep`.

## Execution

**Roles.** The controller runs the plan, creates worktrees, merges lanes (rebase, fast-forward, `git cherry` empty
before a branch goes), re-runs `--update-derives` and resolves `Cargo.lock` after each merge (`cargo metadata
--offline` once, then `--locked`; never `cargo update`), keeps the ledger, and asks 5-D12. One **pipeline owner**
runs every lock job (Tasks 2 Steps 3-4, 17, 18 Step 5 and Step 1 if the base moved, 19, 21 Steps 2 and 4).
**Lanes** do host work only.

**Lane procedure** (5a's): worktree from `step5b` at the commit the task names; `include/native/directx` and
`external/nvapi` filled read-only; RED, GREEN, CK, commit. A lane touches only its files; a needed change elsewhere
goes to the controller. Shared files (root `Cargo.toml`/`.lock`, `src/d3d11/Cargo.toml`, `src/d3d11/build.rs`,
`rustfmt.toml`, `tools/rust-check.sh`, `tools/audit.sh`, `src/d3d11/src/lib.rs`, `src/d3d11/src/context.rs`,
`src/dxmt/src/lib.rs`, `src/dxmt/src/encoder.rs`'s `mod` lines) are Task 3's. **Commits:** one per module file, each
with its own tests (`<stem>/tests.rs`), never one per lane; Tasks 7 and 8 name their groupings.

| Task | Runs | Model | Depends on | Owns (disjoint) | Lock |
|---|---|---|---|---|---|
| 0 base, kit, audit | owner | Sonnet | 5a Task 22; 5c Tasks 1-4 on `step5w` (P23) | `$S/*`, ledger | no |
| 1 programs, probe, harness | lane `mn` | Opus | step 3 on MacNeutron `dxmtrs` | MacNeutron `dxmt/tests/{d3d11_probe,d3d11_draw,d3d11_hazards}.cpp`, `dxmt/tests/{d3d11_deferred,d3d11_transfer,dxbc}.cpp` (cases, shaders), `dxmt/check.sh` (rows), `eac-vm/dxmt/{ref.sh,invocations}`, `eac-vm/dxmt-native/gate.sh` and `eac-vm/d3d/loopback-gate.sh` (`ROWS`), `eac-vm/tests/d3d11-guest.sh`, `eac-vm/dxmt-native/build-tests.sh` | no |
| 2 early references | owner | Sonnet | 0, 1; Step 4: 5 (lanes 7-10, 14, 15 need Steps 1-3; 11-13 Step 4) | probe goldens, `dxmt/tests/shaders/d3d11/*.dxbc` (new), `$S/golden/*`, `$S/ref-dump-early`, `$S/include-native` | Steps 3, 4 |
| 3 skeleton, generator | owner | Opus | 0 | shared files above, `--com`'s records list in `tools/winemetal-layouts.sh`, `src/d3d11/src/{abi,com}…`, every stub (`src/dxmt/src/encoder/gaps.rs` included) | no |
| 4 util | lane `util` | Sonnet | 3 | `src/util/src/sha1…`; additions to `src/util/src/wsi…` (P23) | no |
| 5 sys | lane `sys` | Sonnet | 3 | additions to `src/winemetal/sys/{sm50-abi.h,src/sm50/…}`; the dump knob in `src/winemetal/unix/src/histogram{.rs,/…}` (P26) | no |
| 6 core | lane `x` | Opus | 3, 0's audit | `src/dxmt/src/{device,frame_stats}…`, `src/dxmt/src/encoder/gaps.rs` (body), `src/dxmt/src/arena.rs` (P8's drop timing), `src/dxmt/src/queue.rs` (P13's guard, the statistics) and `encoder.rs` (new fields only): lane E has merged, no other owner | no |
| 7 resources | lane `rs` | Opus | 2, 3, 6 | `src/d3d11/src/{resource,view,surface,keyed_mutex,kmt,subresource}…` | no |
| 8 shaders, pipelines | lane `st` | Opus | 2, 3, 4, 5, 6; input layouts: P25 | `src/d3d11/src/{state,input_layout,shader,pipeline,scheduler}…` | no |
| 9 device | lane `dv` | Opus | 2, 3, 6 | `src/d3d11/src/{exports,device,inspection,multithread,annotation,class_linkage,names}…` | no |
| 10 context state | lane `cs` | Opus | 2, 3 | `src/d3d11/src/context/{state,setters,getters,binding_set,context_state}…` | no |
| 11 context encoding | lane `cd` | Opus | 2, 3, 6 | `src/d3d11/src/context/{encode,draw,dispatch,pipeline_select,gs_ts}…` | no |
| 12 context transfers | lane `ct` | Opus | 2, 3, 6 | `src/d3d11/src/context/{copy,update,clear,resolve,mips}…` | no |
| 13 contexts, lists, maps | lane `cx` | Opus | 2, 3, 6 | `src/d3d11/src/context/{immediate,deferred,command_list,ext,map,sync}…` | no |
| 14 swap chain, queries | lane `sw` | Opus | 2, 3, 4, 6 | `src/d3d11/src/{swapchain,query,fence}…` | no |
| 15 D3D10 layer | lane `d10` | Sonnet | 2, 3 | `src/d3d11/src/d3d10{.rs,/…}` | no |
| 16 MacNeutron same-pin | lane `mn` | Sonnet | Z2's commit (P24) | `wine-arm64/{lib.sh,tests/translator_key_test.sh}` (the D3D11 entry); `eac-vm/d3d/{gen.py,host/native.cpp,host/dispatch.c,tests/test_notimpl.py}` only if `step5c-mn` has not landed them | no |
| 17 base references | owner | Sonnet | 7-15 done (merged in its Step 0), P2's base, P24's rebase, 5-D12 asked | `$S/ref-*` | yes |
| 18 the switch | owner | Opus | 16, 17 | `src/d3d11/meson.build`, deletions, `src/dxmt/meson.build` (Step 4 only if 5c has switched) | Step 5 (Step 1 if the base moved) |
| 19 gates, once | owner | Sonnet | 18 | — | yes |
| 20 one fix pass | owner, lanes | Opus | 19 | as needed | as needed |
| 21 spec, push, pin | controller | — | 20 | spec, pins, ledgers | Steps 2, 4 |

**What starts before its dependencies land.** Task 1 starts now (C++ and step 3's probe only). Task 3 may start on
`step5w` before 5a's push once 5a's S0 and lanes R1, E1 have merged there, coding against the W lanes' declared
signatures and against 5c Tasks 1-4's names (merged there, or declared in 5c's plan and rebased past when they land,
P23), and rebases onto Task 0's base. Task 2's DXMTRS golden data is committed by the lane that reads it (7-15, `x`
excepted), so those lanes'
golden tests wait for Task 2 (a test with no golden names the Task 19 row that pins it). Lanes 8 and 14 need Task 4
(SHA-1, the WSI additions); 8 needs Task 5's chain mirrors, and its input-layout module needs 4a lane P1's
`airconv-ir` through P25's scaffold (that commit last, rebased onto `step5b` once Z is on the switch's base; the rest
of lane `st` rebases off the scaffold, re-runs CK and merges normally). Lanes 11-14 need Task 6's gaps: each codes
against `encoder/gaps.rs`'s stub signatures from Task 3 and rebases once Task 6 merges. Merge order on `step5b`: 3, 4,
5, 6, then 10, 15, 7, 9, 8, 12, 11, 13, 14 (each as it finishes; the order only breaks ties). Step 4's and 5c's lanes
also edit the root manifest, `Cargo.lock`, `tools/rust-check.sh`, `tools/audit.sh`, `dxmt-util` and `winemetal-sys`:
whichever lands on `main` second rebases mechanically (5a's rule; `cargo metadata --locked` and `cargo tree` of the
shipped packages checked after it). Critical path: Task 3 → 6 → 11 and 13 → 17 → 18 → 19.

## Rules check

- **Statics** (each a process-wide fact of the C++):
  - `LOG: LazyLock<Logger>` in `lib.rs` (`Logger::s_instance("d3d11.log")`, one per DLL as today).
  - `FLAGS: LazyLock<u32>` in `shader/args.rs` (the `call_once` static of `getGlobalShaderFlag`, `d3d11_shader.cpp:12-22`).
  - `SCHEDULERS: LazyLock<Mutex<Vec<…>>>` in `scheduler.rs` (`dxmt_tasks.cpp`'s `schedulers` and its lock).
  - The QI-error log-once set, moved with dxgi's `com.rs` into `dxmt_util::com` (5c Task 2, P23; `com_guid.cpp:45-46`).
  - `DUMP: LazyLock<Option<Mutex<File>>>` in winemetal's `histogram.rs` (P26, beside 5a P10's histogram latch).
  - Config: 5a's `dxmt_util::config` `LazyLock`. Not Rust: the not-implemented flag (C `_Thread_local`, 5c Task 1, P10).
  - Per device, not static: state-object caches, the pipeline cache, the command-list pool. Test-only: winemetal
    fakes under `cfg(test)` as 5a's, `#[unsafe(export_name)]` snake_case functions.
- **Config keys:** none new. Read: `d3d11.maxFeatureLevel`, `d3d11.sampleNaNToZero`, `d3d11.defuseFma`,
  `d3d11.ignoreMapFlagNoWait`, `d3d11.maxTessFactor`, `d3d11.metalSpatialUpscaleFactor`,
  `d3d11.preferredMaxFrameRate`, `dxgi.handleAltTab`, `dxmt.shaderMetalVersion` (grep of `getOption<` over `src/d3d11`,
  `src/d3d10`, `src/dxmt`). Environment: `DXMT_METALFX_SPATIAL_SWAPCHAIN` (here), the capture, cache and log variables
  through 5a's modules; one new dev knob, `DXMT_WMTCMD_DUMP=<file>` (P26: the gates' record dumps need it). Build-time:
  `DXMT_TRANSLATOR_KEY_H`, `DXMT_VERSION_H` (paths to the generated headers, P12, 5c P8's names).
- **Wire fields:** none changed. New mirrors: the chain structs `sm50` lacks (P22), the generated D3D/DXGI records (P7),
  `SharedResourceData` (`d3d11_texture_device.cpp:346-355`) and the keyed mutex's 54-byte names
  (`dxmt_keyed_mutex.cpp:49-56`), `const`-asserted equal on i686 and x86_64 (P15).
- **Dependencies:** `d3d11` → `dxmt-core`, `dxmt-util`, `winemetal-sys`, `winemetal-wire`, `bitflags`, `rustc-hash`,
  and `airconv-ir` (path; on lane `st` through P25's scaffold, added to `step5b` by Task 8's input-layout commit only
  after Z: the folder `src/airconv/ir` is not on `step5b`'s base before, and a path to a missing folder fails `cargo
  metadata`); no build-dependency (`build.rs` reads two files with `std`, P12); features `perf-build`, `guest-vm` (5a
  P17) and `front-end` (P18). `dxmt-core`, `dxmt-util`, `winemetal-sys`: no new edge, no `cc` anywhere (the flag is
  tested in C by 5c; SHA-1 is hand-ported from `src/util/sha1/sha1.c`, public domain, Steve Reid). No crate new to
  `Cargo.lock`. No licence change.
- **Derives:** `Sha1Digest([u8; 20])` `Clone, Copy, PartialEq, Eq, Hash` (a map key, as the C++'s
  `unordered_map<Sha1Digest, …>`; 20 bytes, listed); small `Copy` flag and id newtypes. No derive on a generated
  record: cache keys over descriptors get hand-written `PartialEq`/`Hash` with the C++'s `operator==` (floats by
  value). Each through `--update-derives`.
- **Lint suppressions:** none. Generated, in `rustfmt.toml`'s `ignore`: `src/d3d11/src/abi/{vtbl,records}.rs`.
- **Duplicates (for §7 and 5c):** Rust `dxmt_util::wsi` window functions vs `src/util/wsi_*` C++ (the forwarder's guest
  compiles the C++, 5-D7); `dxmt_util::sha1` vs `src/util/sha1` (D3D12's C++); `shader_cache::version` (5c's) vs
  `ShaderCacheVersion()` (P12); `sm50`'s chain mirrors vs `airconv`'s `abi.rs` (P22); `dxmt_util::ext` vs
  `d3d11_interfaces.hpp` (kept for the forwarder, layout-asserted by 5a); the not-implemented flag vs D3D12's C++
  `throw` (until 5c); `d3d11::com::Managed` vs 5c's `ComObject` (the C++'s two schemes, P23); `--com`'s generated
  vtables vs D3D9's hand-written ones (C4). Retired: dxgi's own `private_data`, `wsi` and QI latch (moved by 5c Tasks 2
  and 4); 5b's planned `tools/com-vtables.sh`, `com::Counts` and P12 env plumbing (never written, P23).

## Review Focus

1. **A 32-bit D3D11 game in the VM.** The i686 `d3d11.dll` runs Rust COM for the first time: stdcall slots, `CpuPlaced`
   buffers, `u64` translator handles, shared textures opened by an x86_64 process. Expected: it creates, draws, maps,
   presents and shares as the C++. Tests: Task 3's i686 vtable/record asserts; Task 7 `kmt`'s `const` layout asserts
   (CK's i686 clippy) and `resource::tests::cpu_placement_per_target` (both targets on the host); Task 8
   `args::tests::digest_hashes_size_t_at_both_widths`; Task 19's i686 programs in guest mode against `$S/ref-i386`
   (compare.py, and air-compare against the C++'s i686 rows), the probe's `d11share` (an i686 child of an x86_64
   parent in the guest), d3d-smoke's `32:` blocks (both modes, `MN_WOW64=new`) and `d3d11-guest.sh i386`.
2. **Recording from several threads.** Deferred contexts on worker threads, `ID3D11Multithread` turns, a command list
   executed twice, lists that hold queries, staging copies, updates and no-overwrite maps. Expected: no torn state, each
   list's records twice, Execute re-issuing the list's queries and marking its staging at the current sequence, Finish
   ending open queries, a list recycled only after the GPU is done with it, the device lock recursive for its owner.
   Tests: Task 9 `multithread::tests::{off_until_protected, recursive_for_the_owner, unprotect_resets_owner}`; Task 13
   `command_list::tests::{runs_twice_with_the_same_records, dynamic_names_per_list, restore_state_vs_clear,
   executed_list_recycles_after_completion}`; Task 19's `threads.sh` (`threads11`), `d3d11_deferred` (its `lists`,
   `shared`, `mixed` and the new `twice`, `restore`, `query`, `staging`, `update`, `nooverwrite` cases) in the Mac A/B,
   native and VM gates.
3. **Maps a game polls.** `DO_NOT_WAIT` on a busy resource, WRITE_DISCARD then NO_OVERWRITE, maps of default resources.
   Expected: `DXGI_ERROR_WAS_STILL_DRAWING` without a flush (`d3d11_transfer`'s header), the rename on the calling
   thread (B2), `d3d11.ignoreMapFlagNoWait` waiting, default-resource maps refused as the C++ (P10). Tests: Task 13
   `map::tests::{do_not_wait_on_busy_answers_still_drawing, ignore_no_wait_waits, discard_renames_on_the_caller,
   default_map_is_not_implemented}`; `d3d11_transfer` (its dynamic-buffer `UpdateSubresource` cases included) and
   `d3d11_dynamic` A/B; the probe's `notimpl` over all 19 sites; `pieces.sh`.
4. **The first run after the update.** A cold shader cache under the moved key. Expected: the same AIR per (shader,
   variant) as the C++, the table named by the new key, a D3D11 and a D3D12 program sharing one table. Tests: Task 8
   `args::tests::{digests_and_names_are_the_cpps, chains_are_the_cpps}`; 5c Task 3's
   `shader_cache::tests::version_is_the_cpps` (P12); Task 1's shared-table row; Task 19's `air-compare: equal` (Mac
   lanes, native and i686) and the version rows.
5. **Alt-tab, minimise, fullscreen, resize.** Expected: `DXGI_STATUS_OCCLUDED` when minimised or zero-sized (bitblt
   models only), `DXGI_PRESENT_TEST` before any work, `SyncInterval > 4` invalid, `dxgi.handleAltTab` leaving fullscreen
   in the background, the gamma ramp read through `IMTLDXGIOutput`. Tests: Task 14
   `swapchain::tests::{occluded_when_minimised_or_zero_size, test_flag_returns_before_present,
   sync_interval_over_4_is_invalid, alt_tab_leaves_fullscreen, frame_latency_fence_signals_at_retire}` (the
   `d11sc` HRESULT lines of Task 2's golden); the probe's `d11sc` runtime cases on an iconic window (Task 1: a DISCARD
   chain's `Present(0,0)` and `Present(0,TEST)` with a `fast` field, a FLIP_DISCARD chain, `Present(5,0)`, a 0x0
   `ResizeBuffers` on a bitblt chain; their answers recorded from the C++); Task 4 `wsi::tests::*`; Task 19's step-3
   fullscreen gamma row, `presenter/check.sh`'s resize rows, d3d-smoke's `wmsize`, present-gate. (check.sh's
   "minimised presents" row is D3D12's: `d3d12_fullscreen.exe d3d11` returns before it, `:157`.)

---

### Task 0: base, kit, audit (owner)

**Files:** scratch `$S/{lane-build.sh,ident*.sh,ident*.py,hist.sh,hist-compare.sh,stats-*.sh}` copied from `$S5a`,
`$S/wmtcmd-compare.sh` (new: two dump folders per program, the reference's mask applied → `wmtcmd-compare: equal`;
mutation RED: one edited field fails), `$S/audit.md`, the ledger.

- [ ] **Step 1: Base.** Ledger: 5a's pushed `main` sha and `step5w`'s head (every W lane and 5c's Tasks 1-4 merged,
  P23), step R's and step 3's commits on `main`, Z's state (`step4a-translator` head, or the pinned sha), 4a lane P1's
  and Z2's commits, what `step5c-mn` has landed on `dxmtrs` (P24). 5c's ledger: Tasks 1-4's commits and the names they
  landed (`sm50`'s items and mirrors, `--com`'s options, `ComObject`, `ext`, `private_data`, `version`'s signature,
  `wsi`'s functions); the gaps P23 lists for 5b (the chain structs `sm50` lacks, the WSI calls 5c's `wsi` lacks) →
  ledger, and the items 5b keeps → 5c's controller. Every path and line this plan cites passes `test -e`/re-read in a
  worktree of `step5w` and MacNeutron `dxmtrs`; moved lines ledgered (`eac-vm/dxmt/gate.sh`'s program count: 45 after
  step 3, 47 after Task 1; the 12 D3D11/D3D10 lines of `eac-vm/dxmt/invocations`; the native and loopback `ROWS`, 31
  each, 33 after Task 1; whether `eac-vm/dxmt-native/build-tests.sh` runs each listed program or only builds it, and
  where the native gate leaves its runs' shader caches). Key → ledger. Name one `debug`-level log literal of D3D11's
  C++ for Task 19's p-build row. The controller asks the maintainer 5-D12's title (P21) and ledgers the answer.
- [ ] **Step 2: Branches and kit.** `git -C $D branch step5b step5w`; a read-only worktree `$S/wt-step5b` of `step5b`
  (moved to each merge's head by the controller; Task 16's `make -C eac-vm/d3d` reads it); MacNeutron `git worktree add
  $S/wt-mn -b step5b-mn dxmtrs`. `ident-diff.py` gains ED11 (P16) with a mutation RED (a copy of an LB `d3d11.dll`
  table with ordinal `@22` removed fails).
- [ ] **Step 3: The audit** (`$S/audit.md`, no code). From `src/d3d11`, `src/d3d10` against `step5w`'s Rust:
  (a) the 65 `enc.`/`ctx.` names D3D11 calls (`grep -ohE '\b(enc|ctx)\.[a-zA-Z_]+'`: `access` 47, `encodeRenderCommand`
  22, `encodeBlitCommand` 20, … `bindConstantBufferOffset` 1) → the `dxmt_core::encoder` method, or **gap**; (b) the 25
  `CommandQueue` producer calls (M1 §3.5.1) → `Queue`/`Chunk`; (c) every `Buffer`, `Texture`, allocation, `Staging`,
  `DynamicBuffer`, `DynamicLinearTexture`, `ResourceInitializer`, `CounterPool`, query and `EmulatedCommandContext`
  member D3D11 calls → lane R/E's; (d) every core template D3D11 instantiates (`ClearRTV`, `ClearUAV`,
  `DepthStencilBlitContext`, `MTLFXMVScaleContext`, `TileBarrierContext`); (e) every `UNREACHABLE`/`DXMT_UNREACHABLE`
  site of `src/d3d11` with its defined result (P10). Each gap names its Task 6 file and Rust signature (Task 3 stubs
  them in `encoder/gaps.rs`). Expected: the list is complete (every grep hit has a row); gaps ledgered.

### Task 1: the D3D11 programs, the probe and the A/B harness (lane `mn`, MacNeutron `step5b-mn`)

**Files:** create `dxmt/tests/d3d11_probe.cpp`, `dxmt/tests/d3d11_draw.cpp` and `dxmt/tests/d3d11_hazards.cpp` (picked
up by `Makefile:60, 79`'s `d3d1[012]_*` wildcards and the i386 rule `:72`), `eac-vm/tests/d3d11-guest.sh` (a copy of
step 3's `dxgi-guest.sh` running `d3d11_probe`, plus a `programs` mode); modify `dxmt/tests/dxbc.cpp` (the new
programs' HLSL: a pass-through geometry shader, the hazard passes; their `.dxbc` are Task 2 Step 4's),
`dxmt/tests/d3d11_deferred.cpp` and `dxmt/tests/d3d11_transfer.cpp` (cases below), `dxmt/check.sh` (rows below),
`eac-vm/dxmt/ref.sh` (`MN_REF_TESTS`: the folder `$exe` is taken from, default `$I`, so the ARM64EC programs run),
`eac-vm/dxmt/invocations` (`d3d11_draw|d3d11_draw.exe|@S/d3d11|draw` and
`d3d11_hazards|d3d11_hazards.exe|@S/d3d11|hazard11`), the `ROWS` of `eac-vm/dxmt-native/gate.sh` and
`eac-vm/d3d/loopback-gate.sh` (both programs), `eac-vm/dxmt-native/build-tests.sh` (the probe in its program list, not
in any gate's `ROWS`). Counts after this task: the VM gate 47 programs, native and loopback 33 each, the Mac A/B 17.
Scratch: `$S/{d3d11.invocations,mac-ab.sh,air-dump.sh,air-compare.sh}`.

**Interfaces:**
- Produces: `d3d11_draw.exe <shaders> [perf]`, printing `draw <case> <texels or sums>` from texel readbacks: R16 and
  R32 `DrawIndexed` at a non-zero index-buffer offset, with `StartIndexLocation` and a negative `BaseVertexLocation`;
  `DrawIndexedInstanced` with `StartInstanceLocation`; strip cut indices; `DrawInstancedIndirect`,
  `DrawIndexedInstancedIndirect` and `DispatchIndirect` at a non-zero args offset; `SetPredication` both ways (an
  occlusion predicate that passes and one that fails); a `SwapDeviceContextState` round trip (state set, swapped out,
  swapped back, drawn); `SetResourceMinLOD` on a sampled mipped texture; a pass-through geometry shader if the C++
  accepts it (else its refusal's HRESULT is the golden). `perf` adds P19's QPC-timed sections as `perfinfo <row> <ns
  per call>` lines (a prefix no gate compares). Tessellation ships on lane `cd`'s reading (ledgered; `d3d11_pipeline`
  shows the translator refusing Wine's hull shader).
- Produces: `d3d11_hazards.exe <shaders> [mode…]`, modelled on `d3d12_hazards`, printing `hazard11 <mode> <values>`
  that a correct ordering makes fixed: a long compute pass writing a 1024² UAV, then a draw sampling it, a blit copying
  it and a compute pass reading it; a render target read as an SRV after a long draw; two ordered UAV dispatches.
- Produces (`d3d11_deferred` cases, each `deferred <case> <texels wrong>`): `twice` (one list executed twice into two
  texels), `restore` (`FinishCommandList(TRUE)`), `query` (an occlusion query begun and ended in a list, one left open
  at Finish; its `GetData` after Execute), `staging` (a `CopyResource` to staging in a list, then `Map(READ)` after
  Execute), `update` (`UpdateSubresource` in a list), `nooverwrite` (a WRITE_DISCARD then NO_OVERWRITE map in a list).
- Produces (`d3d11_transfer` cases): on a DYNAMIC constant buffer, a whole `UpdateSubresource`, a partial box without
  flags (the bytes outside the box kept), `UpdateSubresource1` with `D3D11_COPY_NO_OVERWRITE` after a draw and with
  `D3D11_COPY_DISCARD` (`d3d11_context_impl.cpp:1228-1265`); a 3D `UpdateSubresource` with packed rows and a padded
  depth pitch (`UpdateTexture`'s contiguous branch, `:4224-4229`).
- Produces: `d3d11-guest.sh <i386|x86_64> programs <out>`: the 14 D3D11/D3D10 programs of `$S/d3d11.invocations` (the
  two new ones included; not `d3d11_dlss`, `d3d11_nvapi` or the probe: this run is for `d3d11.dll`'s own branches,
  and the probe has its own mode) built for the arch as the probe is, run in guest mode with
  `DXMT_SHADER_CACHE_PATH` on the share, each `<out>/<name>.txt` and its cache folder; the caller compares (compare.py
  against the Mac reference, `air-dump.sh` and `air-compare.sh` against another run).
- Produces: `d3d11_probe.exe <shaders> <fixed|full> [<family>…]`, families `d11dev`, `d11res`, `d11view`, `d11state`,
  `d11shader`, `d11ctx`, `d11defer`, `d11query`, `d11ext`, `d11sc`, `d11share`, `d10`, and natively `notimpl`; lines
  `<family> <case> <field>=<value>…`, machine-independent in `fixed` (relations where a lane differs, as step 3 D16),
  every raw value in `full` plus a per-family call-rate line. Cases: per object kind the QI matrix over every IID it or
  a companion answers (`hr=`, `same=0|1` against the object and each companion), AddRef/Release return values across
  companions (P6's schemes), private data (step 3 D6's cases), `GetDevice`; each `Create*` over a descriptor table of
  valid and invalid descs (`hr=`, every `GetDesc` field); equal descs give the same state object (`same=`), a
  released-to-zero state object comes back; `CheckFeatureSupport` for every `D3D11_FEATURE` value 0..=N+1,
  `CheckFormatSupport`/`FORMAT_SUPPORT2` for every `DXGI_FORMAT` 0..=191, `CheckMultisampleQualityLevels` ×{1,2,4,8},
  `CheckCounterInfo`; feature levels per `d3d11.maxFeatureLevel` in child processes (step 3's child form); every
  context getter after its setter, `ClearState`'s defaults, `GetType`, `GetContextFlags`; `Map` flags × usages
  (`hr=`, `DO_NOT_WAIT` on a busy buffer); deferred: `FinishCommandList` twice, `ExecuteCommandList` with
  `RestoreContextState` 0/1; queries' `GetData` sizes and flags; `IMTLD3D11ContextExt`/`Ext1`/`Ext2`/`DeviceExt` QIs
  (`Ext2` natively only), `Ext1::CheckFeatureSupport`, `GetPieces` on a deferred context; the swap chain from
  `CreateSwapChainForHwnd` on a hidden window (QI matrix, `GetDesc1`, `GetBuffer` identity, `ResizeBuffers`, the
  frame-latency waitable object, `Present` with `DXGI_PRESENT_TEST`; and on an iconic window (`ShowWindow(SW_MINIMIZE)`,
  `IsIconic` printed): a DISCARD chain's `Present(0,0)` and `Present(0,DXGI_PRESENT_TEST)` with a `fast` field (under
  100 ms), a FLIP_DISCARD chain's `Present(0,0)`, `Present(5,0)`, and a 0x0 `ResizeBuffers` then `Present` on a bitblt
  chain: their HRESULTs recorded from the C++, `d3d11_swapchain.cpp:765-791`); `d11share` (both Mac lanes, step 3's
  child form): the parent creates `MISC_SHARED` and `MISC_SHARED_KEYEDMUTEX` textures, a child opens each
  (`OpenSharedResource`), calls `AcquireSync(0)`, writes and calls `ReleaseSync(1)`, the parent calls `AcquireSync(1)`
  and reads back (each `hr=` and the bytes); in the VM's guest mode also an i686 child of an x86_64 parent; D3D10
  through `D3D10CoreCreateDevice`: QI matrix, each D3D10 `Create*` and its D3D11 counterpart's desc, getters returning
  the wrapper pointers; `notimpl` (native, `NOT_ON_C`): every reachable `UNIMPLEMENTED` site (P10's 19): the six tiled
  calls (`UpdateTileMappings`, `CopyTileMappings`, `CopyTiles`, `UpdateTiles`, `ResizeTilePool`, `TiledResourceBarrier`),
  `GetResourceTiling`, `WriteToSubresource`, `ReadFromSubresource`, `CreateSurface`, a default-buffer `Map` on the
  immediate and on a deferred context, a deferred `ExecuteCommandList`, `UpdateSubresource` on a staging buffer and on
  a staging texture, and the four compressed/staging copies (`d3d11_context_impl.cpp:4109, 4113, 4160, 4164`), each
  printing `hr=` (or `void`) and `flag=` `MTLD3DNotImplemented()`'s two reads (`1,0`); the `DXMT_UNREACHABLE` cases
  P10 lists (`d11res`'s `CreateBuffer` with an invalid `Usage`), `NOT_ON_C`. The probe never calls an `UNIMPLEMENTED`
  method on Windows (P10's list in its header comment).
- Produces: `MN_REF_INVOCATIONS=$S/d3d11.invocations` (the 14 D3D11/D3D10 lines of `invocations`, plus
  `d3d11_dlss`, `d3d11_nvapi`, and `d3d11_probe|d3d11_probe.exe|@S/d3d11 fixed|d11 d10`: 17); `mac-ab.sh <ref> <run>`:
  `compare.py <name> <ref>/<name>.1.txt <ref>/<name>.2.txt <run>/<name>.1.txt <prefixes>` per line →
  `mac-ab: equal (<n> programs)`; `air-dump.sh <cache-root> <out>`: per `<name>.<i>` folder, the one `cache_*` table's
  name and `sqlite3 -readonly <db> "select hex(key)||' '||hex(value) from <table> order by 1"` → `<out>/<name>.<i>.rows`;
  `air-compare.sh <a> <b>` → per program, rows equal (table names reported, not compared) → `air-compare: equal`.
- Produces (check.sh): `run ours d11probe dxmt "$TESTS/d3d11_probe.exe" "Z:$S/d3d11" fixed` with `expect "D3D11's COM
  answers are the C++'s"` against `dxmt/tests/d3d11_probe.fixed` (d3d11_nvapi's `firstdiff` form, `check.sh:1276`);
  `expect "a D3D11 and a D3D12 program open one cache table"` (one cache folder, `present_loop` then `d3d12_clear`, one
  `cache_*` table in `shaders_*.db`).

- [ ] **Step 1: RED.** Built for x64 (llvm-mingw directly into `$S/probe/`, host only; the probe, `d3d11_draw`,
  `d3d11_hazards` and the two extended programs, and the probe for i686); `compare.py self-test` still passes;
  `mac-ab.sh` and `air-compare.sh` on two hand-made folders differing by one line → each FAILs; `sh -n` on
  `d3d11-guest.sh`; `MN_DXMT_ONLY` unset, the two `ROWS` list 33 names each (`wc -w`).
- [ ] **Step 2: GREEN** (the programs, the probe, the rows, the scripts). Goldens and references come from Tasks 2
  and 17; until then the check.sh row is added with the golden file absent and fails "no golden".
- [ ] **Step 3: Commit** (`step5b-mn`), one per program and one for the scripts: `dxmt tests: d3d11_probe, the D3D11
  COM answers (step 5b)`, `dxmt tests: d3d11_draw, indexed, indirect and predicated draws`, `dxmt tests:
  d3d11_hazards`, `dxmt tests: deferred lists with queries, staging and updates; dynamic updates`, `eac-vm: the D3D11
  programs in the gates and in i686 guest runs`.

### Task 2: the early references (owner)

**Files:** MacNeutron `dxmt/tests/d3d11_probe.{fixed,native,guest64,i386}` and the new programs' `.dxbc` in
`dxmt/tests/shaders/d3d11/` (committed on `step5b-mn`); DXMTRS golden data, committed by the lane that reads it:
`src/d3d11/src/shader/golden/{digests,chains}.txt` and `src/d3d11/src/state/golden/metal.txt` (lane `st`),
`src/d3d11/src/resource/golden/descs.txt` (lane `rs`), `src/d3d11/src/inspection/golden/d11dev.txt` (lane `dv`: the
`d11dev` lines of `.fixed`), `src/d3d11/src/context/state/golden/d11ctx.txt` (lane `cs`: `d11ctx`'s defaults and
getter lines), `src/d3d11/src/swapchain/golden/d11sc.txt` (lane `sw`: `d11sc`'s HRESULT lines),
`src/d3d11/src/d3d10/golden/d10.txt` (lane `d10`: the desc lines), `src/d3d11/src/context/golden/wmtcmd/*.txt` (lanes
`cd`, `ct`, `cx`: slices of Step 4's dumps); scratch `$S/golden/*.cpp`, `$S/ref-lb/<kind>`, `$S/ref-log/`,
`$S/ref-dump-early/`, `$S/include-native/`. Lane `x` takes no golden here (its cache version is 5c's, P12).

- [ ] **Step 1: Host goldens** (5a Task 0's harness form; host clang++ against `include/native`, the named C++ and
  `src/util/sha1`): `digests.cpp` prints, over a variant table (each `ShaderVariant*` struct, edge values, input
  layouts of 0, 1 and 32 elements with both step functions), the hashed byte stream and the digest, and each
  function name (`d3d11_shader.cpp:120-444`), with each `size_t` field of the stream marked, so the width-4 stream (the
  i686 one: each marked field cut to 4 bytes) is derived from it and its digest printed by the same harness's SHA-1;
  the i686 cache rows (Task 19) pin it at runtime; `chains.cpp` prints each variant's argument chain field by field (the
  `proc` lambdas' structs); `metal.txt` from `d3d11_state_object.cpp`'s converters (blend, rasterizer, depth-stencil,
  sampler descs → their winemetal infos and records, over a desc table with the C++'s defaults); `descs.txt` from
  `d3d11_resource_helper.cpp`/`d3d11_resource_view_helper.cpp`'s validation and default-view functions over desc
  tables. Expected: two runs byte-equal.
- [ ] **Step 2: LB references.** `LB(mac)`, `LB(native)`, `LB(guest64)`, `LB(guest32)` on a worktree of `step5w`, copied
  to `$S/ref-lb/<kind>`, tabulated (`ident.sh --tabulate`): early C++ `d3d11.dll` import tables for the lanes; Task 17
  Step 1 replaces them with the switch's base's.
- [ ] **Step 3: Probe goldens** (one lock job, `<M>` at `step5b-mn`): build `step5w`'s head (`make wine-arm64
  dxmt-tests dxmt-tests-arm64ec`), run the probe `fixed` twice on each Mac lane → `.fixed` (the lanes' outputs equal,
  else the differing case becomes a relation line in the probe and this step re-runs), `full` once each into
  `$S/ref-log/` with `DXMT_LOG_LEVEL=debug` (the log file kept); natively (`sh eac-vm/dxmt-native/build.sh`,
  `build-tests.sh`, the probe from `build/dxmt-native/tests` with `DXMT_LOG_PATH=none`) → `.native`, and
  `build/dxmt-native/install/include-native` copied to `$S/include-native` (Task 16's private copy); in the VM (guest
  stages as 5a Task 20) `d3d11-guest.sh x86_64` and `i386` in its record mode (as step 3 recorded `.i386`) →
  `.guest64`, `.i386`. Expected: each
  recorded twice equal; the check.sh row passes on this build; mutation RED: one edited `.fixed` line fails the row.
- [ ] **Step 4: Shaders and early dumps** (one lock job, after Task 5 merges; `$D` at `step5b`'s head, `front-end`
  off, so the DLLs are the C++'s): `dxbc.exe` through `eac-vm/dxmt/ref.sh`'s runtime writes the new programs' `.dxbc`
  (committed with the goldens); `DXMT_WMTCMD_DUMP` (P26) over `present_loop` and the programs of
  `$S/d3d11.invocations`, twice → `$S/ref-dump-early`, differing positions masked; the lanes' slices cut from it.
  Expected: two runs equal after the mask; the masked set ledgered.
- [ ] **Step 5: Commit** the goldens (`step5b-mn`: `dxmt tests: d3d11_probe's goldens and the new programs' shaders
  from the C++`).

### Task 3: the skeleton and the vtable generator (owner)

**Files (DXMTRS `step5b`):** modify `tools/winemetal-layouts.sh` (`--com`'s records list, P7; 5c's mode otherwise
unchanged); generated `src/d3d11/src/abi/{vtbl,records}.rs`; `src/d3d11/src/abi/ext.rs` (P20's companion structs,
layout-asserted against `dxmt_util::ext`); `src/d3d11/src/abi.rs` (+ `abi/tests.rs`), `com.rs`
(+ `com/tests.rs`), `src/d3d11/build.rs` (P12, 5c P8's form), and one stub file per module of the lanes' **Owns**
column (each a `//!` line and the Interfaces below with stub bodies returning `E_NOTIMPL`/default, never `todo!`);
the context's vtable statics `src/d3d11/src/context/vtbl.rs` (every `ID3D11DeviceContext4` slot forwarding to a
`Context<M>` method stubbed in the owning lane's file); `src/dxmt/src/{device,frame_stats}.rs` stubs and their `pub
mod` lines; `src/dxmt/src/encoder/gaps.rs` (one `impl Encoder` stub per gap of Task 0's audit, with its signature)
and `mod gaps;` in lane E's `encoder.rs`; modify `src/d3d11/Cargo.toml` (final dependencies but `airconv-ir`, which
Task 8's input-layout commit adds; features `front-end`, `perf-build`, `guest-vm`), `src/d3d11/src/lib.rs`,
`src/d3d11/meson.build` (`d3d11_front_end = false`; the cargo features from it; `DXMT_TRANSLATOR_KEY_H` and
`DXMT_VERSION_H` to the cargo target, which `depends:` on the `vcs_tag` target), root `Cargo.toml`/`Cargo.lock` (no
version moves), `rustfmt.toml` (P7's ignores), `tools/rust-check.sh` (`d3d11` clippy and UT with `--features
front-end`; `--com`'s regeneration check for `d3d11`).

**Interfaces** (what crosses lanes; each item lives in its owner's file, stubbed here):
- `com` (Task 3, P23): `Managed::{add_ref, release, add_ref_private, release_private}` (`ManagedDeviceChild`'s
  transitions, P6) beside 5c's `dxmt_util::com::ComObject` for the `ComObject` classes, both returning the C++'s
  values; `TearOff<T>` (forwards QI/AddRef/Release to its container); `SendPtr<T>` (P8); `container_of!`;
  `iface_of(&field) -> *mut c_void`.
- `context` (Task 3): `pub struct Context<M: Mode>` with its fields (state, `CommandBufferState`, dirty flags, defaults,
  device and queue handles), `trait Mode { fn emit_st(&mut Context<Self>, f: impl Fn(&mut Encoder) + Send + 'static);
  fn emit_op(…); fn retain<T: Send + 'static>(&mut Context<Self>, t: T); fn alloc(&mut Context<Self>, size: usize,
  align: usize) -> (*mut u8, u64); fn staging(…); fn dynamic_name(…) }` (captures and `retain`ed values drop at the
  chunk's reset, P8), `Immediate`, `Deferred`.
- `resource` (lane `rs`): `Common` (`buffer: Option<Arc<Buffer>>`, `texture: Option<Arc<Texture>>`, `bind_flags`,
  `usage`, `misc_flags`, `keyed_mutex: Option<KeyedMutexRef>`, `min_lod`), `common(p: *mut c_void) -> Option<&Common>`
  (the C++'s trusting `static_cast`, `d3d11_resource.hpp:164-188`), `dynamic_buffer`, `dynamic_texture`, `staging`;
  factories `create_buffer`, `create_texture1d/2d/3d` (`(device, desc, init, out) -> HRESULT`), `open_shared(…)`.
- `view` (lane `rs`): `Srv`, `Rtv`, `Dsv`, `Uav` with `from_iface` and their pub fields (resource, view key, subset,
  format, planar flags, array length, UAV counter); factories `create_{srv,rtv,dsv,uav}`.
- `state` (lane `st`): `BlendState::info()`, `RasterizerState::setup(&mut WmtcmdRenderSetrasterizerstate)`,
  `DepthStencilState::state(planar: u32) -> u64`, `SamplerState::{handle, argument, lod_bias}`, `Defaults::new(device)`;
  factories `create_{blend,rasterizer,depth_stencil,sampler}_state`.
- `shader`, `input_layout`, `pipeline` (lane `st`): `Shader::from_iface(p)`, `ManagedShader { sha1, reflection, handle:
  u64 }`, `InputLayout::{from_iface, elements, slot_mask, sha1}`, `StreamOutputLayout`, `PipelineCache::{graphics(&
  GraphicsDesc) -> Arc<CompiledGraphics>, compute(&ManagedShader) -> Arc<CompiledCompute>}`, `Compiled*::ready() ->
  Option<…>`; factories `create_*_shader`, `create_input_layout`, `create_gs_with_so`.
- `device` (lane `dv`): `Device { pub mutex: DeviceMutex, … }` with `impl Drop` (P8: commit and wait before any field
  drops), `Device::{from_iface, core, queue, immediate, pipeline_cache, caches, direct_x_version, d3d10}`,
  `DeviceMutex::{lock, unlock, set_protected, get_protected}`.
- `context::immediate` (lane `cx`): `wait_until_gpu_idle`, `prepare_flush`, `commit`, `so_get_targets_with_offsets`,
  `acquire_sync`, `release_sync` (`IMTLD3D11DeviceContext`, `d3d11_device.hpp:14-23`); factories `create_deferred`,
  command-list pool.
- `swapchain`, `query`, `fence` (lane `sw`): `create_swap_chain(device, factory, hwnd, desc, fs_desc, out) -> HRESULT`
  (`IMTLSwapChainFactory` slot 3), `Query::{from_iface, begin, end, data}`, `Fence::{from_iface, event}`; factories.
- `d3d10` (lane `d10`): `#[repr(C)] Wrapper<V> { vtbl: &'static V, d3d11: *mut c_void, context: *mut c_void }` (the
  C++'s fixed layout, `d3d10_device.cpp:13-17`), the 16 vtable statics the D3D11 lanes embed, `Device::new(d3d11,
  context) -> Box<Device>`.
- `dxmt_core::{device::Device, frame_stats::{FrameStatistics, Container}}` (lane `x`).

- [ ] **Step 1: RED.** `com::tests::{com_object_deletes_at_private_zero, managed_zero_is_valid_and_recycles,
  first_public_ref_adds_device_ref, tear_off_forwards}` (counts against a fake device; the first over 5c's
  `ComObject`, the next two over `Managed`); the `--com … --records …` run over a scratch header with one record field
  widened → its i686 offset assert fails. Expected: FAIL.
- [ ] **Step 2: GREEN.** The records list, then `--com` over the interfaces (every `ID3D11*` of `d3d11.h`..`d3d11_4.h`
  the C++ implements, `ID3D10*` of `d3d10.h`/`d3d10_1.h` the layer implements, `IDXGIDevice3`, `IDXGIResource1`,
  `IDXGISurface2`, `IDXGIKeyedMutex`, `IDXGISwapChain4`, `ID3D11Multithread`, `ID3DUserDefinedAnnotation`) and the
  records they name; regenerate twice → equal; its asserts compile on the five targets; mutation RED: one slot swapped
  in a copy of `vtbl.rs` fails the i686 or x86_64 assert; 5c's own `--com` outputs regenerate unchanged. `com`,
  `build.rs`, the stubs, the context vtable, `gaps.rs`. CK(d3d11 dxmt-core); `LB(mac)`, `LB(native)`, `LB(guest32)` ok
  (feature off: the DLL is still the C++'s); RL → `rust-check: ok`.
- [ ] **Step 3: Commit** `d3d11: the Rust front end's skeleton and its generated vtables (not linked)` (the
  `--records` addition its own commit first: `tools: winemetal-layouts --com prints the records its slots name`).

### Task 4: `dxmt-util`: SHA-1 and the window calls 5c's WSI lacks (lane `util`)

COM helpers, private data, the QI-error latch and the WSI module itself are 5c's Tasks 2 and 4 (P23); this task adds
only what 5b keeps.

**Files:** `src/util/src/{sha1.rs,sha1/tests.rs}` and its `pub mod` line; additions to `src/util/src/wsi{.rs,/…}`
(the calls Task 0 Step 1 ledgered as missing from 5c's module), with their tests.

**Interfaces:**
- Produces: `dxmt_util::sha1::{Sha1, Sha1Digest, Sha1Digest::hex()}` (`sha1.c`; `Sha1Digest::string()`'s lowercase);
  `dxmt_util::wsi::{is_foreground, WsiMode, …}` (what D3D11's swap chain calls and 5c's `wsi` lacks; PE from
  `wsi_window_win32.cpp`/`wsi_monitor_win32.cpp`, natively from `wsi_window_headless.cpp`/`wsi_monitor_headless.cpp`
  over `WMTGetWsiHooks`, B5).
- [ ] **Step 1: RED.** `sha1::tests::{fips_vectors, million_a, streams_like_the_c}`; `wsi::tests::{foreground_from_the_hook,
  no_hooks_is_headless_default_foreground}` and one per further addition (fake hooks, host). Expected: FAIL.
- [ ] **Step 2: GREEN**; CK(dxmt-util).
- [ ] **Step 3: Commits** (one per module) `dxmt-util: SHA-1, for every front end` and `dxmt-util: the window calls
  D3D11's swap chain adds`.

### Task 5: the chain mirrors `sm50` lacks; the record dump knob (lane `sys`)

The `SM50*` declarations and the native not-implemented flag are 5c's Task 1 (P23). The flag's test is 5c's C test
(`win32-test.cpp`); nothing here builds C through cargo.

**Files:** additions to `src/winemetal/sys/sm50-abi.h` and the regenerated `src/winemetal/sys/src/sm50/abi.rs`
(`--seam-abi`, 5c Task 1's pair); `src/winemetal/unix/src/histogram.rs` and `histogram/tests.rs` (P26).

**Interfaces:**
- Produces: in `winemetal_sys::sm50`, whatever of `Sm50ShaderCommonData`, `Sm50ShaderIaInputLayoutData`,
  `Sm50IaInputElement`, `Sm50ShaderGsPassThroughData`, `Sm50ShaderPsoPixelShaderData`,
  `Sm50ShaderPsoGeometryShaderData`, `Sm50ShaderPsoTessellatorData`, `Sm50ShaderEmulateVertexStreamOutputData`,
  `Sm50CompiledBitcode`, … 5c's module lacks (names by its casing; the nine chain types Task 0 Step 1 lists from
  `airconv_public.h:205-320`, the gap ledgered there); `DXMT_WMTCMD_DUMP=<file>` (P26).
- [ ] **Step 1: RED.** Regenerate → differs (the missing mirrors); a scratch drifted field in `sm50-abi.h` → the
  clang check fails; `histogram::tests::{dump_lists_every_field, handles_renumber_by_first_use,
  gpu_addresses_as_buffer_and_offset}`. Expected: FAIL.
- [ ] **Step 2: GREEN**; regenerate twice equal; 5c's existing `sm50` items regenerate unchanged but for the
  additions; mutation RED: one field left out of a dump line → `dump_lists_every_field` fails; `LB(native)` → `nm -gU`
  of `winemetal.dylib` still lists 5c's `_MTLD3DNotImplemented` and `_wmt_native_not_implemented`;
  CK(winemetal-sys winemetal).
- [ ] **Step 3: Commits** `winemetal: D3D11's argument structs for Rust callers` and `winemetal: DXMT_WMTCMD_DUMP
  lists encoded records`.

### Task 6: the core's additions (lane `x`)

**Files:** `src/dxmt/src/{device.rs,device/tests.rs,frame_stats.rs,frame_stats/tests.rs,encoder/gaps/tests.rs}` and
the bodies of Task 3's `encoder/gaps.rs` stubs; modify lane E's files, merged before 5b starts: `src/dxmt/src/arena.rs`
(+ tests: P8's drop timing), `src/dxmt/src/queue.rs` (the statistics container and the frame counters; P13's guard on
the encode, finish and event-listener thread bodies) and `encoder.rs` (a field a gap needs). The cache version is 5c's
(P12): no `version.rs`, no `build.rs` here.

**Interfaces:**
- Produces: `device::Device::new(mtl: Obj, metal_version_option: i32) -> Device` (`dxmt_device.cpp:27-56`: OS,
  `dxmt.shaderMetalVersion`, GPU family), `metal_version() -> u32`, `max_object_threadgroups() -> u32`, `queue() ->
  &Queue` (lane E's, 32 chunks); `frame_stats::{FrameStatistics, Container}` (`dxmt_statistics.hpp:38-167` without
  `min()`), held by `Queue` (`queue.rs`: `current_frame_statistics`, `statistics()`, `current_frame_seq`,
  `current_frame_id`, written at `commit` and `present_boundary` as `.hpp:262-278`, `.cpp:101-111`); each audit gap as
  an `impl Encoder` method in `encoder/gaps.rs` with the audit's signature; `Chunk::emit<F: Fn(&mut Encoder) + Send +
  'static>` and `Chunk::retain<T: Send + 'static>(T)`, both dropped in the chunk's `reset` on the finish thread, after
  the wait and before `coherent` is signalled (P8).
- [ ] **Step 1: RED.** `device::tests::metal_version_as_cpp` (OS × option × family table read from the C++);
  `frame_stats::tests::{average_over_the_window, scaler_info_kept}`; `arena::tests::arena_drops_at_reset_not_at_run`
  (a capture whose drop records the order: run, then reset, then drop; and a `retain`ed value likewise);
  `queue::tests::encode_thread_panic_aborts` (a child process, natively; mutation: the guard dropped → FAIL); one test
  per gap from the C++'s body. Expected: FAIL. 5a E1's `arena_drops_each_command_once` keeps passing.
- [ ] **Step 2: GREEN**; CK(dxmt-core); msvc builds on the four targets.
- [ ] **Step 3: Commits** (one per module) `dxmt: the device and frame statistics (written ahead)`, `dxmt: a recorded
  command's captures live until its chunk retires`, `dxmt: the queue's threads abort on a panic`, `dxmt: D3D11's encoder
  needs (written ahead)`.

### Task 7: resources, views, surfaces, keyed mutex, shared resources (lane `rs`)

**Files:** `src/d3d11/src/{resource.rs,resource/{buffer,texture,dynamic,linear,staging,helper,tests}.rs,view.rs,view/
{helper,tests}.rs,surface.rs,keyed_mutex.rs,kmt.rs,kmt/tests.rs,subresource.rs,subresource/tests.rs}` from
`d3d11_buffer.cpp`, `d3d11_texture*.cpp`, `d3d11_resource_staging.cpp`, `d3d11_resource_helper.cpp`,
`d3d11_resource_view_helper.cpp`, `d3d11_resource.hpp`, `d3d11_view.hpp`, `d3d11_surface.*`, `dxgi_resource.hpp`
(`MTLDXGIResource`, `MTLDXGIKeyedMutex`), `dxmt_keyed_mutex.*`, `util_d3dkmt.h`'s calls, `dxmt_subresource.*`.

**Interfaces:** Consumes lane R's `Buffer`, `Texture`, `Staging`, `DynamicBuffer`, `DynamicLinearTexture`;
`dxmt_util::private_data`; `d3d10::Wrapper` (embedded); `dxmt_core::format`. Produces Task 3's `resource`/`view` items.

- [ ] **Step 1: RED.** `resource::tests::{descs_validate_as_the_golden, default_views_as_the_golden,
  cpu_placement_per_target (the C++'s `__i386__` choice as a function of the target, both values on the host),
  dynamic_buffer_renames_on_the_caller}`; `view::tests::{subset_of_every_view_kind,
  null_desc_takes_the_resource_format}`; `kmt`: `SharedResourceData`'s and the 54-byte mutex name's layouts as `const`
  asserts (size and offsets equal on i686 and x86_64, CK's clippy of both); `subresource::tests::{sets_as_cpp}`.
  Expected: FAIL (the asserts: a widened field fails the i686 clippy).
- [ ] **Step 2: GREEN**; CK(d3d11). Mutation RED: one default-view field changed → `default_views_as_the_golden` fails.
- [ ] **Step 3: Commits**, three: `d3d11: buffers, staging and dynamic resources in Rust (not linked)`
  (`resource/{buffer,dynamic,staging}`), `d3d11: textures, surfaces, shared resources and the keyed mutex in Rust (not
  linked)` (`resource/{texture,linear}`, `surface`, `kmt`, `keyed_mutex`), `d3d11: views and their helpers in Rust (not
  linked)` (`view`, `resource/helper`, `subresource`), each with its own tests.

### Task 8: state objects, shaders, input layouts, pipelines, the compile scheduler (lane `st`)

**Files:** `src/d3d11/src/{state.rs,state/{cache,tests}.rs,input_layout.rs,input_layout/tests.rs,shader.rs,
shader/{args.rs,args/tests.rs},pipeline.rs,pipeline/{graphics,compute,gs,ts,tests}.rs,scheduler.rs,scheduler/
tests.rs}` from `d3d11_state_object.*`, `d3d11_input_layout.*`, `d3d11_shader.*`, `d3d11_pipeline*.{cpp,hpp}`,
`d3d11_pipeline_cache.*`, `dxmt_tasks.*`.

**Interfaces:** Consumes 5c's `sm50` and Task 5's mirrors, 5c's `shader_cache::{version, instance}` (P12) and its
`Store` (writer then reader, its key `(shader sha1, variant digest)` as `d3d11_pipeline_cache.cpp:147-157`), Task 4's
`sha1`, `airconv-ir::{Container::parse, signature::input, signature::output_v4}` (4a P1's table, through P25's
scaffold). Produces Task 3's `state`, `shader`, `input_layout`, `pipeline` items and `scheduler::wait_idle(timeout_ms:
u32) -> bool`.

- [ ] **Step 1: RED.** `args::tests::{digests_and_names_are_the_cpps, chains_are_the_cpps,
  digest_hashes_size_t_at_both_widths (the digest's `size_t` width a parameter, 4 and 8 against Task 2's two streams),
  flags_from_config}` (Task 2's golden); `state::tests::{converters_are_the_golden, equal_descs_share_an_object,
  released_object_comes_back, nan_desc_never_equals}`; `input_layout::tests::{elements_from_vsia_signature,
  missing_semantic_fails_as_cpp, so_layout_from_output_v4}`; `scheduler::tests::{grows_to_twice_parallelism,
  continuation_runs_after, wait_idle_times_out, wait_idle_true_when_drained, worker_panic_aborts (a child process
  expecting an abort, P13)}`; `pipeline::tests::error_text_cut_at_256`. Expected: FAIL.
- [ ] **Step 2: GREEN**; CK(d3d11). Mutation REDs: hash the element count at width 8 when asked for 4 →
  `digest_hashes_size_t_at_both_widths` fails; the worker's guard dropped → `worker_panic_aborts` fails. The
  input-layout module, its `airconv-ir` dependency line and its tests are the lane's last commit, on P25's scaffold,
  rebased onto `step5b` only after Z; the lane's other commits rebase off the scaffold onto `step5b` and re-run CK
  there before they merge.
- [ ] **Step 3: Commits**, two plus the input-layout one: `d3d11: state objects and the compile scheduler in Rust (not
  linked)` (`state`, `scheduler`), `d3d11: shaders, their arguments and pipelines in Rust (not linked)` (`shader`,
  `shader/args`, `pipeline`), then `d3d11: input layouts in Rust, from airconv-ir's signatures (not linked)`.

### Task 9: the device and its exports (lane `dv`)

**Files:** `src/d3d11/src/{exports.rs,device.rs,device/{dxgi,impl_,tests}.rs,inspection.rs,inspection/tests.rs,
multithread.rs,multithread/tests.rs,annotation.rs,class_linkage.rs,names.rs}` from `d3d11.cpp`, `d3d11_device.*`,
`d3d11_inspection.*`, `d3d11_multithread.*`, `d3d11_annotation.hpp`, `d3d11_class_linkage.*`, `d3d11_enumerable.*`,
`dxmt_names.*`.

**Interfaces:** Consumes `IMTLDXGIAdapter` slot 19 (`-> u64`) and 20 (`-> u32`) (step 3 D4), `dxmt_util::ext`
(`IMTLD3D11DeviceExt`), Task 6's `Device`, every lane's factories. Produces: exports `D3D11CoreCreateDevice`,
`D3D11CreateDevice`, `D3D11CreateDeviceAndSwapChain`, `D3D11On12CreateDevice` (`E_NOTIMPL`, outputs nulled), natively
`MTLD3D11WaitIdle() -> i32`; `DllMain` (`DisableThreadLibraryCalls`); `Device`, `DeviceMutex` (Task 3's); the DXGI
device answering `IMTLDXGIDevice` (`6bfa1657-…`), `IMTLSwapChainFactory`, and the QI set of
`d3d11_device.cpp:1161-1215`; `GetDirectXVersion` from `Flags & 0x80000000`.

- [ ] **Step 1: RED.** `multithread::tests::{off_until_protected, recursive_for_the_owner, unprotect_resets_owner}`;
  `inspection::tests::{format_support_is_the_probe_golden, features_are_the_probe_golden}` (lines of
  `d3d11_probe.fixed`'s `d11dev` family, `include_str!` of the copy Task 2 names); `device::tests::{feature_level_from_config,
  apple7_allows_11_1, d3d10_flag_selects_d3d10, qi_set_as_cpp, drop_drains_before_caches (P8: a fake queue with a
  pending command that reads a cached state object; the device's `Drop` commits and waits for it, and the scheduler,
  before the caches drop, whatever the fields' declaration order: Rust drops them first to last, the C++ last to
  first), invalid_usage_is_invalid_arg (P10's `:97`)}`. Expected: FAIL. The drain is a deliberate change from the C++
  (ledgered).
- [ ] **Step 2: GREEN**; CK(d3d11).
- [ ] **Step 3: Commits**, one per module file with its tests (`device`, `exports`, `inspection`, `multithread`,
  `annotation`, `class_linkage`, `names`), each `d3d11: <module> in Rust (not linked)`.

### Task 10: the context's state (lane `cs`)

**Files:** `src/d3d11/src/context/{state.rs,setters.rs,getters.rs,binding_set.rs,context_state.rs,state/tests.rs}`
from `d3d11_context_state.hpp`, `dxmt_binding_set.hpp`, and `d3d11_context_impl.cpp`'s `*Set*`/`*Get*`,
`ClearState`, `SwapDeviceContextState`, `MTLD3D11DeviceContextState` parts.

**Interfaces:** Consumes Task 3's `Context<M>`, `view`/`state`/`shader` accessors. Produces the setter/getter
`Context<M>` methods of the context vtable, `BindingSet<T, N>` (raw-pointer redundancy check as `:23-27`), dirty flags.

- [ ] **Step 1: RED.** `state::tests::{clear_state_defaults_as_cpp, getters_after_setters_as_cpp (both
  `include_str!` of Task 2's `d11ctx.txt`, the C++'s defaults and getter answers: `SampleMask` 0xffffffff and the
  rest), redundant_bind_sets_no_dirty, getters_return_set_objects_addrefed, swap_state_round_trip,
  uav_slots_64_srv_128_cb_14}`. Expected: FAIL. Mutation RED: `ClearState` leaving `SampleMask` 0 →
  `clear_state_defaults_as_cpp` fails.
- [ ] **Step 2: GREEN**; CK(d3d11). **Step 3: Commits**, one per module file with its tests (`state`, `setters`,
  `getters`, `binding_set`, `context_state`), each `d3d11: the context's <module> in Rust (not linked)`.

### Task 11: the context's encoding (lane `cd`)

**Files:** `src/d3d11/src/context/{encode.rs,draw.rs,dispatch.rs,pipeline_select.rs,gs_ts.rs,encode/tests.rs}` from
`d3d11_context_impl.cpp`'s `CommandBufferState` (`:3381-3392`), `InvalidateCurrentPass` (`:4297-4350`),
`SwitchTo*Encoder` (`:4417, 4578, 4596`), `Finalize*Pipeline` (`:4690, 4771, 4818`), `PreDraw` (`:4865-4974`),
`PreDispatch` (`:5017`), `Draw*`, `Dispatch*`, and their closures.

**Interfaces:** Consumes lane E's `Encoder` (binding by stage, `access`/`track`, passes, `encode_*_command`,
argument buffers, GS/TS marshal), Task 6's gaps, `pipeline::PipelineCache`. Produces `Context<M>::{draw*,
dispatch*}` and `invalidate_current_pass(promote_flush: bool)`, `switch_to_{render,compute,blit}_encoder`.

- [ ] **Step 1: RED.** `encode::tests::{state_machine_transitions_as_cpp, draw_without_pipeline_skips,
  dirty_state_emits_once, promote_flush_commits, records_as_the_dump}`: the emitted closures run against lane E's
  `Encoder` over 5a's winemetal fakes, the records decoded and compared with Task 2's dump slices
  (`context/golden/wmtcmd/`: indexed draws at an index-buffer offset with R16 and R32, base vertex and base instance,
  indirect draws and dispatches at an args offset, GS `warp_count` with its `+ 1`, viewport, scissor, depth bias, blend
  constants, stencil ref). Expected: FAIL. Mutation RED: drop `+ index_sub_offset` → `records_as_the_dump` fails.
- [ ] **Step 2: GREEN**; CK(d3d11); every closure of the C++ parts listed has one Rust command (a count check: the
  ledger lists C++ lines with `ArgumentEncodingContext &` in this lane's range and their Rust sites).
  Tessellation ships on this lane's reading (ledgered: `d3d11_pipeline` shows the translator refusing Wine's hull
  shader, so no runtime row draws it).
- [ ] **Step 3: Commits**, one per module file with its tests (`encode`, `draw`, `dispatch`, `pipeline_select`,
  `gs_ts`), each `d3d11: the context's <module> in Rust (not linked)`.

### Task 12: the context's transfers (lane `ct`)

**Files:** `src/d3d11/src/context/{copy.rs,update.rs,clear.rs,resolve.rs,mips.rs,copy/tests.rs}` from
`d3d11_context_impl.cpp`'s `Copy*`, `UpdateSubresource*`, `Clear*`, `ResolveSubresource`, `GenerateMips` paths
(`:1250, 4107-4260` among them) and their closures.

**Interfaces:** Consumes lane R's `ClearRtv`/`ClearUav`, lane E's blit records and `EmulatedContext`, Task 6's gaps.
Produces the transfer methods of `Context<M>`.

- [ ] **Step 1: RED.** `copy::tests::{copy_region_boxes_as_cpp, bc_block_alignment, staging_update_is_not_implemented,
  compressed_to_staging_is_not_implemented}` (P10's inner paths, each setting the flag natively);
  `update::tests::{dynamic_update_paths_as_cpp (whole or COPY_DISCARD → a discard map, COPY_NO_OVERWRITE → a
  no-overwrite map, a partial box without flags → the staging blit), contiguous_3d_honours_depth_pitch}`; the blit
  records of each path against Task 2's dump slices, as Task 11. Expected: FAIL. Mutation RED: a partial dynamic update
  taking the discard path → `dynamic_update_paths_as_cpp` fails.
- [ ] **Step 2: GREEN**; CK(d3d11); closure count as Task 11. **Step 3: Commits**, one per module file with its tests
  (`copy`, `update`, `clear`, `resolve`, `mips`), each `d3d11: the context's <module> in Rust (not linked)`.

### Task 13: immediate and deferred contexts, command lists, maps, extensions (lane `cx`)

**Files:** `src/d3d11/src/context/{immediate.rs,deferred.rs,command_list.rs,ext.rs,map.rs,sync.rs}` and their
`tests.rs` from `d3d11_context_imm.cpp`, `d3d11_context_def.cpp`, `d3d11_context_impl.cpp`'s `MTLD3D11CommandList`
(`:5444-5578`), `MTLD3D11ContextExt` (`:5195-5429`), queries' begin/end/`GetData` dispatch, `Map`/`Unmap`, `Flush`,
`Flush1`, `Signal`/`Wait`, keyed-mutex sync.

**Interfaces:** Consumes lane E's `Queue`/`Chunk`/`CommandList` (P8's drop timing), lane R's staging/dynamic, 5a's
`TemporalScaler`, `dxmt_util::ext`, 5c's flag (P23). Produces `Immediate`/`Deferred` (`Mode`), Task 3's
`context::immediate` items, `IMTLD3D11ContextExt`/`Ext1`/`Ext2` (Ext2's QI answered natively only,
`d3d11_context_impl.cpp:653-658`).

- [ ] **Step 1: RED.** `command_list::tests::{runs_twice_with_the_same_records, dynamic_names_per_list,
  restore_state_vs_clear, executed_list_recycles_after_completion (the list returns to the pool and frees its staging
  blocks only at the chunk's reset, not at the run, P8), execute_marks_staging_and_reissues_queries (Execute's staging
  marks at the current sequence and its query re-issue, `d3d11_context_imm.cpp:611-644`), finish_ends_open_queries}`;
  `map::tests::{do_not_wait_on_busy_answers_still_drawing, ignore_no_wait_waits, discard_renames_on_the_caller,
  default_map_is_not_implemented}`; `ext::tests::{pieces_refused_on_deferred, upscale_desc_is_88}`;
  `immediate::tests::flush1_sets_event_after_commit`; the records of Execute and the maps against Task 2's dump
  slices, as Task 11. Expected: FAIL. Mutation RED: Execute without `use_copy_destination(seq)` →
  `execute_marks_staging_and_reissues_queries` fails.
- [ ] **Step 2: GREEN**; CK(d3d11); closure count. **Step 3: Commits**, one per module file with its tests
  (`immediate`, `deferred`, `command_list`, `ext`, `map`, `sync`), each `d3d11: the context's <module> in Rust (not
  linked)`.

### Task 14: the swap chain, queries, fences (lane `sw`)

**Files:** `src/d3d11/src/{swapchain.rs,swapchain/tests.rs,query.rs,query/tests.rs,fence.rs}` from
`d3d11_swapchain.cpp`, `d3d11_query.*`, `d3d11_fence.*`.

**Interfaces:** Consumes 5a's `Presenter`, `SpatialScaler`, `GammaRamp` through `IMTLDXGIOutput` slot 29, lane R3's
`Hud`, Task 6's frame statistics, 5c's `wsi` and Task 4's additions, 5a Task 3's `wmt_native_*` (frame-latency
semaphore natively). Produces Task 3's `swapchain`/`query`/`fence` items; `MTLD3D11SwapChain<EnableMetalFX>` with
`DXMT_METALFX_SPATIAL_SWAPCHAIN=1` (`:1149-1157`); its present command captures `SyncFrameState` (P8).

- [ ] **Step 1: RED.** `swapchain::tests::{occluded_when_minimised_or_zero_size, test_flag_returns_before_present,
  sync_interval_over_4_is_invalid, alt_tab_leaves_fullscreen}` (their HRESULTs from Task 2's `d11sc.txt`),
  `swapchain::tests::{vsync_duration_as_cpp, guest_paces_skips_latency_wait, frame_latency_fence_signals_at_retire
  (a waitable chain's fence is signalled when the present's chunk resets, not when the present command runs; the next
  `SyncFrame` waits for it, P8)}`; `query::tests::{data_sizes_as_cpp, occlusion_sums_readbacks}`. Expected: FAIL.
- [ ] **Step 2: GREEN**; CK(d3d11). **Step 3: Commits**, one per module file with its tests (`swapchain`, `query`,
  `fence`), each `d3d11: <module> in Rust (not linked)`.

### Task 15: the D3D10 object layer (lane `d10`)

**Files:** `src/d3d11/src/d3d10.rs`, `src/d3d11/src/d3d10/{device,buffer,texture,view,state,shader,input_layout,query,
util,tests}.rs` from `src/d3d10/d3d10_*.{cpp,hpp}` (17 classes; not `d3d10_blob.cpp`, not `d3d10core`).

**Interfaces:** Produces Task 3's `d3d10` items: the 16 wrapper vtables (QI/AddRef/Release/private data forwarded to
the D3D11 object; descriptors and flags converted as `d3d10_util.cpp:20-50`), `Device` (an `ID3D10Device1` over the
D3D11 device and immediate context; one private call, `so_get_targets_with_offsets`); the unwrap `d3d11_of(p)` reading
`Wrapper::d3d11` for every resource kind (`d3d10_device.cpp:324-342, 880-966`).

- [ ] **Step 1: RED.** `d3d10::tests::{flags_convert_both_ways, descs_as_the_probe (`include_str!` of Task 2's
  `d10.txt`: each D3D10 `Create*`'s desc and its D3D11 counterpart's), wrapper_layout_is_vtbl_then_d3d11,
  texture_unwrap_reads_the_buffer_layout}`. Expected: FAIL.
- [ ] **Step 2: GREEN**; CK(d3d11). **Step 3: Commits**, one per module file with its tests (`device`, `buffer`,
  `texture`, `view`, `state`, `shader`, `input_layout`, `query`, `util`), each `d3d11: the D3D10 <module> in Rust (not
  linked)`.

### Task 16: MacNeutron's same-pin edits (lane `mn`, `step5b-mn`)

Runs on Z2's commit (P24): on `step4-mn` when riding Z, on `dxmtrs` after Z's pin when riding 4b; first rebased past
what `step5c-mn` landed on `dxmtrs`. If 5c landed the flag's host plumbing, this task adopts it and keeps only the key
entry and its rows.

**Files:** `wine-arm64/lib.sh` (`translator_key` v2's D3D11 entry, P3), `wine-arm64/tests/translator_key_test.sh`;
only if 5c's plumbing has not landed: `eac-vm/d3d/gen.py` (after each call: `if (mn_not_implemented &&
mn_not_implemented()) return mn_d3d_not_in_dxmt(id, &ret);`, the `catch (...)` kept), `eac-vm/d3d/host/native.cpp`
(the six sites), `eac-vm/d3d/host/dispatch.c` (`dlsym` of `MTLD3DNotImplemented` from `winemetal`, optional),
`eac-vm/d3d/tests/test_notimpl.py`.

- [ ] **Step 1: RED.** `translator_key_test.sh` cases (v2's composition): a tree with `args.rs` and no `.cpp` hashes
  `args.rs`, a tree with neither dies, a tree with the `.cpp` keeps Z's value; if this task owns the plumbing,
  `test_notimpl.py` (every generated stub checks the flag after its call; a stub without it fails). Expected: FAIL.
- [ ] **Step 2: GREEN**; `sh wine-arm64/tests/translator_key_test.sh` ok; `make -C eac-vm/d3d DXMT=$S/wt-step5b/src
  I=$S/include-native GEN=$S/d3d-gen test stubs` ok (host only: command-line variables override the Makefile's `:=`,
  so it reads neither `$D`, the installed headers nor `build/eac-vm-work`).
- [ ] **Step 3: Commit** `translator key: D3D11's Rust arguments file` (and, if owned, `eac-vm d3d: the native
  not-implemented flag`).

### Task 17: references on the switch's base (owner, lock)

The base is P2's (Z's head or `main` with 4b) with `step5b` rebased onto it and `front-end` off, so the DLLs are the
C++'s; `step5b-mn` is P24's rebased branch. 5-D12's answer is ledgered. One or more lock jobs, `<M>` at `step5b-mn`:

- [ ] **Step 0: Merge, rebase, host references.** In order: every lane merged on `step5b`; `step5b` rebased onto P2's
  base (the input-layout commit with it, P25's scaffold dropped); `cargo metadata --locked`; one lock job `make
  llvm-nolto` (P24; a no-op while its `.complete` matches); RL with Z2's inputs → `rust-check: ok`; `$S/lane-build.sh`
  re-checked against Z1's meson environment (its configure line diffed with `wine-arm64/build.sh`'s on the rebased
  `step5b-mn`; a difference fixed in the script first, ledgered); then `LB(mac)`, `LB(native)`, `LB(guest64)`,
  `LB(guest32)` on a worktree of the rebased `step5b` (`front-end` off) → `$S/ref-lb/<kind>`, replacing Task 2's,
  tabulated: the C++ `d3d11.dll` import tables P16 diffs against, on the same base as the switch (the d3d12 rows
  included, whatever 5c has landed); `stats-golden.sh` re-run into 5a's stats reference if Task 19 keeps that row.
- [ ] **Step 1: Build and save.** `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter build/wine-arm64-tests/winshot`,
  `cp -cR` wine.app to `$S/ref-wine.app` (cdhash ledgered); `sh eac-vm/dxmt/build.sh` (both widths) and
  `sh eac-vm/dxmt-native/build.sh` → `$S/ref-guest`, `$S/ref-native`, tabulated.
- [ ] **Step 2: Mac.** check.sh both lanes → `$S/ref-perf/check-{x64,arm64ec}.txt` (0 FAIL; the row set Task 19
  expects); `sh eac-vm/dxmt/share.sh` (copies every `build/dxmt-tests/*.exe`, the probe included);
  `MN_REF_INVOCATIONS=$S/d3d11.invocations MN_REF_RUNS=2 MN_REF_OUT=$S/ref-ab/x64
  MN_REF_WINE_APP=$S/ref-wine.app sh eac-vm/dxmt/ref.sh`, and with `MN_REF_TESTS=build/dxmt-tests-arm64ec` →
  `$S/ref-ab/arm64ec`; after each, `air-dump.sh "$TMPDIR/macneutron dxmt-guest-ref/cache" $S/ref-air/<lane>`;
  `hist.sh` (5a's, which runs check.sh's own D3D11 rows; Task 0's copy also takes a program list) over `present_loop`
  and the programs of `$S/d3d11.invocations` with their arguments → `$S/ref-hist`; the same runs twice with
  `DXMT_WMTCMD_DUMP` (P26) → `$S/ref-dump`, the positions that differ between the two masked (the mask ledgered); the
  spatial-scaler run (5a's form) → `$S/ref-perf/spatial.txt`; P19's rows once (bench-once) → `$S/ref-perf/perf.txt`
  (`d3d11_draw.exe <S> perf` on `$S/ref-wine.app`'s x64 lane and natively against `$S/ref-native`).
- [ ] **Step 3: VM and native.** `MN_REF_RUNS=2 MN_REF_OUT=$S/ref-vm MN_REF_WINE_APP=$S/ref-wine.app sh
  eac-vm/dxmt/ref.sh` (47); present-gate's reference invocations → `$S/ref-present` (5a Task 1's form); the native
  gate once with `MN_REF_FROZEN=$S/ref-vm` → passes (the C++ native against its own Mac reference) and its runs'
  caches dumped by `air-dump.sh` → `$S/ref-air/native` (if the gate deletes them, ledgered and the native AIR row
  dropped); in the VM (guest stages as 5a Task 20) `d3d11-guest.sh i386 programs $S/ref-i386` (the C++'s i686
  `d3d11.dll`), each output compared with `$S/ref-ab/x64` by compare.py (differences ledgered as i686's own) and its
  caches dumped → `$S/ref-air/i386`.
- [ ] **Step 4: Expected.** Run 1 and run 2 of every reference equal (`mac-ab.sh` of a reference against itself,
  `air-compare.sh` of `.1` against `.2`, `hist-compare.sh`, the dump comparator after the mask); the probe still prints
  Task 2's goldens on this base (a difference is a base change: ledgered, the golden re-recorded, the lanes told);
  likewise `wmtcmd-compare.sh $S/ref-dump-early $S/ref-dump` after both masks (a difference re-cuts the slices lanes
  `cd`, `ct` and `cx` test against, and tells them). Mutation REDs (Task 1's, and one edited dump line failing the dump
  comparator) re-run.

### Task 18: the switch (owner)

**Files (DXMTRS):** `src/d3d11/meson.build` (`d3d11_front_end = true`; step 3 Task 12's recipe: `d3d11_res`,
`rust_runtime_dep`, `winemetal_dep`, the staticlib per target, `vs_module_defs : 'd3d11.def'`, the builtin
postprocess, natively 5a's export list; `dxbc_parser_dep`, `airconv_forward_dep` (include paths only,
`src/airconv/meson.build:70-72`; `SM50*` come from `winemetal_dep`), `dxmt_dep`, `util_dep` dropped;
`d3d11_dep` unchanged for `d3d10core`), deletions (P17), `src/dxmt/meson.build` (Step 4).

- [ ] **Step 1: Base check.** `step5b` is Task 17 Step 0's rebased head; if P2's base moved since, it rebases again
  (5c Task 17 Step 1's form: `make llvm-nolto` in a lock job, `cargo metadata --locked`) and Task 17's references are
  re-recorded first; RL with Z2's inputs → `rust-check: ok`.
- [ ] **Step 2: Switch.** The meson edit and P17's first deletion; `grep -rn` of every deleted header over the tree's
  remaining sources → empty. `LB(mac)`, `LB(native)`, `LB(guest64)`, `LB(guest32)`, `LB(mac) p` ok.
- [ ] **Step 3: Tables.** `ident-diff.sh $S/ref-lb <these LB outputs> ED1 ED3 ED10 ED11` → nothing (P16; `$S/ref-lb`
  is Task 17's, on this base); i686 `d3d11.dll` exports `@N`-free names with the `.def` ordinals; natively `nm -gU` =
  P16's list, OSO and `$HOME` 0; `d3d10core.dylib` resolves `_D3D11CoreCreateDevice` from `d3d11.dylib`. Lane D
  unlinked (P1, while 5c has not switched): each LB's link of `d3d11.dll` and `d3d12.dll` re-run with a map
  (`-Wl,-Map=$S/maps/<kind>-<dll>.map`) names no member of the `d3d12` crate's staticlib. Commit `d3d11: in Rust
  (d3d11.dll and .dylib), the D3D10 object layer with it; the queue and the encoder go live`.
- [ ] **Step 4: Unlinked core C++** (only if 5c has switched first; else skipped and ledgered for 5c's switch or
  Phase F, P17). The set is the link closure: the `src/dxmt` objects no remaining C++ (replay, anything else still
  C++) pulls, from `nm -u` of what remains and the archive's members, never from includes. Remove them from
  `src/dxmt/meson.build` and delete them. Check: the four LBs ok, `d3d12.dll`/`.dylib` and replay tables equal to
  Step 2's (ED1). Commit `dxmt: the C++ core no front end links goes`.
- [ ] **Step 5: Loads** (one lock job): build this head, `d3d11_render` and `present_loop` on the x64 lane as check.sh
  runs them → their lines; wine.app's `d3d11.dll` imports `api-ms-win-core-synch-l1-2-0` (Rust std) and no C++ runtime
  DLL; the key equals P2/P3's value; cdhash ledgered. A failure goes back to the owning lane before Task 19.

### Task 19: the switch's gates, once (owner)

Every row once on Task 18's head, as lock jobs in this order, 5a Task 20's job form (each checks `$D` and `<M>`
out detached first, `<M>` back to `dxmtrs` last; lanes idle during timed rows). A failing row is recorded, not fixed.
Reports `$S/reports/gates-{mac,native,vm,rust}.md`, step 2's shape; cdhash per build.

- [ ] **5a's rows, unchanged in form:** every row of 5a Task 20 (Mac, 5a's own rows, native and VM host, VM, identity,
  performance, p profile, Rust and licences), with this plan's references (`MN_REF_WINE_APP=$S/ref-wine.app`,
  `MN_REF_FROZEN=$S/ref-vm`/`$S/ref-present`, `$S/ref-perf/check-*.txt`'s row sets), their Expected lines as written
  there with Task 1's counts (`dxmt gate: all 47 programs passed`, `dxmt-native gate: all 33 programs passed`, loopback
  all 33, `d3d-smoke: ok` both modes and `MN_WOW64=new`, `present gate: all passed` both modes, `dxgi-guest <arch>:
  ok`, `d3d gate: all <n> programs passed`, `d3d9 gate: ok`, `rust-check: ok`). The native gate and the loopback gate
  run with `MN_REF_FROZEN=$S/ref-vm` (else each re-runs `ref.sh`).
- [ ] **5b's rows:**
  - Probe: check.sh's `D3D11's COM answers are the C++'s` on both lanes; natively the probe `fixed` against
    `.native` (and `notimpl`: each of the 19 sites `hr=` its failure value or `void`, `flag=1,0`; the `NOT_ON_C`
    unreachable cases at P10's results); `d11share` on both lanes against `.fixed`; `sh eac-vm/tests/d3d11-guest.sh
    x86_64` and `i386` → `d3d11-guest <arch>: ok` (the i686 `d11share` child included); the probe's `full` log file
    against `$S/ref-log` (P11's field masked) → equal.
  - After `sh eac-vm/dxmt/share.sh`, with the reference runtime unset so the gate's wine.app runs: `env -u
    MN_REF_WINE_APP MN_REF_INVOCATIONS=$S/d3d11.invocations MN_REF_RUNS=1 MN_REF_OUT=$S/run-ab/x64 sh eac-vm/dxmt/ref.sh`
    and the same with `MN_REF_TESTS=build/dxmt-tests-arm64ec` into `$S/run-ab/arm64ec`, each followed by `air-dump.sh`
    into `$S/run-air/<lane>`; then the proof line: `cmp` of the tool folder's `d3d11.dll` (`$TMPDIR/macneutron
    dxmt-guest-ref/ours/wine.app`) with `build/wine-arm64/wine.app`'s → equal, and with `$S/ref-wine.app`'s → differ;
    `mac-ab.sh $S/ref-ab/<lane> $S/run-ab/<lane>` → `mac-ab: equal (17 programs)` for each lane.
  - `air-compare.sh $S/ref-air/<lane> $S/run-air/<lane>` (x64, arm64ec; native from the native gate's caches; i386
    below) → `air-compare: equal`; the version rows and `a D3D11 and a D3D12 program open one cache table` ok.
  - `env -u MN_REF_WINE_APP` for `hist.sh` and the dump runs too: `hist-compare.sh $S/ref-hist <today's>` →
    `hist-compare: equal`; `DXMT_WMTCMD_DUMP` over the same runs → `wmtcmd-compare.sh $S/ref-dump <today's>` →
    `wmtcmd-compare: equal` (mutation RED: one edited field of today's fails); the spatial-scaler run equal to
    `$S/ref-perf/spatial.txt`; step 3's D3D11 fullscreen gamma row ok; `presenter/check.sh` 66 ok.
  - i686 programs: `d3d11-guest.sh i386 programs $S/run-i386` (the Rust i686 `d3d11.dll`) → each output by compare.py
    against `$S/ref-i386` (and against `$S/ref-ab/x64` as Task 17 did, with the same ledgered differences); its caches
    dumped and `air-compare.sh $S/ref-air/i386 $S/run-air/i386` → `air-compare: equal`.
  - Forwarder: `make -C eac-vm/d3d test stubs dispatch-test` (`test_notimpl` included); `threads.sh` (`threads11`),
    `pieces.sh`, `identity.sh`, `answers.sh`, `wrapped.sh`, `both.sh wire` ok; `coverage.py` unchanged; the VM host's
    log shows `MTLD3D11WaitIdle` resolved (no "none: an older DXMT" line).
  - P19's performance rows against `$S/ref-perf/perf.txt` (the `perfinfo` ns-per-call rows of `d3d11_draw.exe <S>
    perf` on the x64 lane and natively, `present_loop` labelled display-bound), each within 5% or one control run.
  - p profile: `strings` of the p `d3d11.dll` has no debug-level literal Task 0 names from the C++ (positive control: the
    std build has it); `llvm-nm` of `libd3d11.a` (p) has no `dxmt_stats_`.
  - Rust: RC (Z2's inputs, P24); `winemetal-layouts.sh`'s modes (`--com` with the records list included) regenerate
    equal; licences per target.
- [ ] **Where lanes R and E go live** (5a review M5: no W test goes live on a lane's reading alone): each row above
  that pins them, ledgered as passed: rings, staging and dynamic pools → `d3d11_dynamic`, `d3d11_transfer` (its dynamic
  updates), `d3d11_deferred`'s `staging` and `nooverwrite`, `pieces.sh`; queue, arena, threads, drop timing →
  `threads11`, `d3d11_deferred` (`twice` included), `present_loop`'s frame time (the thread names are kept by rule: no
  gate reads them); binding, access tracking, argument buffers → `d3d11_render`, `d3d11_compute`, `d3d11_vsia`,
  `d3d11_hazards`, `d3d11_draw`, the histograms and dumps; passes, flush, optimizer → the histograms and dumps,
  `d3d11_render`'s `msaa` and `stencil`, `d3d11_hazards`; queries → `d3d11_query`, `d3d11_draw`'s predication,
  `d3d11_deferred`'s `query`, the probe's `d11query`; initializer → `d3d11_transfer`'s initial data, `d3d10_core`'s
  `init`; counters → `d3d11_compute`'s `append` and `omuav`; emulated commands → `d3d11_render`'s `mips`,
  `d3d11_draw`'s GS case (or its refusal) and the dumps (tessellation on lane `cd`'s reading, ledgered); library and
  clears → `d3d11_render`, check.sh's clear rows; capture → check.sh's capture rows; i686 branches → the i686 programs
  and `d11share`.

### Task 20: one fix pass (owner, lanes as needed)

- [ ] **Step 1:** per failing row: root cause, a failing test or table row first, the fix in the owning file, the
  task's CK, the row re-run once, and every gate whose inputs the fix touched (named per file in the ledger). A defect
  needing a second round goes to the maintainer.
- [ ] **Step 2:** commit each fix (`<area>: …`); the key and RC again after the last.

### Task 21: spec, push, pin (controller)

- [ ] **Step 1: Spec**, on `dxmtrs`: §5 step 5 gains "5b done" with the DXMTRS sha and this plan's corrections (the
  D3D10 layer in `src/d3d11/src/d3d10`; the key's D3D11 entry `src/d3d11/src/shader/args.rs`; the not-implemented flag
  and the cache version landed by 5c's groundwork, P23; the core C++ goes by link closure with D3D12's, P17;
  `libs/DXBCParser` waits for 5c). Privacy-scan spec, plan, ledger.
  Commit `Spec: DXMTRS step 5b, D3D11 in Rust`.
- [ ] **Step 2: DXMTRS main** (lock: pipeline rule 5's restore of `$D`, then outside it): privacy-scan `git log -p
  main..step5b`; if P2's base is Z's head, Z's commits go first as 4a Z5 orders them; `git -C $D fetch . step5b:main`
  (fast-forward only), `push --dry-run`, push. Worktrees and lane branches removed (`git cherry` empty).
- [ ] **Step 3: Pin** in the key-moving bump (one pin bump, one or two commits: 4a Z5 squashes `step4-mn` into its
  own commit when 5b rides Z): `git merge --squash step5b-mn` on `dxmtrs`, `DXMT_COMMIT=<main's sha>`; commit `DXMT:
  D3D11 in Rust (DXMTRS <sha>)`.
- [ ] **Step 4: Pinned build** (one lock job): `make wine-arm64` re-fetches; `DXMT/version` = `<sha>+<series>`;
  `DXMT/translator` = the moved key; `git -C $D status --porcelain` empty; `ident-diff.sh` of `d3d11.dll` against Task
  19's → equal but the version strings; x64 check.sh 0 FAIL.
- [ ] **Step 5: MacNeutron main** fast-forward, `push --dry-run`, push; a failed fast-forward here or in Step 2 stops for
  the maintainer. Program ledger: 5b done, the pinned sha, 5-D12's title (or "none yet") for the milestone, what of
  `step5w` remains for 5c.

## Review record

Three adversarial reviews (5-D14), 2026-10-30; each finding checked against the source before it was applied.

1. Critical, captures dropped at the run (P8): applied. Checked `dxmt_command_list.hpp:62-71`, `dxmt_command_queue.cpp:196-213`, `d3d11_swapchain.cpp:719-759, 810-828`, `d3d11_context_imm.cpp:606-650`, `d3d11_context_impl.cpp:5459-5488`, 5a E1's `FnOnce`. P8's drop-timing requirement; `Chunk::emit` as `Fn` and a generic `retain` in Task 6; tests `arena_drops_at_reset_not_at_run` (Task 6), `frame_latency_fence_signals_at_retire` (Task 14), `executed_list_recycles_after_completion` (Task 13).
2. Important, "the device drains first" untrue (P8 SAFETY): applied. Checked `d3d11_device.cpp:1153-1159, 1306-1312` and `dxmt_command_queue.cpp:75-91`. P8's SAFETY names both arguments; Task 9's `Device` `Drop` drains, ledgered as a deliberate change, test `drop_drains_before_caches`.
3. Important, `UNREACHABLE` undefined: applied. Checked `d3d11_private.h:27-29`, `dxmt_context.hpp:40` and the 14 sites. P10's rule; each `DXMT_UNREACHABLE` site's result in Task 0's audit; `:97` → `E_INVALIDARG`, probed `NOT_ON_C`.
4. Important, panics end only a thread natively: applied. P13 and P20: every spawned thread body takes the guard; tests `worker_panic_aborts` (Task 8) and `encode_thread_panic_aborts` (Task 6).
5. Critical, no D3D11 test of indexed/indirect/predicated draws: applied. Checked: no D3D11 program calls them, `d3d11_pipeline` prints only HRESULTs. New `d3d11_draw` (Task 1), counts 47/33/33 and 17 programs with finding 12's program too; tessellation ledgered on lane `cd`'s reading; `d3d11_pipeline` dropped from the emulated-commands mapping; Task 11's tests run closures against lane E's `Encoder` over the fakes and decode records.
6. Important, no performance row sees per-command cost: applied with one change. QPC-timed sections in `d3d11_draw.exe <S> perf`, warm-up, ns per call, `present_loop` labelled display-bound, the `d3d11_dynamic` wall-time row dropped. Recorded once, not twice: P19 and the maintainer's bench-once rule keep one run per performance row with a control run as the noise check.
7. Important, deferred lists' queries, staging and updates unpinned: applied. Checked `d3d11_deferred.cpp:52-85` and `d3d11_context_imm.cpp:601-657`. Cases `twice`, `restore`, `query`, `staging`, `update`, `nooverwrite`; Review Focus 2 corrected; Task 13 tests.
8. Important, dynamic `UpdateSubresource` branches unrun: applied. Checked `d3d11_context_impl.cpp:1228-1265, 4213-4245` and `d3d11_transfer`'s DEFAULT `ub`. Task 1's transfer cases; Task 12's `dynamic_update_paths_as_cpp`, `contiguous_3d_honours_depth_pitch`.
9. Important, sharing and keyed mutexes untested: applied. Checked: `smoke.cpp`'s `shared` expects a refusal; no test calls `OpenSharedResource` or `AcquireSync`. Probe family `d11share` (both Mac lanes, an i686 child in the guest).
10. Important, Present1's branches untested at runtime: applied. Checked `d3d11_swapchain.cpp:765-791` and `d3d12_fullscreen.cpp:157`. `d11sc`'s iconic-window cases, their answers recorded from the C++ rather than asserted here.
11. Important, histograms miss wrong values: applied. P26's `DXMT_WMTCMD_DUMP` (Task 5, beside 5a's knob), early dumps for the lanes' slices (Task 2 Step 4), reference twice with a mask (Task 17), `wmtcmd-compare: equal` with a mutation RED (Task 19); Rules check lists the knob and its static.
12. Important, no race-sensitive D3D11 program: applied. `d3d11_hazards` (Task 1), in `invocations`, both `ROWS` and the Mac A/B.
13. Important, i686 never exercised: applied. `const` layout asserts, the digest's width a parameter (4 and 8), a target-parameterised placement test, the i686 programs in guest mode (`$S/ref-i386`, `$S/ref-air/i386` at Task 17; compared at Task 19). The width-4 golden is derived from the C++'s marked stream (no 32-bit host run exists), the i686 cache rows pin it at runtime.
14. Important, `*_as_cpp` tests written by the coder: applied. Task 2 commits `d11ctx`, `d11sc` and `d10` golden lines for lanes `cs`, `sw`, `d10`; those lanes wait for Task 2. Lane `x`'s version golden is moot: the cache version is 5c's (finding 19).
15. Important, only `ResizeTilePool` probed natively: applied. Checked the 19 sites. `notimpl` calls every one, `hr=` and `flag=1,0`.
16. Important, the A/B ran the C++ on both sides: applied. Checked `ref.sh:19` and the native gate's `ref.sh` call. `env -u MN_REF_WINE_APP` for the A/B, AIR, histogram and dump runs, a `cmp` proof line, native and loopback gates with `MN_REF_FROZEN=$S/ref-vm`.
17. Critical, CK's `--features d3d11/front-end` without `d3d11`: applied. Reproduced on Rust 1.99.0 in a scratch workspace (`the package 'a' does not contain this feature: b/front-end`). The feature is added only when `d3d11` is among the crates.
18. Critical, MacNeutron scripts stale on Z's tree: applied. Checked 4a T1 and Z2 and `wine-arm64/lib.sh:174-189` (v1 dies without `d3d11_shader.cpp`). P24: `step5b-mn` rebases onto `step4-mn` (riding Z) or `dxmtrs` after 4b; Task 16 on Z2's commit; RL/RC with Z2's inputs; the merge, rebase, `make llvm-nolto` lock job, RL and `lane-build.sh` re-check moved to Task 17 Step 0, before its LB references (Task 18 Step 1 only re-checks the base).
19. Critical, 5c's groundwork duplicates 5b's: applied with three refinements. P23 makes 5c's Tasks 1-4 the owner (landed on `step5w` before `step5b` is cut); Task 4 shrinks to SHA-1 and the WSI calls 5c lacks (both absent from 5c's groundwork), Task 5 to the chain mirrors `sm50` lacks plus finding 11's knob, no `com-vtables.sh`, no `com::Counts`, no env plumbing. Kept and named: `d3d11::com::Managed` (`ManagedDeviceChild` is not 5c's two-count `ComObject`), `--com`'s records list (5c's mode emits vtables only), and P20 adopts 5c's `"system-unwind"` slots instead of forking the generator, with 5c P6's own companion structs for the `IMTLD3D11*Ext` slots. 5c's plan is cited by task and name, its line numbers having moved.
20. Important, deleting the core C++ breaks `d3d12.dll`'s link: applied. Checked `dxmt_command.cpp`'s classes, `d3d12_device.cpp:366`, `d3d12_swapchain.cpp:182` and the native `d3d12.dylib`'s `CommandQueue::CommandQueue`, `ArgumentEncodingContext::startRenderPass`. P17: deletion by link closure at 5c's switch or Phase F; Task 18 Step 4 only if 5c switched first.
21. Important, `$S/ref-lb` stale and the lane-D proof vacuous: applied. Task 17 Step 0 re-records the LB references on the switch's base, after the rebase and the `lane-build.sh` re-check; Task 18 Step 3 proves lane D unlinked by link maps; stats reference re-recorded if its row stays.
22. Important, merging `step4a-translator` into lane `st`: applied. P25, 5c P3's scaffold form; the input-layout commit last; the rest rebased and re-checked; the key exception ledgered.
23. Important, no `gaps.rs` stub: applied. Task 3 stubs `encoder/gaps.rs` and its `mod` line; `version.rs`'s `mod` line is 5c's (finding 19). Critical path `3 → 6 → 11, 13 → 17 → 18 → 19`.
24. Important, `make -C eac-vm/d3d` reads `$D` and installed headers: applied. Checked `eac-vm/d3d/Makefile:4-8`. `DXMT=$S/wt-step5b/src I=$S/include-native GEN=$S/d3d-gen` (Task 0 Step 2's worktree, Task 2 Step 3's private copy); a Global Constraints line for such commands.
25. Important, 5c's MacNeutron edits conflict: applied. P24 rebases `step5b-mn` past what `step5c-mn` landed; Task 16 adopts 5c's flag plumbing and keeps only the key entry and its rows.
26. Important, `vcs_tag` is a build-time file: applied. Checked root `meson.build:153-157, 221`. P12 adopts 5c P8's form (header paths, the `vcs_tag` dependency, `build.rs` in the front end; `dxmt-core` takes no build input).
27. Important, a test-only `cc` build script: applied through finding 19. The flag and its C test are 5c's; 5b checks `nm -gU` only; no `cc` edge anywhere.
28. Important, one commit per lane: applied. Task 7 three commits, Task 8 two plus the input-layout one, every other lane one commit per module file with its tests (Execution's rule).
