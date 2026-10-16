# VM route for Easy Anti-Cheat: measured cost (2026-10-15)

Measured on an Apple M5 Pro (18 cores, 48 GB) under macOS 27.0.1. The host is a Virtualization.framework VM
(`harness/vmhost.swift`): Ubuntu 26.04, Linux 7.0, 4 KB pages, 16 vCPUs and 16 GB of RAM. The FEX is our tree, built
in the guest. The harness and raw results are beside this file.

## 1. Hardware TSO works in a VM guest

Apple's TSO bit is `ACTLR_EL1[1]`. A guest kernel module (`harness/tso.c`) sets and clears it on every vCPU.
Virtualization.framework neither traps nor drops the write. The message-passing litmus test (`harness/mp2.c`; forbidden
under TSO) shows the effect:

| ACTLR_EL1.TSO | violations |
|---|---|
| 0 | 1,094,706,632 of 2,803,484,927 |
| 1 | 0 of 360,987,447 |
| 0 again | 1,051,945,159 of 2,680,024,245 |

Ubuntu's kernel has no `PR_SET_MEM_MODEL`, so the module sets the bit for the whole VM. A production guest kernel
would carry Asahi's per-process TSO patch.

## 2. CPU: FEX with and without hardware TSO, against today's stack

### Game-shaped frame loop

`harness/gameframe.cpp`: an 8-worker job system on a lock-free MPMC queue, SIMD entity integration, a parallel spatial
hash with contended atomics, and a render thread fed by a 64-byte command ring. Median of 3, ms per frame (lower is
better).

| Configuration | ms/frame | vs FEX sw-TSO |
|---|---|---|
| macOS native arm64 | 1.98 | |
| MacNeutron, native ARM64 build under our Wine | 2.02 | |
| Guest native arm64 | 2.10 | |
| Guest native arm64, TSO bit on | 2.15 | |
| **Guest FEX, software TSO (today's FEX codegen)** | 3.21 | 1.00 |
| **Guest FEX, hardware TSO** | **2.14** | **1.50x faster** |
| Guest FEX, TSO off (unsafe upper bound) | 2.06 | |
| **MacNeutron today: x64 under Wine + FEX (ARM64EC)** | **4.94** | 0.65x |
| MacNeutron today, same program without C runtime calls | 3.23 | 0.99x |

Two things explain MacNeutron's 4.94 against the guest FEX's 3.21:

- The program calls `floorf` 157 million times. Under MacNeutron, ucrtbase is native ARM64EC, so each call crosses
  from x64 to ARM64EC: about 11 ns, 1.7 ms per frame here. Without those calls, our stack matches the guest FEX
  (3.23 against 3.21).
- In the VM route the whole C runtime is translated x64, so it never crosses.

### Single-threaded and microbenchmark rows

`wine-arm64/tests/x64-bench.cpp`, built for Linux through `harness/posix-windows.h`. Median of 3, seconds.

| Row family | HW TSO against SW TSO | Notes |
|---|---|---|
| Integer, branch, SSE/AVX compute (22 rows) | 0.94-0.97x (3-6% slower) | TSO mode slows the core's memory pipeline a little, on code that needs no ordering |
| `mem_seq_read`, `mem_seq_write` | **1.95x faster** | Software TSO puts an ordered access on every load and store |
| `mt_memcpy_4`, `rep_movsb_64MB` | 0.69x, 0.81x | Bulk copy bandwidth drops in TSO mode |
| `mem_random_chase` | 0.99x | The guest is 10% slower than MacNeutron (0.83 s against 0.75 s): nested paging on TLB misses |
| Atomics, `mt_xadd`, `mt_spsc_ring` | about 1.0x | Already barrier-free under both |

## 3. Graphics: forwarding DXMT's Metal calls out of the VM

### SMITE 2's call rate

DXMT instrumented on its Windows/Metal boundary (`results/callstats.txt`): **2,772 calls and about 5,100 encoded
commands per frame**. These are steady menu figures over 15,000 frames.

| Call | per frame | Forwarding note |
|---|---|---|
| `NSObject_release` | 698 | Asynchronous, batched |
| `MTLSharedEvent_signaledValue` | 234 | A shared-memory read on the host side's mirror; no round trip |
| `MTLCommandBuffer_status` | 214 | A shared-memory read |
| `MTLCommandEncoder_endEncoding` | 182 | Asynchronous |
| `MTLRenderCommandEncoder_encodeCommands` | 138 | Asynchronous; carries the ~5,100 command nodes |
| `MTLCommandBuffer_renderCommandEncoder` | 131 | Handles assigned guest-side, so asynchronous |
| `MTLDevice_newBuffer` | 81 | Same |

### Transport

A custom virtio device in the VM host (`VZCustomVirtioDevice`, new in macOS 27). It has one doorbell queue and a 64 MB
shared memory region over host memory. A host thread consumes a ring the guest fills (`harness/vbench.c`, `vbg.c`,
`host_ring.c`).

| Measurement | Guest to host | Host-only baseline (two threads) |
|---|---|---|
| Ring throughput, 64-byte commands | 19.8 ns/cmd, 50.5 M/s, 3.2 GB/s | 18.2 ns/cmd, 55 M/s |
| Fence round trip (spinning host thread) | 114 ns | 116 ns |
| Doorbell round trip (virtqueue kick plus interrupt) | **37 µs** | |

Shared memory between guest and host costs essentially what it costs between two host threads. Doorbells are about
300 times dearer than shared-memory fences.

### Estimate for SMITE 2

- **Submission cost:** about 8,000 ring entries per frame at about 20 ns is about 0.16 ms per frame on the render
  thread, asynchronous to the GPU. That's about 2% of a 120 fps frame.
- **Synchronous calls:** handle-returning calls made synchronous would add about 0.03 ms (114 ns each). Status and
  event queries cost nothing if served from shared memory.
- **Bandwidth:** about 0.4 MB per frame, about 50 MB/s, against 3.2 GB/s available.
- **Doorbells:** the host consumer must be awake during frames, either spinning or with an adaptive spin-then-park. A
  doorbell per frame would cost 37 µs, and one per call would cost the frame.
- **GPU, shader translation and pipeline compiles:** these stay on the host and are unchanged.

## Verdict

| Workload | VM route against MacNeutron today |
|---|---|
| GPU-bound (SMITE 2 at its cap) | About the same: at most 2-3% for submission, the GPU work unchanged |
| CPU-bound, memory-ordering heavy (job systems, game threads) | **Faster: about 1.5x** from hardware TSO (2.14 against 3.21 ms) |
| CPU-bound, calling the C runtime or Win32 heavily | Often faster: no x64 to ARM64EC crossings, though the callee is translated |
| Bulk copies, pure compute | 3-6% slower; bulk copies up to 30% slower in TSO mode |
| Random memory access | About 10% slower: nested paging |

Costs not measured here:

- the guest's RAM, 8-16 GB;
- disk, about 10 GB for SteamOS;
- the x86-64 Linux Wine build;
- FEX's ptrace fidelity work for EAC itself.

## Side findings that apply to today's stack

- **The 8-vCPU VM ran this frame loop 3.5x slower than 16 vCPUs (7.08 against 2.10 ms).** Ten spinning threads on
  eight vCPUs starve each other, so a VM route needs at least as many vCPUs as the game's busy threads.
- **Calls from x64 code into Wine's ARM64EC DLLs cost about 11 ns each.** A game calling CRT math in hot loops loses
  real time to this. This is a candidate for a FEX fast path, or x64 builds of the hottest ucrtbase functions.
- **ARM64 Windows builds without `-mcpu` use LL/SC atomics.** Our own native ARM64 build ran 3.4 ms with them against
  2.0 ms with LSE (`-mcpu=apple-m5`). Every ARM64 PE we build must keep `PE_MARCH`.

## 4. The microVM (milestones 1-2, 2026-10-15)

The design moved to a libkrun microVM in our own host process (spec
`docs/superpowers/specs/2026-10-15-macneutron-eac-vm-route-design.md`). Same M5 Pro, macOS 27.0.1; guest with 16 vCPUs
and 16 GiB unless noted. Commands are in `eac-vm/tests/`, run through `eac-vm/run-guest.sh`.

**Guest.** Fedora's signed `kernel-core` 7.2.10-200.fc44, unmodified, booted directly by libkrun (patch 0001 reads its
EFI zboot header; the payload is zstd). `m2-gate.sh` reports `[integrity]` lockdown, `sig_enforce` `Y`, 4 KB pages.
Boot to the test runner: 3.56 s.

