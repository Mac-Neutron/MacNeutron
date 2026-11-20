# DXMTRS C4c-6d: fullscreen devices and the window subclass on D3D9's core runtime

Slice 6 of C4c, last part, **inside C4c by the maintainer's decision of 2026-11-19**. Process: the C4 program's
(code map, this plan, three adversarial reviews folded before code, rows first, one commit a feature, full
gates once, one whole-branch review, spec, pin, push). Map: `build/c4c/s6d/map.md` (read at DXMTRS 53422d08;
cited "map 3" ...). The 5, 6a, 6c and 7a plans hold the rules this one repeats by name only. Reviews:
`build/c4c/s6d/review-{1-design,2-core,3-exec}.md`. Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap: `2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core`:

- **`CreateDevice` and `CreateDeviceEx` with `Windowed = FALSE` make a device**, as mtld3d makes it (map 1):
  the requested display mode set when it is a settable one other than the current, the device window made a
  borderless window covering the primary monitor unless `D3DCREATE_NOWINDOWCHANGES`, the back buffer at the
  mode's size, and at the device's release, or a create that fails later, the mode and the window put back.
- **`Reset` and `ResetEx` into fullscreen, between fullscreen modes, onto another window and back to
  windowed** give mtld3d's answers (map 2): every fullscreen-specific step of mtld3d's `Reset` is plain
  Win32 and front-end state that already runs on a core device; one refusal by name goes.
- **The window subclass is installed on a core device's window** (map 3), so what it does is the core's
  too: the Direct3D cursor re-asserted at `WM_SETCURSOR` (the gap C4c-7a's ruling R7a-7 left), the
  registry's display mode restored when the application is deactivated and the device's set again when it
  comes back, an external resize of a fullscreen window answered by covering the monitor again. It follows
  the device to another window at a `Reset` that names one.

Not in the slice, each stated (rulings below): a **windowed** core device's back buffer does not follow its
window's `WM_SIZE` (mtld3d's auto-resize); the **gamma ramp is not applied** on a fullscreen core device.

A device is never lost: mtld3d takes no exclusive mode and loses a device only on an encoder failure, and
the core's `TestCooperativeLevel` and `CheckDeviceState` already answer as mtld3d's do without one (map 4.1,
C4c-6c's ruling R6c-2). Nothing to do.

Wine, in the runner's unit ("Test failed" lines over mtld3d's baseline): 87 lines wait for this slice
(device 25: `test_swapchain_parameters` 15, `test_swapchain` 4, `test_wndproc_windowed` 4,
`test_reset_fullscreen` 2; d3d9ex 62: `test_reset_ex` 26, `test_reset` 16, `test_swapchain_parameters` 16,
`test_wndproc_windowed` 4). **Predicted: all 87 go**, leaving `visual` 20, `device` 2 (the two triaged
lines), `d3d9ex` 0 over the baseline. The functions that skip their fullscreen part under core today
(`test_wndproc`, `test_lost_device`, `test_window_style`, `test_mode_change`, `test_device_window_reset`,
device's `test_reset`, `test_get_display_mode`, and one iteration each of `test_scissor_size`,
`test_occlusion_query`, `test_cursor`, `test_cursor_clipping`, `test_window_position`) **start to run**:
they bring mtld3d's own baseline failures with them (about 46 lines in `device`, 29 in `d3d9ex`: map 4.2),
which are not new lines, and whatever the core answers differently there, which nobody has seen:
`test_wndproc` above all. R1 reads each of these functions to its end and gives the count a function.

## Global Constraints

