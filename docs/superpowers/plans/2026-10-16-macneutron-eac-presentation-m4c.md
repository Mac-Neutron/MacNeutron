# EAC microVM, milestone 4c: presentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A D3D game in the guest shows its frames in a window on the Mac, through the host's `CAMetalLayer`: DXMT's
presenting test programs present there as they do on macOS, and SMITE 2 renders through the host.

**Architecture:**
- **The window.** The spec (§5.6) has `CreateMetalViewFromHWND` make the layer in the MacNeutron window. The host's
  winemetal.so finds a guest HWND's view through winemac.so's `macdrv_functions`, so the stand-in winemac.so becomes
  the host's window code:
  - `get_win_data` answers every HWND with one window, "MacNeutron VM", made on the main thread at first use;
  - `macdrv_view_create_metal_view` adds a `CAMetalLayer`-backed view filling it, the newest on top;
  - `macdrv_view_release_metal_view` removes that view.

  `macneutron-vm`'s main thread runs AppKit rather than `dispatchMain`, so the window and Metal's main-thread calls
  share one run loop.
- **The 11 presentation slots** (47, 48, 66, 67, 70, 71, 72, 73, 93, 97, 101), refused in the guest since 4b-1, are
  forwarded:
  - a view is a handle of its own kind, released by slot 73 rather than slot 1;
  - the layer and the drawable's texture are borrowed;
  - the drawable is autoreleased, and nil is an answer;
  - `getProps` gives back the guest's own device value, never the host's pointer.
- **Seeing the frames.** This session can't capture the screen: winshot reports "screen capture is not allowed", and
  Screen Recording permission is the user's to grant. So the host checks the frames itself.
  - With `MN_PRESENT_PROBE=1`, the forwarder's present (slots 47/48) copies every 60th drawable's texture into a buffer
    before presenting it. DXMT sets `framebuffer_only` false, so this is allowed.
  - When that command buffer completes, the host logs the frame's green, white and colour counts, the same measures
    winshot takes of a window.
  - winshot stays an extra row for when the permission is there.
- **Gate.**
  - DXMT's `d3d12_clear` and `present_loop` run in the guest and on macOS. They must print the same presentation lines,
    their frames must reach check.sh's green and white shares, and the host must count every frame presented.
  - SMITE 2 runs in the guest with the arguments Steam's own launch entry gives it, and its frames reach the window
    with game content in them.

**Tech Stack:** AppKit and QuartzCore in the stand-in winemac.so (Objective-C), the 4b forwarder (`eac-vm/metal/wm`),
`macneutron-vm` (Swift), the guest image, the 4b-2 gate's scripts, and `wine-arm64/tools/winshot` (unchanged,
optional).

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.6 (Presentation), §5.7 (Game
frames), §8 milestone 4 (4c: "SMITE 2 renders through the host").
- **Facts:**
  - research README §8-9;
  - winemetal's view code (`winemetal_unix.c`: `_CreateMetalViewFromHWND`, `struct macdrv_functions_t`,
    `struct macdrv_win_data`, `execute_on_main`);
  - DXMT's presenter (`dxmt_presenter.cpp` calls getProps, then sets `device` itself, so the device it reads back is
    unused);
  - DXMT patch 0002 (winemetal loads the MetalFX presenter only when `MACNEUTRON_PRESENT=1`).

## Global Constraints

### Repo and product

- **Worktree and commits.** Work in `.claude/worktrees/upstream-master`, branch `exp/upstream-master`. Commit locally,
  never push.
- **The product doesn't change.** No DXMT patch and no change under `wine-arm64/`; winshot is used as it is. The
  host's winemetal.so stays the product's.
- **No MetalFX presenter.** The VM host never sets `MACNEUTRON_PRESENT`, so winemetal never loads it and frames take
  the plain path. That matches the macOS reference's `MACNEUTRON_NO_METALFX=1`.
- **Steam.** Never start, sign into or quit Steam, and never open MacNeutron.app. SMITE 2's run uses the Mac Steam
  that's already running, through the 3a forwarder.
- **SMITE 2's arguments** are Steam's own launch entry for it, `-NOEAC` included, as Steam itself runs it (3c, memory
  "SMITE 2 EAC check"). The EAC bootstrapper path is 3c's, not this plan's.

