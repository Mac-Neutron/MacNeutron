# EAC microVM, milestone 4b-1: winemetal forwarding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Linux program in the guest makes winemetal's calls through a guest `winemetal.so`, and the host runs them
on MacNeutron's own macOS `winemetal.so`. A fixed call sequence prints the same lines in the guest as it does when it
runs directly on the Mac. The sequence covers a device, buffers, a blit, a compute dispatch and a shared event that
signals the guest.

**Architecture:**
- **Guest library.** The guest `winemetal.so` (x86-64 Linux) exports winemetal's `__wine_unix_call_funcs[153]`.
  Each entry forwards its call over 4a's transport, one ring per guest thread.
- **Handles.** The guest picks every object handle: a generation plus an index into a per-context host table, which
  translates and validates it. So a call that creates an object and returns nothing else is queued, not waited for.
- **Ordering.** Because the host runs the rings concurrently, a handle that is used before its creation has run, or
  released while another ring still holds a queued use of it, waits for the other ring.
- **Host side.** The host executor loads the shipped `aarch64-unix/winemetal.so` against stand-ins for its two Wine
  libraries. The stand-in `ntdll.so`'s `NtSetEvent` turns host events into notifications, which the guest delivers to
  Wine.
- **Memory.** Memory the guest touches directly comes from blobs: Shared buffers, CPU-visible heaps and bulk data. The
  host keeps a blob held until Metal deallocates the buffer over it.

