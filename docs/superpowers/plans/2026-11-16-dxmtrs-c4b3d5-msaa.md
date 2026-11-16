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
quad), resolves colour (`Encoder::resolve_texture`) and keys its tile barrier on a sample count (M §A). One thing
in it is wrong for any front end and is fixed first (ruling 2): its optimizer folds a resolve into the source's
last render pass even when a pass in between writes or reads the resolve's destination. Six DXMTRS commits, each
prefix pushable, the multisample refusals lifted by the last:

1. **The plain surface** (Task 2). `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` takes mtld3d's own arm (an
   internal usage-0 texture behind a surface): created, described, locked. R3 traced the arm under core: no
   encoder op is reached, so the refusal's removal is the whole change. What would use the surface further
   (`StretchRect`, `ColorFill`, `UpdateSurface`, `GetDC`) stays refused (C4c).
2. **dxmt-core** (Task 3). A resolve folds only into the render pass right before it.
3. **Fakes** (Task 4). `dxmt-fakes` prints a pass's resolve targets and a pipeline's raster count and
   alpha-to-coverage, and three tests pin on them what dxmt-core does with a non-array `TYPE2D_MULTISAMPLE` view
   (D3D11 resolves through array view types: d3d9-rt is the first user of this one).
4. **The mechanism** (Task 5). A core texture has a sample count; a multisampled one is a `TYPE2D_MULTISAMPLE`
   render target whose views are of that type. The key names the pass, as in C4b-3d4, and now its sample count
   and alpha-to-coverage too: d3d9-rt checks the count against target 0's at every draw, an extra whose count
   differs from target 0's is not in the pass set, and a depth-stencil whose count differs is left out of the
   draw's pass (mtld3d's rules, M §B.2). The pixel variant takes the sample mask and, under alpha-to-coverage on
   a multisampled target, no alpha test. Nothing multisampled can be created yet.
5. **Resolve** (Task 6). d3d9-rt resolves a multisampled target into a single-sample one of its size and stored
   format, through the view format the last pass or Clear wrote the source in (stored, or the sRGB twin's), as
   mtld3d resolves. A multisampled back buffer is a multisampled texture beside the single-sample one, which
   Present resolves into and presents (mtld3d's twin; the presenter stays at one sample).
6. **Creation** (Task 7). `CreateDevice` with a multisample type, multisampled `CreateRenderTarget` and
   `CreateDepthStencilSurface`, with mtld3d's answers; `GetRenderTargetData` refuses a multisampled source as
   mtld3d does; `StretchRect` takes its multisample-resolve shape alone (ruling 1). The refusals go.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-core, dxmt-fakes, mtld3d-core, d3d9-rt, the d3d9.dll front end), C++
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
  is cfg'd in the struct, in every literal and at its definition. An arm added to an always-compiled function
  (`device_stretch_rect`) is one cfg'd block with its `use`s inside it. No mtld3d code moves; mtld3d's own
  `resolve_sample_count`, `effective_sample_mask`, `alpha_to_coverage_requested`,
  `PipelineRsBits::alpha_to_coverage` and `copies_bytes` are called, not restated.
- **One dxmt-core change, in Task 3, and only that one**: the fold's guard in `encoder/optimize.rs`, with its unit
  test. It reaches D3D10-12, so dxmt-core's and d3d11's tests, the x64 `dxmt/check.sh` and the VM's
  `vm/dxmt/gate.sh` are pre-push gates here. d3d11 and winemetal do not change. `src/dxmt/fakes` (test support)
  gains printed lines. Should a task find that dxmt-core must change again, it stops: the change is a ruling and
  a commit of its own with its unit test before the task's.
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code;
  a commit's body follows CONTRIBUTING §Changes. A d3d9-rt or mtld3d-core API change carries its front-end call
  sites and every literal of a changed struct in the same commit (the compiler lists them). The PE front end
  compiles only inside rust-check: grep the import list of an edited front-end file first. A new or removed
  Clone/Copy derive needs `sh tools/audit.sh --update-derives` before rust-check. If rust-check or a job's build
  stops at "fetching DXMT's Rust crates" (the gate runs offline), `cargo fetch --locked` in both worktrees, then
  rerun under a new job name.
- No new dependency. No `HashMap`. No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body;
  lines of 100 columns at most; `#[must_use]` on a new value-returning pub fn, `const fn` where it can be. A
  struct holds at most one `bool` (CONVENTIONS "Booleans pack"), a function at most three.
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`, base 5f9e5caa) and `build/c4b3c/mn` (branch
  `c4b3d-mn`, base 129c271), cargo target `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s5/gates`
  (state names `c4b3d5-*`), logs and reviews in `build/c4b3d/s5`. Builds and GPU jobs under the pipeline lock
  (`/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`). No make in a worktree. A job tests
  **committed** tips (`DXTIP`/`MNTIP`); a job script that has run is never edited; each run of the per-task lane
  takes its own folder. After a restart of the Mac the builder VM is started by hand with the two image shares
  (`build/vm-work/images`, `build/p/eac-vm-work/images`).
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; crate tests as `tools/rust-check.sh`'s test leg
  runs them; mingw compilers at `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x380-0x3bf** here, one a test (`/usr/bin/grep -rnE '0x3[89ab][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src`
  finds none today).
- Every DXMTRS commit is safe to run under core: until Task 7 no multisampled texture can exist (the creation
  gates stand), so Tasks 3-6 move no row and their lanes read exactly as Task 2's. Between Task 1's rows and Task
  7, MacNeutron's core lanes are red by design on: every row of the `msaa` launch ("no line": `CreateDevice` is
  refused, `main` exits) and `msaa-runtime` (no core device line); `targets-rt-msaa` and `depth-ds-msaa`;
  `targets-runtime` and `depth-runtime` (one refusal line more than the sets allow); and until Task 2
  `targets-rt-plain`. Should the slice stop before Task 7, the `msaa` kind leaves the lists of the three scripts,
  `rt-msaa` and `ds-msaa` go back to their refusal forms and `targets_refusals` and `depth_refusals` get their
  multisampled lines back, in the commit that pins.
