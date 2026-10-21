# MacNeutron: a 32-bit Unix side in the VM (old WoW64, as Proton)

Status: asked for by the maintainer on 2026-10-20 ("32-bit support like Proton on regular Linux: an i386 Unix side
(old-style WoW64 with 32-bit Linux libraries) beside our new-WoW64 build, so things like EAC's 32-bit Linux runtime
(lib32/easyanticheat_x86.so) load"); its order ("Proton-style 32-bit after D3D9") and the instruction to go straight
through are the maintainer's, the design below is the executor's, made without a review stop. Milestones M3 and M4,
the maintainer's numbering after D3D9's M1 and M2. Each gets its own plan.

## 1. Goal

A 32-bit game in the VM runs as Proton runs it by default: its Unix side is a 32-bit Linux process (Wine's
`i386-unix`), so 32-bit native libraries load in it, EAC's `v2/lib32/easyanticheat_x86.so` first. New WoW64 (32-bit
PE code over the 64-bit Unix side, the 32-bit games spec of 2026-10-20) stays, as Proton keeps it: opt-in.

## 2. Findings (2026-10-20)

- Wine chooses at exec time. `get_alternate_wineloader` (`dlls/ntdll/unix/loader.c`): a 64-bit `wine` that starts a
  32-bit image execs `<dll_dir>/i386-unix/wine` when it exists, unless `WINEARCH=wow64`, which keeps new WoW64. So a
  32-bit build's `i386-unix` beside the x86-64 build makes old WoW64 the default and `WINEARCH=wow64` the opt-in;
  `PROTON_USE_WOW64=1` is Proton's name for that opt-in.
- One i386 PE build serves both modes. An i386 builtin calls its Unix side through ntdll's dispatcher: in old WoW64
  the i386 `.so`'s `__wine_unix_call_funcs`, in new WoW64 the x86-64 `.so`'s `__wine_unix_call_wow64_funcs`. The PE
  DLLs are the same files either way; only the Unix sides differ.
- The guest's 32-bit Linux userland is there. FEX's x86 RootFS (Fedora's `default.erofs`) carries i686 libraries in
  `/usr/lib`: `ld-linux.so.2`, glibc, `libgcc_s`, `libstdc++`, X11 (libX11, Xext, Xrandr, Xi, Xcursor, Xfixes, Xinerama,
  Xcomposite, Xrender), freetype, fontconfig, gnutls, libGL, libvulkan, SDL2, udev and dbus, but not `libasound`.
- FEX runs 32-bit Linux processes (its x32 syscall layer, which the 32-bit games spec's M1 left alone: there it
  patched FEX's mixed mode for new WoW64 only). Which ioctls it translates for a 32-bit process decides the Metal
  transport (`eac-vm/metal/guest/transport.c`: the virtio-gpu render node's `CONTEXT_INIT`, `RESOURCE_CREATE_BLOB`,
  `MAP`, `EXECBUFFER`, `GEM_CLOSE`), whose structs are all fixed-width.
- The builder's sysroot is x86-64 only (`eac-vm/sysroot`). A 32-bit Wine needs Fedora's i686 `-devel` packages.
- MacNeutron's own Unix modules in the guest are 64-bit only: `winemetal.so` (the Metal transport's forwarder, with
  Direct3D forwarding's Unix side, C), lsteamclient's forwarder (`eac-vm/steam-forward`, C) and `mtld3d.so` (Rust).
  Their i386 PE halves need nothing new. Their 32-bit callers already speak 32-bit layouts to them over new WoW64
  (`__wine_unix_call_wow64_funcs`): the guest `winemetal.so`'s WoW64 table (DXMT's params are width-independent:
  8-byte pointers and handles, `#pragma pack(8)`), the forwarder's WoW64 calls (`MN_WOW64`, Valve's `wow64_*` structs),
  mtld3d's width-independent params.
- An i686 Linux compile of a struct with a `uint64_t` gives it 4-byte alignment (the i386 System V ABI), where the i386
  PE side (MSVC ABI) gives 8. DXMT's `winemetal_thunks.h` relies on `#pragma pack(push, 8)`, which caps alignment and
  doesn't raise it; mtld3d's params are Rust `#[repr(C)]` with explicit padding. Each 32-bit Unix module's parameter
  layouts have to be checked against its PE side's.
- Sound in the guest is an ALSA plugin over vsock, 64-bit; a 32-bit `winealsa.drv` needs i686 `libasound.so.2` and an
  i686 build of the plugin.

## 3. Decisions

