# MacNeutron: Direct3D 9 through mtld3d, in the DXMT fork

Status: design approved 2026-10-20 in conversation (staged: M1 macOS as is, M2 on DXMT's core; mtld3d as a subtree of
the DXMT fork). Each milestone gets its own plan. The 32-bit project modelled on Proton (an i386 Unix side) is a
separate spec; M2's 32-bit half runs on it once it exists.

## 1. Goal

Direct3D 9 games run on Metal in MacNeutron. M1: 64-bit D3D9 games on the macOS path (`MACNEUTRON_VM=0`). M2: every
D3D9 game in the microVM, 32- and 64-bit, where every Steam game runs today. Direct3D 8 follows once mtld3d builds it on
the same core (its issue #788).

Today D3D9 has no Metal path anywhere: DXMT serves D3D10-12 only, and Wine's own `d3d9` (wined3d) renders through
OpenGL, in the VM on Mesa's llvmpipe. Rogue Company's EAC splash, an SDL window on D3D9, costs about 1.3 cores there and
never reaches the Mac (2026-10-20).

## 2. Findings that shaped it (2026-10-20)

- The D3D9 front end the roadmap named (`dacevedo12/dxmt` `v0.4-d3d9`, arm64 design row 7) was archived on 2026-10-18 by
  its author as "a dead end, with no realistic chance of being merged upstream", pointing to athei/mtld3d.
- **mtld3d** (github.com/athei/mtld3d, release 0.14.1 = `0c17212`, 2026-10-19, 699 commits, zlib licence): a Direct3D 9
  for Wine on macOS on Metal, "the fastest Direct3D 9 implementation for Wine on macOS". Rust, about 353k lines with
  tests: `windows/` (PE side: `d3d9` 45k, `core` 128k with the DXSO and fixed-function to MSL translator, `shim`) and
  `unix/` (`mtld3d.so`, 54k, of which its Metal layer is 29k; `shared`, the wire format).
- Its chain: `d3d9.dll` (COM, state, records each frame into fixed records) → `mtld3d.dll` (PE shim owning Wine's
  unix-call globals, `mtld3d_unix_call`) → `mtld3d.so` (Mach-O: encoder, submit, presenter and compile threads on Metal).
  22 unix calls (`Thunks`: device info, queues, layer attach, encoder create/destroy, `SubmitEncoderFrame`,
  `EncoderControl`, shader programs, logging, present).
- Its Unix side reads the PE side's memory directly: the encoder walks the frame's records and payloads in PE-allocated
  chunks, publishes counters into PE-owned memory, and attaches its layer through Wine's `macdrv_functions`
  (`OnMainThread`). It works because both halves share one process; in the VM they don't, so it can't be forwarded at
  its 22-call boundary as is.
- Platforms: PE side i386 and x86_64, and ARM64X images for an arm64 Wine (an x64 game loads them, as our Wine's
  builtins); 32-bit games through WoW64 in a 64-bit prefix. Primary target: athei's wine-build arm64 Wine (macOS 26.4+);
  "Older Wine or CrossOver releases are not expected to work."
- Build: Make over two Cargo workspaces; Rust targets `i686`/`x86_64`/`aarch64`/`arm64ec-pc-windows-msvc` and
  `aarch64-apple-darwin`; the i686/x86_64 links take the MSVC CRT and Windows SDK through `xwin` (`/opt/xwin`, about
  3 GB, `sudo`); the ARM64X link takes llvm-mingw's CRT (llvm-mingw 20260826 or newer, LLD 23 or newer). `WINE_SDK`
  names the Wine it builds against and runs its tests under.
- Install, builtin route: its `d3d9.dll` and `mtld3d.dll` (builtin-marked) into `lib/wine/{i386,aarch64}-windows`,
  `mtld3d.so` into `lib/wine/aarch64-unix`; a prefix made before that needs its marker DLLs in `system32`/`syswow64`.
- MacNeutron's DXMT backend already overrides `d3d9=b` (builtin).

## 3. Decisions

| Question | Decision | Why |
|---|---|---|
| Which D3D9 | mtld3d (zlib), not the archived DXMT branch, DXVK's d3d9 or a GPU path for wined3d | Metal-native and maintained; the DXMT branch is abandoned; DXVK and wined3d-on-Vulkan need Vulkan in the VM (Venus to MoltenVK), new infrastructure beside DXMT |
| Where it lives | A git subtree of athei/mtld3d 0.14.1 under `mtld3d/` in our DXMT fork, history kept, its LICENSE kept | One fork and one pin; upstream releases come in with `git subtree pull`; our changes are fork commits (the patch series gets one large import patch) |
| Order | M1: as is on the macOS path. M2: its Metal layer on DXMT's winemetal API, so it runs in the VM | M1 is days and proves it on our Wine; only M2 reaches Steam games, which run in the VM |
| Fallback | None to wined3d at first | The builtin route replaces Wine's `d3d9`; a switch comes when a game needs one |
| Toolchain | Our llvm-mingw (20261006 pin, LLD 23.1.3) for every PE link if mtld3d's i686/x86_64 builds link with it; `xwin` only if they can't | No 3 GB Microsoft SDK download with `sudo` unless needed; its ARM64X build already links llvm-mingw's CRT |

Out of scope: Direct3D 8 (mtld3d's own plan), forwarding D3D9 calls to the host (MACNEUTRON_VM_D3D=forward stays
D3D10-12), a wined3d fallback switch, and upstream contributions.

## 4. M1: mtld3d on the macOS path

- **Import:** `git subtree add --prefix=mtld3d` of athei/mtld3d at `0c17212` into the DXMT fork tree
  (`build/wine-arm64-src/dxmt`, branch `macneutron`); the series in `wine-arm64/patches/dxmt` gains it as a patch.
- **Build:** `wine-arm64/build.sh`'s DXMT step (`make wine-arm64`) also runs mtld3d's Makefile, with `WINE_SDK` = our
  arm64 Wine build tree,
  `LLVM_MINGW` and `ARM64X_LLD` = our pinned llvm-mingw, for the i686 and ARM64X PE pairs and `aarch64-apple-darwin`
  `mtld3d.so`. The Rust toolchain and targets are the versions mtld3d's CI pins (rustup, a download the maintainer
  approves once). If its i686/x86_64 builds can't link without the MSVC SDK, the plan rules between porting them to
  llvm-mingw's CRT and `xwin`.
