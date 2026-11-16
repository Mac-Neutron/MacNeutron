# DXMTRS C4b-3d4: render targets at index 1-3 on D3D9's core runtime

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** under `runtime.mode=core`, `SetRenderTarget` takes index 1-3 and `GetRenderTarget` answers them; a draw's
pass attaches every bound target that has target 0's size (up to four), a programmable pixel stage writes
`oC1..oC3` to them, each with its own format, its own write mask (`D3DRS_COLORWRITEENABLE1..3`), the shared blend
with its own destination-alpha clamp, and hardware sRGB when every attached target has an sRGB view; `Clear`
reaches every bound target; a target at index 1-3 is read back. Same pixels as under mtld3d, which keeps its
behaviour.

**Architecture:** fourth of C4b-3d's five slices (3d1 targets, 3d2 depth-stencil, 3d3 clears/scissor/sRGB/blending,
done; **3d4**; 3d5 MSAA and the Wine suite sweep). dxmt-core already carries eight colour slots through its pass,
its pipeline description, its optimizer and its tile barrier (M §A): this slice changes none of it and fills what
d3d9-rt left at "slot 0 only". Four DXMTRS commits, the refusal lifted by the last, so every commit is safe to
run:

1. **Fakes** (Task 2). `dxmt-fakes` prints colours 1-7 of a pass and of a pipeline as lines of their own (the
   existing lines keep their text), so d3d9-rt's tests can see a second attachment.
2. **Draws** (Task 3). d3d9-rt keeps three more targets by reference (`extras`). The *pass set* is target 0 and
   every extra that resolves, has target 0's size and is not a subresource already in the set (mtld3d's size rule;
   a duplicate never reaches Metal twice). The draw's key is the one source of what a pass attaches:
   `PipelineKey::extras` names each attached slot's format, write mask and blend, d3d9-rt opens the pass on exactly
   those slots in those formats, and a key the bound targets cannot make is refused, never asserted. The pixel
   variant's `color_out_mask` is the set's extras shifted by one, for a programmable stage. Hardware sRGB is
   all-or-nothing over the set, as mtld3d's.
3. **Clear** (Task 4). `Clear(TARGET)` goes to each bound colour target, in the pass set or not, each judged
   against its own extent (`clear_area`), as one single-view core clear a target; d3d9-rt takes the linear colour
   and encodes it itself when the view it clears is not an sRGB one.
4. **The entry point** (Task 5). `SetRenderTarget(1..3)` binds (mtld3d's bookkeeping: the slot's reference, the
   AUTOGENMIPMAP regeneration on leaving, no viewport or scissor reset), `SetRenderTarget(1..3, NULL)` unbinds in
   d3d9-rt too; the refusal goes.

**Tech Stack:** Rust 1.99.0 (DXMTRS: dxmt-fakes, mtld3d-core, d3d9-rt, the d3d9.dll front end), C++
(`d3d9_triangle`), sh gates.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C4; the C4b-3d line and 3d1-3d3's paragraphs).
Maps: `build/c4b3d/map-3d4.md` (this slice; cited as M §n and its hazards as F.n), `build/c4b3d/map.md`,
`build/c4b3d/map-3d3.md`. Reviews: `build/c4b3d/s4/review-{1-design,2-core,3-exec}.md` (folded; dispositions at
the end, cited as R1-R3).

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check,
  and a crossing reached through a `DeviceInner` method is guarded inside that method (`unbind_extra_render_target`
  is one: state-block Apply and `reset()` reach it); a core-only struct field or private helper is cfg'd in the
  struct, in every literal and at its definition. No mtld3d code moves in this slice; mtld3d-core gains functions
  and one key field only.
- **dxmt-core (`src/dxmt/src`), d3d11 and winemetal do not change.** `src/dxmt/fakes` (test support, linked into
  no shipped binary) gains printed lines. Should a task find that dxmt-core must change (the optimizer mishandling
  a pass with a hole, the tile barrier with several formats), it stops: the change is a ruling, a commit of its own
  with its unit test before the task's, and the x64 `dxmt/check.sh` and the VM's `vm/dxmt/gate.sh` then gate it
  (Task 6 runs the first anyway and the second only then).
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code
  (`sh tools/rust-check.sh > log 2>&1 && git commit ...`, never through `tail`); a d3d9-rt or mtld3d-core API
  change carries its front-end call sites and every literal of a changed struct in the same commit (M §C.2 lists
  them: `PipelineKey` at `core_draw.rs:498`, `pipeline_key.rs:246`, `pipeline_key/tests.rs:122, 146, 371, 384, 414,
  439, 445`, rt `fixture.rs:102`, `depth/tests.rs:107`, `target/tests.rs:268`, `draw/tests.rs:72, 392, 699, 977`;
  `PipelineSource` at `pipeline_key.rs:312`). The PE front end compiles only inside rust-check: grep the import
  list of an edited front-end file before running it. New files are `git add`ed before it; a new or removed
  Clone/Copy derive needs `sh tools/audit.sh --update-derives`.
- No new dependency. No `HashMap`. No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body; lines
  of 100 columns at most. **No struct gains a `bool` field and no function a fourth `bool` parameter**
  (CONVENTIONS "Booleans pack"): this plan adds none (`core_ps_variant` has three and gains a `u8`).
- Edits only in the worktrees `build/c4b3c/dx` (branch `c4b3d`, base 8bca6b74) and `build/c4b3c/mn` (branch
  `c4b3d-mn`, base fb23851), cargo target `build/c4b3c/cargo`; this slice's gate scripts in `build/c4b3d/s4/gates`
  (`head.sh` there; state names `c4b3d4-*`), logs and reviews in `build/c4b3d/s4`. Builds and GPU jobs under the
  pipeline lock (`/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`), `$D` and the main checkout
  restored by each job's trap. No make in a worktree. A job tests **committed** tips (`DXTIP`/`MNTIP` pin them; a
  later commit waits until the job that must test the old tip has begun); a job script that has run is never
  edited (a changed job is a new file); each run of the per-task lane takes its own folder (`JT=jt3` ...).
