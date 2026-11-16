# DXMTRS C4b-3d5: multisampling and the default-pool plain surface on D3D9's core runtime; the Wine sweep

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, a device is created with a multisample type (back buffer and auto
depth-stencil), `CreateRenderTarget` and `CreateDepthStencilSurface` make multisampled surfaces, draws and clears
reach them with `D3DRS_MULTISAMPLEMASK` and alpha-to-coverage, a multisampled target is read through
`StretchRect`'s resolve, and `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` answers. Same pixels as under mtld3d,
which keeps its behaviour. Wine's `visual`, `device` and `d3d9ex` are run once under core and what stops them is
written down for C4c.

**Architecture:** last of C4b-3d's five slices (3d1 targets, 3d2 depth-stencil, 3d3 clears/scissor/sRGB/blending,
3d4 index 1-3, done; **3d5**). dxmt-core already makes multisampled textures, attaches them, clears them (pass and
quad), resolves colour (`Encoder::resolve_texture`, folded into the previous pass by its optimizer) and keys its
tile barrier on a sample count (M §A): this slice is planned to change none of it, and Task 3's tests prove that
before anything leans on it. Five DXMTRS commits, each prefix pushable, the multisample refusals lifted by the
last:

1. **The plain surface** (Task 2). `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` takes mtld3d's own arm (an
   internal usage-0 texture behind a surface): created, described, locked. What would use it further
   (`StretchRect`, `ColorFill`, `UpdateSurface`, `GetDC`) stays refused (C4c).
2. **Fakes** (Task 3). `dxmt-fakes` prints a pass's resolve targets and a pipeline's raster count and
   alpha-to-coverage, and Task 3 pins on them what dxmt-core does with a `TYPE2D_MULTISAMPLE` view.
3. **The mechanism** (Task 4). A core texture has a sample count; a multisampled one is a `TYPE2D_MULTISAMPLE`
   render target whose views are of that type. The key names the pass, as in C4b-3d4, and now names its sample
   count and alpha-to-coverage too: d3d9-rt checks the count against target 0's before any record, an extra whose
   count differs from target 0's is not in the pass set, and a depth-stencil whose count differs is left out of
   the draw's pass (mtld3d's rules, M §B.2). The pixel variant takes the sample mask and, under alpha-to-coverage
   on a multisampled target, no alpha test. Nothing multisampled can be created yet.
