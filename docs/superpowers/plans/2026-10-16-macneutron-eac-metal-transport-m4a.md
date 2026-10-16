# EAC microVM, milestone 4a: Metal transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An x86-64 program in the guest sends commands to, and gets replies from, an executor in the VM host process
through a virtio-gpu context type `dxmt`: shared blob memory, one command ring per guest thread, host consumers that
spin and then park, and fenced waits. The transport is measured against the research ring.

**Architecture:** libkrun's virtio-gpu device (`device.rs`, `worker.rs`, `protocol.rs`, `virtio_gpu.rs`) stays as
upstream has it. Its `rutabaga_gfx` dependency is replaced, through Cargo's `[patch.crates-io]`, by `rutabaga_dxmt`, a
crate inside our libkrun fork that implements the items libkrun imports. It has one context type, capset 63 (`dxmt`),
and no virglrenderer. Blobs are 16 KiB-aligned host `mmap`s, which libkrun maps into the guest's host-visible window.
Command bytes and fences go to C callbacks that the VM host registers. The guest uses only Fedora's signed
`virtio-gpu.ko`, through the in-tree DRM uAPI. A small C library on each side owns the ring protocol (`eac-vm/metal/`).
No DXMT is involved: milestone 4b puts winemetal's calls on this transport, and 4c adds presentation.

**Tech Stack:** Rust (`rutabaga_dxmt`, in libkrun), C (executor, guest transport, tests), Swift (VM host), the
virtio-gpu DRM uAPI (`drm/virtgpu_drm.h`, already in `eac-vm/sysroot`), and clang targeting x86-64 Linux through
`eac-vm/sysroot/cc-x86_64.sh`.

**Spec:** `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` §5.6 and §8 milestone 4. Transport
numbers: `docs/research/2026-10-15-vm-route/README.md` §3.

## Global Constraints

- Worktree `.claude/worktrees/upstream-master`, branch `exp/upstream-master`; commit locally. libkrun changes are
  patches in `eac-vm/libkrun/patches/` (0005 onwards), made on branch `macneutron` in
  `build/eac-vm-work/libkrun-src` (the recipe is in `eac-vm/libkrun/README.md`). Never push the fork without a yes.
- The guest loads no module of ours: signed modules only, under `module.sig_enforce=1` and `lockdown=integrity`, and
  EAC Linux v2 requires the same. `virtio-gpu.ko` comes from Fedora's `kernel-modules-7.2.10-200.fc44`, which is
  signed; `kernel-modules-core` doesn't carry it.
- The guest side uses only these, on `/dev/dri/renderD128` (mode 0666 by udev):
  - `DRM_IOCTL_VIRTGPU_GETPARAM` (`CONTEXT_INIT`, `RESOURCE_BLOB`, `HOST_VISIBLE`, `SUPPORTED_CAPSET_IDs`);
  - `CONTEXT_INIT` (`CAPSET_ID` 63, `NUM_RINGS` 64);
  - `RESOURCE_CREATE_BLOB` (`VIRTGPU_BLOB_MEM_HOST3D`, `VIRTGPU_BLOB_FLAG_USE_MAPPABLE`);
  - `MAP` and `mmap`;
  - `EXECBUFFER` (`VIRTGPU_EXECBUF_RING_IDX`, `VIRTGPU_EXECBUF_FENCE_FD_OUT`);
  - `GEM_CLOSE`.
- `MN_CAPSET_DXMT` is 63, because the driver's capset mask is 64 bits wide.
- Every blob size is a multiple of 16,384 bytes. Linux 7.2 rejects other sizes once the device offers
  `blob_alignment`, and with only aligned sizes, `drm_mm` placement keeps every offset aligned. The host also refuses a
  `RESOURCE_MAP_BLOB` whose offset isn't a multiple of its page size, and logs it.
