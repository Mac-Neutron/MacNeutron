# DXMTRS C4b-3c: the fixed-function vertex stage and vertex textures on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, a draw with no vertex shader over an untransformed layout (world, view and
projection transforms, lights, material, fog, texgen, texture transforms, vertex blending, point size) draws, and a
vs_3_0 that samples draws with its vertex textures, as under mtld3d.

**Architecture:** the fixed-function vertex key (`FfState::build_vs_key`), its shader (`ff_build::vertex`) and every
constant section filler already live in mtld3d-core and the core draw already uses them for XYZRHW; the default runtime
packs the rows through an encoder mirror the core cannot use, so a pure assembler in mtld3d-core builds the rows per
draw and the core binds them at the float-constant slot. Vertex textures reuse C4b-3b's store, uploads and samplers:
the runtime's texture bind gains a stage, the programmable vertex key gains the sampler kinds, and the vertex LOD table
is bound at its slot. Nothing changes for the default runtime.

**Tech Stack:** Rust 1.99.0 (DXMTRS: mtld3d-core, d3d9-rt, d3d9.dll front end, dxmt-fakes), C++ (`d3d9_triangle`), sh
gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: D3D9 on the core). Map:
`build/c4b3c/map-ffvs.md` (file:line for every claim below).

## Global Constraints

- The default runtime (mtld3d) is unchanged: translator key `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9`
  and the plain d3d9.dll imports unchanged; every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]`
  and a runtime `is_core()` check. Shared mtld3d-core functions keep mtld3d's behaviour (its unit tests pass unchanged).
- `tools/rust-check.sh` passes after every DXMTRS commit (fmt on the pinned nightly, gen, clippy, doc, test, audit).
- No new dependency. No `HashMap` (FxHashMap). Doc blocks: a one-line title, a blank doc line, the body.
- Edits only in the worktrees `build/c4b3c/dx` (branch c4b3c) and `build/c4b3c/mn` (branch c4b3c-mn); builds and GPU
  jobs under the pipeline lock, `$D` and the main checkout restored by each job's trap.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name; rt tests use device handles no other test uses
  (0xe1-0xe9 here).
- Every commit on the branch is safe to run under core: a refusal is lifted only in the commit that makes its draws
  correct (Task 5).
- Benches and probes run once.

## Scope

In: no vertex shader over a layout without XYZRHW (every `FfVsKey` feature: lighting with up to 8 active lights,
material and colour sources, ambient, specular and local viewer, normalisation, vertex fog and range fog, texgen,
texture transforms, sequential and indexed vertex blending, point size and scale, clip planes); vs_3_0 samplers s0-s3
(2D, cube, volume; `texldl` with the LOD table; unbound slots read opaque black).

Out (owners): blending, render targets, MRT, depth and depth/RT textures, fetch4, scissor, MSAA, the creation gate
(C4b-3d); UpdateSurface, StretchRect, buffer locks, non-UP draws (C4c); per-draw dedup of constants and binds (C4d).
Tweening stays unsupported as in mtld3d (single world matrix, warned once).

## Review Focus

1. A lit draw whose enabled lights are sparse and more than eight (0-6, 12 and 20): eight contribute, in index order,
   and the ninth (20) drops (`ff-light8`); lights 3 and 12 alone both contribute (`ff-lightsparse`).
2. A draw that changes only VIEW between two lit draws of one frame: the second draw's lights (eye space) and WV rows
   follow it; nothing is cached across draws. `ff-viewchange`: XYZ|NORMAL, every normal (1,0,0); light 0 directional,
   direction (-1,0,0), diffuse and ambient white; material diffuse (0,0.8,0,1), ambient (0.4,0,0); ortho PROJECTION
   (`_33 = 0.2`); quad x,y in [0.25,0.75], z 2.5; draw 1 with VIEW = I, draw 2 with VIEW = diag(-1,-1,1,1); (192,64)
   and (64,192) are 0x66cc00, (64,64) and (192,192) stay BLUE (stale lights: (64,192) 0x660000; stale WV: BLUE).
3. An untransformed FF draw between two XYZRHW ones in the same pass: each binds its own rows (`ff-rhwmix`).
4. A vs_3_0 sampling an unbound slot, and slots holding a cube and a volume: opaque black, and the kinds reach the
   shader's types (`tex-vtxblack`, `tex-vtxcube`, `tex-vtxvolume`).
5. A render-target and a depth texture bound at a vertex sampler the shader reads: refused by name, not sampled
   (`tex-vtxrt`, `tex-vtxdepth`); the same textures under a bypassed shader (an XYZRHW draw) are not refused.

---

### Task 1: the FF vertex constant rows (mtld3d-core)

**Files:** Modify `src/d3d9/core/src/ff_state.rs`; Test `src/d3d9/core/src/ff_state/tests.rs`,
`src/d3d9/core/src/ff_build/tests.rs`.

**Interfaces:**
- Produces: `impl FfState { #[must_use] pub fn ff_vs_rows(&self, rs: &[u32; RENDER_STATE_COUNT], key: &FfVsKey) ->
  Vec<[f32; 4]> }`: `ff_vs_row_count(key)` rows, zero where no section writes; sections at their fixed base rows (WV
  0, PROJ 4, fog 8, ambient 9, material 10, lights 15, texture transforms 63, palette 95), each written by its
  existing `fill_*_section` over exactly its `*_section_rows` rows; the lights, transform and palette sections are
  skipped when they have no rows (as `src/device.rs:13906-13932` does). Not for a `HAS_RHW` key (debug-asserted; the
  caller binds the viewport row). The fillers take `&mut [MaybeUninit<[f32; 4]>]`: the rows are allocated as
  `MaybeUninit::new([0.0; 4])` and read back with one documented `assume_init` per row (every row is initialised at
  allocation; precedent `ff_state/tests.rs:2287-2294`).

- [ ] Step 1: tests (RED, no such method), in `ff_state/tests.rs`: `ff_vs_rows_len_is_the_row_count` (the keys of
  `make_vs_key`, `lit_vs_key` with 0, 1 and 8 lights, `blend_key` sequential and indexed, a TT stage 4 key);
  `ff_vs_rows_match_hand_values` (WORLD T(1,2,3), VIEW T(10,0,0), PROJ diag(2,3,4,1), a POINT light at (1,2,3)
  enabled at index 12 only, AMBIENT 0x00ff0000, LIGHTING on: rows 0-3 are (1,0,0,11), (0,1,0,2), (0,0,1,3),
  (0,0,0,1); rows 4-7 diag(2,3,4,1); row 9 (1,0,0,0); row 15 (11,2,3,1.0)); `ff_vs_rows_leave_unwritten_rows_zero`
  (lit, one light, no specular, no fog: row 8 zero and row 14 zero; a TT stage-2 key: rows between the material and
  row 63 zero); `ff_vs_rows_follow_the_view` (a point light and a VIEW translation: rows 0-3 and row 15 differ, rows
  4-7 equal). In `ff_build/tests.rs`: `ff_vs_rows_cover_the_rows_the_shader_reads` (states whose every section is
  non-zero: unlit with LINEAR fog; lit with one spot light and specular; lit with 8 lights mixing point, spot and
  directional; a COUNT4 transform on stage 3; a 3-weight sequential blend; an indexed blend. Key from `build_vs_key`;
  for every `r` in `direct_rows(&built(&key).1)` minus `dead_rows(&key)`: `r < rows.len()` and `rows[r] != [0.0; 4]`;
  for the indexed key rows 95..255 are all non-zero).
- [ ] Step 2: run `cargo test -p mtld3d-core ff_vs_rows` (rust-check's env) — Expected: FAIL to compile (no
  `ff_vs_rows`).
- [ ] Step 3: implement.
- [ ] Step 4: run the five tests — Expected: PASS; `sh tools/rust-check.sh` — Expected: `rust-check: ok`.
- [ ] Step 5: commit `mtld3d-core: the fixed-function vertex constant rows, assembled per draw`.

### Task 2: the programmable vertex key carries its sampler kinds (mtld3d-core)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs`, `src/d3d9/core/src/sampler_state.rs`,
`src/d3d9/unix/src/draw.rs` (calls the moved rule), `src/d3d9/src/device/core_draw.rs` and
`src/d3d9/rt/src/device/draw/tests.rs` (the new field, default); Test `src/d3d9/core/src/pipeline_key/tests.rs`,
`src/d3d9/core/src/sampler_state/tests.rs`.

