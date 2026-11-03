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

**D3D9's convergence (§7)**, in phases interleaved with the steps, each its own plan:
- C2 (with step 5's configuration, logging and statistics, or earlier behind the C++ signatures): one configuration
  reader for `dxmt.conf` and `mtld3d.conf` (`MTLD3D_CONFIG` an alias of `DXMT_CONFIG`), one logger and crash reporter,
  one statistics format (DXMT_STATS's), one perf-build switch for all Rust. D3D9 honours `DXMT_LOG_LEVEL` from the start
  of this phase.
- C3 (after step 4's LLVM move, same release): D3D9's shaders through the translator by an SM1-3 front end (mtld3d's
  DXSO parser, IR and linkage moved in; fixed function through the same builder), first with today's D3D9 runtime
  (the translator already writes `[[stage_in]]` vertex inputs without an input layout, and top-level resource
  arguments), reached without loading MacNeutron's presenter into D3D9 processes. Checked by a corpus of DXSO shaders
  rendered both ways, the conformance suite and the D3D9 gates. The DXSO MSL emitters and `mtld3d_shaders.bin`'s MSL
  records retire at parity (one cold D3D9 cache, expected less first-use stutter).
- C4 (after step 5): `d3d9.dll` on the core (D3D11's model, PE-side encoding), the bridge and the one presenter; the core
  gains D3D9's needs (fixed-function constants, D3D9 binding, formats and surfaces, lockable pools, queries, present and
  reset, gamma, cursor; draw skipping and pipeline prewarm as per-API core policies); mtld3d's macOS window features
  (cursor overlay, view parking, client-surface present, EDR and occlusion observers) become winemetal slots; behind a
  switch until parity, measured with a 32-bit D3D9 title in the VM's lanes separately.
- C5 (after C4's parity): `mtld3d.dll`, `mtld3d.so`, mtld3d's transport and wire, the `mtl` facade (its wire mirrors
  with it) and C3's interim pieces retire, and with them winemetal's MSL source-library slots for D3D9.

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
