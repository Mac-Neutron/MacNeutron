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

**libkrun at main** (2026-10-16). libkrun moved from v1.19.6 to upstream `main` (a4d501ef), with our four patches
rebased; main's v2 C API (ffier) replaces the old one, and the host builds a device manager, payload and VMM. Every
gate above passes again on it: identity, `m2-gate.sh` (litmus 0, `HostTSO`, gameframe, fex-tests), the 4 GiB
footprint drop (5,222 → 1,126 MB), and Steam forwarding (72 µs median round trip).

## 5. Our Wine and Steam in the guest (milestones 3a-3b, 2026-10-16)

**Our Wine for x86-64 Linux** (`eac-vm/wine/`): upstream master plus our macOS series plus Proton 11's EAC changes,
cross-built in the Fedora builder against a pinned sysroot (clang + lld on both sides). `notes.md` sorts the macOS
patches: most compile out on x86-64 Linux, winemac.drv and msync aren't built, libxess builds but stays inert without
DXMT. Installed: 760 MB; `wine.erofs` (lz4hc): 333 MB, read-only at `/opt/mn-wine` and shared by every game. FEX runs
it against Fedora's x86-64 RootFS, which has every library Wine links or loads except libodbc.

**Prefix** (`wine.sh`, under Xvfb): a fresh `wineboot -i` on `/home` takes 5 s with MacNeutron's `mscoree,mshtml=`
override. Without it the Wine Mono prompt waits on the invisible display and wineboot gives up after 300 s.

**Steam through the forwarder** (`probe.sh`, our x86-64 `steam.exe` and lsteamclient as `steamclient64.dll`, the probe
with a game's `steam_api64.dll`): with `--steam-appid 480`, `init: ok`, `steamid ok`, a 234-byte auth ticket, the
probe's SEH fault caught, and `steamclient_init_registry`'s `language` value replayed into the prefix's HKCU. Without
it, `SteamAPI_Init` fails cleanly ("No SteamClient021") and Wine keeps running. Two fixes came out of it:
- Strings Steam returns (`struct u_buffer _ret`, 262 calls) were host pointers, which lsteamclient's 64-bit PE side
  reads directly. They now come back as guest copies, one per distinct content, kept for the process's life.
- lsteamclient's PE side asserts on any failed call, so a missing host stopped the game at an invisible assertion
  dialog. With no host, the forwarder now answers as lsteamclient does when Steam isn't running: init and the registry
  setup succeed, and `CreateInterface` finds no interface.

The ticket's `GetAuthSessionTicketResponse_t` didn't arrive in 10 s. The same happens against `mn-steam-host` on the
Mac with no VM, and `ISteamUser::BLoggedOn` reported 0: Mac Steam wasn't connected to Steam's servers then. A run
with Steam online still has to show it.

## 6. Guest kernel and EAC Linux v2 (2026-10-16)

**Kernel.** The guest stays on Fedora's signed kernel (7.2.10-200.fc44). What was tried first:
- **SteamOS.** SteamOS 3.9 (3.9.2 on the Deck's beta and preview channels) ships linux-neptune-72 7.2.7-valve1, an
  x86-64 bzImage; Hypervisor.framework runs arm64 guests only. Valve's only arm64 kernel is the Steam Frame's (SteamOS
  0.4.5, 6.18.0 `linux-618-deckard`, from the signed RAUC bundle): it has no virtio-mmio, virtio-blk, virtio-console or
  virtio-net, no module signing and no lockdown LSM, so libkrun can't boot it.
- **Our own build.** Linux 7.2.11 with everything built in and no modules (Fedora's config through `localyesconfig`
  with the guest's lsmod) booted, passed identity and `m2-gate.sh` (gameframe 2.098 ms), and shrank base.erofs from
  345 to 220 MB. It was dropped because EAC Linux v2 accepts only kernels signed with a well-known key (below).
- **Epic's well-known keys** are distribution CAs (Canonical, Fedora, Universal Blue, Debian, openSUSE, SUSE, Rocky,
  AlmaLinux) and the Microsoft UEFI CA that signs shim. Universal Blue's gaming kernel (OGC, Bazzite's) is built for
  x86-64 only; on aarch64 Universal Blue ships Fedora's kernel (its `main` flavor: Fedora's binary from Koji, Fedora's
  signature kept and Universal Blue's added). Fedora's kernel is therefore both the best arm64 choice on the list and
  the one Universal Blue would give us.

**EAC Linux v2 against the microVM.** Epic's eight requirements (announced 2026-10-14). The goal is to meet them as a VM
that says it is one, never to imitate hardware.

| # | Requirement | The guest today | What meeting it takes |
|---|---|---|---|
| 1 | Kernel and modules signed with a well-known key | Fedora's kernel (Authenticode signer `kernel-signer`, issued by Fedora Secure Boot CA 20200709) and in-tree modules, `module.sig_enforce=1`, no out-of-tree modules | Done for the binaries; it counts only once Secure Boot verifies the chain (3) |
| 2 | TPM 2.0 | Every run since 2026-10-17 (§13): libkrun patch 0007, a TIS TPM run by libtpms, its state per game, sealed under a key in the login keychain; Linux registers `/dev/tpm0`. No endorsement-key certificate yet | A TPM over TIS MMIO in our libkrun fork (CRB binds only through ACPI, which this guest boots without; Fedora's kernel has `TCG_TIS=y`), backed by a software TPM on the host (libtpms) with per-VM state. Its endorsement key is certified by MacNeutron as a virtual TPM, never as a vendor's chip |
| 3 | Secure Boot, booting only from well-known keys | Direct kernel boot, no firmware | EDK2 (ArmVirt) with Secure Boot on, `db` holding only the Microsoft UEFI CAs and Fedora's CA, current `dbx`; Fedora's Microsoft-signed shim, then GRUB and the kernel, both Fedora-signed. libkrun main has no firmware loader, so this is a libkrun patch plus our EDK2 build (spec milestone 6) |
| 4 | Measured boot | Nothing measured | Comes with 2 and 3: EDK2's TCG2 measures shim, shim measures GRUB, GRUB measures the kernel, initramfs and command line |
| 5 | IOMMU on, passthrough off (non-handheld devices) | No IOMMU: libkrun's virtio devices write straight into guest memory | A virtio-iommu device in our libkrun with every virtio-mmio device behind it (Fedora's kernel has `VIRTIO_IOMMU=y`), and `iommu.passthrough=0` on the command line. Each DMA buffer then needs a map request: measure the frame loop, disk and network |
| 6 | Lockdown integrity, set explicitly at boot | `lockdown=integrity` on the command line; the identity gate checks `[integrity]` | Done; with 3 the command line moves into GRUB's configuration, where it is measured |
| 7 | kexec disabled, set explicitly at boot | `sysctl.kernel.kexec_load_disabled=1` on the command line since 2026-10-16; the gate checks it (and fails without it) | Done |
| 8 | BPF LSM loaded and active | `bpf` is in Fedora's LSM order; the gate checks it | Done |

EAC v2's client also runs a systemd service during play. The guest has systemd but no D-Bus (systemd-logind fails at
every boot for that reason); D-Bus comes in when that service needs it.

Questions for Epic before building 2-5, since a VM can comply only if Epic accepts one:
- Can a protected game run in a VM at all? Epic describes the TPM as hardware "enabled in the BIOS".
- Does a virtual TPM count when its endorsement key is certified by the VM's maker rather than a TPM vendor?
- Is a virtio-iommu with passthrough off what requirement 5 asks of a VM, which has no DMA-capable hardware?
- Is aarch64 in scope? Fedora signs arm64 kernels with the same CA.

Games keep the choice between EAC Linux v1 and v2, so SMITE 2 (v1, still 301 → client never mapped here) is not
affected yet.

**SMITE 2's EAC setup against Proton's** (2026-10-16, after 301 → 60101 again):
- The anti-cheat is the right one. The Proton EasyAntiCheat Runtime in the share is byte-identical to the builder's
  Steam install (build 10437216, updated 2026-10-15). The bootstrapper (1.9.0) runs in Wine mode (`linux64`), and
  fetches SMITE 2's Linux module for its deployment from Epic's CDN into the prefix's AppData, not into the read-only
  game folder.
- The prefix wasn't Proton's. `eac.sh` ran `wineboot -u` with `PROTON_EAC_RUNTIME` set, which copied the runtime's
  `easyanticheat_x64.dll` and `easyanticheat.dll` into system32. Proton fills prefixes from its default prefix, which
  has neither. `eac.sh` now removes them and runs wineboot without the variable. Same result.