**Interfaces:**
- `VsStageKey::Prog { code, clip_plane_count, sampler_kinds: VsSamplerKinds }`; `pipeline_source` forwards
  `sampler_kinds` into `VsSpec` (the FF arm keeps the default). No refusal changes here: `core_draw_kinds` still
  answers `TransformedFf` and `VertexTexture`, so `resolve` passes `VsSamplerKinds::default()`.
- Produces (sampler_state.rs): `#[must_use] pub const fn vertex_lod_table(explicit_lod: u16, lod_mask: u8) -> bool`
  (`explicit_lod & lod_mask as u16 != 0`): mtld3d's rule at `unix/draw.rs:396-405`, moved; the Unix draw calls it.

- [ ] Step 1: tests (RED): `pipeline_key_tracks_every_input` gains the `sampler_kinds` case (a cube bit and
  `lod_table` each change the key's hash); `prog_vs_spec_carries_the_sampler_kinds` (`pipeline_source` of a Prog key
  with `cube_mask = 0b10` and `lod_table`: the `VsSpec` holds them); `vertex_lod_table_needs_a_sampled_moved_slot`
  ((0b0001, 0b0001) true; (0b0001, 0b0010) false; (0, 0b1111) false).
- [ ] Step 2: run the named tests — Expected: FAIL (compile: no field `sampler_kinds`, no `vertex_lod_table`).
- [ ] Step 3: implement; every `VsStageKey::Prog` construction and match the compiler names gets the field.
- [ ] Step 4: run — Expected: PASS; rust-check ok.
- [ ] Step 5: commit `mtld3d-core: the programmable vertex key carries its sampler kinds; the vertex LOD-table rule
  is shared`.

