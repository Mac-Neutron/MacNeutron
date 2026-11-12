# DXMTRS C4b-3a: Wine's stateblock test on D3D9's core runtime (API state and inert objects)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, Wine's `stateblock` conformance suite runs on the Mac's core lane with the default
lane's result in the same job (0 failing sites, 0 crashes, executed count within 0.1%), by un-refusing the API state and
the inert objects it needs, with every mtld3d crossing behind them cut under core.

**Architecture:** the front end already holds the API state outside mtld3d (`StageBindings`, `FfState`, `BoundBuffers`,
`BoundRt`, state blocks). C4b-3a removes `refuse_under_core!` guards whose bodies are front-end only, guards each mtld3d
crossing **inside the `DeviceInner` method that makes it** (state-block Apply calls those methods directly, not the entry
points), creates vertex/index buffers, textures and additional swap chains as inert objects (CPU staging only, no Metal
object, no warmup or destroy op), refuses their lock/upload/generate methods under core, resets stream 0 and the indices
after a core UP draw as D3D9 does, and refuses a draw whose fixed-function cascade reads a bound texture. Drawing with
textures, transforms, MRT, depth and scissor stays refused: C4b-3b (textures, samplers, uploads, the full fixed-function
pipeline) and C4b-3c (targets, MRT, depth, scissor) follow.