- **Install:** beside DXMT's output in `dxmt-install`; `wine-arm64/bundle.sh` copies the pair into `wine.app`'s
  `lib/wine/i386-windows`, `aarch64-windows` and `aarch64-unix`, over Wine's `d3d9.dll`. Prefixes: new ones
  get the markers from `wineboot`; existing ones from the prefix update MacNeutron's launcher already runs (wine.inf
  changes with the Wine), checked by the gate.
- **Signing:** `mtld3d.so` is signed with the rest of `wine.app`'s Mach-O files (release.sh's walk covers new files in
  `lib/wine/aarch64-unix`).
- **Config:** mtld3d reads `mtld3d.conf` beside the game executable; defaults otherwise. No launcher setting in M1.

## 5. M2: on DXMT's core, in the VM

- **Inventory first:** every Metal and AppKit call mtld3d's Unix side makes, mapped onto winemetal's slots (DXMT's
  `winemetal` API, which the VM already forwards: `eac-vm/metal/wm`). Expected gaps: compiling Metal Shading Language
  source (DXMT compiles AIR), MetalFX spatial scaling, its presenter and frame pacing, its layer attach (the VM's
  presentation slots).
- **Port:** mtld3d's Metal layer onto winemetal, so `mtld3d.so` runs wherever Wine's Unix side runs: natively on macOS,
  and in the VM as an x86_64 Linux library under FEX, its Metal reaching the host through the guest winemetal.so. Its
  PE-memory reads then stay in one process on both.
- **32-bit:** in the VM, through the new WoW64 (its PE i386 pair over the x86_64 Unix side, as on macOS) until the
  Proton-style i386 Unix side exists, then an i386 Linux `mtld3d.so` there too.
- Its own plan, after the inventory.

### 5.1 The inventory (2026-10-20) and what it decided

Taken over mtld3d 0.14.1's Unix side (54k lines) against winemetal's 153 slots and the VM's forwarding
(`eac-vm/metal/wm`); the three tables are in `docs/research/2026-10-20-mtld3d-winemetal-inventory.md`.

- **Encoding fits.** Every Metal encoder call is one replay loop (`metal/command.rs` `encode_pass`,
  `encode_leading_blits`) over a flat command array per pass: one winemetal command list and one `encodeCommands` per
  pass. No fences, events, argument buffers, indirect draws, heaps or residency sets anywhere.
