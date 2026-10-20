# D3D9 M2a: mtld3d on winemetal, proved on macOS — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** mtld3d's Unix side builds and runs with every Metal call going through winemetal's Unix-call table, and that
build (the "loopback" build, aarch64-apple-darwin with the `winemetal` feature) passes M1's gates on macOS.

**Architecture:** A facade crate (`mtld3d/unix/mtl`, package `mtld3d-mtl`) offers the subset of objc2's bindings that
mtld3d's Metal layer uses, with the same names and signatures, implemented over winemetal's slots (loaded from the
`winemetal.so` beside `mtld3d.so`). mtld3d-unix reaches Metal through one module, `crate::mtl`, which re-exports objc2
on the product build and the facade under `winemetal`. Under the feature, the macOS-only window code is left out and
presentation goes through winemetal's metal views. The product build is unchanged.

**Tech Stack:** Rust 1.99.0 (pinned), mtld3d 0.14.1 under the DXMT fork's `mtld3d/`, DXMT's winemetal (C/Objective-C),
llvm-mingw 20261006.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-d3d9-mtld3d-design.md` (§5.1 decides this plan's shape;
`docs/research/2026-10-20-mtld3d-winemetal-inventory.md` is its evidence, line numbers mtld3d 0.14.1's).

## Global Constraints

- Every change inside `build/wine-arm64-src/dxmt` is a DXMT fork commit (branch `macneutron`, trailer
  `Assisted-by: Claude Code: claude-opus-5-5`), exported with `sh wine-arm64/export.sh`; keep only the new patches
  (export's abbreviated-hash churn is not committed; recompute the series stamps as M1 Task 5 did).
- The product build (`make wine-arm64`) is byte-for-byte M1's behaviour: no `winemetal` feature, no change in what
  objc2 code runs. Its gates (`dxmt/d3d9-check.sh`, `dxmt/mtld3d-conformance.sh`, `make dxmt-check`) stay as M1 left
  them.
- winemetal grows only by appending: render nodes after `WMTRenderCommandSetDepthStencilState`; slots 156 and 157 in
  `__wine_unix_call_funcs`, with 153-155 refusing stubs (`STATUS_NOT_IMPLEMENTED`) since the guest's table uses them
  (153-154 its memory allocator, 155 Direct3D forwarding). No existing slot, struct or node changes.
- mtld3d.so calls winemetal's 64-bit table (`__wine_unix_call_funcs`) for 32- and 64-bit processes alike, with
  winemetal's own parameter structs (`winemetal_thunks.h`, `#pragma pack(8)`).
- The facade's values match Metal's: enums keep Metal's raw values (objc2-metal's), so code that switches on them is
  unchanged.
- No new Rust dependency outside mtld3d's existing set (libc, libloading, log, bitflags, rustc-hash are there).

## Review Focus

- A winemetal slot or node index that drifts from the table (a later DXMT change reordering it) would call the wrong
  function: the facade checks, on macOS, each slot it uses against its symbol name (`dladdr`) at load and refuses to
  start on a mismatch (Task 2 test).
- A struct the facade fills where winemetal assigns every field (texture, sampler, pipeline descriptors): an unset
  field must get Metal's default, not zero (`normalized_coords`, `max_anisotropy`, `rasterization_enabled`,
  `sample_count`, `depth`, `array_length`, `lod_max_clamp`) — Task 2's tests read the created object back.
- A completion handler must run exactly once per committed command buffer, in commit order, including for a buffer
  that fails on the GPU: the retirement thread replaces Metal's own handler delivery (Task 3 test with an erroring
  buffer).
- A readback into memory the game owns must not be wrapped (`newBufferWithBytesNoCopy`) under the facade: it goes
  through a staging buffer and a copy after completion (Task 5: `GetRenderTargetData` in d3d9_triangle and the
  conformance runs).
- A process with two devices, or a device recreated on one window (`Reset`, alt-tab): views are created and released
  by winemetal (no parking), so a second attach on one HWND must not leak or stack views (conformance's `d3d9ex` and
  device subtests do this).

---

### Task 1: winemetal's four render nodes and two slots

**Files:**
- Modify (DXMT tree): `src/winemetal/winemetal.h`, `src/winemetal/winemetal_thunks.h`,
  `src/winemetal/winemetal_thunks.c`, `src/winemetal/unix/winemetal_unix.c`

