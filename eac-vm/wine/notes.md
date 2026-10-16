# Our Wine on x86-64 Linux: what each macOS patch does

The guest Wine is the same tree as the macOS one: upstream master plus our series (`build/wine-arm64-src/wine`,
branch `macneutron`, 39 commits over upstream 081042ed) plus `eac-vm/wine-patches` (Proton 11's EAC changes). Built
with `--enable-archs=x86_64` for `x86_64-linux-gnu`, so every patch falls in one of four groups.

**Compiled out (`__APPLE__` or `__aarch64__`/ARM64EC only).** The loader and memory work: 45939b0d (loader link
flags), 9783750b and 0b13586e (soft pagezero), 9886dc49 and 6681fcf9 (4K-page re-exec), 3651bf99, 2cf7ed38, 8c0fd465,
dd92a4b4 (x18 toggle), 3eb3adfd (exec environment), caf58f51 and 9e810bb2 (W^X), 431240b9 (entitlement check),
522a308b (CPU ID registers), 01be28e7 (host regions), and the ARM64EC ones a50c494e, fc928e82, 0771adf1, 0c40afba,
plus 887f9c1d (ARM64 string routines in msvcrt). The x86-64 build has none of this code.

**Not built on Linux.** winemac.drv's 40991c02, 38640fe7 and b378b8e7: configure builds winex11.drv instead. msync
(d56578ae, 9df2c3e8, 2c061864): `server/msync.c` is `#ifdef __APPLE__` with stubs otherwise, so the guest
wineserver uses its plain server-side sync. e52cc81e (makedep) only matters with a hybrid arch.

**Active, and the same as on macOS.** 9cc4d506 (win32u text metrics with no font) and 79874a0c (the Windows 2003
SP1+ critical-section LockCount encoding) are platform-neutral fixes. 4bdae53e builds lsteamclient; the guest replaces
its Unix half with the forwarder (`eac-vm/steam-forward`, plan 3b Task 5). a30386ff (a genuine steamclient64.dll's
exports jump to lsteamclient.dll) is what SteamStub games need here too.

**Active but inert.** libxess (14fd47ce through c35ad85e) builds, but it drives MetalFX through DXMT's
`IMTLD3D12*` interfaces; on a D3D12 device that isn't DXMT's the query fails and XeSS reports an error, which games
treat as XeSS unavailable.

**Proton's EAC changes** (`eac-vm/wine-patches`, plan 3b Task 1): PROTON_EAC_RUNTIME for the bridge files, the bridge
loaded only when its native `.so` sits next to the Windows library, the EAC launcher and EOS bootstrapper handling,
and the `download-alt.easyanticheat.net` DNS hack. These are why the guest runs our Wine and not upstream's.
