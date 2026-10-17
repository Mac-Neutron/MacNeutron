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

**Open:**
- This branch's build trees (Wine, FEX, DXMT, lsteamclient) are the upstream-master experiment's: its merges are
  commits in the trees, past the pins, not patch series. A release, rehearsed or not, refuses them (check_trees), so a
  release from this branch first needs its trees re-pinned and re-exported, or the stable series' trees; the
  maintainer's call.
- A release that rebuilds the images changes the parts' bytes, so every Mac downloads again; reusing an unchanged VM's
  parts is the maintainer's call.
- The first run on a clean macOS account is the maintainer's (plan 4, task 3).
