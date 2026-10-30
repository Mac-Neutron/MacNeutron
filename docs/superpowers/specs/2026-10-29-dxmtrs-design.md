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
  `Cargo.toml` at the component root (`src/nvngx`, `src/nvapi`, `src/winemetal`, `src/d3d9`; step 3's `src/dxgi` and
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
translator crate's `build.rs`; `llvm-sys` is used with its `no-llvm-linking` feature, the build script linking the LLVM
libraries `llvm.sh` builds.

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
  DLLs build and link but did not run in Phase 0; step 1 ran the guest's x86_64 nvngx, its shims still unexercised
  (§5 step 1). MacNeutron's Mac DLLs turned out ARM64EC alone,
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
  35/64 loaded and built pipelines; typing pointer selects and phis from their result and nulls through their use (the
  committed spike) traded crashes for read errors, 27/64. Either way Metal accepts that record format. The rest fail on what inference can't recover from opaque IR (argument-buffer arguments typed `i8*`, struct
  fields losing their address space) and on what the old format can't say (`reassoc nsz arcp contract` widened to
  `fast`, which adds no-NaN and no-Inf; `afn` dropped; `poison` as `undef`). Spike code: DXMTRS branch `phase0`
  (`phase0/air-roundtrip`), our llvm-project fork's local branch `phase0-air-writer`. Every LLVM C++ API airconv uses
  has an LLVM 23 C API equivalent except the bitcode writer (`phase0/llvm-c-api.md`); airconv's two custom passes and
  `splitBasicBlock` have none either and are rewritten in Rust over the C API.
- **E3: AIR builds 5 to 25 times faster than MSL, before the backend.** 45 pipelines built both ways (DXC's SPIR-V and
  SPIRV-Cross's MSL 3.2 of the 19 HLSL test shaders, against airconv's AIR of the same shaders); each MSL source salted
  so its frontend compile isn't a cache hit (two identical sources still shared one); two runs agree within 1%. Metal's
  MSL frontend, which AIR skips, took a median 3.2 ms and a mean 15 ms a pipeline (the first, 230 ms, includes the
  compiler service starting); airconv's whole path, translation included, takes 0.6 ms a shader (`dxil-translate`).
  The backend compile, which both paths pay, was a cache hit in both and is not measured. GPU time is waived, not
  moved: the test shaders are functional tests, binding airconv's argument buffers outside DXMT would reimplement its
  binding model, and no MSL translator exists to compare at game scale (§8 compares LLVM 15's AIR with LLVM 23's).

