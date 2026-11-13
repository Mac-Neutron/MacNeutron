# DXMTRS C4b-3d1: render targets other than the back buffer on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, `SetRenderTarget(0, ...)` takes a render-target texture's level or cube face and a
`CreateRenderTarget` surface; draws, whole-target clears and `GetRenderTargetData` go to and come from that target, and
a render-target texture is sampled afterwards, as under mtld3d.

**Architecture:** C4b-3d is five slices, each planned, reviewed, gated and pushed on its own: **3d1 the target set**
(this plan), 3d2 depth-stencil with the auto-depth creation gate, depth sampling and fetch4, 3d3 blending, scissor and
partial clears, 3d4 MRT, 3d5 MSAA and the Wine suite sweep. Here `CoreDevice` (d3d9-rt) gains a current colour target
(`TargetRef`: the back buffer, or a level and face of a stored render-target texture) that its render pass, Clear and
read-back resolve; a target change closes the open pass. A render-target texture is a core texture made with the
render-target usage and per-subresource tracking; a standalone render-target surface is one too, under an id of its
own. The pipeline key carries colour 0's pixel format. The front end's core arms read the bound target's size and
format where they read the present parameters. Nothing changes for the default runtime.

**Tech Stack:** Rust 1.99.0 (DXMTRS: mtld3d-core, d3d9-rt, d3d9.dll front end, dxmt-fakes), C++ (`d3d9_triangle`), sh
gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: D3D9 on the core; line "C4b-3d (targets, MRT,
depth, MSAA, blending, fetch4, scissor, the creation gate) follows"). Maps: `build/c4b3d/map.md` (the whole of
C4b-3d) and `build/c4b3d/map-3d1.md` (this slice; file:line and excerpts for every claim below, cited as M1 §n and
M2 §n).

## Global Constraints

- The default runtime (mtld3d) is unchanged: translator key `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9`
  and the plain d3d9.dll imports unchanged; every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]`
  and a runtime `is_core()` check. Shared mtld3d-core functions keep mtld3d's behaviour (its unit tests pass unchanged).
- `tools/rust-check.sh` passes after every DXMTRS commit (fmt on the pinned nightly, gen, clippy, doc, test, audit).
- No new dependency. No `HashMap` (FxHashMap). No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`) and `build/c4b3c/mn` (branch `c4b3d-mn`), cargo target
  `build/c4b3c/cargo`; this slice's gate scripts, logs and reviews in `build/c4b3d/s1`. Builds and GPU jobs under the
  pipeline lock, `$D` and the main checkout restored by each job's trap. No make in a worktree.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name; rt tests use device handles no other test uses
  (0xf1-0xfc here).
- Every commit on the branch is safe to run under core: a refusal is lifted only in the commit that makes its calls
  correct (Task 4).
- dxmt-fakes is read only by d3d9-rt's tests and its own (M2 §10): a new fake line is printed after the existing line
  it belongs to, never between `MTLCommandBuffer_renderCommandEncoder` and `  color0 ...`, and no existing line's
  text changes.
- Benches and probes run once. No gate, build or VM job while the maintainer is gaming.

## Scope