The 7a plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-7a pushed**; job scripts in `build/c4c/s6d/gates`, copied from s7a's and edited
before their first run, with the guard (`/usr/bin/grep -nE 'c4c/s7a|c4c7a|C4c-7a'` over `*.sh *.txt` prints
nothing) and the positive checks run before anything else; VM state names `c4c6d-*`; the builder VM stopped
after each VM job; `memory_pressure` printed before a long job; each DXMTRS commit passes
`tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device, **on the
  window's thread included** (the subclass runs outside the device's API lock).
- **No file outside `src/d3d9` and `docs/` changes.** mtld3d's answers are unchanged.
- **The gates' machine** (map 7). A row asks for a fullscreen mode equal to the desktop's current one, so no
  mode is set, unless the row says otherwise; a row that sets another mode first checks that mode sets are
  emulated for the program (the guard of `dxmt/tests/d3d12_fullscreen.cpp`) and FAILs, never skips and
  never switches a physical mode, when they are not. No row changes the foreground window, minimises a
  window or moves the pointer; a row that must see the subclass answer an activation sends the message
  itself. Every row puts back what it changed (mode, window style and rectangle, class cursor) on every path.
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`. **No probe**: no file of the draw path changes.

## The mechanism

### Fullscreen `CreateDevice` (`create_core_device`)

mtld3d's order, with mtld3d's own functions (map 1, steps 5 to 9 and 14):

1. The rejection of `Windowed = FALSE` goes.
2. A fullscreen request with a zero width or height: `D3DERR_INVALIDCALL` before the window is touched.
3. The checks that do not depend on the window come before it, so that only two later failures need a
   restore: the back buffer's format (the core makes X8R8G8B8 and A8R8G8B8: its refusal stays; **`UNKNOWN`
   is resolved to the display's format for a windowed device only**, as on mtld3d) and the sample count.
4. `fullscreen::enter(hwnd, manage_window, fullscreen_mode_request(&pp))` when fullscreen, **before**
   `resolve_backbuffer_dims` (the client rectangle is read after the mode is in place).
5. `warn_present_params_fields_once`, which a core device never called.
6. A failure after the entry (zero dimensions; no Metal device) calls `restore_from_fullscreen`.
7. The saved session is the device's (`fullscreen: Some(saved)`): the release path, which both runtimes
   share, leaves fullscreen before its core return (map 1).

The present's pace takes the device's reported mode (`reported_display_mode(pp).refresh_rate`: the
requested rate for a fullscreen device) where it takes the desktop's today (map 5.1).

The layer: the core's drawable is the back buffer's size and Core Animation scales it to the window (map
5.1), where mtld3d's drawable is the window's and its own pass scales. For a back buffer of the mode's size
on a window covering the monitor in that mode the two agree; a mode smaller than the monitor without mode
emulation is scaled by Core Animation (ruling R6d-5).

### Fullscreen `Reset`

The refusal `IDirect3DDevice9::Reset (into fullscreen)` goes (map 2). What follows it is mtld3d's code,
already run by a core device for windowed resets: `resolve_reset_window_mode`, `apply_reset_window_mode`
(`enter_fullscreen`, `update_fullscreen`, `leave_fullscreen`), C4c-6c's resize and re-target. The format
refusal stays.

### The subclass

- `install_subclass` after the core device is boxed, as mtld3d's create installs it (the registry keeps the
  device's address until the release's `uninstall_subclass`, which runs before the core return).
- **Its one recorder path is cut for a core device** (map 3: `WM_SIZE` of a windowed device →
  `apply_auto_resize`, seven recorder calls): `apply_auto_resize` returns at once on a core device, with
  one debug line (ruling R6d-3). The rest of the `WM_SIZE` arm (the cursor's resize bookkeeping) runs.
  Every other arm is user32, the cursor state and the fullscreen session (R2 proves it arm by arm, the
  functions they call included: `reassert_fullscreen_cover`, `reactivate_fullscreen`,
  `fullscreen::deactivate`, `enter_resize`, `request_client_surface`).
- A core `Reset` that names another window calls `CursorState::retarget` with the new window and a null
  view after the core's re-target, so the subclass and the cursor follow the device (7a's review m-4).

## Review Focus (for the final review)

- The subclass on a core device: no recorder call from any arm; no `CoreDevice` access from the window's
  thread; the registry entry's lifetime (installed after the box, removed before the free, on a failed
  create too).
- Every failure path of a fullscreen create and `Reset` leaves the mode and the window as mtld3d leaves them.
- mtld3d's answers unchanged.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`fullscreen`** of `d3d9_triangle`, a device and a window of its own a row, judged in every
lane; the expectations are mtld3d's answers and the control confirms them. Fullscreen modes are the
desktop's current one unless said. Rows (R3 settles the helpers and whether each cell can be observed):

