# DXMTRS C4c-6d: fullscreen devices and the window subclass on D3D9's core runtime

Slice 6 of C4c, last part, **inside C4c by the maintainer's decision of 2026-11-19**. Process: the C4 program's
(code map, this plan, three adversarial reviews folded before code, rows first, one commit a feature, full
gates once, one whole-branch review, spec, pin, push). Map: `build/c4c/s6d/map.md` (read at DXMTRS 53422d08;
cited "map 3" ...). The 5, 6a, 6c and 7a plans hold the rules this one repeats by name only. Reviews:
`build/c4c/s6d/review-{1-design,2-core,3-exec}.md` (cited R1, R2, R3; **R1 holds the Wine table, a function
at a time, and mtld3d's answer for every call of every row; R2 the subclass arm by arm and the walks of
create and `Reset`; R3 the helpers a row reuses, the registration edits, the mutant table and the job-script
list**). Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap:
`2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core`:

- **`CreateDevice` and `CreateDeviceEx` with `Windowed = FALSE` make a device**, as mtld3d makes it (map 1):
  the requested display mode set when it is a settable one other than the current, the device window made a
  borderless window covering the primary monitor unless `D3DCREATE_NOWINDOWCHANGES`, the back buffer at the
  mode's size, and at the device's release the mode and the window put back.
- **`Reset` and `ResetEx` into fullscreen, between fullscreen modes, onto another window and back to
  windowed** give mtld3d's answers (map 2): every fullscreen-specific step of mtld3d's `Reset` is plain
  Win32 and front-end state that already runs on a core device; one refusal by name goes.
- **The window subclass is installed on a core device's window** (map 3), so what it does is the core's
  too: the Direct3D cursor kept at `WM_SETCURSOR` (the gap C4c-7a's ruling R7a-7 left), the registry's
  display mode restored when the application is deactivated and the device's set again when it comes back,
  an external resize of a fullscreen window answered by covering the monitor again. It follows the device
  to another window at a `Reset` that names one.

Not in the slice, each stated (rulings below): a **windowed** core device's back buffer does not follow its
window's `WM_SIZE` (mtld3d's auto-resize: R6d-3); the **gamma ramp is not applied** on a fullscreen core
device (R6d-4); the present's pace is unchanged (R6d-7).

A device is never lost: mtld3d takes no exclusive mode and loses a device only on an encoder failure, and
the core's `TestCooperativeLevel` and `CheckDeviceState` already answer as mtld3d's do without one (map 4.1,
C4c-6c's ruling R6c-2). Nothing to do.

Wine, in the runner's unit ("Test failed" lines; "new" = over mtld3d's baseline), by R1's reading of every
function that makes a fullscreen device or resets into one, to its end (its table):

| suite | new now | new after | baseline lines that come back | "Test failed" after |
|---|---|---|---|---|
| device | 27 | **3**, all triaged (device.c:14544, 15104, and 14946, which joins them) | 54 | about 740 |
| d3d9ex | 62 | **0** | 35 | 74 |
| visual | 20 | **20** (four sites at risk, below) | 0 | 39 |

- The 87 lines that wait for this slice go (device: `test_swapchain_parameters` 15, `test_swapchain` 4,
  `test_wndproc_windowed` 4, `test_reset_fullscreen` 2; d3d9ex: `test_reset_ex` 26, `test_reset` 16,
  `test_swapchain_parameters` 16, `test_wndproc_windowed` 4).