### Task 3: stage-tagged texture binds (d3d9-rt)

**Files:** Modify `src/d3d9/rt/src/device/textures.rs`, `src/d3d9/rt/src/device/samplers.rs`,
`src/d3d9/rt/src/device/draw.rs`, `src/d3d9/src/device/core_draw.rs` (callers pass `Stage::Pixel`); Test
`src/d3d9/rt/src/device/draw/tests.rs`.

**Interfaces:**
- `TextureBind` gains `pub(super) stage: Stage`; `CoreDevice::texture_bind(&self, stage: Stage, slot: u8, id:
  TextureId, view_format: u32, sampler: Option<Arc<Sampler>>) -> Option<TextureBind>` and `black_bind(&mut self, stage:
  Stage, slot: u8, sampler: Option<Arc<Sampler>>) -> TextureBind`.
- `draw_up` binds each at its stage (`direct::bind_texture::<{ stage::VERTEX }>` / `bind_sampler::<{ stage::VERTEX
  }>` for `Stage::Vertex`); the tile barrier resolve stays once per draw with any texture.

- [ ] Step 1: test (RED) `a_vertex_texture_binds_at_the_vertex_stage` (device 0xe1): a stored 1x1 texture bound with
  `Stage::Vertex` at slot 2 and one with `Stage::Pixel` at slot 0: the calls hold one
  `render {WMT_RENDER_COMMAND_SET_VERTEX_TEXTURE} ` and one `render {WMT_RENDER_COMMAND_SET_VERTEX_SAMPLER_STATE} `
  before the draw, and exactly one fragment texture record.
- [ ] Step 2: run `cargo test -p d3d9-rt a_vertex_texture` — Expected: FAIL (compile: `texture_bind` takes 4
  arguments).
- [ ] Step 3: implement; existing callers and tests pass `Stage::Pixel`.
- [ ] Step 4: run — Expected: PASS; rust-check ok.
- [ ] Step 5: commit `d3d9-rt: a texture bind names its stage`.

### Task 4: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`.

- A new mode `fixed [strict]` (launch `fixed` on the Mac, `fx64-fresh`/`fx32-fresh` in the VM; `budgetEnd` 180 s
  inside a 240 s kill). Its rows, with the exact setups, probes and colours of the appendix (at most 8 probes a row,
  each at least 8 px inside its quad, no blue in any expected colour, texel, level or face, no mirroring transform):
  `ff-xform`, `ff-persp`, `ff-rhwmix`, `ff-viewchange`, `ff-ambient`, `ff-lightdefault`, `ff-lightdir`,
  `ff-lightpoint`, `ff-lightspot`, `ff-lightsparse`, `ff-light8`, `ff-specular`, `ff-matsrc`, `ff-normalize`,
  `ff-fogvertex`, `ff-fogtable`, `ff-texgen`, `ff-tt`, `ff-ttproj`, `ff-tcindex`, `ff-tex`, `ff-blend`,
  `ff-blendidx`, `ff-point`, `ff-pointscale`, `ff-clip`, `ff-indexed`, `ff-decl`, and the refused pair `tex-vtxrt`,
  `tex-vtxdepth` (`refusedRow`: under core the launch's logs hold exactly the render-target and the depth refusal
  lines, one row each; `refusals_ok` gets a `fixed` case).
- `ff-tex` (XYZ|NORMAL|TEX1, directional white, material diffuse (0.8,0.8,0.8,1), RG at uv (0.75,0.5), MODULATE:
  0x00cc00), `ff-tcindex` (XYZ|TEX2, sets (0.25,0.5) and (0.75,0.5), RG: default index 0xff0000, TEXCOORDINDEX(0) = 1
  0x00ff00), `ff-indexed` (DrawIndexedPrimitiveUP, min index 4, XYZ|DIFFUSE, WORLD T(0.25,0,0)), `ff-decl` (a
  declaration: COLOR D3DCOLOR at 0 = 0xffcc0000, POSITION FLOAT3 at 4, NORMAL FLOAT3 at 16 = (0,0,-1); Ldir((0,0,1),
  white), material diffuse (0,0.8,0,1): 0xcc0000), `ff-lightdefault` (LIGHTING TRUE, XYZ|DIFFUSE 0x00ff00, no light,
  zero material: 0x000000).
- `textures` mode: `tex-vtx` becomes a drawn row and `tex-vtxlod`, `tex-vtxblack`, `tex-vtxcube` (samples -Y and +Z),
  `tex-vtxvolume`, `tex-vtxsrgb` join it (appendix); its refusal set drops the vertex-sampler name (four names).
