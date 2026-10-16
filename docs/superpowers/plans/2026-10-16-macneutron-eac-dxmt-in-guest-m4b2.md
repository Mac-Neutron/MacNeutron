# EAC microVM, milestone 4b-2: DXMT in the guest Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DXMT's own headless test programs run in the guest, under our x86-64 Wine with an x86-64 build of our
DXMT, and print the same lines as on macOS.

**Architecture:**
- **DXMT build.** An x86-64 Windows build of our DXMT fork, made on the Mac with MacNeutron's llvm-mingw against
  the guest Wine's import libraries. A new meson option, `guest_vm`, is off by default, so the product's ARM64EC build
  compiles the same source as before. With it on, the build skips DXMT's macOS-only parts (winemetal's Unix side and
  airconv's darwin library).
- **Memory DXMT hands to Metal.** Four DXMT sites allocate memory themselves and give it to `newBufferWithBytesNoCopy`.
  In a `guest_vm` build they allocate it through two new winemetal calls, `WMTMemoryAlloc` and `WMTMemoryFree`
  (slots 153 and 154). The guest `winemetal.so` answers both from its blob allocator, so the host can map that memory.
- **Packaging.** DXMT's DLLs replace Wine's `d3d10core`, `d3d11`, `d3d12` and `dxgi` in `wine.erofs`.
  `winemetal.dll` goes in as a builtin, and the guest `winemetal.so` beside it in `x86_64-unix`.
- **Gate.** Each program runs twice on macOS through MacNeutron's launcher, and once in the guest. The guest must print
  every line both macOS runs agree on, from that program's own output.