- The functions that skip their fullscreen part under core today start to run and bring **mtld3d's own
  failures** with them, which are not new lines (device 54, d3d9ex 35: `test_wndproc` 37 and 24,
  `test_lost_device` 5 and 5, `test_wndproc_windowed`'s baseline sites, `test_window_style`,
  `test_device_window_reset`, d3d9ex's `test_reset` and `test_reset_ex` 10 each).
- One line appears: device.c:14946 (`test_cursor_clipping`, the cursor's clip rectangle after a mode change:
  the display driver's, triaged for mtld3d in `dxmt/mtld3d-triaged.txt`; it fails in today's default lane).
- **At risk, not determined by reading** (R1 I-2): visual's `test_multisample_get_front_buffer_data` skips
  under core today (it makes a fullscreen device) and then runs the core's first fullscreen multisampled
  `Reset` and two `GetFrontBufferData` reads: visual.c:17204, 17215/17217, 17227/17229.
- No Wine check sees a windowed device follow its window (every resize in the three suites is followed by a
  `Reset`), so R6d-3 moves no count.

## Global Constraints

The 7a plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-7a pushed** (DXMTRS f5604fa7, MacNeutron 39bc2ddf); job scripts in
`build/c4c/s6d/gates`, copied from s7a's and edited before their first run, with R3's wider guard
(`/usr/bin/grep -nE 'c4c/s7a|c4c7a|C4c-7a|53422d08|3b7cccf8|18e67c2d|JTVAL="extras"|for v in extras|want 1[47]\)'`
over `*.sh *.txt` prints nothing) and its positive checks run before anything else; VM state names
`c4c6d-*`; the builder VM stopped after each VM job; `memory_pressure` printed before a long job; each
DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device, **on the
  window's thread included** (the subclass runs outside the device's API lock).
