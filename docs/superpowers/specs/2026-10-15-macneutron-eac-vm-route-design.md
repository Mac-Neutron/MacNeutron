# MacNeutron EAC mode: the microVM route

Status: design approved 2026-10-15. Benchmarks are in `docs/research/2026-10-15-vm-route/`. This version replaces the
first draft (a Virtualization.framework VM running Fedora Cloud); section 3 says what changed and why.

## 1. Goal

A Windows game whose Easy Anti-Cheat works under Proton on Linux works on MacNeutron, through EAC's own Linux/Proton
mode, unmodified. The guest is as small as we can make it and shared by every such game, and the boundary between the
guest and macOS is as thin as we can make it, so the VM costs little RAM and CPU.

Success for this spec: SMITE 2 reaches `Launcherfinishedwith:301` (EAC loaded in game) and plays online at the
frame rate MacNeutron reaches today, within 5%.

## 2. Principles

These follow the paper "Anti-Cheat Compatibility on Apple Silicon" (NZ-Linix and Serrano, 2026) and the
eac-arm64-emulation-poc project, and they are not negotiable:

- **Fidelity, not evasion.** Every layer behaves like the environment it claims to be: an x86-64 Linux process tree
  under FEX, a real Linux kernel in a VM, and Valve's Proton and EAC runtime. The only Proton files we replace are the
  graphics DLLs it installs for DXVK and vkd3d-proton.
- **Nothing hides that this is a VM or a translator.** No hidden hypervisor, no spoofed hardware identity, DMI/SMBIOS
  or CPUID, no null client.