**Interfaces:**
- Produces: nodes `WMTRenderCommandSetVertexBytes` (struct `wmtcmd_render_setbytes`), `WMTRenderCommandSetVertexTexture`
  (`wmtcmd_render_settexture`), `WMTRenderCommandSetVertexSamplerState` and `WMTRenderCommandSetFragmentSamplerState`
  (new `struct wmtcmd_render_setsampler { enum WMTRenderCommandType type; ...next as the others...; obj_handle_t
  sampler; uint8_t index; }`), appended in that order.
- Produces: `struct WMTVertexAttributeInfo { uint32_t format; uint32_t offset; uint32_t buffer_index; }`,
  `struct WMTVertexBufferLayoutInfo { uint32_t stride; uint32_t step_function; uint32_t step_rate; }`,
  `struct WMTVertexDescriptorInfo { struct WMTVertexAttributeInfo attributes[31]; struct WMTVertexBufferLayoutInfo
  layouts[31]; }` (format 0 = unused attribute, stride 0 = unused layout; Metal's raw values).
- Produces: slot 156 `MTLDevice_newRenderPipelineStateWithVertexDescriptor(device, const WMTRenderPipelineInfo *,
  const WMTVertexDescriptorInfo *, obj_handle_t *err_out)` → pipeline; params struct
  `unixcall_mtldevice_newrenderpso_vd { device; WMTConstMemoryPointer info; WMTConstMemoryPointer vertex;
  obj_handle_t ret; obj_handle_t err_out; }`.
- Produces: `struct WMTCompileOptions { uint64_t language_version; uint8_t preserve_invariance; uint8_t math_mode; }`
  (raw `MTLLanguageVersion`, `MTLMathMode`) and slot 157 `MTLDevice_newLibraryWithSourceOptions(device, source,
  length, const WMTCompileOptions *, obj_handle_t *err_out)` → library; params `unixcall_mtldevice_newlibrary_source_options
  { device; WMTConstMemoryPointer source; uint64_t length; WMTConstMemoryPointer options; obj_handle_t ret;
  obj_handle_t err_out; }`.

- [ ] Step 1: the RED is Task 2's slot check and Task 2/3's tests that use these; record in the ledger that Task 1 is
  verified there, and build DXMT with the additions (`make wine-arm64`, which rebuilds winemetal.so).
- [ ] Step 2: implement: the nodes' cases in `_MTLRenderCommandEncoder_encodeCommands` (`setVertexBytes:length:atIndex:`,
  `setVertexTexture:atIndex:`, `setVertexSamplerState:atIndex:`, `setFragmentSamplerState:atIndex:`); slot 156 builds
  the pipeline descriptor exactly as slot 34 does, then fills `vertexDescriptor` from the info (attributes and layouts
  whose format/stride is non-zero); slot 157 makes an `MTLCompileOptions` with the three fields and calls
  `newLibraryWithSource:options:error:`; the PE thunks for completeness; stubs at 153-155 in the macOS table.
- [ ] Step 3: `make wine-arm64` passes; `make dxmt-check` unchanged (run at Task 6).
- [ ] Step 4: commit (DXMT 0101) and export.

### Task 2: the facade: loader, objects, resources, state, libraries, pipelines

**Files:**
- Create (DXMT tree): `mtld3d/unix/mtl/Cargo.toml`, `mtld3d/unix/mtl/src/lib.rs` and modules `table.rs` (loader and
  slot calls), `objc.rs` (the `objc2`-shaped base: `Retained`, `ProtocolObject`, `autoreleasepool`, `Message`),
  `foundation.rs` (`NSString`, `NSError`, `NSRange`), `metal/*.rs` (device, buffer, texture, sampler, depth-stencil,
  library, function, pipeline, enums and descriptor types)
- Modify: `mtld3d/unix/Cargo.toml` (workspace member)
- Test: `mtld3d/unix/mtl/tests/objects.rs`

**Interfaces:**
- Produces: `mtld3d_mtl::table::Table::get() -> Result<&'static Table, String>` (loads once: `MTLD3D_WINEMETAL` if
  set, else `<dir of the loaded image>/winemetal.so`, via `dladdr` of a facade symbol), `Table::call(slot: u32, params:
  *mut c_void) -> i32`.
- Produces: modules named and shaped like objc2's for the used subset: `mtld3d_mtl::objc2`, `::objc2_foundation`,
  `::objc2_metal`, `::objc2_quartz_core`, `::objc2_metal_fx`, `::block2` (Tasks 3-4 fill the last three). Objects are
  `Retained<ProtocolObject<dyn MTLTexture>>` etc. over winemetal handles: `Clone` is `NSObject_retain` (slot 0),
  `Drop` is `NSObject_release` (slot 1); `ProtocolObject::from_handle` borrows without retaining (mtld3d's
  `borrow_retained`).
