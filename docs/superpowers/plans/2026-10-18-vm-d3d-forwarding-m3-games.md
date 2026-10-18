# Direct3D forwarding M3: games Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real D3D12 games run in the VM through forwarding: their swapchains present on native DXMT into the VM
window, paced in the guest; XeSS and DLSS run on MetalFX; each game's shader cache, recordings and settings reach
native DXMT; native DXMT ships in the app's VM helpers. SMITE 2 and Palworld play this way, and their traces show no
call the forwarding doesn't carry.

**Architecture:** M2's proxies, records and dispatcher stay as they are. Native DXMT reaches the VM window through the
same stand-in `winemac.so` guest DXMT uses today (the one window: every HWND is it), and it learns the game's window
and displays from what the guest pushes (two new family ops) through window-system hooks in its native build. The
window's own changes (full screen, resizes, minimising) happen in the guest, in `mnd3d.dll`, with DXMT's own Win32
window code. Frame pacing is the guest's: each swapchain has a mirrored fence its proxy signals after every `Present`,
which paces `Present` and backs the frame-latency object, so no host call waits for a drawable on a vCPU when the game
keeps to its latency. The upscalers stay guest PE DLLs and reach native DXMT through the private interfaces M2 already
forwards. Per game, `macneutron-vm` points native DXMT's shader cache and recordings at the game's VM state folder,
and the launcher replays the recordings natively before the VM boots.

**Tech Stack:** C (guest unix side, host dispatcher), C++17 (guest proxies and specials, host stubs, the native replay
tool), Python 3 (the generator), Swift (the launcher), DXMT's fork (meson, native and PE), POSIX sh.

