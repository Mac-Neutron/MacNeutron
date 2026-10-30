# DXMTRS step 4W: our typed-pointer AIR writer for LLVM 23 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Metal accepts AIR written from LLVM 23 IR. To get there, a typed-pointer bitcode writer goes into
Mac-Neutron/llvm-project with a C entry point that the Rust translator will call at 4b. An oracle harness shows that
it reproduces the C++ translator's LLVM 15 output on the whole corpus.

**W's exit (when 4b can switch):** Task 8's table all green (SMITE 2 and the DXBC corpora at `--metal 320` included),
`usetyped=0` on every repo corpus in `defs` and `opt23`, and Task 8 Step 5 (the check suite on our AIR) run, or P18
ruled by the controller.

**Architecture:** The writer is a copy of LLVM 23's DXIL writer, lifted out of the DirectX target into a component of
its own, `LLVMAIRBitWriter`. It changes three things:
- Pointee types come from a table the caller hands it, then from facts that survive opaque IR. It never defaults to
  `i8*`.
- It writes LLVM 15's encodings wherever the DXIL writer's 3.7 layout loses meaning: attributes, fast-math flags,
  `local_unnamed_addr`, `poison`, `freeze`, the identification block, the hash and the Darwin wrapper.
- It inserts pointer casts where a use needs another pointee type than the operand has.

The oracle needs no Rust. The C++ translator's LLVM 15 metallibs are dumped to typed text and a type table by `air15`,
an LLVM 15 tool in MacNeutron. They are read with LLVM 23, written by `llvm-air-write` (the fork's tool, which calls
the C entry point), read back with LLVM 15 and compared. Metal then builds every pipeline from the result. Three legs:
- **full:** the whole table, which must give identical typed text;
- **defs:** only the type-level table that 4b's translator can supply, with the helpers typed by their refs alone;
- **opt23:** LLVM 23's passes first, as 4b will run them (inside `llvm-air-write`, P15).