**Tech Stack:** Rust (src/d3d9/src COM front end, src/d3d9/core mtld3d-core), POSIX sh and C++ (MacNeutron's dxmt/, vm/).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: `d3d9.dll` on the core, behind a switch until
parity). Outline: `docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4b's gate: conformance stateblock
under the core lane on the Mac). C4b-2's split of C4b-3: `docs/superpowers/plans/2026-11-11-dxmtrs-c4b2-programmable-draws.md`
(Scope). Maps: `build/c4b3/map-{frontend,stateblock,core,ff-samplers}.md`. Reviews: `build/c4b3/review-c4b3a-{1-design,
2-core,3-exec}.md` (folded; record at the end).

**Bases:** DXMTRS `main` 17355a78 → `c4b3a` (`build/c4b3/dx`); MacNeutron `main` (17355a78's pin) → `c4b3a-mn`
(`build/c4b3/mn`). Ledgers: the program ledger and `.superpowers/sdd/<this plan>/progress.md`. Sites below are named by
function (line numbers drift).

## Scope

In:
- **API state, un-refused** (front-end bodies): Set/Get/MultiplyTransform, Set/GetMaterial, SetLight/GetLight/LightEnable/
  GetLightEnable, Set/GetTextureStageState, Set/GetSamplerState (pixel and vertex slots), Set/GetTexture (pixel and vertex
  slots), Set/GetScissorRect, Set/GetStreamSource, Set/GetStreamSourceFreq, Set/GetIndices, Create/Begin/EndStateBlock,
  and the state block's Capture/Apply/GetDevice on a core device.
- **Inert objects:** CreateVertexBuffer, CreateIndexBuffer, CreateTexture (incl. `D3DUSAGE_DEPTHSTENCIL`'s
  `create_depth_texture_path`), CreateCubeTexture, CreateVolumeTexture (all pools), CreateAdditionalSwapChain,
  GetSwapChain, GetNumberOfSwapChains, the chain's GetBackBuffer, GetPresentParameters and GetDevice. No warmup op at
  creation, no destroy/retire op at release (also when the device finalizes an object at teardown). Methods that reach
  mtld3d answer D3DERR_NOTAVAILABLE under core, once per name: buffer Lock/Unlock; texture LockRect/UnlockRect/LockBox/
  UnlockBox/AddDirtyRect/AddDirtyBox/GenerateMipSubLevels; texture-level surface paths are already refused by
  `refused_under_core`. Front-end methods work: GetDesc, GetLevelDesc, GetLevelCount, Set/GetLOD (its vertex-sampler row
  push is guarded), GetSurfaceLevel, GetCubeMapSurface, GetVolumeLevel, Set/GetPriority, private data, GetType,
  GetContainer, PreLoad (already a no-op).
- **Render-target switch, front-end half:** SetRenderTarget(0, s) where `s` is a back buffer surface with no parent texture
  (the implicit one or an additional chain's: both alias the core back buffer, as additional chains alias the device back
  buffer under mtld3d, STATUS.md's documented divergence), after the existing argument, ownership and usage checks;
  SetRenderTarget(n > 0, NULL); GetDepthStencilSurface (already D3DERR_NOTFOUND with a NULL out-pointer on a core device);
  SetDepthStencilSurface(NULL). Any other surface, or a non-NULL depth surface, stays refused.
- **Draws:** a new `CoreRefusal::FfTexture` for a draw whose fixed-function cascade reads a bound texture; the
  pre-transformed FF VS key takes the real pixel bound mask, as mtld3d's does; after a successful core UP draw, stream 0
  is unbound (and the indices, for an indexed UP draw), as mtld3d's arms do.
- **Gates:** a core conformance lane (stateblock only), a suite subset in the lane compare, `d3d9_triangle state` rows in
  every D3D9 lane (Mac and VM).

Out (named owners): uploads, sampling draws, samplers, the full FF VS/PS, texture creation in the core (C4b-3b); colour and
depth targets, MRT, Z/stencil clears, scissor, auto depth-stencil (C4b-3c); buffer locks, non-UP draws, the other three
suites under core, Reset (C4c).

## Global Constraints

- The default runtime (`mtld3d`) is unchanged: every edit under `src/d3d9/src` is behind `is_core()` (or the object's
  equivalent) or deletes a guard whose body is runtime-agnostic. Job A proves it.
- No mtld3d crossing reachable on a core device. The guarded methods (each guarded once, inside itself): `push_vertex_sampler_row`,
  `set_vertex_texture_slot` (store the binding; skip `rehydrate_for_device`, `promote_cpu_only_texture`, `flush_dirty_mips`
  and `SetVertexTextureOp`), `push_texture_warmup`, `push_texture_warmups`, `push_buffer_warmup`, `retire_released_buffer`
  (drop the backing, no op), `queue_vbib_retention`, `unbind_extra_render_target`'s op push, `push_depth_binding_op`,
  `rehydrate_for_device`'s slow path (`push_texture_warmup`, `flush_converted_mips`). Anything else reaching
  `current_frame`, `encoder`, `push_control`, `try_push_control`, `record_draw`, `push_stage_upload`,
  `push_color_rt_binding_op`, `retire_color_target`, `retire_depth_target`, `DestroyTextureOp` or `unix_call` stays behind
  a refused entry point. A miss panics; the gate shows it as `crash=1` or the panic text in the logs.
- Translator key unchanged (no airconv edit); D3D10-12 untouched; the core runtime only in `DXMT_D3D9_CORE=1` builds.
- Every un-refused entry point and every Review Focus line has a gate row (stateblock under core, or Task 4's rows).
- Standing rules: the pipeline lock for builds, `$D` checkouts and gates; no `make` in a worktree (jobs detach the main
  checkout at `c4b3a-mn` and restore it); test exes rebuilt by the job (Task 4 edits the .cpp); no game runs; every job
  held while the maintainer is gaming; benches once (none planned: the only per-draw addition is one key build when a
  texture is bound); push after the final review (standing authorization).
- DXMTRS rules: docs/CONVENTIONS.md (audit), `tools/rust-check.sh` green; MacNeutron: privacy scan before push.

## Review Focus

1. An object released after the device (`detach_from_device`) or finalized by the device's teardown: no crossing.
2. A D3DSBT_ALL Apply on a core device: it calls `set_vertex_sampler_slot_state` and `set_vertex_texture_slot` for every
   vertex slot, NULL included; both are guarded inside.
3. A SYSTEMMEM texture set on a pixel and a vertex stage under core: stored, never promoted.
4. An FF draw with a texture bound on an active stage: refused (FfTexture), the target keeps its clear colour; on a
   disabled stage: drawn.
5. SetRenderTarget(0, an additional chain's back buffer) then Clear and read-back under core: the clear lands on the
   shared back buffer; the chain's Present stays refused.

---

### Task 1: the fixed-function texture refusal and the bound mask

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (`DrawFlags`, `CoreRefusal`, `core_draw_kinds`),
`src/d3d9/core/src/pipeline_key/tests.rs`, `src/d3d9/src/device/core_draw.rs` (`resolve`).

**Interfaces:** Produces `DrawFlags::FF_PS_TEXTURES`, `CoreRefusal::FfTexture` (name
`"IDirect3DDevice9::Draw*PrimitiveUP (a fixed-function stage reading a texture)"`).

- [ ] Step 1: test `ff_cascade_reading_a_texture_is_refused`: facts `LAYOUT_RHW | FF_PS_TEXTURES`, no PS →
  `Err(CoreRefusal::FfTexture)`; with `PS_BOUND | PS_CORE` too → `Ok((Ff, Prog))`; and `FfTexture.name()` is the string above.
- [ ] Step 2: `cargo test -p mtld3d-core pipeline_key` → FAIL (no `FF_PS_TEXTURES`).
- [ ] Step 3: add the flag (next free bit), the variant and its name; in the FF PS arm, `Err(FfTexture)` when the flag is
  set, before the constant-rows check.
- [ ] Step 4: `cargo test -p mtld3d-core` green.
- [ ] Step 5: in `resolve`: `mask = (stage_bindings().bound_mask() & 0xff) as u8` (`cached_bound_texture_mask` is written
  only by mtld3d's snapshot and is 0 on a core device). Pass `mask` to the pre-transformed FF VS's `build_vs_key`. With no PS
  bound and `mask != 0`, set `FF_PS_TEXTURES` when `build_ps_key(&rs, mask) != build_ps_key(&rs, 0)` (only `HAS_TEXTURE`
  differs: the key says whether an active stage reads the texture); `mask == 0` builds the key once, as today.
- [ ] Step 6: rust-check green; commit `d3d9: a core draw whose fixed-function cascade reads a bound texture is refused`.

### Task 2: API state, state blocks and the UP draw's unbinds on a core device

**Files:** Modify `src/d3d9/src/device.rs`, `src/d3d9/src/device/core_draw.rs` or the UP arms in `device.rs`,
`src/d3d9/src/texture.rs` (`rehydrate_for_device`).

- [ ] Step 1: delete the `refuse_under_core!` of each Scope API-state entry point.
- [ ] Step 2: guard, inside each, `push_vertex_sampler_row` and `set_vertex_texture_slot` (Global Constraints) and
  `rehydrate_for_device`'s slow path, on `is_core()`. SetLOD's `refresh_vertex_texture_lod` then pushes nothing.
- [ ] Step 3: audit `state_block.rs` (`capture`, `from_recording`, both `apply_to`, Release) and every setter's recording
  arm; list each `DeviceInner` method called; any crossing not covered by Step 2 gets its guard inside the method.
  The list goes in the commit message.
- [ ] Step 4: after a successful core `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP`, run the same stream-0 reset (and index
  reset for the indexed form) as mtld3d's arms (`reset_stream0` and the index-buffer reset), sharing their code.
- [ ] Step 5: rust-check green; commit `d3d9: API state and state blocks on a core device; a core UP draw unbinds stream 0`.

### Task 3: inert buffers, textures and swap chains; the render-target switch

**Files:** Modify `src/d3d9/src/device.rs` (create paths, `push_texture_warmup(s)`, `push_buffer_warmup`,
`retire_released_buffer`, `queue_vbib_retention`, `device_set_render_target`, `unbind_extra_render_target`,
`push_depth_binding_op`), `src/d3d9/src/device/runtime.rs`, `src/d3d9/src/{texture,vertex_buffer,index_buffer,swapchain,surface}.rs`.

**Interfaces:** Produces `pub(crate) fn device_is_core(dev_inner_raw: usize) -> bool` in `device/runtime.rs` (false for 0,
a detached object), used by the object methods' guards.

- [ ] Step 1: delete the create guards (and GetSwapChain/GetNumberOfSwapChains'); guard `push_texture_warmup`,
  `push_texture_warmups`, `push_buffer_warmup`, `retire_released_buffer` (drop the backing) and `queue_vbib_retention`
  inside themselves.
- [ ] Step 2: refuse under core, once per name, the Scope's crossing object methods with `device_is_core`.
- [ ] Step 3: swap chains: delete GetBackBuffer's guard; Present, GetFrontBufferData, GetRasterStatus, GetDisplayMode and
  the Ex methods stay refused. GetPresentParameters and GetDevice are front-end (confirm, delete any guard).
- [ ] Step 4: SetRenderTarget: after its existing checks, on a core device accept index 0 with a back-buffer surface
  without a parent texture (a new `Direct3DSurface9` accessor over the private `ImplicitKind::Backbuffer`): front-end
  half only (`replace_render_target`, viewport and scissor reset), returning before `publish_render_target_write`, the
  autogen op and the binding push; accept index > 0 with NULL (`unbind_extra_render_target`'s op push guarded); refuse the
  rest. Delete GetDepthStencilSurface's guard; SetDepthStencilSurface accepts NULL (`push_depth_binding_op` guarded) and
  refuses the rest.
- [ ] Step 5: rust-check green; commit `d3d9: inert buffers, textures and swap chains, and the back-buffer target switch, on a core device`.

### Task 4: `d3d9_triangle state` rows in every D3D9 lane

**Files:** Modify MacNeutron's `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/tests/d3d9.sh`,
`vm/dxmt/d3d9-gate.sh`.

- [ ] Step 1: a `state` mode (own launch, `strict` under core, 30 s timeout like `shaders`), one `ok`/`FAIL` row each:
  `state-xform` (MultiplyTransform then GetTransform), `state-material`, `state-tss`, `state-sampler` (pixel and vertex
  slot), `state-scissor`, `state-freq`, `state-swapchain` (CreateAdditionalSwapChain, GetSwapChain(0),
  GetNumberOfSwapChains == 1, the chain's GetPresentParameters/GetDevice/GetBackBuffer), `state-rt` (SetRenderTarget(0,
  chain back buffer), Clear red, GetRenderTargetData reads red, restore; SetRenderTarget(1, NULL); GetDepthStencilSurface
  is NOTFOUND with NULL; SetDepthStencilSurface(NULL)), `state-tex` (2D, cube, volume and SYSTEMMEM 2D creation;
  GetSurfaceLevel, GetLevelDesc, GetLevelCount, SetLOD; bound on pixel stage 0 and vertex slot 0; released after a device
  in a second device's run), `state-lock` (VB Lock: NOTAVAILABLE under core, D3D_OK under mtld3d), `state-sb`
  (CreateStateBlock(ALL) after binding a texture, Apply, GetDevice), `state-up-unbind` (SetStreamSource(0, vb) then a UP
  draw: GetStreamSource(0) is NULL), `state-fftex` (a 4x4 green texture on stage 0, MODULATE TEXTURE, an FF
  pre-transformed white quad over a blue clear: under core the draw answers NOTAVAILABLE and the centre stays blue; under
  mtld3d it is green), `state-fftex-off` (the same texture with stage 0 COLOROP DISABLE: drawn white in every lane).
- [ ] Step 2: d3d9-check: each launch has an expected set of core refusal names; `state`'s is exactly the FfTexture name
  plus the object-method refusals `state-lock` provokes (`IDirect3DVertexBuffer9::Lock`), `shaders`' stays the crossing
  row's (C4b2-8). `refusals_ok` takes the launch name and its set; any other refusal line fails the lane.
- [ ] Step 3: the VM scripts run `state` in every D3D9 lane (x64 and x86, old and new WoW64, both runtimes) with the
  same rows and refusal sets, as C4b-2 added `shaders`.
- [ ] Step 4: commit (MacNeutron) `dxmt, vm: d3d9_triangle state rows in every D3D9 lane`.

### Task 5: the core conformance lane

**Files:** Modify MacNeutron's `dxmt/mtld3d-conformance.sh`, `dxmt/d3d9-conf-compare.sh`.

- [ ] Step 1: `D3D9_RUNTIME=core` (air only) runs: stamp `mtld3d-conformance: lane core (runtime.mode=core)`; requires
  runner args `--only stateblock` (detected by scanning the args; anything else exits 2 naming C4c); unsets
  `MTLD3D_CONFORMANCE_RAW_DIR`; deletes and recreates `logs-core`; sets
  `DXMT_CONFIG=runtime.mode=core;log.dir=Z:<B>/mtld3d-conformance/logs-core` and adds `--log info` (the runner's default
  `off` silences every line). Its proof replaces the kinds row: the logs hold `d3d9 core: device on dxmt-core`, not
  mtld3d.so's encoder line (`texture versions: rename pool cap`), no `is not implemented under runtime.mode = core` line,
  no `panicked` line.
- [ ] Step 2: any lane with `--only stateblock` skips the kinds row (stateblock writes no shader record) and writes
  `run-<lane>-stateblock.txt` (the full runs' files are kept).
- [ ] Step 3: `MTLD3D_PE=<d3d9.dll>` runs that build of the PE side (as d3d9-check), for the mutation REDs.
- [ ] Step 4: `d3d9-conf-compare.sh --suites <list>` registers and requires only the named suites; the executed count is
  compared against the base file of the same job.
- [ ] Step 5: commit (MacNeutron) `dxmt: the core conformance lane (stateblock) and a suite subset in the lane compare`.

### Task 6: gates

- [ ] Step 1 (Job A, plain build of the c4b3a tip): rust-check; `make wine-arm64` and the test exes; conformance default
  (all four suites) no worse than the base run (the three triaged device.c sites); d3d9-check default and msl lanes;
  imports and section sizes as C4b-2's Job A; translator key unchanged.
- [ ] Step 2 (Job B, `DXMT_D3D9_CORE=1`): conformance `--only stateblock` default and core lanes; `d3d9-conf-compare.sh
  --suites stateblock --cur-lane core`; d3d9-check core and default.
- [ ] Step 3 (Job C): the VM's D3D9 lanes (x64 and x86, old and new WoW64, both runtimes), from the main checkout.
- [ ] Step 4: mutation REDs, each built under the lock and run through `MTLD3D_PE`, then reverted: (a) drop
  `set_vertex_texture_slot`'s guard → the core stateblock lane shows a crash or a `panicked` line; (b) drop the
  `FF_PS_TEXTURES` refusal → `state-fftex` FAILs under core (strict); (c) drop `retire_released_buffer`'s guard →
  `state-tex`/stateblock under core crash at release.

### Task 7: review and push

- [ ] Final whole-branch Opus review; one fix pass for Critical/Important; privacy scan; push DXMTRS `main` and MacNeutron
  (pin, scripts, tests, a spec §5 paragraph); post-push x64 build check.

## Rules check

New: one `DrawFlags` bit and one `CoreRefusal` variant (fields of existing types, no static); one `pub(crate)` helper
(`device_is_core`; docs/CONVENTIONS.md allows `pub(crate)` only where its section says: check at Task 3, else a private fn
per module); no config key (`runtime.mode` exists); no wire field; no dependency; no derive; no lint suppression; no
duplicate (the stream-0 reset shares mtld3d's code). Gates enforcing: `tools/rust-check.sh` (fmt, clippy, doc, test,
audit), d3d9-check, the conformance runner and lane compare.

## Rulings

- **C4b3a-1 (split).** C4b-3 splits into 3a (this), 3b (textures, samplers, uploads, full FF VS/PS) and 3c (targets,
  MRT, depth, scissor), as C4b split into C4b-1/2/3. Cost if wrong: three pushes instead of one.
- **C4b3a-2 (inert).** Buffers and textures are the front end's real objects with CPU staging and no Metal object; how and
  when C4b-3b gives a texture its core storage is C4b-3b's decision (the outline's upload model). Cost if wrong: none here.
- **C4b3a-3 (refuse by key difference).** A cascade reads a texture exactly when the FF PS key with the real bound mask
  differs from the key with none (only `HAS_TEXTURE` depends on the mask); it over-refuses an active stage whose args do
  not name TEXTURE. Cost if wrong: `state-fftex-off` or `state-fftex` shows it.
- **C4b3a-4 (aliasing back buffers).** An additional swap chain's back buffer is the core back buffer, as under mtld3d
  (STATUS.md's divergence). Cost if wrong: C4b-3c gives additional chains their own target.
- **C4b3a-5 (stateblock only).** The core conformance lane runs only stateblock until C4c; the compare takes a suite
  subset. Cost if wrong: none (the other suites are C4c's gate).
- **C4b3a-6 (guards inside methods).** Every crossing reached from an un-refused path is guarded once inside the
  `DeviceInner` method that makes it, not at entry points: state-block Apply and teardown call the methods directly.
  Cost if wrong: a missed site, which the gates show as a crash.

## Review record

- Design (`review-c4b3a-1-design.md`): 0 Critical, 4 Important (post-draw unbinds; the bound mask; missing rows; C4b3a-2's
  clause), 5 Minor; all folded.
- Code reality (`review-c4b3a-2-core.md`): 0 Critical, 6 Important (Apply bypasses entry points; two crossings; the mask;
  guards inside methods incl. the depth-texture path and teardown; post-draw unbinds; SetRenderTarget's shape), Minors;
  all folded.
- Execution (`review-c4b3a-3-exec.md`): 2 Critical (`--log info`; the kinds row under `--only stateblock`), 9 Important
  (`log.dir` precedence; the panic signal; `--suites` registration; `MTLD3D_PE`; strict refusal row and its own name;
  per-launch refusal sets; coverage; VM scripts; Job A and the standing rules), Minors; all folded.