- The launch wasn't Steam's. SMITE 2's Steam launch entries run `Windows/Hemingway.exe -homedir=SMITE2Alpha -d3d12
  -game -oss=SteamV2 -RallyHereURL=…`, and the public ones add `-NOEAC`: Steam doesn't take SMITE 2 through EAC's
  bootstrapper. Launched that way in the guest, the game starts and logs up to D3D12 adapter selection (no adapter
  until milestone 4). Through the bootstrapper, with the same arguments minus `-NOEAC` (now in `eac.sh`), the game
  executable exits before writing a log, and the bootstrapper ends with 60101.
- Where the EAC screen comes from: screenshots of the guest's display (Xvfb `-fbdir`) show the SMITE 2 splash with
  Easy Anti-Cheat's logo and "INITIALIZING..." from `start_protected_game.exe`; then nothing, no game window and no
  dialog. `Hemingway.exe` without `-NOEAC` stops at an "Invalid handle." dialog and never starts the game. The
  builder's Linux Steam (Proton Experimental, 2026-10-15) launched `Hemingway.exe ... -NOEAC` too, and that Proton
  prefix has no EAC files or bootstrapper log. Its 2-step install script is the Steamworks redistributables
  (depots 228989 and 228990), not EAC.

## 7. Metal transport (milestone 4a, 2026-10-16)

**What exists.**
- **Device.** A virtio-gpu context of capset 24 (`dxmt`) in our libkrun. Patch 0005 replaces rutabaga_gfx with a
  stand-in that has only this context and no virglrenderer. Patch 0006 starts libkrun's vmm worker on macOS.
- **Guest driver.** Fedora's signed `virtio-gpu.ko`, from `kernel-modules-core`.
- **Executor.** In `macneutron-vm` (`eac-vm/metal/host`).
- **Guest library.** `eac-vm/metal/guest`, x86-64 under FEX.
- **Rings.** One ring blob per guest thread: 1 MiB of records and a 64 KiB reply area. The host consumer spins 2 ms
  after its last record and then parks. The guest sends one doorbell per park. Synchronous calls spin 50 µs, then wait
  on a fence.

Gates: `ring-test` (on the Mac, ASan and UBSan), `metal-probe.sh` and `metal-transport.sh` (in the guest).

| Measurement | 4a: x86-64 guest code under FEX | Research §3: native guest module |
|---|---|---|
| Asynchronous record | **14.1 ns** | 19.8 ns per 64-byte command |
| Synchronous call, consumer spinning | 310 ns | 114 ns fence round trip |
| Synchronous call answered through a fence | 27.5 µs | |
| Call to a parked consumer (doorbell, wake, reply) | 35.6 µs | 37 µs doorbell |

At SMITE 2's 2,772 calls per frame, submission costs about 0.04 ms of the render thread.

Findings:
- **Capset id.** The dxmt capset id must be under 32. The guest driver builds its capset mask with `1 << id` on an
  int, so capset 63 was never offered.
- **No virgl capsets.** The device must not count virgl's capsets: the driver asks for each one and waits 5 s per
  missing reply.
- **The vmm worker.** libkrun main's v2 builder never started the vmm worker on macOS, so the first blob map panicked
  the gpu worker.
- **Cache-line traffic.** Per-record cache-line traffic, not the ring code, cost the first 86 ns per record. Each
  side now caches the other's position, and the consumer publishes its progress per batch.
- **Clock reads.** `clock_gettime` costs about 270 ns under FEX, so spin loops read the clock every 1,024 checks.

**Hardening after the 4a review** (2026-10-16), each with a test that failed first:
- **Freed blobs.** A blob the guest frees now belongs to the executor, which unmaps it once nothing uses it.
  `mn_executor_blob_hold`/`_release` keep one alive past the free; that is what 4b's `newBufferWithBytesNoCopy`
  buffers need (`executor-test`).
- **Ring limits.** A context gets at most 256 rings. An attach is fenced and stamped in the ring header, so a refusal
  reaches the guest and the thread's fence index comes back (`ring-cap`).
- **Lost doorbells.** A WAIT wakes its ring's consumer, so a lost doorbell can't leave a waiting thread stuck
  (`lost-wake`).
- **Thread exit.** An exiting thread drains its own ring before detaching, instead of draining it on the gpu worker
  (`exit-drain`: the slowest call from another thread dropped from 2.0 s to under 200 ms).
- **Test ops.** They exist only with the host's `--metal-test-ops` flag (`no-test-ops`).
- **Smaller fixes.** Layout asserts on every struct shared by the x86-64 guest and the arm64 host, a serialised
  `mn_open`, checked allocations, and a log line for an out-of-window blob offset.

`eac-vm/metal/gate.sh` runs all the gates from the Mac.

**FEX and the doorbell handshake.** The guest's `seq_cst` fence (x86 `mfence`) becomes `dmb sy` in FEX
(`OpcodeDispatcher.cpp` MFENCE → `_Fence(LoadStore)`, `JIT/MiscOps.cpp`). No TSO setting touches fences, our
HostTSO patch included; it only turns off the atomic load and store emulation. Locked read-modify-writes become `*AL`
LSE atomics. Empirically, `gap 5000` (5,000 park and wake races) loses no wake.

**Handles for milestone 4b, measured** (`harness/handles-bench.c`, `results/handles-bench.txt`, on the host). The
question is whether the guest should pass the host's object pointers through, or pick its own handles and have the
host translate them.

- **What translation costs.** Guest handles are a generation and an index into a dense host table, so translating
  one is a bounds check, a generation check and a load; a stale or forged handle fails. A SMITE 2 frame carries about
  9,500 handles: one per call over about 2,770 calls, and about 1.3 per command node over about 5,150 nodes. Looking
  them all up costs **3.8 µs per frame**, about 0.4 ns per handle, from a 30,000-object table held in cache. With
  700 creations and releases per frame added, it costs 2 to 4 µs more than passing pointers through.
- **What passing pointers through costs.** Every call that returns a new handle must wait for the host: about 250 a
  frame, mostly render encoders and autorelease pools. At the 310 ns a spinning round trip takes (§7), that is about
  **78 µs per frame** on the game's render thread. Creations that return GPU addresses (buffers, textures, samplers)
  and those that can fail with an error (pipeline states) wait in both designs.

## 8. Winemetal forwarding (milestone 4b-1, 2026-10-16)

**What exists.**
- **Guest library.** The guest `winemetal.so` (`eac-vm/metal/wm`, x86-64 under FEX) exports winemetal's 153-slot
  Unix-call table. Each slot is packed by one walk over a per-slot descriptor (`calls.c`, from the call catalog) and
  sent on the thread's ring. 74 slots wait for the host, 62 are queued, and the guest answers 17 itself (presentation
  is 4c's, Mach ports are host-only).
- **Host.** `macneutron-vm` loads MacNeutron's own `aarch64-unix/winemetal.so`, unchanged, beside stand-ins for its
  `ntdll.so` and `winemac.so`.
- **Handles.** The guest picks every handle, (generation << 32) | index. The host table binds it with a kind and
  translates it without a lock on the hot path. A handle used on one ring before its creation has run on another
  waits for it. A last release waits, in two phases, until the context's other rings have finished what they had
  published, so queued uses still find the object and in-flight lookups never see it released.
- **Memory.** CPU-visible buffers and heaps live in blob regions the guest sub-allocates (whole 16 KiB pages). The
  host maps them in place (`newBufferWithBytesNoCopy`); a holder on each Metal object over a region tells the guest
  when Metal deallocates it, and only then is the region reused. Shared heaps are emulated over a region. Bulk data
  goes through a per-thread scratch blob.
- **Command lists** go flat (at most 65,536 nodes of known types) and are rebuilt on the host.
- **Shared events.** The host runs each listener on a thread of its own. NtSetEvent posts to the listener's notify
  queue, and the guest's slot-109 stub sets the Win32 events, sleeping on an `MN_CTRL_NOTIFY_WAIT` fence between them.

**Gates.**
- On the Mac: `handles-test`, `marshal-test` (with fakes) and `memory-test` (the guest's and the host's code in one
  process against real Metal).
- In the guest: `metal-wm.sh` runs the 4b-1 sequence (device, buffers, a blit, a compute dispatch from MSL source, a
  shared event that sets a Win32 event) and must print `host-test`'s 14 lines from the Mac, then clean stats
  (`failures=0 live=1 deferred=0`). It also runs the allocator's cases, the refused slots and a fork. A boot with
  `--metal-test-ops` runs the cross-ring case, holding one thread's ring in a 200 ms host delay.

**Findings** (each with a test that failed first):
- **Placed buffers.** DXMT leaves `info.memory` uninitialised for buffers placed in heaps (slot 141): only an output
  there. Reading it as guest memory would have refused every placed buffer.
- **Autoreleased holders.** An atomic associated-object getter autoreleases what it returns. That kept a buffer's
  holder, and its region, alive until some pool drained: never, on a ring's consumer thread.
- **Listener destroy.** Winemetal's listener destroy does nothing until slot 109 has published the listener's run
  loop, and the thread running it would never return. The host waits for the run loop first.
- **Queued calls can't race.** The host drains a ring faster than a guest thread fills it, so queued calls alone
  never hold a ring back. The cross-ring case needs a host-side delay to show that the ordering rules work, and mutating
  either rule fails it.

**Left for 4b-2** (its own plan, after this one). DXMT's x86-64 PE build in the guest's Wine, `WMTMemoryAlloc` for
the memory DXMT hands to Metal itself (`dxmt_buffer.cpp:44-46`, `d3d12_command_allocator.cpp:63`,
`dxmt_context.cpp:60`, `dxmt_ring_bump_allocator.hpp:119`), and the 17 headless tests as the gate.

## 9. DXMT in the guest (milestone 4b-2, 2026-10-16)

**What exists.**
- **DXMT build.** DXMT patch 0071 adds `-Dguest_vm=true`, off by default. With it on:
  - DXMT is built for x86-64, with no Unix side of its own.
  - DXMT's four allocations that Metal makes buffers over use two new winemetal calls, `WMTMemoryAlloc` and
    `WMTMemoryFree` (slots 153 and 154). The four: placed buffers, a command allocator's 4 MiB GPU heap, the dummy
    constant buffer and staging blocks.
  - Texture and visibility-result buffers do the same on i386 only, so they are left unchanged.
- **The product build compiles the same source:**
  - each of its 171 translation units preprocesses identically with and without the patch (`eac-vm/dxmt/inert.sh`);
  - its compile commands, meson targets and list of installed files are identical too.
- **But its translator key changes.** The key (`wine-arm64/lib.sh`) hashes `meson.build`, `meson.options` and
  `src/airconv`, and a new meson option has to touch `meson.options`. So the release that ships 0071 rebuilds every
  user's DXMT shader cache once, as any release that changes those files does. `inert.sh` reports both keys.
- **Build script.** `eac-vm/dxmt/build.sh` builds the guest DXMT on the Mac:
  - with MacNeutron's llvm-mingw, against the guest Wine's import libraries;
  - with no CPU flags, so the x86-64 baseline that FEX runs;
  - with the product's translator key, since the host's translator compiles the guest's shaders.
- **Guest library.** The guest `winemetal.so` answers slots 153 and 154 from its blob allocator, so the host maps that
  memory too. A freed allocation stays reserved until every Metal buffer over it is gone. The host refuses both slots
  if they ever reach it.
- **Image.** `wine.erofs` replaces Wine's own d3d10core, d3d11, d3d12 and dxgi with our DXMT's (`eac-vm/dxmt/stage.sh`,
  `image/build.sh`). It also has `winemetal.dll` as a builtin and the guest `winemetal.so`. D3D in the guest now goes
  through the Metal transport to the host's GPU, no longer through wined3d and vkd3d on llvmpipe.
- **Software rendering.** Presenting stays refused until 4c, so SMITE 2 and 3c's `eac.sh` need an image built with
  `DXMT_GUEST=none`, which keeps Wine's own D3D. `eac.sh` hasn't been run on the DXMT image: it needs the Mac's
  Steam.

**Gate** (`eac-vm/dxmt/gate.sh`).
- **Programs.** 20 of DXMT's headless test programs: `d3d11_vsia`, and d3d12 `api`, `bounds`, `cache`, `compress`,
  `copy`, `depth`, `dxil`, `dxil_exec`, `hazards`, `indirect`, `layered`, `lod`, `null`, `query`, `timestamp`,
  `triangle`, `upscale`, `volume` and `vsread`. Each runs with the arguments of `dxmt/check.sh`'s first run of it.
- **Left out:**
  - the four that present (`clear`, `fullscreen`, `ffx_swapchain`, `present_loop`), which wait for 4c;
  - `xess`, which needs Intel's DLL;
  - `dlss`, part of the cancelled NVIDIA route.
- **Runs.** Each program runs twice on macOS through MacNeutron's launcher, in a scratch tool folder with check.sh's
  environment, and once in the guest under our Wine.
- **Comparison rule** (`compare.py`), fixed before the first run:
  - only lines starting with the program's own output prefixes count;
  - lines where the two macOS runs agree are required, byte for byte;
  - the guest must give as many lines as macOS;
  - a program with no required lines fails, and so does a timeout or an error line from the VM host.
- **Host errors.** The host now logs, when a context ends, how many of its queued calls failed.

**Result: all 20 programs match macOS.**
- 283 of their 288 lines are required. The 5 masked ones are timings.
- No slot was refused, and the host logged no errors.
- **Run times.** Most programs take about 1.05 s each in the guest, and `d3d12_upscale` takes 8.3 s. On macOS they
  take 3.8 s and 14.6 s, but a macOS run includes the launcher, so this is not a speed comparison. It only shows that
  nothing in the guest is slow.

**Findings** (each with a test that failed first):
- **Reused memory must come back zeroed.**
  - **Symptom.** 19 of the 20 programs matched on the first run. `d3d12_hazards`' `zeroed` case read 33554432 non-zero
    bytes in eight new 4 MiB buffers, against 0 on macOS.
  - **Cause.** A CPU-visible buffer's memory comes from the guest's blob allocator, which handed out a freed region
    with its old bytes. A new Metal buffer's memory is zeroed, and DXMT relies on that for D3D12's committed
    resources, which start zeroed.
  - **Fix and tests.** Freed pages are marked dirty and zeroed when handed out again. The allocator's cases now check
    this.
- **Six places, not four.** DXMT has six places where it gives Metal memory of its own; two of them do so on i386
  only (above).

## 10. Presentation (milestone 4c, 2026-10-16)

**What exists.**
- **The window.** The VM host's stand-in for winemac.so (`eac-vm/metal/wm/host/winemac.m`) is the host's window code:
  - MacNeutron's winemetal.so finds it through `macdrv_functions`, as on macOS;
  - every guest HWND maps to one window, "MacNeutron VM" (`MN_VM_WINDOW=WxH` points, 1280x720 by default), made on the
    main thread at the first present;
  - a metal view is a `CAMetalLayer`-backed view over the content view, the newest on top.
  - `macneutron-vm`'s main thread runs AppKit instead of `dispatchMain`: the window and Metal's main-thread calls share
    its run loop.
- **The 11 presentation slots are forwarded** (47, 48, 66, 67, 70-73, 93, 97, 101). The guest refuses 6 slots now: the
  Mach-port ones (120-124) and the empty 83.
  - A view is a handle kind of its own whose release is winemetal's slot 73, so a context's end takes its views off the
    window.
  - The drawable's texture is autoreleased with the frame's pool: DXMT asks for it every frame and never releases it.
  - getProps' reply carries the guest's own device value, never the host's pointer.
  - `nextDrawable` waits for its answer: nil is one.
- **Frames.** They go straight to the host's `CAMetalLayer`, as the spec says (§5.6). The host counts the drawables each
  context presents and logs the count when the context ends.
- **The probe** (`MN_PRESENT_PROBE=1`, the gate's): every 60th present first blits the drawable into a Shared buffer in
  the same command buffer, and the host logs winshot's measures of it: green (60-95), white (all above 200), colours at
  4 bits a channel. It reads 8-bit BGRA and RGBA and 10-bit RGB10A2 and BGR10A2 (SMITE 2's). This session couldn't
  capture the screen at first, so the host checks its own frames; winshot is a row of the gate when it can.
- **Input** (milestone 5's, done here to play): the window's content view posts its keys, buttons, wheel and pointer to
  a virtio-input device (libkrun built with `input`; `eac-vm/host/input.c`).
  - The pointer is absolute, the Mac cursor being the game's, until Control+Option captures it for mouse look (relative
    deltas, the cursor hidden).
  - In the guest, `mn-input` (aarch64, libxcb's XTEST: base.erofs has no libXtst) replays the device into Xvfb, which
    reads no input devices of its own. A udev rule lets the user `mn` read that one device.
  - `eac-vm/tests/input.sh` checks it end to end: the host's `MN_INPUT_SELFTEST` sequence arrives in an X client as
    absolute motion, a key, a click, a relative move and a wheel notch.
  - `eac-vm/play-smite2.sh` plays SMITE 2 until the game exits.

**Gate** (`eac-vm/dxmt/present-gate.sh`), all rows passed:

| Program | macOS frame | Guest frame | Host counted | Probes |
|---|---|---|---|---|
| `d3d12_clear` (600 frames) | 8.00 ms | 8.34 ms | 600 | 10, each green 100 |
| `present_loop` (D3D11, 600 frames) | 7.90 ms | 8.30 ms | 600 | median green 74, white 25 |
| SMITE 2 (180 s, Steam's launch entry) | - | - | 1555 | 116 colours after 60 s |

- `d3d12_clear`'s ResizeBuffers lines are macOS's, and the host logged no error of its own in any run.
- winshot read the window ("MacNeutron VM", green 95) once Screen Recording was allowed.
- SMITE 2 starts through the Mac's Steam (forwarded), reaches its lobby and presents there.

**Findings** (each with a test that failed first, but the shutdown race):
- **The shader cache's writer raced.** SMITE 2's first run crashed the VM host in SQLite (`sqlite3BtreeInsert` under
  `CacheWriter_set`). DXMT guards its writer with a lock, but slot 118 was queued: it returned before the host ran it,
  so two rings inserted into one connection opened without SQLite's own mutex. Slot 118 now waits for the host.
- **The HUD's instance (slot 62) is nil in the VM host.** A queued creation that comes back nil counted as a failed
  call. Nil is an answer, so the slot waits too.
- **A crash left a DLL of 0 bytes.** The VM died seconds after wineboot wrote `winemetal.dll` to the state disk, before
  its data reached it. Wineboot keeps a file it didn't make, so `dxgi.dll`'s import of it failed from then on. The
  guest scripts remove empty DLLs before wineboot.
- **The Steam forwarder couldn't carry a buffer whose count is behind a pointer.** SMITE 2's lobby calls ISteamInventory's
  `GetItemDefinitionProperty`, which failed in the game's lsteamclient, and the game thread stopped at engine frame 9.
  `MN_COUNT_PTR` now carries 12 such calls, the copy back bounded by the request's size.
- **The poweroff raced the host.** A killed SMITE 2 takes seconds to go; the guest powered off before its GPU context
  ended on the host, so no count came. The script waits for the game's processes.
- **XeSS was Intel's own.** With XeSS on, SMITE 2's lobby ran at 7 fps: the GPU was saturated (Device Utilization 100%,
  Renderer 0%) by a few compute passes, about 30-50 ms each a frame, with sane dispatch sizes. Ruled out first: blob
  memory (a Metal buffer over `MAP_SHARED` memory mapped into an HVF VM streams at 274 GB/s, as Metal's own), the
  transport and the dispatch sizes. The cause: the guest loaded the game's Intel XeSS, whose non-Intel (DP4a) path is
  this slow, where MacNeutron's launcher sets `libxess,libxess_dx11=b` for Wine's builtin, which answers with MetalFX
  through DXMT. With XeSS off, the game played at 110-135 fps in a match (the user's report). `smite2-render.sh` now
  sets the launcher's override, and the 4b-2 gate gains check.sh's `d3d12_xess` rows (quality, performance, nodepth,
  api), all the same as macOS; the cycles row is left out, its GPU memory sizes being a few MB apart (blob memory is
  freed when the guest drains its frees). XeSS in SMITE 2 itself is the user's to check.
- **DLSS, as the launcher offers it.** The guest's DXMT builds its nvapi64 and nvngx, Authenticode-signed as the
  product's (the NGX SDK loads only a signed nvngx.dll). `MACNEUTRON_DLSS=1` turns DXMT's NVIDIA extension on;
  check.sh's `d3d12_dlss` rows (vendor 10de with NVAPI, quality, performance, inverted depth, the refusals) give the
  same lines as macOS.
- **Mouse look had no raw input.** Capture sent relative motion, but Wine turns raw input (XInput2's raw motion, what a
  game's mouse look reads) on only when one of its windows gets an X FocusIn. Xvfb has no window manager and its focus
  stays PointerRoot, so it never did, whatever the X server sent (it does send raw motion for XTEST's device). `mn-input`
  now gives the top-level window under the pointer the focus. `eac-vm/tests/input-wine.sh` checks it with a Windows
  program that registers raw mouse input and clips the cursor, as a game in a match: the host's relative move arrives as
  WM_INPUT.

**Open.**
- `mn-steam-host` takes the guest's interface values (`u_iface`) as host pointers unchecked: one run crashed it
  (EXC_BAD_ACCESS at a guest value in ISteamInventory's `GetResultStatus`). A trust boundary for 3a's design.
- A killed game that lingers in the guest holds the poweroff for minutes.
- A swapchain's layer stays retained until its context ends (DXMT never releases it).
- No audio, gamepads or window life yet (milestone 5).

## 11. Every game in the microVM (2026-10-16)

**What exists.** The maintainer asked for every game MacNeutron starts to run in the microVM, and for one copy of the
VM's base for all of them.
- **One base, a state disk per game.**
  - The kernel, initramfs and the base, x86-64 RootFS and Wine EROFS images are one read-only copy every VM attaches.
  - A game's own data (its prefix, `/home`) is its state disk (`macneutron-vm --state`, `run-guest.sh MN_STATE`). It
    is made on first use as an APFS clone of the images' empty `state-template.img` (`image/build.sh`), so a new game
    costs only what it writes: 6.5 MB to start.
  - Its run has its own share too (`run-guest.sh MN_SHARE`; the launcher's `runs/<appid>`): the script, results and
    console, with the tools the guest takes from the share (`bin/`, `valve/`) as APFS clones of the default share's.
    So games run side by side, a VM each; the same game twice is refused (same share, then same state disk). Checked
    with two VMs booted together.
- **The launcher** (`Sources/MacNeutronCore/MicroVM.swift`) sends Steam's `run` and `waitforexitandrun` to the VM when
  one is installed:
  - the VM's folder is `MACNEUTRON_VM_DIR`, else `~/Library/Application Support/MacNeutron/vm`; `MACNEUTRON_VM=0`
    keeps a game on macOS;
  - the launch becomes the guest's script: VERB, APPID, GAME_DIR, EXE, the arguments quoted, and the launch options the
    guest reads (`MACNEUTRON_DLSS`, `MACNEUTRON_XESS`, `DXMT_ENABLE_NVEXT`, `MACNEUTRON_LOG`), ahead of
    `eac-vm/guest/run-game.sh`;
  - the VM folder's `run-guest.sh` boots it with the game's state disk, its Steam library as `/mnt/games` and Steam
    forwarded for its app ID, and the game's status goes back to Steam.
- **`run-game.sh`** is Proton's waitforexitandrun in the guest: the prefix `/home/mn/pfx`, `steam.exe` with Valve's
  DLL, the game, then `wineserver -w`.
- **A game's first run** makes its prefix in the guest, where nothing shows: the launcher shows a toast
  (`macneutron toast`, Sources/macneutron/Toast.swift) until `run-game.sh` writes `traces/ready` in the run's share.
  On macOS the same toast covers a prefix being prepared, and Wine's own wait window is gone (Wine patch 0039).
- **What stays on macOS:** path queries (`getcompatpath`, `getnativepath`), a game outside a Steam library, and every
  game on a Mac without the VM.
- **Checked end to end.** The installed launcher, called as Steam calls it, ran SMITE 2 in the VM to its lobby: 4,400
  frames presented, `backend=vm` in `launcher.log`.

**Open** (milestone 5):
- Steam's Stop ends only the launcher; the game is quit from inside it.
- A fixed 16 GiB of RAM.
- 32-bit games (the guest's Wine is 64-bit only).
- Games that write into their install folder: `/mnt/games` is read-only.
- The VM folder isn't part of MacNeutron's install yet: the development setup links it from `build/eac-vm-work`.

## 12. Sound (2026-10-16)

A game's sound plays on the Mac. The route is an ALSA plugin over vsock, not the spec's virtio-snd (§5.7):
- The image's kernel modules (`kernel-modules-core`) have no `virtio_snd.ko`; it is in `kernel-modules`, a new
  download of about 30 MB.
- Wine runs x86-64 under FEX, so a sound card would put ALSA's ioctls through FEX's translation. A plugin sends plain
  socket writes.
- libkrun has no sound device; the plugin needs only the vsock device the VM already has.

The pieces:
- **`eac-vm/guest/alsa-mn.c`**, ALSA's `mn` PCM, is the image's default device (`/etc/asound.conf`).
  - It is x86-64, built against the FEX RootFS's libasound (`eac-vm/guest/build-audio.sh`), and sits in the share's
    `bin/` like `mn-input`.
  - It accepts two channels, S16 or FLOAT, at any rate. A wider device would let ALSA's plug drop channels; games mix
    down to two themselves.
  - Its hardware pointer is the frames the host has played, so the game is paced by the Mac's audio clock.
  - Without the host it plays into nothing at the stream's rate, as the null device did.
- **`eac-vm/host/audio.c`** in `macneutron-vm` serves vsock port 5102 (`eac-vm/guest/mn-audio.h`).
  - Each stream gets its own CoreAudio output (the default device, which converts the rate).
  - A ring of a second, or 256 KiB at least, sits between the connection and the output.
  - The connection's thread never stops reading. libkrun's vsock proxy sends with blocking calls from the one thread
    that also serves every other vsock connection (Steam's included) and the console, and an unread UNIX socket here
    holds only about 8 KiB.
  - So each stream's output has its own thread. It makes the CoreAudio unit, and until there is one it takes frames at
    the stream's rate into nothing.
  - CoreAudio is asked off the streams' threads until it answers: a stuck audio driver can stall every client's
    calls. A stream that began without it moves to the Mac's output once it does.
  - STOP leaves a mark that the output drops up to; it never waits.
  - `MN_AUDIO_OUTPUT=none` keeps the Mac quiet in tests; `=slow` makes each output take 3 s.

**Checked:**
- `eac-vm/tests/audio-host.sh` runs the player against a stand-in guest: half a second played in 0.505 s, STOP in
  8 ms, PAUSE kept. It fails against an unpaced output ("0.008 s").
- The same test with an output that takes 3 s to make took 256 KiB in 1 ms. Before the fix (review finding), it took
  3.011 s, because the connection stopped reading.
- `eac-vm/tests/audio.sh` runs in the VM:

  | Row | Played | Host count |
  |---|---|---|
  | ALSA, blocking writes under FEX | 2.0 s in 2.003 s | 96,000 frames |
  | Wine's waveOut | 2.0 s in 1.997 s | 95,936 frames, and 64 dropped at Wine's stop |

  Both rows failed while the plugin didn't load: Wine's `waveOutOpen` fails, which SMITE 2 would crash on.

**Open:**
- On the development Mac, CoreAudio stopped answering every client (even `afplay`) during this work, so the real
  output path is only proven up to "no output". It needs a run with a working audio server.
- Microphone input: the plugin refuses capture.
- libkrun logs `unexpected dgram pkt: 3` once a connection; the streams are unaffected.

## 13. TPM 2.0 (2026-10-16, engine 2026-10-17)

EAC Linux v2's second requirement (§6). Every run has one: the device in libkrun, libtpms behind it, its state per game.

- **libkrun patch 0007** adds `TpmDevice` (`krun_tpm_device_new`, `include/libkrun_tpm.h`).
  - It is a TPM 2.0 behind the TCG PC Client TIS (FIFO) interface: five 4 KiB localities of MMIO.
  - Its device-tree node is `tcg,tpm-tis-mmio`, which Fedora's built-in `tpm_tis` binds to.
  - It offers no interrupt, so the driver polls.
  - The host's backend, a C vtable, runs each command on a thread of its own. A slow command (an RSA key generation)
    never stalls the vCPU that sent it; a failed one answers `TPM_RC_FAILURE`.
  - CRB, which §6 first planned, binds only through ACPI, and this guest boots from a device tree.
  - Patch 0008: a command the guest aborts finishes on its thread, and the device reports ready only once it has, so
    one command thread runs at a time however often the guest aborts.
- **`macneutron-vm --tpm <dir>`** attaches it, with `eac-vm/host/tpm.c` as the engine and its state in `<dir>`.

**Checked:**
- The device's unit tests (`cargo test -p krun-devices --lib tpm_tis`): locality grant and hand-over, a command to the
  backend and back, Expect until the whole command is in, retry, a failed backend, and an aborted command holding the
  device until it ends (which fails without patch 0008). Three of them fail when Expect is never set.
- Before the engine, `eac-vm/tests/tpm-host.sh` ran the guest kernel's own driver against it. The guest's device tree had
  `tpm@a00d000: tcg,tpm-tis-mmio`, and the host saw the probe's `TPM2_GetCapability` and then both self-tests. The
  driver sends a command only after reading the last response, so the TIS exchange works both ways.

**The engine** (2026-10-17):
- **libtpms 0.10.2** (BSD-3-Clause, IBM's TPM 2.0 reference code; `eac-vm/libtpms/PIN`), built in-process: TPM 2.0 only,
  its `default-v1` profile, statically on Homebrew's openssl@3 `libcrypto.a`. `eac-vm/libtpms/build.sh` runs clang over
  the sources its `src/Makefile.am` lists, with the defines its configure picks on OpenSSL 3, since automake and
  libtool aren't installed here. Its users rebuild it when `eac-vm/libtpms/PIN` no longer matches the copy beside
  `libtpms.a`.
- **Its state, the way Parallels Desktop keeps its virtual TPM's** ([kb.parallels.com/en/122702](https://kb.parallels.com/en/122702)):
  - libtpms's files (`permall`, its NV) are encrypted in the TPM's folder. They use AES-256-GCM with each file's name
    authenticated (Parallels uses AES-128).
  - The key is a generic password in the login keychain, never in the folder (Parallels: the System keychain). Its
    account is the folder's random id (`<dir>/id`), so the key follows the TPM, not the game.
  - A folder whose key isn't in this Mac's keychain (copied from another Mac), or whose files don't decrypt, moves to
    `<dir>/backup-<id>/`, and the TPM starts afresh with a new identity: Parallels 18's way.
  - That is decided once, before libtpms starts. Anything else stops the VM (exit 7) rather than lose the state: a
    keychain that is locked or says no, or a file that can't be read just now. The VM's timeout runs from before
    the keychain is asked, so a keychain dialog nobody answers can't hold a run forever.
  - Each file is written beside itself and renamed over, with `F_BARRIERFSYNC` first: on macOS a plain `fsync`
    doesn't keep the data ahead of the rename.
  - A backup never replaces an earlier one: a second reset under the same id goes to `backup-<id>-2/`.
  - The folder is locked (`<dir>/.lock`) while its TPM runs, so a second VM on it is refused.
  - If the TPM refuses to start, the launcher tells the player in a notification; the reason is in the game's log.
  - The guest's command is copied before libtpms takes it, since libtpms decrypts parameters in place.
  - Skipped: Parallels' key export for moving a VM by hand, and its iCloud Keychain sync (that needs the
    data-protection keychain, so an app bundle with a provisioning profile; `macneutron-vm` is a bare tool). A game's
    TPM on a new Mac is a new TPM, which is what Parallels falls back to as well.
  - The key trusts the binary that made it. `host/build.sh` signs `macneutron-vm` with `MACNEUTRON_SIGN_IDENTITY`
    when it is set, so a rebuild is still that binary. Signed ad hoc, the first VM after each rebuild asks once to
    use the keychain.
- **Each game's TPM is `state/<appid>.tpm`**, beside its state disk: run-guest.sh passes `--tpm` for every run (`MN_TPM`
  names another folder; empty turns it off), so the launcher's games and the gates all have one.
- Parallels turns Secure Boot on with its TPM; ours waits for firmware (§6 row 3, spec milestone 6).

**Checked:**
- `eac-vm/tests/tpm-engine.sh` (the engine on the host, no VM; `host/tpm-test.c`):
  - A restart gives the same TPM: the same owner-hierarchy ECC primary key.
  - `permall` on disk is the plaintext's size plus 28 bytes and holds none of it.
  - A damaged file, and a key removed from the keychain, each give a new TPM with the old files in `backup-<id>/`.
  - An unreadable `permall` or `id` stops the start, moves nothing, and the next start has the same TPM. Before the
    fix, an unreadable `id` made a new TPM.
  - A keychain that says no (the test swaps in its own keychain read) and a folder another VM holds are both refused,
    with nothing moved, and the next start has the same TPM.
  - A second reset under the same id leaves the first one's backup as it was.
  - With saving turned off, it fails; the lock and backup cases fail on the code before them.
- `eac-vm/tests/tpm-host.sh` (the guest's own kernel):
  - Linux registers the chip: `/dev/tpm0` and `/dev/tpmrm0`, which need its boot-time HMAC session and so a working
    primary key.
  - It reports a TPM 2.0 and reads PCR 0 through sysfs (zeros: nothing is measured yet).
  - A second boot keeps the same id, with nothing moved aside, and so does a boot after rebuilding `macneutron-vm`
    signed with the same identity: it read its key with no keychain dialog.

**Open** (the §6 questions still stand):
- Whether EAC v2 accepts a virtual TPM whose endorsement key MacNeutron certifies. There is no EK certificate yet; it
  would go in the TPM's NV at the TCG's index, signed by a MacNeutron CA, as swtpm_setup does for its TPMs.
- Measured boot (4) needs Secure Boot firmware (3) first.

## 14. The app sets up its own VM (2026-10-17)

Spec `docs/superpowers/specs/2026-10-17-macneutron-vm-setup-design.md`, plans `2026-10-17-vm-setup-1..4`. A Mac with only
MacNeutron.app gets the VM folder (`~/Library/Application Support/MacNeutron/vm`) by itself:
- **Host tools** ride in the app: `Contents/Helpers/vm` (macneutron-vm, libkrun, the Steam forwarder's host, gvproxy,
  the Metal host) and `Contents/Resources/vm` (the two scripts: codesign takes a script in Helpers for unsigned code).
  `VMToolsInstaller` copies them in on each app version, never while a VM runs from the folder, never into a developer's
  linked folder.
- **Images are built on the Mac** by `macneutron vm-setup`, with no VM and nothing unpacked onto the case-insensitive
  disk:
  - Fedora's pinned packages (166 aarch64, 177 MB, plus FEX's x86-64 root, 969 MB) come from Fedora's mirrors, falling
    back to Koji, each checked against the SHA-256 in the app's `vm-manifest.json`.
  - Our parts (118.7 MB xz: the recipes, an overlay of what package scripts and our build wrote, `wine.tar`, the guest
    tools) are a release asset.
  - RPM payloads (zstd through the vendored decoder in `Sources/CZstd`, xz through Compression) fill a store by SHA-256.
    The recipes then stream tars into a bundled `mkfs.erofs` (erofs-utils 1.9.4 with lz4, built here by plain clang),
    and the initramfs's cpio.
  - FEX's image is used as FEX ships it, mounted by its virtio-blk id.
- **The recipes** (`eac-vm/image/recipe.py`, run by `build.sh` in the builder) record the builder's base tree and
  initramfs file by file.
  - Base: 17,882 entries, 193 from the overlay.
  - Initramfs: 3,301 entries, 24 from the overlay, 5 device nodes.
  - The overlay is 26.9 MB of generated files and our FEX. No Fedora binary ships.
  - Making this true changed the build: the initramfs is now made by the pinned root's own dracut, in a chroot. Before,
    dracut took the builder's own programs (its systemd was 259.5 against the image's 259.9). The rpm and dnf
    databases, and kernel-core's unused `/boot` initramfs, leave the base tree.
- **The app** runs the setup when the folder's `images/manifest.sha256` isn't its manifest's hash, and shows "Game VM:
  downloading / building its images / ready". A game running from the folder defers the swap (exit 75), retried from
  the poll. A failure waits for Retry. Settings' Rebuild clears the downloads and builds a fresh set.

**Checked** (`eac-vm/tests/vm-setup.sh`, the gate): a folder set up from the builder's packages and the parts, served
as `file:` URLs, took 22 s. Its base, Wine and initramfs images equal the builder's (type, path, mode, owner, size,
link, SHA-256, capabilities), and the kernel and x86-64 root are byte for byte the same. It booted 7.2.10-200.fc44 with
`[integrity]`, `sig_enforce` Y and `/dev/tpm0`, and played sound under FEX (2.008 s for 2 s) and Wine (1.995 s).
A Mac downloads about 1.27 GB.

**Release** (`release.sh`):
- It builds the images in the builder, archives the parts, writes the manifest naming the release's asset URL, and
  checks that the template stays sparse and that the manifest names the archive.
- It signs the helpers under the hardened runtime (macneutron-vm with the hypervisor entitlement, mn-steam-host with
  disable-library-validation).
- No helper names a build path: libkrun remaps Cargo's paths, and the forwarder is rebuilt against the release
  wine.app's lsteamclient.
- The licences ship (libkrun's 92 crates among them), and erofs-utils' source goes in the source archive.

**Reviews** (one per two plans) found, and the fixes passes settled:
- Koji's path takes the source package's name (kernel-core is under `packages/kernel/`). The pinned kernel was already
  off the mirrors, so setup failed on every real Mac until the manifest carried each package's source RPM.
  `vm-setup --check-sources` now HEADs every pin, and the release runs it: 167/167 answer.
- The tools stamp is a hash over every item, the store goes before the last image (the floor is 8 GB), and the
  builder's `/etc/machine-id` no longer ships.
- The guest Wine's source (`wine-linux`: eac-vm/wine-patches on its BASE) is in the source archive, and the parts
  carry their licences.
- The parts archive names no account (root owners, no Mac metadata), and the release checks the builder's `/mnt/out`
  is this tree's.
- One setup a folder (a lock), and the manifest's hash leaves out the parts' URL.

**Before publishing, settled 2026-10-17:**
- llvm-mingw's C++ runtime builds with its llvm-project folder mapped away (`dxmt/llvm-mingw.sh`). libc++abi's
  checks had named this tree in every DXMT DLL, the macOS wine.app's too. The release refuses parts or an app that
  name a build or home path.
- Each guest stage records what it was built from (`.mn-source`, `eac-vm/image/stamps.sh`): the guest Wine, the
  Steam forwarder, DXMT's guest DLLs and winemetal.so, and FEX. `eac-vm/image/guest-stages.sh` rebuilds them from the
  tree, and the release refuses a stage with no stamp or another tree's.
- gvproxy's notices ship: its own, its 17 Go modules' from its vendored source at v0.8.9, and Go 1.26.3's
  (`eac-vm/net/licences.sh`).

**Rehearsed 2026-10-17:** `release.sh --rehearse 0.6.0` passes end to end (R3 against Mac Steam, the VM's parts and
manifest with every pinned package answering, R4, the zip, R5 sources), and `eac-vm/tests/vm-setup.sh` on the images it
built. It ran on the stable series' trees: this branch's own trees are the upstream-master experiment's (its merges are
commits in the trees, past the pins), which check_trees refuses. Wine 0037 left the series for
`wine-arm64/patches/wine-master` (only master's mem areas need it); every other patch applies at the pins. The stable
trees, their build folders and wine.app wait in `build/wine-arm64-src.stable`; the experiment's are back in
`build/wine-arm64-src`.

**Open:**
- A release from this branch builds for the M5 (`MACNEUTRON_CPU=apple-m5`, the experiment's default): the launcher
  refuses an M1-M4 Mac (Preflight). A release for every Mac needs `MACNEUTRON_CPU=generic`.
- A release that rebuilds the images changes the parts' bytes, so every Mac downloads again; reusing an unchanged VM's
  parts is the maintainer's call.
- The first run on a clean macOS account is the maintainer's (plan 4, task 3).

## 15. Direct3D forwarding, M0: the baseline (2026-10-17)

Spec `docs/superpowers/specs/2026-10-17-macneutron-vm-d3d-forwarding-design.md`, plan
`docs/superpowers/plans/2026-10-17-vm-d3d-forwarding-m0-measure.md`. SMITE 2 in a match, 1440p, DLSS Performance (on
MetalFX), the maintainer playing; `eac-vm/tests/perf-compare.sh`, 60 s each, launched with
`/usr/bin/env MACNEUTRON_PERF=1 %command%` (and `MACNEUTRON_VM=0` for macOS). Palworld was skipped (maintainer).

| | macOS | VM | VM / macOS | Gate |
|---|---|---|---|---|
| CPU (the game's Wine processes; `macneutron-vm`, `mn-steam-host`, `gvproxy`) | 4.64 cores | 11.64 cores | 2.51 | 1.5 or less: FAIL |
| GPU busy (`ioreg`) | 97.7% | 91.6% | | 95% or more: FAIL |
| Frame rate | 119.5 fps (the game's 120 cap) | 108.6 fps | 0.91 | 0.90 or more: pass |

The VM's numbers are its second run (14:49-14:50), with the host `sample` after the window. A first run took the
sample inside the window: `sample` suspends the process at each tick, and the VM's own frame counter fell to 25 and
1.4 fps for those 10 seconds, so that run's CPU, GPU and frame rate were all low. In the second run the record's
frame rate still took in the windows the sample stalled after it (94.9 fps in its JSON); 108.6 is the mean of the ten
5 s windows inside the measured minute (96.8-116.3), and `perf-compare.sh` now stops reading at the window's end
(`perf-compare-test.sh`). The macOS run ran with DXMT_STATS and its capture mode (its stats need the DXIL dump folder,
which also saves every new shader, root signature and pipeline): a little more CPU on macOS than without them.

Where the VM's CPU goes, from the guest's profiler (`top -H`, % of a vCPU, mean over 5 s batches) and the host
`sample`'s shares scaled to what `ps` measured for `macneutron-vm` (second run):

| Where | Cores |
|---|---|
| The game's own threads: `Background Work` 2.20, `Foreground Work` 0.81, `GameThread` 0.65 | ~3.7 |
| wineserver (x86-64 under FEX) | 0.67 |
| Rendering: `RenderThread 0` 0.32, `RHISubmissionTh` 0.25, `RHIThread` 0.20, `RHIInterruptThr` 0.16 | 0.92 |
| DXMT's own thread (`dxmt-cmdbuf-wai`) | 0.14 |
| Sound in the guest (`AudioMixerRende`, `FAudio_AudioCli`) | 0.24 |
| Host: guest code (`hv_trap`) | 6.44 |
| Host: vCPU wake-ups (Hypervisor.framework's lock and interrupt bookkeeping) | 2.53 |
| Host: executor consumers spinning | 0.71 |
| Host: sound (`audio.c`) | 0.62 |
| Host: other (gpu worker, vsock, virtiofs) | 1.35 |

DXMT's share, measured natively in the macOS run's `stats.txt` (DXMT_STATS times every D3D12 call on every thread,
the game's workers included): 0.44 cores in D3D12 calls, 0.33 without `allocator.Reset`'s fence waits.
`RHISubmissionThread` 0.22, `RHIInterruptThread` 0.11, `RHIThread` 0.03, DXMT's own thread 0.02; the task-graph
workers about 0.01 each.

What it says:
- DXMT is about 7-10% of the macOS run's CPU natively. In the guest it runs as x86-64 under FEX, several times
  slower, inside the rendering threads' 0.92 core and its own 0.14: moving it to the host (spec §4) returns roughly
  that difference, on the order of a core of the guest's 6.4. Not enough for the 1.5x CPU gate alone.
- The host's own overheads, 5.2 cores (wake-ups 2.5, spinning, sound, other), and wineserver's 0.67 are together
  much larger than DXMT's share. They are spec §5.3's work (M5); wineserver isn't in the spec yet.
- How the numbers are taken matters. The first VM run with `DXMT_STATS=1` in the guest fell to 33 fps (it times
  every D3D12 call, a clock read is about 270 ns under FEX, and its capture mode writes each new shader and pipeline
  over the share), so the VM's frames are counted by `macneutron-vm` at present (`MN_PRESENT_STATS`). And `sample`
  stalls what it samples: a same-window check gave 1.03 cores from the sample against 0.47 from `ps` for those
  seconds, so only its shares are used.

## 16. Direct3D forwarding, M5: the VM's overheads (2026-10-17)

Plan `docs/superpowers/plans/2026-10-17-vm-d3d-forwarding-m5-overheads.md`. Each change was measured on a repeatable
scene, SMITE 2's lobby in the dev VM (`eac-vm/tests/bench-smite2.sh`: 60 s once the lobby is up, sound to nowhere,
the guest profiler and the host's frame counter, `perf-compare.sh vm`). The lobby is capped at 60 fps, so it shows CPU
saved at the same frame rate, not headroom. Two runs of the same setting (base-a/b) set the noise: 0.1 core, 0.1 fps.

| Lobby run | CPU (cores) | wineserver | fps | Host: wake / spin / other |
|---|---|---|---|---|
| Before M5 (base-a, base-b) | 7.74, 7.65 | 47% | 58.1, 58.2 | 0.30 / 2.91 / 0.29 |
| Executor spin 200 us, then park (was 2 ms) | 5.60 | 47% | 59.1 | 0.30 / 1.19 / 0.40 |
| App Nap off, window uncovered / covered | 5.40, 5.50 | 46-47% | 58.0, 59.2 | 0.29 / 1.04-1.16 / 0.30 |
| NTSync (two runs) | 4.00, 4.12 | 6% | 59.4, 59.5 | 0.15 / 0.95 / 0.29 |
| NTSync, 12 vCPUs | 3.73 | 5% | 59.5 | 0.10 / 1.06 / 0.24 |
| NTSync, 9 vCPUs | 3.47 | 5% | 59.7 | 0.08 / 0.85 / 0.27 |
| NTSync, 6 vCPUs | 3.08 | 4% | 59.6 | 0.05 / 0.89 / 0.29 |
| NTSync, spin 50 us | 3.72 | 7% | 59.4 | 0.15 / 0.53 / 0.29 |

The changes:
- **The executor's consumers** spin 200 us, then park until the doorbell (`MN_EXECUTOR_SPIN_US`, 0-2000).
- **The sound server** wakes once a period (10 ms) instead of polling: about 800 wake-ups a second fell to 84
  (`audio-test.sh`'s calm case). The lobby can't show it (sound goes nowhere); M0's match had sound at 0.62 cores.
- **Settings for the match:** `MN_VM_CPUS` (vCPUs, 1 to the Mac's cores; every core by default) and
  `MN_VM_CMDLINE_EXTRA` (tick and RCU parameters only: `nohz_full=`, `rcu_nocbs=`, `nohz=`, `skew_tick=`,
  `irqaffinity=`).
- **No App Nap.** A covered VM window let macOS nap the whole host: timers coalesced (the sound's 10 ms poll took
  about 90 ms), and the lobby fell to 19-22 fps behind a browser window. `macneutron-vm` now holds a user-initiated,
  latency-critical activity for its life (`appnap.sh`), which also keeps an idle Mac from sleeping under a running
  game. Every "collapse" first seen with fewer vCPUs, a 50 us spin and a native wineserver was this: those windows
  were covered.
- **NTSync.** Fedora's signed `ntsync` module was in the image but never loaded (it has no devname alias), so every
  wait, event and mutex the game made went through wineserver. `modules-load.d` loads it at boot (`wine.sh` checks
  the server holds `/dev/ntsync`); the driver makes the device 0666. wineserver fell from 0.47 cores to 0.06 in the
  lobby, and the guest's own time with it.
- **A native arm64 wineserver** (a Wine patch, `WINESERVER_MACHINE=x86_64`) was tried and dropped: on the same image
  the x86-64 server under FEX used 46% at 59 fps and the native one 42% at 50 fps, with no saving a frame. FEX wasn't
  wineserver's cost, the sync traffic was, and NTSync removed it.
- **The host's other work** is real work, not polling: 0.42 of its 0.53 sampled is Metal's command-buffer submission
  (`IOGPUCommandQueueSubmitCommandBuffers`), M1-M4's to shrink; virtio-gpu's worker is about 0.1, and the one timed
  poll left (mouse capture's 50 ms check) is under 0.01.

The gate (spec §8: in a match, the host's overheads from 5.2 cores to 2.0 or less, wineserver from 0.67 to 0.2 or
less, frame rate and GPU busy no lower than M0's 108.6 fps and 91.6%) isn't taken yet: the maintainer's VM folder
builds its images from a release's parts, and NTSync comes with the next release. In the lobby the host's overheads
are 1.4 cores with today's defaults (0.97 with a 50 us spin), from 3.5 before M5, and wineserver is 0.06. The match
decides the defaults the lobby can't: 6 to 12 vCPUs and the 50 us spin, since without App Nap neither cost a frame
in the lobby.

Open: the 4b-1 Metal gate (`eac-vm/metal/gate.sh`) crashes `macneutron-vm` in the host's `winemetal.so` on the main
queue (games run); with `MN_EXECUTOR_SPIN_US=0` SMITE 2 never reached the lobby and the VM didn't power off.

## 17. Direct3D forwarding, M1: native DXMT (2026-10-17)

Plan `docs/superpowers/plans/2026-10-17-vm-d3d-forwarding-m1-native-dxmt.md`. Our DXMT fork builds natively, as arm64
macOS dylibs (`eac-vm/dxmt-native/build.sh`: `d3d12`, `dxgi`, `d3d11`, `d3d10core`, `winemetal` with airconv, 34 MB),
and DXMT's headless D3D12 programs built natively against them print what they print on the macOS Wine runtime.

The go/no-go, before the plan: upstream's native path (VirtualBox's, which forwards guest D3D11 to native DXMT on the
host) built everything but our D3D12, which failed with 259 errors. With the PE build's D3D12 headers and the
aggregate-return macro, 123 were left, all Win32 calls in diagnostics and recording code: none in a rendering path.

Three fork patches:
- **0072**, real Win32 handles natively (`src/util/win32_native.h`): events and semaphores with timed waits, files
  (paths from native WCHAR's UTF-32 as UTF-8), the clock, thread names by id, `dlopen`, `setenv`. Upstream's stand-ins
  were warnings that did nothing (`CloseHandle` returned FALSE, `CreateSemaphore` NULL). `eac-vm/dxmt-native/tests/
  win32.sh`, eight cases.
- **0073**, the native build: the PE build's D3D12 headers (`include/native/d3d12`, llvm-mingw's WIDL 11.12, ahead of
  the older mingw-directx-headers), `WIDL_EXPLICIT_AGGREGATE_RETURNS`, the stand-ins in D3D12, constexpr enum flag
  operators as mingw's, and the replay tool left to the PE build.
- **0074**, found by the first native run (`d3d12_null`: "fail timeout"): winemetal's native build made a fence's
  `SetEventOnCompletion` a no-op, so no event was ever set. Its listener now runs natively too.

All three apply on the stable series (pin and 0001-0071) as well as on this tree. They touch `include/` and DXMT's
top-level `meson.build`, which the PE translator key hashes: users' DXMT caches re-translate once, behind the precache
toast, with the release that carries them.

The gate (`eac-vm/dxmt-native/gate.sh`): the 4b-2 gate's 19 headless D3D12 rows, each run once natively with a fresh
shader cache against two macOS runs through the runtime's launcher (`eac-vm/dxmt/ref.sh`), by `compare.py`'s rule.
Measured lines (timings, timestamps) are left out on both sides: natively they run at another speed, and the rule
masks a line only when the two macOS runs differ (twice they agreed to the tenth).

| Program | Lines | macOS (s) | Native (s) |
|---|---|---|---|
| d3d12_api | 49 | 7.22 | 0.60 |
| d3d12_bounds | 1 | 3.72 | 0.08 |
| d3d12_cache | 1 | 3.72 | 0.09 |
| d3d12_compress | 3 | 3.78 | 0.14 |
| d3d12_copy | 11 | 3.74 | 0.10 |
| d3d12_depth | 2 | 3.73 | 0.10 |
| d3d12_dxil | 8 | 3.72 | 0.09 |
| d3d12_dxil_exec | 13 | 3.76 | 0.10 |
| d3d12_hazards | 59 | 4.08 | 0.42 |
| d3d12_indirect | 23 | 3.86 | 0.23 |
| d3d12_layered | 1 | 3.73 | 0.10 |
| d3d12_lod | 85 | 3.76 | 0.12 |
| d3d12_null | 18 | 3.72 | 0.03 |
| d3d12_query | 1 | 3.71 | 0.09 |
| d3d12_timestamp | 2 | 3.74 | 0.11 |
| d3d12_triangle | 1 | 3.72 | 0.09 |
| d3d12_upscale | 1 | 14.67 | 0.72 |
| d3d12_volume | 2 | 3.73 | 0.09 |
| d3d12_vsread | 1 | 3.73 | 0.10 |

All 19 pass, 282 lines. The macOS times are mostly Wine starting each program (about 3.6 s); natively a program
starts in milliseconds. The test programs needed one portability fix: they printed HRESULTs as `unsigned long`, 64-bit
natively, which sign-extended a negative HRESULT (`ffffffff80070057`); they print through 32 bits now, the same digits
on Windows.

Left for later milestones: the XeSS rows (4) and DLSS rows (5) need the upscalers natively (§4.11, M3: there is no
Wine builtin `libxess` natively, and `nvngx` is a native build of its own); `d3d11_vsia` compiles HLSL with Wine's
`d3dcompiler`, which has no native build, and D3D11 is M4's. Native DXMT isn't in the app yet: it goes in with the
first milestone whose VM loads it.

## 18. A trap per call instead of a ring (spike, 2026-10-17)

The question: could a forwarded call skip the ring's cross-thread handoff (consumer threads that spin, doorbells, fence
waits) and run on the vCPU that made it? A guest access to a page the host leaves unmapped exits to that vCPU's own
host thread, which could execute the call and resume the guest. A throwaway libkrun build left one virtio-gpu blob of a
marked size unmapped (`GpuRemoveMapping` instead of `GpuAddMapping`) and answered the GPU window's exits on the vCPU
thread: a write counted, a read answered the count. No guest kernel change: the stock virtio-gpu driver maps the blob
into the program. `trapbench` (x86-64 under FEX, as a game's code runs, and arm64 natively) against today's ring
(`metal-transport-test bench`), one VM boot:

| Path | Cost |
|---|---|
| Trap: a store (a flush's doorbell) | 0.98 µs (natively 0.95-1.07) |
| Trap: a load (a call with an 8-byte answer) | 0.98 µs |
| Trap: store then load (doorbell and answer) | 2.0 µs |
| Ring: an asynchronous record | 11 ns |
| Ring: a synchronous call, the host's consumer spinning | 0.32 µs |
| Ring: a synchronous call on the fence (consumer not spinning) | 26.5 µs |
| Ring: a call that wakes a parked consumer | 48 µs |

Every trap arrived (400,000 of 400,000): FEX's stores carry a valid syndrome. FEX adds nothing to an exit.

Exits from several threads at once don't scale: 1.0, 1.3, 1.7, 4.5 and 8.2 µs an exit a thread with 1, 2, 4, 8 and 16
threads, about 2 million exits a second for the VM. Not the trap's code: answered lock-free before libkrun's bus, the
same. A host `sample` of 8 writers puts the waits in Hypervisor.framework's virtual GIC: every vCPU entry and exit syncs
its list registers under one VM-wide lock (`Gic::apply_list_registers`, `Gic::compute_list_registers`), and its MMIO
data-abort path takes it too (`Gic::write_memory`). That lock is taken by every exit the VM makes, timer interrupts and
idle wake-ups included: M0's "vCPU wake-ups" (Hypervisor.framework's lock and interrupt bookkeeping) are its other face.

What it says for forwarding: a synchronous call by trap costs about 1 µs with no host thread spinning, against 0.32 µs
with one spinning (a core while it spins) or 26-48 µs without. Asynchronous calls would still collect in memory at the
ring's cost; one exit flushes them. At tens of thousands of flushes a second (a game's submissions, presents and
synchronous calls) the exits cost a few percent of a core, against the 0.5-1 core the consumers spin and the wake-ups
they cause. The costs: the game's thread waits while the host encodes (native DXMT runs on the game's thread too), a
long host call (a pipeline's compile) stalls that vCPU, and every extra exit takes the GIC's lock. The spike's libkrun
was put back (`eac-vm/libkrun/build.sh`); nothing of it is kept.

The other controller (same day): libkrun keeps its own userspace GICv3 and uses Hypervisor.framework's only when
`hv_gic_create` exists (`vmm/builder.rs`). A throwaway switch skipped Apple's:

| | Apple's vGIC | libkrun's GIC |
|---|---|---|
| A trap, 1 thread / 8 / 16 | 1.0 / 4.4 / 8.8 µs | 0.89 / 2.2 / 5.5 µs |
| Ring: waking a parked consumer | 44 µs | 29 µs |
| SMITE 2's lobby (two runs each) | 4.23, 4.14 cores; 59.3, 59.6 fps | 3.78, 4.27 cores; 58.9, 58.9 fps |

Without Apple's lock exits scale about twice as far and a wake is a third quicker, but the guest's interrupts become
exits libkrun emulates (the lobby's wake share 0.19-0.20 against 0.14-0.15), and the lobby, with few exits, can't tell
the two apart: its two userspace runs differ by more than the controllers do. A match, with M0's 2.5 cores of wake-ups,
is where the choice would show. The switch was removed with the rest of the spike.

## 19. Direct3D forwarding, M2: D3D12 and DXGI (2026-10-18)

Plan `docs/superpowers/plans/2026-10-17-vm-d3d-forwarding-m2-d3d12.md`. With `MACNEUTRON_VM_D3D=forward`, a guest
program's D3D12 and DXGI run in native DXMT on the Mac (§17) instead of the guest's x86-64 DXMT under FEX. The guest's
`d3d12.dll` and `dxgi.dll` are stand-ins whose exports forward to `mnd3d.dll` (a plain x86-64 PE): proxies for the
host's objects that pack each call with generated tables and hand it to the guest `winemetal.so` through winemetal's
`WMTForward` (its slot 155). The unix side queues the record on the thread's ring, or for a call with an answer stores
to a trap blob: libkrun runs the VM host's executor on that vCPU's own thread (§18's batched traps, libkrun 0009), which
runs the thread's records through the dispatcher into native DXMT and resumes the guest with the answer. A queue
operation (`ExecuteCommandLists`, a queue's `Signal` and `Wait`) first runs every other ring of the process, so
another thread's recorded lists are there. The default stays guest DXMT (`run-game.sh`, `prefix.sh`).

The generator (`eac-vm/d3d/gen.py`) reads widl's C headers from native DXMT's include set (`d3d12.h`, `dxgi.h` to
`dxgi1_6.h`, `d3dcommon.h`, `dxgidebug.h`), not the IDL the spec named: the headers are what both builds compile
against, with the guest's layouts checked against them (`make layout`). D3D12 and DXGI are 112 interfaces and 393
methods with 12 exports: 405 numbered calls. 195 are classified by the defaults alone (an interface pointer is a handle,
`REFIID` then `void **` a created object, `const T *` one input, a non-const pointer an output, an output or a
non-HRESULT return makes the call synchronous). `rules.txt` (245 rules) adds a rule to 92: counts, sizes, unions, wide
strings, the three queue operations' submit mode, the seven CPU descriptor writes made synchronous (below), 15 calls
with code of our own (fences, `Map`, `CheckFeatureSupport`, DXGI's descriptions in the guest's 2-byte WCHAR) and 9
special rules, 11 calls with guest code by hand (`CreatePipelineState` and `LoadPipeline` read the stream into the
description forms; the blob and the root signature deserializers are answered in the guest; `WriteToSubresource` and
`ReadFromSubresource` have none yet). 137 are refused in the guest (E_NOTIMPL): 118 of 47 interfaces DXMT doesn't
implement, and 19 that DXMT aborts on (`IMPLEMENT_ME`: `SetPredication`, `ExecuteBundle`, tiled resources) or leaves
out. Calls end up 59 asynchronous, 195 synchronous, 3 submit, 11 special and 137 refused.

What the guest keeps: `AddRef`/`Release` (the last release reaches the host after every ring that used the object),
`QueryInterface` (from the interfaces the host reported at creation; the first handle the host bound an object to
makes the guest return the live proxy, so a pipeline library's loaded pipeline is the stored object), fences (a mirror
page the host raises, `GetCompletedValue` without a call; events set by the guest when the host posts them) and mapped
memory (arenas of guest-shared memory, 64 MiB at a time, from which native DXMT's CPU-visible buffers and heaps are
carved through the memory hooks: `Map` answers the guest's own address). Fork patches: 0076 (the hooks and relay
events natively), 0077 (CPU-visible heaps over the hooks), 0078 (DXGI's adapter description in native WCHAR; it was
garbage natively), 0079 (`WMTForward`, call 155 only) and 0080 (a relay event's end). All apply on the pin.

The gates (`eac-vm/d3d/gate.sh`), against one macOS run each (`compare.py` given it as both references, measured lines
left out): the loopback (`loopback-gate.sh`: the 19 programs linked to a library that runs the guest's proxies,
marshaller, rings and the host's dispatcher in one Mac process), then the VM (`tests/dxmt.sh` with forwarding on, one
boot). `coverage.py` over the two traces (`MACNEUTRON_VM_D3D_TRACE=1`): 89 methods called about 38,400 times, every
one forwarded and reaching the host. The guest-DXMT column is the 4b-2 gate's (`eac-vm/dxmt/gate.sh`, the same day).

| Program | Lines | macOS (s) | Loopback (s) | VM, forwarded (s) | VM, guest DXMT (s) |
|---|---|---|---|---|---|
| d3d12_api | 49 | 7.18 | 0.65 | 0.66 | 1.50 |
| d3d12_bounds | 1 | 3.71 | 0.10 | 1.01 | 1.05 |
| d3d12_cache | 1 | 3.72 | 0.10 | 1.00 | 1.05 |
| d3d12_compress | 3 | 3.74 | 0.14 | 1.03 | 1.09 |
| d3d12_copy | 11 | 3.71 | 0.10 | 0.99 | 1.04 |
| d3d12_depth | 2 | 3.70 | 0.10 | 0.99 | 1.07 |
| d3d12_dxil | 8 | 3.70 | 0.10 | 0.98 | 1.05 |
| d3d12_dxil_exec | 13 | 3.73 | 0.11 | 1.02 | 1.08 |
| d3d12_hazards | 59 | 4.07 | 0.44 | 1.41 | 1.46 |
| d3d12_indirect | 23 | 3.84 | 0.23 | 1.13 | 1.20 |
| d3d12_layered | 1 | 3.71 | 0.10 | 1.01 | 1.05 |
| d3d12_lod | 85 | 3.74 | 0.12 | 1.02 | 1.08 |
| d3d12_null | 18 | 3.70 | 0.10 | 1.00 | 1.04 |
| d3d12_query | 1 | 3.70 | 0.10 | 0.99 | 1.05 |
| d3d12_timestamp | 2 | 3.72 | 0.11 | 1.01 | 1.08 |
| d3d12_triangle | 1 | 3.70 | 0.10 | 0.99 | 1.05 |
| d3d12_upscale | 1 | 14.74 | 0.75 | 8.13 | 8.09 |
| d3d12_volume | 2 | 3.70 | 0.10 | 0.99 | 1.04 |
| d3d12_vsread | 1 | 3.71 | 0.10 | 0.98 | 1.06 |

All 19 pass both ways, 282 lines, with no host error. These programs time Wine and the GPU, not the CPU: in the VM
most of a program's second is a fresh wineserver (`dxmt.sh` stops it after each program; the first program finds
wineboot's: `d3d12_api` here, 0.66 s, `d3d11_vsia` in the 4b-2 gate, 0.29 s), and forwarded and guest DXMT are within
0.1 s of each other. The CPU question is M5's, in a game. The VM host peaks at 4.5 GB with forwarding and without
(`d3d-smoke.sh`, `d3d12_null`).

The VM's other gates still pass (`vm-setup.sh`, `turn.sh`, `modes.sh`, `tpm-host.sh`) but two things fail, neither
from M2's code: `audio-host.sh`'s small-buffer case (136-142 position reports a second against its bar of 150, with
`audio.c` unchanged since it measured 170, the Mac under load), and one 4b-2 row, `d3d12_dlss_refuse`: in the guest
the program reads all three of `nvngx`'s refusals as successes, though `nvngx` logs them and returns the right codes
(today's build, loaded as the builtin). The first of them returns before any Direct3D call, so the guest loses
`nvngx`'s return value on the way back (FEX or the call path): open, and a game could take a refused DLSS feature as
made. The 4b-2 gate runs its references once now, as the others do.

The final review (one fresh reviewer over the whole branch) found three things the gates didn't, each now pinned by a
test that failed first:
- The thread that sets the events the host posts was a pthread of the unix side's, so `NtSetEvent` ran on its
  creator's TEB and server pipes; when that thread had ended, the process died at its first event set
  (`tests/smoke.cpp`'s eventthread line in `d3d-smoke.sh`: the set must be the event's first use, or ntsync's cached
  handle needs no server call). It is now `mnd3d.dll`'s own Wine thread (op 6), started with the first event a fence is given.
- A CPU descriptor written on one thread was still in that thread's ring when another thread recorded with it
  (`threads.cpp`'s view case: a clear through another thread's new view cleared nothing, 64 of 64 pixels). The seven
  descriptor writes now run when they're made (a trap each; M5 measures what that costs); left open, another thread
  overwriting a descriptor after a recording read it and before that recorder's ring ran.
- Two host calls trusted guest pointers: `CheckFeatureSupport`'s levels when the guest hadn't put them after the
  struct (DXMT read the guest's address: `dispatch-test`, SIGSEGV), and the blob op on an object that isn't a blob.
Not fixed, for the maintainer: the host runs a record on whatever object its handles name, as native DXMT trusts its
caller, so a guest that names a fence where a device belongs, or forges a descriptor handle (DXMT's carry host
pointers, passed through as the spec says), can crash or corrupt `macneutron-vm`. Whether the VM must be a security
boundary against its own game decides whether every record's objects get checked against what they answer.

Also from M2: the VM host's native DXMT wrote an empty `_dxgi.log` into the folder `macneutron-vm` ran from; its logs
go to the run's `traces/` now.

The review's deferred minors, fixed after it (2026-10-18), each with a test that failed first where one could:
- `SetName` is the guest's own `SetPrivateData(WKPDID_D3DDebugObjectNameW)`, so `GetPrivateData` hands back the
  guest's 2-byte WCHARs; forwarded, it read back native DXMT's UTF-32 (`smoke.cpp`'s name line: 24 bytes for 12).
- An event goes to the host as the listener's own duplicate, closed once set: Wine gives a closed handle's value to the
  next event at once, and a program that closed its event after `SetEventOnCompletion` had its next one set
  (`smoke.cpp`'s eventreuse line, forwarded; guest DXMT does the same).
- `WMTForward` (0079) takes call 155 only; 156 indexed past the guest's table (an access violation in both modes).
- A relay DXMT never sets no longer keeps its context's notify queue and mirror for good (`dispatch-test`): a relay
  ends when DXMT lets it go (0080: a wait on no fences), and the context's end disarms the rest. Metal itself keeps an
  unreached notification's block and event for good (a probe: neither the event's release nor the listener's frees
  them), so a fence released with an event still to set leaves that relay's few bytes, Metal's, and in the guest the
  listener's duplicate, until the process ends.
- The unix side's per-thread buffers (320 KiB) go with their thread (`threads.cpp`'s exits case: 512 threads kept
  160 MiB).
- An identity hit on an object the guest knows only as another interface (a companion `QueryInterface` made) is a proxy
  of the interface asked, not of the object's first one (`tests/identity.sh`); the host's identity list is a hash map
  by object (native DXMT shares none between contexts), and a companion's handle counts in it.
- `MN_D3D_NEED_ARENA` is retried up to 8 times, not once: another thread's creation can take the arena one added. No
  test caught it: 16 threads making 48 buffers that each want an arena never lost one (a creation's Metal call takes
  milliseconds, the window microseconds).
- `memory.c` says what the review found (a creation's own DXMT buffers, such as a shader-visible heap's argument
  buffer, come from the arenas too, as guest DXMT's come from guest memory), and `build-tests.sh` leaves no
  `_dxgi.log` where it runs.

Left for M3: presentation (swapchains on the VM window's layer, `Present`, the frame-latency object: the swapchain
methods are forwarded as generated but untested, and a guest window's handle means nothing natively), XeSS and DLSS
natively, and the games; the launcher's DXMT settings, which native DXMT reads from the VM host's environment, not the
guest program's (NVEXT, stats, a shader cache per game: natively it defaults to one for every game), native DXMT in
the app's VM folder, `WriteToSubresource`/`ReadFromSubresource`, and DXMT's conditional `IMPLEMENT_ME` paths, which
still abort the VM host rather than fail the call. The guest caches no immutable answer yet (spec §4.5): M5 measures
whether one is worth it. Left for M4: D3D11 and D3D10 (`d3d11_vsia`). Until then forwarding breaks them in the same
prefix: the stand-in `dxgi.dll` replaces DXMT's, whose adapter guest DXMT's `d3d11` needs, so
`MACNEUTRON_VM_D3D=forward` is for D3D12 games only.

## 20. Direct3D forwarding, M3: games (2026-10-18)

Plan `docs/superpowers/plans/2026-10-18-vm-d3d-forwarding-m3-games.md`. M2's forwarding (§19) gains what a game needs
on top of the headless programs: presentation, pacing, the upscalers, and the shader cache and precache. With
`MACNEUTRON_VM_D3D=forward` SMITE 2 plays a match in the VM on native DXMT, with XeSS and with DLSS on MetalFX, every
method it calls forwarded. Palworld hasn't run yet (the maintainer's go-ahead), so M3's gate is open for it.

What M3 built:
- Presentation (spec §4.8). A swapchain is made from the game's window in the guest and presents into the VM window's
  layer on the host. Native DXMT asks about windows and displays through window-system hooks (fork 0082): the guest
  pushes a window's state before the calls that read it (op 7) and the displays and their modes (op 8). Full screen
  is native DXMT's own logic; the window actions it takes (save, enter, leave, minimise) come back in the reply and
  are replayed in the guest with DXMT's own Win32 window code (fork 0083, op 9), so the guest's emulated mode switch
  is Wine's EmulateModeset, as in guest mode. A present hook (0081) counts the drawables presented and shown.
- Pacing in the guest (§4.7). Each swapchain has a fence signalled after each `Present`. Without the frame-latency
  object, at most 3 frames are in flight (DXGI's default); with it, the object is a guest semaphore that a pacer
  thread releases. Presents also pace on the display: the host mirrors the drawables presented and shown (Metal's
  presented handler) and the guest waits, on a shown event (op 11), until at most one is unshown. No present holds a
  vCPU: the host's `mn-d3d: slow:` lines (calls over 4 ms on a vCPU, by method, at a context's end) are the smoke's
  check. `Present` stays synchronous, so its HRESULT is native DXMT's.
- DXMT's `IMPLEMENT_ME` throws natively (0084), and each host stub catches it: the call fails, logged, and the VM host
  goes on (it used to abort it).
- The upscalers (§4.11). XeSS (Wine's builtin `libxess`) and DLSS (guest DXMT's `nvngx`) reach native DXMT's MetalFX
  through its private interfaces, forwarded like the rest. DXMT's NVIDIA extension is on natively from
  `DXMT_ENABLE_NVEXT=1` at `dxgi.dylib`'s load (0085), set in `macneutron-vm`'s environment by the launcher's
  `MACNEUTRON_DLSS=1` (process-wide on the VM host: a VM runs one game); the guest writes the registry keys NVIDIA's
  loader reads. A companion interface is cached once per object, as COM identity needs.
- Shaders and precache (§4.12). Native DXMT's shader cache and pipeline recordings live in the game's VM state folder
  (`MN_D3D_STATE=<vm>/state/<appid>.dxmt`), and a native `dxmt-replay` (0086) replays the recordings before the VM
  boots, as the macOS precache does. The release ships native DXMT beside `macneutron-vm` (`dxmt/`, signed).
- The generator gained `wrap` (guest code first, then the generated proxy), for methods whose guest side must push
  state first; it covers exports too. DXMT builds are keyed on their tree's commit, after a stale guest `nvngx` object
  read refusals as successes.

Fork patches 0079 (amended) and 0080 to 0086; the series applies on the pin.

SMITE 2's first forwarded runs found four faults the gate programs didn't; each now has a test that failed first:
- `GetDisplayModeList` took the generator's defaults (the count out only, the list one struct), and DXMT answered
  `DXGI_ERROR_MORE_DATA`, fatal to the game. Rules now marshal the count in and out and the list as long as it
  (`dispatch-test`).
- The VM host crashed in DXMT's `EndQuery`: one thread opens a command list and stamps its start with a timestamp,
  another records the rest and closes it, and the close ran before the first thread's queued records. A command
  list's method on a list another thread called last is now sent as a submit (flushed with `MN_REC_SUBMIT`: the
  other rings run first). `threads.cpp`'s handoff case crashed the loopback with SMITE 2's stack.
- The game died in its god-select lobby: Unreal calls `IDXGIAdapter3::QueryVideoMemoryInfo` (slot 14) through the
  `IDXGIAdapter` that `EnumAdapters` gives it. DXGI's and DXMT's adapters have one vtable for every version; our
  proxy's stopped at slot 9. A proxy now has the vtable of the most derived interface its object answers at its own
  pointer (`identity.sh`). FEX with multiblock (its default) reported the fault inside `EnumAdapters`' proxy;
  `FEX_MULTIBLOCK=0` reported the jump to 0 exactly, and the game's code at the return address showed the call. When
  a fault under FEX doesn't fit its registers, that is the setting to try first.
- The game calls `OpenExistingHeapFromAddress`, which the guest refuses with the `E_NOTIMPL` native DXMT answers
  itself (Unreal's GPU breadcrumbs go without). `coverage.py` takes the four refusals that give DXMT's own answer as
  forwarded.

The gate's programs. The 28 D3D12 rows of the 4b-2 gate (the 19, 4 XeSS, 5 DLSS), each against one macOS run by
`compare.py`'s rule: guest mode (`eac-vm/dxmt/gate.sh`, its 29 rows with `d3d11_vsia`) and forwarded
(`eac-vm/d3d/gate.sh`, the loopback first). All pass both ways, with no host error; coverage over the forwarded
traces: 96 methods, every one forwarded. Seconds, one run each:

| Program | macOS (forwarded's ref) | VM, forwarded | macOS (guest's ref) | VM, guest DXMT |
|---|---|---|---|---|
| d3d12_api | 7.17 | 0.66 | 4.16 | 1.53 |
| d3d12_bounds | 3.72 | 1.05 | 3.73 | 1.08 |
| d3d12_cache | 3.72 | 1.05 | 3.72 | 1.08 |
| d3d12_compress | 3.77 | 1.09 | 3.76 | 1.12 |
| d3d12_copy | 3.71 | 1.05 | 3.70 | 1.08 |
| d3d12_depth | 3.71 | 1.05 | 3.74 | 1.08 |
| d3d12_dxil | 3.71 | 1.05 | 3.73 | 1.08 |
| d3d12_dxil_exec | 3.73 | 1.07 | 3.76 | 1.11 |
| d3d12_hazards | 4.07 | 1.42 | 4.09 | 1.47 |
| d3d12_indirect | 3.86 | 1.19 | 3.86 | 1.24 |
| d3d12_layered | 3.72 | 1.04 | 3.95 | 1.08 |
| d3d12_lod | 3.74 | 1.11 | 3.75 | 1.11 |
| d3d12_null | 3.71 | 1.04 | 3.72 | 1.09 |
| d3d12_query | 3.73 | 1.04 | 3.71 | 1.07 |
| d3d12_timestamp | 3.73 | 1.06 | 3.75 | 1.10 |
| d3d12_triangle | 3.72 | 1.05 | 3.73 | 1.10 |
| d3d12_upscale | 14.80 | 8.48 | 15.43 | 8.51 |
| d3d12_volume | 3.71 | 1.04 | 3.74 | 1.09 |
| d3d12_vsread | 3.72 | 1.03 | 3.73 | 1.08 |
| d3d12_xess_quality | 18.47 | 8.29 | 15.78 | 8.01 |
| d3d12_xess_performance | 11.50 | 6.12 | 11.78 | 6.10 |
| d3d12_xess_nodepth | 34.26 | 16.87 | 35.20 | 16.97 |
| d3d12_xess_api | 3.85 | 1.13 | 3.89 | 1.15 |
| d3d12_dlss_vendor | 3.71 | 0.15 | 3.72 | 1.06 |
| d3d12_dlss_quality | 14.50 | 8.31 | 15.94 | 8.42 |
| d3d12_dlss_performance | 11.28 | 6.60 | 18.55 | 6.25 |
| d3d12_dlss_inverted | 12.73 | 7.34 | 22.76 | 7.07 |
| d3d12_dlss_refuse | 3.79 | 1.12 | 4.30 | 1.13 |

As in §19 these time Wine and the GPU, not the CPU (a row's first program finds wineboot's wineserver: `d3d12_api`
and `d3d12_dlss_vendor`, the DLSS rows' own boot). The 4c gate's rows forwarded (`present-gate.sh forward`): every
`d3d12_clear` and `present_loop` check passes, with frames of 8.34 ms against macOS's 7.78 and 7.44-7.51 ms against
7.26-8.30; `d3d12_fullscreen`'s three leave cases match guest mode. Task 4 measured `d3d12_clear` at 9.30 ms a frame
forwarded against 8.40 in guest mode: with Metal's three drawables only one frame can wait for the display without a
host wait (the upgrade: DXMT's presenter fetching its next drawable on a thread of its own).

The games, recorded and not graded (M5's). SMITE 2 through `present-gate.sh forward` with `MN_GATE_GAMES=1` (Mac
Steam, a practice match the maintainer played, 1920x1080, the game's settings):

| Run | Frames presented | Frame rate | VM host CPU | Upscaler on the host | Coverage |
|---|---|---|---|---|---|
| Forwarded, XeSS | 13,610 | about 78 fps | 3.6-4.4 cores (`ps`, in the match) | 13,289 `TemporalUpscale` | 117 methods, all forwarded |
| Forwarded, DLSS (`MN_GATE_DLSS=1`) | 7,114 | about 63 fps | (not sampled in the match) | 4 scalers, 6,680 `TemporalUpscale` (`nvngx`) | 118 methods, all forwarded |
| M0, guest DXMT (§15: 1440p, DLSS Performance) | | 108.6 fps | 11.64 cores (with `mn-steam-host` and `gvproxy`) | | |
| M0, macOS (§15) | | 119.5 fps (the cap) | 4.64 cores | | |

Forwarded, the VM host uses about the cores macOS does, a third of guest mode's, but the frame rate is lower: the
settings differ from M0's, and where the frames go is M5's question. Leads from the runs, largest first: the CPU
descriptor writes, about 765 a frame in the DLSS run (`CopyDescriptors` 3.93 million, `CopyDescriptorsSimple` 0.79
million, `CreateShaderResourceView` 0.67 million over 7,114 frames), each a submit (an exit that takes the one submit
lock and runs every ring, §19); every other call with an answer is an exit too (`QueryVideoMemoryInfo` about 32 a
frame, `GetBuffer`, `GetCurrentBackBufferIndex` and `GetFullscreenState` each frame); and pipeline creations still
compile on vCPU threads (721 graphics pipelines over 4 ms in one run, the longest 120 ms). The 7 `GetResourceAllocationInfo failed`
lines in SMITE 2's log are DXMT's: the macOS run logs the same 7.

The final review (one fresh reviewer over the branch) found two things the runs didn't, each now pinned by a test that
failed first:
- The guest's replay of native DXMT's window actions held the swapchain's lock across DXMT's Win32 window calls, which
  send `WM_SIZE` to the game's window procedure at once: a game that answers `WM_SIZE` with `ResizeBuffers(0, 0)`, as
  DXGI's samples do, hung in `ResizeTarget` and `SetFullscreenState` (`smoke.cpp`'s wmsize line, forwarded; guest DXMT
  drops its own lock there, and passed). The calls now run on a copy of the saved window state, without the lock.
- A record naming no object (`self` 0) ran its stub on NULL and could take the VM host down (`dispatch-test`); the
  dispatcher refuses it with `STATUS_INVALID_HANDLE`.

Open:
- Palworld (M3's gate), with the maintainer.
- In a forwarded process a D3D11 program gets the forwarded DXGI, whose adapter guest DXMT's `d3d11` refuses ("Not a
  DXMT adapter"): Unreal's crash reporter showed a black window. M4 forwards D3D11.
- One DLSS run aborted at startup in Streamline's signature check (Wine's `wintrust` failed its delayed import of
  `imagehlp`); the next passed in full. Seen once, cause open.
- The VM's display (Xvfb) reports no refresh rate, so neither native nor guest DXMT paces sync interval 1: vsync is
  off in the VM in both modes.
- A SMITE 2 run whose game crashed still waits out its 180 s (Unreal's crash reporter holds the launcher), and a game
  that dies before Steam initialises makes the row skip, not fail.

## 21. Direct3D forwarding, M4: D3D11 and D3D10 (2026-10-18)

Plan `docs/superpowers/plans/2026-10-18-vm-d3d-forwarding-m4-d3d11.md`. M2's and M3's forwarding (§19, §20) gains
D3D11 and D3D10: with `MACNEUTRON_VM_D3D=forward`, a process's `d3d11.dll` and `d3d10core.dll` are stand-ins too, so its
D3D11 is native DXMT's on the host, as its DXGI and D3D12 are, and the stand-in DXGI no longer breaks a forwarded
process's D3D11 (§20's "Not a DXMT adapter"). Wine's `d3d10.dll` (effects, state blocks) stays the guest's, on the
forwarded `d3d10core`. DXMT's D3D11 and D3D10 test programs through forwarding match macOS.

What M4 built:
- The model (spec §4.2): `d3d10.h`, `d3d10_1.h` and `d3d11.h` to `d3d11_4.h` after D3D12's and DXGI's headers, and
  DXMT's `IMTLD3D11ContextExt` to `Ext2` declared in `rules.txt`: 207 interfaces (the answers' masks grew to 256 bits)
  and 928 calls, 528 of them D3D11's and D3D10's. `ID3D10Multithread` is `ID3D11Multithread` (one IID and vtable).
  The video and content-protection interfaces, the tiled-resource methods and shared handles are refused.
- Objects at other pointers. D3D11's device answers its DXGI device, D3D10 device and multithread lock, a resource its
  D3D10 counterpart and DXGI resource, a view, state, shader or query its D3D10 one, each at another C++ subobject:
  `companion` rules each way. A companion's `QueryInterface` (op 4) now answers its whole interfaces block, as a
  creation does, so a companion answers its own family and an object the guest already has comes back as its proxy;
  a companion's `IUnknown` is its root's. Getters (`PSGetShaderResources` and the rest) return objects as the handles
  the guest knows them by.
- Data. D3D11 passes no sizes for initial data and `UpdateSubresource`: the guest packs each subresource's bytes into
  one region (`guest/formats.cpp`: blocks, pitches, boxes, from the resource's description), and the host rebuilds
  the subresource array.
- Threads. A D3D11 context's and a D3D10 device's methods, as a command list's (§20), go as a submit when another
  thread called that object last; `ID3D11Multithread::Enter` and `Leave` are a lock in the guest (a host lock would be
  a vCPU thread's).
- Fences and pacing (spec §4.7). D3D11's fences are D3D12's design: a mirror slot, its value read in the guest, its
  events relays. A D3D11 swapchain (`CreateSwapChain` or `D3D11CreateDeviceAndSwapChain`, now a wrap) is paced in the
  guest on its device's immediate context, at its device's latency (`IDXGIDevice1::SetMaximumFrameLatency`, a wrap;
  DXGI's 3); fork 0088 makes native DXMT's D3D11 present not wait with a VM host's hooks. D3D11 presents on DXMT's
  encoder thread after its `Present` call: the host counts the frame at the call and probes it there.
- Maps, in two layers (spec §4.10). Every map's memory, a rename included, comes from the guest's arenas (a new rule
  word, `arena`): a resource's creation names its allocation, and the host first checks that an arena has a piece
  that size, asking for one otherwise, since a rename into host memory can't be taken back. A map that would wait
  goes with `DO_NOT_WAIT`, and while the host answers still drawing the context signals a fence of its own (DXMT's
  `Signal` flushes; its `DO_NOT_WAIT` map doesn't, a DXMT finding of Task 1) and the guest waits for it. On top, fork
  0089's `IMTLD3D11ContextExt2` lends the immediate context's dynamic resources' memory to the guest in pieces (one
  suballocation each, 8 at a time, doubling to 64 while a frame empties them): `WRITE_DISCARD` and
  `WRITE_NO_OVERWRITE` are answered in the guest, and `Unmap` makes the piece current with a queued `UsePiece`.
  `d3d11_dynamic` with 30 frames of 1,000 discards maps 40,181 times in the guest and 81 on the host (the loopback,
  `tests/pieces.sh`); in the VM, 14,128 maps and 104 on the host. Deferred contexts, linear dynamic textures and
  readbacks map on the host.
- DXMT's `UNIMPLEMENTED` throws natively (fork 0087), as `IMPLEMENT_ME` does (0084): the call fails and the VM host
  goes on.

Fork patches 0087 to 0090; the series applies on the pin, and the macOS runtime's build (`wine-arm64/build.sh`) and
`dxmt/check.sh` take 0087-0089 (one check failed in one of its two runs, D3D12's `atomic64typed` on macOS, which M4
doesn't touch); 0090 came with the final review.

Faults the gate found that the plan's tests didn't; each now has a test that failed first:
- The generator passed a guest's event `HANDLE` to native DXMT, which took it for its own: `ID3D11Fence::
  SetEventOnCompletion` crashed the VM host (the loopback shares an address space, so it passed there). A generated
  stub now passes no guest `HANDLE` (own code relays events: the fence's, `Flush1`'s); the content-protection event,
  a DXMT assert, is refused (`test_gen`, `dispatch-test`).
- The derived rules gave single objects out a count (`CreateInputLayout`, the depth-stencil view getters): the guest
  would have written that many pointers into one.
- At a context's end the handle table released what the guest kept in index order, the device first, and a UAV's
  counter then used the device's freed pool: the VM host crashed (`Counter::decRef`) or its heap broke and a later
  free failed. Native DXMT's D3D11 children use their device's pools without holding it; the table now releases a
  context's last objects newest first (`handles-test`; the four rows that crashed in one boot pass).
- `IDXGIResource` was refused whole (D3D12's resources have none in DXMT); D3D11's have one (`d3d11_api`).

The final review (one fresh reviewer over the branch, the guest taken as untrusted) found five more, each now pinned
by a test that failed first:
- A getter's objects went into the guest's memory, and the host read them back as pointers to release: another vCPU
  could have rewritten them. DXMT now writes them into host memory, and the stub copies the handles out (`test_gen`).
- Initial data and updates were checked against the guest's own size words, not what DXMT reads: a 64 KiB staging
  buffer from a 16-byte region had DXMT read past it. The host checks its own copy of the description and box, every
  subresource and each one's bytes by its pitches (`guest/formats.cpp` is the host's too; `dispatch-test`).
- A dynamic buffer discarded in a command list, then mapped on the immediate context, was answered from the replaced
  memory: an executed list's discards end the guest's current piece (`d3d11_deferred`'s mixed line).
- The immediate context didn't answer `ID3D11Multithread` (`d3d11_api`'s ctxmt line); `Enter` and `Leave` are one
  guest lock for the process.
- A process that ended mid-load left native DXMT compiling for it, and the release of its shaders and pipelines
  freed their tasks under the compile threads: the VM host crashed. Fork 0090 exports `MTLD3D11WaitIdle`, which the
  host calls at each context's end (the smoke now runs three such processes first, and the host outlives them).

DXMT's own limits the programs met: no geometry-shader stream output nor `DrawAuto`, its emulated vertex stream
output wrote nothing, its translator refuses Wine's compiled hull shader, Wine's compiler has no
`AppendStructuredBuffer`, and its `DO_NOT_WAIT` map never flushes, so a game polling one without a `Flush` hangs in
every mode. `d3d11_pipeline` checks those stages' creations only.

The gate's rows (D3D11 and D3D10), each against one macOS run by `compare.py`'s rule: natively
(`eac-vm/dxmt-native/gate.sh`), through the loopback (`eac-vm/d3d/loopback-gate.sh`), in guest mode
(`eac-vm/dxmt/gate.sh`) and forwarded in the VM (`eac-vm/d3d/gate.sh`). All pass every way; the forwarded run's 40 rows
(the 28 D3D12 ones too) show no host error, and coverage over its traces is 255 methods, every one forwarded or
answered in the guest by design (the dynamic maps). Seconds, one run each (macOS: the run each side compared with):

| Program | macOS | native | loopback | VM, guest DXMT | VM, forwarded |
|---|---|---|---|---|---|
| d3d10_core | 6.80 | 0.05 | 0.23 | 0.19 | 0.73 |
| d3d10_wine | 5.74 / 8.80 | | | 1.67 | 1.66 |
| d3d11_api | 3.72 | 0.05 | 0.10 | 1.09 | 1.05 |
| d3d11_compute | 3.73 | 0.05 | 0.12 | 1.07 | 1.06 |
| d3d11_deferred | 3.73 | 0.05 | 0.11 | 1.08 | 1.05 |
| d3d11_dynamic | 3.87 | 0.17 | 0.23 | 1.34 | 1.23 |
| d3d11_getters | 3.72 | 0.04 | 0.09 | 1.06 | 1.04 |
| d3d11_pipeline | 3.72 | 0.04 | 0.09 | 1.07 | 1.07 |
| d3d11_query | 3.71 | 0.05 | 0.11 | 1.07 | 1.04 |
| d3d11_render | 3.74 | 0.05 | 0.11 | 1.08 | 1.06 |
| d3d11_transfer | 3.73 | 0.07 | 0.12 | 1.09 | 1.07 |
| d3d11_vsia | 5.67 | 0.04 | 0.10 | 1.66 | 1.67 |

As in §19 the VM's times are Wine's start, not D3D's (a row's first program pays wineboot's wineserver: `d3d10_core`,
and `d3d10_wine` in its own boot). `present-gate.sh forward` now runs `present_loop`, the 4c gate's D3D11 game loop,
forwarded: 600 drawables counted, probed as in guest mode, 8.30 ms a frame against macOS's 8.29, and no present
holding its vCPU on the host (d3d-smoke.sh's rule). The smoke's new `d3d11latency` line (60 GPU-bound D3D11
presents, the fence never more than 3 behind) passes in both modes. Its slow-present rule failed for a while in
Task 9 (68-69 presents over 4 ms, the longest 9-14 ms, while the Mac sat idle on its lock screen) and passes again
after the final review's fixes on a fresh smoke prefix (one present over 4 ms, 4.6); which of the idle display, the
old prefix or the mid-load crashes' damage made them slow isn't pinned.

Open:
- A D3D11 game through forwarding (none ran in M4), and D3D11's MetalFX: `IMTLD3D11ContextExt`'s and `Ext1`'s own
  methods (`TemporalUpscale`, DXMT's `nvngx` for D3D11 DLSS) aren't forwarded yet.
- The present counts for D3D11 go to the context of the latest `Present` call: two processes presenting D3D11 at once
  would mix their probes and drawables.
- The arenas' room check asks for a whole allocation a piece (a suballocated piece needs far less): `d3d11_dynamic`'s
  arenas reach 640 MiB, all of it its one-frame stress section's.

## 22. The frame rate, M5 revisited, the deferred findings and the upstream merges (2026-10-18)

The maintainer played SMITE 2 forwarded (MacNeutron-next.app, the dev VM folder) and found the frame rate low, with the
game's frame cap off. Every number here is `bench-smite2.sh` (SMITE 2's lobby in the dev VM, DLSS, 2 minutes a run
since this day: the measured minute plus one) unless it says otherwise; one run each.

**Where the forwarded frame went.** The first lobby run, forwarded: 74.1 fps, GPU 82.9% busy, 4.52 cores. A host
`sample` put 1.7 cores of the vCPUs' exit time in waits on the executor's locks (the global `submit_lock` and other
rings' drain locks: `eac-vm/metal/host/executor.c`) against 0.47 cores of Direct3D work. The submits were mostly D3D12
descriptor writes: every `Create*View`, `CreateSampler` and `CopyDescriptors*` was one (M2's final review, M3 Task 5),
about 765 a frame in M3's DLSS run, each running every other thread's published records first.

- **Descriptor writes** (d1d786a). DXMT reads a shader-visible heap's slots only on the GPU: descriptor tables keep
  handles, and `ExecuteCommandLists`, itself a submit, runs every ring first (a research pass over
  `d3d12_descriptor_heap.cpp`, `d3d12_command_list.cpp`). A write into a shader-visible heap needs no submit; one into a
  CPU-only, RTV or DSV heap does (clears, a render pass's first draw and copies read those when their records run), as
  does a copy from a shader-visible heap. The guest classifies a write by its destination: each heap's CPU handles,
  from its start and description, asked once at `GetCPUDescriptorHandleForHeapStart`. `MN_D3D_DESCRIPTORS` picks the
  fast way: `sync` (the default: a call, which can't reorder two threads' writes to one slot) or `async` (queued).
  Lock waits fell to 0.74 cores; the lobby to 77.3 fps queued and 77.1-78.3 as calls. `descriptors.sh` pins the
  split: of its 226 writes, 84 submits through the loopback (229 before).
- **Idle rings** (257e0fd): a queue operation no longer takes a ring whose every published record has run. Within
  noise (76.7 fps).
- **The present's time** (d34499a, with `MACNEUTRON_VM_D3D_TRACE=1`): about 2.2 ms a frame waiting for the display,
  0.1 ms in the host's `Present`. Without the display wait (an experiment, removed) the lobby ran 76.4 fps: it is
  three drawables' back-pressure, not the limit.
- **Guest mode in the same lobby**: 78.2 fps, against forwarded 78.0 (an earlier 88.6 didn't repeat). A Metal System
  Trace (`MN_BENCH_GPU_TRACE`, read by `dxmt/tools/gpu-trace.py`) gave 10.9 ms of GPU work a frame in guest mode and
  11.1 forwarded, the GPU 87-90% busy, its idle time 0.92 ms between encoders, 0.43 between command buffers and 0.16
  waiting for the CPU a frame. Forwarding no longer holds the lobby: its GPU work does.
- **Practice, at the maintainer's settings** (2560x1440, DLSS, uncapped; `MACNEUTRON_PERF=1`, the maintainer
  playing): macOS 114.6 fps, forwarded 113.5 (99%), both just under the display's 120 Hz.

**The GPU's work** (practice, 6.1 ms a frame, 1080p output, 960x540 internal): vertex 2.6 ms, fragment 1.7, compute
2.0. The base pass is one merged Metal pass (DXMT_STATS's pass numbers shift between frames: summing by label spread it
over six, read one frame's encoder sequence instead): vertex 1.91 ms, fragment 0.33. MetalFX (DLSS) 0.6 ms. Ruled out:
- **Vertex fetch**: a Metal micro-benchmark (4M vertices, a UE static-mesh layout, the vertex stage only) runs Metal's
  own fetch (`stage_in`, a vertex descriptor) in 0.55 ms and DXMT's pulling (`pull_vertex_input`'s table, address
  arithmetic, loads and decodes) in 0.55 ms.
- **Concurrent dispatch**: D3D12 compute encoders are serial (`computeCommandEncoder(false)`; D3D11's are concurrent).
  An experiment dispatching concurrently, with a memory barrier wherever a barrier call fell between two dispatches,
  passed the native gate and left the lobby at 11.35 ms of GPU a frame either way; reverted.
Still open from the GPU efficiency spec's D3DMetal comparison (2026-10-02): compute 1.4-3x slower per dispatch, the
base pass's vertex +0.4 ms.

**M5 revisited** (spec §8's gate; clean build b4906d7, the lobby). Forwarded: 4.41 cores, the host's overheads (wake-ups
0.66, spinning 0.00, sound 0.00, other 0.43, of which Metal's command-buffer submits 0.44) 1.10 cores against the gate's
2.0, wineserver 0.11 against 0.2, 76.3 fps. Guest mode: 5.37 cores, overheads 1.61 (the executor's consumers spin 1.10),
wineserver 0.10, 76.9 fps. Forwarding makes the same frames for about a core less. The gate's own run is a match at
M0's settings (1440p, DLSS Performance): the maintainer's practice run above passes its frame rate (113.5 against
108.6); its CPU numbers are the maintainer's next run. The trace's exits a frame showed what M2's final review
deferred to M5, immutable answers asked each frame: the guest now keeps them (`answers.sh`: 412 exits to 15) —
`GetDeviceRemovedReason` (DXMT's is S_OK; the host's once the forwarding answers a call DXGI_ERROR_DEVICE_REMOVED),
a queue's `GetTimestampFrequency`, a resource's `GetDesc`, the adapter's `QueryVideoMemoryInfo` for 100 ms (forgotten at
`SetVideoMemoryReservation`).

**The deferred findings** of M0-M5, M1-M4 and the VM setup's plans: about 70, each checked against the code first.
Most were fixed by later work; the rest are fixed (each behaviour change with a test that failed first: the D3D10 map
and `Enter` cases in `threads11.cpp`, `formats-test`, `dispatch-test`'s overflow, `MicroVMTests`' native translator key,
AppModelTests' real VM folder, `perf_test.py`, `executor-test`'s spin) or ruled with their cost. Found on the way: five
AppModel tests had failed since M3 Task 9 (the tests' fake VM tools had no `dxmt/`). Fork patches 0091 (the pieces'
interface native only) and 0092 (the native stand-ins' timed waits on the uptime clock; exclusive moves on exFAT and
SMB). Waiting for the maintainer: the App Nap activity keeps a hung VM's Mac awake (deliberate under a game), and a
milliseconds-wide `share/bin` window after an image swap.

**Upstream, merged into the experiment's trees** (fetched 2026-10-18 22:55): Wine master at its 11.20 release (139
commits; `ea288a70`, built as `wine-11.19-383-gea288a70`), DXMT upstream main (one commit, an nvngx fix; `1343d34`)
and FEX main (11 commits, rpmalloc's submodule moved with them; `b65d59e`). Proton's `proton_11.0` (lsteamclient) and
libkrun's main had nothing new (libkrun's main is still the pin). One conflict, Wine's `dlls/ntdll/unix/virtual.c`:
upstream merged its reserved-areas and free-ranges arrays (`1cae1f53`) and added a top-down area (`686ccbff`); the
merge keeps our soft-pagezero reservation (in mem areas since `0b13586e`) beside upstream's new area, and drops the
`virtual_ranges` debug channel nothing uses now. On the merged trees: the Swift suite (422), `make dxmt-check` (both
lanes), `make smoke`, `make bridge-check`, the unit tests, the loopback gate (30), the native gate (30), `vm-setup`,
`turn`, `modes`, `tpm-host`, `audio-host`, the guest-mode DXMT gate (40), the forwarded D3D gate (40) and the present
gate all pass. `d3d-smoke.sh` passes every check but its slow-present rule, 68 presents over 4 ms (the longest 8.9),
the same count as in M4 Task 9 and again while the Mac sat idle past midnight; it waits for a run with the maintainer at
the Mac. SMITE 2's lobby: forwarded 76.5 fps at 4.27 cores (overheads 1.08, wineserver 0.10), guest mode 78.7 at 5.06.

`bridge/check.sh` now runs with `MACNEUTRON_VM=0`: since every game runs in the microVM, its launcher rows had run their
app-0 game in the VM folder MicroVM finds, the user's own by default (its state and share were made there tonight, then
removed).
