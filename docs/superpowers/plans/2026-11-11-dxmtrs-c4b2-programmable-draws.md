# DXMTRS C4b-2: programmable draws on D3D9's core runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, SM1-3 vertex and pixel shaders (no samplers) draw through `DrawPrimitiveUP` and
`DrawIndexedPrimitiveUP` on dxmt-core, with their constants, vertex declarations, viewport, culling and clip planes,
their pipelines keyed in the core's `PsoMap` and their stage functions cached per shader variant, as D3D11's are.

**Architecture:** C4b-1's `FfPipelineKey` becomes `PipelineKey` (`mtld3d-core`, host-tested): a stage is fixed-function
(its FF key) or programmable (its tokens), the layout is the bound declaration's elements, and one exhaustive function
`pipeline_source(&PipelineKey)` turns a key into what builds it. `d3d9-rt` keeps that source as the PSO entry's spec,
gains a stage-function cache (D3D11's `ManagedShader` variants + `PipelineCache`), a general `DrawData` (binds by stage
and slot, inline indices from the core's argument-buffer ring, viewport, rasterizer state) and an indexed draw. The COM
front end creates shaders in-process (`parse_shader`, no thunk 19, no op 27), un-refuses the API-state entry points
this slice needs (each called by a gate row) and resolves a draw in `src/d3d9/src/device/core_draw.rs`, its decisions
made by host-tested `mtld3d-core` functions.

**Tech Stack:** Rust (DXMTRS workspace, `MTLD3D_RUST` 1.99.0), winemetal-sys, meson/ARM64X via MacNeutron
`make wine-arm64`; C++ (MacNeutron `dxmt/tests/d3d9_triangle.cpp`); sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4) and §7; the C4 plan
`docs/superpowers/plans/2026-11-11-dxmtrs-c4-d3d9-front-end.md` (Rulings C4-1 … C4-15 bind this plan); C4b-1's plan
`2026-11-11-dxmtrs-c4b1-shared-pipeline-cache.md`. Maps: `build/c4/map2-sync-worklist.md` §2-3 (this is slice S3),
`map2-d3d11-template.md`, `map3-d3d11-cache.md`, `map2-passes.md`. Reviews: `build/c4/review-c4b2-{1-design,2-core,
3-exec}.md` (record at the end).

**Bases:** DXMTRS `main` c9dd980c → `c4b2` (`build/c4/b`); MacNeutron `main` f1144ba → `c4b2-mn` (`build/c4/b-mn`).
"C4b-3" alone is the later part; C4b-1's rulings are written "C4b-1's ruling C4b-n". Ledgers: the C4 ledger and the
program ledger, as C4b-1.

## Scope