- Produces: descriptor types (`MTLTextureDescriptor`, `MTLSamplerDescriptor`, `MTLDepthStencilDescriptor`,
  `MTLStencilDescriptor`, `MTLRenderPipelineDescriptor` with its colour-attachment array, `MTLVertexDescriptor` with
  attributes and layouts, `MTLCompileOptions`, `MTLComputePipelineDescriptor` if used) as Rust structs with objc2's
  setter and getter names, filled into winemetal's `WMT*Info` at creation with Metal's defaults for every unset
  field.
- Produces: per-object metadata winemetal has no getter for (texture type, sample count, usage, storage mode, label,
  buffer length, owning device) recorded at creation and answered from the facade.

- [ ] Step 1: write `tests/objects.rs` (runs against `make -C eac-vm/metal/wm metal-host`'s folder through
  `MTLD3D_WINEMETAL`): slot check (each used slot's symbol, `dladdr`, equals its expected `_<name>`); device name
  non-empty and registry id non-zero; a Shared buffer's `contents()` written and read back through a blit (Task 3
  reuses); a texture's width/height/format/type/sample count/mip count read back as created; a sampler and a
  depth-stencil state created from descriptors with only non-default fields set; MSL 2.4 source with
  `preserveInvariance` compiled through slot 157 and `newFunctionWithName`; a render pipeline with a two-attribute
  vertex descriptor through slot 156; a compile error's text in `NSError`'s description.
- [ ] Step 2: run it before Task 1's DXMT is built: the slot check fails on 156/157. Expected: FAIL naming them.
- [ ] Step 3: implement the facade for these types.
- [ ] Step 4: `cargo +1.99.0 test -p mtld3d-mtl` passes.
- [ ] Step 5: commit (DXMT 0102).

### Task 3: the facade: command buffers, encoders as command lists, retirement

**Files:**
- Create: `mtld3d/unix/mtl/src/metal/command.rs` (queue, command buffer, render/blit/compute encoders, render pass
  descriptor), `mtld3d/unix/mtl/src/retire.rs`, `mtld3d/unix/mtl/src/block2.rs`
- Test: `mtld3d/unix/mtl/tests/encode.rs`

**Interfaces:**
- Produces: `MTLRenderCommandEncoder` methods mtld3d calls (the inventory's command table) append nodes to the
  encoder's list (an arena owned by the encoder); `endEncoding` makes one `encodeCommands` call for the list (split
  at 65,536 nodes), then `endEncoding`. Cull mode, fill mode and depth bias coalesce into one
  `SetRasterizerState` node from a shadow that starts at Metal's defaults.
- Produces: `addCompletedHandler(RcBlock)` registers the block with the queue's retirement thread (`retire.rs`),
  which waits on each committed buffer in commit order (`waitUntilCompleted`, `status`) and runs its blocks once; a
  buffer released uncommitted runs none. `block2::RcBlock::new(closure)` is the facade's block.
- Produces: GPU start and end times (`MTLCommandBuffer_property`, slot 130) in seconds, as objc2's.