| Image | Size |
|---|---|
| `vmlinuz` | 16.5 MB |
| `initramfs.img` (dracut) | 41.3 MB |
| `base.erofs` (systemd, udev, kernel modules, FEX; 107 pinned packages) | 216.5 MB |
| `x86-64-rootfs.erofs` (Fedora's FEX RootFS) | 1,643 MB |
| `state.img` | 16 GiB sparse, 6.5 MB used |

**FEX fidelity.** All 19 runs of the PoC's tests give their expected result on our FEX (macneutron + 27 patches),
in the builder and in the microVM. The earlier `ptrace-inject` failure was a mixed setup: the tracer ran under our
FEX, the exec'd payload under the PoC's FEX registered in binfmt. `run-all.sh` now refuses that mix.

**Hardware TSO from the host** (libkrun patch 0003, `tso.sh`):

| | Litmus violations | FEX `mem_seq_read` |
|---|---|---|
| Without EnTSO | 1,363,792,963 of 2.8 G | 0.90 s (software TSO) |
| EnTSO on every vCPU | 0 of 186 M | 0.456 s (`HostTSO`, no barriers) |

**Game-shaped frame loop** (`gameframe.x86_64` under FEX): 2.112, 2.116, 2.141 ms per frame, median 2.116 ms, against
2.14 ms for FEX with hardware TSO in the Virtualization.framework VM and 4.94 ms for MacNeutron today.

**Memory returns to macOS** (libkrun patch 0002, `footprint.sh`, 8 GiB guest): touching 4 GiB and freeing it took the
host's physical footprint from 5,120 MB to 972 MB within 14 s. Before the patch it stayed at 5,222 MB. While
Hypervisor.framework maps a page a vCPU has written, `madvise` frees nothing, whether `MADV_FREE` or
`MADV_FREE_REUSABLE`. Unmapping the reported range from the VM, `MADV_FREE`, and mapping it back frees it in about
30 µs per 4 MB, and pages the guest touches again are charged again. macOS's `footprint` tool leaves guest memory out;
`vmmap -summary`'s physical footprint includes it.