**Tech Stack:**
- meson and MacNeutron's llvm-mingw (`dxmt/toolchain.sh`);
- the DXMT fork tree MacNeutron builds (`build/wine-arm64-src/dxmt`, the pinned commit plus `wine-arm64/patches/dxmt`);
- 4b-1's forwarding (`eac-vm/metal/wm`);
- the builder VM (guest Wine build and image);
- `macneutron launch` for the macOS reference.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.6 and §8 milestone 4 (4b-2: "DXMT's
check suite passes in the guest as it does on macOS").
- **Facts:**
  - 4b-1's plan, section "After 4b-1";
  - research README §8;
  - `docs/research/2026-10-15-vm-route/winemetal-calls.md`.

## Global Constraints

### Repo and product

- **Worktree and commits.** Work in `.claude/worktrees/upstream-master`, branch `exp/upstream-master`. Commit locally,
  never push.
- **New DXMT patch.** It is `wine-arm64/patches/dxmt/0071-…patch`, made with `git format-patch` on the `macneutron`
  branch of `build/wine-arm64-src/dxmt` after 0070. The product build applies the series with `git am` in glob order.
- **The product must not change.**
  - Every source change in 0071 sits behind `#ifdef DXMT_GUEST_VM`, which only `-Dguest_vm=true` defines.
  - Every meson change is conditional on that option, which defaults to false.
  - Task 1 proves this by preprocessing every file 0071 touches with the product's own compile commands, before and
    after the patch: the output must be identical.
- **Guest DXMT build options:**
  - `--cross-file build-win64.txt` with the llvm-mingw from `dxmt/toolchain.sh` on PATH;
  - no `dxmt-cpu.txt`: the guest CPU is FEX's, so the x86-64 baseline;
  - `-Dguest_vm=true -Dwine_builtin_dll=true -Denable_d3d12=true -Denable_nvapi=false -Denable_nvngx=false`;
  - `-Dwine_install_path=build/eac-vm-work/guest-wine-sdk`, which holds
    `lib/wine/x86_64-windows/lib{winecrt0,ntdll,dbghelp}.a` from the builder's `~/wine-stage/opt/mn-wine`, and
    `bin/winebuild`, a symlink to the Mac's `build/wine-arm64-src/wine-build/tools/winebuild/winebuild`;
  - build dir `build/eac-vm-work/dxmt-guest-build`, install `build/eac-vm-work/dxmt-guest`.

  NVAPI and NVNGX are left out: the route where a game sees an NVIDIA GPU is cancelled.

### Forwarding

- **Slots 153 and 154** (`WMTMemoryAlloc`, `WMTMemoryFree`) are answered in the guest:
  - alloc: a `galloc` region, its pointer 16 KiB-aligned and its length in whole pages;
  - free: `mn_gfree` of that region, which comes back only after every host holder is gone;
  - the host never sees them. Its `winemetal.so` stays the product's.
- **Internal ops.** `MN_WM_STATS` and `MN_WM_NOTIFY_ATTACH` move to fixed op numbers past any slot count (`0x800`,
  `0x801`), and `dispatch.m`'s range check follows.

### The gate

- **Programs (20):**
  - `d3d11_vsia`;
  - `d3d12_api`, `d3d12_bounds`, `d3d12_cache`, `d3d12_compress`, `d3d12_copy`, `d3d12_depth`, `d3d12_dxil`,
    `d3d12_dxil_exec`;
  - `d3d12_hazards`, `d3d12_indirect`, `d3d12_layered`, `d3d12_lod`, `d3d12_null`, `d3d12_query`;
  - `d3d12_timestamp`, `d3d12_triangle`, `d3d12_upscale`, `d3d12_volume`, `d3d12_vsread`.

  Each runs once, with the arguments of `dxmt/check.sh`'s first `run ours` line for it, its `cachetest`/`dxil`
  helpers, and `X`/`H`.
- **Left out:**
  - `d3d12_clear`, `d3d12_fullscreen`, `d3d12_ffx_swapchain` and `present_loop`: they present, which is 4c's;
  - `d3d12_xess`: it needs Intel's `libxess.dll`;
  - `d3d12_dlss`: the cancelled NVIDIA route.
- **The comparison rule.** It is fixed here, before anything runs:
  - **Which lines.** A program's compared lines are those starting with that program's own prefixes, listed in
    `eac-vm/dxmt/invocations`: for example `triangle `, `copy `, `group `, `hazard `. Launcher, Wine and DXMT log
    lines never count.
  - **The reference.** It is two macOS runs. Where both give the same number of prefixed lines, a position is
    required if they agree there. Positions where they differ (timings, counters) are masked.
  - **The guest's output** must give the same number of prefixed lines, and equal the reference at every required
    position, byte for byte. Both sides run on the same GPU, so pixels, hashes and DXIL words must be exact.
  - **Too few lines fails.** A program fails if it has no required lines. The gate prints each program's count.
- **Runs:**
  - **macOS reference.** `.build/release/macneutron install --tool-dir <work>/ours --wine-app
    build/wine-arm64/wine.app`, in `${TMPDIR}/macneutron dxmt-guest-ref`, never a user tool folder. Then `macneutron
    launch waitforexitandrun`, with `check.sh`'s environment:
    - `SteamAppId=0`;
    - `MACNEUTRON_GRAPHICS=dxmt`;
    - `MACNEUTRON_NO_STEAM_BRIDGE=1`;
    - `MACNEUTRON_NO_METALFX=1`;
    - `DXMT_MAX_ANISOTROPY=0`;
    - a fresh shader cache per run.

    Steam is never started or touched.
  - **Guest.** As user `mn`, with `WINEPREFIX=/home/mn/dxmt` (made with `WINEDLLOVERRIDES="mscoree,mshtml=" wineboot
    -u`) and `DISPLAY=:0` on Xvfb, as `eac.sh` does. Each program has a 300 s timeout, and `wineserver -k` runs between
    programs.
- **The host's own errors fail the gate.** An `mn-executor: error` or `mn-winemetal:` failure line in the host's
  output during the guest run fails it.

### Process

- Lighter testing: one gate per task, the full gates at the end (`eac-vm/metal/gate.sh` and the new
  `eac-vm/dxmt/gate.sh`).
- Gates assert.
- Privacy scan before docs commits.
- Commit trailers.

## Review Focus

1. **`WMTMemoryFree` while Metal still holds a buffer over that memory.**
   - Expected: the region stays reserved until the host's `MN_NOTIFY_REGION_FREE`. A new allocation doesn't get it
     back before then.
   - Test: Task 3's `wmt-memory` case in memory-test.
2. **The product build with the option off.**
   - Expected: it compiles the same source.
   - Test: Task 1's preprocessing check over every file 0071 touches, with the product's own compile commands.
3. **A program that hangs or crashes in the guest:** a wait across rings, a lost event, a refused call DXMT doesn't
   check.
   - Expected: the gate names it, after its timeout, and goes on with the next program.
   - Test: Task 5's runner kills it and reports `FAIL <name>: timeout`.
4. **A comparison that passes by masking everything.**
   - Expected: a program with no required lines fails, and the gate prints the counts.
   - Test: Task 5's `self-test`, which feeds the comparer a reference with one required line changed, one with every
     line masked, and a guest output with a line missing. Each must fail.
5. **A host failure no guest line shows** (a queued call refused).
   - Expected: the gate fails on the host's error lines.
   - Test: Task 5's gate reads the host output.

---

### Task 1: DXMT patch 0071, the guest option

**Files:**
- Create: `wine-arm64/patches/dxmt/0071-meson-a-guest_vm-build-for-MacNeutron-s-EAC-microVM.patch`, made in
  `build/wine-arm64-src/dxmt` (branch `macneutron`).
- Create: `eac-vm/dxmt/inert.sh`, the product-inertness check.

**What 0071 changes (all behind the option):**
- `meson.options`: `option('guest_vm', type: 'boolean', value: false)`.
- `meson.build`: with the option on, `-DDXMT_GUEST_VM`.
- `src/winemetal/meson.build`: skip `subdir('unix')`.
- `src/airconv/meson.build`: skip `subdir('darwin')`.
- **The two calls:**
  - `src/winemetal/winemetal.h`: declares `WINEMETAL_API void *WMTMemoryAlloc(uint64_t length)` and
    `WINEMETAL_API void WMTMemoryFree(void *ptr)`.
  - `winemetal_thunks.h`: `struct unixcall_wmtmemory { uint64_t length; struct WMTMemoryPointer ret; }`.
  - `winemetal_thunks.c`: the thunks, `UNIX_CALL(153, …)` and `UNIX_CALL(154, …)`.
  - The macOS Unix side gets no entries: it never sees a `guest_vm` build.
- **The four sites**, under `DXMT_GUEST_VM`:
  - `dxmt_buffer.cpp:44-47` and its free at `:75`;
  - `d3d12_command_allocator.cpp:63` and its free in `d3d12_command_allocator.hpp:132-136`;
  - `dxmt_context.cpp:58-62` and its destructor;
  - `dxmt_ring_bump_allocator.hpp:119` and `Block::~Block`.

  Each allocates with `WMTMemoryAlloc` and frees with `WMTMemoryFree`.

**`inert.sh`:**
- From the product build's `compile_commands.json` (`build/wine-arm64-src/dxmt-build`), it preprocesses (`-E`) every
  `.c`/`.cpp` 0071 touches. It also preprocesses one file that includes each touched header.
- It does this twice: on the tree without 0071 (`git stash` is not used: a `git worktree` of the pre-0071 commit in
  the DXMT repo) and with it.
- It exits 1 if any output differs.

- [ ] **Step 1: Write `inert.sh`.** Run it against a deliberately non-inert 0071 draft, with one site changed outside
  the guard. Expected: FAIL, naming the file.
- [ ] **Step 2: Write 0071 properly.**
- [ ] **Step 3: Run `inert.sh`.** Expected: identical for every file.
- [ ] **Step 4: Build the product's DXMT step** (`wine-arm64/build.sh`'s DXMT stage, the incremental `make
  wine-arm64`). Expected: it builds; 0071 applies after 0070.