- No new dependency (no virglrenderer, no MoltenVK), and nothing is installed on the Mac.
- A host consumer spins at most 2 ms after its ring's last command, then parks. Only rings with work spin.
- The executor runs in `macneutron-vm`'s process. `krun_vmm_run` runs on a background thread, and the main thread runs
  the main dispatch queue, because 4b and 4c need `dispatch_sync` to main.
- Guest tests are x86-64 Linux programs under FEX, run as `mn` and built with `cc-x86_64.sh` in the builder.
- Testing follows the lighter regime: one gate per task, then identity, net and wine after the image change. The full
  `m2-gate.sh` runs in Task 4. Gates assert and exit non-zero on a miss.
- Run a privacy scan before committing docs. Commit messages end with the session's two trailer lines.

## Review Focus

1. **A lost wake.** The guest publishes a command just as the host parks. Expected: the command is processed and
   nothing stays stuck. Task 3's `gap` test.
2. **A blob freed while a queued command still uses it.** Expected: the host is done with it before it is unmapped.
   `mn_blob_free` is a synchronous release that goes through the ring. Task 3's `free-in-flight` test.
3. **A misaligned or out-of-window blob offset.** Expected: refused with a log line, and the VM keeps running.
   Task 1's `map_blob_rejects_unaligned_offset` test.
4. **More guest threads than fence rings (64).** Expected: every thread completes, and the extra threads wait by
   polling instead of fences. Task 3's `threads 100` test.
5. **A synchronous call right after asynchronous commands on the same thread.** Expected: it sees their effects,
   because each ring keeps order. Task 3's `order` test.

---

### Task 1: rutabaga_dxmt in our libkrun

**Files:**
- Create (in `build/eac-vm-work/libkrun-src`, exported as patch 0005): `src/rutabaga_dxmt/Cargo.toml` and
  `src/rutabaga_dxmt/src/lib.rs`; the workspace `Cargo.toml` gets `[patch.crates-io] rutabaga_gfx = { path = "src/rutabaga_dxmt" }`.
- Modify (same patch): `src/devices/src/virtio/gpu/virtio_gpu.rs`, where `resource_map_blob` refuses an offset that
  isn't a multiple of the page size and logs it; `src/libkrun/src/api/device_builders.rs`, which gets the constructor
  below.
- Modify: `eac-vm/libkrun/build.sh` (`--features blk,net,ffi,gpu`) and `eac-vm/libkrun/README.md` (the patch list,
  plus why the stand-in exists: virglrenderer is absent, and rutabaga's component traits are private).

**Interfaces:**
- **Produces (Rust):** a crate named `rutabaga_gfx`, version 0.1.85, with an empty `virgl_renderer` feature. It
  exposes every item libkrun's gpu code imports (`grep -rh rutabaga_gfx src` lists them: `Rutabaga`,
  `RutabagaBuilder`, `RutabagaFence`, `RutabagaFenceHandler`, `RutabagaError`, `RutabagaResult`, `RutabagaIovec`,
  `RutabagaPath`, `RutabagaDescriptor`, `RutabagaComponentType`, `ResourceCreate3D`, `ResourceCreateBlob`,
  `Transfer3D`, and the `RUTABAGA_*` constants), with the signatures libkrun calls. Only the dxmt behaviour is real:
  - `get_capset_info(0)` returns capset 63, version 0, size 0;
  - `create_context` accepts capset 63 only;
  - `resource_create_blob` takes `HOST3D` and mappable blobs only, and allocates
    `mmap(MAP_ANON|MAP_SHARED)` aligned to 16 KiB;
  - `map_info` and `map` return that host pointer;
  - `export_blob` returns a handle of type `RUTABAGA_HANDLE_TYPE_MEM_DMABUF`, since libkrun's macOS map path checks
    only the type;
  - `submit_command` calls `ops.submit`, and `create_fence` calls `ops.fence`;
  - resource unref calls `ops.blob_destroyed`, then `munmap`;
  - the 2D, 3D and transfer calls return `Ok` and do nothing.

  `mn_dxmt_fence_done` completes a fence through the stored `RutabagaFenceHandler`.
