# DXMTRS C3c: D3D9 fixed function through the DXSO translator — Implementation Plan

> **For agentic workers:** execution is **subagent-driven** (`superpowers:subagent-driven-development`), as the DXMTRS
> ledger's per-step loop runs it: one implementer per task (Sonnet where the task is mechanical, Opus for design and
> correctness), each task reviewed as the Execution table says. Steps use checkbox (`- [ ]`) syntax for tracking.

**Go-ahead.** The maintainer's "Start C3" (chat, 2026-11-07) covers all of C3; the standing DXMTRS authorization
(ledger, 2026-10-29) self-approves this plan after three adversarial Opus reviews. No ruling here amends a C3 ruling
except C3c-4, which realizes C3-18's "FF records gain their source" for AIR records and corpus records only (the MSL
lane stays byte-identical, C3b-19); it is ledgered and reported, not waited on.

**Goal:** under `shader.backend=air`, a fixed-function (FF) stage is built as DXSO tokens from its `FfVsKey`/`FfPsKey`
(`vs_3_0`/`ps_3_0`, C3-18), parsed and lowered by the same `airconv_ir::dxso` front end and translated to AIR, so a pair
with an FF stage is AIR like a programmable one; AIR FF records carry their key and tokens (regenerable); the parity
corpus holds FF pairs from the synthetic set and from Wine's conformance run, rendered against `ff.rs`'s MSL, with
`ff-skipped` gone. `msl` behaviour stays byte-identical to C3b's.

**The translator key moves.** C3c changes `src/airconv` (four FF-only lowering hooks, C3c-2, and the writer, C3c-3), so
the key `c298383e…` moves once to a new value recorded at Task 9 Step 1 (after the last `src/airconv` commit; lock jobs
before it read the key from the build's `DXMT/translator` file, never a literal); the 13-corpus DXBC/DXIL identity check
(`xlate.sh`, C3-10) reruns on the new tree and must print `ok` with no `air-only` line; the VM host's `winemetal.so` and
the Mac's `winemetal.so`/`mtld3d.so` are rebuilt with it (Task 9's job order). Players' D3D10-12 caches turn over once at
the next release (C3-9 holds releases to C3d). The emitter fingerprint (`SHADER_EMITTER_VERSION`, `W
src/d3d9/core/build.rs:21-60`) does **not** move (C3c-3), checked as a host row.

**Split.** C3c is this plan (the C3 plan's C3c outline, rebased on C3a/C3b's code). Not here: the default flip and the MSL
emitters' retirement (C3d); game caches and benches (outward, parked).

**Architecture.** `mtld3d_core::ff_build::{vertex, pixel}` builds an IR program from the key with typed helpers and
serializes it with `airconv_ir::dxso_write::write_parts`; `parse` turns the tokens back into a `DxsoProgram`, the C3b
prefilter (`lower::vertex`/`lower::pixel` with the pair's real spec) decides support, and `translate_dxso` makes the AIR
as for a programmable stage. FF reads that no `c#` row can name (VsDraw's point/scale/inv_view rows, PosFixup's row 0,
the bump environment's luminance rows) go through register numbers no valid D3D9 shader can name (`c2032..`, a PS
source of token type 11), and world-space clip planes through an `o31` output no valid `vs_3_0` declares (C3c-2): no spec or wire change,
no guest change. The pair rule becomes `(Prog|FF) × (Prog|FF)` is AIR iff both lower and neither stage has failed, with
FF stages identified by key (C3c-6). AIR FF records are kinds 8/9 with an FF `ShaderSource` (key + tokens + spec).

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), LLVM 23 (airconv), Metal, sh.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C3), §7, §8. **C3 plan:**
`docs/superpowers/plans/2026-11-07-dxmtrs-c3-dxso-translator.md` (C3-1…C3-24, the C3c outline, Open risks). **C3b plan:**
`docs/superpowers/plans/2026-11-09-dxmtrs-c3b-runtime-switch.md` (C3b-1…C3b-19, gate rows). **Ledgers:**
`.superpowers/sdd/dxmtrs-program.md`, C3b's `.superpowers/sdd/2026-11-09-dxmtrs-c3b-runtime-switch/progress.md` (gate
figures, pitfalls, deferred minors), this step's `.superpowers/sdd/2026-11-09-dxmtrs-c3c-fixed-function/progress.md`.
**Map:** `build/c3c/map-vs.md` (FF VS vs the lowering), `build/c3c/map-ps.md` (FF PS), `build/c3c/map-plumbing.md`
(runtime, cache, corpus, IR, facade). `W` = DXMTRS tree, `M` = MacNeutron tree; file:line refs are at b4da41c5/47a8c26.

**Bases.** DXMTRS `main` b4da41c5 (C3b), MacNeutron `main` 47a8c26 (pins it). Rebase before running if either moved.

---

## Rulings

Standing authorization plus "Start C3": each default below is self-approved after the three reviews. A later fact that
breaks one goes to the step ledger and, when it changes the plan, into the report.

- **C3c-1 (scope):** the outline: FF VS and PS builders, the writer and its round trip, AIR FF records, the corpus and
  the parity FF pairs, the pair rule. Every FF feature `ff.rs` emits goes through the builder; no FF key is refused by
  design (the prefilter still answers MSL for a key whose lowering fails, C3b-2/3). Also folded: C3b's deferred minor
  "list `CachedKind`'s kinds explicitly before 8/9 arrive" (required: today `is_air` is `>= 10` and `is_programmable`
  is `!FfVs|FfPs`, so 8/9 would read as programmable MSL, `W core/src/shader_key.rs:96-146`). Not folded: "route the
  facade's SM50 family 74..=88" (FF uses slots 74-80 only, `map-plumbing.md` §5) and the other C3b minors. Cost if
  wrong: a minor found again at C3d.
- **C3c-2 (FF-only lowering hooks, no spec flag; the key moves):** four hooks in `src/airconv/ir/src/dxso/lower*`,
  each reachable only through register numbers no valid D3D9 shader can name under DXMT's caps (256 VS constants,
  `W core/src/caps.rs:60`; DXMT forces hardware vertex processing, `W src/device.rs:12105-12127`), so every existing
  program lowers byte-identically (checked: Task 3 Step 2 translates the whole parity corpus before and after and
  compares an AIR digest kept in `$S/base/air-digest.txt`):
  (a) **VS constant window:** a direct `c#` read at `c2032 + r`, `r` in 0..12, reads `VsDraw` row `r` (point, point
  scale, inv_view 0-3, clip 0-5, `W src/d3d9/core/src/vs_draw.rs`); `c2044` reads `PosFixup` row 0 (x, y, z, w). No
  alias for `PosFixup` row 1 (a 20-byte buffer: a float4 read of row 1 over-reads, `W
  src/airconv/src/dxso/bindings.rs:13-14`); relative reads never alias (the window is for direct reads only; a
  relative read reaching it reads the buffer as today). (b) **RHW depth clamp:** in a program with a direct `c2044`
  read, the VS epilogue gains, after the depth bias, `if pos_fixup.z != 0 { z = clamp(z, 0, w) }` (`ff.rs:1215-1221`'s
  rule, the same place in the order: after `fog_z` and the bias). Every FF RHW program reads `c2044` (C3c-7's RHW
  rule); a point-scale key also reads it and gets an inert clamp (`pos_fixup.z` is non-zero only on FF RHW draws, `W
  src/d3d9/unix/src/draw.rs:1007-1009`); no existing program changes. (c) **World-space clip basis:** when a `vs_3_0`
  program declares `dcl_position o31` (no valid `vs_3_0` has `o12+`), `plan_vertex` gives that register a private
  mirror (today it would alias the position mirror, `W lower/vs.rs:200-221`, and overwrite the clip position), not a
  varying, not in the signature and not in `Lowered.outputs`; the epilogue's clip loop (`W lower/vs.rs:381-390`) dots
  that mirror with `VsDraw.clip[i]` instead of the pre-fixup clip position. The FF builder writes `o31 = inv_view ·
  pos_view` (through the window), so clipping is `ff.rs:1296-1306`'s. The builder allocates its outputs from `o0` up
  (at most `o20`: position, 8 texcoords, 2 colours, fog, psize, 8 passthrough extras), never `o31`. (d) **Bump
  luminance rows:** a pixel-stage source of token register type 11 (`RegKind::Output`, `W parser.rs:351`; not the
  `vs_3_0` `o#` type 6), refused in a PS today (`W lower/regs.rs:443`), at index `r` reads `bump_env` row `r`, and the
  lowering's bump-env binding decision (`W lower.rs:196`) also counts such a source, computed in `lower/` (not by
  editing `DxsoProgram::uses_bump_env` in `ir.rs`, which the emitter fingerprint hashes by name, `W
  core/build.rs:28-33`). Cost if wrong: a hand-written invalid program (or a software-VP title's shader naming
  `c2032..c2044`, which today reads out-of-bounds rows) gets FF meanings instead of garbage reads; (b) four ops per FF
  RHW or point-scale VS. Rejected: a `VsSpec.fixed_function` flag
  (wire 6→7 bytes across about eight files in both repos incl. the i686 converter, `map-plumbing.md` §5); clip-space
  FF planes on the d3d9 side (changes the MSL FF, breaking C3c-9).