- [ ] **Step 5: Commit.** `dxmt 0071: a guest_vm build for the EAC microVM, inert in the product`.

### Task 2: the guest DXMT build

**Files:**
- Create: `eac-vm/dxmt/build.sh` (Mac). It:
  - fetches the guest import libraries from the builder once, into `build/eac-vm-work/guest-wine-sdk`;
  - makes the `bin/winebuild` symlink;
  - runs meson with Global Constraints' options;
  - installs into `build/eac-vm-work/dxmt-guest`.
- Create: `eac-vm/dxmt/check-build.sh`, which requires these of the output:
  - `x86_64-windows/{d3d10core,d3d11,d3d12,dxgi,winemetal}.dll`, each a PE32+ x86-64 DLL with the Wine builtin marker;
  - `winemetal.dll` exporting `WMTMemoryAlloc` and `WMTMemoryFree`;
  - `d3d12.dll` importing `winemetal.dll`;
  - no Mach-O and no `*-unix` directory.

- [ ] **Step 1: Write `check-build.sh`.** Run it. Expected: FAIL (nothing built).
- [ ] **Step 2: Write `build.sh`, and run it.**
- [ ] **Step 3: Run `check-build.sh`.** Expected: PASS.
- [ ] **Step 4: Commit.** `eac-vm: DXMT's x86-64 guest build`.

