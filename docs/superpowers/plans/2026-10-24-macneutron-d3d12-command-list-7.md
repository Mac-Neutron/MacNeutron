# DXMT's D3D12 command lists up to List7 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DXMT's command list answers ID3D12GraphicsCommandList3 to 7 and its device ID3D12Device10, with emulated render
passes, ResolveSubresourceRegion and enhanced barriers, forwarded in the VM, so 3DMark's Steel Nomad gets past
CreateCommandList.

**Architecture:** Everything lives once in the DXMT fork (`build/wine-arm64-src/dxmt`, branch `macneutron`, patches
0124 to 0127), which the macOS PE build, the VM host's native DXMT and the guest's DXMT all compile. Render passes
become OMSetRenderTargets, full clears and ResolveSubresourceRegion calls; enhanced barriers become legacy
D3D12_RESOURCE_BARRIERs that ResourceBarrier classifies. The forwarder (`eac-vm/d3d/rules.txt`, guest `proxy.cpp`)
forwards it all, translating barrier groups in the guest with the same header.

**Tech Stack:** C++17 (DXMT, the guest proxy), Python (gen.py), sh (gates), D3D12 test programs (`dxmt/tests`).

**Spec:** `docs/superpowers/specs/2026-10-24-macneutron-d3d12-command-list-7-design.md`

## Global Constraints

- Branch `vm-mixed-arch-games` in the repo; branch `macneutron` in the fork. Commit locally; push, tag or publish nothing.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- DXMT export, after each fork commit (never `export.sh`, which re-abbreviates every patch):
  `git -C build/wine-arm64-src/dxmt format-patch --zero-commit -N --start-number <N> -1 -o <scratch>`, copy the patch
  into `wine-arm64/patches/dxmt/`, `git -C build/wine-arm64-src/dxmt rev-parse HEAD > build/wine-arm64-src/dxmt.applied`,
  then `(. wine-arm64/lib.sh; tree_series dxmt) > build/wine-arm64-src/dxmt.series`; `tree_mode` must still say
  `applied`. Patch numbers: Task 1 → 0124, Task 3 → 0125, Task 4 → 0126, Task 5 → 0127.
- Caps stay: RaytracingTier, VariableShadingRateTier, MeshShaderTier NOT_SUPPORTED; RenderPassesTier TIER_0;
  RelaxedFormatCastingSupported FALSE. Only `EnhancedBarriersSupported` changes (to TRUE, Task 5).
- Gating scans use `/usr/bin/grep` (the interactive grep is a wrapper that can return 0 silently). No `rtk` here.
- Kill processes by exact pid, never `pkill -f`. Never print SteamIDs or account names; write the home folder as `~`.
- Steam and games: the maintainer starts them, except 3DMark demo, which this work may start
  (`open steam://rungameid/231350`) and stop (the VM's exact pid). Never open MacNeutron.app.
- One-lane check per task; full gates only in Task 6 (memory: lighter task testing).
- Builds that take over ~2 minutes run in the background: `eac-vm/dxmt-native/build.sh`, `wine-arm64/build.sh`,
  `eac-vm/host/build.sh` (with `MACNEUTRON_SIGN_IDENTITY` and `MACNEUTRON_PROVISIONING_PROFILE` set), the builder's
  guest stages and image.

## Spec realization notes

- §5.1 "ResourceBarrier's per-barrier decision moves into one helper both use": realized by translating barrier groups
  into legacy barriers (`LegacyBarriers`, Task 5) that ResourceBarrier itself classifies. Same code path for both, no
  refactor of ResourceBarrier.
- §6.2 "Barrier: a union=Type rule … or a special": gen.py can't express it (the arms are pointers counted by a field
  outside the union; `mn_wm_field.count_offset` is unsigned), so Barrier is `own` in the guest: the same
  `LegacyBarriers`, then the forwarded ResourceBarrier.
- §7 "rows in smoke.cpp": the rows are one test program, `dxmt/tests/d3d12_list7.cpp`, which every lane already knows
  how to build and run: the macOS PE runtime (`build/dxmt-tests`, ref.sh), native DXMT
  (`eac-vm/dxmt-native/build-tests.sh`), the loopback forwarder (`loopback-gate.sh`) and d3d-smoke's VM lanes (which
  compile and run it beside smoke.exe).

## Review Focus

