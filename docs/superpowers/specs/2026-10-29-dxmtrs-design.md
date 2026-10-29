# DXMTRS: DXMT in Rust, then one project for D3D9 to D3D12

Status: draft for the maintainer's review, 2026-10-29. Nothing is built or created yet; the first implementation plan
covers Phase 0 and the repository (§4, §9).
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
  every binary MacNeutron ships: ARM64X (aarch64 + arm64ec), x86_64 and i686 PE DLLs, the macOS Unix side, the native
  macOS build the VM host uses (DXMT_NATIVE), and mtld3d's Linux builds for the VM guest.
- D3D9 runs on the shared pieces of §7.
- Every gate of §8 passes, and performance is at parity or better (§8).

Not in this project: one shader path for D3D9 to D3D12 (D3D9 keeps MSL, D3D10-12 keep AIR); moving D3D10-12's command
encoding to the Unix side; new D3D features. Each would be its own decision.

## 2. Decisions (maintainer, 2026-10-28 and 2026-10-29)

| Question | Decision |
|---|---|
| How to get there | One project, ported in steps: DXMT is replaced module by module behind its existing C and COM boundaries, adopting mtld3d's shared pieces as it goes; D3D10-12 keep DXMT's encoding design; checks green at every step. |
| Shader translator | "Rust over LLVM if it's faster than MSL, but use the latest version of LLVM instead of 15." Decided by Phase 0's measurements (§4). |
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
  which become DXMTRS's: `pe/` (Windows targets: today's `mtld3d/windows`) and `unix/` (macOS and Linux: today's
  `mtld3d/unix`). Two workspaces, not one, because a workspace-wide build serves one target family; crates both sides
  use (wire types, configuration) are path members of both, as `unix/shared` already is. One repository, one Rust
  toolchain pin (mtld3d's, 1.99.0 today), one dependency policy.
- **Rust inside the C++ DLLs.** Ported code reaches the remaining C++ as a Rust `staticlib`, exactly one per final
  binary (two Rust staticlibs in one DLL would each carry the standard library), which re-exports that binary's ported
  modules through `extern "C"` functions and COM vtables. PE targets use mtld3d's recipe: Rust's `*-pc-windows-msvc`
  targets with llvm-mingw's C/C++ (`mtld3d-mingw-cc.sh`, `ARM64X_CXX_ABI=mingw`), linked by meson. When a binary has no
  C++ left, its crate becomes a `cdylib` and meson stops building it; when none has, meson goes.
- **Build ownership.** DXMTRS carries the scripts that build it (today's MacNeutron `dxmt/mtld3d-build.sh`,
  `mtld3d-mingw-cc.sh`, `mtld3d-linux-cargo.sh`, `llvm.sh`, and the mingw parts of `toolchain.sh`), behind one entry
  point. MacNeutron's build scripts (`wine-arm64/build.sh` for the Mac, `eac-vm/dxmt/build.sh` for the VM guest,
  `eac-vm/dxmt-native/build.sh` for the VM host) keep fetching the pinned commit and toolchains and call that entry
  point, so a port step is a DXMTRS change plus a pin bump, never a two-repository code change. The move happens in step
  1's plan, the first that changes a build; Phase 0's Rust build already sits inside DXMTRS's meson (§4, E1).
- **Every build stays first-class at every step**: ARM64X, x86_64 and i686 PE; the macOS Unix side; DXMT_NATIVE for the
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

## 5. The order of the port

Ported code replaces C++ only behind a boundary that is already a C or COM interface, or one first cut inside the C++
as its own commit. That rules out the chat design's "util first": `util` is mostly C++ templates (`com`, `rc`,
`objc_pointer`, `util_bit`, the thread pool) that C++ callers instantiate inline, so it goes away with its last C++
caller. Its functions with a plain interface (configuration, environment, logging) are ported early, behind their
existing C++ signatures.

1. `nvapi` and `nvngx`: whole DLLs with C exports.
2. `winemetal`, Unix side: the `unix_call` handlers, onto mtld3d's `mtl` crate (objc2 bindings). Then the PE side: the
   thunks behind `winemetal.h`, onto mtld3d's PE-to-Unix transport. `winemetal.h` stays the boundary the C++ calls.
3. `dxgi`, then `d3d10`: whole DLLs behind COM.
4. The translator (`airconv`, the DXBC parser, our DXIL translator) behind `airconv_public.h`, once Phase 0 has decided
   it. Ported on the LLVM it uses today first, written against the explicit-type C API (`LLVMBuildLoad2`,
   `LLVMBuildGEP2`, `LLVMBuildCall2`) so that pointer types are made in one helper; gated by identical `llvm-dis`
   output on the corpus. Then the LLVM move as its own step (§8's translator gate), with the shader-cache key bumped.
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
  shader builds its pipelines on Metal, DXMT's shader checks render as before, and E3's harness shows no GPU-time loss.
- **Performance**, once per milestone (end of steps 2, 4 and 5, and after §7), each bench run once: SMITE 2's lobby,
  Palworld's title screen and a D3D11 title the maintainer picks in step 2's plan, at parity or better. Game benches need the maintainer's go-ahead each time.

## 9. Scale and plans

About 100k lines of C and C++ to port, over several months, landing as many small steps. This document covers all of
it. The first implementation plan covers §3 (DXMTRS created and pinned) and §4 (Phase 0). Each step of §5, and §7,
gets its own plan after the step before it has landed and Phase 0 has reported.

Outward steps, each needing the maintainer's yes when it comes: creating DXMTRS on GitHub and pushing to it; switching
MacNeutron's pin; fetching SPIRV-Cross or installing bindgen. DXMTRS starts from a dxmt `main` that includes 89558c1
(adaptive frame generation), which is not pushed yet.