**Decided (maintainer, 2026-10-29).** The translator is Rust on LLVM 23 (the newest release; `llvm-sys` with
`no-llvm-linking`), emitting AIR through **our own AIR writer** in Mac-Neutron/llvm-project: a typed-pointer bitcode
writer grown from LLVM's DXIL writer, to which the translator hands the pointee type of every pointer it creates (no
inference), and which writes the newer records Metal reads (the full fast-math flags, `poison`). The writer is one C
entry point the Rust translator calls. It gets its own plan before §5's step 4 moves the translator off LLVM 15, gated
by 64/64 on the test shaders, then the corpus and the check suite on its output.

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
   matters only at link time. The i686 shims (`__tls_array`, `__aulldiv`) wait for step 3, whose `dxgi` and `d3d10core`
   have i386 builds.
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
     `src/winemetal/unix/sm50_wow64.c` (Wine build only): nothing reaches them, and they go with the translator at
     step 4 (P4);
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
R. **Layout and build** (maintainer's direction, 2026-10-30; before step 3, so nothing moves twice). mtld3d becomes
   `src/d3d9` (§3): every file moved with `git mv` (history kept), one root workspace, D3D9 built by meson for every
   target, mtld3d's Makefile, CI, standalone bundle, release machinery and `INSTALL.md` retired (kept in history), its
   check legs one script (`tools/rust-check.sh`, §8), its conventions and audit tree-wide, its documents under
   `docs/d3d9/`. Nothing a player or MacNeutron's launcher sees changes: the same DLL and library names, exports,
   builtin markers and ARM64X images, variables (`MTLD3D_CONFIG`, `DXMT_*`), configuration files, log locations, shader
   caches (D3D9's emitter version is computed from path-independent names, so `mtld3d_shaders.bin` stays valid),
   package names and crate versions; the translator key does not move. MacNeutron changes its build and test scripts in
   the same pin bump. One deliberate change, its own commit: every production C/C++ is built with NDEBUG (the guest's
   PE and Linux D3D9 binaries carried snmalloc's assertions). The guest's CPU baselines (`x86-64-v2`/`pentium4` for D3D9,
   none for DXMT's C++) are kept at R and aligned later in their own measured commit. Plan:
   `2026-10-30-dxmtrs-stepR-layout.md`.
3. `d3d10core`, then `dxgi`: whole DLLs behind COM (`src/d3d10`, `src/dxgi`). Only `d3d10core.dll` is a D3D10 DLL: the
   D3D10 object layer (17 classes) is compiled into `d3d11.dll` and lives inside its objects, so it moves with D3D11's
   front end at step 5, and `d3d10_blob.cpp` (linked only into `d3d12.dll`) with D3D12 (corrected 2026-10-30; plan
   `2026-10-30-dxmtrs-step3-d3d10core-dxgi.md`).
4. The translator (`airconv`, the DXBC parser, our DXIL translator) behind `airconv_public.h`, once Phase 0 has decided
   it. Ported on the LLVM it uses today first, written against the explicit-type C API (`LLVMBuildLoad2`,
   `LLVMBuildGEP2`, `LLVMBuildCall2`) so that pointer types are made in one helper; gated by identical `llvm-dis`
   output on the corpus. Then the LLVM move as its own step (§8's translator gate), with the shader-cache key bumped:
   LLVM 23 and our AIR writer (§4, Decided).
   Two steps rather than one because only the first can be checked byte for byte, which matters for 26k lines.
   Once the translator is Rust, the key also hashes its crate's resolved dependencies and the Rust toolchain. D3D9's
   SM1-3 front end (C3) follows the LLVM move and ships in the same release, so players' caches turn over once.
5. The core (`src/dxmt`) with D3D12 and D3D11, subsystem by subsystem, each behind a seam cut in the C++ first:
   configuration, logging and statistics; the presenter, frame pacing, scaler and frame generation; allocators, staging
   and residency; the shader cache; command encoding and the queue; then the D3D12 and D3D11 front ends (the COM
   objects), with the D3D10 object layer inside D3D11's. The order inside this step is set by its own plan, which
   includes what D3D9 needs from the core (C4); D3D9's front end joins it after D3D11's.
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

Each step is one or more commits, each with its checks green (§8). Each step gets its own plan once the steps before it
have landed.

## 6. What outside code relies on

- **The VM forwarder** (MacNeutron `eac-vm/d3d`) compiles against DXMT's private headers: `d3d12_interfaces.hpp` and
  `d3d11_interfaces.hpp` (private interfaces such as `IMTLD3D11ContextExt2`, in `host/stubs.h`),
  `d3d12_barrier_legacy.hpp` (enhanced barriers as legacy ones, in `guest/proxy.cpp`) and `d3d12_common.hpp` (its tests).
  DXMTRS keeps publishing headers that describe the same interfaces, layouts and conversion, under `include/`; a port
  step that touches one runs the forwarder's build and loopback gate.
- **MacNeutron** relies on the DLL and Unix-library names, the environment variables (`DXMT_*`, `MACNEUTRON_*`,
  `MTLD3D_*`), the log and statistics formats its tools read, and the shader cache's location. Names and variables are
  kept; a format change is made in both repositories in the same pin bump. D3D9's outside names are kept too:
  `MTLD3D_CONFIG`, `mtld3d.conf` beside the game, `log.dir` and `~/Library/Logs/MacNeutron/mtld3d`, wine.app's
  `licenses/mtld3d`, and `mtld3d_shaders.bin` until C3.
- **The translator's C interface** (`airconv_public.h`, `AIRCONV_VERSION`) is internal to DXMTRS, but its cache key is
  what players' shader caches are stored under: it changes only at the LLVM move or a deliberate translator change.

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
  target a crate ships on (1.99.0 has clippy since 2026-10-30), `cargo doc`, the host unit tests (with `cargo test`), and
  `tools/audit.sh`, run under MacNeutron's environment (Wine archives, llvm-mingw and the guest sysroot named by
  documented variables). `docs/CONVENTIONS.md` applies to the whole tree; DXMT is no longer on its list of competing
  implementations.
- **Licences**: DXMTRS is LGPL-2.1-or-later (a work derived from DXMT); mtld3d's files keep their zlib notice: an
  unaltered `LICENSE` in `src/d3d9/`, `tests/d3d9/` and `docs/d3d9/`, `license = "Zlib"` in every mtld3d crate, and the
  README's list of the zlib files elsewhere; `vendor/snmalloc-sys` carries its MIT text. New crates only where a few
  lines won't do, and only under permissive or dual licences (no Apache-2.0-only crate in an LGPL-2.1 tree), checked from
  `cargo metadata` next to the notices `mtld3d-notices.py` writes, for every target that ships (the VM guest's
  included).
- **Binary identity** (step R and any later move of a build): every shipped binary keeps its name, machine, exports,
  imports (listed differences only), builtin marker and VERSIONINFO.
- **Translator**: identical `llvm-dis` output on the shader corpus until the LLVM move; at the move, every corpus
  shader builds its pipelines on Metal, DXMT's shader checks render as before, and the GPU time of the same passes,
  measured on DXMT with DXMT_STATS on the milestone's benches, shows no loss (E3 timed only the build). At C3, every
  shader of a DXSO corpus builds its pipelines and D3D9 renders as before.
- **Performance**, once per milestone (end of steps 2, 4 and 5, after C3 and C4, and after §7), each bench run once:
  SMITE 2's lobby, Palworld's title screen and a D3D11 title the maintainer picks, at parity or better; after C3 and C4
  also the D3D9 titles the maintainer picks (a 32-bit one measured separately in the VM). Game benches need the
  maintainer's go-ahead each time; step 2's is parked for the maintainer (§5, D11).

## 9. Scale and plans

About 100k lines of C and C++ to port, over several months, landing as many small steps. This document covers all of
it. The first implementation plan covers §3 (DXMTRS created and pinned) and §4 (Phase 0). Each step of §5, and §7,
gets its own plan after the step before it has landed and Phase 0 has reported.

Outward steps, each needing the maintainer's yes when it comes: creating DXMTRS on GitHub and pushing to it; switching
MacNeutron's pin; fetching SPIRV-Cross or installing bindgen; fetching xwin if mtld3d's end-to-end programs cannot link
with llvm-mingw; the D3D9 game benches. Step R has its own plan (`2026-10-30-dxmtrs-stepR-layout.md`), and each of
C2-C5 its own, after the steps it depends on have landed. Tools installed at the maintainer's word (2026-10-30): clippy
and rustfmt for 1.99.0, a nightly with rustfmt. DXMTRS started from dxmt `main` at 89558c1 (adaptive frame
generation), pushed to both repositories on 2026-10-29.
