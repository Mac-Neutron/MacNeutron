# MacNeutron EAC mode: the VM route

Status: in progress. Started 2026-10-15. Benchmarks are in `docs/research/2026-10-15-vm-route/`.

## Goal

A Windows game whose Easy Anti-Cheat works under Proton on Linux should work on MacNeutron, using EAC's own
Linux/Proton mode, unmodified.

## Principles

These follow the paper "Anti-Cheat Compatibility on Apple Silicon" and the eac-arm64-emulation-poc project, and they
are not negotiable:

- **Fidelity, not evasion.** Every layer behaves like the environment it claims to be:
  - an x86-64 Linux process tree, under FEX;
  - a Linux kernel, real, in a VM;
  - Proton, stock.
- **Nothing hides that this is a VM or a translator.** We don't hide the hypervisor, spoof the hardware identity or
  DMI/SMBIOS, use a null client, or mask CPUID.
- **EAC and the game's files stay stock.**
  - No hooks, shims, replay or offline overrides of EAC.
  - Graphics DLLs go where Proton puts DXVK (`system32`), never into the game folder.
  - A rejected title stays rejected: we don't probe for a workaround.
- **Nothing ptrace-based touches a protected process.** No debuggers or profilers attached to it. Diagnostics use
  in-process tools (FEX's sampler) only.
- **A launch failure that looks like detection is treated as a fidelity defect first.** It is fixed in the emulator,
  never masked.

## Architecture

```
macOS host (MacNeutron)
  MacNeutronVM: Virtualization.framework VM
    EFI boot, NAT network, virtiofs shares, vsock agent channel, virtio-gpu 2D (Steam UI),
    keyboard/mouse, virtio-snd
    metal transport: a custom virtio device (macOS 27) with a shared memory region and a doorbell
      host side: DXMT's Metal executor (winemetal's Metal half, the same code we ship today)
      presents into a host CAMetalLayer window
Linux guest (arm64, 4 KB pages)
  Fedora's own signed kernel (7.2.10-200.fc44) under Secure Boot: Microsoft's UEFI CA and the shim, signed modules,
  lockdown=integrity. No custom kernel or module (Epic's coming Linux requirements are a legitimately signed kernel
  and modules). Fedora Asahi Remix's kernel, which carries the TSO series, replaces it if it is ever signed.
  FEX 2610 + the fidelity patches (ptrace emulation, signals, seccomp, SMC hot pages, /proc)
  x86-64 user space: Steam (Linux), Steam Linux Runtime, Proton, Proton EasyAntiCheat Runtime (1826330)
  graphics: DXMT's D3D10/11/12 (x86-64 PE, in system32 like DXVK)
    -> winemetal.so (guest), which forwards Metal calls through the transport
```

### Why Virtualization.framework rather than libkrun

- **Hardware TSO works in a VZ guest.** `ACTLR_EL1[1]` is writable. The Asahi patches detect it through `AIDR_EL1[9]`,
  which this VM reports as set. The litmus test goes from 1.09 billion violations to 0.
- **macOS 27 adds custom virtio devices** with shared memory regions and guest-memory access. Our Metal transport needs
  exactly that; libkrun would need virtio-gpu and Venus instead.
- **No Venus or MoltenVK layer.** VZ gives Linux guests 2D graphics only, but we don't need Vulkan. DXMT translates
  D3D straight to Metal and forwards the Metal calls, skipping the SPIR-V -> MSL path and its pipeline stalls the paper
  measured.

### Measured costs (from the benchmarks)

| | VM route | Today |
|---|---|---|
| CPU, game-shaped frame loop | 2.14 ms (FEX + hardware TSO) | 4.94 ms (3.23 without CRT crossings) |
| Shared-memory ring to the host | 19.8 ns/cmd, 114 ns fence round trip | |
| Doorbell (virtqueue round trip) | 37 µs: avoid per frame | |
| SMITE 2 Metal calls per frame | ~2,770 calls and ~5,100 command nodes: ~0.16 ms/frame of forwarding | |

## Milestones

Decisions (2026-10-15):

- **Fedora, signed.** Fedora 44's kernel 7.2.10 and Rawhide's 7.3-rc7 have no `PR_SET_MEM_MODEL`
  (`CONFIG_ARM64_MEMORY_MODEL_CONTROL` is absent; the prctl returns EINVAL), so FEX uses software TSO (LRCPC2).
  That costs 1.5x on sync-heavy CPU work against hardware TSO (benchmarks); GPU-bound games don't see it.
- **Secure Boot.** It works in the VM through macOS 27's `enrollDefaultSecureBootSignatures` and
  `enableSecureBootUsingDefaultPlatformKey`. Our own transport must therefore use an in-tree driver: virtio-gpu with
  a custom context type, as Venus and gfxstream do.

1. **Guest image.** Ubuntu 26.04 arm64 with:
   - kernel 7.2 + the Asahi TSO series;
   - FEX eac-2610.2, built in the guest;
   - an x86-64 rootfs;
   - Steam (Linux).

   Verified when:
   - `PR_SET_MEM_MODEL` succeeds;
   - the litmus test shows 0 violations;
   - `FEXGetConfig` reports hardware TSO.
   Status: done, on Fedora 44 under Secure Boot (`eac-vm/guest/provision.sh`). The original plan was a custom
   kernel with the Asahi TSO series; it was dropped for the signed distribution kernel.
2. **Fidelity tests.** The fork's `eac-tests/run-all.sh` passes in our guest (ptrace-inject, signal-mask, signal-regs,
   seccomp, signal frames, /proc).
   Status: done. All 19 runs give their expected result in our VZ guest (18 PASS; pop-fault is FAIL by design).
3. **EAC launches.** Steam, Proton and the EAC runtime are in the guest, rendering in software at first.
   - Pass: an EAC title's launcher reports `301 Easy Anti-Cheat successfully loaded in-game`.
   - First candidate: SMITE 2, which ships `start_protected_game.exe` and EOS EAC.
4. **Metal transport.** A guest `winemetal.so` that serialises winemetal's unix calls into the shared ring. The host
   executes them with the existing Metal code. In detail:
   - Metal buffers live in shared memory (`newBufferWithBytesNoCopy`), so contents need no copies.
   - Event and status queries read host-mirrored values.
   - Handles are assigned guest-side, so creation is asynchronous.
5. **Presentation and input.**
   - The game's swapchain presents into a host window.
   - The host window's input goes to the guest (uinput through the vsock agent).
6. **Product.**
   - MacNeutron switches a game to EAC mode when Proton EAC applies to it.
   - The guest image is built from pinned, verified downloads.
   - The game library is shared with the Mac.
   - The guest's memory is sized to the game.
7. **Optimisation.** The VM gets at least as many vCPUs as the game has busy threads, plus:
   - FEX's DiskCache;
   - pipeline pre-caching on the host (our existing replay);
   - an adaptive spin-then-park consumer thread.