1. A depth-only render pass (no render targets, a depth-stencil with CLEAR), as shadow maps do: the depth is cleared.
   Pinned by Task 4's `depthpass` row.
2. Two render targets, one CLEAR and one PRESERVE, as G-buffers do: only the CLEAR one changes. Task 4's `mrt` row.
3. A RESOLVE ending with two subresource parameters (two half rectangles), forwarded: both halves resolved. Task 4's
   `renderpass` row.
4. A resolve the spec leaves out (MAX mode): nothing happens to the destination and nothing aborts. Task 3's
   `resolveregion` row.
5. Zero barrier groups, or a group of zero barriers: nothing recorded, no crash. Task 5's unit cases.

---

### Task 1: List3 to List7 exist, with logged stubs, and the forwarder forwards them

**Files:**
- Modify (fork): `src/d3d12/d3d12_device.hpp:68` (`MTLD3D12GraphicsCommandList` base),
  `src/d3d12/d3d12_command_list.cpp` (QueryInterface ~303; new methods after `SetViewInstanceMask` ~2119)
- Create: `dxmt/tests/d3d12_list7.cpp`
- Modify: `eac-vm/d3d/rules.txt:101-105` and the "DXMT answers E_NOTIMPL itself" block (~163), `eac-vm/d3d/tests/coverage.py:22`
  (AS_DXMT), `eac-vm/dxmt-native/build-tests.sh:16` (PROGRAMS), `eac-vm/dxmt-native/gate.sh` (ROWS),
  `eac-vm/d3d/loopback-gate.sh` (ROWS), `eac-vm/dxmt/invocations`, `eac-vm/tests/d3d-smoke.sh`

**Interfaces:**
- Produces: `class MTLD3D12GraphicsCommandList : public ID3D12GraphicsCommandList7`; the program
  `d3d12_list7[.exe]` (no arguments) printing one line per row, `<row> ok` or `<row> FAIL <what>`, rows in this
  order: `list4`, then (later tasks) `resolveregion`, `renderpass`, `mrt`, `depthpass`, `suspend`, `barrier`,
  `barrierglobal`, `layout3`. A row's failure never ends the program (each row returns its line).

- [ ] **Step 1: Write the `list4` row in `dxmt/tests/d3d12_list7.cpp`**

