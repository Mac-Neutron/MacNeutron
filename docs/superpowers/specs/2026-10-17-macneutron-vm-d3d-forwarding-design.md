# Direct3D forwarding: DXMT runs natively on the Mac, the VM's games call it

Status: design approved in conversation 2026-10-17; this spec awaits the maintainer's review.
Parent: `2026-10-15-macneutron-eac-vm-route-design.md` (§5.6, the graphics boundary this spec moves).

## 1. Goal

In the microVM a game costs what it costs on macOS. Today DXMT runs inside the guest as x86-64 code under FEX, and so
do Wine and wineserver; on macOS they run natively and only the game is translated. SMITE 2 in a match, measured
2026-10-17 (a 10-second `sample` of `macneutron-vm`, 18 vCPUs):

| Where | Cores |
|---|---|
| The guest's own code (`hv_trap`: the game, Wine, wineserver, DXMT) | 8.35 |
| vCPUs waking (Hypervisor.framework's `wait_for_interrupt` re-taking its lock, interrupt bookkeeping) | ~1.7 |
| Metal executor consumers spinning (one ring never parks: its records come less than 2 ms apart) | ~1.6 |
| Sound (`audio.c`'s two streams, a 2 ms poll that reports every turn) | ~0.95 |

The process used 1146% CPU with the GPU at about 80%; the same scene on macOS uses 400-500% with the GPU at 98%. (These
were read while `sample` ran, which suspends the process: M0's baseline, research README §15, has the cleaner numbers.)

Done when, for SMITE 2 and Palworld at the same in-game settings as on macOS (`eac-vm/tests/perf-compare.sh`):
- The GPU is busy 95% of the time or more.
- Frames per second are within 10% of the macOS run.
- The VM's whole CPU (`macneutron-vm`, `mn-steam-host`, `gvproxy`) is within 1.5 times the macOS run's (the game's Wine
  processes).

And DXMT's own D3D11 and D3D10 test programs, run in the guest through the forwarding, match their macOS run.

## 2. Decisions (maintainer, 2026-10-17)

| Question | Decision |
|---|---|
| Which APIs | D3D12, DXGI, D3D11 and D3D10 in this spec. D3D9 gets its own spec after it (DXMT has no D3D9: the host needs a D3D9 implementation chosen first). |
| Where DXMT runs | Natively, inside `macneutron-vm` (approach 1): its native build, loaded in the process that already maps the guest's memory and runs the Metal executor. Not a host Wine process. |
| The target | Parity in CPU too: the three numbers in §1, not only a GPU-bound game. |
| The D3D10/11 gate | DXMT's own D3D11 and D3D10 test programs, not a game. |

## 3. Architecture

```
guest (x86-64 Linux, our Wine)                         host (macneutron-vm, arm64 macOS)
  game.exe                                               native DXMT: d3d12, dxgi, d3d11, d3d10core, winemetal
   -> d3d12.dll / dxgi.dll / d3d11.dll / d3d10core.dll       ^  real COM objects, Metal, airconv, MetalFX
      (generated COM proxies, x86-64 PE)                     |
   -> guest winemetal.so transport: per-thread rings,     dispatcher: handle table -> native object, one
      blobs, doorbells (virtio-gpu context "dxmt")  ----->  consumer per ring, replies, shared pages
                                                          VM window (stand-in winemac.so): DXMT presents into
                                                          its CAMetalLayer
```

Today's path, DXMT in the guest over winemetal's 153 calls (parent spec §5.6), stays as the fallback
(`MACNEUTRON_VM_D3D=guest`).

## 4. Components

### 4.1 Native DXMT on the host

- Our fork, built through DXMT's native (non-cross) meson path, which upstream keeps (`build: add native build
  targets`): `d3d12`, `dxgi`, `d3d11` and `d3d10core` as macOS dylibs, `winemetal` linked in natively
  (`src/nativemetal`), Windows types from `include/native`. arm64, with the runtime's CPU tuning (`MACNEUTRON_CPU`).
- Native DXMT's Win32 calls are few (in `d3d12`, `dxgi` and `dxmt`: eight `CloseHandle`, one each of `CreateEventW`,
  `SetEvent`, `WaitForSingleObject`): the ones the native build lacks get small shims in the host.
- Built by `eac-vm/dxmt-native/build.sh` from `build/wine-arm64-src/dxmt`; shipped in MacNeutron.app's
  `Contents/Helpers/vm/dxmt/`, installed into the VM folder with the other host tools (`VMToolsInstaller.items`).
  The macOS runtime's PE build of DXMT is unchanged.

### 4.2 The generator

- `eac-vm/d3d/gen.py` reads Wine's IDL (`d3d12.idl`, `dxgi.idl` to `dxgi1_6.idl`, `d3d11.idl` to `d3d11_4.idl`,
  `d3d10.idl`, `d3d10_1.idl`: 199 interfaces, 899 methods) and `eac-vm/d3d/rules.txt`, the hand-written annotations.
- A rule per method: each pointer argument's direction (in, out, in-out) and length (a count argument, a struct's
  size field, a NUL-terminated string, a fixed size), and whether the call is asynchronous (§4.5).
- It writes the guest proxies, the host stubs and a manifest that numbers every method. A method without a rule stops
  the build; `eac-vm/d3d/tests/coverage.py` checks every method a game called (§7) has one.

### 4.3 The guest stand-ins

- `d3d12.dll`, `dxgi.dll`, `d3d11.dll` and `d3d10core.dll`, x86-64 PE, built with MacNeutron's llvm-mingw: the
  generated proxy objects and the exports games call (`D3D12CreateDevice`, `CreateDXGIFactory2`, `D3D11CreateDevice`,
  `D3D10CoreCreateDevice` and the rest of each DLL's export list).
- They reach the host through the guest `winemetal.so` transport: its rings, blobs and doorbells, with a new record
  family for forwarded methods. No new device or kernel module.
- They go into `wine.erofs` beside the guest DXMT DLLs, and `run-game.sh` chooses which set a game loads (§4.13).

### 4.4 The host dispatcher

- One consumer per ring, as the executor runs today: it reads a record, finds the object by handle, unmarshals the
  arguments, calls the native DXMT method, and writes the reply when the call has one.
- The handle table holds each object's native COM pointer and the interfaces it reported at creation.

### 4.5 Objects and calls

- Handles are the guest's choice; a creation record carries the new one. `AddRef` and `Release` count in the guest;
  only the last `Release` reaches the host, deferred until every ring that used the object is past it (the winemetal
  forwarding's rule for its own objects).
- `QueryInterface` is answered in the guest from the interfaces the host object reported.
- Immutable answers are cached in the guest after their first call: `GetDesc` and its siblings,
  `GetGPUVirtualAddress`, `GetCPUDescriptorHandleForHeapStart`, `GetGPUDescriptorHandleForHeapStart`,
  `GetDescriptorHandleIncrementSize`, `CheckFeatureSupport`.
- Asynchronous, batched on the calling thread's ring: every command-list recording call (draws, dispatches, barriers,
  copies, root arguments, render targets), descriptor writes (`Create*View`, `CreateSampler`, `CopyDescriptors*`),
  `ExecuteCommandLists`, `Signal`, `Present`.
- Synchronous: creations a game checks for failure (resources, heaps, pipelines, root signatures, command signatures,
  query heaps, swapchains), `Map`, and every method with output data.
- A call on one thread that uses an object created on another waits for that creation (the winemetal forwarding's
  cross-ring rule).

### 4.6 Memory

- Mappable resources (upload and readback heaps, CPU-visible custom heaps, D3D11 dynamic and staging resources) live
  in memory the guest shares with the host: the guest hands the host arenas from its shared-memory allocator (the
  winemetal forwarding's galloc regions, mapped in the game's process), and native DXMT carves its CPU-visible
  allocations from them through the allocation hook DXMT patch 0071 added (`WMTMemoryAlloc`). `Map` returns the
  guest's own address for that memory: persistently mapped upload buffers need no copies.
- When an arena is full the host asks for another in the reply of the call that needed it.
- GPU-only resources stay in the host's private Metal memory and never touch guest RAM.
- Pointer inputs are copied per their rule (§4.2): shader bytecode, pipeline and root signature descriptions, initial
  data, subresource data. One that doesn't fit a ring record goes through a blob.
- Descriptor handles, GPU virtual addresses and fence values pass through as opaque values.

### 4.7 Waiting

- Each fence's completed value is mirrored by the host into a shared page; `GetCompletedValue` reads it in the guest.
- `SetEventOnCompletion` signals the guest's Win32 event through the winemetal forwarding's shared-event path.
- DXGI's frame-latency waitable object is mirrored the same way.

### 4.8 Presentation

- A swapchain is created from the game's window in the guest and bound, on the host, to the VM window's Metal layer
  (one window per VM, as now).
- `ResizeBuffers` is forwarded. Full-screen modes stay emulated in the guest (Wine's EmulateModeset): the host presents
  at the buffer's size and the window scales it.
- `Present` is asynchronous; the game paces itself on the mirrored frame-latency object.

### 4.9 Threads

- Each guest thread that calls Direct3D gets its own ring; the host runs each ring on its own consumer, so a thread's
  recording stays in order.
- `ExecuteCommandLists` waits, on the host, until the lists it names are fully recorded on their own rings. D3D11
  deferred contexts and `ExecuteCommandList` work the same way.

### 4.10 D3D11 maps

- `Map` with `D3D11_MAP_WRITE_DISCARD` or `D3D11_MAP_WRITE_NO_OVERWRITE` is answered in the guest, from a pool of
  replacement memory per resource that the host hands out ahead of time; `Unmap` is asynchronous and says which piece
  is now current. D3D11 games map dynamic buffers hundreds of times a frame.
- Other maps (reads, staging) are synchronous.

### 4.11 Upscalers

- `nvngx` and `nvapi` (DLSS on MetalFX) and our `libxess` stand-in (XeSS on MetalFX, Wine patch 0022) reach DXMT
  through its private interfaces. Their entry points are forwarded the same way, to their native host builds.

### 4.12 Shaders and precache

- DXIL to Metal (airconv) runs natively on the host.
- DXMT's shader cache and its pipeline recordings live in the game's VM state folder (`state/<appid>.dxmt/`).
- The launcher's precache replays a game's recordings natively before it starts, as it does on macOS, behind the same
  toast.

### 4.13 Settings and selection

- The game's `DXMT_*` settings (launch options, per-game settings) reach native DXMT through `macneutron-vm`'s
  environment: a VM runs one game.
- `run-game.sh` points Wine at the stand-ins by default and at the guest DXMT DLLs with `MACNEUTRON_VM_D3D=guest`,
  today's path, which stays for any game the forwarding breaks.

## 5. VM overheads and measurement

### 5.1 The guest profiler

- `MACNEUTRON_VM_PROFILE=1`: `run-game.sh` writes every guest thread's CPU share and name (`/proc/<pid>/task/*/stat`)
  to `traces/profile.txt` every 5 s, and the host takes a `sample` of `macneutron-vm` after the measured window:
  `sample` suspends the process at each tick (SMITE 2 fell to 1.4 fps while one ran, 2026-10-17).

### 5.2 The comparison

- `eac-vm/tests/perf-compare.sh`: the maintainer plays the same scene for 60 s, on macOS and in the VM; it samples once
  a second the CPU of the process tree (§1), the GPU's busy share (`ioreg`'s IOAccelerator statistics, no sudo) and
  the frame rate (DXMT's frame statistics), then prints the three ratios §1 gates on.

### 5.3 The fixes, each measured with §5.2

- **vCPUs:** a setting (`MN_VM_CPUS`), its default chosen by measurement (likely the performance cores only, not 18):
  fewer vCPUs for the same work wake each other less.
- **Executor spin:** a consumer spins only while records keep arriving within a short window, then parks; the window
  is chosen by measuring frame rate against host CPU (a parked consumer takes about 36 us to wake).
- **Sound:** each stream reports its position when it has moved by a period, or every 10 ms at most, not every 2 ms.
- **What the profiler finds next:** for example wineserver, which also runs as x86-64 under FEX. It enters the M5 plan
  when it's large enough to matter for §1, and §11 otherwise.

## 6. Failure handling

| Failure | What happens |
|---|---|
| Native DXMT loses its device (a Metal error, a failed call) | Every later call returns `DXGI_ERROR_DEVICE_REMOVED` (or the D3D11 equivalent); the game's log says why. |
| A method without a rule | The generator stops the build. |
| An interface the host object doesn't have | `E_NOINTERFACE`, logged. |
| A malformed record from the guest | The host refuses it and stops that ring, as the executor does with a broken ring today; the game's log says which. |
| A host arena can't grow | The creation or map that needed it returns `E_OUTOFMEMORY`. |
| A game the forwarding breaks | `MACNEUTRON_VM_D3D=guest` runs it as before. |

## 7. Testing

- Generated marshaling, round trip: guest proxies and host stubs in one Mac process over a loopback transport, as the
  winemetal forwarding's `memory-test` does: every struct a rule names, in and out.
- `MACNEUTRON_VM_D3D_TRACE=1`: the guest proxies log every method ID called; `coverage.py` checks each has a rule and
  reached the host.
- DXMT's test programs (the 4b-2 gate's 20 headless D3D12 programs and the presenting ones, then the D3D11 and D3D10
  ones): natively on the Mac (M1), and in the guest through forwarding (M2, M4), each compared with its macOS run by
  the 4b-2 compare rule.
- Games: SMITE 2 and Palworld through forwarding (M3), then `perf-compare.sh` (M5).
- The existing VM gates keep passing: `vm-setup.sh`, `turn.sh`, `modes.sh`, `tpm-host.sh`, `audio.sh`.

## 8. Milestones, in order (one plan each)

| Milestone | Delivers | Gate |
|---|---|---|
| M0, measure | §5.1 and §5.2 | Baseline numbers for SMITE 2 and Palworld, macOS and today's VM; DXMT's share of the guest's CPU. |
| M1, native DXMT | §4.1 | DXMT's test programs natively on the Mac match the macOS Wine run. Go/no-go: if our D3D12 can't build natively at reasonable cost, stop and ask the maintainer before M2. |
| M2, D3D12 and DXGI forwarding | §4.2 to §4.7, §4.9 | The loopback test; the headless D3D12 programs through forwarding match macOS. |
| M3, games | §4.8, §4.11, §4.12 | SMITE 2 and Palworld play in the VM through forwarding, with XeSS and DLSS on MetalFX; the coverage trace shows no unforwarded call. |
| M4, D3D10 and D3D11 | §4.10 and the D3D10/11 rules | DXMT's D3D11 and D3D10 test programs through forwarding match macOS. |
| M5, overheads and parity | §5.3 | §1's three numbers for SMITE 2 and Palworld. |

## 9. Shipping

- After M5, forwarding is the VM's default; `MACNEUTRON_VM_D3D=guest` keeps today's path.
- Native DXMT ships in the app's VM helpers (§4.1). The guest stand-ins ship in the VM parts' `wine.tar`, stamped from
  their trees like the other guest stages (`eac-vm/image/stamps.sh`); the guest DXMT DLLs stay in it for the fallback.
- Licences: DXMT's notices are already in the app; the generated code is ours.
- Anti-cheat: unchanged. The game loads Wine's builtin DLLs in the guest; nothing hooks or injects into it.

## 10. Risks

| Risk | Answer |
|---|---|
| Our D3D12 needs real porting to build natively | M1 is the go/no-go, before any forwarding work. |
| A game reads descriptor-heap memory or relies on undefined behaviour | Rare; the fallback switch runs it. |
| Too many synchronous calls on a hot path | M3's trace and M5's measurements show where; guest-side caching (§4.5) or new asynchronous rules answer them. |
| Arena memory grows without bound in a long session | Freed allocations return to their arena (DXMT's own free through the hook); the profiler (§5.1) logs arena use. |

## 11. Out of scope

- D3D9: its own spec, starting with which D3D9 implementation runs on the host.
- D3D12 video (`d3d12video.idl`), `d3d11on12`, and the SDK debug layers (`*sdklayers.idl`): `E_NOINTERFACE`.
- Wine's `d3dcompiler` stays in the guest: a game compiling HLSL at run time does it under FEX.
- `d3d10.dll`'s effects framework stays Wine's, in the guest, on top of the forwarded `d3d10core`.
- The vCPU wake-up cost beyond choosing their number (a different idle mechanism in the guest kernel).
