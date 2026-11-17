# DXMTRS C4c-2b: dynamic buffers mapped directly on D3D9's core runtime, the WRITEONLY backing's release, the retained-bytes gauge and its cap

Status: three adversarial reviews folded (`build/c4c/s2b/review-{1-design,2-core,3-exec}.md`; dispositions at the
end). Parent: `2026-11-11-dxmtrs-c4-d3d9-front-end.md` (C4), spec §5 (`C4c's slices`), the C4 work-list
`build/c4/map2-sync-worklist.md` §1.5 and §1.7. Map: `build/c4c/s2b/map.md` (cited "map §n"). Ledger:
`.superpowers/sdd/2026-11-16-dxmtrs-c4c/progress.md`.

## Goal

Under `runtime.mode = core`:

- a default-pool `D3DUSAGE_DYNAMIC` vertex or index buffer is mapped directly, as D3D11's dynamic buffers are:
  `Lock` answers a pointer into memory the GPU reads, a draw uploads nothing and splits no pass, and
  mtld3d-core's `plan_lock` decides between writing in place and a new name. The `dyn` bench's 54 ms of CPU a
  frame (22 times mtld3d's) is the figure this moves;
- a static default-pool `D3DUSAGE_WRITEONLY` vertex buffer releases its CPU backing once uploaded, by mtld3d's
  rule (C4c-1's kept divergence goes);
- the vertex and index bytes the GPU side still holds and the process cannot reuse (renamed-away names, staged
  buffer uploads) are counted, and at `memory.vbibRetentionCapMB` the API thread waits for the oldest instead
  of growing.

## Global Constraints

- Work only in the two worktrees `build/c4b3c/dx` (DXMTRS, branch `c4c`) and `build/c4b3c/mn` (MacNeutron, branch
  `c4c-mn`). The main checkout stays on `main`.
- The **base** is DXMTRS main a4c94873 and MacNeutron main 844be686 (C4c-2a). Fetch both origins before the first
  build and before the push; rebase if a main moved (then rust-check, one lane, the plain build).
- No line of `src/dxmt/src` (dxmt-core), `src/d3d11`, `src/d3d12`, `src/dxgi`, `src/winemetal` changes. If a task
  finds it needs one, it stops and reports (the D3D10-12 gate would then be owed). `src/dxmt/fakes` is test
  support and may change.
- The default runtime (mtld3d) draws exactly what it draws today: its translator key, imports and conformance
  are gates. The mtld3d lines touched: a constructor added to `BufferBacking`, and
  `release_backing_after_upload` split so that its caller passes the `buffer.ignoreLockBounds` knob (same
  behaviour).
- Every build, gate and `$D` checkout runs under `/usr/bin/lockf -k ~/Documents/MacProton/build/pipeline.lock`;
  no make in a worktree.
- **Job scripts** live in `build/c4c/s2b/gates`, each copied from `build/c4c/s2/gates` and edited **before** its
  first run; a job that has run is never edited; jobs test committed tips. The s2 scripts name s2 by absolute
  path (`head.sh` in every job, `ctl.sh`, `chain.sh`, `post.sh`'s wanted key) and the VM state `c4c2a-*`
  (`head.sh`'s trap, `jcr.sh`, `jc.sh`): **before any s2b job runs,
  `/usr/bin/grep -rnE 'c4c/s2([^b]|$)|c4c2a' build/c4c/s2b/gates` prints nothing.** Written with Task 1:
  `head.sh`, `ctl.sh`, `ja.sh`, `jcr.sh`, `jb.sh`, `jv0.sh`, `jt.sh`, `jc5.sh`; written after `jb` and before
  Task 6: `jq.sh`, `jm.sh` (its row list, its allowed sets, its mutant count), `jr.sh`, `jc.sh`, `jp.sh`,
  `post.sh`, `chain.sh`.
- **Wanted values come from the base:** control job `jb` builds the base plain and writes its translator key and
  imports list; `jr.sh` and `post.sh` read them.
- **No `dxmt/check.sh` run may cross 03:35** (macOS's dirhelper empties the staged D3DMetal reference). From the
  s2 logs a chain's x64 check runs from about 64 to 93 minutes after its start: a chain starts by 01:55 or
  from 02:40; a lone `jr` or `post` does not start between 03:00 and 03:30.
- Each DXMTRS commit passes `tools/rust-check.sh` before it is made. A front-end helper or constant lands in the
  commit of its first caller, under `cfg(feature = "core-runtime")` (the front end is a staticlib: dead code
  fails the gate). d3d9-rt is an rlib: its pub functions pass without a caller, so Task 4 is its own commit.
- One lane (default, core) per task that moves a row; full gates once; no fix round for Minors.
- Home path written `~`; privacy scan before the push.

## Scope

In: Tasks 1-7 below. Out, with where it goes:

- Read-back of a buffer (an index buffer whose backing was released, read by a fan): C4c-3. Index buffers keep
  their backing here (ruling 7).
- A trim of `DynamicBuffer`'s free names, a gauge inside dxmt-core: dxmt-core changes, not here (Stated gaps).
- mtld3d's perf counters for renames under core, the address-space watch's view of direct buffers: C4d.
- `DYNAMIC` buffers of other pools: staged, as mtld3d's `classify_map_mode` says.
- Texture uploads under the cap: not here (the key's meaning is vertex and index memory).
- mtld3d's own hazard, met while planning: a lock of size 0 (whole) on a re-created backing, `READONLY`
  included, marks the whole buffer dirty and uploads its zeros over the device's bytes
  (`vertex_buffer.rs:742-751`, `:806-817`; `buffer_rename.rs:270`). The core inherits it. A finding for the
  maintainer, not fixed here; no row sits on it.

## The mechanism (what is ported, and from where)

**The model is D3D11's `Map` of a dynamic buffer** (`d3d11/src/resource/buffer.rs:70-107`,
`context/map.rs:97-140`; map §4), with D3D9's lock rules from mtld3d-core (`buffer_rename.rs` `plan_lock`,
`classify_map_mode`; map §5).

*Creation.* For a buffer `classify_map_mode` calls `Direct` on a core device, the front end asks d3d9-rt for a
dynamic buffer at `CreateVertexBuffer` / `CreateIndexBuffer`: a `Buffer` (its length: the logical length when
that is at most one 4096-byte page, else rounded up to whole pages, so that a write past the end stays in pages
the name owns, in a 32-bit process too), `allocate(flags)`, `rename`, `DynamicBuffer::new`. Flags:
`SUBALLOCATE_FROM_ONE_PAGE`, and `CPU_PLACED` when `cfg!(target_arch = "x86")`, D3D11's expression (a 64-bit PE
dereferences Metal's contents pointer on the Mac and the host hook's pages in the VM, the path d3d9-rt's
read-back staging already takes there; a 32-bit one its own placed pages). No `CPU_WRITE_COMBINED`: D3D9 reads
these bytes (the preserve copy, a fan, a `READONLY` lock), and mtld3d's pages are ordinary memory. A name is
usable when its Metal handle is not 0 **and** its mapped memory is not null (a placed name Metal refused has
memory and no handle). The first name's bytes are zeroed (mtld3d's first backing is; a placed page is not). If
the first name is not usable, the buffer is staged as today and one error line says so. A direct buffer holds
no `PageBox`: its `BufferBacking` starts released (a constructor in mtld3d-core; `padded_len` 0) and nothing
restores it.

*Lock.* d3d9-rt owns the decision: `dynamic_lock(id, flags, usage, offset, size)` answers the pointer **at
`offset`**, or null for an id it does not hold. It calls
`plan_lock(flags, usage, logical length, offset, size, last_use, queue.coherent_seq())`.
- `WriteInPlace`: the immediate name's mapped memory at its immediate suballocation.
- `Rename`: the preserve copy's source (the immediate name and suballocation) is captured first. Then the next
  suballocation of the immediate name if it has one (emit `use_suballocation(next)`; nothing enters the
  ledger: no memory left the name), else `hold_under_cap()`, `allocate(queue.coherent_seq())` read **after**
  the hold, `update_immediate_name(current_seq, …)`, emit `{ use_suballocation(0); buffer.rename(name) }`,
  and the old name's whole size (`suballocation_size * suballocation_count`) enters the ledger at
  `current_seq`. `PreserveKind::Cpu` copies the logical length from the captured source to the new memory
  before the pointer is answered. `last_use = 0` on both arms. A new name that is not usable renames nothing:
  one error line, and the lock writes in place (a null pointer would crash the title; ruling 4).
- Every CPU reader (the pointer, the preserve copy's source, the fan's index bytes) reads the **immediate** name
  and suballocation, never `buffer.current()` or `current_suballocation()`, which are the encode thread's.
- The store's entry cannot stay borrowed across `hold_under_cap`, `emit` and the ledger (all `&mut self`): the
  lock looks its entry up in two phases.
The front end's `vb_lock` / `ib_lock` call one shared helper (`cfg(feature = "core-runtime")`, over the device,
the id, length, usage, offset, size, flags) right after the bounds checks they share with mtld3d. For a direct
buffer on a core device it answers the pointer and control jumps to the shared tail (the unknown-flag warning,
`lock_count`, the out pointer) with it: the released-backing restore, the Staged branch, mtld3d's Direct branch
(`coherent_seq_arc`, `alloc_pagebox_capped`, `queue_vbib_retention`) and `backing.write_ptr_at` are never
reached. `Unlock` stays the count.

*Draw.* `flush()` skips a direct buffer before it asks the store anything (nothing to upload). d3d9-rt's `draw`
binds it like any stored buffer (the closure reads `buffer.current()` and its suballocation offset at encode
time, after every rename emitted before it), and stamps `last_use = queue.current_seq()` on every dynamic
buffer the draw names, vertex streams and index buffer, **after its emit**, walking `draw.streams` and
`draw.indices`: nothing in `draw` commits, and the front end's commits (`flush`, textures) come before
`core.draw`. The first stamp of a device logs "d3d9 core: dynamic buffers are read in place" once (a flag on
the device; the level expression of d3d9-rt's device line, which a perf build keeps). An indexed fan over a
direct index buffer copies its window out of d3d9-rt (`dynamic_copy(id, first, len) -> Option<Vec<u8>>`, the
immediate name, bounded by the logical length): a borrowed slice would not outlive the draw's `&mut`.

*Release.* `forget_buffer` drops the `DynamicBuffer`; recorded draws hold the `Buffer`, rename closures hold the
names they install, and a chunk retains what it encoded (`track_buffer`), as for D3D11.

**The WRITEONLY release.** `release_backing_after_upload` (`vertex_buffer.rs:1032-1048`) is split: its guards
and action take the `buffer.ignoreLockBounds` knob as an argument; mtld3d's `vb_unlock` passes what it reads
today. `flush()` reads the knob before it borrows the core device and passes it to `core_flush` (reading it
inside would alias the device). `core_flush` (vertex buffer), after a successful upload: `note_upload(min,
max)`, then the release under those guards (`lock_count == 0`, not the knob, `may_release_backing`,
`may_widen_upload`). A released backing with an empty dirty range is a flush of nothing, answered before
`as_slice()` or the store is read. **A re-created backing is uploaded at its Unlock:** the lock path that
re-creates a released backing (zeroed, `Partial`, the announced window only) already runs on a core device,
but the dirty range is one merged span, so two windows locked before a draw would upload the zeros between
them over the device's bytes. Under core, the Unlock that brings `lock_count` to 0 on a backing that may not
widen (`Partial`) runs `core_flush` at once, as mtld3d uploads each window at its own Unlock. Index buffers
are left out (ruling 7).

**The gauge** is one ledger in d3d9-rt: a FIFO of `(seq, bytes)` and their sum. A rename adds the old name's
size at the chunk being recorded; `buffer_upload` adds its length at the chunk being recorded; every add
settles first (`settle(coherent)` pops what the GPU has retired), so the ledger never grows past what is in
flight. `hold_under_cap()` settles and, while the sum is at or over the cap and the ledger is not empty,
`sync_to(oldest seq)` (which commits the chunk being recorded when that is the oldest) and settles again. It
runs before a rename's allocation and at the start of `buffer_upload`. The cap is
`Config::vbib_retention_cap_bytes` (`memory.vbibRetentionCapMB`, 512 MiB, 0 = none), set on the core device by
the front end after it is made (a setter; `CoreDevice::new` keeps its signature).

## Review Focus (for the final review)

1. API-time name against encode-time name, for every CPU reader and every emitted closure.
2. The stamp: after the emit, no commit between; vertex streams and the index buffer; refused and dropped draws.
3. Lifetimes: a dynamic buffer forgotten with draws in flight; a renamed-away name; the fan's copied bytes.
4. A name that is not usable: the staged fall-back at create, the in-place write at a rename, both loud.
5. The release: mtld3d's guards unchanged by the split, the Unlock upload of a re-created backing,
   `note_upload`, the knob read before the borrow, index buffers left out.
6. The ledger's arithmetic (settle, add, wait, cap 0, no underflow, `sync_to` of the chunk being recorded).
7. Each mutant traced to its row.
8. The proof line, its level, and its checks in both gate scripts and the probe.

## Tasks

### Task 1: rows, scripts, controls (MacNeutron)

Files: `dxmt/tests/d3d9_triangle.cpp`, `dxmt/d3d9-check.sh`, `vm/dxmt/d3d9-gate.sh`,
`vm/tests/d3d9-gate-bench-test.sh`, `vm/tests/d3d9.sh` and whatever counts the buffers rows (33 → 39).

- Six rows (Appendix). Each passes on mtld3d by construction; the same expectation under `strict`. A frame
  asserts at most 8 points (`frames()`).
- **The proof, both scripts, under core only, with its own token** (`buffers-direct` in `d3d9-check.sh`, its
  own check row in `d3d9-gate.sh`, not folded into `buffers-runtime`): the buffers launch's log holds
  "d3d9 core: dynamic buffers are read in place". **The fall-back**, a fixed substring ("has no mapped
  memory"): in `d3d9-check.sh`'s `unpresented` patterns and in the VM gate's `runtime()`, so that any core
  launch holding it fails. `vm/tests/d3d9-gate-bench-test.sh` gains three cases in the gate's trace mode (no
  VM): no proof line → FAIL, with it → ok, with the fall-back line → FAIL.
- **Controls**, before any DXMTRS code, at the base + this commit:
  - `jb`: the base built plain → `s2b/gates/base-key.txt`, `base-imports.txt`.
  - `ja` (Mac): default and msl green on all 39 rows with no expectation corrected. Core, **red by design,
    exactly:** `buf-writeonly` (its zero read: the backing is still held) and `buffers-direct` (no proof line).
    `ja.sh` prints each failing token with its cause line.
  - `jv0` (Mac, default runtime, Metal's validation layer, the buffers launch, one run with every row): the
    layer proven on by its banner, mtld3d proven by the absence of the core line, 39 rows ok, 0 reports.
  - `jcr` (VM default, old WoW64): green; the buffers launch's duration recorded from the trace files' times
    (budget 180 s, kill 240 s).
- Commit: "dxmt, vm: d3d9_triangle rows for dynamic buffers and a WRITEONLY buffer locked again, the proof that
  dynamic buffers are read in place under core (DXMTRS C4c-2b)".

### Task 2: the WRITEONLY backing's release (front end)

- As "The WRITEONLY release" above: the split, `flush()` passing the knob, `core_flush`'s `note_upload` and
  release (vertex buffer), the empty flush of a released backing, the Unlock upload of a re-created backing.
  The index buffer's `core_flush` gains a comment saying why it keeps its backing.
- No unit test: the front end's tests do not run without Wine (`docs/CONVENTIONS.md`); the row and its mutants
  are the test.
- Lane `jt2` (default, core): `buf-writeonly` green under core; red under core: `buffers-direct` alone.
- Commit: "d3d9: a static WRITEONLY vertex buffer releases its backing once uploaded on the core runtime".

### Task 3: the retained ledger and its cap (d3d9-rt, front end)

- A small type (`Retained`: `add(seq, bytes, coherent)`, `settle(coherent)`, `bytes()`, `oldest()`), fields on
  `CoreDevice` (the ledger, the cap), `set_retention_cap`, `hold_under_cap()`; `buffer_upload` holds at its
  start and adds its length. The front end sets the cap from `Config::vbib_retention_cap_bytes` when it makes
  the core device.
- Tests: the type by value (order, partial settle, zero cap, zero bytes, no underflow). On the fakes (chunks
  retire at once, so a wait on a committed chunk cannot be seen): uploads past a small cap in a first,
  uncommitted chunk show a commit line before any present and `coherent_seq() >= ` the held seq afterwards; a
  cap of 0 never commits.
- No row can see a cap; no lane. Commit: "d3d9: the vertex and index bytes the GPU side still holds are counted
  and capped on the core runtime".

### Task 4: dynamic buffers in d3d9-rt

- The store's entry becomes the buffer and, for a dynamic one, its `DynamicBuffer`, its logical length and
  `last_use`. `dynamic_create(id, length) -> bool`, `dynamic_lock(id, flags, usage, offset, size) -> *mut u8`,
  `dynamic_copy(id, first, len) -> Option<Vec<u8>>`; `draw` stamps and logs the proof line once;
  `forget_buffer` unchanged in contract.
- Tests on the fakes (they show handles, offsets, blits and passes in the captured calls; their memory is
  always zeroed and never null, so the zeroing and the not-usable paths are **not** proven by a test: stated):
  - `dynamic_lock(NOOVERWRITE)` between two draws emits nothing: no blit copy, no second pass;
  - a `DISCARD` after a draw on a buffer of more than one page: the draw before binds the old handle, the draw
    after the new; on a one-page buffer: the handle stays and the bound offset moves by the suballocation;
  - a lock with no flags over the whole buffer after a draw answers memory holding the old bytes; twice in one
    uncommitted chunk, the second holds what the first wrote (the immediate name, not `current()`); a partial
    one writes in place;
  - before the chunk is committed, the pointer of a `NOOVERWRITE` lock after a `DISCARD` is the new memory;
  - a first lock of any kind on a buffer no draw named writes in place; a draw naming the buffer as its index
    buffer stamps it too;
  - a rename on a buffer of more than one page adds the old name's size to the ledger, a suballocation step
    adds nothing, and a rename over a small cap in an uncommitted chunk commits it;
  - `dynamic_copy` after a contended `DISCARD` reads the new bytes;
  - a forgotten dynamic buffer's recorded draw still encodes; the proof line is logged once.
- Commit: "d3d9: dynamic buffers on the core runtime's store (D3D11's names, mtld3d's lock plan)".

### Task 5: the front end maps dynamic buffers directly

- `BufferBacking::released(length, class)` (mtld3d-core, with a host test). `new` of both buffers: `Direct` on a
  core device when `dynamic_create` succeeds, else `Staged` with the fall-back line.
- The shared lock helper; `flush()` skipping a direct buffer; `indexed_fan` over `dynamic_copy`; every site of
  map §1.1 that runs on a core device checked against a buffer with no backing, the warm-up entry (built before
  its core early return) included: the table is the checklist and its result is written in the ledger.
- The core-only `trusted` override in the Staged branch stays for the fall-back.
- Lane `jt5` (default, core): everything green, `buffers-direct` included.
- **One VM core lane right after** (`jc5`: the VM's D3D9 gate under core on old WoW64, which runs the buffers
  rows on x64 and i386). If a lane's pointer cannot be written, the flag expression becomes
  `x86 || feature = "guest-vm"` (placed pages are the guest's shared memory there, the staging ring's own
  path), ledgered, and `jc5` runs again; if that fails too the task stops and Tasks 2-3 ship alone.
- Commit: "d3d9: default-pool dynamic buffers map directly on the core runtime".

### Task 6: gates

- `jq`: d3d9-check on default, msl, core; stateblock under default and core; ten core launches under Metal's
  validation layer (the layer proven on), 0 reports, rows ok, the proof line in the buffers launch and the
  fall-back line in none.
- `jm`: mutants as commits off the tip (`gates/mutants.txt`), each red on its named row and on nothing outside
  the buffers launch, all by construction (d3d9-rt commits only at a present, a read-back and the staged
  budget, so within a row's frame the GPU reads after every CPU write):
  a. the emitted rename dropped (the immediate name still moves) → `buf-dyn-many` (large cell);
  b. the emitted `use_suballocation(next)` dropped → `buf-discard`;
  c. the preserve copy dropped → `buf-dyn-preserve`;
  d. the vertex stamp dropped → `buf-discard`;
  e. the index buffer's stamp dropped → `buf-dyn-index`;
  f. the preserve copy reads `buffer.current()` and `current_suballocation()` → `buf-dyn-preserve` (its second
     whole lock);
  g. the fan's copy reads `buffer.current()` → `buf-dyn-index`;
  h. `core_flush` uploads a re-created backing whole → `buf-writeonly`;
  i. the Unlock upload of a re-created backing dropped → `buf-writeonly` (its two-window cell).
- `jr`: plain build: translator key = `base-key.txt`, imports = `base-imports.txt`, d3d9-check default, the Mac
  Wine conformance with no new site, the x64 check (the two known reference flakes are named in the ledger).
- `jc`: the VM's D3D9 gate on default and core, old and new WoW64.
- `jp` / `jp0`: the probe at the tip and at the base, modes `tex`, `buf`, `dyn`, mtld3d and core, each once →
  `build/c4c/s2b/probe.md`. Per core `dyn` run at the tip the job also wants the proof line and no fall-back
  line. **Wanted:** `dyn` under core at 10 ms of CPU a frame or less and a wall time under 10 s for 600 frames
  (the base: 54 ms, 24 s; `buf` under core: 4.1 ms, 4.6 s); above 20 ms the direct path is not engaged and the
  slice does not ship.
- No D3D10-12 gate if `git diff --stat <base>..<tip>` shows no file outside `src/d3d9` (ruling 10).

### Task 7: review, spec, pin, push

One whole-branch Opus review (`build/c4c/s2b/review-final.md`); Critical and Important fixed with a red cell
first where a pixel can show it; Minors fixed if one line, else ledgered. Spec §5 paragraph "C4c-2b", C4c-1's
kept-divergence sentence marked as ended, the slice list. Plan execution notes. `dxmt/pins`. Privacy scan of
both ranges. Fetch, rebase if a main moved. Advisor. Push DXMTRS then MacNeutron. Post-push check (not across
03:35). CI. Memory. Scratch deleted by full path.

## Rules check

- **A static:** none. **A config key:** none new; `memory.vbibRetentionCapMB` is read under core with the
  meaning `mtld3d.conf` gives it (vertex and index memory; textures are not counted). **A wire field:** none.
  **A dependency:** none. **A lint suppression:** none.
- **New state:** on `CoreDevice` (the ledger, the cap, the proof line's once flag) and in its store's entry
  (`DynamicBuffer`, the logical length, `last_use`): state on an object. **A derive:** only `Clone`/`Copy` are
  inventoried; whatever the new small types need is recorded with `--update-derives`.
- **New pub items:** each with its doc (`dynamic_create`, `dynamic_lock`, `dynamic_copy`, `set_retention_cap`,
  `BufferBacking::released`). **Unsafe:** the pointer arithmetic and copies each carry a SAFETY comment, one
  operation a block.
- **A duplicate:** the rename sequence (next suballocation, else allocate, name, emitted rename) is D3D11's
  `Map` in d3d9-rt's words: a second caller of `DynamicBuffer`, not a copy of dxmt-core code; a shared helper
  in dxmt-core waits for a third caller (it owes the D3D10-12 gate). `plan_lock`, `classify_map_mode`,
  `may_release_backing` and the release guards are called, not copied. The two buffer types share one core
  lock helper.
- **Not silent:** the staged fall-back and a rename that could not take a new name each log an error; the
  gates fail on the first.
- **Divergence removed:** C4c-1's WRITEONLY one. **Kept, unchanged:** mtld3d's partial contended lock
  (`docs/d3d9/STATUS.md` :304-305), now the core's too.

## Rulings (planning)

1. **D3D11's model (a `DynamicBuffer`, no `PageBox`), not mtld3d's (a `PageBox` wrapped without a copy).** Why:
   the C4 work-list's design (§1.7); the names' memory is reachable in every lane by a path D3D11 or d3d9-rt's
   read-back already takes; a `PageBox` under `allocate_over` is unproven in the VM guest. Cost if wrong: the
   front end's readers of a backing fork (the fan).
2. **d3d9-rt owns `last_use` and the `plan_lock` call.** Why: the stamp is exact only after the emit. Cost if
   wrong: the front end's `last_submit_seq` stays unused under core.
3. **Flags: suballocated; never write-combined; placed on 32-bit x86** (and in a guest build if `jc5` says so).
   Why: D3D9 reads these bytes; mtld3d's are ordinary memory. Cost if wrong: writes slower than D3D11's by
   whatever write-combining buys on this hardware; the probe shows it.
4. **A name that is not usable: staged at create, written in place at a rename, both with an error line.** Why:
   a null lock pointer crashes a title; mtld3d's own rename cannot fail on memory. Cost if wrong: after a
   failed rename a pending draw reads the new bytes.
5. **The first name is zeroed.** Cost if wrong: one memset a create. Not proven by a test (the fakes zero).
6. **The ledger counts renamed-away names and staged buffer uploads; the cap is mtld3d's key, on every
   architecture; textures are not counted.** Why: that is what the key bounds on mtld3d (its renames and its
   staged snapshots). The hold runs before every allocation of a name, where mtld3d skips its cap on a pool
   hit: the ledger settles first, so the hold fires only when the bytes in flight are at the cap. Cost if
   wrong: a wait mtld3d would not make.
7. **Index buffers keep their backing under core.** Why: a fan reads it and no read-back exists before C4c-3.
   Cost if wrong: a 32-bit process holds its static index bytes twice.
8. **The WRITEONLY release is mtld3d's rule and `buf-writeonly` asserts its zero read** through a windowed
   lock. Why: parity; it is `docs/d3d9/STATUS.md`'s kept divergence. Cost if wrong: a title that reads a
   WRITEONLY buffer back sees zeros on both runtimes (`buffer.ignoreLockBounds` keeps the copy).
9. **No perf counter for the core's renames** (C4d).
10. **No D3D10-12 gate** while no file outside `src/d3d9` changes; `jr`'s x64 check runs anyway.
11. **A dynamic buffer's core length is its logical length up to one page, whole pages above.** Cost if wrong:
    see the gaps (a one-page buffer overrun; bindings and fans in mtld3d's padding).
12. **No mutant drops the release's `lock_count` guard:** it would write through freed memory in the row
    program. The cell that draws a locked WRITEONLY buffer stays, green.

## Stated gaps

- `DynamicBuffer` never drops a free name: the cap bounds what is in flight, not what a buffer holds after a
  burst. A trim is a dxmt-core change.
- The cap and the ledger are proven by unit tests only; no 32-bit lane runs a bench; the bench's 15360-byte
  buffer never takes the suballocation path (the rows do).
- The Mac has no i686 lane: the placed heap path (4096-aligned pages against Metal's 16 KiB page) is proven
  only as the guest's shared memory in the VM's i386 lane; a refusal there or on a Mac i686 build falls back
  to staged, loudly.
- A write past the end of a one-page dynamic buffer lands in the next suballocation of its page (a neighbour
  generation of the same buffer: a glitch, not memory the process does not own); mtld3d's padded page absorbs
  it. A draw past a dynamic buffer's end, a binding or a fan's window in mtld3d's padding: read or refused
  differently from mtld3d.
- A draw of a locked dynamic buffer reads live memory, as on mtld3d.
- The address-space watch and the perf counters see nothing of direct buffers.
- No row: a dynamic buffer of another pool; `D3DLOCK_NOSYSLOCK` / `DONOTWAIT`; a name that is not usable;
  the zeroed first name.

## Appendix: the rows

1. **`buf-dyn-preserve`**: a DYNAMIC buffer (not WRITEONLY). Small cell (180 bytes, one page): fill, draw;
   `Lock(0, 0, 0)` (whole, contended: a new name with the old bytes), rewrite one triangle's vertices only,
   draw; `Lock(0, 0, 0)` again, rewrite another triangle's, draw. Asserts each draw's pixels: the first shows
   the fill, the second the first rewrite over the fill, the third both rewrites. Large cell (more than 4096
   bytes): the first two steps.
2. **`buf-dyn-many`**: DYNAMIC|WRITEONLY. Small cell: 300 times `Lock(DISCARD)`, one triangle at grid cell i
   in colour i, draw (past one page's suballocations into new names). Large cell: a buffer of more than 4096
   bytes, 40 times the same. Asserts at most 8 cells of each, the second among them.
3. **`buf-dyn-index`**: a DYNAMIC|WRITEONLY index buffer: fill, an indexed list draw; `Lock(DISCARD)`
   (contended), other indices, an indexed list draw; a `NOOVERWRITE` append, draw; then an indexed fan over it.
   Asserts each draw's pixels: the first shows the first indices.
4. **`buf-dyn-read`**: a DYNAMIC buffer (not WRITEONLY): write, unlock; `Lock(READONLY)` reads the same bytes;
   draw; `Lock(NOOVERWRITE)` reads them too; draw.
5. **`buf-dyn-cross`**: C4c-2a's crossing layout (stride 12) at stream offset 2 from a DYNAMIC|WRITEONLY
   buffer; draw; a `NOOVERWRITE` append that starts past every byte the first draw fetches (its last vertex's
   crossing attribute included); draw.
6. **`buf-writeonly`**: a static WRITEONLY default-pool vertex buffer. Cell 1: fill, draw; `Lock` of one
   vertex's window (no flags): the bytes read through the pointer are zeros (the backing was released; red
   under core until Task 2), write the vertex, unlock, draw: the untouched vertices draw as before (the
   device's bytes), the rewritten one moved. Cell 2 (two windows): `Lock` of one vertex, write, unlock; `Lock`
   of another, not adjacent, write, unlock; draw: the vertex between them is the device's. Cell 3 (drawn while
   locked): a second such buffer, `Lock`, write, draw, write through the same pointer, unlock, draw. No
   `READONLY` or size-0 lock anywhere in the row (Scope: mtld3d's hazard).

## Review dispositions

- **R1** (design; 1 Critical, 5 Important, 8 Minor): C1 → the Unlock upload of a re-created backing, cell 2 of
  `buf-writeonly`, mutant i. I1 → the row reads its zeros through the windowed lock; mtld3d's hazard in Scope.
  I2 → "usable" = handle and memory, at create and at every new name. I3 → ruling 4 (in place, an error line).
  I4 → the ledger counts `buffer_upload`, not staged textures. I5 → whole pages above one page; the one-page
  overrun and the padding in the gaps. Minors → coherent read after the hold; the preserve source captured
  first; settle at every add; `dynamic_copy` (a `Vec`); never write-combined; the zeroing test dropped and
  stated; no front-end unit test; the pointer at `offset`.
- **R2** (core; 2 Critical, 5 Important, 8 Minor): C1 → as R1's I1. C2 → as R1's M4. I1 → as R1's I2. I1b → the
  helper answers the pointer and jumps to the tail; `padded_len` 0; `flush` skips a direct buffer before the
  store. I2 → the split and the knob read before the borrow. I3 → the device line's level; the once flag in the
  Rules check. I4 → no commit hook any more; a rename's size is the name's whole size. I5 → the tests reworded
  to what the fakes show. Minors → the stamp after the emit over `draw.streams`/`draw.indices`; the two-phase
  lookup; ruling 6's sentence on the hold; the setter; Task 4 its own commit, the helper under `cfg`,
  `is_dynamic` and the index buffer's `note_upload` dropped; the guest flag pre-authorised at `jc5`; mutant d
  by construction.
- **R3** (exec; 2 Critical, 9 Important, 9 Minor): C1 → as R1's I1 (and mutant h can now go red). C2 → the
  proof line is logged at the first draw that reads a dynamic buffer in place. I1 → the grep over `s2b/gates`
  before any run. I2 → the token `buffers-direct`. I3 → the three trace-mode cases. I4 → `buf-dyn-cross`'s
  append past the first draw's fetch. I5 → the second whole lock, the contended `DISCARD` of the index buffer,
  mutants e, f, g. I6 → cell 3 of `buf-writeonly`; no mutant (ruling 12). I7 → the probe's proof lines and
  wanted figure; the bench's gap. I8 → as R2's I5. I9 → the start times. Minors → mutant d names one row;
  `jm.sh`'s edits and `jv0`'s single run in the script list; the fixed fall-back substring; 8 points a frame;
  the VM launch's duration; when each script is written; the `state` launch may hold the proof line too.
