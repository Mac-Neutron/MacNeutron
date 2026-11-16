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
its pipeline description, its optimizer and its tile barrier (M §A; R2 walked nine pass sequences through the
optimizer): this slice changes none of it and fills what d3d9-rt left at "slot 0 only". Four DXMTRS commits, the
refusal lifted by the last, so every commit is safe to run:

1. **Fakes** (Task 2). `dxmt-fakes` prints colours 1-7 of a pass and of a pipeline as lines of their own (the
   existing lines keep their text) and gains the tile-pipeline entry, so d3d9-rt's tests can see a second
   attachment and the tile barrier's formats.
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
the end, cited as R1-R3). Ledger: `~/Documents/MacProton/.superpowers/sdd/2026-11-15-dxmtrs-c4b3d4-mrt/progress.md`
(the main checkout's ignored folder).

## Global Constraints

- The default runtime (mtld3d) keeps its behaviour: translator key
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` and the plain d3d9.dll imports unchanged;
  every core-only front-end branch is behind `#[cfg(feature = "core-runtime")]` and a runtime `is_core()` check,
  and a crossing inside a `DeviceInner` method is guarded inside that method (`unbind_extra_render_target`, whose
  one caller is `SetRenderTarget`'s null branch, already is); a core-only struct field or private helper is cfg'd
  in the struct, in every literal and at its definition. No mtld3d code moves in this slice; mtld3d-core gains
  `CoreExtra`, a field on `PipelineKey` and on `PipelineSource`, three functions and a parameter of
  `core_ps_variant`.
- **dxmt-core (`src/dxmt/src`), d3d11 and winemetal do not change.** `src/dxmt/fakes` (test support, linked into
  no shipped binary and not into d3d11's tests) gains printed lines and one entry. Should a task find that
  dxmt-core must change, it stops: the change is a ruling, a commit of its own with its unit test before the
  task's, and the x64 `dxmt/check.sh` and the VM's `vm/dxmt/gate.sh` then gate it (Task 6 runs the first anyway
  and the second only then).
- `tools/rust-check.sh` passes after every DXMTRS commit, and the commit is chained on the gate's own exit code
  (`sh tools/rust-check.sh > log 2>&1 && git commit ...`, never through `tail`); a commit's body follows
  CONTRIBUTING §Changes (what, why, the alternative, the verification), as 3d3's did. A d3d9-rt or mtld3d-core API
  change carries its front-end call sites and every literal of a changed struct in the same commit (the compiler
  lists them; M §C.2: `PipelineKey` at `core_draw.rs:498`, `pipeline_key.rs:246`, `pipeline_key/tests.rs:122, 146,
  371, 384, 414, 439, 445`, rt `fixture.rs:102`, `depth/tests.rs:107`, `target/tests.rs:268`, `draw/tests.rs:72,
  392, 699, 977`; `PipelineSource` at `pipeline_key.rs:312`). The PE front end compiles only inside rust-check:
  grep the import list of an edited front-end file before running it. New files are `git add`ed before it; a new
  Clone/Copy derive needs `sh tools/audit.sh --update-derives` **before** rust-check (its audit leg diffs
  `tools/derive_inventory.txt`).
- No new dependency. No `HashMap`. No `pub(crate)`. Doc blocks: a one-line title, a blank doc line, the body; lines
  of 100 columns at most; `#[must_use]` on a new value-returning pub fn, `const fn` where it can be. **No struct
  gains a `bool` field and no function a fourth `bool` parameter** (CONVENTIONS "Booleans pack"): this plan adds
  none (`core_ps_variant` has three and gains a `u8`; `CoreDevice` already has three bools and gains none).
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
  ever bound and their Mac lanes read **exactly as the control** (Task 1 Step 5); Task 5 lifts it. Between Task
  1's rows and Task 5, MacNeutron's core lanes are red by design on the `mrt` launch, on `targets-rt-mrt` and on
  `targets-runtime` (two refusal lines logged against the one-line set). Should the slice stop before Task 5, the
  `mrt` kind leaves the lists, `rt-mrt` goes back to its refusal form and `targets_refusals` gets its index line
  back, in the commit that pins.
- Per task one lane (the Mac's `d3d9-check` on default and core, job `jt`); full gates once, in Task 6. Benches and
  probes run once. No gate, build or VM job while the maintainer is gaming. The Mac stays idle (subagents too)
  while the x64 `dxmt/check.sh` or a bench runs. Autonomous work stops at 95% weekly usage.

## Scope

In: `SetRenderTarget` and `GetRenderTarget` at index 1-3 (a render-target texture's level or cube face, a
`CreateRenderTarget` surface, the device's back buffer); draws to up to four colour targets with a programmable
pixel stage writing `oC0..oC3` and with the fixed-function pixel stage (colour 0 alone); a hole (an empty slot
between bound ones); targets of different formats; `D3DRS_COLORWRITEENABLE1..3`; blending on every attached target
with each target's own destination-alpha clamp, and the blend factor colour; `D3DRS_SRGBWRITEENABLE` over several
targets; `Clear` (whole, rects, scissor, viewport) over every bound target; the scissor, the viewport and the
depth-stencil with several targets; a bound target of another size than target 0 (skipped by draws, reached by
`Clear`, joining the pass when target 0 changes to its size); `GetRenderTargetData` of a target at index 1-3;
AUTOGENMIPMAP targets at index 1-3.

Out (owners): multisampling, and with it mtld3d's sample-count half of the "takes part" rule (3d5; under core no
multisampled surface exists yet); `StretchRect`, `ColorFill`, `Reset` (and with it `reset_targets_to_implicit`
under core), an additional swap chain's back buffer as a target of its own (C4c; under core it aliases the
device's at any index, C4b3d1-4); per-draw dedup of the set's look-ups and `GetDesc` calls, an unwritten extra's
blend in the key, a variant mask cut to what the program writes (C4d; R2 M-10); the same surface at two indices
(ruling 3); mtld3d's "RT0 drop" (it needs no extra bound); a texture sampled while one of its subresources is
attached at index 1-3 on Metal (ruling 11).

## Review Focus

1. **A pass and its pipeline name the same slots in the same formats, always.** A mismatch is undefined behaviour
   in Metal with the validation layer off (M §A.2). The key names the slots; d3d9-rt attaches exactly those; a key
   the bound targets cannot make (a stale extra, a wrong format, a size that no longer matches) is
   `DrawRefused::Target` before any record; the pixel stage exports exactly the key's extras that the program
   writes; a hole is `Target::NONE` in the pass, `INVALID` in the pipeline and in the tile-barrier key, and no
   export. rt `the_key_names_the_pass`, `a_key_the_targets_cannot_make_is_refused`, `a_hole_stays_empty`,
   `the_tile_barrier_names_each_slot`; rows `mrt-hole`, `mrt-nobind`, `mrt-unwritten`; every core launch under
   Metal's validation layer with no report (Task 6).
2. **Two passes that differ in any slot never merge.** dxmt-core's `same_targets` compares the first
   `render_target_count` slots only (M §A.3): the count is the highest attached slot + 1. rt
   `an_extra_change_ends_the_pass` (the fakes see the passes after the optimizer: with a count of 1 the two passes
   merge and the test counts one encoder), with a hole variant for "highest slot + 1"; row `mrt-switch` on Metal;
   mutant (e).
3. **Four index conventions meet in one function** (F.1): slot `i + 1` is `extras[i]`, `COLORWRITEENABLE{i+1}`,
   bit `i + 1` of the program's written mask, of the alpha mask and of the variant's `color_out_mask`, and
   `colors[i + 1]` on the wire. mtld3d-core `core_extras_follow_mtld3d`; rows `mrt-hole` (distinct masks and
   formats at slots 1 and 2 with slot 1 empty), `mrt-four` (`COLORWRITEENABLE3`).
4. **Each target blends against its own alpha, encodes once, and takes its own mask.** rows `mrt-mask`,
   `mrt-destalpha`, `mrt-blend`, `mrt-srgb` (a set with twins, a mixed set), `mrt-srgbclear`.
5. **`Clear` reaches every bound target and nothing else**: a target of another size too, over its own extent; an
   unbound one not; the masks, blending and scissor of draws never. Rows `mrt-clear`, `mrt-size`.
6. **A target that leaves the set leaves the pass, and one that fits joins it**: unbinding, rebinding, a change of
   target 0's size, a texture forgotten while bound. rt `an_extra_change_ends_the_pass`,
   `forgetting_an_extra_unbinds_it`, `a_target_0_of_another_size_drops_the_extra`; rows `mrt-switch`, `mrt-size`
   (its target-0 step).

---

### Task 1: rows (MacNeutron) and the gate scripts

**Files:** Modify `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`;
`build/c4b3d/s4/gates/*.sh` (scratch, not committed).

- [ ] **Step 0: the gate scripts** (copied from `build/c4b3d/s3/gates` with `s3`→`s4`, `c4b3d3-`→`c4b3d4-`; none
  has run). Each job's header comment says what it gates here (task numbers, "dxmt-core changed" gone, `post.sh`'s
  stale usage lines). `jq.sh`: the validation-layer loop's list gains `mrt`. `ja.sh` (new): `jt.sh` with a
  `D3D9_BACKEND=msl` run between the default and the core one (the rows' first msl run is the control, not Task
  6). `jm.sh`: rewritten at Task 6 (the letters, the pairs `a:mrt-two b:mrt-mask c:mrt-destalpha d:mrt-size
  e:mrt-switch f:mrt-srgb g:mrt-clear`, the other-launch filter `grep -v '^mrt-'`, `mutants.txt`). `chainA.sh`,
  `chainB.sh`: written at Task 6 with the tips; `chainB.sh` exports `JC_D3D9_ONLY=1` for `jc`.
  `/usr/bin/grep -n 's3\|3d3\|bl-\|blend' build/c4b3d/s4/gates/*.sh` — Expected: `jq.sh`'s kind list, and `jm.sh`
  until Task 6.
- [ ] **Step 1: write.** `reset()` gains `D3DRS_COLORWRITEENABLE1`, `2`, `3` at 0xf (R1 I1, R3 I2). `rt-mrt`
  (targets kind) becomes a result in every lane: a viewport (13,17,40,30) and a scissor rect set; `SetRenderTarget(1,
  s)` is `D3D_OK`; `GetViewport` and `GetScissorRect` answer what was set; `GetRenderTarget(1)` answers `s` (pointer
  equal, released); `SetRenderTarget(1, nullptr)` is `D3D_OK` and `GetRenderTarget(1)` then `D3DERR_NOTFOUND`,
  writing null over a non-null sentinel; `SetRenderTarget(4, s)` and `SetRenderTarget(1, sysSurf)` are
  `D3DERR_INVALIDCALL`. The `mrt` launch kind (`d3d9_triangle mrt [strict]`): `main`'s flag `mrtMode`, the blend
  kind's device (`if (depth || blendMode || mrtMode || depthBench)`), its dispatch beside `blendMode`'s, `mrtRows`
  with the blend kind's prologue and per-row runner (`reset() && depthReset() && blendStart() && r.run()`), budget
  180 s, the 20 rows of the appendix. New token arrays `PS_MRT2` (`ps_2_0`: `mov oC0, c0` = `0x02000001,
  0x800f0800, 0xa0e40000`; `mov oC1, c1` = `0x02000001, 0x800f0801, 0xa0e40001`) and `PS_MRT4` (`oC0..oC3` from
  `c0..c3`), drawn with `VS_POS`/`DECL_XYZ` as `rt-ps` does. Helpers: `bindExtra(DWORD slot, IDirect3DSurface9 *)`
  (CK), `mrtDraw(const Prog &, const float (*c)[4], int n, float z = 0.5f)` (the constants, then a full-clip-space
  `V3` quad at `z`), `rtIs(surface, size, format, x, y, expect[, mask])` over `readTarget`/`want` for the 4-byte
  formats and `rtIsF(surface, size, format, x, y, const float rgba[4], int channels)` over `decodeTexel` (±0.004)
  for the half and float ones. A row's wrapper is named in the appendix: `until` around a body that draws and
  reads back, `frames` for back-buffer probes; a body that fails inside `frames` is not retried, so no read-back
  of a target sits inside one. A row unbinds index 1-3 before it presents, shows a target or returns (`frames()`
  and `until()` rebind index 0 only, F.17). Headers: the kind, the counts, the targets launch's one refused call.
- [ ] **Step 2: scripts.** `d3d9-check.sh`: the header and `refusals_ok`'s comment; `targets_refusals` loses the
  index line and stays one single-quoted line with no trailing newline (`wc -l` of the `printf '%s\n'` is then 1);
  `launch ours fresh mrt 240 mrt $strict` and `check mrt "<rows>"` after `check blend`, before the mtld3d-lane
  refusal grep; `refusals_ok`'s zero-line arm becomes `blend|mrt)`. `d3d9-gate.sh`: the header and `runtime()`'s
  comment, `targets_refusals`, `for r in mr64-fresh mr32-fresh` row loops, the core block (`runtime "$G/logs/$r"`
  with no kind: zero refusals). `d3d9.sh`: the header, two `tri` lines (`mr64-fresh`, `mr32-fresh`, 240 s, `mrt
  $strict`) after `bl32-fresh`.
- [ ] **Step 3: compile** `d3d9_triangle.cpp` with the three mingw compilers (`-std=c++17 -fsyntax-only -Wall
  -Wextra`) — Expected: no diagnostic.
- [ ] **Step 4: commit** `dxmt, vm: d3d9_triangle mrt rows in every D3D9 lane (DXMTRS C4b-3d4)`.
- [ ] **Step 5: the controls.** Job `ja` (DXMTRS 8bca6b74, this commit). Expected: default and msl `d3d9-check:
  ok` with every `mrt` row and `rt-mrt` ok; core: `targets-rt-mrt` and `targets-runtime` red, the `mrt` launch red
  on every row that binds index 1-3 (all but `mrt-nobind`; a refused bind fails its row through `CK`, the launch
  goes on) and `mrt-runtime` red (refusal lines), every other launch as at 3d3's close. For a row red on the
  default runtime, one rule (R3 I4): an expectation derived from D3D9's formula that mtld3d misses is a defect of
  mtld3d (ledgered as a ruling; the row keeps D3D9's value and is listed red-on-default until the defect's fix,
  this slice's only if it is a one-line fix); an expectation read off mtld3d's code that mtld3d does not give (the
  appendix marks these "parity") is a wrong row, corrected in the appendix and the row.
- [ ] **Step 6: the VM's default lanes once** (`jcr`: old WoW64, x64 and x86): `mr64-fresh` and `mr32-fresh` green
  inside their budget — Expected: ok; a budget miss raises the kind's budget before any core work.

### Task 2: dxmt-fakes prints colours 1-7 and the tile pipeline

**Files:** Modify `src/dxmt/fakes/src/lib.rs`.

- [ ] **Step 1: implement.** `fake_render_encoder`: after the colour-0 lines, for `i in 1..8` with
  `pass.colors[i].texture != 0`: `  color{i} load={} store={} clear={},{},{},{}` and `  target{i} #{}` (the view's
  handle, as `target0` prints it, without the size). `fake_new_render_pipeline_with_vertex_descriptor`: after `pso
  color0=`, for `i in 1..8` with `info.colors[i].pixel_format != 0`: `  pso color{i}={} mask={} blend={}` (format,
  `write_mask`, `blending_enabled`). A new export `MTLDevice_newTileRenderPipelineState` (the signature of
  `winemetal-sys`'s declaration; dxmt-core calls it at `encoder/gaps.rs:283` and the fakes have none, so an rt
  test that reached a tile barrier would call an unresolved symbol): `  tile color{i}={}` for each non-zero colour
  format, `  tile samples={}`, and a made handle. The colour-0 lines keep their text (rt's tests match them
  exactly, M §A.6).
- [ ] **Step 2: run** dxmt-core's and d3d9-rt's tests and `rust-check` — Expected: ok, no test moved (no test
  outside d3d9-rt reads a fake's transcript: `/usr/bin/grep -rln 'color0 load\|pso color0' src tests` names d3d9-rt
  and the fakes only).
- [ ] **Step 3: commit** `dxmt-fakes: colours 1-7 of a render pass and of a pipeline; the tile pipeline`.

### Task 3: draws to several colour targets (mtld3d-core, d3d9-rt, front end)

**Files:** Modify `src/d3d9/core/src/pipeline_key.rs` (+ `pipeline_key/tests.rs`); `src/d3d9/rt/src/device.rs`,
`device/target.rs`, `device/recorder.rs`, `device/draw.rs`, `device/textures.rs` (`forget_texture`),
`device/fixture.rs` and every rt test the compiler lists (`target/tests.rs:288, 289, 334, 341, 342` read
`target_desc`); `src/d3d9/src/device/core_draw.rs`, `core_arms.rs` (`target_desc`'s other caller);
`tools/derive_inventory.txt`.

**Interfaces (produces):**
- mtld3d-core `pipeline_key.rs`:
  `#[derive(Clone, Debug, PartialEq, Eq, Hash)] pub struct CoreExtra { pub format: u32, pub write_mask: u8, pub
  blend: Option<CoreBlend> }` (a colour target above index 0 as the pipeline declares it: its attached
  `WMTPixelFormat`, its `MTLColorWriteMask`, its blend);
  `PipelineKey { .., pub extras: [Option<CoreExtra>; 3] }` and the same field on `PipelineSource` (cloned by
  `pipeline_source`); `#[must_use] pub const fn color_formats(&self) -> [u32; 4]` on `PipelineKey` (colour 0's,
  then each extra's, 0 for none);
  `#[must_use] pub fn core_extras(formats: [u32; 3], rs: &[u32; RENDER_STATE_COUNT], ps_written: u8, has_alpha: u8)
  -> [Option<CoreExtra>; 3]`: `formats[i]` is slot `i + 1`'s attached format (0: not in the pass → `None`); both
  masks are slot-indexed like the variant's (bit `i + 1` is slot `i + 1`; bit 0 is colour 0's and unread).
  `write_mask` is mtld3d's own rule, called, not restated: `PipelineRsBits { color_write_mask_ext:
  [enum_value(rs, D3DRS_COLORWRITEENABLE1), enum_value(rs, ..2), enum_value(rs, ..3)], ..default }
  .extra_write_mask(&ExtraColorAttachments { present_mask: extra_present(formats), ..NONE }, ps_written, i)`,
  narrowed to `u8` (`u8::try_from(mask.bits()).unwrap_or(0)`): the slot's mask when the program writes
  `oC{i+1}` (the fixed-function stage's `ps_written` is 1), empty otherwise (declared, unwritten, preserved);
  `blend` is `core_blend(rs, has_alpha & (1 << (i + 1)) != 0)` (mtld3d's `extra_blend_factors`: the shared
  effective blend, this target's own destination-alpha clamp);
  `#[must_use] pub const fn extra_present(formats: [u32; 3]) -> u8` (bit `i` for a non-zero `formats[i]`:
  `ExtraColorAttachments::present_mask`'s convention);
  `core_ps_variant(base, ps_is_prog, point_list, linked_input_mask, has_depth, extra_present: u8)`: a programmable
  stage's `color_out_mask` is `extra_present << 1` (mtld3d's draw: `present_mask << 1`; bit 0 is implied); the
  fixed-function stage keeps `base`'s. `pipeline_source` debug-asserts, for a programmable pixel stage,
  `variant.color_out_mask == extra_present(the key's extras' formats) << 1` (the key states presence twice; R2
  M-7).
- d3d9-rt:
  `CoreDevice { .., extras: [Option<TargetRef>; 3], open_colors: [u32; 4] }` (`open_colors` replaces
  `open_format`: the format each slot of the open pass attached, 0 for none; `close_pass` zeroes it);
  `pub fn set_extra_target(&mut self, slot: usize, target: Option<TargetRef>) -> bool`: slot 1-3; false, and
  nothing changed, for another slot or a target that does not resolve; a change ends the open render pass;
  `#[must_use] pub fn target_desc(&self, srgb_write: bool) -> Option<((u32, u32), [u32; 4])>`: target 0's size and
  the format each slot of the pass set attaches under `srgb_write` (0: the slot is not in the set). The set:
  target 0, and each extra, in slot order, that resolves, has target 0's width and height, and is not the
  `TargetRef` of target 0 or of a lower slot in the set. Under `srgb_write` every format is its sRGB twin's when
  **every** member has one, and every format is the stored one otherwise (mtld3d's `srgb_write_is_attachable`);
  `DrawRefused::Target` (the key names colour formats the bound targets do not make);
  `pub(super) fn pass_set(&self) -> [Option<Shape>; 4]` (the one place the set is decided; `target_desc`,
  `open_pass` in `recorder.rs` and Task 4's clears read it).
- Behaviour: `draw_up` takes `colors = key.color_formats()`; unless `self.render_open && self.open_colors ==
  colors`, it requires `target_desc(false)` or `target_desc(true)` to answer exactly `colors`, else
  `Err(DrawRefused::Target)` before the pipeline is built (the check replaces the colour-format debug assert).
  `open_pass(with_depth, colors)`: reopens when the depth kind or `colors` differs; `start_render_pass(planes, 0,
  count, None)` with `count` the highest non-zero slot + 1; for each non-zero slot an attachment (`READ | WRITE`)
  of that slot's target through its view in that format, `LOAD`/`STORE`, into `targets.colors[slot]`, and
  `tile_barrier_pso_key[slot] = format`; `[8] = 1`; the extent as today (every set member has target 0's size).
  `open_render_pass` opens on `[target_format, 0, 0, 0]` (target 0 alone; a draw naming more reopens through the
  check). `build_pipeline`: `colors[i + 1]` from `source.extras[i]` (format, mask, and the blend fields through a
  helper extracted from colour 0's inline fill and used for slots 0-3). `set_target`, `set_extra_target`,
  `set_depth` and `forget_texture` end the open pass on a change; `forget_texture` empties every extra slot that
  holds the id.
- Front end: `core_target_of` answers `((u32, u32), [u32; 4], bool)` (the set's formats; hardware as today, from
  colour 0); `let [_, f1, f2, f3] = formats; extras = core_extras([f1, f2, f3], &rs, written, alpha)` with
  `written = ps.color_out_mask()` for a programmable stage and 1 otherwise, and `alpha` bit `i` from
  `slot_has_alpha(dev, i)` for the slots `core_extras` reads (`target_has_alpha` with the slot as a parameter: a
  back buffer, an empty slot or an unknown format stores alpha), computed only under `ALPHABLENDENABLE`;
  `core_ps_variant(.., extra_present([f1, f2, f3]))`. `core_arms::clear` reads `target_desc(true)`'s colour 0 as
  before (Task 4 replaces it). `Err(DrawRefused::Target)` answers `D3D_OK` and logs once
  (`mtld3d_shared::log_once_warn!`: "d3d9 core: a draw's colour targets are not its key's; the draw is dropped":
  none of d3d9-check's `unpresented` or refusal patterns); it is unreachable while the front end builds its key
  from `target_desc`.

- [ ] **Step 0:** `/usr/bin/grep -rn '0x3[4-7][0-9a-f]\b' src/d3d9/rt/src src/dxmt/fakes/src` — Expected: no
  device handle in 0x340-0x37f.
- [ ] **Step 1: failing tests.** mtld3d-core: `core_extras_follow_mtld3d` (an absent slot is `None` whatever the
  states; slot 2 alone present takes `COLORWRITEENABLE2` and bit 2 of the written mask, not 1's; slot 3 takes
  `COLORWRITEENABLE3`; an unwritten present slot has mask 0 and keeps its format, the case of mtld3d's
  `unwritten_extra_target_gets_an_empty_write_mask`; blending off → `blend: None`; DESTALPHA/INVDESTALPHA with
  `has_alpha` 0b0100 → slot 1 One/Zero, slot 2 DestinationAlpha/OneMinusDestinationAlpha, the case of
  `extra_target_blend_factors_clamp_on_their_own_alpha`; the operations equal colour 0's), `extra_present_bits`,
  `core_ps_variant_rules` re-tabled (a programmable stage with `extra_present` 0b101 → `color_out_mask` 0b1010; 0
  → 0; the fixed-function stage keeps the base's), the key table grows by `extras` (`[Case; 19]`),
  `color_formats_lists_the_slots`. rt (target 0 is a 64x64 texture in these tests: the fixture's back buffer is
  64x32 and its texture helper is square; the fixture's `key()` gains `extras: [None, None, None]`; a helper
  `key_extras(extras)`): `the_key_names_the_pass` (two extras bound, a key naming both: one encoder with
  `color1`/`target1` and `color2`/`target2` lines and `pso color1=`, `pso color2=` with the key's masks; the same
  targets with a key naming none: `Err(Target)`), `a_key_the_targets_cannot_make_is_refused` (a format the slot
  does not store; an extra the device has not bound; an extra unbound after a draw, with the stale key; nothing
  recorded, no pipeline built), `a_hole_stays_empty` (slots 0 and 2: `color2`/`target2`, no `color1`; `pso color2=`
  and no `pso color1=`), `an_extra_of_another_size_is_not_in_the_set` (`target_desc` answers 0 for it; a key
  naming it is refused), `a_duplicate_is_not_in_the_set` (target 0's own `TargetRef` at slot 1; the same texture
  level at slots 1 and 2: slot 1 alone), `a_target_0_of_another_size_drops_the_extra` (and one of the extra's size
  takes it back), `an_extra_change_ends_the_pass` (draw, `set_extra_target` to another texture of the same shape,
  draw: two encoders, each with its own `target1`; to the same: one; the same with the extra at slot 2 and slot 1
  empty: two), `forgetting_an_extra_unbinds_it`, `srgb_twins_are_all_or_nothing` (BGRA8 + RGBA8, `view_twins` and
  `srgb_format` set as `target/tests.rs:336-339` does: both twins under `srgb_write`; BGRA8 + R32F: both stored;
  the R32F extra of another size: colour 0 the twin's), `the_blend_reaches_each_extra` (`pso color1= .. blend=1`),
  `the_tile_barrier_names_each_slot` (target 0 level 0 of a 2-level BGRA8 texture, slot 2 an RGBA16F extra, slot
  1 empty, a draw that samples target 0's texture: `tile color0=`, `tile color2=`, no `tile color1`, `tile
  samples=1`; with two twinned members under sRGB write both lines name the twins). Expected: do not compile.
- [ ] **Step 2: implement.**
- [ ] **Step 3: run** `audit.sh --update-derives`, mtld3d-core's and d3d9-rt's tests, `rust-check` — Expected: ok.
- [ ] **Step 4: commit** `d3d9: draws to several colour targets in the core runtime's device`.
- [ ] **Step 5: lane** `JT=jt3` — Expected: both runtimes exactly as the control (`ja`): default ok; core red on
  `targets-rt-mrt`, `targets-runtime` and the `mrt` launch only.

### Task 4: Clear over every bound colour target (d3d9-rt, front end)

**Files:** Modify `src/d3d9/rt/src/device/recorder.rs`, `device/target.rs`; every `clear(`/`clear_rects(` call
site the compiler lists (16 test calls: rt `recorder/tests.rs`, `target/tests.rs`, `textures/tests.rs`,
`depth/tests.rs`, `swapchain/tests.rs`, `surface/tests.rs`, `draw/tests.rs`); `src/d3d9/src/device/core_arms.rs`.

**Interfaces (produces):**
- d3d9-rt: `#[must_use] pub fn color_size(&self, slot: usize) -> Option<(u32, u32)>` (the colour target bound at
  slot 0-3, in the pass set or not; `None` for an empty slot or one that does not resolve);
  `pub fn clear(&mut self, slot: usize, linear: [f32; 4], srgb_write: bool)` and `pub fn clear_rects(&mut self,
  slot: usize, rects: &[[u32; 4]], linear: [f32; 4], srgb_write: bool)`: the slot's target through the view a pass
  made under `srgb_write` attaches it in (a set member: the set's all-or-nothing answer, so the whole Clear folds
  into that pass; a bound target outside the set: its own twin when it has one, mtld3d's scoped pass that binds it
  alone, unix `encoder.rs:3731`); `linear` is stored as given through an sRGB view and through
  `linear_to_srgb_rgba` otherwise when `srgb_write` (mtld3d's `resolved_clear_rgba`: each target encoded exactly
  once). A duplicate slot is cleared again, harmlessly.
- Front end `core_arms::clear`: the colour part loops `for slot in 0..4` over `core.color_size(slot)`: `viewport =
  drawn_rect(&dev.viewport(), extent)`, `clear_area(regions, viewport, extent)` → `clear(slot, ..)` or
  `clear_rects(slot, ..)`; the CPU encode and the `hardware` test leave the front end. The depth part is
  unchanged (its viewport against target 0's size).

- [ ] **Step 1: failing tests.** rt: `a_clear_reaches_each_bound_target` (two extras, three clears, a draw naming
  all three: one encoder whose `color0`, `color1`, `color2` lines say `load=2` with the clear colour: N single-view
  clears fold into the N slots, M §A.3), `a_clear_of_a_target_outside_the_set_is_its_own_pass` (an extra of
  another size: its Clear is an encoder of its own on its view; the draw's pass has no `color1`),
  `a_clear_encodes_once` (under `srgb_write`: a BGRA8 target alone takes the linear colour through the twin's
  view; BGRA8 with an R32F extra: both take the encoded colour through their stored views; the 3d3 clear tests
  re-stated on `clear(0, ..)` with linear inputs), `a_rect_clear_reaches_one_slot` (`clear_rects(1, ..)`: a pass on
  the extra's view alone), `color_size_lists_bound_slots`. Expected: do not compile.
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
  `unbind_extra_render_target`, under core (cfg'd), **before** `replace_render_target(slot, null)` (which may
  release the last reference and forget the texture): `core.texture_generate_mips(old_id)` for the slot's autogen
  id, then `core.set_extra_target(slot, None)`.
- [ ] **Step 2: run** `rust-check` — Expected: ok (the front end compiles only there).
- [ ] **Step 3: commit** `d3d9: SetRenderTarget at index 1-3 on the core runtime`.
- [ ] **Step 4: lane** `JT=jt5` — Expected: both runtimes `d3d9-check: ok`: the 20 `mrt` rows, `rt-mrt`, the
  targets launch's log with exactly its one refusal line, the mrt launch's with none.

### Task 6: gates, mutants, probe

- [ ] **Step 1: `jq`** (Mac): `DXMT_D3D9_CORE=1` build; `dxmt-tests`; `d3d9-check.sh` default, `D3D9_BACKEND=msl`,
  `D3D9_RUNTIME=core`; `dxmt/mtld3d-conformance.sh D3D9_RUNTIME=core --only stateblock` equal to default; **every
  core launch** (`shaders`, `state`, `textures`, `fixed`, `targets`, `depth`, `blend`, `mrt`) once under the Metal
  validation layer with no report and the rows still ok. Expected: all ok.
- [ ] **Step 2: mutants** (`jm`, one build and one core `d3d9-check` each; each red on its row, and on no item
  outside `mrt-*`; each is inert with no extra bound, R3): (a) `core_ps_variant`: `color_out_mask = extra_present`
  (no shift) → `mrt-two`; (b) `core_extras` fills all three `color_write_mask_ext` from `D3DRS_COLORWRITEENABLE` →
  `mrt-mask`; (c) the front end's `alpha` is colour 0's for every slot → `mrt-destalpha`; (d) `color_size` answers
  set members only (a bound target of another size gets no Clear) → `mrt-size`; (e) `open_pass` starts the pass
  with count 1 → `mrt-switch`; (f) `target_desc` picks each member's twin on its own → `mrt-srgb`; (g)
  `core_arms::clear` loops `0..1` → `mrt-clear`. (a) may also abort the launch at `mrt-hole` (an export with no
  attachment): an abort after its named row is red is red. The size rule of draws has no mutant (dropping it
  attaches a 32x32 view under a 64x64 area, undefined on Metal, R2 M-11): rt
  `an_extra_of_another_size_is_not_in_the_set` is its proof.
- [ ] **Step 3: plain build** (`jr`): no `DXMT_D3D9_CORE`; translator key and d3d9.dll imports unchanged;
  `d3d9-check.sh` default ok; the Mac's default-runtime Wine conformance with no new failing site; **x64
  `dxmt/check.sh`: all passed** (idle Mac).
- [ ] **Step 4: VM** (`jc` with `JC_D3D9_ONLY=1`: dxmt-core did not change, ruling 6): default and core lanes, old
  and new WoW64, x64 and x86: `d3d9-gate` ok (a default lane short only on executed-test floors is rerun once
  before it is blamed).
- [ ] **Step 5: probe** (`jp`, once, idle Mac): this tip on its own full build, `--bench 2000 <0|600> tex` and
  `tex depth`, mtld3d and core; written to `build/c4b3d/s4/probe.md` beside 3d3's figures (core 2.18 / 2.25,
  mtld3d 1.70 / 1.77). The single-target draw now asks for a set of four and builds three empty extras: a core
  delta against 8bca6b74 below the 0.32 ms single-run spread is reported as not measurable; one above it gets a
  retake of 8bca6b74 on its own build in the same quiet window (as 3d3's `jq1`) before it is called a cost.

### Task 7: review, spec, pin, push

- [ ] Whole-branch review (Opus; the plan's Review Focus verbatim; the ledger's rulings); one fix pass, each fix
  RED→GREEN with the suite green; minors deferred to the ledger.
- [ ] Spec §5: a C4b-3d4 paragraph after 3d3's. Execution notes at this plan's end. `dxmt/pins` to the DXMTRS tip.
- [ ] Privacy scan of both ranges; push DXMTRS `c4b3d` → `main`, then MacNeutron `c4b3d-mn` → `main`; post-push
  check from main (plain build, tests, `d3d9-check`, x64 check); CI; scratch of this slice deleted by full path;
  memory.

## Rules check

- **No dxmt-core, d3d11 or winemetal change.** `dxmt-fakes` (test support) prints more lines and exports one more
  entry; no test outside d3d9-rt reads them.
- **No move and no duplicate of mtld3d's code.** `core_extras` calls mtld3d's own `extra_write_mask` and 3d3's
  `core_blend` (which is `effective_blend_rs` and `d3d_to_metal_blend_rt`); it fills a `PipelineRsBits` from
  render states as `core_blend` does. The pass-set rule restates `recompute_extra_present_mask`'s size test (one
  comparison) in d3d9-rt, which has no `PassState`; `target_desc` restates `srgb_write_is_attachable`'s
  all-or-nothing rule and d3d9-rt's clear restates `resolved_clear_rgba`'s encode decision, for the same reason: three
  near-duplicates, each pinned (rt `an_extra_of_another_size_is_not_in_the_set` and `mrt-size`;
  `srgb_twins_are_all_or_nothing` and `mrt-srgb`; `a_clear_encodes_once` and `mrt-srgbclear`).
- New types: `CoreExtra` (Clone: a key is cloned into the pipeline map; no Copy). New variant
  `DrawRefused::Target`, which is not silent: the front end logs it once (CONTRIBUTING "No silent failures"). New
  fields: `PipelineKey::extras`, `PipelineSource::extras`, `CoreDevice::extras`, `CoreDevice::open_colors`
  (replacing `open_format`). No new `bool` field or parameter. The derive inventory follows.
- No new file, no static, no config key, no wire field, no dependency, no lint suppression. Metal wire values come
  from `winemetal_sys::wire` (d3d9-rt) and D3D9 constants from `mtld3d-types` (the three `COLORWRITEENABLE`
  states by name, not `+ i`).
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
   skipped by draws (and cleared once more by `Clear`, harmlessly). mtld3d does not check and would hand Metal the
   texture twice; D3D9's own rule is not established here. No row: the control itself could abort under Metal.
   Because every back-buffer surface aliases the device's under core (C4b3d1-4), an additional swap chain's back
   buffer at index 1-3 is skipped beside the device's back buffer, and beside a texture it draws into the
   device's back buffer, where mtld3d draws to the swap chain's own (the same alias slot 0 has had since 3d1).
   Cost if wrong: a title that binds one surface twice misses the higher index's pixels under core; one that uses
   a second swap chain's back buffer as an MRT target writes the wrong surface until C4c's swap chains.
4. **The key decides the pass; d3d9-rt verifies it against the bound targets when a pass opens**, not at every
   draw. Why: nothing changes the set while a pass is open (every change ends it, and `close_pass` zeroes
   `open_colors`; R2 confirmed each path and that every `texture_create` caller uses a fresh id). Cost if wrong: a
   path that changes a bound target's shape with the pass open draws through a stale set.
5. **`Clear` is N single-view core clears**, not one multi-target quad as mtld3d's rect clear draws. Why: dxmt-core's
   Clear pass and `ClearRtv` are single-view, and the optimizer folds each whole clear into its slot of the next
   pass. Cost if wrong: a rect Clear with four targets bound is four small passes a call under core (C4d input).
6. **The VM's D3D10-12 gate is not rerun** (`JC_D3D9_ONLY=1`): no shipped D3D10-12 code changes (C4b3a-7's rule).
   The x64 `dxmt/check.sh` runs anyway (the brief's gate). Cost if wrong: a D3D10-12 regression in the VM from a
   change that touched none of its code would wait for the next slice's gate.
7. **The Clear's sRGB decision moves into d3d9-rt** (it takes the linear colour). Why: the view is chosen there,
   per slot; two callers deciding it apart is how a target gets encoded twice or never. The decision is not
   pixel-visible (0x80 reads 188 encoded by the CPU or by the view): the rows prove "encoded exactly once". Cost
   if wrong: none in pixels (`sr-clear`, `rt-srgb`, `mrt-srgbclear`).
8. **`reset_targets_to_implicit` is not wired to d3d9-rt.** `Reset` is refused under core; C4c wires both. Cost if
   wrong: none until then.
9. **Rows are controls first** (3d3's ruling 9): every row is green on the default runtime, on the Mac (default
   and msl) and in the VM, before any core work.
10. **The mixed-set sRGB rows assert parity with mtld3d, not D3D9.** With `SRGBWRITEENABLE` on and a set where one
    target has no sRGB view, mtld3d keeps the whole set on the shader path: the stage encodes every exported
    output before the blender and the Clear stores the encoded colour in every target, a float one included.
    Windows writes sRGB per target (the twin-capable target would blend linear, a float target takes no
    conversion). The core does as mtld3d. `mrt-srgb`'s mixed set and `mrt-srgbclear`'s float target are marked
    "parity" in the appendix. Cost if wrong: a title mixing an sRGB-capable and a float target under sRGB write
    differs from Windows under both runtimes alike; the fix is mtld3d's rule, then the core's with it.
11. **No row samples a texture with a subresource attached at index 1-3.** The tile barrier's key is proven by rt
    `the_tile_barrier_names_each_slot` on the fakes; Metal never builds that pipeline in this slice's lanes. Why:
    mtld3d has no guard for the case, so a control row would assert undefined pixels, and a masked row proves
    only "no report". Cost if wrong: a feedback draw with several targets bound trips Metal on a format this
    slice's test did not model; the fix is local to `open_pass`'s key fill.

## Stated gaps

- No row binds the same surface at two indices (ruling 3), a multisampled target (3d5), or more than one swap
  chain's back buffer.
- No row runs alpha test, fog or `oDepth` with several targets: the translator's, shared with mtld3d's default
  lane, where no row covers them either.
- The fakes print no blend factor of a pipeline: an extra's factors are proven by `core_extras_follow_mtld3d` and
  the rows `mrt-blend`, `mrt-destalpha`.
- `mrt-unwritten` is a parity check, not the proof of the empty mask: a non-zero mask with no fragment output
  leaves the attachment undefined on Metal, which may well be "unchanged". `core_extras_follow_mtld3d` is the
  proof.
- The tile barrier with several targets is proven on the fakes only (ruling 11).
- Wine's `multiple_rendertargets_test` and `test_clear_different_size_surfaces` do not run under core until 3d5;
  `mrt-two`, `mrt-clear`, `mrt-unwritten` and `mrt-size` stand in.

## Appendix: the rows

The blend kind's device: back buffer 256x256 X8R8G8B8, auto D24S8; the runner cleared Z to 1.0, stencil to 0 and
set `ZENABLE` off. Targets are 64x64 A8R8G8B8 render-target textures (`makeRt`) unless said, named T0..T3 by
their index. "MRT2"/"MRT4" is `PS_MRT2`/`PS_MRT4` with `VS_POS` over a full-clip-space quad (`mrtDraw`), its
constants `c0..c3` given per row. A row's body draws and reads its targets back inside one `until` attempt unless
said, and unbinds index 1-3 before the attempt ends. Colours are (r, g, b) bytes; "parity" marks a value read off
mtld3d's code rather than D3D9's formula (Task 1 Step 5's rule).

**rt-mrt** (targets kind): in Task 1 Step 1.

**mrt (20).**
- `mrt-two`: T0, T1 cleared green; MRT2 with magenta, yellow: T0 magenta, T1 yellow, T1 read **while still
  bound**; `GetRenderTarget(1)` is T1's level.
- `mrt-four`: T0 a texture, T1 a `CreateRenderTarget` surface, T2 face 3 of a 64 cube, T3 level 1 of a 128
  texture, all cleared black; `COLORWRITEENABLE3` green|alpha; MRT4 with red, green, blue, white: T0 red, T1
  green, T2 blue, T3 (0,255,0).
- `mrt-hole`: T0 A8R8G8B8, index 1 empty, T2 A16B16G16R16F cleared black; `COLORWRITEENABLE1` 0,
  `COLORWRITEENABLE2` red|alpha; MRT4 with c0 magenta, c1 yellow, c2 (0.5, 0.5, 0.5, 1): T0 magenta; T2 (0.5, 0,
  0, 1) (`rtIsF`).
- `mrt-unwritten`: T0, T1 cleared green; `PS_C0` (writes `oC0` alone) with red: T0 red, T1 green; the same with no
  pixel shader (a pre-transformed blue diffuse quad over all, after a Clear of both to green): T0 blue, T1
  green.
- `mrt-nobind`: T0 alone; MRT2 magenta, yellow: T0 magenta and the draw answers `D3D_OK`.
- `mrt-clear`: T0, T1. A whole `Clear` to red: both red. `Clear(1, (8,8)-(24,24))` to green: (16,16) green and
  (40,40) red on both. Viewport (32,32,16,16), a null-rect Clear to blue: (40,40) blue, (28,28) red on both; the
  viewport back to (0,0,64,64). Scissor rect (0,48)-(64,64) with the test on, a null-rect Clear to white: (32,56)
  white, (20,44) red on both; the test off. `COLORWRITEENABLE` and `COLORWRITEENABLE1` red only, a whole Clear to
  yellow: both (255,255,0); the masks back to 0xf. `SetRenderTarget(1, NULL)`, a whole Clear to black: T0 black,
  T1 still yellow.
- `mrt-mask`: T0, T1 cleared black; `COLORWRITEENABLE1` red only, MRT2 white, white: T0 white, T1 (255,0,0);
  then both cleared black, `COLORWRITEENABLE` green only and `COLORWRITEENABLE1` 0xf, MRT2 white, white: T0
  (0,255,0), T1 white; then both cleared black, `COLORWRITEENABLE` 0, MRT2 white, white: T0 black, T1 white (review
  M9).
- `mrt-blend`: T0 cleared red, T1 cleared blue; SRCALPHA/INVSRCALPHA, MRT2 with (0,1,0,0.5) and (1,1,1,0.25): T0
  (128,128,0), T1 (64,64,255). Then both cleared again (red, blue), BLENDFACTOR/INVBLENDFACTOR with
  `D3DRS_BLENDFACTOR` 0x80808080, MRT2 white, white: T0 (255,128,128), T1 (128,128,255).
- `mrt-destalpha`: DESTALPHA/INVDESTALPHA, both targets cleared to 0x40285078 ((40,80,120), alpha 0x40), MRT2
  white, white: T0 A8R8G8B8 (94,124,154) (0.251 + 0.749 x dest); T1 X8R8G8B8 white (factors 1, 0); then the
  formats swapped: T0 X8 white, T1 A8 (94,124,154).
- `mrt-formats`: T0 A8R8G8B8, T1 A16B16G16R16F, T2 R32F; MRT4 with (0.25, 0.5, 0.75, 1), (2.5, 0.5, 0.25, 1),
  (3.5, 0, 0, 1): T0 (64,128,191), T1 (2.5, 0.5, 0.25, 1), T2 3.5 (`rtIsF`).
- `mrt-srgb`: `SRGBWRITEENABLE` on. T0, T1 A8R8G8B8, both cleared to 0x808080 (read 188); SRCALPHA/INVSRCALPHA,
  MRT2 with c0 = c1 = (0,0,0,0.5): 137 on both (the linear blend, encoded). T0 A8R8G8B8 with T1 A16B16G16R16F (no
  twin), the same Clear and draw: T0 94 and T1 0.368 in each colour channel (parity, ruling 10: the stage encodes
  before the blender, the Clear stored 0.737 in both).
- `mrt-srgbclear`: `SRGBWRITEENABLE` on, a whole Clear to 0x808080: T0 A8R8G8B8 with T1 R32F: 188 and 0.737
  (±0.004; parity, ruling 10); T0 A8R8G8B8 64 with T1 A8R8G8B8 32: 188 on both; on the first pair a `Clear(1,
  (8,8)-(24,24))` to 0x404040 over that: (16,16) reads 137 and 0.538, (40,40) the first Clear's values.
- `mrt-size`: T0 64, T1 32: the bind is `D3D_OK`; both cleared green (whole); MRT2 red, blue: T0 red at (48,48)
  and (8,8), T1 green. `Clear(1, (8,8)-(48,48))` to white: T0 white at (40,40), red at (56,56); T1 white at
  (24,24), green at (4,4). Then `SetRenderTarget(0, a 32 texture U)` with T1 kept: MRT2 red, blue: U red, T1
  blue; `SetRenderTarget(0, T0)` again, MRT2 yellow, magenta: T0 yellow, T1 still blue. A second pair, T0 64 with
  T1 128, each cleared green at index 0 before the bind; MRT2 red, blue under T0's viewport (0,0,64,64): T0 red, T1 green; a whole Clear to white under that viewport: T0 white, T1 white at (32,32) and
  green at (100,100) (the viewport's rect on T1's own extent).
- `mrt-switch`: one attempt, nothing between the two draws but the rebind: T0 with A at index 1, both cleared
  black first; MRT2 red, green; `SetRenderTarget(1, B)` (B the same size and format, cleared black before the
  attempt's first draw); MRT2 red, blue; then read: A green, B blue. Then T0' (another 64 texture) at index 0
  with B kept, the viewport back to (0,0,64,64): MRT2 white, yellow: T0' white, B yellow.
- `mrt-scissor`: T0, T1 cleared blue; scissor (16,16)-(48,48), test on, MRT2 red, green: (32,32) red / green,
  (8,8) blue on both; both cleared blue, test off, viewport (0,0,32,64): (16,32) red / green, (48,32) blue.
- `mrt-depth`: T0, T1 256x256 with the auto depth-stencil, Z cleared to 1; `ZENABLE` on, LESSEQUAL; MRT2 red,
  green at z 0.2 under the viewport (0,0,128,256), then MRT2 white, white at z 0.8 under the full viewport: (64,128)
  red / green, (192,128) white on both.
- `mrt-back`: (`frames`, clear blue) the back buffer at index 0 with a 256x256 texture T1 at index 1: MRT2 red,
  green; index 1 unbound; T1 shown in the square (64,64)-(192,192): the back buffer red at (32,32), green at
  (129,129). Then (`until`) a 256x256 A8R8G8B8 texture at index 0 with **the back buffer at index 1**, both
  cleared to 0x40285078, DESTALPHA/INVDESTALPHA, MRT2 white, white: the texture (94,124,154) and the back buffer
  (94,124,154) too (it counts as alpha-bearing and stores the Clear's alpha: 3d3's ruling 10, parity), read with
  `GetRenderTargetData`.
- `mrt-autogen`: (`frames`, clear blue; `rt-autogen`'s set-up: a 64 X8R8G8B8 texture made with
  `D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP` and 0 levels, MIN/MAG LINEAR, MIPFILTER POINT) a 64 texture
  T0 at index 0 and the autogen texture G at index 1, a whole Clear to red; `SetRenderTarget(1, NULL)`; back to
  the screen, G shown in an 8x8 quad (level 3): red at (128,128). A second `frames`: the same with
  `SetRenderTarget(1, another texture)` in place of the unbind (the slot's swap), then the unbind: red.
- `mrt-order`: T0, T1: MRT2 red, red; `Clear(1, (0,0)-(32,64))` to green; MRT2 white, white under the viewport
  (0,0,64,32): on both, (16,16) white, (16,48) green, (48,48) red, (48,16) white.
- `mrt-read`: T0, T1; MRT2 red, green; `GetRenderTargetData(T1)` with T1 bound and the pass open, then MRT2 blue,
  yellow with no other call, then both read: the first read saw green, T0 blue, T1 yellow (a read-back between
  two draws of one set keeps the order and the set).

## Review dispositions

- **R1** (design; 0 Critical, 5 Important, 12 Minor): I1 → `reset()`'s three masks; I2 → `mrt-switch`'s one
  attempt, the viewport check moved to `rt-mrt`; I3 → `mrt-autogen` on `rt-autogen`'s set-up, with the slot swap;
  I4 → wrappers named a row, `mrt-back` reworked; I5 → the back buffer at index 1 (`mrt-back`),
  `COLORWRITEENABLE3` (`mrt-four`), target 0 changing size (`mrt-size`); M1 → the caller sentence; M2, M3 → ruling
  3's text and cost; M4 → `targets-runtime` in every interim expectation; M5 → `the_tile_barrier_names_each_slot`;
  M6, M7 → the derived numbers, `rtIsF`; M8 → ruling 10; M9 → `mrt-clear`'s resets; M10 → stated gap; M11 → the
  blend factor 0x80808080; M12 → MRT2 in `mrt-srgb`.
- **R2** (core; 0 Critical, 1 Important, 11 Minor): I-1 → the fakes' tile entry, the rt test, ruling 11 (the
  optional Metal row not taken); M-1 → Review Focus 2 restated, the hole variant; M-2 → `pub(super) pass_set`;
  M-3 → the destructured formats, the narrowing, `const fn`, the blend helper; M-4 → `open_render_pass`; M-5 →
  the file lists; M-6 → a 64x64 target 0 in the rt tests; M-7 → the debug assert in `pipeline_source`; M-8 → the
  skip tied to `render_open`, `close_pass` zeroing; M-9 → slot-indexed masks; M-10 → Out (C4d); M-11 → mutant (d)
  replaced by a safe one, the size rule's proof named.
- **R3** (exec; 0 Critical, 4 Important, 20 Minor): I1 → `targets-runtime`, the stop-early cut; I2 → `reset()`;
  I3 → the resets inside `mrt-clear` and `mrt-switch`; I4 → one correction rule, the derived values, ruling 10;
  M1 → Task 4's call sites; M2 → `--update-derives` first, the inventory file; M3 → the caller sentence; M4 →
  `key_extras`, the 64x64 target; M5 → `rtIsF`, `mrtDraw`'s z, the sentinel; M6 → `mrtMode`; M7 → the script edit
  points; M8 → Step 0's comments, the filter, `JC_D3D9_ONLY`; M9 → the ledger's path; M10 → the probe's retake;
  M11 → `extra_write_mask` called; M12, M13 → the signatures, `open_render_pass`; M14 → the logged drop; M15 →
  Review Focus 2; M16 → `ja.sh` with msl; M17 → commit bodies; M18 → the order in the unbind; M19, M20 → the
  bool note, the commands and the constraint's wording.

## Execution notes (2026-11-16)

- Built as planned in four commits on DXMTRS over 8bca6b74: 04a8702e (the fakes: colours 1-7, the tile pipeline),
  95f47ff7 (draws to several colour targets), 9cd00b09 (Clear over every bound colour target), d4714e45
  (`SetRenderTarget` at index 1-3). dxmt-core, d3d11 and winemetal are untouched. Tasks 2-5 were written by an Opus
  subagent from this plan, one writer, this session running the lanes and gates (ledger ruling E-1); its commits
  carry its own co-author line.
- Controls: all 20 `mrt` rows and `rt-mrt` were green on the default runtime at their first run, on the Mac (default
  and msl) and in the VM (default, old WoW64, x64 and x86), before any core work; no expectation had to be corrected
  and no defect of mtld3d was found. Each lane read as its step expected: after Tasks 3 and 4 exactly as the control
  (core red on `targets-rt-mrt`, `targets-runtime` and the `mrt` launch alone), after Task 5 both runtimes green,
  with no fix round.
- Changes from the plan's text, each ledgered: the eleven new rt tests of Task 3 sit in `target/tests.rs`; a
  `slot_target(slot)` helper serves `color_size`, the clears and `open_pass`; `open_pass` attaches a slot only when
  the key names it and the set has it; mutants (b), (e), (f) and (g) are written so the PE build keeps no unused
  item (the same defects). The appendix above was corrected to the rows where they differed (the float of a 64 grey
  rect is 0.538; the fixed-function quad of `mrt-unwritten` is blue; one probe of `mrt-clear`; how the 128 pair of
  `mrt-size` is cleared): the rows were the ones proven on mtld3d.
- Incidents, none in the code: the first controls job stopped at the build because cargo's crate cache had been
  emptied that evening (restored with `cargo fetch --locked`, the lock files' crates only: ruling T1-1); the VM
  control stopped because the builder VM was down after a restart of the Mac and its start line names a share
  folder that is gone (started by hand with the folder that exists: ruling T1-2; the gate jobs' start line wants the
  same correction).
- Gates on DXMTRS d4714e45: the Mac's default, msl and core lanes ok; the stateblock conformance under core equal to
  default; every core launch under Metal's validation layer with no report (the `mrt` launch included); the seven
  mutants each red on its row and in no other launch; the plain build's translator key and imports unchanged, the
  Mac's Wine conformance passed (triaged sites only) and the x64 check all passed; the VM's D3D9 lanes (both
  runtimes, old and new WoW64) ok at the first run. The VM's D3D10-12 gate was not rerun (ruling 6).
- Final review (Opus): no Critical, no Important, nine Minor. Two fixed on the MacNeutron side before the VM lanes
  and controlled by a lane of their own: the once-logged "a draw's colour targets are not its key's" warning now
  fails its launch in `d3d9-check` and the VM's gate, and `mrt-mask` draws with `COLORWRITEENABLE` 0 on index 0
  (index 1 still written). Two went into this document (the appendix, the Rules check's three near-duplicates).
  Five deferred (the ledger and the close-out list them): the test-only `open_render_pass` opens a pass `draw_up`'s
  shortcut trusts; no test of `set_extra_target`'s "false and nothing changed"; `set_extra_target` has no
  `#[must_use]` and the unbind discards its answer; `core_extras` narrows the mask with `unwrap_or(0)` where
  CONVENTIONS asks for `expect`; `forget_texture`'s doc reads as if extras were emptied only with target 0.
- Probe (`build/c4b3d/s4/probe.md`), on a Mac that was not quiet: this tip read the core at 3.93 and 4.13 ms a
  frame (tex) against mtld3d's 1.47 and 1.93; C4b-3d3's unchanged tip, rebuilt and run in the same window, 3.85
  against 1.87 (it had read 2.18 on a quiet Mac). No difference between the two tips exceeds one run's spread; a
  quiet-Mac figure is owed to C4b-3d5's probe.
- After the push the maintainer asked for the five deferred minors: DXMTRS 5f9e5caa (2026-11-16) makes
  `open_render_pass` the tests' alone, `set_extra_target` `#[must_use]` with the unbind asserting its answer, adds
  the test of its refusals, takes a write mask's low byte in `core_extras` and corrects `forget_texture`'s doc.
  Rust gate and the Mac lane (default and core) green; MacNeutron pins it.
