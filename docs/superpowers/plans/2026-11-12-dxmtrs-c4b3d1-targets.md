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
render-target usage and per-subresource tracking, zeroed by the core's resource initializer; a standalone
render-target surface is one too, under an id of its own. The pipeline key carries colour 0's pixel format. The front
end's core arms read the bound target's size and format where they read the present parameters. Nothing changes for
the default runtime.

**Tech Stack:** Rust 1.99.0 (DXMTRS: mtld3d-core, d3d9-rt, d3d9.dll front end, dxmt-fakes), C++ (`d3d9_triangle`), sh
gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4: D3D9 on the core; line "C4b-3d (targets, MRT,
depth, MSAA, blending, fetch4, scissor, the creation gate) follows"). Maps: `build/c4b3d/map.md` (the whole of
C4b-3d) and `build/c4b3d/map-3d1.md` (this slice; file:line and excerpts for every claim below, cited as M1 §n and
M2 §n; its §1.3 claim that a cube render target locks through staging is wrong, see ruling C4b3d1-11). Reviews:
`build/c4b3d/s1/review-{1-design,2-core,3-exec}.md` (folded; dispositions at the end).

## Global Constraints

- The default runtime (mtld3d) is unchanged: translator key `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9`
  and the plain d3d9.dll imports unchanged; every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]`
  and a runtime `is_core()` check. Shared mtld3d-core functions keep mtld3d's behaviour (its unit tests pass unchanged).
- `tools/rust-check.sh` passes after every DXMTRS commit (fmt on the pinned nightly, gen, clippy, doc, test, audit);
  it lints d3d9 with `core-runtime`, so a d3d9-rt API change carries its front-end call sites in the same commit.
- No new dependency. No `HashMap` (FxHashMap). No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body.
- No dxmt-core change (review 2 found none needed): D3D10-12 are untouched.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`) and `build/c4b3c/mn` (branch `c4b3d-mn`), cargo target
  `build/c4b3c/cargo`; this slice's gate scripts (`build/c4b3d/s1/gates`, adapted from C4b-3c's: worktree root
  `build/c4b3c`, branches `c4b3d`/`c4b3d-mn`, state names `c4b3d-*`, `DXTIP`/`MNTIP` to pin a commit), logs and
  reviews in `build/c4b3d/s1`. Builds and GPU jobs under the pipeline lock, `$D` and the main checkout restored by
  each job's trap. No make in a worktree. A job tests **committed** tips: commit before it starts.
- Commands: cargo as `cargo test --offline --target aarch64-apple-darwin -p <crate>` after
  `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0 CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`
  (d3d9-rt adds `--config 'target.aarch64-apple-darwin.rustflags=["-C","link-arg=-Wl,-undefined,dynamic_lookup"]'`);
  the mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name; rt tests use device handles no other test uses
  (0xf3-0xff here; 0xf1 and 0xf2 are taken).
- Every DXMTRS commit on the branch is safe to run under core: a refusal is lifted only in the commit that makes its
  calls correct (Task 4). MacNeutron's core lanes are red between Task 3's rows and Task 6's pin by design, as in
  C4b-3c.
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
`GetRenderTargetData` from any of these into a system-memory surface under mtld3d's rules and answers;
`UpdateTexture` into a render-target texture, in API order.

Out (owners): a depth-stencil surface, depth state, depth and fetch4 sampling, the auto-depth creation gate (3d2);
blending, scissor, rect and partial-viewport clears, the hardware sRGB attachment (3d3); `SetRenderTarget` at index
1-3 (3d4); multisampled targets (3d5); `StretchRect`, `ColorFill`, `UpdateSurface`, a render-target texture's or
surface's `LockRect`, `CreateRenderTargetEx`, `Reset`, additional swap chains' own back buffers, render scale (C4c);
per-draw dedup (C4d).

## Review Focus

1. A target switch in mid-frame keeps both targets' contents, and a target rewritten after it was sampled is not
   reordered past its reader: a quad drawn to the back buffer before `SetRenderTarget` to a texture and back is
   still there (`rt-switch`); a texture drawn, shown, redrawn and shown again in one frame shows each state
   (`rt-rewrite`; the d3d9-rt test `a_rewritten_target_is_not_merged_past_its_reader`, C4a's ruling C4-3).
2. A Clear of a target with no draw after it still happens, and two Clears of one target land in order
   (`rt-clearonly`): the core folds a Clear only into a later pass on the same view (M2 §9.5).
3. A level below 0 and a cube face: the pass, the viewport reset and the read-back use the level's size
   (`max(1, size >> level)`) and the face's slice, and the read-back waits for that subresource's writer
   (`rt-mip`, `rt-cube`; `a_read_back_waits_on_its_subresource`).
4. Formats that are not 4 bytes a texel or not BGRA: the pipeline's colour format, the tile-barrier key (asserted
   in the fake's `target0` line) and the read-back row follow the target (`rt-formats`: 4, 8 and 16 bytes; unorm and
   float; a red quad, so a red/blue swap shows).