| Question | Decision | Why |
|---|---|---|
| Builds | The x86-64 build as today (`--enable-archs=i386,x86_64`), plus a 32-bit build in the builder (`--host=i686-linux-gnu --with-wine64=<the x86-64 build> --enable-archs=i386`) whose `i386-unix` (and `bin/wine` loader) is installed beside the 64-bit one | Proton's layout; the x86-64 build and its new WoW64 stay as they are |
| Default | Old WoW64 for every 32-bit program in the VM; `MACNEUTRON_VM_WOW64=1` (launch option) or `PROTON_USE_WOW64=1` sets `WINEARCH=wow64` in `run-game.sh` | Proton's default and its opt-in, under MacNeutron's name and Proton's |
| 32-bit libraries | FEX's RootFS's i686 libraries; i686 `alsa-lib` and our ALSA plugin's i686 build go into `wine.erofs` (`lib/wine/i386-unix/../mn-i386`, on `LD_LIBRARY_PATH` for the 32-bit loader) | No new image; the one missing library comes with Wine |
| Sysroot | `eac-vm/sysroot` gains an i686 tree (`packages-i686.txt`, signature-checked and locked as the x86-64 one) | Same pinning as today |
| Our Unix modules | i386 builds of the guest `winemetal.so` (its 64-bit table then the WoW64 one's entries, which already take 32-bit callers' params), the lsteamclient forwarder (its WoW64 calls as its table) and `mtld3d.so` (`i686-unknown-linux-gnu`, its WoW64 dispatch the 64-bit one); each parameter struct's layout checked against its PE side | The 32-bit callers and wire formats exist; only the process width changes |
| EAC | `run-game.sh` sets `PROTON_EAC_RUNTIME` when the runtime is in the share (`eac.sh` puts it there); bringing it to players' Macs stays the separate gap the 32-bit games spec named | The load is this work; delivery is not |
| Spike first | M3's first task builds the 32-bit Wine and runs a 32-bit console program and `metal-probe` (i686) in the guest, before anything else is built on it | FEX's x32 layer and its ioctls decide whether the approach holds |

Out of scope: old WoW64 on the macOS path (the Mac Wine has no i386 Unix side; Preflight keeps refusing 32-bit games
there), 32-bit Linux games (native), and anything upstream to FEX or Wine.

## 4. M3: old WoW64 runs

- The spike (above). If FEX can't run a 32-bit Wine process, the work stops there and reports.
- `eac-vm/sysroot`: the i686 tree. `eac-vm/wine/build.sh wine32`: the 32-bit build; `install` installs both.
- `wine.erofs` gains `i386-unix` (about 100 MB, measured) and `mn-i386` (alsa-lib, the ALSA plugin).
- `run-game.sh` and the tests: the mode (old WoW64 unless `MACNEUTRON_VM_WOW64=1` or `PROTON_USE_WOW64=1`), and
  `PROTON_EAC_RUNTIME` when present. The launcher passes `MACNEUTRON_VM_WOW64` into the VM like the other options.
- lsteamclient's forwarder for i386 (its WoW64 entry points as the 32-bit table), so a 32-bit game's Steam works
  under old WoW64 as it does under new.
- Gates (in the guest): a 32-bit console program and `wow64.sh`'s rows under old WoW64 (the 32-bit process is an i386
  Linux process: `/proc/<pid>/exe` is `i386-unix/wine-preloader`); new WoW64 with `MACNEUTRON_VM_WOW64=1` unchanged; the
  32-bit Steam probe (app 480) under both; `eac.sh` with Rogue Company: `easyanticheat_x86.so` mapped in the game's
  process and EAC's own verdict recorded (held for the maintainer: needs the game installed).

## 5. M4: Metal for the 32-bit Unix side

- `eac-vm/metal/build-guest.sh`: an i686 `winemetal.so` (transport, galloc's low region, the D3D forwarding Unix side),
  its params checked against DXMT's and mnd3d's i386 PE sides.
- mtld3d's i686 `mtld3d.so` (`dxmt/mtld3d-linux-cargo.sh` for `i686-unknown-linux-gnu`, a Rust target to install).
- `image/build.sh`: both into `i386-unix`.
- Gates (in the guest, under old WoW64): DXMT's 32-bit rows (`d3d-smoke.sh`'s `smoke32`), `d3d9_triangle` i386 and
  Wine's d3d9 tests' x86 lane (`eac-vm/dxmt/d3d9-gate.sh`), all as under new WoW64; one 32-bit D3D9 game (held).

## 6. Risks

- FEX's x32 layer may lack ioctls the transport needs, or the futexes and signals Wine's 32-bit Unix side uses: the
  spike is first for this reason.
- Two Unix sides double what must agree with each PE side; a layout mismatch is silent until a call misreads. Each
  module gets a layout check (offsets compared between its i686 Linux and i686 PE compiles).
- `wine.erofs` grows by the 32-bit Unix side; the setup download grows with it.
- Downloads: Fedora's i686 packages (sysroot, alsa-lib) and the Rust `i686-unknown-linux-gnu` target need the
  maintainer's approval before they are fetched.