**Tech Stack:**
- C on both sides, and Objective-C on the host (the buffer-lifetime hook, and the `@try` around winemetal calls);
- DXMT's winemetal headers;
- 4a's transport (`eac-vm/metal`);
- clang for x86-64 Linux, through `eac-vm/sysroot/cc-x86_64.sh` (as 4a's guest programs are built);
- MacNeutron's `wine.app` build of `winemetal.so`.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.6 and §8 milestone 4.
- **Call catalog:** `docs/research/2026-10-15-vm-route/winemetal-calls.md`. For every slot it gives the fields,
  memory, handles, blocking behaviour and special cases. Slot numbers below are its slots.
- **Handle decision:** research README §7, "Handles for milestone 4b, measured". The maintainer chose guest handles
  with translation on 2026-10-16.

## Global Constraints

### Where the code and libraries come from

- **Worktree.** `.claude/worktrees/upstream-master`, branch `exp/upstream-master`. Commit locally, never push.
- **DXMT is the tree MacNeutron builds:** `build/wine-arm64-src/dxmt` (the pinned fork commit plus
  `wine-arm64/patches/dxmt`). Include its headers, don't modify it. The host runs
  `build/wine-arm64/wine.app/Contents/Resources/lib/wine/aarch64-unix/winemetal.so` unchanged.
- **Host libraries.** That `winemetal.so` resolves `@rpath/ntdll.so` and `@rpath/winemac.so`, and both files must
  exist. It imports only `NtSetEvent(HANDLE, LONG *)` from them, and reaches `macdrv_functions` through `dlsym`.
  - Our stand-ins and a copy of the `.so` sit in `build/eac-vm-work/metal-host/`.
  - `macneutron-vm` gets the rpath `@executable_path/metal-host`.
  - `macneutron-vm` is signed ad hoc without the hardened runtime, so library validation doesn't apply.
- **Guest library.** The guest `winemetal.so` links the guest Wine's `ntdll.so` (DT_NEEDED), as the Steam forwarder
  does. Wine `dlopen`s Unix libraries with `RTLD_NOW` only. The plain-Linux test programs load it with a stub
  `ntdll.so` on `LD_LIBRARY_PATH` that records `NtSetEvent` calls.

### Handles

- **Format.** A guest handle is `(uint64_t)generation << 32 | index`, and index 0 is the null handle.
  - The guest allocates indexes and bumps generations. It reuses a freed index only after 65,536 later frees (a FIFO).
  - The host translates a handle only if its generation matches the index's binding. Otherwise it refuses the call
    with `STATUS_INVALID_HANDLE` (0xC0000008) and never calls winemetal.
- **Kinds.** Each binding has a kind:
  - an Objective-C object;
  - an SM50 shader, bitcode or error (slots 74-88: C++ pointers);
  - a shared-event listener (slot 108: a malloc'd struct);
  - an emulated heap (Task 5).

  Each handle field in `calls.c` names the kind it takes, and the host refuses another kind. Some values are typed as
  handles but aren't objects, and are passed through untranslated:
  - slot 92's colour space;
  - slots 94-96's display ids;
  - slot 114's guest pointer, which the guest replaces with the copied bytes;
  - slots 104 and 146's Win32 event handles.
- **Ownership, per output field.** `calls.c` marks each handle output with one of these:
  - **+1:** creations.
  - **Autoreleased:** slots 7, 11, 28, 30-32, 60, 89-91, 129, 147, plus the `ret_error` field of slots 25, 29, 34,
    35, 98, 112, 113, 131, 132 and 144.
  - **Borrowed:** slots 2, 53 and 62. (Slots 66, 67, 71 and 72 also return borrowed or autoreleased handles, but
    they belong to 4c.)
- **References.** Both sides count references per handle in lockstep:
  - a binding starts at 1;
  - slot 0 (retain) adds 1 and slot 1 (release) takes 1;
  - at 0 the host sends the object its last `release` and unbinds (see Ordering), and the guest frees the index.
- **Autoreleased and borrowed lifetimes.** The host retains these objects when it binds them.
  - An autoreleased handle belongs to the innermost autorelease pool of its guest thread (its ring), or to the
    context when that thread has no pool open. Releasing a pool's handle (slot 1 on a slot-10 handle) drops one
    reference from each of the pool's handles, on both sides.
  - A borrowed handle belongs to the context until it ends.
  - `ponytail:` slot 66 (a drawable's texture, every frame) needs owner-tied release in 4c.

### Ordering across rings

Each guest thread's ring runs in order, but the host runs rings concurrently. These rules always resolve: a guest
thread can only use a handle that another thread created, or still holds, after that thread published its records.
- **A use before the binding.** The handle's creation is still queued on another ring, so the use waits for the
  binding, up to 5 s. After that the call is refused and counted. Generations only grow, so a handle whose generation
  is below the index's binding, or equal to it and unbound, is stale and refused at once.
- **A last release.** It unbinds and releases the object only once every other ring of the context has finished the
  records it had published when the release ran.
  - Consumers sweep these deferred releases every 64 records and before they park.
  - A detached ring counts as finished.
  - Context end releases everything.
- **A bind at an index still bound** waits up to 5 s, then is refused and counted. The FIFO makes this practically
  unreachable.

### Which calls wait

- **Synchronous slots (74).** A call waits for the host only for one of these reasons, and `calls.c` marks exactly
  these slots:
  - **Data other than guest-chosen handles comes back** (scalars, strings, bytes): slots 3, 5, 6, 8, 14, 16, 24, 27,
    39-44, 49-52, 63, 68, 69, 77, 79, 88, 91, 92, 94-96, 99, 111, 119, 125, 128, 130, 139, 140, 145 and 148-150.
  - **GPU addresses or resource ids come back:** slots 18, 19, 21-23 and 141-143.
  - **It can return an error handle:** slots 25, 29, 34, 35, 98, 112, 113, 131, 132 and 144.
  - **Nil is an answer the caller tests:** slots 56, 89, 115, 116 (a cache miss), 127 and 151.
  - **It's an SM50 call with outputs:** slots 74, 76, 81, 82, 84 and 85.
  - **It blocks:** slots 13, 117 and 126.
- **Nil handles in a reply.** A synchronous reply says which handle outputs came back nil. The guest writes 0 for those
  and frees their indexes.
- **Queued slots (62).** The rest, except those the guest refuses. A queued creation whose host call returns nil stays
  bound to nil: winemetal's nil-safe messaging makes later calls on it no-ops, and the failure is counted.
- **Bulk data waits.** A queued call with more than `MN_REC_MAX` (256 KiB) of data goes through its thread's scratch
  blob and waits, so the scratch is free again when it returns. Output data past `MN_REPLY_BYTES` (64 KiB) also goes
  through the scratch blob.
- **Refused in the guest (17):** the guest answers these itself and logs each slot once. The host refuses them too.
  - slots 47, 48, 66, 67, 70-73, 93, 97 and 101 (presentation, which is 4c): `STATUS_NOT_IMPLEMENTED` (0xC0000002);
  - slots 120-124 (Mach ports: sharing across processes on the host), and slot 83 (NULL in winemetal):
    `STATUS_NOT_SUPPORTED` (0xC00000BB).
- **Stats.** `MN_OP_WM_STATS` (op `MN_OP_WM + 153`, synchronous) replies the context's live handles, its deferred
  releases and the executor's async failures, after a sweep. The gate reads it, because `--metal-test-ops` (and its
  ERRORS op) is off in winemetal mode.

### Memory and paths

- **Blob memory.** A buffer over blob memory starts on a 16 KiB page and spans whole 16 KiB pages:
  `newBufferWithBytesNoCopy` needs page-aligned memory, and the host page is 16 KiB. D3D12's 64 KiB placement keeps
  heap-backed buffers aligned.
- **Host paths** (slots 54, 112, 113, 115, 117, 119). The guest's path is never used on the host. The host maps it to a
  file in `~/Library/Caches/MacNeutron/eac-vm/metal/`, named by a hash of the guest path. Every context shares the
  directory, just as they share the guest path.

### Scope, trust and process

- **4b-1 has no Wine, no DXMT and no game.** The test programs call `__wine_unix_call_funcs[]` with params structs built
  from DXMT's headers, as winemetal's PE thunks would. `--metal-test-ops` stays for 4a's gate; otherwise the executor's
  handler is winemetal's dispatcher.
- **Untrusted guest:**
  - The host checks every length, count, offset and handle from the guest before it uses them. The checks include
    the ones winemetal doesn't make itself (Tasks 5 and 8).
  - The host walks a command list at most `MN_WM_MAX_NODES` (65,536) nodes.
  - Winemetal runs inside `@try`. An Objective-C exception (an object of the wrong class) refuses the call with
    `STATUS_INVALID_PARAMETER` (0xC000000D) and is counted.
- **Process discipline:**
  - lighter testing: one gate per task, then the full Metal gates (`eac-vm/metal/gate.sh`) at the end;
  - gates assert;
  - privacy scan before docs commits;
  - commit trailers.

## Review Focus

1. **Cross-ring order.** A handle is created or released on one guest thread's ring and used on another's, and the host
   runs the rings out of guest order.
   - Expected: the use waits for the creation, the release waits for the use, and the result matches a
     single-threaded run.
   - Tests: Task 2's `cross-ring-use` and `cross-ring-release`, and Task 6's `cross-thread`.
2. **A forged, stale, wrong-kind or drained-pool handle.**
   - Expected: refused, and nothing dereferenced.
   - Tests: Task 2's `stale`, `unbound`, `out-of-range`, `kind-mismatch` and `pool`, and Task 3's `bad-handle`.
3. **A guest that frees a blob while a buffer over it is still in Metal's hands.**
   - Expected: the memory stays mapped until Metal deallocates the buffer, and the guest doesn't reuse the region
     before then.
   - Test: Task 5's `buffer-outlives-free`.
4. **A guest-supplied size that winemetal trusts.** The cases:
   - slot 107's offset plus length;
   - the region of slots 45 and 145;
   - slot 79's `buffer_size` 0;
   - slot 88's output arrays;
   - a looping or over-long command list.

   Expected: refused before winemetal runs. Tests: Task 5's `update-bounds` and `region-bounds`, Task 6's
   `cmdlist-loop`, and Task 8's `sm50-sizes`.
5. **A creation that returns nil.**
   - Expected: where nil is an answer (a slot-116 miss), the guest gets 0 and the index back. Where nil is a failure
     on a queued call, the handle stays bound to nil, its later calls are no-ops, and the failure is counted.
   - Tests: Task 3's `nil-answer` and `create-fails`.

---

### Task 1: the host runs the shipped winemetal.so

**Files:**
- Create:
  - `eac-vm/metal/wm/host/stand-ins.c`: `ntdll.so`, with `NtSetEvent` calling a hook set through an exported setter;
    `winemac.so`, empty;
  - `eac-vm/metal/wm/host/loader.c` and `loader.h`;
  - `eac-vm/metal/wm/tests/sequence.c` and `sequence.h`: the call sequence, written once so that the host test and
    the guest gate both run it;
  - `eac-vm/metal/wm/tests/host-test.c`;
  - `eac-vm/metal/wm/Makefile` (Mac).
- Modify: `eac-vm/host/build.sh`, which builds the stand-ins into `build/eac-vm-work/metal-host/`, copies
  `winemetal.so` there and adds the rpath.

**Interfaces:**
```c
/* loader.h */
typedef unsigned int NTSTATUS_T;
typedef NTSTATUS_T (*wm_unix_fn)(void *params);
int mn_wm_load(const char *dir);             /* dlopen dir/winemetal.so (beside its stand-ins): 0 or -1 */
extern const wm_unix_fn *mn_wm_funcs;         /* its __wine_unix_call_funcs[153] */
void mn_wm_set_event_hook(void (*hook)(uint64_t nt_event)); /* what the stand-in NtSetEvent calls */
/* sequence.h */
struct mn_wm_env {
    NTSTATUS_T (*call)(unsigned slot, void *params);    /* the real table on the Mac, the forwarder in the guest */
    void (*print)(const char *line);
    int (*signalled)(uint64_t nt_event, int timeout_ms); /* 1 once NtSetEvent(nt_event) happened */
};
int mn_wm_sequence(const struct mn_wm_env *env, int steps); /* steps 0 = all; 0 when every step's result matched */
```

**The sequence** (`sequence.c`), each step printing one line:
1. Get the device: slot 4, then the first of the array through slots 3 and 2.
2. Print its name (slots 7 and 8).
3. Inside an autorelease pool (slot 10), make a command queue (slot 9) and a 64 KiB Shared buffer (slot 18, memory
   NULL).
4. Write a pattern into the buffer through `memory.ptr`.
5. Make a second buffer, then a command buffer (slot 11).
6. Blit-copy the first buffer into the second and fill a range (slot 30, then slot 36 with nodes 1 and 8).
7. Compile an MSL compute kernel from source that doubles each word (slots 144, 26, 29), and dispatch it over the
   second buffer (slot 31, then slot 37 with nodes 3, 4 and 1).
8. Make a shared event (slot 15).
9. Register Win32 event 0x1234 at value 1 (slot 104) on a listener (slot 108, with slot 109 on its own thread).
10. Signal the event at 1 (slot 17).
11. Commit and wait (slots 12 and 13).
12. Print the second buffer's checksum and status (slot 14), and wait for 0x1234 to have been signalled.
13. Release everything (slots 110 and 1), with the pool last.

- [ ] **Step 1: Write `host-test.c`.** It loads the `.so` with `mn_wm_load` and runs `mn_wm_sequence` on `mn_wm_funcs`.
  Its event hook records each handle for `signalled`. It requires these lines:
  - `device ok`;
  - `name <non-empty>`;
  - `blit ok`;
  - `compute ok sum=<the doubled pattern's sum, computed on the CPU>`;
  - `event 0x1234 signalled`;
  - `released ok`.
- [ ] **Step 2: Run it, expecting a failure.** Run `make -C eac-vm/metal/wm host-test`. Expected: FAIL (no loader).
- [ ] **Step 3: Implement** the stand-ins, the loader and `sequence.c`.
- [ ] **Step 4: Run, expecting a pass.** `host-test` prints all the lines, under ASan.
- [ ] **Step 5: Commit.** `eac-vm: the host runs MacNeutron's winemetal.so (milestone 4b-1)`.

### Task 2: handle tables

**Files:**
- Create:
  - `eac-vm/metal/wm/handles.h`;
  - `eac-vm/metal/wm/guest/ghandles.c`: the allocator (with the FIFO), references, and the autorelease-pool mirror per
    thread;
  - `eac-vm/metal/wm/host/htable.c`: per context, it binds, translates and counts references, keeps pools per ring,
    and runs the ordering rules;
  - `eac-vm/metal/wm/tests/handles-test.c` (Mac, ASan).

**Interfaces:**
```c
typedef uint64_t mn_handle; /* (generation << 32) | index; 0 = null */
enum mn_hkind { MN_HK_OBJC = 1, MN_HK_SM50_SHADER, MN_HK_SM50_BITCODE, MN_HK_SM50_ERROR, MN_HK_LISTENER, MN_HK_HEAP_EMU };
enum mn_own { MN_OWN_PLUS1, MN_OWN_AUTORELEASED, MN_OWN_BORROWED };
/* guest */
mn_handle mn_gh_alloc(enum mn_own own);  /* refs 1; autoreleased ones join this thread's innermost pool */
void mn_gh_retain(mn_handle h);
int  mn_gh_release(mn_handle h);          /* 1 when the index was freed */
void mn_gh_drop(mn_handle h);             /* a synchronous reply said nil: free the index, no host release */
void mn_gh_pool_push(mn_handle pool);
void mn_gh_pool_pop(mn_handle pool);      /* one reference less for each handle the pool holds */
/* host: one table per context; rings are identified by the executor's ring_state pointer */
struct mn_mark { void *ring; uint64_t pos; };
struct mn_ht_rings {                      /* the executor's (Task 3); fakes in handles-test */
    void *ctx;
    size_t (*snapshot)(void *ctx, void *except, struct mn_mark *marks, size_t cap); /* published positions */
    int (*passed)(void *ctx, const struct mn_mark *marks, size_t n);               /* each finished past its mark */
};
struct mn_ht *mn_ht_new(const struct mn_ht_rings *rings, void (*release)(enum mn_hkind kind, void *obj));
void  mn_ht_free(struct mn_ht *t);        /* context end: releases every bound and deferred object */
int   mn_ht_bind(struct mn_ht *t, void *ring, mn_handle h, enum mn_hkind kind, void *obj, enum mn_own own);
void *mn_ht_get(struct mn_ht *t, mn_handle h, enum mn_hkind kind, int *ok); /* ok 0: refuse the call */
void  mn_ht_retain(struct mn_ht *t, mn_handle h);
void  mn_ht_release(struct mn_ht *t, void *ring, mn_handle h);   /* the last one is deferred (ordering) */
void  mn_ht_sweep(struct mn_ht *t);       /* runs the deferred releases whose rings have passed */
void  mn_ht_pool_push(struct mn_ht *t, void *ring, mn_handle pool);
void  mn_ht_pool_pop(struct mn_ht *t, void *ring, mn_handle pool);
size_t mn_ht_live(struct mn_ht *t);
```

- [ ] **Step 1: Write the failing tests.** Each runs with fake objects (pointers into an array whose releases are
  counted) and fake rings (positions the test moves).
  - `bind-get`.
  - `stale`: after a release frees the index, the old handle is refused, and a rebind with the next generation
    resolves.
  - `unbound`, `out-of-range`, and `kind-mismatch` (an SM50 handle where an Objective-C one is expected).
  - `refs`: retain, then two releases, then the object is released once.
  - `pool`: push, bind two autoreleased handles, retain one, pop. One is freed on both sides, and the retained one
    survives with refs 1.
  - `nested-pools`.
  - `borrowed`: it survives a pool pop and is released by `mn_ht_free`.
  - `cross-ring-use`: a lookup on ring B of a future-generation handle returns once ring A's thread binds it 50 ms
    later. With no bind, it is refused after the timeout (5 s).
  - `cross-ring-release`: ring A's last release while fake ring B's position is behind its mark. The object stays
    bound and unreleased through a sweep, and is released at the first sweep after B passes.
  - `reuse-fifo` (guest): an index isn't handed out again before 65,536 later frees.
  - `threads`: two rings each push and pop their own pools while binding concurrently.
  - `free-all`.
- [ ] **Step 2: Run them, expecting a failure.** Run `make -C eac-vm/metal/wm handles-test`. Expected: FAIL.
- [ ] **Step 3: Implement.**
  - The host table is a growable array indexed by handle index, up to 2^20 entries, with a mutex and a condition
    variable per context.
  - Deferred releases are a FIFO, and the sweep stops at the first entry that hasn't passed.
  - `ponytail:` one lock per context; per-index locks if contention shows.
- [ ] **Step 4: Run, expecting a pass.**
- [ ] **Step 5: Commit.** `eac-vm: guest-chosen Metal handles and the host's table`.

### Task 3: call table, marshaller, and the host dispatcher

**Files:**
- Create:
  - `eac-vm/metal/wm/calls.h` and `calls.c`: one `mn_wm_call` per slot, built with `offsetof`/`sizeof` on DXMT's
    headers;
  - `eac-vm/metal/wm/marshal.c` and `marshal.h`: pack (guest), and unpack, call and reply (host);
  - `eac-vm/metal/wm/host/dispatch.m`: the executor's handler and hooks for winemetal ops (`@try` around each call);
  - `eac-vm/metal/wm/tests/marshal-test.c` (Mac, ASan).
- Modify:
  - `eac-vm/metal/proto.h`: adds `MN_OP_WM = 0x1000`; a call is op `MN_OP_WM + slot`, and stats is
    `MN_OP_WM + 153`;
  - `eac-vm/metal/host/executor.c` and `executor.h`: hooks, marks and context end (below);
  - `eac-vm/metal/tests/executor-test.c`;
  - `eac-vm/host/macneutron-vm.swift`: passes the new `mn_executor_start` argument.

**Interfaces:**
```c
enum mn_wm_field_kind { WMF_HANDLE_IN, WMF_HANDLE_OUT, WMF_IN, WMF_OUT, WMF_INOUT, WMF_STR_IN, WMF_VALUE_HANDLE };
struct mn_wm_field {
    uint16_t offset; uint8_t kind;           /* enum mn_wm_field_kind */
    uint8_t hkind, own;                      /* handle fields: enum mn_hkind; outputs: enum mn_own */
    uint8_t flags;                           /* WMF_ARRAY */
    uint32_t size;                           /* bytes, or per element with WMF_ARRAY */
    uint16_t count_offset; uint8_t count_width; uint8_t nested; /* nested: index into mn_wm_nested (handles inside) */
};
struct mn_wm_call {
    const char *name; uint32_t params_size;
    uint8_t mode;                            /* MN_WM_QUEUED, MN_WM_SYNC, MN_WM_REFUSED */
    uint8_t special;                         /* an mn_wm_special_* handler instead of the generic path */
    uint32_t refuse_status;                  /* MN_WM_REFUSED */
    uint16_t nfields; const struct mn_wm_field *fields;
};
extern const struct mn_wm_call mn_wm_calls[153];
int  mn_wm_pack(unsigned slot, const void *params, struct mn_wm_out *out); /* guest: a record; out handles allocated */
int32_t mn_wm_host_call(struct mn_wm_ctx *c, void *ring, unsigned slot, const void *rec, uint32_t len,
                        void *reply, uint32_t cap, uint32_t *reply_len);
/* executor.h */
struct mn_executor_hooks {
    void (*progress)(uint32_t ctx_id, void *ring_state); /* every 64 records and before parking */
    void (*context_end)(uint32_t ctx_id);                 /* after the context's rings have stopped */
};
void mn_executor_start(mn_op_fn handler, const struct mn_executor_hooks *hooks); /* hooks may be NULL */
size_t mn_executor_snapshot(uint32_t ctx_id, void *except, struct mn_mark *marks, size_t cap);
int mn_executor_passed(uint32_t ctx_id, const struct mn_mark *marks, size_t n);
```

- [ ] **Step 1: Write the failing tests.**
  - `marshal-test.c` packs on a fake guest side, unpacks into a fake winemetal table (functions that record their
    params and write outputs), and checks the round trip for each field kind.
  - `bad-handle`: a stale handle refuses the call before the fake runs.
  - `create-fails`: a queued creation whose fake returns nil leaves a nil binding and counts the failure.
  - `nil-answer`: slot 116's fake returns nil. The reply marks it, the guest's `ret_data` is 0 and its index is free.
  - `exception`: a fake that raises refuses its call with `STATUS_INVALID_PARAMETER`, and the next call runs.
  - `coverage`: all 153 slots have a descriptor or a special handler, and each `params_size` matches the `sizeof` of
    its params struct.
  - `modes`: the synchronous, queued and refused slots are exactly the lists in Global Constraints, written into the
    test (74, 62 and 17).
  - `executor-test` gets `marks`: a snapshot of two rings, which passes only once the second ring finishes past its
    mark. It also gets `hooks`: `progress` runs before a park, and `context_end` after a context's rings stop.
- [ ] **Step 2: Run them, expecting a failure.**
- [ ] **Step 3: Implement.**
  - Build the table from the catalog's columns, then write the marshaller and `dispatch.m`.
  - `progress` calls `mn_ht_sweep`, and `context_end` calls `mn_ht_free`.
  - The executor keeps each ring's finished position.
- [ ] **Step 4: Run, expecting a pass.**
- [ ] **Step 5: Commit.** `eac-vm: winemetal's call table and marshaller`.

### Task 4: the guest winemetal.so, first end to end

**Files:**
- Create:
  - `eac-vm/metal/wm/guest/winemetal.c`: `__wine_unix_call_funcs[153]` stubs, each packing and then
    `mn_send`/`mn_call`, or answering a refused slot itself;
  - `eac-vm/metal/wm/guest/stub-ntdll.c`: for the test programs only;
  - `eac-vm/metal/wm/tests/guest-test.c`: runs `mn_wm_sequence` through the guest table, then prints
    `stats failures=<n> live=<n> deferred=<n>`;
  - `eac-vm/tests/metal-wm.sh`, the gate.
- Modify:
  - `eac-vm/metal/build-guest.sh`, which also builds `winemetal.so` (linked against the guest Wine build's
    `ntdll.so`), the stub `ntdll.so`, and `metal-wm-test`;
  - `eac-vm/metal/gate.sh`, which runs host-test first, writes its lines to the share as `metal-wm-host.txt`, then
    runs `metal-wm.sh`;
  - `eac-vm/host/macneutron-vm.swift`: without `--metal-test-ops`, it calls `mn_wm_load`, then
    `mn_executor_start(mn_wm_dispatch, &mn_wm_hooks)`.

- [ ] **Step 1: Write the gate.** `metal-wm.sh` runs `metal-wm-test` with `LD_LIBRARY_PATH` set to the stub. It
  requires:
  - `metal-wm-host.txt`'s lines, in order;
  - `stats failures=0 live=1 deferred=0` (the device from slot 2 is borrowed, so the context holds it until the end).

  Until Tasks 5-7, the sequence stops after step 2, behind a `--steps` argument: `device ok` and `name ...`.
- [ ] **Step 2: Run it, expecting a failure.** Run `eac-vm/run-guest.sh eac-vm/tests/metal-wm.sh`. Expected: FAIL.
- [ ] **Step 3: Implement** `winemetal.c` and the wiring.
- [ ] **Step 4: Run, expecting a pass.** `device ok` and `name` match the Mac's lines, and 4a's gates still pass
  (`eac-vm/metal/gate.sh`).
- [ ] **Step 5: Commit.** `eac-vm: winemetal calls from the guest, first end to end`.

### Task 5: memory the guest touches

**Files:**
- Create:
  - `eac-vm/metal/wm/guest/galloc.c`: the blob sub-allocator. Regions are 16 KiB multiples in 64 MiB blobs. A freed
    region is reused only after the host says the buffer over it is gone.
    - `ponytail:` blobs are kept until the context ends; free empty ones if guest memory pressure shows.
  - `eac-vm/metal/wm/host/lifetime.m`: an associated object whose `dealloc` releases the blob hold and queues the
    region's release to the guest.
  - `eac-vm/metal/wm/notify.h`: host-to-guest queues in blobs (host producer under a mutex, guest consumer).
    - Records are `{type, value}`.
    - Each context has a queue for `MN_NOTIFY_REGION_FREE`, which the allocator drains when it allocates.
    - Each listener gets its own queue in Task 7.
  - `eac-vm/metal/wm/tests/memory-test.m` (Mac, real Metal).
- Modify: `calls.c`, which gets the special handlers.
  - **Slot 18:**
    - Memory NULL and not Private: a blob region, with `memory.ptr` set to its guest address.
    - Memory not NULL: it must lie in a blob region, otherwise `STATUS_INVALID_PARAMETER`.
    - Either way the host gets a held region.
  - **Slots 138 and 141, the emulated heap.** A non-Private heap is a blob region emulated on the host, with no
    `MTLHeap`.
    - Slot 141 makes `newBufferWithBytesNoCopy` over the heap's region at the given offset. An offset of `~0`, or a
      range outside the heap, is refused.
    - DXMT adds a heap to its residency set at creation and never adds the heap's buffers (`d3d12_heap.cpp:93`,
      `RegisterResidencyAndVA`). So an emulated heap remembers the sets that slot 133 added it to. Each buffer slot
      141 places in it is added to those sets, which are then committed. Slots 134 and 135 forget the association.
    - Slot 142 on an emulated heap returns `STATUS_NOT_SUPPORTED`. D3D12 puts textures in default (Private) heaps.
  - **Bulk data**, both directions: the record, or the scratch blob, as Global Constraints says.
  - **Slot 77:** the bitcode is copied into guest memory, which is freed at slot 78.
  - **Bounds winemetal doesn't check:**
    - slot 107: offset plus length must fit in the buffer's length;
    - slots 45 and 145: the region must lie within the texture's level and slice, using the sizes the host asks Metal
      for.
- Tests:
  - `memory-test` (Mac):
    - `buffer-outlives-free`: a real `MTLBuffer` over a held region, used by an uncompleted command buffer, survives
      the blob's free;
    - `update-bounds`;
    - `region-bounds`;
    - `heap`: a buffer at offset 64 KiB has contents at the region plus 64 KiB, and joins the residency set that the
      heap was added to.
  - `galloc` cases in `guest-test`: alignment, length rounding, and no reuse before `MN_NOTIFY_REGION_FREE`.

- [ ] **Step 1: Write the failing tests.** Advance the gate sequence's `--steps` past step 4, so the guest writes the
  pattern through `memory.ptr` and the host blit reads it.
- [ ] **Step 2: Run them, expecting a failure.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expecting a pass.**
- [ ] **Step 5: Commit.** `eac-vm: winemetal buffers, heaps and bulk data over blobs`.

### Task 6: command lists

**Files:**
- Create:
  - `eac-vm/metal/wm/cmdlist.c` and `cmdlist.h`: a per-encoder table of node types giving each type's size, handle
    offsets and nested pointer fields, from the catalog's §1. The nested fields are `setbytes.bytes`/`length`,
    `viewports`/`viewport_count` and `scissor_rects`/`rect_count`.
  - the guest serialiser and the host rebuilder.
- Test:
  - `marshal-test` gets:
    - `cmdlist-roundtrip`: every node type of all three encoders;
    - `cmdlist-loop`: a cycle, and 65,537 nodes, both refused;
    - `cmdlist-unknown-type`.
  - `guest-test` gets `cross-thread`:
    - Thread A creates a buffer behind 10,000 queued retain/release pairs on the device, so its ring lags. It hands
      the buffer to thread B, which blits from it at once.
    - A then releases the buffer, while B's ring is itself held behind 10,000 pairs.
    - The copy must be right, and stats must show 0 failures.

- [ ] **Step 1: Write the failing tests.** Advance the gate past steps 6-7 (blit and compute).
- [ ] **Step 2: Run them, expecting a failure.**
- [ ] **Step 3: Implement.**
  - The guest walks at most `MN_WM_MAX_NODES`, copying each node and its nested payloads into the record (or the
    scratch blob).
  - The host rebuilds the list in its own memory. It rewrites `next` and the nested pointers and translates each
    handle, then calls the real slot.
- [ ] **Step 4: Run, expecting a pass.**
- [ ] **Step 5: Commit.** `eac-vm: winemetal command lists across the guest boundary`.

### Task 7: shared events and the listener

**Files:**
- Modify `calls.c`, which gets the special handlers.
  - **Slot 108:** a listener binding with its own notify queue.
  - **Slot 109:** queued. The host runs the real `SharedEventListener_start` on its own thread per listener, never on a
    ring consumer, and sets the thread's notify queue for the `NtSetEvent` hook. On the guest side, the stub loops on
    `MN_NOTIFY_EVENT` records, calling Wine's `NtSetEvent` for each, until it reads `MN_NOTIFY_STOP`.
  - **Slot 110:** the host stops the listener, then queues `MN_NOTIFY_STOP`.
  - **Slots 104 and 146:** the Win32 handle travels as a value.
  - **Slot 126:** it blocks its own ring only.
- Add `MN_CTRL_NOTIFY_WAIT` to `proto.h`. Its fence completes once the named queue has records past `arg`.
- Wire the stand-in `NtSetEvent` hook to the queue of the listener thread that called it.

- [ ] **Step 1: Write the failing tests.** Advance the gate past step 12, so the stub `ntdll.so` must record
  `NtSetEvent(0x1234)`.
- [ ] **Step 2: Run them, expecting a failure.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expecting a pass.** The guest prints every line of the sequence, matching host-test.
- [ ] **Step 5: Commit.** `eac-vm: shared events signal the guest's Win32 events`.

### Task 8: the rest of the table

**Files:**
- Modify `calls.c` for:
  - **SM50** (slots 74-88):
    - Shaders, bitcode and errors are handles of their own kinds.
    - Slot 74 stores the reflection's `NumConstantBuffers` and `NumArguments` with the shader, and slot 88 sizes its
      two output arrays from those, never from the guest.
    - The argument lists of slots 76, 81, 82, 84 and 85 are walked like command lists. They are bounded and read by
      type, and their nested element arrays and root-signature bytecode are copied.
    - Slot 79 refuses a `buffer_size` of 0.
  - **Paths:** slots 54, 112, 113, 115, 117 and 119, mapped into the host cache directory.
  - **Display:** slots 92, 94-96, 99, 100 and 111, forwarded.
  - **Strings, errors and logs:** slots 8, 27, 28, 60, 61 and 89-91. Slot 91 pre-allocates `buffer_size` handles and
    frees the ones past `ret_read`.
  - **Residency sets, counters, binary archives, indirect command buffers and MetalFX:** the generic path.
- Modify `guest/winemetal.c`: a `pthread_atfork` child handler drops the thread's ring and context, so the child's
  first call opens its own.
- Docs:
  - research README §8, "winemetal forwarding (4b-1)";
  - the spec's milestone 4 line;
  - memory.

- [ ] **Step 1: Write the failing tests.**
  - `marshal-test` gets:
    - `sm50-sizes`: slot 88's outputs follow slot 74's counts even when the guest claims more, and slot 79 with
      `buffer_size` 0 is refused;
    - `sm50-args`: a looping argument list is refused;
    - `paths`;
    - `strings`;
    - `enumerate`.
  - `guest-test` gets:
    - `refused`: each refused slot answers its status and logs once;
    - `fork`: a child after `fork` makes a call through its own ring.
- [ ] **Step 2: Run them, expecting a failure.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run, expecting a pass.** Then run the full Metal gates: `eac-vm/metal/gate.sh`, which includes
  `metal-wm.sh`.
- [ ] **Step 5: Commit.** `eac-vm: the rest of winemetal's table forwarded (milestone 4b-1)`.

## After 4b-1: plan 4b-2 (DXMT in the guest)

4b-2 gets its own plan once 4b-1's gate passes.

- **DXMT's x86-64 PE build.** It needs a DXMT patch: a meson option to skip the macOS Unix side for a guest build. It
  is built with MacNeutron's llvm-mingw (`build/dxmt-src/llvm-mingw-own`) against the guest Wine build's x86-64
  import libraries (`winecrt0`, `ntdll`, `dbghelp`), and marked builtin.
- **Game-allocated memory over blobs.** A DXMT patch adds `WMTMemoryAlloc`/`WMTMemoryFree` thunks (on macOS,
  `aligned_malloc`/`free`) for every site that hands its own memory to slot 18 on x86-64:
  - `dxmt_buffer.cpp:44-46`: `CpuPlaced`, which D3D11 buffers, dynamic textures and staging use;
  - `d3d12_command_allocator.cpp:63`: the 4 MiB GPU heap;
  - `dxmt_context.cpp:60`: the dummy constant buffer;
  - `dxmt_ring_bump_allocator.hpp:119`: placed blocks. These are plain `malloc`, page-aligned on macOS only because
    large allocations are.

  The `aligned_malloc` sites at `dxmt_texture.cpp:198` and `dxmt_occlusion_query.hpp:170` are i386-only.
- **Packaging.** DXMT's DLLs replace Wine's `d3d11`, `d3d12`, `dxgi` and `d3d10core` in `wine.erofs`, with the guest
  `winemetal.so` in `x86_64-unix`.
- **Gate.** The 17 headless tests (research §7 inventory) run in the guest under Wine. The guest's output must match
  our DXMT on macOS for the same exe and arguments:
  - lines;
  - pixels, within ±1 per channel;
  - DXIL words, compared by `compare.py`.

  The macOS reference comes from the launcher (`macneutron launch waitforexitandrun`, as `dxmt/check.sh` runs it),
  skipping timing and counter lines.