**Spec:** `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md` (§4.7's latency object, §4.8,
§4.11, §4.12, §4.13, §6, §7, §8 M3). M2: research README §19 (its open items are this plan's inputs).

## Global Constraints

- Gate (spec §8 M3): SMITE 2 and Palworld play in the VM through forwarding, with XeSS and DLSS on MetalFX; the
  coverage trace (`MACNEUTRON_VM_D3D_TRACE=1`, `coverage.py`) shows no unforwarded call. Before the games, every D3D12
  row of the 4b-2 gate (`eac-vm/dxmt/invocations`: the 19, 4 XeSS, 5 DLSS) and the 4c gate's `d3d12_clear` run through
  forwarding as they run on macOS (`compare.py`'s rule; the 4c gate's own checks).
- No host call waits for the GPU or a drawable on a vCPU: waits happen in the guest (M2's transport constraint).
- One VM window per VM; full-screen modes stay emulated in the guest (Wine's EmulateModeset); the host presents at the
  buffer's size and the window scales it (spec §4.8).
- `MACNEUTRON_VM_D3D=forward` stays opt-in (the default is `guest`) until spec §9; D3D11 and D3D10 through forwarding
  are M4's, so a forwarded game that makes a D3D11 device fails it (README §19).
- Games run only with the maintainer's go-ahead (they start on the maintainer's Steam account) and never while the
  maintainer is playing; we never start or quit Mac Steam; EAC: comply, never bypass. A game's files are read where
  they're installed and never copied (`amd_fidelityfx_dx12.dll` included); Valve binaries are never committed.
- Benches and reference runs run once (`MN_REF_RUNS=1`). The VM keeps every core and its RAM. Apple's vGIC stays.
- DXMT fork changes: native-only unless a task says so, one commit each in `build/wine-arm64-src/dxmt` (the
  maintainer as author, `Assisted-by: Claude Code: claude-opus-5-5`, as M2's), exported with `git format-patch -N`
  into `wine-arm64/patches/dxmt/` (0081 on), and checked by `git am` of the whole series onto `DXMT_COMMIT`
  (`. dxmt/pins`) in a scratch worktree. The macOS runtime's PE build must not change behaviour.
- `eac-vm/host/build.sh` runs with `MACNEUTRON_SIGN_IDENTITY` and `MACNEUTRON_PROVISIONING_PROFILE` set (an ad hoc host
  hangs every boot on a keychain prompt); the builder starts with `VM_SHARES="out=$PWD/build/eac-vm-work/images"`
  for image builds. The VM host and the native DXMT it loads (its copy in `build/eac-vm-work/dxmt`) are rebuilt
  together: a struct shared through `win32_native_event.h` must match.
- Scripts use `/usr/bin/grep`; commits end with the session's trailer lines; a privacy scan before committing docs; no
  push, tag or publish without the maintainer's yes. Testing per task is one lane (lighter-task-testing); the full
  gates run in Task 10.

## Rulings made writing this plan

- **The upscalers stay in the guest** (spec §4.11 says their entry points go to native host builds): `nvngx.dll`,
  `nvapi64.dll` and Wine's `libxess.dll` already reach DXMT only through `IMTLD3D12DeviceExt` and
  `IMTLD3D12CommandListExt`, which M2 forwards as companions; MetalFX runs natively behind them. A native build would
  forward the NGX SDK's own exports and parameter maps instead, for no saving: their per-frame work is one
  `TemporalUpscale` record. Cost if wrong: nvngx's parameter handling under FEX each frame.
- **Frame latency is the guest's, on a mirrored fence** (spec §4.7: "mirrored the same way"): the swapchain's proxy
  signals a fence of its own after each `Present`, on the swapchain's queue; DXMT releases its own semaphore at the
  same point (the present's command buffer done), which nothing in the guest can see without a fork change.
- **The window and displays come from the guest** (spec §4.8: "a swapchain is created from the game's window in the
  guest"): native DXMT's window-system calls answer from what the guest last pushed, and its window actions do
  nothing natively; the guest makes the changes with DXMT's own `wsi_window_win32.cpp` and `wsi_monitor_win32.cpp`,
  built into `mnd3d.dll`.
- **The precache replays natively** (spec §4.12): `dxmt-replay` built against native DXMT the way M1 builds DXMT's
  test programs, run by the launcher before the VM boots, into the game's state folder.
- **`WriteToSubresource` and `ReadFromSubresource` get code only if a game's trace shows them** (Task 10): native DXMT
  implements them for CPU-visible textures only, which fork 0077's heaps don't place.
- **DXMT's `IMPLEMENT_ME` throws in the native build** and the generated stubs catch it (`E_NOTIMPL`, logged): its
  conditional paths (README §19) must not abort the VM host. The seven unconditional swapchain ones are answered in
  the guest instead.

## Review Focus

- A game that changes resolution in its menu or whose window is resized (`ResizeBuffers(0, 0)` after `WM_SIZE`): the
  buffers take the guest window's new client size, never the VM window's or the native default 1024x1024 (Task 2).
- Alt-tab, minimising and focus loss in the guest: `Present` while minimised returns `DXGI_STATUS_OCCLUDED` as DXMT
  does in the guest and never blocks; the window state the host answers is the latest pushed (Task 2).
- A game that never asks for the waitable object and presents faster than the GPU: the guest holds frames in flight
  to the maximum latency, and the host's `Present` takes no longer than on macOS (Task 4).
- DLSS or XeSS turned on in a game's settings after its device exists (the rows load the upscaler after the device):
  the upscaler's refusals reach the game as failures, not as features made (Task 7).
- A second process in the same VM (a launcher, then the game): each process's window state, latency fences and NVEXT
  answer are its own context's (Tasks 2, 4, 6).

---

### Task 1: Presenting on the VM window (fork 0081)

**Files:**
- Fork: `src/winemetal/unix/winemetal_unix.c` (`_CreateMetalViewFromHWND`: NULL, not a crash, when there's no window
  data or view; native `presentDrawable` and `presentDrawableAfterMinimumDuration` call a present hook first),
  `src/winemetal/winemetal.h` (`WMTSetPresentHook`)
- Modify: `eac-vm/d3d/host/dispatch.c` (`mn_d3d_load`), `eac-vm/metal/wm/host/present.m`, `eac-vm/dxmt/present-gate.sh`
- Test: `eac-vm/dxmt/present-gate.sh forward`

**Interfaces:**
- Produces: `void WMTSetPresentHook(void (*hook)(void *cmdbuf, void *drawable))` (native winemetal, fork 0081);
  `void mn_wm_present_native(void *cmdbuf, void *drawable)` (present.m: slots 47/48's probe, count and stats for a
  native present; the count goes to `mn_d3d_current_ctx()`'s context, or a process-wide one off a stub call);
  `present-gate.sh forward` runs `d3d12_clear` and SMITE 2 with `MACNEUTRON_VM_D3D=forward` (`present_loop` is D3D11:
  it stays in guest mode).

- [ ] **Step 1:** Add `present-gate.sh forward` (the guest script exports `MACNEUTRON_VM_D3D=forward` and sources
  `prefix.sh` after wineboot; SMITE 2's run passes `MACNEUTRON_VM_D3D=forward` through `run-guest.sh`).
- [ ] **Step 2:** Run `sh eac-vm/dxmt/present-gate.sh forward` (SMITE 2 skips unless Mac Steam runs).
  Expected RED: `d3d12_clear` FAILs (the VM host aborts at the swapchain's view, or counts 0 drawables).
- [ ] **Step 3:** `mn_d3d_load` promotes the stand-in to global symbols before loading native DXMT:
  `dlopen(<metal-host dir>/winemac.so, RTLD_NOLOAD | RTLD_GLOBAL)` (native winemetal finds `macdrv_functions` with
  `dlsym(RTLD_DEFAULT)`); a failure is `mn-d3d: error: native DXMT: no window (<dlerror>)` and every creation answers
  `E_FAIL`. Fork 0081 as above; `mn_d3d_load` registers `mn_wm_present_native` with `WMTSetPresentHook`.
- [ ] **Step 4:** Rebuild native DXMT, the host (signing env), and run Step 2 again. Expected: `d3d12_clear: presented
  600/600 frames`, probes green at least 80, 600 drawables counted, `host: no error lines` (its `resize size` and
  `resize waitable` lines are Tasks 2 and 4's).
- [ ] **Step 5:** `git am` check of the series; commit (fork patch, host, present.m, gate).

### Task 2: The window and displays the guest pushes (fork 0082)

**Files:**
- Fork: `src/util/wsi_window_headless.cpp`, `src/util/wsi_monitor_headless.cpp` (native: answers from hooks when set,
  else today's), `src/winemetal/winemetal.h` (`struct WMTWsiHooks`, `WMTSetWsiHooks`)
- Create: `eac-vm/d3d/host/window.c` (each context's windows and displays; the hooks)
- Modify: `eac-vm/d3d/mn_d3d.h`, `eac-vm/d3d/guest/proxy.cpp`, `eac-vm/d3d/guest/unix.c`, `eac-vm/d3d/host/dispatch.c`,
  `eac-vm/d3d/rules.txt` (the swapchain creations and resizes become specials that push first), `eac-vm/d3d/Makefile`,
  `eac-vm/d3d/build-loopback.sh`, `eac-vm/host/build.sh` (window.c)
- Test: `eac-vm/d3d/tests/dispatch-test.c` (window case), `eac-vm/d3d/tests/smoke.cpp` (minimised line),
  `present-gate.sh forward`

**Interfaces:**
- Produces (mn_d3d.h): `MN_D3D_OP_WINDOW = 7` with `struct mn_d3d_window { uint64_t hwnd, monitor; uint32_t width,
  height, flags; uint32_t pad; }` (flags: 1 minimised, 2 foreground, 4 a window); `MN_D3D_OP_DISPLAYS = 8` with
  `uint32_t count`, then `count` of `struct mn_d3d_display { uint64_t monitor; int32_t left, top, right, bottom;
  uint16_t name[32]; uint32_t modes; uint32_t pad; }` each followed by `modes` of `struct mn_d3d_mode { uint32_t width,
  height, refresh_num, refresh_den; }`, the current mode first.
- Produces (fork): `WMTSetWsiHooks(const struct WMTWsiHooks *)` whose members answer `getWindowSize`, `isMinimized`,
  `isForeground`, `isWindow`, `getWindowMonitor`, `enumMonitors`, `getDisplayName`, `getDesktopCoordinates`,
  `getDisplayMode`, `getCurrentDisplayMode` and `getDesktopDisplayMode`; with hooks set the window actions
  (`resizeWindow`, `setWindowMode`, `enterFullscreenMode`, `leaveFullscreenMode`, `restoreDisplayMode`,
  `updateFullscreenWindow`, `minimizeWindow`) return success and do nothing.
- Guest: before every forwarded `CreateSwapChain`, `CreateSwapChainForHwnd`, `ResizeBuffers`, `ResizeBuffers1`,
  `ResizeTarget` and `SetFullscreenState`, and before a `Present` whose window state differs from the last pushed, the
  proxy pushes op 7 for its HWND (`GetClientRect`, `IsIconic`, `GetForegroundWindow`, `MonitorFromWindow`); the first
  `CreateDXGIFactory*` pushes op 8 (`EnumDisplayMonitors`, `GetMonitorInfoW`, `EnumDisplaySettingsW`).
- Host: `window.c` keeps the latest of each by `(ctx_id, hwnd)` and `(ctx_id, monitor)`; a hook called off a stub call
  answers the HWND's latest in any context.

- [ ] **Step 1:** dispatch-test's window case: op 7 for HWND 0x1234 (800x600, a window) and op 8 for one monitor
  0x10001 (0,0-1920x1080, "\\.\DISPLAY1", modes 1920x1080@60/1, 1280x720@60/1); then `IDXGIFactory1::EnumAdapters1`,
  `IDXGIAdapter::EnumOutputs(0)`, `IDXGIOutput::GetDesc` answers DesktopCoordinates 0,0,1920,1080 and
  `GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM)` 2 modes; op 7 from context 2 for the same HWND at 640x480: the
  window hook answers 800x600 during context 1's call and 640x480 during context 2's. smoke.cpp's `minimised` line:
  a swapchain on a Wine window, the window minimised (`ShowWindow(SW_MINIMIZE)`), then `Present` answers
  `DXGI_STATUS_OCCLUDED` (0x087A0001) within 100 ms ("ok" or what it answered, and how long). Run `make -s -C
  eac-vm/d3d dispatch-test`. Expected RED: the output's coordinates are the headless ones.
- [ ] **Step 2:** Implement (fork 0082, window.c, the ops, the guest pushes). Expected: dispatch-test ok; `make -s -C
  eac-vm/d3d test`, `sh eac-vm/d3d/loopback-gate.sh` (19/19) still pass.
- [ ] **Step 3:** Rebuild native DXMT, the host, the d3d guest stage and `wine.erofs`; run `present-gate.sh forward`
  and `sh eac-vm/tests/d3d-smoke.sh`. Expected: `d3d12_clear`'s `resize size` line as on macOS; `minimised ok`
  forwarded.
- [ ] **Step 4:** Commit.

### Task 3: Full screen and the swapchain's own methods, in the guest (fork 0083)

**Files:**
- Modify: `eac-vm/d3d/build-guest.sh` (`mnd3d.dll` builds DXMT's `src/util/wsi_window_win32.cpp`,
  `wsi_monitor_win32.cpp` and what they need), `eac-vm/d3d/guest/proxy.cpp`, `eac-vm/d3d/rules.txt`, `eac-vm/d3d/gen.py`
  (stubs catch `dxmt::NotImplemented`)
- Fork: `src/d3d12/d3d12_device.hpp` (natively, `IMPLEMENT_ME` throws `dxmt::NotImplemented` instead of `abort()`)
- Test: `eac-vm/d3d/tests/smoke.cpp` (swapchain line), dispatch-test (notimpl case), `present-gate.sh forward`
  (`d3d12_fullscreen` row added in forward mode)

**Interfaces:**
- Guest specials: `IDXGISwapChain::SetFullscreenState`, `GetFullscreenState` and `ResizeTarget` do the window's part
  with DXMT's Win32 wsi in the guest (as guest DXMT's swapchain does), then push op 7 and forward the call.
  `IDXGISwapChain1::GetRestrictToOutput` answers `*ppRestrictToOutput = nullptr`, `S_OK`; `SetBackgroundColor` keeps
  the colour, `GetBackgroundColor` answers it (`{0, 0, 0, 1}` until set); `SetRotation(DXGI_MODE_ROTATION_IDENTITY)`
  is `S_OK`, any other `DXGI_ERROR_INVALID_CALL`; `GetRotation` answers `DXGI_MODE_ROTATION_IDENTITY`;
  `IDXGISwapChain2::SetSourceSize(w, h)` keeps a size no larger than the buffers (else `E_INVALIDARG`), and
  `GetSourceSize` answers it, or the buffers' size.
- Host stubs: a call whose DXMT path throws `dxmt::NotImplemented` logs `mn-d3d: error: <method>: not in DXMT` once a
  method and answers `E_NOTIMPL` (void: nothing).

- [ ] **Step 1:** smoke.cpp's `swapchain` line (a swapchain on a Wine window: the seven answers above, "ok" or the
  first wrong one); dispatch-test's notimpl case (`CreateRenderTargetView` on a buffer, DXMT's `IMPLEMENT_ME`: the test
  goes on and the log has the line); `d3d12_fullscreen` in `present-gate.sh forward` against its guest-mode lines.
  Expected RED: dispatch-test aborts; the VM host aborts on smoke's swapchain line; `d3d12_fullscreen`'s lines differ.
- [ ] **Step 2:** Implement. Expected: dispatch-test ok, `d3d-smoke: ok` with `swapchain ok` in forward mode,
  `d3d12_fullscreen`'s lines as in guest mode.
- [ ] **Step 3:** Commit.

### Task 4: Frame latency and pacing, in the guest

**Files:**
- Modify: `eac-vm/d3d/guest/proxy.cpp`, `eac-vm/d3d/rules.txt` (`Present`, `Present1`, the latency methods and
  `ResizeBuffers*` become specials)
- Test: `eac-vm/d3d/tests/smoke.cpp` (latency line), `present-gate.sh forward` (`d3d12_clear`'s `resize waitable` line,
  host `Present` time)

**Interfaces:**
- Consumes: M2's fence proxies (`create_fence`, the mirror, `on_event`).
- A swapchain proxy holds a fence F made on its queue's device, the frames presented n, and the maximum latency L
  (the host's `GetMaximumFrameLatency` after the creation). `Present`/`Present1`: without
  `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`, wait in the guest until F's mirror reaches n + 1 - L; forward
  the present, then the queue's `Signal(F, ++n)` (both asynchronous, in order). With the flag, a guest semaphore S
  (count L at creation) is the waitable object (`GetFrameLatencyWaitableObject` answers a duplicate of S); a guest
  thread for the swapchain releases S once for each frame F completes. `SetMaximumFrameLatency` forwards, then raises S
  by the difference. `ResizeBuffers*` first waits for F to reach n.

- [ ] **Step 1:** smoke.cpp's `latency` line: a waitable swapchain's object is signalled L times at once and then once
  a presented frame (each wait 2 s at most), and a non-waitable one's frames in flight never pass L (F's value against
  n before each present); "ok" or what broke. Expected RED: the waitable object is a host handle (the wait fails).
- [ ] **Step 2:** Implement. Expected: `latency ok` forwarded; `d3d12_clear`'s three `resize` lines as on macOS; its
  frame time within 10% of guest mode's, recorded.
- [ ] **Step 3:** Commit.

### Task 5: M2's open items: descriptor writes after every ring, and lowering a fence

**Files:**
- Modify: `eac-vm/d3d/rules.txt` (`Create*View`, `CreateSampler`, `CreateSamplerFeedbackUnorderedAccessView`,
  `CopyDescriptors`, `CopyDescriptorsSimple`: `submit`), `eac-vm/d3d/guest/proxy.cpp` (`fence_signal` stores),
  `eac-vm/d3d/host/native.cpp`, `eac-vm/d3d/host/events.c` (`mn_d3d_fence_set`)
- Test: `eac-vm/d3d/tests/threads.cpp` (overwrite case), `eac-vm/d3d/tests/wire.cpp` (lower row)

**Interfaces:**
- Produces: `void mn_d3d_fence_set(void *obj, uint64_t v)` (events.c: the slot set to v, lower or higher; relays still
  only raise).

- [ ] **Step 1:** threads.cpp's overwrite case: one thread clears through RTV descriptor h (its ring unflushed, the
  thread alive), another then writes a different target's view into h; the first closes and executes: the first
  target reads back cleared, the second not ("threads overwrite 0 of 64 pixels wrong"). wire.cpp's lower row: a CPU
  `Signal(9)` then `Signal(3)`: "wire lower 3". Run `sh eac-vm/d3d/threads.sh` and `sh eac-vm/d3d/both.sh wire
  dxmt/tests/shaders`. Expected RED: lines differ (the loopback clears the second target; reads 9).
- [ ] **Step 2:** Implement. Expected: both ok; `threads.sh` expects four " 0 of " lines natively.
- [ ] **Step 3:** Commit.

### Task 6: The upscalers over forwarding (fork 0084)

**Files:**
- Fork: `src/dxgi/dxgi.cpp` (natively, `DXGIGetDebugInterface1(DXMT_NVEXT_GUID)` turns the vendor extension on from
  `DXMT_ENABLE_NVEXT=1`, without the registry, as the Windows build does)
- Modify: `eac-vm/d3d/guest/proxy.cpp` (`::DXGIGetDebugInterface1`: for `DXMT_NVEXT_GUID`, DXMT's registry keys in the
  guest from the guest's `DXMT_ENABLE_NVEXT`, as `dxgi.cpp`'s `InitializeVendorExtensionNV`, then forwarded),
  `eac-vm/d3d/rules.txt`, `eac-vm/guest/run-game.sh` and `eac-vm/tests/dxmt.sh` (the NVEXT and libxess settings in
  forward mode), `Sources/MacNeutronCore/MicroVM.swift` (`DXMT_ENABLE_NVEXT=1` in `macneutron-vm`'s environment with
  `MACNEUTRON_DLSS=1`), `eac-vm/d3d/gate.sh` (the 4 XeSS and 4 DLSS rows; `d3d12_dlss_refuse` is Task 7's)
- Test: `sh eac-vm/d3d/gate.sh` (27 rows), `Tests/MacNeutronCoreTests/MicroVMTests.swift`

- [ ] **Step 1:** Add the 8 rows to the d3d gate, and the MicroVM test (`MACNEUTRON_DLSS=1` and forward: the VM
  host's environment has `DXMT_ENABLE_NVEXT=1`). Run `sh eac-vm/d3d/gate.sh` and `swift test --filter MicroVMTests`.
  Expected RED: the DLSS rows read "vendor" wrong or fail to create a feature; the Swift test fails.
- [ ] **Step 2:** Implement. Expected: `d3d gate: all 27 programs passed`; coverage counts
  `IMTLD3D12CommandListExt::TemporalUpscale` forwarded; the Swift test passes.
- [ ] **Step 3:** Commit.

### Task 7: DLSS's refusals reach the program (`d3d12_dlss_refuse`)

**Files:** found by the debugging (`dxmt/tests/d3d12_dlss.cpp`'s `Refuse()`, DXMT's `src/nvngx`, the guest's call path).
**Test:** the `d3d12_dlss_refuse` row in guest mode (`sh eac-vm/dxmt/gate.sh`) and forwarded (`eac-vm/d3d/gate.sh`).

- [ ] **Step 1:** superpowers:systematic-debugging: reproduce in guest mode (the 4b-2 gate's row: `refused` thrice on
  macOS, `taken` in the guest), then find where nvngx's return value is lost (README §19: before any Direct3D call, so
  nvngx's own build or its call path in the guest). Ledger the root cause.
- [ ] **Step 2:** Fix it where it lies, with the row as the RED; add the row to the d3d gate (28 rows). Expected:
  `dlss refuse refused,refused,refused` in both modes; the 4b-2 gate's 29 rows pass.
- [ ] **Step 3:** Commit.

### Task 8: Per game: settings, shader cache, recordings and the precache (fork 0085)

**Files:**
- Fork: `src/d3d12/d3d12_pipeline_record.cpp` (natively: `DXMT_PIPELINE_RECORD`'s folder as given, no `Z:`; the file
  `<folder>/<DXMT_CACHE_EXE, or "game">.pipelines`)
- Modify: `eac-vm/dxmt-native/build.sh` (`install/bin/dxmt-replay`, `src/replay` built with M1's harness: the native
  prelude and a `main` calling its `wmain`), `eac-vm/host/build.sh` (copies it beside the dylibs),
  `eac-vm/host/macneutron-vm.swift` (with `MN_D3D_STATE=<folder>`: `DXMT_SHADER_CACHE_PATH=<folder>/` and, unless
  `MACNEUTRON_PRECACHE=0`, `DXMT_PIPELINE_RECORD=<folder>/pipelines`, each only if unset),
  `Sources/MacNeutronCore/MicroVM.swift` (`state/<appid>.dxmt`, passed as `MN_D3D_STATE`),
  `Sources/MacNeutronCore/Launcher.swift` (the VM branch with forwarding: `LaunchEnvironment.build`'s `DXMT_*` in the VM
  host's environment; the precache before the boot, behind the same toast),
  `Sources/MacNeutronCore/ShaderPrecache.swift` (a native replay: `vm/dxmt/dxmt-replay <file>` with the state
  folder's cache path, no Wine)
- Test: `Tests/MacNeutronCoreTests/MicroVMTests.swift`, `ShaderPrecacheTests.swift`, `eac-vm/dxmt-native/build-tests.sh`
  (a native record-and-replay check)

- [ ] **Step 1:** Swift tests: a forwarded VM launch of app 123 has `MN_D3D_STATE=<vm>/state/123.dxmt` and the game's
  per-game `DXMT_*` settings in the VM host's environment; the precache's native command is `<vm>/dxmt/dxmt-replay
  <file>` with `DXMT_SHADER_CACHE_PATH=<vm>/state/123.dxmt/` and no `DXMT_PIPELINE_RECORD`. build-tests.sh's check:
  `d3d12_cache` natively with `DXMT_PIPELINE_RECORD` writes `<folder>/game.pipelines`, and the native `dxmt-replay` on
  it fills a fresh cache folder (its `shaders_*.db` has rows). Expected RED: all fail.
- [ ] **Step 2:** Implement. Expected: the Swift tests and `tests: ok`.
- [ ] **Step 3:** Commit.

### Task 9: Native DXMT in the app's VM helpers

**Files:**
- Modify: `eac-vm/host/stage.sh` (`dxmt/`: the dylibs and `dxmt-replay`), `release/release.sh`
  (`eac-vm/dxmt-native/build.sh` before `eac-vm/host/build.sh`; `Helpers/vm/dxmt/*` signed),
  `Sources/MacNeutronCore/VMToolsInstaller.swift` (`items` has `dxmt`; `verify` checks its Mach-O files)
- Test: `Tests/MacNeutronCoreTests/VMToolsInstallerTests.swift`, `eac-vm/host/stage.sh` into a scratch folder

- [ ] **Step 1:** VMToolsInstaller test: a tools folder with `dxmt/d3d12.dylib` installs it into the VM folder and the
  digest covers it. Expected RED: not copied.
- [ ] **Step 2:** Implement; stage into a scratch folder. Expected: the test passes; `<scratch>/dxmt/` holds
  `d3d12.dylib`, `dxgi.dylib`, `winemetal.dylib`, `d3d11.dylib`, `d3d10core.dylib` and `dxmt-replay`, each passing
  `codesign -v`. `release.sh` is not run (it publishes nothing, but it is the maintainer's to run).
- [ ] **Step 3:** Commit.

### Task 10: The gate

- [ ] **Step 1:** The programs: `sh eac-vm/d3d/gate.sh` (28 rows, coverage ok), `sh eac-vm/dxmt/present-gate.sh
  forward` without SMITE 2, `sh eac-vm/tests/d3d-smoke.sh`, M1's `sh eac-vm/dxmt-native/gate.sh`, the 4b-2 gate in guest
  mode (29 rows), `vm-setup.sh`, `turn.sh`, `modes.sh`, `tpm-host.sh`, `audio-host.sh` (its small-buffer case is known
  to fail: record it), and `dxmt/check.sh` in one lane (the series' macOS check). Expected: each as M2 left it or
  better.
- [ ] **Step 2:** Ask the maintainer for the game runs (they aren't playing; Mac Steam is theirs to start). SMITE 2:
  `present-gate.sh forward`'s SMITE 2 row with `MACNEUTRON_VM_D3D_TRACE=1`, XeSS on, then again with
  `MACNEUTRON_DLSS=1` and DLSS on; Palworld the same through the launcher (the maintainer plays a scene). Each: game
  content on screen (the probe's colours), the upscaler named in DXMT's log, `coverage.py` ok on the run's traces.
- [ ] **Step 3:** For each method the traces mark (refused, unhandled, or no host count), a rule or code, each with a
  wire or threads case that fails first; `WriteToSubresource`/`ReadFromSubresource` only if marked. Then Step 2 again.
- [ ] **Step 4:** Commit.

### Task 11: Record

- [ ] **Step 1:** Research README §20 (what M3 built, the gate's table: macOS, guest mode, forwarded, per row and per
  game, with frame rate and host CPU recorded, not graded: M5's), the spec's "(M3: ...)" notes in §4.7, §4.8, §4.11,
  §4.12, §4.13 and §8's row, and the memory files that change. Privacy scan; commit.