5. Lifetime and order: a render-target texture the application released while it is bound as the target or at a
   stage is still drawn to and sampled, and its Clear does not land on the back buffer (`rt-release`); an
   `UpdateTexture` into the bound target lands before the draws that follow it (`rt-updatebound`); in d3d9-rt,
   forgetting the current target's texture falls back to the back buffer and closes the pass (unit test).

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
- [ ] **Step 2: run** `cargo test ... -p mtld3d-core pipeline_key` — Expected: does not compile (no field
  `color_format`).
- [ ] **Step 3: implement** the two fields and the forward; every constructor in the workspace sets BGRA8.
- [ ] **Step 4: run** the same command — Expected: pass; then `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 5: commit** `mtld3d-core: the pipeline key carries colour 0's pixel format`.

### Task 2: the core device's colour target (dxmt-fakes, d3d9-rt)

**Files:** Modify `src/dxmt/fakes/src/lib.rs`, `src/d3d9/rt/src/device.rs`, `src/d3d9/rt/src/device/{recorder,surface,
textures,draw,swapchain}.rs`; Create `src/d3d9/rt/src/device/target.rs` and `target/tests.rs`; Test also
`recorder/tests.rs`, `surface/tests.rs`, `textures/tests.rs`, `draw/tests.rs`. The two front-end call sites the API
change breaks, in this commit: `src/d3d9/src/texture.rs` (`core_flush_dirty`'s desc: `render_target: false`) and
`src/d3d9/src/device/core_arms.rs` (`read_back(TargetRef::BackBuffer, ..)`).

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
      /// The current target's (width, height) and `WMTPixelFormat`; `None` when it does not resolve.
      pub fn target_size(&self) -> Option<(u32, u32)>;
      pub fn target_format(&self) -> Option<u32>;
      pub fn clear(&mut self, rgba: [f32; 4]);                    // the current target, whole
      pub fn read_back(&mut self, source: TargetRef, dst: &mut [u8], dst_pitch: usize) -> bool;
  }
  pub struct CoreTextureDesc { .., pub render_target: bool }
  ```
- Rules the tests pin:
  - A target resolves when it is `BackBuffer` with a back buffer, or names a stored texture made with
    `render_target`, `level < levels`, and `face` is `Some(0..6)` exactly for a cube. Its view key is built
    literally (not through `view::texture_view`, which zeroes a 2D view's slice): `TextureViewDescriptor { format:
    the stored pixel format, texture_type: WMT_TEXTURE_TYPE2D, first_mip: level, mip_count: 1, first_slice: face or
    0, array_size: 1 }` (ruling C4b3d1-3; the tracker takes slice and level from the key's fields, and mtld3d makes
    the same Metal view), its size `max(1, size >> level)`, the pass's `render_target_array_length` 0 and
    `tile_barrier_pso_key[0]` its pixel format, `[8]` 1.
  - `set_target` to a different target closes the open render pass (`close_pass`); to the current one it records
    nothing. `open_render_pass`, `clear` and `draw_up` use the current target; `draw_up` answers
    `DrawRefused::NoBackBuffer` when it does not resolve, and `debug_assert`s the key's `color_format` is the
    target's.
  - `texture_create` with `render_target`: usage `RENDER_TARGET | SHADER_READ` (and `PIXEL_FORMAT_VIEW` with
    `view_twins`), allocation `GPU_PRIVATE` without `SHADER_READONLY` (a tracker per subresource, M2 §7.2), then
    every level and face zeroed by `queue().initializer().init_texture_zero(&texture, &allocation, slice, level)`,
    D3D11's route (ruling C4b3d1-2): nothing enters the chunk being recorded and an open pass stays open.
    `desc.block.2` is the texel's bytes.
  - `forget_texture` of the current target's texture: the target becomes `BackBuffer`, the open pass closes.
  - `read_back(source, ..)`: the tight row is `width * texel bytes` of the source's level (4 for the back buffer);
    the blit and its tracked read name the source's own level and slice (`key.first_mip()`, `key.first_slice()`);
    false when `source` does not resolve, `dst_pitch` is under the row or `dst` is short. The read-back staging is
    remade when its length or row differs from the last one's.
  - `build_pipeline` sets `info.colors[0].pixel_format = source.color_format`.
- Fakes (each line after the existing line it extends): the render-encoder fake prints
  `  target0 <handle> <width>x<height> key=<tile_barrier_pso_key[0]>` after `color0` (the attached texture or view's
  handle; a clear-only encoder's size is 0x0, so sizes are asserted on draw passes); the texture-view fake prints the
  view's handle, its parent's, format, type, first mip and first slice (the level-0 view of a one-level 2D texture
  is the texture itself: no view line); the pipeline fake prints `  pso color0=<pixel_format>` after its line; a
  texture-to-buffer copy prints `    copy level=<n> slice=<n> row=<bytes_per_row>` after its `  blit 3` line.

- [ ] **Step 1: failing tests** (each on its own fake device handle), in `target/tests.rs` unless named:
  - `a_render_target_texture_has_the_usage_and_starts_zeroed`: `texture_create` of a 2-level 2D `render_target`
    desc records a `MTLDevice_newTexture` whose bits hold `WMT_TEXTURE_USAGE_RENDER_TARGET`, and the initializer's
    two clears (`load=2`, clear `0,0,0,0`) reach the device before the first draw's pass.
  - `creating_a_target_leaves_the_open_pass_open`: draw, `texture_create(render_target)`, draw, commit: the two
    draws share one render encoder of the back buffer.
  - `a_draw_targets_the_set_texture`: after `set_target(Texture { id, level: 0, face: None })` a `draw_up` opens a
    pass whose `target0` line names that texture at 64x64 with `key=` its format (the fixture's back buffer is
    64x32 BGRA8), and the pipeline's `pso color0=` is the key's format.
  - `set_target_closes_the_open_pass`: draw (back buffer), `set_target(texture)`, draw, commit: two draw encoders
    whose `target0` handles are the back buffer's then the texture's, in that order; `set_target` to the current
    target between two draws: one encoder.
  - `a_clear_folds_into_the_targets_pass`: `set_target(texture)`, `clear([1, 0, 0, 1])`, draw, commit: the pass on
    the texture has `color0 load=2 ... clear=1,0,0,1`, and no other chunk encoder targets it.
  - `a_level_and_a_face_have_their_own_views`: level 1 of a 64x64 3-level texture resolves to 32x32 through a view
    line with first mip 1; face 3 of a cube through one with first slice 3, type 2D; a `face` on a 2D texture,
    `None` on a cube, level 3 of 3 levels, an id the store lacks and a texture made without `render_target` do not
    resolve (`set_target` false, `target()` unchanged, `target_size()` of the unchanged target).
  - `forgetting_the_current_target_falls_back_to_the_back_buffer`: `target()` is `BackBuffer`, and a draw after it
    lands in an encoder whose `target0` is the back buffer's handle.
  - `a_rewritten_target_is_not_merged_past_its_reader`: draw to A, draw to the back buffer sampling A, draw to A,
    draw to the back buffer sampling A, commit: four render encoders in that order (`target0` A, back buffer, A,
    back buffer).
  - `pipelines_differ_by_colour_format` (`draw/tests.rs`): two keys differing only in `color_format` build two
    pipelines.
  - `read_back_reads_the_named_target_at_its_texel_size` (`surface/tests.rs`): an RGBA16 16x8 target: the copy
    line's `row=128`, and a `dst_pitch` of 127 is refused; reading `BackBuffer` while a texture is current reads
    the back buffer.
  - `a_read_back_waits_on_its_subresource` (`surface/tests.rs`, after
    `readback_waits_on_the_render_pass_then_its_seq`): a draw to level 1, then `read_back` of level 1: the copy
    line says `level=1`, after that level's render encoder ended.
- [ ] **Step 2: run** `cargo test ... -p d3d9-rt --config ...` — Expected: does not compile (`TargetRef`,
  `render_target`).
- [ ] **Step 3: implement.** The resolved target is computed at use from `TargetRef` and the store (no cached
  `Arc` to go stale); `back_buffer_view` callers in recorder and surface go through one private
  `resolve(&self, TargetRef) -> Option<Resolved { texture, key, width, height, pixel_format, texel_bytes }>` in
  `target.rs`. `present` still presents the back buffer whatever the target. If the initializer cannot be driven
  from d3d9's queue profile, the zeroing falls back to `close_pass()` then one `clear_color` per subresource view,
  and `creating_a_target_leaves_the_open_pass_open` becomes "closes it first" (ledgered with C4b3d1-2's cost).
- [ ] **Step 4: run** the same command — Expected: every new and existing rt test passes (the existing ones
  unchanged but for the `key` helpers); `cargo test ... -p dxmt-fakes` passes; `sh tools/rust-check.sh` ok.
- [ ] **Step 5: commit** `d3d9-rt: a current colour target (the back buffer or a render-target texture's level or face)`.

### Task 3: rows (MacNeutron)

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s1/gates/{jq,jm}.sh` (the summary grep gains `targets`; the mutant pairs).

**Interfaces:**
- Produces: a mode `targets` (`d3d9_triangle targets [strict]`, 150 s budget, rows in the appendix), launched in
  every D3D9 lane after `fixed` (`launch ours fresh targets 240 targets $strict`; VM `tri ... tg64-fresh 240
  targets $strict` and `tg32-fresh`) and checked by name; `targets_refusals` (two names, appendix) checked like the
  other sets; `tex-rttex` and `tex-vtxrt` become drawn rows; `texture_refusals` loses the render-target-sampled
  name (three left) and `fixed_refusals` keeps the depth one alone.
- Script edit points (review 3 M2): `dxmt/d3d9-check.sh` the sets near :130-136, `refusals_ok`'s outer pattern
  `state|textures|fixed|targets` **and** an inner `targets) set_=$targets_refusals ;;` arm (without it the inner
  `*)` hands `targets` the fixed set), the launch and `check targets "..."` before the mtld3d-lane refusal check,
  the header comments and counts; `vm/dxmt/d3d9-gate.sh` the sets (:168-173), a `tg64-fresh tg32-fresh` row loop, a
  `runtime ... targets` check with the same two-level case, the headers; `vm/tests/d3d9.sh` the two `tri` lines and
  its header.
- Harness: `frames()` binds `rtSurf` at render target 0 before its `SetViewport(&FULL)` (a retry after a `draw()`
  that left another target bound clears the back buffer again); `reset()` restores render target 0 to `rtSurf`,
  unbinds 1-3 and sets the full viewport; a helper `until(attempt)`: each try runs `attempt()` (the target switch,
  Clears and draws inside `BeginScene`/`EndScene`, back to `rtSurf`, then the read and its asserts, setting `why`),
  then `Present`, until it passes, for 10 s within the budget, once under `strictMode`. `matches()`'s tolerance (1
  a channel) stands for every row.

- [ ] **Step 1: write the rows** and the script lists (appendix), the header comments and counts.
- [ ] **Step 2: syntax** `-std=c++17 -fsyntax-only -Wall -Wextra dxmt/tests/d3d9_triangle.cpp` with the x86_64,
  i686 and aarch64 `*-w64-mingw32-clang++` of `build/dxmt-src/llvm-mingw-own/bin` — Expected: no diagnostics.
- [ ] **Step 3: commit** `dxmt, vm: d3d9_triangle targets rows in every D3D9 lane (DXMTRS C4b-3d1)` (the jobs
  check out committed tips).
- [ ] **Step 4: rows first, on mtld3d** (job `jr`: plain build of DXMTRS at Task 2's commit, `sh dxmt/d3d9-check.sh`
  default and `D3D9_BACKEND=msl`; then job `jcr`: the VM image at that commit, `vm/dxmt/d3d9-gate.sh` default
  lanes, old and new WoW64, which first run the 32-bit exe and time `tg64`/`tg32` against the budget) — Expected:
  every `targets` row ok in each lane, `tex-rttex` and `tex-vtxrt` ok, the other launches as before. A row mtld3d
  fails is ruled on (fixed if the row is wrong; dropped with its limit named if mtld3d is), ledgered, recommitted.
- [ ] **Step 5: RED under core** (job `jq-red`: core build of DXMTRS at Task 2's commit, `D3D9_RUNTIME=core`) —
  Expected: every `targets` row FAILs but `rt-mrt`, `rt-msaa` and `rt-cubelock` (each why names `SetRenderTarget`,
  `CreateRenderTarget` or `DrawPrimitiveUP` with 8876086a); `tex-rttex` and `tex-vtxrt` FAIL; `textures-runtime`,
  `fixed-runtime` and `targets-runtime` FAIL (the sets; `rt-mrt`'s refusal still has its old name); `shaders`,
  `state` and the other rows unchanged.

### Task 4: the front end's targets; the refusals go

**Files:** Modify `src/d3d9/src/device.rs` (`device_set_render_target`'s core arm, `device_create_render_target`,
`create_color_target_surface`), `src/d3d9/src/device/core_arms.rs`, `src/d3d9/src/device/core_draw.rs`,
`src/d3d9/src/surface.rs`, `src/d3d9/src/texture.rs`, `src/d3d9/core/src/pipeline_key.rs` and its tests.

**Interfaces:** consumes Tasks 1-2.
- `Direct3DSurface9::core_target(&self) -> Option<TargetRef>` (core-runtime): `BackBuffer` for a back-buffer
  surface (the implicit one and an additional swap chain's, ruling C4b3d1-4); `Texture { id: parent's id, level:
  mip_level(), face: cube_face() }` for a level of a `D3DUSAGE_RENDERTARGET` texture; `Texture { id, level: 0, face:
  None }` for a standalone surface holding a core id; `None` otherwise.
- `SurfaceInner` holds a standalone render target's core `TextureId`; `finalize_surface` forgets it
  (`core.forget_texture`). `CreateRenderTarget` under core: mtld3d's checks in mtld3d's order with mtld3d's answers
  (M2 §3 steps 1-4; the format checks of `create_color_target_surface` move into one helper both runtimes call, a
  refactor of mtld3d's path that changes none of its answers), a sample count above 1 refused as
  `IDirect3DDevice9::CreateRenderTarget (multisampled)`, then `texture_create(new id, render_target desc)` and the
  surface without Metal handles. A lockable one is made the same (its `LockRect` stays refused with every
  non-system-memory surface's).
- `core_flush_dirty`: a render-target texture gets its core texture (`render_target: true`) with no first upload
  (its `fresh` mask is the dirty mask alone); dirty levels upload as any texture's. Depth formats still answer false.
- A CPU write into a render-target texture lands in API order: the core branch of the function behind
  `publish_render_target_write` (`texture.rs` near :6037, where it returns at once today) runs `core_flush_dirty`
  for a render-target texture, as mtld3d publishes there; `UpdateTexture` and `GenerateMipSubLevels` already route
  through it.
- `SetRenderTarget` core arm: index above 0 with a surface is refused as `IDirect3DDevice9::SetRenderTarget (index
  above 0)`; at 0: the surface's `core_target()` (none: refused as `IDirect3DDevice9::SetRenderTarget (a surface
  the core does not target)`), the parent texture flushed (`core_flush_dirty`) so its core texture exists, then
  `core.set_target` (false: the same refusal), and only then mtld3d's bookkeeping in mtld3d's order:
  `cur_autogen_rt_ids[0]` (an autogen texture the slot leaves has `core.texture_generate_mips(old)` recorded, which
  closes the pass that wrote it), `replace_render_target`, the viewport and scissor reset to the surface's size,
  the dirty marks.
- `core_arms::clear`: `viewport_covers` against `core.target_size()`; the refusal's name becomes
  `IDirect3DDevice9::Clear (a viewport not covering the target)`. `core_arms::get_render_target_data` follows
  mtld3d's order and answers (`device.rs:8883-8975`): a NULL or non-surface `rt` or `dst`, a destination that is
  not system memory, a source that is no render target (`core_target()` none) and a `reject_readback_dst` reject
  (the source's `GetDesc` size and format) all answer `D3DERR_INVALIDCALL` with no refusal line; an additional swap
  chain's back buffer stays refused as `IDirect3DDevice9::GetRenderTargetData (an additional swap chain's back
  buffer)` (it would read the device's, ruling C4b3d1-4); a texture source is flushed first; then
  `core.read_back(source, ..)`.
- `core_draw`: the target size from `core.target_size()` (both `pos_fixup` and `core_viewport`), the key's
  `color_format` from `core.target_format()`; `CoreRefusal::RenderTargetTexture` and
  `DrawFlags::SAMPLES_RT_TEXTURE` are deleted with their tests' cases, and the fold over the vertex slots keeps
  its depth half.

- [ ] **Step 1: failing test** (mtld3d-core): in `core_draw_kinds_cover_every_case`, beside the distinct-names
  array, an exhaustive `match` over each case's `Err` naming `NotCoreShader | Blending | Scissor | DepthTexture |
  Fetch4` — Expected (`cargo test ... -p mtld3d-core pipeline_key`): does not compile (non-exhaustive:
  `RenderTargetTexture`). The behavioural RED is Task 3 Step 5's rows (the front end has no unit tests).
- [ ] **Step 2: implement** the arms above; delete the variant, the flag and the case.
- [ ] **Step 3: run** the test — pass; `sh tools/rust-check.sh` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: a core device renders to render-target textures and surfaces and samples them`.
- [ ] **Step 5: GREEN under core** (job `jq`: core build at this commit, `D3D9_RUNTIME=core`) — Expected: `targets`
  21/21 strict, `textures` 38/38, `fixed` 31/31, `state` and `shaders` as before, each refusal set exact.