1. `fs-create`: a fullscreen device with `D3DCREATE_NOWINDOWCHANGES`: `D3D_OK`; the swap chain's present
   parameters say `Windowed` 0; `GetDisplayMode` the back buffer's size; `CreateAdditionalSwapChain`
   `D3DERR_INVALIDCALL`; a quad drawn and read back; `Present` `D3D_OK`; the window's style and rectangle
   untouched.
2. `fs-cover`: the same without the flag: the window borderless and covering the primary monitor; a quad
   read back and a present (C4c-6a's pattern); after the device's release the window's style and rectangle
   as before the create.
3. `fs-reject`: a fullscreen request of zero width: `D3DERR_INVALIDCALL`, the window untouched; a sample
   count the device cannot make: `D3DERR_INVALIDCALL`, the window as before.
4. `fs-reset-in`: a windowed device, `Reset` into fullscreen: the description, the window covering, a quad
   read back; `Reset` back to a windowed size: the window's style and rectangle as before, a quad read back.
5. `fs-reset-mode`, **after the mode-emulation guard**: a fullscreen device, `Reset` to a smaller mode the
   adapter lists: `GetDisplayMode` and the desktop's current mode are the new one; the release puts the
   desktop's mode back.
6. `fs-reset-window`: a fullscreen device, `Reset` naming another window: the new window covers, a quad read
   back and a present; **the new window's procedure is the subclass and the old window's is its own again**.
7. `fs-wndproc`: a windowed device's window procedure differs from the class's after the create and is the
   class's again after the release; the same for a fullscreen device.
8. `fs-activate`, after the guard, a fullscreen device in a smaller mode: the row sends
   `WM_ACTIVATEAPP(FALSE)`: the desktop's mode is the registry's again; `WM_ACTIVATEAPP(TRUE)` and the
   queue pumped: the device's mode again.
9. `fs-recover`: a fullscreen device's window made smaller with `SetWindowPos`, the queue pumped: it covers
   the monitor again and the back buffer's description is unchanged.
10. `ws-size`, **last in the launch**: a windowed device's window resized with `SetWindowPos`, the queue
    pumped: on mtld3d the back buffer follows the client area; **under `strict` it keeps its size** (R6d-3),
    and on both a quad drawn afterwards reads back and a present answers `D3D_OK` (the arm that would reach
    the recorder on a core device, met by a row before any game meets it).

Rows of other launches that change:

- `rs-refuse` (`reset`, strict only): `Reset` into fullscreen is no refusal any more. The row keeps its
  name and takes the refusal no row meets: a `Reset` to an R5G6B5 back buffer answers
  `D3DERR_NOTAVAILABLE`, the device usable and owing no `Reset`; the launch's one refusal literal becomes
  `IDirect3DDevice9::Reset (a back-buffer format the core does not make)`.
- `cu-setcursor` (`extras`): its `strict` branch goes: the same handle after `WM_SETCURSOR` on both runtimes.

- Scripts: as C4c-7a registered `extras` (the launch, its `check` list, no refusal list: the default arm of
  `refusals_ok` is "none"? R3 says how a launch with no expected refusal is registered so that a refusal
  line fails it); `reset`'s literal.
- Job scripts: an `fsrows` counter at every call site; `jq`'s list fifteen launches; the VM counters; `jm`'s
  letters and tokens; new runners.
- Controls: `ja` (the launch on mtld3d under the validation layer: reports 0; then default, msl and core:
  **core red on every `fullscreen` row but `fs-reject`'s first cell's row if R3 finds it green, on
  `rs-refuse` and on `cu-setcursor`**; R3 gives the exact list with the call each stops at), `jcr` (the VM
  on the default runtime; whether the guest emulates mode sets decides `fs-reset-mode` and `fs-activate`
  there: R3).

### Task 2: fullscreen `CreateDevice` (DXMTRS)

One commit: `create_core_device` as above, the present's pace, STATUS.md's line.

### Task 3: fullscreen `Reset` (DXMTRS)

One commit: the refusal gone; STATUS.md's line.

### Task 4: the subclass (DXMTRS)

One commit: the install, `apply_auto_resize`'s core return, the cursor's re-target at a core `Reset`;
STATUS.md (R7a-7's cursor sentence goes; R6d-3's and R6d-4's come). **One `jt` at this tip**: every row of
every launch green strict under core; `fullscreen` under core with the validation layer: reports 0.

### Task 5: gates

`mutants-expect.txt`, `jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Mutants (R3 refines the
cells and whether each is cuttable):
  a. the fullscreen session not kept on the core device (`fullscreen: None`) → `fs-cover` (the window not
     restored at release), and the rows that leave fullscreen;
  b. no restore after a fullscreen create that fails late → `fs-reject` (its sample-count cell, if that
     check still runs after the entry; else R3 names the cell or the mutant goes);
  c. the subclass not installed on a core device → `fs-wndproc`, `fs-activate`, `fs-recover`,
     `cu-setcursor`;
  d. the cursor's re-target not called at a core `Reset` → `fs-reset-window` (its procedure cell);
  e. `apply_auto_resize` without its core return → `ws-size`: the launch dies there (a panic), which is why
     the row is last: `fullscreen-ws-size` and `fullscreen-runtime` red, nothing else.

### Task 6: review. Task 7: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression, a new pub item of
  d3d9-rt:** none intended (the subclass's registry is the existing static, with its documented argument).
- **Not silent:** `apply_auto_resize` on core logs at debug; STATUS.md states R6d-3 and R6d-4.

## Rulings (planning)

Each with its cost if wrong.

- **R6d-1: fullscreen is mtld3d's own front-end code, run by the core device**, not dxmt-core's fullscreen
  (DXGI-shaped: it raises the window topmost and minimises on focus loss, which D3D9's mtld3d deliberately
  does not: map 5.3). Cost if wrong: none found; a second implementation would diverge from mtld3d's
  triaged conformance.
- **R6d-2: no device loss.** Parity with mtld3d (map 4.1). Cost: Wine's ten `test_lost_device` lines stay,
  as on mtld3d's baseline.
- **R6d-3: a windowed core device does not follow `WM_SIZE`.** mtld3d re-makes the back buffer at the new
  client size from the window's procedure; the core's version would resize and drain the `CoreDevice` from
  the window's thread, outside the API lock (map 9.8): a mechanism with a hazard of its own, and no Wine
  line asks for it. Cost if wrong: a game that resizes its window without a `Reset` renders at the old size,
  scaled to the window by Core Animation, where mtld3d renders at the new one; pinned by `ws-size`; owed to
  C4d's window work.
- **R6d-4: the gamma ramp is not applied on a fullscreen core device.** dxmt-core's presenter takes a
  ramp, but d3d9-rt would have to name `dxmt-util`'s type (a line of `Cargo.lock`, outside `src/d3d9`) and
  no row can see the result (the back buffer and the front-buffer read are before the ramp). Cost if wrong:
  a game's brightness setting does nothing under the opt-in runtime. **Owed to C4c-7d**, which runs the
  x64 check anyway; said in STATUS.md and the roadmap.
- **R6d-5: the layer keeps the back buffer's size as its drawable** (C4c-6c's design): a fullscreen mode
  smaller than the monitor is scaled by Core Animation when mode sets are not emulated. Cost if wrong: a
  filter or an aspect other than mtld3d's on such a device (not readable from the code: map 5.1); the
  launcher's prefixes emulate mode sets, where the window is the mode's size.
- **R6d-6: rows ask for the desktop's own mode** unless they test a mode change, and those check the
  emulation first. Cost if wrong: a physical mode switch on the gates' machine: the guard's FAIL prevents it.
- **P-6d: no probe; X-6d: no x64 check.** As 7a's.

## Stated gaps

- The presenter's colour space and HDR options, `present.maxFps` and a window on another monitor than the
  primary are as they are for a windowed core device (map 5.1, 9.9): not this slice's.
- What Core Animation's scaling looks like (R6d-5) is seen by no gate.

## Review dispositions

(after the three reviews)

## Execution notes

(after the gates)