- Commands: `. build/c3/gates/env.sh; export MTLD3D_RUST=1.99.0
  CARGO_TARGET_DIR=$HOME/Documents/MacProton/build/c4b3c/cargo`; crate tests as `tools/rust-check.sh`'s test leg
  runs them (the `dynamic_lookup` rustflags for d3d9-rt and dxmt-core); mingw compilers at
  `build/dxmt-src/llvm-mingw-own/bin/`.
- A refusal answers `D3DERR_NOTAVAILABLE`, logged once by name. rt tests use device handles no other test uses:
  **0x340-0x37f** here, one a test (Task 3 Step 0 greps that none is taken).
- Every DXMTRS commit is safe to run under core: Tasks 2-4 leave the refusal of index 1-3 standing, so no extra is
  ever bound and their Mac lanes read **exactly as the control** (Task 1 Step 5); Task 5 lifts it. MacNeutron's
  core lanes are red on the `mrt` launch and on `rt-mrt` between Task 1's rows and Task 5 by design; should the
  slice stop before Task 5, the `mrt` kind leaves the lists and `rt-mrt` goes back to its refusal form in the
  commit that pins.
- Per task one lane (the Mac's `d3d9-check` on default and core, job `jt`); full gates once, in Task 6. Benches and
  probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents too)
  while the x64 `dxmt/check.sh` or a bench runs. Autonomous work stops at 95% weekly usage.

## Scope

In: `SetRenderTarget` and `GetRenderTarget` at index 1-3 (a render-target texture's level or cube face, a
`CreateRenderTarget` surface, a back buffer); draws to up to four colour targets with a programmable pixel stage
writing `oC0..oC3` and with the fixed-function pixel stage (colour 0 alone); a hole (an empty slot between bound
ones); targets of different formats; `D3DRS_COLORWRITEENABLE1..3`; blending on every attached target with each
target's own destination-alpha clamp, and the blend factor colour; `D3DRS_SRGBWRITEENABLE` over several targets;
`Clear` (whole, rects, scissor, viewport) over every bound target; the scissor, the viewport and the depth-stencil
with several targets; a bound target of another size than target 0 (skipped by draws, reached by `Clear`);
`GetRenderTargetData` of a target at index 1-3; AUTOGENMIPMAP targets at index 1-3.

Out (owners): multisampling, and with it mtld3d's sample-count half of the "takes part" rule (3d5; under core no
multisampled surface exists yet); `StretchRect`, `ColorFill`, `Reset` (and with it `reset_targets_to_implicit`
under core), an additional swap chain's back buffer as a target of its own (C4c; under core it aliases the
device's, C4b3d1-4); per-draw dedup of the set's look-ups and `GetDesc` calls (C4d); the same surface at two
indices, which D3D9 does not allow (ruling 3); mtld3d's "RT0 drop" (it needs no extra bound).

## Review Focus

1. **A pass and its pipeline name the same slots in the same formats, always.** A mismatch is undefined behaviour
   in Metal with the validation layer off (M §A.2). The key names the slots; d3d9-rt attaches exactly those; a key
   the bound targets cannot make (a stale extra, a wrong format, a size that no longer matches) is
   `DrawRefused::Target` before any record; the pixel stage exports exactly the key's extras that the program
   writes; a hole is `Target::NONE` in the pass, `INVALID` in the pipeline and in the tile-barrier key, and no
   export. rt `the_key_names_the_pass`, `a_key_the_targets_cannot_make_is_refused`; rows `mrt-hole`, `mrt-nobind`,
   `mrt-unwritten`; every core launch under Metal's validation layer with no report (Task 6).
2. **Two passes that differ in any slot never merge.** dxmt-core's `same_targets` compares the first
   `render_target_count` slots only (M §A.3): the count is the highest attached slot + 1. Row `mrt-switch` (the
   only proof: a fakes test cannot see a merge); mutant (e).
3. **Four index conventions meet in one function** (F.1): slot `i + 1` is `extras[i]`, `COLORWRITEENABLE{i+1}`,
   bit `i + 1` of the program's written mask and of the variant's `color_out_mask`, and `colors[i + 1]` on the
   wire. mtld3d-core `core_extras_follow_mtld3d`; row `mrt-hole` (distinct masks and formats at slots 1 and 2 with
   slot 1 empty).
4. **Each target blends against its own alpha, encodes once, and takes its own mask.** rows `mrt-mask`,
   `mrt-destalpha`, `mrt-blend`, `mrt-srgb` (a set with twins, a mixed set), `mrt-srgbclear`.
5. **`Clear` reaches every bound target and nothing else**: a target of another size too, over its own extent; an
   unbound one not; the masks, blending and scissor of draws never. Rows `mrt-clear`, `mrt-size`.
6. **A target that leaves the set leaves the pass**: unbinding, rebinding, a change of target 0's size, a texture
   forgotten while bound. rt `an_extra_change_ends_the_pass`, `forgetting_an_extra_unbinds_it`,
   `a_target_0_of_another_size_drops_the_extra`; rows `mrt-switch`, `mrt-size`.

---