Uses `d3d12_common.hpp`'s `Gpu`. The row: for each of `ID3D12GraphicsCommandList4`, `5`, `6`, `7`, CreateCommandList
(DIRECT, the Gpu's allocator) with that IID succeeds; the List7 one answers QueryInterface for List3 to List6; on the
List7 list, these return without a crash: `SetProtectedResourceSession(nullptr)`, `RSSetShadingRate(D3D12_SHADING_RATE_1X1,
nullptr)`, `RSSetShadingRateImage(nullptr)`, `DispatchMesh(1, 1, 1)`, `SetPipelineState1(nullptr)`; then `Close()`
succeeds. Prints `list4 ok`, or `list4 FAIL <step> <hr>`. The header comment lists every row and what it checks, in
the style of `d3d12_clear.cpp`'s.

- [ ] **Step 2: Register the program**

`d3d12_list7` added to: build-tests.sh's PROGRAMS, dxmt-native `gate.sh`'s ROWS, `loopback-gate.sh`'s ROWS,
`invocations` (`d3d12_list7|d3d12_list7.exe||list7`, the format of its neighbours; the last field is its prefix name).
`d3d-smoke.sh`: compile `dxmt/tests/d3d12_list7.cpp` beside smoke.exe for x86-64 (`list7.exe`) and i686
(`list7_32.exe`, `-Wl,--large-address-aware`) with `-I$ROOT/dxmt/tests`; run each after smoke.exe/smoke32.exe into
the same raw file (`timeout -k 10 60 wine \$T/list7.exe`); add `list4` to `guest_checks`. In the 32-bit forwarded
(new WoW64) case, `checks32` stays as it is (D3D12 is refused there). (`dxmt/check.sh` is not a gate of this
work and can't run one program alone: the macOS PE run is ref.sh's, Task 6.)

- [ ] **Step 3: See it fail natively**

Run: `MN_D3D_PROGRAMS=d3d12_list7 sh eac-vm/dxmt-native/build-tests.sh && build/dxmt-native/tests/d3d12_list7`
Expected: `list4 FAIL CreateCommandList(ID3D12GraphicsCommandList4) 80004002`

- [ ] **Step 4: The interfaces in DXMT**

`MTLD3D12GraphicsCommandList` derives `ID3D12GraphicsCommandList7`; QueryInterface answers List3 to List7 with
`ref(this)`. New methods on `MTLD3D12GraphicsCommandListImpl`, each with its `DXMT_STAT_SCOPE("list.<Name>")`:
- Final logged no-ops (spec §6.1), each WARNing once with the `static bool warned` pattern of
  `d3d12_command_queue.cpp:1284`, its text saying why (e.g. `"DispatchMesh: mesh shaders not supported (MeshShaderTier
  NOT_SUPPORTED)"`): SetProtectedResourceSession (no WARN for a null session), InitializeMetaCommand,
  ExecuteMetaCommand, BuildRaytracingAccelerationStructure, EmitRaytracingAccelerationStructurePostbuildInfo,
  CopyRaytracingAccelerationStructure, SetPipelineState1, DispatchRays, RSSetShadingRate (no WARN for 1X1 with null
  combiners), RSSetShadingRateImage (no WARN for null), DispatchMesh.
- Temporary until Tasks 4 and 5: BeginRenderPass, EndRenderPass, Barrier WARN on every call
  (`"BeginRenderPass: not implemented yet"`) and do nothing.

- [ ] **Step 5: The forwarder's rules**

In `rules.txt`, the five `ID3D12GraphicsCommandList3::* refuse` to `…7::* refuse` lines go. In the "DXMT answers
E_NOTIMPL itself" block, a `refuse` line each for the eleven §6.1 methods of Step 4 (List3's, List4's
InitializeMetaCommand to DispatchRays, List5's two, List6's DispatchMesh), and `ID3D12GraphicsCommandList7::Barrier
refuse` (Task 5 makes it `own`). The same eleven go into coverage.py's `AS_DXMT`. BeginRenderPass's:
```
ID3D12GraphicsCommandList4::BeginRenderPass render_targets in count=render_targets_count
ID3D12GraphicsCommandList4::BeginRenderPass depth_stencil in one
struct D3D12_RENDER_PASS_ENDING_ACCESS.UNIONNAME union=Type Resolve=2
struct D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_PARAMETERS.pSubresourceParameters in count=SubresourceCount
```

- [ ] **Step 6: Generator and its tests**

Run: `make -C eac-vm/d3d test` and `python3 -I eac-vm/d3d/tests/coverage.py self-test`
Expected: the unittest run `OK`, `layout: ok`, roundtrip passes, `coverage: ok` self-test.

- [ ] **Step 7: Native and loopback green**

Run (background, then): `sh eac-vm/dxmt-native/build.sh`, then Step 3's command; then
`LIB=$(sh eac-vm/d3d/build-loopback.sh) && MN_D3D_PROGRAMS=d3d12_list7 MN_D3D_PROGRAMS_LIB=$LIB
MN_D3D_PROGRAMS_OUT=build/d3d-loopback/tests sh eac-vm/dxmt-native/build-tests.sh && build/d3d-loopback/tests/d3d12_list7`
Expected: `list4 ok` both times.

- [ ] **Step 8: Commit**

Fork: `git commit -am "d3d12: command lists answer ID3D12GraphicsCommandList3 to 7"`, then the export (0124). Repo: the
patch, `dxmt/tests/d3d12_list7.cpp`, the registrations, rules.txt, coverage.py; message
`eac-vm/d3d, dxmt 0124: command lists answer List3 to List7 (3DMark's Steel Nomad)`.

---

### Task 2: Steel Nomad's calls, traced in a test VM

**Files:** none in the repo; findings to the notes folder
(`~/Library/Application Support/Claude/side-session-notes/local_f06bbaaa-8666-4444-b2bf-a8d2ab5dfc67/3dmark-roguecompany.md`).

**Interfaces:**
- Consumes: Task 1's builds.
- Produces: the list of List4 to List7 and List1 methods Steel Nomad calls (with counts), and its next error, which
  Tasks 3 to 5 keep in mind (a method it calls that a task leaves as a WARN is a finding to report, not to fix here).

- [ ] **Step 1: Rebuild the VM's parts**

