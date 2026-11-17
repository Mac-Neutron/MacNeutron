# DXMTRS C4c-2b: dynamic buffers mapped directly on D3D9's core runtime, the WRITEONLY backing's release, the retained-bytes gauge and its cap

Status: draft, before its three adversarial reviews. Parent: `2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4), spec §5
(`C4c's slices`), the C4 work-list `build/c4/map2-sync-worklist.md` §1.5 and §1.7. Map: `build/c4c/s2b/map.md`
(cited "map §n"). Ledger: `.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`:

- a default-pool `D3DUSAGE_DYNAMIC` vertex or index buffer is mapped directly, as D3D11's dynamic buffers are:
  `Lock` answers a pointer into memory the GPU reads, a draw uploads nothing and splits no pass, and
  mtld3d-core's `plan_lock` decides between writing in place and a new name. The `dyn` bench's 54 ms of CPU a
  frame (22 times mtld3d's) is the figure this moves;
- a static default-pool `D3DUSAGE_WRITEONLY` vertex buffer releases its CPU backing once uploaded, by mtld3d's
  rule (C4c-1's kept divergence goes);
- the bytes the GPU side still holds and the process cannot reuse (renamed-away names, committed staging) are
  counted, and at `memory.vbibRetentionCapMB` the API thread waits for the oldest instead of growing.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main a4c94873 and MacNeutron main 844be686 (C4c-2a). Fetch both origins before the first
  build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- No line of `src/dxmt/src` (dxmt-core), `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal` changes. If a task
  finds it needs one, it stops and reports (the D3D10-12 gate would then be owed). `src/dxmt/fakes` is test
  support and may change.
- The default runtime (mtld3d) draws exactly what it draws today: its translator key, imports and conformance
  are gates. The only mtld3d-core change is additive (a constructor of `BufferBacking`).
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree. Job scripts live in `build/c4c/s2b/gates`, each copied from `build/c4c/s2/gates` and
  edited (`S=$B/c4c/s2b` in `head.sh` first of all, the VM state names `c4c2b-*`) **before** its first run; an s2
  script is never run as it is; a job that has run is never edited; jobs test committed tips.
- **Wanted values come from the base:** control job `jb` builds the base plain and writes its translator key and
  imports list; `jr.sh` and `post.sh` are written after it.
- **No `dxmt/check.sh` run may cross 03:35** (macOS's dirhelper empties the staged D3DMetal reference): a chain
  is started so that its x64 check ends before, or starts after.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper or constant lands in the
  commit of its first caller (the front end is a staticlib: dead code fails the gate); if the gate refuses
  d3d9-rt's new pub functions without a caller, Tasks 4 and 5 are one commit.
- One lane (default, core) per task that moves a row; full gates once; no fix round for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-7 below. Out, with where it goes:

- Read-back of a buffer (an index buffer whose backing was released, read by a fan): C4c-3. Index buffers keep
  their backing here (ruling 7).
- A trim of `DynamicBuffer`'s free names, a gauge inside dxmt-core: dxmt-core changes, not here (Stated gaps).
- mtld3d's perf counters for renames under core: C4d with the other counters.
- `DYNAMIC` buffers of other pools: staged, as mtld3d's `classify_map_mode` says.

## The mechanism (what is ported, and from where)

**The model is D3D11's `Map` of a dynamic buffer** (`d3d11/src/resource/buffer.rs:70-107`,
`context/map.rs:97-140`; map §4), with D3D9's lock rules from mtld3d-core (`buffer_rename.rs` `plan_lock`,
`classify_map_mode`; map §5).

*Creation.* For a buffer `classify_map_mode` calls `Direct` on a core device, the front end asks d3d9-rt for a
dynamic buffer at `CreateVertexBuffer` / `CreateIndexBuffer`: `Buffer::new(length)` (the logical length),
`allocate(flags)`, `rename`, `DynamicBuffer::new`. Flags: `SUBALLOCATE_FROM_ONE_PAGE`; `CPU_WRITE_COMBINED` when
the buffer is `D3DUSAGE_WRITEONLY`; `CPU_PLACED` when `cfg!(target_arch = "x86")`, D3D11's expression (a 64-bit PE
dereferences Metal's contents pointer on the Mac and the host hook's pages in the VM; a 32-bit one its own placed
pages: `dxmt/src/resource.rs:66-106`, `winemetal/unix/src/g2.rs` `hooked_buffer`). The first name's bytes are
zeroed (mtld3d's first backing is; a placed page is not). If the first name has no mapped memory, the buffer is
staged as today and one error line says so. A direct buffer holds no `PageBox`: its `BufferBacking` starts
released (a constructor in mtld3d-core) and nothing restores it.

*Lock.* d3d9-rt owns the decision: `dynamic_lock(id, flags, usage, offset, size) -> pointer or null` calls
`plan_lock(flags, usage, length, offset, size, last_use, queue.coherent_seq())`.
- `WriteInPlace`: the immediate name's mapped memory.
- `Rename`: the next suballocation of the immediate name if it has one (emit `use_suballocation(next)`), else
  `hold_under_cap()`, `allocate(coherent)`, `update_immediate_name(current_seq, …)`, emit
  `{ use_suballocation(0); buffer.rename(name) }`, and the old name's bytes enter the retained ledger at
  `current_seq`. `PreserveKind::Cpu` copies `length` bytes from the old immediate name and suballocation to the
  new before the pointer is answered. `last_use = 0`. A new name with no mapped memory renames nothing and the
  lock answers what mtld3d answers when its rename allocation fails.
- Every CPU reader (the pointer, the preserve copy's source, the fan's index bytes) reads the **immediate** name
  and suballocation, never `buffer.current()`, which is the encode thread's.
The front end's `vb_lock` / `ib_lock` reach this from one shared helper, before the released-backing restore,
the Staged branch, `coherent_seq_arc`, `alloc_pagebox_capped` and `queue_vbib_retention` (the last three panic or
no-op on a core device), and join the shared tail (unknown-flag warning, null answer, `lock_count`). `Unlock`
stays the count.

*Draw.* `flush()` skips a direct buffer (nothing to upload). d3d9-rt's `draw` binds it like any stored buffer
(the closure reads `buffer.current()` and its suballocation offset at encode time, after every rename emitted
before it), and stamps `last_use = queue.current_seq()` on every dynamic buffer the draw names, vertex and
index, **inside `draw`, at the emit**: no commit can fall between the stamp and the draw's closure. An indexed
fan reads a direct index buffer's bytes from d3d9-rt (`dynamic_bytes`), not from the backing.

*Release.* `forget_buffer` drops the `DynamicBuffer`; recorded draws hold the `Buffer`, rename closures hold the
names they install, and a chunk retains what it encoded (`track_buffer`), as for D3D11.

**The WRITEONLY release** is mtld3d's `release_backing_after_upload` (`vertex_buffer.rs:1032-1048`) called by
`core_flush` after a successful upload, under mtld3d's guards (`lock_count == 0`, not `buffer.ignoreLockBounds`,
`may_release_backing`, `may_widen_upload`), for vertex buffers only. `core_flush` reports its uploads to
`BufferBacking::note_upload` (a re-created backing returns to `Mirrors` only once an upload covered it whole), and
a released backing with nothing dirty is a flush of nothing. The lock path that re-creates a released backing
(zeroed, `Partial`, the announced window only) already runs on a core device.

