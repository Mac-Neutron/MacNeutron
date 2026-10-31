# DXMTRS step 4b: the translator on LLVM 23, writing AIR through our writer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Rust translator (`src/airconv`) runs on LLVM 23.1.4 from Mac-Neutron/llvm-project `air-23` and writes
AIR through `LLVMWriteAIRBitcode` instead of LLVM 15's bitcode writer. It lands as one switch, one pin bump and one
translator-key turnover, and Metal builds every pipeline the C++ baseline builds.

**Architecture:** One integration branch `step4b` in DXMTRS. The LLVM substrate comes first: the hand-declared C API
for LLVM 23, `Ty` lowering every pointer to opaque `ptr` with a second, typed lowering for the writer, and the C++ shim
on LLVM 23's C++ API. Then two lanes work side by side: the writer (P4's table, built after the passes) and the DXIL
front end on opaque IR. The LLVM recipe moves into DXMTRS as `tools/llvm.sh`. MacNeutron's same-pin edits cover the
key, the build plumbing, the licences and the grading tools. One writer fix on `air-23` goes first (F-D5-1). The gates
run once, then one fix pass, then the benches with the maintainer's go-ahead, then the controller's pin.

**Tech Stack:** Rust 1.99.0 (edition 2024, cargo `--offline`; rustfmt from `nightly-2026-10-30`), LLVM 23.1.4 at
`air-23` (C API plus the AIR writer's C header, and the C++17 shim built by `cc`), Apple `metal` through
`wine-arm64/tools/xcrun-metal.sh`, meson, POSIX sh, `python3 -I`, lit and FileCheck in the fork.

**Spec:** `docs/superpowers/specs/2026-10-29-dxmtrs-design.md`: §3 (layout, one workspace, `llvm.sh` moves with step 4),
§4 Decided (the writer as 4W amended it), §5 step 4, 4a, 4W and Pace, §6, §8. Binding inputs:
- `build/step4/map/7-decisions.md`: 4-D1 to 4-D10 and "4b hand-off from 4W" (4b-H1 to H15);
- maps `0-porting-map.md`, `1-5`, `6-critic.md`;
- 4a: its plan, ledger and reports (`build/step4/reports/*.md`);
- 4W: its plan, ledger and gate report (`build/step4/w/reports/gate.md`);
- the program ledger;
- `build/pipeline.md` (rules 1-9);
- 5b's plan (`2026-10-30-dxmtrs-step5b-d3d11.md`, P2, P3 and P24: the key coordination).

**Paths** (home is `~`):
- `$M` = `~/Documents/MacProton`, `$B` = `$M/build`, `$SRC` = `$B/wine-arm64-src`, `$D` = `$SRC/dxmt` (the pipeline's
  DXMTRS checkout; never edited, checked out only inside a lock job).
- `$S` = `$B/step4` (4a's folder: `base/`, `corpus/`, `bin/base-0439fa9`, `llvm15`); `$W` = `$S/w` (4W's folder:
  `llvm23/` is the installed LLVM 23 with tools, `in/`, `t/`, `reports/`); `$B4` = `$S/b` (this plan's scratch).
- `$F` = `$B/forks/llvm-project` (the fork clone); `$FW` = `$W/llvm-src` (its `air-23` worktree, the controller's).
- `A/` = `src/airconv/` in a DXMTRS tree; `L23/` = `$FW/llvm` (LLVM 23.1.4 plus the writer). DXMTRS lines are cited at main **265833e** (`$B4/src`); fork lines at
  **da8441e28e** (`$FW`); MacNeutron lines at `dxmtrs` **2c63baa** (`$B4/wt-mn`).
- Ledger: `.superpowers/sdd/2026-10-31-dxmtrs-step4b-llvm23/progress.md`.

## Decisions

Binding, short form (`7-decisions.md`):
- **4-D1** 4b is the LLVM 23 move as one switch, planned from W's results.
- **4-D2** The C API is hand-declared in `airconv` (no `llvm-sys`), one declaration file per LLVM version; 4b redoes
  it for LLVM 23. The writer's header is declared the same way, with layout asserts.
- **4-D3** The pass pipeline and the two custom passes stay C++ in the shim, now on LLVM 23's C++ API (new pass
  manager, MemorySSA).
- **4-D4** The key hashes `cargo tree -p airconv`, the Rust toolchain, the shim's inputs and (4b-H5) `tools/llvm.sh`.
  One turnover, at this switch.
- **4-D5** No process exits on bad input. Every context has the diagnostic handler, and writer errors come back as text.
- **4-D6** Lanes use a non-LTO LLVM outside the pipeline's build folders. The shipped build keeps ThinLTO. 4b's commits
  stay off DXMTRS `main` until the controller's task.
- **4-D7** Corpora: the repo's shaders, `tests/dx11` as DXBC, SMITE 2's capture and 3DMark's recording. The two
  captures are read in place and read-only; outputs stay under `build/step4`; nothing from them is committed.
- **4-D8** `dxil-translate` and `corpus-diff.py` are the gate tools.
- **4-D9** The milestone benches need the maintainer's go-ahead.
- **4-D10** Three adversarial Opus plan reviewers.
- **4b-H1** Fork `air-23` from da8441e28e; a writer fix lands on `air-23` and moves the pin.
- **4b-H2** `llvm-c/AIRWriter.h`: two functions and four structs, field order fixed.
- **4b-H3** P4's table is type-level, built after the shim's passes by walking the module. "Run passes" and "write"
  are two calls. Stale entries are left out and counted, and the writer's erasure check stays an error.
- **4b-H4** cmake: Release, `LLVM_TARGETS_TO_BUILD=""` with no experimental target, assertions Off, zstd Off,
  `LLVM_HOST_TRIPLE=arm64-apple-darwin`, `CMAKE_OSX_ARCHITECTURES=arm64`, `-mcpu=apple-m5`,
  `LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO=Off`, `CMAKE_POLICY_VERSION_MINIMUM=3.5`; ThinLTO and tools Off for what
  ships.
- **4b-H5** `llvm.sh` becomes DXMTRS `tools/llvm.sh`, building the fork at `air-23`'s commit, with no `fix_llvm`.
- **4b-H6** Notices come from the fork's source. p builds LLVM 23 a second time. No profile cfg reaches the
  translator or the writer.
- **4b-H7** `dxil-translate.mm`'s LLVM 15 reader is ported (`:162-168`, `:172`, `:191`, `:1489` at 2c63baa). DXIL's
  data layout is overridden on read (P5).
- **4b-H8** `air-oracle.sh check` grades 4b's output. The writer's stats are information, except `use-typed` (P15).
- **4b-H9** SMITE 2's ledgered `usetyped` (0) is the ceiling.
- **4b-H10** F1-F3 stand.
- **4b-H11** P5's option: decided in P2.
- **4b-H12** F-D5-1: fixed first, in P1.
- **4b-H13** The writer's rewrites (D3-D5) and P13's closed `llvm.*` list are inherited. The shim keeps
  `InstCombinePass()` (R7-5).
- **4b-H14** L4's list: reconciled in P3.
- **4b-H15** The gate items carried from W: D2/D6, n=4 for DXBC, smite2 option A, R9-1.

Rulings of this plan (each: decision — why — cost if wrong):
- **P1 F-D5-1.** The writer writes a vector-typed `ConstantFP` or `ConstantInt` (a splat) as LLVM 15's
  `CST_CODE_DATA`, the element repeated, FP elements by their bits as the `ConstantDataSequential` branch writes them
  (`llvm/lib/Bitcode/AIRWriter/AIRBitcodeWriter.cpp:1621-1640` and `:1669-1680`). A splat of integers wider than 64
  bits is an error. The fix lands as a lit test plus a new `air-23` commit, which becomes 4b's pin (Task 1).
  — LLVM 23 makes every all-equal FP vector a vector `ConstantFP`, through `ConstantVector::get`
  (`L23/lib/IR/Constants.cpp:1548-1584`, which is what the C API's `LLVMConstVector` calls) and through `getSplat`
  (`:1611-1612`, zero included), and LLVM 15 reads the writer's `FLOAT` record as undef. — none.
- **P2 P5's controller option is not taken.** airconv's three literal descriptor types stay literal
  (`A/src/dxbc/binding_rootsig.rs:371,885,937,967`). — The option edits four emitters, adds struct registration and
  changes the module the passes see; it buys only fewer casts. W graded neither form: its Metal corpora are plain mode,
  where the DXIL path binds through `Sm50Bindings` (`A/src/dxil/converter.rs:257-280`) and never emits the three
  types, so SMITE 2's 10,271/10,271 says nothing about them. Their proof in 4b: the `--pipelines` runs of C8 and C11
  with `--out`, graded by `check` with `NOMETAL=1` (verify, inventory, wrapper, `dual`, `use-typed`) against the C++
  tool's own `--pipelines --out` folders (Task 9); check.sh's D3D12 rows; and SMITE 2's bench, which the pin waits for
  (P17). — If 4b's gate shows `use-typed > 0` or a descriptor-chain failure, the option is the fix-pass remedy, still
  inside the one turnover.
- **P3 L4's list is not needed.** 4b hands the writer nothing below functions, globals and identified structs (P4's
  contract). — L4's 511 lines (4a ledger, L4 Step 3: 307 handle loads, 174 arguments, 11 selects against null, 10
  bitcasts, 9 integer-pointer loads) are what the writer's P5 derives: the `air.*` declarations' typed types type the
  handle loads, the entry functions' and the helpers' types type the arguments, and the fixpoint types the rest. W
  proved this on the same corpora: `usetyped=0` in defs and opt23. So 4a's typed-truth `oracle::derive` and the
  frozen `$S/untyped` lists retire with LLVM 15. The creation check (`lower(ty)` equals the value's LLVM type, now up
  to erasure) stays under feature `oracle`. The measure becomes the writer's `UseTyped`, written per write under
  `oracle` and graded by `check`'s `stats` row (P15). Spec §5 step 4a's "4b must type L4's list" is amended. — If a pointer
  escapes P5, `use-typed > 0` names it at the gate and P2's option or a table entry is the fix.
- **P4 Pointers the translator did not make.** `TyCx::lift` gives an LLVM 23 `ptr` the kind `Ptr { pointee: Unknown,
  addrspace }`, using a new `TyKind::Unknown`. `pointee()` answers `None` for it, `lower` ignores every pointer's
  pointee, and `typed()` refuses `Unknown` with an error. `by_raw` is read only for identified structs (unique per
  LLVM type); every other kind is lifted from its parts, because with opaque pointers many `Ty`s erase to one LLVM
  type. — The DXIL body is parsed, and `dxil::lower::lift` (`A/src/dxil/lower.rs:1303-1311`) would refuse every
  pointer operand: the groupshared 64-bit atomics' address is one (`lower.rs:155-160`). No lowering reads a lifted
  pointer's pointee: every `pointee(` read in `A/src` is on a value the translator built (grep at 265833e: `dxil/` 5,
  `dxbc/` 18). Any later read of one is an error, never a guess. — none.