- **No file outside `src/d3d9` and `docs/` changes.** mtld3d's answers are unchanged.
- **A core log line of this slice contains none of the gates' can't-present words** (7a's constraint).
- **The gates' machine** (map 7, R3 I1, I6, I8, M3):
  - Every `fs-*` row first checks that mode sets are emulated for the program (the guard of
    `dxmt/tests/d3d12_fullscreen.cpp`, by the module's own name) and **FAILs, never skips and never touches
    a device**, when they are not: a physical mode switch cannot happen.
  - A row asks for the desktop's current mode, which sets no mode, unless it tests a mode change; a row that
    sets a mode puts the registry's mode back itself on every path (a scope guard), after its release cell.
  - **One row covers the display** (`fs-cover`): on the Mac a window that covers a screen is raised above
    other applications' windows while the test program is active, and the Dock and the menu bar hide. Two
    sessions of well under a second each, in about twenty launches a slice's gates make. Every other row
    takes `D3DCREATE_NOWINDOWCHANGES`. Wine's `device.c`, in the conformance run each slice already makes on
    this machine, covers and switches far more.
  - No row changes the foreground window, minimises a window or moves the pointer; a row that must see the
    subclass answer an activation sends the message itself, after one drain of its queue (a real
    activation change pending from the person's own app switch would otherwise land inside the row).
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`. **No probe**: no file of the draw path changes.

## The mechanism

### Fullscreen `CreateDevice` (`create_core_device`)

mtld3d's answers, in an order that leaves nothing to undo (R2's Q3 has it against the current function):

1. The rejection of `Windowed = FALSE` goes.
2. A fullscreen request with a zero width or height: `D3DERR_INVALIDCALL`.
3. `D3DFMT_UNKNOWN` is resolved to the display's format **for a windowed device only**, as on mtld3d. Then
   **the sample count before the format refusal**, as `reset_impl` already has them: a fullscreen `UNKNOWN`
   then answers `D3DERR_INVALIDCALL`, mtld3d's answer (its sample-count check rejects format 0), where the
   current order would refuse it by name (R1 I-7, R2 I-3). The format refusal stays for a real format the
   core does not make. `select_device` moves up too: it reads no window.
4. `fullscreen::enter(hwnd, manage_window, fullscreen_mode_request(&pp))` when fullscreen, **before**
   `resolve_backbuffer_dims` (a size that is no mode follows the client rectangle, read after the window
   covers). mtld3d checks the count after its entry and restores; the core checks before, so a failing
   create never touches the window: the same answer, one flash fewer.
5. Nothing after the entry can fail (R2 m-2: a fullscreen request never resolves to zero dimensions, and
   the core's device, attach and textures return no error); `restore_from_fullscreen` stays at the
   zero-dimensions site as a guard.
6. The saved session is the device's (`fullscreen: Some(saved)`): the release path, which both runtimes
   share, leaves fullscreen before its core return (map 1).
7. `warn_present_params_fields_once`, which a core device never called; its refresh-rate text ("the display
   mode is never changed") is false on both runtimes since mtld3d sets modes: corrected in the same commit
   (R2 m-4).

### Fullscreen `Reset`

The refusal `IDirect3DDevice9::Reset (into fullscreen)` goes (map 2). What follows it is mtld3d's code,
already run by a core device for windowed resets: `resolve_reset_window_mode`, `apply_reset_window_mode`
(`enter_fullscreen`, `update_fullscreen`, `leave_fullscreen`), C4c-6c's resize and re-target (R2's Q4 walks
the five cases). The format refusal stays.

### The subclass

- `install_subclass` after the core device is boxed, as mtld3d's create installs it; nothing after it in
  the create sends a message or fails; the release unregisters before the core is taken and freed (R2 Q2).
- **Its one recorder path is cut for a core device** (R2 proved the count arm by arm: `WM_SIZE` →
  `apply_auto_resize`, reached by a windowed device **and by a fullscreen one while it leaves fullscreen**:
  the registry's mode is restored outside the driving guard, R2 m-1). The return is **the first statement**
  of `apply_auto_resize`, keyed on what holds for the device's whole life, the recorder's absence
  (`current_frame.get().is_none()`), not on `is_core()`, which turns false when the release takes the core.
  One `log_once_info!` (R2 m-4). The rest of the `WM_SIZE` arm (the cursor's resize bookkeeping) runs.
- A core `Reset` that names another window calls `CursorState::retarget` with the new window and a null
  view, **whatever the core's re-target answered** and after its borrow ends (R2 m-6), so the subclass and
  the cursor follow the device (7a's review m-4).
- `set_gamma_ramp` on a fullscreen core device logs once that the ramp is stored and not applied (R6d-4).

Texts that become false go with the commit that makes them so (R2 m-5 lists them: the docs of
`create_core_device`, of `apply_auto_resize`, of the subclass's residual race in `cursor.rs` and
`docs/d3d9/ARCHITECTURE.md`, three comments in `device.rs`, STATUS.md's three places; and STATUS.md's RESZ
bullet says "Two differences" where its own list has three: fixed here).

## Review Focus (for the final review)

- The subclass on a core device: no recorder call from any arm; no `CoreDevice` access from the window's
  thread; the registry entry's lifetime.
- `apply_auto_resize`'s return: first, and true through the release.
- Every path of a fullscreen create and `Reset` leaves the mode and the window as mtld3d leaves them.
- mtld3d's answers unchanged.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`fullscreen`** of `d3d9_triangle`, a device and a window of its own a row, plain devices
(an extended device keeps its rectangle when it leaves: R3 M5; Wine's d3d9ex is `CreateDeviceEx`'s and
`ResetEx`'s cover), judged in every lane; the expectations are mtld3d's answers (R1's table) and the control
confirms them. New helpers, no more (R3): `modesEmulated`, `placement` (style, extended style, rectangle)
and its compare, `procOf`, `pumpUntil` (drain the queue, test a condition, at most 2 s; never a fixed
sleep), `ModeBack` (the scope guard). The guard runs first in every `fs-*` row. Nine rows, in this order:

1. `fs-create` (flag): `D3D_OK`; `GetPresentParameters` says `Windowed` 0; **the back buffer has the
   adapter mode's size although the window's client area is 256x256**; `CreateAdditionalSwapChain` of a
   windowed chain `D3DERR_INVALIDCALL`; a quad drawn and read back; `Present` `D3D_OK`; the window's
   placement untouched.
2. `fs-cover`, **the one row that covers**: a fullscreen create without the flag: the window borderless at
   the primary monitor's rectangle; a quad read back and a present (C4c-6a's pattern); the window made
   smaller with `SetWindowPos` and the queue pumped: **the monitor's rectangle again**; `Reset` to a
   windowed 256x256: the placement as before the create, a quad read back; `Reset` into fullscreen: covering
   again; the release: the placement as before. Then a second create of **a size no mode has** (333x222):
   `D3D_OK` with the back buffer at the monitor's size (the client rectangle read after the cover: step 4),
   and its release restores the placement.
3. `fs-reject` (flag): a zero width, a sample count the device cannot make and `D3DFMT_UNKNOWN`: each
   `D3DERR_INVALIDCALL`, no device, the placement untouched.
4. `fs-reset-in` (flag): a windowed device, `Reset` into fullscreen: `Windowed` 0, the back buffer at the
   mode's size (not the client's), a quad read back; `Reset` back to a windowed 256x256: the description,
   a quad read back.
5. `fs-reset-window` (flag): a fullscreen device on window A (its procedure after the create noted), `Reset`
   naming window B: a quad read back and a present; **B's procedure is the one A had, A's is its own
   again**.
6. `fs-wndproc`: a windowed device's window procedure differs from the one read before the create and is
   that one again after the release; the same for a fullscreen device (flag).
7. `fs-reset-mode` (flag): a fullscreen device, `Reset` to a smaller mode the adapter lists: the desktop's
   current mode (`EnumDisplaySettings`, current) is the new one; after the release it is the registry's.
8. `fs-activate` (flag), a fullscreen device in a smaller mode, the queue drained once: the row sends
   `WM_ACTIVATEAPP(FALSE)`: the current mode is the registry's; `WM_ACTIVATEAPP(TRUE)` and the queue pumped:
   the device's mode again.
9. `ws-size`, **last**: a windowed device's window resized with `SetWindowPos` (which sends `WM_SIZE`
   before it returns: no pump): on mtld3d **the back buffer's description and the viewport** are the new
   client rectangle's (`GetPresentParameters` follows on neither runtime); **under `strict` they keep
   their size** (R6d-3); on both a quad drawn afterwards reads back and a present answers `D3D_OK`.

Rows of other launches that change:

- `rs-refuse` (`reset`, strict only) keeps its name and takes the refusal no row meets: a `Reset` to an
  R5G6B5 back buffer answers `D3DERR_NOTAVAILABLE`, the device usable and owing no `Reset`; the launch's one
  refusal literal becomes `IDirect3DDevice9::Reset (a back-buffer format the core does not make)`.
- `cu-setcursor` (`extras`): its `strict` branch goes: the same handle after `WM_SETCURSOR` on both runtimes.

- Scripts (R3 section 4 has each edit): `fullscreen` joins `d3d9-check.sh`'s **zero-refusal arm** (the
  default arm of `refusals_ok` passes a launch it does not know) with its `check` list; the VM gate's rows
  loop and its kind-less core loop (`fs64-fresh`, `fs32-fresh`); the guest script's two lines; `reset`'s
  literal and the comments that name the old one; the comments that name `cu-setcursor`'s strict cell.
- Job scripts (R3 section 6): an `fsrows` counter (9) beside `exrows`; the validation launches of `ja`,
  `jt` and `jq` on `fullscreen` **with d3d9-check's 300 s watchdog** (R3 M7); `jq`'s list fifteen launches;
  the VM counters; `jm`'s letters and tokens; new runners.
- Controls: `ja` (the launch on mtld3d under the validation layer: reports 0, rows ok 11; then default, msl
  and core). **Under strict core at the base** (R1 I-5, R3 I2): red on the eight `fs-*` rows (each at its
  create or its `Reset`; `fs-reject` at its first cell, the windowed-only rejection answering
  `D3DERR_NOTAVAILABLE`; `fs-wndproc` at its windowed cell), on `fullscreen-runtime` (the refusal line of
  `fs-reset-in`'s `Reset`: this is what proves the zero-refusal arm) and on `extras-cu-setcursor`; **green
  on `ws-size`, `rs-refuse` and `reset-runtime`**: `rs-refuse` pins a refusal that exists; `ws-size`'s
  strict cell holds at the base because no subclass is there. Then `jcr` (the VM, default runtime: the
  guest's prefix emulates mode sets and lists smaller modes; its screen has no window manager).

### Task 2: fullscreen `CreateDevice` (DXMTRS)

One commit: `create_core_device` as above, the warning's text, the docs that it makes false, STATUS.md.

### Task 3: fullscreen `Reset` (DXMTRS)

One commit: the refusal gone, its comments, STATUS.md.

### Task 4: the subclass (DXMTRS)

One commit: the install, `apply_auto_resize`'s return, the cursor's re-target at a core `Reset`, the gamma
line, the docs it makes false, STATUS.md (R7a-7's cursor sentence goes; R6d-3's, R6d-4's and the stated
gaps' come). **One `jt` at this tip**: every row of every launch green strict under core; `fullscreen`
under core with the validation layer: reports 0 (the only gate that would see a drawable resized under a
live layer: R2 m-8).

### Task 5: gates

`mutants-expect.txt`, `jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Four mutants (R3's table):
  a. the fullscreen session not kept on the core device (`fullscreen: None` at the create) → `fs-cover` (its
     re-cover cell: the `WM_SIZE` takes the windowed branch) and `fs-activate` (no deactivation);
     `fs-reset-in` and `fs-reset-mode` stay green (a `Reset` enters afresh);
  c. the subclass not installed on a core device → `fs-wndproc`, `fs-activate`, `fs-cover` (its re-cover
     cell), `fs-reset-window` (its procedure cell), `cu-setcursor`;
  d. the cursor's re-target not called at a core `Reset` → `fs-reset-window`;
  f. `enter` after the dimensions are resolved → `fs-cover`, at its second create's cell (256x256 where the
     monitor's size is wanted).
- **Not mutants** (R3 I4, I5): a restore after a late failure of the create has no reachable cell (step 5);
  `apply_auto_resize`'s return is shown without a crash: `ws-size`'s "follows" cell is green on mtld3d only
  if the row's `SetWindowPos` reached the arm, `fs-wndproc` green under core shows the subclass is there,
  so `ws-size` green under strict core is the return (without it the launch aborts in a panic).

### Task 6: review. Task 7: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression, a new pub item of
  d3d9-rt:** none (the subclass's registry is the existing static, with its documented argument).
- **Not silent:** the auto-resize's and the gamma ramp's `log_once_info!`/`log_once_warn!`; STATUS.md
  states R6d-3, R6d-4, R6d-5 and the covered-window gap.

## Rulings (planning)

Each with its cost if wrong.

- **R6d-1: fullscreen is mtld3d's own front-end code, run by the core device**, not dxmt-core's fullscreen
  (DXGI-shaped: it raises the window topmost and minimises on focus loss, which D3D9's mtld3d deliberately
  does not: map 5.3). Cost if wrong: none found; a second implementation would diverge from mtld3d's
  triaged conformance.
- **R6d-2: no device loss.** Parity with mtld3d (map 4.1). Cost: Wine's ten `test_lost_device` lines come
  back as baseline lines, as on mtld3d.
- **R6d-3: a windowed core device does not follow `WM_SIZE`.** mtld3d re-makes the back buffer at the new
  client size from the window's procedure; the core's version would drain and resize the `CoreDevice` from
  the window's thread, outside the API lock: a mechanism with a hazard of its own, and no Wine line asks
  for it. **The cost is wider than a game that resizes its window**: macOS shrinking a window's visible
  rectangle after the layer is attached (the menu bar, the Dock), a user dragging a resizable window, and
  maximize and restore all reach it; the core then keeps its back buffer and Core Animation stretches it to
  the window (magnified with the nearest filter, minified linearly), where mtld3d renders at the new size.
  Not a regression (a core device has never followed its window); pinned by `ws-size`. **Proposed as a
  slice of its own (6e) and put to the maintainer**, else C4d's window work.
- **R6d-4: the gamma ramp is not applied on a fullscreen core device.** dxmt-core's presenter takes a
  ramp, but d3d9-rt would have to name `dxmt-util`'s type (a line of `Cargo.lock`, outside `src/d3d9`) and
  no row can see the result. Cost if wrong: a game's brightness setting does nothing under the opt-in
  runtime, with one warning. **Owed to C4c-7d**, which runs the x64 check anyway.
- **R6d-5: the layer's drawable keeps the back buffer's size** (C4c-6c's design, and D3D11's on this
  core). **Under the launcher's emulated mode sets, which is the usual case, a fullscreen core device at any
  mode other than the desktop's is magnified by Core Animation with the nearest-neighbour filter** (Wine's
  mac driver sets it on the layer), where mtld3d's own present pass resamples (MetalFX when enlarging);
  the aspect is kept by Wine's letterbox; at the desktop's own mode the two agree. Cost: a visibly coarser
  image for a game below the desktop's resolution under the opt-in runtime; seen by no gate. A stated gap
  for C4d's presenter options and the maintainer's list.
- **R6d-6: the rows and the machine** (the constraint above): one covering row, the guard in every row,
  modes restored by the rows themselves. Cost if wrong: a display covered for longer than a second when a
  row hangs (the launch's 300 s watchdog bounds it), never a physical mode switch.
- **R6d-7: the present's pace is unchanged** (the draft took the device's reported refresh rate for every
  interval: R2 I-1 showed that halves the frame rate of a game that asks for 60 Hz on a 120 Hz panel;
  mtld3d reads the requested rate only for an interval that divides it). Cost: intervals TWO to FOUR of a
  fullscreen core device divide the desktop's rate, not the requested one; with `present.maxFps` not
  followed either (map 9.3), a pacing gap that exists today, for 7d's list.
- **P-6d: no probe; X-6d: no x64 check.** As 7a's.

## Stated gaps

- **A fullscreen core device keeps presenting into a window another application covers** (R2 I-2): mtld3d
  drops those presents; the core skips only a hidden or minimised window, and D3D9's fullscreen leaves the
  window in place on an application switch, so covered is the usual state after alt-tab. How long each
  present then waits for a drawable is not determined (Metal documents up to a second). A windowed core
  device has the same gap. Owed with the occlusion observer the spec lists for C4.
- After a `Reset` onto another window the old one stays covering with no view, on both runtimes (R2 m-7).
- The presenter's colour space and HDR options, `present.maxFps` and a window on another monitor than the
  primary are as they are for a windowed core device (map 5.1, 9.9).
- `CreateDeviceEx`'s and `ResetEx`'s fullscreen forms have no row (Wine's d3d9ex runs them).
- `rs-refuse` pins a refusal the base already has: no run shows it red.

## Review dispositions

Three reviews, no Critical. Folded:

- **R1** (9 Important, 13 Minor): I-1 device ends at three triaged lines; I-2 the visual function that
  starts to run, its four sites at risk; I-3 R6d-5 restated for the emulated case, the usual one; I-4
  R6d-3's cost widened and put to the maintainer; I-5 the control's three rows; I-6 the zero-refusal arm;
  I-7 the sample count before the format refusal; I-8 the restore mutant dropped; I-9 `ws-size` judges the
  back buffer's description and the viewport; the Wine table's exact counts.
- **R2** (3 Important, 8 Minor): I-1 the pace change dropped (R6d-7); I-2 the covered window, a stated gap;
  I-3 as R1 I-7; m-1 the return first and keyed on the recorder's absence; m-2 `select_device` before the
  entry, nothing to restore; m-3 as R1 I-3; m-4 the two log lines at info or warn and the false warning
  text; m-5 the texts that become false; m-6 the cursor's re-target unconditional; m-7, m-8 stated.
- **R3** (10 Important, 13 Minor): I1 **one covering row** where the draft had four (`fs-recover` and the
  windowed round trip folded into `fs-cover`, the rest under the flag), said in the constraint and to the
  maintainer; I2 the control's prediction; I3 the scripts; I4 **not taken as recommended** (keep mtld3d's
  order for the count, to keep the restore mutant): the core checks before the entry and has no restore to
  prove, one cover fewer; I5 the auto-resize mutant replaced by its argument; I6 mutant a's cells and the
  rows' own mode restore; I7 the cells that discriminate; I8 `pumpUntil` and the drain; I9 the wider guard;
  I10 the size no mode has, and mutant f; M1 to M5, M7 the rows' and scripts' details. **M6 not taken**: a
  fullscreen `UNKNOWN` is `D3DERR_INVALIDCALL` on mtld3d (its sample-count check rejects format 0: read
  again), not a device.

## Execution notes

- **Commits.** MacNeutron: rows 5640717b (launch `fullscreen`, nine rows; `rs-refuse` on R5G6B5;
  `cu-setcursor` without its strict branch). DXMTRS: 0f55d538 (Task 2), a9514c5f (Task 3), 25001b77 (Task 4),
  each through `tools/rust-check.sh`; 28166459, a correction of STATUS.md's RESZ bullet after the final
  review, through the gate too. The pin is 28166459; every lane ran at 25001b77, whose code it has.
- **Controls** (`ja` with the validation launch, `jcr`; DXMTRS at the base): **as written beforehand**.
  mtld3d: `fullscreen` under the validation layer, no report, rows ok 11; default and msl green; the VM,
  both widths and both WoW64 forms, 9 of 9. The core at the base: red on the eight `fs-*` rows (each at its
  create or its `Reset`; `fs-reject` at its first cell; `fs-wndproc` at its windowed cell), on
  `fullscreen-runtime` (the `Reset` refusal's line: the zero-refusal arm proven) and on `cu-setcursor` (the
  class arrow back after `WM_SETCURSOR`: 7a's inferred gap, observed); green `ws-size`, `rs-refuse`,
  `reset-runtime`. No second row commit.
- **Lane** (`jt1`, 25001b77 with 5640717b): d3d9-check ok on default and core, `fullscreen` 9 of 9 on both;
  `fullscreen` under core with the validation layer: reports 0.
- **Chain**: `jw` below; `jr` (plain build: key and imports the base's, conformance at triaged sites only,
  x64 check not due); `jc` (the VM, default and core, old and new WoW64, 64- and 32-bit: ok, `fullscreen` 9
  and 10 with the runtime line); `jq` (fifteen launches under the validation layer: no report; stateblock
  equal on both runtimes); `jm`: **a, c, d and f red on exactly the cells written before their builds, no
  other row, green whole** (a: `fs-cover`'s re-cover cell and `fs-activate`'s deactivation; c: those two,
  `fs-wndproc`, `fs-reset-window` and `cu-setcursor`; d: `fs-reset-window`'s procedure cell; f: `fs-cover`'s
  333x222 cell, 256x256 where the monitor's size is wanted).
- **Sweep** (`build/c4c/s6d/sweep.md`): every count as R1 predicted: over the baseline `device` 27 to 3
  (triaged: 14544, 14946, 15104), `d3d9ex` 62 to 0 (the runner: "ok"), `visual` 20; "Test failed" 739, 74,
  39. `device` executes 159570 checks where it executed 56814: what the default runtime executes. The
  function with the four sites at risk ran clean. No new Metal validation report. Baseline lines back in
  `device`: 52, not R1's 54 (two sites the runner marks flaky, device.c:4471 and 4476, did not fail in this
  run); `d3d9ex` 35.
- **The auto-resize's return, seen**: the core logs of `jt1`'s `fullscreen` launches hold its info line
  ("a windowed device's back buffer keeps its size ..."), so `ws-size`'s `WM_SIZE` reached the arm and took
  the return (the lanes capture info lines).
- **Final review** (`build/c4c/s6d/review-final.md`): push as is; no Critical, no Important, six Minors.
  m-1: STATUS.md's RESZ bullet counted the scratch texture as a difference and left the real third one (the
  bound depth-stencil's own texture) in the skip list: corrected in 28166459. m-2: **the create's order
  changes two more answers on core** than the one the implementer noted (a windowed create with an
  unsupported format and an unresolvable size: now `D3DERR_NOTAVAILABLE`): with an unsupported format and an
  invalid multisample type it now answers `D3DERR_INVALIDCALL`, mtld3d's answer; with no Metal device and an
  unresolvable size `D3DERR_NOTAVAILABLE` (unreachable in practice). m-3: **`fs-cover` covers the display in
  three sessions a launch** (the create, the `Reset` back into fullscreen, the 333x222 create), not the two
  the constraint above says; in the default and msl lanes a covered attempt may retry for up to 10 s; the
  windowed round trip cannot move to a row under the flag (it is the only check of a managed window's
  placement across a `Reset`). Said to the maintainer. m-4, m-5: three log texts inexact on one runtime,
  for 7d-1. m-6: `fs-reject`'s placement cells pass whatever the create's order, as the plan says.
- **For the maintainer**: the cover count above; R6d-3 (a windowed core device does not follow its window:
  proposed as a slice 6e, else C4d); R6d-5 (nearest-neighbour magnification of a sub-desktop fullscreen
  mode under emulated mode sets); the covered-window presents.
- **No probe, no x64 check.**