**The gauge** is one ledger in d3d9-rt: a FIFO of `(seq, bytes)` and their sum. A rename adds the old name's
bytes at the chunk being recorded; `commit` adds the chunk's staged bytes at its seq; `settle(coherent)` pops
what the GPU has retired. `hold_under_cap()` settles and, while the sum is at or over the cap and the ledger is
not empty, `sync_to(oldest seq)` and settles again. It runs before a rename's allocation and after a
staged-budget commit (buffers and textures). The cap is `Config::vbib_retention_cap_bytes`
(`memory.vbibRetentionCapMB`, 512 MiB, 0 = none), handed to d3d9-rt at device creation.

## Review Focus (for the final review)

1. API-time name against encode-time name, for every CPU reader and every emitted closure.
2. The stamp: no commit between it and the emit; vertex streams and the index buffer; refused and dropped draws.
3. Lifetimes: a dynamic buffer forgotten with draws in flight; a renamed-away name; the fan's borrowed bytes.
4. Null pointers: the staged fall-back at create, a failed rename, both loud.
5. The release's guards against mtld3d's, `Partial`'s window, `note_upload`, index buffers left out.
6. The ledger's arithmetic (settle, add, wait, cap 0, no underflow, `sync_to` of the chunk being recorded).
7. Each mutant traced to its row, and which go red by construction.
8. The positive proof lines in both gate scripts.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Files: `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`, `vm/tests/d3d9.sh` and
whatever counts the buffers rows (33 → 39).

