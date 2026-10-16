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
| 2 | TPM 2.0 | None: libkrun has no TPM device | A CRB TPM in our libkrun fork (Fedora's kernel has `TCG_CRB=y`), backed by a software TPM on the host (libtpms) with per-VM state. Its endorsement key is certified by MacNeutron as a virtual TPM, never as a vendor's chip |
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