- **C3c-3 (placement; the fingerprint stays):** the writer is `src/airconv/ir/src/dxso_write.rs` (`pub mod dxso_write;`
  in `ir/src/lib.rs`, not in `dxso.rs`, which `core/build.rs` hashes by name): `pub fn write(p: &DxsoProgram) ->
  Vec<u32>` and `pub fn write_parts(shader_type, major, minor, &[Declaration], &[DefConstant], &[DefIntConstant],
  &[DefBoolConstant], &[Instruction], &BTreeMap<u32, Vec<Instruction>>) -> Vec<u32>` (`write` wraps it; a
  `DxsoProgram` cannot be built outside `dxso`, `bytecode` is `pub(super)`, `W ir/src/dxso/ir.rs:298`). The builder is
  `src/d3d9/core/src/ff_build.rs` (+ `ff_build/{vs.rs,ps.rs,asm.rs,tests.rs}`), outside `core/src/dxso/` (whose `.rs`
  files the fingerprint hashes, `core/build.rs:23`). No edit to `core/src/dxso/**`, `ir/src/dxso.rs`, `ir/src/dxso/{ir,
  link,opcode,parser,spec}.rs`, `vs_draw.rs`, `ps_draw.rs`, `shared/src/{mtl,shader_create}.rs` (the fingerprint's
  inputs); IR types the builder must construct get public constructors or fields only if they are already `pub`
  (checked in Task 2; if one is not, the builder emits tokens through `dxso_write`'s token helpers instead). Host row:
  the fingerprint printed by `emit_corpus`'s header equals C3b's. Cost if wrong: one fingerprint move (players'
  sourceless FF MSL records dropped and rebuilt live, programmable ones regenerated at prewarm, as C3-20).
- **C3c-4 (FF sources; amends C3-18's "FF records gain their source" for MSL records):** `ShaderSource` gains FF arms
  `FfVertex { key: FfVsKey, provided_input_mask: u16, tokens }` and `FfPixel { key: FfPsKey, variant: VariantKey,
  tokens }`. No FF key codec exists today (`shader_record.rs:205-218` is layout asserts; the snapshot wire copies the
  struct), so Task 6 writes a field-by-field codec that refuses only what would be undefined behaviour or a panic in
  `ff.rs`/`ff_build` (a flag bit outside `FfVsFlags`/`FfStageFlags`, a `bool` byte above 1, a count or set index past its
  array: `tex_coord_count`/`input_tex_coord_count` > 8, `tci` set > 7, `tex_coord_dims` > 4, `vertex_blend_count` > 4,
  `clip_plane_count` > 6, a passthrough code `link.rs` cannot name) and loads every value `ff.rs` merely warns about
  (material source > 2, TCI mode > 4, unknown ops/args). `disk_key()` = the MSL twin's key through the existing
  `vs_source_disk_key_ff`/`ps_source_disk_key_ff` (`W core/src/draw_data.rs:889-896`), `stage_spec()` = `Vs(VsSpec {
  provided_input_mask, clip_plane_count: key.clip_plane_count, sampler_kinds: default })` / `Ps(variant)`, `emit()` =
  `ff.rs`'s `emit_{vs,ps}_ff_named`, decode by kind (0/1/8/9 FF, 2-7/10-15 programmable). AIR FF records (kinds 8/9)
  always carry one. MSL FF records (0/1): the runtime writes none, under either backend (`LibraryInput::source()` is
  `Some` for an FF input only when its backend is `Air`; an FF stage demoted to MSL under `air` persists sourceless:
  byte-identical MSL records; older builds still read them, `W shader_cache.rs:612`); the reader accepts one (corpus
  records from `emit_corpus --write-cache`, which cannot make metallibs: mtld3d-core has no airconv dependency). No
  `SHADER_CACHE_SCHEMA_VERSION` or `CACHE_FORMAT_VERSION` bump (new kinds are skipped by older builds, C3-11, and a C3b
  build's compaction drops them: one AIR FF rebuild after switching builds; a 0/1 record with a source is dropped by a
  C3b build, corpus-only). Cost if wrong: MSL FF records stay unregenerable until C3d deletes them.
- **C3c-5 (AIR FF staleness):** the builder is outside the translator key, so a builder fix would leave stale AIR FF
  metallibs. Prewarm's `compile_library` (under `air`, with a key) rebuilds `ff_build` from the record's key and
  compares tokens; a mismatch regenerates the record from the new tokens (translate + re-append, as C3b's stale-key
  path, `W unix/src/shader_prewarm.rs:385-397`); a matching record with another translator key regenerates as today.
  `ff_build` must be deterministic (a test). The parity harness rebuilds FF tokens from the key too and counts records
  whose stored tokens differ (`<s> ff-stale` on its line, which must be 0 for a freshly written corpus). Cost: one build
  per AIR FF record at prewarm (microseconds each); no version constant to forget.
- **C3c-6 (pair rule and failure identity):** `PairRule` (`W unix/src/encoder/compile/libraries.rs:454-546`) answers,
  under `air`, AIR iff both stages lower with the pair's spec and neither has failed as AIR; FF stages lower through
  `ff_build` → `parse` → `lower::{vertex,pixel}`, memoized on the encoder thread per `FfVsKey` (`Option<Arc<
  FfBuilt>>`, `None` = refused or failed, so a failure lives in the memo and the probe stays a borrow) and per
  `(FfPsKey, VariantKey)` (bool, beside a per-`FfPsKey` `Option<Arc<FfBuilt>>`), `FfBuilt` = tokens + parsed
  `DxsoProgram` + the VS's input mask; the built program rides in `LibraryInput::FixedFunction{Vs,Ps}` so the worker
  does not build it again. Failures are `AirStage { Program(ProgramId), FfVs(FfVsKey), FfPs(FfPsKey) }` (`fail` clears
  the FF memo entry; the refusal/failure info lines name the FF key's disk key instead of a `ProgramId`);
  `PipelineIdentity.air_programs` becomes `Option<(AirStage, AirStage)>` (cloned on a pipeline build only). One
  `ff_kind(is_pixel, backend)` helper names the FF kind (0/1 or 8/9) for `begin_{vs,ps}_library` **and**
  `pipeline_shader_refs` (`W compile.rs:2037, 2044`, which today stamps FF as 0/1 always: an AIR FF pair's recipe would
  name MSL records, and an FF × programmable AIR recipe would mix backends and be dropped as malformed by
  `PipelineRecipe::is_valid`, `W core/src/shader_cache.rs:873-876`). FF indexes become per backend (`[BuildIndex<
  FfVsKey, _>; 2]`, `[FxHashMap<FfPsKey, BuildIndex<VariantKey, _>>; 2]`, indexed by `backend as usize`; a tuple key
  would need an owned key per draw). This retires C3b-2's one cost (a VS paired with an FF PS built by both compilers).
  Cost if wrong: an FF key that lowers but fails in LLVM/Metal drops one frame's draws once per launch (C3b-3).
- **C3c-7 (builder rules, from the maps):** VS: `vs_3_0`; `vN` = the FF attribute index (`ff_attr_index_for_semantic`,
  `W ff.rs:653-665`; passthrough `v15..v22`), declared exactly as `emit_vertex_in` declares (`ff.rs:753-791`), mask bit
  15 when passthrough[0] is set; reads only the `c#` rows `ff.rs`'s MSL reads for that key (a test compares the two sets
  per key, C3c-10); literals by `def` at `c256+` (never shadowing an FF row; a direct read of a def index is the def;
  few `def`s, lanes shared, since each one costs an icmp+movc per relative palette read, `regs.rs:522-536`);
  colours `_sat` where `ff.rs` saturates (SM3 does not clamp, `lower/vs.rs:368`); `oFog` always written in full as
  `ff.rs` writes it (`(f,0,0,0)` fogged, `(1,1,1,1)` unfogged, the whole attribute for an RHW FOG0 passthrough,
  `ff.rs:1186-1196`; without a write the epilogue takes `color1.w`); no `lit` (power clamp ±128, `ops.rs:342-361`);
  `length` as `rcp(rsq(d))` (`d·rsq(d)` is NaN at 0); the blend index as `ff.rs:959` has it: `t = min(max(idx, 0), 39)`,
  rounded as `r = t + 0.5; fl = r - frc(r)` (half up, against `rint`'s half to even: only float indices at .5 differ,
  R5), then `× 4`, then `mova a0.x` (`mova` of an unrounded `4·2.4` would give 10); at most 32 temporaries
  (`regs.rs:22`, asserted); outputs allocated from `o0` up with the extras' semantics equal to `passthrough_outputs`
  (`W unix/src/encoder.rs:5638-5654`), `o31` reserved (C3c-2c); RHW: the body writes `ff.rs:1104-1135`'s position
  minus `pos_fixup.xy · w` (read from `c2044`) so the epilogue's half pixel adds it back (`ff.rs`'s logical-viewport
  half pixel kept; exact in algebra, 1-2 ulp in bits: triage reason "RHW half-pixel reassociation"); point size: oPts
  written with `ff.rs:894-907`'s scale (window rows) and left to the epilogue's clamp × `pos_fixup.w`. `ff.rs`'s private
  predicates (`reads_eye_normal` `:705`, `input_dim` `:797`, `cascade_sampled_stages` `:2013`, `reads_temp`'s text rule
  `:1807`, `is_bump_env*`, the `*_unbound_fallback` checks `:521-547`, `op_reads_argument` `:2291`) are re-derived in
  `ff_build`, never made `pub` (that edits `core/src/dxso/`, the fingerprint), each cross-checked by a Task 4/5 set test.
  PS: `ps_3_0`;
  inputs `dcl_color0 v0`, `dcl_color1 v1`, `dcl_texcoordN v(2+N)`; `dcl` every bit of `sampled_stage_mask` and sample
  only per `ff.rs`'s walk (`map-ps.md` §1a); constants `c0..c8` from the buffer, literals by `def` at `c16+`; blends as
  `mul`+`mad`, not `lrp`; `_sat` on the last instruction of an op only; projected samples guarded with `cmp` for
  `w == 0` before `texldp`/`_dw` (`map-ps.md` G2); bump: `bem rn.xy` with `rn` the bump stage's index (matrix
  `bump_env[2n]`), `rn.zw` pre-written; luminance through C3c-2(d); no fog, alpha test, sRGB, sample mask, point sprite
  or flat code (the lowering applies the variant); an unhandled argument reads `(1,1,1,1)` as `ff.rs:2317` does. Both:
  `ff.rs`'s warning sites (VS four: absent set `:819`, TCI 1-4 on RHW `:1165`, TCI > 4 `:1621`, material source > 2
  `:1769`; PS three: unhandled argument `:2317`, unhandled modifier `:2334`, unhandled op `:2387`) become warnings the
  builder returns in `FfProgram.warnings: Vec<&'static str>` (C3-23's convention: the caller logs them once under
  `mtld3d::dxso` with the same text), so tests assert them without depending on test order. Cost if wrong: a parity
  `differ` (the check).
