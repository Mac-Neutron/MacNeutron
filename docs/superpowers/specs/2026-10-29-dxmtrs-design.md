# DXMTRS: DXMT in Rust, then one project for D3D9 to D3D12

Status: approved 2026-10-29. Phase 0 ran the same day (plan `docs/superpowers/plans/2026-10-29-dxmtrs-phase0.md`):
Mac-Neutron/DXMTRS exists and MacNeutron builds from it; the results and the translator decision are in §4.
Related: `2026-09-28-macneutron-dxmt-fork-design.md` (our DXMT fork), `2026-09-29-macneutron-dxil-translator-design.md`
(our DXIL translator), `2026-10-20-macneutron-d3d9-mtld3d-design.md` (D3D9 through mtld3d),
`2026-10-17-macneutron-vm-d3d-forwarding-design.md` (the VM host's native DXMT), `2026-10-14-macneutron-framegen-metalfx-design.md`.

## 1. Goal

DXMT (our fork, ~100k lines of C, C++ and Objective-C under `src/`) is rewritten in Rust, step by step, in a new
repository, Mac-Neutron/DXMTRS, which also holds mtld3d (Rust, D3D9). The port adopts mtld3d's pieces as it reaches
them, and once no C++ is left the merge is finished (§7), so that D3D9, D3D10, D3D11 and D3D12 come from one project
with shared interfaces: one Metal binding layer, one PE/Unix bridge, one configuration,
logging, statistics and shader-cache scheme, and one presenter (MetalFX upscaling, HDR, frame pacing and frame
generation) for every API.

Done when:
- DXMTRS has no C, C++ or Objective-C of DXMT's left (generated headers for outside users aside, §6), and cargo builds
  every binary MacNeutron ships: ARM64EC PE DLLs (MacNeutron's Mac build; DXMT's own cross file makes ARM64X), x86_64
  and i686 PE DLLs, the macOS Unix side, the native
  macOS build the VM host uses (DXMT_NATIVE), and mtld3d's Linux builds for the VM guest.
- D3D9 runs on the shared pieces of §7.
- Every gate of §8 passes, and performance is at parity or better (§8).

Not in this project: one shader path for D3D9 to D3D12 (D3D9 keeps MSL, D3D10-12 keep AIR); moving D3D10-12's command
encoding to the Unix side; new D3D features. Each would be its own decision.

## 2. Decisions (maintainer, 2026-10-28 and 2026-10-29)

| Question | Decision |
|---|---|
| How to get there | One project, ported in steps: DXMT is replaced module by module behind its existing C and COM boundaries, adopting mtld3d's shared pieces as it goes; D3D10-12 keep DXMT's encoding design; checks green at every step. |
| Shader translator | "Rust over LLVM if it's faster than MSL, but use the latest version of LLVM instead of 15." After E2 showed Metal refuses opaque-pointer AIR, the maintainer chose (2026-10-29) LLVM 23 with our own AIR writer over LLVM 16 or 15 (§4). |
| Upstream | Followed by hand: upstream DXMT and mtld3d changes are re-implemented in DXMTRS when they bring parity or better; the merge-upstream flow for DXMT ends. |
| Home | A new repository, github.com/Mac-Neutron/DXMTRS. |
| Design (chat, 2026-10-29) | Approved: this document writes it down, with the corrections of §5 and §4. |

Assumption to confirm: "D3DMtl" is mtld3d (athei/mtld3d, already vendored at our DXMT fork's `mtld3d/`).

## 3. The repository and its build

- **Seed.** DXMTRS is created from our dxmt fork's `main` with its history, so it starts as exactly the tree MacNeutron
  ships, mtld3d included. Our dxmt fork is then frozen (kept: it holds the LGPL source of every release so far).
  MacNeutron's `dxmt/pins` switches `DXMT_REPO` to DXMTRS at its first commit, so every later step goes through
  MacNeutron's checks and can ship.
- **Layout.** C and C++ stay under `src/` with meson while any is left. Rust lives in mtld3d's two Cargo workspaces,
  `mtld3d/windows` (Windows targets) and `mtld3d/unix` (macOS and Linux), which §7 renames DXMTRS's `pe/` and `unix/`. Two workspaces, not one, because a workspace-wide build serves one target family; crates both sides
  use (wire types, configuration) are path members of both, as `unix/shared` already is. One repository, one Rust
  toolchain pin (mtld3d's, 1.99.0 today), one dependency policy.
- **Rust inside the C++ DLLs.** Ported code reaches the remaining C++ as a Rust `staticlib`, exactly one per final
  binary (two Rust staticlibs in one DLL would each carry the standard library), which re-exports that binary's ported
  modules through `extern "C"` functions and COM vtables. PE targets use Rust's `*-pc-windows-msvc` targets, linked by
  meson with llvm-mingw (DXMTRS's `tools/cargo-staticlib.sh`, and `rust_runtime_dep` for what MSVC's runtime has and
  llvm-mingw's lacks). When a binary has no C++ left, meson links it from its crate's static library and a `.def` of
  its exports (nvngx, step 1); when none has, meson goes.
- **Build ownership.** DXMTRS carries the scripts that build it (today's MacNeutron `dxmt/mtld3d-build.sh`,
  `mtld3d-mingw-cc.sh`, `mtld3d-linux-cargo.sh`, `llvm.sh`, and the mingw parts of `toolchain.sh`), behind one entry
  point. MacNeutron's build scripts (`wine-arm64/build.sh` for the Mac, `eac-vm/dxmt/build.sh` for the VM guest,
  `eac-vm/dxmt-native/build.sh` for the VM host) keep fetching the pinned commit and toolchains and call that entry
  point, so a port step is a DXMTRS change plus a pin bump, never a two-repository code change. The move happens at
  §7, with the `pe/`/`unix/` rename: DXMT's Rust builds inside DXMTRS's meson (§4, E1), so steps 1 to 6 change no
  MacNeutron script, and mtld3d's scripts first change at the merge (maintainer, 2026-10-29).
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
   1b. `nvapi`, its own plan (`2026-10-29-dxmtrs-step1b-nvapi.md`): done 2026-10-29. It answers 46 functions behind 54
   IDs, 8 of them null on purpose, and logs any other of `nvapi_interface.h`'s 511 IDs; the first draft's 94 was wrong.
   Characterized first by `d3d11_nvapi` (a 152-line golden recorded from the C++ DLL), then ported with three
   decisions:
   - a null pointer the C++ would dereference gets `NVAPI_INVALID_ARGUMENT` (-5) instead of a crash (the error-message
     string, the shader-extension out pointer and device, the display name and id, the resource handle, the HDR
     structs); that is the only change. Three functions still write through a null output, as the C++ does
     (`EnumNvidiaDisplayHandle`, `DISP_GetGDIPrimaryDisplayId`, `GPU_GetConnectedDisplayIds` with a null array);
   - `dxmt-util` (`mtld3d/windows/dxmt-util`) holds what nvngx and nvapi share (logging, COM helpers, the NVEXT check),
     and §7's merge folds it into the shared crates;
   - the probe is the authority where a reading of the C++ and the probe disagree.

   Gates: both Mac `check.sh` lanes 412 ok; the VM guest's x86_64 `nvapi64.dll` (195 KB, Rust; the C++ one is 1.3 MB)
   passed `eac-vm/dxmt/gate.sh`'s seven programs, `d3d11_nvapi` 152 of 152 lines the same as macOS. Two findings: mtld3d's
   `audit.sh` now runs from `mtld3d/` (it `cd`ed to the git toplevel, which inside DXMTRS is the repository root, and
   died partway, so its later rules never ran); and DXMT's log files keep the C++'s CRLF line endings (a fix for the
   Rust sink lands separately).
2. `winemetal`, Unix side: the `unix_call` handlers, onto mtld3d's `mtl` crate (objc2 bindings). Then the PE side: the
   thunks behind `winemetal.h`, onto mtld3d's PE-to-Unix transport. `winemetal.h` stays the boundary the C++ calls.
3. `dxgi`, then `d3d10`: whole DLLs behind COM.
4. The translator (`airconv`, the DXBC parser, our DXIL translator) behind `airconv_public.h`, once Phase 0 has decided
   it. Ported on the LLVM it uses today first, written against the explicit-type C API (`LLVMBuildLoad2`,
   `LLVMBuildGEP2`, `LLVMBuildCall2`) so that pointer types are made in one helper; gated by identical `llvm-dis`
   output on the corpus. Then the LLVM move as its own step (§8's translator gate), with the shader-cache key bumped:
   LLVM 23 and our AIR writer (§4, Decided).
   Two steps rather than one because only the first can be checked byte for byte, which matters for 26k lines.
5. The core (`src/dxmt`) with D3D12 and D3D11, subsystem by subsystem, each behind a seam cut in the C++ first:
   configuration, logging and statistics; the presenter, frame pacing, scaler and frame generation; allocators, staging
   and residency; the shader cache; command encoding and the queue; then the D3D12 and D3D11 front ends (the COM
   objects). The order inside this step is set by its own plan.
6. `util`'s remaining templates, `replay` and the meson build go with their last users.

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
  kept; a format change is made in both repositories in the same pin bump.
- **The translator's C interface** (`airconv_public.h`, `AIRCONV_VERSION`) is internal to DXMTRS, but its cache key is
  what players' shader caches are stored under: it changes only at the LLVM move or a deliberate translator change.

## 7. The merge with mtld3d

Started during the port wherever a ported module needs a piece mtld3d already has (step 2 puts winemetal on mtld3d's
`mtl` crate and transport), finished once no C++ is left:
- **Shared crates**: Metal bindings (`mtl`), the PE/Unix wire types and transport (`shared`), configuration and
  environment variables, logging and crash reports, statistics (DXMT_STATS and mtld3d's performance lines become one
  format), and shader caching and pre-caching.
- **One presenter** for D3D9 to D3D12: MetalFX upscaling, HDR, frame pacing and frame generation.
- **One bridge**: `winemetal.dll` and its Unix side absorb `mtld3d.dll` and `mtld3d.so`'s transport, keeping DXMT's names
  (the ones MacNeutron, the VM forwarder and the Wine prefix already know); D3D9's runtime stays its own crate inside
  the Unix side.
- **What stays separate**: D3D9 keeps mtld3d's design (most work on the Unix side) and its MSL shaders; D3D10-12 keep
  DXMT's encoding on the PE side and its AIR shaders.
- **COM definitions** follow mtld3d: hand-rolled `#[repr(C)]` vtables, no `windows` crate (the native macOS and Linux
  builds need them too). D3D10-12's are generated once with `bindgen` from the C view of the headers the C++ builds
  against (`include/native`, `CINTERFACE`), committed, with size and offset assertions; bindgen is a development tool
  (installing it needs the maintainer's yes).

## 8. Gates

At every step:
- **Functional**: `dxmt/check.sh` in both lanes, the wine-arm64 checks, mtld3d's conformance and end-to-end tests, and
  the VM gates (d3d-smoke, the forwarder's loopback gate), with `MACNEUTRON_FRAMEGEN` modes as today.
- **Rust**: mtld3d's `make check` legs (format, clippy `pedantic` and `nursery` with warnings denied, `scripts/audit.sh`,
  `make doc`) over all Rust in DXMTRS, its CONVENTIONS.md adopted for the whole tree, with its "competing
  implementation" list adjusted now that DXMT is this project.
- **Licences**: DXMTRS is LGPL-2.1-or-later (a work derived from DXMT); mtld3d's files keep their zlib notices. New crates
  only where a few lines won't do, and only under permissive or dual licences (no Apache-2.0-only crate in an LGPL-2.1
  tree), checked from `cargo metadata` next to the notices `mtld3d-notices.py` writes.
- **Translator**: identical `llvm-dis` output on the shader corpus until the LLVM move; at the move, every corpus
  shader builds its pipelines on Metal, DXMT's shader checks render as before, and the GPU time of the same passes,
  measured on DXMT with DXMT_STATS on the milestone's benches, shows no loss (E3 timed only the build).
- **Performance**, once per milestone (end of steps 2, 4 and 5, and after §7), each bench run once: SMITE 2's lobby,
  Palworld's title screen and a D3D11 title the maintainer picks in step 2's plan, at parity or better. Game benches need the maintainer's go-ahead each time.

## 9. Scale and plans

About 100k lines of C and C++ to port, over several months, landing as many small steps. This document covers all of
it. The first implementation plan covers §3 (DXMTRS created and pinned) and §4 (Phase 0). Each step of §5, and §7,
gets its own plan after the step before it has landed and Phase 0 has reported.

Outward steps, each needing the maintainer's yes when it comes: creating DXMTRS on GitHub and pushing to it; switching
MacNeutron's pin; fetching SPIRV-Cross or installing bindgen. DXMTRS started from dxmt `main` at 89558c1 (adaptive frame
generation), pushed to both repositories on 2026-10-29.