- Per task one lane (the Mac's `d3d9-check` on default and core); full gates once, in Task 8. Benches and probes
  run once, on an idle Mac. No gate, build or VM job while the maintainer is gaming. Autonomous work stops at 95%
  weekly usage.

## Scope

In: `CreateDevice` with `MultiSampleType` 2, 4 or 8 samples or `NONMASKABLE` with a quality (windowed; back buffer
and auto depth-stencil); multisampled `CreateRenderTarget` and `CreateDepthStencilSurface`; `GetDesc` of them;
draws, whole and rect clears, the scissor, blending, sRGB write and index 1-3 targets on multisampled targets;
`D3DRS_MULTISAMPLEMASK`; alpha-to-coverage (`D3DRS_ADAPTIVETESS_Y == ATOC` with the alpha test on; the `A2M1` /
`A2M0` point-size latch); a depth-stencil or an extra target whose sample count differs from target 0's;
`StretchRect` from a multisampled render target into a single-sample render target (a texture level, a
`CreateRenderTarget` surface, the device's back buffer) of the same size and of a format whose bytes copy, whole
surface; Present of a multisampled back buffer; `GetRenderTargetData` of a multisampled source
(`D3DERR_INVALIDCALL`); `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)`, its `GetDesc` and its lock; the sweep.

Out (owners): every other `StretchRect` (stretching, a rect, a converting format pair such as X8 into A8, depth,
a plain surface, a multisampled or AUTOGENMIPMAP destination, an additional swap chain's back buffer),
`ColorFill`, `UpdateSurface`, `GetDC`, `Reset` and with it a changed multisample type, fullscreen, RESZ and any
depth resolve, `GetFrontBufferData`, the `Ex` creators (C4c); fixing what the sweep finds (C4c); per-draw dedup
and a Present that resolves only what was drawn (C4d); `D3DRS_MULTISAMPLEANTIALIAS` (a no-op under both
runtimes).

## Review Focus

1. **A pass, its pipeline and its tile barrier agree on the sample count, always** (a mismatch is undefined
   behaviour in Metal, like a format mismatch). The key names the count; d3d9-rt refuses, at every draw and
   whatever pass is open, a key whose count is not target 0's (`DrawRefused::Target`); every attachment of a pass
   has that count: an extra of another count is out of the set, a depth-stencil of another count is not attached
   and the key then names no depth format, no depth state is set and the pre-transformed clamp is on (mtld3d's
   clamp reads the pass's planes: unix `draw.rs:998`). rt `the_key_names_the_sample_count`,
   `an_extra_of_another_count_is_not_in_the_set`, `a_depth_of_another_count_is_left_out`; rows `ms-mrt`,
   `ms-mismatch`; every core launch under Metal's validation layer with no report.
2. **A resolve lands where the application made it.** It is never moved before a pass that writes or reads its
   destination (Task 3's guard); it reads what was last written, through the view format it was written in; the
   destination's open pass ends first and its owed CPU writes are flushed. dxmt-core
   `a_resolve_is_not_folded_across_a_pass`; rt `a_resolve_takes_the_last_view`,
   `a_resolve_ends_the_destinations_pass`; rows `ms-srgbmask`, `ms-redraw`.
3. **Coverage follows the current target, not the device.** The sample mask is narrowed against target 0's count
   and type (a non-maskable target ignores it; a single-sample one too); alpha-to-coverage replaces the alpha test
   only on a multisampled target. Rows `ms-mask`, `ms-nonmask`, `ms-a2c`.
4. **mtld3d's answers at creation and at the read paths**: `D3DERR_INVALIDCALL` for an invalid or unavailable
   multisample type at `CreateDevice` and at the two creators, for a lockable multisampled target, for
   `GetRenderTargetData` of a multisampled source; a `StretchRect` outside the resolve shape is still the logged
   refusal. Rows `ms-desc`, `rt-msaa`.
5. **The default runtime is untouched and single-sample draws pay nothing visible**: the count is 1 and the
   coverage inputs are inert on every existing path. The probe; the plain build.

---

### Task 1: rows (MacNeutron) and the gate scripts

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s5/gates/*.sh` (scratch).

- [ ] **Step 0: the gate scripts** (copied from `build/c4b3d/s4/gates`; `s4`→`s5`, `c4b3d4-`→`c4b3d5-`): each
  job's header says what it gates here; `jq.sh`'s validation-layer list has `msaa`; `jc.sh` and `jcr.sh` start
  the builder with `outp=$B/p/eac-vm-work/images`; `jp.sh` waits, before its first run, until `sysctl -n
  vm.loadavg`'s one-minute figure is under 3 (ten minutes at most, then it runs and says so). `jm.sh`,
  `chainA.sh` (jq, jm), `chainB.sh` (jp, jr, jc **with** D3D10-12's gate, jw) and `jw.sh` are written at Task 8.
- [ ] **Step 1: write.** Constants `D3DFMT_ATOC`, `FOURCC_A2M1`, `FOURCC_A2M0` (`MAKEFOURCC`). `main`: a flag
  `msaaMode` (`argv[1] == "msaa"`), whose device has `MultiSampleType = D3DMULTISAMPLE_4_SAMPLES` and the auto
  D24S8; its dispatch; `msaaRows` with the blend kind's prologue and runner, budget 180 s, the 16 rows of the
  appendix. Helpers: `msRt(Com<IDirect3DSurface9> &, size, format, type, quality = 0)` (`CreateRenderTarget`);
  `resolveInto(src, Rt &dst, size, format)` (a new single-sample render-target texture of that size and format,
  `StretchRect(src, nullptr, dst level 0, nullptr, D3DTEXF_NONE)`), after which `rtHas` reads `dst`; `inBand(what,
  got, lo, hi)` for one channel and `meanOf(surface, format, x, y, n)` (the mean of an n x n block) for a value
  coverage decides; `msStart()` (the mrt kind's `mrtStart` plus `D3DRS_MULTISAMPLEMASK 0xffffffff`,
  `D3DRS_ADAPTIVETESS_Y 0`, `D3DRS_POINTSIZE` = `A2M0` then 1.0, `ALPHATESTENABLE FALSE`, `ALPHAFUNC ALWAYS`,
  `ALPHAREF 0`) run at the start of every attempt by `msUntil` (as `mrtUntil`: `reset()` restores none of these).
  No row uses `frames()` (it reads the back buffer with `GetRenderTargetData`, which refuses a multisampled one).
  `targets` kind: `rt-msaa` becomes a result and `rt-plain` is new; `depth` kind: `ds-msaa` becomes a result
  (appendix). Headers and counts.
- [ ] **Step 2: scripts.** `d3d9-check.sh`: delete the `targets_refusals=` line, remove `targets` from the
  `state|textures|fixed|targets|depth)` arm and its inner `targets) set_=...` line, make the zero arm
  `blend|mrt|targets|msaa)`; `depth_refusals` loses its multisampled line and stays one single-quoted value of
  two lines with no trailing newline; `rt-plain` joins the targets rows; `launch ours fresh msaa 240 msaa $strict`
  and `check msaa "<rows>"` after `check mrt`, before the mtld3d-lane refusal grep; the `unpresented` pattern
  gains `depth-stencil is not its key` (Task 5's logged drop); the header and `refusals_ok`'s comment.
  `d3d9-gate.sh`: the same sets, arm and pattern; `runtime "$G/logs/$r"` (no kind) for `tg64-fresh tg32-fresh`;
  `ms64-fresh ms32-fresh` join the `bl64-fresh bl32-fresh mr64-fresh mr32-fresh` loop; `rt-plain`; `for r in
  ms64-fresh ms32-fresh` row loops; the header. `d3d9.sh`: two `tri` lines (`ms64-fresh`, `ms32-fresh`, 240 s,
  `msaa $strict`) after `mr32-fresh`, the header.
- [ ] **Step 3: compile** with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall -Wextra`) — no
  diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle msaa rows in every D3D9 lane (DXMTRS C4b-3d5)`.
- [ ] **Step 5: the controls** (`ja`: default, msl, core on DXMTRS 5f9e5caa). Expected: default and msl ok with
  every new row; core red exactly as the Global Constraints list. The rule for a row red on the default runtime
  (C4b-3d4's): an expectation derived from D3D9's formula that mtld3d misses is a defect of mtld3d (ledgered; the
  row keeps D3D9's value, red-on-default until its fix); one read off mtld3d's code ("parity" in the appendix) or
  a hardware band that mtld3d's run falls outside is a wrong row, corrected in the appendix and the row.
- [ ] **Step 6: the VM's default lanes once** (`jcr`): `ms64-fresh`, `ms32-fresh` and the changed rows green
  inside their budget; a budget miss raises the kind's budget before any core work. A VM whose device cannot be
  made with 4 samples stops the task for a ruling (the guest forwards `supportsTextureSampleCount`, so none is
  expected).

### Task 2: `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` (front end)

**Files:** Modify `src/d3d9/src/device.rs` (`device_create_offscreen_plain_surface`).

- [ ] **Step 1: implement.** The `refuse_under_core!` at the default-pool arm goes (R3's trace of
  `create_texture_path(offscreen_plain: true)`, `new_owned_texture_backed`, the lock through the parent texture
  and `finalize_surface` under core found no `push_control` and no encoder op). `StretchRect`, `ColorFill`,
  `UpdateSurface`, `GetDC` and the `Ex` creator keep their refusals.
- [ ] **Step 2: run** `rust-check`; **commit** `d3d9: CreateOffscreenPlainSurface in the default pool on the core
  runtime`.
- [ ] **Step 3: lane** `JT=jt2` — both runtimes: `rt-plain` ok; core otherwise as the control.

### Task 3: dxmt-core: a resolve folds only into the pass right before it

**Files:** Modify `src/dxmt/src/encoder/optimize.rs` and `optimize/tests.rs`.

- [ ] **Step 1: failing test** `a_resolve_is_not_folded_across_a_pass` (beside
  `optimizer_refuses_across_dependency`; a multisampled source T and a single-sample destination D): `Render{T}`,
  `Render{D}` (a draw on D), `Resolve(T→D)`: three passes in that order, the resolve its own pass (today: the
  resolve is folded into the first, so the draw on D lands over the resolved image); `Render{T}`, `Clear{D}`,
  `Resolve(T→D)`: the resolve after the Clear (today: D ends as the clear colour); `Render{T}`, `Resolve(T→D)`
  with nothing between: one pass, `STORE_AND_MULTISAMPLE_RESOLVE` (the fold stays). Expected: the first two fail.
- [ ] **Step 2: implement.** In `Encoder::optimize`'s scan, a `(Render, Resolve)` pair is offered to the fold
  only when no live (non-`Null`) pass lies between them; otherwise it is `Merged::Nothing` (the dependency test
  then decides, as for any other pair). The module doc states the deviation from the C++'s list, as C4b-3d3's
  area guard did.
- [ ] **Step 3: run** dxmt-core's and d3d11's tests, `rust-check` — no other test moved (D3D11's
  `ResolveSubresource` keys its source as an array view, which never matched a non-array target's pass: R2).
- [ ] **Step 4: commit** `dxmt-core: a resolve folds only into the render pass right before it`.

### Task 4: dxmt-fakes, and what dxmt-core does with a multisampled view

**Files:** Modify `src/dxmt/fakes/src/lib.rs`; add tests to `src/d3d9/rt/src/device/target/tests.rs`.

- [ ] **Step 1: the fakes.** A pass: `  resolve{i} #{}` after a colour's lines when it has a resolve texture, and
  `  samples={}` after the stencil line when `default_raster_sample_count` is above 1. Both pipeline fakes
  (`..WithVertexDescriptor` and the plain `MTLDevice_newRenderPipelineState` the quad clear uses): `  pso
  samples={} a2c={}` when the raster count is not 1 or alpha-to-coverage is on. Existing lines keep their text.
- [ ] **Step 2: three tests through dxmt-core's public API**, on a texture made as Task 5 will make it (`info_bits
  (TYPE2D_MULTISAMPLE, 1, 4, RENDER_TARGET | SHADER_READ | PIXEL_FORMAT_VIEW)`, the usage `texture_create` gives
  a render target with twins): `dxmt_core_attaches_a_multisampled_view` (a pass on its `TYPE2D_MULTISAMPLE` view:
  the texture's bits say 4 samples and the multisample type; the pass is encoded), `dxmt_core_resolves_into_a_
  level_view` (`resolve_texture` into a `TYPE2D` view of level 1 of a 2-level texture, and into a cube face's:
  alone, a pass with `color0 .. store=3` and `resolve0 #<that view>`; right after a render pass on the
  multisampled view, folded: one pass, `store=3`), `dxmt_core_resolves_through_a_twin` (both views re-keyed to
  the sRGB format: `resolve0` names the twin's view). These pin what the transcript can show; that Metal accepts
  a format view of a multisampled texture and a non-array resolve is Task 8's validation-layer run (mtld3d's
  companion and its sRGB twin are the in-tree precedent).
- [ ] **Step 3: run** dxmt-core's and d3d9-rt's tests, `rust-check`; **commit** `dxmt-fakes: a pass's resolve
  targets and sample count, a pipeline's raster count; multisampled views through the core, pinned`.

### Task 5: the sample count in d3d9-rt and the key (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ tests); `src/d3d9/rt/src/device/textures.rs`,
`target.rs`, `depth.rs`, `recorder.rs`, `draw.rs`, `surface.rs`, `samplers.rs`, `fixture.rs` and tests;
`src/d3d9/src/device/core_draw.rs`; every `CoreTextureDesc` literal (rt `samplers.rs`, `fixture.rs` x4,
`draw/tests.rs`, `target/tests.rs`, `textures/tests.rs` x4; front end `device.rs` x2, `direct3d9.rs`,
`texture.rs` x2); `tools/derive_inventory.txt` if a derive moves.

**Interfaces (produces):**
- d3d9-rt: `CoreTextureDesc`'s two `bool`s become `pub flags: CoreTextureFlags` (bitflags `VIEW_TWINS`,
  `RENDER_TARGET`: the struct is touched at every literal, and CONVENTIONS packs two) and it gains `pub samples:
  u8` (1, or 2/4/8 for a multisampled `Tex2d` render target of one level: `TYPE2D_MULTISAMPLE`, `info_bits(..,
  samples, ..)`; `texture_create` debug-asserts that shape); `Shape { .., samples: u8 }` (the back buffer's is 1
  until Task 6); a multisampled target's and depth-stencil's views are `TYPE2D_MULTISAMPLE` (`texture_target`,
  `resolve_depth`); `#[must_use] pub fn target_samples(&self) -> u8` (target 0's; 1 when it does not resolve)
  and `#[must_use] pub fn depth_samples(&self) -> Option<u8>`; `pass_set` keeps an extra only when its count is
  target 0's too; `open_pass` sets `tile_barrier_pso_key[8]` to the count; `build_pipeline` sets
  `raster_sample_count` and `alpha_to_coverage_enabled` from the source; `draw_up` answers
  `Err(DrawRefused::Target)` when `key.samples` is not the count of the shape it already fetched, **before** the
  open-pass shortcut (an open pass on the same colours does not excuse the count), and `Err(DrawRefused::Depth)`
  for a key naming a depth format while `depth_samples() != Some(target_samples())`; `read_back` answers false
  for a source of more than one sample (Metal blits no multisampled texture).
- mtld3d-core: `PipelineKey { .., pub samples: u8, pub alpha_to_coverage: bool }` and the same on
  `PipelineSource`; `core_key_depth(bound, samples_bound, uses_plane, same_samples: bool)`: a bound depth-stencil
  whose count is not target 0's gives format 0 before anything else (mtld3d's `pass_binds_depth`), no refusal;
  `#[must_use] pub fn core_coverage_variant(variant: VariantKey, sample_mask: u8, alpha_to_coverage: bool) ->
  VariantKey`: a mask other than `SAMPLE_MASK_ALL` sets `VariantFlags::SAMPLE_MASK` and the byte (mtld3d's draw,
  unix `draw.rs:583-589`); `alpha_to_coverage` zeroes `alpha_func` (`draw.rs:529-540`). `core_ps_variant` is
  unchanged (it clears the mask, which this then sets): the order is `core_ps_variant`, `core_texture_variant`,
  `core_srgb_variant`, `core_coverage_variant`, and the result is what the alpha-reference bind and the key take
  (the fixed-function stage honours both, R2).
- Front end (`core_draw.rs`): `samples = core.target_samples()`; the bound target 0 surface's `multi_sample()`
  type for `effective_sample_mask(rs[D3DRS_MULTISAMPLEMASK], samples, type)`; `a2c = PipelineRsBits { flags:
  ALPHA_TO_COVERAGE when alpha_to_coverage_requested(&rs, dev.a2m_enabled()), ..default }
  .alpha_to_coverage(samples)` (mtld3d's own predicate); the variant through `core_coverage_variant`;
  `key.samples`, `key.alpha_to_coverage`; `key_depth` passes `same_samples = core.depth_samples() ==
  Some(samples)`, and the clamp's "depth bound" input is false for a depth-stencil of another count.
  `Err(DrawRefused::Depth)` answers `D3D_OK` and logs once ("d3d9 core: a draw's depth-stencil is not its key's;
  the draw is dropped"), as `Target` does.

- [ ] **Step 1: failing tests.** mtld3d-core: `core_coverage_variant_rules` (the all mask leaves the variant
  alone; 0x3 sets flag and byte; alpha-to-coverage zeroes `alpha_func` and leaves the mask), `core_key_depth`'s
  table grows by the count column, the key table by `samples` and `alpha_to_coverage` (two cases more than its
  size at 5f9e5caa). rt (a 4-sample 64x64 target from a fixture helper `ms_desc(format, 64, samples)`; target 0 a
  64x64 texture as C4b-3d4's tests): `a_multisampled_texture_is_made_so` (the fake's `bits` hold 4 samples and
  the multisample type; zeroed by a pass), `the_key_names_the_sample_count` (target 0 4-sample: a key with
  `samples: 4` draws, `pso samples=4 a2c=0`; a key with 1 is `Err(Target)`, also right after a 4-sample draw left
  its pass open; on a single-sample target a key with 4 is `Err(Target)`),
  `an_extra_of_another_count_is_not_in_the_set` (4 beside 1 and 1 beside 4: `target_desc` answers 0 for the slot;
  equal counts: in), `a_depth_of_another_count_is_left_out` (a 4-sample depth-stencil with a single-sample target
  and the reverse: a key naming the depth format is `Err(DrawRefused::Depth)`, one naming none draws with no
  `depth` line; equal counts attach), `alpha_to_coverage_is_the_pipelines` (`pso samples=4 a2c=1`; two keys
  differing in it build two pipelines), `a_clear_of_a_multisampled_target` (whole: folds into the pass; rects:
  the quad clear's `pso samples=4`), `a_multisampled_target_is_not_read_back`. Expected: do not compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** `audit.sh --update-derives`, the crates' tests, `rust-check`.
- [ ] **Step 4: commit** `d3d9: a sample count in the core runtime's textures, passes and pipelines`.
- [ ] **Step 5: lane** `JT=jt5` — both runtimes exactly as Task 2's lane.

### Task 6: resolve, and the multisampled back buffer (d3d9-rt)

**Files:** Modify `src/d3d9/rt/src/device.rs`, `device/target.rs`, `recorder.rs`, `swapchain.rs`, `textures.rs`,
`surface.rs` and tests; `src/d3d9/src/direct3d9.rs` (`attach`'s call site, still with 1).

**Interfaces (produces):**
- d3d9-rt: each multisampled colour target remembers the view format it was last written in (`Stored`, and the
  back buffer's companion: a `u32`, the stored format at creation; set by `open_pass` for each attached slot and
  by `clear`/`clear_rects`);
  `#[must_use] pub fn resolve_into(&mut self, source: TargetRef, destination: TargetRef) -> bool` (`resolve` is
  taken): false, and nothing recorded, unless the source has more than one sample, the destination one, their
  sizes and stored formats are equal and the format resolves (`CAP_RESOLVE` in dxmt-core's public
  `device_capability_map`; the float formats do not: R1 I2); else `close_pass()`, and `enc.resolve_texture` from
  the source's `TYPE2D_MULTISAMPLE` view to the destination's view, both in the source's last-written format
  (equal stored formats have the same twin);
  `attach(hwnd, width, height, samples: u8)`: with `samples > 1` the back buffer is the single-sample texture as
  today plus a multisampled companion (BGRA8, the twin usage, zeroed). Two accessors, named for what they give:
  the *drawn* back buffer (the companion when there is one: `shape`, `resolve`/`resolve_as` for passes and
  clears, `resolve_into`'s source, `target_desc`, `color_size`; its `Shape::samples` is the count) and the
  *presented* one (the single-sample texture: `present`, and `resolve_into`'s destination when the device's back
  buffer has one sample). `present` with a companion resolves it into the presented texture first, in its
  last-written format, then presents as today (the presenter stays at one sample; every frame, drawn or not: a
  C4d input).
- [ ] **Step 1: failing tests.** rt: `a_resolve_takes_the_last_view` (a draw through the twin, a pass on another
  target, then `resolve_into`: a pass of its own whose `resolve0` names the destination's twin view; the same
  after a Clear with sRGB write off: the stored views; with nothing between the draw and the resolve: folded,
  `store=3` on the draw's pass), `a_resolve_ends_the_destinations_pass` (a draw on D, then `resolve_into(T, D)`:
  D's pass ends, the resolve follows it, and a later draw on D opens a new pass after the resolve),
  `a_resolve_refuses_what_it_cannot_do` (single-sample source, multisampled destination, another size, another
  format, a float format: false, nothing recorded), `a_multisampled_back_buffer_presents_resolved` (`attach(..,
  4)`: a draw's pass names the companion; `present`: the resolve into the presented texture, then the present of
  it, in that order in the transcript; a second frame the same), `a_single_sample_back_buffer_is_as_before`.
  Expected: do not compile.
- [ ] **Step 2: implement; run** tests and `rust-check`.
- [ ] **Step 3: commit** `d3d9: multisample resolve and a multisampled back buffer in the core runtime's device`.
- [ ] **Step 4: lane** `JT=jt6` — as Task 2's lane.

### Task 7: creation, `StretchRect`'s resolve, the read-back's refusal (front end)

**Files:** Modify `src/d3d9/src/direct3d9.rs` (`create_core_device`), `device.rs` (`core_color_target`,
`core_depth_surface`, the two creators' core arms, `device_stretch_rect`), `device/core_arms.rs`
(`get_render_target_data`, and the resolve arm's body).

- [ ] **Step 1: implement.**
  - `create_core_device`: `windowed == 0` is still refused first. The multisample check sits **after** the back
    buffer's format is settled (the zero format's replacement and the X8/A8 test, `direct3d9.rs:2341-2356`: a
    zero format is `Invalid` to `resolve_sample_count`): `resolve_sample_count(type, quality, format, caps)`, any
    reject `D3DERR_INVALIDCALL` (mtld3d's answer, `direct3d9.rs:2126`); `core.attach(hwnd, w, h, samples)`; the
    auto depth-stencil's `CoreTextureDesc::samples` is the count; `DeviceCreateInfo::backbuffer_sample_count` is
    the count and the stored present parameters keep the type and quality (`GetDesc` and the mask narrowing read
    them).
  - `core_color_target` and `core_depth_surface`: `samples` from the spec's `SurfaceMultiSample`; both
    multisample refusals go (the shared `lockable && multisampled → D3DERR_INVALIDCALL` runs before them).
  - `get_render_target_data` (core arm): a source whose `multi_sample().sample_count > 1` is
    `D3DERR_INVALIDCALL`, where mtld3d checks it.
  - `device_stretch_rect`: mtld3d's later validation cannot run on core surfaces (`resolve_stretch_surface`
    answers `None` without Metal handles, and past it a core device would reach `push_control`). So the core arm
    is one cfg'd block **right after the null and filter checks** (`device.rs:9404-9418`), replacing the blanket
    refusal there, and calls `core_arms::stretch_rect_resolve`, which answers `D3D_OK` for exactly one shape and
    the refusal for everything else: both surfaces have a `core_target()` and neither is an additional swap
    chain's back buffer (the test `get_render_target_data` uses); `GetDesc` of both: render-target usage, the
    default pool, equal sizes, formats for which mtld3d's `copies_bytes` holds; the source's `multi_sample()`
    count above 1 and the destination's 1; each rect null or the whole surface; the destination's parent texture,
    if any, not AUTOGENMIPMAP and flushed (`core_flush_dirty`, as `SetRenderTarget` does); and
    `core.resolve_into(source, destination)` true. The refusal's name becomes "IDirect3DDevice9::StretchRect
    (other than a multisample resolve)".
- [ ] **Step 2: run** `rust-check`; **commit** `d3d9: multisampled devices and surfaces, and StretchRect's resolve,
  on the core runtime`.
- [ ] **Step 3: lane** `JT=jt7` — both runtimes `d3d9-check: ok`: the 16 `msaa` rows, `rt-msaa`, `ds-msaa`,
  `rt-plain`; the targets and msaa launches' logs with no refusal line, the depth launch's with its two.

### Task 8: gates, mutants, the sweep, the probe

- [ ] **Step 1: `jq`**: the three Mac lanes; the stateblock conformance under core equal to default; every core
  launch, `msaa` included, under Metal's validation layer with no report (the proof that Metal takes the format
  views and the non-array resolve Task 4 could only print).
- [ ] **Step 2: mutants** (`jm`; one line each, leaving no unused item in the PE build; each red on its row and
  on no item outside `msaa-*`; none hands Metal a pass and a pipeline that disagree): (a) the front end's mask is
  `effective_sample_mask(0xffff_ffff, ..)` → `ms-mask`; (b) `core_coverage_variant` keeps `alpha_func` →
  `ms-a2c`; (c) `resolve_into` takes the stored format's views → `ms-srgbmask` (its unfolded resolve); (d) the
  clamp's "depth bound" input ignores the count → `ms-mismatch`; (e) the mask is narrowed with
  `D3DMULTISAMPLE_4_SAMPLES` whatever the target's type → `ms-nonmask`; (f) alpha-to-coverage takes the device's
  back-buffer count, not target 0's → `ms-a2c` (its single-sample cell); (g) the Clear loop reaches a
  bound slot of target 0's size and another sample count → `ms-mrt` (that extra turns white).
- [ ] **Step 3: plain build** (`jr`): translator key and imports unchanged; `d3d9-check` default; the Mac's Wine
  conformance with no new failing site; the x64 `dxmt/check.sh` all passed (idle Mac).
- [ ] **Step 4: VM** (`jc`, with `vm/dxmt/gate.sh`: dxmt-core changed): D3D10-12's gate ok; the D3D9 lanes on
  default and core, old and new WoW64.
- [ ] **Step 5: the sweep** (`jw`, measured, never a gate verdict; R3's recipe): a `DXMT_D3D9_CORE=1` build; a
  copy of `dxmt/mtld3d-conformance.sh` in the job's folder with the "stateblock alone" guard and its
  `unset MTLD3D_CONFORMANCE_RAW_DIR` cut and `ROOT` pinned to the main checkout; for each of `visual`, `device`,
  `d3d9ex`: `D3D9_RUNTIME=core MTLD3D_CONFORMANCE_TIMEOUT_SECS=900 MTLD3D_CONFORMANCE_RAW_DIR=<suite>/raw sh
  conf-sweep.sh --only <suite>`, a 1500 s watchdog that ends that script by its pid and that prefix's
  wineserver, then the suite's `run-core.txt` and `logs-core` copied aside (the next suite overwrites them).
  `build/c4b3d/s5/sweep.md` holds, per suite: executed tests and failures beside the default runtime's from
  `jr/conf-default.out`; whether it ran to its end, and the last test before a crash, hang or timeout (the raw
  log's tail); each refusal line and each `reject CreateDevice: runtime.mode = core` in order of first
  appearance, mapped to the test function that met it (a refusal is logged once a process: no counts); every
  `panicked` line with its location; the failing sites grouped by test function. The logs are deleted once the
  report is written. Nothing is fixed here.
- [ ] **Step 6: the probe** (`jp`, once, on a Mac it waited to find quiet): this tip on its own build, tex and
  texdepth, mtld3d and core, into `build/c4b3d/s5/probe.md` beside C4b-3d3's quiet figures (core 2.18 / 2.25). A
  core figure more than 0.32 above them gets C4b-3d4's retake (DXMTRS 5f9e5caa with MacNeutron 129c271 in the
  same window).

### Task 9: review, spec, pin, push

- [ ] Whole-branch review (Opus; the Review Focus verbatim; the ledger's rulings); one fix pass; a minor is fixed
  when it is a line, else ledgered.
- [ ] Spec §5: a C4b-3d5 paragraph; C4b-3d as a whole marked done; the sweep's headline for C4c. Execution notes
  here. `dxmt/pins`.
- [ ] Privacy scan; fetch-check both origins; push DXMTRS then MacNeutron; post-push check from main (plain build,
  tests, `d3d9-check` default and msl, x64 check); CI; scratch deleted by full path; memory.

## Rules check

- **One dxmt-core change** (Task 3): a guard in the optimizer's scan, with its unit test; the fold was wrong for
  every front end (a resolve moved before a pass that writes or reads its destination), reached by D3D11 with
  an array multisampled target only (its resolve keys do not match a non-array target's pass). Gated by
  dxmt-core's and d3d11's tests, the x64 check and the VM's D3D10-12 gate.
- **mtld3d's rules are called**: `resolve_sample_count`, `effective_sample_mask`, `alpha_to_coverage_requested`,
  `PipelineRsBits::alpha_to_coverage`, `copies_bytes`. Restated in d3d9-rt, which has no `PassState`, each one
  comparison and each pinned: the extra's count test (`recompute_extra_present_mask`), the depth-stencil's
  (`pass_binds_depth`), and "resolve through the view last written" (`color_resolve_view`).
- New type: `CoreTextureFlags` (bitflags; `CoreTextureDesc`'s two `bool`s, packed as CONVENTIONS asks of a struct
  that is touched). New fields: `CoreTextureDesc::samples`, `Shape::samples`, `PipelineKey::{samples,
  alpha_to_coverage}` and `PipelineSource`'s (one `bool` on each), a multisampled target's last-written format,
  the back buffer's companion. New functions: `core_coverage_variant`, `target_samples`, `depth_samples`,
  `resolve_into`, `core_arms::stretch_rect_resolve`. A fourth parameter of `core_key_depth` (its third `bool`).
  No new file, static, config key, wire field, dependency or lint suppression.
- Not silent: `DrawRefused::Depth` is logged once by the front end now, as `Target` is, and both phrases fail a
  launch in the gates.
- New refusal literal "IDirect3DDevice9::StretchRect (other than a multisample resolve)" replaces
  "IDirect3DDevice9::StretchRect"; three literals go (the plain surface's, the two multisampled creators').

## Rulings (planning)

1. **`StretchRect`'s multisample-resolve shape is built here** although the program's split puts `StretchRect` in
   C4c. Why: under core nothing else can read a multisampled target (`GetRenderTargetData` refuses one by D3D9's
   rule; no sampling, no `GetFrontBufferData`), so no row could see a multisampled pixel. Cost if wrong: an arm
   C4c widens or rewrites; every other shape stays the logged refusal, so the sweep still counts it.
2. **The optimizer's resolve fold is fixed in dxmt-core**, not keyed around in d3d9-rt (R2 C1: array view keys
   would never fold). Why: the fold moves a write earlier past passes nobody checked, for any front end; one
   guard and its test. Cost if wrong: a D3D11 title whose resolve followed its source's pass with other passes
   between (an array multisampled target only: the one shape that folds for D3D11) pays a resolve pass of its
   own, which is the order it asked for.
3. **A multisampled colour surface is one multisampled texture; only the back buffer has a single-sample twin.**
   mtld3d gives every multisampled target a twin and resolves at each submit. Why: under core the only readers
   are `StretchRect` (which resolves into its destination) and Present. Cost if wrong: C4c's general
   `StretchRect` from a multisampled source needs a scratch resolve target, or the twin then.
4. **The resolve goes through the last-written view format**, as mtld3d's. Cost if wrong: the mask and edge
   averages of an sRGB-written target differ from mtld3d's (`ms-srgbmask` pins the mask case, through a resolve
   that is not folded). Should the GPU average an sRGB view's samples encoded (the row then reads 128 with sRGB
   write on, under both runtimes), the row keeps that value as parity and mutant (c) has no row: ledgered then.
5. **A depth-stencil or extra of another sample count than target 0 is left out, not refused**, as mtld3d. Cost
   if wrong: none against mtld3d.
6. **`CreateDevice`'s multisample reject is `D3DERR_INVALIDCALL`** under core now (it was `NOTAVAILABLE` with the
   whole feature). Fullscreen stays `NOTAVAILABLE`.
7. **Coverage-dependent pixels are asserted as bands** with integer bounds (48 to 208 of 255 for "between a
   quarter and three quarters", a 4x4 mean where a dither decides): they are the hardware's, neither D3D9's
   formula nor mtld3d's code. Exact values are asserted for interiors, clears and full-quad sample-mask
   fractions. Cost if wrong: a coverage defect inside the band passes.
8. **The Present-time resolve has no row** (no call reads the presented image under core): rt
   `a_multisampled_back_buffer_presents_resolved` is its proof, and the eye its check.
9. **The sweep is measured, not gated, and nothing it finds is fixed here** (C4c's input).
10. **Rows are controls first**, on the Mac (default, msl) and in the VM.
11. **`CoreTextureDesc`'s two flags are packed while its literals are being edited** rather than argued around
    (R3 M2). Cost if wrong: churn in thirteen literals that were changing anyway.

## Stated gaps

- No row reads a presented multisampled frame (ruling 8), a multisampled depth-stencil's resolved depth (C4c), or
  a `StretchRect` shape other than the whole-surface resolve.
- The plain surface's content has no reader under core until C4c: `rt-plain` reads back what it locked.
- Sample-rate shading, centroid interpolation and `vPos` under multisampling: no row under either runtime.
- D3D9 refuses multisampling with a lockable back buffer or a swap effect other than DISCARD; mtld3d checks
  neither and the core follows it. A `Reset` to another multisample type and the `Ex` creators are refused whole.
- mtld3d waits for a resolve to retire on the paravirtual GPU (`resolve_needs_retire`); the core has no
  counterpart: the VM's `msaa` rows are the check.
- An index 1-3 target written through its sRGB twin under multisampling is covered by one cell of `ms-mrt` only.
- No machine without 4-sample targets is handled: `main`'s `msaa` device and `ds-msaa` would fail there (every
  Metal device this project runs on offers 4).

## Appendix: the rows

The `msaa` device: back buffer 256x256 X8R8G8B8 with 4 samples, auto D24S8 with 4 samples. "T" is a 64x64
A8R8G8B8 `CreateRenderTarget` surface of 4 samples unless said; "read" is `resolveInto` a new single-sample
target of the same format, then `GetRenderTargetData` of that. Quads are pre-transformed and pixel-aligned unless
said. "band" is a channel between 48 and 208. A blended or coverage quad's alpha is its diffuse alpha (`ALPHAOP
SELECTARG1`, `ALPHAARG1 DIFFUSE`).

**targets kind** (single-sample device). `rt-msaa`: a 4-sample 64x64 A8R8G8B8 target: `D3D_OK`, `GetDesc` says 4
samples; cleared green with a red quad over its left half; read into a texture level: (16,32) red, (48,32)
green; read into a single-sample `CreateRenderTarget` surface: the same; a 4-sample 256x256 X8R8G8B8 target
cleared green, `StretchRect` into the back buffer, the back buffer read: green; `GetRenderTargetData` of the
multisampled target is `D3DERR_INVALIDCALL`; a lockable 4-sample target is `D3DERR_INVALIDCALL`; `StretchRect`
of it into a 32x32 target answers the same under both runtimes when it succeeds on mtld3d (non-strict: any
answer; strict: `D3DERR_NOTAVAILABLE`, the shape is not the resolve's). `rt-plain`:
`CreateOffscreenPlainSurface(64, 64, A8R8G8B8, D3DPOOL_DEFAULT)` is `D3D_OK`; `GetDesc`: default pool, usage 0,
no multisampling; a texel written under `LockRect` is read back under a second lock; `SetRenderTarget(0, it)` is
`D3DERR_INVALIDCALL`.

**depth kind.** `ds-msaa`: a 4-sample 256x256 D24S8 surface: `D3D_OK`, `GetDesc` says 4 samples; bound with the
single-sample back buffer and `ZENABLE` on, a near quad then a far quad over it: the far one shows (the
depth-stencil of another count is left out: parity), every call `D3D_OK`.

**msaa (17, as built: the cells below are the rows', corrected where the rows agent's differ from the draft).**
- `ms-initial` (added by the final review; run first, before anything binds a target, with index 0 still the
  device's own back buffer): the mask 0x3 and a white quad over black, the back buffer read through a 256
  X8R8G8B8 target: 128 in each channel.
- `ms-desc`: the back buffer and the auto depth-stencil say 4 samples; `GetRenderTargetData` of the back buffer
  is `D3DERR_INVALIDCALL`; `CreateRenderTarget` and `CreateDepthStencilSurface` with 16 samples are
  `D3DERR_INVALIDCALL` (mtld3d's creators answer it for every reject: parity); a `StretchRect` with a null source
  is `D3DERR_INVALIDCALL`; a depth-stencil `StretchRect` (the auto depth into a 4-sample D24S8 surface) is
  `D3DERR_NOTAVAILABLE` under strict, any answer otherwise.
- `ms-resolve`: T cleared green, a red quad over x < 32: read (16,32) red, (48,32) green.
- `ms-edge`: T cleared black, a white clip-space triangle (-1,-1), (-1,1), (1,-1) over its lower left half:
  (8,56) white, (56,8) black; (31,31), on the hypotenuse, in the band, and the 4x4 mean from (30,30) in the band.
- `ms-back`: the back buffer cleared blue, a red quad (64,64)-(192,192), `Present` `D3D_OK`; then cleared blue
  and drawn again (a presented DISCARD back buffer is undefined) and read through a 256 X8R8G8B8 target:
  (128,128) red, (32,32) blue.
- `ms-clear`: T: a whole Clear to red; `Clear(1, (8,8)-(24,24))` to green; viewport (32,32,16,16) and a null-rect
  Clear to blue: (16,16) green, (40,40) blue, (28,28) and (56,40) red, and the texels on each side of the rect's
  edges exact ((7,16) red, (8,16) green, (23,16) green, (24,16) red).
- `ms-depth`: T 256x256 with the 4-sample auto depth-stencil, Z cleared; a near red quad (z 0.6) over the left
  half, a far white quad (0.8) over all, LESSEQUAL: (64,128) red, (192,128) white; then `Clear(1,
  (128,0)-(256,256), ZBUFFER, 0.1)` and a green quad at z 0.5 over all: green on the left (the drawn control),
  (192,128) still white.
- `ms-mismatch`: T (4 samples) with a single-sample 64x64 D24S8 surface bound, `ZENABLE` on: a near red quad
  over all then a far white one over y < 48: white at (48,32) (no depth plane in the pass), red at (48,56); a
  pre-transformed green quad at z 1.5 over the left half: drawn (the clamp is on; parity); a Z Clear answers
  `D3D_OK`. Added by the final review: that Z Clear does nothing to the depth-stencil (parity: mtld3d's is a
  no-op): Z cleared to 1 beside a single-sample target, "cleared" to 0.1 beside T, and a quad at z 0.5 beside the
  single-sample target still passes.
- `ms-mask`: T cleared black, `D3DRS_MULTISAMPLEMASK` 0x3, a white quad over all: 128 in each channel; 0x1: 64;
  0xf: 255; 0x3 through a ps_2_0 writing white: 128; on a single-sample target the mask 0x2 leaves a white quad
  white.
- `ms-nonmask`: `CreateRenderTarget(D3DMULTISAMPLE_NONMASKABLE, quality 2)`: `D3D_OK`, `GetDesc` says
  non-maskable, quality 2; the mask 0x3 and a white quad: 255 (the mask does not apply).
- `ms-a2c`: `D3DRS_ADAPTIVETESS_Y` = ATOC, `ALPHATESTENABLE` on, `ALPHAREF` 0xc0, `ALPHAFUNC` GREATEREQUAL, a
  white quad of alpha 0x80 over black on T: the 4x4 mean is in the band (the alpha test did not kill it, coverage
  thinned it). ATOC with `ALPHAFUNC` LESS 0x80 and a white quad of alpha 0xff: on T white (coverage, no alpha
  test); on a single-sample target alpha 0 is white (the pipeline's drawn control, which a NEVER pipeline would
  not have) and alpha 0xff black (the alpha test applies there). ATOC off, the alpha test off, `D3DRS_POINTSIZE`
  = A2M1, alpha 0x80: the band on T; after A2M0: white.
- `ms-srgbmask`: T, `SRGBWRITEENABLE` on, cleared black, the mask 0x3, a white quad; then a Clear of another
  target (a pass between, so the resolve is not folded); read: 188 in each channel (the linear average, encoded:
  parity, ruling 4); with sRGB write off: 128.
- `ms-blend`: T cleared red, SRCALPHA/INVSRCALPHA, a green quad of alpha 0x80: (128,128,0) inside.
- `ms-mrt`: T at index 0 and a second 4-sample target at index 1, `PS_MRT2` red, green: each read its own; the
  same pair under `SRGBWRITEENABLE` with the mask 0x3 and white, white over black: 188 on both. With a
  single-sample 64x64 texture at index 1, both cleared blue then `PS_MRT2`: T red, the texture blue (skipped), and
  a whole Clear to white reaches T alone: the texture stays blue (parity: mtld3d clears apart only a bound target
  of another *size*; one of target 0's size and another sample count is in no pass and gets no Clear). With a
  2-sample target at index 1 (when the device offers 2): the same.
- `ms-scissor`: T cleared blue; scissor (16,16)-(48,48), a red quad over all: (32,32) red, (15,32) and (48,32)
  blue; a viewport (0,0,32,64) and a full clip-space quad: (16,32) red, (48,32) blue.
- `ms-redraw`: T cleared black, a red quad over the left half, resolved into R1; a green quad over the right
  half, resolved into R2: R1 is red and black, R2 red and green; R1 is then bound at index 0 and a white quad
  drawn over its top half: white there, red and black below.
- `ms-samples`: for each of 2 and 8 samples the device offers: a target of that count, the mask of its lower half
  (0x1 of 2; 0x0f of 8), a white quad over black: 128.

The targets and msaa launches' refusal set is one line, not empty (execution ruling T1-1): `rt-msaa`'s and
`ms-desc`'s out-of-shape `StretchRect` cells log "IDirect3DDevice9::StretchRect (other than a multisample
resolve)" under core, and `dxmt/d3d9-check.sh` and `vm/dxmt/d3d9-gate.sh` hold that set (`stretch_refusals`).

## Review dispositions

- **R1** (design; 1 Critical, 5 Important, 15 Minor): C1 → the `StretchRect` arm right after the null and filter
  checks, judging the shape itself; the depth cell in `ms-desc`; I1 → `copies_bytes`; I2 → `CAP_RESOLVE` in
  `resolve_into`; I3 → the check's place in `create_core_device`; I4 → mutant (g) by count; I5 → `rt-msaa`'s
  resolves into a surface and the back buffer. Minors → `ms-desc`'s 16 samples, `ms-mask`'s 0x2 cell, `ms-a2c`
  with `NEVER` and the diffuse alpha, integer bands, `ms-back`'s second Clear, the sRGB cell of `ms-mrt`, the
  fallback dropped, an AUTOGENMIPMAP destination out of the shape, `msStart`'s A2M0, the stated gaps (lockable
  and swap effect, no 4-sample device), the additional swap chain's guard, ruling 4's encoded-average case, the
  sweep report's contents.
- **R2** (core; 1 Critical, 5 Important, 9 Minor): C1 → Task 3 and ruling 2; I1 → `resolve_into`; I2 → the
  unfolded resolve in `ms-srgbmask` and in `a_resolve_takes_the_last_view`; I3 → `read_back` refuses, the two
  back-buffer accessors; I4 → both pipeline fakes print, the pass line above 1 only; I5 → the count is checked
  before the open-pass shortcut. Minors → the variant order, `depth_samples`' `#[must_use]`, Task 4's usage
  bits, "pins, the validation layer proves", the retire wait as a stated gap, Present's every-frame resolve
  (Out, C4d), mtld3d's own alpha-to-coverage predicate.
- **R3** (exec; 1 Critical, 5 Important, 16 Minor): C1 → as R1's; I1 → mutant (g); I2 → as R2's; I3 → the VM
  hedge dropped; I4 → the script edits listed; I5 → the sweep's recipe. Minors → `samplers.rs` and the literal
  list, `CoreTextureFlags` (ruling 11), the fakes' lines, the logged depth drop and its pattern, 16 samples,
  mutant hygiene, `msaa-runtime` named, the helpers and constants, the stop-early cut, the process carry-overs
  (the cargo cache, the handle grep, headers, the probe's wait and retake pair, the chains), `read_back`, the
  wording conflicts, the cfg'd block, the budget rule.

## Execution notes (2026-11-16)

- Built as planned in seven commits on DXMTRS over 5f9e5caa: 665d5b91 (the default-pool plain surface), fa785176
  (dxmt-core: a resolve folds only into the render pass right before it), d451d853 (the fakes, and multisampled
  views through the core pinned), e89569a0 (the sample count in textures, passes and pipelines), cd4b6ca5 (resolve
  and the multisampled back buffer), 9d53514a (creation, `StretchRect`'s resolve, the read-back's refusal),
  82a588ba (the final review's two fixes). d3d11 and winemetal are untouched; dxmt-core changes in the optimizer's
  one guard. The rows were written by one Opus subagent and Tasks 2-7 by another from this plan, one writer a
  repository, this session running the lanes and gates; their commits carry their own co-author line.
- Controls: on the default runtime, on the Mac (default and msl) and in the VM (old WoW64, x64 and x86), every new
  row was green at its first run but one. `ms-mrt` expected a whole Clear to reach a bound extra of target 0's
  size and another sample count; mtld3d's does not (it clears apart only a bound target of another size, and one
  of another count is in no pass). The row and the appendix were corrected to parity, the core's Clear follows
  (`clear_size`), and mutant (g) became "the Clear reaches that slot" (ruling T1-2; cost if wrong: a title mixing
  sample counts across its targets, which D3D9 does not allow, never sees that target cleared, under both runtimes
  alike). mtld3d's own doc says such a target "is still owed every Clear": left for the maintainer.
- Changes from the plan's text, each ledgered: the rows agent's cells where the draft's could not prove what they
  meant to (the appendix above has them: `ms-edge`'s probes off the hypotenuse, `ms-a2c`'s LESS 0x80 pair,
  `ms-depth`'s drawn control); the refusal set of one line (T1-1); the rows' `offers()` asks main's `IDirect3D9`,
  not the device's (`GetDirect3D` is refused under core, which had hidden the 2- and 8-sample cells: T7-1);
  d3d9-rt depends on the workspace's bitflags; mtld3d's `copies_bytes` takes two formats (its two callers pass
  the same one twice); the resolve refusal for a format without `CAP_RESOLVE` is tested with RGBA16Uint;
  `device_capability_map` is rebuilt at each `StretchRect` resolve (C4d's input) (T5-1). The implementer wrote
  Tasks 5 and 6's code before their tests and proved each test by reverting its guard.
- Gates. On 9d53514a: the Mac's default, msl and core lanes; the stateblock conformance under core equal to
  default; all nine core launches under Metal's validation layer with no report; the seven mutants each red on its
  row and on nothing outside `msaa-*`; the VM's D3D10-12 gate (48 programs) and its D3D9 lanes on both runtimes,
  old and new WoW64. On the fix tip 82a588ba: the three Mac lanes, the stateblock conformance, the nine launches
  under validation and the VM's D3D9 lanes again; the plain build's translator key and imports unchanged,
  `d3d9-check` default, the x64 check all passed, the Mac's Wine conformance with only triaged sites. The plain
  build's first run (on 9d53514a) reported one untriaged site, device.c:1945 (test_cursor's cursor handle, on a
  Mac in use); it did not recur at the fix tip and no cursor code changed: ruled a flake of that run (T8-1; cost if
  wrong: an intermittent cursor-handle difference of the default runtime, older than this slice, goes
  unexamined). The mutants and the D3D10-12 gate were not rerun for the two front-end lines of the fix.
- Final review (Opus): no Critical, two Important, four Minor. Important 1: `D3DRS_MULTISAMPLEMASK` was ignored on
  a multisampled back buffer nothing had bound (a null slot 0 read as no multisampling; mtld3d reads the back
  buffer's type). Important 2: a Z or stencil Clear wrote a depth-stencil of another sample count than target 0's
  (mtld3d: nothing). Both fixed in 82a588ba, each with a cell that was red on the old tip under core alone
  (`ms-initial`, `ms-mismatch`'s Z Clear) and green on the default runtime. Two Minors fixed with them (a comment,
  a doc). Deferred: `get_render_target_data`'s multisampled-source check and log text are a copy of mtld3d's, not
  shared; the optimizer's guard holds the fold back across any live pass, also one that does not touch the
  resolve's textures (ruling 2 states the cost: one resolve pass of its own).
- Probe (`build/c4b3d/s5/probe.md`): this slice read the core at 3.62 / 3.62 ms a frame (tex / texdepth) against
  mtld3d's 1.82 / 1.80, and C4b-3d4's tip rebuilt the same day 3.47 / 3.58 against 1.58 / 1.67: no difference
  beyond one run's spread. The Mac was in use the whole day (the job never saw the load it waited for), so the
  quiet-Mac figure C4b-3d4 owed is still owed, to C4d's first probe.
- Sweep (`build/c4b3d/s5/sweep.md`; measured, not a gate, nothing fixed): under core none of `visual`, `device`
  and `d3d9ex` runs to its end. Each process ends within seconds at the first refused call its test does not
  survive: `IDirect3DSurface9::LockRect` of a render target (`visual`, in `stretchrect_test`'s read-back, 8 failed
  lines before it), `IDirect3DVertexBuffer9::Lock` (`device`, in `test_draw_primitive`, 30),
  `IDirect3DDevice9::GetDirect3D` (`d3d9ex`, in `test_reset`, 19). No panic, no hang, no validation report (a
  process that dies prints no "executed" count). Until those three answer, a sweep cannot see past them; the
  others met on the way (`StretchRect`'s other shapes, `Reset`,
  fullscreen devices, the cursor calls, `CreateQuery`, the `Ex` creators, the swap chain's display-mode getters)
  fail their tests without ending them.
- Incidents, none in the code: three jobs were started with the lock command held in a shell variable, which zsh
  does not split, and ended with 127 before running anything (rerun with the command spelled out); the sweep
  job's first form stopped at its own self-check, whose pattern matched the script's header comment (a second job
  file with a narrower check).