- **C3c-8 (parity FF inputs):** FF recipes render with FF uniforms, programmable recipes with today's (so C3b's 159/159
  and 147/149 stay comparable): FF VS rows from an `FfState` with a fixed, non-degenerate state (WORLD a rotation and
  scale, VIEW a non-identity look-at, PROJ a perspective, two lights, material with power 12, fog 0.2..0.8, TT matrices
  scaling, a 40-matrix palette filled row by row (`FfState` has no palette helper), RHW row 0 = the **logical**
  viewport `(32, 32, 0, 0)` as `device.rs:13506-13514` packs `dev.viewport()`); `VsDraw` with plane 0 non-trivial in
  world space and `inv_view` = the VIEW's inverse transpose (as `vs_draw.rs` packs it), point `(4, 1, 16)`, point
  scale `(1, 0.1, 0.01)`; `PosFixup` from the **target** viewport 64×64 (`(1/64, -1/64, z, 2, bias)`, `render.scale` 2:
  the logical half pixel differs from the target one, exercising C3c-7's RHW rule), and `z` = 1 with a non-zero bias
  on RHW recipes whose position has `z` outside `[0,w]` (exercising C3c-2(b)); `bump_env` 16 rows, each different
  (today 8 identical rows, `dxso_parity.rs:260`: a wrong bump stage would pass and stages 4-7 would read past the
  buffer). Point size is invisible to a triangle render: covered by Wine's visual
  point tests in the AIR conformance lane (`visual.c` point-sprite and point-size tests) and Task 4's builder tests.
  Cost if wrong: an FF drift outside these inputs passes the corpus (conformance's AIR lane is the second oracle).
- **C3c-9 (MSL lane byte-identical):** `ff.rs`, `emit.rs`, `synthetic_ff()` and `synthetic_sm()` are not edited; MSL FF
  records stay sourceless at runtime; under `msl` no FF builder runs (`pair_backend` returns before any FF work, `W
  libraries.rs:481-483`). Host rows: `msl-digest 730b0a5c1332e44f 131 shaders` and the emitter fingerprint = C3b's.
  New FF corpus shapes (C3c-10) live in a separate function used by `--write-cache` only. Cost: none.
- **C3c-10 (corpus):** `synthetic.bin` gains the synthetic FF set as MSL FF records with sources (kinds 0/1) and
  recipes pairing them: `synthetic_ff()`'s keys plus `synthetic_ff_extra()` (clip planes 1 and 3, indexed and
  sequential blending 1-4 with UBYTE4 indices, RHW with passthrough extras and with `z` outside `[0,1]`, point
  scale, spot lights, every TCI mode, PROJECTED counts 2-4, every D3DTOP colour/alpha op, TEMP results, bump
  BUMPENVMAP and LUMINANCE with and without a texture, projected bump, specular add, an FF PS stage on a depth, cube
  and volume slot, `LOD_BIAS`, raw depth, projected depth), vertex attrs from a `D3DVERTEXELEMENT9` list the corpus
  builds per key (`convert::resolve_attrs_for_ff` takes elements, not a key), and cross-family pairs (FF VS × a
  `ps_1_x`/`ps_2_0`/`ps_3_0` from `synthetic_sm`, `vs_1_1`/`vs_2_0`/`vs_3_0` × FF PS, and an RHW-with-passthrough FF VS
  × a `ps_3_0` reading the extras). The key lists live in `ff_build/tests.rs` (Task 4/5's tests) and again in
  `emit_corpus` (an example, which unit tests cannot call): a listed duplicate (Rules check). `conformance-x64.bin` is
  recaptured under `air` (Task 8: its programmable records become AIR kinds 10-15, FF records 8/9). Pairs that still
  went MSL under `air` (refused partners, demotions) leave sourceless MSL records: `--merge` drops sourceless records
  and the recipes naming them and prints the count (`merge: dropped <r> sourceless records, <p> recipes`), so every
  record kept has a source (proved by the kinds line on the merged file). The merged file replaces today's (README
  updated; the two C3b triage lines of the old file are re-checked against the new one). Each parity line must show a
  positive FF-pair count (`<n> ff` added to the line) and `0 ff-skipped` is removed together with its counter: a
  recipe naming a record without a source fails the file. Cost if wrong: the parity runtime grows with ~900
  conformance recipes (minutes on the host).
- **C3c-11 (gates):** C3b's set (Task 9) on the final tips, with the kinds awks counting FF (`/^(air_)?(sm[0-9]|ff)(vs|ps)=/`)
  and failing unless `air_ffvs > 0` and `air_ffps > 0` too, summed over the run's files (a full run's `visual` has
  hundreds of FF draws; an `--only` subset without FF prints the row as info); `d3d9-check.sh`'s air run copies its
  EXEs into `$WORK/exe/` (the cache sits beside the EXE, `W unix/src/shader_prewarm.rs:3`, as C3b's 614cb03 did in the
  VM) and checks the same kinds row on the kept cache (its d3d9_triangle is FF: after C3c it is the first FF AIR proof
  on wine.app and on the loopback). **First FF AIR verdict:** the capture's conformance verdict is recorded whatever it
  is; each new failing site is root-caused in lane `pt` (a builder bug fixed with a test, a numeric reason triaged in
  `dxmt/mtld3d-triaged.txt`); a Metal validation error or a GPU hang stops Task 8 until fixed. Task 9's target: the
  AIR lane's visual failures ≤ C3b's air figure (30, arm64x leg) and the verdict line `passed`. Per the
  lighter-task-testing memory: each task runs its own tests; the one early lock job is Task 8's capture (it also
  proves `make wine-arm64` builds with the moved key). Gate scripts in `$S/gates/` are rewritten from C3b's, not used as
  copied: `S=$B/c3c`, `c3c` names, every env assignment as `run <n> env VAR=x …` (C3b's `mac.sh:43,47,65,66` used
  `VAR=x run`), keys read from the build, and a guard before each job: `/usr/bin/grep -nE '^[[:space:]]*[A-Z_]+=[^ ]*
  run ' $S/gates/*.sh` prints nothing. Cost if wrong: a gate found late.
- **C3c-12 (no release):** C3-9 holds; nothing is tagged or released; the parent pushes after reading the gate logs.

## Global Constraints

- **Who pushes.** Nobody in this plan pushes, merges to `main`, fetches or downloads; Task 9 stops before the Push
  order steps, which the parent runs. No MacNeutron.app, Steam or game; nothing under `~/Library/Application
  Support/MacNeutron` or `steamapps/compatdata`; kill by exact PID or process group, never `pkill -f`; home written `~`
  in anything committed; no SteamID or account name printed; no hook bypass; no PR to DXMT upstream. Commits end with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (every implementer, C3b R3); DXMTRS subjects `<area>:
  sentence`. Never delete a branch; worktrees go with `git worktree remove --force` after `chmod -R u+w` of the filled
  submodules; never `git submodule deinit`.