- **Produces (Rust, `device_builders.rs`):** `GpuDevice::without_display(shm_size: usize) -> GpuDevice`, using
  `devices::display::NoopDisplayBackend` with no displays. ffier exports it in the C header; the header that the
  `gpu` build generates names the function.
- **Produces (C ABI, exported from `libkrun.1.dylib`):**
```c
struct mn_dxmt_ops {
    void *ctx;
    void (*context_create)(void *ctx, uint32_t ctx_id);
    void (*context_destroy)(void *ctx, uint32_t ctx_id);
    void (*blob_created)(void *ctx, uint32_t ctx_id, uint64_t blob_id, void *host_ptr, uint64_t size);
    void (*blob_destroyed)(void *ctx, uint32_t ctx_id, uint64_t blob_id);
    void (*submit)(void *ctx, uint32_t ctx_id, const void *cmd, uint32_t len);
    void (*fence)(void *ctx, uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);
};
void mn_dxmt_set_ops(const struct mn_dxmt_ops *ops);  /* before the VM starts; copied */
void mn_dxmt_fence_done(uint32_t ctx_id, uint32_t ring_idx, uint64_t fence_id);
```

- [ ] **Step 1: Write the failing tests.** Unit tests in `src/rutabaga_dxmt/src/lib.rs`:
  - `capset_is_dxmt_only`: one capset, id 63.
  - `context_rejects_other_capsets`: capset 1 returns `Err`.
  - `blob_size_must_be_16k_multiple`: 16,384 returns `Ok`, 4,096 returns `Err`.
  - `blob_map_returns_aligned_writable_ptr`: `ptr % 16384 == 0`, and a written byte reads back.
  - `fence_done_reaches_handler`: after `mn_dxmt_fence_done(1, 2, 7)` the handler has seen `{ctx_id 1, ring_idx 2,
    fence_id 7}`.

  Next to the existing `checked_blob_map_addr` tests in `virtio_gpu.rs`, add `map_blob_rejects_unaligned_offset`:
  offset 4,096 with a 16 KiB page returns `Err`.
- [ ] **Step 2: Run them, expecting a failure.** Run `cargo test -p rutabaga_gfx -p devices --features devices/gpu`
  in `libkrun-src`. Expected: the build fails (no crate yet).
- [ ] **Step 3: Implement** the crate, the offset check and `without_display`.
- [ ] **Step 4: Run, expecting a pass.** The same `cargo test` passes. Then run
  `cargo build --release -p libkrun --no-default-features --features blk,net,ffi,gpu`, which must link without
  virglrenderer. Then check that `nm -gU target/release/libkrun.dylib | grep -E 'mn_dxmt_(set_ops|fence_done)|krun_gpu_device'`
  lists all three names, and that `include/libkrun.h` declares the `without_display` constructor.
- [ ] **Step 5: Package it.** `git format-patch` 0005 into `eac-vm/libkrun/patches/`, and update `build.sh` and the
  README. Then run `eac-vm/libkrun/build.sh`, which resets to the pin and applies every patch, and expect it to build.
- [ ] **Step 6: Commit.** `eac-vm: libkrun's virtio-gpu with a dxmt context in place of rutabaga`.

### Task 2: the guest sees the device

**Files:**
- Modify:
  - `eac-vm/image/packages.txt` gets `kernel-modules-7.2.10-200.fc44`, and `eac-vm/image/packages.lock` takes the
    `UPDATE_LOCK=1` result: a changed `packages.txt` refetches the cache, so expect other packages to move too, and
    record the size change of `base.erofs`.
  - `eac-vm/host/macneutron-vm.swift` gets the gpu device (shm 8 GiB), calls `mn_executor_start()` before the VM
    starts, runs `krun_vmm_run` on a `Thread`, and the main thread calls `dispatchMain()`. Exit codes and `--timeout`
    behave as before.
  - `eac-vm/host/build.sh` compiles `eac-vm/metal/host/*.c` into `macneutron-vm`.
