# MacNeutron's side of P0-3 (S: drive) and P1-1 (steam.exe port): current code, change map, breakage

Read-only research. Nothing was edited, built or run, except reading files and fetching two Proton files as text.

**Citation key**
- Repo-relative paths are in the repository root. `MNC/` = `Sources/MacNeutronCore/`, `T/` = `Tests/MacNeutronCoreTests/`, `W/` = `build/wine-arm64-src/wine/`.
- `P:steam.c` and `P:proton.py` are ValveSoftware/Proton at ref `proton_11.0`. The ref exists; the copies were fetched with `gh api` as text and saved as `steam.c.txt` (1063 lines) and `proton.py.txt` (2189 lines) next to this file.
- Spec = `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`.

**Baseline.** The maintainer reports that SMITE 2 and Palworld work today. That is M2's "before" state: Z: paths, steam.exe in `Steam\`, and no command handler. The gate for P0-3 is the same two games plus a launcher-based game after S: lands (spec :26, :584).

**Production environment (names only, read from the game-log headers `~/Library/Logs/MacNeutron/steam-1623730.log` and `steam-2437170.log`, Palworld and SMITE 2):** Mac Steam sets `SteamGameId`, `SteamAppId`, `STEAM_COMPAT_APP_ID`, `STEAM_COMPAT_INSTALL_PATH`, `STEAM_COMPAT_LIBRARY_PATHS` and `STEAM_COMPAT_CLIENT_INSTALL_PATH` at every launch. So a gate on `SteamGameId` and S: detection both work in production. The tests are the problem (section 3).

---

## 1. bridge/steam.c today (269 lines)