4. **Resolve** (Task 5). d3d9-rt resolves a multisampled target into a single-sample one of its size and format,
   through the view format the last pass or Clear wrote it in (stored, or the sRGB twin's), as mtld3d resolves. A
   multisampled back buffer is a multisampled texture beside the single-sample one, which Present resolves into
   and presents (mtld3d's twin; the presenter stays at one sample).
5. **Creation** (Task 6). `CreateDevice` with a multisample type, multisampled `CreateRenderTarget` and
   `CreateDepthStencilSurface`, with mtld3d's answers; `GetRenderTargetData` refuses a multisampled source as
   mtld3d does; `StretchRect` takes its multisample-resolve shape alone (ruling 1). The refusals go.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-fakes, mtld3d-core, d3d9-rt, the d3d9.dll front end), C++
(`d3d9_triangle`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line and 3d1-3d4's paragraphs).
Maps: `build/c4b3d/map-3d5.md` (this slice; cited as M §n and its hazards as F.n), `build/c4b3d/map-3d4.md`,
`build/c4b3d/map.md`. Reviews: `build/c4b3d/s5/review-{1-design,2-core,3-exec}.md` (folded; dispositions at the
end, cited as R1-R3). Ledger: `~/Documents/MacProton/.superpowers/sdd/2026-11-16-dxmtrs-c4b3d5-msaa/progress.md`.

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check,
  a crossing inside a `DeviceInner` method guarded inside that method; a core-only struct field or private helper
  is cfg'd in the struct, in every literal and at its definition. No mtld3d code moves; mtld3d's own
  `resolve_sample_count`, `effective_sample_mask`, `alpha_to_coverage_requested` and `PipelineRsBits::
  alpha_to_coverage` are called, not restated.
- **dxmt-core (`src/dxmt/src`), d3d11 and winemetal do not change**, and Task 3 proves the three things this slice
  asks of them. Should a task find that one must change, it stops: the change is a ruling, a commit of its own
  with its unit test before the task's, and the VM's `vm/dxmt/gate.sh` then joins Task 7 (the x64
  `dxmt/check.sh` runs there anyway). `src/dxmt/fakes` (test support) gains printed lines.
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code;
  a commit's body follows CONTRIBUTING §Changes. A d3d9-rt or mtld3d-core API change carries its front-end call
  sites and every literal of a changed struct in the same commit (the compiler lists them; M §C.3 and map-3d4
  §C.2 name the `PipelineKey`, `CoreTextureDesc` and `Shape` literals). The PE front end compiles only inside
  rust-check: grep the import list of an edited front-end file first. A new Clone/Copy derive needs `sh
  tools/audit.sh --update-derives` before rust-check.
- No new dependency. No `HashMap`. No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body;
  lines of 100 columns at most; `#[must_use]` on a new value-returning pub fn, `const fn` where it can be. A
  struct holds at most one `bool` (CONVENTIONS "Booleans pack"; `CoreTextureDesc` already holds two and gains
  none), a function at most three.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`, base 5f9e5caa) and `build/c4b3c/mn` (branch
  `c4b3d-mn`, base 129c271), cargo target `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s5/gates`
  (state names `c4b3d5-*`), logs and reviews in `build/c4b3d/s5`. Builds and GPU jobs under the pipeline lock
  (`/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`). No make in a worktree. A job tests
  **committed** tips (`DXTIP`/`MNTIP`); a job script that has run is never edited; each run of the per-task lane
  takes its own folder.
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; crate tests as `tools/rust-check.sh`'s test leg
  runs them; mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x380-0x3bf** here, one a test.
- Every DXMTRS commit is safe to run under core: until Task 6 no multisampled texture can exist (the creation
  gates stand), so Tasks 3-5 move no row and their lanes read exactly as Task 2's. Between Task 1's rows and Task
  6, MacNeutron's core lanes are red by design on the `msaa` launch (no device: every row "no line"), on
  `targets-rt-msaa`, `depth-ds-msaa`, `targets-runtime` and `depth-runtime` (a refusal line more than the sets
  name), and until Task 2 on `targets-rt-plain`. Should the slice stop early, the rows and sets of the tasks not
  done go back to their refusal forms in the commit that pins.
- Per task one lane (the Mac's `d3d9-check` on default and core); full gates once, in Task 7. Benches and probes
  run once, on an idle Mac. No gate, build or VM job while the maintainer is gaming. Autonomous work stops at 95%
  weekly usage.

## Scope

In: `CreateDevice` with `MultiSampleType` 2, 4 or 8 samples or `NONMASKABLE` with a quality (windowed; back buffer
and auto depth-stencil); multisampled `CreateRenderTarget` and `CreateDepthStencilSurface`; `GetDesc` of them;
draws, whole and rect clears, the scissor, blending, sRGB write and index 1-3 targets on multisampled targets;
`D3DRS_MULTISAMPLEMASK`; alpha-to-coverage (`D3DRS_ADAPTIVETESS_Y == ATOC` with the alpha test on; the `A2M1` /
`A2M0` point-size latch); a depth-stencil or an extra target whose sample count differs from target 0's;
`StretchRect` from a multisampled render target into a single-sample render target of the same size and format,
whole surface; Present of a multisampled back buffer; `GetRenderTargetData` of a multisampled source
(`D3DERR_INVALIDCALL`); `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)`, its `GetDesc` and its lock; the sweep.

Out (owners): every other `StretchRect` (stretching, a rect, a format change, depth, a plain surface, a
multisampled destination), `ColorFill`, `UpdateSurface`, `GetDC`, `Reset` and with it a changed multisample type,
fullscreen, RESZ and any depth resolve, `GetFrontBufferData`, the `Ex` creators (C4c); fixing what the sweep
finds (C4c); per-draw dedup (C4d); `D3DRS_MULTISAMPLEANTIALIAS` (a no-op under both runtimes).

## Review Focus

1. **A pass, its pipeline and its tile barrier agree on the sample count, always** (a mismatch is undefined
   behaviour in Metal, like a format mismatch). The key names the count; d3d9-rt refuses a key whose count is not
   target 0's (`DrawRefused::Target`); every attachment of a pass has that count: an extra of another count is
   out of the set, a depth-stencil of another count is not attached and the key then names no depth format, no
   depth state is set and the pre-transformed clamp is on (mtld3d's clamp reads the pass's planes: unix
   `draw.rs:998`). rt `the_key_names_the_sample_count`, `an_extra_of_another_count_is_not_in_the_set`,
   `a_depth_of_another_count_is_left_out`; rows `ms-mrt`, `ms-mismatch`; every core launch under Metal's
   validation layer with no report.
2. **A resolve reads what was last written, in the space it was written in.** The view format of the last pass or
   Clear on the multisampled target picks both views of the resolve; a resolve after more draws sees them; the
   destination's open pass ends first and its owed CPU writes are flushed. rt `a_resolve_takes_the_last_view`,
   `a_resolve_ends_the_destinations_pass`; rows `ms-srgbmask`, `ms-redraw`.
3. **Coverage follows the current target, not the device.** The sample mask is narrowed against target 0's count
   and type (a non-maskable target ignores it; a single-sample one too); alpha-to-coverage replaces the alpha test
   only on a multisampled target. Rows `ms-mask`, `ms-nonmask`, `ms-a2c`.
4. **mtld3d's answers at creation and at the read paths**: `D3DERR_INVALIDCALL` for an invalid or unavailable
   multisample type at `CreateDevice`, for a lockable multisampled target, for `GetRenderTargetData` of a
   multisampled source, and mtld3d's shared `StretchRect` validation before the core arm. Row `ms-desc`.
5. **The default runtime is untouched and single-sample draws pay nothing visible**: the count is 1 and the
   coverage inputs are inert on every existing path. The probe; the plain build.

---

### Task 1: rows (MacNeutron) and the gate scripts

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s5/gates/*.sh` (scratch).

- [ ] **Step 0: the gate scripts**, copied from `build/c4b3d/s4/gates` (`s4`→`s5`, `c4b3d4-`→`c4b3d5-`,
  `C4b-3d4`→`C4b-3d5`): `head.sh`, `ja.sh`, `jt.sh`, `jq.sh` (its validation-layer list gains `msaa`), `jr.sh`,
  `jc.sh` (the builder's start line already names `build/p/eac-vm-work/images`), `jcr.sh` (the same correction),
  `jp.sh`, `post.sh`; `jm.sh`, `chainA.sh`, `chainB.sh` and the sweep job `jw.sh` are written at Task 7.
- [ ] **Step 1: write.** `main`: a flag `msaaMode` (`argv[1] == "msaa"`), whose device has `MultiSampleType =
  D3DMULTISAMPLE_4_SAMPLES` and the auto D24S8; `msaaRows` with the blend kind's prologue and runner, budget 180
  s, the 16 rows of the appendix. Helpers: `msRt(Com<IDirect3DSurface9> &, size, format, type, quality = 0)`
  (`CreateRenderTarget`), `resolved(src, size, format, probes)` (a new single-sample render-target texture of
  that size and format, `StretchRect(src, nullptr, level 0, nullptr, D3DTEXF_NONE)`, then `rtHas` on it),
  `inBand(what, got, lo, hi)` for a channel that coverage decides, `msStart()` (the mrt kind's `mrtStart` plus
  `D3DRS_MULTISAMPLEMASK 0xffffffff`, `D3DRS_ADAPTIVETESS_Y 0`, `D3DRS_POINTSIZE` back to 1.0 after an `A2M0`,
  `ALPHATESTENABLE FALSE`). No row uses `frames()` (it reads the back buffer with `GetRenderTargetData`, which
  refuses a multisampled one): a row draws inside `until`, resolves and reads. `targets` kind: `rt-msaa` becomes a
  result (a 4-sample target is made, described, cleared, resolved and read; skipped as ok when
  `CheckDeviceMultiSampleType` refuses 4 samples) and `rt-plain` is new (appendix). `depth` kind: `ds-msaa`
  becomes a result (appendix). Headers and counts.
- [ ] **Step 2: scripts.** `d3d9-check.sh`: `targets_refusals` goes (the targets launch joins the zero-refusal
  arm `blend|mrt|targets|msaa`), `depth_refusals` loses its multisampled line (two left), `rt-plain` joins the
  targets rows, `launch ours fresh msaa 240 msaa $strict` and `check msaa "<rows>"` after `check mrt`, the header
  and `refusals_ok`'s comment. `d3d9-gate.sh`: the same sets and comments, `rt-plain`, `for r in ms64-fresh
  ms32-fresh` row loops, the core block (targets and msaa with no kind: zero refusals). `d3d9.sh`: two `tri`
  lines (`ms64-fresh`, `ms32-fresh`, 240 s, `msaa $strict`) after `mr32-fresh`, the header.
- [ ] **Step 3: compile** with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall -Wextra`) — no
  diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle msaa rows in every D3D9 lane (DXMTRS C4b-3d5)`.
- [ ] **Step 5: the controls** (`ja`: default, msl, core on DXMTRS 5f9e5caa). Expected: default and msl ok with
  every new row; core red as the Global Constraints list, nothing else. The rule for a row red on the default
  runtime (C4b-3d4's): an expectation derived from D3D9's formula that mtld3d misses is a defect of mtld3d
  (ledgered; the row keeps D3D9's value, red-on-default until its fix); one read off mtld3d's code ("parity" in
  the appendix) or a hardware band that mtld3d's run falls outside is a wrong row, corrected in the appendix and
  the row.
- [ ] **Step 6: the VM's default lanes once** (`jcr`): `ms64-fresh`, `ms32-fresh` and the changed rows green
  inside their budget; if the VM's GPU path offers no 4-sample target the kind's rows are skipped as ok there by
  their own `CheckDeviceMultiSampleType` test, and that is ledgered.

### Task 2: `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` (front end)

**Files:** Modify `src/d3d9/src/device.rs` (`device_create_offscreen_plain_surface`); whatever the arm's path
needs a core guard on (found in Step 1).

- [ ] **Step 1: read the arm end to end under core** (`create_texture_path(offscreen_plain: true)`,
  `new_owned_texture_backed`, the surface's `LockRect`/`UnlockRect` through its parent texture, the unlock's
  dirty note, `finalize_surface`): every `push_control` or encoder op on the way has a core guard or is not
  reached (a core device has no encoder: M §B.4 says `push_texture_warmups` returns early and the lock gate
  passes a usage-0 texture level). An unguarded one is guarded as its neighbours are.
- [ ] **Step 2: implement.** The `refuse_under_core!` at the default-pool arm goes. `StretchRect`, `ColorFill`,
  `UpdateSurface`, `GetDC` and the `Ex` creator keep their refusals.
- [ ] **Step 3: run** `rust-check`; **commit** `d3d9: CreateOffscreenPlainSurface in the default pool on the core
  runtime`.
- [ ] **Step 4: lane** `JT=jt2` — both runtimes: `rt-plain` ok; core otherwise as the control.

### Task 3: dxmt-fakes, and what dxmt-core does with a multisampled view

**Files:** Modify `src/dxmt/fakes/src/lib.rs`; add tests to `src/d3d9/rt/src/device/target/tests.rs` (they drive
dxmt-core through the fakes, as that file's do).

- [ ] **Step 1: the fakes.** A pass: `  resolve{i} #{}` for each colour with a resolve texture, and `  samples={}`
  when `default_raster_sample_count` is not 0. A pipeline: `  pso samples={} a2c={}` when the raster count is not
  1 or alpha-to-coverage is on. Existing lines keep their text.
- [ ] **Step 2: three tests through dxmt-core's public API**, on textures made as Task 4 will make them (a
  `Texture::new` with `info_bits(TYPE2D_MULTISAMPLE, 1, 4, RENDER_TARGET | PIXEL_FORMAT_VIEW)`):
  `dxmt_core_attaches_a_multisampled_view` (a pass on its `TYPE2D_MULTISAMPLE` view, a draw-less end: the
  texture's bits say 4 samples, the pass is encoded), `dxmt_core_resolves_into_a_level_view` (`resolve_texture`
  from the multisampled view into a `TYPE2D` view of level 1 of a 2-level texture, and into a cube face's: a pass
  with `color0 .. store=3` and `resolve0 #<that view>`; after a render pass on the multisampled view it is folded:
  one pass, `store=3`), `dxmt_core_resolves_through_a_twin` (both views re-keyed to the sRGB format: the resolve
  line names the twin's view). Expected: green with no dxmt-core change. A red one is the stop of the Global
  Constraints.
- [ ] **Step 3: run** dxmt-core's and d3d9-rt's tests, `rust-check`; **commit** `dxmt-fakes: a pass's resolve
  targets and sample count, a pipeline's raster count; multisampled views through the core, pinned`.

### Task 4: the sample count in d3d9-rt and the key (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ tests); `src/d3d9/rt/src/device/textures.rs`,
`target.rs`, `depth.rs`, `recorder.rs`, `draw.rs`, `fixture.rs` and tests; `src/d3d9/src/device/core_draw.rs`;
every `CoreTextureDesc` literal (front end `device.rs`, `direct3d9.rs`, `texture.rs`; rt tests).

**Interfaces (produces):**
- d3d9-rt: `CoreTextureDesc { .., pub samples: u8 }` (1, or 2/4/8 for a multisampled `Tex2d` render target of one
  level: `TYPE2D_MULTISAMPLE`, `info_bits(.., samples, ..)`; `texture_create` debug-asserts that shape);
  `Shape { .., samples: u8 }` (the back buffer's is 1 until Task 5); a multisampled target's and depth-stencil's
  views are `TYPE2D_MULTISAMPLE` (`texture_target`, `resolve_depth`); `#[must_use] pub fn target_samples(&self) ->
  u8` (target 0's, 1 when it does not resolve) and `depth_samples(&self) -> Option<u8>`; `pass_set` keeps an extra
  only when its count is target 0's too; `open_pass` sets `tile_barrier_pso_key[8]` to the count;
  `build_pipeline` sets `raster_sample_count` and `alpha_to_coverage_enabled` from the source; `draw_up` answers
  `Err(DrawRefused::Target)` when `key.samples != target_samples()` (in the check, so an open pass on the same
  colours and count skips it) and treats a key depth format as today (the front end names none for a
  depth-stencil of another count).
- mtld3d-core: `PipelineKey { .., pub samples: u8, pub alpha_to_coverage: bool }` and the same on
  `PipelineSource`; `core_key_depth(bound, samples_bound, uses_plane, same_samples: bool)`: a bound depth-stencil
  whose count is not target 0's gives format 0 before anything else (mtld3d's `pass_binds_depth`), no refusal;
  `#[must_use] pub fn core_coverage_variant(variant: VariantKey, sample_mask: u8, alpha_to_coverage: bool) ->
  VariantKey`: a mask other than `SAMPLE_MASK_ALL` sets `VariantFlags::SAMPLE_MASK` and the byte (mtld3d's draw,
  unix `draw.rs:583-589`); `alpha_to_coverage` zeroes `alpha_func` (`draw.rs:529-540`); `core_ps_variant` is
  unchanged (it still clears the mask, which this then sets).
- Front end (`core_draw.rs`): `samples = core.target_samples()`; the bound target 0 surface's `multi_sample()`
  type for `effective_sample_mask(rs[D3DRS_MULTISAMPLEMASK], samples, type)`; `a2c =
  alpha_to_coverage_requested(&rs, dev.a2m_enabled()) && samples > 1`; the variant through
  `core_coverage_variant`; `key.samples`, `key.alpha_to_coverage`; `key_depth` passes `same_samples =
  core.depth_samples() == Some(samples)`, and the clamp's "depth bound" input is false for a depth-stencil of
  another count (Review Focus 1).

- [ ] **Step 1: failing tests.** mtld3d-core: `core_coverage_variant_rules` (the all mask leaves the variant
  alone; 0x3 sets flag and byte; alpha-to-coverage zeroes `alpha_func` and leaves the mask), `core_key_depth`'s
  table grows by the count column, the key table by `samples` and `alpha_to_coverage` (`[Case; 21]`). rt (a
  4-sample 64x64 target made by a fixture helper `ms_desc(format, 64, samples)`):
  `a_multisampled_texture_is_made_so` (the fake's `bits` hold 4 samples and the multisample type; zeroed by a
  pass), `the_key_names_the_sample_count` (target 0 4-sample: a key with `samples: 4` draws, `pso samples=4`, the
  barrier `tile samples=4` when a texture is sampled; a key with 1 is `Err(Target)`; on a single-sample target a
  key with 4 is `Err(Target)`), `an_extra_of_another_count_is_not_in_the_set` (4 beside 1 and 1 beside 4:
  `target_desc` answers 0 for the slot; equal counts: in), `a_depth_of_another_count_is_left_out` (a 4-sample
  depth-stencil with a single-sample target and the reverse: a key naming the depth format is
  `Err(DrawRefused::Depth)`, one naming none draws with no `depth` line; equal counts attach),
  `alpha_to_coverage_is_the_pipelines` (`pso samples=4 a2c=1`; two keys differing in it build two pipelines),
  `a_clear_of_a_multisampled_target` (whole: folds into the pass; rects: the quad clear's pipeline says 4
  samples). Expected: do not compile.
- [ ] **Step 2: implement.** `draw_up`'s depth check gains the count: a key naming a depth format while
  `depth_samples() != Some(target_samples())` is `Err(DrawRefused::Depth)`.
- [ ] **Step 3: run** `audit.sh --update-derives` if a derive moved, the crates' tests, `rust-check`.
- [ ] **Step 4: commit** `d3d9: a sample count in the core runtime's textures, passes and pipelines`.
- [ ] **Step 5: lane** `JT=jt4` — both runtimes exactly as Task 2's lane.

### Task 5: resolve, and the multisampled back buffer (d3d9-rt)

**Files:** Modify `src/d3d9/rt/src/device.rs`, `device/target.rs`, `recorder.rs`, `swapchain.rs`, `textures.rs`
and tests; `src/d3d9/src/direct3d9.rs` (`attach`'s call site).

**Interfaces (produces):**
- d3d9-rt: each multisampled colour target remembers the view format it was last written in (`Stored` and the
  back buffer's companion: a `u32`, the stored format at creation; set by `open_pass` for each attached slot and
  by `clear`/`clear_rects`);
  `#[must_use] pub fn resolve(&mut self, source: TargetRef, destination: TargetRef) -> bool`: false, and nothing
  recorded, unless the source is multisampled, the destination single-sample, their sizes equal and their stored
  formats equal; else `close_pass()`, and `enc.resolve_texture` from the source's `TYPE2D_MULTISAMPLE` view to the
  destination's view, both in the source's last-written format (a format the destination has no view for, a twin
  it lacks, falls back to the stored format on both sides);
  `attach(hwnd, width, height, samples: u8)`: with `samples > 1` the back buffer is the single-sample texture as
  today plus a multisampled companion (BGRA8, `PIXEL_FORMAT_VIEW`, zeroed); `TargetRef::BackBuffer` then resolves
  to the companion for passes, clears and `resolve`'s source (its `Shape::samples` is the count), and `present`
  resolves the companion into the single-sample texture first, in its last-written format, then presents that as
  today (the presenter stays at one sample). `read_back` of the back buffer is unchanged (the front end refuses a
  multisampled source in Task 6).
- [ ] **Step 1: failing tests.** rt: `a_resolve_takes_the_last_view` (a draw through the twin, then `resolve`:
  `resolve0` names the destination's twin view, and the pass before it took the fold, `store=3`; after a Clear
  with sRGB write off, the stored views), `a_resolve_ends_the_destinations_pass` (the destination is the open
  pass's target: its pass ends before the resolve; a draw after it opens a new one),
  `a_resolve_refuses_what_it_cannot_do` (single-sample source, multisampled destination, another size, another
  format: false, nothing recorded), `a_multisampled_back_buffer_presents_resolved` (`attach(.., 4)`: a draw's
  pass names the companion; `present`: a resolve into the single-sample texture, then the present of it; a
  second frame the same), `a_single_sample_back_buffer_is_as_before` (no companion, no resolve). Expected: do not
  compile.
- [ ] **Step 2: implement; run** tests and `rust-check`.
- [ ] **Step 3: commit** `d3d9: multisample resolve and a multisampled back buffer in the core runtime's device`.
- [ ] **Step 4: lane** `JT=jt5` — as Task 2's lane.

### Task 6: creation, `StretchRect`'s resolve, the read-back's refusal (front end)

**Files:** Modify `src/d3d9/src/direct3d9.rs` (`create_core_device`), `device.rs` (`core_color_target`,
`core_depth_surface`, the two creators' core arms, `device_stretch_rect`), `device/core_arms.rs`
(`get_render_target_data`).

- [ ] **Step 1: implement.**
  - `create_core_device`: `windowed == 0` is still refused; the multisample type goes through mtld3d's
    `resolve_sample_count(type, quality, back buffer format, caps)`, any reject `D3DERR_INVALIDCALL` (mtld3d's
    answer, `direct3d9.rs:2126`); `core.attach(hwnd, w, h, samples)`; the auto depth-stencil's
    `CoreTextureDesc::samples` is the count; `DeviceCreateInfo::backbuffer_sample_count` is the count and the
    stored present parameters keep the type and quality (`GetDesc` and the mask narrowing read them).
  - `core_color_target` and `core_depth_surface`: `samples` from the spec's `SurfaceMultiSample`; both
    multisample refusals go (the shared `lockable && multisampled → D3DERR_INVALIDCALL` runs before them).
  - `get_render_target_data` (core arm): a source whose `multi_sample().sample_count > 1` is
    `D3DERR_INVALIDCALL`, where mtld3d checks it.
  - `device_stretch_rect`: the blanket refusal moves after mtld3d's shared validation (null surfaces, the filter,
    the class and pool rules: the same `INVALIDCALL` answers). The core arm then takes one shape: both surfaces
    have a `core_target()`, the source's sample count is above 1 and the destination's is 1, the rects are null
    or the whole of equal-size surfaces, and `core.resolve(source, destination)` answers true after the
    destination's parent texture, if any, was flushed (`core_flush_dirty`, as `SetRenderTarget` does); an
    AUTOGENMIPMAP destination is left for its slot's bookkeeping. Anything else, or a false answer, is the
    refusal "IDirect3DDevice9::StretchRect (other than a multisample resolve)".
- [ ] **Step 2: run** `rust-check`; **commit** `d3d9: multisampled devices and surfaces, and StretchRect's resolve,
  on the core runtime`.
- [ ] **Step 3: lane** `JT=jt6` — both runtimes `d3d9-check: ok`: the 16 `msaa` rows, `rt-msaa`, `ds-msaa`,
  `rt-plain`; the targets and msaa launches' logs with no refusal line, the depth launch's with its two.

### Task 7: gates, mutants, the sweep, the probe

- [ ] **Step 1: `jq`**: the three Mac lanes; the stateblock conformance under core equal to default; every core
  launch, `msaa` included, under Metal's validation layer with no report.
- [ ] **Step 2: mutants** (`jm`; each red on its row and on no item outside `msaa-*`; none hands Metal a pass and
  a pipeline that disagree): (a) the front end passes `SAMPLE_MASK_ALL` whatever the state → `ms-mask`; (b)
  `core_coverage_variant` keeps `alpha_func` → `ms-a2c`; (c) `resolve` always takes the stored format's views →
  `ms-srgbmask`; (d) the clamp's "depth bound" input ignores the count → `ms-mismatch`; (e) the sample mask is
  narrowed with the maskable type whatever the target's → `ms-nonmask`; (f) alpha-to-coverage reads the device's
  back-buffer count, not target 0's → `ms-a2c` (its single-sample cell); (g) `core_arms::clear` skips a bound
  slot outside the pass set → `ms-mrt` (its Clear of the other-count extra).
- [ ] **Step 3: plain build** (`jr`): translator key and imports unchanged; `d3d9-check` default; the Mac's Wine
  conformance with no new failing site; the x64 `dxmt/check.sh` all passed (idle Mac).
- [ ] **Step 4: VM** (`jc`, `JC_D3D9_ONLY=1` unless dxmt-core changed): default and core, old and new WoW64.
- [ ] **Step 5: the sweep** (`jw`, measured, not a gate): a copy of `dxmt/mtld3d-conformance.sh`'s invocation
  without the "stateblock alone" guard, `D3D9_RUNTIME=core`, `--only visual`, `--only device`, `--only d3d9ex`,
  each with `MTLD3D_CONFORMANCE_TIMEOUT_SECS=900`. Written to `build/c4b3d/s5/sweep.md`: per suite, executed
  tests and failures against the default runtime's figures, whether it ran to its end, the refusal lines in
  order of first appearance with their counts, any crash or hang with its last test. Nothing is fixed here.
- [ ] **Step 6: the probe** (`jp`, once, idle Mac, load average under 3 at its start or it waits): this tip on its
  own build, tex and texdepth, mtld3d and core, into `build/c4b3d/s5/probe.md` beside C4b-3d3's quiet figures
  (core 2.18 / 2.25). A core figure more than 0.32 above them gets C4b-3d4's retake rule (the older tip in the
  same window).

### Task 8: review, spec, pin, push

- [ ] Whole-branch review (Opus; the Review Focus verbatim; the ledger's rulings); one fix pass; minors fixed
  when they are a line, else ledgered.
- [ ] Spec §5: a C4b-3d5 paragraph; C4b-3d as a whole marked done; the sweep's headline for C4c. Execution notes
  here. `dxmt/pins`.
- [ ] Privacy scan; fetch-check both origins; push DXMTRS then MacNeutron; post-push check from main (plain build,
  tests, `d3d9-check` default and msl, x64 check); CI; scratch deleted by full path; memory.

## Rules check

- **No dxmt-core, d3d11 or winemetal change** (Task 3 pins the three behaviours relied on). `dxmt-fakes` prints
  more.
- **mtld3d's rules are called**: `resolve_sample_count`, `effective_sample_mask`, `alpha_to_coverage_requested`.
  Restated in d3d9-rt, which has no `PassState`, each one comparison and each pinned: the extra's count test
  (`recompute_extra_present_mask`), the depth-stencil's (`pass_binds_depth`), and "resolve through the view the
  pass wrote" (`color_resolve_view`).
- New fields: `CoreTextureDesc::samples`, `Shape::samples`, `PipelineKey::{samples, alpha_to_coverage}` and
  `PipelineSource`'s (one `bool` on each: allowed), the last-written format of a multisampled target, the back
  buffer's companion. New functions: `core_coverage_variant`, `target_samples`, `depth_samples`, `resolve`. A
  fourth parameter of `core_key_depth` (its third `bool`). No new type, file, static, config key, wire field,
  dependency or lint suppression.
- New refusal literal "IDirect3DDevice9::StretchRect (other than a multisample resolve)" replaces
  "IDirect3DDevice9::StretchRect"; three literals go (the plain surface's, the two multisampled creators').

## Rulings (planning)

1. **`StretchRect`'s multisample-resolve shape is built here** although the program's split puts `StretchRect` in
   C4c. Why: under core nothing else can read a multisampled target (`GetRenderTargetData` refuses one by D3D9's
   rule; no sampling, no `GetFrontBufferData`), so no row could see a multisampled pixel. Cost if wrong: an arm
   C4c widens or rewrites; every other shape stays refused.
2. **A multisampled colour surface is one multisampled texture; only the back buffer has a single-sample twin.**
   mtld3d gives every multisampled target a twin and resolves at each submit. Why: under core the only readers
   are `StretchRect` (which resolves into its destination) and Present. Cost if wrong: C4c's general
   `StretchRect` from a multisampled source needs a scratch resolve target, or the twin then.
3. **The resolve goes through the last-written view format**, as mtld3d's. Cost if wrong: the edge and mask
   averages of an sRGB-written target differ from mtld3d's (`ms-srgbmask` pins the mask case).
4. **A depth-stencil or extra of another sample count than target 0 is left out, not refused**, as mtld3d. Cost
   if wrong: none against mtld3d.
5. **`CreateDevice`'s multisample reject is `D3DERR_INVALIDCALL`** under core now (it was `NOTAVAILABLE` with the
   whole feature). Fullscreen stays `NOTAVAILABLE`.
6. **Coverage-dependent pixels are asserted as bands** (an edge, alpha-to-coverage's dither): they are the
   hardware's, neither D3D9's formula nor mtld3d's code. Exact values are asserted for interiors, clears and
   full-quad sample-mask fractions. Cost if wrong: a coverage defect inside the band passes.
7. **The Present-time resolve has no row** (no call reads the presented image under core): rt
   `a_multisampled_back_buffer_presents_resolved` is its proof, and the eye its check.
8. **The sweep is measured, not gated, and nothing it finds is fixed here** (C4c's input).
9. **Rows are controls first**, on the Mac (default, msl) and in the VM.
10. **The VM's D3D10-12 gate is not rerun unless dxmt-core changes** (C4b3a-7's rule).

## Stated gaps

- No row reads a presented multisampled frame (ruling 7), a multisampled depth-stencil's resolved depth (C4c), or
  a `StretchRect` shape other than the whole-surface resolve.
- The plain surface's content has no reader under core until C4c (`StretchRect`, `UpdateSurface`): `rt-plain`
  reads back what it locked, no more.
- Sample-rate shading, centroid interpolation and `vPos` under multisampling: no row under either runtime.
- A `Reset` to another multisample type, the `Ex` creators with a multisample type: refused whole (C4c).

## Appendix: the rows

The `msaa` device: back buffer 256x256 X8R8G8B8 with 4 samples, auto D24S8 with 4 samples. "T" is a 64x64
A8R8G8B8 `CreateRenderTarget` surface of 4 samples unless said; "read" is `resolved()`: `StretchRect` of the whole
surface into a new single-sample target, then `GetRenderTargetData`. Quads are pre-transformed and pixel-aligned
unless said, so their interiors and edges are whole pixels. "band" is a channel between 25% and 75%.

**targets kind.** `rt-msaa`: a 4-sample 64x64 target: `D3D_OK`, `GetDesc` says 4 samples; cleared green with a
red quad over its left half, read: (16,32) red, (48,32) green; `GetRenderTargetData` of it is
`D3DERR_INVALIDCALL`; a lockable 4-sample target is `D3DERR_INVALIDCALL`. `rt-plain`:
`CreateOffscreenPlainSurface(64, 64, A8R8G8B8, D3DPOOL_DEFAULT)` is `D3D_OK`; `GetDesc`: default pool, usage 0,
no multisampling; a texel written under `LockRect` is read back under a second lock; `SetRenderTarget(0, it)` is
`D3DERR_INVALIDCALL`.

**depth kind.** `ds-msaa`: a 4-sample 256x256 D24S8 surface: `D3D_OK`, `GetDesc` says 4 samples; bound with the
single-sample back buffer and `ZENABLE` on, a near quad then a far quad over it: the far one shows (the
depth-stencil of another count is left out: parity), every call `D3D_OK`.

**msaa (16).**
- `ms-desc`: the back buffer and the auto depth-stencil say 4 samples; `GetRenderTargetData` of the back buffer
  is `D3DERR_INVALIDCALL`; `CreateRenderTarget` with 16 samples and `CreateDepthStencilSurface` with 16 answer
  what `CheckDeviceMultiSampleType` does for an unavailable type under mtld3d (parity); a `StretchRect` with a
  null source is `D3DERR_INVALIDCALL`.
- `ms-resolve`: T cleared green, a red quad over x < 32: read (16,32) red, (48,32) green.
- `ms-edge`: T cleared black, a white clip-space triangle whose hypotenuse crosses the target: a texel well
  inside white, one well outside black, and on the hypotenuse's row at least one texel in the band.
- `ms-back`: the back buffer cleared blue, a red quad (64,64)-(192,192); `Present` is `D3D_OK`; drawn again and
  read through a 256 target before the next Present: (128,128) red, (32,32) blue.
- `ms-clear`: T: a whole Clear to red; `Clear(1, (8,8)-(24,24))` to green; a viewport-bounded Clear to blue:
  exact at each region's inner and outer edge texels.
- `ms-depth`: T 256x256 with the 4-sample auto depth-stencil, Z cleared; a near red quad over the left half, a
  far white quad over all, LESSEQUAL: (64,128) red, (192,128) white; a rect Z Clear to 0.1 hides a later quad
  inside it.
- `ms-mismatch`: T (4 samples) with a single-sample 64x64 D24S8 surface bound, `ZENABLE` on: near then far: the
  far quad shows; a pre-transformed quad at z 1.5 is drawn (no depth plane in the pass: the clamp is on; parity);
  the Z Clear answers `D3D_OK`.
- `ms-mask`: T cleared black, `D3DRS_MULTISAMPLEMASK` 0x3, a white quad over all: 128 in each channel; 0x1: 64;
  0xf: 255; the same 0x3 through a ps_2_0 writing white: 128; on a single-sample target 0x3 leaves it white.
- `ms-nonmask`: `CreateRenderTarget(D3DMULTISAMPLE_NONMASKABLE, quality 2)`: `D3D_OK`, `GetDesc` says
  non-maskable, quality 2; the mask 0x3 and a white quad: 255 (the mask does not apply).
- `ms-a2c`: `D3DRS_ADAPTIVETESS_Y` = ATOC, `ALPHATESTENABLE` on, `ALPHAREF` 0xc0, `ALPHAFUNC` GREATEREQUAL, a
  white quad of alpha 0x80 over black: on T every texel read is in the band or its 4x4 mean is (the alpha test
  did not kill it, coverage thinned it); on a single-sample target the same quad is not drawn (black); with ATOC
  off and `D3DRS_POINTSIZE` = A2M1 and the alpha test off: the band on T; after A2M0: white.
- `ms-srgbmask`: T, `SRGBWRITEENABLE` on, cleared black, the mask 0x3, a white quad: 188 in each channel (the
  resolve averages in linear space through the twin's views: parity); with sRGB write off: 128.
- `ms-blend`: T cleared red, SRCALPHA/INVSRCALPHA, a green quad of alpha 0x80: (128,128,0) inside.
- `ms-mrt`: T at index 0 and a second 4-sample target at index 1, `PS_MRT2` red, green: each read its own. With a
  single-sample 64x64 texture at index 1: the draw leaves it (its Clear colour), a whole Clear reaches it. With a
  2-sample target at index 1 (when the device offers 2): the same.
- `ms-scissor`: T cleared blue; scissor (16,16)-(48,48), a red quad over all: (32,32) red, (15,32) and (48,32)
  blue; a viewport (0,0,32,64) and a full clip-space quad: (16,32) red, (48,32) blue.
- `ms-redraw`: T cleared black, a red quad over the left half, read into R1; a green quad over the right half,
  read into R2: R1 is red and black, R2 red and green; R1 is then bound at index 0 and a white quad drawn on it:
  white (the resolve's destination is an ordinary target).
- `ms-samples`: for each of 2 and 8 samples the device offers: a target of that count, the mask of its lower
  half (0x1 of 2; 0x0f of 8), a white quad over black: 128.

## Review dispositions

(filled when the three reviews are folded)
