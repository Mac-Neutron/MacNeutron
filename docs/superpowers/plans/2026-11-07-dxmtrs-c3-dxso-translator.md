# DXMTRS C3: D3D9's shaders through the Rust translator (SM1-3 front end) — Implementation Plan

> **For agentic workers:** execution is **subagent-driven** (`superpowers:subagent-driven-development`), as the DXMTRS
> ledger's per-step loop runs it: one implementer per task, Sonnet for mechanical tasks and Opus for the translator's
> lowering (Tasks 4-6, and Task 8's parity fixes), each task reviewed as the Execution table says. The maintainer's
> general preference for inline execution gives way to the ledger's loop here; an inline run takes the tasks in table
> order, one lane at a time. Steps use checkbox (`- [ ]`) syntax for tracking.

**Go-ahead.** The maintainer gave the go for C3 in chat on 2026-11-07: "Start C3". Binding fork B is the ledger's
default ruling (2026-11-07 assessment, standing authorization), under that go, not a separate maintainer choice.
Task 0 Step 1 writes both into the program ledger.

**Goal:** D3D9's programmable and fixed-function shaders become Metal AIR made by DXMTRS's translator (`src/airconv`)
through a DXSO (SM1-3) front end, on the Mac and in the VM, behind a switch until parity, after which the DXSO MSL
emitters and `mtld3d_shaders.bin`'s MSL records retire (spec §5 C3, §7 "One translator", §8 "At C3").

**Split.** C3 is four plans' worth (the 2026-11-07 assessment guessed 40-55 agent-days). This plan details **C3a** and
outlines C3b-C3d; each later part gets its plan rebased on what landed (spec §5's pace).
- **C3a, the translator side** (this plan, Tasks 0-10): the parser, IR, link data and specialization types move into
  `airconv-ir`; the `Shader::Dxso` arm, one new compilation argument and the lowering of SM1-3 programmable shaders;
  AIR's `point_size`/`point_coord`; a corpus rendered both ways on the Mac; the VM's transport carries the new argument.
  D3D9's runtime is unchanged; DXBC/DXIL output byte-identical; D3D10-12 still refuse non-DXBC input.
- **C3b, the runtime switch** (outline below). **C3c**, fixed function through the same builder (outline). **C3d**,
  parity, default flip and retirement (outline).

**Architecture (C3a).** `airconv-ir::dxso` owns DXSO: parse, IR, semantic linkage and the specialization types
(`VariantKey`, `VsSamplerKinds`, `VsSpec`, `StageSpec`) with their wire form, used by `mtld3d-core` (re-exported),
`mtld3d-mtl` (the guest's facade) and the translator. `airconv-ir::dxso::lower` turns a `DxsoProgram` plus its
specialization into airconv-ir's existing instruction IR, expanding D3D9-only semantics there; airconv's new `dxso`
module lowers that IR with the DXBC converter through a D3D9 `BindingMap` whose top-level buffers, textures and
samplers sit at the slots, and under the conditions, D3D9's MSL uses today (binding fork B). `SM50Initialize`
recognises DXSO by its version token, so the translator's C interface, winemetal's slots 74-88 and the VM's forwarding
carry D3D9 unchanged, plus one argument type, `SM50_SHADER_D3D9`.

**Tech Stack:** Rust 1.99.0 (workspace lints, `docs/CONVENTIONS.md`), LLVM 23 `air-23` (airconv), Metal (host parity
test), C (MacNeutron `vm/metal/wm`), sh.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` §5 (C3 bullet; 4b's P16 release hold; the binding-fork
note), §6 (the key), §7, §8, §9. **Ledger:** `.superpowers/sdd/dxmtrs-program.md` (2026-11-07 assessment). **Pipeline
rules:** `build/pipeline.md` rules 1-9 (written before two renames: its `eac-vm/` is today's `vm/`, and rule 9's
`## dxmtrs` is today's `## main`, the only MacNeutron branch). **Map (evidence for every ruling):** `build/c3/map.md`.
**Reviews:** three adversarial Opus reviews (spec §5 asks three for §7's phases): one at planning, then
`build/c3/review-2-translator.md` and `build/c3/review-3-execution.md`, every Critical and Important folded in
2026-11-07 (Review record at the end).

**Bases.** DXMTRS `main` 5d240384 (opt2), MacNeutron `main` 4c99f4f (pins it). Rebase before running if either moved.

---

## Rulings

Standing authorization (ledger, 2026-10-29) plus the maintainer's go for C3 (2026-11-07, "Start C3"): each default
below is self-approved after the three reviews. A later fact that breaks one goes to the ledger and, when it changes a
plan, to the maintainer.

- **Ruling C3-1 (front end):** the DXSO parser (`parser.rs`), IR (`ir.rs`), opcode table (`opcode.rs`), the data half
  of linkage (`Semantic`, `SemanticSet`, `LinkInputs`, `MAX_LINKED_INPUTS`, `decl_passthrough_code`) and the
  specialization types move into `airconv-ir::dxso`; `mtld3d-core::dxso` re-exports them, and the MSL emitters and
  link's MSL halves (`VsOutputs`, `PsInputs`, prologue/epilogue writers) stay in `mtld3d-core` until C3d. The
  translator runs on the VM host, where no mtld3d crate is linked, and one parser must serve both sides. Cost if
  wrong: a reverse move of ~1.8k lines, no runtime effect.
- **Ruling C3-2 (lowering target):** DXSO lowers into airconv-ir's DXBC instruction IR and reuses the DXBC LLVM
  lowering. D3D9-only semantics are expansions in `lower`: `lit`, `dst`, `nrm`, `crs`, `sincos`, `pow`,
  `exp/log/expp/logp`, the `m4x4` family, `cnd`/`cmp`, predicates (a predicated `texld` is an unconditional sample plus
  a select, as `emit.rs:3036-3040`, never a branch around an implicit-derivative sample), `loop aL`/`rep`, `if b#`,
  `texkill` (a block split: `PixelDiscard` takes no operand, `ir.rs:819`), ps_1_x texture ops, modifiers and
  source/dest scaling; relative constant reads `c[a0/aL + N]` (with `def`s present: index clamped to 0-255, then the
  `def` overlay as an `IntegerCompare` + `MovConditional` chain, as `emit.rs:481-517` and `:2795-2808`; without `def`s
  the raw index, as the MSL); ps_1_x's [-1, 1] clamp on plain `c#` reads (`emit.rs:2820-2826`); `call`/`callnz`/`label`
  through a DXSO inliner in `lower` (blocks copied per call site, a conditional branch around `callnz b#`/`callnz p0`,
  the depth limit kept; `cfg.rs`'s private inliner untouched, DXBC still refuses `callc`, `cfg.rs:39`). Constructs the
  IR cannot express get new IR variants whose DXBC path is untouched: `SampleCompareLevel { lod }` and
  `SampleCompareGrad` for depth compares at an explicit level (`texldl`, `LOD_BIAS`'s `pinned_depth_level`,
  `emit.rs:3330-3340`) and with gradients (`texldd`, `emit.rs:3245-3249, 3296-3306`), each with its own converter
  function; `InstSampleCompare` (which the DXBC decoder builds, `ir.rs:297-308`) gains no field. Cost if wrong: a
  parallel LLVM lowering for DXSO (~3-5k lines).
- **Ruling C3-3 (no new winemetal slot):** translation runs where winemetal's translator runs (the Mac's
  `winemetal.so`, the VM host's for the guest) through the existing slots: `SM50Initialize` (74) sniffs token 0 before
  `Container::parse` (`src/airconv/src/entry/initialize.rs:69`): only when `len >= 4` and `len % 4 == 0`, reading
  tokens with `u32::from_le_bytes` (the host's bytecode comes from a record with no alignment promise,
  `vm/metal/wm/slots.c:466-473`), high half `0xFFFE` VS or `0xFFFF` PS. No DXBC/DXIL input can match: the container
  starts with `DXBC` (0x43425844 little-endian), and `DXILGetEntry` takes a handle, not bytes. `SM50Compile` (76) takes
  `SM50_SHADER_D3D9 = 10` (free today, `airconv_public.h:209-218`; an upstream DXMT that assigns 10 later is a merge
  conflict, ledgered: `slots.c` indexes `arg_types[]` by type), and bitcode returns through 77 as for DXBC. Because the
  sniff makes `SM50Initialize` accept SM1-3 tokens, `dxmt_core::shader::initialize` (`src/dxmt/src/shader.rs:81-105`,
  D3D10-12's one entry: `d3d11/src/shader.rs:121`, `d3d12/src/shader_cache.rs:235`) refuses input whose first four
  bytes are not `DXBC` before calling it, so `CreateVertexShader` on a `vs_2_0` stream still fails at creation. The VM
  already forwards 74-88 with flattened argument chains and two-step bitcode (`vm/metal/wm/calls.c:232-246`,
  `slots.c`). Cost if wrong: a dedicated slot later, ~150 lines across winemetal, `g10.rs` and the transport.
- **Ruling C3-4 (the Mac reaches the translator by linking it, in C3b):** the Mac `mtld3d.so` links the `airconv` rlib
  under `cfg(not(mtl_facade))` and calls it in-process; it does not dlopen `winemetal.so`, whose constructor loads the
  MetalFX presenter whenever `MACNEUTRON_PRESENT=1` (`src/winemetal/unix/src/init.rs:40-65`), which the launcher sets
  for every game (`crates/macneutron-core/src/launcher.rs:258-260`): spec §5 C3 forbids loading the presenter into
  D3D9 processes. Cost if wrong: about +38 MB in `aarch64-unix` until C5 retires `mtld3d.so`; the alternative (a
  constructor guard keyed on mtld3d loading winemetal) is recorded in the ledger, not built.
- **Ruling C3-5 (VM data path):** the guest gets the metallib back (slot 77) and sends it again for
  `MTLDevice_newLibrary` (slots 114 + 25): two copies of tens of KiB per first compile. Size is no risk: the 256 KiB
  `MN_REC_MAX` caps only a record's inline payload; bulk data goes through the thread's scratch blob
  (`vm/metal/wm/marshal.c:105-128, 158`) and slot 77's second step allows 64 MiB (`slots.c:267`). Cost if wrong:
  first-compile latency in the VM; the upgrade is a host-side "compile to library" slot.
- **Ruling C3-6 (Metal version and math):** D3D9's AIR targets `SM50_SHADER_METAL_310` (AIR 2.6), as D3D10-12; the MSL
  path's 2.4 pin was for Intel Macs, and MacNeutron ships `aarch64` only. D3D9 passes common flags 0 (no
  `SAMPLE_NAN_TO_ZERO`, no `DEFUSE_FMA`, `airconv_public.h:249-252`); fast-math flags are the DXBC lowering's
  (`converter.rs:400-415`: the VS gets only `afn`+`nsz`, the PS contraction too) against the MSL's Fast mode
  (`unix/src/metal/shader.rs:32-59`, `mad` as `fma`, `emit.rs:1937`), so Task 8 expects ULP differences on `rcp`,
  `rsq` and `log` of 0 (`emit.rs:1961, 2007`), triaged with that reason; `[[position, invariant]]` maps to
  `air.invariant` (`src/air/signature.rs:483`). Cost if wrong: none on shipped hardware; numerical drift shows in Task
  8's parity, which is the check.
- **Ruling C3-7 (binding fork B, from the ledger):** AIR binds exactly where the MSL does: VS `c#` 30, `i#` 29, PosFixup
  28, VsDraw 27, `b#` 26, vertex LOD 25; PS `c#` 15, alpha ref 14, fog 13, bump env 12, `i#` 11, `b#` 10, LOD bias 9,
  PsDraw 8; textures and samplers at the D3D sampler index; vertex inputs `[[stage_in]]` attribute N = `vN`, absent when
  `provided_input_mask` lacks it (reads 0). Each argument is declared and read only under the MSL emitter's condition
  for it (`emit.rs:1244-1290` for the PS: alpha ref with an active alpha test, fog with fog blend, bump env with bump
  ops, LOD bias under `LOD_BIAS`, PsDraw with a scaled `vPos`, `i#`/`b#` for dynamic constants only; the VS's
  likewise), because `draw.rs` binds them under the same conditions (`shared/src/mtl.rs:603-686`): without `LOD_BIAS`
  the sampler bias the converter reads on every sample (`converter.rs:983-999`) is a constant 0. `draw.rs` is
  untouched. Cost if wrong: C4's binding glue, ~500 lines (ledger).
- **Ruling C3-8 (varyings, per-pair backend):** both stages of a pipeline come from one backend; AIR names each varying
  by its D3D9 semantic, `d3d9_<code>` (`code` = link.rs's semantic byte, base members fixed as in MSL), passed as the
  `user` of the existing `FunctionOutput::Vertex { user }` and `FunctionInput::FragmentStageIn { user }`
  (`ir/src/binding.rs:249, 277`; `air/signature.rs:335-345` emits `user(<name>)`; Metal links DXBC's `reg<N>_<c>` the
  same way; `Interpolation::Flat` covers `FLAT_SHADE`), so an AIR VS never meets an MSL PS. Cost if wrong (mixing
  wanted later): matching Metal's generated MSL varying names.
- **Ruling C3-9 (key and release):** 4b has not reached players: the last tags are v1.0.0/v1.0.0p (2026-10-27), 4b
  landed 2026-11-01, 130 MacNeutron commits since. Every C3 commit under `src/airconv` moves the key (the key hashes the
  whole tree, `wine-arm64/lib.sh:199-250`); that costs players nothing while unreleased. Pushing DXMTRS and pinning it
  are not releasing. 4b's P16 hold extends: no MacNeutron release until C3d lands unless the maintainer asks. This
  goes past the ledger's open maintainer item "release hold to C3 vs ending at 4b's pin (4b plan P16/P17)"
  (2026-10-31), which stays open; holding to C3d may mean weeks without a release. Cost of an earlier release: one more
  cache turnover for D3D10-12, and for D3D9 if C3b's records are on.
- **Ruling C3-10 (DXBC/DXIL identity):** through all of C3, DXBC and DXIL output stays byte-identical: `corpus-diff.py`
  (dis=, refl=, tags=, air=) with no `air-only` line (corpus-diff only prints it, so `xlate.sh` fails on it), on every
  in-repo corpus the tools already know (4b's table, `dxmt/tools/air-oracle.sh:95-106`): `dxmt/tests/shaders`,
  `dxmt/tests/dxil`, `dxmt/tests/dxil/reject`, `dxmt/tests/dxbc` (also `--metal 320` and `--metal 320
  --shader-flags 3`), `dxmt/tests/dxbc-corpus` and `dxmt/tests/shaders/d3d11` (both with the same two variants), and
  `src/airconv/ir/tests/data` (its top level only: `dxil-translate.mm:1394` does not recurse). No DXBC file is known to
  use `sample_c`/`sample_d`/`sample_b`/`gather`/`call`, the converter functions beside C3-2's new variants, so Tasks
  3-6 also keep a **diff rule**: in `src/airconv/src/{dxbc,dxil}/` and `src/airconv/src/air/signature.rs` they add
  match arms and functions only, checked by `git -C <wt> diff -U0 5d240384 -- src/airconv/src/dxbc
  src/airconv/src/dxil src/airconv/src/air/signature.rs | /usr/bin/grep '^-[^-]'` printing nothing but reflowed match
  lists (read by the task's Opus reviewer). A regression gate, not a key preserver (C3-9). SMITE 2's capture and
  3DMark's recording are not read: step 4's read grant was step 4's only, and the data was deleted 2026-11-07; the
  lighter-task-testing memory keeps "the SMITE 2 corpus check on translator changes", so its re-capture (a game run,
  which needs the maintainer's go-ahead) is parked as an open maintainer item (Task 10 Step 6). Cost if wrong: a
  DXBC/DXIL drift outside those corpora reaches the milestone benches.
- **Ruling C3-11 (D3D9 cache keying, C3b):** AIR records go into `mtld3d_shaders.bin` as new `CachedKind` bytes 8-15
  (the AIR twins of 0-7); their body is the translator key (32 bytes), the DXSO source and specialization, and the
  metallib. A key other than the build's regenerates the record from its source at prewarm, as `refresh_msl` does.
  No `CACHE_FORMAT_VERSION` or schema bump (unknown kinds are skipped, `shader_cache.rs:1066-1068`). DXMT's own cache
  waits for C4 (no d3d9 crate depends on `dxmt-core`). The key reaches mtld3d's `build.rs` as d3d11's does
  (`DXMT_TRANSLATOR_KEY_H`, `src/d3d11/build.rs:31-41`), from `wine-arm64/build.sh` and `vm/dxmt/build.sh`. The host's
  and the guest's stages compute the key from the same `$D`, so they differ only after a partial rebuild, which the
  lock forbids. Cost if wrong: one D3D9 cache wipe later; after a partial rebuild, guest AIR records valid but stale.
- **Ruling C3-12 (the switch):** `shader.backend = msl | air` in `mtld3d.conf` (`core/src/config.rs` beside
  `shader.asyncCompile`). C3a: no runtime switch. End of C3b: default `msl`. End of C3 (C3d): default `air`, with `msl`
  kept as the escape until the D3D9 milestone benches pass; the MSL emitters' and MSL records' deletion is C3d's last
  task and waits for those benches (outward, parked). Players' D3D9 caches go cold once, at the flip. Cost if wrong
  (`air` earlier): exposure before the corpus and conformance prove parity.
- **Ruling C3-13 (fallback, C3b):** a program's AIR support is decided once, at first sight, by `SM50Initialize`
  (synchronous, on the encoder); a refused program, a fixed-function stage (until C3c) or a variant whose
  `SM50Compile` fails sends the whole pair to MSL (the failed build re-queued through the lost-build path,
  `compile.rs:1517`). Cost if wrong: a variant-only refusal costs one extra build; and one programmable VS paired with
  the fixed-function PS (a depth-only pass) and with a programmable PS (the colour pass) is built by both compilers,
  whose VS math differs (C3-6), so multi-pass `EQUAL`/`LESSEQUAL` depth tests can z-fight (`preserveInvariance` holds
  only within MSL). C3b's plan picks, before its `air` lane is used for games, (a) a sticky per-VS backend (the first
  pair decides) or (b) AIR varyings named as Metal's MSL front end names them (C3-8's cost route); Task 8 measures the
  gap (`pos-ulps`).
- **Ruling C3-14 (corpus):** three sources, all D3D9's own or Wine's tests, none a game's: `emit_corpus`'s synthetic
  programmable set written as a cache file (`--write-cache`), and the caches of a Wine d3d9 conformance run (x64 and
  i686 legs) captured with a new runner flag (`--keep-shader-cache <dir>`; the runner otherwise sets
  `shaderCache.enable=false`, `run.rs:803-828`), merged per leg by a new `emit_corpus --merge`. Cache files carry DXSO,
  the exact specialization and the pipeline recipes that pair them. Committed as `tests/d3d9/dxso-corpus/*.bin` with a
  README naming each file's origin and licence (the conformance files hold Wine's d3d9 test shaders, LGPL-2.1, inside
  `tests/d3d9`, whose LICENSE is Zlib; the README's licence list covers the folder) and how to regenerate them (a
  `SHADER_CACHE_SCHEMA_VERSION` bump makes `load` drop them). Game caches only locally, with the maintainer's
  go-ahead (outward). The e2e programs are C3b's (C3-17). Cost if wrong: ops only games use stay uncovered until the
  benches.
- **Ruling C3-15 (rendered both ways):** on the Mac through Metal, no Wine: per recipe, the MSL pair (today's emitters,
  MSL 2.4, Fast, invariance) and the AIR pair (`airconv::dxso::translate`, `newLibraryWithData`), same vertex descriptor
  and formats, deterministic inputs (attribute, constant, texture patterns, zero divisors), 64x64 target. Equal: every
  channel within 2/255 on 8-bit formats, within 1e-3 relative (1e-5 absolute floor) on float formats, NaN/Inf classes
  equal, and the two pipelines' active buffer, texture and sampler indices equal (C3-7's conditions). A difference is a
  bug unless `tests/d3d9/dxso-corpus/triaged.txt` gives its reason. A pair with a fixed-function stage is
  `ff-skipped` until C3c. Cost if wrong (too loose): a visible drift passes; mitigated by Wine's visual tests in C3b's
  AIR conformance lane.
- **Ruling C3-16 (where the parity test lives):** `src/airconv/tests/dxso_parity.rs`, an integration test: the key
  excludes `tests/` (`lib.sh`), it can use `mtld3d-core`'s emitters and `objc2-metal` as dev-dependencies (both in
  `Cargo.lock` already: no new crate), and it skips, saying so, without a Metal device or corpus. The dev-dependency
  edit changes airconv's `Cargo.lock` entry, so Task 8 updates the lock (the key's `cargo tree --locked` dies
  otherwise; `-e normal,build` keeps dev-dependencies out of the hash, `lib.sh:233-240`). Cost if wrong: a key move per
  tool edit if it had been an example.
- **Ruling C3-17 (e2e programs):** spec §8 names them for C3's plan; they run in no MacNeutron gate today. C3b's Task
  tries to link `tests/d3d9/programs` for `x86_64-pc-windows-msvc` with llvm-mingw's lld (as `tools/arm64x-link.sh`
  does for the DLLs); if it fails, the suite stays parked and xwin is the outward ask. C3a's parity rests on C3-15 and
  conformance. Cost if wrong: one fewer pixel oracle in C3b.
- **Ruling C3-18 (fixed function, C3c):** the FF builder writes DXSO tokens (`vs_3_0`/`ps_3_0`, FF constant rows as
  today, register limits not enforced by our front end) from `FfVsKey`/`FfPsKey`; the same parser, lowering, wire and
  cache follow, and FF records gain their source (regenerable, unlike today's). Cost if wrong: an FF-only IR path.
- **Ruling C3-19 (licences):** moved mtld3d files keep their zlib notice in `airconv-ir`, whose `license` becomes
  `LGPL-2.1-or-later AND MIT AND Zlib` (the MIT is the DXBCParser port's, `src/airconv/ir/Cargo.toml`; the comment above
  it gains the moved files), as `dxmt-util`'s pairing (`src/util/Cargo.toml:6`); the README's list of zlib files
  elsewhere gains them (§8 Licences). Cost if wrong: a notice fix.
- **Ruling C3-20 (MSL fingerprint):** `core/build.rs`'s emitter fingerprint lists the moved files by name at their new
  paths (`dxso.rs`, `dxso/{opcode,parser,ir,link,spec}.rs`), not the `dxso/` folder, so Tasks 4-6's `dxso/lower*` files
  do not move it; the move changes `SHADER_EMITTER_VERSION` once, so players' programmable MSL records regenerate once
  at prewarm, with identical MSL, in the same release as the key move. Cost if wrong: one slower first launch per D3D9
  game.
- **Ruling C3-21 (32-bit PE callers):** `g10.rs`'s WoW64 converter refuses `SM50_SHADER_D3D9` (no 32-bit PE caller
  exists: on the Mac D3D9 is in-process, C3-4; the i686 guest `mtld3d.so` goes through the guest's WoW64 table,
  `vm/metal/wm/guest/wow64.c`, then `sm50-args32.c`, the wire and the host's 64-bit thunk). `walk_chain` has no error
  path (`g10.rs:570`), so `sm50_compile_32` pre-scans the 32-bit chain for type 10 and returns
  `STATUS_INVALID_PARAMETER`. The guest's `sm50-args32.c` converts the node. Cost if wrong: a converter row later.
- **Ruling C3-22 (gates):** per the lighter-task-testing memory (2026-10-30, "Skip the gates on sub-subtask"): each task
  runs only its own tests (unit, CK, LX, ID, the task's layout or args32 checks); every gate (builds into `build/`,
  `check.sh`, the VM gates) runs once, at Task 10, before the push, the whole spec §8 "at every step" set, since
  `winemetal`, the translator, the mtl facade and the transport all change. Game benches are outward and parked.
- **Ruling C3-23 (airconv-ir stays leaf):** `airconv-ir` gains no path dependency (the key script dies on a path crate
  outside `src/airconv` among airconv's dependencies, `wine-arm64/lib.sh:233-240`); it may gain registry crates already
  in `Cargo.lock` (`bitflags`). `DeclUsage` moves into `airconv-ir` and `mtld3d-shared` re-exports it (the reverse
  direction); `D3DDECLUSAGE_*` values are inlined; the moved code's two `log_once` sites (`ir.rs:561`, `link.rs:245`)
  become returned warnings that `mtld3d-core` logs under `mtld3d::dxso` with today's text. Cost if wrong: a log line's
  target or timing differs.
- **Ruling C3-24 (the D3D9 argument's wire form):** the payload crosses the VM trust boundary (the host copies the
  guest's node bytes verbatim, `slots.c:115-123`) and must be built by the LLVM-free guest facade (`mtld3d-mtl`
  depends on bitflags, log and rustc-hash; `airconv`'s `build.rs` needs `AIRCONV_LLVM`/xcrun), so: C carries it as
  bytes, `struct SM50_SHADER_D3D9_DATA { void *next; enum SM50_SHADER_COMPILATION_ARGUMENT_TYPE type; uint32_t stage;
  uint8_t vs[<VS_SPEC_WIRE>]; uint8_t ps[<VARIANT_KEY_WIRE>]; }` (lengths as literals, measured in Task 1), one
  struct, so `winemetal-layouts.sh --airconv` (which wants a `#[repr(C)] pub struct` named `camel(<C tag>)` in
  `abi.rs`, `tools/winemetal-layouts.sh:161-176`) needs only the mirror `Sm50ShaderD3d9Data`, plus
  `pub const SM50_SHADER_D3D9: u32 = 10;` in `abi.rs`. The types and their codec live in `airconv_ir::dxso::spec`
  (`StageSpec::{to_wire, from_wire}`, `SM50_SHADER_D3D9`, `VS_SPEC_WIRE`, `VARIANT_KEY_WIRE`), built on every target,
  used by `airconv` (host) and `mtld3d-mtl` (guest, which builds its own width-dependent node in `mtl/src/sm50.rs`, not
  `wmt.rs`, whose `#[repr(C)]` structs `dxmt/mtld3d-layout32-check.sh` compares across widths). `from_wire` writes each
  field at its `offset_of!` (padding zero) and refuses a stage above 1, a `lod_table` byte above 1 (a Rust `bool` from
  guest bytes would be undefined behaviour), and any `alpha_func`, fog mode or `VariantFlags` bit `emit.rs` has no
  case for; `SM50Compile` refuses a `stage` that disagrees with the program's version token. Cost if wrong: one more
  wire version (the type number is ours).

## Global Constraints

- **Who pushes.** Lane agents never push, merge to `main`, fetch or download; the owner pushes in Task 10 under the
  standing authorization (ledger, 2026-10-29), after its gates. No MacNeutron.app, Steam or game; nothing under
  `~/Library/Application Support/MacNeutron`; kill by exact PID; home written `~` (never a full home path); privacy scan
  before every push. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; DXMTRS subjects
  `<area>: sentence`.
- **Usage stop rule** (memory, 2026-10-31): check weekly usage at every wake-up; at 95% used stop autonomous work (no
  new agents, running ones stopped, state in the ledgers); at 90%+ start only work that lands something already done.
- **Names.** `$M` = `~/Documents/MacProton`, `$D` = `$M/build/wine-arm64-src/dxmt`, `$S` = `$M/build/c3`, `$B` =
  `$M/build`. DXMTRS integration worktree `$S/wt` (branch `c3a` from 5d240384), MacNeutron worktree `$S/wt-mn` (branch
  `c3a-mn` from 4c99f4f, `BUILD_DIR=$M/build` in the environment, never as a make argument: rule 6).
- **Lanes** (C1). Each task runs in lane `$S/wt-<lane>` on branch `c3a-<lane>`, cut from the `c3a` tip when the task
  starts (after its dependencies are integrated): `git -C $D worktree add -b c3a-<lane> $S/wt-<lane> c3a`, outside any
  lock job (rule 2), its submodules filled read-only as step 3's lane procedure
  (`docs/superpowers/plans/2026-10-30-dxmtrs-step3-d3d10core-dxgi.md:331`, `chmod -R u+w` before removal). The owner
  integrates a lane as soon as its task commits: `git -C $S/wt-<lane> rebase c3a`, `git -C $S/wt merge --ff-only
  c3a-<lane>`, `git -C $S/wt cherry c3a c3a-<lane>` empty, a ledger line; a conflict in `Cargo.lock` or
  `tools/derive_inventory.txt` is resolved by regenerating them (below), never by hand. Then dependent lanes start.
  Merged lanes: `rm -rf` the worktree and `git -C $D worktree prune` under the lock, `git branch -D` after the cherry
  check (rule 7: only C3's branches).
- **The lock.** Every build or install into the shared folders, every gate reading installed outputs and every
  checkout in `$D` runs as one job:
  `/usr/bin/lockf -k $M/build/pipeline.lock sh -c 'git -C $D checkout -q --detach <sha> && <build> && <gate>'`, the
  job naming and logging its `<sha>` (`git -C $D rev-parse c3a` when it starts) and wine.app's cdhash. Before a push
  or pin `$D` is restored to `$(cat $B/wine-arm64-src/dxmt.applied)` (rule 5; fa3bc094 today). Never
  `git submodule deinit`, never re-clone or delete `$D` (rule 7).
- **Checkouts for gates.** `dxmt/check.sh`, `vm/builder.sh` and the VM scripts read `$ROOT/build` (`check.sh:13-15`,
  `builder.sh:14-17`), so they never run from `$S/wt-mn`: the Mac gates run from the main checkout at `main`; jobs
  that need `c3a-mn`'s scripts (Task 9's, Task 10's VM jobs) run from the main checkout detached at `c3a-mn` for the
  job (rule 9; `git -C $M status -sb` shows `## main` before, restored by `trap 'git -C $M checkout -q main' EXIT`).
- **Builder** (build-folders-deleted and builder-out-share memories). Export `BUILDER_MAC=52:54:00:ea:c0:03` for every
  `vm/builder.sh` call and VM job (the script's default `:02` is not used). Before starting a builder: `pgrep -x
  vmhost` and `ps -p <pid> -E -o command=`; one running with `BUILDER_MAC=52:54:00:ea:c0:03` and `VM_SHARES=out=…` naming
  `$M/build/eac-vm-work/images` is used (`sh vm/builder.sh ssh true` answers), and a second is never started. On
  2026-11-07 pid 85900 is that builder (MAC and shares as above), though its argv names a removed folder
  (`build/refactor/mn-main/...`), ledgered; if it does not answer, the owner stops it by that pid and starts
  `VM_SHARES="out=$M/build/vm-work/images,outp=$M/build/p/vm-work/images" sh vm/builder.sh start`.
- **Signing env** for `make wine-arm64`, `vm/dxmt/build.sh`, `vm/host/build.sh`, `vm/image/guest-stages.sh dxmt`:
  `MACNEUTRON_SIGN_IDENTITY="Developer ID Application: Chad Cormier Roussel (49QMZXLR8S)"`,
  `MACNEUTRON_PROVISIONING_PROFILE=$M/build/Mac_Neutron.provisionprofile` (the `~/Downloads` copy the signing-env
  memory names is gone).
- **Rust.** Every cargo line runs offline (`--offline`, or `CARGO_NET_OFFLINE=true`). `airconv` builds need
  `AIRCONV_LLVM=$M/build/wine-arm64-src/llvm23-arm64-nolto AIRCONV_XCRUN=$M/wine-arm64/tools/xcrun-metal.sh`.
  **UT(<crates>)** host unit tests on `aarch64-apple-darwin`; **CK(<crates>)** = nightly `cargo fmt --all --check`,
  `cargo clippy --offline -p <crates> --all-targets -- -D warnings` on `aarch64-apple-darwin` and every
  `-pc-windows-msvc` target the crate ships on, `RUSTDOCFLAGS="-D warnings" cargo doc --offline --no-deps -p <crates>`,
  UT, `git -C <wt> add -A && sh tools/audit.sh` → `audit: clean (<n> files)`. **LX** = `sh tools/linux-cargo.sh`
  builds of `mtld3d-core` and `mtld3d-mtl` for the guest's `x86_64` and `i686` (`MTLD3D_SYSROOT`). **RL** =
  `tools/rust-check.sh` → `rust-check: ok`, with the `AIRCONV_*` pair and `MTLD3D_RUST`, `MTLD3D_RUST_NIGHTLY` (both
  from `$M/dxmt/pins`), `MTLD3D_LLVM_MINGW` (the parent of what `sh $M/dxmt/toolchain.sh` prints, with `BUILD_DIR`
  set), `WINE_BUILD` and `GUEST_WINE_SDK=$M/build/eac-vm-work/guest-wine-sdk` (`rust-check.sh:37-44`). **Manifests:** a
  task that edits a `Cargo.toml` or moves a derived type runs `cargo metadata --offline --format-version 1 > /dev/null`
  and `sh tools/audit.sh --update-derives`, and commits `Cargo.lock` and `tools/derive_inventory.txt` with the task
  (`build_translate` builds `--locked`, `dxmt/tools.sh:29-31`; the key's `cargo tree --locked`, `lib.sh:233-235`).
- **ID (the identity gate, C3-10):** `sh $S/xlate.sh <wt> <name>` (Task 0) builds `dxil-translate` from `<wt>` into
  `$S/bin/<name>`, translates every C3-10 corpus with `--no-metal --out` (the mode that prints `dis=`, `refl=`, `tags=`
  and `air=`, `dxil-translate.mm:1002-1004`), runs `python3 -I $M/dxmt/tools/corpus-diff.py $S/base/<c>.txt
  $S/runs/<name>/<c>.txt` per corpus (each `corpus-diff: <n>/<n> ok, 0 new failures, 0 fixed, 0 changed, 0 extra, <n>
  unchanged`), fails on its exit 1 or any `^air-only` line, computes the tree's `translator_key`, and ends
  `xlate: <name> ok (<c> corpora), key <hex>`.
- Conventions (`docs/CONVENTIONS.md`): no `#[allow]`/`#[expect]`; one-operation `unsafe` blocks with `SAFETY:`; no
  `pub(crate)`; tests in `<stem>/tests.rs`; `foo.rs` + `foo/`; `FxHashMap`; derives through
  `tools/audit.sh --update-derives`. zsh: gating scans use `/usr/bin/grep`.
- Behaviour visible outside the translator for DXBC/DXIL is unchanged (C3-10, C3-3's D3D10-12 refusal); D3D9's runtime
  is unchanged in C3a (its MSL, cache bytes and draw path; C3-20's fingerprint move excepted).

## Review Focus

1. C3-3's sniff and the D3D10-12 refusal: every caller of `SM50Initialize` that is not D3D9's goes through
   `dxmt_core::shader::initialize`?
2. C3-2: each (opcode, control) pair, relative-address operand kind and source modifier against Tasks 4-6; any left
   neither lowered nor `Unsupported`?
3. C3-7/C3-8: the AIR pair's slots, conditions, attribute numbering and varying names against
   `emit.rs`/`ff.rs`/`mtl.rs`, and point sprite's texcoord override.
4. C3-10: corpora and the diff rule enough to see a change to shared IR (`ir.rs`, `binding.rs`, `signature.rs`)?
5. C3-15's tolerance and inputs: negative, >1 and zero-divisor ranges; can the 8-bit tolerance hide a sign or saturate
   bug?
6. C3-24 on the i686 guest: the node's 32-bit layout, `-malign-double`, `layout32-check.sh`.
7. Licence and audit: zlib files inside an LGPL+MIT crate; `cargo metadata` notices.

## Execution

| Task | Lane | Starts from | Implementer | Reviews | Depends on | Owns | Lock |
|---|---|---|---|---|---|---|---|
| 0 base, kit, references | owner (`$S/wt`, `$S/wt-mn`) | 5d240384 / 4c99f4f | Sonnet | — | — | `$S/{xlate.sh,base,bin,runs}`, ledgers, plan commit | no |
| 1 move DXSO into airconv-ir | `ir` | `c3a` after 0 | Sonnet | Sonnet + Opus | 0 | `src/airconv/ir/{Cargo.toml,src/lib.rs,src/dxso…}`, `src/d3d9/core/src/dxso…`, `core/{build.rs,Cargo.toml}`, `src/d3d9/shared/{Cargo.toml,src/shader_create.rs}`, README zlib list, `Cargo.lock`, derive inventory | no |
| 2 ABI, sniff, `Shader::Dxso` | `abi` | `c3a` after 1 | Sonnet | Sonnet + Opus | 1 | `airconv_public.h`, `src/airconv/src/{abi,abi/layouts,shader,api}.rs`, `entry/{initialize,compile}.rs`, `src/airconv/src/dxso.rs` (stub), `src/winemetal/unix/src/g10.rs`, `src/dxmt/src/shader.rs` | no |
| 3 AIR point size, point coord | `air` | `c3a` after 1 | Sonnet | Sonnet + Opus | 1 | `ir/src/binding.rs`, `src/air/signature.rs` | no |
| 4 lowering: VS | `vs` | `c3a` after 2, 3 | Opus | Sonnet + Opus | 2, 3 | `ir/src/dxso/lower…` (VS), `src/airconv/src/dxso…` | no |
| 5 lowering: PS 2-3 | `ps` | `c3a` after 4 | Opus | Sonnet + Opus | 4 | `ir/src/dxso/lower/ps…`, the IR's compare variants, their converter functions | no |
| 6 lowering: PS 1.x | `ps1` | `c3a` after 5 | Opus | Sonnet + Opus | 5 | `ir/src/dxso/lower/ps1…` | no |
| 7 corpus capture | `cp` | `c3a` after 1 | Sonnet | Sonnet | 1 | `core/examples/emit_corpus.rs`, `tests/d3d9/conformance/src/{cli,run}.rs`, `tests/d3d9/dxso-corpus/` | Step 3 |
| 8 parity test | `pt` | `c3a` after 4 (WIP), rebased after 6, 7 | Sonnet (harness), Opus (fixes) | Sonnet + Opus | 6, 7 | `src/airconv/tests/dxso_parity.rs`, `src/airconv/Cargo.toml` dev-deps, `Cargo.lock` | no |
| 9 VM transport | `vm` + `$S/wt-mn` | `c3a` after 2 | Sonnet | Sonnet | 2 | `$S/wt-mn` `vm/metal/wm/{sm50-wire.h,slots.c,guest/sm50-args32.{h,c},tests/sm50-args32-test.c}`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh`; DXMTRS `src/d3d9/mtl/{Cargo.toml,src/sm50.rs,src/sm50/tests.rs,src/wmt.rs}` | Steps 1-2 |
| 10 C3a gates, push, pin | owner | `c3a` with 1-9 | Sonnet | Opus final | 1-9 | spec line, ledgers, pin | yes |

Tasks 3 and 7 run beside 2; Task 9 beside 4-6; Task 8's lane is written and run locally beside 4-6, committed on
`c3a-pt` as work in progress (no loss when a worktree is removed), squashed at integration after 6 and 7. Task 8's
parity fixes land as new commits on `c3a` after `ps1` is integrated, each a failing unit test first (Opus, with an Opus
lens). Reviews: per the lighter-task-testing memory, a Sonnet spec+gate lens on every task and an Opus correctness
lens on the translator tasks 1-6 and the parity fixes; Minors get no fix round (batched into Task 10's ledger).

---

### Task 0: base, kit, references (owner)

**Files:** scratch `$S/xlate.sh`, `$S/base/`, `$S/bin/base/`; worktrees `$S/wt`, `$S/wt-mn`; DXMTRS
`src/d3d9/core/examples/emit_corpus.rs` (`--digest`, usage line `:3`); MacNeutron `$S/wt-mn/docs/superpowers/plans/
2026-11-07-dxmtrs-c3-dxso-translator.md` (this file, untracked in the main checkout today).

- [ ] **Step 1:** worktrees, no lock (rule 2): `git -C $D worktree add -b c3a $S/wt 5d240384`, `git -C $M worktree add
      -b c3a-mn $S/wt-mn 4c99f4f`; submodules as the Lanes line. Copy this plan into `$S/wt-mn` and commit it
      (`docs: the DXMTRS C3a plan`), so Task 10's merge carries it; from then on `$S/wt-mn`'s copy is the live one
      (plan amendments are committed there), and the untracked copy in the main checkout stays until Task 10 Step 5
      deletes it just before the merge (git refuses to overwrite an untracked file). Program ledger: a 2026-11-07 line "C3 STARTED:
      maintainer's go 'Start C3' (chat, 2026-11-07); binding fork B by default; release hold to C3d (C3-9); plan
      docs/superpowers/plans/2026-11-07-dxmtrs-c3-dxso-translator.md"; step ledger
      `.superpowers/sdd/2026-11-07-dxmtrs-c3-dxso-translator/progress.md` opened.
- [ ] **Step 2:** `$S/xlate.sh <wt> <name> [--base]`: `set -eu`; `M=~/Documents/MacProton ROOT=$M UNIX_CPU= UNIX_LTO=`;
      `. $M/wine-arm64/lib.sh` (`die`, `toml_sections`, `translator_key`); `. $M/dxmt/pins` (`MTLD3D_RUST`);
      `PATH="$(BUILD_DIR=$M/build sh $M/dxmt/mtld3d-tools.sh):$PATH"` (a program that prints its folder, never
      sourced; as `wine-arm64/build.sh:525`); `. $M/dxmt/tools.sh`; `mkdir -p $S/bin/<name> $S/runs/<name>`;
      `build_translate arm64 $M/build/wine-arm64-src/llvm23-arm64 <wt> $S/cargo-<name> $S/bin/<name> $S/runs/<name>`
      (the last argument is the log folder); then per corpus `<c>` (folder, extra arguments): `shaders`
      (`dxmt/tests/shaders`), `dxil` (`dxmt/tests/dxil`), `reject` (`dxmt/tests/dxil/reject`), `dxbc`, `dxbc-320`
      (`--metal 320`), `dxbc-320f` (`--metal 320 --shader-flags 3`) on `dxmt/tests/dxbc`, `dxbccorpus`,
      `dxbccorpus-320`, `dxbccorpus-320f` on `dxmt/tests/dxbc-corpus`, `d3d11`, `d3d11-320`, `d3d11-320f` on
      `dxmt/tests/shaders/d3d11`, `irdata` (`<wt>/src/airconv/ir/tests/data`): `$S/bin/<name>/dxil-translate <folder>
      <args> --no-metal --out $S/runs/<name>/<c>.out > $S/runs/<name>/<c>.txt`; `--base` writes into `$S/base/`
      instead and diffs nothing; otherwise corpus-diff per corpus as ID says. Last: `key=$(translator_key <wt>)` called
      as `wine-arm64/build.sh:531-540` calls it, and `echo "xlate: <name> ok (13 corpora), key $key"`. `sh -n` clean;
      two mutation REDs: a hand-edited `refl=` in one `.txt` makes it exit 1, and an injected `air-only` line does.
- [ ] **Step 3:** references, no lock (host-only, nothing in `$D`): `sh $S/xlate.sh $S/wt base --base`; the base key
      logged.
- [ ] **Step 4:** `emit_corpus --digest` prints `msl-digest <xxh3 hex> <n> shaders` over every MSL it emits, in order
      (test: two runs, one digest); usage line updated; commit `d3d9(core): emit_corpus prints a digest of its MSL`
      (integrated into `c3a` at once); then `cargo run --offline --release -p mtld3d-core --features disk-cache
      --example emit_corpus -- --iters 1 --digest > $S/base/msl.txt`.

**Expected:** `$S/base/<c>.txt` for the 13 corpora, each ending in a summary line; `xlate: base ok (13 corpora), key
<hex>`; `$S/base/msl.txt` one `msl-digest` line; worktrees clean; plan committed on `c3a-mn`.

### Task 1: the DXSO parser, IR, link data and specialization types move into `airconv-ir` (lane `ir`)

**Files:** create `src/airconv/ir/src/dxso.rs`, `src/airconv/ir/src/dxso/{opcode,parser,ir,link,spec}.rs` with
`parser/tests.rs`, `link/tests.rs` (the data half), `spec/tests.rs`; modify `src/airconv/ir/src/lib.rs` (`pub mod
dxso`), `src/d3d9/core/src/dxso.rs` (re-exports), `src/d3d9/core/src/dxso/{link,emit}.rs` (imports; `VariantKey`,
`VariantFlags`, `VsSamplerKinds` leave `emit.rs`), `src/d3d9/core/Cargo.toml` (`airconv-ir`), `src/d3d9/core/build.rs`
(fingerprint list), `src/d3d9/core/src/dxso/tests.rs` (guard), `src/airconv/ir/Cargo.toml` (`bitflags`, licence and
its comment, C3-19), `src/d3d9/shared/{Cargo.toml,src/shader_create.rs}` (`pub use airconv_ir::dxso::DeclUsage`),
README's zlib list, `Cargo.lock`, `tools/derive_inventory.txt`; delete `src/d3d9/core/src/dxso/{opcode,parser,ir}.rs`
and `parser/tests.rs`.

**Interfaces:**
- `airconv_ir::dxso::{parse, DxsoProgram, DxsoError, ShaderType, RegKind, Declaration, DeclUsage, TextureType,
  CmpFunc, Instruction, Semantic, SemanticSet, LinkInputs, MAX_LINKED_INPUTS, decl_passthrough_code}` with today's
  signatures (`parse(&[u32]) -> Result<DxsoProgram, DxsoError>`).
- `airconv_ir::dxso::spec::{VariantKey, VariantFlags, VsSamplerKinds, VsSpec, StageSpec, SpecError,
  SM50_SHADER_D3D9, VS_SPEC_WIRE, VARIANT_KEY_WIRE}`: the first three byte-for-byte as `emit.rs:50-321` (`#[repr(C)]`,
  manual `Hash` kept); `VsSpec { provided_input_mask: u16, clip_plane_count: u8, sampler_kinds: VsSamplerKinds }`
  `#[repr(C)]` with size/offset assertions; `enum StageSpec { Vs(VsSpec), Ps(VariantKey) }`; `StageSpec::to_wire(&self)
  -> (u32, [u8; VS_SPEC_WIRE], [u8; VARIANT_KEY_WIRE])` and `StageSpec::from_wire(stage: u32, vs: &[u8; VS_SPEC_WIRE],
  ps: &[u8; VARIANT_KEY_WIRE]) -> Result<StageSpec, SpecError>` (C3-24's checks); `SM50_SHADER_D3D9: u32 = 10`.
- `mtld3d_core::dxso::*` paths unchanged for every caller (`pub use airconv_ir::dxso::…`).

- [ ] **Step 1 (RED):** `airconv_ir::dxso::spec::tests::variant_key_layout_is_the_emitters` (`size_of` and every
      field's `offset_of!` as measured on `emit.rs`'s type at the base, written into the test),
      `…::wire_round_trips`, `…::lod_table_2_is_refused` (`SpecError`), `…::unknown_variant_flag_is_refused`; fail to
      compile before the move.
- [ ] **Step 2:** `git mv` the files (history kept), fix paths, keep each file's zlib header (C3-19); `link.rs` splits
      at `VsOutputs`: the data half moves, the MSL half stays and imports it. C3-23's edits: `DeclUsage` and the
      `D3DDECLUSAGE_*` values, the two warnings returned, and the moved code made clean under `airconv-ir`'s lints
      (`ir/src/lib.rs:10-18`: no `expect`, `panic` or slice indexing outside tests; `parser.rs:322`, `link.rs:251, 275,
      281`). The wire codec (C3-24).
- [ ] **Step 3:** `core/build.rs` hashes the six moved files by name at their new paths (C3-20);
      `every_module_the_emitter_names_is_fingerprinted` updated and green. Manifests line (Global Constraints).
- [ ] **Step 4:** CK(`airconv-ir`, `mtld3d-core`, `mtld3d-shared`), LX, ID.

**Expected:** every `mtld3d-core` test passes unchanged (the emit and ff tests are the MSL identity check);
`emit_corpus --features disk-cache --iters 1 --digest` equals `$S/base/msl.txt`; LX builds both guest widths; the
same D3D9 log text on Task 1's warning tests; `xlate: ir ok (13 corpora), key <hex>`.
Commit: `airconv-ir: the DXSO parser, IR, link data and specialization, moved from mtld3d-core`.

### Task 2: the ABI, the sniff and `Shader::Dxso` (lane `abi`)

**Files:** `src/airconv/airconv_public.h`, `src/airconv/src/abi.rs` (+`abi/tests.rs`), `src/airconv/src/shader.rs`,
`src/airconv/src/entry/{initialize,compile}.rs` (+tests), `src/airconv/src/dxso.rs` (stub `convert` returning
`Error::unsupported("dxso: not lowered")`), `src/airconv/src/abi/layouts.rs` (regenerated with
`sh tools/winemetal-layouts.sh --airconv`, `:52-55`), `src/winemetal/unix/src/g10.rs` (+tests),
`src/dxmt/src/shader.rs` (+`shader/tests.rs`). (`winemetal/sys/sm50-abi.h` holds prototypes only and does not change.)

**Interfaces:**
- C: `SM50_SHADER_D3D9 = 10` and `struct SM50_SHADER_D3D9_DATA` as C3-24, the array lengths Task 1's
  `VS_SPEC_WIRE`/`VARIANT_KEY_WIRE` as literals.
- Rust: `abi::Sm50ShaderD3d9Data` (`#[repr(C)]`, `[u8; N]` payload fields; layout asserts in the regenerated
  `abi/layouts.rs`), `abi::SM50_SHADER_D3D9: u32 = 10` with `const` asserts that it, `VS_SPEC_WIRE` and
  `VARIANT_KEY_WIRE` equal `airconv_ir::dxso::spec`'s; `ArgChain { …, d3d9: Option<&Sm50ShaderD3d9Data> }` filled by
  `keep_first`.
- `Shader::Dxso(Dxso { program: DxsoProgram, entry: EntryView })`; `SM50Initialize` writes a zeroed reflection with
  only the stage set; `SM50Compile` on `Dxso` without a `d3d9` argument → `dxso: no D3D9 specialization`, with a wire
  `from_wire` refuses → `dxso: bad D3D9 specialization`, with a stage that disagrees with the version token → `dxso:
  stage does not match the program`; `SM50GetArgumentsInfo` on `Dxso` answers empty tables; the geometry and
  tessellation compiles refuse a `Dxso`; `DXILGetEntry` (void, `api.rs:360`) answers the `Dxso`'s `EntryView`.
- `dxmt_core::shader::initialize` returns `Err(Sm50Error(0))` (D3D11's `E_FAIL`, D3D12's `E_FAIL`, as for any refusal
  today) when `bytecode` does not start with `b"DXBC"`, before `SM50Initialize`.

- [ ] **Step 1 (RED):** `entry::initialize::tests::reads_a_dxso_vs_2_0_token_stream`,
      `…::reads_an_unaligned_dxso_stream`, `…::refuses_a_dxso_with_version_0xfffe0400`, `…::refuses_a_stream_of_3_bytes`,
      `…::a_dxbc_container_still_reads`, `abi::tests::d3d9_data_layout`,
      `entry::compile::tests::dxso_without_d3d9_argument_is_refused`, `…::d3d9_argument_with_lod_table_2_is_refused`,
      `…::dxso_stage_mismatch_is_refused`, `dxmt_core::shader::tests::refuses_a_dxso_token_stream` (no
      `SM50Initialize` call reached).
- [ ] **Step 2:** the sniff before `Container::parse` (`entry/initialize.rs:69`, C3-3's preconditions); the D3D10-12
      refusal; `g10.rs`'s `sm50_compile_32` pre-scans the 32-bit chain and returns `STATUS_INVALID_PARAMETER` on type 10
      (C3-21), with `g10::tests::d3d9_argument_is_refused_from_wow64`.
- [ ] **Step 3:** CK(`airconv`, `airconv-ir`, `winemetal`, `dxmt-core`), `sh tools/winemetal-layouts.sh --airconv |
      cmp - src/airconv/src/abi/layouts.rs`, ID.

**Expected:** the new tests pass; `cmp` silent; `xlate: abi ok (13 corpora), key <hex>`. Commit: `airconv: DXSO shaders
through SM50Initialize, and the D3D9 compilation argument` (D3D10-12's refusal in the same commit).

### Task 3: AIR's vertex point size and fragment point coord (lane `air`)

**Files:** `src/airconv/ir/src/binding.rs`, `src/airconv/src/air/signature.rs` (+ tests). Named varyings need nothing
new (C3-8).

**Interfaces:** `FunctionOutput::PointSize` (vertex, `air.point_size`, float; mesh's `MeshVertexOutput::PointSize`
metadata, `signature.rs:545`, is the model); `FunctionInput::PointCoord` (fragment, `air.point_coord`, float2).

- [ ] **Step 1 (RED):** `air::signature::tests::vertex_point_size_metadata`, `…::fragment_point_coord_metadata`, and
      `…::top_level_texture_sampler_buffer_metadata` (one `texture2d`, `depth2d`, `texturecube`, `texture3d`,
      `sampler` and `constant float4*` at fixed indices: D3D9 is the first production user of top-level textures and
      samplers, `signature.rs:351-354`), each comparing the printed metadata nodes to Metal's (taken once from `xcrun
      metal -S` of a short MSL file in the test's comment, as `signature`'s existing tests do).
- [ ] **Step 2:** the variants and their metadata; DXBC/DXIL never construct them.
- [ ] **Step 3:** CK(`airconv`, `airconv-ir`), ID, the diff rule (C3-10).

**Expected:** `xlate: air ok (13 corpora), key <hex>`; the diff rule prints nothing. Commit: `airconv: vertex point
size and fragment point coord`.

### Task 4: lowering, vertex shaders (lane `vs`)

**Files:** create `src/airconv/ir/src/dxso/lower.rs`, `lower/{vs,ops,regs,inline,tests}.rs`; fill
`src/airconv/src/dxso.rs` (+`dxso/{bindings,tests}.rs`): the D3D9 `BindingMap` and the VS signature.

**Interfaces:**
- `airconv_ir::dxso::lower::vertex(&DxsoProgram, &VsSpec) -> Result<Lowered, LowerError>`; `Lowered { cfg: Cfg, io:
  SignatureState, outputs: SemanticSet }`; `LowerError::Unsupported { opcode: u16, what: &'static str }`.
- `airconv::dxso::convert(shader: &Dxso, spec: &StageSpec, args: &ArgChain<'_>, name: &CStr, m: &Module<'_>)
  -> Result<(), Error>`, called from `entry::compile::compile`'s new arm; and the public Rust entry
  `airconv::dxso::translate(tokens: &[u32], spec: &StageSpec, name: &CStr) -> Result<Vec<u8>, Error>` (metallib
  bytes), used by Task 8 and by C3b's Mac path (airconv has no other public compile, `src/lib.rs:24-30`).
- `D3d9Bindings: BindingMap` with C3-7's slots and conditions; constants `c#` as `constant float4*` (relative
  `a0`/`aL` indexing through the IR's indexed buffer read, C3-2's clamp and `def` overlay), `i#` `int4*`, `b#` one
  `uint` bit mask, PosFixup (5 floats), VsDraw (12 float4), vertex LOD `float2*`; vertex textures/samplers 0-3 with
  `VsSamplerKinds`.

- [ ] **Step 1 (RED):** `lower::tests::every_opcode_has_a_lowering_or_is_unsupported`: a table over every
      (opcode, control) pair of `opcode.rs` (texldp, texldb, the comparisons and setp are `TexLd`, `Ifc`, `BreakC`
      and `SetP` plus control bits), every relative-address operand kind and every source modifier, both stages; VS
      rows filled here, PS rows `Unsupported` until Tasks 5-6. Then per feature: `vs_1_1_m4x4_position`,
      `vs_2_0_loop_rep_call`, `call_callnz_label_inlined` (`callnz b#` and `callnz p0`, `aL` inside a subroutine, the
      depth limit), `vs_3_0_predicates_and_setp`, `relative_constant_index` (an index that hits a `def` register, one
      out of range with `def`s present), `if_bool_constant_bit`, `lit_dst_nrm_crs_sincos_pow_expansions`,
      `absent_input_reads_zero`, `clip_planes_from_vs_draw`, `point_size_clamped_by_vs_draw`,
      `fog_output_and_default`, `pos_fixup_half_pixel_and_depth_bias`, `texldl_vertex_sampler_kinds`,
      `vs_buffers_only_under_their_condition`, `nonfinite_position_is_not_sanitized`; each checks the IR and that
      `airconv::dxso::translate` returns a metallib.
- [ ] **Step 2:** the lowering, the inliner (C3-2) and the VS signature: outputs per C3-8 (position `invariant`, the
      base members in MSL's order, extras by semantic, `point_size`, `clip_distance[n]`), epilogue as
      `emit.rs:900-931`; `oPos` pops through `Epilogue::Pop` into `FunctionOutput::Position`, never `PopPosition`
      (whose `sanitize_position` replaces a non-finite position, `replay.rs:241-245`, `builder.rs:1603-1625`; the MSL
      writes it raw).
- [ ] **Step 3:** CK(`airconv`, `airconv-ir`), ID, the diff rule.

**Expected:** the VS rows of the opcode table all lowered; `xlate: vs ok (13 corpora), key <hex>`; the diff rule
prints nothing. Commit: `airconv: DXSO vertex shaders`.

### Task 5: lowering, pixel shaders SM2-3 (lane `ps`)

**Files:** `src/airconv/ir/src/dxso/lower/{ps,ps/tests}.rs`, the PS signature in `src/airconv/src/dxso.rs`;
`src/airconv/ir/src/ir.rs` (`SampleCompareLevel`, `SampleCompareGrad`, new variants only) and their converter functions
in `src/airconv/src/dxbc/converter.rs` (new functions and match arms only, C3-10).

**Interfaces:** `airconv_ir::dxso::lower::pixel(&DxsoProgram, &VariantKey) -> Result<Lowered, LowerError>`; inputs by
`LinkInputs` and `linked_input_mask`; `FrontFacing` (vFace), `Position` (vPos, declared with `fix_w: false`, since
`init_input_reg` rewrites `.w` to `1/w` otherwise, `replay.rs:853-865`, and the MSL reads `in.position` raw,
`emit.rs:2937`; `VPOS_SCALE` × PsDraw), `PointCoord` under `POINT_SPRITE` (texcoord0-7 overridden as
`write_point_sprite_prologue`); outputs `color(i)` by `color_out_mask`, depth unless `NO_DEPTH_ATTACHMENT`,
`sample_mask` under `SAMPLE_MASK`; arguments under C3-7's conditions.

- [ ] **Step 1 (RED):** PS rows of the opcode table for SM2-3, then `texld_2d_cube_volume_by_declaration`,
      `texldp_texldb_texldd`, `depth_compare_and_raw_depth_red_masks`,
      `depth_compare_texldl_texldd_and_lod_bias_level`, `fetch4_gather`, `lod_bias_row`,
      `no_lod_bias_argument_without_the_flag` (bias a constant 0, no slot 9), `ps_buffers_only_under_their_condition`,
      `alpha_test_each_cmp_func` (alpha ref slot 14; ALWAYS/NEVER as `emit.rs:1562-1580`),
      `table_fog_linear_exp_exp2_z_and_w`, `srgb_write`, `flat_shade_colors`, `point_sprite_texcoords`, `vpos_scale`
      (`.w` raw), `vface`, `texkill`, `predicated_texld_is_a_select`, `odepth`, `mrt_color_out_mask`,
      `sample_mask_output`, `dsx_dsy`, `linked_extras_only_when_vs_writes_them`.
- [ ] **Step 2:** the lowering and the two IR variants; CK(`airconv`, `airconv-ir`), ID, the diff rule.

**Expected:** SM2-3 PS rows lowered; `xlate: ps ok (13 corpora), key <hex>`; the diff rule prints only the new arms'
reflowed lists. Commit: `airconv: DXSO pixel shaders, SM2 and SM3`.

### Task 6: lowering, pixel shaders 1.x (lane `ps1`)

**Files:** `src/airconv/ir/src/dxso/lower/{ps1,ps1/tests}.rs`.

- [ ] **Step 1 (RED):** the remaining opcode rows, then `tex_texcoord_ps_1_1_to_1_3`, `texcrd_texld_phase_ps_1_4`,
      `texbem_texbeml_bump_env_rows` (slot 12, two rows a stage), `texm3x2_texm3x3_family`, `texreg2ar_gb_rgb`,
      `texdp3_texdp3tex_texdepth`, `cnd_r0a_half_rule`, `modifiers_bias_bx2_x2_x4_x8_d2_d4_d8_sat`,
      `ps_1_x_range_clamp` (results and plain `c#` reads, `def`s clamped at definition, as `emit.rs:2815-2826`).
- [ ] **Step 2:** the lowering; CK(`airconv`, `airconv-ir`), ID, the diff rule.

**Expected:** `every_opcode_has_a_lowering_or_is_unsupported` has no `Unsupported` row left (or each remaining one
names the MSL emitter's own refusal); `xlate: ps1 ok (13 corpora), key <hex>`. Commit: `airconv: DXSO pixel shaders
1.x`.

### Task 7: the corpus (lane `cp`)

**Files:** `src/d3d9/core/examples/emit_corpus.rs` (`--write-cache <path>`, `--merge <out> <in>...`, usage line),
`tests/d3d9/conformance/src/{cli,run}.rs` (`--keep-shader-cache <dir>`, + tests), `tests/d3d9/dxso-corpus/{README.md,
synthetic.bin,conformance-x64.bin,conformance-i686.bin,triaged.txt}`, the README's licence list.

- [ ] **Step 1:** `--write-cache <path>` writes the synthetic programmable records with a pipeline recipe per VS/PS
      pair through `CacheWriter` (test: the written file `shader_cache::load`s with every record); `--merge <out>
      <in>...` `load`s each file and writes one bundle with `write_bundle` (`shader_cache.rs:1161`; `compact`,
      `:1296`, rewrites one file in place and cannot merge), test `merge_of_one_file_keeps_every_record`.
- [ ] **Step 2:** the runner's `--keep-shader-cache <dir>` replaces `shaderCache.enable=false` with `=true`; the cache
      lives beside the test EXE (`unix/src/shader_prewarm.rs:3`), so the runner deletes `<exe dir>/mtld3d_shaders.bin`
      before each subtest and moves it to `<dir>/<leg>-<subtest>.bin` after it (no subtest sees another's warm cache);
      `run::tests::keep_shader_cache_config`, `…::cache_moved_after_each_subtest`. Integrate Steps 1-2 into `c3a`.
- [ ] **Step 3 (lock):** one job, `$D` detached at the `c3a` tip holding Steps 1-2 (sha logged), from the main checkout:
      `sh $M/dxmt/mtld3d-conformance.sh --keep-shader-cache $S/corpus/x64` (arm64x leg), then `sh
      $M/dxmt/mtld3d-conformance.sh --arch i686-arm64 --exe $B/wine-arm64-tests/winetests/x86/d3d9_test.exe
      --keep-shader-cache $S/corpus/i686` (the runner keeps the last `--arch`/`--exe`, `cli.rs:103-117`); `$D`
      restored to the job's start commit. Outside the lock: `emit_corpus --merge` per leg into
      `conformance-{x64,i686}.bin`; record counts in `README.md` per file (shaders by kind, recipes; the i686 `device`
      subtest crashes in the baseline, so its file is short).
- [ ] **Step 4:** CK(`mtld3d-core`, `mtld3d-conformance`).

**Expected:** each conformance run ends `conformance: only triaged sites (dxmt/mtld3d-triaged.txt) fail anew: passed`
or exits 0 on the runner's own pass; the three corpus files together hold ≥ 1 record of each programmable `CachedKind`
(2-7). Commit: `tests(d3d9): a DXSO corpus from the synthetic set and Wine's d3d9 tests`.

### Task 8: rendered both ways (lane `pt`)

**Files:** `src/airconv/tests/dxso_parity.rs`, `src/airconv/Cargo.toml` (`[dev-dependencies] mtld3d-core = { …,
features = ["disk-cache"] }`, `objc2-metal`, `objc2-foundation`, `dispatch2`, all in `Cargo.lock`), `Cargo.lock`
(C3-16; the key moves once, harmless in C3a, C3-9).

**Interfaces:** `cargo test --offline -p airconv --test dxso_parity` with `AIRCONV_DXSO_CORPUS=<dir>` (default
`concat!(env!("CARGO_MANIFEST_DIR"), "/../../tests/d3d9/dxso-corpus")`), AIR through `airconv::dxso::translate`: per
pipeline recipe, C3-15's two pairs and comparison; prints one line per difference (`differ <file> <vs key> <ps key>
<max delta> <format>`, `bind-differ <file> <vs key> <ps key> <stage> <kind> <index>`), per file `pos-ulps <file> <max>`
(the same VS's clip-space position under both backends, information for C3-13), and per file `dxso-parity: <file>:
<equal>/<pairs> equal, <d> differ, <b> bind-differ, <a> air-refused, <m> msl-failed, <f> ff-skipped, <t> triaged`;
fails on any untriaged `differ`, `bind-differ` or `air-refused`, and on 0 pairs; without a Metal device or corpus it
prints `dxso-parity: skipped (<reason>)` and passes.

- [ ] **Step 1 (RED, minimal mutation):** with a lowering mutated to drop `_sat` on one opcode, the test reports at least
      one `differ` on `synthetic.bin`; reverted.
- [ ] **Step 2:** inputs: attributes `f(i, vertex)` within ±4 with zeros and negatives (zero divisors for `rcp`,
      `rsq`, `log`, C3-6); `c#` rows `sin(i)`-shaped with every 16th row 0; `i#` loops 0-3; `b#` alternating; textures
      4x4 RGBA8 and RGBA16F patterns per kind (2D, cube, volume, depth with compare state as the recipe's); one draw of
      a 4x4 vertex grid as triangles, points for point-sprite recipes. The first AIR pair linked is also the first
      check of C3-8's varying names.
- [ ] **Step 3:** run on all three corpus files; each difference fixed on `c3a` after `ps1` is integrated (a new failing
      unit test first) or triaged with its reason. Manifests line.
- [ ] **Step 4:** CK(`airconv`).

**Expected:** on each file `dxso-parity: <file>: <n>/<n'> equal, 0 differ, 0 bind-differ, 0 air-refused, 0
msl-failed, <f> ff-skipped, <t> triaged` with `<n'>` > 0, and no `skipped` line. Commit: `airconv: DXSO parity against
D3D9's MSL, rendered on the Mac`.

### Task 9: the VM carries the argument (lane `vm`, `$S/wt-mn`)

**Files:** MacNeutron `$S/wt-mn/vm/metal/wm/{sm50-wire.h,slots.c,guest/sm50-args32.h,guest/sm50-args32.c,
tests/sm50-args32-test.c}`, `vm/dxmt/d3d9-gate.sh` and `vm/tests/d3d9.sh` (two rows: the args32 tests, from Task 10
on); DXMTRS `src/d3d9/mtl/{Cargo.toml (airconv-ir),src/sm50.rs,src/sm50/tests.rs,src/wmt.rs}` (slots 74, 75, 76, 77,
78, 79, 80, 114, 25 in `SLOTS`), `Cargo.lock`.

**Interfaces:** `struct mn_sm50_d3d9 { MN_SM50_ARG; uint32_t stage; uint8_t vs[…]; uint8_t ps[…]; }` with `MN_SAME` on
its size and on the offsets of `stage`, `vs` and `ps` against `SM50_SHADER_D3D9_DATA`, and `_Static_assert(sizeof <
256)` (`arg_type.size` is a `uint8_t`, `slots.c:43`); `arg_types[SM50_SHADER_D3D9] = {.size = sizeof(struct
mn_sm50_d3d9)}`; the 32-bit conversion copies the payload (today `sm50-args32.c:95-96` drops an unknown type), and the
32-bit node joins `vm/metal/layout32-check.sh`. `mtld3d_mtl::sm50::translate(tokens: &[u32], spec: &StageSpec, name:
&CStr) -> Result<Vec<u8>, String>` and `new_library(device, &[u8]) -> Option<Library>` (DispatchData 114, newLibrary
25), the node and the i686 params in `sm50.rs` (`airconv_thunks.h`'s `*_params32` layouts, `:127-147`, 32-bit
pointers, not `wmt.rs`'s 8-byte `Ptr<T>`); the shader, bitcode and error out-pointers come back as the transport's
guest handles. The Rust test `sm50::tests::translates_a_dxso_vs_and_ps_on_the_host` (vs_2_0 + ps_2_0 triangle:
translate, libraries, a pipeline) runs where `MTLD3D_WINEMETAL` names a winemetal (the Mac facade test) or in the
guest, against a translator holding Tasks 2-5, so it first passes at Task 10 (RED in Step 1); elsewhere it prints `skipped: no
winemetal`.

- [ ] **Step 1 (RED, lock: `$D` detached at the `c3a-vm` tip (cut after Task 2, so it holds Task 2's header; at Step 2 also the lane's
      committed facade code, which `mtld3d-layout32-check.sh` builds), sha logged, since `wm/Makefile:7-9`,
      `layout32-check.sh:16` and `build-guest.sh:13-18` read its header):** `sm50-args32-test.c` gains a D3D9 chain row
      asserting the node survives the conversion with its payload (fails: the node is dropped); and the Rust test,
      in the lane against the installed translator, which refuses DXSO (fails): from `$S/wt-vm`,
      `PATH="$(BUILD_DIR=$M/build sh $M/dxmt/mtld3d-tools.sh):$PATH" MACOSX_DEPLOYMENT_TARGET=27.0
      MTLD3D_WINEMETAL=$B/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so
      CARGO_TARGET_DIR=$S/cargo-vm cargo "+$MTLD3D_RUST" test --offline -p mtld3d-mtl --target aarch64-apple-darwin --lib
      sm50::tests::translates_a_dxso_vs_and_ps_on_the_host` (`dxmt/mtld3d-facade-test.sh`'s form); it passes first at
      Task 10.
- [ ] **Step 2 (lock, as Step 1; main checkout detached at `c3a-mn`, rule 9; `BUILDER_MAC` exported):** the rows and
      the facade calls; `sh vm/metal/layout32-check.sh`; `sh dxmt/mtld3d-layout32-check.sh`; `sh
      vm/metal/build-guest.sh`; both args32 binaries in the microVM as `vm/metal/gate.sh` runs guest binaries (a script
      `/mnt/share/bin/metal-sm50-args32-test && /mnt/share/bin/metal-sm50-args32-test32` through
      `MN_RUN_TIMEOUT=120 sh vm/run-guest.sh <script>`; they are x86-64/i686 Linux binaries, not the builder's); the
      d3d9-gate rows added.
- [ ] **Step 3:** CK(`mtld3d-mtl`), LX.

**Expected:** `layout32: ok (<n> structs, <m> on the wire)` with `<m>` one more than at the base; `mtld3d-layout32: ok
(<n> structs)`; `sm50-args32-test: ok` twice; `sh -n` clean on both gate scripts. Commits: MacNeutron `vm-metal: SM50's
D3D9 argument through the transport`; DXMTRS `d3d9(mtl): translate DXSO through winemetal's SM50 slots`.

### Task 10: C3a gates, push, pin (owner)

- [ ] **Step 1:** every lane integrated (`git -C $S/wt cherry c3a c3a-<lane>` empty for each), `Cargo.lock` and the
      derive inventory regenerated if any merge touched them; RL; `sh $S/xlate.sh $S/wt c3a` → `xlate: c3a ok (14
      corpora), key <hex>`, the key differing from Task 0's (C3-9, expected).
- [ ] **Step 2 (lock, the Mac job; main checkout at `main`; `$D` detached at the `c3a` tip, sha and cdhash logged):**
      `make wine-arm64`; `sh dxmt/check.sh` (x64) and `MACNEUTRON_ARM64_TESTS=$M/build/dxmt-tests-arm64ec sh
      dxmt/check.sh` (ARM64EC), each `dxmt-check: all passed` (420/0, item 11 `d3d9-check: ok`); `sh
      wine-arm64/check.sh` and `sh presenter/check.sh` (each exit 0, no `FAIL` line; presenter 66 rows);
      `sh dxmt/mtld3d-conformance.sh` (`conformance: only triaged sites (dxmt/mtld3d-triaged.txt) fail anew: passed`, or
      exit 0); the loopback: `so=$(sh dxmt/mtld3d-build.sh --loopback)`, `MTLD3D_SO=$so sh dxmt/d3d9-check.sh`
      (`d3d9-check: ok`); `sh dxmt/mtld3d-facade-test.sh` (cargo's `test result: ok`, with `test
      sm50::tests::translates_a_dxso_vs_and_ps_on_the_host ... ok` and no `skipped: no winemetal`); `sh
      dxmt/wm-probe-check.sh` (wine.app's `winemetal.so`; 11 `ok   wm-probe g<n>` lines); Task 8 on the merged tip (C3-15's line per file).
- [ ] **Step 3 (lock, the VM job; main checkout detached at `c3a-mn`, rule 9; `$D` at the same sha; signing env,
      `BUILDER_MAC`; the builder as Global Constraints):** rebuild, in this order: `sh vm/dxmt-native/build.sh`; `sh
      vm/image/guest-stages.sh dxmt d3d` (it runs `vm/dxmt/build.sh`, `vm/metal/build-guest.sh` and `vm/dxmt/stage.sh`,
      `:41-47`); the image, `sh vm/builder.sh sync vm/image mn-image && sh vm/builder.sh ssh 'sh ~/mn-image/build.sh
      wine'` (then `wine.erofs` under `$B/vm-work/images` is newer than the job's start, else exit 32 went unseen);
      `sh vm/host/build.sh` (copies the new `winemetal.so` into `metal-host`, `:108`). Then: `sh dxmt/wm-probe-check.sh
      --native $B/dxmt-native/install/lib/winemetal.dylib` (11 `ok   wm-probe g<n>`); `sh vm/dxmt-native/build-tests.sh && sh vm/dxmt-native/gate.sh`
      (`dxmt-native gate: all 33 programs passed`); `sh vm/d3d/loopback-gate.sh` (`d3d-loopback gate: all 33 programs
      passed`); `sh vm/dxmt/gate.sh` (`dxmt gate: all 47 programs passed`); `sh vm/d3d/gate.sh` (`d3d gate: all 43
      programs passed`); `sh vm/dxmt/d3d9-gate.sh` and `MN_WOW64=new sh vm/dxmt/d3d9-gate.sh` (each `d3d9 gate: ok`,
      with `ok   unit mtld3d_mtl` and `ok   unit 32-mtld3d_mtl`, `test sm50::tests::translates_a_dxso_vs_and_ps_on_the_host
      ... ok` in `$B/vm-work/share/traces/d3d9/unit-mtld3d_mtl.txt` and `unit-32-mtld3d_mtl.txt`, and the two args32
      rows `ok`); `sh vm/dxmt/present-gate.sh` and `sh vm/dxmt/present-gate.sh forward` (each `present gate: all
      passed`); `sh vm/tests/d3d-smoke.sh` and `MN_WOW64=new sh vm/tests/d3d-smoke.sh` (each `d3d-smoke: ok`).
- [ ] **Step 4:** Opus final review of `5d240384..c3a` and `4c99f4f..c3a-mn` (given the code and the plan, not these
      results); Critical/Important fixed, each fix re-running the Step 2/3 rows its files feed; Minors ledgered.
- [ ] **Step 5 (push, the pipeline's per-step loop item 4):** (1) restore `$D` to `$(cat
      $B/wine-arm64-src/dxmt.applied)` (rule 5, lock); (2) privacy-scan `git -C $S/wt log -p 5d240384..c3a`, `git -C $D
      push --dry-run origin c3a:main`, then push; (3) on `c3a-mn`: `DXMT_COMMIT=<pushed sha>`, spec §5 C3 bullet "C3a
      done …", commit; (4) the pushed pin's development build (lock, never a forced pinned build: rule 7): `$D` detached
      at the pushed sha, `make wine-arm64`, `sh dxmt/check.sh` x64 → `dxmt-check: all passed`, `git -C $D ls-remote
      origin main` names the pushed sha, then rule 5's restore; (5) `git -C $M status -sb` shows `## main` (the plan's untracked copy
      removed first), `git -C $M merge --ff-only c3a-mn` (or a merge commit if `main` moved), privacy scan of `4c99f4f..main`, push `main`. No
      release (C3-9).
- [ ] **Step 6:** program ledger, step ledger and the `dxmtrs` memory updated: the new key, the pushed shas, the
      deferred minors, the ledgered builder argv, and two open maintainer items: the release hold (C3-9) and the SMITE 2
      corpus re-capture (C3-10; a game run, parked until a go-ahead).

**Expected:** every line above as stated; DXMTRS `main` and MacNeutron `main` pushed; no tag.

---

## C3b outline: the runtime switch (own plan, after C3a)

1. `shader.backend` in `core/src/config.rs` and `mtld3d.conf` (C3-12, default `msl`), logged at device creation.
2. Mac: `mtld3d-unix` gains `airconv` under `[target.'cfg(target_vendor = "apple")'.dependencies]` (cargo cannot
   select on the build-script cfg `mtl_facade`, `unix/build.rs:23-29`), its uses gated by `cfg(not(mtl_facade))`
   (C3-4); every Apple `mtld3d-unix` cargo run then needs `AIRCONV_LLVM`/`AIRCONV_XCRUN` (`airconv/build.rs:18-19, 77`):
   meson, the loopback `MTLD3D_SO` build, d3d9-check's builds and clippy. `dxmt_translator_key_h` is made in
   `src/dxmt/meson.build:18` but `src/meson.build` enters `d3d9` (line 7) before `dxmt` (line 10): the configure step
   moves ahead of `subdir('d3d9')`; the guest's `linux-cargo.sh` build gets `DXMT_TRANSLATOR_KEY_H` from
   `vm/dxmt/build.sh`. `translate(tokens, &StageSpec, name)` has one signature on both cfgs (Mac:
   `airconv::dxso::translate` in-process; facade: Task 9's `sm50::translate`); Metal library from bytes
   (`newLibraryWithData`, dispatch2 on the Mac; slots 114+25 in the guest). Binary checks: `otool -L mtld3d.so` gains
   only `libz` and `libxml2` (airconv's Darwin extras, `src/airconv/darwin/meson.build:11-14`, as `winemetal.so` has
   them), recorded as listed differences (§8), `nm -gU mtld3d.so` only the intended exports (no `SM50*`), size logged.
3. `compile.rs`: `LibraryInput` gains the backend; AIR libraries from `translate`; C3-13's per-program support set and
   pair rule at pipeline resolution, with C3-13's (a)/(b) pick; the failed-variant retry.
4. Cache: AIR `CachedKind` 8-15 (C3-11), entry names `mtld3d_{vs|ps}_air_{kind}_{key}`, prewarm builds AIR libraries
   from metallibs and regenerates stale-key records from source; `shader_cache::tests` for each; a
   `vm/dxmt/d3d9-gate.sh` row that the guest stage's key equals the host build's.
5. Gate scripts gain an AIR lane: `D3D9_BACKEND=air` sets `MTLD3D_CONFIG=shader.backend=air` in `d3d9-check.sh`,
   `mtld3d-conformance.sh` (its runner appends it) and `vm/dxmt/d3d9-gate.sh`; the AIR conformance lane holds the MSL
   lane's figures (Wine's visual tests are pixel checks).
6. C3-17's e2e linking attempt, with `bench_shader_stutter.rs`/`bench_cold_start.rs` as the stutter evidence if it links.
7. Full gates as Task 10 Steps 2-3 (the same rebuild order first), plus `d3d9-check` and conformance and d3d9-gate on
   both backends.

## C3c outline: fixed function through the same builder

`mtld3d-core::dxso::ff_build::{vertex(&FfVsKey) -> Vec<u32>, pixel(&FfPsKey) -> Vec<u32>}` writing DXSO (C3-18), with
an assembler `airconv_ir::dxso::write(&DxsoProgram) -> Vec<u32>` and a round-trip test (parse ∘ write = id). FF records
store the key and tokens; `synthetic_ff` and the conformance FF keys go into the corpus; Task 8's parity test gains the
FF pairs against `ff.rs`'s MSL and drops `ff-skipped`. The pair rule stops forcing MSL for FF stages. Gates as C3b's
item 7 on the AIR lane.

## C3d outline: parity, the flip, retirement

1. Parity evidence: Task 8 on every corpus `0 differ` untriaged; AIR conformance equal to MSL's on both legs; the AIR
   lanes of every D3D9 gate green for one full gate run.
2. Flip `shader.backend` to `air` (C3-12); `msl` stays as the escape.
3. Outward, parked: the D3D9 milestone benches the maintainer picks (spec §8, a 32-bit title in the VM separately),
   after C3's D3D10-12 milestone benches (SMITE 2, Palworld, a D3D11 title) for the key move.
4. After the benches: delete `emit.rs`, `ff.rs`'s MSL, link's MSL half, the MSL `CachedKind`s 0-7 (skipped as unknown
   kinds thereafter), `refresh_msl`, `compile_shader_library`'s source path and the emitter fingerprint; winemetal's
   slots 144/157 stay until C5 (spec §5 C5). Release with C3 complete: one D3D10-12 turnover and one cold D3D9 cache.

## Open risks

- **Lowering size (C3-2).** ps_1_x and D3D9's numeric corner cases (rcp/rsq of 0, `pow` of negatives, ps_1_x clamps)
  are where the estimate can double; Task 8's corpus is the stop.
- **Metal's matching of AIR-named varyings (C3-8).** If Metal links AIR stages by something other than the user name
  (interpolation qualifiers, order), Task 8's first linked pair shows it (written beside Tasks 4-6).
- **Mixed backends before C3c (C3-13).** One VS under two compilers; `pos-ulps` sizes it for C3b's pick.
- **Constant buffers beyond 4 KiB.** FF VS rows reach the palette (`FF_VS_PALETTE_BASE_ROW = 95`); setBytes spills
  today (`unix/src/metal/command.rs:3043-3060`); the AIR pointer type must not assume a size (C3c).
- **Compile cost.** LLVM translation plus `newLibraryWithData` per first-seen shader versus `newLibraryWithSource`;
  measured by C3b's stutter benchmark if the e2e suite links, otherwise by the parked benches.
- **Corpus breadth (C3-14).** Wine's tests and the synthetic set skew towards SM2/SM3; game-only patterns wait for the
  maintainer's go-ahead on local game caches.

## Review record (2026-11-07)

Reviews 2 (translator, ABI, VM: 0 Critical, 10 Important, 14 Minor) and 3 (executability: 2 Critical, 14 Important, 14
Minor), each checked against DXMTRS 5d240384 and MacNeutron 4c99f4f before folding. Every Critical and Important is
applied; none was ruled against. Where the two reviews' fixes for the D3D9 argument differed (review 2 I7 and review 3
I5: the types in `airconv-ir`; review 3 I4: generator-named mirrors in `abi.rs`), C3-24 takes both: the types and codec
in `airconv_ir::dxso::spec`, one generator-named mirror whose payload is bytes. Review 2 M6 (`stage` redundant): kept
and checked against the version token. Review 3 M7 (SMITE 2 net): parked as a maintainer item, not asked mid-run.

**Deferred minors.**
- Review 3 M5, first half (a decoded dump of the committed `.bin` files for the privacy scan): the records hold DXSO
  tokens, specialization, generated MSL and recipes only (`shader_cache/source.rs:14-100`), from the synthetic set and
  Wine's tests; a `--dump` mode for `emit_corpus` lands when the corpus first takes a non-generated source (C3b's game
  caches, local only). The second half (regeneration README, 0 pairs fails) is applied.
- Review 2 M5 (a d3d9-gate row asserting the guest stage's key equals the host's): no AIR record exists in C3a, so a
  skew costs nothing yet; moved to C3b item 4. C3-11's wording is fixed now.