The host (`sh eac-vm/host/build.sh`, signing env set: its generated dispatcher has the new rules), then in the builder
(running as in memory "builder out share", `BUILDER_MAC=52:54:00:ea:c0:03`) `sh eac-vm/image/guest-stages.sh dxmt d3d`
and `sh eac-vm/builder.sh ssh 'sh ~/mn-image/build.sh wine'`.
Expected: each exits 0; `build/eac-vm-work/images/wine.erofs` newer than the fork commit.

- [ ] **Step 2: Run the workload**

The notes folder's `steel-nomad-guest.sh` (MACNEUTRON_VM_D3D=forward, trace on), as on 2026-10-23:
`MN_SHARE=$PWD/build/eac-vm-work/sn-share MN_STATE=$PWD/build/eac-vm-work/state/sn.img MN_TPM= MN_AUDIO_OUTPUT=none
MN_RUN_TIMEOUT=300 sh eac-vm/run-guest.sh <notes>/steel-nomad-guest.sh --steam-map "$HOME/Library/Application
Support/Steam/steamapps/common=/mnt/games"`
Expected: `sn-share/traces/sn.txt` no longer ends at `dx_command_buffer.cpp:37`.

- [ ] **Step 3: Read the guest trace**

`/usr/bin/grep -h 'ID3D12GraphicsCommandList[1-7]::\|ID3D12Device[0-9]*::' sn-share/traces/d3d-guest.*.txt`: which new
methods were called, how often, refused or forwarded; and sn.txt's last error. Write both into the notes file.

---

### Task 3: ResolveSubresourceRegion

**Files:**
- Modify (fork): `src/d3d12/d3d12_command_list.cpp:2110` (ResolveSubresourceRegion), `src/d3d12/d3d12_command_allocator.hpp`
  (a scratch-texture list) and `.cpp:120` (Reset clears it)
- Modify: `dxmt/tests/d3d12_list7.cpp`, `eac-vm/d3d/rules.txt:157`, `eac-vm/tests/d3d-smoke.sh` (`resolveregion` in
  `guest_checks`)

**Interfaces:**
- Consumes: `ResolveSubresource` (the existing resolve pass), `allocator_->EncodeBlitCommand<wmtcmd_blit_copy_from_texture_to_texture>()`
  as `CopyTextureRegion` uses it (~854).
- Produces: `ResolveSubresourceRegion(ID3D12Resource *pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
  ID3D12Resource *pSrcResource, UINT SrcSubresource, D3D12_RECT *pSrcRect, DXGI_FORMAT Format,
  D3D12_RESOLVE_MODE ResolveMode)` doing spec §4.1 (Task 4 calls it per subresource parameter);
  `void MTLD3D12CommandAllocatorImpl::KeepTexture(Rc<Texture> texture)`, which keeps a scratch texture alive until
  Reset.

- [ ] **Step 1: The `resolveregion` row**

A 4x4 R8G8B8A8_UNORM 4x MSAA render target `src` cleared red (1,0,0,1); a 4x4 single-sample render target `dst`
cleared blue (0,0,1,1); both then in RESOLVE_SOURCE/RESOLVE_DEST. `ResolveSubresourceRegion(dst, 0, 2, 2, src, 0,
&{0,0,2,2}, R8G8B8A8_UNORM, AVERAGE)`, then `ResolveSubresourceRegion(dst, 0, 0, 0, src, 0, &{0,0,1,1},
R8G8B8A8_UNORM, D3D12_RESOLVE_MODE_MAX)` (Review Focus 4). dst copied to a readback buffer. Asserts: texels (2,2),
(3,2), (2,3), (3,3) are `ff0000ff` (RGBA bytes 255,0,0,255); (0,0), (1,1), (3,0), (0,3) are blue. Prints
`resolveregion ok` or the first wrong texel.

- [ ] **Step 2: See it fail**

Run: Task 1 Step 3's command. Expected: the program aborts at `resolveregion` (IMPLEMENT_ME) — the failing run.

- [ ] **Step 3: Implement**