### Task 5: gates and probe

- [ ] **jb** (core build at the tip): the stateblock conformance under core equals default; `d3d9-check` core and
  default ok.
- [ ] **jm** mutation REDs, one build each: the named row red and no launch other than `targets` changed.
  (a) `open_render_pass` resolves `BackBuffer` always → `rt-tex` (and the other rows that draw into a texture);
  (b) `build_pipeline` sets BGRA8 → `rt-formats` (predicted: a red/blue swap on A8B8G8R8; the job confirms);
  (c) the target view's `first_mip` 0 → `rt-mip`; (d) the autogen regeneration removed → `rt-autogen`;
  (e) `read_back`'s row copy into `dst` takes 4 bytes a texel (the blit unchanged) → `rt-formats` (its read half).
- [ ] **ja** (plain build at the tip): translator key and imports unchanged; `d3d9-check` default and msl ok;
  Wine's conformance on the default runtime (`mtld3d-conformance.sh`, about 3 minutes) as C4b-3c's.
- [ ] **jc** (VM): `sh vm/dxmt/d3d9-gate.sh` in the four lanes (default and core, old and new WoW64) — Expected: ok.
- [ ] **jp** probe: `--bench 2000 <0|600> tex` on mtld3d and core, Mac x64, each once, net per frame beside
  C4b-3b's +22% (`build/c4b3d/s1/probe.md`). It measures the back-buffer path: the resolve added per draw; the
  render-to-texture path has no bench until a title needs one.

