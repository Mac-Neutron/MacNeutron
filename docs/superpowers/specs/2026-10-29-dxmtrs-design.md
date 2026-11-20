# DXMTRS: DXMT in Rust, then one project for D3D9 to D3D12

Status: approved 2026-10-29; revised 2026-10-30 (D3D9's place and full convergence: §1, §2, §3, §5 steps R and 3,
§6, §7, §8, §9; approved by the maintainer 2026-10-30). Phase 0 ran the same day (plan
`docs/superpowers/plans/2026-10-29-dxmtrs-phase0.md`):
Mac-Neutron/DXMTRS exists and MacNeutron builds from it; the results and the translator decision are in §4.
Related: `2026-09-28-macneutron-dxmt-fork-design.md` (our DXMT fork), `2026-09-29-macneutron-dxil-translator-design.md`
(our DXIL translator), `2026-10-20-macneutron-d3d9-mtld3d-design.md` (D3D9 through mtld3d),
`2026-10-17-macneutron-vm-d3d-forwarding-design.md` (the VM host's native DXMT), `2026-10-14-macneutron-framegen-metalfx-design.md`.

## 1. Goal

DXMT (our fork, ~100k lines of C, C++ and Objective-C under `src/`) is rewritten in Rust, step by step, in a new
repository, Mac-Neutron/DXMTRS, which also holds D3D9 (mtld3d's Rust, at `src/d3d9` since step R). D3D9, D3D10, D3D11
and D3D12 end as one project built the same way: one layout and build, one Rust core, one PE/Unix bridge (winemetal),
one shader translator (AIR), one configuration, logging, statistics and shader-cache scheme, and one presenter
(MetalFX upscaling, HDR, frame pacing and frame generation) for every API (§7).

Done when:
- DXMTRS has no C, C++ or Objective-C of DXMT's left (generated headers for outside users aside, §6), and cargo builds
  every binary MacNeutron ships: ARM64EC PE DLLs (MacNeutron's Mac build; DXMT's own cross file makes ARM64X), x86_64
  and i686 PE DLLs, the macOS Unix side, the native
  macOS build the VM host uses (DXMT_NATIVE), and the VM guest's builds.
- D3D9 runs on the shared core, bridge, translator and presenter (§7); `mtld3d.dll` and `mtld3d.so` are gone.
- Every gate of §8 passes, and performance is at parity or better (§8).

Not in this project: moving D3D10-12's command encoding to the Unix side; forwarding D3D9 in the VM; new D3D features.
Each would be its own decision. (D3D9's shaders join the shared translator: maintainer, 2026-10-30, §7.)

## 2. Decisions (maintainer, 2026-10-28 and 2026-10-29)

| Question | Decision |
|---|---|
| How to get there | One project, ported in steps: DXMT is replaced module by module behind its existing C and COM boundaries, adopting mtld3d's shared pieces as it goes; D3D10-12 keep DXMT's encoding design; checks green at every step. |
| Shader translator | "Rust over LLVM if it's faster than MSL, but use the latest version of LLVM instead of 15." After E2 showed Metal refuses opaque-pointer AIR, the maintainer chose (2026-10-29) LLVM 23 with our own AIR writer over LLVM 16 or 15 (§4). |
| Upstream | Followed by hand: upstream DXMT and mtld3d changes are re-implemented in DXMTRS when they bring parity or better; the merge-upstream flow for DXMT ends. |
| Home | A new repository, github.com/Mac-Neutron/DXMTRS. |
| Design (chat, 2026-10-29) | Approved: this document writes it down, with the corrections of §5 and §4. |
| D3D9's place (maintainer, 2026-10-30) | "mtld3d gets moved to src/d3d9 and implemented the same way the other versions of DirectX are implemented … as consistent as possible … not a subproject anymore but part of DXMTRS like the rest of the code." Asked how far, the maintainer chose full convergence: `d3d9.dll` on the same Rust core and winemetal bridge as D3D10-12, D3D9's shaders through the AIR translator via an SM1-3 front end (mtld3d's MSL path retires), and layout, build, configuration, logging and presenter shared. |
| Timing of the move (controller's default, approved with this revision 2026-10-30) | The layout and build move (step R, §5) comes before step 3, so nothing moves twice. It replaces "mtld3d's scripts first change at the merge" (2026-10-29): MacNeutron's scripts change once, in step R's pin bump. The runtime and shader convergence follow steps 4 and 5 (§5, C2-C5). |

Assumption to confirm: "D3DMtl" is mtld3d (athei/mtld3d, already vendored at our DXMT fork's `mtld3d/`).

## 3. The repository and its build

- **Seed.** DXMTRS is created from our dxmt fork's `main` with its history, so it starts as exactly the tree MacNeutron
  ships, mtld3d included. Our dxmt fork is then frozen (kept: it holds the LGPL source of every release so far).
  MacNeutron's `dxmt/pins` switches `DXMT_REPO` to DXMTRS at its first commit, so every later step goes through
  MacNeutron's checks and can ship.
- **Layout (step R, 2026-10-30).** Every component lives under `src/<component>/`: its `meson.build` while meson lives,
  its `.def` and `.rc`, its C/C++ while any is left, and its Rust. The crate that becomes a component's PE binary has its
  `Cargo.toml` at the component root (`src/nvngx`, `src/nvapi`, `src/winemetal`, `src/d3d9`, `src/dxgi`,
  `src/d3d10`); its Unix library's crate sits in `unix/` beside the `meson.build` that links it (`src/winemetal/unix`,
  `src/d3d9/unix`); its other crates in named subdirectories (`src/winemetal/wire`, `src/d3d9/{shim,core,types,shared,mtl}`).
  Rust several components use lives with the component that owns the concept (`src/util`: `dxmt-util`) until §7 folds
  it. D3D9 is `src/d3d9` like the others, with no build, documents or CI of its own: its test crates are `tests/d3d9/`
  (beside DXMT's test programs), its documents `docs/d3d9/`, its conventions and audit the tree's (`docs/CONVENTIONS.md`,
  `tools/audit.sh`). One Cargo workspace at the root (`Cargo.toml`, `Cargo.lock`, `.cargo/config.toml`, `rustfmt.toml`,
  `vendor/`): every build names its package (`-p`) and target, so one workspace serves every target family. One Rust
  toolchain pin (1.99.0, MacNeutron's `MTLD3D_RUST`), plus a pinned nightly for rustfmt only, one dependency policy.
- **Rust inside the C++ DLLs.** Ported code reaches the remaining C++ as a Rust `staticlib`, exactly one per final
  binary (two Rust staticlibs in one DLL would each carry the standard library), which re-exports that binary's ported
  modules through `extern "C"` functions and COM vtables. PE targets use Rust's `*-pc-windows-msvc` targets, linked by
  meson with llvm-mingw (DXMTRS's `tools/cargo-staticlib.sh`, and `rust_runtime_dep` for what MSVC's runtime has and
  llvm-mingw's lacks). When a binary has no C++ left, meson links it from its crate's static library and a `.def` of
  its exports (nvngx, step 1); when none has, meson goes.
- **Build ownership.** From step R, DXMTRS's meson builds every binary MacNeutron ships, D3D9's included: `d3d9.dll` and
  `mtld3d.dll` (ARM64X Wine builtins on the Mac, linked by `tools/arm64x-link.sh` because MacNeutron's ARM64EC cross
  file replaces `-marm64x`, which LTO refuses; x86_64 and i386 builtins in the VM guest, linked as `nvngx.dll` is), the
  Mac `mtld3d.so` and the guest's x86_64 and i686 Linux `mtld3d.so` (cargo `cdylib`s; the guest's sysroot comes from the
  environment). `tools/` holds what MacNeutron used to supply as scripts (the mingw compiler wrapper, the Linux cargo
  setup, the ARM64X link) and the Rust gate (`tools/rust-check.sh`, §8). D3D9 is built in every cross build
  (`src/meson.build`) with no new meson option, so the translator key does not move. MacNeutron's build scripts keep
  fetching the pinned commit and toolchains, fetch the locked crates (`cargo fetch --locked`) and run meson; step R
  changes them once, in its pin bump, and later steps are a DXMTRS change plus a pin bump. `llvm.sh` moves with step 4:
  it is an input of the translator key.
- **Every build stays first-class at every step**: ARM64EC (Mac), x86_64 and i686 PE; the macOS Unix side; DXMT_NATIVE for the
  VM host; the x86_64 and i686 PE DLLs and mtld3d's Linux libraries the VM's guest stages make.

## 4. Phase 0: three experiments before any porting

Each experiment's code is throwaway, kept on a branch for the record; its result is written into this document.

**E1, toolchain.** Extend mtld3d's recipe to DXMT's binaries: one Rust staticlib exporting a trivial function, linked
into each of `winemetal.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `d3d10core.dll`, `nvapi64.dll`, `nvngx.dll`, the
Unix side and the DXMT_NATIVE build, for every target of §3, and called from their C++. Passes when all of them build,
`dxmt/check.sh` passes in both lanes, and the VM guest stages build. This mostly confirms what mtld3d already does
(ARM64X and i686 PE, Linux) for DXMT's DLL set.

**E2, AIR from the latest LLVM.** The translator runs on the Unix side today (`winemetal/airconv_thunks.c` forwards to
it), and the VM guest's shaders are translated by the host's DXMT, so only one LLVM build is involved: macOS arm64. Apple's own compiler (metal 32023.921,
`air64-apple-darwin27.2.0`) still writes typed pointers (`float addrspace(1)*`); LLVM 17 and later write only opaque
pointers, and newer LLVMs also write newer attributes (for example `captures(none)` where older ones wrote `nocapture`).
Whether Metal loads such AIR is unknown, so:
1. Take airconv's current output for the shader corpus and DXMT's test shaders, read it with the latest LLVM release
   (23.1 today; its bitcode reader upgrades typed pointers to opaque ones) and write it back, then load each metallib
   with `newLibraryWithData:` and build its pipelines.
2. Run DXMT's check suite against the round-tripped shaders and compare its output with the original's.
3. If Metal refuses, name what it refuses (opaque pointers, or a newer record) by writing the same IR with only that
   difference.
Also in E2: list every LLVM C++ API airconv uses (IRBuilder, metadata, the new pass manager, bitcode reader and writer)
against LLVM's C API, which `llvm-sys` binds. Whatever the C API lacks goes into a small C++ shim compiled by the
translator crate's `build.rs`; the C API is declared by hand in the crate, without `llvm-sys` (§5 step 4a, 4-D2), and
the build links the LLVM libraries `llvm.sh` builds.

**E3, AIR against MSL.** "Faster" is decided by shader build latency (bytecode to `MTLLibrary` to pipeline state, which
is what players feel as stutter), provided the GPU time of the resulting shaders is at parity. Measured on shaders that
have HLSL source (DXMT's tests, plus any HLSL sample set): AIR through airconv, against MSL from DXC's SPIR-V output
through SPIRV-Cross (development-only tools, never shipped; fetching SPIRV-Cross needs the maintainer's yes), timed in
a native harness like the frame-generation one, each run once.

**What decides the translator:**

| Result | Translator |
|---|---|
| Metal loads latest-LLVM AIR; AIR builds faster than MSL; GPU time at parity | Rust, through `llvm-sys` (plus the shim, if any), on the latest LLVM release `llvm-sys` supports. |
| Metal refuses opaque pointers | Rust, on the newest LLVM that still writes typed pointers: LLVM 16 (opaque pointers switched off), confirmed in E2. |
| MSL builds faster, or AIR's GPU time is worse | Back to the maintainer before the translator is ported. |

**Results (2026-10-29).**

- **E1 passes.** One Rust staticlib (`mtld3d/windows/dxmt-pe`, `mtld3d/unix/dxmt-unix`, built by DXMTRS's
  `tools/cargo-staticlib.sh` from meson) is linked into every DXMT binary of the Mac build, the VM host's native build
  and the VM guest's x86_64 and i686 builds. It runs on the Mac (both `dxmt/check.sh` lanes pass, 810 rows, and a probe
  that fails on request stops a test program from starting) and in the native build (its test check passes); the guest
  DLLs build and link but did not run in Phase 0; step 1 ran the guest's x86_64 nvngx and step 3
  its i686 dxgi and d3d10core, some shims unexercised (§5 step 1). MacNeutron's Mac DLLs turned out ARM64EC alone,
  not ARM64X (`wine-arm64/build.sh` replaces `-marm64x`, which doesn't combine with LTO). Rust's `-msvc` standard library
  needs ntdll on every PE target, and on x86 what MSVC's runtime has and llvm-mingw's lacks (`__chkstk`, `_fltused`,
  and on i686 `__tls_array` and Wine ntdll's undecorated `__aulldiv`): `src/util/rust_pe_rt.c`. `d3d12.dll` grows by
  84 KiB. New guest imports: `ntdll.dll` and `api-ms-win-core-synch-l1-2-0.dll` on every DLL, and
  `api-ms-win-crt-private-l1-1-0.dll` on `winemetal.dll` (the other DLLs had it), which Wine's API-set schema resolves.
- **E2: Metal refuses opaque-pointer AIR.** On DXMT's 64 test shaders (there was no game corpus; capturing one needs a
  SMITE 2 session of the maintainer's), airconv's AIR rewritten by LLVM 23.1.4 loads 0/64 ("Failed to upgrade function
  bitcode" for 41, and 22 crash Metal's compiler service on LLVM 23's newer records); rewritten by LLVM 15 with only its
  pointers made opaque, 0/64; rewritten by LLVM 15 with typed pointers, 64/64. (That LLVM 23's crashes come from its
  newer records is an inference from the difference between those two runs, not isolated.) LLVM's own DirectX backend writes
  typed-pointer bitcode from opaque IR (`DXILBitcodeWriter`, `PointerTypeAnalysis`, in LLVM's old 3.7 record format):
  with two writer fixes (a pointer-valued load's type, the AIR triple and data layout kept) and DXILPrepare's casts,
  35/64 loaded and built pipelines (no result file of that run is on disk; unreproduced, K18);
  typing pointer selects and phis from their result and nulls through their use (the committed spike) traded crashes
  for read errors, 27/64 (`build/phase0/e2/use-23t.txt`, the spike's result on disk). Either way Metal accepts that
  record format. The rest fail on what inference can't recover from opaque IR (argument-buffer arguments typed `i8*`,
  struct fields losing their address space) and on what the old format can't say (`reassoc nsz arcp contract` widened
  to `fast`, which adds no-NaN and no-Inf; `afn` dropped; `poison` as `undef`). Spike code: DXMTRS branch `phase0`
  (d2e6d75, `phase0/air-roundtrip`), our llvm-project fork's branch `phase0-air-writer` (02e8d34). Another result on
  disk, `build/phase0/e2/use-15dx.txt` (0/64), comes from a tool, `air-roundtrip-15dx`, built against an unknown tree
  and is not evidence (K18). Every LLVM C++ API airconv uses has an LLVM 23 C API equivalent except the bitcode writer
  (`phase0/llvm-c-api.md`); airconv's two custom passes and `splitBasicBlock` have none either: step 4a keeps them in
  its C++ shim, the passes through 4b (§5 step 4a, 4-D3).
- **E3: AIR builds 5 to 25 times faster than MSL, before the backend.** 45 pipelines built both ways (DXC's SPIR-V and
  SPIRV-Cross's MSL 3.2 of the 19 HLSL test shaders, against airconv's AIR of the same shaders); each MSL source salted
  so its frontend compile isn't a cache hit (two identical sources still shared one); two runs agree within 1%. Metal's
  MSL frontend, which AIR skips, took a median 3.2 ms and a mean 15 ms a pipeline (the first, 230 ms, includes the
  compiler service starting); airconv's whole path, translation included, takes 0.6 ms a shader (`dxil-translate`).
  The backend compile, which both paths pay, was a cache hit in both and is not measured. GPU time is waived, not
  moved: the test shaders are functional tests, binding airconv's argument buffers outside DXMT would reimplement its
  binding model, and no MSL translator exists to compare at game scale (§8 compares LLVM 15's AIR with LLVM 23's).

**Decided (maintainer, 2026-10-29).** The translator is Rust on LLVM 23 (the newest release; its C API declared by hand
in the crate, no `llvm-sys`: §5 step 4a, 4-D2), emitting AIR through **our own AIR writer** in Mac-Neutron/llvm-project:
a typed-pointer bitcode writer grown from LLVM's DXIL writer, to which the translator hands the pointee types of its
functions, globals and structs, the writer deriving the rest from definitions and uses with no constrained pointer
defaulting to `i8*` (amended at step 4W: F1, P4, P5), and which writes the newer records Metal reads (the full fast-math
flags, `poison`). The writer is one writing entry point and one type constructor the Rust translator calls (amended at
step 4W: P3). It got its own plan, 4W, before §5's step 4 moves the translator off LLVM 15, gated by 64/64 on the test
shaders, then the corpus and the check suite on its output: done 2026-10-31 (§5 step 4W). As built (the 4W plan's P
items and findings F1-F3):
- **Branch.** `air-23` in Mac-Neutron/llvm-project, from `llvmorg-23.1.4` (7a941df), not from the fork's `main`, which
  is trunk with our lld commit and pinned by llvm-mingw; the writer is forward-ported when the translator's LLVM moves,
  and `phase0-air-writer` stays as the record (P1). At da8441e28e (4W) and edc723f40d (4b, pushed: f8ae77ad29 writes
  vector `ConstantFP` splats as LLVM 15's `CST_CODE_DATA` records, fixing F-D5-1; edc723f40d adds `llvm.fabs.v2f32` to
  P13's list for `dxbc-320f`'s `grad.dxbc`): a component of its own, `LLVMAIRBitWriter`
  (`llvm/lib/Bitcode/AIRWriter`), its writer and enumerator copied from the DirectX target's at 7a941df with the
  spike's two fixes, about 4.2k lines that inherit no upstream DXIL fixes; DirectX untouched, and it builds with
  `LLVM_TARGETS_TO_BUILD=""` (P2); the tool `llvm-air-write`; 47 lit tests in `llvm/test/Bitcode/AIRWriter`.
- **Interface** (P3, 4-D2). `llvm-c/AIRWriter.h`: `LLVMWriteAIRBitcode` writes a module as typed-pointer AIR, Darwin
  wrapper included, and `LLVMAIRTypedPointerType` makes the typed pointers (LLVM 23's `TypedPointerType`) the C API
  cannot; four structs (`LLVMAIRValueType`, `LLVMAIRStructBody`, `LLVMAIRTypes` with the helpers' typed bitcode as
  `TypedRefs`, and `LLVMAIRWriteStats`'s six counters, `RefsFilled` among them), which 4b declares by hand with size
  and offset asserts. The module is verified first, then modified (casts, drops) and must not be written again. Errors
  come back as text and nothing aborts, with assertions off as on (P17, 4-D5); the library holds no mutable static, so
  compiles run concurrently.
- **A type-level contract** (P4; finding F1 against "the pointee type of every pointer it creates", which cannot hold
  after the pass pipeline: SROA, InstCombine, GlobalOpt and `deadargelim` replace values, functions and globals between
  their creation and the write, so a handle kept from creation dangles). At write time the translator hands a typed type
  for every function, global and identified struct that holds a pointer, and the helper bitcode it links as typed refs,
  built after the passes by walking the module. An entry whose value the passes deleted, or whose erasure no longer
  matches, is left out by the translator and counted, and P5 derives it (`llvm-air-write --passes` leaves out and counts
  such entries as `stale`, P15); the writer's erasure check stays an error (Task 10's hand-off). Per-value entries are
  accepted while their values are alive.
- **Derivation and casts** (P5, P6). A pointee type comes from the table, then the refs by name, then the definition
  facts opaque IR keeps (alloca, global and GEP source types, loads, the call's typed function type, arguments, casts,
  aggregates), then one worklist fixpoint over use edges anchored in those seeds, a handle winning ties. A pointer no
  seed reaches is typed `i8` in its address space and counted (`UseTyped`); no constrained pointer defaults to `i8*`.
  Where a use needs another pointee type the writer inserts a same-address-space `bitcast`, and a constant gets a
  typed instance per type, each under its own `SETTYPE`; it never invents an addrspacecast. `UseTyped` is 0 on every
  corpus in every leg, SMITE 2's included: the ceiling 4b may not exceed (at 4b: P5's literal-to-identified struct option
  was not taken, 4b P2, and L4's untyped list was reconciled at `UseTyped` 0 on every corpus without per-value entries,
  4b P3).
- **Records and attributes** (P7, P8; K8 widened, finding F2; 4b P20). The DXIL writer's 3.7 layout stays (Metal reads it, E2),
  with LLVM 15's encodings where 3.7 loses meaning: the fast-math flags as LLVM 15 splits them (`afn` kept),
  `local_unnamed_addr`, `poison`, `freeze`, `fneg`, `atomicrmw`'s value type, `source_filename`, the hash, the Darwin
  wrapper and an identification block naming `LLVM15.0.7`, the only one Metal has accepted. On the translator side (4b
  P20), `Context::attributes15` inside `get_or_insert_function` maps the six LLVM 15 memory attribute names the AIR
  tables carry (`argmemonly`, `inaccessiblememonly`, `inaccessiblemem_or_argmemonly`, `readnone`, `readonly`,
  `writeonly`) to LLVM 23's `memory(...)` attributes, which the writer maps back to the same LLVM 15 attribute groups.
  Attributes use LLVM 15's
  whole kind table (codes up to 84), not the map's six (the corpus also holds `speculatable`, `nocallback` and
  `convergent`): `memory(...)` becomes LLVM 15's forms, exact or the strongest sound weaker set, counted;
  `captures(none)` becomes `nocapture`, and any other `captures(...)` is dropped and counted; LLVM 23-only kinds that
  only add guarantees (`nofpclass`, `range`, `writable`, `dead_on_unwind`, `initializes`, `dead_on_return`,
  `nocreateundeforpoison`) and LLVM 23-only flags (`nneg`, `disjoint`, `samesign`, trunc `nuw`/`nsw`, GEP
  `nusw`/`nuw`/`inrange`, fast-math flags on casts and on phi, select and call of a literal struct of one FP type) are
  dropped and counted; any other kind is an error. Every drop is applied to the module before the write, and the module
  left is what LLVM 23 reads back from the bytes (intrinsic declarations end with LLVM 23's table attributes and
  lifetime markers in LLVM 23's form, as its reader upgrades them: R3-2, D3). The lists are re-checked at every LLVM
  bump.
- **What LLVM 15 cannot say** (P13, P17, rulings D3-D5). Every `llvm.*` declaration must be on a closed list of
  intrinsics whose LLVM 23 signature equals LLVM 15's (the lifetime markers excepted, written in LLVM 15's form, D3);
  each new name needs a ruling. LLVM 23's sizeless lifetime markers are written as LLVM 15's, the size taken from the
  alloca; `llvm.scmp` and `llvm.ucmp` become `icmp` and `select`. InstCombine's `<128 x i1>` bit tests (`bitcast` or
  `load`, then `extractelement`), which crash Metal's compiler service, are written as LLVM 15's word extract, `and` and
  `icmp ne`; any other i1-vector bitcast or load is an error. Scalable vectors, `callbr`, EH pads, `ptrtoaddr`, operand
  bundles and debug info are errors.
- **The oracle and the gate** (P9, P18; finding F3). No Rust: the C++ translator's LLVM 15 metallibs are dumped to
  typed text and a type table (`air15`, MacNeutron `dxmt/tools`), read with LLVM 23, written by `llvm-air-write` and
  read back with LLVM 15 (`air-oracle.sh`). Three legs: full (the whole table, identical typed text), defs (only the
  type-level table 4b can supply, the helpers typed by their refs) and opt23 (LLVM 23's passes first, as 4b runs them:
  a harness that only round-trips LLVM 15-optimized AIR never sees LLVM 23's `i8` GEPs or its new flags, F3). Every
  leg checks that the bytes read back as the module the writer left, LLVM 15's verifier, the record inventory, the
  wrapper, and Metal building every pipeline from our bytes alone; defs and opt23 also compare the writer's input with
  its output up to the inserted casts and the counted drops. The gate (2026-10-31, the release build, assertions off):
  45 of 45 legs exit 0 on the test shaders (96 metallibs, Metal 64/64), the DXIL tests (33), 4a's DXBC corpora at
  Metal 3.1 and 3.2 (4, 243 and 570 metallibs), the helpers, and SMITE 2's capture (16,092 metallibs of 10,271
  shaders, Metal 10,271/10,271 in every leg); typed text is identical in the full leg up to §5 step 4W's rulings, two
  processes and a threaded batch agree, and every comparator's mutation fires. The check suite on our AIR (P18):
  `d3d12_dxil_exec`'s six rows ran cold with the C++ AIR, then warm with our AIR rewritten in place into the shader
  cache (34 of 34 metallibs). All 38 `expect` lines pass both times, the warm run hit every stored metallib (two refused
  pipelines miss in both runs) and left the cache unchanged, and its output equals the cold run's under ruling R9-1:
  per-run lines normalized, the cache counters compared as lookup totals, the contended 64-bit atomics' returned old
  values (slots 8-12 of `exec-a64`'s `atomic64`, `atomic64typed` and `gsatomic64`), which two C++ runs also differ in,
  graded per run by `expect.py` and not compared across runs, and every other line and word compared exactly.

## 5. The order of the port

Ported code replaces C++ only behind a boundary that is already a C or COM interface, or one first cut inside the C++
as its own commit. That rules out the chat design's "util first": `util` is mostly C++ templates (`com`, `rc`,
`objc_pointer`, `util_bit`, the thread pool) that C++ callers instantiate inline, so it goes away with its last C++
caller. Its functions with a plain interface (configuration, environment, logging) are ported early, behind their
existing C++ signatures.

1. `nvngx`, a whole DLL with C exports: done 2026-10-29 (plan `2026-10-29-dxmtrs-step1-nvngx.md`), its D3D11 path
   characterized first (`d3d11_dlss`). Its gates ran the VM guest's x86_64 `nvngx.dll` (`eac-vm/dxmt/gate.sh`'s DLSS
   programs), Rust's first run in the guest. Its `__chkstk` shim links but its callers there (writes to a console,
   `io::Error`'s text, the panic printer) didn't run with stderr in a file, so the shim is still unexercised; `_fltused`
   matters only at link time. Step 3 closed the i686 `__aulldiv`: the i386 `dxgi.dll` imports Wine
   ntdll's `_aulldiv`, which its mode filter and two of its sorts call. `dxgi-guest.sh i386` passed, and so did
   d3d-smoke's `32: wmsize` (a window through full screen and `ResizeTarget`) in both WoW64 modes. `__tls_array`
   resolves (the DLLs read `%fs:0x2c`) but stayed unexercised until step 5a: the i386 `dxgi.dll` and `d3d10core.dll`
   read it only in Rust's stderr write, which their logger uses only where ntdll has no `__wine_dbg_output` (outside
   Wine), and on a panic; step 5a's i386 `d3d12.dll` keeps its statistics in a `thread_local!`, which ran in the VM.
   1b. `nvapi`, its own plan (`2026-10-29-dxmtrs-step1b-nvapi.md`): done 2026-10-29: DXMTRS main a7df84b,
   which MacNeutron pins. It answers 46 functions behind 54 IDs, 8 of them null on purpose, and logs any
   other of `nvapi_interface.h`'s 511 IDs; the first draft's 94 was wrong. Characterized first by `d3d11_nvapi` (a
   152-line golden recorded from the C++ DLL), then ported with three decisions:
   - a null pointer the C++ would dereference gets `NVAPI_INVALID_ARGUMENT` (-5) instead of a crash. The list:
     `GetErrorMessage`'s string, `D3D11_IsNvShaderExtnOpCodeSupported`'s out pointer, `D3D11_SetNvShaderExtnSlot`'s
     device, `DISP_GetDisplayIdByDisplayName`'s name and its id (the id only after the name is found: an unknown name
     still gets -6, as in the C++), `D3D_GetObjectHandleForResource`'s handle, `Disp_GetHdrCapabilities`'s and
     `Disp_HdrColorControl`'s struct, and `GPU_GetLogicalGpuInfo`'s `pOSAdapterId`. Every other quirk stays:
     `GetErrorMessage` writes an empty string. `EnumNvidiaDisplayHandle`, `DISP_GetGDIPrimaryDisplayId` and
     `GPU_GetConnectedDisplayIds` null-check their output in the C++ and in the Rust:
     they write it when given a non-null pointer (as the first probe run recorded) and return -5 for a null one; none
     writes through null (the plan's first draft wrongly listed them as exceptions);
   - the count: 46 functions behind 54 IDs, not 94;
   - `dxmt-util` (`mtld3d/windows/dxmt-util`) holds what nvngx and nvapi share (logging, COM helpers, the NVEXT check),
     and §7's merge folds it into the shared crates.

   The C++ log files end lines in CRLF (text-mode streams on Windows). `dxmt-util` does the same for the file and for
   stderr outside Wine, and the C++ and Rust log files are byte-identical; this is part of the step.

   Gates, on the CRLF tree (1befd21): both Mac `check.sh` lanes 412 ok and 0 FAIL (x64 re-run on 1befd21, arm64ec on
   e39014e); the C++ and Rust DLLs' `all` dumps and log files identical; the VM guest's x86_64 `nvapi64.dll` (195 KB,
   Rust; the C++ one is 1.3 MB) passed `eac-vm/dxmt/gate.sh`'s seven programs, `d3d11_nvapi` 152 of 152. The review's
   final fixes (up to a7df84b: log file naming, creation only on a passing message, the name-lookup status order,
   docs) were gated the same way: x64 412 ok, identical dumps and log files, no log file at `DXMT_LOG_LEVEL=none`,
   the guest's seven programs. Another finding: mtld3d's `audit.sh` now runs from `mtld3d/` (it `cd`ed to the git toplevel,
   which inside DXMTRS is the repository root, and died partway, so its later rules never ran).
2. `winemetal`: done 2026-10-30 (plan `2026-10-29-dxmtrs-step2-winemetal.md`): DXMTRS main da6ed2d, which
   MacNeutron pins. All 158 Unix handlers, the 158-entry Wine table and the 153-entry WoW64 table are Rust (crate
   `winemetal`, `mtld3d/unix/winemetal`), and so are `winemetal.dll`'s thunks and `DllMain` (crate `winemetal-pe`);
   `winemetal_unix.c`, `cache.c` and `main.c` are gone. `winemetal.h` stays the boundary the C++ calls. Characterized
   first by `wm_probe`, which drives every slot and records, per field, outputs, ownership, the autorelease pool and
   the thread, with goldens taken from the C. Corrections to the step as first written (the plan's D and P items):
   - the Unix side stands on objc2 0.6.5 and objc2-metal, -metal-fx, -quartz-core and -foundation 0.3.2 directly, not
     on mtld3d's `mtl` crate, which is a client of this table (D1); SQLite through raw FFI, files and lookups
     byte-identical;
   - "onto mtld3d's transport" means Wine's unix-call handshake and typed calls inside `winemetal.dll`, with
     winemetal's own slots, layouts, module name and exports, not mtld3d's `Thunks` or `mtld3d.dll` (D2);
   - one pure-data crate, `winemetal-wire`, holds the wire mirrors, layout asserts, slot numbers and macro encodings
     for both sides (P1); `wmt.rs`'s mirrors stay in `mtld3d-mtl`, a duplicate listed for §7 (D16);
   - the Mac WoW64 table keeps its 153 entries (D3); `thunk32_*` and the argument-chain converter moved verbatim into
     `src/winemetal/unix/sm50_wow64.c` (Wine build only): nothing reaches them, and they stay until step 6
     (P4, corrected at step 4a: 4-D3);
   - Objective-C exceptions behave as before (D4): the crate builds with `panic = "unwind"` (profile
     `production-unwind`) and its handlers are `extern "C-unwind"`; each holds `AbortOnPanic`, so a Rust panic aborts
     and never unwinds into the VM host's `catch (...)`, while a foreign Objective-C unwind passes (P11);
   - code under OS gates below macOS 27 is not ported: slot 56's signal swap, the pre-27 HUD class and the
     `@available` 14-26 branches (D6);
   - slots 96, 97 and 101 read AppKit off the main thread as the C did, through `AnyThread` re-declarations under an
     audit exception (D14);
   - the native dylib keeps `winemetal_thunks.c` and its 12 `SM50*` exports until step 6 (D12). Both images link
     `-weak_framework AppKit`, and the dylib links with `b_asneeded` false, because objc2 looks classes up by name
     (P14);
   - the Rust PE thunks zero every params struct, so slot 54's `startCapture`, which returned uninitialised stack,
     now answers false (P5);
   - the SharedEventListener's use-after-free is gone: its state lives beside its first word, and a notification
     after destroy gives back its reference instead of spinning (D5, P12). A null input the C faulted on gets a null
     check that writes nothing;
   - `cfg(windows)` Rust gets rustc's `-Dwarnings`, not clippy: 1.99.0 has no clippy and stable no msvc standard
     library. Clippy for 1.99.0 is a download that waits for the maintainer (P13);
   - the translator key moved with step 1's pin (e1ac8f03 to 71a7e5f2), accepted; step 2 leaves it alone (D15);
   - no game bench (D11): performance was C against Rust on `present_loop`, the DLSS cost row, `check.sh`'s timing
     lines and the probe's call rate, each within 5%. One ARM64EC warm pipeline-creation reading was +24% in a single
     run, while the same Unix path halved on x64. The game bench is parked for the maintainer.

   Gates, once at the end of the step (maintainer, 2026-10-30): both Mac `check.sh` lanes 412 ok and 0 FAIL, the
   wine-arm64 DXMT checks, the presenter and facade tests, the probe (fixed and full), the native gate and probe, the
   VM host's metal, host and memory tests, the loopback gate, `d3d9-check`, the conformance suite (as the C), the
   guest build, the VM DXMT gate (44 programs), `d3d-smoke` (the first i686 Rust to run), `d3d9-gate` and
   `present-gate`. `winemetal.dll` imports 38 more symbols, all from Rust's standard library and all present in Wine.
   The final review found one defect, fixed before the push: an empty archive path aborted slots 112 and 113 (objc2
   panics on a nil from an init declared non-null). It is now a nil URL, as in the C.
R. **Layout and build** (maintainer's direction, 2026-10-30; before step 3, so nothing moves twice): done 2026-10-30,
   DXMTRS main 1ccec40, which MacNeutron pins. mtld3d becomes
   `src/d3d9` (§3): every file moved with `git mv` (history kept), one root workspace, D3D9 built by meson for every
   target, mtld3d's Makefile, CI, standalone bundle, release machinery and `INSTALL.md` retired (kept in history), its
   check legs one script (`tools/rust-check.sh`, §8), its conventions and audit tree-wide, its documents under
   `docs/d3d9/`. Nothing a player or MacNeutron's launcher sees changes: the same DLL and library names, exports,
   builtin markers and ARM64X images, variables (`MTLD3D_CONFIG`, `DXMT_*`), configuration files, log locations, shader
   caches (D3D9's emitter version is computed from path-independent names, so `mtld3d_shaders.bin` stays valid),
   package names and crate versions; the translator key does not move. MacNeutron changes its build and test scripts in
   the same pin bump. One deliberate change, its own commit: every production C/C++ is built with NDEBUG (the guest's
   Linux `mtld3d.so` carried 8 of snmalloc's assertion messages, now 0; its PE DLLs had none). The guest's CPU baselines (`x86-64-v2`/`pentium4` for D3D9,
   none for DXMT's C++) are kept at R and aligned later in their own measured commit. Plan:
   `2026-10-30-dxmtrs-stepR-layout.md`. Corrections found while doing it: the 11 files D3D9's emitter version hashes
   stay byte-identical through R (three stale comments in them wait for C3, which retires the emitters); the rustfmt
   nightly is pinned as `nightly-2026-10-30` (`MTLD3D_RUST_NIGHTLY` in MacNeutron's `dxmt/pins`); `tools/rust-check.sh`
   has no Linux leg, so Linux-only Rust stays unlinted as before; the guest's `d3d9.dll` resource is compiled by
   llvm-windres instead of llvm-rc, with equal VERSIONINFO; `vendor/snmalloc-sys` carries the crate's own MIT notice and
   Microsoft's notice for its C++ (`vendor/snmalloc-sys/upstream/LICENSE`); meson's guest DLLs differ from the old
   hand-linked ones only in fields no loader reads (subsystem, file alignment, a `.buildid` section), accepted. Every
   shipped binary's identity table matched the one saved before the move, the translator key and D3D9's emitter
   version did not move, and every gate of step 2 passed (each flaky row on its one re-run).
3. `d3d10core`, then `dxgi`: whole DLLs behind COM (`src/d3d10`, `src/dxgi`). Only `d3d10core.dll` is a D3D10 DLL: the
   D3D10 object layer (17 classes) is compiled into `d3d11.dll` and lives inside its objects, so it moves with D3D11's
   front end at step 5, and `d3d10_blob.cpp` (linked only into `d3d12.dll`) with D3D12 (corrected 2026-10-30; plan
   `2026-10-30-dxmtrs-step3-d3d10core-dxgi.md`). Done 2026-10-31, DXMTRS main 58b99d7, which MacNeutron's `dxmtrs`
   pins (its `main` waits for three rows, below). Both DLLs and their native dylibs are Rust: crate `d3d10core` (its
   two exports over d3d11's `D3D11CoreCreateDevice`) and crate `dxgi` (the five exports, `DllMain` and NVEXT, the
   factory, the adapter with D3DKMT, the output and its modes, the Win32 window system on PE and winemetal's WSI hooks
   natively, private data, the configuration reader), on what they share in `dxmt-util`, whose LUID rule nvapi now
   uses too. `d3d10core.cpp` and dxgi's C++ are gone; `dxgi_interfaces.h`, `dxgi_object.hpp` and `dxgi_resource.hpp`
   stay for d3d11 and d3d12. Two C++ cuts came first (D2, D3). Characterized first by `d3d11_dxgi` (MacNeutron
   `dxmt/tests`, 202 cases over every slot of the factory, adapter and output and the seven exports), with goldens
   from the C++ after the cuts: `.fixed` (both Mac lanes), `.native` (fake displays) and `.i386` and `.guest64` (the
   VM guest). The 22 cases that crashed the C++ (23 natively) were added from the Rust. Corrections to the step as
   first written (the plan's D and P items):
   - the native `dxgi.dylib` exports only its five entry points (it exported 125, and d3d11, d3d12 and d3d10core bound
     50, 49 and 3 names from it), and `d3d10core` links util and d3d11 itself. Natively each dylib now has its own log
     file (`_dxgi.log`, `_d3d11.log`, `_d3d12.log`, `_d3d10core.log`) and configuration, accepted (D2);
   - d3d11's swap chain reads the output's gamma ramp through a private COM interface, `IMTLDXGIOutput`
     (`IDXGIOutput6` plus `GetGammaRamp`, IID `4c1128cd-b437-4d44-9958-5d765bb85a76`), queried once where its target
     is set. It replaces a `static_cast` to the C++ output, so a foreign output gets no gamma read and its presents go
     on (D3, P8). The fullscreen-gamma rows grade the presents alone: `winshot` looks at window layer 0 and a fullscreen
     window sits at 21, so P7's screen check fell back. The ramp's effect was checked once, with a scratch `winshot`
     that takes any layer (P7);
   - no C++ cut for `GetMTLDevice`: adapter slot 19 is a Rust function that returns the device as a `u64` (D4);
   - dxgi has its own configuration reader, drift-tested against `config.cpp`, which d3d11 and d3d12 keep until step 5
     (a duplicate listed for §7). The C++ now logs a matched profile's lines in key order, and so does the Rust (D5);
   - every observable C++ quirk stays (D7). What crashed the C++ now has a defined result, each a probe case recorded
     from the Rust. A null `riid` gives `E_POINTER` for QI-shaped calls and `E_INVALIDARG` for private data and
     `CheckInterfaceSupport`. `EnumAdapters1(i, NULL)` gives `DXGI_ERROR_INVALID_CALL`, and adapter slot 12 gives
     `E_NOTIMPL` with an error line. Slot 16's null cookie, `CheckFeatureSupport`'s null data and
     `D3D10CoreCreateDevice`'s null adapter give `E_INVALIDARG`. A native mode with a zero refresh denominator sorts
     last. A request with a rate never matches it (the size filter keeps it, the rate filter drops it), but one without
     a rate still can, since a headless or VM display may report every mode at 0/0 (P20);
   - the `.def`s, exports and both `version.rc`s are kept, the guest DLLs are Wine builtins and i686 links with
     `--kill-at`. Every i686 Rust vtable function and export pops the bytes the C++'s did (`i686-ret.sh`). On the Mac,
     `dxgi.dll` and `d3d10core.dll` import 34 and 30 more symbols, all from Rust's standard library, and 137 and 120
     fewer. New DLLs: ntdll, `api-ms-win-core-synch-l1-2-0` and, for one symbol, `api-ms-win-crt-private-l1-1-0`,
     all in Wine. The i386 imports were checked: `d3d10core.dll`'s `D3D11.DLL: D3D11CoreCreateDevice` and
     `dxgi.dll`'s `ntdll.dll: _aulldiv` (D11; step 1's i686 shims);
   - natively the same crates build for `aarch64-apple-darwin`. Each dylib exports exactly its names (`nm -gU`) through
     `-Wl,-u` and `-Wl,-exported_symbol` for each export, plus `-Wl,-S` outside debug builds, and no Rust symbol is
     bound across dylibs (D12, P9). D12's fallback, keeping the C++ natively, wasn't needed;
   - `panic = "abort"`, and d3d11's `IMTLSwapChainFactory::CreateSwapChain` is called as `extern "system-unwind"`.
     `DllMain` acts on `DLL_PROCESS_ATTACH` only: `DisableThreadLibraryCalls`, the shader cache path, then NVEXT (all
     three HKLM keys, or NVEXT off; i386 skips it). Rust's TLS makes `DisableThreadLibraryCalls` fail, so `DllMain`
     runs on every thread, as in `winemetal.dll` and `nvapi64.dll`. The configuration is first read at `EnumAdapters1`,
     never under the loader lock (D13, P10);
   - `d3d10core` is checked with `D3D10_CREATE_DEVICE_BGRA_SUPPORT` alone; no d2d1 row (D17);
   - the tools installed on 2026-10-30 run as legs of step R's `tools/rust-check.sh`. Its lists now name `dxgi` and
     `d3d10core` on every target they ship on, i686 included, and `dxmt-util` on i686. Step R's format commit and
     clippy fixes took the place of the plan's format task (D20, D24, P14);
   - no game bench (D22): C++ against Rust once, each within 5% but one reading, below. `present_loop` took 8.286 ms
     against 8.285. On a busy machine the ARM64EC call rates were over their recorded baseline, so the C++ ran again
     in the same session: the Rust was at or below it (-7%, -8%, -1%). Natively `GetDesc1` was +1.3% and
     `QueryVideoMemoryInfo` +1.6%. `GetMTLDevice` read 0.8 ns against 0.7, one clock tick, and both bodies are the
     same two instructions, so this is within the measurement's resolution (reported to the maintainer);
   - Tasks 1-4 (the cuts, the probe and its goldens) ran beside step R and were then rebased onto it. The C++
     references taken after the cuts stayed valid, because R changed none of DXMT's C++ (P17). MacNeutron's probe
     commits land with the pin as one commit (P22).

   Gates, once at the end (on 58b99d7): both Mac `check.sh` lanes 417 ok and 0 FAIL (412 plus five new rows: `dxgi`,
   `d3d10-core`, `d3d10-wine` and the two fullscreen-gamma rows), wine-arm64's DXMT and release checks, and the
   presenter (66 ok) and facade tests. The probe's `full` C++/Rust diff showed no unmasked difference on either lane or
   natively. Also passed: the native gate (31 programs), the VM host's tests, the loopback gate (31), `d3d9-check`
   both ways, the conformance suite (step R's figures), the guest build, the VM DXMT gate (45 programs),
   `dxgi-guest.sh` on i386 (the first i386 Rust dxgi) and x86_64, the forwarder gate (41), `d3d9-gate` and
   `tools/rust-check.sh` (721 files audited). The translator key did not move. Three rows failed with the screen
   locked: wine-arm64's `dxmt-present` and present-gate's `winshot` captures, and d3d-smoke's `present-held-a-vcpu`.
   Every other d3d-smoke row passed, both `32:` blocks included. The cause is the environment: `winshot` can't capture
   a locked screen, and the S0 C++ failed the smoke row the same way in a control run. MacNeutron `main` waits until
   the three pass on an unlocked screen. The final review found no Critical or Important defect; seven Minors are
   deferred (ledger).
4. The translator (`airconv`, the DXBC parser, our DXIL translator) behind `airconv_public.h`, once Phase 0 has decided
   it. Ported on the LLVM it uses today first, written against the explicit-type C API (`LLVMBuildLoad2`,
   `LLVMBuildGEP2`, `LLVMBuildCall2`) so that pointer types are made in one helper; gated by identical `llvm-dis`
   output on the corpus. Then the LLVM move as its own step (§8's translator gate), with the shader-cache key bumped:
   LLVM 23 and our AIR writer (§4, Decided).
   Two steps rather than one because only the first can be checked byte for byte, which matters for 26k lines.
   Once the translator is Rust, the key also hashes its crate's resolved dependencies and the Rust toolchain. D3D9's
   SM1-3 front end (C3) follows the LLVM move and ships in the same release, so players' caches turn over once.
   4a. The translator in Rust on LLVM 15: done 2026-10-31, DXMTRS main 265833e, which MacNeutron pins (plan
   `2026-10-30-dxmtrs-step4a-translator.md`; the AIR writer has its own plan, 4W,
   `2026-10-30-dxmtrs-step4w-air-writer.md`, and 4b is planned from its results, 4-D1). Everything behind
   `airconv_public.h` is Rust, in two crates under `src/airconv`: `airconv-ir` (no LLVM: the DXBC container, token
   parser, decode and control flow, root signatures, the signature handlers as data, reflection, the metallib
   container and SHA-256; it builds for every target, for step 5's PE users) and `airconv` (LLVM 15's C API declared
   by hand, one type handle, `Ty`, for every type the translator makes, the AIR layer, DXBC and DXIL lowering, the 14
   C exports, `DXILGetEntry` new for the test tools), an rlib of `winemetal`, linked into `winemetal.so` and the
   native `winemetal.dylib`. What LLVM 15's C API lacks, the pass pipeline among it, is a C++ shim of 19 functions
   (`src/airconv/shim/shim.cpp`) that the crate's `build.rs` compiles. On every corpus module the output equals the
   C++'s: `llvm-dis` text, metallib bytes, the 72 reflection bytes, tags and failure reasons, at Metal 3.1 and 3.2 and
   with D3D11's shader flags, and two processes agree. The corpus: the repository's DXIL and DXBC test shaders,
   `tests/dx11`'s HLSL compiled to DXBC with added geometry, tessellation, barrier and system-value shaders,
   malformed containers, SMITE 2's capture and 3DMark's recording. The C++ it was compared with carries step 4a's
   first three commits (`DXILGetEntry`, the barrier pass's own intrinsic declaration, the clip fix below). One switch
   (Z) replaced `libairconv.a` in both images and deleted 46 C++ files; the translator key moved there once (71a7e5f2
   to ad6adc5d), accepted. Corrections to the step as first written (the plan's 4-D and P items):
   - no `llvm-sys`, which may need `llvm-config`, absent from the LLVM 15 install: the C API's 431 functions are
     declared in the crate with its own signatures (checked against LLVM 15's headers), one file per LLVM version,
     which 4b redoes for LLVM 23 (4-D2; §4's wording amended);
   - C++ that stays: the pass pipeline and airconv's two passes (`src/airconv/transforms`) in the shim through 4b
     (MemorySSA has no C API, and identity needed the same adaptor), and `sm50_wow64.c` until step 6. §1's end state,
     no C, C++ or Objective-C of DXMT's left, is reached at step 6 (4-D3);
   - the key (MacNeutron's `translator_key`; P7, with step 5's 5-D6): for a tree with `src/airconv/Cargo.toml` it
     hashes `src/airconv` (Rust, shim, passes, helpers), `src/winemetal/airconv_thunks.[ch]`,
     `src/d3d11/d3d11_shader.cpp` (until step 5b deletes it, riding the next pin that moves the key anyway), the root
     `Cargo.toml`'s release, production and production-unwind profiles, `.cargo/config.toml`'s `[build]`, `[env]` and
     aarch64-apple-darwin sections, `rustc -vV` and `cargo tree -p airconv` (registry crates by version, path crates
     by name), besides what it hashed before: the build type, the LLVM tag, `dxmt/llvm.sh`, `xcrun-metal.sh` and the
     C++ and Metal compilers' versions. `libs/DXBCParser`, `include/` and meson's files are no longer inputs, so step
     5's meson edits, crates and dependencies leave players' caches alone. Each command's output is taken before the
     hash, and the key dies rather than drop a line, on a path crate outside `src/airconv` and on a `cargo_toolchain`
     default other than `MTLD3D_RUST`. Older pins keep the old composition, whose key still moves once at this pin:
     `dxmt/llvm.sh` lost the tool builders to `dxmt/tools.sh` (P8). Known: airconv's tests are hashed (a test-only
     commit moves the key; 4b may leave them out), and the shim's `cc` environment is not;
   - the game corpora (4-D7, P1), read in place and read-only, nothing from them committed: SMITE 2's capture (10,271
     DXIL shaders, 150 root signatures, 18,985 pipelines, 4 of them refused by both) was used. 3DMark's D3D12
     recording gave 290 DXIL shaders and 114 compute pipelines under real 1.1 root signatures, and no DXBC: it holds
     only pipelines whose creation succeeded, so no DXBC pipeline was created in the sessions it covers. SM5.1 DXBC
     under real root signatures (tables, root constants and descriptors, static samplers) is therefore compared only
     through synthetic single-slot root signatures built from each DXBC shader's reflection, a known gap (the plan's
     critic 27). A D3D12 title with fxc's DXBC, or a recording of a DXBC-era 3DMark test, would close it (the
     maintainer's call). No D3D11 capture (P12);
   - errors added where the C++ crashed, read out of bounds or exited (P15); no valid shader of the corpus fails anew.
     A read past a blob (a token walk, a container, a root signature) gives `Invalid DXBC bytecode: <what ran past>`,
     and other values the C++ trusted get like texts; where the C++ checks, its text stays byte for byte. A name with
     an interior NUL gives `Invalid DXBC bytecode: a name holds a NUL` (DXIL names too: `DXILGetEntry`'s view is built
     at Initialize), a DXIL signature name that is not UTF-8 `DXIL: a signature element's name is not UTF-8`. An LLVM
     error diagnostic (a helper that does not link, anything the C++ let `exit(1)`) gives `LLVM error:
     <description>`, no `DXIL:` prefix, so D3D12 answers `E_FAIL`; a constant made with a type of another kind (a
     `dx.op` declared with other overload types) gives `LLVM error: a constant of the wrong type` (the C++ made an
     `i0` constant). An AIRBuilder null gives the C++'s `UnsupportedFeature` text naming the op; a DXIL shader handed
     to the tessellation pipeline gives `not ported` (no caller sends one). `SM50GetErrorMessage` with size 0 writes
     nothing and returns 0; an unknown domain's workload count is 0. Error texts are bytes, never re-encoded. LLVM's
     own fatal errors (`report_fatal_error`) still end the process, as with the C++;
   - a C++ crash fixed before the baselines (07439fd): a DXBC vertex shader writing `SV_ClipDistance`, compiled with
     rasterization off (stream output, or the pass-through geometry shader), inserted into a null value. It now
     translates, without the clip output, and ships with this translator;
   - 4b's handoff: MacNeutron's `dxil-probe.cpp` and `dxil-translate.mm` move to LLVM 23's API in 4b's pin bump
     (`setOpaquePointers(false)` and `startswith` are gone; critic 23; `dxil-probe.cpp` already did at step 4W, with
     DXIL's data layout overridden: 4W below); L4's untyped list (pointers whose pointee no use gives: 511 in the
     baseline modules, also counted on SMITE 2 and 3DMark) is what 4b must type explicitly for the writer (under 4W's P4
     the translator hands only functions, globals and structs, and the writer derived every other pointer with
     `UseTyped` 0; 4b's plan says whether L4's list is still needed); G5b's timing and coverage (67.5% of the C++'s
     lines; 235 functions no corpus reaches were ported by reading alone); `make llvm-nolto` moves to LLVM 23 with the
     recipe; the handle types Z4 deferred (a value can outlive its dropped module; readers take raw handles); 4W's open
     F-D5-1 (an all-equal FP vector constant, which LLVM 23's C API makes, is written as a record Metal reads as undef).
     4W's state: its writer and harness are green on the repository corpora, SMITE 2 and 4a's DXBC corpora, Metal builds
     every pipeline, and its gate is running (it passed the same day: 4W below). After Z nothing repeats the C++/Rust
     comparison (its tests skip without `AIRCONV_STEP4_BASE`); the baselines stay in `build/step4` until 4b's plan says
     otherwise;
   - no MacNeutron release is cut between this pin and 4b's unless the maintainer asks: caches turn over per release,
     not per pin, so players' shader caches turn over once (P7). The milestone benches (§8) wait for the maintainer's
     go-ahead (4-D9).

   Gates, once at the end (Z3 on 26d3d8e; Z4's one commit re-ran the crate checks and the translator block): both
   Mac `check.sh` lanes 417 ok and 0 FAIL, wine-arm64's DXMT checks with `dxmt-present`, the presenter (66 ok) and
   facade tests, the probe (11 ok; g10's metallib hashes equal the C++'s), D3D9's checks both ways and the conformance
   suite at step 3's figures, the native gate (31) and probe, the VM host's tests, the loopback gate (31), the guest
   build, the VM DXMT gate (45), `d3d9-gate`, `present-gate` (12 ok), `make llvm-nolto` and `tools/rust-check.sh`
   (6,547 tests, 816 files audited), and licences (`airconv` LGPL-2.1-or-later, `airconv-ir` also MIT for the
   DXBCParser port; no new third-party crate). The translator block, 25 corpus runs of the shipped ThinLTO tool: no
   difference in `llvm-dis` text or metallib bytes, two processes equal; translate-only times equal within noise
   without a rustc `target-cpu` (P10), C1 0.1 ms a shader slower. Two rows failed, neither on the translator's path.
   The probe's `--full` (it needs `--goldens build/step2/goldens`: the committed folder holds none) failed g10 on a
   handle label (Rust's allocator reuses a freed bitcode's address; every content hash equal) and g6's colour space
   (the C++ reads the same with the screen locked). d3d-smoke's `present-held-a-vcpu` failed all six runs of one lock
   job, the Rust and the C++ at main alternating, on its rule of five slow presents (Rust 63-69, C++ 20 and 68), under
   load with the screen locked, as in step 3. The two final reviews' Important findings were fixed or ruled in Z4:
   erasing a lowered value is `unsafe` and constants check their kind (the raw-handle readers go to 4b), and C10 is
   the gap above. Their Minors are deferred (ledger).
   4W. Our AIR writer for LLVM 23: W done (2026-10-31, air-23 da8441e28e; MacNeutron db02b94), plan
   `2026-10-30-dxmtrs-step4w-air-writer.md`. The writer and its gate are §4's Decided; 4b, the LLVM move, is planned
   from it, with the hand-off in `build/step4/map/7-decisions.md`. Nothing reaches players: W edits neither
   `dxmt/llvm.sh` nor any other translator-key input, so no pin moves and the key stays (4-D4). MacNeutron gains host
   tools only, cherry-picked onto `dxmtrs` (`build/pipeline.md` rule 8): `dxmt/tools/air15.cpp` (LLVM 15's typed
   extract, disassembly, type table, verifier and metallib repack), `air-tools.sh`, `air-oracle.sh`, `air-bcinv.py`,
   and `dxil-probe` building on LLVM 15 and 23 (LLVM 23's reader needs DXIL's data layout overridden). Corrections to
   the step as first written (the plan's P items and the ledger's rulings):
   - the full leg's typed text treats a constant GEP's zero indices, `i32 0` or `i64 0`, as equal: LLVM 23's reading of
     the C++'s LLVM 15 output makes the difference, a path 4b never takes, and Metal still builds each such module
     (D1: 141 SMITE 2 modules, one DXIL test);
   - a module whose instruction count LLVM 23's reader changes (constant expressions expanded; D2: two DXIL tests)
     leaves the full leg's positional and text rows, and one whose intrinsics it upgrades (LLVM 15's lifetime markers;
     D6: one DXIL test) the full leg's text rows and opt23's normalized text; each is graded by a sliced defs run
     instead, automatically and listed per module. This replaces the plan's "posmis = 0, on every corpus except
     `smite2`", so the plan's posmis mutation flips its row without failing the leg, while D2's own mutation (the sliced
     run's refs removed) fails it;
   - the self row leaves out `; preds =` comments, which follow a use-list order the bitcode does not carry and the
     passes reorder (R7-4, amending P9's "no normalizer"). The normalized-text row mirrors the writer's scalar
     `scmp`/`ucmp` lowering (a vector form fails the row) and its bit-test rewrite (R7-6, D5) and deletes drop-list
     attributes on a define's return, keeping them on intrinsic declarations, whose attributes the reader sets;
     parameters and call sites it leaves alone, a ceiling that fails the row rather than passing it;
   - the type table names an unnamed identified struct `%N`, as `Module::print` numbers the module before the passes
     (the tessellation helpers' domain-location struct);
   - opt23 runs P15's pipeline with `instcombine<no-verify-fixpoint>`: a textual `instcombine` verifies its fixpoint
     and ended the tool on 21 SMITE 2 modules, while the shim's `InstCombinePass()` does not verify (R7-5);
   - SMITE 2 is graded on W's own plain-mode translation of the capture, not on a G6 baseline: G6's `--pipelines`
     prints hashes only, the plain-mode metallibs' `air=` equals G5's tool's on all 10,271 shaders, and the
     root-signature path stays graded by 4a's hash baseline, C8 (option A). 4a's corpora are graded with G5's tool,
     `base-0439fa9`, on its frozen snapshots (R7-7);
   - the DXBC tests are 4 metallibs (2 shaders and their `.1` variants), not the plan's 2;
   - the first 14 names added to P13's list, eight from the C++ AIR and six made by LLVM 23's passes, were ruled in once
     their LLVM 15 and 23 signatures were compared (R7-2);
   - the identity stand-in writer's own control reads text23n 94/96 once attribute drops are counted (it reports none
     for the two modules that have them), so it serves only the comparator-only mutations;
   - P18 ran under ruling R9-1 (§4), and a second two-minute lock job, beyond the two the plan allowed, ran the C++
     cold-against-cold control the rule rests on (R8-1).

   Open for 4b: P5's option (airconv's three literal descriptor types made identified structs, so that they enter the
   table), and F-D5-1, which blocks 4b (an all-equal FP vector constant, which LLVM 23 builds as a vector `ConstantFP`,
   is written as one record LLVM 15's reader reads as undef; the bitcode reader makes `ConstantDataVector` instead, and
   scans of SMITE 2's and 4a's DXBC corpora's opt23 outputs found no FP-vector undef, but 4b's C API makes them).
   4b. The translator on LLVM 23 with our AIR writer: done 2026-11-01, DXMTRS main 8abbdf8
   (8abbdf83768c95e2ca999d195d616b76f772d1bc) and Mac-Neutron/llvm-project `air-23` edc723f40d
   (edc723f40de7c461a3e8473d174d88a9027dcc54), which MacNeutron pins (plan `2026-10-31-dxmtrs-step4b-llvm23.md`). The
   translator key moves once, as planned: `ad6adc5daa8e8a5860fb5c666d8b661e55642b50d8fbf79de3ded217cf5eaf67` to
   `3e3474d503541d410b2c659fa36653ea2c03f52302353b125b93f1e5b15780f6`. Decisions and corrections (the plan's P items):
   - `TyCx::lower` creates opaque pointers (`LLVMPointerTypeInContext(cx, space)`) while `Ty` keeps the full pointee
     algebra, and `TyCx::typed` builds `LLVMAIRTypedPointerType` handles only at write time (`writer::write`) for the
     module's surviving functions, globals and identified structs (`type_table`), plus the helper bitcode as `TypedRefs`;
     `TyCx::lift` recovers `Ty` from a parsed LLVM 23 `LLVMTypeRef` (`TyKind::Unknown` for opaque pointers and foreign
     types) (P4, P5);
   - DXIL 3.7 bitcode is parsed directly by LLVM 23's reader (which auto-upgrades typed pointers to opaque), with the
     target triple and DXIL's 32-bit data layout restored before the helper link (`module::parse` `Layout::Dxil`);
     groupshared 64-bit atomics match globals in `addrspace(3)` by value type rather than pointer element type; no-op
     pointer-to-pointer bitcasts (`.hca`) folded by LLVM 23's `IRBuilder` are retargeted and erased without rebuilding a
     placeholder, and `is_lifetime_marker` is gone (LLVM 23's reader upgrades lifetime markers to one pointer argument)
     (P6, P7, critic 2);
   - the LLVM 23 build recipe lives in DXMTRS's `tools/llvm.sh` (`build_llvm`, `LLVM_AIR_COMMIT`), sourced by
     `wine-arm64/build.sh`, `eac-vm/dxmt-native/build.sh` and `make llvm-nolto`, with `-DLLVM_ENABLE_TERMINFO=Off`,
     `-DLLVM_ENABLE_ZSTD=Off`, `-DLLVM_TARGETS_TO_BUILD=""` and `-DLLVM_BUILD_TOOLS=Off` (removing `libncurses.5.4.dylib`
     from `winemetal.so` and `winemetal.dylib`); the 53 `-lLLVM*` libraries live in one list in `src/airconv/meson.build`
     read by `src/airconv/build.rs` and `dxmt/tools.sh` (P8, P10, P11, P12);
   - the translator key moves to composition 3 (`macneutron translator 3`, P9, P16): it hashes `tools/llvm.sh` instead
     of `LLVM_TAG` and `dxmt/llvm.sh` (dying if absent), hashes `src/d3d11/d3d11_shader.cpp` when present else
     `src/d3d11/src/shader/args.rs` (5b's P3 rule, dying if neither exists), excludes `tests.rs` and `tests/` under
     `src/airconv`, refuses `[profile.*.package.*]` overrides for `airconv`, `airconv-ir` or `"*"` and `[target.'cfg(...)]`
     sections, and captures every command's output and exit status before hashing;
   - P13's C++ LLVM 15 bitcode-identity tests and `oracle::derive` are deleted, replaced by `writer::tests` on our AIR
     writer and `air-oracle.sh check` with `self`, `dual` and `stats` (`use-typed=0`) rows; P15's residual risk is
     accepted (value-level equality with the C++ is proven for what the check suite executes and renders, plus the
     milestone benches);
   - P16's release hold: 4a's no-release rule ends at 4b's pin, and a release cut before 5b's switch (and C3) lands
     costs one more cache turnover.
   Gates, once at the end (on 8abbdf8 + 175af58): all 15 `air-oracle.sh check` rows (`shaders`, `dxil`, `dxbc*`,
   `d3d11*`, `dx11*`, `smite2` 16,092 metallibs, `c11` 426, `smite2-pl` 34,238, `c11-pl` 114) pass with `usetyped=0`,
   `newfail=0`, `reasons=eq`, `self=n`, `dual=n` and `stats=n`; `b23` (ThinLTO) equals `lr` (oracle-on, no-LTO) and two
   `b23` runs are `cmp`-equal on C1 (96/96), C2 (33/33) and SMITE 2's first 500 shaders (500/500); cold Metal build time
   on SMITE 2's 500 shaders is 27.78 ms mean vs ref15's 27.27 ms (+1.9%, 0/500 shaders >10% slower beyond round spread);
   R9-1 `check-exec.sh` + `step5-cmp.py` 6/6 rows equal (38/38 ok on both ref15 and b23); `tools/rust-check.sh` passes
   (6,624 unit tests, 888 files audited, 0 new crates); both Mac `check.sh` lanes pass 417 ok and 0 FAIL (the fast-math
   flags row re-recorded as `64:64:50` for LLVM 23 InstCombine's `SimplifyDemandedFPClass` stamping `nnan` on
   `uitofp`/`sitofp` and finite-constant arithmetic in 14 shaders while `ninf=0 cmp=0` holds on all 64); `wm-probe-check`
   passes 11/11 ok in both standard and `--native` modes (g10's 5 bitcode hashes updated for K4; `wm_heap_growth`
   updated to sum live `MALLOC_PTR_IN_USE_RANGE_TYPE` ranges across zones instead of macOS 26 `xzone`'s 16 KiB slice
   counter); the native gate (31/31), loopback gate (31/31), VM DXMT gate (45/45), `d3d-smoke` in both WoW64 modes,
   `d3d9-gate` (45/45), `present-gate` (11/11), `d3d/gate.sh` (41/41, 73 ok) and the `p` profile checks (key equality,
   `.complete`, byte-equal `libairconv_shim.a` objects across all 4 builds) all pass. Milestone benches (P17, forwarded,
   cold cache on cloned state disks, `4b` vs `ref15`, window uncovered throughout): SMITE 2 lobby 59.8 vs 58.4 fps
   (+2.4%), 42.8% vs 41.8% GPU busy, 7.15 vs 7.16 ms GPU/frame (-0.1%); Palworld title screen 117.0 vs 116.8 fps
   (+0.2%), 63.5% vs 63.9% GPU busy, 5.43 vs 5.47 ms GPU/frame (-0.7%); Age of Empires II DE menu 118.3 vs 117.5 fps
   (+0.7%), 60.0% vs 59.4% GPU busy, 5.07 vs 5.06 ms GPU/frame (+0.2%).
5. The core (`src/dxmt`, crate `dxmt-core`) with D3D12 and D3D11, in three plans (5-D1; step 5's decisions are in
   `build/step5/map/7-decisions.md`): 5a gives the core its crates, links them into `d3d11.dll` and `d3d12.dll` and
   moves the shared seams to Rust behind the unchanged C++ classes; then 5b (the D3D11 track: the D3D11 front end with
   the D3D10 object layer inside it, one switch of `d3d11.dll`) and 5c (the D3D12 track, one switch of `d3d12.dll`)
   run in parallel. The order as decided:
   - live in 5a, each behind a C ABI cut in the C++ header the seam owns: statistics with the dump folder, the
     presenter with its scalers, formats, the shader cache;
   - S5 dropped (5-D2): the queue and the encoding context go live with D3D11's switch, not alone (a foreign call per
     recorded command otherwise), so the queue's port is not checked apart from it;
   - allocators, staging and residency have no live seam and go live inside each DLL at its switch, as do the
     configuration reader (`config.cpp` serves the C++ until then) and the core's logging, which each front end routes
     to its own log file;
   - frame generation and pacing stay D3D12's until its switch, where frame generation moves into the core and 5c's
     Rust calls the statistics directly, dropping 5a's C ABI for them;
   - 5b and 5c take the DXBC parser and the root-signature deserializer from 4a's `airconv-ir`, never the C++ copies
     (5-D6).
   The core's Rust API is shaped for D3D11, D3D12 and D3D9 (C4; 5-D13, `build/step5/map/5-d3d9-needs.md`); D3D9's
   binding fork is decided before C3, and its front end joins after D3D11's.
   The key after step 4's switch (5-D6, 4-D4; 4a's composition, above): it hashes airconv's resolved dependencies and
   the Rust toolchain, not the root `Cargo.lock`, so step 5's crates, dependencies and meson edits never turn players'
   caches over. D3D11's switch deletes `src/d3d11/d3d11_shader.cpp`, a key input, and rides the next pin that moves the
   key anyway (4b's, since 4a has pinned), never a key move of its own.
   5a. The core's crates and the shared seams: done 2026-10-31, DXMTRS 54b71f6 (`step5a` rebased onto 4a's main
   265833e, the controller's ruling, below), which MacNeutron pins (plan `2026-10-30-dxmtrs-step5a-core-seams.md`).
   Phase P made two commits: D3D12's encoder types in their own namespace, and `DXMT_WMTCMD_HISTOGRAM`, a winemetal
   development knob that counts encoded records per command buffer. S0 laid down every crate and module of step 5:
   `dxmt-core`, the `d3d11` and `d3d12` staticlibs (one in each DLL and native dylib, on every target) and
   `winemetal-sys` (`src/winemetal/sys`: winemetal's C API for Rust callers, generated by `tools/winemetal-layouts.sh`,
   with `Obj` and `Pool` beside it); what every front end shares moved into `dxmt-util` (step 3's configuration reader,
   the gamma ramp, the private interfaces). S1-S4 made four seams Rust behind the unchanged C++ classes: statistics with
   the dump folder, the presenter with its scalers, the format table and the shader cache's store. Each seam's `extern
   "C"` declarations sit in the C++ header it already owns, between `dxmt-abi` lines, and a generated `abi.rs` pins
   every Rust export to them on all five targets. Characterized first by goldens recorded twice from the C++: the
   statistics' counters and timed-line shapes, record histograms, the presenter's calls over 37 scenarios, the spatial
   scaler's run, the format tables per GPU family and the shader cache's paths over an environment matrix. Corrections
   to the step as first written (the plan's 5-D and P items and the ledger's rulings):
   - three plans and no S5 (5-D1, 5-D2: the order above). The rest of the core is written ahead, host-tested and not
     linked (P1, P14): resources, rings, staging, dynamic pools and residency, samplers, the internal library, capture,
     HUD and version text (lanes R1-R3); the queue and its chunk arena, binding and access tracking, passes, flush and
     the optimizer, queries, the resource initializer, UAV counters and emulated clears (E1-E4); D3D12's legacy
     barriers, root-signature serialization, resource math, feature answers, samplers and pipeline-stream parsing
     (D1-D4). D1-D4 are on DXMTRS `step5w` (92eba01), which never reaches `main` in 5a; R1-R3 and E1-E4 are committed
     on their lane branches and wait, R1-R3 for `step5w`, E1-E4 for `step5w-e`, cut from it once R1 is on it.
     `step5w` is rebased onto 5a's main and, once its last W lane has merged, is 5b's and 5c's base;
   - each seam's C ABI was cut in the same commit as its Rust, not first in a commit of its own (this section's opening
     rule): the C++ class stays the boundary, unchanged for its callers, as a thin forward (P2). Phase P's cut, the
     encoder types' namespace, was its own commit;
   - natively `winemetal.dylib` exports seven more Win32 calls (`wmt_native_*`: events, semaphores, handle duplication
     and closing, waits, `OpenThread`), because 5b's and 5c's front ends need them in parallel (5-D8). They wrap
     `win32_native.h` in a new `src/nativemetal/win32_native.cpp`, not in `win32_event.c`: C cannot wrap that header;
   - the native `d3d11.dylib` and `d3d12.dylib` export only their `.def` names (and `MTLD3D11WaitIdle`), not every
     global, as step 3 did for dxgi, with OSO stabs and `$HOME` 0 (`-Wl,-S` outside debug builds); the PE DLLs gain
     only the imports of Rust's standard library (P16);
   - natively the seam exports are `extern "C-unwind"` under an abort-on-panic guard and the staticlibs build with
     `production-unwind`, so an Objective-C exception still reaches the VM host's `catch (...)` (P19, step 2's D4);
     PE is unchanged;
   - the core logs through `dxmt_log`, which each front end defines at its switch; no seam logs, so 5a defines it
     nowhere (P5). `config.cpp` stays the C++'s reader until each switch, drift-tested against `dxmt_util::config`
     (P6), and `ShaderCacheVersion()` stays C++ until 5c (P7);
   - the drawable prefetch stays off (5-D5); its `DXMT_NATIVE` block, never compiled, went with the C++ presenter (P8);
   - the i386 `d3d12.dll`'s Rust statistics ran in the VM: the first run of a `thread_local!` through `__tls_array`
     (step 1);
   - the fix pass: COM's HRESULT codes have one home, `dxmt_util::com`, and `tools/rust-check.sh` gains a `gen` leg
     that regenerates winemetal's generated bindings and the seams' and fails on any difference (airconv's
     `layouts.rs`, 4a's, is not yet in it);
   - the key: nothing in 5a moves it (5-D6, 4-D4). 5a touches no input of 4a's composition, so 54b71f6's key is
     265833e's, ad6adc5d; before the rebase it stayed 71a7e5f2 (Task 0's, under the MacNeutron scripts 5a was built
     with), before and after every commit;
   - the rebase: `step5a` was cut from 58b99d7 and `main` had moved to 265833e (4a), so it could not fast-forward; the
     controller had it rebased (13 commits). Ten apply unchanged; three differ only where 4a's lines meet 5a's (the
     workspace and `rustfmt.toml` lists, `tools/rust-check.sh`, `tools/winemetal-layouts.sh`'s `--airconv` mode inside
     5a's parser, `docs/DEVELOPMENT.md`). MacNeutron's side, the notices of `d3d11.dll`'s and `d3d12.dll`'s Rust,
     lands with the pin as one squashed commit;
   - no game bench (5-D12); the D3D11 title §8 asks for is Age of Empires II DE (maintainer, 2026-10-31), asked for
     before 5b's switch gates.

   Gates, once at the end (Task 20 on db50eb6; the fix pass left the identity tables of every Mac and native binary
   equal, so no lock gate re-ran): both Mac `check.sh` lanes 417 ok and 0 FAIL, wine-arm64's DXMT, present and release
   checks, the presenter (66 ok) and facade tests, the probe (11 ok), D3D9's checks both ways and the conformance suite
   at step R's figures, the native gate (31) and probe, the VM host's tests, the loopback gate (31), the forwarder's
   checks, the guest build, the VM DXMT gate (45), the d3d gate (41), `dxgi-guest.sh` on both widths, `d3d9-gate`,
   `present-gate` (12 ok; forwarded, 17), the VM statistics row, the p profile (no statistics or dump code left), the
   goldens' rows (statistics, histograms and the spatial run equal to the C++'s) and `tools/rust-check.sh` (on the
   fixed tree: 6,454 tests, 792 files audited). Identity is P16's delta once ED10 reads Rust's pooled string literals
   (a comparator gap, fixed); dxgi and the native winemetal differ by S0's intended changes. C++ against Rust once:
   within 5% (`present_loop` +2%; two ARM64EC DLSS rows read +32% and +43%, load, gone in a same-session control). Two
   rows fail and fail their C++ controls too, so they go to the maintainer as older than 5a: d3d-smoke's
   `present-held-a-vcpu` in both WoW64 modes (with the C++ native dylibs in the VM host as well; every other row
   passed) and the probe's `--full` g6 colour space (`ref-wine.app`'s `winemetal.so` reads the same). Re-verified on
   54b71f6 after the rebase: `tools/rust-check.sh` (6,613 tests, 887 files audited), the key, the Mac build,
   `check.sh` ARM64EC 417 ok and x64 415 ok twice (each of its four failing rows, `atomic64typed`, `bg-leave` and
   `ts-many`'s two, passed in the other run), the native gate (31), the VM DXMT gate (45) and the d3d gate (41).
   5c. The D3D12 front end in Rust: done 2026-11-02, DXMTRS main 424073c
   (424073c26e0faa63362090dc5b079de28d6c04c0), which MacNeutron pins (plan `2026-10-30-dxmtrs-step5c-d3d12.md`).
   Both `d3d12.dll` (ARM64EC, x86_64, i686) and the native `d3d12.dylib` are pure Rust (`src/d3d12`, crate `d3d12`),
   and all 22 C++ files of `src/d3d12/` plus `src/d3d10/d3d10_blob.{cpp,hpp}` and 5a's C ABI for statistics and the
   dump folder (`dxmt_stats_*`, `dxmt_dump_*`, `src/dxmt/src/{stats,dump}/abi.rs`) are deleted (`d3d12_interfaces.hpp`,
   `d3d12_barrier_legacy.hpp`, `d3d12_shared_fence.hpp`, `d3d12.def` and `version.rc` stay for the VM forwarder and
   the PE link). The switch waited until 4a's Z and 4b had landed on `main` (P3), so `d3d12` consumes `airconv-ir` and
   `tools/llvm.sh` from `main` and the translator key (`3e3474d503541d410b2c659fa36653ea2c03f52302353b125b93f1e5b15780f6`)
   does not move. Corrections and decisions (the plan's 5-D and P items):
   - frame generation and its pacing thread move into the core (`dxmt_core::presenter::framegen`, ordered behind the
     queue's fence ring through `framegen::Order`), characterized by `adapt_replays_cpp_trace`,
     `present_orders_as_cpp` and `fgpix.sh` over `d3d12_dlss framegen-still` (5-D3, P11);
   - natively, unimplemented COM slots report through a thread-local flag in `winemetal.dylib`
     (`wmt_native_not_implemented` sets it, `MTLD3DNotImplemented` reads and clears it) instead of C++ exceptions, and
     MacNeutron's VM host (`eac-vm/d3d/gen.py`, the six hand-written sites in `eac-vm/d3d/host/native.cpp`, and
     `eac-vm/d3d/host/dispatch.c`) checks `mn_not_impl()` beside `catch (...)` so `mn_d3d_not_in_dxmt` still fires
     (5-D9, P14); `winemetal.dylib` also exports `wmt_native_handle_count` (reading `mn_win32_handle::refs`), pinned by
     `wmt-exports.sh`;
   - every COM entry point and spawned thread runs under P20's `slot!` abort-on-panic guard (kept under `perf-build`),
     while `scope!` and `count!` compile to nothing under `perf-build` (`MACNEUTRON_PROFILE=p`);
   - every unimplemented slot matches the C++'s `IMPLEMENT_ME` or `E_NOTIMPL` set (`unimpl-parity: equal (25 stubs, 17 E_NOTIMPL)`),
     every statistics counter and scope label matches the C++ (`statnames: equal (269, 29)`), and `WMTWsiAction`'s wire
     layout and `restore_display_mode` monitor argument align with `winemetal` across 32-bit and 64-bit targets.

   Gates, once at the end (Task 19 on 0bcf16e and Task 20's fix pass on 424073c + 6a29542): `tools/rust-check.sh` passes
   all 7 legs (6,799 host unit tests, 1,037 files audited, all 11 generated files byte-equal, `i686-ret.sh` callee-pop
   byte counts equal across all 269 vtable slots and 7 exports); both Mac `check.sh` lanes pass 418 ok and 0 FAIL
   (including the new `d3d12_probe` row), wine-arm64's `dxmt` and `dxmt-release` checks pass, Task 6's corpus
   comparators (`stats-compare.sh --names-as-set`, `hist-compare.sh`, `rec-compare.sh`, `dump-compare.sh`,
   `log-compare.sh`), `fgpix.sh` on both Mac lanes, `crosshit.sh` both ways, `d3d11_dxgi full --ported` and
   `d3d12_probe` (`fixed --ported` and `full --ported`) on both Mac lanes and natively, 3DMark pipeline replay (348
   pipelines, 591 keys) and `pso-compare.sh` on replay and `native-wmt-trace`, the native gate (31/31), loopback gate
   (31/31), forwarder checks (`dispatch-test`, `answers`, `wire`, `identity`, `wrapped`, `pieces`, `threads`), the guest
   build (`mtld3d-layout32: ok (132 structs)`, `layout32: ok (241 structs, 222 on the wire)`), the VM DXMT gate (45/45),
   the d3d gate (41/41), `dxgi-guest.sh` (`d3d11_dxgi` and `d3d12_probe` on i386 and x86_64), VM guest statistics on
   both widths, `present-gate` (guest and forward), `d3d-smoke` in both WoW64 modes, `parts.sh` path and licence checks,
   the `p` profile binary checks (0 stats/dump symbols or strings, `d3d12_null` 18/18 equal) and P19's performance rows
   all pass within budget.
   5b. The D3D11 front end and the D3D10 object layer in Rust, with the queue and the encoder live: done 2026-11-03,
   DXMTRS main 0cb6b8f (0cb6b8f4bcb5af51c3bada9a511999ad23112c15, switch commit 02bb81c plus the core C++ retirement
   0cb6b8f on top of 5c's 424073c), which MacNeutron pins (plan `2026-10-30-dxmtrs-step5b-d3d11.md`). Both `d3d11.dll`
   (ARM64EC, x86_64, i686) and the native `d3d11.dylib` are pure Rust (`src/d3d11`, crate `d3d11`), with the D3D10
   object layer inside `src/d3d11/src/d3d10` (`d3d10core.dll` and `.dylib` call `D3D11CoreCreateDevice` as before) and
   `dxmt-core`'s command queue, chunk arena, binding and access tracking, passes, flush and optimizer, queries, resource
   initializer, UAV counters and emulated clears live. All 15 C++/header files of `src/d3d11/` and `src/d3d10/` are
   deleted (`d3d11_interfaces.hpp`, `d3d11.def` and `version.rc` stay for the VM forwarder and the PE link), and because
   5c had already switched, Task 18 Step 4's link-closure check retired all 28 remaining C++/header files of `src/dxmt/`
   (`0cb6b8f`; `libs/DXBCParser` is unlinked by any front end and waits for step 6, P17). Corrections and decisions (the
   plan's 5-D and P items):
   - deleting `src/d3d11/d3d11_shader.cpp` switches `translator_key` (`macneutron translator 3`, §5 step 4b P9, 5b P2/P3)
     to `src/d3d11/src/shader/args.rs`, moving the key once as planned (`3e3474d503541d410b2c659fa36653ea2c03f52302353b125b93f1e5b15780f6`
     to `6c9fa750ccc52b6e052aee6f867b869495575c7382258a01bb2c53760006dbe4`);
   - 5c's groundwork (Tasks 1-4 on `step5w`, P23) supplied the native not-implemented flag (`wmt_native_not_implemented`
     and `MTLD3DNotImplemented`), `ShaderCacheVersion()`, COM vtable generation and environment plumbing, while 5b added
     `dxmt_util::sha1`, the WSI calls D3D11's swap chain uses, `winemetal-layouts.sh --com --records`,
     `DXMT_WMTCMD_DUMP` (P26), `d3d11::com::Managed` (`ManagedDeviceChild` refcount separation), and `dxmt-core` chunk
     capture lifetimes living until `Arena::reset` (`arena_drops_at_reset_not_at_run`, P8) plus `Device::drop` draining
     the queue before dropping caches (`drop_drains_before_caches`);
   - every `DXMT_UNREACHABLE` site has a defined non-UB return (`E_INVALIDARG`, `E_FAIL`, `NULL` or `0`, P10), verified
     by `d3d11_probe` (`NOT_ON_C (--ported): ok`), and all 19 native unimplemented sites set and clear
     `MTLD3DNotImplemented` (`notimpl: 19/19 sites verified (flag=1,0)`);
   - MacNeutron's VM forwarder (`eac-vm/d3d/rules.txt`, `6f8fc13`) forwards `ID3D11Device1::CreateDeviceContextState`
     and wraps the returned `ID3DDeviceContextState` alongside `SwapDeviceContextState`, and `d3d11-guest.sh` passes the
     Unix path `/mnt/share/cache` for `DXMT_SHADER_CACHE_PATH` (matching `winemetal`'s `fopen` under Wine).

   Gates, once at the end (Tasks 19-20 on 0cb6b8f + 6f8fc13, reports `build/step5b/reports/gates-{rust,mac,native,vm}.md`):
   `tools/rust-check.sh` passes all 6 legs (6,964 host unit tests across 43 suites, 1,135 files audited, all 12 generated
   files byte-equal, 0 `Apache-2.0`-only crates); `ident-diff.sh` reports 0 differences across all 42 binary identity
   tables (`16` Mac, `6` native, `20` VM guest, with and without `ED5`, `OSO=0`, `$HOME=0`); both Mac `check.sh` lanes
   pass 420 ok and 0 FAIL (adding `a D3D11 and a D3D12 program open one cache table` and `D3D11's COM answers are the
   C++'s`), wine-arm64's `dxmt` and `dxmt-release` checks, `presenter/check.sh` (66 ok) and `d3d9-check.sh` (59 ok) all
   pass; `mac-ab.sh` (17/17 programs equal on x64 and arm64ec), `air-compare.sh` (230/230 shaders byte-equal on x64,
   arm64ec, native and i386 guest), `hist-compare.sh` (17/17 equal), `wmtcmd-compare.sh` (17/17 equal + mutation RED),
   `spatial.sh` (59/59 lines equal) and `d3d11_probe full` under `DXMT_LOG_LEVEL=debug` (531 log lines and 663 probe
   lines equal on x64 and arm64ec) all pass; the native gate (33/33), native unit tests, VM host forwarder checks and
   loopback gate (33/33), VM guest build (`mtld3d-layout32: ok (132 structs)`, `layout32: ok (241 structs, 222 on the
   wire)`), VM DXMT gate (47/47), `d3d/gate.sh` (43/43, `coverage: ok (277 methods, 114250 calls, all forwarded)`),
   `d3d11-guest.sh` (`x86_64`, `i386` and all 14 `i386` programs + AIR compare), `dxgi-guest.sh` (`i386` and `x86_64`),
   `d3d9-gate.sh` (20/20), `present-gate.sh` (`guest` and `forward`, 0 slow presents across 600 D3D11 frames),
   `d3d-smoke.sh` in both WoW64 modes (38/38 functional checks; 64-bit `smoke.exe`'s `present-held-a-vcpu` row is the
   pre-existing D3D12 `Queue::present_frame` behavior from 5c), `parts.sh` and the `p` profile checks (0 `dxmt_stats_`
   symbols, 0 debug/trace log strings in guest PE DLLs, `p-native d3d11_probe fixed` 642/642 equal with `info:` lines
   `std=44 p=0`) all pass; `present_loop` is equal (`8.289 ms` vs `8.291 ms` on x64, `8.296 ms` vs `8.310 ms` on
   arm64ec) and `d3d11_draw perf`'s `map_discard_draw` is -50.5% on Mac (`327.6 ns` vs `661.7 ns`) and -48.5% natively
   (`127.5 ns` vs `247.8 ns`).
6. `util`'s remaining templates, `replay` and the meson build go with their last users. With no C++ LTO left, the
   ARM64X link of `tools/arm64x-link.sh` is open to every DLL.
   *Step 6 done* (DXMTRS b191ebc, plan `docs/superpowers/plans/2026-11-03-dxmtrs-step6-cleanup.md`): `dxmt-replay` is
   Rust (`src/replay`, `dxmt-replay` crate, linked into both `dxmt-replay.exe` on Windows PE and `dxmt-replay` on macOS
   native; `src/replay/dxmt_replay.cpp`, `src/replay/native_main.cpp` and `src/d3d12/d3d12_pipeline_record.{hpp,cpp}`
   are gone), `src/winemetal/unix/sm50_wow64.c`'s 9 WoW64 32-bit SM50 thunks (`thunk32_SM50*`) live in
   `src/winemetal/unix/src/g10.rs` (`#[cfg(not(feature = "native"))]`), `src/winemetal/winemetal_thunks.c` is replaced on
   macOS native by `winemetal-pe` (`src/winemetal/src/lib.rs`, bundled into `winemetal` under `native =
   ["dep:winemetal-pe"]`), and `libs/DXBCParser/`, `libs/meson.build`, `src/util/meson.build`,
   `include/{adt.hpp,ftl.hpp,tl/generator.hpp}`, `src/winemetal/Metal.hpp`, `src/dxgi/dxgi_{object,resource}.hpp`, the
   dead C++ classes in `src/dxmt/dxmt_{format,presenter,scaler,shader_cache}.hpp`, and all 45 unreferenced C/C++ files
   in `src/util/` are deleted (`92 files changed, 1930 insertions(+), 17155 deletions(-)`), leaving only §6's external
   consumer closure and `src/nativemetal/win32_{event.c,native.cpp}`. `tools/rust-check.sh` passes all 6 legs (`1,137`
   files audited), `translator_key` is unchanged (`6c9fa750ccc52b6e052aee6f867b869495575c7382258a01bb2c53760006dbe4`),
   `ident-diff.sh` shows 0 diffs on all 34 untouched binaries across Mac (`13/16`), native (`5/6`, including
   `winemetal.dylib` with 0 export diffs) and VM guest (`16/20`), and the full Mac (`check.sh` 420/420 on x64 and
   arm64ec, `wine-arm64/check.sh`, `presenter/check.sh` 66/66, `d3d9-check.sh`, `wm-probe-check.sh --so` 11/11), native
   (`wmt-exports.sh`, `wm-probe-check.sh --native` 11/11, `build-tests.sh` including native `dxmt-replay`, `gate.sh`
   33/33) and VM (`loopback-gate.sh` 33/33, `dxmt/gate.sh` 47/47, `d3d/gate.sh` 43/43, `d3d11-guest.sh`, `dxgi-guest.sh`,
   `d3d9-gate.sh`, `present-gate.sh` guest and forward, `d3d-smoke.sh` old and new WoW64, `parts.sh`) gate suites pass.

**D3D9's convergence (§7)**, in phases interleaved with the steps, each its own plan:
- C2 (with step 5's configuration, logging and statistics, or earlier behind the C++ signatures): one configuration
  reader for `dxmt.conf` and `mtld3d.conf` (`MTLD3D_CONFIG` an alias of `DXMT_CONFIG`), one logger and crash reporter,
  one statistics format (DXMT_STATS's), one perf-build switch for all Rust. D3D9 honours `DXMT_LOG_LEVEL` from the start
  of this phase.
  Done 2026-11-06, as the SC-2 rulings define it, at DXMTRS main 0e751474 (0e751474f7bdc7088e6faa4518462987f024e362),
  which MacNeutron pins: one implementation per piece in `dxmt-util` (configuration reader, level parsing, file names,
  once-macros, crash reporter; D3D9 keeps its queued sink and `env_logger` line format), one perf-tracking switch, and
  D3D9's statistics in `stats.txt`'s format in perf-tracking builds; the D3D9 binaries are LGPL combined works through
  `dxmt-util`. It sits on the shared-core refactor SC (14920a40: one D3D10-12 core for settings, COM helpers,
  swapchains, resources, state and encoding, translator key unchanged) and R3 (aabbc99a: residency removals immediate,
  `GetCachedBlob` empty, `ExecuteCommandLists` committing at once, the ring's in-flight count before hand-off, D3D11
  multithread protection off by default, each as at 11067f9).
- C3 (after step 4's LLVM move, same release): D3D9's shaders through the translator by an SM1-3 front end (mtld3d's
  DXSO parser, IR and linkage moved in; fixed function through the same builder), first with today's D3D9 runtime
  (the translator already writes `[[stage_in]]` vertex inputs without an input layout, and top-level resource
  arguments), reached without loading MacNeutron's presenter into D3D9 processes. Checked by a corpus of DXSO shaders
  rendered both ways, the conformance suite and the D3D9 gates. The DXSO MSL emitters and `mtld3d_shaders.bin`'s MSL
  records retire at parity (one cold D3D9 cache, expected less first-use stutter).
  C3a done 2026-11-09 at DXMTRS main aa02c4b1 (aa02c4b117b095bc4a864c17777f3c8b21e8ba61), which MacNeutron pins:
  the DXSO parser, IR and link data moved into `airconv-ir::dxso`; `SM50_SHADER_D3D9` input (a DXSO sniff on token 0,
  refused for D3D10-12 and from old WoW64); VS, PS 2-3 and ps_1_x lowered into the translator's IR, with AIR
  point size and point coordinates; a DXSO corpus with parity against the MSL path (synthetic 152/152, conformance
  147/149 plus 2 triaged NaN cases); and the VM transport. D3D9's runtime still takes the MSL path (the switch is
  C3b); DXBC and DXIL output unchanged over 13 corpora; translator key now c298383e…. No release until C3d.
  C3b done 2026-11-09 at DXMTRS main b4da41c5 (b4da41c57a2e9bf676b34a28bfd820f740d24481), which MacNeutron pins:
  the runtime switch `shader.backend=msl|air` (default msl, which behaves as C3a); AIR records in
  `mtld3d_shaders.bin` (kinds 10-15, keyed by the translator key; older builds drop them); a pair goes AIR only when
  both stages are programmable and both lower with their real specialization, else both stay MSL, and any AIR library
  or pipeline failure sends that program to MSL for the session, with no retry (amends C3-13); the Mac mtld3d.so
  links airconv; prewarm under air; depth parity (synthetic 159/159, conformance 147/149 plus 2 triaged); the D3D9
  gates on both backends, Mac and VM, old and new WoW64. Translator key unchanged (c298383e…). No release until C3d.
  C3c done 2026-11-10 at DXMTRS main a93f9033 (a93f90336353ad350329c044ee7c255b357d3853), which MacNeutron pins:
  under `shader.backend=air`, fixed-function stages are built as vs_3_0/ps_3_0 DXSO (`mtld3d_core::ff_build`) and go
  through the same parse, lower and translate as programmable ones; FF reads that DXSO cannot name use four lowering
  hooks reachable only by registers no valid shader names; AIR FF records (kinds 8/9) carry the FF key and tokens and
  are rebuilt at prewarm when the builder's output changes (a 64-bit reader also accepts an i686 writer's FF key); a
  pair is AIR when both stages lower, FF or programmable, and neither has failed (amends C3b's rule); MSL output is
  byte-identical and its FF records stay sourceless (amends C3-18 for them; regenerable from C3d). Parity: synthetic
  521/521 (362 FF), conformance 847/896 plus 49 triaged; the D3D9 gates on both backends, Mac and VM, old and new
  WoW64, every FF stage AIR. Translator key now 051df2a0… (DXBC and DXIL output unchanged over 13 corpora). No
  release until C3d. One VM row stays open, unexplained: d3d-smoke present-held (forwarded D3D12 presents held about
  7.8 ms on 2 of every 3 frames for tens of frames, mostly on early boots; no code shared with that path changed); C3d
  blocks its release on 0 of 6 fresh-state boots showing it (frames 5-72 held >= 16).
  C3d's flip done 2026-11-10 at DXMTRS main b3673ad4 (b3673ad4187a82b86654109876350cdcb4cc4e9f), which MacNeutron
  pins: `shader.backend` defaults to `air`; `msl` stays the escape (`mtld3d.conf`, `MTLD3D_CONFIG`). Every D3D9 gate
  runs a default lane and an explicit msl lane, each stamped and proven by its cache's record kinds; a per-site
  comparator (`dxmt/d3d9-conf-compare.sh`) finds no site failing more under AIR on five legs (Mac arm64x; VM x64 and x86
  under old and new WoW64). A cache written under msl keeps its MSL records: from the second launch both sets prewarm
  until the deletion. The open present-held row showed no hold-mode run in six fresh-state boots (release evidence; the
  release build is measured again). Parked for the maintainer: the D3D9 benches (with the flip's per-launch costs:
  encoder-thread lowering, FF prefilter, per-draw backend probe, z-fighting, the upgrade path), then the MSL emitters'
  deletion and the release with C3 complete.
  C4's first push, 2026-11-11, DXMTRS main 1d3fa95b (1d3fa95b36902fa3d90ab2313d8cbc96d0625f76): a core hazard fix for
  every API. A render pass rewriting a target last written 128+ ids back no longer waits on that write
  unconditionally (its strong bit masked the lane of a weak wait, so a read of a resource written exactly 64 ids back
  had no fence edge; the C++ DXMT shares the gap); the wait is gated `< LANE` like every other. `d3d11_hazards lane64`
  pins it on the Mac and in the VM. Also `runtime.mode` (`mtld3d` | `core`) and its process latch; every build runs
  `mtld3d`. The present-held row is not this change's: on one image, the hold mode showed in 2 of 3 boots of b3673ad4
  and 0 of 3 of this commit, so C3's release still measures it.
  C4a done 2026-11-11 at DXMTRS main b86ced33 (b86ced330965406b5ddc1b86e1a678d31be98b6a), which MacNeutron pins: D3D9
  as a DXMT front end, off by default. The core gains a queue profile (`Device::with_config`; D3D10-12's defaults
  unchanged), direct binds and `initialize_dxso`; `d3d9-rt` holds D3D9's `CoreDevice`. A build with
  `-Dd3d9_core_runtime=true` (MacNeutron's `DXMT_D3D9_CORE=1`) and `runtime.mode=core` runs `d3d9.dll`'s device on
  dxmt-core: Clear, Present, read-back and a pre-transformed fixed-function `DrawPrimitiveUP`; every other entry point
  answers D3DERR_NOTAVAILABLE under core, and the native arm64 half keeps mtld3d (no Mac ARM64 winemetal). Gated by a
  core lane proven by the code that ran (Mac; VM x64 and x86 under old and new WoW64) and a mutation run; D3D10-12
  gates unchanged. Probe (Mac x64, 2000 draws a frame): wall -3%, CPU +55% a frame, unprofiled; the slice rebuilds its
  fixed-function tokens every draw (C4b keys pipelines first, then re-benches); the VM and 32-bit address-space probes are
  C4b's first VM job's exit criteria.
  C4b-1 done 2026-11-11 at DXMTRS main 9ff1f787 (9ff1f787fbe9ebbd1eaf60b792b0efece89e48ac), which MacNeutron pins: one compiled-pipeline
  cache in the core for every API. D3D11's compile scheduler and keyed cache move into dxmt-core
  (`dxmt_core::scheduler`, `dxmt_core::pipeline_cache`: `Compiled<S>`, `PsoMap<K, S>`), D3D11 on them unchanged; the core
  path's fixed-function pipelines key on the fixed-function state (`FfPipelineKey` in mtld3d-core) and build only on a
  miss, still synchronously (async compile, draw skipping and prewarm are C4d's). Probe (Mac x64, 2000 draws a frame, one
  environment): core CPU 1.72 ms a frame against mtld3d's 1.90 (-9.6%) and C4a's 3.42 (-50%); the VM's guest rows show
  core +31% (x64) and +47% (x86) over mtld3d, whose guest presents bypass the host presenter: an open finding for C4c.
  i686 address space under core stays below mtld3d's.
  C4b-2 done 2026-11-12 at DXMTRS main 162685bd (162685bdd23db6f30b2186b936c7f25bf35cc491), which MacNeutron pins:
  programmable SM1-3 draws on the core runtime. `PipelineKey` (mtld3d-core) keys each stage by its fixed-function key or
  its exact tokens and the layout by its declaration; vs_1_1-3_0 and ps_1_x-3_0 with float, int and bool constants and
  relative addressing, DrawPrimitive, DrawIndexedPrimitive, their UP forms and fans, colour write masks, wireframe, cull,
  viewport, clip planes, fog, alpha test and sRGB writes draw under core; samplers, blending, scissor, transformed
  fixed-function, bump-env and texture-stage constants and an attribute crossing a UP stride still answer D3DERR_NOTAVAILABLE.
  `d3d9_triangle shaders` (22 rows) gates every D3D9 lane. Probe (Mac x64): core CPU +31% (ff) and +23% (prog) a frame
  over mtld3d, the cost of re-emitting PSO, viewport, rasterizer state and binds on every draw; dedup is C4d's.
  C4b-3a done 2026-11-12: Wine's stateblock test on the core runtime. API state (transforms, material, lights, stage,
  sampler and scissor states, textures, stream sources and frequencies, indices, state blocks) and inert objects
  (vertex/index buffers, 2D/cube/volume textures, additional swap chains: CPU staging only, no Metal object, their locks
  refused until C4b-3b and C4c) are taken under core; SetRenderTarget binds a back buffer at index 0 (additional chains
  alias the one back buffer); a core UP draw unbinds stream 0 and the indices; a fixed-function draw whose cascade reads
  a bound texture is refused (FfTexture) until C4b-3b. Every mtld3d crossing reachable on a core device is guarded inside
  the method that makes it (state-block Apply and teardown call them directly). Gated: conformance stateblock under the
  core lane equals the default lane (executed 14738, 0 failing, 0 crashes, the runtime proven from the logs), 12
  `d3d9_triangle state` rows in every D3D9 lane on the Mac and in the VM, mutation REDs; default runtime, translator key
  and imports unchanged. C4b-3b (textures, samplers, uploads, the full fixed-function pipeline) and C4b-3c (targets,
  MRT, depth, scissor) follow.
  C4b-3b done 2026-11-12 at DXMTRS main ed03918b (ed03918b3ffb71a9231ed8ba47ce9ea7265eb83a), which MacNeutron pins:
  pixel-stage textures, samplers and the fixed-function pixel cascade on the core runtime. d3d9-rt keeps a texture store
  (2D, cube, volume; view twins for sRGB and swizzles; generated mips) and a sampler cache with an opaque black fallback;
  a texture is created and uploaded at its first sampling bind and re-uploaded from its staging (dirty masks and rects,
  cube faces, volume depth, block-compressed extents, formats converted to A8R8G8B8) at record time, in blit passes that
  merge between render passes, a staged-bytes budget committing the chunk early (32 MiB in 32-bit processes). The draw's
  stage walk, the LOD table and the texture variant are mtld3d-core functions both runtimes share. Staging is the truth
  under core: never dropped, never read back. Refused with D3DERR_NOTAVAILABLE, each by name: a depth texture sampled,
  fetch4, a render-target texture sampled, a vertex texture, and a render-target or depth texture's LockRect (C4b-3d).
  Gated: 33 `d3d9_triangle textures` rows (and the 32-bit tex-big256) in every D3D9 lane on the Mac and in the VM,
  exactly the five refusals under core, mutation REDs; default runtime, translator key and imports unchanged. Probe
  (Mac x64, textured): core CPU +22% a frame over mtld3d, C4b-2's re-emission cost. Wine's visual and device suites
  create no core device yet: core's CreateDevice refuses an auto depth-stencil and multisampling until C4b-3d. C4b-3c
  (transformed fixed-function, lights, vertex textures) and C4b-3d (targets, MRT, depth, MSAA, fetch4, scissor) follow.
  C4b-3c done 2026-11-12 at DXMTRS main a841fe8d (a841fe8d1a093567f66dc0b13b51cd22d60eb40b), which MacNeutron pins: the
  fixed-function vertex stage and vertex textures on the core runtime. A draw with no vertex shader over an
  untransformed layout takes the fixed-function vertex stage, whose key and shader the core already built for XYZRHW:
  `FfState::ff_vs_rows` (mtld3d-core) assembles its constant rows (WV, PROJ, fog, ambient, material, up to eight
  lights, texture transforms, the blend palette) whole for each draw, where mtld3d sends its encoder mirror deltas. A
  vs_3_0 that samples binds the vertex slots it declares (texture, view, sampler, the opaque black for none) and the
  vertex LOD table; the programmable vertex key carries the bound kinds, and the LOD-table rule is one function both
  runtimes call. d3d9-rt's texture binds name their stage. The depth clamp is a pre-transformed draw's alone (it took
  every fixed-function draw for one: an untransformed draw under point scale would have clamped, not clipped). Still
  refused by name: blending and scissor, a depth, fetch4 or render-target texture sampled at a pixel or vertex slot,
  a render-target or depth texture's LockRect (C4b-3d). Gated: 31 `d3d9_triangle fixed` rows and six vertex-texture
  rows in every D3D9 lane on the Mac and in the VM, each first passed on mtld3d (default and msl), failing under core
  before the change and passing strict after it; five mutation REDs; default runtime, translator key and imports
  unchanged. Probe (Mac x64, lit): core CPU about 4x mtld3d's a frame (3.68 against 0.93 ms for 2000 draws): mtld3d
  sends no constants when nothing is dirty, the core rebuilds the rows and re-emits every bind each draw; that dedup
  is C4d's. C4b-3d (targets, MRT, depth, MSAA, blending, fetch4, scissor, the creation gate) follows, in five slices
  (3d1 the target set, 3d2 depth-stencil with the auto-depth creation gate, depth sampling and fetch4, 3d3 blending,
  scissor and partial clears, 3d4 MRT, 3d5 MSAA and the Wine suite sweep).
  C4b-3d1 done 2026-11-13 at DXMTRS main 2acb1d99 (2acb1d997de77b5b02cac7f1758b8421c5b001f7), which MacNeutron pins:
  render targets other than the back buffer on the core runtime. d3d9-rt's `CoreDevice` keeps a colour target by
  reference (`TargetRef`: the back buffer, or a level and cube face of a stored render-target texture) and resolves it
  for its render pass, Clear and read-back; a change of target ends the open pass. A render-target texture is a core
  texture with the render-target usage, tracked a subresource at a time and zeroed by the core's initializer; a level
  or face is attached through a 2D view of that one subresource; a `CreateRenderTarget` surface is one under an id of
  its own. The pipeline key carries colour 0's pixel format. `SetRenderTarget(0, ...)`, draws, whole-target `Clear`
  and `GetRenderTargetData` (mtld3d's checks and answers) use the bound target's size and format; a CPU write into a
  render target uploads at the write; an AUTOGENMIPMAP target's chain is regenerated when slot 0 leaves it; sRGB write
  stays in the pixel shader for every target. A sampled render-target texture is no longer refused. Still refused by
  name: a target at index 1-3 (3d4), a multisampled `CreateRenderTarget` (3d5), an additional swap chain's back buffer
  read back, blending and scissor, depth and fetch4 textures, a render-target texture's LockRect. Gated: 22
  `d3d9_triangle targets` rows in every D3D9 lane on the Mac and in the VM, each first passed on mtld3d (default and
  msl) and passing strict under core after the change (18 failed under core before it, two are the refusals above, one
  asserts D3D9's own answer for a cube's lock, one a size past the device's limit); five mutation REDs; the stateblock
  conformance under core; default runtime, translator key and imports unchanged. Not covered by a row: a wrong
  pipeline colour format on a 4-byte target that is not BGRA. Probe (Mac x64, textured, single runs about 0.1 ms
  apart): core CPU 2.57 ms a frame against mtld3d's 1.90 (+35%), not above C4b-3c's tip (2.72 and 2.80), a draw
  reading its target's shape without a reference; the core's textured draw was 2.25 at C4b-3b, so C4b-3c's vertex-slot
  work on every draw costs about 0.5 ms a frame, C4d's to remove with the re-emission.
  C4b-3d2 done 2026-11-13 at DXMTRS main 63f9fda0 (63f9fda05cd1873a228eb817a5b5be4316f6186e), which MacNeutron pins: the
  depth-stencil on the core runtime. d3d9-rt's `CoreDevice` keeps a depth-stencil by reference (`DepthRef`: a level of a
  stored depth texture) beside the colour target; `depth_state()` says whether one is bound and whether a pass on the
  colour target can attach it (it has the target's size). The front end's `core_key_depth` turns that into the draw's
  key format (`PipelineKey::depth_format`), and the key alone decides the pass (with the depth-stencil or without), the
  pipeline's depth and stencil formats, a pixel shader's depth output, the depth-stencil state and the pre-transformed
  clamp lane; a change of depth-stencil, or a draw of the other kind, ends the open pass. A depth texture is a core
  texture on the render-target path (zeroed by the initializer: depth 0, stencil 0); the auto depth-stencil, a
  `CreateDepthStencilSurface` surface and a depth texture's level are each one. The creation gate takes
  `EnableAutoDepthStencil` (`D3DRS_ZENABLE` then defaults on, as under mtld3d). The state is mtld3d-core's
  `depth_stencil_state` made into Metal state objects the device keeps, set with the stencil reference at each draw; the
  depth bias goes through the vertex stage's lane, the slope scale through the rasterizer record. `Clear` takes Z and
  stencil over the whole target (planes the format has not are dropped, as D3D9 has it). Depth textures are sampled with
  mtld3d's variant masks: a shadow sample through a less-equal compare sampler, INTZ/DF16/DF24 raw through a point
  sampler, fetch4 on every eligible format. A depth-stencil of another size than the target, or one the draw samples, is
  left out of that draw's pass when the draw uses no plane. Still refused by name: a draw that tests or writes a
  depth-stencil of another size than the target and a Z or stencil Clear of one (3d3), a draw that tests or writes the
  depth-stencil it samples, a depth texture at a vertex sampler and a dynamic depth texture's lock (C4c), a multisampled
  depth-stencil surface (3d5). Gated: 29 `d3d9_triangle depth` rows on a device with an auto D24S8 in every D3D9 lane on
  the Mac and in the VM, each first passed on mtld3d (default and msl), failing under core before the change (the device
  was refused) and passing strict after it, with no report under the Metal validation layer; `tex-fetch4` over four
  values and a new `tex-fetch4a`; six mutation REDs; the stateblock conformance under core; default runtime, translator
  key and imports unchanged. Not covered by a row: a shadow sample of a D24S8 texture, `EnableAutoDepthStencil` with
  format 0. Probe (Mac x64, textured): inconclusive. The Mac was in use and identical code read 0.5-0.7 ms a frame
  apart; against C4b-3d1's tip the core's textured draw read higher in two comparisons of single runs (4.07 vs the
  recorded 2.57; 3.50 vs 2.63 and 2.03) and lower in five interleaved rounds (mean 3.15 vs 3.80, mtld3d 1.78), so no
  regression is measurable and the direction is unknown; C4b-3d3's probe retakes it on a quiet Mac, each tip on its own
  build. Wine's `visual` under core (measured, not gated) now creates its device and stops at
  `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)`, which the core does not implement: the first thing 3d5's sweep needs.
  C4b-3d3 done 2026-11-14 at DXMTRS main 8bca6b74 (8bca6b748b92fd036e3101e3f54935d5677c6f09), which MacNeutron pins:
  partial clears, the scissor test, a depth-stencil of any size, hardware sRGB write and blending on the core runtime.
  `Clear` resolves each attachment (the colour target; the depth-stencil, against its own size) to a whole clear or to
  regions with mtld3d's own rules, which moved to mtld3d-core for both runtimes (`clear_area`: the rects cut to the
  scissor, the viewport clip, the cover test): a whole clear is the core's Clear pass, regions are dxmt-core's quad
  clear (`ClearRtv`, D3D11's `ClearView`) on that view alone, which takes nothing of the draws' state. A draw carries
  its scissor as mtld3d resolves it (the rect with the test on, the drawn viewport with it off); d3d9-rt clamps it to
  the pass's extent, records nothing for a draw cut to no pixel, and records the rect at the first draw of each pass and
  on a change. The bound depth-stencil is attached whatever its size and the pass draws to the smaller of the two
  (`DepthState` is gone; the pre-transformed clamp follows the bound depth-stencil, as mtld3d's). A target with an sRGB
  twin (the back buffer, BGRA8 and RGBA8 render targets) is attached through the twin's view by a draw or Clear made
  under `D3DRS_SRGBWRITEENABLE`: the key names the twin's format, the pixel stage stops encoding and Metal encodes after
  the blender; d3d9-rt keeps no sRGB state, and a pass on the other view ends first. A draw's key carries its blend
  (`CoreBlend`: mtld3d's effective blend and its destination-alpha clamp, in mtld3d's enums, mapped to the wire's
  numbers by name in d3d9-rt); the blend factor colour is recorded, with the draw's stencil reference, by the draws that
  read it. Two changes to dxmt-core, the first of C4, each with its test: the optimizer no longer makes a whole-view
  Clear the load action of a pass whose area is smaller than the view (a load action reaches the pass's area only: D3D11
  with a depth view larger than its targets had the same hole), and a stencil-only quad clear takes a fragment that
  writes no depth. No draw state is refused by the UP draws any more; still refused by name: a shader of another device,
  a depth texture at a vertex sampler, a depth-stencil sampled while the draw uses it and a dynamic depth texture's lock
  (C4c), a target at index 1 (3d4), multisampled surfaces (3d5). Gated: 36 `d3d9_triangle blend` rows on a device with
  an auto D24S8 in every D3D9 lane on the Mac and in the VM, `ds-size` turned from a refusal into a result and a new
  `ds-rhwclip` (31 depth rows), each first passed on mtld3d (Mac default and msl, VM default), red under core before its
  commit and green strict after it, with no report under Metal's validation layer in any core launch; seven mutation
  REDs, each on its row alone (the one that removes the area guard is this slice's proof that a load action stops at the
  pass's area); the stateblock conformance under core; the default runtime's Mac conformance, translator key and imports
  unchanged; the x64 check and the VM's D3D10-12 gate over the changed dxmt-core. Not covered by a row: blending on
  32-bit float targets, a blended or scissored multisampled or second target. Known costs, handed to C4d: a Clear made
  with sRGB write off before draws made with it on (or the reverse) is a pass of its own, as under mtld3d; a partial Z
  Clear of a larger depth-stencil is a pass of its own at every Clear; a blended draw asks its target's format through
  `GetDesc`. Probe (Mac x64, textured, one run each): the core read 2.18 (tex) and 2.25 (texdepth) ms a frame against
  mtld3d's 1.70 and 1.77. The retake C4b-3d2 owed was made on a quiet Mac, each tip on its own full build: C4b-3d1's tip
  read 2.05 (mtld3d 1.50) and C4b-3d2's 2.22 and 2.18 (mtld3d 1.82 and 1.82). mtld3d's own figure moves 0.32 ms between
  builds with no change to its draw path, and every difference between the three tips' core figures is below that:
  neither C4b-3d2 nor this slice cost the textured draw anything this probe can measure, and the core stays 20-37% above
  mtld3d until C4d's dedup.
  C4b-3d4 done 2026-11-16 at DXMTRS main d4714e45 (d4714e45b3300772c8bb29b7e9732aed3ba7db36), which MacNeutron pins:
  render targets at index 1-3 on the core runtime. d3d9-rt's `CoreDevice` keeps three more targets by reference beside
  target 0. A draw's pass attaches the pass set: target 0 and each of them that resolves, has target 0's size (mtld3d's
  rule: one of another size stays bound and is skipped by draws) and is not a subresource already in the set. The
  draw's key is the one source of what the pass attaches: `PipelineKey::extras` names each attached slot's format,
  write mask and blend (`CoreExtra`), d3d9-rt opens the pass on exactly those slots in those formats, with the pass's
  slot count the highest attached slot + 1 (the core's optimizer compares that many), a hole left empty in the pass, in
  the pipeline and in the tile barrier's key, and a key the bound targets do not make is refused before any record
  (logged once, and a failure of its launch in the gates). mtld3d-core's `core_extras` fills a slot from mtld3d's own
  rules: `D3DRS_COLORWRITEENABLE1..3` when the bound program writes that `oCn` and an empty mask otherwise (the target
  is declared and keeps its pixels), the draw's blend clamped on that target's own destination alpha; a programmable
  pixel stage's variant exports the set's slots (`color_out_mask`, the set shifted by one), the fixed-function one
  colour 0 alone. Hardware sRGB is all-or-nothing over the set, as mtld3d's: every member through its twin when every
  member has one, else the pixel stage encodes every output. `Clear` goes to every bound colour target, in the set or
  not, each judged against its own extent, as one single-view core clear a target (a whole one folds into its slot of
  the next pass); d3d9-rt takes the linear colour and encodes it when the view it clears is not an sRGB one.
  `SetRenderTarget(1..3)` binds with mtld3d's bookkeeping (the slot's reference, the AUTOGENMIPMAP regeneration on
  leaving, no viewport or scissor reset) and a null unbinds; `GetRenderTarget` and `GetRenderTargetData` answer those
  targets. dxmt-core is unchanged (its pass, pipeline description, optimizer and tile barrier already carried eight
  slots); its test fakes print colours 1-7 and the tile pipeline. Decisions kept: a duplicate (one subresource at two
  indices, or two swap chains' back buffers, which alias under core) is drawn once, where mtld3d would hand Metal the
  texture twice; a set with a target that has no sRGB view is written as mtld3d writes it (encoded before the blender,
  a float target included), not per target as Windows does. Still refused by name: multisampled surfaces (3d5), a
  shader of another device, a depth texture at a vertex sampler, the depth-stencil sampled while a draw uses it and a
  dynamic depth texture's lock (C4c). Gated: 20 `d3d9_triangle mrt` rows on a device with an auto D24S8 in every D3D9
  lane on the Mac and in the VM, and `rt-mrt` turned from a refusal into a result, each first passed on mtld3d (Mac
  default and msl, VM default) with no expectation corrected, red under core until the commit that binds index 1-3 and
  green strict after it, with no report under Metal's validation layer in any core launch; seven mutation REDs, each
  on its row and in no other launch (a pass count of 1 merges two passes that differ at index 1 alone: the proof that
  the count is a correctness input); the stateblock conformance under core; the default runtime's Mac conformance,
  translator key and imports unchanged; the x64 check. Not covered by a row: one surface at two indices, alpha test,
  fog or a depth output with several targets, a texture sampled while one of its levels is attached at index 1-3 (the
  barrier's key is proven on the fakes only). Known costs, handed to C4d: a rect Clear with N targets bound is N small
  passes; a blended draw asks each attached target's format through `GetDesc`; an unwritten extra still keys its
  blend. Probe (Mac x64, textured, one colour target; the Mac was busy, load 5-10): this tip read the core at 3.93 and
  4.13 ms a frame against mtld3d's 1.47 and 1.93, and C4b-3d3's unchanged tip, rebuilt and run in the same window,
  3.85 against 1.87, where it had read 2.18 on a quiet Mac (cause not established): the difference between the tips is
  inside one run's spread
  (mtld3d's own figure moved 0.46 between two runs of this tip), so this slice costs the single-target draw nothing
  the probe can measure, and a quiet-Mac figure is owed to C4b-3d5's probe.
  C4b-3d5 done 2026-11-16 at DXMTRS main 82a588ba (82a588ba47e33d2e02748dd9dabe64a6edccf684), which MacNeutron pins, and
  with it C4b-3d as a whole: multisampling on the core runtime. A core texture has a sample count
  (`CoreTextureDesc::samples`, its two flags packed as `CoreTextureFlags`) and a multisampled one is viewed as a
  multisample 2D texture; a multisampled `CreateRenderTarget` or `CreateDepthStencilSurface` surface is one such
  texture, and only the device's back buffer keeps a single-sample twin (the presented texture; its multisampled
  companion is what draws and clears reach, and Present resolves it first), where mtld3d gives every multisampled target
  a twin. The pass and the pipeline take target 0's count (`PipelineKey::samples`); an index 1-3 target or a
  depth-stencil of another count is left out of the pass, as mtld3d leaves it (the depth clamp then reads "no depth
  plane", a Z or stencil Clear of that depth-stencil does nothing, and a whole Clear does not reach that extra: mtld3d
  clears apart only a bound target of another size). `D3DRS_MULTISAMPLEMASK` applies on a maskable multisampled target
  0, the back buffer nothing bound included; alpha-to-coverage (the ATOC and A2M1 requests) replaces the alpha test on a
  multisampled target 0 and is part of the key; both through mtld3d's own rules (`effective_sample_mask`,
  `alpha_to_coverage_requested`, `resolve_sample_count`). A resolve (`resolve_into`) goes through the view format last
  written, so an sRGB-written target averages linear and encodes, and is refused for a format the device cannot resolve.
  `StretchRect` has one shape under core, the whole-surface resolve of a multisampled render target into a single-sample
  one of the same size and a format whose bytes copy; every other shape stays a logged refusal,
  "IDirect3DDevice9::StretchRect (other than a multisample resolve)" (taken here from C4c because nothing else can read
  a multisampled pixel under core). `GetRenderTargetData` of a multisampled source is `D3DERR_INVALIDCALL`, as D3D9
  says; a device or surface type the adapter does not offer is `D3DERR_INVALIDCALL`;
  `CreateOffscreenPlainSurface(D3DPOOL_DEFAULT)` is made (a lockable surface that is no render target; nothing reads it
  under core until C4c). One dxmt-core change: the optimizer folded a resolve into its source's last render pass across
  passes that write or read the resolve's destination, for any front end (D3D11 reaches the fold with an array
  multisampled target only); it now folds only into the render pass right before it, with a unit test, at the cost of
  one resolve pass of its own where an unrelated pass lies between. Gated: 17 `d3d9_triangle msaa` rows on a device with
  a 4-sample back buffer and auto D24S8 in every D3D9 lane on the Mac and in the VM, `rt-msaa` and `ds-msaa` turned from
  refusals into results and `rt-plain` added, each first passed on mtld3d (one expectation corrected to what mtld3d
  does: the Clear of an extra of another count), red under core until its commit and green strict after it, with no
  report under Metal's validation layer in any core launch; seven mutation REDs, each on its row and on nothing outside
  the msaa launch; the stateblock conformance under core; the default runtime's Mac conformance, translator key and
  imports unchanged; the x64 check and the VM's D3D10-12 gate (dxmt-core changed). The whole-branch review found two
  defects against mtld3d (the mask on a back buffer nothing bound, the Clear of a depth-stencil of another count), each
  fixed with a cell that was red before. Coverage-dependent pixels are asserted as bands (48 to 208 of 255), the
  hardware's. Not covered by a row: a presented multisampled frame (proven on the fakes), a multisampled depth-stencil's
  resolved depth, sample-rate shading, centroid and `vPos` under multisampling; mtld3d's wait for a resolve to retire on
  the paravirtual GPU has no counterpart in the core (the VM's rows are the check). Known costs, handed to C4d: Present
  resolves a multisampled back buffer every frame; the capability table is rebuilt at each `StretchRect` resolve. Probe
  (Mac x64, textured, single-sample; the Mac in use all day): the core read 3.62 ms a frame against C4b-3d4's tip's 3.47
  in the same day, inside one run's spread; the quiet-Mac figure is still owed, to C4d's first probe.
  The Wine suite sweep under core (measured, not gated; `visual`, `device`, `d3d9ex` once each): none runs to its end.
  Each process ends within seconds at the first refused call its test does not survive: `IDirect3DSurface9::LockRect` of
  a render target (`visual`, in `stretchrect_test`'s read-back), `IDirect3DVertexBuffer9::Lock` (`device`, in
  `test_draw_primitive`), `IDirect3DDevice9::GetDirect3D` (`d3d9ex`, in `test_reset`); no panic, no hang, no validation
  report. Those three are C4c's first work, since each hides everything after it; the refusals met on the way that fail
  tests without ending them are `StretchRect`'s other shapes, `Reset`, fullscreen devices, the cursor calls,
  `CreateQuery`, the `Ex` surface creators and the swap chain's display-mode getters.
  C4c-1 done 2026-11-16 at DXMTRS main eeb92e2e (eeb92e2ea868cef0136cd269d13568b83f1a5f2d), which MacNeutron pins:
  vertex and index buffers and the draws that read them on the core runtime, C4c's first slice, with `GetDirect3D` (the
  sweep's three fatal calls but the read-back). Under core every buffer is staged: its CPU backing is the truth and is
  never released, `Lock` and `Unlock` are bookkeeping on it, and the GPU side is one dxmt-core buffer a D3D9 buffer,
  made at its first draw. The draw uploads the dirty span through the staging ring before it is encoded; a buffer still
  locked stays dirty, and a lock of a buffer mtld3d maps directly dirties the whole buffer, since mtld3d shows every
  byte written through that pointer. `PipelineKey` holds a form a stream (`StreamKey`: unused, inline, bound with its
  stride, step and rate, or null) in place of one stride, and the layouts come from mtld3d's rules lifted into
  mtld3d-core (`inline_stream_layout`, `null_stream_layout`, `stepped_stream_layout`), with a parity test against
  mtld3d's own view for every feed; every attribute keeps slot = stream. A declared stream with nothing bound reads
  zeros through a constant layout, under UP draws too (a short float element then reads w = 1, as on mtld3d).
  `DrawPrimitive` and `DrawIndexedPrimitive` answer in the order of mtld3d's checks and share the UP draws' body:
  d3d9-rt's `draw` (renamed from `draw_up`) takes each stream as bytes, zeros or a stored buffer with its offset, and
  the indices as bytes or a stored buffer. Fans are rewritten as mtld3d rewrites them (an indexed fan folds its base
  once; the rule for which fan rides the shared pattern is one function both runtimes ask, `fan_fits_pattern`), and
  instancing follows mtld3d's rule. Refused by name under core: an attribute crossing its stride and a stream offset off
  four bytes (both until C4c-2), a buffer of another device (kept: the dirty range is one a buffer). Kept divergence, in
  the core's favour: a static WRITEONLY default-pool buffer locked again keeps the bytes it held, where mtld3d, which
  released the backing, uploads zeros for what the title does not rewrite (ended by C4c-2b: the core releases that
  backing by mtld3d's rule). Gated: 27 `d3d9_triangle buffers` rows in
  every D3D9 lane on the Mac and in the VM, `state-lock` turned from a refusal into a result, each first passed on
  mtld3d with no expectation corrected, red under core until its commit and green strict after it, with no report under
  Metal's validation layer in any of the ten core launches; eleven mutation REDs, each on its row and on nothing outside
  the buffers launch; the stateblock conformance under core; the default runtime's Mac conformance, translator key and
  imports unchanged; the VM's D3D9 gate on default and core, old and new WoW64; the x64 check (one row failed in the
  first run, "presents to a minimised window", the row known to fail while the Mac is in use; run again by the post-push
  check). No D3D10-12 gate: no line of dxmt-core, d3d11 or winemetal changed. The whole-branch review found no Critical
  and no Important defect. Not covered by a row: `ProcessVertices` (refused), more than two streams, a buffer bound at
  two streams, point and line-strip primitives from buffers, an instanced fixed-function draw, a buffer of another
  device. Known costs: a dynamic buffer appended to between draws pays a whole-buffer upload and a pass split a draw
  (C4c-2 maps it directly); a 32-bit process holds every backing and sends a first draw's whole buffer through the
  staging ring (C4c-2's cap); a buffer bound at two streams and drawn while locked uploads twice a draw (C4d). Probe
  (Mac x64, textured, UP draws, the Mac in use): the core read 3.27 ms a frame against 3.65 at the base in the same hour
  (3.20 against 3.28 with depth), mtld3d 1.83 against 1.73: no cost of the slice is measurable on the UP path. No bench
  draws from buffers yet; C4c-2 adds one with the dynamic path.
  C4c-2a done 2026-11-17 at DXMTRS main a4c94873 (a4c94873c4e1b44880362796e208b6de8402e644), which MacNeutron pins:
  attributes crossing their stride and stream offsets off four bytes on the core runtime, with mtld3d's own fetch plan.
  C4c-2 was split (ruling S-1): 2a is this, the part that removes refusals; 2b is the dynamic path, the part that
  removes a cost. `StreamKey::Bound` gains the stream's shift (its offset's low two bits), so two offsets that differ
  only there are two pipelines, as on mtld3d. The pipeline source asks mtld3d-core's `CrossingFetch` whenever an
  attribute crosses or a stream is shifted, and holds its answer: the attributes and layouts a Metal slot, and for
  every slot the stream it reads and the bytes its binding is advanced by. A crossing attribute moves to a spare slot
  among the sixteen stream slots, at descriptor offset 0, bound to the same buffer advanced by the attribute's offset;
  a shifted stream binds at its offset rounded down to four and its attributes gain the shift. d3d9-rt's `draw` binds
  slot by slot before the pass opens: a stored buffer uploaded once, however many slots bind it; inline bytes advanced and
  zero-filled to the fed length, which never refuses; zeros as before. Refused by name under core, where mtld3d widens
  and draws wrong data: a layout no binding can fetch (an attribute wider than its stride, an advanced binding that
  starts outside its buffer, more than sixteen attributes that cross or are shifted), one line a draw kind,
  "(a layout no binding can fetch)"; the row `buf-wide` pins the buffer draws' line. The index formats answer through one helper
  (`convert::index_format`) at their five sites. Gated: seven `d3d9_triangle buffers` rows (33 in all) in every D3D9
  lane on the Mac and in the VM, and the `shaders` launch's crossing row turned from a refusal into pixels, each first
  passed on mtld3d with no expectation corrected (the buffers rows also under Metal's validation layer there, with no
  report), red under
  core until its commit and green strict after it, with no report under the layer in any of the ten core launches;
  five mutation REDs, each on its row and on nothing outside the buffers launch; the stateblock conformance under
  core; the default runtime's Mac conformance, translator key and imports equal to the base's; the VM's D3D9 gate on
  default and core, old and new WoW64; the x64 check (one row failed in the first run: the hazards program's reference
  run on D3DMetal, a process that loads no code of ours, printed "fence-order 1 0" for "1 1" and stopped there, a flake
  of that reference seen once before, on 2026-10-10; run again by the post-push check). No D3D10-12 gate: no file outside `src/d3d9` changed. The whole-branch review found no Critical
  and no Important defect. Not covered by a row: the advanced binding outside its buffer and the index helper (a unit
  test each), the UP draws' refusal line, a declaration of more than sixteen attributes, a crossing attribute on a
  per-instance stream, a zero-stride stream while a fetch is built. Probe (Mac x64, the Mac in use, CPU ms a frame, tip against base): textured UP draws 3.87 against 3.92 on the
  core and 1.97 against 1.80 on mtld3d; draws from a static buffer 4.10 against 3.87 and 2.08 against 2.03; a dynamic
  buffer appended to between draws 54.1 against 54.8 on the core and 2.47 against 2.50 on mtld3d. The slice costs
  nothing measurable; the dynamic path under core is 22 times mtld3d's, which is C4c-2b's work and its before figure.
  C4c-2b done 2026-11-17 at DXMTRS main 21d0ca5f (21d0ca5f5e0e2e38fad17bb944d26ca295edadfa), which MacNeutron pins:
  default-pool dynamic buffers mapped directly on the core runtime, the static WRITEONLY backing's release, and a
  count and a cap of the vertex and index bytes in flight. The model is D3D11's `Map` of a dynamic buffer under D3D9's
  lock rules. At creation a buffer that mtld3d-core's `classify_map_mode` calls `Direct` gets a dxmt-core
  `DynamicBuffer` in d3d9-rt (`dynamic_create`) and no CPU backing (`BufferBacking::released`): its length is the
  logical length up to one 4096-byte page and whole pages above; its names are suballocated from one page, placed on
  32-bit x86 and never write-combined (D3D9 reads these bytes); the first name is zeroed. `Lock` asks d3d9-rt
  (`dynamic_lock`), which calls mtld3d-core's `plan_lock` with the sequence of the last draw that named the buffer and
  answers the pointer: the same memory, or a rename, which is the name's next suballocation or, when it has none, a
  new name, with the old bytes copied over first when the lock keeps them. Every CPU reader (the pointer, the preserve
  copy, an indexed fan's window through `dynamic_copy`) reads the name the API thread holds, never the encode thread's.
  A draw uploads nothing and splits no pass: it binds the buffer like any stored one and stamps it after its emit, and
  the first such draw of a device logs "d3d9 core: dynamic buffers are read in place" once, the gates' proof. A name
  Metal refuses: at creation the buffer is staged as before, at a rename the lock writes in place, each with an error
  line the gates fail on. A static default-pool WRITEONLY vertex buffer releases its backing after its upload by
  mtld3d's rule and guards, and a backing a later windowed lock re-creates is uploaded at the Unlock that ends the
  lock, as mtld3d uploads each window, so two windows locked before a draw never upload the zeros between them. Index
  buffers that are staged keep their backing (a fan reads it; a directly mapped index buffer has none and a fan reads
  it through `dynamic_copy`; the static WRITEONLY release is C4c-3b's). The count is one ledger in d3d9-rt (`Retained`):
  a renamed-away name's size and a staged buffer upload's length enter at the chunk being recorded and leave when the
  GPU has retired it; at `memory.vbibRetentionCapMB` (512 MiB, 0 for none, textures not counted) the API thread waits
  for the oldest before a new name or an upload. Gated: six `d3d9_triangle buffers` rows (39 in all) in every D3D9
  lane on the Mac and in the VM, each first passed on mtld3d (also under Metal's validation layer there, with no
  report); `buf-writeonly` red under core until the release's commit and the proof token `buffers-direct` until the
  last one, green strict after it (the other five rows pin behaviour the staged model already gave), with no report
  under the layer in any of the ten core launches; nine mutation REDs, each on its row and on nothing outside the
  buffers launch; the stateblock conformance under core; the default runtime's Mac conformance, translator key and
  imports equal to the base's; the VM's D3D9 gate on default and core, old and new WoW64, where the planned flags hold
  on x64 and on i386 with no fall-back; the x64 check. Two rows outside the change failed while the Mac was in heavy
  use. The x64 check's nvapi row on the connected-display count failed in the first run and passed in the second. The
  VM's default lane on new WoW64 ran 211159 x64 `visual` tests for 211219 in both runs, the count seen in C3b and
  C4b-1, before this code existed. The reading, from the test's source (its skip line was not captured): Wine's
  `test_map_synchronisation` estimates from two timed draws how many triangles fit in 100 ms and skips its four cases
  of fifteen checks, 60, when the estimate passes the device's primitive count. The Mac's plain build, which holds no
  core code, ran the same 211159 in the first run and 211219 in the second, and the VM's old WoW64 lane ran all of
  them with the same binary both times (ruling T6-3; the gate's floor is owed that case). One
  expectation was added after a row's first green: `buf-writeonly`'s two-window cell gained a pixel (ruling T6-1),
  because its first pixel could not see the zeros between the windows and the mutant that drops the Unlock upload
  stayed green; the added pixel is green on both runtimes and red under that mutant. No D3D10-12 gate: no file
  outside `src/d3d9` changed. The whole-branch review found no Critical and no Important defect. Not covered by a
  row: the zeroed first name, a name Metal refuses (the staged fall-back, the failed rename), the cap (unit tests on
  the ledger only), the absence of write-combining, a dynamic buffer of another pool, the build without the core
  runtime (the Rust gate's clippy only). Known gaps, under core only: a write
  past the end of a dynamic buffer whose bytes end at a page's end leaves the name's memory, where mtld3d's padded page
  absorbs it; on 32-bit x86 that memory is an exact heap allocation, so a title that overruns its dynamic buffers
  corrupts its heap (a guard page of padding is owed before core is the default, C4d); a `Lock` from a second thread
  on a device made without `D3DCREATE_MULTITHREADED` reads d3d9-rt's store unguarded; a fan's window is bounded by the
  logical length, so one that reaches into mtld3d's padding is refused where mtld3d drew; `DynamicBuffer` never drops
  a free name (a trim is a dxmt-core change); a failed rename logs at every lock. Inherited from mtld3d, a finding for
  the maintainer: a whole (size 0) or a `READONLY` lock of a re-created WRITEONLY backing uploads its zeros over the
  device's bytes. Probe (Mac x64, the Mac in heavy use, CPU ms a frame, tip against base in the same half hour): a
  dynamic buffer appended to between draws 3.32 against 54.65 on the core (600 frames in 2.5 s against 28.9 s, 182 MB
  against 468 MB) and 1.98 against 1.88 on mtld3d; draws from a static buffer 2.63 against 2.73 and 1.97 against 1.58;
  textured UP draws 2.32 against 2.30 and 1.65 against 1.52. The dynamic path costs the core what its other draws
  cost; what is left over mtld3d is the core's per-draw cost, C4d's work.
  C4c-3a done 2026-11-18 at DXMTRS main a1d45dd5, which MacNeutron pins: colour read back through locks on the core
  runtime, on the two primitives it had (`read_back`, `texture_upload`). C4c-3 was split (ruling S-2): 3a is this; 3b
  is what needs a new primitive. d3d9-rt gains `read_presented`: a rect of the back buffer as Present would show it
  now, a multisampled back buffer first resolved into the presented texture by the resolve Present records (mtld3d
  resolves at every flush, so its lock and its front buffer return the frame as drawn so far); `read_back` shares its
  body, and the device's read-back buffer only grows. In the front end the arms sit inside mtld3d's own functions,
  where a core device's null handles would otherwise take the wrong branch without a word. A lockable render target
  keeps its staging: `LockRect` runs mtld3d's `lockable_rt_lock_rect` whole, its read-back step being the core's read
  of the target (skipped under `DISCARD`), and `UnlockRect` uploads the staging whole at record time unless the lock
  was `READONLY`; it is no `GetRenderTargetData` or `GetFrontBufferData` destination. The back buffer locks
  `READONLY` through mtld3d's `backbuffer_lock_readback`, the read being `read_presented` into a zeroed page of the
  rect. A render-target texture's level locks through `IDirect3DTexture9::LockRect`: one arm at the head of
  `materialize_subresource_from_gpu`, the seam every reader passes, flushes the texture's CPU writes, reads the level
  into its staging and fails the lock with an error line when it cannot (no branch answers success over unread
  staging); what the lock writes goes up at `UnlockRect` as before. `GetFrontBufferData`, both forms, is
  `read_presented` of the whole back buffer after mtld3d's checks; it and `GetRenderTargetData` form their
  destination's bytes in one helper. A non-lockable or multisampled target, a depth-stencil surface and a
  `GetSurfaceLevel` or `GetCubeMapSurface` surface of a target or depth texture answer mtld3d's `INVALIDCALL`.
  Refused by name under core, 3b's: a writable lock of the back buffer (a device made with
  `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`), a lock of an additional swap chain's back buffer (it aliases the device's),
  the lock of a DYNAMIC depth-format texture, `GetDC`, `ReleaseDC`, `IDirect3DSurface9::PreLoad`.
  Kept, mtld3d's and now the core's too:
  a DEFAULT render-target texture's `LockRect` succeeds; the back buffer locks `READONLY` without the lockable flag;
  a `READONLY` back-buffer lock reports the rect's own pitch; the front buffer is the back buffer as it is now.
  Gates at the tips (a1d45dd5 + MacNeutron 95004a33): the translator key and the imports equal the base's; the D3D9
  gate ok on default, msl and core on the Mac, and on default and core in the VM on old and new WoW64; conformance
  with only triaged sites; the x64 check whole; ten core launches under Metal's validation layer, 0 reports;
  nine mutants each red on its named row alone, on the texel named beforehand. The whole-branch review found
  nothing Critical and one Important, fixed (the
  front buffer's arm repeated a raw page-slice tail; one helper now). No row covers `IDirect3DTexture9::LockRect`
  of a depth-stencil-usage texture, a rect lock of a render-target level, `GetFrontBufferData` into a level of a
  system-memory texture, or `UnlockRect` of a render-target level's surface. Kept open: a full back-buffer lock
  aborts when its page cannot be allocated (both runtimes). No probe: nothing a bench's frame takes changed.
  C4c-3b done 2026-11-18 at DXMTRS main 4eea8d68, which MacNeutron pins: device contexts, the writable back buffer and
  dynamic depth textures on the core runtime. d3d9-rt gains two primitives beside one ring helper that the three
  uploads share: `write_presented` (the whole single-sample back buffer from CPU rows, in application order; nothing
  recorded on a multisampled one) and `depth_upload` (a whole level of a depth texture from a float depth plane and,
  for the combined format, a stencil plane: one plain copy for `Depth32Float`, two copies with Metal's plane options
  for `Depth32FloatStencil8`; never through `texture_upload`, whose block is D3D9's texel). In the front end the back
  buffer's arms sit at the heads of mtld3d's two functions, `readback_full_backbuffer` (a zeroed page,
  `read_presented` of the whole frame) and `backbuffer_page_upload` (`write_presented` of the held page), which
  mtld3d's own call sites reach from the writable lock, `GetDC`, `ReleaseDC` and the Unlock tail; 3a's own Unlock arm
  for a back buffer is gone (it dropped the page without asking whether a lock was held, which a held DC would not
  have survived), so mtld3d's tail and its gates run on both runtimes. `GetDC` and `ReleaseDC` lose their refusal for
  every kind: CPU stores and texture levels run mtld3d's code as it is, a render-target level reads through 3a's arm,
  and a lockable render target is told by one helper (a core texture, staging, not depth-stencil) where mtld3d asks
  for a colour handle: its staging is refreshed at `GetDC` and uploaded at `ReleaseDC`. An additional swap chain's
  back buffer locks, gives a DC and is a `GetRenderTargetData` source as the device's back buffer it aliases. The
  surface's `PreLoad` is the no-op it is on mtld3d. A DYNAMIC depth-format texture (D16, D24X8, D24S8) locks as on
  mtld3d (packed CPU staging, dirty marks) and `core_flush_dirty`'s depth branch uploads each level that carries a
  mark at the next sampling bind (`PlaneLayout` at alignment 4, the shared unpack). Refused by name under core: a
  writable lock and a `GetDC` of a multisampled lockable back buffer (D3D9 forbids the pair). Divergences: a draw
  recorded between a back buffer's writable lock or `GetDC` and its unlock or `ReleaseDC` is overwritten by the
  core's write-back, where mtld3d's rides the frame's leading blits and lands under it; a dirty depth level is
  uploaded whole, so bytes written under `NO_DIRTY_UPDATE` outside a later marked rect reach the GPU. Kept: the
  writable lock reads the frame under `DISCARD`; `ReleaseDC` of the back buffer always writes back; an additional
  chain's own lockable flag is not read; the device's and a chain's back-buffer surfaces each hold their own lock,
  DC and page, so the last write-back wins. The static WRITEONLY index buffer's release left C4c-3 for C4d (it needs a
  read of a device buffer for a gain in memory alone; under core one CPU copy of each such buffer stays).
  Gates at the tips (4eea8d68 + MacNeutron e043b36e): the translator key and the imports equal the base's; the D3D9
  gate ok on default, msl and core on the Mac, and on default and core in the VM on old and new WoW64; conformance
  with only triaged sites; the x64 check whole; ten core launches under Metal's validation layer, 0 reports (the
  optioned plane copies among them); nine mutants each red on its named rows alone, on the texel named beforehand.
  The whole-branch review found nothing Critical or Important and five Minors, ledgered in the plan's notes with no
  fix round (one of them: on a multisampled lockable device the lock's refusal comes before two `INVALIDCALL`
  checks). No row covers simultaneous locks or DCs on the device's and a chain's back-buffer surfaces, `GetDC` on
  DYNAMIC or DEFAULT colour texture levels, cube faces or an R5G6B5 lockable target, D16 and D24X8 levels above 0,
  `NO_DIRTY_UPDATE` or `DISCARD` on a dynamic depth texture, or the stencil bytes' values; the `DISCARD` cell of
  the writable lock has no mutant. No probe: no draw or recorder file changed.
  C4c-4a done 2026-11-18 at DXMTRS main 3d5dcda3, which MacNeutron pins: `ColorFill`, `UpdateSurface` and
  `StretchRect`'s answers on the core runtime; C4c-4 is three slices, each with the one kind of primitive it needs.
  d3d9-rt gains `fill` (the whole of, or a rect of, any colour target the core resolves, bound or not, through the
  view of its stored format with the colour as given, as mtld3d's fill writes its bytes whatever sRGB write says;
  `clear` and `clear_rects` share its two emit bodies and emit what they did), `write_presented` takes a rect, and
  `texture_upload` refuses a multisampled texture. In the front end `resolve_stretch_surface` takes the device's
  runtime and classifies a core device's standalone surfaces by `core_target()` and `core_depth()` with a null
  handle in the kind; **every core arm is keyed on the device, never on those two tests alone** (`core_target()`
  answers the back buffer on either runtime, and the default lanes run on the build that has the feature).
  `StretchRect` loses its early jump: a core device runs mtld3d's gates, so what mtld3d rejects there is
  `D3DERR_INVALIDCALL` on both runtimes, and leaves at two exits (after the depth branch's gates, and after the
  scaling gate, each ahead of the first flush) for the resolve arm, where a valid shape other than the multisample
  resolve is still refused by name. `ColorFill` runs mtld3d's gates and then, for a render target (the back buffer,
  an additional chain's, a standalone target multisampled or not, a texture's level or cube face),
  `core_arms::color_fill`: the texture's owed uploads, the fill, and on success the GPU claim and an autogen
  chain's regeneration; a DEFAULT plain surface is filled in its staging as on mtld3d and only marked dirty.
  `UpdateSurface` into a texture is mtld3d's staging copies as they are; into a standalone target or the back
  buffer the region goes through `texture_upload` or `write_presented` in application order
  (`core_arms::update_color_target`). **Kept: on a core device only render-target levels, standalone targets and
  the back buffer are written by the GPU** (a plain surface and a level that is no target stay CPU-authoritative:
  the core reads no other texture back), which 4b's plain-to-plain copy has to keep. Answers that differ from
  mtld3d: a valid `StretchRect` other than the resolve is `D3DERR_NOTAVAILABLE` (the resolve-like shapes
  `resolve_shape` declines too: an additional chain's back buffer, an AUTOGENMIPMAP destination, X8 into A8, a
  sub-rect, multisampled into multisampled, the depth resolve), and mtld3d's failures found while running, behind
  the exits, are not reached. Refusals removed: `IDirect3DDevice9::ColorFill`, `IDirect3DDevice9::UpdateSurface`.
  Gated at that tip with the `blit` launch's 18 rows (`dxmt/tests/d3d9_triangle.cpp`; every verdict counts them,
  so a launch that is not wired fails): the plain build's translator key and `d3d9.dll` imports are the base's;
  d3d9-check default, msl and core, 18 of 18 on each; the Mac's conformance fails anew at triaged sites only;
  stateblock on both runtimes; the eleven launches under core with Metal's validation layer report nothing; the
  VM's D3D9 gate on both runtimes and both WoW64 forms, 18 rows in 64-bit and in 32-bit; the x64 check, all 420
  rows, at its second run (the first had one red: `d3d12_xess` under wined3d, no DXMT in the process, printed
  nothing and met the check's 120 s watchdog; not reproduced, not traced).
  Nine mutants (`StretchRect` leaving for the resolve arm ahead of the gates, the core depth arm's sample count,
  the fill's rect, a fill colour encoded under sRGB write, the texture's owed uploads, the autogen chain's
  regeneration, the texture arm's and the back buffer's `UpdateSurface` origin, exit 2 below the CPU-convert
  return) each turn exactly their rows red, at the texel or answer written before the run. The whole-branch
  review: nothing Critical or Important, three Minors ledgered without a fix round. No row covers the core arms'
  error paths, a multisampled back buffer's fill, a whole fill of an additional chain's back buffer or
  `UpdateSurface` into it, a converted `UpdateSurface` into the back buffer, target formats other than A8R8G8B8
  and X8R8G8B8, or a whole fill under sRGB write; the texture arm's GPU claim has no mutant (no CPU read shows
  its absence). No probe: no draw file changed, and the twelve `clear` tests pass unchanged over the shared emit
  bodies.
  C4c-4b done 2026-11-18 at DXMTRS main 30080983, which MacNeutron pins: `StretchRect`'s unscaled copies on the
  core runtime. d3d9-rt gains `copy` (one texture-to-texture blit from any stored colour level, a target or
  not, or the back buffer, into a target; refused, with nothing recorded, for an end that does not resolve, a
  multisampled end, unequal stored formats, a rect or origin that leaves its level, and one texture at both ends
  unless mtld3d's own `same_surface_route` says `Direct`) and `copy_depth` (a whole depth-stencil level into
  another of the same size, format and sample count); the wire record is built by the pure `blit_record`, and
  the copy's two `access_texture` calls are what orders it among the passes, since texture allocations are
  untracked. In the front end the two exits of `StretchRect` reach `core_arms::stretch_depth` and
  `stretch_colour`. A colour copy first tries the multisample resolve (4a's, unchanged), then is refused by name
  when it scales, has a multisampled end, or converts into a target; then **the destination itself decides**: a
  render target takes the GPU route (the source's and the destination's owed uploads, the blit, and on success
  the destination's GPU claim and an autogen chain's regeneration; a level that is no target and a plain surface
  are sources through their core texture, made and uploaded at first use); a plain surface takes the CPU route
  from another plain surface only (a raw staging copy when the two stagings hold the same bytes, mtld3d's
  `convert_stretch_dst_staging` otherwise, a YUV source included), so **the invariant holds: on a core device
  only render-target levels, standalone targets and the back buffer are written by the GPU**, on an extended
  device too. One texture at both ends follows mtld3d's rule: identical rects are `D3D_OK` with nothing
  recorded, disjoint rects or two subresources are one blit, overlapping rects are refused by name. A depth
  pair of equal sample counts is one blit, multisampled included, and one surface at both ends is `D3D_OK`. Every
  refusal by name goes through one function (the log is once a string pointer), and a copy the core does not
  record logs "was not recorded" and answers `D3DERR_INVALIDCALL`; the gates count those lines. Answers that
  differ from mtld3d, each `D3DERR_NOTAVAILABLE` by name: a scaled copy, a converting copy into a target (a YUV
  decode included), a multisampled source's sub-rect or a multisampled destination, the depth resolve,
  overlapping rects of one subresource, a plain surface onto itself, an extended device's whole-surface copy
  into a non-target. Where mtld3d blits a plain pair and claims the destination for the GPU, the core copies
  the staging; a block-compressed plain pair with a rect off the block grid rounds out to whole blocks (not
  judged; the grid check belongs to the gates of both runtimes).
  Gated at that tip with thirteen new rows of the `blit` launch (31 in all) and one of `msaa` (20): the plain
  build's translator key and `d3d9.dll` imports are the base's; d3d9-check default, msl and core, every row;
  the Mac's conformance fails anew at triaged sites only (at its second run: the first had one more,
  device.c:1945 in `test_cursor`, the host's cursor state, not reproduced); stateblock on both runtimes; the
  eleven launches under core with Metal's validation layer report nothing; the VM's D3D9 gate on both runtimes
  and both WoW64 forms, in 64-bit and in 32-bit; no "was not recorded" line in any lane. Before any front-end
  code, the `blit` and `msaa` launches ran on mtld3d under the validation layer, silent and with their pixels
  judged: that settled that Metal copies between disjoint rects of one subresource and between two multisampled
  depth textures of one count. Ten mutants (the copy's destination origin, its source origin, the level of a
  source that is no target, the source's owed uploads at first use and afterwards, the autogen chain's
  regeneration, the plain copy's destination point, the depth copy's direction, the copy's tracked source read
  and its tracked destination subresource) turn red the rows named before the run (two of them at another
  texel than the one named); the last reddens two rows more than predicted (a Clear of the destination's level or face is then not
  ordered before the blit), which hides nothing. The whole-branch review: nothing Critical or Important, three
  Minors ledgered without a fix round. No row covers the CPU route's dirty mark on a destination that already
  has a core texture (a cell for 4c), the claim after a copy into a level, a cube face of a texture that is no
  target as a source, or an extended device's paths. The x64 check did not run in this slice: the range is
  inside `src/d3d9`, and the maintainer set it at the end of C4c-4 (2026-11-18). No probe: no draw file
  changed.
  C4c-4c done 2026-11-19 at DXMTRS main 8bde3b14, which MacNeutron pins: `StretchRect`'s scaled, converting,
  YUV, multisampled and overlapping copies on the core runtime. d3d9-rt gains `stretch`, **a render pass of its
  own** that samples one subresource of any stored colour texture onto a rect of a target, multisampled or
  not, with mtld3d's own quad shader: the text and its two entry names now live in the shared crate
  (`mtld3d_core::stretch_rect::BLIT_MSL`), byte for byte, and each runtime compiles it (the core once a device,
  through `MTLDevice_newLibraryWithSourceOptions` with mtld3d's options; a spike had shown the call working from
  the recorder in every lane). The pass records its own rasterizer state, viewport, scissor, pipeline (by the
  destination's stored format and sample count), sampler (the device's cache) and uniforms (a pure function,
  the rect over the source level's own extent), **because the core merges it with a draw's pass on the same
  target and Metal would keep that draw's cull and fill**; the source is a one-level, one-slice view in the
  format the front end names, read through the tracker; the destination is written through its stored format
  and noted so. `copy` and `stretch` read a multisampled back buffer through its presented texture, resolved
  first. `scratch_like` makes a private single-sample target like another, with its sRGB twin when it has one
  (a resolve goes through the view the source was last written in). In the front end `stretch_colour` now, for
  a destination that is a target: answers one texture's identical rects first; flushes both parents; settles
  the source (a planar YUV plain surface is decoded on the CPU into a scratch texture; a packed one is decoded
  by the shader from its own texels; a standalone multisampled target is resolved into a scratch; **a level of
  a texture that is no target is sampled through its swizzled view, a target, the back buffer and a scratch
  through the stored format**, as mtld3d's quad binds them, with `blit_decode`'s alpha rule); copies a
  single-sample source out to a scratch when it is still the destination's texture and the copy is not an
  unscaled disjoint one; then blits (4b) when the copy is unscaled, of one storage and into a single-sample
  target, and samples otherwise. A plain surface copied onto itself is a CPU copy through a buffer. Refused by
  name after it: the depth-stencil resolve (C4c-4d), an extended device's whole-surface copy into a non-target,
  a plain pair no CPU route serves, a standalone multisampled source of a format this GPU does not resolve.
  Differences from mtld3d, stated: LINEAR on a planar source filters decoded RGB (mtld3d filters luma only);
  a multisampled target copied onto itself at disjoint rects keeps the copy (mtld3d's lands on its resolve
  texture); a scratch is made, zeroed and released at each call that needs one.
  Gated at that tip with ten new rows of the `blit` launch (41 in all): the plain build's translator key and
  `d3d9.dll` imports are the base's; d3d9-check default, msl and core, every row; the Mac's conformance fails
  anew at triaged sites only; stateblock on both runtimes; the eleven launches under core with Metal's
  validation layer report nothing; the VM's D3D9 gate on both runtimes and both WoW64 forms, in 64-bit and in
  32-bit (the shader compiles and draws in the guest); no "was not recorded" line in any lane. Before any
  front-end code the `blit`, `targets` and `msaa` launches ran on mtld3d under the validation layer, silent and
  with their pixels judged, the swizzled-view cell among them. Eight mutants (the source rect moved to the
  origin, LINEAR for every filter, the multisampled source's resolve skipped, the overlap's scratch skipped,
  the quad's source read untracked, the plain self-copy's destination point, the view without its swizzle, the
  two packed YUV orders swapped) turn red the rows named before the run (one of them at another cell than
  the one named); the untracked read reddens one row more than predicted (a copy from the back buffer then
  shows a colour from before the row's own Clear and draw; not traced further), which hides nothing. The
  whole-branch review: nothing Critical or Important, five Minors ledgered without a fix round
  (among them: d3d9-rt now depends on mtld3d-types outside its tests; a refused pipeline logs no Metal text;
  two Metal constants restated in `stretch.rs`). No row covers a sampled copy from or into a multisampled back
  buffer (the sweep's functions reach the second), a YUV source's sub-rect, a swizzled view of a level above 0
  or of a cube face, or an extended device's paths. The x64 check did not run in this slice (the range is
  inside `src/d3d9`); it runs at the end of C4c-4d. No probe: no draw file changed.
  C4c-4d done 2026-11-19 at DXMTRS main 7d48da31, which MacNeutron pins: `StretchRect`'s depth-stencil resolve
  on the core runtime, which closes C4c-4. d3d9-rt gains `depth_resolve`, **a render pass of its own with no
  colour target** on the destination's level: one covering triangle whose fragment function writes, for each
  texel, sample 0 of the source's depth (`[[depth(any)]]`) and, for a format with that plane, of its stencil
  (`[[stencil]]`, under a state that always passes and replaces), as mtld3d's compute resolve keeps them; both
  planes load as don't-care (the gates allow whole surfaces only) and are stored. A depth-only source (D16,
  D24X8) is read itself; **a source with stencil is first copied (4b's `copy_depth`) into a scratch
  multisampled depth texture made with views in another format**, because Metal gives the `X32_Stencil8` view
  only to a texture with that usage and the core's depth textures have none; the scratch is forgotten once
  its copy is recorded. The functions are compiled once a device from a text of d3d9-rt's own (mtld3d has no
  fragment resolve to share), through 4c's compile helper made to take a text and its entry names; the quad's
  and the resolve's state records share one helper. The pass never merges with a draw's (no colour against
  one or more), and a Clear of the destination recorded before it folds into it and is dropped. In the front
  end `stretch_depth` sends the one pair of unequal sample counts the gates let through, more samples into
  one, to it; a resolve the core does not record logs "was not recorded" and answers `D3DERR_INVALIDCALL`,
  where mtld3d answers `D3D_OK` with a warning. **No `StretchRect` of a depth-stencil pair is refused by name
  any more**; the refusal's remaining callers are in the colour arm (an extended device's whole-surface copy
  into a non-target, a plain pair no CPU route serves, a multisampled colour source of a format this GPU does
  not resolve).
  Gated at that tip with one row replaced in the `blit` launch (`sd-resolve`, 41 rows) and one new in `msaa`
  (`ms-dres`, 21): the plain build's translator key and `d3d9.dll` imports are the base's; d3d9-check default,
  msl and core, every row, and no launch meets a refusal of `StretchRect` any more; the Mac's conformance
  fails anew at triaged sites only; stateblock on both runtimes; the eleven launches under core with Metal's
  validation layer report nothing (the first compile of the resolve's text, its colourless pass and the
  scratch's multisampled stencil view among them); the VM's D3D9 gate on both runtimes and both WoW64 forms,
  in 64-bit and in 32-bit; no "was not recorded" line in any lane. Before any gate of the code the `blit`,
  `targets` and `msaa` launches ran on mtld3d under the validation layer: its own compute resolve, judged by
  pixels for the first time, is silent and green. Four mutants (the stencil written as 0, the scratch read
  without its copy, the depth-only source read untracked, the depth-only function returning 1.0) turn red the
  cells named before the run, each at the texel and colour named. The whole-branch review: nothing Critical
  or Important, nine Minors ledgered. **One file of the range is outside the plan**: the deadline of two
  mtld3d-unix startup-failure tests goes from 5 s to 30 s (test code only), after one of them failed five
  runs of the Rust gate in a row on an unchanged crate while passing when its binary ran directly; with 30 s
  the test passes and its binary ends in about 6 s, so the cancel it waits for came late, not never; what
  slowed those runs is not
  traced, and a teardown regression of 5 to 30 s would now pass that test. No row tells sample 0 from another
  reduction, and the rows' surfaces are square.
  **The x64 check, once for C4c-4** (maintainer, 2026-11-18), at that tip on the plain build: 420 rows ok, 0
  failed, at its second run. The first, unattended at 03:40, stopped completing rows at 03:54 and resumed at
  07:21 when the maintainer came back to a window saying the Mac had run out of memory (30 rows failed, all
  inside that stall): spoiled by the machine, not judged. No probe: no draw file changed.
  C4c-5 done 2026-11-19 at DXMTRS main 501d43d1, which MacNeutron pins: queries on the core runtime.
  `CreateQuery` answers as on mtld3d (EVENT and OCCLUSION; every other type `D3DERR_NOTAVAILABLE`). d3d9-rt
  gains `event_stamp` and `event_done` (**an EVENT on the queue's own clocks with mtld3d's two rules**: done
  once the chunk its END was recorded in has retired, which the finish thread signals only after that chunk's
  command buffer completed; a poll commits the recording chunk when the END is still in it, flag or not) and
  the OCCLUSION bracket (`Occlusion`, `occlusion_begin`, `occlusion_end`, `occlusion_poll`,
  `occlusion_release`) over dxmt-core's `VisibilityQuery` as it is: **a fresh one a BEGIN** (an open bracket
  is ended first: the encoder's active count is a plain counter, and a chunk still to be read back may hold
  the old one), END first an empty span that answers 0, END twice keeping the result, a poll with the flag
  waiting for the END's chunk. **dxmt-core reads a chunk back only when its passes wrote results, so a span
  whose END sits alone in a chunk is never issued; d3d9-rt settles it** (an empty range issued once that chunk
  retired: nothing added, the chunk marked) instead of changing the shared core. The draw closure bumps the
  visibility offset before each of its two draw records, and **from a device's first OCCLUSION BEGIN on, a
  draw pass and the `StretchRect` quad's pass start with counting off**: the core's optimizer puts an earlier
  pass's records in front of a later pass's on the same targets, and a pass with no query active records no
  mode, so behind a counting pass it would count. The device counts its open brackets and logs "was not
  recorded" if one is left at its end, which is how the gates hold the front end's Release arm. In the front
  end the query's `Occlusion` sits in a `RefCell`; `Issue`, `GetData` and the final Release take a core arm
  ahead of every mtld3d recorder call; the answers and the bytes written are mtld3d's, the two config
  opt-ins included. Differences from mtld3d, stated: the core counts only an application's draws (mtld3d
  also counts a scaled `StretchRect` into the bound target right after a counted draw: its finding); END
  first answers 0 at once; no slot cap; no render scale.
  Gated at that tip with a twelfth launch of twelve rows (`queries`): the plain build's translator key and
  `d3d9.dll` imports are the base's; d3d9-check default, msl and core, every row; the Mac's conformance fails
  anew at triaged sites only; stateblock on both runtimes; the twelve launches under core with Metal's
  validation layer report nothing (Metal takes the disabled visibility mode in a pass without a result
  buffer); the VM's D3D9 gate on both runtimes and both WoW64 forms, in 64-bit and in 32-bit (the visibility
  read-back works in the guest); no "was not recorded" line in any lane. Before any gate of the code the
  rows ran on mtld3d under the validation layer, all green: their expectations are mtld3d's answers. Five
  mutants (no bump in the draw closure, the settle removed, an EVENT's poll that never commits, no pass
  starting with counting off, the front end's Release arm removed) turn red what was named before the run,
  each at its cell and value; the last reddens no row and shows as one "was not recorded" line, which is
  the gate. The whole-branch review: nothing Critical or Important, six Minors ledgered. Held by no row or
  test, stated: an EVENT that answers done early; the two config opt-ins under core; which mode the record
  carries at the `StretchRect` quad (no row may put a `StretchRect` right after a counted draw: mtld3d
  counts it); and the Release arm's gate lives in the slice's job scripts, not yet in `d3d9-check.sh`. The
  probe (`build/c4c/s5/probe.md`, a Mac in use): no cost of the bump is measurable. The x64 check did not run
  in this slice (the range is inside `src/d3d9` and one doc); it runs at the end of C4c-7.
  C4c-6a done 2026-11-19 at DXMTRS main d8ac1c1b, which MacNeutron pins: the getters, the obsolete calls and
  every form of `Present` on the core runtime, **with no new mechanism**. Thirty-four refusal sites go (and
  the present arm's own refusal of rects): thirty-one where mtld3d's own body touches no recorder, the
  eviction's, which takes an arm, and the two other present entries' (the creation parameters, the display
  modes of the device and
  of the swap chain, the texture-memory figure, the raster status, the palette, clip-status, N-patch,
  software-vertex-processing and patch calls, `ValidateDevice`, the extended device's present counters,
  residency, compose and priority calls): the body that runs on a core device is the one mtld3d always ran.
  **One core branch at the top of the device's present** replaces the arm `Present` had: `Present` with a
  source rect, a destination rect, a destination window or a dirty region, `PresentEx` and the swap chain's
  `Present` now run mtld3d's own entry code down to it, so they present the whole back buffer into the
  device's window with mtld3d's warnings for what is ignored, as mtld3d does (and D3D11's `Present1`);
  an additional swap chain presents the device's back buffer. `EvictManagedResources` runs mtld3d's marking
  loop and destroys nothing. Differences from mtld3d, stated: a present skips mtld3d's frame capture and
  cursor work (C4d's); the eviction keeps the GPU copies, and its marks find nothing to mark on a core device
  today; `GetAvailableTextureMem` does not count standalone render-target and depth-stencil surfaces;
  `ValidateDevice` answers one pass for a state whose draw the core refuses. Still refused: `Reset` and what
  tests for it, fullscreen devices, `ProcessVertices`, the multisampled back buffer's writable lock and
  `GetDC`, and C4c-7's calls.
  Gated at that tip with four new rows in the `state` launch (16): the plain build's translator key and
  `d3d9.dll` imports are the base's; d3d9-check default, msl and core, every row; the Mac's conformance fails
  anew at triaged sites only; stateblock on both runtimes; the twelve launches under core with Metal's
  validation layer report nothing; the VM's D3D9 gate on both runtimes and both WoW64 forms, in 64-bit and
  in 32-bit. **That a present happened is judged by an occlusion query polled without the flush flag, which
  only the form under test can complete**: reading the front buffer reads the back buffer on both runtimes
  and cannot tell. Before any gate of the code the rows ran on mtld3d under the validation layer, all green.
  Three mutants (the swap chain's `Present`, `PresentEx`, and a `Present` with a source rect each returning
  without presenting on a core device) turn red the cell named before the run. Both gate scripts now list
  "was not recorded" among the lines a core launch must not hold, which only the slices' job scripts counted
  before; **no run has shown that addition red yet** (C4c-6c's mutants take C4c-5's open-bracket mutant for
  it). The whole-branch review: nothing Critical or Important, four Minors ledgered. Held by no row or
  test, stated: that a present reached a drawable (a cell proves a commit); one present a call; a rect not
  stretched; the eviction's arm. No probe (no draw file changed); no x64 check (inside `src/d3d9` and one
  doc).
  C4c-6c done 2026-11-19 at DXMTRS main 4166a54a, which MacNeutron pins: `Reset`, `ResetEx`,
  `TestCooperativeLevel` and `CheckDeviceState` on the core runtime, for a windowed device. The core's
  branches sit **inside mtld3d's own `reset_impl` and its helpers**, beside the recorder calls they replace,
  so the validation, the default-pool rule, the defaults and the needs-reset state are mtld3d's code. d3d9-rt
  gains `resize` (**the back buffer and its multisampled companion re-made in place after a drain, the Metal
  view and the presenter kept**: by a reading of the window driver, never run, a view made again on one
  window leaks a client surface each time on the Mac, which the plan's first design, a re-attach, would do),
  `retarget` (a new view and presenter
  only when the device window changes; a failure is a warning and the `Reset` still `D3D_OK`, as on mtld3d)
  and `reset_targets`. The automatic depth-stencil is re-made under a fresh id, kept or dropped;
  `reset_targets_to_implicit` regenerates a bound autogen target's mipmaps and puts the core's targets back
  before the teardown (its recorder call was reachable on a core device and would have panicked). On an
  extended device the surfaces an application holds across `ResetEx` are detached as on mtld3d: a held
  depth-stencil keeps its old texture, **a held back buffer becomes inert** (it keeps its description and
  answers `D3DERR_INVALIDCALL` for every use), where mtld3d keeps its pixels. Refused by name after mtld3d's
  validation, without marking the device as needing a reset: a `Reset` into fullscreen and one to a
  back-buffer format the core does not make. The core has no device-lost answer from the GPU (dxmt-core
  reports no command-buffer failure): `TestCooperativeLevel` answers only for a `Reset` that was refused.
  Gated at that tip with a thirteenth launch of fourteen rows (`reset`, each row on a device and a window of
  its own): the plain build's translator key and `d3d9.dll` imports are the base's; d3d9-check default, msl
  and core, every row; the Mac's conformance fails anew at triaged sites only; stateblock on both runtimes;
  the thirteen launches under core with Metal's validation layer report nothing; the VM's D3D9 gate on both
  runtimes and both WoW64 forms, in 64-bit and in 32-bit. Before any gate of the code the rows ran on mtld3d
  under the validation layer, all green. Five mutants turn red what was named before the run (the old back
  buffer kept; the depth-stencil not re-made; a refused `Reset` that does not mark the device; the targets
  not put back; and C4c-5's open-bracket mutant, **which shows the gate scripts' "was not recorded" line red
  for the first time**), with one miss: the depth-stencil mutant leaves `rs-msaa` green, because no row sees
  the automatic depth-stencil's sample count. The whole-branch review: nothing Critical or Important, six
  Minors ledgered; **which window a re-targeted device presents into is held by that review alone** (no row
  or host test can tell). Stated gaps: that window; the depth-stencil's sample count; a return to a window
  the device left makes a second view there; a lock held across `ResetEx`. No probe (no draw file changed);
  no x64 check (inside `src/d3d9` and one doc).
  C4c-7a done 2026-11-19 at DXMTRS main f5604fa7, which MacNeutron pins: the gamma ramp, the frame latency,
  the extended device's `CreateRenderTargetEx`, `CreateOffscreenPlainSurfaceEx` and
  `CreateDepthStencilSurfaceEx`, `SetRenderState(D3DRS_POINTSIZE, RESZ)` and the three cursor calls on the
  core runtime (the roadmap's 7c folded in: the cursor's bodies need no window work for their answers). Ten
  refusals go and **mtld3d's own bodies run on a core device** (traced to their leaves before and after the
  code: none reaches the recorder); the latency bound is enforced through dxmt-core's queue, at the value
  mtld3d would enforce, from the device's creation (a configured `present.maxFrameLatency` holds on core
  too), with no new item in d3d9-rt. RESZ is one front-end arm over C4c-4b's depth copy and 4d's resolve:
  the bound depth-stencil into level 0 of the depth texture at stage 0, **named by the texture, made with
  `D3DUSAGE_DEPTHSTENCIL` or not**, at the call, the state stored by mtld3d's code after it. What the core
  cannot pair is skipped with one warning and `D3D_OK`, as mtld3d's skips answer. Refused by name, after
  the source check and before anything is made, stored or recorded: RESZ into a `D3DUSAGE_DYNAMIC` depth
  texture. Differences from mtld3d, each in STATUS.md: that refusal; a multisampled source of another pixel
  format than the destination's is skipped where mtld3d's transfer converts (D24X8, D16 or D32 into INTZ,
  D24S8 or D24FS8 into DF24 or DF16); the latency bound counts the GPU's retirement where mtld3d also waits
  for the display's pace; **without the window subclass, while the Direct3D cursor shows, the window's next
  `WM_SETCURSOR` reaches the game's window procedure and its class cursor replaces it** (a row pins it;
  slice 6d brings the subclass); the cursor's scale is 1 and `cursor.software` is not followed.
  Gated at that tip's code (53422d08; f5604fa7 adds a correction of two STATUS.md sentences and passed the
  Rust gate) with a fourteenth launch of fifteen rows (`extras`): before any code, the launch on mtld3d
  under the validation layer and in the VM, **one cell red everywhere: with a colour texture of the
  source's size at stage 0, mtld3d's RESZ leaves that texture zeroed** (its copy is refused for the formats
  after it was queued; not traced further; the row's texture moved to another size, where mtld3d skips
  first), and on the core the fourteen rows expected red, the one expected green (`rz-refuse`: the blanket
  refusal gave its answer already). At the tip: the plain build's translator key and `d3d9.dll` imports are
  the base's; d3d9-check default, msl and core, every row; the Mac's conformance fails anew at triaged sites
  only; stateblock on both runtimes; the fourteen launches under core with Metal's validation layer report
  nothing; the VM's D3D9 gate on both runtimes and both WoW64 forms, in 64-bit and in 32-bit. Four mutants
  turn red exactly what was named before their builds and nothing else (RESZ that always copies; a
  destination taken only with `D3DUSAGE_DEPTHSTENCIL`; the refusal answered after the store; the arm
  returning before the store). The whole-branch review: push as is, five Minors (two STATUS.md sentences,
  corrected; three row cells that cannot fail on core, stated). Stated gaps: no row or mutant sees the
  latency's enforcement or `SetCursorPosition`; gamma has rows only (no Wine test calls it); a
  stencil-format multisampled RESZ makes a scratch texture at every call; if mtld3d's queued-then-refused
  copy also zeroes a depth destination, a one-sample pair of two formats leaves different contents on the
  two runtimes (one mtld3d control owed at 7d). No probe (no draw file changed); no x64 check (inside
  `src/d3d9` and one doc).
  C4c's slices: 1 buffers and the draws that read them (done); 2a crossing attributes (`CrossingFetch`) and odd stream
  offsets (done); 2b dynamic buffers mapped directly, the WRITEONLY backing's release, the buffer count and its cap
  (done); 3a colour read-back through locks (lockable targets, the back buffer read-only, render-target texture
  levels, the front buffer; done); 3b device contexts, the surface's `PreLoad`, the writable back-buffer lock, an
  additional swap chain's back buffer, the DYNAMIC depth texture's lock (done);
  4a `ColorFill`, `UpdateSurface` and `StretchRect`'s answers (done); 4b `StretchRect`'s unscaled copies: into a
  target, between plain surfaces on the CPU, the depth-stencil copy (done); 4c its scaled, converting and YUV
  copies into a target, multisampled ends, overlapping rects of one surface (done); 4d the depth-stencil
  resolve (done);
  5 queries (done); 6a the getters, the obsolete calls and the presents (done); 6c `Reset`, windowed (done);
  7a gamma, frame latency, the `Ex` creators, RESZ and the cursor calls (done); 6d fullscreen devices and the
  window subclass
  (inside C4c: the maintainer's decision of 2026-11-19); 7b the device's information and pages from winemetal
  (moved to C4d with the logger's move: the maintainer's decision of 2026-11-20); 7d `ProcessVertices`, the silent
  differences, the gamma ramp applied on a fullscreen device, and the conformance lane for `visual`, `device` and
  `d3d9ex` under core, which is C4c's gate.
  The sweep after 3a (`build/c4c/s3/sweep.md`, gating nothing): `visual` and `device` run to their end
  under core (2431 failed of 211208; 1871 of 56686, about 100000 checks not executed), `d3d9ex` dies in
  `test_reset`; the new failures are refusals by name (`StretchRect`'s other shapes about 2770 of 3356) but two
  silent ones, not traced yet and owed where their code is next touched: a volume texture bound where the pixel
  function samples a 2D one (Metal's validation reports it; undefined without the layer), and
  `D3DSAMP_SRGBTEXTURE` for values other than 0 and 1 (converted under core, not on mtld3d).
  The sweep after 3b (`build/c4c/s3b/sweep.md`, gating nothing): `test_getdc` passes whole under core (231 new
  failures to 0), `test_lockable_backbuffer` keeps the three that are `Reset`'s, and no surface or texture refusal
  is met any more (Wine's own counts as above: `visual` 2426 failed of 211208, `device` 1635 of 56830; `d3d9ex`
  still dies in `test_reset`, so `test_user_memory_getdc` is not reached). `StretchRect`'s other shapes,
  `ColorFill` and `UpdateSurface` are about 2950 of the 3115 new failures left in the two suites. The two silent
  differences above are unchanged.
  The sweep after 4a (`build/c4c/s4a/sweep.md`, gating nothing): `color_fill_test`, `update_surface_test`,
  `test_signed_formats`, `test_cube_wrap`, `test_flip`, `test_generate_mipmap` and `test_mipmap_lock` pass whole
  under core, and `test_stretch_rect` falls from 810 new failures to 81 on validation alone (Wine's own counts:
  `visual` 2162 failed of 211218, `device` 939 of 56830; "Test failed" lines over the default runtime's baseline:
  2183 to 1912 and 932 to 200; `d3d9ex` still dies in `test_reset`). `StretchRect`'s remaining shapes are about
  1900 of the 2112 lines left in the two suites. The two silent differences are unchanged. A third, older than 4a
  (the same five lines are in 3b's sweep, among `add_dirty_rect_test`'s 13), is named here and not traced either:
  that test's five colours after `READONLY` and `NO_DIRTY_UPDATE` writes into a managed texture's levels, which
  D3D9 does not upload and a core draw shows.
  The sweep after 4b (`build/c4c/s4b/sweep.md`, gating nothing): every function moved as predicted before the
  run: `test_pointsize` 570 new failures to 0, `depth_blit_test` 15 to 0, `test_mipmap_autogen` 8 to 0,
  `test_stretch_rect` 81 to 40, `stretchrect_test` 51 to 17, `test_scene` 5 to 1, `test_blit_format_conversion`
  10 to 8 (Wine's own counts: `visual` 1528 failed of 211160, `device` 894 of 56830; "Test failed" lines over
  the default runtime's baseline: 1912 to 1278 and 200 to 155; `d3d9ex` still dies in `test_reset`).
  `StretchRect`'s remaining shapes are about 1215 of the 1433 lines left in the two suites. The three silent
  differences are unchanged.
  The sweep after 4c (`build/c4c/s4c/sweep.md`, gating nothing): every function moved as predicted before the
  run: `yuv_layout_test` 992 new failures to 0, `yuv_color_test` 106 to 0, `test_multisample_stretch_rect` 54
  to 0, `test_stretch_rect` 40 to 0, `stretchrect_test` 17 to 0, `test_filling_convention` 14 to 0,
  `test_blit_format_conversion` 8 to 0, `np2_stretch_rect_test` 5 to 0, `clear_test` 5 to 0,
  `multisampled_depth_buffer_test` 12 to 5 (Wine's own counts: `visual` 320 failed of 211160, `device` 854 of
  56830; "Test failed" lines over the default runtime's baseline: 1278 to 70 and 155 to 115; `d3d9ex` still
  dies in `test_reset`). `StretchRect` is 5 of the 185 lines left in the two suites (the depth-stencil resolve,
  4d). The three silent differences are unchanged.
  The sweep after 4d (`build/c4c/s4d/sweep.md`, gating nothing): `multisampled_depth_buffer_test` 5 new
  failures to 0 as predicted, nothing else moved (Wine's own counts: `visual` 315 failed of 211160, `device`
  854 of 56830; "Test failed" lines over the default runtime's baseline: 70 to 65 and 115; `d3d9ex` still dies
  in `test_reset`). No `StretchRect`, `ColorFill` or `UpdateSurface` line is left in the three suites. The
  three silent differences are unchanged.
  The sweep after 5 (`build/c4c/s5/sweep.md`, gating nothing, run first in the chain): as predicted no failing
  line moved (none was the refusal's), and no suite meets `CreateQuery`'s refusal;
  `test_sample_attached_rendertarget` runs under core for the first time (40 checks more in `visual`, no
  failed line), and **three Metal validation reports are new** ("threadsPerTile (16, 16) must equal tile size
  (32, 32)": the pass barrier of a texture attached and sampled in one pass; attributed to that test's three
  draws by elimination), not this slice's code, not traced, owed at C4c-7's parity. Wine's own counts: `visual`
  315 failed of 211200, `device` 854 of 56832.
  The sweep after 6a (`build/c4c/s6/sweep.md`, gating nothing): every function named before the run cleared
  (`test_get_display_mode`, `test_creation_parameters`, `test_vidmem_accounting`, `test_set_palette`,
  `test_pixel_format`, `test_swapchain_get_displaymode_ex` 9 lines, `test_mvp_software_vertex_shaders`,
  `test_evict_bound_resources`, `ds_size_test`) and none that now runs further added a site; "Test failed"
  lines over the default runtime's baseline: `visual` 65 to 55, `device` 115 to 103, `d3d9ex` 66 to 57. Of the
  215 left, about 138 wait for `Reset`.
  The sweep after 6c (`build/c4c/s6c/sweep.md`, gating nothing): each function as predicted from a reading of
  it to its end, within two lines: `device` 103 to 39 (`test_swapchain_parameters` 62 to 15,
  `test_reset_resources`, `test_lockable_backbuffer`, `test_scissor_size`, `test_scene` and
  `test_swapchain_multisample_reset` to 0), `visual` 55 to 51, and **`d3d9ex` runs to its end for the first
  time under core** (1967 checks, no crash; `test_reset` 54 to 16), so its count rises, as predicted, from 57
  to 138: the functions it never reached, most of them waiting for C4c-7a's `Ex` creators
  (`test_resource_access` 60). What `device` keeps is fullscreen's or the window subclass's; one of its
  lines (`test_cursor_pos`) went by a skip of that run (the host's cursor could not be warped), not by the
  slice. No Metal validation report is new: `visual`'s nine of C4c-5's sweep, none in the two others.
  The sweep after 7a (`build/c4c/s7a/sweep.md`, gating nothing): **every count as predicted**, in each of
  the three units the plan's first review named: lines over the default runtime's baseline `visual` 51 to 20
  (`resz_test` 31 to 0), `device` 39 to 27 (`test_cursor` 12 to 0; `test_cursor_pos` skipped again, the
  host's cursor could not be warped), `d3d9ex` 138 to 62 (`test_resource_access` 60, `test_frame_latency`
  9, `test_format_unknown` 4 and `test_create_depth_stencil_surface_ex` 3 to 0; Wine's own summary, which
  also counts checks that succeed inside todo blocks, goes from 180 to 138: 37 such successes now).
  No Metal validation report is new. Of the 109 lines
  left, 87 wait for fullscreen and the window subclass (slice 6d), 20 for 7d (`ProcessVertices` 5, three
  silent differences 15), 2 are triaged.
- C4 (after step 5): `d3d9.dll` on the core (D3D11's model, PE-side encoding), the bridge and the one presenter; the core
  gains D3D9's needs (fixed-function constants, D3D9 binding, formats and surfaces, lockable pools, queries, present and
  reset, gamma, cursor; draw skipping and pipeline prewarm as per-API core policies); mtld3d's macOS window features
  (cursor overlay, view parking, client-surface present, EDR and occlusion observers) become winemetal slots; behind a
  switch until parity, measured with a 32-bit D3D9 title in the VM's lanes separately.
- C5 (after C4's parity): `mtld3d.dll`, `mtld3d.so`, mtld3d's transport and wire, the `mtl` facade (its wire mirrors
  with it) and C3's interim pieces retire, and with them winemetal's MSL source-library slots for D3D9.
- The C, C++ and Objective-C left in the tree (maintainer, 2026-11-16: port them, like the rest of DXMTRS; the inventory,
  the order and the open questions are in `docs/superpowers/plans/2026-11-16-dxmtrs-c-to-rust.md`). First landing
  2026-11-16 at DXMTRS main 40c76ab1 (40c76ab1def4126fe30b137bbb69e0aac138670a), which MacNeutron pins: the three groups that
  depend on no decision. `src/util/rust_pe_rt.c` (the MSVC runtime symbols Rust's `-msvc` objects ask of an llvm-mingw
  link: `_fltused`, `__chkstk`, i686's `__tls_array`) is the `@pe_rt` arm of `dxmt_dll!`, named once by each PE image's
  root crate; a meson target made of Rust archives alone takes `rust_no_source`, an empty generated C file, for its link
  driver. `src/winemetal/airconv_thunks.c` (the twelve SM50 calls of `winemetal.dll`) is `src/winemetal/src/sm50.rs`,
  through the crate's `exports!` and transport, its prototypes asserted from the existing `sm50-abi.h` seam.
  `src/nativemetal/win32_event.c` and `win32_native.cpp` (the native build's Win32 handles, events, semaphores, threads
  and waits, the `mn_win32_handle` protocol MacNeutron's VM host shares) are `src/winemetal/unix/src/native_win32.rs`,
  the thirteen prototypes now declared in `src/util/win32_native_event.h` and asserted with the handle's layout.
  Behaviour is unchanged: over 37 image records against the C build no export and no import differs (`_fltused` moved
  from `.data` to `.rdata`, and is gone from the i686 `d3d9.dll` and both guest `mtld3d.dll`, whose links never
  referenced it); `winemetal.so` and the five native dylibs are identical records. The translator key moves once,
  `3a0b1ab584cc3be8e762cefbf6ff7cc643d5a37b1212ab85e562409ac6209bd9` to
  `cce99ae0860100e960370acaf0bf6fc07e1efe69bbf89a3057a4612f078cd9be`, with no change of translated output: the key hashes
  the thunks' source text, and `translator_key` (`wine-arm64/lib.sh`) now reads `sm50.rs` where the C file is absent.
  Gates: `tools/rust-check.sh` at each commit (its test leg now runs `winemetal-pe`'s tests, which the first run of the
  thunks' commit did not); the Mac build, the x64 `dxmt/check.sh`, `dxmt/d3d9-check.sh`, the plain `d3d9.dll` import
  list, `wm-probe` on `winemetal.so` and on the native dylib; the native tests with a new 14-case cross test
  (`vm/dxmt-native/tests/win32-cross.sh`, green against the C dylib first); in the VM the D3D10-12 gate (48 programs),
  D3D9's default lanes on old and new WoW64, the dxgi and d3d11 guest probes for i386 and x86_64, and forwarding
  (`present-gate.sh forward`, `d3d-smoke.sh`). The smoke's `present-held-a-vcpu` row failed in the gate run (two presents
  over 4 ms, a first frame of 15.3 ms); run alternately on the native dylib of the base (C) and of the tip (Rust), same
  image and host, it failed 2 of 3 on the base and 1 of 3 on the tip: the open row of C3c, not this change.
  What stays C-family at this pin, each listed with its reason in DXMTRS's `tools/audit.sh` (`C_SOURCES`; a new such
  file under `src/` or `tests/` fails the gate), and each waiting on a decision of the maintainer: the translator's shim
  and its two LLVM passes (LLVM's C API does not reach the pass manager, MemorySSA or a module pass; the options run
  from generic C entry points added to our LLVM fork to keeping the three files); `delegate_forward.m` (`mtld3d.so` is
  `panic = "abort"`, where no Rust frame can catch an Objective-C exception; C4 re-homes its observer into
  `winemetal.so`, which unwinds); `wsi_monitor_win32.cpp` and `wsi_window_win32.cpp` (already Rust for DXMTRS, compiled
  only by MacNeutron's guest proxy, `vm/d3d/build-guest.sh`); the eleven `tests/dx11` sample programs (built by no gate).
  Second landing 2026-11-16 at DXMTRS main cdd64734 (cdd647344ff36e31ec567290e3acb30070f7816c), which MacNeutron pins,
  after the maintainer's answer (all of it in Rust): no C, C++ or Objective-C source is left under `src/` or `tests/`,
  and `tools/audit.sh` fails on one. The translator's shim and its two LLVM passes are Rust. What LLVM's C API covers is
  called through it (the fast-math queries, the `memory` and `captures` attributes, the SDK version flag); what it lacks
  comes from nineteen generic C entry points added to our LLVM fork as one component, `LLVMAIRExtras`
  (`llvm/include/llvm-c/AIRExtras.h`, `llvm/lib/AIRExtras`; Mac-Neutron/llvm-project branch `air-23-extras`,
  9df6993f28043ccc381aa0b43e2b9acf2a6223fc, the commit `tools/llvm.sh` pins), each one LLVM call that holds no policy of
  the translator. The pass pipeline is a text constant LLVM's `PassBuilder` parses, its two passes
  (`air::Lower16BitTexReadPass`, `air::SimdgroupImplicitMemBarrierPass`) Rust callbacks in
  `src/airconv/src/module/passes.rs`. `delegate_forward.m` is a landing pad in assembly inside `delegate_forward.rs`
  (`global_asm!`, arm64 and x86_64: what clang emits for the `@try` it replaces, with the Objective-C personality and
  its exception table), so `mtld3d.so` stays `panic = "abort"` and compiles no Objective-C. The window-system C++ (two
  sources, three headers) left DXMTRS for `vm/d3d/guest/wsi` here, beside the guest proxy that compiles it, with its
  licences. The eleven `tests/dx11` programs, their headers and assets and the meson option `enable_tests` are deleted;
  their nine shaders stay for the corpus.
  Behaviour is unchanged: the 13 shader corpora translate to the same 2,742 files byte for byte, on a non-LTO LLVM
  install and on the shared ThinLTO one, and the DXSO identity digest over 875 programs is equal. One stated difference:
  a threadgroup load whose MemorySSA definition is the function's live-on-entry one is skipped by the barrier pass, where
  the C++ dereferenced a null instruction; a unit test pins it. A mutation showed that the corpora catch a missing
  barrier pass and not a missing 16-bit read pass, so unit tests pin both passes' output and the sixteen forms of the
  `memory` attribute. The translator key moves once, `cce99ae0860100e960370acaf0bf6fc07e1efe69bbf89a3057a4612f078cd9be`
  to `e1074f4f5f966252586e28ef2b935c7a4bc9acb462ef098691730f9091e6e59c`, with no change of translated output (the LLVM
  pin and the source text it hashes). Over the 37 image records against the first landing, `winemetal.so` and the native
  `winemetal.dylib` lose the shim's and the passes' symbols and gain the nineteen `LLVMAIR*`; the three `d3d9.dll`
  differ by C4c-1's code (the base moved); nothing else differs. `mtld3d.so` holds the pad `_mtld3d_try_send` with its
  exception table, imports the Objective-C personality and `objc_msgSend`, and has no `__objc_imageinfo` section.
  Gates: `tools/rust-check.sh` at each of the eight commits; the Mac build with both shared LLVM installs rebuilt at
  the new pin; the x64 `dxmt/check.sh` (420 rows), `dxmt/d3d9-check.sh`, the plain `d3d9.dll` import list, `wm-probe`
  on `winemetal.so` and on the native dylib, the native tests; in the VM the D3D10-12 gate (48 programs), D3D9's default
  lanes on old and new WoW64, the dxgi and d3d11 guest probes for i386 and x86_64, and forwarding. Two rows failed in
  the gate run: `wm-probe` g3's `resolve-no-pool heap-growth` on the native dylib (the load-sensitive row of C3c's and
  C3d's ledgers; three reruns passed), and the smoke's `present-held-a-vcpu`, which failed 2 of 4 runs at this tip, each
  with the open row's signature (two presents over 4 ms: frame 1 at 15 to 18 ms, frame 4 at 5 to 9 ms), the rate the
  first landing measured on its tip and on its base. No base run was taken at this landing.
  Not run: `wine-arm64/tests/licences_test.sh` (it needs a bundle) and `vm/tests/vm-setup.sh`, both owed at the next
  release. The landing pad's x86_64 block is proven by a scratch harness under Rosetta only: no x86_64 Rust target is
  installed here.

Each step is one or more commits, each with its checks green (§8). Pace (maintainer, 2026-10-30): a step's map and plan
may be written ahead, while earlier steps run, and are rebased onto what has landed before the step starts; steps 4
and 5 run at the same time in their own branches (their code meets only at `airconv_public.h`; the workspace's and the
tools' lists merge by hand, as at 5a's rebase), as may step 3 beside step R where their files are disjoint. Builds and
gates stay serialized on the one Mac: every command that builds or installs MacNeutron's runtime, or checks out a
branch in the build's DXMTRS checkout, holds one pipeline lock, and all editing happens in worktrees. Small steps
(3, 6) get one adversarial plan reviewer, large ones (4, 5, §7's phases) three.

## 6. What outside code relies on

- **The VM forwarder** (MacNeutron `eac-vm/d3d`) compiles against DXMT's private headers where they sit in `src/`:
  `src/d3d12/d3d12_interfaces.hpp` and `src/d3d11/d3d11_interfaces.hpp` (private interfaces such as
  `IMTLD3D11ContextExt2`, in `host/stubs.h`; both include `src/util/com/com_guid.hpp`),
  `src/d3d12/d3d12_barrier_legacy.hpp` (enhanced barriers as legacy ones) and `d3d12_shared_fence.hpp` (a fence's
  shared handles), both in `guest/proxy.cpp`, and `src/util/win32_native_event.h` (in `host/events.c` and
  `guest/unix.c`; `win32_native.h` comes with the fence header). The guest's `mnd3d.dll` also builds DXMT's Win32
  window code: `src/util/wsi_window.hpp`, `wsi_monitor.hpp`, `wsi_platform_win32.hpp` and `util_d3dkmt.h`, and copies
  of `wsi_window_win32.cpp` and `wsi_monitor_win32.cpp`. DXMTRS keeps publishing headers that describe the same
  interfaces, layouts and conversion, at these paths, not under `include/` as first planned (5-D7,
  `build/step5/map/7-decisions.md`); `d3d12_common.hpp`, once listed here, is MacNeutron's (`dxmt/tests`). Natively,
  `winemetal.dylib` exports `MTLD3DNotImplemented` (set by `wmt_native_not_implemented` in Rust front ends), which
  `eac-vm/d3d/gen.py`, `eac-vm/d3d/host/native.cpp` and `eac-vm/d3d/host/dispatch.c` check beside `catch (...)` so
  unimplemented COM slots report through `mn_d3d_not_in_dxmt` (5-D9, §5 step 5c). A port step
  that touches one runs the forwarder's build and loopback gate, and one that replaces one (the window code included)
  changes the forwarder in the same pin bump.
- **MacNeutron** relies on the DLL and Unix-library names, the environment variables (`DXMT_*`, `MACNEUTRON_*`,
  `MTLD3D_*`), the log and statistics formats its tools read, and the shader cache's location. Names and variables are
  kept; a format change is made in both repositories in the same pin bump. D3D9's outside names are kept too:
  `MTLD3D_CONFIG`, `mtld3d.conf` beside the game, `log.dir` and `~/Library/Logs/MacNeutron/mtld3d`, wine.app's
  `licenses/mtld3d`, and `mtld3d_shaders.bin` until C3.
- **The translator's C interface** (`airconv_public.h`, `AIRCONV_VERSION`) is internal to DXMTRS, but its cache key is
  what players' shader caches are stored under: it changes only at the LLVM move or a deliberate translator change.
  At step 4b (`macneutron translator 3`, §5 step 4b P9), the key hashes `tools/llvm.sh` in the DXMTRS tree (replacing
  `LLVM_TAG` and `dxmt/llvm.sh`), `src/airconv` with `tests.rs` and `tests/` excluded, `src/winemetal/airconv_thunks.[ch]`,
  `src/d3d11/d3d11_shader.cpp` when present else `src/d3d11/src/shader/args.rs` (step 5b's P3 rule, dying if neither
  exists), the root `Cargo.toml` and `.cargo/config.toml` sections (refusing `[profile.*.package.*]` overrides for
  `airconv`, `airconv-ir` or `"*"` and `[target.'cfg(...)]` sections), `rustc -vV`, `cargo tree -p airconv`,
  `xcrun-metal.sh`, and the C++ and Metal compilers' versions, taking and checking every command's output before
  hashing. D3D11's switch, which deletes `src/d3d11/d3d11_shader.cpp`, moves the key once more when it lands (§5 step
  4b P16, step 5 5-D6).

## 7. One project: D3D9 on the shared pieces

Full convergence (maintainer, 2026-10-30): D3D9 ends on the same Rust core, bridge, translator and presenter as
D3D10-12, reached through §5's step R and phases C2-C5, and finished when `mtld3d.dll` and `mtld3d.so` are gone. Step 2
kept winemetal on objc2 and its own slots (D1, D2); the merge goes the other way: D3D9 moves onto winemetal.
- **One layout and build** (step R): `src/d3d9` like every component, built by DXMTRS's meson, one workspace, one gate.
- **Shared crates**: Metal access through winemetal; configuration and environment (one reader for both sample files;
  `MTLD3D_CONFIG`, `mtld3d.conf` and `log.dir` stay as aliases, §6); logging and crash reports for every API;
  statistics (DXMT_STATS's `stats.txt`; mtld3d's performance lines map into it); shader caching and pre-caching (AIR in
  DXMT's cache, keyed by the translator key; mtld3d's pipeline prewarm a core policy).
- **One translator**: D3D9's shaders through the Rust translator via an SM1-3 front end (C3).
- **One core**: `d3d9.dll` records into the core's command chunks as D3D11 does and encodes on the PE side (C4). If a
  32-bit D3D9 title measures worse that way in the VM, the result goes back to the maintainer before C5.
- **One bridge**: `d3d9.dll` calls `winemetal.dll` like the other front ends; mtld3d's macOS window features become
  winemetal Unix slots on the Mac; `mtld3d.dll`, `mtld3d.so`, mtld3d's transport and the `mtl` facade retire (C5).
- **One presenter** for D3D9 to D3D12: MetalFX upscaling, HDR, gamma, frame pacing, frame generation and the cursor.
  D3D9's defaults (inverse tone mapping on EDR screens, `present.maxFps`) stay as presenter options.
- **What stays per API**: each front end crate (COM objects, state and its defaults; for D3D9 state blocks, fixed-function
  state, D3D9 formats and surfaces and its Win32 hooks). D3D9 is not forwarded in the VM (its own decision). `d3d9.dll`
  stays a Wine builtin in wine.app (MacNeutron ships no other d3d9) and ARM64X on the Mac.
- **COM definitions** follow mtld3d: hand-rolled `#[repr(C)]` vtables, no `windows` crate (the native macOS and Linux
  builds need them too), with size and offset assertions per target. D3D10-12's are written by hand from the headers the
  C++ builds against, or generated with `bindgen` from their C view (`include/native`, `CINTERFACE`) where that saves
  work; bindgen is a development tool.

## 8. Gates

At every step:
- **Functional**: `dxmt/check.sh` in both lanes (its `d3d9-check` item included), the wine-arm64 checks, D3D9's
  conformance suite (MacNeutron `dxmt/mtld3d-conformance.sh`, against `dxmt/mtld3d-triaged.txt`), the facade test and
  the loopback `d3d9-check` while the facade lives, and the VM gates (the DXMT gate, d3d-smoke, `d3d9-gate` in both
  lanes, present-gate, the forwarder's loopback gate), with `MACNEUTRON_FRAMEGEN` modes as today. mtld3d's end-to-end
  programs once they link with llvm-mingw (C3's plan) or xwin is approved.
- **Rust**: `tools/rust-check.sh` over the root workspace: rustfmt (the pinned nightly: `rustfmt.toml`'s options are
  unstable), clippy `pedantic` and `nursery` with warnings denied on `aarch64-apple-darwin` and every `-pc-windows-msvc`
  target a crate ships on (1.99.0 has clippy since 2026-10-30), `cargo doc`, the host unit tests (with `cargo test`),
  the generated bindings regenerated and compared (winemetal's and the seams', since step 5a), and `tools/audit.sh`,
  run under MacNeutron's environment (Wine archives, llvm-mingw, the guest sysroot and, since step 4a, the LLVM of
  MacNeutron's `make llvm-nolto` and its `xcrun-metal.sh`, named by documented variables).
  `docs/CONVENTIONS.md` applies to the whole tree; DXMT is no longer on its list of competing implementations.
- **Licences**: DXMTRS is LGPL-2.1-or-later (a work derived from DXMT); mtld3d's files keep their zlib notice: an
  unaltered `LICENSE` in `src/d3d9/`, `tests/d3d9/` and `docs/d3d9/`, `license = "Zlib"` in every mtld3d crate, and the
  README's list of the zlib files elsewhere; `vendor/snmalloc-sys` carries its MIT text. New crates only where a few
  lines won't do, and only under permissive or dual licences (no Apache-2.0-only crate in an LGPL-2.1 tree), checked from
  `cargo metadata` next to the notices `mtld3d-notices.py` writes, for every target that ships (the VM guest's
  included).
- **Binary identity** (step R and any later move of a build): every shipped binary keeps its name, machine, exports,
  imports (listed differences only), builtin marker and VERSIONINFO.
- **Translator**: identical `llvm-dis` output on the shader corpus until the LLVM move; at the move (step 4b P15),
  `air-oracle.sh check` on every corpus (`self` through `<stem>.post.ll`, `dual` comparing `air15 dis` and LLVM 23
  `llvm-dis` undef/poison/zeroinitializer counts, and `stats` verifying `use-typed=0`, plus `verify`, `inv`, `wrapper`,
  `corpus-diff.py --no-dis` for status, reasons, `refl=` and `tags=`, and Metal building every shader and pipeline on the
  user corpora), DXMT's shader checks rendering and executing as before (`check.sh` in both lanes and R9-1's
  `check-exec.sh` + `step5-cmp.py`), cold Metal build latency on SMITE 2's 500-shader sample within 10% of ref15, and the
  milestone benches (SMITE 2, Palworld, Age of Empires II DE) showing no GPU-time or frame-rate loss beyond 5% (residual
  risk: value-level equality with the C++ is proven only for what the check suite executes and renders). At C3, every
  shader of a DXSO corpus builds its pipelines and D3D9 renders as before.
- **Performance**, once per milestone (end of steps 2, 4 and 5, after C3 and C4, and after §7), each bench run once:
  SMITE 2's lobby, Palworld's title screen and a D3D11 title the maintainer picks, at parity or better; after C3 and C4
  also the D3D9 titles the maintainer picks (a 32-bit one measured separately in the VM). Game benches need the
  maintainer's go-ahead each time; step 2's is parked for the maintainer (§5, D11).

## 9. Scale and plans

About 100k lines of C and C++ to port, over several months, landing as many small steps. This document covers all of
it. The first implementation plan covers §3 (DXMTRS created and pinned) and §4 (Phase 0). Each step of §5, and §7,
gets its own plan, written ahead where §5's pace allows and rebased before it runs.

Outward steps, each needing the maintainer's yes when it comes: creating DXMTRS on GitHub and pushing to it; switching
MacNeutron's pin; fetching SPIRV-Cross or installing bindgen; fetching xwin if mtld3d's end-to-end programs cannot link
with llvm-mingw; the D3D9 game benches. Step R has its own plan (`2026-10-30-dxmtrs-stepR-layout.md`), and each of
C2-C5 its own, after the steps it depends on have landed. Tools installed at the maintainer's word (2026-10-30): clippy
and rustfmt for 1.99.0, a nightly with rustfmt. DXMTRS started from dxmt `main` at 89558c1 (adaptive frame
generation), pushed to both repositories on 2026-10-29.