### Task 3: slots 153 and 154 in the guest

**Files:**
- Modify:
  - `eac-vm/metal/wm/calls.h` and `calls.c`: `MN_WM_SLOTS` 155, a mode `MN_WM_LOCAL`, and the two entries with the
    params struct from 0071's `winemetal_thunks.h`, taken from the build tree's headers;
  - `MN_WM_STATS` → `0x800` and `MN_WM_NOTIFY_ATTACH` → `0x801` in `calls.h` and `notify.h`, with `marshal.c`'s and
    `dispatch.m`'s range checks;
  - `guest/winemetal.c`: `STUB(153) STUB(154)`, answered with `galloc` (local slots never reach the transport);
  - `tests/marshal-test.m`: `coverage`'s sizes (+2, from the header) and `modes` (74 synchronous, 62 queued,
    17 refused, 2 local);
  - `tests/memory-test.m`.
- The guest build gets the headers from `build/wine-arm64-src/dxmt` after 0071 (`build-guest.sh` already copies them).

- [ ] **Step 1: Write the failing tests.**
  - `coverage` and `modes` with 155 slots.
  - memory-test `wmt-memory`:
    1. Slot 153 gives 64 KiB, 16 KiB-aligned. A slot-18 buffer is made over it (`info.memory` set).
    2. Slot 154 frees it while the buffer lives. A new 64 KiB allocation doesn't return that memory.
    3. The buffer is released and swept, and the queue drained. Now an allocation can return it.
- [ ] **Step 2: Run them, expecting a failure.** `make -C eac-vm/metal/wm marshal-test memory-test`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expecting a pass,** plus `eac-vm/metal/gate.sh`.
- [ ] **Step 5: Commit.** `eac-vm: WMTMemoryAlloc and WMTMemoryFree answered in the guest`.

### Task 4: DXMT in the guest image

**Files:**
- Modify: `eac-vm/image/build.sh`. Its `wine_image` installs, from `$DXMT_GUEST` (default `~/mn-dxmt-guest` in the
  builder), before `mkfs.erofs`:
  - `x86_64-windows/*.dll` over Wine's own in `lib/wine/x86_64-windows`;
  - `x86_64-unix/winemetal.so` into `lib/wine/x86_64-unix`.
- Create: `eac-vm/dxmt/stage.sh` (Mac). It assembles `build/eac-vm-work/dxmt-guest-stage/{x86_64-windows,x86_64-unix}`
  from Task 2's install and `share/bin/wm/winemetal.so`, and syncs it to the builder's `~/mn-dxmt-guest`.