Spec §4.1. Whole subresource (`!pSrcRect`, or the rect is the source's full width and height) at DstX = DstY = 0:
`ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format)`. Otherwise: a scratch
single-sample texture of the source's size and Metal format (private storage, usage render target | shader read),
`KeepTexture`'d, resolved into by a resolve pass as `ResolveSubresource` builds one, then a blit copy of the rect to
(DstX, DstY) of the destination's level and slice. Modes: AVERAGE colour as above; DECOMPRESS, ENCODE/DECODE_SAMPLER_FEEDBACK
nothing; MIN, MAX and any depth/stencil format WARN once and return, with
`// ponytail: no depth resolve or colour MIN/MAX (the resolve pass has a colour attachment only); add when a game needs it`.

- [ ] **Step 4: Forwarder**

`rules.txt`: `ID3D12GraphicsCommandList1::ResolveSubresourceRegion refuse` goes; add
`ID3D12GraphicsCommandList1::ResolveSubresourceRegion src_rect in one`. Run `make -C eac-vm/d3d test`. Expected: OK.

- [ ] **Step 5: Green**

Run: Task 1 Step 7 (native build, native run, loopback run). Expected: `list4 ok`, `resolveregion ok` both times.

- [ ] **Step 6: Commit**

Fork commit `d3d12: ResolveSubresourceRegion` + export (0125); repo commit with the patch, the row, rules.txt,
d3d-smoke.sh.

---

### Task 4: Render passes

**Files:**
- Modify (fork): `src/d3d12/d3d12_command_list.cpp` (BeginRenderPass, EndRenderPass from Task 1; Reset ~295 clears
  the pass state)
- Modify: `dxmt/tests/d3d12_list7.cpp`, `eac-vm/tests/d3d-smoke.sh` (`renderpass mrt depthpass suspend` in
  `guest_checks`)

**Interfaces:**
- Consumes: Task 3's ResolveSubresourceRegion; the list's `OMSetRenderTargets`, `ClearRenderTargetView`,
  `ClearDepthStencilView`.
- Produces: in `MTLD3D12GraphicsCommandListImpl`,
  `struct PendingResolve { ID3D12Resource *src, *dst; DXGI_FORMAT format; D3D12_RESOLVE_MODE mode;
  std::vector<D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS> subresources; };`,
  `std::vector<PendingResolve> pass_resolves_;` and `D3D12_RENDER_PASS_FLAGS pass_flags_;`, both cleared by Reset.

- [ ] **Step 1: The rows**

Each makes its targets, records into the Gpu's list (and, for `suspend`, a second list from the same allocator),
submits, reads back through a readback buffer:
- `renderpass`: a 4x4 4x MSAA RGBA8 target (pre-cleared blue outside the pass), a 4x4 single-sample `dst`
  (pre-cleared black), a 4x4 4x MSAA D32_FLOAT depth target. BeginRenderPass: the RT CLEAR green (0,1,0,1), its ending
  RESOLVE into dst with two subresource parameters (SrcRect {0,0,2,4} to (0,0), SrcRect {2,0,4,4} to (2,0); Review
  Focus 3); the depth CLEAR 0.5 with DISCARD endings, stencil NO_ACCESS both ways. EndRenderPass. Assert every dst
  texel green.
- `mrt` (Review Focus 2): two single-sample 4x4 RGBA8 targets pre-cleared blue; RT0 CLEAR green, RT1 PRESERVE, both
  ending PRESERVE. Assert RT0 all green, RT1 all blue.
- `depthpass` (Review Focus 1): no render targets; a single-sample 4x4 D32_FLOAT target pre-cleared 1.0; depth CLEAR
  0.25, PRESERVE ending. Copied out (CopyTextureRegion to a footprint), every value 0.25f.
- `suspend`: the 4x MSAA target pre-cleared blue; list A: Begin with CLEAR red, ending RESOLVE into dst, flags
  SUSPENDING_PASS; End. List B: Begin with CLEAR green, the same RESOLVE, flags RESUMING_PASS; End. Both lists in one
  ExecuteCommandLists. Assert every dst texel red (a list that ignored the flags gives green; one that swapped them,
  blue).

- [ ] **Step 2: See them fail**

Run: Task 1 Step 3's command. Expected: `renderpass FAIL …` (dst black), `mrt FAIL`, `depthpass FAIL`, `suspend FAIL`.

- [ ] **Step 3: Check the rows against D3DMetal once**

`make dxmt-tests`, then D3DMetal as check.sh's `run` drives it, on a clone of the frozen reference tool (never the
tool itself): `R="$HOME/Library/Application Support/MacNeutron Reference/rosetta-tool"; X=<scratch>/d3dmetal; mkdir -p
$X/compat; cp -cR "$(cd "$R" && pwd -P)" $X/ref; env STEAM_COMPAT_DATA_PATH=$X/compat SteamAppId=0
MACNEUTRON_GRAPHICS=d3dmetal MACNEUTRON_NO_STEAM_BRIDGE=1 MACNEUTRON_NO_METALFX=1 $X/ref/bin/macneutron launch
waitforexitandrun $PWD/build/dxmt-tests/d3d12_list7.exe`, with a 120 s watchdog as `run` has. Expected: D3DMetal
prints `ok` on `resolveregion`, `renderpass`, `mrt`, `depthpass` and `suspend` (the rows say what D3D12 does). A
D3DMetal FAIL means the row is wrong, unless D3DMetal lacks the feature: note which in the commit message.

