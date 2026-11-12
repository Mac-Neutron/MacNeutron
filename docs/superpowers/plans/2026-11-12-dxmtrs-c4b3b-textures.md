# DXMTRS C4b-3b: textures, samplers and the full fixed-function pixel cascade on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, D3D9 draws sample 2D, cube and volume textures in programmable pixel shaders
(incl. `texbem`) and in the fixed-function pixel cascade (every stage, stage constants, TFACTOR, bump-env), with the
application's texels uploaded through the core, its sampler states honoured, and texture locks, dirty rects,
`UpdateTexture` and automatic mipmaps working, gated by `d3d9_triangle textures` rows in every D3D9 lane.

**Architecture:** `d3d9-rt` gains a texture store (a core `Texture` per D3D9 texture keyed by its never-reused
`TextureId`, created at `CreateTexture` as mtld3d creates DEFAULT/MANAGED ones, or at first bind for one it lacks), an
upload path (a level's staging bytes into the core's staging ring at record time, a blit in API order; consecutive
uploads share one blit pass, closed before any render pass), a sampler cache and an opaque-black 1x1 2D fallback. At
draw time a pure mtld3d-core function walks the sampled pixel slots (mtld3d's own rule, shared by both runtimes) and
yields per slot the texture, view kind, sRGB, sampler snapshot and the variant masks and LOD-table bytes; the front end
flushes those textures' dirty levels to the core and the draw binds views and samplers. The CPU staging stays the source
of truth under core (never dropped, never read back).

**Tech Stack:** Rust (src/d3d9/rt `d3d9-rt`, src/d3d9/src front end, src/d3d9/core mtld3d-core, src/dxmt/fakes
`dxmt-fakes`; dxmt-core used as is), C++/sh (MacNeutron's d3d9_triangle, d3d9-check, VM scripts).

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4). Outline: `docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md`
(C4b: "bytes into the core's staging ring at record time, blits in API order"). Previous part:
`docs/superpowers/plans/2026-11-12-dxmtrs-c4b3a-stateblock.md`. Maps: `build/c4b3/map-{frontend,core,ff-samplers}.md`.
Reviews: `build/c4b3/review-c4b3b-{1-design,2-core,3-exec}.md` (folded; record at the end).

**Bases:** DXMTRS `main` ae1c21e9 → `c4b3b` (`build/c4b3b/dx`); MacNeutron `main` b6d1fc5 → `c4b3b-mn`
(`build/c4b3b/mn`). Ledgers: the program ledger and `.superpowers/sdd/<this plan>/progress.md`.

## Scope

In:
- Pixel-stage textures (cascade stages 0-7, ps_1_x-3_0 samplers s0-s15): 2D, cube and volume, every pool except
  render-target textures; every format with a `FormatMapping` (BC1-3 and ATI1/2 where the device has BC; R8G8B8 and
  packed 16-bit widened on the CPU by `pixel_convert::convert_region`); mip chains, `SetLOD`, `D3DSAMP_MAXMIPLEVEL`,
  `D3DSAMP_MIPMAPLODBIAS`, explicit-LOD rows (`texldl`), every address/filter/anisotropy state, border presets,
  `D3DSAMP_SRGBTEXTURE`, format swizzles (L8, A8, A8L8, ...).
- The fixed-function cascade in full (textures on any active stage, stage constants, TFACTOR, BUMPENVMAP/
  BUMPENVMAPLUMINANCE) and programmable `texbem`/`texbeml` (the `BumpEnv` refusal goes).
- Texture CPU paths on a core device: LockRect/UnlockRect (incl. DYNAMIC + DISCARD), LockBox/UnlockBox,
  AddDirtyRect/AddDirtyBox, a level surface's LockRect/UnlockRect, GenerateMipSubLevels, `D3DUSAGE_AUTOGENMIPMAP` (core
  blit `GENERATE_MIPMAPS`, eligibility by mtld3d's `uses_noautogen_fallback`), `UpdateTexture` (CPU copy of the source's
  dirty regions with mtld3d's level offset, AUTOGEN destinations copying level 0 and regenerating).
- Unbound sampled slots read opaque black (a 1x1 (0,0,0,1) 2D: the emitter types an unbound slot 2D).
- `GetRenderTargetData` into a SYSTEMMEM texture level calls `note_system_memory_written` (C4b-3a's carried minor).

Out (owners): vertex textures, the FF VS for transformed layouts, lights (C4b-3c); render targets, render-target
textures, depth textures, fetch4, MRT, depth, scissor (C4b-3d); UpdateSurface, StretchRect, ColorFill, YUV, buffer
locks, non-UP draws, dropping staging with a core read-back (C4c); async compile, prewarm, per-draw dedup, rename-on-
relock to save passes (C4d). A draw reaching an Out item answers D3DERR_NOTAVAILABLE with its own `CoreRefusal`:
`VertexTexture` (renamed from `Sampler`), `DepthTexture`, `Fetch4` (on the active fetch4 mask), `RenderTargetTexture`.

## Global Constraints

- Default runtime unchanged (every `src/d3d9/src` edit behind `#[cfg(feature = "core-runtime")]` + `is_core()`, or
  runtime-agnostic; the shared mtld3d-core functions keep mtld3d's behaviour, proven by its unit tests and Job A).
- No mtld3d crossing on a core device. Cut under core: both creation-time staging drops (`texture.rs` ~3591-3605: DEFAULT
  non-dynamic and render-target textures), `schedule_upload*`/`flush_dirty_mips*` op pushes (they only mark dirty),
  `dropped_staging` after upload, `read_released_level_back`/`read_level_from_gpu` (unreachable with staging kept; debug
  assert, release refusal), `promote_cpu_only_texture`'s warmup, `publish_render_target_write`, `GenerateMipmapsOp`,
  `was_uploaded` (never set under core; `EvictManagedResources` stays refused), and `build_ps_stage_constants`' arena
  (a slice-filling variant; the arena belongs to mtld3d's frame).
- dxmt-core unchanged. `dxmt-fakes` (dev-only) gains texture, sampler and blit capture by decoding records in its own
  `encode` (no `test_support.rs` edit).
- Translator key unchanged (no airconv edit; the variant already carries the masks and `LOD_BIAS`). Bump-env's slot 12
  gets a named `PS_BUMP_ENV_SLOT` in mtld3d's `mtl.rs` (mtld3d binds a bare 12 today).
- Standing rules as C4b-3a's plan. DXMTRS: CONVENTIONS.md, rust-check; MacNeutron: privacy scan.

## Review Focus

1. A texture locked and drawn, relocked (partial dirty rect) and drawn in the same frame: the first draw samples the old
   texels, the second the new (blit WRITE after the first pass's READ: the fence order test in Task 1, `tex-twodraws`).
2. Small mips (rows under 16 bytes) and BC mips smaller than a block: extents clamp to the mip edge, origins block-aligned.
3. A 32-bit process binding more texture bytes in one frame than its address space affords (ring blocks retire by
   sequence): the staged-bytes budget commits the chunk early.
4. A MANAGED texture created on one core device and bound on a new one after Release+CreateDevice: the new store has no
   entry and uploads every level.
5. A texture released while bound: its binding keeps the object; the store entry goes at finalize; encoded closures hold
   their own `Arc`.

---

### Task 1: the core texture store and its uploads (`d3d9-rt`, `dxmt-fakes`)

**Files:** Create `src/d3d9/rt/src/device/textures.rs` (+ `textures/tests.rs`); modify `src/d3d9/rt/src/device.rs`,
`src/d3d9/rt/src/device/recorder.rs`, `src/dxmt/fakes/src/lib.rs`.

**Interfaces:** on `CoreDevice`:
- `pub fn texture_create(&mut self, id: TextureId, desc: &CoreTextureDesc)` (`CoreTextureDesc { pixel_format, kind:
  Tex2d|Cube|Volume, width, height, depth, levels, view_twins: bool, autogen: bool }`; usage SHADER_READ, plus
  PIXEL_FORMAT_VIEW when the format has an sRGB twin or a swizzle (decided from the format); `GPU_PRIVATE |
  SHADER_READONLY`); `pub fn has_texture(&self, id) -> bool`.
- `pub fn texture_upload(&mut self, id, level, face_or_slice, region: UploadRegion, bytes: &[u8], bytes_per_row,
  bytes_per_image)` — origin block-aligned, size clamped to the mip edge (a 2x2 BC1 mip: size 2x2, bpr 8); volume: slice
  0, `origin.z`/`size.depth`; copies into a staging-ring block (an oversize request gets its own block, `ring.rs:144`);
  opens or reuses a blit pass (`blit_open`), emits `access_texture::<COMPUTE>(WRITE)` + `CopyFromBufferToTexture`.
- `pub fn texture_generate_mips(&mut self, id)`, `pub fn texture_view(&self, id, kind, srgb, swizzle) -> Option<(Arc<Texture>, TextureViewKey)>`, `pub fn forget_texture(&mut self, id)`.
- `open_render_pass` (and every pass opener) closes an open blit pass first.
- Staged-bytes budget: bytes staged in the open chunk past 32 MiB (i686 profile) or 256 MiB (64-bit) commit the chunk
  (the call Present makes to end a chunk, without presenting).

- [ ] Step 1: fakes capture `newTexture` (type, format, size, levels, usage), `newSamplerState` (filters, address,
  aniso, lod clamps, border) and decode blit records (copy: buffer offset, bytes_per_row, bytes_per_image, origin, size,
  level, slice; generate-mipmaps) in `encode`.
- [ ] Step 2: tests on `core_on_fakes`: `create_once_upload_twice` (one newTexture; two copies in one blit pass),
  `blit_closes_before_render` (draw, upload, draw: render pass, blit pass, render pass in record order, the blit's texture
  WRITE after the first pass's READ), `render_blit_render_merges_when_unsampled` (draw X, upload T, draw X with T unbound:
  one render pass after the optimizer, as map2-passes §3), `bc_small_mip_extents`, `volume_slice_extents`,
  `oversize_upload_i686` (`queue_profile_for(32)`: a 2 MiB upload is one allocation), `budget_commits_chunk`,
  `forget_keeps_encoded`. RED (no store).
- [ ] Step 3: implement. Step 4: `cargo test -p d3d9-rt` green; commit `d3d9-rt: a core texture store and record-time uploads`.

### Task 2: samplers and the black fallback (`d3d9-rt`)

**Files:** Create `src/d3d9/rt/src/device/samplers.rs` (+ tests).

**Interfaces:** `pub fn sampler(&mut self, key: SamplerKey, desc: &SamplerDescription) -> Arc<Sampler>` (cached;
`WMTSamplerInfo` from the description 1:1: s/t/r = U/V/W, Metal-valued enums, `max_anisotropy`, `lod_min/max_clamp`,
border preset, `normalized_coords = 1`, `support_argument_buffers = 0`, `Sampler::create(&mtl, &info, 0.0)`);
`pub fn black(&mut self) -> (Arc<Texture>, TextureViewKey)` (1x1 BGRA8 2D holding (0,0,0,1), uploaded once).

- [ ] Steps: tests `sampler_cached_by_key`, `sampler_info_fields` (CLAMP/POINT/MIPLINEAR, aniso 8, MAXMIPLEVEL 2 → the
  fake's captured fields), `black_is_opaque_once`; RED; implement; GREEN; commit `d3d9-rt: a sampler cache and an opaque
  black fallback`.

### Task 3: texture CPU paths and the core flush (front end)

**Files:** Modify `src/d3d9/src/texture.rs`, `src/d3d9/src/surface.rs`, `src/d3d9/src/device.rs`
(`create_texture_path` and cube/volume, `device_update_texture`, `promote_cpu_only_texture`),
`src/d3d9/src/device/core_arms.rs` (`get_render_target_data`), `src/d3d9/core/src/ff_state.rs` (a slice-filling
`fill_ps_stage_constants`).

**Interfaces:** `pub(crate) fn core_flush_dirty(tex: &mut Direct3DTexture9, core: &mut CoreDevice)` (creates the store
entry if missing and then uploads every level; else each dirty level/face/slice region, converted by
`pixel_convert::convert_region` when its upload decode is not a plain blit; clears the dirty bits; AUTOGEN →
`texture_generate_mips`).

- [ ] Step 1: under core, CreateTexture/Cube/Volume skip both staging-drop loops and call `texture_create` (DEFAULT and
  MANAGED; SYSTEMMEM/SCRATCH stay CPU-only; render-target usage textures get no store entry).
- [ ] Step 2: delete C4b-3a's refusals of LockRect/UnlockRect/LockBox/UnlockBox/AddDirtyRect/AddDirtyBox/
  GenerateMipSubLevels; let `refused_under_core` pass texture-level surfaces (back buffers and standalone targets stay
  refused); cut the Global Constraints' crossings under core (mark dirty only).
- [ ] Step 3: `UpdateTexture` under core: mtld3d's validation and region logic (source dirty rects, level offset when the
  source has more levels, AUTOGEN destination: level 0 then regenerate), CPU copy between stagings, destination dirty,
  source dirty cleared; no op.
- [ ] Step 4: `finalize_texture` under core calls `forget_texture(id)` while attached; core `GetRenderTargetData` into a
  SYSTEMMEM texture level calls `note_system_memory_written`.
- [ ] Step 5: rust-check green; commit `d3d9: texture locks, dirty rects, UpdateTexture and autogen mips on a core device`.

### Task 4: the shared stage walk, refusals and the draw's binds

**Files:** Create `src/d3d9/core/src/stage_walk.rs` (+ tests); move `PsSamplerDecls` (`mask`, `explicit_lod_mask`, as is)
from `src/d3d9/unix/src/encoder.rs` into mtld3d-core (the Unix side imports it); move mtld3d's LOD-table rule
(`unix/src/draw.rs:476-576`: sampled slots minus fetch4 with an active bias or an explicit-LOD row) into
`stage_walk.rs`, the Unix side calling it; modify `src/d3d9/core/src/pipeline_key.rs` (+ tests),
`src/d3d9/src/device/core_draw.rs`, `src/d3d9/rt/src/device/draw.rs` (`DrawData` gains textures and samplers).

**Interfaces:** `pub fn walk_pixel_stages(inputs: &StageWalkInputs) -> StageWalk` (inputs: per slot the bound
texture's facts (kind, format class: depth / RT / ordinary, sRGB twin, swizzle, LOD), sampler states, fetch4 mask, the
cascade's `sampled_stage_mask` or the PS's `PsSamplerDecls`; output: per sampled slot `Slot { texture: Option<index>,
kind, srgb, sampler: SamplerSnapshot }`, `volume_sampler_mask`, `cube_sampler_mask`, `lod_bias: Option<[u8; LOD_BIAS_BYTES]>`,
and the refusal flags `SAMPLES_DEPTH_TEXTURE`, `FETCH4`, `SAMPLES_RT_TEXTURE`).

- [ ] Step 1: `stage_walk` tests: FF slots are `sampled_stage_mask`'s, PS slots the decl mask; an unbound sampled slot is
  `texture: None`; a cube/volume texture sets its mask bit; `LOD_BIAS` only with a non-zero bias or an explicit-LOD row
  (mtld3d's existing cases ported as tests); a depth-format, RT-usage or fetch4 slot sets its flag; mtld3d's draw path
  gives the same masks and table before and after the move (its unit tests pass unchanged). RED, implement, GREEN.
- [ ] Step 2: `pipeline_key` tests: `PS_SAMPLES`, `FF_PS_TEXTURES`, `ff_ps_constant_rows > 1` and `PS_BUMP_ENV` no longer
  refuse; `SAMPLES_DEPTH_TEXTURE` → `DepthTexture`, `FETCH4` → `Fetch4`, `SAMPLES_RT_TEXTURE` → `RenderTargetTexture`,
  `VS_SAMPLES` → `VertexTexture`; names distinct. RED, implement, GREEN.
- [ ] Step 3: `resolve`: build `StageWalkInputs` from `stage_bindings`/`ff_state`/the PS meta, run the walk; per slot with
  a texture call `core_flush_dirty`, the view (`texture_view` with sRGB and the format's swizzle) and the sampler
  (`key_from_snapshot`/`description_from_snapshot` → `sampler`); an unbound slot binds `black()`; the variant takes the
  masks and `LOD_BIAS` with the table at `PS_LOD_BIAS_SLOT`; the FF PS key takes the bound mask; stage constants
  (`fill_ps_stage_constants`) at slot 15 rows 1-8, bump-env rows at `PS_BUMP_ENV_SLOT`.
- [ ] Step 4: `draw_up`: binds (`bind_texture::<PIXEL>`, `bind_sampler::<PIXEL>(.., cube = false)`, as mtld3d binds the
  plain state), then `resolve_render_pass_barrier(dispatch_tile_barrier)`, then DRAW; re-emitted every draw (C4b2-9).
- [ ] Step 5: rust-check green; a core build's d3d9-check core lane (the existing rows, before Task 5's) green; commit
  `d3d9: core draws sample pixel-stage textures`.

### Task 5: rows and the bench mode

**Files:** Modify MacNeutron's `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/tests/d3d9.sh`,
`vm/dxmt/d3d9-gate.sh`.

- [ ] Step 1: `reset()` also restores textures (all stages and vertex slots NULL), TSS (stage 0 MODULATE/TEXTURE/
  DIFFUSE, others DISABLE), sampler states (defaults) and TFACTOR. FF rows use XYZRHW quads; every sampled quad has
  constant per-vertex texcoords at texel centres unless the row is about addressing; alpha checks route `.a` into a colour
  channel through a ps_2_0 (`mov oC0, t.aaaa` style) or ALPHAOP→COLOROP.
- [ ] Step 2: a `textures` mode (strict under core, 180 s budget, launch timeout 240 s), rows: `tex-ps2d`, `tex-ff`,
  `tex-cascade` (TEXTURE, ADD TFACTOR, MODULATE a stage constant), `tex-cube` (three faces), `tex-volume` (two slices),
  `tex-mips` (MANAGED, MIPFILTER POINT, three quads: SetLOD 1, MAXMIPLEVEL 2, LOD bias +1; level 2 has 2x2 rows of 8 B),
  `tex-address` (CLAMP/WRAP/MIRROR at texel-centre coordinates past 1), `tex-filter` (LINEAR at a texel-boundary
  coordinate: the two texels' mean), `tex-srgb` (one texture drawn with and without SRGBTEXTURE: decoded vs raw),
  `tex-dxt1` (+ a DXT1 chain whose 2x2 and 1x1 mips are sampled), `tex-l8`, `tex-a8`, `tex-a8l8` (alpha routed), `tex-
  r5g6b5`, `tex-a4r4g4b4`, `tex-r8g8b8` (CPU-widened), `tex-lock` (two frames, partial dirty rect), `tex-twodraws` (one
  frame: draw, relock, draw), `tex-dynamic` (DYNAMIC + DISCARD each frame), `tex-lockbox`, `tex-levelsurf`
  (GetSurfaceLevel(1)->LockRect), `tex-genmips` (explicit GenerateMipSubLevels), `tex-update` (UpdateTexture from
  SYSTEMMEM with a dirty rect and a level offset), `tex-autogen` (non-uniform level 0, level 1 = its 2x2 box mean
  within 2), `tex-black` (red on s1, draw, SetTexture(1, NULL), draw again in the same pass: black, alpha 1 routed),
  `tex-bumpenv` (FF BUMPENVMAP), `tex-texbem` (ps_1_1 texbem), `tex-big` (a 1024x1024 A8R8G8B8 texture, 4 MiB, sampled),
  `tex-release` (Release while bound, draw, unbind), and the refusal rows `tex-depthtex` (a D16 texture sampled),
  `tex-fetch4` (fetch4 latched on an L8), `tex-vtx` (vs_3_0 `texldl`), `tex-rttex` (a RENDERTARGET texture sampled):
  each D3D_OK and drawn under mtld3d, D3DERR_NOTAVAILABLE and undrawn under strict.
- [ ] Step 3: `--bench <draws> <frames> tex` (a ps_2_0 sampling one 64x64 texture per draw); the line names `mode tex`
  and counts failed draws.
- [ ] Step 4: d3d9-check: the `textures` launch; `refusals_ok` gains `textures)` = exactly DepthTexture, Fetch4,
  VertexTexture, RenderTargetTexture's names; the `state` set drops FfTexture and `IDirect3DTexture9::LockRect` (only the
  VB Lock stays); `texturedQuad` requires D3D_OK under strict. VM scripts: `tx64-fresh`, `tx32-fresh` launches and the
  same sets; x86 lanes also run `tex-big256` (256 MiB of textures bound in one frame).
- [ ] Step 5: commit (MacNeutron) `dxmt, vm: d3d9_triangle textures rows in every D3D9 lane, and a textured bench`.

### Task 6: gates and probe

- [ ] Job A (plain): build, x64 check, d3d9-check default and msl, conformance default (4 suites), key and imports
  unchanged. Job B (core): d3d9-check core and default; stateblock core = default; a core sweep of conformance visual and
  device (information: crash 0 and no panic required, failures counted, not a parity gate). Job C (VM): D3D9 lanes, both
  WoW64 modes, both runtimes. Probe once on the Mac: `--bench 2000 600 tex` mtld3d vs core, each run proven (core device
  line, zero refusals, zero failed draws), into `build/c4b3b/probe.md`.
- [ ] Mutation REDs: (a) upload only when the store lacks the texture → `tex-lock` FAILs; (b) every sampler from the
  default `SamplerKey` → `tex-address` FAILs; (c) a black fallback of alpha 0 → `tex-black` FAILs.

### Task 7: review and push

- [ ] Final Opus review; one fix pass; privacy scan; push DXMTRS main and MacNeutron (pin, rows, spec §5 paragraph);
  post-push build check.

## Rules check

New: `textures.rs`, `samplers.rs` (state on `CoreDevice`), `stage_walk.rs` (pure), `PsSamplerDecls` and the LOD-table rule
moved Unix → mtld3d-core (moves, the Unix side calls them: no duplicate), `fill_ps_stage_constants` (the arena variant
calls it), `PS_BUMP_ENV_SLOT`, four `CoreRefusal` variants, `DrawFlags` bits, `dxmt-fakes` capture. No config key, wire
field, dependency, derive or lint suppression. Gates: rust-check, d3d9-check, the VM d3d9 gate, conformance.

## Rulings

- **C4b3b-1 (split).** C4b-3 is 3a (stateblock), 3b (this), 3c (the FF VS for transformed layouts, lights, vertex
  textures), 3d (targets, MRT, depth, depth and RT textures, fetch4, scissor). Cost if wrong: one more push.
- **C4b3b-2 (eager creation, as mtld3d).** DEFAULT and MANAGED textures get their core texture at creation; a store with
  no entry (a texture from another device) creates it and uploads every level at first bind. Cost if wrong: GPU memory
  for textures never drawn, as mtld3d.
- **C4b3b-3 (staging is the truth under core).** Staging is never dropped and never read back under core; C4c's
  lockable-pool work brings the drop with a core read-back. 32-bit cost bounded by the chunk budget and gated by
  `tex-big256`. Cost if wrong: address-space pressure in 32-bit titles until C4c.
- **C4b3b-4 (rt-private upload helper).** In `d3d9-rt`, not `dxmt-core`; D3D11's `update_texture` unchanged. Cost: a
  later lift.
- **C4b3b-5 (samplers without argument buffers).** `support_argument_buffers = 0`; bound directly with the plain state
  (`cube = false`), as mtld3d. Cost if wrong: a Metal validation line, then 1 / the cube twin.
- **C4b3b-6 (blits for every mip, CPU widening).** `copyFromBuffer:toTexture:` for every level (D3D11's path), with
  `pixel_convert::convert_region` for the widened formats; mtld3d's render-pass decode serves its own buffer-wrapped
  staging. Cost if wrong: `tex-mips`/`tex-r8g8b8` fail; the GPU decode comes over.
- **C4b3b-7 (one opaque-black 2D fallback).** The emitter types an unbound slot 2D, so one (0,0,0,1) 2D serves every
  unbound slot. Cost if wrong: `tex-black` shows it.
- **C4b3b-8 (rename-on-relock deferred).** A relock between draws sampling the texture splits the pass (correct, slower);
  mtld3d's texture versions that avoid it are C4d's, measured by the probe. Cost: passes in sprite-heavy frames.

## Review record

- Design (`review-c4b3b-1-design.md`): 0 Critical, 7 Important (merge tests; eager creation; 32-bit staging; black value and
  type; the LOD rule; UpdateTexture; task order), 7 Minor; all folded.
- Code reality (`review-c4b3b-2-core.md`): 1 Critical (creation-time staging drop), 7 Important (black; LOD rule; blit pass
  before render pass; extents; `was_uploaded`; stage-constant arena; fakes), Minors; all folded.
- Execution (`review-c4b3b-3-exec.md`): 0 Critical, 13 Important (mutations; fakes vs core; vacuous rows; reset; XYZRHW;
  texbem; textures refusal set; refusal rows; Focus 4-5 rows; CPU-path and format rows; mutation precision; the carried
  minor; the bench mode), Minors; all folded.