- Six rows (Appendix): `buf-dyn-preserve`, `buf-dyn-many`, `buf-dyn-index`, `buf-dyn-read`, `buf-dyn-cross`,
  `buf-writeonly`. Each passes on mtld3d by construction; the same expectation under `strict`.
- **Positive proof, both scripts** (under core only): the buffers launch's log holds the line Task 5 logs once a
  device at its first direct buffer ("d3d9 core: dynamic buffers map directly"), and no launch holds the
  fall-back line ("d3d9 core: a dynamic buffer of … has no mapped memory"). Written now, red under core until
  Task 5.
- Scripts written now: `s2b/gates/head.sh`, `ctl.sh`, `ja.sh`, `jcr.sh`, `jb.sh`, `jv0.sh`.
- **Controls**, before any DXMTRS code, at the base + this commit:
  - `jb`: the base built plain → `s2b/gates/base-key.txt`, `base-imports.txt`.
  - `ja` (Mac): default and msl green on all 39 rows with no expectation corrected. Core, **red by design,
    exactly:** `buf-writeonly` (its zero read: the backing is still held) and `buffers-runtime` (the proof line
    is absent). Everything else green: the staged model draws the other five rows right.
  - `jv0` (Mac, default runtime, Metal's validation layer, the buffers launch): the layer proven on by its
    banner, mtld3d proven by the absence of the core line, 0 reports.
  - `jcr` (VM default, old WoW64): green.
- Commit: "dxmt, vm: d3d9_triangle rows for dynamic buffers and a WRITEONLY buffer locked again, the proof that
  dynamic buffers map directly under core (DXMTRS C4c-2b)".

### Task 2: the WRITEONLY backing's release (front end)

- `core_flush` (vertex buffer): `note_upload(min, max)` after a successful upload (index buffer too, for the
  state's sake); then the release under mtld3d's guards, vertex buffers only; a released backing with an empty
  dirty range returns true before `as_slice()` is read. The index buffer's `core_flush` gains a comment saying
  why it keeps its backing (ruling 7).
- Tests: front-end unit tests if the buffer types have a seam for one; else the row is the test.
- Lane `jt2` (default, core): `buf-writeonly` green under core; `buffers-runtime` still red (the proof line).
- Commit: "d3d9: a static WRITEONLY vertex buffer releases its backing once uploaded on the core runtime".

### Task 3: the retained ledger and its cap (d3d9-rt, front end)

- A small type (`Retained`: `add(seq, bytes)`, `settle(coherent)`, `bytes()`, `oldest()`), fields on
  `CoreDevice` (the ledger, the cap), `hold_under_cap()`; `commit` adds the chunk's staged bytes at its seq
  before it resets them; the two staged-budget sites call `hold_under_cap()` after their commit. The cap reaches
  d3d9-rt from `Config::vbib_retention_cap_bytes` at device creation.
- Tests: the type by value (order, partial settle, zero cap, no underflow); on the fakes, uploads past a small
  cap: a test that fails if the wait is removed where the fakes can show a wait, else one that shows the commit
  and the ledger's sum after it.
- No row can see a cap. Commit: "d3d9: the bytes the GPU side still holds are counted and capped on the core
  runtime".

### Task 4: dynamic buffers in d3d9-rt

- The store's entry becomes the buffer and, for a dynamic one, its `DynamicBuffer` and `last_use`.
  `dynamic_create(id, length, write_only) -> bool`, `dynamic_lock(id, flags, usage, offset, size) -> *mut u8`,
  `dynamic_bytes(id) -> Option<&[u8]>`, `is_dynamic(id)`; `draw` stamps `last_use`; `forget_buffer` unchanged in
  contract.
- Tests on the fakes, each named for what it would catch:
  - an append (`NOOVERWRITE`) between two draws emits no blit copy and opens no second pass;
  - a `DISCARD` after a draw: the draw before binds the old name, the draw after the new (handles and offsets in
    the captured calls); the same within one page: the suballocation offset moves, the handle does not;
  - a lock with no flags over the whole buffer after a draw copies the old bytes into the new name; a partial
    one writes in place;
  - before the chunk is committed, the pointer of a `NOOVERWRITE` lock after a `DISCARD` is the new name's
    (the immediate name), though `buffer.current()` is still the old;
  - a first lock of any kind on a buffer no draw named writes in place (no rename);
  - a rename over the cap waits (Task 3's ledger) and a rename adds the old name's bytes;
  - a forgotten dynamic buffer's recorded draw still encodes;
  - the zeroed first name.
- Commit: "d3d9: dynamic buffers on the core runtime's store (D3D11's names, mtld3d's lock plan)".

### Task 5: the front end maps dynamic buffers directly

- `BufferBacking::released(length, class)` (mtld3d-core). `new` of both buffers: `Direct` on a core device when
  `dynamic_create` succeeds, else `Staged` with the fall-back line; the proof line once a device.
- One shared helper for both buffers' core lock; `core_flush` returns true for a direct buffer; `indexed_fan`
  reads `dynamic_bytes`; the warm-up entry and every site of map §1.1 that runs on a core device checked against
  a buffer with no backing (the table is the checklist; the result is written in the ledger).
- The core-only `trusted` override in the Staged branch stays for the fall-back.
- Lane `jt5` (default, core): everything green, `buffers-runtime` with its proof line.
- **One VM core lane right after** (`jc5`: the VM's D3D9 gate under core on old WoW64, which runs the buffers
  rows on x64 and i386): the placed path and the hook's pages are proven or the task stops here.
- Commit: "d3d9: default-pool dynamic buffers map directly on the core runtime".

### Task 6: gates

- `jq`: d3d9-check on default, msl, core; stateblock under default and core; ten core launches under Metal's
  validation layer (the layer proven on), 0 reports, rows ok (buffers: 39 + its two lines).
- `jm`: mutants as commits off the tip (`gates/mutants.txt`), each red on its row and on nothing outside the
  buffers launch:
  a. the emitted rename dropped (the immediate name still moves) → `buf-dyn-many` (its large cell: by
     construction, the second triangle is the first again);
  b. the emitted `use_suballocation(next)` dropped → `buf-discard`;
  c. the preserve copy dropped → `buf-dyn-preserve`;
  d. the stamp dropped (every lock uncontended) → `buf-discard` or `buf-dyn-many` (near-certain, by timing: the
     CPU write lands before the GPU reads; the allowed set says so);
  e. the fan reads the released backing, not `dynamic_bytes` → `buf-dyn-index`;
  f. `core_flush` uploads a re-created backing whole → `buf-writeonly`.
- `jr`: plain build: translator key = `base-key.txt`, imports = `base-imports.txt`, d3d9-check default, the Mac
  Wine conformance with no new site, the x64 check (not across 03:35; the two known reference flakes are named
  in the ledger).
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64.
- `jp` / `jp0`: the probe at the tip and at the base, modes `tex`, `buf`, `dyn`, mtld3d and core, each once →
  `build/c4c/s2b/probe.md`. `dyn` under core is the slice's figure.
- No D3D10-12 gate if `git diff --stat <base>..<tip>` shows no file outside `src/d3d9` (ruling 10).

### Task 7: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s2b/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-2b", C4c-1's
kept-divergence sentence marked as ended, the slice list. Plan execution notes. `dxmt/pins`. Privacy scan of
both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron. Post-push check (not across
03:35). CI. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none new; `memory.vbibRetentionCapMB` is read under core with the
  meaning `mtld3d.conf` gives it. **A wire field:** none. **A dependency:** none. **A lint suppression:** none.
- **New state:** on `CoreDevice` (the ledger, the cap) and in its store's entry (`DynamicBuffer`, `last_use`):
  state on an object. **A derive:** whatever the new small types need, recorded with `--update-derives`.
- **New pub items:** each with its doc (`dynamic_create`, `dynamic_lock`, `dynamic_bytes`, `is_dynamic`,
  `hold_under_cap` if pub, `BufferBacking::released`).
- **A duplicate:** the rename sequence (next suballocation, else allocate, name, emitted rename) is D3D11's
  `Map` in d3d9-rt's words: a second caller of `DynamicBuffer`, not a copy of dxmt-core code; a shared helper
  in dxmt-core waits for a third caller (it owes the D3D10-12 gate). `plan_lock`, `classify_map_mode`,
  `may_release_backing` and the release guards are called, not copied. The two buffer types share one core
  lock helper.
- **Not silent:** the staged fall-back and a failed rename each log an error; the gates fail on the first.
- **Divergence removed:** C4c-1's WRITEONLY one. **Kept, unchanged:** mtld3d's partial contended lock
  (`docs/d3d9/STATUS.md` :304-305), now the core's too.

## Rulings (planning)

1. **D3D11's model (a `DynamicBuffer`, no `PageBox`), not mtld3d's (a `PageBox` wrapped without a copy).** Why:
   the C4 work-list's design (§1.7); the names' memory is reachable in every lane by a path D3D11 or d3d9-rt's
   read-back already takes; a `PageBox` under `allocate_over` is unproven in the VM guest. Cost if wrong: the
   front end's readers of a backing fork (the fan).
2. **d3d9-rt owns `last_use` and the `plan_lock` call.** Why: the stamp is exact only at the emit. Cost if
   wrong: the front end's `last_submit_seq` stays unused under core (a field with one runtime).
3. **Flags: suballocated; write-combined only when WRITEONLY; placed on 32-bit x86.** Cost if wrong: a
   WRITEONLY buffer read back (a `READONLY` lock, a fan over a dynamic index buffer, the preserve copy) reads
   slowly; a lane whose pointer is not writable shows at `jc5`.
4. **A first name with no mapped memory falls back to staged, loudly.** Cost if wrong: none in behaviour.
5. **The first name is zeroed.** Cost if wrong: one memset a create.
6. **The ledger counts renamed-away names and committed staging; the cap is mtld3d's key, on every
   architecture.** Why: mtld3d's cap bounds both (its renames and its staged snapshots). Cost if wrong: a
   64-bit title uploading more than 512 MiB between retirements waits where it did not (mtld3d's own
   behaviour).
7. **Index buffers keep their backing under core.** Why: a fan reads it and no read-back exists before C4c-3.
   Cost if wrong: a 32-bit process holds its static index bytes twice.
8. **The WRITEONLY release is mtld3d's rule and `buf-writeonly` asserts its zero read.** Why: parity; it is
   `docs/d3d9/STATUS.md`'s kept divergence. Cost if wrong: a title that reads a WRITEONLY buffer back sees
   zeros on both runtimes (`buffer.ignoreLockBounds` keeps the copy).
9. **No perf counter for the core's renames** (C4d). 
10. **No D3D10-12 gate** while no file outside `src/d3d9` changes; `jr`'s x64 check runs anyway.
11. **A dynamic buffer's core length is its logical length** (a staged one's is its padded backing). Cost if
    wrong: a binding that starts between the two is refused under core for a dynamic buffer and drawn for a
    static one.

## Stated gaps

- `DynamicBuffer` never drops a free name: the cap bounds what is in flight, not what a buffer holds after a
  burst. A trim is a dxmt-core change.
- The cap and the ledger are proven by unit tests only: no row or lane can see them, and no 32-bit lane runs a
  bench.
- The Mac has no i686 lane: the placed heap path is proven only as `Placed::Guest` in the VM's i386 lane.
- A draw past a dynamic buffer's end reads past a smaller allocation than mtld3d's padded page.
- A draw of a locked dynamic buffer reads live memory, as on mtld3d.
- No row: a dynamic buffer of another pool; `D3DLOCK_NOSYSLOCK` / `DONOTWAIT`; a lock that fails to rename.

## Appendix: the rows

1. **`buf-dyn-preserve`**: a DYNAMIC buffer (not WRITEONLY), two cells (one triangle list of 180 bytes: one
   page; one of more than 4096 bytes). Fill, draw; `Lock(0, 0, 0)` (whole, contended: a new name with the old
   bytes), rewrite one triangle's vertices only, draw into the other half. Asserts the first draw's pixels
   (old bytes) and the second's (the kept vertices and the new ones).
2. **`buf-dyn-many`**: DYNAMIC|WRITEONLY. Small cell: 300 times `Lock(DISCARD)`, one triangle at grid cell i
   in colour i, draw (past one page's suballocations into new names). Large cell: a buffer of more than 4096
   bytes, 40 times the same. Asserts a handful of cells of each, the second among them.
3. **`buf-dyn-index`**: a DYNAMIC|WRITEONLY index buffer: `DISCARD` then `NOOVERWRITE` appends, an indexed
   list draw each; then an indexed fan over it.
4. **`buf-dyn-read`**: a DYNAMIC buffer (not WRITEONLY): write, unlock, `Lock(READONLY)` reads the same
   bytes; `Lock(NOOVERWRITE)` after a draw reads them too; draw.
5. **`buf-dyn-cross`**: C4c-2a's crossing layout (stride 12) at stream offset 2 from a DYNAMIC|WRITEONLY
   buffer, appended to with `NOOVERWRITE` between two draws.
6. **`buf-writeonly`**: a static WRITEONLY default-pool vertex buffer: fill, draw; `Lock(0, 0, READONLY)`
   reads zeros (the backing was released); `Lock` of one vertex's window, rewrite, draw: the untouched
   vertices draw as before (the device's bytes), the rewritten one moved.
