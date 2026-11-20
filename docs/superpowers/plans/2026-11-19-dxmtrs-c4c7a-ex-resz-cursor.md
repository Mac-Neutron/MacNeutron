# DXMTRS C4c-7a: gamma, frame latency, the `Ex` creators, RESZ and the cursor calls on D3D9's core runtime

Slice 7 of C4c, first part. Process: the C4 program's (code map, this plan, three adversarial reviews folded
before code, rows first, one commit a feature, full gates once, one whole-branch review, spec, pin, push). Map:
`build/c4c/s7a/map.md` (all of slice 7, read at DXMTRS 4166a54a; cited "map 2.4" ...). The 5, 6a and 6c plans
hold the rules this one repeats by name only. Reviews: `build/c4c/s7a/review-{1-design,2-core,3-exec}.md`.
Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`. Roadmap:
`2026-11-19-dxmtrs-c4-remaining-steps.md`.

## Goal

Under `runtime.mode = core` these calls stop answering `D3DERR_NOTAVAILABLE` and give mtld3d's answers:

- `SetGammaRamp`, `GetGammaRamp`: mtld3d's own bodies (map 2.1: they store and report a ramp and touch no
  recorder; a ramp is applied on a fullscreen device only, which the core does not make yet).
- `SetMaximumFrameLatency`, `GetMaximumFrameLatency`: mtld3d's own bodies (map 2.2), **and the bound is
  enforced**: the core's queue waits for the frame that many presents back, at the value mtld3d would
  enforce (the application's, or `present.maxFrameLatency` when it names one), from device creation on.
- `CreateRenderTargetEx`, `CreateOffscreenPlainSurfaceEx`, `CreateDepthStencilSurfaceEx`: mtld3d's own
  bodies (map 2.3: a usage check, the base creator, the reported usage).
- `SetRenderState(D3DRS_POINTSIZE, RESZ)`: the bound depth-stencil is copied (one sample) or resolved (more)
  into the depth texture bound at stage 0, at the call, and the state is stored (map 2.4).
- `SetCursorProperties`, `SetCursorPosition`, `ShowCursor`: mtld3d's own bodies (map 4: user32 and a
  message to a view the core device does not have, which is a no-op).

Refused by name on the core, `D3DERR_NOTAVAILABLE`: RESZ into a **`D3DUSAGE_DYNAMIC` depth texture** (ruling
R7a-5): `IDirect3DDevice9::SetRenderState(RESZ) (into a dynamic depth texture)`.

Wine, by the map's table of the sweep after C4c-6c and my reading of `resz_test` to its end (its five RESZ
calls: a 2-sample D24S8 surface into an INTZ texture; a 2-sample automatic depth-stencil into one; no
texture at stage 0; a 1-sample surface into one; no depth-stencil bound):

| suite | function | now | predicted |
|---|---|---|---|
| visual | `resz_test` | 31 | 0 |
| device | `test_cursor` | 12 | 0 |
| device | `test_cursor_pos` | 0 (skipped) or 1 | 0 if it runs; it skips on a Mac in use, and is marked flaky |
| d3d9ex | `test_resource_access` | 60 | 0 |
| d3d9ex | `test_frame_latency` | 9 | 0 |
| d3d9ex | `test_format_unknown` | 4 | 0 |
| d3d9ex | `test_create_depth_stencil_surface_ex` | 3 | 0 |

Totals over mtld3d's baseline: `visual` 51 → 20, `device` 39 → 27, `d3d9ex` 138 → 62. What stays: fullscreen
and the window subclass (87: the maintainer's decision), `ProcessVertices` (5) and the three silent
differences (15), which are 7d's, and the two triaged lines. Each function above runs further than it did,
so a line nobody has seen may appear; the reviews read each function to its end (R1's table).

## Global Constraints

The 6c plan's, with this slice's names: worktrees `build/c4b3c/dx` (`c4c`) and `build/c4b3c/mn` (`c4c-mn`);
**the base is what C4c-6c pushed** (DXMTRS 4166a54a, MacNeutron ad9cbaee); job scripts in
`build/c4c/s7a/gates`, copied from s6c's and edited before their first run, with the guard
(`/usr/bin/grep -nE 'c4c/s6c|c4c6c|C4c-6c'` prints nothing) and the positive checks (row counts, launch
lists, letters and tokens, `X64_BASE` 4166a54a, the runner's SHAs) run before anything else; VM state names
`c4c7a-*`; the builder VM stopped after each VM job; `memory_pressure` printed before a long job; each
DXMTRS commit passes `tools/rust-check.sh`; home path `~`, privacy scan.

- A core arm is keyed on the device being core; no mtld3d recorder call reachable on a core device.
- **No file outside `src/d3d9` changes.** mtld3d's answers are unchanged: where a body runs for both
  runtimes, it is mtld3d's body as it stands.
- **No row calls `SetCursorPosition`** or asserts on the pointer's position: it moves the pointer of the
  machine the gates run on (and a moved pointer is what made `test_cursor_pos` skip in the last sweep).
- **The x64 check does not run in this slice** (once at the end of C4c-7), unless the range leaves
  `src/d3d9` and `docs/`. **No probe**: no file of the draw path changes (`rt/src/device/draw.rs`,
  `src/device/core_draw.rs`).

## The mechanism

### Guards removed (gamma, latency, the `Ex` creators, the cursor calls)

Ten `refuse_under_core!` sites go (map 1: sites 3, 4, 13 to 20). The bodies behind them are mtld3d's, and
by the map's reading none reaches the recorder on a core device:

- gamma (`src/device.rs` `device_set_gamma_ramp`, `device_get_gamma_ramp`): plain fields; `queue_gamma`
  queues only on a fullscreen device; `pending_gamma` is consumed by mtld3d's frame flush only.
- latency (`src/device/extended.rs`): `FrameLatency` fields.
- the `Ex` creators (`src/device/extended.rs`): `usage_accepted`, the base creator (already served on
  core), `report_usage` (ORs restricted-content bits into the surface's reported usage; it cannot set
  `D3DUSAGE_DEPTHSTENCIL`, which `core_depth()` reads).
- the cursor calls (`src/cursor.rs`): user32, the cursor state, the bitmap's own `GetDesc` and
  `LockRect(READONLY)`, and `send_overlay_state`, which returns on a null view (a core device's).

**R2 proves or refutes each "no recorder call" by tracing the body to its leaves**, the paths a `Reset`, a
present and the device's release take through the cursor and gamma state included (`reapply_gamma`, the
cursor's teardown): a `Mtld3dOnly` access on a core device panics.

### Frame latency enforced

d3d9-rt gains one method, `CoreDevice::set_max_latency(frames)`: the queue's `set_max_latency` (dxmt-core,
an atomic store read at each present's boundary: no clamp, no ring; D3D11 sets it the same way). The front
end calls it with `FrameLatency::enforced()` in `SetMaximumFrameLatency` on a core device and once where the
core device is made, so a configured `present.maxFrameLatency` holds on core too. The default (3) is the
queue's default: a device nobody configures behaves as today. The doc comment of
`set_maximum_frame_latency` stays true on both runtimes.

### RESZ

One front-end arm, `core_arms::resz(dev)`, called where mtld3d's `resolve_resz_depth` is, in place of the
guard; the state store that follows is mtld3d's code (so `GetRenderState(POINTSIZE)` reads the magic back).
In mtld3d's order:

1. No texture at stage 0: mtld3d's one warning, nothing recorded.
2. The stage-0 texture is a `D3DUSAGE_DYNAMIC` depth texture: the refusal by name, before the store
   (R7a-5).
3. A stage-0 texture that is not of a depth format: one warning, nothing recorded (the core has no depth
   level for it).
4. Source = `core.depth()`, the bound depth-stencil (R7a-3); none: one warning, nothing recorded.
5. Destination = `DepthRef { id: texture_id, level: 0 }` after `texture::core_flush_dirty` has made the
   texture's core form (a depth texture never bound or sampled has none yet). **Not** `Surface::core_depth()`:
   that names a level only of a texture made with `D3DUSAGE_DEPTHSTENCIL`, and a RESZ destination may be a
   plain depth texture (map 2.4 b).
6. `depth_resolve(src, dst)` when the source has more than one sample, else `copy_depth(src, dst)` (C4c-4b
   and 4d's: each ends the open pass first, so the draws recorded so far come before it, as mtld3d's
   stream order has them). False (sizes, formats or sample counts the core does not pair; the source being
   the destination) is one warning naming both, nothing recorded and `D3D_OK`, as mtld3d's skip.

A non-dynamic depth texture has no dirty level (its lock is refused), so nothing uploads over the resolve
afterwards (`core_flush_dirty`'s own comment); the texture's next sampling bind reads the resolved depth.

## Review Focus (for the final review)

- Every body un-guarded: no recorder call on a core device on any path (R2's traces against the diff).
- RESZ: the destination named without `D3DUSAGE_DEPTHSTENCIL`; the refusal before the store and every
  other path through the store; nothing recorded on a skip; `src == dst`.
- The latency value at creation and at Set; nothing else reads the queue's bound.
- mtld3d's answers unchanged (the diff outside `cfg(feature = "core-runtime")` and core branches).

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

A new launch **`extras`** of `d3d9_triangle`, a device and a window of its own a row, judged in every lane;
the expectations are mtld3d's answers (the implementer reads the code; the control confirms them).
Thirteen rows:

1. `gm-ramp`: `GetGammaRamp` before any Set (mtld3d's default ramp); a ramp set and read back; a null ramp
   and a swap chain other than 0 as mtld3d answers (no crash, the stored ramp unchanged).
2. `fl-latency` (an extended device): Get's default 3; Set 30 and Get; Set 31 `D3DERR_INVALIDCALL` and Get
   still 30; Set 0 and Get 3; Get with a null pointer as mtld3d answers; after Set 1, three frames drawn
   and presented, the last read back (a present by C4c-6a's pattern).
3. `ex-rt` (extended): `CreateRenderTargetEx` with usage 0: `D3D_OK`, its description, a Clear and a quad
   read back through it; a usage it refuses: `D3DERR_INVALIDCALL` and the out slot as passed;
   `D3DFMT_UNKNOWN`: as mtld3d answers.
4. `ex-ds` (extended): `CreateDepthStencilSurfaceEx` with usage 0: its description, bound and a depth-tested
   draw read back; a refused usage; `D3DFMT_UNKNOWN`.
5. `ex-plain` (extended): `CreateOffscreenPlainSurfaceEx` with usage 0 in the default, system-memory and
   scratch pools as mtld3d answers each: a lock written and read back; a refused usage; a restricted-content
   usage as `GetDesc` reports it.
6. `rz-msaa`: a 2-sample render target and a 2-sample D24S8 surface, a quad whose depth runs 0 to 1 across
   the target, an INTZ texture (`D3DUSAGE_DEPTHSTENCIL`) at stage 0, RESZ: `D3D_OK`,
   `GetRenderState(POINTSIZE)` the magic; the texture then sampled by a pixel shader into the back buffer
   and four columns read (`resz_test`'s first part).
7. `rz-auto`: a device whose back buffer and automatic depth-stencil have 2 samples: the same from the
   automatic depth-stencil (`resz_test`'s second part).
8. `rz-single`: a 1-sample D24S8 surface into the INTZ texture: the copy.
9. `rz-plain`: the destination an INTZ texture made **without** `D3DUSAGE_DEPTHSTENCIL`, never bound as a
   depth-stencil and never sampled before the RESZ, from a 2-sample and from a 1-sample source (if mtld3d
   refuses such a texture at creation, the row says so and the row is dropped with a line in the notes).
10. `rz-edges`: each `D3D_OK` with the destination's content as it was (read by sampling it): no texture at
    stage 0; no depth-stencil bound; a destination of another size; a colour texture at stage 0; the bound
    depth-stencil's own texture at stage 0.
11. `rz-refuse`, **under the launch's `strict` argument only** (the core lanes; elsewhere the row does
    nothing and passes): RESZ into a `D3DUSAGE_DYNAMIC` depth texture answers `D3DERR_NOTAVAILABLE`,
    `GetRenderState(POINTSIZE)` keeps its old value, the device still draws.
12. `cu-props`: `SetCursorProperties` with a null bitmap; a 32x32 A8R8G8B8 offscreen plain surface of the
    default pool and one of the scratch pool; a size that is no power of two; a format other than A8R8G8B8;
    a hot spot outside the bitmap: each as mtld3d answers.
13. `cu-show`: `ShowCursor`'s return value through a sequence (before any cursor is set; after
    `SetCursorProperties`; TRUE then FALSE then TRUE): the previous state each time, as mtld3d answers.

- Scripts: as C4c-6c registered `reset` (`d3d9-check.sh`'s launch, `check` list and refusal list; the VM
  gate's two loops; the guest script's 64- and 32-bit lines). `rz-refuse` meets one refusal by name on the
  core: the launch's refusal list is that one literal.
- Job scripts: an `EXTRAS_ROWS` counter (13) at every call site, `jv0` on the launch, `jq`'s list fourteen
  launches with its count, the VM counters, `jm`'s letters and tokens.
- Controls, in this order: `jv0` (the launch on mtld3d under the validation layer; a red cell is a wrong
  expectation or an mtld3d finding, settled in a second row commit before any code), `ja` (core red on
  exactly the launch's rows and its `-runtime`), `jcr` (the VM on the default runtime).

### Task 2: gamma, frame latency, the `Ex` creators (DXMTRS)

One commit: seven guards removed; `CoreDevice::set_max_latency` with its test; the two front-end calls.

### Task 3: RESZ (DXMTRS)

One commit: `core_arms::resz`, the narrowed refusal, the guard gone.

### Task 4: the cursor calls (DXMTRS)

One commit, apart so that it can be dropped whole (R7a-1): three guards removed and whatever R2's trace
shows the bodies need on a core device. `docs/d3d9/STATUS.md`'s core section in the last commit.
Lane `jt` at the tip: every row of every launch green strict under core; `extras` under core with the
validation layer: reports 0.

### Task 5: gates

`jmb`, then the chain (`jw` first, `jr`, `jc`, `jq`, `jm`). Mutants, the expected cells written into
`mutants-expect.txt` before any build:
  a. RESZ always copies (never resolves) → `rz-msaa`, `rz-auto`, `rz-plain`'s 2-sample half;
  b. RESZ names its destination through `Surface::core_depth()`'s rule (a `D3DUSAGE_DEPTHSTENCIL` texture
     only) → `rz-plain`;
  c. `CreateDepthStencilSurfaceEx`'s guard put back → `ex-ds`;
  d. `SetMaximumFrameLatency` not storing on core (its guard put back) → `fl-latency`;
  e. `ShowCursor`'s guard put back → `cu-show`.
- **No row and no mutant sees the latency's enforcement** (a present cannot tell how far the CPU ran
  ahead): the queue's value is covered by d3d9-rt's unit test and the final review's reading. Stated here,
  not found as a NOT-RED later. `SetCursorPosition` has no row either (constraint above); gamma has no
  mutant (a guard like c, d and e).

### Task 6: review. Task 7: spec, pin, push

As the 6a plan's.

## Rules check

- **A static, a config key, a wire field, a dependency, a derive, a lint suppression:** none intended.
- **New pub items:** `CoreDevice::set_max_latency`; `core_arms::resz` (crate-visible). **New state:** none.
- **Not silent:** the one refusal by name; each RESZ skip warns once.
- **Divergences, each stated:** RESZ into a dynamic depth texture refused (R7a-5); a multisampled source
  whose format differs from the destination's is skipped where mtld3d's depth transfer converts (R7a-4);
  no `WM_SETCURSOR` re-assert without the window subclass (R7a-7); the latency bound is the queue's, not
  mtld3d's ring (R7a-2).

## Rulings (planning)

Each with its cost if wrong.

- **R7a-1: the roadmap's 7c (the cursor calls) is folded into 7a.** The map shows the three bodies need no
  window work (the roadmap's own condition for keeping them in C4c), and a slice of three deletions would
  spend a whole review cycle. Cost if wrong (`test_cursor` needs the subclass, or a body reaches the
  recorder): Task 4's commit is dropped and the cursor calls go to C4d with a line in the roadmap; the rest
  of the slice stands.
- **R7a-2: the latency bound is enforced through dxmt-core's queue**, at the value mtld3d's `enforced()`
  gives, set at creation and at each Set. Cost if wrong: frames paced differently from mtld3d for a game
  that sets a bound other than 3 (the queue waits for the frame N presents back to retire, mtld3d for its
  ring's entry): a difference in pacing, not in answers; nothing in the gates sees it.
- **R7a-3: RESZ's source is the core's bound depth-stencil**, attached to the current pass or not. Cost if
  wrong: where mtld3d's encoder has no current depth attachment although one is bound (R1 reads which
  cases those are), the core resolves and mtld3d skips: a destination written where mtld3d leaves it.
- **R7a-4: a pair the core's copy or resolve does not take is skipped with a warning and `D3D_OK`.** For a
  multisampled source of another pixel format than the destination's (D24X8 or D16 into INTZ, if mtld3d's
  transfer converts those: R1 reads it) this is a difference. Cost if wrong: such a game samples a depth
  texture nothing wrote; the warning names both formats; 7d's parity pass decides whether a conversion is
  owed.
- **R7a-5: RESZ into a `D3DUSAGE_DYNAMIC` depth texture is refused by name.** Its CPU staging would upload
  over the resolve at the next bind unless the level is claimed for the GPU and read back at the next lock:
  a mechanism of its own, met by no suite. Cost if wrong: a game doing it gets `D3DERR_NOTAVAILABLE` from a
  `SetRenderState` and its log line; 7d decides whether it stays.
- **R7a-6: gamma is stored and reported, never applied**, as on mtld3d for a windowed device. Cost if
  wrong: none while the core makes windowed devices only; fullscreen's slice owes the presenter's ramp.
- **R7a-7: the cursor calls run without the window subclass.** A game that changes its cursor in its own
  `WM_SETCURSOR` loses mtld3d's re-assert; the software cursor overlay does not exist on core. Cost if
  wrong: a wrong or flickering pointer in such a game under the opt-in runtime; C4d's window work owes it.
- **P-7a: no probe; X-7a: no x64 check** (constraints above). Cost if wrong: a draw-cost change unseen
  until 7d's probe; a regression outside D3D9 unseen until 7d's check. Neither is expected from a diff that
  touches neither the draw path nor a file outside `src/d3d9`.

**Not ruled here, put to the maintainer:** the roadmap's 7b cannot do what it says inside C4c (map 3 and
7.2: the logger's thunks keep `mtld3d.so` on the core's path whatever 7b does, and memory pages on the Mac
need a new winemetal export). 7a does not depend on the answer.

## Stated gaps

- No row for `SetCursorPosition`; no row or mutant for the latency's enforcement; gamma's rows are its only
  evidence (no Wine test calls it).
- The cursor rows set the calling thread's Win32 cursor over the row's own window only.
- `GetAvailableTextureMem` does not count the `Ex` creators' surfaces on core, as it does not count the base
  creators' (documented in STATUS.md).

## Review dispositions

(after the three reviews)

## Execution notes

(after the gates)
