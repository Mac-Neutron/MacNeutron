# MacNeutron EAC mode: the microVM route

Status: design approved 2026-10-15; milestones 1 and 2 done the same day (results in
`docs/research/2026-10-15-vm-route/README.md` §4). Benchmarks are in `docs/research/2026-10-15-vm-route/`. This version replaces the
first draft (a Virtualization.framework VM running Fedora Cloud); section 3 says what changed and why. Revised the same
day: the guest runs our Wine instead of Valve's Proton, and the game's Steam calls go to the Mac's own Steam client
(§3, §5.5, §5.9).

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
  under FEX, a real Linux kernel in a VM, a Wine with Proton's EAC loader support, and Valve's EAC runtime as shipped.
  Valve's Proton is the reference: a title that reaches EAC under Proton must reach it under our Wine, and a difference
  is a defect in our Wine.
- **Nothing hides that this is a VM or a translator.** No hidden hypervisor, no spoofed hardware identity, DMI/SMBIOS
  or CPUID, no null client. The game's Steam calls reach a real Steam client (the Mac's); a call the forwarder does not
  carry fails loudly and is never answered in the guest.
- **EAC and the game's files stay stock.** No hooks, shims, replay or offline overrides of EAC. DXMT's DLLs are our
  Wine's builtins in the prefix's `system32`, never in the game folder. A rejected title stays rejected.
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
| Wine | Our Wine (the tree MacNeutron ships on the Mac: upstream master plus our patches) built for x86-64 Linux, plus Proton 11's four EAC changes: the runtime's DLL path for `PROTON_EAC_RUNTIME` (`ntdll/unix/loader.c`), the bridge load-order rule (`ntdll/unix/loadorder.c`), the launcher-process marker (`kernelbase/process.c`) and one per-game DNS workaround (`ws2_32/protocol.c`) | One Wine for both of MacNeutron's paths, and SMITE 2 already runs on it. EAC's Linux mode needs the loader support, not Valve's build. Valve's Proton stays as the reference run. |
| Steam | The Mac's Steam client. lsteamclient's Windows half runs in the guest; its Unix-call table is forwarded over vsock to the host, where MacNeutron's existing macOS Unix half calls Mac Steam's `steamclient.dylib` (§5.9) | One Steam and one sign-in, no "playing on another computer", no Steam client or Steam UI in the guest. The host half is code we already ship and test. |
| Synchronisation | ntsync (`/dev/ntsync`, Fedora's signed module) | Upstream Wine uses it; msync is macOS-only. |
| Steam Linux Runtime | Not used | Our x86-64 image carries Wine's libraries: one container layer fewer. |
| EAC Linux v2 | A later phase; mapped requirement by requirement in the research README §6 (2026-10-16) | Announced 2026-10-14. It adds TPM 2.0, measured boot, an IOMMU without passthrough, kexec disabled and the BPF LSM, and accepts only kernels signed with a well-known key (Fedora's CA is one), which rules out a self-built guest kernel. The guest already has lockdown integrity, kexec disabled at boot, the BPF LSM and Fedora-signed binaries. Neither libkrun nor Virtualization.framework gives a guest a TPM or a virtual IOMMU. Nothing in this design rules them out. |

## 4. Architecture

```
macOS: MacNeutron
  MacNeutronVM (Swift) + libkrun fork (Rust, in process)
    Hypervisor.framework vCPUs, EnTSO set on each; hv_gic
    virtio-blk ×3   base.erofs, x86-64-rootfs.erofs (read-only, shared), state.img (writable)
    virtio-fs       the Mac's Steam library (game files), per game
    virtio-gpu      context type "dxmt" -> Metal executor (winemetal's Metal code)
    virtio-vsock    Steam forwarding -> MacNeutron's macOS lsteamclient Unix half -> Mac Steam's steamclient.dylib
    virtio-snd      -> CoreAudio
    virtio-input    keyboard and mouse from the MacNeutron window
    virtio-console  agent port (launch, focus, clock, gamepads)
    virtio-net      -> gvproxy (the Mac's own sockets)
    virtio-balloon  free-page reporting -> pages handed back to macOS
    virtio-rng
Linux guest (arm64, Fedora signed kernel, 4 KB pages, whole-VM TSO)
  systemd, Xvfb (Wine's windows: nothing is displayed from the guest), ALSA
  FEX (ours + eac-vm/fex-patches) as the binfmt interpreter for x86 and x86-64
  x86-64: our Wine (+ Proton's EAC loader changes) -> EAC bootstrapper -> game + EAC client
    DXMT (x86-64 PE builtins) -> winemetal.so (x86-64 ELF) -> /dev/dri/renderD128 (context "dxmt")
    lsteamclient (x86-64 PE half) -> forwarding Unix half -> vsock -> host
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
  - The `dxmt` context type (capset 24) in its virtio-gpu device, section 5.6: a stand-in for rutabaga_gfx in our
    fork (patch 0005), since rutabaga needs virglrenderer and keeps its component traits private.
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
pinned package versions, with `dnf --installroot`. The EAC runtime (Steam app 1826330) is fetched once through the
user's Steam account (§10) and kept on `state.img`; it is never shipped.

| Image | Contents | Shared |
|---|---|---|
| `base.erofs` (arm64) | systemd, glibc, kernel modules, FEX and its binfmt units, Xvfb, alsa-lib, the agent, CA bundle | Read-only, all games |
| `x86-64-rootfs.erofs` | Our Wine and its x86-64 libraries, as FEX's RootFS | Read-only, all games |
| `state.img` | Wine prefixes, the EAC runtime, logs | Writable, one per user |

Size targets are set in the plan from the first build and become gates.

### 5.4 FEX

- Our FEX (`macneutron` branch) with `eac-vm/fex-patches/` (the paper's fidelity series), built for Linux arm64.
- New option `HostTSO` (`FEX_HOSTTSO`): the VM already runs TSO, so FEX calls `SetHardwareTSOSupport(true)` without
  `PR_SET_MEM_MODEL`. The image's FEX config sets it only after a boot-time litmus check (section 7) passes.
- Registered through binfmt_misc for x86 and x86-64, so the whole tree (Wine, the game, the bootstrapper and the
  injected EAC client) runs under it.

### 5.5 Our Wine in the guest

- Built from the tree MacNeutron's macOS Wine comes from (`build/wine-arm64-src/wine`: upstream master plus
  `wine-arm64/patches/wine`), for `x86_64-linux-gnu`, cross-compiled in the builder; macOS-only patches (msync, winemac,
  x18, the ARM64EC work) stay out. `eac-vm/wine-patches/` adds Proton 11's EAC changes (§3).
- Builtins: DXMT (`d3d11`, `dxgi`, `d3d10core`, `d3d12`, `d3d12core`, `winemetal`), lsteamclient (`steamclient64.dll`
  and its forwarding Unix half, §5.9), and our `steam.exe`.
- Launch: the agent runs the game's `start_protected_game.exe` with `PROTON_EAC_RUNTIME`, `EAC_LAUNCHERDIR`,
  `SteamAppId` and `SteamGameId` set, the prefix on `state.img`, and sync through ntsync.
- The game folder is never written.
- Reference: Valve's Proton 11, run in the builder VM through its Linux Steam, defines the expected behaviour of each
  milestone-3 title.

### 5.6 Metal transport

- **Boundary:** winemetal's 153 Unix calls (`__wine_unix_call_funcs` in `src/winemetal/unix/winemetal_unix.c`,
  including airconv's shader compiles). The guest `winemetal.so` implements the same table by encoding each call into a
  ring; the host decodes it and runs today's Metal code unchanged.
- **Ring:** one blob per guest thread, mapped into the guest and the host (19.8 ns per 64-byte command in the
  benchmarks; 14 ns from x86-64 guest code under FEX in milestone 4a). A host thread consumes it, spinning while frames
  arrive and parking after 2 ms idle; the guest wakes it with one virtio-gpu submit per park. Synchronous calls spin
  50 µs, then wait on a fence. Implemented in `eac-vm/metal/` (research README §7).
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
| Wine's windows | none: Xvfb in the guest | Nothing is shown from the guest |
| Steam API | vsock port | MacNeutron's lsteamclient Unix half and Mac Steam (§5.9) |
| Game frames | context `dxmt` | `CAMetalLayer` in the same window |
| Keyboard, mouse | virtio-input | MacNeutron window events; relative motion while the game has the mouse |
| Gamepads | agent port -> `uhid` in the guest | GameController framework, real vendor and product IDs |
| Audio | virtio-snd | CoreAudio |
| Network | virtio-net | gvproxy (the Mac's sockets, no network extension) |
| Game files | virtio-fs | The game's folder in the Mac's Steam library |
| Clock after sleep | agent port | Wall time sent on resume |

### 5.8 Agent

A systemd service in `base.erofs` on the virtio-console port. It launches the game's EAC bootstrapper under our Wine
(§5.5), reports progress and focus, applies the host's time zone and wall clock, and creates gamepads.

### 5.9 Steam forwarding

- **Cut:** lsteamclient's Unix-call table (`__wine_unix_call_funcs`, 6,539 calls for 64-bit games). In the guest, the
  Unix half is a forwarder: each entry serialises its params struct and sends it over vsock (a libkrun vsock port mapped
  to a host Unix socket). The host executor calls the same entry in MacNeutron's macOS lsteamclient Unix half, which
  loads Mac Steam's `steamclient.dylib` as the Mac bridge does today. Both halves come from one lsteamclient tree, so
  the table matches.
- **Marshalling,** generated from `unixlib_generated.h` (84 distinct pointer types, most of them a handful of shapes):
  strings go in; fixed-size scalars and structs are copied both ways; buffer-and-size pairs are found by naming rules
  (`cb*`, `cch*`, `*Size`, `unMax*`, …) with an override table; interface pointers (`u_iface`) and `u_buffer` values
  pass through as opaque host values (`steamclient_get_unix_buffer` copies their contents back); function pointers and
  pointers to pointers are written by hand per call. A call whose shape is not covered fails with a logged error naming
  it.
- **Coverage is trace-driven:** every call SMITE 2 makes (traced through today's Mac bridge) must be covered for
  milestone 3a; each further title widens it.
- **Paths:** the host provides the four ntdll helpers lsteamclient's Unix half uses (Unix↔DOS paths, two string
  conversions). Host paths under the shared library map to the guest's mount (`/mnt/games/…`, `Z:\mnt\games\…`);
  paths outside it are refused.
- **Steam must be running** on the Mac and signed in; the executor sets `SteamAppId` for the launched game, as the Mac
  bridge does.
- **Parity deviation** (the maintainer's rule: the Steam connection follows Proton 11): Valve's `steamclient.so` is
  replaced by Mac Steam behind the forwarder, because the guest has no Steam client by design.

## 6. Launch flow

1. The user starts the game from Mac Steam, as today. Mac Steam runs MacNeutron as the game's compatibility tool, and
   MacNeutron chooses EAC mode when the title's Proton EAC support is known to work.
2. MacNeutronVM boots the guest with the game's folder shared and the Steam forwarder's socket listening.
3. The agent runs the EAC bootstrapper under our Wine. The bootstrapper injects EAC's client through ptrace, which FEX
   emulates.
4. The game's Steam calls reach Mac Steam through the forwarder; DXMT's calls reach the host's Metal executor.
5. When the game exits, the guest shuts down and MacNeutron exits, so Mac Steam sees the game end.

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
3. **EAC launches,** in three parts, each with its own plan:
   - **3a, Steam forwarding.** A Linux test program in the guest gets Steam initialised and a 234-byte auth ticket from
     Mac Steam through the forwarder (the Mac bridge's acceptance check), and every call in SMITE 2's trace is covered.
   - **3b, our Wine for x86-64 Linux,** with Proton's EAC changes, DXMT, lsteamclient and `steam.exe`, in the guest
     image. Pass: the bridge's probe under our Wine in the guest reports Steam initialised and the auth ticket.
   - **3c, EAC 301.** First the reference: SMITE 2 under Valve's Proton in the builder VM reaches
     `Launcherfinishedwith:301`. Then SMITE 2 under our Wine in the microVM, rendering in software, reaches 301.
4. **Metal transport,** in three parts, each with its own plan:
   - **4a, transport (done 2026-10-16).** A guest program's commands, blobs, waits and wakes reach the host executor
     through the dxmt context; numbers in research README §7.
   - **4b, winemetal forwarding,** in two plans:
     - **4b-1 (done 2026-10-16):** winemetal's 153 slots forwarded, with guest-chosen handles. A Linux program in the
       guest prints the same lines as on the Mac for a device, buffers, a blit, compute and a shared event; research
       README §8.
     - **4b-2 (done 2026-10-16):** DXMT's check suite passes in the guest as it does on macOS (the headless tests give
       the same lines and pixels): our DXMT's x86-64 build in the guest's Wine, 20 programs line for line; research
       README §9. Next: 4c.
   - **4c, presentation (done 2026-10-16).** SMITE 2 renders through the host: DXMT's presenting programs present in
     the host's window as on macOS, and SMITE 2 reaches its lobby and plays there through the Mac's Steam; research
     README §10. Input (milestone 5's keyboard and mouse) came with it. Next: milestone 5.
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

- **The EAC runtime's download.** It is a Linux Steam Tool (app 1826330), and Mac Steam may not install a Linux tool.
  Probes, cheapest first: `steamcmd +login anonymous +app_update 1826330`; Mac Steam's console `app_install 1826330`;
  SteamCMD forced to the Linux platform with the user's sign-in; the builder VM's signed-in Linux Steam (development
  only). Whatever works, the runtime is never committed or shipped.
- **Steam overlay and Steam Input** are not available to the game in the VM.
- **Valve's genuine `steamclient64.dll`** (SteamStub-wrapped games check its signature) comes as the Mac bridge gets it
  today (Valve's CDN, never committed) and goes into the guest prefix.

## 11. Out of scope

- EAC Linux v2 (TPM 2.0, measured boot, IOMMU, kexec, BPF LSM).
- BattlEye's Proton path.
- Titles whose developers have not enabled EAC on Proton, and kernel-mode anti-cheats.
- Rosetta for Linux: it cannot trace translated binaries, which EAC's injection needs.

## 12. Deferred from milestones 1-2

From the final review of milestones 1-2 (2026-10-15). The first two belong in milestone 3's plan.

- **HostTSO can be forced.** It sits in FEX's lowest config layer, so a user or app config on `state.img`, or
  `FEX_HOSTTSO` in a launch environment, enables it whatever the litmus test found. FEX should honour it only when
  `mn-tso-check` has left a marker (e.g. under `/run`).
- **`/var` is volatile.** §5.2 puts `/home` and `/var` on `state.img` with an overlay for `/etc`; the image puts only
  `/home` there. Decide before Steam keeps state in `/var`.
- **vCPU default and the Metal executor.** The default is every non-efficiency core (18 on an M5 Pro). Milestone 4's
  host consumer thread spins during frames and may need a core left free.
- **Host errors.**
  - Unknown or malformed `macneutron-vm` flags are ignored silently, and `UInt8(cpus)` traps outside 1-255.
  - Any `open`/`flock` failure on `state.img` reads "in use"; the lock fd needs `O_CLOEXEC`.
  - An unreadable `cmdline.txt` boots without the lockdown flags; the host should fail or append them.
  - A kernel the loader rejects isn't named in the error.
  - A guest panic and a clean power-off both exit 0, because libkrun maps SYSTEM_RESET to shutdown.
- **libkrun patches.**
  - 0003 logs EnTSO at debug level only.
  - 0002 calls `hv_vm_map`/`hv_vm_unmap` from the event loop, not the vmm worker thread; revisit with milestone 4's
    blob mappings.
  - 0002 bounds only the start of a reported range (use `get_slice`).
- **TSO check.**
  - `mn-tso-check` doesn't verify its `sed` took effect.
  - The litmus threads aren't pinned to distinct vCPUs.
  - Gates read the config file rather than FEX's effective config (`FEXGetConfig`).
- **Image build.**
  - `packages.lock` doesn't identify our FEX build.
  - The output set isn't replaced atomically, and a failed `mkfs.ext4` leaves an unformatted `state.img` that is kept.
  - dracut still runs the builder's own userspace (its modules now come from the pinned kernel).
- **Helpers.**
  - `builder.sh sync` (`rsync --delete`) accepts `~` or an empty target, which would wipe the builder's home, where
    the Steam sign-in lives.
  - `builder.sh` rebuilds only when `vmhost.swift` changes.
  - `run-all.sh` can't tell a replaced-but-not-re-registered binfmt interpreter (the F flag keeps the old file).
  - `libkrun/build.sh` drops commits on branch `macneutron` that haven't been exported as patches.