- **Resources fit**, but for: no vertex descriptor in `WMTRenderPipelineInfo` (every game pipeline uses
  `[[stage_in]]`); no compile options on `newLibraryWithSource` (mtld3d sets MSL 2.4, `preserveInvariance`, math mode);
  four render nodes missing (vertex bytes, vertex texture, vertex and fragment sampler state); no getters for texture
  type, sample count or usage (mtld3d keeps them itself instead).
- **Memory.** mtld3d hands Metal its own pages (`PageBox`, 16 KiB) and a readback's destination through
  `newBufferWithBytesNoCopy`. In the VM a buffer's memory must be blob memory (`WMTMemoryAlloc`), and a readback must go
  through a staging buffer.
- **Callbacks.** Completion handlers (6 sites, 3 load-bearing) and the drawable's presented handler have no winemetal
  form: a retirement thread per queue waits on each command buffer instead, and the present's command buffer stands in
  for its presented time.
- **macOS-only.** About 3,000 lines exist for Wine's macOS window code (view parking, client surfaces, the
  `WineMetalLayer` bypass, the cursor overlay, the screen-parameter filter, the HUD's State Reporter, the Main Thread
  Checker, App Nap activity): none has a counterpart in the VM, whose host window is MacNeutron's own.

Decisions (rulings of the M2 plan, recorded here as the spec they bind):

| Question | Decision | Why |
|---|---|---|
| One Metal layer or two | One source. A `mtl` module re-exports objc2's bindings on the product macOS build and, under the `winemetal` feature, is a facade with the same names over winemetal's call table. The Linux build always takes the facade | The Metal layer's code stays upstream's (each `git subtree pull` merges as before) and the macOS product keeps M1's behaviour; the facade is the only new Metal code |
| How mtld3d.so reaches winemetal | It loads `winemetal.so` from its own folder and calls its 64-bit `__wine_unix_call_funcs` slots with winemetal's parameter structs, for 32- and 64-bit processes alike | Wine's Unix side of a process is 64-bit; the guest's `winemetal.so` exports the same table and forwards it |
| How it is tested before the VM | The facade also builds for macOS (`winemetal` feature, aarch64-apple-darwin): that build runs M1's gates on macOS's own winemetal.so | Every VM-only difference except FEX and the transport is then found on the Mac |
| macOS-only features under the facade | Left out: views are winemetal's (`CreateMetalViewFromHWND`), the hardware cursor is the game's, SDR only, occlusion as DXMT has it | The VM has none of the window code they serve; the product macOS build keeps them |
| winemetal's gaps | Fork commits in DXMT: four render nodes appended to `WMTRenderCommandType`; two new slots appended to the table (a render pipeline with a vertex descriptor, a source library with compile options); the VM's command-list table and slot catalog grow by the same | Appending keeps every existing slot and node of DXMT's as it is |
| Toolchains | The x86_64 and i686 PE pairs link llvm-mingw's CRTs as the ARM64X pair does (no xwin); the Linux `mtld3d.so` is built on the Mac with the pinned Rust's x86_64-unknown-linux-gnu target, llvm-mingw's clang and ld.lld, against the builder's x86-64 sysroot | No Microsoft SDK, no Rust in the builder |

Milestones: **M2a** the facade, proved on macOS (M1's gates on the `winemetal` build); **M2b** mtld3d in the VM (the
Linux build, the x86 PE pairs, the VM's wire, the image; Wine's d3d9 tests and our triangle program in the guest, 64-
and 32-bit). Each has its own plan.

## 6. Gates

M1: mtld3d's `make check` and `make test` (its end-to-end suite and Wine's D3D9 conformance corpus) against our Wine;
MacNeutron's `make dxmt-check` and `make test` unchanged; one 64-bit D3D9 program or game rendering on the macOS path.
M2: the same suite in the VM; one 32-bit D3D9 game playing there.

## 7. Risks

- Our Wine is not wine-build: patches and ARM64X re-stamping differ (mtld3d's install needs wine-build arm64-11.19-2's
  winebuild or later for hybrid builtins). M1's first step is its test suite against our Wine.
- "Conformance serves speed": where exact D3D9 behaviour costs frame time without breaking games, mtld3d chooses speed.
- M2 diverges from upstream's Metal layer: each `git subtree pull` after it has to be merged through the port.
- mtld3d's own CI and conventions (its `make audit`, its `CONTRIBUTING.md` rules check) apply to code we change inside
  `mtld3d/`.