- `reset()` (shared by every mode) also restores: LIGHTING FALSE (the rows' baseline, not D3D9's default: the comment
  says so), WORLD, VIEW, PROJECTION, WORLDMATRIX(1), (5), (9) and TEXTURE0-7 identity, the zero material,
  LightEnable FALSE for 0-8, 12 and 20, AMBIENT 0, COLORVERTEX TRUE, SPECULARENABLE FALSE, LOCALVIEWER TRUE,
  NORMALIZENORMALS FALSE, the four MATERIALSOURCE defaults (COLOR1, COLOR2, MATERIAL, MATERIAL for diffuse, specular,
  ambient, emissive), VERTEXBLEND DISABLE, INDEXEDVERTEXBLENDENABLE FALSE, FOGSTART 0, FOGEND 1, FOGDENSITY 1,
  RANGEFOGENABLE FALSE, FOGVERTEXMODE and FOGTABLEMODE NONE, POINTSIZE 1, POINTSCALEENABLE FALSE, POINTSCALE_A 1, _B
  0, _C 0, TEXCOORDINDEX = stage and TEXTURETRANSFORMFLAGS DISABLE on the 8 stages, and the vertex samplers 257-260
  (no texture, the sampler defaults). D3DRS_CLIPPING stays TRUE.
- A bench mode `--bench <draws> <frames> lit` (XYZ|NORMAL, one directional light; its line says `mode lit`, and one
  frame read back before the timed loop must show the lit colour, else `failed` counts it).

- [ ] Step 1: write the rows; build the test exes (job `jr`: the plain build at the branch base).
- [ ] Step 2: the default and msl lanes (`sh dxmt/d3d9-check.sh`, `D3D9_BACKEND=msl`) — Expected: every `ff-*` and
  `tex-vtx*` row ok under mtld3d. A row mtld3d fails is a row defect or an mtld3d limit: fix the row, or rule it out
  with the limit named. Colours the appendix marks "to confirm" (point coverage, the W fog source, cube and volume
  reads) are confirmed here.