### Functions, in file order
| Lines | Function | What it does |
|---|---|---|
| 51-53 | macros | `STEAM_DIR` = `C:\Program Files (x86)\Steam`, `STEAM_KEY`, `ACTIVE_PROCESS_KEY` |
| 55-59 | `create_key` | `RegCreateKeyExW(KEY_ALL_ACCESS)`, NULL on failure |
| 61-69 | `set_string`, `set_dword` | `RegSetValueExW` wrappers |
| 71-125 | `register_steam(pid)` | Writes all registry state (next table) |
| 129-138 | `unregister_steam(pid)` | Sets `ActiveProcess\pid` = 0 only if it still holds our pid. **Proton has no equivalent: it never clears the pid.** |
| 141-143 | NT declarations | `DIRECTORY_BASIC_INFORMATION`, `NtOpenDirectoryObject`, `NtQueryDirectoryObject` |
| 150 | `drm_consume`, `drm_produce` | Globals |
| 153-176 | `find_ack_event` | Walks `\Sessions\1\BaseNamedObjects` for `STEAM_START_ACK_EVENT*`. Proton walks `\BaseNamedObjects\Session\1` (P:steam.c:822-861) |
| 178-194 | `answer_drm` | DIPC loop: waits on CONSUME, sets the ack event, logs to stderr, releases PRODUCE. Proton's `steam_drm_thread` (P:steam.c:863-902) caches the ack handle; ours looks it up again each round |
| 197-210 | `child_command_line` | `GetCommandLineW` minus argv[0] (quoted, or up to the first blank). No escaped-quote handling (Proton's `find_quote` at P:steam.c:247-265 has it) |
| 212-269 | `main(void)` | Wide command line, no argc/argv |

### `main()` order
1. `child_command_line()`. Nothing left means usage and exit 1 (:217, :221-225).
2. `SetEnvironmentVariableW(SteamPath)` (:228). `ValvePlatformMutex` is the **constant** `C:/Program Files (x86)/Steam/steam.exe` (:229-230). Proton uses `GetModuleFileNameW` with `\` changed to `/` (P:steam.c:949-958). This is L7.
3. `PROTON_HIDE_PROCESS_WINDOW`: when the value starts with something other than `0`, set `STARTF_USESHOWWINDOW` + `SW_HIDE` (:233-237). Same rule as Proton's `env_nonzero` (P:steam.c:59-63, 579, 633-637).
4. **`CreateProcessW(NULL, cmd, …, bInheritHandles=TRUE, CREATE_SUSPENDED, …)`** (:240). On failure: stderr message, exit 1, **nothing registered** (:238-244).
   - Proton: `bInheritHandles=FALSE`, `CREATE_UNICODE_ENVIRONMENT`, plus `CREATE_NEW_CONSOLE` for console images from a Unix path (P:steam.c:422, 507-512, 639).
   - Wine still duplicates std handles when inheritance is off (W/server/process.c:1308-1319). So FALSE alone does not lose stdout.
5. `register_steam(pid)` (:245), after the child exists.
6. Named events `Steam3Master_SharedMemLock` and `Global\Valve_SteamIPC_Class` (:249-250).
7. DIPC semaphores `STEAM_DIPC_CONSUME` (0, 512) and `SREAM_DIPC_PRODUCE` (1, 512), then the `answer_drm` thread (:252-254). This is **the SteamStub DIPC answer**, the same names and counts as P:steam.c:871-899.
8. `ResumeThread` (:255).
9. `done = pi.hProcess`, then `NtSetInformationProcess(1000 /* ProcessWineMakeProcessSystem */, &done)` (:260-262). On success Wine replaces `done` with the shutdown event, as Proton does (P:steam.c:1003-1005).
10. `FreeConsole()` (:264), unconditional. Proton calls it only on its wait path (P:steam.c:1027).
11. `WaitForSingleObject(done)` (:265), then `GetExitCodeProcess(child)` (:266), then `unregister_steam` (:267), then return the child's code (:268).

### What `register_steam` writes (all on every run; no `SteamGameId` gate)
- `HKCU\Software\Valve\Steam`: `SteamPath`, `SteamExe` = `…\Steam\steam.exe`, and `language` from `MACNEUTRON_STEAM_LANGUAGE` (:78-85).
- `HKCU\…\Steam\Apps\<SteamAppId>`: `Installed=1`, `Running=1`, `Updating=0`, only when SteamAppId is nonzero (:89-97).
- `HKCU\…\ActiveProcess`:
  - `pid`;
  - `SteamClientDll64` (always);
  - `SteamClientDll`, only if `steamclient.dll` exists;
  - **`ActiveUser` from `MACNEUTRON_STEAM_ACCOUNT`** (:98-107). L2 says to drop it.
- `HKLM\Software\Wow6432Node\Valve\Steam\InstallPath` (:108-112).
- **The winebrowser `HKCR\steam` route:** `HKCR\steam` gets `URL Protocol` = "" (:115-119), and `HKCR\steam\shell\open\command` (default value) is `"C:\windows\system32\winebrowser.exe" "%1"` (:120-124).
  - winebrowser tries the `HKCU\Software\Wine\WineBrowser\Browsers` list, or by default `xdg-open` and then `/usr/bin/open` (W/programs/winebrowser/main.c:114-137).
  - It runs the first one that starts, through `__wine_unix_spawnvp(argv, FALSE)` (main.c:66-96).
  - Proton's value is `"%16426%\Steam\Steam.exe" -- "%1"`, where `%16426%` is `C:\Program Files (x86)`. It is in ValveSoftware/wine `proton_11.0` `loader/wine.inf.in:316`, fetched as `vw-wine.inf.in.txt` next to this file.

### How it differs from Proton's main (P:steam.c:916-1063)
| Proton | Ours |
|---|---|
| `steam_command_handler` runs first and returns 0 for `steam://…` and `-…` arguments (928-929, 652-748) | No handler. `steam.exe -- steam://…` tries to start a program named `--` and exits 1 |
| Setup happens only `if (getenv("SteamGameId"))` (931-980) | Always |
| The pid is set **before** the child starts (946) | After a suspended `CreateProcessW` succeeds (:238-245) |
| `vguiPopupWindow` "Steam" plus a "SteamVR Status" static window on a thread, after `GetDesktopWindow()` (92-111, 941-944) | None |
| `GamingRepair` (75-90, 947), `steamclient_init_registry` (206-221, 960), `setup_steam_files` (750-816, 961), `PROTON_WAIT_ATTACH` (963-977), `setup_vr_registry` (184-204, 987) | None. The launcher writes the Steam files (§5) |
| `run_process`: Unix-path conversion, ShellExecute for non-`.exe` targets, EA workarounds, `-game` fix-up (417-649) | `CreateProcessW` only |
| DRM thread only for the game process (999-1000) | Always |
| Make-process-system only for the game process (1003-1005) | Always |
| Restart loop on `PROTON_STEAM_EXE_RESTART_APP` (1013-1047) | None |
| Never clears the pid | `unregister_steam` |

**A Proton bug not to port:** P:steam.c:1028 reads `while ((ret = WaitForMultipleObjects(...) != WAIT_OBJECT_0))`. Because of operator precedence, `ret` is 0 or 1, so `WAIT_FAILED` is treated as a restart (1032). Write `(ret = WaitForMultipleObjects(...)) != WAIT_OBJECT_0`.

---

## 2. How steam.exe is built (there is no `bridge/Makefile`)

- The root `Makefile` has it:
  - `MINGW_A64` = `$(MINGW_BIN)/aarch64-w64-mingw32-clang -O2 -static -s $(PE_MARCH)` (Makefile:10), with `PE_MARCH` = `-march=armv8.5-a+fp16fml+aes+sha3` (:7). The toolchain is llvm-mingw from `dxmt/toolchain.sh` (:4-5).
  - The `bridge:` target (:27-31) builds `build/bridge/arm64/steam.exe` from `bridge/steam.c` with **`-ladvapi32 -lntdll`** (:30).
  - It builds the helper `build/bridge/arm64/tests/helper.exe` with `-ladvapi32 -lshell32` (:31). It is a console-subsystem PE: there is no `-mwindows`.
  - It also builds the x64 `steamprobe.exe` (:29).
- `bridge-check:` runs `probe.sh --redact-self-test`, then `check.sh` (:34-36).
- The `app:` target copies steam.exe into `Contents/Resources/steam.exe` (:95). `macneutron install --steam-exe` puts it at the tool's `bin/steam.exe` (MNC/CommandLineTool.swift:29-38; MNC/RuntimeInstaller.swift:159-172; MNC/ToolLayout.swift:42).
- **What the port needs:**
  - `-lshell32` (`ShellExecuteW`, `SHGetFileInfoW`), `-lole32` (`CoInitialize`, if kept), `-luser32` (windows), and `-lshlwapi` (`StrStrIA`), or a local case-insensitive prefix test instead.
  - `-lmsi` for the EA uninstall branch's `MsiConfigureProductExW` (P:steam.c:616), or a `GetProcAddress` lookup.
  - Services come from advapi32, which is already linked.
  - All of these import libraries exist in `build/dxmt-src/llvm-mingw/aarch64-w64-mingw32/lib`.
  - There is no `wine/debug.h`; keep the `fprintf(stderr)` style.

---

## 3. bridge/check.sh and bridge/tests/helper.c

### How check.sh runs under Wine without Steam
- **Modes** (:2-9):
  - Tool-folder mode. `MACNEUTRON_TOOL_DIR`, or one assembled at `$WORK/tool` by `macneutron install --tool-dir … --wine-app build/wine-arm64/wine.app --steam-exe build/bridge/arm64/steam.exe` (:22-32). It runs the direct rows and then the launcher rows.
  - arm64 mode. `MACNEUTRON_ARM64_APP` plus `MACNEUTRON_ARM64_PREFIX`; direct rows only (:16-21).
- `WORK` defaults to `${TMPDIR}/macneutron bridge ü`: a space and a non-ASCII letter, on purpose (:12).
- **The prefix:**
  - In tool-folder mode the launcher prepares it: `env STEAM_COMPAT_DATA_PATH="$COMPAT" SteamAppId=0 macneutron launch getcompatpath "$WORK"` (:38-40). getcompatpath installs no bridge.
  - Then `WINEDEBUG=-all WINEMSYNC=1` (:42).
  - steam.exe is copied by hand to `drive_c/Program Files (x86)/Steam/steam.exe` (:45-47). The helper becomes `$WORK/game dir/hélper.exe` (:48).
- **Paths:** `winpath()` makes a `Z:` path (:49), and `HELPER` is the helper's `Z:` path (:50). `STEAMEXE='C:\Program Files (x86)\Steam\steam.exe'` (:51). `steam()` runs `"$WINE" "$STEAMEXE" "$HELPER" "$@"` (:63).
- **The stub `open(1)`:** it is **created at :52-61**: `$WORK/bin/open` writes its arguments to `$WORK/open.txt`, and `HKCU\Software\Wine\WineBrowser\Browsers` points at it (:60). It is **exercised at :95-99** (the spec's "97-99" is the test, not the stub). It intercepts only the winebrowser route.
- **How the STEAM_COMPAT_* variables are set:**
  - Always inline `env` assignments: `STEAM_COMPAT_DATA_PATH` at :39 and :116.
  - `STEAM_COMPAT_LIBRARY_PATHS` only at :137, for one launcher call.
  - **`STEAM_COMPAT_INSTALL_PATH` and `SteamGameId` are never set** anywhere in check.sh.
  - `SteamAppId` is 0 (:39, :116), or `$$` for the "app" row (:93).
  - The launcher fills `STEAM_COMPAT_CLIENT_INSTALL_PATH` from the real `SteamLocation()` (MNC/Launcher.swift:184-186).
  - `bridge/probe.sh:56` does export `SteamAppId` and `SteamGameId` (480).
- **Callers:**
  - `make bridge-check` (Makefile:34-36).
  - `wine-arm64/check.sh` gate S7 runs it in arm64 mode (:426-430).
  - Gate L3 runs it through a tool folder (:436-440).
  - Gate L3 then runs `probe.sh` through the launcher with a **macOS path** to `steamprobe.exe` (probe.sh:62-63), and `probe_rows` reads its **stdout** (wine-arm64/check.sh:404-417, 441-443).

### helper.c modes (`main` :36-169; the helper is a console PE, per Makefile:31)
| Mode | Lines | Output |
|---|---|---|
| `exit N` | 42 | Exit code N |
| `args …` | 43-48 | `[a][b]…` on stdout |
| `steam` | 49-58 | `alive=%d user=<ActiveUser> client64=<SteamClientDll64>` |
| `pid` | 59-63 | `ActiveProcess\pid` |
| `spawn <file>` | 64-74 | Starts itself as `late <file>` and exits (a launcher) |
| `env NAME…` | 75-84 | `[value]…` |
| `hidden` | 85-91 | `hidden=0/1` from `GetStartupInfoW` |
| `events` | 92-98 | `lock=1 ipc=1` |
| `app <id>` | 99-114 | `installed= running= updating= language=` |
| `url <url>` | 115-120 | `ShellExecuteW(open, url)`, then `url ok` / `url failed` |
| `breakaway <file>` | 121-137 | `CREATE_BREAKAWAY_FROM_JOB` child, then `breakaway ok` |
| `drm` | 138-156 | The DIPC handshake, then `ack=1` |
| `late <file>` | 157-166 | Sleeps 1.5 s, then writes `alive=%d` to the file |
| anything else | 167-168 | Exit 3 |

### Every check.sh row
Direct rows (`steam()` at :63, Z: helper path):

| Line | Row | Expects |
|---|---|---|
| 69-70 | exit code passes through | 7 |
| 71 | arguments arrive unchanged | `[a b][--name="Player One"][]` |
| 72-73 | registry names a live Steam | `alive=1 user=12345 client64=C:\…\steamclient64.dll` |
| 74-76 | launcher-style child still sees Steam | `late.txt`: `alive=1` |
| 77 | pid cleared afterwards | helper `pid` run with no steam.exe: `0` |
| 78-79 | missing program exits 1 | `C:\missing.exe`: 1 |
| 80-83 | breakaway, and that game still sees Steam | `breakaway ok`, `alive=1` |
| 84 | SteamStub DRM handshake | `ack=1` |
| 85-86 | SteamPath and ValvePlatformMutex | `[C:\Program Files (x86)\Steam][C:/Program Files (x86)/Steam/steam.exe]` |
| 87-90 | PROTON_HIDE_PROCESS_WINDOW | `hidden=1 hidden=0` |
| 91 | named events | `lock=1 ipc=1` |
| 92-94 | Apps key and language | `installed=1 running=1 updating=0 language=français $$` |
| 95-99 | steam:// link opens via open(1) | `url ok`, then `open.txt` = `steam://store/480`. :96 deletes `HKCR\steam` first |
| 100-108 | steam.exe ends under a terminal (`script(1)`) | 5 |
| 147-158 | a second steam.exe doesn't end Steam for the first one's game | `alive=1` |

Launcher rows (:114-145, tool-folder mode only). `L()` sets `STEAM_COMPAT_DATA_PATH`, `SteamAppId=0`, `MACNEUTRON_STEAM_ACCOUNT=12345` and `MACNEUTRON_STEAM_LANGUAGE=english` (:116-117). `lsteam` runs `L waitforexitandrun "$GAME"`, where GAME is a **macOS path** that today's launcher converts (:113, :115, :118).

| Line | Row |
|---|---|
| 119-120 | exit code |
| 121 | arguments (stdout) |
| 122-123 | registry (stdout) |
| 124-126 | launcher-style child |
| 127 | pid cleared (through `runinprefix`) |
| 128-129 | missing program (Windows path) |
| 130-133 | breakaway |
| 134-143 | `libraryfolders.vdf` with `Z:` paths for `$WORK/library ü` and `/Volumes/Game Drive/SteamLibrary`. The launcher writes it today |
| 144 | `Steam\config` exists |

---

## 4. Launcher.swift (and the argv steam.exe receives)

- `launch` (MNC/Launcher.swift:36-133):
  1. `LaunchEnvironment.build` (:68);
  2. `usesSteamBridge` (:69, 168-179). This is only `run` and `waitforexitandrun`, off with `MACNEUTRON_NO_STEAM_BRIDGE=1` or with no bridge installed;
  3. `addSteamClient` (:70, 183-198);
  4. `prefix.prepare(environment: env, steamBridge:)` (:86, :93, also :121 for getcompatpath/getnativepath);
  5. `removeSteamBridge` when there is no bridge (:87, :94);
  6. `runGame` (:88, :114).

  runinprefix never prepares and never uses the bridge (:83-84).
- **`runGame` (:152-157), with line :155 as the conversion:**
  ```swift
  let command = throughSteam ? [SteamBridge.steamExe, SteamBridge.windowsPath(request.target)] + request.arguments : game
  ```
  - So today's argv to wine is `["C:\Program Files (x86)\Steam\steam.exe", "Z:\…\Game.exe", args…]`, run as `runner.run(layout.wine, command, environment: env, output: gameLog)` (:156).
  - Wine then quotes argv[0], and any entry that contains a blank or is empty, when it builds the Windows command line (W/dlls/ntdll/unix/env.c:1476-1510, test at :1494). So a Steam path with `Application Support` reaches steam.exe quoted and takes the quoted branch (P:steam.c:459-469), not the unquoted branch that splits at the first space (:470-477).
  - A direct start passes the raw Unix path, and Wine converts it itself (W/dlls/ntdll/unix/file.c:4428 `get_full_path` → `find_drive_nt_root`).
- `addSteamClient` (:183-198):
  - It overrides `STEAM_COMPAT_CLIENT_INSTALL_PATH` with the bundle's MacOS folder (:184-186).
  - It sets `MACNEUTRON_STEAM_LANGUAGE` from `registry.vdf` (:191).
  - It sets `MACNEUTRON_STEAM_ACCOUNT` from `loginusers.vdf` (:192-197); this is L2.
  - `redactedKeys` holds `MACNEUTRON_STEAM_ACCOUNT` (:202).

## 5. SteamBridge.swift
- `steamExe` = `C:\Program Files (x86)\Steam\steam.exe` (MNC/SteamBridge.swift:7).
- `windowsPath`: prepends `Z:` to an absolute path and changes `/` to `\` (:10-12). Its callers are Launcher.swift:155 and `libraryFolders` (:37).
- `prefixFolder` = `drive_c/Program Files (x86)/Steam` (:15).
- `steamClient64Variable` = `MACNEUTRON_STEAMCLIENT64` (:20).
- **`prefixFiles`** (:23-26) is `[(layout.steamHelper → Steam/steam.exe), (steamClient64 ?? layout.lsteamclient → Steam/steamclient64.dll)]`.
- **`libraryFolders(steamapps:)`** (:33-42):
  - one entry per `STEAM_COMPAT_LIBRARY_PATHS` item, using the parent of each `steamapps`;
  - empty entries and `/steamapps` are dropped;
  - each path goes through `windowsPath`, so it is `Z:`;
  - entry 0 (the client folder) is left out on purpose (D-8);
  - the result is serialised with `KeyValues`.
- `clientDirectory` (:46-51).

## 6. PrefixManager.swift
- **`prepare(environment:steamBridge:)`** (MNC/PrefixManager.swift:54-103):
  - creates the compat folder;
  - takes `withFileLock(lockFile)` (:58);
  - checks the stamp, then prepares in place, stops msync, or renames a Rosetta-era prefix and carries its data (:60-93);
  - runs `ProfileLinks.apply` on every launch (:95-96);
  - **then, only `if steamBridge`: `deploySteamBridge` and `writeSteamFiles` (:97-100)**;
  - returns the link report.
- `prepareNew` (:105-137):
  - `wineboot -u` with mscoree/mshtml disabled;
  - three best-effort `reg add` calls (FEX, ShowCrashDialog, EmulateModeset);
  - the DXMT DLLs into `drive_c/windows/system32` through `install` (:127-135);
  - `wineserver -w`.
- **`deploySteamBridge`** (:202-208) installs each `SteamBridge.prefixFiles` entry. `steamClient64` (:213-237) chooses the override, then `builtin`, then the Valve cache when the marker is present, then nil (lsteamclient).
- **`writeSteamFiles`** (:242-250): creates `Steam/config` and `Steam/steamapps`. If `STEAM_COMPAT_LIBRARY_PATHS` is set, it writes `steamapps/libraryfolders.vdf` = `SteamBridge.libraryFolders(…)`. Best effort.
- **`removeSteamBridge`** (:255-261), under the lock, deletes `Steam/{steam.exe, steamclient64.dll, steamclient.dll}`.
- `install` (:266-281): copies unless the size and mtime (nanoseconds) match, then stamps the source's times.
- `withFileLock` (:286-293).
- Today nothing in `MNC/` touches `dosdevices`.

## 7. LaunchEnvironment.swift, CompatContext.swift, ToolLayout
- `LaunchEnvironment.build` (MNC/LaunchEnvironment.swift:6-35):
  - starts from Steam's environment (:8), so every `STEAM_COMPAT_*`, `SteamGameId` and `PROTON_*` launch option reaches both `prepare` and steam.exe;
  - sets `WINEPREFIX` (:9);
  - removes `STEAM_COMPAT_PROTON` (:12);
  - sets `WINEDLLOVERRIDES` = backend plus libxess, merged with the user's (:16-17). There is **no `steam.exe=b`**; that waits for P1-1 step 5;
  - sets `WINEDEBUG` (:20-24), `WINEMSYNC` (:26-30) and `DXMT_PIPELINE_RECORD` (:31-33).
- `CompatContext` (MNC/CompatContext.swift):
  - `dataPath` comes from `STEAM_COMPAT_DATA_PATH` (:19-22);
  - `appID` is `SteamAppId`, or "0" (:23); `SteamGameId` is not read anywhere in `MNC/`;
  - `prefix` = `<data>/pfx` (:26), plus the `version` and `macneutron.lock` files (:27-28).
- ToolLayout:
  - `steamHelper` = `<tool>/bin/steam.exe` (MNC/ToolLayout.swift:42);
  - `steamBridgeInstalled` is true when steam.exe and both lsteamclient halves exist (:45-48).

## 8. Swift tests that pin this code

| Test | Where | Pins |
|---|---|---|
| `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` | T/LauncherTests.swift:236-247 | **:240 literal** `[C:\Program Files (x86)\Steam\steam.exe, Z:\Steam Library\My Game\Game.exe, -windowed]`; :243 account |
| `runInPrefixNeverGoesThroughSteamExe` | LauncherTests:249-253 | runinprefix argv |
| `escapeHatchStartsTheGameDirectly`, `missingBridgeStartsTheGameDirectly` | :255-269 | Direct argv |
| `accountFromLaunchOptionsWins` | :271-277 | L2 |
| `steamsLanguageReachesSteamExe` | :279-288 | P1-2 |
| `escapeHatchTakesTheBridgeOutOfThePrefix` | :300-314 | **:311 name list** (Steam folder only) |
| `anyDirectStartTakesLeftoverBridgeFilesOut` | :316-324 | Removal |
| `gameLogsHideTheSteamAccount` | :326-344 | L2 (:336-342) |
| `presenterIsAskedForByDefault` | :364-370 | Matches on the `SteamBridge.steamExe` symbol (:367), so it survives a constant change; `DYLD_INSERT_LIBRARIES` unset (:369) |
| `windowsPathMapsUnixPathsToDriveZ` | T/SteamBridgeTests.swift:5-8 | `windowsPath` |
| `libraryFoldersListsEachLibraryUnderItsParent` | SteamBridgeTests:25-31 | `libraryFolders`, Z: |
| `mostRecentUserIsTheActiveAccount`, `newestTimestampWinsWithoutMostRecent`, `noLoginUsersMeansNoAccount` | SteamBridgeTests:47-64 | L2 (`activeAccountID`) |
| `bridgeNeedsSteamExeAndBothHalvesOfTheClient` | SteamBridgeTests:66-78 | `steamBridgeInstalled` |
| `steamBridgeIsCopiedIntoTheSteamFolder` | T/PrefixManagerTests.swift:213-221 | Steam folder copies |
| `steamFilesComeWithTheBridge` | PrefixManagerTests:223-233 | `writeSteamFiles` and `libraryFolders` |
| `unchangedBridgeFilesAreNotRecopied`, `changedBridgeFileIsRecopied` | :239-263 | `install` |
| steamclient64 choice rows | :265-369 | Unaffected |
| `upToDatePrefixStillGetsTheSteamBridge`, `prefixWithoutBridgeRequestGetsNoSteamFolder` | :371-386 | Unaffected |

These are not affected (checked):
- `copiesDXMTIntoSystem32Only` (PrefixManagerTests:160-168) prepares without the bridge.
- `gameArgumentsPassThroughUnchanged` (LauncherTests:79-84) and `waitForExitAndRunPreparesWaitsRunsThenWaits` (:35-57) run without the bridge, and P0-3 step 1 adds no runner call as long as the link is made with `symlink(2)`, not `wine reg`/`ln`.

Support:
- `steamEnvironment` sets only `STEAM_COMPAT_DATA_PATH`, `SteamAppId` and `PATH` (T/Support.swift:97-99).
- `wineEnvironment(manager, extra)` merges launch options (PrefixManagerTests:14-18).
- `winebootCreatingPrefix` creates only the `pfx` folder, with **no `dosdevices`** (Support:64-73).
- `installFakeSteamBridge` writes `bin/steam.exe` (Support:121-123).

Smoke: Tests/Smoke/smoke.sh:27 installs steam.exe, and `launch()` runs `waitforexitandrun "$WORK/bin/$exe"` with `SteamAppId=0` and no `SteamGameId` (:46-50). Without `SteamGameId`, Proton's flow waits on the child and returns its code (P:steam.c:986-997, 1056-1062), so the smoke rows (exit codes, a d3d11 probe) keep passing. Their exes are console PEs that report through the exit code only.

---

## 9. Wine exports (our tree, W/ = Wine 11.19 + patches)

- **`wine_get_dos_file_name`:**
  - **Exported from kernel32 for every arch, arm64 PE included:** `@ cdecl wine_get_dos_file_name(str)`, W/dlls/kernel32/kernel32.spec:1740, under "Wine internal extensions" (:1733-1740), with no `-arch` limit.
  - Implementation, W/dlls/kernel32/path.c:470-533:
    - it takes a CP_UNIXCP `char *`;
    - an absolute path becomes `\??\unix<path>` (:490-497);
    - `NtOpenFile` walks up to the deepest existing folder (:498-509), so **paths that don't exist still convert** (the tail is appended, :515);
    - `GetFinalPathNameByHandleW(VOLUME_NAME_DOS)` (:512), then `\??\` is stripped (:519-525);
    - the caller frees the result with `HeapFree`.
  - The NT name comes from `get_nt_and_unix_names` (W/dlls/ntdll/unix/file.c:4378-4392) through `find_drive_nt_root` (:4229-4268). That function walks from the full path upward, so the **deepest** drive wins. An `s:` drive therefore gives `S:\steamapps\common\…`, and `drive_c` paths stay `C:`.
  - **mingw:** the function is not in llvm-mingw's `libkernel32.a`, and `CP_UNIXCP` (65010, W/include/winnls.h:369) is not in its headers. So use `GetProcAddress(GetModuleHandleW(L"kernel32"), "wine_get_dos_file_name")`, as the spec says. Pass the path in UTF-8: the Unix code page is UTF-8 on macOS (`unix_cp` defaults to CP_UTF8 at W/dlls/ntdll/unix/env.c:92, and the table loading at :311 is in the non-Apple branch, :216-321). Either define `CP_UNIXCP 65010` or use `CP_UTF8`.
- **`__wine_unix_spawnvp`:**
  - **Exported from ntdll for PE callers:** `@ stdcall __wine_unix_spawnvp(long ptr)`, W/dlls/ntdll/ntdll.spec:1770 ("Unix interface").
  - The PE stub is W/dlls/ntdll/loader.c:3535-3540 (`WINE_UNIX_CALL(unix_wine_spawnvp)`). The Unix side is W/dlls/ntdll/unix/process.c:470-519: `fork` and `execvp`, which **searches PATH** for a bare name; `wait=FALSE` double-forks.
  - Declared only in Wine's W/include/winternl.h:5629, and not in llvm-mingw's `libntdll.a`. So use a typedef plus `GetProcAddress(GetModuleHandleW(L"ntdll"), …)`, as P:steam.c:654-709 does.
  - winebrowser already uses it (W/programs/winebrowser/main.c:87).
- Our Wine has **no builtin steam.exe** (no `programs/steam*`) and no `steam` entry in `loader/wine.inf.in`. So a native PE at `system32\steam.exe` loads, and `wineboot -u` leaves it alone.

---

## 10. P0-3 change map: S: game drive (spec :228-257, Part A :20-27, M1 :563-572, M2 :574-586)

Steps 2 and 3 must land together. Without step 3, steam.exe would pass a Unix path to `CreateProcessW`, and Wine reads `/…` as relative to the current drive.

### Step 1: link `dosdevices/s:` in `PrefixManager.prepare`
- **Where:**
  - In `PrefixManager.prepare` (MNC/PrefixManager.swift:54-103), inside the lock, after `ProfileLinks.apply` (:95-96) and **outside** `if steamBridge` (:97). That way it runs for `run`, `waitforexitandrun` and `getcompatpath`/`getnativepath` (Launcher.swift:86, 93, 121), like Proton's `setup_prefix` (P:proton.py:988-989, 1259) plus the pre-`run` call (:2160-2163).
  - Never for `runinprefix`, matching Proton's `init_session(argv[1] != "runinprefix")` (:2156, 2029).
  - Put the library choice and validation in a pure static function, for example `SteamBridge.gameDriveTarget(environment:) -> String?`, so it can be unit tested without a prefix.
- **Port of Proton (P:proton.py:299-348):**
  - `try_get_game_library_dir` (:311-322) needs both `STEAM_COMPAT_INSTALL_PATH` and `STEAM_COMPAT_LIBRARY_PATHS`. Match by **path component**, not Proton's substring `in` (:319), as the spec says.
  - `get_validated_steamapps_parent` (:299-309):
    - `realpath` the entry;
    - it must be a folder named `steamapps`;
    - its parent must be writable (`access(W_OK)`);
    - it must be on the same `st_dev` as the parent;
    - then the result is the parent.
  - **On failure, Proton returns the entry unchanged (`orig_path`, the steamapps folder), not "no link" (M1).** Either port that (the game then sees `S:\common\Game`) or record "no S:" as a D-line. The maintainer decides (Part A :21-22).
  - `setup_dir_drive` (:330-345):
    - when on and a target exists, create the link, or recreate it if `readlink` differs;
    - when on with no target, remove the link;
    - when off, remove the link.
  - Proton raises if `s:` is not a symlink (`os.readlink` on a real file). Port that as "leave it and log".
- **Opt-out:** `PROTON_SET_GAME_DRIVE`. Proton's `check_environment` (:1630-1637, :1772) uses `nonzero` = `len > 0 and != "0"` (:57-58), which is the whole string. This is not steam.c's first-character `env_nonzero` (P:steam.c:59-63). With the variable unset, the default is on (:1457).
- **Reuse, don't add:**
  - realpath: there are already two private copies, `RuntimeInstaller.realPath` (MNC/RuntimeInstaller.swift:149-154; its comment notes that `URL.resolvingSymlinksInPath` strips `/private`) and `ProfileLinks.resolved` (MNC/ProfileLinks.swift:145-150). Make one internal and use it.
  - Atomic link replacement: `ProfileLinks.replaceLink` (MNC/ProfileLinks.swift:152-161), a new link renamed over the old one.
  - `dosdevices` may be missing (in the fake prefix it always is). Skip or create it.
- **Breaks:** no existing Swift test. No fixture sets `STEAM_COMPAT_INSTALL_PATH`, and `steamFilesComeWithTheBridge` sets only `LIBRARY_PATHS` (PrefixManagerTests:227). No check.sh row breaks either.
- **New Swift tests** (PrefixManagerTests, temp library):
  - the link goes to the library's parent, as realpath gives it (compare with the realpath'd path: temp folders sit under `/var` → `/private/var`);
  - validation fails when the basename isn't `steamapps`, when the parent is read-only, or (simulated) when the devices differ;
  - the link is retargeted, and removed when no library matches;
  - `PROTON_SET_GAME_DRIVE=0` removes it;
  - a real file at `s:` is left alone;
  - a component match beats a prefix (`/Lib/steamapps` doesn't match `/Lib/steamapps2/…`).
- No new hazard from walkers: `dosdevices/z:` → `/` already exists, and the only prefix walkers don't follow links (`FileManager.enumerator`, MNC/OrphanPrefixes.swift:33; MNC/ProfileLinks.swift:120).

### Step 2: the launcher passes the raw Unix target
- **Where:** `Launcher.runGame`, MNC/Launcher.swift:155. Pass `request.target` instead of `SteamBridge.windowsPath(request.target)`.
- **Breaks:**
  - `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` (LauncherTests:240, the `Z:\Steam Library\…` literal).
  - check.sh's comment at :110-113 ("the launcher converts it") becomes untrue.
  - Once step 4 also lands, `windowsPath` has no callers: delete it and `windowsPathMapsUnixPathsToDriveZ` (SteamBridgeTests:5-8).

### Step 3: port the Unix-path branch of `run_process` into bridge/steam.c
- **Where:** replace `child_command_line` (bridge/steam.c:197-210) and the `CreateProcessW` call in `main` (:240) with a `run_process` port (P:steam.c:417-521):
  - argv[0] skipped with `find_quote` (247-265, 426-441);
  - for a leading `/` or `"/`, convert argv[0] with `wine_get_dos_file_name` (§9);
  - `SHGetFileInfoW(SHGFI_EXETYPE)`: when `HIWORD == 0` (a console image), add `CREATE_NEW_CONSOLE` (505-512);
  - rebuild the command line as `"<dos>"` plus the remainder, unchanged (514-520).
- **Fixes over Proton:**
  - Guard `dos == NULL`. Proton calls `SHGetFileInfoW(NULL)` and `wcslen(NULL)` (503-516).
  - Keep `CREATE_SUSPENDED`, and register after the child exists (our deviation, :238-239). The row at :147-158 depends on it.
- **Makefile:30** needs `-lshell32`, plus `-lole32` if `CoInitialize` (505) is kept. `SHGFI_EXETYPE` doesn't need COM.
- **Hazard: `CREATE_NEW_CONSOLE` for console targets.**
  - With that flag and no `STARTF_USESTDHANDLES`, the child does not get the parent's std handles (W/dlls/kernelbase/process.c:195, 206-217). It allocates a **new, windowed** console (W/dlls/kernelbase/console.c:2397-2405), so a console window opens through winemac.
  - helper.exe and steamprobe.exe are console PEs (Makefile:29, 31). Through the launcher, with a Unix path, their stdout no longer reaches the pipe. These go empty:
    - check.sh launcher rows :121 and :122-123 (`lsteam` reads stdout, :118);
    - **wine-arm64/check.sh gate L3 `probe_rows launcher`** (:441-443), which looks for `init: ok`, `steamid ok` and the other lines in the stdout of `probe.sh` tool-folder mode (probe.sh:62-63).
  - Exit-code and marker-file rows still pass.
  - In production, a console-subsystem launch target gets a console window, and its stdout leaves the MacNeutron game log (Launcher.swift:156 `output: gameLog`). Proton does the same on Linux. Count it among M2's regression risks.
  - **Fix the tests, not the port. Options:**
    - (a) build helper.exe with `-mwindows` and write results to a file named by an argument or variable, with a second, console helper only for the "own console" row;
    - (b) launcher rows that need stdout pass a Windows path. This loses the Unix-path coverage, so only for the probe.
- **Breaks once steps 2+3 land:** check.sh launcher rows :121 and :122-123 (stdout through `CREATE_NEW_CONSOLE`), and gate L3 `probe_rows launcher`. The direct rows pass `Z:` Windows paths (:49-50, 63), so they keep today's path.

### Step 4: move `setup_steam_files` into steam.exe
- **Where:**
  - New `setup_steam_files` in bridge/steam.c, ported from P:steam.c:750-816 together with `escape_path_unix_to_dos` (127-148) and `strappend` (150-182) or a simpler fixed buffer.
  - Call it from the `SteamGameId` block, after the registry setup (P:steam.c:961).
  - Delete `PrefixManager.writeSteamFiles` (MNC/PrefixManager.swift:239-250), its call (:99), and `SteamBridge.libraryFolders` (MNC/SteamBridge.swift:28-42).
- **Explicit deviation code (D-8):** `setup_steam_files` reads `STEAM_COMPAT_CLIENT_INSTALL_PATH` as entry 0 (P:steam.c:752, 774-784). The launcher sets that to the bundle's MacOS folder (Launcher.swift:184-186), so a straight port would list it. Leave out the `if (steam_install_path)` block on purpose.
- **Proton bug:**
  - For an empty `STEAM_COMPAT_LIBRARY_PATHS` entry (`a::b`), `lstrcpynA(path, start, 0)` leaves `path` uninitialised before `wine_get_dos_file_name` (P:steam.c:798-800). Skip empty entries, as `libraryFolders` does today (SteamBridgeTests:26).
  - `/steamapps` gives `""`. The relative branch then fails and ERR-skips (path.c:480-485), which matches today's "dropped".
- **Breaks:**
  - Swift: `steamFilesComeWithTheBridge` (PrefixManagerTests:223-233) and `libraryFoldersListsEachLibraryUnderItsParent` (SteamBridgeTests:25-31). Delete both.
  - check.sh :134-144. The vdf and `config` are written only for a `SteamGameId` process (`L()` sets none, :116). Also, with S: in a test that sets `INSTALL_PATH`, the expected text becomes `S:\\`.
  - The `Z:` expectation for libraries that don't exist still holds without S:, because the conversion appends the missing tail (path.c:500-515).
- **New check.sh row (spec :250-254, tool-folder mode only):**
  - Set up `<work>/Lib Ünï/steamapps/common/Game/helper.exe`, with `STEAM_COMPAT_LIBRARY_PATHS` and `STEAM_COMPAT_INSTALL_PATH` set, through `L()`.
  - It needs a new helper mode that prints or writes `GetModuleFileNameW(NULL)`. Today's `spawn`/`breakaway` call it (helper.c:70, 127) but never print it.
  - Expected: `S:\steamapps\common\Game\helper.exe`, arguments unchanged, the vdf lists `S:\\`, and a console helper gets its own console (check with `GetConsoleProcessList` count 1, written to a file).
  - In arm64 mode there is no launcher, so either `ln -s` the link by hand or skip the row.

### Gates (Part A :20-27, M1, M2)
- Before P0-3 can be P0, Part A requires:
  - Proton's fallback or a D-line;
  - `PROTON_SET_GAME_DRIVE=0`, set as `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%` (D-9);
  - the three-appid list, which comes with P1-1's `run_process`;
  - a maintainer smoke on Palworld, SMITE 2 and one launcher-based game.

  Without them, P0-3 is P1.
- Today's working Palworld and SMITE 2 are the "before" for that A/B.
- `PROTON_SET_STEAM_DRIVE` (`t:`, :350-351, :1773) is opt-in in Proton and not in the spec. Skip it.

---

## 11. P1-1 change map: complete the steam.exe port (spec :261-295, M3 :588-592, L5 :652-656, L6 :658-662)

### Step 1: run `system32\steam.exe`, keep the `Steam\` copy, add the three-appid list
- **Where:**
  - `SteamBridge.prefixFiles` (MNC/SteamBridge.swift:23-26) adds `(layout.steamHelper, "drive_c/windows/system32/steam.exe")`.
  - `SteamBridge.steamExe` (:7) becomes `C:\windows\system32\steam.exe`.
  - Add M3's list as a function of `SteamGameId`, which Proton keys on (P:proton.py:2109-2115). For 311210, 1985810 and 1549250, run `C:\Program Files (x86)\Steam\steam.exe`.
  - `Launcher.runGame` (:155) uses it. `CompatContext.appID` is `SteamAppId` (MNC/CompatContext.swift:23); read `env["SteamGameId"]` instead.
  - `PrefixManager.removeSteamBridge` (MNC/PrefixManager.swift:255-261) also removes the `system32` copy. Its list is relative to `Steam/`, so add a full path.
- **Breaks:**
  - `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` (LauncherTests:240, the literal Steam path; the matcher at :239 uses the symbol and survives).
  - `escapeHatchTakesTheBridgeOutOfThePrefix` (:311 lists only `Steam/`; extend it).
  - `steamBridgeIsCopiedIntoTheSteamFolder` (PrefixManagerTests:213-221; add the `system32` expectation).
  - `presenterIsAskedForByDefault` (:367) survives.
- **check.sh:**
  - The launcher rows get `system32` automatically, through `deploySteamBridge`.
  - The direct rows can stay on `Steam\steam.exe` (:47, :51), which is also the `HKCR` target.
  - "The launcher runs the system32 path" becomes observable through L7: once ValvePlatformMutex comes from `GetModuleFileNameW`, `lsteam env ValvePlatformMutex` prints `C:/windows/system32/steam.exe`.
- **L7 ripple:** the direct row :85-86 (which expects the Steam path) still passes if the direct rows keep running `Steam\steam.exe`. The D-13a text changes (spec :485).

### Step 2: ShellExecute, the EA workarounds, `fixup_cmdline_paths`
- **Where:** the `run_process` port from P0-3 step 3. It adds:
  - `get_end_of_excutable_name` and `should_use_shell_execute` (P:steam.c:282-321);
  - the link2ea/steam2ea block (529-578), `try_recover_eadesktop_symlink` (323-362) and the ShellExecute retry/uninstall loop (590-628);
  - `fixup_cmdline_paths` (364-415) on the `CreateProcessW` branch (631);
  - `should_await = !use_shell_execute` (585).
- With ShellExecute there is no child handle. `main` must then:
  - register **before** `ShellExecuteW` (there is no suspended child to wait for);
  - still make itself a system process and wait for the shutdown event when it is a game process. This works because `NtSetInformationProcess` overwrites `wait_handle` (P:steam.c:1003-1005);
  - return 0.
- **Breaks:** none of the existing rows; every target ends in `.exe`, and `C:\missing.exe` still fails in `CreateProcessW` with exit 1.
- **New:** a `.bat` target writes its marker through ShellExecute. Wine runs it through `cmd.exe`.

### Step 3: the command handler, restart, and `HKCR\steam`
- **Where:**
  - `main` moves to `int main(int argc, char *argv[])`, or keeps the wide command line and uses `CommandLineToArgvW`.
  - Port `steam_command_handler` (P:steam.c:652-748) and call it first (928-929).
  - Restart: when `argv[1] == "--"` and `argv[2]` is `steam://launch/<SteamGameId>` or `steam://rungameid/<SteamGameId>`, set `PROTON_STEAM_EXE_RESTART_APP` and return 0 (673-698).
  - Otherwise strip a leading `--`, then:
    - forward `steam://…` URLs;
    - map `-applaunch <id>` to `steam://rungameid/<id>`;
    - log and ignore other `-` options (D-12).
  - The main wait becomes `WaitForMultipleObjects({done, restart_event})` with the restart loop (1013-1047). Fix the precedence bug (§1). Re-running needs `run_process` as a function returning the handle.
  - The `HKCR\steam\shell\open\command` default in `register_steam` (bridge/steam.c:120-124) becomes `"C:\Program Files (x86)\Steam\steam.exe" -- "%1"`, which reaches the `Steam\` copy (A4).
- **Route to open(1): use winebrowser, not `__wine_unix_spawnvp("/usr/bin/open")`.** Both are allowed (spec :281). The difference is testability:
  - check.sh's stub is reached only through winebrowser's `Browsers` key (:52-61). An absolute `/usr/bin/open` can't be intercepted, so check.sh would open the real Mac Steam.
  - A bare `open` through spawnvp would search PATH (W/dlls/ntdll/unix/process.c:491-494). That needs `PATH=$WORK/bin:…` in check.sh, and a stray `open` in a player's PATH could win.
  - Recommended: steam.exe runs `CreateProcessW(L"C:\\windows\\system32\\winebrowser.exe \"<url>\"")`. It must not ShellExecute the URL, because `HKCR\steam` now points back at steam.exe, which would loop.
  - That keeps today's handler, the stub and the `xdg-open`-first default (winebrowser main.c:116-123). Record it in D-12.
- **No-argument case:** Proton forwards a bare `steam` (argc 1 skips the guard at 670). Ours prints usage and exits 1 (:221-225). Under D-12 it is "log, exit 0". The maintainer decides.
- **Breaks:**
  - check.sh :95-99. The row passes only if this run rewrites `HKCR\steam`, which needs `SteamGameId` once step 4 gates `register_steam` (:96 deletes the key first). The new chain is helper → ShellExecute → `Steam\steam.exe -- "steam://store/480"` → handler → winebrowser → stub. With the spawnvp route it would hit the real `/usr/bin/open`.
- **New rows:**
  - `steam.exe steam://store/480` reaches the stub.
  - **Restart (L6):** a new helper mode counts its starts in a file. On the first start it runs `Steam\steam.exe -- steam://rungameid/$SteamGameId` (`CreateProcessW`, no wait) and **exits right away**. On the second it exits. The expected count is 2.
    - The handler's process startup comes well after the child's exit. Proton refuses to restart while the child runs (P:steam.c:1037-1043), and the auto-reset event is then lost, so a child that waits for the handler can never pass (L6).
    - The handler process also keeps the prefix alive, so the shutdown event doesn't fire before the restart.

### Step 4: `SteamGameId` gate, the window, `PROTON_WAIT_ATTACH`; GamingRepair waits
- **Where:**
  - In `main`, wrap the events (:249-250), `register_steam` (:245), `SteamPath`/`ValvePlatformMutex` (:228-230), the DIPC thread (:252-254) and make-process-system (:260-262) in `if (getenv("SteamGameId"))` (P:steam.c:931-980, 999-1005). Any value counts, "0" included.
  - Add `create_steam_windows` (92-111, both windows) on a thread, after `GetDesktopWindow()` on the main thread (941-944). This needs `-luser32`.
  - Add `PROTON_WAIT_ATTACH` (963-977, with `is_ptraced` 904-914, `ProcessWineUnixDebuggerPid` 1100).
  - **GamingRepair: no (L5).** It only opens a key that install scripts create; it waits for P1-4.
  - `unregister_steam` stays (ours). The direct row :77 checks it, and Proton doesn't clear the pid. Document it as a deviation, or drop it and the row.
- **Breaks: check.sh as a whole.** With no `SteamGameId` in check.sh's environment:
  - direct rows :72-73, :74-76, :80-83, :84, :85-86, :91, :92-94, :95-99 and :147-158 fail;
  - launcher rows :122-123, :124-126, :130-133 and :134-144 fail;
  - :127 passes vacuously, since the pid is never written.

  Minimal fix:
  - export `SteamGameId` once, next to `WINEDEBUG` (:42), and in `L()` (:116);
  - add the spec's row "without `SteamGameId`, no registry values are written" with `env -u SteamGameId`;
  - add `FindWindowW(L"vguiPopupWindow", L"Steam")` as a helper mode.

  Production is unaffected: Mac Steam sets `SteamGameId` (game-log headers). Smoke keeps working through the child-wait path.

### Step 5 (later, WoW64)
- A builtin steam.exe, `steam.exe=b` in `LaunchEnvironment.build`'s overrides (MNC/LaunchEnvironment.swift:16-17) with the Locoland 352130 exception (P:proton.py:1514-1525), and SteamService.exe at `Steam\steam.exe`. Then `prefixFiles` must stop mapping the helper there (spec :446).
- Not now: Wine ignores non-builtins under `=b` (spec :525).

### Adjacent binding item touching the same rows: L2 (drop `ActiveUser`)
- Code:
  - bridge/steam.c:104-105;
  - `Launcher.addSteamClient`'s `loginusers.vdf` read (MNC/Launcher.swift:192-197);
  - `MACNEUTRON_STEAM_ACCOUNT` in `redactedKeys` (:202; keep it only if a launch option can still set it).
- Tests:
  - helper.c:54 `user=`;
  - check.sh :72-73 and :122-123 (`user=12345` → drop), :116 (variable);
  - LauncherTests :243, `accountFromLaunchOptionsWins` :271-277, `gameLogsHideTheSteamAccount` :336-342;
  - SteamBridgeTests :47-64 (with `SteamLocation.activeAccountID`, MNC/SteamLocation.swift:64);
  - bridge/probe.sh:6, 14, 59 (`ACCOUNT`).
- Landing it with P1-1 avoids editing the same expectations twice.

---

## 12. Summary: which change breaks which existing test

| Change | File / function | Existing tests broken |
|---|---|---|
| P0-3.1 s: link | `PrefixManager.prepare` (+ a static chooser) | None |
| P0-3.2 raw target | `Launcher.runGame` :155 | `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` (:240); `windowsPathMapsUnixPathsToDriveZ` (dead code once 3.4 lands) |
| P0-3.3 Unix branch | bridge/steam.c `run_process` (replaces :197-210, :240); Makefile:30 | check.sh launcher rows :121, :122-123; wine-arm64/check.sh L3 `probe_rows launcher` (:441-443), both through `CREATE_NEW_CONSOLE` |
| P0-3.4 steam files | bridge/steam.c `setup_steam_files`; remove `PrefixManager.writeSteamFiles`, `SteamBridge.libraryFolders` | `steamFilesComeWithTheBridge`, `libraryFoldersListsEachLibraryUnderItsParent`; check.sh :134-144 (with the `SteamGameId` gate, or with S: set) |
| P1-1.1 system32 + appids | `SteamBridge.steamExe`/`prefixFiles`, `Launcher.runGame`, `PrefixManager.removeSteamBridge` | `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` (:240); extend `escapeHatchTakesTheBridgeOutOfThePrefix` (:311) and `steamBridgeIsCopiedIntoTheSteamFolder` |
| P1-1.2 ShellExecute | steam.c `run_process`, `main` | None |
| P1-1.3 handler/restart/HKCR | steam.c `steam_command_handler`, `main` wait, `register_steam` :120-124 | check.sh :95-99 (passes only with the winebrowser route and `SteamGameId`) |
| P1-1.4 SteamGameId gate | steam.c `main` | check.sh :72-99 (except :69-71, :77-79, :87-90), :122-144, :147-158, until check.sh exports `SteamGameId` |
| L7 (optional with P1-1.1) | steam.c :229-230 | Launcher rows gain an observable; direct :85-86 unchanged while they run `Steam\` |
| L2 | steam.c :104-105, Launcher :192-197 | Listed in §11 |
