# MacNeutron D3D9 M2b: mtld3d in the VM — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Direct3D 9 games in the VM draw through mtld3d on the host's Metal: an x86_64 Linux `mtld3d.so` on the guest
winemetal.so (M2a's facade), with x86_64 and i386 PE pairs, in wine.erofs.

**Architecture:** The facade M2a proved on macOS builds for `x86_64-unknown-linux-gnu` on the Mac (the pinned Rust,
llvm-mingw's clang and ld.lld, a copy of the builder's x86-64 sysroot). Its slots reach the host through the VM's
winemetal forwarding, which learns M2a's four render nodes and two slots. The x86 PE pairs take M1's ARM64X route
(static libraries linked by llvm-mingw's lld-link against its CRT). `eac-vm/dxmt/build.sh` builds all three for the
guest; `stage.sh` and `image/build.sh` put them in wine.erofs over Wine's `d3d9.dll`.

**Tech Stack:** Rust 1.99.0 (`dxmt/pins` `MTLD3D_RUST`), llvm-mingw 20261006 (clang/LLD 23), the Fedora builder, the
microVM (`eac-vm/run-guest.sh`), FEX in the guest.

**Spec:** `docs/superpowers/specs/2026-10-20-macneutron-d3d9-mtld3d-design.md` §5, §5.1, §6. M2a's plan and ledger
(`.superpowers/sdd/2026-10-20-macneutron-d3d9-m2a-winemetal-facade/progress.md`) hold the facade's rulings.

## Global Constraints

- Appending keeps every existing slot and node: winemetal grows by slots 156/157 and nodes 46-49 only; `MN_WM_D3D`
  stays 155 (mnd3d.dll calls `WMTForward(155)`).
- No xwin, no Microsoft SDK, no Rust in the builder (§5.1 Toolchains).
- The macOS product build keeps M1's behaviour: every change in `mtld3d/` is under `cfg(mtl_facade)`,
  `cfg(target_os = "linux")` or a Makefile variable the macOS build does not set.
- mtld3d's rules apply to code changed inside `mtld3d/` (its CONTRIBUTING.md, docs/CONVENTIONS.md, `make audit`).
- One VM run at a time; the builder starts with `VM_SHARES="out=$PWD/build/eac-vm-work/images,outp=$PWD/build/p/eac-vm-work/images"`.
- Signing env (`MACNEUTRON_SIGN_IDENTITY`, `MACNEUTRON_PROVISIONING_PROFILE`) exported for builds that sign.
- Never open MacNeutron.app or touch Steam; launcher tests with `MACNEUTRON_VM=0`; gating scans with `/usr/bin/grep`.
- Commits: repo `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; DXMT fork `Assisted-by: Claude Code: claude-opus-5-5`.

## Rulings this plan makes (spec §5.1 refined)

- **32-bit processes call winemetal's WoW64 table.** The guest's blob memory goes below 2 GiB only through
  `__wine_unix_call_wow64_funcs` (its entries mark the process 32-bit, `galloc.h`), and the i386 PE side writes
  into PageBox pages and Shared buffers' contents. mtld3d.so's own WoW64 table marks the process; the facade's loader
  then takes winemetal's WoW64 table (its params are the 64-bit ones for every slot the facade calls: no SM50 slot).
  §5.1 said the 64-bit table for both widths. Cost if wrong: one symbol name.
- **The guest Wine is rebased for 0041** here (M1's deferred minor 5, part two), since mtld3d's reservations reach the
  same `map_reserved_area` in the guest.
- **Wine's d3d9 tests run in the guest through mtld3d's own runner**, cross-built for Linux like `mtld3d.so`; the x64
  lane's figures are compared with the macOS product's (stateblock 0, visual 31, device 739, d3d9ex 74).

## Review Focus

1. A 32-bit process: PageBox pages and Shared buffer contents below 4 GiB (the i386 PE side faults otherwise).
2. A prefix made before mtld3d was in the image (no `mtld3d.dll` marker in system32/syswow64): d3d9 still loads mtld3d.
3. A guest whose winemetal.so lacks slots 156/157 (an old image): pipelines with vertex descriptors fail loudly, not
   silently draw nothing.
4. Command lists with nodes 46-49 among DXMT's own: the host's node walk keeps every node after them.
5. The builder off: the sysroot copy and the guest SDK already fetched are used; a missing one stops with its reason.

---

### Task 1: the VM wire learns M2a's nodes and slots

**Files:**
- Modify: `eac-vm/metal/wm/cmdlist.c` (render nodes 46-49), `eac-vm/metal/wm/calls.h` (`MN_WM_SLOTS` 158,
  `MN_WM_D3D` 155 as a literal), `eac-vm/metal/wm/calls.c` (slots 155 placeholder, 156, 157),
  `eac-vm/metal/wm/guest/winemetal.c` and `guest/wow64.c` (entries 156, 157)
- Test: `eac-vm/metal/wm/tests/marshal-test.m`

**Interfaces:**
- Produces: guest `__wine_unix_call_funcs[158]` and `__wine_unix_call_wow64_funcs[158]` (155 Direct3D, 156
  `MTLDevice_newRenderPipelineStateWithVertexDescriptor`, 157 `MTLDevice_newLibraryWithSourceOptions`).

- [ ] **Step 1: tests.** In marshal-test.m: `catalog_sizes` gains 155 (0: no params, guest-only), 156
  (`sizeof(struct unixcall_mtldevice_newrenderpso_vertex)` = 40), 157 (48); slot 156 packs its info's two function
  handles and its vertex descriptor (31 attributes, 31 layouts) and replays with them translated; slot 157 packs the
  source and its `WMTCompileOptions`; an encode of a render list `[SetVertexBytes(16 bytes), SetVertexTexture(t),
  SetVertexSamplerState(s), SetFragmentSamplerState(s), Draw]` arrives with both handles translated, the 16 bytes
  intact and the draw after them.
- [ ] **Step 2: run** `make -C eac-vm/metal/wm marshal-test`. Expected: FAIL on the new slots and nodes.
- [ ] **Step 3:** nodes 46/47 take `wmtcmd_render_setbytes`/`settexture`, 48/49 `wmtcmd_render_setsampler` (handle
  `sampler`); slot 156 `S` with `info` (`N_RENDER`) and `vertex` (`WMTVertexDescriptorInfo`, no handles), 157 `S`
  with `source` (array, `length`) and `options` (`WMTCompileOptions`); [155] `MN_WM_LOCAL` named `MacNeutron_d3d`.
- [ ] **Step 4: run** marshal-test, handles-test, kinds-test, memory-test, host-test. Expected: all pass.
- [ ] **Step 5: commit** `metal wm: mtld3d's render nodes 46-49 and slots 156/157 forwarded (D3D9 M2b)`.

### Task 2: mtld3d-unix for x86_64 Linux

**Files:**
- Create: `dxmt/mtld3d-linux-cc.sh` (clang for the guest: llvm-mingw's, `--target=x86_64-redhat-linux
  --sysroot=$MTLD3D_SYSROOT -fuse-ld=lld`), `eac-vm/sysroot/fetch.sh` (the builder's sysroot's `usr/include`,
  `usr/lib64`, `usr/lib/gcc` into `build/eac-vm-work/sysroot-x86_64`; kept when the builder is off)
- Modify (DXMT fork, `mtld3d/unix/`): `unix/src/crash.rs`, `shared/src/identity.rs`, `unix/src/handlers.rs`,
  `unix/src/translation.rs`, `unix/src/lib.rs` (the WoW64 table), `mtl/src/table.rs` (WoW64 table pick),
  `mtl/src/metal/mod.rs` (`alloc_pages`/`free_pages` through slots 153/154 on Linux), whatever else the compiler
  lists
- Test: `mtl/tests/objects.rs` (pages), the unit suites, and the Linux test binaries' build

**Interfaces:**
- Produces: `mtld3d_mtl::table::mark_wow64()` (the unix crate's WoW64 entries call it first);
  `libmtld3d_unix.so` for `x86_64-unknown-linux-gnu`.

- [ ] **Step 1:** `cargo build --target x86_64-unknown-linux-gnu -p mtld3d-unix` with the linker and `CC_`/`CXX_`
  of that target set to the wrapper: the compiler's list goes to the ledger.
- [ ] **Step 2: test** in `objects.rs`, Linux-only: `pages_come_from_winemetal_memory` (an `alloc_pages(65536)`
  pointer is 16 KiB-aligned and is what slot 153 returned; `free_pages` hands it to 154). Under the facade on macOS,
  `pages_are_mmapped` stays.
- [ ] **Step 3:** the Linux arms: no Mach crash reporter (the signal path, or none: the ledger says which); the
  image identity from the ELF build ID or none; memory statistics from `getrusage`/`mallinfo2`; `translated()`
  `None`; pages from slots 153/154; the WoW64 pick. Each `cfg(target_os = "linux")`.
- [ ] **Step 4:** Linux build links; `cargo test --no-run --target x86_64-unknown-linux-gnu` for mtld3d-shared,
  mtld3d-unix and mtld3d-mtl builds; on macOS the product unit suites (358 unix + 116 shared) and the facade suites
  (250, and the 12 facade tests) still pass. Expected: as listed.
- [ ] **Step 5: commit** (fork) `mtld3d: an x86_64 Linux build on winemetal (the VM's)`.

### Task 3: the x86 PE pairs on llvm-mingw

**Files:**
- Modify (fork): `mtld3d/Makefile` (`X86_MINGW=<dir>`: `windows-x86_64` and `windows-i686` link as `windows-arm64x`
  does, staticlibs through `$(LLVM_MINGW)/bin/lld-link -lldmingw /machine:x64|x86` against llvm-mingw's CRT and
  `<dir>/lib/wine/<arch>-windows/libntdll.a`)
- Modify: `dxmt/mtld3d-build.sh` (`--guest <guest-wine-sdk>`: the x86_64 and i686 pairs and the Linux `mtld3d.so`,
  printing the three folders), `Makefile` (`build/dxmt-tests-i386/d3d9_triangle.exe` through i686 clang)

- [ ] **Step 1: check** (a script row in `dxmt/tests/mtld3d_guest_test.sh`): each `d3d9.dll` exports
  `Direct3DCreate9`, `Direct3DCreate9Ex` and the rest of `d3d9.def` undecorated; each `mtld3d.dll` imports from
  `ntdll.dll` and carries Wine's builtin marker; machine types x64/i386; `mtld3d.so` is ELF x86-64 with
  `__wine_unix_call_funcs` and `__wine_unix_call_wow64_funcs`, needing only libc, libm, libgcc_s and ld-linux.
  Expected before Step 2: FAIL (no build).
- [ ] **Step 2:** the Makefile route and `--guest`.
- [ ] **Step 3: run** the script. Expected: every row ok.
- [ ] **Step 4: commit** (fork, then repo).

### Task 4: the guest build, stage and image

**Files:**
- Modify: `eac-vm/dxmt/build.sh` (mtld3d's guest build after DXMT's, into `dxmt-guest/{x86_64,i386}-windows` and
  `dxmt-guest/x86_64-unix`), `eac-vm/dxmt/stage.sh` (`d3d9.dll`, `mtld3d.dll` both widths, `mtld3d.so`),
  `eac-vm/dxmt/check-build.sh`, `eac-vm/image/build.sh` (`x86_64-unix/*.so`), `eac-vm/wine-patches/BASE` and
  `eac-vm/wine/notes.md` (wine-linux on the macneutron tip with 0041)

- [ ] **Step 1:** wine-linux rebased (`git am -3` of `eac-vm/wine-patches` on df0e539), BASE updated.
- [ ] **Step 2:** check-build.sh rows for mtld3d's five files (RED before Step 3).
- [ ] **Step 3:** build/stage/image changes; `guest-stages.sh wine forwarder dxmt d3d`, then `image/build.sh wine`
  in the builder. Expected: check-build ok; wine.erofs newer than the run; `.mn-source-dxmt` names the fork's tip.
- [ ] **Step 4: commit**.

### Task 5: the gates in the guest

**Files:**
- Create: `eac-vm/tests/d3d9.sh` (guest), `eac-vm/dxmt/d3d9-gate.sh` (Mac)
- Modify: `eac-vm/dxmt/invocations` (`d3d9_triangle|d3d9_triangle.exe||d3d9`), `eac-vm/dxmt/share.sh` (the i386
  triangle, the Linux runner and test binaries, d3d9_test.exe x64 and i386), `wine-arm64/winetests.sh` (d3d9's
  i386 build)

- [ ] **Step 1:** d3d9.sh, one VM run: (a) the Linux unit-test binaries (mtld3d-mtl's facade tests on the guest
  winemetal.so through the stub ntdll.so); (b) d3d9_triangle x64 and i386, fresh prefix and one whose
  `mtld3d.dll` markers were removed: four `ok` rows each; (c) Wine's d3d9 tests through the runner, x64 as
  `arm64x`, i386 as `i686-arm64`; prints a summary line per lane and `d3d9 gate: ok`/`FAIL`.
- [ ] **Step 2: run** `sh eac-vm/dxmt/d3d9-gate.sh`. Expected: unit tests pass; triangle rows ok (64 and 32);
  x64 conformance: stateblock 0, visual 31, device 739, d3d9ex 74 and no crash, each difference named in the
  ledger with its cause and fixed when mtld3d or the wire is wrong; i386: no crash, each subtest's difference from
  x64 named.
- [ ] **Step 3:** the VM's gates unchanged: `eac-vm/dxmt/gate.sh` (now with the triangle), `metal-wm.sh`,
  `wow64.sh`. Then `MACNEUTRON_VM=0 make test` and `make dxmt-check` on the Mac.
- [ ] **Step 4: commit**; DXMT patches exported (new ones only).

Held for the maintainer (spec §6): one 32-bit D3D9 game playing in the VM.
