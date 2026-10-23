# MacNeutron: 32-bit games in the microVM

Status: design approved 2026-10-20 in conversation (approach 1 for FEX, milestones M1-M4). Each milestone gets its own
plan. The maintainer's ruling for this work: no gate per milestone; every gate, new and regression, runs once after M4
(§8).

## 1. Goal

A 32-bit Windows game runs in the microVM as a 64-bit one does: its Steam calls reach Mac Steam, its Direct3D 10, 11 and
12 run on DXMT (in the guest, or forwarded to the host's native DXMT), and a 32-bit Easy Anti-Cheat launcher runs.

The first title is Rogue Company (Steam app 872200). Its game is 64-bit, but its EAC launcher, `RogueCompanyEAC.exe`, and
EAC's `EasyAntiCheat_EOS_Setup.exe` are 32-bit, and today they can't start in the VM at all.

## 2. Findings that shaped it (2026-10-20)

- The guest's Wine is built `--enable-archs=x86_64` only: the image has `x86_64-unix` and `x86_64-windows`, no 32-bit
  Windows DLLs, and the root filesystem has no 32-bit loader. The launcher's Preflight refuses 32-bit games everywhere
  ("MacNeutron 0.1 runs 64-bit games only").
- Proton runs 32-bit programs with old WoW64 (`i386-unix`, a 32-bit Linux process) by default. New WoW64 is its opt-in
  mode: `PROTON_USE_WOW64=1` sets `WINEARCH=wow64` (on by default for two app ids in Proton Experimental 11.0-20261014).
  The Proton EasyAntiCheat Runtime ships for both: `v2/lib32/easyanticheat_x86.so` (32-bit ELF) for old WoW64, and
  `v2/lib64/easyanticheat.dll` beside a 64-bit `easyanticheat.so` for a 32-bit program over a 64-bit Unix side.
- Wine's new WoW64 on x86-64 Linux, as `dlls/ntdll/unix/signal_x86_64.c` and `dlls/wow64cpu/cpu.c` do it: 32-bit code
  runs in the kernel's 32-bit user code segment `0x23`; `wow64cpu` enters it with `iretq` or `ljmp` and leaves it with a
  far jump to `0x33`; the 32-bit TEB's `%fs` selector comes from `set_thread_area` called through `int $0x80` from 64-bit
  code (`alloc_fs_sel`); 64-bit code loads that selector with `mov %fs` before returning to 32-bit code (`leave_handler`,
  `wow64cpu`, the syscall dispatcher) and resets the base with `wrfsbase` or `arch_prctl(ARCH_SET_FS)` on the way in.
- FEX already decodes each block in the mode its code segment's L bit gives (`Frontend.cpp`: `BlockInfo.Is64BitMode =
  CSSegment->L == 1`; the opcode tables carry both modes), builds IR per block (`BeginFunction`), switches `cs` on
  `iretq` and far `jmp`/`call`/`ret` and ends the block there, and saves and restores `cs` in 64-bit signal frames. The
  asserts that the mode never changes are compiled out of release builds (`ENABLE_ASSERTIONS` off). What it lacks is in
  §4.
- FEX's repository says "AI must not be used to generate code for contributions to this project." Our FEX changes stay
  in our fork's series (`eac-vm/fex-patches`), as the fidelity series does, and are never offered upstream.
- The Mac `lsteamclient.so` exports `___wine_unix_call_wow64_funcs`; Proton's lsteamclient has WoW64 thunks for every
  call (6539 `wow64_` structs in `unixlib_generated.h`). DXMT builds for 32-bit (`build-win32.txt`) and its
  `winemetal_unix.c` has a `__wine_unix_call_wow64_funcs` table.
- The Mac Wine gets the low 4 GiB on Apple Silicon through the `com.apple.developer.cross-architecture-support`
  entitlement: the hard 4 GiB `__PAGEZERO` becomes one hard page plus a soft reservation `MAP_FIXED` may replace
  (`virtual.c`). The VM host (`macneutron-vm`) is signed with the hypervisor entitlement only.

## 3. Decisions

| Question | Decision | Why |
|---|---|---|
| Which WoW64 | Wine's new WoW64 (`--enable-archs=i386,x86_64`): 32-bit PE code over the 64-bit Unix side | One Wine build and no 32-bit Linux libraries in the image or in the setup players download. It is a mode Proton supports, and the EAC runtime has its bridge. |
| FEX | Approach 1: per-block mode plus the kernel behaviour Wine relies on (§4) | Each change maps to a line of Wine. The rejected approaches: the whole 32-bit syscall table behind `int 0x80` (FEX's x32 handler assumes a 32-bit process; Wine never makes those calls), and changing Wine instead (its 32-bit code still needs a 32-bit code segment). |
| Steam for 32-bit games | The host runs Valve's own WoW64 functions from the Mac `lsteamclient.so` in a low-4 GiB region; M3 starts with a spike for that region. Fallback: guest-side translation to the 64-bit calls | Valve's 32-bit struct conversions and returned-string handling are reused, not rewritten. |
| Direct3D for 32-bit games | Guest mode: 32-bit DXMT plus a WoW64 table in the guest `winemetal.so`. Forwarded: 32-bit proxies that widen to the existing 64-bit wire | The host side stays as it is in both modes. |
| Testing | No gate per milestone; all gates once after M4 | The maintainer's ruling (2026-10-20). |

Out of scope: D3D9 (DXMT has none, as for 64-bit games), old WoW64, 32-bit games on the macOS path (Preflight keeps
refusing them there), bringing the EAC runtime to players' Macs (a separate gap: `run-game.sh` sets no
`PROTON_EAC_RUNTIME`, and Mac Steam can't install a Linux-only dependency), and anything upstream to FEX.

## 4. M1: FEX mixed mode

Four patches after 0029 in `eac-vm/fex-patches`, on the guest's FEX (`build/eac-vm-work/fex-linux`). The macOS FEX tree
is not touched.

| Patch | Change |
|---|---|
| 0030 GDT | A 64-bit process's default GDT gets the kernel's other user segments beside `0x33`: entry 4 (`0x23`, 32-bit code: L=0, D=1, base 0, 4 GiB limit) and entry 5 (`0x2b`, data). Threads inherit them as they inherit the rest of the GDT. |
| 0031 Mode per block | The two "Expected operating mode to not change at runtime" asserts go. The x87 stack pass takes the GPR size of the block it optimizes (`Core.cpp` passes the decoder's per-block mode), not the process's, so a 32-bit block's x87 memory addresses wrap at 4 GiB. |
| 0032 FS and GS in 64-bit code | `mov` and `pop` to `%fs`/`%gs` with a non-null selector store the selector and load the descriptor's 32-bit base, as the CPU does. A null selector loads base 0, as Intel CPUs do (AMD keeps the base; Wine never loads a null `%fs` there). Reading `%fs`/`%gs` gives the selector instead of 0. |
| 0033 `int 0x80` in a 64-bit process | The syscall handler tells `int 0x80` (`CD 80`) from `syscall` (`0F 05`) by the instruction at the saved RIP. `int 0x80` uses the i386 ABI (`eax`, then `ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp`): `set_thread_area` (243) and `get_thread_area` (244) run FEX's x32 implementation, which writes the thread's GDT TLS entries 12-14; any other number answers `-ENOSYS` (`ponytail:` the two Wine makes; the x32 table when a program needs more). |

Known ceilings, each a `ponytail:` comment where it lives:
- Compiled blocks are looked up by RIP alone; a RIP run in both modes would reuse the first mode's code. Wine's 32-bit
  and 64-bit code never share an address.
- FEX's code cache (`EnableCodeCachingWIP`, off by default) assumes one mode; it stays off.
- A seccomp filter doesn't see an `int 0x80` call from a 64-bit process (it is answered before the filter runs). Wine's own filter already fails to install under FEX.

The test, `eac-vm/fex-tests/wow64-mode/` (a freestanding static x86-64 binary, built like `open-efault`), prints a row
for each step:
1. `int 0x80` `set_thread_area` with entry -1 gives a TLS entry; `get_thread_area` returns its base.
2. `mov %fs` with that selector in 64-bit code.
3. `iretq` to `0x23`: 32-bit code runs encodings only 32-bit mode has (`inc`/`dec` at `0x40`-`0x4f`, `pusha`/`popa`)
   and reads `%fs:0`.
4. `ud2` in the 32-bit code: the 64-bit SIGILL handler sees `cs` `0x23` and the `ud2`'s address, skips it, and
   `sigreturn` resumes the 32-bit code.
5. A far jump back to `0x33`, and the program exits 0.

On today's FEX it fails at step 1 (`-ENOSYS`).

## 5. M2: the WoW64 guest Wine

- `eac-vm/wine/build.sh` configures `--enable-archs=i386,x86_64` with `i386_CC="clang --target=i686-windows
  -fuse-ld=lld"`. `wine.erofs` gains the `i386-windows` DLLs (its size is measured); the root filesystem, the recipes
  and `vm-setup` don't change. The image carries M1's FEX.
- Prefixes: a new one gets `syswow64` from `wineboot`; an existing 64-bit-only prefix is upgraded by the `wineboot -u`
  that `run-game.sh` already runs.
- A 32-bit process's Direct3D is Wine's own builtins (wined3d on llvmpipe, software) until M4. `run-game.sh` sets
  `GALLIUM_OVERRIDE_CPU_CAPS=sse4.1`, without which llvmpipe crashes under FEX. `prefix.sh`'s forwarded overrides become
  `n,b`, so a 32-bit process, which finds no forwarding DLLs in `syswow64`, falls back to the builtins.
- The launcher: Preflight lets a 32-bit game through when it runs in the VM (the macOS path still refuses it), and the
  `run` verb stops skipping 32-bit redistributable installers in the VM, as Proton runs them.
- `eac-vm/tests/eac.sh` takes the game as parameters (app id, folder, executable, arguments) instead of SMITE 2's, keeps
  the row "`easyanticheat_x64.so` mapped in a Wine process", and reports the EAC launcher's own result.

## 6. M3: Steam for 32-bit games

- The spike: `mn-steam-host` (or the process that hosts it) holds a low-4 GiB region the way the Mac Wine does, with
  the cross-architecture entitlement and MacNeutron's provisioning profile.
- The guest forwarder registers `__wine_unix_call_wow64_funcs`. `steam-forward/gen.py` makes descriptors from the
  `wow64_*` parameter structs: their 32-bit pointers are copied into the host's low region, and pointers the host
  returns are copied back into guest memory below 4 GiB.
- The host calls the Mac `lsteamclient.so`'s `___wine_unix_call_wow64_funcs`.
- If the spike fails: the guest translates each WoW64 call to its 64-bit call (generated from lsteamclient's paired
  structs, with the 32-bit and 64-bit Windows struct layouts converted), and the host stays as it is.
- The i386 PE half of lsteamclient (`steamclient.dll`, `lsteamclient.dll`) is built into the guest's Wine.

## 7. M4: Direct3D for 32-bit games

- Guest mode: DXMT built for i386 goes into `wine.erofs` over Wine's i386 `d3d*` DLLs. The guest `winemetal.so`
  (`eac-vm/metal/wm`) gets a WoW64 table that turns DXMT's 32-bit parameters into the existing 64-bit slots, converting
  as DXMT's own `winemetal_unix.c` WoW64 table does.
- Forwarded mode: `eac-vm/d3d/gen.py` also writes i386 proxies (`mnd3d.dll` and its stand-ins, installed in
  `syswow64`) that widen 32-bit structs (pointers, `SIZE_T`, packing) to the existing 64-bit wire; the host's stubs don't
  change. `prefix.sh` links them into `syswow64`.

## 8. The final gates (after M4)

New:
- `eac-vm/fex-tests/wow64-mode` passes under the guest's FEX in the builder and in the microVM, and fails on 0029's FEX.
- A 32-bit console program and a 32-bit D3D11 clear-and-present program run in the VM; a 64-bit-only prefix upgrades.
- Rogue Company: the EAC launcher starts in the VM and reaches EAC's own verdict, recorded as it is.
- A 32-bit Steam probe (app 480: init, Steam ID, auth ticket) through a 32-bit `steam_api.dll`.
- The D3D smoke's programs built for i386 match their 64-bit rows, in guest and forwarded mode; one 32-bit D3D11 game
  plays.

Regression (unchanged results): the other `eac-vm/fex-tests`, `m2-gate.sh`, `wine.sh`, `steam-forward.sh`, the 64-bit
probe, `d3d-smoke.sh` in both modes, `eac.sh` for SMITE 2 (301 then 60101, as since 2026-10-16), `vm-setup.sh`, and the
launcher's unit tests.

## 9. Risks

- Signals that land inside a 32-bit block: FEX rebuilds the guest state through register sets sized for the process
  (64-bit, a superset), which should hold; the test's step 4 exercises the path.
- Wine's 32-bit allocations need the low 4 GiB free; FEX's own mappings in a 64-bit process could land there. M2 checks
  where FEX maps before Wine's preloader reserves.
- EAC's 32-bit launcher under new WoW64 is untested by Valve's default (Proton runs old WoW64). Its verdict is the
  result either way.
- The cross-architecture entitlement for the Steam host may not be granted for a non-app binary; §6's fallback covers
  it.