- [ ] Step 3 (the RED of Task 5): the core lane at the branch base (`D3D9_RUNTIME=core`, a `DXMT_D3D9_CORE=1` build
  of Task 3's tip) — Expected: `fixed` FAILs: `ff-xform`, `ff-lightdir` and the other untransformed rows answer
  D3DERR_NOTAVAILABLE under strict; `tex-vtx` FAILs.
- [ ] Step 4: commit `dxmt, vm: d3d9_triangle fixed rows (the FF vertex stage) and vertex texture rows in every D3D9
  lane, and a lit bench (DXMTRS C4b-3c)`.

### Task 5: the front end's core draw; the refusals go

**Files:** Modify `src/d3d9/src/device/core_draw.rs`, `src/d3d9/core/src/pipeline_key.rs` and its tests,
`src/d3d9/src/device.rs` (comments at `set_vertex_texture_slot`, `push_vertex_sampler_row`, and every comment that
names C4b-3c or the removed refusals: `git grep -n "C4b-3c\|TransformedFf\|VertexTexture\|VS_SAMPLES"`).

**Interfaces:** consumes Tasks 1-3.
- `core_draw_kinds`: no vertex shader over a layout without RHW is `StageKind::Ff`; a core vertex shader that samples
  is `StageKind::Prog`. `CoreRefusal::TransformedFf`, `CoreRefusal::VertexTexture` and `DrawFlags::VS_SAMPLES` are
  removed. `SAMPLES_DEPTH_TEXTURE` and `SAMPLES_RT_TEXTURE` cover every sampled slot, pixel or vertex.
- `resolve`:
  - `pos_fixup(&viewport, target, layout.has_rhw())` (it passed `vs_kind == StageKind::Ff`: an untransformed FF draw
    under point scale must clip, not clamp, as mtld3d does at `unix/draw.rs:997-999`).
  - FF vertex stage: `layout.has_rhw()` keeps the viewport row; otherwise the bytes of
    `dev.ff_state().ff_vs_rows(&rs, &key)` (rows × 16, at most 4080, little-endian) at `mtl::VS_FLOAT_CONST_SLOT`.
  - A programmable, unbypassed (`!layout.has_rhw()`) core vertex shader whose meta samples: `declared =
    meta.sampler_mask & 0xF`. The vertex slots are walked in a list of their own (they share numbers with pixel slots
    0-3; the pixel `lod_table` call must never see them). Per declared slot: the state row is
    `dev.vertex_sampler_states[slot]` with `TEXTURE_LOD_SLOT` = the bound texture's `lod()` (0 when none) and
    `sample_unfiltered` where the pixel arm applies it; `VertexLodTable::set_slot`; the texture is flushed
    (`core_flush_dirty`), viewed (the sRGB twin by `D3DSAMP_SRGBTEXTURE`, the swizzle) and bound with
    `texture_bind(Stage::Vertex, ..)`; an unbound slot, or one `core_flush_dirty` refuses, gets
    `black_bind(Stage::Vertex, ..)`. `sampler_kinds = dev.vertex_texture_kinds()` with `lod_table =
    vertex_lod_table(meta.explicit_lod_mask, table.mask())` (mtld3d's kinds, unmasked, as its key); the table's bytes
    at `mtl::VS_LOD_SLOT` when `lod_table`.
  - `SAMPLES_DEPTH_TEXTURE` / `SAMPLES_RT_TEXTURE` are also set when a declared vertex slot of such a shader holds a
    depth-stencil or render-target texture (not for a bypassed shader).

- [ ] Step 1: tests (RED): `core_draw_kinds_cover_every_case` expects `Ok((StageKind::Ff, _))` for the untransformed
  no-VS case and `Prog` for a core VS (the flag is gone); run — Expected: FAIL to compile. The GPU RED is Task 4
  Step 3's.
- [ ] Step 2: implement.
- [ ] Step 3: rust-check ok (its clippy legs compile the PE with and without `core-runtime`).
- [ ] Step 4: job `jq` (core build, `d3d9-check` core) — Expected: every `fixed` and `textures` row ok under core
  strict, the `fixed` launch's two refusal lines exactly, the `textures` launch's four.
- [ ] Step 5: commit `d3d9: core draws run the fixed-function vertex stage and vertex textures`.

### Task 6: gates and probe

- [ ] Job A (plain): build, x64 check, d3d9-check default and msl, conformance default; key and imports unchanged.
  Job B (core): d3d9-check core and default; stateblock core = default. Job C (VM): D3D9 lanes, both WoW64 modes, both
  runtimes (`tx64-fresh` timed before and after: the budgets rise if its margin falls under 40 s). Probe once on the
  Mac: `--bench 2000 600 lit`, mtld3d vs core, each run proven, into `build/c4b3c/probe.md`.
- [ ] Mutation REDs (each in bounds, each naming its red row): (a) the lights section at base 14 → `ff-lightdir`
  left 0x000000; (b) vertex binds sent to the pixel stage → `tex-vtx`; (c) `sampler_kinds` not forwarded (default in
  `pipeline_source`) → `tex-vtxlod` frame 2; (d) the WV and PROJ bases swapped → `ff-xform` (200,80); (e)
  `pos_fixup`'s flag left as `vs_kind == StageKind::Ff` → `ff-pointscale` (192,64).

### Task 7: review and push

- [ ] Final Opus review; one fix pass; privacy scan; push DXMTRS main and MacNeutron (pin, rows, spec §5 paragraph);
  post-push build check.

## Rules check

New: `FfState::ff_vs_rows` (pure, calls the existing fillers: no duplicate of the mirror, which is the default
runtime's delta protocol) with one documented `assume_init` over rows initialised at allocation;
`sampler_state::vertex_lod_table` (moved from the Unix draw, which calls it: no duplicate);
`VsStageKey::Prog.sampler_kinds` (its wire form exists in `VS_SPEC_WIRE`); `TextureBind.stage`; the `fixed` mode and
the `lit` bench. Removed: `CoreRefusal::TransformedFf`, `CoreRefusal::VertexTexture`, `DrawFlags::VS_SAMPLES`. A `Vec`
per FF draw (C4b3c-2). No static, config key, wire field, dependency, derive or lint suppression. Gates: rust-check,
d3d9-check, the VM d3d9 gate, conformance.

## Rulings

- **C4b3c-1 (blending is C4b-3d's).** The split left `Blending` unowned; it is output-merger state with the targets
  (the target's alpha decides DESTALPHA factors, the blend colour is per-pass state). Cost if wrong: blended draws stay
  refused one subproject longer.
- **C4b3c-2 (rows rebuilt per draw).** `ff_vs_rows` allocates and fills up to 255 rows on every FF draw, and the bind
  copies them once more; mtld3d sends deltas. Dedup against the last-bound rows is C4d's; the probe measures it. Cost
  if wrong: core CPU on FF-heavy scenes until C4d.
- **C4b3c-3 (vertex-sampled target textures reuse the pixel refusals).** A render-target or depth texture at a declared
  vertex slot sets the same flags and names as a pixel one; their rows run in the `fixed` launch, whose logs then hold
  exactly those two names. Cost if wrong: a log line that does not say "vertex".
- **C4b3c-4 (no core sweep of Wine's suites).** Core's CreateDevice refuses an auto depth-stencil until C4b-3d, so the
  visual and device suites create no core device (C4b-3b's sweep). Cost if wrong: none measurable before C4b-3d.
- **C4b3c-5 (mtld3d is the rows' reference).** Each new row must pass in the default and msl lanes before it gates
  the core. Cost if wrong: a shared-code defect both runtimes have goes unseen (Wine's conformance default lane covers
  mtld3d).
- **C4b3c-6 (five mutations, not eight).** The per-frame row cache and the palette base are pinned by
  `ff-viewchange` and the unit oracle (`ff_vs_rows_cover_the_rows_the_shader_reads`), the unapplied vertex sampler row
  by `tex-vtx`'s MIRROR quad; they get no mutant build. Cost if wrong: a row believed discriminating that is not.
- **C4b3c-7 (kinds unmasked, as mtld3d).** The core keys the bound vertex kinds of all four slots, not only the
  declared ones, so its rule is mtld3d's. Cost if wrong: a pipeline variant forked by a texture the shader ignores.
- **C4b3c-8 (stated gaps).** No exact row exists for the spot penumbra, specular off the mirror direction, LOCALVIEWER
  TRUE or the material power (pow(1, n) = 1); the unit tests carry them. R32F vertex textures from render targets stay
  refused until C4b-3d. Flat shading and material alpha rows are left out. Cost if wrong: a defect there reaches
  C4b-3d's wider rows.

Plan reviews (2026-11-12, three Opus): design 1 Critical, 3 Important, 6 Minor; core 0, 6, 12; execution 3, 12, 9 —
all Critical and Important folded above (the depth-clamp flag, the task order, the Rules check, `reset()`, the empty
sections, the vertex slot list, the bypassed shader, the re-keyed zero test, the oracle and hand-value tests, the
discriminating row setups, the depth row, the in-bounds mutations, the guest script).

## Appendix: the rows

Shared setup:
- **PO**: an ortho PROJECTION with `_11 = _22 = 1`, `_33 = 0.2`, `_44 = 1` (eye z 0-5 maps to depth 0-1). **PO(s)**
  sets `_11 = _22 = s`.
- **PP**: a perspective PROJECTION with zn 1 and zf 5: `_11 = _22 = 1`, `_33 = 1.25`, `_34 = 1`, `_43 = -1.25`,
  `_44 = 0`.
- **RG**: a 2x1 texture, red then green, with POINT filtering and CLAMP addressing.
- **Light shorthand**: Ldir(d, D, A) is a directional light with direction d, diffuse D and ambient A.
- **Defaults**: WORLD, VIEW and PROJ are I unless stated, and LIGHTING is FALSE unless a light is named.
- **Layouts, unless a row says otherwise.**

  | Layout | Model quad (x, y = [-0.25, 0.25] for every quad) | Probes |
  |---|---|---|
  | One quad | x in [-0.25, 0.25] | (128,128), and a BLUE probe at (16,16) |
  | Two quads, L and R | L x in [-0.875, -0.125], R x in [0.125, 0.875] | (64,128) and (192,128) |
  | Three quads, L, M and R | x in [-0.875, -0.375], [-0.25, 0.25] and [0.375, 0.875] | (48,128), (128,128) and (208,128) |

  - z is 0.5 under the identity PROJ, 2.5 under PO, and as stated under PP.
  - Under PP, scale the model x and y by z, so the quad keeps its pixels.
  - `ff-normalize` R: the model x is [0.1875, 0.4375], which the WORLD scale (2,1,1) maps to [0.375, 0.875].
- **Tolerance**: each probe is held to `matches()`'s ±1 per channel.
  - Most colours are exact integers.
  - Four are not:
    - `ff-ambient` green: 102.4 → 102;
    - `ff-normalize` R: 182.46 → 182;
    - `ff-light8` red: 214.2 → 214;
    - `tex-vtxsrgb`: 55, as `tex-srgb` already passes.

| Row | Setup | Probe: want | Basis | A broken build gives |
|---|---|---|---|---|
| ff-xform | XYZ\|DIFFUSE 0x00cc00; quad x,y in [-0.5,0.5], z 2.5; WORLD T(0.5,0,0), VIEW T(0,0.5,0), PO(0.5); covers px 128-192, py 64-128 | (144,80) 00cc00; (112,80) BLUE; (200,80) BLUE; (144,144) BLUE | derived, vs.rs:280-293 | WORLD ignored: (112,80) green. VIEW ignored or y flipped: (144,144) green. WV and PROJ swapped: (200,80) green |
| ff-persp | as ff-xform; quad x in [-0.5,0.5], y in [0.1,0.9], z 2; PP | (128,96) 00cc00; (80,96) BLUE; (128,40) BLUE | derived; confirm the edge pixels | no divide: both BLUE probes green |
| ff-rhwmix | RHW red px 16-72; XYZ\|DIFFUSE green, x in [-0.375,0], y in [-0.25,0.25], z 0.5; RHW yellow px 144-200; one frame, in that order | (44,128) ff0000; (104,128) 00ff00; (172,128) ffff00; (232,128) BLUE | derived | viewport row bound for the XYZ draw: (104,128) BLUE. WV rows bound for the 3rd draw: (172,128) BLUE |
| ff-viewchange | see Review Focus 2 | see Review Focus 2 | derived | see Review Focus 2 |
| ff-ambient | XYZ only; LIGHTING TRUE, no light; AMBIENT 0x00ff8000; material ambient (0.8,0.8,0.8), emissive (0,0,0.4) | (128,128) cc6666 | derived, vs.rs:571-579 | lighting off: ffffff. Row 9 zero: 000066 |
| ff-lightdir | XYZ\|NORMAL; Ldir((0,0,1),(1,1,1),0); material diffuse (0.8,0,0,1); left normals (0,0,-1), right (0.8660254,0,-0.5) | left cc0000; right 660000 | derived, vs.rs:700-717 | lighting off: ffffff. Lights at base 14: left 000000 |
| ff-lightpoint | XYZ\|NORMAL, PO; POINT light at (-0.6,0,2.0), range 0.9, attenuation (1,0,0), diffuse (0,0.4,0), ambient (0.8,0,0); material diffuse and ambient (1,1,1); normals point at the light per vertex; quad A x in [-0.8,-0.4], y in [-0.2,0.2], z 2.5; quad B x in [0.4,0.8] | (51,128) cc6600; (205,128) 000000 | derived, vs.rs:664-678; N.L within 1e-6 | range ignored: B is cc6600 |
| ff-lightspot | SPOT at (0,0,1.5), dir (0,0,1), theta 0.6, phi 0.8, falloff 1, range 10, attenuation (1,0,0), ambient (0.8,0,0), diffuse (0,0.4,0); quad A x,y in [-0.1,0.1] (rho ≥ 0.99), quad B x in [0.6,0.9] (rho ≤ 0.854), z 2.5, PO | (128,128) cc6600; (224,128) 000000 | derived, vs.rs:679-693 (inside: saturate gives 1, pow(1,1)) | spot treated as a point light: B is cc6600 |
| ff-lightsparse | Ldir at index 3 diffuse (0.8,0,0) and at 12 diffuse (0,0.4,0), both dir (0,0,1); normals (0,0,-1); material diffuse white | (128,128) cc6600 | derived | 12 dropped: cc0000. 3 dropped: 006600 |
| ff-light8 | lights 0-6 diffuse (0.12,0,0), 12 (0,0.4,0), 20 (0,0,0.4), all Ldir (0,0,1) | (128,128) d66600 | derived, ff_state.rs:557-597 | 9 lit: d66666. 20 kept, 12 dropped: d60066 |
| ff-specular | as ff-lightdir with normals (0,0,-1); SPECULARENABLE TRUE, LOCALVIEWER FALSE; light diffuse (0.8,0,0), specular (0,0.4,0); material diffuse and specular white, power 8 | (128,128) cc6600 | derived, vs.rs:585-593, 739-773; ps.rs:114-119 | specular not added: cc0000 |
| ff-matsrc | XYZ\|NORMAL\|DIFFUSE 0x00cc00; Ldir white; material diffuse (0.8,0,0); L: DIFFUSEMATERIALSOURCE MATERIAL; M: COLOR1; R: COLOR1 with COLORVERTEX FALSE | L cc0000; M 00cc00; R cc0000 | derived, vs.rs:547-557; ff_state.rs:2451-2462 | source ignored: M or L wrong. COLORVERTEX ignored: R 00cc00 |
| ff-normalize | Ldir((0,0,1), white); material diffuse (0.8,0,0); L normals (0,0,-0.5), NORMALIZE off; M the same, on; R WORLD scale (2,1,1), normals (0.7071068,0,-0.7071068), on | L 660000; M cc0000; R b60000 (182) | derived, vs.rs:308-315, 481-522 | always on: L cc0000. Never on: M 660000. WV instead of inverse-transpose: R 5b0000 |
| ff-fogvertex | XYZ\|DIFFUSE 0xcc0000, z 3, PO (depth 0.6); FOGENABLE, VERTEXMODE LINEAR, TABLEMODE NONE, start 1, end 5, FOGCOLOR 0x006600 | (128,128) 663300 | derived, vs.rs:903, 922-933; lower/ps.rs:984-987 | fog off or depth as source: cc0000. Row 8 zero: 006600 |
| ff-fogtable | XYZ\|DIFFUSE 0xcc0000, FOGENABLE, FOGVERTEXMODE NONE, TABLEMODE LINEAR, FOGCOLOR 0x006600; (a) PP, quad z 3, start 1, end 5 (W 3); (b) PO, z 2.5, start 0, end 1 (depth 0.5) | 663300 each | (a) confirm W = 1/position.w on the mtld3d lane (lower/ps.rs:990-995); (b) derived | (a) depth source: cc0000. (b) W source: 006600 |
| ff-texgen | XYZ only, PO; VIEW T(0,0,2), model z 1 (eye 3); RG on stage 0; TEXCOORDINDEX(0) CAMERASPACEPOSITION; TTFF COUNT2; TEXTURE0 zero except `_31` 0.25 and `_42` 0.5 | (96,128) 00ff00; (160,128) 00ff00 | derived, vs.rs:797-800, 843-873 | passthru fallback: ff0000. Transform ignored: (96,128) ff0000. Model-space position: ff0000 |
| ff-tt | XYZ\|TEX1 uv (0.25,0.5); RG; TTFF COUNT2; TEXTURE0 = I with `_31` = 0.5 | (128,128) 00ff00 | derived, vs.rs:843-869 | transform ignored, or translation in `_41`: ff0000 |
| ff-ttproj | XYZ\|TEX1\|TEXCOORDSIZE3(0) uvw (0.875,0.5,2); 4x1 texture red, green, white, yellow; TTFF COUNT3\|PROJECTED; TEXTURE0 = I | (128,128) 00ff00 | derived (vs.rs:878) plus the PS divide, core_draw.rs:289 | no divide: ffff00 |
| ff-blend | XYZB1\|DIFFUSE green, weight 0.5; VERTEXBLEND 1WEIGHTS; WORLD(0) = I, WORLD(1) = T(0.5,0,0); quad x in [-0.75,-0.25], y in [0.25,0.75], z 0.5 | (96,64) green; (48,64) BLUE; (144,64) BLUE | derived, vs.rs:394-425 | blend off: (48,64) green. Weight 1: (144,64) green. Palette misplaced: (96,64) BLUE |
| ff-blendidx | XYZB2\|LASTBETA_UBYTE4\|DIFFUSE; beta0 1.0, indices 0x00000905; 1WEIGHTS, INDEXEDVERTEXBLENDENABLE TRUE; WORLD(5) = T(0.5,0,0), WORLD(1) = WORLD(9) = T(0,-1,0); quad as ff-blend | (128,64) green; (48,64) BLUE | derived, vs.rs:343-368 | indices ignored: (48,64) green |
| ff-point | XYZ\|DIFFUSE green; POINTLIST, one point at (0,0.5,0.5); POINTSIZE 16.0f | (124,60) and (132,68) green; (142,64) BLUE | colour derived; coverage to confirm on the mtld3d lane | size ignored: (124,60) BLUE |
| ff-pointscale | POINTSCALEENABLE; A 0, B 0, C 1; POINTSIZE 0.125; point at view (0,0,2), PO, so size 256 x 0.125 / 2 = 16; a 2nd point at (0.5,0.5,6), depth 1.2 | (124,124) and (132,132) green; (142,128) BLUE; (192,64) BLUE | derived, vs.rs:441-475; clipping per Metal | scale ignored: (124,124) BLUE. Depth clamp (Task 5): (192,64) green |
| ff-clip | XYZ\|DIFFUSE green; VIEW T(0.25,0,0); plane 0 (1,0,0,0), CLIPPLANEENABLE 1, CLIPPING TRUE; quad world x in [-0.5,0.5], y in [0.25,0.75], z 0.5 | (192,64) green; (144,64) BLUE; (112,64) BLUE | derived, vs.rs:296-307; vs_draw.rs inverse view | plane in eye space: (144,64) green |
| tex-vtx | vs_3_0 {dcl_position v0, o0; dcl_color o1; dcl_2d s0; texldl r0, c0, s0; mov o0, v0; mov o1, r0}; ps_3_0 {dcl_color v0; mov oC0, v0}; RG MANAGED at 257, point filtering; left c0 = (0.75,0.5,0,0); right c0 = (1.75,0.5,0,0) with ADDRESSU MIRROR | left 00ff00; right ff0000 | derived; the unbound read is Metal's behaviour | binds sent to the pixel stage: left not green. Sampler row unapplied: right 00ff00 (WRAP or CLAMP) |
| tex-vtxlod | 4x4, 3 levels red, green, yellow, at 257; MIPFILTER POINT; texldl (0.5,0.5,0,1); frame 1, then frame 2 with MAXMIPLEVEL 2 | frame 1 00ff00; frame 2 ffff00 | derived, sampler_state.rs `explicit_lod_row`; ops.rs:496-517 | `lod_table` not forwarded: frame 2 00ff00. MIPFILTER left at NONE: level 0 |
| tex-vtxblack | s0 dcl_2d and s1 dcl_cube, both unbound; one shader writes r0, one writes r0.wwww | rgb 000000; alpha ffffff | the core's black bind | black bind skipped: alpha 000000 (Metal's behaviour) |
| tex-vtxcube | cube 2x2, faces +X red, -X green, +Y cyan, -Y yellow, +Z white, -Z magenta, at 258 (s1); directions (0,-1,0) and (0,0,1) | ffff00; ffffff | Metal  | kinds not forwarded: probably face 0, red |
| tex-vtxvolume | 2x2x2, slices red then green, at 259 (s2), ADDRESSW CLAMP; texldl (0.5,0.5,0.75,0) | 00ff00 | Metal | kinds not forwarded: slice 0, red |
| tex-vtxsrgb | 1x1 0x808080 at 257; SRGBTEXTURE FALSE, then TRUE | 808080; 373737 | as tex-srgb | sRGB twin not used: 808080 twice |
| tex-vtxrt, tex-vtxdepth | RT or D24S8 texture at 257, the tex-vtx shaders, `refusedRow` | strict: NOTAVAILABLE and (128,128) BLUE | the row asserts it | not refused: the strict row fails. Wrong name: see C4b3c-3 |