### The window

- **One window.** Title `MacNeutron VM`. It isn't closable (milestone 5 owns the window's life). Its size is
  `MN_VM_WINDOW=WxH`, default `1280x720`, in points.
  - It's made on the main thread at the first `get_win_data`, then shown and brought to the front: the activation
    policy turns `.regular`.
  - A run that never presents never makes a window.
- **AppKit first.** The stand-in calls `[NSApplication sharedApplication]` before its first window. A test's worker
  thread starts only once the main thread runs the application (a ready flag set from the run loop): a
  `dispatch_sync` to a main queue nothing serves would deadlock.
- **Every HWND maps to that window.** `struct macdrv_win_data` is exactly winemetal's: `hwnd`, `cocoa_window`,
  `cocoa_view`, `client_cocoa_view`, with `client_cocoa_view` the content view.
- **A metal view** is an `NSView` subclass that `wantsLayer`, has a `CAMetalLayer` as its layer, and autoresizes to the
  content view. It's added on top, and removed by `macdrv_view_release_metal_view`.
- **`macdrv_functions`** is the exported data symbol, in `struct macdrv_functions_t`'s exact order:
  - `get_win_data`, `release_win_data`, `macdrv_view_create_metal_view`, `macdrv_view_get_metal_layer`,
    `macdrv_view_release_metal_view`, and `on_main_thread` (runs the block at once on the main thread, else
    `dispatch_sync` to the main queue);
  - the others NULL.

### Forwarding (calls.c)

| Slot | Call | Mode | Fields |
|---|---|---|---|
| 47 | `MTLCommandBuffer_presentDrawable` | queued | handle, arg: handles in |
| 48 | `…presentDrawableAfterMinimumDuration` | queued | handle, arg0: handles in |
| 66 | `MetalDrawable_texture` | queued | handle in; ret out, borrowed |
| 67 | `MetalLayer_nextDrawable` | sync (nil is an answer) | handle in; ret out, autoreleased |
| 70 | `MetalLayer_setProps` | queued | handle in; arg: `WMTLayerProps` in, its `device` a nested handle in |
| 71 | `MetalLayer_getProps` | sync | handle in; arg: `WMTLayerProps` out, `device` restored to the guest's own value |
| 72 | `CreateMetalViewFromHWND` | sync | hwnd a value; device in; ret_view out (new kind `MN_HK_VIEW`, plus 1); ret_layer out, borrowed |
| 73 | `ReleaseMetalView` | queued | special: the view handle's object goes to winemetal's slot 73, then the handle is unbound with no `release` |
| 93 | `MetalLayer_setColorSpace` | sync | handle in; ret |
| 97 | `MetalLayer_getEDRValue` | sync | handle in; arg: `WMTEDRValue` out |
| 101 | `WMTQueryDisplaySettingForLayer` | sync | layer in; hdr_metadata: `WMTHDRMetadata` out |

- **Mode counts.** Refused slots go from 17 to 6 (120-124 Mach ports, 83). Synchronous: 80. Queued: 67. Local: 2.
- **A view left at a context's end** is released by the handle table's end (`mn_wm_release_object` for
  `MN_HK_VIEW` is winemetal's slot 73), so no stale view stays in the window.
- **The host counts drawables presented** (slots 47 and 48) per context. When a context ends it logs
  `mn-present: context <id>: <n> drawables presented`. That prefix isn't an error line for `compare.py host`.

### The present probe

- **Only with `MN_PRESENT_PROBE=1`** in `macneutron-vm`'s environment. Probing every 60th present (the 30th, 90th,
  ...):
  - before calling winemetal's present, the host encodes a blit of the drawable's texture into a Shared buffer, in the
    same command buffer;
  - a completed handler counts its pixels;
  - it logs `mn-present: probe <frame> pixels <n> green <pct> white <pct> colors <k>`.
- **The measures are winshot's:**
  - green: the green channel between 60 and 95;
  - white: every channel above 200;
  - colors: distinct colours at 4 bits a channel.

  They're read from the drawable's 8-bit BGRA or RGBA pixels, as stored.