- Create:
  - `eac-vm/metal/proto.h`: `MN_CAPSET_DXMT`, `MN_PAGE`, `struct mn_ctrl` and `MN_CTRL_PEEK` (Task 3 adds the rest).
  - `eac-vm/metal/host/executor.c` and `executor.h`. A stub for now: it registers the ops, logs contexts and blobs,
    and answers an `mn_ctrl {MN_CTRL_PEEK, ring_blob = blob_id, arg = offset}` submit by checking for the probe's
    magic and writing magic+1. It completes every fence at once.
  - `eac-vm/metal/tests/probe.c`, the guest test.
  - `eac-vm/metal/build-guest.sh`, which builds the guest programs into the share's `bin/` in the builder, like
    `steam-forward/build-guest.sh`.
  - `eac-vm/tests/metal-probe.sh`, the gate.

**Interfaces:**
- **Consumes:** Task 1's `mn_dxmt_set_ops`, `mn_dxmt_fence_done` and the gpu device constructor.
- **Produces:** `void mn_executor_start(void)` (host). The probe's output lines are `renderD128 ok`,
  `context_init ok`, `capset 63 ok`, `blob ok`, `host saw guest bytes ok` and `fence ok`.

- [ ] **Step 1: Write the probe and the gate.** The probe opens the node, checks `GETPARAM`, runs `CONTEXT_INIT`,
  creates and maps one 16 KiB blob, writes `0x4d4e44584d540001` (64-bit, as its first 8 bytes), submits
  `MN_CTRL_PEEK` with `FENCE_FD_OUT`, `poll()`s the fence for at most 1 s, and then expects magic+1. `metal-probe.sh` requires all six
  lines.
- [ ] **Step 2: Run it on today's image, expecting a failure.** Run
  `eac-vm/run-guest.sh eac-vm/tests/metal-probe.sh`. Expected: `FAIL renderD128`.
- [ ] **Step 3: Implement the image and host changes.** If the guest's `virtio-gpu` refuses a device with no scanouts
  (dmesg on the console), give the device one display through the same constructor, and note it in the libkrun
  README.
- [ ] **Step 4: Run, expecting a pass.** `metal-probe.sh` passes, and so do `identity.sh`, `net.sh` and `wine.sh`.
- [ ] **Step 5: Commit.** `eac-vm: the guest gets virtio-gpu with the dxmt context`.

### Task 3: rings, the transport library and the executor

**Files:**
- Modify: `eac-vm/metal/proto.h`, and `eac-vm/metal/host/executor.c` (the real one).
- Create:
  - `eac-vm/metal/guest/transport.c` and `transport.h`;
  - `eac-vm/metal/host/testops.c`, 4a's test ops;
  - `eac-vm/metal/tests/ring-test.c`, a Mac-only test with ASan and UBSan;
  - `eac-vm/metal/tests/transport-test.c`, the guest test;
  - `eac-vm/tests/metal-transport.sh`, the gate;
  - `eac-vm/metal/Makefile`, which builds `ring-test` on the Mac.

**Interfaces (`proto.h`):**
```c
#define MN_CAPSET_DXMT 63
#define MN_PAGE 16384
#define MN_RING_BYTES (1u << 20)    /* record area of a ring blob */
#define MN_REPLY_BYTES (64u << 10)  /* reply area of a ring blob */
struct mn_ring {                    /* at offset 0 of a ring blob */
    _Alignas(64) _Atomic uint64_t head;     /* bytes the guest has published */
    _Alignas(64) _Atomic uint64_t tail;     /* bytes the host has consumed */
    _Alignas(64) _Atomic uint64_t done_seq; /* the last command the host finished */
    _Alignas(64) _Atomic uint32_t parked;   /* the consumer sleeps: send MN_CTRL_WAKE */
    _Alignas(64) uint32_t reply_len; int32_t reply_status;
};  /* followed by MN_RING_BYTES of records, then MN_REPLY_BYTES; the blob is MN_PAGE-rounded */
struct mn_rec { uint32_t op, len; uint64_t seq; };  /* then len bytes, padded to 8; MN_OP_WRAP jumps to the start */
struct mn_ctrl { uint32_t type, reserved; uint64_t ring_blob, arg; };  /* an EXECBUFFER payload */
enum { MN_CTRL_RING_ATTACH = 1, MN_CTRL_RING_DETACH, MN_CTRL_WAKE,
       MN_CTRL_WAIT,    /* its fence completes once done_seq >= arg */
       MN_CTRL_PEEK };  /* Task 2's probe, kept */
enum { MN_OP_WRAP = 0, MN_OP_NOP = 1, MN_OP_BLOB_RELEASE = 2,
       MN_OP_TEST_ECHO = 0x100, MN_OP_TEST_SET, MN_OP_TEST_GET, MN_OP_TEST_FILL, MN_OP_TEST_SUM };
```