- **Hold.** If the parent says the maintainer is playing or moving: stop the running lock job by its process group,
  run nothing heavy, ledger where it stopped; a killed run is void and reruns in full.
- **Usage stop rule:** weekly usage checked between tasks; at 95% stop (state in the ledgers); at 90%+ only work that
  lands something already done.
- **Names.** `$M` = `~/Documents/MacProton`, `$D` = `$M/build/wine-arm64-src/dxmt`, `$S` = `$M/build/c3c`, `$B` =
  `$M/build`. Integration worktree `$S/wt` (DXMTRS `c3c` from b4da41c5), `$S/wt-mn` (MacNeutron `c3c-mn` from 47a8c26).
  Lanes: `sh $S/lane.sh <lane>` cuts `$S/wt-<lane>` on `c3c-<lane>` from `c3c` (submodules filled read-only); `sh
  $S/integrate.sh <lane>` rebases, fast-forwards `c3c`, checks `git cherry` empty. A conflict in `Cargo.lock` or
  `tools/derive_inventory.txt` is resolved by regenerating them. Each lane uses its own cargo target dir
  `$S/cargo-<lane>` (`CARGO_TARGET_DIR`); a merged lane's dir is deleted at integration (disk). `$S/wt-mn` commits add
  explicit paths only.
