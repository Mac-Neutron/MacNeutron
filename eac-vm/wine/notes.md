# Our Wine on x86-64 Linux: what each macOS patch does

The guest Wine is the same tree as the macOS one: Wine 11.20 plus our series (`build/wine-arm64-src/wine`,
branch `macneutron`, 43 commits over wine-11.20, 7298a818) plus `eac-vm/wine-patches` (Proton 11's EAC changes). Built
with `--enable-archs=x86_64` for `x86_64-linux-gnu`, so every patch falls in one of four groups.

**Compiled out (`__APPLE__` or `__aarch64__`/ARM64EC only).** The loader and memory work: e7ca11cc (loader link
flags), 6bb72271 and 89c9b11d (soft pagezero), bb246c0a and 03d212ac (4K-page re-exec), 31044c7c, 9c1fe0af, 819956c4,
108a073d (x18 toggle), 94a3d0e9 (exec environment), 48ad4b70 and 304cbd26 (W^X), 8c88c517 (entitlement check),
a9c3932a (CPU ID registers), 60235a9b (host regions), and the ARM64EC ones 16f72e0b, 7a678afd, 00ae4b5e, ad576f74,
plus db0153de (ARM64 string routines in msvcrt). The x86-64 build has none of this code.

**Not built on Linux.** winemac.drv's e92bd64d, 5dc7fa99 and 432cf73c: configure builds winex11.drv instead. msync
(769575fb, 40421051, 875c4db7): `server/msync.c` is `#ifdef __APPLE__` with stubs otherwise, so the guest
wineserver uses its plain server-side sync. 97cb9aea (makedep) only matters with a hybrid arch.

**Active, and the same as on macOS.** e3aaf5fd (win32u text metrics with no font), 8699ded1 (the Windows 2003 SP1+
critical-section LockCount encoding), 26130284 (no wineboot wait window while a prefix is updated), 42321e57
(WINEDEBUG read before init_startup_info drops argv[0]: a child crashed without a trace's lazy read) df0e5392
(map_reserved_area skips a reserved area its aligned start passes: mtld3d's aligned reservations) and 21d6c56b (its
top-down search starts at the lower limit) and patch 0043 (639f389b on the tree rebuilt 2026-10-23: a mount point,
such as the games share's root, answers FSCTL_GET_REPARSE_POINT as the reparse point Wine calls it) are
platform-neutral fixes. 3b389503 builds lsteamclient; the guest replaces its Unix half with the forwarder
(`eac-vm/steam-forward`, plan 3b Task 5). d8b1c118 (a genuine steamclient64.dll's exports jump to lsteamclient.dll) is
what SteamStub games need here too.

**Active but inert.** libxess (23235145 through c56b03e2) builds, but it drives MetalFX through DXMT's
`IMTLD3D12*` interfaces; on a D3D12 device that isn't DXMT's the query fails and XeSS reports an error, which games
treat as XeSS unavailable.

**Proton's EAC changes** (`eac-vm/wine-patches`, plan 3b Task 1): PROTON_EAC_RUNTIME for the bridge files, the bridge
loaded only when its native `.so` sits next to the Windows library, the EAC launcher and EOS bootstrapper handling,
and the `download-alt.easyanticheat.net` DNS hack. These are why the guest runs our Wine and not upstream's.

**New WoW64** (0007, the 32-bit games' spec 2026-10-20 §5): the guest builds i386 and x86-64 PE code over the 64-bit
Unix side; libxess and libxess_dx11 stay x86-64 only (XeSS ships x64 only, and their size asserts are x64's).