- **EAC and the game's files stay stock.** No hooks, shims, replay or offline overrides of EAC. Graphics DLLs go where
  Proton puts DXVK (the prefix's `system32`), never into the game folder. A rejected title stays rejected.
- **Nothing ptrace-based touches a protected process.** Diagnostics use in-process tools (FEX's sampler) only.
- **A launch failure that looks like detection is treated as a fidelity defect first**, and fixed in the emulator.

## 3. Decisions (2026-10-15)

| Question | Decision | Why |
|---|---|---|
| VM host | Our fork of libkrun (Apache-2.0), linked into the MacNeutron host process, on Hypervisor.framework | A microVM with in-process devices: no XPC hop per device access, the fewest emulated devices, free-page reporting. Virtualization.framework (the first draft) exposes no vCPU registers, so it cannot give a stock kernel hardware TSO (next row). |
| Memory ordering | The host sets `ACTLR_EL1.EnTSO` on every vCPU (`hv_vcpu_set_sys_reg(HV_SYS_REG_ACTLR_EL1)`, public since macOS 15). The whole VM runs TSO. FEX is told and emits no barriers. | No signed kernel has the TSO switch: Apple's and Asahi's `PR_SET_MEM_MODEL` series never merged; Fedora 44 (7.2.10) and Rawhide (7.3-rc7) lack it, and so do the other kernels Epic accepts. A stock arm64 kernel never writes `ACTLR_EL1`, so the host's setting holds. Proven on this M5 Pro (2026-10-11 spike: 0 litmus violations in 200 M). |
| Kernel | Fedora's signed `kernel-core` and its signed modules, unmodified, 4 KB pages | Fedora is on Epic's well-known-key list, ships among the newest kernels on that list, and is where Asahi's TSO series would most likely land if it ever does. |
| Guest system | No distribution install. Two read-only EROFS images built from pinned Fedora packages, shared by every game, plus one small writable state disk | The first draft's Fedora Cloud install took 11 GB and a full boot. The kernel signature is what Epic checks; the root filesystem is ours to keep small. |
| Graphics | DXMT, with its Metal calls forwarded to the host over a virtio-gpu context type of our own | Fedora's in-tree, signed `virtio_gpu` driver carries it (as Venus and gfxstream do), so the guest needs no module of ours. D3D goes straight to Metal: no Vulkan, MoltenVK or SPIR-V layer, and shader compilation stays on the host with our existing caches. |
| Secure Boot | Not enforced in milestones 1-5. The kernel boots with `lockdown=integrity module.sig_enforce=1`. Milestone 6 is a spike: EDK2 with Secure Boot and Microsoft's and Fedora's keys built in, booting shim, GRUB and the same kernel. Fallback: Virtualization.framework, which has Secure Boot, with software TSO. | Proton EAC today (v1) does not require Secure Boot: it runs on the Steam Deck, which boots without it. libkrun has no UEFI variable store, so keys must be built into the firmware. |
| EAC Linux v2 | Out of scope; a later phase | Announced 2026-10-14. It adds TPM 2.0, measured boot, an IOMMU without passthrough, kexec disabled and the BPF LSM. Neither libkrun nor Virtualization.framework gives a guest a TPM or a virtual IOMMU. Nothing in this design rules them out. |

## 4. Architecture

```
macOS: MacNeutron
  MacNeutronVM (Swift) + libkrun fork (Rust, in process)
    Hypervisor.framework vCPUs, EnTSO set on each; hv_gic
    virtio-blk ×3   base.erofs, x86-64-rootfs.erofs (read-only, shared), state.img (writable)
    virtio-fs       the Mac's Steam library (game files), per game
    virtio-gpu      2D scanout (Steam's UI) + context type "dxmt" -> Metal executor (winemetal's Metal code)
    virtio-snd      -> CoreAudio
    virtio-input    keyboard and mouse from the MacNeutron window
    virtio-console  agent port (launch, focus, clock, gamepads)
    virtio-net      -> gvproxy (the Mac's own sockets)
    virtio-balloon  free-page reporting -> pages handed back to macOS
    virtio-rng
Linux guest (arm64, Fedora signed kernel, 4 KB pages, whole-VM TSO)
  systemd, cage + Xwayland (Steam's UI, Wine's windows), ALSA
  FEX (ours + eac-vm/fex-patches) as the binfmt interpreter for x86 and x86-64
  x86-64: Steam (Linux) -> Steam Linux Runtime -> Proton-MacNeutron -> game + EAC bootstrapper + EAC client
    Proton-MacNeutron = Valve's Proton from the guest's Steam, DXMT's DLLs in DXVK's and vkd3d-proton's place
    DXMT (x86-64 PE) -> winemetal.so (x86-64 ELF) -> /dev/dri/renderD128 (context "dxmt")
```

## 5. Components

Each unit has one job and a narrow interface, so it can be built and tested on its own.

### 5.1 MacNeutronVM (host)

- **What:** a Swift executable (`eac-vm/host/`) that configures and runs the VM through the libkrun fork's C API, owns
  the game window, and bridges input, gamepads and the agent. It replaces `eac-vm/host/vmhost.swift` (the
  Virtualization.framework host, kept for the Secure Boot fallback and for benchmarks).
- **libkrun fork** (`eac-vm/libkrun/`: a pinned upstream commit plus our patches, like `wine-arm64/patches/`):
  - `EnTSO` on every vCPU at creation; read back once and logged.
  - Direct boot of Fedora's `vmlinuz` (an EFI zboot image) with our initramfs and command line, through libkrun's
    external-kernel loader (we add the zboot format if it lacks it).
  - The `dxmt` context type in its virtio-gpu device (rutabaga), section 5.6.
  - virtio-snd with a CoreAudio backend (upstream libkrun has no sound device).
  - Blob memory aligned to 16 KB, the host page size, so Metal can wrap it without copies.
- **Entitlements:** `com.apple.security.hypervisor`, signed with our Developer ID. No virtualization entitlement and
  no restricted entitlement.
- **Sizing:** vCPUs default to the host's performance cores plus half the efficiency cores. The benchmarks showed
  ten spinning threads on eight vCPUs running 3.5x slower, so a game needs at least as many vCPUs as busy threads.
  Guest RAM defaults to the game's stated requirement plus 2 GB; free-page reporting returns what the guest frees.
- **vCPU and device threads** run at the user-interactive QoS class, so macOS keeps them on performance cores.

### 5.2 Boot

- The host loads Fedora's signed `vmlinuz` directly (no firmware in milestones 1-5), with an initramfs we build with
  dracut from the same kernel's signed modules (virtio, EROFS, overlayfs, virtiofs).
- Command line: `lockdown=integrity module.sig_enforce=1 ro rootfstype=erofs` plus our root and overlay arguments.
- The initramfs mounts `base.erofs` as `/`, `x86-64-rootfs.erofs` for FEX, and `state.img` for `/home` and `/var`, with
  an overlay for `/etc`.

### 5.3 Guest images

All three are built by `eac-vm/image/build.sh` in a Fedora aarch64 builder (today's Virtualization.framework VM) from
pinned package versions, with `dnf --installroot`. Nothing is downloaded on the Mac at run time beyond what Steam itself
fetches inside the guest.

| Image | Contents | Shared |
|---|---|---|
| `base.erofs` (arm64) | systemd, glibc, kernel modules, FEX and its binfmt units, cage, Xwayland, Mesa llvmpipe (Steam's UI), alsa-lib, the agent, CA bundle | Read-only, all games |
| `x86-64-rootfs.erofs` | The x86-64 libraries Steam (Linux) needs outside its runtime, as FEX's RootFS | Read-only, all games |
| `state.img` | The Steam client, its compat tools (Steam Linux Runtime, Proton, Proton EasyAntiCheat Runtime, Proton-MacNeutron), prefixes, logs | Writable, one per user |

Size targets are set in the plan from the first build and become gates.

### 5.4 FEX

- Our FEX (`macneutron` branch) with `eac-vm/fex-patches/` (the paper's fidelity series), built for Linux arm64.
- New option `HostTSO` (`FEX_HOSTTSO`): the VM already runs TSO, so FEX calls `SetHardwareTSOSupport(true)` without
  `PR_SET_MEM_MODEL`. The image's FEX config sets it only after a boot-time litmus check (section 7) passes.
- Registered through binfmt_misc for x86 and x86-64, so the whole tree (Steam, the runtime, Proton, Wine, the game,
  the bootstrapper and the injected EAC client) runs under it.

### 5.5 Proton-MacNeutron

- A compat tool the agent creates in the guest from the Proton that the guest's Steam installed: a copy of Valve's
  Proton with DXMT's x86-64 DLLs in the files Proton installs for DXVK (`d3d11`, `dxgi`, `d3d10core`) and vkd3d-proton
  (`d3d12`, `d3d12core`), plus `winemetal.dll` and `winemetal.so`. Nothing else in Proton changes, and we
  redistribute none of it.
- Launch environment: Proton's own `PROTON_EAC_RUNTIME` and `EAC_LAUNCHERDIR`, as the paper's appendix B.
- The game folder is never written.

### 5.6 Metal transport

- **Boundary:** winemetal's 198 Unix calls (`__wine_unix_call_funcs` in `src/winemetal/unix/winemetal_unix.c`,
  including airconv's shader compiles). The guest `winemetal.so` implements the same table by encoding each call into a
  ring; the host decodes it and runs today's Metal code unchanged.
- **Ring:** a blob resource mapped into the guest and the host (19.8 ns per 64-byte command in the benchmarks). A host
  thread consumes it, spinning while frames arrive and parking after 2 ms idle; the guest wakes it with one
  virtio-gpu submit.
- **Calls fall into four kinds:**
  - Fire-and-forget (encoders, commits, releases): queued.
  - Object creation: the guest picks the handle, so creation is queued too.
  - Queries (`MTLCommandBuffer_status`, `MTLSharedEvent_signaledValue`): read from values the host mirrors into shared
    memory.
  - Only calls that return data (`MTLTexture_getBytes`, shader-compile results, display queries) wait for the host.
- **Memory:** an `MTLBuffer`'s contents are a blob both sides map (`newBufferWithBytesNoCopy`), so uploads need no
  copies. Texture uploads go through the ring or a staging blob.
- **Presentation:** `CreateMetalViewFromHWND` creates the layer in the MacNeutron window. Frames go straight to the
  host's `CAMetalLayer`, never through the guest compositor.
- **Shaders and pipelines** compile on the host, so our pipeline cache and replay work as they do today, and stay
  outside the guest where EAC never sees them.

### 5.7 Devices

| Need | Device | Host side |
|---|---|---|
| Steam's UI, Wine's windows | virtio-gpu 2D scanout | Shown in the MacNeutron window when no game layer is up |
| Game frames | context `dxmt` | `CAMetalLayer` in the same window |
| Keyboard, mouse | virtio-input | MacNeutron window events; relative motion while the game has the mouse |
| Gamepads | agent port -> `uhid` in the guest | GameController framework, real vendor and product IDs |
| Audio | virtio-snd | CoreAudio |
| Network | virtio-net | gvproxy (the Mac's sockets, no network extension) |
| Game files | virtio-fs | The game's folder in the Mac's Steam library |
| Clock after sleep | agent port | Wall time sent on resume |

### 5.8 Agent

A systemd service in `base.erofs` on the virtio-console port. It creates Proton-MacNeutron when Proton updates,
launches games (`steam -applaunch <appid>`), reports progress and focus, applies the host's time zone and wall clock,
and creates gamepads.

## 6. Launch flow

1. The user starts an EAC game from MacNeutron. MacNeutron decides on EAC mode when the game's Proton EAC support is
   known to work.
2. MacNeutronVM boots the guest (cold boot target set in the plan), with the game's folder shared.
3. The agent starts Steam (Linux) if it isn't running and asks it to launch the game with Proton-MacNeutron.
4. Steam runs the Steam Linux Runtime, Proton and the EAC bootstrapper. The bootstrapper injects EAC's client through
   ptrace, which FEX emulates.
5. DXMT's calls reach the host. The window shows the game's layer.
6. When the game exits, the guest stays up for 5 minutes for a relaunch, then shuts down.

## 7. Errors

- **TSO check:** at boot a 200 ms message-passing litmus test runs. Any violation means the host's `EnTSO` is not in
  effect: FEX keeps software TSO, and the agent reports it to the host log. It is never silently wrong.
- **EAC launcher codes** are read from Proton's log and shown by MacNeutron. 206 or 210 are treated as fidelity
  defects (section 2).
- **Guest hang:** no agent heartbeat for 10 s marks the guest unresponsive, and MacNeutron offers to stop it.
- **Lost transport:** a broken ring or a host-side Metal error ends the game with a message. The ring never drops
  commands silently.
- **Secure Boot or signature failures** stop the boot with the reason shown. The guest never falls back to unsigned
  modules.

## 8. Milestones and gates

1. **FEX fidelity on our FEX.** `eac-tests/run-all.sh` gives all 19 expected results in a guest. Today
   `ptrace-inject` fails its register round trips on our FEX and passes on the PoC's.
2. **The microVM.** MacNeutronVM boots Fedora's signed kernel from our images. Verified when:
   - `uname -r` is the pinned Fedora release, `/sys/kernel/security/lockdown` shows `[integrity]`, and
     `/sys/module/module/parameters/sig_enforce` is `Y`;
   - the page size is 4096;
   - the litmus test shows 0 violations with no guest module, and FEX reports hardware TSO;
   - the game-shaped frame loop (`harness/gameframe.cpp`) runs within 5% of 2.14 ms;
   - freeing 4 GB in the guest lowers the host process footprint by at least 3 GB within 30 s.
3. **EAC launches.** Steam, the runtime, Proton and EAC in the guest, rendering in software. Pass: SMITE 2's
   `Launcherfinishedwith:301`.
4. **Metal transport.** DXMT's check suite passes in the guest as it does on macOS, and SMITE 2 renders through
   the host.
5. **Devices and product.** Audio, input, gamepads, the shared library, launch from MacNeutron, sizing. Pass: an
   online SMITE 2 match within 5% of today's frame rate, and key-to-presented-frame latency within 5 ms of today's.
6. **Secure Boot spike.** EDK2 with Secure Boot and built-in keys boots shim, GRUB and the same kernel in the
   libkrun fork. If it fails, the fallback in section 3 is documented and measured.

## 9. Measured costs (from the benchmarks)

| | microVM route | MacNeutron today |
|---|---|---|
| Game-shaped frame loop (CPU) | 2.14 ms (FEX + hardware TSO) | 4.94 ms (3.23 without CRT crossings) |
| Code that needs no ordering | 3-6% slower in TSO mode | |
| Bulk copies | up to 30% slower in TSO mode | |
| Random memory access | about 10% slower (nested paging) | |
| Shared-memory ring | 19.8 ns per command, 114 ns fence round trip | |
| SMITE 2 forwarding | about 2,770 calls and 5,100 commands per frame, about 0.16 ms | |

The doorbell figure in the first draft (37 µs) was Virtualization.framework's out-of-process device path. In
libkrun a guest notification is one vCPU exit handled in process; milestone 2 measures it.

## 10. Open questions

- **Two Steam clients, one account.** The guest runs its own Steam (Linux), logged into the user's account. If Mac
  Steam launches the game while the guest's Steam also runs it, Steam will likely refuse ("playing on another
  computer"). Milestone 5 decides whether EAC games launch only from MacNeutron.
- **Library sharing.** Both clients download the same Windows depot into the shared folder, but each keeps its own
  manifest. Milestone 5 measures what happens when one updates the game and the other sees the new files.
- **Steam's sign-in** happens once in the guest's UI (or its QR code). It stays on `state.img`.

## 11. Out of scope

- EAC Linux v2 (TPM 2.0, measured boot, IOMMU, kexec, BPF LSM).
- BattlEye's Proton path.
- Titles whose developers have not enabled EAC on Proton, and kernel-mode anti-cheats.
- Rosetta for Linux: it cannot trace translated binaries, which EAC's injection needs.