In: SM1-3 VS and PS without samplers; `SetVertexShader`/`SetPixelShader` and getters; float, int and bool constants
(`bind_bytes`, D3D11's route); `CreateVertexDeclaration`/`SetVertexDeclaration`; `DrawPrimitiveUP` and
`DrawIndexedPrimitiveUP` with either stage programmable or the C4a fixed-function pair (FF VS only for a pre-transformed
layout, which bypasses a bound VS; FF PS with at most one constant row); triangle fans through `FanRewrite`;
`SetViewport`, cull and fill mode, `SetClipPlane`, `GetDeviceCaps`; sRGB write on draws and Clear.
Out, each to a named part:
- **C4b-3** (S4 and the first stateblock run): textures and samplers, vertex samplers (ops 7, 8), texture stage states,
  the FF VS for transformed layouts (`SetTransform`, lights, material), render targets and MRT (op 12), depth surfaces
  and Z clears, scissor, and the "inert objects plus API state" `stateblock` needs (a VB, an IB, a texture, a swap chain,
  `SetRenderTarget`: `stateblock.c:189, 204, 1722, 1912, 1928`; no lock, draw, clear or present). It must take buffer
  creation (not locking) from C4c for that: the outline's C4b gate lands there.
- **C4c**: vertex/index buffers and non-UP draws (S5), crossing attributes (C4b2-8), blending (C4b-1's ruling C4b-4),
  Wine `visual`, `device`, `d3d9ex` per-site parity (the outline's C4c gate).
- **C4d**: async compile, prewarm, the core's shader cache keyed by the translator key; an MSL fallback or not (C4b2-6).

## Global Constraints

- Default builds unchanged: no D3D10-12 change; D3D9 without `runtime.mode=core` unchanged (every new FE branch is
  `dev.is_core()`; mtld3d's encoder and `src/d3d9/unix` untouched; new `mtld3d_shared::mtl` constants only).
- Translator key 051df2a0… and DXBC/DXIL output unchanged (13 corpora, `build/c3/xlate.sh`); no airconv change.
- `tools/rust-check.sh` (no arguments; all six legs): tests in `<stem>/tests.rs`, never inline; multi-line docs title /
  blank / body at <= 100 columns; derives in `tools/derive_inventory.txt` (`tools/audit.sh --update-derives`); new
  files git-added before the audit leg; a `len` has an `is_empty`.
- Focused runs: `mtld3d-core` as `cargo test -p mtld3d-core --features disk-cache --lib <one filter>`; `d3d9-rt` alone
  as rust-check does: `cargo test -p d3d9-rt --config 'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,
  -undefined,dynamic_lookup"]' <one filter>` (`tools/rust-check.sh:343-344`); dxmt-fakes link no airconv.
- Lock jobs: pipeline lock; the main checkout detached at the committed branch tip, restored by the trap; `$D` back to
  `dxmt.applied`; after detaching and before any gate, `make dxmt-tests build/dxmt-tests-i386/d3d9_triangle.exe`. No
  `make` in a worktree. Edits only in the two worktrees. No game runs; benches once each.
- The core's can't-build line keeps `does not build`. A refused draw logs through `refuse_under_core` with a static
  name per entry point and reason (its once-key is the name's address, `device/runtime.rs:62-72`).
- Nothing is un-refused under core without a gate row that calls it (`src/d3d9/src` has no host tests); its decisions
  live in host-tested `mtld3d-core` functions (`core_draw_kinds`, `float_rows`, `core_ps_variant`, `core_viewport`).
- A Job A/B/C row that fails: fix, `tools/rust-check.sh`, re-run the failing job.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Rulings

- **C4b2-1 (the key) (flag: replaces C4b-1's `FfPipelineKey`).** `PipelineKey { vs: VsStageKey, ps: PsStageKey,
  variant: VariantKey, layout: DeclCode, stride: u32, write_mask: u8 }`, `VsStageKey = Ff(FfVsKey) | Prog { code:
  ShaderCode, clip_plane_count: u8 }`, `PsStageKey = Ff(FfPsKey) | Prog(ShaderCode)`. `ShaderCode { id: ProgramId,
  tokens: Arc<[u32]> }` hashes `id`, compares `Arc::ptr_eq || (id == id && tokens == tokens)`: exact (mtld3d keys on the
  64-bit id alone). It is built once at creation: the device interns tokens (`ProgramId → Weak<[u32]>`, one exact
  compare at creation, as D3D11 interns by SHA-1, `d3d11/src/pipeline.rs:294-312`), so per-draw compares are `ptr_eq`
  and no draw hashes tokens. `DeclCode { hash: u64, with_end: Arc<[D3DVERTEXELEMENT9]> }` likewise (`hash_elements`),
  built at declaration creation; the FVF draw keys on the declaration `SetFVF` built. `VsKey`/`PsKey`
  (`draw_data.rs:763-797`) are not reused: no tokens, not `Clone`, and `VsKey::Programmable` carries `variant`. The
  stride is in the key because D3D9 uses a Metal vertex descriptor (slot 156) where D3D11 pulls vertices in the shader;
  C4c's per-stream strides widen it (or `MTLBufferLayoutStrideDynamic`, a winemetal slot). Cost if wrong: per-draw
  `Arc` clones and the FF keys' hashes, measured by the prog bench row.
- **C4b2-2 (spec and stage functions, D3D11's model).** `PsoMap<PipelineKey, PipelineSource>`: the entry's spec is what
  the pipeline is compiled from (`pipeline_cache.rs:21-28`), so C4d's `Compiled::pending(spec, build)` can rebuild from
  it. Compiled functions are cached per variant (`ManagedShader::compile_variant`, `map3-d3d11-cache.md` §2):
  `StageFunctions { map: HashMap<StageFnKey, Option<Obj>> }`, `StageFnKey { code: ShaderCode, wire: (u32, [u8;
  VS_SPEC_WIRE], [u8; VARIANT_KEY_WIRE]) }` (`StageSpec::to_wire`, `spec.rs:385`). The miss path is a free function
  `build_pipeline(device: &Device, source: &PipelineSource, functions: &mut StageFunctions) -> Option<Obj>`, so C4d can
  move it into a job; C4d also puts `functions` behind a lock with single flight, as D3D11's. A failed build is inserted
  as handle 0 (no re-parse per draw). Synchronous on the API thread (C4b-1's ruling C4b-3). This also lifts C4b-1's
  ruling C4b-4's ceiling (an FF miss on stride or write mask re-translated both stages).
- **C4b2-3 (programs under core).** `CreateVertexShader`/`CreatePixelShader` call `parse_shader`
  (`shader_create.rs:45`) in-process: no `unix_call` (thunk 19), no `AdoptProgramOp` (op 27, `device.rs:15092`); every
  error answers INVALIDCALL (`unix/src/shader_programs.rs:179-197`). What the unix `register_program`
  (`unix/src/encoder.rs:5610-5630`) derives for a draw is computed once by `core_meta(&DxsoProgram) -> CoreShaderMeta {
  samples, bump_env, link_inputs, link_outputs }` (`declared_ps_samplers`, `dxso/emit.rs:766`; airconv's
  `dxso/link.rs`; `uses_bump_env`, `ir.rs:560`); `PsSamplerDecls` stays unix-side (no core user before C4b-3). A shader
  whose `core_meta()` is `None` (not made by a core device) is refused at the draw. airconv's DXSO entry parses an
  application program with the same `parse` (`src/airconv/src/dxso.rs:70`).
- **C4b2-4 (the pixel variant and sRGB) (flag).** mtld3d patches the PS variant on its encoder thread
  (`unix/src/draw.rs:530-612`); under core it feeds the key, so it is one pure function `core_ps_variant(base,
  ps_is_prog, point_list, linked_input_mask) -> VariantKey`: one BGRA8 target (prog `color_out_mask` 0), no depth (prog
  `NO_DEPTH_ATTACHMENT`), no samplers (`LOD_BIAS` clear), no render scale (`VPOS_SCALE` clear), one sample (no
  `SAMPLE_MASK`), `POINT_SPRITE` off point lists, `linked_input_mask` set. `SRGB_WRITE` stays (the core's back buffer
  has no sRGB view, so the shader encodes); the core Clear encodes its colour with `convert::linear_to_srgb_rgba` under
  `D3DRS_SRGBWRITEENABLE`, as mtld3d's no-view branch (`unix/src/encoder.rs:3894-3954`). The `srgb` row pins both.
  Cost if wrong: an sRGB-write pixel off by one LSB against mtld3d's view.
- **C4b2-5 (what a sampling or bump-env shader gets).** A draw whose VS or PS declares a sampler, or whose PS uses a
  bump-env op (`bem` reads slot 12 without sampling), records nothing and answers D3DERR_NOTAVAILABLE, logged once per
  reason. No null-texture binding: the black fallback (`missing_texture_kind`, `draw_data.rs:867`) comes with C4b-3's
  textures. Cost: a sampling draw is missing, not black.
- **C4b2-6 (AIR only) (flag).** The core translates through airconv's AIR path only; mtld3d routes a pair AIR refuses
  to MSL (`libraries.rs:556-583`). Concretely refused by AIR's lowering (`dxso/lower/regs.rs:343, 473` and siblings): a
  pixel shader without `oC0`, relative addressing on a register other than `c#` (ps_3_0 `v[aL]`, vs_3_0 `o[aL]`), a
  colour output the program never writes. Under core such a pipeline does not build: its draws drop with `does not
  build`, which fails the core lanes. C4d decides fallback against coverage.
- **C4b2-7 (validation is shared).** The core arms run after the entry point's shared checks (zero stride, no layout
  source, zero primitive count answers D3D_OK, null data, index format); today's FF arm runs first
  (`device.rs:13250-13258`). Indexed UP takes vertices `[0, min_vertex_index + num_vertices)` (`device.rs:14420-14425`)
  with checked `u32` arithmetic (`up_byte_count`; INVALIDCALL on overflow, i686-safe), not mtld3d's `usize`.
- **C4b2-8 (stream 0, crossing refused).** An attribute that ends past the UP stride needs mtld3d's slot remap
  (`streams::remap_crossing_attributes`, `streams.rs:180-268`; `bind_crossing_inline`, `unix/src/draw.rs:1910-1918`),
  not padding alone. Under core such a draw is refused (NOTAVAILABLE, logged once): `PipelineSource::crosses_stride()`
  (kept attributes' stream-0 extent > stride) is checked on every lookup, before any compile; the port is C4c's with
  the bound-buffer crossings. Elements on streams other than 0 are not attributes of a UP draw: both stages'
  `provided_input_mask` is intersected with the attributes kept on buffer 0 (index < 16), so the inputs they back read
  the translator's zero-fill variant, as mtld3d's unbound stream reads zeros.
- **C4b2-9 (state per draw) (flag).** Every draw re-emits PSO, viewport (`SET_VIEWPORTS` from encoder memory,
  `allocate_cpu`, as D3D11's `pipeline_select.rs:611-640`), rasterizer state (`d3d_to_metal_cull`, `d3d_to_metal_fill`,
  `winding: 0`, `depth_clip_mode: 0`, as `pipeline_select.rs:567-568`) and binds, so C4-8 holds through merges; no
  last-bound dedup, and no D3D11-style clean-state lookup skip (`pipeline_select.rs:211-214`): measured by the prog
  bench, C4d's if it costs. `core_viewport` applies mtld3d's two rules: a zero width or height becomes the target's
  (`passes.rs:3133-3150`), `max_z >= min_z + 0.001` (`device.rs:1103-1106`, the same function there). A zero-length
  bind is never emitted. Relative-addressed constants bind the whole 256-row file in either stage (4 KiB, inline at
  `SET_BYTES_MAX`; airconv clamps a relative index to 0-255, `regs.rs:504-517`). Draws with
  `D3DRS_SCISSORTESTENABLE` set are refused (scissor is C4b-3's).
- **C4b2-10 (WoW64 slots) (flag for C4e).** No change: Preflight refuses 32-bit games on macOS
  (`b-mn/crates/macneutron-core/src/launcher.rs:1150-1166`), so the Mac WoW64 table (`winemetal/unix/src/table.rs:195`,
  D3D9 node refused `g10.rs:610-627`) has no core caller; the VM guest carries 156/157 and the D3D9 node
  (`vm/metal/wm/guest/wow64.c:106`, `sm50-args32.c:80`). The maintainer means to lift that refusal: then the Mac table
  needs slot 156 and the node (ledgered for C4e). The `shd32-fresh` rows are also the first i686 use of the
  argument-buffer ring (indexed and fan draws): a failure there is a core or guest issue.
- **C4b2-11 (the gate) (flag: not the outline's S3 gate).** Per-site Wine `visual` parity under core is impossible in
  C4b-2: the runner cannot select a function (`conformance/src/isolate.rs:3-5`), and `create_core_device` refuses the
  D24S8 auto-depth device `visual.c`'s `create_device` asks for (`direct3d9.rs:2326-2333`). Instead `d3d9_triangle
  shaders`: rows asserting a drawn and an undrawn pixel each, run on the Mac in the default, msl and core lanes, and in
  the VM in the default and core lanes (x64; x86 under old and new WoW64; msl stays Mac-only). Parity is the same rows
  passing in every lane; under core the first frame must match (`strict`: no retry, the core compiles synchronously).
  Conformance under core stays off (exit 2) until C4b-3. Rejected: a Wine-fork test file running the shader functions
  alone (a test patch to rebase; still blocked by the auto-depth refusal).
- **C4b2-12 (probe).** `d3d9_triangle --bench <draws> <frames> prog`: each draw a vs_2_0/ps_2_0 pair over an XYZ
  declaration, four VS float rows set per draw; the bench line names its `mode ff|prog`. C4b-2 does not touch
  presentation, so the VM's +31%/+47% finding (`probe-b1.md`) stays C4c's.

### Entry points un-refused under core (each has a gate row, Task 3)

Refusal lines are within two of those given.

| Entry point (refusal line) | Reaches today | Under core |
|---|---|---|
| GetDeviceCaps (`device.rs:4921`) | `caps::build` (FE) | as is |
| Set/GetViewport (`:11332`, `:11363`) | `set_viewport` → `push_control` → `current_frame` (`:1106`, `:2296`) | push guarded in `set_viewport`; Clear refuses a partial viewport |
| Set/GetClipPlane (`:11548`, `:11578`) | `clip_planes`, dirty bits (FE) | as is |
| DrawIndexedPrimitiveUP (`:14384`) | `generated_fan_source`, `current_frame` | core arm after the checks (C4b2-7) |
| Create/Set/GetVertexDeclaration (`:14631`, `:14726`, `:14781`), GetFVF (`:14887`) | FE | as is; decl keeps a `DeclCode` |
| CreateVertexShader (`:15016`), CreatePixelShader (`:15874`) | `unix_call` thunk 19, `try_push_control` op 27 | `parse_shader` branch (C4b2-3) |
| Set/GetVertexShader (`:15209`, `:15246`), Set/GetPixelShader (`:15957`, `:15995`) | FE | as is |
| SetVertexShaderConstantF (`:15282`), SetPixelShaderConstantF (`:16031`) | `propagate_*_const_delta` → `current_frame` (`:13444`) | propagate only `if !dev.is_core()` |
| Get*ConstantF/I/B, Set*ConstantI/B (`:15422`-`:15620`, `:16091`-`:16283`) | mirrors, dirty bits (FE) | as is |

`set_viewport`'s push is guarded inside it, where every caller routes (Reset, resize, `SetRenderTarget` and state blocks
stay refused). `core_arms::clear` (`:38-53`) refuses a viewport that does not cover the back buffer (C4-13).
`set_clip_plane` reaches no frame; shader and declaration release paths reach no encoder (`vertex_shader.rs:198-202`);
state-block recording cannot happen under core (`BeginStateBlock` stays refused).

## Review Focus

1. A crossing attribute (stride below the stream-0 extent) and elements on streams 1-15 in a UP draw (C4b2-8):
   `a_crossing_attribute_is_detected`, `elements_off_stream_0_are_not_provided` (Task 1), `a_crossing_draw_is_refused`
   (Task 2), the `crossing` row (Task 3).
2. A zero primitive count, a fan of one triangle, 32-bit indices, `min_vertex_index > 0`, an index range past `u32`:
   the `indexed` and `fan` rows and `a_fan_becomes_an_indexed_list`, `indexed_byte_count_is_checked` (Tasks 1-3).
3. A sampling, bump-env or foreign shader, and scissor, under core: `core_meta_flags_*`, `core_draw_kinds_cover_every_
   case` (Task 1); the core lane fails a `shaders` launch whose logs hold any refusal line but the crossing row's.
4. Bind layout: relative constants past `max_const_used`, `i2`, `b5`, neighbour float rows, an empty stage: the `rel`,
   `consti`, `constb`, `vs1` rows, `float_rows_*` (Task 1), `an_empty_bind_is_not_emitted` (Task 2).
5. One VS with many PS, a recreated shader: `a_vertex_shader_compiles_once_across_pixel_shaders`,
   `a_recreated_shader_hits_the_cache` (Task 2), the `prog` bench row.

---

### Task 1: the key, its source and the core's draw decisions (`mtld3d-core`)

**Files:** create `src/d3d9/core/src/pipeline_key.rs` (first commit of Step 1: a stub with `#[cfg(test)] mod tests;`)
and `pipeline_key/tests.rs` (C4b-1's two tests copied; Task 2 deletes the originals with `ff_pipeline.rs`);
`src/d3d9/core/src/lib.rs`; `shader_create.rs` and its tests; `src/d3d9/shared/src/mtl.rs` (`PS_FLOAT_CONST_SLOT = 15`,
`PS_ALPHA_REF_SLOT = 14`, `PS_FOG_SLOT = 13`, airconv's `src/airconv/src/dxso/bindings.rs:54-58`).

**Interfaces (produces), in `mtld3d_core::pipeline_key`:**
- `ShaderCode` (`from_parts(id: ProgramId, tokens: Arc<[u32]>) -> Self`), `DeclCode` (`new(with_end: Arc<[_]>) ->
  Self`, `elements(&self)`), `VsStageKey`, `PsStageKey`, `PipelineKey` (C4b2-1; `Clone + Debug + Eq + Hash`).
- `StageSource { code: ShaderCode, spec: StageSpec }`; `PipelineSource { vs, ps: StageSource, attrs: Vec<(u32, u32,
  u32)>, stream0_extent: u32, stride: u32, write_mask: u8 }` with `fn crosses_stride(&self) -> bool`.
- `fn pipeline_source(key: &PipelineKey) -> Result<PipelineSource, ShaderCreateError>`: exhaustive destructure; a Prog
  VS's semantics re-derived by `parse_shader` on the miss (`ShaderInputSemantic` → `convert::InputSemantic` field by
  field, as `device.rs:15198`), Err when its tokens do not parse; FF stages from `ff_build` (`ShaderCode::from_parts`
  with `ProgramId::from_tokens`, miss only); attrs `resolve_attrs_for_vs` / `resolve_attrs_for_ff(elements,
  &vs.passthrough)`, buffer 0 only; both stages' `provided_input_mask` ∩ kept attrs (C4b2-8); Prog VS spec `VsSpec {
  provided_input_mask, clip_plane_count, sampler_kinds: default }`.
- `fn core_ps_variant(base: VariantKey, ps_is_prog: bool, point_list: bool, linked_input_mask: u8) -> VariantKey`.
- `DrawFacts { vs_bound, vs_core, vs_samples, layout_rhw, ps_bound, ps_core, ps_samples, ps_bump_env, blending,
  scissor: bool, ff_ps_constant_rows: u8 }`, `enum StageKind { Ff, Prog }`, `enum CoreRefusal { NotCoreShader,
  Sampler, BumpEnv, TransformedFf, PsStageConstants, Blending, Scissor }` (`name(self) -> &'static str`, the refusal's
  static name), `fn core_draw_kinds(f: DrawFacts) -> Result<(StageKind, StageKind), CoreRefusal>`.
- `fn float_rows(max_const_used: u32, relative: bool) -> u32` (256 when relative); `fn up_byte_count(stride: u32,
  count: u32) -> Option<usize>` (kept); `fn indexed_up_vertices(min_vertex_index: u32, num_vertices: u32) ->
  Option<u32>` (checked add).
- `fn viewport_depth_range(min_z: f32, max_z: f32) -> (f32, f32)`; `fn core_viewport(v: &D3DVIEWPORT9, target: (u32,
  u32)) -> [f64; 6]` (origin x, y, width, height, znear, zfar).
- In `shader_create`: `CoreShaderMeta { samples: bool, bump_env: bool, link_inputs: LinkInputs, link_outputs:
  SemanticSet }`, `fn core_meta(program: &DxsoProgram) -> CoreShaderMeta`.

- [ ] **Step 1: failing tests** (token arrays with their assembly in comments). `pipeline_key/tests.rs`:
  `pipeline_key_tracks_every_input` (C4b-1's widened: per field and stage arm, keys differing only there give unequal
  sources); `shader_code_equality_is_exact` (two `Arc`s of the same tokens equal, equal hashes; a forged id with other
  tokens unequal); `decl_code_equality_is_exact`; `programmable_attrs_resolve_by_semantic` (decl POSITION0 FLOAT4 @0,
  TEXCOORD1 FLOAT2 @16, COLOR0 D3DCOLOR @24 unconsumed; vs_2_0 `dcl_position v0` `dcl_texcoord1 v3`: `attrs == [(0,
  <Float4>, 0), (3, <Float2>, 16)]`, mask `0b1001`, `stream0_extent == 24`, kept attributes only);
  `elements_off_stream_0_are_not_provided` (prog: TEXCOORD1 on stream 1 → mask `0b0001`; FF: POSITIONT + COLOR0 on
  stream 1 → the FF VS's mask lacks COLOR0's input); `rhw_decl_feeds_the_ff_vs_with_its_passthrough`;
  `a_crossing_attribute_is_detected` (FLOAT2 @20, stride 16: true; the same with stride 28: false);
  `core_ps_variant_rules` (each rule of C4b2-4; FF keeps no `NO_DEPTH_ATTACHMENT`);
  `core_draw_kinds_cover_every_case` (bound VS + XYZ → Prog; bound VS + RHW → Ff; no VS + XYZ → `TransformedFf`; each
  of sampler, bump-env, foreign shader, FF PS with 2 rows, blending, scissor → its refusal);
  `float_rows_bind_the_whole_file_for_relative` (`(12, false) → 12`, `(2, true) → 256`, `(0, false) → 0`);
  `indexed_byte_count_is_checked` (`indexed_up_vertices(u32::MAX, 1)` is None); `core_viewport_rules` (a 0x0 viewport →
  the target; `max_z == min_z` → `min_z + 0.001`); `up_byte_count_refuses_an_overflow`. `shader_create/tests.rs`:
  `core_meta_flags_a_declared_sampler` (ps_2_0 `dcl_2d s0; texld`, ps_1_1 `tex t0`, vs_3_0 `dcl_2d s0`: true; ps_2_0
  `mov oC0, c0`: false); `core_meta_flags_bump_env` (ps_1_4 `bem` without `texld`: `bump_env`, not `samples`);
  `core_meta_links_a_ps_3_0_input` (ps_3_0 `dcl_normal v0`, vs_3_0 `dcl_normal o2`: mask 1).
  Run: `cargo test -p mtld3d-core --features disk-cache --lib pipeline_key`, then `… --lib shader_create`. Expected:
  FAIL (the stub's items unresolved).
- [ ] **Step 2: implement; tests pass.** Mutations, each must fail a test: drop `clip_plane_count` from the Prog VS
  spec; skip the buffer-0 intersection.
- [ ] **Step 3:** `tools/rust-check.sh`. Commit `d3d9: one pipeline key for fixed-function and programmable stages,
  its source and the core's draw decisions, host-tested`.

### Task 2: stage functions, programmable and indexed draws in `d3d9-rt`

**Files:** `src/d3d9/rt/src/device.rs` (`pipelines: PsoMap<PipelineKey, PipelineSource>`, `functions:
StageFunctions`, both dropped after the drain), `device/draw.rs`, `draw/tests.rs`; `src/d3d9/src/device/core_arms.rs`
(temporary: the C4a arm builds its `DeclCode` per draw from `fvf_to_elements(fvf).0` plus END and a `DrawData`, no
behaviour change; Task 4 caches it); `git rm src/d3d9/core/src/ff_pipeline.rs ff_pipeline/tests.rs` and its `lib.rs`
line.

**Interfaces:** consumes Task 1. Produces: `pub enum Stage { Vertex, Pixel }`; `pub struct Bind<'a> { pub stage: Stage,
pub slot: u8, pub bytes: &'a [u8] }`; `pub struct Indices<'a> { pub bytes: &'a [u8], pub index_type: u8, pub count:
u32 }` (0 u16, 1 u32); `pub struct DrawData<'a> { pub primitive: u8, pub vertices: &'a [u8], pub vertex_count: u32,
pub indices: Option<Indices<'a>>, pub viewport: [f64; 6], pub fill_mode: u8, pub cull_mode: u8, pub binds: &'a
[Bind<'a>] }` (replaces `FfDrawData`; the `slot` module keeps `VERTICES`); `DrawRefused::{NoBackBuffer, Pipeline,
Crossing}`; `pub fn draw_up(&mut self, key: &PipelineKey, draw: &DrawData<'_>) -> Result<(), DrawRefused>` (lookup;
on a hit or after `pipeline_source`, `Crossing` if `crosses_stride()` (inserted uncompiled); on a miss
`build_pipeline` (C4b2-2; stage names `d3d9_vs`/`d3d9_ps`; slot 156); a failed or unparsable entry inserted as handle 0
and logged once with `does not build`; the closure copies non-empty binds into one byte vector with `(stage, slot,
range)`s, then emits PSO, viewport, rasterizer state, vertex bytes, binds, and `WmtcmdRenderDraw` or the index bytes
into `allocate_argument_buffer` and `WmtcmdRenderDrawIndexed { base_vertex: 0, instance_count: 1 }`); test-only
`compiles()` (a counter `compile_stage` bumps; the fakes do not log `newFunction`) and `pipelines_len()`.

- [ ] **Step 1: failing tests** (dxmt-fakes: SM50 is faked, so a stage "compiles" unless it does not parse).
  C4b-1's three FF tests ported, names kept. New: `prog_draw_up_records_pipeline_binds_and_draw` (vs_2_0/ps_2_0; binds
  VS 30 64 B, VS 29 256 B, VS 26 4 B, VS 28 20 B, PS 15 16 B; in the cleared pass `render 15`, `render 13 viewports=48B
  viewport_count=1`, `render 12 fill_mode=0 cull_mode=2`, `render 46 length=64 index=30`, `render 46 length=256
  index=29`, `render 46 length=4 index=26`, `render 11 length=16 index=15`, all before `render 19 primitive_type=4
  vertex_start=0 vertex_count=4`); `an_empty_bind_is_not_emitted` (one empty and one 16 B bind: no `length=0` record);
  `indexed_draw_up_draws_from_the_ring` (six u16 indices: `render 20 primitive_type=3 index_type=0 index_count=6 …
  base_vertex=0 base_instance=0`, the vertex bytes at index 0 before it); `a_fan_becomes_an_indexed_list`;
  `a_vertex_shader_compiles_once_across_pixel_shaders` (one VS, two PS: 2 pipelines, `compiles() == 3`; the first pair
  again: still 3); `a_recreated_shader_hits_the_cache` (the same tokens in a new `Arc`: compiles +0, no pipeline);
  `a_crossing_draw_is_refused` (FLOAT2 @20, stride 16: `Err(Crossing)` twice, `compiles() == 0`, no pipeline, no draw
  record); `a_vertex_shader_that_does_not_parse_is_cached_as_failed` (Prog VS `0xFFFE0200` alone: `Err(Pipeline)`
  twice, `pipelines_len() == 1`, `compiles() == 0`, no `render 19`/`render 20`).
  Run: rust-check's `d3d9-rt` line with `draw::`. Expected: FAIL (`DrawData`, `draw_up(&PipelineKey, …)` unresolved).
- [ ] **Step 2: implement; tests pass.** Mutations, each must fail a test: compile every miss (no function cache); drop
  the crossing check; emit empty binds.
- [ ] **Step 3:** `tools/rust-check.sh`. Commit `d3d9: core draws of programmable stages, inline indices and fans,
  stage functions cached per variant`.

### Task 3: the shader rows, RED on the base, and the prog bench (MacNeutron)

**Files:** `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/tests/d3d9.sh`, `vm/dxmt/d3d9-gate.sh`,
`vm/tests/d3d9-gate-bench-test.sh`.

- [ ] **Step 1: `d3d9_triangle shaders [strict]`.** `setvbuf(stdout, nullptr, _IONBF, 0)` first in `main`. Same window
  and device, no auto depth. Shaders are DWORD token arrays with their assembly in comments (no D3DX). A `reset()`
  before each row restores every state the rows touch. Each row prints exactly one line `d3d9 <row> ok|FAIL <why>`: a
  failing Create or Set prints `FAIL <call> <hr>` at once; each row asserts, in one frame, a drawn pixel in a colour
  other than the clear and an undrawn pixel that keeps it; it retries frames (mtld3d skips draws while it compiles)
  unless `strict` (the first frame must match); the mode has a 90 s total budget, rows past it print `FAIL budget`.
  Rows (0/255 colours unless named): `caps` (VS and PS 3.0); `vs1` (vs_1_1 + ps_1_1, colour in c4, c0-c3 and c5 set to
  decoys); `vs2` (vs_2_0 + ps_2_0, a `def` and a PS float, neighbours decoys); `vs3` (vs_3_0 + ps_3_0 through NORMAL: the
  linked input); `consti` (`rep i2`, i0/i1/i3 decoys); `constb` (`if b5` in vs_2_0 and ps_3_0, b0-b4 decoys, both b5
  values in one line); `rel` (`mova a0.x` = 200, read `c[a0.x + 1]` = c201, `max_const_used` ≈ 2); `decl` (colour at
  TEXCOORD5); `indexed` (16- and 32-bit, `min_vertex_index` 4, one line); `fan` (prog and FF RHW fans; a zero-primitive
  UP draw answers D3D_OK in a frame where the fan shows); `bypass` (XYZRHW FVF, a VS bound, a ps_2_0); `vsff` (vs_2_0
  `oD0`, no PS; vs_3_0 `dcl_color o3`, no PS); `alpha` (ps_2_0 alpha 0.25, ALPHATEST GREATER ref 0x80: undrawn; ref
  0x20: drawn); `fog` (vs_2_0 `oFog` 0, linear vertex fog: the fog colour); `mask` (COLORWRITEENABLE red only);
  `wire` (WIREFRAME quad: edge drawn, interior clear); `cull` (CULL_NONE until visible, then the default CCW cull: gone,
  the CW quad beside it drawn); `viewport` (prog VS, left-half viewport: left drawn, right clear; full viewport restored
  before each Clear); `clip` (only plane 2 enabled, x > 0; planes 0, 1 decoys: right drawn, left clear); `srgb`
  (SRGBWRITEENABLE: Clear to linear 0.5 grey and a ps_2_0 draw of linear 0.25: 188 ±1 and 137 ±1); `crossing` (decl
  FLOAT2 @20, stride 16, FLOAT2 feeding the colour: ok if D3D_OK and drawn, or D3DERR_NOTAVAILABLE and undrawn);
  `getters` (Get* of each un-refused setter, GetFVF included). With `--bench <draws> <frames> prog` (argc > 4):
  C4b2-12's draw; the bench line becomes `d3d9 bench: mode ff|prog frames …`.
- [ ] **Step 2: Mac lanes.** `d3d9-check.sh`: `launch` takes an optional timeout (300 s for `shaders`); `launch ours
  fresh shaders shaders` (`shaders strict` under core) and `check shaders "<the 22 rows>"` in every lane; under core
  the launch's logs hold exactly one refusal line, the crossing row's, and no `does not build`; in the mtld3d lanes no
  refusal line.
- [ ] **Step 3: VM.** `vm/tests/d3d9.sh`: `tri <exe> <name> [timeout] [args...]`; right after `tri32-fresh`, `shd64-fresh`
  and `shd32-fresh` (300 s, `shaders`, `strict` under core); `bench <exe> <row> <frames> [mode]`, rows `benchp-<64|32>-
  <0|600>` under the bench file (x64 only under old WoW64, C4b-1's ruling C4b-5). `d3d9-gate.sh`: the 22 rows of both
  files in every lane; `runtime()` also fails on a refusal line (bar the crossing row's in `shd*` under core) and on
  `does not build`, for `tri*`, `shd*` and `benchp-*`; each bench row's line has its `mode`.
- [ ] **Step 4: script checks.** `sh -n`; `d3d9-gate-bench-test.sh` (scratch `BUILD_DIR`) extended: 8 failing bench rows
  on traces without them; a good `shd64-fresh`/`shd32-fresh` passes, one missing a row fails; under `D3D9_RUNTIME=core`
  logs without the device line, or with a refusal line, fail; a bench line of the wrong mode fails. Commit `dxmt, vm:
  d3d9_triangle's shader rows in every D3D9 lane, and a programmable bench`.
- [ ] **Step 5: RED and the reference (one lock job on the Step 4 commit).** A `DXMT_D3D9_CORE=1` build of the bases
  (c9dd980c; MacNeutron at the Step 4 commit), the exes made after detaching: `d3d9-check` default lane: the 22 rows ok
  on mtld3d (the reference for the hand-written expectations); `D3D9_RUNTIME=core`: exactly 22 `d3d9 <row> FAIL`
  lines (a "no line" row is a broken run, not RED), the triangle rows pass. Expected: default `d3d9-check: ok`; core
  `d3d9-check: FAIL` naming `shaders-<the 22 rows>` and `shaders-runtime`. A row mtld3d fails is a wrong expectation
  (fixed in a follow-up commit, re-run) or an mtld3d bug (ledgered, dropped from the parity set, named in the final
  message). Keep this build's ARM64X `d3d9.dll` and `mtld3d.dll` in `$HOME/Documents/MacProton/build/c4/red-b2/`.

### Task 4: the front end on the core (`src/d3d9/src`)

**Files:** `src/d3d9/src/device.rs` (the table's refusals; both const-F setters; `set_viewport`; the two UP entry
points; the mod list; a device field `programs: HashMap<ProgramId, Weak<[u32]>>`), create `device/core_draw.rs` (the
UP arms; `core_arms.rs` loses `draw_primitive_up`, its Clear gains the viewport refusal and the sRGB encode),
`vertex_shader.rs`, `pixel_shader.rs` (`code(&self) -> &ShaderCode`, stored at creation; `core_meta(&self) ->
Option<&CoreShaderMeta>`), `vertex_decl.rs` (`elements_with_end: Arc<[D3DVERTEXELEMENT9]>`, `code(&self) -> &DeclCode`
built at creation, `SetFVF`'s included), `shader_bindings.rs` (`vs_constant_bytes(&self, rows: u32) -> &[u8]`,
`ps_constant_bytes`).

**Interfaces:** consumes Tasks 1-2; proven by Task 3's rows. Produces `core_draw::draw_primitive_up(this, primitive,
primitive_count, data, stride) -> Option<i32>`, `core_draw::draw_indexed_primitive_up(this, primitive,
min_vertex_index, num_vertices, primitive_count, index_data, index_format, vertex_data, stride) -> Option<i32>` (`None`
for an mtld3d device), `fn resolve(dev: &mut DeviceInner, primitive: u32) -> Result<(PipelineKey, Binds), i32>`
(`Binds` owns the bytes the `Bind`s borrow), `fn create_core_shader(dev: &mut DeviceInner, bytecode: &[u32], stage:
ShaderStage) -> Result<(ParsedShader, CoreShaderMeta, ShaderCode), i32>` (interned tokens).

- [ ] **Step 1: creation and guards.** Delete the table's refusals; under `dev.is_core()` the two Create calls use
  `create_core_shader` and build the COM object from `ParsedShader` (the unix reply's metadata); `propagate_*_const_
  delta` only `if !dev.is_core()`; `set_viewport` pushes only `if !self.is_core()`, through `viewport_depth_range`;
  the Clear refuses a partial viewport and encodes under SRGBWRITEENABLE (C4b2-4).
- [ ] **Step 2: `resolve`.** Facts into `core_draw_kinds` (a refusal answers NOTAVAILABLE via `refuse_under_core(
  refusal.name())`). Layout: the bound declaration's `code()`. VS `Prog(code, clip_plane_count(rs))` or `Ff(build_vs_key
  (&rs, layout, 0, dev.bound_decl_passthrough()))`; PS `Prog(code)` or `Ff(build_ps_key(&rs, 0))`. Variant:
  `core_ps_variant(variant_key(&rs, has_rhw, uses_automatic_fog) + tt_projected_mask, …, linked)` with `linked` the
  Prog PS's `link_inputs.mask_against` of the Prog VS's `link_outputs`, or of `SemanticSet::passthrough_outputs(&ff_vs_
  key.passthrough)` (`unix/src/encoder.rs:5638-5655`). Binds (`mtld3d_shared::mtl` slots; none for a zero-row stage):
  VS float `float_rows` rows; int 29 (`vs_constants_i_bytes`, 256 B) and bool 26 (`vs_constants_b_bits`, 4 B) only for
  a VS using them; pos_fixup 28 (`[1/w, -1/h, 0, 1, 0]`; C4a's for RHW); `VsDraw` 27; PS float 15 (`float_rows`; FF:
  `build_ps_constants`), int 11 / bool 10 likewise, alpha ref 14 and fog 13 when non-empty. Viewport `core_viewport(
  dev.viewport(), back buffer)`; cull and fill from render states.
- [ ] **Step 3: the arms.** `device_draw_primitive_up`: the core call below the zero-count return (`device.rs:13272-
  13274`); `device_draw_indexed_primitive_up`: the core call replaces the refusal, after the `index_format` match
  (`:14408-14418`), vertex bytes from `indexed_up_vertices` + `up_byte_count`. Fans through `convert::FanRewrite`
  (`sequential`/`indexed`, base 0) into `Indices`. `Crossing` answers NOTAVAILABLE (refusal logged); the other
  `DrawRefused`s D3D_OK (C4a's rule).
- [ ] **Step 4:** clippy on arm64ec/x86_64/i686 with and without `core-runtime`; `tools/rust-check.sh`. GREEN for Task
  3's rows is Task 5's Job B. Commit `d3d9: programmable shaders, constants, declarations, viewport, clip planes and
  indexed UP draws under runtime.mode=core`.

### Task 5: gates and benches (one build per lock job; each makes both exes after detaching)

- [ ] Job A (plain build): C4b-1's Job A (D3D10-12 checks, presenter, wm-probe, xlate 13 corpora key unchanged,
  conformance default, loopback, facade, licences) plus the plain `d3d9.dll`'s imports and section sizes as `main`'s,
  and `d3d9-check` default and msl with the shader rows.
- [ ] Job B (`DXMT_D3D9_CORE=1`; `cargo build --release` first): `d3d9-check` core lane, every row green; the mutation
  RED without a rebuild (`MTLD3D_PE=$HOME/Documents/MacProton/build/c4/red-b2`: 22 shader FAIL lines, triangle rows
  pass); Mac bench mtld3d vs core, `ff` and `prog`, 0 and 600 frames, each with `debug.presentOccluded=true`, its
  runtime proven, no refusal or `does not build` line, its `mode` checked, into `build/c4/probe-b2.md`. The 10% line
  (CPU(600) − CPU(0) per frame, core vs mtld3d, same lane) on `prog`; `ff`'s core/mtld3d ratio in this job against
  C4b-1's −9.6%.
- [ ] Job C (VM, the core image): C4b-1's Job C rows (D3D10-12 gates, d3d-smoke old/new) plus `d3d9-gate` default and
  core under old and new WoW64 with the shader rows and `D3D9_BENCH=1` (`benchp-*` into `probe-b2.md`, `vm_used_mb`
  for x86).

### Task 6: final review, push

- [ ] Fresh Opus review of both branches; one fix pass; a `d3d9` fix re-runs Job B's core lane.
- [ ] Push DXMTRS `c4b2` → main, MacNeutron `c4b2-mn` → main with the pin and a spec §5 "C4b-2 done" paragraph (the
  flags: C4b2-1, C4b2-4, C4b2-6, C4b2-9, C4b2-10, C4b2-11); ledger notes (below); privacy scan; the pushed pin built
  plain and `dxmt/check.sh` x64 once more. No tag.

**Ledger notes** (written with Task 6): the stride in the key and its C4c widening (C4b2-1); C4b2-10's dependence on
Preflight's 32-bit refusal (C4e); per-draw bind copies held three times on i686 (FE `Binds`, the closure's vector until
the chunk retires, the encoder's `BlockPool` high-water mark): ~4 MB of heap and ~12 MB in flight at latency 3 for
1000 relative draws a frame, a chunk-memory copy (`Chunk::allocate_cpu`) if the probe or C4e shows it; a fan with
`max_vertex == 65535` takes 16-bit indices whose 0xFFFF restarts (parity with mtld3d, `convert.rs:741-745`).

---

## Review record (2026-11-11)

Three Opus reviews (`build/c4/review-c4b2-{1-design,2-core,3-exec}.md`), folded with the coordinator's rulings:
- Design I1 (spec is the source) → C4b2-2, Task 2 (`PsoMap<PipelineKey, PipelineSource>`, `DrawSpec` deleted, the miss
  path a free function). I2 (sRGB Clear) → C4b2-4, Task 4 Step 1, the `srgb` row. I3 (gate holes) → Task 3 rows `rel`
  c201, `consti` i2, `constb` b5, decoy neighbours, `vsff`, `alpha`, `fog`, `mask`, `wire`, `clip` plane 2, drawn +
  undrawn pixel, `strict` under core (C4b2-11). M1 → C4b2-1 (interning, `from_parts`). M2 → `CoreRefusal::BumpEnv`,
  `core_meta_flags_bump_env`. M3 → `viewport_depth_range`/`core_viewport`. M4 → C4b2-2, failed entry handle 0,
  `pipelines_len() == 1`. M5, M6 → ledger notes, C4b2-1 and C4b2-10 flags. M7 → C4b2-6. M8 → `draw_data.rs:867`.
  The `visual` claim strengthened with `direct3d9.rs:2326-2333`.
- Core C1 (crossing) → ruling: refused under core until C4c: C4b2-8, `crosses_stride`, `a_crossing_*` tests, the
  `crossing` row proving the refusal line. I1 → the failing stage is a VS (`0xFFFE0200`); "real airconv" dropped
  (SM50 is faked). I2 → buffer-0 intersection for both stages, the FF case in `elements_off_stream_0_are_not_provided`.
  I3 → `*_constants_b_bits` 4 B, `*_constants_i_bytes` 256 B, rows `i2`/`b5`. M1 → `mtl.rs` slots 15/14/13. M2 →
  `core_viewport`, viewport from encoder memory. M3 → 256 rows for a relative PS. M4 → no zero-length bind. M6 →
  `indexed_up_vertices`, checked. M7 → `NotCoreShader`. M9 → `CoreRefusal::Scissor`. M5, M8, M10 → ledger notes and
  C4b2-10.
- Exec C1 → each row asserts a drawn pixel in the same frame. C2 → fail fast, `setvbuf`, 90 s budget, 300 s watchdog in
  `d3d9-check.sh` and `d3d9.sh`. C3 → RED = 22 explicit FAIL lines; `mode ff|prog` checked; `make` of both exes in every
  lock job. I1 → Step 5 on the Step 4 commit. I2 → no `make` in a worktree. I3 → `compiles()` counter. I4 →
  `an_empty_bind_is_not_emitted`. I5 → one cargo filter per run, the dynamic_lookup config, a stub module first. I6 →
  Task 2 builds `DeclCode` per draw temporarily. I7 → `debug.presentOccluded=true`, refusal greps (Mac and VM), the FF
  ratio in-job. I8 → `d3d9-gate-bench-test.sh` extended. Minors: M1 `reset()`, M2 token arrays, M3 one line per row,
  M4 `shd*` after `tri32-fresh`, M5 `shaders-runtime`, M6 msl Mac-only (C4b2-11 narrowed), M7 imports to Job A and
  rust-check without arguments, M8 copied tests and the wire names, M9 a refusal name per reason, M10 the unconsumed
  element past the kept one, M11 the fix rule, M12 an absolute `MTLD3D_PE`.