**Interfaces (guest, `transport.h`):**
```c
struct mn_blob { uint32_t gem; uint64_t blob_id, size; void *ptr; };
int mn_open(void);                                    /* once per process: 0 or -errno */
int mn_blob_alloc(uint64_t size, struct mn_blob *b);  /* rounded up to MN_PAGE */
int mn_blob_free(struct mn_blob *b);                  /* a synchronous MN_OP_BLOB_RELEASE, then GEM_CLOSE */
int mn_send(uint32_t op, const void *p, uint32_t len);  /* asynchronous, on this thread's ring */
int mn_call(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len);  /* host status */
```

**Interfaces (host, `executor.h`):**
```c
typedef int32_t (*mn_op_fn)(uint32_t ctx_id, void *ring_state, uint32_t op, const void *p, uint32_t len,
                            void *reply, uint32_t cap, uint32_t *reply_len);
void mn_executor_start(mn_op_fn handler);  /* replaces Task 2's stub; registers with mn_dxmt_set_ops */
void *mn_executor_blob(uint32_t ctx_id, uint64_t blob_id, uint64_t offset, uint64_t len);  /* NULL if out of range */
```

**Protocol rules:**
- **Ring ownership.** A thread's ring blob is created and attached on its first call (`MN_CTRL_RING_ATTACH`), and is
  detached by a pthread key destructor.
- **Fence ring indexes.** Indexes 1 to 63 are leased from a pool, one per thread; index 0 is for control.
- **Waking the host.** After publishing `head`, a thread issues a `seq_cst` fence and then reads `parked`. If it's
  set, the thread sends `MN_CTRL_WAKE` on ring index 0.
- **Parking.** A consumer that has had nothing for 2 ms sets `parked` and issues a `seq_cst` fence. It then reads
  `head` again: on work it clears `parked` and goes on; otherwise it sleeps on its condition variable until
  `MN_CTRL_WAKE`.
- **Waiting in `mn_call`.** Spin on `done_seq` for up to 50 µs. After that, a thread holding a fence index sends
  `MN_CTRL_WAIT` with `FENCE_FD_OUT` and `poll()`s the fd. A thread without one sleeps 100 µs between checks.
- **Test ops.**
  - `ECHO` replies with its payload.
  - `SET` stores a u64 per ring, and `GET` replies with it.
  - `FILL {blob_id, offset, len, byte}` writes into the blob.
  - `SUM {blob_id, offset, len}` replies with the u64 sum of the blob's bytes.

- [ ] **Step 1: Write the ring test.** `ring-test.c` runs a host-only producer and consumer over the same ring code,
  with a fake doorbell. Cases:
  - 1M records of 0 to 4,096 bytes, including wraps;
  - checksums in order;
  - park and wake with random gaps of 0 to 3 ms, where every record is consumed.

  Run `make -C eac-vm/metal ring-test`. Expected: FAIL (nothing to link).