### Task 1: rows (MacNeutron) and the gate scripts

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s4/gates/*.sh` (scratch, not committed).

- [ ] **Step 0: the gate scripts** (copied from `build/c4b3d/s3/gates` with `s3`→`s4`, `c4b3d3-`→`c4b3d4-`; none
  has run). `jq.sh`: the validation-layer loop's list gains `mrt`. `jm.sh`: rewritten for this slice's mutants
  (Task 6 Step 2: the letters, the `x:row` pairs, the filter `^mrt-` and `^targets-rt-mrt$`, `mutants.txt`).
  `jc.sh`: `JC_D3D9_ONLY=1` is how Task 6 runs it. `chainA.sh`, `chainB.sh`: written at Task 6 with the tips.
  `/usr/bin/grep -n 's3\|3d3\|bl-\|blend' build/c4b3d/s4/gates/*.sh` — Expected: only `jq.sh`'s kind list.
- [ ] **Step 1: write.** `rt-mrt` (targets kind) becomes a result in every lane: `SetRenderTarget(1, s)` is
  `D3D_OK`; `GetRenderTarget(1)` answers `s` (pointer equal, released); `GetViewport` and `GetScissorRect` are what
  they were before the call (a 13,17,40,30 viewport set first); `SetRenderTarget(1, nullptr)` is `D3D_OK` and
  `GetRenderTarget(1)` then `D3DERR_NOTFOUND` with a null out; `SetRenderTarget(4, s)` and `SetRenderTarget(1,
  sysSurf)` are `D3DERR_INVALIDCALL`. The `mrt` launch kind (`d3d9_triangle mrt [strict]`): the blend kind's
  device (`if (depth || blend || mrt || depthBench)`), prologue and per-row runner (`reset() && depthReset() &&
  blendStart() && r.run()`), budget 180 s, the 19 rows of the appendix. New token arrays `PS_MRT2` (`ps_2_0`: `mov
  oC0, c0`, `mov oC1, c1`) and `PS_MRT4` (`oC0..oC3` from `c0..c3`), drawn with `VS_POS`/`DECL_XYZ` as `rt-ps`
  does; helpers `bindExtra(DWORD slot, IDirect3DSurface9 *)` (CK), `mrtConsts(const float (*c)[4], int n)`,
  `readRt(surface, size, format, x, y, expect[, mask])` over `readTarget`. A row unbinds index 1-3 itself before
  it presents or returns (`frames()` and `until()` rebind index 0 only, F.17). Headers: the kind, the counts, the
  targets launch's one refused call.
- [ ] **Step 2: scripts.** `d3d9-check.sh`: the header; `targets_refusals` loses the index line (one line left);
  `launch ours fresh mrt 240 mrt $strict` and `check mrt "<rows>"` after the blend launch; `refusals_ok`'s
  zero-line arm becomes `blend|mrt)`. `d3d9-gate.sh`: the header, `targets_refusals`, `for r in mr64-fresh
  mr32-fresh` row loops, the core block (`runtime "$G/logs/$r"` with no kind: zero refusals). `d3d9.sh`: the
  header, two `tri` lines (`mr64-fresh`, `mr32-fresh`, 240 s, `mrt $strict`) after `bl32-fresh`.
- [ ] **Step 3: compile** `d3d9_triangle.cpp` with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall
  -Wextra`) — Expected: no diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle mrt rows in every D3D9 lane (DXMTRS C4b-3d4)`.
- [ ] **Step 5: the controls.** Job `JT=ja` (DXMTRS 8bca6b74, this commit). Expected: default `d3d9-check: ok` with
  every `mrt` row and `rt-mrt` ok; core: `targets-rt-mrt` red, the `mrt` launch red on every row that binds index
  1-3 (all but `mrt-nobind`), `mrt-runtime` red (refusal lines), every other launch as at 3d3's close. A row red
  on the default runtime is a wrong row or a defect of mtld3d: fix the row; a defect is ledgered as a ruling, the
  row then asserts what both runtimes should give and is listed as red-on-default until the defect's own fix (this
  slice's only if it is a one-line fix). An expectation of the appendix that mtld3d does not give is corrected
  there (the appendix marks the ones this plan could not derive from code: `mrt-size`'s larger target,
  `mrt-srgb`'s mixed set).
- [ ] **Step 6: the VM's default lanes once** (`jcr`: old WoW64, x64 and x86): `mr64-fresh` and `mr32-fresh` green
  inside their budget — Expected: ok; a budget miss raises the kind's budget before any core work.

### Task 2: dxmt-fakes prints colours 1-7

**Files:** Modify `src/dxmt/fakes/src/lib.rs`.

- [ ] **Step 1: implement.** `fake_render_encoder`: after the colour-0 lines, for `i in 1..8` with
  `pass.colors[i].texture != 0`: `  color{i} load={} store={} clear={},{},{},{}` and `  target{i} #{}` (the view's
  handle, as `target0` prints it, without the size). `fake_new_render_pipeline_with_vertex_descriptor`: after `pso
  color0=`, for `i in 1..8` with `info.colors[i].pixel_format != 0`: `  pso color{i}={} mask={} blend={}` (format,
  `write_mask`, `blending_enabled`). The colour-0 lines keep their text (rt's tests match them exactly, M §A.6).
- [ ] **Step 2: run** dxmt-core's, d3d11's and d3d9-rt's tests and `rust-check` — Expected: ok, no test moved (no
  test outside d3d9-rt reads a fake's transcript: `/usr/bin/grep -rln 'color0 load\|pso color0' src tests` names
  d3d9-rt and the fakes only).
- [ ] **Step 3: commit** `dxmt-fakes: colours 1-7 of a render pass and of a pipeline`.

### Task 3: draws to several colour targets (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ `pipeline_key/tests.rs`); `src/d3d9/rt/src/device.rs`,
`device/target.rs`, `device/recorder.rs`, `device/draw.rs`, `device/textures.rs` (`forget_texture`),
`device/fixture.rs` and their tests; `src/d3d9/src/device/core_draw.rs`, `core_arms.rs` (`target_desc`'s caller).

**Interfaces (produces):**
- mtld3d-core `pipeline_key.rs`:
  `#[derive(Clone, Debug, PartialEq, Eq, Hash)] pub struct CoreExtra { pub format: u32, pub write_mask: u8, pub
  blend: Option<CoreBlend> }` (a colour target above index 0 as the pipeline declares it: its attached
  `WMTPixelFormat`, its `MTLColorWriteMask`, its blend);
  `PipelineKey { .., pub extras: [Option<CoreExtra>; 3] }` and the same field on `PipelineSource` (cloned by
  `pipeline_source`); `impl PipelineKey { pub fn color_formats(&self) -> [u32; 4] }` (colour 0's, then each
  extra's, 0 for none);
  `pub fn core_extras(formats: [u32; 3], rs: &[u32; RENDER_STATE_COUNT], ps_written: u8, has_alpha: u8) ->
  [Option<CoreExtra>; 3]`: `formats[i]` is slot `i + 1`'s attached format (0: not in the pass → `None`);
  `write_mask` is `d3d_to_metal_write_mask(enum_value(rs, D3DRS_COLORWRITEENABLE1 + i)).bits()` when `ps_written`
  has bit `i + 1` (the program writes `oC{i+1}`; the fixed-function stage's mask is 1), else 0 (declared,
  unwritten, preserved: mtld3d's `extra_write_mask`); `blend` is `core_blend(rs, has_alpha & (1 << i) != 0)`
  (mtld3d's `extra_blend_factors`: the shared effective blend, this target's own destination-alpha clamp);
  `pub const fn extra_present(formats: [u32; 3]) -> u8` (bit `i` for a non-zero `formats[i]`);
  `core_ps_variant(base, ps_is_prog, point_list, linked_input_mask, has_depth, extra_present: u8)`: a programmable
  stage's `color_out_mask` is `extra_present << 1` (mtld3d's draw: `present_mask << 1`; bit 0 is implied); the
  fixed-function stage keeps `base`'s.
- d3d9-rt:
  `CoreDevice { .., extras: [Option<TargetRef>; 3], open_colors: [u32; 4] }` (`open_colors` replaces
  `open_format`: the format each slot of the open pass attached, 0 for none);
  `pub fn set_extra_target(&mut self, slot: usize, target: Option<TargetRef>) -> bool`: slot 1-3; false, and
  nothing changed, for another slot or a target that does not resolve; a change ends the open render pass;
  `pub fn target_desc(&self, srgb_write: bool) -> Option<((u32, u32), [u32; 4])>`: target 0's size and the format
  each slot of the pass set attaches under `srgb_write` (0: the slot is not in the set). The set: target 0, and
  each extra, in slot order, that resolves, has target 0's width and height, and is not the `TargetRef` of target 0
  or of a lower slot in the set. Under `srgb_write` every format is its sRGB twin's when **every** member has one,
  and every format is the stored one otherwise (mtld3d's `srgb_write_is_attachable`);
  `DrawRefused::Target` (the key names colour formats the bound targets do not make);
  private `fn pass_set(&self) -> [Option<Shape>; 4]` (the one place the set is decided; `target_desc`, `open_pass`
  and Task 4's clears read it).
- Behaviour: `draw_up` takes `colors = key.color_formats()`; unless a render pass with `open_colors == colors` is
  open it requires `target_desc(false)` or `target_desc(true)` to answer exactly `colors`, else
  `Err(DrawRefused::Target)` before the pipeline is built (the check replaces the colour-format debug assert).
  `open_pass(with_depth, colors)`: reopens when the depth kind or `colors` differs; `start_render_pass(planes, 0,
  count, None)` with `count` the highest non-zero slot + 1; for each non-zero slot an attachment (`READ | WRITE`)
  of that slot's target through its view in that format, `LOAD`/`STORE`, into `targets.colors[slot]`, and
  `tile_barrier_pso_key[slot] = format`; `[8] = 1`; the extent as today (every set member has target 0's size).
  `build_pipeline`: `colors[i + 1]` from `source.extras[i]` (format, mask, and the blend fields through the helper
  colour 0 uses). `set_target`, `set_extra_target`, `set_depth` and `forget_texture` end the open pass on a
  change; `forget_texture` empties every extra slot that holds the id.
- Front end: `core_target_of` answers `((u32, u32), [u32; 4], bool)` (the set's formats; hardware as today, from
  colour 0); `extras = core_extras(formats[1..], &rs, written, alpha)` with `written = ps.color_out_mask()` for a
  programmable stage and 1 otherwise, and `alpha` bit `i` from `slot_has_alpha(dev, i + 1)` (`target_has_alpha`
  with the slot as a parameter: a back buffer, or an unknown format, stores alpha), computed only under
  `ALPHABLENDENABLE`; `core_ps_variant(.., extra_present(formats[1..]))`. `core_arms::clear` reads
  `target_desc(true)`'s colour 0 as before. A draw refused as `DrawRefused::Target` answers `D3D_OK` like the
  other silent drops (deferred minor of 3d3's review, same class; unreachable while the front end builds its key
  from `target_desc`).

- [ ] **Step 0:** `/usr/bin/grep -rn '0x3[4-7][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src` — Expected: no
  device handle in 0x340-0x37f.
- [ ] **Step 1: failing tests.** mtld3d-core: `core_extras_follow_mtld3d` (an absent slot is `None` whatever the
  states; slot 2 alone present takes `COLORWRITEENABLE2` and bit 2 of the written mask, not 1's; an unwritten
  present slot has mask 0 and keeps its format; blending off → `blend: None`; DESTALPHA/INVDESTALPHA with
  `has_alpha` 0b010 → slot 1 One/Zero, slot 2 DestinationAlpha/OneMinusDestinationAlpha; the operations equal
  colour 0's), `extra_present_bits`, `core_ps_variant_rules` re-tabled (a programmable stage with `extra_present`
  0b101 → `color_out_mask` 0b1010; 0 → 0; the fixed-function stage keeps the base's), the key table grows by
  `extras` (`[Case; 19]`), `color_formats_lists_the_slots`. rt (the fixture's `key()` gains `extras: [None, None,
  None]`; a helper `key_with(extras)`): `the_key_names_the_pass` (two extras of the fixture's size bound, a key
  naming both: one encoder with `color1`/`target1` and `color2`/`target2` lines and `pso color1=`, `pso color2=`
  with the key's masks; the same targets with a key naming none: `Err(Target)`),
  `a_key_the_targets_cannot_make_is_refused` (a format the slot does not store; an extra the device has not bound;
  nothing recorded, no pipeline built), `a_hole_stays_empty` (slots 0 and 2: `color2`/`target2`, no `color1`; `pso
  color2=` and no `pso color1=`), `an_extra_of_another_size_is_not_in_the_set` (`target_desc` answers 0 for it; a
  key naming it is refused), `a_duplicate_is_not_in_the_set` (target 0's own `TargetRef` at slot 1; the same
  texture level at slots 1 and 2: slot 1 alone), `a_target_0_of_another_size_drops_the_extra`,
  `an_extra_change_ends_the_pass` (draw, `set_extra_target` to another texture, draw: two encoders; to the same:
  one), `forgetting_an_extra_unbinds_it`, `srgb_twins_are_all_or_nothing` (BGRA8 + RGBA8: both twins under
  `srgb_write`; BGRA8 + R32F: both stored; the R32F extra of another size: colour 0 the twin's),
  `the_blend_reaches_each_extra` (`pso color1= .. blend=1`). Expected: do not compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** mtld3d-core's and d3d9-rt's tests, `rust-check`, `audit.sh --update-derives` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: draws to several colour targets in the core runtime's device`.
- [ ] **Step 5: lane** `JT=jt3` — Expected: both runtimes exactly as the control (`ja`): default ok; core red on
  `targets-rt-mrt` and the `mrt` launch only.

### Task 4: Clear over every bound colour target (d3d9-rt, front end)

**Files:** Modify `src/d3d9/rt/src/device/recorder.rs`, `device/target.rs` and tests;
`src/d3d9/src/device/core_arms.rs`.

**Interfaces (produces):**
- d3d9-rt: `pub fn color_size(&self, slot: usize) -> Option<(u32, u32)>` (the colour target bound at slot 0-3,
  in the pass set or not; `None` for an empty slot or one that does not resolve);
  `pub fn clear(&mut self, slot: usize, linear: [f32; 4], srgb_write: bool)` and `pub fn clear_rects(&mut self,
  slot: usize, rects: &[[u32; 4]], linear: [f32; 4], srgb_write: bool)`: the slot's target through the view a pass
  made under `srgb_write` attaches it in (a set member: the set's all-or-nothing answer, so the whole Clear folds
  into that pass; a bound target outside the set: its own twin when it has one, mtld3d's scoped pass that binds it
  alone); `linear` is stored as given through an sRGB view and through `linear_to_srgb_rgba` otherwise when
  `srgb_write` (mtld3d's `resolved_clear_rgba`: each target encoded exactly once). A slot already cleared in this
  call as a duplicate is cleared again, harmlessly.
- Front end `core_arms::clear`: the colour part loops `for slot in 0..4` over `core.color_size(slot)`: `viewport =
  drawn_rect(&dev.viewport(), extent)`, `clear_area(regions, viewport, extent)` → `clear(slot, ..)` or
  `clear_rects(slot, ..)`; the CPU encode and the `hardware` test leave the front end. The depth part is
  unchanged (its viewport against target 0's size).

- [ ] **Step 1: failing tests.** rt: `a_clear_reaches_each_bound_target` (two extras, three clears, a draw naming
  all three: one encoder whose `color0`, `color1`, `color2` lines say `load=2` with the clear colour: N single-view
  clears fold into the N slots, M §A.3), `a_clear_of_a_target_outside_the_set_is_its_own_pass` (an extra of
  another size: its Clear is an encoder of its own on its view; the draw's pass has no `color1`),
  `a_clear_encodes_once` (under `srgb_write`: a BGRA8 target alone takes the linear colour through the twin's
  view; BGRA8 with an R32F extra: both take the encoded colour through their stored views; the fixture's existing
  3d3 clear tests re-stated on `clear(0, ..)` with linear inputs), `a_rect_clear_reaches_one_slot` (`clear_rects(1,
  ..)`: a pass on the extra's view alone), `color_size_lists_bound_slots`. Expected: do not compile.
- [ ] **Step 2: implement; run** tests and `rust-check`.
- [ ] **Step 3: commit** `d3d9: Clear over every bound colour target on the core runtime`.
- [ ] **Step 4: lane** `JT=jt4` — Expected: exactly as the control (the 3d3 clear rows `cl-*`, `sr-clear`,
  `rt-srgb` still ok under core: their Clear now encodes in d3d9-rt).

### Task 5: `SetRenderTarget` at index 1-3 (front end)

**Files:** Modify `src/d3d9/src/device.rs` (`device_set_render_target`'s core arm,
`unbind_extra_render_target`).

- [ ] **Step 1: implement.** The core arm runs for every slot: `core_target()` and the parent's
  `core_flush_dirty` as today; slot 0 → `set_target`, slot 1-3 → `set_extra_target(slot, Some(target))` (false:
  the `UNTARGETED` refusal); the AUTOGENMIPMAP swap on `cur_autogen_rt_ids[slot]`; `replace_render_target(slot,
  ..)`; slot 1-3 returns `D3D_OK` there (no viewport or scissor reset, no snapshot section: mtld3d's, M §B.1);
  slot 0 goes on as today. The refusal "IDirect3DDevice9::SetRenderTarget (index above 0)" and its comment go.
  `unbind_extra_render_target`, under core (cfg'd): `core.texture_generate_mips(old_id)` for the slot's autogen
  id, and `core.set_extra_target(slot, None)`.
- [ ] **Step 2: run** `rust-check` — Expected: ok (the front end compiles only there).
- [ ] **Step 3: commit** `d3d9: SetRenderTarget at index 1-3 on the core runtime`.
- [ ] **Step 4: lane** `JT=jt5` — Expected: both runtimes `d3d9-check: ok`: the 19 `mrt` rows, `rt-mrt`, the
  targets launch's log with exactly its one refusal line, the mrt launch's with none.

### Task 6: gates, mutants, probe

- [ ] **Step 1: `jq`** (Mac): `DXMT_D3D9_CORE=1` build; `dxmt-tests`; `d3d9-check.sh` default, `D3D9_BACKEND=msl`,
  `D3D9_RUNTIME=core`; `dxmt/mtld3d-conformance.sh D3D9_RUNTIME=core --only stateblock` equal to default; **every
  core launch** (`shaders`, `state`, `textures`, `fixed`, `targets`, `depth`, `blend`, `mrt`) once under the Metal
  validation layer with no report and the rows still ok. Expected: all ok.
- [ ] **Step 2: mutants** (`jm`, one build and one core `d3d9-check` each; each red on its row, and on no row of
  another launch but `targets-rt-mrt` where said): (a) `core_ps_variant`: `color_out_mask = extra_present` (no
  shift) → `mrt-two`; (b) `core_extras` reads `D3DRS_COLORWRITEENABLE` for every slot → `mrt-mask`; (c) the front
  end's `alpha` is colour 0's for every slot → `mrt-destalpha`; (d) `pass_set` drops the size test → `mrt-size`;
  (e) `open_pass` starts the pass with count 1 → `mrt-switch`; (f) `target_desc` picks each member's twin on its
  own → `mrt-srgb`; (g) `core_arms::clear` loops `0..1` → `mrt-clear`.
- [ ] **Step 3: plain build** (`jr`): no `DXMT_D3D9_CORE`; translator key and d3d9.dll imports unchanged;
  `d3d9-check.sh` default ok; the Mac's default-runtime Wine conformance with no new failing site; **x64
  `dxmt/check.sh`: all passed** (idle Mac).
- [ ] **Step 4: VM** (`jc` with `JC_D3D9_ONLY=1`: dxmt-core did not change, ruling 6): default and core lanes, old
  and new WoW64, x64 and x86: `d3d9-gate` ok (a default lane short only on executed-test floors is rerun once
  before it is blamed).
- [ ] **Step 5: probe** (`jp`, once, idle Mac): this tip on its own full build, `--bench 2000 <0|600> tex` and
  `tex depth`, mtld3d and core; written to `build/c4b3d/s4/probe.md` beside 3d3's figures (core 2.18 / 2.25,
  mtld3d 1.70 / 1.77). The single-target draw now asks for a set of four and builds three empty extras: a core
  delta against 8bca6b74 below the 0.32 ms single-run spread is reported as not measurable.

### Task 7: review, spec, pin, push

- [ ] Whole-branch review (Opus; the plan's Review Focus verbatim; the ledger's rulings); one fix pass, each fix
  RED→GREEN with the suite green; minors deferred to the ledger.
- [ ] Spec §5: a C4b-3d4 paragraph after 3d3's. Execution notes at this plan's end. `dxmt/pins` to the DXMTRS tip.
- [ ] Privacy scan of both ranges; push DXMTRS `c4b3d` → `main`, then MacNeutron `c4b3d-mn` → `main`; post-push
  check from main (plain build, tests, `d3d9-check`, x64 check); CI; scratch of this slice deleted by full path;
  memory.

## Rules check

- **No dxmt-core, d3d11 or winemetal change.** `dxmt-fakes` (test support) prints more lines; no test outside
  d3d9-rt reads them.
- **No move and no duplicate of mtld3d's code.** `core_extras` calls `core_blend` (3d3's, which is
  `effective_blend_rs` and `d3d_to_metal_blend_rt`) and `d3d_to_metal_write_mask`; it restates
  `PipelineRsBits::extra_write_mask`'s three-line rule (present and written → the slot's mask, else empty) over
  render states instead of a snapshot, as `core_blend` restates the snapshot's blend fill: stated here as the one
  near-duplicate, pinned against mtld3d's by `core_extras_follow_mtld3d` using the cases of mtld3d's
  `unwritten_extra_target_gets_an_empty_write_mask` and `extra_target_blend_factors_clamp_on_their_own_alpha`.
  The pass-set rule restates `recompute_extra_present_mask`'s size test (one comparison) in d3d9-rt, which has no
  `PassState`.
- New types: `CoreExtra` (Clone: a key is cloned into the pipeline map; no Copy). New variant
  `DrawRefused::Target`. New fields: `PipelineKey::extras`, `PipelineSource::extras`, `CoreDevice::extras`,
  `CoreDevice::open_colors` (replacing `open_format`). No new `bool` field or parameter. The derive inventory
  follows (`audit.sh --update-derives`).
- No new file, no static, no config key, no wire field, no dependency, no lint suppression. Metal wire values come
  from `winemetal_sys::wire` (d3d9-rt) and D3D9 constants from `mtld3d-types`.
- Removed: the refusal literal "IDirect3DDevice9::SetRenderTarget (index above 0)"; the front end's Clear-colour
  encode (moved into d3d9-rt's clear, its one user).

## Rulings (planning)

1. **The refusal is lifted last** (fakes, draws, Clear, entry point), so Tasks 2-4 move no row. Why: a bound extra
   with any of the pass, the pipeline, the masks, the blend or the Clear missing is wrong pixels or a pass/pipeline
   mismatch; interim refusals by name would be code written to be deleted. Cost if wrong: none in the result; the
   lanes of Tasks 3 and 4 prove no regression only, and the per-feature RED→GREEN evidence is the unit tests' and
   the mutants'.
2. **An extra of another size than target 0 is bound, skipped by draws and reached by `Clear`**, as mtld3d has it
   (Windows accepts the bind: Wine's `test_clear_different_size_surfaces`). D3D11's rule (refuse the bind) is not
   D3D9's. Cost if wrong: none against mtld3d; a title that draws to targets of unequal sizes loses the extra's
   pixels under both runtimes.
3. **A duplicate is not in the pass set**: an extra that is target 0's subresource, or a lower set member's, is
   skipped by draws (and cleared once more by `Clear`, harmlessly). mtld3d would hand Metal the texture twice. No
   row: the control itself could abort under Metal. Under core this also skips an additional swap chain's back
   buffer bound beside the device's (they alias, C4b3d1-4), where mtld3d draws to both. Cost if wrong: a title
   that binds one surface twice, or two swap chains' back buffers at once, misses the higher index's pixels under
   core; the first is invalid D3D9, the second waits for C4c's swap chains.
4. **The key decides the pass; d3d9-rt verifies it against the bound targets when a pass opens**, not at every
   draw. Why: nothing changes the set while a pass is open (every change ends it). Cost if wrong: a path that
   changes a bound target's shape with the pass open draws through a stale set; `forget_texture` and the three
   setters are the paths, each with its test.
5. **`Clear` is N single-view core clears**, not one multi-target quad as mtld3d's rect clear draws. Why: dxmt-core's
   Clear pass and `ClearRtv` are single-view, and the optimizer folds each whole clear into its slot of the next
   pass. Cost if wrong: a rect Clear with four targets bound is four small passes a call under core (C4d input).
6. **The VM's D3D10-12 gate is not rerun** (`JC_D3D9_ONLY=1`): no shipped D3D10-12 code changes (C4b3a-7's rule).
   The x64 `dxmt/check.sh` runs anyway (the brief's gate). Cost if wrong: a D3D10-12 regression in the VM from a
   change that touched none of its code would wait for the next slice's gate.
7. **The Clear's sRGB decision moves into d3d9-rt** (it takes the linear colour). Why: the view is chosen there,
   per slot; two callers deciding it apart is how a target gets encoded twice or never. Cost if wrong: none in
   pixels (the 3d3 rows `sr-clear`, `rt-srgb` and the new `mrt-srgbclear` pin it).
8. **`reset_targets_to_implicit` is not wired to d3d9-rt.** `Reset` is refused under core; C4c wires both. Cost if
   wrong: none until then.
9. **Rows are controls first** (3d3's ruling 9): every row is green on the default runtime, on the Mac and in the
   VM, before any core work.

## Stated gaps

- No row binds the same surface at two indices (ruling 3), a multisampled target (3d5), or more than one swap
  chain's back buffer.
- No row runs alpha test, fog or `oDepth` with several targets: the translator's, shared with mtld3d's default
  lane, where no row covers them either.
- The fakes print no blend factor of a pipeline: an extra's factors are proven by `core_extras_follow_mtld3d` and
  the rows `mrt-blend`, `mrt-destalpha`.
- A sampled texture that is also an attachment at index 1-3 in the same pass (feedback) has no row; mtld3d has no
  guard either, and the core's tile barrier is keyed per slot here (rt test on the key; not on Metal).
- Wine's `multiple_rendertargets_test` and `test_clear_different_size_surfaces` do not run under core until 3d5;
  `mrt-two`, `mrt-clear`, `mrt-unwritten` and `mrt-size` stand in.
- That two passes differing only in an extra do not merge is proven on Metal by one row (`mrt-switch`) and its
  mutant, not by a unit test (the fakes see no merge).

## Appendix: the rows

The blend kind's device: back buffer 256x256 X8R8G8B8, auto D24S8; the runner cleared Z to 1.0, stencil to 0 and
set `ZENABLE` off. Targets are 64x64 A8R8G8B8 render-target textures (`makeRt`) unless said, named T0..T3 by
their index. "MRT2"/"MRT4" is `PS_MRT2`/`PS_MRT4` with `VS_POS` over a full-target quad, its constants `c0..c3`
given per row. Every row reads its targets with `GetRenderTargetData` (index 1-3 **while still bound** in
`mrt-two`), then unbinds index 1-3. Every expectation is confirmed on the default runtime in Task 1 Step 5; a
value mtld3d does not give is corrected there, not in the runtime. (†) marks an expectation this plan could not
derive from code.

**rt-mrt** (targets kind): in Task 1 Step 1.

**mrt (19).**
- `mrt-two`: T0, T1 cleared green; MRT2 with magenta, yellow: T0 magenta, T1 yellow, read while bound;
  `GetRenderTarget(1)` is T1's level.
- `mrt-four`: T0 a texture, T1 a `CreateRenderTarget` surface, T2 face 3 of a 64 cube, T3 level 1 of a 128
  texture; MRT4 with four colours: each its own.
- `mrt-hole`: T0 A8R8G8B8, index 1 empty, T2 A16B16G16R16F cleared black; `COLORWRITEENABLE1` 0,
  `COLORWRITEENABLE2` red|alpha; MRT4 (c2 = (0.5, 0.5, 0.5, 1)): T0 is c0; T2 is (0.5, 0, 0, 1).
- `mrt-unwritten`: T0, T1 cleared green; `PS_C0` (writes `oC0` alone) with red: T0 red, T1 green; the same with no
  pixel shader (a pre-transformed diffuse quad): T0 the quad's colour, T1 green.
- `mrt-nobind`: T0 alone; MRT2: T0 is c0 and the draw answers `D3D_OK`.
- `mrt-clear`: T0, T1: a whole `Clear` to red reaches both; `Clear(1, (8,8)-(24,24))` to green: that rect on both;
  viewport (32,32,16,16) and a null-rect Clear to blue: that square on both; scissor test on with (0,48)-(64,64)
  and a null-rect Clear to white: that strip on both; with `COLORWRITEENABLE` and `COLORWRITEENABLE1` at red
  only, a whole Clear to yellow stores yellow on both; after `SetRenderTarget(1, NULL)` a whole Clear to black
  leaves T1 yellow.
- `mrt-mask`: T0, T1 cleared black; `COLORWRITEENABLE1` red only, MRT2 white, white: T0 white, T1 (255,0,0);
  then `COLORWRITEENABLE` green only and `COLORWRITEENABLE1` 0xf over a black Clear: T0 (0,255,0), T1 white.
- `mrt-blend`: T0 cleared red, T1 cleared blue; SRCALPHA/INVSRCALPHA, MRT2 with (0,1,0,0.5) and (1,1,1,0.25): T0
  (128,128,0), T1 (64,64,255); then BLENDFACTOR/INVBLENDFACTOR with `D3DRS_BLENDFACTOR` 0x80404040, opaque white
  on both over the same clears: T0 (255,64,64)·(†: per the formula), T1 the same factor on blue.
- `mrt-destalpha`: DESTALPHA/INVDESTALPHA, both cleared to (40,80,120) with alpha 0x40, MRT2 white, white: T0
  A8R8G8B8 blends by 0x40/255; T1 X8R8G8B8 takes the source (factor 1, 0); then the formats swapped: T0 X8 takes
  the source, T1 A8 blends.
- `mrt-formats`: T0 A8R8G8B8, T1 A16B16G16R16F, T2 R32F; MRT4 with (0.25, 0.5, 0.75, 1), (2.5, 0.5, 0.25, 1),
  (3.5, 0, 0, 1): T0 (64,128,191), T1 the floats unclamped, T2 3.5.
- `mrt-srgb`: `SRGBWRITEENABLE` on. T0, T1 A8R8G8B8, both cleared to 128 grey (read 188); a black quad at alpha
  0.5 on both under SRCALPHA/INVSRCALPHA: 137 on both (the linear blend, encoded). T0 A8R8G8B8 with T1
  A16B16G16R16F (no twin): the same Clear and quad: T0 and T1 as mtld3d's shader path gives them (†: T0 the
  encoded values' blend, about 94; T1 the float of the encoded source blended).
- `mrt-srgbclear`: `SRGBWRITEENABLE` on, a whole Clear to linear 0.5: T0 A8R8G8B8 with T1 R32F: 188 and 0.735; T0
  A8R8G8B8 64 with T1 A8R8G8B8 32 (another size): 188 on both; a rect Clear on the first pair: the same values
  inside the rect.
- `mrt-size`: T0 64, T1 32: the bind is `D3D_OK`; both cleared green; MRT2 red, blue: T0 red at (48,48) and at
  (8,8), T1 green; `Clear(1, (8,8)-(48,48))` to white: T0 white at (40,40) and red at (56,56), T1 white at
  (24,24) and green at (4,4). T1 128: both cleared green, MRT2: T0 red, T1 green; a whole Clear to white with the
  viewport T0's (0,0,64,64): T0 white, T1 white at (32,32) and (†) green at (100,100).
- `mrt-switch`: T0 with A at index 1: MRT2 red, green; B (same size and format) at index 1 with no other call:
  MRT2 red, blue; A is green, B is blue. A viewport (8,8,16,16) and scissor rect set before `SetRenderTarget(1,
  A)` read back unchanged after it. Then T0' (another 64 texture) at index 0 with B kept: MRT2 white, yellow: T0'
  white, B yellow.
- `mrt-scissor`: T0, T1 cleared blue; scissor (16,16)-(48,48), test on, MRT2 red, green: inside red / green,
  outside blue on both; test off with viewport (0,0,32,64): the left half on both.
- `mrt-depth`: T0, T1 256x256 with the auto depth-stencil, Z cleared; `ZENABLE` on, a near MRT2 quad (red, green)
  over the left half, then a far one (white, white) over all with LESSEQUAL: left red / green, right white on
  both.
- `mrt-back`: the back buffer at index 0 with a 256x256 texture at index 1; MRT2 red, green over a blue frame
  (`frames`): the back buffer red; T1 green (read back); then T1 shown on the back buffer in the next frame.
- `mrt-autogen`: an AUTOGENMIPMAP render target at index 1 cleared red through the MRT Clear; after
  `SetRenderTarget(1, NULL)` an 8x8 quad sampling it (level 3) is red.
- `mrt-order`: T0, T1: MRT2 red, red over all; `Clear(1, left half)` to green; MRT2 white, white over the top
  half: on both, top-left white, bottom-left green, bottom-right red.

## Review dispositions

(filled when the three reviews are folded)