Every leg also checks that the written bitcode reads back as the module the writer left (P9's `self` row), and `defs`
and `opt23` check that this module equals its input up to the inserted casts and the counted drops (`text23n`).

**Tech Stack:** C++17 inside llvm-project at llvmorg-23.1.4, LLVM 15.0.7 (typed reader, for the oracle), Objective-C++
and sh/Python dev tools in MacNeutron (`dxmt/tools`), `xcrun metal`, Metal (through `dxil-translate --use`), lit and
FileCheck.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md` (§4 E2 results and "Decided", §5 step 4 and PACE, §8
translator gate). Inputs:
- `build/step4/map/0-porting-map.md` (W row, K8, K9, K18);
- `6-critic.md` (rows 13, 14, 20, 22, 23 apply here);
- `7-decisions.md` (4-D1 to 4-D10, binding);
- maps 1, 2 and 4 for facts.

Format follows `2026-10-30-dxmtrs-step3-d3d10core-dxgi.md` and `2026-10-30-dxmtrs-stepR-layout.md`; Phase 0's E2
method is in `2026-10-29-dxmtrs-phase0.md` Tasks 3-4.

**Paths:**
- `$M` = `~/Documents/MacProton`.
- `$W` = `$M/build/step4/w` (4-D7: every output under `build/step4`).
- `$F` = `$M/build/forks/llvm-project` (origin `Mac-Neutron/llvm-project`).
- `$FW` = `$W/llvm-src`: `$F`'s integration worktree on `air-23`, and the source of the in-tree build.
- `$P0` = `$M/build/phase0`.
- `$I23` = `$P0/llvm-23`: LLVM 23.1.4 install with tools, read-only. Its headers are 23.1.4's; the spike changed only
  DirectX `.cpp` files.
- `$L15` = `$M/build/step4/llvm15`: 4-D6's non-LTO LLVM 15, shared with 4a.
- `$D` = `$M/build/wine-arm64-src/dxmt`: DXMTRS. W reads it only through `git -C $D show <rev>:<path>`.
- `$S` = `$M/build/step4`: 4a's step 4 folder (its `base/`, `bin/` and `llvm-project`), named as 4a names it.
- `L15/` = `$M/build/dxmt-src/llvm-project/llvm`; `L23/` = `$P0/llvm-23-src/llvm`; `DW/` =
  `L23/lib/Target/DirectX/DXILWriter`; `A/` = DXMTRS `src/airconv` at da6ed2d (read with `git -C $D show`).

Every command below runs with these names exported (`export M W F FW P0 I23 L15 D S`), because several of them pass
the names inside `sh -c '…'`. `$I23` is always the prefix; the harness's bin folder is its own variable, `BIN23`
(Task 6).

**Ledger:** `.superpowers/sdd/2026-10-30-dxmtrs-step4w-air-writer/progress.md`.

**Step R:** W edits no DXMTRS file. It reads only `src/airconv/shaders/*.metal` and `src/airconv/metallib_writer.cpp`,
which R does not move, so nothing here waits for R or is rebased on it. 4b's DXMTRS paths are given in post-R form
(Task 10). Before Task 10 merges it, W's MacNeutron branch `step4w` is rebased onto `dxmtrs`, which by then carries
R's and step 3's pin bumps. W adds new files there and edits only `dxmt/tools/dxil-probe.cpp`.

## Decisions

Binding (`7-decisions.md`), short form:
- **4-D1** Two plans now: 4a, and 4W (this plan: the typed-pointer AIR writer for LLVM 23, gated by an oracle harness
  on the C++ output, its own lane in Mac-Neutron/llvm-project). 4b, the LLVM 23 switch, is planned after Z from W's
  results. W ends when 4b can switch (W's exit, under Goal).
- **4-D2** LLVM bindings are hand-declared, with no llvm-sys. 4b declares this plan's C header the same way (2
  functions, 4 structs, layout-asserted); 4b redoes the declarations for LLVM 23.
- **4-D3** The pass pipeline and airconv's two custom passes stay C++ in the shim through 4b. W's opt23 leg runs the
  stock passes only; the custom passes already ran at 15.
- **4-D4** The key after Z hashes `cargo tree -p airconv`, the toolchain and the shim's inputs. W moves no key: it
  edits neither `dxmt/llvm.sh` nor `wine-arm64/lib.sh`.
- **4-D5** No process exits on bad input. For W: the writer never aborts and returns its error as text (P17).
- **4-D6** One non-LTO LLVM 15 (`$L15`) serves every lane, outside the pipeline's build dirs. Step 4's DXMTRS commits
  stay off main until Z; W has none.
- **4-D7** Corpus: the repo's shaders, `tests/dx11`'s HLSL compiled to DXBC, and the game captures the maintainer
  allowed (2026-10-30, "Yes, both": SMITE 2's capture and 3DMark's recording; read in place, read-only, outputs only
  under `build/step4`, never committed).
- **4-D8** The gate tool is MacNeutron's `dxil-translate`, extended by 4a's lane G (`dis=`, `--no-metal`, all DXBC
  stages, `--pipelines`). W's gate uses G5's baselines and tool (`$S/base/<C>.out`, `$S/bin/base-<G1b>/dxil-translate`),
  with G3's DXBC stages and G6's `--pipelines`.
- **4-D9** Milestone benches need the maintainer's go-ahead. W runs none; GPU time is 4b's gate.
- **4-D10** Three adversarial Opus plan reviewers.

Map defaults this plan uses, as amended here:
- **K8**: widened by P8.
- **K9**: made type-level by P4 (finding F1).
- **K18**: cite the pushed `phase0` (d2e6d75) and the fork's `phase0-air-writer` (02e8d34). The 15dx run
  (`$P0/e2/use-15dx.txt`, 0/64) was built against an unknown tree and is not evidence. The 35/64 is unreproduced: on
  disk the spike gives 27/64 (`use-23t`).
- **Critic rows**: 13 (notices), 14 (no profile cfg reaches the writer; at 4b LLVM 23 is built for std and p), 20 (no
  `llvm.sh` edit), 22 (`$L15`), 23 (tool ports: `dxil-probe` here, `dxil-translate` at 4b).

Rulings this plan adds (ledgered; Task 10 carries the spec-changing ones):

- **P1 Branch `air-23` from `llvmorg-23.1.4`** (7a941df) in `$F`, not from the fork's `main`.
  - Why: `main` is upstream trunk with our lld commit, and llvm-mingw pins it (`LLVM_MINGW_LLVM=bff2c9ff`). The
    translator pins the newest release (spec §4).
  - Cost: a second long-lived branch, against the org's main-only convention. The writer is forward-ported when the
    translator's LLVM moves. `phase0-air-writer` stays as the record.
- **P2 A new component, `LLVMAIRBitWriter`**, at `llvm/lib/Bitcode/AIRWriter/`.
  - Its writer and enumerator are copied from `lib/Target/DirectX/DXILWriter/` at 7a941df, with the spike's two
    writer fixes (02e8d34: a pointer load's own typed type; an `air*` triple keeps its triple and data layout).
  - DirectX is untouched, and the component builds with `LLVM_TARGETS_TO_BUILD=""` and no experimental target (4b's
    recipe).
  - The copy leaks opaque types, which the spike's output shows (`$P0/air-23t/lod.ps.dxil.1.metallib`: `STRUCT_NAME
    'dxilOpaquePtrReservedName'` then `OPAQUE`). Its enumerator enumerates opaque types
    (`DW/DXILValueEnumerator.cpp:392,399-401,465,500-506,955`), `getTypeID(T, V)` consults the pointer map only for
    pointers and constants (`DW/DXILBitcodeWriter.cpp:531-544`), so GEP source, alloca, parameter and struct-body
    types go through raw (`:1098-1100,2314,2486`), and an opaque `ptr` is written as that named `OPAQUE`
    (`:1082-1092`). Ours enumerates only typed types, in module order (Task 2), and its `PointerTyID` case is an error.
  - Cost: about 4.2k copied lines that inherit no upstream DXIL fixes.
- **P3 The C interface: one writing entry point plus one type constructor** (Task 2's Interfaces).
  - A typed type is an ordinary LLVM 23 type whose pointers are `TypedPointerType`s, as PointerTypeAnalysis builds
    them (`L23/lib/Target/DirectX/DirectXIRPasses/PointerTypeAnalysis.cpp:143`). Struct and array types accept such
    elements (`L23/lib/IR/Type.cpp:767-833`).
  - The C API cannot make a `TypedPointerType`, hence `LLVMAIRTypedPointerType`.
  - Why: 4b's `Ty` lowers straight to these types, and checking that a typed type erases to the value's own type is
    one recursive comparison.
  - Cost: amends spec §4's "one C entry point" to one writing entry point (Task 10).
- **P4 What 4b hands the writer is type-level.**
  - Required: every function whose type holds a pointer (definitions and `air.*` declarations); every global whose
    value type holds a pointer; every identified struct with pointer fields (typed body); the helper bitcode the
    translator links, as `TypedRefs`.
  - Per-value entries are accepted, but they are valid only for values alive at write time. The full leg uses them.
  - Function and global entries are keyed by `LLVMValueRef` too, so 4b builds the whole table after the passes, by
    walking the module (Task 10's hand-off).
  - Why: `runOptimizationPasses` (`A/airconv_context.cpp:69-158`) runs between creating a value and writing it
    (`A/metallib_writer.cpp:30`). SROA, InstCombine and GlobalOpt replace values, and an `LLVMValueRef` kept from
    creation dangles, its address reused. `deadargelim` replaces a function with a new one (new type, name taken over)
    and `globalopt` can SRA-split or delete a global, so function and global handles kept from creation dangle the
    same way. LLVM 23 also canonicalizes constant GEPs to `i8`
    (`L23/lib/Transforms/InstCombine/InstructionCombining.cpp:3414`).
  - Cost: the writer derives instruction types itself (P5, Task 4), proven by the defs and opt23 legs (finding F1).
- **P5 Where a pointer's pointee type comes from, in order.**
  1. The table.
  2. Typed refs, by name.
  3. Definition facts that opaque IR keeps: alloca type; global value type; GEP source type and indices; load type;
     the call's typed function type (Task 4's required types); arguments from their function's typed type;
     addrspacecast and no-op bitcast follow their operand; extract and insert follow their aggregate.
     - **Literal aggregate types.** airconv's descriptors are literal structs, `{ <handle>, i64, i64, i64 }` and the
       sampler pair (`StructType::get` at `A/dxbc_binding_rootsig.cpp:48,76,129,193`), not identified ones, so P4's
       table cannot name them. 225 GEPs in 35 of the 96 modules use them, and 11 typed literals erase to 3 opaque
       ones. A GEP's typed source type is its base's typed pointee when that erases to the GEP's source type;
       otherwise it is rebuilt through the indices from the typed types its result's uses demand (step 4). Alloca
       and load types that are aggregates follow the same rule.
  4. **Everything else is typed by one worklist fixpoint over use edges**: `inttoptr`, a pointer loaded through
     memory whose typed pointee is not a pointer, select and phi, an argument of a function with no entry, a literal
     field with no fact.
     - Edges: load and store (slot and value), GEP, phi, select, no-op cast, icmp, cmpxchg and atomicrmw, call
       arguments.
     - Seeds: steps 1-3 (the table and the refs give the `air.*` declarations, hence the handle types).
     - A pointer to an opaque struct (a handle) wins over other candidates; remaining ties go by instruction order.
       The losing uses get casts (P6).
     - It ends: a value's type only goes from unset to set, and to a handle at most once.
     - Values typed this way are derived, not guessed: each is listed by `--explain`, none is counted.
  5. **A pointer no seed reaches** (one that is only compared, only `ptrtoint`'d, only stored as a value nothing
     reads typed) is typed `i8` in its own address space and counted (`UseTyped`). No access goes through it, so the
     choice is harmless. The target is 0 on every repo corpus (Task 7).

  `null`, `undef` and `poison` pointers take each use's own type. No constrained pointer defaults to `i8*`.
  - Why: E2's failures were `i8*` defaults and use-based inference with no definitions (spec §4). The 4b shape leaves
    descriptor-table pointers and handles as chains of `load ptr`: measured with LLVM 23's `instcombine` on
    `lod.ps.dxil`, 85 literal GEPs become 0 literal and 64 `i8` GEPs, `%argument_buffer_struct` is referenced 0 times
    and the function holds 100 `load ptr` (`%16 = load ptr addrspace(2), %2`, then `%17 = load ptr addrspace(1), %16`,
    then `call @air.sample_texture_2d(%17, …)`). Indexed descriptors (`IdxDescriptor`, `A/dxbc_binding_rootsig.cpp:
    53,81,134,198`) keep the literal GEP (`shouldCanonicalizeGEPToPtrAdd`, `InstructionCombining.cpp:3198-3225`).
    "First use in instruction order" gives no order for such chains; a fixpoint anchored in the declarations' types
    does.
  - Controller option at 4b (not taken here): 4b's translator makes the three descriptor types identified structs
    (`%dxmt.desc.<kind>`, at the four sites above), so they enter P4's table. That is an output change inside 4b's
    key turnover; Task 10 carries the question.
  - Cost: casts, which Metal has read in airconv's AIR all along (634 instruction bitcasts in the 96-module set).
- **P6 Casts.**
  - Where a use needs another pointee type in the same address space, the writer inserts `bitcast ptr to ptr` into
    the module: before the user, or at the end of the incoming block for a phi.
  - Constant casts, and typed `null`/`undef`/`poison`, are synthetic constants that the enumerator owns per (constant,
    typed type) pair. LLVM 23 folds same-type constant casts, and the 96 modules use `ptr addrspace(1) null` as `i32*`
    131 times, `float*` 3 times and `i64*` once.
  - Every `Use` has a required type, constant-expression operands, constant aggregate elements and global
    initializers included, and synthetic instances recurse: a cast nested inside a constant GEP (after LLVM 23's
    passes, `getelementptr inbounds (i8, ptr addrspace(2) @sample_pos_2, i64 k)` over `[2 x i8]`), the corpus's 3
    constant `inttoptr (i64 4 to i32 addrspace(1)*)`, a typed `zeroinitializer`, `undef` or constant struct whose type
    holds a pointer. The copied writer types CE operands from their opaque types (`DW/DXILBitcodeWriter.cpp:2150,
    2169-2172`); ours does not.
  - The constants block's `SETTYPE` records and its sort are keyed on typed types. The copy keys both on the opaque
    `V->getType()` (`DW/DXILBitcodeWriter.cpp:1994-2000`, `OptimizeConstants`), so the three typed nulls above would be
    written under one `SETTYPE` and read back as `i32*`.
  - A mismatch across address spaces is an error. The writer never invents an addrspacecast.
  - Cost: the caller's module is modified and must not be written again.
- **P7 Record layout.**
  - The DXIL writer's 3.7 layout stays (`MODULE_CODE_VERSION` 1, module-level VST names, relative ids); Metal reads
    it (E2).
  - Where 3.7 loses meaning, the writer uses LLVM 15's encodings, as `L15/lib/Bitcode/Writer/BitcodeWriter.cpp`
    writes them (Task 3's rows).
  - The identification block says producer `LLVM15.0.7`, epoch 0, because the records are LLVM 15's and that is the
    only identification Metal has accepted (64/64). It also gets `MODULE_CODE_HASH` and the Darwin wrapper.
  - The module gets `MODULE_CODE_SOURCE_FILENAME` (16) as LLVM 15 writes it. Every C++ module carries
    `source_filename = "airconv_generated.metal"`; the DXIL writer writes only `MODULE_CODE_VERSION` at module level
    (`DW/DXILBitcodeWriter.cpp:2917`), and without the record LLVM 23's reader takes the ModuleID, the file's path
    (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:4928-4932`, `L23/lib/IR/Module.cpp:75`). text15 and text23 would differ
    on every module otherwise.
  - Cost: no byte identity with LLVM 15's output, since the layouts differ. Identity is semantic instead (P9).
- **P8 Attributes.**
  - The writer uses LLVM 15's whole kind table (codes up to 84), not K8's six. The corpus also holds `speculatable`
    (53, 2 uses), `nocallback` (71, 2) and `convergent` (23), and the DXIL writer's `default:` for these is
    `llvm_unreachable` (`DXILBitcodeWriter.cpp:725-727`), which is UB in a build without assertions.
  - `memory(...)` (86) becomes LLVM 15's forms: exact for `readnone`, `readonly`, `writeonly`, `argmemonly` and its
    read/write forms, `inaccessiblememonly` and `inaccessiblemem_or_argmemonly`. Other forms become the strongest
    weaker set that is still sound, counted.
  - `captures(none)` (102) becomes `nocapture` (11); any other `captures(...)` is dropped and counted.
  - LLVM 23-only kinds are dropped only if Task 3's guarantee-only list names them; any other kind is an error naming
    it.
  - Every drop and every weaker form (attributes and Task 3's 23-only flags) is applied to the module before it is
    written, as the casts are (P6). So the module the writer leaves is exactly what was written, which P9's `self` row
    checks. `AttrsDropped` counts attribute-group entries.
  - Cost: the list is re-checked at every LLVM bump (finding F2).
- **P9 The oracle.** The comparisons a leg makes, with the legs that fail on them:

  | Comparison | Gates |
  |---|---|
  | Typed text (`text15`): `air15 dis` of the output equals the C++ output's | full leg |
  | Opaque text (`text23`): `llvm-dis` (23) of both sides, after dropping line 1 (ModuleID) | full leg |
  | Self (`self`): `llvm-dis` (23) of the output equals `llvm-air-write --emit-ll`'s `.post.ll` (the module as the writer left it, after casts and drops), line 1 dropped on both | every leg |
  | Normalized text (`text23n`): the output and the writer's input (`.pre.ll`), both through `instsimplify` and `llvm-dis`, attribute references stripped, are equal once Task 3's drop-list tokens are deleted from the input side; the deleted tokens number `flags-dropped`, and `attrs-dropped` equals the drop-list attribute entries in the input's `attributes #` lines | defs and opt23 legs |
  | `air15 verify` (LLVM 15 typed parse and verifier, plus the atomics and `llvm.*` checks of Task 1) | every leg |
  | Record inventory (P9b) | every leg |
  | Darwin wrapper: magic, version, offset and CPU type equal LLVM 15's | every leg |
  | Metal: `dxil-translate --use` on our files only (`used=n`, Task 6) gives the baseline's ok set and fail reasons | every leg |
  | Determinism: full, defs and opt23; two processes, and a threaded batch against a sequential run | gate |
  | Check suite: `d3d12_dxil_exec`'s rows rendering from our AIR through the shader cache (Task 8 Step 5) | gate |

  - Why `self`: the defs and opt23 legs are the only ones in which derivation, casts and synthetic constants run on
    every module, and verify, inventory, wrapper and Metal building a pipeline pass any writer bug that keeps types
    consistent (two same-typed operands swapped, a phi's cast in the wrong predecessor, `undef` written for `null`, a
    constant under the wrong `SETTYPE`). The two texts are identical by construction: LLVM 23's reader keeps
    same-address-space pointer bitcasts (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:5332-5358`) and folds same-type
    constant casts, and P8 applies every drop to the module. No normalizer.
  - Why `text23n`: `self` cannot see a cast wired into the wrong use, a flag dropped from the wrong instruction or a
    `memory(...)` mapped wrongly, because the module it compares already carries them. `instsimplify` folds `bitcast x
    -> x` (`L23/lib/Analysis/InstructionSimplify.cpp:5657-5660`), so our no-op casts vanish. Measured in opt23: 153
    LLVM 23-only flag tokens, 120 `memory(none)`, 53 `memory(argmem: read)`, 13 `memory(argmem: readwrite)` and 66
    `captures(none)`, otherwise checked only by Task 3's lit CHECKs.
  - P9b, the record inventory, in every leg:
    - every (block, record code) is in a static allowlist: the pairs `L15/include/llvm/Bitcode/LLVMBitCodes.h`
      defines, intersected with what the writer emits (Task 3's `codes-15-23.txt` is half of it). LLVM 15's reader
      skips unknown module-level records silently, and LLVM 23's passes legitimately add records the C++ output lacks
      (opt23 introduces `poison` in 12 of 96 modules), so the reference module is not the list;
    - the one same-module rule: every `STRUCT_NAME` value is in the reference's dump (it catches
      `dxilOpaquePtrReservedName` and a shadow struct written twice, which LLVM 15 renames `….0`);
    - attribute kinds are at most 84, and there is no `OPAQUE_POINTER` (code 25).
  - Positions line up: LLVM 23's reader keeps every pointer bitcast instruction (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:5332-5358`).
    Measured on `$P0/air-15`: 634 of 634 bitcasts, and 96 of 96 modules with equal instruction counts.
- **P10 Where things live.**
  - MacNeutron: `dxmt/tools/air15.cpp`, `air-tools.sh`, `air-oracle.sh` and `air-bcinv.py`.
  - Fork: `llvm/tools/llvm-air-write`.
  - No edit to `dxmt/llvm.sh`, which is a translator-key input (critic 20). No LGPL code enters the fork: air15
    repacks containers from their layout, in MacNeutron.
  - Cost: one more build script.
- **P11 Inputs.**
  - During development: a copy of the installed C++ `dxil-translate`, taken in one lock job (Task 0) with key
    `71a7e5f2…3311`, whose airconv is da6ed2d's.
  - At the gate: 4a's G5 baselines `$S/base/<C>.out` and its tool `$S/bin/base-<G1b>/dxil-translate` (all DXBC
    stages through G3, `--pipelines` through G6).
  - Why: W does not wait for G, and the installed tool is a pipeline output.
  - Cost: inputs are made again if the key differs at the gate.
- **P12 Builds.**
  - Each lane configures Task 0 Step 4's cmake line on its own worktree into `$W/build-<lane>`, with
    `-DLLVM_ENABLE_ASSERTIONS=On` and no install, and runs the real in-tree lit,
    `$W/build-<lane>/bin/llvm-lit -v $W/wt-<lane>/llvm/test/Bitcode/AIRWriter`. A few minutes once, then incremental.
  - The integration build `$W/llvm23-build` of `$FW` (Release, assertions Off, `LLVM_BUILD_TOOLS=On`,
    `LLVM_INCLUDE_TESTS=On`, `LLVM_TARGETS_TO_BUILD=""`, no experimental targets, LTO Off, `-mcpu=apple-m5`, zstd Off,
    installed into `$W/llvm23` at the gate) is what the harness runs. The controller rebuilds it (`ninja -C
    $W/llvm23-build`) after each fast-forward of `air-23`.
  - Why: a full LLVM 23 Release build with tools takes about 4 minutes here (Phase 0's: `llvm-23-build/` created
    10:48:04, `llvm-23/` installed 10:52:20, 2,605 edges, 18 cores). Lanes test with assertions on; the harness and
    the gate run what ships (assertions off).
  - Cost: one LLVM build tree per lane plus the integration one.
- **P13 Every `llvm.*` declaration outside a list is an error.**
  - The writer holds a `constexpr` list of LLVM 15 intrinsic names (mangled, as `llvm.umax.i64`) whose LLVM 15
    signature equals the declaration's, seeded with the corpus's two, `llvm.umax.i64` and `llvm.usub.sat.i32`. Any
    other `llvm.*` declaration is an error naming it, with or without pointer parameters.
  - Why: LLVM 23's InstCombine creates `llvm.scmp` and `llvm.ucmp` (`L23/lib/Transforms/InstCombine/
    InstCombineSelect.cpp:4306,5365`, `InstCombineAndOrXor.cpp:4689`, `InstCombineCalls.cpp:2263`), which LLVM 15
    lacks. LLVM 15 reads an unknown `llvm.*` declaration as a plain function and checks intrinsic calls only for known
    IDs (`L15/lib/IR/Verifier.cpp:3343`), so only Metal would decide. (`ptrtoaddr`, `L23/include/llvm/IR/
    Instruction.def:195`, is an opcode: an error under P17.)
  - Each new name gets a controller ruling, then its entry with its LLVM 15 signature checked against
    `L15/include/llvm/IR/Intrinsics.td`. Expected first rulings in Task 7: `llvm.lifetime.start`/`end`
    (`AlwaysInlinerPass` inserts them, `A/airconv_context.cpp:134`; LLVM 23's drops the size argument LLVM 15's
    takes, `L23/include/llvm/IR/Intrinsics.td:1859-1861`) and `llvm.fabs.v4f32` (measured: 22 of 96 modules after
    P15).
- **P14 Game corpus.**
  - SMITE 2's capture, `~/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt` (10,271 `.dxil`,
    150 `rs-*.bin`, `pipelines.txt`), is allowed by 4-D7 (the maintainer's "Yes, both", 2026-10-30): read in place,
    read-only, outputs only under `build/step4`, never committed.
  - It is a required corpus. In development (Task 0 Step 5, Task 7) Task 0's tool copy translates it in plain `--out`
    mode (about 4-5 minutes a run, map 4 §4); at the gate it is 4a's C8, through G6's `--pipelines`.
  - Why: it is the only game-scale input, and where use-typed values, positional mismatches, `inttoptr`, heap shapes
    and `--pipelines` root signatures occur.
  - 3DMark's recording is not used: `dxil-translate` cannot read a DXMTPRC1 recording (4a's P1 says the same).
- **P15 The opt23 pipeline**, from `A/airconv_context.cpp:110-151` minus the two custom passes:
  `annotation2metadata,forceattrs,inferattrs,function<eager-inv>(lower-expect,simplifycfg,sroa<modify-cfg>,early-cse),always-inline,ipsccp,globalopt,function(mem2reg),deadargelim,function<eager-inv>(instcombine,simplifycfg<switch-range-to-icmp>)`.
  - It runs inside `llvm-air-write --passes` (the tool only, never the library): `PassBuilder` with no
    TargetMachine, in the context that read the C++ `.bc`, then the write. That mirrors 4b's shim (4-D3), keeps the
    `air64` triple and keeps the named structs in the context for the `.types` lines.
  - Why not `opt`: LLVM 23's `opt` exits 1 on every AIR module, "unrecognized architecture 'air64'"
    (`L23/tools/opt/optdriver.cpp:696-703`; 96 of 96). With `-mtriple` (`:616-617`) the copied writer writes
    `dxil-ms-dx` and the DXIL data layout (`DW/DXILBitcodeWriter.cpp:1182-1190`), InstCombine turns struct GEPs into
    `i8` GEPs (403 against 134) and LLVM 23's writer drops every named struct (292 `STRUCT_NAME` records to 0), so the
    `s` lines and the `f` lines naming `%argument_buffer_struct` or `%struct._texture_*` fail on 65 of 96 modules.
  - Entries whose value the passes deleted, or whose erasure no longer matches, are dropped by the tool before the
    write and counted (`stale=`), each one in the ledger. That is 4b's rule too (Task 10).
- **P16 Typed refs are parsed per call**, with no global cache, because compiles run concurrently (map 1 §6). A ref
  fills only missing entries, by name. A ref that disagrees with an explicit entry is an error. The writer counts the
  names refs filled (`RefsFilled`).
- **P17 The writer never aborts.**
  - The C entry runs `verifyModule` first and returns its text on failure.
  - Every copied `llvm_unreachable`, and every `assert` on an opcode dispatch or map lookup, that an input can reach
    becomes an error naming the construct and the function. In the shipped build (assertions off) the asserts are
    otherwise UB, and lane lit runs with assertions on never show it:
    - `writeInstruction`'s `default:` arm writes any non-cast opcode it has no case for as a BINOP under only
      `assert(isa<BinaryOperator>)`, reading `getOperand(1)` out of range for `freeze`, `fneg`, `callbr` or an EH pad
      (`DW/DXILBitcodeWriter.cpp:2286-2306`). Ours errors on anything that is not a `BinaryOperator`;
    - `getTypeID`, `getAttributeListID`, `getAttributeGroupID` (`DW/DXILValueEnumerator.h:170-193`) and `getValueID`
      (`DW/DXILValueEnumerator.cpp:548`) dereference `end()` for a missing entry, such as a typed type built after
      enumeration. Ours return an error;
    - attribute kinds, type ids, constant kinds and metadata kinds.
  - Those left are over enums the verifier constrains, listed in Task 3's commit.
  - No mutable statics and no `cl::opt`: the DXIL writer's two live in `DXILWriterPass.cpp`, which is not copied.
- **P18 Spec §4's "the check suite on its output", for W.**
  - Identical typed text in the full leg, plus Task 8 Step 5: one lock job runs check.sh's `d3d12_dxil_exec` rows
    with our AIR (the defs leg's) injected into a fresh shader cache, and they must render as with the C++ AIR.
  - Why: text identity is measured through LLVM 15's reader, but Metal reads the 3.7-layout records with Apple's. A
    misread that still builds a pipeline passes every other row.
  - If the cache's stored values cannot be rewritten in place, the controller rules P18 instead as a spec change,
    with its cost: no render proof until 4b's gate.

Findings against binding text (for the controller; Task 10 records the outcome):
- **F1 (K9, spec §4 "the pointee type of every pointer it creates"):** that cannot hold after the pass pipeline. P4
  makes the contract type-level and P5 derives instruction types.
- **F2 (K8):** the attribute list misses `speculatable` and `nocallback` (in the corpus) and every other LLVM 15
  kind. P8 replaces it.
- **F3 (map 0's W row):** a harness that only round-trips LLVM 15-optimized AIR never sees LLVM 23's i8 GEPs or its
  new flags. The opt23 leg covers them.

## Global Constraints

- **Agents never push, merge to `main`, fetch or download.**
  - Task 10's pushes run under the maintainer's standing authorization, by the controller only.
  - No MacNeutron.app, Steam or game. Kill processes by exact PID.
  - No read of `~/Library/Application Support/MacNeutron` except P14's folder, which 4-D7 allows (the maintainer's
    "Yes, both"), read in place and read-only; outputs from it stay under `build/step4` and are never committed.
  - Home is written `~` in documents. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **No DXMTRS edit and no checkout in `$D`** (`git -C $D show <rev>:<path>` only). No `make wine-arm64` or
  `eac-vm/*/build.sh` run, so the signing variables are never needed. The only check.sh use is Task 8 Step 5's rows on
  the installed runtime, which builds nothing. The translator key stays
  `71a7e5f22c50f03e79c56344f7a11819f2f922e67fe9ebe4f84ed9761d763311`.
- **The pipeline lock** (`build/pipeline.md`) is used by two jobs only, Task 0 Step 2's and Task 8 Step 5's, each as
  `/usr/bin/lockf -k $M/build/pipeline.lock sh -c '…'`. Everything else is host-only work (rule 4). `$L15` is built
  under `/usr/bin/lockf -k $M/build/step4/llvm15.lock`, by both plans (the cross-plan contract), so that 4a and W
  never build it twice at once.
- **Versions.** LLVM 23 is `llvmorg-23.1.4` (7a941df). LLVM 15 is `llvmorg-15.0.7` with `fix_llvm`, non-LTO
  (`$L15/.complete` = `llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto`). C++17 in LLVM's style.
- **Licences.** Each new or copied fork file carries the Apache-2.0 WITH LLVM-exception header. Every copied or
  modified upstream file carries a `Modified by Mac-Neutron (2026): <what>` notice (Apache-2.0 §4(b), critic 13).
- **The writer's errors** read `<construct>: <why> (in @<function>)`, or `typed ref <n>: <why>`, or
  `table: <value> does not erase to <type>`.
- **No Rust** in W. 4b's hand-declared binding mirrors Task 2's header.
- **Shells.** zsh: quote `"====="`; an unquoted `$var` is not split. Gating scans use `/usr/bin/grep`.

## Execution

**Roles.**
- The controller runs the plan, creates worktrees, merges lanes, fast-forwards `air-23` in `$FW` and keeps the ledger.
- The **pipeline owner** (step 4's, shared with 4a) runs Task 0 Step 2's one-minute lock job and Task 8 Step 5's lock
  job. The owner also runs the rest of the gate, which is host-only and needs no quiet machine (W times nothing for a
  gate).
- **Lanes** work in their own worktrees with host tools only.

**Lanes and their files (disjoint):**

| Lane | Worktree, branch | Owns |
|---|---|---|
| `fork` | `$W/wt-fork`, `air-23-w` from `air-23` | `llvm/include/llvm-c/AIRWriter.h`, `llvm/lib/Bitcode/AIRWriter/*` except `AIRTypedRefs.*`, `llvm/tools/llvm-air-write/*`, `llvm/test/Bitcode/AIRWriter/*` except `typed-refs*`, the three one-line upstream edits (Task 2) |
| `refs` | `$W/wt-refs`, `air-23-refs` from Task 2's commit | `llvm/lib/Bitcode/AIRWriter/AIRTypedRefs.{h,cpp}`, `llvm/test/Bitcode/AIRWriter/typed-refs*` and `Inputs/helper-ref.*` |
| `mn` | `$W/wt-mn`, `step4w` from `dxmtrs` | `dxmt/tools/air15.cpp`, `air-tools.sh`, `air-oracle.sh`, `air-bcinv.py`, `dxil-probe.cpp` |

Uncommitted lane state lives in the lane build folders `$W/build-fork` and `$W/build-refs` (P12). The integration
build `$W/llvm23-build` belongs to the controller.

**Order and parallelism:**

| Task | Runs | Depends on |
|---|---|---|
| 0 Setup, tool copy, `$L15`, inputs (SMITE 2 included), the integration build | owner (Sonnet) | — |
| 1 air15, air-tools.sh, dxil-probe for both LLVMs | `mn` (Sonnet) | 0 |
| 2 Component, C entry point, typed types, llvm-air-write | `fork` (Opus) | 0 Step 1 |
| 3 LLVM 15's encodings | `fork` (Opus) | 2 |
| 4 Pointee derivation and casts | `fork` (Opus) | 3 |
| 5 Typed refs | `refs` (Opus) | 2 |
| 6 The harness | `mn` (Sonnet) | 1, 2 |
| 7 Burn-down: repo corpora and SMITE 2, then 4a's corpora as they land | `fork` and `mn` (Opus) | 4, 5, 6; Step 5 also 4a's G5 baselines (G3, G4, G6) |
| 8 The gate, once | owner (Sonnet) | 7 (Step 5 clean); 4a's G5 (`$S/base/<C>.out` for C1, C2, C4-C6 at 310 and 320, C8 through G6; `$S/bin/base-<G1b>/dxil-translate`) |
| 9 One fix pass | lanes (Opus) | 8 |
| 10 Spec, push, merge, 4b hand-off | controller | 9 |

**What starts before its dependencies land.** Tasks 0 to 7 Step 4 start now. None waits for step R, for 4a's lanes or
for the pipeline beyond Task 0 Step 2. Task 7 Step 5 takes 4a's corpora as each lands; Task 8 waits for all of them.
4b waits for Task 10 and for 4a's Z.

**Cross-plan interfaces 4a must keep** (the controller passes these to 4a's author now):
- **`$L15`'s contract** is the path, one source path (4a's `$S/llvm-project`, copied only under
  `[ -d $S/llvm-project/llvm ] ||`), the `build_llvm arm64 $S/llvm15 $S/llvm-project` call with `UNIX_LTO` empty, the
  `.complete` stamp, and the lock: both plans run `build_llvm` inside `/usr/bin/lockf -k $S/llvm15.lock`, and 4a's G5
  Step 1 says so. `build_llvm` uses one `$2-build` folder and one log for every caller and writes `.complete` last
  (`dxmt/llvm.sh:11-41`), so without the lock both plans can run cmake and ninja in one folder at once; with a
  different recipe it deletes the folder (`rm -rf` on a stamp mismatch) while the other plan may be using it.
- **G's `dxil-translate`**, a line for 4a's G2 with its RED there: "`--use <dir>` keeps today's semantics
  (`dxil-translate.mm:12-14,176-190`: Metal gets the file's bytes where `<dir>/<f>[.<n>].metallib` exists, and `air=`
  hashes the bytes Metal got) together with `--pipelines` (G6) and G3's stage grouping; `--out`'s `<f>[.<n>].metallib`
  naming is the contract; `dis=` (under `--no-metal`) hashes the translator's own module, never `--use`'s bytes. RED:
  a `--use` folder with one changed metallib changes that shader's `air=` and no other." G5 keeps
  `$S/base/<C>.out` and `$S/bin/base-<G1b>/dxil-translate`. Without that, W's gate cannot grade SMITE 2 or the DXBC
  stages on Metal. If 4a does not take the line, W builds its own tool with `$S/tool.sh cxx` from G's
  `dxil-translate.mm` plus today's `--use` hunk.
- **Which `corpus-diff.py` grades W:** W's own copy on `step4w` (today's: ok, fail, summary and `air=`), never G2's.
  W's runs use Metal, so their lines carry no `dis=`.

**Merging.** The controller rebases `refs` onto `air-23` after Task 4 and fast-forwards. A lane branch is deleted only
after `git cherry` shows nothing unmerged. Reviews run on Opus.

## Rules check

- **Statics:** none. The library has no mutable global or function-local `static` and no `cl::opt`. The attribute and
  record tables and P13's `llvm.*` list are `constexpr`. Why: concurrent compiles in one process (P16, P17). Task 2's
  and Task 4's reviews grep for `static` that is not a function or `constexpr`.
- **Config keys:** none. Neither the library nor `llvm-air-write` reads an environment variable. The harness reads
  `W`, `S`, `AIRW`, `BIN23`, `XLATE` and `SRC_<corpus>`; these are dev-only and documented in `air-oracle.sh`'s header,
  with its corpus table. `BIN23` is its own name because `$I23` is the prefix everywhere else.
- **Wire fields:** the C header's structs (`LLVMAIRValueType`, `LLVMAIRStructBody`, `LLVMAIRTypes`,
  `LLVMAIRWriteStats`, which has `RefsFilled` for P16's count) form an in-process ABI that 4b mirrors by hand (4-D2)
  with size and offset asserts. Field order is fixed in Task 2; a change later means a header and binding change in
  the same pin bump.
- **Dependencies:**
  - LLVM components `Core`, `BitstreamReader` (typed refs), `Support` and `TargetParser`, plus `MC` only if
    `StringTableBuilder` survives the copy. `Object`/irsymtab and `DirectXIRPasses` are dropped. Why: the writer needs
    nothing else.
  - `llvm-air-write` (the tool, not `LLVMAIRBitWriter`) also links `Passes` and the transform and analysis components
    it pulls, for `--passes` (P15). Why: the opt23 leg runs P15 in the tool's context; `opt` refuses `air64`.
  - air15 links LLVM 15's `BitReader` set (the dxil-probe list in `dxmt/llvm.sh:56-58`) and `CommonCrypto` (system).
    `dxil-probe-15` keeps that hand list (the LLVM 15 install has no `llvm-config`); `dxil-probe-23` takes
    `$I23/bin/llvm-config --libs bitreader core` and `--system-libs --link-static`, because LLVM 23's Core also needs
    `LLVMTargetParser`.
  - `air-bcinv.py` uses `python3`'s stdlib.
  - No crate, no download.
- **Derives:** none (no Rust).
- **Lint suppressions:** none: no `#pragma clang diagnostic` and no `-Wno-*`. Our files compile warning-free in the
  lane builds and the release build (Task 2 Step 4, Task 8 Step 1).
- **Duplicates:**
  - `AIRBitcodeWriter`/`AIRValueEnumerator` against upstream's `DXILBitcodeWriter`/`DXILValueEnumerator` and
    `ModuleBitcodeWriter` (three writers in one tree; P2).
  - `AIRPointerTypes` against DirectX's `PointerTypeAnalysis` and `DXILPrepare` (another policy: no `i8*` default).
  - Metallib container knowledge: air15 `pack` against DXMTRS `src/airconv/metallib_writer.cpp` (4a moves it to
    `airconv-ir`). Checked by `pack(extract(x)) == x`.
  - Typed disassembly: air15 `dis` against Phase 0's `air-roundtrip dis` and 4a's `dis=` (all `Module::print`;
    checked equal on 96 modules).
  - `dxil-probe`: one source for LLVM 15 and 23.
  - `air-bcinv.py`'s (block, code) allowlist against the writer's `EmitRecord` codes and LLVM 15's
    `LLVMBitCodes.h`. Checked by Task 6 Step 2's RED: an allowlist copy missing one code the writer emits fails,
    naming it.
  - P13's `llvm.*` list against LLVM 15's own intrinsic table, which `air15 verify` consults: the list is what the
    writer allows, the table what LLVM 15 knows. Each list entry's signature is checked against
    `L15/include/llvm/IR/Intrinsics.td` when it is ruled in.
  - P15's pipeline string against the shim's C++ pipeline (`A/airconv_context.cpp:110-151`, kept by 4-D3): re-read
    against the shim at 4b.
  - The typed-type grammar: printed by air15 (LLVM 15's `Type::print`) and parsed by `llvm-air-write` (Task 2).

## Review Focus

1. **A game shader whose post-pass IR holds a pointer with no definition fact.** Examples: InstCombine's `i8` ptradd
   GEPs (`InstructionCombining.cpp:3414`), `inttoptr` from a heap address, a pointer loaded through `i8*`-typed
   memory.
   Also airconv's literal descriptor structs, which after InstCombine become chains of `load ptr` (P5).
   - Expected: a type from P5's fixpoint, anchored in a seed, plus casts; a pointer no seed reaches is `i8` in its
     address space and counted as `UseTyped` (0 on the repo corpora). Never a silent `i8*` for a constrained pointer,
     never an abort.
   - Tests: Task 4's `desc-*.ll`, `gep-i8-ptradd.ll`, `inttoptr-use-typed.ll`, `load-ptr-through-i8.ll`,
     `unconstrained-ptr.ll`; the opt23 leg (Tasks 7 and 8); SMITE 2 in Task 7 and at the gate.
2. **One opaque constant at several pointee types, and pointer phi/select cycles.** The corpus uses
   `ptr addrspace(1) null` at three pointee types.
   - Expected: one typed constant per use type, each under its own `SETTYPE`; the fixpoint ends.
   - Tests: Task 4's `null-three-pointees.ll`, `const-gep-nested.ll`, `phi-cycle.ll`, `select-mixed.ll`; the full
     leg's typed-text identity; the `self` row in every leg.
3. **Struct fields and array elements that are pointers.** This was E2's failure: argument-buffer fields as `i8*`,
   lost address spaces. The corpus has `%argument_buffer_struct = type { i32 addrspace(2)*, …, i32 addrspace(1)* }`.
   - Expected: typed bodies under the original struct name, written once, with address spaces kept; no opaque type
     reaches the type table.
   - Tests: Task 2's `struct-typed-body.ll`; the full leg; P9b's `STRUCT_NAME` rule.
4. **Attributes and flags with no 3.7 encoding.**
   - The cases: `local_unnamed_addr` (the DXIL writer writes a bool, `:1296-1355`); split FMF (it widens
     `reassoc|contract` to UnsafeAlgebra and drops `afn`, `:752-777`, 106 `afn` in the corpus); `speculatable` and
     `nocallback`; `memory`/`captures`; LLVM 23-only flags.
   - Expected: LLVM 15's encodings, sound drops that are counted, or errors.
   - Tests: Task 3's lit rows; the full leg's typed-text identity; `text23n` and `self` in defs and opt23; the release
     build at the gate.
5. **Bad or concurrent input at the C boundary**, since 4b feeds modules derived from guest bytes and D3D11 compiles in
   parallel.
   - The cases: a table type that does not erase to the value's type; a truncated or conflicting typed ref; a module
     the verifier rejects; an opcode or a type the copied switches and maps do not hold, which only an `assert`
     guarded (P17).
   - Expected: error text, no shared state, the same with assertions off.
   - Tests: Task 2's `bad-erasure.ll`, `bad-untyped.ll`, `bad-verify` (the tool's `--self-test-broken-module`, an
     unverifiable module built through the C API); Task 3's `unencodable.ll` on the release build (Task 8 Step 2);
     Task 5's `typed-refs-truncated.ll`, `typed-refs-conflict.ll`; the gate's threaded batch against sequential output
     in three legs.

---

### Task 0: setup, the tool copy, `$L15`, inputs, the release build (owner)

**Files:** none committed. Ledger only. `$W/tools`, `$W/in`, `$W/llvm23-build` (not tracked).

**Interfaces:** Produces:
- `$FW` on `air-23`;
- `$W/tools/{dxil-translate,dxil-probe,key,dxmt-head}`;
- `$L15`;
- `$W/in/<corpus>/*.metallib` and `$W/in/<corpus>.txt` for `shaders`, `dxil`, `dxbc` and `smite2` (P14);
- `$W/in/helpers/{air_msad,air_samplepos,air_tessellation}.air`;
- `$W/llvm23-build`, the integration build of P12.

- [ ] **Step 1: Worktrees.**
  - Run `git -C $F worktree add $FW -b air-23 llvmorg-23.1.4`, then `git -C $M worktree add $W/wt-mn -b step4w
    dxmtrs`.
  - Ledger: `git -C $FW rev-parse HEAD` (7a941dfb2877…), `git -C $M rev-parse dxmtrs`, this plan's path. Commit the
    plan on `dxmtrs`: `Plan: DXMTRS step 4W, the typed-pointer AIR writer`.
- [ ] **Step 2: The one lock job.**
  - Run `/usr/bin/lockf -k $M/build/pipeline.lock sh -c 'set -e; mkdir -p $W/tools; cp build/wine-arm64/dxil-translate
    build/wine-arm64/dxil-probe $W/tools/; cp build/wine-arm64-src/dxmt-translator-key $W/tools/key; git -C
    build/wine-arm64-src/dxmt rev-parse HEAD > $W/tools/dxmt-head; [ -f build/step4/llvm15/.complete ] || [ -d
    build/step4/llvm-project/llvm ] || cp -cR build/dxmt-src/llvm-project build/step4/llvm-project'` from `$M`. The
    source path is 4a's (the cross-plan contract), so whichever plan runs first copies it once.
  - Expected: `cat $W/tools/key` is the Global Constraints key. If it is not, STOP: the C++ translator changed, so the
    controller rules.
- [ ] **Step 3: `$L15`**, only if `$L15/.complete` is not `llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto`.
  - Run, in the background:
    `/usr/bin/lockf -k $M/build/step4/llvm15.lock sh -c 'ROOT=$M; die(){ echo "$*" >&2; exit 1; }; . $M/dxmt/pins;
    . $M/dxmt/llvm.sh; [ "$(cat $M/build/step4/llvm15/.complete 2>/dev/null)" = "llvmorg-15.0.7+fdivfmf+-mcpu=apple-m5+nolto" ]
    || UNIX_CPU=-mcpu=apple-m5 UNIX_LTO= build_llvm arm64 $M/build/step4/llvm15 $M/build/step4/llvm-project'`.
    4a's G5 Step 1 runs the same call under the same lock.
  - The copy holds `llvm/`, so nothing is cloned, and `fix_llvm` finds it already fixed.
  - Expected: that stamp, and `$L15/lib/libLLVMCore.a` exists.
- [ ] **Step 4: The integration build, at the base.**
  - Configure: `cmake -G Ninja -S $FW/llvm -B $W/llvm23-build -DCMAKE_INSTALL_PREFIX=$W/llvm23
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DLLVM_HOST_TRIPLE=arm64-apple-darwin
    -DLLVM_TARGETS_TO_BUILD="" -DLLVM_ENABLE_ASSERTIONS=Off -DLLVM_ENABLE_ZSTD=Off -DLLVM_BUILD_TOOLS=On
    -DLLVM_INCLUDE_TESTS=On -DLLVM_ENABLE_LTO=Off -DLLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS=-mcpu=apple-m5 -DCMAKE_CXX_FLAGS=-mcpu=apple-m5`. This is "Task 0
    Step 4's cmake line" that the lane builds reuse with their own `-S`, `-B` and `-DLLVM_ENABLE_ASSERTIONS=On` (P12).
  - Then run `ninja -C $W/llvm23-build` (about 4 minutes, P12).
  - Expected: `bin/llvm-dis --version` prints 23.1.4; `bin/llvm-lit` exists; `CMakeCache.txt` has empty
    `LLVM_EXPERIMENTAL_TARGETS_TO_BUILD`.
- [ ] **Step 5: Inputs.** For `c` in `shaders`, `dxil` and `dxbc`, run
  `$W/tools/dxil-translate $M/dxmt/tests/$c --out $W/in/$c > $W/in/$c.txt`. Then SMITE 2 (P14), read in place:
  `$W/tools/dxil-translate "$HOME/Library/Application Support/MacNeutron/vm.0.7.2/runs/2437170/traces/dxmt" --out
  $W/in/smite2 > $W/in/smite2.txt` (plain mode: each shader with its own resources; about 4-5 minutes).
  - Expected: `summary 64/64 ok` (96 metallibs), `33/33`, `2/2`; for `smite2` a summary over 10,271 files, ledgered
    with its fail reasons (the dev baseline).
- [ ] **Step 6: Helpers.** For each of the three `src/airconv/shaders/*.metal`:
  - `git -C $D show main:src/airconv/shaders/<f>` into a folder under `$W/in/helpers-src`.
  - Compile with `xcrun-metal.sh`'s form and `A/meson.build:81`'s arguments: `/usr/bin/xcrun -sdk macosx metal -x metal
    -I <dir> -Xclang -main-file-name -Xclang <f> -o $W/in/helpers/<name>.air -c - -std=metal3.1
    --target=air64-apple-macos14.0 < <dir>/<f>`.
  - Expected: three `.air` files; `$I23/bin/llvm-bcanalyzer -dump` exits 0 on each and shows `<POINTER` records
    (Apple writes typed pointers).

### Task 1: air15, air-tools.sh, dxil-probe for both LLVMs (lane `mn`)

**Files (MacNeutron `step4w`):**
- Create `dxmt/tools/air15.cpp` and `dxmt/tools/air-tools.sh`.
- Modify `dxmt/tools/dxil-probe.cpp:59`: `setOpaquePointers(false)` only `#if LLVM_VERSION_MAJOR < 17`, through
  `llvm/Config/llvm-config.h`.
- Modify `dxmt/tools/dxil-probe.cpp:73`: `startswith("dx.op.")` becomes `substr(0, 6) == "dx.op."`, which reads the
  same on both LLVMs.

**Interfaces:** Produces, with every subcommand exiting 0 on success and 1 with a reason on stderr:
- `air15 extract <in.metallib> <out.bc>`: the bytes at the header's BitcodeOffset and BitcodeSize.
- `air15 dis <in.metallib|in.bc> <out|->`: LLVM 15 typed `Module::print`, context `setOpaquePointers(false)`, an empty
  module identifier, no ModuleID line.
- `air15 types <in.metallib|in.bc> <out|->`: the types file, in this grammar. A type is printed by LLVM 15's
  `Type::print`; a name by `Module::print`'s identifier form (`@x`, `@"…\XX…"`, `%x`). The line kinds:
  - `s <%struct> <typed literal body>`: every identified struct with a pointer inside.
  - `g <@global> <typed value type>`: globals whose value type has a pointer.
  - `f <@function> <typed function type>`: every function whose type has a pointer.
  - `c <@function> <n>`: instruction count, for every function with a body.
  - `i <@function> <k> <typed type>`: the k-th instruction (0-based, function order), only when its type has a
    pointer.

  Line order: `s`, `g`, `f`, then for each function its `c` line and then its `i` lines.
- `air15 verify <in>`: prints `ok`, or `fail <reason>`, after a typed parse, `verifyModule` and two checks LLVM 15
  leaves out:
  - each `cmpxchg` and `atomicrmw` pointer operand points to its value type. LLVM 15's reader takes the comparand and
    value with `getValueTypePair` and checks no pointee (`L15/lib/Bitcode/Reader/BitcodeReader.cpp:6000-6022,
    6067-6090`), its verifier checks the pointee for store but not for these (`L15/lib/IR/Verifier.cpp:3852-3857`
    against `:3934-3960`), and `$L15` has no assertions, so `cmpxchg i8 addrspace(1)* %p, i32 0, i32 1` otherwise
    passes;
  - every `llvm.*` declaration is an intrinsic LLVM 15 knows (`getIntrinsicID() != 0`); LLVM 15 reads an unknown one
    as a plain function.
- `air15 pack <orig.metallib> <new.bc> <out.metallib>`: orig's container with the bitcode replaced. It updates the
  header's FileSize and BitcodeSize, and each function's `HASH` (SHA-256 of the new bitcode, `CC_SHA256`) and `MDSZ`
  (`metallib_writer.cpp:20-70, 365-410`).
- `air-tools.sh <llvm15-prefix> <llvm23-prefix|-> <out-dir>`: builds `air15` (C++17, `-fno-rtti`, against
  `<llvm15>`) and `dxil-probe-15` and `dxil-probe-23` (the latter when the second prefix is given).
  - `air15` and `dxil-probe-15` link the way `build_probe` does (`dxmt/llvm.sh:54-59`): its hand list, since the
    LLVM 15 install has no `llvm-config`.
  - `dxil-probe-23` links `$2/bin/llvm-config --libs bitreader core` and `$2/bin/llvm-config --system-libs
    --link-static`. The 15 list fails there: LLVM 23's Core also needs `LLVMTargetParser` (`--libs` prints
    `-lLLVMBitReader -lLLVMCore -lLLVMRemarks -lLLVMBinaryFormat -lLLVMTargetParser -lLLVMBitstreamReader
    -lLLVMSupport -lLLVMDemangle`).

- [ ] **Step 1: Failing checks.**
  - Run `sh dxmt/tools/air-tools.sh $L15 $I23 $W/bin`. Expected: it fails, because no source exists.
  - Write `$W/t1-check.sh` (uncommitted) with these checks:
    - (a) for each of the 96 `$P0/air-15/*.metallib`, `cmp` of `air15 dis` with `$P0/air-roundtrip-15t dis`;
    - (b) `air15 pack x <(air15 extract x)` then `cmp` with `x`;
    - (c) `air15 verify` gives `ok`; a copy of one extracted `.bc` truncated to half its size gives `fail`;
    - (d) in `$P0/air-15/cache.cs.dxil.metallib`'s types file, the `s %argument_buffer_struct` body equals the
      `%argument_buffer_struct = type …` line of its dis text, and `c @main <n>` equals the instruction count of
      `@main` in that text;
    - (e) `dxil-probe-15` output is byte-equal to `$W/tools/dxil-probe`'s on `$M/dxmt/tests/shaders/*.dxil`,
      `dxmt/tests/dxil/*.dxil` and `dxmt/tests/dxil/reject/*.dxil`.
  - Expected: every check fails.
- [ ] **Step 2: Implement** `air15.cpp`, `air-tools.sh` and the two dxil-probe edits.
- [ ] **Step 3: Checks pass.**
  - Expected: (a) 96/96 equal, (b) 96/96 identical, (c) 96 `ok` and the truncated copy `fail`, (d) equal, (e)
    byte-equal.
  - `$W/bin/dxil-probe-23` exists and, run with no argument, prints its usage. Its outputs are first compared in
    Task 6's probe leg.
- [ ] **Step 4: Commit** `dxmt tools: air15 (typed LLVM 15 extract, dis, types, verify, pack); dxil-probe builds on
  LLVM 15 and 23`.

### Task 2: the component, the C entry point, typed types, llvm-air-write (lane `fork`)

**Files (fork `air-23-w`):**
- Create:
  - `llvm/include/llvm-c/AIRWriter.h`;
  - `llvm/lib/Bitcode/AIRWriter/CMakeLists.txt` (`add_llvm_component_library(LLVMAIRBitWriter …)`);
  - `AIRBitcodeWriter.{h,cpp}` and `AIRValueEnumerator.{h,cpp}`, copied from `lib/Target/DirectX/DXILWriter/` at
    7a941df, namespace `llvm::air`, with 02e8d34's two writer hunks; there is no `DXILDebugInfo` (AIR carries no
    debug info, and a module with any is an error);
  - `AIRTypes.{h,cpp}`: building the table, the erasure check, shadow identified structs written under the
    original name;
  - `AIRTypedRefs.{h,cpp}`, a stub that Task 5 replaces;
  - `AIRWriterC.cpp`;
  - `llvm/tools/llvm-air-write/{CMakeLists.txt,llvm-air-write.cpp}` (`LLVM_LINK_COMPONENTS` adds `Passes` for
    `--passes`; the library does not);
  - `llvm/test/Bitcode/AIRWriter/{typed-pointers,struct-typed-body,bad-erasure,bad-untyped,bad-verify}.ll` and
    `Inputs/*.types`.
- Modify, each with a `Mac-Neutron:` comment line:
  - `llvm/lib/Bitcode/CMakeLists.txt` (`add_subdirectory(AIRWriter)`);
  - `llvm/test/CMakeLists.txt` (`llvm-air-write` in `LLVM_TEST_DEPENDS`);
  - `llvm/test/lit.cfg.py` (`llvm-air-write` in the tools list).
- Uncommitted: the lane build `$W/build-fork` (P12).

**Interfaces:** Produces, exactly, in `llvm-c/AIRWriter.h`:
```c
LLVMTypeRef LLVMAIRTypedPointerType(LLVMTypeRef Pointee, unsigned AddressSpace);
typedef struct { LLVMValueRef Value; LLVMTypeRef Type; } LLVMAIRValueType;   /* function: typed function type;
  global: typed value type; argument or instruction: the value's typed type */
typedef struct { LLVMTypeRef Struct; LLVMTypeRef Body; } LLVMAIRStructBody;  /* identified struct -> typed literal body */
typedef struct {
  const LLVMAIRValueType *Values; size_t NumValues;
  const LLVMAIRStructBody *Structs; size_t NumStructs;
  const LLVMMemoryBufferRef *TypedRefs; size_t NumTypedRefs;  /* typed bitcode typing same-named functions, globals, structs */
} LLVMAIRTypes;
typedef struct {
  uint32_t InstCasts, ConstCasts, UseTyped, AttrsDropped, FlagsDropped, RefsFilled;
} LLVMAIRWriteStats;
/* Writes M as typed-pointer AIR bitcode (Darwin wrapper included) into *OutBuf. M is verified first and modified
   (pointer casts); it must not be written again. 0 on success; 1 with *ErrorMessage (LLVMDisposeMessage) otherwise.
   Never aborts. Stats may be null. */
LLVMBool LLVMWriteAIRBitcode(LLVMModuleRef M, const LLVMAIRTypes *Types, LLVMMemoryBufferRef *OutBuf,
                             LLVMAIRWriteStats *Stats, char **ErrorMessage);
```
- C++ (not installed), `AIRBitcodeWriter.h`: `Error writeAIRBitcode(Module &M, const AIRTypeTable &T, raw_ostream &OS,
  AIRWriteReport *R)`. `AIRWriteReport` holds the stats plus one line per cast, value typed by P5's fixpoint,
  use-typed value and dropped attribute or flag, for `--explain`.
- `AIRTypedRefs.h`: `Expected<AIRRefTable> readTypedRefs(LLVMContext &C, ArrayRef<MemoryBufferRef> Refs)`.
  `AIRRefTable` holds `StringMap<FunctionType *> Functions`, `StringMap<Type *> Globals` and
  `StringMap<StructType *> Bodies`, all with typed types. The Task 2 stub returns an empty table when `Refs` is empty
  and an error otherwise.
- `llvm-air-write <in.bc|in.ll> --types <file> [--named-only] [--ref <typed.bc>]… [--passes <pipeline>]
  [--emit-ll <stem>] [--explain] -o <out.bc>`:
  - `--named-only` reads only the `s`, `g` and `f` lines.
  - It checks each function's `c` count before using that function's `i` lines; on a mismatch it drops them and
    prints `positional mismatch @<fn>`.
  - `--passes` runs that pipeline with `PassBuilder` and no TargetMachine on the module it read, in the same
    context, before building the table (P15). Entries whose value is gone, or whose erasure no longer matches, are
    dropped and printed as `stale <name>`.
  - `--emit-ll <stem>` prints `<stem>.pre.ll` (the module the writer gets, after `--passes`) and `<stem>.post.ll` (the
    module as the writer left it: casts inserted, drops applied).
  - It also takes `--batch <list> -j <n>`: list lines are `<in> <types> <out>`, the other options apply to every
    job, and each job gets its own `LLVMContext`.
  - `--self-test-broken-module` (this task) and `--self-test-late-type` (added by Task 3, with P17's conversions)
    build, through the C API and the C++ entry, a module the verifier rejects and a typed type first made after
    enumeration, for the lit tests.
  - On stderr it prints `stats inst-casts=… const-casts=… use-typed=… attrs-dropped=… flags-dropped=… refs=…
    stale=…`, or `error: <message>` with exit 1.

  It always goes through `LLVMWriteAIRBitcode`.
- The tool's typed-syntax parser is a deliverable of this task, because LLVM 23's `LLParser` refuses `T*`. It reads
  leaf types, `[N x T]`, `<N x T>`, `{…}`, `<{…}>`, `%name`, the `*` and `addrspace(N)*` suffixes, and function types
  with `...`. A `%name` resolves with `StructType::getTypeByName(Ctx, name)` in the opaque module; an unknown name is
  an error.

In this task the writer types values from the table only: a pointer-bearing value with no entry is an error (Task 4
adds derivation). Record encodings stay the DXIL writer's (Task 3 changes them).

- [ ] **Step 1: Failing tests.**
  - `typed-pointers.ll`: a kernel with `ptr addrspace(1)` and `ptr addrspace(3)` arguments typed `float
    addrspace(1)*` and `i32 addrspace(3)*`. The CHECK lines are two `<POINTER` records with address-space operands 1
    and 3, plus `CHECK-NOT: OPAQUE_POINTER`.
  - `struct-typed-body.ll`: `%argument_buffer_struct = type { ptr addrspace(2), ptr addrspace(1) }` with its `s`
    line. Its CHECKs are `STRUCT_NAME` `argument_buffer_struct` and `STRUCT_NAMED` with two pointer element ids,
    exactly one `argument_buffer_struct` name (a `CHECK-NOT` for a second after the first), and
    `CHECK-NOT: dxilOpaquePtrReservedName`.
  - `bad-erasure.ll`: an `f` line whose type erases to another function type → `not llvm-air-write … 2>&1 |
    FileCheck` for `does not erase to`.
  - `bad-untyped.ll`: a pointer argument with no entry → an error naming `@k` and the argument.
  - `bad-verify`: `llvm-air-write --self-test-broken-module` builds, through the C API, a block with no terminator.
    The CHECK is `error: ` and the verifier's text, exit 1.
  - Configure the lane build once: Task 0 Step 4's cmake line with `-S $W/wt-fork/llvm -B $W/build-fork
    -DLLVM_ENABLE_ASSERTIONS=On`, then `ninja -C $W/build-fork`. Run `$W/build-fork/bin/llvm-lit -v
    $W/wt-fork/llvm/test/Bitcode/AIRWriter`. Expected: FAIL, because the tool does not exist.
- [ ] **Step 2: Implement.** Copy, rename and wire `AIRTypes`, the C entry point and the tool.
  - The writer's `getTypeID(T, V)` looks every type up in the typed map (not only pointers and constants), never in
    an `i8*` default.
  - The enumerator enumerates only typed types: each value's typed type, typed GEP source and alloca types, typed
    parameters, typed struct bodies. Opaque identified structs are mapped to their shadows before enumeration, so
    each name is written once.
  - It enumerates in module and instruction order and iterates nothing keyed by pointer (a `MapVector` where
    iteration is needed). The copy iterates `PointerMap`, a `DenseMap<const Value *, Type *>`
    (`DW/DXILBitcodeWriter.cpp:147-148`, `L23/lib/Target/DirectX/DirectXIRPasses/PointerTypeAnalysis.h:26`), so its
    type table, bytes and metallib `HASH` follow allocation addresses.
  - The `PointerTyID` case returns the error `type table: opaque pointer`.
- [ ] **Step 3: Pass.** Run `ninja -C $W/build-fork && $W/build-fork/bin/llvm-lit -v
  $W/wt-fork/llvm/test/Bitcode/AIRWriter`. Expected: all PASS; `nm -gU $W/build-fork/lib/libLLVMAIRBitWriter.a`
  lists `_LLVMWriteAIRBitcode` and `_LLVMAIRTypedPointerType`.
- [ ] **Step 4: Clean compile.** Run `/usr/bin/grep -c 'warning:'` over the build log for our files. Expected: 0.
- [ ] **Step 5: Commit** `AIR writer: a typed-pointer bitcode writer from the DXIL writer, its C entry point and
  llvm-air-write`. The controller fast-forwards `air-23`.

### Task 3: LLVM 15's encodings (lane `fork`)

**Files:**
- Modify `llvm/lib/Bitcode/AIRWriter/AIRBitcodeWriter.cpp` and `AIRValueEnumerator.{h,cpp}` (P17's asserts become
  errors), and `llvm/tools/llvm-air-write/llvm-air-write.cpp` (`--self-test-late-type`).
- Create tests `attrs-15.ll`, `attrs-memory.ll`, `attrs-captures.ll`, `attrs-23-only.ll`, `fmf.ll`,
  `flags-23-only.ll`, `poison-freeze-fneg.ll`, `unnamed-addr.ll`, `calls-atomics.ll`, `ident-hash-wrapper.ll` and
  `unencodable.ll`.
- Create `$W/codes-15-23.txt` (uncommitted): every record code, attribute kind, FMF bit and per-instruction flag in
  `L23/include/llvm/Bitcode/LLVMBitCodes.h` that `L15/` lacks or numbers differently.

**Interfaces:** Consumes Task 2's writer. Produces these encodings. Each row is LLVM 15's, from
`L15/lib/Bitcode/Writer/BitcodeWriter.cpp` and `L15/include/llvm/Bitcode/LLVMBitCodes.h`. Every drop and weaker form
is applied to the module before the write (P8), so `--emit-ll`'s `.post.ll` is what was written.

| Row | Encoding | Test |
|---|---|---|
| Attribute kinds | 15's table, codes ≤ 84 (`nocapture` 11, `readnone` 20, `readonly` 21, `argmemonly` 45, `inaccessiblemem*` 49/50, `writeonly` 52, `speculatable` 53, `willreturn` 61, `nofree` 62, `nosync` 63, `noundef` 68, `mustprogress` 70, `nocallback` 71); integer and string attributes as 15; type attributes are an error | `attrs-15.ll` |
| `memory(...)` | P8's mapping | `attrs-memory.ll` (each exact form, and one weaker form counted) |
| `captures(...)` | `none` becomes 11; others dropped and counted | `attrs-captures.ll` |
| 23-only kinds | the guarantee-only drop list written into the test from `codes-15-23.txt`, each with its reason (e.g. `range`, `nofpclass`, `initializes`, `dead_on_unwind`, `writable`); any other kind is an error | `attrs-23-only.ll` |
| FMF | 15's FastMathMap bits (`LLVMBitCodes.h:476-484`, `getOptimizationFlags` `:1489`) on binop, unop, fcmp, call (`CALL_FMF`), select and phi (`:2795, 2808, 2846, 2876, 3057, 3182-3188`); `afn` and split `reassoc`/`contract` kept | `fmf.ll` |
| Instruction flags | `nuw`, `nsw`, `exact` and `inbounds` as 15; `nneg`, trunc `nuw`/`nsw`, `disjoint`, `samesign`, GEP `nusw`/`nuw`/`inrange` dropped and counted | `flags-23-only.ll` |
| Constants and new instructions | `CST_CODE_POISON` 26; `FUNC_CODE_INST_FREEZE` 58 (`:3218`); `FUNC_CODE_INST_UNOP` 56 for `fneg` | `poison-freeze-fneg.ll` |
| `unnamed_addr` | 0/1/2 (`local_unnamed_addr`) for functions, globals and aliases, replacing the DXIL writer's bool (`DXILBitcodeWriter.cpp:1296-1355`) | `unnamed-addr.ll` |
| Calls and atomics | the tail, musttail and notail markers; the explicit-type flag; ordering and syncscope, as 15; `atomicrmw` as code 59 with its value's type (`valty`), as 15 writes it. The copy writes 59 with code 38's layout, the value with no type (`DW/DXILBitcodeWriter.cpp:2543-2546`), which LLVM 15 misparses when the value is a forward reference (`L15/…/BitcodeReader.cpp:6067-6090`) | `calls-atomics.ll` (an `atomicrmw` whose value is a forward reference) |
| Module framing | IDENTIFICATION (`LLVM15.0.7`, epoch 0, `:4411`); `MODULE_CODE_SOURCE_FILENAME` (16) as 15 writes it (P7); `MODULE_CODE_HASH` (`:4435-4446`); the Darwin wrapper and trailer (`emitDarwinBCHeaderAndTrailer`, `:4533`) | `ident-hash-wrapper.ll` (`<SOURCE_FILENAME` with `airconv_generated.metal`) |
| Unencodable | scalable vectors, `callbr`, `ptrtoaddr`, debug-info metadata, and any opcode, type id or constant kind outside the copied switches: an error naming the construct (P17). The `default:` arm errors on anything that is not a `BinaryOperator` | `unencodable.ll` (also a typed type first made after enumeration, through `--self-test-late-type`) |

- [ ] **Step 1: Failing tests** for every row: CHECKs on `llvm-bcanalyzer -dump`, and on the stats line for drops.
  Expected: FAIL (the DXIL writer's encodings, or `llvm_unreachable` or an `assert` under the lane build's
  assertions).
- [ ] **Step 2: Implement.** Also list, in the commit message, the copied `llvm_unreachable`s and `assert`s that
  stay, each over an enum the verifier constrains.
- [ ] **Step 3: Pass.** Expected: the lane lit (`$W/build-fork/bin/llvm-lit -v $W/wt-fork/llvm/test/Bitcode/AIRWriter`)
  all PASS.
- [ ] **Step 4: Commit** `AIR writer: LLVM 15's attribute, flag and record encodings`. The controller fast-forwards
  `air-23`.

### Task 4: pointee derivation and casts (lane `fork`)

**Files:**
- Create `llvm/lib/Bitcode/AIRWriter/AIRPointerTypes.{h,cpp}`: P5's derivation, each use's required type, and cast
  insertion. It is built from `PointerTypeAnalysis.cpp` and `DXILPrepare.cpp:72-190` at 7a941df, without their `i8*`
  defaults.
- Modify `AIRValueEnumerator.{h,cpp}` (typed constant instances per (constant, typed type), recursing into constant
  operands, P6; the constants sorted by typed type) and `AIRBitcodeWriter.cpp` (operands written through the typed-use
  map; `writeConstants`' `SETTYPE` keyed on the typed type, P6; the `llvm.*` list, P13).
- Create tests `derive-defs.ll`, `cast-load-store.ll`, `cast-call-arg.ll`, `cast-atomics.ll`, `call-type-mismatch.ll`,
  `const-cast.ll`, `const-gep-nested.ll`, `const-inttoptr.ll`, `null-three-pointees.ll`, `phi-cycle.ll`,
  `select-mixed.ll`, `gep-i8-ptradd.ll`, `inttoptr-use-typed.ll`, `load-ptr-through-i8.ll`, `unconstrained-ptr.ll`,
  `desc-field0.ll`, `desc-fields12.ll`, `desc-sampler.ll`, `desc-dynamic.ll`, `cross-as-error.ll`,
  `llvm-intrinsic-ptr-error.ll` and `llvm-intrinsic-unlisted.ll`.

**Interfaces:** Consumes the table (Task 2) and `readTypedRefs` (Task 5's file; the stub until it merges). Produces:
- `Expected<AIRPointerTypes> AIRPointerTypes::run(Module &M, const AIRTypeTable &T, AIRWriteReport &R)`. It runs
  P5's fixpoint, inserts instruction casts and records the typed type of every value and each use's required typed
  type. Its worklist and every map it iterates follow module and instruction order (a `MapVector` where iteration is
  needed), never pointer order, so the output bytes are the same in every process.
- The writer asks it `typedTypeOf(const Value *)` and `typedOperand(const Use &)`, which returns the operand or its
  synthetic typed constant.

Each use's required type (every `Use`, constant operands included, P6):
- load and store pointer operands: a pointer to the accessed type (for a stored pointer value, its typed type);
- `cmpxchg` and `atomicrmw` pointer operands: a pointer to the value type;
- the GEP base: a pointer to the typed source element type (P5's literal-aggregate rule);
- the callee operand: a pointer to the call's typed function type. When the call's function type differs from the
  callee's own (opaque IR allows it; `getOrInsertFunction` at another type, critic row 7), that is a constant bitcast
  of the function, because LLVM 15 requires the explicit call type to equal the callee's pointee
  (`L15/lib/Bitcode/Reader/BitcodeReader.cpp:5471-5472`, `L15/lib/IR/Verifier.cpp:3158`). The call's typed function
  type is then built from the arguments' typed types and the result's uses (P5);
- call arguments: the call's typed parameters (from that type, never the callee's own when they differ);
- `ret`: the function's typed return;
- a pointer `icmp`, and select and phi operands: the type P5's fixpoint gave the result;
- constant-expression operands, constant aggregate elements and global initializers: as the instruction operand of
  the same kind, through a synthetic instance;
- a value inside metadata: its own typed type, with no cast.

- [ ] **Step 1: Failing tests.**
  - `derive-defs.ll`: alloca, GEP into the typed body, load of a pointer field, the callee's return, addrspacecast,
    extractvalue. Run with `--named-only` types, so `UseTyped` is 0 in the stats line.
  - `cast-load-store.ll`: a `load i32` through a `float addrspace(3)*` GEP → one `INST_CAST` with opcode bitcast,
    `inst-casts=1`.
  - `cast-call-arg.ll`: the argument cast to the callee's typed parameter.
  - `cast-atomics.ll`: an `i8` GEP feeding `cmpxchg i32` and `atomicrmw add i32` → one cast each, `inst-casts=2`.
  - `call-type-mismatch.ll`: a call whose function type differs from its callee's → a constant bitcast of the callee
    to the call's typed function type, and arguments typed from that type.
  - `const-cast.ll`: `@g` (typed `[4 x float] addrspace(2)*`) passed where `i8 addrspace(2)*` is needed → a
    `CE_CAST` constant record, `const-casts=1`.
  - `const-gep-nested.ll`: `getelementptr inbounds (i8, ptr addrspace(2) @sample_pos_2, i64 1)` over a `[2 x i8]`
    global → a `CE_CAST` nested inside the constant GEP.
  - `const-inttoptr.ll`: `load i32, ptr addrspace(1) inttoptr (i64 4 to ptr addrspace(1))`, the corpus's form → the
    constant typed `i32 addrspace(1)*`.
  - `null-three-pointees.ll`: one `ptr addrspace(1) null` used as `i32*`, `float*` and `i64*` → three `SETTYPE`
    records with distinct type ids, each followed by a `NULL`, and three `POINTER` records.
  - `phi-cycle.ll`: a loop-carried pointer phi; it terminates, and its typed type is its entry operand's.
  - `select-mixed.ll`: two operands of different pointee types → one cast.
  - `gep-i8-ptradd.ll`: LLVM 23's `getelementptr inbounds i8, ptr %p, i64 16` from a `float addrspace(1)*`, then
    `load float` → the base cast to `i8*` and the result cast to `float*`.
  - `inttoptr-use-typed.ll`: `inttoptr` used by `load float` → typed `float addrspace(1)*` through the fixpoint,
    listed by `--explain`, `use-typed=0`.
  - `load-ptr-through-i8.ll`: a pointer loaded through an `i8*` slot, then used by `load i32` → typed from that use,
    the slot's cast reported, `use-typed=0`.
  - `unconstrained-ptr.ll`: an `inttoptr` only compared with `null` → typed `i8` in its address space, `use-typed=1`.
  - `desc-field0.ll`, `desc-fields12.ll`, `desc-sampler.ll`, `desc-dynamic.ll`: copied from `lod.ps.dxil`'s typed
    text (`$P0/air-roundtrip-15t dis`, made opaque): a descriptor's field 0 (the handle), fields 1 and 2 (the `i64`s),
    the sampler pair, and a runtime `IdxDescriptor` index. Each has two RUN lines, as is and with `--passes
    instcombine`. Expected in both: no cast on any handle passed to an `air.*` call, the typed GEP source (or, after
    instcombine, the typed load chain) checked in `llvm-bcanalyzer` output, `use-typed=0`.
  - `cross-as-error.ll`: a use that needs another address space → the error `address space`.
  - `llvm-intrinsic-ptr-error.ll`: `declare void @llvm.memcpy.p0.p0.i64(...)` called → the error naming it (P13).
  - `llvm-intrinsic-unlisted.ll`: `declare i32 @llvm.scmp.i32.i32(i32, i32)` called (no pointer parameter) → the
    error naming it (P13).
  - Expected: FAIL.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Pass.** Expected: the lane lit all PASS, including Tasks 2 and 3's tests.
- [ ] **Step 4: Commit** `AIR writer: pointee types from definitions, casts where uses differ`. The controller
  fast-forwards `air-23`, then rebases `refs`.

### Task 5: typed refs (lane `refs`)

**Files:** Replace `llvm/lib/Bitcode/AIRWriter/AIRTypedRefs.{h,cpp}`. Create tests `typed-refs.ll`,
`typed-refs-conflict.ll` and `typed-refs-truncated.ll`, plus `Inputs/helper-ref.metal` and `Inputs/helper-ref.air`.
The `.metal` file states its compile command: Task 0 Step 6's form, with a `threadgroup int *` function using
`atomic_fetch_add_explicit` as `air_tessellation.metal:152-153` does.

**Interfaces:** Produces Task 2's `readTypedRefs`.
- It scans with `BitstreamCursor`: `isBitcodeWrapper` and `SkipBitcodeWrapperHeader`, `IDENTIFICATION`, `MODULE`,
  `TYPE_BLOCK_ID_NEW` (every type code LLVM 15 and Apple write, `POINTER` included), and `MODULE_CODE_FUNCTION` and
  `MODULE_CODE_GLOBALVAR`. Names come from STRTAB (version 2) or the module VST (version 1).
- Typed types are built in `C` with `TypedPointerType`.
- Unknown records are skipped. Malformed input is an error, `typed ref <n>: <why>`.
- The table builder fills only names that have no explicit entry. A disagreement is an error (P16).

- [ ] **Step 1: Failing tests.**
  - `typed-refs.ll`: an opaque module that calls the helper's `air.atomic.local.add…` and declares it with no `f`
    line, plus `--ref %S/Inputs/helper-ref.air`. The CHECK is that its `POINTER` record has address space 3 to `i32`,
    with `use-typed=0`.
  - `typed-refs-conflict.ll`: an explicit `f` line that disagrees with the ref → error.
  - `typed-refs-truncated.ll`: a `head -c` copy of the ref → error, no crash.
  - Expected: FAIL (the stub's error).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Pass.** The lane build `$W/build-refs` is configured once as Task 2 Step 1's, with `-S $W/wt-refs/llvm
  -B $W/build-refs`. Run `ninja -C $W/build-refs && $W/build-refs/bin/llvm-lit -v
  $W/wt-refs/llvm/test/Bitcode/AIRWriter`. Expected: all PASS.
- [ ] **Step 4: Commit** `AIR writer: typed references (functions, globals and structs from typed bitcode)`.

### Task 6: the harness (lane `mn`)

**Files (MacNeutron `step4w`):** Create `dxmt/tools/air-oracle.sh` and `dxmt/tools/air-bcinv.py`.

**Interfaces:** `sh dxmt/tools/air-oracle.sh <leg> <corpus>` and `sh dxmt/tools/air-oracle.sh check <out-dir>
<corpus>`. Environment:
- `W` (default `$M/build/step4/w`) and `S` (default `$M/build/step4`);
- `AIRW` (default `$W/llvm23-build/bin/llvm-air-write`, the integration build of P12);
- `BIN23` (the LLVM 23 bin folder, default `$I23/bin`; `$I23` itself stays the prefix);
- `XLATE` (default `$W/tools/dxil-translate`);
- `SRC_<corpus>`: the shader folder, for `--use`.

**Corpora**, a table in `air-oracle.sh`'s header (4a's ids, 4a's `Corpora` table):

| W name | 4a id | Inputs: development, gate | `XLATE` arguments |
|---|---|---|---|
| `shaders` | C1 | `$W/in/shaders`, `$S/base/C1.out` | — |
| `dxil` | C2 | `$W/in/dxil`, `$S/base/C2.out` | — |
| `dxbc`, `dxbc-320` | C4, C4-320 | `$W/in/dxbc` (310 only), `$S/base/C4.out`, `$S/base/C4-320.out` | `--metal 320` for `-320` |
| `d3d11`, `d3d11-320` | C5, C5-320 | `$S/base/C5.out`, `$S/base/C5-320.out` (from G3) | `--metal 320` for `-320` |
| `dx11`, `dx11-320` | C6, C6-320 | `$S/base/C6.out`, `$S/base/C6-320.out` (G4's `dxmt/tests/dxbc-corpus`) | `--metal 320` for `-320` |
| `smite2` | C8 | `$W/in/smite2` (plain `--out`), `$S/base/C8.out` (G6) | at the gate `--pipelines <capture>/pipelines.txt` |
| `helpers` | — | `$W/in/helpers` | no Metal row |

C3 and C7 make no metallibs, so W has nothing to write for them. W grades with its own `corpus-diff.py` on `step4w`,
never G2's (cross-plan interfaces).

| Leg | What it does | Output |
|---|---|---|
| `prep` | for each input metallib: `air15 extract`, `dis` (`.ll15`), `types` (`.types`); `$BIN23/llvm-dis` of the `.bc`, line 1 dropped (`.ll23`) | `$W/t/<corpus>/` |
| `full` | `llvm-air-write --types --emit-ll` → `air15 pack` → `.ll15`, `.ll23`, `self`, `verify`, inventory, wrapper → `XLATE <SRC> <args> --use …`, ok set and fail reasons compared with `$W/in/<corpus>.txt` (W's `corpus-diff.py`, plus a sorted diff of the `fail` lines with their reasons), with the `used` and `air=` checks below | `$W/out/full/<corpus>/` |
| `defs` | the same with `--named-only --ref $W/in/helpers/*.air`, and with the `f`, `g` and `s` lines of every name the helpers define or declare (from `air15 types` over `$W/in/helpers/*.air`) removed from the `.types` copy, so the refs alone type them as at 4b; adds `text23n`. A second writer run per module, with the full `f`/`g`/`s` lines and the refs and its output discarded, must exit 0 (P16's conflict check) | `$W/out/defs/<corpus>/` |
| `opt23` | as `defs`, with `--passes '<P15>'` in the tool (P15); `stale` entries counted, each one ledgered | `$W/out/opt23/<corpus>/` |
| `probe` | `dxil-probe-15` against `dxil-probe-23` on `SRC`'s `.dxil` files: the same ok or fail per file and the same entry and op columns; each helper `.air` through `$BIN23/llvm-dis` | |
| `det` | `full`, `defs` and `opt23`, each twice in separate processes and as `--batch -j 8` against `-j 1`, on `shaders`, `dxil` and a fixed SMITE 2 slice (the first 500 sorted names); compares only `*.bc`, `*.metallib`, `*.ll15`, `*.ll23` and the stats lines without times (`dxil-translate` prints `%.1f` ms, `dxil-translate.mm:460,465,475`) | |
| `check <out-dir> <corpus>` | grades metallibs another producer wrote under the corpus's names (4b's translator): verify, inventory (the C++ baseline's module of the same name as reference), wrapper, Metal ok set and reasons against the C++ baseline with the `used` and `air=` checks; the writer's stats where the producer logs them, informational | |

- **The Metal row reads only our bytes.** `dxil-translate --use` substitutes a file only where it exists and otherwise
  hands Metal the translator's own bytes (`dxil-translate.mm:12-14,176-190`), so a missing file, a naming slip or a
  wrong folder would pass on the C++ AIR. Before the run, the number of metallibs in the `--use` folder equals the
  number in the input folder. After it, each `ok` line's `air=` equals FNV-1a 64 over our files for that shader in
  translation order (`<f>.metallib`, `<f>.1.metallib`, …), recomputed by the harness. `used=<k>/<s>` counts the
  shaders whose `air=` matched.
- **Summary line:** `air-oracle <leg> <corpus>: n=<n> writer=<ok> self=<ok> text15=<eq|-> text23=<eq|->
  text23n=<ok|-> posmis=<p> verify=<ok> inv=<ok> wrapper=<ok> used=<k>/<s> metal=<ok>/<total> base=<ok>/<total>
  newfail=<k> reasons=<eq|ne> casts=<i>/<c> usetyped=<u> dropped=<a>/<f> refs=<r> stale=<t>`.
- **Exit 0 only when:**
  - writer = self = verify = inv = wrapper = n; used = s; newfail = 0; reasons = eq;
  - posmis = 0, on every corpus except `smite2`;
  - `full`: text15 = text23 = n − posmis;
  - `defs` and `opt23`: text23n = n, the tokens deleted from the input side number `flags-dropped`, and
    `attrs-dropped` equals the entries P8 drops or weakens in the inputs' `attributes #` lines (23-only kinds on the
    drop list, `captures(...)` other than `none`, `memory(...)` forms with no exact LLVM 15 form).
- **Positional mismatches:** a module whose tool run printed `positional mismatch` is listed in `posmis` and left out
  of `full`'s text comparisons. On `smite2` the count is reported, and each such module must pass `defs` including
  `text23n`. Anywhere else it fails the leg (the 96 test modules have none).
- **`text23n`'s token deletion** is per instruction, with sed rules: `trunc`'s `nuw`/`nsw`, `getelementptr`'s
  `nusw`/`nuw`/`inrange`, `nneg`, `or`'s `disjoint`, `icmp`'s `samesign` (Task 3's drop list). Both sides go through
  `$BIN23/opt -mtriple=unknown-apple-macosx14.0.0 -passes=instsimplify -S`, the same override on both sides, used for
  this comparison only (no module from it is ever written).
- **Helpers:** the `helpers` inputs are bare `.air` bitcode, not metallibs. `prep` takes them as `.bc`, with no
  `extract` and no `pack`. Their legs grade writer, self, text15, text23, verify and inv. There is no Metal row, and
  the wrapper row is informational, because their reference wrapper is Apple's.
- **Metal baseline:** `$W/in/<corpus>.txt` is the run on the original C++ metallibs, made once per corpus. In
  development it is Task 0 Step 5's output. For 4a's corpora it is `XLATE <SRC> <args> --use $S/base/<C>.out >
  $W/in/<c>.txt`, with `XLATE=$S/bin/base-<G1b>/dxil-translate`, never G2's `--no-metal` output.
- **`air-bcinv.py <ours.bc> <ref15.bc>`:** P9b. It fails when a (block, code) of ours is outside the allowlist written
  once in the script, when a `STRUCT_NAME` value of ours is not in the reference's dump, when an attribute kind is
  above 84, or on any `OPAQUE_POINTER`, and names what failed.

- [ ] **Step 1: RED.** With `$W/llvm23-build` at `air-23` after Task 2's fast-forward, run `prep` and `full` on
  `shaders`, and `opt23` on one module of it (a smoke: the leg runs end to end through the tool's `--passes`).
  Expected: exit 1 and the summary line printed in both. Ledger the counts.
- [ ] **Step 2: Comparators fire.** Each mutation below, on scratch copies, makes its row fail and the leg exit 1:
  - text15: one `i` line's pointee changed;
  - text23: one `reassoc` removed from a `.ll23`;
  - self: two same-typed operands swapped in one `.post.ll` (the comparator's other side; a swap inside a written
    `.bc` cannot be made without the writer);
  - text23n: one extra `nsw` added to one `.pre.ll`;
  - metal: one `.metallib`'s bitcode cut by 100 bytes;
  - used: one output metallib deleted;
  - reasons: one baseline `fail` line's reason edited (`reasons=ne`);
  - wrapper: the CPU type changed in one output's wrapper;
  - verify: Task 1's half-truncated `.bc` packed as one output;
  - inv: a `$P0/air-23` module and a `$P0/air-23t` module (the latter by its `dxilOpaquePtrReservedName`); an
    allowlist copy missing one code the writer emits, which must name the code;
  - posmis: a `.types` copy with one `c` count edited (`posmis=1`);
  - det: one byte flipped in one copy;
  - probe: a `dxil-probe-23` output copy with one `ops=` edited.
- [ ] **Step 3: Commit** `dxmt tools: air-oracle, the AIR writer's oracle harness`.

### Task 7: burn-down: the repo corpora and SMITE 2, then 4a's corpora (lanes `fork` and `mn`)

**Files:** as each fix needs, inside the lanes' own files (Execution table). A class that needs a decision goes to
the controller with its evidence and is ledgered as a ruling: a new `llvm.*` name (P13; `llvm.lifetime.*` and
`llvm.fabs.v4f32` are expected), a 23-only attribute outside the lists, or a Metal refusal of a record.

**Interfaces:** Consumes Tasks 4, 5 and 6 merged (the integration build `$W/llvm23-build` at `air-23`'s head, with
`refs` merged). This is development, not a gate: no step here counts as Task 8's run.

- [ ] **Step 1: Run** `prep`, `full`, `defs`, `opt23` and `probe` on `shaders`, `dxil`, `dxbc` and `smite2` (Task 0's
  inputs; `smite2` read in place, P14); `full` and `defs` on `helpers` (no Metal row: helpers have no entry function,
  so `SRC_helpers` is unset and the leg skips `--use`). Ledger the failures by class.
- [ ] **Step 2: Fix each class, test first.** Each writer class gets a lit test in the fork that reproduces it from a
  reduced `.ll` (or `llvm-reduce` output from the `.bc`); each harness class gets a fix in `air-oracle.sh`. Then
  re-run that corpus's legs.
  - A text15 difference that comes from the reader rather than from pointee types gets no writer fix. Example: a
    version-1 module being normalized on read. Such a difference is normalized in `air-oracle.sh` only by controller
    ruling, with its named cause ledgered (as 4a's K4), never silently. A difference known before Task 7 is decided
    in this plan, not at the lane (`source_filename` is written, P7).
- [ ] **Step 3: Expected.**
  - All legs exit 0 on every repo corpus, with `posmis=0`.
  - `usetyped=0` in `defs` and `opt23` on every repo corpus. A non-zero value needs a controller ruling before Task 8,
    with its `--explain` line.
  - opt23's Metal row is ok on every texture shader.
  - `probe` is equal.
  - On `smite2`: every leg exits 0; `posmis`, `usetyped` and `stale` are ledgered, and the ledgered `usetyped` is the
    number 4b may not exceed.
  - The per-leg stats sums are ledgered as information only: they come from LLVM 15-optimized C++ IR, not comparable
    with 4b's modules.
- [ ] **Step 4: Commit** each fix in its lane with its test. The controller fast-forwards `air-23` and `step4w`.
- [ ] **Step 5: 4a's corpora as they land.** As each of 4a's G5 baselines lands (C5 and C6 at 310 and 320 after G3
  and G4, C4-320, C8 after G6), make its Metal baseline (Task 6's rule) and run `prep`, `full`, `defs` and `opt23` on
  it with `XLATE=$S/bin/base-<G1b>/dxil-translate`. These are the DXBC VS, PS, HS, DS and GS stages, the
  stream-output `.1` variants, tessellation through `air_tessellation`, the 3.2 path and SMITE 2's `--pipelines`
  root signatures, which the installed tool (DXBC compute only) never gives Step 1. Fix classes as in Step 2.
  - Expected: as Step 3, on every such corpus. Task 8 waits for this step to be clean.

### Task 8: the gate, once (owner)

Host-only except Step 5, which is one lock job. Preconditions: Task 7 is done, Step 5 included. 4a's G5 has produced
`$S/base/<C>.out` for C1, C2, C4, C5, C6 (each DXBC one also at 320) and C8 (through G6's `--pipelines`), and
`$S/bin/base-<G1b>/dxil-translate`.

- [ ] **Step 1: Release build.**
  - Run `ninja -C $W/llvm23-build install` on `air-23`'s head.
  - Expected:
    - `$W/llvm23/lib/libLLVMAIRBitWriter.a`, `include/llvm-c/AIRWriter.h` and `bin/llvm-air-write` exist;
    - `nm -gU` of the library lists the two C symbols;
    - `$W/llvm23/bin/llvm-config --components | tr ' ' '\n' | /usr/bin/grep -x airbitwriter` matches;
    - the build log shows 0 warnings for our files.
- [ ] **Step 2: In-tree lit.**
  - Run `$W/llvm23-build/bin/llvm-lit -v $FW/llvm/test/Bitcode/AIRWriter $FW/llvm/test/Bitcode`.
  - Expected: all PASS, `unencodable.ll` included: P17's errors hold with assertions off. The second suite proves
    that the lit and CMake edits break nothing.
- [ ] **Step 3: The harness on what ships.**
  - Settings: `AIRW=$W/llvm23/bin/llvm-air-write BIN23=$W/llvm23/bin XLATE=$S/bin/base-<G1b>/dxil-translate`, inputs
    from `$S/base/<C>.out` (Task 6's corpus table). For each corpus, first make the Metal baseline `$W/in/<c>.txt`
    from the original bytes (Task 6's rule), then run the legs below.
  - Expected, in Task 0 Step 5's form (`n` is the number of metallibs; where it says "base's", the count is G5's
    summary for that corpus, ledgered before the run):

    | Corpus | Legs | n | Expected on every leg |
    |---|---|---|---|
    | `shaders` (C1) | `full`, `defs`, `opt23`, `probe`, `det` | 96 | `metal=64/64 base=64/64`, `used=64/64`, `posmis=0`, `usetyped=0` |
    | `dxil` (C2) | `full`, `defs`, `opt23`, `probe`, `det` | 33 | `metal=33/33 base=33/33`, `used=33/33`, `posmis=0`, `usetyped=0` |
    | `dxbc`, `dxbc-320` (C4, C4-320) | `full`, `defs`, `opt23` | 2 each | `metal=2/2 base=2/2`, `posmis=0`, `usetyped=0` |
    | `d3d11`, `d3d11-320` (C5, C5-320) | `full`, `defs`, `opt23` | base's | `metal` = `base`, `used` = all, `posmis=0`, `usetyped=0` |
    | `dx11`, `dx11-320` (C6, C6-320) | `full`, `defs`, `opt23` | base's | `metal` = `base`, `used` = all, `posmis=0`, `usetyped=0` |
    | `smite2` (C8, `--pipelines`) | `full`, `defs`, `opt23`, `probe`, `det` (the 500-name slice) | base's | `metal` = `base`, `used` = all, `reasons=eq`; `posmis` and `usetyped` at most Task 7's ledgered counts, each posmis module passing `defs` with `text23n` |
    | `helpers` | `full`, `defs` | 3 | no Metal row |

  - Every leg exits 0. Record stats and writer time per module (informational).
- [ ] **Step 4: Mutation REDs once**, on the release tools: every mutation of Task 6 Step 2. Expected: each makes its
  leg exit 1. Restore the copies afterwards.
- [ ] **Step 5: The check suite on our AIR (P18), one lock job.**
  - The job, inside `/usr/bin/lockf -k $M/build/pipeline.lock sh -c '…'`, builds nothing:
    - it checks that `build/wine-arm64-src/dxmt-translator-key` is the Global Constraints key, else STOP: the cold
      run must come from the C++ translator that made W's inputs;
    - cold: check.sh's `d3d12_dxil_exec` rows (`dxmt/check.sh:586-624,738-745`: `exec-ours`, `exec-ref`,
      `exec-sm68`, `exec-heap-noflag`, `exec-threads`, `exec-a64`) with `CACHE=$W/cs/cache` (check.sh's `run`
      honours `CACHE`, `:67`), through `$W/cs/check-exec.sh`, an uncommitted copy of check.sh with every other row
      removed;
    - rewrite: every metallib value in the cache database (`shaders_<v>.db`; values that start with `MTLB`), through
      the defs leg's writer invocation exactly as Task 6's row runs it (`air15 extract` and `types`, the helpers'
      lines removed, `--named-only --ref $W/in/helpers/*.air`, then `air15 pack`), stored back in the store's own
      format, read from the cache code at the pinned DXMTRS commit (`git -C
      $D show`); then a snapshot of the database;
    - warm: the same rows with the same `CACHE`.
  - Expected: every row's `expect` line passes; each warm row's output equals its cold output; the database after the
    warm run equals the snapshot. A changed value means a nil library or function, a silent miss that recompiled with
    the C++ translator and stored its metallib over ours (pipeline-cache design §3.3), and fails the step.
  - If the values cannot be rewritten in place, STOP: the controller rules P18 instead, with its cost stated.
- [ ] **Step 6: Report** to `$W/reports/gate.md`: commands, SHAs (`air-23`, `step4w`, G5's tool and its key), every
  summary line, the stats, and Step 5's result or ruling.

### Task 9: one fix pass (lanes)

- [ ] **Step 1:** Each gate failure gets a lit test (or a harness check), then the fix in its lane, then a re-run of
  only the affected corpus's legs on a rebuilt `$W/llvm23` (`ninja install`). Minors go to the ledger, not into fixes.
  Expected: the affected legs exit 0. Commit each fix with its test.

### Task 10: spec, push, merge, the 4b hand-off (controller)

**Interfaces:** This task hands 4b the following:
- the fork commit;
- Task 2's header: two functions and four structs (`LLVMAIRWriteStats` with `RefsFilled`), hand-declared by 4b with
  layout asserts (4-D2);
- P4's contract: at write time, a typed `LLVMAIRValueType` for every function, global and struct that holds a
  pointer, built with `LLVMAIRTypedPointerType`; the helper bitcode it already links passed as `TypedRefs`; no
  instruction entries recorded before the passes. The table is built after the shim's pass pipeline, by walking the
  module (`LLVMGetFirstFunction`/`LLVMGetNextFunction`, `LLVMGetFirstGlobal`/`LLVMGetNextGlobal`) and looking each
  name up in Rust's name→`Ty` map, so the shim exposes "run passes" and "write" as two calls. An entry whose erasure
  no longer matches (`deadargelim`, `globalopt`) is left out by 4b and counted, and P5 derives it; the writer's
  erasure check stays an error (the opt23 leg's `stale` rule is the same);
- the cmake flags of Task 0 Step 4, with ThinLTO and tools Off as `llvm.sh` builds them;
- `air-oracle.sh check <out-dir> <corpus>` (Task 6): verify, inventory, wrapper, Metal ok set and reasons against the
  C++ baseline with the `used` and `air=` checks; the writer's stats, where the translator logs them, are
  informational (Task 7's sums come from LLVM 15-optimized C++ IR and are no pass/fail baseline);
- SMITE 2's ledgered `usetyped`, the number 4b may not exceed;
- the question of P5's controller option: whether 4b makes airconv's three descriptor types identified structs.

- [ ] **Step 1: Fork.**
  - Privacy-scan `git -C $FW log -p llvmorg-23.1.4..air-23`.
  - Run `git -C $F push --dry-run origin air-23`, then push (standing authorization). Expected: `air-23 -> air-23`.
  - Remove the lane worktrees and branches, after `git cherry` comes back empty.
- [ ] **Step 2: MacNeutron.**
  - In `$W/wt-mn`, `git rebase dxmtrs`. Then on `$M`: `git switch dxmtrs && git merge --ff-only step4w`.
  - Expected: a fast-forward. If it fails, STOP: never rebase `dxmtrs` unasked.
  - Pushed with the controller's next MacNeutron push.
- [ ] **Step 3: Spec** §4 "Decided" amended:
  - the interface (P3, one writing entry point plus a type constructor);
  - the type-level contract and its derivation (P4, P5, P6);
  - attributes (P8, K8 widened);
  - the oracle, and Task 8 Step 5's check-suite run or the controller's P18 ruling;
  - branch `air-23` and its commit; K18's Phase 0 record wording.

  §5 step 4 gets "W done (date, commit)". Commit `Spec: DXMTRS step 4W, the AIR writer`.
- [ ] **Step 4: 4b hand-off.** Append to `build/step4/map/7-decisions.md` and `.superpowers/sdd/dxmtrs-program.md`:
  - this task's Interfaces;
  - `llvm.sh` moves to DXMTRS `tools/llvm.sh` (post-R) and builds `Mac-Neutron/llvm-project` at `air-23`'s commit
    instead of `LLVM_TAG`, with no `fix_llvm` (it `die`s on 23, map 2 §3) and no DirectX target;
  - `bundle.sh:127-128` takes LLVM's notices from the fork clone (critic 13); p builds LLVM 23 a second time
    (critic 14);
  - `dxil-translate`'s LLVM 15 reader (`CountFlags`, `--flags`, `setOpaquePointers`, `startswith`) is ported in 4b's
    pin bump (critic 23);
  - SMITE 2's ledgered counts (P14);
  - findings F1 to F3.

  Update memory `dxmtrs.md` and `MEMORY.md`.

## Review record

Three adversarial reviews (`build/step4/map/plan-review-4w-{correctness,gates,exec}.md`), 2026-10-30. Each finding was
checked against its sources before it was applied; none was rejected. One line per finding, in the order received.

1. Critical, descriptor literal structs and chained use-typing: applied. P5 (literal-aggregate rule; one worklist
   fixpoint over use edges, handles first, ties by instruction order; a pointer no seed reaches is `i8` and counted),
   Task 4's required types and `desc-*.ll` (as is and with `--passes instcombine`), Task 7 Step 3. The
   identified-struct alternative is P5's controller option and a Task 10 question, not taken here.
2. Critical, atomics: applied. Task 4's required types and `cast-atomics.ll`; Task 1's `air15 verify` walk; Task 3's
   calls-and-atomics row (`atomicrmw` as code 59 with `valty`).
3. Critical, no value-level oracle in defs and opt23: applied. P9's `self` row in every leg through `--emit-ll`'s
   `.post.ll`; P8 and Task 3 apply drops to the module. The mutation is on the `.post.ll` side (Task 6 Step 2): a
   swap inside a written `.bc` cannot be made without the writer.
4. Important, opaque `ptr` leak (`dxilOpaquePtrReservedName`): applied. P2, Task 2 Step 2 (typed-only enumeration,
   shadows first, `PointerTyID` error), P9b's `STRUCT_NAME` rule, `struct-typed-body.ll`, the inv mutation on
   `$P0/air-23t`.
5. Important, asserts on input-reachable paths: applied. P17, Task 3's unencodable row with `--self-test-late-type`,
   Task 8 Step 2 on the release build.
6. Important, `SETTYPE` keyed on the opaque type: applied. P6, Task 4's files (`writeConstants`, the sort),
   `null-three-pointees.ll` checked as three `SETTYPE`s with distinct type ids, each before a `NULL`, and three
   `POINTER` records (bcanalyzer does not resolve type ids to pointees).
7. Important, call type differing from the callee's: applied. Task 4's required types (callee operand and
   arguments), P5 step 3, `call-type-mismatch.ll`.
8. Important, `llvm.*` intrinsics LLVM 15 lacks: applied. P13 (a `constexpr` list; `llvm.lifetime.*` and
   `llvm.fabs.v4f32` named as expected first rulings), Task 1's `air15 verify`, `llvm-intrinsic-unlisted.ll`, Task 7.
9. Important, constant operands untyped: applied. P6 (every `Use`, recursion), Task 4's required types,
   `const-gep-nested.ll`, `const-inttoptr.ll`.
10. Important, defs never exercises the refs: applied. Task 6's defs row (the helpers' names removed from the
    `.types` copy, the conflict check as a second run), `refs=`, P16's `RefsFilled`.
11. Important, type table in pointer order: applied. Task 2 Step 2, Task 4's Interfaces, Task 6's det row.
12. Critical, `--use` falls back to the C++ bytes: applied. Task 6 (file-count check, `air=` recomputed as FNV-1a 64
    over our files, `used=` in the exit rule) and the deleted-metallib RED. The recompute replaces the cheaper
    "`air=` differs from the baseline" form, since it also catches a partial fallback.
13. Critical, opt23 cannot run through `opt`: applied, the preferred way. P15 (`llvm-air-write --passes`, no
    TargetMachine, `stale=`), Task 2's tool interface, Rules check Dependencies (the tool links `Passes`), Task 6's
    opt23 row and its one-module smoke in Step 1.
14. Important, `text23n`: applied. P9 and Task 6 (`instsimplify` on both sides, token deletion per instruction,
    `flags-dropped` and `attrs-dropped` equalities, its RED).
15. Important, `source_filename` (gates I2): applied. P7, Task 3's module-framing row and `ident-hash-wrapper.ll`;
    Task 7 Step 2's example removed.
16. Important, gate inputs named after the wrong 4a task (gates I3): applied with 27. G5's names and paths, Task 6's
    corpus table with 4a's ids, C4/C5/C6 at 320, Task 8 Step 3's table.
17. Important, F4 wrong (gates I4): applied. F4 deleted; P14 makes SMITE 2 required, in development (Task 0 Step 5,
    Task 7 Step 1) and at the gate as C8.
18. Important, DXBC stages first seen at the gate: applied. Task 7 Step 5; Task 8 waits for it.
19. Important, posmis exits 0: applied. Task 6's exit rule and positional-mismatch rule; the RED.
20. Important, `usetyped` cannot fail: applied. Task 7 Step 3 (0 on repo corpora, a ruling otherwise; SMITE 2's
    count the ceiling for 4b), with P5's definition of `UseTyped` made reachable.
21. Important, inventory reference wrong for opt23: applied. P9b's static allowlist in every leg, `air-bcinv.py`,
    the allowlist RED; the `STRUCT_NAME` value check (4) is the one same-module rule kept.
22. Important, defs never makes refs a type source (gates I9): applied with 10.
23. Important, det on the least-exercised path, comparing timings: applied. Task 6's det row (three legs,
    `shaders`, `dxil` and a 500-name SMITE 2 slice, compared files only) and its RED; Task 8 Step 3.
24. Important, comparators without a RED: applied. Task 6 Step 2's list; Task 8 Step 4 runs all of them.
25. Important, P18 swaps the spec's check suite for text identity: applied. P18, Task 8 Step 5 (one lock job: key
    check, cold run, rewrite through defs, snapshot, warm run), Global Constraints and Execution amended. The
    STOP-and-rule fallback stays because the cache store's format is read when the step runs.
26. Important, no checkable exit and no real baseline for 4b: applied. Goal ("W's exit"), Task 6's `check`
    subcommand, Task 10's Interfaces (stats informational).
27. Critical, the gate wired to 4a outputs that do not exist (exec 1): applied. Cross-plan interfaces (the G2 line
    with its RED, `.out` naming as the contract, `dis=` hashing the translator's module), W's own `corpus-diff.py`
    pinned, and the fallback of building W's own tool with `$S/tool.sh cxx`.
28. Important, `I23` as prefix and bin folder: applied. `BIN23` in Paths, Task 6, Task 8 Step 3 and Rules check.
29. Important, `$L15` build race: applied. The cross-plan contract (the lock on both sides, one source path
    `$S/llvm-project` copied under a guard), Task 0 Steps 2 and 3, Global Constraints.
30. Important, `dxil-probe-23` cannot link: applied. Task 1's `air-tools.sh` (`llvm-config` for 23), Step 3's check,
    Rules check Dependencies.
31. Important, the out-of-tree build and lit do not work: applied, the simplest way. The oot arrangement is dropped
    (P12): lane builds `$W/build-<lane>` with assertions on and the in-tree lit, the harness on the integration
    build; "about an hour" is now about 4 minutes. `LLVM_INCLUDE_TESTS=On` is added to Task 0 Step 4, which the
    in-tree lit needs.
32. Important, F4 stale (exec 6): applied with 17; 3DMark stays out (no DXMTPRC1 reader).
33. Important, function and global handles dangle across the passes: applied. P4, Task 10's hand-off (the table
    built after the passes by walking the module, two shim calls, 4b leaving out and counting entries whose erasure
    changed, the writer's check still an error), P15's matching `stale` rule. The optional lit test is not added:
    with the writer's check kept, that case is `bad-erasure.ll`.
34. Important, `source_filename` (exec 8): applied with 15.