In: `CreateTexture`/`CreateCubeTexture` with `D3DUSAGE_RENDERTARGET` as targets and as sampled textures (pixel and
vertex slots), every level and face, every colour-renderable format of `is_render_target_format_device` (M2 §1.1);
`D3DUSAGE_AUTOGENMIPMAP` render targets (mtld3d's regeneration rule: when slot 0 changes away from one);
`CreateRenderTarget` without multisampling; `SetRenderTarget(0, ...)` with its viewport and scissor reset;
draws (fixed-function and programmable, pre-transformed and not) and whole-target `Clear` on the bound target;
`GetRenderTargetData` from any of these into a system-memory surface under mtld3d's rules (`reject_readback_dst`);
`UpdateTexture` into a render-target texture (through its staging, as any texture's).

Out (owners): a depth-stencil surface, depth state, depth and fetch4 sampling, the auto-depth creation gate (3d2);
blending, scissor, rect and partial-viewport clears, the hardware sRGB attachment (3d3); `SetRenderTarget` at index
1-3 (3d4); multisampled targets (3d5); `StretchRect`, `ColorFill`, `UpdateSurface`, a render-target texture's or
surface's `LockRect`, `CreateRenderTargetEx`, `Reset`, additional swap chains' own back buffers, render scale (C4c);
per-draw dedup (C4d).

## Review Focus

1. A target switch in mid-frame keeps both targets' contents: a quad drawn to the back buffer before
   `SetRenderTarget` to a texture and back is still there, and the texture keeps what was drawn (`rt-switch`).
2. A Clear of a target with no draw after it still happens, and two Clears of one target land in order
   (`rt-clearonly`): the core folds a Clear only into a later pass on the same view (M2 §9.5).
3. A level below 0 and a cube face: the pass, the viewport reset and the read-back use the level's size
   (`max(1, size >> level)`) and the face's slice (`rt-mip`, `rt-cube`).
4. Formats that are not 4 bytes a texel or not BGRA: the pipeline's colour format, the tile-barrier key and the
   read-back row follow the target (`rt-formats`: 4, 8 and 16 bytes; unorm and float).
5. Lifetime: a render-target texture the application released while it is bound as the target or at a stage is
   still drawn to and sampled (`rt-release`); in d3d9-rt, forgetting the current target's texture falls back to
   the back buffer and closes the pass (unit test).

---

### Task 1: the pipeline key carries colour 0's format (mtld3d-core)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs`, `src/d3d9/core/src/pipeline_key/tests.rs`,
`src/d3d9/src/device/core_draw.rs` (the one constructor), `src/d3d9/rt/src/device/draw.rs` and its tests' `key`
helpers (construct the field).

**Interfaces:**
- Produces: `PipelineKey { .., pub color_format: u32 }` and `PipelineSource { .., pub color_format: u32 }`: the
  `WMTPixelFormat` of colour target 0; `pipeline_source` forwards it. The key's doc line "The arm draws only to the
  BGRA8 one-sample back buffer" is rewritten.
- Until Task 4 the front end passes `mtl::PixelFormat::Bgra8Unorm as u32`; Task 2 makes `build_pipeline` read it.

- [ ] **Step 1: failing test.** In `pipeline_key_tracks_every_input` add the case
  `("color_format", ff_base, |k| k.color_format = mtl::PixelFormat::Rgba16Unorm as u32)` (the table becomes
  `[Case; 16]`): `key != base` and `source(&key) != source(&base)`.
- [ ] **Step 2: run** `cargo test --offline --target aarch64-apple-darwin -p mtld3d-core pipeline_key` — Expected:
  does not compile (no field `color_format`).
- [ ] **Step 3: implement** the two fields and the forward; every constructor in the workspace sets BGRA8.
- [ ] **Step 4: run** the same command — Expected: pass; then `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 5: commit** `mtld3d-core: the pipeline key carries colour 0's pixel format`.

### Task 2: the core device's colour target (dxmt-fakes, d3d9-rt)

**Files:** Modify `src/dxmt/fakes/src/lib.rs`, `src/d3d9/rt/src/device.rs`, `src/d3d9/rt/src/device/{recorder,surface,
textures,draw,swapchain}.rs`; Create `src/d3d9/rt/src/device/target.rs` and `target/tests.rs`; Test also
`recorder/tests.rs`, `surface/tests.rs`, `textures/tests.rs`, `draw/tests.rs`.

**Interfaces:**
- Consumes: `PipelineSource::color_format` (Task 1).
- Produces (re-exported from `d3d9_rt::device`):
  ```rust
  /// A colour target: the back buffer, or one level (and cube face) of a stored render-target texture.
  #[derive(Clone, Copy, Debug, PartialEq, Eq)]
  pub enum TargetRef { BackBuffer, Texture { id: TextureId, level: u32, face: Option<u32> } }
  impl CoreDevice {
      /// Makes `target` the current colour target; false, and nothing changed, when it does not resolve.
      pub fn set_target(&mut self, target: TargetRef) -> bool;
      pub fn target(&self) -> TargetRef;
      /// The current target's (width, height) and `WMTPixelFormat`; `None` with no back buffer.
      pub fn target_size(&self) -> Option<(u32, u32)>;
      pub fn target_format(&self) -> Option<u32>;
      pub fn clear(&mut self, rgba: [f32; 4]);                    // the current target, whole
      pub fn read_back(&mut self, source: TargetRef, dst: &mut [u8], dst_pitch: usize) -> bool;
  }
  pub struct CoreTextureDesc { .., pub render_target: bool }
  ```
- Rules the tests pin:
  - A target resolves when it is `BackBuffer` with a back buffer, or names a stored texture made with
    `render_target`, `level < levels`, and `face` is `Some(0..6)` exactly for a cube. Its view key is
    `TextureViewDescriptor { format: the stored pixel format, texture_type: WMT_TEXTURE_TYPE2D, first_mip: level,
    mip_count: 1, first_slice: face or 0, array_size: 1 }` (ruling C4b3d1-3), its size `max(1, size >> level)`,
    the pass's `render_target_array_length` 0 and `tile_barrier_pso_key[0]` its pixel format.
  - `set_target` to a different target closes the open render pass (`close_pass`); to the current one it records
    nothing. `open_render_pass`, `clear` and `draw_up` use the current target; `draw_up` answers
    `DrawRefused::NoBackBuffer` when it does not resolve.
  - `texture_create` with `render_target`: usage `RENDER_TARGET | SHADER_READ` (and `PIXEL_FORMAT_VIEW` with
    `view_twins`), allocation `GPU_PRIVATE` without `SHADER_READONLY` (a tracker per subresource, M2 §7.2), then a
    Clear to (0, 0, 0, 0) of every level and face through its target view (ruling C4b3d1-2). `desc.block.2` is the
    texel's bytes.
  - `forget_texture` of the current target's texture: the target becomes `BackBuffer`, the open pass closes.
  - `read_back(source, ..)`: the tight row is `width * texel bytes` of the source (4 for the back buffer); false
    when `source` does not resolve, `dst_pitch` is under the row or `dst` is short. The read-back staging is
    remade when its length or row differs from the last one's.
  - `build_pipeline` sets `info.colors[0].pixel_format = source.color_format`.
- Fakes: the render-encoder fake prints, after the `color0` line, `  target0 <texture handle> <width>x<height>`
  (the attached view's handle as the fakes name textures and views, the pass's render-target size); the texture-view
  fake prints the view's format, type, first mip and first slice if it does not yet; the pipeline fake prints
  `  pso color0=<pixel_format>` after its line.

- [ ] **Step 1: failing tests** (each on its own fake device handle), in `target/tests.rs` unless named:
  - `a_render_target_texture_has_the_usage_and_starts_cleared`: `texture_create` of a 2-level 2D `render_target`
    desc records a `MTLDevice_newTexture` whose bits hold `WMT_TEXTURE_USAGE_RENDER_TARGET`, and after `commit` two
    render encoders with `load=2` (clear) and clear colour `0,0,0,0`.
  - `a_draw_targets_the_set_texture`: after `set_target(Texture { id, level: 0, face: None })` a `draw_up` opens a
    pass whose `target0` line names a view of that texture at 64x64 (the fixture's back buffer is 64x32), and the
    pipeline's `pso color0=` is the key's format.
  - `set_target_closes_the_open_pass`: draw, `set_target` to a texture, draw: two render encoders, the first ended
    before the second begins; `set_target` to the current target between two draws: one encoder.
  - `a_clear_folds_into_the_targets_pass`: `set_target(texture)`, `clear([1, 0, 0, 1])`, draw, commit: the pass on
    the texture's view has `color0 load=2 ... clear=1,0,0,1`, and no other encoder targets that view after creation.
  - `a_level_and_a_face_have_their_own_views`: level 1 of a 64x64 3-level texture resolves to 32x32 with
    `first_mip 1`; face 3 of a cube to `first_slice 3`, type 2D; a `face` on a 2D texture, `None` on a cube, level
    3 of 3 levels, an id the store lacks and a texture made without `render_target` do not resolve (`set_target`
    false, `target()` unchanged).
  - `forgetting_the_current_target_falls_back_to_the_back_buffer`: `target()` is `BackBuffer` and a pass that was
    open is closed.
  - `pipelines_differ_by_colour_format` (`draw/tests.rs`): two keys differing only in `color_format` build two
    pipelines.
  - `read_back_reads_the_named_target_at_its_texel_size` (`surface/tests.rs`): an RGBA16 16x8 target: the blit's
    `bytes_per_row` is 128 and a `dst_pitch` of 127 is refused; reading `BackBuffer` while a texture is current
    reads the back buffer.
- [ ] **Step 2: run** `cargo test --offline --target aarch64-apple-darwin -p d3d9-rt --config
  'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'` — Expected: does not
  compile (`TargetRef`, `render_target`).
- [ ] **Step 3: implement.** The resolved target is computed at use from `TargetRef` and the store (no cached
  `Arc` to go stale); `back_buffer_view` callers in recorder and surface go through one private
  `resolve(&self, TargetRef) -> Option<Resolved { texture, key, width, height, pixel_format, texel_bytes }>` in
  `target.rs`. `present` still presents the back buffer whatever the target.
- [ ] **Step 4: run** the same command — Expected: every new and existing rt test passes (the existing ones
  unchanged but for the `key` helpers); `cargo test ... -p dxmt-fakes` passes; `sh tools/rust-check.sh` ok.
- [ ] **Step 5: commit** `d3d9-rt: a current colour target (the back buffer or a render-target texture's level or face)`.

### Task 3: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`.

**Interfaces:**
- Produces: a mode `targets` (`d3d9_triangle targets [strict]`, 150 s budget, rows in the appendix), launched in
  every D3D9 lane after `fixed` (`launch ours fresh targets 240 targets $strict`; VM `tg64-fresh`, `tg32-fresh`)
  and checked by name; `targets_refusals` (three names, appendix) checked like the other sets; `tex-rttex` and
  `tex-vtxrt` become drawn rows (their texture is cleared green through `SetRenderTarget` and the draw shows green
  in every lane); `texture_refusals` loses the render-target-sampled name (three left) and `fixed_refusals` keeps
  the depth one alone. `reset()` restores render target 0 to `rtSurf` and the full viewport.
- A helper `until(attempt)`: runs `attempt()` (which sets `why`) each frame with a `Present` after it until it
  passes, for 10 s within the budget, once under `strictMode`: the rows that assert a read-back of a texture use
  it as the others use `frames`.

- [ ] **Step 1: write the rows** and the script lists (appendix), the header comments and counts.
- [ ] **Step 2: syntax** `x86_64-w64-mingw32-clang++ -std=c++17 -fsyntax-only -Wall -Wextra dxmt/tests/d3d9_triangle.cpp`
  with the i686 and aarch64 compilers too — Expected: no diagnostics.
- [ ] **Step 3: rows first, on mtld3d** (job `jr`: plain build of DXMTRS at Task 2's commit, `sh dxmt/d3d9-check.sh`
  default and `D3D9_BACKEND=msl`) — Expected: every `targets` row ok in both lanes, `tex-rttex` and `tex-vtxrt` ok,
  the other launches as before. A row mtld3d fails is ruled on (fixed if the row is wrong; dropped with its limit
  named if mtld3d is), ledgered.
- [ ] **Step 4: RED under core** (job `jq-red`: core build of DXMTRS at Task 2's commit, `D3D9_RUNTIME=core`) —
  Expected: `targets` rows FAIL on `SetRenderTarget`/`CreateRenderTarget` NOTAVAILABLE (all but the three refused
  rows), `tex-rttex` and `tex-vtxrt` FAIL, `textures-runtime` FAIL (the refusal set); nothing else changes.
- [ ] **Step 5: commit** `dxmt, vm: d3d9_triangle targets rows in every D3D9 lane (DXMTRS C4b-3d1)`.

### Task 4: the front end's targets; the refusals go

**Files:** Modify `src/d3d9/src/device.rs` (`device_set_render_target`'s core arm, `device_create_render_target`),
`src/d3d9/src/device/core_arms.rs`, `src/d3d9/src/device/core_draw.rs`, `src/d3d9/src/surface.rs`,
`src/d3d9/src/texture.rs`, `src/d3d9/core/src/pipeline_key.rs` and its tests.

**Interfaces:** consumes Tasks 1-2.
- `Direct3DSurface9::core_target(&self) -> Option<TargetRef>` (core-runtime): `BackBuffer` for a back-buffer
  surface (the implicit one and an additional swap chain's, ruling C4b3d1-4); `Texture { id: parent's id, level:
  mip_level(), face: cube_face() }` for a level of a `D3DUSAGE_RENDERTARGET` texture; `Texture { id, level: 0, face:
  None }` for a standalone surface holding a core id; `None` otherwise.
- `SurfaceInner` holds a standalone render target's core `TextureId`; `finalize_surface` forgets it
  (`core.forget_texture`). `CreateRenderTarget` under core: mtld3d's checks in mtld3d's order (M2 §3 steps 1-4 and
  the format checks of `create_color_target_surface`), a sample count above 1 refused as
  `IDirect3DDevice9::CreateRenderTarget (multisampled)`, then `texture_create(new id, render_target desc)` and the
  surface without Metal handles. A lockable one is made the same (its `LockRect` stays refused with every
  non-system-memory surface's).
- `core_flush_dirty`: a render-target texture gets its core texture (`render_target: true`) with no first upload
  (its `fresh` mask is the dirty mask alone); dirty levels upload as any texture's. Depth formats still answer false.
- `SetRenderTarget` core arm: index above 0 with a surface is refused as `IDirect3DDevice9::SetRenderTarget (index
  above 0)`; at 0: the surface's `core_target()` (none: refused as `IDirect3DDevice9::SetRenderTarget (a surface
  the core does not target)`), the parent texture flushed (`core_flush_dirty`) so its core texture exists,
  `core.set_target`, then mtld3d's bookkeeping in mtld3d's order: `cur_autogen_rt_ids[0]` (an autogen texture the
  slot leaves has `core.texture_generate_mips(old)` recorded, which closes the pass that wrote it),
  `replace_render_target`, the viewport and scissor reset to the surface's size, the dirty marks.
- `core_arms::clear`: `viewport_covers` against `core.target_size()`; the refusal's name becomes
  `IDirect3DDevice9::Clear (a viewport not covering the target)`. `core_arms::get_render_target_data`: source
  `core_target()` of `rt` (none: `IDirect3DDevice9::GetRenderTargetData (a source the core does not read)`),
  a texture source flushed first, `reject_readback_dst` with the source's `GetDesc` size and format (INVALIDCALL
  on a reject, as mtld3d), then `core.read_back(source, ..)`.
- `core_draw`: the target size from `core.target_size()` (both `pos_fixup` and `core_viewport`), the key's
  `color_format` from `core.target_format()`; `CoreRefusal::RenderTargetTexture` and
  `DrawFlags::SAMPLES_RT_TEXTURE` are deleted with their tests' cases (`core_draw_kinds_cover_every_case` loses
  one), and the fold over the vertex slots keeps its depth half.
- A cube render-target texture's `LockRect`/`UnlockRect` is refused as `IDirect3DCubeTexture9::LockRect (a
  render-target texture)` / `::UnlockRect (...)` (it locked staging no target reads, M2 §1.3).

- [ ] **Step 1: unit tests first** (mtld3d-core): `core_draw_kinds_cover_every_case` without the render-target
  case compiles only once the variant is gone; run `cargo test -p mtld3d-core pipeline_key` — Expected: fails to
  compile until Step 2's deletion (the RED is Task 3 Step 4's rows).
- [ ] **Step 2: implement** the arms above.
- [ ] **Step 3: run** `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: a core device renders to render-target textures and surfaces and samples them`.
- [ ] **Step 5: GREEN under core** (job `jq`: core build at this commit, `D3D9_RUNTIME=core`) — Expected: `targets`
  18/18 strict, `textures` 38/38, `fixed` 31/31, `state` and `shaders` as before, each refusal set exact. A cube
  face Metal refuses as a 2D view switches to D3D11's array view (C4b3d1-3's fallback), ledgered.

### Task 5: gates and probe

- [ ] **jb** (core build at the tip): the stateblock conformance under core equals default; `d3d9-check` core and
  default ok.
- [ ] **jm** mutation REDs, one build each, each red on its named row and nothing else new: (a) `open_render_pass`
  resolves `BackBuffer` always → `rt-tex`; (b) `build_pipeline` sets BGRA8 → `rt-formats`; (c) the target view's
  `first_mip` 0 → `rt-mip`; (d) the autogen regeneration removed → `rt-autogen`; (e) `read_back` rows at 4 bytes →
  `rt-formats` (its read-back half).
- [ ] **ja** (plain build at the tip): translator key and imports unchanged; `d3d9-check` default and msl ok.
  Wine's conformance on the default runtime runs at 3d2 and 3d5 (ruling C4b3d1-9).
- [ ] **jc** (VM): `sh vm/dxmt/d3d9-gate.sh` in the four lanes (default and core, old and new WoW64) — Expected: ok.
- [ ] **jp** probe: `--bench 2000 300 tex` on mtld3d and core, Mac x64, once; `build/c4b3d/s1/probe.md` beside
  C4b-3b's +22%.

### Task 6: review and push

- [ ] Whole-branch review by a fresh Opus reviewer (the package, this plan, the spec, Review Focus verbatim, the
  ledger's rulings); one fix pass for Critical and Important findings, each RED then GREEN.
- [ ] Spec §5 paragraph, pin, privacy scan, push DXMTRS `c4b3d` and MacNeutron `c4b3d-mn` to their `main`s
  (fast-forward), post-push check (plain build from main, tests, x64 `dxmt/check.sh`), memory, scratch deleted.

## Rules check

- New `Vec`s per call: `texture_create`'s clears (one closure per subresource, at creation only). No per-draw
  allocation is added: the target resolves to an `Arc` clone and a key, as the back buffer's did.
- The pass description, Clear and read-back share one resolver (`target.rs`), so a new target kind (3d4's slots,
  3d5's multisampled storage) has one place to extend.
- Front-end additions are core arms beside mtld3d's code, not edits to it; the shared checks (`CreateRenderTarget`'s
  validation, `reject_readback_dst`) are called, not copied.

## Rulings

- **C4b3d-1 (five slices).** C4b-3d is planned, reviewed (three adversarial Opus reviews a plan), gated and pushed
  as 3d1-3d5. Cost if wrong: five review rounds; a design choice of an early slice reworked by a later one.
- **C4b3d-2 (blending is 3d's).** The spec's line names blending in C4b-3d; C4b-2's plan had put it in C4c. The spec
  wins. Cost if wrong: none.
- **C4b3d-3 (C4c keeps the surface operations).** `StretchRect`, `ColorFill`, `UpdateSurface`, surface and
  render-target locks and `Reset` stay C4c's, as the C4 outline has them. How a multisampled standalone target is
  observed without `StretchRect` is 3d5's plan's to rule. Cost if wrong: 3d5 pulls a resolve-only `StretchRect`.
- **C4b3d1-1 (sRGB write stays in the shader).** Every target takes the pixel-shader encode and the front end's
  encoded Clear, as the back buffer does; the hardware sRGB attachment is decided in 3d3 by a row with blending.
  Cost if wrong: none without blending (the two encodes differ by rounding only).
- **C4b3d1-2 (new targets start transparent black).** As mtld3d clears a new render-target texture and surface.
  Cost if wrong: a Clear pass per subresource at creation.
- **C4b3d1-3 (a face is a 2D view).** A level or face is a single-subresource `WMT_TEXTURE_TYPE2D` view, array
  length 0, where D3D11 takes a 2D-array view of a cube with array length 1 (layered rendering). Cost if wrong:
  `rt-cube` fails on the first core run and the view becomes D3D11's.
- **C4b3d1-4 (additional swap chains still alias).** Their back buffers are the device's (C4b3a-4) until C4c.
  Cost if wrong: a second window's contents drawn into the first.
- **C4b3d1-5 (a render target keeps its staging pages).** C4b3b-3 stands: never dropped under core, and
  `UpdateTexture` into a render target uploads through them. Cost if wrong: a render target's size in CPU memory
  each, which a 32-bit title pays until C4c's gauge.
- **C4b3d1-6 (the current target sampled).** A pixel-stage read of the pass's target gets the core's tile barrier; a
  vertex-stage read is unordered. D3D9 leaves both undefined and mtld3d handles neither. No row. Cost if wrong: a
  title that relies on it reads stale texels.
- **C4b3d1-7 (no memory accounting).** A core standalone target is not counted in the device's VRAM figure
  (`GetAvailableTextureMem` is refused under core). Cost if wrong: none until C4c.
- **C4b3d1-8 (a fifth launch).** The rows are a mode of their own, not more `textures` rows: their refusal set and
  budget stay apart. Cost if wrong: about 20 s a lane.
- **C4b3d1-9 (the slice's gates).** Rows first on mtld3d (default, msl), RED then GREEN strict under core, the
  stateblock conformance under core, the VM's four lanes, the plain build's key and imports, five mutation REDs, the
  textured probe. Wine's conformance on the default runtime and the Wine suites under core run at 3d2 (the creation
  gate) and 3d5. Cost if wrong: a default-runtime regression from this slice's shared edits found one slice late.

## Appendix: the rows (`d3d9_triangle targets`)

`A` is a 64x64 one-level A8R8G8B8 render-target texture unless a row says otherwise; RED 0xff0000, GREEN 0x00ff00,
BLUE 0x0000ff (the back buffer's clear). "Read" is `GetRenderTargetData` into a system-memory surface of the
source's size and format, asserted at texel coordinates; "shown" is `A` drawn point-filtered by the fixed-function
pixel stage on the back-buffer quad (64,64)-(192,192), asserted at back-buffer coordinates (texel (x,y) shows at
(64+2x, 64+2y)).

| Row | What it does | Asserts |
|---|---|---|
| `rt-tex` | Target `A`: Clear GREEN, a pre-transformed RED quad (8,8)-(24,24). Back to `rtSurf`; shown. | (96,96) RED, (160,160) GREEN, (96,160) GREEN, (160,96) GREEN, (32,32) BLUE |
| `rt-read` | Same contents with the Clear colour 0x8000ff00. Read. | texel (16,16) 0xffff0000, (48,48) 0x8000ff00 (all four bytes), (16,48) green; a 32x32 destination answers `D3DERR_INVALIDCALL` |
| `rt-surface` | `CreateRenderTarget(64,64,X8R8G8B8, none, 0, FALSE)`: `GetDesc`; `SetRenderTarget`; `GetRenderTarget(0)`; Clear RED; an untransformed GREEN quad over clip x,y in [0,1]. Read. | desc 64x64 X8R8G8B8 `D3DUSAGE_RENDERTARGET` `D3DPOOL_DEFAULT`; the same surface back; (48,16) GREEN, (16,48) RED, (16,16) RED |
| `rt-viewport` | `SetRenderTarget(0, A)`: `GetViewport`, `GetScissorRect`; viewport {32,0,32,64}, an untransformed full-clip GREEN quad over a RED Clear made before the viewport change; back to `rtSurf`: `GetViewport`. Read. | {0,0,64,64,0,1} and {0,0,64,64}; (48,32) GREEN, (16,32) RED; {0,0,256,256,0,1} |
| `rt-big` | A 512x512 X8R8G8B8 texture: Clear RED, viewport {256,256,256,256}, an untransformed full-clip GREEN quad. Read. | (500,500) GREEN, (100,100) RED, (300,100) RED, (100,300) RED |
| `rt-mip` | A 64x64 three-level texture: level 0 Clear RED; level 1: `GetViewport`, Clear GREEN, a pre-transformed BLUE quad (0,0)-(16,16); level 2 Clear 0xffff00. Read levels 1 and 0; shown with `D3DSAMP_MAXMIPLEVEL` 1 and point mip filter. | level 1's viewport 32x32; level 1 (8,8) BLUE, (24,24) GREEN; level 0 (32,32) RED; shown (160,160) GREEN |
| `rt-cube` | A 16x16 X8R8G8B8 cube: each face Clear to its own colour (0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff). Read each face; one draw sampling direction (0,0,1) (the `tex-cube` row's draw). | each face's (8,8); the draw shows face 4's colour |
| `rt-formats` | For A8B8G8R8, A16B16G16R16, A16B16G16R16F, R32F, A32B32G32R32F (a format `CheckDeviceFormat` refuses as a render target is skipped and named in the row's line): a 16x16 texture, Clear 0xff4080c0, a pre-transformed white quad over x < 8. Read (skipped where the system-memory surface is not made); shown. | read: x >= 8 holds (0x40,0x80,0xc0,0xff)/255 in the format's layout (unorm16 within 257, float within 0.004; R32F red alone), x < 8 ones; shown: 0x4080c0 (R32F 0x40ffff) right, white left |
| `rt-switch` | On the back buffer a pre-transformed RED quad (32,32)-(64,64); target `A`: Clear GREEN; back; `A` drawn on (160,160)-(224,224). | (48,48) RED, (192,192) GREEN, (128,128) BLUE |
| `rt-clearonly` | Target `A`: Clear RED, Clear GREEN, no draw; back; shown. | (128,128) GREEN |
| `rt-autogen` | A 64x64 `D3DUSAGE_RENDERTARGET \| D3DUSAGE_AUTOGENMIPMAP` texture (`GetLevelCount` 1): target, Clear RED; back; drawn on the 8x8 quad (124,124)-(132,132) with linear min and mag and point mip filters. | (128,128) RED |
| `rt-release` | Two textures: `B` made, set as target, released by the row, Clear GREEN, back to `rtSurf`; `C` made, Clear RED through its target, set at stage 0, released, drawn. | every call `D3D_OK`; (128,128) RED |
| `rt-srgb` | Target `A` with `D3DRS_SRGBWRITEENABLE`: Clear 0x808080, a pre-transformed quad (0,0)-(32,64) of diffuse 0x404040; write enable off. Read; shown with `D3DSAMP_SRGBTEXTURE`. | read (48,32) 0xbcbcbc and (16,32) 0x898989, each channel within 2; shown (160,128) 0x808080 and (96,128) 0x404040 within 2 |
| `rt-ps` | Target `A`: Clear GREEN; a ps_2_0 that outputs c0 = (1,0,1,1) on an untransformed full-clip quad. Read. | (32,32) 0xff00ff |
| `rt-update` | A 4x4 system-memory A8R8G8B8 texture filled 0xff00ffff, `UpdateTexture` into a 4x4 render-target texture; shown. | (128,128) 0x00ffff |
| `rt-mrt` | `SetRenderTarget(1, A)`. | strict: `D3DERR_NOTAVAILABLE`; otherwise `D3D_OK`, then `SetRenderTarget(1, nullptr)` |
| `rt-msaa` | `CreateRenderTarget(64,64,X8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE)`. | strict: `D3DERR_NOTAVAILABLE`; otherwise any answer, a surface made is released |
| `rt-cubelock` | A cube render-target texture's `LockRect(face 0, level 0)`. | strict: `D3DERR_NOTAVAILABLE`; otherwise any answer, a lock made is unlocked |

`targets_refusals`: `IDirect3DDevice9::SetRenderTarget (index above 0)`, `IDirect3DDevice9::CreateRenderTarget
(multisampled)`, `IDirect3DCubeTexture9::LockRect (a render-target texture)`.

Changed rows elsewhere: `tex-rttex` (a 4x4 render-target texture cleared GREEN through `SetRenderTarget`, drawn by
`ffTexDraw`: (128,128) GREEN in every lane), `tex-vtxrt` (the same texture at vertex sampler 0: the quad's colour
GREEN in every lane); `tex-rtlock`, `tex-vtxdepth`, `tex-vtxdepthfmt`, `tex-depthtex`, `tex-fetch4` unchanged.