- [ ] **Step 2: Write the guest test and the gate.** `transport-test.c` has one subcommand per case, and
  `metal-transport.sh` runs each one, requires its `ok` line, and fails if the console has `mn-executor: error`:
  - `order`: 8 threads × 10,000 iterations of an asynchronous `SET i`, then a synchronous `GET`, which must be `i`.
  - `blob`: the guest writes a pattern and `SUM` matches; `FILL`, then the guest reads it back.
  - `free-in-flight`: 1,000 rounds of an asynchronous `FILL` followed straight away by `mn_blob_free`.
  - `gap`: 100,000 sends with sleeps of 1.5 to 2.5 ms between bursts, all done within 100 ms of the last.
  - `threads 100`: 100 threads × 1,000 `ECHO` calls, all correct.
  - `bench`: prints `async ns/cmd`, `sync spin ns`, `sync fenced us` and `wake us`, each the median of 3 runs.

  Run the gate on Task 2's image. Expected: FAIL (no transport yet).
- [ ] **Step 3: Implement** `proto.h`, `transport.c`, `executor.c` and `testops.c`.
- [ ] **Step 4: Run, expecting a pass.** `ring-test` passes under ASan and UBSan, and `metal-transport.sh` passes in
  the guest.
- [ ] **Step 5: Commit.** `eac-vm: per-thread command rings between the guest and the host executor`.

### Task 4: measured against the research ring; docs

**Files:**
- Modify:
  - `eac-vm/tests/metal-transport.sh`, which gets thresholds;
  - `docs/research/2026-10-15-vm-route/README.md`, a new §7 "Metal transport (milestone 4a)";
  - `docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md` (§4, §5.1, §5.6, §8);
  - memory file `eac-microvm-route.md`.

- [ ] **Step 1: Add the thresholds** to `bench` in the gate:
  - `async ns/cmd` ≤ 40 (the research ring did 19.8);
  - `sync spin ns` ≤ 2,000 (research: 114 ns);
  - `sync fenced us` and `wake us` are printed but not asserted (research doorbell: 37 µs).
- [ ] **Step 2: Run the gates.** `metal-transport.sh`, `m2-gate.sh --cpus 16 --ram-mib 16384` and `identity.sh` all
  pass. Record the numbers.
- [ ] **Step 3: Write the docs.**
  - The README §7 gets the four numbers next to research §3's.
  - The spec revision: rutabaga becomes `rutabaga_dxmt`; winemetal's call count goes from 198 to 153; the guest's
    `virtio-gpu.ko` comes from `kernel-modules`; the VM runs off the main thread.
  - Update the memory file. Run the privacy scan, then commit: `docs: Metal transport measured (milestone 4a)`.

## After 4a: plans 4b and 4c

Each gets its own plan, written once 4a's numbers are in.

- **4b, winemetal forwarding.**
  - **Calls.** A generator for winemetal's 153 calls, after `steam-forward/gen.py`, with hand-written serialisers
    for the three `encodeCommands` linked lists, which are the hot path.
  - **Host.** The host loads MacNeutron's own `aarch64-unix/winemetal.so` with a stand-in `ntdll.so` (the
    `host_wine.c` pattern) and keeps a handle table, because the guest picks handles.
  - **DXMT patches for the guest build.** The six `newBufferWithBytesNoCopy` sites, CPU-visible heap buffers
    (Metal-owned heap memory can't be shared) and `newBuffer` contents all allocate from blobs. `SM50GetCompiledBitcode`
    output is copied back. `setWin32EventAtValue` signals reach `NtSetEvent` through a guest listener thread that
    waits on a fence.
  - **Guest.** DXMT's x86-64 PE build goes into the guest Wine.
  - **Gate.** The headless `dxmt/tests` programs give, in the guest, the same output lines and pixels as our DXMT on
    macOS. `check.sh` itself can't run there, because it goes through the launcher and compares live against
    D3DMetal.
- **4c, presentation.**
  - **Host.** An `NSWindow` and `CAMetalLayer` per HWND replace the `winemac.drv` path in `CreateMetalViewFromHWND`.
    `MetalLayer_*` calls are served on the main queue.
  - **Gate.** `present_loop`, `d3d12_clear` (300 frames) and `d3d12_fullscreen` pass in the guest, and SMITE 2
    renders through the host.