- **Another format** logs `mn-present: probe <frame> format <n> not read`.
- **Nothing for the product**, and no cost without the variable: one load and a branch per present.

### The gate (`eac-vm/dxmt/present-gate.sh`)

- **Programs.**
  - `d3d12_clear.exe 600`: check.sh's program, with more frames.
  - `present_loop.exe 1280 720 0 0 600 0`: check.sh's D3D11 game loop, in `share/dxmt-tests` (share.sh copies
    `build/presenter/present_loop.exe`).
- **macOS reference.** One run each through the launcher, as `ref.sh` does, for the program's own lines and frame
  time.
- **Guest.** One VM boot each, with `MN_PRESENT_PROBE=1`, the program alone in the guest. It runs as `mn` in prefix
  `/home/mn/dxmt`, under Xvfb.
- **Rows, all required:**
  - `d3d12_clear`:
    - the guest prints `presented 600/600 frames`;
    - its `resize flags`, `resize waitable` and `resize size` lines equal macOS's;
    - every probe's green is at least 80 (check.sh's `CLEAR_GREEN`);
    - the host counted at least 600 drawables presented.
  - `present_loop`:
    - the guest prints `avg frame`;
    - the median probe's green is at least 56 and white at least 12 (`LOOP_GREEN`, `LOOP_WHITE`);
    - the host counted at least 600.
  - **The host:** no error lines (`compare.py host`).
  - **SMITE 2:**
    - the host counted at least 600 drawables presented within 180 s;
    - one probe after the first 60 s has at least 64 colours;
    - the host log is kept.
    - **Named skips,** none of them a FAIL:
      - `skip: Mac Steam isn't running`;
      - `skip: Steam didn't initialise in the guest` (the forwarder's init, or a signed-out Mac Steam);
      - `skip: SMITE 2 isn't installed`.
- **winshot row, optional.** With Screen Recording allowed, winshot reads `MacNeutron VM` while `d3d12_clear` runs,
  and green is at least 80. Without it, the row prints `skip: no Screen Recording permission for this process`.
- **Times.** Both sides' frame times are printed and recorded, not graded.

### Process

- Lighter testing: one gate per task, the full gates (`eac-vm/metal/gate.sh`, `eac-vm/dxmt/gate.sh`,
  `eac-vm/dxmt/present-gate.sh`) at the end.
- Gates assert.
- Privacy scan before docs commits.
- Commit trailers.
- SMITE 2's logs stay in `build/`, never committed.

## Review Focus

1. **A view released while another ring's queued present still uses its layer.**
   - Expected: the present runs (the drawable holds its layer) and nothing crashes.
   - Test: Task 2's `present` case releases the view on one thread right after presenting on another.
2. **A guest process that exits without releasing its view.**
   - Expected: the context's end removes it from the window.
   - Test: Task 2's `view-left` case.
3. **Two swapchains (two HWNDs).**
   - Expected: two views, the newest on top. Releasing it shows the older one.
   - Test: Task 1's `two-views` case.
4. **`nextDrawable` with no drawable available** (the window occluded or minimised).
   - Expected: nil comes back as a nil handle, as on macOS, and the next frame asks again.
   - Test: Task 2's `nil-drawable` case: a layer with a zero drawable size gives nil.
5. **The host's device pointer reaching the guest through `getProps`.**
   - Expected: never. `props.device` comes back as the guest put it.
   - Test: Task 2's `props` case.

---

### Task 1: the window, on the host

**Files:**
- Create: `eac-vm/metal/wm/host/winemac.m`, the stand-in winemac.so; `stand-ins.c` keeps ntdll alone.
- Modify:
  - `eac-vm/metal/wm/Makefile`: `$(HOST)/winemac.so` from `winemac.m`, with `-framework AppKit -framework QuartzCore`;
  - `eac-vm/host/macneutron-vm.swift`: `import AppKit`; the main thread runs `NSApplication.shared.run()` with
    policy `.accessory`, instead of `dispatchMain()`.
- Create: `eac-vm/metal/wm/tests/window-test.m`. It loads `metal-host`'s winemetal.so; the main thread runs NSApp and
  a worker thread, started once it does, calls winemetal's slots directly:
  - **`view`:**
    - slot 72 for HWND `0x1234`, with the default device, gives a view and a layer;
    - the layer's delegate is in a window titled `MacNeutron VM`, 1280x720 in points;
    - slot 70 sets a 320x180 BGRA8 drawable, slot 67 gives a drawable, and slot 66 its texture;
    - a command buffer clears the texture to (0, 0.3, 0), blits it into a Shared buffer, and presents it: the buffer's
      pixels are (0, 77, 0).
  - **`two-views`:** a second slot 72 for HWND `0x5678` adds a second view on top. Slot 73 on it leaves the first
    view on top and its layer intact.
  - **`release`:** slot 73 on the last view leaves the content view with no metal views.
  - Make target `window-test`.

- [ ] **Step 1: Write `window-test.m`; run it.** Expected: FAIL `view`. Today's stand-in has no `macdrv_functions`, so
  slot 72 gives no view.
- [ ] **Step 2: Implement `winemac.m` and the Swift main-thread change.**
- [ ] **Step 3: Run `make -C eac-vm/metal/wm window-test`.** Expected: PASS all three.
- [ ] **Step 4: Run `eac-vm/host/build.sh`, then `eac-vm/metal/gate.sh`.** Expected: all passed. Metal's main-thread
  calls still work under the AppKit run loop.
- [ ] **Step 5: Commit.** `eac-vm: the host's game window (stand-in winemac.so)`.

### Task 2: the presentation slots, forwarded, and the probe

**Files:**
- Modify:
  - `eac-vm/metal/wm/calls.c`, `calls.h` and `handles.h`: Global Constraints' table, `MN_HK_VIEW`, and the specials
    `MN_SP_VIEW` (72, 73), `MN_SP_LAYER` (71's device) and `MN_SP_PRESENT` (47, 48: the count and the probe);
  - `marshal.c` and `slots.c`: the specials;
  - `host/invoke.m`: `mn_wm_release_object` for `MN_HK_VIEW` calls winemetal's slot 73;
  - Create `host/present.m`: the present count per context and the probe;
  - `host/dispatch.m`: the `mn-present:` count line;
  - `eac-vm/dxmt/compare.py`: `mn-present:` isn't an error line, and its self-test says so;
  - `tests/marshal-test.m`: `modes` (80 sync, 67 queued, 6 refused, 2 local); `coverage` is unchanged (the sizes are
    the catalog's);
  - `tests/memory-test.m`, which loads the new stand-in and starts its cases off the main thread, as window-test does.
- **memory-test cases** (the guest's code and the host's in one process, real winemetal.so, real window):
  - **`present`:**
    - through `__wine_unix_call_funcs`: 72 for HWND `0x1234`, then 70 (320x180), 67, 66, a render pass clearing the
      texture to (0, 0.3, 0) (the forwarded encoder slots), 47 and a commit;
    - the context's present count is 1;
    - then, on another thread, 73 while the first thread's present is still queued: nothing crashes, and the view is
      gone once both rings have run.
  - **`probe`:** with `MN_PRESENT_PROBE=1`, the 30th of 30 such presents logs a probe line with pixels 57600 and
    green 100.
  - **`props`:** 70 with the guest's device handle, then 71. `props.device` is the guest's value from before the call,
    and the drawable size is 320x180.
  - **`nil-drawable`:** a layer set to a 0x0 drawable: 67 gives handle 0 and status 0.
  - **`view-left`:** a context that ends with a view still bound. Its end leaves no metal view in the window.

- [ ] **Step 1: Write the failing tests:** `modes` with the new counts, the five memory-test cases, and compare.py's
  `host-present` self-test case.
- [ ] **Step 2: Run them.** Expected: FAIL. The slots are refused, there is no probe, and `mn-present:` fails
  `compare.py host`.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run `make -C eac-vm/metal/wm marshal-test memory-test` and `python3 -I eac-vm/dxmt/compare.py
  self-test`.** Expected: PASS.
- [ ] **Step 5: Commit.** `eac-vm: winemetal's presentation calls forwarded, with a host-side frame probe`.

### Task 3: the presenting programs in the guest

**Files:**
- Modify: `eac-vm/host/macneutron-vm.swift` passes `MN_PRESENT_PROBE` through. It's the process's own environment, so
  nothing to do unless something strips it; check that.
- Modify: `eac-vm/dxmt/share.sh` also copies `build/presenter/present_loop.exe`.
- Create:
  - `eac-vm/tests/present.sh` (guest): runs `$1` with its arguments under Wine (prefix, Xvfb, as `dxmt.sh` does) and
    writes `/mnt/share/traces/present/<name>.txt` and `.status`;
  - `eac-vm/dxmt/present-gate.sh` (Mac): Global Constraints' gate rows:
    - the macOS reference run;
    - the guest run with `MN_PRESENT_PROBE=1 run-guest.sh`, its host output kept;
    - each row from the guest's output, the host's `mn-present:` lines and `compare.py host`;
    - the optional winshot row.
- Rebuild the guest's winemetal.so (`build-guest.sh`), `stage.sh`, then `image/build.sh wine`.

- [ ] **Step 1: Write `present.sh` and `present-gate.sh`. Run the gate on today's image, before Task 2's guest
  library is in it.** Expected: FAIL. The guest still refuses presentation, so d3d12_clear presents 0 frames.
- [ ] **Step 2: Rebuild the guest library and the image.**
- [ ] **Step 3: Run `eac-vm/dxmt/present-gate.sh`.** Expected: the `d3d12_clear`, `present_loop` and host rows pass.
  The winshot row skips, unless the permission has been granted. The SMITE 2 row is Task 4's.
- [ ] **Step 4: Fix what it finds, each with a Mac-side test where it reproduces (systematic-debugging).**
- [ ] **Step 5: Commit.** `eac-vm: DXMT's presenting programs show in the host window`.

### Task 4: SMITE 2 renders through the host

**Files:**
- Create: `eac-vm/tests/smite2-render.sh` (guest).
  - As `eac.sh` sets up: prefix `/home/mn/smite2`, `steam.exe` and Valve's `steamclient64.dll` from the share, game
    files at `/mnt/games`.
  - It runs Steam's launch entry: `wine 'C:\Program Files (x86)\Steam\steam.exe'
    'Z:\mnt\games\SMITE 2\Windows\Hemingway.exe' -homedir=SMITE2Alpha -d3d12 -game -oss=SteamV2
    -RallyHereURL=https://api-smite2.titanforgegames.com -NOEAC`, for 180 s, then `wineserver -k`.
  - It prints the game's last D3D12 log lines (adapter, swapchain) and whether SteamAPI initialised (the forwarder's
    log).
- Modify: `eac-vm/dxmt/present-gate.sh`'s SMITE 2 row.
  - It boots with `--steam-appid 2437170 --ram-mib 16384` and `MN_PRESENT_PROBE=1`.
  - It checks the named skips first: `pgrep -x steam_osx`, the game folder, then the guest's SteamAPI line.
  - It requires Global Constraints' SMITE 2 row.
  - It keeps the host log in `build/eac-vm-work/smite2-render.txt`.

- [ ] **Step 1: Run the row.** Expected: a pass, a named skip, or a FAIL naming what's missing (a slot, a winemac
  function, a game file).
- [ ] **Step 2: Fix what it finds,** each with a Mac-side test where it reproduces.
- [ ] **Step 3: Run the whole present gate.** Expected: every row passes or names its skip.
- [ ] **Step 4: Commit.** `eac-vm: SMITE 2 renders through the host (milestone 4c)`.

### Task 5: docs

- [ ] **Research README §10,** "Presentation (4c)":
  - what's forwarded and how the window works;
  - the probe;
  - the gate's numbers: frame times on both sides, presents counted, probe shares and colours;
  - the findings.
- [ ] **The spec's milestone 4 line:** 4c done, or what the SMITE 2 row still waits for; next milestone 5.
- [ ] **`eac.sh`'s comment and image/build.sh's `DXMT_GUEST=none` note:** presenting works now, so software rendering
  is optional.
- [ ] **Memory.**
- [ ] **Privacy scan, then commit.** `docs: presentation (milestone 4c)`.