- [ ] **Step 4: Implement**

Spec §4.2. BeginRenderPass: `pass_flags_ = Flags`, `pass_resolves_` refilled from the ending accesses (RT descs, then
the depth's and stencil's), copying each subresource array; unless RESUMING_PASS, a CLEAR beginning access is a full
`ClearRenderTargetView(cpuDescriptor, Clear.ClearValue.Color, 0, nullptr)`, or one `ClearDepthStencilView` with
D3D12_CLEAR_FLAG_DEPTH and/or _STENCIL from the two accesses; then `OMSetRenderTargets(NumRenderTargets, handles,
FALSE, pDepthStencil && !(both planes NO_ACCESS) ? &pDepthStencil->cpuDescriptor : nullptr)`. EndRenderPass: unless
`pass_flags_` has SUSPENDING_PASS, one ResolveSubresourceRegion per kept subresource parameter
(`&SrcRect`, Dst/Src subresources, DstX/DstY, the access's Format and ResolveMode); then clear the kept state. Other
access types do nothing.

- [ ] **Step 5: Green**

Run: Task 1 Step 7. Expected: every row so far `ok`, native and loopback.

- [ ] **Step 6: Commit**

Fork commit `d3d12: render passes, emulated` + export (0126); repo commit with the patch, the rows, d3d-smoke.sh.

---

### Task 5: Enhanced barriers, Device9 and Device10

**Files:**
- Create (fork): `src/d3d12/d3d12_barrier_legacy.hpp` (header-only; d3d12.h and `<vector>` only)
- Modify (fork): `src/d3d12/d3d12_command_list.cpp` (Barrier), `src/d3d12/d3d12_device.hpp:283`,
  `src/d3d12/d3d12_device.cpp` (QueryInterface ~427, OPTIONS12 ~850, the Device9/10 methods after `CreateReservedResource1` ~1737)
- Create: `eac-vm/d3d/tests/barrier-test.cpp`
- Modify: `eac-vm/d3d/Makefile` (a `barrier-test` target, in `test`), `eac-vm/d3d/rules.txt`, `eac-vm/d3d/guest/proxy.cpp`
  (`barrier()` and its `code[]` entry), `eac-vm/d3d/tests/coverage.py` (AS_DXMT), `dxmt/tests/d3d12_list7.cpp`,
  `eac-vm/tests/d3d-smoke.sh` (`barrier barrierglobal layout3` in `guest_checks`)

**Interfaces:**
- Produces, in `d3d12_barrier_legacy.hpp` (namespace `dxmt`):
  - `inline D3D12_RESOURCE_STATES LegacyState(D3D12_BARRIER_ACCESS access, bool *known)`: spec §5.1's table; `*known`
    false for any bit outside it.
  - `inline void LegacyBarriers(UINT32 group_count, const D3D12_BARRIER_GROUP *groups,
    std::vector<D3D12_RESOURCE_BARRIER> &out)`: clears `out`, then appends the algorithm below.
  - `inline D3D12_RESOURCE_STATES InitialState(D3D12_BARRIER_LAYOUT layout, D3D12_HEAP_TYPE heap)`: spec §5.2's
    mapping.
- Consumed by DXMT's Barrier and Device10 creates, and by the guest's `barrier()` (the guest build already has
  `-I$DXMT/d3d12`).

`LegacyBarriers`, per barrier (the decisions spec §5.1 makes; ResourceBarrier classifies what comes out):

| Barrier | Appended |
|---|---|
| any, SyncBefore NONE or AccessBefore NO_ACCESS | nothing |
| global, LegacyState(AccessBefore) read-only (ReadOnly's set) | nothing |
| global, AccessBefore exactly UNORDERED_ACCESS | UAV barrier, pResource null |
| global, otherwise | aliasing barrier, both resources null |
| texture/buffer, either access unknown | aliasing barrier, both resources null |
| texture/buffer, UNORDERED_ACCESS to UNORDERED_ACCESS | UAV barrier on pResource |
| texture/buffer, otherwise | transition on pResource, ALL_SUBRESOURCES, LegacyState(before) to LegacyState(after) (AccessAfter NO_ACCESS: COMMON) |

- [ ] **Step 1: The unit test `eac-vm/d3d/tests/barrier-test.cpp`**

`assert`-based, compiled natively like `formats-test` (Makefile target `barrier-test`: `-include $(NT)/native-prelude.h
-I$(NT)/include -I$(I)/d3d12 -I$(I)/windows -I$(I)/directx -I$(I) -I$(DXMT)/d3d12`, added to `test`). Cases, each
one group of one barrier unless said (R is a fake `(ID3D12Resource *)0x1000`):
1. buffer UAV→COPY_SOURCE, SyncBefore ALL_SHADING → one transition R, UNORDERED_ACCESS→COPY_SOURCE.
2. texture RENDER_TARGET→SHADER_RESOURCE → transition RENDER_TARGET→(PIXEL_SHADER_RESOURCE|NON_PIXEL_SHADER_RESOURCE).
3. SyncBefore NONE, AccessBefore NO_ACCESS → nothing. 4. AccessBefore NO_ACCESS with SyncBefore ALL → nothing.
5. global UAV→SHADER_RESOURCE → UAV, null. 6. global RENDER_TARGET→SHADER_RESOURCE → aliasing, null/null.
7. global SHADER_RESOURCE→COPY_SOURCE → nothing. 8. buffer UAV→UAV → UAV on R.
9. texture RAYTRACING_ACCELERATION_STRUCTURE_READ→SHADER_RESOURCE → aliasing, null/null.
10. texture RENDER_TARGET→NO_ACCESS → transition RENDER_TARGET→COMMON. 11. buffer COMMON→COPY_DEST → transition COMMON→COPY_DEST.
12. zero groups; and one group with NumBarriers 0 → nothing, `out` emptied of an earlier entry (Review Focus 5).
13. InitialState: UNDEFINED+UPLOAD → GENERIC_READ; UNDEFINED+READBACK → COPY_DEST; UNDEFINED+DEFAULT → COMMON;
    RENDER_TARGET+DEFAULT → RENDER_TARGET; DIRECT_QUEUE_SHADER_RESOURCE+DEFAULT → PIXEL|NON_PIXEL_SHADER_RESOURCE;
    COMPUTE_QUEUE_UNORDERED_ACCESS+DEFAULT → UNORDERED_ACCESS.
Prints `barrier-test: ok`.

- [ ] **Step 2: The rows in d3d12_list7.cpp**

- `barrier`: a 256-byte DEFAULT buffer with ALLOW_UNORDERED_ACCESS; ClearUnorderedAccessViewUint to 0x1234 (a
  shader-visible and a CPU CBV_SRV_UAV heap, R32_UINT raw-less view of 64 elements); a List7 `Barrier` with one buffer
  barrier (SyncBefore ALL_SHADING, SyncAfter COPY, UNORDERED_ACCESS→COPY_SOURCE); CopyBufferRegion to a readback
  buffer. Every word 0x1234.
- `barrierglobal`: the same with one global barrier (same syncs and accesses) instead.
- `layout3`: OPTIONS12's EnhancedBarriersSupported is TRUE; the device answers ID3D12Device10; CreateCommittedResource3
  (UPLOAD heap, a 4096-byte buffer DESC1, layout UNDEFINED, one castable format R32_UINT) succeeds; Map of it
  succeeds.

- [ ] **Step 3: See them fail**

Run: `make -C eac-vm/d3d barrier-test` (fails to compile: no header) and Task 1 Step 3's command (`layout3 FAIL
OPTIONS12`).

- [ ] **Step 4: The header and DXMT**

The header as in Interfaces. DXMT: `Barrier` = `LegacyBarriers(count, groups, legacy_barriers_)` (a member scratch
vector) then `ResourceBarrier(legacy_barriers_.size(), legacy_barriers_.data())`. Device: `MTLD3D12Device :
ID3D12Device10`, QueryInterface answers 9 and 10; CreateCommandQueue1 → CreateCommandQueue; CreateShaderCacheSession,
ShaderCacheControl → E_NOTIMPL; CreateCommittedResource3 → CreateCommittedResource2 with
`InitialState(layout, pHeapProperties->Type)`; CreatePlacedResource2 → CreatePlacedResource1 with the heap's
`GetDesc().Properties.Type`; CreateReservedResource2 → E_NOTIMPL; a non-null protected session → E_NOTIMPL; castable
formats WARN once and are ignored. OPTIONS12: `EnhancedBarriersSupported = TRUE`.

- [ ] **Step 5: The forwarder**

`rules.txt`: `ID3D12Device9::* refuse` and `ID3D12Device10::* refuse` go; `ID3D12Device9::CreateShaderCacheSession
refuse`, `ID3D12Device9::ShaderCacheControl refuse` (both into AS_DXMT too);
`ID3D12Device10::<each of the three> castable_formats in count=castable_formats_count`;
`ID3D12GraphicsCommandList7::Barrier refuse` becomes `ID3D12GraphicsCommandList7::Barrier own`. `proxy.cpp`:
`static void STDMETHODCALLTYPE barrier(mn_proxy *This, UINT32 count, const D3D12_BARRIER_GROUP *groups)`, a
thread-local vector through `LegacyBarriers`, then `((ID3D12GraphicsCommandList *)This)->ResourceBarrier(...)`; its
entry `{"ID3D12GraphicsCommandList7::Barrier", (const void *)barrier}` in `code[]`.

- [ ] **Step 6: Green**

Run: `make -C eac-vm/d3d test barrier-test`, then Task 1 Step 7. Expected: `barrier-test: ok`, the generator tests
OK, every row `ok` native and loopback.

- [ ] **Step 7: Commit**

Fork commit `d3d12: enhanced barriers, ID3D12Device9 and 10` + export (0127); repo commit with the patch, the test,
Makefile, rules.txt, proxy.cpp, coverage.py, the rows, d3d-smoke.sh.

---

### Task 6: Full gates, Steel Nomad, notes

**Files:**
- Modify: the spec's Status line; the notes file; memory (`dxmt-d3d12.md`, a line for the new interfaces).

- [ ] **Step 1: The macOS PE build and its reference**

Run (background): `sh wine-arm64/build.sh`, `make dxmt-tests`, then `sh eac-vm/dxmt/ref.sh` (all programs; the
macOS run of our PE DXMT). Expected: exit 0; `build/eac-vm-work/share/dxmt-ref/d3d12_list7.1.txt` has every row `ok`.

- [ ] **Step 2: The Mac gates**

Run: `sh eac-vm/dxmt-native/gate.sh`, `sh eac-vm/d3d/loopback-gate.sh`, `make -C eac-vm/d3d test dispatch-test
formats-test barrier-test`. Expected: `dxmt-native gate: all <n> programs passed`, `d3d-loopback gate: all <n>
programs passed`, the make targets OK.

- [ ] **Step 3: The VM gates**

Rebuild as Task 2 Step 1, then `sh eac-vm/dxmt/share.sh`, `sh eac-vm/tests/d3d-smoke.sh`, and again with
`MN_WOW64=new`. Expected: `d3d-smoke: ok` both times (the new rows in forward and guest, 64- and 32-bit guest).

- [ ] **Step 4: Steel Nomad**

Task 2 Step 2's run. Expected: it runs to its result file (`sn-share/traces/sn-result.json` with a score), or its
next blocker is named from sn.txt and the traces. Then, with the maintainer told first, `open
steam://rungameid/231350`; the maintainer clicks Run on Steel Nomad; read `~/Library/Logs/MacNeutron/steam-231350.log`
and the run's traces; stop the VM by its exact pid if it hangs.

- [ ] **Step 5: Notes and commit**

The spec's Status: implemented 2026-10-2x on vm-mixed-arch-games, with what §1's "Done when" showed. The notes file:
Steel Nomad's outcome, the D3DMetal comparison, what stays a WARN. Commit the spec and plan updates.