- **The lock.** Every build or install into the shared folders, every gate reading installed outputs and every
  checkout in `$D` runs as one job: `/usr/bin/lockf -k $B/pipeline.lock sh <job>.sh`, the job logging its `$D` sha and
  wine.app's CDHash, its trap restoring `$D` to `$(cat $B/wine-arm64-src/dxmt.applied)` (fa3bc094) and the main
  checkout to `main` (`git -C $M status -sb` = `## main` after every job). Mac/VM jobs run with the main checkout
  detached at `c3c-mn` (`M dxmt/d3d9-check.sh:20-26` reads `$ROOT/build`), templates `$B/c3b/gates/{mac,vm}.sh`
  (copied to `$S/gates/`). Env: `$B/c3/gates/env.sh` for Mac jobs, `env-vm.sh` for VM jobs (llvm-mingw's bin first on
  PATH breaks `vm/host/build.sh`); `run <n> env VAR=x …`, never `VAR=x run …` (the assignment persists past a shell
  function, C3b ledger). VM order: `vm/dxmt-native/build.sh` → `vm/image/guest-stages.sh dxmt d3d` → builder sync +
  image → `vm/host/build.sh` → gates. While a lock job runs, lanes run no GPU work. Long jobs under `caffeinate -d -i
  -u` (d3d-smoke's present-held row fails while the Mac idles).
- **Builder.** `BUILDER_MAC=52:54:00:ea:c0:03`; reuse a running builder when `sh vm/builder.sh ssh true` answers; else
  `VM_SHARES="out=$B/vm-work/images,outp=$B/p/vm-work/images" sh vm/builder.sh start`.
- **Signing env** for `make wine-arm64`, `vm/dxmt-native/build.sh`, `vm/host/build.sh`, `vm/image/guest-stages.sh dxmt`:
  `MACNEUTRON_SIGN_IDENTITY="Developer ID Application: Chad Cormier Roussel (49QMZXLR8S)"`,
  `MACNEUTRON_PROVISIONING_PROFILE=$B/Mac_Neutron.provisionprofile`.
- **Rust.** Offline, pinned toolchain (`. $S/env.sh`: `AIRCONV_LLVM`, `AIRCONV_XCRUN`, toolchain PATH,
  `MACOSX_DEPLOYMENT_TARGET=27.0`, `CARGO_NET_OFFLINE`). **UT/CK/LX/RL** as C3b's Global Constraints: CK = nightly `cargo
  fmt --all --check`, clippy `-D warnings` (aarch64-apple-darwin and the shipped `-pc-windows-msvc` targets), `cargo doc
  -D warnings`, UT, `git add -A && sh tools/audit.sh` → `audit: clean (<n> files)`; LX = `tools/linux-cargo.sh` builds
  of `mtld3d-core`, `mtld3d-mtl`, `mtld3d-unix` for `x86_64` and `i686`; RL = `tools/rust-check.sh` → `rust-check: ok`.
  **KEY** = `sh $S/key.sh <wt>` (prints the key; its equality check is edited to the new key at Task 9 Step 1, and
  before that `KEY` means "prints a key": the lanes of Tasks 2/3 move it). **PAR** = `cargo test --offline --release
  -p airconv --test dxso_parity -- --nocapture` (its two or more `dxso-parity:` lines). **DIG** = `cargo run --offline
  --release -p mtld3d-core --features disk-cache --example emit_corpus -- --iters 1 --digest` → `msl-digest
  730b0a5c1332e44f 131 shaders` and the header's fingerprint = Task 0's.
- Conventions (`docs/CONVENTIONS.md`): no `#[allow]`/`#[expect]`; one-operation `unsafe` with `SAFETY:`; no
  `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; `FxHashMap`; no new mutable `static`; derives through
  `tools/audit.sh --update-derives`. Gating scans use `/usr/bin/grep`.

## Rules check (`W CLAUDE.md`)

| Introduced | Where | Why allowed |
|---|---|---|
| `CachedKind::{AirFfVs = 8, AirFfPs = 9}` | Task 1 | C3-11's reserved twins; unknown kinds are skipped by older builds, no schema bump |
| module `airconv_ir::dxso_write` (pub fns) | Task 2 | C3-18's assembler; leaf crate, no dependency (C3-23); outside the fingerprint's files (C3c-3) |
| four lowering hooks (window, depth clamp, clip basis, bump rows) | Task 3 | C3c-2; no spec/wire change; reachable only by invalid register numbers; the clamp only in programs reading `c2044` |
| module `mtld3d_core::ff_build`, `struct FfProgram` (derives `Clone, Debug`) | Tasks 4-5 | C3-18; outside `core/src/dxso/` (C3c-3); inventory regenerated |
| `ShaderSource::{FfVertex, FfPixel}` arms, an FF key codec | Task 6 | C3c-4; cache-file bytes only, no PE/Unix wire; validating decode |
| `enum AirStage` (derives `Clone, Debug, PartialEq, Eq, Hash`), `struct FfBuilt` (no derive beyond `Debug`), per-backend FF index arrays | Task 7 | C3c-6; failure identity for keyless stages |
| `synthetic_ff_extra()` in `emit_corpus`; `--merge` drops sourceless records | Task 8 | C3c-10; keeps `synthetic_ff()` and the digest untouched |
| schema / format version | none | C3c-4: new kinds are skipped by older builds (C3-11) |
| config keys, PE/Unix wire fields, dependencies, statics, lint suppressions | none | — |
| duplicates | `ff_build`'s formulas restate `ff.rs`'s MSL math in DXSO; `ff_build`'s re-derived predicates (C3c-7); the FF key lists in `ff_build/tests.rs` and `emit_corpus`; `dxso_write` beside the test-only token helpers (`emit_corpus.rs:1924-1960`, `parser/tests.rs:57,66`, …) | the point of C3c: C3d deletes `ff.rs`'s MSL, and the parity test holds the two equal until then; the predicates cannot be made `pub` without moving the fingerprint; an example cannot be called by unit tests; the test helpers stay (moving them edits fingerprinted or other lanes' files), ledgered for C3d |
| fingerprint companion rule (`docs/CONVENTIONS.md`: an emitter change moves `SHADER_EMITTER_VERSION`) | — | no fingerprint input is edited (C3c-3), checked by DIG |

## Review Focus

1. C3c-2: can any valid D3D9 program (any `vs_1_1..vs_3_0`, `ps_1_1..ps_3_0`, Wine's tests, the corpus) reach the
   window, `o31` or a PS type-11 source? Is every existing program's AIR byte-identical (Task 3's check)? Does the depth
   clamp sit after the bias and `fog_z` as `ff.rs:1215-1221` has it? Does the window read the right `VsDraw` rows
   (`vs_draw.rs` layout) and never `PosFixup` row 1?
2. C3c-3: is any fingerprint input edited (the file list in `core/build.rs:21-60`)? Does DIG's fingerprint hold?
3. C3c-6: does every pair the encoder forms (FF/programmable × FF/programmable, `SetPixelShader(NULL)`, no-colour
   siblings, deferred and `rt0_drop` waits, a PS variant that depends on the VS's `linked_input_mask`) end with both
   stages from one backend? Can an FF stage's AIR failure be recorded under the MSL index, or an MSL FF library be
   found under an AIR probe? Are the per-draw probes allocation-free?
4. C3c-4/C3c-5: every exhaustive and `matches!` site on `CachedKind`; decode bounds of the FF source (a short or
   hostile file; `disk_key` checked); MSL FF records still sourceless at runtime; the staleness compare.
5. C3c-7/C3c-8: per feature, does the builder's DXSO equal `ff.rs`'s MSL (formula, clamp, condition, rows read)? Do
   the FF parity inputs exercise each C3c-2 hook (a plane with non-identity view, `render.scale` ≠ 1, z clamp)?
6. C3c-10/C3c-11: does the merged conformance file really hold FF pairs (kinds line)? Do both awks fail on zero
   `air_ff*`? Is `0 ff-skipped`'s removal a real failure for a sourceless recipe?
7. C3c-9: nothing new runs under `msl`; the MSL lanes' figures equal C3b's.

## Execution

| Task | Lane | Implementer | Reviews | Depends on | Owns | Lock |
|---|---|---|---|---|---|---|
| 0 base, ledgers, plan commit | owner | owner | — | — | `$S/base`, ledgers, plan commit | no |
| 1 kinds 8/9, explicit arms, gate awks | `kinds` + `$S/wt-mn` | Sonnet | Sonnet | 0 | `core/src/{shader_key.rs,shader_key/tests.rs}`, every `CachedKind` match site the compiler or a grep finds (`shader_cache.rs`, `shader_cache/source.rs` arms only, `perf/compilation.rs`, `unix/src/encoder/compile.rs` tag arms, `unix/src/shader_prewarm.rs` arms), `core/examples/emit_corpus.rs` (`KINDS`), `shader_cache.rs:21` doc; `$S/wt-mn` `dxmt/{mtld3d-conformance.sh,d3d9-check.sh}`, `vm/dxmt/d3d9-gate.sh` | no |
| 2 writer + round trip | `write` | Opus | Sonnet + Opus | 0 | `src/airconv/ir/src/{lib.rs,dxso_write.rs,dxso_write/tests.rs}`, `src/airconv/tests/dxso_write.rs` | no |
| 3 lowering hooks | `hooks` | Opus | Sonnet + Opus | 0 | `src/airconv/ir/src/dxso/lower/{vs.rs,ps.rs,regs.rs}`, `lower.rs`, `lower/tests.rs`, `src/airconv/src/dxso/tests*`, `src/airconv/tests/dxso_identity.rs` | no |
| 4 FF VS builder | `ffvs` | Opus | Sonnet + Opus | 2, 3 | `core/src/{lib.rs,ff_build.rs,ff_build/{asm.rs,vs.rs,tests.rs}}` | no |
| 5 FF PS builder | `ffps` | Opus | Sonnet + Opus | 2, 3 (cut after 4's `asm.rs` lands) | `core/src/ff_build/{ps.rs,ps/tests.rs}`, one `mod` line in `ff_build.rs` | no |
| 6 FF sources, AIR FF records, staleness | `src` | Opus | Sonnet + Opus | 1, 4, 5 | `core/src/shader_cache{.rs,/source.rs,/tests.rs}`, `unix/src/shader_prewarm{.rs,/tests.rs}` | no |
| 7 pair rule, FF AIR builds | `rt` | Opus | Sonnet + Opus | 1, 4, 5, 6 | `unix/src/encoder/compile{.rs,/libraries.rs,/libraries/tests.rs,/tests.rs}`, `unix/src/encoder.rs` (FF arms), `unix/src/draw.rs` if a probe moves, `mtld3d.conf`/`docs/d3d9/STATUS.md` lines that say FF stays MSL | no |
| 8 corpus, parity FF pairs, conformance capture | `pt` | Opus | Sonnet + Opus | 6, 7 | `core/examples/emit_corpus.rs` (+tests), `src/airconv/tests/dxso_parity.rs`, `src/airconv/Cargo.toml` dev-deps only if needed, `tests/d3d9/dxso-corpus/*` | Step 4 (early job) |
| 9 gates, final review | owner | Sonnet (jobs) | Opus final | 1-8 | ledgers, `$S/gates/*` | yes |

Tasks 1, 2, 3 start together after Task 0 (disjoint files). Task 4 starts after 2 and 3 integrate; Task 5 after Task 4's
`asm.rs` (the typed helpers) is integrated (Task 4 lands it as its first commit and the owner integrates that commit
alone, so 4 and 5 then run in parallel). Task 6 after 1, 4, 5; Task 7 after 6; Task 8 after 7. Reviews per the
lighter-task-testing memory: a Sonnet spec lens on every task, an Opus correctness lens on 2-8; Minors get no fix round.

---

### Task 0: base, ledgers, plan commit (owner)

- [ ] **Step 1:** worktrees exist (done at planning: `$S/wt` on `c3c` at b4da41c5 with submodules filled read-only,
      `$S/wt-mn` on `c3c-mn` at 47a8c26); the plan and the three maps in `$S`; the plan committed on `c3c-mn` (`docs:
      the DXMTRS C3c plan`); program ledger line (written at start); step ledger opened with a preflight scan of the
      Execution table's produces/consumes.
- [ ] **Step 2:** base references, host-only: KEY on `$S/wt` = `c298383e…`; DIG in `$S/wt` (target `$S/cargo-int`) →
      `$S/base/msl.txt` (digest + fingerprint header line); PAR at the base = C3b's two lines (synthetic 159/159, 0
      ff-skipped; conformance-x64 147/149 + 2 triaged); C3b's gate figures (C3b ledger lines 60-77) copied into the
      step ledger as Task 9's targets.

**Expected:** key, digest, fingerprint and parity equal to C3b's; plan committed; ledgers opened.

### Task 1: kinds 8/9, explicit arms, gate awks (lane `kinds`, and `$S/wt-mn`)

**Interfaces:** `CachedKind::{AirFfVs = 8, AirFfPs = 9}`; `from_byte` 8/9; `air()` FfVs→AirFfVs, FfPs→AirFfPs;
`msl_twin()` 8→FfVs, 9→FfPs; `is_air()`, `is_vertex()`, `is_pixel()`, `is_programmable()`, `air()`, `msl_twin()` written
as exhaustive `match`es (no wildcard, no `>=`, no `!matches!`), so the next kind is a compile error everywhere;
`entry_name` label `air_ff` (`mtld3d_{vs|ps}_air_ff_{key:08x}`); `compile_bucket` FF; `emit_corpus --kinds` gains
`(AirFfVs, "air_ffvs")`, `(AirFfPs, "air_ffps")` (16 entries). No producer of 8/9 yet (Task 6/7).

- [ ] **Step 1 (RED):** `shader_key::tests::{air_ff_kinds_round_trip_bytes, air_ff_kinds_twin_ff_kinds,
      air_ff_kinds_are_air_and_not_programmable, air_ff_kinds_are_vertex_or_pixel}`; seen failing (8/9 unknown). The
      existing test that asserts 8/9 are unknown (`shader_key/tests.rs:80-81`) is updated in the same commit.
- [ ] **Step 2:** the kinds, the explicit arms, every match site the compiler then names; `shader_cache.rs:21`'s doc
      (`0..=15`); `perf/compilation.rs` and the telemetry tags (`compile.rs:713-719`, `:1987-2004`) treat 8/9 as FF.
- [ ] **Step 3 (`$S/wt-mn`):** both kinds awks (`M dxmt/mtld3d-conformance.sh:95-105`, `M vm/dxmt/d3d9-gate.sh:147-166`)
      match `/^(air_)?(sm[0-9]|ff)(vs|ps)=/` and fail (`no-air-ff-records`) unless the `air_ffvs` and `air_ffps`
      counts are both > 0, besides today's checks; `M dxmt/d3d9-check.sh`'s air run keeps its cache in a scratch
      folder and prints and checks the same kinds line (its comment "fixed-function, so this is a smoke … not proof"
      rewritten). Hand-check each awk on four lines (all-zero, MSL-only, programmable-AIR only, full) in the commit
      message. `sh -n` each script.
- [ ] **Step 4:** CK(`mtld3d-core`, `mtld3d-unix`), LX.

**Expected:** new tests pass, the existing 2201+ core and 381 unix tests pass, DIG = base. Commits: `d3d9(core): the
fixed-function AIR cache kinds 8 and 9, every kind listed explicitly`; MacNeutron `dxmt, vm: the AIR kinds rows count
fixed-function records and need them`.

### Task 2: the DXSO writer and its round trip (lane `write`)

**Interfaces:** C3c-3's `write`/`write_parts`, plus the token helpers the builder needs if an IR type has private
fields (named in the commit). Canonical order: version, dcls, defs (float, int, bool), main instructions, `ret` and
`label sN … ret` per subroutine when any exist, `end`. SM1 relative addressing for `major < 2`, explicit relative
token for SM2+; instruction length bits for SM2+ (`operand_token_count`, `W parser.rs:40-68`), none for SM1; the dst
kind bits split as the decoder reads them.

- [ ] **Step 1 (RED):** `dxso_write::tests::round_trips_hand_programs` (a table in `dxso_write/tests.rs` of hand
      streams covering every SM1/SM2/SM3 operand form the parser keeps: dcls incl. sampler types, def/defi/defb, SM1
      implicit and SM2+ explicit relative addressing, predicates, coissue, texld control bits, subroutines; the parser's
      and lowering's own test programs are private to their modules and cannot be reached), and
      `src/airconv/tests/dxso_write.rs::round_trips_the_parity_corpus` (every record source in
      `tests/d3d9/dxso-corpus/*.bin`, loaded as `dxso_parity.rs` loads them through mtld3d-core, a dev-dependency
      airconv-ir cannot have), comparing `parse(write(P))` with `P` field-wise (`bytecode` ignored, `def` floats by
      bits; a local `fn same(&a, &b)` since `DxsoProgram` has no `PartialEq`), and `…::writes_canonical_streams_back_identically`
      (hand streams without comments: `write(parse(T)) == T`). A writer returning `bytecode()` is forbidden (a review
      check). Files: `src/airconv/ir/src/{lib.rs,dxso_write.rs,dxso_write/tests.rs}`, `src/airconv/tests/dxso_write.rs`;
      tests run with `cargo test -p airconv-ir` and `cargo test -p airconv --test dxso_write`.
- [ ] **Step 2:** the writer.
- [ ] **Step 3:** CK(`airconv-ir`), the airconv tests that exist (`cargo test -p airconv-ir`), LX; KEY prints (moves).

**Expected:** round trips hold for every corpus and parser-test program. Commit: `airconv(ir): a DXSO writer, parse ∘
write is the identity`.

### Task 3: FF-only lowering hooks (lane `hooks`)

**Files:** `src/airconv/ir/src/dxso/lower/{vs.rs,ps.rs,regs.rs}`, `lower.rs` (the bump-env binding decision only),
tests in `lower/tests.rs` and `src/airconv/src/dxso/tests/` (AIR-level). **No edit** to `ir.rs`, `spec.rs`,
`parser.rs`, `link.rs`, `opcode.rs`, `ir/src/dxso.rs` (C3c-3), nor to `src/airconv/src/{dxbc,dxil}` or
`air/signature.rs` beyond added match arms (C3-10's diff rule, checked by the Opus reviewer with `git diff -U0 b4da41c5
-- src/airconv/src/dxbc src/airconv/src/dxil src/airconv/src/air/signature.rs | /usr/bin/grep '^-[^-]'` printing
nothing but reflowed lists).

- [ ] **Step 1 (RED, one per hook):** `vs_window_reads_vs_draw_and_pos_fixup_rows` (a `vs_3_0` reading `c2032`,
      `c2037`, `c2043`, `c2044` lowers to cbuf reads of `VS_DRAW` rows 0/5/11 and `POS_FIXUP` row 0; `c2045` and a
      relative read stay plain FLOAT rows); `rhw_depth_clamp_follows_the_bias` (a program reading `c2044`: the
      epilogue's op order is fog_z, bias, clamp under `pos_fixup.z`; a program without one has no clamp);
      `o31_position_is_the_clip_basis_not_a_varying` (declared `dcl_position o31` + `dcl_position o0` + 2 planes: o0's
      position is unchanged, no extra output, the clip loop reads o31's value; without `o31` the clip-space position as
      today); `ps_type11_source_reads_bump_env_rows` (a `ps_3_0` reading token type 11 index 3 `.x` binds slot 12 and
      reads row 3; a program without one does not bind it); `bem_lowers_in_ps_3_0` (`map-ps.md` G3);
      `vs_inputs_v16_to_v22_are_attributes` (untested today, `map-plumbing.md` §1). Each seen failing (the last two may
      pass at once: they pin untested behaviour, and are recorded as such).
- [ ] **Step 2 (the identity check):** `src/airconv/tests/dxso_identity.rs` (excluded from the key like every
      `tests/`) translates every corpus program with its recorded spec and prints `dxso-identity: <n> programs, digest
      <xxh3 of the concatenated AIR bitcode>`; run in the lane before Step 3's code → `$S/base/air-digest.txt`, and
      after → the same line (C3c-2: no existing program reads `c2032+`, declares `o31` or reads a PS type-11 source,
      so the hooks, including (b)'s clamp, change none). The digest is kept in `$S/base/`, not in the test (Task 8
      replaces `conformance-x64.bin`).
- [ ] **Step 3:** the hooks.
- [ ] **Step 4:** CK(`airconv-ir`, `airconv`), PAR (still 159/159 and 147/149: hook (b) is inert with `pos_fixup.z` 0),
      LX; KEY prints (moves).

**Expected:** six new tests pass, existing airconv tests pass, PAR unchanged. Commit: `airconv(dxso): fixed-function
hooks: the VsDraw/PosFixup window, the RHW depth clamp, a world-space clip basis, bump luminance rows`.

### Task 4: the FF VS builder (lane `ffvs`)

**Files:** `core/src/lib.rs` (`pub mod ff_build;`), `core/src/ff_build.rs` (module doc, `pub fn vertex(key: &FfVsKey)
-> FfProgram`, `pub struct FfProgram { pub tokens: Arc<[u32]>, pub provided_input_mask: u16, pub warnings:
Vec<&'static str> }` (the VS's declared-input mask for its `VsSpec`; the PS returns the same struct with mask 0)),
`ff_build/asm.rs` (`pub mod asm`: typed helpers over `airconv_ir::dxso` IR types and `dxso_write::write_parts`:
registers, swizzles, modifiers, `def` allocation at `c256+`/`c16+`, temp allocation with the 32 cap, output
allocation from `o0` with `o31` reserved; `asm/tests.rs` with one test per helper family; first commit, carrying
`lib.rs`'s line and `ff_build.rs` with `FfProgram`, CK green on its own (public items, so no dead-code lint), integrated
alone for Task 5), `ff_build/vs.rs`, `ff_build/tests.rs` (owns the FF VS and PS key lists both builders test:
`synthetic_ff()`'s shapes restated plus C3c-10's extras).

Builds C3c-7's VS from `map-vs.md` §1 in `ff.rs`'s order: inputs; RHW path (`ff.rs:1104-1226`) or transform (two-step
WV then PROJ, `ff.rs:1269-1289`), blending (`:945-1029`), clip basis `o31` (C3c-2c), eye normal with the adjugate and
its fallback (`:1325-1340`), `posEye`, lighting (directional/point/spot, attenuation with `step(d, range)`, ambient
always, Blinn-Phong with `cmp` selects and `pow`, `_sat` outputs, unlit colours unsaturated, `resolve_mat` sources),
texcoords (TCI modes 0-4 and >4's passthru, `tex_coord_dims` expansion, TT counts 2-4 and PROJECTED, zero for stages ≥
`tex_coord_count`), fog modes 0-4 + RANGE_FOG (`exp2` with `-1.442695`), point size (C3c-7), passthrough outputs (FOG0
skipped under fog mode 4). No `if`/`loop`: everything unrolled per key.

- [ ] **Step 1 (RED):** `ff_build::tests`: `every_synthetic_vs_key_lowers` (all `synthetic_ff` VS keys + C3c-10's extra
      shapes, built in the test from the same constructors `emit_corpus` uses or a shared list in `ff_build/tests.rs`:
      `parse` then `lower::vertex` with the key's spec succeed); `vs_reads_the_rows_the_msl_reads` (per key: the set of
      direct `c#` reads below 256 equals the set of `vs_c[<n>]` indices in `emit_vs_ff_named`'s text, and the palette is
      read only relatively; window reads only where `ff.rs` reads `vs_draw`/`pos_fixup` beyond the epilogue's);
      `vs_declares_the_msl_inputs` (dcl set = `emit_vertex_in`'s attributes); `vs_writes_fog_in_full`;
      `vs_outputs_are_the_passthrough_extras` (the lowered program's extras = `passthrough_outputs` for the key, the
      set a `ps_3_0` links against); `vs_blend_index_is_clamped_and_rounded`; `vs_temporaries_fit`;
      `vs_tokens_round_trip` (`write(parse(T)) == T`); `ff_build_is_deterministic`; `vs_warns_where_ff_rs_warns` (the
      four sites, through `FfProgram.warnings`). The rows-read rule: the builder's direct `c#` set ⊆ the MSL's, equal
      where the read affects an output. Seen failing (no builder).
- [ ] **Step 2:** the builder (`asm.rs` first, its own commit, then `vs.rs`).
- [ ] **Step 3:** CK(`mtld3d-core`), LX, DIG = base (digest and fingerprint).

**Expected:** every VS key lowers; row and input sets equal the MSL's. Commits: `d3d9(core): fixed-function DXSO
assembly helpers`, `d3d9(core): the fixed-function vertex stage as vs_3_0 DXSO`.

### Task 5: the FF PS builder (lane `ffps`)

**Files:** `core/src/ff_build/{ps.rs,ps/tests.rs}`, `pub fn pixel(key: &FfPsKey) -> FfProgram` (one `mod`/`pub use` line
in `ff_build.rs`).

Builds C3c-7's PS from `map-ps.md` §1 in `ff.rs`'s order: the control sets (active stages, bump stages, the two
unbound-fallback rules, `reads_temp` by the emitted text's rule, dead TEMP stages, the sampled walk from the last
stage back, declared = `sampled_stage_mask`, `reads_bump_env`), `current = v0`, `temp = 0`, per stage the sample
(projected with the `w == 0` guard, bump-displaced by `bem` from the previous bump stage, luminance via C3c-2d,
chained bumps), colour and alpha per `map-ps.md` §1e/§1f (every op incl. the default arm, the modifiers'
replicate-then-complement order, DOT3 writing all four channels, Kept/FromColor/fallback alphas, the write order reading
old values first), specular add, `mov oC0, current`.

- [ ] **Step 1 (RED):** `every_synthetic_ps_key_lowers` (all `synthetic_ff` PS keys × their variants + C3c-10's extra
      shapes: `lower::pixel` succeeds); `ps_reads_the_rows_the_msl_reads` (direct `c#` < 16 = `ps_c[<n>]` set in
      `emit_ps_ff_named`'s text); `ps_declares_the_msl_textures` (dcl'd samplers = `sampled_stage_mask`);
      `ps_samples_what_the_msl_samples` (texld targets = the `t{i}` the MSL text names); `ps_binds_bump_env_when_the_msl_does`
      (lowered `PsBindings.bump_env` == `reads_bump_env`); `ps_tokens_round_trip`; `ps_temporaries_fit`;
      `ps_warns_where_ff_rs_warns` (the three sites: unhandled argument, modifier, op). Seen failing.
- [ ] **Step 2:** the builder.
- [ ] **Step 3:** CK(`mtld3d-core`), LX, DIG = base.

**Expected:** every PS key lowers; reads and bindings equal the MSL's. Commit: `d3d9(core): the fixed-function pixel
stage as ps_3_0 DXSO`.

### Task 6: FF sources, AIR FF records, staleness (lane `src`)

**Interfaces:** C3c-4's `ShaderSource` arms and codec; `CacheEntry::new_air` for kinds 8/9 with an FF source;
`decode_body`/`decode_air` admitting an FF source on 0/1/8/9 (kind-checked: a VS source on a PS kind is refused;
`disk_key()` must equal the record key); `ShaderSource::emit` dispatching FF to `ff.rs`; `refresh_msl` regenerating a
0/1 record that has a source; prewarm's C3c-5 compare (`ff_build::{vertex,pixel}(key).tokens != stored` → regenerate).

- [ ] **Step 1 (RED):** `shader_cache::tests::{an_ff_vs_source_round_trips, an_ff_ps_source_round_trips,
      an_air_ff_record_round_trips, an_ff_source_on_the_wrong_stage_kind_is_refused, a_truncated_ff_source_is_refused,
      a_runtime_msl_ff_record_stays_sourceless, a_corpus_msl_ff_record_with_a_source_loads, a_hostile_ff_key_is_refused,
      a_warned_ff_key_round_trips (material source 3, TCI mode 6)}`;
      `shader_prewarm::tests::{stale_ff_tokens_regenerate_the_air_record, matching_ff_tokens_keep_the_record}`. Seen
      failing.
- [ ] **Step 2:** the code.
- [ ] **Step 3:** CK(`mtld3d-core`, `mtld3d-unix`), LX, DIG = base.

**Expected:** tests pass; MSL FF record bytes unchanged (the stays-sourceless test compares bytes with a C3b-encoded
fixture). Commit: `d3d9: fixed-function sources and the AIR fixed-function records`.

### Task 7: the pair rule and FF AIR builds (lane `rt`)

**Interfaces:** C3c-6. `LibraryInput::FixedFunctionVs { key, backend, program: Option<Arc<FfBuilt>> }` /
`FixedFunctionPs { key, variant, backend, program }` (`FfBuilt` = tokens + parsed program + mask), `backend()` and
`source()` honouring them; `begin_vs_library`/PS kinds 8/9 under AIR; `build_air_library` for FF (translate the
memoized tokens with the FF spec; persist `CacheEntry::new_air` with the FF source when a key exists); install's AIR
failure → `fail(AirStage::Ff*)`; `pipeline_identity`'s `air_programs: Option<(AirStage, AirStage)>`; `recipe_persists`
for AIR FF recipes as for programmable AIR ones; `PairRule` FF memo; FF index arrays per backend; `ff_count` sums both;
`forget_failures`/`clear` loop over both; the docs and `mtld3d.conf` lines that say FF stays MSL under `air`.

- [ ] **Step 1 (RED):** `libraries::tests::{an_ff_pair_is_air_when_both_lower, an_ff_vs_with_a_refusing_ps_is_msl_both,
      a_failed_ff_vs_demotes_its_pairs, an_ff_ps_failure_is_keyed_by_ff_key, ff_indexes_keep_backends_apart,
      msl_backend_builds_no_ff_program}`; `compile::tests::{an_air_ff_library_persists_with_its_source,
      an_msl_ff_library_under_air_persists_sourceless, a_keyless_air_ff_build_persists_no_record,
      an_air_ff_pipeline_failure_demotes_both_stages, an_air_ff_recipe_names_kinds_8_and_9,
      an_ff_by_programmable_air_recipe_is_valid}`; `shader_prewarm::tests::an_air_ff_recipe_replays_from_air_records`.
      Seen failing.
- [ ] **Step 2:** the code.
- [ ] **Step 3:** CK(`mtld3d-unix`, `mtld3d-core`), LX, DIG = base.

**Expected:** tests pass; the existing 381 unix tests pass. Commit: `d3d9(unix): fixed-function stages pair as AIR
under shader.backend=air`.

### Task 8: corpus, parity FF pairs, conformance capture (lane `pt`)

- [ ] **Step 1 (RED):** the parity harness requires a positive FF-pair count per file and has no `ff-skipped` counter
      (a recipe naming a sourceless record fails the file): run PAR at the lane's start → fails (`synthetic.bin: 0
      ff pairs`), seen.
- [ ] **Step 2:** `emit_corpus --write-cache` writes `synthetic_ff()` + `synthetic_ff_extra()` (C3c-10) as MSL FF
      records with sources plus recipes (vertex attrs from per-key `D3DVERTEXELEMENT9` lists; cross-family pairs);
      `Shader::record` keeps FF records with a source; `--merge` drops sourceless records and the recipes naming them
      and prints the count (RED: `merge_drops_sourceless_records_and_their_recipes`; and a pinning test that 8/9 and
      10-15 records with sources survive a merge). Example tests run with `--features disk-cache` (C3b fix1's
      pitfall). The parity harness: FF records' MSL via `ShaderSource::emit`,
      AIR via `translate(tokens, stage_spec)`; C3c-8's FF uniforms for recipes with an FF stage; the line gains `<n>
      ff`; `ff-skipped` removed. Regenerate `synthetic.bin` (README's procedure); PAR: `synthetic.bin` all FF pairs
      equal or triaged with C3c-7/map reasons (`map-vs.md` §3, `map-ps.md` §3) in `triaged.txt`; every differ
      root-caused (a builder bug is fixed in this lane with a builder test; only a numeric reason from the maps' tables
      is triaged).
- [ ] **Step 3 (mutation REDs):** one `ff_build` mutation per hook and per feature family (e.g. drop the window's
      half-pixel subtraction, swap a BLEND's factors, skip the `w == 0` guard, drop the clip basis): PAR shows a differ
      for each; reverted.
- [ ] **Step 4 (lock, the early job `$S/gates/early.sh`, rewritten from C3b's `early.sh` per C3c-11):** usage checked
      first; main detached at `c3c-mn` (Task 1's awks), `$D` at the `c3c` tip, `make wine-arm64` (CDHash; the key
      literal grep reads the key from `$B/wine-arm64-src/dxmt-build/src/dxmt_translator_key.h`, never a literal), then
      `run conf-air env D3D9_BACKEND=air sh dxmt/mtld3d-conformance.sh` (the script itself keeps the caches in
      `$B/mtld3d-conformance/air-caches` under `air`, `M dxmt/mtld3d-conformance.sh:65-69`; no second
      `--keep-shader-cache`, which the runner would let override it) and its verdict and kinds lines (`air_ffvs`,
      `air_ffps` > 0); the arm64x leg's caches copied to `$S/capture/`; `$D` restored, main `## main`. Then host-side
      `emit_corpus --merge` of the leg's files into `conformance-x64.bin` (the name kept; README says the leg),
      its dropped-sourceless count and kinds line (FF > 0), PAR on it: FF pairs equal or triaged as Step 2; the
      first-verdict rule (C3c-11) applied; README's table updated (origin, date, the `air` lane).
- [ ] **Step 5:** CK(`mtld3d-core`, `airconv`), DIG = base, privacy check of the new `.bin` payloads (tokens, keys,
      recipes and metallibs only: `strings` of the decoded payloads shows no path, user or host name).

**Expected:** `dxso-parity: synthetic.bin: <n>/<n> equal … <f> ff …` and `… conformance-x64.bin: …` with FF pairs, every
differ triaged with a reason, pos-ulps reported. Commits: `d3d9(core): fixed-function pairs in the DXSO corpus`,
`airconv: fixed-function pairs in the DXSO parity test`, `tests(d3d9): the conformance corpus recaptured under air`.

### Task 9: gates, final review (owner)

- [ ] **Step 1:** every lane integrated (`git cherry` empty), RL; KEY prints the new key (recorded; `$S/key.sh`'s
      literal edited to it); `sh $B/c3/xlate.sh $S/wt c3c` → `xlate: c3c ok (13 corpora), key <new>` with no `air-only`
      line; PAR (Task 8's lines); DIG = base; fingerprint = base.
- [ ] **Step 2 (lock, Mac job `$S/gates/mac.sh` from C3b's, main detached at `c3c-mn`, `$D` at the `c3c` tip):** C3b's
      Task 10 Step 2 rows: `make wine-arm64` (CDHash; `mtld3d.so` exports 6 + 14, imports + libz + libxml2, size, key
      literal = the new key, codesign); `dxmt/check.sh` x64 and ARM64EC (`dxmt-check: all passed`);
      `wine-arm64/check.sh` (exit 0; atomic64typed flake rule); `presenter/check.sh` (66, exit 0; pacing flake rule);
      conformance MSL and air (verdict line; air kinds line with `air_ffvs`/`air_ffps` > 0; visual failed counts vs
      C3b's figures: arm64x visual MSL 43, air 30); `d3d9-check.sh` MSL and air on wine.app (air: the kinds row with FF AIR records); the
      seeded prewarm run (`(<n> AIR)`, n ≥ C3b's 118 since FF now counts); loopback build + `d3d9-check` MSL and air;
      `mtld3d-facade-test.sh` (`test result: ok`); `wm-probe-check.sh` (11 ok).
- [ ] **Step 3 (lock, VM job `$S/gates/vm.sh`, env-vm.sh, signing, `BUILDER_MAC`):** the rebuild order (Global
      Constraints), then C3b's vm4 rows: `wm-probe --native` (11), dxmt-native (33), loopback (33), dxmt (47), d3d (43),
      `d3d9-gate.sh` old/new WoW64 × msl/air (four `d3d9 gate: ok`, `(air)` on two, each `ok   key guest=host`; air:
      `ok   air backend logged`, `ok   air records x64/x86` with FF > 0 on both legs), present ×2, d3d-smoke old/new.
      Flake rule: one rerun of the known timing rows.
- [ ] **Step 4:** final Opus review of `b4da41c5..c3c` and `47a8c26..c3c-mn` given the code, this plan and the gate
      figures; one fix wave (Critical/Important, RED→GREEN, the task's CK/LX, then RL, KEY, PAR); scoped Opus
      re-review; reruns as C3b's Task 10 Step 5 (a shipped-code fix reruns Steps 2-3 in full; a script fix its rows;
      a `src/d3d9/mtl`-only fix the loopback/facade rows and the VM job, C3b R11).
- [ ] **Step 5:** restore `$D`; main `## main`; merged lanes' worktrees removed (branches kept) and their cargo dirs
      deleted; ledgers: program "C3c GATED, UNPUSHED …", step ledger tips, figures, rulings, minors; memory's DXMTRS
      line. STOP.

**Expected:** every row at C3b's figure or better; the AIR lanes show FF AIR records; DXMTRS `c3c` and MacNeutron
`c3c-mn` gated and unpushed.

---

## Push order (the parent's; nothing here runs them)

1. `$D` at `$(cat $B/wine-arm64-src/dxmt.applied)` (lock).
2. Privacy scan of `git -C $S/wt log -p b4da41c5..c3c` and the `.bin` payloads; `git -C $D push --dry-run origin
   c3c:main`; push DXMTRS `c3c` to `main` (fast-forward from b4da41c5).
3. On `c3c-mn`: `dxmt/pins` `DXMT_COMMIT=<pushed sha>`, spec §5's C3 entry "C3c done …" (as 47a8c26), commit.
4. The pushed pin's development build (lock): `make wine-arm64`, `sh dxmt/check.sh` x64 → `dxmt-check: all passed`;
   `$D` restored.
5. Main `## main`, `git -C $M merge --ff-only c3c-mn`, privacy scan of `47a8c26..main`, push `main`. No tag, no
   release (C3c-12).

## Open risks

- **Lowering reach (C3c-2):** the hooks assume no valid program names `c2032+`, `o31` or a PS type-11 source; Task 3's
  identity digest over the corpus proves no corpus program changes; Wine's tests (conformance AIR lane) are the second
  check.
- **Numeric parity (C3c-7):** ULP-level differences at lighting, fog and projected divides; triage reasons are written
  ahead in the maps; an EQUAL/NOTEQUAL alpha test on a computed alpha can flip a discard (triaged by site).
- **Mixed backends across passes (R9):** one `FfVsKey` drawn as MSL in one pair (the PS refused) and AIR in another
  can z-fight under EQUAL depth; the cost of C3b-2's rule, now limited to refusing programmable PSs.
- **Conformance capture:** AIR records in the corpus need the harness to treat kinds 10-15 like 2-7 (Task 8 proves
  it); ~900 recipes lengthen PAR.
- **Software vertex processing (C3c-2):** D3D9's SWVP allows `c0..c8191`; DXMT forces hardware VP, and a shader naming
  `c2032..c2044` would read VsDraw instead of an out-of-bounds row (garbage either way).
- **Prefilter cost:** one build+parse+lower per new FF key on the encoder thread under `air`; unmeasured (the parked
  benches).

## Review record (2026-11-09)

Three adversarial Opus reviews against b4da41c5/47a8c26: `build/c3c/review-1-translator.md` (0 Critical, 7 Important,
10 Minor), `review-2-runtime.md` (1 Critical, 4 Important, 10 Minor), `review-3-execution.md` (1 Critical, 7
Important, 21 Minor). Every Critical and Important is folded; none was ruled against:
- R1 I1/I2 (`o16` aliases the position mirror; output allocation): the clip basis is `o31` with a private mirror,
  outputs from `o0`, a test that `o0`'s position is unchanged (C3c-2c, C3c-7, Task 3). I3 (blend index clamp and
  rounding): spelled out (C3c-7, Task 4 test). I4 (RHW row 0 is the logical viewport): C3c-8. I5 (`bump_env` rows):
  16 distinct rows (C3c-8). I6 (private test programs): a hand table + the corpus (Task 2). I7 (identity check void
  after an unconditional clamp): the clamp only in programs reading `c2044`, the digest in `$S/base/` (C3c-2b, Task 3).
- R2 C1 (`pipeline_shader_refs` stamps FF 0/1): `ff_kind(is_pixel, backend)` for both sites + recipe tests (C3c-6,
  Task 7). I1 (no FF key codec): a validating codec (C3c-4, Task 6). I2 (demoted FF stages under `air` persisting a
  source): `source()` only for `Air` + a test (C3c-4, Task 7). I3 (passthrough outputs untested): a Task 4 test and a
  cross-family recipe. I4 (sourceless MSL FF records in the air capture): `--merge` drops them with a count (C3c-10,
  Task 8).
- R3 C1 (capture command and template): the script's own kept caches, main detached at `c3c-mn`, the key read from
  the build (Task 8 Step 4). I1: as R2 I4. I2 (key lists in an example): owned by `ff_build/tests.rs`, duplicated in
  `emit_corpus` (Rules check). I3 (`asm.rs` alone): `pub mod asm` with its tests and `ff_build.rs` in the first
  commit (Task 4). I4: as R1 I6. I5 (`d3d9-check.sh` kept cache): EXEs copied into `$WORK/exe/` (C3c-11). I6 (copied
  templates still `c3b` and `VAR=x run`): rewritten + a grep guard (C3c-11). I7 (first FF AIR verdict): the rule and
  Task 9's target (C3c-11).

Minors folded where they changed a plan line (four hooks; type 11 not 6; three PS warning sites as returned warnings;
fog wording; re-derived predicates; FF sampling recipes; `def` count; disk keys reused; mask stored in the source;
`fail` in the memo; prewarm placement and determinism; parity `ff-stale`; `--features disk-cache`; key read from the
build; `vm/dxmt-native/build.sh`; leg naming; SWVP risk). **Deferred minors** (ledgered for the implementers or C3d):
per-draw FF key hashing in `pair_backend` under `air` (unmeasured, the parked benches); RHW half-pixel bit-exactness
(triage reason written ahead); the widened awk on `--only` subsets (info row); disk housekeeping beyond the lanes'
cargo dirs; usage checked before each lock job (in Task 8 Step 4 and Task 9); the spec §5 edit named once (Push order).