- **P5 DXIL's data layout on read.** The shim's reader takes a flag. Set, its `DataLayout` callback answers the
  file's layout with the `i8:32` component removed, so `i8` takes LLVM's default and every other component stays as
  DXIL has it. The two DXIL parses (`InitializeDXIL`, and `Load` in the converter) set the flag; the helpers' parse
  does not. — LLVM 23 refuses `i8:32` ("i8 must be 8-bit aligned", `L23/lib/IR/DataLayout.cpp:381`), and its reader
  fills a missing load or store alignment from the module's layout (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:6451,
  6531`). Keeping DXIL's other components gives the alignments 4a's LLVM 15 read. `Load` then sets AIR's layout as
  before. — An `i8` access with no explicit alignment reads as align 1 where 4a had 4. Metal and the check suite
  grade that.
- **P6 Typed refs are the helpers this compile linked.** `module::link_helpers` answers the bytes it linked, and
  `writer::write` hands exactly those as `TypedRefs`, as no-copy memory buffers. — The writer parses refs on every
  call (W's P16), so refs never linked would be parsing for nothing. W's defs leg handed all three, and a ref fills
  only names the module holds, so the result is the same. — none.
- **P7 Writer errors.** A failed `LLVMWriteAIRBitcode` gives `AIR writer: <message>` as bytes, status 1, and no
  `DXIL:` prefix, so D3D12 answers `E_FAIL` as for 4a's `LLVM error:`. — A distinct prefix names the stage in a
  game's log. — none.
- **P8 Where the table comes from.** `Context` records every function and global the translator creates: their name
  as LLVM gave it, with their `Ty`. `Module::add_function` records, and a new `Module::add_global` replaces the five
  raw `sys::add_global_in_address_space` calls (`A/src/dxil/converter.rs:359,566`, `A/src/dxbc/resources.rs:272`,
  `ts.rs:1178`, `gs.rs:1134`). At write time, every module function and global whose name is recorded and whose
  `lower(ty)` is its value type gets an entry `typed(ty)`. Every identified struct `TyCx` holds with a body gets a
  body entry, except a lifted one holding `Unknown`. Recorded names that are gone, or whose type the passes changed,
  are counted as `stale`. Entries go to pointer-free values too. — The writer accepts any entry whose erasure matches
  (`L23/lib/Bitcode/AIRWriter/AIRTypes.cpp:243-273`), refuses only opaque structs as struct entries (`:281-289`), and
  checks refs against entries (P16). So "every recorded one" needs no "holds a pointer" test. — none.
- **P9 The key, composition 3** (4-D4). For a tree with `tools/llvm.sh`, the key hashes:
  - the files `src/airconv` (without `tests.rs` files and `tests/` folders, which no image compiles: 4a's seams
    review, Minor 3), D3D11's arguments file, `src/winemetal/airconv_thunks.[ch]` and `tools/llvm.sh` (the LLVM
    commit and recipe). D3D11's file is 5b's P3 rule, taken now: `src/d3d11/d3d11_shader.cpp` when it exists, else
    `src/d3d11/src/shader/args.rs`, and the key dies when neither exists. 4b's tree has the `.cpp`, so the rule leaves
    4b's key value as the plain `.cpp` entry would make it, and 5b's Task 16 keeps only its test rows (P16);
  - the cargo sections, `rustc -vV` and `cargo tree`, as composition 2;
  - the buildtype, `xcrun-metal.sh`, and the `c++` and `metal` versions.

  It drops `llvm $LLVM_TAG` and `$ROOT/dxmt/llvm.sh`. In the same edit it takes every command's output before the
  hashed group, `cat` and `find` included (4a's quality review M-4, its seams review Minor 5). It dies on a
  `[profile.*.package.*]` section naming `airconv`, `airconv-ir` or `"*"`, and on a `[target.'cfg(` section in
  `.cargo/config.toml` (M-4). Compositions 1 and 2 go: a tree without `tools/llvm.sh` dies with "built with the
  MacNeutron commit that pinned it", since its LLVM recipe left MacNeutron with this pin. `inert.sh` prints that
  reason instead of a base key. `wine-arm64/build.sh` and `make llvm-nolto` check `$D/tools/llvm.sh` before sourcing
  it and die with the same text, so a pre-4b tree paired with post-4b scripts (5a's checks, 5c before its rebase, a
  reference build) stops with that reason, never a bare `sh` error. — The cache turns over once anyway, so the
  turnover is the moment to fix the key's known gaps; a later fix would cost players another turnover. — Older pins
  can no longer be built with current MacNeutron scripts (they couldn't anyway: their recipe is gone); a branch on a
  pre-4b DXMTRS base rebases its DXMTRS branch and its MacNeutron branch together (Task 14 Step 6).
- **P10 LLVM 23 gets its own install paths:** `$SRC/llvm23-arm64` (ThinLTO, shipped), `$SRC/llvm23-arm64-nolto`
  (`make llvm-nolto`, the Rust gate's `AIRCONV_LLVM`), and the source `$B/dxmt-src/llvm-air`. LLVM 15's
  `$SRC/llvm-arm64*` and `$B/dxmt-src/llvm-project` stay untouched. — 5a's gates and 5b/5c's lanes build LLVM 15
  trees with `dxmtrs`'s scripts while 4b runs, and `build_llvm` deletes an install whose stamp differs
  (`dxmt/llvm.sh:20-25`), so one path would rebuild LLVM on every alternate lock job. — About 1 GB until the
  controller deletes LLVM 15's folders, once no live branch builds LLVM 15 (program-ledger note).
- **P11 No download.** `tools/llvm.sh` uses its source folder when `git -C <src> rev-parse HEAD` is the pinned commit,
  and fetches it from GitHub only otherwise (a release on a fresh machine). This plan seeds every source folder from
  `$F` with a local `git fetch` (Task 3's `$B4/llvm-air`; Task 11's `$B/dxmt-src/llvm-air` and
  `$B/p/dxmt-src/llvm-air` as APFS clones of it), so no build here touches the network. Every time `LLVM_AIR_COMMIT`
  moves (Task 10, a fork fix in Task 12), the next lock job fetches the new commit into `$B4/llvm-air` from `$F` and
  replaces each seed whole: `rm -rf <seed>`, then `cp -cR` (copying into an existing folder nests it). — Agents never
  download (Global Constraints). — none.
- **P12 airconv's surface shrinks before the switch.** Every module of `airconv` but `abi` becomes private; `api`'s
  re-exports stay, since winemetal uses only `airconv::abi` and the `sm50_*` re-exports (grep of
  `src/winemetal/unix/src` at 265833e). What the compiler then reports unused is deleted. This lands on LLVM 15
  first, proven by 4a's identity run (Task 2), so the switch starts from a smaller surface. The `Val`-lifetime
  refactor 4a's Z4 also deferred (a `Val` borrows the context, not its module) is **not** done here; the only owner
  of a `Module` is a compile, which keeps it until it is written. — These are 4a Z4's notes 2 and 3. Private modules
  close the public reach of note 3, and deleting dead code shrinks what Task 3 declares and checks against LLVM 23.
  — A crate-internal invariant stays documented, not enforced.
- **P13 Tests at the switch.** A test whose expected text is LLVM IR is updated to LLVM 23's printing of the same
  module, keeping its assertions on order, names, flags and attributes. The tests that read `AIRCONV_STEP4_BASE` and
  compare typed LLVM 15 text against the C++ are deleted with the identity proof they served, which ends here (4a
  quality review M-7):
  - `entry::compute_shader_matches_the_baseline`;
  - `module::{initialize_prints_the_baseline_header, writer_packs_baseline, writer_round_trip_keeps_the_text}`;
  - `air::signature::signature_matches_baseline`;
  - `air::builder::declares_match_baseline`;
  - `oracle::derive_*` and `oracle::derive_over_every_baseline_module`.

  `airconv-ir`'s baseline tests, which are LLVM-free, stay. An attribute change in an expected text is a failure,
  never re-recorded: P20 keeps the C++'s LLVM 15 attribute kinds, and LLVM 23's printing of them (`memory(...)`,
  `captures(none)`) is the only change allowed there. — none.
- **P14 The shim's pipeline is pinned to W's P15 string.** That string is the harness's (`dxmt/tools/air-oracle.sh:121`
  at 2c63baa), as W's ruling R7-5 amended it (W ledger, R7-5; W's plan text still has a plain `instcombine`):

  `annotation2metadata,forceattrs,inferattrs,function<eager-inv>(lower-expect,simplifycfg,sroa<modify-cfg>,early-cse),always-inline,ipsccp,globalopt,function(mem2reg),deadargelim,function<eager-inv>(instcombine<no-verify-fixpoint>,simplifycfg<switch-range-to-icmp>)`

  A test compares the shim's pipeline, printed by `printPipeline` without the two custom passes, with what LLVM 23's
  `opt -disable-output -print-pipeline-passes -passes='<W's P15 string>'` prints (run on an empty module, its trailing
  `,verify` dropped). A second assertion places the two passes right after the second `simplifycfg<…>`, inside the
  second function adaptor. To match it the shim uses `SROAPass(SROAOptions::ModifyCFG)`
  (`L23/include/llvm/Transforms/Scalar/SROA.h:42` has no default constructor) and keeps `InstCombinePass()`, whose
  options leave fixpoint verification off (`L23/include/llvm/Transforms/InstCombine/InstCombine.h:33`). The shim never
  sets `VerifyFixpoint`: a textual `instcombine` turns it on (`L23/lib/Passes/PassBuilder.cpp:1264-1268`), and a
  function that needs another iteration then ends the process through `reportFatalUsageError`
  (`L23/lib/Transforms/InstCombine/InstructionCombining.cpp:6244-6250`), against 4-D5. — 4b inherits W's evidence only
  if the shipped pipeline is opt23's (4b-H13). A review-only rule would rot. — none.
- **P15 How 4b's output is graded.**
  - **Metal corpora** go through `air-oracle.sh check` (writer, verify, inventory, wrapper, Metal ok set and reasons
    against the C++ baseline, `used`, `air=`) with three rows this plan adds to `check` (Task 9): `self` (LLVM 23's
    `llvm-dis` of the output equals `<stem>.post.ll`, the module as `LLVMWriteAIRBitcode` left it), `dual` and
    `stats` (below).
    Corpora: C1, C2, C4, C5, C6 at 310 and 320, SMITE 2 in plain mode (option A), and two additions: C4-C6 at `320f`
    (`--metal 320 --shader-flags 3`, which 4a gated) and `c11` (3DMark's 290 DXIL shaders, `$S/corpus/C11`, with C++
    reference modules made once by the frozen base tool).
  - **The root-signature path:** the `--pipelines` runs of C8 and C11 also write `--out` and are graded by `check` with
    `NOMETAL=1`, as `smite2-pl` and `c11-pl`, against the `--pipelines --out` folders of 4a's frozen C++ G6 tool
    (`$S/bin/base-0439fa9-g6/dxil-translate`, which made `$S/base/C8.txt`; `base-0439fa9` has no `--pipelines`), as
    `$B4/base/C8p.out` and `$B4/base/C11p.out` (Task 9; stems are the pipelines.txt line numbers, as b23's tool
    writes them): writer, self, dual, stats, verify, inventory and wrapper.
    `--pipelines` requires `--no-metal` (`dxmt/tools/dxil-translate.mm:1517`), so Metal compiles this AIR only in
    check.sh's D3D12 rows and in SMITE 2's bench, which the pin waits for (P17).
  - **Translation-level runs:** C3, C7, the `-init` runs, and the `--pipelines` runs of C8 and C11. They are compared
    by `corpus-diff.py --no-dis`: status, failure reasons, `refl=` and `tags=` exact; `dis=` and `air=` are
    information.
  - **`dual`** (F-D5-1's class): per module, `air15 dis` and LLVM 23's `llvm-dis` of the same extracted bytes count
    the same `undef`, `poison` and `zeroinitializer` operands. LLVM 15 reads a vector `FLOAT` record as undef while
    LLVM 23 reads it as the splat, so the two readers of one file disagree exactly where an LLVM 15-only misreading
    hides; Task 9's RED shows the row listing da8441e28e's splat output. It replaces a scan against the C++ module,
    whose passes differ and which needed an "explained" escape.
  - **`use-typed` and the stats.** Under feature `oracle`, when `AIRCONV_ORACLE_DIR` names a folder, `writer::write`
    writes `<dir>/<sha256 of the metallib>.log`, one line in `check`'s format (`stats inst-casts=<n> const-casts=<n>
    use-typed=<n> attrs-dropped=<n> flags-dropped=<n> refs=<n> stale=<n>`, the order `air-oracle.sh:533` parses),
    and `<dir>/<sha256>.post.ll`, the module printed after the write (`airconv_ir::sha256`; no new crate). LR step 2
    copies both beside each `<stem>.metallib` by hashing it. `check`'s `stats` row counts a module when its `.log`
    holds that line with `use-typed=0`, so a missing line and a nonzero `use-typed` both fail it, and it must read
    `stats=n` on every corpus (4b-H9; a grep sum would read zero lines as 0). The `stale` count and the other stats
    are information.
  - **Which bytes.** `check` grades the LR tool's output (`oracle` on, `$W/llvm23`, no LTO). The shipped bytes are
    b23's (ThinLTO, `oracle` off, the recipe's LLVM): every metallib b23 writes with `--out` on C1, C2 and SMITE 2's
    first 500 shaders is `cmp`-equal to the LR tool's, and both LLVMs' `.complete` name the same `air-23` sha.
  - **R9-1** (4b-H15): W's check-exec rows (`$W/cs/check-exec.sh`, with `MACNEUTRON_ARM64_APP` naming the wine.app
    under test and a fresh `DXMT_CHECK_WORK`, so cold) run once on ref15's wine.app and once on b23's, both inside g0,
    and `$W/cs/step5-cmp.py` compares them → exit 0.
  - **Timing:** translate-only and Metal pipeline time, A/B against ref15 (g0 step 2), three alternating rounds. Each
    round on each side gets a fresh Metal cache folder through `dxil-translate --metal-cache <dir>` (Task 7), which
    calls `MTLSetShaderCachePath` before the device is made, as `WMTSetMetalShaderCachePath` does
    (`src/winemetal/unix/src/g8.rs:207,602`): Metal keys its cache on the library bytes in one shared store, so a
    tool copied to a fresh path stays warm and ref15's bytes (equal to the C++'s) are warm from W's and 4a's runs. Per
    shader, each side's figure is the median of its three rounds, and a difference counts only beyond the larger of
    the two sides' own spread between rounds. A per-shader build time (translate plus Metal) on SMITE 2 slower than
    ref15 by more than 10% under that rule goes to the maintainer before the pin, since spec §4 chose AIR for build
    latency. Smaller differences are reported.
  - **g10's** five `bitcode=` lines are re-recorded with the reason (K4), by Task 7's g10 step, run inside g0.
  - **A `check.sh` row** whose expected count is a property of the AIR (`--flags`: `fdiv=`, `vout=`, `grad=`,
    `fence=`, `barrier=`) and moves under LLVM 23 is ruled in the fix pass, never edited silently.
  - **Residual risk, for the maintainer.** 4b ends the identity proof against the C++ (`dis=` becomes information).
    A wrong translation that Metal compiles, whose module verifies and whose undef, poison and zero counts agree,
    passes every row above but the check suite's executed and rendered results (both lanes, R9-1) and the benches.
    Value-level equality with the C++ is proven only for what the check suite runs.

  — Spec §8 at the move: every shader builds on Metal, the shader checks render as before, and GPU time shows no
  loss. — The root-signature path's AIR has no Metal compile in the gate itself; its proof is `check`'s rows above,
  check.sh's D3D12 rows and SMITE 2's bench.
- **P16 Coordination with 5b, and the release hold.**
  - 4b waits for no 5b work, and 5b cannot land first: its switch sits on `main` with 4b (5b's P2) and its MacNeutron
    branch rebases onto `dxmtrs` after 4b's pin (5b's P24). 5b's switch deletes `src/d3d11/d3d11_shader.cpp`, so it
    moves the key value when it lands: content is hashed, and no version token stands in for the file, because a
    forgotten token would serve stale translations.
  - P9 takes 5b's P3 rule now (the `.cpp`, else `args.rs`, else die; Task 6's two rows), so 4b's key value is
    unchanged by it and 5b's Task 16 keeps only its test rows; the two branches no longer edit the same lines of
    `translator_key`.
  - 4b pins alone. 5b's pin moves the key once more. Players see one turnover per release, not per pin, as long as no
    release is cut between the two pins.
  - So 4a's no-release rule ends at 4b's pin (binding), and the controller tells the maintainer, when a release is
    asked for, that a release before 5b's switch (and C3, spec §5 step 4) lands costs one more cache turnover.

  — Turnover is per release (4a P7, K1). — A release between 4b and 5b costs one replay per game.
- **P17 Benches** (4-D9). SMITE 2's lobby, Palworld's title screen and Age of Empires II DE's menu
  (`eac-vm/tests/bench-smite2.sh`, `BENCH_GAME=smite2|palworld|aoe2`), on 4a's route (`build/step4/bench/build.sh`,
  `build/step4/s4a-bench/{run,run-aoe2,prep-guest,env}.sh`, copied into `$B4/bench` and adapted). Each game runs once
  on 4b's build and once on the reference, 4b first.
  - The reference is ref15 (Task 10: DXMTRS `git merge-base step4b main`), built with the MacNeutron commit
    `step4b-mn` was rebased onto, in a detached MacNeutron worktree (`$B4/bench/wt-ref`), never "`<M>` at `dxmtrs`"
    (after the pin `dxmtrs` cannot build a pre-4b tree, P9).
  - One build lock job makes two kits, as 4a's `build.sh`: `$D` detached at each side's DXMTRS commit, after a guard
    that `$D` is a development tree, `eac-vm/dxmt-native/build.sh` and `eac-vm/host/build.sh` into `kit-ref15` and
    `kit-4b` (`dxmt/`, `dxmt-translator`, `macneutron-vm`, `metal-host/`, each with its sha256 list), each with its own
    tree's key in the key file, which is put back as found. The guest side (`eac-vm/dxmt/build.sh`, `check-build.sh`,
    `d3d/build-guest.sh`, `guest-stages.sh dxmt d3d`) and the image are built once, at `step4b`, into
    `kit-common/wine.erofs`: the runs are forwarded, so the guest's DXMT does not translate, and one image keeps the
    prefix update from firing between runs.
  - Each run lock job clones the pipeline's VM host set aside (`macneutron-vm`, `metal-host/`, `dxmt/`,
    `dxmt-translator`, `images/wine.erofs`) and puts it back at its end; swaps a kit in by APFS clone and checks its
    sha256 list before each run; and moves `state/<appid>.dxmt` (the forwarded host cache folder, not ext4) aside
    before the first run and back at the end, so every run starts with a fresh `MN_D3D_STATE` folder and the
    maintainer's warm tables survive (the cache opener drops every other key's table, `g8.rs:429,476-493`).
  - The state disk: never `state/default.img` (15 of 16 GB in 4a, and ENOSPC there zeroed SMITE 2's DLLs), and there
    is no `state/<appid>.img` for SMITE 2 or AoE2 (`bench-smite2.sh` sets no `MN_STATE`). A base is an APFS clone of
    `default.img` and `default.tpm` (same id, same keychain key) with the gates' test prefixes removed and the game's
    prefix repaired **in the clone** (`prep-guest.sh`, `prefix-ok`); each run boots its own clone of that base through
    `MN_STATE` and `MN_TPM`. The base's ext4 superblock, read on the host, must show at least 4 GiB free, else stop
    and ask; nothing is deleted from `default.img`.
  - Run settings: `MACNEUTRON_VM_D3D=forward` for all three games (4a's mode; the harness defaults SMITE 2 and
    Palworld to guest), no `MACNEUTRON_DLSS`, `BENCH_GUEST_ENV=<GAME>_RUN_SECONDS=180` for SMITE 2 and Palworld and
    `240` for AoE2 (cold, SMITE 2's lobby took 79 s and a 120 s run ended inside the measured window). Before each run,
    wait for a 1-minute load under 5 (up to 10 minutes). A run is invalid when its VM window was covered (the
    harness's `window` line), `processes_ended` is not 0, or the run ended early; an invalid run is re-run once, and a
    second invalid one goes to the maintainer.
  - Graded, each 4b run against its reference: fps, GPU busy and GPU time per frame (busy share over fps, as 4a's
    AoE2 table), parity or better within 5% (4a's band). SMITE 2 is capped near 60 fps, so it is graded on GPU busy
    and GPU per frame. (DXMT_STATS' per-pass time is not used: 4a recorded none, so there is no reference.)
  - The go-ahead is asked once for the six runs. Mac Steam must be the maintainer's own, already running; agents
    never start or quit it.
  - The pin waits for the benches: SMITE 2's lobby is the only Metal run of the root-signature path's AIR at game
    scale (P2, P15). If the maintainer defers them, the pin waits too.

  — spec §8; 4a's bench found the default disk full, the harness mode wrong and the run too short. — About 30-40
  minutes of the maintainer's machine (six runs of 3-4 minutes, the base preparations and the clones).
- **P18 Lane LLVM.** Lanes use `$W/llvm23` (Release, assertions off, no LTO, `-mcpu=apple-m5`; 4W's integration
  install) as `AIRCONV_LLVM`. Task 0 writes its `.complete` (`build.rs` reruns on that file), and the controller
  reinstalls it after Task 1, as W did after each fast-forward. Task 3 proves `tools/llvm.sh` with a host-only build
  of its own, and the gate builds the shipped installs from the recipe. — Nothing to build before the lanes start.
  — none.
- **P19 Kept as is:** the shim's fast-math functions (IRBuilder's flags work unchanged on LLVM 23), `AIRCONV_VERSION
  26` (K20) and `airconv_public.h` byte for byte. `-lcurses` is dropped from every link line (LLVM 23 needs no
  terminfo: `llvm-config --system-libs` prints `-lm -lz -lxml2`), and the identity rows list the import it removes.
- **P20 LLVM 15's attribute names on LLVM 23.** The translator's attribute tables stay as written, in LLVM 15's names
  (`A/src/air/builder.rs:31-110`, `A/src/dxbc/converter.rs:2852,2867,2897,2917-2921,2956-2960,2996-2997`,
  `A/src/dxbc/replay.rs:1072-1079`, `A/src/dxil/lower_math.rs:295`, `A/src/dxil/lower.rs:348-353,551`).
  `Module::get_or_insert_function` takes them as names, `&[(AttributeIndex, &[u8])]`, and turns each index's set into
  LLVM 23 attributes in one place (`Context::attributes15`), exactly as LLVM 23's reader upgrades the C++'s LLVM 15
  bitcode (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:2357-2380`):
  - on the function index, `readnone`, `readonly`, `writeonly`, `argmemonly` and `inaccessiblememonly` fold, by
    intersection, into one `memory(...)`: `readnone` → `memory(none)`, `readonly` → `memory(read)`, `argmemonly` →
    `memory(argmem: readwrite)`, `argmemonly readonly` → `memory(argmem: read)`;
  - on a parameter, `nocapture` → `captures(none)`; a parameter's `readonly`, `readnone` and `writeonly` stay plain
    (they are parameter attributes in LLVM 23, `L23/include/llvm/IR/Attributes.td:286,289,394`);
  - every other name is LLVM 23's enum attribute of that name. `Context::enum_attribute` answers an error for a name
    LLVM 23 does not know, never `None`, and the six `filter_map(... enum_attribute ...)` sites go.

  The two composite attributes come from two shim helpers (`Attribute::getWithMemoryEffects`,
  `Attribute::getWithCaptureInfo`), so the writer's `memory15` (`L23/lib/Bitcode/AIRWriter/AIRBitcodeWriter.cpp:
  714-760`) writes back the C++'s exact LLVM 15 kinds. — In LLVM 23, `readnone`, `readonly` and `writeonly` are
  parameter-only and `argmemonly`, `nocapture` and `inaccessiblememonly` no longer exist (`memory`, `:163`, and
  `captures`, `:187`, replace them). Created by name, a function-level `readonly` fails the verifier ("does not apply
  to functions", `L23/lib/IR/Verifier.cpp:2351-2357`), so DXIL fails at `A/src/dxil/converter.rs:627-650` and DXBC at
  the writer's `verifyModule` (`AIRWriterC.cpp:51`) for every module with an `air` sample, read or query; and
  `argmemonly` and `nocapture` (kind 0) were silently dropped, so the passes and Metal would treat `air` reads as
  reading and writing all memory and the inventory would differ from the C++. W never met this: LLVM 23's reader
  upgraded the C++'s attributes itself. — none.

Findings against the inputs (for the controller):
- The release rule, as handed over, ends at 4b's pin, while spec §5 step 4 says C3 ships in the same release as the
  LLVM move. P16 keeps the binding rule and reports the cost. The maintainer should say which holds.
- 4b-H9 makes SMITE 2's `posmis` and `stale` ceilings. Both are W harness counts over C++ input. 4b's output has no
  types file (no `posmis`), and its pre-pass module differs from the C++'s (`stale` differs by construction). Only
  `use-typed` is a ceiling here (P15).
- 4b-H5's "builds the fork at air-23's commit" would download the fork in a normal build. P11 seeds the source from
  `$F` locally.
- 4b-H13 cites W's P15 string; W's plan text (4W plan, P15) still reads `instcombine`, while R7-5 made it
  `instcombine<no-verify-fixpoint>`. P14 quotes the string as the harness has it.

## Global Constraints

- **Authority.** Agents never push, merge to `main`, fetch or download. Task 14's pushes run under the maintainer's
  standing authorization, by the controller only. No MacNeutron.app. No Steam start or quit, and no game, without the
  maintainer's go-ahead (P17). No read of `~/Library/Application Support/MacNeutron` beyond 4-D7's two captures. Kill
  by exact PID. Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; DXMTRS subjects read
  `<area>: sentence`.
- **The lock** (`build/pipeline.md`). Every command that builds or installs the runtime or a DXMT build, reads
  installed outputs in a gate, or checks out in `$D` runs as one job: `/usr/bin/lockf -k $B/pipeline.lock sh -c '…'`.
  The job checks out (detached) first, builds before it tests, logs commit and cdhash, leaves `$D` clean and detached
  as found, and creates no branch or worktree there. `<M>` is switched to `step4b-mn` (detached) only inside such a
  job and back to `dxmtrs` at its end (rule 9). Lock jobs: Task 11's, Task 12's re-runs, Task 13's and Task 14's. Everything else is
  host-only (rule 4).
- **Commits in `<M>`** (the spec commit, the squash, the fast-forward of `dxmtrs` or `main`): first `git -C $M status
  -sb` must read `## dxmtrs`. Otherwise (another step's gate job has `<M>` detached) the commit is made in a temporary
  worktree on a temporary branch from `dxmtrs`, and `dxmtrs` moves with `git branch -f dxmtrs <commit>` (rule 9).
- **Builds.** `BUILD_DIR=$B` goes in the environment, never as a make argument, and every MacNeutron script or test
  a lane runs in `$B4/wt-mn` or `$B4/wt-mn9` (`build_test.sh`, which runs `dxmt/toolchain.sh`, included) runs with
  `BUILD_DIR=$B` in its environment: without it `toolchain.sh` builds llvm-mingw inside the worktree (about 40
  minutes) or fetches it (rule 6, 5a's Task 21 incident). `make wine-arm64` and the eac-vm builds need
  `MACNEUTRON_SIGN_IDENTITY` and `MACNEUTRON_PROVISIONING_PROFILE`. VM jobs use the main checkout's
  `eac-vm/builder.sh`.
- **Branches.**
  - DXMTRS: `step4b` from `main` (265833e), with lanes `step4b-<lane>` in worktrees `$B4/wt-<lane>` (`git -C $D
    worktree add …`, submodule folders filled read-only as 4a did).
  - Fork: `air-23-b` in `$B4/wt-fork` (`git -C $F worktree add …`).
  - MacNeutron: `step4b-mn` in `$B4/wt-mn` (Tasks 6, 7, 8), and `step4b-mn-tools` in `$B4/wt-mn9` (Task 9, its own
    worktree so two lanes never share one index). The controller rebases `step4b-mn-tools` onto `step4b-mn` and
    fast-forwards `step4b-mn` at a lane-mn task boundary (the worktree clean), at the latest in Task 10.

  Nothing of these reaches `main` or `dxmtrs` before Task 14. MacNeutron lands only by squash (rule 8).
- **Rust.** `PATH=~/.rustup/toolchains/1.99.0-aarch64-apple-darwin/bin:$PATH cargo … --offline`, with
  `CARGO_TARGET_DIR=$B4/cargo-<lane>`, `AIRCONV_LLVM=$W/llvm23`,
  `AIRCONV_XCRUN=$M/wine-arm64/tools/xcrun-metal.sh` and `AIRCONV_STEP4_BASE=$S/base`.
- **CK, a task's crate checks** (per task; the full `tools/rust-check.sh` runs once, in Task 11, under the
  maintainer's rule):
  - fmt: `cargo +nightly-2026-10-30 fmt --all --check`;
  - clippy: `cargo clippy --target aarch64-apple-darwin -p airconv-ir -p airconv -p winemetal --all-targets
    --features airconv/oracle -- -D warnings`, and `-p airconv-ir --all-targets` on the four `-pc-windows-msvc`
    targets;
  - doc: `RUSTDOCFLAGS="-D warnings" cargo doc --no-deps -p airconv-ir -p airconv`;
  - test: `cargo test --target aarch64-apple-darwin -p airconv-ir`, `-p airconv --features oracle` and `-p winemetal`.
    In Tasks 3 and 4 only, `-p airconv` may take `-- --skip <name>` for the DXIL tests on the ledger's skip list
    (Task 3 Step 5); Task 5's CK runs with no skip;
  - audit: `git -C <wt> add -A && sh tools/audit.sh` → `audit: clean`.
- **LR, a lane run** of the translator on corpora (host-only):
  1. Build the tool: `sh $B4/tool.sh <wt> $B4/bin/<lane>` (Task 9's scratch builder: `oracle` on, `$W/llvm23`).
  2. For corpus `<c>`: `AIRCONV_ORACLE_DIR=$B4/runs/<lane>/<c>.oracle $B4/bin/<lane>/dxil-translate <src> --no-metal
     --out $B4/runs/<lane>/<c>.out [variant flags] > $B4/runs/<lane>/<c>.txt 2> …/<c>.err`, with `<src>` =
     `$S/corpus/C<n>` and 4a's variant flags (`--metal 320`, `--shader-flags 3`; `--initialize-only` for an `-init`
     run, without `--out`). Then, for every `<stem>.metallib` in the `--out` folder, copy
     `<c>.oracle/<sha256 of it>.log` and `.post.ll` to `<stem>.log` and `<stem>.post.ll` beside it (P15); a missing
     `.log` fails the run. Two corpora run twice:
     - SMITE 2 (C8): the capture in place in plain mode with `--out` (graded by `check` as `smite2`, option A), and
       with `--pipelines <capture>/pipelines.txt --out` (corpus-diff against `$S/base/C8.txt`, then `check` as
       `smite2-pl` with `NOMETAL=1`).
     - 3DMark (C11): `$S/corpus/C11` in plain mode with `--out` (corpus-diff against `$S/base/C11-shaders.txt`, then
       `check` as `c11`), and with `--pipelines $S/corpus/C11/pipelines.txt --out` (corpus-diff against
       `$S/base/C11.txt`, then `check` as `c11-pl` with `NOMETAL=1`).
  3. Compare: `python3 -I $TOOLS/corpus-diff.py --no-dis $S/base/<c>.txt $B4/runs/<lane>/<c>.txt` →
     `0 new failures, 0 fixed, 0 changed, 0 extra`. `$TOOLS` is `$B4/wt-mn9/dxmt/tools` (Task 9's worktree) until
     Task 10, then `$B4/wt-mn/dxmt/tools`.
  4. For a Metal corpus, and for `smite2-pl` and `c11-pl` with `NOMETAL=1`: `ROWS_USER="writer self dual stats
     verify inv wrapper" sh $TOOLS/air-oracle.sh check $B4/runs/<lane>/<c>.out <corpus>` → exit 0, with W's gate settings
     (`$W/reports/gate.md`, "harness settings": `XLATE=$S/bin/base-0439fa9/dxil-translate`, `BIN23=$W/llvm23/bin`,
     `GATE=1 SRC_<c>=$S/corpus/C<n>`; smite2 under option A, `GATE` unset).
  5. `use-typed`: `check`'s summary reads `stats=n` (every module's `.log` present, each `use-typed=0`) and `ut=0`.

  Corpus ids map to W's names: C1 `shaders`, C2 `dxil`, C4 `dxbc`, C5 `d3d11`, C6 `dx11`, C8 `smite2`, C11 `c11`;
  the variants are `-320` and `-320f`; C8's and C11's `--pipelines` runs are `smite2-pl` and `c11-pl`.
- **Seams that do not move:** `airconv_public.h` and `dxil/dxil_public.h` byte for byte; `AIRCONV_VERSION 26`; the
  error contract (`*ppError = nullptr`, 0/1, `DXIL:` texts as data); the reflection bytes; the binding model; the VM
  wire; `DXMT_PROBE`/`DXMT_DXIL_BOUNDS`; concurrent compiles on one handle with no lock.
- **No profile cfg, feature or flag** from the p profile reaches `airconv`, `airconv-ir` or the writer (4b-H6).
- **Disk.** Scratch lives under `$B4` (`cargo-*`, `bin/`, `runs/`, `build-fork`, `llvm-air`, `llvm23-recipe`,
  `base/`, `time/`, `logs/`, `reports/`, `ref15/`, `cs/`, `vm/`, `bench/`). Each task deletes its regenerable outputs
  when its check is ledgered (`runs/<lane>`, `llvm23-recipe`, `build-fork` once Task 1 is merged). Check `df -h $M`
  before every build: below 30 GiB free, stop and tell the controller.
- **Shell.** zsh: quote `"====="`. Gating scans use `/usr/bin/grep`. Plans and scripts write the home folder as `~`.

## Execution

**Roles.**
- The controller runs the plan, creates worktrees, merges lanes, fast-forwards `air-23` in `$FW` and reinstalls
  `$W/llvm23` (`ninja -C $W/llvm23-build install`), and keeps the ledger.
- One **pipeline owner** runs every lock job (Tasks 11, 12's re-runs, 13 and 14's development build of the pin, and
  Task 7's g10 step inside g0) and Task 0's host setup.
- **Lanes** use host tools in their worktrees.

| Task | Lane / branch | Model | Depends on | Files (disjoint) | Lock |
|---|---|---|---|---|---|
| 0 setup | controller, owner | — | — | ledger; `$W/llvm23/.complete` | no |
| 1 F-D5-1 | `fork`, `air-23-b` | Opus | 0 | `AIRBitcodeWriter.cpp`, `test/Bitcode/AIRWriter/const-vector-splat.ll` | no |
| 2 private modules, dead code | `l`, `step4b-l` | Sonnet | 0 | `A/src/lib.rs` and what the compiler flags | no |
| 3 LLVM 23 substrate | `l` | Opus | 2 | `A/src/llvm*`, `A/shim/`, `A/build.rs`, `A/meson.build`, `A/darwin/meson.build`, `A/src/module.rs`, `A/src/oracle*`, the P13 test files, `tools/llvm.sh`, `tools/rust-check.sh`; P20's call sites (`A/src/air/builder.rs` `declare`, `A/src/dxbc/converter.rs` `call_declared`, `A/src/dxbc/replay.rs:1072-1082`, `A/src/dxil/lower.rs:348-366,551-554`, `A/src/dxil/lower_math.rs:295-298`); the `module::parse` callers (`A/src/dxil/converter.rs:390`, `A/src/dxil/initialize.rs:96`, `A/src/dxil/converter/tests.rs:53`) | no |
| 4 the writer | `w`, `step4b-w` | Opus | 3 | `A/src/writer*`, `A/src/llvm.rs` (registry, `add_global`), `A/src/dxbc/{resources,ts,gs}.rs`, `A/src/entry/*`, `A/src/module.rs` (`link_helpers`), `A/shim/shim.cpp` and `sys.rs` (the old writer's removal) | no |
| 5 DXIL on opaque IR | `x`, `step4b-x` | Opus | 3; its LR after 4 | `A/src/dxil/**` but the lines Task 3 owns; `A/ir/tests/data` (two fixtures) | no |
| 6 key, composition 3 | `mn`, `step4b-mn` | Opus | 0 | `wine-arm64/lib.sh` (`translator_key`, `toml_sections`), `wine-arm64/tests/translator_key_test.sh`, `eac-vm/dxmt/inert.sh` | no |
| 7 build plumbing and tools | `mn` | Sonnet | 3, 6; its tool link after 4; its g10 step in g0 | `wine-arm64/build.sh`, `Makefile`, `eac-vm/dxmt-native/build.sh`, `dxmt/tools.sh`, `dxmt/tools/dxil-translate.mm`, `dxmt/pins`, `dxmt/llvm.sh` (deleted), `dxmt/tests/build_test.sh`, `wine-arm64/bundle.sh`, `wine-arm64/README.md`, `dxmt/check.sh` (the `fdiv=` comment), `dxmt/tests/wm_probe/g10.{fixed,native}` | no (g10 step: g0's lock) |
| 8 licences and SOURCE | `mn` | Sonnet | 7 | `wine-arm64/lib.sh` (`write_source`), `release/verify-sources.sh`, `wine-arm64/tests/licences_test.sh`, `wine-arm64/licenses/{README,NOTICES.md}` | no |
| 9 grading tools | `mn9`, `step4b-mn-tools` | Sonnet | 0 | `dxmt/tools/corpus-diff.py`, `dxmt/tools/air-oracle.sh`; scratch `$B4/tool.sh`, `$B4/base/{C11,C8p,C11p}.out` | no |
| 10 integration | controller | — | 1-9 | `tools/llvm.sh`'s commit line | no |
| 11 gates, once | owner | Sonnet | 10 | reports | yes |
| 12 one fix pass | owner, lanes | Opus | 11 | as needed | as needed |
| 13 benches | owner | Sonnet | 12, go-ahead | reports | yes |
| 14 spec, push, pin | controller | — | 13 (P17: the pin waits for the benches) | spec, pins, memory | yes |

**Before dependencies land.** Tasks 1, 2, 6 and 9 start at once (9 in its own worktree); Task 8 starts with 7. Task 3
starts after 2, on `$W/llvm23`, whether or not Task 1 has landed: the bindings do not depend on the fix. Task 3 owns
every line its signature changes touch (P20's call sites, the three `module::parse` callers), so it compiles alone.
Its CK covers the DXIL tests that parse with `Layout::Dxil` and declare through P20; a DXIL test that still fails only
for Task 5's converter work (lift, the static-const move) goes on the ledger's skip list with its name and failure
text, which Tasks 3 and 4 pass to `--skip`, and Task 5's CK runs every test with no skip (the list may be empty).
Tasks 4 and 5 run side by side after 3 (rebased onto it). Task 5's lane run waits for 4's merge. Task 7 starts after 3
(its recipe rows test Task 3's `tools/llvm.sh`) and 6; its link of `dxil-translate` waits for 4. Critical path: 2 → 3
→ 4 → 5's LR → 10 → 11.

**Merging.** The controller rebases each lane onto `step4b` and fast-forwards, in the order 2, 3, 4, 5. It re-runs
`sh tools/audit.sh --update-derives` after each rebase and resolves `Cargo.lock` with one `cargo metadata --offline`
(never `cargo update`). A lane branch goes only after `git cherry` is empty.

**Gate point.** One: Task 11. Then one fix pass (12), the benches (13) and the controller (14). Reviews run on Opus.

## Rules check (`CLAUDE.md`, tree-wide)

- **Statics:** none new. The two `LazyLock` environment reads stay (`DXMT_PROBE`, `DXMT_DXIL_BOUNDS`). P8's record is
  state on `Context`, one per compile, so compiles share nothing (the writer holds no mutable static either, 4W's
  P17). Typed refs are parsed per call (P6). `AIRCONV_ORACLE_DIR` is read per write, not cached in a static.
- **Config keys:**
  - none new at run time in a shipped build;
  - under feature `oracle` only (lane and gate tools, never shipped): `AIRCONV_ORACLE_DIR`, the folder for P15's
    per-module `.log` and `.post.ll` — the stem is dxil-translate's, which airconv cannot see, so the file is named by
    the metallib's hash and the LR renames it;
  - `dxil-translate --metal-cache <dir>` (Task 7), a host tool flag for P15's cold timing;
  - build-time `AIRCONV_LLVM` now names an LLVM 23 install, and `AIRCONV_XCRUN` is unchanged;
  - `AIRCONV_STEP4_BASE` is read only by `airconv-ir`'s kept tests;
  - MacNeutron: `LLVM_TAG` leaves `dxmt/pins`, and SOURCE's `LLVM_TAG` becomes `LLVM_REPO` and `LLVM_COMMIT`, read
    from the tree's `tools/llvm.sh` (Task 8);
  - `tools/llvm.sh`'s `LLVM_AIR_REPO` and `LLVM_AIR_COMMIT` are recipe constants, not environment.
- **Wire fields:** the four `AIRWriter.h` structs are an in-process ABI mirrored in `A/src/llvm/sys.rs`:
  - each with `const` size and offset asserts on aarch64 (`AirValueType` 16, `AirStructBody` 16, `AirTypes` 48 with
    `typed_refs` at 32, `AirWriteStats` 24);
  - checked once against a C `offsetof` compile of the header (Task 3's kit);
  - field order fixed (4b-H2).

  No VM wire, slot or `airconv_public.h` change.
- **Dependencies:** no new crate. The LLVM component `AIRBitWriter` joins the link list. `-lcurses` leaves it (P19).
  `tools/llvm.sh` moves from MacNeutron (the maintainer's own script) into the LGPL tree.
- **Derives:** none by default. The four `#[repr(C)]` mirrors derive nothing (they are built in place and passed by
  pointer). `TyKind::Unknown` inherits `TyKind`'s derives. `tools/audit.sh --update-derives` runs in every commit that
  changes one.
- **Lint suppressions:** none. The crate-level `cfg_attr(not(test), deny(…))` stays. The shim builds without
  warnings: the `LLVMAtomicRMWBinOp` switch gets a `default:` (LLVM 23 has more operations).
- **Duplicates:**
  - W's P15 string against the shim, pinned by P14's test;
  - P20's LLVM 15-to-23 attribute map against LLVM 23's reader upgrade and the writer's `memory15`, tied by Task 3's
    `attribute_tables_round_trip_to_the_cxx` test;
  - `typed()` against `lower()`, both in `ty.rs`, one walk over `TyKind` with the pointer arm differing;
  - the LLVM library list: one list (`A/meson.build`'s `llvm_deps`), read by `build.rs`, by `dxmt/tools.sh`'s
    `build_translate` (Task 7, which replaces its copy) and by `$B4/tool.sh`;
  - the C header against its Rust mirror (asserted);
  - MacNeutron's `dxmt/llvm.sh` is deleted, so one recipe remains;
  - `air15`'s typed-type grammar against `typed()` (both describe LLVM 15 types; the gate's verify row ties them).

## Review Focus

1. **A parsed DXIL value whose type holds a pointer, reaching a lowering** (the groupshared 64-bit atomics' address,
   a static-const array's bitcast). Expected: the value lifts as `Ptr { Unknown }`, the lowering works from the
   instruction's own types, and anything that asks for its pointee gets an error naming the op, never
   `LLVMGetElementType` on a pointer (UB on LLVM 23). A pointer-to-pointer bitcast LLVM 23's reader leaves (the
   `.hca` pattern) is retargeted and erased, never rebuilt: a same-type cast folds to the placeholder, which
   `rebuilt` would free and then use. Tests: Task 3's `lift_of_an_opaque_pointer_has_no_pointee`, Task 5's
   `groupshared64_atomics_lower_on_opaque_pointers` and `hca_noop_bitcast_is_retargeted_not_rebuilt` (under
   `MallocScribble=1`), and Task 5's LR on C2 (gsatomic64, gsatomic64c, buffers).
2. **An all-equal FP vector constant from the C API** (F-D5-1). Expected: LLVM 15 reads the splat, never undef.
   Tests: Task 1's lit test and its `air15 dis` check; Task 4's `const_vector_splat_is_written`; `check`'s `dual`
   row on every corpus (P15).
3. **A declaration or internal function the passes delete or retype** (`globalopt`, `deadargelim`). Expected: no
   entry, counted `stale`, and the write succeeds through P5's derivation, never an erasure error. Tests: Task 4's
   `table_leaves_out_a_retyped_function` and `table_leaves_out_a_deleted_declaration`.
4. **Two variants of one shader compiling on two threads**, now through the writer and its per-call refs.
   Expected: each output byte-equal to its single-thread compile. Test: Task 4's
   `compile_on_eight_threads_matches_one`, alternating two argument chains (4a quality review M-5).
5. **A DXIL file LLVM 23's reader refuses as written** (`i8:32`), or a writer refusal at the C boundary. Expected:
   the override reads it, and a writer error is `AIR writer: …` text with status 1. Tests: Task 3's
   `dxil_layout_override_drops_i8_32`, Task 4's `write_returns_the_writers_error_as_text`, and C3/C7's reasons in
   every LR.
6. **An `air.*` declaration with LLVM 15 memory attributes** (`readonly`/`readnone` on the function, `argmemonly`,
   `nocapture`; P20). Expected: it verifies on LLVM 23, and the writer writes back the C++'s LLVM 15 attribute kinds;
   an unknown name is an error. Tests: Task 3's `attribute_tables_round_trip_to_the_cxx` and
   `unknown_attribute_name_is_an_error`, and `check`'s `inv` row on every corpus.

---

### Task 0: setup (controller, owner)

**Files:** none committed but the plan. Host-only.

- [ ] **Step 1:** Commit this plan on `step4b-plan` in `$B4/wt-mn` (`Plan: DXMTRS step 4b, the translator on LLVM 23`),
  then rename the branch to `step4b-mn` (the MacNeutron lane continues there; Task 14 squashes it), and add Task 9's
  worktree: `git -C $M worktree add $B4/wt-mn9 -b step4b-mn-tools step4b-mn`.
- [ ] **Step 2:** Run `git -C $D branch step4b 265833e` (a branch, no checkout) and `git -C $F worktree add $B4/wt-fork
  -b air-23-b air-23`. Ledger `git -C $F rev-parse air-23` (da8441e28e…), DXMTRS `main` and the key `ad6adc5d…af67`.
- [ ] **Step 3:** Write `$W/llvm23/.complete` = `<air-23 sha>+-mcpu=apple-m5+nolto+tools` (P18). Copy 4a's binding
  kit to `$B4/kit` (`$S/reports/review-quality-kit`), adding `#include "llvm-c/AIRWriter.h"` and the four structs'
  `offsetof` lines to `h.c`.
- [ ] **Step 4:** Ledger the 4-D7 permissions as answered, and the program-ledger line "4b executing".

### Task 1: F-D5-1, vector splats as LLVM 15's DATA record (lane `fork`)

**Files:** modify `llvm/lib/Bitcode/AIRWriter/AIRBitcodeWriter.cpp` (the `ConstantInt` and `ConstantFP` branches,
`:1587-1640`); create `llvm/test/Bitcode/AIRWriter/const-vector-splat.ll`. Lane build `$B4/build-fork` (4W Task 0
Step 4's cmake line with `-S $B4/wt-fork/llvm -B $B4/build-fork -DLLVM_ENABLE_ASSERTIONS=On`).

**Interfaces:** Produces the `air-23` commit that becomes 4b's pin (`LLVM_AIR_COMMIT`, Task 10).

- [ ] **Step 1: RED.** `const-vector-splat.ll` builds its constants through `llvm-as` (whose parser makes splats with
  `getSplat`), in one kernel that stores each:
  - `<3 x float> splat (float 1.0)`, `<4 x half> splat (half 0xH3C00)`, `<2 x double> splat (double -0.0)` and
    `<4 x float> splat (float 0.0)`;
  - a second RUN with `-use-constant-int-for-fixed-length-splat` and `<4 x i32> splat (i32 7)`.

  CHECKs on `llvm-bcanalyzer -dump`: in the constants block, one `<DATA` record per splat with as many operands as
  elements, and `CHECK-NOT: <FLOAT` / `CHECK-NOT: <INTEGER` (the module holds no scalar constants of those types).
  `llvm-dis` of the output gives the splats back. Run `$B4/build-fork/bin/llvm-lit -v
  $B4/wt-fork/llvm/test/Bitcode/AIRWriter/const-vector-splat.ll`. Expected: FAIL (`<FLOAT` records). Keep the first
  RUN's output as this unfixed writer wrote it at `$B4/base/splat-da8441e.bc` (Task 9's `dual` RED).
- [ ] **Step 2: Implement** P1 in `writeConstants`: a constant whose type is a vector and that is a `ConstantFP` or
  `ConstantInt` is written as `CST_CODE_DATA`, its element repeated; an integer element wider than 64 bits is the
  error `constant: a vector splat of <type> has no LLVM 15 encoding`. Add a line to the file's `Modified by
  Mac-Neutron (2026)` notice.
- [ ] **Step 3: GREEN.** The lane lit over `llvm/test/Bitcode/AIRWriter` gives all 47 PASS. Then
  `$W/bin/air15 dis` of the test's first output (wrapped as the test writes it) shows each store's operand with
  every element, and no `undef`. Our files compile with 0 warnings.
- [ ] **Step 4: Commit** `AIR writer: vector splats as LLVM 15's DATA records (F-D5-1)`. The controller fast-forwards
  `air-23` in `$FW`, reinstalls `$W/llvm23` (rewriting its `.complete` with the new sha) and ledgers the sha. The
  lane worktree and `$B4/build-fork` go once `git cherry` is empty.

### Task 2: airconv's modules private, dead code deleted (lane `l`, on LLVM 15)

**Files:** modify `A/src/lib.rs:21-32` (every `pub mod` but `abi` becomes `mod`; `pub use api::{…}` stays) and the
files the compiler then reports, deleting what is unused.

**Interfaces:** `airconv`'s public API afterwards is `abi::*` and the `sm50_*`/`dxil_*` re-exports. Nothing else
changes.

- [ ] **Step 1:** Run `grep -rn 'airconv::' src/winemetal tools` to confirm that only `airconv::abi` and the re-exports
  are used outside the crate; ledger the list.
- [ ] **Step 2:** Make the modules private and delete every item `dead_code` names (4a Z4 counted 376 errors). An item
  only tests use moves into its module's `tests.rs`. No body changes.
- [ ] **Step 3: Identity on LLVM 15:** CK with `AIRCONV_LLVM=$S/llvm15`, then 4a's lane run with the LLVM 15 tool
  source frozen first (Task 7 ports wt-mn's copy to LLVM 23 meanwhile): `git -C $M show
  2c63baa:dxmt/tools/dxil-translate.mm > $B4/dxil-translate-15.mm`, then `TOOL_SRC=$B4/dxil-translate-15.mm sh
  $S/tool.sh rust $B4/wt-l $B4/bin/l2` (4a's `$S/wt-mn` is gone) on C1-C7, their `-320`/`-320f`/`-init` runs, C8 and C11
  → `$B4/wt-mn/dxmt/tools/corpus-diff.py` (with `dis=`) `0 new failures, 0 fixed, 0 changed, 0 extra` against
  `$S/base`. Then delete `$S/cargo-l2` and `$B4/bin/l2`.
- [ ] **Step 4: Commit** `airconv: modules private but abi; what nothing calls is gone`.

### Task 3: the LLVM 23 substrate (lane `l`)

**Files:**
- `A/src/llvm/sys.rs`: LLVM 23's C API for what the crate calls, plus `AIRWriter.h`'s functions and structs.
- `A/src/llvm.rs`: `Context::new` without the opaque-pointer call (`:126`).
- `A/src/llvm/ty.rs`: `make` at `:487-488`, `lift_raw` at `:391-431`, plus `typed`.
- `A/src/llvm/tests.rs`.
- `A/shim/shim.cpp`.
- `A/build.rs`: `curses` out of `:69`.
- `A/meson.build`: `llvm_deps` at `:5-19`, LLVM 23's list.
- `A/darwin/meson.build`: `-lcurses` out.
- `A/src/module.rs` and `A/src/module/tests.rs`: `parse` takes a `Layout`; its callers outside: `A/src/dxil/converter.rs:390`
  and `A/src/dxil/initialize.rs:96` (`Layout::Dxil`), `A/src/dxil/converter/tests.rs:53` (`Layout::Keep`).
- P20: `A/src/llvm.rs` (`Context::attributes15`, `enum_attribute` as a `Result`, `Module::get_or_insert_function`
  taking names) and its call sites, which pass their tables as names and lose their `filter_map`: `A/src/air/builder.rs`
  (`declare`, `:1985-1992`), `A/src/dxbc/converter.rs` (`call_declared`, `:441-444`), `A/src/dxbc/replay.rs:1072-1082`,
  `A/src/dxil/lower.rs:348-366` (the `convergent` call-site attribute through `?`) and `:551-554`,
  `A/src/dxil/lower_math.rs:295-298`. The tables' names and contents do not change.
- `A/src/oracle.rs` and `A/src/oracle/tests.rs` (P3).
- P13's test files: `A/src/air/{builder,signature}/tests.rs` and `A/src/entry/tests.rs`.
- Create `tools/llvm.sh`; modify `tools/rust-check.sh:21-22,39` (its `AIRCONV_LLVM` text names an LLVM 23 install,
  MacNeutron's `build/wine-arm64-src/llvm23-arm64-nolto`); replace every doc mention of "LLVM 15" in these
  files.

**Interfaces:**
- `TyCx::lower(ty) -> sys::TypeRef`: a pointer lowers to `LLVMPointerTypeInContext(cx, addrspace)` whatever its
  pointee.
- `TyCx::typed(ty) -> Result<sys::TypeRef, Error>`:
  - a pointer becomes `LLVMAIRTypedPointerType(typed(pointee), addrspace)`;
  - an identified struct is itself;
  - a literal struct, array, vector or function is rebuilt from typed parts;
  - `Unknown`, or a pointee `LLVMAIRTypedPointerType` refuses, gives `AIR writer: a type the writer cannot be told
    (<ty>)`.
- `TyKind::Unknown` and `TyCx::lift` per P4: `pointee(Ptr { Unknown, .. })` answers `None`.
- `module::parse(cx, bitcode, name, prefix, layout: Layout) -> Result<Module, Error>`, with `pub enum Layout { Keep,
  Dxil }`. The shim's `airconv_shim_parse_bitcode` gains `LLVMBool dxil_layout`, P5's callback.
- P20: `Context::attributes15(&self, attrs: &[(sys::AttributeIndex, &[u8])]) -> Result<Vec<(sys::AttributeIndex,
  sys::AttributeRef)>, Error>`; `Context::enum_attribute(&self, name, value) -> Result<sys::AttributeRef, Error>`
  (`LLVM error: no attribute <name>` for a name LLVM 23 does not know); `Module::get_or_insert_function(name, ty,
  attrs: &[(sys::AttributeIndex, &[u8])])`. Shim: `airconv_shim_memory_attribute(LLVMContextRef, int argmem_only,
  int mod_ref)` (`Attribute::getWithMemoryEffects`) and `airconv_shim_captures_none(LLVMContextRef)`
  (`Attribute::getWithCaptureInfo(CaptureInfo::none())`).
- Shim on LLVM 23:
  - `getAsInstruction()` then `insertBefore` (`L23/include/llvm/IR/Constants.h:1580` takes no position);
  - `starts_with` (`:229`);
  - `SROAPass(SROAOptions::ModifyCFG)` (`:176`);
  - `airconv_shim_pipeline_text(int air_passes, char **out, size_t *len)`, which builds the same pipeline as
    `airconv_shim_run_passes` through one shared function and prints it (P14);
  - `airconv_shim_write_bitcode` stays until Task 4.
- `sys.rs` drops `LLVMContextSetOpaquePointers`. Its constants are LLVM 23's: opcode `2` is gone, and
  `LLVMUncondBr`/`LLVMCondBr` are 70/71 (`$W/llvm23/include/llvm-c/Core.h:60-72`). It adds
  `air_typed_pointer_type`, `write_air_bitcode`, `AirValueType`, `AirStructBody`, `AirTypes` and `AirWriteStats`
  (Rules check sizes).
- `tools/llvm.sh` — `build_llvm <arch> <install> <llvm-project>`:
  - constants `LLVM_AIR_REPO=https://github.com/Mac-Neutron/llvm-project.git` and `LLVM_AIR_COMMIT=<air-23's sha at
    this task>`;
  - the stamp `.complete` = `$LLVM_AIR_COMMIT+${UNIX_CPU:-generic}+${UNIX_LTO:-nolto}`, and an install with another
    stamp is rebuilt;
  - the source is used when its `HEAD` is the commit, else fetched shallow into `<src>.tmp` and moved (P11);
  - cmake as 4b-H4, with `LLVM_ENABLE_LTO=Thin` when `UNIX_LTO` is set and `LLVM_BUILD_TOOLS=Off`, and no
    `fix_llvm`;
  - the caller defines `die`, `UNIX_CPU` and `UNIX_LTO`.
- `A/meson.build`'s `llvm_deps` = `$W/llvm23/bin/llvm-config --libs airbitwriter passes linker bitreader`, in that
  order (53 libraries, `-lLLVMAIRBitWriter` among them; the comment above the list names that command).

- [ ] **Step 1: RED** in `A/src/llvm/tests.rs` and `A/src/module/tests.rs`:
  - `pointer_lowers_opaque_whatever_its_pointee`;
  - `typed_lowering_builds_typed_pointers`: `LLVMPrintTypeToString` of `typed(Ptr{Ptr{float,1},2})` is
    `typedptr(typedptr(float, 1), 2)`, and an identified struct's typed type is the struct itself;
  - `typed_refuses_an_unknown_pointee`;
  - `lift_of_an_opaque_pointer_has_no_pointee`;
  - `lift_keeps_identified_structs`: `named_struct` of a struct the table made answers that `Ty`, typed fields and all;
  - `air_writer_mirror_layouts` (`size_of`/`offset_of!`);
  - `dxil_layout_override_drops_i8_32`: `A/ir/tests/data/cache.cs.dxil`'s bitcode parses with `Layout::Dxil` and
    fails with `Keep`, the reader's text naming `i8`;
  - `shim_pipeline_equals_ws_p15`: the literal is what `$W/llvm23/bin/opt` prints for W's P15 string as P14 quotes
    it (`instcombine<no-verify-fixpoint>`, `sroa<modify-cfg>`), recorded once and ledgered with its command;
  - `shim_pipeline_places_the_two_passes`;
  - `attribute_tables_round_trip_to_the_cxx` (P20): a module with one declaration per table (each `Attrs` constant in
    `air/builder.rs` and each inline list in `dxbc/` and `dxil/`), declared through `get_or_insert_function`,
    verifies; each declaration's attributes as LLVM 23 prints them equal the literal recorded once from
    `$W/llvm23/bin/llvm-dis` of a C++ baseline module declaring the same `air.*` name (module and command ledgered),
    which is LLVM 23's reader's upgrade of the C++'s LLVM 15 attributes;
  - `unknown_attribute_name_is_an_error`: `get_or_insert_function` with `(FN, b"nosuchattr")` → `Err`, no
    declaration made.

  Expected: the crate fails to build on `$W/llvm23` (it still calls LLVM 15's API).
- [ ] **Step 2: Implement.** Then run the kit on `sys.rs` against `$W/llvm23/include` (`$B4/kit`'s `cproto.py`,
  `rcheck.py` and `echeck.py`, as 4a's quality review ran them): every declaration and enumerator matches, the
  mirrors' layout equals the C `offsetof`s, and no declaration is unused (P12).
- [ ] **Step 3: P13.** Delete the listed tests and update every other LLVM-text expectation to LLVM 23's printing;
  remove `oracle::derive`, its untyped printing and its call sites (`A/src/entry/{compile,geometry,tessellation}.rs`,
  `#[cfg(feature = "oracle")]` lines only). The creation check and `every_builder_method_agrees_with_llvm` stay.
- [ ] **Step 4: Recipe check** (host-only, `/usr/bin/lockf -k $B4/llvm23.lock`):
  - seed `$B4/llvm-air` (`git init`, `git fetch --depth 1 $F <sha>`, `git checkout -q FETCH_HEAD`);
  - run `ROOT=$PWD; die(){ echo "$*" >&2; exit 1; }; . tools/llvm.sh; UNIX_CPU=-mcpu=apple-m5 UNIX_LTO= build_llvm
    arm64 $B4/llvm23-recipe $B4/llvm-air`.

  Expected: `.complete` = `<sha>+-mcpu=apple-m5+nolto`; `lib/libLLVMAIRBitWriter.a` and
  `include/llvm-c/AIRWriter.h` exist; no `libLLVMDirectX*` and no `bin/llvm-config`; a second call returns at once.
  Then delete `$B4/llvm23-recipe` (keep `$B4/llvm-air`, the seed).
- [ ] **Step 5: CK** (on `$W/llvm23`) → every leg rc 0. A DXIL test that fails only for Task 5's converter work
  (`lift` refusing a pointer, `get_element_type` on one, the static-const move) goes on the ledger's skip list, each
  with its name and failure text, and the test leg runs with `-- --skip <name>` for exactly those; any other failure
  stops the task. Commit `airconv: LLVM 23's C API, opaque lowering and the typed one; the shim on LLVM 23;
  tools/llvm.sh`.

### Task 4: the writer (lane `w`)

**Files:**
- `A/src/llvm.rs`: `Context::record`/`recorded`; `Module::add_function` records; `Module::add_global`.
- `A/src/dxbc/{resources,ts,gs}.rs`: their `add_global` sites.
- `A/src/module.rs`: `link_helpers`' answer.
- `A/src/writer.rs` and a new `A/src/writer/tests.rs`.
- `A/src/entry/{compile,geometry,tessellation}.rs`: the write calls.
- `A/src/entry/tests.rs`: M-5.
- `A/shim/shim.cpp` and `A/src/llvm/sys.rs`: `airconv_shim_write_bitcode` removed.

**Interfaces:**
- Consumes Task 3's `typed`, `sys::write_air_bitcode` and the mirrors.
- Produces:
  - `Context::record(&self, name: &[u8], ty: Ty)` and `Context::recorded(&self, name: &[u8]) -> Option<Ty>` (a
    `BTreeMap<Vec<u8>, Ty>`; the name read back with `LLVMGetValueName2` after creation);
  - `Module::add_global(&self, name: &[u8], ty: Ty, addrspace: u32) -> Result<Val<'cx>, Error>`;
  - `module::link_helpers(m, msad, samplepos, tessellation) -> Result<Vec<&'static [u8]>, Error>`;
  - `writer::write(m: &Module<'_>, refs: &[&[u8]]) -> Result<Vec<u8>, Error>`, which builds P8's table after the
    passes, calls `LLVMWriteAIRBitcode` and packs the metallib as before;
  - P7's error text;
  - under feature `oracle`, when `AIRCONV_ORACLE_DIR` is set, per write: `<dir>/<sha256 of the metallib>.log`, the one
    line `stats inst-casts=<n> const-casts=<n> use-typed=<n> attrs-dropped=<n> flags-dropped=<n> refs=<n> stale=<n>`
    (check's format, P15), and `<dir>/<sha256>.post.ll`, the module printed after `LLVMWriteAIRBitcode`; a failed
    file write is ignored (`let _ =`), and nothing else is printed.

- [ ] **Step 1: RED** in `A/src/writer/tests.rs`:
  - `table_has_an_entry_per_recorded_function_and_global` (each erasing to its value's type);
  - `table_leaves_out_a_retyped_function`: an internal function with an unused pointer argument, called once, then
    `module::optimize` → no entry, `stale=1`, and the write succeeds;
  - `table_leaves_out_a_deleted_declaration`: an unused `air.*` declaration → `stale=1`;
  - `struct_bodies_are_typed`: an `%argument_buffer_struct` with two pointer fields gets one body entry, typed;
  - `write_returns_the_writers_error_as_text`: a module the verifier refuses, built through the C API → `Err`
    starting `AIR writer: `, and the process goes on;
  - `write_types_the_helpers_from_their_refs`: a module calling `air_tessellation`'s functions, linked and written
    with its ref → ok, with `refs` above 0 in the stats;
  - `const_vector_splat_is_written`: `const_vector` of three equal floats is a `ConstantFP` (F-D5-1's shape), and the
    module writes.

  - `oracle_files_are_named_by_the_metallib` (feature `oracle`): the helper `write` calls with the folder (the
    environment read is the one line around it, so the test sets no variable) leaves exactly `<sha256 of the
    metallib>.log`, whose line matches `air-oracle.sh:533`'s pattern, and `<sha256>.post.ll`.

  In `A/src/entry/tests.rs`: `compile_on_eight_threads_matches_one`, with two chains alternated (null, and COMMON at
  metal 320), each result equal to its own single-thread output. Expected: FAIL (no table, no `write` with refs).
- [ ] **Step 2: Implement.** Then delete the shim's old writer and its declaration.
- [ ] **Step 3: CK** (with Task 3's skip list). Then an LR on the DXBC corpora with Metal: C4, C5 and C6 at 310 and
  320 (W's names `dxbc[-320]`, `d3d11[-320]`, `dx11[-320]`), plus C7's reasons. Expected: corpus-diff `--no-dis`
  0/0/0/0, and `check` exit 0 on each with LR step 4's rows (`self`, `dual`, `stats`: Task 9's, run from
  `$B4/wt-mn9` until Task 10). P20's writer side: `$W/bin/air15 dis` of C4's and C5's outputs against their C++
  references gives the same attribute group for every `air.*` declaration (ledgered). The `-320f` runs join once
  Task 9's rows are committed, else in Task 11.
- [ ] **Step 4: Commit** `airconv: AIR through our writer, typed from the table P4 describes`.

### Task 5: the DXIL front end on opaque IR (lane `x`)

**Files:**
- `A/src/dxil/converter.rs`:
  - `:778-796`, the bitcast branch of `retarget`: on opaque pointers a pointer-to-pointer bitcast is a no-op, so it is
    `retarget(cx, user, to)?` and then erased, with no rebuilt cast. LLVM 23's reader keeps a typed bitcast of a
    static global as `bitcast ptr @g to ptr` (`L23/lib/Bitcode/Reader/BitcodeReader.cpp:5332-5358`) and drops the
    lifetime markers that used it (`L23/lib/IR/AutoUpgrade.cpp:5856-5876`); rebuilding it over the `freeze`
    placeholder gives a same-type cast, which `IRBuilder::CreateCast` folds to the placeholder
    (`L23/include/llvm/IR/IRBuilder.h:2280-2281`), so `rebuilt` would erase it (`:834`), rename the freed value
    (`:837`) and the branch read it again (`:789`, `:792`): a use after free on Palworld's `.hca` pattern;
  - `rebuilt` (`:819-839`) returns a `Result` and answers an error when `moved` is the placeholder;
  - the lifetime-marker branch (`:800-805`) and `is_lifetime_marker` (`:855-865`) are deleted: LLVM 23's reader
    removes such markers, and its verifier refuses one on anything but an alloca (`L23/lib/IR/Verifier.cpp:7017-7023`),
    so the branch cannot fire; a marker reaching `retarget` is the existing "used other than by loads" error;
  - `:359` and `:566`: `Module::add_global`.
- `A/src/dxil/lower.rs`: `lift`'s doc, and `group_shared64` working from the instruction's types.
- `A/src/dxil/**/tests.rs`; `A/ir/tests/data/{gsatomic64,buffers}.dxil`.

The `module::parse` callers (`Layout::Dxil`) and P20's attribute sites in `dxil/` are Task 3's.

**Interfaces:** Consumes Task 3's `Layout`, `lift` and `typed`, and Task 4's `add_global` (Task 5 rebases onto 4
before its Step 3).

- [ ] **Step 1: RED:**
  - `groupshared64_atomics_lower_on_opaque_pointers`: C2's `gsatomic64.dxil`, copied into `A/ir/tests/data` with its
    sha256 ledgered → compiles, and its module holds no i64 `atomicrmw`;
  - `hca_noop_bitcast_is_retargeted_not_rebuilt`: C2's `buffers.dxil` (`buffers.hlsl`'s local `kNeighbor`, which
    DXC hoists to a `.hca` global with lifetime markers, Palworld's NeighborOffset pattern), copied into
    `A/ir/tests/data` with its sha256 ledgered, parsed by LLVM 23's reader so the dead `bitcast ptr @g to ptr`
    instruction is really there (a C-API-built module folds the cast away and never reaches the bug) → compiles, and
    the moved global is in address space 2 with every load reading it; run once more under `MallocScribble=1`;
  - `rebuilt_refuses_a_folded_cast`: `rebuilt` with a builder closure that returns its placeholder → `Err`, nothing
    erased.

  Expected: FAIL (lift refuses the pointer; `get_element_type` on a pointer; `rebuilt` has no error).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: CK** with no skip: every test on Task 3's skip list passes. Then an LR with Metal on C1 (`shaders`),
  C2 (`dxil`), smite2 (plain) and `c11` (Task 9's row), at translation level on C3, C7 and the `-init` runs, and C8's
  and C11's `--pipelines` with `--out` (`smite2-pl`, `c11-pl`, `NOMETAL=1`). Expected: corpus-diff `--no-dis`
  0/0/0/0 on each, `check` exit 0 on each checked corpus with LR step 4's rows, and no error text naming a type.
- [ ] **Step 4: Commit** `airconv: the DXIL front end on LLVM 23's opaque IR`.

### Task 6: the translator key, composition 3 (lane `mn`)

**Files:** `wine-arm64/lib.sh:164-246`, `wine-arm64/tests/translator_key_test.sh`, `eac-vm/dxmt/inert.sh:26`.

**Interfaces:** `translator_key <dxmt-tree> <buildtype>` per P9. The caller still sets `MTLD3D_RUST`, and its PATH
holds rustup's cargo and rustc; it no longer sets `LLVM_TAG`.

- [ ] **Step 1: RED.** `translator_key_test.sh` rows, on the scratch workspace (a tree with `src/airconv/Cargo.toml`
  and `tools/llvm.sh`):
  - an edit to `tools/llvm.sh` changes the key;
  - edits to `src/airconv/src/x/tests.rs` and to `src/airconv/ir/tests/data/f` keep it;
  - a tree without `tools/llvm.sh` dies and prints no key;
  - a `[profile.production.package.airconv]` dies, and so does a `[target.'cfg(unix)']` in `.cargo/config.toml`;
  - an unreadable folder under `src/airconv` dies (the `find` status);
  - composition 2's rows carried over: lock edit inside and outside airconv's dependencies, meson/include edits,
    `[profile.production]`, toolchain, uninstalled toolchain, path crate outside, `cargo_toolchain` mismatch, empty
    `cargo tree`;
  - the "another LLVM pin" row replaced by the `tools/llvm.sh` row;
  - D3D11's arguments file (5b's P3 rule, P9): a tree with `src/d3d11/src/shader/args.rs` and no
    `src/d3d11/d3d11_shader.cpp` hashes `args.rs` (an edit to it changes the key), and a tree with neither dies;
  - the key's first line reads `macneutron translator 3`.

  Expected: FAIL on 2c63baa's `lib.sh`.
- [ ] **Step 2: Implement** P9. In `inert.sh`, write `kb=$(translator_key …) || kb="none ($reason)"` (and the same for
  `kn`).
- [ ] **Step 3:** `sh wine-arm64/tests/translator_key_test.sh` → all ok, and `sh -n` on the three files. Commit `translator
  key: composition 3 (tools/llvm.sh, tests out, every input taken first)`.

### Task 7: build plumbing and the host tools on LLVM 23 (lane `mn`)

**Files:**
- `wine-arm64/build.sh`:
  - `:12`: the comment no longer names `LLVM_TAG`;
  - `:17`: the `. dxmt/llvm.sh` line goes;
  - `:162`: `dxmt/llvm.sh` leaves the stamp list;
  - `:487` (its comment) and `:494`: `[ -f "$D/tools/llvm.sh" ] || die "<P9's text>"`, then `. "$D/tools/llvm.sh"` and
    `build_llvm arm64 "$SRC/llvm23-arm64" "$B/dxmt-src/llvm-air"`;
  - `:537`, `:572` and `:573`: `$SRC/llvm23-arm64`.
- `Makefile:167-174`: `llvm-nolto` (and its comment) dies with P9's text when `$B/wine-arm64-src/dxmt/tools/llvm.sh`
  is missing, else sources it and builds `$B/wine-arm64-src/llvm23-arm64-nolto` from `$B/dxmt-src/llvm-air`.
- `eac-vm/dxmt-native/build.sh:5,18,21`: `llvm23-arm64`.
- `dxmt/tools.sh`:
  - its header;
  - `build_probe`: the hand list adds `-lLLVMTargetParser` and drops `-lcurses`;
  - `build_translate`: its list is read from `$3/src/airconv/meson.build`'s `llvm_deps` with `build.rs`'s rule, and
    `-lcurses` goes.
- `dxmt/tools/dxil-translate.mm`:
  - `:10`: the `fdiv=` note no longer names `dxmt/llvm.sh`;
  - `:162-168`: no `setOpaquePointers`;
  - `:191`: `starts_with`;
  - `:172`: `CountFlags` reads through LLVM 23;
  - `--metal-cache <dir>` (P15's timing): `MTLSetShaderCachePath(<dir>)` before the device is made, declared as
    `src/winemetal/unix/src/g8.rs:206-208` declares it; without the flag nothing changes;
- `dxmt/pins:6`: `LLVM_TAG` goes.
- `dxmt/llvm.sh`: deleted.
- `dxmt/tests/build_test.sh:29-66`: the recipe's rows, against
  `LLVM_SH=${LLVM_SH:-${BUILD_DIR:-$ROOT/build}/wine-arm64-src/dxmt/tools/llvm.sh}`, skipped with one line when that
  file is absent.
- `wine-arm64/bundle.sh:125-126`: `$B/dxmt-src/llvm-air/llvm`.
- `wine-arm64/README.md:195,198`: LLVM 23 from DXMTRS's `tools/llvm.sh`, at `llvm23-arm64` and
  `llvm23-arm64-nolto`.
- `dxmt/check.sh:1089-1090`: the comment no longer names `dxmt/llvm.sh` (the row itself is P15's, ruled in the fix
  pass if it moves).
- `dxmt/tests/wm_probe/g10.{fixed,native}`: re-recorded in Step 5, inside g0.

**Interfaces:** Consumes `tools/llvm.sh`'s `build_llvm` (Task 3). P10's paths.

- [ ] **Step 1: RED.** `build_test.sh`'s recipe rows, run as `BUILD_DIR=$B LLVM_SH=<a DXMTRS worktree holding Task
  3's commit>/tools/llvm.sh sh dxmt/tests/build_test.sh` (`$B4/wt-l` while lane `l` lives, else a detached worktree
  at `step4b`; `$D` holds no 4b commit outside a lock job, so the default path would skip), with stub `git` and
  `cmake` that fail and log their names:
  - an install whose `.complete` is the stamp is reused (nothing called);
  - another CPU or LTO keeps its source and rebuilds (`cmake` called);
  - a source whose `HEAD` is the commit is used, not fetched;
  - one at another `HEAD` is fetched (stub `git` fails: `can't fetch`).

  Expected: FAIL (the rows read MacNeutron's old recipe).
- [ ] **Step 2: Implement.** Then run `sh -n` on every edited script, and the RED command again → every row ok, none
  skipped. `/usr/bin/grep -rn 'LLVM_TAG\|dxmt/llvm\.sh\|llvm-arm64\([^-n]\|$\)'` over the worktree (without
  `docs/`) lists only:
  - Task 6's lines (`wine-arm64/lib.sh:175-242`, `wine-arm64/tests/translator_key_test.sh:9,76`), until Task 6 lands;
  - Task 8's (`wine-arm64/lib.sh:78`, `release/verify-sources.sh:15,123`, `wine-arm64/tests/licences_test.sh:112`);
  - two expected survivors, left as they are: `dxmt/tests/shaders/fdiv.hlsl:2` (a test shader's comment; editing a
    corpus source is out of scope) and `dxmt/tools/air-tools.sh:4` (its LLVM 15 tools, which link as LLVM 15's
    `build_probe` did).

  Any other line stops for the controller.
- [ ] **Step 3: Tools against LLVM 23** (host-only, after Task 4's merge): `build_probe` and `build_translate` with
  `$W/llvm23` and `$B4/wt-w` into `$B4/tools-check`. `dxil-probe` matches `$W/bin/dxil-probe-23` on C1, C2 and C3.
  `dxil-translate --flags $S/corpus/C1` runs and prints its rows (values recorded, graded in Task 11).
  `dxil-translate --metal-cache $B4/tools-check/mc $S/corpus/C1` (with Metal) leaves Metal's cache files in that
  folder, and a second run with an empty folder is as slow as the first (cold). Then delete `$B4/tools-check`.
- [ ] **Step 4: Commit** `DXMT: LLVM 23 from DXMTRS's tools/llvm.sh; the host tools on LLVM 23`.
- [ ] **Step 5: g10's goldens** (K4; run by the owner inside job g0, right after g0 step 3's build, and again after
  a fix that moves the key). First `sh dxmt/wm-probe-check.sh g10` and, after the native build,
  `sh dxmt/wm-probe-check.sh --native <winemetal.dylib> g10` with the committed goldens → FAIL, and the differing
  lines are only the five `bitcode=sha256:<hash>/<size>` lines of each file (the old goldens fail on 4b's bytes, and
  nothing else moved). Then record both files from the probe's output, the way `wm-probe-check.sh` runs it (`fixed
  g10`, and the native form), and diff them against 2c63baa's: only those ten lines differ. g10 compiles through
  winemetal's slots with its own chains and function names, so no `dxil-translate` output is its twin; the ten-line
  diff is the cross-check. The files are written in `$B4/wt-mn` (never in `<M>`, rule 2) and committed there on
  `step4b-mn`: `dxmt tests: g10's bitcode hashes for the translator on LLVM 23 (K4)`. The job then detaches `<M>` at
  the new `step4b-mn` head before the gate's wm-probe rows, which run against the new files.

### Task 8: licences and SOURCE (lane `mn`)

**Files:** `wine-arm64/lib.sh:61-85` (`write_source`: `LLVM_REPO=` and `LLVM_COMMIT=`, read from
`$S/dxmt/tools/llvm.sh`), `release/verify-sources.sh:15,123-126` (`LLVM_COMMIT` cited: present in SOURCE and equal to
the archived DXMTRS tree's `tools/llvm.sh`), `wine-arm64/tests/licences_test.sh:112` (the key list),
`wine-arm64/licenses/README:47-50`, `wine-arm64/licenses/NOTICES.md` (only if Step 1 finds a difference).

**Interfaces:** The README entry names LLVM 23.1.4 from `https://github.com/Mac-Neutron/llvm-project.git` at
`LLVM_COMMIT`, "llvmorg-23.1.4 with Mac-Neutron's AIR writer (`llvm/lib/Bitcode/AIRWriter`,
`llvm/include/llvm-c/AIRWriter.h`, `llvm/tools/llvm-air-write`, each modified file noting the change)", replacing
"unmodified".

- [ ] **Step 1:** Diff LLVM 23's notice files (`llvm/LICENSE.TXT`, `lib/Support/COPYRIGHT.regex`,
  `include/llvm/Support/LICENSE.TXT`, `lib/Support/BLAKE3/LICENSE`) and the texts `NOTICES.md` quotes against LLVM
  15's. Ledger the result: the same set of files exists in both.
- [ ] **Step 2: RED:** `licences_test.sh --self-test` with a SOURCE that lacks `LLVM_COMMIT` → FAIL.
- [ ] **Step 3: Implement**; the self-test passes, and `sh -n` passes on each script. Commit `licences: LLVM 23 from our
  fork, cited by commit`.

### Task 9: the grading tools (lane `mn9`, its own worktree)

**Files:** in `$B4/wt-mn9` on `step4b-mn-tools`:
- `dxmt/tools/corpus-diff.py`: `--no-dis`.
- `dxmt/tools/air-oracle.sh`: corpus rows `dxbc-320f`, `d3d11-320f`, `dx11-320f`, `c11`, `smite2-pl` and `c11-pl`
  (the `-pl` rows' `XLATE` is G6's tool, unused under `NOMETAL=1`);
  in `check` mode, the producer's `<stem>.post.ll` copied beside its log (for `self`), and two new rows, `dual` and
  `stats`.
- Scratch, uncommitted: `$B4/tool.sh` (4a's `$S/tool.sh` `rust` mode, with `$W/llvm23` as the default LLVM and the
  list read from the tree's `A/meson.build`) and `$B4/base/{C11,C8p,C11p}.out`.

**Interfaces:**
- `corpus-diff.py --no-dis`: `dis=` differences are printed as `dis-only <file>` and never fail. Status, reasons,
  `refl=` and `tags=` are compared as before.
- The `-320f` rows: `XLATE` arguments `--metal 320 --shader-flags 3`, references `$S/base/C<n>-320f.out`.
- The `c11` row: `SRC_c11=$S/corpus/C11`, references `$B4/base/C11.out`.
- The `smite2-pl` and `c11-pl` rows: references `$B4/base/C8p.out` and `$B4/base/C11p.out`; no Metal row (`--pipelines`
  needs `--no-metal`), so they run with `NOMETAL=1`.
- `dual`: a module counts when `air15 dis` and `$BIN23/llvm-dis` of its extracted bitcode count the same `undef`,
  `poison` and `zeroinitializer` operands; a difference is listed with both counts.
- `stats`: a module counts when the producer's `<stem>.log` holds a `stats …` line (`:533`'s pattern) with
  `use-typed=0`. The summary's exit rule requires `stats=n` when the row is in `ROWS`.

- [ ] **Step 1: RED** (each on scratch copies under `$B4/red`, deleted after):
  - `corpus-diff.py --no-dis` on two copies of `$S/base/C1.txt`, one `dis=` edited → exit 0 with `dis-only`;
  - the same with one `refl=` edited → exit 1;
  - `air-oracle.sh check $S/base/C5-320f.out d3d11-320f` → "unknown corpus" (and the same for `c11`, `smite2-pl`,
    `c11-pl`).
- [ ] **Step 2:** Make the references with the frozen C++ tool (outputs under `build/step4`, never committed; `df -h
  $M` first): `$S/bin/base-0439fa9/dxil-translate $S/corpus/C11 --out $B4/base/C11.out`, and, with G6's C++ tool
  (`$S/bin/base-0439fa9-g6/dxil-translate`, the one with `--pipelines`), `--pipelines <capture>/pipelines.txt
  --no-metal --out $B4/base/C8p.out` and `--pipelines $S/corpus/C11/pipelines.txt --no-metal --out
  $B4/base/C11p.out` (C11: 114 files, `<n>.metallib`, 114/114 ok in a trial run). Implement. Then the REDs of the new rows, each → exit 1 naming the module:
  - per new corpus row, a copy of its reference folder with one metallib truncated to half;
  - `stats`: a copy with each `<stem>.log` holding `stats … use-typed=0 …`, then one line changed to `use-typed=1`, and
    then one `.log` deleted;
  - `dual`: the row's comparison on the splat module Task 1 kept from da8441e28e's writer
    (`$B4/base/splat-da8441e.bc`) lists it (more `undef` in `air15 dis` than in LLVM 23's reading);
  - `self`: a copy whose `<stem>.post.ll` has one instruction line changed;
  - the b23/LR identity (P15): two copies of one `--out` folder, one byte of one metallib flipped → `cmp` fails.

  Then the control: `check` of each `-320f` reference folder against itself, with the Metal row, and of each new
  reference folder (`c11` with Metal, the two `-pl` with `NOMETAL=1`) exits 0 with `ROWS_USER="writer verify inv
  wrapper dual"` (the C++ output graded as its own producer: W's control form, which has no `.log` or `.post.ll`).
- [ ] **Step 3: Commit** `dxmt tools: corpus-diff --no-dis; air-oracle grades 320f, 3DMark's shaders and the
  pipelines' output, with dual, self and stats rows in check`.

### Task 10: integration (controller)

- [ ] **Step 0:** 5a's pin lands first: Task 10 starts once 5a's commits are on DXMTRS `main` (5a is pushing now), so
  the freeze below never blocks it. If 5a is not ready when Tasks 1-9 are, the controller ledgers the wait or its
  reason for going ahead, and tells 5a's controller of the freeze.
- [ ] **Step 1:** Merge 2, 3, 4 and 5 into `step4b` (the Merging rule). Set `tools/llvm.sh`'s `LLVM_AIR_COMMIT` to
  `air-23`'s head (Task 1's commit), in its own commit if Task 3 wrote another: `tools: LLVM at air-23 <sha>`.
  Rebase `step4b-mn-tools` onto `step4b-mn` and fast-forward `step4b-mn` (if not done at a lane boundary).
- [ ] **Step 2:** Rebase `step4b` onto DXMTRS `main`. A conflict outside `src/airconv`, `tools/` and the root
  manifest or lock stops for a ruling. Re-run CK on the result. Declare a DXMTRS-`main` pin freeze in the program
  ledger from now through Task 12, and tell 5a's and 5c's controllers. If `main` moves after Task 12 and before Task
  14, `step4b` is rebased again before Task 14: when the rebase changes no key input (`src/airconv`, the shim,
  `tools/llvm.sh`, D3D11's arguments file) and nothing the benches ran, Task 14 Step 5's development build of the pin
  is the check; otherwise Task 12's minimum re-run set runs again.
- [ ] **Step 3:** Rebase `step4b-mn` onto `dxmtrs`. Ledger both shas, and the reference both sides of every A/B use
  (g0's ref15, the timing, the benches): ref15's DXMTRS commit = `git -C $D merge-base step4b main` (the `main` this
  step rebased onto), and its MacNeutron commit = the `dxmtrs` commit `step4b-mn` was rebased onto. The A/Bs then
  differ only in 4b's commits.

### Task 11: the gates, once (owner)

Each row runs once on `step4b`'s head with `<M>` at `step4b-mn` (switched detached inside each lock job). A failing
row is recorded, not fixed (Task 12). Command forms, references and the flaky rule are 4a Z3's
(`build/step4/reports/gates.md`). Report: `$B4/reports/gates.md`.

- [ ] **Job g0** (lock):
  1. Fetch `LLVM_AIR_COMMIT` into `$B4/llvm-air` from `$F` (local), then replace each seed whole (P11): `rm -rf
     $B/dxmt-src/llvm-air && cp -cR $B4/llvm-air $B/dxmt-src/llvm-air`, and the same for `$B/p/dxmt-src/llvm-air`.
  2. Reference (ref15, Task 10 Step 3): check `$D` out detached at ref15's DXMTRS commit and, from a detached
     MacNeutron worktree at ref15's MacNeutron commit (`$B4/ref15/wt-mn`, made host-only before the job and set up as
     4a Z5's was, `build/step4/reports/pinned-check.md`), run
     `BUILD_DIR=$B make wine-arm64` (LLVM 15 at its own paths, P10). That worktree's `dxmt/tools/dxil-translate.mm`
     carries Task 7's `--metal-cache` lines as an uncommitted edit (the flag only; the tool is no key input), so both
     timing tools take the flag. Copy `$B/wine-arm64/dxil-translate` to `$B4/bin/ref15/` and record the key. Then
     R9-1's first half: `DXMT_CHECK_WORK=$B4/cs/ref15 sh $W/cs/check-exec.sh` on this wine.app (a fresh work folder:
     cold). It runs the main build's test programs (`$M/build/dxmt-tests`, `build/presenter`), whose sources 4b does
     not change; their sha256 is logged at both halves and must match.
  3. Then check out `step4b` and detach `<M>` at `step4b-mn`. Run `make wine-arm64 dxmt-tests dxmt-tests-arm64ec
     presenter build/wine-arm64-tests/winshot` and `eac-vm/dxmt-native/build.sh`; copy `build/wine-arm64/dxil-translate`
     to `$B4/bin/b23/`; record the new key (≠ ref15's), the cdhash and `$SRC/llvm23-arm64/.complete`. R9-1's second
     half: `DXMT_CHECK_WORK=$B4/cs/b23 sh $W/cs/check-exec.sh`, then `python3 -I $W/cs/step5-cmp.py $B4/cs/ref15
     $B4/cs/b23` → exit 0.
  4. Task 7 Step 5: g10's goldens, recorded and committed on `step4b-mn`, `<M>` detached at the new head.
- [ ] **Mac** (lock jobs):
  - both `check.sh` lanes → 417 ok, 0 FAIL (P15's rule for AIR-property rows);
  - `wine-arm64/check.sh dxmt dxmt-present`;
  - `presenter/check.sh`;
  - `wm-probe-check.sh` → 11 ok, against g0 step 4's goldens;
  - facade, both `d3d9-check` forms, conformance (R's figures);
  - `wine-arm64/tests/{mode,licences,translator_key}_test.sh`, and `BUILD_DIR=$B sh dxmt/tests/build_test.sh` with
    `$D` at `step4b` (its recipe rows read `$D/tools/llvm.sh`, none skipped).
- [ ] **Native and VM host** (lock): `eac-vm/dxmt-native/build.sh`; `nm -gU …/winemetal.dylib` → the 14 names; the
  native gate (31); `wm-probe-check.sh --native` → 11 ok; `eac-vm/host/build.sh` and the host tests; the loopback gate
  (31).
- [ ] **VM** (lock):
  - every VM row runs on `MN_STATE=$B4/vm/state.img`, a `cp -c` of `images/state-template.img` (with its own
    `state.tpm` beside it), never on `state/default.img`: 4a found that disk at 15 of 16 GB with the gates' test
    prefixes, and ENOSPC there zeroed SMITE 2's DLLs. Nothing in `default.img` is deleted. A row that sets its own
    `MN_STATE` keeps it;
  - `eac-vm/dxmt/build.sh` (the guest gets the new key), `check-build.sh`, the guest stages;
  - the VM DXMT gate (45), d3d-smoke in both WoW64 modes, `d3d9-gate`, `present-gate`, the forwarder gate (41).
- [ ] **Translator** (host-only, P15):
  - every LR comparison on every corpus and variant, with the LR tool built from `step4b`'s head (`$B4/tool.sh`),
    LR steps 1-5 (`self`, `dual`, `stats`), `smite2-pl` and `c11-pl` included;
  - which bytes: `$B4/bin/b23/dxil-translate --no-metal --out` on C1, C2 and SMITE 2's first 500 sorted shaders (a
    folder of APFS clones, as W's `det` leg) → every metallib `cmp`-equal to the LR tool's; `$SRC/llvm23-arm64/.complete`
    and `$W/llvm23/.complete` name the same `air-23` sha;
  - determinism: two b23 processes with `--out` on the same three → `cmp -r` equal;
  - timing against `$B4/bin/ref15` (P15): three alternating rounds per corpus, each run with its own `--metal-cache
    $B4/time/mc-<side>-<round>` (fresh, deleted after the round), per-shader medians and the spread rule.
- [ ] **Rust and licences**: one lock job checks `$D` out at `step4b`, detaches `<M>` at `step4b-mn` and runs `BUILD_DIR=$B
  make llvm-nolto` (the recipe is `$D`'s, so `$D` must hold 4b; the Makefile dies with P9's text otherwise). Then,
  host-only, in a DXMTRS worktree at `step4b`'s head: RC with `AIRCONV_LLVM=$B/wine-arm64-src/llvm23-arm64-nolto
  AIRCONV_XCRUN=$M/wine-arm64/tools/xcrun-metal.sh` → `rust-check: ok`; `cargo metadata` → no new package;
  `licences_test.sh` on the built `wine.app`.
- [ ] **Binary identity:** `$B/step-r/ident.sh` and `ident-diff.sh` on `winemetal.so` and `winemetal.dylib` against
  4a's tables (`$S/z3/id/after`, `after-n1`). Every difference is listed: LLVM's symbols, `libncurses` gone, sizes. The
  install name, rpaths, minos, `___wine_unix_call_*` and the `NtSetEvent` import are unchanged.
- [ ] **p** (lock, one job). 5a's g8 did not run `MACNEUTRON_PROFILE=p make wine-arm64` (its Mac p side was
  lane-build.sh), and the p dxmt tree (`$B/p/wine-arm64-src/dxmt`: one branch, `HEAD` = applied = series, the old pin)
  answers `reapply` to a moved pin, which means `rm -rf` and a GitHub fetch. So, in order:
  - `git -C $B/p/wine-arm64-src/dxmt fetch -q $D <step4b sha>` and `git -C $B/p/wine-arm64-src/dxmt checkout -q
    --detach FETCH_HEAD` (a local fetch; `HEAD` ≠ applied makes it a development tree; no branch is made);
  - guard: with `BUILD_DIR=$B/p`, `tree_mode` of wine, fex, dxmt and lsteamclient answers neither `reapply` nor
    `pinned`, else stop;
  - from `<M>` detached at `step4b-mn`: `MACNEUTRON_PROFILE=p BUILD_DIR=$B/p make wine-arm64`, then
    `MACNEUTRON_PROFILE=p BUILD_DIR=$B/p sh eac-vm/dxmt-native/build.sh` (`release/p-seed.sh` is not used: it
    hard-codes `build/`);
  - at the end, `git -C $B/p/wine-arm64-src/dxmt checkout -q --detach $(cat $B/p/wine-arm64-src/dxmt.applied)`.

  Graded:
  - p's `llvm23-arm64` is built from the recipe (`$B/p/wine-arm64-src/llvm23-arm64/.complete` names the pinned sha);
  - p's key equals std's;
  - the shim's objects of the std and p builds are byte-equal (`ar -x` of each `libairconv_shim.a`, then `cmp`).
- [ ] **Report.** Restore `$D` to `$(cat $SRC/dxmt.applied)` and `<M>` to `dxmtrs`; write the report.

### Task 12: one fix pass (owner, lanes as needed)

- [ ] For each failing row: find the root cause, write a failing test or LR row first, fix it in the owning file, run
  that task's checks, then the failed row once, plus the minimum re-run set, named in the ledger:
  - a fix touching `src/airconv`, the shim, `tools/llvm.sh` or the fork pin moves the key and every output byte, so,
    as 4a's Z4: CK; g0 step 3 with the new key recorded (and g0 step 1 first when `LLVM_AIR_COMMIT` moved); Task 7
    Step 5 (g10) again; the whole Translator block; both `check.sh` lanes; `wm-probe-check.sh` and its `--native`
    form; the native, loopback and VM DXMT gates; and the p rows (key equality, shim objects);
  - a fix only in a MacNeutron script or tool re-runs the rows that read it.
- [ ] **A writer fix** (fork) takes Task 1's route: a lit test RED on a new `air-23-b` commit, the fix, the lane's lit
  over `llvm/test/Bitcode/AIRWriter` all PASS, the commit. The controller fast-forwards `air-23` in `$FW`, reinstalls
  `$W/llvm23` (its `.complete` rewritten with the new sha) and commits the new `LLVM_AIR_COMMIT` on `step4b` (Task 10
  Step 1's form). The first lock job after it re-runs g0 step 1 (a local fetch from `$F` into `$B4/llvm-air`, each
  seed replaced whole), then the minimum set above.
- [ ] An AIR-property `check.sh` row that moved is ruled by the controller: what the count measures must still hold,
  and the expected value is re-recorded with its reason in the MacNeutron commit. A second round goes to the
  maintainer. Commit each fix in its lane.

### Task 13: the milestone benches (owner, the maintainer's go-ahead)

- [ ] **Step 1:** Ask the go-ahead once for the six runs (P17), saying that Mac Steam must be running and that the
  maintainer should not be playing. Wait.
- [ ] **Step 2** (host-only): copy 4a's bench scripts into `$B4/bench` (`build/step4/bench/{build,env}.sh`,
  `build/step4/s4a-bench/{run,run-aoe2,prep-guest}.sh`) and adapt them to P17: sides `ref15` and `4b`, games
  `smite2`, `palworld` and `aoe2` (AoE2 with a fresh `/home/mn/aoe2` in its base, as `run-aoe2.sh`), forward, the
  run lengths, the load wait, the window check and the re-run rule. Make two detached MacNeutron worktrees, set up
  as 4a Z5's: `$B4/bench/wt-ref` at ref15's MacNeutron commit and `$B4/bench/wt-4b` at `step4b-mn`'s head. Paths in
  the scripts are written with `~`.
- [ ] **Step 3** (one lock job, `$B4/bench/build.sh`): the guard that `$D` is a development tree; then, at each
  side's DXMTRS commit and from its worktree, `eac-vm/dxmt-native/build.sh` and `eac-vm/host/build.sh` into
  `kit-ref15` and `kit-4b` (each with its own key in the key file, put back as found, and a sha256 list); then, at
  `step4b` from `wt-4b`, `eac-vm/dxmt/build.sh`, `check-build.sh`, `d3d/build-guest.sh`, `guest-stages.sh dxmt d3d`
  and the image into `kit-common/wine.erofs`. `$D` is restored and clean at the end; the job logs every commit,
  cdhash and sha256.
- [ ] **Step 4** (one lock job per game, `$B4/bench/run.sh <game>`): set the pipeline's VM host set aside; move
  `state/<appid>.dxmt` aside; prepare the game's base (a clone of `default.img` and `default.tpm`, `prep-guest.sh`
  → `prefix-ok`); read the prepared base's ext4 superblock on the host (free blocks × block size): below 4 GiB, STOP
  and ask, deleting nothing. Then `4b` and `ref15`, in that order, each on its own clone of the base, after the load
  wait, with its kit swapped in and its sha256 list checked, and with no `state/<appid>.dxmt` present. At the end put
  back the VM host set and the original cache folder, and delete the base and the run clones.
- [ ] **Step 5:** Report each 4b run against its reference: fps, GPU busy and GPU time per frame, the lobby or menu
  time, the window lines and the load at start and end, and every invalid run with its re-run. Within 5% or better
  passes; a loss goes to the maintainer before the pin.

### Task 14: spec, push, pin, key turnover (controller)

Every commit in `<M>` follows Global Constraints' `## dxmtrs` rule.

- [ ] **Step 1: Spec** on `dxmtrs`:
  - §4 Decided: F-D5-1 fixed at `<air-23 sha>`; P5's option not taken (P2); L4 reconciled (P3); the attribute map
    (P20).
  - §5 step 4b: done, with the date, the DXMTRS and fork shas, the key `ad6adc5d…` → `<new>`, and this plan's rulings
    that change binding text (P4, P5, P8-P12, P13's deletions, P15, P16, P17, P20).
  - §6: the key's composition 3, with 5b's P3 rule.
  - §8: the translator gate at the move, worded as `check` with its `self`, `dual` and `stats` rows, and P15's
    residual risk.
  - The release note as the maintainer answers the finding.

  Privacy-scan the spec, the plan and the ledger. Commit `Spec: DXMTRS step 4b, the translator on LLVM 23`.
- [ ] **Step 2: Fork.** Privacy-scan `git -C $FW log -p da8441e28e..air-23`; `git -C $F push --dry-run origin air-23`,
  then push.
- [ ] **Step 3: DXMTRS main.** Restore `$D` (rule 5). Privacy-scan `main..step4b`, then `git -C $D fetch . step4b:main`
  (it refuses a non-fast-forward and checks nothing out), push `--dry-run`, push. No branch or worktree is removed
  yet.
- [ ] **Step 4: Pin, one commit:** `git merge --squash step4b-mn` onto `dxmtrs`, `DXMT_COMMIT=<main's sha>`, commit
  `DXMT: the translator on LLVM 23 (DXMTRS <sha>)`. 5b pins later, alone (P16).
- [ ] **Step 5: The pushed pin's development build** (lock; 4a Z5's form, pipeline rule 7, never forced pinned):
  - `git -C $D ls-remote origin main` names the pushed sha, and `git -C $F ls-remote origin air-23` names
    `LLVM_AIR_COMMIT`;
  - a detached MacNeutron worktree at the pin commit, set up as Z5's, with `BUILD_DIR=$B` in the environment;
  - guard: `tree_mode` of the dxmt tree answers `development` (wine, fex and lsteamclient `applied`), none `reapply`
    or `pinned`; `$D` is checked out detached at the pushed sha;
  - `make wine-arm64 dxmt-tests dxmt-tests-arm64ec presenter` → "development build"; `DXMT/version` =
    `<sha>+dev`, and `$D` is clean;
  - the key and `$SRC/llvm23-arm64/.complete` equal Task 11's (or Task 12's last);
  - both `check.sh` lanes give 0 FAIL, and `wm-probe-check.sh` 11 ok;
  - `$D` restored to `$(cat $SRC/dxmt.applied)`, clean; the worktree removed.

  Then fast-forward MacNeutron `main` and push. A failed fast-forward stops for the maintainer.
- [ ] **Step 6: Branches.** Only now remove 4b's DXMTRS worktrees and lane branches (`git cherry` empty first), and
  never the last ones: if a removal would leave `$D` with one branch and no second worktree, `step4b` stays (ledgered),
  since a single-branch `$D` answers `reapply` and the next build would `rm -rf` it (rule 7).
- [ ] **Step 7: Records:**
  - ledger and program ledger: 4b done; the new key; the release hold (P16, P17);
  - 5a, 5b and 5c (and steps 3, 5, C2-C5): each rebases its DXMTRS branch onto `main` with 4b and its MacNeutron
    branch onto `dxmtrs` with the pin, together (a pre-4b tree with post-4b scripts dies, P9); their `make llvm-nolto`
    lock job checks `$D` out at their rebased commit first, and their RC uses
    `AIRCONV_LLVM=$B/wine-arm64-src/llvm23-arm64-nolto AIRCONV_XCRUN=$M/wine-arm64/tools/xcrun-metal.sh`;
  - 5b rides `main` with 4b; its pin moves the key once more (P16);
  - LLVM 15's folders (`$SRC/llvm-arm64*`, `$B/dxmt-src/llvm-project`) are deleted once no live branch builds LLVM
    15.

  Update memory `dxmtrs.md` and `MEMORY.md`.
- [ ] **Step 8: Disk.** Delete `$B4/{cargo-*,bin,runs,time,llvm-air,vm,ref15,cs,red}`, `$B4/bench/{kit-*,state,caches}`,
  `$B4/base/{C8p,C11p}.out`, `$S/{llvm15,llvm15-build,llvm-project,runs}`, `$W/{t,gate/pregate,llvm23-build}` and,
  after this task, `$W/llvm23`. Keep `$S/base`, `$S/corpus`, the rest of `$B4/base`, the reports and the ledgers.

## Review record

Plan review, 2026-10-31 (33 findings: 7 Critical, 26 Important; each checked against the source first). Numbers are
the findings' order.
1. Critical, LLVM 15 attribute names on LLVM 23 — applied: P20; Task 3 owns the map (`Context::attributes15` inside
   `get_or_insert_function`, one place, the six call sites only lose their `filter_map`), `enum_attribute` errors on
   an unknown name, RED `attribute_tables_round_trip_to_the_cxx` and `unknown_attribute_name_is_an_error`, Task 4's
   `air15` attribute-group check, P13's no-re-record rule. Verified: `Attributes.td:163,187,286,289,394`,
   `Verifier.cpp:2351-2357`, `BitcodeReader.cpp:2357-2380`, `AIRBitcodeWriter.cpp:714-760`.
2. Critical, use after free on the `.hca` bitcast — applied: Task 5 retargets and erases a pointer-to-pointer bitcast,
   `rebuilt` errors when `moved` is the placeholder, the lifetime branch and `is_lifetime_marker` go with their test.
   RED fixture: C2's `buffers.dxil` (its `kNeighbor` is the `.hca` pattern; W's gate lists it in
   `$W/gate/res/full.dxil/upgraded.txt`, D6's dead cast), parsed by LLVM 23's reader, under
   `MallocScribble=1`. Verified: `IRBuilder.h:2280-2281`, `BitcodeReader.cpp:5332-5358`, `AutoUpgrade.cpp:5856-5876`,
   `Verifier.cpp:7017-7023`, `converter.rs:778-839`.
3. Important, Task 3 cannot compile or test without Task 5's files — applied with 19: Task 3 owns the three
   `module::parse` callers and P20's sites. Not taken as worded: "Task 3's CK covers the DXIL tests" is a prediction
   (`cache.cs.dxil` also needs Task 5's lift and lowering), so 19's ledgered skip list carries what still fails.
4. Important, P14's pipeline string — applied: P14 quotes the harness's string (`air-oracle.sh:121`) with
   `instcombine<no-verify-fixpoint>` and `sroa<modify-cfg>`, cites R7-5, says the shim never sets `VerifyFixpoint`,
   and "W's P15 string" is used throughout (test `shim_pipeline_equals_ws_p15`).
5. Critical, bench state disks — applied for the benches (4a's `run.sh` route: a prepared base cloned from
   `default.img` and `default.tpm`, a clone per run through `MN_STATE`, 180 s and 240 s runs, P17, Task 13).
   Rejected in part: "every Task 11 VM row on a `cp -c` clone of `default.img`" — a clone of a disk 4a found 15/16
   full would hit ENOSPC with the gates' test prefixes; the VM rows run on a template clone, unconditionally.
6. Important, `check` grades no values — applied: `dual` row (a), `self` through `<stem>.post.ll` (b), R9-1 in g0 on
   ref15 and b23 with `step5-cmp.py` (c), P15's residual risk (d); the old undef scan is gone.
7. Important, `use-typed` as a grep sum — applied: oracle `.log` files in `check`'s `stats` format named by the
   metallib's sha256 and renamed by the LR, a `stats` row that fails a missing line or a nonzero count, b23's metallibs
   `cmp`-equal to the LR tool's on C1, C2 and SMITE 2's first 500, both `.complete` stamps compared.
8. Important, no REDs for the new rows — applied: Task 9 Step 2's REDs per new corpus row, `stats` (planted
   `use-typed=1`, deleted `.log`), `dual` (da8441e28e's splat module, kept by Task 1), `self`, and the flipped-byte
   `cmp`.
9. Important, warm Metal cache in the timing — applied: `dxil-translate --metal-cache` (Task 7; ref15's worktree
   carries the same lines uncommitted), a fresh folder per round and side, per-shader medians, the spread rule.
10. Important, per-pass GPU time unmeasurable — applied through P17: 4a's GPU busy and GPU per frame, 5% band, SMITE 2
    graded on those two.
11. Important, bench mode, validity and reference — applied with 31: forward for all three, the load wait, the window
    check, one re-run of an invalid run, kits built once and swapped with sha256 checks, `state/<appid>.dxmt` aside
    per job, the reference built from ref15's MacNeutron commit in a detached worktree.
12. Important, the root-signature path never reaches Metal — applied: P2's evidence corrected; C8's and C11's
    `--pipelines` runs write `--out` and are checked with `NOMETAL=1` (`smite2-pl`, `c11-pl`) against 4a's frozen C++
    G6 tool's `--pipelines --out` folders instead of ref15's (equal bytes by 4a's identity proof, made once in Task 9,
    so the lanes have them before g0; a C11 trial wrote 114/114); the
    finding's second option taken: the pin waits for SMITE 2's bench (no deferral), so no Metal smoke tool is added.
13. Important, Task 12's re-run scope — applied with 28: the minimum re-run set for a key-moving fix, and the
    fork-fix route.
14. Important, Task 14 Step 5's pinned-build form — applied with 17: Z5's development build.
15. Important, g10 goldens unowned — applied with 32: Task 7 Step 5, run in g0 (Mac and native, one commit in
    `$B4/wt-mn`, `<M>` re-detached). Not taken: "the five new hashes equal b23's metallibs" — g10 compiles through
    winemetal's slots with its own chains and function names and has no `dxil-translate` twin; the cross-check is
    that only the ten `bitcode=` lines differ from 2c63baa's.
16. Critical, Task 13 reads state images that do not exist — applied: no `<appid>.img`, `state/<appid>.dxmt` named as
    the host cache folder. Rejected in part: "check `default.img` for 4 GiB, else stop" — 4a found it 15/16 full with
    SMITE 2's DLLs zeroed, so that check would stop every bench; the benches never run on it, and the 4 GiB check reads
    the prepared base (5's route).
17. Critical, Task 14 Steps 3 and 5 against a 22-branch `$D` — applied: both `ls-remote`s, the `tree_mode`
    development guard, `$D` detached at the pushed sha, a detached MacNeutron worktree at the pin, `<sha>+dev`, restore;
    branch removal moved after the build and never the last (Step 6).
18. Critical, the p row — applied: a local fetch of `step4b` into the p dxmt tree and a detached checkout (development),
    the four-tree guard, `MACNEUTRON_PROFILE=p BUILD_DIR=$B/p make wine-arm64` then the native build, the tree put back
    to its applied commit; "as 5a's g8 ran them" dropped.
19. Critical, Task 3's CK cannot pass before Task 5 — applied with 3: the ledgered `--skip` list for Tasks 3 and 4,
    empty in Task 5; the 3 → 5 → 4 order not needed.
20. Important, P16's combined path does not exist — applied: P9 and Task 6 take 5b's P3 rule (two rows), 4b's key
    value unchanged, 5b keeps only its test rows and cannot land first, the combined path deleted.
21. Important, pre-4b trees with post-4b scripts — applied: `build.sh` and `make llvm-nolto` check `$D/tools/llvm.sh`
    and die with P9's text; the bench reference from a detached worktree; Task 14 Step 7 tells 5a, 5b and 5c to rebase
    both branches together.
22. Important, a hard-coded reference — applied: ref15 = `git merge-base step4b main` after Task 10, built with the
    `dxmtrs` commit `step4b-mn` was rebased onto, both ledgered.
23. Important, the freeze's cost — applied: 5a lands before Task 10, the freeze runs from Task 10 through Task 12,
    5a's and 5c's controllers are told, and a later move of `main` means a re-rebase before Task 14.
24. Important, Tasks 6 and 9 in one worktree — applied: Task 9 in `$B4/wt-mn9` on `step4b-mn-tools`, merged by the
    controller; the LR reads Task 9's tools there until Task 10.
25. Important, Task 7's recipe rows test nothing — applied: `LLVM_SH`, RED and GREEN on Task 3's `tools/llvm.sh` (in
    `$B4/wt-l` or a detached worktree at `step4b`), Task 7 depends on 3, Task 11's row with `$D` at `step4b`.
26. Important, `build_test.sh` without `BUILD_DIR` — applied: Global Constraints' Builds item.
27. Important, Task 2 links a file Task 7 ports — applied: `git show 2c63baa:…` into `$B4/dxil-translate-15.mm`.
28. Important, stale seeds after a fork fix — applied with 13: P11, g0 step 1 (`rm -rf` before `cp -cR`), Task 12's
    route.
29. Important, `make llvm-nolto` reads `$D` unchecked; RC paths — applied: the lock job checks `$D` out at `step4b`
    first, the Makefile dies without the recipe, RC uses `$B/…/llvm23-arm64-nolto` and `$M/…/xcrun-metal.sh`.
30. Important, commits in `<M>` without the `## dxmtrs` guard; `merge --ff-only` in `$D` — applied: Global
    Constraints' rule 9 item and `git -C $D fetch . step4b:main`.
31. Important, VM mode, guest rebuild, warm caches — applied with 11. Taken differently: the guest side and image are
    built once at `step4b` into a common kit, as 4a's recipe did, not at each side: the runs are forwarded, so the
    guest's DXMT never translates, and one image keeps the prefix update from firing between runs.
32. Important, g10 re-record unowned — applied with 15.
33. Important, Task 7's grep survivors — applied: `wine-arm64/README.md` and the `check.sh` comment join Task 7's
    files; the comments the grep also hits at `build.sh:12,487`, `Makefile:169`, `dxmt-native/build.sh:5` and
    `dxil-translate.mm:10` become Task 7 edits; the grep's allowed output lists the lines Tasks 6 and 8 own and the two
    expected survivors (`fdiv.hlsl:2`, `air-tools.sh:4`); the pattern also matches `llvm-arm64` at the end of a line.