- [ ] Step 1: write `tests/encode.rs`: a render pass clearing a 64x64 texture and drawing a triangle with vertex
  bytes, a vertex texture and both sampler binds, read back by a blit into a Shared buffer (centre and corner
  pixels); cull and fill changes between two draws coalesce (the second draw's culled triangle is absent); a
  completion handler runs exactly once per buffer, in commit order over 100 buffers; a buffer whose fragment shader
  faults reports an error status in its handler.
- [ ] Step 2: run it: FAIL (no encoder yet).
- [ ] Step 3: implement.
- [ ] Step 4: `cargo +1.99.0 test -p mtld3d-mtl` passes.
- [ ] Step 5: commit (DXMT 0103).

### Task 4: the facade: layers, drawables, presenting, MetalFX

**Files:**
- Create: `mtld3d/unix/mtl/src/quartz_core.rs`, `mtld3d/unix/mtl/src/metal_fx.rs`
- Test: `mtld3d/unix/mtl/tests/present.rs` (layer creation through the metal-host folder's stand-in winemac.so)

**Interfaces:**
- Produces: `CAMetalLayer` (from `CreateMetalViewFromHWND`, slot 72; released with its view, slot 73) with the
  setters mtld3d's attach and presenter call mapped onto `MetalLayer_setProps`/`getProps` (pixel format, opaque,
  framebuffer-only, display sync, drawable size, device) and `MetalLayer_setColorSpace` (sRGB only under the
  facade); `nextDrawable` (slot 67), `CAMetalDrawable::texture` (slot 66), `presentDrawable` and
  `presentDrawable_afterMinimumDuration` (47, 48). A drawable's presented handler runs from its present command
  buffer's completion (the stand-in for `presentedTime`, which reads as that time).
- Produces: `MTLFXSpatialScaler` over slots 68 (support), `MTLDevice_newSpatialScaler` and
  `MTLCommandBuffer_encodeSpatialScale`; `colorProcessingMode` Perceptual only (HDR is left out under the facade).

- [ ] Step 1: write `tests/present.rs`: a view from a stand-in HWND, ten drawables acquired, cleared and presented
  with a minimum duration, each presented handler run once.
- [ ] Step 2: run it: FAIL.
- [ ] Step 3: implement.
- [ ] Step 4: `cargo +1.99.0 test -p mtld3d-mtl` passes.
- [ ] Step 5: commit (DXMT 0104).

### Task 5: mtld3d-unix on the facade

**Files:**
- Modify (DXMT tree, mtld3d): `unix/unix/Cargo.toml` (feature `winemetal` = `dep:mtld3d-mtl`; objc2, block2,
  objc2-app-kit and friends stay macOS deps), `unix/unix/src/lib.rs` and a new `unix/unix/src/mtl.rs` (the switch:
  `pub use objc2_metal`, ... or `pub use mtld3d_mtl::objc2_metal`, ...), every `use objc2*`/`use block2` in the Metal
  layer to `crate::mtl::...`, `cfg(not(feature = "winemetal"))` on `metal/macdrv*` (but the attach entry points),
  `hud_state`, `main_thread_checker`, the cursor overlay, the screen-parameter filter and the App Nap activity, with
  facade forms of `attach_metal_layer`/`detach_metal_layer` (`CreateMetalViewFromHWND`/`ReleaseMetalView`) and no-op
  forms of the rest; `metal/command.rs` `blit_texture_to_buffer` through a staging buffer under the feature;
  `shared/` and `windows/core/src/page_box.rs`: a page-allocation hook the device sets when the Unix side asks for
  it (a new `DeviceInfo` flag and two thunks, `AllocPages`/`FreePages`, served by mmap under the macOS facade and by
  winemetal's `WMTMemoryAlloc`/`Free` in M2b).
- Modify (repo): `dxmt/mtld3d-build.sh` (`MTLD3D_UNIX=winemetal` builds the loopback mtld3d.so into
  `unix/target-wm`), `dxmt/d3d9-check.sh` (`MTLD3D_SO=<path>` swaps the tool folder's `mtld3d.so` for that one in its
  scratch copy)

**Interfaces:**
- Consumes: Tasks 2-4's modules under objc2's names.
- Produces: `$D/mtld3d/unix/target-wm/aarch64-apple-darwin/production/mtld3d.so`, the loopback build.

- [ ] Step 1: RED: the loopback build doesn't compile yet (`MTLD3D_UNIX=winemetal sh dxmt/mtld3d-build.sh ...`
  fails); record the first errors.
- [ ] Step 2: implement until it builds; mtld3d's own unit tests pass in both builds (`cargo test` in `unix/` with and
  without `--features mtld3d-unix/winemetal`, the M1 ruling's `--lib --bins --tests`).
- [ ] Step 3: `MTLD3D_SO=<loopback mtld3d.so> OLD=<pre-mtld3d wine.app> sh dxmt/d3d9-check.sh`: eight `ok`s.
- [ ] Step 4: the product build's `dxmt/d3d9-check.sh` still passes.
- [ ] Step 5: commit (DXMT 0105, repo scripts).

### Task 6: the gates

**Files:**
- Modify: `dxmt/mtld3d-conformance.sh` (`MTLD3D_SO=<path>` runs on a scratch copy of wine.app with that `mtld3d.so`)

- [ ] Step 1: conformance on the loopback build. Expected: every subtest's failures and crashes equal to the product
  build's run (M1: stateblock 0, visual 31, device 740, d3d9ex 74-76); each difference named in the ledger with its
  cause, fixed when the facade is wrong.
- [ ] Step 2: the product build's gates unchanged: `dxmt/d3d9-check.sh` (with OLD), `dxmt/mtld3d-conformance.sh`,
  `make dxmt-check`, `make test`.
- [ ] Step 3: commit.