- Create: `eac-vm/tests/dxmt-smoke.sh`. It runs `d3d12_copy.exe` in the guest and requires `copy ` lines.
- Modify: `eac-vm/dxmt/` gets `share.sh`, which copies `build/dxmt-tests/*.exe` and `dxmt/tests/{shaders,dxil,dxbc}`
  into `build/eac-vm-work/share/dxmt-tests`.

- [ ] **Step 1: Write `dxmt-smoke.sh`, and run it on the current image.** Expected: FAIL. Wine's own `d3d12.dll` has
  no device here, so there are no `copy ` lines.
- [ ] **Step 2: Implement**, then rebuild `wine.erofs` (`eac-vm/builder.sh ssh 'cd ~/… && sh image/build.sh wine'`, as
  the image README says).
- [ ] **Step 3: Run it.** Expected: PASS, the `copy ` lines printed.
- [ ] **Step 4: Commit.** `eac-vm: our DXMT in the guest's Wine image`.

### Task 5: the gate

**Files:**
- Create: `eac-vm/dxmt/invocations`. One line per program: `name|exe|args|prefixes`. Arguments name files under
  `@T` (the tests folder) and `@S` (shaders), which each side resolves to its own `Z:` path.
- Create: `eac-vm/dxmt/compare.py`. It implements Global Constraints' comparison rule: `compare.py <ref1> <ref2>
  <guest> <prefixes>`, printing `ok <name> <n> lines` or `FAIL <name>: <why>`. Also `compare.py self-test`.
- Create: `eac-vm/dxmt/ref.sh` (Mac). It runs each invocation twice through `macneutron launch` and writes
  `share/dxmt-ref/<name>.{1,2}.txt`.
- Create: `eac-vm/tests/dxmt.sh` (guest). It makes the prefix, starts Xvfb, runs each invocation (300 s timeout,
  `wineserver -k` after), writes `/tmp/dxmt/<name>.txt`, and runs `compare.py` for each.
- Create: `eac-vm/dxmt/gate.sh` (Mac). It runs `share.sh`, then `ref.sh`, then the guest run, then checks the host
  output for error lines. It exits 1 on any FAIL.

- [ ] **Step 1: Write `compare.py` and its `self-test`.** The self-test runs three cases, each of which must fail: a
  reference with one required line changed, references that mask every line, and a guest output with a line
  missing. Run it. Expected: PASS.
- [ ] **Step 2: Write `invocations`, `ref.sh`, `dxmt.sh` and `gate.sh`. Run the gate.** Expected: FAIL in places.
  These are the forwarding gaps the 4b-1 sequence never reached, and Task 6 fixes them.
- [ ] **Step 3: Commit the gate**, its first results noted in the commit message.
  `eac-vm: DXMT's headless programs in the guest, against macOS`.

### Task 6: what the gate finds

Each failing program, in turn:
- [ ] **Step 1: Find the cause** (superpowers:systematic-debugging): the host's log, the guest's stderr, a Mac
  `memory-test`/`marshal-test` repro.
- [ ] **Step 2: Write the failing test.** A Mac-side test (marshal-test or memory-test) where the cause can be
  reproduced there. Otherwise the program's own gate run is the RED.
- [ ] **Step 3: Fix, and run the test and the gate.**
- [ ] **Step 4: Commit each fix,** named for what it fixes.

Done when `eac-vm/dxmt/gate.sh` passes all 20 programs and `eac-vm/metal/gate.sh` still passes.

### Task 7: docs

- [ ] **Research README §9,** "DXMT in the guest (4b-2)": what runs, the numbers (each program's run time in the guest
  against macOS, the host's error count), and the findings from Task 6.
- [ ] **The spec's milestone 4 line:** 4b-2 done; next 4c.
- [ ] **Memory.**
- [ ] **Privacy scan, then commit.** `docs: DXMT in the guest (milestone 4b-2)`.