### Task 6: review and push

- [ ] Whole-branch review by a fresh Opus reviewer (the package, this plan, the spec, Review Focus verbatim, the
  ledger's rulings); one fix pass for Critical and Important findings, each RED then GREEN.
- [ ] Spec §5 paragraph, pin, privacy scan, push DXMTRS `c4b3d` and MacNeutron `c4b3d-mn` to their `main`s
  (fast-forward), post-push check (plain build from main, tests, x64 `dxmt/check.sh`), memory, scratch deleted.

## Rules check

- No per-draw allocation is added: the target resolves to an `Arc` clone and a key, as the back buffer's did.
- The pass description, Clear and read-back share one resolver (`target.rs`), so a new target kind (3d4's slots,
  3d5's multisampled storage) has one place to extend; review 1 checked that `TargetRef` extends to 3d2-3d5
  without a rewrite.
- Front-end additions are core arms beside mtld3d's code; the one edit to mtld3d's path is the extraction of
  `create_color_target_surface`'s format checks into a helper both call. `reject_readback_dst` is called, not copied.

## Rulings

- **C4b3d-1 (five slices; the C4b outline's remaining items).** C4b-3d is planned, reviewed (three adversarial Opus
  reviews a plan), gated and pushed as 3d1-3d5. The outline's unplaced C4b items: DONT_CARE planes, `drop_dead_store`
  and the later store in `same_targets` are 3d2's to take or rule out (depth planes); `clear_into_render`'s area
  guard is 3d3's (moot while its rect clears are draws); `Resolve::discard_source` is 3d5's; the pass binder is
  absorbed (the core's own passes since C4b-3b); of `map2-passes.md`'s two characterization tests one is C4b-3b's
  `render_blit_render_merges_when_unsampled` and the write/sample/rewrite one is this slice's, with 3d2's plan to
  check that file for a remainder; colour self-sampling detection is dropped (C4b3d1-6). Cost if wrong: five
  review rounds; a design choice of an early slice reworked by a later one; an outline item closed unbuilt.
- **C4b3d-2 (blending is 3d's).** The spec's line names blending in C4b-3d; C4b-2's plan had put it in C4c. The spec
  wins. Cost if wrong: none.
- **C4b3d-3 (C4c keeps the surface operations).** `StretchRect`, `ColorFill`, `UpdateSurface`, surface and
  render-target locks and `Reset` stay C4c's, as the C4 outline has them. How a multisampled standalone target is
  observed without `StretchRect` is 3d5's plan's to rule. Cost if wrong: 3d5 pulls a resolve-only `StretchRect`.
- **C4b3d1-1 (sRGB write stays in the shader).** Every target takes the pixel-shader encode and the front end's
  encoded Clear, as the back buffer does; the hardware sRGB attachment is decided in 3d3 by a row with blending.
  Cost if wrong: none without blending (the two encodes differ by rounding only).
- **C4b3d1-2 (new targets start transparent black, through the initializer).** As mtld3d clears a new render-target
  texture and surface; the outline had creation contents in C4c, pulled forward because a fresh target is sampled
  by `rt-fresh` and titles sample targets before drawing them. Cost if wrong: an initializer clear per subresource
  at creation; if the fallback (Clear passes) is taken, a first sampling of a never-targeted texture splits the
  open pass until C4c moves it.
- **C4b3d1-3 (a face is a 2D view).** A level or face is a single-subresource `WMT_TEXTURE_TYPE2D` view, array
  length 0, where D3D11 takes a 2D-array view with array length 1 (layered rendering). Review 2 confirmed the
  tracker and Metal accept it (mtld3d makes the same view). Cost if wrong: `rt-cube` stops the `targets` launch on
  the first core run and the view becomes D3D11's.
- **C4b3d1-4 (additional swap chains still alias).** Their back buffers are the device's as targets (C4b3a-4)
  until C4c; reading one back stays refused. Cost if wrong: a second window's contents drawn into the first.
- **C4b3d1-5 (a render target keeps its staging pages).** C4b3b-3 stands: never dropped under core, and a CPU
  write into a render target uploads through them at the write. Cost if wrong: a render target's size in CPU memory
  each, which a 32-bit title pays until C4c's gauge.
- **C4b3d1-6 (the current target sampled).** A pixel-stage read of the pass's target gets the core's tile barrier,
  unless the same draw also reads it at a vertex slot (the barrier is then dropped); a vertex-stage read is
  unordered. D3D9 leaves all of it undefined and mtld3d handles none; the outline's self-sampling detection is
  dropped. No row. Cost if wrong: a title that relies on it reads stale texels.
- **C4b3d1-7 (no memory accounting; creation does not fail).** A core standalone target is not counted in the
  device's VRAM figure (`GetAvailableTextureMem` is refused under core), and `texture_create` has no failure
  answer, as for every core texture. Cost if wrong: an allocation that fails draws nothing instead of answering
  `D3DERR_INVALIDCALL`.
- **C4b3d1-8 (a fifth launch).** The rows are a mode of their own, not more `textures` rows: their refusal set and
  budget stay apart. Cost if wrong: about 20 s a lane.
- **C4b3d1-9 (the slice's gates).** Rows first on mtld3d (Mac default and msl, the VM's default lanes), RED then
  GREEN strict under core, the stateblock conformance under core, Wine's conformance on the default runtime (Mac and
  the VM's default lanes), the VM's four lanes, the plain build's key and imports, five mutation REDs, the textured
  probe. Wine's suites under core wait for 3d2 (no core device is created by them before the auto-depth gate).
  Cost if wrong: none identified.
- **C4b3d1-10 (read-back pulled forward).** `GetRenderTargetData` from render-target textures and surfaces is one
  of C4c's "every read-back form": taken here because the rows read targets with it. Cost if wrong: none.
- **C4b3d1-11 (a cube render target's lock is D3D9's INVALIDCALL).** `cube_lock_rect` already answers
  `D3DERR_INVALIDCALL` for a default-pool non-dynamic cube in both runtimes; no core refusal is added
  (`rt-cubelock` asserts it in every lane). Cost if wrong: none.

## Appendix: the rows (`d3d9_triangle targets`)

`A` is a 64x64 one-level A8R8G8B8 render-target texture unless a row says otherwise; RED 0xff0000, GREEN 0x00ff00,
BLUE 0x0000ff (the back buffer's clear). Diffuse colours carry alpha 0xff. Objects a row uses are made inside its
per-frame body (a retried frame makes them again). "Read" is `GetRenderTargetData` into a system-memory surface of
the source's size and format, asserted at texel coordinates, through `until`; "shown" is the texture drawn
point-filtered by the fixed-function pixel stage on the back-buffer quad (64,64)-(192,192) of a `frames` call:
texel (x,y) of a 64x64 texture shows at pixels 64+2x and 65+2x, and rows assert at the texel's centre (65+2x,
65+2y). At most eight probes a `frames` call.

| Row | What it does | Asserts |
|---|---|---|
| `rt-tex` | Target `A`: Clear GREEN, a pre-transformed RED quad (8,8)-(24,24). Back to `rtSurf`; shown. | (97,97) RED, (161,161) GREEN, (97,161) GREEN, (161,97) GREEN, (32,32) BLUE |
| `rt-read` | Same contents with the Clear colour 0x8000ff00. Read. | texel (16,16) 0xffff0000, (48,48) 0x8000ff00 (all four bytes), (16,48) green; a 32x32 destination answers `D3DERR_INVALIDCALL`; a NULL source answers `D3DERR_INVALIDCALL` |
| `rt-surface` | `CreateRenderTarget(64,64,X8R8G8B8, none, 0, FALSE)`: `GetDesc`; `SetRenderTarget`; `GetRenderTarget(0)`; Clear RED; an untransformed GREEN quad over clip x,y in [0,1]. Read. | desc 64x64 X8R8G8B8 `D3DUSAGE_RENDERTARGET` `D3DPOOL_DEFAULT`; the same surface back; (48,16) GREEN, (16,48) RED, (16,16) RED |
| `rt-viewport` | `SetRenderTarget(0, A)`: `GetViewport`, `GetScissorRect`; Clear RED; viewport {32,0,32,64}, an untransformed full-clip GREEN quad; back to `rtSurf`: `GetViewport`. Read. | {0,0,64,64,0,1} and {0,0,64,64}; (48,32) GREEN, (16,32) RED; {0,0,256,256,0,1} |
| `rt-big` | A 512x512 X8R8G8B8 texture: Clear RED, viewport {256,256,256,256}, an untransformed full-clip GREEN quad. Read. | (500,500) GREEN, (100,100) RED, (300,100) RED, (100,300) RED |
| `rt-mip` | A 64x64 three-level texture: level 0 Clear RED; level 1: `GetViewport`, Clear GREEN, a pre-transformed BLUE quad (0,0)-(16,16); level 2 Clear 0xffff00. Read levels 1 and 0; shown with `D3DSAMP_MAXMIPLEVEL` 1 and point mip filter. | level 1's viewport 32x32; level 1 (8,8) BLUE, (24,24) GREEN; level 0 (32,32) RED; shown (161,161) GREEN |
| `rt-cube` | A 16x16 X8R8G8B8 cube: each face Clear to its own colour (0xff0000, 0x00ff00, 0x0000ff, 0xffff00, 0xff00ff, 0x00ffff). Read each face; one draw sampling direction (0,0,1) (the `tex-cube` row's draw). | each face's (8,8); the draw shows 0xff00ff (face 4) |
| `rt-formats` | For A8B8G8R8, A16B16G16R16, A16B16G16R16F, R32F, A32B32G32R32F (a create that fails is a FAIL): a 16x16 texture, Clear 0xff4080c0, a pre-transformed RED quad over x < 8. Read at x = 4 and x = 12; shown, one `frames` call a format. | read x = 12: (0x40,0x80,0xc0,0xff)/255 in the format's layout (unorm16 within 257, float within 0.004; R32F red alone); x = 4: (1,0,0,1) (R32F 1.0); shown right 0x4080c0 (R32F 0x40ffff), left 0xff0000 (R32F 0xffffff) |
| `rt-switch` | On the back buffer a pre-transformed RED quad (32,32)-(64,64); target `A`: Clear GREEN; back; `A` drawn on (160,160)-(224,224). | (48,48) RED, (192,192) GREEN, (128,128) BLUE |
| `rt-rewrite` | Target `A`: Clear GREEN; back; `A` drawn on (32,32)-(96,96); target `A`: Clear RED; back; `A` drawn on (160,160)-(224,224). | (64,64) GREEN, (192,192) RED, (128,128) BLUE |
| `rt-clearonly` | Target `A`: Clear RED, Clear GREEN, no draw; back; shown. | (129,129) GREEN |
| `rt-fresh` | A new 64x64 A8R8G8B8 render-target texture, never a target; shown. | (129,129) 0x000000, (32,32) BLUE |
| `rt-autogen` | A 64x64 `D3DUSAGE_RENDERTARGET \| D3DUSAGE_AUTOGENMIPMAP` texture (`GetLevelCount` 1): target, Clear RED; back; drawn on the 8x8 quad (124,124)-(132,132) with linear min and mag and point mip filters. | (128,128) RED |
| `rt-release` | `B` made, set as target, released by the row, Clear GREEN, back to `rtSurf`; `C` made, Clear RED through its target, set at stage 0, released, shown. | every call `D3D_OK`; (129,129) RED, (32,32) BLUE |
| `rt-srgb` | Target `A` with `D3DRS_SRGBWRITEENABLE`: Clear 0x808080, a pre-transformed quad (0,0)-(32,64) of diffuse 0x404040; write enable off. Read; shown with `D3DSAMP_SRGBTEXTURE`. | read (48,32) 0xbcbcbc and (16,32) 0x898989; shown (161,129) 0x808080 and (97,129) 0x404040 |
| `rt-ps` | Target `A`: Clear GREEN; a ps_2_0 that outputs c0 = (1,0,1,1) on an untransformed full-clip quad. Read. | (32,32) 0xff00ff |
| `rt-update` | A 4x4 system-memory A8R8G8B8 texture filled 0xff00ffff, `UpdateTexture` into a 4x4 render-target texture; shown. | (129,129) 0x00ffff |
| `rt-updatebound` | Target `A`: Clear GREEN; `UpdateTexture` of a 64x64 system-memory texture filled 0xff00ffff into `A`; a pre-transformed RED quad (0,0)-(32,64). Read. | (16,32) RED, (48,32) 0x00ffff |
| `rt-mrt` | `SetRenderTarget(1, A)`. | strict: `D3DERR_NOTAVAILABLE`; otherwise `D3D_OK`, then `SetRenderTarget(1, nullptr)` |
| `rt-msaa` | `CreateRenderTarget(64,64,X8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE)`. | strict: `D3DERR_NOTAVAILABLE`; otherwise any answer, a surface made is released |
| `rt-cubelock` | A cube render-target texture's `LockRect(face 0, level 0)`. | `D3DERR_INVALIDCALL` in every lane |

`targets_refusals`: `IDirect3DDevice9::SetRenderTarget (index above 0)`, `IDirect3DDevice9::CreateRenderTarget
(multisampled)`.

Changed rows elsewhere: `tex-rttex` (a 4x4 render-target texture cleared GREEN through `SetRenderTarget`, drawn by
`ffTexDraw`: (128,128) GREEN in every lane), `tex-vtxrt` (the same texture at vertex sampler 0: the quad's colour
GREEN in every lane); `tex-rtlock`, `tex-vtxdepth`, `tex-vtxdepthfmt`, `tex-depthtex`, `tex-fetch4` unchanged.

## Review dispositions

Review 1 (design): F1 folded (the publish arm, `rt-updatebound`); F2 folded (mtld3d's order and answers, the NULL
source in `rt-read`); F3 folded (the rt test, `rt-rewrite`); F4 folded (C4b3d-1); F5 folded (the initializer,
C4b3d1-2 and -10, `rt-fresh`); F6 folded (`set_target` before the bookkeeping); F7 folded as a `debug_assert`;
F8 ruled (C4b3d1-7); F9 folded (the helper named); F10 folded (the refusal kept); F11 folded (Task 3 Step 5);
F12 folded (C4b3d1-6). Review 2 (core): C1 folded (the initializer; the fallback closes the pass first); I1-I5
folded (the two call sites, handles 0xf3+, the copy fake line, the read-back's level and slice, the publish arm);
M1-M4 folded. Review 3 (rows and gates): C1 folded (commit before the jobs); I1 folded (C4b3d1-11); I2-I4 folded;
M1-M10 and M12 folded; M11 folded as job `jcr`.

## Execution notes (2026-11-13)

- The rows are 22: the final review's Critical finding added `rt-huge` (`CreateRenderTarget(16385, 16, ...)` answers
  `D3DERR_INVALIDCALL` in every lane; the core answered `D3D_OK` before DXMTRS dc1eba82, which checks the extent
  mtld3d's Unix create checks).
- At the RED before Task 4, 18 of the then 21 rows failed; `rt-mrt`, `rt-msaa` and `rt-cubelock` pass on both sides
  of the change by design.
- The fake lines as built: `  target0 #<handle> <width>x<height>` (no key: the tile-barrier key is not on
  winemetal's wire, so no host test sees it), `MTLTexture_newTextureView #<view> of #<texture> format= type= mip=
  slice=`, `  pso color0=<format>`, `    copy_to_buffer #<texture> level= slice= size= bytes_per_row=`.
- The initializer route worked from D3D9's queue (no fallback). `creating_a_target_leaves_the_open_pass_open`
  became `a_target_made_between_two_draws_is_zeroed_ahead_of_them`: whether the pass was closed cannot be seen
  through encoders, which the optimizer merges.
- No helper was extracted from `create_color_target_surface`: the core arm is a branch inside it, after mtld3d's
  format checks.
- Mutation (b) did not show the predicted red/blue swap: A8B8G8R8 passed under a BGRA8 pipeline and the 8-byte format
  caught it. No row detects a wrong pipeline format on a 4-byte target that is not BGRA.
- The fix pass also removed five target resolves a draw (review minor M3, re-graded by the probe): a draw reads the
  target's shape without a reference (DXMTRS 2acb1d99). `build/c4b3d/s1/probe.md`.
- `dxmt/d3d9-check.sh` removes the work folder it made (each run had left 3.7 GiB in the temp folder).
