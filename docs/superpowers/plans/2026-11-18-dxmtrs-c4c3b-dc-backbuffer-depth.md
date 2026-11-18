# DXMTRS C4c-3b: device contexts, the writable back buffer and dynamic depth textures on D3D9's core runtime

Slice 3 of C4c, second half (ruling S-2 of C4c-3a's plan). Process: the C4 program's (code map, this plan, three
adversarial reviews folded before code, rows first, one commit a feature, full gates once, one whole-branch
review, spec, pin, push). Map: `build/c4c/s3b/map.md` (cited as "map A1", "map H2" ...; where a review corrected
it, this plan says so). The sweep that ranks what Wine's suites meet: `build/c4c/s3/sweep.md`. Reviews:
`build/c4c/s3b/review-{1-design,2-core,3-exec}.md` (cited "R1 f3" = review 1, finding 3). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`, with mtld3d's answers and rules:

- `IDirect3DSurface9::PreLoad` is the no-op it is on mtld3d (no refusal line);
- `IDirect3DSurface9::GetDC` / `ReleaseDC` work on every surface kind mtld3d serves: system-memory and plain
  surfaces, texture levels and cube faces (CPU stores, no GPU work), a render-target texture's level (3a's read
  arm), a lockable render target (its staging refreshed at `GetDC`, uploaded at `ReleaseDC`), the back buffer of
  a device made with `D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`; a non-lockable back buffer answers
  `D3DERR_INVALIDCALL` and leaves the out handle alone;
- the back buffer of a lockable device locks for writing: the lock reads the whole frame (`D3DLOCK_DISCARD` is
  not honoured, as on mtld3d), `UnlockRect` writes it back unless the lock was `READONLY`;
- an additional swap chain's back buffer locks, gives a device context, and is a `GetRenderTargetData` source,
  as the device's back buffer it aliases;
- a DYNAMIC depth-format texture (D16, D24X8, D24S8) locks, and what the lock wrote is what a later sample of
  the texture reads.

Wine's suites ask for the first three (`test_getdc`: 231 of `device`'s new failures under core; then
`test_lockable_backbuffer`, `d3d9ex`'s `test_user_memory_getdc`, `visual`'s `add_dirty_rect_test`); the depth
lock is asked for by parity alone (map G).

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main a1d45dd5 and MacNeutron main 288a0195 (C4c-3a). Fetch both origins before the
  first build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- **No file outside `src/d3d9` changes** in DXMTRS: not dxmt-core (`src/dxmt/src`), not its fakes, not
  `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal`, `src/nvapi`. If a task finds it needs one, it stops and
  reports.
- The default runtime (mtld3d) draws and answers exactly what it does today: its translator key, imports and
  conformance are gates. Every edit of a function mtld3d runs is a core arm under
  `cfg(feature = "core-runtime")` and a test that is false on an mtld3d device, ahead of code that stays as it
  is.
- **No mtld3d recorder call may be reachable on a core device.** On the paths this slice opens none is, even
  without the arms: every null-handle test comes before the flush, the capture and the push (R1, verified).
  That is the hazard: **every trap of this slice is silent** (a null colour handle answers `INVALIDCALL` or
  drops a write with one warning; `is_lockable_render_target()` is false on a core device; the depth branch
  creates and never uploads). So **each lifted call has a read cell and a write cell in a row** (the table in
  the Appendix), and the arms key on what is set on a core device (`core_texture`, `on_core_device()`), never
  on a colour handle.
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree.
- **Job scripts** live in `build/c4c/s3b/gates`, each copied from `build/c4c/s3/gates` and edited **before**
  its first run; a job that has run is never edited (a new job is a new file); jobs test committed tips.
  Before any s3b job runs, `/usr/bin/grep -rnE 'c4c/s3([^b]|$)|c4c3a|C4c-3a' build/c4c/s3b/gates` prints only
  lines the ledger lists as meant (`head.sh:2` and `post.sh:5` hold `S=$B/c4c/s3;` with no slash after it: left
  as they are, every s3b job would delete s3's evidence; R3 f1).
- **A mutant names a feature-gated item only under `#[cfg(feature = "core-runtime")]`** (C4c-3a's ruling T7-1),
  and every mutant is built once before the chain (`jmb`: builds only, each failed build's DXMT log kept); the
  chain's `jm` reuses those builds.
- **Wanted values come from the base:** control job `jb` builds the base plain and writes its translator key and
  imports list; `jr.sh` and `post.sh` read them.
- **No `dxmt/check.sh` run may cross 03:35.** A chain or a lone `jr` or `post` starts before 02:55 or after
  03:36. `jm`'s lanes run with `DXMT_CHECK_WORK` under `build/` (not `$TMPDIR`, which macOS empties at 03:35).
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper lands in the commit of
  its first caller; a function that loses its last caller is deleted in that commit (`refused_under_core` in
  Task 4, `core_refuses_lock` in Task 6). d3d9-rt is an rlib: its primitives are their own commits.
- One lane (default, core) per task that moves a row, where this plan names one; full gates once; no fix round
  for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-8. Out, with where it goes:

- **The static WRITEONLY index buffer's release** (ruling S-3): it needs a third primitive (a read of a
  device buffer, for a fan over a released backing) for a gain in memory only, which no row and no suite asks
  for. Core keeps one CPU copy of each static WRITEONLY index buffer; it goes to C4d with the memory probe.
- **A lockable back buffer with multisampling** (ruling 2): a writable lock and `GetDC` of a multisampled back
  buffer of a lockable device are refused by name. D3D9 forbids the pair at `CreateDevice`; mtld3d does not
  check (R1, verified: `device.rs:16603-16615`).
- **Lockable depth formats** (`D16_LOCKABLE`, `D32F_LOCKABLE`) and locks of depth-stencil surfaces: mtld3d
  serves none (`INVALIDCALL` on both runtimes); nothing to build. **Depth cubes** cannot be created.
- **A read of a depth texture back to the CPU**: unreachable on core (R1 traced every writer of `gpu_wrote`:
  `RESZ`, `StretchRect` and `ColorFill` into it are refused, a dynamic depth texture is never an attachment).
  The depth texture is CPU-authoritative; Tasks 5-6 are the upload alone.
- **C4c-4:** `StretchRect`'s other shapes, `ColorFill`, `UpdateSurface`. **C4c-6:** `Reset`,
  `IDirect3DSwapChain9::Present`. So `test_lockable_backbuffer` and `test_getdc` still hold failures after this
  slice, and **no conformance lane gates C4c-3b**; a sweep that gates nothing says what moved.
- The two silent differences the sweep found (a volume texture bound where a 2D one is sampled;
  `D3DSAMP_SRGBTEXTURE` for values other than 0 and 1): not locks; traced in the slice that touches binding.

## The mechanism

### d3d9-rt

One private helper copies rows into a staging-ring block (today's loop in `texture_upload`,
`rt/textures.rs:476-497`); `texture_upload`, `write_presented` and `depth_upload` all call it (R2 f2). Each of
the three records the same way: the ring allocation, `open_blit_pass` (a blit record handed to an open render
pass is guarded only by a `debug_assert` in dxmt-core), one `emit`, `staged_bytes` raised and a commit past the
staged budget.

- **`write_presented(bytes: &[u8], pitch: usize) -> bool`** (`#[must_use]`): the whole back buffer from CPU
  rows, in application order. Single-sample back buffer only (false, recording nothing, when there is a
  multisample companion: no copy line at all). Validated before anything is recorded (pitch at least
  `width * 4`, `bytes` at least `pitch * (height - 1) + width * 4`, a non-empty extent). `texture_upload`
  becomes "look the texture up, then the shared body"; the back buffer's texture is private storage with
  render-target usage, which a blit may write, and a Clear recorded before the write stays under it (R2 V1,
  V3). A core device's back buffer is always a 4-byte BGRA texel (R1, R2: verified), so `linear_row_pitch` of
  it is `width * 4`.
- **`depth_upload(id: TextureId, level: u32, planes: DepthPlanes<'_>) -> bool`** (`#[must_use]`), with
  `DepthPlanes { depth: &[u8], depth_pitch: usize, stencil: Option<(&[u8], usize)> }`: a whole level of a
  stored depth texture. **The layout rule is `PlaneLayout::new(w, h, 4)`** (depth rows `w * 4` bytes of
  `f32`, stencil rows `w` rounded up to 4: dxmt-core's initializer's numbers; the device's alignment query is
  not in the fakes, R2 f1); each plane is checked against its own pitch, and each blit's bytes-per-row is that
  pitch. One ring allocation holds both planes, the stencil plane at a 16-aligned offset. `Depth32Float`: one
  plain `COPY_FROM_BUFFER_TO_TEXTURE` record (what mtld3d does, `unix/src/metal/command.rs:2432-2458`; the
  fakes print every field of it). `Depth32FloatStencil8`: two `..WITH_BLIT_OPTION` records (options
  `DEPTH_FROM_DEPTH_STENCIL`, then `STENCIL_FROM_DEPTH_STENCIL`), built by a pure function
  (`plane_copies(..) -> [WmtcmdBlitCopyFromBufferToTextureWithblitoption; 2]`) whose fields a unit test reads
  directly, because the fakes print such a record as a bare `blit 10` (R2 f3). Refuses, recording nothing: an
  unknown id, a texture that is no depth texture, `samples > 1`, a level out of range or past `u16`, a short
  plane, a missing stencil plane for a stencil format. **It never goes through `texture_upload`** (that
  function's block is D3D9's texel size, 2 for D16, against Metal's 4, and it has no plane option).
  Encodable from d3d9-rt with no change under `src/dxmt` (R2 V7); the initializer's zeroing of a new depth
  texture cannot land after an upload (each chunk waits for the initializer's event, R2 V9).

### Front end

**The back buffer: two arms at two heads, and 3a's Unlock arm deleted** (R1 f1, f3). `is_lockable_backbuffer()`
reads the device's parameters and is true on a core device, so mtld3d's own call sites already route the
writable lock (`surface.rs:2879`), `GetDC` (`:3996`), `ReleaseDC` (`:4170`) and the Unlock tail (`:2781`) into
two functions; on core both fail silently on the null colour handle. The arms go at their heads:

- `readback_full_backbuffer`: on a core device, a zeroed page of the whole surface at `linear_row_pitch`,
  filled by `read_presented(DirtyRect::full(w, h), ..)`, stored in `inner.readback`; `None` (and one error
  line) when the read fails.
- `backbuffer_page_upload`: on a core device, `write_presented` of `inner.readback`'s page; one error line when
  there is no page or the write fails.

**3a's "spot 2"** (`surface_unlock_rect`'s core arm for a back buffer, `surface.rs:2761-2765`) **is deleted**:
it drops the page and clears the flags with no test of whether a lock was held, and once `GetDC` is lifted an
`UnlockRect` with a DC held would free the page the DC's DIB is built over. mtld3d's tail (`:2778-2787`) is
recorder-free on core once `backbuffer_page_upload` has its arm, and already has the gates: the mapped flag
read before the lock ends, `lock_flags` taken before they are cleared, the page dropped only when a lock was
held. A `READONLY` lock's page (3a's rect read) is dropped by the same tail.

**The multisample refusals** sit at the call sites, after mtld3d's state checks, so every `INVALIDCALL` mtld3d
gives comes first (R1 f6): `IDirect3DSurface9::LockRect (a writable lock of a multisampled back buffer)` where
3a's writable refusal stands (after the non-lockable `INVALIDCALL`; a core device, not `READONLY`, more than
one sample), `IDirect3DSurface9::GetDC (a multisampled back buffer)` inside `GetDC`'s lockable-back-buffer
test. A non-lockable multisampled device keeps `INVALIDCALL` on both. 3a's two refusals (the writable lock,
the additional chain) go.

