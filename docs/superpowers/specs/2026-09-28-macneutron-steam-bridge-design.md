# MacNeutron — Sub-project 2: Steam API Bridge

- **Date:** 2026-09-28
- **Status:** Approved 2026-09-28; amended the same day (see "Amendment")
- Superseded in part by `2026-10-04-macneutron-arm64-release-design.md` (the Rosetta runtime, GPTK, DXVK and the x86_64 DXMT build were removed in 0.1.0).
- **Builds on:** `2026-09-27-macproton-runtime-design.md` (architecture, runtime, launcher) and `2026-09-27-macneutron-app-design.md` (app, Steam Play mode).
- **Scope:**
  - **In:** our own `steam.exe`; wiring the runtime's Steam client bridge into every prefix; launcher integration; getting `steam.exe` from the app into the tool folder; a developer probe; acceptance.
  - **Out:** building or patching `lsteamclient` (the runtime provides it, §2.8); the Steam overlay; Easy Anti-Cheat and other anti-cheat runtimes; VR (`vrclient`); the repo's own license (§11).

## Amendment (2026-09-28, while planning)

The approved design planned to build Proton's `lsteamclient` for macOS ourselves (fetch Proton, build winecx, patch, bundle, ship under the Steamworks SDK license). While planning, §2.8 turned up: **the pinned runtime already ships a macOS build of `lsteamclient`**, ported by the runtime's maintainer, including the exact patch we planned. So:

- Dropped: fetching Proton and winecx, building `lsteamclient`, our patches, bundling it in the app, the bridge version file, and the licensing decision (the runtime's distributor carries it).
- Kept: `steam.exe`, the prefix layout, launcher integration, the probe, acceptance.
- Changed: the Windows helpers are built with Homebrew's `mingw-w64` (already required by `Tests/Smoke`), not llvm-mingw, so there are no downloads.

## 1. Goal

A Windows game started from native macOS Steam through MacNeutron can use the Steam API: `SteamAPI_Init` succeeds, and ownership/DRM checks, achievements, cloud saves, inventory and auth session tickets reach the user's running Mac Steam client. Games stop reporting "Steam unavailable".

**Done when** the acceptance run in §9 passes. This completes the v1 goal in the runtime spec: "a Windows-only Steam game that uses the Steam API (DRM, achievements) installs and launches from native macOS Steam's Play button".

### Decisions

| Decision | Choice |
|---|---|
| Steam client bridge | The runtime's own `lsteamclient` (§2.8); we neither build nor ship it |
| "Steam is running" | Our own `steam.exe` (open source, not Proton's `steam_helper`) |
| Windows helper toolchain | Homebrew `mingw-w64` (`x86_64-w64-mingw32-gcc`), as `Tests/Smoke` already uses |
| Shipping `steam.exe` | Inside `MacNeutron.app/Contents/Resources/` (codesign accepts only Mach-O code in `Contents/Helpers`), installed into the tool folder with the launcher |
| 32-bit games | `steamclient.dll` is copied whenever the runtime has an i386 build; 32-bit is recorded as untested unless a 32-bit Steam game is available |
| Escape hatch | `MACNEUTRON_NO_STEAM_BRIDGE=1` in a game's launch options starts the game directly |

## 2. Evidence (verified 2026-09-28)

1. macOS Steam's `Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib` is universal (`x86_64 arm64`), and so are its dependencies `libtier0_s.dylib`, `libvstdlib_s.dylib` and `libaudio.dylib`.
2. Its x86_64 slice exports `CreateInterface`, `Steam_BGetCallback`, `Steam_GetAPICallResult`, `Steam_FreeLastCallback`, `Steam_ReleaseThreadLocalMemory` and the other `Steam_*` entry points, but **not** `Steam_IsKnownInterface` or `Steam_NotifyMissingInterface`.
3. Proton's `lsteamclient` (`proton_11.0`, `5b89db9`) has an `#ifdef __APPLE__` path that loads `$STEAM_COMPAT_CLIENT_INSTALL_PATH/steamclient.dylib`, but requires the two functions above.
4. `lsteamclient/LICENSE` is the Steamworks SDK license.
5. The runtime `runtime-v4.7.3` was built from `dappermint/winecx` `e0aa380780b73e20fabcfe78fd42713b94929a53`.
6. SMITE 2 ships `Engine/Binaries/ThirdParty/Steamworks/Steamv157/Win64/steam_api64.dll` (SDK 1.57: `SteamClient017`, `SteamUser021`) and reports "Steam unavailable" under the current runtime.
7. Mac Steam sets `STEAM_COMPAT_CLIENT_INSTALL_PATH` for compat tools (runtime spec §2); its value on macOS has not been recorded.
8. **The runtime ships the bridge.** `Libraries/Wine/lib/wine/` contains `x86_64-unix/lsteamclient.so` (Mach-O x86_64, linked to `@rpath/ntdll.so` and libc++), `x86_64-windows/lsteamclient.dll` (PE32+) and `i386-windows/lsteamclient.dll` (PE32). winecx added them in four commits on 2026-08-25 ("add proton's steam client bridge", "build the bridge on macos", "link the unix half against libc++", "do not require the proton only client exports"). Its `unixlib.cpp` loads `steamclient.dylib` from `STEAM_COMPAT_CLIENT_INSTALL_PATH`, treats the two missing functions as optional (logging "host steamclient has no …, carrying on without it"), answers `Steam_IsKnownInterface` with 1 when absent, and skips `Steam_NotifyMissingInterface`.
9. In SMITE 2's prefix, `wineboot` placed `lsteamclient.dll` in `system32`, but there is no `C:\Program Files (x86)\Steam` and no `HKCU\Software\Valve\Steam` key. Nothing currently tells `steam_api64.dll` that Steam runs or where its client DLL is. That is the whole gap.
10. The maintainer's `loginusers.vdf` has one account and no `MostRecent` key.

## 3. Architecture

```
Mac Steam (Linux mode) ──Play──▶ macneutron launcher (Swift)
                                    │  copies into the prefix, every launch:
                                    │    <tool>/bin/steam.exe             → C:\Program Files (x86)\Steam\steam.exe
                                    │    runtime x86_64 lsteamclient.dll  → …\Steam\steamclient64.dll
                                    │    runtime i386 lsteamclient.dll    → …\Steam\steamclient.dll (when present)
                                    │  STEAM_COMPAT_CLIENT_INSTALL_PATH → folder containing steamclient.dylib
                                    │  MACNEUTRON_STEAM_ACCOUNT ← account ID from Steam's loginusers.vdf
                                    ▼
                                 wine "C:\Program Files (x86)\Steam\steam.exe" "Z:\…\game.exe" <args>
                                    │  writes HKCU\Software\Valve\Steam (ActiveProcess pid, client DLL paths, user)
                                    │  starts the game; waits until no program is left in the prefix; clears pid
                                    ▼
game.exe ─▶ its own steam_api64.dll ─▶ steamclient64.dll (runtime's lsteamclient, Windows side)
                                          │ Wine unixlib call
                                          ▼
                                       lsteamclient.so (runtime, Mac x86_64 under Rosetta)
                                          │ dlopen
                                          ▼
                                       Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib (x86_64 slice)
                                          │ Steam's own local IPC
                                          ▼
                                       running Mac Steam client (arm64)
```

Only `run` and `waitforexitandrun` go through `steam.exe`, as in Proton. `runinprefix`, `getcompatpath` and `getnativepath` are unchanged. The launcher passes the program as a Windows path (`Z:` plus the Unix path with backslashes); `Z:` is Wine's standard mapping of `/`.

## 4. Components

### 4.1 `lsteamclient`

Provided by the runtime (§2.8). MacNeutron only copies it into prefixes. If a future runtime drops it, the launcher logs `note: Steam bridge not installed` and starts games directly.

### 4.2 `steam.exe` (ours)

`bridge/steam.c`, a Windows console program built with `x86_64-w64-mingw32-gcc`.

- **Usage:** `steam.exe <program> [args…]`, where `<program>` is a Windows path.
- **Registry values written:**

  | Key | Value | Data |
  |---|---|---|
  | `HKCU\Software\Valve\Steam` | `SteamPath` | `C:\Program Files (x86)\Steam` |
  | `HKCU\Software\Valve\Steam` | `SteamExe` | `C:\Program Files (x86)\Steam\steam.exe` |
  | `…\ActiveProcess` | `pid` | its own process ID |
  | `…\ActiveProcess` | `SteamClientDll64` | `C:\Program Files (x86)\Steam\steamclient64.dll` |
  | `…\ActiveProcess` | `SteamClientDll` | `C:\Program Files (x86)\Steam\steamclient.dll`, only when that file exists |
  | `…\ActiveProcess` | `ActiveUser` | `MACNEUTRON_STEAM_ACCOUNT`, only when set |
  | `HKLM\Software\Wow6432Node\Valve\Steam` | `InstallPath` | `C:\Program Files (x86)\Steam` |

- **Command line:** everything after its own name in its command line, passed verbatim as the child's command line, so the game gets exactly the arguments Wine built.
- **Waiting:** it starts the child suspended, then resumes it. Then, as Proton's `steam.exe` does, it becomes a Wine system process and waits on Wine's shutdown event, which Wine sets once no user process is left in the prefix, because launchers start the real game and exit (also with `CREATE_BREAKAWAY_FROM_JOB` or ShellExecute). It leaves its console first (`FreeConsole`): under a terminal, Wine's console host would wait for it. If Wine refuses the shutdown event, it waits for the child only.
- **Exit:** sets `ActiveProcess\pid` to 0 only if it still holds its own PID, then exits with the child's exit code. If the child can't be started, it prints the command line and Windows error to stderr and exits 1. With no program, it prints usage and exits 1.

### 4.3 `steamprobe.exe` (developer only, never shipped)

`bridge/probe.c`. `bridge/probe.sh <steam_api64.dll>` prepares a scratch prefix exactly as the launcher does and runs `steam.exe steamprobe.exe <dll>` with `SteamAppId=480` (Valve's public test app). The probe uses only flat C exports looked up with `GetProcAddress`, so it needs no SDK headers. It:

1. calls `SteamAPI_ManualDispatch_Init`, then `SteamAPI_Init` (or `SteamAPI_InitFlat` on newer SDKs);
2. finds `SteamAPI_SteamUser_v0NN` and `SteamAPI_SteamFriends_v0NN` among the DLL's exports, trying the newest version first;
3. prints the SteamID and persona name;
4. calls `GetAuthSessionTicket` and pumps manual dispatch for up to 10 s, waiting for callback 163 (`GetAuthSessionTicketResponse_t`).

It exits 0 only when all four succeed.

## 5. Build: `make bridge`

`make bridge` compiles `bridge/steam.c`, `bridge/probe.c` and `bridge/tests/helper.c` with Homebrew's `x86_64-w64-mingw32-gcc -O2 -static -s` into `build/bridge/`. If the compiler is missing, the error says `brew install mingw-w64`. `make bridge-check` runs `bridge/check.sh` against the installed runtime (§9).

## 6. Shipping and installation

- **App bundle.** `make app` depends on `make bridge` and copies `build/bridge/steam.exe` to `MacNeutron.app/Contents/Resources/steam.exe`. It can't sit next to the `macneutron` CLI in `Contents/Helpers`: codesign accepts only signed Mach-O code there.
- **Tool folder.** `RuntimeInstaller.writeToolFiles` installs the launcher and `steam.exe` into `<tool>/bin/`, taking `steam.exe` from next to the launcher it was given or, for the app's `Contents/Helpers/macneutron`, from `Contents/Resources`. Each file is written to a temporary name and renamed into place, and skipped when identical, so a game launching at that moment never sees a missing launcher. It runs on every runtime install and at every app start when a runtime is installed. That way an updated app updates the launcher and `steam.exe` without a reinstall.
- **Prefix.** Before `run` and `waitforexitandrun`, when the bridge is enabled, `PrefixManager.prepare` copies the files in §3 into `drive_c/Program Files (x86)/Steam/`, every launch, the same way it deploys graphics DLLs.

## 7. Launcher integration

- **When.** The bridge is enabled for `run` and `waitforexitandrun` when `ToolLayout.steamBridgeInstalled` (`bin/steam.exe`, the runtime's `x86_64-unix/lsteamclient.so` and `x86_64-windows/lsteamclient.dll` all exist) and `MACNEUTRON_NO_STEAM_BRIDGE` isn't `1`.
- **Command.** `wine "C:\Program Files (x86)\Steam\steam.exe" <Windows path of target> <args…>`; otherwise `wine <target> <args…>` as today.
- **Steam's client path.** `STEAM_COMPAT_CLIENT_INSTALL_PATH` keeps Steam's value when that folder holds `steamclient.dylib`, and is otherwise set to `SteamLocation.bundleMacOS` (no trailing slash). The launcher log records the folder used and what Steam passed (`note: Steam client folder <folder> (Steam passed <value or nothing>)`), and says so when that folder has no `steamclient.dylib`.
- **Account.** `SteamLocation.activeAccountID()` reads `config/loginusers.vdf`. It takes the user with `MostRecent` `1`, otherwise the one with the largest `Timestamp`, and returns the low 32 bits of that user's SteamID64. The launcher sets `MACNEUTRON_STEAM_ACCOUNT` to it unless the variable is already set; when none is found, the launcher log says so.
- **Logging.** `MACNEUTRON_LOG=1` makes the default `WINEDEBUG` `+err,+warn,+loaddll,+steamclient`.

## 8. Errors

| Condition | Behavior |
|---|---|
| Runtime or tool folder lacks a bridge file | Game starts directly, with leftover bridge files removed from the prefix (as for the escape hatch); launcher log: `note: Steam bridge not installed` |
| `MACNEUTRON_NO_STEAM_BRIDGE=1` | Game starts directly, and `steam.exe`, `steamclient64.dll` and `steamclient.dll` are removed from the prefix: otherwise the game's `steam_api` loads the leftover client DLL (its registry values persist) and the bridge aborts the game for want of Steam's client path (seen in acceptance). Launcher log: `note: Steam bridge disabled by launch option` |
| No `steamclient.dylib` in the folder used | Launcher log: `note: steamclient.dylib not found in <folder>`; the game still starts |
| No readable `loginusers.vdf` or no user in it | `ActiveUser` not written; launcher log: `note: no Steam account found in loginusers.vdf` |
| Launcher-style game (first process exits) | `steam.exe` keeps Steam "running" until no program is left in the prefix |
| Game crashes | `steam.exe` still clears its pid; the exit code passes through |
| `steam.exe` killed | The stale pid is overwritten at the next launch |
| Copying bridge files into a prefix fails | Launch fails with `could not install the Steam bridge: <file>: <reason>`, like other prefix-preparation errors |

## 9. Testing

- **Swift unit tests** (test-first, `Tests/MacNeutronCoreTests`):
  - `windowsPath`;
  - `clientDirectory` keeps a valid Steam value and replaces an invalid one;
  - `activeAccountID`: `MostRecent` wins, newest `Timestamp` otherwise, low 32 bits, `nil` for a missing file or no users;
  - `WINEDEBUG` gains `+steamclient`;
  - `steamBridgeInstalled`;
  - `PrefixManager` copies `steam.exe`, `steamclient64.dll` and `steamclient.dll`, and skips the last when there's no i386 build;
  - launcher: `steam.exe` command and environment for `run` and `waitforexitandrun`; direct start for `runinprefix`, the escape hatch and a missing bridge, each with its log note;
  - `writeToolFiles` installs `steam.exe` from next to the launcher, replaces a changed launcher, and succeeds without a `steam.exe`.
- **`bridge/check.sh`** (`make bridge-check`) runs `steam.exe` under the real installed runtime, without Steam, from a folder whose name has a space and a non-ASCII letter:
  - the exit code passes through;
  - arguments with spaces, quotes and an empty string arrive unchanged;
  - the registry values in §4.2 are written, and the pid names a live process;
  - a launcher-style child that outlives its parent still sees a live Steam pid;
  - the pid is 0 afterwards;
  - a missing program exits 1.
- **Probe** (§4.3) on a Mac with Steam running and logged in.

**Acceptance on the maintainer's Mac,** recorded in `docs/testing/acceptance-bridge.md`:

1. The probe prints `SteamAPI_Init` success, the account's SteamID and persona name, and receives the auth-ticket callback.
2. SMITE 2 no longer reports "Steam unavailable". Whether its anti-cheat then blocks login is recorded, not graded.
3. A Steam API game without anti-cheat works fully: Bongo Cat's Windows build (via "Runs as: Windows") loads its inventory. If that build doesn't use the Steam API, another owned Windows game that does is chosen and named in the record.
4. Timberborn (Mac native) still launches natively; `MACNEUTRON_NO_STEAM_BRIDGE=1` starts a game without `steam.exe` (visible in the launcher log).
5. The value Mac Steam passes in `STEAM_COMPAT_CLIENT_INSTALL_PATH` is recorded (§2.7).

## 10. Feasibility gate: decision rules

The probe runs (plan task 2) before any Swift change:

| Question | Result → decision |
|---|---|
| Does the probe print a SteamID? | Yes → continue. No → stop and report; the rest isn't built on a guess |
| Does `SteamAPI_Init` need `ActiveUser`? (probe with and without `MACNEUTRON_STEAM_ACCOUNT`) | Either way, keep writing it when known; record the answer |
| Does Wine load the prefix copy `steamclient64.dll` and find `lsteamclient.so`? | Yes → §6 as written. No → point `SteamClientDll64` at `C:\windows\system32\lsteamclient.dll` (placed there by `wineboot`), drop the DLL copies, record the change |

## 11. Risks

- **The runtime drops or breaks the bridge.** The launcher detects missing files and falls back to a direct start. A broken bridge shows up in `+steamclient` logs. The pinned runtime changes only when MacNeutron updates `RuntimePin`, and a pin update must re-run acceptance item 1.
- **License.** `lsteamclient` stays the runtime distributor's (Steamworks SDK license); MacNeutron doesn't redistribute it. The repo has no `LICENSE` file yet; our own code (`steam.c`, `probe.c`, tests) needs one before public release. That is the maintainer's choice and not part of this plan.
- **Intel slice.** If Valve drops the x86_64 slice of `steamclient.dylib`, the bridge stops working; the launcher's missing-file note won't catch that (the file exists), but `+steamclient` logs `unable to load native steamclient library`. There's no mitigation short of an arm64 Wine.
- **SDK versions newer than the runtime's `lsteamclient`.** Games built against a newer Steamworks SDK may fail to get some interfaces until the runtime updates.
- **Rosetta after macOS 27.** Apple has said that beyond macOS 27 it keeps only a subset of Rosetta aimed at games. The whole runtime depends on that; the bridge adds no new dependency.