**An additional swap chain's back buffer** then takes the same paths (the surface aliases the device's back
buffer and reads the device's size, format, samples and lockable flag; R1 traced it). `GetRenderTargetData`'s
refusal for it (`core_arms.rs:175-180`) goes: what follows it is the device back buffer's path, with the
multisample `INVALIDCALL` first. `StretchRect`'s alias test stays (C4c-4).

**Device contexts.** The three `refused_under_core` lines go (`PreLoad`, `GetDC`, `ReleaseDC`) and the function
with them. One helper answers "a lockable render target of a core device" (a core texture, staging, not
depth-stencil: 3a's lock arm's test, moved onto it); where `GetDC` and `ReleaseDC` test
`is_lockable_render_target()` (its only two callers; false on core) they also test the helper: `GetDC`
refreshes the staging through `lockable_rt_readback_fill` (3a's core arm serves it), `ReleaseDC` calls
`core_lockable_rt_upload`. Every other kind runs mtld3d's code as it is: CPU stores, 3a's arm where a
render-target level or cube face is read, `publish_render_target_write`'s arm where one is written; no staging
is renamed or dropped on core (R1 traced `prepare_subresource_for_dc`). The error lines of 3a's helpers say "a
CPU read of" / "a CPU write-back of", since `GetDC` and `ReleaseDC` now reach them (R1 f7).

**The dynamic depth texture.** The two refusals and `core_refuses_lock` go; the lock and unlock are mtld3d's
(packed CPU staging, dirty marks). `core_flush_dirty`'s depth branch, after it creates the core texture,
uploads **each level that carries a dirty mark** (a level never written keeps the initializer's zeros, which
is what its zeroed staging holds; R2 f9): `PlaneLayout::new(w, h, 4)`, `PlaneLayout::unpack` (the shared rule
mtld3d's encoder uses) into the two planes, `depth_upload`, then the mark cleared and
`was_uploaded[level]` set (so a `READONLY` re-lock uploads nothing, as on mtld3d; R1 f5). A failed upload
keeps the mark and logs once a texture (`log_once_warn_by!` keyed by the id, as the colour branch). The branch
runs at a draw's sampling bind, ahead of the draw, closing the open pass, as colour textures do (R1 item 6).
Whole levels, not dirty rects (ruling 6). The branch's and the function's stale comments are rewritten.

## Review Focus (for the final review)

- `UnlockRect` of a back buffer while a DC is held on it: answers mtld3d's result, keeps the page, writes
  nothing; `ReleaseDC` then writes back (the fault R1 f1 found in the draft).
- The lock and DC state machines on every failure path of the arms (`try_begin_lock` / `try_end_lock`,
  `lock_flags`, `readback`, `dc_in_use`, `map_count`, the DC's holder): a failed read leaves the surface
  unlocked and no DC; a failed write-back leaves it unlocked.
- No mtld3d recorder call reachable on a core device from any entry point this slice opens; the default
  runtime's functions textually unchanged outside the arms.
- `write_presented` and `depth_upload`: bounds before recording, the shared ring helper unchanged for
  `texture_upload`'s callers, the pass after a write loads.
- A dynamic depth texture bound and sampled, then locked and written again: the second write reaches the
  sample; a multi-level one: each level's planes go to its own level.
- Two core devices in one process (the rows' second device): d3d9-rt state that is per process, not per
  device.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Rows in `dxmt/tests/d3d9_triangle.cpp` (Appendix). **Rules for every new cell:**

- a lock or a DC is released before its pixels are judged; every drawn-pixel cell runs in `until()` with its
  objects made inside the attempt; colours have every channel non-zero and are pairwise distinct within a row;
  no cell judges an out structure after a failed call;
- **GDI** (R3 f15): `GetPixel` answers 0x00BBGGRR, the rows' colours are 0x00RRGGBB: compare against
  `RGB(c >> 16 & 255, c >> 8 & 255, c & 255)`, and `FillRect`'s brush takes that same value; GDI writes alpha
  0, so every cell after GDI masks to 0xffffff; an R5G6B5 colour is exact in 5-6-5 (channels of the expanded
  form) and its lock-side cell compares the packed `WORD`;
- **the program links gdi32** (`GetPixel`, `CreateSolidBrush`, `DeleteObject`): `-lgdi32` in the four rules of
  `Makefile` (77, 87, 93, 104), and Task 1 **links** the four programs into the scratchpad with the Makefile's
  own command lines (not make): a syntax-only compile does not see a missing library (R3 f3);
- **the lockable device is a second device made inside the row** (R3 f4, f13): from the global `direct3d` (a
  forward declaration: it is declared after `targetRows`), on a window of its own, 256x256,
  `BackBufferFormat = D3DFMT_X8R8G8B8`, `D3DSWAPEFFECT_COPY` (a first draw is never left out),
  `Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER`. The helpers read the globals `dev` and `rtSurf`: the row
  points them at the second device through a scoped guard declared after the device's `Com<>`, so it restores
  them before the device is released, on every return. `until()` then presents the main device; nothing is
  presented on the second. **`lb-lock` and `lb-dc` are the last two rows of `targetRows`.**
- budgets: if `targets` or `depth` needs more time, the three places move together
  (`d3d9_triangle.cpp:3406` and `:4026`, `dxmt/d3d9-check.sh:222, 226`, `vm/tests/d3d9.sh:118-121`).

Scripts (R3's list): `dxmt/d3d9-check.sh` (prose 46-52 and 149-156; `depth_refusals` 160-161 keeps only the
`Draw*PrimitiveUP` line; targets list 223-225 gains `dc-mem dc-tex dc-rtex dc-rt dc-bb rt-chain lb-lock
lb-dc`; depth list 227-229 `ds-lock` → `ds-dynlock`), `vm/dxmt/d3d9-gate.sh` (prose 37-43; lists 174-176 and
181-183; `runtime`'s comment 223-232; `depth_refusals` 236-237), `vm/tests/d3d9.sh` (prose 17-21),
`d3d9_triangle.cpp`'s header 43-58 and the two row lists. `sh -n` on the scripts.

Job scripts in `build/c4c/s3b/gates`, from s3's: every `S=` and header line (the guard above); state names
`c4c3b-*`; `jv0` loops over `targets depth` and its prose names the new rows; `jcr` runs old **and new** WoW64
(GDI over `D3DKMTCreateDCFromMemory` and a second device are new there; R3 f18); `jt` takes an optional
validation launch (`jq.sh`'s loop body for one launch kind); `jmb` new (builds every mutant, keeps each
`red-<x>` folder and each failed build's DXMT log); `jm` from `jm2.sh` with `OLD` = every letter taken from
`gates/jmb/red-<x>`, building the tip alone, the count and the `P` map below, `DXMT_CHECK_WORK` under its own
folder; a chain in the order `jr`, `jc`, `jq`, `jm`, `jw`.

Controls at the base + the rows, each failing set written in the ledger before its run:
- `jb`: the base built plain: key and imports written.
- `ja`: default and msl: **every row green on mtld3d at the first run, no expectation corrected** (a row red on
  mtld3d is a wrong row or a finding: fixed or dropped before any code, and the ledger says which;
  `ds-dynlock` is the first exercise of mtld3d's CPU-written dynamic depth path, R3 item 1). Core, the failing
  tokens predicted: `targets-dc-mem targets-dc-tex targets-dc-rtex targets-dc-rt targets-dc-bb
  targets-rt-chain targets-lb-lock targets-lb-dc targets-runtime depth-ds-dynlock depth-runtime` and nothing
  else (R3 derived the same eleven).
- `jv0`: the `targets` and `depth` launches on mtld3d under Metal's validation layer: 0 reports inside the new
  rows.
- `jcr`: the VM's default lane on old and new WoW64.

Commit: "dxmt, vm: d3d9_triangle rows for device contexts, a lockable back buffer, an additional swap chain's
back buffer and a dynamic depth texture's lock (DXMTRS C4c-3b)".

### Task 2: `write_presented` (d3d9-rt)

As "d3d9-rt" above, with the ring helper. Tests on the fakes as they are (R2 f5): the back-buffer twin of 3a's
"Clear, upload, draw" test (`core_and_back_buffer` moves to `fixture.rs`): two passes, the first clearing, the
second loading, the copy's destination the back buffer's handle and between them; a pitch over the row asserts
the copy line's `bytes_per_row = width * 4` (the bytes themselves are the rows' proof); a multisampled back
buffer, a short buffer and a pitch under the row answer false with **no copy line at all**;
`texture_upload`'s existing exact-line asserts hold. Commit: "d3d9: CPU rows into the back buffer on the core
runtime".

### Task 3: the lockable back buffer and an additional swap chain's (front end)

The two arms at the two heads, spot 2 deleted, 3a's two lock refusals replaced by the multisample one,
`GetRenderTargetData`'s chain refusal removed, the helpers' error wording. `GetDC` stays refused whole. Lane
`jt3` (default, core): `lb-lock` and `rt-chain` green under core; failing tokens predicted: `targets-dc-mem
targets-dc-tex targets-dc-rtex targets-dc-rt targets-dc-bb targets-lb-dc targets-runtime depth-ds-dynlock
depth-runtime`. Commit: "d3d9: a lockable back buffer locks for writing on the core runtime, and an additional
swap chain's back buffer as the device's".

### Task 4: device contexts and PreLoad (front end)

The three refusal lines and `refused_under_core` deleted; the lockable-target helper with its three callers
(3a's lock arm, `GetDC`, `ReleaseDC`); `GetDC`'s multisample refusal. Lane `jt4` (default, core): every
`targets` row green strict under core and the launch's refusal set exact; failing tokens predicted:
`depth-ds-dynlock depth-runtime`. Commit: "d3d9: device contexts on the core runtime".

### Task 5: `depth_upload` (d3d9-rt)

As "d3d9-rt" above. Tests: `Depth32Float` on the fakes, every field of the copy line (destination, level,
size, bytes-per-row); `plane_copies` directly (options 1 then 2, each plane's pitch, the stencil's source
offset, the level, slice 0, the level's size) at widths 3 and 5 and at a 1x1 level; `Depth32FloatStencil8` on
the fakes: two `blit 10` lines in one blit block between the render passes; each refusal records nothing; an
upload recorded while a render pass is open closes it. Commit: "d3d9: a depth texture's planes uploaded from
the CPU on the core runtime".

### Task 6: the dynamic depth texture (front end)

The two refusals and `core_refuses_lock` removed, `core_flush_dirty`'s depth branch uploads, the comments.
Lane `jt6` (default, core) at the tip: every row green strict under core, every launch's refusal set exact,
**and the `depth` launch once under core with Metal's validation layer**. Commit: "d3d9: a dynamic depth
texture locks on the core runtime".

**Stop rule (ruling 7).** The one trigger: a Metal validation report from a depth blit in `jt6`'s validation
launch (`Depth32Float`, or either plane of `Depth32FloatStencil8`) that a change inside `src/d3d9` does not
remove. "The fakes cannot show it" is no trigger, and a `ds-dynlock` cell red under core is a fault to fix.
If it fires: Tasks 5 and 6's commits are reverted, and one MacNeutron commit restores `ds-lock`,
`depth_refusals` and the prose and row lists of the three scripts; mutants h-j are dropped (`jm`: seven
letters); the depth lock stays refused by name and goes to a slice of its own.

### Task 7: gates

- `jmb` before the chain: every mutant builds.
- `jr`, `jc`, `jq` as C4c-3a's (`jq`'s validation run is the gate that the back-buffer write and the depth
  blits are legal Metal: 0 reports).
- `jm`: mutants as commits off the tip, each red on its named row or rows and on nothing else;
  `gates/mutants-expect.txt` names, before the chain runs, the cell and the texel or answer that changes. The
  map (`letter:launch:rows`):
  a. `targets:dc-rt`: `GetDC` does not refresh a lockable target's staging (the mutation in `GetDC`'s arm, not
     in the shared helper) → `GetPixel` reads the colour the earlier lock left in the staging;
  b. `targets:dc-rt`: `ReleaseDC` uploads nothing for a lockable target (in `ReleaseDC`'s arm) → the GDI block
     is missing from the target;
  c. `targets:lb-lock,lb-dc`: `readback_full_backbuffer`'s core arm answers its zeroed page without reading
     (the mutation in the arm, never in `read_presented`, which `rt-bblock`, `rt-front` and the msaa rows
     share) → the lock and the DC read zeros;
  d. `targets:lb-lock,lb-dc`: `backbuffer_page_upload`'s core arm writes nothing → the block is missing after
     the Unlock and after `ReleaseDC`;
  e. `targets:lb-lock`: the Unlock tail writes back whatever the lock's flags (under the feature's `cfg`) →
     the whole `READONLY` lock's scribble reaches the back buffer;
  f. `targets:lb-dc`: the Unlock tail drops the page when no lock was held (3a's spot 2's fault, under the
     feature's `cfg`) → after `GetDC`, `FillRect`, `UnlockRect`, `ReleaseDC` the block is missing;
  g. `targets:lb-lock`: the writable lock skips its read under `DISCARD` → outside the small block the
     `DISCARD` cell reads zeros where the frame is wanted;
  h. `depth:ds-dynlock`: `core_flush_dirty`'s depth branch records no upload → both halves read the zeroed
     texture: the 0.75 half wants lit and shows unlit;
  i. `depth:ds-dynlock`: the depth branch uploads only when it creates the core texture → the swapped state's
     0.75 half shows unlit;
  j. `depth:ds-dynlock`: the depth branch uploads level 0's planes whatever the level → the level-1 cell.
  Not gated by a mutant, stated: the multisample refusals (no row makes that device); the additional chain
  (its row is the gate: the arms are the device's); `PreLoad` (a `void` call: `targets-runtime` fails on its
  refusal line); the depth planes' row pitch (the quadrant and narrow-level cells are its rows; no mutant: a
  wrong pitch may fault in Metal, not draw).
- `jw` (gates nothing): the sweep → `build/c4c/s3b/sweep.md`: `test_getdc`, `test_lockable_backbuffer`,
  `test_user_memory_getdc`, `add_dirty_rect_test` before and after.
- **Pre-ruled, no rerun for them** (memory): the VM or Mac `visual` count 211159 with failed 29 and
  `visual.c:25196` at 0; the hazards program's reference run printing "fence-order 1 0"; "presents to a
  minimised window" when HID idle time at the failure is under ten minutes. A build that died on "A timestamp
  was expected but was not found" or "The timestamp service is not available" is run again. Anything else red
  is investigated before the push.
- **No probe** (ruling 8) unless the range touches `rt/src/device/{draw,recorder}.rs` or
  `src/device/core_draw.rs`.
- No D3D10-12 gate while `git diff --stat <base>..<tip>` shows no file outside `src/d3d9`.

### Task 8: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s3b/review-final.md`), given the diff and this plan; Critical and
Important fixed with a red cell first where a pixel can show it; Minors fixed if one line, else ledgered. Spec
§5 paragraph "C4c-3b" and the slice list (the index buffer's release moves to C4d). Plan execution notes.
`dxmt/pins`. Privacy scan of both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron.
Post-push check (not across 03:35). CI. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none. **A wire field:** none. **A dependency:** `gdi32` for the row
  program alone. **A lint suppression:** none (`depth_upload` takes its planes as one struct). **A derive:**
  `Clone, Copy` on `DepthPlanes` if it is passed by value (inventoried).
- **New state:** none on the device or on a surface (`readback`, `lock_flags`, the DC fields, the texture's
  dirty marks and `was_uploaded` are mtld3d's, now also used by the core arms).
- **New pub items:** `write_presented`, `depth_upload`, `DepthPlanes`, each with its doc. **Unsafe:** the page
  slice of `readback_full_backbuffer`'s arm, with a SAFETY comment, one operation a block; the page is zeroed
  before a slice of it exists.
- **A duplicate:** none meant. One ring helper for the three uploads; one lockable-target helper for the lock
  arm and the two DC arms; the back buffer's whole read and write-back are one arm each, reached through
  mtld3d's own call sites; `PlaneLayout` is called, not copied.
- **Not silent:** a read, a write-back or a depth upload that fails logs an error in its arm (the depth one
  once a texture); every refusal has its name; the gates fail on an unexpected refusal line. **New refusal
  literals:** `IDirect3DSurface9::LockRect (a writable lock of a multisampled back buffer)`,
  `IDirect3DSurface9::GetDC (a multisampled back buffer)`. **Removed:** `IDirect3DSurface9::PreLoad`,
  `IDirect3DSurface9::GetDC`, `IDirect3DSurface9::ReleaseDC`, `IDirect3DSurface9::LockRect (a writable lock of
  the back buffer)`, `IDirect3DSurface9::LockRect (an additional swap chain's back buffer)`,
  `IDirect3DDevice9::GetRenderTargetData (an additional swap chain's back buffer)`,
  `IDirect3DTexture9::LockRect (a depth texture)`, `IDirect3DTexture9::UnlockRect (a depth texture)`.
- **Divergence added:** (1) a draw recorded between a back buffer's writable `LockRect` / `GetDC` and its
  `UnlockRect` / `ReleaseDC`: mtld3d's write-back rides the frame's leading blits and lands under the draw;
  the core's is in application order and lands over it (D3D9 leaves the case undefined; R1 f2). (2) a dirty
  depth level is uploaded whole where mtld3d uploads its dirty rect: bytes written under
  `D3DLOCK_NO_DIRTY_UPDATE` outside a later marked rect reach the GPU on core and not on mtld3d (ruling 6).
  **Kept, mtld3d's and now the core's too:** a writable back-buffer lock reads the frame under `DISCARD`; a
  lockable flag on an additional chain's parameters is not read (the device's is); `ReleaseDC` of the back
  buffer writes the whole frame back.
- **Docs in the same change:** DXMTRS's `STATUS.md`, `COVERAGE.md`, `CONFORMANCE.md` and `mtld3d.conf` say
  nothing of the core runtime: no edit owed; Task 8 confirms it against the final diff.

## Rulings (planning)

- **S-3. The static WRITEONLY index buffer's release leaves C4c-3** for C4d. Why: a third primitive for memory
  alone. Cost if wrong: under core one CPU copy of every static WRITEONLY index buffer stays for its life (in
  a 32-bit title, address space mtld3d gives back).
- **1. The lockable device of the rows is a second device inside a row,** not a new launch. Why: a launch
  ripples into both gate scripts' refusal sets and row lists, the VM gate, the validation and mutant jobs.
  `state-late` proves only that a second device can be made under core; that it draws, locks and reads back
  beside the first is what `ja` (mtld3d) and `jt3` (core) show first. Cost if wrong: `lb-lock` and `lb-dc`
  move to a launch of their own, with those ripples.
- **2. A multisampled lockable back buffer is refused by name** for the writable lock and `GetDC`. Why: D3D9
  forbids the pair; on mtld3d the write lands in the single-sample texture and the next resolve overwrites it;
  a companion cannot take a blit. Cost if wrong: a title that makes such a device gets `NOTAVAILABLE` where
  mtld3d answers and then loses the write.
- **3. The back buffer's arms are at the heads of mtld3d's two functions, and 3a's Unlock arm goes.** Why: the
  call sites and their gates are then mtld3d's by construction, the page-lifetime fault cannot be written,
  and the lock and the DC share one read and one write-back. Cost if wrong: a caller of either function that
  should not reach the core (there is none today: the lock, `GetDC`, `ReleaseDC`, the Unlock tail).
- **4. `ReleaseDC` of the back buffer always writes back,** and the writable lock reads under `DISCARD`
  (mtld3d's rules). Cost if wrong: an upload for a DC that only read; a read a title asked to skip.
- **5. The DC arms test a helper beside `is_lockable_render_target()`**, which is left as it is (its two
  callers are these two; a core term inside it would do the same; the helper is also the lock arm's test).
  Cost if wrong: none in behaviour.
- **6. A dirty depth level is uploaded whole.** Why: one path, and the staging is CPU-authoritative. Cost if
  wrong: a title that rewrites a few texels of a large dynamic depth texture every frame uploads all of it;
  and the divergence stated above.
- **7. Depth is last and may leave the slice** (Task 6's stop rule). Why: it is the one piece Metal has not
  yet accepted from this crate and the one no suite asks for. Cost if wrong: one more slice.
- **8. No probe.** The slice adds no work to a draw; `core_flush_dirty`'s depth branch does work only for a
  depth texture with a dirty mark. Cost if wrong: a cost at a bind nobody measured.
- **9. No conformance lane; one sweep that gates nothing.** Cost if wrong: none; the lane cannot pass before
  C4c-4 to 7.

## Stated gaps

- Every writable back-buffer lock and every `GetDC` of a back buffer or a lockable target is a whole read, a
  commit and a wait, and a whole upload after. A 4K lock or DC holds three copies of the frame at once (the
  page, 3a's grow-only read-back buffer, the ring block: about 100 MB of a 32-bit process's address space),
  and each write makes a ring block of its own that is not reused. A 4096x4096 D24S8 level is 80 MiB unpacked
  in the front end and again in the ring (R2 f8).
- The rows' device contexts are over X8R8G8B8, A8R8G8B8 and R5G6B5 stores; R8G8B8 and the 15-bit formats take
  the same path with no row.
- A dynamic depth texture's stencil plane has no pixel that shows it (a dynamic depth texture is never a
  depth-stencil target): the D24S8 cells prove the depth plane of the combined format and that Metal accepts
  both blits (`jq`, `jt6`), not the stencil bytes.
- No row: a lockable device with multisampling; a lock or a DC held across Present; `GetDC` on a device whose
  window is hidden; a draw between a back buffer's lock and its unlock (the divergence above); the VM's
  forwarding of the optioned blit records beyond what `jc`'s depth rows show.

## Appendix: the rows

Every lifted call against the cell that is red if the lift is a silent no-op:

| lifted call | read cell | write cell |
|---|---|---|
| `GetDC` / `ReleaseDC`, system memory, DEFAULT plain, a MANAGED cube face | `dc-mem` (`GetPixel` of what a lock wrote) | `dc-mem` (a lock reads what GDI filled) |
| the same, a MANAGED texture level | `dc-tex` (`GetPixel`) | `dc-tex` (the quad drawn again shows GDI's colour) |
| the same, a render-target texture level | `dc-rtex` | `dc-rtex` |
| the same, a lockable render target | `dc-rt` (stale staging shows) | `dc-rt` |
| the same, the lockable back buffer and the chain's | `lb-dc` | `lb-dc` |
| `GetDC`, a non-lockable back buffer | `dc-bb` (the answer) | |
| the writable back-buffer lock | `lb-lock` | `lb-lock` |
| an additional chain's lock and `GetRenderTargetData` | `rt-chain` | `lb-lock`'s chain cell |
| the dynamic depth lock, the level surface's lock | `ds-dynlock` (CPU-authoritative: the write is the cell) | `ds-dynlock` |
| `PreLoad` | `targets-runtime` (its refusal line) | |

`targets` launch (the device has no lockable flag):

- **`dc-mem`**: a 64x64 X8R8G8B8 `CreateOffscreenPlainSurface(SYSTEMMEM)`. A block written through
  `LockRect`; `PreLoad()`; `GetDC`: `GetPixel` inside the block reads its colour; `FillRect` a second block; a
  second `GetDC` answers `INVALIDCALL`; `LockRect` with the DC held answers `INVALIDCALL`; `ReleaseDC`; a
  `READONLY` lock reads the second block's colour where GDI drew and the first's elsewhere. The same read and
  write cells on: an R5G6B5 system-memory surface; a DEFAULT-pool plain surface; a face of a 64-texel MANAGED
  cube. An A2R10G10B10 system-memory surface: `GetDC` answers `INVALIDCALL` and leaves the handle.
- **`dc-tex`**: a 64x64 MANAGED X8R8G8B8 texture, level 0 written by a lock and drawn once (so it is
  uploaded); `GetDC` of the level's surface: `GetPixel` reads the lock's colour; `FillRect`; `ReleaseDC`; the
  textured quad drawn again shows GDI's colour (the dirty mark `ReleaseDC` sets), read through
  `GetRenderTargetData`.
- **`dc-rtex`**: a 64x64 render-target texture's level 0: Clear, a triangle; `GetDC`: `GetPixel` in the
  triangle and in the corner read their colours; `FillRect`; `ReleaseDC`; `GetRenderTargetData` shows the
  block, the triangle and the corner.
- **`dc-rt`**: a 64x64 lockable render target. A colour X written over it through a lock (so the staging
  holds X); Clear, a triangle; `GetDC`: `GetPixel` in the triangle and in the corner read the triangle's and
  the Clear's colours, not X; `FillRect`; `ReleaseDC`; a second triangle drawn after; `GetRenderTargetData`
  shows the block, both triangles and the corner.
- **`dc-bb`**: the device's back buffer: `GetDC` answers `INVALIDCALL` with the handle left at its sentinel;
  `ReleaseDC` of a null handle answers `INVALIDCALL`.
- **`rt-chain`**: an additional swap chain (the device's size); the device's back buffer drawn (Clear, a
  triangle); a `READONLY` lock of the chain's back buffer reads the triangle and the corner; a writable lock
  answers `INVALIDCALL` (no lockable flag); `GetRenderTargetData` of the chain's back buffer into a
  system-memory surface shows the same two texels.
- **`lb-lock`** (the second device). Clear, a triangle. `LockRect(NULL, 0)`: the triangle's texel and the
  corner's read their colours, a block written, `UnlockRect`; a second triangle drawn; `GetRenderTargetData`:
  the block, both triangles, the corner (the pass after the write loads). `LockRect(NULL, DISCARD)`, one small
  block written, unlock: every other region keeps its colour. `LockRect(&{8,8,16,16}, 0)`: a texel written at
  the pointer lands at (8,8) and (0,0) keeps its colour. **A whole `READONLY` lock (`NULL` rect)** with a
  scribble at a named texel in a colour of its own: after the unlock that texel keeps the frame's colour (a
  rect lock's page is the rect's and could not be written back whole: R3 f7). A second lock while locked and
  an empty rect answer `INVALIDCALL`. An additional chain on this device: a writable lock of its back buffer
  writes a block the device's back buffer then shows.
- **`lb-dc`** (the second device): Clear, a triangle; `GetDC` of the back buffer: `GetPixel` in the triangle
  and the corner; `FillRect`; `ReleaseDC`; `GetRenderTargetData` shows the block, the triangle and the corner.
  Then `GetDC`, `FillRect` of a second block, **`UnlockRect` (answers `D3D_OK`, no lock held)**, `ReleaseDC`:
  the second block is in the back buffer. Then the additional chain's back buffer: `GetDC`, `FillRect` of a
  third block, `ReleaseDC`: the device's back buffer shows it.

`depth` launch:

- **`ds-dynlock`** (replaces `ds-lock`): 64x64 DYNAMIC textures in D16, D24X8 and D24S8. **mtld3d's packings**
  (`core/src/depth_texture.rs:53-57`; R3 f8), with the codes for depth 0.25 and 0.75: D16 one little-endian
  `WORD`, 0x4000 and 0xBFFF; D24X8 depth in bytes 0-2, `DWORD = 0xEE000000 | 0x400000` and `| 0xBFFFFF` (byte
  3 not zero); D24S8 stencil in byte 0 and depth in bytes 1-3, `DWORD = 0x400000 << 8 | 0xA5` and
  `0xBFFFFF << 8 | 0xA5`. `sampleAt` gains a `v`; every sample is at a texel centre on an odd row (R3 f10).
  One `frames()` per (format, state), the texture made before it, the lock and the write inside its draw
  (idempotent across retries), Z cleared in the draw. For each format: the left half written at 0.25 and the
  right at 0.75, sampled with reference 0.5 as the launch's shadow rows sample: the 0.75 half lit, the 0.25
  half not; then locked again with the halves swapped: swapped. On D24X8: the top-left quadrant rewritten at
  0.75 over 0.25 **through `GetSurfaceLevel(0)->LockRect` with a rect**: a top and a bottom texel of the left
  half now differ (the cell that varies in y). On a D24S8 texture with a full mip chain: level 1 written with
  the halves swapped against level 0 and sampled with the sampler held to level 1; the 2x2 level written
  0.25 | 0.75 and sampled held to it (a level whose width is under 4: the stencil rows' padding). If mtld3d
  cannot hold the sampler to a level of a depth texture, the two level cells are dropped at `ja`, the ledger
  says so, mutant j goes with them, and "a level other than 0" joins the stated gaps.

## Review dispositions

Three Opus reviews of the draft (`build/c4c/s3b/review-{1-design,2-core,3-exec}.md`): R1 1 Critical, 3
Important, 7 Minor; R2 0 Critical, 4 Important, 5 Minor; R3 0 Critical, 12 Important, 7 Minor. All folded.

- **R1.** f1 (Critical: 3a's back-buffer Unlock arm frees the page a held DC draws into once `GetDC` is
  lifted) and f3 (the back buffer's arm sites unnamed, each a silent misbranch) → ruling 3: arms at the heads
  of `readback_full_backbuffer` and `backbuffer_page_upload`, spot 2 deleted, the `UnlockRect`-with-a-DC cell
  in `lb-dc`, mutant f, the first Review Focus line. f2 → "Divergence added" (1). f4 (Task 2's interim refusal
  had no predicate, and its lane could not tell a refusal from a misbranch) → no interim state: the tasks are
  reordered so `GetDC` is lifted once, with every arm in place (Task 4), and ruling 5 reworded (the map's
  "other callers" and "`lockable_rt_upload` panics" were wrong). f5 → `was_uploaded` set, divergence (2). f6
  → the refusals' places. f7 → wording. f8 → logged once a texture. f9 → `core_refuses_lock` and the
  comments. f10 → the chain's `GetDC` cell, the level cells. f11 → the 16-bit follow-up dropped (no back
  buffer is 16-bit).
- **R2.** f1 → `DepthPlanes` carries its pitches, the layout rule is alignment 4, narrow-width unit tests and
  the 2x2 level cell. f2 → the recording body stated, one ring helper. f3 → the plain record for
  `Depth32Float`, `plane_copies` tested directly. f4 → the stop rule's one trigger and `jt6`'s validation
  launch. f5 → Task 2's tests. f6 → as R1 f8. f7 → the two refusals. f8 → Stated gaps. f9 → "each level that
  carries a dirty mark" and why.
- **R3.** f1 → the guard's pattern, `head.sh:2` and `post.sh:5` named. f2 → the table; the read cell in
  `dc-tex`, the DEFAULT plain surface and cube face in `dc-mem`, the chain's `GetDC`, the level surface's
  lock; `PreLoad` stated. f3 → `-lgdi32` and a link in Task 1. f4, f13 → the second device's rule (the global
  `direct3d`, the scoped guard, last in the list, `D3DSWAPEFFECT_COPY`, the format); ruling 1 reworded. f5,
  f6, f9 → the mutants rewritten (e and f are the Unlock tail's two gates; g is the `DISCARD` read; h, i, j
  pinned to the core's depth branch; the old "D16 as D24" was h under another name). f7 → the whole
  `READONLY` lock. f8 → the packings with their codes. f10 → `sampleAt`'s `v`, odd-row texel centres, one
  `frames()` a state, the Z clear, the quadrant in y. f11 → the stop rule's revert list and its job. f12 →
  `jm` reuses `jmb`'s builds, the map. f14 → the predicted tokens stand; no interim literal is left. f15 →
  the GDI rules. f16 → `core_refuses_lock` in Task 6. f17 → the three budget places. f18 → `jcr` on new WoW64
  too, `DXMT_CHECK_WORK` for `jm`. f19 → the refusals' places, the copied lines.
